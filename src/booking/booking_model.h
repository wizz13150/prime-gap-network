#include "app/horizon_user_paths.h"
#pragma once
#include "booking/booking_store.h"
#include "booking/bit_length_catalog.h"
#include <QObject>
#include <QPointer>
#include <QJsonObject>
#include <functional>
#include <optional>

namespace pgn::booking {
struct RuntimeProgress {QString run,start,end,device,label,positions;QDateTime observedUtc,blockStartedUtc;bool running{};std::optional<double> fraction;};
class BookingModel final : public QObject {
public:
    explicit BookingModel(const QString& root,QObject* parent=nullptr);
    const SourceRegistry& source()const{return source_;}
    // Production work is assigned by consensus. Manual selection is permitted
    // only in an explicitly marked, isolated engine-validation registry.
    bool manualReservationsAllowed()const{return source_.valid()&&source_.localValidation;}
    const QVector<SourceRange>& viewRanges()const{return catalog_.bits?catalog_.ranges:source_.ranges;}
    int viewBitLength()const{return std::max(0,catalog_.bits);}
    bool viewIsRoundRange()const{return catalog_.bits<0;}
    quint64 viewPage()const{return catalog_.page;}
    quint64 viewPages()const{return catalog_.bits?catalog_.pageCount():1;}
    quint64 viewCount()const{return viewIsRoundRange()?quint64(catalog_.ranges.size()):catalog_.bits?catalog_.count:quint64(source_.ranges.size());}
    quint64 viewFirstOrdinal()const{return viewIsRoundRange()?1:catalog_.bits?catalog_.firstOrdinal():1;}
    QString viewKey()const{return QString("%1/%2").arg(catalog_.bits).arg(catalog_.page);}
    void setViewBitLength(int bits);
    void setViewPage(quint64 page);
    void setRoundRange(quint64 page);
    const QVector<Booking>& jobs()const{return jobs_;}
    const QVector<Booking>& recentJobs()const{return recentJobs_;}
    const QVector<Booking>& completedRanges()const{return completedRanges_;}
    quint64 jobsRevision()const{return jobsRevision_;}
    const RuntimeProgress& runtime()const{return runtime_;}
    const QString& researcherName()const{return researcherName_;}
    QString setResearcherName(const QString& name);
    std::optional<bool> automaticAfterManual(const QString& device) const {return store_.automaticAfterManual(device);}
    QString setAutomaticAfterManual(const QString& device,bool enabled);
    const QString& completedPositions()const{return completedPositions_;}
    QString error()const{return observationError_.isEmpty()?error_:error_.isEmpty()?observationError_:error_+"\n"+observationError_;}
    QString historyDatabase()const{return pgn::app::mutableDataRoot(root_)+"/runtime/local-bookings.sqlite";}
    void subscribe(QObject* owner,std::function<void()> changed);
    QString queue(const QString& start,const QString& end,const QString& device);
    int availableBlocks(const QString& start,const QString& end,const QString& zoneStart,const QString& zoneEnd) const {
        return store_.availableBlocks(source_,start,end,zoneStart,zoneEnd);
    }
    QString reserveBlocks(const QString& start,const QString& end,const QString& zoneStart,const QString& zoneEnd);
    int availableSelection(const QVector<ExactBlockSelection>& spans,QString* reason=nullptr)const{return store_.availableSelection(source_,spans,reason);}
    QString reserveSelection(const QVector<ExactBlockSelection>&);
    QString releaseSelection(const QVector<ExactBlockSelection>&);
    QString release(const QString& id);
    QString releaseRange(const QString& start,const QString& end);
    QString reorderQueuedZones(const QVector<ExactBlockSelection>& zones,const QStringList& expectedQueuedIds);
    QString resetQueueOrder();
    QString dispatch(const QString& id);
    void reloadDispatchState(){refresh();}
    std::function<void(const QString&)> onDispatch;
    void observeSourceCompletion(const QJsonObject&,bool reloadSource);
    // Fresh hardware identity enables local choices before the first job. It
    // supplies no range, progress, process ownership or execution permission.
    void observeDeviceIdentity(const QString& device,const QString& label,const QDateTime& observedUtc);
    void observeRuntime(const QString& run,const QString& start,const QString& end,const QString& device,
                        const QString& label,const QString& positions,const QDateTime& observedUtc,bool running);
    std::function<void(const QString&)> onSelect;
    std::function<void()> onMining;
    void select(const QString& id){if(onSelect)onSelect(id);}
private:
    void refresh();void notify();
    SourceRegistry source_;BitLengthCatalog catalog_;BookingStore store_;QVector<Booking> jobs_,recentJobs_,completedRanges_;
    RuntimeProgress runtime_;QString error_,observationError_;
    QDateTime latestObservationUtc_; // State-only packets never freshen the displayed progress sample.
    quint64 jobsRevision_{};
    QString root_,lastCompletionRun_,completedPositions_{"0"},researcherName_{"Your work"};
    struct Listener{QPointer<QObject> owner;std::function<void()> changed;};QVector<Listener> listeners_;
};
}
