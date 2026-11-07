#pragma once
#include <pgn/currency_constants.h>
#include <string_view>
namespace pgn::wallet {
inline constexpr int CurrencyDecimalPlaces=int(core::PGN_DECIMAL_PLACES);
inline constexpr auto AtomicUnitsPerCoin=core::PGN_ATOMIC_UNITS_PER_COIN;
inline constexpr const char* currencyTicker(bool testnet){return testnet?"tPGN":"PGN";}
inline constexpr const char* currencyTickerForChain(std::string_view chain){
    if(chain=="main")return "PGN";
    if(chain=="test"||chain=="testnet"||chain=="regtest"||chain=="signet")return "tPGN";
    return ""; // No mainnet denomination inferred for an unknown network.
}
}
