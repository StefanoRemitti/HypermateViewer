#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "core/config.hpp"
#include "network/arp_discovery.hpp"
#include "network/interface_discovery.hpp"
#include "output/formatter.hpp"
#include "parser/ifconfig_parser.hpp"
#include "scanner/port_scanner.hpp"
#include "ssh/ssh_client.hpp"
#include "threading/thread_pool.hpp"

namespace {

using devdisc::Config;
using devdisc::ExitCode;

void print_usage() {
    std::cout <<
        R"(device-discovery - find the directly connected Linux device and report its IPv4 interfaces

Usage:
  DEVICE_SSH_PASSWORD='...' device-discovery [options]

Options:
  --threads N              worker threads used for probing (default 32)
  --ssh-user USER          SSH user name (default root)
  --ssh-password-env VAR   environment variable holding the password
                           (default DEVICE_SSH_PASSWORD)
  --timeout MS             TCP connect timeout in milliseconds (default 750)
  --banner-timeout MS      SSH banner read timeout (default 1000)
  --auth-timeout MS        SSH authentication timeout (default 5000)
  --exec-timeout MS        SSH command timeout (default 5000)
  --l2-timeout MS          layer-2 listening window (default 8000)
  --host-key-policy P      off | known-hosts (default off, see README)
  --known-hosts PATH       known_hosts file for --host-key-policy known-hosts
  --select IP              continue with this candidate when several are found
  --interface NAME         override interface auto-detection (troubleshooting)
  --json                   machine readable output
  --verbose                progress diagnostics on stderr
  -h, --help               this help

The subnet and the target IP are discovered automatically; there are
deliberately no --subnet or --target-ip options.
)";
}

bool parse_int(const char* text, int* out) {
    if (text == nullptr) {
        return false;
    }
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || value <= 0 || value > 3600000) {
        return false;
    }
    *out = static_cast<int>(value);
    return true;
}

int fail(const Config& config, const std::string& message, ExitCode code) {
    if (config.json_output) {
        std::cout << devdisc::format_json_error(message, code);
    } else {
        std::cerr << "error: " << message << "\n";
    }
    return code;
}

