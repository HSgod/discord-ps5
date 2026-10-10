/*
 * Accord - a local TLS server, for the client tests that need a real peer.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The client has to be right about four things that only a real peer can show:
 * a certificate it can trust, one it cannot, one that is for another name, and a
 * peer that answers a ClientHello with something other than a ServerHello. All
 * four live here: one connection, an ephemeral loopback port, its own thread,
 * and the certificates from tests/fixtures/tls (see tools/make-test-certs.sh).
 *
 * Blocking sockets with a timeout, not the memory-BIO pump the client itself
 * uses: nothing on this side has to be interruptible in the middle of a record,
 * and a thread allowed to block is a great deal less code.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>

namespace accord::test
{
class TlsServer
{
  public:
    // What happens once the handshake is done, or instead of it.
    enum class Mode
    {
        answer_get,   // read the request, answer "HTTP/1.1 200 OK" plus a short body
        big_response, // handshake, then a body far larger than one TLS record
        quiet,        // handshake, then nothing at all, so a client read has to time out
        no_tls,       // plain text where a ServerHello belongs
        stall,        // accept, then nothing: the client's own handshake wait has to end it
    };

    // Which certificate to present. `trusted` is the one the test CA signed --
    // the file the tests hand the client as its root -- and `selfsigned` is the
    // same name signed by itself, so the chain is the only thing to refuse.
    enum class Cert
    {
        trusted,
        selfsigned,
    };

    static constexpr std::string_view kBody = "hello from the TLS server";
    static constexpr std::size_t kBigBodySize = 200 * 1024;
    // What kBigBodySize bytes of body are made of, repeated. The client checks
    // what it read against the same pattern.
    static constexpr std::string_view kBigBodyUnit = "0123456789abcdef";

    TlsServer() = default;
    TlsServer(const TlsServer &) = delete;
    TlsServer &operator=(const TlsServer &) = delete;
    ~TlsServer();

    // Binds 127.0.0.1:0 and starts serving. The certificates are read from
    // `fixtures` ("tests/fixtures/tls" by default, which is where the tests run
    // from). False means no port came up, or no certificate was found, and
    // error() says which.
    bool start(Mode mode, Cert cert, std::string_view fixtures = "tests/fixtures/tls");

    std::uint16_t port() const noexcept
    {
        return port_;
    }
    const std::string &error() const noexcept
    {
        return error_;
    }

    // Shuts the connection and the listener down and joins the thread. Safe to
    // call twice, and safe when start() was never called.
    void stop() noexcept;

    // Whether the handshake on this side got as far as the certificate.
    bool handshake_ok() const noexcept
    {
        return handshake_ok_;
    }
    // The request head as it arrived, "\r\n\r\n" and all. Empty in the modes
    // that never read one.
    const std::string &request() const noexcept
    {
        return request_;
    }

  private:
    // The thread's whole life. It owns the OpenSSL objects it makes and releases
    // them before it returns, so stop() only has to unblock the sockets.
    void serve() noexcept;
    bool accept_one() noexcept;
    // The certificate and key for cert_, as a server context. Null on failure,
    // with error() set and nothing left open.
    void *make_context() noexcept;
    // The SSL object is passed as void* on purpose: this header says nothing
    // about TLS, and the .cpp is the only place that knows what one is.
    bool read_request(void *ssl) noexcept;
    bool send_all(void *ssl, const std::uint8_t *data, std::size_t size) noexcept;
    bool send_all(void *ssl, std::string_view text) noexcept;
    void send_response(void *ssl) noexcept;
    void send_big_response(void *ssl) noexcept;

    int listener_ = -1;
    int client_ = -1;
    std::uint16_t port_ = 0;
    Mode mode_ = Mode::answer_get;
    Cert cert_ = Cert::trusted;
    std::string fixtures_;
    std::string error_;
    std::string request_;
    bool handshake_ok_ = false;
    std::atomic<bool> stopping_{false};
    std::thread worker_;
};
} // namespace accord::test
