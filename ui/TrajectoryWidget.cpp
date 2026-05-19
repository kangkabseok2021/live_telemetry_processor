#include "TrajectoryWidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <algorithm>
#include <cmath>

TrajectoryWidget::TrajectoryWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(400, 400);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
}

void TrajectoryWidget::onNewSnapshot(const TelemetrySnapshot& snap) {
    points_.push_back(snap.ned_pos);
    if (static_cast<int>(points_.size()) > kMaxPoints) points_.pop_front();
    predicted_ = snap.predicted_ned;

    // Auto-scale: expand range if trajectory exceeds current window
    for (const auto& p : {snap.ned_pos, snap.predicted_ned}) {
        double extent = std::max(std::abs(p[0]), std::abs(p[1]));
        if (extent * 1.2 > range_) range_ = extent * 1.5;
    }
    update();
}

void TrajectoryWidget::resizeEvent(QResizeEvent*) {
    scale_ = std::min(width(), height()) / 2.0 / range_;
}

void TrajectoryWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const double cx = width()  / 2.0;
    const double cy = height() / 2.0;

    // Grid
    p.setPen(QPen(Qt::darkGray, 1, Qt::DotLine));
    for (double d : {range_ * 0.25, range_ * 0.5, range_ * 0.75, range_}) {
        double r = d * scale_;
        p.drawEllipse(QPointF(cx, cy), r, r);
    }
    p.drawLine(QPointF(cx, 0), QPointF(cx, height()));
    p.drawLine(QPointF(0, cy), QPointF(width(), cy));

    // Axis labels
    p.setPen(Qt::gray);
    p.drawText(QPointF(cx + 4, 14), "N");
    p.drawText(QPointF(width() - 14, cy - 4), "E");

    if (points_.empty()) return;

    // Trajectory path — colour gradient green → red
    QPainterPath path;
    for (std::size_t i = 0; i < points_.size(); ++i) {
        double x = cx + points_[i][1] * scale_;  // East → screen X
        double y = cy - points_[i][0] * scale_;  // North → screen Y (inverted)
        if (i == 0) path.moveTo(x, y);
        else        path.lineTo(x, y);

        float t = static_cast<float>(i) / points_.size();
        p.setPen(QPen(QColor::fromRgbF(t, 1.0f - t, 0.0f), 1.5));
        if (i > 0) {
            QPainterPath seg;
            double px = cx + points_[i-1][1] * scale_;
            double py = cy - points_[i-1][0] * scale_;
            seg.moveTo(px, py);
            seg.lineTo(x, y);
            p.drawPath(seg);
        }
    }

    // Current position dot
    if (!points_.empty()) {
        double x = cx + points_.back()[1] * scale_;
        double y = cy - points_.back()[0] * scale_;
        p.setBrush(Qt::red);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(x, y), 5, 5);
    }

    // Predicted position
    {
        double x = cx + predicted_[1] * scale_;
        double y = cy - predicted_[0] * scale_;
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::yellow, 1, Qt::DashLine));
        p.drawEllipse(QPointF(x, y), 8, 8);
    }
}
