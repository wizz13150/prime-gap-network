#pragma once
#include "wallet/difficulty_history.h"
#include "ui/dense_point_layer.h"
#include <atomic>
#include <memory>
class QWidget;
namespace pgn::ui {
struct DifficultyRaster {
 DenseView view;QImage image;QString genesis,tipHash,error;
 qint64 height{},points{},rowsRead{},elapsedNs{};bool complete{},incremental{};
};
DifficultyRaster renderDifficultyHistory(const QString& file,const pgn::wallet::DifficultySummary&,const DenseView&,
 const std::shared_ptr<std::atomic_bool>& cancel,const DifficultyRaster* previous=nullptr);
QWidget* createDifficultyHistoryView(const QString& file);
void presentDifficultyHistory(QWidget*,const pgn::wallet::DifficultySummary&);
void presentDifficultyHistoryError(QWidget*,const QString& error);
}
