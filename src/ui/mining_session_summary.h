#pragma once
#include "ui/position_formats.h"
#include "ui/ticket_productivity.h"
#include "ui/observed_ticket_productivity.h"
#include "ui/network_mining_summary.h"
#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>
#include <QScrollBar>
#include <QWheelEvent>
#include <QSignalBlocker>
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
    QElapsedTimer received_;QScrollBar* scroll_{};
    QElapsedTimer productivityClock_;ObservedTicketProductivity observedProductivity_;
    QJsonObject coreMetrics_;QString coreGenesis_;QElapsedTimer coreReceived_;bool coreBound_{};
    QJsonObject networkSummary()const{const auto sourceGenesis=context_.value("genesis").toString();if(coreBound_&&!sourceGenesis.isEmpty()&&sourceGenesis!=coreGenesis_)return {{"available",false},{"rows",QJsonArray{}}};return coreBound_?networkMiningCoreSummary(coreMetrics_,coreGenesis_,coreReceived_.isValid()?coreReceived_.elapsed():90001):networkMiningSummary(context_,received_.isValid()?received_.elapsed():0);}
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
        setObjectName("pgn-session-statistics");setMinimumSize(0,0);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Ignored);sessionButton_=new QPushButton("Session",this);historyButton_=new QPushButton("History",this);sessionButton_->setObjectName("pgn-summary-session");historyButton_->setObjectName("pgn-summary-history");for(auto* button:{sessionButton_,historyButton_}){button->setCheckable(true);button->setAutoExclusive(true);button->setStyleSheet("QPushButton{background:#08232b;color:#9fc8c3;border:1px solid #2a5a61;font-size:12px;padding:0;} QPushButton:checked{background:#17584f;color:#eafff5;border-color:#69efc5;} QPushButton:hover{border-color:#9ffff0;}");}connect(sessionButton_,&QPushButton::clicked,this,[this]{mode(false);});connect(historyButton_,&QPushButton::clicked,this,[this]{mode(true);});scroll_=new QScrollBar(Qt::Vertical,this);scroll_->setObjectName("pgn-summary-scroll");scroll_->setSingleStep(22);scroll_->setStyleSheet("QScrollBar:vertical{background:#071b22;width:9px;border:0;}QScrollBar::handle:vertical{background:#387b78;min-height:22px;border-radius:4px;}QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}");connect(scroll_,&QScrollBar::valueChanged,this,[this]{update();});mode(QSettings().value("mining/summaryHistory",false).toBool());
        auto* freshness=new QTimer(this);freshness->setInterval(1000);connect(freshness,&QTimer::timeout,this,[this]{const bool available=networkSummary().value("available").toBool();if(property("pgn_network_summary_available").toBool()!=available)update();});freshness->start();
    }
    void present(const QJsonObject& session,const QJsonObject& history,const QJsonObject& context){session_=session;history_=history;context_=context;received_.start();describe();update();}
    void presentNetworkMetrics(const QJsonObject& metrics,const QString& genesis){coreBound_=true;coreMetrics_=metrics;coreGenesis_=genesis;coreReceived_.start();update();}
    void presentProductivity(const QJsonArray& devices,const QString& width){if(!productivityClock_.isValid())productivityClock_.start();auto value=ticketProductivity(devices,width);value.insert("observed",observedProductivity_.observe(devices,width,productivityClock_.nsecsElapsed()));if(value==productivity_)return;productivity_=value;setProperty("pgn_ticket_productivity",value);update();}
