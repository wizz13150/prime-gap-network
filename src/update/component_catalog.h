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
#include <QUrl>

namespace pgn::update {
// Read-only discovery. No manifest or release response grants installation authority.
inline bool componentId(const QString& id){return QSet<QString>{"ui","core","gpu","cpu"}.contains(id);}
inline bool digest(const QString& value){return QRegularExpression("^[0-9a-f]{64}$").match(value).hasMatch();}
inline bool versionText(const QString& value){return !value.isEmpty()&&value.size()<=96&&QRegularExpression("^[A-Za-z0-9][A-Za-z0-9.+_-]*$").match(value).hasMatch();}
inline bool relativeProgramPath(const QString& path){
    if(path.isEmpty()||path.size()>512||path.contains('\\')||path.contains(':')||QDir::isAbsolutePath(path))return false;
    const auto parts=path.split('/');
    for(const auto& part:parts){
        if(part.isEmpty()||part=="."||part==".."||part.contains(QChar(0)))return false;
        const auto lowered=part.toLower();if(lowered=="wallets"||lowered=="wallet.dat"||lowered==".cookie")return false;
    }
    const auto suffix=QFileInfo(path).suffix().toLower();
    if(QSet<QString>{"sqlite","db","key","log","wal","shm"}.contains(suffix))return false;
    return true;
}
// The caller supplies a code-only installation root, never the wallet/data directory.
// File hashing belongs in a worker task; this function does no network or mutation.
inline QJsonObject inspectComponent(const QString& root,const QJsonObject& declaration,const QString& expectedPrimary={}){
    QJsonObject result{{"id",declaration.value("id")},{"state","unidentified"},{"version",QJsonValue{}},
                       {"reason","Component has no matching local build manifest"}};
    const auto id=declaration.value("id").toString(),version=declaration.value("version").toString();
    const auto files=declaration.value("files").toArray();const auto base=QFileInfo(root).canonicalFilePath();
    if(!componentId(id)||!versionText(version)||base.isEmpty()||files.isEmpty()||files.size()>512)return result;
    QSet<QString> seen,declaredFiles;qint64 total=0;
    for(const auto& value:files){
        const auto entry=value.toObject();const auto path=entry.value("path").toString(),expected=entry.value("sha256").toString();
        if(!relativeProgramPath(path)||!digest(expected)||seen.contains(path))return result;seen.insert(path);
        const QFileInfo info(QDir(base).filePath(path));const auto actualPath=info.canonicalFilePath();
        if(info.isSymLink()||actualPath.isEmpty()||!actualPath.startsWith(base+'/')||!info.isFile()||info.size()>512LL*1024*1024)return result;
        declaredFiles.insert(actualPath);
        total+=info.size();if(total>1024LL*1024*1024)return result;
        QFile file(actualPath);if(!file.open(QIODevice::ReadOnly))return result;
        QCryptographicHash hash(QCryptographicHash::Sha256);if(!hash.addData(&file))return result;
        if(QString::fromLatin1(hash.result().toHex())!=expected){result.insert("reason","Installed files differ from the declared build");return result;}
    }
    if(!expectedPrimary.isEmpty()){
        const auto primary=QFileInfo(expectedPrimary).canonicalFilePath();
        if(primary.isEmpty()||!QFileInfo(expectedPrimary).isAbsolute()||!declaredFiles.contains(primary)){
            result.insert("reason","Active/configured primary executable is not declared by this manifest");return result;
        }
    }
    result.insert("state","matched_local_manifest");result.insert("version",version);
    result.insert("primary_bound",!expectedPrimary.isEmpty());
    result.insert("reason","Declared version matches all declared code file digests; publisher/process attestation is separate");
    return result;
}

inline QJsonObject publishedPackages(const QByteArray& bytes,bool includePrereleases){
    QJsonObject result{{"schema","PGN_PUBLISHED_PACKAGE_CATALOG_V1"},{"packages",QJsonArray{}},
                       {"component_versions_available",false},{"installation_authorized",false}};
    if(bytes.size()>1024*1024){result.insert("error","Release response exceeds its byte budget");return result;}
    QJsonParseError error;const auto doc=QJsonDocument::fromJson(bytes,&error);
    if(error.error!=QJsonParseError::NoError||!doc.isArray()||doc.array().size()>100){result.insert("error","Invalid release catalog");return result;}
    QJsonArray packages;
    for(const auto& value:doc.array()){
        const auto release=value.toObject();const auto tag=release.value("tag_name").toString();
        if(!release.value("draft").isBool()||!release.value("prerelease").isBool()||release.value("draft").toBool()
           ||(!includePrereleases&&release.value("prerelease").toBool())||!versionText(tag))continue;
        const auto url=QUrl(release.value("html_url").toString());
        if(url.scheme()!="https"||url.host()!="github.com"||url.path()!="/wizz13150/prime-gap-network/releases/tag/"+tag)continue;
        QJsonArray assets;
        for(const auto& assetValue:release.value("assets").toArray()){
            const auto asset=assetValue.toObject();const auto name=asset.value("name").toString();
            const auto download=QUrl(asset.value("browser_download_url").toString());
            const auto sha=asset.value("digest").toString();const auto size=asset.value("size").toInteger(-1);
            if(!relativeProgramPath(name)||name.contains('/')||download.scheme()!="https"||download.host()!="github.com"
               ||download.path()!="/wizz13150/prime-gap-network/releases/download/"+tag+'/'+name
               ||!sha.startsWith("sha256:")||!digest(sha.mid(7))||size<0||size>512LL*1024*1024)continue;
            assets.append(QJsonObject{{"name",name},{"bytes",size},{"sha256",sha.mid(7)},{"url",download.toString()}});
        }
        packages.append(QJsonObject{{"version",tag},{"prerelease",release.value("prerelease")},
                                    {"url",url.toString()},{"assets",assets}});
    }
    result.insert("packages",packages);
    // A whole-package tag cannot be invented as a per-component version.
    return result;
}
}
