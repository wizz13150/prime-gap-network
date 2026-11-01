#include "science/mining_results.h"
#include "storage/sqlite_read.h"
#include "mining/native_scientific_context.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
namespace pgn::science {
namespace {
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
MiningResult resultRow(storage::ReadStatement& q){
    MiningResult r;r.id=sqlite3_column_int64(q.value,0);bool ok=false;r.gap=q.text(1).toUInt(&ok);need(ok&&r.gap>0,"Invalid result gap");
    r.merit=q.text(2);const auto merit=r.merit.toDouble(&ok);need(ok&&std::isfinite(merit)&&merit>=0,"Invalid result merit");
    r.lower=q.text(3);r.upper=q.text(4);need(booking::canonicalUnsigned(r.lower)==r.lower&&booking::canonicalUnsigned(r.upper)==r.upper&&booking::subtractUnsigned(r.upper,r.lower)==QString::number(r.gap),"Result endpoints differ from gap");return r;
}
}
QStringList ticketSourceRuns(const QString& file,const QStringList& requests,const QString& genesis){
    QStringList runs;if(requests.isEmpty())return runs;storage::ReadOnlyDatabase db(file);
    storage::ReadStatement query(db.db,"SELECT value FROM booking_metadata WHERE key>=? AND key<?");query.text(1,"mining-source-manifest/v1/");query.text(2,"mining-source-manifest/v10");
    while(query.step()==SQLITE_ROW){
        need(sqlite3_column_bytes(query.value,0)<=16384,"Saved source manifest exceeds bound");
        const auto manifest=QJsonDocument::fromJson(query.text(0).toUtf8()).object();
        if(manifest.value("source_kind")!="NATIVE_PGN_SCIENCE")continue;
        if(!mining::nativeScientificSearchOrigin(manifest))continue;
        const auto work=manifest.value("native_scientific_context").toObject().value("rpc_work").toObject();
        if(work.value("genesis").toString()==genesis&&requests.contains(work.value("request_id").toString()))runs.append(manifest.value("run").toString());
    }
    runs.removeDuplicates();return runs;
}
MiningResults readMiningResults(const QString& file,const QStringList& inputRuns,bool whole,int limit,std::optional<double> target,const std::atomic_bool* cancelled){
    MiningResults out;
    try{
        need(limit>=10&&limit<=30&&(!target||(std::isfinite(*target)&&*target>0)),"Invalid result view parameters");
        const auto stopped=[&]{return cancelled&&cancelled->load();};if(stopped()){out.cancelled=true;return out;}
        storage::ReadOnlyDatabase db(file);sqlite3_progress_handler(db.db,2048,[](void* p){return p&&static_cast<const std::atomic_bool*>(p)->load()?1:0;},const_cast<std::atomic_bool*>(cancelled));db.execute("BEGIN");
        storage::ReadStatement head(db.db,"SELECT COALESCE(MAX(id),0) FROM gaps");head.step();out.revision=sqlite3_column_int64(head.value,0);
        const auto count=[&](quint32 gap,double merit,qint64 n){need(n>0&&std::isfinite(merit)&&merit>=0&&merit<=INT32_MAX&&out.points<=INT64_MAX-n,"Invalid result multiplicity");out.points+=n;out.gapSizes[gap]+=n;if(target&&merit>=*target)out.atOrAboveTarget+=n;else out.meritBands[int(std::floor(merit))]+=n;};
        const auto retain=[&](const MiningResult& row){
            out.best.append(row);std::sort(out.best.begin(),out.best.end(),[](const auto& a,const auto& b){return a.gap!=b.gap?a.gap>b.gap:a.id>b.id;});if(out.best.size()>limit)out.best.removeLast();
            out.recent.append(row);std::sort(out.recent.begin(),out.recent.end(),[](const auto& a,const auto& b){return a.id>b.id;});if(out.recent.size()>limit)out.recent.removeLast();
        };
        if(!whole){
            db.execute("CREATE TEMP TABLE selected_runs(run TEXT PRIMARY KEY) WITHOUT ROWID");
            storage::ReadStatement insert(db.db,"INSERT OR IGNORE INTO selected_runs VALUES(?)");static const QRegularExpression run("^[a-f0-9]{32}$");
            for(const auto& id:inputRuns){need(run.match(id).hasMatch(),"Invalid selected source run");insert.text(1,id);insert.step();sqlite3_reset(insert.value);}
            storage::ReadStatement q(db.db,"SELECT DISTINCT g.id,g.gap,g.merit,g.p,g.q FROM selected_runs r CROSS JOIN origins o INDEXED BY native_origins_run_gap ON o.run=r.run JOIN gaps g ON g.id=o.gap_id");
            while(q.step()==SQLITE_ROW){const auto row=resultRow(q);++out.rowsRead;count(row.gap,row.merit.toDouble(),1);retain(row);}
        }else{
            // Exact weighted prefix plus the raw admitted suffix, as in the
            // graph renderer. No 31-million-row scan or rich-cache sampling.
            qint64 base=-1,prefixPoints=0,prefixCells=0;
            {storage::ReadStatement m(db.db,"SELECT base_revision,points,cells FROM native_plot_multiplicity_manifest WHERE scope='' AND axis='length'");if(m.step()==SQLITE_ROW){base=sqlite3_column_int64(m.value,0);prefixPoints=sqlite3_column_int64(m.value,1);prefixCells=sqlite3_column_int64(m.value,2);}}
            qint64 revision=0,expected=0;{storage::ReadStatement b(db.db,"SELECT revision,points FROM native_plot_bounds WHERE scope='' AND axis='length'");if(b.step()==SQLITE_ROW){revision=sqlite3_column_int64(b.value,0);expected=sqlite3_column_int64(b.value,1);}}
            need(base<=revision&&prefixPoints>=0&&prefixCells>=0,"Result cache prefix invalid");
            if(base>=0){storage::ReadStatement c(db.db,"SELECT x,y,multiplicity FROM native_plot_multiplicity WHERE scope='' AND axis='length'");qint64 cells=0;
                while(c.step()==SQLITE_ROW){const auto gap=sqlite3_column_double(c.value,0);need(gap>=1&&gap<=UINT32_MAX&&std::floor(gap)==gap,"Invalid cached gap size");count(quint32(gap),sqlite3_column_double(c.value,1),sqlite3_column_int64(c.value,2));++cells;++out.rowsRead;}
                need(cells==prefixCells&&out.points==prefixPoints,"Result cache cardinality differs");}
            storage::ReadStatement suffix(db.db,"SELECT x,y FROM native_plot_coordinates INDEXED BY native_plot_revision WHERE scope='' AND axis='length' AND admission_revision>? AND admission_revision<=?");suffix.integer(1,base);suffix.integer(2,revision);
            while(suffix.step()==SQLITE_ROW){const auto gap=sqlite3_column_double(suffix.value,0);need(gap>=1&&gap<=UINT32_MAX&&std::floor(gap)==gap,"Invalid result gap coordinate");count(quint32(gap),sqlite3_column_double(suffix.value,1),1);++out.rowsRead;}
            need(out.points==expected,"Whole result snapshot incomplete");
            storage::ReadStatement best(db.db,"SELECT g.id,g.gap,g.merit,g.p,g.q FROM native_plot_coordinates c JOIN gaps g ON g.id=c.gap_id WHERE c.scope='' AND c.axis='length' ORDER BY c.point_key DESC LIMIT ?");best.integer(1,limit);while(best.step()==SQLITE_ROW)out.best.append(resultRow(best));
            storage::ReadStatement recent(db.db,"SELECT id,gap,merit,p,q FROM gaps ORDER BY id DESC LIMIT ?");recent.integer(1,limit);while(recent.step()==SQLITE_ROW)out.recent.append(resultRow(recent));
        }
        db.execute("COMMIT");
    }catch(const std::exception& e){out.cancelled=cancelled&&cancelled->load();if(!out.cancelled)out.error=QString::fromUtf8(e.what());out.best.clear();out.recent.clear();out.points=0;out.gapSizes.clear();out.meritBands.clear();}
    return out;
}
}
