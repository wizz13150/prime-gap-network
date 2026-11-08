#include "ui/render/horizon_native_frame.h"
#include <QApplication>
#include <QMainWindow>
#include <iostream>
#include <stdexcept>
class TestWindow:public QMainWindow{public:void recreate(){destroy();create();}};
int main(int argc,char** argv){QApplication app(argc,argv);try{
 TestWindow window;
 for(int pass=0;pass<2;++pass){
  if(pass)window.recreate();const auto handle=reinterpret_cast<HWND>(window.winId());
  SendMessageW(handle,WM_SETICON,ICON_SMALL,0);SendMessageW(handle,WM_SETICON,ICON_BIG,0);
  ApplyFrontierNativeFrame(&window);
  if(!SendMessageW(handle,WM_GETICON,ICON_SMALL,0)||!SendMessageW(handle,WM_GETICON,ICON_BIG,0))throw std::runtime_error("Native icons were not restored after loss/window recreation");
 }
 std::cout<<"PASS: real hidden Windows handles, small/large icons restored after loss and recreation; no user window controlled\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
