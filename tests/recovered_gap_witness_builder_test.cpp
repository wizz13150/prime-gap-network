// Public fixed supplied inputs; production producer API, unchanged native consumers. No mining/network.
#include <prime_certificate_builder.h>
#include <certified_gap_builder.h>
#include <certified_gap_encoding.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace pgn::core;
namespace {
unsigned checks{};
void check(bool value, const char* label) {
    ++checks;
    if (!value) throw std::runtime_error(label);
}
constexpr std::array<unsigned,12> bases{2,3,5,7,11,13,17,19,23,29,31,37};
}
int main(int argc, char** argv) {
 try {
    const auto started=std::chrono::steady_clock::now();
    check(argc==1||argc==2,"optional private proof output path only");
    const std::filesystem::path output=argc==2?argv[1]:"";
    if(argc==2)check(!std::filesystem::exists(output),"never overwrite an existing proof");
    const Integer lower("3317044064679887385961813"), upper("3317044064679887385962123");
    const Integer pseudo("3317044064679887385961981"), divisor("2575672364521");
    const Integer child("1658522032339943692981061");
    const PrimeBuildBudget budget{{128,32,32},10000,64};
    const CertifiedGapLimits limits{budget.certificate,1000};
    const CertifiedGapEncodingLimits encoding{{1002,1048576,32,1},limits};
    // GNU factor output is only a hint. Each supplied leaf, exponent, product,
    // divisibility condition and Pocklington witness is checked natively.
    const std::vector<PrimeFactorPower> lowHints{{2,2},{3,1},{103,1},{132408623,1},{Integer("20268261599279"),1}};
    const std::vector<PrimeFactorPower> childHints{{2,2},{5,1},{461,1},{Integer("179883083767889771473"),1}};
    const auto first=buildSuppliedPrimeCertificateFromFactors(lower,lowHints,budget);
    const auto intermediate=buildSuppliedPrimeCertificateFromFactors(child,childHints,budget);
    check(first.verdict==PrimeBuildVerdict::Built&&first.evidence,"lower endpoint certificate");
    check(intermediate.verdict==PrimeBuildVerdict::Built&&intermediate.evidence,"intermediate endpoint factor certificate");
    check(child*2+1==upper&&child*child>upper,"upper endpoint factor product");
    auto upperCertificate=*intermediate.evidence;
    const auto reference=static_cast<std::uint32_t>(upperCertificate.size()-1);
    upperCertificate.push_back({upper,{{child,1,0,reference}}});
    bool upperVerified=false;
    unsigned upperWitness{};
    for(unsigned a=2;a<66;++a) {
        upperCertificate.back().factors.front().witness=a;
        if(verifyPrimeCertificate(upper,upperCertificate,budget.certificate)==CertificateVerdict::VerifiedPrime) {
            upperVerified=true;upperWitness=a;break;
        }
    }
    check(upperVerified,"unchanged native consumer accepts the complete upper certificate DAG");
    check(verifyPrimeCertificate(lower,*first.evidence,budget.certificate)==CertificateVerdict::VerifiedPrime,"independent lower certificate consumer");
    const auto original=buildCertifiedGapFromPrimeCertificates(lower,upper-lower,*first.evidence,upperCertificate,limits);
    check(original.verdict==GapBuildVerdict::BudgetExceeded&&!original.evidence,"original small-base producer cannot prove this strong pseudoprime");
    check(original.interior_positions_built==167,"original producer stops at the known supplied pseudoprime");
    const std::vector<SuppliedCompositeWitness> hints{{168,{CompositeWitnessKind::Divisor,divisor}}};
    check(pseudo==lower+168&&pseudo==divisor*Integer("1287836182261"),"supplied factor identity");
    for(const auto base:bases)check(!verifyStrongCompositeWitness(pseudo,base),"known strong pseudoprime passes each small base");
    const auto built=buildCertifiedGapFromPrimeCertificatesAndWitnesses(lower,upper-lower,*first.evidence,upperCertificate,hints,limits);
    check(built.verdict==GapBuildVerdict::Built&&built.evidence&&built.interior_positions_built==309,"production producer uses supplied large factor and covers all309 positions");
    const auto& proof=*built.evidence;
    const unsigned suppliedDivisors=1;
    check(proof.interior[167].kind==CompositeWitnessKind::Divisor&&proof.interior[167].value==divisor,"exact provided witness retained at its implicit position");
    const auto reject=[&](const std::vector<SuppliedCompositeWitness>& input,GapBuildVerdict expected,const char* label){
        const auto result=buildCertifiedGapFromPrimeCertificatesAndWitnesses(lower,upper-lower,*first.evidence,upperCertificate,input,limits);
        check(result.verdict==expected&&!result.evidence,label);
    };
    auto wrong=hints;wrong[0].offset=0;reject(wrong,GapBuildVerdict::InvalidPair,"zero hint offset refused");
    wrong=hints;wrong[0].offset=310;reject(wrong,GapBuildVerdict::InvalidPair,"endpoint offset refused");
    wrong=hints;wrong[0].offset=UINT64_MAX;reject(wrong,GapBuildVerdict::InvalidPair,"oversized offset refused");
    wrong=hints;wrong.push_back(wrong[0]);reject(wrong,GapBuildVerdict::InvalidPair,"duplicate hint offset refused");
    wrong=hints;wrong.push_back({1,{CompositeWitnessKind::Divisor,2}});reject(wrong,GapBuildVerdict::InvalidPair,"unordered hint offsets refused");
    wrong=hints;wrong[0].witness.value=2;reject(wrong,GapBuildVerdict::InvalidPair,"false supplied divisor refused");
    wrong=hints;wrong[0].witness.value=1;reject(wrong,GapBuildVerdict::InvalidPair,"unit divisor refused");
    wrong=hints;wrong[0].witness.value=pseudo;reject(wrong,GapBuildVerdict::InvalidPair,"trivial self divisor refused");
    wrong=hints;wrong[0].witness.value=Integer(1)<<129;reject(wrong,GapBuildVerdict::BudgetExceeded,"huge witness rejected before modular arithmetic");
    wrong=hints;wrong[0].witness.kind=static_cast<CompositeWitnessKind>(255);reject(wrong,GapBuildVerdict::InvalidPair,"unknown hint kind refused");
    wrong=hints;wrong[0].witness={CompositeWitnessKind::StrongTestFailure,2};reject(wrong,GapBuildVerdict::InvalidPair,"passing strong test is not a compositeness witness");
    wrong=hints;wrong[0].offset=167;reject(wrong,GapBuildVerdict::InvalidPair,"factor cannot be transplanted to a different integer");
    wrong.assign(310,hints[0]);reject(wrong,GapBuildVerdict::InvalidPair,"more hints than interior positions refused");
    reject({},GapBuildVerdict::BudgetExceeded,"missing necessary witness retains unresolved result");
    const std::vector<PrimeCertificateNode> smallLower{{113,{}}},smallUpper{{127,{}}};
    const std::vector<SuppliedCompositeWitness> strongHints{{1,{CompositeWitnessKind::Divisor,2}},
                                                          {4,{CompositeWitnessKind::StrongTestFailure,2}}};
    check(verifyStrongCompositeWitness(117,2),"fixed supplied117 has an actual strong-test failure");
    const auto strongBuilt=buildCertifiedGapFromPrimeCertificatesAndWitnesses(113,14,smallLower,smallUpper,strongHints,limits);
    check(strongBuilt.verdict==GapBuildVerdict::Built&&strongBuilt.evidence&&
          verifyCertifiedGap(*strongBuilt.evidence,limits).verdict==CertifiedGapVerdict::VerifiedConsecutiveGap,
          "mixed ordered divisor and strong-failure hints produce a fully independently verified gap");
    auto cancelled=buildCertifiedGapFromPrimeCertificatesAndWitnesses(lower,upper-lower,*first.evidence,upperCertificate,hints,limits,[]{return true;});
    check(cancelled.verdict==GapBuildVerdict::Cancelled&&!cancelled.evidence&&!cancelled.interior_positions_built,"pre-cancel produces no partial evidence");
    unsigned cancellationChecks=0;
    cancelled=buildCertifiedGapFromPrimeCertificatesAndWitnesses(lower,upper-lower,*first.evidence,upperCertificate,hints,limits,[&]{return ++cancellationChecks>20;});
    check(cancelled.verdict==GapBuildVerdict::Cancelled&&!cancelled.evidence&&cancelled.interior_positions_built>0,"mid-build cancellation produces no partial evidence");
    const auto verified=verifyCertifiedGap(proof,limits);
    check(verified.verdict==CertifiedGapVerdict::VerifiedConsecutiveGap&&verified.interior_positions_examined==309,
          "unchanged native consumer accepts both endpoint proofs and every interior witness");
    auto bad=proof;
    bad.interior[167].value=2;
    check(verifyCertifiedGap(bad,limits).verdict==CertifiedGapVerdict::InvalidEvidence,"false supplied divisor rejected");
    bad=proof;bad.interior.pop_back();
    check(verifyCertifiedGap(bad,limits).verdict==CertifiedGapVerdict::InvalidEvidence,"omitted interior rejected");
    bad=proof;std::swap(bad.lower_certificate,bad.upper_certificate);
    check(verifyCertifiedGap(bad,limits).verdict==CertifiedGapVerdict::InvalidEvidence,"swapped endpoint certificates rejected");
    bad=proof;bad.upper_certificate.back().factors.front().witness=1;
    check(verifyCertifiedGap(bad,limits).verdict==CertifiedGapVerdict::InvalidEvidence,"forged endpoint witness rejected");
    bad=proof;bad.upper_certificate.back().factors.front().prior_certificate=1;
    check(verifyCertifiedGap(bad,limits).verdict==CertifiedGapVerdict::InvalidEvidence,"self-referential endpoint certificate rejected");
    auto shortLimit=limits;shortLimit.max_interior_positions=308;
    check(verifyCertifiedGap(proof,shortLimit).verdict==CertifiedGapVerdict::BudgetExceeded,"interior budget excludes partial positive");
    shortLimit=limits;shortLimit.endpoint_limits.max_integer_bits=81;
    check(verifyCertifiedGap(proof,shortLimit).verdict==CertifiedGapVerdict::BudgetExceeded,"bit budget checked");
    const auto bytes=encodeCertifiedGap(proof,encoding);
    const auto decoded=decodeCertifiedGap(bytes,encoding);
    check(verifyCertifiedGap(decoded,limits).verdict==CertifiedGapVerdict::VerifiedConsecutiveGap,"canonical roundtrip independently verifies");
    auto malformed=bytes;malformed.pop_back();bool refused=false;
    try { (void)decodeCertifiedGap(malformed,encoding); } catch(const std::exception&) {refused=true;}
    check(refused,"truncated encoded proof rejected");
    // Native consumers remain separate from this supplied-input constructor.
    // This proves this gap, not range completeness, merit or network credit.
    if(argc==2) {
    std::ofstream file(output,std::ios::binary|std::ios::out);
    check(bool(file),"open private output");
    file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));file.flush();
    check(bool(file),"write private output");file.close();
    }
    std::cout<<"{\"PASS\":true,\"checks\":"<<checks
             <<",\"lower\":\""<<lower<<"\",\"upper\":\""<<upper
             <<"\",\"gap\":310,\"interior_positions\":309,\"supplied_large_divisors\":1"
             <<",\"lower_certificate_nodes\":"<<proof.lower_certificate.size()
             <<",\"upper_certificate_nodes\":"<<proof.upper_certificate.size()
             <<",\"upper_witness\":"<<upperWitness<<",\"encoded_bytes\":"<<bytes.size()
             <<",\"elapsed_microseconds\":"<<std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count()
             <<",\"network_submission\":false,\"range_completeness_claim\":false}\n";
    return 0;
 } catch(const std::exception& error) {
    std::cerr<<"FAIL after "<<checks<<" checks: "<<error.what()<<'\n';return 1;
 }
}
