#pragma once
#include <QPointF>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pgn::ui {
// Pure, bounded motion: no random generator, allocation, or source mutation in
// the frame loop. A new passage gets a new seed instead of replaying a spline.
inline double flowSeed(std::uint32_t key)
{
    key ^= key >> 16; key *= 0x7feb352dU;
    key ^= key >> 15; key *= 0x846ca68bU; key ^= key >> 16;
    return double(key) / 4294967295.;
}
inline double flowNoise(double seed, double time)
{
    const auto cell = std::int64_t(std::floor(time));
    const auto key = std::uint32_t(seed * 1000003.);
    const double u = time - double(cell), smooth = u*u*(3.-2.*u);
    return std::lerp(flowSeed(key + std::uint32_t(cell)),
                     flowSeed(key + std::uint32_t(cell + 1)), smooth) * 2. - 1.;
}
inline QPointF livingFlowPoint(QPointF from, QPointF to, double phase,
                               double seed, double seconds, double freedom)
{
    phase = std::clamp(phase, 0., 1.);
    if (phase == 0.) return from;
    if (phase == 1.) return to;
    constexpr double pi = 3.141592653589793;
    const QPointF delta = to - from;
    const double distance = std::hypot(delta.x(), delta.y());
    if (distance < .001) return to;
    const QPointF tangent = delta / distance;
    const QPointF normal(-tangent.y(), tangent.x());
    const double envelope = std::sin(pi * phase);
    const double hesitation = .075 * std::sin(phase * 2*pi + seed * 8.) * envelope;
    const double u = std::clamp(phase + hesitation, 0., 1.);
    const double drift = flowNoise(seed, seconds*.8 + seed*31.);
    const double curl = std::sin(phase*(5. + seed*7.) + seconds*(.4 + seed));
    const double amplitude = std::min(freedom, distance*.38) * envelope;
    return from + delta*(u*u*(3.-2.*u)) + normal*(amplitude*(drift*.65 + curl*.35))
           + tangent*(amplitude*.16*flowNoise(seed+.37, seconds*1.1));
}
}
