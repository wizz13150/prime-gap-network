#pragma once
#include "ui/range_visuals.h"
#include "ui/position_formats.h"
#include "ui/public_profile_store.h"
#include "wallet/ticket_atlas_snapshot.h"
#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPointer>
#include <QTimer>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <cmath>
#include <functional>

namespace pgn::ui {
inline QString atlasSuperscript(int value){const QString digits="⁰¹²³⁴⁵⁶⁷⁸⁹";QString result;for(const auto c:QString::number(value))result+=digits[c.digitValue()];return result;}
inline QString atlasScientificLabel(const pgn::wallet::AtlasInteger& n){const auto digits=QString::fromStdString(n.str());return "≈ "+digits.left(1)+"."+digits.mid(1,3)+" × 10"+atlasSuperscript(digits.size()-1);}
inline constexpr qint64 AtlasBitUnit=qint64(1)<<40;
inline qint64 atlasBitCoordinate(const pgn::wallet::AtlasInteger& n){if(n<=0)return 0;const int k=boost::multiprecision::msb(n);const pgn::wallet::AtlasInteger base=pgn::wallet::AtlasInteger(1)<<k;return qint64(k)*AtlasBitUnit+((n-base)*AtlasBitUnit/base).convert_to<qint64>();}
inline pgn::wallet::AtlasInteger atlasBitInteger(qint64 coordinate){coordinate=std::max(qint64(0),coordinate);const int k=int(coordinate/AtlasBitUnit);const pgn::wallet::AtlasInteger base=pgn::wallet::AtlasInteger(1)<<k;return base+base*(coordinate%AtlasBitUnit)/AtlasBitUnit;}
inline QString ticketAtlasCoordinateLabel(const pgn::wallet::AtlasInteger& position,const pgn::wallet::AtlasInteger& span){
    const auto digits=QString::fromStdString(position.str());if(span<1000000000000LL)return digits;
    if(position>0&&(position&(position-1))==0)return "2"+atlasSuperscript(boost::multiprecision::msb(position));
    const int significant=std::clamp(int(digits.size()-QString::fromStdString(span.str()).size()+3),5,16);
    return "≈ "+digits.left(1)+"."+digits.mid(1,significant-1)+" × 10"+atlasSuperscript(digits.size()-1);
}
// Refactored ticket-only use of the atlas range tiles/spans. The reservation
// calendar, zones, context actions and absolute floating-point axis are absent.
class TicketAtlasView final:public QWidget {
public:
    using Reply=std::function<void(QJsonObject,QString)>;
    using Reader=std::function<void(const QString&,const QString&,int,Reply)>;
    std::function<void(const QString&)> inspect;
    std::function<void()> onViewportChanged;
private:
    using Int=pgn::wallet::AtlasInteger;
    Reader reader_;QJsonObject snapshot_;QString first_,last_,selected_;
    QTimer debounce_,poll_;QLabel *status_,*domain_;QVector<QPair<QRectF,QJsonObject>> hits_;
    QPoint pressed_;Int pressedFirst_{},pressedLast_{};bool dragging_{},busy_{},pending_{},autoFocused_{},bitScale_{},pressedBitScale_{},embedded_{};
    bool startupWarm_{};
    bool readable()const{return startupWarm_||isVisible()||(embedded_&&parentWidget()&&parentWidget()->isVisible());}
    static QColor color(size_t s){static const std::array<QColor,5> c{QColor("#70badb"),QColor("#e6bd64"),QColor("#ba9aee"),QColor("#63dfb3"),QColor("#f37b7e")};return c[s];}
    static QString stageTitle(size_t s){static const std::array<QString,5> names{"Assigned","Awaiting audits","Audits assigned","Audits passed","Audit failed"};return names[s];}
    QVector<size_t> visibleStages()const{QVector<size_t> result;const auto totals=snapshot_.value("totals").toObject();for(size_t s=0;s<5;++s)if(totals.value(pgn::wallet::AtlasStates[s]).toObject().value("tickets").toInteger()>0)result.append(s);if(result.isEmpty())result.append(0);return result;}
    QRectF plot()const{return {14,4,qreal(std::max(1,width()-28)),qreal(std::max(50,height()-(bitScale_?210:116)))};}
    Int first()const{return first_.isEmpty()?Int(0):Int(first_.toStdString());}
    Int last()const{return last_.isEmpty()?Int(1):Int(last_.toStdString());}
    static QString text(const Int& value){return QString::fromStdString(value.str());}
    static QString axisLabel(const Int& position,const Int& span){
        // Precision follows the visible span, not the absolute magnitude.
        // A zoomed axis must not print the same rounded value at every tick.
        return ticketAtlasCoordinateLabel(position,span);
    }
    double pixel(const Int& position)const{
        const auto area=plot();const Int a=first(),span=last()-a;
        if(bitScale_)return area.left()+area.width()*double(atlasBitCoordinate(position)-atlasBitCoordinate(a))/double(atlasBitCoordinate(last())-atlasBitCoordinate(a));
        // Conversion follows subtraction and bounded fixed-point projection.
        // Absolute 95-bit coordinates never pass through a floating-point value.
        const Int fixed=(position-a)*1000000/span;
        return area.left()+area.width()*fixed.convert_to<double>()/1000000.;
    }
    Int at(qreal x)const{const auto p=plot();const int n=std::clamp(int((x-p.left())*1000000/std::max(1.,p.width())),0,1000000);if(bitScale_){const auto a=atlasBitCoordinate(first()),b=atlasBitCoordinate(last());return std::clamp(atlasBitInteger(a+(Int(b-a)*n/1000000).convert_to<qint64>()),first(),last());}return first()+(last()-first())*n/1000000;}
    void viewport(Int a,Int b){
        if(snapshot_.isEmpty())return;
        const Int origin=pgn::wallet::atlasInteger(snapshot_.value("range_origin")),limit=pgn::wallet::atlasInteger(snapshot_.value("range_limit"));
        Int span=b-a;if(span<=0)span=1;if(span>limit-origin)span=limit-origin;
        if(a<origin)a=origin;if(a+span>limit)a=limit-span;
        first_=text(a);last_=text(a+span);setProperty("pgn_ticket_atlas_start",first_);setProperty("pgn_ticket_atlas_end",last_);update();
        const bool entire=a==origin&&a+span==limit;
        const auto bits=[](const Int& n){return n>0?int(boost::multiprecision::msb(n))+1:1;};
        const int lowerBits=bits(a),upperBits=bits(Int(a+span-1));
        bitScale_=upperBits-lowerBits>1;setProperty("pgn_ticket_atlas_bit_scale",bitScale_);if(!embedded_)setMinimumHeight(bitScale_?285:172);
        const auto bitLabel=lowerBits==upperBits?QString("%1 bits").arg(lowerBits):QString("%1–%2 bits").arg(lowerBits).arg(upperBits);
        domain_->setText(QString("%1 · %2 · %3").arg(entire?"Network domain":"Ticket area",bitLabel,bitScale_?"one band per bit":"linear scale"));
        domain_->setToolTip("Exact integer interval ["+text(a)+", "+text(a+span)+") · upper bound excluded. This is the network search domain, not detected hardware capacity.");
        if(onViewportChanged)onViewportChanged();
    }
    void requestSoon(){debounce_.start();}
    void fetch(){
        if(!reader_||!readable())return;if(busy_){pending_=true;return;}
        const QString start=first_,end=last_;busy_=true;pending_=false;const QPointer<TicketAtlasView> guard(this);
        reader_(start,end,256,[guard,start,end](QJsonObject value,QString error){
            if(!guard)return;guard->busy_=false;
            if(start!=guard->first_||end!=guard->last_){guard->requestSoon();return;}
            if(error.isEmpty())error=pgn::wallet::validateTicketAtlas(value,value.value("genesis").toString(),start,end,256);
            if(!error.isEmpty()){guard->status_->setText("Atlas unavailable · "+error);guard->setProperty("pgn_ticket_atlas_error",error);}
            else{
                guard->snapshot_=value;guard->setProperty("pgn_ticket_atlas_snapshot",value);guard->setProperty("pgn_ticket_atlas_error",QString{});
                guard->viewport(pgn::wallet::atlasInteger(value.value("start")),pgn::wallet::atlasInteger(value.value("end")));
                guard->status_->setText(QString("%1 tickets in view · %2 tracked · %3 · wheel to zoom, drag to move").arg(value.value("visible_assignments").toInteger()).arg(value.value("tracked_assignments").toInteger()).arg(value.value("details_truncated").toBool()?QString("Coverage density: click a band to zoom"):QString("Individual tickets: click to inspect")));
                if(!guard->autoFocused_){guard->autoFocused_=true;guard->focusActive();}
                if(value.value("start")==guard->first_&&value.value("end")==guard->last_)guard->startupWarm_=false;
            }
            if(guard->pending_)guard->requestSoon();
        });
    }
    void zoom(int steps,qreal x){
        if(snapshot_.isEmpty()||!steps)return;const Int anchor=at(x);Int span=last()-first();
        if(bitScale_){const auto pivot=atlasBitCoordinate(anchor);qint64 width=atlasBitCoordinate(last())-atlasBitCoordinate(first());if(steps>0)width>>=std::min(steps,8);else width<<=std::min(-steps,8);width=std::max(qint64(1),width);const int fraction=std::clamp(int((x-plot().left())*1000000/plot().width()),0,1000000);const auto low=pivot-(Int(width)*fraction/1000000).convert_to<qint64>();viewport(atlasBitInteger(low),atlasBitInteger(low+width));requestSoon();return;}
        if(steps>0)span>>=std::min(steps,8);else span<<=std::min(-steps,8);if(span<1)span=1;
        const int fraction=std::clamp(int((x-plot().left())*1000000/plot().width()),0,1000000);
        const Int a=anchor-span*fraction/1000000;viewport(a,a+span);requestSoon();
    }
protected:
    void showEvent(QShowEvent* event)override{QWidget::showEvent(event);QTimer::singleShot(0,this,[this]{fetch();});}
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),QColor("#03141b"));const auto area=plot();
        p.setPen(QColor("#a8dbdf"));QFont f=font();f.setPixelSize(12);p.setFont(f);
        const auto totals=snapshot_.value("totals").toObject();int lx=14,ly=int(area.bottom())+(bitScale_?126:42);
        for(size_t s=0;s<5;++s){const QString label=stageTitle(s)+" "+QString::number(totals.value(pgn::wallet::AtlasStates[s]).toObject().value("tickets").toInteger());p.setPen(color(s));const int w=p.fontMetrics().horizontalAdvance(label)+22;if(lx+w>width()-10){lx=14;ly+=15;}p.drawEllipse(QPointF(lx+3,ly-3),3,3);p.drawText(lx+11,ly,label);lx+=w;}
        hits_.clear();p.save();p.setClipRect(area);const auto lanes=visibleStages();const qreal row=area.height()/lanes.size();std::array<int,5> lane{-1,-1,-1,-1,-1};
        for(qsizetype i=0;i<lanes.size();++i){lane[lanes[i]]=int(i);p.fillRect(QRectF(area.left(),area.top()+i*row,area.width(),row-2),QColor(9,33,42));}
        if(!snapshot_.isEmpty()){
            const Int begin=first(),finish=last();
            const auto bins=snapshot_.value("bins").toArray();
            // At overview scale use short density bands, rather than hundreds
            // of full-height needles. Four exact native bins form one cell;
            // positions add, but ticket counts must not (a ticket may cross bins).
            if(snapshot_.value("details_truncated").toBool())for(qsizetype i=0;i<bins.size();i+=4){
                const auto left=bins[i].toObject(),right=bins[std::min(i+3,bins.size()-1)].toObject();
                const Int a=pgn::wallet::atlasInteger(left.value("start")),b=pgn::wallet::atlasInteger(right.value("end"));if(a>=finish||b<=begin)continue;
                const double x1=pixel(std::max(a,begin)),x2=pixel(std::min(b,finish));
                for(size_t s=0;s<5;++s){if(lane[s]<0)continue;Int occupied{};
                    for(qsizetype j=i;j<std::min(i+4,bins.size());++j)occupied+=pgn::wallet::atlasInteger(bins[j].toObject().value("stages").toObject().value(pgn::wallet::AtlasStates[s]).toObject().value("positions"));
                    if(occupied==0)continue;const double fraction=(occupied*1000000/(b-a)).convert_to<double>()/1000000.;
                    const qreal h=std::min(qreal(20),row-8),cy=area.top()+(lane[s]+.5)*row;
                    const QRectF cell(x1,cy-h/2,std::max(1.,x2-x1-2),h);
                    p.save();p.setOpacity(.4+.6*std::sqrt(std::clamp(fraction,0.,1.)));p.setPen(Qt::NoPen);p.setBrush(color(s));p.drawRoundedRect(cell,3,3);p.restore();
                    hits_.append({QRectF(x1,area.top()+lane[s]*row,std::max(1.,x2-x1),row-2),QJsonObject{{"atlas_bin",true},{"start",left.value("start")},{"end",right.value("end")}}});
                }
            }
            // The first100 details are a bounded response, not a representative
            // sample. Draw whole-schedule bins until all visible details fit.
            if(!snapshot_.value("details_truncated").toBool())for(const auto& entry:snapshot_.value("details").toArray()){
                const auto item=entry.toObject();const Int a=pgn::wallet::atlasInteger(item.value("start")),b=pgn::wallet::atlasInteger(item.value("end"));if(a>=finish||b<=begin)continue;
                size_t s{};while(s<5&&item.value("stage")!=pgn::wallet::AtlasStates[s])++s;if(s==5)continue;
                const double x1=pixel(std::max(a,begin)),x2=pixel(std::min(b,finish));
                if(lane[s]<0)continue;
                if(x2-x1<14||row<26){
                    const QPointF center((x1+x2)/2,area.top()+(lane[s]+.5)*row);p.setPen(QPen(item.value("request")==selected_?QColor("#ffffff"):color(s),item.value("request")==selected_?2:1));p.setBrush(color(s));p.drawRoundedRect(QRectF(center.x()-4,center.y()-4,8,8),2,2);
                    hits_.append({QRectF(center.x()-6,area.top()+lane[s]*row,12,row-2),item});continue;
                }
                const qreal h=std::min(qreal(28),row-8);const QRectF box(x1,area.top()+(lane[s]+.5)*row-h/2,x2-x1,h);
                const auto publicName=item.value("public_attribution").toObject().value("name").toString();const auto checked=canonicalPublicName(publicName);
                const auto owner=checked&&!checked->isEmpty()?*checked:QString("Unnamed miner");
                const auto label=box.width()<54?QString{}:"#"+item.value("ticket_number").toString()+(box.width()>150?" · "+owner:QString{});
                drawRangeTile(p,box,color(s),label,{},std::nullopt,item.value("request")==selected_);hits_.append({box,item});
            }
        }
        p.restore();p.setPen(QPen(QColor("#27515d"),1));
        if(!bitScale_)for(int i=0;i<=4;++i){const qreal x=area.left()+area.width()*i/4;p.drawLine(QPointF(x,area.top()),QPointF(x,area.bottom()+5));}
        p.setPen(QColor("#b2d8de"));f.setPixelSize(12);p.setFont(f);
        if(first_.isEmpty())p.drawText(QRectF(area.left(),area.bottom()+8,area.width(),18),Qt::AlignLeft,"Loading verified ticket ranges…");
        else if(bitScale_){
            f.setPixelSize(11);p.setFont(f);QJsonArray ticks;
            const int low=boost::multiprecision::msb(first())+1,high=boost::multiprecision::msb(Int(last()-1))+1;
            const qreal labelAngle=area.width()/std::max(1,high-low+1)<32?90:65;
            for(int bit=low;bit<=high;++bit){const Int boundary=std::max(first(),Int(Int(1)<<(bit-1)));const qreal x=pixel(boundary);p.setPen(QColor("#27515d"));p.drawLine(QPointF(x,area.top()),QPointF(x,area.bottom()+5));p.setPen(QColor("#b2d8de"));p.drawText(QRectF(x-12,area.bottom()+9,30,17),Qt::AlignCenter,QString::number(bit));p.save();p.translate(x,area.bottom()+36);p.rotate(labelAngle);p.drawText(QPointF(0,0),atlasScientificLabel(boundary));p.restore();ticks.append(QJsonObject{{"bits",bit},{"position",text(boundary)},{"scientific",atlasScientificLabel(boundary)}});}
            const qreal x=area.right();p.setPen(QColor("#537f8a"));p.drawLine(QPointF(x,area.top()),QPointF(x,area.bottom()+5));p.setPen(QColor("#b2d8de"));p.drawText(QRectF(x-24,area.bottom()+9,28,17),Qt::AlignCenter,"end");p.save();p.translate(x-1,area.bottom()+36);p.rotate(90);p.drawText(QPointF(0,0),atlasScientificLabel(last()));p.restore();setProperty("pgn_ticket_atlas_bit_ticks",ticks);
        }else{
            const Int span=last()-first();const int labelWidth=std::max(p.fontMetrics().horizontalAdvance(axisLabel(first(),span)),p.fontMetrics().horizontalAdvance(axisLabel(last(),span)))+32;
            const int divisions=area.width()>labelWidth*4?4:area.width()>labelWidth*2?2:1;
            for(int i=0;i<=divisions;++i){const Int position=first()+span*i/divisions;
                const auto label=axisLabel(position,span);
                const qreal w=p.fontMetrics().horizontalAdvance(label)+2,x=std::clamp(area.left()+area.width()*i/divisions-w/2,area.left(),area.right()-w);
                p.drawText(QRectF(x,area.bottom()+8,w,18),Qt::AlignCenter,label);
            }
        }
    }
    void wheelEvent(QWheelEvent* e)override{if(plot().contains(e->position())){zoom(e->angleDelta().y()>0?1:-1,e->position().x());e->accept();}else e->ignore();}
    void mousePressEvent(QMouseEvent* e)override{if(e->button()==Qt::LeftButton&&plot().contains(e->position())&&!snapshot_.isEmpty()){dragging_=true;pressed_=e->position().toPoint();pressedFirst_=first();pressedLast_=last();pressedBitScale_=bitScale_;setCursor(Qt::ClosedHandCursor);e->accept();}else QWidget::mousePressEvent(e);}
    void mouseMoveEvent(QMouseEvent* e)override{if(dragging_){const int dx=e->position().toPoint().x()-pressed_.x();if(pressedBitScale_){const auto a=atlasBitCoordinate(pressedFirst_),b=atlasBitCoordinate(pressedLast_);const auto delta=(Int(b-a)*dx/int(std::max(1.,plot().width()))).convert_to<qint64>();viewport(atlasBitInteger(a-delta),atlasBitInteger(b-delta));}else{const Int delta=(pressedLast_-pressedFirst_)*dx/int(std::max(1.,plot().width()));viewport(pressedFirst_-delta,pressedLast_-delta);}e->accept();}else QWidget::mouseMoveEvent(e);}
    void mouseReleaseEvent(QMouseEvent* e)override{if(!dragging_){QWidget::mouseReleaseEvent(e);return;}dragging_=false;setCursor(Qt::OpenHandCursor);
        if((e->position().toPoint()-pressed_).manhattanLength()<6){QStringList found;for(const auto& hit:hits_)if(hit.first.contains(e->position())){
                if(hit.second.value("atlas_bin").toBool()){viewport(pgn::wallet::atlasInteger(hit.second.value("start")),pgn::wallet::atlasInteger(hit.second.value("end")));requestSoon();return;}
                found.append(hit.second.value("request").toString());
            }
            if(found.size()==1){setSelected(found.first());if(inspect)inspect(selected_);return;}
            if(found.size()>1){zoom(2,e->position().x());return;}
        }
        requestSoon();e->accept();
    }
    void mouseDoubleClickEvent(QMouseEvent* e)override{if(plot().contains(e->position()))zoom(2,e->position().x());}
