#pragma once

#include "horizon_difficulty_index.h"
#include "horizon_graph_controls.h"
#include "graph_border_ticks.h"
#include <QDateTime>
#include <QFontMetrics>
#include <QMetaObject>
#include <QResizeEvent>
#include <QShowEvent>
#include <QThread>
#include <QTimer>
#include <QTimeZone>
#include <QWheelEvent>
#include <atomic>

// All producer methods run on the GUI thread and only enqueue bounded work.
// The worker owns the UTC index. Paint uses an already-decimated frame only:
// no source reads, RPC, archive scans, or corpus-sized copies on the UI thread.
class HorizonDifficultyGraph final : public QWidget {
public:
    explicit HorizonDifficultyGraph(HorizonDifficulty::Coin coin, QWidget* parent = nullptr)
        : QWidget(parent), m_coin(coin), m_sessionUtc(QDateTime::currentMSecsSinceEpoch()),
          m_index(std::make_shared<HorizonDifficulty::Index>())
    {
        setObjectName(QStringLiteral("horizonDifficultyGraph"));
        setMouseTracking(true); setFocusPolicy(Qt::StrongFocus);
        m_worker = new QObject; m_worker->moveToThread(&m_thread);
        connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
        m_thread.start();
        auto* timer = new QTimer(this); timer->setInterval(1000);
        connect(timer, &QTimer::timeout, this, [this] { requestFrame(); });
        timer->start();
    }
    ~HorizonDifficultyGraph() override
    {
        m_stopping = true; m_thread.quit(); m_thread.wait();
    }
    // A generation is a single real network source/cache identity. Historical
    // backfill must use the same identity, or explicitly replace the generation.
    // This is also the reorg/correction boundary; resetting is never tied to a job.
    void setSource(QString generation, QString provenance, QString historyState)
    {
        if (generation != m_generation) {
            m_generation = std::move(generation); ++m_sourceEpoch;
            const auto index = m_index;
            QMetaObject::invokeMethod(m_worker, [index] { index->clear(); }, Qt::QueuedConnection);
            m_frame = {}; m_haveFrame = false;
        }
        m_provenance = std::move(provenance); m_historyState = std::move(historyState);
        ++m_revision; requestFrame(); update();
    }
    void setHistoryState(QString reason)
    {
        // Preserve earlier genuine observations through disconnects.
        m_historyState = std::move(reason); update();
    }
    bool appendBatch(HorizonDifficulty::Coin coin, const QString& generation,
                     std::vector<HorizonDifficulty::Sample> samples,
                     std::function<void()> consumed = {})
    {
        if (coin != m_coin || generation != m_generation || generation.isEmpty() ||
            samples.empty() || samples.size() > 4096) return false;
        // Invalid input rejects the complete batch, never silently fabricates
        // values or substitutes receipt/current time for a missing timestamp.
        for (const auto& s : samples)
            if (s.utcMs <= 0 || s.utcMs >= 253402300800000LL ||
                !std::isfinite(s.value) || s.value <= 0 ||
                (s.clock != HorizonDifficulty::Clock::BlockUtc &&
                 s.clock != HorizonDifficulty::Clock::ObservationUtc)) return false;
        ++m_revision;
        const auto index = m_index;
        QMetaObject::invokeMethod(m_worker,
            [this, index, samples = std::move(samples), consumed = std::move(consumed)]() mutable {
                if (m_stopping) return;
                for (const auto& sample : samples) index->insert(sample);
                if (!m_stopping) QMetaObject::invokeMethod(this,
                    [this, consumed = std::move(consumed)] {
                        if (consumed) consumed();
                        requestFrame();
                    }, Qt::QueuedConnection);
            }, Qt::QueuedConnection);
        return true;
    }
    bool appendObservation(HorizonDifficulty::Coin coin, const QString& generation,
                           qint64 utcMs, double value,
                           HorizonDifficulty::Clock clock = HorizonDifficulty::Clock::ObservationUtc)
    {
        return appendBatch(coin, generation, {{utcMs, value, clock}});
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event); requestFrame();
    }
    void showEvent(QShowEvent* event) override
    {
        QWidget::showEvent(event); requestFrame();
    }
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        const QRectF graph(rect()); const QRectF plot = plotRect();
        painter.fillRect(rect(), QColor(2,12,20));
        painter.setBrush(Qt::NoBrush); painter.setPen(QPen(QColor(44,92,103,170),1));
        painter.drawRoundedRect(graph.adjusted(.5,.5,-.5,-.5),14,14);
        painter.setFont(HorizonDeviceVisuals::font(8,QFont::DemiBold,true));
        painter.setPen(QColor("#78b9c6"));
        const QString title = coinLabel() + QStringLiteral(" · NETWORK DIFFICULTY");
        painter.drawText(QRectF(16,7,std::max(1.,graph.width()-584),18),Qt::AlignLeft,
            QFontMetrics(painter.font()).elidedText(title,Qt::ElideRight,int(std::max(1.,graph.width()-584))));
        for (int i=0;i<int(FrontierTimeRangePresetCount);++i)
            HorizonGraphControls::drawTimeRange(painter,HorizonGraphControls::timeRangeButton(graph,i),i,!m_zoomed&&m_range==i);
        HorizonGraphControls::drawReset(painter,HorizonGraphControls::reset(graph),m_zoomed);
        HorizonGraphControls::drawToggle(painter,HorizonGraphControls::current(graph),QStringLiteral("CURRENT OBS."),m_currentOnly);
        HorizonGraphControls::drawToggle(painter,HorizonGraphControls::history(graph),QStringLiteral("FULL HISTORY"),!m_currentOnly);
        painter.setFont(HorizonDeviceVisuals::font(7,QFont::Normal,true));
        painter.setPen(QColor("#7dabae"));
        const QString status = m_historyState.isEmpty()
            ? QStringLiteral("NO NETWORK SOURCE · historical provider not connected") : m_historyState;
        painter.drawText(QRectF(16,50,width()-32,17),Qt::AlignLeft,
            QFontMetrics(painter.font()).elidedText(status,Qt::ElideRight,std::max(1,width()-32)));
        if (plot.width()<1 || plot.height()<1) return;
        const auto view = displayedView();
        const auto yTicks=FrontierBorderTicks(view.minMerit,view.maxMerit,
            FrontierVisibleMeritTicks(view,std::clamp(int(plot.height()/30.0),3,12),false),
            plot.height(),22.);
        QVariantList yValues;
        for(const auto value:yTicks){
            yValues.append(value);
            const qreal y=plot.bottom()-(value-view.minMerit)/(view.maxMerit-view.minMerit)*plot.height();
            painter.setPen(QPen(QColor("#10333d"),1));painter.drawLine(QPointF(plot.left(),y),QPointF(plot.right(),y));
            painter.setPen(QColor("#78b9c6"));painter.drawLine(QPointF(plot.left()-4,y),QPointF(plot.left(),y));
            painter.drawLine(QPointF(plot.right(),y),QPointF(plot.right()+4,y));
            painter.drawText(QRectF(19,y-8,plot.left()-25,16),Qt::AlignRight|Qt::AlignVCenter,QString::number(value,'g',9));
        }
        const double span=view.maxTimeMs-view.minTimeMs;
        const auto step=FrontierNiceTimeStepMs(span/std::clamp(int(plot.width()/110.0),2,12));
        double utc=std::ceil(view.minTimeMs/step)*step;
        std::vector<double> interior;
        for(int i=0;i<64&&utc<=view.maxTimeMs;++i){
            interior.push_back(utc);
            const auto next=utc+step;if(next<=utc)break;utc=next;
        }
        const auto format=span<60000.?QStringLiteral("HH:mm:ss.zzz"):
            span<86400000.?QStringLiteral("HH:mm:ss"):QStringLiteral("yyyy-MM-dd HH:mm");
        const qreal labelWidth=QFontMetricsF(painter.font()).horizontalAdvance(
            QDateTime::fromMSecsSinceEpoch(qRound64(view.minTimeMs),Qt::UTC).toString(format))+10.;
        const auto xTicks=FrontierBorderTicks(view.minTimeMs,view.maxTimeMs,
            std::move(interior),plot.width(),labelWidth+8.);
        QVariantList xValues;
        for(const auto utc:xTicks){
            xValues.append(utc);
            const qreal x=plot.left()+(utc-view.minTimeMs)/span*plot.width();
            painter.setPen(QPen(QColor("#10333d"),1));painter.drawLine(QPointF(x,plot.top()),QPointF(x,plot.bottom()));
            painter.setPen(QColor("#78b9c6"));painter.drawLine(QPointF(x,plot.bottom()),QPointF(x,plot.bottom()+4));
            painter.drawLine(QPointF(x,plot.top()-4),QPointF(x,plot.top()));
            const qreal left=std::clamp(x-labelWidth/2.,2.,std::max(2.,width()-labelWidth-2.));
            painter.drawText(QRectF(left,plot.bottom()+4,labelWidth,16),Qt::AlignCenter,
                QDateTime::fromMSecsSinceEpoch(qRound64(utc),Qt::UTC).toString(format));
        }
        setProperty("horizon_difficulty_plot",plot);
        setProperty("horizon_difficulty_view",QVariantList{view.minTimeMs,view.maxTimeMs,view.minMerit,view.maxMerit});
        setProperty("horizon_difficulty_x_ticks",xValues);setProperty("horizon_difficulty_y_ticks",yValues);
        setProperty("horizon_difficulty_visible_samples",qulonglong(m_frame.visible.timestamps));
        painter.save(); painter.setClipRect(plot);
        if (m_haveFrame && !m_frame.columns.empty()) {
            const auto y=[&](double v) { return plot.bottom()-(v-view.minMerit)/(view.maxMerit-view.minMerit)*plot.height(); };
            const auto x=[&](double t) { return plot.left()+(t-view.minTimeMs)/(view.maxTimeMs-view.minTimeMs)*plot.width(); };
            for (const auto& column : m_frame.columns) {
                if (column.samples.empty()) {
                    // Amber coverage rail = no observation in this UTC bucket.
                    // No connected line is ever drawn across any missing data.
                    painter.fillRect(QRectF(x(column.fromUtcMs),plot.bottom()-3,
                        std::max(1.,x(column.untilUtcMs)-x(column.fromUtcMs)),3),QColor(170,113,49,100));
                    continue;
                }
                const qreal xx=x((double(column.samples.firstUtcMs)+column.samples.lastUtcMs)/2.);
                painter.setPen(QPen(QColor("#f2c76e"),1.2));
                painter.drawLine(QPointF(xx,y(column.samples.minimum)),QPointF(xx,y(column.samples.maximum)));
                painter.drawPoint(QPointF(xx,y(column.samples.minimum)));
                painter.drawPoint(QPointF(xx,y(column.samples.maximum)));
            }
        }
        if (!m_haveFrame || m_frame.visible.empty()) {
            painter.setPen(QColor("#77969c"));
            painter.drawText(plot.adjusted(10,10,-10,-10),Qt::AlignCenter|Qt::TextWordWrap,
                m_generation.isEmpty() ? QStringLiteral("NETWORK DIFFICULTY HISTORY UNAVAILABLE\nNo verified network source connected") :
                QStringLiteral("NO NETWORK OBSERVATIONS IN THIS WINDOW\nEarlier history is not inferred"));
        }
        painter.restore();
        // A viewport edge is a real coordinate even when no rounded grid tick
        // lands on it. Keep the same frame after every pan/zoom and resize.
        painter.setBrush(Qt::NoBrush);painter.setPen(QPen(QColor("#78b9c6"),1));
        painter.drawRect(plot);
        painter.setPen(QColor("#5d9eaa"));
        painter.drawText(QRectF(plot.left(),plot.bottom()+22,plot.width(),18),Qt::AlignCenter,
            QStringLiteral("TIME · UTC  |  point samples / pixel min–max  |  amber = no observation"));
        painter.save(); painter.translate(12,plot.center().y()); painter.rotate(-90);
        painter.drawText(QRectF(-plot.height()/2,-8,plot.height(),16),Qt::AlignCenter,unitLabel());
        painter.restore();
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button()!=Qt::LeftButton) { QWidget::mousePressEvent(event); return; }
        const QRectF graph(rect());
        for(int i=0;i<int(FrontierTimeRangePresetCount);++i)
            if(HorizonGraphControls::timeRangeButton(graph,i).contains(event->position())) {
                m_range=i; m_zoomed=false; requestFrame(); event->accept(); return;
            }
        if(HorizonGraphControls::reset(graph).contains(event->position())) m_zoomed=false;
        else if(HorizonGraphControls::current(graph).contains(event->position())) m_currentOnly=true;
        else if(HorizonGraphControls::history(graph).contains(event->position())) m_currentOnly=false;
        else if(plotRect().contains(event->position()) ||
                QRectF(plotRect().left(),plotRect().bottom(),plotRect().width(),28).contains(event->position()) ||
                QRectF(2,plotRect().top(),plotRect().left()-4,plotRect().height()).contains(event->position())) {
            m_dragging=true; m_dragAnchor=event->position(); m_dragView=displayedView();
            m_dragAxis=event->position().y()>plotRect().bottom()?1:
                event->position().x()<plotRect().left()?2:0;
            setCursor(m_dragAxis==1?Qt::SizeHorCursor:m_dragAxis==2?Qt::SizeVerCursor:Qt::ClosedHandCursor);
            event->accept(); return;
        } else { QWidget::mousePressEvent(event); return; }
        requestFrame(); event->accept();
    }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        if(m_dragging) {
            const auto delta=event->position()-m_dragAnchor;
            const double dx=delta.x()/std::max(1.,plotRect().width()),dy=delta.y()/std::max(1.,plotRect().height());
            m_zoomView=m_dragAxis?FrontierPanSingleGraphAxis(m_dragView,m_dragAxis==1,m_dragAxis==1?dx:dy):
                FrontierPanGraphView(m_dragView,dx,dy);
            m_zoomed=true; requestFrame(); event->accept(); return;
        }
        QWidget::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if(event->button()==Qt::LeftButton) { m_dragging=false; unsetCursor(); }
        QWidget::mouseReleaseEvent(event);
    }
    void wheelEvent(QWheelEvent* event) override
    {
        const auto plot=plotRect();
        if(!plot.contains(event->position())) { event->ignore(); return; }
        const double steps=event->pixelDelta().isNull()?event->angleDelta().y()/120.:event->pixelDelta().y()/50.;
        m_zoomView=FrontierZoomGraphView(m_zoomed?m_zoomView:displayedView(),(event->position().x()-plot.left())/plot.width(),
            (event->position().y()-plot.top())/plot.height(),steps);
        m_zoomed=true; requestFrame(); event->accept();
    }
    bool event(QEvent* event) override
    {
        if(event->type()==QEvent::ToolTip) {
            auto* help=static_cast<QHelpEvent*>(event);
            for(int i=0;i<int(FrontierTimeRangePresetCount);++i)
                if(HorizonGraphControls::timeRangeButton(QRectF(rect()),i).contains(help->pos())) {
                    QToolTip::showText(help->globalPos(),HorizonGraphControls::timeRangeTooltip(i),this); return true;
                }
            QString tip=m_provenance+QStringLiteral("\n")+m_historyState+
                QStringLiteral("\nFULL HISTORY / ALL means all observations loaded into this source generation, not a claim of complete blockchain coverage. CURRENT OBS. filters from this UI session start; jobs never reset the index.");
            const auto plot=plotRect();
            if(m_haveFrame&&plot.contains(help->pos())&&!m_frame.columns.empty()) {
                const auto i=std::clamp(int((help->pos().x()-plot.left())/plot.width()*m_frame.columns.size()),0,int(m_frame.columns.size())-1);
                const auto& c=m_frame.columns[std::size_t(i)];
                tip+=QStringLiteral("\n%1 — %2 UTC").arg(
                    QDateTime::fromMSecsSinceEpoch(c.fromUtcMs,Qt::UTC).toString(Qt::ISODateWithMs),
                    QDateTime::fromMSecsSinceEpoch(c.untilUtcMs,Qt::UTC).toString(Qt::ISODateWithMs));
                tip+=c.samples.empty()?QStringLiteral("\nNO OBSERVATION; no interpolation"):
                    QStringLiteral("\nMin %1 · Max %2 · %3 distinct UTC timestamps\nClock: %4")
                        .arg(c.samples.minimum,0,'g',15).arg(c.samples.maximum,0,'g',15)
                        .arg(qulonglong(c.samples.timestamps)).arg(c.samples.clocks==1?QStringLiteral("block UTC"):
                            c.samples.clocks==2?QStringLiteral("observation UTC (not block time)"):QStringLiteral("block UTC + observation UTC"));
            }
            QToolTip::showText(help->globalPos(),tip,this); return true;
        }
        return QWidget::event(event);
    }
