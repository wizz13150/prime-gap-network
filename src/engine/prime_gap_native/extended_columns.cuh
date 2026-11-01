#pragma once
#include "montgomery_columns.cuh"
#if defined(__CUDACC__)
#define EXT_HD __host__ __device__ __forceinline__
#else
#define EXT_HD inline
#endif
namespace extended_columns {
using Value=optimized_narrow_euler::Value;using Wide=montgomery_columns::Wide;
template<unsigned Bits,unsigned Cache>inline uint64_t reciprocal(uint64_t low,uint32_t high){
    static_assert(Cache<=128&&Cache>=Bits+31,"Exact host reciprocal target");
    const uint32_t n[3]={uint32_t(low),uint32_t(low>>32),high};
    uint32_t target[5]={0,0,0,0,0};target[Cache/32]=1u<<(Cache%32);
    uint64_t mu=0;
    for(int bit=Cache-Bits;bit>=0;--bit){
        const uint64_t trial=mu|(uint64_t(1)<<bit);const uint32_t m[2]={uint32_t(trial),uint32_t(trial>>32)};
        uint32_t p[5]={0,0,0,0,0};
        for(unsigned i=0;i<2;++i)for(unsigned j=0;j<3;++j){
            const uint64_t product=uint64_t(m[i])*n[j];
            uint64_t sum=uint64_t(p[i+j])+uint32_t(product);p[i+j]=uint32_t(sum);
            uint64_t carry=(product>>32)+(sum>>32);unsigned k=i+j+1;
            while(carry&&k<5){sum=uint64_t(p[k])+carry;p[k++]=uint32_t(sum);carry=sum>>32;}
            assert(!carry);
        }
        bool accept=true;for(int j=4;j>=0;--j)if(p[j]!=target[j]){accept=p[j]<target[j];break;}
        if(accept)mu=trial;
    }return mu;
}
template<unsigned Cache>EXT_HD uint64_t quote(Wide a,uint32_t mu,uint32_t high_mu){
    const uint64_t low_product=uint64_t(a.z)*mu;
    const uint64_t middle=uint64_t(a.w)*mu+(low_product>>32);
    const uint64_t add=uint64_t(uint32_t(middle))+uint64_t(a.z)*high_mu;
    const uint64_t upper=(middle>>32)+(add>>32)+uint64_t(a.w)*high_mu;
    const uint32_t p0=uint32_t(add),p1=uint32_t(upper),p2=uint32_t(upper>>32);
    constexpr unsigned Shift=Cache-96;
    if constexpr(Shift==32)return uint64_t(p1)|(uint64_t(p2)<<32);
    else return((uint64_t(p0)|(uint64_t(p1)<<32))>>Shift)|(uint64_t(p2)<<(64-Shift));
}
EXT_HD Value subtract_product(Wide a,uint64_t low,uint32_t high,uint64_t q){
    const uint32_t q0=uint32_t(q),q1=uint32_t(q>>32),n0=uint32_t(low),n1=uint32_t(low>>32);
    const uint64_t p0=uint64_t(q0)*n0,p1=uint64_t(q0)*n1+(p0>>32),p2=uint64_t(q0)*high+(p1>>32);
    const uint64_t extra=uint64_t(uint32_t(p1))+uint64_t(q1)*n0;
    const uint32_t z=uint32_t(p2+(extra>>32)+uint64_t(q1)*n1);
    uint64_t u=uint64_t(a.x)-uint32_t(p0);Value r{uint32_t(u),0,0};
    u=uint64_t(a.y)-uint32_t(extra)-uint32_t(u>>63);r.y=uint32_t(u);r.z=a.z-z-uint32_t(u>>63);return r;
}
template<unsigned Cache>EXT_HD Value fold(Wide a,unsigned bits,uint64_t low,uint32_t high,uint32_t mu,uint32_t high_mu){
#if defined(__CUDA_ARCH__)
    Value r;
    asm("{\n\t.reg .u32 t0,t1,t2,t3,q,qh,p0,p1,p2,c0,c1,c2; .reg .pred whole_word;\n\t"
        "shl.b32 t0,%3,%7;\n\tshf.l.wrap.b32 t1,%3,%4,%7;\n\t"
        "shf.l.wrap.b32 t2,%4,%5,%7;\n\tshf.l.wrap.b32 t3,%5,%6,%7;\n\t"
        "mul.hi.u32 c0,t2,%11;\n\tmul.lo.u32 c1,t3,%11;\n\tmul.hi.u32 c2,t3,%11;\n\t"
        "add.cc.u32 c0,c0,c1;\n\taddc.u32 c1,c2,0;\n\tmov.u32 c2,0;\n\t"
        "mad.lo.cc.u32 c0,t2,%12,c0;\n\tmadc.hi.cc.u32 c1,t2,%12,c1;\n\taddc.u32 c2,c2,0;\n\t"
        "mad.lo.cc.u32 c1,t3,%12,c1;\n\tmadc.hi.u32 c2,t3,%12,c2;\n\t"
        "shf.r.wrap.b32 q,c0,c1,%13;\n\tshf.r.wrap.b32 qh,c1,c2,%13;\n\t"
        "setp.eq.u32 whole_word,%13,32;\n\t@whole_word mov.u32 q,c1;\n\t@whole_word mov.u32 qh,c2;\n\t"
        "mul.lo.u32 p0,q,%8;\n\tmul.hi.u32 p1,q,%8;\n\t"
        "mad.lo.cc.u32 p1,q,%9,p1;\n\tmadc.hi.u32 p2,q,%9,0;\n\tmad.lo.u32 p2,q,%10,p2;\n\t"
        "mad.lo.cc.u32 p1,qh,%8,p1;\n\tmadc.hi.u32 p2,qh,%8,p2;\n\tmad.lo.u32 p2,qh,%9,p2;\n\t"
        "sub.cc.u32 %0,t0,p0;\n\tsubc.cc.u32 %1,t1,p1;\n\tsubc.u32 %2,t2,p2;\n\t}"
        :"=r"(r.x),"=r"(r.y),"=r"(r.z)
        :"r"(a.x),"r"(a.y),"r"(a.z),"r"(a.w),"r"(bits),"r"(uint32_t(low)),"r"(uint32_t(low>>32)),"r"(high),"r"(mu),"r"(high_mu),"n"(Cache-96));
    return r;
#else
    if(bits){
        const uint64_t x=uint64_t(a.x)<<bits,y=(uint64_t(a.y)<<bits)|(x>>32),z=(uint64_t(a.z)<<bits)|(y>>32),w=(uint64_t(a.w)<<bits)|(z>>32);
        assert(!(w>>32));a={uint32_t(x),uint32_t(y),uint32_t(z),uint32_t(w)};
    }
    return subtract_product(a,low,high,quote<Cache>(a,mu,high_mu));
#endif
}
template<unsigned CacheBits>
EXT_HD Value square_fold(Value a,uint32_t n0,uint32_t n1,uint32_t np,uint32_t high,uint32_t mu,uint32_t high_mu){
#if defined(__CUDA_ARCH__)
    Value d;
    asm volatile("{\n\t"
                 ".reg .u32 l,m,h,q0,q1,z2,t0,t1,t2,t3;\n\t"
                 "add.u32 z2,%5,%5;\n\t"
                 "mul.lo.u32 l,%3,%3;\n\t"
                 "mul.hi.u32 m,%3,%3;\n\t"
                 "mul.lo.u32 q0,l,%8;\n\t"
                 "mad.lo.cc.u32 l,q0,%6,l;\n\t"
                 "madc.hi.cc.u32 m,q0,%6,m;\n\t"
                 "addc.u32 h,0,0;\n\t"
                 "mov.u32 l,m;\n\t"
                 "mov.u32 m,h;\n\t"
                 "mad.lo.cc.u32 l,q0,%7,l;\n\t"
                 "madc.hi.cc.u32 m,q0,%7,m;\n\t"
                  "addc.u32 h,0,0;\n\t"
                 "mad.lo.cc.u32 l,%3,%4,l;\n\t"
                 "madc.hi.cc.u32 m,%3,%4,m;\n\t"
                 "addc.u32 h,h,0;\n\t"
                 "mad.lo.cc.u32 l,%3,%4,l;\n\t"
                 "madc.hi.cc.u32 m,%3,%4,m;\n\t"
                 "addc.u32 h,h,0;\n\t"
                 "mul.lo.u32 q1,l,%8;\n\t"
                 "mad.lo.cc.u32 l,q1,%6,l;\n\t"
                 "madc.hi.cc.u32 m,q1,%6,m;\n\t"
                 "addc.u32 h,h,0;\n\t"
                 "mov.u32 l,m;\n\t"
                 "mov.u32 m,h;\n\t"
                 "mad.lo.cc.u32 l,q0,%9,l;\n\t"
                 "madc.hi.u32 m,q0,%9,m;\n\t"
                 "mad.lo.cc.u32 l,%3,z2,l;\n\t"
                 "madc.hi.cc.u32 m,%3,z2,m;\n\t"
                  "addc.u32 h,0,0;\n\t"
                 "mad.lo.cc.u32 l,q1,%7,l;\n\t"
                 "madc.hi.cc.u32 m,q1,%7,m;\n\t"
                 "addc.u32 h,h,0;\n\t"
                 "mad.lo.cc.u32 l,%4,%4,l;\n\t"
                 "madc.hi.cc.u32 m,%4,%4,m;\n\t"
                 "addc.u32 h,h,0;\n\t"
                 "mov.u32 t0,l;\n\t"
                 "mov.u32 l,m;\n\t"
                 "mov.u32 m,h;\n\t"
                 "mad.lo.cc.u32 l,q1,%9,l;\n\t"
                 "madc.hi.u32 m,q1,%9,m;\n\t"
                 "mad.lo.cc.u32 l,%4,z2,l;\n\t"
                 "madc.hi.cc.u32 m,%4,z2,m;\n\t"
                  "addc.u32 h,0,0;\n\t"
                 "mov.u32 t1,l;\n\t"
                 "mad.lo.cc.u32 t2,%5,%5,m;\n\t"
                 "madc.hi.u32 t3,%5,%5,h;\n\t"

        ".reg .u32 q,qh,p0,p1,p2,c0,c1,c2; .reg .pred whole_word;\n\t"
        "mul.hi.u32 c0,t2,%10;\n\tmul.lo.u32 c1,t3,%10;\n\tmul.hi.u32 c2,t3,%10;\n\t"
        "add.cc.u32 c0,c0,c1;\n\taddc.u32 c1,c2,0;\n\tmov.u32 c2,0;\n\t"
        "mad.lo.cc.u32 c0,t2,%12,c0;\n\tmadc.hi.cc.u32 c1,t2,%12,c1;\n\taddc.u32 c2,c2,0;\n\t"
        "mad.lo.cc.u32 c1,t3,%12,c1;\n\tmadc.hi.u32 c2,t3,%12,c2;\n\t"
        "shf.r.wrap.b32 q,c0,c1,%11;\n\tshf.r.wrap.b32 qh,c1,c2,%11;\n\t"
        "setp.eq.u32 whole_word,%11,32;\n\t@whole_word mov.u32 q,c1;\n\t@whole_word mov.u32 qh,c2;\n\t"
        "mul.lo.u32 p0,q,%6;\n\tmul.hi.u32 p1,q,%6;\n\t"
        "mad.lo.cc.u32 p1,q,%7,p1;\n\tmadc.hi.u32 p2,q,%7,0;\n\t"
        "mad.lo.cc.u32 p1,qh,%6,p1;\n\tmadc.hi.u32 p2,qh,%6,p2;\n\t"
        "mad.lo.u32 p2,q,%9,p2;\n\tmad.lo.u32 p2,qh,%7,p2;\n\t"
        "sub.cc.u32 %0,t0,p0;\n\tsubc.cc.u32 %1,t1,p1;\n\tsubc.u32 %2,t2,p2;\n\t}"
        :"=r"(d.x),"=r"(d.y),"=r"(d.z)
        :"r"(a.x),"r"(a.y),"r"(a.z),"r"(n0),"r"(n1),"r"(np),"r"(high),"r"(mu),"n"(CacheBits-96),"r"(high_mu));
    return d;
#else
    return fold<CacheBits>(montgomery_columns::rawSquareWide(a,n0,n1,np,high),0,uint64_t(n0)|(uint64_t(n1)<<32),high,mu,high_mu);
#endif
}

}
#undef EXT_HD
