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
        case SMACHINE_CMD_START:
            result = sm_.transition(CmdStart{});
            break;
        case SMACHINE_CMD_STOP:
            result = sm_.transition(CmdStop{});
            break;
        case SMACHINE_CMD_RESET:
            result = sm_.transition(CmdReset{});
            break;
        case SMACHINE_EVT_FRAME:
            result = sm_.transition(EvtFrameReady{});
            break;
        case SMACHINE_EVT_ERROR:
            result = sm_.transition(EvtError{"external"});
            break;
        case SMACHINE_EVT_CALIB_DONE:
            result = sm_.transition(EvtCalibDone{});
            break;
        case SMACHINE_EVT_BATCH:
            result = sm_.transition(EvtBatchReady{FRAME_BATCH_SZ});
            break;
        case SMACHINE_EVT_PROC_DONE:
            result = sm_.transition(EvtProcessDone{});
            break;
        default:
            return SMACHINE_ERR;
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
    // Attempt RT scheduling — requires CAP_SYS_NICE
    sched_param sp{};
    sp.sched_priority = 50;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0) {
        std::fprintf(stderr,
            "[RtEngine] SCHED_FIFO unavailable — continuing without RT priority\n");
    }
    // Attempt memory lock — requires elevated privileges
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        std::fprintf(stderr,
            "[RtEngine] mlockall failed — continuing without memory lock\n");
    }

    int tfd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (tfd >= 0) {
        itimerspec its{};
        its.it_value.tv_nsec    = 10'000'000;  // 10 ms first expiry
        its.it_interval.tv_nsec = 10'000'000;  // 10 ms interval = 100 Hz
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
    // macOS / fallback: sleep-based 100 Hz loop
    while (!st.stop_requested()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        tick();
    }
}

void RtEngine::tick() {
    // Tick loop monitors state for autonomous transitions.
    // External events are injected via sendCommand().
    // Example auto-transition: if Calibrating with no steps left → EvtCalibDone
    const State& cur = sm_.currentState();
    if (std::holds_alternative<Calibrating>(cur)) {
        if (std::get<Calibrating>(cur).steps_remaining <= 0) {
            (void)sm_.transition(EvtCalibDone{});
        }
    }
}

} // namespace imaging
