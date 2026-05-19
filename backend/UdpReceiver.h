#pragma once
#include "SpscQueue.h"
#include "TelemetryFrame.h"
#include <atomic>
#include <cstdint>
#include <stop_token>
#include <thread>

// UDP socket listener running in a dedicated std::jthread.
// Pushes raw frames into an SpscQueue for zero-copy handoff to the
// processing thread.  Attempts SCHED_FIFO scheduling; silently
// falls back to default scheduling when run without CAP_SYS_NICE.
class UdpReceiver {
public:
    using Queue = SpscQueue<TelemetryFrame, 4096>;

    explicit UdpReceiver(Queue& queue, uint16_t port = 57300);
    ~UdpReceiver();

    void start();
    void stop();

    uint64_t received() const noexcept {
        return received_.load(std::memory_order_relaxed);
    }
    uint64_t dropped() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

private:
    void run(std::stop_token st);

    Queue&    queue_;
    uint16_t  port_;
    int       sock_{-1};

    std::jthread            thread_;
    std::atomic<uint64_t>   received_{0};
    std::atomic<uint64_t>   dropped_{0};
};
