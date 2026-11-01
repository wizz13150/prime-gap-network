#pragma once
#include <QImage>
#include <QPainter>
#include <QRectF>
#include <cstdint>
#include <vector>
namespace pgn::ui {
struct DensePoint {double x{},y{};QRgb color{};std::size_t multiplicity{1};};
struct DenseView {
 double minX{},maxX{},minY{},maxY{};QSize pixels;std::uint64_t revision{};
 bool operator==(const DenseView&) const = default;
};
// Lossless source input, pixel-limited display. Every visible point contributes
// its actual footprint; no mean, trend, random sampling or source deletion.
class DensePointLayer {
public:
 bool matches(const DenseView& view) const {return valid_&&view==view_;}
 void begin(const DenseView&);
    bool resume(const DenseView&,const QImage&,std::size_t visible);
 void append(const std::vector<DensePoint>&);
 void finish(){valid_=!image_.isNull();}
 void rebuild(const DenseView&,const std::vector<DensePoint>&);
 void paint(QPainter& painter,const QRectF& rectangle) const {painter.drawImage(rectangle,image_);}
 std::size_t visibleCount() const {return visible_;}
 const QImage& image() const {return image_;}
private:
 DenseView view_;QImage image_;bool valid_{};std::size_t visible_{};
};
}
