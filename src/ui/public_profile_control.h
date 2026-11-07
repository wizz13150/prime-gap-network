#pragma once
#include "ui/public_profile_store.h"
#include <QJsonDocument>
#include <QJsonObject>
namespace pgn::ui {
inline QByteArray publicProfileControl(const QString& draft) {
    const auto name=canonicalPublicName(draft);
    if(!name)return {};
    return QJsonDocument(QJsonObject{{"action","public_profile"},{"name",*name}}).toJson(QJsonDocument::Compact)+'\n';
}
}
