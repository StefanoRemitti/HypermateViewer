#include "scanner/ssh_detector.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <thread>

#include "test_support.hpp"

namespace {

// Minimal loopback TCP server used as a mock device.
class MockServer {
public:
    // payload is written to the first client; when payload is empty the server
    // accepts the connection and stays silent (banner timeout scenario).
    explicit MockServer(std::string payload) : payload_(std::move(payload)) {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        int reuse = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;  // ephemeral
        ::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        ::listen(listen_fd_, 4);
        socklen_t len = sizeof(addr);
        ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        thread_ = std::thread([this]() { run(); });
    }

    ~MockServer() {
        stop_ = true;
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    int port() const { return port_; }

private:
    void run() {
        while (!stop_) {
            const int client = ::accept(listen_fd_, nullptr, nullptr);
            if (client < 0) {
                return;
            }
            if (!payload_.empty()) {
                (void)::send(client, payload_.data(), payload_.size(), MSG_NOSIGNAL);
            } else {
                // stay silent long enough for the probe to time out
                std::this_thread::sleep_for(std::chrono::milliseconds(400));
            }
            ::close(client);
        }
    }

    std::string payload_;
    int listen_fd_ = -1;
    int port_ = 0;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

int free_port() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    socklen_t len = sizeof(addr);
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    const int port = ntohs(addr.sin_port);
    ::close(fd);
    return port;
}

}  // namespace

int main() {
    SECTION("banner validation");
    {
        std::string banner;
        CHECK(devdisc::validate_ssh_banner("SSH-2.0-OpenSSH_9.6\r\n", &banner));
        CHECK_EQ(banner, std::string("SSH-2.0-OpenSSH_9.6"));
        CHECK(devdisc::validate_ssh_banner("SSH-1.99-Cisco-1.25\r\n", &banner));
        CHECK(!devdisc::validate_ssh_banner("HTTP/1.1 400 Bad Request\r\n", &banner));
        CHECK(!devdisc::validate_ssh_banner("SSH-\r\n", &banner));
        CHECK(!devdisc::validate_ssh_banner("ssh-2.0-OpenSSH\r\n", &banner));
        CHECK(!devdisc::validate_ssh_banner("", &banner));
    }

    SECTION("valid ssh banner over tcp");
    {
        MockServer server("SSH-2.0-OpenSSH_9.6\r\n");
        const auto outcome = devdisc::probe_ssh("127.0.0.1", server.port(), 750, 1000);
        CHECK(outcome.result == devdisc::ProbeResult::kSshDetected);
        CHECK_EQ(outcome.banner, std::string("SSH-2.0-OpenSSH_9.6"));
    }

    SECTION("non-ssh service on the port");
    {
        MockServer server("220 mail.example.com ESMTP\r\n");
        const auto outcome = devdisc::probe_ssh("127.0.0.1", server.port(), 750, 1000);
        CHECK(outcome.result == devdisc::ProbeResult::kNotSsh);
        CHECK(outcome.banner.empty());
    }

    SECTION("malformed banner");
    {
        MockServer server("SSH-\r\n");
        const auto outcome = devdisc::probe_ssh("127.0.0.1", server.port(), 750, 1000);
        CHECK(outcome.result == devdisc::ProbeResult::kNotSsh);
    }

    SECTION("connection refused");
    {
        const auto outcome = devdisc::probe_ssh("127.0.0.1", free_port(), 750, 1000);
        CHECK(outcome.result == devdisc::ProbeResult::kConnectRefused);
    }

    SECTION("banner timeout");
    {
        MockServer server("");
        const auto outcome = devdisc::probe_ssh("127.0.0.1", server.port(), 750, 100);
        CHECK(outcome.result == devdisc::ProbeResult::kTimeout);
    }

    SECTION("invalid address");
    {
        const auto outcome = devdisc::probe_ssh("not-an-ip", 22, 100, 100);
        CHECK(outcome.result == devdisc::ProbeResult::kError);
    }

    return testing::summary("ssh_detector");
}
