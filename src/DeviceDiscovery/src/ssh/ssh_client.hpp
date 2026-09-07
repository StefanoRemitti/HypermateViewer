#pragma once

#include <string>

#include "core/config.hpp"

namespace devdisc {

enum class SshStatus {
    kOk,
    kConnectFailed,
    kHandshakeFailed,
    kHostKeyRejected,
    kAuthFailed,
    kCommandFailed,
};

struct SshCommandResult {
    SshStatus status = SshStatus::kConnectFailed;
    int exit_code = -1;
    std::string stdout_data;
    std::string stderr_data;
    std::string error;       // never contains the password
    std::string host_key_fp;  // SHA256 fingerprint of the server host key
};

// Minimal libssh2 wrapper.
//
// Host-key verification: controlled by Config::host_key_policy.
//   "off"          - the fingerprint is reported but not verified (default,
//                    documented as insecure; acceptable only on a physically
//                    isolated point-to-point link).
//   "known-hosts"  - the key must already be present and matching in the
//                    known_hosts file, otherwise the connection is refused.
class SshClient {
public:
    SshClient(std::string host, const Config& config);
    ~SshClient();

    SshClient(const SshClient&) = delete;
    SshClient& operator=(const SshClient&) = delete;

    // Connects, verifies the host key according to the policy and authenticates
    // with username/password. The password is never logged or echoed.
    SshStatus connect_and_authenticate(const std::string& username, const std::string& password,
                                       std::string* error);

    // Runs a command and captures stdout/stderr with a bounded timeout.
    SshCommandResult run_command(const std::string& command);

    const std::string& host_key_fingerprint() const { return host_key_fp_; }

    void disconnect();

private:
    struct Impl;
    Impl* impl_;
    std::string host_;
    Config config_;
    std::string host_key_fp_;
};

// Global libssh2 init/teardown helper (RAII).
class SshLibrary {
public:
    SshLibrary();
    ~SshLibrary();
    bool ok() const { return ok_; }

private:
    bool ok_ = false;
};

}  // namespace devdisc
