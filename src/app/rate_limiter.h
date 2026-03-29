#ifndef APP_RATE_LIMITER_H
#define APP_RATE_LIMITER_H

#include <algorithm>
#include <chrono>
#include <mutex>

/// Simple sliding-window token-bucket rate limiter.
/// Thread-safe: guards internal state with a mutex.
class RateLimiter {
public:
    explicit RateLimiter(double max_requests_per_second = 10.0)
        : max_tokens_(max_requests_per_second),
          tokens_(max_requests_per_second),
          rate_(max_requests_per_second),
          last_refill_(std::chrono::steady_clock::now()) {}

    /// Returns true if the request is allowed (i.e., under the rate limit).
    bool allow() {
        std::lock_guard<std::mutex> lock(mutex_);
        refill();
        if (tokens_ >= 1.0) {
            tokens_ -= 1.0;
            return true;
        }
        return false;
    }

private:
    void refill() {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed =
            std::chrono::duration<double>(now - last_refill_).count();
        tokens_ = std::min(max_tokens_, tokens_ + elapsed * rate_);
        last_refill_ = now;
    }

    double max_tokens_;
    double tokens_;
    double rate_;
    std::chrono::steady_clock::time_point last_refill_;
    std::mutex mutex_;
};

#endif
