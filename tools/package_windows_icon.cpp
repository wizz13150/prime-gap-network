#include <QCoreApplication>
#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSaveFile>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
constexpr int sizes[]{16,24,32,48,64,128,256};
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
QImage sourceImage(const QString& path){
    const QFileInfo file(path);require(file.isFile()&&file.size()>0&&file.size()<=2*1024*1024,"Invalid/big source icon");
    QImage image(path);require(!image.isNull()&&image.width()==image.height()&&image.width()>=256&&image.width()<=2048,"Source icon must be square,256..2048px");return image;
}
void pack(const QString& input,const QString& output){
    const auto source=sourceImage(input);QVector<QByteArray> images;
    for(const int size:sizes){QByteArray png;QBuffer buffer(&png);require(buffer.open(QIODevice::WriteOnly),"PNG buffer");
        require(source.scaled(size,size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).save(&buffer,"PNG"),"PNG encoding");images.append(png);}
    QByteArray ico;QDataStream stream(&ico,QIODevice::WriteOnly);stream.setByteOrder(QDataStream::LittleEndian);
    stream<<quint16(0)<<quint16(1)<<quint16(images.size());quint32 offset=6+16*images.size();
    for(int i=0;i<images.size();++i){const quint8 edge=sizes[i]==256?0:sizes[i];
        stream<<edge<<edge<<quint8(0)<<quint8(0)<<quint16(1)<<quint16(32)<<quint32(images[i].size())<<offset;offset+=images[i].size();}
    for(const auto& png:images)require(stream.writeRawData(png.constData(),png.size())==png.size(),"ICO payload");
    require(stream.status()==QDataStream::Ok,"ICO serialization");
    QSaveFile file(output);require(file.open(QIODevice::WriteOnly)&&file.write(ico)==ico.size()&&file.commit(),"Atomic generated ICO save");
    std::cout<<"Generated native ICO:7 sizes16..256, source unchanged\n";
}
#ifdef _WIN32
quint16 word(const unsigned char* p){return quint16(p[0])|(quint16(p[1])<<8);}
quint32 dword(const unsigned char* p){return quint32(word(p))|(quint32(word(p+2))<<16);}
struct ResourceImage{HMODULE module;~ResourceImage(){if(module)FreeLibrary(module);}};
void verify(const QString& executable,const QString& input){
    const auto source=sourceImage(input);const auto native=QFileInfo(executable).absoluteFilePath().toStdWString();
    // Resource-only mapping: NEVER invokes WinMain, DLL entry points or the GUI.
    ResourceImage mapped{LoadLibraryExW(native.c_str(),nullptr,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE)};
    require(mapped.module!=nullptr,"Cannot resource-map executable");
    const auto group=FindResourceW(mapped.module,MAKEINTRESOURCEW(1),MAKEINTRESOURCEW(14));
    require(group!=nullptr,"Missing native ICON group1 (Qt window icon alone cannot supply it)");
    const auto data=LoadResource(mapped.module,group);const auto* p=static_cast<const unsigned char*>(LockResource(data));
    const auto bytes=SizeofResource(mapped.module,group);
    require(p&&bytes==6+14*7&&word(p)==0&&word(p+2)==1&&word(p+4)==7,"Native ICON group structure/sizes");
    for(int i=0;i<7;++i){const auto* entry=p+6+14*i;const int width=entry[0]?entry[0]:256,height=entry[1]?entry[1]:256;
        require(width==sizes[i]&&height==sizes[i]&&word(entry+4)==1&&word(entry+6)==32,"Native icon dimensions/depth");
        const auto imageResource=FindResourceW(mapped.module,MAKEINTRESOURCEW(word(entry+12)),MAKEINTRESOURCEW(3));
        require(imageResource!=nullptr,"Missing native icon payload");const auto payload=LoadResource(mapped.module,imageResource);
        const auto length=SizeofResource(mapped.module,imageResource);require(length==dword(entry+8)&&length<=2*1024*1024,"Native icon payload size");
        const auto decoded=QImage::fromData(static_cast<const uchar*>(LockResource(payload)),length,"PNG");
        const auto expected=source.scaled(width,height,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
        require(!decoded.isNull()&&decoded.convertToFormat(QImage::Format_ARGB32)==expected.convertToFormat(QImage::Format_ARGB32),"Native icon pixels differ from existing brand asset");
    }
    require(ExtractIconExW(native.c_str(),-1,nullptr,nullptr,0)==1,"Windows Shell cannot find exactly one native icon group");
    std::cout<<"Windows native icon:7 sizes/payloads/source-pixel matches; Shell group1 PASS; application not executed\n";
}
#endif
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    try{require(argc==4,"Use --pack source.png output.ico or --verify executable source.png");
        const auto mode=QString::fromLocal8Bit(argv[1]);
        if(mode=="--pack")pack(QString::fromLocal8Bit(argv[2]),QString::fromLocal8Bit(argv[3]));
#ifdef _WIN32
        else if(mode=="--verify")verify(QString::fromLocal8Bit(argv[2]),QString::fromLocal8Bit(argv[3]));
#endif
        else require(false,"Unknown mode");return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
