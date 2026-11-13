#pragma once
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QElapsedTimer>
#include <QSet>
#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>

namespace pgn::science {
// One ordered source stream: replacement snapshots never add the same prefix
// twice. Only aggregates and 100 Best/Recent pairs survive a ticket transition.
class MiningScopeArchive {
    static constexpr qint64 maximumBytes=2*1024*1024;
    QString file_,error_,stream_,ticket_;qint64 sequence_{};QString prefix_;quint64 activeNs_{},clockNs_{};QString clockEpoch_;qint64 observedGolds_{},previousGolds_{};bool goldBaseline_{};
    QJsonObject base_,current_;std::unique_ptr<QLockFile> lock_;QElapsedTimer saved_;
    bool dirty_{},blocked_{},includeAudits_{};QSet<QString> retiredStreams_;
    static qint64 count(const QJsonValue& value){
        bool ok=false;const auto n=value.toVariant().toLongLong(&ok);
        if(!ok||n<0)throw std::runtime_error("Invalid archive counter");return n;
    }
    static QString addDecimal(QString a,QString b){
        if(a.isEmpty())a="0";if(b.isEmpty())b="0";if(a.size()>40||b.size()>40)throw std::runtime_error("Archive decimal counter exceeds domain");
        QString out;int carry=0;for(int i=a.size()-1,j=b.size()-1;i>=0||j>=0||carry;--i,--j){int n=carry;if(i>=0){if(!a[i].isDigit())throw std::runtime_error("Invalid decimal counter");n+=a[i].digitValue();}if(j>=0){if(!b[j].isDigit())throw std::runtime_error("Invalid decimal counter");n+=b[j].digitValue();}out.prepend(QChar('0'+n%10));carry=n/10;}return out;
    }
    static QString subtractDecimal(const QString& a,const QString& b){
        if(lessUnsigned(a,b))throw std::runtime_error("Archive decimal counter moved backwards");QString out;int borrow=0;
        for(int i=a.size()-1,j=b.size()-1;i>=0;--i,--j){int n=a[i].digitValue()-borrow-(j>=0?b[j].digitValue():0);borrow=n<0;if(borrow)n+=10;out.prepend(QChar('0'+n));}while(out.size()>1&&out[0]=='0')out.remove(0,1);return out;
    }
    static qint64 sum(qint64 a,qint64 b){if(b>std::numeric_limits<qint64>::max()-a)throw std::runtime_error("Archive counter capacity reached");return a+b;}
    static bool lessUnsigned(const QString& a,const QString& b){return a.size()!=b.size()?a.size()<b.size():a<b;}
    static QJsonObject histogram(const QJsonObject& a,const QJsonObject& b,bool intersection){
        if(a.size()>128||b.size()>128)throw std::runtime_error("Histogram exceeds archive budget");
        QJsonObject out;QSet<QString> keys;for(auto i=a.begin();i!=a.end();++i)keys.insert(i.key());for(auto i=b.begin();i!=b.end();++i)keys.insert(i.key());
        for(const auto& key:keys){if(intersection&&(!a.contains(key)||!b.contains(key)))continue;out.insert(key,QString::number(sum(a.contains(key)?count(a.value(key)):0,b.contains(key)?count(b.value(key)):0)));}return out;
    }
    static QJsonArray combineRows(const QJsonArray& first,const QJsonArray& second,bool best){
        QVector<QJsonObject> rows;QSet<QString> seen;
        for(const auto& array:{first,second}){if(array.size()>100)throw std::runtime_error("Too many archive rows");for(const auto& v:array){auto r=v.toObject();const auto key=r.value("lower_prime").toString()+":"+QString::number(r.value("gap").toInt());if(!seen.contains(key)){seen.insert(key);rows.append(r);}}}
        if(best)std::sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){const auto ag=a.value("gap").toInt(),bg=b.value("gap").toInt();return ag!=bg?ag>bg:lessUnsigned(a.value("lower_prime").toString(),b.value("lower_prime").toString());});
        QJsonArray out;for(int i=0;i<std::min(100,int(rows.size()));++i)out.append(rows[i]);return out;
    }
