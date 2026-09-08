#pragma once

#include <string>

namespace devdisc {

// All tunable defaults live here so that timeouts are configuration values
// rather than magic numbers scattered through the code base.
struct Config {
    // Concurrency
    unsigned int threads = 32;

    // Timeouts (milliseconds)
    int tcp_connect_timeout_ms = 750;
    int ssh_banner_timeout_ms = 1000;
    int ssh_auth_timeout_ms = 5000;
    int ssh_exec_timeout_ms = 5000;

    // Layer-2 fallback discovery: how long we listen on the link (milliseconds).
    int l2_discovery_timeout_ms = 8000;

    // SSH credentials
    std::string ssh_user = "root";
    std::string ssh_password_env = "DEVICE_SSH_PASSWORD";

    // Host key verification: "off" (documented default), "known-hosts" (strict).
    std::string host_key_policy = "off";
    std::string known_hosts_path;  // defaults to ~/.ssh/known_hosts

    // Behaviour
    bool json_output = false;
    bool verbose = false;

    // Optional explicit candidate selection when several SSH devices are found.
    std::string select_ip;

    // Optional interface override (never required; used for troubleshooting).
    std::string interface_override;

    // Largest subnet (in host count) we are willing to sweep in stage A. A
    // directly connected link is always small; anything larger indicates a
    // misconfiguration and is refused instead of scanning the Internet.
    unsigned int max_scan_hosts = 4096;
};

// Meaningful, documented process exit codes.
enum ExitCode : int {
    kOk = 0,
    kNoInterface = 2,
    kMultipleInterfaces = 3,
    kNoDeviceDiscovered = 4,
    kMultipleDevices = 5,
    kSshConnectFailed = 6,
    kSshAuthFailed = 7,
    kCommandFailed = 8,
    kParseFailed = 9,
    kUnexpectedInterfaceCount = 10,
    kUsageError = 64,
    kConfigError = 78,
};

}  // namespace devdisc
