#pragma once
#include <QString>
namespace pgn::ui {
// Operator-directed mixed surfaces: Horizon chrome; neutral reading/editing.
// Selectors are confined to this component and exclude primary/subtab chrome.
inline QString SocialLightStyle(){return QStringLiteral(R"QSS(
QWidget#pgn-social-workspace {background:#041319;}
QWidget[socialSurface="true"], QWidget[socialSurface="true"] QWidget {
 background:#041319;color:#c9eee7;font:14px "Segoe UI";
}
QWidget[socialSurface="true"] QLabel {background:transparent;}
QLabel[socialSurface="true"] {background:#041319;color:#9db6b9;font:12px "Segoe UI";}
QWidget[socialSurface="true"] QLabel[caption="true"] {font:12px "Segoe UI";color:#9db6b9;}
QWidget[socialSurface="true"] QLabel[heading="true"] {font:600 16px "Segoe UI";color:#e8fffb;}
QWidget[socialSurface="true"] QLineEdit, QWidget[socialSurface="true"] QPlainTextEdit,
QWidget[socialSurface="true"] QComboBox, QWidget[socialSurface="true"] QSpinBox {
 background:#eaf0ef;color:#161616;border:1px solid #6c898d;border-radius:5px;padding:5px 9px;
 selection-background-color:#bad4d4;selection-color:#161616;min-height:24px;
}
QWidget[socialSurface="true"] QLineEdit:focus, QWidget[socialSurface="true"] QPlainTextEdit:focus,
QWidget[socialSurface="true"] QComboBox:focus {border:2px solid #458a8f;padding:4px 8px;}
QWidget[socialSurface="true"] QPushButton {
 background:#09262d;color:#c9eee7;border:1px solid #547b82;border-radius:5px;padding:4px 12px;min-height:24px;
}
QWidget[socialSurface="true"] QPushButton:hover {background:#16434a;}
QWidget[socialSurface="true"] QPushButton:checked {background:#16434a;color:#ffffff;}
QWidget[socialSurface="true"] QPushButton:focus {border:2px solid #69efc5;padding:3px 11px;}
QWidget[socialSurface="true"] QPushButton:disabled {background:#0b2027;color:#91aaad;border-color:#27444c;}
QWidget[socialSurface="true"] QLineEdit:disabled, QWidget[socialSurface="true"] QPlainTextEdit:disabled {background:#dfe7e7;color:#525252;border-color:#6c898d;}
QWidget[socialSurface="true"] QListWidget, QWidget[socialSurface="true"] QTextBrowser,
QWidget[socialSurface="true"] QTableWidget,
QWidget[socialSurface="true"] QListWidget QWidget, QWidget[socialSurface="true"] QTextBrowser QWidget,
QWidget[socialSurface="true"] QTableWidget QWidget {
 background:#dfe7e7;color:#161616;border:0;border-radius:0;padding:0;
 selection-background-color:#c3d4d4;selection-color:#161616;gridline-color:#bdcccc;
}
QWidget[socialSurface="true"] QListWidget::item {padding:7px;}
QWidget[socialSurface="true"] QListWidget::item:hover {background:#d4dfdf;}
QWidget[socialSurface="true"] QListWidget::item:selected {background:#c3d4d4;color:#161616;}
QWidget[socialSurface="true"] QHeaderView::section {background:#09262d;color:#c9eee7;border:0;border-bottom:1px solid #547b82;padding:9px;font:600 14px "Segoe UI";}
QWidget[socialSurface="true"] QComboBox::drop-down {width:24px;border:0;background:#dfe7e7;}
QWidget[socialSurface="true"] QComboBox::down-arrow {image:none;width:11px;height:7px;}
QWidget[socialSurface="true"] QSpinBox::up-arrow, QWidget[socialSurface="true"] QSpinBox::down-arrow {image:none;}
QWidget[socialSurface="true"] QComboBox QAbstractItemView {background:#eaf0ef;color:#161616;selection-background-color:#c3d4d4;selection-color:#161616;}
QWidget[socialSurface="true"] QTabWidget::pane {border:0;background:#041319;}
QWidget[socialSurface="true"] QTabBar::tab {background:#09262d;color:#c9eee7;padding:8px 18px;}
QWidget[socialSurface="true"] QTabBar::tab:selected {background:#16434a;border-bottom:2px solid #69efc5;}
QWidget[socialSurface="true"] QScrollBar:vertical {background:#dfe7e7;width:9px;margin:0;}
QWidget[socialSurface="true"] QScrollBar::handle:vertical {background:#80989b;min-height:24px;border-radius:4px;}
QWidget[socialSurface="true"] QScrollBar::add-line:vertical, QWidget[socialSurface="true"] QScrollBar::sub-line:vertical {height:0;}
QWidget[socialSurface="true"] QLineEdit#pgn-social-search-1, QWidget[socialSurface="true"] QLineEdit#pgn-social-search-2, QWidget[socialSurface="true"] QLineEdit#pgn-social-search-3 {background:#09262d;color:#c9eee7;border-color:#547b82;}
QWidget[socialSurface="true"] QToolTip {background:#09262d;color:#c9eee7;border:1px solid #547b82;}
QWidget[socialSurface="true"] QMenu {background:#09262d;color:#c9eee7;border:1px solid #547b82;}
QWidget[socialSurface="true"] QMenu::item:selected {background:#16434a;}
)QSS");}
inline QString SocialChatContrastStyle(){return QStringLiteral(R"QSS(
QWidget[socialChrome="true"], QWidget[socialChrome="true"] QWidget,
QLabel[socialChrome="true"] {background:#041319;color:#c9eee7;font:12px "Segoe UI";}
QWidget#pgn-social-chat-surface, QWidget#pgn-social-chat-surface QWidget {background:#041319;color:#c9eee7;font:14px "Segoe UI";}
QWidget#pgn-social-chat-surface QLabel {background:transparent;}
QWidget#pgn-social-chat-surface QLabel[caption="true"] {color:#9db6b9;font:12px "Segoe UI";}
QWidget#pgn-social-chat-surface QLabel[heading="true"] {color:#e8fffb;font:600 15px "Segoe UI";}
QWidget#pgn-social-chat-surface QFrame#pgn-social-channels-pane,
QWidget#pgn-social-chat-surface QFrame#pgn-social-members-pane,
QWidget#pgn-social-chat-surface QListWidget#pgn-social-chat-channels,
QWidget#pgn-social-chat-surface QListWidget#pgn-social-chat-members {background:#09262d;color:#c9eee7;border:0;}
QWidget#pgn-social-chat-surface QListWidget::item {padding:8px;border:0;}
QWidget#pgn-social-chat-surface QListWidget::item:selected {background:#16434a;color:#ffffff;}
QWidget#pgn-social-chat-surface QListWidget::item:hover {background:#10343c;}
QWidget#pgn-social-chat-surface QListWidget#pgn-social-list-0,
QWidget#pgn-social-chat-surface QTextBrowser#pgn-social-chat-preview {background:#dfe7e7;color:#161616;border:0;}
QWidget#pgn-social-chat-surface QPlainTextEdit {background:#eaf0ef;color:#161616;border:1px solid #6c898d;border-radius:6px;padding:8px;selection-background-color:#bad4d4;selection-color:#161616;}
QWidget#pgn-social-chat-surface QPlainTextEdit:focus {border:2px solid #458a8f;padding:7px;}
QWidget#pgn-social-chat-surface QPushButton {background:#09262d;color:#c9eee7;border:1px solid #547b82;border-radius:5px;padding:3px 9px;min-height:22px;font:13px "Segoe UI";}
QWidget#pgn-social-chat-surface QPushButton:hover {background:#16434a;}
QWidget#pgn-social-chat-surface QPushButton:checked {background:#16434a;color:#ffffff;}
QWidget#pgn-social-chat-surface QPushButton:focus {border:2px solid #69efc5;padding:2px 8px;}
QWidget#pgn-social-chat-surface QPushButton:disabled {background:#0b2027;color:#91aaad;border-color:#27444c;}
QWidget#pgn-social-chat-surface QLineEdit {background:#09262d;color:#c9eee7;border:1px solid #547b82;min-height:22px;padding:3px 8px;}
QWidget#pgn-social-chat-surface QScrollBar:vertical {background:#dfe7e7;width:9px;}
QWidget#pgn-social-chat-surface QScrollBar::handle:vertical {background:#80989b;}
)QSS");}
}
