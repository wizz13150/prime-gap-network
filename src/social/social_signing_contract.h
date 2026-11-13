#pragma once
#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
namespace pgn::social {
struct SigningContext {QString genesis,script,address;};
enum class EnvelopeUse { NewSignature, StoredVerification };
inline QByteArray canonical(const QJsonObject& object){return QJsonDocument(object).toJson(QJsonDocument::Compact);}
inline bool hexField(const QString& value,int length){return value.size()==length&&QRegularExpression("^[0-9a-f]+$").match(value).hasMatch();}
inline QString signingRequestError(const SigningContext& context,const QByteArray& message,qint64 now=QDateTime::currentMSecsSinceEpoch(),EnvelopeUse use=EnvelopeUse::NewSignature){
    if(!hexField(context.genesis,64)||!hexField(context.script,44)||!context.script.startsWith("0014")||context.address.isEmpty()||context.address.size()>100)return "Verified native P2WPKH signing context required.";
    if(message.size()>24576||message.size()<64)return "Social signing payload exceeds its bound.";
    const int split=message.indexOf('\n');if(split<1)return "Social signing domain required.";
    const auto domain=message.left(split);const auto bytes=message.mid(split+1);
    const bool event=domain=="PGN/SOCIAL/EVENT/1",peer=domain=="PGN/SOCIAL/PEER/1",receipt=domain=="PGN/SOCIAL/RECEIPT/1",roster=domain=="PGN/SOCIAL/ROSTER-KEY/1";
    if(!event&&!peer&&!receipt&&!roster)return "Social signing domain is not allowed.";
    QJsonParseError parse;const auto document=QJsonDocument::fromJson(bytes,&parse);
    if(parse.error!=QJsonParseError::NoError||!document.isObject()||canonical(document.object())!=bytes)return "One canonical UTF8 Social document required.";
    const auto object=document.object();
    QSet<QString> expected{"v","genesis","origin","sender_script","signing_address","device","created_ms","nonce"};
    if(!roster)expected.insert("channel");
    if(event)expected.unite({"kind","parents","bbcode_b64","event_id"});
    if(peer||roster)expected.insert("expires_ms");
    if(receipt)expected.unite({"event_id","recipient_script"});
    if(roster)expected.insert("roster_hash");
    const auto keys=object.keys();if(QSet<QString>(keys.begin(),keys.end())!=expected)return "Unexpected Social signing fields.";
    for(auto it=object.begin();it!=object.end();++it)if(it.value().isString())for(const auto c:it.value().toString())if(c.unicode()>127||c.unicode()<32)return "Canonical envelope fields must be ASCII; BBCode uses UTF8/base64.";
    if(object.value("v").toInteger()!=1||object.value("v").toDouble()!=1||object.value("genesis").toString()!=context.genesis||object.value("origin").toString()!="pgn-social://"+context.genesis||object.value("sender_script").toString()!=context.script||object.value("signing_address").toString()!=context.address)return "Social signer identity or genesis mismatch.";
    if(!hexField(object.value("device").toString(),32)||!hexField(object.value("nonce").toString(),64))return "Invalid Social device or nonce.";
    if(!roster&&!QRegularExpression("^[a-z0-9][a-z0-9/_-]{0,63}$").match(object.value("channel").toString()).hasMatch())return "Invalid channel ID.";
    const auto time=object.value("created_ms").toInteger(-1);
    if(time<0||time>9007199254740991LL||object.value("created_ms").toDouble()!=double(time))return "Social timestamp encoding is invalid.";
    // A stored chat signature authenticates content, not a freshness claim.
    // Auth challenges remain short-lived even when invoked by a stored verifier.
    if(((!event&&!receipt)||use==EnvelopeUse::NewSignature)&&(time<now-(event?7*86400000LL:60000LL)||time>now+(event?60000:1000)))return "Social signing/authentication time is invalid.";
    if(peer||roster){const auto expiry=object.value("expires_ms").toInteger(-1);if(expiry<now||expiry>time+60000||object.value("expires_ms").toDouble()!=double(expiry))return "Social authentication challenge expired.";}
    if(event){
        if(object.value("kind").toString()!="chat"||!object.value("parents").isArray())return "Unsupported Social event.";
        const auto parents=object.value("parents").toArray();QSet<QString> seen;
        if(parents.size()>4)return "At most four causal parents are supported.";
        for(const auto parent:parents){const auto id=parent.toString();if(!hexField(id,64)||seen.contains(id))return "Invalid causal parent.";seen.insert(id);}
        const auto encoded=object.value("bbcode_b64").toString().toLatin1();const auto raw=QByteArray::fromBase64(encoded,QByteArray::AbortOnBase64DecodingErrors);
        const auto source=QString::fromUtf8(raw);
        if(encoded.size()>16000||raw.toBase64()!=encoded||source.toUtf8()!=raw||source.trimmed().isEmpty()||source.size()>4000)return "Invalid canonical BBCode source.";
        auto payload=object;payload.remove("event_id");
        const auto expectedId=QCryptographicHash::hash("PGN/SOCIAL/EVENT-ID/1\n"+canonical(payload),QCryptographicHash::Sha256).toHex();
        if(object.value("event_id").toString().toLatin1()!=expectedId)return "Social event ID does not match its content.";
    }
    if(receipt&&(!hexField(object.value("event_id").toString(),64)||!hexField(object.value("recipient_script").toString(),44)||!object.value("recipient_script").toString().startsWith("0014")))return "Invalid peer receipt target.";
    if(roster&&!hexField(object.value("roster_hash").toString(),64))return "Invalid moderator roster reference.";
    return {};
}
} // namespace pgn::social

