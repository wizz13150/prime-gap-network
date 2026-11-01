#include "engine/cuda/euler96_prefix_seed.cuh"
#include <gmp.h>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>
using pgn_euler96::U96;
namespace p=pgn_euler96_prefix_seed;
using pgn_euler96::equal;
static uint64_t cases=0,fixtureCases=0,prefixCases=0,fallbackCases=0,seedChecks=0,oneHintChecks=0,
  scaleChecks=0,aliases=0,poisons=0,positive=0,seedRepair[3]{},batchRepair[3]{},wrongSeedRejected=0,omissionRejected=0;
static uint32_t oddClasses=0;
static uint64_t omissionWitnessLow=0;
static uint64_t hotSeeds=0,hotResidues=0,hotPredicates=0,hotGuards=0,hotWrongSeedRejected=0,hotOmissionRejected=0;
static uint32_t hotOddMod30=0;
static void require(bool ok,const char* why){if(!ok){std::cerr<<"FAIL "<<why<<" cases="<<cases<<" prefix="<<prefixCases<<"\n";std::exit(1);}}
static void integer(mpz_ptr out,const U96& a){mpz_import(out,3,-1,4,0,0,a.w);}
static U96 words(mpz_srcptr a){require(mpz_sgn(a)>=0&&mpz_sizeinbase(a,2)<=96,"GMP unsigned96 range");
  U96 out{};size_t count=0;mpz_export(out.w,&count,-1,4,0,0,a);return out;}
static uint64_t scalar(mpz_srcptr a){require(mpz_sgn(a)>=0&&mpz_sizeinbase(a,2)<=64,"GMP unsigned64 range");
  uint64_t out=0;size_t count=0;mpz_export(&out,&count,-1,8,0,0,a);return out;}
