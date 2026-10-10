/*
 * Accord - Tests for the audio failure record.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The audio thread writes this record and the HTTP thread reads it, so what is
 * checked here is that a reader always sees a whole failure: a named call
 * together with the code that call returned.
 */

#include "tests/micro_test.hpp"

#include <atomic>
#include <string>
#include <thread>

#include "daemon/src/audio_error.hpp"

using namespace accord::daemon;

MICRO_TEST(nothing_has_failed_yet_means_no_text)
{
    const AudioError error;
    MICRO_CHECK(error.text().empty());
}

MICRO_TEST(the_record_names_the_call_and_the_code)
{
    AudioError error;

    error.remember(AudioCall::output, -1);
    MICRO_CHECK_EQ(error.text(), std::string{"sceAudioOutOutput = 0xffffffff"});

    // The last failure wins; there is no queue.
    error.remember(AudioCall::set_volume, static_cast<int>(0x80020001u));
    MICRO_CHECK_EQ(error.text(), std::string{"sceAudioOutSetVolume = 0x80020001"});

    // A call that failed with zero is still a failure worth naming, and the
    // record keeps it apart from "nothing failed".
    error.remember(AudioCall::init, 0);
    MICRO_CHECK_EQ(error.text(), std::string{"sceAudioOutInit = 0x00000000"});
}

MICRO_TEST(a_reader_never_catches_half_a_failure)
{
    // Two writers' worth of failures from one thread, read by another: every
    // line has to be one of the two in full, never a new call name with the
    // previous code.
    constexpr int kIterations = 20000;
    AudioError error;
    std::atomic<bool> writer_done{false};
    int reads = 0;
    bool whole = true;

    std::thread writer{[&] {
        for (int i = 0; i < kIterations; ++i)
        {
            error.remember(AudioCall::open, static_cast<int>(0x80020001u));
            error.remember(AudioCall::output, -1);
        }
        writer_done.store(true);
    }};

    while (!writer_done.load() || reads < kIterations)
    {
        const std::string text = error.text();
        whole = whole && (text.empty() || text == "sceAudioOutOpen = 0x80020001" ||
                          text == "sceAudioOutOutput = 0xffffffff");
        ++reads;
    }
    writer.join();

    MICRO_CHECK(reads >= kIterations);
    MICRO_CHECK(whole);
    MICRO_CHECK_EQ(error.text(), std::string{"sceAudioOutOutput = 0xffffffff"});
}
