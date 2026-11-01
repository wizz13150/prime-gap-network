// MIT. Reuses existing native certificate semantics; no new primality test.
#include "certified_gap_builder.h"
#include <array>
#include <new>

namespace pgn::core {
namespace {
constexpr std::array<unsigned,12> BASES{2,3,5,7,11,13,17,19,23,29,31,37};
bool admitted(const Integer& n,std::uint32_t bits) {
  return bits && n>=0 && (n==0 || boost::multiprecision::msb(n)<bits);
}
}
GapBuildResult buildCertifiedGapFromPrimeCertificates(const Integer& lower,const Integer& gap,
    const std::vector<PrimeCertificateNode>& lower_certificate,const std::vector<PrimeCertificateNode>& upper_certificate,
    CertifiedGapLimits limits,const std::function<bool()>& cancelled) {
  std::uint64_t built=0;
  const auto stop=[&]{return cancelled && cancelled();};
  const auto fail=[&](GapBuildVerdict verdict){return GapBuildResult{verdict,std::nullopt,built};};
  if(stop())return fail(GapBuildVerdict::Cancelled);
  if(lower<2 || gap<=0)return fail(GapBuildVerdict::InvalidPair);
  const auto& endpoint=limits.endpoint_limits;
  if(!endpoint.max_nodes || !endpoint.max_factors_per_node ||
      !admitted(lower,endpoint.max_integer_bits) || !admitted(gap,endpoint.max_integer_bits))
    return fail(GapBuildVerdict::BudgetExceeded);
  const Integer upper=lower+gap;
  if(!admitted(upper,endpoint.max_integer_bits) || gap-1>limits.max_interior_positions)
    return fail(GapBuildVerdict::BudgetExceeded);
  try {
    const auto first=verifyPrimeCertificate(lower,lower_certificate,endpoint);
    if(first!=CertificateVerdict::VerifiedPrime)
      return fail(first==CertificateVerdict::BudgetExceeded?GapBuildVerdict::BudgetExceeded:GapBuildVerdict::InvalidPair);
    if(stop())return fail(GapBuildVerdict::Cancelled);
    const auto last=verifyPrimeCertificate(upper,upper_certificate,endpoint);
    if(last!=CertificateVerdict::VerifiedPrime)
      return fail(last==CertificateVerdict::BudgetExceeded?GapBuildVerdict::BudgetExceeded:GapBuildVerdict::InvalidPair);
    CertifiedGap proof{lower,gap,lower_certificate,upper_certificate,{}};
    const auto positions=(gap-1).convert_to<std::uint64_t>();
    if(positions>proof.interior.max_size())return fail(GapBuildVerdict::BudgetExceeded);
    proof.interior.reserve(static_cast<std::size_t>(positions));
    Integer n=lower+1;
    for(std::uint64_t i=0;i<positions;++i,++n) {
      if(stop())return fail(GapBuildVerdict::Cancelled);
      bool found=false;
      for(const auto base:BASES)if(n>base && n%base==0) {
        proof.interior.push_back({CompositeWitnessKind::Divisor,base});found=true;break;
      }
      if(!found)for(const auto base:BASES)if(verifyStrongCompositeWitness(n,base)) {
        proof.interior.push_back({CompositeWitnessKind::StrongTestFailure,base});found=true;break;
      }
      if(!found)return fail(n<mr12ExclusiveBound()?GapBuildVerdict::InvalidPair:GapBuildVerdict::BudgetExceeded);
      ++built;
    }
    if(stop())return fail(GapBuildVerdict::Cancelled);
    return {GapBuildVerdict::Built,std::move(proof),built};
  }catch(const std::bad_alloc&) {
    return fail(GapBuildVerdict::BudgetExceeded);
  }
}
GapBuildResult buildBoundedCertifiedGap(const Integer& lower,const Integer& gap,
    CertifiedGapLimits limits,const std::function<bool()>& cancelled){
  const auto fail=[](GapBuildVerdict v){return GapBuildResult{v,{},0};};
  if(cancelled&&cancelled())return fail(GapBuildVerdict::Cancelled);
  if(lower<2||gap<=0)return fail(GapBuildVerdict::InvalidPair);
  const auto& e=limits.endpoint_limits;
  const Integer upper=lower+gap;
  if(!e.max_nodes||!e.max_factors_per_node||!admitted(lower,e.max_integer_bits)||!admitted(gap,e.max_integer_bits)||
      !admitted(upper,e.max_integer_bits)||gap-1>limits.max_interior_positions)return fail(GapBuildVerdict::BudgetExceeded);
  if(upper>=mr12ExclusiveBound())return fail(GapBuildVerdict::UnsupportedDomain);
  const std::vector<PrimeCertificateNode> first{{lower,{}}},last{{upper,{}}};
  return buildCertifiedGapFromPrimeCertificates(lower,gap,first,last,limits,cancelled);
}
} // namespace pgn::core
