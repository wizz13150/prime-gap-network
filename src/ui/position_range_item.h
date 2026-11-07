#pragma once
#include "booking/decimal_range.h"
#include "ui/position_formats.h"
#include <QTreeWidgetItem>

namespace pgn::ui {
// The visible quantity uses the common three notations; the original interval
// remains an exact, independently selectable detail. No double subtraction.
inline QTreeWidgetItem* positionRangeItem(QTreeWidgetItem* parent,const QString& name,
                                          const QString& start,const QString& end,bool inlineQuantity=false){
    const auto range=positionRangeFormats(start,end);
    auto* item=new QTreeWidgetItem(parent,{name,QString{}});
    new QTreeWidgetItem(item,{"Exact interval","["+start+", "+end+")"});
    if(!range.valid){item->setText(1,"Invalid interval");return item;}
    const auto count=positionFormats(pgn::booking::subtractUnsigned(range.end.exact,range.start.exact));
    item->setText(1,(inlineQuantity?count.inlineText():count.stackedText())+" positions");
    new QTreeWidgetItem(item,{"Scientific interval","["+range.start.decimal+", "+range.end.decimal+")"});
    new QTreeWidgetItem(item,{"Binary interval","["+range.start.binary+", "+range.end.binary+")"});
    return item;
}
}
