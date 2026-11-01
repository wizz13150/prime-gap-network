#include "mining/engine_module_registry.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <iostream>

using namespace pgn::mining;
namespace {
int checks{};
void check(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);}
template<class F>void refuses(F fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}check(rejected,"invalid declaration/intent must refuse, never execute/admit");}
QJsonObject base(){return {{"schema","pgn-engine-module-1"},{"id","native-fixture"},{"protocol","phase0-source-1"},{"host_refill",false},{"host_verification",false},{"whole_job_gpu_owned",true},{"single_low64_window",true},{"supported_high_word","10"}};}
QJsonObject intent(const QString& key,const QString& id="native-request"){return {{"request_id",id},{"module_key",key},{"device_id","opaque-device"},{"owner","explicit-user"},{"expected_run","original-run"},{"boundary","NEXT_JOB_ONLY"},{"job",QJsonObject{{"start","184467440737095516160"},{"length","30"}}},{"unknown",QJsonArray{0,false,QJsonValue::Null}}};}
bool equal(const QJsonValue& a,const QJsonValue& b){return canonicalEngineModuleJson(a)==canonicalEngineModuleJson(b);}
struct Sql {
    sqlite3* db{};explicit Sql(const QString& path){check(sqlite3_open(path.toUtf8().constData(),&db)==SQLITE_OK,"own fixture SQLite open");}
    ~Sql(){if(db)sqlite3_close(db);}
    void exec(const char* sql){check(sqlite3_exec(db,sql,nullptr,nullptr,nullptr)==SQLITE_OK,"fixture SQL transition");}
    QString text(const char* sql){sqlite3_stmt* s{};check(sqlite3_prepare_v2(db,sql,-1,&s,nullptr)==SQLITE_OK&&sqlite3_step(s)==SQLITE_ROW,"fixture scalar query");const auto out=QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(s,0)),sqlite3_column_bytes(s,0));sqlite3_finalize(s);return out;}
};
void pure(){
    auto c=base();c.insert("extension",QJsonObject{{"zero",0},{"unknown",QJsonValue::Null},{"literal","$(never-execute)"}});check(equal(validateEngineModuleCapabilities(c),c),"unknown JSON extensions preserved");
    const auto o=observeEngineModule(c,{{"observed_utc","original-age"},{"artifact_sha256",QJsonValue::Null},{"source","fixture"},{"unknown",QJsonObject{{"a",0}}}});
    check(o.value("evidence_scope")=="PRODUCER_DECLARATION_ONLY"&&o.value("provenance").toObject().value("observed_utc")=="original-age","provenance preserved, no fabricated freshness");
    auto i=intent(QString(64,'a'));i.insert("start","184467440737095516160");i.insert("length","30");const auto request=createEngineModuleRequest(o,i);
    check(request.value("state")=="REQUESTED"&&!request.contains("applied")&&!request.contains("command"),"pure request is not controller admission");
    check(observeEngineModule(c).value("provenance").toObject().isEmpty(),"unknown observation remains absent");
    for(const auto* key:{"host_refill","host_verification","whole_job_gpu_owned"}){auto bad=c;bad.remove(key);refuses([&]{validateEngineModuleCapabilities(bad);});bad=c;bad[key]=0;refuses([&]{validateEngineModuleCapabilities(bad);});}
    for(const auto& value:QJsonArray{QJsonValue(9007199254740992LL),QJsonValue("01"),QJsonValue("18446744073709551616"),QJsonValue(-1),QJsonValue(true)}){auto bad=c;bad["supported_high_word"]=value;refuses([&]{validateEngineModuleCapabilities(bad);});}
    for(const auto& patch:QList<QJsonObject>{{{"start","202914184810805067776"}},{{"length","0"}},{{"start","202914184810805067775"},{"length","1"}},{{"length","01"}},{{"length","18446744073709551616"}},{{"start","340282366920938463463374607431768211456"}}}){auto bad=i;for(auto it=patch.begin();it!=patch.end();++it)bad.insert(it.key(),it.value());refuses([&]{createEngineModuleRequest(o,bad);});}
    auto generic=c;generic.remove("supported_high_word");generic.remove("single_low64_window");auto bad=i;bad["start"]="340282366920938463463374607431768211455";bad["length"]="1";refuses([&]{createEngineModuleRequest(observeEngineModule(generic),bad);});
    auto scout=base();scout.remove("supported_high_word");scout["cuda_arch"]=QJsonArray{"sm86","sm120"};scout["bits_min"]=67;scout["bits_max"]=95;scout["supported_high_word_range"]=QJsonArray{4,2147483647};scout["host_verification"]=true;scout["whole_job_gpu_owned"]=false;
    check(equal(validateEngineModuleCapabilities(scout),scout),"Scout architecture list and exact numeric domain retained");
    auto wide=i;wide["start"]="20000000000000000000000000000";wide["length"]="2000000000000";check(createEngineModuleRequest(observeEngineModule(scout),wide).value("job").toObject().value("start")==wide.value("start"),"95bit start remains exact decimal string");
    for(const auto& start:QStringList{"73786976294838206463","39614081257132168796771975168"}){auto bad=wide;bad["start"]=start;refuses([&]{createEngineModuleRequest(observeEngineModule(scout),bad);});}
    for(const auto& patch:QList<QJsonObject>{{{"cuda_arch",QJsonArray{}}},{{"cuda_arch",QJsonArray{"sm86",false}}},{{"supported_high_word_range",QJsonArray{10,4}}},{{"bits_min",96},{"bits_max",95}}}){auto bad=scout;for(auto it=patch.begin();it!=patch.end();++it)bad[it.key()]=it.value();refuses([&]{validateEngineModuleCapabilities(bad);});}
    auto future=c;future["protocol"]="future-protocol";check(observeEngineModule(future).value("capabilities").toObject().value("protocol")=="future-protocol","future descriptor remains inspectable");refuses([&]{createEngineModuleRequest(observeEngineModule(future),i);});
    check(canonicalEngineModuleJson(-0.0)=="0"&&canonicalEngineModuleJson(1e-7)=="1e-7"&&canonicalEngineModuleJson(1e-6)=="0.000001"&&canonicalEngineModuleJson(1e21)=="1e+21","JS number thresholds, not Qt JSON formatting");
    check(canonicalEngineModuleJson(QString(QChar(0xd800)))=="\"\\ud800\""&&canonicalEngineModuleJson(QString(QChar(0xdc00)))=="\"\\udc00\"","JSON.stringify preserves lone surrogate escapes");
    auto whitespace=c;whitespace["id"]=QString(QChar(0xfeff));refuses([&]{validateEngineModuleCapabilities(whitespace);});whitespace["id"]=QString(QChar(0x85));check(validateEngineModuleCapabilities(whitespace).value("id")==whitespace.value("id"),"JS trim differs from Qt unicode whitespace");
}
void nativeStore(){
    QTemporaryDir temp;check(temp.isValid(),"own fresh temp only");const auto path=temp.filePath("engine-modules.sqlite");EngineModuleRegistry registry(path);
    const auto empty=registry.page();check(empty.value("items").toArray().isEmpty()&&empty.value("next_cursor").isNull()&&!QFile::exists(path),"absent page read creates neither file nor synthetic records");
    const auto reg=registry.registerModule(base(),"original source");check(reg.value("created")==true&&reg.value("artifact_sha256").isNull()&&reg.value("gpu_control_calls")==0,"declaration only, digest unknown not invented");const auto key=reg.value("module_key").toString();const auto row=registry.descriptor(key);
    Sql db(path);db.exec("CREATE TRIGGER forbid_module_update BEFORE UPDATE ON modules BEGIN SELECT RAISE(ABORT,'NO UPDATE');END;CREATE TRIGGER forbid_request_update BEFORE UPDATE ON module_requests BEGIN SELECT RAISE(ABORT,'NO UPDATE');END;");
    check(registry.registerModule(base(),"MUST NOT REPLACE").value("created")==false&&equal(row,registry.descriptor(key)),"same declaration replay never rewrites source/age");
    const auto wanted=intent(key);const auto receipt=registry.request(wanted);check(receipt.value("state")=="REQUESTED_NOT_ADMITTED"&&receipt.value("applied")==false&&receipt.value("reservation_changes")==0,"selection intent not applied/reserved");
    check(equal(registry.request(wanted),receipt),"exact request replay no UPDATE/originaldate preserved");auto conflict=wanted;conflict["owner"]="another";refuses([&]{registry.request(conflict);});check(equal(registry.receipt("native-request"),receipt),"request conflict cannot overwrite original");
    registry.close();EngineModuleRegistry reopened(path);check(equal(reopened.receipt("native-request"),receipt)&&equal(reopened.descriptor(key),row),"native close/reopen preserves exact old row/date");
    db.exec("CREATE TRIGGER fail_request AFTER INSERT ON module_requests BEGIN SELECT RAISE(ABORT,'AFTERFAIL');END;");auto after=intent(key,"afterfail");refuses([&]{reopened.request(after);});check(db.text("SELECT count(*) FROM module_requests")=="1","AFTERFAIL statement rollback, no partial receipt");db.exec("DROP TRIGGER fail_request");
    db.exec("CREATE TRIGGER fail_module AFTER INSERT ON modules BEGIN SELECT RAISE(ABORT,'AFTERFAIL');END;");auto extra=base();extra["id"]="afterfail";refuses([&]{reopened.registerModule(extra);});check(db.text("SELECT count(*) FROM modules")=="1","registration AFTERFAIL no partial declaration");db.exec("DROP TRIGGER fail_module");
    auto command=handleModuleRegistryCommand(reopened,{{"action","module_receipt"},{"request_id","correlation"},{"selection_id","native-request"}});check(command.value("request_id")=="correlation"&&equal(command.value("selection"),receipt)&&command.value("applied")==false,"handler preserves correlation not effect");
    refuses([&]{handleModuleRegistryCommand(reopened,{{"action","start"},{"request_id","not-an-executor"}});});
    for(int n=0;n<145;++n){auto c=base();c["id"]="pages-"+QString::number(n);if(n==4)c["protocol"]="future";reopened.registerModule(c);}
    QString cursor;QSet<QString> all;do{const auto page=reopened.page(cursor,7);const auto rows=page.value("items").toArray();check(rows.size()<=7,"page bounded, not data capped");for(const auto& row:rows)all.insert(row.toObject().value("module_key").toString());cursor=page.value("next_cursor").toString();}while(!cursor.isEmpty());check(all.size()==146,"all declarations accessible beyond128 without history cap");
    refuses([&]{reopened.page({},0);});refuses([&]{reopened.page({},129);});refuses([&]{reopened.page("not-hash");});
    auto bad=base();bad["extension"]=QString(40000,'x');refuses([&]{reopened.registerModule(bad);});
    auto package=QJsonObject{{"schema","pgn-engine-package-1"},{"capabilities",base()},{"artifact_sha256",QString(64,'A')},{"artifact_ref","C:/opaque/$(never-execute).exe"}};
    const auto located=reopened.registerModule(package);check(located.value("module_key")!=key&&located.value("artifact_sha256")==QString(64,'a'),"artifact ref changes identity, only digest case normalized");package["artifact_ref"]=QString("bad")+QChar(0);refuses([&]{reopened.registerModule(package);});
    Sql lock(path);lock.exec("BEGIN IMMEDIATE");auto blocked=base();blocked["id"]="busy";refuses([&]{reopened.registerModule(blocked);});lock.exec("ROLLBACK");check(db.text("SELECT count(*) FROM modules WHERE module_id='busy'")=="0","busy write no side effect");
}
void legacy(){
    QTemporaryDir temp;const auto path=temp.filePath("legacy.sqlite");Sql db(path);db.exec("CREATE TABLE modules(module_key TEXT PRIMARY KEY,module_id TEXT NOT NULL,descriptor_sha256 TEXT NOT NULL,artifact_sha256 TEXT,capabilities TEXT NOT NULL,source_ref TEXT NOT NULL,added_utc TEXT NOT NULL)");
    sqlite3_stmt* insert{};check(sqlite3_prepare_v2(db.db,"INSERT INTO modules VALUES(?,?,?,?,?,?,?)",-1,&insert,nullptr)==SQLITE_OK,"legacy seed statement");const auto c=canonicalEngineModuleJson(base());const QList<QByteArray> values{QByteArray(64,'f'),"legacy",QByteArray(64,'e'),{},c,"original-source","original-age"};for(int i=0;i<values.size();++i)if(i==3)sqlite3_bind_null(insert,i+1);else sqlite3_bind_text(insert,i+1,values[i].constData(),values[i].size(),SQLITE_TRANSIENT);check(sqlite3_step(insert)==SQLITE_DONE,"legacy public row");sqlite3_finalize(insert);
    EngineModuleRegistry r(path);const auto row=r.descriptor(QString(64,'f'));check(row.value("artifact_ref").isNull()&&row.value("added_utc")=="original-age"&&row.value("module_key")==QString(64,'f'),"legacy migration preserves arbitrary old key/age, does not rehash history");check(r.page().value("items").toArray().size()==1,"legacy remains inspectable");
}
QJsonObject read(const QString& path){QFile file(path);check(file.open(QIODevice::ReadOnly)&&file.size()<=1048576,"bounded own golden JSON");QJsonParseError error;auto doc=QJsonDocument::fromJson(file.readAll(),&error);check(error.error==QJsonParseError::NoError&&doc.isObject(),"valid own golden JSON");return doc.object();}
void deepLegacy(const QString& directory){
    const auto expected=read(directory+"/deep-golden.json");check(expected.value("schema")=="pgn-engine-module-depth-test-1"&&expected.value("fixture_test_only")==true&&expected.value("depth")==1025,"actual Node TESTONLY1025 metadata golden");
    const auto key=expected.value("registration").toObject().value("module_key").toString();EngineModuleRegistry registry(directory+"/node.sqlite");const auto row=registry.descriptor(key);
    check(row.value("capabilities").isNull()&&row.value("protocol").isNull()&&row.value("descriptor_unavailable")=="NATIVE_QT_JSON_UNAVAILABLE_NOT_A_DECLARATION","deep capability unavailable, never invented protocol/declaration");
    check(row.value("capabilities_json")==expected.value("canonical"),"exact raw canonical legacy JSON remains inspectable as opaque text");
    auto metadata=row;for(const auto* name:{"capabilities","protocol","descriptor_unavailable","capabilities_json"})metadata.remove(name);check(equal(metadata,expected.value("metadata")),"all original key/hash/source/artifact/date metadata preserved");
    const auto page=registry.page({},128);int known=0,unknown=0;for(const auto& item:page.value("items").toArray()){
        const auto o=item.toObject();if(o.value("module_key")==key){++unknown;check(o.value("protocol").isNull()&&!o.contains("capabilities_json")&&o.value("source_ref")=="original deep source"&&o.value("added_utc")==metadata.value("added_utc"),"deep page row metadata retained, no fullraw duplication");}
        else{++known;check(o.value("protocol").isString()&&!o.contains("descriptor_unavailable"),"other catalogue rows remain inspectable");}
    }check(known==68&&unknown==1,"one deep descriptor neither hides others nor disappears");
    const auto original=registry.request(expected.value("intent").toObject());check(equal(original,expected.value("receipt")),"deep old Node request replay no DOM required, original date retained");
    Sql db(directory+"/node.sqlite");db.exec("CREATE TRIGGER deep_no_update BEFORE UPDATE ON modules BEGIN SELECT RAISE(ABORT,'NO UPDATE');END;CREATE TRIGGER deep_request_no_update BEFORE UPDATE ON module_requests BEGIN SELECT RAISE(ABORT,'NO UPDATE');END;");
    auto intent=expected.value("intent").toObject();intent["request_id"]="deep-native-request";const auto receipt=registry.request(intent);
    check(receipt.value("state")=="REQUESTED_NOT_ADMITTED"&&receipt.value("applied")==false&&receipt.value("gpu_control_calls")==0&&receipt.value("reservation_changes")==0,"new deep metadata intent exists, NOT engine admission");check(equal(registry.request(intent),receipt),"deep native exact replay no UPDATE");
    intent["request_id"]="deep-job-must-hold";intent["job"]=QJsonObject{{"start","184467440737095516160"},{"length","30"}};refuses([&]{registry.request(intent);});check(db.text("SELECT count(*) FROM module_requests WHERE request_id='deep-job-must-hold'")=="0","compatibility-required job refused BEFORE intent insertion");
    check(registry.descriptor(key).value("capabilities_json")==expected.value("canonical"),"no legacy descriptor rewrite/conversion");
    QFile out(directory+"/deep-native-receipt.json");check(out.open(QIODevice::WriteOnly|QIODevice::NewOnly),"new deep receipt fixture, never overwrite");const auto bytes=canonicalEngineModuleJson(receipt);check(out.write(bytes)==bytes.size()&&out.flush(),"native deep metadata receipt preserved for actual Node reopen");registry.close();
}
void cross(const QString& directory){
    const auto golden=read(directory+"/goldens.json");check(golden.value("schema")=="pgn-engine-module-native-crosslang-test-1"&&golden.value("fixture_test_only")==true,"TESTONLY fixture identity");
    for(const auto& item:golden.value("direct").toArray()){const auto o=item.toObject();auto doc=QJsonDocument::fromJson('['+o.value("raw").toString().toUtf8()+']');check(canonicalEngineModuleJson(doc.array().first())==o.value("canonical").toString().toUtf8(),"actual Node direct number stringify golden");}
    check(equal(observeEngineModule(golden.value("observation").toObject().value("capabilities"),golden.value("observation").toObject().value("provenance").toObject()),golden.value("observation")),"actual Node pure observation golden");
    const auto expected=golden.value("pureRequest").toObject();const auto job=expected.value("job").toObject();check(equal(createEngineModuleRequest(golden.value("observation").toObject(),{{"request_id",expected.value("request_id")},{"device_id",expected.value("device_id")},{"owner",expected.value("owner")},{"start",job.value("start")},{"length",job.value("length")}}),expected),"actual Node pure request golden");
    check(!QFile::exists(directory+"/native.sqlite")&&!QFile::exists(directory+"/native-results.json"),"never overwrite/reinitialize native cross fixture");EngineModuleRegistry node(directory+"/node.sqlite"),native(directory+"/native.sqlite");QJsonArray observed;
    for(const auto& item:golden.value("cases").toArray()){
        const auto c=item.toObject();const auto key=c.value("registration").toObject().value("module_key").toString();check(equal(node.descriptor(key),c.value("row")),"native reads actual Node SQLite, source/age/extensions exact");check(equal(node.receipt(c.value("intent").toObject().value("request_id").toString()),c.value("receipt")),"native reads actual Node receipt/date");
        auto duplicate=c.value("registration").toObject();duplicate["created"]=false;check(equal(node.registerModule(c.value("input"),"DO NOT OVERWRITE"),duplicate),"native recomputes actual Node hash/key, duplicate no rewrite");check(equal(node.request(c.value("intent").toObject()),c.value("receipt")),"native exact replay of Node request immutable");
        const auto added=native.registerModule(c.value("input"),c.value("source").toString());check(equal(added,c.value("registration")),"native->Node registration identity golden");const auto row=native.descriptor(key),receipt=native.request(c.value("intent").toObject());check(canonicalEngineModuleJson(row.value("capabilities"))==c.value("canonical").toString().toUtf8(),"actual old registry canonical descriptor bytes equal native");
        auto comparable=row,prior=c.value("row").toObject();comparable.remove("added_utc");prior.remove("added_utc");check(equal(comparable,prior),"same SQL columns/text conversion including loneUTF16 ref");observed.append(QJsonObject{{"row",row},{"receipt",receipt}});
    }
    node.close();native.close();QFile file(directory+"/native-results.json");check(file.open(QIODevice::WriteOnly|QIODevice::NewOnly),"new own cross result only");const auto bytes=canonicalEngineModuleJson(QJsonObject{{"fixture_test_only",true},{"cases",observed}});check(file.write(bytes)==bytes.size()&&file.flush(),"native cross results exact UTF8");
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{if(app.arguments().size()==3&&app.arguments()[1]=="--deep")deepLegacy(app.arguments()[2]);else{pure();nativeStore();legacy();if(app.arguments().size()==3&&app.arguments()[1]=="--cross")cross(app.arguments()[2]);else check(app.arguments().size()==1,"only explicit own --cross/--deep fixture options");}std::cout<<"NATIVE_ENGINE_MODULE_REGISTRY "<<checks<<" checks / 0 failures; declaration/request only, own SQLite, no GPU/SSH/UI/engine\n";return 0;}catch(const std::exception& error){std::cerr<<"NATIVE_ENGINE_MODULE_REGISTRY "<<checks<<" failure="<<error.what()<<'\n';return 1;}}
