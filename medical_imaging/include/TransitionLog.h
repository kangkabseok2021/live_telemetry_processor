#pragma once
#include "RtConfig.h"
#include <array>
#include <atomic>
#include <cstdint>

namespace imaging {

struct TransitionEntry {
    uint8_t  state_from{};
    uint8_t  state_to{};
    uint32_t t_exec_us{};
    uint32_t t_comm_us{};
    int64_t  timestamp_ns{};
};

class TransitionLog {
public:
    void push(const TransitionEntry& e) noexcept;
    TransitionEntry get(uint32_t idx) const noexcept;
    uint32_t count() const noexcept;

private:
    std::array<TransitionEntry, LOG_CAPACITY> buf_{};
    alignas(64) std::atomic<uint32_t> head_{0};
    alignas(64) std::atomic<uint32_t> count_{0};
};

// Module-level log shared between StateMachine.cpp and tests
extern TransitionLog g_log;

} // namespace imaging
