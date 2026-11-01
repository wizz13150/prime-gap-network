#pragma once
#include <QString>

namespace pgn::booking {
// Exact decimal identities at storage/control boundaries. Floating point is
// used only after exact translation, for a normalized visual coordinate.
QString canonicalUnsigned(const QString& text);
int compareUnsigned(const QString& a,const QString& b);
QString scaleDecimal(const QString& text,int places);
QString subtractUnsigned(const QString& a,const QString& b);
QString addUnsigned(const QString& a,const QString& b);
QString divideUnsignedSmall(const QString& numerator,unsigned divisor);
double rangeFraction(const QString& position,const QString& start,const QString& end);
int bitLength(const QString& positive);
bool overlaps(const QString& a,const QString& b,const QString& c,const QString& d);
}
