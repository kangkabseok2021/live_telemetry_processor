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

Valid state transitions are enforced by `StateMachine::transition()` via `std::visit` over a `std::variant<Idle, Calibrating, Acquiring, Processing, Fault>`. Invalid transitions return `std::unexpected(TransitionError::INVALID_TRANSITION)`.

---

## Class Diagram

```mermaid
classDiagram
    class StateMachine {
        -State state_
        -atomic~uint64_t~ deadline_violations_
        +transition(Event) expected~State,TransitionError~
        +currentState() const State ref
        +deadlineViolations() uint64_t
        +stateName() string
    }
    class TransitionLog {
        -array~TransitionEntry,1024~ buf_
        -atomic~uint32_t~ head_  [alignas(64)]
        -atomic~uint32_t~ count_ [alignas(64)]
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
    class ImagingEngineClient {
        <<C#>>
        -SmachineSafeHandle _handle
        +SendEvent(EventId) TransitionResult
        +GetState() ImagingState
        +DeadlineViolations uint64
        +Dispose()
    }

    RtEngine --> StateMachine : owns
    RtEngine --> ImagePipeline : owns
    StateMachine --> TransitionLog : records entries in g_log
    CApiShim --> RtEngine : wraps via smachine_tag opaque pointer
    ImagingEngineClient --> CApiShim : P/Invoke DllImport
```

---

## RT Boundary

The RT control plane communicates synchronously with the C# diagnostic layer via the `extern-C` shim compiled into `libimaging_engine.so`. All imaging state transitions are bounded by `DEADLINE_US = 500 µs`. Telemetry (timing reports, metrics) flows asynchronously over REST at 400–800 ms round-trips, which is fully acceptable for non-real-time dashboards.

See [ADR-001](adr/ADR-001-rt-vs-rest-boundary.md) for the rationale behind this boundary decision.

---

## Module Layout

```
medical_imaging/
├── include/
│   ├── RtConfig.h          Compile-time constants (DEADLINE_US, LOG_CAPACITY, ...)
│   ├── StateMachine.h      State/Event variant types + StateMachine class
│   ├── TransitionLog.h     Lock-free ring-buffer log + TransitionEntry struct
│   ├── ImagePipeline.h     Sensor pipeline stub interface
│   ├── RtEngine.h          100 Hz jthread engine wrapping SM + pipeline
│   └── c_api.h             extern-C opaque API + integer constants
├── src/
│   ├── StateMachine.cpp    std::visit transition table, timing, deadline check
│   ├── TransitionLog.cpp   Atomic push/get/count + g_log definition
│   ├── ImagePipeline.cpp   Stub implementations
│   ├── RtEngine.cpp        timerfd (Linux) / sleep_for (macOS) tick loop
│   ├── c_api.cpp           smachine_tag + 5 C functions
│   └── main.cpp            Demo: start engine, CmdStart, 3s, print state
├── tests/
│   └── test_state_machine.cpp  14 GoogleTests
├── csharp/
│   └── ImagingEngineClient.cs  C# P/Invoke wrapper (no .csproj needed)
├── scripts/
│   └── generate_timing_report.py  P50/P95/P99 Markdown table from CSV
├── docs/
│   ├── ARCHITECTURE.md     (this file)
│   ├── RT-CONSTRAINTS.md   Timing budget and scheduling documentation
│   └── adr/
│       └── ADR-001-rt-vs-rest-boundary.md
└── CMakeLists.txt
```
