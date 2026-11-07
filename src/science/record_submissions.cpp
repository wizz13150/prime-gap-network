#include "science/record_submissions.h"
#include "booking/decimal_range.h"
#include "verification/bounded_gap_verifier.h"
#include <certified_gap_builder.h>
#include <prime_certificate_builder.h>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>
#include <QJsonObject>
#include <sqlite3.h>
#include <bit>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pgn::science {
namespace {
// Reuse native exact certificate semantics for supplied endpoints above the
// bounded-MR leaf domain. This is asynchronous record qualification, never a
// mining-path test. A hard factorization stays pending, not prime or composite.
pgn::core::GapBuildResult recordGapEvidence(const pgn::core::Integer& lower,
    const pgn::core::Integer& gap,pgn::core::CertifiedGapLimits limits,
    const std::function<bool()>& cancelled,QElapsedTimer& largerDomainBudget){
    using namespace pgn::core;
    auto built=buildBoundedCertifiedGap(lower,gap,limits,cancelled);
    if(built.verdict!=GapBuildVerdict::UnsupportedDomain)return built;
    // Aggregate wall budget prevents a large candidate list from monopolizing
    // the background pool; per-endpoint division/witness limits also apply.
    if(!largerDomainBudget.isValid())largerDomainBudget.start();
    QElapsedTimer pairBudget;pairBudget.start();
    const auto stop=[&]{return (cancelled&&cancelled())||largerDomainBudget.elapsed()>=1000||pairBudget.elapsed()>=100;};
    const auto deferred=[&]{return GapBuildResult{cancelled&&cancelled()?GapBuildVerdict::Cancelled:GapBuildVerdict::BudgetExceeded,std::nullopt,0};};
    if(stop())return deferred();
    const PrimeBuildBudget budget{limits.endpoint_limits,20000,64};
    const auto first=buildSuppliedPrimeCertificate(lower,budget,stop);
    if(!first.evidence||stop())return deferred();
    const auto last=buildSuppliedPrimeCertificate(lower+gap,budget,stop);
    if(!last.evidence||stop())return deferred();
    built=buildCertifiedGapFromPrimeCertificates(lower,gap,*first.evidence,*last.evidence,limits,stop);
    if(built.verdict==GapBuildVerdict::Cancelled&&!(cancelled&&cancelled()))return deferred();
    return built;
}
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
quint32 integer(const QJsonValue& value){const auto n=value.toInteger(-1);need(value.isDouble()&&n>0&&n<=UINT32_MAX&&value.toDouble()==double(n),"Invalid record-reference gap size");return quint32(n);}
QString decimal(const QString& text){need(!text.isEmpty()&&text.size()<=160&&booking::canonicalUnsigned(text)==text,"Invalid exact record coordinate");return text;}
QString column(sqlite3_stmt* s,int n){need(sqlite3_column_type(s,n)==SQLITE_TEXT&&sqlite3_column_bytes(s,n)<=160,"Record archive coordinate type/size invalid");const QByteArray raw(reinterpret_cast<const char*>(sqlite3_column_text(s,n)),sqlite3_column_bytes(s,n));const auto text=QString::fromUtf8(raw);need(text.toUtf8()==raw,"Record archive coordinate UTF8 invalid");return decimal(text);}
}
RecordReference readRecordReference(const QString& file){
    RecordReference result;
    try{
        QFile input(file);need(input.open(QIODevice::ReadOnly)&&input.size()>0&&input.size()<=4*1024*1024,"Record reference unavailable or outside input budget");
        QJsonParseError error;const auto document=QJsonDocument::fromJson(input.readAll(),&error);need(error.error==QJsonParseError::NoError&&document.isObject(),"Record reference JSON invalid");const auto data=document.object();
        need(data.value("schema")=="prime-gap-record-reference-1"&&data.value("supported_bits").toArray()==QJsonArray{67,95},"Record reference domain/schema unsupported");
        result.observedUtc=data.value("retrieved_utc").toString();result.sourceUrl=data.value("source_url").toString();result.sourceBlob=data.value("source_blob_sha1").toString();
        need(QDateTime::fromString(result.observedUtc,Qt::ISODateWithMs).isValid()&&!result.sourceUrl.isEmpty()&&result.sourceBlob.size()==40,"Record reference provenance unavailable");
        result.minimumPotentialGap=integer(data.value("minimum_potential_gap_in_domain"));
        const auto starts=data.value("known_starts").toObject();need(!starts.isEmpty()&&starts.size()<=200000,"Record reference cardinality invalid");
        for(auto it=starts.begin();it!=starts.end();++it){bool ok=false;const auto gap=it.key().toUInt(&ok);need(ok&&gap>0&&QString::number(gap)==it.key()&&it.value().isString(),"Record reference key invalid");const auto lower=decimal(it.value().toString());need(lower!="0"&&lower.size()<=29,"Record reference coordinate outside domain");result.starts.insert(gap,lower);}
        const auto beyond=data.value("known_beyond_domain").toArray();need(beyond.size()<=200000,"Record reference cardinality exceeded");
        for(const auto& value:beyond){const auto gap=integer(value);need(!result.starts.contains(gap)&&!result.beyondDomain.contains(gap),"Duplicate record reference size");result.beyondDomain.insert(gap);}
        need(result.starts.value(1)=="2"&&result.starts.value(2)=="3"&&result.starts.value(4)=="7","Record reference base identities invalid");
        quint32 minimum=2;
        while(result.starts.contains(minimum)&&booking::compareUnsigned(result.starts.value(minimum),"73786976294838206464")<=0)minimum+=2;
        need(result.minimumPotentialGap==minimum,"Record reference domain cutoff differs from exact starts");
    }catch(const std::exception& error){result.error=QString::fromUtf8(error.what());}
    return result;
}
bool improvesRecordReference(const RecordReference& ref,quint32 gap,const QString& lower){
    if(!ref.valid()||gap<2||gap%2)return false;
    try{decimal(lower);if(booking::bitLength(lower)<67||booking::bitLength(lower)>95)return false;}catch(...){return false;}
    const auto known=ref.starts.constFind(gap);
    return known==ref.starts.cend()||booking::compareUnsigned(lower,*known)<0;
}
RecordSubmissions collectRecordSubmissions(const QString& database,const QString& reference,const std::atomic_bool* cancelled){
    RecordSubmissions out;sqlite3* db{};sqlite3_stmt* query{};
    const auto stopped=[&]{return cancelled&&cancelled->load();};QElapsedTimer largerDomainBudget;
    try{
        const auto ref=readRecordReference(reference);need(ref.valid(),ref.error.toUtf8().constData());out.referenceUtc=ref.observedUtc;out.referenceUrl=ref.sourceUrl;
        need(QFileInfo(database).isFile()&&sqlite3_open_v2(database.toUtf8().constData(),&db,SQLITE_OPEN_READONLY|SQLITE_OPEN_NOMUTEX,nullptr)==SQLITE_OK,"Record archive unavailable");sqlite3_busy_timeout(db,200);
        sqlite3_progress_handler(db,4096,[](void* value){return static_cast<const std::atomic_bool*>(value)&&static_cast<const std::atomic_bool*>(value)->load()?1:0;},const_cast<std::atomic_bool*>(cancelled));
        need(sqlite3_exec(db,"BEGIN",nullptr,nullptr,nullptr)==SQLITE_OK,"Record archive snapshot unavailable");
        need(sqlite3_prepare_v2(db,"SELECT COALESCE(MAX(id),0) FROM gaps",-1,&query,nullptr)==SQLITE_OK&&sqlite3_step(query)==SQLITE_ROW,"Record archive revision unavailable");out.revision=sqlite3_column_int64(query,0);sqlite3_finalize(query);query=nullptr;
        const auto bits=std::bit_cast<quint64>(double(ref.minimumPotentialGap))^(quint64(1)<<63);
        const auto key=QString::number(bits,16).rightJustified(16,'0')+QString(16,'0');
        need(sqlite3_prepare_v2(db,"SELECT g.id,g.gap,g.p,g.q FROM native_plot_coordinates c JOIN gaps g ON g.id=c.gap_id WHERE c.scope='' AND c.axis='length' AND c.point_key>=? ORDER BY c.point_key",-1,&query,nullptr)==SQLITE_OK,"Record archive length index unavailable");
        const auto encoded=key.toUtf8();need(sqlite3_bind_text(query,1,encoded.constData(),encoded.size(),SQLITE_TRANSIENT)==SQLITE_OK,"Record index bound invalid");
        QHash<quint32,SubmissionRecord> best;int status;
        while((status=sqlite3_step(query))==SQLITE_ROW){
            if(stopped()){out.cancelled=true;break;}
            ++out.examined;const auto size=column(query,1),lower=column(query,2),upper=column(query,3);bool ok=false;const auto gap=size.toUInt(&ok);need(ok&&gap>0&&booking::subtractUnsigned(upper,lower)==size,"Record archive pair identity differs");
            if(!improvesRecordReference(ref,gap,lower))continue;
            const auto prior=best.constFind(gap);if(prior!=best.cend()&&booking::compareUnsigned(prior->lower,lower)<=0)continue;
            const pgn::core::Integer p{lower.toStdString()},g{size.toStdString()};
            const pgn::core::CertifiedGapLimits limits{{128,32,32},10000};
            const auto built=recordGapEvidence(p,g,limits,stopped,largerDomainBudget);
            if(built.verdict==pgn::core::GapBuildVerdict::Cancelled||stopped()){out.cancelled=true;break;}
            if(built.verdict!=pgn::core::GapBuildVerdict::Built){++out.pending;continue;}
            if(!built.evidence||pgn::core::verifyCertifiedGap(*built.evidence,limits).verdict!=pgn::core::CertifiedGapVerdict::VerifiedConsecutiveGap){++out.rejected;continue;}
            if(booking::compareUnsigned(upper,QString::fromLatin1(verification::PrimeBound.data(),qsizetype(verification::PrimeBound.size())))<0){
                const auto independent=verification::verifyGap(lower.toStdString(),upper.toStdString(),10000,cancelled);
                need(independent.verdict==verification::GapVerdict::VerifiedWithinBound,"Independent bounded MR gap verifier disagrees with certificate");
            }
            best.insert(gap,{gap,lower,upper,QString::number(double(gap)/std::log(lower.toDouble()),'f',6)});
        }
        need(status==SQLITE_DONE||out.cancelled,"Record archive read interrupted or failed");
        for(const auto& row:best)out.records.append(row);
        std::sort(out.records.begin(),out.records.end(),[](const auto& a,const auto& b){return a.gap<b.gap;});
        for(const auto& row:out.records)out.text+=QString("%1  %2  %3\n").arg(row.gap).arg(row.merit,row.lower);
    }catch(const std::exception& error){out.error=QString::fromUtf8(error.what());}
    if(query)sqlite3_finalize(query);if(db)sqlite3_close(db);return out;
}
RecordSubmissions collectRecordSubmissionsFromPairs(const QVector<SubmissionRecord>& pairs,const QString& reference,const std::atomic_bool* cancelled){
    RecordSubmissions out;
    const auto stopped=[&]{return cancelled&&cancelled->load();};QElapsedTimer largerDomainBudget;
    try{
        need(pairs.size()<=10000,"Ticket candidate budget exceeded");const auto ref=readRecordReference(reference);need(ref.valid(),ref.error.toUtf8().constData());out.referenceUtc=ref.observedUtc;out.referenceUrl=ref.sourceUrl;
        QHash<quint32,SubmissionRecord> best;
        for(const auto& pair:pairs){
            if(stopped()){out.cancelled=true;break;}++out.examined;const auto lower=decimal(pair.lower),upper=decimal(pair.upper);const auto gap=pair.gap;
            need(gap>0&&booking::subtractUnsigned(upper,lower)==QString::number(gap),"Ticket pair identity differs");
            if(!improvesRecordReference(ref,gap,lower))continue;
            const auto prior=best.constFind(gap);if(prior!=best.cend()&&booking::compareUnsigned(prior->lower,lower)<=0)continue;
            const pgn::core::Integer p{lower.toStdString()},g{gap};const pgn::core::CertifiedGapLimits limits{{128,32,32},10000};
            const auto built=recordGapEvidence(p,g,limits,stopped,largerDomainBudget);
            if(built.verdict==pgn::core::GapBuildVerdict::Cancelled||stopped()){out.cancelled=true;break;}
            if(built.verdict!=pgn::core::GapBuildVerdict::Built){++out.pending;continue;}
            if(!built.evidence||pgn::core::verifyCertifiedGap(*built.evidence,limits).verdict!=pgn::core::CertifiedGapVerdict::VerifiedConsecutiveGap){++out.rejected;continue;}
            if(booking::compareUnsigned(upper,QString::fromLatin1(verification::PrimeBound.data(),qsizetype(verification::PrimeBound.size())))<0){const auto verified=verification::verifyGap(lower.toStdString(),upper.toStdString(),10000,cancelled);need(verified.verdict==verification::GapVerdict::VerifiedWithinBound,"Independent gap verifier disagrees");}
            best.insert(gap,{gap,lower,upper,QString::number(double(gap)/std::log(lower.toDouble()),'f',6)});
        }
        for(const auto& row:best)out.records.append(row);std::sort(out.records.begin(),out.records.end(),[](const auto& x,const auto& y){return x.gap<y.gap;});for(const auto& row:out.records)out.text+=QString("%1  %2  %3\n").arg(row.gap).arg(row.merit,row.lower);
    }catch(const std::exception& error){out.error=QString::fromUtf8(error.what());}
    return out;
}

}
