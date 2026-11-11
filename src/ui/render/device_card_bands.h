#pragma once
#include <QRectF>
#include <algorithm>
namespace pgn::ui {
// One band calculation for Total, GPU and CPU cards. Content determines the
// reserved header/footer rows; gauges can never consume those reserved bands.
struct DeviceCardBands {qreal centerY{},radius{},headerEnd{},footerStart{};};
inline DeviceCardBands deviceCardBands(const QRectF& card,int metadataRows,int footerRows,qreal preferredRadius){
    const qreal header=card.top()+28+12*metadataRows,footer=card.bottom()-4-12*footerRows;
    // Reserve a pixel on each side even when a larger requested gauge reaches
    // the available band; its label must not touch the footer or header.
    const qreal radius=std::max(1.,std::min(preferredRadius,(footer-header-20)/2));
    return {(header+footer-18)/2,radius,header,footer};
}
}
