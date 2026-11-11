#include "ui/social_forum_thread.h"
#include "social/social_bbcode.h"
#include "ui/social_entry_delegate.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QPainter>
#include <QScrollBar>
#include <QApplication>
#include <QTimer>
#include <QMenu>
#include <algorithm>
#include <memory>
namespace pgn::ui {
namespace {
QLabel* text(QString value){auto* label=new QLabel(value);label->setTextFormat(Qt::PlainText);return label;}
class Post final:public QWidget{
    QTextBrowser* body_;QListWidgetItem* item_;
public:
    Post(ForumThread* thread,QListWidgetItem* item,QString id,QString author,QString body,QDateTime date,
        std::optional<QVector<social::Reaction>> reactions,bool bbcode,bool original):QWidget(thread),item_(item){
        setObjectName("pgn-social-forum-post-"+id);setProperty("forumPostId",id);
        setStyleSheet("QWidget{background:#09262d;color:#c9eee7;font:14px 'Segoe UI';}QLabel{background:transparent;}QTextBrowser{background:#e4ecea;color:#161616;border:0;border-radius:6px;font:15px 'Segoe UI';}QPushButton{background:#09262d;color:#c9eee7;border:1px solid #547b82;border-radius:5px;min-height:22px;padding:3px 10px;font:13px 'Segoe UI';}QPushButton:hover{background:#16434a;border-color:#69b4b3;}QPushButton:checked{background:#16434a;}QPushButton:focus{border:2px solid #69efc5;}QPushButton:disabled{background:#0b2027;color:#91aaad;border-color:#27444c;}");
        auto* box=new QVBoxLayout(this);box->setContentsMargins(12,10,12,10);box->setSpacing(8);auto* header=new QHBoxLayout;
        auto* avatar=text({});avatar->setFixedSize(32,32);QPixmap icon(32,32);icon.fill(Qt::transparent);QPainter painter(&icon);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(Qt::NoPen);painter.setBrush(QColor("#c3d4d4"));painter.drawEllipse(icon.rect());painter.setPen(QColor("#161616"));const auto words=author.split(' ',Qt::SkipEmptyParts);QString initials;if(!words.isEmpty()){initials=words.first().left(1);if(words.size()>1)initials+=words.last().left(1);}painter.drawText(icon.rect(),Qt::AlignCenter,initials.toUpper());avatar->setPixmap(icon);header->addWidget(avatar);
        auto* name=text(author);name->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);name->setToolTip("<qt>"+author.toHtmlEscaped()+"</qt>");name->setStyleSheet("font:600 15px 'Segoe UI';color:#e8fffb;");header->addWidget(name,1);auto* time=text(SocialRelativeActivity(date));time->setToolTip(date.toUTC().toString("yyyy-MM-dd HH:mm 'UTC'"));time->setStyleSheet("font:13px 'Segoe UI';color:#9db6b9;");header->addWidget(time);header->addStretch();auto* kind=text(original?"Topic":"Reply");kind->setStyleSheet("font:13px 'Segoe UI';color:#9db6b9;");header->addWidget(kind);box->addLayout(header);
        body_=new QTextBrowser;body_->setObjectName("pgn-social-forum-body-"+id);body_->setDocument(new social::bbcode::Document(body_));auto font=body_->font();font.setPixelSize(15);body_->document()->setDefaultFont(font);body_->document()->setDocumentMargin(14);body_->setOpenLinks(false);body_->setOpenExternalLinks(false);
        if(bbcode)social::bbcode::render(*body_->document(),body,original?16000:4000);else body_->setPlainText(body);box->addWidget(body_);
        auto* actions=new QHBoxLayout;auto* quote=new QPushButton("Quote");quote->setObjectName("pgn-social-forum-quote-"+id);quote->setEnabled(thread->canDraft);actions->addWidget(quote);
        QObject::connect(quote,&QPushButton::clicked,thread,[this,thread,id,body]{auto selected=body_->textCursor().selectedText().replace(QChar(0x2029),'\n');if(thread->onQuote)thread->onQuote(id,selected.isEmpty()?body:selected);});
        auto* reply=new QPushButton("Reply");reply->setObjectName("pgn-social-forum-reply-"+id);reply->setEnabled(thread->canDraft);actions->addWidget(reply);QObject::connect(reply,&QPushButton::clicked,thread,[thread,id]{if(thread->onReply)thread->onReply(id);});
        if(reactions){int shown=0;QMenu* overflow=nullptr;for(const auto& reaction:*reactions){const QString full=reaction.label+" · "+reaction.count.value_or("unavailable");const auto why=thread->reactionUnavailable?thread->reactionUnavailable(id,reaction.id):"No authenticated reaction service connected.";
            if(shown++<2){auto* button=new QPushButton(fontMetrics().elidedText(full,Qt::ElideRight,140));button->setMaximumWidth(164);button->setObjectName("pgn-social-forum-reaction-"+id+"-"+reaction.id);button->setCheckable(true);button->setChecked(reaction.selected);button->setEnabled(why.isEmpty());button->setToolTip("<qt>"+(full+"\n"+why).toHtmlEscaped()+"</qt>");actions->addWidget(button);QObject::connect(button,&QPushButton::clicked,thread,[thread,id,key=reaction.id,button,selected=reaction.selected]{button->setChecked(selected);if(thread->onReact)thread->onReact(id,key);});}
            else{if(!overflow){overflow=new QMenu(this);auto* more=new QPushButton("More reactions");more->setMenu(overflow);actions->addWidget(more);}auto* action=overflow->addAction(fontMetrics().elidedText(full,Qt::ElideRight,240));action->setEnabled(why.isEmpty());action->setToolTip("<qt>"+(full+"\n"+why).toHtmlEscaped()+"</qt>");QObject::connect(action,&QAction::triggered,thread,[thread,id,key=reaction.id]{if(thread->onReact)thread->onReact(id,key);});}
        }}
        else{auto* missing=text("Reactions unavailable");missing->setStyleSheet("font:12px 'Segoe UI';color:#9db6b9;");actions->addWidget(missing);}
        actions->addStretch();auto* menu=new QPushButton("Actions");menu->setObjectName("pgn-social-forum-actions-"+id);menu->setAccessibleName("Actions for this post. Shift+F10 also opens post actions.");actions->addWidget(menu);
        QObject::connect(menu,&QPushButton::clicked,thread,[thread,menu,id]{if(thread->onMenu)thread->onMenu(id,thread->viewport()->mapFromGlobal(menu->mapToGlobal(QPoint(0,menu->height()))));});box->addLayout(actions);
        body_->setContextMenuPolicy(Qt::CustomContextMenu);QObject::connect(body_,&QTextBrowser::customContextMenuRequested,thread,[thread,id,this](QPoint pos){if(thread->onMenu)thread->onMenu(id,thread->viewport()->mapFromGlobal(body_->viewport()->mapToGlobal(pos)));});
    }
    void reflow(int width){
        setFixedWidth(width);
        // QTextEdit also changes its document width during resize. Measure an
        // independent formatted copy so that viewport/layout feedback cannot
        // turn a short post into a maximum-height reader.
        std::unique_ptr<QTextDocument> measure(body_->document()->clone());measure->setTextWidth(qMax(160,width-56));
        const int height=qBound(56,int(measure->size().height())+8,640);body_->setFixedHeight(height);layout()->activate();item_->setSizeHint({width,sizeHint().height()+12});
    }
};
}
ForumThread::ForumThread(QWidget* parent):QListWidget(parent){
    setObjectName("pgn-social-forum-thread");setAccessibleName("Forum topic posts in chronological order");setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);setSpacing(10);setSelectionMode(QAbstractItemView::SingleSelection);
    setStyleSheet("QListWidget#pgn-social-forum-thread,QListWidget#pgn-social-forum-thread QWidget#qt_scrollarea_viewport{background:#041319;border:0;}");
}
QString ForumThread::selectedPost()const{return currentItem()?currentItem()->data(Qt::UserRole).toString():QString{};}
void ForumThread::setTopic(const social::Entry& topic,bool preserve){
    const int scroll=preserve?verticalScrollBar()->value():0;const auto selected=selectedPost();const bool owned=QApplication::focusWidget()&&isAncestorOf(QApplication::focusWidget());const auto focused=owned?QApplication::focusWidget()->objectName():QString{};clear();
    auto add=[&](QString id,QString author,QString body,QDateTime date,std::optional<QVector<social::Reaction>> reactions,bool bbcode,bool original){
        auto* item=new QListWidgetItem(author+" · "+date.toUTC().toString(Qt::ISODate)+"\n"+body,this);item->setData(Qt::UserRole,id);auto* post=new Post(this,item,id,author,body,date,reactions,bbcode,original);setItemWidget(item,post);post->reflow(qMax(240,viewport()->width()-24));if(id==selected)setCurrentItem(item);};
    add(topic.id,topic.author,topic.body,topic.createdUtc,topic.reactions,topic.bbcode,true);auto replies=topic.replies;std::stable_sort(replies.begin(),replies.end(),[](const auto& a,const auto& b){return a.createdUtc==b.createdUtc?a.id<b.id:a.createdUtc<b.createdUtc;});
    for(const auto& reply:replies)add(reply.id,reply.author,reply.body,reply.createdUtc,reply.reactions,reply.bbcode,false);
    verticalScrollBar()->setValue(scroll);
    // Restore only focus already inside this thread; never take focus from a composer.
    if(owned)if(auto* widget=findChild<QWidget*>(focused))widget->setFocus();
}
void ForumThread::reflow(){for(int i=0;i<count();++i)if(auto* post=dynamic_cast<Post*>(itemWidget(item(i))))post->reflow(qMax(240,viewport()->width()-24));}
void ForumThread::resizeEvent(QResizeEvent* event){QListWidget::resizeEvent(event);reflow();}
}
