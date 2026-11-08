#pragma once
#include <QCryptographicHash>
#include <QIODevice>
#include <optional>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <bcrypt.h>
#include <array>
#include <vector>
#endif

namespace pgn::update {
#ifdef Q_OS_WIN
namespace digest_detail {
// Load only the Windows system implementation. Each call owns its hash state;
// the immutable algorithm provider is shared across background inventory jobs.
struct WindowsSha256 {
 HMODULE module{};BCRYPT_ALG_HANDLE algorithm{};DWORD objectLength{};bool ready{};
#define PGN_CNG_FUNCTION(name) decltype(&::name) name{}
 PGN_CNG_FUNCTION(BCryptOpenAlgorithmProvider);PGN_CNG_FUNCTION(BCryptGetProperty);
 PGN_CNG_FUNCTION(BCryptCreateHash);PGN_CNG_FUNCTION(BCryptHashData);
 PGN_CNG_FUNCTION(BCryptFinishHash);PGN_CNG_FUNCTION(BCryptDestroyHash);
 PGN_CNG_FUNCTION(BCryptCloseAlgorithmProvider);
#undef PGN_CNG_FUNCTION
 WindowsSha256(){
  wchar_t directory[MAX_PATH+1]{};const auto size=GetSystemDirectoryW(directory,MAX_PATH);
  if(!size||size>=MAX_PATH)return;
  const auto path=QString::fromWCharArray(directory,int(size))+"/bcrypt.dll";
  module=LoadLibraryExW(reinterpret_cast<LPCWSTR>(path.utf16()),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
  if(!module)return;
#define PGN_CNG_LOAD(name) name=reinterpret_cast<decltype(name)>(GetProcAddress(module,#name));if(!name)return
  PGN_CNG_LOAD(BCryptOpenAlgorithmProvider);PGN_CNG_LOAD(BCryptGetProperty);
  PGN_CNG_LOAD(BCryptCreateHash);PGN_CNG_LOAD(BCryptHashData);
  PGN_CNG_LOAD(BCryptFinishHash);PGN_CNG_LOAD(BCryptDestroyHash);PGN_CNG_LOAD(BCryptCloseAlgorithmProvider);
#undef PGN_CNG_LOAD
  if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return;
  DWORD received{};
  ready=BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectLength),sizeof(objectLength),&received,0)>=0
    &&received==sizeof(objectLength)&&objectLength>0&&objectLength<=1024*1024;
 }
 ~WindowsSha256(){if(algorithm&&BCryptCloseAlgorithmProvider)BCryptCloseAlgorithmProvider(algorithm,0);if(module)FreeLibrary(module);}
};
}
#endif
// Exact SHA-256 over the remaining readable bytes. I/O/provider failure never
// produces an accepted digest; memory stays bounded independently of file size.
inline std::optional<QByteArray> codeFileSha256(QIODevice& file){
 if(!file.isOpen()||!file.isReadable())return {};
#ifdef Q_OS_WIN
 static const digest_detail::WindowsSha256 api;
 if(!api.ready)return {};
 std::vector<UCHAR> object(api.objectLength);BCRYPT_HASH_HANDLE handle{};
 if(api.BCryptCreateHash(api.algorithm,&handle,object.data(),api.objectLength,nullptr,0,0)<0)return {};
 struct Guard{BCRYPT_HASH_HANDLE handle;decltype(api.BCryptDestroyHash) destroy;~Guard(){destroy(handle);}} guard{handle,api.BCryptDestroyHash};
 QByteArray buffer(1024*1024,'\0');
 for(;;){const auto count=file.read(buffer.data(),buffer.size());if(count<0)return {};if(!count)break;
  if(api.BCryptHashData(handle,reinterpret_cast<PUCHAR>(buffer.data()),ULONG(count),0)<0)return {};
 }
 std::array<UCHAR,32> digest{};
 if(api.BCryptFinishHash(handle,digest.data(),ULONG(digest.size()),0)<0)return {};
 return QByteArray(reinterpret_cast<const char*>(digest.data()),qsizetype(digest.size()));
#else
 QCryptographicHash hash(QCryptographicHash::Sha256);if(!hash.addData(&file))return {};return hash.result();
#endif
}
}
