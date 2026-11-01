#include "ui/throttle_badge.h"
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QFontMetrics>
#include <iostream>
int main(int argc,char** argv){
 QGuiApplication application(argc,argv);
 const QStringList classes{"NONE","THERMAL","POWER/CURRENT","HARDWARE","CLOCK/POLICY","UNKNOWN"};
 for(const auto& reason:classes){
  const auto label=pgn::ui::throttleBadgeLabel(reason);
  if(label!=QStringLiteral("THROTTLE ")+reason||label.contains("CONTROL"))return 1;
  for(int width:{80,120,240}){
   QImage image(width,20,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::black);
   QPainter painter(&image);painter.setPen(Qt::white);painter.setFont(QFont("Monospace",7));
   const auto visible=QFontMetrics(painter.font()).elidedText(label,Qt::ElideRight,width);
   if(visible.contains("CONTROL"))return 2;
   painter.drawText(image.rect(),Qt::AlignLeft|Qt::AlignVCenter,visible);
  }
 }
 for(const auto& invalid:QStringList{"","CONTROL ON","CONTROL OFF","UNKNOWN · CONTROL ON"})
  if(pgn::ui::throttleBadgeLabel(invalid)!="THROTTLE UNKNOWN")return 3;
 std::cout<<"PASS observational throttle badge: six driver states, three render widths, no mining/control status conflation; no hardware mutations.\n";
}
