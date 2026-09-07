#include "network/arp_discovery.hpp"

// net/if.h must come before the linux/* headers: including them the other way
// round makes glibc and the kernel UAPI headers redefine IFF_* and struct ifreq.
#include <net/if.h>

#include <arpa/inet.h>
#include <errno.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "network/interface_discovery.hpp"

namespace devdisc {
namespace {

class ScopedFd {
public:
    explicit ScopedFd(int fd) : fd_(fd) {}
    ~ScopedFd() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }
    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;
    int get() const { return fd_; }

private:
    int fd_;
};

struct ArpFrame {
    ethhdr eth;
    uint16_t htype;
    uint16_t ptype;
    uint8_t hlen;
    uint8_t plen;
    uint16_t oper;
    uint8_t sender_mac[6];
    uint8_t sender_ip[4];
    uint8_t target_mac[6];
    uint8_t target_ip[4];
} __attribute__((packed));

std::string mac_to_string(const uint8_t* mac) {
    char buffer[18];
    std::snprintf(buffer, sizeof(buffer), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2],
                  mac[3], mac[4], mac[5]);
    return std::string(buffer);
}

int remaining_ms(const std::chrono::steady_clock::time_point& deadline) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
        return 0;
    }
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
}

void add_neighbour(std::vector<L2Neighbour>& out, const std::vector<std::string>& local_ipv4,
                   const std::string& ip, const std::string& mac, const char* source) {
    if (ip.empty() || ip == "0.0.0.0" || ip == "255.255.255.255") {
        return;
    }
    uint32_t host = 0;
    if (!parse_ipv4(ip, &host)) {
        return;
    }
    // Ignore multicast (224/4), broadcast-ish and our own addresses.
    if ((host >> 28) == 0xE || (host >> 24) == 127) {
        return;
    }
    if (std::find(local_ipv4.begin(), local_ipv4.end(), ip) != local_ipv4.end()) {
        return;
    }
    auto existing = std::find_if(out.begin(), out.end(),
                                 [&ip](const L2Neighbour& n) { return n.ipv4 == ip; });
    if (existing != out.end()) {
        if (existing->mac.empty() && !mac.empty()) {
            existing->mac = mac;
        }
        return;
    }
    out.push_back(L2Neighbour{ip, mac, source});
}

// Stage B1 - kernel neighbour table via netlink.
bool read_neighbour_table(unsigned int ifindex, const std::vector<std::string>& local_ipv4,
                          std::vector<L2Neighbour>& out, std::string* error) {
    ScopedFd sock(::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE));
    if (sock.get() < 0) {
        *error = std::string("netlink socket(): ") + std::strerror(errno);
        return false;
    }

    struct {
        nlmsghdr header;
        ndmsg message;
    } request{};
    request.header.nlmsg_len = NLMSG_LENGTH(sizeof(ndmsg));
    request.header.nlmsg_type = RTM_GETNEIGH;
    request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    request.header.nlmsg_seq = 1;
    request.message.ndm_family = AF_INET;

    if (::send(sock.get(), &request, request.header.nlmsg_len, 0) < 0) {
        *error = std::string("netlink send(): ") + std::strerror(errno);
        return false;
    }

    std::vector<char> buffer(16384);
    bool done = false;
    while (!done) {
        pollfd pfd{};
        pfd.fd = sock.get();
        pfd.events = POLLIN;
        const int poll_rc = ::poll(&pfd, 1, 1000);
        if (poll_rc <= 0) {
            break;
        }
        const ssize_t received = ::recv(sock.get(), buffer.data(), buffer.size(), 0);
        if (received <= 0) {
            break;
        }
        int len = static_cast<int>(received);
        for (nlmsghdr* nh = reinterpret_cast<nlmsghdr*>(buffer.data());
             NLMSG_OK(nh, static_cast<unsigned int>(len)); nh = NLMSG_NEXT(nh, len)) {
            if (nh->nlmsg_type == NLMSG_DONE) {
                done = true;
                break;
            }
            if (nh->nlmsg_type == NLMSG_ERROR) {
                *error = "netlink returned an error while dumping the neighbour table";
                return false;
            }
            if (nh->nlmsg_type != RTM_NEWNEIGH) {
                continue;
            }
            const auto* nd = static_cast<const ndmsg*>(NLMSG_DATA(nh));
            if (nd->ndm_family != AF_INET ||
                static_cast<unsigned int>(nd->ndm_ifindex) != ifindex) {
                continue;
            }
            if ((nd->ndm_state & (NUD_FAILED | NUD_INCOMPLETE | NUD_NOARP)) != 0) {
                continue;
            }
            std::string ip;
            std::string mac;
            int attr_len = static_cast<int>(nh->nlmsg_len - NLMSG_LENGTH(sizeof(ndmsg)));
            for (const rtattr* rta = reinterpret_cast<const rtattr*>(
                     reinterpret_cast<const char*>(nd) + NLMSG_ALIGN(sizeof(ndmsg)));
                 RTA_OK(rta, attr_len); rta = RTA_NEXT(rta, attr_len)) {
                if (rta->rta_type == NDA_DST && RTA_PAYLOAD(rta) == 4) {
                    const auto* raw = static_cast<const uint8_t*>(RTA_DATA(rta));
                    char text[INET_ADDRSTRLEN] = {0};
                    if (inet_ntop(AF_INET, raw, text, sizeof(text)) != nullptr) {
                        ip = text;
                    }
                } else if (rta->rta_type == NDA_LLADDR && RTA_PAYLOAD(rta) == 6) {
                    mac = mac_to_string(static_cast<const uint8_t*>(RTA_DATA(rta)));
                }
            }
            add_neighbour(out, local_ipv4, ip, mac, "neighbour-table");
        }
    }
    return true;
}

