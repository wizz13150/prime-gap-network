#include "ui/network_metrics_view.h"
#include <QDateTime>
#include <QEvent>
#include <QJsonArray>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>

namespace pgn::ui {
NetworkMetricsHub::NetworkMetricsHub(Reader reader,QObject* parent):QObject(parent),reader_(std::move(reader)){
    timer_.setInterval(5000);
    connect(&timer_,&QTimer::timeout,this,[this]{
        const bool visible=std::any_of(subscribers_.begin(),subscribers_.end(),[](const auto& item){return item.owner&&item.owner->isVisible();});
        if(visible||snapshot_.isEmpty())refresh();
    });
    timer_.start();
}
void NetworkMetricsHub::subscribe(QWidget* owner,std::function<void(const QJsonObject&,const QString&)> receive){
    subscribers_.push_back({owner,std::move(receive)});
    if(owner)owner->installEventFilter(this);
    if(!snapshot_.isEmpty()||!error_.isEmpty())subscribers_.back().receive(snapshot_,error_);
}
bool NetworkMetricsHub::eventFilter(QObject* watched,QEvent* event){
    if(event->type()==QEvent::Show){
        const QPointer<QWidget> page=qobject_cast<QWidget*>(watched);
        // Show arrives before normal child visibility is settled. One queued
        // shared refresh covers Mining as well as either graph, without a
        // second request for sibling shows or rapid tab switches.
        QTimer::singleShot(0,this,[this,page]{
            if(page&&page->isVisible()&&!busy_&&(!lastDispatch_.isValid()||lastDispatch_.elapsed()>=5000))refresh();
        });
    }
    return QObject::eventFilter(watched,event);
}
void NetworkMetricsHub::setSpan(int seconds){
    if(seconds!=3600&&seconds!=86400&&seconds!=604800)return;
    if(span_==seconds)return;span_=seconds;refresh();
}
void NetworkMetricsHub::refresh(){
    if(busy_){pending_=true;return;}
    lastDispatch_.start();busy_=true;const auto span=span_;const QPointer<NetworkMetricsHub> guard(this);
    reader_(span,[guard,span](QJsonObject snapshot,QString error){
        if(!guard)return;guard->busy_=false;
        if(span!=guard->span_){guard->pending_=false;guard->refresh();return;}
        if(error.isEmpty())guard->snapshot_=std::move(snapshot);
        const bool changed=guard->error_!=error;guard->error_=error;
        std::erase_if(guard->subscribers_,[](const auto& item){return item.owner.isNull();});
        const auto subscribers=guard->subscribers_;
        for(const auto& item:subscribers)if(item.owner)item.receive(guard->snapshot_,guard->error_);
        if(changed)guard->setProperty("pgn_network_metrics_error",error);
        if(guard->pending_){guard->pending_=false;guard->refresh();}
    });
}
namespace {
QString networkRate(double value){
    static const char* suffixes[]{"","k","M","G","T","P"};unsigned unit=0;
    while(value>=1000&&unit<5){value/=1000;++unit;}
    return QString::number(value,'f',value<10?2:value<100?1:0)+suffixes[unit]+" pos/s";
}
class NetworkMetricsView final:public QWidget {
public:
    NetworkMetricsView(NetworkMetricsHub* hub,bool compact):hub_(hub),compact_(compact){
        setObjectName(compact?"pgn-network-metrics-overview":"pgn-network-metrics-history");
        setMinimumHeight(compact?220:260);setMouseTracking(true);
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
        auto* row=new QHBoxLayout;status_=new QLabel("Network metrics · waiting for verified Core data");status_->setWordWrap(true);row->addWidget(status_,1);
        auto* choices=new QButtonGroup(this);choices->setExclusive(true);
        if(!compact)for(const auto& choice:std::vector<std::pair<QString,int>>{{"1 h",3600},{"24 h",86400},{"7 d",604800}}){
            auto* button=new QPushButton(choice.first);button->setCheckable(true);button->setChecked(choice.second==hub_->span());
            button->setStyleSheet("QPushButton{background:#08232b;color:#9fc8c3;border:1px solid #2a5a61;border-radius:6px;min-height:28px;padding:2px 10px;} QPushButton:checked{background:#17584f;color:#eafff5;border-color:#69efc5;} QPushButton:hover{border-color:#9ffff0;}");
            choices->addButton(button);button->setObjectName("pgn-network-metrics-span-"+QString::number(choice.second));buttons_.push_back({choice.second,button});
            connect(button,&QPushButton::clicked,this,[this,seconds=choice.second]{hub_->setSpan(seconds);});row->addWidget(button);
        }
        layout->addLayout(row);layout->addStretch();
        const QPointer<NetworkMetricsView> guard(this);
        hub_->subscribe(this,[guard](const QJsonObject& value,const QString& error){
            if(!guard)return;
            const bool changed=guard->snapshot_!=value||guard->error_!=error;
            if(guard->snapshot_!=value)guard->clearHover();
            guard->snapshot_=value;guard->error_=error;
            for(const auto& choice:guard->buttons_)choice.second->setChecked(choice.first==guard->hub_->span());
            QString caption=error.isEmpty()?QString("Network · ledger %1 · 10-minute chain-time estimate").arg(value.value("height").toInteger()):
                QString("Network metrics unavailable · %1%2").arg(error,value.isEmpty()?QString{}:QString(" · last verified snapshot retained"));
            if(guard->status_->text()!=caption)guard->status_->setText(caption);
            const QString style=error.isEmpty()?"color:#9bdacf":"color:#ffbd69";if(guard->status_->styleSheet()!=style)guard->status_->setStyleSheet(style);
            guard->setProperty("pgn_network_metrics_snapshot",value);guard->setProperty("pgn_network_metrics_error",error);
            if(changed)guard->update();
        });
    }
protected:
    void paintEvent(QPaintEvent*)override{
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        const auto points=snapshot_.value("points").toArray();
        QJsonArray chartGeometry;
        const qreal top=std::max(38,status_->geometry().bottom()+12);
        const QRectF available(0,top,width(),std::max(1.,height()-top));
        const bool columns=compact_||width()>=1000;
        for(int chart=0;chart<2;++chart){
            QRectF card=columns?QRectF(chart*(available.width()+12)/2,top,(available.width()-12)/2,available.height()):
                QRectF(0,top+chart*(available.height()+8)/2,available.width(),(available.height()-8)/2);
            painter.setPen(QColor("#256573"));painter.setBrush(QColor("#061b22"));painter.drawRoundedRect(card.adjusted(1,1,-1,-1),8,8);
            QFont titleFont("Segoe UI");titleFont.setPixelSize(compact_?11:13);titleFont.setWeight(QFont::DemiBold);painter.setFont(titleFont);painter.setPen(QColor("#bfece3"));
            painter.drawText(card.adjusted(12,5,-10,-5),Qt::AlignLeft|Qt::AlignTop,
                chart==0?(compact_?"DIFFICULTY · merit":"NETWORK DIFFICULTY · target merit"):(compact_?"NETWORK HASHRATE · pos/s":"NETWORK HASHRATE · search positions/s"));
            QRectF plot=card.adjusted(68,55,-16,-32);plots_[chart]=plot;
            if(plot.width()<8||plot.height()<8)continue;
            if(points.isEmpty()){
                setProperty(chart==0?"pgn_merit_plot_bounds":"pgn_rate_plot_bounds",QVariantList{});
                painter.setPen(QColor("#91acb4"));painter.drawText(plot,Qt::AlignCenter,"Waiting for chain observations");continue;
            }
            const double first=points.first().toObject().value("time").toDouble(),last=points.last().toObject().value("time").toDouble();
            const double left=first==last?first-30:first,right=first==last?last+30:last;
            double low=chart==0?std::numeric_limits<double>::max():0,high=0;
            for(const auto& entry:points){
                const auto point=entry.toObject();const auto seconds=point.value("window_seconds").toDouble();
                if(chart==0){const auto merit=point.value("merit_target_q16").toDouble()/65536.;low=std::min(low,merit);high=std::max(high,merit);}
                else if(seconds>0)high=std::max(high,point.value("reported_positions").toString().toDouble()/seconds);
            }
            if(chart==0){const auto margin=std::max(.05,(high-low)*.1);low=std::max(0.,low-margin);high+=margin;}
            else high=high>0?high*1.08:1.;
            const auto map=[&](double time,double value){return QPointF(plot.left()+(time-left)/(right-left)*plot.width(),plot.bottom()-(value-low)/(high-low)*plot.height());};
            QFont axisFont("Segoe UI");axisFont.setPixelSize(11);painter.setFont(axisFont);
            const int verticalTicks=std::clamp(int(plot.height()/32)+1,3,6);
            const int precision=std::clamp(int(std::ceil(-std::log10(high-low)))+2,2,6);
            for(int tick=0;tick<verticalTicks;++tick){
                const double value=low+(high-low)*tick/(verticalTicks-1);const qreal y=map(left,value).y();
                painter.setPen(QColor("#20424b"));painter.drawLine(QPointF(plot.left(),y),QPointF(plot.right(),y));
                painter.setPen(QColor("#91b5bd"));
                auto label=chart==0?QString::number(value,'f',precision):networkRate(value).replace(" pos/s","");
                painter.drawText(QRectF(card.left()+4,y-8,60,16),Qt::AlignRight|Qt::AlignVCenter,label);
            }
            for(int series=0;series<(chart==0?1:2);++series){
                QPainterPath path;bool begun=false;
                for(const auto& entry:points){
                    const auto point=entry.toObject();const double seconds=point.value("window_seconds").toDouble();
                    if(chart==1&&seconds<=0){begun=false;continue;}
                    const double value=chart==0?point.value("merit_target_q16").toDouble()/65536.:
                        point.value(series==0?"reported_positions":"audited_positions").toString().toDouble()/seconds;
                    const auto at=map(point.value("time").toDouble(),value);
                    if(begun)path.lineTo(at);else path.moveTo(at);begun=true;
                }
                painter.save();painter.setClipRect(plot.adjusted(-2,-2,2,2));
                painter.setPen(QPen(chart==0?QColor("#ffd166"):series==0?QColor("#65e6c2"):QColor("#68baff"),1.7));
                painter.setBrush(Qt::NoBrush);painter.drawPath(path);
                if(points.size()==1){const auto point=points.last().toObject();const auto seconds=point.value("window_seconds").toDouble();if(chart==0||seconds>0){const auto value=chart==0?point.value("merit_target_q16").toDouble()/65536.:point.value(series==0?"reported_positions":"audited_positions").toString().toDouble()/seconds;painter.drawEllipse(map(last,value),2.5,2.5);}}
                painter.restore();
            }
            painter.setPen(QColor("#91b5bd"));
            const bool changesDay=QDateTime::fromSecsSinceEpoch(qint64(first)).toLocalTime().date()!=QDateTime::fromSecsSinceEpoch(qint64(last)).toLocalTime().date();
            const auto timeFormat=changesDay||right-left>86400?QString("dd/MM HH:mm"):QString("HH:mm");
            const int horizontalTicks=std::clamp(int(plot.width()/115)+1,2,6);
            for(int tick=0;tick<horizontalTicks;++tick){
                const double stamp=left+(right-left)*tick/(horizontalTicks-1);const qreal x=map(stamp,low).x();
                painter.setPen(QColor("#16363f"));painter.drawLine(QPointF(x,plot.top()),QPointF(x,plot.bottom()));
                painter.setPen(QColor("#91b5bd"));
                const QString label=QDateTime::fromSecsSinceEpoch(qint64(stamp)).toLocalTime().toString(timeFormat);
                const qreal labelWidth=std::min(108.,plot.width());const qreal at=std::clamp(x-labelWidth/2,plot.left(),plot.right()-labelWidth);
                painter.drawText(QRectF(at,plot.bottom()+7,labelWidth,17),Qt::AlignHCenter,label);
            }
            if(hoverChart_==chart&&hoverIndex_>=0&&hoverIndex_<points.size()){
                const auto sample=points[hoverIndex_].toObject();const double seconds=sample.value("window_seconds").toDouble();
                const double value=chart==0?sample.value("merit_target_q16").toDouble()/65536.:seconds>0?sample.value("reported_positions").toString().toDouble()/seconds:0.;
                if(chart==0||seconds>0){
                    const auto selected=map(sample.value("time").toDouble(),value);painter.save();painter.setClipRect(plot.adjusted(-4,-4,4,4));
                    painter.setPen(QPen(QColor("#b0d6dc"),1,Qt::DashLine));painter.drawLine(QPointF(selected.x(),plot.top()),QPointF(selected.x(),plot.bottom()));painter.drawLine(QPointF(plot.left(),selected.y()),QPointF(plot.right(),selected.y()));
                    painter.setPen(QPen(chart==0?QColor("#ffd166"):QColor("#65e6c2"),2));painter.setBrush(QColor("#061b22"));painter.drawEllipse(selected,3.5,3.5);painter.restore();
                }
            }
            chartGeometry.append(QJsonObject{{"title",chart==0?"Difficulty · target merit":"Network hashrate · search positions/s"},{"x",plot.x()},{"y",plot.y()},{"width",plot.width()},{"height",plot.height()},{"vertical_ticks",verticalTicks},{"horizontal_ticks",horizontalTicks},{"samples",points.size()},{"axis_font_px",11}});
            const auto latest=points.last().toObject();const double seconds=latest.value("window_seconds").toDouble();
            painter.setPen(chart==0?QColor("#ffd166"):QColor("#65e6c2"));
            const QString current=chart==0?QString::number(latest.value("merit_target_q16").toDouble()/65536.,'f',4):
                seconds>0?networkRate(latest.value("reported_positions").toString().toDouble()/seconds):QString("Not enough chain time");
            painter.drawText(QRectF(card.left()+12,card.top()+23,card.width()-24,16),Qt::AlignLeft,current+(chart==1&&!compact_?QString(" · reported; audited in blue"):QString{}));
            painter.setPen(QColor("#83aaa9"));const QString caption=chart==0?QString("%1 chain samples").arg(points.size()):QString("%1 chain samples · %2 min window").arg(points.size()).arg(seconds/60.,0,'g',3);
            painter.drawText(QRectF(card.left()+12,card.top()+39,card.width()-24,14),Qt::AlignLeft,caption);
            setProperty(chart==0?"pgn_merit_plot_bounds":"pgn_rate_plot_bounds",QVariantList{left,right,low,high});
        }
        setProperty("pgn_network_chart_geometry",chartGeometry);
    }
    void mouseMoveEvent(QMouseEvent* event)override{
        const auto points=snapshot_.value("points").toArray();if(points.isEmpty()){clearHover();return;}
        for(int chart=0;chart<2;++chart)if(plots_[chart].contains(event->position())){
            const auto bounds=property(chart==0?"pgn_merit_plot_bounds":"pgn_rate_plot_bounds").toList();if(bounds.size()!=4)return;
            const double time=bounds[0].toDouble()+(event->position().x()-plots_[chart].left())/plots_[chart].width()*(bounds[1].toDouble()-bounds[0].toDouble());
            QJsonObject nearest;int nearestIndex=-1,index=0;double distance=std::numeric_limits<double>::max();
            for(const auto& value:points){const auto point=value.toObject();const auto delta=std::abs(time-point.value("time").toDouble());if(delta<distance){distance=delta;nearest=point;nearestIndex=index;}++index;}
            const auto seconds=nearest.value("window_seconds").toDouble();
            const auto label=QDateTime::fromSecsSinceEpoch(nearest.value("time").toInteger()).toLocalTime().toString("dd/MM HH:mm:ss");
            QString text=label+QString(" · ledger %1\nTarget merit: %2").arg(nearest.value("height").toInteger()).arg(nearest.value("merit_target_q16").toDouble()/65536.,0,'f',4);
            if(seconds>0)text+="\nReported: "+networkRate(nearest.value("reported_positions").toString().toDouble()/seconds)+"\nAudited: "+networkRate(nearest.value("audited_positions").toString().toDouble()/seconds);
            text+="\nSearch positions from report confirmations over chain time. Audits are excluded from throughput; historical audit qualification is evaluated at the current snapshot tip.";
            const QJsonObject hover{{"chart",chart},{"sample_index",nearestIndex},{"height",nearest.value("height")},{"time",nearest.value("time")},{"merit_target_q16",nearest.value("merit_target_q16")},{"reported_positions",nearest.value("reported_positions")},{"audited_positions",nearest.value("audited_positions")},{"window_seconds",nearest.value("window_seconds")}};
            setProperty("pgn_network_chart_hover",hover);if(hoverChart_!=chart||hoverIndex_!=nearestIndex){hoverChart_=chart;hoverIndex_=nearestIndex;update();}
            setToolTip(text);return;
        }
        clearHover();
    }
    void leaveEvent(QEvent* event)override{clearHover();QWidget::leaveEvent(event);}
private:
    void clearHover(){setToolTip({});setProperty("pgn_network_chart_hover",QJsonObject{});if(hoverChart_!=-1){hoverChart_=hoverIndex_=-1;update();}}
    int hoverChart_{-1},hoverIndex_{-1};
    NetworkMetricsHub* hub_;bool compact_;QLabel* status_{};QJsonObject snapshot_;QString error_;QRectF plots_[2];
    std::vector<std::pair<int,QPushButton*>> buttons_;
};
}
QWidget* createNetworkMetricsView(NetworkMetricsHub* hub,bool compact){return new NetworkMetricsView(hub,compact);}
}
