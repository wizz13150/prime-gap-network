#pragma once
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <stdexcept>

// Explicit historical recovery evidence. It never launches or authorizes a
// producer. The caller pays custody/freshness; lineage replays the complete
// immutable parent log against the bound hash and exact checkpoint counter.
namespace pgn::mining::checkpoint_recovery {
inline constexpr auto Origin="RECOVERED_CHECKPOINT_PUBLICATION";
struct Evidence {QString parentSourceSha,imageSha,childBirth;quint64 childPid{};};
inline void need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
inline QString text(const QJsonObject& o,const char* key){need(o.value(key).isString(),"Recovery exact string absent");return o.value(key).toString();}
inline bool digest(const QString& value){return QRegularExpression("^[0-9a-f]{64}$").match(value).hasMatch()&&value.size()==64;}
inline quint64 word(const QByteArray& b,int at){quint64 v=0;for(int j=0;j<8;++j)v|=quint64(uchar(b[at+j]))<<(8*j);return v;}
inline QJsonObject decoded(const QJsonObject& o,const char* key,int maximum){
 const auto encoded=text(o,key).toLatin1(),bytes=QByteArray::fromBase64(encoded);
 need(!bytes.isEmpty()&&bytes.size()<=maximum&&bytes.toBase64()==encoded&&QString::fromUtf8(bytes).toUtf8()==bytes,"Recovery bounded exact UTF8/base64 required");
 QJsonParseError e;const auto d=QJsonDocument::fromJson(bytes,&e);
 need(e.error==QJsonParseError::NoError&&d.isObject(),"Recovery evidence JSON invalid");return d.object();
}
inline bool integer(const QJsonObject& o,const char* key,quint64 value){
 const auto v=o.value(key);return value<=9007199254740991ULL&&v.isDouble()&&v.toDouble()==double(value);
}
inline Evidence validate(const QJsonObject& proof,const QString& parent,const QString& child,
 const QString& checkpointSha,const QByteArray& checkpoint){
 auto keys=proof.keys();keys.sort();QStringList expected{"schema","parent_exit_base64","parent_source_sha256","source_image_sha256","publication_base64","seal_base64"};expected.sort();
 need(keys==expected&&proof.value("schema")=="pgn-checkpoint-publication-recovery-1","Recovery evidence exact schema/keys required");
 need(checkpoint.size()==64&&checkpoint.left(8)=="PGNCG001"&&digest(checkpointSha)&&QCryptographicHash::hash(checkpoint,QCryptographicHash::Sha256).toHex()==checkpointSha.toLatin1(),"Recovery exact checkpoint hash/bytes differ");
 Evidence result;result.parentSourceSha=text(proof,"parent_source_sha256");result.imageSha=text(proof,"source_image_sha256");
 need(digest(result.parentSourceSha)&&digest(result.imageSha),"Recovery exact source/image SHA required");
 const auto exit=decoded(proof,"parent_exit_base64",1024);
 need(exit.size()==3&&text(exit,"run")==parent&&integer(exit,"exit_code",5)&&!text(exit,"utc").isEmpty(),"Recovery requires the actual parent exit5, never a successful or unknown exit");
 const auto publication=decoded(proof,"publication_base64",8192);
 need(publication.value("state")=="EXACT_COMPLETE_PENDING_CHECKPOINT_RECOVERED_ATOMICALLY"&&text(publication,"source_run")==parent&&text(publication,"published_sha256").toLower()==checkpointSha&&publication.value("pending_retained").isBool()&&publication.value("pending_retained").toBool()&&publication.value("source_or_GPU_started").isBool()&&!publication.value("source_or_GPU_started").toBool(),"Recovery exact inactive pending-publication receipt required");
 need(digest(text(publication,"old_sha256").toLower())&&text(publication,"old_sha256").toLower()!=checkpointSha,"Recovery old checkpoint CAS differs");
 need(text(publication,"positions")==QString::number(word(checkpoint,32))&&text(publication,"tests")==QString::number(word(checkpoint,40))&&integer(publication,"gaps",word(checkpoint,48))&&integer(publication,"batches",word(checkpoint,56)),"Recovery publication counters differ from checkpoint bytes");
 const auto seal=decoded(proof,"seal_base64",8192);
 need(seal.value("state")=="QUALIFIED_NATIVE_RECOVERY_PREFIX_REAL_CLEAN_STOP"&&text(seal,"run")==child&&text(seal,"old_failed_run_preserved")==parent&&text(seal,"input_sha256").toLower()==checkpointSha&&text(seal,"image_sha256").toLower()==result.imageSha&&integer(seal,"actual_exit_code",0)&&seal.value("source_credit")=="SOURCE_PRP_INDEPENDENT_PROOF_PENDING","Recovery real qualified child clean-stop witness differs");
 const auto pid=seal.value("pid");need(pid.isDouble()&&pid.toInteger(-1)>0&&pid.toInteger()<=4294967295LL&&pid.toDouble()==double(pid.toInteger()),"Recovery actual child PID invalid");
 result.childPid=quint64(pid.toInteger());result.childBirth=text(seal,"birth");
 need(result.childBirth.size()==28&&QRegularExpression("^\\d{4}-\\d\\d-\\d\\dT\\d\\d:\\d\\d:\\d\\d\\.\\d{7}Z$").match(result.childBirth).hasMatch(),"Recovery actual child birth absent");
 return result;
}
}
