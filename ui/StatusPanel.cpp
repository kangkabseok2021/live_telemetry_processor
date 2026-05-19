#include "StatusPanel.h"
#include <QFont>
#include <QVBoxLayout>
#include <QString>

StatusPanel::StatusPanel(QWidget* parent) : QGroupBox("Mission Status", parent) {
    QFont mono("Courier New", 10);

    state_label_  = new QLabel("INIT");
    frames_label_ = new QLabel("Frames: 0");
    missed_label_ = new QLabel("Missed: 0");
    pos_label_    = new QLabel("NED Pos: --");
    vel_label_    = new QLabel("NED Vel: --");

    for (auto* l : {frames_label_, missed_label_, pos_label_, vel_label_})
        l->setFont(mono);

    state_label_->setAlignment(Qt::AlignCenter);
    state_label_->setStyleSheet("font-size: 18px; font-weight: bold; padding: 6px;");

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(state_label_);
    layout->addWidget(frames_label_);
    layout->addWidget(missed_label_);
    layout->addWidget(pos_label_);
    layout->addWidget(vel_label_);
}

void StatusPanel::apply_state_style(PipelineState s) {
    QString bg;
    switch (s) {
        case PipelineState::LINK_OK:        bg = "#1a472a"; break;
        case PipelineState::LINK_DEGRADED:  bg = "#5c4a00"; break;
        case PipelineState::SENSOR_OUTLIER: bg = "#5c0000"; break;
        case PipelineState::MISSION_ABORT:  bg = "#8b0000"; break;
        default:                            bg = "#2c2c2c"; break;
    }
    state_label_->setStyleSheet(
        QString("font-size:18px;font-weight:bold;padding:6px;"
                "background:%1;color:white;border-radius:4px;").arg(bg));
}

void StatusPanel::onNewSnapshot(const TelemetrySnapshot& snap) {
    state_label_->setText(pipeline_state_name(snap.state));
    apply_state_style(snap.state);

    frames_label_->setText(
        QString("Frames: %1").arg(snap.frame_count));
    missed_label_->setText(
        QString("Missed: %1").arg(snap.missed_count));

    pos_label_->setText(QString("NED Pos: N=%1  E=%2  D=%3 m")
        .arg(snap.ned_pos[0], 8, 'f', 1)
        .arg(snap.ned_pos[1], 8, 'f', 1)
        .arg(snap.ned_pos[2], 8, 'f', 1));

    vel_label_->setText(QString("NED Vel: N=%1  E=%2  D=%3 m/s")
        .arg(snap.ned_vel[0], 7, 'f', 2)
        .arg(snap.ned_vel[1], 7, 'f', 2)
        .arg(snap.ned_vel[2], 7, 'f', 2));
}
