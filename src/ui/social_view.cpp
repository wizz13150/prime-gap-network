#include "ui/social_view.h"
#include "social/social_numbers.h"
#include "ui/render/horizon_coin_navigation.h"
#include "ui/render/horizon_workspace_style.h"
#include "ui/social_light_style.h"
#include "ui/social_entry_delegate.h"
#include "ui/social_access_dialog.h"
#include "ui/social_controls.h"
#include "ui/social_presentation.h"
#include <QInputMethodEvent>
#include "ui/social_forum_thread.h"
#include <QFrame>
#include <QMenu>
#include <QClipboard>
#include <QApplication>
#include <QMouseEvent>
#include <QUuid>
#include <memory>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyleOptionFocusRect>
#include <QTableWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocumentFragment>
#include <algorithm>
namespace pgn::ui {
namespace {
const char* names[]={"Chat","Publications","Forum","Miners"};
const char* nouns[]={"messages","publications","topics","miners"};
constexpr int LoadedLimit=500;
constexpr int DocumentTargetProperty=QTextFormat::UserProperty+31,DocumentAuthorProperty=QTextFormat::UserProperty+32;
QLabel* text(const QString& value,QWidget* parent=nullptr){
    auto* p=new QLabel(value,parent);p->setTextFormat(Qt::PlainText);p->setWordWrap(false);return p;
}
QVBoxLayout* vertical(QWidget* widget,int margin=0){
    auto* layout=new QVBoxLayout(widget);layout->setContentsMargins(margin,margin,margin,margin);layout->setSpacing(6);return layout;
}
QString stamp(const QDateTime& value){return value.toUTC().toString("yyyy-MM-dd HH:mm 'UTC'");}
QString safeTip(const QString& value){return "<qt>"+value.left(1000).toHtmlEscaped().replace("\n","<br/>")+"</qt>";}
void settleInitialChat(QListWidget* list){
    QTimer::singleShot(0,list,[list]{if(!list->isVisible()||!list->property("pgn_initial_scroll").toBool())return;
        list->doItemsLayout();list->scrollToBottom();list->setProperty("pgn_initial_scroll",false);});
}
QString metadata(const social::Entry& entry){
    QStringList result;if(!entry.author.isEmpty())result<<entry.author;
    if(entry.createdUtc.isValid())result<<stamp(entry.createdUtc);
    if(!entry.category.isEmpty())result<<entry.category;return result.join(" · ");
}
void document(QTextBrowser* browser,const social::Entry& entry,bool local=false){
    const auto scroll=browser->verticalScrollBar()->value();browser->clear();auto cursor=browser->textCursor();
    const auto base=browser->font();QTextCharFormat normal;normal.setFont(base);normal.setForeground(QColor("#161616"));normal.setProperty(DocumentTargetProperty,entry.id);normal.setProperty(DocumentAuthorProperty,entry.authorId);
    if(!entry.title.isEmpty()){auto title=normal;auto titleFont=base;titleFont.setPixelSize(20);titleFont.setWeight(QFont::DemiBold);title.setFont(titleFont);
        cursor.insertText(entry.title+"\n",title);}
    auto info=normal;info.setForeground(QColor("#525252"));
    cursor.insertText(local?"Local draft · not published\n":metadata(entry)+"\n",info);
    if(!entry.reference.isEmpty())cursor.insertText("Ticket / Gold: "+entry.reference+"\n",info);
    cursor.insertText("\n",normal);if(entry.bbcode){social::bbcode::Document body;body.setDefaultFont(base);social::bbcode::render(body,entry.body,16000);cursor.insertFragment(QTextDocumentFragment(&body));}else cursor.insertText(entry.body,normal);cursor.insertText("\n",normal);
    for(const auto& reply:entry.replies){auto replyFormat=normal;replyFormat.setProperty(DocumentTargetProperty,reply.id);replyFormat.setProperty(DocumentAuthorProperty,reply.authorId);auto author=replyFormat;author.setFontWeight(QFont::DemiBold);
        cursor.insertText("\n"+reply.author+" · "+stamp(reply.createdUtc)+"\n",author);cursor.insertText(reply.body+"\n",replyFormat);}
    browser->verticalScrollBar()->setValue(scroll);
}
class SortHeader final:public QHeaderView{
    int section_{1};
public:
    std::function<void(int)> onSort;
    explicit SortHeader(QWidget* parent):QHeaderView(Qt::Horizontal,parent){
        setFocusPolicy(Qt::StrongFocus);setSectionsClickable(true);setSortIndicatorShown(true);
        setAccessibleName("Sort miners. Left and Right choose a column. Enter or Space changes its order.");
        setObjectName("pgn-social-miner-sort-header");
        connect(this,&QHeaderView::sectionClicked,this,[this](int section){section_=section;if(onSort)onSort(section);});
    }
protected:
    void keyPressEvent(QKeyEvent* event)override{
        if(event->key()==Qt::Key_Left||event->key()==Qt::Key_Right){section_=qBound(0,section_+(event->key()==Qt::Key_Right?1:-1),count()-1);viewport()->update();event->accept();return;}
        if(event->key()==Qt::Key_Home||event->key()==Qt::Key_End){section_=event->key()==Qt::Key_Home?0:count()-1;viewport()->update();event->accept();return;}
        if(event->key()==Qt::Key_Enter||event->key()==Qt::Key_Return||event->key()==Qt::Key_Space){if(onSort)onSort(section_);event->accept();return;}
        QHeaderView::keyPressEvent(event);
    }
    void paintSection(QPainter* painter,const QRect& rect,int logical)const override{
        QHeaderView::paintSection(painter,rect,logical);
        if(hasFocus()&&logical==section_){QStyleOptionFocusRect option;option.initFrom(this);option.rect=rect.adjusted(3,3,-3,-3);style()->drawPrimitive(QStyle::PE_FrameFocusRect,&option,painter,this);}
    }
};
}
SocialView::SocialView(QString path,social::Services services,QWidget* parent)
    :QWidget(parent),services_(std::move(services)),store_(std::move(path)){
    setObjectName("pgn-social-workspace");
    setStyleSheet(SocialLightStyle()+SocialChatContrastStyle()+SocialReadableStyle());
    auto* box=vertical(this);box->setSpacing(0);tabs_=new QTabWidget;tabs_->setProperty("socialSurface",true);tabs_->setObjectName("pgn-social-sections");
    for(int i=0;i<4;++i){
        auto& p=pages_[i];auto* page=new QWidget;if(i==0)page->setObjectName("pgn-social-chat-surface");auto* layout=vertical(page,14);
        auto* tools=new QHBoxLayout;tools->setSpacing(8);
        if(i==0){channelTitle_=text("Conversation");channelTitle_->setProperty("heading",true);channelTitle_->setObjectName("pgn-social-global-channel");tools->addWidget(channelTitle_);
            channelDescription_=text({});channelDescription_->setProperty("caption",true);channelDescription_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);tools->addWidget(channelDescription_,1);}
        if(i==3){
            tools->addWidget(text("Window"));
            p.filter=new SocialComboBox;p.filter->setObjectName("pgn-social-miner-window");
            p.filter->setAccessibleName("Miner observation window");
             p.filter->addItem("All recorded activity");p.filter->addItem(social::windowLabel(social::Window::Day));p.filter->setToolTip("Confirmed activity on the current chain. Today uses canonical block-header timestamps in a fixed UTC day, not physical execution time. Hardware count and mining time are not reported by this source.");
            tools->addWidget(p.filter);
             p.windowInfo=text({});p.windowInfo->setObjectName("pgn-social-miner-interval");p.windowInfo->setProperty("caption",true);tools->addWidget(p.windowInfo);
        }
        p.search=new QLineEdit;p.search->setMaxLength(160);p.search->setPlaceholderText(i==0?"Find in loaded messages":"Filter loaded "+QString(nouns[i]));
        p.search->setAccessibleName(QString("Filter loaded %1").arg(nouns[i]));p.search->setObjectName(QString("pgn-social-search-%1").arg(i));tools->addWidget(p.search,i==0?0:1);
        if(i==0){p.search->setFixedWidth(180);p.search->setPlaceholderText("Search messages");
            auto* members=new QPushButton("Members");members->setObjectName("pgn-social-toggle-members");members->setCheckable(true);members->setChecked(true);tools->addWidget(members);
            connect(members,&QPushButton::toggled,this,[this](bool shown){membersPane_->setVisible(shown);});}
        p.refresh=new QPushButton("Refresh");p.refresh->setObjectName(QString("pgn-social-refresh-%1").arg(i));tools->addWidget(p.refresh);
        connect(p.refresh,&QPushButton::clicked,this,[this,i]{read(i);});connect(p.search,&QLineEdit::textChanged,this,[this,i]{filter(i);});
        auto* browse=new QWidget;auto* browseBox=vertical(browse);browseBox->addLayout(tools);
        p.dataSlot=new QStackedWidget;p.dataSlot->setObjectName(QString("pgn-social-data-slot-%1").arg(i));
        if(i==3){
            p.ranking=new QTableWidget(0,6);p.ranking->setObjectName("pgn-social-miners");
            auto* header=new SortHeader(p.ranking);p.ranking->setHorizontalHeader(header);
            p.ranking->setHorizontalHeaderLabels({"Miner","Validated positions","Tickets reported","Tickets validated","Audits validated","Last activity · block"});
            header->setSectionResizeMode(0,QHeaderView::Stretch);for(int col=1;col<6;++col)header->setSectionResizeMode(col,QHeaderView::ResizeToContents);
            header->setSortIndicator(1,Qt::DescendingOrder);header->onSort=[this](int col){sortDescending_=sortColumn_==col?!sortDescending_:col!=0;sortColumn_=col;renderMiners();};
            p.ranking->verticalHeader()->hide();p.ranking->setEditTriggers(QAbstractItemView::NoEditTriggers);p.ranking->setSelectionBehavior(QAbstractItemView::SelectRows);
            p.ranking->setSelectionMode(QAbstractItemView::SingleSelection);p.ranking->setWordWrap(false);p.ranking->verticalHeader()->setDefaultSectionSize(32);
            p.dataSlot->addWidget(p.ranking);
             connect(p.filter,&QComboBox::currentIndexChanged,this,[this]{auto& q=pages_[3];++q.generation;q.busy=q.loaded=false;q.nextCursor.clear();q.miners.clear();q.leaderboardPeriod.reset();q.windowInfo->setText(q.filter->currentIndex()==1?"UTC interval loading…":QString{});renderMiners();updateAccess();read(3);});
        }else{
        p.list=new QListWidget;p.list->setWordWrap(true);p.list->setMouseTracking(true);p.list->viewport()->setAttribute(Qt::WA_Hover,true);p.list->setObjectName(QString("pgn-social-list-%1").arg(i));p.list->setAccessibleName(i==0?"Global message chronology":i==1?"Publication feed":"Forum topics");
            p.list->setItemDelegate(new SocialEntryDelegate(i,p.list));p.list->setMouseTracking(true);
            if(i==0)p.list->installEventFilter(this);if(i==2){p.list->viewport()->installEventFilter(this);p.list->setCursor(Qt::PointingHandCursor);}
            p.list->setSelectionMode(QAbstractItemView::SingleSelection);p.dataSlot->addWidget(p.list);
            if(i>0){connect(p.list,&QListWidget::itemActivated,this,[this,i]{openRead(i);});connect(p.list,&QListWidget::itemClicked,this,[this,i]{openRead(i);});}
        }
        auto* emptyPane=new QWidget;emptyPane->setObjectName(QString("pgn-social-empty-pane-%1").arg(i));auto* emptyBox=vertical(emptyPane,12);p.empty=text({});p.empty->setWordWrap(true);p.empty->setAlignment(Qt::AlignLeft|Qt::AlignTop);
        p.empty->setObjectName(QString("pgn-social-empty-%1").arg(i));emptyBox->addWidget(p.empty);emptyBox->addStretch();p.dataSlot->addWidget(emptyPane);
        p.dataSlot->setCurrentIndex(1);
        if(i==2){auto* headers=new QHBoxLayout;headers->setContentsMargins(24,4,12,4);headers->setSpacing(0);headers->addWidget(text("Topic"),1);for(const auto& spec:QList<QPair<QString,int>>{{"Participants",140},{"Replies",88},{"Activity",140},{"State",100}}){auto* label=text(spec.first);label->setFixedWidth(spec.second);headers->addWidget(label);}browseBox->addLayout(headers);}
        browseBox->addWidget(p.dataSlot,1);
        auto* pager=new QHBoxLayout;p.count=text({});p.count->setObjectName(QString("pgn-social-loaded-%1").arg(i));pager->addWidget(p.count,1);
        p.more=new QPushButton(i==0?"Older messages":i==1?"Older publications":i==2?"More topics":"Load more");p.more->setObjectName(QString("pgn-social-more-%1").arg(i));pager->addWidget(p.more);browseBox->addLayout(pager);
        connect(p.more,&QPushButton::clicked,this,[this,i]{read(i,true);});
        if(i==0){
            layout->setContentsMargins(0,0,0,0);auto* columns=new QHBoxLayout;columns->setSpacing(0);
            auto* rail=new QFrame;rail->setObjectName("pgn-social-channels-pane");rail->setFixedWidth(190);auto* railBox=vertical(rail,14);railBox->setSpacing(12);
            auto* heading=text("Network");heading->setProperty("heading",true);railBox->addWidget(heading);auto* caption=text("Text channels");caption->setProperty("caption",true);railBox->addWidget(caption);
            channels_=new QListWidget;channels_->setObjectName("pgn-social-chat-channels");channels_->setAccessibleName("Text channels from the authenticated service");railBox->addWidget(channels_,1);
            connect(channels_,&QListWidget::itemActivated,this,[this]{selectChannel();});connect(channels_,&QListWidget::itemClicked,this,[this]{selectChannel();});
            auto* access=new QPushButton("Roles && members");access->setObjectName("pgn-social-open-access");railBox->addWidget(access);connect(access,&QPushButton::clicked,this,[this]{openAccess();});columns->addWidget(rail);
            channels_->setContextMenuPolicy(Qt::CustomContextMenu);connect(channels_,&QListWidget::customContextMenuRequested,this,[this](QPoint pos){auto* item=channels_->itemAt(pos);if(!item)item=channels_->currentItem();if(!item)return;const QString id=item->data(Qt::UserRole).toString();auto* menu=new QMenu(this);menu->setProperty("socialSurface",true);auto* copy=menu->addAction("Copy channel ID");connect(copy,&QAction::triggered,this,[id]{QApplication::clipboard()->setText(id);});auto* permissions=menu->addAction("Channel permissions…");connect(permissions,&QAction::triggered,this,[this,id]{openAccess({},id);});connect(menu,&QMenu::aboutToHide,menu,&QObject::deleteLater);menu->popup(channels_->viewport()->mapToGlobal(pos));});
            auto* conversation=new QWidget;auto* cb=vertical(conversation,12);cb->setSpacing(8);cb->addWidget(browse,1);cb->addWidget(makeComposer(i));columns->addWidget(conversation,1);
            auto* side=new QFrame;side->setObjectName("pgn-social-members-pane");side->setFixedWidth(204);membersPane_=side;auto* mb=vertical(side,14);memberCount_=text("Participants unavailable");memberCount_->setProperty("caption",true);mb->addWidget(memberCount_);
            members_=new QListWidget;members_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);members_->setTextElideMode(Qt::ElideRight);members_->setObjectName("pgn-social-chat-members");members_->setItemDelegate(new SocialMemberDelegate(members_));members_->setAccessibleName("Authenticated channel participants and reported presence");mb->addWidget(members_,1);
            connect(members_,&QListWidget::itemActivated,this,[this]{if(members_->currentItem())openAccess(members_->currentItem()->data(Qt::UserRole).toString());});
            members_->setContextMenuPolicy(Qt::CustomContextMenu);connect(members_,&QListWidget::customContextMenuRequested,this,[this](QPoint pos){auto* item=members_->itemAt(pos);if(!item)item=members_->currentItem();if(!item||item->data(Qt::UserRole).toString().isEmpty())return;const auto id=item->data(Qt::UserRole).toString();auto* menu=new QMenu(this);menu->setProperty("socialSurface",true);auto* copy=menu->addAction("Copy full identity");connect(copy,&QAction::triggered,this,[id]{QApplication::clipboard()->setText(id);});auto* manage=menu->addAction("Roles & member actions…");connect(manage,&QAction::triggered,this,[this,id]{openAccess(id);});connect(menu,&QMenu::aboutToHide,menu,&QObject::deleteLater);menu->popup(members_->viewport()->mapToGlobal(pos.x()<0?QPoint(20,20):pos));});
            columns->addWidget(side);layout->addLayout(columns,1);
        }
        else if(i==3)layout->addWidget(browse,1);
        else{
            auto* create=new QPushButton(i==1?"New publication":"New topic");create->setObjectName(QString("pgn-social-new-%1").arg(i));tools->addWidget(create);
            connect(create,&QPushButton::clicked,this,[this,i]{openComposer(i);});
            p.journey=new QStackedWidget;p.journey->setObjectName(QString("pgn-social-journey-%1").arg(i));
            if(i==2){auto* overview=new QWidget;auto* columns=new QHBoxLayout(overview);columns->setContentsMargins(0,0,0,0);columns->setSpacing(12);auto* categories=new QWidget;categories->setFixedWidth(164);auto* cb=vertical(categories);auto* heading=text("Categories");heading->setProperty("heading",true);cb->addWidget(heading);forumCategories_=new QListWidget;forumCategories_->setObjectName("pgn-social-forum-categories");forumCategories_->setAccessibleName("Forum categories");forumCategories_->addItems(social::categories());forumCategories_->setCurrentRow(0);forumCategories_->setStyleSheet("QListWidget{background:#09262d;color:#c9eee7;border:0;}QListWidget::item:selected{background:#16434a;color:#ffffff;}");cb->addWidget(forumCategories_,1);connect(forumCategories_,&QListWidget::currentRowChanged,this,[this]{filter(2);});columns->addWidget(categories);columns->addWidget(browse,1);p.journey->addWidget(overview);}else p.journey->addWidget(browse);
            auto* reading=new QWidget;auto* rb=vertical(reading);auto* readActions=new QHBoxLayout;
            auto* backButton=new QPushButton(i==1?"← Publications":"← Topics");backButton->setObjectName(QString("pgn-social-back-read-%1").arg(i));readActions->addWidget(backButton);readActions->addStretch();
            connect(backButton,&QPushButton::clicked,this,[this,i]{back(i);});
            rb->addLayout(readActions);
            if(i==2){p.threadTitle=text({});p.threadTitle->setProperty("heading",true);p.threadTitle->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);p.threadTitle->setObjectName("pgn-social-forum-title");rb->addWidget(p.threadTitle);p.thread=new ForumThread;rb->addWidget(p.thread,1);
                p.thread->onReply=[this](QString post){replyToPost(post);};p.thread->onQuote=[this](QString post,QString quote){replyToPost(post,quote);};p.thread->onMenu=[this](QString post,QPoint pos){contextMenu(2,pos,true,post);};
                p.thread->onReact=[this](QString post,QString reaction){reactToPost(post,reaction);};p.thread->reactionUnavailable=[this](QString post,QString reaction){const auto r=postRequest(post,social::AccessAction::ReactContent,reaction);return social::unavailable(access_?&*access_:nullptr,r,bool(services_.manage));};
                p.thread->setContextMenuPolicy(Qt::CustomContextMenu);connect(p.thread,&QListWidget::customContextMenuRequested,this,[this](QPoint pos){contextMenu(2,pos,true);});auto* footer=new QHBoxLayout;p.threadState=text({});p.threadState->setObjectName("pgn-social-forum-thread-state");footer->addWidget(p.threadState,1);p.moreReplies=new QPushButton("More replies");p.moreReplies->setObjectName("pgn-social-more-replies");footer->addWidget(p.moreReplies);connect(p.moreReplies,&QPushButton::clicked,this,[this]{readThread();});rb->addLayout(footer);
            }else{p.detail=new QTextBrowser;p.detail->setDocument(new social::bbcode::Document(p.detail));p.detail->document()->setDocumentMargin(24);p.detail->setOpenLinks(false);p.detail->setOpenExternalLinks(false);p.detail->setObjectName(QString("pgn-social-detail-%1").arg(i));rb->addWidget(p.detail,1);p.detail->setContextMenuPolicy(Qt::CustomContextMenu);connect(p.detail,&QTextBrowser::customContextMenuRequested,this,[this,i](QPoint pos){contextMenu(i,pos,true);});}
            if(i==2){auto* replyActions=new QHBoxLayout;p.reply=new QPushButton("Reply");p.reply->setObjectName("pgn-social-reply-topic");replyActions->addStretch();replyActions->addWidget(p.reply);rb->addLayout(replyActions);connect(p.reply,&QPushButton::clicked,this,[this]{openComposer(2,true);});}
            p.journey->addWidget(reading);p.journey->addWidget(makeComposer(i));
            if(i==1||i==2){auto* previewPane=new QWidget;auto* pv=vertical(previewPane);auto* bar=new QHBoxLayout;
                auto* returnDraft=new QPushButton("← Edit draft");returnDraft->setObjectName("pgn-social-back-preview");bar->addWidget(returnDraft);bar->addWidget(text("Preview · local draft"),1);pv->addLayout(bar);
                returnDraft->setObjectName(i==1?"pgn-social-back-preview":"pgn-social-forum-back-preview");p.preview=new QTextBrowser;p.preview->setDocument(new social::bbcode::Document(p.preview));p.preview->document()->setDocumentMargin(24);p.preview->setOpenLinks(false);p.preview->setOpenExternalLinks(false);p.preview->setObjectName(i==1?"pgn-social-publication-preview":"pgn-social-forum-preview");pv->addWidget(p.preview,1);
                connect(returnDraft,&QPushButton::clicked,this,[this,i]{pages_[i].journey->setCurrentIndex(2);pages_[i].draft->setFocus();});p.journey->addWidget(previewPane);}
            layout->addWidget(p.journey,1);
        }
        tabs_->addTab(page,names[i]);
        QWidget* target=i==3?static_cast<QWidget*>(p.ranking):static_cast<QWidget*>(p.list);target->setContextMenuPolicy(Qt::CustomContextMenu);connect(target,&QWidget::customContextMenuRequested,this,[this,i](QPoint pos){contextMenu(i,pos);});
        if(i==3){QWidget::setTabOrder(p.refresh,p.ranking->horizontalHeader());QWidget::setTabOrder(p.ranking->horizontalHeader(),p.ranking);}
    }
    auto* nav=HorizonCoinNavigation(tabs_,{{"01  CHAT","GLOBAL CHANNEL"},{"02  PUBLICATIONS","IDEAS + UPDATES"},{"03  FORUM","TOPICS + REPLIES"},{"04  MINERS","VERIFIED ACTIVITY"}},this);
    // Retain legacy pages/drafts, remove them from every user navigation route.
    tabs_->setTabVisible(1,false);tabs_->setTabVisible(2,false);tabs_->setTabText(3,"Leaderboard");
    for(const int legacy:{1,2}){tabs_->widget(legacy)->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);tabs_->widget(legacy)->setMinimumSize(0,0);}
    if(auto* group=nav->findChild<QButtonGroup*>()){
        group->button(1)->hide();group->button(2)->hide();
        group->button(3)->setText("02  LEADERBOARD\nVERIFIED ACTIVITY");
    }
    nav->setStyleSheet(nav->styleSheet()+"QPushButton{font:600 12px 'Segoe UI';}QPushButton:focus{border:2px solid #7eafb8;}");box->addWidget(nav);
    auto* context=new QWidget;context->setProperty("socialSurface",true);context->setProperty("socialChrome",true);context->setFixedHeight(30);auto* contextBox=new QHBoxLayout(context);contextBox->setContentsMargins(14,2,14,2);contextBox->setSpacing(12);
    account_=text({});account_->setObjectName("pgn-social-account");account_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);contextBox->addWidget(account_,1);
    account_->setContextMenuPolicy(Qt::CustomContextMenu);connect(account_,&QLabel::customContextMenuRequested,this,[this](QPoint pos){if(identity_.subject.isEmpty())return;auto* menu=new QMenu(this);auto* copy=menu->addAction("Copy full identity");connect(copy,&QAction::triggered,this,[this]{QApplication::clipboard()->setText(identity_.subject);});connect(menu,&QMenu::aboutToHide,menu,&QObject::deleteLater);menu->popup(account_->mapToGlobal(pos));});
    network_=text({});network_->setObjectName("pgn-social-network");network_->setAlignment(Qt::AlignRight|Qt::AlignVCenter);contextBox->addWidget(network_,1);box->addWidget(context);
    status_=text({});status_->setProperty("socialSurface",true);status_->setProperty("socialChrome",true);status_->setObjectName("pgn-social-status");status_->setFixedHeight(22);status_->setContentsMargins(0,0,0,0);status_->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Fixed);status_->setMinimumWidth(180);status_->setMaximumWidth(540);contextBox->insertWidget(1,status_);box->addWidget(tabs_,1);
    tabs_->setFocusPolicy(Qt::NoFocus);tabs_->tabBar()->setFocusPolicy(Qt::NoFocus);
    connect(tabs_,&QTabWidget::currentChanged,this,[this](int i){updateStatus();if(!pages_[i].loaded)read(i);});
    for(int i=0;i<2;++i){const int page=i==0?0:3;auto* shortcut=new QShortcut(QKeySequence(QString("Ctrl+%1").arg(i+1)),this);shortcut->setContext(Qt::WidgetWithChildrenShortcut);connect(shortcut,&QShortcut::activated,this,[this,page]{selectSection(static_cast<social::Section>(page));});}
    auto* save=new QShortcut(QKeySequence::Save,this);save->setContext(Qt::WidgetWithChildrenShortcut);connect(save,&QShortcut::activated,this,[this]{saveDrafts();});
    auto* toggleMembers=new QShortcut(QKeySequence("Ctrl+U"),this);toggleMembers->setContext(Qt::WidgetWithChildrenShortcut);connect(toggleMembers,&QShortcut::activated,this,[this]{if(tabs_->currentIndex()==0)findChild<QPushButton*>("pgn-social-toggle-members")->toggle();});
    for(const auto sequence:{QKeySequence("Shift+F10"),QKeySequence(Qt::Key_Menu)}){auto* menu=new QShortcut(sequence,this);menu->setObjectName("pgn-social-keyboard-context");menu->setContext(Qt::WidgetWithChildrenShortcut);connect(menu,&QShortcut::activated,this,[this]{
        const auto* focused=QApplication::focusWidget();if(tabs_->currentIndex()==0&&(focused==channels_||focused==members_)){auto* list=focused==channels_?channels_:members_;QMetaObject::invokeMethod(list,"customContextMenuRequested",Qt::DirectConnection,Q_ARG(QPoint,QPoint(-1,-1)));return;}
        const int i=tabs_->currentIndex();QString post;if(i==2&&pages_[i].journey->currentIndex()==1)for(auto* widget=QApplication::focusWidget();widget&&widget!=this;widget=widget->parentWidget())if(widget->property("forumPostId").isValid()){post=widget->property("forumPostId").toString();break;}contextMenu(i,{-1,-1},i>0&&i<3&&pages_[i].journey->currentIndex()==1,post);});}
    for(const bool backwards:{false,true}){auto* jump=new QShortcut(QKeySequence(backwards?"Shift+F6":"F6"),this);jump->setContext(Qt::WidgetWithChildrenShortcut);connect(jump,&QShortcut::activated,this,[this,backwards]{
        if(tabs_->currentIndex()!=0)return;QVector<QWidget*> zones{channels_,pages_[0].list};if(membersPane_->isVisible())zones<<members_;zones<<pages_[0].draft;
        int current=-1;for(int i=0;i<zones.size();++i)if(zones[i]->hasFocus()||zones[i]->isAncestorOf(QApplication::focusWidget()))current=i;
        zones[(current+(backwards?zones.size()-1:1)+zones.size())%zones.size()]->setFocus();});}
    auto* escape=new QShortcut(QKeySequence(Qt::Key_Escape),this);escape->setContext(Qt::WidgetWithChildrenShortcut);connect(escape,&QShortcut::activated,this,[this]{back(tabs_->currentIndex());});
    autosave_.setSingleShot(true);autosave_.setInterval(500);connect(&autosave_,&QTimer::timeout,this,[this]{saveDrafts();});
    updateAccess();for(int i=0;i<3;++i)loadEditor(i);
}
QWidget* SocialView::makeComposer(int i){
    auto& p=pages_[i];auto* compose=new QWidget;compose->setObjectName(QString("pgn-social-composer-%1").arg(i));auto* box=vertical(compose);
    if(i>0){auto* bar=new QHBoxLayout;auto* button=new QPushButton(i==1?"← Publications":"← Topic");button->setObjectName(QString("pgn-social-back-compose-%1").arg(i));bar->addWidget(button);
        p.composeContext=text(i==1?"New publication":"New topic");p.composeContext->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);bar->addWidget(p.composeContext,1);box->addLayout(bar);connect(button,&QPushButton::clicked,this,[this,i]{back(i);});
        if(i==2){p.categoryRow=new QWidget;auto* row=new QHBoxLayout(p.categoryRow);row->setContentsMargins(0,0,0,0);row->addWidget(text("Category"));p.category=new SocialComboBox;p.category->addItems(social::categories());p.category->setObjectName("pgn-social-topic-category");row->addWidget(p.category,1);box->addWidget(p.categoryRow);}
        p.titleRow=new QWidget;auto* row=new QHBoxLayout(p.titleRow);row->setContentsMargins(0,0,0,0);row->addWidget(text("Title"));
        p.title=new QLineEdit;p.title->setMaxLength(160);p.title->setAccessibleName(i==1?"Publication title":"Topic title");p.title->setObjectName(QString("pgn-social-draft-title-%1").arg(i));row->addWidget(p.title,1);box->addWidget(p.titleRow);
    }
    p.draft=new QPlainTextEdit;p.draft->setTabChangesFocus(true);p.draft->setMinimumHeight(60);p.draft->setObjectName(QString("pgn-social-draft-body-%1").arg(i));p.draft->setAccessibleName(QString("%1 draft text").arg(names[i]));
    p.draft->setPlaceholderText(i==0?"Write a message · BBCode supported":i==1?"Write your publication…":"Write a topic or reply…");
    {auto palette=p.draft->palette();palette.setColor(QPalette::PlaceholderText,QColor("#526e78"));p.draft->setPalette(palette);}
    if(i==0){
        auto* toolbar=new QHBoxLayout;toolbar->setSpacing(4);
        for(const auto& tag:{"b","i","u","s","quote","code","url"}){const QString t=tag;auto* format=new QPushButton(t=="b"?"B":t=="i"?"I":t=="u"?"U":t=="s"?"S":t=="url"?"Link":t=="quote"?"Quote":"Code");
            format->setObjectName("pgn-social-bbcode-"+t);format->setProperty("bbcodeTool",true);format->setAccessibleName("Insert BBCode "+t);format->setToolTip(social::bbcode::opening(t)+"…[/"+t+"]");format->setMaximumWidth(t.size()==1?34:62);toolbar->addWidget(format);connect(format,&QPushButton::clicked,this,[this,t]{formatChat(t);});}
        toolbar->addStretch();previewChat_=new QPushButton("Preview");previewChat_->setObjectName("pgn-social-preview-chat");previewChat_->setCheckable(true);toolbar->addWidget(previewChat_);connect(previewChat_,&QPushButton::clicked,this,[this]{previewChat();});box->addLayout(toolbar);
        chatEditor_=new QStackedWidget;p.draft->setFixedHeight(62);chatEditor_->addWidget(p.draft);chatPreview_=new QTextBrowser;chatPreview_->setDocument(new social::bbcode::Document(chatPreview_));chatPreview_->setOpenLinks(false);chatPreview_->setOpenExternalLinks(false);chatPreview_->setObjectName("pgn-social-chat-preview");chatEditor_->addWidget(chatPreview_);chatEditor_->setFixedHeight(62);box->addWidget(chatEditor_);
    }else{
        if(i==2){auto* toolbar=new QHBoxLayout;for(const auto& tag:{"b","i","u","s","quote","code","url"}){const QString t=tag;auto* button=new QPushButton(t=="url"?"Link":t=="quote"?"Quote":t=="code"?"Code":t.toUpper());button->setObjectName("pgn-social-forum-bbcode-"+t);button->setToolTip(social::bbcode::opening(t)+"…[/"+t+"]");toolbar->addWidget(button);connect(button,&QPushButton::clicked,this,[this,t]{formatDraft(2,t);});}toolbar->addStretch();box->addLayout(toolbar);}
        box->addWidget(p.draft,1);
    }
    if(i==1){auto* row=new QHBoxLayout;row->addWidget(text("Ticket / Gold reference (optional)"));p.reference=new QLineEdit;p.reference->setMaxLength(256);p.reference->setAccessibleName("Optional ticket or Gold reference");p.reference->setObjectName("pgn-social-publication-reference");row->addWidget(p.reference,1);box->addLayout(row);}
    auto* actions=new QHBoxLayout;p.saved=text({});p.saved->setObjectName(QString("pgn-social-save-status-%1").arg(i));p.saved->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);actions->addWidget(p.saved,1);
    p.counter=text({});p.counter->setObjectName(QString("pgn-social-counter-%1").arg(i));actions->addWidget(p.counter);
    p.save=new QPushButton("Save draft");p.save->setObjectName(QString("pgn-social-save-%1").arg(i));actions->addWidget(p.save);
    auto* clear=new QPushButton("Clear");clear->setObjectName(QString("pgn-social-clear-%1").arg(i));actions->addWidget(clear);
    if(i==1||i==2){auto* preview=new QPushButton("Preview");preview->setObjectName(i==1?"pgn-social-preview-publication":"pgn-social-preview-forum");actions->addWidget(preview);connect(preview,&QPushButton::clicked,this,[this,i]{previewPublication(i);});}
    p.send=new QPushButton(i==0?"Send":i==1?"Publish":"Post topic");p.send->setObjectName(QString("pgn-social-send-%1").arg(i));p.send->setEnabled(false);actions->addWidget(p.send);box->addLayout(actions);
    if(i==0){p.send->setMinimumSize(96,36);p.send->setToolTip("Enter sends · Shift+Enter starts a new line");p.draft->installEventFilter(this);QPixmap plane(18,18);plane.fill(Qt::transparent);QPainter painter(&plane);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(QPen(QColor("#f3ffff"),1.6,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));painter.drawPolyline(QPolygonF{{2,8},{15,2},{10,16},{7,10},{2,8}});painter.drawLine(QPointF(7,10),QPointF(15,2));painter.end();p.send->setIcon(QIcon(plane));}
    p.reason=text("Sending unavailable · no authenticated send service connected.");p.reason->setObjectName(QString("pgn-social-send-reason-%1").arg(i));box->addWidget(p.reason);
    if(i==0)connect(p.send,&QPushButton::clicked,this,[this]{sendChat();});
    connect(p.save,&QPushButton::clicked,this,[this]{saveDrafts();});connect(clear,&QPushButton::clicked,this,[this,i]{
        pages_[i].draft->clear();if(!pages_[i].replyMode){if(pages_[i].title)pages_[i].title->clear();if(pages_[i].reference)pages_[i].reference->clear();}saveDrafts();
    });
    connect(p.draft,&QPlainTextEdit::textChanged,this,[this,i]{draftChanged(i);});
    if(p.title)connect(p.title,&QLineEdit::textChanged,this,[this,i]{draftChanged(i);});
    if(p.reference)connect(p.reference,&QLineEdit::textChanged,this,[this,i]{draftChanged(i);});
    if(p.category)connect(p.category,&QComboBox::currentTextChanged,this,[this,i]{draftChanged(i);});
    return compose;
}
SocialView::~SocialView(){if(dirty_&&!loadFailed_&&identity_.valid())store_.save(identity_,drafts_);}
void SocialView::selectSection(social::Section section){const int i=static_cast<int>(section);if(i==0||i==3)tabs_->setCurrentIndex(i);}
void SocialView::loadEditor(int i){
    QScopedValueRollback<bool> guard(loadingDraft_,true);auto& p=pages_[i];const auto& d=drafts_.sections[i];
    const bool reply=i==2&&p.replyMode;const bool room=!reply||drafts_.replies.contains(replyId_)||drafts_.replies.size()<8;
    const bool chatRoom=i!=0||channel_.isEmpty()||drafts_.chats.contains(channel_)||drafts_.chats.size()<16;
    const bool enabled=identity_.valid()&&!loadFailed_&&room&&chatRoom&&(!reply||!replyId_.isEmpty());
    if(p.title){p.titleRow->setVisible(!reply);p.title->setText(d.title);p.title->setEnabled(enabled);}
    if(p.reference){p.reference->setText(d.reference);p.reference->setEnabled(enabled);}
    if(p.category){p.categoryRow->setVisible(!reply);p.category->setCurrentText(d.category);p.category->setEnabled(enabled);}
    p.draft->setPlainText(i==0&&!channel_.isEmpty()?drafts_.chats.value(channel_):reply?drafts_.replies.value(replyId_):d.body);p.draft->setEnabled(enabled);p.save->setEnabled(enabled);
    if(i==0){chatEditor_->setCurrentIndex(0);previewChat_->setChecked(false);for(auto* button:findChildren<QPushButton*>())if(button->objectName().startsWith("pgn-social-bbcode-"))button->setEnabled(enabled);}
    p.send->setText(i==0?"Send":i==1?"Publish":reply?"Send reply":"Post topic");p.send->setEnabled(false);
    p.counter->setText(QString("%1 / %2").arg(p.draft->toPlainText().size()).arg(reply?4000:social::draftLimit(i)));
    p.saved->setText(loadFailed_?"Draft file unavailable":!identity_.valid()?"Select identity and network for drafts":!room?"Eight reply drafts already saved":reply&&replyId_.isEmpty()?"Select a topic before replying":"Local draft · autosave");
    p.saved->setToolTip(safeTip(loadError_));
    if(i==0)updateChatSend();
}
void SocialView::draftChanged(int i){
    if(loadingDraft_||!identity_.valid()||loadFailed_)return;auto& p=pages_[i];const bool reply=i==2&&p.replyMode;
    if(reply&&replyId_.isEmpty())return;const int limit=reply?4000:social::draftLimit(i);
    if(p.draft->toPlainText().size()>limit){QSignalBlocker guard(p.draft);auto value=p.draft->toPlainText().left(limit);if(!value.isEmpty()&&value.back().isHighSurrogate())value.chop(1);p.draft->setPlainText(value);}
    if(i==0&&!channel_.isEmpty()){const auto value=p.draft->toPlainText();if(value.isEmpty())drafts_.chats.remove(channel_);else if(drafts_.chats.contains(channel_)||drafts_.chats.size()<16)drafts_.chats[channel_]=value;}
    else if(reply){const auto value=p.draft->toPlainText();if(value.isEmpty()){drafts_.replies.remove(replyId_);drafts_.replyTargets.remove(replyId_);}else if(drafts_.replies.contains(replyId_)||drafts_.replies.size()<8){drafts_.replies.insert(replyId_,value);drafts_.replyTargets.insert(replyId_,replyPostId_.isEmpty()?replyId_:replyPostId_);}}
    else{auto& d=drafts_.sections[i];d.body=p.draft->toPlainText();if(p.title)d.title=p.title->text();if(p.reference)d.reference=p.reference->text();if(p.category)d.category=p.category->currentText();}
    dirty_=true;p.counter->setText(QString("%1 / %2").arg(p.draft->toPlainText().size()).arg(limit));p.saved->setText("Saving locally…");autosave_.start();
    if(i==0){if(!p.sending)p.sendNotice.clear();updateChatSend();}
}
bool SocialView::saveDrafts(){
    autosave_.stop();QString error=!identity_.valid()?"Select identity and network before saving.":loadFailed_?loadError_:store_.save(identity_,drafts_);
    for(int i=0;i<3;++i){pages_[i].saved->setText(error.isEmpty()?"Saved locally · "+QDateTime::currentDateTimeUtc().toString("HH:mm"):"Draft not saved");pages_[i].saved->setToolTip(safeTip(error));}
    if(error.isEmpty()){dirty_=false;pending_.remove(store_.path(identity_));}
    else if(dirty_&&identity_.valid()&&(pending_.contains(store_.path(identity_))||pending_.size()<64))pending_.insert(store_.path(identity_),drafts_);
    return error.isEmpty();
}
void SocialView::setIdentity(social::Identity identity){
    if(identity==identity_)return;if(dirty_)saveDrafts();autosave_.stop();++sendEpoch_;identity_=std::move(identity);drafts_={};dirty_=false;loadError_.clear();loadFailed_=false;replyId_.clear();pages_[0].canSend=pages_[0].sending=false;pages_[0].sendNotice.clear();pages_[0].sendWhy.clear();
    channel_.clear();channelData_.clear();channels_->clear();members_->clear();replyPostId_.clear();memberCount_->setText("Participants unavailable");channelTitle_->setText("Conversation");channelDescription_->clear();access_.reset();if(accessDialog_)delete accessDialog_;
    if(identity_.valid()){loadError_=store_.load(identity_,drafts_);loadFailed_=!loadError_.isEmpty();const auto key=store_.path(identity_);if(pending_.contains(key)){drafts_=pending_.value(key);dirty_=true;}else if(pending_.size()>=64){loadFailed_=true;loadError_="Recover pending drafts before editing another identity.";}}
    for(auto& p:pages_){++p.generation;++p.threadGeneration;p.threadBusy=false;if(p.thread)p.thread->clear();p.loaded=p.busy=p.replyMode=false;p.entries.clear();p.miners.clear();p.selectedId.clear();p.nextCursor.clear();p.observedUtc={};p.leaderboardPeriod.reset();if(p.windowInfo){p.windowInfo->clear();p.windowInfo->setToolTip({});}
        if(p.list){QSignalBlocker block(p.list);p.list->clear();}if(p.detail)p.detail->clear();if(p.ranking)p.ranking->setRowCount(0);if(p.journey)p.journey->setCurrentIndex(0);}
    updateAccess();for(int i=0;i<3;++i)loadEditor(i);read(tabs_->currentIndex());
}
void SocialView::pageState(int i,QString state,QString message,QString detail){
    auto& p=pages_[i];p.state=std::move(state);p.statusText=std::move(message);if(detail.isEmpty())detail=p.statusText;
    if(tabs_->currentIndex()==i){status_->setText(p.statusText);status_->setToolTip(safeTip(detail));status_->setProperty("socialState",p.state);}
    setProperty(QString("pgn_social_state_%1").arg(i).toLatin1().constData(),p.state);
}
void SocialView::updateStatus(){
    const auto& p=pages_[tabs_->currentIndex()];status_->setText(p.statusText);status_->setToolTip(safeTip(p.statusText));status_->setProperty("socialState",p.state);
}
void SocialView::updateAccess(){
    const bool connected=identity_.valid()&&bool(services_.read);
    account_->setText(identity_.valid()?SocialDisplayName(identity_.subject):"Identity not selected");account_->setToolTip(safeTip(identity_.subject));
    network_->setText(identity_.valid()?"Selected PGN network":"Network not selected");network_->setToolTip(safeTip(identity_.genesis));
    for(int i=0;i<4;++i){auto& p=pages_[i];p.refresh->setEnabled(connected&&!p.busy);
        const int count=i==3?p.miners.size():p.entries.size();p.more->setEnabled(connected&&!p.busy&&!p.nextCursor.isEmpty()&&count<LoadedLimit);
        p.count->setText(p.loaded?QString("%1 %2 loaded").arg(count).arg(nouns[i]):QString{});
        if(!connected){pageState(i,"offline","Offline · social service not connected");p.empty->setText("Connect an authenticated social service to load "+QString(nouns[i])+".");p.dataSlot->setCurrentIndex(1);}
        else if(!p.loaded){pageState(i,"ready","Ready to load "+QString(nouns[i]));p.empty->setText("No "+QString(nouns[i])+" loaded yet.");p.dataSlot->setCurrentIndex(1);}
    }
    updateStatus();
}
void SocialView::openRead(int i){
    auto& p=pages_[i];const int row=p.list->currentRow();if(row<0||row>=p.entries.size())return;p.selectedId=p.entries[row].id;++p.threadGeneration;p.threadBusy=false;showDetail(i);p.journey->setCurrentIndex(1);if(p.thread)p.thread->setFocus();else p.detail->setFocus();
}
void SocialView::showDetail(int i,bool preserve){
    auto& p=pages_[i];const auto it=std::find_if(p.entries.begin(),p.entries.end(),[&](const auto& item){return item.id==p.selectedId;});
    if(it==p.entries.end()){if(p.detail)p.detail->setPlainText("This item is unavailable in the current snapshot. Return to the list or refresh.");if(p.thread){p.thread->clear();p.threadTitle->setText("Topic unavailable in the current snapshot");p.moreReplies->setEnabled(false);}if(p.reply)p.reply->setEnabled(false);return;}
    if(p.thread){p.threadTitle->setText(it->category+" / "+it->title);p.thread->canDraft=identity_.valid()&&!loadFailed_;p.thread->setTopic(*it,preserve);
        p.threadState->setText(QString("%1 replies loaded").arg(it->replies.size())+(it->replyCount?" · "+*it->replyCount+" total":" · total unavailable"));
        p.moreReplies->setEnabled(!p.threadBusy&&bool(services_.readThread)&&!it->nextReplyCursor.isEmpty()&&it->replies.size()<200);
        p.moreReplies->setToolTip(!it->nextReplyCursor.isEmpty()&&!services_.readThread?"No authenticated thread paging service connected.":it->replies.size()>=200?"Loaded reply limit reached.":QString{});
    }else{const int scroll=preserve?p.detail->verticalScrollBar()->value():0;document(p.detail,*it);p.detail->verticalScrollBar()->setValue(scroll);}if(p.reply)p.reply->setEnabled(true);
}
void SocialView::openComposer(int i,bool reply){
    auto& p=pages_[i];p.replyMode=reply;
    if(reply){const auto it=std::find_if(p.entries.begin(),p.entries.end(),[&](const auto& item){return item.id==p.selectedId;});if(it==p.entries.end())return;replyId_=it->id;replyPostId_=drafts_.replyTargets.value(replyId_,replyId_);p.composeContext->setText("Reply to: "+it->title);p.composeContext->setToolTip(safeTip(it->title+"\nPost: "+replyPostId_));}
    else if(p.composeContext)p.composeContext->setText(i==1?"New publication":"New topic");
    if(i==2)if(auto* button=findChild<QPushButton*>("pgn-social-back-compose-2"))button->setText(reply?"← Topic":"← Topics");
    loadEditor(i);p.journey->setCurrentIndex(2);if(p.draft->isEnabled())p.draft->setFocus();
}
void SocialView::back(int i){
    if(i==0){if(chatEditor_->currentIndex()==1){chatEditor_->setCurrentIndex(0);previewChat_->setChecked(false);pages_[0].draft->setFocus();}return;}
    if(i<1||i>2)return;auto& p=pages_[i];if(p.journey->currentIndex()==3){p.journey->setCurrentIndex(2);p.draft->setFocus();return;}
    if(p.journey->currentIndex()==2&&i==2&&p.replyMode&&!p.selectedId.isEmpty()){p.journey->setCurrentIndex(1);p.thread->setFocus();}
    else{p.journey->setCurrentIndex(0);p.list->setFocus();}
}
void SocialView::previewPublication(int i){
    auto& p=pages_[i];const auto& d=drafts_.sections[i];social::Entry entry;entry.title=p.replyMode?p.composeContext->text():d.title;entry.body=i==2&&p.replyMode?drafts_.replies.value(replyId_):d.body;entry.reference=d.reference;entry.bbcode=i==2;
    document(p.preview,entry,true);p.journey->setCurrentIndex(3);p.preview->setFocus();
}
social::AccessRequest SocialView::postRequest(QString post,social::AccessAction action,QString reaction)const{
    social::AccessRequest r;r.identity=identity_;r.community=access_?access_->community:QString{};r.revision=access_?access_->revision:QString{};r.sessionReference=access_?access_->session.reference:QString{};r.scope=social::AccessScope::Forum;r.action=action;r.reaction=std::move(reaction);
    const auto& page=pages_[2];for(const auto& topic:page.entries)if(topic.id==page.selectedId){if(post==topic.id)r.target=post;else for(const auto& reply:topic.replies)if(reply.id==post){r.target=post;r.parent=topic.id;}}
    return r;
}
void SocialView::reactToPost(QString post,QString reaction){auto r=postRequest(post,social::AccessAction::ReactContent,reaction);if(!r.target.isEmpty())manageContext(std::move(r));}
void SocialView::replyToPost(QString post,QString quote){
    auto& p=pages_[2];QString author;bool found=false;for(const auto& topic:p.entries)if(topic.id==p.selectedId){if(topic.id==post){author=topic.author;found=true;}else for(const auto& reply:topic.replies)if(reply.id==post){author=reply.author;found=true;}}
    if(!found)return;openComposer(2,true);if(!p.draft->isEnabled())return;replyPostId_=post;p.composeContext->setText("Reply to "+author);p.composeContext->setToolTip(safeTip("Topic: "+p.selectedId+"\nPost: "+post));
    if(drafts_.replies.contains(replyId_)){drafts_.replyTargets.insert(replyId_,post);dirty_=true;autosave_.start();}
    if(!quote.isEmpty()){const QString insertion="[quote]"+quote+"[/quote]\n";if(insertion.size()+p.draft->toPlainText().size()+(p.draft->toPlainText().isEmpty()?0:1)>4000){p.saved->setText("Quote exceeds reply limit · select a shorter excerpt");return;}
        auto cursor=p.draft->textCursor();cursor.movePosition(QTextCursor::End);cursor.insertText((p.draft->toPlainText().isEmpty()?QString{}:"\n")+insertion);p.draft->setTextCursor(cursor);}
    p.draft->setFocus();
}
void SocialView::readThread(){
    auto& p=pages_[2];auto it=std::find_if(p.entries.begin(),p.entries.end(),[&](const auto& e){return e.id==p.selectedId;});
    if(it==p.entries.end()||p.threadBusy||!services_.readThread||it->nextReplyCursor.isEmpty()||it->replies.size()>=200)return;
    const social::ThreadRequest request{identity_,it->id,it->nextReplyCursor,qMin(50,200-int(it->replies.size()))};const auto generation=++p.threadGeneration;p.threadBusy=true;p.moreReplies->setEnabled(false);p.threadState->setText("Loading replies…");const QPointer<SocialView> weak(this);
    services_.readThread(request,[weak,request,generation](social::ThreadSnapshot data,QString error){if(!weak||weak->identity_!=request.identity)return;auto& p=weak->pages_[2];if(!p.threadBusy||p.threadGeneration!=generation||p.selectedId!=request.topic)return;p.threadBusy=false;
        auto topic=std::find_if(p.entries.begin(),p.entries.end(),[&](const auto& e){return e.id==request.topic;});if(topic==p.entries.end())return;
        if(error.isEmpty())error=social::validateThread(data,request);
        if(error.isEmpty()){QSet<QString> ids{topic->id};for(const auto& q:topic->replies)ids.insert(q.id);for(const auto& q:data.replies)if(ids.contains(q.id))error="Reply page overlaps loaded posts.";if(topic->replies.size()+data.replies.size()>200)error="Loaded reply limit exceeded.";}
        if(!error.isEmpty()){p.threadState->setText("Unable to load replies · previous posts retained");p.threadState->setToolTip(safeTip(error));p.moreReplies->setEnabled(true);return;}
        topic->replies+=data.replies;topic->nextReplyCursor=data.nextCursor;weak->showDetail(2,true);
    });
}

