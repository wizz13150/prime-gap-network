#include "euler68_fused_vectors.h"
#include <gmp.h>
#include <fstream>
#include <vector>
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <cstdlib>
static void require(bool value,const char* message){if(!value){std::fprintf(stderr,"REFUSED %s\n",message);std::exit(1);}}
int main(int argc,char** argv){
  require(argc==3,"Pinned input and fresh output paths required");
  std::ifstream in(argv[1],std::ios::binary);pgn68_vectors::Header h{};
  in.read(reinterpret_cast<char*>(&h),sizeof(h));
  require(bool(in)&&h.magic==pgn68_vectors::Magic&&h.count==16384&&h.record_bytes==sizeof(pgn68_vectors::Record)&&!h.reserved,"Exact source fixture header");
  std::vector<pgn68_vectors::Record> source(h.count);
  in.read(reinterpret_cast<char*>(source.data()),source.size()*sizeof(source[0]));
  char extra;require(bool(in)&&!in.read(&extra,1)&&in.eof(),"Exact source fixture tail");
  mpz_t n,a,r,ri,result;mpz_inits(n,a,r,ri,result,nullptr);
  mpz_set_ui(r,1);mpz_mul_2exp(r,r,68);
  std::vector<pgn68_fused_vectors::Record> expected(h.count);
  for(size_t i=0;i<source.size();++i){
    const auto& v=source[i];require(v.n.w[2]==10&&(v.n.w[0]&1U)&&!v.reserved,"Supported source modulus");
    mpz_import(n,3,-1,4,0,0,v.n.w);mpz_import(a,3,-1,4,0,0,v.a.w);
    require(mpz_cmp(a,n)<0&&mpz_invert(ri,r,n)!=0,"Normalized operand and independent inverse");
    for(unsigned bit=0;bit<2;++bit){
      mpz_mul(result,a,a);mpz_mul(result,result,ri);mpz_mul_2exp(result,result,bit);mpz_mod(result,result,n);
      size_t count=0;mpz_export(expected[i].square[bit].w,&count,-1,4,0,0,result);
      require(count<=3,"Exact square oracle word bound");
    }
  }
  // Exclusive create never replaces an earlier fixture or partial failed output.
  const int fd=_open(argv[2],_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY,_S_IREAD|_S_IWRITE);
  require(fd>=0,"Fresh output admission");FILE* out=_fdopen(fd,"wb");require(out!=nullptr,"Output stream");
  h.magic=pgn68_fused_vectors::Magic;h.record_bytes=sizeof(pgn68_fused_vectors::Record);
  require(std::fwrite(&h,sizeof(h),1,out)==1&&std::fwrite(expected.data(),sizeof(expected[0]),expected.size(),out)==expected.size(),"Complete fixture write");
  require(std::fclose(out)==0,"Fixture close");mpz_clears(n,a,r,ri,result,nullptr);
  std::printf("FUSED_GMP_FIXTURE records=%u square_bit_oracles=%u\n",h.count,h.count*2U);
}
