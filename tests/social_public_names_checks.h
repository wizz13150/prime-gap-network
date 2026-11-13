#pragma once
#include "social/social_leaderboard.h"
#include "social/social_public_names.h"
#include "social/social_core_service.h"
#include "social/social_signing_contract.h"
#include "ui/social_view.h"
#include "ui/social_presentation.h"
#include <QApplication>
#include <QLabel>
#include <QListWidget>
#include <QTemporaryDir>
#include <stdexcept>

namespace pgn::test::names {
inline int checks=0;
inline void expect(bool good,const char* why){++checks;if(!good)throw std::runtime_error(why);}
inline const QString scriptA="0014"+QString(40,'1'),scriptB="0014"+QString(40,'2');
inline social::Identity identity(){return {QString(64,'a'),scriptA};}
inline QJsonObject row(QString op,QString label,QStringList scripts){
    QJsonArray aliases;for(const auto& script:scripts)aliases.append(script);
    return {{"operator_id",op},{"display_name",label},{"signing_scripts",aliases},{"signing_scripts_total",scripts.size()},{"signing_scripts_more",false},{"tickets_reported","1"},{"tickets_validated","1"},{"audits_finalized_valid","0"},{"validated_search_positions","9007199254740993"},{"last_activity_height",12}};
}
inline QJsonObject wire(QJsonArray rows,qint64 offset=0,qint64 total=-1){
    const qint64 next=offset+rows.size();if(total<0)total=next;
    return {{"schema","PGN_LEADERBOARD_V1"},{"genesis",identity().genesis},{"bestblockhash",QString(64,'c')},{"height",20},{"offset",offset},{"next_offset",next},{"total",total},{"more",next<total},{"sort","validated_search_positions_desc_then_operator_id_asc"},{"rows",rows}};
}
inline social::Snapshot decode(const social::ReadRequest& request,QJsonObject value){social::Snapshot out;expect(social::decodeLeaderboard(request,value,out).isEmpty(),"Name fixture failed real leaderboard decoder");expect(social::validate(out,request).isEmpty(),"Name fixture failed authenticated snapshot validation");return out;}
inline void run(QApplication& app,const QString& path){
    using namespace social;ReadRequest first{Section::Miners,identity()};
    const auto a=row(QString(64,'2'),"Wizz",{scriptA});const auto b=row(QString(64,'3'),"Wizz",{scriptB});
    auto initial=decode(first,wire({a},0,2));PublicNames directory;directory.begin(identity());
    expect(directory.append(first,initial).isEmpty()&&directory.collecting()&&directory.display(identity(),scriptA).isEmpty(),"Partial page published public names");
    auto second=first;second.cursor=initial.nextCursor;auto final=decode(second,wire({b},1,2));
    expect(directory.append(second,final).isEmpty()&&!directory.collecting(),"Complete pinned name registry failed");
    const auto nameA=directory.display(identity(),scriptA),nameB=directory.display(identity(),scriptB);
    expect(nameA==initial.miners[0].name&&nameB==final.miners[0].name&&nameA!=nameB,"Homonyms merged or names differ from Leaderboard");
    expect(directory.display({QString(64,'b'),scriptA},scriptA).isEmpty(),"Public name leaked into another genesis");
    expect(directory.display(identity(),"0014"+QString(40,'9')).isEmpty(),"Unknown Social key inherited an operator name");
    expect(directory.display(identity(),scriptA,initial.observedUtc.addSecs(301)).isEmpty(),"Expired registry provenance retained a name");
    auto alias=row(QString(64,'2'),"Robert",{scriptA,scriptB});directory.begin(identity());auto aliases=decode(first,wire({alias}));
    expect(directory.append(first,aliases).isEmpty()&&directory.display(identity(),scriptA)==directory.display(identity(),scriptB),"Authenticated aliases did not share their operator label");
    expect(directory.display(identity(),scriptA).contains("Robert")&&!directory.display(identity(),scriptA).contains("Wizz"),"Canonical rename retained old label");
    auto collision=b;collision.insert("signing_scripts",QJsonArray{scriptA});directory.begin(identity());
    expect(directory.append(first,decode(first,wire({a,collision}))).isEmpty()&&directory.display(identity(),scriptA).isEmpty(),"Ambiguous historical script selected an arbitrary operator");
    auto truncated=a;truncated.insert("signing_scripts_total",2);truncated.insert("signing_scripts_more",true);directory.begin(identity());
    expect(!directory.append(first,decode(first,wire({truncated}))).isEmpty()&&directory.display(identity(),scriptA).isEmpty(),"Truncated aliases were treated as a complete registry");
    auto legacy=a;legacy.remove("signing_scripts");legacy.remove("signing_scripts_total");legacy.remove("signing_scripts_more");directory.begin(identity());
    expect(!directory.append(first,decode(first,wire({legacy}))).isEmpty(),"Missing alias evidence granted a public label");
    directory.begin(identity());expect(directory.append(first,initial).isEmpty(),"Pinned fixture first page failed");
    auto moved=wire({b},1,2);moved.insert("bestblockhash",QString(64,'d'));auto forged=final;forged.leaderboardPeriod->anchor=QString(64,'d');
    expect(!directory.append(second,forged).isEmpty(),"Name pages mixed canonical anchors");
    directory.begin(identity());expect(directory.append(first,initial).isEmpty(),"Pinned fixture restart failed");
    forged=final;forged.leaderboardPeriod->total=3;expect(!directory.append(second,forged).isEmpty(),"Name pages mixed exhaustive row totals");
    auto bad=a;bad.insert("signing_scripts",QJsonArray{scriptA,scriptA});bad.insert("signing_scripts_total",2);Snapshot rejected;
    expect(!decodeLeaderboard(first,wire({bad}),rejected).isEmpty(),"Duplicate signing alias accepted");
    bad=a;bad.insert("signing_scripts",QJsonArray{QString("0014")+QString(40,'A')});expect(!decodeLeaderboard(first,wire({bad}),rejected).isEmpty(),"Noncanonical signing alias accepted");
    bad=a;bad.insert("signing_scripts_more",1);expect(!decodeLeaderboard(first,wire({bad}),rejected).isEmpty(),"Untyped alias completeness flag accepted");
    auto wrong=first;wrong.identity.subject=scriptB;directory.begin(identity());expect(!directory.append(wrong,initial).isEmpty(),"Late registry accepted after identity scope change");

    // Service integration boundary. Crypto callbacks are explicit test doubles;
    // native BIP322/network proof is separate, never implied by these fixtures.
    QVector<QPair<ReadRequest,Services::Reply>> requests;
    CoreSocialCrypto crypto;crypto.sign=[](QByteArray,auto done){done("SYNTHETIC-TEST-PROOF",{});};crypto.verify=[](QString,QByteArray,QString sig,auto done){done(sig=="SYNTHETIC-TEST-PROOF",{});};
    QJsonObject event{{"v",1},{"genesis",identity().genesis},{"origin","pgn-social://"+identity().genesis},{"sender_script",scriptB},{"signing_address","SYNTHETIC-TEST-ADDRESS"},{"device",QString(32,'b')},{"channel","global"},{"created_ms",QDateTime::currentMSecsSinceEpoch()},{"nonce",QString(64,'a')},{"kind","chat"},{"parents",QJsonArray{}},{"bbcode_b64",QString::fromLatin1(QByteArray("[b]Synthetic source[/b]").toBase64())}};
    event.insert("event_id",QString::fromLatin1(QCryptographicHash::hash("PGN/SOCIAL/EVENT-ID/1\n"+canonical(event),QCryptographicHash::Sha256).toHex()));
    const auto message="PGN/SOCIAL/EVENT/1\n"+canonical(event);
    CoreSocialRpc rpc=[&](CoreSocialCall call,QJsonArray,auto done){
        if(call==CoreSocialCall::ReadEvents)done(QJsonObject{{"genesis",identity().genesis},{"generation",QString(64,'c')},{"next_cursor",1},{"records",QJsonArray{QJsonObject{{"canonical_message",QString::fromUtf8(message)},{"signature","SYNTHETIC-TEST-PROOF"}}}},{"more",false}},{});
        else if(call==CoreSocialCall::ReadPeers)done(QJsonObject{{"genesis",identity().genesis},{"relay_peers",QJsonArray{}}},{});
        else done(QJsonObject{{"genesis",identity().genesis},{"accepted_local",true}},{});
    };
    CoreSocialContext context{identity(),"SYNTHETIC-OWNED-ADDRESS",QString(32,'a'),"global","SYNTHETIC UNIT NETWORK"};
    CoreSocialService service(context,crypto,rpc,path);service.leaderboard=[&](ReadRequest request,Services::Reply done){requests.append({request,done});};service.start();app.processEvents();
    expect(requests.size()==1,"Native Social service did not request authenticated public names");
    auto snapshot=[&]{Snapshot s;QString error;service.services().read({Section::Chat,identity()},[&](Snapshot v,QString e){s=v;error=e;});expect(error.isEmpty(),"Chat became unavailable while resolving names");return s;};
    auto chat=snapshot();expect(chat.entries.size()==1&&chat.entries[0].author==scriptB&&chat.entries[0].authorId==scriptB,"Unresolved author key was changed or guessed");
    const auto lookup=requests[0];lookup.second(decode(lookup.first,wire({a,b})),{});chat=snapshot();
    expect(chat.entries[0].author==publicIdentityDisplayName(QString(64,'3'),"Wizz")&&chat.entries[0].authorId==scriptB&&chat.entries[0].body=="[b]Synthetic source[/b]","Public name changed message identity/source or failed service path");
    expect(chat.participants&&chat.participants->size()==2&&chat.participants->at(0).id==scriptA&&chat.participants->at(0).name==publicIdentityDisplayName(QString(64,'2'),"Wizz"),"Self name or canonical participant identity missing");
    expect(chat.participants->at(0).roleIds.isEmpty()&&chat.participants->at(1).roleIds.isEmpty()&&!chat.access,"Public name granted moderator authority");
    {ui::SocialView view(path+"-view",service.services());view.setIdentity(identity());
     const auto* account=view.findChild<QLabel*>("pgn-social-account");const auto* members=view.findChild<QListWidget*>("pgn-social-chat-members");
     expect(account&&account->text()==publicIdentityDisplayName(QString(64,'2'),"Wizz")&&account->textFormat()==Qt::PlainText,"Self header did not use the same plain public name as members/Leaderboard");
     expect(members&&members->count()==2&&members->item(0)->data(Qt::UserRole)!=members->item(1)->data(Qt::UserRole),"Homonymous member names merged their canonical keys");}
    const auto longName=publicIdentityDisplayName(QString(64,'2'),QString(192,'x'));
    expect(longName.size()<=80&&longName.endsWith(QString(8,'2')+"…"+QString(6,'2')),"Long label hid canonical disambiguation suffix");
    expect(ui::SocialAvatarInitials(scriptA,publicIdentityDisplayName(QString(64,'2'),"Wizz"))=="W","Operator suffix contaminated avatar initials");
    expect(ui::SocialAvatarInitials(scriptA,"Miner Bob")=="MB","Legitimate Miner-prefixed public name was treated as missing identity");
    lookup.second({},"duplicate callback must be ignored");expect(snapshot().entries[0].author==chat.entries[0].author,"Duplicate name completion replaced established registry");
    service.stop();service.start();app.processEvents();const auto late=requests.last();service.stop();late.second(decode(late.first,wire({a,b})),{});
    service.start();app.processEvents();expect(snapshot().entries[0].author==scriptB,"Stopped service late callback restored old public names");service.stop();
}
}
