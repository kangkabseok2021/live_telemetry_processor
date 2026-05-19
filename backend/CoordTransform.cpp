#include "CoordTransform.h"
#include <cmath>

static constexpr double kA  = 6378137.0;        // WGS-84 semi-major axis [m]
static constexpr double kE2 = 6.6943799901414e-3; // WGS-84 first eccentricity squared

// Geodetic (lat, lon, alt) → ECEF
static void lla_to_ecef(double lat_rad, double lon_rad, double alt_m,
                         double& x, double& y, double& z) {
    double sin_lat = std::sin(lat_rad);
    double cos_lat = std::cos(lat_rad);
    double N = kA / std::sqrt(1.0 - kE2 * sin_lat * sin_lat);
    x = (N + alt_m) * cos_lat * std::cos(lon_rad);
    y = (N + alt_m) * cos_lat * std::sin(lon_rad);
    z = (N * (1.0 - kE2) + alt_m) * sin_lat;
}

CoordTransform::CoordTransform(double lat_deg, double lon_deg, double alt_m) {
    const double lat = lat_deg * M_PI / 180.0;
    const double lon = lon_deg * M_PI / 180.0;

    lla_to_ecef(lat, lon, alt_m, ox_, oy_, oz_);

    // ECEF-to-NED rotation matrix (rows = North, East, Down unit vectors)
    double sin_lat = std::sin(lat), cos_lat = std::cos(lat);
    double sin_lon = std::sin(lon), cos_lon = std::cos(lon);

    // North row
    R_[0][0] = -sin_lat * cos_lon;
    R_[0][1] = -sin_lat * sin_lon;
    R_[0][2] =  cos_lat;
    // East row
    R_[1][0] = -sin_lon;
    R_[1][1] =  cos_lon;
    R_[1][2] =  0.0;
    // Down row
    R_[2][0] = -cos_lat * cos_lon;
    R_[2][1] = -cos_lat * sin_lon;
    R_[2][2] = -sin_lat;
}

std::array<double, 3> CoordTransform::ecef_to_ned(const double ecef[3]) const noexcept {
    double dx = ecef[0] - ox_;
    double dy = ecef[1] - oy_;
    double dz = ecef[2] - oz_;
    return {
        R_[0][0]*dx + R_[0][1]*dy + R_[0][2]*dz,
        R_[1][0]*dx + R_[1][1]*dy + R_[1][2]*dz,
        R_[2][0]*dx + R_[2][1]*dy + R_[2][2]*dz,
    };
}

std::array<double, 3> CoordTransform::vel_ecef_to_ned(const double vel[3]) const noexcept {
    return {
        R_[0][0]*vel[0] + R_[0][1]*vel[1] + R_[0][2]*vel[2],
        R_[1][0]*vel[0] + R_[1][1]*vel[1] + R_[1][2]*vel[2],
        R_[2][0]*vel[0] + R_[2][1]*vel[1] + R_[2][2]*vel[2],
    };
}
