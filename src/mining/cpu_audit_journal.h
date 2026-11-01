#pragma once
#include "mining/cpu_audit_work.h"
#include "mining/cpu_audit_result.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QRandomGenerator>
#include <QLockFile>
#include <memory>
namespace pgn::mining {
// Small private outbox, not a history database. Expected answers from another
// participant never enter it. The CPU receives serviceRequest alone.
class CpuAuditJournal {
    QString file_,error_;QJsonObject entries_;bool loaded_{};std::unique_ptr<QLockFile> lock_;
    static constexpr qint64 ByteLimit=32768;
    static constexpr int EntryLimit=16;
    static qint64 reservedSize(const QJsonObject& entries){
        qint64 size=QJsonDocument(QJsonObject{{"schema","PGN_CPU_AUDIT_OUTBOX_V1"},{"entries",entries}}).toJson(QJsonDocument::Compact).size();
        for(const auto& value:entries){
            const auto row=value.toObject();
            // Five bounded counts, nonce and commitment fit inside 1 KiB.
            // Leave room for the three eventual block references as well.
            if(!row.contains("answer"))size+=1024;
            for(const auto* phase:{"assignment_block","commit_block","reveal_block"})if(!row.contains(phase))size+=100;
        }
        return size;
    }
    bool save(const QJsonObject& entries){
        const auto bytes=QJsonDocument(QJsonObject{{"schema","PGN_CPU_AUDIT_OUTBOX_V1"},{"entries",entries}}).toJson(QJsonDocument::Compact);
        if(entries.size()>EntryLimit||bytes.size()>ByteLimit){error_="CPU audit outbox is full; completed work must remain retained";return false;}
        if(entries==entries_&&loaded_){error_.clear();return true;}
        if(!QDir().mkpath(QFileInfo(file_).absolutePath())){error_="CPU audit outbox directory unavailable";return false;}
        QSaveFile file(file_);if(!file.open(QIODevice::WriteOnly)){error_="CPU audit outbox cannot be opened";return false;}
        file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);
        if(file.write(bytes)!=bytes.size()||!file.commit()){error_="CPU audit outbox could not be committed; completed work retained";return false;}
        entries_=entries;loaded_=true;error_.clear();return true;
    }
public:
    explicit CpuAuditJournal(QString file):file_(std::move(file)){}
    static QString key(const CpuAuditWork& work){
        const QJsonObject identity{{"genesis",work.genesis},{"request",work.requestId},{"slot",work.slot},{"attempt",qint64(work.attempt)}};
        return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
    }
    bool load(){
        if(loaded_)return true;
        if(!QDir().mkpath(QFileInfo(file_).absolutePath())){error_="CPU audit outbox directory unavailable";return false;}
        if(!lock_)lock_=std::make_unique<QLockFile>(file_+".lock");
        if(!lock_->isLocked()&&!lock_->tryLock(0)){error_="Another process owns the CPU audit outbox";return false;}
        QFile file(file_);
        if(!file.exists()){loaded_=true;return true;}
        if(!file.open(QIODevice::ReadOnly)||file.size()>ByteLimit){error_="CPU audit outbox unavailable or oversized; no work discarded";return false;}
        QJsonParseError parse;const auto bytes=file.readAll();const auto document=QJsonDocument::fromJson(bytes,&parse);const auto value=document.object();file.close();
        if(parse.error!=QJsonParseError::NoError||document.toJson(QJsonDocument::Compact)!=bytes||value.value("schema")!="PGN_CPU_AUDIT_OUTBOX_V1"||!value.value("entries").isObject()||value.value("entries").toObject().size()>EntryLimit){error_="CPU audit outbox is invalid; no work discarded";return false;}
        const auto entries=value.value("entries").toObject();static const QRegularExpression hash("^[0-9a-f]{64}$");
        for(auto i=entries.begin();i!=entries.end();++i)if(!hash.match(i.key()).hasMatch()||!i.value().isObject()){error_="CPU audit outbox identity is invalid";return false;}
        entries_=entries;loaded_=true;error_.clear();return true;
    }
    const QString& error()const{return error_;}
    bool exists()const{return QFileInfo::exists(file_);}
    QJsonObject entries()const{return entries_;}
    QJsonObject entry(const QString& id)const{return entries_.value(id).toObject();}
    bool canRequestAssignment(){
        if(!load())return false;
        return entries_.size()<EntryLimit&&reservedSize(entries_)+4096<=ByteLimit;
    }
    bool retainAssignment(const QString& device,const QString& address,const CpuAuditWork& work){
        if(!load())return false;static const QRegularExpression hash("^[0-9a-f]{64}$");
        const auto digest=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(work.serviceRequest).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
        if(device.isEmpty()||address.isEmpty()||!hash.match(work.genesis).hasMatch()||!hash.match(work.requestId).hasMatch()||!hash.match(work.reportRoot).hasMatch()||work.actor.isEmpty()||work.slot<0||work.slot>1||digest!=work.requestSha256||work.serviceRequest.value("schema")!="PGN_EXPLICIT_CPU_AUDIT_REQUEST_V1"){error_="Exact confirmed CPU assignment metadata required";return false;}
        const auto id=key(work);
        QJsonObject row{{"device",device},{"address",address},{"genesis",work.genesis},{"request",work.requestId},{"report_root",work.reportRoot},{"actor",work.actor},{"slot",work.slot},{"attempt",qint64(work.attempt)},{"request_sha256",work.requestSha256},{"service_request",work.serviceRequest}};
        if(entries_.contains(id)){
            const auto existing=entry(id);for(auto i=row.begin();i!=row.end();++i)if(existing.value(i.key())!=i.value()){error_="Retained CPU audit identity changed; no overwrite";return false;}
            error_.clear();return true;
        }
        auto next=entries_;next.insert(id,row);
        if(next.size()>EntryLimit||reservedSize(next)>ByteLimit){error_="CPU audit outbox has no reserved space for another complete result; no assignment adopted";return false;}
        return save(next);
    }
    bool retainComputation(const CpuAuditWork& work,const QJsonObject& result){
        if(!load())return false;const auto invalid=cpuAuditResultError(result,work.requestSha256);if(!invalid.isEmpty()){error_=invalid;return false;}
        const auto id=key(work);auto row=entry(id);
        if(row.isEmpty()||row.value("request_sha256")!=work.requestSha256||row.value("report_root")!=work.reportRoot||row.value("actor")!=work.actor||row.value("service_request")!=work.serviceRequest){error_="The confirmed CPU assignment must be retained before its result";return false;}
        if(row.contains("answer")){
            if(row.value("answer").toObject().value("counts")!=result.value("counts")){error_="Repeated CPU result changed its counts; original nonce retained";return false;}
            error_.clear();return true;
        }
        QByteArray nonce(32,'\0');for(int i=0;i<8;++i){const auto word=QRandomGenerator::system()->generate();for(int j=0;j<4;++j)nonce[4*i+j]=char(word>>(8*j));}
        QJsonArray partitions;for(const auto& item:work.serviceRequest.value("partitions").toArray())partitions.append(item.toObject().value("index"));
        const QJsonObject answer{{"schema","PGN_AUDIT_ANSWER_V1"},{"ticket",work.serviceRequest.value("ticket_id")},{"attempt",qint64(work.attempt)},{"partitions",partitions},{"counts",result.value("counts")},{"nonce",QString::fromLatin1(nonce.toHex())}};
        const auto bytes=QJsonDocument(answer).toJson(QJsonDocument::Compact);row.insert("answer",answer);row.insert("commitment",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()));
        auto next=entries_;next.insert(id,row);return save(next);
    }
    bool recordPublication(const QString& id,const QString& phase,const QString& blockHash){
        static const QRegularExpression hash("^[0-9a-f]{64}$");
        if(!load()||!entries_.contains(id)||!hash.match(blockHash).hasMatch()||(phase!="assignment"&&phase!="commit"&&phase!="reveal")){error_="Exact retained audit publication required";return false;}
        auto row=entry(id);row.insert(phase+"_block",blockHash);auto next=entries_;next.insert(id,row);return save(next);
    }
    // Caller must first verify the terminal verdict AND retain a durable,
    // rollback-recoverable archive. No timer deletes pending results here.
    bool eraseConfirmed(const QString& id){if(!load()||!entries_.contains(id))return false;auto next=entries_;next.remove(id);return save(next);}
};
}
