// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <cstdint>
#include <limits>

namespace pgn::core {
// ROOT535: approved monetary unit and ceiling, NOT an issuance curve, fee
// policy, genesis, selected chain or permission to launch a network.
inline constexpr uint32_t PGN_DECIMAL_PLACES = 10;
inline constexpr int64_t PGN_ATOMIC_UNITS_PER_COIN = 10000000000LL;
inline constexpr int64_t PGN_MAX_SUPPLY_COINS = 100000000LL;
static_assert(PGN_MAX_SUPPLY_COINS <=
              std::numeric_limits<int64_t>::max() / PGN_ATOMIC_UNITS_PER_COIN);
inline constexpr int64_t PGN_MAX_SUPPLY_ATOMIC =
    PGN_MAX_SUPPLY_COINS * PGN_ATOMIC_UNITS_PER_COIN;
} // namespace pgn::core

namespace pgn::reference {
// Only historical RIC golden data/reference arithmetic. Never PGN defaults,
// selected parameters or authority to access an existing RIC wallet/network.
inline constexpr int64_t RIC_ATOMIC_UNITS_PER_COIN = 100000000;
inline constexpr int64_t RIC_MAX_ATOMIC = 84000000LL * RIC_ATOMIC_UNITS_PER_COIN;
} // namespace pgn::reference
