#pragma once

#include <string>

namespace devdisc {

enum class ProbeResult {
    kSshDetected,     // TCP connect succeeded and banner starts with "SSH-"
    kNotSsh,          // TCP connect succeeded but the peer is not an SSH server
    kConnectRefused,  // port closed
    kTimeout,         // no answer in time (connect or banner)
    kError,           // socket/route error (see detail)
};

struct ProbeOutcome {
    ProbeResult result = ProbeResult::kError;
    std::string ip;
    std::string banner;  // trimmed identification string when kSshDetected
    std::string detail;  // diagnostic, never contains credentials
};

// Non-blocking TCP connect to ip:port bounded by connect_timeout_ms, followed by
// a bounded read of the SSH identification string.
ProbeOutcome probe_ssh(const std::string& ip, int port, int connect_timeout_ms,
                       int banner_timeout_ms);

// Pure helper: validate/normalise an SSH identification string.
// Returns true when the string is a valid SSH identification (RFC 4253 §4.2)
// and stores the trimmed banner in *out.
bool validate_ssh_banner(const std::string& raw, std::string* out);

}  // namespace devdisc
