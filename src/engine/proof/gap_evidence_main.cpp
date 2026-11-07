#include <pgn/certified_gap_builder.h>
#include <pgn/certified_gap_encoding.h>
#include <pgn/prime_certificate_builder.h>
#include <pgn/range_commitment.h>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <chrono>
#include <algorithm>
using namespace pgn::core;
namespace {
Integer decimal(const QJsonValue& value){const auto text=value.toString();if(!QRegularExpression("^(?:0|[1-9][0-9]{0,38})$").match(text).hasMatch())throw std::invalid_argument("Canonical bounded decimal required");return Integer{text.toStdString()};}
QString hex(std::span<const uint8_t> bytes){return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(bytes.data()),qsizetype(bytes.size())).toHex());}
Digest digest(const QString& text){const auto bytes=QByteArray::fromHex(text.toLatin1());if(bytes.size()!=32||QString::fromLatin1(bytes.toHex())!=text)throw std::invalid_argument("Canonical digest required");Digest result;std::copy(bytes.begin(),bytes.end(),result.begin());return result;}
Digest sha(std::span<const uint8_t> value){const auto bytes=QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(value.data()),qsizetype(value.size())),QCryptographicHash::Sha256);Digest result;std::copy(bytes.begin(),bytes.end(),result.begin());return result;}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);QJsonObject output;int code=0;try{
 QFile input;if(!input.open(stdin,QIODevice::ReadOnly))throw std::runtime_error("Evidence input unavailable");const auto raw=input.read(1024*1024+1);if(raw.size()>1024*1024)throw std::invalid_argument("Evidence input exceeds1MiB");QJsonParseError error;const auto doc=QJsonDocument::fromJson(raw,&error);if(error.error!=QJsonParseError::NoError||!doc.isObject())throw std::invalid_argument("Invalid evidence request");const auto request=doc.object(),work=request.value("work").toObject(),computed=request.value("computed").toObject();
 RangeContract range{digest(work.value("ticket").toString()),decimal(work.value("start")),decimal(work.value("length")),100};if(computed.value("ticket_id").toString()!=work.value("ticket").toString()||decimal(computed.value("start"))!=range.start||decimal(computed.value("end"))!=range.start+range.length)throw std::invalid_argument("Evidence does not match issued ticket");
 const auto wanted=decimal(request.value("lower_prime"));std::vector<SegmentResult> segments(100);for(uint32_t i=0;i<100;++i)segments[i].index=i;const auto records=computed.value("best").toArray();if(records.size()>100)throw std::invalid_argument("Too many larger gaps");Integer gap{};size_t slot=100,index=0;
 for(const auto& entry:records){const auto record=entry.toObject();const auto lower=decimal(record.value("lower_prime"));const auto width=record.value("gap").toInteger();if(width<=0||lower<range.start||lower>=range.start+range.length||decimal(record.value("upper_prime"))!=lower+width)throw std::invalid_argument("Invalid larger-gap identity");const auto segment=(((lower-range.start+1)*100-1)/range.length).convert_to<size_t>();segments[segment].records.push_back({lower,Integer{width},0});if(lower==wanted){gap=width;slot=segment;}}
 if(slot==100||gap>65536)throw std::invalid_argument("Selected gap missing or beyond local builder budget");for(auto& segment:segments)std::sort(segment.records.begin(),segment.records.end(),[](const auto& a,const auto& b){return a.lower<b.lower;});for(;index<segments[slot].records.size()&&segments[slot].records[index].lower!=wanted;++index){}
 const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(30);const auto cancelled=[&]{return std::chrono::steady_clock::now()>=until;};const CertifiedGapLimits limits{{128,64,64},65536};const EncodingLimits encoding{100,4*1024*1024,32,100};
 GapBuildResult built{GapBuildVerdict::InvalidPair,std::nullopt,0};
 if(request.contains("supplied_scientific_bytes")){
  // The entire request is already bounded to1MiB. A cached/externally built
  // certificate is an untrusted hint, never a substitute for native proof.
  const auto value=request.value("supplied_scientific_bytes");if(!value.isString())throw std::invalid_argument("Supplied scientific proof must be canonical hex");
  const auto text=value.toString();const auto bytes=QByteArray::fromHex(text.toLatin1());if(bytes.isEmpty()||QString::fromLatin1(bytes.toHex())!=text)throw std::invalid_argument("Supplied scientific proof must be canonical hex");
  auto evidence=decodeCertifiedGap(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(bytes.constData()),size_t(bytes.size())),{encoding,limits});
  if(evidence.lower!=wanted||evidence.gap!=gap)throw std::invalid_argument("Supplied scientific proof belongs to another gap");
  built={GapBuildVerdict::Built,std::move(evidence),0};
 }else{
  built=buildBoundedCertifiedGap(wanted,gap,limits,cancelled);
  if(built.verdict==GapBuildVerdict::UnsupportedDomain){const PrimeBuildBudget budget{{128,64,64},2000000,128};const auto left=buildSuppliedPrimeCertificate(wanted,budget,cancelled),right=buildSuppliedPrimeCertificate(wanted+gap,budget,cancelled);if(left.evidence&&right.evidence)built=buildCertifiedGapFromPrimeCertificates(wanted,gap,*left.evidence,*right.evidence,limits,cancelled);}
 }
 if(!built.evidence||verifyCertifiedGap(*built.evidence,limits).verdict!=CertifiedGapVerdict::VerifiedConsecutiveGap)throw std::runtime_error("Exact gap certificate could not be built within budget");const auto commitment=commitRange(range,segments,encoding,sha);const auto proof=proveSegment(range,segments,uint32_t(slot),encoding,sha);const auto segment=encodeSegment(range,segments[slot],encoding);const auto scientific=encodeCertifiedGap(*built.evidence,{encoding,limits});QJsonArray siblings;for(const auto& value:proof.siblings)siblings.append(hex(value));
 output={{"schema","PGN_NATIVE_GAP_EVIDENCE_V1"},{"details_root",hex(commitment.root)},{"segment_index",qint64(slot)},{"segment_bytes",hex(segment)},{"siblings",siblings},{"record_index",qint64(index)},{"scientific_bytes",hex(scientific)},{"lower_prime",QString::fromStdString(wanted.convert_to<std::string>())},{"verified",true},{"network_accepted",false}};
 }catch(const std::exception& error){output={{"schema","PGN_NATIVE_GAP_EVIDENCE_V1"},{"verified",false},{"network_accepted",false},{"error",error.what()}};code=1;}QFile out;if(out.open(stdout,QIODevice::WriteOnly))out.write(QJsonDocument(output).toJson(QJsonDocument::Compact)+"\n");return code;}