public:
    explicit MiningScopeArchive(QString file={},bool includeAudits=false):file_(std::move(file)),includeAudits_(includeAudits){
        if(file_.isEmpty())return;
        QDir().mkpath(QFileInfo(file_).absolutePath());lock_=std::make_unique<QLockFile>(file_+".lock");
        if(!lock_->tryLock(0)){blocked_=true;error_="Local archive is already open in another window";return;}
        QFile f(file_);if(!f.exists())return;
        if(!f.open(QIODevice::ReadOnly)||f.size()>maximumBytes){blocked_=true;error_="Local archive cannot be read within its storage budget";return;}
        QJsonParseError e;const auto data=QJsonDocument::fromJson(f.readAll(),&e).object();
        if(e.error!=QJsonParseError::NoError||data.value("schema")!="PGN_MINING_SCOPE_ARCHIVE_V1"){blocked_=true;error_="Local archive format is invalid; file retained";return;}
        for(const auto& value:data.value("retired_streams").toArray())retiredStreams_.insert(value.toString());stream_=data.value("stream_id").toString();ticket_=data.value("ticket_id").toString();prefix_=data.value("prefix").toString();sequence_=data.value("sequence").toInteger();base_=data.value("base").toObject();current_=data.value("current").toObject();activeNs_=current_.value("active_time_ns").toString().toULongLong();clockNs_=data.value("clock_ns").toString().toULongLong();clockEpoch_=data.value("clock_epoch").toString();observedGolds_=current_.value("observed_golds").toVariant().toLongLong();previousGolds_=current_.value("statistics").toObject().value("at_or_above_target").toVariant().toLongLong();goldBaseline_=!current_.isEmpty();
    }
    ~MiningScopeArchive(){flush();}
    const QString& error()const{return error_;}
    void resumeFrom(const MiningScopeArchive& other){if(!other.error_.isEmpty())return;stream_=other.stream_;ticket_=other.ticket_;sequence_=other.sequence_;prefix_=other.prefix_;base_=other.base_;current_=other.current_;retiredStreams_=other.retiredStreams_;activeNs_=other.activeNs_;clockNs_=other.clockNs_;clockEpoch_=other.clockEpoch_;observedGolds_=other.observedGolds_;previousGolds_=other.previousGolds_;goldBaseline_=other.goldBaseline_;}
    static QJsonObject merge(const QJsonObject& older,const QJsonObject& newer){
        if(older.isEmpty())return newer;if(newer.isEmpty())return older;
        auto a=older.value("statistics").toObject(),b=newer.value("statistics").toObject();
        const bool same=a.value("statistics_target").isDouble()&&a.value("statistics_target")==b.value("statistics_target");
        QJsonObject stats{{"count_total",QString::number(sum(count(a.value("count_total")),count(b.value("count_total"))))},{"at_or_above_target",QString::number(sum(count(a.value("at_or_above_target")),count(b.value("at_or_above_target"))))},{"threshold_counts",histogram(a.value("threshold_counts").toObject(),b.value("threshold_counts").toObject(),true)},{"merit_band_counts",histogram(a.value("merit_band_counts").toObject(),b.value("merit_band_counts").toObject(),false)},{"statistics_target",same?a.value("statistics_target"):QJsonValue()},{"mixed_targets",!same}};
        return {{"statistics",stats},{"best",combineRows(older.value("best").toArray(),newer.value("best").toArray(),true)},{"recent",combineRows(newer.value("recent").toArray(),older.value("recent").toArray(),false)},{"ticket_count",QString::number(sum(count(older.value("ticket_count")),count(newer.value("ticket_count"))))},{"completed_tickets",QString::number(sum(older.value("completed_tickets").toVariant().toLongLong(),newer.value("completed_tickets").toVariant().toLongLong()))},{"observed_golds",QString::number(sum(older.value("observed_golds").toVariant().toLongLong(),newer.value("observed_golds").toVariant().toLongLong()))},{"observed_positions",addDecimal(older.value("observed_positions").toString(),newer.value("observed_positions").toString())},{"observed_tests",addDecimal(older.value("observed_tests").toString(),newer.value("observed_tests").toString())},{"positions",addDecimal(older.value("positions").toString(),newer.value("positions").toString())},{"tests",addDecimal(older.value("tests").toString(),newer.value("tests").toString())},{"active_time_ns",addDecimal(older.value("active_time_ns").toString(),newer.value("active_time_ns").toString())}};
    }
    bool observe(const QJsonObject& frame){
        if(blocked_)return false;
        try{
            const auto role=frame.value("work_role");
            if(role=="audit"&&!includeAudits_){flush();return false;}
            if(!role.isUndefined()&&role!="search"&&!(includeAudits_&&role=="audit"))throw std::runtime_error("Unsupported source activity; discovery archive retained");
            const auto stream=frame.value("stream_id").toString(),ticket=frame.value("ticket_id").toString(),prefix=frame.value("positions").toString();
            const auto sequence=frame.value("local_sequence").toInteger();
            if(stream.isEmpty()||ticket.size()!=64||sequence<1||prefix.isEmpty())throw std::runtime_error("Source archive identity is unavailable");
            const bool rollover=!stream_.isEmpty()&&stream_!=stream;
            if(rollover&&retiredStreams_.contains(stream))throw std::runtime_error("Retired source snapshot ignored; archive retained");
            if(!rollover&&(sequence<sequence_||(sequence==sequence_&&!ticket_.isEmpty()&&ticket_!=ticket)))throw std::runtime_error("Out-of-order source snapshot; archive retained");
            const bool sameTicket=!current_.isEmpty()&&ticket==ticket_;
            if(sameTicket&&!prefix_.isEmpty()&&lessUnsigned(prefix,prefix_))throw std::runtime_error("Source prefix moved backwards; archive retained");
            auto current=frame.value("scopes").toObject().value("ticket").toObject();if(current.isEmpty())return false;
            const auto stats=current.value("statistics").toObject();const auto total=count(stats.value("count_total")),above=count(stats.value("at_or_above_target"));qint64 bands=above;
            const auto bins=stats.value("merit_band_counts").toObject(),thresholds=stats.value("threshold_counts").toObject();
            if(bins.size()>128||thresholds.size()>128||!stats.value("statistics_target").isDouble())throw std::runtime_error("Incomplete source statistics");
            for(auto i=bins.begin();i!=bins.end();++i)bands=sum(bands,count(i.value()));if(bands!=total)throw std::runtime_error("Merit histogram does not match source count");
            for(auto i=thresholds.begin();i!=thresholds.end();++i)if(count(i.value())>total)throw std::runtime_error("Gap histogram exceeds source count");
            current.insert("ticket_count",QString("1"));
            for(const auto& key:{QString("best"),QString("recent")}){auto rows=current.value(key).toArray();if(rows.size()>100)throw std::runtime_error("Source row budget exceeded");for(int i=0;i<rows.size();++i){auto row=rows[i].toObject();row.insert("target_merit",frame.value("target_merit"));rows[i]=row;}current.insert(key,rows);}
            const auto tests=frame.value("tests").toString("0");
            current.insert("observed_positions",sameTicket?addDecimal(current_.value("observed_positions").toString(),subtractDecimal(prefix,prefix_)):QString("0"));
            current.insert("observed_tests",sameTicket?addDecimal(current_.value("observed_tests").toString(),subtractDecimal(tests,current_.value("tests").toString("0"))):QString("0"));
            auto nextBase=base_;if(!sameTicket&&!current_.isEmpty())nextBase=merge(base_,current_);
            quint64 nextActive=sameTicket?activeNs_:0;const auto oldClock=sameTicket?clockNs_:0;const auto oldEpoch=sameTicket?clockEpoch_:QString{};
            bool clockOk=false;const auto clock=frame.value("compute_time_ns").toString().toULongLong(&clockOk);const auto epoch=frame.value("compute_epoch").toString();
            if(clockOk&&!oldEpoch.isEmpty()&&epoch==oldEpoch&&clock>=oldClock){const auto delta=clock-oldClock;if(delta>std::numeric_limits<quint64>::max()-nextActive)throw std::runtime_error("Invalid active time");nextActive+=delta;}
            qint64 nextGolds=sameTicket?observedGolds_:0;
            if(sameTicket&&goldBaseline_){if(above<previousGolds_)throw std::runtime_error("Source Gold counter moved backwards");nextGolds=sum(nextGolds,above-previousGolds_);}
            current.insert("observed_golds",QString::number(nextGolds));current.insert("positions",prefix);current.insert("tests",tests);current.insert("active_time_ns",QString::number(nextActive));
            const bool fullPrefix=prefix!="0"&&frame.value("positions_total").isString()&&prefix==frame.value("positions_total").toString();
            const bool completed=(sameTicket&&current_.value("completed_tickets")=="1")||frame.value("phase")=="Report ready"||fullPrefix;
            current.insert("completed_tickets",completed?QString("1"):QString("0"));
            // Commit only after every counter and histogram has validated.
            // A source-root change is a new producer generation, not a new
            // lifetime history. Retired generations cannot be counted twice.
            const bool changed=current!=current_||ticket!=ticket_||rollover;
            if(rollover)retiredStreams_.insert(stream_);
            base_=nextBase;activeNs_=nextActive;clockNs_=clockOk?clock:oldClock;clockEpoch_=clockOk?epoch:oldEpoch;observedGolds_=nextGolds;previousGolds_=above;goldBaseline_=true;
            stream_=stream;ticket_=ticket;sequence_=sequence;prefix_=prefix;current_=current;dirty_|=changed;error_.clear();
            if(dirty_&&(!saved_.isValid()||saved_.elapsed()>=5000))flush();return true;
        }catch(const std::exception& e){error_=QString::fromUtf8(e.what());return false;}
    }
    QJsonObject currentScope()const{return current_;}
    bool retiredSource(const QString& source)const{return source!=stream_&&retiredStreams_.contains(source);}
    bool currentSource(const QJsonObject& frame)const{return !current_.isEmpty()&&ticket_==frame.value("ticket_id").toString()&&stream_==frame.value("stream_id").toString();}
    QJsonObject scope()const{try{return merge(base_,current_);}catch(...){return {};}}
    void flush(){
        if(file_.isEmpty()||!dirty_||!error_.isEmpty())return;
        QJsonArray retired;auto generations=retiredStreams_.values();std::sort(generations.begin(),generations.end());for(const auto& value:generations)retired.append(value);
        const QJsonObject state{{"retired_streams",retired},{"schema","PGN_MINING_SCOPE_ARCHIVE_V1"},{"stream_id",stream_},{"ticket_id",ticket_},{"prefix",prefix_},{"sequence",sequence_},{"clock_ns",QString::number(clockNs_)},{"clock_epoch",clockEpoch_},{"base",base_},{"current",current_}};
        const auto bytes=QJsonDocument(state).toJson(QJsonDocument::Compact);
        if(bytes.size()>maximumBytes){error_="Local archive storage budget reached; file retained";return;}
        QSaveFile out(file_);if(!out.open(QIODevice::WriteOnly)||out.write(bytes)!=bytes.size()||!out.commit()){error_="Local archive could not be saved; previous file retained";return;}dirty_=false;saved_.restart();
    }
};
}
