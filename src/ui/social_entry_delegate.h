#pragma once
#include "social/social_bbcode.h"
#include <QStyledItemDelegate>
#include <QListWidget>
#include <QPainter>
#include <QAbstractTextDocumentLayout>
namespace pgn::ui {
enum SocialItemRole {AuthorRole=Qt::UserRole+1,TimeRole,BodyRole,TitleRole,CategoryRole,AuthorIdRole,RepliesRole,ActivityRole,StateRole,ContributorsRole,LastContributorRole,ActivityTimeRole};
inline QString SocialRelativeActivity(QDateTime time,QDateTime now=QDateTime::currentDateTimeUtc()){
    if(!time.isValid())return "unavailable";const auto seconds=qMax<qint64>(0,time.secsTo(now));
    if(seconds<60)return "now";if(seconds<3600)return QString("%1m ago").arg(seconds/60);if(seconds<86400)return QString("%1h ago").arg(seconds/3600);if(seconds<604800)return QString("%1d ago").arg(seconds/86400);return time.toUTC().toString("MMM d");
}
inline QString SocialInitials(const QString& name){const auto words=name.split(' ',Qt::SkipEmptyParts);if(words.isEmpty())return {};return (words.first().left(1)+(words.size()>1?words.last().left(1):QString{})).toUpper();}
class SocialEntryDelegate final:public QStyledItemDelegate{
    int kind_;
    void content(social::bbcode::Document& doc,const QStyleOptionViewItem& option,const QModelIndex& index)const{
        auto font=option.font;font.setPixelSize(14);doc.setDefaultFont(font);doc.setDocumentMargin(0);
        if(kind_==0){social::bbcode::render(doc,index.data(BodyRole).toString());return;}
        QTextCursor cursor(&doc);QTextCharFormat normal;normal.setFont(font);normal.setForeground(QColor("#161616"));
        auto title=normal;auto titleFont=font;titleFont.setPixelSize(kind_==1?18:16);titleFont.setWeight(QFont::DemiBold);title.setFont(titleFont);
        cursor.insertText(index.data(TitleRole).toString()+"\n",title);
        auto info=normal;auto small=font;small.setPixelSize(12);info.setFont(small);info.setForeground(QColor("#525252"));
        const QStringList parts{index.data(AuthorRole).toString(),index.data(TimeRole).toString(),index.data(CategoryRole).toString()};
        QStringList meta;for(const auto& p:parts)if(!p.isEmpty())meta<<p;cursor.insertText(meta.join(" · ")+"\n",info);
        if(kind_==1)cursor.insertText(index.data(BodyRole).toString().left(260),normal);
    }
public:
    SocialEntryDelegate(int kind,QObject* parent):QStyledItemDelegate(parent),kind_(kind){}
    QSize sizeHint(const QStyleOptionViewItem& option,const QModelIndex& index)const override{
        const auto* list=qobject_cast<const QListWidget*>(parent());const int width=list?list->viewport()->width():option.rect.width();
        if(kind_==2){auto font=option.font;font.setPixelSize(16);const QFontMetrics metrics(font);return {width,qMax(68,metrics.boundingRect(QRect(0,0,qMax(180,width-520),400),Qt::TextWordWrap,index.data(TitleRole).toString()).height()+38)};}
        social::bbcode::Document doc;content(doc,option,index);doc.setTextWidth(qMax(160,width-(kind_==0?84:48)));
        return {width,qMax(kind_==0?52:72,int(doc.size().height())+(kind_==0?30:28))};
    }
    void paint(QPainter* painter,const QStyleOptionViewItem& option,const QModelIndex& index)const override{
        painter->save();painter->fillRect(option.rect,option.state&QStyle::State_Selected?QColor("#c3d4d4"):option.state&QStyle::State_MouseOver?QColor("#d4dfdf"):QColor("#dfe7e7"));
        if(kind_==2){
            const int width=option.rect.width(),x=option.rect.left(),y=option.rect.top(),height=option.rect.height();const bool hover=option.state&QStyle::State_MouseOver,selected=option.state&QStyle::State_Selected;
            painter->fillRect(option.rect,QColor(selected?"#d3e2df":hover?"#edf3f1":index.row()%2?"#e6edeb":"#e1e9e7"));painter->setRenderHint(QPainter::Antialiasing);
            if(option.state&QStyle::State_HasFocus)painter->fillRect(QRect(x,y+9,3,height-18),QColor("#2d7379"));
            auto font=option.font;font.setPixelSize(16);font.setWeight(QFont::DemiBold);font.setUnderline(hover);painter->setFont(font);painter->setPen(QColor(hover?"#174f55":"#17363b"));
            painter->drawText(QRect(x+24,y+10,qMax(180,width-520),height-34),Qt::TextWordWrap,index.data(TitleRole).toString());
            font.setPixelSize(13);font.setWeight(QFont::Medium);font.setUnderline(false);painter->setFont(font);
            const QString category=index.data(CategoryRole).toString();const int badgeWidth=painter->fontMetrics().horizontalAdvance(category)+18;const QRect badge(x+24,option.rect.bottom()-27,badgeWidth,21);
            painter->setPen(Qt::NoPen);painter->setBrush(QColor("#bdd3d0"));painter->drawRoundedRect(badge,5,5);painter->setPen(QColor("#214f53"));painter->drawText(badge,Qt::AlignCenter,category);
            font.setPixelSize(14);font.setWeight(QFont::Normal);painter->setFont(font);painter->setPen(QColor("#3a5558"));painter->drawText(QRect(badge.right()+10,badge.top(),qMax(80,width-550-badgeWidth),21),Qt::AlignVCenter,painter->fontMetrics().elidedText("by "+index.data(AuthorRole).toString(),Qt::ElideRight,qMax(80,width-550-badgeWidth)));
            auto people=index.data(ContributorsRole).toStringList();if(people.isEmpty())people<<index.data(AuthorRole).toString();const QColor colors[]{QColor("#adc9c6"),QColor("#c6d9d5"),QColor("#98bbb9")};
            for(int i=qMin(3,int(people.size()))-1;i>=0;--i){const QRect avatar(x+width-480+i*23,y+(height-30)/2,30,30);painter->setPen(QPen(QColor("#e1e9e7"),2));painter->setBrush(colors[i]);painter->drawEllipse(avatar);font.setPixelSize(11);font.setWeight(QFont::DemiBold);painter->setFont(font);painter->setPen(QColor("#214f53"));painter->drawText(avatar,Qt::AlignCenter,SocialInitials(people[i]));}
            font.setPixelSize(15);font.setWeight(QFont::DemiBold);painter->setFont(font);painter->setPen(QColor("#21464b"));painter->drawText(QRect(x+width-340,y,80,height),Qt::AlignVCenter,index.data(RepliesRole).toString());
            font.setPixelSize(14);font.setWeight(QFont::Medium);painter->setFont(font);painter->drawText(QRect(x+width-252,y+12,134,22),Qt::AlignVCenter,SocialRelativeActivity(index.data(ActivityTimeRole).toDateTime()));
            font.setPixelSize(13);font.setWeight(QFont::Normal);painter->setFont(font);painter->setPen(QColor("#3a5558"));painter->drawText(QRect(x+width-252,y+34,134,20),Qt::AlignVCenter,painter->fontMetrics().elidedText(index.data(LastContributorRole).toString(),Qt::ElideRight,132));
            font.setPixelSize(12);font.setWeight(QFont::Medium);painter->setFont(font);const QRect state(x+width-112,y+(height-23)/2,78,23);painter->setPen(QPen(QColor("#9fbfbc"),1));painter->setBrush(QColor("#d5e2de"));painter->drawRoundedRect(state,6,6);painter->setPen(QColor("#28585b"));painter->drawText(state,Qt::AlignCenter,painter->fontMetrics().elidedText(index.data(StateRole).toString(),Qt::ElideRight,70));
            if(hover||selected){painter->setPen(Qt::NoPen);painter->setBrush(QColor("#315c61"));for(int dot=0;dot<3;++dot)painter->drawEllipse(QPointF(x+width-23+dot*4,y+height/2.),1.1,1.1);}
            painter->setPen(QColor("#cad8d4"));painter->drawLine(option.rect.bottomLeft(),option.rect.bottomRight());painter->restore();return;
        }
        const int left=option.rect.left()+(kind_==0?62:24),top=option.rect.top()+(kind_==0?8:12);social::bbcode::Document doc;content(doc,option,index);
        doc.setTextWidth(qMax(160,option.rect.width()-(kind_==0?84:48)));
        if(kind_==0){
            const auto author=index.data(AuthorRole).toString();const QRect avatar(option.rect.left()+16,top,32,32);
            painter->setRenderHint(QPainter::Antialiasing);painter->setPen(Qt::NoPen);painter->setBrush(QColor("#e0e0e0"));painter->drawEllipse(avatar);
            auto initialsFont=option.font;initialsFont.setPixelSize(12);initialsFont.setWeight(QFont::DemiBold);painter->setFont(initialsFont);painter->setPen(QColor("#525252"));QString initials;
            const auto words=author.split(' ',Qt::SkipEmptyParts);if(!words.isEmpty()){initials=words.first().left(1).toUpper();if(words.size()>1)initials+=words.last().left(1).toUpper();}painter->drawText(avatar,Qt::AlignCenter,initials);
            auto authorFont=option.font;authorFont.setPixelSize(14);authorFont.setWeight(QFont::DemiBold);painter->setFont(authorFont);painter->setPen(QColor("#161616"));
            const auto name=painter->fontMetrics().elidedText(author,Qt::ElideRight,qMax(80,option.rect.width()-270));painter->drawText(left,top+13,name);
            const int offset=painter->fontMetrics().horizontalAdvance(name)+10;auto small=option.font;small.setPixelSize(12);painter->setFont(small);painter->setPen(QColor("#525252"));
            painter->drawText(left+offset,top+13,index.data(TimeRole).toString());
        }
        painter->translate(left,top+(kind_==0?20:0));QAbstractTextDocumentLayout::PaintContext ctx;ctx.palette.setColor(QPalette::Text,QColor("#161616"));doc.documentLayout()->draw(painter,ctx);painter->restore();
        if(option.state&QStyle::State_HasFocus){painter->save();painter->fillRect(QRect(option.rect.left(),option.rect.top()+9,3,option.rect.height()-18),QColor("#2d7379"));painter->restore();}
    }
};
}
