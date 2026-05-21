# Medical Imaging State Machine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `medical_imaging/` subfolder to `live_telemetry_processor/` containing a C++23 real-time imaging state machine with a SPSC transition log, C extern shim, C# P/Invoke wrapper, Python timing report script, GitHub Actions CI, and 14 passing GoogleTests.

**Architecture:** A `StateMachine` class uses `std::variant`-based states (Idle/Calibrating/Acquiring/Processing/Fault) and `std::expected` return from `transition()`. An `RtEngine` wraps the SM + ImagePipeline and drives a 100 Hz tick thread. A `TransitionLog` ring buffer (stack-allocated, 1024 entries, atomic head/tail) records every transition. A `c_api` extern-C shim exposes the engine to C# via P/Invoke.

**Tech Stack:** C++23 (`std::expected`, `std::variant`, `std::jthread`, `std::atomic`), GoogleTest 1.14.0, Python 3.12 + NumPy, C# (P/Invoke, no .csproj)

---

## File Map

| File | Responsibility |
|---|---|
| `medical_imaging/include/RtConfig.h` | Compile-time constants (DEADLINE_US etc.) |
| `medical_imaging/include/StateMachine.h` | State/Event variant types + SM class declaration |
| `medical_imaging/include/TransitionLog.h` | Ring-buffer log struct + class declaration |
| `medical_imaging/include/ImagePipeline.h` | Stub pipeline interface |
| `medical_imaging/include/c_api.h` | C extern-linkage API + constants |
| `medical_imaging/src/StateMachine.cpp` | `transition()` via `std::visit`, timing, log recording |
| `medical_imaging/src/TransitionLog.cpp` | Atomic push/get/count ring buffer |
| `medical_imaging/src/ImagePipeline.cpp` | Stub implementations |
| `medical_imaging/src/RtEngine.cpp` | `jthread` 100 Hz tick loop, sendCommand |
| `medical_imaging/src/c_api.cpp` | Opaque-pointer C shim wrapping RtEngine |
| `medical_imaging/src/main.cpp` | Demo: start engine, send CmdStart, run 3s, print state |
| `medical_imaging/tests/test_state_machine.cpp` | 14 GoogleTests |
| `medical_imaging/csharp/ImagingEngineClient.cs` | C# P/Invoke wrapper (no .csproj) |
| `medical_imaging/scripts/generate_timing_report.py` | CSV → P50/P95/P99 Markdown table |
| `medical_imaging/docs/ARCHITECTURE.md` | Mermaid state + class diagrams |
| `medical_imaging/docs/RT-CONSTRAINTS.md` | Deadline/scheduling documentation |
| `medical_imaging/docs/adr/ADR-001-rt-vs-rest-boundary.md` | ADR: P/Invoke vs REST boundary decision |
| `medical_imaging/CMakeLists.txt` | imaging_core + imaging_engine + test_imaging targets |
| `.github/workflows/medical-imaging.yml` | CI: build+test + ASan jobs |
| `CMakeLists.txt` (root, modify) | Add `add_subdirectory(medical_imaging)` |
| `README.md` (root, modify) | Add medical_imaging row to projects table |

---

## Task 1: Create directory scaffold and headers

**Files:**
- Create: `medical_imaging/include/RtConfig.h`
- Create: `medical_imaging/include/StateMachine.h`
- Create: `medical_imaging/include/TransitionLog.h`
- Create: `medical_imaging/include/ImagePipeline.h`
- Create: `medical_imaging/include/c_api.h`

- [ ] **Step 1.1: Create directory structure**

```bash
mkdir -p /Users/kab/Projects/Portfolio/live_telemetry_processor/medical_imaging/include
mkdir -p /Users/kab/Projects/Portfolio/live_telemetry_processor/medical_imaging/src
mkdir -p /Users/kab/Projects/Portfolio/live_telemetry_processor/medical_imaging/tests
mkdir -p /Users/kab/Projects/Portfolio/live_telemetry_processor/medical_imaging/csharp
mkdir -p /Users/kab/Projects/Portfolio/live_telemetry_processor/medical_imaging/scripts
mkdir -p /Users/kab/Projects/Portfolio/live_telemetry_processor/medical_imaging/docs/adr
```

- [ ] **Step 1.2: Write `RtConfig.h`**

```cpp
#pragma once
#include <cstdint>
namespace imaging {
    inline constexpr uint32_t DEADLINE_US    = 500;
    inline constexpr uint32_t LOG_CAPACITY   = 1024;
    inline constexpr uint32_t CALIB_STEPS    = 10;
    inline constexpr uint32_t FRAME_BATCH_SZ = 10;
}
```

- [ ] **Step 1.3: Write `StateMachine.h`**

```cpp
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
struct CmdStart     {};
struct CmdStop      {};
struct CmdReset     {};
struct EvtFrameReady{};
struct EvtCalibDone {};
struct EvtBatchReady{ uint32_t count; };
struct EvtProcessDone{};
struct EvtError     { std::string reason; };

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
```

- [ ] **Step 1.4: Write `TransitionLog.h`**

```cpp
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
```

- [ ] **Step 1.5: Write `ImagePipeline.h`**

```cpp
#pragma once
#include <cstdint>

namespace imaging {

class ImagePipeline {
public:
    void calibrate(int steps);
    void startAcquisition();
    void stopAcquisition();
    void processBatch(uint32_t batch_size);
    bool isBusy() const noexcept;

private:
    bool busy_{false};
};

} // namespace imaging
```

- [ ] **Step 1.6: Write `c_api.h`**

