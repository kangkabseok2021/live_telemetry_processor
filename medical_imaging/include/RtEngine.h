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
