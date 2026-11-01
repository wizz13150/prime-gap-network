// Hardware metadata only. No prime arithmetic, sieve, FLINT or job command.
#include <cstdint>
#include <cstring>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif
#include "inventory_only.h"
int main(int argc,char**argv){
 if(argc!=2||std::strcmp(argv[1],"--inventory")!=0){std::fprintf(stderr,"Usage: horizon-cpu-inventory --inventory\n");return 2;}
 try{printCpuInventory(false);return 0;}catch(const std::exception&e){std::fprintf(stderr,"CPU inventory error: %s\n",e.what());return 1;}
}
