#include "mining/source_history.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <iostream>
#include <stdexcept>

namespace {
int checks=0;void require(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);}
template<class F> void rejected(F action,const char* what){bool failed=false;try{action();}catch(const std::exception&){failed=true;}require(failed,what);}
const QString a(32,'a'),b(32,'b');
QByteArray gap(const char* p,const char* q,const char* n,const char* time=""){
    return QByteArray("[phase0-gpu] GAP merit=1.9000 gap=")+n+" lower="+p+" upper="+q+" verified=1 proof=GPU_MR12_EULER_COMPOSITE record=off table=unknown"+time+'\n';
}
QJsonObject bound(const QJsonObject& p,const char* scope,const char* axis){return p.value(scope).toObject().value(axis).toObject();}
void mutate(const QString& file,const char* sql){sqlite3* db{};if(sqlite3_open(file.toUtf8().constData(),&db)!=SQLITE_OK)throw std::runtime_error("fixture database open");const int rc=sqlite3_exec(db,sql,nullptr,nullptr,nullptr);sqlite3_close(db);if(rc!=SQLITE_OK)throw std::runtime_error("fixture mutation failed");}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    try{
        QTemporaryDir temp;require(temp.isValid(),"temporary storage");const auto database=temp.filePath("science.sqlite");pgn::mining::SourceHistory history(database);
        auto p=history.indexPublication(a);
        require(p.value("type")=="native_index"&&p.value("complete")==true,"existing viewer protocol complete index snapshot");
        require(bound(p,"all","length").value("points").toInteger(-1)==0&&bound(p,"all","time").value("points").toInteger(-1)==0,"empty archive independently counted, not guessed");
        require(p.value("current").toObject().value("scope")==a&&bound(p,"current","length").value("points").toInteger(-1)==0,"explicit empty current scope");
        const auto first=gap("23","29","6"," unix_ts=1791982800"),second=gap("31","37","6");
        history.append(a,0,first+second);p=history.indexPublication(a);
        require(bound(p,"all","length").value("points").toInteger()==2&&bound(p,"all","time").value("points").toInteger()==1,"all exact count with genuine dated subset");
        require(bound(p,"current","length").value("points").toInteger()==2&&bound(p,"current","time").value("points").toInteger()==1,"current exact count in same committed snapshot");
        require(bound(p,"all","time").value("min_x").toDouble()==1791982800000.0,"original real UTC coordinate, no retiming");
        require(bound(history.indexPublication(b),"current","length").value("points").toInteger(-1)==0,"empty different run does not inherit all history");
        const auto secondTimed=gap("31","37","6"," unix_ts=1791982801");history.append(b,0,first+secondTimed);p=history.indexPublication(a);
        require(bound(p,"all","length").value("points").toInteger()==2&&bound(p,"all","time").value("points").toInteger()==2,"duplicate gaps do not duplicate all points; genuine time repairs all origins");
        require(bound(p,"current","time").value("points").toInteger()==2&&bound(history.indexPublication(b),"current","time").value("points").toInteger()==2,"new genuine time included for both real origins");
        // Another connection mutates only metadata: the same-connection
        // data_version must invalidate a previously audited base.
        mutate(database,"UPDATE native_plot_bounds SET points=1 WHERE scope='' AND axis='length';");
        rejected([&]{history.indexPublication(a);},"external metadata damage not published as smaller complete history");
        mutate(database,"UPDATE native_plot_bounds SET points=2 WHERE scope='' AND axis='length';");
        require(bound(history.indexPublication(a),"all","length").value("points").toInteger()==2,"repaired external base reaudited rather than latched forever");
        mutate(database,"DELETE FROM native_plot_coordinates WHERE scope='' AND axis='length' AND gap_id=1;");
        rejected([&]{history.indexPublication(a);},"missing actual coordinate held even if metadata count unchanged");
        mutate(database,"INSERT INTO native_plot_coordinates SELECT '',axis,point_key,gap_id,x,y,admission_revision FROM native_plot_coordinates WHERE scope='aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' AND axis='length' AND gap_id=1;");
        require(bound(history.indexPublication(a),"all","length").value("points").toInteger()==2,"coordinate repair reaudited");
        mutate(database,"UPDATE native_plot_bounds SET revision=9007199254740993 WHERE scope='' AND axis='length';");
        p=history.indexPublication(a);const auto encoded=QJsonDocument(p).toJson(QJsonDocument::Compact);
        require(bound(p,"all","length").value("revision").toInteger()==9007199254740993LL&&encoded.contains("9007199254740993"),"stored revision above2^53 published exactly, never through double");
        mutate(database,"UPDATE native_plot_bounds SET min_x=100,max_x=1 WHERE scope='' AND axis='length';");
        rejected([&]{history.indexPublication(a);},"inverted viewport bounds not published");
        mutate(database,"UPDATE native_plot_bounds SET min_x=6,max_x=6 WHERE scope='' AND axis='length';");
        require(history.indexPublication(a).value("proof_status")=="SOURCE_PRP_INDEPENDENT_PROOF_PENDING","index completeness never becomes independent proof");
        rejected([&]{history.indexPublication("not-a-run");},"invalid selected scope rejected");
        std::cout<<"Native index publication: "<<checks<<" checks, 0 failures; real SQLite, coherent scope/time snapshot\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Native index publication failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}
