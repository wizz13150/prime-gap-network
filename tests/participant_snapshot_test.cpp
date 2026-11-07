#include "wallet/participant_snapshot.h"
#include <QStringList>
#include <iostream>
#include <stdexcept>
using pgn::wallet::validateParticipantSnapshot;
static void require(bool value,const char* what){if(!value)throw std::runtime_error(what);}
int main(){
 const QString genesis(64,'a');
 const QJsonObject member{{"operator_id",QString(64,'b')},{"signing_script","0014c9440489de0fdd513f592a7fa90abbda584544fc"},{"may_audit",false}};
 const QJsonObject valid{{"genesis",genesis},{"bestblockhash",QString(64,'c')},{"height",1852},{"total",1},{"participants",QJsonArray{member}}};
 require(validateParticipantSnapshot(valid,genesis).isEmpty(),"Real authority identity rejected");
 auto alternate=member;alternate.insert("signing_script","0014"+QString(40,'d'));auto aliases=valid;aliases.insert("participants",QJsonArray{member,alternate});aliases.insert("total",2);
 require(validateParticipantSnapshot(aliases,genesis).isEmpty(),"Valid aliases of the same operator rejected");
 int negative=0;
 for(const auto& script:QStringList{QString{},"f","0g","AB",QString(20002,'a')}){
  auto row=member;row.insert("signing_script",script);auto state=valid;state.insert("participants",QJsonArray{row});require(!validateParticipantSnapshot(state,genesis).isEmpty(),"Invalid script accepted");++negative;
 }
 for(const auto& identity:QStringList{QString(64,'0'),QString(63,'b'),QString(64,'B')}){
  auto row=member;row.insert("operator_id",identity);auto state=valid;state.insert("participants",QJsonArray{row});require(!validateParticipantSnapshot(state,genesis).isEmpty(),"Invalid operator accepted");++negative;
 }
 auto maximum=member;maximum.insert("signing_script",QString(20000,'a'));auto state=valid;state.insert("participants",QJsonArray{maximum});require(validateParticipantSnapshot(state,genesis).isEmpty(),"Largest bounded script rejected");
 state=valid;state.insert("genesis",QString(64,'d'));require(!validateParticipantSnapshot(state,genesis).isEmpty(),"Foreign chain accepted");++negative;
 state=aliases;state.insert("participants",QJsonArray{member,member});require(!validateParticipantSnapshot(state,genesis).isEmpty(),"Duplicate identity accepted");++negative;
 state=valid;state.insert("total",0);require(!validateParticipantSnapshot(state,genesis).isEmpty(),"Incorrect total accepted");++negative;
 state=valid;QJsonArray many;for(int i=0;i<101;++i)many.append(member);state.insert("total",101);state.insert("participants",many);require(!validateParticipantSnapshot(state,genesis).isEmpty(),"Oversized page accepted");++negative;
 auto wrong=member;wrong.insert("may_audit","true");state=valid;state.insert("participants",QJsonArray{wrong});require(!validateParticipantSnapshot(state,genesis).isEmpty(),"String permission accepted");++negative;
 std::cout<<"PASS native participant identities, aliases, maximum script and "<<negative<<" malformed boundaries\n";
}
