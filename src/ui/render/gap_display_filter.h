#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace pgn::ui {
// Presentation only. Never feed this value back to telemetry, counters, history,
// position/test rates or source age. Time advances only with producer samples.
class GapDisplayFilter {
public:
    explicit GapDisplayFilter(double timeConstantSeconds = 2.0)
        : m_timeConstant(timeConstantSeconds) {}

    std::optional<double> observe(std::optional<double> raw,
                                  std::string_view sourceIdentity,
                                  std::optional<std::uint64_t> sourceNs) {
        if (!raw || !std::isfinite(*raw) || *raw < 0.0) {
            reset();
            return std::nullopt;
        }
        if (sourceIdentity.empty() || !sourceNs ||
            !std::isfinite(m_timeConstant) || m_timeConstant <= 0.0) {
            reset();
            return raw;
        }
        // A restart, clock regression or corrected same-time observation starts
        // a fresh display series. Repainting the same sample never changes it.
        if (!m_value || m_identity != sourceIdentity || *sourceNs < m_time ||
            (*sourceNs == m_time && *raw != m_raw)) {
            m_identity = sourceIdentity;
            m_value = raw;
        } else if (*sourceNs > m_time) {
            const double seconds = static_cast<double>(*sourceNs - m_time) / 1.0e9;
            const double alpha = -std::expm1(-seconds / m_timeConstant);
            m_value = std::lerp(*m_value, *raw, alpha);
        }
        m_time = *sourceNs;
        m_raw = *raw;
        return m_value;
    }

private:
    void reset() { m_value.reset(); m_identity.clear(); }
    double m_timeConstant;
    std::optional<double> m_value;
    std::string m_identity;
    std::uint64_t m_time{};
    double m_raw{};
};
}
