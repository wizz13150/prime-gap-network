#pragma once
#include <QRectF>
#include <algorithm>
namespace pgn::ui {
// One band calculation for Total, GPU and CPU cards. Content determines the
// reserved header/footer rows; gauges can never consume those reserved bands.
struct DeviceCardBands {qreal centerY{},radius{},headerEnd{},footerStart{};};
inline DeviceCardBands deviceCardBands(const QRectF& card,int metadataRows,int footerRows,qreal preferredRadius){
    const qreal header=card.top()+28+12*metadataRows,footer=card.bottom()-4-12*footerRows;
    const qreal radius=std::max(1.,std::min(preferredRadius,(footer-header-18)/2));
    return {(header+footer-18)/2,radius,header,footer};
}
}
