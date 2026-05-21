#include "StateMachine.h"
#include "TransitionLog.h"
#include <chrono>
#include <type_traits>

namespace imaging {

namespace {
uint8_t stateIndex(const State& s) {
    return static_cast<uint8_t>(s.index());
}
} // anonymous namespace

StateMachine::StateMachine() : state_(Idle{}) {}

const State& StateMachine::currentState() const noexcept {
    return state_;
}

uint64_t StateMachine::deadlineViolations() const noexcept {
    return deadline_violations_.load(std::memory_order_relaxed);
}

std::string StateMachine::stateName() const noexcept {
    return std::visit([](auto&& s) -> std::string {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, Idle>)            return "Idle";
        else if constexpr (std::is_same_v<T, Calibrating>) return "Calibrating";
        else if constexpr (std::is_same_v<T, Acquiring>)   return "Acquiring";
        else if constexpr (std::is_same_v<T, Processing>)  return "Processing";
        else if constexpr (std::is_same_v<T, Fault>)       return "Fault";
        else return "Unknown";
    }, state_);
}

std::expected<State, TransitionError> StateMachine::transition(Event e) {
    using Clock = std::chrono::high_resolution_clock;
    auto t_start = Clock::now();

    uint8_t from_idx = stateIndex(state_);

    auto result = std::visit([&](auto&& ev) -> std::expected<State, TransitionError> {
        using Ev = std::decay_t<decltype(ev)>;

        // EvtError: any non-Fault state → Fault
        if constexpr (std::is_same_v<Ev, EvtError>) {
            if (std::holds_alternative<Fault>(state_)) {
                return std::unexpected(TransitionError::INVALID_TRANSITION);
            }
            return Fault{ ev.reason, std::chrono::steady_clock::now() };
        }

        // CmdReset: Fault → Idle
        if constexpr (std::is_same_v<Ev, CmdReset>) {
            if (std::holds_alternative<Fault>(state_)) {
                return Idle{};
            }
            return std::unexpected(TransitionError::INVALID_TRANSITION);
        }

        // CmdStart: Idle → Calibrating
        if constexpr (std::is_same_v<Ev, CmdStart>) {
            if (std::holds_alternative<Idle>(state_)) {
                return Calibrating{ static_cast<int>(CALIB_STEPS) };
            }
            return std::unexpected(TransitionError::INVALID_TRANSITION);
        }

        // EvtCalibDone: Calibrating → Acquiring
        if constexpr (std::is_same_v<Ev, EvtCalibDone>) {
            if (std::holds_alternative<Calibrating>(state_)) {
                return Acquiring{ 0u };
            }
            return std::unexpected(TransitionError::INVALID_TRANSITION);
        }

        // EvtBatchReady: Acquiring → Processing
        if constexpr (std::is_same_v<Ev, EvtBatchReady>) {
            if (std::holds_alternative<Acquiring>(state_)) {
                return Processing{ ev.count };
            }
            return std::unexpected(TransitionError::INVALID_TRANSITION);
        }

        // EvtProcessDone: Processing → Idle
        if constexpr (std::is_same_v<Ev, EvtProcessDone>) {
            if (std::holds_alternative<Processing>(state_)) {
                return Idle{};
            }
            return std::unexpected(TransitionError::INVALID_TRANSITION);
        }

        // CmdStop, EvtFrameReady: not currently handled → INVALID
        return std::unexpected(TransitionError::INVALID_TRANSITION);

    }, e);

    auto t_end = Clock::now();
    auto exec_us = std::chrono::duration_cast<std::chrono::microseconds>(
                       t_end - t_start).count();

    if (result.has_value()) {
        state_ = result.value();
        if (static_cast<uint64_t>(exec_us) > DEADLINE_US) {
            deadline_violations_.fetch_add(1, std::memory_order_relaxed);
        }
        TransitionEntry entry{
            from_idx,
            stateIndex(state_),
            static_cast<uint32_t>(exec_us),
            0u,
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                t_start.time_since_epoch()).count()
        };
        g_log.push(entry);
    }

    return result;
}

} // namespace imaging