protected:
    bool event(QEvent* event)override{if(event->type()==QEvent::ToolTip){auto* help=static_cast<QHelpEvent*>(event);if(trackedRect_.contains(help->pos())){QToolTip::showText(help->globalPos(),"Distinct tickets included in this view: completed, current or paused. Tickets done counts completed calculation, not network audit acceptance.",this,trackedRect_.toRect());}else QToolTip::hideText();event->accept();return true;}return QWidget::event(event);}
    void contextMenuEvent(QContextMenuEvent* event)override{
        QMenu menu(this);auto* detail=menu.addMenu("Tickets/h details");
        const auto observed=productivity_.value("observed").toObject();
        auto* caption=detail->addAction("Observed · includes confirmed waiting");caption->setEnabled(false);
        for(const auto& item:observed.value("devices").toArray()){
            const auto row=item.toObject();const auto value=row.value("tickets_per_hour");
            const auto seconds=row.value("wall_time_ns").toString().toDouble()/1e9;
            const auto text=value.isDouble()?QString("%1 · %2 tickets/h · %3s").arg(row.value("label").toString(),QString::number(value.toDouble(),'g',4),QString::number(seconds,'f',0)):QString("%1 · %2").arg(row.value("label").toString(),row.value("state").toString());
            detail->addAction(text)->setEnabled(false);
        }
        detail->addSeparator();detail->addAction("Calculation estimate · excludes waiting")->setEnabled(false);
        for(const auto& item:productivity_.value("devices").toArray()){
            const auto row=item.toObject();const auto value=row.value("tickets_per_hour");detail->addAction(QString("%1 · %2").arg(row.value("label").toString(),value.isDouble()?QString::number(value.toDouble(),'g',4)+" tickets/h":QString("—")))->setEnabled(false);
        }
        menu.addSeparator();auto* positions=menu.addAction("Copy exact positions");auto* tests=menu.addAction("Copy exact test count");auto* start=menu.addAction("Copy exact range start");auto* end=menu.addAction("Copy exact range end (exclusive)");const auto chosen=menu.exec(event->globalPos());if(chosen==positions)QGuiApplication::clipboard()->setText(scalar(scope(),"positions"));else if(chosen==tests)QGuiApplication::clipboard()->setText(scalar(scope(),"tests"));else if(chosen==start)QGuiApplication::clipboard()->setText(scalar(context_,"start"));else if(chosen==end)QGuiApplication::clipboard()->setText(scalar(context_,"end"));
    }
    void resizeEvent(QResizeEvent* event)override{QWidget::resizeEvent(event);const int w=std::max(2,width()-16),half=w/2;sessionButton_->setGeometry(8,1,half,25);historyButton_->setGeometry(8+half,1,w-half,25);scroll_->setGeometry(std::max(0,width()-10),31,9,std::max(1,height()-33));}
    void wheelEvent(QWheelEvent* event)override{
        if(scroll_->isVisible()&&event->angleDelta().y()){
            scroll_->setValue(scroll_->value()-event->angleDelta().y()/120*scroll_->singleStep());event->accept();return;
        }
        QWidget::wheelEvent(event);
    }
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.setRenderHint(QPainter::TextAntialiasing);
        const QStringList labels{"Tickets done","Tickets tracked","Positions","Tests","Active time","Golds found","Blocks/day est.","Largest gap"};
        const auto& data=scope();const auto v=values();
        const auto network=networkSummary();
        const auto coverage=networkBitCoverage(coreMetrics_,coreGenesis_,context_.value("bits").toInt(),coreReceived_.isValid()?coreReceived_.elapsed():90001);
        const bool hasCoverage=coreBound_&&network.value("available").toBool()&&coverage.value("available").toBool();
        setProperty("pgn_bit_range_coverage",hasCoverage?coverage:QJsonObject{{"available",false}});
        setProperty("pgn_network_summary_available",network.value("available"));setProperty("pgn_network_summary",network);
        constexpr double top=32.,domainHeight=22.,rateHeight=22.,networkHeight=116.;
        const double coverageHeight=hasCoverage?64.:0.;
        const bool footer=height()>=560;
        const double unit=std::clamp((height()-top-domainHeight-rateHeight-networkHeight-coverageHeight-(footer?34.:0.))/12.,17.,25.);
        const double contentHeight=top+12*unit+domainHeight+rateHeight+networkHeight+coverageHeight+(footer?34.:0.);
        const bool needsScroll=contentHeight>height();
        {QSignalBlocker blocked(scroll_);scroll_->setRange(0,std::max(0,int(std::ceil(contentHeight-height()))));scroll_->setPageStep(std::max(1,height()-int(top)));}
        scroll_->setVisible(needsScroll);
        const double offset=scroll_->value(),left=8.,usable=std::max(1,width()-16-(needsScroll?12:0));
        const QRectF viewport(0,top,width()-(needsScroll?12:0),std::max(0,height()-int(top)));
        p.setClipRect(viewport);p.translate(0,-offset);
        QFont font("Consolas");font.setPixelSize(width()>=260?13:12);QFont detail=font;detail.setPixelSize(std::max(11,font.pixelSize()-1));
        setProperty("pgn_summary_font_px",font.pixelSize());setProperty("pgn_summary_detail_font_px",detail.pixelSize());setProperty("pgn_summary_scrollable",needsScroll);
        QJsonArray regions;double y=top;
        auto region=[&](const QString& label,const QString& text,const QRectF& rect,int pixels){const auto visible=rect.translated(0,-offset).intersected(viewport);regions.append(QJsonObject{{"label",label},{"text",text},{"x",rect.x()},{"y",rect.y()-offset},{"width",rect.width()},{"height",rect.height()},{"font_px",pixels},{"visible",!visible.isEmpty()}});};
        auto row=[&](const QString& label,const QString& value,double height,const QColor& valueColor=QColor("#e7fff7")){
            p.setFont(font);const QRectF rect(left,y,usable,height);const int vw=std::min(int(usable*.68),std::max(34,p.fontMetrics().horizontalAdvance(value)+4));
            p.setPen(QColor("#9fc7c3"));p.drawText(rect.adjusted(0,0,-vw-6,0),Qt::AlignLeft|Qt::AlignVCenter,p.fontMetrics().elidedText(label,Qt::ElideRight,std::max(1,int(usable)-vw-6)));
            p.setPen(valueColor);p.drawText(rect,Qt::AlignRight|Qt::AlignVCenter,p.fontMetrics().elidedText(value,Qt::ElideMiddle,int(usable)));
            region(label,value,rect,font.pixelSize());y+=height;
        };
        for(int i=0;i<8;++i){
            const bool quantity=i==2||i==3;const QRectF firstLine(left,y,usable,unit);
            if(i==1)trackedRect_=firstLine.translated(0,-offset).intersected(viewport);
            row(labels[i],v.value(i),unit,QColor(i==5||i==6?"#ffd700":"#e7fff7"));
            if(quantity){
                const auto formats=positionFormats(scalar(data,i==2?"positions":"tests"));
                for(const auto& text:QStringList{formats.decimal,formats.binary}){
                    const QRectF rect(left,y,usable,unit);p.setFont(detail);p.setPen(QColor("#91b9b5"));p.drawText(rect,Qt::AlignRight|Qt::AlignVCenter,p.fontMetrics().elidedText(text,Qt::ElideMiddle,int(usable)));region(labels[i]+" notation",text,rect,detail.pixelSize());y+=unit;
                }
            }
        }
        const int bits=context_.value("bits").toVariant().toInt(),minimum=context_.value("minimum_gap").toVariant().toInt();
        const QString bitText=bits?QString::number(bits):QString("—"),minText=minimum?QString::number(minimum):QString("—");
        const QString domain=width()>=190?QString("%1 bits · min gap ≥ %2").arg(bitText,minText):QString("%1b · min ≥ %2").arg(bitText,minText);
        p.setFont(font);p.setPen(QColor("#69efc5"));const QRectF domainRect(left,y,usable,domainHeight);p.drawText(domainRect,Qt::AlignVCenter,p.fontMetrics().elidedText(domain,Qt::ElideMiddle,int(usable)));region("Range",domain,domainRect,font.pixelSize());y+=domainHeight;
        setProperty("pgn_summary_range_regions",QJsonArray{QJsonObject{{"text",domain},{"y",domainRect.y()-offset},{"height",domainRect.height()},{"font_px",font.pixelSize()}}});
        const auto observed=productivity_.value("observed").toObject().value("total_tickets_per_hour");
        const auto total=observed.isDouble()?observed:productivity_.value("total_tickets_per_hour");const QString rate=total.isDouble()?(observed.isDouble()?QString{}:QString("≈ "))+QString::number(total.toDouble(),'g',3):QString("—");
        const double rateY=y;row("Tickets/h",rate,rateHeight);setProperty("pgn_productivity_regions",QJsonArray{QJsonObject{{"label","Tickets/h"},{"value",rate},{"y",rateY-offset},{"height",rateHeight}}});
        p.setPen(QColor("#2b5961"));p.drawLine(QPointF(left,y+3),QPointF(left+usable,y+3));y+=8;
        p.setFont(font);p.setPen(QColor("#69efc5"));const auto seconds=network.value("window_seconds").toInteger();const QString networkTitle=network.value("available").toBool()?QString("Network · %1 min").arg(seconds/60.):QString("Network · unavailable");
        p.drawText(QRectF(left,y,usable,18),Qt::AlignVCenter,p.fontMetrics().elidedText(networkTitle,Qt::ElideRight,int(usable)));region("Network",networkTitle,QRectF(left,y,usable,18),font.pixelSize());y+=18;
        QJsonArray networkRegions;auto rows=network.value("rows").toArray();
        if(rows.isEmpty())for(const auto* label:{"Registered operators","Reporting miners","Reported search","Gold merit","Audits pending"})rows.append(QJsonObject{{"label",label},{"value","—"}});
        for(const auto& item:rows){const auto object=item.toObject();const QString label=object.value("label").toString(),text=object.value("value").toString();const double at=y;row(label,text,18);networkRegions.append(QJsonObject{{"label",label},{"value",text},{"y",at-offset},{"height",18},{"font_px",font.pixelSize()}});}
        QJsonArray coverageRegions;
        if(hasCoverage){
            y+=6;
            for(const auto& item:QList<QPair<QString,QString>>{{QString("Reported · %1b").arg(coverage.value("bits").toInt()),"reported_ppb"},{"Audited","audited_ppb"},{"Before search start","excluded_ppb"}}){
                const auto quantity=item.second=="excluded_ppb"?"excluded_prefix":item.second=="audited_ppb"?"audited_positions":"reported_positions";
                const double at=y;const auto text=coveragePercent(coverage.value(item.second).toInteger(),coverage.value(quantity).toString()!="0");
                row(item.first,text,18,item.second=="excluded_ppb"?QColor("#91b9b5"):QColor("#69efc5"));
                coverageRegions.append(QJsonObject{{"label",item.first},{"value",text},{"y",at-offset},{"height",18},{"font_px",font.pixelSize()}});
            }
            y+=4;
        }
        setProperty("pgn_bit_range_coverage_regions",coverageRegions);
        setProperty("pgn_network_summary_regions",networkRegions);setProperty("pgn_summary_layout_regions",regions);
        if(footer){p.setFont(detail);p.setPen(QColor("#79a8a5"));p.drawText(QRectF(left,y+4,usable,30),Qt::AlignLeft|Qt::TextWordWrap,"Gold discoveries await audits; accepted blocks are separate.");}
    }
};
}
