#include "wallet/core_bundle.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>

int main(int argc,char** argv){
    QCoreApplication application(argc,argv);QTemporaryDir temp;if(!temp.isValid())return 2;
    int checks=0;const auto check=[&](bool okay,const char* label){++checks;if(!okay){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}};
    const auto root=temp.filePath("application");QDir().mkpath(root+"/Core");
    const QByteArray executable("INERT NEVER EXECUTED"),library("INERT LIBRARY");
    const auto hash=[](const QByteArray& bytes){return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());};
    for(const auto& entry:QList<QPair<QString,QByteArray>>{{"pgn-core.exe",executable},{"support.dll",library}}){QFile file(root+"/Core/"+entry.first);check(file.open(QIODevice::WriteOnly),"fixture open");file.write(entry.second);}
    QJsonObject deployment{{"product","PGN Core"},{"chain","test"},{"frontend","daemon"},{"executable","pgn-core.exe"},{"executable_sha256",hash(executable)},{"genesis_hash",QString(64,'a')},{"rpc_port",30101},{"peer_port",30102}};
    QJsonArray entries{QJsonObject{{"path","pgn-core.exe"},{"bytes",executable.size()},{"sha256",hash(executable)}},QJsonObject{{"path","support.dll"},{"bytes",library.size()},{"sha256",hash(library)}}};
    const QJsonObject valid{{"schema","PGN_CORE_BUNDLE_V1"},{"deployment",deployment},{"files",entries}};
    const auto write=[&](const QJsonObject& bundle){QFile file(root+"/Core/core-template.json");check(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"manifest write");file.write(QJsonDocument(bundle).toJson());};
    write(valid);auto result=pgn::wallet::readCoreBundle(root,"test");check(result.error.isEmpty(),"valid payload");check(QFileInfo(result.deployment.value("executable").toString()).isAbsolute(),"resolved executable");
    check(!pgn::wallet::readCoreBundle(root,"main").error.isEmpty(),"wrong network");
    auto changed=valid;changed.remove("files");write(changed);check(!pgn::wallet::readCoreBundle(root,"test").error.isEmpty(),"missing manifest files");
    for(const auto& value:QStringList{"../pgn-core.exe","C:/outside.exe","nested/pgn-core.exe"}){changed=valid;auto d=deployment;d.insert("executable",value);changed.insert("deployment",d);write(changed);check(!pgn::wallet::readCoreBundle(root,"test").error.isEmpty(),"executable escape");}
    changed=valid;auto files=entries;files.append(entries[0]);changed.insert("files",files);write(changed);check(!pgn::wallet::readCoreBundle(root,"test").error.isEmpty(),"duplicate file");
    changed=valid;auto d=deployment;d.insert("data_directory","C:/private-wallet");changed.insert("deployment",d);write(changed);check(!pgn::wallet::readCoreBundle(root,"test").error.isEmpty(),"private data setting");
    changed=valid;d=deployment;d.insert("executable_sha256",QString(64,'b'));changed.insert("deployment",d);write(changed);check(!pgn::wallet::readCoreBundle(root,"test").error.isEmpty(),"manifest disagreement");
    write(valid);QFile corrupt(root+"/Core/support.dll");check(corrupt.open(QIODevice::WriteOnly|QIODevice::Truncate),"corrupt fixture open");corrupt.write(QByteArray(library.size(),'x'));corrupt.close();check(!pgn::wallet::readCoreBundle(root,"test").error.isEmpty(),"same-size modified library");
    check(!QFileInfo::exists(root+"/data"),"no data installation");
    if(argc>1){const auto actual=pgn::wallet::readCoreBundle(QString::fromLocal8Bit(argv[1]),"test");check(actual.error.isEmpty(),"actual packaged Core and libraries");}
    std::cout<<"PASS "<<checks<<" Core bundle checks; no executable launched\n";
}