void SocialView::renderList(int i){
    auto& p=pages_[i];QString selected;const int row=p.list->currentRow();if(row>=0&&row<p.list->count())selected=p.list->item(row)->data(Qt::UserRole).toString();
    const int scroll=p.list->verticalScrollBar()->value();QSignalBlocker block(p.list);p.list->clear();
    int entryIndex=0;for(const auto& e:p.entries){const auto display=SocialDisplayName(e.authorId,e.author);QString value=i==0?display+" · "+stamp(e.createdUtc)+"\n"+e.body:i==1?e.title+"\n"+metadata(e)+"\n"+e.body.left(210):e.title+"\n"+metadata(e);
        auto* item=new QListWidgetItem(value);item->setData(Qt::UserRole,e.id);item->setData(AuthorRole,display);item->setData(TimeRole,i==0?e.createdUtc.toUTC().toString("HH:mm"):stamp(e.createdUtc));item->setData(BodyRole,e.body);item->setData(TitleRole,e.title);item->setData(CategoryRole,e.category);item->setData(AuthorIdRole,e.authorId);item->setData(RepliesRole,e.replyCount.value_or("unavailable"));item->setData(ActivityRole,e.lastActivity.isValid()?e.lastActivity.toUTC().toString("MM-dd HH:mm 'UTC'"):"unavailable");item->setData(StateRole,e.state.isEmpty()?"unavailable":e.state);
        if(i==0){const auto* previous=entryIndex?&p.entries[entryIndex-1]:nullptr;const bool day=!previous||previous->createdUtc.toUTC().date()!=e.createdUtc.toUTC().date();const bool start=day||previous->authorId!=e.authorId||previous->createdUtc.secsTo(e.createdUtc)>300;item->setData(MessageDayRole,day);item->setData(MessageGroupRole,start);item->setData(MessageDateRole,e.createdUtc);item->setToolTip(safeTip(display+"\n"+e.authorId+"\n"+e.createdUtc.toUTC().toString(Qt::ISODate)));}++entryIndex;
        item->setData(ActivityTimeRole,e.lastActivity);item->setData(LastContributorRole,e.lastContributor);QStringList people,peopleInfo;if(e.contributors)for(const auto& person:*e.contributors){people<<person.name;peopleInfo<<person.name+" · "+person.id;}item->setData(ContributorsRole,people);
        if(i==2){item->setText(value+"\nReplies: "+e.replyCount.value_or("unavailable")+" · Activity: "+item->data(ActivityRole).toString()+" · State: "+item->data(StateRole).toString());item->setToolTip(safeTip(item->text()+"\nRecent contributors: "+peopleInfo.join(", ")+"\nLast contributor: "+e.lastContributor));item->setData(Qt::AccessibleDescriptionRole,"Enter reads the topic. Menu or Shift+F10 opens its actions.");}
        p.list->addItem(item);if(e.id==selected)p.list->setCurrentItem(item);}
    p.list->verticalScrollBar()->setValue(scroll);filter(i);
}
void SocialView::filter(int i){
    auto& p=pages_[i];const auto query=p.search->text();int visible=0;
    if(i==3){for(int row=0;row<p.ranking->rowCount();++row){bool match=false;for(int col=0;col<4;++col)match|=p.ranking->item(row,col)->text().contains(query,Qt::CaseInsensitive);p.ranking->setRowHidden(row,!match);if(match)++visible;}}
    else for(int row=0;row<p.entries.size();++row){const auto& e=p.entries[row];const bool match=(i!=2||e.category==forumCategories_->currentItem()->text())&&(e.title+" "+e.author+" "+e.body+" "+e.reference).contains(query,Qt::CaseInsensitive);p.list->item(row)->setHidden(!match);if(match)++visible;}
    const int total=i==3?p.miners.size():p.entries.size();
    p.dataSlot->setCurrentIndex(visible?0:1);
    if(!visible&&total)p.empty->setText(i==2?"No topics match this category or filter.":"No matches in loaded "+QString(nouns[i])+". Change the filter.");
    else if(!visible&&p.loaded)p.empty->setText("No "+QString(nouns[i])+" in this snapshot.");
    p.count->setText(p.loaded?QString("%1 of %2 %3 loaded").arg(visible).arg(total).arg(nouns[i]):QString{});
    if(i==3&&p.loaded)p.count->setText(p.count->text()+" · Sorted by "+p.ranking->horizontalHeaderItem(sortColumn_)->text()+(sortDescending_?" (descending)":" (ascending)"));
    p.more->setEnabled(identity_.valid()&&bool(services_.read)&&!p.busy&&!p.nextCursor.isEmpty()&&total<LoadedLimit);
}
void SocialView::refresh(){read(tabs_->currentIndex());}
void SocialView::read(int i,bool more){
    auto& p=pages_[i];const int total=i==3?p.miners.size():p.entries.size();
    if(!identity_.valid()||!services_.read||p.busy||(more&&(p.nextCursor.isEmpty()||total>=LoadedLimit)))return;
    p.busy=true;const auto generation=++p.generation;p.refresh->setEnabled(false);p.more->setEnabled(false);pageState(i,"loading",more?"Loading more "+QString(nouns[i])+"…":"Loading "+QString(nouns[i])+"…");
    const social::ReadRequest request{static_cast<social::Section>(i),identity_,i==3?static_cast<social::Window>(p.filter->currentIndex()):social::Window::AllTime,more?p.nextCursor:QString{},more?qMin(100,LoadedLimit-total):100,i==0?channel_:QString{}};
    const QPointer<SocialView> weak(this);
    services_.read(request,[weak,i,generation,request,more](social::Snapshot data,QString error){
        if(!weak)return;auto& p=weak->pages_[i];if(p.generation!=generation||!p.busy||weak->identity_!=request.identity)return;
        p.busy=false;p.refresh->setEnabled(true);if(error.isEmpty())error=social::validate(data,request);
         if(error.isEmpty()&&more){QSet<QString> ids;if(i==3){if(p.leaderboardPeriod!=data.leaderboardPeriod)error="Leaderboard anchor or UTC interval changed; refresh the ranking.";for(const auto& item:p.miners)ids.insert(item.id);for(const auto& item:data.miners)if(ids.contains(item.id))error="Page overlaps loaded miner identities.";}
            else{for(const auto& item:p.entries)ids.insert(item.id);for(const auto& item:data.entries)if(ids.contains(item.id))error="Page overlaps loaded records.";}
            if((i==3?p.miners.size()+data.miners.size():p.entries.size()+data.entries.size())>LoadedLimit)error="Loaded data limit reached. Refine the filter or refresh.";}
        if(!error.isEmpty()){if(i==0){p.canSend=false;p.sendWhy=error;weak->access_.reset();weak->updateChatSend();}weak->pageState(i,"error","Unable to load "+QString(nouns[i])+" · "+error.left(130)+(p.loaded?" · previous snapshot retained":" · retry available"),error);
            if(!p.loaded){p.empty->setText("Unable to load "+QString(nouns[i])+". Use Refresh to retry.");p.dataSlot->setCurrentIndex(1);}weak->filter(i);return;}
        weak->present(i,data,more);
    });
}
void SocialView::present(int i,const social::Snapshot& data,bool append){
    if(data.access){const auto error=social::validateAccess(*data.access,identity_);const bool changed=!access_||access_->revision!=data.access->revision||access_->session.reference!=data.access->session.reference||!error.isEmpty();
        if(changed&&accessDialog_)if(auto* dialog=dynamic_cast<SocialAccessDialog*>(accessDialog_.data()))dialog->invalidateAccess(data.access->session,"Permissions or session changed · refresh the authenticated access view.");
        if(error.isEmpty())access_=data.access;else access_.reset();}
    if(i==0){pages_[0].canSend=data.chatSend&&data.chatSend->allowed;pages_[0].sendWhy=data.chatSend?data.chatSend->reason:QString{};if(!append)chatDirectory(data);updateChatSend();}
    auto& p=pages_[i];const bool initial=!p.loaded;p.loaded=true;p.observedUtc=data.observedUtc;p.nextCursor=data.nextCursor;
     if(i==3){p.leaderboardPeriod=data.leaderboardPeriod;
         if(data.leaderboardPeriod&&data.leaderboardPeriod->utcStart){const auto start=QDateTime::fromSecsSinceEpoch(*data.leaderboardPeriod->utcStart,Qt::UTC),end=start.addDays(1);p.windowInfo->setText(start.toString("yyyy-MM-dd")+" → "+end.toString("yyyy-MM-dd")+" · UTC");p.windowInfo->setToolTip("Fixed interval ["+stamp(start)+", "+stamp(end)+"). Canonical block-header time, not physical execution time. Reports: inclusion; audits: first PASSED; validated volume: current qualification episode. Anchor: "+data.leaderboardPeriod->anchor);}
         else{p.windowInfo->clear();p.windowInfo->setToolTip({});}
         if(append)p.miners+=data.miners;else p.miners=data.miners;renderMiners();}
    else{if(append)p.entries+=data.entries;else p.entries=data.entries;
        std::stable_sort(p.entries.begin(),p.entries.end(),[i](const auto& a,const auto& b){const auto x=i==2&&a.lastActivity.isValid()?a.lastActivity:a.createdUtc,y=i==2&&b.lastActivity.isValid()?b.lastActivity:b.createdUtc;return x==y?a.id<b.id:i==0?x<y:x>y;});
        renderList(i);if((p.detail||p.thread)&&!p.selectedId.isEmpty()){if(i==2&&!append){++p.threadGeneration;p.threadBusy=false;}showDetail(i,true);}if(i==0&&initial){p.list->setProperty("pgn_initial_scroll",true);settleInitialChat(p.list);}}
    p.refresh->setEnabled(true);const int total=i==3?p.miners.size():p.entries.size();p.more->setEnabled(!p.nextCursor.isEmpty()&&total<LoadedLimit);
    if(i==0&&data.connection){const auto& state=*data.connection;network_->setText(state.networkLabel);network_->setToolTip(safeTip(identity_.genesis));pageState(i,state.connectedPeers?"loaded":"ready",state.connectedPeers?QString("Connected · %1 peer%2").arg(state.connectedPeers).arg(state.connectedPeers==1?"":"s"):"Waiting for a peer connection");}
    else pageState(i,total?"loaded":"empty",total?"Updated "+data.observedUtc.toUTC().toString("HH:mm")+" UTC":"Empty snapshot · "+data.observedUtc.toUTC().toString("HH:mm")+" UTC");
}
void SocialView::renderMiners(){
    auto& p=pages_[3];auto values=p.miners;const auto decimal=[](const QString& a,const QString& b){return a.size()!=b.size()?(a.size()<b.size()?-1:1):QString::compare(a,b);};
    const auto counter=[](const social::Miner& m,int col)->const social::Counter&{switch(col){case 1:return m.validatedSearchPositions;case 2:return m.ticketsReported;case 3:return m.ticketsValidated;case 4:return m.validatedAudits;default:return m.lastActivityHeight;}};
    std::stable_sort(values.begin(),values.end(),[this,&decimal,&counter](const auto& a,const auto& b){int compare=0;
        if(sortColumn_==0)compare=QString::compare(a.name,b.name,Qt::CaseInsensitive);
        else{const auto& x=counter(a,sortColumn_).value;const auto& y=counter(b,sortColumn_).value;if(bool(x)!=bool(y))return bool(x);if(x)compare=decimal(*x,*y);}
        return compare?(sortDescending_?compare>0:compare<0):a.id<b.id;});
    p.ranking->horizontalHeader()->setSortIndicator(sortColumn_,sortDescending_?Qt::DescendingOrder:Qt::AscendingOrder);
    p.ranking->setRowCount(values.size());
    for(int row=0;row<values.size();++row){const auto& m=values[row];
        for(int col=0;col<6;++col){const auto raw=col?counter(m,col).value.value_or("Unavailable"):m.name;auto* item=new QTableWidgetItem(col?social::leaderboardNumber(raw):raw);if(!col){item->setData(Qt::UserRole,m.id);item->setData(AuthorIdRole,m.socialMemberId);}if(col)item->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);
            QString info;if(!col)info="Miner identity: "+m.id+"\nGenesis: "+identity_.genesis;
            else{const auto& source=counter(m,col).provenance;
                const bool present=bool(counter(m,col).value);
                info=present?"Exact value: "+raw+"\n"+source.source+"\nObserved: "+stamp(source.observedUtc)+"\nGenesis: "+source.genesis:"Unavailable: no authenticated value supplied. No zero inferred.";}
            item->setToolTip(safeTip(info));item->setData(Qt::AccessibleDescriptionRole,info);p.ranking->setItem(row,col,item);}}
    filter(3);
}
void SocialView::formatChat(QString tag){formatDraft(0,std::move(tag));}
void SocialView::formatDraft(int i,QString tag){
    auto* draft=pages_[i].draft;if(!draft->isEnabled())return;
    auto cursor=draft->textCursor();const auto selected=cursor.selectedText().replace(QChar(0x2029),'\n');
    const QString open=social::bbcode::opening(tag),close="[/"+tag+"]";
    const int limit=i==2&&pages_[2].replyMode?4000:social::draftLimit(i);if(draft->toPlainText().size()+open.size()+close.size()>limit){pages_[i].saved->setText("Formatting exceeds the draft source limit");return;}
    const int start=cursor.selectionStart();cursor.beginEditBlock();cursor.insertText(open+selected+close);cursor.endEditBlock();
    cursor.setPosition(start+open.size());if(!selected.isEmpty())cursor.setPosition(start+open.size()+selected.size(),QTextCursor::KeepAnchor);draft->setTextCursor(cursor);
    if(i==0){chatEditor_->setCurrentIndex(0);previewChat_->setChecked(false);}draft->setFocus();
}
void SocialView::previewChat(){
    const bool show=previewChat_->isChecked();if(show){social::bbcode::render(*chatPreview_->document(),pages_[0].draft->toPlainText());chatEditor_->setCurrentIndex(1);chatPreview_->setFocus();}
    else{chatEditor_->setCurrentIndex(0);pages_[0].draft->setFocus();}
}
void SocialView::chatDirectory(const social::Snapshot& data){
    channelTitle_->setProperty("channelId",data.channel);channelTitle_->setText("Conversation");channelDescription_->clear();
    QSignalBlocker guard(channels_);channels_->clear();channelData_=data.channels.value_or(QVector<social::Channel>{});
    for(const auto& channel:channelData_){auto* item=new QListWidgetItem("#  "+channel.name,channels_);item->setData(Qt::UserRole,channel.id);item->setToolTip(safeTip(channel.description));
        if(channel.id==data.channel){channels_->setCurrentItem(item);channelTitle_->setText("# "+channel.name);channelDescription_->setText(channel.description);}}
    channels_->setToolTip(data.channels?"Only channels supplied by the authenticated service.":"Channel directory unavailable.");
    members_->clear();memberCount_->setText(data.participants?QString("Members · %1").arg(data.participants->size()):"Members unavailable");
    if(!data.participants)return;
    auto participants=*data.participants;
    auto group=[this](const social::Participant& person){QString name="Members";int top=-1;if(access_)for(const auto& role:access_->roles)if(person.roleIds.contains(role.id)&&role.position>top){top=role.position;name=role.name;}return QPair<int,QString>{top,name};};
    std::stable_sort(participants.begin(),participants.end(),[&](const auto& a,const auto& b){const auto x=group(a),y=group(b);return x.first!=y.first?x.first>y.first:QString::compare(a.name,b.name,Qt::CaseInsensitive)<0;});
    QString last;for(const auto& person:participants){const auto role=group(person).second;if(role!=last){if(role!="Members"){auto* heading=new QListWidgetItem(role,members_);heading->setFlags(Qt::NoItemFlags);auto font=heading->font();font.setPixelSize(12);font.setWeight(QFont::DemiBold);heading->setFont(font);}last=role;}
        const auto name=SocialDisplayName(person.id,person.name);auto* item=new QListWidgetItem(name+(person.presence.isEmpty()?QString{}:"\n"+person.presence),members_);item->setData(Qt::UserRole,person.id);item->setToolTip(safeTip(name+"\n"+person.id+(person.presence.isEmpty()?QString{}:"\n"+person.presence)));
        QPixmap avatar(32,32);avatar.fill(Qt::transparent);QPainter painter(&avatar);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(Qt::NoPen);painter.setBrush(SocialAvatarColor(person.id));painter.drawEllipse(avatar.rect());painter.setPen(QColor("#193f48"));QFont font("Segoe UI");font.setPixelSize(12);font.setWeight(QFont::DemiBold);painter.setFont(font);painter.drawText(avatar.rect(),Qt::AlignCenter,SocialAvatarInitials(person.id,name));item->setIcon(QIcon(avatar));}
    members_->setIconSize({32,32});
}
void SocialView::updateChatSend(){
    auto& p=pages_[0];if(!p.send)return;const auto source=p.draft->toPlainText();const QString channel=channel_.isEmpty()?channelTitle_->property("channelId").toString():channel_;
    const bool connected=bool(services_.sendChat)&&p.canSend&&identity_.valid()&&!channel.isEmpty()&&!loadFailed_;
    p.send->setEnabled(connected&&!p.sending&&!source.trimmed().isEmpty());
    QString reason="Sending unavailable · no authenticated send service connected.";
    if(services_.sendChat){reason=!p.sendWhy.isEmpty()?p.sendWhy:"Sending unavailable · authenticated availability required.";
        if(connected){reason=p.sendNotice.isEmpty()?"Enter to send · Shift+Enter for a new line":p.sendNotice;
            if(p.sendNotice.isEmpty()&&drafts_.chatOperations.contains(channel)&&drafts_.chatOperations[channel].sourceHash==QString::fromLatin1(QCryptographicHash::hash(source.toUtf8(),QCryptographicHash::Sha256).toHex()))reason="Pending send recovered · Send retries the same operation.";}}
    if(p.sending)reason="Publishing signed message…";
    p.reason->setText(reason.left(256));p.reason->setToolTip(safeTip(reason));
}
void SocialView::sendChat(){
    auto& p=pages_[0];if(!p.send->isEnabled()||!services_.sendChat)return;
    const QString source=p.draft->toPlainText(),channel=channel_.isEmpty()?channelTitle_->property("channelId").toString():channel_;
    const QString hash=QString::fromLatin1(QCryptographicHash::hash(source.toUtf8(),QCryptographicHash::Sha256).toHex());
    if(!drafts_.chatOperations.contains(channel)&&drafts_.chatOperations.size()>=16){p.sendNotice="Pending Chat operation limit reached.";updateChatSend();return;}
    auto& operation=drafts_.chatOperations[channel];if(operation.sourceHash!=hash||operation.requestId.isEmpty())operation={QUuid::createUuid().toString(QUuid::WithoutBraces),hash};
    dirty_=true;if(!saveDrafts()){p.sendNotice="Send paused · pending operation could not be saved.";updateChatSend();return;}
    const social::SendRequest request{identity_,channel,operation.requestId,source};const auto epoch=sendEpoch_;p.sending=true;p.sendNotice.clear();updateChatSend();
    const QPointer<SocialView> weak(this);const auto completed=std::make_shared<bool>(false);
    services_.sendChat(request,[weak,request,epoch,completed](social::SendResult result){
        if(!weak||*completed||weak->sendEpoch_!=epoch||weak->identity_!=request.identity)return;*completed=true;auto& page=weak->pages_[0];page.sending=false;
        if(result.identity!=request.identity||result.channel!=request.channel||result.requestId!=request.requestId){page.canSend=false;page.sendWhy="Send response scope mismatch · draft retained.";weak->updateChatSend();return;}
        if((result.outcome==social::SendOutcome::Accepted||result.outcome==social::SendOutcome::Posted)&&!result.messageId.isEmpty()&&result.messageId.size()<=256){
            if(weak->drafts_.chatOperations.value(request.channel).requestId==request.requestId)weak->drafts_.chatOperations.remove(request.channel);
            const bool unchanged=page.draft->toPlainText()==request.bbcode;if(unchanged)page.draft->clear();weak->dirty_=true;weak->saveDrafts();page.sendNotice=unchanged?(result.reason.isEmpty()?"Sent · confirmation received":result.reason.left(180)):"Previous message published · current draft kept.";
            ++page.generation;page.busy=false;weak->read(0);
        }else{page.sendNotice=result.outcome==social::SendOutcome::Rejected?"Send refused · "+result.reason.left(150):"Publication not confirmed · draft kept. Retry safely. "+result.reason.left(100);
            weak->pageState(0,"error",page.sendNotice);if(result.reason.contains("401")){page.canSend=false;page.sendWhy=page.sendNotice;}}
        weak->updateChatSend();
    });
}
void SocialView::selectChannel(){
    auto* item=channels_->currentItem();if(!item)return;const QString chosen=item->data(Qt::UserRole).toString();if(chosen.isEmpty()||chosen==channel_)return;
    if(dirty_)saveDrafts();++sendEpoch_;channel_=chosen;if(accessDialog_)delete accessDialog_;auto& p=pages_[0];p.canSend=p.sending=false;p.sendNotice.clear();++p.generation;p.loaded=p.busy=false;p.entries.clear();p.list->clear();p.nextCursor.clear();members_->clear();memberCount_->setText("Participants loading…");
    channelTitle_->setText(item->text());loadEditor(0);read(0);
}
void SocialView::openAccess(QString member,QString channel){
    if(accessDialog_){accessDialog_->raise();accessDialog_->activateWindow();return;}
    auto* dialog=new SocialAccessDialog(identity_,access_,services_,channel.isEmpty()?channelTitle_->property("channelId").toString():channel,member,this);dialog->setAttribute(Qt::WA_DeleteOnClose);accessDialog_=dialog;dialog->show();
}
void SocialView::contextMenu(int i,const QPoint& pos,bool reading,QString post){
    auto& p=pages_[i];QString content,id,member,parent;bool replyTarget=false;QWidget* target=p.list?static_cast<QWidget*>(p.list):static_cast<QWidget*>(p.ranking);QPoint point=pos;
    if(i==3){auto* item=p.ranking->itemAt(pos);if(!item)item=p.ranking->currentItem();if(!item)return;const int row=item->row();content=p.ranking->item(row,0)->text();id=p.ranking->item(row,0)->data(Qt::UserRole).toString();member=p.ranking->item(row,0)->data(AuthorIdRole).toString();}
    else if(i==2&&reading){target=p.thread;id=p.selectedId;const auto selected=post.isEmpty()?p.thread->selectedPost():post;for(const auto& entry:p.entries)if(entry.id==id){content=entry.body;member=entry.authorId;if(!selected.isEmpty()&&selected!=id)for(const auto& reply:entry.replies)if(reply.id==selected){parent=id;id=reply.id;content=reply.body;member=reply.authorId;replyTarget=true;}}}
    else{auto* item=reading?p.list->currentItem():p.list->itemAt(pos);if(!item)item=p.list->currentItem();if(!item)return;id=item->data(Qt::UserRole).toString();content=item->data(BodyRole).toString();member=item->data(AuthorIdRole).toString();if(reading)target=p.detail;}
    auto* menu=new QMenu(this);menu->setObjectName("pgn-social-context-"+QString::number(i));menu->setProperty("socialSurface",true);
    auto* copy=menu->addAction(i==3?"Copy miner name":"Copy source text");connect(copy,&QAction::triggered,this,[content]{QApplication::clipboard()->setText(content);});
    auto* copyId=menu->addAction(i==3?"Copy public miner ID":"Copy item ID");connect(copyId,&QAction::triggered,this,[id]{QApplication::clipboard()->setText(id);});
    if(!member.isEmpty()){menu->addSeparator();auto* memberAction=menu->addAction("Roles & member actions…");connect(memberAction,&QAction::triggered,this,[this,member]{openAccess(member);});}
    if(i<3){menu->addSeparator();for(const auto action:{social::AccessAction::RemoveContent,social::AccessAction::PinContent}){
        social::AccessRequest r;r.identity=identity_;r.community=access_?access_->community:QString{};r.revision=access_?access_->revision:QString{};r.sessionReference=access_?access_->session.reference:QString{};r.channel=i==0?channelTitle_->property("channelId").toString():QString{};r.action=action;r.target=id;
        r.parent=parent;r.scope=static_cast<social::AccessScope>(i+1);const QString noun=i==0?"message":i==1?"publication":replyTarget?"reply":"topic";auto* entry=menu->addAction((action==social::AccessAction::RemoveContent?"Remove ":"Pin ")+noun);const auto error=social::unavailable(access_?&*access_:nullptr,r,bool(services_.manage));entry->setEnabled(error.isEmpty());entry->setToolTip(safeTip(error));connect(entry,&QAction::triggered,this,[this,r]{manageContext(r);});}
        if(!services_.manage){auto* reason=menu->addAction("Moderation unavailable · service not connected");reason->setEnabled(false);}
    }
    menu->setToolTipsVisible(true);connect(menu,&QMenu::aboutToHide,menu,&QObject::deleteLater);
    if(point.x()<0||point.y()<0)point={24,24};menu->popup(target->mapToGlobal(point));
}
void SocialView::manageContext(social::AccessRequest r){
    const auto error=social::unavailable(access_?&*access_:nullptr,r,bool(services_.manage));if(!error.isEmpty()){pageState(tabs_->currentIndex(),"denied","Denied · "+error);return;}
    r.requestId=QUuid::createUuid().toString(QUuid::WithoutBraces);const QPointer<SocialView> weak(this);const int section=tabs_->currentIndex();const auto generation=pages_[section].generation,threadGeneration=pages_[section].threadGeneration;
    const auto completed=std::make_shared<bool>(false);services_.manage(r,[weak,r,section,generation,threadGeneration,completed](social::AccessResult result){if(!weak||*completed||weak->identity_!=r.identity||weak->pages_[section].generation!=generation||(section==2&&weak->pages_[section].threadGeneration!=threadGeneration))return;*completed=true;
        if(!social::matchingResult(result,r)){weak->pageState(section,"denied","Denied · response scope mismatch");return;}
        if(result.outcome==social::AccessOutcome::Accepted){weak->access_.reset();weak->pageState(section,"ready","Accepted by service · refresh to read current state");}
        else weak->pageState(section,"denied","Denied · "+result.reason.left(256));});
}

