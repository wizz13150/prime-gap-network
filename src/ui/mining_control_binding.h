#pragma once
#include <QWidget>
#include <QStringList>
namespace pgn::ui {
// A worker profile grants control of its actual device, irrespective of
// whether its process is local or reached through a remote endpoint.
inline void registerMiningSourceDevice(QWidget* canvas,const QString& device){
 if(device.isEmpty())return;
 auto devices=canvas->property("pgn_controlled_source_devices").toStringList();
 if(!devices.contains(device,Qt::CaseInsensitive))devices.append(device);
 canvas->setProperty("pgn_controlled_source_devices",devices);
 if(canvas->property("pgn_managed_source_device").toString().isEmpty())canvas->setProperty("pgn_managed_source_device",device);
 canvas->setProperty("pgn_source_only_control",true);
}
inline bool miningSourceManaged(const QWidget* canvas,const QString& device,bool cpuControl){
 return !canvas->property("pgn_source_only_control").toBool()||cpuControl
  ||canvas->property("pgn_controlled_source_devices").toStringList().contains(device,Qt::CaseInsensitive)
  ||device.compare(canvas->property("pgn_managed_source_device").toString(),Qt::CaseInsensitive)==0;
}
}
