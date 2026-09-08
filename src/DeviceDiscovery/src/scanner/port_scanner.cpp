#include "scanner/port_scanner.hpp"

#include <algorithm>
#include <future>

#include "network/interface_discovery.hpp"

namespace devdisc {

std::vector<std::string> enumerate_subnet_hosts(const std::string& ip, const std::string& netmask,
                                                unsigned int max_hosts) {
    std::vector<std::string> hosts;
    uint32_t addr = 0;
    uint32_t mask = 0;
    if (!parse_ipv4(ip, &addr) || !parse_ipv4(netmask, &mask) || mask == 0) {
        return hosts;
    }

    const uint32_t network = addr & mask;
    const uint32_t broadcast = network | ~mask;
    if (broadcast <= network) {
        return hosts;  // /31 or /32: no usable host range to sweep
    }

    const uint64_t count = static_cast<uint64_t>(broadcast) - network - 1;
    if (count == 0 || count > max_hosts) {
        return hosts;  // refuse to sweep an implausibly large "directly connected" link
    }

    hosts.reserve(static_cast<std::size_t>(count));
    for (uint32_t host = network + 1; host < broadcast; ++host) {
        if (host == addr) {
            continue;  // skip ourselves
        }
        hosts.push_back(ipv4_to_string(host));
    }
    return hosts;
}

ScanReport scan_for_ssh(ThreadPool& pool, const std::vector<std::string>& targets,
                        const Config& config) {
    ScanReport report;
    std::vector<std::future<ProbeOutcome>> futures;
    futures.reserve(targets.size());

    for (const std::string& target : targets) {
        futures.push_back(pool.submit(probe_ssh, target, 22, config.tcp_connect_timeout_ms,
                                      config.ssh_banner_timeout_ms));
    }

    for (auto& future : futures) {
        ProbeOutcome outcome;
        try {
            outcome = future.get();
        } catch (const std::exception& ex) {
            outcome.result = ProbeResult::kError;
            outcome.detail = ex.what();
        }
        ++report.probed;
        if (outcome.result == ProbeResult::kSshDetected) {
            report.ssh_devices.push_back(SshCandidate{outcome.ip, outcome.banner});
        } else if (outcome.result == ProbeResult::kNotSsh) {
            report.open_not_ssh.push_back(outcome);
        }
    }

    std::sort(report.ssh_devices.begin(), report.ssh_devices.end(),
              [](const SshCandidate& a, const SshCandidate& b) { return a.ip < b.ip; });
    return report;
}

}  // namespace devdisc
