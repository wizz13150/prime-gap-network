#pragma once

#include "horizon_device_visuals.h"
#include "graph_view.h"
#include <QPainter>
#include <QPushButton>
#include <QMouseEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <functional>

// The original Gapcoin graph controls, shared by every coin workspace.
namespace HorizonGraphControls {
// Shared Gapcoin-sized plot; extra height belongs to the central work area.
inline qreal miningHeight(qreal sceneHeight) {
    return std::clamp(sceneHeight*.41,qreal(250),qreal(300));
}
inline QRectF miningPanel(qreal width,qreal height,qreal minimumTop) {
    const qreal w=std::min(std::max(qreal(1),width-16),std::clamp((width-56)*.91,qreal(900),qreal(1200)));
    const qreal h=std::min(miningHeight(height-108),std::max(qreal(1),height-minimumTop-8));
    return QRectF((width-w)/2,height-h-8,w,h);
}
inline QRectF miningRail(qreal width,qreal top,const QRectF& graph) {
    return QRectF(68,top,std::max(qreal(1),width-136),std::max(qreal(62),graph.top()-top-12));
}
inline qreal railLineY(const QRectF& rail,int index) {
    return rail.height()<=80 ? rail.top()+(index?48:27)
                            : rail.top()+rail.height()*(index?.70:.36);
}
inline void drawToggle(QPainter& painter, const QRectF& area,
                       const QString& label, bool selected)
{
    painter.setBrush(selected ? QColor(31, 103, 89, 225) : QColor(3, 20, 29, 240));
    painter.setPen(QPen(selected ? QColor(QStringLiteral("#69efc5"))
                                 : QColor(QStringLiteral("#42666d")), 1));
    painter.drawRoundedRect(area, 10, 10);
    painter.setFont(HorizonDeviceVisuals::font(7, QFont::DemiBold, true));
    painter.setPen(selected ? QColor(QStringLiteral("#f1fffc"))
                            : QColor(QStringLiteral("#91aaa8")));
    painter.drawText(area, Qt::AlignCenter, label);
}
inline qreal actionY(const QRectF& graph) { return graph.top()+(graph.width()<900?28:6); }
inline QRectF current(const QRectF& graph) { return {graph.right()-226, actionY(graph), 104, 21}; }
inline QRectF history(const QRectF& graph) { return {graph.right()-116, actionY(graph), 104, 21}; }
inline QRectF reset(const QRectF& graph) { return {graph.right()-306,actionY(graph),72,21}; }
inline QRectF axis(const QRectF& graph,int index) { return {graph.left()+16+index*60,graph.top()+28,56,19}; }
inline void drawReset(QPainter& painter,const QRectF& area,bool zoomed,const QString& label=QStringLiteral("RESET ZOOM")) {
    painter.setBrush(zoomed?QColor(23,53,63,235):QColor(7,20,27,220));
    painter.setPen(QPen(zoomed?QColor("#6fbfd0"):QColor("#3d5960"),1));
    painter.drawRoundedRect(area,9,9);
    painter.setFont(HorizonDeviceVisuals::font(7,QFont::DemiBold,true));
    painter.setPen(zoomed?QColor("#d9f6f7"):QColor("#71888a"));
    painter.drawText(area,Qt::AlignCenter,label);
}
// Exact Golden (--legacy-r4) geometry, formerly private in main.cpp. Every
// consumer uses the same nine presets, hit rectangles and painter below.
inline QRectF timeRangePanel(const QRectF& graph) { return graph.width()<900?QRectF(graph.left()+16,graph.top()+52,227,41):QRectF(graph.right()-560,graph.top()+6,227,41); }
inline QRectF timeRangeButton(int index) { return {qreal((index%5)*46),qreal((index/5)*22),43,19}; }
inline QRectF timeRangeButton(const QRectF& graph,int index) { return timeRangeButton(index).translated(timeRangePanel(graph).topLeft()); }
// Dedicated second-row slots: neither caption may paint through a preset,
// axis toggle or the independent third-row CURRENT/HISTORY counts.
inline QRectF frameCaption(const QRectF& graph) {
    if(graph.width()<900)return {graph.left()+252,graph.top()+52,std::max(1.,graph.width()-264),19};
    const qreal left=timeRangeButton(graph,int(FrontierTimeRangePresetCount)-1).right()+12;
    return {left,graph.top()+28,std::max(qreal(1),graph.right()-12-left),19};
}
inline QRectF importedFrameCaption(const QRectF& graph) {
    if(graph.width()<900)return {graph.left()+252,graph.top()+73,std::max(1.,graph.width()-264),19};
    const qreal left=axis(graph,1).right()+12;
    return {left,graph.top()+28,std::max(qreal(1),timeRangePanel(graph).left()-12-left),19};
}
inline QString timeRangeTooltip(int index) {
    const auto& preset=FrontierTimeRangePresets[index];
    return QStringLiteral("Time range: %1 (%2). Viewport only; CURRENT/FULL HISTORY remains a separate dataset selector.")
        .arg(QString::fromLatin1(preset.label),QString::fromLatin1(preset.tooltip));
}
inline void drawTimeRange(QPainter& painter,const QRectF& rect,int index,bool selected) {
    drawToggle(painter,rect,QString::fromLatin1(FrontierTimeRangePresets[index].label),selected);
}
}

