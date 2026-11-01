// Native112-byte header CUDA bridge candidate. No startup/genesis/consensus
// authority. Explicit RTX3090 only; finite search, or fixed public probes.
// Integrate with the owned GPU controller before any live use. No CPU search.
// SHA256 constants/round primitives are reused from MIT BitCrack6bf8059.
// Keep that upstream sha256.cuh and its LICENSE with any redistributed build.
#include <sha256.cuh>
#include <array>
#include <cstdint>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr unsigned HeaderBytes=112,NonceOffset=82,Limit=1U<<20;
__constant__ unsigned words[32],nativeTarget[8];
__device__ unsigned middle[8];
__device__ void compress(unsigned state[8],const unsigned input[16]){
 unsigned w[16];for(unsigned i=0;i<16;++i)w[i]=input[i];
 unsigned a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
 for(unsigned i=0;i<64;++i){
  if(i>=16)w[i&15]+=s1(w[(i-2)&15])+w[(i-7)&15]+s0(w[(i-15)&15]);
  // Genuine BitCrack round updates d=old_d+T1 and h=T1+T2.
  round(a,b,c,d,e,f,g,h,w[i&15],_K[i]);
  const auto nextA=h,nextE=d;h=g;g=f;f=e;e=nextE;d=c;c=b;b=a;a=nextA;
 }
 state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
}
__global__ void prepare(){for(unsigned i=0;i<8;++i)middle[i]=_IV[i];compress(middle,words);}
__device__ void digest(std::uint64_t nonce,unsigned output[8]){
 unsigned last[16];for(unsigned i=0;i<16;++i)last[i]=words[16+i];
 for(unsigned i=0;i<8;++i){const unsigned at=NonceOffset-64+i,shift=24-8*(at&3);last[at/4]=(last[at/4]&~(255U<<shift))|(unsigned((nonce>>(8*i))&255U)<<shift);}
 for(unsigned i=0;i<8;++i)output[i]=middle[i];compress(output,last);
 unsigned second[16]{};for(unsigned i=0;i<8;++i)second[i]=output[i];second[8]=0x80000000U;second[15]=256;
 for(unsigned i=0;i<8;++i)output[i]=_IV[i];compress(output,second);
}
__device__ unsigned swap32(unsigned x){return (x<<24)|(x>>24)|((x&0xff00U)<<8)|((x>>8)&0xff00U);}
__device__ bool underNativeTarget(const unsigned* digest){
 // Core uint256 stores raw SHA digest bytes, with LITTLE-endian arithmetic.
 // Compare reversed byte order with the explicit big-endian numerical target.
 for(unsigned i=0;i<8;++i){const auto value=swap32(digest[7-i]);if(value<nativeTarget[i])return true;if(value>nativeTarget[i])return false;}return true;
}
__global__ void search(std::uint64_t start,unsigned total,unsigned long long* best,unsigned* count){
 for(unsigned offset=blockIdx.x*blockDim.x+threadIdx.x;offset<total;offset+=gridDim.x*blockDim.x){
  const std::uint64_t nonce=start+offset;unsigned out[8];digest(nonce,out);
  if(underNativeTarget(out)){atomicMin(best,static_cast<unsigned long long>(nonce));atomicAdd(count,1U);}
 }
}
__global__ void samples(const std::uint64_t* nonces,unsigned* output){const unsigned i=threadIdx.x;if(i<16){digest(nonces[i],output+9*i);output[9*i+8]=underNativeTarget(output+9*i);}}
__global__ void witness(const unsigned long long* best,const unsigned* hits,unsigned* output){if(*hits)digest(*best,output);}
void check(cudaError_t status,const char* operation){if(status!=cudaSuccess)throw std::runtime_error(std::string(operation)+": "+cudaGetErrorString(status));}
std::string uuid(const cudaUUID_t& id){char text[64];const auto* b=reinterpret_cast<const unsigned char*>(id.bytes);std::snprintf(text,sizeof(text),"GPU-%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7],b[8],b[9],b[10],b[11],b[12],b[13],b[14],b[15]);return text;}
unsigned nibble(char c){if(c>='0'&&c<='9')return unsigned(c-'0');if(c>='a'&&c<='f')return unsigned(c-'a'+10);throw std::runtime_error("Canonical lowercase header hex required");}
void printHash(const unsigned* value){for(unsigned i=0;i<8;++i)std::printf("%08x",value[i]);}
struct DeviceBuffer{void* value{};explicit DeviceBuffer(std::size_t bytes){check(cudaMalloc(&value,bytes),"cudaMalloc own buffer");}~DeviceBuffer(){if(value)cudaFree(value);}DeviceBuffer(const DeviceBuffer&)=delete;};
}
int main(int argc,char** argv){try{
 if(argc!=5&&argc!=7)throw std::runtime_error("Usage: native-bootstrap-gpu UUID HEADER112_HEX TARGET64_HEX probes|search [START_U64 COUNT_1_TO_268435456]");
 std::uint64_t start=0,total=Limit;
 const auto decimal=[](const char* text){const std::string str{text};std::uint64_t value{};const auto parsed=std::from_chars(str.data(),str.data()+str.size(),value);
  if(str.empty()||(str.size()>1&&str[0]=='0')||parsed.ec!=std::errc{}||parsed.ptr!=str.data()+str.size())throw std::runtime_error("Canonical unsigned64 decimal batch value required");return value;};
 if(argc==7){if(std::string{argv[4]}!="search")throw std::runtime_error("Batch offsets apply only to explicit finite search");start=decimal(argv[5]);total=decimal(argv[6]);}
 if(!total||total>(1U<<28)||start>std::numeric_limits<std::uint64_t>::max()-(total-1))throw std::runtime_error("Invalid or wrapping bounded nonce batch");
 const std::string expected=argv[1],encoded=argv[2],target=argv[3],mode=argv[4];const bool searching=mode=="search";if(!searching&&mode!="probes")throw std::runtime_error("Explicit probes or finite search mode required");if(expected!="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac")throw std::runtime_error("Exact approved RTX3090 UUID required");if(target.size()!=64)throw std::runtime_error("Exact 256bit numerical target required");std::array<unsigned,8> hostTarget{};bool nonzero{};for(unsigned i=0;i<64;++i){const auto n=nibble(target[i]);hostTarget[i/8]=(hostTarget[i/8]<<4)|n;nonzero=nonzero||n;}if(!nonzero)throw std::runtime_error("Zero target refused");if(encoded.size()!=2*HeaderBytes)throw std::runtime_error("Exactly112 native header bytes required");
 std::array<unsigned char,128> padded{};for(unsigned i=0;i<HeaderBytes;++i)padded[i]=static_cast<unsigned char>(16*nibble(encoded[2*i])+nibble(encoded[2*i+1]));
 // Native v2606: version4 + parent32 + Merkle32 + time8 + bits4 + nonce32.
 // NativeBootstrapNonce(counter) = (counter<<16)+2; all high nonce bits zero.
 if(padded[80]!=2||padded[81]!=0)throw std::runtime_error("Native nonce-family selector must equal2");
 for(unsigned i=90;i<112;++i)if(padded[i])throw std::runtime_error("Native high nonce bits must remain zero");
 for(unsigned i=NonceOffset;i<90;++i)if(padded[i])throw std::runtime_error("Input nonce must be zero template, never CPU-searched");
 padded[HeaderBytes]=128;const std::uint64_t bits=HeaderBytes*8;for(unsigned i=0;i<8;++i)padded[127-i]=static_cast<unsigned char>(bits>>(8*i));
 std::array<unsigned,32> hostWords{};for(unsigned i=0;i<32;++i)for(unsigned j=0;j<4;++j)hostWords[i]=(hostWords[i]<<8)|padded[4*i+j];
 int count{},selected=-1;check(cudaGetDeviceCount(&count),"device inventory");cudaDeviceProp chosen{};
 for(int i=0;i<count;++i){cudaDeviceProp properties{};check(cudaGetDeviceProperties(&properties,i),"device identity");if(uuid(properties.uuid)==expected){selected=i;chosen=properties;}}
 if(selected<0||std::string(chosen.name).find("RTX 3090")==std::string::npos)throw std::runtime_error("Exact RTX3090 UUID/name required; no fallback");
 check(cudaSetDevice(selected),"select explicit3090");check(cudaMemcpyToSymbol(words,hostWords.data(),sizeof(hostWords)),"public template upload");check(cudaMemcpyToSymbol(nativeTarget,hostTarget.data(),sizeof(hostTarget)),"explicit numerical target upload");
 DeviceBuffer best(sizeof(unsigned long long)),hits(sizeof(unsigned)),one(8*sizeof(unsigned)),nonces(16*sizeof(std::uint64_t)),outputs(16*9*sizeof(unsigned));
 check(cudaMemset(best.value,255,sizeof(unsigned long long)),"own best init");check(cudaMemset(hits.value,0,sizeof(unsigned)),"own count init");
 const std::array<std::uint64_t,16> probes{0,1,255,256,65535,65536,Limit-1,Limit,0xffffffffULL,0x100000000ULL,0x100000001ULL,0x7fffffffffffffffULL,0x8000000000000000ULL,0xffffffffffffff00ULL,0xfffffffffffffffeULL,0xffffffffffffffffULL};
 check(cudaMemcpy(nonces.value,probes.data(),sizeof(probes),cudaMemcpyHostToDevice),"public probe upload");
 prepare<<<1,1>>>();samples<<<1,16>>>(static_cast<const std::uint64_t*>(nonces.value),static_cast<unsigned*>(outputs.value));
 // Retain a single CUDA context, but synchronize bounded <=2^24 kernels.
 // No giant watchdog-sensitive kernel and no host-side nonce hashing.
 if(searching)for(std::uint64_t offset=0;offset<total;offset+=(1U<<24)){
  const auto quantum=static_cast<unsigned>((total-offset)<(1U<<24)?(total-offset):(1U<<24));
  search<<<512,256>>>(start+offset,quantum,static_cast<unsigned long long*>(best.value),static_cast<unsigned*>(hits.value));
  check(cudaGetLastError(),"finite chunk launch");check(cudaDeviceSynchronize(),"finite chunk complete");
 }
 if(searching)witness<<<1,1>>>(static_cast<unsigned long long*>(best.value),static_cast<unsigned*>(hits.value),static_cast<unsigned*>(one.value));
 check(cudaGetLastError(),"closed kernels launch");check(cudaDeviceSynchronize(),"closed kernels complete");
 unsigned long long found{};unsigned number{},result[8]{},probeHashes[16*9]{};check(cudaMemcpy(&found,best.value,sizeof(found),cudaMemcpyDeviceToHost),"own best read");check(cudaMemcpy(&number,hits.value,sizeof(number),cudaMemcpyDeviceToHost),"own hit read");
 check(cudaMemcpy(probeHashes,outputs.value,sizeof(probeHashes),cudaMemcpyDeviceToHost),"public samples read");if(number)check(cudaMemcpy(result,one.value,sizeof(result),cudaMemcpyDeviceToHost),"public witness read");
 std::printf("{\"schema\":\"PGN_NATIVE112_GPU_CANDIDATE1\",\"device_uuid\":\"%s\",\"nonce_limit\":%u,\"valid_count\":%u,\"nonce\":\"%llu\",\"sha256d\":\"",expected.c_str(),searching?static_cast<unsigned>(total):0U,number,found);if(number)printHash(result);std::printf("\",\"nonce_start\":\"%llu\",\"nonce_count\":%u,\"batch_complete\":%s,\"probes\":[",static_cast<unsigned long long>(start),searching?static_cast<unsigned>(total):0U,searching?"true":"false");
 for(unsigned i=0;i<16;++i){if(i)std::printf(",");std::printf("{\"nonce\":\"%llu\",\"sha256d\":\"",static_cast<unsigned long long>(probes[i]));printHash(probeHashes+9*i);std::printf("\",\"under_target\":%s}",probeHashes[9*i+8]?"true":"false");}std::printf("]}\n");return searching?(number?0:3):0;
 }catch(const std::exception& e){std::fprintf(stderr,"NATIVE112_CANDIDATE_REFUSED %s\n",e.what());return 1;}}
