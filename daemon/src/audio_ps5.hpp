/*
 * Accord - SceAudioOut port for the test tone.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Only this file knows about libSceAudioOut, so the host build and the tests
 * stay free of the SDK. The prototypes are written out here because the payload
 * SDK ships the SCE libraries as stubs without public headers.
 */

#pragma once

#include <cstdint>
#include <span>
#include <string_view>

// libSceAudioOut, as the SDK's stub library exports it.
extern "C" int sceAudioOutInit();
extern "C" int sceAudioOutOpen(unsigned user_id, int type, int index, unsigned grain,
                               unsigned frequency, unsigned format);
extern "C" int sceAudioOutSetVolume(int handle, int flags, const int *volumes);
extern "C" int sceAudioOutOutput(int handle, const void *samples);
extern "C" int sceAudioOutClose(int handle);

namespace accord::daemon
{
// The payload submits one complete block and blocks until the queue has room,
// so the block size is a property of this module.
inline constexpr unsigned kGrainFrames = 256;

// The two port types the console run compares: the voice-chat port, and the
// main port as the fallback.
enum class AudioPort
{
    voice,
    main,
};

std::string_view name_of(AudioPort port) noexcept;

// Maps "voice" or "main" to a port. `known` is false for anything else.
AudioPort audio_port_from_name(std::string_view name, bool &known) noexcept;

class AudioOut
{
  public:
    AudioOut() = default;
    AudioOut(const AudioOut &) = delete;
    AudioOut &operator=(const AudioOut &) = delete;
    ~AudioOut();

    bool open(AudioPort port) noexcept;
    void submit(std::span<const std::int16_t> frames) noexcept;
    void drain() noexcept;
    void close() noexcept;

    bool is_open() const noexcept;
    std::string_view last_error() const noexcept;

  private:
    void remember(const char *call, int code) noexcept;

    int handle_ = -1;
    char error_[96] = "";
};
} // namespace accord::daemon
