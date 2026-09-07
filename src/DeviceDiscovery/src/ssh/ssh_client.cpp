#include "ssh/ssh_client.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <libssh2.h>
#include <netinet/in.h>
#include <poll.h>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace devdisc {
namespace {

std::string fingerprint_sha256(const char* hash) {
    static const char* kBase64 =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const auto* data = reinterpret_cast<const unsigned char*>(hash);
    const std::size_t length = 32;
    std::string encoded;
    for (std::size_t i = 0; i < length; i += 3) {
        const unsigned int octet_a = data[i];
        const unsigned int octet_b = (i + 1 < length) ? data[i + 1] : 0;
        const unsigned int octet_c = (i + 2 < length) ? data[i + 2] : 0;
        const unsigned int triple = (octet_a << 16) | (octet_b << 8) | octet_c;
        encoded.push_back(kBase64[(triple >> 18) & 0x3F]);
        encoded.push_back(kBase64[(triple >> 12) & 0x3F]);
        encoded.push_back((i + 1 < length) ? kBase64[(triple >> 6) & 0x3F] : '=');
        encoded.push_back((i + 2 < length) ? kBase64[triple & 0x3F] : '=');
    }
    return "SHA256:" + encoded;
}

std::string default_known_hosts_path() {
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') {
        return {};
    }
    return std::string(home) + "/.ssh/known_hosts";
}

std::string libssh2_error_text(LIBSSH2_SESSION* session) {
    char* message = nullptr;
    int length = 0;
    libssh2_session_last_error(session, &message, &length, 0);
    return (message != nullptr) ? std::string(message, static_cast<std::size_t>(length))
                                : std::string("unknown libssh2 error");
}

}  // namespace

SshLibrary::SshLibrary() { ok_ = (libssh2_init(0) == 0); }
SshLibrary::~SshLibrary() { libssh2_exit(); }

struct SshClient::Impl {
    int sock = -1;
    LIBSSH2_SESSION* session = nullptr;
};

SshClient::SshClient(std::string host, const Config& config)
    : impl_(new Impl()), host_(std::move(host)), config_(config) {}

SshClient::~SshClient() {
    disconnect();
    delete impl_;
}

void SshClient::disconnect() {
    if (impl_ == nullptr) {
        return;
    }
    if (impl_->session != nullptr) {
        libssh2_session_disconnect(impl_->session, "Bye");
        libssh2_session_free(impl_->session);
        impl_->session = nullptr;
    }
    if (impl_->sock >= 0) {
        ::close(impl_->sock);
        impl_->sock = -1;
    }
}

