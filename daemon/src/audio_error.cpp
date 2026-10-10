/*
 * Accord - The last SceAudioOut failure, shared between two threads.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "daemon/src/audio_error.hpp"

#include <cstdio>

namespace accord::daemon
{
namespace
{
// Where the code sits in the packed word, above the call.
constexpr unsigned kCodeShift = 32;
constexpr std::uint64_t kCallMask = 0xffffffffull;
} // namespace

std::string_view name_of(AudioCall call) noexcept
{
    switch (call)
    {
    case AudioCall::none:
        return "none";
    case AudioCall::init:
        return "sceAudioOutInit";
    case AudioCall::open:
        return "sceAudioOutOpen";
    case AudioCall::set_volume:
        return "sceAudioOutSetVolume";
    case AudioCall::output:
        return "sceAudioOutOutput";
    }
    return "unknown";
}

void AudioError::remember(AudioCall call, int code) noexcept
{
    const std::uint64_t packed =
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(code)) << kCodeShift) |
        (static_cast<std::uint64_t>(call) & kCallMask);
    // Relaxed is enough: the value read is a report of the past, not a handle
    // the reader has to synchronise with.
    state_.store(packed, std::memory_order_relaxed);
}

std::string AudioError::text() const
{
    const std::uint64_t state = state_.load(std::memory_order_relaxed);
    if (state == 0)
        return {};

    const auto call = static_cast<AudioCall>(state & kCallMask);
    const int code = static_cast<int>(static_cast<std::uint32_t>(state >> kCodeShift));

    char printed[16];
    std::snprintf(printed, sizeof(printed), "0x%08x", static_cast<unsigned>(code));
    return std::string{name_of(call)} + " = " + printed;
}
} // namespace accord::daemon
