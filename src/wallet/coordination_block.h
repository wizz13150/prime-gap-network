#pragma once
#include <QJsonObject>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <atomic>
#include <algorithm>
#include <limits>
#include <optional>
#include <boost/multiprecision/cpp_int.hpp>

namespace pgn::wallet {
struct CoordinationBlock {QString block,hash;quint64 attempts{};};
struct CoordinationBlockResult {std::optional<CoordinationBlock> candidate;QString error;quint64 nextNonce{};};
// Construct only the Core's explicitly labelled zero-issuance coordination
// template. Run this bounded work off the GUI thread. Only submitblock and
// subsequent active-branch inspection can establish network acceptance.
inline CoordinationBlockResult coordinationBlock(const QJsonObject& work,const QString& genesis,const QString& parent,
    quint64 firstNonce=0,quint64 maximumTries=1000000,const std::atomic_bool* cancelled=nullptr){
    const auto fail=[&](const QString& error){return CoordinationBlockResult{{},error,firstNonce};};
    static const QRegularExpression hash("^[0-9a-f]{64}$"),hex("^[0-9a-f]+$");
    if(!hash.match(genesis).hasMatch()||!hash.match(parent).hasMatch()||work.value("schema")!="PGN_NATIVE112_BOOTSTRAP_TEMPLATE1"||
       work.value("genesis")!=genesis||work.value("parent")!=parent||work.value("work_verified")!=false)return fail("Verified native coordination template required");
    if(maximumTries<1||maximumTries>1000000||work.value("nonce_offset")!=82||work.value("nonce_bytes")!=8)return fail("Unsupported coordination counter layout or execution budget");
    const auto headerHex=work.value("header").toString().toLatin1(),blockHex=work.value("block").toString().toLatin1();
    const auto target=work.value("numeric_target").toString();
    if(headerHex.size()!=224||blockHex.size()<=224||blockHex.size()>262144||blockHex.size()%2||
       !hex.match(QString::fromLatin1(blockHex)).hasMatch()||!hash.match(target).hasMatch()||!blockHex.startsWith(headerHex))return fail("Invalid native header, block or numeric target");
    auto block=QByteArray::fromHex(blockHex);auto header=block.first(112);
    if(header.sliced(82,8)!=QByteArray(8,'\0'))return fail("Core template must begin with counter zero");
    // The header carries the parent digest in its native little-endian form.
    auto encodedParent=QByteArray::fromHex(parent.toLatin1());std::reverse(encodedParent.begin(),encodedParent.end());
    if(header.sliced(4,32)!=encodedParent)return fail("Template header does not match its parent identity");
    using boost::multiprecision::cpp_int;cpp_int bound=0;
    for(auto ch:target){bound<<=4;bound+=ch.isDigit()?ch.unicode()-'0':ch.unicode()-'a'+10;}
    if(bound==0)return fail("Positive coordination target required");
    auto nonce=firstNonce;
    for(quint64 attempt=0;attempt<maximumTries;++attempt){
        if(cancelled&&cancelled->load())return {{},"Coordination construction cancelled",nonce};
        for(int i=0;i<8;++i)header[82+i]=char(nonce>>(8*i));
        auto digest=QCryptographicHash::hash(QCryptographicHash::hash(header,QCryptographicHash::Sha256),QCryptographicHash::Sha256);
        cpp_int value=0;for(int i=31;i>=0;--i){value<<=8;value+=static_cast<unsigned char>(digest[i]);}
        if(value<=bound){
            block.replace(0,112,header);std::reverse(digest.begin(),digest.end());
            return {CoordinationBlock{QString::fromLatin1(block.toHex()),QString::fromLatin1(digest.toHex()),attempt+1},{},nonce};
        }
        if(nonce==std::numeric_limits<quint64>::max())return {{},"Coordination counter domain exhausted",nonce};
        ++nonce;
    }
    return {{},"Coordination execution budget reached",nonce};
}
}
