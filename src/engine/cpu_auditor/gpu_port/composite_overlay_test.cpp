// Protocol harness for independent integer-reference mapping tests.
#include "cpu_composite_overlay.h"
#include <iostream>
#include <memory>
using namespace pgn_cpu_shared;
int main(){try{
 std::unique_ptr<CpuCompositeOverlay> page;std::string action;
 while(std::cin>>action){
  if(action=="PAGE"){
   uint64_t high,low,length;std::string path;std::cin>>high>>low>>length>>path;
   page=std::make_unique<CpuCompositeOverlay>(CpuCompositeOverlay::factored(path,pgn_psp_native::Cohort::from({low,high},length)));
  }else if(action=="MAP"){
   uint32_t high;uint64_t low,slots;std::cin>>high>>low>>slots;if(!page||slots>65536)return 2;
   Class30Bitmap bitmap({low,high},slots,BitmapWords((slots+63)/64,0));
   const auto changed=page->apply(bitmap);if(page->apply(bitmap)!=0)return 3;
   const auto doubled=CpuCompositeOverlay::merge(*page,*page);
   if(doubled.apply(bitmap)!=0)return 4;
   std::cout<<"{\"changed\":"<<changed<<",\"marked\":[";bool first=true;
   for(uint64_t i=0;i<slots;++i)if(bitmap.marked(i)){if(!first)std::cout<<',';first=false;std::cout<<i;}
   std::cout<<"]}\n";
  }else return 5;
 }
 return std::cin.eof()?0:6;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
