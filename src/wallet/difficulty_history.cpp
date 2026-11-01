#include "wallet/difficulty_history.h"
#include "storage/sqlite_read.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <sqlite3.h>
#include <cmath>
#include <stdexcept>
#include <utility>
namespace pgn::wallet {
namespace {
using Statement=pgn::storage::ReadStatement;
bool hashValid(const QString& value){static const QRegularExpression pattern("^[a-fA-F0-9]{64}$");return pattern.match(value).hasMatch();}
void execute(sqlite3* db,const char* sql){if(sqlite3_exec(db,sql,nullptr,nullptr,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
DifficultyHeader headerRow(const Statement& row,const QString& genesis){return {genesis,row.text(0),row.text(2),row.text(3),sqlite3_column_int64(row.value,1),bool(sqlite3_column_int(row.value,4))};}
std::optional<DifficultyHeader> readHeader(sqlite3* db,const QString& genesis,const QString& hash){
 Statement row(db,"SELECT hash,height,previous,difficulty,ancestry_complete FROM difficulty_headers WHERE genesis=? AND hash=?");row.text(1,genesis);row.text(2,hash);
 if(row.step()!=SQLITE_ROW)return {};return headerRow(row,genesis);
}
}
DifficultyHistoryStore::DifficultyHistoryStore(QString file):file_(QFileInfo(file).absoluteFilePath()){}
DifficultyHistoryStore::~DifficultyHistoryStore(){if(db_)sqlite3_close(db_);}
sqlite3* DifficultyHistoryStore::writer(){
 if(db_)return db_;
 if(!QDir().mkpath(QFileInfo(file_).absolutePath()))throw std::runtime_error("Difficulty archive directory unavailable");
 const auto bytes=file_.toUtf8();
 if(sqlite3_open_v2(bytes.constData(),&db_,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){if(db_)sqlite3_close(db_);db_=nullptr;throw std::runtime_error("Difficulty archive unavailable");}
 sqlite3_busy_timeout(db_,1000);
 execute(db_,"PRAGMA journal_mode=WAL;PRAGMA synchronous=FULL;"
 "CREATE TABLE IF NOT EXISTS difficulty_headers(genesis TEXT NOT NULL,hash TEXT NOT NULL,height INTEGER NOT NULL,previous TEXT NOT NULL,difficulty TEXT NOT NULL,raw_header TEXT NOT NULL,ancestry_complete INTEGER NOT NULL DEFAULT 0,points INTEGER NOT NULL DEFAULT 0,missing INTEGER NOT NULL DEFAULT 0,minimum REAL,maximum REAL,PRIMARY KEY(genesis,hash));"
 "CREATE INDEX IF NOT EXISTS difficulty_parent ON difficulty_headers(genesis,previous,height);"
 "CREATE TABLE IF NOT EXISTS difficulty_observations(genesis TEXT NOT NULL,hash TEXT NOT NULL,pid INTEGER NOT NULL,birth TEXT NOT NULL,generation TEXT NOT NULL,observed_utc TEXT NOT NULL,raw_header TEXT NOT NULL,PRIMARY KEY(genesis,hash,pid,birth,generation,observed_utc));"
 "CREATE TABLE IF NOT EXISTS difficulty_tips(genesis TEXT PRIMARY KEY,hash TEXT NOT NULL,observed_utc TEXT NOT NULL,pid INTEGER NOT NULL,birth TEXT NOT NULL,generation TEXT NOT NULL);"
 "CREATE TABLE IF NOT EXISTS difficulty_tip_observations(genesis TEXT NOT NULL,hash TEXT NOT NULL,observed_utc TEXT NOT NULL,pid INTEGER NOT NULL,birth TEXT NOT NULL,generation TEXT NOT NULL,PRIMARY KEY(genesis,hash,observed_utc,pid,birth,generation));"
 "CREATE TEMP TABLE difficulty_linked(hash TEXT PRIMARY KEY,height INTEGER,points INTEGER,missing INTEGER,minimum REAL,maximum REAL) WITHOUT ROWID;");
 return db_;
}
QString DifficultyHistoryStore::record(const CoreEpoch& owner,const QString& chain,const QString& expectedHash,const QJsonObject& input,const QDateTime& observedUtc){
 // Validate before any directory/database mutation. The caller is the verified
 // Core RPC boundary; a user descriptor or mining event cannot call it implicitly.
 const auto genesis=chain.toLower(),hash=input.value("hash").toString().toLower();
 const auto number=input.value("height");const double h=number.toDouble(-1);
 if(!owner.matches(owner)||!observedUtc.isValid()||!hashValid(genesis)||!hashValid(expectedHash)||hash!=expectedHash.toLower()||
    !number.isDouble()||!std::isfinite(h)||h<0||h>9007199254740991.||std::trunc(h)!=h)return "Invalid owned header identity, height or original observation time";
 const qint64 height=number.toInteger();QString previous=input.value("previousblockhash").toString().toLower();
 if(height==0){if(hash!=genesis||(!previous.isEmpty()&&previous!=QString(64,'0')))return "Genesis header does not match pinned chain";previous.clear();}
 else if(!hashValid(previous)||hash==previous)return "Non-genesis header requires a distinct exact parent hash";
 QString difficulty;const auto value=input.value("difficulty");
 if(!value.isUndefined()&&!value.isNull()){
  const double d=value.toDouble(-1);if(!value.isDouble()||!std::isfinite(d)||d<0)return "Invalid reported difficulty; no zero substituted";
  difficulty=QString::number(d,'g',17);
 }
 const auto raw=QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Compact));
 if(raw.toUtf8().size()>262144)return "Header observation exceeds wire budget; no truncation";
 try{
  auto* db=writer();execute(db,"BEGIN IMMEDIATE");
  try{
   const auto existing=readHeader(db,genesis,hash),parent=height?readHeader(db,genesis,previous):std::nullopt;
   if(existing&&(existing->height!=height||existing->previous!=previous||existing->difficulty!=difficulty))throw std::runtime_error("Conflicting immutable header observation; original retained");
   if(parent&&parent->height+1!=height)throw std::runtime_error("Header parent height mismatch");
   const QString utc=observedUtc.toUTC().toString(Qt::ISODateWithMs),generation=QString::number(owner.generation);
   Statement prior(db,"SELECT raw_header FROM difficulty_observations WHERE genesis=? AND hash=? AND pid=? AND birth=? AND generation=? AND observed_utc=?");
   prior.text(1,genesis);prior.text(2,hash);prior.integer(3,owner.pid);prior.text(4,owner.birth);prior.text(5,generation);prior.text(6,utc);
   if(prior.step()==SQLITE_ROW&&prior.text(0)!=raw)throw std::runtime_error("Observation identity conflict; original retained");
   Statement observation(db,"INSERT OR IGNORE INTO difficulty_observations VALUES(?,?,?,?,?,?,?)");
   observation.text(1,genesis);observation.text(2,hash);observation.integer(3,owner.pid);observation.text(4,owner.birth);observation.text(5,generation);observation.text(6,utc);observation.text(7,raw);observation.step();
   if(!existing){
    Statement insert(db,"INSERT INTO difficulty_headers(genesis,hash,height,previous,difficulty,raw_header) VALUES(?,?,?,?,?,?)");
    insert.text(1,genesis);insert.text(2,hash);insert.integer(3,height);insert.text(4,previous);insert.text(5,difficulty);insert.text(6,raw);insert.step();
    // Newly linked descendants are promoted once, including reverse-order
    // backfill. Materialized exact counts/bounds make each tip lookup O(log N).
    if(height==0||(parent&&parent->ancestryComplete)){
    // A missing parent cannot connect anything. Do not run an empty UPDATE
    // over an ever-growing pending archive for every reverse-order header.
    execute(db,"DELETE FROM difficulty_linked");
    Statement connect(db,R"SQL(WITH RECURSIVE linked(hash,height,points,missing,minimum,maximum) AS (
     SELECT h.hash,h.height,coalesce(p.points,0)+(h.difficulty!=''),coalesce(p.missing,0)+(h.difficulty=''),
      CASE WHEN h.difficulty='' THEN p.minimum WHEN p.minimum IS NULL THEN CAST(h.difficulty AS REAL) ELSE min(p.minimum,CAST(h.difficulty AS REAL)) END,
      CASE WHEN h.difficulty='' THEN p.maximum WHEN p.maximum IS NULL THEN CAST(h.difficulty AS REAL) ELSE max(p.maximum,CAST(h.difficulty AS REAL)) END
     FROM difficulty_headers h LEFT JOIN difficulty_headers p ON p.genesis=h.genesis AND p.hash=h.previous
     WHERE h.genesis=?1 AND h.hash=?2 AND (h.height=0 OR (p.ancestry_complete=1 AND p.height+1=h.height))
     UNION ALL SELECT c.hash,c.height,p.points+(c.difficulty!=''),p.missing+(c.difficulty=''),
      CASE WHEN c.difficulty='' THEN p.minimum WHEN p.minimum IS NULL THEN CAST(c.difficulty AS REAL) ELSE min(p.minimum,CAST(c.difficulty AS REAL)) END,
      CASE WHEN c.difficulty='' THEN p.maximum WHEN p.maximum IS NULL THEN CAST(c.difficulty AS REAL) ELSE max(p.maximum,CAST(c.difficulty AS REAL)) END
     FROM linked p CROSS JOIN difficulty_headers c INDEXED BY difficulty_parent
     WHERE c.genesis=?1 AND c.previous=p.hash AND c.height=p.height+1 AND c.ancestry_complete=0
    ) INSERT INTO difficulty_linked SELECT * FROM linked)SQL");
    connect.text(1,genesis);connect.text(2,hash);connect.step();
    Statement promote(db,R"SQL(UPDATE difficulty_headers SET ancestry_complete=1,
      points=(SELECT points FROM difficulty_linked WHERE hash=difficulty_headers.hash),missing=(SELECT missing FROM difficulty_linked WHERE hash=difficulty_headers.hash),
      minimum=(SELECT minimum FROM difficulty_linked WHERE hash=difficulty_headers.hash),maximum=(SELECT maximum FROM difficulty_linked WHERE hash=difficulty_headers.hash)
      WHERE genesis=? AND hash IN (SELECT hash FROM difficulty_linked))SQL");promote.text(1,genesis);promote.step();
    }
   }
   execute(db,"COMMIT");
  }catch(...){execute(db,"ROLLBACK");throw;}
 }catch(const std::exception& error){return QString::fromUtf8(error.what());}
 return {};
}
std::optional<DifficultyHeader> DifficultyHistoryStore::find(const QString& genesis,const QString& hash)const{
 if(!QFileInfo(file_).isFile())return {};
 pgn::storage::ReadOnlyDatabase db(file_);return readHeader(db.db,genesis.toLower(),hash.toLower());
}
QString DifficultyHistoryStore::activate(const ChainObservation& anchor,const QString& tipHash){
 if(!anchor.valid()||!hashValid(tipHash)||anchor.tipHash!=tipHash.toLower())return "Verified chain anchor and its exact observed tip required";
 try{
  auto* db=writer();execute(db,"BEGIN IMMEDIATE");
  try{
   const auto tip=readHeader(db,anchor.genesis.toLower(),tipHash.toLower());
   if(!tip||!tip->ancestryComplete||tip->height!=anchor.height)throw std::runtime_error("Difficulty history has not reached the pinned genesis; no complete history published");
   const auto utc=anchor.observedUtc.toUTC().toString(Qt::ISODateWithMs),generation=QString::number(anchor.owner.generation);
   Statement previousTip(db,"SELECT hash,observed_utc FROM difficulty_tips WHERE genesis=?");previousTip.text(1,anchor.genesis.toLower());
   if(previousTip.step()==SQLITE_ROW){
    const auto priorUtc=QDateTime::fromString(previousTip.text(1),Qt::ISODateWithMs);
    if(priorUtc>anchor.observedUtc||(priorUtc==anchor.observedUtc&&previousTip.text(0)!=tipHash.toLower()))throw std::runtime_error("Stale or conflicting tip observation; displayed history retained");
   }
   Statement observation(db,"INSERT OR IGNORE INTO difficulty_tip_observations VALUES(?,?,?,?,?,?)");
   observation.text(1,anchor.genesis.toLower());observation.text(2,tipHash.toLower());observation.text(3,utc);observation.integer(4,anchor.owner.pid);observation.text(5,anchor.owner.birth);observation.text(6,generation);observation.step();
   Statement tipRow(db,"INSERT INTO difficulty_tips VALUES(?,?,?,?,?,?) ON CONFLICT(genesis) DO UPDATE SET hash=excluded.hash,observed_utc=excluded.observed_utc,pid=excluded.pid,birth=excluded.birth,generation=excluded.generation");
   tipRow.text(1,anchor.genesis.toLower());tipRow.text(2,tipHash.toLower());tipRow.text(3,utc);tipRow.integer(4,anchor.owner.pid);tipRow.text(5,anchor.owner.birth);tipRow.text(6,generation);tipRow.step();execute(db,"COMMIT");
  }catch(...){execute(db,"ROLLBACK");throw;}
 }catch(const std::exception& error){return QString::fromUtf8(error.what());}
 return {};
}
DifficultySummary DifficultyHistoryStore::summary(const QString& chain)const{
 DifficultySummary result;result.genesis=chain.toLower();if(!QFileInfo(file_).isFile())return result;
 try{
  pgn::storage::ReadOnlyDatabase db(file_);Statement row(db.db,"SELECT h.hash,h.height,h.points,h.missing,h.minimum,h.maximum,t.observed_utc FROM difficulty_tips t JOIN difficulty_headers h ON h.genesis=t.genesis AND h.hash=t.hash WHERE t.genesis=? AND h.ancestry_complete=1");row.text(1,result.genesis);
  if(row.step()!=SQLITE_ROW)return result;
  result.tipHash=row.text(0);result.height=sqlite3_column_int64(row.value,1);result.points=sqlite3_column_int64(row.value,2);result.missing=sqlite3_column_int64(row.value,3);
  if(sqlite3_column_type(row.value,4)!=SQLITE_NULL)result.minimum=sqlite3_column_double(row.value,4);
  if(sqlite3_column_type(row.value,5)!=SQLITE_NULL)result.maximum=sqlite3_column_double(row.value,5);
  result.observedUtc=QDateTime::fromString(row.text(6),Qt::ISODateWithMs);result.available=true;
 }catch(const std::exception& error){result.error=QString::fromUtf8(error.what());}return result;
}
}
