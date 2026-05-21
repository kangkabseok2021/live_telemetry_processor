#include "TransitionLog.h"

namespace imaging {

TransitionLog g_log;

void TransitionLog::push(const TransitionEntry& e) noexcept {
    uint32_t slot = head_.fetch_add(1, std::memory_order_relaxed) % LOG_CAPACITY;
    buf_[slot] = e;
    uint32_t cur = count_.load(std::memory_order_relaxed);
    if (cur < LOG_CAPACITY) {
        count_.fetch_add(1, std::memory_order_relaxed);
    }
}

TransitionEntry TransitionLog::get(uint32_t idx) const noexcept {
    return buf_[idx % LOG_CAPACITY];
}

uint32_t TransitionLog::count() const noexcept {
    return count_.load(std::memory_order_relaxed);
}

} // namespace imaging
