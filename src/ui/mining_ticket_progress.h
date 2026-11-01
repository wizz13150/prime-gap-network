#pragma once
#include "ui/mining_work_tile.h"
#include "ui/position_formats.h"
#include <QWidget>
#include <QTimer>
#include <QPainter>
#include <QElapsedTimer>
#include <QJsonObject>
#include <cmath>
namespace pgn::ui {
// Source counters determine progress. Decorative motion never advances work.
class MiningTicketProgress final : public QWidget {
    QJsonObject frame_;QTimer timer_;QElapsedTimer clock_;
public:
    explicit MiningTicketProgress(QWidget* parent=nullptr):QWidget(parent){
        setObjectName("pgn-ticket-progress-rail");setFixedHeight(78);clock_.start();timer_.setInterval(33);
        connect(&timer_,&QTimer::timeout,this,[this]{if(isVisible()&&(frame_.value("phase")=="Searching"||frame_.value("phase")=="Starting"||frame_.value("phase")=="Stopping"))update();});timer_.start();
    }
    void present(const QJsonObject& frame){frame_=frame;setToolTip(QString("Exact ticket interval [%1, %2). Progress records accepted positions; animation is decorative.").arg(frame.value("start").toString(),frame.value("end").toString()));update();}
protected:
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);const auto phase=frame_.value("phase").toString("Waiting for a ticket");
        const bool active=phase=="Searching",transition=phase=="Starting"||phase=="Stopping";const QColor accent=transition?QColor("#ffbd69"):QColor("#69efc5");
        const double total=frame_.value("positions_total").toString().toDouble(),done=frame_.value("positions").toString().toDouble();const auto fraction=total>0?std::optional<double>(std::clamp(done/total,0.,1.)):std::nullopt;
        const QRectF bar(16,29,std::max(1,width()-32),34);const double seconds=clock_.elapsed()/1000.;const double velocity=active?std::clamp(std::log10(1.+frame_.value("speed_rate").toDouble())/12.,.15,1.):0.;
        drawMiningWorkTile(p,bar,accent,fraction,active,active?std::optional<double>(std::fmod(seconds*.19*velocity,1.)):std::nullopt);
        p.setPen(QColor("#b9e3df"));QFont font("Consolas");font.setPixelSize(11);p.setFont(font);
        const auto title=frame_.value("title").toString("Network ticket")+QString(" · %1 bits · %2 positions").arg(frame_.value("bits").toInt()).arg(positionFormats(frame_.value("positions_total").toString()).inlineText());p.drawText(QRectF(18,5,std::max(0,width()-36),18),Qt::AlignLeft,p.fontMetrics().elidedText(title,Qt::ElideRight,std::max(1,width()-36)));
        if(active){for(int i=0;i<5;++i){const double at=std::fmod(seconds*.19*velocity+i*.2,1.);const QPointF point(bar.left()+at*bar.width(),bar.center().y());QRadialGradient glow(point,12);glow.setColorAt(0,QColor(105,239,197,140));glow.setColorAt(1,QColor(105,239,197,0));p.setPen(Qt::NoPen);p.setBrush(glow);p.drawEllipse(point,12,12);}}
        if(transition){auto color=accent;color.setAlphaF(std::clamp(.3+.35*(1+std::sin(seconds*5)),0.,1.));p.setPen(QPen(color,2));p.setBrush(Qt::NoBrush);p.drawRoundedRect(bar.adjusted(-2,-2,2,2),8,8);}
        const double rate=frame_.value("eta_rate").toDouble();const double remaining=std::max(0.,total-done);const std::optional<double> eta=active&&rate>0&&std::isfinite(rate)&&total>0?std::optional<double>(remaining/rate):std::nullopt;
        QString label=phase+(fraction?QString(" · %1%").arg(100* *fraction,0,'f',2):QString{});
        if(eta&&std::isfinite(*eta)&&*eta<365.*86400){const auto value=qint64(std::ceil(*eta));label+=QString(" · ETA %1:%2:%3").arg(value/3600,2,10,QChar('0')).arg(value/60%60,2,10,QChar('0')).arg(value%60,2,10,QChar('0'));}
        setProperty("pgn_ticket_eta_seconds",eta?QVariant(*eta):QVariant{});
        p.setPen(QColor("#e7f9f1"));p.drawText(bar,Qt::AlignCenter,p.fontMetrics().elidedText(label,Qt::ElideRight,std::max(1,int(bar.width()-12))));setProperty("pgn_ticket_progress_fraction",fraction?QVariant(*fraction):QVariant{});setProperty("pgn_ticket_animation_active",active);setProperty("pgn_ticket_animation_phase",active?QVariant(std::fmod(seconds*.19*velocity,1.)):QVariant{});
    }
};
}
