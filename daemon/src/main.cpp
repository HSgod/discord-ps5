/*
 * Accord - Entry point of the background daemon payload.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The daemon plays a test tone, answers GET /status and POST /quit on a local
 * port, and appends every line to a log file. It never talks to Discord, never
 * opens TLS and never contacts the title: the one question it answers is
 * whether a payload keeps running, and keeps its audio, while the console is
 * busy with something else. At start it runs one unwind self-test and then builds one
 * MLS key package through the DAVE facade, which proves that libdave, mlspp and
 * OpenSSL are linked into the payload and that their exceptions stop at the
 * facade. Both answers go to the log, and the self-test also to /status.
 */

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>

#include "daemon/src/audio_ps5.hpp"
#include "daemon/src/config.hpp"
#include "daemon/src/http.hpp"
#include "daemon/src/log.hpp"
#include "daemon/src/server.hpp"
#include "daemon/src/tone.hpp"
#include "platform/ps5/dave/dave_facade.hpp"

namespace accord::daemon
{
namespace
{
using accord::core::LogLevel;

struct Options
{
    std::uint16_t port = kDefaultPort;
    std::string audio_name = "voice";
    std::string log_path = kLogPath;
};

std::uint16_t read_port(const char *text, std::uint16_t fallback) noexcept
{
    char *end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value == 0 || value > 65535ul)
        return fallback;
    return static_cast<std::uint16_t>(value);
}

// Accepts --port=N / --port N, --audio=voice|main and --log=PATH. Anything else
// is ignored, because a payload manager may pass arguments of its own.
Options read_options(int argc, char **argv) noexcept
{
    Options options;

    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument{argv[index]};
        const bool has_more = index + 1 < argc;

        if (argument == "--port" && has_more)
            options.port = read_port(argv[++index], options.port);
        else if (argument.starts_with("--port="))
            options.port = read_port(std::string{argument.substr(7)}.c_str(), options.port);
        else if (argument == "--audio" && has_more)
            options.audio_name = argv[++index];
        else if (argument.starts_with("--audio="))
            options.audio_name = std::string{argument.substr(8)};
        else if (argument == "--log" && has_more)
            options.log_path = argv[++index];
        else if (argument.starts_with("--log="))
            options.log_path = std::string{argument.substr(6)};
    }
    return options;
}

// The generator and the port belong to this thread alone. The HTTP side only
// reads the frame counter, which is why the counter is atomic and the tone is
// not shared.
void play_tone(AudioOut &audio, std::atomic<bool> &running,
               std::atomic<std::uint64_t> &frames, std::atomic<bool> &idle)
{
    Tone tone;
    std::array<std::int16_t, kGrainFrames * 2> block{};

    while (running.load(std::memory_order_relaxed))
    {
        tone.render(block, kGrainFrames);
        audio.submit(block);
        frames.store(tone.frames_written(), std::memory_order_relaxed);
    }

    idle.store(true, std::memory_order_relaxed);
}
} // namespace
} // namespace accord::daemon

