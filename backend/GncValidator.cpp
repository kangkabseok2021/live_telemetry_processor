#include "GncValidator.h"
#include <cmath>

template<std::size_t N>
double RollingStats<N>::stddev() const noexcept {
    double v = variance();
    return v > 0.0 ? std::sqrt(v) : 0.0;
}

// Explicit instantiation for kWindow = 50
template class RollingStats<GncValidator::kWindow>;

static bool is_outlier(RollingStats<GncValidator::kWindow>& stats,
                       double value, double k) noexcept {
    double sd = stats.stddev();
    bool flagged = sd > 0.0 &&
                   std::abs(value - stats.mean()) > k * sd &&
                   stats.count() >= 10;  // need warm-up samples
    stats.push(value);
    return flagged;
}

ValidationResult GncValidator::validate(const TelemetryFrame& frame) noexcept {
    ValidationResult r;

    // Sequence check
    if (first_) {
        first_ = false;
    } else {
        uint32_t expected = last_seq_ + 1;
        if (frame.sequence != expected) {
            r.seq_gap = true;
            missed_ += frame.sequence > expected
                ? (frame.sequence - expected)
                : 1;
        }
    }
    last_seq_    = frame.sequence;
    r.missed_count = missed_;

    // Per-channel deviation check
    for (int i = 0; i < 3; ++i) {
        if (is_outlier(pos_[i], frame.pos_ecef[i], kThreshold)) r.outlier = true;
        if (is_outlier(vel_[i], frame.vel_ecef[i], kThreshold)) r.outlier = true;
    }

    return r;
}

void GncValidator::reset() noexcept {
    first_   = true;
    missed_  = 0;
    last_seq_ = 0;
    for (auto& s : pos_) s = {};
    for (auto& s : vel_) s = {};
}
