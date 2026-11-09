#pragma once
#include "ui/merit_palette.h"
#include <QHash>
#include <QIcon>
#include <QPainter>
#include <QPolygonF>
#include <QStyledItemDelegate>

namespace pgn::ui {
class GapRosetteDelegate final:public QStyledItemDelegate {
public:
    explicit GapRosetteDelegate(QObject* parent):QStyledItemDelegate(parent){}
protected:
    void initStyleOption(QStyleOptionViewItem* option,const QModelIndex& index)const override{
        QStyledItemDelegate::initStyleOption(option,index);
        // Keep the same text rectangle on ordinary and decorated rows.
        // The view supplies one icon size for this complete column.
        option->features|=QStyleOptionViewItem::HasDecoration;
        option->decorationPosition=QStyleOptionViewItem::Right;
        option->decorationAlignment=Qt::AlignRight|Qt::AlignVCenter;
    }
};
// Discovery/record decoration only. Audit evidence and wallet crediting are
// deliberately supplied by their existing authoritative views, never by merit.
inline QString gapRosetteDescription(MeritTier tier,bool referenceImprovement){
    QString category;
    switch(tier){
    case MeritTier::NearMiss:category="Rust discovery";break;
    case MeritTier::Bronze:category="Bronze discovery";break;
    case MeritTier::Silver:category="Silver discovery";break;
    case MeritTier::Gold:category="Gold discovery";break;
    default:break;
    }
    if(referenceImprovement)category=category.isEmpty()?"Reference improvement":category+" · reference improvement";
    return category;
}
inline QIcon gapRosetteIcon(MeritTier tier,bool referenceImprovement){
    if(tier==MeritTier::Ordinary&&!referenceImprovement)return {};
    const int key=int(tier)*2+int(referenceImprovement);
    static QHash<int,QIcon> cache;
    if(const auto found=cache.constFind(key);found!=cache.cend())return *found;
    const QColor base=referenceImprovement?QColor("#e9c85c"):meritColor(tier);
    QIcon icon;
    for(const int scale:{1,2,3}){
        QPixmap pixmap(24*scale,28*scale);pixmap.setDevicePixelRatio(scale);pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);p.setRenderHint(QPainter::Antialiasing);p.setPen(Qt::NoPen);
        p.setBrush(referenceImprovement?QColor("#43ac87"):base.darker(125));
        p.drawPolygon(QPolygonF{{7,15},{4,26},{9,23},{12,26},{13,16}});
        p.setBrush(base.darker(110));p.drawPolygon(QPolygonF{{12,16},{13,26},{17,23},{21,26},{17,15}});
        const QPointF center(12,10.5);const double step=std::acos(-1.)/16;
        for(int fold=0;fold<32;++fold){
            const double a=fold*step,b=(fold+1)*step;
            const QPointF outer=center+QPointF(std::cos(a),std::sin(a))*(fold%2?8.2:10.0);
            const QPointF next=center+QPointF(std::cos(b),std::sin(b))*((fold+1)%2?8.2:10.0);
            p.setBrush(referenceImprovement&&fold%4<2?QColor("#43ac87"):fold%2?base.darker(112):base.lighter(113));
            p.drawPolygon(QPolygonF{center,outer,next});
        }
        p.setPen(QPen(base.lighter(135),.8));p.setBrush(QColor("#10272c"));p.drawEllipse(center,5.7,5.7);
        p.setPen(Qt::NoPen);p.setBrush(base);
        if(referenceImprovement){
            QPolygonF star;
            for(int i=0;i<10;++i){const double a=i*std::acos(-1.)/5-std::acos(-1.)/2;star<<center+QPointF(std::cos(a),std::sin(a))*(i%2?1.8:4.0);}
            p.drawPolygon(star);
        }else p.drawEllipse(center,2.3,2.3);
        p.end();icon.addPixmap(pixmap);
        // Selection highlights the row without recoloring its discovery tier.
        icon.addPixmap(pixmap,QIcon::Selected);icon.addPixmap(pixmap,QIcon::Active);
    }
    cache.insert(key,icon);return icon;
}
}