int main(int argc, char **argv)
{
    using namespace accord::daemon;
    using accord::core::LogLevel;

    const Options options = read_options(argc, argv);

    bool known = false;
    const AudioPort audio_port = audio_port_from_name(options.audio_name, known);
    if (!known)
        std::fprintf(stderr, "daemon: unknown audio port '%s', playing on 'voice'\n",
                     options.audio_name.c_str());

    if (!open_log(options.log_path))
        std::fprintf(stderr, "daemon: cannot open the log at %s\n", options.log_path.c_str());

    logf(LogLevel::info, "daemon up: pid %d, http port %u, audio port '%s'",
         static_cast<int>(::getpid()), static_cast<unsigned>(options.port),
         name_of(audio_port).data());

    // T6.0-3: first check that an exception raised inside the libraries comes back
    // to us at all, because the facade's catch (...) is worth nothing without it.
    // The answer goes to the log and into /status, so the console start of T2.3
    // settles the one question the host build cannot: libc++/libunwind there.
    const accord::ps5::dave::UnwindSelfTest unwind =
        accord::ps5::dave::run_unwind_self_test();
    logf(unwind.ok() ? LogLevel::info : LogLevel::error,
         "unwind selftest: %s (runtime_error=%s, mlspp=%s, tls_parse=%s)",
         accord::ps5::dave::to_string(unwind), unwind.runtime_error_caught ? "caught" : "missed",
         unwind.mlspp_error_caught ? "caught" : "missed",
         unwind.tls_parse_error_caught ? "caught" : "missed");

    // T6.0-2: the payload is linked with DAVE, so the daemon asks the library for
    // a key package before it touches audio or the network. The facade hands back
    // a status code rather than an exception, so a broken library cannot take the
    // daemon down here.
    const accord::ps5::dave::KeyPackageProbe dave =
        accord::ps5::dave::probe_key_package(kDaveProbeGroupId, kDaveProbeUserId);
    if (dave.status == accord::ps5::dave::Status::ok)
        logf(LogLevel::info, "dave: key package %llu bytes, protocol version %u",
             static_cast<unsigned long long>(dave.bytes),
             static_cast<unsigned>(dave.protocol_version));
    else
        logf(LogLevel::error, "dave: no key package (status %s, protocol version %u)",
             accord::ps5::dave::to_string(dave.status),
             static_cast<unsigned>(dave.protocol_version));

    // The library's own failure callback also reports what it recovers from, so
    // this line shows up even when the key package above came out fine.
    if (dave.library_reported_failure)
        logf(LogLevel::warn, "dave: library reported '%s' while probing", dave.reported_reason);

    AudioOut audio;
    if (audio.open(audio_port))
        logf(LogLevel::info, "audio port '%s' is open",
             std::string{name_of(audio_port)}.c_str());
    else
        logf(LogLevel::error, "audio port failed: %s", audio.last_error().c_str());

    Server server;
    if (server.listen(options.port))
        logf(LogLevel::info, "listening on 0.0.0.0:%u", static_cast<unsigned>(server.port()));
    else
        logf(LogLevel::error, "listen failed: %s", std::string{server.last_error()}.c_str());

    std::atomic<std::uint64_t> frames{0};
    std::atomic<bool> running{true};
    std::atomic<bool> idle{false};
    std::thread audio_thread;
    if (audio.is_open())
        audio_thread = std::thread{play_tone, std::ref(audio), std::ref(running), std::ref(frames),
                                   std::ref(idle)};

    const std::time_t started = std::time(nullptr);
    std::uint64_t requests = 0;
    bool quitting = false;

    const RequestHandler handler = [&](const RequestLine &request) -> Response {
        ++requests;

        if (request.method == Method::get && request.path == "/status")
        {
            const std::uint64_t written = frames.load(std::memory_order_relaxed);
            // Composed here rather than stored: the audio thread owns the
            // record, this thread only reads it. The view below points into
            // this string, which outlives the render call.
            const std::string audio_error = audio.last_error();

            Status status;
            status.uptime_seconds = static_cast<std::uint64_t>(std::time(nullptr) - started);
            status.audio_frames = written;
            status.audio_open = audio.is_open();
            status.tone_playing = tone_playing_at(written);
            status.port = options.port;
            status.audio_port = name_of(audio_port);
            status.last_error = audio_error.empty() ? server.last_error() : audio_error;
            status.unwind_selftest = accord::ps5::dave::to_string(unwind); // a literal, so it outlives this

            Response response;
            response.content_type = "application/json";
            response.body = render_status_json(status);
            return response;
        }

        if (request.method == Method::post && request.path == "/quit")
        {
            quitting = true;

            Response response;
            response.content_type = "application/json";
            response.body = "{\"quitting\":true}\n";
            return response;
        }

        Response response;
        response.code = 404;
        response.reason = "Not Found";
        response.body = "not found: try GET /status or POST /quit\n";
        return response;
    };

    while (!quitting)
    {
        server.poll(handler);
        ::usleep(2000);
    }

    logf(LogLevel::info, "stopping after %llu request(s)",
         static_cast<unsigned long long>(requests));

    running.store(false, std::memory_order_relaxed);

    if (audio_thread.joinable())
    {
        // A port that a game has taken over can block the audio thread inside
        // the port forever. The exit must not depend on it, so wait a second
        // and leave it behind if it is stuck.
        for (int waited = 0; waited < 100 && !idle.load(std::memory_order_relaxed); ++waited)
            ::usleep(10000);

        if (idle.load(std::memory_order_relaxed))
        {
            audio_thread.join();
            audio.close();
        }
        else
        {
            logf(LogLevel::warn, "the audio thread is stuck in the port, exiting anyway");
            std::_Exit(0);
        }
    }

    server.close();
    logf(LogLevel::info, "daemon stopped cleanly");
    close_log();
    return 0;
}
