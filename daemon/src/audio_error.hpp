/*
 * Accord - The last SceAudioOut failure, shared between two threads.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The audio thread is the one that sees failures (sceAudioOutOutput is what
 * fails once the port is open) and the HTTP thread is the one that reports them
 * through GET /status, so the record has to be safe to read while it is written.
 *
 * The code and the call that produced it travel as one atomic and the text is
 * composed by whoever reads it. A shared char buffer would let a reader catch
 * half a line; two atomics would let it pair a new call name with the previous
 * code. One 64-bit word has neither problem.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

namespace accord::daemon
{
// The SceAudioOut calls that can fail, in the order the daemon runs them.
enum class AudioCall : std::uint8_t
{
    none = 0,
    init,
    open,
    set_volume,
    output,
};

std::string_view name_of(AudioCall call) noexcept;

class AudioError
{
  public:
    // Called from the audio thread. The last failure wins; there is no queue.
    void remember(AudioCall call, int code) noexcept;

    // Called from the HTTP thread: empty while nothing has failed. Composes the
    // text, so unlike the rest of this module it allocates.
    std::string text() const;

  private:
    // The call in the low 32 bits, the code in the high 32: one atomic, so the
    // two always belong together. Zero is "nothing has failed yet", which is
    // why `none` is 0 and the code is stored as its bit pattern.
    std::atomic<std::uint64_t> state_{0};
};
} // namespace accord::daemon