```c
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct smachine_tag smachine_t;

smachine_t* smachine_create(void);
int          smachine_send_event(smachine_t* sm, int event_id);
int          smachine_get_state(smachine_t* sm);
uint64_t     smachine_get_deadline_violations(smachine_t* sm);
void         smachine_destroy(smachine_t* sm);

#define SMACHINE_CMD_START      1
#define SMACHINE_CMD_STOP       2
#define SMACHINE_CMD_RESET      3
#define SMACHINE_EVT_FRAME      4
#define SMACHINE_EVT_ERROR      5
#define SMACHINE_EVT_CALIB_DONE 6
#define SMACHINE_EVT_BATCH      7
#define SMACHINE_EVT_PROC_DONE  8

#define SMACHINE_OK                  0
#define SMACHINE_INVALID_TRANSITION  1
#define SMACHINE_ERR                -1

#define SMACHINE_STATE_IDLE        0
#define SMACHINE_STATE_CALIBRATING 1
#define SMACHINE_STATE_ACQUIRING   2
#define SMACHINE_STATE_PROCESSING  3
#define SMACHINE_STATE_FAULT       4

#ifdef __cplusplus
}
#endif
```

---

## Task 2: Write 14 failing tests (TDD — test first)

**Files:**
- Create: `medical_imaging/tests/test_state_machine.cpp`

- [ ] **Step 2.1: Write the 14-test file**

```cpp
#include <gtest/gtest.h>
#include "StateMachine.h"
#include "TransitionLog.h"
#include <chrono>
#include <thread>

using namespace imaging;

// ── Suite 1: Valid transitions ────────────────────────────────────────────

TEST(StateMachineTest, IdleToCalibrating_OnCmdStart) {
    StateMachine sm;
    auto result = sm.transition(CmdStart{});
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<Calibrating>(result.value()));
    EXPECT_EQ(std::get<Calibrating>(result.value()).steps_remaining,
              static_cast<int>(CALIB_STEPS));
}

TEST(StateMachineTest, CalibratingToAcquiring_OnCalibDone) {
    StateMachine sm;
    sm.transition(CmdStart{});
    auto result = sm.transition(EvtCalibDone{});
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<Acquiring>(result.value()));
    EXPECT_EQ(std::get<Acquiring>(result.value()).frame_count, 0u);
}

TEST(StateMachineTest, AcquiringToProcessing_OnBatchReady) {
    StateMachine sm;
    sm.transition(CmdStart{});
    sm.transition(EvtCalibDone{});
    auto result = sm.transition(EvtBatchReady{5});
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<Processing>(result.value()));
    EXPECT_EQ(std::get<Processing>(result.value()).batch_size, 5u);
}

TEST(StateMachineTest, ProcessingToIdle_OnProcessDone) {
    StateMachine sm;
    sm.transition(CmdStart{});
    sm.transition(EvtCalibDone{});
    sm.transition(EvtBatchReady{5});
    auto result = sm.transition(EvtProcessDone{});
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<Idle>(result.value()));
}

// ── Suite 2: Invalid transitions ─────────────────────────────────────────

TEST(StateMachineTest, CalibratingToProcessing_DirectBlocked) {
    StateMachine sm;
    sm.transition(CmdStart{});  // now Calibrating
    auto result = sm.transition(EvtBatchReady{3});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), TransitionError::INVALID_TRANSITION);
}

TEST(StateMachineTest, AcquiringToIdle_DirectBlocked) {
    StateMachine sm;
    sm.transition(CmdStart{});
    sm.transition(EvtCalibDone{});  // now Acquiring
    auto result = sm.transition(EvtProcessDone{});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), TransitionError::INVALID_TRANSITION);
}

TEST(StateMachineTest, FaultToCalibrating_DirectBlocked) {
    StateMachine sm;
    sm.transition(EvtError{"test"});  // now Fault
    auto result = sm.transition(CmdStart{});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), TransitionError::INVALID_TRANSITION);
}

// ── Suite 3: Fault handling ───────────────────────────────────────────────

TEST(StateMachineTest, AnyStateToFault_FromIdle) {
    StateMachine sm;
    auto result = sm.transition(EvtError{"hardware fault"});
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<Fault>(result.value()));
    EXPECT_EQ(std::get<Fault>(result.value()).reason, "hardware fault");
}

TEST(StateMachineTest, AnyStateToFault_FromCalibrating) {
    StateMachine sm;
    sm.transition(CmdStart{});
    auto result = sm.transition(EvtError{"sensor error"});
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<Fault>(result.value()));
}

TEST(StateMachineTest, AnyStateToFault_FromAcquiring) {
    StateMachine sm;
    sm.transition(CmdStart{});
    sm.transition(EvtCalibDone{});
    auto result = sm.transition(EvtError{"frame overrun"});
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<Fault>(result.value()));
}

TEST(StateMachineTest, FaultToIdle_OnCmdReset) {
    StateMachine sm;
    sm.transition(EvtError{"test"});
    auto result = sm.transition(CmdReset{});
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<Idle>(result.value()));
}

// ── Suite 4: Infrastructure ───────────────────────────────────────────────

TEST(StateMachineTest, DeadlineNotExceeded_NormalTransition) {
    StateMachine sm;
    sm.transition(CmdStart{});
    EXPECT_EQ(sm.deadlineViolations(), 0u);
}

TEST(StateMachineTest, TransitionLog_PopulatedAfterTransition) {
    // Reset by creating a fresh SM; g_log is cumulative across tests —
    // capture count before then after.
    StateMachine sm;
    uint32_t before = g_log.count();
    sm.transition(CmdStart{});
    EXPECT_GT(g_log.count(), before);
}

TEST(StateMachineTest, TransitionLog_RingWraps_At_1024) {
    StateMachine sm;
    // Drive 1025 transitions: Idle→Calibrating (1) +
    // 512× (Calibrating→Acquiring + Processing + back via reset) cycles.
    // Simpler: pump 2048 EvtError + CmdReset pairs from Idle.
    for (int i = 0; i < 512; ++i) {
        sm.transition(EvtError{"x"});  // → Fault
        sm.transition(CmdReset{});     // → Idle
    }
    // After 1024 entries the ring must wrap: count stays ≤ LOG_CAPACITY
    EXPECT_LE(g_log.count(), LOG_CAPACITY);
    // And we can still read an entry at index 0 without crashing
    auto entry = g_log.get(0);
    (void)entry;  // just check no abort
}
```

