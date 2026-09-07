#include "scanner/port_scanner.hpp"

#include <algorithm>

#include "output/formatter.hpp"
#include "test_support.hpp"

int main() {
    SECTION("subnet host enumeration");
    {
        const auto hosts = devdisc::enumerate_subnet_hosts("192.168.10.2", "255.255.255.0", 4096);
        CHECK_EQ(hosts.size(), std::size_t(253));  // 254 usable minus ourselves
        CHECK(std::find(hosts.begin(), hosts.end(), "192.168.10.1") != hosts.end());
        CHECK(std::find(hosts.begin(), hosts.end(), "192.168.10.254") != hosts.end());
        CHECK(std::find(hosts.begin(), hosts.end(), "192.168.10.2") == hosts.end());
        CHECK(std::find(hosts.begin(), hosts.end(), "192.168.10.0") == hosts.end());
        CHECK(std::find(hosts.begin(), hosts.end(), "192.168.10.255") == hosts.end());
    }

    SECTION("point to point and degenerate masks");
    {
        CHECK(devdisc::enumerate_subnet_hosts("10.0.0.1", "255.255.255.255", 4096).empty());
        CHECK(devdisc::enumerate_subnet_hosts("10.0.0.1", "0.0.0.0", 4096).empty());
        CHECK(devdisc::enumerate_subnet_hosts("bogus", "255.255.255.0", 4096).empty());
    }

    SECTION("oversized networks are refused instead of scanned");
    {
        CHECK(devdisc::enumerate_subnet_hosts("10.0.0.1", "255.0.0.0", 4096).empty());
        CHECK_EQ(devdisc::enumerate_subnet_hosts("10.0.0.1", "255.255.255.240", 4096).size(),
                 std::size_t(13));
    }

    SECTION("json formatting");
    {
        devdisc::DiscoveryReport report;
        report.ssh_ip = "192.168.10.50";
        report.ssh_banner = "SSH-2.0-OpenSSH_9.6";
        report.discovery_stage = "subnet-scan";
        report.interfaces = {{"eth0", "192.168.10.50"}, {"eth1", "10.20.30.1"}};
        const std::string json = devdisc::format_json(report);
        CHECK(json.find("\"ssh_ip\": \"192.168.10.50\"") != std::string::npos);
        CHECK(json.find("\"name\": \"eth1\"") != std::string::npos);
        CHECK(json.find("password") == std::string::npos);

        const std::string human = devdisc::format_human(report);
        CHECK(human.find("SSH-2.0-OpenSSH_9.6") != std::string::npos);
        CHECK(human.find("eth1") != std::string::npos);

        CHECK_EQ(devdisc::json_escape("a\"b\\c\n"), std::string("a\\\"b\\\\c\\n"));
    }

    return testing::summary("port_scanner");
}
