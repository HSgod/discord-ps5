/*
 * Accord - the TLS client, against a local server that holds a certificate.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The criterion this file answers: a host test with a local server, a chain that
 * has to reach a pinned root, and a name that has to match. So the tests are
 * about the two things TLS is for -- talking to a peer you trust, and refusing
 * one you do not -- and about the ways that can go wrong without a crash: a
 * certificate from another CA, a certificate for another name, a store that is
 * not a store, a peer that talks rubbish, a peer that says nothing at all.
 *
 * What is NOT here is a test that turns verification off, because there is no
 * option like that to test: handshake() takes roots and a name and nothing else,
 * and the two tests that a mismatched name and an untrusted chain still fail are
 * what keeps it that way.
 */

#include "tests/micro_test.hpp"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include "core/stream.hpp"
#include "platform/net/ca_bundle.hpp"
#include "platform/net/tcp_stream.hpp"
#include "platform/net/tls_stream.hpp"
#include "tests/support/tls_server.hpp"

using accord::core::StreamResult;
using accord::core::StreamStatus;
using accord::net::TcpStream;
using accord::net::TlsStream;
using accord::test::TlsServer;

namespace
{
constexpr std::string_view kFixtures = "tests/fixtures/tls";
constexpr std::string_view kHost = "localhost";
constexpr std::string_view kRequest =
    "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
constexpr int kHandshakeMs = 5000;
constexpr int kIoMs = 3000;

std::string read_file(const std::string &path)
{
    std::ifstream file{path, std::ios::binary};
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

std::string lower_ascii(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text)
        out.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
    return out;
}

std::string name_of(StreamStatus status)
{
    switch (status)
    {
    case StreamStatus::ok:
        return "ok";
    case StreamStatus::timeout:
        return "timeout";
    case StreamStatus::closed:
        return "closed";
    case StreamStatus::failed:
        return "failed";
    }
    return "?";
}

// The test CA: what a client is given to trust for everything below.
std::string test_ca()
{
    return read_file(std::string{kFixtures} + "/ca.pem");
}

// The whole response, read until the peer says goodbye. Anything but a byte or a
// goodbye is a failure worth seeing.
std::string read_to_end(TlsStream &tls, int timeout_ms)
{
    std::string text;
    for (;;)
    {
        std::uint8_t buffer[4096];
        const StreamResult got = tls.read(buffer, sizeof(buffer), timeout_ms);
        if (got.status == StreamStatus::ok)
        {
            text.append(reinterpret_cast<const char *>(buffer), got.transferred);
            continue;
        }
        MICRO_CHECK_EQ(name_of(got.status), std::string{"closed"});
        break;
    }
    return text;
}

// Connects, hands the bytes to TLS, and reports both steps. The transport has to
// outlive the TlsStream, which is why the caller owns it.
bool connect_and_handshake(TlsServer &server, std::string_view host, TcpStream &transport,
                           TlsStream &tls, std::string_view roots, int timeout_ms = kHandshakeMs)
{
    if (!transport.connect("127.0.0.1", server.port(), kIoMs))
        return false;
    return tls.handshake(transport, host, roots, timeout_ms);
}

std::string bytes_of(std::string_view text)
{
    return std::string{text};
}

// Asserts what the client says about a refusal, and shows what it actually said
// when it does not: the OpenSSL prefix in front of that text carries error codes
// worth not pinning a test to, so only the part this project writes is checked.
void check_error_says(const TlsStream &tls, std::string_view needle)
{
    const std::string &error = tls.error();
    if (error.find(needle) == std::string::npos)
        micro_test::record_failure(__FILE__, __LINE__,
                                   std::string{"the error does not say \""} +
                                       std::string{needle} + "\": got \"" + error + "\"");
}
} // namespace

// --- a peer worth talking to ------------------------------------------------

MICRO_TEST(a_trusted_certificate_with_the_right_name_opens_the_connection)
{
    TlsServer server;
    MICRO_CHECK(server.start(TlsServer::Mode::answer_get, TlsServer::Cert::trusted, kFixtures));

    TcpStream transport;
    TlsStream tls;
    MICRO_CHECK(connect_and_handshake(server, kHost, transport, tls, test_ca()));
    MICRO_CHECK_EQ(tls.error(), std::string{}); // nothing went wrong, and nothing says otherwise

    MICRO_CHECK(tls.is_open());
    MICRO_CHECK_EQ(tls.tls_version(), std::string{"TLSv1.3"});
    MICRO_CHECK(tls.peer_subject().find("localhost") != std::string::npos);

    const std::string request = bytes_of(kRequest);
    const StreamResult written =
        tls.write(reinterpret_cast<const std::uint8_t *>(request.data()), request.size(), kIoMs);
    MICRO_CHECK_EQ(name_of(written.status), std::string{"ok"});
    MICRO_CHECK_EQ(written.transferred, request.size());

    const std::string response = read_to_end(tls, kIoMs);
    MICRO_CHECK_EQ(response.compare(0, 15, "HTTP/1.1 200 OK"), 0);
    MICRO_CHECK(response.find(TlsServer::kBody) != std::string::npos);

    // And the server saw the request we think we sent.
    MICRO_CHECK_EQ(server.request(), request);
    MICRO_CHECK(server.handshake_ok());
}