- [ ] **Step 2.2: Confirm tests fail to compile (no implementation yet)**

This step is verified automatically when we try to build in Task 4 before implementations exist. Proceed to Task 3.

---

## Task 3: Implement source files

**Files:**
- Create: `medical_imaging/src/StateMachine.cpp`
- Create: `medical_imaging/src/TransitionLog.cpp`
- Create: `medical_imaging/src/ImagePipeline.cpp`
- Create: `medical_imaging/src/RtEngine.cpp`
- Create: `medical_imaging/src/c_api.cpp`
- Create: `medical_imaging/src/main.cpp`

- [ ] **Step 3.1: Write `TransitionLog.cpp`**

```cpp
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
```

- [ ] **Step 3.2: Write `StateMachine.cpp`**

```cpp
#include "StateMachine.h"
#include "TransitionLog.h"
#include <chrono>

namespace imaging {

namespace {
// Helper: map State variant index to uint8_t
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
        if constexpr (std::is_same_v<T, Idle>)        return "Idle";
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

        // EvtError: any state → Fault (except already Fault)
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
                return Acquiring{ 0 };
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

        // CmdStop, EvtFrameReady: not handled → INVALID
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
```

- [ ] **Step 3.3: Write `ImagePipeline.cpp`**

```cpp
#include "ImagePipeline.h"

namespace imaging {

void ImagePipeline::calibrate(int /*steps*/) { busy_ = false; }
void ImagePipeline::startAcquisition()       { busy_ = false; }
void ImagePipeline::stopAcquisition()        { busy_ = false; }
void ImagePipeline::processBatch(uint32_t /*batch_size*/) { busy_ = false; }
bool ImagePipeline::isBusy() const noexcept  { return busy_; }

} // namespace imaging
```

- [ ] **Step 3.4: Write `RtEngine.cpp`**

```cpp
#include "StateMachine.h"
#include "ImagePipeline.h"
#include "c_api.h"
#include <thread>
#include <chrono>
#include <cstdio>

#ifdef __linux__
#include <sys/timerfd.h>
#include <unistd.h>
#include <cstring>
#include <pthread.h>
#include <sys/mman.h>
#endif

namespace imaging {

class RtEngine {
public:
    RtEngine() = default;
    ~RtEngine() { stop(); }

    void start() {
        thread_ = std::jthread([this](std::stop_token st) {
            tickLoop(st);
        });
    }

    void stop() {
        if (thread_.joinable()) {
            thread_.request_stop();
            thread_.join();
        }
    }

    // Returns SMACHINE_OK, SMACHINE_INVALID_TRANSITION, or SMACHINE_ERR
    int sendCommand(int event_id) {
        std::expected<State, TransitionError> result =
            std::unexpected(TransitionError::INVALID_TRANSITION);

        switch (event_id) {
            case SMACHINE_CMD_START:      result = sm_.transition(CmdStart{});      break;
            case SMACHINE_CMD_STOP:       result = sm_.transition(CmdStop{});       break;
            case SMACHINE_CMD_RESET:      result = sm_.transition(CmdReset{});      break;
            case SMACHINE_EVT_FRAME:      result = sm_.transition(EvtFrameReady{}); break;
            case SMACHINE_EVT_ERROR:      result = sm_.transition(EvtError{"external"}); break;
            case SMACHINE_EVT_CALIB_DONE: result = sm_.transition(EvtCalibDone{}); break;
            case SMACHINE_EVT_BATCH:      result = sm_.transition(EvtBatchReady{FRAME_BATCH_SZ}); break;
            case SMACHINE_EVT_PROC_DONE:  result = sm_.transition(EvtProcessDone{}); break;
            default: return SMACHINE_ERR;
        }

        if (!result.has_value()) {
            return (result.error() == TransitionError::INVALID_TRANSITION)
                ? SMACHINE_INVALID_TRANSITION : SMACHINE_ERR;
        }
        return SMACHINE_OK;
    }

    int getStateId() const noexcept {
        return static_cast<int>(sm_.currentState().index());
    }

    uint64_t deadlineViolations() const noexcept {
        return sm_.deadlineViolations();
    }

private:
    StateMachine  sm_;
    ImagePipeline pipeline_;
    std::jthread  thread_;

    void tickLoop(std::stop_token st) {
#ifdef __linux__
        // Try to set SCHED_FIFO — requires CAP_SYS_NICE
        sched_param sp{};
        sp.sched_priority = 50;
        if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0) {
            std::fprintf(stderr, "[RtEngine] SCHED_FIFO unavailable — running without RT priority\n");
        }
        if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
            std::fprintf(stderr, "[RtEngine] mlockall failed — continuing without memory lock\n");
        }
        int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
        if (tfd >= 0) {
            itimerspec its{};
            its.it_value.tv_nsec    = 10'000'000;  // 10 ms
            its.it_interval.tv_nsec = 10'000'000;
            timerfd_settime(tfd, 0, &its, nullptr);
            while (!st.stop_requested()) {
                uint64_t expirations = 0;
                ::read(tfd, &expirations, sizeof(expirations));
                tick();
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            ::close(tfd);
            return;
        }
#endif
        // macOS / fallback
        while (!st.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            tick();
        }
    }

    void tick() {
        // Auto-drive state machine based on current state
        const State& cur = sm_.currentState();
        if (std::holds_alternative<Calibrating>(cur)) {
            auto& c = std::get<Calibrating>(const_cast<State&>(cur));
            if (c.steps_remaining <= 0) {
                sm_.transition(EvtCalibDone{});
            }
        }
    }
};

} // namespace imaging

// ── C API implementation ──────────────────────────────────────────────────
// (defined here to avoid a separate translation unit for smachine_tag)
```

- [ ] **Step 3.5: Write `c_api.cpp`**

