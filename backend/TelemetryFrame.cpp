#include "TelemetryFrame.h"

const char* pipeline_state_name(PipelineState s) noexcept {
    switch (s) {
        case PipelineState::INIT:           return "INIT";
        case PipelineState::LINK_OK:        return "LINK_OK";
        case PipelineState::LINK_DEGRADED:  return "LINK_DEGRADED";
        case PipelineState::SENSOR_OUTLIER: return "SENSOR_OUTLIER";
        case PipelineState::MISSION_ABORT:  return "MISSION_ABORT";
    }
    return "UNKNOWN";
}
