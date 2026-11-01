#include "science/record_reference_refresh.h"
#include "science/record_submissions.h"
#include "booking/decimal_range.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <stdexcept>

namespace pgn::science {
namespace {
const QString sourceUrl=QStringLiteral("https://raw.githubusercontent.com/primegap-list-project/prime-gap-list/master/allgaps.sql");
constexpr qsizetype DownloadLimit=64*1024*1024;
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
QList<QByteArray> fields(QByteArrayView input){
    QList<QByteArray> values;qsizetype at=0;
    const auto space=[&]{while(at<input.size()&&(input[at]==' '||input[at]=='\t'||input[at]=='\r'))++at;};
    while(at<input.size()){
        space();need(at<input.size(),"Missing record data value");QByteArray value;
        const bool blob=at+1<input.size()&&(input[at]=='x'||input[at]=='X')&&input[at+1]=='\'';
        if(blob)++at;
        if(input[at]=='\''){
            ++at;bool closed=false;
            while(at<input.size()){
                const auto c=input[at++];
                if(c=='\''){if(at<input.size()&&input[at]=='\''){value+='\'';++at;}else{closed=true;break;}}
                else value+=c;
            }
            need(closed,"Unclosed record data string");
            if(blob){static const QRegularExpression hex("^(?:[0-9a-fA-F]{2})*$");need(hex.match(QString::fromLatin1(value)).hasMatch(),"Invalid record blob");value=QByteArray::fromHex(value);}
        }else{
            const auto begin=at;while(at<input.size()&&input[at]!=',')++at;value=input.sliced(begin,at-begin).toByteArray().trimmed();
            static const QRegularExpression number("^(?:NULL|-?[0-9]+(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)$");
            need(number.match(QString::fromLatin1(value)).hasMatch(),"Non-data record token");
        }
        values.append(value);need(values.size()<=10,"Record row has too many fields");space();if(at==input.size())break;
        need(input[at++]==',',"Invalid record field separator");need(at<input.size(),"Missing final record field");
    }
    need(values.size()==10,"Record row schema changed");return values;
}
}
QJsonObject parseRecordReferenceSql(const QByteArray& bytes,const QString& utc){
    try{
        need(!bytes.isEmpty()&&bytes.size()<=DownloadLimit&&bytes.trimmed().endsWith("COMMIT;"),"Record download incomplete or oversized");
        need(QDateTime::fromString(utc,Qt::ISODateWithMs).isValid(),"Record reference time invalid");
        QHash<quint32,QString> starts;QSet<quint32> beyond;qsizetype at=0;
        while(at<bytes.size()){
            auto end=bytes.indexOf('\n',at);if(end<0)end=bytes.size();need(end-at<=1024*1024,"Record data line oversized");
            const auto line=QByteArrayView(bytes).sliced(at,end-at);at=end+1;
            if(!line.startsWith("INSERT INTO gaps "))continue;
            constexpr QByteArrayView prefix("INSERT INTO gaps VALUES(");
            auto data=line;if(data.endsWith('\r'))data=data.sliced(0,data.size()-1);
            need(data.startsWith(prefix)&&data.endsWith(");"),"Record data statement changed");
            const auto row=fields(data.sliced(prefix.size(),data.size()-prefix.size()-2));
            bool ok=false;const auto gap=row[0].toUInt(&ok);need(ok&&gap>0&&(gap==1||gap%2==0)&&row[2]=="C","Record gap/category invalid");
            const auto digits=row[8].toInt(&ok);need(ok&&digits>0,"Record digit count invalid");
            if(digits>29){if(!starts.contains(gap))beyond.insert(gap);continue;}
            const auto lower=QString::fromLatin1(row[9]);
            need(lower.size()==digits&&lower!="0"&&booking::canonicalUnsigned(lower)==lower,"Small record expression requires an explicit parser update");
            if(!starts.contains(gap)||booking::compareUnsigned(lower,starts.value(gap))<0)starts.insert(gap,lower);
            beyond.remove(gap);
        }
        need(starts.value(1)=="2"&&starts.value(2)=="3"&&starts.value(4)=="7","Record reference base identities invalid");
        quint32 minimum=2;while(starts.contains(minimum)&&booking::compareUnsigned(starts.value(minimum),"73786976294838206464")<=0)minimum+=2;
        QJsonObject known;for(auto i=starts.cbegin();i!=starts.cend();++i)known.insert(QString::number(i.key()),i.value());
        auto keys=beyond.values();std::sort(keys.begin(),keys.end());QJsonArray large;for(auto key:keys)large.append(qint64(key));
        const auto blob=QByteArray("blob ")+QByteArray::number(bytes.size())+'\0'+bytes;
        return {{"schema","prime-gap-record-reference-1"},{"supported_bits",QJsonArray{67,95}},
            {"retrieved_utc",utc},{"source_url",sourceUrl},{"source_blob_sha1",QString::fromLatin1(QCryptographicHash::hash(blob,QCryptographicHash::Sha1).toHex())},
            {"source_sha256",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())},
            {"known_gaps",starts.size()+beyond.size()},{"minimum_potential_gap_in_domain",qint64(minimum)},
            {"known_starts",known},{"known_beyond_domain",large}};
    }catch(const std::exception& e){return {{"error",QString::fromUtf8(e.what())}};}
}
class RecordReferenceRefresh::Impl {
public:
    RecordReferenceRefresh* owner;QString file;QNetworkAccessManager network;QTimer timer;QElapsedTimer attempt;
    QNetworkReply* reply{};QByteArray body,etag;QFutureWatcher<QJsonObject> parser;bool tooLarge{};
    Impl(RecordReferenceRefresh* o,QString f):owner(o),file(std::move(f)){
        QObject::connect(&timer,&QTimer::timeout,owner,[this]{owner->refresh();});timer.setInterval(60000);timer.start();
        QObject::connect(&parser,&QFutureWatcher<QJsonObject>::finished,owner,[this]{publish(parser.result());});
        QTimer::singleShot(0,owner,[this]{owner->refresh();});
    }
    ~Impl(){if(reply){reply->disconnect(owner);reply->abort();delete reply;}}
    void report(bool ok,const QString& message){if(owner->onChanged)owner->onChanged(ok,message);}
    void publish(QJsonObject data){
        if(data.contains("error")){report(false,"Reference update unavailable: "+data.value("error").toString());return;}
        if(data.value("known_gaps").toInteger()<1000){report(false,"Reference update incomplete; saved reference retained");return;}
        data.insert("http_etag",QString::fromLatin1(etag));QSaveFile out(file);const auto encoded=QJsonDocument(data).toJson(QJsonDocument::Compact);
        if(!out.open(QIODevice::WriteOnly)||out.write(encoded)!=encoded.size()||!out.commit()){report(false,"Reference update could not be saved: "+out.errorString()+"; previous reference retained");return;}
        report(true,"Official reference checked "+data.value("retrieved_utc").toString());
    }
};
RecordReferenceRefresh::RecordReferenceRefresh(QString file,QObject* parent):QObject(parent),impl_(std::make_unique<Impl>(this,std::move(file))){}
RecordReferenceRefresh::~RecordReferenceRefresh()=default;
void RecordReferenceRefresh::refresh(bool force){
    auto& s=*impl_;if(s.reply||s.parser.isRunning())return;
    const auto ref=readRecordReference(s.file);const auto when=QDateTime::fromString(ref.observedUtc,Qt::ISODateWithMs);
    if(!force&&ref.valid()&&when.secsTo(QDateTime::currentDateTimeUtc())<3600)return;
    if(!force&&s.attempt.isValid()&&s.attempt.elapsed()<15*60000)return;
    s.attempt.start();s.body.clear();s.tooLarge=false;
    QNetworkRequest request{QUrl(sourceUrl)};request.setTransferTimeout(30000);request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);request.setRawHeader("User-Agent","PGN-Horizon record reference");
    if(ref.valid()){QFile old(s.file);if(old.open(QIODevice::ReadOnly)&&old.size()<=4*1024*1024){const auto object=QJsonDocument::fromJson(old.readAll()).object();s.etag=object.value("http_etag").toString().toLatin1();if(!s.etag.isEmpty())request.setRawHeader("If-None-Match",s.etag);}}
    s.reply=s.network.get(request);s.reply->setReadBufferSize(1024*1024);
    QObject::connect(s.reply,&QNetworkReply::readyRead,this,[&s]{s.body+=s.reply->readAll();if(s.body.size()>DownloadLimit){s.tooLarge=true;s.reply->abort();}});
    QObject::connect(s.reply,&QNetworkReply::finished,this,[this,&s]{
        auto* reply=s.reply;s.reply=nullptr;s.body+=reply->readAll();const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();const auto error=reply->errorString();const bool failed=reply->error()!=QNetworkReply::NoError;const auto tag=reply->rawHeader("ETag");reply->deleteLater();
        if(s.tooLarge||s.body.size()>DownloadLimit||failed||(code!=200&&code!=304)){s.report(false,"Reference update unavailable; saved reference retained ("+error+")");return;}
        const auto utc=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        if(code==304){QFile old(s.file);if(!readRecordReference(s.file).valid()||!old.open(QIODevice::ReadOnly)||old.size()>4*1024*1024){s.report(false,"Saved reference invalid; complete reload required");return;}auto data=QJsonDocument::fromJson(old.readAll()).object();old.close();data.insert("retrieved_utc",utc);s.publish(data);return;}
        s.etag=tag;auto bytes=std::move(s.body);s.parser.setFuture(QtConcurrent::run([bytes=std::move(bytes),utc]{return parseRecordReferenceSql(bytes,utc);}));
    });
}
}
