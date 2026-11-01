#pragma once
#include "ui/catalog_projection.h"
#include <QObject>
#include <QJsonObject>
#include <QFutureWatcher>
#include <functional>
#include <optional>
#include <vector>

namespace pgn::ui {
// One application-start read, independent of remote engine/control state.
// The worker owns values only; destroying owner cancels SQL and delivery.
void loadNativeArchiveIndex(QString database,QObject& owner,std::function<void(const QJsonObject&)> publish);
// UI-thread controller; at most one raster worker and one latest desired view.
// Superseded work is cancelled, never queued once per repaint/mouse movement.
class CatalogLayer final : public QObject {
public:
    explicit CatalogLayer(QString database, std::function<void()> changed, QObject* parent=nullptr,bool native=false,QRgb color=ImportedObservationColor);
    ~CatalogLayer() override;
    const CatalogBounds& lengthBounds() const { return length_; }
    const CatalogBounds& timeBounds() const { return time_; }
    bool ready() const { return ready_; }
    bool busy() const { return running_; }
    std::uint64_t serial() const { return serial_; }
    const QString& error() const { return error_; }
    const QString& scope() const { return scope_; }
    void publishBounds(const QString& scope,const CatalogBounds& length,const CatalogBounds& time);
    void request(const QString& axis, const DenseView&,const CatalogYFit& yFit={});
    const CatalogRaster* raster(const QString& axis, const DenseView&,const CatalogYFit& yFit={}) const;
    // A separately identified last COMPLETE frame may remain visible during
    // refresh. It never claims the desired revision or changes raw counters.
    const CatalogRaster* lastComplete(const QString& axis) const;
    // Complete pixels only; raw observations stay in the immutable catalogue.
    // Bounded LRU makes an already calculated timeline/axis view reusable.
    std::size_t cachedFrames() const { return cache_.size(); }
    qint64 cacheBytes() const;
    std::uint64_t cacheHits() const { return cacheHits_; }
    QJsonObject diagnostics() const;
    static constexpr qint64 CacheByteLimit=8*1024*1024;
    static constexpr std::size_t CacheFrameLimit=4;
private:
    struct Request { QString axis; DenseView view;QString scope;std::uint64_t generation{};CatalogYFit yFit; bool operator==(const Request&) const=default; };
    static bool sameGeometry(const Request&,const Request&);
    struct Bounds { CatalogBounds length, time; };
    void startLatest();
    void remember(const CatalogRaster&);
    const CatalogRaster* cached(const Request&, bool exactRevision) const;
    QString database_, error_, publishedAxis_;
    std::function<void()> changed_;
    QFutureWatcher<Bounds> boundsWatcher_;
    QFutureWatcher<CatalogRaster> rasterWatcher_;
    CatalogBounds length_, time_;
    CatalogRaster published_;
    std::vector<CatalogRaster> cache_; // oldest first, image data implicitly shared
    std::optional<Request> desired_, inFlight_;
    std::shared_ptr<std::atomic_bool> cancelled_;
    bool ready_{}, running_{};
    bool native_{};QString scope_;QRgb color_;
    std::uint64_t serial_{};
    std::uint64_t generation_{}; // invalidate already-running work on scope/revision reset
    std::uint64_t cacheHits_{};
};
}
