// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <util/moneystr.h>

#include <consensus/amount.h>
#include <tinyformat.h>
#include <util/strencodings.h>
#include <util/string.h>

#include <cstdint>
#include <optional>

using util::ContainsNoNUL;
using util::TrimString;

std::string FormatMoneyFixed(const CAmount n)
{
    // Avoid signed -INT64_MIN; magnitude is representable in uint64.
    const uint64_t magnitude = n < 0 ? uint64_t(-(n + 1)) + 1 : uint64_t(n);
    std::string fraction = std::to_string(magnitude % uint64_t(COIN));
    fraction.insert(0, pgn::core::PGN_DECIMAL_PLACES - fraction.size(), '0');
    return (n < 0 ? "-" : "") + std::to_string(magnitude / uint64_t(COIN)) + "." + fraction;
}

std::string FormatMoney(const CAmount n)
{
    std::string str = FormatMoneyFixed(n);
    const auto point = str.find('.');
    // Keep inherited CLI minimum2fraction places, without losing any atoms.
    while (str.back() == '0' && str.size() - point - 1 > 2) str.pop_back();
    return str;
}

std::optional<CAmount> ParseMoney(const std::string& money_string)
{
    if (!ContainsNoNUL(money_string)) {
        return std::nullopt;
    }
    const std::string str = TrimString(money_string);
    if (str.empty()) {
        return std::nullopt;
    }

    std::string strWhole;
    int64_t nUnits = 0;
    bool saw_digit{false};
    const char* p = str.c_str();
    for (; *p; p++)
    {
        if (*p == '.')
        {
            p++;
            int64_t nMult = COIN / 10;
            while (IsDigit(*p) && (nMult > 0))
            {
                saw_digit = true;
                nUnits += nMult * (*p++ - '0');
                nMult /= 10;
            }
            break;
        }
        if (IsSpace(*p))
            return std::nullopt;
        if (!IsDigit(*p))
            return std::nullopt;
        saw_digit = true;
        strWhole.insert(strWhole.end(), *p);
    }
    if (*p || !saw_digit) {
        return std::nullopt;
    }
    if (strWhole.size() > 10) // guard against 63 bit overflow
        return std::nullopt;
    if (nUnits < 0 || nUnits > COIN)
        return std::nullopt;
    int64_t nWhole = LocaleIndependentAtoi<int64_t>(strWhole);
    // Widening the approved unit to1e10 makes the old10-digit-whole check
    // insufficient. Establish the cap BEFORE multiplication/addition.
    if (nWhole < 0 || nUnits > MAX_MONEY || nWhole > (MAX_MONEY - nUnits) / COIN)
        return std::nullopt;
    CAmount value = nWhole * COIN + nUnits;

    if (!MoneyRange(value)) {
        return std::nullopt;
    }

    return value;
}
