/*
 * Discord PS5 - HTTP server of the background daemon payload.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * One client at a time and never blocking: the daemon's main loop calls poll()
 * between audio blocks, so a half-open client cannot stall it.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "daemon/src/http.hpp"

namespace discord_ps5::daemon
{
using RequestHandler = std::function<Response(const RequestLine &)>;

class Server
{
  public:
    Server() = default;
    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;
    ~Server();

    bool listen(std::uint16_t port) noexcept;

    // Serves at most one pending connection, then returns.
    void poll(const RequestHandler &handler) noexcept;

    void close() noexcept;

    bool is_listening() const noexcept;
    std::uint16_t port() const noexcept;
    std::string_view last_error() const noexcept;

  private:
    void remember(const char *call, int code) noexcept;

    char error_[96] = "";
    int fd_ = -1;
    std::uint16_t port_ = 0;
};
} // namespace discord_ps5::daemon
