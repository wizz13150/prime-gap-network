#pragma once
#include <QString>
#include <stdexcept>

namespace pgn::mining::source_decimal {
// Exact ASCII decimal identities. The enclosing protocol sets its own record
// budget; there is no floating-point or fixed machine-word narrowing here.
inline bool canonical(const QString& text){
    if(text.isEmpty()||(text.size()>1&&text.front()=='0'))return false;
    for(const auto c:text)if(c<'0'||c>'9')return false;
    return true;
}
inline bool less(const QString& a,const QString& b){return a.size()<b.size()||(a.size()==b.size()&&a<b);}
inline QString difference(QString larger,const QString& smaller){
    if(!canonical(larger)||!canonical(smaller)||less(larger,smaller))throw std::invalid_argument("Canonical nonnegative source difference required");
    int borrow=0;qsizetype j=smaller.size()-1;
    for(qsizetype i=larger.size();i-->0;--j){
        const int digit=larger[i].unicode()-'0'-borrow-(j>=0?smaller[j].unicode()-'0':0);
        borrow=digit<0;larger[i]=QChar('0'+digit+(borrow?10:0));
    }
    qsizetype leading=0;while(leading+1<larger.size()&&larger[leading]=='0')++leading;
    return larger.mid(leading);
}
}