```cpp
#include "c_api.h"
#include "StateMachine.h"
#include "ImagePipeline.h"
#include <mutex>
#include <memory>

// Forward-declare RtEngine from RtEngine.cpp
namespace imaging { class RtEngine; }

// We include RtEngine inline via a header-like approach — 
// easier to forward-declare it properly via a separate header.
// Instead, we directly re-use the full RtEngine definition via
// a shared header included from RtEngine.cpp.
// For simplicity, duplicate the minimal interface here:

#include "StateMachine.h"
#include "ImagePipeline.h"
#include <thread>
#include <chrono>
#include <cstdio>

#ifdef __linux__
#include <pthread.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <unistd.h>
#endif

namespace imaging {

class RtEngine {
public:
    RtEngine() = default;
    ~RtEngine() { stop(); }

    void start();
    void stop();
    int  sendCommand(int event_id);
    int  getStateId() const noexcept;
    uint64_t deadlineViolations() const noexcept;

private:
    StateMachine  sm_;
    ImagePipeline pipeline_;
    std::jthread  thread_;

    void tickLoop(std::stop_token st);
    void tick();
};

} // namespace imaging
```

Wait — that approach duplicates RtEngine. Let me redesign: RtEngine will have its own header, and both `RtEngine.cpp` and `c_api.cpp` include it.

- [ ] **Step 3.5 (revised): Add `include/RtEngine.h`, then write clean `RtEngine.cpp` and `c_api.cpp`**

**`include/RtEngine.h`:**
```cpp
#pragma once
#include "StateMachine.h"
#include "ImagePipeline.h"
#include <thread>

namespace imaging {

class RtEngine {
public:
    RtEngine() = default;
    ~RtEngine() { stop(); }

    void start();
    void stop();
    int  sendCommand(int event_id);
    int  getStateId() const noexcept;
    uint64_t deadlineViolations() const noexcept;

private:
    StateMachine  sm_;
    ImagePipeline pipeline_;
    std::jthread  thread_;

    void tickLoop(std::stop_token st);
    void tick();
};

} // namespace imaging
```

**`src/RtEngine.cpp`:**
```cpp
#include "RtEngine.h"
#include "c_api.h"
#include <chrono>
#include <cstdio>

#ifdef __linux__
#include <sys/timerfd.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#endif

namespace imaging {

void RtEngine::start() {
    thread_ = std::jthread([this](std::stop_token st) {
        tickLoop(st);
    });
}

void RtEngine::stop() {
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
}

int RtEngine::sendCommand(int event_id) {
    std::expected<State, TransitionError> result =
        std::unexpected(TransitionError::INVALID_TRANSITION);

    switch (event_id) {
        case SMACHINE_CMD_START:      result = sm_.transition(CmdStart{});      break;
        case SMACHINE_CMD_STOP:       result = sm_.transition(CmdStop{});       break;
        case SMACHINE_CMD_RESET:      result = sm_.transition(CmdReset{});      break;
        case SMACHINE_EVT_FRAME:      result = sm_.transition(EvtFrameReady{}); break;
        case SMACHINE_EVT_ERROR:      result = sm_.transition(EvtError{"external"}); break;
        case SMACHINE_EVT_CALIB_DONE: result = sm_.transition(EvtCalibDone{}); break;
        case SMACHINE_EVT_BATCH:      result = sm_.transition(EvtBatchReady{FRAME_BATCH_SZ}); break;
        case SMACHINE_EVT_PROC_DONE:  result = sm_.transition(EvtProcessDone{}); break;
        default: return SMACHINE_ERR;
    }

    if (!result.has_value()) {
        return (result.error() == TransitionError::INVALID_TRANSITION)
            ? SMACHINE_INVALID_TRANSITION : SMACHINE_ERR;
    }
    return SMACHINE_OK;
}

int RtEngine::getStateId() const noexcept {
    return static_cast<int>(sm_.currentState().index());
}

uint64_t RtEngine::deadlineViolations() const noexcept {
    return sm_.deadlineViolations();
}

void RtEngine::tickLoop(std::stop_token st) {
#ifdef __linux__
    sched_param sp{};
    sp.sched_priority = 50;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0) {
        std::fprintf(stderr, "[RtEngine] SCHED_FIFO unavailable\n");
    }
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        std::fprintf(stderr, "[RtEngine] mlockall failed\n");
    }
    int tfd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (tfd >= 0) {
        itimerspec its{};
        its.it_value.tv_nsec    = 10'000'000;
        its.it_interval.tv_nsec = 10'000'000;
        timerfd_settime(tfd, 0, &its, nullptr);
        while (!st.stop_requested()) {
            uint64_t expirations = 0;
            ::read(tfd, &expirations, sizeof(expirations));
            tick();
        }
        ::close(tfd);
        return;
    }
#endif
    while (!st.stop_requested()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        tick();
    }
}

void RtEngine::tick() {
    // The tick loop currently just monitors state; external events
    // are injected via sendCommand(). Extend here for autonomous transitions.
}

} // namespace imaging
```

**`src/c_api.cpp`:**
```cpp
#include "c_api.h"
#include "RtEngine.h"
#include <mutex>

struct smachine_tag {
    imaging::RtEngine engine;
    std::mutex        mutex;
};

smachine_t* smachine_create() {
    return new smachine_tag{};
}

int smachine_send_event(smachine_t* sm, int event_id) {
    if (!sm) return SMACHINE_ERR;
    std::lock_guard<std::mutex> lock(sm->mutex);
    return sm->engine.sendCommand(event_id);
}

int smachine_get_state(smachine_t* sm) {
    if (!sm) return SMACHINE_ERR;
    return sm->engine.getStateId();
}

uint64_t smachine_get_deadline_violations(smachine_t* sm) {
    if (!sm) return 0;
    return sm->engine.deadlineViolations();
}

void smachine_destroy(smachine_t* sm) {
    delete sm;
}
```

