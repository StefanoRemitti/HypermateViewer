#pragma once

#include <string>
#include <vector>

namespace devdisc {

struct RemoteInterface {
    std::string name;
    std::string ipv4;
};

// Parses `ifconfig` output. Supports both the modern net-tools layout
//   eth0: flags=4163<UP,...>  mtu 1500
//           inet 192.168.10.50  netmask 255.255.255.0  broadcast ...
// and the legacy layout
//   eth0      Link encap:Ethernet  HWaddr 00:11:22:33:44:55
//             inet addr:192.168.10.50  Bcast:...  Mask:255.255.255.0
// Loopback interfaces, IPv6 addresses and entries without an IPv4 address are
// skipped.
std::vector<RemoteInterface> parse_ifconfig(const std::string& output);

// Parses `ip -4 addr` output (fallback when ifconfig is unavailable).
std::vector<RemoteInterface> parse_ip_addr(const std::string& output);

// Chooses the correct parser based on the shape of the output.
std::vector<RemoteInterface> parse_interfaces(const std::string& output);

}  // namespace devdisc
