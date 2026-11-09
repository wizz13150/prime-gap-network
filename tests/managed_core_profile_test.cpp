#include "wallet/managed_core_profile.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>
using namespace pgn::wallet;
static void need(bool value,const char* text){if(!value){std::cerr<<"FAIL "<<text<<'\n';std::exit(1);}}
static QByteArray get(const QString& path){QFile f(path);need(f.open(QIODevice::ReadOnly),"read fixture");return f.readAll();}
static void put(const QString& path,const QByteArray& bytes){QDir().mkpath(QFileInfo(path).absolutePath());QFile f(path);need(f.open(QIODevice::WriteOnly|QIODevice::Truncate),"write fixture");need(f.write(bytes)==bytes.size(),"complete fixture write");}
static QString hash(const QByteArray& bytes){return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
static QJsonObject bundle(const QString& root,const QByteArray& program,const QString& genesis){
    put(root+"/Core/pgn-core",program);
    QJsonObject deployment{{"product","PGN Core"},{"chain","test"},{"frontend","daemon"},{"executable","pgn-core"},
        {"executable_sha256",hash(program)},{"genesis_hash",genesis},{"rpc_port",30101},{"peer_port",30102}};
    put(root+"/Core/core-template.json",QJsonDocument(QJsonObject{{"schema","PGN_CORE_BUNDLE_V1"},{"deployment",deployment},
        {"files",QJsonArray{QJsonObject{{"path","pgn-core"},{"bytes",program.size()},{"sha256",hash(program)}}}}}).toJson());
    deployment.insert("executable",QFileInfo(root+"/Core/pgn-core").canonicalFilePath());return deployment;
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);QTemporaryDir temp;need(temp.isValid(),"temporary root");
    const auto oldRoot=temp.filePath("old-package"),root=temp.filePath("new-package"),runtime=temp.filePath("profile"),data=temp.filePath("wallet-data");
    QDir().mkpath(runtime);QDir().mkpath(data);
    const auto genesis=QString(64,'a');const auto old=bundle(oldRoot,"OLD INERT CORE",genesis),current=bundle(root,"NEW INERT CORE",genesis);
    need(managedCoreBundleRoot(oldRoot,root)==root,"running program bundle wins over retained configuration root");
    need(managedCoreBundleRoot(root,temp.filePath("probe-tools"))==root,"explicit test/tool bundle remains supported");
    auto profile=old;profile.insert("schema","pgn-core-deployment-3");profile.insert("wallet","alice");
    profile.insert("data_directory",data);profile.insert("data_directory_origin","user_setup");
    profile.insert("approved_data_directory",data);profile.insert("create_wallet",false);profile.insert("peers",QJsonArray{"127.0.0.1:30103"});
    const auto before=QJsonDocument(profile).toJson(QJsonDocument::Compact);
    const auto oldProfile=CoreProfile::fromJson(profile);
    const auto owner=QJsonDocument(coreOwnershipBinding(oldProfile,1234567,"fixture-birth")).toJson(QJsonDocument::Compact);
    put(data+"/wallets/alice/wallet.dat","DO NOT CHANGE WALLET");put(data+"/testnet/blocks/blk00000.dat","DO NOT CHANGE CHAIN");
    const auto wallet=get(data+"/wallets/alice/wallet.dat"),chain=get(data+"/testnet/blocks/blk00000.dat");
    const auto reset=[&]{put(runtime+"/wallet-core.json",before);put(runtime+"/wallet-owned.json",owner);};
    const auto absent=[](const CoreProfile&,const QJsonObject&){return CoreOwnerPresence::Absent;};
    const auto live=[](const CoreProfile&,const QJsonObject&){return CoreOwnerPresence::Live;};
    const auto unknown=[](const CoreProfile&,const QJsonObject&){return CoreOwnerPresence::Unknown;};
    reset();auto result=prepareManagedCoreProfile(root,runtime,"test",true,absent);
    need(result.error.isEmpty()&&result.profile.executable==current.value("executable").toString()&&result.digest==hash("NEW INERT CORE"),"old package rebind");
    const auto after=get(runtime+"/wallet-core.json");auto expected=profile;expected.insert("executable",current.value("executable"));expected.insert("executable_sha256",current.value("executable_sha256"));
    need(QJsonDocument::fromJson(after).object()==expected,"only program binding changes");
    need(!QFileInfo::exists(runtime+"/wallet-owned.json")&&!QFileInfo::exists(runtime+"/wallet-core-rebind.json"),"stopped owner and journal settled");
    result=prepareManagedCoreProfile(root,runtime,"test",true,absent);need(result.error.isEmpty()&&get(runtime+"/wallet-core.json")==after,"repeat is idempotent");
    reset();result=prepareManagedCoreProfile(root,runtime,"test",true,live);
    need(result.error.isEmpty()&&result.profile.executable==oldProfile.executable&&get(runtime+"/wallet-core.json")==before&&get(runtime+"/wallet-owned.json")==owner,"live old process is retained");
    reset();result=prepareManagedCoreProfile(root,runtime,"test",true,unknown);
    need(!result.error.isEmpty()&&get(runtime+"/wallet-core.json")==before&&get(runtime+"/wallet-owned.json")==owner,"ambiguous owner cannot authorize replacement");
    reset();put(runtime+"/wallet-pending-setup.json","user folder selection");result=prepareManagedCoreProfile(root,runtime,"test",true,absent);
    need(result.error.isEmpty()&&!result.ownedUpgradePending&&get(runtime+"/wallet-core.json")==before&&get(runtime+"/wallet-pending-setup.json")=="user folder selection","pending user folder selection retains its exact binding");QFile::remove(runtime+"/wallet-pending-setup.json");
    reset();put(runtime+"/wallet-launch-intent.json","unresolved");result=prepareManagedCoreProfile(root,runtime,"test",true,absent);
    need(!result.error.isEmpty()&&get(runtime+"/wallet-core.json")==before,"launch intent prevents duplication");QFile::remove(runtime+"/wallet-launch-intent.json");
    reset();bundle(root,"NEW INERT CORE",QString(64,'b'));result=prepareManagedCoreProfile(root,runtime,"test",true,absent);
    need(result.error.isEmpty()&&get(runtime+"/wallet-core.json")==before,"another genesis never replaces a wallet binding");bundle(root,"NEW INERT CORE",genesis);
    reset();auto external=profile;put(temp.filePath("external-node"),"EXTERNAL INERT CORE");external.insert("executable",temp.filePath("external-node"));external.insert("executable_sha256",hash("EXTERNAL INERT CORE"));
    const auto externalRaw=QJsonDocument(external).toJson(QJsonDocument::Compact);put(runtime+"/wallet-core.json",externalRaw);QFile::remove(runtime+"/wallet-owned.json");
    result=prepareManagedCoreProfile(root,runtime,"test",true,absent);need(result.error.isEmpty()&&get(runtime+"/wallet-core.json")==externalRaw,"explicit external Core is retained");
    // Simulate a crash after the profile write but before old owner retirement.
    reset();const QJsonObject journal{{"schema","PGN_MANAGED_CORE_REBIND_V1"},{"before",QString::fromLatin1(before.toBase64())},
        {"after",QString::fromLatin1(after.toBase64())},{"owner",QString::fromLatin1(owner.toBase64())}};
    put(runtime+"/wallet-core-rebind.json",QJsonDocument(journal).toJson(QJsonDocument::Compact));put(runtime+"/wallet-core.json",after);
    result=prepareManagedCoreProfile(root,runtime,"test",true,absent);
    need(result.error.isEmpty()&&get(runtime+"/wallet-core.json")==after&&!QFileInfo::exists(runtime+"/wallet-owned.json")&&!QFileInfo::exists(runtime+"/wallet-core-rebind.json"),"interrupted rebind recovered");
    need(get(data+"/wallets/alice/wallet.dat")==wallet&&get(data+"/testnet/blocks/blk00000.dat")==chain,"wallet and chain bytes retained");
    std::cout<<"PASS managed bundled-Core relocation, live/unknown owners, external Core, network mismatch, interruption recovery and wallet/chain preservation\n";
}
