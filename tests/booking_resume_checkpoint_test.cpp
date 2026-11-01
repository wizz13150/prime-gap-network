#include "booking/booking_store.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <boost/multiprecision/cpp_int.hpp>
#include <sqlite3.h>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace pgn::booking;
using namespace pgn::mining;
namespace {
int checks=0;
const QString gpu="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",otherGpu="GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265";
const QString run="1234567890abcdef1234567890abcdef",otherRun="abcdef1234567890abcdef1234567890";
QString key(const QString& device,const QString& id){return "mining-resume/v1/"+device.toLower()+"/"+id;}
void require(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
class Database {
public:
 sqlite3* db{};
 explicit Database(const QString& file){if(sqlite3_open_v2(file.toUtf8().constData(),&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK)throw std::runtime_error("own SQLite fixture open failed");}
 ~Database(){if(db)sqlite3_close(db);}
 void sql(const QByteArray& value){if(sqlite3_exec(db,value.constData(),nullptr,nullptr,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
 void metadata(const QString& id,const QByteArray& raw,bool blob=false){
  sqlite3_stmt* q{};if(sqlite3_prepare_v2(db,"INSERT INTO booking_metadata(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value",-1,&q,nullptr)!=SQLITE_OK)throw std::runtime_error("fixture write prepare");
  const auto name=id.toUtf8();sqlite3_bind_text(q,1,name.constData(),name.size(),SQLITE_TRANSIENT);
  if(blob)sqlite3_bind_blob(q,2,raw.constData(),raw.size(),SQLITE_TRANSIENT);else sqlite3_bind_text(q,2,raw.constData(),raw.size(),SQLITE_TRANSIENT);
  const auto rc=sqlite3_step(q);sqlite3_finalize(q);if(rc!=SQLITE_DONE)throw std::runtime_error("fixture write failed");
 }
 QByteArray value(const QString& id){
  sqlite3_stmt* q{};if(sqlite3_prepare_v2(db,"SELECT value FROM booking_metadata WHERE key=?",-1,&q,nullptr)!=SQLITE_OK)throw std::runtime_error("fixture read prepare");
  const auto name=id.toUtf8();sqlite3_bind_text(q,1,name.constData(),name.size(),SQLITE_TRANSIENT);QByteArray raw;
  const auto rc=sqlite3_step(q);if(rc==SQLITE_ROW)raw=QByteArray(reinterpret_cast<const char*>(sqlite3_column_blob(q,0)),sqlite3_column_bytes(q,0));
  sqlite3_finalize(q);if(rc!=SQLITE_ROW&&rc!=SQLITE_DONE)throw std::runtime_error("fixture read failed");return raw;
 }
 QByteArray dump(const char* sql){
  sqlite3_stmt* q{};if(sqlite3_prepare_v2(db,sql,-1,&q,nullptr)!=SQLITE_OK)throw std::runtime_error("fixture dump prepare");QByteArray result;int rc;
  while((rc=sqlite3_step(q))==SQLITE_ROW){for(int c=0;c<sqlite3_column_count(q);++c){result+=QByteArray::number(sqlite3_column_type(q,c))+":";result+=QByteArray(reinterpret_cast<const char*>(sqlite3_column_blob(q,c)),sqlite3_column_bytes(q,c)).toHex()+";";}result+='\n';}
  sqlite3_finalize(q);if(rc!=SQLITE_DONE)throw std::runtime_error("fixture dump failed");return result;
 }
};
QByteArray metadata(Database& db){return db.dump("SELECT key,value FROM booking_metadata ORDER BY key");}
QByteArray jobs(Database& db){return db.dump("SELECT * FROM bookings ORDER BY id")+db.dump("SELECT * FROM booking_events ORDER BY sequence")+db.dump("SELECT * FROM sqlite_sequence ORDER BY name");}
QString decimal(const boost::multiprecision::cpp_int& n){return QString::fromStdString(n.str());}
StoredSourceResume fixture(const QString& id=run,quint64 positions=123,quint64 tests=9007199254740993ULL,
                          quint64 gaps=7,quint64 batches=2,quint64 time=9007199254740995ULL,quint64 low=100,quint64 length=10000){
 StoredSourceResume result;auto& r=result.resume;auto& bytes=result.checkpointBytes;bytes="PGNCG001";
 for(const auto word:{low,quint64(10),length,positions,tests,gaps,batches})for(unsigned i=0;i<8;++i)bytes.append(char((word>>(8*i))&255));
 const boost::multiprecision::cpp_int start=(boost::multiprecision::cpp_int(10)<<64)+low;
 r.run=id;r.start=decimal(start);r.end=decimal(start+length);r.length=QString::number(length);r.positions=QString::number(positions);
 r.nextStart=decimal(start+positions);r.remaining=decimal(boost::multiprecision::cpp_int(length)-positions);
 r.tests=QString::number(tests);r.gaps=QString::number(gaps);r.batches=QString::number(batches);r.counterTimeNs=QString::number(time);
 r.checkpointLabel="C:/PUBLIC-FIXTURE/current-job/checkpoint";r.checkpointSha256=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());return result;
}
bool same(const StoredSourceResume& a,const StoredSourceResume& b){
 const auto& x=a.resume;const auto& y=b.resume;
 return a.checkpointBytes==b.checkpointBytes&&x.run==y.run&&x.start==y.start&&x.end==y.end&&x.length==y.length&&x.positions==y.positions&&
 x.nextStart==y.nextStart&&x.remaining==y.remaining&&x.tests==y.tests&&x.gaps==y.gaps&&x.batches==y.batches&&x.counterTimeNs==y.counterTimeNs&&x.checkpointLabel==y.checkpointLabel&&x.checkpointSha256==y.checkpointSha256;
}
bool matches(const std::optional<StoredSourceResume>& value,const StoredSourceResume& expected){return value&&same(*value,expected);}
void reject(BookingStore& store,Database& db,const StoredSourceResume& candidate,const QString& device=gpu){
 const auto before=metadata(db),beforeJobs=jobs(db);require(!store.saveResumeCheckpoint(device,candidate.resume,candidate.checkpointBytes).isEmpty(),"invalid/rebind/regressed candidate explicitly refused");
 require(metadata(db)==before&&jobs(db)==beforeJobs,"refusal preserves all metadata/jobs/events");
}
}
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);
 try{
  QTemporaryDir temporary;require(temporary.isValid(),"own temporary directory");temporary.setAutoRemove(false);const auto file=temporary.filePath("local-bookings.sqlite");
  const auto original=fixture();StoredSourceResume progressed;QByteArray keptJobs,keptUnrelated;
  {
   BookingStore store(file);require(store.valid(),"real store/schema in own tempSQLite");Database db(file);
   db.metadata("fixture/unknown","KEEP");db.sql("INSERT INTO bookings(id,start,end,start_key,end_key,state,kind,device,created_utc) VALUES('PUBLIC-FIXTURE','1000','2000','0041000','0042000','QUEUED','LOCAL_INTENT','GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265','2026-10-14T00:00:00Z');");
   require(store.setMiningSessionMode(gpu,MiningSessionMode::ManualReservations).isEmpty()&&store.setAutomaticAfterManual(gpu,true).isEmpty(),"seed unrelated explicit mode/preference");
   keptJobs=jobs(db);keptUnrelated=metadata(db);const auto before=metadata(db);
   require(!store.savedResumeCheckpoint(gpu,run)&&!store.savedResumeCheckpoint(otherGpu,run),"absence is no resume, not zero");
   require(metadata(db)==before,"getter does not initialize a record/default/pointer");
   require(store.saveResumeCheckpoint(gpu,original.resume,original.checkpointBytes).isEmpty(),"save exact public validated-tuple-shaped payload");
   auto loaded=store.savedResumeCheckpoint(gpu.toLower(),run);require(loaded&&same(*loaded,original),"all13 strings and64 bytes roundtrip exact >2^53");
   const auto raw=db.value(key(gpu,run));require(raw.size()<=16384&&raw.contains("9007199254740993"),"bounded JSON exact integer string retained");
   const auto envelope=QJsonDocument::fromJson(raw).object();for(const auto v:envelope.value("resume").toObject())require(v.isString(),"all tuple fields JSON strings not double");
   db.sql("CREATE TRIGGER fixture_no_update BEFORE UPDATE ON booking_metadata WHEN OLD.key LIKE 'mining-resume/%' BEGIN SELECT RAISE(FAIL,'exact replay must not UPDATE'); END;");
   const auto replayBefore=metadata(db);require(store.saveResumeCheckpoint(gpu,original.resume,original.checkpointBytes).isEmpty()&&metadata(db)==replayBefore,"exact replay idempotent with UPDATE poisoned");
   db.sql("DROP TRIGGER fixture_no_update;");
   progressed=fixture(run,124,9007199254740994ULL,8,3,9007199254740996ULL);
   require(store.saveResumeCheckpoint(gpu,progressed.resume,progressed.checkpointBytes).isEmpty(),"monotonic tuple update accepted");
   require(store.saveResumeCheckpoint(gpu,fixture(otherRun).resume,fixture(otherRun).checkpointBytes).isEmpty(),"different run independent key");
   require(store.saveResumeCheckpoint(otherGpu,original.resume,original.checkpointBytes).isEmpty(),"different physical GPU independent key");
   require(matches(store.savedResumeCheckpoint(gpu,run),progressed)&&matches(store.savedResumeCheckpoint(otherGpu,run),original),"crossrun/GPU cannot overwrite old tuples");
   const auto zero=fixture("00000000000000000000000000000000",0,0,0,0,0);
   require(store.saveResumeCheckpoint(gpu,zero.resume,zero.checkpointBytes).isEmpty(),"known zero counters accepted as present");
   require(matches(store.savedResumeCheckpoint(gpu,zero.resume.run),zero),"known zero not absence");
   for(const auto& candidate:{original,fixture(run,125,9007199254740993ULL,9,4,9007199254740997ULL),fixture(run,125,9007199254740995ULL,7,4,9007199254740997ULL),
        fixture(run,125,9007199254740995ULL,9,2,9007199254740997ULL),fixture(run,125,9007199254740995ULL,9,4,9007199254740995ULL),fixture(run,125,9007199254740995ULL,9,4,9007199254740997ULL,101)})reject(store,db,candidate);
   auto bad=progressed;bad.resume.checkpointLabel+="-rebound";reject(store,db,bad);
   for(int field=0;field<13;++field){bad=progressed;
    switch(field){case 0:bad.resume.run="UPPERCASE";break;case 1:bad.resume.start="01";break;case 2:bad.resume.end+="1";break;case 3:bad.resume.length="0";break;
     case 4:bad.resume.positions="124.0";break;case 5:bad.resume.nextStart+="1";break;case 6:bad.resume.remaining+="1";break;case 7:bad.resume.tests="-1";break;
     case 8:bad.resume.gaps="1e3";break;case 9:bad.resume.batches=" 3";break;case 10:bad.resume.counterTimeNs="18446744073709551616";break;
     case 11:bad.resume.checkpointLabel.append(QChar(0));break;case 12:bad.resume.checkpointSha256="forged";break;}reject(store,db,bad);
   }
   for(const auto& device:QStringList{QString{},"GPU-not-uuid",gpu+QChar(0)})reject(store,db,progressed,device);
   for(int size:{0,63,65}){bad=progressed;bad.checkpointBytes.resize(size);reject(store,db,bad);}
   bad=progressed;bad.checkpointBytes[0]='X';bad.resume.checkpointSha256=QString::fromLatin1(QCryptographicHash::hash(bad.checkpointBytes,QCryptographicHash::Sha256).toHex());reject(store,db,bad);
   bad=progressed;bad.resume.checkpointLabel=QString(16385,'x');reject(store,db,bad);
   bad=progressed;bad.resume.checkpointLabel=QString(4000,QChar(1));reject(store,db,bad); // escaped JSON budget, not just UTF16 count
   bad=progressed;bad.resume.checkpointLabel=QString(QChar(0xd800));reject(store,db,bad);
   auto next=fixture(run,125,9007199254740995ULL,9,4,9007199254740997ULL);
   db.sql("CREATE TRIGGER fixture_after_fail AFTER UPDATE ON booking_metadata WHEN NEW.key LIKE 'mining-resume/%' BEGIN INSERT INTO booking_metadata VALUES('fixture/sideeffect','must rollback'); SELECT RAISE(FAIL,'AFTERFAIL'); END;");
   reject(store,db,next);require(matches(store.savedResumeCheckpoint(gpu,run),progressed),"AFTERFAIL restores prior checkpoint tuple and trigger sideeffect");db.sql("DROP TRIGGER fixture_after_fail;");
   db.sql("CREATE TRIGGER fixture_insert_fail AFTER INSERT ON booking_metadata WHEN NEW.key LIKE 'mining-resume/%' BEGIN INSERT INTO booking_metadata VALUES('fixture/sideeffect','must rollback'); SELECT RAISE(FAIL,'AFTERINSERTFAIL'); END;");
   reject(store,db,fixture("11111111111111111111111111111111"));db.sql("DROP TRIGGER fixture_insert_fail;");
   const auto good=db.value(key(gpu,run));
   QVector<QByteArray> poisons{"{",QByteArray(16385,'x'),QByteArray("\xff\xfe",2),good+QByteArray(1,'\0'),QByteArray("\xef\xbb\xbf",3)+good};
   auto wrong=QJsonDocument::fromJson(good).object();wrong.insert("device",otherGpu.toLower());poisons.append(QJsonDocument(wrong).toJson(QJsonDocument::Compact));
   wrong=QJsonDocument::fromJson(good).object();auto tuple=wrong.value("resume").toObject();tuple.insert("positions",124);wrong.insert("resume",tuple);poisons.append(QJsonDocument(wrong).toJson(QJsonDocument::Compact));
   wrong=QJsonDocument::fromJson(good).object();wrong.insert("checkpoint_base64","not-base64");poisons.append(QJsonDocument(wrong).toJson(QJsonDocument::Compact));
   auto duplicate=good;duplicate.insert(1,"\"schema\":\"pgn-source-resume-1\",");poisons.append(duplicate);
   for(const auto& poison:poisons){db.metadata(key(gpu,run),poison);require(!store.savedResumeCheckpoint(gpu,run),"corrupt TEXT unavailable");reject(store,db,progressed);}
   db.metadata(key(gpu,run),good,true);require(!store.savedResumeCheckpoint(gpu,run),"BLOB not interpreted as TEXT");reject(store,db,progressed);
   db.metadata(key(gpu,run),good);
   require(jobs(db)==keptJobs,"all writes/rejections preserve jobs/audit history");
   require(db.dump("SELECT key,value FROM booking_metadata WHERE key NOT LIKE 'mining-resume/%' ORDER BY key")==keptUnrelated,"modes/preferences/unknown metadata unchanged, no active pointer");
  }
  {
   BookingStore reopened(file);require(reopened.valid(),"close/reopen real sameSQLite");Database db(file);
   require(matches(reopened.savedResumeCheckpoint(gpu,run),progressed)&&matches(reopened.savedResumeCheckpoint(otherGpu,run),original),"reopen retains exact records per GPU/run");
   require(reopened.savedResumeCheckpoint(gpu,otherRun).has_value()&&!reopened.savedResumeCheckpoint(gpu,"22222222222222222222222222222222"),"other run retained, missing never guessed active");
   require(jobs(db)==keptJobs,"reopen preserves jobs/events");
  }
  std::cout<<"PASS checks="<<checks<<" failures=0 fixture="<<temporary.path().toStdString()<<"; native sameSQLite metadata only, no proof/ownership/control/mining\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL checks="<<checks<<" "<<e.what()<<'\n';return 1;}
}