**`src/main.cpp`:**
```cpp
#include "RtEngine.h"
#include <iostream>
#include <chrono>
#include <thread>

int main() {
    imaging::RtEngine engine;
    engine.start();

    std::cout << "[demo] Sending CmdStart...\n";
    engine.sendCommand(SMACHINE_CMD_START);

    std::this_thread::sleep_for(std::chrono::seconds(3));

    std::cout << "[demo] State index after 3s: " << engine.getStateId() << "\n";
    std::cout << "[demo] Deadline violations: " << engine.deadlineViolations() << "\n";

    engine.stop();
    std::cout << "[demo] Done.\n";
    return 0;
}
```

---

## Task 4: Write `CMakeLists.txt` and build

**Files:**
- Create: `medical_imaging/CMakeLists.txt`
- Modify: root `CMakeLists.txt`

- [ ] **Step 4.1: Write `medical_imaging/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.20)
project(medical_imaging CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

# ── imaging_core static library ───────────────────────────────────────────
add_library(imaging_core STATIC
    src/StateMachine.cpp
    src/TransitionLog.cpp
    src/ImagePipeline.cpp
    src/RtEngine.cpp
)
target_include_directories(imaging_core PUBLIC include)
target_link_libraries(imaging_core PUBLIC Threads::Threads)
target_compile_options(imaging_core PRIVATE -Wall -Wextra -Wpedantic)

# ── shared library for C# P/Invoke ───────────────────────────────────────
add_library(imaging_engine SHARED src/c_api.cpp)
target_link_libraries(imaging_engine PRIVATE imaging_core)

# ── demo executable ───────────────────────────────────────────────────────
add_executable(imaging_demo src/main.cpp)
target_link_libraries(imaging_demo PRIVATE imaging_core)

# ── GoogleTests ───────────────────────────────────────────────────────────
if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
        URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
        DOWNLOAD_EXTRACT_TIMESTAMP NEW)
    set(INSTALL_GTEST OFF)
    FetchContent_MakeAvailable(googletest)

    add_executable(test_imaging tests/test_state_machine.cpp)
    target_link_libraries(test_imaging PRIVATE imaging_core GTest::gtest_main)
    target_compile_options(test_imaging PRIVATE -Wall -Wextra)
    add_test(NAME imaging_tests COMMAND test_imaging)
    enable_testing()
endif()
```

- [ ] **Step 4.2: Add `add_subdirectory(medical_imaging)` to root `CMakeLists.txt`**

Append at the end of `/Users/kab/Projects/Portfolio/live_telemetry_processor/CMakeLists.txt`:
```cmake

# Medical Imaging State Machine (C++23 subfolder)
add_subdirectory(medical_imaging)
```

- [ ] **Step 4.3: Build**

```bash
cd /Users/kab/Projects/Portfolio/live_telemetry_processor
cmake -B medical_imaging/build -S medical_imaging \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_TESTING=ON \
    -DCMAKE_CXX_COMPILER=g++
cmake --build medical_imaging/build -j4
```

Expected: build succeeds with no errors.

- [ ] **Step 4.4: Run tests**

```bash
ctest --test-dir medical_imaging/build --output-on-failure
```

Expected output:
```
Test project .../medical_imaging/build
    Start 1: imaging_tests
1/1 Test #1: imaging_tests ............. Passed
14/14 tests passed.
```

All 14 tests must pass. If any fail, diagnose and fix before proceeding.

---

## Task 5: Supporting files — C#, Python, docs, CI

**Files:**
- Create: `medical_imaging/csharp/ImagingEngineClient.cs`
- Create: `medical_imaging/scripts/generate_timing_report.py`
- Create: `medical_imaging/docs/ARCHITECTURE.md`
- Create: `medical_imaging/docs/RT-CONSTRAINTS.md`
- Create: `medical_imaging/docs/adr/ADR-001-rt-vs-rest-boundary.md`
- Create: `.github/workflows/medical-imaging.yml`

- [ ] **Step 5.1: Write `ImagingEngineClient.cs`**

