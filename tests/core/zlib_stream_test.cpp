/*
 * Accord - Tests for the inflate side of Discord's gateway stream compression.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The fixtures are compressed by the pinned zlib itself, driven the way the
 * gateway drives it: one deflate context for the whole connection and a
 * Z_SYNC_FLUSH after every event. One vector written out by Python's zlib is
 * here as well, so a mistake shared by both halves of the same library --
 * compressing the fixture and decompressing it in the code -- cannot pass
 * unnoticed.
 */

#include "tests/micro_test.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "core/zlib_stream.hpp"
#include "tests/support/zlib_peer.hpp"

using namespace accord::core;
using accord::test::ZlibPeer;

namespace
{
constexpr std::string_view kFlushMarker{"\x00\x00\xff\xff", 4};

bool ends_with_marker(std::string_view bytes)
{
    return bytes.size() >= kFlushMarker.size() &&
           bytes.compare(bytes.size() - kFlushMarker.size(), kFlushMarker.size(), kFlushMarker) == 0;
}

std::string name_of(ZlibStatus status)
{
    switch (status)
    {
    case ZlibStatus::ok:
        return "ok";
    case ZlibStatus::corrupt:
        return "corrupt";
    case ZlibStatus::too_large:
        return "too_large";
    case ZlibStatus::no_context:
        return "no_context";
    }
    return "?";
}

int hex_digit(char c)
{
    return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
}

std::string unhex(std::string_view hex)
{
    std::string out;
    for (std::size_t at = 0; at + 1 < hex.size(); at += 2)
        out.push_back(
            static_cast<char>((hex_digit(hex[at]) << 4) | hex_digit(hex[at + 1])));
    return out;
}

// The one feed-and-take every test here does.
std::string take(ZlibStream &stream, std::string_view bytes)
{
    MICRO_CHECK_EQ(name_of(stream.feed(bytes)), std::string{"ok"});
    std::string message;
    MICRO_CHECK(stream.next(&message));
    return message;
}
} // namespace

MICRO_TEST(a_message_in_one_piece_comes_out_of_one_feed)
{
    constexpr std::string_view kHello = "{\"op\":10,\"d\":{\"heartbeat_interval\":41250}}";
    ZlibPeer peer;
    ZlibStream stream;

    const std::string frame = peer.push(kHello);
    MICRO_CHECK(ends_with_marker(frame)); // the fixture itself: one flush, one marker
    MICRO_CHECK_EQ(take(stream, frame), std::string{kHello});

    // One message, and nothing else waiting behind it.
    std::string message;
    MICRO_CHECK(!stream.next(&message));
    MICRO_CHECK(!stream.failed());
}

MICRO_TEST(a_message_split_over_five_pieces_comes_out_whole)
{
    constexpr std::string_view kEvent = "{\"op\":0,\"t\":\"READY\",\"s\":1}";
    ZlibPeer peer;
    ZlibStream stream;

    const std::string bytes = peer.push(kEvent);
    const std::size_t piece = bytes.size() / 5;
    std::string message;
    for (int i = 0; i < 4; ++i)
    {
        const std::string_view part{bytes.data() + i * piece, piece};
        MICRO_CHECK(!ends_with_marker(part)); // the first four carry no marker
        MICRO_CHECK_EQ(name_of(stream.feed(part)), std::string{"ok"});
        // Buffered, not inflated: half a block would decompress to half an
        // event, and the marker is what says the event is all there.
        MICRO_CHECK(!stream.next(&message));
        MICRO_CHECK(!stream.failed());
    }

    MICRO_CHECK_EQ(name_of(stream.feed(std::string_view{bytes}.substr(4 * piece))),
                   std::string{"ok"});
    MICRO_CHECK(stream.next(&message));
    MICRO_CHECK_EQ(message, std::string{kEvent});
}

