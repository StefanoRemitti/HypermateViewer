#include "network/interface_discovery.hpp"

#include "test_support.hpp"

namespace {

devdisc::InterfaceInfo make(const std::string& name, const std::string& ip,
                           const std::string& mask, bool up = true, bool running = true,
                           bool loopback = false) {
    devdisc::InterfaceInfo info;
    info.name = name;
    info.ipv4 = ip;
    info.netmask = mask;
    info.is_up = up;
    info.is_running = running;
    info.is_loopback = loopback;
    info.has_ipv4 = !ip.empty();
    return info;
}

}  // namespace

int main() {
    SECTION("one valid ethernet interface");
    {
        const auto selection = devdisc::select_interface({
            make("lo", "127.0.0.1", "255.0.0.0", true, true, true),
            make("enp1s0", "192.168.10.2", "255.255.255.0"),
            make("docker0", "172.17.0.1", "255.255.0.0"),
        });
        CHECK(selection.status == devdisc::SelectionStatus::kOk);
        CHECK_EQ(selection.selected.name, std::string("enp1s0"));
    }

    SECTION("loopback only");
    {
        const auto selection = devdisc::select_interface({
            make("lo", "127.0.0.1", "255.0.0.0", true, true, true),
        });
        CHECK(selection.status == devdisc::SelectionStatus::kNoCandidate);
        CHECK(!selection.message.empty());
    }

    SECTION("multiple candidate interfaces");
    {
        const auto selection = devdisc::select_interface({
            make("eth0", "192.168.10.2", "255.255.255.0"),
            make("ens33", "10.0.0.5", "255.255.255.0"),
        });
        CHECK(selection.status == devdisc::SelectionStatus::kMultipleCandidates);
        CHECK_EQ(selection.candidates.size(), std::size_t(2));
    }

    SECTION("interface without ipv4 or link");
    {
        devdisc::InterfaceInfo no_ip = make("eth1", "", "");
        const auto selection = devdisc::select_interface({
            no_ip,
            make("eth2", "192.168.1.10", "255.255.255.0", true, false),  // no carrier
            make("eth3", "192.168.2.10", "255.255.255.0", false, true),  // down
        });
        CHECK(selection.status == devdisc::SelectionStatus::kNoCandidate);
    }

    SECTION("virtual interface names");
    {
        CHECK(devdisc::is_virtual_interface_name("docker0"));
        CHECK(devdisc::is_virtual_interface_name("veth1234"));
        CHECK(devdisc::is_virtual_interface_name("br-abc"));
        CHECK(devdisc::is_virtual_interface_name("wg0"));
        CHECK(!devdisc::is_virtual_interface_name("enp2s0"));
        CHECK(!devdisc::is_virtual_interface_name("eth0"));
        CHECK(!devdisc::is_virtual_interface_name("ens33"));
    }

    SECTION("ipv4 helpers");
    {
        uint32_t value = 0;
        CHECK(devdisc::parse_ipv4("192.168.10.50", &value));
        CHECK_EQ(devdisc::ipv4_to_string(value), std::string("192.168.10.50"));
        CHECK(!devdisc::parse_ipv4("999.1.1.1", &value));
        uint32_t mask = 0;
        devdisc::parse_ipv4("255.255.255.0", &mask);
        CHECK_EQ(devdisc::prefix_length_from_netmask(mask), 24);
    }

    return testing::summary("interface_discovery");
}
