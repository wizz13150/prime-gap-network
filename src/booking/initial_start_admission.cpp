#include "booking/initial_start_admission.h"
#include "booking/decimal_range.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>
#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <stdexcept>

namespace pgn::booking {
namespace {
using Integer=boost::multiprecision::cpp_int;
Integer number(const QString& s){if(canonicalUnsigned(s)!=s)throw std::runtime_error("Initial start decimal must be canonical");return Integer(s.toStdString());}
void uuid(const QString& s){const auto value=QUuid::fromString(s);if(value.isNull()||value.toString(QUuid::WithoutBraces)!=s)throw std::runtime_error("Canonical non-null initial command/observation UUID required");}
void validate(const InitialStartScope& s,const SourceRegistry& r){
    const auto es=number(s.executionStart),ee=number(s.executionEnd),a=number(s.autoStart),b=number(s.autoEnd),anchor=number(s.blockAnchor),width=number(s.blockWidth);
    if(es>=ee||a<es||b>ee||a>=b||width<=0||anchor>es)throw std::runtime_error("Initial execution/automatic scope or explicit block grid invalid");
    static const QRegularExpression hash("^[0-9a-f]{64}$");
    if(!r.valid()||r.ranges.size()>10000||!r.observedUtc.isValid()||r.observedUtc.toUTC().toString(Qt::ISODateWithMs).isEmpty()||
       r.url.isEmpty()||r.url.toUtf8().size()>4096||r.url.contains(QChar(0))||r.url.contains('\r')||r.url.contains('\n')||
       QString::fromUtf8(r.url.toUtf8())!=r.url||r.sha256.size()!=64||!hash.match(r.sha256).hasMatch())throw std::runtime_error("Initial source registry/provenance unavailable");
    std::optional<Integer> previous;
    for(const auto& row:r.ranges){
        const auto first=number(row.start),last=number(row.end);
        if(row.row<1||first>=last||(previous&&first<*previous)||row.owner.size()>16384||
           (row.status!="Open"&&row.status!="Reserved"&&row.status!="Complete")||
           (row.status=="Open"&&!row.owner.isEmpty())||(row.status=="Reserved"&&row.owner.isEmpty())||
           row.owner.contains(QChar(0))||QString::fromUtf8(row.owner.toUtf8())!=row.owner)
            throw std::runtime_error("Initial registry interval/status/owner invalid");
        previous=last;
    }
}
}
QString initialStartFingerprint(const QString& request,const QString& device,const InitialStartScope& s,const SourceRegistry& r,const InitialBoundaryEvidence& proof,QString* registryDigest){
    uuid(request);uuid(proof.observationUUID);validate(s,r);
    const auto part=[](QCryptographicHash& hash,const QJsonArray& fields){hash.addData(QJsonDocument(fields).toJson(QJsonDocument::Compact));hash.addData(QByteArray("\n"));};
    QCryptographicHash registryHash(QCryptographicHash::Sha256);part(registryHash,{r.url,r.sha256,r.observedUtc.toUTC().toString(Qt::ISODateWithMs)});
    for(const auto& row:r.ranges)part(registryHash,{row.row,row.start,row.end,row.status,row.owner});
    const auto digest=QString::fromLatin1(registryHash.result().toHex());if(registryDigest)*registryDigest=digest;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    part(hash,{request,device,s.executionStart,s.executionEnd,s.autoStart,s.autoEnd,s.blockAnchor,s.blockWidth,proof.observationUUID,digest});
    // r.start/end are a display/binding envelope, NEVER selection authority.
    return QString::fromLatin1(hash.result().toHex());
}
std::optional<QPair<QString,QString>> firstInitialFreeBlock(const InitialStartScope& s,const SourceRegistry& r,const QVector<QPair<QString,QString>>& occupied){
    validate(s,r);const auto a=number(s.autoStart),b=number(s.autoEnd),anchor=number(s.blockAnchor),width=number(s.blockWidth);
    const auto align=[&](const Integer& n)->Integer{return anchor+((n-anchor+width-1)/width)*width;};
    for(const auto& row:r.ranges){
        if(row.status!="Open")continue;const Integer first=std::max(a,number(row.start)),limit=std::min(b,number(row.end));if(first>=limit)continue;
        Integer candidate=align(first);
        for(const auto& interval:occupied){
            const auto start=number(interval.first),end=number(interval.second);if(start>=end)throw std::runtime_error("Invalid occupied interval");
            if(end<=candidate)continue;if(start>=candidate+width)break;candidate=align(end);
        }
        if(candidate+width<=limit)return QPair<QString,QString>{QString::fromStdString(candidate.str()),QString::fromStdString(Integer(candidate+width).str())};
    }return {};
}
}
