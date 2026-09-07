#include "output/formatter.hpp"

#include <iomanip>
#include <sstream>

namespace devdisc {

std::string json_escape(const std::string& value) {
    std::ostringstream oss;
    for (const char c : value) {
        switch (c) {
            case '"':
                oss << "\\\"";
                break;
            case '\\':
                oss << "\\\\";
                break;
            case '\n':
                oss << "\\n";
                break;
            case '\r':
                oss << "\\r";
                break;
            case '\t':
                oss << "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    oss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(static_cast<unsigned char>(c)) << std::dec;
                } else {
                    oss << c;
                }
        }
    }
    return oss.str();
}

std::string format_human(const DiscoveryReport& report) {
    std::ostringstream oss;
    oss << "Directly connected device discovered\n\n";
    oss << "SSH endpoint:\n";
    oss << "  IP:       " << report.ssh_ip << "\n";
    oss << "  Banner:   " << report.ssh_banner << "\n";
    if (!report.host_key_fingerprint.empty()) {
        oss << "  Host key: " << report.host_key_fingerprint << "\n";
    }
    oss << "\nNetwork interfaces:\n";
    for (const RemoteInterface& iface : report.interfaces) {
        oss << "  " << std::left << std::setw(10) << iface.name << iface.ipv4 << "\n";
    }
    if (report.interfaces.empty()) {
        oss << "  (none)\n";
    }
    for (const std::string& warning : report.warnings) {
        oss << "\nWARNING: " << warning << "\n";
    }
    return oss.str();
}

std::string format_json(const DiscoveryReport& report) {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"ssh_ip\": \"" << json_escape(report.ssh_ip) << "\",\n";
    oss << "  \"ssh_banner\": \"" << json_escape(report.ssh_banner) << "\",\n";
    oss << "  \"host_key_fingerprint\": \"" << json_escape(report.host_key_fingerprint) << "\",\n";
    oss << "  \"discovery_stage\": \"" << json_escape(report.discovery_stage) << "\",\n";
    oss << "  \"interfaces\": [";
    for (std::size_t i = 0; i < report.interfaces.size(); ++i) {
        oss << (i == 0 ? "\n" : ",\n");
        oss << "    {\n";
        oss << "      \"name\": \"" << json_escape(report.interfaces[i].name) << "\",\n";
        oss << "      \"ipv4\": \"" << json_escape(report.interfaces[i].ipv4) << "\"\n";
        oss << "    }";
    }
    oss << (report.interfaces.empty() ? "]," : "\n  ],") << "\n";
    oss << "  \"warnings\": [";
    for (std::size_t i = 0; i < report.warnings.size(); ++i) {
        oss << (i == 0 ? "\n" : ",\n");
        oss << "    \"" << json_escape(report.warnings[i]) << "\"";
    }
    oss << (report.warnings.empty() ? "]" : "\n  ]") << "\n";
    oss << "}\n";
    return oss.str();
}

std::string format_json_error(const std::string& message, int exit_code) {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"error\": \"" << json_escape(message) << "\",\n";
    oss << "  \"exit_code\": " << exit_code << "\n";
    oss << "}\n";
    return oss.str();
}

}  // namespace devdisc