MICRO_TEST(one_context_carries_every_event_of_the_connection)
{
    constexpr std::string_view kFirst = "{\"op\":10,\"d\":{\"heartbeat_interval\":41250}}";
    constexpr std::string_view kSecond = "{\"op\":11,\"d\":null}";
    constexpr std::string_view kThird = "{\"op\":0,\"t\":\"READY\",\"s\":1}";

    ZlibPeer peer;
    ZlibStream stream;
    MICRO_CHECK_EQ(take(stream, peer.push(kFirst)), std::string{kFirst});
    const std::string second = peer.push(kSecond);
    MICRO_CHECK_EQ(take(stream, second), std::string{kSecond});
    MICRO_CHECK_EQ(take(stream, peer.push(kThird)), std::string{kThird});

    // The second event's bytes carry no zlib header of their own, so a context
    // that started fresh for each event -- or a second context on the same
    // connection -- cannot read them. That is what makes "one context per
    // connection" a requirement rather than an implementation detail.
    ZlibStream fresh;
    MICRO_CHECK_EQ(name_of(fresh.feed(second)), std::string{"corrupt"});
}

MICRO_TEST(corrupt_bytes_are_refused)
{
    // What this layer can see is damage to the deflate structure: a header whose
    // check bits do not add up, a stored block whose length and its complement
    // disagree, a code that cannot be. What it cannot see is damage that happens
    // to decode as other valid codes -- this framing carries no per-message
    // checksum, the zlib trailer is only checked when a stream ends -- and a
    // mangled event like that dies one layer up, in the JSON reader. Which is
    // why the cases below are the structural ones rather than a flipped byte,
    // whose fate depends on the data it lands in.
    // A char array rather than a string literal: this one is full of zeroes, and
    // a string_view built from a literal would stop at the first of them.
    constexpr char kBadStoredBlock[] =
        "\x78\x9c"       // a valid zlib header
        "\x00\x05\x00"   // stored block, LEN 5
        "\x00\x00"       // and NLEN 0, where ~5 was required
        "\x00\x00\xff\xff"; // a flush marker, so the reader inflates it

    ZlibStream stream;
    MICRO_CHECK_EQ(name_of(stream.feed(std::string_view{kBadStoredBlock,
                                                       sizeof(kBadStoredBlock) - 1})),
                   std::string{"corrupt"});
    MICRO_CHECK(stream.failed());
    MICRO_CHECK(!stream.error().empty());

    // The first answer is the one that counts: a corrupt connection does not
    // recover on the next frame.
    ZlibPeer peer;
    MICRO_CHECK_EQ(name_of(stream.feed(peer.push("{\"op\":1}"))), std::string{"corrupt"});

    // And a stream that does not start with a zlib header at all.
    ZlibPeer fresh_peer;
    std::string no_header = fresh_peer.push("{\"op\":1}");
    no_header[0] = 0x79; // the header's check bits no longer add up
    ZlibStream second;
    MICRO_CHECK_EQ(name_of(second.feed(no_header)), std::string{"corrupt"});
    MICRO_CHECK(std::string_view{second.error()}.find("zlib") != std::string_view::npos);
}

MICRO_TEST(a_message_over_the_inflated_limit_is_refused)
{
    // Compression is what makes this the dangerous direction: a few kilobytes
    // on the wire claim 16 MiB of memory, and the limit is what refuses it.
    ZlibPeer peer;
    const std::string bomb(ZlibStream::kMaxInflatedBytes + 1, 'a');
    const std::string wire = peer.push(bomb);
    MICRO_CHECK(wire.size() < 64 * 1024); // the bomb is small on the wire

    ZlibStream stream;
    MICRO_CHECK_EQ(name_of(stream.feed(wire)), std::string{"too_large"});
    MICRO_CHECK(stream.failed());

    // Nothing partial is handed over: the caller never sees half a message.
    std::string message;
    MICRO_CHECK(!stream.next(&message));
}

MICRO_TEST(compressed_bytes_with_no_marker_pile_up_only_to_the_limit)
{
    ZlibStream stream;
    // No marker anywhere in this, and nothing else either: it stands for a peer
    // that keeps sending bits of a message it never flushes.
    const std::string chunk(1u << 20, 'x');
    ZlibStatus last = ZlibStatus::ok;
    int feeds = 0;
    while (last == ZlibStatus::ok && feeds < 64)
    {
        last = stream.feed(chunk);
        ++feeds;
    }
    MICRO_CHECK_EQ(name_of(last), std::string{"too_large"});
    MICRO_CHECK(stream.failed());
    MICRO_CHECK(feeds <= static_cast<int>(ZlibStream::kMaxPendingBytes / chunk.size()) + 2);
}

