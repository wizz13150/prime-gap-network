#pragma once
// Private pinned canonical data: runtime hashing replaces repeated construction,
// never mathematical validation of a new caller-supplied progression table.
#include "native_small_factor_sieve.h"
#include "frozen_table_pin.h"
#include <fstream>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#ifdef _MSC_VER
#pragma comment(lib,"bcrypt.lib")
#endif
#endif
namespace pgn_psp_native {
struct FrozenCanonicalTable {
    static SmallFactorTable load(const std::string& path){
        std::ifstream in(path,std::ios::binary|std::ios::ate);
        require(bool(in),"Missing canonical complement table");
        require(in.tellg()==std::streamoff(frozen_pin::Bytes),"Canonical complement table size changed");
        std::vector<unsigned char> bytes(frozen_pin::Bytes);in.seekg(0);
        require(bool(in.read(reinterpret_cast<char*>(bytes.data()),bytes.size())),"Truncated canonical complement table");
#ifdef _WIN32
        struct Provider {BCRYPT_ALG_HANDLE h{};~Provider(){if(h)BCryptCloseAlgorithmProvider(h,0);}} provider;
        require(BCryptOpenAlgorithmProvider(&provider.h,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"Cannot open native SHA256 provider");
        DWORD objectLength{},returned{};
        require(BCryptGetProperty(provider.h,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectLength),sizeof(objectLength),&returned,0)>=0
                &&returned==sizeof(objectLength)&&objectLength<=1048576,"Invalid native SHA256 object size");
        std::vector<unsigned char> object(objectLength);
        struct Hash {BCRYPT_HASH_HANDLE h{};~Hash(){if(h)BCryptDestroyHash(h);}} hash;
        require(BCryptCreateHash(provider.h,&hash.h,object.data(),ULONG(object.size()),nullptr,0,0)>=0,"Cannot create native SHA256 state");
        std::array<unsigned char,32> digest{};
        require(BCryptHashData(hash.h,bytes.data(),ULONG(bytes.size()),0)>=0
                &&BCryptFinishHash(hash.h,digest.data(),ULONG(digest.size()),0)>=0,"Cannot verify canonical complement digest");
        require(digest==frozen_pin::Sha256,"Canonical complement table digest mismatch");
#else
        require(false,"Canonical table loader requires its qualified native SHA256 provider");
#endif
        auto u32=[&](size_t at){uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(bytes[at+i])<<(8*i);return v;};
        auto u64=[&](size_t at){uint64_t v=0;for(unsigned i=0;i<8;++i)v|=uint64_t(bytes[at+i])<<(8*i);return v;};
        require(std::equal(bytes.begin(),bytes.begin()+8,"PGNORD01")&&u32(8)==80000&&u32(12)==10000000&&u32(16)==656742,"Canonical complement table schema mismatch");
        SmallFactorTable table(80000,10000000);table.rows_.reserve(656742);uint32_t previous=80000;
        for(size_t at=20;at<bytes.size();at+=16){
            Progression r{u32(at),u32(at+4),u64(at+8)};
            require(r.prime>previous&&r.prime<=10000000&&(r.prime&1)&&r.order>0&&(r.prime-1)%r.order==0,"Invalid pinned progression row");
            uint64_t step=uint64_t(r.prime)*r.order;if(step&1)step*=2;
            require(r.step==step,"Invalid pinned progression stride");table.rows_.push_back(r);previous=r.prime;
        }
        return table;
    }
};
}
