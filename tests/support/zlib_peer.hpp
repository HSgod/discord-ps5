/*
 * Accord - the peer's side of the gateway's stream compression, for tests.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * One deflate context for a whole connection and a Z_SYNC_FLUSH after every
 * event: exactly what a gateway does when it was asked for `compress=zlib-stream`,
 * and what a client has to be able to read. The compression comes from the
 * pinned zlib itself, so the fixtures and the reader are not two independent
 * readings of the format -- one vector made by Python's zlib lives in the tests
 * for that reason.
 *
 * Header only, and only for the host tests: it needs deflate, which the soak
 * binary does not link.
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "third_party/zlib/zlib.h"

namespace accord::test
{
class ZlibPeer
{
  public:
    ZlibPeer() : ready_(deflateInit(&stream_, Z_DEFAULT_COMPRESSION) == Z_OK) {}

    ZlibPeer(const ZlibPeer &) = delete;
    ZlibPeer &operator=(const ZlibPeer &) = delete;

    ~ZlibPeer()
    {
        if (ready_)
            deflateEnd(&stream_);
    }

    bool ready() const noexcept { return ready_; }

    // The bytes one event adds to the stream: its data, then the flush marker
    // the reader waits for. An event with no data adds nothing at all -- zlib
    // writes no flush for an empty one.
    std::string push(std::string_view message)
    {
        stream_.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(message.data()));
        stream_.avail_in = static_cast<uInt>(message.size());

        std::string out;
        std::uint8_t chunk[1024];
        do
        {
            stream_.next_out = chunk;
            stream_.avail_out = sizeof(chunk);
            if (deflate(&stream_, Z_SYNC_FLUSH) != Z_OK)
                return out;
            out.append(reinterpret_cast<const char *>(chunk), sizeof(chunk) - stream_.avail_out);
        } while (stream_.avail_in > 0 || stream_.avail_out == 0);
        return out;
    }

    // Z_FINISH: what a peer that decides the compressed stream is over sends.
    // The bytes carry no flush marker, so the reader never inflates them.
    std::string finish()
    {
        stream_.next_in = Z_NULL;
        stream_.avail_in = 0;

        std::string out;
        std::uint8_t chunk[1024];
        int result = Z_OK;
        do
        {
            stream_.next_out = chunk;
            stream_.avail_out = sizeof(chunk);
            result = deflate(&stream_, Z_FINISH);
            if (result != Z_OK && result != Z_STREAM_END)
                return out;
            out.append(reinterpret_cast<const char *>(chunk), sizeof(chunk) - stream_.avail_out);
        } while (result != Z_STREAM_END);
        return out;
    }

  private:
    z_stream stream_{};
    bool ready_ = false;
};
} // namespace accord::test
