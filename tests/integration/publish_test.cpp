#include "support/test.hpp"

#include "modules/publish/verify.hpp"
#include "okfw/sha256.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <string>
#include <thread>

using namespace vyzorix;

namespace {

// A minimal HTTP/1.1 server for one request at a time, enough to exercise the
// publish handshake. It serves `body` at /blob:
//   - a request with no Authorization gets 401 + a WWW-Authenticate challenge;
//   - a request with the expected Bearer token gets 200 + the body;
//   - /token returns {"token":"..."}.
class FakeRegistry {
public:
    FakeRegistry(std::string body, std::string token)
        : body_(std::move(body)), token_(std::move(token)) {
        sock_ = ::socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        ::setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;  // an ephemeral port
        ::bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        ::listen(sock_, 4);

        socklen_t len = sizeof(addr);
        ::getsockname(sock_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);

        thread_ = std::thread([this] { serve(); });
    }

    ~FakeRegistry() {
        stop_ = true;
        // Unblock accept() so the thread can exit.
        ::shutdown(sock_, SHUT_RDWR);
        ::close(sock_);
        if (thread_.joinable()) thread_.join();
    }

    FakeRegistry(const FakeRegistry&) = delete;
    FakeRegistry& operator=(const FakeRegistry&) = delete;

    [[nodiscard]] std::string base() const { return "http://127.0.0.1:" + std::to_string(port_); }
    [[nodiscard]] std::string token() const { return token_; }

private:
    void serve() {
        while (!stop_) {
            int client = ::accept(sock_, nullptr, nullptr);
            if (client < 0) break;
            handle(client);
            ::close(client);
        }
    }

    static std::string read_request(int client) {
        std::string req;
        char buf[4096];
        for (;;) {
            ssize_t n = ::read(client, buf, sizeof(buf));
            if (n <= 0) break;
            req.append(buf, static_cast<size_t>(n));
            if (req.find("\r\n\r\n") != std::string::npos) break;
        }
        return req;
    }

    static void write_response(int client, const std::string& headers, const std::string& body) {
        std::string resp = headers + "\r\n" + body;
        ::write(client, resp.data(), resp.size());
    }

    void handle(int client) {
        std::string req = read_request(client);

        if (req.rfind("GET /token", 0) == 0) {
            std::string body = "{\"token\":\"" + token_ + "\"}";
            write_response(client,
                           "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                               std::to_string(body.size()) + "\r\nConnection: close\r\n",
                           body);
            return;
        }

        if (req.rfind("GET /blob", 0) == 0) {
            bool authed = req.find("Authorization: Bearer " + token_) != std::string::npos;
            if (!authed) {
                // The OCI challenge: a client must mint a token from /token and
                // retry. This is what GHCR does even for a public blob.
                std::string challenge =
                    "HTTP/1.1 401 Unauthorized\r\n"
                    "WWW-Authenticate: Bearer realm=\"" + base() + "/token\",service=\"fake\","
                    "scope=\"repository:test/repo:pull\"\r\n"
                    "Content-Length: 0\r\nConnection: close\r\n";
                write_response(client, challenge, "");
                return;
            }
            write_response(client,
                           "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: " +
                               std::to_string(body_.size()) + "\r\nConnection: close\r\n",
                           body_);
            return;
        }

        write_response(client, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n", "");
    }

    int sock_ = -1;
    int port_ = 0;
    std::string body_;
    std::string token_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
};

}  // namespace

VYZ_TEST(publish_verify_does_the_oci_handshake_and_checks_bytes) {
    const std::string body = "the published artifact bytes";
    FakeRegistry reg(body, "anon-token-abc");

    const std::string url = reg.base() + "/blob";
    const std::string good = okfw::sha256_hex(body);

    // The URL 401s a plain GET and 200s with the token; verify_anonymous must do
    // the handshake itself and then confirm the bytes.
    CHECK(publish::verify_anonymous(url, good).has_value());

    // Wrong bytes at a 200 is a refusal, naming the digest mismatch.
    auto bad = publish::verify_anonymous(url, std::string(64, '0'));
    CHECK(!bad.has_value());
    if (!bad.has_value()) {
        CHECK(bad.error().message.find("digest") != std::string::npos);
    }

    // A status-only check (no digest) also passes through the handshake.
    CHECK(publish::verify_anonymous(url, "").has_value());
}

VYZ_TEST(publish_verify_refuses_a_404) {
    FakeRegistry reg("x", "tok");
    // A path the server does not serve is a refusal, not a silent pass.
    CHECK(!publish::verify_anonymous(reg.base() + "/nope", "").has_value());
}
