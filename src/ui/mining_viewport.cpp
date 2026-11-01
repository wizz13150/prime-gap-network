#include "ui/mining_canvas.h"
#include "ui/render/mining_layout.h"
#include <QVBoxLayout>

namespace pgn::ui {
QWidget* createMiningViewport(QWidget* canvas, QWidget* parent)
{
    if (!canvas) return nullptr;
    auto* viewport = new QWidget(parent);
    viewport->setObjectName("pgn-mining-responsive-viewport");
    viewport->setMinimumHeight(MiningLayout::MinimumViewportHeight);
    auto* layout = new QVBoxLayout(viewport);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    canvas->setMinimumSize(0, 0);
    canvas->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    layout->addWidget(canvas);
    return viewport;
}
}
