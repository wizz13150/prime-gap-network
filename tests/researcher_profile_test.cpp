#include "booking/booking_model.h"
#include "booking/researcher_name.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QTemporaryDir>
#include <iostream>

int main(int argc,char** argv){
    QCoreApplication app(argc,argv);int checks=0,failures=0;
    const auto check=[&](bool pass,const char* what){++checks;if(!pass){++failures;std::cerr<<"FAIL "<<what<<'\n';}};
    using pgn::booking::validResearcherName;
    for(int c=0;c<256;++c){
        const bool alpha=(c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9');
        const QString one(QChar(static_cast<ushort>(c)));
        check(validResearcherName(one)==alpha,"initial ASCII alphabet");
        check(validResearcherName(QString("A")+one)==(alpha||c=='_'||c=='-'),"remaining ASCII alphabet");
    }
    for(int size=0;size<=40;++size)check(validResearcherName(QString(size,'x'))==(size>0&&size<=24),"length boundary");
    for(const auto* bad:{" Benxy","Benxy ","Ben xy","éclair","Ｂenxy","a\xe2\x80\x8b" "b","a/b","a\\b","a.b","a:b","<b>","_name","-name","a\nb","a\rb"})
        check(!validResearcherName(QString::fromUtf8(bad)),"invalid/confusable name");
    for(const auto* good:{"Benxy","robert44444uk","A_1-b","0","ABCDEFGHIJKLMNOPQRSTUVWX"})check(validResearcherName(QString::fromLatin1(good)),"valid name");

    QTemporaryDir isolated;check(isolated.isValid(),"temporary root");
    const QString runtime=isolated.path()+"/data/runtime";
    check(QDir().mkpath(runtime),"temporary runtime");
    const QString path=runtime+"/community-profile.json";
    const auto write=[&](const QByteArray& bytes){QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size();};
    const auto read=[&]{QFile file(path);if(!file.open(QIODevice::ReadOnly))return QByteArray{};return file.readAll();};
    pgn::booking::BookingModel model(isolated.path());int notices=0;
    model.subscribe(&app,[&]{++notices;});
    check(model.researcherName()=="Your work","unchanged fallback");
    check(!model.setResearcherName("invalid name").isEmpty()&&!QFile::exists(path)&&notices==0,"reject before creating profile");
    check(model.setResearcherName("Benxy").isEmpty()&&model.researcherName()=="Benxy"&&notices==1,"first atomic profile save");
    {pgn::booking::BookingModel reopened(isolated.path());check(reopened.researcherName()=="Benxy","reopen persisted name");}
    check(write("{\"display_name\":\"Old\",\"other\":{\"preserved\":7},\"version\":2}"),"seed unknown fields");
    check(model.setResearcherName("New_name-42").isEmpty(),"update profile");
    const auto object=QJsonDocument::fromJson(read()).object();
    check(object.value("other").toObject().value("preserved").toInt()==7&&object.value("version").toInt()==2,"preserve unknown fields");
    const auto stable=read();const auto stableName=model.researcherName();const auto stableNotices=notices;
    check(!model.setResearcherName(QString(25,'a')).isEmpty()&&read()==stable&&notices==stableNotices,"overlong save no mutation");
    {QLockFile held(path+".lock");held.setStaleLockTime(0);check(held.tryLock(0),"hold profile lock");
     check(!model.setResearcherName("Busy").isEmpty()&&read()==stable&&model.researcherName()==stableName&&notices==stableNotices,"concurrent writer refused");}
    for(const auto& incompatible:{QByteArray("{broken"),QByteArray("[]"),QByteArray(4097,' ')}){
        check(write(incompatible),"seed incompatible profile");
        check(!model.setResearcherName("Safe").isEmpty()&&read()==incompatible&&model.researcherName()==stableName&&notices==stableNotices,"invalid existing profile preserved");
    }
    const auto legacy=QString(60,'x');
    check(write(QJsonDocument(QJsonObject{{"display_name",legacy}}).toJson()),"seed legacy name");
    {pgn::booking::BookingModel historical(isolated.path());check(historical.researcherName()==legacy,"legacy names not truncated or erased");}
    check(QFile::remove(path)&&QDir().mkdir(path),"seed unwritable profile target");
    check(!model.setResearcherName("Safe").isEmpty()&&model.researcherName()==stableName&&notices==stableNotices,"failed write preserves displayed identity");
    std::cout<<"researcher profile: "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
