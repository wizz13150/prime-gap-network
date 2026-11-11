#pragma once
#include "social/social_access.h"
#include "social/social_bbcode.h"
#include "ui/social_access_dialog.h"
#include "ui/social_entry_delegate.h"
#include <QTextBlock>
#include <QMenu>
#include <QContextMenuEvent>
#include <cmath>
namespace pgn::test::v4 {
inline void expect(bool good,const char* why){if(!good)throw std::runtime_error(why);}
template<class T>inline T* control(QWidget& root,const QString& name){auto* value=root.findChild<T*>(name);expect(value,"Missing V4 control");return value;}
inline social::Identity identity(){return {QString(64,'a'),"SYNTHETIC-PERSISTENT-ACCOUNT"};}
inline social::AccessSnapshot access(){
    social::AccessSnapshot s;s.identity=identity();s.authenticated=true;s.community="SYNTHETIC-COMMUNITY";s.revision="SYNTHETIC-REVISION";s.source="TEST-ONLY";s.observedUtc=QDateTime::currentDateTimeUtc();s.moderatorRoleId="moderator";
    s.roles={{"moderator","Moderator",10,{"MANAGE_ROLES","BAN_MEMBERS"},false},{"member","Member",0,{"VIEW_CHANNEL"},false}};
    s.members={{identity().subject,"Wizz",{"member"}},{"peer","Synthetic moderator",{"moderator"}}};
    s.permissionCatalog={"VIEW_CHANNEL","SEND_MESSAGES","MANAGE_ROLES","BAN_MEMBERS"};s.grantablePermissions={"VIEW_CHANNEL","SEND_MESSAGES"};
    s.session.identity=identity();s.session.state=social::AuthState::Verified;s.session.reference="SYNTHETIC-NONSECRET-REFERENCE";
    s.session.verificationRef="TEST-STATUS-NOT-A-SIGNATURE";s.session.revocationRevision="TEST-REVOCATION-1";s.session.expiresUtc=QDateTime::currentDateTimeUtc().addSecs(300);
    s.session.devices={{"test-device","Synthetic device",false}};s.session.device="test-device";s.session.availableFlows={social::AuthIntent::SignIn,social::AuthIntent::Recover,social::AuthIntent::RevokeDevice};
    return s;
}
inline social::AccessRequest request(const social::AccessSnapshot& s,social::AccessAction action,QString target={},QString role={}){
    social::AccessRequest r;r.identity=s.identity;r.community=s.community;r.revision=s.revision;r.sessionReference=s.session.reference;r.action=action;r.target=target;r.role=role;return r;
}
inline double luminance(QColor c){auto part=[](double v){return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);};return .2126*part(c.redF())+.7152*part(c.greenF())+.0722*part(c.blueF());}
inline double contrast(QColor a,QColor b){const auto x=luminance(a),y=luminance(b);return (qMax(x,y)+.05)/(qMin(x,y)+.05);}
inline void run(QApplication& app,const QString& path){
    using namespace social;
    bbcode::Document document;QFont body("Segoe UI");body.setPixelSize(14);document.setDefaultFont(body);
    for(const auto tag:{"b","i","u","s","quote","code","url"}){
        const QString name=tag;const auto source=bbcode::opening(name)+"text[/"+name+"]";
        expect(bbcode::render(document,source),"Supported BBCode tag was not parsed");const auto cursor=document.find("text");expect(!cursor.isNull(),"BBCode lost visible text");
        const auto format=cursor.charFormat();if(name=="b")expect(format.fontWeight()>=QFont::Bold,"Bold not rendered");
        if(name=="i")expect(format.fontItalic(),"Italic not rendered");if(name=="u")expect(format.fontUnderline(),"Underline not rendered");if(name=="s")expect(format.fontStrikeOut(),"Strike not rendered");
        if(name=="quote")expect(cursor.blockFormat().leftMargin()>0,"Quote not indented");
        if(name=="url")expect(document.toPlainText().contains("https://example.org")&&!format.isAnchor(),"URL activates or hides target");
    }
    expect(bbcode::render(document,"[code][b]<img src='file:///private'>[/b][/code]")&&document.toPlainText()=="[b]<img src='file:///private'>[/b]","Code is not opaque");
    const QString raw="[b]&amp; <script>literal</script>[/b]";expect(bbcode::render(document,raw)&&document.toPlainText()=="&amp; <script>literal</script>","HTML or entities executed/double escaped");
    for(const auto source:QStringList{"[b][i]cross[/b][/i]","[b]open","[url=file:///private]file[/url]","[url=javascript:alert(1)]x[/url]"}){
        expect(!bbcode::render(document,source)&&document.toPlainText()==source,"Malformed/unsafe BBCode not preserved literally");}
    const QString unknown="[img]https://example.org/image.png[/img][unknown]text[/unknown]";expect(bbcode::render(document,unknown)&&document.toPlainText()==unknown,"Unknown tag not readable");
    const QString deep=QString("[b]").repeated(17)+"x"+QString("[/b]").repeated(17);expect(!bbcode::render(document,deep)&&document.toPlainText()==deep,"Depth is unbounded");
    expect(!bbcode::render(document,QString("[x]").repeated(1025)),"Token count unbounded");expect(!bbcode::render(document,QString(4001,'x'))&&document.toPlainText().contains("limit reached"),"Source display limit missing");
    expect(!document.resource(QTextDocument::ImageResource,QUrl("file:///private")).isValid()&&!document.resource(QTextDocument::ImageResource,QUrl("https://example.org/remote")).isValid(),"Implicit resource loading allowed");
    expect(contrast(QColor("#161616"),QColor("#dfe7e7"))>=4.5&&contrast(QColor("#525252"),QColor("#dfe7e7"))>=4.5,"Reading text contrast failed");
    expect(contrast(QColor("#c9eee7"),QColor("#09262d"))>=4.5&&contrast(QColor("#9db6b9"),QColor("#041319"))>=4.5,"Chrome text contrast failed");
    expect(contrast(QColor("#547b82"),QColor("#09262d"))>=3&&contrast(QColor("#6c898d"),QColor("#eaf0ef"))>=3,"Control boundary contrast failed");
    expect(contrast(QColor("#214f53"),QColor("#bdd3d0"))>=4.5&&contrast(QColor("#28585b"),QColor("#d5e2de"))>=4.5&&contrast(QColor("#3a5558"),QColor("#e6edeb"))>=4.5,"Forum badge or metadata contrast failed");
    const auto clock=QDateTime::currentDateTimeUtc();expect(ui::SocialRelativeActivity(clock.addSecs(-300),clock)=="5m ago"&&ui::SocialRelativeActivity({},clock)=="unavailable","Relative activity fabricated a missing value");

    auto s=access();expect(validateAccess(s,identity()).isEmpty(),"Valid synthetic access fixture rejected");
    auto r=request(s,AccessAction::AssignRole,identity().subject,"moderator");expect(!unavailable(&s,r,true).isEmpty(),"Self-grant offered");
    r=request(s,AccessAction::EditRole,{},"moderator");expect(!unavailable(&s,r,true).isEmpty(),"Superior role change offered");
    r=request(s,AccessAction::BanMember,"peer");s.availability.append({AccessAction::BanMember,"peer",{},{},false,"Equal or higher role cannot be moderated."});
    expect(unavailable(&s,r,true).contains("Equal"),"Peer moderation refusal missing");
    auto wrong=r;wrong.identity.genesis=QString(64,'b');expect(!unavailable(&s,wrong,true).isEmpty(),"Network scope escalation accepted");
    auto forbidden=s;forbidden.roles.append({"above","Hidden owner",11,{},false});expect(!validateAccess(forbidden,identity()).isEmpty(),"Hidden role above Moderator accepted");
    auto nameOnly=s;nameOnly.members[0].name="Robert";expect(!unavailable(&nameOnly,request(nameOnly,AccessAction::AssignRole,identity().subject,"moderator"),true).isEmpty(),"Display name granted rights");
    for(const auto state:{AuthState::Unverified,AuthState::RecoveryRequired,AuthState::Revoked,AuthState::Expired}){auto stale=s;stale.session.state=state;expect(!validateAccess(stale,identity()).isEmpty(),"Unverified/revoked session granted access");}
    auto expired=s;expired.session.expiresUtc=QDateTime::currentDateTimeUtc().addSecs(-1);expect(!validateAccess(expired,identity()).isEmpty(),"Expired session granted access");
    auto publicOnly=s;publicOnly.session.verificationRef.clear();expect(!validateAccess(publicOnly,identity()).isEmpty(),"Public address alone granted access");
    auto revokedCredential=s;revokedCredential.session.devices[0].revoked=true;expect(!validateAccess(revokedCredential,identity()).isEmpty(),"Revoked credential retained verified access");
    for(int section=1;section<=4;++section){
        auto scoped=s;auto a=Availability{AccessAction::RemoveContent,"same-id",{},{},true,{}};a.scope=static_cast<AccessScope>(section);scoped.availability.append(a);
        auto operation=request(scoped,AccessAction::RemoveContent,"same-id");operation.scope=static_cast<AccessScope>(section);
        expect(unavailable(&scoped,operation,true).isEmpty(),"Matching section availability lost");
        operation.scope=section==4?AccessScope::Chat:static_cast<AccessScope>(section+1);expect(!unavailable(&scoped,operation,true).isEmpty(),"Cross-section capability escalation accepted");
    }
    auto child=s;Availability childAvailability{AccessAction::RemoveContent,"reply",{},{},true,{}};childAvailability.scope=AccessScope::Forum;childAvailability.parent="topic-a";child.availability.append(childAvailability);
    auto childRequest=request(child,AccessAction::RemoveContent,"reply");childRequest.scope=AccessScope::Forum;childRequest.parent="topic-a";expect(unavailable(&child,childRequest,true).isEmpty(),"Reply parent availability lost");childRequest.parent="topic-b";expect(!unavailable(&child,childRequest,true).isEmpty(),"Reply action escaped its parent topic");
    int mutations=0;Services manage;manage.manage=[&](auto operation,auto done){expect(operation.permissions.contains("VIEW_CHANNEL")&&!operation.permissions.contains("View channel"),"Display label used as canonical permission");++mutations;done(AccessResult{operation.identity,operation.community,operation.channel,operation.requestId,AccessOutcome::Denied,"Current service policy denies this request",{}});};
    s.availability.append({AccessAction::EditRole,{},"member",{},true,{}});
    ui::SocialAccessDialog accessDialog(identity(),s,manage,"general",{});accessDialog.show();app.processEvents();
    auto* roles=control<QListWidget>(accessDialog,"pgn-social-role-list");roles->setCurrentRow(1);control<QLineEdit>(accessDialog,"pgn-social-role-name")->setText("Requested local edit");
    control<QPushButton>(accessDialog,"pgn-social-save-role")->click();expect(mutations==1&&control<QLabel>(accessDialog,"pgn-social-access-status")->text().contains("Denied"),"Service refusal not explicit");
    expect(roles->item(1)->text()=="Member","Client optimistically changed role");accessDialog.close();
    std::function<void(AccessResult)> pendingAction;AccessRequest pendingOperation;Services deferred;deferred.manage=[&](auto operation,auto done){pendingOperation=operation;pendingAction=done;};
    ui::SocialAccessDialog pendingDialog(identity(),s,deferred,"general",{});pendingDialog.show();control<QListWidget>(pendingDialog,"pgn-social-role-list")->setCurrentRow(1);control<QPushButton>(pendingDialog,"pgn-social-save-role")->click();expect(bool(pendingAction),"No pending role action");
    auto changed=s.session;changed.state=AuthState::Revoked;pendingDialog.invalidateAccess(changed,"Session revoked");pendingAction({pendingOperation.identity,pendingOperation.community,pendingOperation.channel,pendingOperation.requestId,AccessOutcome::Accepted,{},{}});
    expect(control<QLabel>(pendingDialog,"pgn-social-access-status")->text()=="Session revoked"&&!control<QPushButton>(pendingDialog,"pgn-social-save-role")->isEnabled(),"Late mutation response restored revoked access");pendingDialog.close();
    QVector<QPair<AuthRequest,std::function<void(AuthResult)>>> authPending;Services auth;auth.authenticate=[&](auto req,auto done){authPending.append({req,done});};
    ui::SocialAccessDialog account(identity(),s,auth,"general",{});account.show();app.processEvents();
    control<QPushButton>(account,"pgn-social-auth-0")->click();expect(authPending.size()==1,"Sign-in adapter not invoked");
    auto first=authPending[0];auto revoked=s.session;revoked.state=AuthState::Revoked;first.second({first.first.identity,first.first.requestId,"Revoked by service",revoked});
    expect(control<QLabel>(account,"pgn-social-auth-status")->text().contains("revoked"),"Revocation not visible");first.second({first.first.identity,first.first.requestId,{},s.session});
    expect(control<QLabel>(account,"pgn-social-auth-status")->text().contains("revoked"),"Duplicate/replayed completion re-elevated UI");
    control<QPushButton>(account,"pgn-social-auth-0")->click();const auto old=authPending.last();control<QPushButton>(account,"pgn-social-auth-2")->click();const auto fresh=authPending.last();
    auto recovery=s.session;recovery.state=AuthState::RecoveryRequired;fresh.second({fresh.first.identity,fresh.first.requestId,{},recovery});old.second({old.first.identity,old.first.requestId,{},s.session});
    expect(control<QLabel>(account,"pgn-social-auth-status")->text().contains("Recovery required"),"Old device flow replaced newer recovery state");account.close();

    ui::SocialView local(path+"/drafts.json");local.resize(1280,677);local.show();local.setIdentity(identity());app.processEvents();
    auto* draft=control<QPlainTextEdit>(local,"pgn-social-draft-body-0");
    for(const auto tag:{"b","i","u","s","quote","code","url"}){draft->setPlainText("text");auto cursor=draft->textCursor();cursor.select(QTextCursor::Document);draft->setTextCursor(cursor);
        control<QPushButton>(local,"pgn-social-bbcode-"+QString(tag))->click();const auto expected=bbcode::opening(tag)+"text[/"+tag+"]";expect(draft->toPlainText()==expected,"Toolbar source differs from manual BBCode");
        control<QPushButton>(local,"pgn-social-preview-chat")->click();bbcode::Document manual;bbcode::render(manual,expected);expect(control<QTextBrowser>(local,"pgn-social-chat-preview")->toPlainText()==manual.toPlainText(),"Preview and reading renderer diverge");
        control<QPushButton>(local,"pgn-social-preview-chat")->click();}
    expect(local.saveDrafts(),"BBCode draft save failed");Drafts saved;expect(DraftStore(path+"/drafts.json").load(identity(),saved).isEmpty()&&saved.sections[0].body==draft->toPlainText(),"Stored source is not BBCode");
    control<QPushButton>(local,"pgn-social-open-access")->click();app.processEvents();auto* unverified=local.findChild<QDialog*>("pgn-social-access-dialog");expect(unverified&&!control<QPushButton>(*unverified,"pgn-social-assign-role")->isEnabled(),"Local identity auto-granted a role");
    local.setIdentity({QString(64,'b'),identity().subject});expect(!local.findChild<QDialog*>("pgn-social-access-dialog"),"Identity change retained access dialog");local.close();
    struct PendingRead{ReadRequest request;Services::Reply done;};QVector<PendingRead> reads;
    struct PendingThread{ThreadRequest request;std::function<void(ThreadSnapshot,QString)> done;};QVector<PendingThread> threadReads;
    Services injected;injected.read=[&](auto req,auto done){reads.append({req,done});};injected.readThread=[&](auto req,auto done){threadReads.append({req,done});};
    auto response=[&](const ReadRequest& req){Snapshot result;result.identity=req.identity;result.section=req.section;result.window=req.window;result.authenticated=true;result.source="SYNTHETIC CONTRACT FIXTURE";result.observedUtc=QDateTime::currentDateTimeUtc();result.access=access();
        if(req.section==Section::Chat){result.channel=req.channel.isEmpty()?"general":req.channel;result.channels=QVector<Channel>{{"general","general","Synthetic channel"},{"mining","mining","Synthetic channel"}};
            result.participants=QVector<Participant>{{"peer","Synthetic moderator","Online",{"moderator"}},{identity().subject,"Synthetic member",{},{"member"}}};}
        if(req.section==Section::Miners){Provenance provenance{req.identity.genesis,"SYNTHETIC PROVENANCE",QDateTime::currentDateTimeUtc(),true};Miner miner{"miner","Synthetic miner",{QString("0"),provenance},{},{}};miner.socialMemberId=identity().subject;result.miners={miner};}
        else{Entry item;item.id="synthetic-item";item.author="Synthetic author";item.authorId=identity().subject;item.title="Synthetic title";item.body="[b]Synthetic source[/b]";item.createdUtc=QDateTime::currentDateTimeUtc();if(req.section==Section::Forum){item.category="General";item.replies={{"reply","Synthetic reply","Synthetic reply body",QDateTime::currentDateTimeUtc(),identity().subject}};item.replyCount="200";item.lastActivity=QDateTime::currentDateTimeUtc();item.state="Open";item.nextReplyCursor="page-0";item.replies[0].reactions=QVector<Reaction>{{"like","Like",{},false}};}result.entries={item};}
        return result;};
    ui::SocialView network(path+"/network/drafts.json",injected);network.resize(1280,677);network.show();network.activateWindow();network.setIdentity(identity());app.processEvents();
    const auto initial=reads.last();initial.done(response(initial.request),{});
    auto* channelList=control<QListWidget>(network,"pgn-social-chat-channels");auto* chatBody=control<QPlainTextEdit>(network,"pgn-social-draft-body-0");
    expect(channelList->count()==2&&control<QLabel>(network,"pgn-social-global-channel")->text()=="# general","Authenticated channel data not displayed");
    chatBody->setPlainText("Unassigned network draft");channelList->setCurrentRow(1);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(channelList,&enter);
    expect(reads.last().request.channel=="mining"&&chatBody->toPlainText().isEmpty(),"Channel request/draft scope not isolated");initial.done(response(initial.request),{});
    expect(control<QListWidget>(network,"pgn-social-list-0")->count()==0,"Late channel response accepted");
    reads.last().done(response(reads.last().request),{});chatBody->setPlainText("[b]Mining draft[/b]");
    channelList->setCurrentRow(0);QApplication::sendEvent(channelList,&enter);reads.last().done(response(reads.last().request),{});expect(chatBody->toPlainText().isEmpty(),"Channel draft leaked into general");
    chatBody->setPlainText("[i]General draft[/i]");channelList->setCurrentRow(1);QApplication::sendEvent(channelList,&enter);reads.last().done(response(reads.last().request),{});expect(chatBody->toPlainText()=="[b]Mining draft[/b]","Channel BBCode draft not recovered");
    auto* participantList=control<QListWidget>(network,"pgn-social-chat-members");expect(participantList->horizontalScrollBarPolicy()==Qt::ScrollBarAlwaysOff,"Participants have horizontal chrome");
    auto* toggle=control<QPushButton>(network,"pgn-social-toggle-members");toggle->click();expect(!participantList->isVisible(),"Participants cannot collapse");toggle->click();expect(participantList->isVisible(),"Participants cannot reopen");
    for(int section=0;section<4;++section){
        network.selectSection(static_cast<Section>(section));if(section)reads.last().done(response(reads.last().request),{});app.processEvents();
        QWidget* target;if(section==3){auto* table=control<QTableWidget>(network,"pgn-social-miners");table->setCurrentCell(0,0);target=table;}
        else{auto* list=control<QListWidget>(network,"pgn-social-list-"+QString::number(section));list->setCurrentRow(0);target=list;}
        target->setFocus();QContextMenuEvent event(QContextMenuEvent::Keyboard,QPoint(-1,-1),QPoint(20,20));QApplication::sendEvent(target,&event);app.processEvents();
        auto* menu=network.findChild<QMenu*>("pgn-social-context-"+QString::number(section));
        if(!menu){QMetaObject::invokeMethod(target,"customContextMenuRequested",Qt::DirectConnection,Q_ARG(QPoint,QPoint(-1,-1)));app.processEvents();menu=network.findChild<QMenu*>("pgn-social-context-"+QString::number(section));}
        expect(menu&&menu->actions().size()>=2,"Keyboard context menu unavailable");
        bool memberAction=false;for(const auto* action:menu->actions()){memberAction|=action->text().contains("member actions");if(action->text().startsWith("Remove "))expect(section!=3&&!action->isEnabled(),"Unauthenticated moderation enabled");if(section==3)expect(!action->text().startsWith("Remove ")&&!action->text().contains("approve",Qt::CaseInsensitive),"Leaderboard mutation/approval invented");}
        expect(memberAction,"Canonical member target missing from context menu");menu->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        if(section==2){auto* list=control<QListWidget>(network,"pgn-social-list-2");const QPoint local(list->viewport()->width()-16,list->visualItemRect(list->currentItem()).center().y());QMouseEvent moreEvent(QEvent::MouseButtonRelease,QPointF(local),QPointF(list->viewport()->mapToGlobal(local)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(list->viewport(),&moreEvent);app.processEvents();auto* rowMenu=network.findChild<QMenu*>("pgn-social-context-2");expect(rowMenu&&control<QStackedWidget>(network,"pgn-social-journey-2")->currentIndex()==0,"Inline topic actions also opened the reader");rowMenu->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
            QApplication::sendEvent(list,&enter);auto* reader=control<QTextBrowser>(network,"pgn-social-forum-body-reply");
            QMetaObject::invokeMethod(reader,"customContextMenuRequested",Qt::DirectConnection,Q_ARG(QPoint,QPoint(20,20)));app.processEvents();auto* replyMenu=network.findChild<QMenu*>("pgn-social-context-2");bool replyAction=false;for(const auto* action:replyMenu->actions())replyAction|=action->text()=="Remove reply";expect(replyAction,"Reply menu targets the whole topic");replyMenu->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
            auto* reaction=control<QPushButton>(network,"pgn-social-forum-reaction-reply-like");expect(reaction->text().contains("unavailable")&&!reaction->isEnabled(),"Reaction absence became zero or enabled write");
            control<QPushButton>(network,"pgn-social-forum-quote-reply")->click();auto* forumDraft=control<QPlainTextEdit>(network,"pgn-social-draft-body-2");expect(forumDraft->toPlainText().contains("[quote]Synthetic reply body[/quote]")&&network.saveDrafts(),"Forum quote did not produce saved BBCode");
            Drafts forumSaved;expect(DraftStore(path+"/network/drafts.json").load(identity(),forumSaved).isEmpty()&&forumSaved.replyTargets.value("synthetic-item")=="reply","Quoted reply lost canonical target");
            control<QPushButton>(network,"pgn-social-preview-forum")->click();expect(control<QTextBrowser>(network,"pgn-social-forum-preview")->toPlainText().contains("Synthetic reply body")&&!control<QTextBrowser>(network,"pgn-social-forum-preview")->toPlainText().contains("[quote]"),"Forum editor/preview format diverged");control<QPushButton>(network,"pgn-social-forum-back-preview")->click();control<QPushButton>(network,"pgn-social-back-compose-2")->click();
            auto* more=control<QPushButton>(network,"pgn-social-more-replies");more->click();auto invalidPage=threadReads.last();ThreadSnapshot bad;bad.identity=invalidPage.request.identity;bad.identity.genesis=QString(64,'b');bad.topic=invalidPage.request.topic;bad.source="SYNTHETIC THREAD FIXTURE";bad.authenticated=true;bad.observedUtc=QDateTime::currentDateTimeUtc();invalidPage.done(bad,{});
            expect(control<QListWidget>(network,"pgn-social-forum-thread")->count()==2&&control<QLabel>(network,"pgn-social-forum-thread-state")->text().contains("retained"),"Wrong-genesis reply page replaced posts");
            more->click();invalidPage=threadReads.last();bad.identity=invalidPage.request.identity;bad.replies={{"reply","Synthetic overlap","Should be rejected",QDateTime::currentDateTimeUtc(),identity().subject}};invalidPage.done(bad,{});
            expect(control<QListWidget>(network,"pgn-social-forum-thread")->count()==2,"Overlapping reply page duplicated posts");
            const int started=threadReads.size();for(int page=0;page<4;++page){more->click();const auto pending=threadReads.last();expect(pending.request.limit==(page==3?49:50),"Thread remaining-capacity bound missing");more->click();expect(threadReads.size()==started+page+1,"Overlapping thread read admitted");
                ThreadSnapshot thread;thread.identity=pending.request.identity;thread.topic=pending.request.topic;thread.source="SYNTHETIC THREAD FIXTURE";thread.authenticated=true;thread.observedUtc=QDateTime::currentDateTimeUtc();thread.nextCursor=page==3?QString{}:QString("page-%1").arg(page+1);
                for(int j=0;j<pending.request.limit;++j)thread.replies.append({QString("page-%1-reply-%2").arg(page).arg(j),"Synthetic member","Synthetic paged reply",QDateTime::currentDateTimeUtc(),identity().subject});pending.done(thread,{});}
            expect(control<QListWidget>(network,"pgn-social-forum-thread")->count()==201&&!more->isEnabled(),"Thread paging exceeded cap or omitted distinct posts");
        }
    }
    network.close();

    ui::SocialView restarted(path+"/drafts.json"),newMachine(path+"/new-machine/drafts.json");for(auto* fresh:{&restarted,&newMachine}){fresh->setIdentity(identity());expect(fresh->property("pgn_social_state_0")=="offline","Restart/new machine restored client authority");}
}
}
