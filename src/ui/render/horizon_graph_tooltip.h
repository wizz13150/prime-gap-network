#pragma once
#include "tooltip_layout.h"
#include <QWidget>
#include <QPainter>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>

// The Gapcoin document, colors and placement are the common graph contract.
namespace HorizonGraphTooltip {
inline QString escaped(const QString& text) {
    QString result;
    for(int i=0;i<text.size();i+=30) {
        if(i)result+=QStringLiteral("&#8203;");
        result+=text.mid(i,30).toHtmlEscaped();
    }
    return result;
}
inline QString row(const QString& label,const QString& value) {
    return QStringLiteral("<tr><td class='label'>%1</td><td class='value'>%2</td></tr>")
        .arg(label.toHtmlEscaped(),escaped(value));
}
inline void draw(QPainter& painter,const QRectF& bounds,const QPointF& cursor,
                 const QPointF& target,const QColor& color,const QString& html) {
    if(html.isEmpty()||bounds.width()<50||bounds.height()<40)return;
    QTextDocument document;document.setDocumentMargin(0);
    document.setDefaultStyleSheet(QStringLiteral(
        "div,td{font-family:'Cascadia Mono';font-size:11px;color:#cfe8e4;}"
        ".headline{font-size:12px;font-weight:700;color:%1;margin-bottom:2px;}"
        ".device{color:#8fcbc1;margin-bottom:5px;}"
        "table{width:100%%;border-collapse:collapse;}"
        "td{padding-top:1px;padding-bottom:1px;vertical-align:top;}"
        ".label{width:112px;color:#69efc5;font-weight:700;padding-right:8px;}"
        ".value{color:#dff4f0;}").arg(color.name(QColor::HexRgb)));
    document.setHtml(html);
    const qreal width=std::min<qreal>(470,bounds.width());document.setTextWidth(width-24);
    const qreal height=std::min<qreal>(std::ceil(document.size().height())+18,bounds.height());
    const auto placement=FrontierPlaceFixedTooltip(bounds,cursor,target,QSizeF(width,height));
    const auto box=placement.rect;painter.save();painter.setClipping(false);
    QColor border=color;border.setAlpha(180);painter.setPen(QPen(border,1.2));
    painter.drawLine(target,placement.leaderAnchor);
    painter.setBrush(QColor(std::max(1,color.red()/14),std::max(7,color.green()/14),std::max(12,color.blue()/14),248));
    painter.drawRoundedRect(box,10,10);
    painter.setClipRect(box.adjusted(10,8,-10,-8));painter.translate(box.left()+12,box.top()+9);
    QAbstractTextDocumentLayout::PaintContext context;document.documentLayout()->draw(&painter,context);
    painter.restore();
}
}

// Transparent child overlay also works over nested constellation canvases.
class HorizonGraphBubble final : public QWidget {
public:
    explicit HorizonGraphBubble(QWidget* host):QWidget(host) {
        setAttribute(Qt::WA_TransparentForMouseEvents);setAttribute(Qt::WA_NoSystemBackground);hide();
    }
    void present(QWidget* source,QPointF cursor,QPointF target,QString html,QColor accent=QColor("#69efc5")) {
        auto* host=parentWidget();setGeometry(host->rect());
        const auto offset=source->mapTo(host,QPoint());m_cursor=cursor+offset;m_target=target+offset;
        m_html=std::move(html);m_accent=accent;show();raise();update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        HorizonGraphTooltip::draw(p,QRectF(rect()).adjusted(8,8,-8,-8),m_cursor,m_target,m_accent,m_html);
    }
private:
    QPointF m_cursor,m_target;QString m_html;QColor m_accent;
};
