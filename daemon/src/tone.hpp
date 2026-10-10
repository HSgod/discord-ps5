/*
 * Accord - Test tone for the background daemon payload.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * One second of a 440 Hz sine, then one second of silence, on repeat. Pure
 * arithmetic, so the host tests exercise exactly what the console plays.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace accord::daemon
{
// Interleaved signed 16-bit stereo: one frame is two samples.
inline constexpr std::uint32_t kSampleRate = 48000;
inline constexpr double kToneFrequencyHz = 440.0;
inline constexpr std::int16_t kToneAmplitude = 8000; // about -12 dBFS
inline constexpr std::uint64_t kToneOnFrames = kSampleRate;
inline constexpr std::uint64_t kToneOffFrames = kSampleRate;

// Whether the tone sounds at `frames` of the two-second cycle. Free function,
// so a status read never has to touch the generator the audio path owns.
bool tone_playing_at(std::uint64_t frames) noexcept;

class Tone
{
  public:
    // Writes `frames` interleaved stereo frames into `out`.
    void render(std::span<std::int16_t> out, std::size_t frames) noexcept;

    std::uint64_t frames_written() const noexcept;
    bool playing() const noexcept;

  private:
    std::uint64_t frames_written_ = 0;
    double phase_ = 0.0;
};
} // namespace accord::daemon
