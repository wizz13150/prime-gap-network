#pragma once
#include "booking/booking_model.h"
#include "booking/round_range_catalog.h"
#include "horizon_graph_controls.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QLineEdit>
#include <QSignalBlocker>
namespace pgn::ui {
inline QWidget* createRoundRangeNavigation(pgn::booking::BookingModel* model,QWidget* parent){
    auto* panel=new QWidget(parent);panel->setObjectName("pgn-round-range-navigation");
    auto* row=new QHBoxLayout(panel);row->setContentsMargins(0,0,0,0);row->setSpacing(6);
    auto* previous=new HorizonGraphResetButton("PREVIOUS RANGE",102,panel);
    auto* next=new HorizonGraphResetButton("NEXT RANGE",88,panel);previous->setZoomed(true);next->setZoomed(true);
    auto* title=new QLabel;title->setObjectName("pgn-round-range-title");title->setStyleSheet("color:#d8fff4;font:600 11px 'Segoe UI';");row->addWidget(previous);row->addWidget(title,1);row->addWidget(next);
    auto* jump=new QSpinBox(panel);jump->setObjectName("pgn-round-range-jump");
    jump->setRange(0,int(pgn::booking::roundRangeMaximumPage));jump->setKeyboardTracking(false);jump->setButtonSymbols(QAbstractSpinBox::NoButtons);
    jump->setFixedSize(qMax(65,jump->fontMetrics().horizontalAdvance(QString::number(pgn::booking::roundRangeMaximumPage))+14),21);jump->setToolTip("Enter the round range start in units of10²⁰. Each page has1000 zones, each zone100 blocks. Example1 opens[10²⁰,2×10²⁰).");jump->setAccessibleName("Numeric range start in units of ten to the twentieth");jump->setStyleSheet("QSpinBox{min-width:0;min-height:0;max-height:21px;padding:1px 4px;}QSpinBox QLineEdit{min-height:0;padding:0;}");row->insertWidget(2,jump);
    const auto startupPage=model->viewIsRoundRange()?model->viewPage():model->source().ranges.isEmpty()?1:pgn::booking::decimalZoneIndex(model->source().ranges.first().start)/1000;
    auto* home=new HorizonGraphResetButton("DEFAULT RANGE",106,panel);home->setObjectName("pgn-default-range");home->setZoomed(true);home->setToolTip("Return to the number range shown at startup and reset its zoom.");row->insertWidget(1,home);
    const auto refresh=[=]{QSignalBlocker guard(jump);const auto page=model->viewIsRoundRange()?model->viewPage():model->source().ranges.isEmpty()?1:pgn::booking::decimalZoneIndex(model->source().ranges.first().start)/1000;
        const auto* editor=jump->findChild<QLineEdit*>();
        if(!editor||!editor->hasFocus()||!jump->property("pgn_sync_page").isValid()||jump->property("pgn_sync_page").toULongLong()!=page)jump->setValue(int(page));
        jump->setProperty("pgn_sync_page",qulonglong(page));title->setText(page==pgn::booking::roundRangeMaximumPage?QString("NUMBER RANGE · %1.00 ×10²⁰ – 2^95").arg(page):QString("NUMBER RANGE · %1.00 – %2.00 ×10²⁰").arg(page).arg(page+1));
        title->setToolTip("Exact interval ["+QString::number(page)+QString(20,'0')+", "+pgn::booking::roundRangeEnd(page)+")\nKnown declarations retain their identities. Grey intervals are UNKNOWN, not free.");
        previous->setEnabled(page>0);next->setEnabled(page<pgn::booking::roundRangeMaximumPage);
    };model->subscribe(panel,refresh);refresh();
    QObject::connect(jump,&QSpinBox::valueChanged,panel,[model](int value){model->setRoundRange(quint64(value));});
    QObject::connect(previous,&QPushButton::clicked,panel,[model,jump]{if(jump->value()>0)model->setRoundRange(quint64(jump->value()-1));});
    QObject::connect(next,&QPushButton::clicked,panel,[model,jump]{model->setRoundRange(quint64(jump->value()+1));});
    QObject::connect(home,&QPushButton::clicked,panel,[model,parent,startupPage]{model->setRoundRange(startupPage);if(parent)if(auto* reset=parent->findChild<QPushButton*>("pgn-source-atlas-reset"))reset->click();});
    return panel;
}
}