bool SocialView::eventFilter(QObject* object,QEvent* event){
    if(object==pages_[0].draft&&event->type()==QEvent::InputMethod)chatPreedit_=!static_cast<QInputMethodEvent*>(event)->preeditString().isEmpty();
    if(object==pages_[0].draft&&event->type()==QEvent::KeyPress){auto* key=static_cast<QKeyEvent*>(event);if((key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter)&&!chatPreedit_&&!(key->modifiers()&(Qt::ShiftModifier|Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier))){if(pages_[0].send->isEnabled())sendChat();return true;}}
    if(pages_[2].list&&object==pages_[2].list->viewport()&&event->type()==QEvent::MouseButtonRelease){auto* mouse=static_cast<QMouseEvent*>(event);if(mouse->button()==Qt::LeftButton&&mouse->position().x()>=pages_[2].list->viewport()->width()-34){const auto pos=mouse->position().toPoint();if(auto* item=pages_[2].list->itemAt(pos)){pages_[2].list->setCurrentItem(item);contextMenu(2,pos);return true;}}}
    if(object==pages_[0].list&&(event->type()==QEvent::Show||event->type()==QEvent::Resize)
        &&pages_[0].list->property("pgn_initial_scroll").toBool())settleInitialChat(pages_[0].list);
    return QWidget::eventFilter(object,event);
}
}
