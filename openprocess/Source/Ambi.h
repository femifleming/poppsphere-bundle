// Ambisonics for opensphere: ACN channel order, SN3D normalisation (AmbiX), no Condon-Shortley
// phase, up to 7th order (64 channels). x to the front, y to the left, z up; azimuth turns to the
// left, elevation goes up - as the IEM and SPARTA plug-ins have it. (Same maths as Leitner's.)
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace ambi
{
constexpr int kMaxOrder = 7;
constexpr int kCh = 64;
using Coefs = std::array<float, kCh>;

constexpr int chFor (int order) noexcept { return (order + 1) * (order + 1); }
inline int orderFor (int channels) noexcept
{
    int o = 0;
    while (o < kMaxOrder && chFor (o + 1) <= channels) ++o;
    return o;
}
inline int orderOf (int acn) noexcept { int l = 0; while ((l + 1) * (l + 1) <= acn) ++l; return l; }

inline const std::array<std::array<double, kMaxOrder + 1>, kMaxOrder + 1>& sn3d()
{
    static const auto t = []
    {
        std::array<std::array<double, kMaxOrder + 1>, kMaxOrder + 1> n {};
        auto fact = [] (int k) { double f = 1.0; for (int i = 2; i <= k; ++i) f *= i; return f; };
        for (int l = 0; l <= kMaxOrder; ++l)
            for (int m = 0; m <= l; ++m)
                n[(size_t) l][(size_t) m] = std::sqrt ((m == 0 ? 1.0 : 2.0) * fact (l - m) / fact (l + m));
        return n;
    }();
    return t;
}

// SN3D real spherical harmonics of the direction (azimuth, elevation in radians), ACN order
inline Coefs sh (float az, float el, int order) noexcept
{
    order = std::clamp (order, 0, kMaxOrder);
    Coefs y {};
    const double x = std::cos (el) * std::cos (az), yy = std::cos (el) * std::sin (az), z = std::sin (el);
    const auto& N = sn3d();
    double cm = 1.0, sm = 0.0, qmm = 1.0;
    for (int m = 0; m <= order; ++m)
    {
        if (m > 0)
        {
            const double c = cm * x - sm * yy, s = cm * yy + sm * x;
            cm = c; sm = s;
            qmm *= (2.0 * m - 1.0);
        }
        double q2 = 0.0, q1 = qmm;
        for (int l = m; l <= order; ++l)
        {
            double q;
            if (l == m) q = qmm;
            else if (l == m + 1) q = z * (2.0 * m + 1.0) * qmm;
            else q = ((2.0 * l - 1.0) * z * q1 - (l + m - 1.0) * q2) / (double) (l - m);
            if (l > m) { q2 = q1; q1 = q; }
            const double nq = N[(size_t) l][(size_t) m] * q;
            y[(size_t) (l * l + l + m)] = (float) (nq * cm);
            if (m > 0) y[(size_t) (l * l + l - m)] = (float) (nq * sm);
        }
    }
    return y;
}
} // namespace ambi
