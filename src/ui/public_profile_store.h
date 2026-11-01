#pragma once
#include <QSettings>
#include <QRegularExpression>
#include <optional>
namespace pgn::ui {
inline constexpr int publicNameMaximumLength = 48;
inline QRegularExpression publicNameCharacters() {
    return QRegularExpression("^[\\p{L}\\p{M}\\p{N} ._'-]{0,48}$", QRegularExpression::UseUnicodePropertiesOption);
}
inline std::optional<QString> canonicalPublicName(const QString& draft) {
    if (draft.size() > publicNameMaximumLength || !publicNameCharacters().match(draft).hasMatch()) return {};
    auto name = draft.normalized(QString::NormalizationForm_C).trimmed();
    if (name.isEmpty()) return QString("PGN");
    if (!QRegularExpression("[\\p{L}\\p{N}]").match(name).hasMatch()) return {};
    return name;
}
// Automatic and explicit saves use one synchronous, idempotent persistence path.
inline bool savePublicProfileDraft(QSettings& settings, const QString& draft) {
    const auto name = canonicalPublicName(draft);
    if (!name) return false;
    settings.setValue("public/profileDraft", draft);
    settings.setValue("public/profileName", *name);
    settings.setValue("public/profileChosen", !draft.trimmed().isEmpty());
    settings.sync();
    return settings.status() == QSettings::NoError && settings.value("public/profileName").toString() == *name && settings.value("public/profileDraft").toString() == draft;
}
}
