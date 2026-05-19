#pragma once
#include "../backend/MetricStore.h"
#include <QGroupBox>
#include <QLabel>

// Displays pipeline state, packet counters, and GNC channel readouts.
// Updated at 60 Hz from MainWindow.
class StatusPanel : public QGroupBox {
    Q_OBJECT
public:
    explicit StatusPanel(QWidget* parent = nullptr);

public slots:
    void onNewSnapshot(const TelemetrySnapshot& snap);

private:
    QLabel* state_label_;
    QLabel* frames_label_;
    QLabel* missed_label_;
    QLabel* pos_label_;
    QLabel* vel_label_;

    void apply_state_style(PipelineState s);
};
