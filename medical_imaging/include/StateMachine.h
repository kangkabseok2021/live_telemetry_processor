#pragma once
#include "RtConfig.h"
#include <variant>
#include <expected>
#include <string>
#include <chrono>
#include <atomic>
#include <cstdint>

namespace imaging {

// ── States ────────────────────────────────────────────────────────────────
struct Idle {};
struct Calibrating { int steps_remaining; };
struct Acquiring   { uint32_t frame_count; };
struct Processing  { uint32_t batch_size; };
struct Fault       { std::string reason;
                     std::chrono::steady_clock::time_point fault_time; };

// ── Events ────────────────────────────────────────────────────────────────
struct CmdStart      {};
struct CmdStop       {};
struct CmdReset      {};
struct EvtFrameReady {};
struct EvtCalibDone  {};
struct EvtBatchReady { uint32_t count; };
struct EvtProcessDone{};
struct EvtError      { std::string reason; };

using State = std::variant<Idle, Calibrating, Acquiring, Processing, Fault>;
using Event = std::variant<CmdStart, CmdStop, CmdReset, EvtFrameReady,
                           EvtCalibDone, EvtBatchReady, EvtProcessDone, EvtError>;

enum class TransitionError { INVALID_TRANSITION, FAULT_NOT_RESET };

class StateMachine {
public:
    StateMachine();
    [[nodiscard]] std::expected<State, TransitionError> transition(Event e);
    [[nodiscard]] const State& currentState() const noexcept;
    [[nodiscard]] uint64_t deadlineViolations() const noexcept;
    std::string stateName() const noexcept;

private:
    State state_;
    std::atomic<uint64_t> deadline_violations_{0};
};

} // namespace imaging