void verbose(const Config& config, const std::string& message) {
    if (config.verbose) {
        std::cerr << "[*] " << message << "\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    Config config;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const char* value = (i + 1 < argc) ? argv[i + 1] : nullptr;
        auto need_value = [&](int* target) {
            if (!parse_int(value, target)) {
                std::cerr << "error: " << arg << " requires a positive integer\n";
                std::exit(ExitCode::kUsageError);
            }
            ++i;
        };

        if (arg == "-h" || arg == "--help") {
            print_usage();
            return ExitCode::kOk;
        } else if (arg == "--json") {
            config.json_output = true;
        } else if (arg == "--verbose") {
            config.verbose = true;
        } else if (arg == "--threads") {
            int threads = 0;
            need_value(&threads);
            config.threads = static_cast<unsigned int>(threads);
        } else if (arg == "--timeout") {
            need_value(&config.tcp_connect_timeout_ms);
        } else if (arg == "--banner-timeout") {
            need_value(&config.ssh_banner_timeout_ms);
        } else if (arg == "--auth-timeout") {
            need_value(&config.ssh_auth_timeout_ms);
        } else if (arg == "--exec-timeout") {
            need_value(&config.ssh_exec_timeout_ms);
        } else if (arg == "--l2-timeout") {
            need_value(&config.l2_discovery_timeout_ms);
        } else if (arg == "--ssh-user" || arg == "--ssh-password-env" || arg == "--select" ||
                   arg == "--interface" || arg == "--host-key-policy" || arg == "--known-hosts") {
            if (value == nullptr) {
                std::cerr << "error: " << arg << " requires a value\n";
                return ExitCode::kUsageError;
            }
            if (arg == "--ssh-user") {
                config.ssh_user = value;
            } else if (arg == "--ssh-password-env") {
                config.ssh_password_env = value;
            } else if (arg == "--select") {
                config.select_ip = value;
            } else if (arg == "--interface") {
                config.interface_override = value;
            } else if (arg == "--host-key-policy") {
                config.host_key_policy = value;
            } else {
                config.known_hosts_path = value;
            }
            ++i;
        } else {
            std::cerr << "error: unknown argument '" << arg << "'\n";
            print_usage();
            return ExitCode::kUsageError;
        }
    }

    if (config.host_key_policy != "off" && config.host_key_policy != "known-hosts") {
        return fail(config, "--host-key-policy must be 'off' or 'known-hosts'",
                    ExitCode::kUsageError);
    }

    // The password is only ever read from the environment so it never appears in
    // the process command line.
    const char* password_raw = std::getenv(config.ssh_password_env.c_str());
    if (password_raw == nullptr || *password_raw == '\0') {
        return fail(config,
                    "SSH password not configured: set the environment variable " +
                        config.ssh_password_env,
                    ExitCode::kConfigError);
    }
    const std::string password(password_raw);
    // Best effort: keep the secret out of the environment of child processes.
    ::unsetenv(config.ssh_password_env.c_str());

    // ---------------------------------------------------------------- step 1
    devdisc::InterfaceInfo iface;
    {
        const std::vector<devdisc::InterfaceInfo> all = devdisc::enumerate_interfaces();
        if (!config.interface_override.empty()) {
            bool found = false;
            for (const devdisc::InterfaceInfo& candidate : all) {
                if (candidate.name == config.interface_override && candidate.has_ipv4) {
                    iface = candidate;
                    found = true;
                    break;
                }
            }
            if (!found) {
                return fail(config,
                            "interface '" + config.interface_override +
                                "' does not exist or has no IPv4 address",
                            ExitCode::kNoInterface);
            }
        } else {
            const devdisc::InterfaceSelection selection = devdisc::select_interface(all);
            if (selection.status == devdisc::SelectionStatus::kNoCandidate) {
                return fail(config, selection.message, ExitCode::kNoInterface);
            }
            if (selection.status == devdisc::SelectionStatus::kMultipleCandidates) {
                return fail(config, selection.message, ExitCode::kMultipleInterfaces);
            }
            iface = selection.selected;
        }
    }
    verbose(config, "using interface " + iface.name + " (" + iface.ipv4 + "/" +
                        std::to_string(devdisc::prefix_length_from_netmask([&iface]() {
                            uint32_t mask = 0;
                            devdisc::parse_ipv4(iface.netmask, &mask);
                            return mask;
                        }())) +
                        ")");

    devdisc::ThreadPool pool(config.threads);

    // ---------------------------------------------------------- step 2 (A)
    std::vector<devdisc::SshCandidate> candidates;
    std::string stage = "subnet-scan";
    std::vector<std::string> warnings;

    const std::vector<std::string> hosts =
        devdisc::enumerate_subnet_hosts(iface.ipv4, iface.netmask, config.max_scan_hosts);
    if (hosts.empty()) {
        warnings.push_back("the local subnet on " + iface.name +
                           " is empty or too large to sweep; relying on layer-2 discovery");
    } else {
        verbose(config, "stage A: probing " + std::to_string(hosts.size()) +
                            " addresses on the directly connected subnet");
        const devdisc::ScanReport report = devdisc::scan_for_ssh(pool, hosts, config);
        candidates = report.ssh_devices;
        for (const devdisc::ProbeOutcome& outcome : report.open_not_ssh) {
            warnings.push_back(outcome.ip + ": " + outcome.detail);
        }
    }

    // ---------------------------------------------------------- step 3 (B)
    if (candidates.empty()) {
        verbose(config, "stage A found nothing; falling back to layer-2 discovery on " + iface.name);
        stage = "layer2-arp";
        const devdisc::L2DiscoveryResult l2 =
            devdisc::discover_link_neighbours(iface.name, {iface.ipv4}, config);
        if (l2.status == devdisc::L2Status::kPermissionDenied) {
            return fail(config, "no device discovered on the connected subnet and " + l2.message,
                        ExitCode::kNoDeviceDiscovered);
        }
        if (l2.neighbours.empty()) {
            return fail(config,
                        "no device discovered: the subnet sweep on " + iface.name +
                            " found no SSH server and layer-2 discovery observed no IPv4 traffic "
                            "on the link (" +
                            l2.message +
                            "). The device may be silent; check cabling/link state or power-cycle "
                            "the device so that it emits ARP traffic.",
                        ExitCode::kNoDeviceDiscovered);
        }

        std::vector<std::string> l2_targets;
        for (const devdisc::L2Neighbour& neighbour : l2.neighbours) {
            verbose(config, "layer-2 candidate " + neighbour.ipv4 + " (" + neighbour.mac + ", via " +
                                neighbour.source + ")");
            l2_targets.push_back(neighbour.ipv4);
        }
        const devdisc::ScanReport report = devdisc::scan_for_ssh(pool, l2_targets, config);
        candidates = report.ssh_devices;
        for (const devdisc::ProbeOutcome& outcome : report.open_not_ssh) {
            warnings.push_back(outcome.ip + ": " + outcome.detail);
        }
        if (candidates.empty()) {
            std::string detail;
            for (const devdisc::L2Neighbour& neighbour : l2.neighbours) {
                detail += " " + neighbour.ipv4 + " (" + neighbour.mac + ")";
            }
            return fail(config,
                        "device found at layer 2 but no SSH server could be reached. Observed "
                        "addresses:" +
                            detail +
                            ". If the address is outside the local subnet the PC has no route to "
                            "it; add a temporary address on " +
                            iface.name + " in that subnet and retry.",
                        ExitCode::kNoDeviceDiscovered);
        }
    }

    // -------------------------------------------------------------- step 4
    if (candidates.size() > 1) {
        if (config.select_ip.empty()) {
            std::string message = "multiple SSH devices discovered; refusing to pick one. Use "
                                  "--select IP to continue. Candidates:";
            for (const devdisc::SshCandidate& candidate : candidates) {
                message += "\n  " + candidate.ip + "  " + candidate.banner;
            }
            if (config.json_output) {
                std::cout << devdisc::format_json_error(message, ExitCode::kMultipleDevices);
            } else {
                std::cerr << "WARNING: multiple SSH devices discovered\n";
                for (const devdisc::SshCandidate& candidate : candidates) {
                    std::cerr << "  " << candidate.ip << "  " << candidate.banner << "\n";
                }
                std::cerr << "error: refusing to choose automatically; rerun with --select IP\n";
            }
            return ExitCode::kMultipleDevices;
        }
        auto chosen = std::find_if(candidates.begin(), candidates.end(),
                                   [&config](const devdisc::SshCandidate& candidate) {
                                       return candidate.ip == config.select_ip;
                                   });
        if (chosen == candidates.end()) {
            return fail(config, "--select " + config.select_ip + " is not one of the discovered SSH devices",
                        ExitCode::kMultipleDevices);
        }
        const devdisc::SshCandidate selected = *chosen;
        candidates.clear();
        candidates.push_back(selected);
    }

    const devdisc::SshCandidate target = candidates.front();
    verbose(config, "SSH server confirmed at " + target.ip + " (" + target.banner + ")");

    // -------------------------------------------------------------- step 5
    devdisc::SshLibrary library;
    if (!library.ok()) {
        return fail(config, "could not initialise libssh2", ExitCode::kSshConnectFailed);
    }

    devdisc::SshClient client(target.ip, config);
    std::string ssh_error;
    const devdisc::SshStatus status =
        client.connect_and_authenticate(config.ssh_user, password, &ssh_error);
    if (status == devdisc::SshStatus::kAuthFailed) {
        return fail(config, ssh_error, ExitCode::kSshAuthFailed);
    }
    if (status != devdisc::SshStatus::kOk) {
        return fail(config, "SSH connection failed: " + ssh_error, ExitCode::kSshConnectFailed);
    }
    if (config.host_key_policy == "off") {
        verbose(config, "host-key verification is disabled; server key " +
                            client.host_key_fingerprint());
    }

    // -------------------------------------------------------------- step 6
    devdisc::SshCommandResult command = client.run_command("ifconfig -a");
    if (command.status != devdisc::SshStatus::kOk) {
        return fail(config, command.error, ExitCode::kCommandFailed);
    }
    std::vector<devdisc::RemoteInterface> interfaces;
    if (command.exit_code == 0) {
        interfaces = devdisc::parse_ifconfig(command.stdout_data);
    }
    if (command.exit_code != 0 || interfaces.empty()) {
        warnings.push_back("ifconfig failed or produced no usable output (exit " +
                           std::to_string(command.exit_code) + "); falling back to 'ip -4 addr'");
        verbose(config, "ifconfig unusable, retrying with 'ip -4 addr'");
        devdisc::SshCommandResult fallback = client.run_command("ip -4 addr show");
        if (fallback.status != devdisc::SshStatus::kOk || fallback.exit_code != 0) {
            client.disconnect();
            return fail(config,
                        "could not execute 'ifconfig' nor 'ip -4 addr' on the device: " +
                            (fallback.error.empty() ? fallback.stderr_data : fallback.error),
                        ExitCode::kCommandFailed);
        }
        interfaces = devdisc::parse_ip_addr(fallback.stdout_data);
    }
    client.disconnect();

    // -------------------------------------------------------------- step 7
    if (interfaces.empty()) {
        return fail(config, "could not parse any IPv4 network interface from the device output",
                    ExitCode::kParseFailed);
    }
    int exit_code = ExitCode::kOk;
    if (interfaces.size() != 2) {
        warnings.push_back("expected two non-loopback IPv4 interfaces but found " +
                           std::to_string(interfaces.size()));
        exit_code = ExitCode::kUnexpectedInterfaceCount;
    }

    devdisc::DiscoveryReport report;
    report.ssh_ip = target.ip;
    report.ssh_banner = target.banner;
    report.host_key_fingerprint = client.host_key_fingerprint();
    report.discovery_stage = stage;
    report.interfaces = interfaces;
    report.warnings = warnings;

    std::cout << (config.json_output ? devdisc::format_json(report) : devdisc::format_human(report));
    return exit_code;
}
