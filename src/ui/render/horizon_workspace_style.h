#pragma once

#include <QString>

// Shared control skin for every Horizon workspace.  Keeping the complete
// widget-state palette in one place prevents Qt/Windows native colours from
// leaking black text, white corners or unthemed sub-controls into dark pages.
inline QString HorizonWorkspaceControlStyle()
{
    return QStringLiteral(R"QSS(
        QCheckBox { color:#c9eee7; spacing:10px; padding:4px 0; }
        QCheckBox::indicator { width:19px; height:19px; border:2px solid #57bfc5; border-radius:4px; background:#092c36; }
        QCheckBox::indicator:hover { border-color:#a6ffec; background:#12434a; }
        QCheckBox::indicator:checked { border-color:#9bffe3; background:#69efc5; image:url(:/frontier/checkbox-tick.svg); }
        QCheckBox:disabled { color:#7da9aa; }
        QCheckBox::indicator:disabled { border-color:#42666f; background:#10252d; }
        QCheckBox::indicator:checked:disabled { border-color:#59a99b; background:#389384; image:url(:/frontier/checkbox-tick.svg); }
        QLabel { color:#c9eee7; background:transparent; }
        QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
            min-height:34px; background:#03131a; color:#e8fffb;
            border:1px solid #286672; border-radius:8px;
            padding:0 11px; selection-background-color:#176568;
            selection-color:#ffffff;
        }
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus {
            border:1px solid #69efc5; background:#051b22;
        }
        QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled,
        QDoubleSpinBox:disabled {
            color:#557982; background:#041118; border-color:#173a43;
        }
        QComboBox { padding-right:34px; }
        QComboBox::drop-down {
            subcontrol-origin:padding; subcontrol-position:top right;
            width:30px; background:#08242d; border:0;
            border-left:1px solid #286672;
            border-top-right-radius:7px; border-bottom-right-radius:7px;
        }
        QComboBox::down-arrow {
            image:url(:/frontier/chevron-down.svg); width:11px; height:7px;
        }
        QComboBox QAbstractItemView {
            background:#061a22; color:#e8fffb; border:1px solid #2a737e;
            selection-background-color:#0d4948; selection-color:#ffffff;
            outline:0; padding:5px;
        }
        QSpinBox, QDoubleSpinBox { padding-right:34px; }
        QSpinBox::up-button, QDoubleSpinBox::up-button,
        QSpinBox::down-button, QDoubleSpinBox::down-button {
            subcontrol-origin:border; width:32px; background:#0b333d;
            border-left:1px solid #4aa0a9;
        }
        QSpinBox::up-button, QDoubleSpinBox::up-button {
            subcontrol-position:top right; border-top-right-radius:7px;
        }
        QSpinBox::down-button, QDoubleSpinBox::down-button {
            subcontrol-position:bottom right; border-bottom-right-radius:7px;
        }
        QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {
            image:url(:/frontier/chevron-up.svg); width:14px; height:9px;
        }
        QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {
            image:url(:/frontier/chevron-down.svg); width:14px; height:9px;
        }
        QPushButton {
            min-height:34px; background:#0b3939; color:#69efc5;
            border:1px solid #4bc9b0; border-radius:9px;
            padding:0 15px; font:700 9px monospace;
        }
        QPushButton:hover { background:#105252; color:#ffffff; border-color:#7fffe0; }
        QPushButton:pressed { background:#082d31; padding-top:1px; }
        QPushButton:disabled { color:#52757d; border-color:#23424a; background:#06171e; }
        QPlainTextEdit, QTableWidget {
            background:#020f15; color:#c6f7ef; border:1px solid #174b56;
            border-radius:9px; selection-background-color:#0d4948;
            selection-color:#ffffff; font:9px monospace;
        }
        QTableWidget { gridline-color:#123a44; alternate-background-color:#05212a; }
        QHeaderView::section {
            background:#082c34; color:#69efc5; border:0;
            border-right:1px solid #245e69; border-bottom:1px solid #245e69;
            padding:7px; font:700 8px monospace;
        }
        QTableCornerButton::section, QAbstractScrollArea::corner {
            background:#082c34; border:0;
        }
        QScrollBar:vertical { background:#03131a; width:11px; margin:2px; }
        QScrollBar::handle:vertical { background:#23606a; min-height:28px; border-radius:4px; }
        QScrollBar::handle:vertical:hover { background:#45a999; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
        QScrollBar:horizontal { background:#03131a; height:11px; margin:2px; }
        QScrollBar::handle:horizontal { background:#23606a; min-width:28px; border-radius:4px; }
        QScrollBar::handle:horizontal:hover { background:#45a999; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; }
        QSplitter::handle { background:#123b45; }
        QSplitter::handle:hover { background:#69efc5; }
        QTabWidget::pane {
            background:#020f15; border:1px solid #174b56;
            border-radius:9px; top:-1px;
        }
        QTabBar::tab {
            min-height:25px; min-width:150px; padding:0 14px;
            background:#03131a; color:#91aaa8; border:0;
            border-bottom:1px solid #245e69; font:700 8px monospace;
        }
        QTabBar::tab:selected {
            background:#0b3939; color:#f1fffc;
            border-bottom:2px solid #69efc5;
        }
        QTabBar::tab:hover { background:#105252; color:#ffffff; }
        QToolTip {
            background:#071820; color:#e8fffb; border:1px solid #4bc9b0;
            padding:6px; font:9px monospace;
        }
    )QSS");
}
