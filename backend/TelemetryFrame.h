#pragma once
#include <cstdint>
#include <cstddef>
#include <span>

// Binary telemetry packet layout — matches HIL simulator and production
// vehicle software.  All fields little-endian.
#pragma pack(push, 1)
struct TelemetryFrame {
    static constexpr uint32_t kMagic = 0x41455243u;  // "AERC"

    uint32_t magic;           // 4 B — must equal kMagic
    uint32_t sequence;        // 4 B — monotonically increasing
    uint64_t timestamp_ns;    // 8 B — CLOCK_MONOTONIC at sender

    // GNC state vector (48 B)
    double pos_ecef[3];       // m  — ECEF X, Y, Z
    double vel_ecef[3];       // m/s — body velocity in ECEF frame

    // Structural temperatures (32 B)
    float temperature[8];     // °C — thermocouple channels 0–7

    // Trajectory point pre-computed by vehicle (24 B)
    double traj_ned[3];       // m  — North, East, Down from launch site
};
#pragma pack(pop)

static_assert(sizeof(TelemetryFrame) == 120,
              "TelemetryFrame size changed — update parser and HIL sim");

// Pipeline state — written by processing thread, read by UI (atomic).
enum class PipelineState : uint8_t {
    INIT,
    LINK_OK,
    LINK_DEGRADED,  // sequence gap > 10
    SENSOR_OUTLIER, // > 20 % outlier frames in 1 s window
    MISSION_ABORT,  // latching, cleared by operator
};

const char* pipeline_state_name(PipelineState s) noexcept;
