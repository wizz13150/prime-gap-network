#pragma once
#include <QListWidget>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QShowEvent>
#include <QTimer>
#include <QWheelEvent>
#include <functional>

namespace pgn::ui {
// Qt lays variable-height delegates out again after insertion/resize. Preserve
// the visible message and its pixel offset, rather than a stale scrollbar range.
// https://doc.qt.io/qt-6/qabstractitemview.html#ScrollMode-enum
// https://doc.qt.io/qt-6/qlistview.html#resizeMode-prop
class SocialChatList final:public QListWidget {
    struct Bookmark {QString id;int top{};bool bottom{true};};
    Bookmark desired_;bool updating_{},applying_{},pending_{},followNext_{},unseen_{};quint64 revision_{};
    Bookmark capture()const{
        Bookmark result;result.bottom=verticalScrollBar()->maximum()-verticalScrollBar()->value()<=2;
        // Prefer a visible message start: preserving a negative offset larger
        // than the reflowed row would move that message wholly out of view.
        for(int row=0;row<count();++row){auto* entry=item(row);if(entry->isHidden())continue;const auto rect=visualItemRect(entry);if(rect.bottom()<0)continue;if(result.id.isEmpty()||rect.top()>=0){result.id=entry->data(Qt::UserRole).toString();result.top=rect.top();}if(rect.top()>=0)break;}
        return result;
    }
    void unseen(bool value){if(unseen_==value)return;unseen_=value;if(unseenChanged)unseenChanged(value);}
    void remember(){if(updating_||applying_||pending_)return;desired_=capture();if(desired_.bottom)unseen(false);}
    void apply(){
        QScopedValueRollback<bool> guard(applying_,true);doItemsLayout();
        if(desired_.bottom){scrollToBottom();return;}
        for(int row=0;row<count();++row){auto* entry=item(row);if(!entry->isHidden()&&entry->data(Qt::UserRole).toString()==desired_.id){
            scrollToItem(entry,QAbstractItemView::PositionAtTop);
            verticalScrollBar()->setValue(verticalScrollBar()->value()+visualItemRect(entry).top()-desired_.top);return;
        }}
        // Retention can remove an anchor. Do not pretend it is still available.
        scrollToTop();setProperty("pgn_chat_anchor_unavailable",!desired_.id.isEmpty());
    }
    void settle(){
        pending_=true;const auto token=++revision_;
        QTimer::singleShot(0,this,[this,token]{if(token!=revision_||!isVisible())return;apply();
            // Let viewport/scrollbar geometry changes finish before accepting a
            // position as stable. A newer update/channel invalidates both jobs.
            QTimer::singleShot(0,this,[this,token]{if(token!=revision_||!isVisible())return;apply();pending_=false;desired_=capture();if(desired_.bottom)unseen(false);});
        });
    }
protected:
    void resizeEvent(QResizeEvent* event)override{QListWidget::resizeEvent(event);settle();}
    void showEvent(QShowEvent* event)override{QListWidget::showEvent(event);settle();}
    void wheelEvent(QWheelEvent* event)override{++revision_;pending_=false;QListWidget::wheelEvent(event);remember();}
    void keyPressEvent(QKeyEvent* event)override{++revision_;pending_=false;QListWidget::keyPressEvent(event);remember();}
public:
    std::function<void(bool)> unseenChanged;
    explicit SocialChatList(QWidget* parent=nullptr):QListWidget(parent){
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setResizeMode(QListView::Adjust);setLayoutMode(QListView::SinglePass);setUniformItemSizes(false);
        connect(verticalScrollBar(),&QScrollBar::valueChanged,this,[this]{remember();});
        connect(verticalScrollBar(),&QScrollBar::rangeChanged,this,[this]{if(!updating_&&!applying_)settle();});
        connect(verticalScrollBar(),&QScrollBar::sliderPressed,this,[this]{++revision_;pending_=false;});
        connect(verticalScrollBar(),&QScrollBar::actionTriggered,this,[this]{if(!applying_){++revision_;pending_=false;}});
    }
    void beginUpdate(){if(!pending_)desired_=capture();++revision_;updating_=true;pending_=true;}
    void endUpdate(){updating_=false;if(followNext_){desired_.bottom=true;followNext_=false;unseen(false);}settle();}
    void newMessages(bool own){if(own)followNext_=true;else if(!desired_.bottom)unseen(true);}
    void followPublished(){followNext_=true;}
    bool followingLatest()const{return desired_.bottom||followNext_;}
    void jumpToLatest(){desired_.bottom=true;followNext_=false;unseen(false);settle();}
    void resetConversation(){++revision_;pending_=false;updating_=false;followNext_=false;desired_={};unseen(false);setProperty("pgn_chat_anchor_unavailable",false);}
};
}
