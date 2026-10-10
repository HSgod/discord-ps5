/*
 * Accord - HTTP server of the background daemon payload.
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

namespace accord::daemon
{
using RequestHandler = std::function<Response(const RequestLine &)>;

// SO_RCVTIMEO bounds one recv, and the daemon serves its clients from the same
// thread that feeds audio, so a client that sends a byte just before every
// timeout would hold that thread for as long as it liked. This is the budget for
// one whole request: the read loop asks how much of it is left, waits no longer
// than that, and stops when there is none. The arithmetic is separate from the
// socket, which is what lets the host tests cover it.
class RequestBudget
{
  public:
    // Two seconds for the whole request, and no single wait longer than half a
    // second, so a client that stops talking is noticed early.
    static constexpr std::int64_t kBudgetUs = 2'000'000;
    static constexpr int kMaxStepMs = 500;

    explicit RequestBudget(std::int64_t started_us) noexcept : started_us_{started_us}
    {
    }

    // Never negative: once the budget is gone it stays gone.
    std::int64_t remaining_us(std::int64_t now_us) const noexcept;
    bool expired(std::int64_t now_us) const noexcept;
    // How long the next recv may wait: the remaining budget rounded up to a
    // millisecond and capped at kMaxStepMs, or 0 when the budget is spent.
    int next_timeout_ms(std::int64_t now_us) const noexcept;

  private:
    std::int64_t started_us_ = 0;
};

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
} // namespace accord::daemon
