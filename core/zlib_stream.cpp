/*
 * Accord - the inflate side of Discord's gateway stream compression.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/zlib_stream.hpp"

#include <cstdint>
#include <utility>

#include "third_party/zlib/zlib.h"

namespace accord::core
{
namespace
{
// The four bytes a Z_SYNC_FLUSH leaves behind, and the only boundary signal the
// stream carries. The buffer is read as "it ends with one of these", which is
// how the reference clients read Discord's stream: the same four bytes can turn
// up inside a block by coincidence, so nothing but the end of the buffer says
// where the sender stopped.
constexpr std::string_view kFlushMarker{"\x00\x00\xff\xff", 4};

// How much output one inflate() call may ask for. The message limit is 16 MiB,
// so this is only about how often the loop comes up for air.
constexpr std::size_t kOutChunk = 64 * 1024;

bool ends_with_marker(std::string_view bytes) noexcept
{
    return bytes.size() >= kFlushMarker.size() &&
           bytes.compare(bytes.size() - kFlushMarker.size(), kFlushMarker.size(), kFlushMarker) == 0;
}
} // namespace

struct ZlibStream::Impl
{
    z_stream inflater{};
    std::string pending;    // compressed bytes with no flush marker yet
    std::string ready;      // a message inflated and not yet taken
    std::string error;
    ZlibStatus status = ZlibStatus::ok;
    bool has_ready = false; // ready may be an empty message, so it has its own flag
    bool started = false;   // inflateInit has run

    bool start();
    void give_up(ZlibStatus why, std::string text);
    ZlibStatus inflate_message();
};

bool ZlibStream::Impl::start()
{
    inflater.zalloc = Z_NULL;
    inflater.zfree = Z_NULL;
    inflater.opaque = Z_NULL;
    // The default window bits: a zlib stream with its RFC 1950 header, which is
    // what `compress=zlib-stream` carries (the gateway documentation sends the
    // reader of it straight to zlib.decompress(), discord.py's inflater).
    const int result = inflateInit(&inflater);
    if (result != Z_OK)
    {
        give_up(ZlibStatus::no_context, "zlib could not create the inflate context");
        return false;
    }
    started = true;
    return true;
}

void ZlibStream::Impl::give_up(ZlibStatus why, std::string text)
{
    status = why;
    error = std::move(text);
}

ZlibStatus ZlibStream::Impl::inflate_message()
{
    // Everything in pending ends with a flush marker, so it is one message: one
    // flush per event is the framing Discord promises, and it is also the only
    // framing this can read -- zlib has no way back to a flush point it has
    // already passed, so two messages that reached one buffer are inflated as
    // one and the JSON reader above is what rejects the result.
    inflater.next_in = reinterpret_cast<Bytef *>(pending.data());
    inflater.avail_in = static_cast<uInt>(pending.size());

    std::string message;
    std::uint8_t chunk[kOutChunk];
    while (true)
    {
        inflater.next_out = chunk;
        inflater.avail_out = sizeof(chunk);
        const int result = inflate(&inflater, Z_SYNC_FLUSH);
        const std::size_t produced = sizeof(chunk) - inflater.avail_out;

        if (produced > 0)
        {
            // Checked as it grows, so a message that claims a gigabyte is cut
            // off in the middle rather than buffered and then regretted.
            if (message.size() + produced > kMaxInflatedBytes)
            {
                pending.clear();
                give_up(ZlibStatus::too_large,
                        "a message inflates to more than the 16 MiB limit");
                return status;
            }
            message.append(reinterpret_cast<const char *>(chunk), produced);
        }

        if (result == Z_STREAM_END)
        {
            // The peer closed the zlib stream. Nothing follows a finished
            // stream, so the bytes that did (the message's own flush marker,
            // left over) mean the two sides disagree about where the connection
            // ends. Discord's transport stream is flushed, never finished: it
            // runs until the socket does.
            pending.clear();
            give_up(ZlibStatus::corrupt, "the peer finished the compressed stream");
            return status;
        }
        if (result != Z_OK && result != Z_BUF_ERROR)
        {
            pending.clear();
            give_up(ZlibStatus::corrupt,
                    inflater.msg != nullptr ? std::string{"zlib: "} + inflater.msg
                                            : std::string{"the compressed stream is not readable"});
            return status;
        }
        if (produced == 0 && inflater.avail_in == 0)
            break; // the block is consumed and it said all it had

        if (result == Z_BUF_ERROR && produced == 0)
        {
            // No progress is possible: the bytes are not the block their flush
            // marker promised. Corruption that still decodes as valid codes
            // cannot be caught here -- there is no per-message checksum in this
            // framing -- and dies one layer up, in the JSON reader.
            pending.clear();
            give_up(ZlibStatus::corrupt, "the flush marker did not end a block");
            return status;
        }
    }

    pending.clear();
    ready = std::move(message);
    has_ready = true;
    return ZlibStatus::ok;
}

ZlibStream::ZlibStream() : impl_(std::make_unique<Impl>()) {}

ZlibStream::~ZlibStream()
{
    if (impl_ != nullptr && impl_->started)
        inflateEnd(&impl_->inflater);
}

void ZlibStream::reset()
{
    // A new connection is a new zlib stream. A context carried over from the old
    // one would fail on the first message of the new one -- zlib reads the
    // second message's own header as the middle of a block ("incorrect header
    // check"), which is what a RESUME that kept the old context would hit.
    if (impl_ != nullptr && impl_->started)
        inflateEnd(&impl_->inflater);
    *impl_ = Impl{};
}

ZlibStatus ZlibStream::feed(std::string_view bytes)
{
    Impl &impl = *impl_;
    if (impl.status != ZlibStatus::ok)
        return impl.status; // the first answer is the one that counts
    if (!impl.started && !impl.start())
        return impl.status;

    if (bytes.empty())
        return ZlibStatus::ok; // an empty frame is legal and carries nothing

    impl.pending.append(bytes);
    if (!ends_with_marker(impl.pending))
    {
        // Still the middle of a message. Nothing is inflated yet -- a partial
        // block would decompress to a partial event -- and the room it may take
        // while it waits is bounded too, so a peer that never flushes cannot
        // fill memory with fragments of a message that never ends.
        if (impl.pending.size() > kMaxPendingBytes)
        {
            impl.pending.clear();
            impl.give_up(ZlibStatus::too_large,
                         "compressed bytes piled up past the limit with no flush marker");
        }
        return impl.status;
    }
    return impl.inflate_message();
}

bool ZlibStream::next(std::string *out)
{
    if (out == nullptr || !impl_->has_ready)
        return false;
    *out = std::move(impl_->ready);
    impl_->ready.clear();
    impl_->has_ready = false;
    return true;
}

ZlibStatus ZlibStream::status() const noexcept
{
    return impl_->status;
}

bool ZlibStream::failed() const noexcept
{
    return impl_->status != ZlibStatus::ok;
}

const std::string &ZlibStream::error() const noexcept
{
    return impl_->error;
}
} // namespace accord::core
