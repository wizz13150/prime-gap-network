#pragma once
#include "montgomery_columns.cuh"
#include "retained_columns.cuh"
#if defined(__CUDACC__)
#define W87_HD __host__ __device__ __forceinline__
#define W87_UNROLL _Pragma("unroll")
#else
#define W87_HD inline
#define W87_UNROLL
#endif
namespace optimized_pairs81_87 {
using Value=optimized_narrow_euler::Value;using Wide=montgomery_columns::Wide;
struct PreparedState{uint32_t muLower=0,coefficient=0;};
W87_HD Wide retained_raw(Value a,uint32_t n0,uint32_t n1,uint32_t np,uint32_t high){
#if defined(__CUDA_ARCH__)
 return retained_columns::square_raw(a,n0,n1,np,high);
#else
 return montgomery_columns::rawSquareWide(a,n0,n1,np,high);
#endif
}
W87_HD Wide product(uint32_t q,uint64_t low,uint32_t high){
    const uint64_t p0=uint64_t(q)*uint32_t(low),p1=uint64_t(q)*uint32_t(low>>32)+(p0>>32),p2=uint64_t(q)*high+(p1>>32);
    return{uint32_t(p0),uint32_t(p1),uint32_t(p2),uint32_t(p2>>32)};
}
W87_HD Wide subtract(Wide a,Wide b){
    uint64_t x=uint64_t(a.x)-b.x;Wide r{uint32_t(x),0,0,0};
    x=uint64_t(a.y)-b.y-uint32_t(x>>63);r.y=uint32_t(x);
    x=uint64_t(a.z)-b.z-uint32_t(x>>63);r.z=uint32_t(x);r.w=a.w-b.w-uint32_t(x>>63);return r;
}
W87_HD Wide shifted_wide(Wide a,unsigned bits){
    if(!bits)return a;
    const uint64_t x=uint64_t(a.x)<<bits,y=(uint64_t(a.y)<<bits)|(x>>32),z=(uint64_t(a.z)<<bits)|(y>>32),w=(uint64_t(a.w)<<bits)|(z>>32);
    assert(!(w>>32));return{uint32_t(x),uint32_t(y),uint32_t(z),uint32_t(w)};
}
W87_HD Value subtract_if_ge(Value a,Value n){
    if(a.z>n.z||(a.z==n.z&&(a.y>n.y||(a.y==n.y&&a.x>=n.x)))){
        uint64_t v=uint64_t(a.x)-n.x;a.x=uint32_t(v);v=uint64_t(a.y)-n.y-uint32_t(v>>63);a.y=uint32_t(v);a.z-=n.z+uint32_t(v>>63);
    }return a;
}
W87_HD uint32_t hostU32ToFloatRD(uint32_t x) {
    if (!x)
        return 0;
    int e = 31;
    while (!(x & (uint32_t(1) << e)))
        --e;
    uint32_t s = e > 23 ? x >> (e - 23) : x << (23 - e);
    return (uint32_t(e + 127) << 23) | (s & 0x7fffffu);
}
W87_HD uint32_t hostMultiplyFloatRD(uint32_t a, uint32_t b) {
    if (!a || !b)
        return 0;
    uint64_t p = uint64_t((a & 0x7fffffu) | (1u << 23)) * ((b & 0x7fffffu) | (1u << 23));
    int k = (p & (uint64_t(1) << 47)) ? 47 : 46,
        e = int((a >> 23) & 255) + int((b >> 23) & 255) - 300 + k;
    uint32_t s = uint32_t(p >> (k - 23));
    return (uint32_t(e + 127) << 23) | (s & 0x7fffffu);
}
W87_HD uint32_t hostFloatFloorU32(uint32_t a) {
    if (!a)
        return 0;
    int e = int((a >> 23) & 255) - 127;
    if (e < 0)
        return 0;
    uint32_t s = (a & 0x7fffffu) | (1u << 23);
    return e < 23 ? s >> (23 - e) : s << (e - 23);
}
W87_HD uint32_t hostU64ToFloatRD(uint64_t x) {
    if(!x)return 0;int e=63;while(!(x&(uint64_t(1)<<e)))--e;
    const uint64_t s=e>23?x>>(e-23):x<<(23-e);
    return(uint32_t(e+127)<<23)|(uint32_t(s)&0x7fffffu);
}
W87_HD uint32_t hostQuotient(Wide a,uint32_t coeff,unsigned digit) {
    const uint64_t top=uint64_t(a.z)|(uint64_t(a.w)<<32);
    return hostFloatFloorU32(hostMultiplyFloatRD(hostU64ToFloatRD(top),coeff+(digit<<23)));
}
W87_HD Value firstFloatWide(Value a,uint64_t low,uint32_t high,uint32_t np,uint32_t coeff) {
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
                 
        ".reg .f32 fz,fc,fq; .reg .u64 column;\n\t"
        "mov.b64 column,{t2,t3};\n\tcvt.rm.f32.u64 fz,column;\n\t"
        "mov.b32 fc,%10;\n\tmul.rm.f32 fq,fz,fc;\n\tcvt.rmi.u32.f32 q0,fq;\n\t"
        "mul.lo.u32 l,q0,%6;\n\tmul.hi.u32 m,q0,%6;\n\t"
        "mad.lo.cc.u32 m,q0,%7,m;\n\tmadc.hi.u32 h,q0,%7,0;\n\tmad.lo.u32 h,q0,%9,h;\n\t"
        "sub.cc.u32 %0,t0,l;\n\tsubc.cc.u32 %1,t1,m;\n\tsubc.u32 %2,t2,h;\n\t}"
        :"=r"(d.x),"=r"(d.y),"=r"(d.z)
        :"r"(a.x),"r"(a.y),"r"(a.z),"r"(uint32_t(low)),"r"(uint32_t(low>>32)),"r"(np),"r"(high),"r"(coeff));
    return d;
#else
    const Wide raw=retained_raw(a,uint32_t(low),uint32_t(low>>32),np,high);
    const Wide r=subtract(raw,product(hostQuotient(raw,coeff,0),low,high));
    assert(!r.w);return{r.x,r.y,r.z};
#endif
}
W87_HD Value lastFloatWide(Wide a,unsigned digit,uint64_t low,uint32_t high,uint32_t coeff) {
#if defined(__CUDA_ARCH__)
    Value r;
    asm volatile("{\n\t.reg .f32 fz,fc,fq; .reg .u64 column;\n\t"
        ".reg .u32 cb,q,x,y,z,p0,p1,p2;\n\tmov.b64 column,{%5,%6};\n\t"
        "cvt.rm.f32.u64 fz,column;\n\tshl.b32 cb,%7,23;\n\tadd.u32 cb,cb,%10;\n\tmov.b32 fc,cb;\n\t"
        "mul.rm.f32 fq,fz,fc;\n\tcvt.rmi.u32.f32 q,fq;\n\t"
        "shl.b32 x,%3,%7;\n\tshf.l.wrap.b32 y,%3,%4,%7;\n\tshf.l.wrap.b32 z,%4,%5,%7;\n\t"
        "mul.lo.u32 p0,q,%8;\n\tmul.hi.u32 p1,q,%8;\n\t"
        "mad.lo.cc.u32 p1,q,%9,p1;\n\tmadc.hi.u32 p2,q,%9,0;\n\tmad.lo.u32 p2,q,%11,p2;\n\t"
        "sub.cc.u32 %0,x,p0;\n\tsubc.cc.u32 %1,y,p1;\n\tsubc.u32 %2,z,p2;\n\t}"
        :"=r"(r.x),"=r"(r.y),"=r"(r.z)
        :"r"(a.x),"r"(a.y),"r"(a.z),"r"(a.w),"r"(digit),"r"(uint32_t(low)),"r"(uint32_t(low>>32)),"r"(coeff),"r"(high));
    return r;
#else
    const Wide x=shifted_wide(a,digit);
    const Wide r=subtract(x,product(hostQuotient(a,coeff,digit),low,high));
    assert(!r.w);return{r.x,r.y,r.z};
#endif
}

