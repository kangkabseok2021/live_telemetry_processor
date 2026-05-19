#pragma once
#include "TelemetryFrame.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <shared_mutex>
#include <string>

// Thread-safe snapshot of the latest processed telemetry state.
// Processing thread holds unique_lock to write; Qt 60 Hz timer holds
// shared_lock to read — zero contention on the non-write path.
struct TelemetrySnapshot {
    TelemetryFrame frame{};           // latest validated raw frame
    std::array<double, 3> ned_pos{};  // NED position [m]
    std::array<double, 3> ned_vel{};  // NED velocity [m/s]
    std::array<double, 3> predicted_ned{};  // 500 ms ahead prediction
    bool   outlier{false};
    bool   seq_gap{false};
    uint32_t missed_count{0};
    uint64_t frame_count{0};
    PipelineState state{PipelineState::INIT};
};

class MetricStore {
public:
    void   update(const TelemetrySnapshot& snap);
    TelemetrySnapshot snapshot() const;

    PipelineState state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }
    void set_state(PipelineState s) noexcept {
        state_.store(s, std::memory_order_release);
    }

private:
    mutable std::shared_mutex mu_;
    TelemetrySnapshot         snap_;
    std::atomic<PipelineState> state_{PipelineState::INIT};
};
