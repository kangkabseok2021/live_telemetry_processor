#pragma once
#include "../backend/CoordTransform.h"
#include "../backend/GncValidator.h"
#include "../backend/MetricStore.h"
#include "../backend/SpscQueue.h"
#include "../backend/TrajectoryPredictor.h"
#include "../backend/UdpReceiver.h"
#include "StatusPanel.h"
#include "TrajectoryWidget.h"
#include <QMainWindow>
#include <QTimer>
#include <memory>
#include <thread>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(uint16_t port = 57300, QWidget* parent = nullptr);
    ~MainWindow() override;

signals:
    void newSnapshot(const TelemetrySnapshot& snap);

private slots:
    void onRenderTick();

private:
    void processing_loop(std::stop_token st);

    UdpReceiver::Queue  queue_;
    UdpReceiver         receiver_;
    CoordTransform      coord_;
    GncValidator        validator_;
    TrajectoryPredictor predictor_;
    MetricStore         store_;

    std::jthread        proc_thread_;
    QTimer*             render_timer_;

    TrajectoryWidget*   traj_widget_;
    StatusPanel*        status_panel_;
};
