#include "mining/source_history.h"
#include "mining/source_mirror.h"
#include "mining/native_source_identity.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <iostream>
#include <stdexcept>

using namespace pgn::mining;
namespace {
void need(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
qint64 integer(const QString& value,bool positive=false){
    static const QRegularExpression digits("^(0|[1-9][0-9]*)$");bool ok=false;
    const auto number=value.toLongLong(&ok);
    need(digits.match(value).hasMatch()&&ok&&number>=0&&(!positive||number>0),"Canonical signed64 decimal receipt/argument required");return number;
}
// Qt validates JSON syntax; this bounded lexical pass only preserves duplicate
// decoded-key evidence and the manifest PID lexeme that last-wins/rounding loses.
class ReceiptKeys {
    const QByteArray& b;qsizetype at{};
    void space(){while(at<b.size()&&(b[at]==' '||b[at]=='\n'||b[at]=='\r'||b[at]=='\t'))++at;}
    QByteArray string(){const auto from=at++;while(at<b.size()){const auto c=b[at++];if(c=='\\'){++at;continue;}if(c=='"')return b.mid(from,at-from);}throw std::runtime_error("Receipt string truncated");}
    void value(const QString& path,int depth){
        need(depth<=64,"Receipt nesting outside bound");space();need(at<b.size(),"Receipt value missing");
        if(b[at]=='{'||b[at]=='['){const bool object=b[at++]=='{';const char end=object?'}':']';QSet<QString> keys;space();
            while(at<b.size()&&b[at]!=end){QString key;
                if(object){need(b[at]=='"',"Receipt key missing");const auto raw=string();const auto array=QJsonDocument::fromJson("["+raw+"]").array();
                    need(array.size()==1&&array[0].isString(),"Receipt key invalid");key=array[0].toString();need(!keys.contains(key),"Duplicate decoded receipt key");keys.insert(key);space();need(at<b.size()&&b[at++]==':',"Receipt separator missing");}
                value(path+"/"+(object?key:"[]"),depth+1);space();if(at<b.size()&&b[at]==','){++at;space();}else break;
            }need(at<b.size()&&b[at++]==end,"Receipt container truncated");return;
        }
        if(b[at]=='"'){string();return;}
        const auto from=at;while(at<b.size()&&b[at]!=','&&b[at]!=']'&&b[at]!='}'&&b[at]!=' '&&b[at]!='\r'&&b[at]!='\n'&&b[at]!='\t')++at;
        if(path=="/manifest/pid")pidLexeme=b.mid(from,at-from);
    }
public:
    QByteArray pidLexeme;
    explicit ReceiptKeys(const QByteArray& bytes):b(bytes){value({},0);space();need(at==b.size(),"Receipt trailing data");}
};
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);QCoreApplication::setApplicationName("pgn-enroll-legacy-source");
    QCommandLineParser parser;parser.setApplicationDescription("EXPLICIT existing-DB legacy prefix/declared-identity enrollment. Offline linear prefix+origin audit; held Windows source handle. No GAP/origin/time/cursor changes, ingestion, process, GPU or network action. Not authentication/powercut proof. Relative receipt provenance paths resolve against explicit invocation working directory.");parser.addHelpOption();
    for(const auto& name:{"database-existing","mirror-directory","run","device","manifest-receipt","expected-offset","expected-prefix-sha256"})parser.addOption({QString::fromLatin1(name),QString::fromLatin1(name),"value"});
    parser.process(app);
    try{
        need(parser.positionalArguments().isEmpty(),"Unexpected positional arguments");
        for(const auto& name:{"database-existing","mirror-directory","run","device","manifest-receipt","expected-offset","expected-prefix-sha256"})need(parser.isSet(QString::fromLatin1(name))&&!parser.value(QString::fromLatin1(name)).isEmpty(),"All enrollment arguments must be explicit");
        const auto database=parser.value("database-existing"),directory=parser.value("mirror-directory"),run=parser.value("run"),device=parser.value("device");
        const auto receiptPath=parser.value("manifest-receipt");const auto offset=integer(parser.value("expected-offset"),true);const auto digest=parser.value("expected-prefix-sha256").toLatin1();
        need(QFileInfo(database).isAbsolute()&&QFileInfo(directory).isAbsolute()&&QFileInfo(receiptPath).isAbsolute(),"Explicit absolute local paths required");
        const QFileInfo info(receiptPath);need(info.isFile()&&!info.isSymLink()&&info.size()>0&&info.size()<=16384,"Receipt must be existing regular non-symlink file <=16KiB");
        QFile input(receiptPath);need(input.open(QIODevice::ReadOnly),"Receipt unavailable");const auto raw=input.read(16385);
        need(raw.size()<=16384&&input.atEnd()&&QString::fromUtf8(raw).toUtf8()==raw,"Receipt oversized or invalid UTF8/BOM");
        QJsonParseError error;const auto document=QJsonDocument::fromJson(raw,&error);need(error.error==QJsonParseError::NoError&&document.isObject(),"Receipt JSON object required");
        ReceiptKeys lexical(raw);const auto receipt=document.object();
        need(receipt.value("schema")=="pgn-root-legacy-source-audit-1"&&receipt.value("writer")=="root"&&receipt.value("run")==run&&receipt.value("device")==device&&receipt.value("manifest").isObject(),"Receipt identity/schema differs from explicit arguments");
        need(receipt.value("committed_offset").isString()&&integer(receipt.value("committed_offset").toString(),true)==offset&&receipt.value("expected_prefix_sha256").toString().toLatin1()==digest,"Receipt prefix tuple differs from explicit arguments");
        need(QRegularExpression("^[0-9a-f]{64}$").match(QString::fromLatin1(digest)).hasMatch(),"Prefix SHA256 must be canonical lowercase");
        need(QFileInfo(receipt.value("local_history").toString()).absoluteFilePath()==QFileInfo(database).absoluteFilePath()&&QFileInfo(receipt.value("local_mirror_directory").toString()).absoluteFilePath()==QFileInfo(directory).absoluteFilePath(),"Receipt local paths differ from explicit targets");
        const auto remote=receipt.value("remote").toObject();
        need(remote.value("owned").isBool()&&!remote.value("owned").toBool()&&remote.value("state")=="OFF"&&remote.value("prefix_sha256").toString().toLatin1()==digest&&remote.value("prefix_interval")=="[0,"+QString::number(offset)+")"&&remote.value("source_size").isString()&&integer(remote.value("source_size").toString())>=offset,"Independent prefix receipt missing or contradictory");
        const auto manifest=receipt.value("manifest").toObject();
        const auto pid=integer(QString::fromLatin1(lexical.pidLexeme),true);need(pid<=std::numeric_limits<quint32>::max()&&manifest.value("pid")==QJsonValue(pid),"Manifest PID lexeme is not exact");
        const auto identity=nativeSourceObservationIdentity(manifest,run,device);
        SourceMirror mirror(directory);auto freeze=mirror.freezeForLineage();auto held=freeze->open(run);
        need(receipt.value("local_mirror_size").isString()&&held->size()==integer(receipt.value("local_mirror_size").toString())&&held->size()>=offset,"Held local mirror size differs from audited receipt");
        SourceHistory::enrollLegacySourceExisting(database,run,device,manifest,offset,digest,
            [&](qint64 at,qsizetype count){return held->read(at,count);},[&]{held->verifyStable();});
        // Handle/barrier remains live through the SQL commit. This close is not
        // the precommit validation and never grants rollback after COMMITTED.
        held->closeAndVerifyStable();
        const QJsonObject result{{"schema","pgn-native-legacy-source-enrollment-1"},{"state","COMMITTED"},{"run",run},{"source","native/"+run+".stdout"},
            {"database",database},{"committed_offset",QString::number(offset)},{"prefix_sha256",QString::fromLatin1(digest)},
            {"declared_identity",identity},{"audit_receipt_sha256",QString::fromLatin1(QCryptographicHash::hash(raw,QCryptographicHash::Sha256).toHex())},
            {"scope","PREFIX_ORIGIN_AUDIT_AND_DECLARED_IDENTITY_NOT_AUTHENTICATION"},{"unchanged","GAP/origin/time/cursor bytes; remote tail is not imported"}};
        std::cout<<QJsonDocument(result).toJson(QJsonDocument::Compact).constData()<<'\n';return 0;
    }catch(const std::exception& failure){std::cerr<<"Legacy enrollment held (inspect committed state if post-commit output failed): "<<failure.what()<<'\n';return 1;}
}