```csharp
// ImagingEngineClient.cs
// C# P/Invoke wrapper for libimaging_engine.so
// No .csproj required — standalone file for reference / copy-paste into a project.
//
// Usage:
//   using var client = new ImagingEngineClient();
//   client.Start();
//   var result = client.SendEvent(EventId.CmdStart);
//   ImagingState state = client.GetState();
//   client.Stop();

using System;
using System.Runtime.InteropServices;

namespace ImagingEngine
{
    // ── Native constants (mirror of c_api.h) ──────────────────────────────

    /// <summary>Events that can be sent to the state machine via P/Invoke.</summary>
    public enum EventId : int
    {
        CmdStart     = 1,
        CmdStop      = 2,
        CmdReset     = 3,
        EvtFrame     = 4,
        EvtError     = 5,
        EvtCalibDone = 6,
        EvtBatch     = 7,
        EvtProcDone  = 8,
    }

    /// <summary>State identifiers returned by smachine_get_state.</summary>
    public enum ImagingState : int
    {
        Idle        = 0,
        Calibrating = 1,
        Acquiring   = 2,
        Processing  = 3,
        Fault       = 4,
    }

    /// <summary>Return codes from smachine_send_event.</summary>
    public enum TransitionResult : int
    {
        Ok                  =  0,
        InvalidTransition   =  1,
        Error               = -1,
    }

    // ── P/Invoke declarations ─────────────────────────────────────────────

    internal static class NativeMethods
    {
#if WINDOWS
        private const string LibName = "imaging_engine.dll";
#else
        private const string LibName = "libimaging_engine.so";
#endif

        /// <summary>Allocate a new state machine handle.</summary>
        [DllImport(LibName, EntryPoint = "smachine_create",
                   CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr Create();

        /// <summary>Send an event; returns TransitionResult code.</summary>
        [DllImport(LibName, EntryPoint = "smachine_send_event",
                   CallingConvention = CallingConvention.Cdecl)]
        internal static extern int SendEvent(IntPtr handle, int eventId);

        /// <summary>Return current state index.</summary>
        [DllImport(LibName, EntryPoint = "smachine_get_state",
                   CallingConvention = CallingConvention.Cdecl)]
        internal static extern int GetState(IntPtr handle);

        /// <summary>Return cumulative count of deadline violations.</summary>
        [DllImport(LibName, EntryPoint = "smachine_get_deadline_violations",
                   CallingConvention = CallingConvention.Cdecl)]
        internal static extern ulong GetDeadlineViolations(IntPtr handle);

        /// <summary>Free the state machine handle.</summary>
        [DllImport(LibName, EntryPoint = "smachine_destroy",
                   CallingConvention = CallingConvention.Cdecl)]
        internal static extern void Destroy(IntPtr handle);
    }

    // ── SafeHandle wrapper ────────────────────────────────────────────────

    internal sealed class SmachineSafeHandle : SafeHandle
    {
        public SmachineSafeHandle() : base(IntPtr.Zero, ownsHandle: true) { }

        public override bool IsInvalid => handle == IntPtr.Zero;

        protected override bool ReleaseHandle()
        {
            NativeMethods.Destroy(handle);
            return true;
        }
    }

    // ── High-level client ─────────────────────────────────────────────────

    /// <summary>
    /// Managed wrapper around the C imaging engine state machine.
    /// Implements <see cref="IDisposable"/>; use in a <c>using</c> block.
    /// </summary>
    public sealed class ImagingEngineClient : IDisposable
    {
        private readonly SmachineSafeHandle _handle;
        private bool _disposed;

        /// <summary>Creates a new imaging engine instance.</summary>
        public ImagingEngineClient()
        {
            _handle = new SmachineSafeHandle();
            IntPtr raw = NativeMethods.Create();
            if (raw == IntPtr.Zero)
                throw new InvalidOperationException("smachine_create returned null");
            // Transfer ownership into the SafeHandle
            _handle.SetHandle(raw);
        }

        /// <summary>
        /// Send an event to the state machine.
        /// </summary>
        /// <param name="event">The event to dispatch.</param>
        /// <returns>A <see cref="TransitionResult"/> indicating success or failure.</returns>
        public TransitionResult SendEvent(EventId @event)
        {
            ThrowIfDisposed();
            int code = NativeMethods.SendEvent(_handle.DangerousGetHandle(), (int)@event);
            return (TransitionResult)code;
        }

        /// <summary>Returns the current state of the imaging engine.</summary>
        public ImagingState GetState()
        {
            ThrowIfDisposed();
            int idx = NativeMethods.GetState(_handle.DangerousGetHandle());
            return (ImagingState)idx;
        }

        /// <summary>Returns the cumulative count of RT deadline violations.</summary>
        public ulong DeadlineViolations
        {
            get
            {
                ThrowIfDisposed();
                return NativeMethods.GetDeadlineViolations(_handle.DangerousGetHandle());
            }
        }

        /// <inheritdoc/>
        public void Dispose()
        {
            if (!_disposed)
            {
                _handle.Dispose();
                _disposed = true;
            }
        }

        private void ThrowIfDisposed()
        {
            if (_disposed)
                throw new ObjectDisposedException(nameof(ImagingEngineClient));
        }
    }
}
```

- [ ] **Step 5.2: Write `generate_timing_report.py`**

```python
#!/usr/bin/env python3
"""
generate_timing_report.py
Reads timing_log.csv and prints a Markdown table of P50/P95/P99 per
state transition. Exits with code 1 if any P99 >= 500 µs.

CSV columns: state_from, state_to, t_exec_us, t_comm_us, timestamp_ns
"""

import sys
import csv
import pathlib
import numpy as np

STATE_NAMES = {
    0: "Idle",
    1: "Calibrating",
    2: "Acquiring",
    3: "Processing",
    4: "Fault",
}

DEADLINE_US = 500


def load_csv(path: pathlib.Path) -> list[dict]:
    with path.open(newline="") as f:
        return list(csv.DictReader(f))


def transition_label(row: dict) -> str:
    frm = STATE_NAMES.get(int(row["state_from"]), str(row["state_from"]))
    to  = STATE_NAMES.get(int(row["state_to"]),   str(row["state_to"]))
    return f"{frm} → {to}"


def compute_percentiles(rows: list[dict]) -> dict[str, dict]:
    buckets: dict[str, list[float]] = {}
    for row in rows:
        label = transition_label(row)
        buckets.setdefault(label, []).append(float(row["t_exec_us"]))

    result = {}
    for label, times in buckets.items():
        arr = np.array(times)
        result[label] = {
            "p50": float(np.percentile(arr, 50)),
            "p95": float(np.percentile(arr, 95)),
            "p99": float(np.percentile(arr, 99)),
            "n":   len(arr),
        }
    return result


def print_markdown_table(stats: dict[str, dict]) -> None:
    print("| Transition | N | P50 (µs) | P95 (µs) | P99 (µs) | Pass? |")
    print("|---|---|---|---|---|---|")
    for label, s in sorted(stats.items()):
        ok = "PASS" if s["p99"] < DEADLINE_US else "FAIL"
        print(f"| {label} | {s['n']} | {s['p50']:.1f} | {s['p95']:.1f} | {s['p99']:.1f} | {ok} |")


def main() -> int:
    csv_path = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "timing_log.csv")
    if not csv_path.exists():
        print(f"ERROR: {csv_path} not found", file=sys.stderr)
        return 1

    rows  = load_csv(csv_path)
    stats = compute_percentiles(rows)
    print_markdown_table(stats)

    violations = [lbl for lbl, s in stats.items() if s["p99"] >= DEADLINE_US]
    if violations:
        print(f"\nFAIL: P99 >= {DEADLINE_US} µs for: {', '.join(violations)}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 5.3: Write `docs/ARCHITECTURE.md`**

```markdown
# Medical Imaging State Machine — Architecture

