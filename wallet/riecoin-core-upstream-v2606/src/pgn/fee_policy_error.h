// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <stdexcept>

namespace pgn::core {
/** Missing/invalid local pricing metadata or provider. Never a numeric zero,
 * peer-invalid consensus verdict or authorization to choose a fallback price. */
class FeePolicyError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
}
