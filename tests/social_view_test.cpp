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
using namespace pgn;
static void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
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
    return {{"missing","Synthetic unavailable",{},{},{}},
        {"zero","Synthetic explicit zero",{QString("0"),provenance()},{QString("12"),provenance()},{}},
        {"large","Synthetic accepted Gold",{QString("9007199254740993"),provenance()},{},{QDateTime::currentDateTimeUtc().addSecs(-3600),provenance()}}};
}
static void key(QWidget* view,int code){QKeyEvent e(QEvent::KeyPress,code,Qt::NoModifier);QApplication::sendEvent(view,&e);QKeyEvent up(QEvent::KeyRelease,code,Qt::NoModifier);QApplication::sendEvent(view,&up);}
static void activate(QWidget& view,int section,int row){
    auto* list=find<QListWidget>(view,QString("pgn-social-list-%1").arg(section));list->setCurrentRow(row);key(list,Qt::Key_Return);QApplication::processEvents();
}
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
    }else if(request.section==social::Section::Publications){
        const QStringList titles{"Reading an audit receipt","A reproducible research note","Describing a retained gap","Comparing observation windows","Keeping ticket context","Reviewing a publication draft","Useful details for support"};
        const QStringList bodies{
            "An audit discussion is easier to follow when the observation, its source and the verification result are described separately.",
            "This note lays out the question, the method and the evidence needed for another reader to reproduce the comparison.",
            "A retained gap deserves an exact reference and enough context to explain what was observed and what remains to verify.",
            "Two snapshots can describe different windows. This example keeps that boundary explicit before any numerical comparison.",
            "A ticket reference connects a publication to the work being discussed without turning the draft itself into a proof.",
            "A readable draft starts with a clear title and preserves the important facts. The preview lets its author check the full text.",
            "A support report can include its time, network context and relevant public reference. Credentials never belong in the post."
        };
        for(int i=0;i<titles.size();++i){auto e=entry(QString("publication-%1").arg(i),titles[i],i*5400);e.author="Review author "+QString(QChar('A'+i%3));e.body=bodies[i];if(i==0)e.reference="PREVIEW-TICKET-1042 / PREVIEW-GOLD-27";s.entries.append(e);}
    }else if(request.section==social::Section::Forum){
        auto topic=entry("general","What makes a useful audit discussion?",900,"General");topic.author="Review author A";
        topic.body="A discussion should make the context clear before comparing results.\n\nWhich details help a reader understand an audit observation without mistaking it for an accepted result?";
        topic.replies={{"reply-a","Review author B","I would start with the network and the observation time, then add the public ticket reference.",QDateTime::currentDateTimeUtc().addSecs(-720)},
            {"reply-b","Review author C","The method belongs next to the evidence. A missing value should stay unavailable instead of becoming a numerical claim.",QDateTime::currentDateTimeUtc().addSecs(-480)},
            {"reply-c","Review author A","Agreed. I will keep those points in the publication draft and link back to the relevant context.",QDateTime::currentDateTimeUtc().addSecs(-240)}};
        s.entries={topic};const QStringList titles{"When to open a support topic","Keeping a research question focused","Public references and useful context","How to describe a partial snapshot","Following up on an open question","Separating observations from conclusions"};
        for(int i=0;i<titles.size();++i){auto e=entry(QString("general-%1").arg(i),titles[i],1800+i*300,"General");e.author="Review author "+QString(QChar('A'+i%3));e.body="A review topic with a clear question and space for a contextual reply.";s.entries.append(e);}
        auto mining=entry("mining","Reading a miner snapshot",1200,"Mining");mining.author="Review author B";mining.body="Which observation window and source were used for this snapshot?";s.entries.append(mining);
        auto research=entry("research","Reproducing a comparison",2100,"Research");research.author="Review author C";research.body="Describe the method and the exact evidence before comparing the outcomes.";s.entries.append(research);
        auto support=entry("support","Preparing a useful support report",2400,"Support");support.author="Review author A";support.body="Keep public context and the observed error; exclude credentials.";s.entries.append(support);
    }else{
        for(int i=0;i<12;++i){social::Miner m;m.id=QString("review-miner-%1").arg(i);m.name="Review miner "+QString(QChar('A'+i));
            if(i!=10)m.acceptedGold={QString::number(i==11?0:36-i*3),provenance()};if(i!=4&&i!=10)m.validatedAudits={QString::number(196-i*11),provenance()};
            if(i!=10&&i!=11)m.lastActivity={QDateTime::currentDateTimeUtc().addSecs(-300-i*720),provenance()};s.miners.append(m);}
    }
    for(auto& e:s.entries){e.authorId=e.author.endsWith(" B")?"review-b":e.author.endsWith(" C")?"review-c":"review-a";
        if(request.section==social::Section::Forum){if(e.replies.isEmpty())e.replies={{e.id+"-a","Review author B","Which part of the observation should we check first?",e.createdUtc.addSecs(60),"review-b"},{e.id+"-b","Review author C","Keep the method and the public reference together so another reader can follow the discussion.",e.createdUtc.addSecs(120),"review-c"}};
            e.replyCount=QString::number(e.replies.size());e.lastActivity=e.replies.last().createdUtc;e.lastContributor=e.replies.last().author;e.state="Open";e.bbcode=true;e.reactions=QVector<social::Reaction>{{"like","Like",QString("3"),false}};
            for(int j=0;j<e.replies.size();++j){auto& reply=e.replies[j];reply.authorId=reply.author.endsWith(" B")?"review-b":reply.author.endsWith(" C")?"review-c":"review-a";reply.bbcode=true;reply.reactions=QVector<social::Reaction>{{"like","Like",QString::number(j+1),false}};}}
        if(request.section==social::Section::Forum){QVector<social::Contributor> people;QSet<QString> seen;for(auto it=e.replies.crbegin();it!=e.replies.crend();++it)if(!seen.contains(it->authorId)){people.append({it->authorId,it->author});seen.insert(it->authorId);}if(!seen.contains(e.authorId))people.append({e.authorId,e.author});e.contributors=people;}
    }
    for(auto& m:s.miners)m.socialMemberId="review-a";
    return s;
}
int main(int argc,char** argv){
 try{
    if(!test::prepareOffscreen())return 2;QApplication app(argc,argv);app.setFont(QFont("Segoe UI",10));
    QTemporaryDir temp;check(temp.isValid(),"No temporary draft directory");
    test::v4::run(app,temp.path()+"/v4-contract");
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
        auto* tabs=find<QTabWidget>(local,"pgn-social-sections");for(int i=0;i<4;++i)check(tabs->tabText(i)==QStringList({"Chat","Publications","Forum","Miners"})[i],"Incorrect subtab name");
        for(auto* label:local.findChildren<QLabel*>())check(!label->text().contains("People, ideas")&&!label->text().contains("SOCIAL / COMMUNITY"),"Hero or marketing header remains");
        check(!find<QPlainTextEdit>(local,"pgn-social-draft-body-0")->isEnabled(),"Unscoped draft allowed");
        local.setIdentity(A());auto* body=find<QPlainTextEdit>(local,"pgn-social-draft-body-0");body->setPlainText("Draft A");
        local.setIdentity(B());check(body->toPlainText().isEmpty(),"Identity switch leaked draft");body->setPlainText("Draft B");
        local.setIdentity(C());check(body->toPlainText().isEmpty(),"Genesis switch leaked draft");body->setPlainText("Draft C");
        local.setIdentity(A());check(body->toPlainText()=="Draft A","Scoped draft not restored");
        body->setPlainText(QString(3999,'x')+QString::fromUtf8("🌌"));check(body->toPlainText().size()==3999,"Limit split Unicode surrogate");
        body->setPlainText("Close flush");auto* nav=local.findChild<QFrame*>("horizonNavigation");check(nav&&nav->height()==58,"Navigation diverges from Horizon height");
        const auto buttons=nav->findChildren<QPushButton*>();buttons[0]->setFocus();key(buttons[0],Qt::Key_Right);check(tabs->currentIndex()==1,"Arrow navigation failed");
        buttons[0]->setFocus();key(buttons[0],Qt::Key_Space);key(buttons[0],Qt::Key_Tab);check(QApplication::focusWidget()!=buttons[0],"Hidden tab proxy traps focus");
        body->setFocus();key(body,Qt::Key_Tab);check(QApplication::focusWidget()!=body,"Composer traps Tab");
        fullyVisible(local,find<QPushButton>(local,"pgn-social-send-0"),"Chat Send cut at 1280x720");
        fullyVisible(local,find<QPushButton>(local,"pgn-social-save-0"),"Chat Save cut at 1280x720");
        fullyVisible(local,find<QLabel>(local,"pgn-social-send-reason-0"),"Disabled Send reason cut");
        for(int i=0;i<3;++i)check(!find<QPushButton>(local,QString("pgn-social-send-%1").arg(i))->isEnabled(),"Unconnected send enabled");
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
    view->selectSection(social::Section::Publications);auto pub=snapshot(pending.last().request);pub.entries={entry("older","Older",90),entry("newer","Newer",10)};pub.entries[1].reference="TEST-TICKET / TEST-GOLD";pending.last().reply(pub,{});
    auto* pubJourney=find<QStackedWidget>(*view,"pgn-social-journey-1");auto* feed=find<QListWidget>(*view,"pgn-social-list-1");
    check(pubJourney->currentIndex()==0&&feed->item(0)->text().startsWith("Newer"),"Publication feed or chronological order failed");
    activate(*view,1,0);check(pubJourney->currentIndex()==1&&find<QTextBrowser>(*view,"pgn-social-detail-1")->toPlainText().contains("TEST-GOLD"),"Full publication read/reference missing");
    find<QPushButton>(*view,"pgn-social-back-read-1")->click();find<QPushButton>(*view,"pgn-social-new-1")->click();check(pubJourney->currentIndex()==2,"New publication did not open its own composer");
    find<QLineEdit>(*view,"pgn-social-draft-title-1")->setText("Local title");find<QPlainTextEdit>(*view,"pgn-social-draft-body-1")->setPlainText("Local <script> draft");find<QLineEdit>(*view,"pgn-social-publication-reference")->setText("LOCAL-GOLD");
    find<QPushButton>(*view,"pgn-social-preview-publication")->click();check(pubJourney->currentIndex()==3&&find<QTextBrowser>(*view,"pgn-social-publication-preview")->toPlainText().contains("Local <script> draft"),"Safe local publication preview failed");
    find<QPushButton>(*view,"pgn-social-back-preview")->click();check(pubJourney->currentIndex()==2&&view->saveDrafts(),"Preview did not return to saved draft");
    fullyVisible(*view,find<QPushButton>(*view,"pgn-social-send-1"),"Publish cut in dedicated composer");
    fullyVisible(*view,find<QLabel>(*view,"pgn-social-send-reason-1"),"Publish disabled reason cut");
    key(find<QPlainTextEdit>(*view,"pgn-social-draft-body-1"),Qt::Key_Escape);check(pubJourney->currentIndex()==0,"Escape did not return from composer");view->refresh();auto duplicate=pub;duplicate.entries.append(pub.entries[0]);pending.last().reply(duplicate,{});
    check(feed->count()==2&&view->property("pgn_social_state_1")=="error","Malformed refresh erased valid feed");

    view->selectSection(social::Section::Forum);auto forum=snapshot(pending.last().request);auto general=entry("general","General topic",10,"General");
    general.replies={{"reply","<b>Reply author</b>","Reply <img> literal",QDateTime::currentDateTimeUtc()}};forum.entries={general,entry("mining","Mining topic",20,"Mining")};pending.last().reply(forum,{});
    auto* category=find<QListWidget>(*view,"pgn-social-forum-categories");for(int i=0;i<4;++i)check(category->item(i)->text()==social::categories()[i],"Forum category changed");
    auto* forumJourney=find<QStackedWidget>(*view,"pgn-social-journey-2");activate(*view,2,0);
    check(forumJourney->currentIndex()==1&&find<QListWidget>(*view,"pgn-social-forum-thread")->count()==2&&find<QTextBrowser>(*view,"pgn-social-forum-body-reply")->toPlainText().contains("Reply <img> literal"),"Forum posts are not distinct or safe");
    const auto beforeForumResize=view->size();for(const QSize size:{QSize(1366,950),QSize(1920,1030)}){view->resize(size);app.processEvents();check(find<QTextBrowser>(*view,"pgn-social-forum-body-reply")->height()<=180,"Short forum reply expands after native window resize");}view->resize(beforeForumResize);app.processEvents();
    find<QPushButton>(*view,"pgn-social-reply-topic")->click();check(forumJourney->currentIndex()==2,"Reply did not open dedicated composer");
    auto* reply=find<QPlainTextEdit>(*view,"pgn-social-draft-body-2");reply->setPlainText("General reply draft");check(view->saveDrafts(),"Reply draft not saved");
    fullyVisible(*view,find<QPushButton>(*view,"pgn-social-send-2"),"Reply action cut at 1280x720");
    find<QPushButton>(*view,"pgn-social-back-compose-2")->click();find<QPushButton>(*view,"pgn-social-back-read-2")->click();
    category->setCurrentRow(1);activate(*view,2,1);find<QPushButton>(*view,"pgn-social-reply-topic")->click();check(reply->toPlainText().isEmpty(),"Reply draft crossed topics");reply->setPlainText("Mining reply draft");
    find<QPushButton>(*view,"pgn-social-back-compose-2")->click();find<QPushButton>(*view,"pgn-social-back-read-2")->click();category->setCurrentRow(0);activate(*view,2,0);find<QPushButton>(*view,"pgn-social-reply-topic")->click();
    check(reply->toPlainText()=="General reply draft","Contextual reply draft not restored");
    find<QPushButton>(*view,"pgn-social-back-compose-2")->click();find<QPushButton>(*view,"pgn-social-back-read-2")->click();find<QPushButton>(*view,"pgn-social-new-2")->click();
    find<QLineEdit>(*view,"pgn-social-draft-title-2")->setText("Research draft");find<QComboBox>(*view,"pgn-social-topic-category")->setCurrentText("Research");reply->setPlainText("A new topic draft");check(view->saveDrafts(),"Topic draft save failed");
    fullyVisible(*view,find<QPushButton>(*view,"pgn-social-save-2"),"Topic Save cut");
    fullyVisible(*view,find<QLabel>(*view,"pgn-social-send-reason-2"),"Topic disabled reason cut");

    view->selectSection(social::Section::Miners);auto data=snapshot(pending.last().request);data.miners=miners();data.miners[2].acceptedGold.provenance.source="<img src='not-loaded'>";pending.last().reply(data,{});
    auto* table=find<QTableWidget>(*view,"pgn-social-miners");const QStringList columns{"Miner","Accepted Gold","Validated audits","Last activity"};for(int i=0;i<4;++i)check(table->horizontalHeaderItem(i)->text()==columns[i],"Wrong miner columns");
    check(table->item(0,1)->text()=="9007199254740993"&&table->item(1,1)->text()=="0"&&table->item(2,1)->text()=="unavailable","Default Gold sort or absent/zero distinction failed");
    check((table->item(0,1)->textAlignment()&Qt::AlignRight)&&table->item(0,1)->data(Qt::AccessibleDescriptionRole).toString().contains(A().genesis),"Numeric alignment or accessible provenance missing");
    check(table->item(0,1)->toolTip().contains("&lt;img")&&!table->item(0,1)->toolTip().contains("<img"),"External tooltip interpreted as rich content");
    check(find<QLabel>(*view,"pgn-social-loaded-3")->text().contains("Sorted by Accepted Gold (descending)"),"Default Gold sort is not explicit");
    auto* sort=find<QHeaderView>(*view,"pgn-social-miner-sort-header");auto* refreshMiners=find<QPushButton>(*view,"pgn-social-refresh-3");refreshMiners->setFocus();key(refreshMiners,Qt::Key_Tab);app.processEvents();check(sort->hasFocus(),"Sort header is not reachable with Tab");key(sort,Qt::Key_Home);key(sort,Qt::Key_Right);key(sort,Qt::Key_Return);
    check(find<QLabel>(*view,"pgn-social-loaded-3")->text().contains("Sorted by Accepted Gold (ascending)"),"Changed sort order is not explicit");
    check(table->item(0,1)->text()=="0"&&table->item(2,1)->text()=="unavailable","Keyboard Gold sort failed or placed unknown first");
    auto badMetric=data;badMetric.miners[2].acceptedGold.provenance.genesis=C().genesis;check(!social::validate(badMetric,pending.last().request).isEmpty(),"Wrong-genesis value accepted");
    badMetric=data;badMetric.miners[2].acceptedGold.provenance.authenticated=false;check(!social::validate(badMetric,pending.last().request).isEmpty(),"Unauthenticated metric accepted");
    view->refresh();const auto oldWindow=pending.last();find<QComboBox>(*view,"pgn-social-miner-window")->setCurrentIndex(1);check(pending.last().request.window==social::Window::Day,"Window not announced in request");
    oldWindow.reply(data,{});check(table->rowCount()==0,"Late old-window page accepted");auto day=snapshot(pending.last().request);day.miners=miners();pending.last().reply(day,{});
    view->refresh();pending.last().reply({},"Source unavailable");check(table->rowCount()==3&&view->property("pgn_social_state_3")=="error","Error erased known snapshot");
    view->refresh();pending.last().reply(snapshot(pending.last().request),{});check(table->rowCount()==0&&view->property("pgn_social_state_3")=="empty","Empty response confused with offline/error");
    view->setIdentity({});check(table->rowCount()==0&&messages->count()==0&&view->property("pgn_social_state_3")=="offline","Signout did not clear data/state");
    view->setIdentity(A());const auto late=pending.last();delete view;late.reply(snapshot(late.request),{});
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

    if(argc==3&&(QString::fromLocal8Bit(argv[1])=="--render"||QString::fromLocal8Bit(argv[1])=="--render-forum")){
        const bool forumOnly=QString::fromLocal8Bit(argv[1])=="--render-forum";
        const QString output=QString::fromLocal8Bit(argv[2]);check(QDir().mkpath(output),"No capture output directory");
        QWidget shell;shell.setStyleSheet("QWidget{background:#010b10;color:#c9eee7;}"+HorizonWorkspaceControlStyle());
        auto* layout=new QVBoxLayout(&shell);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
        auto* top=new QFrame;top->setObjectName("horizonCoinNavigation");top->setStyleSheet(HorizonPrimaryNavigationStyle());top->setFixedHeight(43);auto* bar=new QHBoxLayout(top);bar->setContentsMargins(14,5,14,5);
        int primary=0;for(const auto& label:{"PGN\nPRIME GAP SEARCH","WALLET\nPGN CORE CLIENT","SOCIAL\nCOMMUNITY SPACE"}){auto* button=new QPushButton(label);button->setCheckable(true);button->setChecked(QString(label).startsWith("SOCIAL"));button->setFixedSize(QList<int>{260,210,160}[primary++],33);bar->addWidget(button);}
        auto* indicator=new QLabel("REVISION PREVIEW · synthetic fixtures · no network");indicator->setStyleSheet("font:11px 'Segoe UI';color:#e6bd64;");bar->addWidget(indicator,1);layout->addWidget(top);
        social::Services fixtures{[](auto request,auto done){done(previewData(request),{});}};auto* socialView=new ui::SocialView(temp.path()+"/review/drafts.json",fixtures);layout->addWidget(socialView,1);
        auto identity=A();identity.subject="Review author A";socialView->setIdentity(identity);
        for(const QSize size:{QSize(1366,950),QSize(1920,1030)}){
            shell.resize(size);shell.show();app.processEvents();check(shell.size()==size,"Page forces oversized shell");
            for(int i=0;i<4;++i){
                if(forumOnly&&i!=2)continue;
                socialView->selectSection(static_cast<social::Section>(i));app.processEvents();
                if(i==0){find<QPlainTextEdit>(*socialView,"pgn-social-draft-body-0")->setPlainText("I will keep the exact observation window in the draft before sharing it.");find<QPushButton>(*socialView,"pgn-social-save-0")->click();}
                if(i==1){auto* journey=find<QStackedWidget>(*socialView,"pgn-social-journey-1");
                    find<QPushButton>(*socialView,"pgn-social-new-1")->click();fullyVisible(shell,find<QPushButton>(*socialView,"pgn-social-send-1"),"Publication composer action clipped");
                    fullyVisible(shell,find<QPushButton>(*socialView,"pgn-social-preview-publication"),"Preview action clipped");fullyVisible(shell,find<QLabel>(*socialView,"pgn-social-send-reason-1"),"Publication reason clipped");
                    find<QPushButton>(*socialView,"pgn-social-back-compose-1")->click();check(journey->currentIndex()==0,"Publication cannot return to feed");}
                if(i==2){find<QPushButton>(*socialView,"pgn-social-back-read-2")->click();activate(*socialView,2,0);fullyVisible(shell,find<QPushButton>(*socialView,"pgn-social-reply-topic"),"Forum reply action clipped");
                    find<QPushButton>(*socialView,"pgn-social-reply-topic")->click();fullyVisible(shell,find<QPushButton>(*socialView,"pgn-social-send-2"),"Forum compose action clipped");
                    fullyVisible(shell,find<QLabel>(*socialView,"pgn-social-send-reason-2"),"Forum reason clipped");find<QPushButton>(*socialView,"pgn-social-back-compose-2")->click();if(!forumOnly)find<QPushButton>(*socialView,"pgn-social-back-read-2")->click();}
                if(i==0){fullyVisible(shell,find<QPushButton>(*socialView,"pgn-social-send-0"),"Chat Send clipped");fullyVisible(shell,find<QPushButton>(*socialView,"pgn-social-save-0"),"Chat Save clipped");fullyVisible(shell,find<QLabel>(*socialView,"pgn-social-send-reason-0"),"Chat reason clipped");}
                fullyVisible(shell,find<QLabel>(*socialView,"pgn-social-status"),"Stable status line clipped");app.processEvents();
                if(forumOnly){const auto posts=socialView->findChildren<QTextBrowser*>(QRegularExpression("pgn-social-forum-body-.*"));check(!posts.isEmpty(),"Forum review has no post bodies");for(auto* post:posts)check(post->height()<=200,"Short review forum post has oversized reader");}
                QImage image(shell.size(),QImage::Format_ARGB32);image.fill(Qt::transparent);shell.render(&image);check(image.save(output+(forumOnly?QString("/forum-thread-%1.png").arg(size.width()):QString("/social-%1-%2.png").arg(size.width()).arg(i))),"Capture not saved");
            }
        }
    }
    std::cout<<"PASS: V4 mixed surfaces and contrast; BBCode source/toolbar/preview/bounds/safe resources; Discord-style channels/participants and scoped drafts; service-authoritative roles and refusal; no hidden superior; identity/revocation/recovery/replay adapter guards; four-section context menus; publication feed/read/write/preview; contextual forum replies; native keyboard/focus; bounded atomic drafts; genesis/provenance/Gold sort; 500-row pagination; offline/loading/empty/error; safe late/destruction\n";
    return 0;
 }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
