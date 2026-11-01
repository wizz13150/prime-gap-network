#include "booking/source_registry.h"
#include "booking/decimal_range.h"
#include <QFile>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QRegularExpression>
#include <algorithm>
#include <stdexcept>

namespace pgn::booking {
namespace {
QVector<QStringList> parseCsv(const QByteArray& bytes){
    QVector<QStringList> rows;QStringList row;QByteArray cell;bool quoted=false,closed=false;
    auto field=[&]{const auto value=QString::fromUtf8(cell);if(value.toUtf8()!=cell)throw std::runtime_error("Source UTF-8 invalid");
        row.append(value.trimmed());cell.clear();closed=false;if(row.size()>64)throw std::runtime_error("Source column bound");};
    auto line=[&]{field();rows.append(row);row.clear();if(rows.size()>10001)throw std::runtime_error("Source row bound");};
    for(qsizetype i=0;i<bytes.size();++i){
        const char c=bytes[i];
        if(quoted){if(c=='"'){if(i+1<bytes.size()&&bytes[i+1]=='"'){cell+='"';++i;}else{quoted=false;closed=true;}}else cell+=c;}
        else if(c==',')field();
        else if(c=='\n'||c=='\r'){if(c=='\r'&&i+1<bytes.size()&&bytes[i+1]=='\n')++i;line();}
        else if(c=='"'){if(!cell.isEmpty()||closed)throw std::runtime_error("Malformed source quote");quoted=true;}
        else{if(closed)throw std::runtime_error("Trailing source quote data");cell+=c;}
        if(cell.size()>16384)throw std::runtime_error("Source field bound");
    }
    if(quoted)throw std::runtime_error("Incomplete source quote");
    if(!cell.isEmpty()||!row.isEmpty()||closed)line();
    return rows;
}
}
SourceRegistry loadSourceRegistry(const QString& directory){
    SourceRegistry result;
    try{
        QFile binding(directory+"/binding.json"),source(directory+"/community-source.csv");
        if(!binding.open(QIODevice::ReadOnly)||binding.size()>32768||!source.open(QIODevice::ReadOnly)||source.size()>1048576)
            throw std::runtime_error("Community source unavailable/bound exceeded");
        const auto metadata=QJsonDocument::fromJson(binding.readAll()).object().value("community").toObject();
        auto bytes=source.readAll();result.sha256=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
        if(result.sha256!=metadata.value("source_sha256").toString())throw std::runtime_error("Community source fingerprint changed");
        result.url=metadata.value("source").toString();
        result.observedUtc=QDateTime::fromString(metadata.value("observed_utc").toString(),Qt::ISODateWithMs);
        result.localValidation=metadata.value("schema")=="pgn-local-validation-registry-1";
        const bool localProvenance=result.localValidation&&metadata.value("authority")=="OPERATOR_ENGINE_VALIDATION_NO_NETWORK_ACCEPTANCE"
            &&QRegularExpression("^local-validation://[0-9a-f]{32}$").match(result.url).hasMatch()
            &&metadata.value("scope_start")=="73786976294838206464"&&metadata.value("scope_end")=="39614081257132168796771975168";
        if((result.localValidation?!localProvenance:!result.url.startsWith("https://docs.google.com/spreadsheets/d/1y0OWkPbf8dvg6cpNqGNawhIykAlAWAtI8vqUA4KPiQ8/"))||!result.observedUtc.isValid())
            throw std::runtime_error("Community provenance missing");
        if(bytes.startsWith("\xef\xbb\xbf"))bytes.remove(0,3);
        const auto rows=parseCsv(bytes);
        if(rows.isEmpty()||rows[0].size()<4||rows[0][0]!="Start *1e15"||rows[0][1]!="End *1e15")
            throw std::runtime_error("Community range unit/schema mismatch");
        QString previous;
        for(int i=1;i<rows.size();++i){
            const auto& cells=rows[i];if(cells.size()<4)throw std::runtime_error("Incomplete community range");
            if(cells[0].isEmpty()&&cells[1].isEmpty())continue;
            SourceRange range{i+1,scaleDecimal(cells[0],15),scaleDecimal(cells[1],15),cells[2],cells[3]};
            if(compareUnsigned(range.start,range.end)>=0||(!previous.isEmpty()&&compareUnsigned(range.start,previous)<0))
                throw std::runtime_error("Overlapping/unordered community ranges");
            if(result.localValidation&&(compareUnsigned(range.start,metadata.value("scope_start").toString())<0||compareUnsigned(range.end,metadata.value("scope_end").toString())>0))
                throw std::runtime_error("Local validation row outside explicit67..95 test domain");
            if(range.status!="Open"&&range.status!="Reserved"&&range.status!="Complete")throw std::runtime_error("Unknown community status");
            if((range.status=="Open"&&!range.owner.isEmpty())||(range.status=="Reserved"&&range.owner.isEmpty()))
                throw std::runtime_error("Community owner/status disagreement");
            result.ranges.append(range);previous=range.end;
        }
        if(result.ranges.isEmpty())throw std::runtime_error("Empty community register");
        result.start=result.ranges.first().start;result.end=result.ranges.last().end;
    }catch(const std::exception& e){result.ranges.clear();result.error=QString::fromUtf8(e.what());}
    return result;
}
SourceRegistry loadReservationDirectory(const QString& directory,const SourceRegistry& historical){
    auto result=historical;
    try{
        const auto root=QDir(directory).absoluteFilePath("../community");
        QFile metadata(root+"/registry.json");if(!metadata.exists())return result;
        QFile original(root+"/historical-original.csv"),extended(root+"/community-reservations.csv");
        if(!metadata.open(QIODevice::ReadOnly)||metadata.size()>32768||!original.open(QIODevice::ReadOnly)||original.size()>1048576||!extended.open(QIODevice::ReadOnly)||extended.size()>1048576)
            throw std::runtime_error("Development reservation directory unavailable/bound exceeded");
        const auto info=QJsonDocument::fromJson(metadata.readAll()).object();
        if(info.value("schema").toString()!="pgn-community-registry-v1"||info.value("interval_convention").toString()!="start-inclusive-end-exclusive")
            throw std::runtime_error("Development reservation directory schema mismatch");
        const auto originalBytes=original.readAll(),bytes=extended.readAll();
        const auto digest=QString::fromLatin1(QCryptographicHash::hash(originalBytes,QCryptographicHash::Sha256).toHex());
        if(digest!=info.value("historical_source").toObject().value("sha256").toString()||!bytes.startsWith(originalBytes))
            throw std::runtime_error("Development historical prefix changed; directory not admitted");
        const auto oldRows=parseCsv(originalBytes),rows=parseCsv(bytes);
        if(rows.size()<oldRows.size()||rows.first().size()<4||rows.first()[0]!="Start *1e15"||rows.first()[1]!="End *1e15")
            throw std::runtime_error("Development reservation coordinate schema mismatch");
        // Fresh historical declarations win over the immutable archival prefix.
        // Extensions remain exact raw declarations: a manual overlap never
        // marks a whole block or zone reserved/processed in the mining model.
        for(int i=oldRows.size();i<rows.size();++i){const auto& r=rows[i];
            if(r.size()<4)throw std::runtime_error("Incomplete development reservation row");
            if(r[0].isEmpty()&&r[1].isEmpty())continue;
            SourceRange item{-(i+1),scaleDecimal(r[0],15),scaleDecimal(r[1],15),r[2],r[3]};
            if(compareUnsigned(item.start,item.end)>=0||(item.status!="Open"&&item.status!="Reserved"&&item.status!="Complete")||(item.status=="Open"&&!item.owner.isEmpty())||(item.status=="Reserved"&&item.owner.isEmpty()))
                throw std::runtime_error("Invalid development reservation declaration");
            if(!item.owner.isEmpty())result.ranges.append(item);
        }
        std::stable_sort(result.ranges.begin(),result.ranges.end(),[](const auto& a,const auto& b){return compareUnsigned(a.start,b.start)<0;});
        result.sha256+=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
    }catch(const std::exception& e){result=historical;result.error=QString::fromUtf8(e.what());}
    return result;
}
const SourceRange* SourceRegistry::declaredOpenContaining(const QString& first,const QString& last) const{
    if(!valid()||compareUnsigned(first,last)>=0)return nullptr;
    for(const auto& range:ranges)if(range.status=="Open"&&compareUnsigned(first,range.start)>=0&&compareUnsigned(last,range.end)<=0)return &range;
    return nullptr;
}
}
