#include "parser/ifconfig_parser.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace devdisc {
namespace {

bool is_loopback_name(const std::string& name) {
    return name == "lo" || name.rfind("lo:", 0) == 0;
}

bool is_valid_ipv4_literal(const std::string& text) {
    int parts = 0;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t dot = text.find('.', pos);
        const std::string part = text.substr(pos, dot == std::string::npos ? std::string::npos
                                                                          : dot - pos);
        if (part.empty() || part.size() > 3 ||
            !std::all_of(part.begin(), part.end(),
                         [](unsigned char c) { return std::isdigit(c) != 0; })) {
            return false;
        }
        if (std::stoi(part) > 255) {
            return false;
        }
        ++parts;
        if (dot == std::string::npos) {
            break;
        }
        pos = dot + 1;
    }
    return parts == 4;
}

void add_interface(std::vector<RemoteInterface>& out, const std::string& name,
                   const std::string& ipv4) {
    if (name.empty() || ipv4.empty() || is_loopback_name(name) || !is_valid_ipv4_literal(ipv4)) {
        return;
    }
    const bool known = std::any_of(out.begin(), out.end(), [&](const RemoteInterface& i) {
        return i.name == name && i.ipv4 == ipv4;
    });
    if (!known) {
        out.push_back(RemoteInterface{name, ipv4});
    }
}

std::string strip_trailing_colon(std::string text) {
    if (!text.empty() && text.back() == ':') {
        text.pop_back();
    }
    return text;
}

}  // namespace

std::vector<RemoteInterface> parse_ifconfig(const std::string& output) {
    std::vector<RemoteInterface> result;
    // "inet 192.168.10.50" (net-tools >= 2.0) or "inet addr:192.168.10.50" (legacy).
    static const std::regex kInetRegex(R"(\binet\b(?:\s+addr:|\s+)((?:\d{1,3}\.){3}\d{1,3}))");

    std::istringstream stream(output);
    std::string line;
    std::string current;
    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }
        const bool continuation = std::isspace(static_cast<unsigned char>(line[0])) != 0;
        if (!continuation) {
            std::istringstream line_stream(line);
            std::string token;
            line_stream >> token;
            current = strip_trailing_colon(token);
            // `ip addr` style headers ("2: eth0: <BROADCAST,...>") are not
            // ifconfig output; ignore them so the correct parser is used.
            if (!current.empty() && std::all_of(current.begin(), current.end(), [](unsigned char c) {
                    return std::isdigit(c) != 0;
                })) {
                current.clear();
            }
        }
        if (current.empty()) {
            continue;
        }
        std::smatch match;
        if (std::regex_search(line, match, kInetRegex)) {
            add_interface(result, current, match[1].str());
        }
    }
    return result;
}

std::vector<RemoteInterface> parse_ip_addr(const std::string& output) {
    std::vector<RemoteInterface> result;
    static const std::regex kHeaderRegex(R"(^\d+:\s+([^:@\s]+)[@:])");
    static const std::regex kInetRegex(R"(^\s*inet\s+((?:\d{1,3}\.){3}\d{1,3}))");

    std::istringstream stream(output);
    std::string line;
    std::string current;
    while (std::getline(stream, line)) {
        std::smatch match;
        if (std::regex_search(line, match, kHeaderRegex)) {
            current = match[1].str();
            continue;
        }
        if (!current.empty() && std::regex_search(line, match, kInetRegex)) {
            add_interface(result, current, match[1].str());
        }
    }
    return result;
}

std::vector<RemoteInterface> parse_interfaces(const std::string& output) {
    std::vector<RemoteInterface> result = parse_ifconfig(output);
    if (result.empty()) {
        result = parse_ip_addr(output);
    }
    return result;
}

}  // namespace devdisc
