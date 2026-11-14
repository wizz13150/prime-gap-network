#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <boost/multiprecision/cpp_int.hpp>
namespace pgn::discussion {
using Integer=boost::multiprecision::cpp_int;
struct HourlyCalibration {int reference_merit;uint64_t frequency_numerator,frequency_denominator;};
enum class Observation {Complete,Startup,Unavailable};
inline Integer Power(Integer base,unsigned exponent){Integer out=1;while(exponent){if(exponent&1)out*=base;base*=base;exponent>>=1;}return out;}
// Counts are newest -> oldest. No device-specification or audit count input.
inline Integer WeightedReturns(const std::array<uint64_t,6>& counts){Integer total=0;for(unsigned i=0;i<6;++i)total+=Integer{counts[i]}*(6-i);return total;}
// UTC half-open windows: target H observes [H-65min,H-5min).
inline std::optional<unsigned> ReceiptWindow(int64_t activation,int64_t receipt){
 if(activation%3600)throw std::invalid_argument("UTC hour required");
 const auto end=activation-300;if(receipt>=end||receipt<end-3600)return {};
 return unsigned((end-1-receipt)/600);
}
// Exact rounding without log()/floating-point. C=1 is explicit for this
// provisional model; calibration is a required argument, never a hidden default.
// No activation hook: existing tickets and active consensus remain unchanged.
inline std::optional<unsigned> HourlyTarget(const std::array<uint64_t,6>& counts,Observation observation,HourlyCalibration model){
 if(observation==Observation::Unavailable)return {};
 if(observation==Observation::Startup)return 275;
 if(!model.frequency_numerator||!model.frequency_denominator||model.reference_merit<0||model.reference_merit>95)
  throw std::invalid_argument("Invalid scientific calibration");
 const auto weighted=WeightedReturns(counts);if(weighted==0)return 275;
 // Nhat=6*weighted/21; Nhat*B/6=weighted*B/21.
 const auto numerator=Power(weighted*model.frequency_numerator,20);
 const auto denominator=Power(Integer{21}*model.frequency_denominator,20);
 auto below_midpoint=[&](unsigned tenth){
  const int exponent=int(2*tenth+1)-20*model.reference_merit;
  Integer left=numerator,right=denominator;
  if(exponent>=0)right*=Power(3,unsigned(exponent));else left*=Power(3,unsigned(-exponent));
  // For integer reference merit and positive rational inputs, equality is
  // impossible: the exponent of prime 3 on the two sides differs mod 20.
  if(left==right)throw std::logic_error("Unexpected exact half-tenth");
  return left<right;
 };
 unsigned lo=275,hi=275;
 while(!below_midpoint(hi))hi*=2;
 while(lo<hi){const auto mid=lo+(hi-lo)/2;if(below_midpoint(mid))hi=mid;else lo=mid+1;}
 return lo;
}
}

