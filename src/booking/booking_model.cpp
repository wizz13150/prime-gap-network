#include "booking/booking_model.h"
#include "booking/decimal_range.h"
#include "booking/round_range_catalog.h"
#include "booking/researcher_name.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QUuid>
#include <stdexcept>

namespace pgn::booking {
BookingModel::BookingModel(const QString& root,QObject* parent):QObject(parent),
    source_(loadSourceRegistry(root+"/data/runtime")),store_(root+"/data/runtime/local-bookings.sqlite"),root_(root){
    error_=source_.valid()?store_.error():source_.error;
    QFile profile(root+"/data/runtime/community-profile.json");
    if(profile.open(QIODevice::ReadOnly)&&profile.size()<=4096){
        const auto name=QJsonDocument::fromJson(profile.readAll()).object().value("display_name").toString().trimmed();
        if(!name.isEmpty()&&name.size()<=80&&!name.contains('\n')&&!name.contains('\r'))researcherName_=name;
    }
    // Exact historical completion is explicitly weaker than certification.
    // Load only a named compact receipt, never scan the old journal corpus.
    QFile receipt(root+"/evidence/source-completion-08462f1aef7a4b389999da01707c90ef.json");
    if(receipt.open(QIODevice::ReadOnly)&&receipt.size()<=16384){
        const auto data=QJsonDocument::fromJson(receipt.readAll()).object();
        if(data.value("schema")=="pgn-source-range-completion-1"&&data.value("source_range_complete").toBool()&&
           data.value("independent_coverage").isBool()&&!data.value("independent_coverage").toBool()){
            const auto error=store_.recordSourceCompletion(data.value("run").toString(),data.value("start").toString(),data.value("end").toString(),
                "GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac");if(!error.isEmpty())error_=error;
        }
    }
    refresh();
}
void BookingModel::subscribe(QObject* owner,std::function<void()> changed){listeners_.append({owner,std::move(changed)});}
QString BookingModel::setAutomaticAfterManual(const QString& device,bool enabled){
    if(!manualReservationsAllowed())return "PGN work is assigned automatically by consensus tickets.";
    const auto result=store_.setAutomaticAfterManual(device,enabled);
    if(result.isEmpty())notify(); // No optimistic UI state before the real commit.
    return result;
}
QString BookingModel::setResearcherName(const QString& name){
    if(!validResearcherName(name))return "Use 1–24 ASCII letters, digits, underscores or hyphens; start with a letter or digit.";
    const auto path=root_+"/data/runtime/community-profile.json";
    QLockFile lock(path+".lock");lock.setStaleLockTime(0);
    if(!lock.tryLock(0))return "Local profile is busy or its directory is unavailable. Try again after the other writer finishes.";
    if(QFileInfo(path).isSymLink())return "Refusing to replace a linked local profile.";
    QJsonObject object;
    QFile existing(path);
    if(existing.exists()){
        if(!existing.open(QIODevice::ReadOnly))return "Cannot read the existing local profile; it has not been changed.";
        const auto bytes=existing.read(4097);QJsonParseError parseError;
        const auto document=QJsonDocument::fromJson(bytes,&parseError);
        if(existing.error()!=QFileDevice::NoError||existing.size()>4096||bytes.size()>4096||parseError.error!=QJsonParseError::NoError||!document.isObject())
            return "Existing local profile is oversized or invalid; it has not been changed.";
        object=document.object();existing.close();
    }
    object.insert("display_name",name);
    const auto bytes=QJsonDocument(object).toJson(QJsonDocument::Compact);
    if(bytes.size()>4096)return "Updated local profile would exceed its supported size; it has not been changed.";
    QSaveFile saved(path);
    if(!saved.open(QIODevice::WriteOnly)||saved.write(bytes)!=bytes.size()||!saved.commit())
        return "Cannot save the local profile atomically; the displayed name has not been changed.";
    researcherName_=name;notify();return {};
}
void BookingModel::setViewBitLength(int bits){
    if(bits==catalog_.bits)return;
    if(!bits)catalog_={};else{
        if(bits<CatalogMinimumBits||bits>CatalogMaximumBits)return;
        auto candidate=bitLengthCatalog(bits,0,source_);
        if(!runtime_.start.isEmpty()&&bitLength(runtime_.start)==bits){const auto grid=decimalZoneIndex(runtime_.start);candidate=bitLengthCatalog(bits,(grid-candidate.firstGrid)/BitLengthCatalog::pageSize,source_);}
        catalog_=std::move(candidate);
    }
    notify();
}
void BookingModel::setViewPage(quint64 page){if(!catalog_.bits||page>=catalog_.pageCount()||page==catalog_.page)return;if(viewIsRoundRange())setRoundRange(page);else{catalog_=bitLengthCatalog(catalog_.bits,page,source_);notify();}}
void BookingModel::setRoundRange(quint64 page){if(page>roundRangeMaximumPage||(viewIsRoundRange()&&catalog_.page==page))return;catalog_=roundRangeCatalog(page,source_);notify();}
void BookingModel::notify(){
    for(qsizetype i=listeners_.size();i>0;--i){auto& listener=listeners_[i-1];if(!listener.owner){listeners_.removeAt(i-1);continue;}listener.changed();}
}
void BookingModel::refresh(){try{jobs_=store_.active();recentJobs_=store_.history(256);completedRanges_=store_.completedRanges();completedPositions_=store_.completedPositions();++jobsRevision_;}catch(const std::exception& e){error_=QString::fromUtf8(e.what());}notify();}
QString BookingModel::queue(const QString& first,const QString& last,const QString& device){
    if(!manualReservationsAllowed())return "Manual reservations are disabled on PGN. Work is assigned by consensus tickets.";
    const auto result=store_.queue(source_,first,last,device);if(result.isEmpty())refresh();return result;
}
QString BookingModel::release(const QString& id){const auto result=store_.release(id);if(result.isEmpty())refresh();return result;}
QString BookingModel::reserveBlocks(const QString& first,const QString& last,const QString& za,const QString& zb){
    if(!manualReservationsAllowed())return "Manual reservations are disabled on PGN. Work is assigned by consensus tickets.";
    const auto result=store_.queueAvailableBlocks(source_,first,last,za,zb,runtime_.device);if(result.isEmpty())refresh();return result;
}
QString BookingModel::releaseRange(const QString& first,const QString& last){const auto result=store_.releaseRange(first,last);if(result.isEmpty())refresh();return result;}
QString BookingModel::reserveSelection(const QVector<ExactBlockSelection>& spans){if(!manualReservationsAllowed())return "Manual reservations are disabled on PGN. Work is assigned by consensus tickets.";const auto result=store_.queueSelection(source_,spans,runtime_.device);if(result.isEmpty())refresh();return result;}
QString BookingModel::releaseSelection(const QVector<ExactBlockSelection>& spans){const auto result=store_.releaseSelection(spans);if(result.isEmpty())refresh();return result;}
QString BookingModel::reorderQueuedZones(const QVector<ExactBlockSelection>& zones,const QStringList& expectedIds){const auto result=store_.reorderQueuedZones(zones,expectedIds);refresh();return result;}
QString BookingModel::resetQueueOrder(){const auto result=store_.resetQueueOrder();refresh();return result;}
QString BookingModel::dispatch(const QString& id){
    if(!manualReservationsAllowed())return "Manual plans cannot become PGN network tickets. The original plan and history are retained.";
    for(const auto& job:jobs_)if(job.id==id&&job.kind=="LOCAL_INTENT"&&(job.state=="QUEUED"||job.state=="ARMED")){
        if(!onDispatch)return "Dispatch service unavailable; no work scheduled";
        onDispatch(id);return {};
    }
    return "Select an exact queued local intent; the current physical job remains unchanged";
}
void BookingModel::observeSourceCompletion(const QJsonObject& receipt,bool reloadSource){
    const auto run=receipt.value("run").toString();
    if(run==lastCompletionRun_&&!reloadSource)return;
    try{
        const auto first=canonicalUnsigned(receipt.value("start").toString()),last=canonicalUnsigned(receipt.value("end").toString());
        if(receipt.value("schema")!="pgn-source-range-completion-1"||!receipt.value("source_range_complete").toBool()||
           !receipt.value("independent_coverage").isBool()||receipt.value("independent_coverage").toBool()||
           receipt.value("source_positions").toString()!=subtractUnsigned(last,first))throw std::runtime_error("Source completion identity/scope invalid; not promoted");
        const auto error=store_.recordSourceCompletion(run,first,last,"GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac");
        if(!error.isEmpty())throw std::runtime_error(error.toStdString());
        lastCompletionRun_=run;if(reloadSource){source_=loadSourceRegistry(root_+"/data/runtime");if(!source_.valid())error_=source_.error;}refresh();
    }catch(const std::exception& e){error_=QString::fromUtf8(e.what());notify();}
}
void BookingModel::observeDeviceIdentity(const QString& device,const QString& label,const QDateTime& observedUtc){
    if(!runtime_.run.isEmpty()||!observedUtc.isValid()||!device.startsWith("GPU-")||label.isEmpty()||label.size()>256)return;
    const auto id=QUuid::fromString(device.mid(4));
    if(id.isNull()||id.toString(QUuid::WithoutBraces)!=device.mid(4).toLower())return;
    if(runtime_.device==device&&runtime_.label==label)return;
    runtime_.device=device;runtime_.label=label;notify();
}
void BookingModel::observeRuntime(const QString& run,const QString& first,const QString& last,const QString& device,
    const QString& label,const QString& positions,const QDateTime& observedUtc,bool running){
    const auto reject=[this](const QString& reason){observationError_=reason;notify();};
    try{
        if(run.isEmpty()||first.isEmpty()||last.isEmpty())return;
        const auto begin=canonicalUnsigned(first),end=canonicalUnsigned(last);
        if(compareUnsigned(begin,end)>=0||device.isEmpty()){
            reject("Physical job range/device invalid; previous reservation retained");return;
        }
        if(!observedUtc.isValid()){
            reject("Physical observation time unavailable; reservation and original progress age retained");return;
        }
        // Validate before any durable store transition. A late packet for an
        // old run must not pause the current one and then reset its time guard.
        if(latestObservationUtc_.isValid()&&observedUtc<latestObservationUtc_){
            reject("Out-of-order physical observation rejected; current reservation retained");return;
        }
        if(runtime_.run==run&&(runtime_.start!=begin||runtime_.end!=end||runtime_.device!=device)){
            reject("Physical job range/identity changed without a new run");return;
        }
        std::optional<QString> exact;
        std::optional<double> fraction;
        if(!positions.isEmpty()){
            exact=canonicalUnsigned(positions);
            const auto length=subtractUnsigned(end,begin);
            if(compareUnsigned(*exact,length)>0){
                reject("Producer progress exceeds exact range; previous reservation retained");return;
            }
            if(runtime_.run==run&&!runtime_.positions.isEmpty()&&compareUnsigned(*exact,runtime_.positions)<0){
                reject("Cumulative positions regressed within the same run; previous progress retained, source reset NOT hidden");return;
            }
            fraction=rangeFraction(addUnsigned(begin,*exact),begin,end);
        }
        const bool changed=runtime_.run!=run||runtime_.running!=running;
        if(changed){
            const auto error=store_.observeRuntime(run,begin,end,device,running);if(!error.isEmpty()){reject(error);return;}
            if(runtime_.run!=run)runtime_={};
            runtime_.run=run;runtime_.start=begin;runtime_.end=end;runtime_.device=device;runtime_.label=label;runtime_.running=running;
        }
        if(!runtime_.blockStartedUtc.isValid()){
            const auto saved=store_.savedSourceRunManifest(device,run);
            if(saved.status==SourceRunManifestReadStatus::Present&&saved.manifest.value("start")==begin&&saved.manifest.value("end")==end){
                const auto started=QDateTime::fromString(saved.blockStartedUtc,Qt::ISODateWithMs);
                if(started.isValid()&&started<=observedUtc)runtime_.blockStartedUtc=started;
            }
        }
        latestObservationUtc_=observedUtc;
        observationError_.clear();
        if(exact){
            runtime_.positions=*exact;runtime_.observedUtc=observedUtc;runtime_.fraction=fraction;
        }
        // Subscribers see one coherent accepted identity/progress transition.
        // Missing counters preserve the original values AND measurement time.
        if(changed)refresh();else if(exact)notify();
    }catch(const std::exception& e){reject(QString::fromUtf8(e.what()));}
}
}
