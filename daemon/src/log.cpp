/*
 * Discord PS5 - Log file of the background daemon payload.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "daemon/src/log.hpp"

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>

namespace discord_ps5::daemon
{
namespace
{
using discord_ps5::core::LogLevel;

std::FILE *log_file = nullptr;

// Creates the directory that holds `path`, when it does not exist yet. Only one
// level deep: the log path lives directly under /data.
bool ensure_directory(std::string_view path) noexcept
{
    const std::size_t slash = path.find_last_of('/');
    if (slash == std::string_view::npos || slash == 0)
        return true;

    const std::string parent{path.substr(0, slash)};
    return ::mkdir(parent.c_str(), 0755) == 0 || errno == EEXIST;
}

std::string timestamp() noexcept
{
    const std::time_t now = std::time(nullptr);
    std::tm broken{};
    if (::localtime_r(&now, &broken) == nullptr)
        return "0000-00-00 00:00:00";

    char text[32] = "";
    std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &broken);
    return text;
}
} // namespace

bool open_log(std::string_view path) noexcept
{
    close_log();

    if (!ensure_directory(path))
        return false;

    log_file = std::fopen(std::string{path}.c_str(), "a");
    return log_file != nullptr;
}

void close_log() noexcept
{
    if (log_file != nullptr)
    {
        std::fclose(log_file);
        log_file = nullptr;
    }
}

void logf(LogLevel level, const char *format, ...) noexcept
{
    char message[512] = "";
    std::va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);

    const std::string when = timestamp();
    const std::string_view name = discord_ps5::core::to_string(level);

    std::FILE *console = level == LogLevel::info ? stdout : stderr;
    std::fprintf(console, "[%s] %.*s: %s\n", when.c_str(), static_cast<int>(name.size()),
                 name.data(), message);
    std::fflush(console);

    if (log_file != nullptr)
    {
        std::fprintf(log_file, "[%s] %.*s: %s\n", when.c_str(), static_cast<int>(name.size()),
                     name.data(), message);
        std::fflush(log_file);
    }
}
} // namespace discord_ps5::daemon
