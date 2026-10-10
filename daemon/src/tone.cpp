/*
 * Discord PS5 - Test tone for the background daemon payload.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "daemon/src/tone.hpp"

#include <cmath>

namespace discord_ps5::daemon
{
namespace
{
constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr std::uint64_t kCycleFrames = kToneOnFrames + kToneOffFrames;
} // namespace

bool tone_playing_at(std::uint64_t frames) noexcept
{
    return frames % kCycleFrames < kToneOnFrames;
}

void Tone::render(std::span<std::int16_t> out, std::size_t frames) noexcept
{
    const double step = kTwoPi * kToneFrequencyHz / static_cast<double>(kSampleRate);

    for (std::size_t frame = 0; frame < frames; ++frame)
    {
        const std::uint64_t position = frames_written_ % kCycleFrames;

        // Restart the phase with every burst, so the speaker does not click on
        // the seam between the tone and the silence.
        if (position == 0)
            phase_ = 0.0;

        std::int16_t value = 0;
        if (position < kToneOnFrames)
        {
            value = static_cast<std::int16_t>(std::lround(std::sin(phase_) * kToneAmplitude));
            phase_ += step;
            if (phase_ >= kTwoPi)
                phase_ -= kTwoPi;
        }

        out[frame * 2] = value;
        out[frame * 2 + 1] = value;
        ++frames_written_;
    }
}

std::uint64_t Tone::frames_written() const noexcept
{
    return frames_written_;
}

bool Tone::playing() const noexcept
{
    return tone_playing_at(frames_written_);
}
} // namespace discord_ps5::daemon