bool get_hw_address(int fd, const std::string& interface_name, uint8_t* mac) {
    ifreq req{};
    std::strncpy(req.ifr_name, interface_name.c_str(), IFNAMSIZ - 1);
    if (::ioctl(fd, SIOCGIFHWADDR, &req) != 0) {
        return false;
    }
    std::memcpy(mac, req.ifr_hwaddr.sa_data, 6);
    return true;
}

void send_arp_request(int fd, unsigned int ifindex, const uint8_t* src_mac, uint32_t src_ip,
                      uint32_t target_ip) {
    ArpFrame frame{};
    std::memset(frame.eth.h_dest, 0xFF, 6);
    std::memcpy(frame.eth.h_source, src_mac, 6);
    frame.eth.h_proto = htons(ETH_P_ARP);
    frame.htype = htons(ARPHRD_ETHER);
    frame.ptype = htons(ETH_P_IP);
    frame.hlen = 6;
    frame.plen = 4;
    frame.oper = htons(ARPOP_REQUEST);
    std::memcpy(frame.sender_mac, src_mac, 6);
    const uint32_t src_net = htonl(src_ip);
    const uint32_t dst_net = htonl(target_ip);
    std::memcpy(frame.sender_ip, &src_net, 4);
    std::memcpy(frame.target_ip, &dst_net, 4);

    sockaddr_ll dest{};
    dest.sll_family = AF_PACKET;
    dest.sll_protocol = htons(ETH_P_ARP);
    dest.sll_ifindex = static_cast<int>(ifindex);
    dest.sll_halen = 6;
    std::memset(dest.sll_addr, 0xFF, 6);

    (void)::sendto(fd, &frame, sizeof(frame), 0, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
}

}  // namespace

