#include "wallet/audit_event_publisher.h"
#include "wallet/coordination_block.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QPointer>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

namespace pgn::wallet {
namespace {
constexpr qint64 JournalLimit=262144;
bool hash(const QString& value){static const QRegularExpression pattern("^[0-9a-f]{64}$");return pattern.match(value).hasMatch();}
bool retainedBlockIdentity(const QJsonObject& intent){
    static const QRegularExpression hex("^[0-9a-f]+$");
    const auto encoded=intent.value("block").toString(),envelope=intent.value("envelope").toString();
    const auto parent=intent.value("proposal").toObject().value("parent").toString();
    if(!hash(parent)||!hash(intent.value("block_hash").toString())||encoded.size()<=224||encoded.size()>JournalLimit||encoded.size()%2||
       !hex.match(encoded).hasMatch()||envelope.isEmpty()||envelope.size()%2||envelope.size()>160000||!hex.match(envelope).hasMatch())return false;
    const auto header=QByteArray::fromHex(encoded.toLatin1()).first(112);
    auto nativeParent=QByteArray::fromHex(parent.toLatin1());std::reverse(nativeParent.begin(),nativeParent.end());
    auto digest=QCryptographicHash::hash(QCryptographicHash::hash(header,QCryptographicHash::Sha256),QCryptographicHash::Sha256);std::reverse(digest.begin(),digest.end());
    return header.sliced(4,32)==nativeParent&&QString::fromLatin1(digest.toHex())==intent.value("block_hash").toString();
}
QJsonArray parameters(const AuditOperation& operation){
    return {operation.action,operation.address,operation.action=="assign"?QJsonValue{}:QJsonValue(operation.request),
        operation.action=="assign"?QJsonValue{}:QJsonValue(operation.slot),
        operation.action=="assign"?QJsonValue{}:QJsonValue(operation.attempt),
        operation.action=="assign"?QJsonValue{}:QJsonValue(operation.body)};
}
}
AuditEventPublisher::AuditEventPublisher(QString file,QString genesis,Rpc rpc,Sign sign,QObject* parent):
    QObject(parent),file_(std::move(file)),genesis_(std::move(genesis)),rpc_(std::move(rpc)),sign_(std::move(sign)){}
AuditEventPublisher::~AuditEventPublisher(){cancelled_->store(true);}
bool AuditEventPublisher::pending()const{return QFileInfo::exists(file_);}
void AuditEventPublisher::fail(const QString& error){
    busy_=false;auto completion=std::move(completion_);completion_={};
    if(completion)completion({},error);
}
bool AuditEventPublisher::persist(){
    const auto bytes=QJsonDocument(intent_).toJson(QJsonDocument::Compact);
    if(bytes.size()>JournalLimit||!QDir().mkpath(QFileInfo(file_).absolutePath()))return false;
    QSaveFile file(file_);if(!file.open(QIODevice::WriteOnly))return false;
    file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);
    return file.write(bytes)==bytes.size()&&file.commit();
}
void AuditEventPublisher::publish(AuditOperation operation,Completion completion,ProposalGate gate){
    if(busy_||pending()){completion({},"A retained audit publication must be recovered first");return;}
    static const QRegularExpression address("^[A-Za-z0-9]{14,128}$"),hex("^[0-9a-f]+$");
    const bool assignment=operation.action=="assign";
    if(!hash(genesis_)||!address.match(operation.address).hasMatch()||
       (!assignment&&operation.action!="commit"&&operation.action!="reveal")||
       (assignment&&(!operation.request.isEmpty()||!operation.body.isEmpty()||operation.slot!=-1||operation.attempt!=-1))||
       (!assignment&&(!hash(operation.request)||operation.slot<0||operation.slot>1||operation.attempt<0||operation.attempt>std::numeric_limits<quint32>::max()||
        operation.body.isEmpty()||operation.body.size()>131072||operation.body.size()%2||!hex.match(operation.body).hasMatch()))){completion({},"Invalid bounded audit operation");return;}
    busy_=true;completion_=std::move(completion);const QPointer<AuditEventPublisher> guard(this);const auto args=parameters(operation);
    rpc_("getpgnauditevent",args,[guard,args,operation,gate=std::move(gate)](QJsonValue value,QString error){
        if(!guard)return;if(!error.isEmpty()){guard->fail(error);return;}
        const auto proposal=value.toObject();
        if(proposal.value("available")!=true){const auto reason=proposal.value("reason").toString();guard->fail(reason.isEmpty()?QString("No independent audit assignment is available"):reason.left(240));return;}
        if(gate&&!gate(proposal)){guard->fail("Proposed audit exceeds this device's capabilities; no assignment signed");return;}
        if(proposal.value("accepted")!=false||!hash(proposal.value("parent").toString())||!hash(proposal.value("request_id").toString())||
           !proposal.value("challenge").toString().startsWith("PGN/UNAPPROVED/SCRIPT-WORK/1:")){guard->fail("Native audit proposal identity is invalid");return;}
        if(proposal.value("challenge").toString().size()>256||proposal.value("slot").toInteger(-1)<0||proposal.value("slot").toInteger(-1)>1||proposal.value("attempt").toInteger(-1)<0||(!operation.request.isEmpty()&&(proposal.value("request_id")!=operation.request||proposal.value("slot").toInteger()!=operation.slot||proposal.value("attempt").toInteger()!=operation.attempt))){guard->fail("Audit proposal does not match the requested operation");return;}
        guard->sign_(operation.address,proposal.value("challenge").toString(),[guard,args,proposal,operation](QJsonValue signature,QString error){
            if(!guard)return;if(!error.isEmpty()||!signature.isString()){guard->fail(error.isEmpty()?QString("Native wallet did not return a signature"):error);return;}
            auto signedArgs=args;signedArgs.append(signature);
            guard->rpc_("getpgnauditevent",signedArgs,[guard,proposal,operation](QJsonValue value,QString error){
                if(!guard)return;if(!error.isEmpty()){guard->fail(error);return;}
                const auto signedProposal=value.toObject();
                for(const auto* key:{"parent","request_id","slot","attempt","challenge","ranges"})if(signedProposal.value(key)!=proposal.value(key)){guard->fail("Audit parent or selection changed while signing");return;}
                const auto envelope=signedProposal.value("envelope").toString();
                static const QRegularExpression hex("^[0-9a-f]+$");
                if(envelope.isEmpty()||envelope.size()>160000||envelope.size()%2||!hex.match(envelope).hasMatch()){guard->fail("Native audit envelope is invalid or oversized");return;}
                guard->intent_={{"schema","PGN_AUDIT_PUBLICATION_V1"},{"genesis",guard->genesis_},{"action",operation.action},{"address",operation.address},{"proposal",proposal},{"envelope",envelope}};
                guard->build(proposal,envelope);
            });
        });
    });
}
void AuditEventPublisher::build(const QJsonObject& proposal,const QString& envelope){
    const QPointer<AuditEventPublisher> guard(this);
    rpc_("getpgnbootstraptemplate",{QJsonArray{envelope}},[guard,proposal](QJsonValue value,QString error){
        if(!guard)return;if(!error.isEmpty()){guard->fail(error);return;}
        const auto work=value.toObject();
        if(work.value("parent")!=proposal.value("parent")){guard->fail("Audit parent changed before publication");return;}
        auto* job=new QFutureWatcher<CoordinationBlockResult>(guard);
        QObject::connect(job,&QFutureWatcher<CoordinationBlockResult>::finished,guard,[guard,job]{
            const auto result=job->result();job->deleteLater();if(!guard)return;
            if(!result.candidate){guard->fail(result.error);return;}
            guard->intent_.insert("block",result.candidate->block);guard->intent_.insert("block_hash",result.candidate->hash);
            if(!guard->persist()){guard->fail("Audit publication could not be journaled; nothing submitted");return;}
            guard->rpc_("submitblock",{result.candidate->block},[guard](QJsonValue value,QString error){
                if(!guard)return;
                // A timeout is ambiguous. Keep the exact block and inspect it
                // on recovery; never create a replacement assignment blindly.
                if(!error.isEmpty()){guard->fail("Audit submit outcome is unknown; retained publication requires recovery: "+error);return;}
                if(!value.isNull()&&value.toString()!="duplicate"){guard->fail("Core held the audit publication; intent retained");return;}
                guard->inspectPublished();
            });
        });
        const auto cancelled=guard->cancelled_;const auto genesis=guard->genesis_,parent=proposal.value("parent").toString();
        job->setFuture(QtConcurrent::run([work,genesis,parent,cancelled]{return coordinationBlock(work,genesis,parent,0,1000000,cancelled.get());}));
    });
}
void AuditEventPublisher::inspectPublished(bool allowExactRetry){
    const QPointer<AuditEventPublisher> guard(this);const auto blockHash=intent_.value("block_hash").toString();
    intent_.remove("coordination_confirmed");
    rpc_("getblock",{blockHash,2},[guard,blockHash,allowExactRetry](QJsonValue value,QString error){
        if(!guard)return;if(!error.isEmpty()){
            if(allowExactRetry){guard->retryRetained();return;}
            guard->fail("Audit publication confirmation unavailable; retained: "+error);return;
        }
        const auto block=value.toObject();bool found=false;
        for(const auto& transaction:block.value("tx").toArray())for(const auto& output:transaction.toObject().value("vout").toArray())
            found=found||output.toObject().value("scriptPubKey").toObject().value("hex")==guard->intent_.value("envelope");
        if(block.value("hash")!=blockHash||block.value("confirmations").toInteger()<1||!found){guard->fail("Signed audit envelope is not confirmed on the active branch; retained");return;}
        guard->intent_.insert("coordination_confirmed",true);
        if(!guard->persist()){guard->fail("Audit confirmation could not be journaled; publication retained");return;}
        guard->busy_=false;auto completion=std::move(guard->completion_);guard->completion_={};
        if(completion)completion(guard->intent_,{});
    });
}
void AuditEventPublisher::retryRetained(){
    const QPointer<AuditEventPublisher> guard(this);const auto parent=intent_.value("proposal").toObject().value("parent").toString();
    rpc_("getbestblockhash",{},[guard,parent](QJsonValue value,QString error){
        if(!guard)return;
        // Retrying the identical block is idempotent. A replacement proposal,
        // signature or assignment would require a new coherent branch check.
        if(!error.isEmpty()||value.toString()!=parent||!retainedBlockIdentity(guard->intent_)){
            guard->fail("Audit publication retained: original parent is not the verified current tip; no replacement signed");return;
        }
        guard->rpc_("submitblock",{guard->intent_.value("block")},[guard](QJsonValue value,QString error){
            if(!guard)return;
            if(!error.isEmpty()||(!value.isNull()&&value.toString()!="duplicate")){
                guard->fail("Exact audit retransmission is unconfirmed; original publication retained");return;
            }
            guard->inspectPublished(false);
        });
    });
}
void AuditEventPublisher::recover(Completion completion){
    if(busy_){completion({},"Audit publisher is busy");return;}
    QFile file(file_);if(!file.open(QIODevice::ReadOnly)||file.size()>JournalLimit){completion({},"Retained audit publication is unavailable or oversized");return;}
    const auto intent=QJsonDocument::fromJson(file.readAll()).object();file.close();
    if(intent.value("schema")!="PGN_AUDIT_PUBLICATION_V1"||intent.value("genesis")!=genesis_||!retainedBlockIdentity(intent)){completion({},"Retained audit publication identity is invalid");return;}
    intent_=intent;busy_=true;completion_=std::move(completion);inspectPublished(true);
}
bool AuditEventPublisher::acknowledge(const QString& blockHash){
    if(busy_||!hash(blockHash)||intent_.value("block_hash")!=blockHash||intent_.value("coordination_confirmed")!=true)return false;
    if(!QFile::remove(file_))return false;intent_={};return true;
}
}
