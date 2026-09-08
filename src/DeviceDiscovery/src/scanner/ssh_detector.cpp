#include "scanner/ssh_detector.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <vector>

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

int remaining_ms(const std::chrono::steady_clock::time_point& deadline) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
        return 0;
    }
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
}

}  // namespace

bool validate_ssh_banner(const std::string& raw, std::string* out) {
    // The identification string is terminated by CR LF; take the first line.
    std::string line = raw.substr(0, raw.find_first_of("\r\n"));
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
        line.pop_back();
    }
    if (line.rfind("SSH-", 0) != 0) {
        return false;
    }
    // Must be at least "SSH-x.y-" to be a plausible identification string.
    if (line.size() < 8) {
        return false;
    }
    const std::size_t dash = line.find('-', 4);
    if (dash == std::string::npos || dash == 4) {
        return false;
    }
    if (out != nullptr) {
        *out = line;
    }
    return true;
}

ProbeOutcome probe_ssh(const std::string& ip, int port, int connect_timeout_ms,
                       int banner_timeout_ms) {
    ProbeOutcome outcome;
    outcome.ip = ip;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        outcome.result = ProbeResult::kError;
        outcome.detail = "invalid IPv4 address";
        return outcome;
    }

    ScopedFd sock(::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
    if (sock.get() < 0) {
        outcome.result = ProbeResult::kError;
        outcome.detail = std::string("socket(): ") + std::strerror(errno);
        return outcome;
    }

    const auto connect_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(connect_timeout_ms);

    int rc = ::connect(sock.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc != 0) {
        if (errno != EINPROGRESS) {
            const int err = errno;
            outcome.result = (err == ECONNREFUSED) ? ProbeResult::kConnectRefused : ProbeResult::kError;
            outcome.detail = std::string("connect(): ") + std::strerror(err);
            return outcome;
        }
        pollfd pfd{};
        pfd.fd = sock.get();
        pfd.events = POLLOUT;
        const int poll_rc = ::poll(&pfd, 1, remaining_ms(connect_deadline));
        if (poll_rc == 0) {
            outcome.result = ProbeResult::kTimeout;
            outcome.detail = "TCP connect timed out";
            return outcome;
        }
        if (poll_rc < 0) {
            outcome.result = ProbeResult::kError;
            outcome.detail = std::string("poll(): ") + std::strerror(errno);
            return outcome;
        }
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        if (::getsockopt(sock.get(), SOL_SOCKET, SO_ERROR, &so_error, &len) != 0) {
            outcome.result = ProbeResult::kError;
            outcome.detail = std::string("getsockopt(): ") + std::strerror(errno);
            return outcome;
        }
        if (so_error != 0) {
            outcome.result =
                (so_error == ECONNREFUSED) ? ProbeResult::kConnectRefused : ProbeResult::kError;
            outcome.detail = std::string("connect(): ") + std::strerror(so_error);
            return outcome;
        }
    }

    // Connected: read the identification string with its own bounded deadline.
    const auto banner_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(banner_timeout_ms);
    std::string data;
    char buffer[256];
    for (;;) {
        pollfd pfd{};
        pfd.fd = sock.get();
        pfd.events = POLLIN;
        const int poll_rc = ::poll(&pfd, 1, remaining_ms(banner_deadline));
        if (poll_rc == 0) {
            outcome.result = ProbeResult::kTimeout;
            outcome.detail = "port 22 is open but sent no identification string in time";
            return outcome;
        }
        if (poll_rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            outcome.result = ProbeResult::kError;
            outcome.detail = std::string("poll(): ") + std::strerror(errno);
            return outcome;
        }
        const ssize_t n = ::recv(sock.get(), buffer, sizeof(buffer), 0);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            outcome.result = ProbeResult::kError;
            outcome.detail = std::string("recv(): ") + std::strerror(errno);
            return outcome;
        }
        if (n == 0) {
            break;  // peer closed without a complete banner
        }
        data.append(buffer, static_cast<std::size_t>(n));
        if (data.find('\n') != std::string::npos || data.size() > 1024) {
            break;
        }
    }

    std::string banner;
    if (validate_ssh_banner(data, &banner)) {
        outcome.result = ProbeResult::kSshDetected;
        outcome.banner = banner;
    } else {
        outcome.result = ProbeResult::kNotSsh;
        outcome.detail = data.empty() ? "port 22 is open but sent no data"
                                      : "port 22 is open but the peer is not an SSH server";
    }
    return outcome;
}

}  // namespace devdisc