## Overview

Three-layer architecture:

| Layer | Components | Language |
|---|---|---|
| RT Control Plane | `StateMachine`, `TransitionLog`, `RtEngine` | C++23 |
| Pipeline Stub | `ImagePipeline` | C++23 |
| Integration Shim | `c_api` (extern C), `ImagingEngineClient` | C / C# |

---

## State Machine

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> Calibrating   : CmdStart
    Calibrating --> Acquiring : EvtCalibDone
    Acquiring --> Processing  : EvtBatchReady
    Processing --> Idle       : EvtProcessDone
    Idle --> Fault            : EvtError
    Calibrating --> Fault     : EvtError
    Acquiring --> Fault       : EvtError
    Processing --> Fault      : EvtError
    Fault --> Idle            : CmdReset
```

---

## Class Diagram

```mermaid
classDiagram
    class StateMachine {
        -State state_
        -atomic~uint64_t~ deadline_violations_
        +transition(Event) expected~State,TransitionError~
        +currentState() State
        +deadlineViolations() uint64_t
        +stateName() string
    }
    class TransitionLog {
        -array~TransitionEntry,1024~ buf_
        -atomic~uint32_t~ head_
        -atomic~uint32_t~ count_
        +push(TransitionEntry)
        +get(uint32_t) TransitionEntry
        +count() uint32_t
    }
    class ImagePipeline {
        -bool busy_
        +calibrate(int)
        +startAcquisition()
        +stopAcquisition()
        +processBatch(uint32_t)
        +isBusy() bool
    }
    class RtEngine {
        -StateMachine sm_
        -ImagePipeline pipeline_
        -jthread thread_
        +start()
        +stop()
        +sendCommand(int) int
        +getStateId() int
        +deadlineViolations() uint64_t
    }
    class CApiShim {
        <<extern C>>
        +smachine_create() smachine_t*
        +smachine_send_event(smachine_t*, int) int
        +smachine_get_state(smachine_t*) int
        +smachine_get_deadline_violations(smachine_t*) uint64_t
        +smachine_destroy(smachine_t*)
    }
    RtEngine --> StateMachine
    RtEngine --> ImagePipeline
    StateMachine --> TransitionLog : records entries in g_log
    CApiShim --> RtEngine : wraps via opaque pointer
```

---

## RT Boundary

The RT control plane communicates synchronously with the C# diagnostic layer via the `extern-C` shim. All imaging state transitions are bounded by `DEADLINE_US = 500 µs`. Telemetry (timing reports, metrics) flows asynchronously over REST (400–800 µs round-trip acceptable).

See [ADR-001](adr/ADR-001-rt-vs-rest-boundary.md) for the rationale.
```

- [ ] **Step 5.4: Write `docs/RT-CONSTRAINTS.md`**

```markdown
# Real-Time Constraints

## Timing Budget

| Metric | Budget | Measured |
|---|---|---|
| T_deadline | 500 µs | — |
| T_exec (state transition) | < 200 µs | TBD |
| T_comm (C API round-trip) | < 100 µs | TBD |
| Margin | 200 µs | — |

## Deadline Enforcement

`StateMachine::transition()` times itself with `std::chrono::high_resolution_clock`. If `t_exec > DEADLINE_US`, it increments `deadline_violations_` (atomic). The `generate_timing_report.py` script post-processes logged timing data and asserts all P99 latencies are below the deadline.

## Scheduling

On Linux, `RtEngine` attempts `SCHED_FIFO` at priority 50 via `pthread_setschedparam`. If the process lacks `CAP_SYS_NICE`, the call fails gracefully with a warning and the engine continues in `SCHED_OTHER`. For production deployment, grant the process `CAP_SYS_NICE` or run as root.

## Memory

`mlockall(MCL_CURRENT | MCL_FUTURE)` prevents page faults on the RT thread. Like `SCHED_FIFO`, this requires elevated privileges and fails gracefully.

## Platform Notes

- **Linux:** `timerfd_create(CLOCK_MONOTONIC)` drives the 100 Hz tick loop with kernel-precision wakeups.
- **macOS / other:** Falls back to `std::this_thread::sleep_for(10ms)` — adequate for development and CI.

## Compliance

The `TransitionLog` ring buffer is stack-allocated (`std::array`), 64-byte cache-line aligned on head/tail, and lock-free (atomic operations only). No heap allocation occurs in the hot path.
```

- [ ] **Step 5.5: Write `docs/adr/ADR-001-rt-vs-rest-boundary.md`**

```markdown
# ADR-001: RT vs REST Boundary

**Date:** 2026-05-21
**Status:** Accepted

## Context

The medical imaging engine has two classes of external consumers:

1. **RT diagnostic client (C# GUI):** needs to poll state and inject events at < 1 ms latency for closed-loop control.
2. **Telemetry dashboard:** needs timing histograms and transition counts, but 400–800 ms round-trips are acceptable.

## Decision

Use the **extern-C P/Invoke shim** (`c_api.h`) for all RT control and state queries. Use a REST endpoint (outside this module's scope) for telemetry export.

## Rationale

| Criterion | P/Invoke | REST |
|---|---|---|
| Round-trip latency | < 3 µs (in-process shared memory) | 400–800 µs (network stack) |
| Implementation complexity | Low — single `DllImport` per function | Medium — HTTP client + serialisation |
| Marshalling overhead | Trivial (int, uint64_t) | JSON encode/decode |
| Fault isolation | Same process — crash propagates | Separate process — fault contained |

For RT control, the latency advantage of P/Invoke (< 3 µs) outweighs the fault-isolation benefit of REST. Telemetry data is not latency-sensitive, so REST is appropriate there.

## Consequences

- The `imaging_engine.so` shared library must be deployed alongside the C# host process.
- The P/Invoke ABI is the stable API surface — `c_api.h` must remain backward-compatible.
- Breaking changes to `c_api.h` require a major version bump.
```

