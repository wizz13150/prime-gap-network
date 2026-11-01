#pragma once
#include <QStringView>

namespace pgn::booking {
inline constexpr int researcherNameMaxLength = 24;
// Applies to new local display-name submissions, never imported identities.
inline bool validResearcherName(QStringView name) {
    if (name.isEmpty() || name.size() > researcherNameMaxLength) return false;
    const auto alphaNumeric = [](QChar c) {
        const auto u = c.unicode();
        return (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9');
    };
    if (!alphaNumeric(name.front())) return false;
    for (const auto c : name) if (!alphaNumeric(c) && c != u'_' && c != u'-') return false;
    return true;
}
}
