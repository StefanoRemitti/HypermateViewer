#include "network/interface_discovery.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <sstream>

namespace devdisc {
namespace {

const std::array<const char*, 14> kVirtualPrefixes = {
    "lo",     "docker", "br-",   "virbr", "veth", "vmnet", "vboxnet",
    "tun",    "tap",    "wg",    "ppp",   "zt",   "tailscale", "bond",
};

}  // namespace

bool is_virtual_interface_name(const std::string& name) {
    for (const char* prefix : kVirtualPrefixes) {
        const std::size_t len = std::strlen(prefix);
        if (name.size() >= len && name.compare(0, len, prefix) == 0) {
            // "bond" and "br-" style names are virtual; a physical name such as
            // "enp1s0" never matches these prefixes.
            return true;
        }
    }
    return false;
}

bool parse_ipv4(const std::string& text, uint32_t* out_host_order) {
    in_addr addr{};
    if (inet_pton(AF_INET, text.c_str(), &addr) != 1) {
        return false;
    }
    if (out_host_order != nullptr) {
        *out_host_order = ntohl(addr.s_addr);
    }
    return true;
}

std::string ipv4_to_string(uint32_t host_order) {
    in_addr addr{};
    addr.s_addr = htonl(host_order);
    char buffer[INET_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET, &addr, buffer, sizeof(buffer)) == nullptr) {
        return {};
    }
    return std::string(buffer);
}

int prefix_length_from_netmask(uint32_t netmask_host_order) {
    int bits = 0;
    for (int i = 31; i >= 0; --i) {
        if ((netmask_host_order >> i) & 1U) {
            ++bits;
        } else {
            break;
        }
    }
    return bits;
}

InterfaceSelection select_interface(const std::vector<InterfaceInfo>& interfaces) {
    InterfaceSelection result;
    for (const InterfaceInfo& iface : interfaces) {
        if (iface.is_loopback) {
            continue;
        }
        if (!iface.is_up || !iface.is_running) {
            continue;
        }
        if (!iface.has_ipv4 || iface.ipv4.empty()) {
            continue;
        }
        if (is_virtual_interface_name(iface.name)) {
            continue;
        }
        result.candidates.push_back(iface);
    }

    if (result.candidates.empty()) {
        result.status = SelectionStatus::kNoCandidate;
        result.message =
            "No suitable network interface found: no interface is up, has an IPv4 "
            "address and looks like a physical link.";
        return result;
    }
    if (result.candidates.size() > 1) {
        std::ostringstream oss;
        oss << "Multiple candidate interfaces found; refusing to guess. Candidates:";
        for (const InterfaceInfo& iface : result.candidates) {
            oss << "\n  " << iface.name << " " << iface.ipv4 << "/"
                << prefix_length_from_netmask([&iface]() {
                       uint32_t mask = 0;
                       parse_ipv4(iface.netmask, &mask);
                       return mask;
                   }());
        }
        result.status = SelectionStatus::kMultipleCandidates;
        result.message = oss.str();
        return result;
    }

    result.status = SelectionStatus::kOk;
    result.selected = result.candidates.front();
    return result;
}

std::vector<InterfaceInfo> enumerate_interfaces() {
    std::vector<InterfaceInfo> result;
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) {
        return result;
    }

    for (ifaddrs* it = list; it != nullptr; it = it->ifa_next) {
        if (it->ifa_name == nullptr) {
            continue;
        }
        if (it->ifa_addr == nullptr || it->ifa_addr->sa_family != AF_INET) {
            // Remember the interface even without IPv4 so that diagnostics can
            // mention it, but only once.
            const std::string name(it->ifa_name);
            const bool known = std::any_of(result.begin(), result.end(),
                                           [&name](const InterfaceInfo& i) { return i.name == name; });
            if (!known) {
                InterfaceInfo info;
                info.name = name;
                info.is_up = (it->ifa_flags & IFF_UP) != 0;
                info.is_running = (it->ifa_flags & IFF_RUNNING) != 0;
                info.is_loopback = (it->ifa_flags & IFF_LOOPBACK) != 0;
                info.is_point_to_point = (it->ifa_flags & IFF_POINTOPOINT) != 0;
                info.has_ipv4 = false;
                result.push_back(info);
            }
            continue;
        }

        InterfaceInfo info;
        info.name = it->ifa_name;
        info.is_up = (it->ifa_flags & IFF_UP) != 0;
        info.is_running = (it->ifa_flags & IFF_RUNNING) != 0;
        info.is_loopback = (it->ifa_flags & IFF_LOOPBACK) != 0;
        info.is_point_to_point = (it->ifa_flags & IFF_POINTOPOINT) != 0;
        info.has_ipv4 = true;

        char buffer[INET_ADDRSTRLEN] = {0};
        const auto* sin = reinterpret_cast<const sockaddr_in*>(it->ifa_addr);
        if (inet_ntop(AF_INET, &sin->sin_addr, buffer, sizeof(buffer)) != nullptr) {
            info.ipv4 = buffer;
        }
        if (it->ifa_netmask != nullptr) {
            const auto* mask = reinterpret_cast<const sockaddr_in*>(it->ifa_netmask);
            char mbuffer[INET_ADDRSTRLEN] = {0};
            if (inet_ntop(AF_INET, &mask->sin_addr, mbuffer, sizeof(mbuffer)) != nullptr) {
                info.netmask = mbuffer;
            }
        }

        // Replace a previously recorded IPv4-less placeholder for this name and
        // keep only the first IPv4 address of an interface: an interface with
        // several addresses is still a single candidate.
        auto existing = std::find_if(result.begin(), result.end(), [&info](const InterfaceInfo& i) {
            return i.name == info.name;
        });
        if (existing != result.end()) {
            if (!existing->has_ipv4) {
                *existing = info;
            }
        } else {
            result.push_back(info);
        }
    }

    freeifaddrs(list);
    return result;
}

InterfaceSelection discover_interface() {
    return select_interface(enumerate_interfaces());
}

}  // namespace devdisc