private:
    QRectF plotRect() const { return QRectF(rect()).adjusted(88,76,-70,-48); }
    QString coinLabel() const
    {
        switch(m_coin) { case HorizonDifficulty::Coin::Gapcoin:return QStringLiteral("GAPCOIN");
        case HorizonDifficulty::Coin::Riecoin:return QStringLiteral("RIECOIN");
        case HorizonDifficulty::Coin::Primecoin:return QStringLiteral("PRIMECOIN"); }
        return {};
    }
    QString unitLabel() const
    {
        switch(m_coin) { case HorizonDifficulty::Coin::Gapcoin:return QStringLiteral("GAP · NETWORK DIFFICULTY");
        case HorizonDifficulty::Coin::Riecoin:return QStringLiteral("RIC · NETWORK DIFFICULTY");
        case HorizonDifficulty::Coin::Primecoin:return QStringLiteral("XPM · NETWORK CHAIN DIFFICULTY"); }
        return {};
    }
    FrontierGraphView displayedView() const
    {
        // Keep hover and geometry coherent with the cached frame during an
        // asynchronous viewport request. Never relabel old buckets as new ones.
        if(m_haveFrame&&m_presentedZoomed)return m_presentedZoomView;
        const auto now=QDateTime::currentMSecsSinceEpoch();
        double low=0,high=1;
        if(!m_frame.visible.empty()) {
            low=m_frame.visible.minimum; high=m_frame.visible.maximum;
            const auto pad=std::max((high-low)*.08,std::max(std::abs(high)*1.e-5,1.e-7));
            low=std::max(0.,low-pad); high+=pad;
        }
        return {m_haveFrame?double(m_frame.fromUtcMs):double(now-60000),
                m_haveFrame?double(m_frame.untilUtcMs):double(now),low,high};
    }
    void requestFrame()
    {
        if(!isVisible() || m_stopping) return;
        if(m_queryPending) { m_queryAgain=true; return; }
        m_queryPending=true; m_queryAgain=false;
        const int pixels=std::clamp(int(plotRect().width()),1,8192);
        const auto now=QDateTime::currentMSecsSinceEpoch();
        const auto preset=FrontierTimeRangePresets[m_range];
        const auto zoom=m_zoomView; const bool zoomed=m_zoomed, current=m_currentOnly;
        const auto session=m_sessionUtc;
        const auto epoch=m_sourceEpoch, revision=m_revision;
        const auto index=m_index;
        QMetaObject::invokeMethod(m_worker,[this,index,now,preset,zoom,zoomed,current,session,epoch,revision,pixels] {
            if(m_stopping)return;
            const auto retained=index->retained();
            const std::int64_t right=zoomed?std::int64_t(zoom.maxTimeMs):std::max<std::int64_t>(now,retained.lastUtcMs+1);
            const std::int64_t left=zoomed?std::int64_t(zoom.minTimeMs):preset.allHistory?
                (retained.empty()?right-60000:std::min(retained.firstUtcMs,right-1000)):right-preset.spanMs;
            // Keep viewport and scope independent. Out-of-scope buckets remain
            // explicit blanks; changing CURRENT must not secretly pan the view.
            auto frame=index->frame(left,right,pixels);
            if(current) {
                frame.visible={};
                for(auto& c:frame.columns) {
                    c.samples=index->interval(std::max<std::int64_t>(c.fromUtcMs,session),c.untilUtcMs);
                    frame.visible=HorizonDifficulty::merge(frame.visible,c.samples);
                }
            }
            if(!m_stopping)QMetaObject::invokeMethod(this,[this,frame=std::move(frame),epoch,revision,zoomed,zoom]() mutable {
                m_queryPending=false;
                if(epoch==m_sourceEpoch) {
                    m_frame=std::move(frame); m_haveFrame=true;
                    m_presentedZoomed=zoomed; m_presentedZoomView=zoom; update();
                }
                if(m_queryAgain||revision!=m_revision) requestFrame();
            },Qt::QueuedConnection);
        },Qt::QueuedConnection);
    }
    HorizonDifficulty::Coin m_coin;
    qint64 m_sessionUtc;
    std::shared_ptr<HorizonDifficulty::Index> m_index;
    QObject* m_worker{}; QThread m_thread; std::atomic_bool m_stopping{false};
    QString m_generation,m_provenance,m_historyState;
    HorizonDifficulty::Frame m_frame;
    quint64 m_sourceEpoch{},m_revision{};
    int m_range{3},m_dragAxis{};
    bool m_currentOnly{},m_haveFrame{},m_queryPending{},m_queryAgain{},m_zoomed{},m_dragging{};
    bool m_presentedZoomed{};
    FrontierGraphView m_zoomView,m_dragView,m_presentedZoomView; QPointF m_dragAnchor;
};

