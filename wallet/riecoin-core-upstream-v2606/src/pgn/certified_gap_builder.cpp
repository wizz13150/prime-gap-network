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
GapBuildResult buildCertifiedGapFromPrimeCertificatesAndWitnesses(const Integer& lower,const Integer& gap,
    const std::vector<PrimeCertificateNode>& lower_certificate,const std::vector<PrimeCertificateNode>& upper_certificate,
    std::span<const SuppliedCompositeWitness> witnesses,CertifiedGapLimits limits,const std::function<bool()>& cancelled) {
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
  // Entire hint structure is admitted before endpoint modular arithmetic or
  // interior allocation. The independent consumer will verify the final proof.
  if(witnesses.size()>limits.max_interior_positions || witnesses.size()>gap-1)
    return fail(GapBuildVerdict::InvalidPair);
  std::uint64_t previous=0;
  for(const auto& hint:witnesses) {
    if(stop())return fail(GapBuildVerdict::Cancelled);
    if(hint.offset<=previous || hint.offset>=gap || hint.witness.value<2 ||
        (hint.witness.kind!=CompositeWitnessKind::Divisor&&hint.witness.kind!=CompositeWitnessKind::StrongTestFailure))
      return fail(GapBuildVerdict::InvalidPair);
    if(!admitted(hint.witness.value,endpoint.max_integer_bits))
      return fail(GapBuildVerdict::BudgetExceeded);
    if(hint.witness.value>=lower+hint.offset)return fail(GapBuildVerdict::InvalidPair);
    previous=hint.offset;
  }
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
    std::size_t hint_index=0;
    for(std::uint64_t i=0;i<positions;++i,++n) {
      if(stop())return fail(GapBuildVerdict::Cancelled);
      bool found=false;
      if(hint_index<witnesses.size()&&witnesses[hint_index].offset==i+1) {
        const auto& witness=witnesses[hint_index++].witness;
        const bool valid=witness.kind==CompositeWitnessKind::Divisor
            ? n%witness.value==0 : verifyStrongCompositeWitness(n,witness.value);
        if(!valid)return fail(GapBuildVerdict::InvalidPair);
        proof.interior.push_back(witness);found=true;
      }
      if(!found)for(const auto base:BASES)if(n>base && n%base==0) {
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
GapBuildResult buildCertifiedGapFromPrimeCertificates(const Integer& lower,const Integer& gap,
    const std::vector<PrimeCertificateNode>& lower_certificate,const std::vector<PrimeCertificateNode>& upper_certificate,
    CertifiedGapLimits limits,const std::function<bool()>& cancelled) {
  return buildCertifiedGapFromPrimeCertificatesAndWitnesses(lower,gap,lower_certificate,upper_certificate,{},limits,cancelled);
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