template<unsigned Bits>W87_HD uint32_t coefficient_bits(uint32_t mu){
#if defined(__CUDA_ARCH__)
    return __float_as_uint(__uint2float_rd(mu))-((Bits-33u)<<23);
#else
    return hostU32ToFloatRD(mu)-((Bits-33u)<<23);
#endif
}
template<unsigned Bits>W87_HD Value make_seed(uint64_t low,uint32_t high,uint32_t mu,uint32_t coeff){
    constexpr unsigned Cache=Bits+31;
    const auto p=product(mu,low,high);const auto r=subtract(Wide{0,0,0,0},p);
    Value a{r.x,r.y,r.z};
    const unsigned prefix=high>>(Bits-72),target=Bits==81?prefix>>1:prefix;
    unsigned remaining=target-(Cache-64);
    // Retain RCache and the existing 30-bit seed folds. Only the actual prefix changes.
    W87_UNROLL for(unsigned j=0;j<7;++j)if(remaining>=30){
        a=montgomery_columns::fold<Cache>(Wide{a.x,a.y,a.z,0},30,low,high,mu);remaining-=30;
    }
    if(remaining)a=montgomery_columns::fold<Cache>(Wide{a.x,a.y,a.z,0},remaining,low,high,mu);
    if constexpr(Bits==81){
        const auto np=optimized_narrow_euler::negative_inverse32(uint32_t(low));
        return lastFloatWide(retained_raw(a,uint32_t(low),uint32_t(low>>32),np,high),prefix&1,low,high,coeff);
    }
    return subtract_if_ge(a,Value{uint32_t(low),uint32_t(low>>32),high});
}
template<unsigned Bits>W87_HD unsigned pair_digit(uint64_t low,uint32_t high,unsigned group){
    const unsigned shift=Bits-11-2*group;
    if(shift>=63)return(high>>(shift-63))&3u;
    if(shift==62)return unsigned(((high&1u)<<1)|(low>>63));
    return unsigned((low>>1)>>shift)&3u;
}
template<unsigned Bits,bool Upper>W87_HD Value first(Value a,uint64_t low,uint32_t high,uint32_t np,uint32_t mu,uint32_t coeff){
#if defined(__CUDA_ARCH__)
    if constexpr(Bits<=85||!Upper)return retained_columns::square_float(a,uint32_t(low),uint32_t(low>>32),np,coeff,high);
    else return retained_columns::square_integer32<Bits+31>(a,uint32_t(low),uint32_t(low>>32),np,mu,high);
#else
    if constexpr(Bits<=85||!Upper)return firstFloatWide(a,low,high,np,coeff);
    else return montgomery_columns::square_fold<Bits+31>(a,uint32_t(low),uint32_t(low>>32),np,high,mu);
#endif
}
template<unsigned Bits,bool Upper>W87_HD Value state_before_terminal(uint64_t low,uint32_t high,uint32_t mu,uint32_t coeff,uint32_t np){
    constexpr unsigned Tail=(Bits&1)?2:1,Pairs=(Bits-9-Tail)/2;
    static_assert(Bits>=81&&Bits<=87,"Retained width-specific pair schedules");
    Value a=make_seed<Bits>(low,high,mu,coeff);
    W87_UNROLL for(unsigned group=0;group<Pairs;++group){
        a=first<Bits,Upper>(a,low,high,np,mu,coeff);
        const auto raw=retained_raw(a,uint32_t(low),uint32_t(low>>32),np,high);
        const auto digit=pair_digit<Bits>(low,high,group);
        if constexpr(Bits==81||(Bits==82&&!Upper))a=lastFloatWide(raw,digit,low,high,coeff);
        else {
#if defined(__CUDA_ARCH__)
            a=retained_columns::last_integer32<Bits+31>(raw,digit,low,mu,high);
#else
            a=montgomery_columns::fold<Bits+31>(raw,digit,low,high,mu);
#endif
        }
    }return a;
}
W87_HD uint32_t upper_product(uint32_t a,uint32_t b){
#if defined(__CUDA_ARCH__)
    return __umulhi(a,b);
#else
    return uint32_t(uint64_t(a)*b>>32);
#endif
}
W87_HD bool terminal(Wide a,uint64_t low,uint32_t high,unsigned digit,unsigned negative,uint32_t np,uint32_t maximum){
    const uint32_t q=0u-a.x*np;if(q>maximum)return false;
    const uint64_t p1=uint64_t(q)*uint32_t(low>>32)+upper_product(q,uint32_t(low)),p2=uint64_t(q)*high+(p1>>32);
    const uint64_t t=uint64_t(1)<<(32-digit),mask=uint64_t(0)-negative,delta=(t^mask)+negative;const uint32_t ext=0u-negative;
    Value target;
#if defined(__CUDA_ARCH__)
    asm("{\n\t.reg .u32 r1,r2,r3;\n\tadd.cc.u32 r1,%3,%6;\n\taddc.cc.u32 r2,%4,%7;\n\taddc.u32 r3,%5,%8;\n\t"
        "mov.u32 %0,r1;\n\tmov.u32 %1,r2;\n\tmov.u32 %2,r3;\n\t}"
        :"=r"(target.x),"=r"(target.y),"=r"(target.z)
        :"r"(uint32_t(p1)),"r"(uint32_t(p2)),"r"(uint32_t(p2>>32)),"r"(uint32_t(delta)),"r"(uint32_t(delta>>32)),"r"(ext));
#else
    uint64_t u=uint64_t(uint32_t(p1))+uint32_t(delta);target.x=uint32_t(u);
    u=uint64_t(uint32_t(p2))+uint32_t(delta>>32)+(u>>32);target.y=uint32_t(u);target.z=uint32_t(p2>>32)+ext+uint32_t(u>>32);
#endif
    return a.y==target.x&&a.z==target.y&&a.w==target.z;
}


template<unsigned Bits,bool Upper>W87_HD bool test_prepared(uint64_t low,uint32_t high,uint32_t mu,uint32_t coeff){
    if(!(low&1))return false;
    constexpr unsigned Tail=(Bits&1)?2:1;
    const auto np=optimized_narrow_euler::negative_inverse32(uint32_t(low));
    auto a=state_before_terminal<Bits,Upper>(low,high,mu,coeff,np);
    if constexpr(Tail==2)a=first<Bits,Upper>(a,low,high,np,mu,coeff);
    const auto raw=retained_raw(a,uint32_t(low),uint32_t(low>>32),np,high);
    // Raw(a)/N <4*high+1 for the proved a<2N hot bound; include the signed target.
    return terminal(raw,low,high,unsigned((low>>1)&((1u<<Tail)-1)),unsigned(((low>>1)^(low>>2))&1),np,(1u<<(Bits-62))+2);
}
}
#undef W87_HD
#undef W87_UNROLL