MICRO_TEST(a_response_larger_than_one_record_arrives_whole)
{
    TlsServer server;
    MICRO_CHECK(
        server.start(TlsServer::Mode::big_response, TlsServer::Cert::trusted, kFixtures));

    TcpStream transport;
    TlsStream tls;
    MICRO_CHECK(connect_and_handshake(server, kHost, transport, tls, test_ca()));
    MICRO_CHECK_EQ(tls.error(), std::string{});

    const std::string request = bytes_of(kRequest);
    tls.write(reinterpret_cast<const std::uint8_t *>(request.data()), request.size(), kIoMs);

    const std::string response = read_to_end(tls, kIoMs);
    const std::string head = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " +
                             std::to_string(TlsServer::kBigBodySize) +
                             "\r\nConnection: close\r\n\r\n";
    MICRO_CHECK_EQ(response.size(), head.size() + TlsServer::kBigBodySize);
    MICRO_CHECK(response.compare(0, head.size(), head) == 0);

    // Every byte of the body is the pattern the server repeated, which is how a
    // dropped or shuffled record would show up.
    const std::string body = response.substr(head.size());
    bool pattern_ok = body.size() == TlsServer::kBigBodySize;
    for (std::size_t at = 0; pattern_ok && at < body.size(); ++at)
        pattern_ok = body[at] == TlsServer::kBigBodyUnit[at % TlsServer::kBigBodyUnit.size()];
    MICRO_CHECK(pattern_ok);
}

// --- the two refusals this layer exists for ---------------------------------

MICRO_TEST(a_certificate_from_another_ca_is_refused)
{
    // The same name and the same shape of certificate, signed by itself rather
    // than by the CA the client was handed: the chain is the only thing wrong.
    TlsServer server;
    MICRO_CHECK(
        server.start(TlsServer::Mode::answer_get, TlsServer::Cert::selfsigned, kFixtures));

    TcpStream transport;
    TlsStream tls;
    MICRO_CHECK(!connect_and_handshake(server, kHost, transport, tls, test_ca()));
    MICRO_CHECK(!tls.is_open());
    check_error_says(tls, "certificate was refused");
}

MICRO_TEST(a_certificate_for_another_name_is_refused)
{
    // A certificate the client does trust, and a name it does not carry.
    TlsServer server;
    MICRO_CHECK(server.start(TlsServer::Mode::answer_get, TlsServer::Cert::trusted, kFixtures));

    TcpStream transport;
    TlsStream tls;
    MICRO_CHECK(!connect_and_handshake(server, "example.test", transport, tls, test_ca()));
    MICRO_CHECK(!tls.is_open());
    check_error_says(tls, "hostname mismatch");
    }

MICRO_TEST(a_store_that_does_not_hold_the_issuer_cannot_verify_anything)
{
    // The production bundle, against the test CA's server: the store loads (121
    // roots, and a failure to load says so instead), and it still has no reason
    // to accept this chain. This is the one test that runs the pinned roots
    // end to end.
    TlsServer server;
    MICRO_CHECK(server.start(TlsServer::Mode::answer_get, TlsServer::Cert::trusted, kFixtures));

    TcpStream transport;
    TlsStream tls;
    MICRO_CHECK(!connect_and_handshake(server, kHost, transport, tls, accord::net::bundled_ca_pem()));
    MICRO_CHECK(!tls.is_open());
    check_error_says(tls, "certificate was refused");
}

// --- roots that are not roots ----------------------------------------------

MICRO_TEST(a_handshake_without_a_trust_store_is_refused)
{
    // The only way to reach "verify nothing" would be to empty the store, and
    // this is what happens instead: nothing is attempted at all.
    TcpStream never_connected;
    TlsStream tls;
    MICRO_CHECK(!tls.handshake(never_connected, kHost, std::string_view{}, kHandshakeMs));
    MICRO_CHECK(!tls.is_open());
    check_error_says(tls, "no trust store");
    }

MICRO_TEST(a_handshake_without_a_host_name_is_refused)
{
    TcpStream never_connected;
    TlsStream tls;
    MICRO_CHECK(!tls.handshake(never_connected, std::string_view{}, test_ca(), kHandshakeMs));
    MICRO_CHECK(!tls.is_open());
    check_error_says(tls, "no host name");
}

