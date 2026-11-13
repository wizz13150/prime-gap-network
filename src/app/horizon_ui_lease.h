#pragma once
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <sys/file.h>
#include <errno.h>
#else
#include <QLockFile>
#endif
namespace pgn::app {
// A transport listener is not an exclusive ownership primitive on Windows.
// The per-user data file supplies a kernel lock, released on actual process
// death without a stale PID/age guess. It never contains wallet information.
class HorizonUiLease final {
#ifdef Q_OS_WIN
 HANDLE handle_{INVALID_HANDLE_VALUE};OVERLAPPED range_{};
#elif defined(Q_OS_LINUX)
 QFile file_;
#else
 QLockFile lock_;
#endif
 bool owned_{};QString error_;
public:
 explicit HorizonUiLease(const QString& file)
#if defined(Q_OS_LINUX)
  :file_(file)
#elif !defined(Q_OS_WIN)
  :lock_(file)
#endif
 {
  if(!QDir().mkpath(QFileInfo(file).absolutePath())){error_="Cannot open the application ownership directory";return;}
#ifdef Q_OS_WIN
  auto native=QDir::toNativeSeparators(QFileInfo(file).absoluteFilePath());
  if(!native.startsWith("\\\\?\\"))native=native.startsWith("\\\\")?"\\\\?\\UNC\\"+native.mid(2):"\\\\?\\"+native;
  handle_=CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(handle_==INVALID_HANDLE_VALUE){error_=QString("Cannot open the application ownership file (Windows error %1)").arg(GetLastError());return;}
  owned_=LockFileEx(handle_,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&range_);
  if(!owned_&&GetLastError()!=ERROR_LOCK_VIOLATION)error_="Application ownership lock could not be acquired";
#elif defined(Q_OS_LINUX)
  if(!file_.open(QIODevice::ReadWrite)){error_="Cannot inspect application ownership";return;}
  owned_=flock(file_.handle(),LOCK_EX|LOCK_NB)==0;
  if(!owned_&&errno!=EWOULDBLOCK&&errno!=EAGAIN)error_="Application ownership lock could not be acquired";
#else
  lock_.setStaleLockTime(0);owned_=lock_.tryLock(0);
  if(!owned_&&lock_.error()!=QLockFile::LockFailedError)error_="Application ownership lock could not be acquired";
#endif
 }
 ~HorizonUiLease(){
#ifdef Q_OS_WIN
  if(handle_!=INVALID_HANDLE_VALUE){if(owned_)UnlockFileEx(handle_,0,1,0,&range_);CloseHandle(handle_);}
#elif defined(Q_OS_LINUX)
  if(owned_)flock(file_.handle(),LOCK_UN);
#endif
 }
 HorizonUiLease(const HorizonUiLease&)=delete;HorizonUiLease& operator=(const HorizonUiLease&)=delete;
 bool owned()const{return owned_;}QString error()const{return error_;}
};
}
