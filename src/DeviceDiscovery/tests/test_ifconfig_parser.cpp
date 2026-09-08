#include "parser/ifconfig_parser.hpp"

#include "test_support.hpp"

namespace {

const char* kModernIfconfig = R"(eth0: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 1500
        inet 192.168.10.50  netmask 255.255.255.0  broadcast 192.168.10.255
        inet6 fe80::211:22ff:fe33:4455  prefixlen 64  scopeid 0x20<link>
        ether 00:11:22:33:44:55  txqueuelen 1000  (Ethernet)

eth1: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 1500
        inet 10.20.30.1  netmask 255.255.255.0  broadcast 10.20.30.255
        ether 00:11:22:33:44:56  txqueuelen 1000  (Ethernet)

lo: flags=73<UP,LOOPBACK,RUNNING>  mtu 65536
        inet 127.0.0.1  netmask 255.0.0.0
        inet6 ::1  prefixlen 128  scopeid 0x10<host>
)";

const char* kLegacyIfconfig = R"(ens33     Link encap:Ethernet  HWaddr 00:0c:29:aa:bb:cc
          inet addr:192.168.10.50  Bcast:192.168.10.255  Mask:255.255.255.0
          inet6 addr: fe80::20c:29ff:feaa:bbcc/64 Scope:Link
          UP BROADCAST RUNNING MULTICAST  MTU:1500  Metric:1

enp2s0    Link encap:Ethernet  HWaddr 00:0c:29:aa:bb:cd
          inet addr:10.20.30.1  Bcast:10.20.30.255  Mask:255.255.255.0
          UP BROADCAST RUNNING MULTICAST  MTU:1500  Metric:1

lo        Link encap:Local Loopback
          inet addr:127.0.0.1  Mask:255.0.0.0
          UP LOOPBACK RUNNING  MTU:65536  Metric:1
)";

const char* kNoIpv4 = R"(eth0: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 1500
        inet6 fe80::211:22ff:fe33:4455  prefixlen 64  scopeid 0x20<link>
        ether 00:11:22:33:44:55  txqueuelen 1000  (Ethernet)
)";

const char* kIpAddr = R"(1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN group default qlen 1000
    inet 127.0.0.1/8 scope host lo
       valid_lft forever preferred_lft forever
2: enp1s0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc fq_codel state UP group default qlen 1000
    inet 192.168.10.50/24 brd 192.168.10.255 scope global enp1s0
       valid_lft forever preferred_lft forever
3: enp2s0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc fq_codel state UP group default qlen 1000
    inet 10.20.30.1/24 brd 10.20.30.255 scope global enp2s0
       valid_lft forever preferred_lft forever
)";

}  // namespace

int main() {
    SECTION("modern ifconfig output");
    {
        const auto interfaces = devdisc::parse_ifconfig(kModernIfconfig);
        CHECK_EQ(interfaces.size(), std::size_t(2));
        CHECK_EQ(interfaces[0].name, std::string("eth0"));
        CHECK_EQ(interfaces[0].ipv4, std::string("192.168.10.50"));
        CHECK_EQ(interfaces[1].name, std::string("eth1"));
        CHECK_EQ(interfaces[1].ipv4, std::string("10.20.30.1"));
    }

    SECTION("legacy ifconfig output with non eth names");
    {
        const auto interfaces = devdisc::parse_ifconfig(kLegacyIfconfig);
        CHECK_EQ(interfaces.size(), std::size_t(2));
        CHECK_EQ(interfaces[0].name, std::string("ens33"));
        CHECK_EQ(interfaces[0].ipv4, std::string("192.168.10.50"));
        CHECK_EQ(interfaces[1].name, std::string("enp2s0"));
        CHECK_EQ(interfaces[1].ipv4, std::string("10.20.30.1"));
    }

    SECTION("ipv6 only interface is ignored");
    {
        const auto interfaces = devdisc::parse_ifconfig(kNoIpv4);
        CHECK(interfaces.empty());
    }

    SECTION("ip -4 addr fallback output");
    {
        const auto interfaces = devdisc::parse_ip_addr(kIpAddr);
        CHECK_EQ(interfaces.size(), std::size_t(2));
        CHECK_EQ(interfaces[0].name, std::string("enp1s0"));
        CHECK_EQ(interfaces[1].name, std::string("enp2s0"));
        CHECK_EQ(interfaces[1].ipv4, std::string("10.20.30.1"));
    }

    SECTION("format auto-detection");
    {
        CHECK_EQ(devdisc::parse_interfaces(kIpAddr).size(), std::size_t(2));
        CHECK_EQ(devdisc::parse_interfaces(kModernIfconfig).size(), std::size_t(2));
        CHECK(devdisc::parse_interfaces("").empty());
        CHECK(devdisc::parse_interfaces("bash: ifconfig: command not found").empty());
    }

    return testing::summary("ifconfig_parser");
}
