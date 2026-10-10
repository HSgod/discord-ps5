/*
 * Accord - a local TLS server, for the client tests that need a real peer.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tests/support/tls_server.hpp"

#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace accord::test
{
namespace
{
constexpr int kWaitMs = 1000;
constexpr std::size_t kReadChunk = 4096;
constexpr std::size_t kMaxRequest = 16 * 1024;

std::string openssl_error(const char *call)
{
    std::string text{call};
    for (unsigned long code = ::ERR_get_error(); code != 0; code = ::ERR_get_error())
    {
        char buffer[256];
        ::ERR_error_string_n(code, buffer, sizeof(buffer));
        text += ": ";
        text += buffer;
    }
    return text;
}

void set_timeout(int fd, int option, int milliseconds)
{
    timeval timeout{};
    timeout.tv_sec = milliseconds / 1000;
    timeout.tv_usec = (milliseconds % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, option, &timeout, sizeof(timeout));
}

// Releases whatever the thread made, on every way out of it.
struct Owned
{
    SSL_CTX *ctx = nullptr;
    SSL *ssl = nullptr;

    ~Owned()
    {
        // SSL_set_fd left the socket alone, so this does not close it: stop()
        // owns the file descriptors. SSL_free releases the BIO over it.
        if (ssl != nullptr)
            ::SSL_free(ssl);
        if (ctx != nullptr)
            ::SSL_CTX_free(ctx);
    }
};
} // namespace

TlsServer::~TlsServer()
{
    stop();
}

bool TlsServer::start(Mode mode, Cert cert, std::string_view fixtures)
{
    stop();
    mode_ = mode;
    cert_ = cert;
    fixtures_ = std::string{fixtures};
    handshake_ok_ = false;
    request_.clear();
    stopping_.store(false);

    listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener_ < 0)
    {
        error_ = std::string{"socket: "} + std::strerror(errno);
        return false;
    }

    const int reuse = 1;
    ::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0)
    {
        error_ = std::string{"bind: "} + std::strerror(errno);
        stop();
        return false;
    }
    if (::listen(listener_, 1) != 0)
    {
        error_ = std::string{"listen: "} + std::strerror(errno);
        stop();
        return false;
    }

    socklen_t size = sizeof(address);
    if (::getsockname(listener_, reinterpret_cast<sockaddr *>(&address), &size) != 0)
    {
        error_ = std::string{"getsockname: "} + std::strerror(errno);
        stop();
        return false;
    }
    port_ = ntohs(address.sin_port);

    worker_ = std::thread{[this] { serve(); }};
    return true;
}

void TlsServer::stop() noexcept
{
    stopping_.store(true);
    if (listener_ >= 0)
    {
        ::shutdown(listener_, SHUT_RDWR);
        ::close(listener_);
        listener_ = -1;
    }
    if (client_ >= 0)
    {
        ::shutdown(client_, SHUT_RDWR);
        ::close(client_);
        client_ = -1;
    }
    if (worker_.joinable())
        worker_.join();
}

bool TlsServer::accept_one() noexcept
{
    // The client connects before it hands over, so this normally answers at
    // once; the poll is here so that a test that never connects can still stop
    // the server instead of waiting on accept() forever.
    for (;;)
    {
        if (stopping_.load())
            return false;
        pollfd waiting{};
        waiting.fd = listener_;
        waiting.events = POLLIN;
        const int ready = ::poll(&waiting, 1, 100);
        if (ready > 0)
            break;
        if (ready < 0 && errno != EINTR)
        {
            error_ = std::string{"poll: "} + std::strerror(errno);
            return false;
        }
    }

    client_ = ::accept(listener_, nullptr, nullptr);
    if (client_ < 0)
    {
        error_ = std::string{"accept: "} + std::strerror(errno);
        return false;
    }
    set_timeout(client_, SO_RCVTIMEO, kWaitMs);
    set_timeout(client_, SO_SNDTIMEO, kWaitMs);
    return true;
}

void *TlsServer::make_context() noexcept
{
    const bool trusted = cert_ == Cert::trusted;
    const std::string crt = fixtures_ + (trusted ? "/server-localhost.crt" : "/server-selfsigned.crt");
    const std::string key = fixtures_ + (trusted ? "/server-localhost.key" : "/server-selfsigned.key");

    SSL_CTX *ctx = ::SSL_CTX_new(::TLS_server_method());
    if (ctx == nullptr)
    {
        error_ = openssl_error("SSL_CTX_new");
        return nullptr;
    }
    if (::SSL_CTX_use_certificate_chain_file(ctx, crt.c_str()) != 1)
    {
        error_ = crt + ": " + openssl_error("SSL_CTX_use_certificate_chain_file");
        ::SSL_CTX_free(ctx);
        return nullptr;
    }
    if (::SSL_CTX_use_PrivateKey_file(ctx, key.c_str(), SSL_FILETYPE_PEM) != 1)
    {
        error_ = key + ": " + openssl_error("SSL_CTX_use_PrivateKey_file");
        ::SSL_CTX_free(ctx);
        return nullptr;
    }
    return ctx;
}

bool TlsServer::read_request(void *ssl) noexcept
{
    SSL *tls = static_cast<SSL *>(ssl);

    std::string collected;
    std::uint8_t buffer[kReadChunk];
    while (collected.find("\r\n\r\n") == std::string::npos && collected.size() < kMaxRequest)
    {
        const int got = ::SSL_read(tls, buffer, static_cast<int>(sizeof(buffer)));
        if (got <= 0)
            return false;
        collected.append(reinterpret_cast<const char *>(buffer), static_cast<std::size_t>(got));
    }
    request_ = std::move(collected);
    return !request_.empty();
}

bool TlsServer::send_all(void *ssl, const std::uint8_t *data, std::size_t size) noexcept
{
    SSL *tls = static_cast<SSL *>(ssl);

    std::size_t sent = 0;
    while (sent < size)
    {
        const std::size_t left = size - sent;
        const int chunk =
            ::SSL_write(tls, data + sent, left > 16 * 1024 ? 16 * 1024 : static_cast<int>(left));
        if (chunk <= 0)
            return false;
        sent += static_cast<std::size_t>(chunk);
    }
    return true;
}

bool TlsServer::send_all(void *ssl, std::string_view text) noexcept
{
    return send_all(ssl, reinterpret_cast<const std::uint8_t *>(text.data()), text.size());
}

void TlsServer::send_response(void *ssl) noexcept
{
    const std::string head = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " +
                             std::to_string(kBody.size()) + "\r\nConnection: close\r\n\r\n";
    send_all(ssl, head);
    send_all(ssl, kBody);
}

void TlsServer::send_big_response(void *ssl) noexcept
{
    const std::string head =
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " +
        std::to_string(kBigBodySize) + "\r\nConnection: close\r\n\r\n";
    send_all(ssl, head);

    // The body goes out in pieces of the pattern, so what the client reads back
    // is something it can check without a second copy of 200 KB in a fixture.
    std::string piece;
    piece.reserve(4096);
    while (piece.size() < 4096)
        piece += kBigBodyUnit;

    std::size_t sent = 0;
    while (sent < kBigBodySize)
    {
        const std::size_t left = kBigBodySize - sent;
        const std::size_t take = left < piece.size() ? left : piece.size();
        if (!send_all(ssl, std::string_view{piece.data(), take}))
            return;
        sent += take;
    }
}

void TlsServer::serve() noexcept
{
    Owned owned;

    if (!accept_one())
        return;

    if (mode_ == Mode::stall)
    {
        // Nothing at all, not even a ServerHello: the client's own wait is what
        // has to end this conversation.
        while (!stopping_.load())
            ::usleep(20 * 1000);
        return;
    }

    if (mode_ == Mode::no_tls)
    {
        // No SSL object exists in this mode, which is the point: the bytes go
        // out on the socket itself, where a ClientHello was expected.
        const std::string_view text{"this is a plain text server, not a TLS one\r\n"};
        std::size_t sent = 0;
        while (sent < text.size())
        {
            const ssize_t chunk = ::send(client_, text.data() + sent, text.size() - sent,
                                         MSG_NOSIGNAL);
            if (chunk <= 0)
                break;
            sent += static_cast<std::size_t>(chunk);
        }
        return;
    }

    owned.ctx = static_cast<SSL_CTX *>(make_context());
    if (owned.ctx == nullptr)
        return;

    owned.ssl = ::SSL_new(owned.ctx);
    if (owned.ssl == nullptr)
    {
        error_ = openssl_error("SSL_new");
        return;
    }
    ::SSL_set_fd(owned.ssl, client_);
    // The accepted socket has a receive timeout, and OpenSSL's auto-retry would
    // turn every one of those timeouts into another wait inside itself, forever.
    // This server wants the timeout to come back to it as an answer, which is how
    // "the client sent no request" turns into "say goodbye and stop".
    ::SSL_clear_mode(owned.ssl, SSL_MODE_AUTO_RETRY);
    if (::SSL_accept(owned.ssl) != 1)
    {
        error_ = openssl_error("SSL_accept");
        return;
    }
    handshake_ok_ = true;

    switch (mode_)
    {
    case Mode::answer_get:
        if (read_request(owned.ssl))
            send_response(owned.ssl);
        break;
    case Mode::big_response:
        // The request is not the point here, and no request is a fine answer.
        read_request(owned.ssl);
        send_big_response(owned.ssl);
        break;
    case Mode::quiet:
        while (!stopping_.load())
            ::usleep(20 * 1000);
        break;
    case Mode::no_tls:
    case Mode::stall:
        break;
    }

    // A proper goodbye, so that a client reading this sees the end of the TLS
    // stream rather than a socket that went away underneath it. Note that this
    // is SSL_shutdown() without the quiet flag: quiet shutdown sends nothing at
    // all -- it marks both directions shut and returns -- and a test server that
    // never says goodbye is a test server that cannot test this. The call sends
    // the close_notify and then waits for the peer's; the accepted socket's
    // receive timeout is what ends that wait, and the return value is not news.
    ::SSL_shutdown(owned.ssl);
    // And the FIN, so that reading past the goodbye is an end of stream rather
    // than another wait. The close_notify above went out first: same socket,
    // same order.
    ::shutdown(client_, SHUT_WR);
}
} // namespace accord::test
