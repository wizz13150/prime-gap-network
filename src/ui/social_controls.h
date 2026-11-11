#pragma once
#include <QComboBox>
#include <QSpinBox>
#include <QPainter>
#include <QStyleOption>
namespace pgn::ui {
// Native controls with locally drawn indicators: no optional SVG decoder needed.
class SocialComboBox final:public QComboBox{
public:using QComboBox::QComboBox;
protected:void paintEvent(QPaintEvent* event)override{
    QComboBox::paintEvent(event);QStyleOptionComboBox option;initStyleOption(&option);
    const auto arrow=style()->subControlRect(QStyle::CC_ComboBox,&option,QStyle::SC_ComboBoxArrow,this);
    QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);painter.translate(arrow.center().x()-5.5,arrow.center().y()-3.5);painter.scale(11./12.,7./8.);
    painter.setPen(QPen(QColor("#525252"),1.6,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));painter.drawPolyline(QPolygonF{QPointF(1.5,1.5),QPointF(6,6),QPointF(10.5,1.5)});
}
};
class SocialSpinBox final:public QSpinBox{
public:using QSpinBox::QSpinBox;
protected:void paintEvent(QPaintEvent* event)override{
    QSpinBox::paintEvent(event);QStyleOptionSpinBox option;initStyleOption(&option);QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(QPen(QColor("#525252"),1.4,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    for(const bool up:{true,false}){const auto r=style()->subControlRect(QStyle::CC_SpinBox,&option,up?QStyle::SC_SpinBoxUp:QStyle::SC_SpinBoxDown,this);const auto c=r.center();
        painter.drawPolyline(QPolygonF{QPointF(c.x()-3,c.y()+(up?1:-1)),QPointF(c.x(),c.y()+(up?-2:2)),QPointF(c.x()+3,c.y()+(up?1:-1))});}
}
};
}
