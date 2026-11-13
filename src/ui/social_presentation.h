#pragma once
#include <QString>
#include <QColor>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QFontInfo>
#include <QRawFont>
#include <QVariantMap>
namespace pgn::ui {
inline QVariantMap SocialActualFont(const QFont& font){const QFontInfo info(font);const auto raw=QRawFont::fromFont(font);return {{"requested",font.families()},{"family",info.family()},{"style",info.styleName()},{"pixelSize",info.pixelSize()},{"weight",int(info.weight())},{"italic",info.italic()},{"exactMatch",info.exactMatch()},{"rawFamily",raw.familyName()},{"rawStyle",raw.styleName()}};}
// Display only. Canonical scripts remain in model roles, tooltips and actions.
inline QString SocialFingerprint(QString id){
 if(id.startsWith("0014")&&id.size()==44)id=id.mid(4);
 if(!QRegularExpression("^[0-9a-f]{8,256}$").match(id).hasMatch())id=QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(),QCryptographicHash::Sha256).toHex());
 return id;
}
inline QString SocialDisplayName(const QString& id,const QString& profile={}){
 const auto name=profile.trimmed();
 const bool technical=name==id||QRegularExpression("^[0-9a-f]{32,256}$").match(name).hasMatch();
 if(!name.isEmpty()&&!technical)return name.left(80);
 const auto fingerprint=SocialFingerprint(id);
 return "Miner "+fingerprint.left(4)+QString::fromUtf8("…")+fingerprint.right(4);
}
inline QString SocialAvatarInitials(const QString& id,const QString& profile={}){
 auto name=SocialDisplayName(id,profile);
 if(name==SocialDisplayName(id))return SocialFingerprint(id).left(2).toUpper();
 name.remove(QRegularExpression(" · [0-9a-f]{8}…[0-9a-f]{6}$"));
 const auto words=name.split(' ',Qt::SkipEmptyParts);return (words.value(0).left(1)+(words.size()>1?words.last().left(1):QString{})).toUpper();
}
inline QColor SocialAvatarColor(const QString& id){
 const QColor palette[]{QColor("#b4cfcc"),QColor("#b8cadb"),QColor("#c9c4da"),QColor("#d6c6b3"),QColor("#bad0bc"),QColor("#d4bdc9")};
 const auto digest=QCryptographicHash::hash(id.toUtf8(),QCryptographicHash::Sha256);return palette[static_cast<quint8>(digest[0])%6];
}
}

