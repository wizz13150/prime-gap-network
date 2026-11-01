#pragma once
#include <QRectF>
#include <algorithm>

namespace pgn::ui {
// Logical pixels, not a painter scale: text/gauges keep their native size.
// Small windows use explicit section tabs; none of the sections is discarded.
struct MiningLayout {
    // Closed real-painter regression: at360px the work summary falls to77px;
    // at400px every section retains readable, nonoverlapping native-size text.
    static constexpr int MinimumViewportHeight=400;
    static int minimumWindowHeight(int chromeHeight,int availableClientHeight) {
        return std::min(std::max(1,availableClientHeight),
                        std::max(0,chromeHeight)+MinimumViewportHeight);
    }
    QRectF scene, cards, work, graph;
    bool sections{}, sideBySide{};
    static MiningLayout fit(qreal width, qreal height) {
        MiningLayout l;
        l.scene = QRectF(12, 82, std::max(1., width - 24), std::max(1., height - 108));
        l.sections = height < 560 || (width < 1000 && height < 850);
        l.sideBySide = !l.sections && height < 850;
        const qreal top = l.scene.top() + (l.sections ? 30 : 0);
        l.cards = QRectF(l.scene.left()+4, top+4, l.scene.width()-8, 172);
        if (l.sections) {
            l.work = l.graph = QRectF(l.scene.left()+4, top+4, l.scene.width()-8,
                                      std::max(1., l.scene.bottom()-top-8));
        } else if (l.sideBySide) {
            const qreal y = l.cards.bottom()+8;
            const qreal w = std::clamp(l.scene.width()*.47, 430., 620.);
            l.work = QRectF(l.scene.left()+4, y, w, l.scene.bottom()-y-4);
            l.graph = QRectF(l.work.right()+12, y, l.scene.right()-l.work.right()-16,
                             l.work.height());
        } else {
            const qreal graphHeight = std::clamp(l.scene.height()*.36, 230., 390.);
            l.graph = QRectF(l.scene.left()+4, l.scene.bottom()-graphHeight-4,
                             l.scene.width()-8, graphHeight);
            const qreal y = l.cards.bottom()+8;
            l.work = QRectF(l.scene.left()+4, y, l.scene.width()-8, l.graph.top()-y-8);
        }
        return l;
    }
};
}