L2DiscoveryResult discover_link_neighbours(const std::string& interface_name,
                                           const std::vector<std::string>& local_ipv4,
                                           const Config& config) {
    L2DiscoveryResult result;
    const unsigned int ifindex = if_nametoindex(interface_name.c_str());
    if (ifindex == 0) {
        result.status = L2Status::kError;
        result.message = "interface '" + interface_name + "' disappeared before layer-2 discovery";
        return result;
    }

    // Stage B1: whatever the kernel already knows about this link.
    std::string netlink_error;
    if (!read_neighbour_table(ifindex, local_ipv4, result.neighbours, &netlink_error)) {
        result.message = netlink_error;
    }

    // Stage B2/B3 need raw packet access.
    ScopedFd packet_sock(
        ::socket(AF_PACKET, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, htons(ETH_P_ALL)));
    if (packet_sock.get() < 0) {
        if (errno == EPERM || errno == EACCES) {
            result.status = result.neighbours.empty() ? L2Status::kPermissionDenied : L2Status::kOk;
            result.message =
                "layer-2 discovery needs raw packet access (run as root or grant "
                "CAP_NET_RAW+CAP_NET_ADMIN: sudo setcap cap_net_raw,cap_net_admin+eip "
                "./device-discovery)";
            return result;
        }
        result.status = result.neighbours.empty() ? L2Status::kError : L2Status::kOk;
        result.message = std::string("AF_PACKET socket(): ") + std::strerror(errno);
        return result;
    }

    sockaddr_ll bind_addr{};
    bind_addr.sll_family = AF_PACKET;
    bind_addr.sll_protocol = htons(ETH_P_ALL);
    bind_addr.sll_ifindex = static_cast<int>(ifindex);
    if (::bind(packet_sock.get(), reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) != 0) {
        result.status = result.neighbours.empty() ? L2Status::kError : L2Status::kOk;
        result.message = std::string("bind(AF_PACKET): ") + std::strerror(errno);
        return result;
    }

    uint8_t src_mac[6] = {0};
    const bool have_mac = get_hw_address(packet_sock.get(), interface_name, src_mac);
    uint32_t src_ip = 0;
    if (!local_ipv4.empty()) {
        parse_ipv4(local_ipv4.front(), &src_ip);
    }

    // Stage B3 (first pass): re-probe addresses the kernel already knows so
    // that stale entries are refreshed while we listen.
    if (have_mac) {
        for (const L2Neighbour& neighbour : result.neighbours) {
            uint32_t target = 0;
            if (parse_ipv4(neighbour.ipv4, &target)) {
                send_arp_request(packet_sock.get(), ifindex, src_mac, src_ip, target);
            }
        }
    }

    // Stage B2: passively observe the link. A directly connected Linux device
    // reveals its static address through ARP or any IPv4 packet it emits.
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(config.l2_discovery_timeout_ms);
    std::vector<uint8_t> buffer(2048);
    while (remaining_ms(deadline) > 0) {
        pollfd pfd{};
        pfd.fd = packet_sock.get();
        pfd.events = POLLIN;
        const int poll_rc = ::poll(&pfd, 1, remaining_ms(deadline));
        if (poll_rc == 0) {
            break;
        }
        if (poll_rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        const ssize_t len = ::recv(packet_sock.get(), buffer.data(), buffer.size(), 0);
        if (len < static_cast<ssize_t>(sizeof(ethhdr))) {
            continue;
        }
        const auto* eth = reinterpret_cast<const ethhdr*>(buffer.data());
        const uint16_t proto = ntohs(eth->h_proto);

        if (proto == ETH_P_ARP && len >= static_cast<ssize_t>(sizeof(ArpFrame))) {
            const auto* arp = reinterpret_cast<const ArpFrame*>(buffer.data());
            if (ntohs(arp->ptype) == ETH_P_IP && arp->plen == 4 && arp->hlen == 6) {
                char text[INET_ADDRSTRLEN] = {0};
                if (inet_ntop(AF_INET, arp->sender_ip, text, sizeof(text)) != nullptr) {
                    add_neighbour(result.neighbours, local_ipv4, text,
                                  mac_to_string(arp->sender_mac), "arp");
                }
            }
            continue;
        }

        if (proto == ETH_P_IP &&
            len >= static_cast<ssize_t>(sizeof(ethhdr) + sizeof(struct iphdr))) {
            const auto* ip_header =
                reinterpret_cast<const struct iphdr*>(buffer.data() + sizeof(ethhdr));
            if (ip_header->version != 4) {
                continue;
            }
            char text[INET_ADDRSTRLEN] = {0};
            if (inet_ntop(AF_INET, &ip_header->saddr, text, sizeof(text)) != nullptr) {
                add_neighbour(result.neighbours, local_ipv4, text, mac_to_string(eth->h_source),
                              "ipv4-traffic");
            }
        }
    }

    if (result.neighbours.empty()) {
        result.status = L2Status::kNoObservation;
        result.message =
            "no IPv4 address was observed on the link: the device is either silent or does not "
            "use IPv4 on this interface";
    } else {
        result.status = L2Status::kOk;
        result.message.clear();
    }
    return result;
}

}  // namespace devdisc
