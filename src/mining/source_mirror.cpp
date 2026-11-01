#include "mining/source_mirror.h"
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <cstring>
#include <limits>
#include <list>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pgn::mining {
namespace {
[[noreturn]] void fail(const char* message){throw std::runtime_error(message);}
void validateRun(const QString& run){
    if(run.size()!=32)fail("Invalid source mirror identity");
    for(const auto c:run)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))fail("Invalid source mirror identity");
}
#ifdef Q_OS_WIN
[[noreturn]] void winFail(const char* message){
    const auto error=GetLastError();
    throw std::runtime_error(std::string(message)+" (Windows error "+std::to_string(error)+")");
}
class Handle final {
public:
    HANDLE value{INVALID_HANDLE_VALUE};
    explicit Handle(HANDLE h=INVALID_HANDLE_VALUE):value(h){}
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    Handle(Handle&& other) noexcept:value(std::exchange(other.value,INVALID_HANDLE_VALUE)){}
};
BY_HANDLE_FILE_INFORMATION checkHandle(HANDLE handle,bool directory){
    if(GetFileType(handle)!=FILE_TYPE_DISK)fail("Source mirror requires a disk file/directory");
    FILE_ATTRIBUTE_TAG_INFO attributes{};
    if(!GetFileInformationByHandleEx(handle,FileAttributeTagInfo,&attributes,sizeof(attributes)))winFail("Source handle attributes unavailable");
    if((attributes.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||
       bool(attributes.FileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=directory)
        fail("Source mirror reparse/nonregular object refused");
    BY_HANDLE_FILE_INFORMATION identity{};
    if(!GetFileInformationByHandle(handle,&identity))winFail("Source handle identity unavailable");
    return identity;
}
qint64 lengthOf(HANDLE handle){
    LARGE_INTEGER size{};if(!GetFileSizeEx(handle,&size))winFail("Source handle size unavailable");
    if(size.QuadPart<0)fail("Source handle size invalid");return size.QuadPart;
}
void seek(HANDLE handle,qint64 offset){
    LARGE_INTEGER position{};position.QuadPart=offset;
    if(!SetFilePointerEx(handle,position,nullptr,FILE_BEGIN))winFail("Source handle seek failed");
}
#endif
}
class SourceMirror::Impl {
public:
    QString directory;
    mutable std::mutex mutex;
    bool frozen{};
    const std::thread::id owner=std::this_thread::get_id();
    void requireWritableEpoch()const{if(frozen)fail("Source mirror frozen for lineage; chunk must be replayed after release");}
#ifdef Q_OS_WIN
    struct Entry {
        QString run;Handle handle;BY_HANDLE_FILE_INFORMATION identity;std::uint64_t epoch;bool durable{};
        Entry(QString name,Handle&& h,BY_HANDLE_FILE_INFORMATION id,std::uint64_t token):run(std::move(name)),handle(std::move(h)),identity(id),epoch(token){}
    };
    Handle root;
    std::wstring nativeDirectory;
    std::list<Entry> files; // MRU first; eviction releases exclusion, NOT data.
    std::uint64_t epoch{};
    explicit Impl(const QString& path):directory(QDir::cleanPath(QFileInfo(path).absoluteFilePath())){
        if(path.isEmpty()||path.contains(QChar(0)))fail("Explicit existing source directory required");
        const auto native=QDir::toNativeSeparators(directory).toStdWString();
        Handle opened(CreateFileW(native.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
            nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(opened.value==INVALID_HANDLE_VALUE)winFail("Source directory unavailable; not created");
        checkHandle(opened.value,true);
        // Identity/type come from the opened handle, not a path-stat race.
        // Its no-share-DELETE protects this root object, not hostile ancestors.
        wchar_t resolved[32768];
        const auto count=GetFinalPathNameByHandleW(opened.value,resolved,32768,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
        if(!count||count>=32768)winFail("Source directory handle path unavailable");
        nativeDirectory.assign(resolved,count);
        if(nativeDirectory.back()!=L'\\')nativeDirectory+=L'\\';
        root.value=std::exchange(opened.value,INVALID_HANDLE_VALUE);
    }
    Entry* open(const QString& run,bool create){
        for(auto it=files.begin();it!=files.end();++it)if(it->run==run){files.splice(files.begin(),files,it);return &files.front();}
        if(epoch==std::numeric_limits<std::uint64_t>::max())fail("Source handle incarnation capacity exhausted");
        const auto path=nativeDirectory+run.toStdWString()+L".stdout";
        constexpr DWORD flags=FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS;
        Handle opened(CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,flags,nullptr));
        if(opened.value==INVALID_HANDLE_VALUE){
            if(GetLastError()!=ERROR_FILE_NOT_FOUND)winFail("Conserved source file unavailable");
            if(!create)return nullptr;
            // Only offset0 may reach CREATE_NEW. Never OPEN_ALWAYS/TRUNCATE.
            opened.value=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,flags,nullptr);
            if(opened.value==INVALID_HANDLE_VALUE){
                if(GetLastError()!=ERROR_FILE_EXISTS&&GetLastError()!=ERROR_ALREADY_EXISTS)winFail("Source file creation refused");
                opened.value=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,flags,nullptr);
                if(opened.value==INVALID_HANDLE_VALUE)winFail("Concurrent source creation unavailable");
            }
        }
        const auto identity=checkHandle(opened.value,false);
        if(files.size()==HandleLimit)files.pop_back();
        files.emplace_front(run,std::move(opened),identity,++epoch);return &files.front();
    }
#else
    explicit Impl(const QString&){fail("SourceMirror native file contract is Windows-only");}
#endif
};
SourceMirror::SourceMirror(const QString& directory):impl_(std::make_unique<Impl>(directory)){}
SourceMirror::~SourceMirror()=default;
QString SourceMirror::file(const QString& run)const{
    validateRun(run);return QDir(impl_->directory).filePath(run+".stdout");
}
std::optional<qint64> SourceMirror::size(const QString& run){
    if(const auto state=inspect(run))return state->size;
    return {};
}
std::optional<SourceMirrorState> SourceMirror::inspect(const QString& run){
    validateRun(run);
    const std::lock_guard lock(impl_->mutex);impl_->requireWritableEpoch();
#ifdef Q_OS_WIN
    if(const auto entry=impl_->open(run,false))return SourceMirrorState{lengthOf(entry->handle.value),entry->epoch};
    return {};
#else
    fail("SourceMirror native file contract is Windows-only");
#endif
}
QByteArray SourceMirror::read(const QString& run,qint64 offset,qsizetype maximum,std::uint64_t expectedHandleEpoch){
    validateRun(run);
    if(offset<0||maximum<0||maximum>BatchLimit)fail("Invalid source prefix read bounds");
    const std::lock_guard lock(impl_->mutex);impl_->requireWritableEpoch();
#ifdef Q_OS_WIN
    const auto* entry=impl_->open(run,false);
    if(!entry||entry->epoch!=expectedHandleEpoch)fail("Source handle changed; enrolled prefix requires verification");
    const auto length=lengthOf(entry->handle.value);
    if(offset>length)fail("Source prefix read starts beyond EOF");
    const auto wanted=static_cast<qsizetype>(std::min<qint64>(maximum,length-offset));
    QByteArray result(wanted,Qt::Uninitialized);seek(entry->handle.value,offset);
    DWORD read=0;
    while(read<static_cast<DWORD>(wanted)){
        DWORD count=0;
        if(!ReadFile(entry->handle.value,result.data()+read,static_cast<DWORD>(wanted)-read,&count,nullptr))winFail("Source prefix read failed");
        if(!count)fail("Source prefix ended early");read+=count;
    }
    return result;
#else
    fail("SourceMirror native file contract is Windows-only");
#endif
}
void SourceMirror::append(const QString& run,qint64 offset,const QByteArray& bytes,std::optional<std::uint64_t> expectedHandleEpoch){
    validateRun(run);
    if(offset<0||bytes.size()>BatchLimit||offset>std::numeric_limits<qint64>::max()-bytes.size())fail("Invalid source mirror packet bounds");
    const std::lock_guard lock(impl_->mutex);impl_->requireWritableEpoch();
#ifdef Q_OS_WIN
    auto* entry=impl_->open(run,offset==0&&!expectedHandleEpoch);
    if(!entry)fail("Source mirror missing prefix; file not created");
    if(expectedHandleEpoch&&entry->epoch!=*expectedHandleEpoch)fail("Source handle changed; enrolled prefix requires verification");
    const auto handle=entry->handle.value;
    const auto length=lengthOf(handle);
    if(offset>length)fail("Source mirror missing prefix; original bytes retained");
    const auto overlap=static_cast<qsizetype>(std::min<qint64>(bytes.size(),length-offset));
    if(overlap){
        QByteArray previous(overlap,Qt::Uninitialized);seek(handle,offset);
        DWORD received=0;
        while(received<static_cast<DWORD>(overlap)){
            DWORD count=0;
            if(!ReadFile(handle,previous.data()+received,static_cast<DWORD>(overlap)-received,&count,nullptr))winFail("Source overlap read failed");
            if(!count)fail("Source overlap ended early; no bytes written");received+=count;
        }
        if(std::memcmp(previous.constData(),bytes.constData(),overlap))fail("Source mirror conflict; original bytes left intact");
    }
    if(overlap<bytes.size()){
        seek(handle,length); // matched overlap ends EXACTLY at EOF, no sparse gap.
        entry->durable=false;
        auto written=overlap;
        while(written<bytes.size()){
            DWORD count=0;
            if(!WriteFile(handle,bytes.constData()+written,static_cast<DWORD>(bytes.size()-written),&count,nullptr))winFail("Source tail write failed; partial suffix may remain");
            if(!count)fail("Source tail write made no progress; partial suffix may remain");written+=count;
        }
    }
    if(!entry->durable){
        if(!FlushFileBuffers(handle))winFail("Source durability not established; identical retry required");
        entry->durable=true;
    }
#else
    fail("SourceMirror native file contract is Windows-only");
#endif
}
class SourceMirrorReadFreeze::Impl {
public:
    std::shared_ptr<SourceMirror::Impl> mirror;
    bool armed{};
    explicit Impl(std::shared_ptr<SourceMirror::Impl> value):mirror(std::move(value)){}
};
class SourceMirrorFrozenFile::Impl {
public:
    // The last reader keeps the writer barrier AND original root/LRU handles
    // alive even when the owner/scope/opener has already been destroyed.
    std::shared_ptr<SourceMirrorReadFreeze> freeze;
#ifdef Q_OS_WIN
    Handle handle;
    BY_HANDLE_FILE_INFORMATION initial{};
    std::wstring path;
    qint64 length{};
    Impl(std::shared_ptr<SourceMirrorReadFreeze> value,Handle&& opened,std::wstring name)
        :freeze(std::move(value)),handle(std::move(opened)),path(std::move(name)){
        initial=checkHandle(handle.value,false);length=lengthOf(handle.value);verifyPath();
    }
    static bool sameIdentity(const BY_HANDLE_FILE_INFORMATION& a,const BY_HANDLE_FILE_INFORMATION& b){
        return a.dwVolumeSerialNumber==b.dwVolumeSerialNumber&&a.nFileIndexHigh==b.nFileIndexHigh&&a.nFileIndexLow==b.nFileIndexLow&&
            CompareFileTime(&a.ftCreationTime,&b.ftCreationTime)==0;
    }
    void verifyPath()const{
        // A duplicate of the ingestion RW handle still has its original
        // SHARE_READ-only exclusion. This additional READ probe must SHARE
        // WRITE to be compatible with it, but cannot weaken its exclusion.
        Handle probe(CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr));
        if(probe.value==INVALID_HANDLE_VALUE)winFail("Frozen source path identity unavailable");
        if(!sameIdentity(initial,checkHandle(probe.value,false)))fail("Frozen source path identity changed");
    }
    void verify()const{
        const auto final=checkHandle(handle.value,false);
        if(!sameIdentity(initial,final)||lengthOf(handle.value)!=length||
            CompareFileTime(&initial.ftLastWriteTime,&final.ftLastWriteTime)!=0||initial.dwFileAttributes!=final.dwFileAttributes)
            fail("Frozen source identity/birth/size/write-time changed");
        verifyPath();
    }
#endif
};
SourceMirrorReadFreeze::SourceMirrorReadFreeze(std::unique_ptr<Impl> value):impl_(std::move(value)){}
SourceMirrorReadFreeze::~SourceMirrorReadFreeze(){if(impl_->armed){const std::lock_guard lock(impl_->mirror->mutex);impl_->mirror->frozen=false;}}
std::shared_ptr<SourceMirrorReadFreeze> SourceMirror::freezeForLineage(){
    const std::lock_guard lock(impl_->mutex);
    if(std::this_thread::get_id()!=impl_->owner)fail("Source lineage freeze must begin on the mirror owner thread");
    impl_->requireWritableEpoch();
    auto freeze=std::shared_ptr<SourceMirrorReadFreeze>(new SourceMirrorReadFreeze(std::make_unique<SourceMirrorReadFreeze::Impl>(impl_)));
    freeze->impl_->armed=true;impl_->frozen=true;return freeze;
}
bool SourceMirror::lineageFrozen()const{const std::lock_guard lock(impl_->mutex);return impl_->frozen;}
std::unique_ptr<SourceMirrorFrozenFile> SourceMirrorReadFreeze::open(const QString& run){
    validateRun(run);const auto& mirror=impl_->mirror;const std::lock_guard lock(mirror->mutex);
    if(!mirror->frozen)fail("Source lineage read requires its active freeze");
#ifdef Q_OS_WIN
    Handle opened;
    for(const auto& entry:mirror->files)if(entry.run==run){
        HANDLE duplicate=INVALID_HANDLE_VALUE;
        if(!DuplicateHandle(GetCurrentProcess(),entry.handle.value,GetCurrentProcess(),&duplicate,
            FILE_READ_DATA|FILE_READ_ATTRIBUTES|SYNCHRONIZE,FALSE,0))winFail("Frozen ingestion handle duplication failed");
        opened.value=duplicate;break;
    }
    const auto path=mirror->nativeDirectory+run.toStdWString()+L".stdout";
    if(opened.value==INVALID_HANDLE_VALUE){
        // Historical source is opened EXISTING and never inserted/adopted in
        // the ingestion LRU. Its own handle denies writes AND deletes.
        opened.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
        if(opened.value==INVALID_HANDLE_VALUE)winFail("Frozen historical source unavailable; never created");
    }
    return std::unique_ptr<SourceMirrorFrozenFile>(new SourceMirrorFrozenFile(std::make_unique<SourceMirrorFrozenFile::Impl>(shared_from_this(),std::move(opened),path)));
#else
    fail("Source lineage held-handle contract is Windows-only");
#endif
}
SourceMirrorFrozenFile::SourceMirrorFrozenFile(std::unique_ptr<Impl> value):impl_(std::move(value)){}
SourceMirrorFrozenFile::~SourceMirrorFrozenFile()=default;
qint64 SourceMirrorFrozenFile::size()const{if(!impl_)fail("Frozen source reader already closed");
#ifdef Q_OS_WIN
    return impl_->length;
#else
    fail("Source lineage held-handle contract is Windows-only");
#endif
}
QByteArray SourceMirrorFrozenFile::read(qint64 offset,qsizetype maximum){
    if(!impl_)fail("Frozen source reader already closed");
    if(offset<0||maximum<0||maximum>SourceMirror::BatchLimit)fail("Frozen source read bound invalid");
    // DuplicateHandle shares the original synchronous file pointer. Serialize
    // ALL frozen readers under the same barrier; mirror reads/writes refuse.
    const std::lock_guard lock(impl_->freeze->impl_->mirror->mutex);
#ifdef Q_OS_WIN
    if(offset>impl_->length)fail("Frozen source read starts beyond EOF");
    const auto wanted=static_cast<qsizetype>(std::min<qint64>(maximum,impl_->length-offset));
    QByteArray result(wanted,Qt::Uninitialized);seek(impl_->handle.value,offset);DWORD received=0;
    while(received<static_cast<DWORD>(wanted)){DWORD count=0;
        if(!ReadFile(impl_->handle.value,result.data()+received,static_cast<DWORD>(wanted)-received,&count,nullptr))winFail("Frozen source read failed");
        if(!count)fail("Frozen source ended before pinned EOF");received+=count;}
    return result;
#else
    fail("Source lineage held-handle contract is Windows-only");
#endif
}
void SourceMirrorFrozenFile::verifyStable()const{
    if(!impl_)fail("Frozen source reader already closed");
    const std::lock_guard lock(impl_->freeze->impl_->mirror->mutex);
#ifdef Q_OS_WIN
    impl_->verify();
#else
    fail("Source lineage held-handle contract is Windows-only");
#endif
}
void SourceMirrorFrozenFile::closeAndVerifyStable(){
    if(!impl_)fail("Frozen source reader already closed");
    // Own it locally before checks: release on success AND exception, after
    // unlocking the barrier mutex (last-lease destruction also takes it).
    auto closing=std::move(impl_);
    const std::lock_guard lock(closing->freeze->impl_->mirror->mutex);
#ifdef Q_OS_WIN
    closing->verify();
#else
    fail("Source lineage held-handle contract is Windows-only");
#endif
}
}
