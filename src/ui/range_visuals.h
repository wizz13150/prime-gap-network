#pragma once
#include <QPainter>
#include <QLinearGradient>
#include <optional>
#include <algorithm>

namespace pgn::ui {
// Mining rail and community atlas share this exact range-box primitive.
// Progress is an original observation, never interpolated or averaged.
inline void drawRangeTile(QPainter& painter,const QRectF& box,const QColor& accent,
                          const QString& title,const QString& detail={},
                          std::optional<double> progress={},bool selected=false){
    if(box.width()<1||box.height()<1)return;
    painter.save();painter.setClipRect(box,Qt::IntersectClip);QLinearGradient fill(box.topLeft(),box.bottomLeft());
    fill.setColorAt(0,QColor(9,42,48));fill.setColorAt(1,QColor(3,21,29));
    painter.setBrush(fill);painter.setPen(QPen(accent,selected?2:1));
    painter.drawRoundedRect(box.adjusted(.5,.5,-.5,-.5),4,4);
    if(progress){const auto amount=std::clamp(*progress,0.,1.);
        const QRectF track(box.left()+2,box.bottom()-4,std::max(0.,box.width()-4),2);
        painter.fillRect(track,QColor(23,57,65));painter.fillRect(QRectF(track.topLeft(),QSizeF(track.width()*amount,track.height())),accent);}
    if(box.width()>72){
        painter.setPen(QColor("#dbf8f2"));QFont font("Segoe UI");font.setPixelSize(box.height()<24?9:box.height()<30?10:12);font.setWeight(QFont::DemiBold);painter.setFont(font);
        const auto text=box.adjusted(9,4,-9,progress?-7:-4);
        if(box.height()>=52&&!detail.isEmpty()){
            painter.drawText(QRectF(text.left(),text.top(),text.width(),18),Qt::AlignLeft|Qt::AlignVCenter,painter.fontMetrics().elidedText(title,Qt::ElideRight,int(text.width())));
            font.setPointSize(8);font.setWeight(QFont::Normal);painter.setFont(font);painter.setPen(QColor("#91c4cc"));
            painter.drawText(QRectF(text.left(),text.top()+21,text.width(),text.height()-21),Qt::AlignLeft|Qt::AlignTop,painter.fontMetrics().elidedText(detail,Qt::ElideRight,int(text.width())));
        }else painter.drawText(text,Qt::AlignCenter,painter.fontMetrics().elidedText(title,Qt::ElideRight,int(text.width())));
    }
    painter.restore();
}
inline QString compactRangeNumber(const QString& value){
    if(value.size()<8)return value;
    return value.left(1)+"."+value.mid(1,7)+"e"+QString::number(value.size()-1);
}
// An exact sub-interval in a shared tile, not whole-cell reservation/coverage.
// Only its observed cursor fills the lower track; no tween or interpolation.
inline void drawRangeSpan(QPainter& painter,const QRectF& box,const QColor& accent,
                          double begin,double end,std::optional<double> progressEnd={}){
    begin=std::clamp(begin,0.,1.);end=std::clamp(end,begin,1.);
    if(end<=begin||box.width()<4||box.height()<8)return;
    const double width=box.width()-4;
    const QRectF span(box.left()+2+width*begin,box.top()+3,width*(end-begin),2);
    // Occupancy is a real exact interval, not merely a hairline border. This
    // shared fill distinguishes processed/allocated cases in both calendars.
    painter.fillRect(QRectF(span.left(),box.top()+3,span.width(),std::max(0.,box.height()-8)),QColor(accent.red(),accent.green(),accent.blue(),55));
    painter.fillRect(span,accent);
    const QRectF track(span.left(),box.bottom()-5,span.width(),3);
    painter.fillRect(track,QColor(23,57,65));
    if(progressEnd){const double cursor=std::clamp(*progressEnd,begin,end);
        painter.fillRect(QRectF(track.topLeft(),QSizeF(width*(cursor-begin),track.height())),accent);}
}
}
