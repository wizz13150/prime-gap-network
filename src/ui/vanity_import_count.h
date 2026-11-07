#pragma once
#include <QSet>
#include <QString>
namespace pgn::ui {
// Caller supplies canonical public addresses after native key inspection and
// invokes this only for a successful Core result. No key or codec policy here.
inline bool rememberVanityImportedAddress(QSet<QString>& addresses,const QString& address){
    if(address.isEmpty()||addresses.contains(address))return false;
    addresses.insert(address);return true;
}
}
