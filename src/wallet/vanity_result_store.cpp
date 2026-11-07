#include "wallet/vanity_result_store.h"
#include "wallet/vanity_address.h"
#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUuid>
#include <sqlite3.h>
#include <algorithm>

namespace pgn::wallet {
namespace {
struct Statement {
    sqlite3_stmt* value{};
    Statement(sqlite3* db,const char* sql){sqlite3_prepare_v2(db,sql,-1,&value,nullptr);}
    ~Statement(){if(value)sqlite3_finalize(value);}
    void text(int at,const QString& value){const auto bytes=value.toUtf8();sqlite3_bind_text(this->value,at,bytes.constData(),bytes.size(),SQLITE_TRANSIENT);}
};
QString column(sqlite3_stmt* query,int index){const auto* text=sqlite3_column_text(query,index);return text?QString::fromUtf8(reinterpret_cast<const char*>(text)):QString{};}
}
struct VanityResultStore::Impl {
    sqlite3* db{};QString failure,readFailure;qint64 budget{};
    bool execute(const char* sql){return db&&sqlite3_exec(db,sql,nullptr,nullptr,nullptr)==SQLITE_OK;}
    qint64 scalar(const char* sql)const{Statement query(db,sql);return query.value&&sqlite3_step(query.value)==SQLITE_ROW?sqlite3_column_int64(query.value,0):-1;}
    ~Impl(){if(db)sqlite3_close(db);}
};
VanityResultStore::VanityResultStore(const QString& directory,qint64 budget):impl_(std::make_unique<Impl>()){
    impl_->budget=std::clamp<qint64>(budget,32768,128*1024*1024);
    if(!QFileInfo(directory).isAbsolute()||!QDir().mkpath(directory)){impl_->failure="Address index directory unavailable";return;}
    const auto path=directory+"/addresses.sqlite";
    const bool existing=QFileInfo(path).size()>0;
    if(QFileInfo(path).isSymLink()||QFileInfo(path).size()>16*1024*1024){impl_->failure="Address index exceeds its size limit";return;}
    const auto bytes=path.toUtf8();
    if(sqlite3_open_v2(bytes.constData(),&impl_->db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){impl_->failure="Address index could not be opened";return;}
    if(existing&&(impl_->scalar("PRAGMA application_id")!=0x50474e56||impl_->scalar("PRAGMA user_version")!=1&&impl_->scalar("PRAGMA user_version")!=2&&impl_->scalar("PRAGMA user_version")!=3)){
        impl_->failure="Unrecognized address index; existing file retained";return;
    }
    sqlite3_busy_timeout(impl_->db,0);
    if(existing&&impl_->scalar("PRAGMA user_version")==1&&!impl_->execute("BEGIN IMMEDIATE;ALTER TABLE addresses ADD COLUMN network TEXT NOT NULL DEFAULT 'main';PRAGMA user_version=2;COMMIT;")){impl_->execute("ROLLBACK");impl_->failure="Address network metadata migration failed; original index retained";return;}

    if(existing&&impl_->scalar("PRAGMA user_version")==2&&!impl_->execute("BEGIN IMMEDIATE;ALTER TABLE addresses ADD COLUMN suffix TEXT NOT NULL DEFAULT '';PRAGMA user_version=3;COMMIT;")){impl_->execute("ROLLBACK");impl_->failure="Address pattern metadata migration failed; original index retained";return;}

    if(!impl_->execute("PRAGMA page_size=4096;PRAGMA max_page_count=4096;PRAGMA journal_mode=DELETE;PRAGMA synchronous=FULL;PRAGMA cache_size=-512;PRAGMA application_id=1346850390;PRAGMA user_version=3;"
        "CREATE TABLE IF NOT EXISTS addresses(seq INTEGER PRIMARY KEY,session TEXT UNIQUE NOT NULL,address TEXT UNIQUE NOT NULL,prefix TEXT NOT NULL,created TEXT NOT NULL,footprint INTEGER NOT NULL,network TEXT NOT NULL DEFAULT 'main',suffix TEXT NOT NULL DEFAULT '');"
        "CREATE TABLE IF NOT EXISTS budget(id INTEGER PRIMARY KEY CHECK(id=1),reserved INTEGER NOT NULL CHECK(reserved>=0));INSERT OR IGNORE INTO budget VALUES(1,0);"))
        impl_->failure="Address index schema unavailable";
    if(impl_->failure.isEmpty()&&(impl_->scalar("PRAGMA page_size")!=4096||impl_->scalar("PRAGMA max_page_count")!=4096))impl_->failure="Address index page-size limit unavailable";
}
VanityResultStore::~VanityResultStore()=default;
const QString& VanityResultStore::error()const{return impl_->failure;}
const QString& VanityResultStore::pageError()const{return impl_->readFailure;}
qint64 VanityResultStore::count()const{return impl_->failure.isEmpty()?impl_->scalar("SELECT count(*) FROM addresses"):-1;}
qint64 VanityResultStore::reservedBytes()const{return impl_->failure.isEmpty()?impl_->scalar("SELECT reserved FROM budget WHERE id=1"):-1;}
QString VanityResultStore::sessionFor(const QString& address,const QString& network)const{
    if(!impl_->failure.isEmpty()||address.size()>100||network!="main"&&network!="test")return {};
    Statement query(impl_->db,"SELECT session FROM addresses WHERE address=?1 AND network=?2");if(!query.value)return {};query.text(1,address);query.text(2,network);
    return sqlite3_step(query.value)==SQLITE_ROW?column(query.value,0):QString{};
}
QString VanityResultStore::append(const QJsonObject& row,qint64 protectedBytes){
    if(!impl_->failure.isEmpty())return impl_->failure;
    const auto network=row.value("network").toString("main");const auto session=row.value("session_id").toString(),address=row.value("address").toString(),prefix=row.value("prefix").toString(),created=row.value("created").toString(),suffix=row.value("suffix").toString();
    const QString alphabet="qpzry9x8gf2tvdw0s3jn54khce6mua7l";
    const bool validMain=network=="main"&&QRegularExpression("^PGN[1-9A-HJ-NP-Za-km-z]{0,32}$").match(prefix).hasMatch()&&QRegularExpression("^[1-9A-HJ-NP-Za-km-z]+$").match(address).hasMatch();
    const bool validTestLegacy=network=="test"&&QRegularExpression("^tpgn1q["+alphabet+"]{0,32}$").match(prefix).hasMatch()&&QRegularExpression("^tpgn1q["+alphabet+"]{38}$").match(address).hasMatch();
    const bool validTestAlias=network=="test"&&QRegularExpression("^tpgn[1-9A-HJ-NP-Za-km-z]{0,32}$").match(prefix).hasMatch()&&isCanonicalTestnetKeyHashAlias(address);
    if(QUuid(session).isNull()||QUuid(session).toString(QUuid::WithoutBraces)!=session||address.size()<20||address.size()>100||!address.startsWith(prefix)||!address.endsWith(suffix)||(!row.value("suffix").isUndefined()&&!row.value("suffix").isString())||suffix.size()>(validTestLegacy?38:32)||!(validMain||validTestLegacy||validTestAlias)||!QDateTime::fromString(created,Qt::ISODateWithMs).isValid()||protectedBytes<0||protectedBytes>262144)
        return "Invalid public address record";
    // Include directory/file allocation overhead, not just a few key bytes.
    const qint64 footprint=std::max<qint64>(32768,protectedBytes+8192);
    if(!impl_->execute("BEGIN IMMEDIATE"))return "Address index is busy; generation paused";
    const auto rollback=[&](QString message){impl_->execute("ROLLBACK");return message;};
    Statement previous(impl_->db,"SELECT session,address,prefix,created,network,suffix FROM addresses WHERE session=?1 OR address=?2");
    if(!previous.value)return rollback("Address index lookup unavailable");previous.text(1,session);previous.text(2,address);
    const int old=sqlite3_step(previous.value);
    if(old==SQLITE_ROW){const bool identical=column(previous.value,0)==session&&column(previous.value,1)==address&&column(previous.value,2)==prefix&&column(previous.value,3)==created&&column(previous.value,4)==network&&column(previous.value,5)==suffix;return rollback(identical?QString{}:QString("Conflicting address identity; previous key retained"));}
    if(old!=SQLITE_DONE)return rollback("Address index lookup failed");
    const auto reserved=reservedBytes();if(reserved<0||reserved>impl_->budget-footprint)return rollback("Protected-address storage budget reached; generation paused and existing keys retained");
    Statement insert(impl_->db,"INSERT INTO addresses(session,address,prefix,created,footprint,network,suffix)VALUES(?1,?2,?3,?4,?5,?6,?7)");
    if(!insert.value)return rollback("Address index insertion unavailable");insert.text(1,session);insert.text(2,address);insert.text(3,prefix);insert.text(4,created);sqlite3_bind_int64(insert.value,5,footprint);insert.text(6,network);insert.text(7,suffix);
    if(sqlite3_step(insert.value)!=SQLITE_DONE)return rollback("Address index full or unwritable; protected key retained");
    Statement update(impl_->db,"UPDATE budget SET reserved=reserved+?1 WHERE id=1");if(!update.value)return rollback("Address budget update unavailable");sqlite3_bind_int64(update.value,1,footprint);
    if(sqlite3_step(update.value)!=SQLITE_DONE||!impl_->execute("COMMIT"))return rollback("Address index commit failed; protected key retained");
    return {};
}
QJsonArray VanityResultStore::page(int offset,int limit)const{
    QJsonArray rows;impl_->readFailure.clear();if(!impl_->failure.isEmpty()){impl_->readFailure=impl_->failure;return rows;}if(offset<0||limit<1||limit>1000){impl_->readFailure="Invalid address page bounds";return rows;}
    Statement query(impl_->db,"SELECT session,address,prefix,created,network,suffix FROM addresses ORDER BY seq DESC LIMIT ?1 OFFSET ?2");if(!query.value){impl_->readFailure="Address page query unavailable";return rows;}
    sqlite3_bind_int(query.value,1,limit);sqlite3_bind_int(query.value,2,offset);
    int status;while((status=sqlite3_step(query.value))==SQLITE_ROW)rows.append(QJsonObject{{"session_id",column(query.value,0)},{"address",column(query.value,1)},{"prefix",column(query.value,2)},{"created",column(query.value,3)},{"network",column(query.value,4)},{"suffix",column(query.value,5)}});
    if(status!=SQLITE_DONE){impl_->readFailure="Address page read failed; previous view retained";return {};}
    return rows;
}
}
