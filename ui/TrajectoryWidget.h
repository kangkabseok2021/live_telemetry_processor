#pragma once
#include "../backend/MetricStore.h"
#include <QWidget>
#include <deque>

// Renders the live NED trajectory as a 2D top-down (North-East) plot.
// Updated at 60 Hz via a QTimer signal from MainWindow.
// Never called from the processing thread — Qt thread-affinity satisfied.
class TrajectoryWidget : public QWidget {
    Q_OBJECT
public:
    explicit TrajectoryWidget(QWidget* parent = nullptr);

    static constexpr int kMaxPoints = 10'000;

public slots:
    void onNewSnapshot(const TelemetrySnapshot& snap);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    std::deque<std::array<double, 3>> points_;   // NED history
    std::array<double, 3>             predicted_{};
    double scale_{1.0};
    double range_{5000.0};  // visible range [m] each axis
};
