#include "science/record_reference_refresh.h"
#include "science/record_submissions.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <iostream>
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);if(argc!=3)return 1;
    QFile dump(QString::fromLocal8Bit(argv[1]));if(!dump.open(QIODevice::ReadOnly)||dump.size()>64*1024*1024)return 2;
    const auto bytes=dump.readAll(),utc=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8();
    const auto parsed=pgn::science::parseRecordReferenceSql(bytes,QString::fromUtf8(utc));if(parsed.contains("error")){std::cerr<<QJsonDocument(parsed).toJson().constData();return 3;}
    QTemporaryDir temp;QSaveFile file(temp.filePath("reference.json"));if(!file.open(QIODevice::WriteOnly))return 4;file.write(QJsonDocument(parsed).toJson());if(!file.commit())return 5;
    const auto actual=pgn::science::readRecordReference(temp.filePath("reference.json")),golden=pgn::science::readRecordReference(QString::fromLocal8Bit(argv[2]));
    if(!actual.valid()||!golden.valid()||actual.starts!=golden.starts||actual.beyondDomain!=golden.beyondDomain||actual.minimumPotentialGap!=golden.minimumPotentialGap||actual.sourceBlob!=golden.sourceBlob)return 6;
    if(!pgn::science::parseRecordReferenceSql(bytes.left(bytes.size()-100),QString::fromUtf8(utc)).contains("error"))return 7;
    auto changed=bytes;changed.replace("1,'2');","1,'2+0');");if(!pgn::science::parseRecordReferenceSql(changed,QString::fromUtf8(utc)).contains("error"))return 8;
    changed=bytes;changed.replace("1,'2');","1,system());");if(!pgn::science::parseRecordReferenceSql(changed,QString::fromUtf8(utc)).contains("error"))return 9;
    auto published=actual;published.starts[1658]="199833824235735045053";published.starts[1790]="199890572137560908423";
    if(pgn::science::improvesRecordReference(published,1658,"199833824235735045053")||pgn::science::improvesRecordReference(published,1790,"199890572137560908423"))return 10;
    published.starts[1658]="199833824235735045051";if(pgn::science::improvesRecordReference(published,1658,"199833824235735045053"))return 11;
    std::cout<<"Reference refresh PASS: exact public dump parity, truncation/expression/code refused, published and superseded candidates removed; no SQL execution or submission\n";return 0;
}
