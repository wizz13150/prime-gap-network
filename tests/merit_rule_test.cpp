#include "protocol/merit_rule.h"
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/rational.hpp>
#include <iostream>
namespace {
using Integer=boost::multiprecision::cpp_int;
using Rational=boost::rational<Integer>;
// Independent exact rational series, without the implementation's dyadic
// rounding recurrence. Positive geometric tail certifies both endpoints.
std::pair<Rational,Rational> series(const Rational& z){
 Rational sum(0),power=z,square=z*z;
 for(int i=0;i<96;++i){sum+=Rational(2)*power/Rational(2*i+1);power*=square;}
 return {sum,sum+Rational(2)*power/(Rational(193)*(Rational(1)-square))};
}
std::pair<Rational,Rational> reference(int p){
 int k=0;while((1<<(k+1))<=p)++k;
 const int two=1<<k;
 const auto a=series(Rational(1,3)),b=series(Rational(p-two,p+two));
 return {Rational(k)*a.first+b.first,Rational(k)*a.second+b.second};
}
}
int main(){
 using namespace pgn::protocol;
 for(int p=2;p<=32;++p){const auto bounds=reference(p);
  for(int gap=1;gap<=6;++gap)for(int target=1;target<=8;++target){
   const auto result=compareMerit(QString::number(p),QString::number(p+gap),QString::number(target),"1");
   if(Rational(gap)>=Rational(target)*bounds.first&&Rational(gap)<=Rational(target)*bounds.second){std::cerr<<"FAIL independent oracle precision insufficient\n";return 7;}
   const auto expected=Rational(gap)>Rational(target)*bounds.second?MeritDecision::AtLeast:MeritDecision::Below;
   if(result.decision!=expected){std::cerr<<"FAIL rational reference p="<<p<<" gap="<<gap<<" target="<<target<<"\n";return 1;}
   const Rational lower(Integer(result.logarithmLower.toStdString()),Integer(result.scale.toStdString()));
   const Rational upper(Integer(result.logarithmUpper.toStdString()),Integer(result.scale.toStdString()));
   if(lower>bounds.first||upper<bounds.second){std::cerr<<"FAIL certified ln interval\n";return 2;}
  }
 }
 if(compareMerit("199999999999999999999","200000000000000000863","18","1").decision!=MeritDecision::AtLeast||
    compareMerit("199999999999999999999","200000000000000000863","19","1").decision!=MeritDecision::Below)return 3;
 const Integer high=Integer(1)<<127;
 const auto first=QString::fromStdString(high.convert_to<std::string>());
 const auto last=QString::fromStdString(Integer(high+127).convert_to<std::string>());
 if(compareMerit(first,last,"1","1").decision!=MeritDecision::AtLeast||compareMerit(first,last,"2","1").decision!=MeritDecision::Below)return 8;
 const QString denominator="1000000000000000000000000000";
 const auto close=compareMerit("2","3","1442695040888963407359924681",denominator,{160,64});
 if(close.decision!=MeritDecision::Unresolved)return 4;
 const auto accepted=compareMerit("2","3","1442695040888963407359924681",denominator);
 const auto rejected=compareMerit("2","3","1442695040888963407359924682",denominator);
 if(accepted.decision!=MeritDecision::AtLeast||rejected.decision!=MeritDecision::Below||accepted.precisionBits<=64)return 5;
 if(compareMerit("02","3","1","1").decision!=MeritDecision::InvalidInput||
    compareMerit("1","3","1","1").decision!=MeritDecision::InvalidInput||
    compareMerit("2","2","1","1").decision!=MeritDecision::InvalidInput||
    compareMerit("2","3","1","0").decision!=MeritDecision::InvalidInput||
    compareMerit("2","3","1.0","1").decision!=MeritDecision::InvalidInput||
    compareMerit("2","3","1","1",{160,63}).decision!=MeritDecision::InvalidInput)return 6;
 std::cout<<"PASS 1488 exact rational comparisons/log certificates,68bit endpoints,sub-1e-27 boundary,work-budget unresolved never accepted\n";
}
