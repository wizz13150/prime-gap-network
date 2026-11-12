#pragma once
#include "ui/social_view.h"
#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <memory>
#include <stdexcept>

namespace pgn::test::chat {
inline int checks=0;
inline void expect(bool good,const char* why){++checks;if(!good)throw std::runtime_error(why);}
template<class T>inline T* control(QWidget& root,const QString& name){auto* p=root.findChild<T*>(name);expect(p,"Missing Chat test control");return p;}
// These callbacks deliberately model the Services UI boundary. They do not
// simulate BIP322, grant network rights, or replace native signature tests.
inline void run(const QString& path){
    using namespace social;
    const Identity a{QString(64,'a'),"SYNTHETIC-SIGNING-SCRIPT-A"};
    const Identity b{a.genesis,"SYNTHETIC-SIGNING-SCRIPT-B"};
    struct Pending{SendRequest request;std::function<void(SendResult)> done;};
    QVector<Pending> pending;
    Services services;
    services.read=[](ReadRequest req,Services::Reply done){
        Snapshot s;s.identity=req.identity;s.section=req.section;s.authenticated=true;
        s.source="SYNTHETIC UI CONTRACT ONLY";s.observedUtc=QDateTime::currentDateTimeUtc();
        s.channel=req.channel.isEmpty()?"synthetic-channel":req.channel;
        s.channels=QVector<Channel>{{s.channel,"Synthetic channel",{}}};
        s.chatSend=SendAvailability{true,{}};done(s,{});
    };
    services.sendChat=[&](SendRequest req,auto done){pending.append({req,done});};
    auto view=std::make_unique<ui::SocialView>(path,services);view->setIdentity(a);
    auto body=[&](){return control<QPlainTextEdit>(*view,"pgn-social-draft-body-0");};
    auto send=[&](){return control<QPushButton>(*view,"pgn-social-send-0");};
    auto reason=[&](){return control<QLabel>(*view,"pgn-social-send-reason-0")->text();};
    auto complete=[](const Pending& p,SendOutcome outcome,QString message={},QString why={}){
        p.done({p.request.identity,p.request.channel,p.request.requestId,outcome,message,why});
    };
    const QString source="[b]Source[/b]\n[quote]BBCode only[/quote]";
    body()->setPlainText(source);send()->click();expect(pending.size()==1,"Composer did not call send adapter");
    const auto first=pending.last();expect(first.request.identity==a&&first.request.bbcode==source&&!first.request.requestId.isEmpty(),"Send changed canonical identity/source");
    send()->click();expect(pending.size()==1&&!send()->isEnabled(),"Overlapping send admitted");
    expect(control<QListWidget>(*view,"pgn-social-list-0")->count()==0&&body()->toPlainText()==source,"Unconfirmed send invented a message or cleared source");
    complete(first,SendOutcome::Uncertain,{},"Connection lost");expect(body()->toPlainText()==source&&send()->isEnabled(),"Uncertain send lost editable draft/retry");
    send()->click();expect(pending.size()==2&&pending.last().request.requestId==first.request.requestId&&pending.last().request.bbcode==source,"Retry duplicated operation or changed source");
    const auto retry=pending.last();complete(first,SendOutcome::Accepted,"late-old-completion");
    expect(body()->toPlainText()==source&&!send()->isEnabled(),"Duplicate completion cleared in-flight retry");
    complete(retry,SendOutcome::Accepted,"synthetic-receipt-id");expect(body()->toPlainText().isEmpty(),"Confirmed unchanged source not cleared");
    body()->setPlainText("New source after confirmation");complete(retry,SendOutcome::Accepted,"duplicate");
    expect(body()->toPlainText()=="New source after confirmation","Duplicate confirmation cleared a newer draft");
    send()->click();const auto edited=pending.last();body()->setPlainText("Edited while pending");complete(edited,SendOutcome::Accepted,"synthetic-receipt-id-2");
    expect(body()->toPlainText()=="Edited while pending"&&reason().contains("current draft kept"),"Accepted earlier source erased current edit");
    send()->click();const auto stale=pending.last();view->setIdentity(b);body()->setPlainText("Other identity source");
    complete(stale,SendOutcome::Accepted,"stale-identity");expect(body()->toPlainText()=="Other identity source","Old identity send modified current draft");
    view->setIdentity(a);expect(body()->toPlainText()=="Edited while pending","Identity return lost scoped draft");
    send()->click();const auto restored=pending.last();expect(restored.request.requestId==stale.request.requestId,"Identity return did not retain exact pending operation");
    complete(restored,SendOutcome::Uncertain,{},"Lost reply before restart");view.reset();
    view=std::make_unique<ui::SocialView>(path,services);view->setIdentity(a);
    expect(body()->toPlainText()=="Edited while pending"&&reason().contains("recovered"),"Restart did not recover pending source/operation");
    send()->click();const auto restarted=pending.last();expect(restarted.request.requestId==restored.request.requestId,"Restart retry created duplicate operation");
    complete(restarted,SendOutcome::Posted,"synthetic-local-id","Posted locally · awaiting recipient receipt");
    expect(body()->toPlainText().isEmpty()&&reason().contains("awaiting recipient")&&!reason().contains("Delivered"),"Local Posted was presented as remote delivery");
    body()->setPlainText("Unauthorized source");send()->click();const auto denied=pending.last();
    complete(denied,SendOutcome::Rejected,{},"401 revoked credential");expect(body()->toPlainText()=="Unauthorized source"&&!send()->isEnabled()&&reason().contains("401"),"Revoked send remained enabled or lost source");
    view->refresh();send()->click();const auto mismatch=pending.last();auto wrong=mismatch.request.identity;wrong.genesis=QString(64,'b');
    mismatch.done({wrong,mismatch.request.channel,mismatch.request.requestId,SendOutcome::Accepted,"forged-context",{}});
    expect(body()->toPlainText()=="Unauthorized source"&&!send()->isEnabled()&&reason().contains("scope mismatch"),"Wrong-genesis send response accepted");
    // Actual composer event filter: Shift+Enter is a newline, Enter submits.
    view->refresh();body()->setPlainText("Keyboard source");const int before=pending.size();
    QKeyEvent newline(QEvent::KeyPress,Qt::Key_Return,Qt::ShiftModifier);QApplication::sendEvent(body(),&newline);
    expect(pending.size()==before&&body()->toPlainText().contains('\n'),"Shift+Enter sent rather than inserted newline");
    QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(body(),&enter);
    expect(pending.size()==before+1&&pending.last().request.bbcode==body()->toPlainText(),"Enter did not send the exact source");
    const auto destroyed=pending.last();view.reset();complete(destroyed,SendOutcome::Accepted,"after-destruction");
    expect(true,"Destroyed widget callback survived safely");
}
}
