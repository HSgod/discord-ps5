/*
 * Discord PS5 - Tests for the daemon's test tone.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tests/micro_test.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "daemon/src/tone.hpp"

namespace
{
using namespace discord_ps5::daemon;

// Both channels of one second, as the payload would have submitted them.
constexpr std::size_t kSamplesPerSecond = kSampleRate * 2;

std::vector<std::int16_t> render_seconds(unsigned seconds)
{
    const std::size_t frames = static_cast<std::size_t>(seconds) * kSampleRate;
    std::vector<std::int16_t> samples(frames * 2, 0);

    Tone tone;
    tone.render(samples, frames);
    return samples;
}
} // namespace

MICRO_TEST(tone_plays_one_second_then_rests_one_second)
{
    const std::vector<std::int16_t> samples = render_seconds(2);

    bool first_second_has_signal = false;
    for (std::size_t i = 0; i < kSamplesPerSecond; ++i)
    {
        if (samples[i] != 0)
            first_second_has_signal = true;
    }
    MICRO_CHECK(first_second_has_signal);

    bool second_second_is_silent = true;
    for (std::size_t i = kSamplesPerSecond; i < samples.size(); ++i)
    {
        if (samples[i] != 0)
            second_second_is_silent = false;
    }
    MICRO_CHECK(second_second_is_silent);
}

MICRO_TEST(tone_is_stereo_and_within_its_amplitude)
{
    const std::vector<std::int16_t> samples = render_seconds(1);

    for (std::size_t i = 0; i + 1 < samples.size(); i += 2)
    {
        MICRO_CHECK(std::abs(samples[i]) <= kToneAmplitude);
        MICRO_CHECK_EQ(samples[i], samples[i + 1]);
    }
}

MICRO_TEST(tone_restarts_at_the_same_phase_in_every_burst)
{
    // The cycle is two seconds long, so the second burst starts one second
    // after the first one ends: at sample 2 * kSamplesPerSecond.
    const std::vector<std::int16_t> samples = render_seconds(3);

    for (std::size_t i = 0; i < 1000; ++i)
        MICRO_CHECK_EQ(samples[i], samples[2 * kSamplesPerSecond + i]);
}

MICRO_TEST(tone_reports_which_half_of_the_cycle_it_is_in)
{
    MICRO_CHECK(tone_playing_at(0));
    MICRO_CHECK(tone_playing_at(kSampleRate - 1));
    MICRO_CHECK(!tone_playing_at(kSampleRate));
    MICRO_CHECK(!tone_playing_at(2 * kSampleRate - 1));
    MICRO_CHECK(tone_playing_at(2 * kSampleRate));
}

MICRO_TEST(tone_counts_the_frames_it_renders)
{
    Tone tone;
    std::array<std::int16_t, 512> block{};

    tone.render(block, 256);
    MICRO_CHECK_EQ(tone.frames_written(), 256ull);
    MICRO_CHECK(tone.playing());

    tone.render(block, 256);
    MICRO_CHECK_EQ(tone.frames_written(), 512ull);
}
