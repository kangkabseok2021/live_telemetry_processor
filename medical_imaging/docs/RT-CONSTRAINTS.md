# Real-Time Constraints

## Timing Budget

| Metric | Budget | Notes |
|---|---|---|
| T_deadline | 500 µs | Configured in `RtConfig.h` as `DEADLINE_US` |
| T_exec (state transition) | < 200 µs | Measured by `StateMachine::transition()` |
| T_comm (C API round-trip) | < 100 µs | P/Invoke call overhead (in-process) |
| Margin | 200 µs | T_deadline − T_exec − T_comm |

## Deadline Enforcement

`StateMachine::transition()` times itself with `std::chrono::high_resolution_clock`. If `t_exec > DEADLINE_US`, it increments `deadline_violations_` (atomic, relaxed order). Callers can poll `StateMachine::deadlineViolations()` or `RtEngine::deadlineViolations()` to detect RT budget overruns without locking.

The `generate_timing_report.py` script post-processes a `timing_log.csv` file (populated externally from the `TransitionLog` ring buffer) and asserts all P99 latencies are below the deadline, exiting with code 1 on failure.

## RT Thread Scheduling

On Linux, `RtEngine::start()` spawns a `std::jthread`. The tick loop (`tickLoop`) attempts:

```cpp
sched_param sp{};
sp.sched_priority = 50;
pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
```

If the process lacks `CAP_SYS_NICE`, `pthread_setschedparam` returns `EPERM` and a warning is printed to stderr. The engine continues in `SCHED_OTHER`. For production deployment, grant the binary `CAP_SYS_NICE`:

```bash
sudo setcap cap_sys_nice+ep ./imaging_demo
```

or run it under a systemd unit with `AmbientCapabilities=CAP_SYS_NICE`.

## Memory Locking

`mlockall(MCL_CURRENT | MCL_FUTURE)` is called to prevent page faults on the RT thread. This also requires elevated privileges and fails gracefully with a warning if unavailable.

## Tick Rate

The RT engine runs at 100 Hz (10 ms tick interval):

- **Linux:** `timerfd_create(CLOCK_MONOTONIC)` with 10 ms interval — kernel-precision wakeups, minimal drift.
- **macOS / other:** `std::this_thread::sleep_for(10ms)` fallback — adequate for development and CI but not for production RT use.

## TransitionLog Ring Buffer

The `TransitionLog` class is designed to be safe on the hot RT path:

- **No heap allocation:** `std::array<TransitionEntry, 1024>` is stack-allocated in the object.
- **Cache-line aligned head/tail:** `alignas(64)` prevents false sharing between producer and consumer cores.
- **Lock-free:** only `std::atomic` operations (relaxed/relaxed) — no mutex on the hot path.
- **Ring wrap:** once 1024 entries are written, `head_` wraps modulo `LOG_CAPACITY`; `count_` saturates at `LOG_CAPACITY`.

## Platform Notes

| Platform | RT Scheduling | Memory Lock | Timer |
|---|---|---|---|
| Linux (production) | SCHED_FIFO (with CAP_SYS_NICE) | mlockall | timerfd |
| Linux (CI/dev) | SCHED_OTHER (EPERM, graceful) | fails gracefully | timerfd |
| macOS (dev) | Not available | Not available | sleep_for |
