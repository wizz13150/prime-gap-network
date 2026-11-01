#include "mining/source_mirror.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#endif

namespace {
int checks=0;
void require(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);}
template<class F> void rejected(F operation,const char* what){bool held=false;try{operation();}catch(const std::exception&){held=true;}require(held,what);}
QByteArray contents(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("fixture read");return f.readAll();}
QString run(int n){return QString::number(n,16).rightJustified(32,'0');}
#ifdef Q_OS_WIN
std::wstring native(const QString& path){return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()).toStdWString();}
void writerDenied(const QString& path){
    const auto p=native(path);
    const auto h=CreateFileW(p.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    const auto error=GetLastError();if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);
    require(h==INVALID_HANDLE_VALUE&&error==ERROR_SHARING_VIOLATION,"second native writer really denied");
    const bool deleted=DeleteFileW(p.c_str());const auto deleteError=GetLastError();
    require(!deleted&&deleteError==ERROR_SHARING_VIOLATION,"native delete really denied");
}
void writerAllowed(const QString& path){
    const auto p=native(path);
    const auto h=CreateFileW(p.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    require(h!=INVALID_HANDLE_VALUE,"native writer allowed after release");
    if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);
}
// Own empty temporary directory only. Junctions exercise REAL reparse metadata
// without needing symlink privileges or Developer Mode. Removed before cleanup.
class Junction final {
public:
    QString link;
    explicit Junction(QString path,const QString& target):link(std::move(path)){
        require(QDir().mkdir(link),"fixture junction directory");
        const auto p=native(link),print=native(target),substitute=L"\\??\\"+print;
        const auto h=CreateFileW(p.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,
            OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
        require(h!=INVALID_HANDLE_VALUE,"fixture junction handle");
        struct Header {DWORD tag;WORD length,reserved,subOffset,subLength,printOffset,printLength;};
        static_assert(sizeof(Header)==16);
        const auto subBytes=static_cast<WORD>(substitute.size()*sizeof(wchar_t));
        const auto printBytes=static_cast<WORD>(print.size()*sizeof(wchar_t));
        const Header header{IO_REPARSE_TAG_MOUNT_POINT,static_cast<WORD>(8+subBytes+printBytes+4),0,
            0,subBytes,static_cast<WORD>(subBytes+2),printBytes};
        QByteArray data(sizeof(Header)+subBytes+printBytes+4,0);
        std::memcpy(data.data(),&header,sizeof(header));
        std::memcpy(data.data()+sizeof(header),substitute.data(),subBytes);
        std::memcpy(data.data()+sizeof(header)+subBytes+2,print.data(),printBytes);
        DWORD returned=0;const bool set=DeviceIoControl(h,FSCTL_SET_REPARSE_POINT,data.data(),static_cast<DWORD>(data.size()),nullptr,0,&returned,nullptr);
        const auto error=GetLastError();CloseHandle(h);
        if(!set)throw std::runtime_error("fixture junction creation Windows error "+std::to_string(error));
        require(set,"real native junction installed");
    }
    ~Junction(){
        const auto p=native(link);const auto h=CreateFileW(p.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
        if(h!=INVALID_HANDLE_VALUE){struct Delete {DWORD tag;WORD length,reserved;} header{IO_REPARSE_TAG_MOUNT_POINT,0,0};
            DWORD result=0;DeviceIoControl(h,FSCTL_DELETE_REPARSE_POINT,&header,sizeof(header),nullptr,0,&result,nullptr);CloseHandle(h);}
        RemoveDirectoryW(p.c_str());
    }
    Junction(const Junction&)=delete;Junction& operator=(const Junction&)=delete;
};
#endif
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
#ifndef Q_OS_WIN
    std::cerr<<"SourceMirror Windows contract is not qualified on this platform\n";return 2;
#else
    try{
        QTemporaryDir temporary;require(temporary.isValid(),"own temporary directory");
        const auto directory=temporary.filePath("mirror");require(QDir().mkdir(directory),"own mirror directory");
        const auto a=run(1),aPath=QDir(directory).filePath(a+".stdout");
        {
            pgn::mining::SourceMirror mirror(directory);
            require(!mirror.size(a)&&!QFileInfo::exists(aPath),"missing is absent, size creates nothing");
            rejected([&]{mirror.append(a,1,"x");},"sparse missing packet held");
            require(!QFileInfo::exists(aPath),"sparse missing creates no file");
            const auto entries=QDir(directory).entryList(QDir::AllEntries|QDir::NoDotAndDotDot);
            for(const auto invalid:{QString{},QString(31,'a'),QString(32,'A'),QString(32,'g'),QString("../x"),QString(32,QChar(0))}){
                rejected([&]{mirror.append(invalid,0,"x");},"invalid run held");
                rejected([&]{mirror.size(invalid);},"invalid run size held");
            }
            rejected([&]{mirror.append(a,-1,"x");},"negative offset held");
            rejected([&]{mirror.append(a,0,QByteArray(65537,'x'));},"oversize held");
            rejected([&]{mirror.append(a,std::numeric_limits<qint64>::max(),"x");},"offset plus length overflow held");
            require(QDir(directory).entryList(QDir::AllEntries|QDir::NoDotAndDotDot)==entries,"invalid packets create nothing");
            mirror.append(a,0,{});require(mirror.size(a)==0&&contents(aPath).isEmpty(),"existing empty0 valid");
            const auto state=mirror.inspect(a);require(state&&state->handle_epoch,"held handle token present");
            writerDenied(aPath);
            QByteArray data(65536,Qt::Uninitialized);for(qsizetype i=0;i<data.size();++i)data[i]=char(i%251);
            mirror.append(a,0,data,state->handle_epoch);require(contents(aPath)==data,"max batch binary/NUL exact");
            mirror.append(a,0,data,state->handle_epoch);require(contents(aPath)==data,"complete replay unchanged");
            mirror.append(a,5,data.mid(5,17),state->handle_epoch);require(contents(aPath)==data,"interior replay does not truncate");
            QByteArray conflict=data.mid(1);conflict.back()=char(conflict.back()^1);conflict+='x';
            rejected([&]{mirror.append(a,1,conflict,state->handle_epoch);},"last overlap conflict rejected before any tail");
            require(contents(aPath)==data,"entire prefix and length intact on late conflict");
            const auto matching=data.mid(1)+QByteArray("Z");mirror.append(a,1,matching,state->handle_epoch);data+='Z';
            require(contents(aPath)==data&&mirror.size(a)==65537,"matching overlap adds only tail");
            rejected([&]{mirror.append(a,65538,"hole",state->handle_epoch);},"existing sparse gap held");
            require(contents(aPath)==data,"existing sparse failure unchanged");
            require(mirror.read(a,0,65536,state->handle_epoch)==data.left(65536),"bounded handle read exact");
            require(mirror.read(a,65536,65536,state->handle_epoch)=="Z","read clips only at real EOF");
            require(mirror.read(a,65537,0,state->handle_epoch).isEmpty(),"known EOF empty read");
            rejected([&]{mirror.read(a,65538,1,state->handle_epoch);},"read beyond EOF held");
            rejected([&]{mirror.read(a,0,65537,state->handle_epoch);},"oversize read held");
            rejected([&]{mirror.append(a,0,{},state->handle_epoch+1);},"wrong incarnation append held");
            require(contents(aPath)==data,"wrong token unchanged");
            // Eight OTHER runs evict a, releasing its exclusion but never data.
            for(int i=2;i<=9;++i)mirror.append(run(i),0,QByteArray::number(i));
            writerAllowed(aPath);writerDenied(mirror.file(run(9)));
            const auto next=mirror.inspect(a);require(next&&next->handle_epoch!=state->handle_epoch&&next->size==data.size(),"eviction/reopen gets new token, data retained");
            rejected([&]{mirror.read(a,0,1,state->handle_epoch);},"stale session cannot read as validated prefix");
            rejected([&]{mirror.append(a,0,{},state->handle_epoch);},"stale session cannot append");
            require(mirror.read(a,0,65536,next->handle_epoch)==data.left(65536),"new session handle can reverify");
            writerDenied(aPath);
        }
        writerAllowed(aPath);
        {
            pgn::mining::SourceMirror reopen(directory);const auto state=reopen.inspect(a);
            require(state&&state->size==65537,"close/reopen conserved exact length");
            require(reopen.read(a,65536,1,state->handle_epoch)=="Z","close/reopen conserved durable tail");
            reopen.append(a,65536,"Z",state->handle_epoch);writerDenied(aPath);
        }
        const auto missing=temporary.filePath("not-created");
        rejected([&]{pgn::mining::SourceMirror absent(missing);},"missing directory held");
        require(!QFileInfo::exists(missing),"missing directory not created");
        rejected([&]{pgn::mining::SourceMirror nonregular(aPath);},"regular file not accepted as root");
        const auto fakeRun=run(100);const auto fakePath=QDir(directory).filePath(fakeRun+".stdout");require(QDir().mkdir(fakePath),"fixture final directory");
        {pgn::mining::SourceMirror store(directory);rejected([&]{store.append(fakeRun,0,"x");},"nonregular final refused");}
        require(QDir(fakePath).entryList(QDir::AllEntries|QDir::NoDotAndDotDot).isEmpty(),"nonregular final unchanged");
        const auto target=temporary.filePath("junction-target");require(QDir().mkdir(target),"own junction target");
        {
            Junction junction(temporary.filePath("root-junction"),target);
            rejected([&]{pgn::mining::SourceMirror store(junction.link);},"real reparse root refused");
        }
        {
            const auto linkRun=run(101);Junction junction(QDir(directory).filePath(linkRun+".stdout"),target);
            pgn::mining::SourceMirror store(directory);rejected([&]{store.append(linkRun,0,"x");},"real final reparse refused");
            require(QDir(target).entryList(QDir::AllEntries|QDir::NoDotAndDotDot).isEmpty(),"reparse target unchanged");
        }
        require(contents(aPath).size()==65537,"all rejection tests preserve source bytes");
        std::cout<<"SOURCE_MIRROR "<<checks<<" checks / 0 failures; native Windows held-handle overlap/tail/replay, real FlushFileBuffers return, writer/delete exclusion, LRU epochs, real junction refusal; own temporary bytes only\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"SOURCE_MIRROR failed after "<<checks<<": "<<error.what()<<'\n';return 1;}
#endif
}
