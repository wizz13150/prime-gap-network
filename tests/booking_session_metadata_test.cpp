#include "booking/booking_store.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <iostream>
#include <stdexcept>

using namespace pgn::booking;
using namespace pgn::mining;
namespace {
int checks=0;
const QString gpu="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",other="GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265";
const QString modeKey="mining-session-mode/v1/"+gpu.toLower(),preferenceKey="mining-auto-after-manual/v1/"+gpu.toLower();
void require(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
class Database {
public:
 sqlite3* db{};
 explicit Database(const QString& file){if(sqlite3_open_v2(file.toUtf8().constData(),&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK)throw std::runtime_error("own fixture SQLite open failed");}
 ~Database(){if(db)sqlite3_close(db);}
 void sql(const QByteArray& sql){if(sqlite3_exec(db,sql.constData(),nullptr,nullptr,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
 QByteArray dump(const char* query){
  sqlite3_stmt* statement{};if(sqlite3_prepare_v2(db,query,-1,&statement,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));
  QByteArray bytes;int status;
  while((status=sqlite3_step(statement))==SQLITE_ROW){
   for(int column=0;column<sqlite3_column_count(statement);++column){
    bytes+=QByteArray::number(sqlite3_column_type(statement,column))+":";
    const auto* text=sqlite3_column_text(statement,column);bytes+=QByteArray(reinterpret_cast<const char*>(text),sqlite3_column_bytes(statement,column)).toHex()+";";
   }bytes+='\n';
  }sqlite3_finalize(statement);if(status!=SQLITE_DONE)throw std::runtime_error("own fixture query failed");return bytes;
 }
 void metadata(const QString& key,const QByteArray& value,bool blob=false){
  sqlite3_stmt* statement{};if(sqlite3_prepare_v2(db,"INSERT INTO booking_metadata(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value",-1,&statement,nullptr)!=SQLITE_OK)throw std::runtime_error("fixture metadata prepare failed");
  const auto bytes=key.toUtf8();sqlite3_bind_text(statement,1,bytes.constData(),bytes.size(),SQLITE_TRANSIENT);
  if(blob)sqlite3_bind_blob(statement,2,value.constData(),value.size(),SQLITE_TRANSIENT);else sqlite3_bind_text(statement,2,value.constData(),value.size(),SQLITE_TRANSIENT);
  const auto status=sqlite3_step(statement);sqlite3_finalize(statement);if(status!=SQLITE_DONE)throw std::runtime_error("fixture metadata write failed");
 }
};
QByteArray jobs(Database& db){return db.dump("SELECT * FROM bookings ORDER BY id")+db.dump("SELECT * FROM booking_events ORDER BY sequence")+db.dump("SELECT * FROM sqlite_sequence ORDER BY name");}
QByteArray metadata(Database& db){return db.dump("SELECT key,value FROM booking_metadata ORDER BY key");}
bool manualAvailable(const BookingStore& store){for(const auto& job:store.active())if(job.device.compare(gpu,Qt::CaseInsensitive)==0&&job.kind=="LOCAL_INTENT"&&(job.state=="QUEUED"||job.state=="ARMED"))return true;return false;}
NextWorkDecision decide(const BookingStore& store,bool open=true,bool enabled=true){
 const auto saved=store.miningSessionMetadata(gpu);if(!saved)return {};
 NextWorkContext context;context.applicationOpen=open;context.miningEnabled=enabled;context.mode=saved->mode;context.automaticAfterManual=saved->automaticAfterManual;
 context.manualWorkAvailable=manualAvailable(store);context.boundary=WorkBoundary::Completed;return chooseNextWork(context);
}
}
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);
 try{
  QTemporaryDir temporary;require(temporary.isValid(),"own temporary directory");const auto file=temporary.filePath("booking.sqlite");QByteArray originalJobs,unknown;
  {
   BookingStore store(file);require(store.valid(),"real booking store creates own fixture schema");Database db(file);
   db.metadata("fixture/unrelated","KEEP");db.metadata("mining-session-mode/v2/future-device","future-unknown-preserved");
   db.sql("INSERT INTO bookings(id,start,end,start_key,end_key,state,kind,device,created_utc) VALUES('PUBLIC-OTHER-DEVICE-INTENT','1000','2000','0041000','0042000','QUEUED','LOCAL_INTENT','GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265','2026-10-14T00:00:00Z');");
   originalJobs=jobs(db);unknown=db.dump("SELECT key,value FROM booking_metadata WHERE key LIKE 'fixture/%' OR key LIKE '%/v2/%' ORDER BY key");const auto before=metadata(db);
   for(int i=0;i<32;++i){const auto saved=store.miningSessionMetadata(gpu);require(saved&&saved->mode==MiningSessionMode::Unselected&&!saved->automaticAfterManual,"missing preference/mode defaults OFF/Unselected, never Auto");}
   require(store.automaticAfterManual(gpu).has_value()&&!store.automaticAfterManual(gpu).value(),"known default false distinct from unavailable");
   require(metadata(db)==before&&jobs(db)==originalJobs,"reads insert no defaults and change no jobs/audit rows");
   require(decide(store).action==NextWorkAction::Hold,"opening a store/empty device queue does not authorize automatic mining");
   require(store.setMiningSessionMode(gpu,explicitMiningStart(true)).isEmpty(),"explicit manual user start persists mode");
   require(store.miningSessionMetadata(gpu)->mode==MiningSessionMode::ManualReservations&&!store.automaticAfterManual(gpu).value(),"mode write preserves absent OFF preference");
   require(!manualAvailable(store)&&jobs(db)==originalJobs,"target device queue is empty, other device intent untouched");
  }
  {
   BookingStore store(file);require(store.valid(),"actual close/reopen");Database db(file);const auto persisted=store.miningSessionMetadata(gpu);
   require(persisted&&persisted->mode==MiningSessionMode::ManualReservations&&!persisted->automaticAfterManual,"Manual/OFF persists across actual reopen");
   require(decide(store).action==NextWorkAction::EndSession&&decide(store).mode==MiningSessionMode::ManualReservations,"empty queue after manual ends session, never inferred fresh automatic start");
   require(decide(store,false,true).action==NextWorkAction::Hold&&decide(store,true,false).action==NextWorkAction::Hold,"closed application or disabled mining always holds");
   require(store.setAutomaticAfterManual(gpu,true).isEmpty(),"explicit opt-in saved");
   require(store.miningSessionMetadata(gpu)->mode==MiningSessionMode::ManualReservations&&decide(store).action==NextWorkAction::ClaimAvailable,"ON permits policy claim but retains manual origin");
   require(decide(store,false,true).action==NextWorkAction::Hold&&decide(store,true,false).action==NextWorkAction::Hold,"ON never bypasses application close/disable");
   const auto unrelated=store.miningSessionMetadata(other);require(unrelated&&unrelated->mode==MiningSessionMode::Unselected&&!unrelated->automaticAfterManual,"settings are device-scoped");
   require(store.setMiningSessionMode(other,MiningSessionMode::Automatic).isEmpty(),"other device explicit mode independent");
   require(store.miningSessionMetadata(gpu.toLower())->mode==MiningSessionMode::ManualReservations&&store.automaticAfterManual(gpu.toLower()).value(),"GPU UUID case cannot create a second preference namespace");
   require(jobs(db)==originalJobs&&db.dump("SELECT key,value FROM booking_metadata WHERE key LIKE 'fixture/%' OR key LIKE '%/v2/%' ORDER BY key")==unknown,"all jobs/audit rows and unrelated/unknown metadata preserved");
  }
  {
   BookingStore store(file);require(store.valid(),"second real reopen");Database db(file);
   require(store.miningSessionMetadata(gpu)->mode==MiningSessionMode::ManualReservations&&store.automaticAfterManual(gpu).value()&&decide(store).action==NextWorkAction::ClaimAvailable,"Manual/ON persistence, no constructor reset");
   require(store.setAutomaticAfterManual(gpu,false).isEmpty()&&decide(store).action==NextWorkAction::EndSession,"explicit OFF restores EndSession without rewriting mode");
   for(const auto device:{QString{},QString("GPU-not-an-identity"),gpu+'\n',gpu+QChar(0)}){
    const auto before=metadata(db);require(!store.miningSessionMetadata(device)&&!store.automaticAfterManual(device),"invalid device never defaults to an available policy");
    require(!store.setAutomaticAfterManual(device,true).isEmpty()&&!store.setMiningSessionMode(device,MiningSessionMode::Automatic).isEmpty()&&metadata(db)==before,"invalid device writes refuse before mutation");
   }
   {const auto before=metadata(db);require(!store.setMiningSessionMode(gpu,static_cast<MiningSessionMode>(99)).isEmpty()&&metadata(db)==before,"unknown enum refuses without ordinal persistence");}
   for(const auto& text:{QByteArray("true"),QByteArray("01"),QByteArray(""),QByteArray(" 1"),QByteArray("1\n"),QByteArray("1\0junk",6),QByteArray(1,char(0xff)),QByteArray("\xef\xbb\xbf")+"1"}){
    db.metadata(preferenceKey,text);const auto before=metadata(db);require(!store.miningSessionMetadata(gpu)&&!store.automaticAfterManual(gpu),"invalid preference is unavailable, not false/true fallback");
    require(metadata(db)==before,"invalid preference reads preserve original bytes");require(store.setAutomaticAfterManual(gpu,false).isEmpty(),"explicit user preference repair only");
   }
   for(const auto& text:{QByteArray("Auto"),QByteArray("manual"),QByteArray("automatic\0junk",14),QByteArray("manual-reservations\n"),QByteArray(""),QByteArray(1,char(0xff)),QByteArray("\xef\xbb\xbf")+"manual-reservations"}){
    db.metadata(modeKey,text);const auto before=metadata(db);require(!store.miningSessionMetadata(gpu)&&!store.automaticAfterManual(gpu),"invalid joint mode never exposes a usable context");
    require(metadata(db)==before,"invalid mode reads preserve original bytes");require(store.setMiningSessionMode(gpu,MiningSessionMode::ManualReservations).isEmpty(),"explicit user mode repair only");
   }
   for(const auto& key:{modeKey,preferenceKey}){
    db.metadata(key,key==modeKey?QByteArray("manual-reservations"):QByteArray("0"),true);
    require(!store.miningSessionMetadata(gpu),"blob masquerading as text is invalid");
    require((key==modeKey?store.setMiningSessionMode(gpu,MiningSessionMode::ManualReservations):store.setAutomaticAfterManual(gpu,false)).isEmpty(),"explicit valid setter restores only chosen key");
   }
   for(const auto& key:{modeKey,preferenceKey}){
    // RAISE(FAIL) leaves a statement's earlier changes in place unless the
    // store really rolls back its transaction; ABORT alone would be weaker.
    db.sql("CREATE TRIGGER fixture_metadata_failure AFTER UPDATE ON booking_metadata WHEN NEW.key='"+key.toUtf8()+"' BEGIN UPDATE booking_metadata SET value='POISON' WHERE key='fixture/unrelated'; SELECT RAISE(FAIL,'fixture failure after mutation'); END;");
    const auto before=metadata(db),beforeJobs=jobs(db);
    const auto error=key==modeKey?store.setMiningSessionMode(gpu,MiningSessionMode::Automatic):store.setAutomaticAfterManual(gpu,true);
    require(!error.isEmpty()&&metadata(db)==before&&jobs(db)==beforeJobs,"failure after update rolls back chosen key, trigger side effect and all jobs/events");
    db.sql("DROP TRIGGER fixture_metadata_failure;");
   }
   require(store.miningSessionMetadata(gpu)->mode==MiningSessionMode::ManualReservations&&!store.automaticAfterManual(gpu).value()&&decide(store).action==NextWorkAction::EndSession,"rollback leaves original policy usable");
   require(jobs(db)==originalJobs&&db.dump("SELECT key,value FROM booking_metadata WHERE key LIKE 'fixture/%' OR key LIKE '%/v2/%' ORDER BY key")==unknown,"final unrelated metadata and full audit/job tuples unchanged");
  }
  {
   const auto broken=temporary.filePath("read-error.sqlite");BookingStore store(broken);require(store.valid(),"separate own read-error fixture");Database db(broken);db.sql("DROP TABLE booking_metadata;");
   const auto before=jobs(db);require(!store.miningSessionMetadata(gpu)&&!store.automaticAfterManual(gpu),"SQL error is unavailable, never default Auto/OFF usable context");
   require(!store.setAutomaticAfterManual(gpu,true).isEmpty()&&jobs(db)==before,"SQL write error preserved and no job/event mutation");
  }
  std::cout<<"Booking session metadata: "<<checks<<" checks / 0 failures; genuine own SQLite close/reopen/rollback + native policy, NO userDB/GPU/UI/process control\n";return 0;
 }catch(const std::exception& e){std::cerr<<"Booking session metadata failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}
