// Public supplied integers only. No generated endpoints, range/CPU/GPU mining.
#include <prime_certificate_builder.h>
#include <certified_gap_builder.h>
#include <certified_gap_encoding.h>
#include <QCryptographicHash>
#include <QFile>
#include <chrono>
#include <iostream>
using namespace pgn::core;
namespace {unsigned checks{},failures{};void Check(bool b,const char* what){++checks;if(!b){++failures;std::cerr<<"FAIL "<<what<<'\n';}}}
int main(int argc,char** argv){const auto start=std::chrono::steady_clock::now();
    const PrimeBuildBudget budget{{128,32,32},10000,64};
    const auto leaf=buildSuppliedPrimeCertificate(19,budget);
    Check(leaf.verdict==PrimeBuildVerdict::Built&&leaf.evidence&&leaf.evidence->size()==1&&leaf.evidence->front().factors.empty(),"strict MR12 leaf remains identical, no extra factoring");
    const Integer n=(Integer(1)<<89)-1; // supplied fixed public Mersenne number.
    const auto built=buildSuppliedPrimeCertificate(n,budget);
    Check(n>=mr12ExclusiveBound()&&built.verdict==PrimeBuildVerdict::Built&&built.evidence&&!built.evidence->back().factors.empty(),"produce real89-bit Pocklington certificate beyond deterministic MR12 domain");
    Integer lucas=4;for(unsigned i=0;i<87;++i)lucas=(lucas*lucas-2)%n;
    Check(lucas==0,"independent Lucas-Lehmer arithmetic corroborates supplied M89, no certificate-generator predicate reuse");
    if(built.evidence){
        Check(verifyPrimeCertificate(n,*built.evidence,budget.certificate)==CertificateVerdict::VerifiedPrime,"UNCHANGED independent native verifier accepts generated Pocklington graph");
        auto falseGraph=*built.evidence;falseGraph.back().factors.front().witness=1;
        Check(verifyPrimeCertificate(n,falseGraph,budget.certificate)==CertificateVerdict::Invalid,"native consumer rejects wrong producer witness");
        Check(verifyPrimeCertificate(n+2,*built.evidence,budget.certificate)==CertificateVerdict::Invalid,"proof cannot move to another integer");
    }
    Check(built.trial_divisions_used<=budget.trial_divisions&&built.witness_candidates_used<=std::uint64_t(32)*64,"explicit factoring/witness work counters stay bounded");
    for(const Integer x:std::vector<Integer>{0,1,-3}){const auto r=buildSuppliedPrimeCertificate(x,budget);Check(r.verdict==PrimeBuildVerdict::InvalidInput&&!r.evidence,"non-prime-domain input refuses without partial evidence");}
    for(const Integer x:std::vector<Integer>{4,21,3*n}){const auto r=buildSuppliedPrimeCertificate(x,budget);Check(r.verdict==PrimeBuildVerdict::Composite&&!r.evidence,"actual composite supplied input never promoted");}
    const auto poison=buildSuppliedPrimeCertificate(mr12ExclusiveBound(),budget);
    Check(poison.verdict!=PrimeBuildVerdict::Built&&!poison.evidence,"strict-bound pseudoprime cannot use MR12 as positive proof");
    auto low=budget;low.trial_divisions=1;const auto exhausted=buildSuppliedPrimeCertificate(n,low);
    Check(exhausted.verdict==PrimeBuildVerdict::BudgetExceeded&&!exhausted.evidence,"factoring budget exhaustion is NOT composite/partial positive");
    low=budget;low.witness_candidates_per_factor=0;const auto noWitness=buildSuppliedPrimeCertificate(n,low);
    Check(noWitness.verdict==PrimeBuildVerdict::BudgetExceeded&&!noWitness.evidence&&!noWitness.trial_divisions_used,"missing explicit witness budget refuses before work");
    low=budget;low.certificate.max_integer_bits=88;const auto tooBig=buildSuppliedPrimeCertificate(n,low);
    Check(tooBig.verdict==PrimeBuildVerdict::BudgetExceeded&&!tooBig.evidence&&!tooBig.trial_divisions_used,"integer-bit admission BEFORE expensive work");
    low=budget;low.certificate.max_factors_per_node=1;const auto nodes=buildSuppliedPrimeCertificate(n,low);
    Check(nodes.verdict==PrimeBuildVerdict::BudgetExceeded&&!nodes.evidence,"factor graph budget never leaks a partial certificate");
    auto cancelled=buildSuppliedPrimeCertificate(n,budget,[]{return true;});
    Check(cancelled.verdict==PrimeBuildVerdict::Cancelled&&!cancelled.evidence&&!cancelled.trial_divisions_used,"pre-cancel does no factor work");
    unsigned calls{};cancelled=buildSuppliedPrimeCertificate(n,budget,[&]{return ++calls>35;});
    Check(cancelled.verdict==PrimeBuildVerdict::Cancelled&&!cancelled.evidence&&cancelled.trial_divisions_used>0,"mid-factoring cancellation releases no partial positive");
    for(const Integer value:std::vector<Integer>{Integer("2751796634684905577763684629"),Integer("2751796634684905577763684853")}){
        const auto outcome=buildSuppliedPrimeCertificate(value,budget);
        Check(outcome.verdict!=PrimeBuildVerdict::Composite&&outcome.verdict!=PrimeBuildVerdict::InvalidInput,"conserved supplied BEST224 endpoint is not mislabeled by an exhausted factorization budget");
        if(outcome.evidence)Check(verifyPrimeCertificate(value,*outcome.evidence,budget.certificate)==CertificateVerdict::VerifiedPrime,"actual92-bit supplied endpoint has native verified certificate");
        std::cout<<"SUPPLIED_BEST224 endpoint="<<value<<" verdict="<<int(outcome.verdict)<<" divisions="<<outcome.trial_divisions_used<<" witness_candidates="<<outcome.witness_candidates_used<<'\n';
    }
    const Integer p("2751796634684905577763684629"),q("2751796634684905577763684853");
    // Untrusted GNU factor development output, independently revalidated here.
    // The library never executes GNU factor or depends on an external daemon.
    const std::vector<PrimeFactorPower> pf{{2,2},{7,2},{1716041,1},{1013843563,1},{Integer("8069779271"),1}};
    const std::vector<PrimeFactorPower> qf{{2,2},{3,1},{7,1},{7457,1},{19726277,1},{Integer("222703877584277"),1}};
    const auto pc=buildSuppliedPrimeCertificateFromFactors(p,pf,budget),qc=buildSuppliedPrimeCertificateFromFactors(q,qf,budget);
    Check(pc.verdict==PrimeBuildVerdict::Built&&pc.evidence&&verifyPrimeCertificate(p,*pc.evidence,budget.certificate)==CertificateVerdict::VerifiedPrime,"independent exact native verification certifies first actual92-bit endpoint");
    Check(qc.verdict==PrimeBuildVerdict::Built&&qc.evidence&&verifyPrimeCertificate(q,*qc.evidence,budget.certificate)==CertificateVerdict::VerifiedPrime,"independent exact native verification certifies second actual92-bit endpoint");
    auto wrong=pf;wrong[1].prime=6;const auto badFactor=buildSuppliedPrimeCertificateFromFactors(p,wrong,budget);
    Check(badFactor.verdict==PrimeBuildVerdict::InvalidInput&&!badFactor.evidence,"untrusted composite factor hint rejected");
    wrong=pf;wrong[1].prime=2;Check(!buildSuppliedPrimeCertificateFromFactors(p,wrong,budget).evidence,"duplicate/out-of-order factor cannot double-charge Pocklington F");
    wrong=pf;wrong[0].exponent=0;Check(!buildSuppliedPrimeCertificateFromFactors(p,wrong,budget).evidence,"zero exponent refuses");
    wrong=pf;wrong[0].exponent=129;Check(!buildSuppliedPrimeCertificateFromFactors(p,wrong,budget).evidence,"oversized exponent refused before product/modular work");
    wrong=pf;++wrong.back().exponent;Check(!buildSuppliedPrimeCertificateFromFactors(p,wrong,budget).evidence,"factor product overflow/divisibility cannot forge F");
    const std::vector<PrimeFactorPower> tiny{{2,2}};Check(buildSuppliedPrimeCertificateFromFactors(p,tiny,budget).verdict==PrimeBuildVerdict::BudgetExceeded,"insufficient factored prefix does NOT prove or disprove supplied candidate");
    if(pc.evidence&&qc.evidence){
        const CertifiedGapLimits gapLimits{budget.certificate,1000};
        const auto gap=buildCertifiedGapFromPrimeCertificates(p,q-p,*pc.evidence,*qc.evidence,gapLimits);
        Check(gap.verdict==GapBuildVerdict::Built&&gap.evidence&&gap.interior_positions_built==223,"full supplied92-bit gap224 produced without endpoint search");
        if(gap.evidence){
            Check(verifyCertifiedGap(*gap.evidence,gapLimits).verdict==CertifiedGapVerdict::VerifiedConsecutiveGap,"UNCHANGED native consumer independently verifies both endpoints and ALL223 interiors");
            const auto bytes=encodeCertifiedGap(*gap.evidence,{{1002,1048576,32,1},gapLimits});
            const QByteArray canonical(reinterpret_cast<const char*>(bytes.data()),qsizetype(bytes.size()));
            const auto hash=QCryptographicHash::hash(canonical,QCryptographicHash::Sha256).toHex();
            Check(verifyCertifiedGap(decodeCertifiedGap(bytes,{{1002,1048576,32,1},gapLimits}),gapLimits).verdict==CertifiedGapVerdict::VerifiedConsecutiveGap,"portable canonical92-bit gap certificate decodes and independently revalidates");
            std::cout<<"BEST224_NATIVE_FULL_PROOF bytes="<<bytes.size()<<" sha256="<<hash.constData()<<"; all223 interiors, no new origin/record/network credit\n";
            if(argc==3&&std::string(argv[1])=="--output-proof"){
                QFile output(QString::fromLocal8Bit(argv[2]));
                Check(output.open(QIODevice::WriteOnly|QIODevice::NewOnly)&&output.write(canonical)==canonical.size()&&output.flush(),"write exact requested proof artifact NEWONLY, never overwrite");
            }
        }
        Check(buildBoundedCertifiedGap(p,q-p,gapLimits).verdict==GapBuildVerdict::UnsupportedDomain,"existing production MR12-only entry/policy remains unchanged");
        Check(buildCertifiedGapFromPrimeCertificates(p,q-p,*qc.evidence,*pc.evidence,gapLimits).verdict==GapBuildVerdict::InvalidPair,"provided endpoint certificates cannot be swapped");
        auto shortGap=gapLimits;shortGap.max_interior_positions=222;Check(!buildCertifiedGapFromPrimeCertificates(p,q-p,*pc.evidence,*qc.evidence,shortGap).evidence,"whole gap interior budget excludes partial positive");
    }
    std::cout<<"PRIME_CERTIFICATE_BUILDER "<<checks<<" checks / "<<failures<<" failures; supplied89-bit Pocklington, divisions="<<built.trial_divisions_used<<", witness_candidates="<<built.witness_candidates_used<<", elapsed_ms="
        <<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()<<"; NO mining/network/production policy change\n";
    return failures?1:0;
}
