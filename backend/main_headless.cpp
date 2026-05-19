#include "CoordTransform.h"
#include "GncValidator.h"
#include "MetricStore.h"
#include "SpscQueue.h"
#include "TelemetryFrame.h"
#include "TrajectoryPredictor.h"
#include "UdpReceiver.h"
#include <chrono>
#include <csignal>
#include <iostream>
#include <stop_token>
#include <thread>

static std::stop_source g_stop;
static void on_signal(int) { g_stop.request_stop(); }

int main(int argc, char* argv[]) {
    uint16_t port = 57300;
    if (argc > 1) port = static_cast<uint16_t>(std::atoi(argv[1]));

    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    // Cape Canaveral as default launch site
    CoordTransform  coord(28.4, -80.6, 0.0);
    GncValidator    validator;
    TrajectoryPredictor predictor;
    MetricStore     store;

    UdpReceiver::Queue queue;
    UdpReceiver receiver(queue, port);
    receiver.start();
    store.set_state(PipelineState::LINK_OK);

    std::cout << "Telemetry processor listening on UDP :" << port << "\n";

    uint64_t frame_count = 0;
    int      outlier_window = 0;

    auto stop = g_stop.get_token();
    while (!stop.stop_requested()) {
        auto opt = queue.pop();
        if (!opt) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }

        const TelemetryFrame& f = *opt;
        ++frame_count;

        auto vr = validator.validate(f);

        // Update pipeline state
        PipelineState ps = PipelineState::LINK_OK;
        if (vr.missed_count > 10) ps = PipelineState::LINK_DEGRADED;
        if (vr.outlier) ++outlier_window;
        else            outlier_window = 0;
        if (outlier_window > 200) ps = PipelineState::SENSOR_OUTLIER;

        auto ned_pos = coord.ecef_to_ned(f.pos_ecef);
        auto ned_vel = coord.vel_ecef_to_ned(f.vel_ecef);
        predictor.push(ned_pos);
        auto pred = predictor.predict(0.5);

        TelemetrySnapshot snap;
        snap.frame         = f;
        snap.ned_pos       = ned_pos;
        snap.ned_vel       = ned_vel;
        snap.predicted_ned = pred;
        snap.outlier       = vr.outlier;
        snap.seq_gap       = vr.seq_gap;
        snap.missed_count  = vr.missed_count;
        snap.frame_count   = frame_count;
        snap.state         = ps;
        store.update(snap);

        if (frame_count % 1000 == 0) {
            std::cout << "Frames: " << frame_count
                      << "  Missed: " << vr.missed_count
                      << "  State: " << pipeline_state_name(ps)
                      << "  NED: [" << ned_pos[0] << ", "
                      << ned_pos[1] << ", " << ned_pos[2] << "]\n";
        }
    }

    receiver.stop();
    return 0;
}
