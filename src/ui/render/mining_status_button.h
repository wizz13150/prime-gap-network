#pragma once
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QHelpEvent>
#include <QVariant>

// Ordered projection of the cards, never a second hardware inventory.
inline QVariant MiningCardState(const QString& id,const QString& name,bool active) {
    return QVariantMap{{"id",id},{"name",name},{"active",active}};
}
class MiningStatusButton final : public QPushButton {
public:
    MiningStatusButton(const QString& title,QWidget* parent=nullptr):QPushButton(title,parent) {
        setMouseTracking(true);
    }
    // The same source event that updates a card publishes its state projection.
    // Absence of an event is not an OFF event; no timer invents a transition.
    void present(const QVariantList& states){
        if(states==m_states)return;
        m_states=states;
        setProperty("miningCardStates",m_states);
        // Reserve a real layout region; labels and indicators never overpaint.
        const int reserve=16+12*m_states.size();
        setProperty("miningStatusReserve",reserve);
        setStyleSheet(QStringLiteral("padding-right:%1px;").arg(reserve));
        QStringList labels;
        for(const auto& state:m_states){const auto s=state.toMap();labels.append(s["name"].toString()+": "+(s["active"].toBool()?"MINING":"INACTIVE"));}
        setAccessibleDescription(labels.join("; "));setToolTip(labels.join('\n'));
        updateGeometry();update();
    }
protected:
    void paintEvent(QPaintEvent* event) override {
        QPushButton::paintEvent(event);
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        // Selection belongs to the primary workspace, not the mining state.
        // Clip the shared orange edge to the existing rounded button outline.
        if(isChecked()){
            QPainterPath outline;outline.addRoundedRect(QRectF(rect()).adjusted(.5,.5,-.5,-.5),9,9);
            p.save();p.setClipPath(outline);p.fillRect(QRectF(0,0,5,height()),QColor("#ffad4a"));p.restore();
        }
        for(int i=0;i<m_states.size();++i){
            const bool active=m_states[i].toMap()["active"].toBool();
            const auto center=point(i);const QColor color(active?"#69efc5":"#ef6b72");
            p.setPen(QPen(QColor(active?"#245e54":"#57323b"),1));p.setBrush(QColor("#02131a"));p.drawEllipse(center,5.,5.);
            p.setPen(Qt::NoPen);p.setBrush(color);p.drawEllipse(center,3.,3.);
        }
    }
    bool event(QEvent* event) override {
        if(event->type()==QEvent::ToolTip){
            auto* help=static_cast<QHelpEvent*>(event);
            for(int i=0;i<m_states.size();++i)if(QRectF(point(i)-QPointF(6,9),QSizeF(12,18)).contains(help->pos())){
                const auto s=m_states[i].toMap();
                QToolTip::showText(help->globalPos(),QStringLiteral("%1 · %2\n%3").arg(s["name"].toString(),s["active"].toBool()?"MINING":"INACTIVE",s["id"].toString()),this);return true;
            }
        }
        return QPushButton::event(event);
    }
private:
    QPointF point(int i)const{return QPointF(width()-14.-12.*(m_states.size()-1-i),height()/2.);}
    QVariantList m_states;
};
