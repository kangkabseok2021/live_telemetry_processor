#pragma once
#include <array>
#include <atomic>
#include <optional>

// Lock-free single-producer / single-consumer ring buffer.
// N must be a power of two.  acquire/release ordering on head/tail.
template<typename T, std::size_t N>
class SpscQueue {
    static_assert((N & (N - 1)) == 0, "N must be a power of two");
public:
    bool push(const T& item) noexcept {
        const std::size_t h    = head_.load(std::memory_order_relaxed);
        const std::size_t next = (h + 1) & mask_;
        if (next == tail_.load(std::memory_order_acquire)) return false;
        buf_[h] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    std::optional<T> pop() noexcept {
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) return std::nullopt;
        T item = buf_[t];
        tail_.store((t + 1) & mask_, std::memory_order_release);
        return item;
    }

    bool empty() const noexcept {
        return tail_.load(std::memory_order_acquire) ==
               head_.load(std::memory_order_acquire);
    }
    bool full() const noexcept {
        const std::size_t h = head_.load(std::memory_order_acquire);
        return ((h + 1) & mask_) == tail_.load(std::memory_order_acquire);
    }

private:
    static constexpr std::size_t mask_ = N - 1;
    std::array<T, N>             buf_{};
    std::atomic<std::size_t>     head_{0};
    std::atomic<std::size_t>     tail_{0};
};
