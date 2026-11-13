#pragma once
#include <QString>
#include <QRegularExpression>
namespace pgn::social {
// Display only. The model, sorting and tooltip keep the original exact integer.
inline QString leaderboardNumber(const QString& integer){
 if(integer.size()<=12||!QRegularExpression("^[1-9][0-9]+$").match(integer).hasMatch())return integer;
 int exponent=integer.size()-1;auto significant=integer.left(4).toUInt();
 const bool approximate=integer.mid(4).contains(QRegularExpression("[1-9]"));
 if(integer[4]>='5')++significant;
 if(significant==10000){significant=1000;++exponent;}
 const auto digits=QString::number(significant).rightJustified(4,'0');
 return(approximate?QString::fromUtf8("≈"):QString{})+digits.left(1)+"."+digits.mid(1)+"e"+QString::number(exponent);
}
}