MICRO_TEST(an_empty_flush_is_an_empty_message)
{
    ZlibPeer peer;
    ZlibStream stream;
    MICRO_CHECK_EQ(take(stream, peer.push("{\"op\":1}")), std::string{"{\"op\":1}"});

    // A flush with nothing behind it is an empty stored block: its header byte,
    // LEN 0, NLEN 0xffff -- five bytes whose last four are the marker. zlib's own
    // deflate writes nothing at all when a flush has nothing to flush, so the
    // peer here is those five bytes directly, which is also what a bare marker
    // from the gateway reads as.
    const std::string empty_event{"\x00\x00\x00\xff\xff", 5};
    MICRO_CHECK(ends_with_marker(empty_event));

    // Delivered as an event of zero bytes: what to make of an empty payload is
    // the JSON layer's business, and dropping it here would be this file
    // deciding it.
    std::string message{"not empty"};
    MICRO_CHECK_EQ(name_of(stream.feed(empty_event)), std::string{"ok"});
    MICRO_CHECK(stream.next(&message));
    MICRO_CHECK_EQ(message, std::string{});
    MICRO_CHECK(!stream.failed());
}

MICRO_TEST(a_reset_puts_the_context_back_to_the_start)
{
    constexpr std::string_view kFirst = "{\"op\":10,\"d\":{\"heartbeat_interval\":41250}}";
    constexpr std::string_view kSecond = "{\"op\":11,\"d\":null}";

    ZlibPeer peer;
    ZlibStream stream;
    MICRO_CHECK_EQ(take(stream, peer.push(kFirst)), std::string{kFirst});

    // A connection that starts over needs both ends to start over: the bytes of
    // a stream that has already run carry no header of their own, so this peer's
    // next event cannot be read by a reset context. Discord's RESUME is a new
    // connection, which is the same rule seen from the other end.
    stream.reset();
    MICRO_CHECK_EQ(name_of(stream.feed(peer.push(kSecond))), std::string{"corrupt"});
    MICRO_CHECK(stream.failed());

    // Reset is also the way out of a failure: the next connection is new, and a
    // peer that starts its stream over is read again.
    stream.reset();
    ZlibPeer new_peer;
    MICRO_CHECK(!stream.failed());
    MICRO_CHECK_EQ(take(stream, new_peer.push(kFirst)), std::string{kFirst});
}

MICRO_TEST(a_peer_that_finishes_its_stream_is_refused)
{
    constexpr std::string_view kHello = "{\"op\":10,\"d\":{\"heartbeat_interval\":41250}}";
    ZlibPeer peer;
    ZlibStream stream;
    MICRO_CHECK_EQ(take(stream, peer.push(kHello)), std::string{kHello});

    // The finish sequence closes the zlib stream and carries no marker, so it
    // waits in the buffer like any other bytes without one. Put a marker behind
    // it and the reader reaches the end of the stream with bytes to spare --
    // which is the disagreement this refuses.
    std::string ending = peer.finish();
    ending.append(kFlushMarker);
    MICRO_CHECK_EQ(name_of(stream.feed(ending)), std::string{"corrupt"});
    MICRO_CHECK(stream.failed());
}

MICRO_TEST(the_vector_from_another_zlib_decodes)
{
    // Compressed by Python's zlib: compressobj, then flush(Z_SYNC_FLUSH), for
    // the string below. Nothing in this repository made it.
    constexpr std::string_view kWire =
        "789caa56ca2f50b23234d0514a51b2aa56ca484d2c2a494a4d2c89cfcc2b492d2a4bcc51b23231343235a8ad0500"
        "0000ffff";
    constexpr std::string_view kEvent = "{\"op\":10,\"d\":{\"heartbeat_interval\":41250}}";

    const std::string bytes = unhex(kWire);
    MICRO_CHECK(ends_with_marker(bytes));
    ZlibStream stream;
    MICRO_CHECK_EQ(take(stream, bytes), std::string{kEvent});
}
