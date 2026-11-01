#pragma once
#include "ui/position_formats.h"
#include "ui/ticket_productivity.h"
#include <QWidget>
#include <QPainter>
#include <QPushButton>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <QHelpEvent>
#include <QContextMenuEvent>
#include <QToolTip>
#include <QMenu>
#include <QClipboard>
#include <QGuiApplication>
#include <algorithm>
#include <cmath>
namespace pgn::ui {
class MiningSessionSummary final:public QWidget {
    QJsonObject session_,history_,context_,productivity_;QRectF trackedRect_;bool historyMode_{};QPushButton *sessionButton_,*historyButton_;
    const QJsonObject& scope()const{return historyMode_?history_:session_;}
    static QString scalar(const QJsonObject& object,const char* key){return object.value(key).toVariant().toString();}
    QStringList values()const{
        if(scope().value("statistics").toObject().isEmpty())return {"—","—","—","—","—","—","—","—"};
        const auto& data=scope();const double seconds=scalar(data,"active_time_ns").toDouble()/1e9;const auto secs=qint64(seconds),golds=data.value("observed_golds").toVariant().toLongLong();const auto best=data.value("best").toArray();
        return {scalar(data,"completed_tickets"),scalar(data,"ticket_count"),positionFormats(scalar(data,"positions")).human,positionFormats(scalar(data,"tests")).human,QString("%1:%2:%3").arg(secs/3600,2,10,QChar('0')).arg(secs/60%60,2,10,QChar('0')).arg(secs%60,2,10,QChar('0')),QString::number(golds),data.value("daily_forecast").toObject().value("validated").toBool()?QString("≈ %1").arg(data.value("daily_forecast").toObject().value("blocks_per_day").toDouble(),0,'f',1):QString("—"),best.isEmpty()?QString("—"):QString::number(best.first().toObject().value("gap").toInt())};
    }
    void mode(bool history){historyMode_=history;sessionButton_->setChecked(!history);historyButton_->setChecked(history);QSettings().setValue("mining/summaryHistory",history);setProperty("pgn_summary_scope",history?"history":"session");describe();update();}
    void describe(){setProperty("pgn_summary_statistics",scope());setToolTip(QString{});setProperty("pgn_summary_range_start",context_.value("start").toString());setProperty("pgn_summary_range_end",context_.value("end").toString());}

public:
    explicit MiningSessionSummary(QWidget* parent=nullptr):QWidget(parent){
        setObjectName("pgn-session-statistics");setMinimumSize(0,0);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Ignored);sessionButton_=new QPushButton("Session",this);historyButton_=new QPushButton("History",this);sessionButton_->setObjectName("pgn-summary-session");historyButton_->setObjectName("pgn-summary-history");for(auto* button:{sessionButton_,historyButton_}){button->setCheckable(true);button->setAutoExclusive(true);button->setStyleSheet("QPushButton{background:#08232b;color:#9fc8c3;border:1px solid #2a5a61;font-size:10px;padding:0;} QPushButton:checked{background:#17584f;color:#eafff5;border-color:#69efc5;} QPushButton:hover{border-color:#9ffff0;}");}connect(sessionButton_,&QPushButton::clicked,this,[this]{mode(false);});connect(historyButton_,&QPushButton::clicked,this,[this]{mode(true);});mode(QSettings().value("mining/summaryHistory",false).toBool());
    }
    void present(const QJsonObject& session,const QJsonObject& history,const QJsonObject& context){session_=session;history_=history;context_=context;describe();update();}
    void presentProductivity(const QJsonArray& devices,const QString& width){const auto value=ticketProductivity(devices,width);if(value==productivity_)return;productivity_=value;setProperty("pgn_ticket_productivity",value);update();}
protected:
    bool event(QEvent* event)override{if(event->type()==QEvent::ToolTip){auto* help=static_cast<QHelpEvent*>(event);if(trackedRect_.contains(help->pos())){QToolTip::showText(help->globalPos(),"Distinct tickets included in this view: completed, current or paused. Tickets done counts completed calculation, not network audit acceptance.",this,trackedRect_.toRect());}else QToolTip::hideText();event->accept();return true;}return QWidget::event(event);}
    void contextMenuEvent(QContextMenuEvent* event)override{QMenu menu(this);auto* positions=menu.addAction("Copy exact positions");auto* tests=menu.addAction("Copy exact test count");auto* start=menu.addAction("Copy exact range start");auto* end=menu.addAction("Copy exact range end (exclusive)");const auto chosen=menu.exec(event->globalPos());if(chosen==positions)QGuiApplication::clipboard()->setText(scalar(scope(),"positions"));else if(chosen==tests)QGuiApplication::clipboard()->setText(scalar(scope(),"tests"));else if(chosen==start)QGuiApplication::clipboard()->setText(scalar(context_,"start"));else if(chosen==end)QGuiApplication::clipboard()->setText(scalar(context_,"end"));}
    void resizeEvent(QResizeEvent* event)override{QWidget::resizeEvent(event);const int w=std::max(2,width()-16),half=w/2;sessionButton_->setGeometry(8,1,half,21);historyButton_->setGeometry(8+half,1,w-half,21);}
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.setRenderHint(QPainter::TextAntialiasing);const QStringList labels{"Tickets done","Tickets tracked","Positions","Tests","Active time","Golds found","Blocks/day est.","Largest gap"};const auto& data=scope();const auto v=values();
        const auto range=positionRangeFormats(scalar(context_,"start"),scalar(context_,"end"));
        const bool hasRates=!productivity_.value("devices").toArray().isEmpty();
        const bool compact=height()<350;const double rangeLine=compact?10.:13.,rateLine=compact?10.:14.;
        QJsonArray rateRows=productivity_.value(compact?"groups":"devices").toArray();
        if(hasRates)rateRows.append(QJsonObject{{"label","Total"},{"active",productivity_.value("active_devices").toInt()>0},{"tickets_per_hour",productivity_.value("total_tickets_per_hour")}});
        const double rateHeight=hasRates?5.+12.+rateRows.size()*rateLine+(compact?16.:18.):0.;
        const double reserved=27.+(range.valid?26.+4*rangeLine:28.)+rateHeight+(hasRates&&compact?0.:20.);
        const double unit=std::clamp((height()-reserved)/12.,8.,27.);QFont font("Consolas");font.setPixelSize(unit<12?7:unit<15?8:11);double y=27;
        for(int i=0;i<8;++i){const bool quantity=i==2||i==3;const double h=unit*(quantity?3:1);const QRectF line(8,y,std::max(1,width()-16),h);if(i==1)trackedRect_=line;const auto formats=quantity?positionFormats(scalar(data,i==2?"positions":"tests")):PositionFormats{};const int quantityWidth=quantity?std::max(QFontMetrics(font).horizontalAdvance(formats.decimal),QFontMetrics(font).horizontalAdvance(formats.binary)):0;const int valueWidth=std::min(int(line.width()*.72),std::max({50,QFontMetrics(font).horizontalAdvance(v[i])+4,quantityWidth+4}));p.setFont(font);p.setPen(QColor("#9fc7c3"));p.drawText(line.adjusted(0,0,-valueWidth-6,0),Qt::AlignLeft|Qt::AlignVCenter,p.fontMetrics().elidedText(i==2&&line.width()<180?QString("Pos."):labels[i],Qt::ElideRight,std::max(1,int(line.width())-valueWidth-6)));p.setPen(QColor(i==5||i==6?"#ffd700":"#e7fff7"));
            if(quantity){const QStringList lines{formats.human,formats.decimal,formats.binary};for(int j=0;j<3;++j){QFont detail=font;if(j)detail.setPixelSize(std::max(7,font.pixelSize()-1));p.setFont(detail);p.drawText(QRectF(line.right()-valueWidth,y+j*unit,valueWidth,unit),Qt::AlignRight|Qt::AlignVCenter,lines[j]);}}else p.drawText(line,Qt::AlignRight|Qt::AlignVCenter,v[i]);y+=h;
        }
        font.setPixelSize(width()<190?8:10);p.setFont(font);p.setPen(QColor("#2b5961"));p.drawLine(QPointF(8,y+4),QPointF(width()-8,y+4));p.setPen(QColor("#69efc5"));const auto minimum=context_.value("minimum_gap").toInt();p.drawText(QRectF(8,y+9,width()-16,16),Qt::AlignLeft|Qt::AlignVCenter,QString("%1 bits · Min gap ≥ %2").arg(context_.value("bits").toInt()?QString::number(context_.value("bits").toInt()):QString("—"),minimum?QString::number(minimum):QString("—")));
        double footnote=y+26;
        if(range.valid){
            const QStringList lines{"From  "+range.start.decimal,range.start.binary,"To <  "+range.end.decimal,range.end.binary};
            QJsonArray regions;
            for(int i=0;i<lines.size();++i){
                QFont detail("Consolas");detail.setPixelSize(width()<190?8:10);while(detail.pixelSize()>6&&QFontMetrics(detail).horizontalAdvance(lines[i])>width()-16)detail.setPixelSize(detail.pixelSize()-1);
                p.setFont(detail);p.setPen(QColor(i%2?"#79a8a5":"#e7fff7"));const QRectF line(8,y+26+i*rangeLine,width()-16,rangeLine);p.drawText(line,Qt::AlignLeft|Qt::AlignVCenter,lines[i]);regions.append(QJsonObject{{"text",lines[i]},{"x",line.x()},{"y",line.y()},{"width",line.width()},{"height",line.height()},{"text_width",QFontMetrics(detail).horizontalAdvance(lines[i])}});
            }
            setProperty("pgn_summary_range_regions",regions);footnote=y+26+4*rangeLine;
        }else setProperty("pgn_summary_range_regions",QJsonArray{});
        if(hasRates){
            font.setPixelSize(compact?7:9);p.setFont(font);p.setPen(QColor("#2b5961"));p.drawLine(QPointF(8,footnote+3),QPointF(width()-8,footnote+3));footnote+=5;
            p.setPen(QColor("#69efc5"));p.drawText(QRectF(8,footnote,width()-16,12),Qt::AlignVCenter,"Live ticket eq./h · est.");footnote+=12;QJsonArray regions;
            for(const auto& value:rateRows){const auto row=value.toObject();const auto rate=row.value("tickets_per_hour");const QString text=rate.isDouble()?QString::number(rate.toDouble(),'g',3):QString("—");const auto label=row.value("label").toString()+ (row.value("validation_only")==true?QString(" · test"):QString{});const int valueWidth=std::max(30,p.fontMetrics().horizontalAdvance(text)+4);const QRectF rect(8,footnote,width()-16,rateLine);p.setPen(QColor("#9fc7c3"));p.drawText(rect.adjusted(0,0,-valueWidth-4,0),Qt::AlignVCenter,p.fontMetrics().elidedText(label,Qt::ElideRight,std::max(1,int(rect.width())-valueWidth-4)));p.setPen(QColor("#e7fff7"));p.drawText(rect,Qt::AlignRight|Qt::AlignVCenter,text);regions.append(QJsonObject{{"label",label},{"value",text},{"y",rect.y()},{"height",rect.height()}});footnote+=rateLine;}
            setProperty("pgn_productivity_regions",regions);font.setPixelSize(compact?6:8);p.setFont(font);p.setPen(QColor("#79a8a5"));p.drawText(QRectF(8,footnote,width()-16,compact?16:18),Qt::AlignLeft|Qt::TextWordWrap,compact?QString("Includes audits; excludes waits. Not accepted blocks."):QString("Current network ticket = 1 unit. Includes audits; excludes waiting."));footnote+=compact?16:18;
        }else setProperty("pgn_productivity_regions",QJsonArray{});
        if(!hasRates||!compact){font.setPixelSize(compact?6:8);p.setFont(font);p.setPen(QColor("#79a8a5"));p.drawText(QRectF(8,footnote,width()-16,20),Qt::AlignLeft|Qt::TextWordWrap,"Gold discoveries await audits; accepted blocks are separate.");}
    }
};
}
