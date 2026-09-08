#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/config.hpp"
#include "scanner/ssh_detector.hpp"
#include "threading/thread_pool.hpp"

namespace devdisc {

struct SshCandidate {
    std::string ip;
    std::string banner;
};

struct ScanReport {
    std::vector<SshCandidate> ssh_devices;   // confirmed by banner
    std::vector<ProbeOutcome> open_not_ssh;  // port 22 open, not an SSH server
    std::size_t probed = 0;
};

// Computes every usable host address of the network ip/netmask (network and
// broadcast addresses excluded, the PC's own address excluded).
std::vector<std::string> enumerate_subnet_hosts(const std::string& ip, const std::string& netmask,
                                                unsigned int max_hosts);

// Stage A: sweep the directly connected IPv4 subnet for SSH servers using the
// supplied thread pool (bounded concurrency, no thread-per-IP).
ScanReport scan_for_ssh(ThreadPool& pool, const std::vector<std::string>& targets,
                        const Config& config);

}  // namespace devdisc
