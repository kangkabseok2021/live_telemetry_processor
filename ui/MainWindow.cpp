#include "MainWindow.h"
#include <QDockWidget>
#include <QHBoxLayout>
#include <chrono>

MainWindow::MainWindow(uint16_t port, QWidget* parent)
    : QMainWindow(parent)
    , receiver_(queue_, port)
    , coord_(28.4, -80.6, 0.0)     // Cape Canaveral launch site
{
    setWindowTitle("Live Telemetry Processor");
    resize(1200, 800);

    traj_widget_  = new TrajectoryWidget(this);
    status_panel_ = new StatusPanel(this);

    setCentralWidget(traj_widget_);

    auto* dock = new QDockWidget("Mission Status", this);
    dock->setWidget(status_panel_);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    // Connect snapshot signal → UI slots (QueuedConnection: safe cross-thread)
    connect(this, &MainWindow::newSnapshot,
            traj_widget_, &TrajectoryWidget::onNewSnapshot,
            Qt::QueuedConnection);
    connect(this, &MainWindow::newSnapshot,
            status_panel_, &StatusPanel::onNewSnapshot,
            Qt::QueuedConnection);

    // 60 Hz render timer on GUI thread — reads MetricStore snapshot
    render_timer_ = new QTimer(this);
    connect(render_timer_, &QTimer::timeout, this, &MainWindow::onRenderTick);
    render_timer_->start(1000 / 60);  // ~16.7 ms

    receiver_.start();
    store_.set_state(PipelineState::LINK_OK);

    // Processing jthread — never touches Qt objects directly
    proc_thread_ = std::jthread([this](std::stop_token st) {
        processing_loop(st);
    });
}

MainWindow::~MainWindow() {
    proc_thread_.request_stop();
    receiver_.stop();
}

void MainWindow::onRenderTick() {
    auto snap = store_.snapshot();
    emit newSnapshot(snap);
}

void MainWindow::processing_loop(std::stop_token st) {
    uint64_t frame_count = 0;
    int      outlier_window = 0;

    while (!st.stop_requested()) {
        auto opt = queue_.pop();
        if (!opt) {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            continue;
        }

        const TelemetryFrame& f = *opt;
        ++frame_count;

        auto vr      = validator_.validate(f);
        auto ned_pos = coord_.ecef_to_ned(f.pos_ecef);
        auto ned_vel = coord_.vel_ecef_to_ned(f.vel_ecef);
        predictor_.push(ned_pos);
        auto pred    = predictor_.predict(0.5);

        PipelineState ps = PipelineState::LINK_OK;
        if (vr.seq_gap && vr.missed_count > 10) ps = PipelineState::LINK_DEGRADED;
        if (vr.outlier) ++outlier_window; else outlier_window = 0;
        if (outlier_window > 200) ps = PipelineState::SENSOR_OUTLIER;

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
        store_.update(snap);
    }
}
