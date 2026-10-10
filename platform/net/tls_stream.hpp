/*
 * Accord - TLS over any core::Stream, with the peer always verified.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * OpenSSL 3.5.2 -- the version the daemon links, see THIRD_PARTY.md -- is driven
 * through memory BIOs rather than a socket: the record layer hands its bytes to a
 * buffer this class drains into the transport underneath, and takes the peer's
 * bytes from a buffer it fills from that transport. That is what keeps the TLS
 * client on the same core::Stream seam the WebSocket client was written against,
 * so the protocol code above it does not know a certificate is involved.
 *
 * Verification is not a parameter. The chain is checked against the roots the
 * caller passes, and the name is checked against the host the caller names. There
 * is no flag, no option and no overload that turns either off: a build that
 * cannot check a certificate has no way to ask for one that does not. Callers
 * choose which roots to trust, not whether to look.
 *
 * The host goes into the handshake twice for that reason -- as SNI, so the server
 * can pick a certificate, and as the name that certificate must carry. The two
 * are the same string on purpose.
 *
 * No exceptions: handshake() answers false, read()/write() answer
 * StreamStatus::failed, and error() says what happened.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <openssl/ssl.h>

#include "core/stream.hpp"

namespace accord::net
{
class TlsStream final : public core::Stream
{
  public:
    TlsStream() noexcept;
    ~TlsStream() override;
    TlsStream(const TlsStream &) = delete;
    TlsStream &operator=(const TlsStream &) = delete;
    TlsStream(TlsStream &&) = delete;
    TlsStream &operator=(TlsStream &&) = delete;

    // Runs a client handshake over `transport`, which must already be connected
    // and must outlive this object. `host` is both the SNI name and the name the
    // certificate has to match; `roots_pem` is the trust store, one or more PEM
    // certificates (bundled_ca_pem() is the pinned production bundle, and the
    // tests pass their own). `timeout_ms` bounds the whole handshake.
    //
    // False means nothing was established -- the transport broke, the wait ran
    // out, the chain did not reach a root in the store, or the name did not
    // match -- and error() says which. A failed handshake leaves nothing open.
    bool handshake(core::Stream &transport, std::string_view host, std::string_view roots_pem,
                   int timeout_ms);

    // read: decrypted bytes, up to `size`, inside the caller's wait. write: the
    // whole buffer before returning ok, because a half-written request is worse
    // than a closed connection. Both answer `closed` when the peer ended the TLS
    // stream and `timeout` when the wait ran out with nothing to hand back; a
    // failure leaves the object as it was, so the caller can close it.
    core::StreamResult read(std::uint8_t *data, std::size_t size, int timeout_ms) noexcept override;
    core::StreamResult write(const std::uint8_t *data, std::size_t size,
                             int timeout_ms) noexcept override;

    // Drops the connection, keys and all. No close_notify is sent: there is no
    // wait budget here, and the layer that decides a conversation is over is the
    // one that sends its own goodbye. error() keeps the last diagnosis.
    void close() noexcept;
    bool is_open() const noexcept;

    const std::string &error() const noexcept
    {
        return error_;
    }

    // What the handshake settled on, for the log: the subject of the certificate
    // the peer presented and the protocol version in use ("TLSv1.3"). Empty until
    // a handshake has succeeded.
    const std::string &peer_subject() const noexcept
    {
        return peer_subject_;
    }
    const std::string &tls_version() const noexcept
    {
        return tls_version_;
    }

  private:
    // How much one trip to the transport moves. The buffer lives here rather than
    // on the stack: a thread stack on the console has no documented size, and one
    // allocation per connection is the cheaper side of that bet.
    static constexpr std::size_t kChunk = 16 * 1024;

    // What one call into the transport produced.
    enum class Wire
    {
        moved, // bytes came in and are waiting for OpenSSL
        quiet, // nothing arrived inside the wait
        gone,  // the peer ended the transport
        broke, // the transport failed
    };

    bool load_roots(std::string_view roots_pem);
    // Everything OpenSSL has produced goes out. False means it could not: the
    // transport failed or the wait is over with bytes still queued (error_ set).
    bool flush_out(std::int64_t deadline) noexcept;
    // One read from the transport, into the read BIO.
    Wire wait_on_wire(std::int64_t deadline) noexcept;

    core::Stream *transport_ = nullptr;
    SSL_CTX *ctx_ = nullptr;
    SSL *ssl_ = nullptr;
    BIO *rbio_ = nullptr; // owned by ssl_ once SSL_set_bio has run
    BIO *wbio_ = nullptr;
    bool established_ = false;
    std::string error_;
    std::string peer_subject_;
    std::string tls_version_;
    std::uint8_t chunk_[kChunk]{};
};
} // namespace accord::net
