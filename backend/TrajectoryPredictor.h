#pragma once
#include <array>
#include <deque>
#include <cstddef>

// 2nd-order polynomial trajectory predictor using least-squares.
// Fits parabola to the last N NED positions; projects forward dt_s seconds.
//
// Solves A^T·A·x = A^T·b (3×3 normal equations via Cholesky) for each axis.
class TrajectoryPredictor {
public:
    static constexpr std::size_t kWindow = 100;  // sample history

    void push(const std::array<double, 3>& ned_m) noexcept;

    // Returns predicted NED position dt_s seconds ahead.
    // Returns last known position if fewer than 3 samples accumulated.
    std::array<double, 3> predict(double dt_s) const noexcept;

    std::size_t count() const noexcept { return pts_.size(); }
    void reset() noexcept { pts_.clear(); t_ = 0.0; }

private:
    std::deque<std::array<double, 3>> pts_;
    double t_{0.0};  // sample index (used as time axis)

    // Fit parabola to one axis; returns [a0, a1, a2] for a0 + a1·t + a2·t².
    std::array<double, 3> fit_axis(int axis) const noexcept;
};
