#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace devdisc {

// A snapshot of one IPv4-capable interface of the local PC.
struct InterfaceInfo {
    std::string name;
    std::string ipv4;     // dotted quad
    std::string netmask;  // dotted quad
    bool is_up = false;
    bool is_running = false;   // IFF_RUNNING: carrier/link present
    bool is_loopback = false;
    bool is_point_to_point = false;
    bool has_ipv4 = false;
};

enum class SelectionStatus {
    kOk,
    kNoCandidate,
    kMultipleCandidates,
};

struct InterfaceSelection {
    SelectionStatus status = SelectionStatus::kNoCandidate;
    InterfaceInfo selected;             // valid when status == kOk
    std::vector<InterfaceInfo> candidates;  // all interfaces that passed filtering
    std::string message;                // human-readable diagnostic
};

// Returns true when the interface name looks like a virtual/software device
// that can never be a directly connected Ethernet link (docker, bridges, vpn,
// wireguard, virtual machine host taps, ...).
bool is_virtual_interface_name(const std::string& name);

// Applies the documented selection policy to an already collected list of
// interfaces. Kept free of syscalls so it is unit testable.
//
// Policy (in order):
//   1. drop loopback interfaces
//   2. drop interfaces that are administratively down or without carrier
//   3. drop interfaces without an IPv4 address
//   4. drop well-known virtual interfaces by name
//   5. exactly one survivor  -> selected
//      zero survivors        -> kNoCandidate
//      more than one         -> kMultipleCandidates (never pick arbitrarily)
InterfaceSelection select_interface(const std::vector<InterfaceInfo>& interfaces);

// Enumerates local interfaces using getifaddrs(3).
std::vector<InterfaceInfo> enumerate_interfaces();

// Convenience: enumerate + select.
InterfaceSelection discover_interface();

// Helpers shared with the scanner.
bool parse_ipv4(const std::string& text, uint32_t* out_host_order);
std::string ipv4_to_string(uint32_t host_order);
int prefix_length_from_netmask(uint32_t netmask_host_order);

}  // namespace devdisc
