#pragma once
#include <QColor>
#include <optional>
#include <cmath>
namespace pgn::ui {
enum class MeritTier {Ordinary,NearMiss,Bronze,Silver,Gold};
inline MeritTier meritTier(double merit,std::optional<double> target){
    if(!std::isfinite(merit)||!target||!std::isfinite(*target))return MeritTier::Ordinary;
    if(merit>=*target)return MeritTier::Gold;
    if(merit>=*target-1)return MeritTier::Silver;
    if(merit>=*target-2)return MeritTier::Bronze;
    if(merit>=*target-3)return MeritTier::NearMiss;
    return MeritTier::Ordinary;
}
inline QColor meritColor(MeritTier tier,QColor ordinary=QColor("#69cf8b")){
    switch(tier){
    case MeritTier::Gold:return QColor("#ffd700");
    case MeritTier::Silver:return QColor("#c0c0c0");
    case MeritTier::Bronze:return QColor("#cd7f32");
    case MeritTier::NearMiss:return QColor("#ff6474");
    default:return ordinary;
    }
}
inline QColor meritColor(double merit,std::optional<double> target){return meritColor(meritTier(merit,target));}
}
