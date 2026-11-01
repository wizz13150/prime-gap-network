#ifdef PGN_EULER68
#include "euler68.cuh"
namespace arithmetic = pgn_euler68;
#else
#include "euler96.cuh"
namespace arithmetic = pgn_euler96;
#endif
#include <cuda_runtime.h>
#include <cstdio>
#include <cstring>
#include <vector>
struct Vector {uint64_t low,high,expected;};
__global__ void validateEuler96(const Vector* inputs,unsigned char* results,unsigned count){
 const auto i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=count)return;
#ifdef PGN_EULER96_WINDOW4
 const pgn_euler96::U96 modulus{{uint32_t(inputs[i].low),uint32_t(inputs[i].low>>32),uint32_t(inputs[i].high)}};
 const float reciprocal=arithmetic::reciprocalHint(modulus);
 const pgn_euler96::U96 unit{{1,0,0}},zero{};
 auto maximum=modulus;arithmetic::subtract(maximum,unit);
 const pgn_euler96::U96 probes[3]={zero,maximum,arithmetic::montgomeryOne(modulus)};
 for(int probe=0;probe<3;++probe){
  auto reference=probes[probe];
  for(unsigned shift=0;shift<=16;++shift){
   if(!arithmetic::equal(reference,arithmetic::timesPowerOfTwo(probes[probe],modulus,shift,reciprocal))){results[i]=3;return;}
   reference=arithmetic::twice(reference,modulus);
  }
 }
#endif
#ifdef PGN_EULER96_BATCH_Q
 const uint64_t nwords=i%3==0?1:i%3==1?65ULL*(1ULL<<22):97ULL*(1ULL<<22);
 const uint64_t span=30ULL*nwords*8ULL-1,low=inputs[i].low;
 const auto hint=arithmetic::batchQuotient(low-(low<span?low:span),inputs[i].high,nwords);
 const pgn_euler96::U96 n{{uint32_t(low),uint32_t(low>>32),uint32_t(inputs[i].high)}};
 if(!hint||!arithmetic::equal(arithmetic::montgomeryOne(n),arithmetic::montgomeryOneHinted(n,hint))){results[i]=3;return;}
 results[i]=arithmetic::test(low,inputs[i].high,hint)?1:0;
#else
 results[i]=arithmetic::test(inputs[i].low,inputs[i].high)?1:0;
#endif
}
int main(int argc,char** argv){
 if(argc!=3)return 1;int count=0;cudaDeviceProp properties{};
 if(cudaGetDeviceCount(&count)!=cudaSuccess||count!=1||cudaGetDeviceProperties(&properties,0)!=cudaSuccess)return 2;
 char uuid[41]{};std::strcpy(uuid,"GPU-");int at=4;
 for(int i=0;i<16;++i){if(i==4||i==6||i==8||i==10)uuid[at++]='-';std::sprintf(uuid+at,"%02x",static_cast<unsigned char>(properties.uuid.bytes[i]));at+=2;}
 if(std::strcmp(uuid,argv[2])!=0){std::printf("UUID REJECTED; no allocation/kernel\n");return 3;}
 FILE* fixture=std::fopen(argv[1],"rb");if(!fixture)return 4;
 uint32_t header[2]{};if(std::fread(header,sizeof(header),1,fixture)!=1||header[0]!=0x454a3936U||header[1]<1||header[1]>16384){std::fclose(fixture);return 5;}
 std::vector<Vector> inputs(header[1]);
 if(std::fread(inputs.data(),sizeof(Vector),inputs.size(),fixture)!=inputs.size()||std::fgetc(fixture)!=EOF){std::fclose(fixture);return 6;}std::fclose(fixture);
 for(const auto& item:inputs)if(item.expected>1||!(item.low&1)||item.high==0||item.high>UINT32_MAX)return 7;
 Vector* deviceInputs=nullptr;unsigned char* deviceResults=nullptr;
 if(cudaMalloc(&deviceInputs,inputs.size()*sizeof(Vector))!=cudaSuccess)return 8;
 if(cudaMalloc(&deviceResults,inputs.size())!=cudaSuccess){cudaFree(deviceInputs);return 9;}
 bool okay=cudaMemcpy(deviceInputs,inputs.data(),inputs.size()*sizeof(Vector),cudaMemcpyHostToDevice)==cudaSuccess;
 if(okay){validateEuler96<<<(header[1]+127)/128,128>>>(deviceInputs,deviceResults,header[1]);okay=cudaGetLastError()==cudaSuccess;}
 std::vector<unsigned char> results(inputs.size());
 if(okay)okay=cudaMemcpy(results.data(),deviceResults,results.size(),cudaMemcpyDeviceToHost)==cudaSuccess;
 cudaFree(deviceInputs);cudaFree(deviceResults);if(!okay)return 10;
 unsigned mismatches=0;for(size_t i=0;i<inputs.size();++i)if(results[i]!=inputs[i].expected){
  if(mismatches<4)std::printf("Mismatch index=%llu low=%llu high=%llu expected=%llu actual=%u\n",static_cast<unsigned long long>(i),static_cast<unsigned long long>(inputs[i].low),static_cast<unsigned long long>(inputs[i].high),static_cast<unsigned long long>(inputs[i].expected),unsigned(results[i]));++mismatches;
 }
 std::printf("{\"uuid\":\"%s\",\"cases\":%u,\"mismatches\":%u,\"source\":\"EXACT_GMP_EULER_ORACLE_NOT_PRIME_PROOF\"}\n",uuid,header[1],mismatches);
 return mismatches?11:0;
}
