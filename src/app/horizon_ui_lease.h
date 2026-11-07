#pragma once
#include <QDir>
#include <QLockFile>
#include <QString>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace pgn::app {
// A transport listener is not an exclusive ownership primitive on Windows.
// The per-user data file supplies a kernel lock, released on actual process
// death without a stale PID/age guess. It never contains wallet information.
class HorizonUiLease final {
#ifdef Q_OS_WIN
 HANDLE handle_{INVALID_HANDLE_VALUE};OVERLAPPED range_{};
#else
 QLockFile lock_;
#endif
 bool owned_{};QString error_;
public:
 explicit HorizonUiLease(const QString& file)
#ifndef Q_OS_WIN
  :lock_(file)
#endif
 {
  if(!QDir().mkpath(QFileInfo(file).absolutePath())){error_="Cannot open the application ownership directory";return;}
#ifdef Q_OS_WIN
  const auto native=QDir::toNativeSeparators(file);
  handle_=CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(handle_==INVALID_HANDLE_VALUE){error_="Cannot inspect application ownership";return;}
  owned_=LockFileEx(handle_,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&range_);
  if(!owned_&&GetLastError()!=ERROR_LOCK_VIOLATION)error_="Application ownership lock could not be acquired";
#else
  lock_.setStaleLockTime(0);owned_=lock_.tryLock(0);
  if(!owned_&&lock_.error()!=QLockFile::LockFailedError)error_="Application ownership lock could not be acquired";
#endif
 }
 ~HorizonUiLease(){
#ifdef Q_OS_WIN
  if(handle_!=INVALID_HANDLE_VALUE){if(owned_)UnlockFileEx(handle_,0,1,0,&range_);CloseHandle(handle_);}
#endif
 }
 HorizonUiLease(const HorizonUiLease&)=delete;HorizonUiLease& operator=(const HorizonUiLease&)=delete;
 bool owned()const{return owned_;}QString error()const{return error_;}
};
}
