#pragma once
#include <QString>
namespace pgn::ui {
// Shared PGN data-panel styling. Mining retains its original painted Horizon
// renderer; tables/forms use the same palette without per-frame style work.
inline QString dataPanelStyle(){return
 "QWidget{background:#010b10;color:#c9eee7;font-family:'Segoe UI';font-size:12px;}"
 "QLabel{background:transparent;} QLineEdit,QComboBox{min-height:0;min-width:0;background:#08242d;border:1px solid #286d73;border-radius:7px;padding:7px;}"
 "QAbstractSpinBox{min-height:0;min-width:0;} QAbstractSpinBox QLineEdit{min-height:0;min-width:0;}"
 "QPushButton{min-height:0;min-width:0;background:#0a333a;color:#bdfbea;border:1px solid #337e85;border-radius:7px;padding:7px;}"
 "QPushButton:hover{background:#13545a;border-color:#69efc5;}"
 "QPushButton:disabled{color:#658387;border-color:#244248;}"
 "QTableWidget{background:#031820;alternate-background-color:#08242b;gridline-color:#163b46;border:1px solid #28616d;border-radius:7px;}"
 "QTableWidget::item{border:0;padding:3px;} QTableWidget::item:selected{background:#164a4b;color:#e6fffa;}"
 "QHeaderView::section{background:#0a2933;color:#a8dfdb;border:0;padding:6px;} QSplitter::handle{background:#15363e;}";
}
}
