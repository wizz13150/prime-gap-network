#pragma once

#include <QAbstractButton>
#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTabWidget>
#include <QTabBar>
#include <initializer_list>
#include <utility>

// Extracted unchanged from Gapcoin's workspace navigation. Coin adapters supply
// labels/pages, never their own navigation skin or mining lifecycle callbacks.
inline QFrame* HorizonCoinNavigation(QTabWidget* tabs,
    std::initializer_list<std::pair<const char*,const char*>> items,QWidget* parent)
{
    auto* navigation=new QFrame(parent);
    navigation->setObjectName(QStringLiteral("horizonNavigation"));
    navigation->setFixedHeight(58);
    auto* layout=new QHBoxLayout(navigation);
    layout->setContentsMargins(14,6,14,7);layout->setSpacing(8);
    auto* group=new QButtonGroup(navigation);group->setExclusive(true);
    int index=0;
    for(const auto& item:items){
        auto* button=new QPushButton(QStringLiteral("%1\n%2").arg(
            QString::fromLatin1(item.first),QString::fromLatin1(item.second)),navigation);
        button->setCheckable(true);button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(QStringLiteral("Open this workspace without interrupting active mining."));
        group->addButton(button,index);layout->addWidget(button,1);
        QObject::connect(button,&QPushButton::clicked,tabs,[tabs,index]{tabs->setCurrentIndex(index);});
        ++index;
    }
    if(auto* button=group->button(tabs->currentIndex()))button->setChecked(true);
    QObject::connect(tabs,&QTabWidget::currentChanged,navigation,[group](int i){
        if(auto* button=group->button(i))button->setChecked(true);
    });
    tabs->tabBar()->hide();
    navigation->setStyleSheet(QStringLiteral(
        "QFrame#horizonNavigation{background:#010a0f;"
        "border-top:1px solid #0f313a;border-bottom:1px solid #205963;}"
        "QPushButton{background:#03151c;color:#78b9c6;border:1px solid #123e49;"
        "border-radius:12px;min-height:0px;padding:4px 12px;text-align:left;"
        "font-family:monospace;font-size:9px;font-weight:600;}"
        "QPushButton:hover{background:#08252d;color:#e5fffb;border-color:#31808a;}"
        "QPushButton:checked{background:#0a3535;color:#69efc5;"
        "border:1px solid #69efc5;border-left:5px solid #ffad4a;}"));
    navigation->ensurePolished();
    for(auto* button:group->buttons()) { button->ensurePolished(); button->setFixedHeight(45); }
    return navigation;
}