static unsigned popcount(uint64_t a){unsigned n=0;while(a){n+=unsigned(a&1U);a>>=1;}return n;}
static U96 poisonedSuffix(U96 state,const U96& n,uint64_t suffix,int firstBit){
  const uint32_t inv=pgn_euler96::inverse(n.w[0]);
  for(int bit=firstBit;bit>=0;--bit){state=pgn_euler96::multiply(state,state,n,inv);
    if((suffix>>bit)&1U)state=pgn_euler96::twice(state,n);}
  return state;
}
struct Oracle {
  mpz_t n,r,e,two,ordinary,mont,seed,one,a,b,q,actual,expected;
  U96 nw{};uint64_t exactBatch=0;
  Oracle(){mpz_inits(n,r,e,two,ordinary,mont,seed,one,a,b,q,actual,expected,nullptr);
    mpz_set_ui(r,1);mpz_mul_2exp(r,r,96);mpz_set_ui(two,2);}
  ~Oracle(){mpz_clears(n,r,e,two,ordinary,mont,seed,one,a,b,q,actual,expected,nullptr);}
  Oracle(const Oracle&)=delete;Oracle& operator=(const Oracle&)=delete;
  void verify(U96 output,mpz_srcptr wanted,const char* label){integer(actual,output);
    require(mpz_cmp(actual,wanted)==0&&mpz_cmp(actual,n)<0,label);}
  bool check(uint64_t low,uint64_t high,bool hasFixture=false,uint64_t fixtureVerdict=0){
    const uint64_t input[2]={low,high};mpz_import(n,2,-1,8,0,0,input);nw=words(n);
    require((low&1U)&&high>0&&high<=UINT32_MAX,"closed admitted reference domain");
    mpz_sub_ui(e,n,1);mpz_fdiv_q_2exp(e,e,1);mpz_powm(ordinary,two,e,n);
    const int sign=mpz_jacobi(two,n);require(sign==1||sign==-1,"GMP Jacobi sign");
    if(sign==1)mpz_set_ui(expected,1);else mpz_sub_ui(expected,n,1);
    const bool wanted=mpz_cmp(ordinary,expected)==0;
    if(hasFixture){require(fixtureVerdict<=1&&wanted==bool(fixtureVerdict),"inherited fixture verified independently against GMP");++fixtureCases;}
    require(p::test(low,high)==wanted,"candidate Euler/Jacobi GMP predicate");
    require(p::testHot(low,high)==wanted,"checked hot caller GMP predicate");++hotPredicates;
    require(p::reference(low,high)==wanted,"additional old exact predicate control");
    ++cases;if(wanted)++positive;
    if(high!=10){++fallbackCases;require(p::test(low,high,UINT64_MAX)==wanted,"other-high delegation ignores unsafe batch hint");
      require(p::testHot(low,high,UINT64_MAX)==wanted,"checked hot other-domain fallback");++hotPredicates;return wanted;}
    ++prefixCases;oddClasses|=1U<<unsigned(low&15U);
    hotOddMod30|=1U<<unsigned(mpz_fdiv_ui(n,30));
    mpz_fdiv_q_2exp(actual,e,63);require(mpz_cmp_ui(actual,10)==0&&mpz_sizeinbase(e,2)==67,"independent exponent1010 prefix");
    mpz_mod(one,r,n);mpz_mul_2exp(seed,r,10);mpz_mod(seed,seed,n);
    mpz_mul(mont,ordinary,r);mpz_mod(mont,mont,n);mpz_fdiv_q(q,r,n);exactBatch=scalar(q);
    const U96 oldOne=pgn_euler96::montgomeryOne(nw),hotSeed=p::seedAdmittedHigh10(oldOne,nw);
    verify(hotSeed,seed,"actual admitted ten-double seed against GMP");++hotSeeds;
    U96 hotOut{};const bool hotMatch=p::evaluateAdmittedHigh10(low,&hotOut);
    require(hotMatch==wanted,"actual admitted predicate GMP");verify(hotOut,mont,"actual admitted final residue GMP");++hotResidues;
    bool checkedHotMatch=!wanted;
    require(p::evaluateHot(low,high,checkedHotMatch,&hotOut)==p::Status::ok&&checkedHotMatch==wanted,"checked hot evaluated admission");
    verify(hotOut,mont,"checked hot residue GMP");++hotResidues;
    U96 out{};p::Audit audit{};bool matches=!wanted;
    require(p::evaluate(low,high,matches,&out,0,&audit)==p::Status::ok&&matches==wanted,"checked Euler status");
    verify(out,mont,"final Montgomery residue independently powm/Rmod verified");
    verify(audit.seed,seed,"startup witness1024R GMP");verify(audit.one,one,"unchanged R96 unity GMP");
    require(audit.squarings==63&&audit.bit_doublings==popcount((low-1U)>>1),"exact suffix operation counts");
    require(audit.seed_corrections<=2&&!audit.seed_fallback&&audit.seed_hint_available,"finite normal float hints corrected within budget");
    ++seedRepair[audit.seed_corrections];++seedChecks;
    require(p::evaluate(low,high,matches,nullptr)==p::Status::ok&&matches==wanted,"optional residue omitted");
    for(uint64_t hint:std::array<uint64_t,7>{{exactBatch,exactBatch-1,exactBatch+1,1,1ULL<<32,UINT64_MAX,0}}){
      U96 hotHintResidue{};require(p::evaluateAdmittedHigh10(low,&hotHintResidue,hint)==wanted,"hot ignores normal/poison batch hints");
      verify(hotHintResidue,mont,"hot ignored hint final residue GMP");++hotResidues;
      require(p::testAdmittedHigh10(low,hint)==wanted&&p::testHot(low,high,hint)==wanted,"public hot no-residue/hint entry parity");hotPredicates+=2;
      p::Audit details{};U96 s{};require(p::seed(nw,s,hint,&details)==p::Status::ok,"bounded batch hint or exact-one fallback");
      verify(s,seed,"seed with batch hint GMP");verify(details.one,one,"hinted unity GMP");
      require(details.batch_corrections<=2,"no bad-hint unbounded work");
      require(p::test(low,high,hint)==wanted,"batch-hint predicate GMP");++seedChecks;
    }
    for(int delta=-3;delta<=3;++delta){
      const uint64_t hint=uint64_t(int64_t(exactBatch)+delta);U96 target{{0xdeadbeefU,0xabcdef01U,0x12345678U}},before=target;
      uint32_t corrections=999;const auto status=p::checkedOneHint(nw,hint,target,&corrections);
      require(corrections<=2,"R hint fixed correction bound");
      if(delta>=-2&&delta<=2){require(status==p::Status::ok,"near R hints repair exactly");verify(target,one,"bounded R hint GMP");++batchRepair[corrections];}
      else{require(status==p::Status::hint_too_far&&equal(target,before),"distant R hint refuses unchanged");++poisons;}
      ++oneHintChecks;
    }
    std::vector<U96> operands={U96{},{{1,0,0}},words(one)};
    mpz_sub_ui(a,n,1);operands.push_back(words(a));mpz_sub_ui(a,n,2);operands.push_back(words(a));
    operands.push_back(U96{{UINT32_MAX,UINT32_MAX,9}});
    for(U96 operand:operands){
      integer(a,operand);require(mpz_cmp(a,n)<0,"normalized structured scale operand");
      mpz_mul_2exp(b,a,10);mpz_fdiv_q(q,b,n);const uint64_t quotient=scalar(q);mpz_mod(expected,b,n);
      for(int delta=-3;delta<=3;++delta){
        const int64_t proposed=int64_t(quotient)+delta;const uint64_t hint=uint64_t(proposed);
        U96 scaled{{0xdeadbeefU,0xabcdef01U,0x12345678U}},before=scaled;uint32_t corrections=999;
        const auto status=p::checkedScale1024(operand,nw,hint,scaled,&corrections);
        if(proposed<0||proposed>1024){require(status==p::Status::hint_out_of_range&&equal(scaled,before),"out-of-range scale hints refuse unchanged");++poisons;continue;}
        if(delta<-2||delta>2){require(status==p::Status::hint_too_far&&corrections<=2&&equal(scaled,before),"three-away scale hint refuses bounded unchanged");++poisons;continue;}
        require(status==p::Status::ok,"near scale hints repair");
        verify(scaled,expected,"exact shift/remainder GMP");require(corrections<=2,"scale correction bound");++scaleChecks;
        U96 alias=operand;require(p::checkedScale1024(alias,nw,hint,alias)==p::Status::ok&&equal(alias,scaled),"scale operand/output alias");++aliases;
        alias=nw;require(p::checkedScale1024(operand,alias,hint,alias)==p::Status::ok&&equal(alias,scaled),"scale modulus/output alias");++aliases;
      }
    }
    U96 modulusAlias=nw;require(p::seed(modulusAlias,modulusAlias,exactBatch)==p::Status::ok,"seed modulus/output alias");
    verify(modulusAlias,seed,"aliased seed GMP");++aliases;
    // Acceptance uses seed AND final-residue relation, not mostly-false bool
    // agreement. A corrupted seed always differs from the GMP prefix witness.
    mpz_add_ui(a,seed,1);mpz_mod(a,a,n);const U96 badSeed=words(a);require(!equal(badSeed,words(seed)),"wrong seed fails startup oracle");++wrongSeedRejected;
    const U96 wrongFinal=poisonedSuffix(badSeed,nw,(low-1U)>>1,62);
    const U96 omitted=poisonedSuffix(audit.seed,nw,(low-1U)>>1,61); // omit required bit62, including its square
    integer(actual,wrongFinal);if(mpz_cmp(actual,mont)!=0)++poisons;
    integer(actual,omitted);if(mpz_cmp(actual,mont)!=0){++omissionRejected;if(!omissionWitnessLow)omissionWitnessLow=low;}
    // Exercise poisons from the actual new initializer. A fake unity is
    // outside its admitted contract and must fail independent seed acceptance.
    mpz_add_ui(a,one,1);mpz_mod(a,a,n);const U96 badHotSeed=p::seedAdmittedHigh10(words(a),nw);
    integer(actual,badHotSeed);require(mpz_cmp(actual,seed)!=0,"fake unity cannot pass hot seed oracle");++hotWrongSeedRejected;
    const U96 hotOmitted=poisonedSuffix(hotSeed,nw,(low-1U)>>1,61);
    integer(actual,hotOmitted);if(mpz_cmp(actual,mont)!=0)++hotOmissionRejected;
    return wanted;
  }
};
static uint32_t le32(const unsigned char* p){uint32_t value=0;for(unsigned i=0;i<4;++i)value|=uint32_t(p[i])<<(8*i);return value;}
static uint64_t le64(const unsigned char* p){uint64_t value=0;for(unsigned i=0;i<8;++i)value|=uint64_t(p[i])<<(8*i);return value;}
static void guardTests(){
  const U96 sentinel{{0xdeadbeefU,0xabcdef01U,0x12345678U}},good{{3,0,10}},one{{1,0,0}};
  auto scaleError=[&](U96 a,U96 n,uint64_t hint,p::Status status){U96 out=sentinel;
    require(p::checkedScale1024(a,n,hint,out)==status&&equal(out,sentinel),"scale poison unchanged");++poisons;
    out=a;require(p::checkedScale1024(out,n,hint,out)==status&&equal(out,a),"scale error operand alias unchanged");++poisons;
    out=n;require(p::checkedScale1024(a,out,hint,out)==status&&equal(out,n),"scale error modulus alias unchanged");++poisons;
  };
  for(U96 bad:std::array<U96,4>{{{{3,0,9}},{{3,0,11}},{{2,0,10}},U96{}}}){
    scaleError(one,bad,0,p::Status::unsupported_modulus);U96 out=sentinel;
    require(p::seed(bad,out)==p::Status::unsupported_modulus&&equal(out,sentinel),"seed unsupported unchanged");++poisons;
  }
  scaleError(good,good,0,p::Status::unnormalized_operand);
  scaleError(U96{{UINT32_MAX,UINT32_MAX,UINT32_MAX}},good,0,p::Status::unnormalized_operand);
  scaleError(one,good,1025,p::Status::hint_out_of_range);scaleError(one,good,UINT64_MAX,p::Status::hint_out_of_range);
  U96 extreme=good;pgn_euler96::subtract(extreme,one);scaleError(extreme,good,0,p::Status::hint_too_far);
  for(float hint:std::array<float,5>{{-1.0f,1025.0f,std::numeric_limits<float>::infinity(),
      -std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}}){
    U96 out=sentinel;require(p::checkedScale1024Float(one,good,hint,out)==p::Status::hint_out_of_range&&equal(out,sentinel),"unsafe float conversion poison refused");++poisons;
  }
  for(uint64_t hint:std::array<uint64_t,4>{{0,1ULL<<32|1,UINT64_MAX,1}}){
    U96 out=sentinel;uint32_t steps=0;require(p::checkedOneHint(good,hint,out,&steps)!=p::Status::ok&&equal(out,sentinel)&&steps<=2,"batch poison refuses bounded unchanged");++poisons;
  }
  for(auto bad:std::array<std::array<uint64_t,2>,6>{{{{2,10}},{{0,10}},{{3,9}},{{3,11}},{{3,0}},{{3,10ULL+(1ULL<<32)}}}}){
    for(bool before:std::array<bool,2>{{false,true}}){bool match=before;U96 out=sentinel;p::Audit audit{};audit.squarings=777;
      require(p::evaluate(bad[0],bad[1],match,&out,UINT64_MAX,&audit)==p::Status::unsupported_modulus&&match==before&&
        equal(out,sentinel)&&audit.squarings==777,"unsupported evaluate preserves bool/residue/audit");++poisons;
      require(p::test(bad[0],bad[1],UINT64_MAX)==p::reference(bad[0],bad[1]),"public outside-domain exact fallback, no masking");
      bool hotMatch=before;U96 hotOut=sentinel;
      require(p::evaluateHot(bad[0],bad[1],hotMatch,&hotOut,UINT64_MAX)==p::Status::unsupported_modulus&&hotMatch==before&&equal(hotOut,sentinel),
        "checked hot boundary refuses unchanged, not admitted composite");++hotGuards;
      require(p::testHot(bad[0],bad[1],UINT64_MAX)==p::reference(bad[0],bad[1]),"hot boundary preserves old fallback domain");++hotGuards;
    }
  }
  U96 out=sentinel;
  require(p::checkedScale1024(one,good,0,out,&out.w[0])==p::Status::invariant_violation&&equal(out,sentinel),"diagnostic/output alias refused unchanged");++poisons;
  U96 n=good;require(p::checkedOneHint(n,1,n,&n.w[0])==p::Status::invariant_violation&&equal(n,good),"R diagnostic/modulus alias refused unchanged");++poisons;
  p::Audit audit{};audit.one=sentinel;
  require(p::seed(good,audit.one,0,&audit)==p::Status::invariant_violation&&equal(audit.one,sentinel),"seed/audit alias refused");++poisons;
  audit.seed=sentinel;bool matches=true;
  require(p::evaluate(3,10,matches,&audit.seed,0,&audit)==p::Status::invariant_violation&&matches&&equal(audit.seed,sentinel),"residue/audit alias refused");++poisons;
  audit.batch_fallback=true;
  require(p::evaluate(3,10,audit.batch_fallback,nullptr,0,&audit)==p::Status::invariant_violation&&audit.batch_fallback,"predicate/audit alias refused");++poisons;
}
int main(int argc,char** argv){
  require(argc==2,"one original immutable fixture path required");const std::filesystem::path fixture=argv[1];
  const auto fileBytes=std::filesystem::file_size(fixture);require(fileBytes==289736,"stat-bound inherited fixture size");
  std::ifstream input(fixture,std::ios::binary);std::array<unsigned char,8> header{};input.read(reinterpret_cast<char*>(header.data()),8);
  require(bool(input)&&le32(header.data())==0x454a3936U&&le32(header.data()+4)==12072,"fixture version/count");Oracle oracle;
  for(unsigned i=0;i<12072;++i){std::array<unsigned char,24> record{};input.read(reinterpret_cast<char*>(record.data()),24);
    require(bool(input),"fixture complete record");oracle.check(le64(record.data()),le64(record.data()+8),true,le64(record.data()+16));}
  char extra;require(!input.get(extra)&&input.eof()&&std::filesystem::file_size(fixture)==fileBytes,"fixture exact tail and unchanged size");
  // Closed arithmetic boundary cases only; no prime generation, no new search
  // or source-performance workloads. Existing12072-case fixture is inherited.
  std::vector<uint64_t> lows={1,3,UINT64_MAX,UINT64_MAX-2,UINT32_MAX,(1ULL<<32)+1,(1ULL<<63)-1,(1ULL<<63)+1};
  for(unsigned cls=1;cls<16;cls+=2){lows.push_back(cls);lows.push_back((UINT64_MAX&~15ULL)|cls);}
  for(uint64_t high:std::array<uint64_t,3>{{9,10,11}})for(uint64_t low:lows)oracle.check(low,high);
  guardTests();require(fixtureCases==12072&&oddClasses==0xaaaaU&&wrongSeedRejected==prefixCases&&omissionRejected>0,"scope and poison coverage");
  require(hotSeeds==prefixCases&&hotWrongSeedRejected==prefixCases&&hotOmissionRejected>0&&hotOddMod30==0x2aaaaaaaU,"actual hot entry and all odd-mod30 poison coverage");
  std::cout<<"PREFIX_SEED_GMP_PASS fixture="<<fixtureCases<<" cases="<<cases<<" high10="<<prefixCases<<" fallback="<<fallbackCases
    <<" positives="<<positive<<" seed_checks="<<seedChecks<<" R_hint_checks="<<oneHintChecks<<" scale_checks="<<scaleChecks
    <<" aliases="<<aliases<<" poison_checks="<<poisons<<" wrong_seed_rejected="<<wrongSeedRejected<<" omitted_bit_rejected="<<omissionRejected
    <<" omitted_bit_first_low="<<omissionWitnessLow<<" odd_mod16_mask="<<oddClasses<<"\n";
  std::cout<<"BOUNDED_CORRECTIONS seed="<<seedRepair[0]<<","<<seedRepair[1]<<","<<seedRepair[2]
    <<" batch="<<batchRepair[0]<<","<<batchRepair[1]<<","<<batchRepair[2]<<" squares_per_high10=63 errors=0\n";
  std::cout<<"ADMITTED_HOT_GMP seeds="<<hotSeeds<<" residues="<<hotResidues<<" predicates="<<hotPredicates<<" checked_boundary_guards="<<hotGuards
    <<" wrong_seed_rejected="<<hotWrongSeedRejected<<" omitted_bit_rejected="<<hotOmissionRejected<<" odd_mod30_mask="<<hotOddMod30
    <<" initializer_old_doubles=10 suffix_squares=63 errors=0\n";
#ifdef PGN_EULER96_BATCH_Q
  std::cout<<"BUILD_BATCH_Q=1\n";
#else
  std::cout<<"BUILD_BATCH_Q=0\n";
#endif
}
