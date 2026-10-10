/*
 * Accord - the inflate side of Discord's gateway stream compression.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * `compress=zlib-stream` is not a websocket extension: one zlib stream runs for
 * the whole connection, and every event ends where the sender left a
 * Z_SYNC_FLUSH behind, the four bytes `00 00 ff ff` (04-discord-api.md,
 * "Kompresja zlib-stream"). The bytes reach us in whatever pieces the binary
 * frames happen to carry, so they are collected here until a flush marker closes
 * the buffer, and only then inflated.
 *
 * Two things this class is careful about. The context is never reset between
 * messages -- that is the point of a transport stream, and a client that started
 * a fresh one per message would fail on the second; a new connection, a RESUME
 * included, is what starts a new context, through reset(). And the limits are
 * separate from the websocket's own: a compressed frame is still bounded by
 * kMaxMessageBytes, while what it inflates to is bounded here, so a small
 * compressed bomb cannot ask for memory. Passing either limit is an error and
 * the connection is meant to be dropped, never an unbounded allocation.
 */

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace accord::core
{
enum class ZlibStatus
{
    ok,
    corrupt,    // the bytes are not a zlib stream, or not the one that started here
    too_large,  // a message, or the backlog waiting for a marker, passed a limit
    no_context, // zlib could not start on this machine, so no peer is to blame
};

class ZlibStream
{
  public:
    // How far a message may inflate. Discord's events are kilobytes; this is
    // where a peer that claims a gigabyte is cut off instead of believed.
    static constexpr std::size_t kMaxInflatedBytes = 16u << 20;
    // And how much compressed data may pile up with no flush marker in sight. A
    // message that inflated to something is never larger than this, so a peer
    // this far behind is not sending one message in pieces.
    static constexpr std::size_t kMaxPendingBytes = kMaxInflatedBytes;

    ZlibStream();
    ZlibStream(const ZlibStream &) = delete;
    ZlibStream &operator=(const ZlibStream &) = delete;
    ~ZlibStream();

    // Back to a context that has read nothing: a new connection is a new stream.
    void reset();

    // Adds the bytes of one frame. ok means they were taken, whether or not they
    // completed a message; anything else is final, and error() says why.
    ZlibStatus feed(std::string_view bytes);

    // The next whole message in arrival order. False while none is complete,
    // which is the ordinary answer for the first frames of a split message.
    bool next(std::string *out);

    ZlibStatus status() const noexcept;
    bool failed() const noexcept;
    const std::string &error() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace accord::core