// Thin mode adapter: existing result controls/data keep their own state. Only
// this common child intercepts input while Difficulty is selected. Consumers
// call position() after placing their own children so those cannot occlude it.
class HorizonDifficultyMode final : public QObject {
public:
    HorizonDifficultyMode(HorizonDifficulty::Coin coin,QWidget* host)
        :QObject(host),m_host(host),m_graph(new HorizonDifficultyGraph(coin,host)),
          m_toggle(new HorizonGraphToggleButton(QStringLiteral("DIFFICULTY"),host))
    {
        m_graph->hide(); m_toggle->setFixedSize(100,19);
        m_toggle->setObjectName(QStringLiteral("horizonDifficultyMode"));
        m_toggle->setToolTip(QStringLiteral("Network-difficulty history · independent of jobs and mining results. Click again to return to results."));
        connect(m_toggle,&QPushButton::toggled,this,[this](bool on) {
            m_toggle->setText(on?QStringLiteral("RESULTS ↩"):QStringLiteral("DIFFICULTY"));
            m_graph->setVisible(on); m_graph->raise(); m_toggle->raise(); m_host->update();
        });
    }
    HorizonDifficultyGraph* graph() const { return m_graph; }
    bool active() const { return m_toggle->isChecked(); }
    void position(const QRectF& area,qreal buttonOffset)
    {
        m_graph->setGeometry(area.toRect());
        m_toggle->move(qRound(area.left()+buttonOffset),qRound(area.top()+28));
        if(active())m_graph->raise();
        m_toggle->raise();
    }
private:
    QWidget* m_host;
    HorizonDifficultyGraph* m_graph;
    HorizonGraphToggleButton* m_toggle;
};
