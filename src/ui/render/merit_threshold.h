#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

inline std::optional<double> FrontierNaturalLogDecimal(const std::string& decimal)
{
    if (decimal.empty() || !std::all_of(decimal.cbegin(), decimal.cend(), [](unsigned char c) {
            return c >= '0' && c <= '9';
        })) return std::nullopt;
    const std::size_t first = decimal.find_first_not_of('0');
    if (first == std::string::npos) return std::nullopt;
    const std::string canonical = decimal.substr(first);
    const std::size_t take = std::min<std::size_t>(16, canonical.size());
    const double prefix = std::stod(canonical.substr(0, take));
    const double normalized = prefix / std::pow(10.0, static_cast<double>(take - 1));
    return std::log(normalized) + static_cast<double>(canonical.size() - 1) * std::log(10.0);
}

inline std::optional<unsigned long long> FrontierGapThresholdForMerit(
    const std::string& endpoint,
    double merit)
{
    const auto logN = FrontierNaturalLogDecimal(endpoint);
    if (!logN || !std::isfinite(merit) || merit <= 0.0) return std::nullopt;
    const double threshold = std::ceil(*logN * merit);
    if (!std::isfinite(threshold) || threshold < 0.0 ||
        threshold > static_cast<double>(~0ULL)) return std::nullopt;
    return static_cast<unsigned long long>(threshold);
}