public:
    void prepareStartup(){startupWarm_=true;fetch();}
    explicit TicketAtlasView(Reader reader,QWidget* parent=nullptr):QWidget(parent),reader_(std::move(reader)){
        setObjectName("pgn-ticket-atlas");setMinimumHeight(172);setMaximumHeight(320);setCursor(Qt::OpenHandCursor);
        setFont(QFont("Segoe UI",10));
        setStyleSheet("QWidget#pgn-ticket-atlas{background:#03141b;border:1px solid #27515d;border-radius:8px;} QPushButton{color:#bce8e7;background:#0b2e36;border:1px solid #386674;border-radius:5px;padding:3px 9px;font:12px 'Segoe UI';} QPushButton:hover{background:#174d54;border-color:#65d4c7;} QPushButton:pressed{background:#20645f;} QLabel{color:#a8cdd4;font-family:'Segoe UI';}");
        auto* controls=new QHBoxLayout;controls->setContentsMargins(12,7,12,0);auto* caption=new QLabel("TICKET ATLAS");caption->setStyleSheet("font-weight:600;color:#bfe6e7");controls->addWidget(caption);domain_=new QLabel("Network search domain");domain_->setStyleSheet("color:#a8cdd4");controls->addWidget(domain_,1);
        auto* active=new QPushButton("Active area"),*all=new QPushButton("Network domain"),*minus=new QPushButton("−"),*plus=new QPushButton("+");active->setObjectName("pgn-atlas-active");all->setObjectName("pgn-atlas-overview");
        for(auto* button:{active,all,minus,plus}){button->setMaximumHeight(26);controls->addWidget(button);}plus->setFixedWidth(30);minus->setFixedWidth(30);
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,3);layout->addStretch();layout->addLayout(controls);status_=new QLabel("Reading the authenticated ticket map…");status_->setWordWrap(true);status_->setStyleSheet("color:#92bac6;font-size:12px");layout->addWidget(status_);layout->setStretch(0,1);
        connect(active,&QPushButton::clicked,this,[this]{focusActive();});connect(all,&QPushButton::clicked,this,[this]{if(snapshot_.isEmpty())return;viewport(pgn::wallet::atlasInteger(snapshot_.value("range_origin")),pgn::wallet::atlasInteger(snapshot_.value("range_limit")));requestSoon();});
        connect(plus,&QPushButton::clicked,this,[this]{zoom(1,plot().center().x());});connect(minus,&QPushButton::clicked,this,[this]{zoom(-1,plot().center().x());});
        debounce_.setSingleShot(true);debounce_.setInterval(120);connect(&debounce_,&QTimer::timeout,this,[this]{fetch();});poll_.setInterval(5000);connect(&poll_,&QTimer::timeout,this,[this]{if(readable())fetch();});poll_.start();
    }
    void focusActive(){if(!snapshot_.contains("observed_start"))return;const Int a=pgn::wallet::atlasInteger(snapshot_.value("observed_start")),b=pgn::wallet::atlasInteger(snapshot_.value("observed_end"));const Int margin=std::max(Int(1),Int((b-a)/20));viewport(a-margin,b+margin);requestSoon();}
    void setSelected(QString id){selected_=std::move(id);setProperty("pgn_ticket_atlas_selected",selected_);update();if(embedded_&&onViewportChanged)onViewportChanged();}
    void embed(){embedded_=true;hide();QTimer::singleShot(0,this,[this]{fetch();});}
    void embeddedCommand(const QJsonObject& command){
        const auto action=command.value("action").toString();
        if(action=="active")focusActive();
        else if(action=="bin"){
            const auto bins=snapshot_.value("bins").toArray();const int index=command.value("index").toInt(-1);
            if(index>=0&&index<bins.size()){const auto bin=bins[index].toObject();viewport(pgn::wallet::atlasInteger(bin.value("start")),pgn::wallet::atlasInteger(bin.value("end")));requestSoon();}
        }
        else if(action=="overview"&&!snapshot_.isEmpty()){viewport(pgn::wallet::atlasInteger(snapshot_.value("range_origin")),pgn::wallet::atlasInteger(snapshot_.value("range_limit")));requestSoon();}
        else if(action=="zoom")zoom(std::clamp(command.value("steps").toInt(),-4,4),plot().left()+plot().width()*std::clamp(command.value("x_ppm").toInt(500000),0,1000000)/1000000.);
        else if(action=="shift"&&!snapshot_.isEmpty()){
            const int delta=std::clamp(command.value("delta_ppm").toInt(),-1000000,1000000);
            if(bitScale_){const auto a=atlasBitCoordinate(first()),b=atlasBitCoordinate(last()),d=(Int(b-a)*delta/1000000).convert_to<qint64>();viewport(atlasBitInteger(a+d),atlasBitInteger(b+d));}
            else{const Int d=(last()-first())*delta/1000000;viewport(first()+d,last()+d);}requestSoon();
        }else if(action=="inspect"){
            const auto id=command.value("request").toString();for(const auto& row:snapshot_.value("details").toArray())if(row.toObject().value("request")==id){setSelected(id);if(inspect)inspect(id);break;}
        }
        if(onViewportChanged)onViewportChanged();
    }
    // Same inspection-point convention as the reused community renderer. An
    // empty identity addresses an aggregate cell, never an arbitrary ticket.
    QPointF inspectionPoint(const QString& id={})const{for(const auto& hit:hits_)if(id.isEmpty()?hit.second.value("atlas_bin").toBool():hit.second.value("request")==id)return hit.first.center();return {-1,-1};}
    QJsonObject auditProjection(const QJsonArray& audits,QWidget* surface)const{
        QJsonObject result;if(!surface||first_.isEmpty()||surface->width()<1||surface->height()<1)return result;
        if(startupWarm_&&(!autoFocused_||snapshot_.value("start")!=first_||snapshot_.value("end")!=last_))return result;
        if(embedded_){auto map=snapshot_;map.insert("start",first_);map.insert("end",last_);map.insert("bit_scale",bitScale_);map.insert("selected",selected_);result.insert("__atlas",map);}
        auto rows=audits;if(rows.isEmpty()){const auto details=snapshot_.value("details").toArray();for(int i=0;i<4&&!details.isEmpty();++i){auto row=details[std::min(details.size()-1,qsizetype(i)*details.size()/4)].toObject();row.insert("request_id",row.value("request"));rows.append(row);}}
        for(const auto& entry:rows){const auto row=entry.toObject();if(!row.value("start").isString()||!row.value("end").isString())continue;
            const Int a=pgn::wallet::atlasInteger(row.value("start")),b=pgn::wallet::atlasInteger(row.value("end"));if(b<=a)continue;const Int midpoint=a+(b-a)/2;
            auto point=inspectionPoint(row.value("request_id").toString());if(point.x()<0)point=QPointF(std::clamp(pixel(midpoint),plot().left(),plot().right()),plot().center().y());
            const auto mapped=surface->mapFromGlobal(mapToGlobal(point.toPoint()));
            const QJsonObject endpoint{{"x",std::clamp(double(mapped.x())/surface->width(),0.,1.)},{"y",std::clamp(double(mapped.y())/surface->height(),1.01,3.)}};
            result.insert(row.value("request_id").toString(),QJsonObject{{"source",endpoint},{"target",endpoint},{"outside",midpoint<first()||midpoint>=last()}});
        }return result;
    }
};
}
