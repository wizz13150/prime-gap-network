#pragma once
#include <algorithm>
#include <cmath>

struct FrontierGaugeGeometry {
    double centerX{0.0};
    double meterWidth{0.0};
    double ringTop{0.0};
    double ringBottom{0.0};
    double labelLeft{0.0};
    double labelTop{0.0};
    double labelRight{0.0};
    double labelBottom{0.0};
    double centerTextGuardRadius{0.0};
    double needleInnerRadius{0.0};
    double radius{0.0};
};

inline FrontierGaugeGeometry FrontierGaugeGeometryForCard(double cardLeft,
                                                           double cardWidth,
                                                           int index,
                                                           int count,
                                                           double centerY,
                                                           double radius)
{
    if(count<=0||index<0||index>=count||!std::isfinite(cardWidth)||cardWidth<=22.0||
       !std::isfinite(radius)||radius<=0.0)return {};
    const double meterWidth = (cardWidth - 22.0) / count;
    // The 7px Horizon stroke and a 5px gap belong inside EACH meter cell.
    // Keep the original radius where it fits, not overlapping nine metrics.
    radius=std::min(radius,std::max(0.0,(meterWidth-12.0)/2.0));
    const double centerX = cardLeft + 11.0 + meterWidth * (index + 0.5);
    const double labelWidth = meterWidth - 4.0;
    return {
        centerX,
        meterWidth,
        centerY - radius,
        centerY + radius,
        centerX - labelWidth / 2.0,
        centerY + radius + 4.0,
        centerX + labelWidth / 2.0,
        centerY + radius + 18.0,
        std::max(0.0,radius-2.0),
        radius+1.0,
        radius,
    };
}
