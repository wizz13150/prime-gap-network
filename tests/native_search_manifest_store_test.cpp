#include "booking/booking_store.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <iostream>

int main(int argc,char** argv){
 QCoreApplication app(argc,argv);int checks=0,failures=0;const auto check=[&](bool ok,const char* why){++checks;if(!ok){++failures;std::cerr<<why<<'\n';}};
 const QString device="GPU-11111111-2222-3333-4444-555555555555",run="11111111222233334444555555555555",first="878591000000000000000",last="878591000000000000100",hash(64,'a');
 const QJsonObject work{{"schema","PGN_NATIVE_SCIENTIFIC_WORK1"},{"genesis",hash},{"parent",hash},{"height",5},{"request_id",hash},{"ticket",hash},{"start",first},{"length","1000"},{"segments",256},{"report_root",QString(64,'0')},{"work_id",hash},{"challenge","public component-only fixture"},{"miner_script","00"},{"reward_script","00"},{"target_numerator","1899633"},{"target_denominator","65536"},{"science_policy_script","00"},{"accepted",false}};
 const QJsonObject bounds{{"schema","pgn-native-search-slice-1"},{"work_id",hash},{"start",first},{"end",last}};
 const QJsonObject context{{"schema","pgn-native-scientific-source-context-2"},{"authority","SEARCH_CONTEXT_NOT_NATIVE_ACCEPTANCE_OR_RANGE_CERTIFICATE"},{"rpc_work_sha256",hash},{"rpc_work",work},{"search_slice",bounds}};
 const QJsonObject profile{{"schema","pgn-run-source-profile-2"},{"protocol","PGN_NATIVE_GAP_RUNTIME_V1"},{"job",QJsonObject{{"start",first},{"end",last}}}};
 const QJsonObject manifest{{"schema","pgn-native-process-1"},{"source_kind","NATIVE_PGN_SCIENCE"},{"run",run},{"uuid",device},{"pid",4242},{"birth","2026-10-30T00:00:00.1234567Z"},{"start",first},{"end",last},{"queued_intent",QJsonValue::Null},{"source_profile",profile},{"native_scientific_context",context}};
 QTemporaryDir directory;const auto file=directory.filePath("bookings.sqlite");pgn::booking::BookingStore store(file);check(store.valid(),"store opened");
 check(store.saveSourceRunManifest(device,manifest).isEmpty(),"explicit unqueued native source retained without synthetic reservation");
 auto saved=store.savedSourceRunManifest(device,run);check(saved.status==pgn::booking::SourceRunManifestReadStatus::Present&&saved.manifest==manifest,"exact immutable manifest readback");
 check(saved.blockStartedUtc==manifest.value("birth").toString(),"actual birth retained");
 sqlite3* db{};check(sqlite3_open(file.toUtf8().constData(),&db)==SQLITE_OK,"independent read connection");
 auto scalar=[&](const char* sql){sqlite3_stmt* statement{};if(sqlite3_prepare_v2(db,sql,-1,&statement,nullptr)!=SQLITE_OK)return -1;const int result=sqlite3_step(statement)==SQLITE_ROW?sqlite3_column_int(statement,0):-1;sqlite3_finalize(statement);return result;};
 check(scalar("SELECT count(*) FROM bookings WHERE kind='RUNTIME'")==1,"one observed physical row");
 check(scalar("SELECT count(*) FROM bookings WHERE kind='LOCAL_INTENT'")==0,"no reservation invented");
 check(scalar("SELECT count(*) FROM sqlite_master WHERE name='dispatch_requests'")==0,"no dispatch authority invented");
 const auto before=scalar("SELECT count(*) FROM booking_metadata");
 check(store.saveSourceRunManifest(device,manifest).isEmpty()&&scalar("SELECT count(*) FROM booking_metadata")==before,"immutable replay has no new entries");
 for(int poison=0;poison<7;++poison){auto bad=manifest;auto c=context;auto w=work;auto b=bounds;
  if(poison==0){w.insert("accepted",true);c.insert("rpc_work",w);bad.insert("native_scientific_context",c);}
  if(poison==1){b.insert("work_id",QString(64,'b'));c.insert("search_slice",b);bad.insert("native_scientific_context",c);}
  if(poison==2){b.insert("end","878592000000000000000");c.insert("search_slice",b);bad.insert("native_scientific_context",c);}
  if(poison==3)bad.insert("source_kind","COMMUNITY");
  if(poison==4)bad.insert("resume_previous_run","22222222333344445555666666666666");
  if(poison==5){w.insert("parent","bad");c.insert("rpc_work",w);bad.insert("native_scientific_context",c);}
  if(poison==6){c.insert("unexpected",true);bad.insert("native_scientific_context",c);}
  check(!store.saveSourceRunManifest(device,bad).isEmpty(),"invalid/changed search provenance refused");
  check(store.savedSourceRunManifest(device,run).manifest==manifest,"failed write preserved actual manifest");
 }
 sqlite3_close(db);std::cout<<"native search manifest checks="<<checks<<" failures="<<failures<<"; metadata/physical history only, no reservation/proof/chain acceptance\n";return failures?1:0;
}
