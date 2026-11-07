#pragma once
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>
#include <memory>
#include <atomic>

namespace pgn::ui {
struct CatalogueExportResult {
    bool ok{};
    QString destination, error;
    int parts{};
};

// Large exports remain complete and individually readable NDJSON parts. Only
// the final directory rename publishes them; no partial bundle is advertised.
inline CatalogueExportResult exportBoundedCatalogue(const QString& path,const QString& destination,
                                                     qint64 expectedBytes,qint64 partBudget=64*1024*1024,
                                                     std::shared_ptr<std::atomic_bool> cancellation={}){
    const auto fail=[&](const QString& message){return CatalogueExportResult{false,{},message,0};};
    const auto cancelled=[&]{return cancellation&&cancellation->load();};
    if(cancelled())return fail("Export cancelled; previous destination retained");
    if(expectedBytes<0||partBudget<1024||partBudget>64*1024*1024)return fail("Invalid catalogue export budget");
    const QFileInfo before(path),target(destination);
    const auto sourceIdentity=before.canonicalFilePath();const auto sourceStamp=before.lastModified();
    if(sourceIdentity.isEmpty()||sourceIdentity==target.canonicalFilePath())
        return fail("Choose an export destination different from the conserved source");
    QFile source(path);
    if(!source.open(QIODevice::ReadOnly)||source.size()!=expectedBytes)return fail("Source catalogue identity changed");
    const auto unchanged=[&]{const QFileInfo after(path);return source.size()==expectedBytes
        &&after.size()==expectedBytes&&after.lastModified()==sourceStamp
        &&after.canonicalFilePath()==sourceIdentity;};
    if(expectedBytes<=partBudget){
        QSaveFile output(destination);
        if(!output.open(QIODevice::WriteOnly))return fail("Export destination could not be opened");
        qint64 copied{};
        while(copied<expectedBytes){
            if(cancelled())return fail("Export cancelled; previous destination retained");
            const auto chunk=source.read(qMin<qint64>(65536,expectedBytes-copied));
            if(chunk.isEmpty()||output.write(chunk)!=chunk.size())return fail("Export write failed; previous destination retained");
            copied+=chunk.size();
        }
        if(cancelled()||!unchanged()||!output.commit())return fail("Source changed, export cancelled or commit failed; previous destination retained");
        return {true,destination,{},1};
    }
    const QString finalDirectory=destination+".parts";
    if(QFileInfo::exists(finalDirectory))return fail("That export bundle already exists; choose a new destination");
    QTemporaryDir staging(target.absolutePath()+"/.pgn-catalogue-XXXXXX");
    if(!staging.isValid())return fail("Export staging directory could not be created");
    QJsonArray entries;
    std::unique_ptr<QSaveFile> part;
    std::unique_ptr<QCryptographicHash> partHash;
    QCryptographicHash completeHash(QCryptographicHash::Sha256);
    qint64 partBytes{},copied{};
    QString partName;
    const auto finishPart=[&](){
        if(!part)return true;
        if(!part->commit())return false;
        entries.append(QJsonObject{{"file",partName},{"bytes",QString::number(partBytes)},
                                   {"sha256",QString::fromLatin1(partHash->result().toHex())}});
        part.reset();partHash.reset();partBytes=0;return true;
    };
    while(copied<expectedBytes){
        if(cancelled())return fail("Export cancelled; previous destination retained");
        const QByteArray line=source.readLine(qMin<qint64>(65536,partBudget)+1);
        if(line.isEmpty()||line.size()>qMin<qint64>(65536,partBudget)
           ||(!line.endsWith('\n')&&!source.atEnd())
           ||copied+line.size()>expectedBytes)return fail("Source row or size exceeds its export contract");
        if(part&&partBytes+line.size()>partBudget&&!finishPart())return fail("Export part could not be committed");
        if(!part){
            if(entries.size()>=1024)return fail("Export exceeds the 1024-part bundle budget");
            partName=QString("part-%1.ndjson").arg(entries.size()+1,6,10,QLatin1Char('0'));
            part=std::make_unique<QSaveFile>(staging.path()+"/"+partName);
            if(!part->open(QIODevice::WriteOnly))return fail("Export part could not be opened");
            partHash=std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
        }
        if(part->write(line)!=line.size())return fail("Export part write failed");
        partHash->addData(line);completeHash.addData(line);partBytes+=line.size();copied+=line.size();
    }
    if(!finishPart()||cancelled()||!unchanged())return fail("Source changed, export cancelled or final part failed");
    const QJsonObject manifest{{"schema","PGN_EXACT_CATALOGUE_EXPORT_V1"},
        {"source_file",before.fileName()},{"bytes",QString::number(copied)},
        {"sha256",QString::fromLatin1(completeHash.result().toHex())},
        {"part_byte_limit",QString::number(partBudget)},{"parts",entries}};
    const QByteArray raw=QJsonDocument(manifest).toJson();
    QSaveFile index(staging.path()+"/manifest.json");
    if(raw.size()>262144||!index.open(QIODevice::WriteOnly)||index.write(raw)!=raw.size()||!index.commit())
        return fail("Export manifest could not be committed");
    if(cancelled()||!QDir().rename(staging.path(),finalDirectory))return fail("Export cancelled or bundle could not be published; previous destination retained");
    staging.setAutoRemove(false);
    return {true,finalDirectory,{},int(entries.size())};
}
}
