#pragma once
#include <QWidget>
namespace pgn::ui {
// Exact, bounded pages of the conserved endpoint catalogue. Loading this
// archive cannot block or modify the GPU worker, wallet or range ownership.
QWidget* createArchiveBrowser(const QString& root,QWidget* miningCanvas);
}
