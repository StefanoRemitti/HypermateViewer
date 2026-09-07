#pragma once

#include <string>
#include <vector>

#include "core/config.hpp"

namespace devdisc {

enum class L2Status {
    kOk,             // at least one IPv4 address observed on the link
    kNoObservation,  // link is up but the device never revealed an IPv4 address
    kPermissionDenied,
    kError,
};

struct L2Neighbour {
    std::string ipv4;
    std::string mac;
    std::string source;  // "neighbour-table", "arp", "ipv4-traffic"
};

struct L2DiscoveryResult {
    L2Status status = L2Status::kError;
    std::vector<L2Neighbour> neighbours;
    std::string message;
};

// Layer-2 discovery restricted to a single directly connected interface.
//
// Stage B1: read the kernel neighbour (ARP) table via netlink RTM_GETNEIGH and
//           keep the entries learned on this interface.
// Stage B2: open an AF_PACKET socket bound to the interface and passively
//           observe ARP frames (sender protocol address) and IPv4 frames
//           (source address). A statically addressed Linux device announces
//           itself through gratuitous ARP, ARP requests for its gateway,
//           mDNS/NetBIOS/broadcast traffic, etc.
// Stage B3: for every observed IPv4 address, send a unicast-targeted ARP
//           request on the link to confirm that the address is really alive.
//
// No traffic is generated outside the given interface and no address space is
// scanned blindly: only addresses actually observed on the link are probed.
L2DiscoveryResult discover_link_neighbours(const std::string& interface_name,
                                           const std::vector<std::string>& local_ipv4,
                                           const Config& config);

}  // namespace devdisc
