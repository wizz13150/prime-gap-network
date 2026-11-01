#include "mining/source_history.h"
#include "mining/source_decimal.h"
#include "mining/native_source_identity.h"
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QDebug>
#include <QHash>
#include <QSet>
#include <QRegularExpression>
#include <QStringList>
#include <sqlite3.h>
#include <bit>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>

namespace pgn::mining {
namespace {
constexpr qsizetype RowLimit=32768, BatchLimit=131072;
// This is a bounded local display cache, never an unbounded scientific archive.
constexpr qint64 HistoryCacheBytes=24LL*1024*1024;
constexpr qint64 HistoryWalAdmissionBytes=8LL*1024*1024;
constexpr qint64 HistoryTotalBytes=64LL*1024*1024;
[[noreturn]] void fail(const char* message) { throw std::runtime_error(message); }
class Statement {
public:
    sqlite3_stmt* row{};
    Statement(sqlite3* db,const char* sql) {
        if(sqlite3_prepare_v2(db,sql,-1,&row,nullptr)!=SQLITE_OK)fail("Source history query unavailable");
    }
    ~Statement(){sqlite3_finalize(row);}
    void text(int at,const QString& value){const auto b=value.toUtf8();if(sqlite3_bind_text(row,at,b.constData(),int(b.size()),SQLITE_TRANSIENT)!=SQLITE_OK)fail("Source text binding failed");}
    void integer(int at,qint64 value){if(sqlite3_bind_int64(row,at,value)!=SQLITE_OK)fail("Source integer binding failed");}
    void number(int at,double value){if(sqlite3_bind_double(row,at,value)!=SQLITE_OK)fail("Source coordinate binding failed");}
    int step(){const int rc=sqlite3_step(row);if(rc!=SQLITE_ROW&&rc!=SQLITE_DONE)fail("Source history transaction failed");return rc;}
    QString text(int column)const{return QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(row,column)),sqlite3_column_bytes(row,column));}
};
void execute(sqlite3* db,const char* sql){
    if(QByteArray(sql)=="BEGIN IMMEDIATE"){
        const auto file=QString::fromUtf8(sqlite3_db_filename(db,"main"));
        const auto wal=file+"-wal";
        if(QFileInfo(file).size()+QFileInfo(wal).size()+QFileInfo(file+"-shm").size()>HistoryTotalBytes)
            fail("Source display cache storage budget reached");
        if(QFileInfo(wal).size()>HistoryWalAdmissionBytes){
            // A pinned reader must create backpressure, never an unlimited WAL.
            sqlite3_busy_timeout(db,0);
            const auto checkpoint=sqlite3_wal_checkpoint_v2(db,nullptr,SQLITE_CHECKPOINT_TRUNCATE,nullptr,nullptr);
            sqlite3_busy_timeout(db,5000);
            if(checkpoint!=SQLITE_OK&&checkpoint!=SQLITE_BUSY&&checkpoint!=SQLITE_LOCKED)
                fail("Source cache checkpoint failed");
            if(QFileInfo(wal).size()>HistoryWalAdmissionBytes)
                fail("Source display cache awaits its active readers");
        }
    }
    if(sqlite3_exec(db,sql,nullptr,nullptr,nullptr)!=SQLITE_OK)fail("Source history SQL failed");
}
void validateRun(const QString& run){
    static const QRegularExpression pattern("^[0-9a-f]{32}$");
    if(!pattern.match(run).hasMatch())fail("Invalid native source run identity");
}
using source_decimal::canonical;
using source_decimal::less;
using source_decimal::difference;
QString coordinateKey(double value,qint64 id){
    auto bits=std::bit_cast<quint64>(value==0?0.0:value);
    bits=(bits&(quint64(1)<<63))?~bits:bits^(quint64(1)<<63);
    return QString::number(bits,16).rightJustified(16,'0')+QString::number(id,16).rightJustified(16,'0');
}
struct Gap {QString merit,gap,p,q,basis;std::optional<qint64> time;};
std::optional<Gap> parse(const QByteArray& raw){
    if(!raw.startsWith("[phase0-gpu] GAP "))return {};
    static const QRegularExpression pattern("^\\[phase0-gpu\\] GAP merit=([0-9]+(?:\\.[0-9]+)?) gap=([0-9]+) lower=([0-9]+) upper=([0-9]+) verified=1(?: |$)");
    const auto line=QString::fromLatin1(raw);
    const auto match=pattern.match(line);
    if(!match.hasMatch())fail("Malformed source GAP row; source cursor unchanged");
    Gap result{match.captured(1),match.captured(2),match.captured(3),match.captured(4),{}, {}};
    bool ok=false;const double merit=result.merit.toDouble(&ok);
    if(!ok||!std::isfinite(merit)||merit<0||!canonical(result.gap)||result.gap=="0"||
       !canonical(result.p)||!canonical(result.q)||!less(result.p,result.q)||difference(result.q,result.p)!=result.gap)
        fail("Exact source endpoint/gap identity rejected");
    const double length=result.gap.toDouble(&ok);
    if(!ok||!std::isfinite(length)||length<=0)fail("Source display coordinate unavailable; exact bytes retained");
    static const QRegularExpression timestamp("(?:^| )unix_ts=([^ ]+)(?: |$)");
    const auto t=timestamp.match(line);
    if(t.hasMatch()){
        const auto text=t.captured(1);const auto seconds=text.toLongLong(&ok);
        if(!canonical(text)||!ok||seconds<=0||seconds>9007199254740991LL/1000)fail("Original source event time invalid");
        result.time=seconds;
        if(timestamp.match(line,t.capturedEnd()-1).hasMatch())fail("Duplicate source event time");
    }
    static const QRegularExpression basis("(?:^| )time_basis=([^ ]+)(?: |$)"),basisToken("^[a-z][a-z0-9_]{0,63}$");
    const auto declared=basis.match(line);
    if(declared.hasMatch()){
        if(!basisToken.match(declared.captured(1)).hasMatch()||basis.match(line,declared.capturedEnd()-1).hasMatch())fail("Source-declared time basis invalid");
        result.basis=declared.captured(1);
    }
    return result;
}
}
class SourceHistory::Impl {
public:
    sqlite3* db{};
    bool displayCacheSchema{};
    qint64 integrityVersion{-1};QHash<QString,qint64> verifiedOffsets;
    qint64 indexVersion{-1};QSet<QString> auditedIndexScopes;
    void auditLegacyPrefix(const QString& run,qint64 offset,const QByteArray& expected,
        const std::function<QByteArray(qint64,qsizetype)>& reader);
    explicit Impl(const QString& file,bool existingEnrollmentOnly=false){
        const QFileInfo storage(file);
        if(storage.fileName().compare("scientific-history.sqlite",Qt::CaseInsensitive)==0)
            fail("The unbounded legacy scientific catalog is retired");
        if(storage.exists()&&storage.size()>HistoryCacheBytes)
            fail("Source display cache exceeds its24MiB main-database budget");
        if(existingEnrollmentOnly){
            const QFileInfo info(file);
            if(!info.isAbsolute()||!info.isFile()||info.isSymLink())fail("Explicit existing regular legacy database required");
        }
        const auto path=QFileInfo(file).absoluteFilePath().toUtf8();
        const auto flags=SQLITE_OPEN_READWRITE|SQLITE_OPEN_NOMUTEX|(existingEnrollmentOnly?0:SQLITE_OPEN_CREATE);
        if(sqlite3_open_v2(path.constData(),&db,flags,nullptr)!=SQLITE_OK){if(db)sqlite3_close(db);db=nullptr;fail("Native source database unavailable");}
        try{
            sqlite3_busy_timeout(db,5000);
            qint64 bytesPerPage=0;
            {Statement pageSize(db,"PRAGMA page_size");
             if(pageSize.step()!=SQLITE_ROW)fail("Source cache page size unavailable");
             bytesPerPage=sqlite3_column_int64(pageSize.row,0); }
            if(bytesPerPage<=0||bytesPerPage>HistoryCacheBytes)fail("Invalid source cache page size");
            const auto limit=QByteArray("PRAGMA max_page_count=")+QByteArray::number(HistoryCacheBytes/bytesPerPage);
            execute(db,limit.constData());
            execute(db,"PRAGMA temp_store=MEMORY; PRAGMA cache_size=-4096; PRAGMA cache_spill=OFF;");
            // No persistent pragma/schema/index migration before explicit audit.
            // synchronous is connection-local; retain the existing journal mode.
            if(existingEnrollmentOnly){
                execute(db,"PRAGMA synchronous=FULL");
                Statement cache(db,"SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name IN ('native_plot_multiplicity','native_plot_multiplicity_manifest')");
                displayCacheSchema=cache.step()==SQLITE_ROW&&sqlite3_column_int64(cache.row,0)==2;
                return;
            }
            execute(db,"PRAGMA journal_mode=WAL; PRAGMA journal_size_limit=8388608; PRAGMA wal_autocheckpoint=256; PRAGMA synchronous=FULL;");
            execute(db,"BEGIN IMMEDIATE");
            execute(db,
                "CREATE TABLE IF NOT EXISTS gaps(id INTEGER PRIMARY KEY,p TEXT NOT NULL,q TEXT NOT NULL,gap TEXT NOT NULL,merit TEXT NOT NULL,unix_ts INTEGER,time_basis TEXT,UNIQUE(p,q));"
                "CREATE TABLE IF NOT EXISTS origins(gap_id INTEGER NOT NULL,run TEXT NOT NULL,source TEXT NOT NULL,UNIQUE(gap_id,run,source));"
                "CREATE TABLE IF NOT EXISTS cursors(source TEXT PRIMARY KEY,offset INTEGER NOT NULL,run TEXT NOT NULL);"
                "CREATE TABLE IF NOT EXISTS native_source_identities(run TEXT PRIMARY KEY,identity TEXT NOT NULL) WITHOUT ROWID;"
                "CREATE TABLE IF NOT EXISTS native_source_batches(source TEXT NOT NULL,offset INTEGER NOT NULL,length INTEGER NOT NULL,sha256 TEXT NOT NULL,PRIMARY KEY(source,offset)) WITHOUT ROWID;"
                "CREATE INDEX IF NOT EXISTS gaps_time ON gaps(unix_ts,id);"
                "CREATE INDEX IF NOT EXISTS native_origins_run_gap ON origins(run,gap_id);"
                "CREATE TABLE IF NOT EXISTS native_plot_coordinates(scope TEXT NOT NULL,axis TEXT NOT NULL,point_key TEXT NOT NULL,gap_id INTEGER NOT NULL,x REAL NOT NULL,y REAL NOT NULL,admission_revision INTEGER NOT NULL DEFAULT 0,PRIMARY KEY(scope,axis,point_key)) WITHOUT ROWID;"
                "CREATE TABLE IF NOT EXISTS native_plot_bounds(scope TEXT NOT NULL,axis TEXT NOT NULL,min_x REAL,max_x REAL,min_y REAL,max_y REAL,points INTEGER NOT NULL,revision INTEGER NOT NULL,PRIMARY KEY(scope,axis)) WITHOUT ROWID;");
            bool hasTimeBasis=false;{Statement schema(db,"PRAGMA table_info(gaps)");while(schema.step()==SQLITE_ROW)hasTimeBasis|=schema.text(1)=="time_basis";}
            if(!hasTimeBasis)execute(db,"ALTER TABLE gaps ADD COLUMN time_basis TEXT");
            bool hasRevision=false;{Statement schema(db,"PRAGMA table_info(native_plot_coordinates)");while(schema.step()==SQLITE_ROW)hasRevision|=schema.text(1)=="admission_revision";}
            if(!hasRevision)execute(db,"ALTER TABLE native_plot_coordinates ADD COLUMN admission_revision INTEGER NOT NULL DEFAULT 0");
            execute(db,"CREATE INDEX IF NOT EXISTS native_plot_revision ON native_plot_coordinates(scope,axis,admission_revision,point_key)");
            execute(db,
                "CREATE TABLE IF NOT EXISTS native_plot_multiplicity(scope TEXT NOT NULL,axis TEXT NOT NULL,x REAL NOT NULL,y REAL NOT NULL,multiplicity INTEGER NOT NULL CHECK(multiplicity>0),PRIMARY KEY(scope,axis,x,y)) WITHOUT ROWID;"
                "CREATE TABLE IF NOT EXISTS native_plot_multiplicity_manifest(scope TEXT NOT NULL,axis TEXT NOT NULL,base_revision INTEGER NOT NULL,points INTEGER NOT NULL,cells INTEGER NOT NULL,PRIMARY KEY(scope,axis)) WITHOUT ROWID;");
            execute(db,"COMMIT");
            displayCacheSchema=true;
        }catch(...){sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);sqlite3_close(db);db=nullptr;throw;}
    }
    ~Impl(){sqlite3_close(db);}
    qint64 cursor(const QString& run)const{
        Statement query(db,"SELECT offset,run FROM cursors WHERE source=?");query.text(1,"native/"+run+".stdout");
        if(query.step()==SQLITE_DONE)return 0;
        if(sqlite3_column_type(query.row,0)!=SQLITE_INTEGER||sqlite3_column_int64(query.row,0)<0||query.text(1)!=run)fail("Source cursor identity invalid");
        return sqlite3_column_int64(query.row,0);
    }
    qint64 dataVersion()const{
        Statement version(db,"PRAGMA data_version");if(version.step()!=SQLITE_ROW)fail("Source integrity version unavailable");
        return sqlite3_column_int64(version.row,0);
    }
    void requireEnrolledPrefix(const QString& run,qint64 offset){
        // No quadratic revalidation on this owner's own append stream. SQLite
        // data_version invalidates the bounded cache after another connection's
        // commit. BEGIN IMMEDIATE already excludes a racing external writer.
        const auto currentVersion=dataVersion();
        if(currentVersion!=integrityVersion){verifiedOffsets.clear();integrityVersion=currentVersion;}
        const auto known=verifiedOffsets.constFind(run);
        if(known!=verifiedOffsets.cend()&&*known==offset)return;
        Statement batches(db,"SELECT offset,length,sha256 FROM native_source_batches WHERE source=? ORDER BY offset");batches.text(1,"native/"+run+".stdout");
        qint64 at=0;
        while(batches.step()==SQLITE_ROW){
            const auto start=sqlite3_column_int64(batches.row,0),length=sqlite3_column_int64(batches.row,1);
            const auto digest=batches.text(2);
            if(sqlite3_column_type(batches.row,0)!=SQLITE_INTEGER||sqlite3_column_type(batches.row,1)!=SQLITE_INTEGER||start!=at||length<=0||length>BatchLimit||at>offset-length||digest.size()!=64)
                fail("Source integrity coverage disagrees with cursor");
            for(const auto c:digest)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))fail("Source integrity digest encoding invalid");
            at+=length;
        }
        if(at!=offset)fail("Legacy or incomplete source integrity enrollment; explicit prefix audit required");
        if(verifiedOffsets.size()>=64)verifiedOffsets.clear();
        verifiedOffsets.insert(run,offset);
    }
    void verifyPrefix(const QString& run,qint64 offset,const std::function<QByteArray(qint64,qsizetype)>& reader)const{
        Statement batches(db,"SELECT offset,length,sha256 FROM native_source_batches WHERE source=? ORDER BY offset");batches.text(1,"native/"+run+".stdout");
        qint64 at=0;
        while(batches.step()==SQLITE_ROW){
            const auto start=sqlite3_column_int64(batches.row,0),length=sqlite3_column_int64(batches.row,1);
            const auto digest=batches.text(2);
            if(sqlite3_column_type(batches.row,0)!=SQLITE_INTEGER||sqlite3_column_type(batches.row,1)!=SQLITE_INTEGER||start!=at||length<=0||length>BatchLimit||at>offset-length||digest.size()!=64)
                fail("Source prefix integrity coverage invalid");
            QCryptographicHash hash(QCryptographicHash::Sha256);qint64 remaining=length,readAt=start;
            while(remaining>0){const auto requested=std::min<qint64>(65536,remaining);const auto chunk=reader(readAt,requested);if(chunk.isEmpty()||chunk.size()>requested)fail("Source prefix read violated verified range");hash.addData(chunk);remaining-=chunk.size();readAt+=chunk.size();}
            if(QString::fromLatin1(hash.result().toHex())!=digest)fail("Conserved source prefix changed; original catalogue and cursor retained");
            at+=length;
        }
        if(at!=offset)fail("Legacy or incomplete source integrity enrollment; explicit prefix audit required");
    }
    void appendDisplayCache(const QString& scope,const QString& axis,double x,double y,qint64 revision,qint64 points){
        // Same source-admission SQLite transaction; no worker, scan or IPC.
        // Only extend an EXACT already complete prefix. A stale/absent cache
        // with existing observations stays untouched; renderer reads its raw
        // suffix until explicit catch-up. Never relabel a partial prefix.
        if(!displayCacheSchema||!scope.isEmpty())return;
        if(points==0&&revision==0){
            Statement empty(db,"INSERT OR IGNORE INTO native_plot_multiplicity_manifest VALUES(?,?,0,0,0)");empty.text(1,scope);empty.text(2,axis);empty.step();
        }
        {Statement manifest(db,"SELECT base_revision,points,cells FROM native_plot_multiplicity_manifest WHERE scope=? AND axis=?");
         manifest.text(1,scope);manifest.text(2,axis);
         if(manifest.step()!=SQLITE_ROW||sqlite3_column_type(manifest.row,0)!=SQLITE_INTEGER||
            sqlite3_column_type(manifest.row,1)!=SQLITE_INTEGER||sqlite3_column_type(manifest.row,2)!=SQLITE_INTEGER||
            sqlite3_column_int64(manifest.row,0)!=revision||sqlite3_column_int64(manifest.row,1)!=points||
            sqlite3_column_int64(manifest.row,2)<0||sqlite3_column_int64(manifest.row,2)>points)return;}
        Statement cell(db,"INSERT OR IGNORE INTO native_plot_multiplicity VALUES(?,?,?,?,1)");
        cell.text(1,scope);cell.text(2,axis);cell.number(3,x);cell.number(4,y);cell.step();
        const auto newCell=sqlite3_changes(db);
        if(!newCell){
            Statement increment(db,"UPDATE native_plot_multiplicity SET multiplicity=multiplicity+1 WHERE scope=? AND axis=? AND x=? AND y=? AND multiplicity>0 AND multiplicity<9223372036854775807");
            increment.text(1,scope);increment.text(2,axis);increment.number(3,x);increment.number(4,y);increment.step();
            if(sqlite3_changes(db)!=1)fail("Exact display multiplicity cannot be extended");
        }
        Statement advance(db,"UPDATE native_plot_multiplicity_manifest SET base_revision=?,points=?,cells=cells+? WHERE scope=? AND axis=? AND base_revision=? AND points=?");
        advance.integer(1,revision+1);advance.integer(2,points+1);advance.integer(3,newCell);advance.text(4,scope);advance.text(5,axis);
        advance.integer(6,revision);advance.integer(7,points);advance.step();
        if(sqlite3_changes(db)!=1)fail("Exact display prefix changed during native admission");
    }
    void coordinate(const QString& scope,const QString& axis,qint64 id,double x,double y){
        qint64 revision=0,points=0;
        {Statement q(db,"SELECT points,revision FROM native_plot_bounds WHERE scope=? AND axis=?");q.text(1,scope);q.text(2,axis);
         if(q.step()==SQLITE_ROW){if(sqlite3_column_type(q.row,0)!=SQLITE_INTEGER||sqlite3_column_type(q.row,1)!=SQLITE_INTEGER)fail("Source catalogue counters invalid");points=sqlite3_column_int64(q.row,0);revision=sqlite3_column_int64(q.row,1);}}
        if(points<0||revision<0||points==std::numeric_limits<qint64>::max()||revision==std::numeric_limits<qint64>::max())fail("Source catalogue integer capacity exhausted");
        Statement insert(db,"INSERT OR IGNORE INTO native_plot_coordinates(scope,axis,point_key,gap_id,x,y,admission_revision) VALUES(?,?,?,?,?,?,?)");
        insert.text(1,scope);insert.text(2,axis);insert.text(3,coordinateKey(x,id));insert.integer(4,id);insert.number(5,x);insert.number(6,y);insert.integer(7,revision+1);insert.step();
        if(!sqlite3_changes(db))return;
        Statement bounds(db,"INSERT INTO native_plot_bounds VALUES(?,?,?,?,?,?,1,1) ON CONFLICT(scope,axis) DO UPDATE SET min_x=MIN(min_x,excluded.min_x),max_x=MAX(max_x,excluded.max_x),min_y=MIN(min_y,excluded.min_y),max_y=MAX(max_y,excluded.max_y),points=points+1,revision=revision+1");
        bounds.text(1,scope);bounds.text(2,axis);bounds.number(3,x);bounds.number(4,x);bounds.number(5,y);bounds.number(6,y);bounds.step();
        appendDisplayCache(scope,axis,x,y,revision,points);
    }
    bool admit(const Gap& gap,const QString& run){
        Statement insert(db,"INSERT INTO gaps(p,q,gap,merit,unix_ts,time_basis) VALUES(?,?,?,?,?,?) ON CONFLICT(p,q) DO UPDATE SET time_basis=CASE WHEN gaps.unix_ts IS NOT NULL THEN gaps.time_basis WHEN excluded.unix_ts IS NOT NULL THEN excluded.time_basis ELSE COALESCE(gaps.time_basis,excluded.time_basis) END,unix_ts=COALESCE(gaps.unix_ts,excluded.unix_ts)");
        insert.text(1,gap.p);insert.text(2,gap.q);insert.text(3,gap.gap);insert.text(4,gap.merit);
        if(gap.time)insert.integer(5,*gap.time);else sqlite3_bind_null(insert.row,5);
        if(!gap.basis.isEmpty())insert.text(6,gap.basis);else sqlite3_bind_null(insert.row,6);
        // last_insert_rowid is deliberately not used: duplicates must keep the
        // original ID, merit and time, even when a second run observes them.
        Statement before(db,"SELECT 1 FROM gaps WHERE p=? AND q=?");before.text(1,gap.p);before.text(2,gap.q);const bool created=before.step()==SQLITE_DONE;
        insert.step();
        qint64 id{};double merit{},length{};std::optional<qint64> time;
        {Statement row(db,"SELECT id,gap,merit,unix_ts FROM gaps WHERE p=? AND q=?");row.text(1,gap.p);row.text(2,gap.q);
         if(row.step()!=SQLITE_ROW||row.text(1)!=gap.gap)fail("Stored exact gap identity conflicts");
         id=sqlite3_column_int64(row.row,0);bool ok=false;merit=row.text(2).toDouble(&ok);
         if(id<=0||!ok||!std::isfinite(merit))fail("Stored source coordinate invalid");
         length=gap.gap.toDouble();if(sqlite3_column_type(row.row,3)!=SQLITE_NULL){if(sqlite3_column_type(row.row,3)!=SQLITE_INTEGER)fail("Stored source time invalid");time=sqlite3_column_int64(row.row,3);if(*time<=0||*time>9007199254740991LL/1000)fail("Stored source time out of range");}}
        Statement origin(db,"INSERT OR IGNORE INTO origins VALUES(?,?,?)");origin.integer(1,id);origin.text(2,run);origin.text(3,"native/"+run+".stdout");origin.step();
        // Include every origin if an originally undated identity acquired its
        // first genuine source timestamp. Never invent one for old observations.
        QStringList scopes{QString{}};
        {Statement origins(db,"SELECT DISTINCT run FROM origins WHERE gap_id=? ORDER BY run");origins.integer(1,id);while(origins.step()==SQLITE_ROW){const auto scope=origins.text(0);validateRun(scope);scopes.push_back(scope);}}
        for(const auto& scope:scopes){coordinate(scope,"length",id,length,merit);if(time)coordinate(scope,"time",id,double(*time)*1000,merit);}
        return created;
    }
};
SourceHistory::SourceHistory(const QString& file):impl_(std::make_unique<Impl>(file)){}
SourceHistory::~SourceHistory()=default;
qint64 SourceHistory::cursor(const QString& run)const{validateRun(run);return impl_->cursor(run);}
std::optional<SourceCursorRecord> SourceHistory::cursorRecord(const QString& run)const{
    if(run.size()!=32)fail("Source cursor view run invalid");validateRun(run);auto* db=impl_->db;
    if(!sqlite3_get_autocommit(db))fail("Source cursor view requires no active owner transaction");
    const auto before=impl_->dataVersion();bool begun=false;
    try{
        execute(db,"BEGIN");begun=true;
        const auto source="native/"+run+".stdout";
        Statement query(db,"SELECT source,offset,run FROM cursors WHERE source=?");query.text(1,source);
        std::optional<qint64> offset;
        if(query.step()==SQLITE_ROW){
            const auto exactText=[&](int column,const QString& wanted){return sqlite3_column_type(query.row,column)==SQLITE_TEXT&&
                QByteArray(reinterpret_cast<const char*>(sqlite3_column_blob(query.row,column)),sqlite3_column_bytes(query.row,column))==wanted.toUtf8();};
            if(!exactText(0,source)||!exactText(2,run)||sqlite3_column_type(query.row,1)!=SQLITE_INTEGER||sqlite3_column_int64(query.row,1)<0)
                fail("Source cursor stored type/identity/range invalid");
            offset=sqlite3_column_int64(query.row,1);if(query.step()!=SQLITE_DONE)fail("Source cursor identity ambiguous");
        }
        execute(db,"ROLLBACK");begun=false;
        // Check OUTSIDE the read snapshot, too: do not inherit an older WAL
        // snapshot's version after an external commit. No writer lock/write.
        if(impl_->dataVersion()!=before)fail("Source cursor view changed during read; retry requires fresh evidence");
        if(!offset)return {};return SourceCursorRecord{*offset,before};
    }catch(...){if(begun)sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
}
void SourceHistory::bindSourceIdentity(const QString& run,const QString& identity,bool allowCreate){
    validateRun(run);if(identity.isEmpty()||identity.size()>4096)fail("Native source identity outside bound");
    auto* db=impl_->db;execute(db,"BEGIN IMMEDIATE");
    try{
        Statement query(db,"SELECT identity FROM native_source_identities WHERE run=?");query.text(1,run);
        if(query.step()==SQLITE_ROW){if(query.text(0)!=identity)fail("Durable native source identity changed");}
        else{
            if(!allowCreate||impl_->cursor(run)!=0)fail("Legacy native source requires explicit identity audit");
            Statement insert(db,"INSERT INTO native_source_identities(run,identity) VALUES(?,?)");insert.text(1,run);insert.text(2,identity);insert.step();
        }
        execute(db,"COMMIT");
    }catch(...){sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
}
namespace {
QJsonObject readIndexPublication(sqlite3* db,const QString& run,QSet<QString>& audited,bool archiveOnly=false){
        const auto bounds=[&](const QString& scope,const QString& axis){
            QJsonObject result{{"scope",scope},{"axis",axis},{"min_x",0},{"max_x",0},{"min_y",0},{"max_y",0},{"points",qint64(0)},{"revision",qint64(0)}};
            Statement row(db,"SELECT min_x,max_x,min_y,max_y,points,revision FROM native_plot_bounds WHERE scope=? AND axis=?");row.text(1,scope);row.text(2,axis);
            if(row.step()==SQLITE_ROW){
                if(sqlite3_column_type(row.row,4)!=SQLITE_INTEGER||sqlite3_column_type(row.row,5)!=SQLITE_INTEGER)fail("Native index cardinality type invalid");
                const auto points=sqlite3_column_int64(row.row,4),revision=sqlite3_column_int64(row.row,5);
                if(points<0||revision<points)fail("Native index cardinality/revision invalid");
                result["points"]=qint64(points);result["revision"]=qint64(revision);
                if(points){
                    const char* fields[]={"min_x","max_x","min_y","max_y"};
                    for(int c=0;c<4;++c){const auto value=sqlite3_column_double(row.row,c);if(sqlite3_column_type(row.row,c)==SQLITE_NULL||!std::isfinite(value))fail("Native index bound invalid");result[fields[c]]=value;}
                    if(result["min_x"].toDouble()>result["max_x"].toDouble()||result["min_y"].toDouble()>result["max_y"].toDouble())fail("Native index bounds inverted");
                }
            }
            if(!audited.contains(scope)){
                // Once per base/scope, not once per sample. Exact identities
                // are not converted to doubles. An incomplete legacy index
                // is held instead of announcing a smaller complete history.
                const QByteArray where=axis=="time"?" AND g.unix_ts IS NOT NULL":"";
                // A redundant WHERE 1 disables SQLite's specialized Count
                // opcode and walks every row in the all-length audit. Keep
                // the dated predicate, exact audits and snapshot unchanged.
                const QByteArray all=axis=="time"?"SELECT COUNT(*) FROM gaps g WHERE g.unix_ts IS NOT NULL":"SELECT COUNT(*) FROM gaps";
                const QByteArray sql=scope.isEmpty()?all:
                    "SELECT COUNT(DISTINCT o.gap_id) FROM gaps g JOIN origins o ON o.gap_id=g.id WHERE o.run=?"+where;
                Statement count(db,sql.constData());if(!scope.isEmpty())count.text(1,scope);
                if(count.step()!=SQLITE_ROW||sqlite3_column_int64(count.row,0)!=result["points"].toInteger(-1))fail("Native index incomplete; source retained, explicit backfill required");
                Statement coordinates(db,"SELECT COUNT(*) FROM native_plot_coordinates WHERE scope=? AND axis=?");coordinates.text(1,scope);coordinates.text(2,axis);
                if(coordinates.step()!=SQLITE_ROW||sqlite3_column_int64(coordinates.row,0)!=result["points"].toInteger(-1))fail("Native index coordinate/cardinality mismatch; source retained");
            }
            return result;
        };
        const auto allLength=bounds({},"length"),allTime=bounds({},"time");
        if(archiveOnly)return {{"type","native_index"},{"complete",true},{"archive_only",true},
            {"all",QJsonObject{{"length",allLength},{"time",allTime}}},
            {"snapshot_scope","LOCAL_ARCHIVE_NOT_SOURCE_READY_OR_LATEST"},
            {"proof_status","SOURCE_PRP_INDEPENDENT_PROOF_PENDING"}};
        const auto currentLength=bounds(run,"length"),currentTime=bounds(run,"time");
        audited.insert(QString{});audited.insert(run);
        return {{"type","native_index"},{"complete",true},{"all",QJsonObject{{"length",allLength},{"time",allTime}}},
            {"current",QJsonObject{{"scope",run},{"length",currentLength},{"time",currentTime}}},{"proof_status","SOURCE_PRP_INDEPENDENT_PROOF_PENDING"}};
}
qint64 indexDataVersion(sqlite3* db){Statement version(db,"PRAGMA data_version");if(version.step()!=SQLITE_ROW||sqlite3_column_type(version.row,0)!=SQLITE_INTEGER)fail("Native display version unavailable");return sqlite3_column_int64(version.row,0);}
}
QJsonObject SourceHistory::indexPublication(const QString& run){
    validateRun(run);auto* db=impl_->db;
    execute(db,"BEGIN IMMEDIATE");
    try{
        const auto version=impl_->dataVersion();auto audited=impl_->auditedIndexScopes;
        if(version!=impl_->indexVersion||(!audited.contains(run)&&audited.size()>=64))audited.clear();
        auto publication=readIndexPublication(db,run,audited);
        execute(db,"COMMIT");impl_->indexVersion=version;impl_->auditedIndexScopes=std::move(audited);return publication;
    }catch(...){sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
}
class SourceHistoryIndexReader::Impl {
public:
    sqlite3* db{};
    SourceIndexSnapshot snapshot(const QString& run,std::optional<qint64> committedOffset,const std::function<bool()>& cancelled);
    explicit Impl(const QString& file){
        const QFileInfo info(file);if(!info.isAbsolute()||!info.isFile()||info.isSymLink())fail("Existing regular display database required");
        if(sqlite3_open_v2(file.toUtf8().constData(),&db,SQLITE_OPEN_READONLY|SQLITE_OPEN_NOMUTEX,nullptr)!=SQLITE_OK){if(db)sqlite3_close(db);db=nullptr;fail("Readonly display database unavailable");}
         if(qEnvironmentVariableIsSet("PGN_HISTORY_SQL_PROFILE"))sqlite3_trace_v2(db,SQLITE_TRACE_STMT|SQLITE_TRACE_PROFILE,
             [](unsigned event,void*,void* statement,void* detail)->int{
                 if(event==SQLITE_TRACE_STMT)qInfo().noquote()<<"HISTORY SQL"<<sqlite3_sql(static_cast<sqlite3_stmt*>(statement));
                 else qInfo().noquote()<<"HISTORY SQL NS"<<*static_cast<sqlite3_int64*>(detail);
                 return 0;
             },nullptr);
    }
    ~Impl(){sqlite3_close(db);}
};
SourceHistoryIndexReader::SourceHistoryIndexReader(const QString& file):impl_(std::make_unique<Impl>(file)){}
SourceHistoryIndexReader::~SourceHistoryIndexReader()=default;
SourceIndexSnapshot SourceHistoryIndexReader::read(const QString& run,qint64 committedOffset,const std::function<bool()>& cancelled){
    validateRun(run);if(run.size()!=32||committedOffset<0)fail("Native display request identity/cursor invalid");
    return impl_->snapshot(run,committedOffset,cancelled);
}
QJsonObject SourceHistoryIndexReader::readArchive(const std::function<bool()>& cancelled){
    return impl_->snapshot({},std::nullopt,cancelled).publication;
}
SourceIndexSnapshot SourceHistoryIndexReader::Impl::snapshot(const QString& run,std::optional<qint64> committedOffset,const std::function<bool()>& cancelled){
    const auto stop=[&]{return cancelled&&cancelled();};if(stop())fail("Native display audit cancelled");
    const auto progress=[](void* value)->int{try{return (*static_cast<const std::function<bool()>*>(value))()?1:0;}catch(...){return 1;}};
    const std::function<bool()> check=stop;sqlite3_progress_handler(db,1000,progress,const_cast<std::function<bool()>*>(&check));
    try{
        execute(db,"BEGIN");qint64 capturedOffset{};
        if(committedOffset){Statement cursor(db,"SELECT offset,run FROM cursors WHERE source=?");cursor.text(1,"native/"+run+".stdout");
         if(cursor.step()!=SQLITE_ROW||sqlite3_column_type(cursor.row,0)!=SQLITE_INTEGER||sqlite3_column_type(cursor.row,1)!=SQLITE_TEXT||sqlite3_column_int64(cursor.row,0)<*committedOffset||cursor.text(1)!=run)fail("Native display snapshot cursor shrank/absent");capturedOffset=sqlite3_column_int64(cursor.row,0);if(cursor.step()!=SQLITE_DONE)fail("Native display cursor duplicated");}
        const auto version=indexDataVersion(db);QSet<QString> audited; // NO cross-snapshot metadata cache
        auto publication=readIndexPublication(db,run,audited,!committedOffset);execute(db,"COMMIT");
        if(stop())fail("Native display audit cancelled");
        sqlite3_progress_handler(db,0,nullptr,nullptr);return {std::move(publication),capturedOffset,version};
    }catch(...){sqlite3_progress_handler(db,0,nullptr,nullptr);sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);if(stop())fail("Native display audit cancelled");throw;}
}
SourcePrefix SourceHistory::verifySourcePrefix(const QString& run,qint64 size,const std::function<QByteArray(qint64,qsizetype)>& reader)const{
    validateRun(run);if(size<0||!reader)fail("Source prefix reader invalid");
    execute(impl_->db,"BEGIN");
    try{const auto version=impl_->dataVersion();const auto at=impl_->cursor(run);if(size<at)fail("Conserved source journal shrank; cursor retained");impl_->verifyPrefix(run,at,reader);execute(impl_->db,"COMMIT");return {at,version};}
    catch(...){sqlite3_exec(impl_->db,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
}
void SourceHistory::Impl::auditLegacyPrefix(const QString& run,qint64 offset,const QByteArray& expected,const std::function<QByteArray(qint64,qsizetype)>& reader){
    validateRun(run);if(offset<=0||!reader||expected.size()!=64)fail("Explicit legacy source digest/offset/reader required");
    for(const auto c:expected)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))fail("Legacy source digest encoding invalid");
        const auto source="native/"+run+".stdout";
        {Statement current(db,"SELECT offset,run FROM cursors WHERE source=?");current.text(1,source);
         if(current.step()!=SQLITE_ROW||sqlite3_column_type(current.row,0)!=SQLITE_INTEGER||
            sqlite3_column_int64(current.row,0)!=offset||sqlite3_column_type(current.row,1)!=SQLITE_TEXT||
            current.text(1)!=run||current.step()!=SQLITE_DONE)fail("Legacy enrollment cursor changed; no adoption");}
        {Statement existing(db,"SELECT 1 FROM native_source_batches WHERE source=? LIMIT 1");existing.text(1,source);if(existing.step()==SQLITE_ROW)fail("Source already/partly enrolled; verify existing enrollment instead of replacing it");}
        execute(db,"CREATE TEMP TABLE IF NOT EXISTS native_enrollment_seen(gap_id INTEGER PRIMARY KEY) WITHOUT ROWID; DELETE FROM native_enrollment_seen;");
        QCryptographicHash whole(QCryptographicHash::Sha256);qint64 at=0;QByteArray pending;
        while(at<offset){
            const auto requested=std::min<qint64>(65536,offset-at);const auto bytes=reader(at,requested);
            if(bytes.isEmpty()||bytes.size()>requested)fail("Legacy source prefix read incomplete");
            whole.addData(bytes);
            {Statement digest(db,"INSERT INTO native_source_batches VALUES(?,?,?,?)");digest.text(1,source);digest.integer(2,at);digest.integer(3,bytes.size());digest.text(4,QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()));digest.step();}
            at+=bytes.size();pending+=bytes;qsizetype from=0,end;
            while((end=pending.indexOf('\n',from))>=0){
                if(end-from>RowLimit)fail("Legacy source row exceeds admission bound");
                auto line=pending.mid(from,end-from);if(line.endsWith('\r'))line.chop(1);
                if(const auto gap=parse(line)){
                    Statement identity(db,"SELECT g.id,g.gap FROM gaps g JOIN origins o ON o.gap_id=g.id WHERE g.p=? AND g.q=? AND o.run=? AND o.source=?");
                    identity.text(1,gap->p);identity.text(2,gap->q);identity.text(3,run);identity.text(4,source);
                    if(identity.step()!=SQLITE_ROW||identity.text(1)!=gap->gap)fail("Legacy source GAP lacks matching committed identity/origin");
                    const auto id=sqlite3_column_int64(identity.row,0);if(id<=0)fail("Legacy GAP identity invalid");
                    Statement seen(db,"INSERT OR IGNORE INTO native_enrollment_seen VALUES(?)");seen.integer(1,id);seen.step();
                }
                from=end+1;
            }
            pending.remove(0,from);if(pending.size()>RowLimit)fail("Legacy incomplete row exceeds admission bound");
        }
        if(!pending.isEmpty())fail("Legacy cursor ends inside a record; no enrollment");
        if(whole.result().toHex()!=expected)fail("Legacy prefix differs from independently supplied source digest");
        {Statement absent(db,"SELECT 1 FROM origins o WHERE o.run=? AND o.source=? AND NOT EXISTS(SELECT 1 FROM native_enrollment_seen s WHERE s.gap_id=o.gap_id) LIMIT 1");absent.text(1,run);absent.text(2,source);if(absent.step()==SQLITE_ROW)fail("Committed source origin absent from legacy prefix");}
        execute(db,"DELETE FROM native_enrollment_seen");
}
void SourceHistory::enrollSourcePrefix(const QString& run,qint64 offset,const QByteArray& expected,const std::function<QByteArray(qint64,qsizetype)>& reader){
    // Shared audit, retaining the existing explicit digest-only API contract.
    execute(impl_->db,"PRAGMA temp_store=FILE; BEGIN IMMEDIATE");
    try{
        impl_->auditLegacyPrefix(run,offset,expected,reader);
        execute(impl_->db,"COMMIT");
        impl_->verifiedOffsets.remove(run);
    }catch(...){sqlite3_exec(impl_->db,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
}
void SourceHistory::enrollLegacySourceExisting(const QString& database,const QString& run,const QString& device,
    const QJsonObject& manifest,qint64 offset,const QByteArray& expected,
    const std::function<QByteArray(qint64,qsizetype)>& reader,const std::function<void()>& verifyHeldStable){
    // Compute the SAME declared identity as real observation before opening DB.
    const auto identity=QString::fromUtf8(QJsonDocument(nativeSourceObservationIdentity(manifest,run,device)).toJson(QJsonDocument::Compact));
    if(!verifyHeldStable||!reader||offset<=0)fail("Explicit held legacy prefix audit required");
    Impl existing(database,true);auto* db=existing.db;
    execute(db,"PRAGMA temp_store=FILE; BEGIN IMMEDIATE");
    try{
        execute(db,"CREATE TABLE IF NOT EXISTS native_source_identities(run TEXT PRIMARY KEY,identity TEXT NOT NULL) WITHOUT ROWID;"
                   "CREATE TABLE IF NOT EXISTS native_source_batches(source TEXT NOT NULL,offset INTEGER NOT NULL,length INTEGER NOT NULL,sha256 TEXT NOT NULL,PRIMARY KEY(source,offset)) WITHOUT ROWID;");
        {Statement prior(db,"SELECT 1 FROM native_source_identities WHERE run=?");prior.text(1,run);
         if(prior.step()==SQLITE_ROW)fail("Legacy identity already enrolled; no replacement or blind retry");}
        existing.auditLegacyPrefix(run,offset,expected,reader);
        {Statement insert(db,"INSERT INTO native_source_identities(run,identity) VALUES(?,?)");insert.text(1,run);insert.text(2,identity);insert.step();}
        // Callback does NOT release the handle. Caller retains the barrier and
        // external WRITE/DELETE exclusion for the entire COMMIT window.
        verifyHeldStable();
        execute(db,"COMMIT");
    }catch(...){sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
}
SourceImport SourceHistory::append(const QString& run,qint64 offset,const QByteArray& bytes,std::optional<qint64> verifiedDataVersion){
    validateRun(run);
    if(offset<0||bytes.size()>BatchLimit||(!bytes.isEmpty()&&!bytes.endsWith('\n'))||offset>std::numeric_limits<qint64>::max()-bytes.size())fail("Complete source batch bounds invalid");
    SourceImport result{offset+bytes.size()};
    execute(impl_->db,"BEGIN IMMEDIATE");
    try{
        if(verifiedDataVersion&&impl_->dataVersion()!=*verifiedDataVersion)fail("Source catalogue changed since byte-prefix verification; reattach required");
        if(impl_->cursor(run)!=offset)fail("Stale source offset; no bytes skipped or replayed");
        impl_->requireEnrolledPrefix(run,offset);
        qsizetype from=0;
        while(from<bytes.size()){
            const auto end=bytes.indexOf('\n',from);
            if(end<0||end-from>RowLimit)fail("Source row exceeds admission bound");
            auto line=bytes.mid(from,end-from);if(line.endsWith('\r'))line.chop(1);
            if(const auto gap=parse(line)){++result.admitted;result.inserted+=impl_->admit(*gap,run);}
            ++result.lines;from=end+1;
        }
        if(!bytes.isEmpty()){
            Statement integrity(impl_->db,"INSERT INTO native_source_batches VALUES(?,?,?,?)");integrity.text(1,"native/"+run+".stdout");integrity.integer(2,offset);integrity.integer(3,bytes.size());integrity.text(4,QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()));integrity.step();
        }
        Statement cursor(impl_->db,"INSERT INTO cursors VALUES(?,?,?) ON CONFLICT(source) DO UPDATE SET offset=excluded.offset,run=excluded.run");
        cursor.text(1,"native/"+run+".stdout");cursor.integer(2,result.offset);cursor.text(3,run);cursor.step();
        execute(impl_->db,"COMMIT");
        impl_->verifiedOffsets.insert(run,result.offset);
    }catch(...){sqlite3_exec(impl_->db,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
    return result;
}
SourceImport SourceHistory::importJournal(const QString& run,const QString& path){
    const auto offset=cursor(run);QFile file(path);
    if(!file.open(QIODevice::ReadOnly))fail("Conserved source journal unavailable");
    const auto end=file.size();if(end<offset||!file.seek(offset))fail("Conserved source journal shrank; cursor retained");
    impl_->verifyPrefix(run,offset,[&file](qint64 at,qsizetype size){if(!file.seek(at))fail("Source prefix seek unavailable");return file.read(size);});
    SourceImport result{offset};QByteArray pending;qint64 remaining=end-offset;
    while(remaining>0){
        const auto chunk=file.read(std::min<qint64>(65536,remaining));
        if(chunk.isEmpty())fail("Source journal read failed before captured EOF");
        remaining-=chunk.size();pending+=chunk;
        const auto newline=pending.lastIndexOf('\n');
        if(newline>=0){const auto batch=append(run,result.offset,pending.left(newline+1));result.offset=batch.offset;result.lines+=batch.lines;result.admitted+=batch.admitted;result.inserted+=batch.inserted;pending.remove(0,newline+1);}
        if(pending.size()>RowLimit)fail("Incomplete source row exceeds admission bound");
    }
    return result;
}
}
