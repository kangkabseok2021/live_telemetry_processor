#pragma once
#include <array>

// ECEF → NED coordinate transform for a fixed launch-site origin.
//
// NED = R(φ, λ) × (P_ecef − P_origin)
//
// where R is the standard ECEF-to-NED rotation matrix parameterised by
// geodetic latitude φ and longitude λ of the reference origin.
class CoordTransform {
public:
    // origin_lat_deg / lon_deg: geodetic latitude and longitude of the
    // local NED frame origin (launch site).
    CoordTransform(double origin_lat_deg, double origin_lon_deg,
                   double origin_alt_m = 0.0);

    // Convert ECEF position [m] to NED [m] relative to the origin.
    std::array<double, 3> ecef_to_ned(const double ecef[3]) const noexcept;

    // Convert ECEF velocity [m/s] to NED velocity [m/s].
    std::array<double, 3> vel_ecef_to_ned(const double vel[3]) const noexcept;

private:
    double ox_, oy_, oz_;   // origin in ECEF [m]
    double R_[3][3];        // rotation matrix rows: North, East, Down
};
