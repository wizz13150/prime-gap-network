#include "protocol/merit_rule.h"
#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
namespace pgn::protocol {
namespace {
using Integer=boost::multiprecision::cpp_int;
struct Bounds {Integer lower,upper;};
bool canonical(const QString& value,int maximum){
 if(value.isEmpty()||value.size()>maximum||(value.size()>1&&value.front()=='0'))return false;
 for(const auto c:value)if(c<'0'||c>'9')return false;
 return true;
}
Integer integer(const QString& text){Integer n=0;for(const auto c:text){n*=10;n+=c.unicode()-'0';}return n;}
QString decimal(const Integer& n){return QString::fromStdString(n.convert_to<std::string>());}
Integer ceiling(const Integer& numerator,const Integer& denominator){return (numerator+denominator-1)/denominator;}
// ln(x)=2*atanh(z), z=(x-1)/(x+1). Here 0<=z<=1/3.
// Each scaled power is bracketed by outward integer rounding. The positive
// remainder after N terms is <=2*z^(2N+1)/((2N+1)*(1-z*z)).
Bounds series(const Integer& n,const Integer& d,const Integer& scale,int terms){
 const Integer nn=n*n,dd=d*d;
 Integer lowPower=scale*n/d,highPower=ceiling(scale*n,d);
 Bounds sum{0,0};
 for(int i=0;i<terms;++i){
  const Integer odd=2*i+1;
  sum.lower+=2*lowPower/odd;sum.upper+=ceiling(2*highPower,odd);
  lowPower=lowPower*nn/dd;highPower=ceiling(highPower*nn,dd);
 }
 sum.upper+=ceiling(2*highPower*dd,Integer(2*terms+1)*(dd-nn));
 return sum;
}
Bounds logarithm(const Integer& p,const Integer& scale,int bits){
 const auto k=boost::multiprecision::msb(p);
 const Integer power=Integer(1)<<k;
 const auto logTwo=series(1,3,scale,bits+8);
 const auto remainder=series(p-power,p+power,scale,bits+8);
 return {Integer(k)*logTwo.lower+remainder.lower,Integer(k)*logTwo.upper+remainder.upper};
}
}
MeritComparison compareMerit(const QString& first,const QString& last,const QString& n,
                            const QString& d,MeritWorkBudget budget){
 MeritComparison result;
 if(budget.maximumDecimalDigits<1||budget.maximumDecimalDigits>4096||
    budget.maximumPrecisionBits<64||budget.maximumPrecisionBits>65536){result.error="Invalid explicit merit work budget";return result;}
 for(const auto* value:{&first,&last,&n,&d})if(!canonical(*value,budget.maximumDecimalDigits)){result.error="Canonical decimal integers required within caller work budget";return result;}
 const Integer p=integer(first),q=integer(last),numerator=integer(n),denominator=integer(d);
 if(p<2||q<=p||denominator==0){result.error="Require p>=2, q>p and a positive threshold denominator";return result;}
 if(numerator==0){result.decision=MeritDecision::AtLeast;return result;}
 for(int bits=64;;bits=std::min(bits*2,budget.maximumPrecisionBits)){
  const Integer scale=Integer(1)<<bits;
  const auto bounds=logarithm(p,scale,bits);
  const Integer input=(q-p)*denominator*scale;
  result.precisionBits=bits;result.scale=decimal(scale);
  result.logarithmLower=decimal(bounds.lower);result.logarithmUpper=decimal(bounds.upper);
  if(input>=numerator*bounds.upper){result.decision=MeritDecision::AtLeast;return result;}
  if(input<numerator*bounds.lower){result.decision=MeritDecision::Below;return result;}
  if(bits==budget.maximumPrecisionBits){result.decision=MeritDecision::Unresolved;result.error="Integer log bounds overlap threshold; raise caller precision budget, never accept by rounding";return result;}
 }
}
}
