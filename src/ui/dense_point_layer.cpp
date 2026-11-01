#include "ui/dense_point_layer.h"
#include <algorithm>
#include <cmath>
namespace pgn::ui {
void DensePointLayer::rebuild(const DenseView& view,const std::vector<DensePoint>& points){
 begin(view);append(points);finish();
}
void DensePointLayer::begin(const DenseView& view){
 view_=view;visible_=0;valid_=false;
 image_={};
 if(view.pixels.width()<1||view.pixels.height()<1||view.maxX<=view.minX||view.maxY<=view.minY)return;
 image_=QImage(view.pixels,QImage::Format_ARGB32_Premultiplied);image_.fill(Qt::transparent);
}
bool DensePointLayer::resume(const DenseView& view,const QImage& image,std::size_t visible){
 if(image.isNull()||image.size()!=view.pixels||image.format()!=QImage::Format_ARGB32_Premultiplied)return false;
 view_=view;image_=image;visible_=visible;valid_=false;return true;
}
void DensePointLayer::append(const std::vector<DensePoint>& points){
 if(image_.isNull())return;const auto& view=view_;
 const double sx=(view.pixels.width()-1)/(view.maxX-view.minX),sy=(view.pixels.height()-1)/(view.maxY-view.minY);
 for(const auto& p:points){
  if(p.multiplicity==0||!std::isfinite(p.x)||!std::isfinite(p.y)||p.x<view.minX||p.x>view.maxX||p.y<view.minY||p.y>view.maxY)continue;
  // Same coordinates/colour overwrite the same footprint. Their exact count
  // survives compression; this is not sampling, averaging or a density guess.
  visible_+=p.multiplicity;const int x=std::clamp(int(std::llround((p.x-view.minX)*sx)),0,view.pixels.width()-1);
  const int y=std::clamp(int(std::llround((view.maxY-p.y)*sy)),0,view.pixels.height()-1);
  const QRgb color=qPremultiply(p.color);
  for(int dy=0;dy<2&&y+dy<image_.height();++dy){auto* row=reinterpret_cast<QRgb*>(image_.scanLine(y+dy));
   row[x]=color;if(x+1<image_.width())row[x+1]=color;
  }
 }
}
}
