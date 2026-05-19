#pragma once
#include "TelemetryFrame.h"
#include <array>
#include <cstddef>

// Per-channel moving-window deviation filter.
// Flags samples exceeding k·σ as outliers (O(1) update via running stats).
template<std::size_t N>
class RollingStats {
public:
    void push(double x) noexcept {
        sum_sq_ -= buf_[idx_] * buf_[idx_];
        sum_    -= buf_[idx_];
        buf_[idx_] = x;
        sum_sq_ += x * x;
        sum_    += x;
        idx_ = (idx_ + 1) % N;
        if (count_ < N) ++count_;
    }
    double mean()     const noexcept { return count_ ? sum_ / count_ : 0.0; }
    double variance() const noexcept {
        if (count_ < 2) return 0.0;
        double m = mean();
        return sum_sq_ / count_ - m * m;
    }
    double stddev()   const noexcept;
    std::size_t count() const noexcept { return count_; }

private:
    std::array<double, N> buf_{};
    double                sum_{0.0}, sum_sq_{0.0};
    std::size_t           idx_{0}, count_{0};
};

struct ValidationResult {
    bool     outlier{false};     // true = sample flagged
    bool     seq_gap{false};     // true = sequence discontinuity
    uint32_t missed_count{0};    // cumulative missed packets
};

// Validates a parsed frame: sequence continuity + per-channel deviation.
class GncValidator {
public:
    static constexpr std::size_t kWindow    = 50;
    static constexpr double      kThreshold = 4.0;  // k·σ

    ValidationResult validate(const TelemetryFrame& frame) noexcept;
    void reset() noexcept;

private:
    uint32_t last_seq_{0};
    bool     first_{true};
    uint32_t missed_{0};

    // One RollingStats per monitored channel (pos × 3, vel × 3)
    RollingStats<kWindow> pos_[3];
    RollingStats<kWindow> vel_[3];
};
