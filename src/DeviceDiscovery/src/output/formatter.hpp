#pragma once

#include <string>
#include <vector>

#include "parser/ifconfig_parser.hpp"
#include "scanner/port_scanner.hpp"

namespace devdisc {

struct DiscoveryReport {
    std::string ssh_ip;
    std::string ssh_banner;
    std::string host_key_fingerprint;
    std::string discovery_stage;  // "subnet-scan" or "layer2-arp"
    std::vector<RemoteInterface> interfaces;
    std::vector<std::string> warnings;
};

std::string format_human(const DiscoveryReport& report);
std::string format_json(const DiscoveryReport& report);

// JSON error document for --json mode. Never contains credentials.
std::string format_json_error(const std::string& message, int exit_code);

std::string json_escape(const std::string& value);

}  // namespace devdisc
