/*
 * Discord PS5 - SceAudioOut port for the test tone.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "daemon/src/audio_ps5.hpp"

#include <cstdio>

namespace discord_ps5::daemon
{
namespace
{
// Values from the SceAudioOut enumeration the documentation describes for the
// chat port. The console run is what confirms them on this firmware.
constexpr unsigned kSystemUser = 0x10000000u;
constexpr int kPortIndex = 0;
constexpr int kTypeVoice = 2;
constexpr int kTypeMain = 0;
constexpr unsigned kRate = 48000;
constexpr unsigned kFormatS16Stereo = 1;
constexpr int kVolumeFlagAll = 0;
constexpr int kVolumeFull = 0x8000;
} // namespace

std::string_view name_of(AudioPort port) noexcept
{
    return port == AudioPort::voice ? "voice" : "main";
}

AudioPort audio_port_from_name(std::string_view name, bool &known) noexcept
{
    known = true;
    if (name == "voice")
        return AudioPort::voice;
    if (name == "main")
        return AudioPort::main;
    known = false;
    return AudioPort::voice;
}

AudioOut::~AudioOut()
{
    close();
}

bool AudioOut::open(AudioPort port) noexcept
{
    if (handle_ >= 0)
        return true;

    int code = sceAudioOutInit();
    if (code != 0)
    {
        remember("sceAudioOutInit", code);
        return false;
    }

    const int type = port == AudioPort::voice ? kTypeVoice : kTypeMain;
    handle_ = sceAudioOutOpen(kSystemUser, type, kPortIndex, kGrainFrames, kRate,
                              kFormatS16Stereo);
    if (handle_ < 0)
    {
        remember("sceAudioOutOpen", handle_);
        handle_ = -1;
        return false;
    }

    const int volumes[2] = {kVolumeFull, kVolumeFull};
    code = sceAudioOutSetVolume(handle_, kVolumeFlagAll, volumes);
    if (code != 0)
    {
        // Not fatal: the port stays open at whatever volume it came up with.
        remember("sceAudioOutSetVolume", code);
    }
    return true;
}

void AudioOut::submit(std::span<const std::int16_t> frames) noexcept
{
    if (handle_ < 0)
        return;

    const int code = sceAudioOutOutput(handle_, frames.data());
    if (code != 0)
        remember("sceAudioOutOutput", code);
}

void AudioOut::drain() noexcept
{
    if (handle_ < 0)
        return;
    sceAudioOutOutput(handle_, nullptr);
}

void AudioOut::close() noexcept
{
    if (handle_ < 0)
        return;

    drain();
    sceAudioOutClose(handle_);
    handle_ = -1;
}

bool AudioOut::is_open() const noexcept
{
    return handle_ >= 0;
}

std::string_view AudioOut::last_error() const noexcept
{
    return error_;
}

void AudioOut::remember(const char *call, int code) noexcept
{
    std::snprintf(error_, sizeof(error_), "%s = 0x%08x", call, static_cast<unsigned>(code));
}
} // namespace discord_ps5::daemon
