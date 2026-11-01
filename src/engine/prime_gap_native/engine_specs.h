#pragma once
#include <cstdint>
namespace scout {
struct EngineSpec{unsigned bits;uint64_t highFirst,highEnd;unsigned automaticGap,stateBytes;const char*name;};
inline constexpr EngineSpec Engines[]={
 {67,4ULL,8ULL,954,8,"euler67"},
 {68,8ULL,16ULL,960,8,"euler68"},
 {69,16ULL,32ULL,982,4,"euler69"},
 {70,32ULL,64ULL,994,4,"euler70"},
 {71,64ULL,128ULL,1008,4,"euler71"},
 {72,128ULL,256ULL,1022,4,"euler72"},
 {73,256ULL,512ULL,1036,4,"euler73"},
 {74,512ULL,1024ULL,1050,4,"euler74"},
 {75,1024ULL,2048ULL,1062,4,"euler75"},
 {76,2048ULL,4096ULL,1076,8,"euler76"},
 {77,4096ULL,8192ULL,1090,8,"euler77"},
 {78,8192ULL,16384ULL,1104,8,"euler78"},
 {79,16384ULL,32768ULL,1116,8,"euler79"},
 {80,32768ULL,65536ULL,1130,8,"euler80"},
 {81,65536ULL,131072ULL,1144,8,"euler81"},
 {82,131072ULL,262144ULL,1158,8,"euler82"},
 {83,262144ULL,524288ULL,1170,8,"euler83"},
 {84,524288ULL,1048576ULL,1184,8,"euler84"},
 {85,1048576ULL,2097152ULL,1198,8,"euler85"},
 {86,2097152ULL,4194304ULL,1212,8,"euler86"},
 {87,4194304ULL,8388608ULL,1224,8,"euler87"},
 {88,8388608ULL,16777216ULL,1238,8,"euler88"},
 {89,16777216ULL,33554432ULL,1252,8,"euler89"},
 {90,33554432ULL,67108864ULL,1266,8,"euler90"},
 {91,67108864ULL,134217728ULL,1278,8,"euler91"},
 {92,134217728ULL,268435456ULL,1292,8,"euler92"},
 {93,268435456ULL,536870912ULL,1306,8,"euler93"},
 {94,536870912ULL,1073741824ULL,1318,8,"euler94"},
 {95,1073741824ULL,2147483648ULL,1332,4,"euler95"},
};
inline const EngineSpec*engineForHigh(uint64_t high){for(const auto&e:Engines)if(high>=e.highFirst&&high<e.highEnd)return &e;return nullptr;}
}
