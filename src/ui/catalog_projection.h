#pragma once
#include "ui/dense_point_layer.h"
#include <QString>
#include <atomic>
#include <memory>
#include <optional>
namespace pgn::ui {
inline constexpr QRgb ImportedObservationColor=qRgb(139,163,171); // unknown source device, not the live GPU colour
struct CatalogBounds {
 double minX{},maxX{},minY{},maxY{};qint64 points{},revision{};QString error;
};
struct CatalogYFit {
 bool enabled{};
 // A real selected overlay, never a fabricated zero/default. Snapshot identity
 // is part of the request/frame so a pending frame cannot paint a live target.
 std::optional<double> targetY;
 bool operator==(const CatalogYFit&) const=default;
};
// legacyR4Mode is a reused layout flag, not product identity. A populated
// staircase needs its own viewport snapshot contract; preserve that old path.
inline bool nativeWindowYFitAllowed(bool timeAxis,bool finiteWindow,bool manualZoom,bool populatedStaircase) noexcept {
 return timeAxis&&finiteWindow&&!manualZoom&&!populatedStaircase;
}
struct CatalogRaster {
 DenseView requestedView,view;CatalogYFit yFit;QImage image;qint64 points{},elapsedNs{},rowsRead{},fitPoints{};QString error,scope,axis;QRgb color{};bool complete{},incremental{};
};
// Read-only worker API, never called in the paint/mining loop. Memory is one
// raster plus a 256-point batch, independent of source catalogue length.
CatalogBounds readCatalogBounds(const QString& database,const QString& axis);
CatalogRaster renderCatalog(const QString& database,const QString& axis,const DenseView&,
 const std::shared_ptr<std::atomic_bool>& cancelled);
CatalogRaster renderNativeCatalog(const QString& database,const QString& scope,const QString& axis,const DenseView&,
 const std::shared_ptr<std::atomic_bool>& cancelled,QRgb color,const CatalogRaster* previous=nullptr,bool useCompressedPrefix=true,const CatalogYFit& yFit={});
}
