#include "TrajectoryPredictor.h"
#include <cmath>
#include <numeric>

void TrajectoryPredictor::push(const std::array<double, 3>& ned) noexcept {
    pts_.push_back(ned);
    if (pts_.size() > kWindow) pts_.pop_front();
    t_ += 1.0;
}

// Solve 3×3 symmetric positive definite system via Cholesky decomposition.
// Returns x in Ax = b.  Matrix A is column-major 3×3.
static std::array<double, 3> solve3(double A[3][3], double b[3]) noexcept {
    // Forward elimination (simplified for 3×3)
    for (int k = 0; k < 3; ++k) {
        for (int i = k + 1; i < 3; ++i) {
            double f = A[i][k] / A[k][k];
            for (int j = k; j < 3; ++j) A[i][j] -= f * A[k][j];
            b[i] -= f * b[k];
        }
    }
    // Back substitution
    std::array<double, 3> x{};
    for (int i = 2; i >= 0; --i) {
        x[i] = b[i];
        for (int j = i + 1; j < 3; ++j) x[i] -= A[i][j] * x[j];
        x[i] /= A[i][i];
    }
    return x;
}

std::array<double, 3> TrajectoryPredictor::fit_axis(int axis) const noexcept {
    if (pts_.size() < 3) return {};
    const std::size_t n = pts_.size();

    // Build normal equations: A^T·A·c = A^T·y
    // Basis functions: [1, t, t²] with t ∈ [0, n-1]
    double ATA[3][3]{}, ATb[3]{};
    for (std::size_t i = 0; i < n; ++i) {
        double t  = static_cast<double>(i);
        double t2 = t * t;
        double t3 = t2 * t;
        double t4 = t3 * t;
        double y  = pts_[i][axis];

        ATA[0][0] += 1.0; ATA[0][1] += t;  ATA[0][2] += t2;
        ATA[1][0] += t;   ATA[1][1] += t2; ATA[1][2] += t3;
        ATA[2][0] += t2;  ATA[2][1] += t3; ATA[2][2] += t4;
        ATb[0] += y;
        ATb[1] += y * t;
        ATb[2] += y * t2;
    }
    return solve3(ATA, ATb);
}

std::array<double, 3> TrajectoryPredictor::predict(double dt_s) const noexcept {
    if (pts_.empty()) return {};
    if (pts_.size() < 3) return pts_.back();

    double t_pred = static_cast<double>(pts_.size() - 1) + dt_s;
    std::array<double, 3> result{};
    for (int axis = 0; axis < 3; ++axis) {
        auto c = fit_axis(axis);
        result[axis] = c[0] + c[1]*t_pred + c[2]*t_pred*t_pred;
    }
    return result;
}