MICRO_TEST(roots_that_are_not_certificates_are_refused)
{
    TcpStream never_connected;
    TlsStream tls;
    const std::string nonsense = "this is not a certificate\n";
    MICRO_CHECK(!tls.handshake(never_connected, kHost, nonsense, kHandshakeMs));
    MICRO_CHECK(!tls.is_open());
    check_error_says(tls, "holds no certificates");
    }

MICRO_TEST(nothing_can_be_read_or_written_before_a_handshake)
{
    TlsStream tls;
    std::uint8_t buffer[16];
    MICRO_CHECK_EQ(name_of(tls.read(buffer, sizeof(buffer), 0).status), std::string{"failed"});
    MICRO_CHECK_EQ(name_of(tls.write(buffer, sizeof(buffer), 0).status), std::string{"failed"});
    MICRO_CHECK(!tls.is_open());
    MICRO_CHECK(!tls.error().empty());
}

// --- peers that do not hold up their end ------------------------------------

MICRO_TEST(a_peer_that_does_not_speak_tls_fails_the_handshake)
{
    TlsServer server;
    MICRO_CHECK(server.start(TlsServer::Mode::no_tls, TlsServer::Cert::trusted, kFixtures));

    TcpStream transport;
    TlsStream tls;
    MICRO_CHECK(!connect_and_handshake(server, kHost, transport, tls, test_ca()));
    MICRO_CHECK(!tls.is_open());
    MICRO_CHECK(!tls.error().empty());
    }

MICRO_TEST(a_peer_that_says_nothing_ends_the_handshake_when_the_wait_does)
{
    TlsServer server;
    MICRO_CHECK(server.start(TlsServer::Mode::stall, TlsServer::Cert::trusted, kFixtures));

    TcpStream transport;
    TlsStream tls;
    const auto started = std::chrono::steady_clock::now();
    MICRO_CHECK(!connect_and_handshake(server, kHost, transport, tls, test_ca(), 300));
    const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - started)
                           .count();
    MICRO_CHECK(!tls.is_open());
    check_error_says(tls, "handshake timed out");
    // The deadline is what ended it, and it ended it when it said it would.
    MICRO_CHECK(spent < 2000);
}

MICRO_TEST(a_quiet_server_ends_a_read_with_a_timeout_and_not_a_hang)
{
    TlsServer server;
    MICRO_CHECK(server.start(TlsServer::Mode::quiet, TlsServer::Cert::trusted, kFixtures));

    TcpStream transport;
    TlsStream tls;
    MICRO_CHECK(connect_and_handshake(server, kHost, transport, tls, test_ca()));
    MICRO_CHECK_EQ(tls.error(), std::string{});

    const auto started = std::chrono::steady_clock::now();
    std::uint8_t buffer[64];
    const StreamResult got = tls.read(buffer, sizeof(buffer), 200);
    const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - started)
                           .count();
    MICRO_CHECK_EQ(name_of(got.status), std::string{"timeout"});
    MICRO_CHECK(got.transferred == 0);
    MICRO_CHECK(spent < 2000);
    MICRO_CHECK(tls.is_open()); // a wait that ran out is not a broken connection
}

MICRO_TEST(a_peer_that_goes_away_is_a_closed_stream_and_not_a_failure)
{
    TlsServer server;
    MICRO_CHECK(
        server.start(TlsServer::Mode::answer_get, TlsServer::Cert::trusted, kFixtures));

    TcpStream transport;
    TlsStream tls;
    MICRO_CHECK(connect_and_handshake(server, kHost, transport, tls, test_ca()));

    // No request at all: the server's read times out, it says goodbye properly,
    // and that goodbye is what the client has to report.
    std::uint8_t buffer[64];
    const StreamResult got = tls.read(buffer, sizeof(buffer), 4000);
    MICRO_CHECK_EQ(name_of(got.status), std::string{"closed"});
    }

// --- the bundle the artifacts carry -----------------------------------------

MICRO_TEST(the_embedded_bundle_is_the_file_that_was_fetched)
{
    // tools/fetch-cacert.sh fetches the pinned bundle and tools/embed-cacert.sh
    // turns it into the source the binary compiles. Both run before this test
    // can, so the two have to agree byte for byte -- and the headers say which
    // extraction it is.
    const std::string on_disk = read_file("third_party/cacert/cacert.pem");
    MICRO_CHECK(!on_disk.empty());
    const std::string_view embedded = accord::net::bundled_ca_pem();
    MICRO_CHECK_EQ(embedded.size(), on_disk.size());
    MICRO_CHECK_EQ(std::string{embedded}, on_disk);

    std::size_t certificates = 0;
    for (std::size_t at = on_disk.find("-----BEGIN CERTIFICATE-----"); at != std::string::npos;
         at = on_disk.find("-----BEGIN CERTIFICATE-----", at + 1))
        ++certificates;
    MICRO_CHECK(certificates >= 100);
    MICRO_CHECK(on_disk.find("Certificate data from Mozilla as of:") != std::string::npos);
}
