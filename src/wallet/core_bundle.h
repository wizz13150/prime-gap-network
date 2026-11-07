#pragma once
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace pgn::wallet {
struct CoreBundle { QJsonObject deployment; QString error; };

// A release supplies executables only. User data and wallet selection belong
// to the explicit setup workflow, never to this portable bundle.
inline CoreBundle readCoreBundle(const QString& applicationRoot, const QString& chain) {
    const auto reject=[](const QString& error){return CoreBundle{{},error};};
    const auto directory=QFileInfo(applicationRoot+"/Core").canonicalFilePath();
    QFile manifest(applicationRoot+"/Core/core-template.json");
    if(directory.isEmpty()||!manifest.open(QIODevice::ReadOnly)||manifest.size()>32768)
        return reject("The verified PGN Core bundle is unavailable.");
    QJsonParseError parse;
    const auto document=QJsonDocument::fromJson(manifest.readAll(),&parse);manifest.close();
    const auto bundle=document.object(),deployment=bundle.value("deployment").toObject();
    if(parse.error!=QJsonParseError::NoError||bundle.value("schema")!="PGN_CORE_BUNDLE_V1"||
       deployment.value("product")!="PGN Core"||deployment.value("chain")!=chain||
       (chain!="main"&&chain!="test")||
       (deployment.value("frontend")!="daemon"&&deployment.value("frontend")!="qt"))
        return reject("PGN Core bundle or selected network is invalid.");
    for(const auto* key:{"data_directory","approved_data_directory","wallet","create_wallet","rpc_password","rpc_user"})
        if(deployment.contains(key))return reject("A portable Core bundle must not contain wallet or user data settings.");
    static const QRegularExpression digest("^[0-9a-fA-F]{64}$"),name("^[A-Za-z0-9][A-Za-z0-9_.+-]{0,127}$");
    if(!digest.match(deployment.value("genesis_hash").toString()).hasMatch()||
       !digest.match(deployment.value("executable_sha256").toString()).hasMatch())
        return reject("Core bundle fingerprints are missing or malformed.");
    const auto rpc=deployment.value("rpc_port").toInt(),peer=deployment.value("peer_port").toInt();
    if(rpc<1024||rpc>65535||peer<1024||peer>65535||rpc==peer)
        return reject("Core bundle requires separate RPC and peer ports.");
    const auto executableName=deployment.value("executable").toString();
    if(!name.match(executableName).hasMatch())return reject("Core executable must be a file inside the bundle.");
    const auto files=bundle.value("files").toArray();
    if(files.isEmpty()||files.size()>64)return reject("Core bundle file manifest is missing or exceeds its bound.");
    QSet<QString> seen;QString executable;qint64 total=0;
    for(const auto& value:files){
        const auto entry=value.toObject();const auto fileName=entry.value("path").toString();
        const auto expected=entry.value("sha256").toString();const auto size=entry.value("bytes").toInteger(-1);
        const auto key=fileName.toCaseFolded();
        if(!name.match(fileName).hasMatch()||fileName=="core-template.json"||seen.contains(key)||
           !digest.match(expected).hasMatch()||size<=0||size>134217728||total>268435456-size)
            return reject("Core bundle contains an invalid or oversized file entry.");
        seen.insert(key);total+=size;
        const QFileInfo info(QDir(directory).filePath(fileName));
        const auto path=info.canonicalFilePath();
        if(!info.isFile()||path.isEmpty()||QFileInfo(path).absolutePath().compare(directory,Qt::CaseInsensitive)!=0||info.size()!=size)
            return reject("Core bundle file is missing, changed or outside the bundle.");
        QFile file(path);if(!file.open(QIODevice::ReadOnly))return reject("Core bundle file could not be read.");
        QCryptographicHash hash(QCryptographicHash::Sha256);
        auto remaining=size;
        while(remaining>0){
            const auto bytes=file.read(qMin<qint64>(1048576,remaining));
            if(bytes.isEmpty())return reject("Core bundle changed or could not be checked.");
            hash.addData(bytes);remaining-=bytes.size();
        }
        if(!file.atEnd())return reject("Core bundle changed while its fingerprint was being checked.");
        if(QString::fromLatin1(hash.result().toHex()).compare(expected,Qt::CaseInsensitive)!=0)
            return reject("Core bundle fingerprint mismatch; setup has not changed wallet data.");
        if(fileName==executableName){
            if(expected.compare(deployment.value("executable_sha256").toString(),Qt::CaseInsensitive)!=0)
                return reject("Core executable fingerprint disagrees with its file manifest.");
            executable=path;
        }
    }
    if(executable.isEmpty())return reject("Core executable is absent from the verified file manifest.");
    auto selected=deployment;selected.insert("executable",executable);
    return {selected,{}};
}
}
