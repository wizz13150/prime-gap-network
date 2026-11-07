#include "update/component_catalog.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>

int main(int argc,char** argv){
    QCoreApplication app(argc,argv);QTemporaryDir temporary;if(!temporary.isValid())return 1;
    const auto code=temporary.filePath("code");QDir().mkpath(code+"/bin");
    QFile file(code+"/bin/ui.bin");if(!file.open(QIODevice::WriteOnly))return 2;file.write("inert code fixture");file.close();
    const auto sha=QString::fromLatin1(QCryptographicHash::hash("inert code fixture",QCryptographicHash::Sha256).toHex());
    const QJsonObject declaration{{"id","ui"},{"version","0.1.0-dev"},
        {"files",QJsonArray{QJsonObject{{"path","bin/ui.bin"},{"sha256",sha}}}}};
    auto observed=pgn::update::inspectComponent(code,declaration);
    if(observed.value("state").toString()!="matched_local_manifest"||observed.value("version").toString()!="0.1.0-dev")return 3;
    auto changed=declaration;changed.insert("files",QJsonArray{QJsonObject{{"path","bin/ui.bin"},{"sha256",QString(64,'0')}}});
    if(pgn::update::inspectComponent(code,changed).value("version").isString())return 4;
    changed.insert("files",QJsonArray{QJsonObject{{"path","../wallet.dat"},{"sha256",sha}}});
    if(pgn::update::inspectComponent(code,changed).value("state").toString()!="unidentified")return 5;
    changed=declaration;changed.insert("id","wallet-data");
    if(pgn::update::inspectComponent(code,changed).value("version").isString())return 6;
    for(const auto& path:QStringList{"/absolute","C:/wallet.dat","a/../b","a\\b","a//b","wallets/default","wallet.dat",".cookie","history.sqlite"})if(pgn::update::relativeProgramPath(path))return 7;

    const QString tag="v0.1.0-alpha.1",base="https://github.com/wizz13150/prime-gap-network/releases/";
    QJsonObject release{{"tag_name",tag},{"draft",false},{"prerelease",true},
        {"html_url",base+"tag/"+tag},{"assets",QJsonArray{QJsonObject{{"name","package.zip"},{"size",123},
        {"digest","sha256:"+sha},{"browser_download_url",base+"download/"+tag+"/package.zip"}}}}};
    const auto encoded=[&]{return QJsonDocument(QJsonArray{release}).toJson(QJsonDocument::Compact);};
    auto catalog=pgn::update::publishedPackages(encoded(),true);
    if(catalog.value("packages").toArray().size()!=1||!catalog.value("installation_authorized").isBool()||catalog.value("installation_authorized").toBool())return 8;
    if(!catalog.value("component_versions_available").isBool()||catalog.value("component_versions_available").toBool())return 9;
    if(!pgn::update::publishedPackages(encoded(),false).value("packages").toArray().isEmpty())return 10;
    release.insert("html_url","https://github.com/another-owner/another-repo/releases/tag/"+tag);
    if(!pgn::update::publishedPackages(encoded(),true).value("packages").toArray().isEmpty())return 11;
    release.insert("html_url",base+"tag/"+tag);auto assets=release.value("assets").toArray();auto asset=assets[0].toObject();
    asset.insert("browser_download_url","http://evil.invalid/package.zip");assets[0]=asset;release.insert("assets",assets);
    if(!pgn::update::publishedPackages(encoded(),true).value("packages").toArray()[0].toObject().value("assets").toArray().isEmpty())return 12;
    if(!pgn::update::publishedPackages(QByteArray(1024*1024+1,'x'),true).contains("error"))return 13;
    if(!pgn::update::publishedPackages("{}",true).contains("error"))return 14;
    QFile other(code+"/bin/other.bin");if(!other.open(QIODevice::WriteOnly))return 17;other.write("different active build");other.close();
    if(pgn::update::inspectComponent(code,declaration,other.fileName()).value("version").isString())return 18;
    if(pgn::update::inspectComponent(code,declaration,code+"/bin/ui.bin").value("state").toString()!="matched_local_manifest")return 19;
    if(argc>1){QFile published(QString::fromLocal8Bit(argv[1]));if(!published.open(QIODevice::ReadOnly)||published.size()>1024*1024)return 15;
        const auto real=pgn::update::publishedPackages(published.readAll(),true);if(real.contains("error")||real.value("packages").toArray().isEmpty())return 16;}
    std::cout<<"PASS component discovery: exact file hashes, unknown/mismatched versions, path boundaries, release channels, repository/HTTPS/digests; no install authority\n";
    return 0;
}
