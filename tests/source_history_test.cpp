#include "mining/source_history.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStringList>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <iostream>
#include <stdexcept>

namespace {
int checks=0;
void require(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);}
void sql(sqlite3* db,const char* text){if(sqlite3_exec(db,text,nullptr,nullptr,nullptr)!=SQLITE_OK)throw std::runtime_error("fixture SQL");}
struct Read {
    sqlite3* db{};
    explicit Read(const QString& file){if(sqlite3_open(file.toUtf8().constData(),&db)!=SQLITE_OK)throw std::runtime_error("fixture open");}
    ~Read(){sqlite3_close(db);}
    QString value(const char* text){sqlite3_stmt* s{};if(sqlite3_prepare_v2(db,text,-1,&s,nullptr)!=SQLITE_OK)throw std::runtime_error("fixture query");QString result;if(sqlite3_step(s)==SQLITE_ROW)result=QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(s,0)));sqlite3_finalize(s);return result;}
    QString plan(const QByteArray& query,const QString& run){sqlite3_stmt* s{};const auto explain="EXPLAIN QUERY PLAN "+query;if(sqlite3_prepare_v2(db,explain.constData(),-1,&s,nullptr)!=SQLITE_OK)throw std::runtime_error("fixture explain");const auto bytes=run.toUtf8();sqlite3_bind_text(s,1,bytes.constData(),bytes.size(),SQLITE_TRANSIENT);QStringList rows;while(sqlite3_step(s)==SQLITE_ROW)rows.append(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(s,3))));sqlite3_finalize(s);return rows.join('\n');}
    qint64 count(const QByteArray& query,const QString& run){sqlite3_stmt* s{};if(sqlite3_prepare_v2(db,query.constData(),-1,&s,nullptr)!=SQLITE_OK)throw std::runtime_error("fixture count prepare");const auto bytes=run.toUtf8();sqlite3_bind_text(s,1,bytes.constData(),bytes.size(),SQLITE_TRANSIENT);const bool ok=sqlite3_step(s)==SQLITE_ROW&&sqlite3_column_type(s,0)==SQLITE_INTEGER;const auto n=ok?sqlite3_column_int64(s,0):-1;sqlite3_finalize(s);if(!ok)throw std::runtime_error("fixture count result");return n;}
    QByteArray snapshot(){
        QByteArray output;
        for(const auto table:{"gaps","origins","cursors","native_plot_coordinates","native_plot_bounds","native_source_batches"}){
            const auto query=QByteArray("SELECT * FROM ")+table+" ORDER BY 1,2,3";sqlite3_stmt* s{};
            if(sqlite3_prepare_v2(db,query.constData(),-1,&s,nullptr)!=SQLITE_OK)throw std::runtime_error("snapshot query");
            while(sqlite3_step(s)==SQLITE_ROW){for(int c=0;c<sqlite3_column_count(s);++c){output+=QByteArray::number(sqlite3_column_type(s,c))+':';const auto* data=sqlite3_column_text(s,c);if(data)output+=QByteArray(reinterpret_cast<const char*>(data),sqlite3_column_bytes(s,c));output+='\x1f';}output+='\n';}
            sqlite3_finalize(s);output+='\x1e';
        }
        return QCryptographicHash::hash(output,QCryptographicHash::Sha256);
    }
};
template<class F> void rejected(F operation,const char* what){bool held=false;try{operation();}catch(const std::exception&){held=true;}require(held,what);}
QByteArray gap(const QByteArray& p="23",const QByteArray& q="29",const QByteArray& length="6",const QByteArray& extra={}){
    return "[phase0-gpu] GAP merit=1.9000 gap="+length+" lower="+p+" upper="+q+" verified=1 proof=GPU_MR12_EULER_COMPOSITE record=off table=unknown"+extra+'\n';
}
void write(const QString& path,const QByteArray& bytes,QIODevice::OpenMode mode=QIODevice::WriteOnly|QIODevice::Truncate){QFile f(path);if(!f.open(mode)||f.write(bytes)!=bytes.size())throw std::runtime_error("fixture source write");}
const QString AuditA(32,'a'),AuditB(32,'b'),AuditC(32,'c'),AuditEmpty(32,'d');
void seedAudit(pgn::mining::SourceHistory& history,Read& read){
    const auto a=gap()+gap("31","37","6"," unix_ts=1791982800")+gap();
    const auto b=gap("23","29","6"," unix_ts=1791982801")+gap("41","43","2")+gap("31","37","6"," unix_ts=1791989999");
    history.append(AuditA,0,a);history.append(AuditB,0,b);history.append(AuditC,0,gap("47","53","6"," unix_ts=1791982802"));
    // UNIQUE is (gap_id,run,source): multiple retained sources for one run's
    // identity are legal. A dangling legacy origin is not a real gap identity.
    sql(read.db,"INSERT INTO origins SELECT gap_id,run,'TESTONLY-retained-second-origin' FROM origins WHERE run='aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' AND gap_id=1;"
        "INSERT INTO origins VALUES(999999,'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa','TESTONLY-retained-orphan');"
        "CREATE INDEX legacy_origin_source_testonly ON origins(source);");
}
void indexAuditAccessPath(const QString& directory){
    const auto file=directory+"/run-first-audit.sqlite";QList<QJsonObject> golden;QByteArray original;
    const QByteArray oldSql="SELECT COUNT(DISTINCT g.id) FROM gaps g JOIN origins o ON o.gap_id=g.id WHERE o.run=?";
    const QByteArray newSql="SELECT COUNT(DISTINCT o.gap_id) FROM gaps g JOIN origins o ON o.gap_id=g.id WHERE o.run=?";
    const QStringList scopes{AuditA,AuditB,AuditC,AuditEmpty};
    {
        pgn::mining::SourceHistory history(file);Read read(file);seedAudit(history,read);for(const auto& run:scopes)golden.append(history.indexPublication(run));original=read.snapshot();
        require(read.value("SELECT COUNT(*) FROM origins WHERE run='aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'")=="4","fixture has repeated-source identity and orphan origin");
        require(read.value("SELECT unix_ts FROM gaps WHERE p='31'")=="1791982800"&&read.value("SELECT unix_ts IS NULL FROM gaps WHERE p='41'")=="1","golden genuine time/unknown time remain distinct");
    }
    {
        Read before(file);sql(before.db,"DROP INDEX native_origins_run_gap;");
        for(const auto& axis:{QByteArray{},QByteArray(" AND g.unix_ts IS NOT NULL")}){const auto plan=before.plan(oldSql+axis,AuditA);std::cout<<"SOURCE_AUDIT_PLAN before "<<(axis.isEmpty()?"length":"time")<<'\n'<<plan.toUtf8().constData()<<'\n';require(plan.contains("SCAN o")&&plan.contains("TEMP B-TREE"),"prior unindexed actual query scans origins and sorts DISTINCT");}
        require(before.snapshot()==original,"dropping only owned access-path index changes no scientific/archive row");
    }
    {
        pgn::mining::SourceHistory reopened(file);Read read(file);
        require(read.value("SELECT group_concat(name,',') FROM (SELECT name FROM pragma_index_info('native_origins_run_gap') ORDER BY seqno)")=="run,gap_id","regular reopen installs exact owned run-first covering index");
        require(read.value("SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name='legacy_origin_source_testonly'")=="1","unrelated retained index is untouched");
        for(const auto& axis:{QByteArray{},QByteArray(" AND g.unix_ts IS NOT NULL")}){
            const auto plan=read.plan(newSql+axis,AuditA);
            std::cout<<"SOURCE_AUDIT_PLAN after "<<(axis.isEmpty()?"length":"time")<<'\n'<<plan.toUtf8().constData()<<'\n';
            if(!plan.contains("SEARCH o USING COVERING INDEX native_origins_run_gap")||plan.contains("TEMP B-TREE"))std::cerr<<plan.toUtf8().constData()<<'\n';
            require(plan.contains("SEARCH o USING COVERING INDEX native_origins_run_gap")&&plan.contains("run=?")&&!plan.contains("SCAN o")&&!plan.contains("TEMP B-TREE"),"production rewrite searches run-first covering index without DISTINCT temporary tree");
            require(plan.contains("SEARCH g USING INTEGER PRIMARY KEY"),"gap join retained: orphan origins cannot count as real identities");
            for(const auto& run:scopes)require(read.count(oldSql+axis,run)==read.count(newSql+axis,run),"old/new exact DISTINCT count agrees across run/source duplicates/orphans/time/nulls");
        }
        for(qsizetype n=0;n<scopes.size();++n){const auto actual=reopened.indexPublication(scopes[n]);require(actual==golden[n],"all four audited bounds/counts/revisions equal immutable golden publication");const auto all=actual.value("all").toObject(),current=actual.value("current").toObject();require(all.value("length").toObject().value("points")==4&&all.value("time").toObject().value("points")==3,"global identities/time indexes remain fully audited, not a run-only approximation");const qint64 lengths[]={2,3,1,0},times[]={2,2,1,0};require(current.value("length").toObject().value("points")==lengths[n]&&current.value("time").toObject().value("points")==times[n],"numeric current cardinalities preserve distinct identity and real/null time semantics");}
        require(read.snapshot()==original,"reopen/index audit/plan/counts change no original scientific rows, origins, cursors or index data");
    }
    for(int poison=0;poison<8;++poison){const auto poisoned=directory+"/run-first-poison-"+QString::number(poison)+".sqlite";pgn::mining::SourceHistory history(poisoned);Read read(poisoned);seedAudit(history,read);history.indexPublication(AuditA);
        // Every mutation is another OWN connection: warmed audit state must
        // still invalidate on data_version, never trust cached aggregate data.
        switch(poison){
            case 0:sql(read.db,"DELETE FROM native_plot_coordinates WHERE scope='' AND axis='length' AND point_key=(SELECT point_key FROM native_plot_coordinates WHERE scope='' AND axis='length' LIMIT 1);");break;
            case 1:sql(read.db,"DELETE FROM native_plot_coordinates WHERE scope='aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' AND axis='time' AND point_key=(SELECT point_key FROM native_plot_coordinates WHERE scope='aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' AND axis='time' LIMIT 1);");break;
            case 2:sql(read.db,"DELETE FROM gaps WHERE id=1;");break;
            case 3:sql(read.db,"INSERT INTO gaps(p,q,gap,merit,unix_ts) VALUES('101','103','2','1.9000',NULL);");break;
            case 4:sql(read.db,"UPDATE native_plot_bounds SET points=points+1,revision=revision+1 WHERE scope='aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' AND axis='length';");break;
            case 5:sql(read.db,"INSERT INTO native_plot_coordinates VALUES('aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa','length','TESTONLY-orphan-point',999999,2,1.9,1);");break;
            case 6:sql(read.db,"UPDATE gaps SET unix_ts=NULL WHERE id=1;");break;
            case 7:sql(read.db,"UPDATE native_plot_bounds SET points='unknown' WHERE scope='' AND axis='length';");break;
        }
        const auto before=read.snapshot();rejected([&]{history.indexPublication(AuditA);},"indexed access path retains exact cardinality/type corruption Held after external commit");require(read.snapshot()==before,"failed audit preserves every original/poisoned row, no repair/drop/adoption");
    }
    // Existing-only legacy enrollment is not the regular constructor path.
    const auto enrollment=directory+"/existing-only-no-index.sqlite";const auto bytes=gap();
    {pgn::mining::SourceHistory history(enrollment);history.append(AuditA,0,bytes);Read read(enrollment);sql(read.db,"DROP INDEX native_origins_run_gap; DELETE FROM native_source_batches;");}
    const QJsonObject manifest{{"schema","pgn-native-process-1"},{"run",AuditA},{"uuid","GPU-11111111-2222-3333-4444-555555555555"},{"pid",4242},{"birth","2026-10-15T00:00:00.1234567Z"},{"start","20"},{"end","40"},{"stdout","C:/PUBLIC-TESTONLY/"+AuditA+".stdout"}};
    pgn::mining::SourceHistory::enrollLegacySourceExisting(enrollment,AuditA,"GPU-11111111-2222-3333-4444-555555555555",manifest,bytes.size(),QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex(),[&](qint64 at,qsizetype count){return bytes.mid(at,count);},[]{});
    Read enrolled(enrollment);require(enrolled.value("SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name='native_origins_run_gap'")=="0","existing-only enrollment never installs new covering index or executes regular migration DDL");
}
}
void readonlyIndexReader(const QString& directory){
    using namespace pgn::mining;const auto file=directory+"/readonly-display.sqlite";SourceHistory writer(file);Read inspect(file);seedAudit(writer,inspect);
    QByteArray rows;for(int i=0;i<2048;++i){const auto p=QByteArray::number(1000000+10*i);rows+=gap(p,QByteArray::number(1000006+10*i));if(rows.size()>60000){writer.append(AuditB,writer.cursor(AuditB),rows);rows.clear();}}if(!rows.isEmpty())writer.append(AuditB,writer.cursor(AuditB),rows);
    const auto offset=writer.cursor(AuditA);const auto golden=writer.indexPublication(AuditA);SourceHistoryIndexReader reader(file);const auto original=inspect.snapshot();const auto first=reader.read(AuditA,offset);require(first.publication==golden&&first.committedOffset==offset,"readonly reader keeps every exact all/current count and bound");require(inspect.snapshot()==original,"display reader makes no schema/scientific/index write");
    const auto archive=reader.readArchive();require(archive.value("complete")==true&&archive.value("archive_only")==true&&archive.value("all")==golden.value("all"),"offline ALL exactly matches audited retained archive");
    require(!archive.contains("current")&&!archive.contains("source_run")&&!archive.contains("snapshot_committed_offset")&&!archive.contains("ready"),"archive invents no current run, conserved cursor or readiness");require(inspect.snapshot()==original,"archive read preserves all original schema/scientific/index rows");
    int archiveProbes=0;rejected([&]{reader.readArchive([&]{return ++archiveProbes>=2;});},"offline archive SQL audit is cancellable");require(archiveProbes>=2&&reader.readArchive()==archive,"archive cancellation releases read snapshot");
    int probes=0;bool committed=false;const auto past=reader.read(AuditA,offset,[&]{if(++probes==2){writer.append(AuditA,writer.cursor(AuditA),gap("999991","999997","6"));committed=true;}return false;});
    require(committed&&probes>=2,"real writer commits DURING readonly SQLite audit progress, not before snapshot");require(past.publication==golden&&past.committedOffset==offset,"coherent past snapshot keeps its exact old run cursor under continuous same-run commits, no latest claim");const auto newer=writer.indexPublication(AuditA);const auto next=reader.read(AuditA,offset);require(newer!=golden&&next.publication==newer&&next.committedOffset>offset,"next snapshot audits external writer drift and newer cursor, never metadata-only cache");
    probes=0;rejected([&]{reader.read(AuditA,offset,[&]{return ++probes>=2;});},"actual SQLite progress handler interrupts display audit");require(probes>=2&&reader.read(AuditA,offset).publication==newer,"cancel releases transaction and cannot poison subsequent render");
    rejected([&]{reader.read(AuditA,writer.cursor(AuditA)+1);},"cursor beyond captured conserved prefix held");rejected([&]{reader.read(AuditEmpty,0);},"missing cursor is not fabricated zero");
    sql(inspect.db,"DELETE FROM native_plot_coordinates WHERE scope='' AND axis='length' AND point_key=(SELECT MIN(point_key) FROM native_plot_coordinates WHERE scope='' AND axis='length');");
    const auto poisoned=inspect.snapshot();rejected([&]{reader.read(AuditA,offset);},"global incomplete index stays Held despite valid current run");require(inspect.snapshot()==poisoned,"Held reader does not backfill or conceal corruption");
    rejected([&]{reader.readArchive();},"offline incomplete index is explicit failure, not zero or partial history");require(inspect.snapshot()==poisoned,"archive refusal does not repair/drop/rewrite observations");
    const auto cursorless=directory+"/archive-without-source.sqlite";SourceHistory local(cursorless);local.append(AuditA,0,gap());Read localInspect(cursorless);sql(localInspect.db,"DELETE FROM cursors;");SourceHistoryIndexReader localReader(cursorless);
    require(localReader.readArchive().value("all").toObject().value("length").toObject().value("points").toInteger()==1,"retained archive works without any remote/source cursor");rejected([&]{localReader.read(AuditA,0);},"archive access cannot bypass missing SOURCE cursor gate");
    const auto absent=directory+"/not-created.sqlite";rejected([&]{SourceHistoryIndexReader missing(absent);},"readonly reader refuses absent DB");require(!QFileInfo::exists(absent),"no reader CREATE/migration side effect");
}
void nativeDisplayCacheAdmission(const QString& directory){
    const auto file=directory+"/display-admission.sqlite";pgn::mining::SourceHistory history(file);Read read(file);
    const auto cached=[&]{return read.value("SELECT group_concat(v,'|') FROM (SELECT printf('%s:%s:%.17g:%.17g:%lld',scope,axis,x,y,multiplicity) v FROM native_plot_multiplicity UNION ALL SELECT printf('%s:%s:%lld:%lld:%lld',scope,axis,base_revision,points,cells) v FROM native_plot_multiplicity_manifest ORDER BY v)");};
    const auto parity=[&]{
        require(read.value("WITH raw AS (SELECT scope,axis,x,y,COUNT(*) n FROM native_plot_coordinates WHERE scope='' GROUP BY scope,axis,x,y),cache AS (SELECT scope,axis,x,y,multiplicity n FROM native_plot_multiplicity) SELECT (SELECT COUNT(*) FROM (SELECT * FROM raw EXCEPT SELECT * FROM cache))+(SELECT COUNT(*) FROM (SELECT * FROM cache EXCEPT SELECT * FROM raw))")=="0",
            "native admission cache equals exact coordinate multiset in BOTH directions, no sampling or averaging");
        require(read.value("SELECT COUNT(*) FROM native_plot_bounds b LEFT JOIN native_plot_multiplicity_manifest m USING(scope,axis) WHERE b.scope='' AND (m.points IS NULL OR b.points!=m.points OR b.revision!=m.base_revision)")=="0",
            "every cached ALL axis stays at its exact native points/revision");
    };
    history.append(AuditA,0,gap());parity();
    require(read.value("SELECT cells FROM native_plot_multiplicity_manifest WHERE axis='length'")=="1","first source creates a complete exact display prefix without scanning history");
    const auto first=cached();history.append(AuditB,0,gap());
    require(cached()==first,"same observed gap in another run does not duplicate ALL display multiplicity or revision");
    history.append(AuditA,history.cursor(AuditA),gap("31","37"));parity();
    require(read.value("SELECT multiplicity FROM native_plot_multiplicity WHERE axis='length'")=="2"&&
        read.value("SELECT cells FROM native_plot_multiplicity_manifest WHERE axis='length'")=="1","different exact gaps sharing one display coordinate retain multiplicity two, not one lost record");
    history.append(AuditB,history.cursor(AuditB),gap("23","29","6"," unix_ts=1791982800"));parity();
    require(read.value("SELECT points FROM native_plot_multiplicity_manifest WHERE axis='time'")=="1"&&
        read.value("SELECT points FROM native_plot_multiplicity_manifest WHERE axis='length'")=="2","first original time adds only the true TIME coordinate, never duplicates LENGTH or invents other dates");
    const auto nativeBefore=read.snapshot();const auto cacheBefore=cached();const auto cursor=history.cursor(AuditA);
    rejected([&]{history.append(AuditA,cursor,gap("41","43","2")+gap("47","53","2"));},"invalid later source record holds entire admission");
    require(read.snapshot()==nativeBefore&&cached()==cacheBefore&&history.cursor(AuditA)==cursor,"later record failure rolls back source, all coordinates, exact cells, manifest AND cursor together");
    sql(read.db,"CREATE TRIGGER injected_cache_fault BEFORE UPDATE ON native_plot_multiplicity_manifest BEGIN SELECT RAISE(ABORT,'closed cache manifest fault'); END");
    rejected([&]{history.append(AuditA,cursor,gap("41","43","2"));},"late display manifest I/O failure cannot partially admit source");
    require(read.snapshot()==nativeBefore&&cached()==cacheBefore&&history.cursor(AuditA)==cursor,"injected manifest fault rolls back prior source/index/cell writes in SAME transaction");
    sql(read.db,"DROP TRIGGER injected_cache_fault");history.append(AuditA,cursor,gap("41","43","2"));parity();
    // A legitimate older prefix retains first two LENGTH observations only.
    sql(read.db,"DELETE FROM native_plot_multiplicity WHERE axis='length' AND x=2;UPDATE native_plot_multiplicity_manifest SET base_revision=2,points=2,cells=1 WHERE axis='length'");
    const auto stale=cached();history.append(AuditA,history.cursor(AuditA),gap("47","53"));
    require(cached()==stale&&read.value("SELECT points FROM native_plot_bounds WHERE scope='' AND axis='length'")=="4","stale prefix remains exactly old; new raw suffix is conserved, not falsely marked caught up");
    const auto legacy=directory+"/legacy-no-display-cache.sqlite";
    {pgn::mining::SourceHistory old(legacy);old.append(AuditA,0,gap());}
    {Read old(legacy);sql(old.db,"DROP TABLE native_plot_multiplicity;DROP TABLE native_plot_multiplicity_manifest");}
    {pgn::mining::SourceHistory reopened(legacy);reopened.append(AuditA,reopened.cursor(AuditA),gap("31","37"));Read old(legacy);
     require(old.value("SELECT COUNT(*) FROM native_plot_multiplicity_manifest")=="0"&&old.value("SELECT points FROM native_plot_bounds WHERE scope='' AND axis='length'")=="2","legacy missing prefix is neither scanned synchronously nor fabricated from only its new suffix");}
}
void boundedStorageAdmission(const QString& directory){
    const auto retired=directory+"/scientific-history.sqlite";
    rejected([&]{pgn::mining::SourceHistory forbidden(retired);},"retired unbounded filename refused");
    require(!QFileInfo::exists(retired),"retired database is never recreated");
    const auto oversized=directory+"/over-budget.sqlite";
    {QFile file(oversized);require(file.open(QIODevice::WriteOnly),"oversized fixture open");
     require(file.resize(64LL*1024*1024+1),"oversized fixture length");}
    rejected([&]{pgn::mining::SourceHistory forbidden(oversized);},"existing oversized cache refused before migration");
    require(QFileInfo(oversized).size()==64LL*1024*1024+1,"oversized refusal preserves input bytes");
    QFile::remove(oversized);
    const auto cache=directory+"/bounded-cache.sqlite";
    {pgn::mining::SourceHistory history(cache);history.append(AuditA,0,gap());}
    Read read(cache);require(read.value("PRAGMA journal_mode")=="wal","concurrent readers retain WAL snapshot semantics");
    require(QFileInfo(cache+"-wal").size()<=8LL*1024*1024,"idle cache WAL stays within admission budget");
    const auto heldFile=directory+"/held-reader-cache.sqlite";
    pgn::mining::SourceHistory held(heldFile);Read reader(heldFile);
    held.append(AuditA,0,"seed\n");
    sql(reader.db,"BEGIN");reader.value("SELECT count(*) FROM gaps");
    qint64 offset=5;bool blocked=false;
    for(int attempt=0;attempt<3000&&!blocked;++attempt){
        try{held.append(AuditA,offset,"tick\n");offset+=5;}
        catch(const std::exception&){blocked=true;}
    }
    require(blocked,"pinned reader produces bounded writer backpressure");
    require(held.cursor(AuditA)==offset,"blocked cache append does not advance cursor");
    require(QFileInfo(heldFile+"-wal").size()<64LL*1024*1024,"pinned-reader WAL cannot grow to gigabytes");
    sql(reader.db,"COMMIT");
    held.append(AuditA,offset,"tick\n");
    require(held.cursor(AuditA)==offset+5,"writer recovers after pinned reader releases");

}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    try{
        QTemporaryDir temp;require(temp.isValid(),"temporary directory");
        boundedStorageAdmission(temp.path());
        if(app.arguments().contains("--display-cache-only")){nativeDisplayCacheAdmission(temp.path());std::cout<<"SOURCE_HISTORY_DISPLAY_CACHE "<<checks<<" checks / 0 failures; exact atomic incremental cache, source conservation and stale-prefix fallback only\n";return 0;}
        if(app.arguments().contains("--index-reader-only")){readonlyIndexReader(temp.path());std::cout<<"SOURCE_HISTORY_READONLY_INDEX "<<checks<<" checks / 0 failures; exact coherent past snapshot/cancellation/writer concurrency only\n";return 0;}
        if(app.arguments().contains("--index-access-only")){indexAuditAccessPath(temp.path());std::cout<<"SOURCE_HISTORY_RUN_FIRST "<<checks<<" checks / 0 failures; closed SQLite golden cardinalities and actual EXPLAIN access paths only; no real DB/physical performance claim\n";return 0;}
        const auto file=temp.filePath("history.sqlite");const QString a(32,'a'),b(32,'b');
        pgn::mining::SourceHistory store(file);Read read(file);
        const auto first=gap();auto result=store.append(a,0,first);
        require(result.inserted==1&&result.admitted==1&&result.lines==1&&result.offset==first.size(),"first admission");
        require(read.value("SELECT COUNT(*) FROM gaps")=="1","one exact identity");
        require(read.value("SELECT COUNT(*) FROM native_plot_coordinates")=="2","all and run length coordinates");
        require(read.value("SELECT COUNT(*) FROM native_plot_coordinates WHERE axis='time'")=="0","no invented timestamp");
        require(read.value("SELECT point_key FROM native_plot_coordinates WHERE scope='' AND axis='length'")==QStringLiteral("c018000000000000")+QStringLiteral("0000000000000001"),"cross-language sortable key");
        require(read.value("SELECT revision FROM native_plot_bounds WHERE scope='' AND axis='length'")=="1","first revision");
        const auto duplicate=store.append(a,result.offset,first);require(duplicate.inserted==0&&duplicate.admitted==1,"duplicate admitted not duplicated");
        require(read.value("SELECT revision FROM native_plot_bounds WHERE scope='' AND axis='length'")=="1","duplicate no revision drift");
        const auto timed=gap("23","29","6"," unix_ts=1791982800");store.append(b,0,timed);
        require(read.value("SELECT unix_ts FROM gaps")=="1791982800","first genuine event time attached");
        require(read.value("SELECT COUNT(*) FROM native_plot_coordinates WHERE axis='time'")=="3","timestamp indexes all origins");
        require(read.value("SELECT COUNT(*) FROM origins")=="2","cross-run origins retained");
        store.append(b,timed.size(),gap("23","29","6"," unix_ts=1791989999"));
        require(read.value("SELECT unix_ts FROM gaps")=="1791982800","original event time never overwritten");
        auto before=read.snapshot();
        rejected([&]{store.append(a,0,first);},"stale offset held");require(read.snapshot()==before,"stale offset zero logical delta");
        const auto offset=store.cursor(a);
        rejected([&]{store.append(a,offset,gap("31","37","6")+gap("41","43","6"));},"bad second identity rolls back batch");
        require(read.snapshot()==before&&store.cursor(a)==offset,"gap/index/origin/cursor rollback");
        for(const auto& malformed:{gap("023","29","6"),gap("29","23","6"),gap("23","29","0"),gap("23","29","6"," unix_ts=0"),gap("23","29","6"," unix_ts=-1"),gap("23","29","6"," unix_ts=9007199254741"),gap("23","29","6"," unix_ts=123 unix_ts=124"),QByteArray("[phase0-gpu] GAP broken\n"),QByteArray(32769,'x')+'\n'}){
            rejected([&]{store.append(a,offset,malformed);},"malformed source held");require(read.snapshot()==before,"malformed source zero logical delta");
        }
        rejected([&]{store.append(a,offset,first.left(first.size()-1));},"partial append held");
        rejected([&]{store.append("../bad",offset,first);},"run traversal held");
        require(read.snapshot()==before,"invalid request zero delta");
        sql(read.db,"CREATE TRIGGER closed_fault BEFORE INSERT ON native_plot_coordinates WHEN NEW.axis='time' BEGIN SELECT RAISE(ABORT,'closed injected index fault'); END;");
        rejected([&]{store.append(a,offset,gap("31","37","6"," unix_ts=1791982801"));},"index failure held");
        require(read.snapshot()==before,"late index fault atomically rolls back exact rows and cursor");
        sql(read.db,"DROP TRIGGER closed_fault;");
        const QByteArray p="1"+QByteArray(300,'0')+"1",q="1"+QByteArray(300,'0')+"3";
        const auto large=gap(p,q,"2");store.append(a,offset,large);
        require(read.value("SELECT length(p) FROM gaps ORDER BY id DESC LIMIT 1")=="302","endpoint beyond booking160 bound preserved");
        require(read.value("SELECT gap FROM gaps ORDER BY id DESC LIMIT 1")=="2","large endpoints subtract exactly");
        const auto advanced=store.cursor(a);before=read.snapshot();
        sql(read.db,"UPDATE gaps SET gap='7' WHERE p='23' AND q='29';");const auto poisoned=read.snapshot();
        rejected([&]{store.append(a,advanced,first);},"existing exact identity poison rejected");require(read.snapshot()==poisoned,"poison refusal no metadata update");
        sql(read.db,"UPDATE gaps SET gap='6' WHERE p='23' AND q='29';");require(read.snapshot()==before,"fixture restored");
        // Every byte split is a real close/reopen/import seam. All runs reuse the
        // same exact gap, so neither partial EOF nor recovery can duplicate it.
        for(qsizetype split=0;split<=first.size();++split){
            const auto run=QString::number(split,16).rightJustified(32,'0');const auto journal=temp.filePath(run+".stdout");
            write(journal,first.left(split));const auto hash=QCryptographicHash::hash(first.left(split),QCryptographicHash::Sha256);
            {pgn::mining::SourceHistory closed(file);const auto partial=closed.importJournal(run,journal);require(partial.offset==(split==first.size()?first.size():0),"partial EOF cursor boundary");}
            QFile original(journal);require(original.open(QIODevice::ReadOnly)&&QCryptographicHash::hash(original.readAll(),QCryptographicHash::Sha256)==hash,"source bytes unchanged");original.close();
            write(journal,first.mid(split),QIODevice::WriteOnly|QIODevice::Append);
            {pgn::mining::SourceHistory closed(file);const auto complete=closed.importJournal(run,journal);require(complete.offset==first.size()&&closed.cursor(run)==first.size(),"reopen exact cursor");require(closed.importJournal(run,journal).admitted==0,"replay EOF idempotent");}
        }
        require(read.value("SELECT COUNT(*) FROM gaps")=="2","all split recoveries no duplicate endpoints");
        const auto journal=temp.filePath(a+".stdout");write(journal,"short\n");before=read.snapshot();
        rejected([&]{store.importJournal(a,journal);},"journal shrink refused");require(read.snapshot()==before,"shrink zero logical delta");
        // ID/cursor storage is signed64, not renderer-sized32 or JS Number.
        const auto wideFile=temp.filePath("wide.sqlite");pgn::mining::SourceHistory wide(wideFile);Read wideRead(wideFile);
        sql(wideRead.db,"INSERT INTO gaps(id,p,q,gap,merit,unix_ts) VALUES(4294967296,'101','103','2','1.0000',NULL);");wide.append(a,0,gap("101","103","2"));
        require(wideRead.value("SELECT gap_id FROM native_plot_coordinates LIMIT 1")=="4294967296","64bit gap ID retained");
        require(wideRead.value("SELECT point_key FROM native_plot_coordinates LIMIT 1").endsWith("0000000100000000"),"64bit key suffix retained");
        sql(wideRead.db,"UPDATE cursors SET offset=9007199254740993;");require(wide.cursor(a)==9007199254740993LL,"cursor beyond double safe integer retained");
        rejected([&]{wide.append(a,9007199254740993LL,"status\n");},"64bit cursor poison cannot skip uncommitted bytes");
        require(wide.cursor(a)==9007199254740993LL,"refused cursor poison never rounded or reset");
        // Real64KiB chunk boundaries, not a simulated one-record append.
        const QString chunkRun(32,'c');const auto chunkFile=temp.filePath(chunkRun+".stdout");
        const QByteArray chunkBytes=QByteArray(32768,'x')+'\n'+QByteArray(32768,'y')+'\n'+gap("47","53","6")+"unfinished";
        write(chunkFile,chunkBytes);const auto chunkResult=store.importJournal(chunkRun,chunkFile);
        require(chunkResult.lines==3&&chunkResult.admitted==1&&chunkResult.offset==chunkBytes.size()-10,"65536 byte boundary leaves partial EOF");
        write(chunkFile,"\n",QIODevice::WriteOnly|QIODevice::Append);
        require(store.importJournal(chunkRun,chunkFile).lines==1,"next complete tail consumed once");
        const auto committedChunks=read.snapshot();const auto committedOffset=store.cursor(chunkRun);
        auto changed=chunkBytes+'\n';changed[2]='z';write(chunkFile,changed);
        rejected([&]{store.importJournal(chunkRun,chunkFile);},"same-length earlier prefix edit rejected");
        require(read.snapshot()==committedChunks&&store.cursor(chunkRun)==committedOffset,"same-length rewrite zero logical delta");
        write(chunkFile,changed+first);
        rejected([&]{store.importJournal(chunkRun,chunkFile);},"longer replacement cannot skip unobserved prefix");
        require(read.snapshot()==committedChunks,"longer replacement zero logical delta");
        write(chunkFile,chunkBytes+'\n');
        require(store.importJournal(chunkRun,chunkFile).offset==committedOffset,"restored exact source resumes without duplication");
        sql(read.db,"UPDATE native_source_batches SET sha256=printf('%064d',0) WHERE source='native/cccccccccccccccccccccccccccccccc.stdout' AND offset=0;");
        const auto hashPoison=read.snapshot();rejected([&]{store.importJournal(chunkRun,chunkFile);},"integrity digest poison rejected");require(read.snapshot()==hashPoison,"digest poison refusal no reset");
        // Migrate the earlier coordinate schema without dropping a point.
        const auto oldFile=temp.filePath("old.sqlite");{Read old(oldFile);sql(old.db,"CREATE TABLE native_plot_coordinates(scope TEXT NOT NULL,axis TEXT NOT NULL,point_key TEXT NOT NULL,gap_id INTEGER NOT NULL,x REAL NOT NULL,y REAL NOT NULL,PRIMARY KEY(scope,axis,point_key)) WITHOUT ROWID; INSERT INTO native_plot_coordinates VALUES('','length','old-key',99,10,2);");}
        {pgn::mining::SourceHistory old(oldFile);old.append(a,0,first);Read migrated(oldFile);require(migrated.value("SELECT COUNT(*) FROM native_plot_coordinates WHERE point_key='old-key'")=="1","legacy coordinate preserved");require(migrated.value("SELECT admission_revision FROM native_plot_coordinates WHERE point_key='old-key'")=="0","legacy revision retained");}
        const auto legacyFile=temp.filePath("legacy-cursor.sqlite");pgn::mining::SourceHistory legacy(legacyFile);Read legacyRead(legacyFile);
        sql(legacyRead.db,"INSERT INTO cursors VALUES('native/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.stdout',5,'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa');");const auto legacyBefore=legacyRead.snapshot();
        rejected([&]{legacy.append(a,5,first);},"unenrolled legacy tail not silently adopted");require(legacyRead.snapshot()==legacyBefore,"legacy refusal zero logical delta");
        const QByteArray legacyBytes="test\n";const auto legacyHash=QCryptographicHash::hash(legacyBytes,QCryptographicHash::Sha256).toHex();
        const auto legacyReader=[&](qint64 at,qsizetype count){return legacyBytes.mid(at,count);};
        rejected([&]{legacy.enrollSourcePrefix(a,5,QByteArray(64,'0'),legacyReader);},"wrong independent legacy digest held");require(legacyRead.snapshot()==legacyBefore,"failed enrollment entire logical state unchanged");
        legacy.enrollSourcePrefix(a,5,legacyHash,legacyReader);
        require(legacy.verifySourcePrefix(a,5,legacyReader).offset==5,"explicit hash-bound empty-science legacy enrollment");
        const auto enrolledBefore=legacyRead.snapshot();rejected([&]{legacy.enrollSourcePrefix(a,5,legacyHash,legacyReader);},"already enrolled metadata never replaced on retry");require(legacyRead.snapshot()==enrolledBefore,"enrollment retry no delta");
        const auto scientificFile=temp.filePath("legacy-science.sqlite");pgn::mining::SourceHistory scientific(scientificFile);Read scientificRead(scientificFile);
        const auto second=gap("31","37","6"),scientificBytes=first+second;scientific.append(a,0,scientificBytes);
        // Closed compatibility fixture: remove native-only metadata to model
        // an old schema, not a real Node service/database or historical receipt.
        sql(scientificRead.db,"DELETE FROM native_source_batches;");const auto scientificBefore=scientificRead.snapshot();
        rejected([&]{scientific.enrollSourcePrefix(a,scientificBytes.size(),QCryptographicHash::hash(scientificBytes,QCryptographicHash::Sha256).toHex(),[&](qint64 at,qsizetype count){auto bytes=scientificBytes.mid(at,count);if(at==0)bytes.replace("lower=23","lower=24");return bytes;});},"legacy GAP identity mismatch held");
        require(scientificRead.snapshot()==scientificBefore,"legacy identity refusal no new rows or digests");
        sql(scientificRead.db,("UPDATE cursors SET offset="+QByteArray::number(first.size())).constData());const auto phantomBefore=scientificRead.snapshot();
        rejected([&]{scientific.enrollSourcePrefix(a,first.size(),QCryptographicHash::hash(first,QCryptographicHash::Sha256).toHex(),[&](qint64 at,qsizetype count){return first.mid(at,count);});},"stored source origin missing from prefix held");require(scientificRead.snapshot()==phantomBefore,"reverse-origin refusal atomically rolls back enrollment");
        sql(scientificRead.db,("UPDATE cursors SET offset="+QByteArray::number(scientificBytes.size())).constData());
        scientific.enrollSourcePrefix(a,scientificBytes.size(),QCryptographicHash::hash(scientificBytes,QCryptographicHash::Sha256).toHex(),[&](qint64 at,qsizetype count){return scientificBytes.mid(at,count);});
        require(scientificRead.value("SELECT COUNT(*) FROM gaps")=="2"&&scientificRead.value("SELECT COUNT(*) FROM origins")=="2","successful enrollment changes no scientific rows/origins");
        require(scientificRead.value("SELECT COUNT(*) FROM native_source_batches")=="1","successful enrollment stores exact prefix digest");
        require(scientific.verifySourcePrefix(a,scientificBytes.size(),[&](qint64 at,qsizetype count){return scientificBytes.mid(at,count);}).offset==scientificBytes.size(),"enrolled scientific prefix validates via bounded reader");
        scientific.append(a,scientificBytes.size(),gap("41","43","2"));require(scientificRead.value("SELECT COUNT(*) FROM gaps")=="3","native append resumes after explicit audited enrollment");
        const auto oldClockFile=temp.filePath("old-clock.sqlite");{Read old(oldClockFile);sql(old.db,"CREATE TABLE gaps(id INTEGER PRIMARY KEY,p TEXT NOT NULL,q TEXT NOT NULL,gap TEXT NOT NULL,merit TEXT NOT NULL,unix_ts INTEGER,UNIQUE(p,q)); INSERT INTO gaps VALUES(1,'23','29','6','1.9000',123);");}
        {pgn::mining::SourceHistory old(oldClockFile);Read oldRead(oldClockFile);old.append(a,0,gap("23","29","6"," unix_ts=1791982800 time_basis=source_batch_publication"));
         require(oldRead.value("SELECT unix_ts FROM gaps")=="123","old schema timestamp never replaced during basis migration");
         require(oldRead.value("SELECT time_basis IS NULL FROM gaps")=="1","new producer declaration cannot falsely relabel old timestamp");}
        // Another connection removes a middle batch while the native owner
        // remains alive: data_version must invalidate its warmed prefix cache.
        const auto holeFile=temp.filePath("hole.sqlite");pgn::mining::SourceHistory hole(holeFile);Read holeRead(holeFile);
        hole.append(a,0,"one\n");hole.append(a,4,"two\n");hole.append(a,8,"three\n");
        sql(holeRead.db,"DELETE FROM native_source_batches WHERE offset=4;");const auto holeBefore=holeRead.snapshot();
        rejected([&]{hole.append(a,14,first);},"external middle-hole invalidates warmed append proof");require(holeRead.snapshot()==holeBefore&&hole.cursor(a)==14,"middle-hole refusal no data/cursor delta");
        indexAuditAccessPath(temp.path());
        // The actual standalone native entrypoint, never a Node or GPU child.
        if(argc>1){
            const auto cliDatabase=temp.filePath("cli.sqlite"),cliJournal=temp.filePath(b+".stdout");write(cliJournal,first.left(30));
            auto invoke=[&](const QString& destination,const QString& source,const QString& run,int expected){
                QProcess child;child.setProgram(QString::fromLocal8Bit(argv[1]));child.setArguments({"--database",destination,"--journal",source,"--run",run});child.start();
                require(child.waitForStarted(5000)&&child.waitForFinished(10000),"native CLI child finishes");
                if(child.exitCode()!=expected)std::cerr<<child.readAllStandardError().constData();
                require(child.exitStatus()==QProcess::NormalExit&&child.exitCode()==expected,"native CLI exit exact");
                return QJsonDocument::fromJson(child.readAllStandardOutput()).object();
            };
            require(invoke(cliDatabase,cliJournal,b,0).value("offset")=="0","native CLI incomplete EOF");
            write(cliJournal,first.mid(30),QIODevice::WriteOnly|QIODevice::Append);
            require(invoke(cliDatabase,cliJournal,b,0).value("inserted")=="1","native CLI real import");
            require(invoke(cliDatabase,cliJournal,b,0).value("lines")=="0","native CLI reopen EOF");
            Read cliRead(cliDatabase);const auto cliBefore=cliRead.snapshot();
            write(cliJournal,gap("41","43","6"),QIODevice::WriteOnly|QIODevice::Append);
            invoke(cliDatabase,cliJournal,b,1);require(cliRead.snapshot()==cliBefore,"native CLI bad batch retains logical state");
            const auto absent=temp.filePath("must-not-create.sqlite"),badJournal=temp.filePath("bad.stdout");write(badJournal,first);
            invoke(absent,badJournal,"bad",2);require(!QFileInfo::exists(absent),"invalid CLI identity no database created");
            QFile source(cliJournal);require(source.open(QIODevice::ReadOnly)&&source.readAll()==first+gap("41","43","6"),"native CLI never mutates journal");
        }
        std::cout<<"SOURCE_HISTORY "<<checks<<" checks / 0 failures; real SQLite, transaction rollback, all byte-split close/reopen, exact64 cursors; closed synthetic source observations only\n";return 0;
    }catch(const std::exception& error){std::cerr<<"SOURCE_HISTORY failed after "<<checks<<": "<<error.what()<<'\n';return 1;}
}
