#pragma once
#include <QRegularExpression>
#include <QStandardPaths>
#include <QString>
namespace pgn::update {
// Short directory locators keep Windows runtime/library paths usable. The
// complete network/root/manifest digests remain verified in their receipts.
inline QString updateStateDirectory(const QString& genesis){
 if(!QRegularExpression("^[0-9a-f]{64}$").match(genesis).hasMatch())return {};
 const auto base=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
 return base.isEmpty()?QString{}:base+"/updates/"+genesis.left(20);
}
}