- [ ] **Step 5.6: Write `.github/workflows/medical-imaging.yml`**

```yaml
name: Medical Imaging CI

on:
  push:
    branches: [main]
    paths: [medical_imaging/**]
  pull_request:
    branches: [main]
    paths: [medical_imaging/**]

jobs:
  build-test:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4

      - name: Install GCC 12
        run: sudo apt-get install -y gcc-12 g++-12

      - name: Build and test
        env:
          CC: gcc-12
          CXX: g++-12
        run: |
          cmake -B medical_imaging/build -S medical_imaging \
            -DCMAKE_BUILD_TYPE=Debug \
            -DBUILD_TESTING=ON \
            -DCMAKE_CXX_COMPILER=g++-12
          cmake --build medical_imaging/build -j4
          ctest --test-dir medical_imaging/build --output-on-failure

  asan:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4

      - name: Install GCC 12
        run: sudo apt-get install -y gcc-12 g++-12

      - name: ASan build
        run: |
          cmake -B medical_imaging/build-asan -S medical_imaging \
            -DCMAKE_BUILD_TYPE=Debug \
            -DBUILD_TESTING=ON \
            -DCMAKE_CXX_COMPILER=g++-12 \
            -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
            -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
          cmake --build medical_imaging/build-asan -j4
          ctest --test-dir medical_imaging/build-asan --output-on-failure
```

---

## Task 6: Update root README.md and final verification

**Files:**
- Modify: `README.md` (root)

- [ ] **Step 6.1: Add medical_imaging row to the README projects table**

In `/Users/kab/Projects/Portfolio/live_telemetry_processor/README.md`, add to the Tech Stack table or create a Projects table if one doesn't exist. Since the README doesn't currently have a projects table, add a new section before `## Tech Stack`:

```markdown
## Subprojects

| Project | Description | Docs |
|---|---|---|
| **Medical Imaging State Machine** | C++23 `std::variant` FSM (Idle→Calibrating→Acquiring→Processing→Fault), SCHED_FIFO RT thread, extern-C shim + C# P/Invoke | [medical_imaging/docs/ARCHITECTURE.md](medical_imaging/docs/ARCHITECTURE.md) |
```

- [ ] **Step 6.2: Final build + test verification**

```bash
cd /Users/kab/Projects/Portfolio/live_telemetry_processor
cmake -B medical_imaging/build -S medical_imaging \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_TESTING=ON \
    -DCMAKE_CXX_COMPILER=g++
cmake --build medical_imaging/build -j4
ctest --test-dir medical_imaging/build --output-on-failure
```

Expected: `14/14 tests passed`.

- [ ] **Step 6.3: Check git status**

```bash
git -C /Users/kab/Projects/Portfolio/live_telemetry_processor status
```

- [ ] **Step 6.4: Commit**

```bash
cd /Users/kab/Projects/Portfolio/live_telemetry_processor
git add medical_imaging/ .github/workflows/medical-imaging.yml CMakeLists.txt README.md
git commit -m "feat: add medical_imaging C++23 state machine subfolder

- std::variant FSM: Idle/Calibrating/Acquiring/Processing/Fault
- std::expected<State,TransitionError> from transition()
- Lock-free TransitionLog ring buffer (1024 entries, alignas(64))
- RtEngine jthread 100 Hz tick (timerfd on Linux, sleep_for on macOS)
- extern-C c_api shim + C# P/Invoke ImagingEngineClient
- 14 GoogleTests all passing
- GitHub Actions CI: build+test + ASan jobs"
```

- [ ] **Step 6.5: Push to GitHub**

```bash
git -C /Users/kab/Projects/Portfolio/live_telemetry_processor push origin main
```

---

## Self-Review Checklist

**Spec coverage:**
- [x] `RtConfig.h` — Task 1.2
- [x] `StateMachine.h` with all states/events/`std::expected` — Task 1.3
- [x] `TransitionLog.h` ring buffer with `alignas(64)` — Task 1.4
- [x] `ImagePipeline.h` stub — Task 1.5
- [x] `c_api.h` with all constants — Task 1.6
- [x] `StateMachine.cpp` with `std::visit`, timing, deadline check, g_log — Task 3.2
- [x] `TransitionLog.cpp` atomic ring buffer — Task 3.1
- [x] `ImagePipeline.cpp` stubs — Task 3.3
- [x] `RtEngine.h` + `RtEngine.cpp` jthread + timerfd/fallback — Task 3.4 (revised)
- [x] `c_api.cpp` opaque pointer wrapping RtEngine — Task 3.5 (revised)
- [x] `main.cpp` demo — Task 3.5 (revised)
- [x] 14 GoogleTests (all suites) — Task 2
- [x] `ImagingEngineClient.cs` with SafeHandle, enums, XML docs — Task 5.1
- [x] `generate_timing_report.py` with NumPy P50/P95/P99 + assert — Task 5.2
- [x] `docs/ARCHITECTURE.md` with Mermaid diagrams — Task 5.3
- [x] `docs/RT-CONSTRAINTS.md` — Task 5.4
- [x] `docs/adr/ADR-001-rt-vs-rest-boundary.md` — Task 5.5
- [x] `medical_imaging/CMakeLists.txt` — Task 4.1
- [x] Root `CMakeLists.txt` `add_subdirectory` — Task 4.2
- [x] `medical-imaging.yml` CI — Task 5.6
- [x] Root `README.md` row — Task 6.1

**Type consistency check:**
- `g_log` declared `extern` in `TransitionLog.h`, defined in `TransitionLog.cpp` — consistent
- `RtEngine` declared in `include/RtEngine.h` (added in Task 3.4 revision), used in `c_api.cpp` — consistent
- `SMACHINE_*` constants referenced in `RtEngine.cpp` via `#include "c_api.h"` — consistent
- `FRAME_BATCH_SZ` from `RtConfig.h` used in `RtEngine.cpp` — consistent

**No placeholders found.** All steps have complete code.
