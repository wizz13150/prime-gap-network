#pragma once
#include "wallet/core_bundle.h"
#include "wallet/core_profile.h"
#include <QSaveFile>
#include <functional>

namespace pgn::wallet {
enum class CoreOwnerPresence {Absent,Live,Unknown};
using CoreOwnerProbe=std::function<CoreOwnerPresence(const CoreProfile&,const QJsonObject&)>;
struct CoreProfilePreparation {CoreProfile profile;QString digest,error,notice;bool ownedUpgradePending{};};
namespace managed_core_detail {
inline QByteArray read(const QString& path,qint64 limit,bool* okay){
    QFile file(path);*okay=!file.exists();
    if(*okay)return {};
    if(!file.open(QIODevice::ReadOnly)||file.size()>limit)return {};
    const auto bytes=file.read(limit+1);*okay=bytes.size()<=limit&&file.error()==QFile::NoError;return bytes;
}
inline bool write(const QString& path,const QByteArray& bytes){
    QSaveFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();
}
inline QJsonObject object(const QByteArray& bytes){
    QJsonParseError parse;const auto doc=QJsonDocument::fromJson(bytes,&parse);
    return parse.error==QJsonParseError::NoError&&doc.isObject()?doc.object():QJsonObject{};
}
// A saved explicit external Core remains explicit. Automatic replacement is
// limited to a program authenticated by its previous complete portable bundle.
inline bool previousBundle(const CoreProfile& profile){
    const auto directory=QFileInfo(profile.executable).absolutePath();
    if(QFileInfo(directory).fileName().compare("Core",Qt::CaseInsensitive)!=0)return false;
    const auto old=readCoreBundle(QFileInfo(directory).absolutePath(),profile.chain);
    return old.error.isEmpty()&&old.deployment.value("executable").toString()==QFileInfo(profile.executable).canonicalFilePath()&&
        old.deployment.value("executable_sha256").toString().compare(profile.executableSha256,Qt::CaseInsensitive)==0&&
        old.deployment.value("genesis_hash").toString().compare(profile.genesisHash,Qt::CaseInsensitive)==0;
}
inline QJsonObject replacement(const QJsonObject& original,const QJsonObject& bundled){
    const auto profile=CoreProfile::fromJson(original);
    if(bundled.value("chain")!=profile.chain||bundled.value("frontend")!=profile.frontend||
       bundled.value("genesis_hash").toString().compare(profile.genesisHash,Qt::CaseInsensitive)!=0)return {};
    auto next=original;next.insert("executable",bundled.value("executable"));
    next.insert("executable_sha256",bundled.value("executable_sha256"));return next;
}
}
// Caller owns wallet-owned.lock. Runs outside the GUI thread. It never stops
// a process, creates a wallet, changes network settings or moves user data.
inline CoreProfilePreparation prepareManagedCoreProfile(const QString& root,const QString& runtime,
    const QString& chain,bool allowChange,const CoreOwnerProbe& probe)
{
    using namespace managed_core_detail;
    CoreProfilePreparation result;
    const auto reject=[&](const QString& error){result.error=error;return result;};
    const auto profilePath=runtime+"/wallet-core.json",ownerPath=runtime+"/wallet-owned.json";
    const auto journalPath=runtime+"/wallet-core-rebind.json";
    bool okay{};auto raw=read(profilePath,32768,&okay);
    if(!okay||raw.isEmpty())return reject("Saved Core configuration could not be read");
    auto original=object(raw);result.profile=CoreProfile::fromJson(original);
    if(result.profile.chain!=chain)return reject("Saved Core configuration belongs to another network");
    bool journalOkay{};const auto journalRaw=read(journalPath,114688,&journalOkay);
    if(!journalOkay)return reject("Pending Core relocation could not be read");
    QByteArray before=raw,after,ownerRaw;
    if(!journalRaw.isEmpty()){
        const auto journal=object(journalRaw);
        if(journal.value("schema")!="PGN_MANAGED_CORE_REBIND_V1")return reject("Invalid pending Core relocation");
        before=QByteArray::fromBase64(journal.value("before").toString().toLatin1());
        after=QByteArray::fromBase64(journal.value("after").toString().toLatin1());
        ownerRaw=QByteArray::fromBase64(journal.value("owner").toString().toLatin1());
        if(before.size()>32768||after.size()>32768||ownerRaw.size()>8192||(raw!=before&&raw!=after))
            return reject("Core configuration changed during pending relocation");
        original=object(before);result.profile=CoreProfile::fromJson(original);
        const auto bundled=readCoreBundle(root,chain);
        const auto next=replacement(original,bundled.deployment);
        if(!bundled.error.isEmpty()||next.isEmpty()||QJsonDocument(next).toJson(QJsonDocument::Compact)!=after)
            return reject("Pending Core relocation does not match this verified bundle");
    }else if(allowChange&&!QFileInfo::exists(runtime+"/wallet-pending-setup.json")){
        // A user-approved folder change binds the exact old profile bytes.
        // Let that existing transaction finish before relocating its program.
        const auto bundled=readCoreBundle(root,chain);
        if(bundled.error.isEmpty()){
            const auto next=replacement(original,bundled.deployment);
            const bool same=next==original;
            const bool samePath=QFileInfo(result.profile.executable).canonicalFilePath()==bundled.deployment.value("executable").toString();
            if(!next.isEmpty()&&!same&&(samePath||previousBundle(result.profile))){
                bool ownerOkay{};ownerRaw=read(ownerPath,8192,&ownerOkay);
                if(!ownerOkay)return reject("Saved Core ownership could not be read");
                if(QFileInfo::exists(runtime+"/wallet-launch-intent.json"))
                    return reject("An unresolved Core launch must be settled before changing its program");
                const auto presence=ownerRaw.isEmpty()?CoreOwnerPresence::Absent:probe(result.profile,object(ownerRaw));
                if(presence==CoreOwnerPresence::Unknown)return reject("Previous Core ownership is ambiguous; program unchanged");
                if(presence==CoreOwnerPresence::Live){
                    result.ownedUpgradePending=true;
                    result.notice="An earlier owned Core is running; the bundled program will be adopted after its normal shutdown";
                }else after=QJsonDocument(next).toJson(QJsonDocument::Compact);
            }
        }
    }
    if(!after.isEmpty()){
        const auto nextProfile=CoreProfile::fromJson(object(after));
        const auto nextError=nextProfile.validate(root);if(!nextError.isEmpty())return reject(nextError);
        const auto oldProfile=CoreProfile::fromJson(original);
        if(!ownerRaw.isEmpty()&&probe(oldProfile,object(ownerRaw))!=CoreOwnerPresence::Absent)
            return reject("Previous Core is not confirmed stopped; relocation retained for recovery");
        bool currentOkay{};const auto currentOwner=read(ownerPath,8192,&currentOkay);
        if(!currentOkay||(!currentOwner.isEmpty()&&currentOwner!=ownerRaw))return reject("Core ownership changed during relocation");
        const QJsonObject intent{{"schema","PGN_MANAGED_CORE_REBIND_V1"},{"before",QString::fromLatin1(before.toBase64())},
            {"after",QString::fromLatin1(after.toBase64())},{"owner",QString::fromLatin1(ownerRaw.toBase64())}};
        if(journalRaw.isEmpty()&&!write(journalPath,QJsonDocument(intent).toJson(QJsonDocument::Compact)))
            return reject("Core relocation could not save its recovery record");
        bool stillOkay{};const auto still=read(profilePath,32768,&stillOkay);
        if(!stillOkay||(still!=before&&still!=after))return reject("Core configuration changed before relocation");
        if(still!=after&&!write(profilePath,after))return reject("Core relocation could not save the new program binding");
        if(!currentOwner.isEmpty()&&!QFile::remove(ownerPath))return reject("Stopped Core ownership could not be retired");
        if(!QFile::remove(journalPath))return reject("Core relocation recovery record could not be settled");
        result.profile=CoreProfile::fromJson(object(after));result.notice="Rebuilt bundled Core selected; wallet and chain data retained";
    }
    const auto invalid=result.profile.validate(root);if(!invalid.isEmpty())return reject(invalid);
    QFile executable(result.profile.executable);if(!executable.open(QIODevice::ReadOnly))return reject("Pinned Core program could not be read");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!hash.addData(&executable))return reject("Pinned Core program could not be checked");
    result.digest=QString::fromLatin1(hash.result().toHex());
    return result;
}
}
