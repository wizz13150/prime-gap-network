#pragma once
#include <QStringList>
namespace pgn::mining {
// Protocol-level probes bound a dead SSH control session. They never bypass
// host authentication, touch GPU ownership or change local worker arguments.
inline QStringList workerControlArguments(QString program,QStringList arguments){
 program.replace('\\','/');const auto name=program.section('/',-1).toLower();
 if(name!="ssh"&&name!="ssh.exe")return arguments;
 return QStringList{"-o","ServerAliveInterval=5","-o","ServerAliveCountMax=2","-o","ConnectTimeout=5"}+arguments;
}
}
