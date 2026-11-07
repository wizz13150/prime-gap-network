#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <algorithm>

namespace pgn::wallet {
inline bool canonicalHex(const QString& text, int maximum) {
    return !text.isEmpty() && text.size() <= maximum && text.size() % 2 == 0 &&
        std::all_of(text.begin(), text.end(), [](QChar c) {
            const auto value = c.unicode();
            return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
        });
}

// Validate public identities without expanding a large repeated regular expression.
inline QString validateParticipantSnapshot(const QJsonObject& state, const QString& genesis) {
    const auto tip = state.value("bestblockhash").toString();
    const auto entries = state.value("participants").toArray();
    if (state.value("genesis") != genesis || tip.size() != 64 || !canonicalHex(tip, 64) ||
        !state.value("participants").isArray() || entries.size() > 100 ||
        !state.value("height").isDouble() || state.value("height").toInteger(-1) < 0 ||
        !state.value("total").isDouble() || state.value("total").toInteger(-1) < entries.size())
        return "Participant snapshot identity or size invalid";
    QSet<QString> scripts;
    for (const auto& entry : entries) {
        const auto row = entry.toObject();
        const auto identity = row.value("operator_id").toString();
        const auto script = row.value("signing_script").toString();
        if (!entry.isObject() || identity.size() != 64 || !canonicalHex(identity, 64) ||
            identity == QString(64, '0') || !canonicalHex(script, 20000) ||
            !row.value("may_audit").isBool() || scripts.contains(script))
            return "Participant entry is not a canonical public identity";
        scripts.insert(script);
    }
    return {};
}
}
