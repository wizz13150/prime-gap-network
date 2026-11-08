#pragma once
#include <QLabel>
#include <QPainter>
#include <QTimer>
#include <QDateTime>
namespace pgn::ui {
// The activity mark occupies fixed pixels; changing it never changes text
// metrics, the baseline or the footer's size hint.
class NetworkStatusLabel final:public QLabel {
    QTimer clock_;bool busy_{};
protected:
    void paintEvent(QPaintEvent* event)override{
        QLabel::paintEvent(event);QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        const auto color=palette().color(QPalette::WindowText);painter.setPen(QPen(color,1.3));
        painter.translate(9,height()/2.);
        if(!busy_){painter.setPen(Qt::NoPen);painter.setBrush(color);painter.drawEllipse(QPointF{},2.5,2.5);return;}
        painter.rotate((QDateTime::currentMSecsSinceEpoch()%1200)*.3);
        for(int i=0;i<2;++i){painter.drawArc(QRectF(-5,-5,10,10),35*16,130*16);painter.setBrush(color);painter.drawPolygon(QPolygonF{{-5.1,-1.3},{-5.2,1.8},{-2.4,.4}});painter.rotate(180);}
    }
public:
    explicit NetworkStatusLabel(const QString& text,QWidget* parent=nullptr):QLabel(text,parent),clock_(this){
        setContentsMargins(18,0,0,0);setAlignment(Qt::AlignLeft|Qt::AlignVCenter);
        clock_.setInterval(40);connect(&clock_,&QTimer::timeout,this,[this]{update(QRect(0,0,18,height()));});
    }
    void setBusy(bool busy){if(busy==busy_)return;busy_=busy;if(busy)clock_.start();else clock_.stop();update();}
};
}
