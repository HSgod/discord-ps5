/*
 * Accord - TLS over any core::Stream, with the peer always verified.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "platform/net/tls_stream.hpp"

#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509err.h>
#include <openssl/x509v3.h>

#include <chrono>
#include <climits>
#include <utility>

namespace accord::net
{
namespace
{
// Same clock as the transport below: the waits here are budgets handed to it.
std::int64_t now_ms() noexcept
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::int64_t deadline_of(int timeout_ms) noexcept
{
    return now_ms() + (timeout_ms > 0 ? timeout_ms : 0);
}

// What is left of the wait, never negative. Zero means "look, do not wait",
// which is exactly how the transport below reads it.
int left_ms(std::int64_t deadline) noexcept
{
    const std::int64_t left = deadline - now_ms();
    if (left <= 0)
        return 0;
    return left > INT_MAX ? INT_MAX : static_cast<int>(left);
}

// Everything OpenSSL has put on the error queue since it was cleared, as one
// line. The queue is drained here so that the next failure is not reported with
// this call's text.
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
} // namespace

TlsStream::TlsStream() noexcept = default;

TlsStream::~TlsStream()
{
    close();
}

void TlsStream::close() noexcept
{
    // SSL_free releases both memory BIOs: SSL_set_bio handed them over. error()
    // is left alone, so a caller that closes after a failure can still say why.
    if (ssl_ != nullptr)
    {
        ::SSL_free(ssl_);
        ssl_ = nullptr;
        rbio_ = nullptr;
        wbio_ = nullptr;
    }
    if (ctx_ != nullptr)
    {
        ::SSL_CTX_free(ctx_);
        ctx_ = nullptr;
    }
    transport_ = nullptr;
    established_ = false;
}

bool TlsStream::is_open() const noexcept
{
    return established_ && ssl_ != nullptr;
}

bool TlsStream::load_roots(std::string_view roots_pem)
{
    X509_STORE *store = ::SSL_CTX_get_cert_store(ctx_);
    if (store == nullptr)
    {
        error_ = "tls: the context has no certificate store";
        return false;
    }
    if (roots_pem.size() > static_cast<std::size_t>(INT_MAX))
    {
        error_ = "tls: the trust store is too large to read";
        return false;
    }

    BIO *bio = ::BIO_new_mem_buf(roots_pem.data(), static_cast<int>(roots_pem.size()));
    if (bio == nullptr)
    {
        error_ = openssl_error("BIO_new_mem_buf");
        return false;
    }

    int loaded = 0;
    for (;;)
    {
        X509 *cert = ::PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
        if (cert == nullptr)
            break; // end of the bundle: the error queue says so and is cleared below
        if (::X509_STORE_add_cert(store, cert) != 1)
        {
            // A published bundle can carry the same root twice -- the same
            // subject and key under a second name, or a straight repeat -- and
            // the store refuses the copy. That is the one add that is not a
            // failure; anything else means these are not roots we can use.
            const unsigned long code = ::ERR_peek_last_error();
            if (::ERR_GET_REASON(code) != X509_R_CERT_ALREADY_IN_HASH_TABLE)
            {
                error_ = openssl_error("X509_STORE_add_cert");
                ::X509_free(cert);
                ::BIO_free(bio);
                return false;
            }
            ::ERR_clear_error();
        }
        ++loaded;
        ::X509_free(cert);
    }
    ::ERR_clear_error(); // PEM_read_bio_X509 reports the end of input as an error

    if (loaded == 0)
    {
        error_ = "tls: the trust store holds no certificates";
        ::BIO_free(bio);
        return false;
    }
    ::BIO_free(bio);
    return true;
}

bool TlsStream::flush_out(std::int64_t deadline) noexcept
{
    while (::BIO_ctrl_pending(wbio_) > 0)
    {
        const int got = ::BIO_read(wbio_, chunk_, static_cast<int>(kChunk));
        if (got <= 0)
            break; // nothing left to take
        const core::StreamResult sent =
            transport_->write(chunk_, static_cast<std::size_t>(got), left_ms(deadline));
        if (sent.status != core::StreamStatus::ok || sent.transferred != static_cast<std::size_t>(got))
        {
            error_ = "tls: the transport did not take the bytes OpenSSL produced";
            return false;
        }
    }
    return true;
}

TlsStream::Wire TlsStream::wait_on_wire(std::int64_t deadline) noexcept
{
    if (!flush_out(deadline))
        return Wire::broke;

    const core::StreamResult got = transport_->read(chunk_, kChunk, left_ms(deadline));
    switch (got.status)
    {
    case core::StreamStatus::ok:
        if (got.transferred == 0)
            return Wire::quiet; // the contract says ok moves a byte; be safe anyway
        if (::BIO_write(rbio_, chunk_, static_cast<int>(got.transferred)) !=
            static_cast<int>(got.transferred))
        {
            error_ = "tls: the TLS library would not take the bytes from the wire";
            return Wire::broke;
        }
        return Wire::moved;
    case core::StreamStatus::timeout:
        return Wire::quiet;
    case core::StreamStatus::closed:
        return Wire::gone;
    case core::StreamStatus::failed:
        error_ = "tls: the transport failed while reading";
        return Wire::broke;
    }
    error_ = "tls: the transport answered with a status that does not exist";
    return Wire::broke;
}

bool TlsStream::handshake(core::Stream &transport, std::string_view host,
                          std::string_view roots_pem, int timeout_ms)
{
    close();
    error_.clear();
    peer_subject_.clear();
    tls_version_.clear();

    // Two things this layer will not do without: a name to check the certificate
    // against, and roots to check it against. Neither has a default, and neither
    // can be turned off, so an empty one is a bug in the caller and ends here.
    if (host.empty())
    {
        error_ = "tls: refusing a handshake with no host name to verify";
        return false;
    }
    if (roots_pem.empty())
    {
        error_ = "tls: refusing a handshake with no trust store";
        return false;
    }

    ctx_ = ::SSL_CTX_new(::TLS_client_method());
    if (ctx_ == nullptr)
    {
        error_ = openssl_error("SSL_CTX_new");
        return false;
    }
    // The gateway speaks TLS 1.2 and 1.3; this is where older versions stop
    // being on offer at all.
    if (::SSL_CTX_set_min_proto_version(ctx_, TLS1_2_VERSION) != 1)
    {
        error_ = openssl_error("SSL_CTX_set_min_proto_version");
        close();
        return false;
    }
    // Verification against the store below is unconditional. There is no path
    // from this file to SSL_VERIFY_NONE, and no argument that could ask for one.
    ::SSL_CTX_set_verify(ctx_, SSL_VERIFY_PEER, nullptr);
    if (!load_roots(roots_pem))
    {
        close();
        return false;
    }

    ssl_ = ::SSL_new(ctx_);
    if (ssl_ == nullptr)
    {
        error_ = openssl_error("SSL_new");
        close();
        return false;
    }
    rbio_ = ::BIO_new(::BIO_s_mem());
    wbio_ = ::BIO_new(::BIO_s_mem());
    if (rbio_ == nullptr || wbio_ == nullptr)
    {
        error_ = openssl_error("BIO_new");
        close();
        return false;
    }
    // An empty memory BIO must read as "no bytes yet", not as end of stream: for
    // OpenSSL the difference is WANT_READ against a peer that went away.
    ::BIO_set_mem_eof_return(rbio_, -1);
    ::BIO_set_mem_eof_return(wbio_, -1);
    ::SSL_set_bio(ssl_, rbio_, wbio_); // ssl_ owns both from here
    ::SSL_set_connect_state(ssl_);

    const std::string name{host};
    if (::SSL_set_purpose(ssl_, X509_PURPOSE_SSL_SERVER) != 1)
    {
        error_ = openssl_error("SSL_set_purpose");
        close();
        return false;
    }
    // The name to check the certificate against -- the check this layer exists
    // for. Wildcards, SAN entries and the rest follow the usual rules.
    if (::SSL_set1_host(ssl_, name.c_str()) != 1)
    {
        error_ = openssl_error("SSL_set1_host");
        close();
        return false;
    }
    // And the same name as SNI, so a server with several certificates hands back
    // the one this check is about.
    if (::SSL_set_tlsext_host_name(ssl_, name.c_str()) != 1)
    {
        error_ = openssl_error("SSL_set_tlsext_host_name");
        close();
        return false;
    }

    transport_ = &transport;
    const std::int64_t deadline = deadline_of(timeout_ms);

    for (;;)
    {
        ::ERR_clear_error();
        const int done = ::SSL_do_handshake(ssl_);
        if (done == 1)
            break;

        const int reason = ::SSL_get_error(ssl_, done);
        if (reason == SSL_ERROR_WANT_READ || reason == SSL_ERROR_WANT_WRITE)
        {
            switch (wait_on_wire(deadline))
            {
            case Wire::moved:
                continue;
            case Wire::quiet:
                // A read that timed out is an ordinary answer: the transport's
                // wait can be shorter than this deadline. Keep going until the
                // deadline itself has passed.
                if (left_ms(deadline) > 0)
                    continue;
                error_ = "tls: the handshake timed out";
                break;
            case Wire::gone:
                error_ = "tls: the peer closed the connection during the handshake";
                break;
            case Wire::broke:
                break; // wait_on_wire() set error_
            }
            close();
            return false;
        }

        // Any other reason ends the handshake, and the error queue says why.
        // When it was the certificate, so does the verify result.
        std::string text = openssl_error("SSL_do_handshake");
        const long refused = ::SSL_get_verify_result(ssl_);
        if (refused != X509_V_OK)
        {
            text += " (the certificate was refused: ";
            text += ::X509_verify_cert_error_string(refused);
            text += ")";
        }
        error_ = std::move(text);
        close();
        return false;
    }

    // The handshake is done; the peer is what it claims to be or it is nothing.
    const long refused = ::SSL_get_verify_result(ssl_);
    if (refused != X509_V_OK)
    {
        error_ = std::string{"tls: the certificate was refused: "} +
                 ::X509_verify_cert_error_string(refused);
        close();
        return false;
    }
    X509 *peer = ::SSL_get1_peer_certificate(ssl_);
    if (peer == nullptr)
    {
        error_ = "tls: the peer presented no certificate";
        close();
        return false;
    }
    char *subject = ::X509_NAME_oneline(::X509_get_subject_name(peer), nullptr, 0);
    if (subject != nullptr)
    {
        peer_subject_ = subject;
        ::OPENSSL_free(subject);
    }
    ::X509_free(peer);
    const char *version = ::SSL_get_version(ssl_);
    tls_version_ = version != nullptr ? version : "";

    established_ = true;
    return true;
}

core::StreamResult TlsStream::read(std::uint8_t *data, std::size_t size, int timeout_ms) noexcept
{
    if (!is_open() || transport_ == nullptr)
    {
        if (error_.empty())
            error_ = "tls: nothing can be read: no handshake has succeeded here";
        return {core::StreamStatus::failed, 0};
    }

    const std::int64_t deadline = deadline_of(timeout_ms);
    const int want = size > static_cast<std::size_t>(INT_MAX) ? INT_MAX : static_cast<int>(size);

    for (;;)
    {
        // Whatever the handshake left queued -- a session ticket, most often --
        // goes out before we wait for anything.
        if (!flush_out(deadline))
            return {core::StreamStatus::failed, 0};

        ::ERR_clear_error();
        const int got = ::SSL_read(ssl_, data, want);
        if (got > 0)
            return {core::StreamStatus::ok, static_cast<std::size_t>(got)};

        const int reason = ::SSL_get_error(ssl_, got);
        if (reason == SSL_ERROR_ZERO_RETURN)
            return {core::StreamStatus::closed, 0}; // the peer said goodbye, properly

        if (reason == SSL_ERROR_WANT_READ || reason == SSL_ERROR_WANT_WRITE)
        {
            switch (wait_on_wire(deadline))
            {
            case Wire::moved:
                continue;
            case Wire::quiet:
                if (left_ms(deadline) > 0)
                    continue;
                return {core::StreamStatus::timeout, 0};
            case Wire::gone:
                return {core::StreamStatus::closed, 0};
            case Wire::broke:
                return {core::StreamStatus::failed, 0}; // error_ set by wait_on_wire()
            }
        }

        // A peer that ends the TCP stream without a close_notify comes back as a
        // syscall error with an empty error queue -- and for a memory BIO that
        // is precisely the case: no bytes, no record, nothing on the wire.
        if (reason == SSL_ERROR_SYSCALL && ::BIO_ctrl_pending(rbio_) == 0)
            return {core::StreamStatus::closed, 0};

        error_ = openssl_error("SSL_read");
        return {core::StreamStatus::failed, 0};
    }
}

core::StreamResult TlsStream::write(const std::uint8_t *data, std::size_t size,
                                    int timeout_ms) noexcept
{
    if (!is_open() || transport_ == nullptr)
    {
        if (error_.empty())
            error_ = "tls: nothing can be written: no handshake has succeeded here";
        return {core::StreamStatus::failed, 0};
    }

    const std::int64_t deadline = deadline_of(timeout_ms);
    std::size_t sent = 0;
    while (sent < size)
    {
        const std::size_t left = size - sent;
        const int want = left > static_cast<std::size_t>(INT_MAX) ? INT_MAX : static_cast<int>(left);

        ::ERR_clear_error();
        const int wrote = ::SSL_write(ssl_, data + sent, want);
        if (wrote > 0)
        {
            sent += static_cast<std::size_t>(wrote);
            continue;
        }

        const int reason = ::SSL_get_error(ssl_, wrote);
        if (reason == SSL_ERROR_ZERO_RETURN)
            return {core::StreamStatus::closed, 0};
        if (reason == SSL_ERROR_WANT_READ || reason == SSL_ERROR_WANT_WRITE)
        {
            switch (wait_on_wire(deadline))
            {
            case Wire::moved:
                continue;
            case Wire::quiet:
                if (left_ms(deadline) > 0)
                    continue;
                error_ = "tls: the record did not leave inside the wait";
                return {core::StreamStatus::timeout, 0};
            case Wire::gone:
                return {core::StreamStatus::closed, 0};
            case Wire::broke:
                return {core::StreamStatus::failed, 0}; // error_ set by wait_on_wire()
            }
        }

        error_ = openssl_error("SSL_write");
        return {core::StreamStatus::failed, 0};
    }

    // The buffer is OpenSSL's, not the wire's: the caller asked for a write, so
    // the bytes are pushed out before this answers ok.
    if (!flush_out(deadline))
        return {core::StreamStatus::failed, 0};
    return {core::StreamStatus::ok, size};
}
} // namespace accord::net
