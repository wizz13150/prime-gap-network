#pragma once
#include "ui/catalog_projection.h"
#include <QPainter>

namespace pgn::ui {
// Caption belongs to the displayed COMPLETE frame, not to the worker's busy
// state. A newer admission must not toggle a whole paragraph at producer rate.
inline QString catalogFrameCaption(const CatalogRaster* frame,const QString& error,
                                  const QString& source=QStringLiteral("FRAME")) {
    if(!frame)return error.isEmpty()?source+QStringLiteral(" · LOADING"):
                                   source+QStringLiteral(" · INDEX ERROR");
    const auto caption=QStringLiteral("%1 · %2 POINTS · REV %3")
        .arg(source).arg(frame->points).arg(frame->view.revision);
    return error.isEmpty()?caption:caption+QStringLiteral(" · INDEX ERROR");
}
// A retained frame is transformed into the displayed linear axes. Its source
// revision and count remain unchanged; no stale coordinates against new axes.
inline void paintCatalogFrame(QPainter& painter,const QRectF& graph,
                              const DenseView& desired,const CatalogRaster* frame) {
    if(!frame)return;
    const double xspan=desired.maxX-desired.minX,yspan=desired.maxY-desired.minY;
    if(xspan<=0||yspan<=0)return;
    const auto& old=frame->view;
    const QRectF target(graph.left()+(old.minX-desired.minX)/xspan*graph.width(),
        graph.top()+(desired.maxY-old.maxY)/yspan*graph.height(),
        (old.maxX-old.minX)/xspan*graph.width(),(old.maxY-old.minY)/yspan*graph.height());
    painter.save();painter.setClipRect(graph,Qt::IntersectClip);
    painter.drawImage(target,frame->image);painter.restore();
}
}
