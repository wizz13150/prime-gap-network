#include "ui/social_view.h"
#include "ui/render/horizon_workspace_style.h"
#include "ui/render/horizon_primary_navigation.h"
#include "qt_offscreen_fixture.h"
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QHeaderView>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QScrollArea>
#include <iostream>
#include <stdexcept>
#include "social_v4_checks.h"
#include "social_chat_checks.h"
#include "social_leaderboard_utc_checks.h"
#include "social/social_numbers.h"
#include <QClipboard>
using namespace pgn;
static int checks=0;
static void check(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
template<class T>static T* find(QWidget& view,const QString& name){auto* p=view.findChild<T*>(name);check(p,"Missing social control");return p;}
static social::Identity A(){return {QString(64,'a'),"SYNTHETIC-ACCOUNT-A"};}
static social::Identity B(){return {QString(64,'a'),"SYNTHETIC-ACCOUNT-B"};}
static social::Identity C(){return {QString(64,'b'),"SYNTHETIC-ACCOUNT-A"};}
static social::Snapshot snapshot(const social::ReadRequest& request){
    social::Snapshot s;s.identity=request.identity;s.section=request.section;s.window=request.window;
    s.authenticated=true;s.source="SYNTHETIC TEST FIXTURE — NOT NETWORK DATA";s.observedUtc=QDateTime::currentDateTimeUtc();return s;
}
static social::Entry entry(QString id,QString title,int age=0,QString category={}){
    return {id,"<b>Synthetic author</b>",title,"Synthetic text <script>literal only</script>",QDateTime::currentDateTimeUtc().addSecs(-age),category,{},{}};
}
static social::Provenance provenance(){return {A().genesis,"SYNTHETIC TEST FIXTURE — NOT NETWORK DATA",QDateTime::currentDateTimeUtc(),true};}
static QVector<social::Miner> miners(){
    QVector<social::Miner> values;
    for(const auto& id:QStringList{"missing","zero","large","adjacent"}){
        social::Miner m;m.id=id;m.name=id=="missing"?"Synthetic unavailable":id=="zero"?"Synthetic explicit zero":"Synthetic homonym";
        m.socialMemberId="canonical-"+id;
        if(id!="missing"){
            m.validatedSearchPositions={id=="zero"?QString("0"):id=="large"?QString("9007199254740993"):QString("9007199254740992"),provenance()};
            m.ticketsReported={QString("12"),provenance()};m.ticketsValidated={QString("9"),provenance()};
            m.validatedAudits={QString("7"),provenance()};m.lastActivityHeight={QString("16640"),provenance()};
        }
        values.append(m);
    }
    return values;
}
static void key(QWidget* view,int code){QKeyEvent e(QEvent::KeyPress,code,Qt::NoModifier);QApplication::sendEvent(view,&e);QKeyEvent up(QEvent::KeyRelease,code,Qt::NoModifier);QApplication::sendEvent(view,&up);}
static void fullyVisible(QWidget& root,QWidget* widget,const char* why){
    QApplication::processEvents();const QRect rect(widget->mapTo(&root,QPoint(0,0)),widget->size());
    check(widget->isVisible()&&!widget->visibleRegion().isEmpty()&&root.rect().contains(rect),why);
}
static social::AccessSnapshot reviewAccess(const social::Identity& identity){
    social::AccessSnapshot a;a.identity=identity;a.authenticated=true;a.community="synthetic-review-community";a.revision="synthetic-revision-1";a.source="SYNTHETIC REVIEW ONLY";a.observedUtc=QDateTime::currentDateTimeUtc();a.moderatorRoleId="preview-moderator";
    a.roles={{"preview-moderator","Moderators",10,{"VIEW_CHANNEL","SEND_MESSAGES","MANAGE_ROLES","MANAGE_MESSAGES","KICK_MEMBERS","BAN_MEMBERS"},false},{"preview-member","Members",0,{"VIEW_CHANNEL","SEND_MESSAGES"},false}};
    a.permissionCatalog={"VIEW_CHANNEL","SEND_MESSAGES","MANAGE_ROLES","MANAGE_MESSAGES","KICK_MEMBERS","BAN_MEMBERS"};a.grantablePermissions={"VIEW_CHANNEL","SEND_MESSAGES"};
    a.members={{"review-mod-a","Review mod A",{"preview-moderator"}},{"review-mod-b","Review mod B",{"preview-moderator"}},{"review-mod-c","Review mod C",{"preview-moderator"}},{"review-a","Review author A",{"preview-member"}},{"review-b","Review author B",{"preview-member"}},{"review-c","Review author C",{"preview-member"}}};
    a.session.identity=identity;a.session.state=social::AuthState::Verified;a.session.reference="SYNTHETIC-SESSION-NOT-A-TOKEN";a.session.verificationRef="SYNTHETIC-ASSERTION-NOT-CRYPTOGRAPHIC-PROOF";a.session.revocationRevision="preview-1";a.session.expiresUtc=QDateTime::currentDateTimeUtc().addSecs(300);
    a.session.devices={{"synthetic-device","Review device · synthetic",false}};a.session.device="synthetic-device";
    return a;
}

static social::Snapshot previewData(const social::ReadRequest& request){
    auto s=snapshot(request);s.source="Review fixtures";s.access=reviewAccess(request.identity);
    if(request.section==social::Section::Chat){
        s.channel=request.channel.isEmpty()?"preview-general":request.channel;
        s.channels=QVector<social::Channel>{{"preview-general","general","Network discussion"},{"preview-mining","mining","Mining discussions"},{"preview-research","research","Research notes"},{"preview-support","support","Support questions"}};
        s.participants=QVector<social::Participant>{{"review-mod-a","Review mod A","Online",{"preview-moderator"}},{"review-mod-b","Review mod B","Online",{"preview-moderator"}},{"review-mod-c","Review mod C","Away",{"preview-moderator"}},{"review-a","Review author A","Online",{"preview-member"}},{"review-b","Review author B","Online",{"preview-member"}},{"review-c","Review author C","Away",{"preview-member"}}};
        const QStringList messages{
            "Welcome. This channel keeps the discussion tied to the selected network.",
            "Can we include the exact observation window when we discuss a miner's activity?",
            "Yes. The source and observation time matter as much as the displayed count.",
            "I am preparing a short note about reading an audit receipt.\nIt separates the reported observation from a verified result.",
            "Please include the ticket reference in the publication so the discussion keeps its context.",
            "The draft is saved locally. I will review the wording before sharing it.",
            "A missing metric should remain [b]unavailable[/b]. It should not be presented as zero.",
            "[quote]A missing metric should remain unavailable.[/quote]That distinction matters when a snapshot is partial.",
            "The research topic now contains the method and the questions still open.",
            "Thanks. Let's keep any follow-up in the relevant forum category."
        };
        for(int i=0;i<messages.size();++i){auto e=entry(QString("chat-%1").arg(i),{},(messages.size()-i)*150);e.author=i%2?"Review author B":"Review author A";e.body=messages[i];s.entries.append(e);}
    }else if(request.section==social::Section::Miners){
        s.miners=miners();
    }
    for(auto& e:s.entries){e.authorId=e.author.endsWith(" B")?"review-b":"review-a";e.bbcode=true;}

    return s;
}
int main(int argc,char** argv){
 try{
    if(!test::prepareOffscreen())return 2;QApplication app(argc,argv);app.setFont(QFont("Segoe UI",10));
    QTemporaryDir temp;check(temp.isValid(),"No temporary draft directory");
    test::v4::run(app,temp.path()+"/v4-contract");
    test::chat::run(temp.path()+"/send-contract/drafts.json");
    test::utc::run(app,temp.path()+"/utc-contract/drafts.json");
    social::DraftStore store(temp.path()+"/store/drafts.json");social::Drafts d,loaded;
    d.sections[0].body=QString::fromUtf8("Unicode café 🌌");check(store.save(A(),d).isEmpty()&&store.load(A(),loaded).isEmpty()&&loaded.sections[0].body==d.sections[0].body,"Unicode round-trip failed");
    check(store.path(A())!=store.path(B())&&store.path(A())!=store.path(C()),"Draft identity/genesis collision");
    check(store.load(B(),loaded).isEmpty()&&loaded.sections[0].body.isEmpty(),"Cross-identity draft leaked");
    d.sections[0].body=QString(4001,'x');check(!store.save(A(),d).isEmpty()&&store.load(A(),loaded).isEmpty()&&loaded.sections[0].body.startsWith("Unicode"),"Invalid save replaced complete draft");
    social::Drafts escaped;for(int i=0;i<3;++i)escaped.sections[i].body=QString(social::draftLimit(i),QChar(1));
    for(int i=0;i<8;++i)escaped.replies.insert(QString("topic-%1").arg(i),QString(4000,QChar(1)));
    social::DraftStore escapedStore(temp.path()+"/escaped/drafts.json");
    check(escapedStore.save(A(),escaped).isEmpty()&&escapedStore.load(A(),loaded).isEmpty()&&loaded.replies.size()==8,"Escaped JSON exceeds its own reload limit");
    escaped.replies.insert("ninth","no");check(!escapedStore.save(A(),escaped).isEmpty(),"Reply draft limit missing");
    {
        ui::SocialView local(temp.path()+"/local/drafts.json");local.resize(1280,677);local.show();app.processEvents();
        check(local.findChildren<QScrollArea*>().isEmpty(),"A whole-page scroll area remains");
        auto* tabs=find<QTabWidget>(local,"pgn-social-sections");
        check(tabs->count()==4&&tabs->isTabVisible(0)&&tabs->isTabVisible(3)&&!tabs->isTabVisible(1)&&!tabs->isTabVisible(2),"Only Chat/Leaderboard may be visible; preserve dormant pages");
        check(tabs->tabText(0)=="Chat"&&tabs->tabText(3)=="Leaderboard","Incorrect active subtab names");
        for(const auto hidden:{social::Section::Publications,social::Section::Forum}){local.selectSection(hidden);check(tabs->currentIndex()==0,"selectSection reopened a retired page");}
        for(auto* label:local.findChildren<QLabel*>())check(!label->text().contains("People, ideas")&&!label->text().contains("SOCIAL / COMMUNITY"),"Hero or marketing header remains");
        check(!find<QPlainTextEdit>(local,"pgn-social-draft-body-0")->isEnabled(),"Unscoped draft allowed");
        local.setIdentity(A());auto* body=find<QPlainTextEdit>(local,"pgn-social-draft-body-0");body->setPlainText("Draft A");
        local.setIdentity(B());check(body->toPlainText().isEmpty(),"Identity switch leaked draft");body->setPlainText("Draft B");
        local.setIdentity(C());check(body->toPlainText().isEmpty(),"Genesis switch leaked draft");body->setPlainText("Draft C");
        local.setIdentity(A());check(body->toPlainText()=="Draft A","Scoped draft not restored");
        body->setPlainText(QString(3999,'x')+QString::fromUtf8("🌌"));check(body->toPlainText().size()==3999,"Limit split Unicode surrogate");
        body->setPlainText("Close flush");auto* nav=local.findChild<QFrame*>("horizonNavigation");check(nav&&nav->height()==58,"Navigation diverges from Horizon height");
        const auto buttons=nav->findChildren<QPushButton*>();buttons[0]->setFocus();key(buttons[0],Qt::Key_Right);check(tabs->currentIndex()==3,"Arrow navigation did not skip retired sections");
        buttons[0]->setFocus();key(buttons[0],Qt::Key_Space);key(buttons[0],Qt::Key_Tab);check(QApplication::focusWidget()!=buttons[0],"Hidden tab proxy traps focus");
        body->setFocus();key(body,Qt::Key_Tab);check(QApplication::focusWidget()!=body,"Composer traps Tab");
        fullyVisible(local,find<QPushButton>(local,"pgn-social-send-0"),"Chat Send cut at 1280x720");
        fullyVisible(local,find<QPushButton>(local,"pgn-social-save-0"),"Chat Save cut at 1280x720");
        fullyVisible(local,find<QLabel>(local,"pgn-social-send-reason-0"),"Disabled Send reason cut");
        check(!find<QPushButton>(local,"pgn-social-send-0")->isEnabled(),"Unconnected send enabled");
    }
    social::DraftStore localStore(temp.path()+"/local/drafts.json");check(localStore.load(A(),loaded).isEmpty()&&loaded.sections[0].body=="Close flush","Close lost pending autosave");
    social::DraftStore brokenStore(temp.path()+"/broken/drafts.json");QDir().mkpath(QFileInfo(brokenStore.path(A())).absolutePath());QFile bad(brokenStore.path(A()));
    check(bad.open(QIODevice::WriteOnly),"No corrupt fixture");bad.write("{broken");bad.close();
    {ui::SocialView local(temp.path()+"/broken/drafts.json");local.setIdentity(A());check(!local.saveDrafts(),"Corrupt draft file silently overwritten");}
    check(bad.open(QIODevice::ReadOnly)&&bad.readAll()=="{broken","Corrupt file not retained");bad.close();

    struct Pending{social::ReadRequest request;social::Services::Reply reply;};QVector<Pending> pending;
    social::Services services{[&](auto request,auto reply){pending.append({request,std::move(reply)});}};
    auto* view=new ui::SocialView(temp.path()+"/remote/drafts.json",services);view->resize(1280,677);view->show();app.processEvents();
    check(pending.isEmpty(),"Network read inferred without identity");view->setIdentity(A());view->refresh();check(pending.size()==1,"Overlapping reads admitted");
    auto invalid=snapshot(pending.last().request);invalid.authenticated=false;invalid.entries={entry("x",{})};pending.last().reply(invalid,{});
    check(find<QListWidget>(*view,"pgn-social-list-0")->count()==0&&view->property("pgn_social_state_0")=="error","Unauthenticated content accepted");
    view->refresh();const auto old=pending.last();view->setIdentity(C());old.reply(snapshot(old.request),{});
    check(find<QListWidget>(*view,"pgn-social-list-0")->count()==0,"Late wrong-genesis response accepted");
    auto* focus=find<QPlainTextEdit>(*view,"pgn-social-draft-body-0");focus->setFocus();app.processEvents();
    auto chat=snapshot(pending.last().request);chat.entries={entry("new",{},10),entry("old",{},90)};pending.last().reply(chat,{});
    check(QApplication::focusWidget()==focus,"Arriving content stole composer focus");
    auto* messages=find<QListWidget>(*view,"pgn-social-list-0");check(messages->count()==2&&messages->item(0)->text().contains("<script>literal only</script>"),"Chat is not safe plain text");
    check(!view->findChild<QTextBrowser*>("pgn-social-detail-0"),"Chat has a duplicate reading panel");
    view->setIdentity(A());pending.last().reply(snapshot(pending.last().request),{});
    auto validChat=snapshot(pending.last().request);validChat.entries={entry("kept",{},10)};
    view->refresh();pending.last().reply(validChat,{});view->refresh();auto duplicate=validChat;duplicate.entries.append(validChat.entries[0]);pending.last().reply(duplicate,{});
    check(messages->count()==1&&view->property("pgn_social_state_0")=="error","Malformed refresh erased valid chat");
    const int beforeHidden=pending.size();
    for(const auto hidden:{social::Section::Publications,social::Section::Forum})view->selectSection(hidden);
    check(pending.size()==beforeHidden&&find<QTabWidget>(*view,"pgn-social-sections")->currentIndex()==0,"Hidden section started a read");

    view->selectSection(social::Section::Miners);auto data=snapshot(pending.last().request);data.miners=miners();data.miners[2].validatedSearchPositions.provenance.source="<img src='not-loaded'>";pending.last().reply(data,{});
    auto* table=find<QTableWidget>(*view,"pgn-social-miners");
    const QStringList columns{"Miner","Validated positions","Tickets reported","Tickets validated","Audits validated","Last activity · block"};
    check(table->columnCount()==6,"Leaderboard column count changed");for(int i=0;i<columns.size();++i)check(table->horizontalHeaderItem(i)->text()==columns[i],"Wrong leaderboard columns");
    const auto rowId=[&](int row){return table->item(row,0)->data(Qt::UserRole).toString();};
    check(rowId(0)=="large"&&rowId(1)=="adjacent"&&rowId(2)=="zero"&&rowId(3)=="missing","Exact volume sort failed above 2^53 or missing value sorted as zero");
    check(table->item(0,1)->text()==social::leaderboardNumber("9007199254740993")&&table->item(0,1)->text()==table->item(1,1)->text(),"Scientific display fixture does not exercise equal rounded text");
    check(table->item(2,1)->text()=="0"&&table->item(3,1)->text()=="Unavailable","Absent/explicit zero distinction failed");
    check((table->item(0,1)->textAlignment()&Qt::AlignRight)&&table->item(0,1)->data(Qt::AccessibleDescriptionRole).toString().contains(A().genesis)&&table->item(0,1)->toolTip().contains("9007199254740993"),"Numeric alignment or exact accessible provenance missing");
    check(table->item(0,1)->toolTip().contains("&lt;img")&&!table->item(0,1)->toolTip().contains("<img"),"External tooltip interpreted as rich content");
    check(find<QLabel>(*view,"pgn-social-loaded-3")->text().contains("Sorted by Validated positions (descending)"),"Default volume sort is not explicit");
    auto* sort=find<QHeaderView>(*view,"pgn-social-miner-sort-header");auto* refreshMiners=find<QPushButton>(*view,"pgn-social-refresh-3");refreshMiners->setFocus();key(refreshMiners,Qt::Key_Tab);app.processEvents();check(sort->hasFocus(),"Sort header is not reachable with Tab");key(sort,Qt::Key_Home);key(sort,Qt::Key_Right);key(sort,Qt::Key_Return);
    check(find<QLabel>(*view,"pgn-social-loaded-3")->text().contains("Sorted by Validated positions (ascending)"),"Changed sort order is not explicit");
    check(rowId(0)=="zero"&&rowId(1)=="adjacent"&&rowId(2)=="large"&&rowId(3)=="missing","Keyboard exact-volume sort failed or placed unknown first");
    // Equal counters are ordered by canonical operator identity, never homonym.
    key(sort,Qt::Key_Right);key(sort,Qt::Key_Return);check(rowId(0)=="adjacent"&&rowId(1)=="large"&&rowId(2)=="zero","Counter ties are not ordered by canonical identity");
    for(int col=1;col<6;++col){check(table->item(3,col)->text()=="Unavailable","Missing metric became a fabricated zero");}
    for(const auto field:{&social::Miner::validatedSearchPositions,&social::Miner::ticketsReported,&social::Miner::ticketsValidated,&social::Miner::validatedAudits,&social::Miner::lastActivityHeight}){
        auto badMetric=data;(badMetric.miners[2].*field).provenance.genesis=C().genesis;check(!social::validate(badMetric,pending.last().request).isEmpty(),"Wrong-genesis value accepted");
        badMetric=data;(badMetric.miners[2].*field).provenance.authenticated=false;check(!social::validate(badMetric,pending.last().request).isEmpty(),"Unauthenticated metric accepted");
    }
    auto* window=find<QComboBox>(*view,"pgn-social-miner-window");check(window->count()==2&&window->itemText(1)==social::windowLabel(social::Window::Day)&&window->currentText()=="All recorded activity"&&pending.last().request.window==social::Window::AllTime,"Leaderboard observation windows or default changed");
    view->refresh();const auto oldPage=pending.last();view->setIdentity(B());oldPage.reply(data,{});check(table->rowCount()==0,"Late old-identity leaderboard page accepted");
    view->setIdentity(A());auto current=snapshot(pending.last().request);current.miners=miners();pending.last().reply(current,{});
    view->refresh();pending.last().reply({},"Source unavailable");check(table->rowCount()==4&&view->property("pgn_social_state_3")=="error","Error erased known snapshot");
    view->refresh();pending.last().reply(snapshot(pending.last().request),{});check(table->rowCount()==0&&view->property("pgn_social_state_3")=="empty","Empty response confused with offline/error");
    view->setIdentity({});check(table->rowCount()==0&&messages->count()==0&&view->property("pgn_social_state_3")=="offline","Signout did not clear data/state");
    view->setIdentity(A());const auto late=pending.last();delete view;late.reply(snapshot(late.request),{});
    {
        ui::SocialView pages(temp.path()+"/page-refusals/drafts.json",services);pages.setIdentity(A());pages.selectSection(social::Section::Miners);
        auto first=snapshot(pending.last().request);first.miners={miners()[1]};first.nextCursor="page-two";pending.last().reply(first,{});
        auto* table=find<QTableWidget>(pages,"pgn-social-miners");auto* more=find<QPushButton>(pages,"pgn-social-more-3");
        more->click();const auto overlap=pending.last();auto bad=snapshot(overlap.request);bad.miners=first.miners;bad.nextCursor="page-three";overlap.reply(bad,{});
        check(table->rowCount()==1&&pages.property("pgn_social_state_3")=="error","Overlapping page duplicated an operator");
        more->click();const auto loop=pending.last();bad=snapshot(loop.request);bad.nextCursor=loop.request.cursor;loop.reply(bad,{});
        check(table->rowCount()==1&&pages.property("pgn_social_state_3")=="error","Repeated page cursor accepted");
        more->click();const auto next=pending.last();auto good=snapshot(next.request);good.miners={miners()[2]};next.reply(good,{});
        check(table->rowCount()==2,"Valid retry did not append one distinct operator");
        next.reply(first,{});check(table->rowCount()==2,"Duplicate page callback changed loaded rows");
    }
    {
        ui::SocialView paged(temp.path()+"/paged/drafts.json",services);paged.setIdentity(A());paged.selectSection(social::Section::Miners);
        int count=0;
        for(int page=0;page<6;++page){const auto req=pending.last().request;const int amount=page==0?95:page==5?5:100;
            check(req.limit==(page==5?5:100),"Remaining capacity not carried into bounded request");
            auto response=snapshot(req);for(int n=0;n<amount;++n){auto m=miners()[1];m.id=QString("page-miner-%1").arg(count++);response.miners.append(m);}response.nextCursor=QString("cursor-%1").arg(page+1);pending.last().reply(response,{});
            check(find<QTableWidget>(paged,"pgn-social-miners")->rowCount()==count,"Bounded page append failed");
            if(page<5){const int before=pending.size();find<QPushButton>(paged,"pgn-social-more-3")->click();find<QPushButton>(paged,"pgn-social-more-3")->click();check(pending.size()==before+1,"More overlaps reads");}}
        const int before=pending.size();check(count==500&&!find<QPushButton>(paged,"pgn-social-more-3")->isEnabled(),"500-row buffer cap missing");
        find<QPushButton>(paged,"pgn-social-more-3")->click();check(pending.size()==before,"Read exceeded buffer cap");
    }
    if(argc==3&&QString::fromLocal8Bit(argv[1])=="--render-access"){
        const QString output=QString::fromLocal8Bit(argv[2]);check(QDir().mkpath(output),"No access capture directory");
        QWidget frame;frame.setStyleSheet("QWidget{background:#041319;color:#c9eee7;}");auto* layout=new QVBoxLayout(&frame);layout->setContentsMargins(16,16,16,16);
        auto* banner=new QLabel("REVISION PREVIEW · synthetic roles / session · no management or identity service");banner->setStyleSheet("font:12px 'Segoe UI';color:#e6bd64;");layout->addWidget(banner);layout->addStretch();
        auto identity=A();identity.subject="Review author A";auto* access=new ui::SocialAccessDialog(identity,reviewAccess(identity),{},"preview-general",{},&frame);access->setWindowFlags(Qt::Widget);access->setFixedSize(820,530);layout->addWidget(access,0,Qt::AlignHCenter);layout->addStretch();frame.resize(1366,950);frame.show();access->show();app.processEvents();
        auto* pages=access->findChild<QTabWidget*>();check(pages&&pages->count()==4,"Access sections missing");const QStringList names{"roles","members","channel-permissions","account-devices"};
        for(int i=0;i<4;++i){pages->setCurrentIndex(i);app.processEvents();fullyVisible(frame,access,"Access panel clipped");QImage image(frame.size(),QImage::Format_ARGB32);image.fill(Qt::transparent);frame.render(&image);check(image.save(output+"/access-"+names[i]+".png"),"Access capture not saved");}
    }

    if(argc==3&&QString::fromLocal8Bit(argv[1])=="--render"){
        const QString output=QString::fromLocal8Bit(argv[2]);check(QDir().mkpath(output),"No capture output directory");
        QWidget shell;shell.setStyleSheet("QWidget{background:#010b10;color:#c9eee7;}"+HorizonWorkspaceControlStyle());
        auto* layout=new QVBoxLayout(&shell);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
        auto* top=new QFrame;top->setObjectName("horizonCoinNavigation");top->setStyleSheet(HorizonPrimaryNavigationStyle());top->setFixedHeight(43);auto* bar=new QHBoxLayout(top);bar->setContentsMargins(14,5,14,5);
        int primary=0;for(const auto& label:{"PGN\nPRIME GAP SEARCH","WALLET\nPGN CORE CLIENT","SOCIAL\nCOMMUNITY SPACE"}){auto* button=new QPushButton(label);button->setCheckable(true);button->setChecked(QString(label).startsWith("SOCIAL"));button->setFixedSize(QList<int>{260,210,210}[primary++],33);bar->addWidget(button);}
        auto* indicator=new QLabel("REVISION PREVIEW · synthetic fixtures · no network");indicator->setStyleSheet("font:11px 'Segoe UI';color:#e6bd64;");bar->addWidget(indicator,1);layout->addWidget(top);
        social::Services fixtures{[](auto request,auto done){done(previewData(request),{});}};auto* socialView=new ui::SocialView(temp.path()+"/review/drafts.json",fixtures);layout->addWidget(socialView,1);
        auto identity=A();identity.subject="Review author A";socialView->setIdentity(identity);
        for(const QSize size:{QSize(1366,900),QSize(1920,1080)}){
            shell.resize(size);shell.show();app.processEvents();check(shell.size()==size,"Page forces oversized shell");
            for(const int i:{0,3}){
                socialView->selectSection(static_cast<social::Section>(i));app.processEvents();
                if(i==0){find<QPlainTextEdit>(*socialView,"pgn-social-draft-body-0")->setPlainText("I will keep the exact observation window in the draft before sharing it.");find<QPushButton>(*socialView,"pgn-social-save-0")->click();}
                if(i==0){fullyVisible(shell,find<QPushButton>(*socialView,"pgn-social-send-0"),"Chat Send clipped");fullyVisible(shell,find<QPushButton>(*socialView,"pgn-social-save-0"),"Chat Save clipped");fullyVisible(shell,find<QLabel>(*socialView,"pgn-social-send-reason-0"),"Chat reason clipped");}
                fullyVisible(shell,find<QLabel>(*socialView,"pgn-social-status"),"Stable status line clipped");app.processEvents();
                QImage image(shell.size(),QImage::Format_ARGB32);image.fill(Qt::transparent);shell.render(&image);check(image.save(output+QString("/social-%1-%2.png").arg(size.width()).arg(i)),"Capture not saved");
            }
        }
    }
    std::cout<<"PASS: V4 mixed surfaces and contrast; BBCode source/toolbar/preview/bounds/safe resources; Discord-style channels/participants and scoped drafts; service-authoritative roles and refusal; no hidden superior; identity/revocation/recovery/replay adapter guards; Chat/Leaderboard native context menus with canonical targets; retired sections inaccessible; native keyboard/focus; bounded atomic drafts; genesis/provenance/exact volume sort and scientific display; 500-row pagination; offline/loading/empty/error; safe late/destruction; same-operation send retry/restart; no optimistic insert; Posted vs Delivered; wrong-scope/401 refusal\n";
    std::cout<<"CHECKS "<<(checks+test::v4::checks+test::chat::checks+test::utc::checks)<<"\n";
    return 0;
 }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
