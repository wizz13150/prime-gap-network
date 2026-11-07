#pragma once
#include <QCryptographicHash>
#include <QString>
#include <array>

namespace pgn::wallet {
// Public encoding validation only; protected-key ownership is checked by Core.
inline bool isCanonicalTestnetKeyHashAlias(const QString& address) {
    if(address.size()!=37 || !address.startsWith("tpgn"))return false;
    const QString alphabet="123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    std::array<unsigned char,28> bytes{};
    for(const auto c:address){
        const int digit=int(alphabet.indexOf(c));if(digit<0)return false;
        unsigned carry=unsigned(digit);
        for(int i=27;i>=0;--i){carry+=unsigned(bytes[size_t(i)])*58;bytes[size_t(i)]=static_cast<unsigned char>(carry&255);carry>>=8;}
        if(carry)return false;
    }
    if(bytes[0]!=1 || bytes[1]!=127 || bytes[2]!=109 || bytes[3]!=171)return false;
    const QByteArray payload(reinterpret_cast<const char*>(bytes.data()),24);
    const auto checksum=QCryptographicHash::hash(QCryptographicHash::hash(payload,QCryptographicHash::Sha256),QCryptographicHash::Sha256);
    for(unsigned i=0;i<4;++i)if(bytes[24+i]!=static_cast<unsigned char>(checksum[int(i)]))return false;
    return true;
}
}
