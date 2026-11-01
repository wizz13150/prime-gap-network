#include "mining/source_checkpoint_recovery.h"
#include "mining/source_resume_lineage_verifier.h"
#include "mining/native_scientific_context.h"
#include "mining/run_source_profile.h"
#include "mining/prime_gap_source_stream.h"
#include "mining/source_decimal.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <sqlite3.h>
#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace pgn::mining {
namespace {
constexpr qint64 Safe=9007199254740991LL;
constexpr qsizetype Chunk=65536,Line=32768;
const QString Scope="LOCAL_RAW_LINEAGE_COUNTER_DELTAS_NOT_INDEPENDENT_PRIME_COVERAGE";
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
QByteArray compact(const QJsonObject& o){return QJsonDocument(o).toJson(QJsonDocument::Compact);}
QString text(const QJsonObject& o,const char* key){const auto v=o.value(key);require(v.isString(),"Lineage exact string field absent");const auto s=v.toString();require(!s.contains(QChar(0))&&QString::fromUtf8(s.toUtf8())==s,"Lineage string encoding invalid");return s;}
bool flag(const QJsonObject& o,const char* key,bool b){return o.value(key).isBool()&&o.value(key).toBool()==b;}
bool match(const QString& s,const char* pattern){const auto m=QRegularExpression(QString::fromLatin1(pattern)).match(s);return m.hasMatch()&&m.capturedLength()==s.size();}
void run(const QString& s){require(match(s,"^[0-9a-f]{32}$"),"Lineage exact lowercase run32 required");}
void uuid(const QString& s){const QUuid u(s);require(!u.isNull()&&u.toString(QUuid::WithoutBraces)==s,"Lineage exact canonical UUID required");}
void sha(const QString& s){require(match(s,"^[0-9a-f]{64}$"),"Lineage exact SHA256 required");}
QString coordinate(const QString& s){require(s.size()<=160&&source_decimal::canonical(s),"Lineage canonical coordinate required");return s;}
quint64 number(const QString& s){coordinate(s);bool ok=false;const auto n=s.toULongLong(&ok);require(ok,"Lineage counter outside UInt64");return n;}
QString add(QString a,const QString& b){int carry=0;qsizetype j=b.size()-1;for(qsizetype i=a.size();i-->0;--j){const auto n=a[i].unicode()-'0'+(j>=0?b[j].unicode()-'0':0)+carry;a[i]=QChar('0'+n%10);carry=n/10;}while(j>=0){const auto n=b[j--].unicode()-'0'+carry;a.prepend(QChar('0'+n%10));carry=n/10;}if(carry)a.prepend('1');return a;}
QString startOf(quint64 low,quint64 high){QString n=QString::number(high);for(int i=0;i<64;++i)n=add(n,n);return add(n,QString::number(low));}
quint64 word(const QByteArray& b,int at){quint64 n=0;for(int i=0;i<8;++i)n|=quint64(uchar(b[at+i]))<<(8*i);return n;}
void keys(const QJsonObject& o,QStringList expected){auto actual=o.keys();actual.sort();expected.sort();require(actual==expected,"Lineage exact receipt keys differ");}
bool utc(const QString& s){const auto m=QRegularExpression("^(\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2})(?:\\.\\d{1,9})?Z$").match(s);if(!m.hasMatch()||m.capturedLength()!=s.size())return false;const auto d=QDateTime::fromString(m.captured(1)+'Z',Qt::ISODate);return d.isValid()&&d.toUTC().toString("yyyy-MM-dd'T'HH:mm:ss")==m.captured(1);}
struct Db {
 sqlite3* db{};bool begun{};
 explicit Db(const QString& file){require(QFileInfo(file).isAbsolute()&&QFileInfo(file).isFile(),"Existing absolute lineage SQLite required");const auto rc=sqlite3_open_v2(file.toUtf8().constData(),&db,SQLITE_OPEN_READONLY|SQLITE_OPEN_FULLMUTEX,nullptr);if(rc!=SQLITE_OK){if(db)sqlite3_close(db);db=nullptr;throw std::runtime_error("Read-only lineage SQLite unavailable");}sqlite3_busy_timeout(db,200);if(sqlite3_exec(db,"BEGIN",nullptr,nullptr,nullptr)!=SQLITE_OK){sqlite3_close(db);db=nullptr;throw std::runtime_error("Read-only lineage snapshot unavailable");}begun=true;}
 ~Db(){if(db){if(begun)sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);sqlite3_close(db);}}
 void finish(){require(sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr)==SQLITE_OK,"Lineage read transaction cleanup failed");begun=false;}
};
struct Query {
 sqlite3_stmt* q{};
 Query(Db& db,const char* sql){require(sqlite3_prepare_v2(db.db,sql,-1,&q,nullptr)==SQLITE_OK,"Lineage SQLite schema/query unavailable");}
 ~Query(){if(q)sqlite3_finalize(q);}
 void bind(int i,const QString& s){const auto b=s.toUtf8();require(sqlite3_bind_text(q,i,b.constData(),b.size(),SQLITE_TRANSIENT)==SQLITE_OK,"Lineage SQLite bind failed");}
 bool next(){const auto rc=sqlite3_step(q);require(rc==SQLITE_ROW||rc==SQLITE_DONE,"Lineage SQLite read failed");return rc==SQLITE_ROW;}
 QString str(int i,int limit=16384){require(sqlite3_column_type(q,i)==SQLITE_TEXT&&sqlite3_column_bytes(q,i)<=limit,"Lineage SQLite TEXT/type/bound invalid");const QByteArray b(reinterpret_cast<const char*>(sqlite3_column_text(q,i)),sqlite3_column_bytes(q,i));const auto s=QString::fromUtf8(b);require(s.toUtf8()==b&&!s.contains(QChar(0)),"Lineage SQLite UTF8 changed");return s;}
 qint64 integer(int i){require(sqlite3_column_type(q,i)==SQLITE_INTEGER,"Lineage SQLite exact INTEGER required");const auto n=sqlite3_column_int64(q,i);require(n>=0&&n<=Safe,"Lineage cursor outside exact safe offset");return n;}
};
bool table(Db& db,const QString& name){Query q(db,"SELECT type FROM sqlite_master WHERE name=?");q.bind(1,name);if(!q.next())return false;require(q.str(0)=="table"&&!q.next(),"Lineage index must be an actual unique table");return true;}
QJsonObject json(const QByteArray& b,int limit){require(!b.isEmpty()&&b.size()<=limit&&QString::fromUtf8(b).toUtf8()==b,"Lineage JSON UTF8/bound invalid");QJsonParseError e;const auto d=QJsonDocument::fromJson(b,&e);require(e.error==QJsonParseError::NoError&&d.isObject()&&d.toJson(QJsonDocument::Compact)==b,"Lineage JSON noncanonical/corrupt");return d.object();}
struct Identity {QString booking,device,start,end;std::optional<PrimeGapEngineJob> engine;};
bool manifest(const QJsonObject& m,const QString& r,const Identity& id){
  const bool origin=id.booking.isEmpty()&&nativeScientificSearchOrigin(m);
  require(compact(m).size()<=16384&&m.value("schema")=="pgn-native-process-1"&&text(m,"run")==r&&text(m,"uuid")==id.device&&(origin||text(m,"queued_intent")==id.booking)&&text(m,"start")==id.start&&text(m,"end")==id.end,"Actual source manifest identity/range mismatch");run(r);
 const auto pid=m.value("pid");require(pid.isDouble()&&pid.toInteger(-1)>0&&pid.toInteger()<=4294967295LL&&pid.toDouble()==double(pid.toInteger())&&utc(text(m,"birth")),"Actual source manifest PID/birth invalid");require(!text(m,"stdout").isEmpty(),"Actual source manifest stdout absent");
 require(inspectRunSourceProfile(m).status!=RunSourceProfileStatus::Invalid,"Actual immutable source profile invalid or unknown; never downgrade to historical absence");
  const auto selected=inspectRunSourceProfile(m).engineJob;require(bool(selected)==bool(id.engine),"Lineage cannot change native checkpoint dialect between epochs");
  if(selected)require(selected->descriptorK==id.engine->descriptorK&&selected->minimumGap==id.engine->minimumGap&&selected->start==id.engine->start&&selected->end==id.engine->end,"Lineage cannot change native K/threshold/range between epochs");
 const bool marked=m.contains("resume_previous_run");require(marked==m.contains("checkpoint_input_sha256"),"Actual source manifest half resume marker");if(marked){run(text(m,"resume_previous_run"));require(text(m,"resume_previous_run")!=r,"Self-referential source epoch");sha(text(m,"checkpoint_input_sha256"));}return marked;
}
struct Counters {quint64 time{},positions{},gaps{},tests{},batches{};};
QJsonObject counters(const Counters& c){return {{"time",QString::number(c.time)},{"positions",QString::number(c.positions)},{"gaps",QString::number(c.gaps)},{"tests",QString::number(c.tests)},{"batches",QString::number(c.batches)}};}
struct Expected {QString kind,label;Counters c;bool batches{},time{};QByteArray tail;QString checkpointSha;QString recoveredSourceSha,recoveredImageSha;};
struct Checkpoint {QJsonObject object,tuple;Counters c;QString previous,label,hash;};
Checkpoint checkpoint(const QJsonObject& cp,const Identity& id,const QString& previous){
 keys(cp,{"schema","device","resume","checkpoint_base64"});require(compact(cp).size()<=16384&&cp.value("schema")=="pgn-source-resume-1"&&cp.value("device")==id.device.toLower()&&cp.value("resume").isObject(),"Lineage checkpoint schema/device differs");
 Checkpoint result;result.object=cp;result.tuple=cp.value("resume").toObject();const auto& r=result.tuple;
 keys(r,{"run","start","end","length","positions","nextStart","remaining","tests","gaps","batches","counterTimeNs","checkpointLabel","checkpointSha256"});for(auto i=r.begin();i!=r.end();++i)text(r,i.key().toLatin1().constData());
 result.previous=text(r,"run");run(result.previous);require(result.previous==previous&&text(r,"start")==id.start&&text(r,"end")==id.end,"Lineage checkpoint original run/range differs");
 const auto encoded=text(cp,"checkpoint_base64").toLatin1(),bytes=QByteArray::fromBase64(encoded);require(encoded.size()==88&&bytes.size()==64&&(id.engine?(bytes.left(4)=="PGNS"&&quint32(word(bytes,0)>>32)==id.engine->minimumGap):bytes.left(8)=="PGNCG001")&&bytes.toBase64()==encoded,"Lineage exact64 PGNCG001 checkpoint required");
 result.hash=text(r,"checkpointSha256");sha(result.hash);require(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()==result.hash.toLatin1(),"Lineage checkpoint SHA differs from bytes");
 const auto low=word(bytes,8),high=word(bytes,16),length=word(bytes,24),positions=word(bytes,32);require((id.engine?(high==id.engine->highWord&&low==id.engine->lowWord&&length==id.engine->positions):high==10)&&length>0&&length<=std::numeric_limits<quint64>::max()-low&&positions<length,"Lineage checkpoint outside declared engine/low64 source profile");
 const auto resumeLow=low+positions;require(resumeLow>=(resumeLow%30+(high%30)*16)%30,"Lineage checkpoint resume floor crosses low64");
 result.c={number(text(r,"counterTimeNs")),positions,word(bytes,48),word(bytes,40),word(bytes,56)};
 require(startOf(low,high)==id.start&&add(id.start,QString::number(length))==id.end&&text(r,"length")==QString::number(length)&&text(r,"positions")==QString::number(positions)&&text(r,"nextStart")==add(id.start,QString::number(positions))&&text(r,"remaining")==QString::number(length-positions)&&text(r,"gaps")==QString::number(result.c.gaps)&&text(r,"tests")==QString::number(result.c.tests)&&text(r,"batches")==QString::number(result.c.batches),"Lineage checkpoint exact bytes/cumulative tuple differ");
 result.label=text(r,"checkpointLabel");require(!result.label.isEmpty()&&!result.label.contains('\r')&&!result.label.contains('\n'),"Lineage checkpoint label invalid");return result;
}
struct Epoch {QJsonObject object;Checkpoint cp;QString previous,request;QJsonValue predecessor;std::optional<checkpoint_recovery::Evidence> recovery;};
std::optional<Epoch> epoch(Db& db,const QString& r,const Identity& id){
 if(!table(db,"dispatch_resume_epochs"))return {};Query q(db,"SELECT run,previous_run,booking_id,request_id,receipt FROM dispatch_resume_epochs WHERE run=? OR run=CAST(? AS BLOB) LIMIT 2");q.bind(1,r);q.bind(2,r);if(!q.next())return {};
 Epoch e;e.object=json(q.str(4,24576).toUtf8(),24576);const auto& o=e.object;QStringList names{"schema","origin","request_id","device","booking_id","previous_run","run","observation_id","predecessor","counter_basis","raw_conservation_requirement","checkpoint"};if(o.value("origin")==checkpoint_recovery::Origin)names<<"recovery";keys(o,names);
 require(o.value("schema")=="pgn-checkpoint-resume-epoch-1"&&(o.value("origin")=="RESUME_CHECKPOINT"||o.value("origin")==checkpoint_recovery::Origin)&&o.value("counter_basis")== (id.engine?"PGNS_GAP32_64B_CUMULATIVE_BASELINE":"PGNCG001_CUMULATIVE_BASELINE")&&o.value("raw_conservation_requirement")=="LINEAGE_RAW_CONSERVATION_REQUIRED"&&text(o,"device")==id.device&&text(o,"booking_id")==id.booking&&text(o,"run")==r,"Lineage epoch schema/basis/identity differs");
 e.previous=text(o,"previous_run");run(e.previous);require(e.previous!=r,"Lineage epoch must reserve a different run");e.request=text(o,"request_id");uuid(e.request);uuid(text(o,"observation_id"));e.predecessor=o.value("predecessor");require(e.predecessor.isNull()||e.predecessor.isString(),"Lineage epoch predecessor type invalid");if(e.predecessor.isString())run(e.predecessor.toString());
 require(q.str(0)==r&&q.str(1)==e.previous&&q.str(2)==id.booking&&q.str(3)==e.request&&!q.next(),"Lineage epoch indexed row/receipt differs");require(o.value("checkpoint").isObject(),"Lineage checkpoint object absent");e.cp=checkpoint(o.value("checkpoint").toObject(),id,e.previous);
 if(o.value("origin")==checkpoint_recovery::Origin)e.recovery=checkpoint_recovery::validate(o.value("recovery").toObject(),e.previous,r,e.cp.hash,QByteArray::fromBase64(e.cp.object.value("checkpoint_base64").toString().toLatin1()));
 Query unique(db,"SELECT run FROM dispatch_resume_epochs WHERE previous_run=? OR previous_run=CAST(? AS BLOB) OR request_id=? OR request_id=CAST(? AS BLOB) LIMIT 2");unique.bind(1,e.previous);unique.bind(2,e.previous);unique.bind(3,e.request);unique.bind(4,e.request);require(unique.next()&&unique.str(0)==r&&!unique.next(),"Lineage epoch request/previous index ambiguous");return e;
}
Expected capture(const QJsonObject& snapshot,const Identity& id,const QString& r){
 const auto last=snapshot.value("last_exit").toObject();const auto exit=last.value("exit_code");require(snapshot.value("state")=="OFF"&&flag(snapshot,"owned",false)&&last.value("run")==r&&exit.isDouble()&&exit.toDouble()==0,"Lineage requires known successful OFF source (caller pays freshness)");
 std::optional<Expected> done;
 for(const auto* name:{"completion","completion_candidate","source_completion_observation"}){
  const auto v=snapshot.value(name);if(v.isUndefined()||v.isNull())continue;require(v.isObject(),"Lineage completion receipt malformed");const auto c=v.toObject();
  // PS deliberately retains last-completion from another epoch. Its exact,
  // valid foreign run scopes it as HISTORY, not as current completion proof.
  // Never relabel/delete it, and never hide an absent/malformed run identity.
  const auto receiptRun=text(c,"run");run(receiptRun);if(receiptRun!=r)continue;
   const auto queued=c.value("queued_intent");
   const bool sameIdentity=id.booking.isEmpty()?(queued.isNull()||(queued.isString()&&queued.toString().isEmpty())):queued==id.booking;
   require(c.value("schema")=="pgn-source-range-completion-1"&&c.value("run")==r&&sameIdentity&&c.value("start")==id.start&&c.value("end")==id.end&&flag(c,"source_range_complete",true)&&flag(c,"independent_coverage",false)&&c.value("proof_status")=="SOURCE_PRP_INDEPENDENT_PROOF_PENDING"&&c.value("source_exit_code").isDouble()&&c.value("source_exit_code").toDouble()==0&&flag(c,"source_tests_complete",true)&&c.value("source_position_evidence")=="final_counter"&&c.value("source")==snapshot.value("manifest").toObject().value("stdout"),"Lineage exact SOURCE completion receipt differs");
  Expected e;e.kind="complete";e.c.positions=number(text(c,"source_positions"));e.c.gaps=number(text(c,"source_session_gaps"));e.c.tests=number(text(c,"source_tests"));require(QString::number(e.c.positions)==source_decimal::difference(id.end,id.start),"Lineage SOURCE positions not complete");if(done)require(done->c.positions==e.c.positions&&done->c.gaps==e.c.gaps&&done->c.tests==e.c.tests,"Lineage contradictory SOURCE receipts");done=e;
 }
 if(done)return *done;
 const auto c=snapshot.value("checkpoint_evidence").toObject();require(c.value("schema")=="pgn-stop-checkpoint-evidence-1"&&c.value("status")=="captured"&&c.value("run")==r,"Lineage stopped checkpoint capture absent");
 const auto encoded=text(c,"checkpoint_base64").toLatin1(),bytes=QByteArray::fromBase64(encoded);require(encoded.size()==88&&bytes.size()==64&&bytes.toBase64()==encoded&&(id.engine?(bytes.left(4)=="PGNS"&&quint32(word(bytes,0)>>32)==id.engine->minimumGap):bytes.left(8)=="PGNCG001"),"Lineage exact stopped64 capture required");
  const auto low=word(bytes,8),high=word(bytes,16),length=word(bytes,24),positions=word(bytes,32);require((id.engine?(high==id.engine->highWord&&low==id.engine->lowWord&&length==id.engine->positions):high==10)&&length&&length<=std::numeric_limits<quint64>::max()-low&&positions<length&&startOf(low,high)==id.start&&add(id.start,QString::number(length))==id.end,"Lineage stopped capture range differs");
 Expected e;e.kind="stopped";e.c={0,positions,word(bytes,48),word(bytes,40),word(bytes,56)};e.batches=true;e.checkpointSha=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());e.label=text(c,"checkpoint_label");require(!e.label.isEmpty()&&!e.label.contains('\r')&&!e.label.contains('\n'),"Lineage stopped label invalid");e.tail=text(c,"source_tail_utf8").toUtf8();require(!e.tail.isEmpty()&&e.tail.size()<=Chunk&&e.tail.endsWith('\n'),"Lineage stopped tail absent/partial/oversized");return e;
}
struct Cursor {QString source;QJsonObject manifest;qint64 offset;};
Cursor cursor(Db& history,Db& booking,const QString& r,const Identity& id){
 const auto source="native/"+r+".stdout";Query q(history,"SELECT source,run,offset FROM cursors WHERE source=? OR source=CAST(? AS BLOB) LIMIT 2");q.bind(1,source);q.bind(2,source);require(q.next(),"Exact conserved history cursor absent");Cursor c;c.source=q.str(0);require(c.source==source&&q.str(1)==r,"Lineage cursor source/run differs");c.offset=q.integer(2);require(!q.next(),"Lineage cursor source ambiguous");
 Query m(booking,"SELECT key,value FROM booking_metadata WHERE key=? OR key=CAST(? AS BLOB) LIMIT 2");const auto key="mining-source-manifest/v1/"+r;m.bind(1,key);m.bind(2,key);require(m.next()&&m.str(0)==key,"Actual immutable source manifest absent");c.manifest=json(m.str(1).toUtf8(),16384);require(!m.next(),"Actual immutable manifest key ambiguous");manifest(c.manifest,r,id);return c;
}
void sourceIdentity(Db& history,const QString& r,const QJsonObject& manifest){
 Query q(history,"SELECT run,identity FROM native_source_identities WHERE run=? OR run=CAST(? AS BLOB) LIMIT 2");q.bind(1,r);q.bind(2,r);require(q.next()&&q.str(0)==r,"HeldNonEnrolled: original native source identity absent");const auto identity=json(q.str(1,4096).toUtf8(),4096);require(!q.next(),"Native source identity index ambiguous");
 const QJsonObject expected{{"run",r},{"uuid",text(manifest,"uuid").toLower()},{"start",text(manifest,"start")},{"end",text(manifest,"end")},{"pid",manifest.value("pid")},{"birth",text(manifest,"birth")}};
 require(compact(identity)==compact(expected),"Native committed source identity differs from actual immutable manifest");
}
QJsonValue relations(Db& db,const QString& r,const Identity& id,bool current,Expected& expected){
 const auto rangeKey=[](const QString& s){return QString::number(s.size()).rightJustified(3,'0')+s;};
 const auto row=[&](const QString& key,const QString& kind){Query q(db,"SELECT start,end,device,kind,state,start_key,end_key FROM bookings WHERE id=?");q.bind(1,key);require(q.next()&&q.str(0)==id.start&&q.str(1)==id.end&&q.str(2)==id.device&&q.str(3)==kind&&q.str(5)==rangeKey(id.start)&&q.str(6)==rangeKey(id.end),"Lineage existing physical/local identity differs");const auto state=q.str(4);require(!q.next(),"Lineage physical/local identity ambiguous");return state;};
  const auto physical=row(r,"RUNTIME");
  if(id.booking.isEmpty()){
   // Native search provenance is not a synthetic community dispatch. This
   // path conserves one observed origin; it cannot authorize a resume epoch.
   require(current&&(physical=="PAUSED"||physical=="ACTIVE"||physical=="SOURCE_FINISHED")&&!epoch(db,r,id),"Native search origin physical state/epoch differs");
   return QJsonValue(QJsonValue::Null);
  }
  const auto local=row(id.booking,"LOCAL_INTENT");
 // The current clean terminal RAW is the evidence used to settle ACTIVE to
 // SOURCE_FINISHED. Requiring that SQL settlement BEFORE this readonly proof
 // makes the first completion circular. Ancestors still MUST be PAUSED.
 // capture() already requires current OFF, exit0, exact range/counters/EOF;
 // the full held RAW scan below remains mandatory before any ready result.
 const bool complete=expected.kind=="complete",firstCompletion=current&&complete&&physical=="ACTIVE";
 require(physical=="PAUSED"||(current&&complete&&physical=="SOURCE_FINISHED")||firstCompletion,"Lineage ancestor not PAUSED or current physical state unknown");
 Query q(db,"SELECT id,start,end,device,state,run,predecessor FROM dispatch_requests WHERE id=?");q.bind(1,id.booking);require(q.next()&&q.str(0)==id.booking&&q.str(1)==id.start&&q.str(2)==id.end&&q.str(3)==id.device,"Lineage original dispatch absent/rebound");const auto state=q.str(4),active=q.str(5);run(active);
 QJsonValue predecessor(QJsonValue::Null);if(sqlite3_column_type(q.q,6)!=SQLITE_NULL){predecessor=q.str(6);run(predecessor.toString());}require(!q.next(),"Lineage dispatch ambiguous");
 if(current&&active!=r){
  // Admission reserves the successor BEFORE physical submission. A refused
  // submission must still be able to re-prove the stopped parent's RAW for
  // reconciliation of that SAME immutable epoch; it cannot allocate another.
  require(!complete&&physical=="PAUSED"&&state=="PREPARED"&&local=="DISPATCHING","Lineage current dispatch points to another run");
  const auto pending=epoch(db,active,id);
  require(pending&&pending->previous==r&&pending->predecessor==predecessor&&
      pending->cp.hash==expected.checkpointSha&&pending->cp.label==expected.label,
      "Lineage prepared successor differs from exact stopped checkpoint");
  Query observed(db,"SELECT 1 FROM bookings WHERE id=? UNION ALL SELECT 1 FROM booking_metadata WHERE key=? LIMIT 1");observed.bind(1,active);observed.bind(2,"mining-source-manifest/v1/"+active);
  require(!observed.next(),"Lineage prepared successor already has physical provenance");
  // The checkpoint bytes omit counter time. Verify the retained receipt's
  // time against the actual terminal RAW as well, never trust its tuple alone.
  expected.c.time=pending->cp.c.time;expected.time=true;
 }
 require((state=="PREPARED"&&local=="DISPATCHING")||(state=="STARTED"&&local=="DISPATCHED")||(state=="SOURCE_FINISHED"&&local=="SOURCE_FINISHED"),"Lineage local/dispatch states inconsistent");
 if(firstCompletion)require(state=="STARTED"&&local=="DISPATCHED","First SOURCE completion requires actual STARTED/DISPATCHED identity, not a PREPARED launch");
 if(current)require((state=="SOURCE_FINISHED")== (physical=="SOURCE_FINISHED"),"Lineage current physical/dispatch completion inconsistent");
 return predecessor;
}
QJsonObject scan(Db& history,HeldSourceLineageReader& reader,const QString& r,const Cursor& cursor,const Identity& id,const Counters& base,const Expected& expected,const std::function<bool()>& cancelled){
 require(reader.run()==r&&reader.source()==cursor.source&&reader.isRegularFile()&&!reader.isSymbolicLink(),"Lineage held regular/nonsymlink reader identity invalid");const auto size=reader.size();require(size>0&&size<=Safe&&size==cursor.offset,"Lineage raw size must equal complete committed cursor");
 qint64 at=0;QByteArray pending,tail;quint64 records=0;int headers=0;std::optional<Counters> last;QString terminal;QCryptographicHash hash(QCryptographicHash::Sha256);const auto length=number(source_decimal::difference(id.end,id.start));
 struct BoundTerminal {quint64 descriptor{};QString first,end,p,q;bool pair{};};
 std::optional<BoundTerminal> boundary;
 quint64 declaredDescriptors{},boundGroups{},groupRecords{};bool declaredV2{};
  std::optional<PrimeGapSourceStream> adaptive;if(id.engine)adaptive.emplace(*id.engine,PrimeGapSourceCounters{base.time,base.positions,base.gaps,base.tests,base.batches});
 QString minimumLower,maximumLower,maximumUpper,maximumUpperLower,secondUpper;
 // Bind the SAME bytes to SourceHistory's committed prefix, not merely its
 // cursor number. A legacy unenrolled prefix is unavailable, never adopted.
 {Query shadow(history,"SELECT 1 FROM native_source_batches WHERE source=CAST(? AS BLOB) LIMIT 1");shadow.bind(1,cursor.source);require(!shadow.next(),"Native source prefix has non-TEXT shadow source");}
 Query batches(history,"SELECT source,offset,length,sha256 FROM native_source_batches WHERE source=? ORDER BY offset");batches.bind(1,cursor.source);
 qint64 hashed=0,remaining=0;QString batchDigest;QCryptographicHash batchHash(QCryptographicHash::Sha256);
 const auto committedBytes=[&](const QByteArray& bytes){qsizetype from=0;while(from<bytes.size()){
  // Enrollment spans complete rows (prior fragment + transport chunk), not
  // individual I/O reads. Stream its digest across bounded Chunk reads; count
  // remains an exact scalar constrained to the remaining committed prefix.
  if(!remaining){require(batches.next()&&batches.str(0)==cursor.source,"Legacy/incomplete native source prefix enrollment");const auto begin=batches.integer(1),count=batches.integer(2);batchDigest=batches.str(3,64);sha(batchDigest);require(begin==hashed&&count>0&&count<=size-hashed,"Native source prefix batch coverage differs from cursor");remaining=count;batchHash.reset();}
  const auto take=qsizetype(std::min<qint64>(remaining,bytes.size()-from));batchHash.addData(bytes.mid(from,take));from+=take;remaining-=take;hashed+=take;if(!remaining)require(batchHash.result().toHex()==batchDigest.toLatin1(),"Native committed source prefix SHA differs from held raw bytes");
 }};
 // Immutable protocol grammar, not a data-dependent cache. Compile once rather
 // than recompiling/JITing the same expressions for every conserved RAW row.
 static const QRegularExpression gapRowPattern("^\\[phase0-gpu\\] GAP merit=([0-9]+\\.[0-9]{4}) gap=([0-9]+) lower=([0-9]+) upper=([0-9]+) verified=1 proof=GPU_MR12_EULER_COMPOSITE record=off table=unknown(?: unix_ts=([1-9][0-9]*) time_basis=source_batch_publication| time_basis=source_batch_publication time_status=unavailable)?$");
 static const QRegularExpression counterRowPattern("^\\[pgn-counters\\] time_ns=([0-9]+) positions=([0-9]+) gaps=([0-9]+) tests=([0-9]+) batches=([0-9]+) processed_gaps=([0-9]+)$");
 static const QRegularExpression headerRowPattern("^\\[phase0-gpu\\] engine=GPU_OWNED_CONTINUOUS descriptors=([0-9]+) original_start=([0-9]+) length=([0-9]+) proof=GPU_MR12_GAP coverage=SOURCE_PRP_INDEPENDENT_PROOF_PENDING(?: boundary_protocol=(pgn-source-boundary-v2))?$");
 static const QRegularExpression boundaryRowPattern("^\\[pgn-source-boundary-v2\\] descriptor=([0-9]+) owned_first=([0-9]+) owned_end=([0-9]+) status=([12]) terminal_p=([0-9]+|none) terminal_q=([0-9]+|none) examined=([0-9]+) source_tests=([0-9]+) scope=SOURCE_PRP_TERMINAL_NOT_INDEPENDENT_COVERAGE$");
 const auto row=[&](QByteArray raw){
  require(raw.size()<=Line,"Lineage source row exceeds32768");if(raw.endsWith('\r'))raw.chop(1);const auto s=QString::fromUtf8(raw);require(s.toUtf8()==raw&&!raw.contains('\0')&&!raw.contains('\r'),"Lineage raw UTF8/NUL/CR invalid");if(s.isEmpty())return;
   if(adaptive){adaptive->row(raw);return;}
  if(!terminal.isEmpty()){require(terminal=="summary"&&s=="[phase0-gpu] --- end ---","Data after terminal source footer");terminal="complete";return;}
  if(s.startsWith("[phase0-gpu] engine=")){const auto m=headerRowPattern.match(s);require(m.hasMatch()&&++headers==1&&!records&&!last&&number(m.captured(1))>0&&m.captured(2)==id.start&&number(m.captured(3))==length,"Lineage producer header/range differs");declaredDescriptors=number(m.captured(1));declaredV2=!m.captured(4).isEmpty();if(declaredV2)require(declaredDescriptors<=UINT32_MAX,"Lineage v2 descriptor count outside UInt32");return;}
  if(s.startsWith("[phase0-gpu] GAP")){
   require(headers==1&&!boundary,"Lineage GAP before header or after terminal marker");const auto m=gapRowPattern.match(s);require(m.hasMatch(),"Lineage malformed source GAP/profile");const auto gap=number(m.captured(2));const auto p=coordinate(m.captured(3)),q=coordinate(m.captured(4));require(gap>0&&gap<=4294967295ULL&&!source_decimal::less(p,id.start)&&source_decimal::less(p,id.end)&&source_decimal::less(p,q)&&source_decimal::difference(q,p)==QString::number(gap),"Lineage source GAP endpoint/width/range differs");
   if(!declaredV2)require(source_decimal::less(q,id.end),"Lineage source GAP endpoint/width/range differs");
   else{
    require(gap>=800,"Lineage v2 GAP below actual producer gap-min800");
    // The marker follows the records, so its owned_end is not known yet.
    // Two greatest upper endpoints suffice to prove at most one crossing at
    // ANY descriptor boundary, including internal ones, in O(1) state. Equal
    // upper endpoints count twice: a duplicate crossing is not paid twice.
    if(maximumUpper.isEmpty()||!source_decimal::less(q,maximumUpper)){secondUpper=maximumUpper;maximumUpper=q;maximumUpperLower=p;}
    else if(secondUpper.isEmpty()||source_decimal::less(secondUpper,q))secondUpper=q;
   }
   if(minimumLower.isEmpty()||source_decimal::less(p,minimumLower))minimumLower=p;if(maximumLower.isEmpty()||source_decimal::less(maximumLower,p))maximumLower=p;
   if(!m.captured(5).isEmpty())number(m.captured(5));require(records<std::numeric_limits<quint64>::max(),"Lineage raw record count overflow");++records;++groupRecords;return;
  }
  if(s.startsWith("[pgn-source-boundary")){
   require(headers==1&&declaredV2&&!boundary,"Lineage undeclared, legacy or duplicate terminal boundary marker");
   const auto m=boundaryRowPattern.match(s);require(m.hasMatch(),"Lineage malformed terminal boundary/profile");
   const auto coordinate128=[](const QString& text){const auto n=coordinate(text);require(!source_decimal::less(QStringLiteral("340282366920938463463374607431768211455"),n),"Lineage terminal coordinate outside UInt128");return n;};
   BoundTerminal b;b.descriptor=number(m.captured(1));b.first=coordinate128(m.captured(2));b.end=coordinate128(m.captured(3));b.pair=m.captured(4)=="1";
   require(b.descriptor<=UINT32_MAX&&b.descriptor==boundGroups&&b.descriptor<declaredDescriptors&&!source_decimal::less(b.first,id.start)&&source_decimal::less(b.first,b.end)&&!source_decimal::less(id.end,b.end),"Lineage terminal descriptor/owned range differs");
   if(b.pair){b.p=coordinate128(m.captured(5));b.q=coordinate128(m.captured(6));require(!source_decimal::less(b.p,b.first)&&source_decimal::less(b.p,b.end)&&!source_decimal::less(b.q,b.end)&&source_decimal::less(b.p,b.q)&&number(source_decimal::difference(b.q,b.p))<=UINT32_MAX,"Lineage terminal pair ownership/width differs");}
   else require(m.captured(5)=="none"&&m.captured(6)=="none"&&groupRecords==0,"Lineage NoOwnedPrime has endpoints or GAP records");
   const auto examined=number(m.captured(7)),sourceTests=number(m.captured(8));
   // Exact producer step(): backward probes end-1..p, then forward p+1..q.
   // source_tests counts terminal PRP survivors, NOT cumulative walk.tests.
   // No independent primality or performance evidence is inferred here.
   require(sourceTests<=examined,"Lineage terminal PRP probes exceed examined positions");
   if(b.pair)require(examined==number(source_decimal::difference(b.end,b.p))+number(source_decimal::difference(b.q,b.p))&&sourceTests>=2,"Lineage terminal Pair probe tuple differs from producer");
   else require(examined==number(source_decimal::difference(b.end,b.first)),"Lineage NoOwnedPrime probes differ from exact owned span");
   boundary=std::move(b);return;
  }
  if(s.startsWith("[pgn-counters]")){
   require(headers==1,"Lineage counter before header");const auto m=counterRowPattern.match(s);require(m.hasMatch(),"Lineage malformed source counter");const Counters c{number(m.captured(1)),number(m.captured(2)),number(m.captured(3)),number(m.captured(4)),number(m.captured(5))};require(number(m.captured(6))==c.gaps&&c.positions<=length&&records<=std::numeric_limits<quint64>::max()-base.gaps&&c.gaps==base.gaps+records,"Lineage cumulative gaps differ from raw new-record DELTA");
   require(c.positions>=base.positions&&c.gaps>=base.gaps&&c.tests>=base.tests&&c.batches>=base.batches&&(!last||(c.positions>=last->positions&&c.gaps>=last->gaps&&c.tests>=last->tests&&c.batches>=last->batches&&c.time>=last->time)),"Lineage cumulative counters regressed");
   if(declaredV2){
    const auto before=last.value_or(base);
    if(boundary){
     require(before.batches<std::numeric_limits<quint64>::max()&&c.batches==before.batches+1&&c.batches-base.batches==boundGroups+1&&c.positions>before.positions,"Lineage terminal counter does not commit one descriptor");
     require(boundary->first==add(id.start,QString::number(before.positions))&&boundary->end==add(id.start,QString::number(c.positions)),"Lineage terminal owned bounds do not match counter positions");
     if(groupRecords)require(!source_decimal::less(minimumLower,boundary->first)&&source_decimal::less(maximumLower,boundary->end),"Lineage GAP lower lies outside descriptor ownership");
     if(boundary->pair){
      if(groupRecords)require(!source_decimal::less(boundary->p,maximumLower),"Lineage GAP lower exceeds terminal owned prime");
      if(number(source_decimal::difference(boundary->q,boundary->p))>=800)require(maximumUpperLower==boundary->p&&maximumUpper==boundary->q,"Lineage gap-min800 terminal Pair missing from its own group");
     }
     if(!maximumUpper.isEmpty()&&!source_decimal::less(maximumUpper,boundary->end))require(boundary->pair&&maximumUpperLower==boundary->p&&maximumUpper==boundary->q&&(secondUpper.isEmpty()||source_decimal::less(secondUpper,boundary->end)),"Lineage crossing GAP is not the unique exact same-group terminal pair");
     ++boundGroups;
    }else require(!groupRecords&&maximumUpper.isEmpty()&&c.positions==before.positions&&c.gaps==before.gaps&&c.tests==before.tests&&c.batches==before.batches,"Lineage v2 progress lacks its terminal publication");
   }
   boundary.reset();groupRecords=0;minimumLower.clear();maximumLower.clear();maximumUpper.clear();maximumUpperLower.clear();secondUpper.clear();last=c;return;
  }
  if(s.startsWith("[phase0-gpu] stopped")){const auto m=QRegularExpression("^\\[phase0-gpu\\] stopped positions=([0-9]+) checkpoint=(.+)$").match(s);require(m.hasMatch()&&last&&expected.kind=="stopped"&&number(m.captured(1))==expected.c.positions&&m.captured(2)==expected.label,"Lineage stopped footer/checkpoint differs");terminal="stopped";return;}
  if(s.startsWith("[phase0-gpu] gaps reported=")||s=="[phase0-gpu] --- end ---"){const auto m=QRegularExpression("^\\[phase0-gpu\\] gaps reported=([0-9]+) verification_failures=0 records_new=0$").match(s);require(m.hasMatch()&&last&&expected.kind=="complete"&&number(m.captured(1))==last->gaps,"Lineage complete footer differs");terminal="summary";}
 };
 while(at<size){require(!cancelled||!cancelled(),"Lineage verification cancelled");const auto wanted=qsizetype(std::min<qint64>(Chunk,size-at));const auto bytes=reader.read(at,wanted);require(!bytes.isEmpty()&&bytes.size()<=wanted,"Lineage bounded source read failed/oversized");committedBytes(bytes);hash.addData(bytes);at+=bytes.size();if(!expected.tail.isEmpty()){tail+=bytes;if(tail.size()>expected.tail.size())tail=tail.right(expected.tail.size());}qsizetype begin=0;for(;;){const auto newline=bytes.indexOf('\n',begin);if(newline<0)break;require(pending.size()+newline-begin<=Line,"Lineage source row exceeds32768");pending+=bytes.mid(begin,newline-begin);row(pending);pending.clear();begin=newline+1;}pending+=bytes.mid(begin);require(pending.size()<=Line,"Lineage source row exceeds32768");}
 require(!remaining&&hashed==size&&!batches.next(),"Native source prefix does not end at exact cursor");
 if(adaptive){
   require(pending.isEmpty()&&(expected.kind=="complete"||expected.kind=="stopped"),"Adaptive source EOF/terminal basis differs");
   adaptive->finish(expected.kind=="complete",expected.label);const auto& c=adaptive->counters();
   require(reader.size()==size&&c.positions==expected.c.positions&&c.gaps==expected.c.gaps&&c.tests==expected.c.tests,"Adaptive final raw tuple/checkpoint/receipt differs");
   if(expected.batches)require(c.batches==expected.c.batches,"Adaptive checkpoint batches differ");
   if(expected.time)require(c.time==expected.c.time,"Adaptive checkpoint counter time differs");
   if(!expected.tail.isEmpty())require(tail==expected.tail,"Adaptive capture tail is not exact raw EOF suffix");
   return {{"size",QString::number(size)},{"source_sha256",QString::fromLatin1(hash.result().toHex())},
    {"new_records",QString::number(adaptive->newRecords())},{"final",counters(Counters{c.time,c.positions,c.gaps,c.tests,c.batches})},
    {"source_boundary_protocol","PGN_NATIVE_GAP_RUNTIME_V1"},{"source_boundary_groups",QString::number(c.batches-base.batches)}};
  }
  require(pending.isEmpty()&&headers==1&&last,"Lineage incomplete source row/header/counter");require(reader.size()==size,"Lineage held source size changed");if(expected.kind=="recovered_checkpoint"){
 require(terminal.isEmpty()&&!expected.recoveredSourceSha.isEmpty()&&hash.result().toHex()==expected.recoveredSourceSha.toLatin1(),"Recovered checkpoint requires exact counter-only EOF/hash, no fabricated footer");terminal=expected.kind;
 }
 require(terminal==expected.kind,"Lineage one exact terminal footer required");require(last->positions==expected.c.positions&&last->gaps==expected.c.gaps&&last->tests==expected.c.tests&&records<=std::numeric_limits<quint64>::max()-base.gaps&&last->gaps==base.gaps+records,"Lineage final raw counter/checkpoint/receipt differs");if(expected.batches)require(last->batches==expected.c.batches,"Lineage checkpoint batches differ");if(expected.time)require(last->time==expected.c.time,"Lineage original checkpoint counter time differs");if(!expected.tail.isEmpty())require(tail==expected.tail,"Lineage captured tail is not exact raw EOF suffix");
 require(!boundary&&maximumUpper.isEmpty()&&!groupRecords,"Lineage uncommitted terminal publication at EOF");
 if(declaredV2&&terminal=="complete")require(boundGroups==declaredDescriptors,"Lineage complete v2 descriptor publications are incomplete");
 QJsonObject result{{"size",QString::number(size)},{"source_sha256",QString::fromLatin1(hash.result().toHex())},{"new_records",QString::number(records)},{"final",counters(*last)}};
 if(declaredV2){result.insert("source_boundary_protocol","pgn-source-boundary-v2");result.insert("source_boundary_groups",QString::number(boundGroups));}return result;
}
}
SourceResumeLineageVerifier::SourceResumeLineageVerifier(QString booking,QString history,OpenHeldLineageSource open,std::function<bool()> cancelled):bookingDatabase_(std::move(booking)),historyDatabase_(std::move(history)),open_(std::move(open)),cancelled_(std::move(cancelled)){}
QJsonObject SourceResumeLineageVerifier::verify(const QJsonObject& snapshot) const noexcept {
 QJsonObject proof{{"schema","pgn-source-resume-lineage-1"},{"ready",false},{"raw_lineage_ready",false},{"run",""},{"booking_id",""},{"start",""},{"end",""},{"epochs",0},{"chain_sha256",""},{"scope",Scope},{"terminal","unknown"},{"error",""}};
 try{
  require(!cancelled_||!cancelled_(),"Lineage verification cancelled");require(bool(open_)&&QFileInfo(bookingDatabase_).canonicalFilePath()!=QFileInfo(historyDatabase_).canonicalFilePath(),"Separate lineage SQLite/read adapter required");require(snapshot.value("schema")=="pgn-runtime-snapshot-1","Actual source snapshot schema absent");const auto m=snapshot.value("manifest").toObject();const auto current=text(m,"run");run(current);proof.insert("run",current);
  const bool nativeOrigin=m.value("queued_intent").isNull()&&nativeScientificSearchOrigin(m);
  Identity id{nativeOrigin?QString{}:text(m,"queued_intent"),text(m,"uuid"),coordinate(text(m,"start")),coordinate(text(m,"end")),inspectRunSourceProfile(m).engineJob};
  if(!nativeOrigin)uuid(id.booking);
  require(match(id.device,"^GPU-[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$")&&source_decimal::less(id.start,id.end),"Lineage device/range invalid");
  proof.insert("booking_id",nativeOrigin?QJsonValue(QJsonValue::Null):QJsonValue(id.booking));if(nativeOrigin)proof.insert("source_origin","native_search");proof.insert("start",id.start);proof.insert("end",id.end);manifest(m,current,id);auto expected=capture(snapshot,id,current);const auto finalKind=expected.kind;
  Db booking(bookingDatabase_),history(historyDatabase_);require(table(booking,"bookings")&&(nativeOrigin||table(booking,"dispatch_requests"))&&table(booking,"booking_metadata")&&table(history,"cursors")&&table(history,"native_source_batches")&&table(history,"native_source_identities"),"HeldNonEnrolled: native lineage tables/prefix/identity enrollment absent");
  QSet<QString> seen;QString r=current;bool protocolsAdmitted=true;QCryptographicHash hash(QCryptographicHash::Sha256);hash.addData("pgn-source-resume-lineage-1\n");
  for(;;){
   require(!cancelled_||!cancelled_(),"Lineage verification cancelled");require(!seen.contains(r),"Resume lineage cycle");seen.insert(r);require(qint64(seen.size())<=Safe,"Lineage epoch count outside safe integer");const auto c=cursor(history,booking,r,id);const auto marked=manifest(c.manifest,r,id);sourceIdentity(history,r,c.manifest);const auto predecessor=relations(booking,r,id,r==current,expected);if(r==current)require(compact(c.manifest)==compact(m),"Final snapshot differs from actual immutable source manifest");const auto e=epoch(booking,r,id);Counters base;
   if(e&&e->recovery){require(text(c.manifest,"birth")==e->recovery->childBirth&&quint64(c.manifest.value("pid").toInteger())==e->recovery->childPid&&inspectRunSourceProfile(c.manifest).profile.value("image_sha256").toString().toLower()==e->recovery->imageSha,"Recovered child actual immutable identity differs");}
   if(expected.kind=="recovered_checkpoint")require(inspectRunSourceProfile(c.manifest).status==RunSourceProfileStatus::KnownFusedWindow3RingV1&&inspectRunSourceProfile(c.manifest).profile.value("image_sha256").toString().toLower()==expected.recoveredImageSha,"Recovered publication parent image is not the qualified checkpoint-IO family");
   if(e){require(marked&&text(c.manifest,"resume_previous_run")==e->previous&&text(c.manifest,"checkpoint_input_sha256")==e->cp.hash&&predecessor==e->predecessor,"Lineage epoch/manifest/predecessor provenance differs");base=e->cp.c;}else require(!marked,"Actual resumed manifest lacks immutable epoch (unknown origin)");
   auto reader=open_(r,c.source);require(bool(reader),"Owned held source reader unavailable");QJsonObject result;
   try{result=scan(history,*reader,r,c,id,base,expected,cancelled_);}catch(...){try{reader->closeAndVerifyStable();}catch(...){ }throw;}
   reader->closeAndVerifyStable();reader.reset(); // successful pin/release BEFORE promoting any result
   const auto profile=inspectRunSourceProfile(c.manifest);
   // Historical absence preserves legacy conservation; it never authenticates
   // an image or licenses a new protocol. Admission requires the exact known
   // immutable image/protocol mapping for EVERY current/ancestor epoch.
   protocolsAdmitted=protocolsAdmitted&&runSourceProtocolAdmitted(profile,result.value("source_boundary_protocol").toString());
   if(r==current)proof.insert("source_profile_status",profile.status==RunSourceProfileStatus::KnownAdaptivePrimeGap?"known-adaptive-prime-gap":profile.status==RunSourceProfileStatus::KnownLegacyA73?"known-legacy-a73":profile.status==RunSourceProfileStatus::KnownBoundaryV2?"known-boundary-v2-74a":profile.status==RunSourceProfileStatus::KnownLazyV1?"known-lazy-v1-a159":profile.status==RunSourceProfileStatus::KnownLazySharedV1?"known-lazy-shared-v1-fa6f":profile.status==RunSourceProfileStatus::KnownLazySharedPeriodicV1?"known-lazy-shared-periodic-v1-bfeb":profile.status==RunSourceProfileStatus::KnownLazySharedPeriodicBankV1?"known-lazy-shared-periodic-bank-v1-9a76":profile.status==RunSourceProfileStatus::KnownRadix70V1?"known-radix70-v1-c6f2":profile.status==RunSourceProfileStatus::KnownFloatPrefixV1?"known-radix70-float-prefix-v1-9afb":profile.status==RunSourceProfileStatus::KnownBarrett70V1?"known-barrett70-v1-1a90":profile.status==RunSourceProfileStatus::KnownWindow2Recip5V1?"known-window2-recip5-v1-c5fb":profile.status==RunSourceProfileStatus::KnownTop2V1?"known-top2-v1-bbf7":profile.status==RunSourceProfileStatus::KnownRollingRootsV1?"known-rolling-roots-v1-3e80":profile.status==RunSourceProfileStatus::KnownRollingRootsLanes2V1?"known-rolling-roots-lanes2-v1-2876":profile.status==RunSourceProfileStatus::KnownCarryChainV1?"known-carry-chain-v1-65c7":profile.status==RunSourceProfileStatus::KnownFusedHigh47V1?"known-fused-high47-v1-da55":profile.status==RunSourceProfileStatus::KnownFusedSquareV1?"known-fused-square-v1-12e1":profile.status==RunSourceProfileStatus::KnownFusedWindow3RingV1?(QStringLiteral("known-fused-window3-ring-v1-")+profile.profile.value("image_sha256").toString().left(5)):"historical-unattested");
   if(r==current&&result.contains("source_boundary_protocol")){proof.insert("source_boundary_protocol",result.value("source_boundary_protocol"));proof.insert("source_boundary_groups",result.value("source_boundary_groups"));}
   hash.addData(compact(QJsonObject{{"run",r},{"source",c.source},{"manifest",c.manifest},{"receipt",e?QJsonValue(e->object):QJsonValue(QJsonValue::Null)},{"result",result}})+'\n');
   if(!e)break;expected={"stopped",e->cp.label,e->cp.c,true,true,{}};
   if(e->recovery){expected.kind="recovered_checkpoint";expected.recoveredSourceSha=e->recovery->parentSourceSha;expected.recoveredImageSha=e->recovery->imageSha;}
   r=e->previous;
  }
  require(!cancelled_||!cancelled_(),"Lineage verification cancelled");history.finish();booking.finish();proof.insert("epochs",qint64(seen.size()));proof.insert("chain_sha256",QString::fromLatin1(hash.result().toHex()));proof.insert("terminal",finalKind);proof.insert("raw_lineage_ready",true);
  require(protocolsAdmitted,"RAW lineage conserved but v2 source image/protocol is not admitted by the immutable run profile");proof.insert("ready",true);
 }catch(const std::exception& e){proof.insert("error",QString::fromUtf8(e.what()).left(400));}catch(...){proof.insert("error","Unknown lineage verification failure");}return proof;
}
}
