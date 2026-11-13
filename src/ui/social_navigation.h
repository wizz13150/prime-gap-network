#pragma once
#include <QPushButton>
#include <QKeySequence>
namespace pgn::ui {
// Same native Horizon chrome, a single readable line at the shell's33px height.
inline QPushButton* createSocialNavigationButton(QWidget* parent){
    auto* button=new QPushButton("SOCIAL · COMMUNITY SPACE",parent);
    button->setObjectName("pgn-primary-social-tab");button->setCheckable(true);
    button->setStyleSheet("QPushButton{font:600 12px 'Segoe UI';background:#12343e;color:#e3f1f1;border:1px solid #426b76;border-radius:8px;padding:4px 12px;}QPushButton:hover{background:#1c4954;}QPushButton:focus{border:2px solid #7eafb8;padding:3px 11px;}");
    button->ensurePolished();button->setMinimumWidth(qMax(210,button->fontMetrics().horizontalAdvance(button->text())+36));button->setFixedHeight(33);
    button->setCursor(Qt::PointingHandCursor);button->setShortcut(QKeySequence("Alt+S"));
    button->setToolTip("Chat and verified miner statistics. Alt+S opens Social.");
    return button;
}
}