SshStatus SshClient::connect_and_authenticate(const std::string& username,
                                              const std::string& password, std::string* error) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(22);
    if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
        *error = "invalid target address";
        return SshStatus::kConnectFailed;
    }

    impl_->sock = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (impl_->sock < 0) {
        *error = std::string("socket(): ") + std::strerror(errno);
        return SshStatus::kConnectFailed;
    }

    int rc = ::connect(impl_->sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc != 0) {
        if (errno != EINPROGRESS) {
            *error = std::string("connect(): ") + std::strerror(errno);
            disconnect();
            return SshStatus::kConnectFailed;
        }
        pollfd pfd{};
        pfd.fd = impl_->sock;
        pfd.events = POLLOUT;
        const int poll_rc = ::poll(&pfd, 1, config_.tcp_connect_timeout_ms);
        if (poll_rc <= 0) {
            *error = "SSH connection timed out";
            disconnect();
            return SshStatus::kConnectFailed;
        }
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        if (::getsockopt(impl_->sock, SOL_SOCKET, SO_ERROR, &so_error, &len) != 0 ||
            so_error != 0) {
            *error = std::string("connect(): ") + std::strerror(so_error != 0 ? so_error : errno);
            disconnect();
            return SshStatus::kConnectFailed;
        }
    }

    impl_->session = libssh2_session_init();
    if (impl_->session == nullptr) {
        *error = "could not initialise the SSH session";
        disconnect();
        return SshStatus::kConnectFailed;
    }
    libssh2_session_set_blocking(impl_->session, 1);
    libssh2_session_set_timeout(impl_->session, config_.ssh_auth_timeout_ms);

    if (libssh2_session_handshake(impl_->session, impl_->sock) != 0) {
        *error = "SSH handshake failed: " + libssh2_error_text(impl_->session);
        disconnect();
        return SshStatus::kHandshakeFailed;
    }

    const char* hash = libssh2_hostkey_hash(impl_->session, LIBSSH2_HOSTKEY_HASH_SHA256);
    if (hash != nullptr) {
        host_key_fp_ = fingerprint_sha256(hash);
    }

    if (config_.host_key_policy == "known-hosts") {
        LIBSSH2_KNOWNHOSTS* known = libssh2_knownhost_init(impl_->session);
        if (known == nullptr) {
            *error = "could not initialise known_hosts handling";
            disconnect();
            return SshStatus::kHostKeyRejected;
        }
        const std::string path =
            config_.known_hosts_path.empty() ? default_known_hosts_path() : config_.known_hosts_path;
        if (path.empty() ||
            libssh2_knownhost_readfile(known, path.c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH) < 0) {
            libssh2_knownhost_free(known);
            *error = "known_hosts file could not be read: " + path;
            disconnect();
            return SshStatus::kHostKeyRejected;
        }
        std::size_t key_len = 0;
        int key_type = 0;
        const char* key = libssh2_session_hostkey(impl_->session, &key_len, &key_type);
        if (key == nullptr) {
            libssh2_knownhost_free(known);
            *error = "server did not present a host key";
            disconnect();
            return SshStatus::kHostKeyRejected;
        }
        const int check = libssh2_knownhost_checkp(
            known, host_.c_str(), 22, key, key_len,
            LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW, nullptr);
        libssh2_knownhost_free(known);
        if (check != LIBSSH2_KNOWNHOST_CHECK_MATCH) {
            *error = "host key verification failed (" + host_key_fp_ +
                     "); the key is unknown or does not match known_hosts";
            disconnect();
            return SshStatus::kHostKeyRejected;
        }
    }

    if (libssh2_userauth_password(impl_->session, username.c_str(), password.c_str()) != 0) {
        // Deliberately generic: never leak the credential or its length.
        *error = "SSH authentication failed for the configured user";
        disconnect();
        return SshStatus::kAuthFailed;
    }

    return SshStatus::kOk;
}

SshCommandResult SshClient::run_command(const std::string& command) {
    SshCommandResult result;
    result.host_key_fp = host_key_fp_;
    if (impl_->session == nullptr) {
        result.status = SshStatus::kConnectFailed;
        result.error = "no authenticated SSH session";
        return result;
    }

    libssh2_session_set_timeout(impl_->session, config_.ssh_exec_timeout_ms);
    LIBSSH2_CHANNEL* channel = libssh2_channel_open_session(impl_->session);
    if (channel == nullptr) {
        result.status = SshStatus::kCommandFailed;
        result.error = "could not open an SSH channel: " + libssh2_error_text(impl_->session);
        return result;
    }

    if (libssh2_channel_exec(channel, command.c_str()) != 0) {
        result.status = SshStatus::kCommandFailed;
        result.error = "could not execute the remote command: " + libssh2_error_text(impl_->session);
        libssh2_channel_free(channel);
        return result;
    }

    char buffer[4096];
    for (;;) {
        const ssize_t n = libssh2_channel_read(channel, buffer, sizeof(buffer));
        if (n > 0) {
            result.stdout_data.append(buffer, static_cast<std::size_t>(n));
            continue;
        }
        if (n == LIBSSH2_ERROR_EAGAIN) {
            continue;
        }
        break;
    }
    for (;;) {
        const ssize_t n = libssh2_channel_read_stderr(channel, buffer, sizeof(buffer));
        if (n > 0) {
            result.stderr_data.append(buffer, static_cast<std::size_t>(n));
            continue;
        }
        if (n == LIBSSH2_ERROR_EAGAIN) {
            continue;
        }
        break;
    }

    while (libssh2_channel_close(channel) == LIBSSH2_ERROR_EAGAIN) {
    }
    result.exit_code = libssh2_channel_get_exit_status(channel);
    libssh2_channel_free(channel);

    result.status = SshStatus::kOk;
    return result;
}

}  // namespace devdisc
