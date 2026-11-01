#include "ui/catalog_layer.h"
#include "mining/source_history.h"
#include <QtConcurrent/QtConcurrentRun>
#include <utility>
#include <algorithm>

namespace pgn::ui {
void loadNativeArchiveIndex(QString database,QObject& owner,std::function<void(const QJsonObject&)> publish){
    auto* watcher=new QFutureWatcher<QJsonObject>(&owner);
    const auto cancelled=std::make_shared<std::atomic_bool>(false);
    QObject::connect(&owner,&QObject::destroyed,watcher,[cancelled]{cancelled->store(true,std::memory_order_relaxed);});
    QObject::connect(watcher,&QFutureWatcher<QJsonObject>::finished,&owner,[watcher,publish=std::move(publish)]{
        const auto packet=watcher->result();watcher->deleteLater();if(publish)publish(packet);
    });
    watcher->setFuture(QtConcurrent::run([database=std::move(database),cancelled]{
        try{pgn::mining::SourceHistoryIndexReader reader(database);return reader.readArchive([&]{return cancelled->load(std::memory_order_relaxed);});}
        catch(const std::exception& e){return QJsonObject{{"type","native_index"},{"archive_only",true},{"complete",false},{"error",QString::fromUtf8(e.what()).left(400)}};}
    }));
}
QJsonObject CatalogLayer::diagnostics() const {
    const auto describe=[](const std::optional<Request>& request){
        if(!request)return QJsonObject{};
        const auto& v=request->view;
        return QJsonObject{{"axis",request->axis},{"scope",request->scope},
            {"generation",qint64(request->generation)},{"revision",qint64(v.revision)},
            {"min_x",v.minX},{"max_x",v.maxX},{"min_y",v.minY},{"max_y",v.maxY},
            {"width",v.pixels.width()},{"height",v.pixels.height()}};
    };
    return {{"ready",ready_},{"running",running_},{"error",error_},
        {"cancelled",cancelled_&&cancelled_->load(std::memory_order_relaxed)},
        {"desired",describe(desired_)},{"in_flight",describe(inFlight_)},
        {"published_complete",published_.complete},{"published_points",published_.points},
        {"published_revision",qint64(published_.view.revision)},{"serial",qint64(serial_)}};
}
bool CatalogLayer::sameGeometry(const Request& a,const Request& b){
    auto geometry=a.view;geometry.revision=b.view.revision;
    return a.axis==b.axis&&a.scope==b.scope&&a.generation==b.generation&&a.yFit==b.yFit&&geometry==b.view;
}
CatalogLayer::CatalogLayer(QString database, std::function<void()> changed, QObject* parent,bool native,QRgb color)
    : QObject(parent), database_(std::move(database)), changed_(std::move(changed)),native_(native),color_(color) {
    connect(&boundsWatcher_, &QFutureWatcher<Bounds>::finished, this, [this] {
        auto result=boundsWatcher_.result();
        length_=std::move(result.length);time_=std::move(result.time);
        error_=!length_.error.isEmpty()?length_.error:time_.error;
        ready_=error_.isEmpty();++serial_;
        if(changed_)changed_();
    });
    connect(&rasterWatcher_, &QFutureWatcher<CatalogRaster>::finished, this, [this] {
        auto result=rasterWatcher_.result();running_=false;
        const bool current=desired_&&inFlight_&&*desired_==*inFlight_;
        // A -> B -> A can return to the in-flight view after its token was
        // cancelled. Cancellation is not a completed publication: restart A.
        if(current&&!result.complete&&cancelled_->load(std::memory_order_relaxed)){
            startLatest();return;
        }
        // Append-only native coordinates are filtered by admission_revision.
        // A complete older prefix is an exact frame, not a partial result.
        // Keep its actual revision and use it as the base for the newest delta
        // instead of starving the initial full raster under continuous input.
        const bool prefix=native_&&desired_&&inFlight_&&result.complete&&
            sameGeometry(*desired_,*inFlight_)&&
            inFlight_->view.revision<desired_->view.revision&&
            !cancelled_->load(std::memory_order_relaxed);
        if(current||prefix){
            error_=result.error;
            if(result.complete){published_=std::move(result);publishedAxis_=inFlight_->axis;remember(published_);}
            ++serial_;if(changed_)changed_();
        }
        if(desired_&&inFlight_&&*desired_!=*inFlight_)startLatest();
    });
    if(native_)return; // metadata arrives from the committed producer-side index, not a paint-time scan
    const auto file=database_;
    boundsWatcher_.setFuture(QtConcurrent::run([file] {
        return Bounds{readCatalogBounds(file,"length"),readCatalogBounds(file,"time")};
    }));
}
void CatalogLayer::publishBounds(const QString& scope,const CatalogBounds& length,const CatalogBounds& time){
    if(!native_||(ready_&&scope_==scope&&length_.revision==length.revision&&time_.revision==time.revision))return;
    const bool reset=scope_!=scope||(ready_&&(length.revision<length_.revision||time.revision<time_.revision));
    if(reset){
        ++generation_;published_={};cache_.clear();desired_.reset();
        if(cancelled_)cancelled_->store(true,std::memory_order_relaxed);
    }
    scope_=scope;length_=length;time_=time;ready_=true;error_.clear();
    ++serial_;if(changed_)changed_();
}
CatalogLayer::~CatalogLayer(){
    if(cancelled_)cancelled_->store(true,std::memory_order_relaxed);
    // Workers capture only value data, not this controller or its QWidget.
}
void CatalogLayer::request(const QString& axis,const DenseView& view,const CatalogYFit& yFit){
    const Request wanted{axis,view,scope_,generation_,yFit};
    if(!ready_||(desired_&&*desired_==wanted))return;
    desired_=wanted;error_.clear();++serial_;
    if(const auto* hit=cached(wanted,true)){
        // Copy before touching the LRU: erasing its entry invalidates pointers.
        published_=*hit;publishedAxis_=axis;remember(published_);++cacheHits_;
        if(running_)cancelled_->store(true,std::memory_order_relaxed);
        if(changed_)changed_();
        return;
    }
    if(running_){
        const bool appendOnly=native_&&inFlight_&&sameGeometry(wanted,*inFlight_)&&
            wanted.view.revision>inFlight_->view.revision;
        if(!appendOnly)cancelled_->store(true,std::memory_order_relaxed);
        return;
    }
    startLatest();
}
void CatalogLayer::startLatest(){
    if(!desired_||running_)return;
    // Cancelled B may finish after A was immediately restored from the cache.
    // A is already complete; do not launch a redundant worker for it.
    if(raster(desired_->axis,desired_->view,desired_->yFit))return;
    inFlight_=desired_;running_=true;
    cancelled_=std::make_shared<std::atomic_bool>(false);
    const auto request=*inFlight_;const auto file=database_;const auto token=cancelled_;const bool native=native_;const auto color=color_;
    const auto* base=cached(request,false);
    const auto previous=base?*base:published_;
    rasterWatcher_.setFuture(QtConcurrent::run([file,request,token,native,color,previous] {
        return native?renderNativeCatalog(file,request.scope,request.axis,request.view,token,color,&previous,true,request.yFit):renderCatalog(file,request.axis,request.view,token);
    }));
}
const CatalogRaster* CatalogLayer::raster(const QString& axis,const DenseView& view,const CatalogYFit& yFit) const {
    return published_.complete&&publishedAxis_==axis&&published_.requestedView==view&&published_.yFit==yFit?&published_:nullptr;
}
const CatalogRaster* CatalogLayer::lastComplete(const QString& axis) const {
    return published_.complete&&publishedAxis_==axis&&published_.scope==scope_?&published_:nullptr;
}
qint64 CatalogLayer::cacheBytes() const {
    qint64 bytes=0;for(const auto& frame:cache_)bytes+=frame.image.sizeInBytes();return bytes;
}
const CatalogRaster* CatalogLayer::cached(const Request& request,bool exactRevision) const {
    for(auto i=cache_.rbegin();i!=cache_.rend();++i){
        auto geometry=request.view;
        if(!exactRevision)geometry.revision=i->requestedView.revision;
        if(i->complete&&i->axis==request.axis&&i->scope==request.scope&&i->color==color_&&
           i->requestedView==geometry&&i->yFit==request.yFit&&(exactRevision||i->requestedView.revision<request.view.revision))return &*i;
    }
    return nullptr;
}
void CatalogLayer::remember(const CatalogRaster& frame){
    if(!frame.complete||frame.image.isNull())return;
    cache_.erase(std::remove_if(cache_.begin(),cache_.end(),[&](const auto& old){
        auto geometry=frame.requestedView;geometry.revision=old.requestedView.revision;
        return old.axis==frame.axis&&old.scope==frame.scope&&old.yFit==frame.yFit&&old.requestedView==geometry;
    }),cache_.end());
    if(frame.image.sizeInBytes()>CacheByteLimit)return;
    cache_.push_back(frame);
    while(cache_.size()>CacheFrameLimit||cacheBytes()>CacheByteLimit)cache_.erase(cache_.begin());
}
}