class HorizonTimeRangeSelector final : public QWidget {
public:
    explicit HorizonTimeRangeSelector(QWidget* parent=nullptr):QWidget(parent) {
        setFixedSize(227,41);setCursor(Qt::PointingHandCursor);
    }
    void setSelected(int index){if(m_selected==index)return;m_selected=index;update();}
    int selected() const{return m_selected;}
    void setSelectionHandler(std::function<void(int)> callback){m_select=std::move(callback);}
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        for(int i=0;i<int(FrontierTimeRangePresetCount);++i)
            HorizonGraphControls::drawTimeRange(painter,HorizonGraphControls::timeRangeButton(i),i,m_selected==i);
    }
    void mousePressEvent(QMouseEvent* event) override {
        if(event->button()==Qt::LeftButton)for(int i=0;i<int(FrontierTimeRangePresetCount);++i)
            if(HorizonGraphControls::timeRangeButton(i).contains(event->position())) {
                if(m_select)m_select(i);event->accept();return;
            }
        QWidget::mousePressEvent(event);
    }
    bool event(QEvent* event) override {
        if(event->type()==QEvent::ToolTip) {
            auto* help=static_cast<QHelpEvent*>(event);
            for(int i=0;i<int(FrontierTimeRangePresetCount);++i)
                if(HorizonGraphControls::timeRangeButton(i).contains(help->pos())) {
                    QToolTip::showText(help->globalPos(),HorizonGraphControls::timeRangeTooltip(i),this);return true;
                }
        }
        return QWidget::event(event);
    }
private:
    int m_selected{-1};std::function<void(int)> m_select;
};

class HorizonGraphToggleButton : public QPushButton {
public:
    explicit HorizonGraphToggleButton(const QString& text, QWidget* parent = nullptr)
        : QPushButton(text, parent) {
        setCheckable(true); setCursor(Qt::PointingHandCursor);
        setStyleSheet(QStringLiteral("QPushButton{min-height:0;min-width:0;padding:0;margin:0;border:0;background:transparent;}"));
        // Apply stylesheet constraints before callers set their exact shared
        // dimensions. Deferred polishing otherwise resets those minimums and
        // a layout shrinks TIME/SEQUENCE controls to their tiny size hints.
        ensurePolished();
    }
    void setFixedSize(int width, int height) { setFixedSize(QSize(width,height)); }
    void setFixedSize(const QSize& size) { m_exactSize=size;QPushButton::setFixedSize(size); }
protected:
    bool event(QEvent* event) override {
        const bool result=QPushButton::event(event);
        // Moving the graph into Mining repolishes its subtree. QSS may reset
        // minimum dimensions even after the first polish; retain the caller's
        // exact shared control contract across that ownership change.
        if(m_exactSize.isValid() && (event->type()==QEvent::StyleChange ||
            event->type()==QEvent::Polish || event->type()==QEvent::PolishRequest))
            QPushButton::setFixedSize(m_exactSize);
        return result;
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        HorizonGraphControls::drawToggle(painter, QRectF(rect()).adjusted(.5,.5,-.5,-.5), text(), isChecked());
        if (hasFocus()) { painter.setPen(QPen(QColor("#f1fffc"), 1, Qt::DotLine));
            painter.setBrush(Qt::NoBrush); painter.drawRoundedRect(rect().adjusted(3,3,-3,-3),7,7); }
    }
private:
    QSize m_exactSize;
};

class HorizonGraphResetButton final : public HorizonGraphToggleButton {
public:
    explicit HorizonGraphResetButton(const QString& label,int width,QWidget* parent=nullptr)
        :HorizonGraphToggleButton(label,parent) {setCheckable(false);setFixedSize(width,21);}
    explicit HorizonGraphResetButton(QWidget* parent=nullptr)
        :HorizonGraphToggleButton(QStringLiteral("RESET ZOOM"),parent) {
        setCheckable(false);setFixedSize(72,21);
    }
    void setZoomed(bool value){if(m_zoomed==value)return;m_zoomed=value;update();}
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        HorizonGraphControls::drawReset(painter,QRectF(rect()).adjusted(.5,.5,-.5,-.5),m_zoomed,text());
    }
private:
    bool m_zoomed{};
};
