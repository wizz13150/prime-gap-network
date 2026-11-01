#ifndef PGN_CONDITIONAL_WALK_CUH
#define PGN_CONDITIONAL_WALK_CUH
#ifndef PGN_MARK_TILE_BITS
#define PGN_MARK_TILE_BITS (1U << 19)
#endif
constexpr unsigned pgn_mark_tile_bits = PGN_MARK_TILE_BITS;
constexpr unsigned pgn_mark_tile_count = (1U << 28) / pgn_mark_tile_bits;
static_assert(pgn_mark_tile_bits == (1U << 18) || pgn_mark_tile_bits == (1U << 19),
              "Only the exact 32/64 KiB sieve representations are admitted here");
static_assert(pgn_mark_tile_count * pgn_mark_tile_bits == (1U << 28),
              "The tile representation must cover every original wheel slot");

// Finite qualification adapter, not a production scheduler. Two bitmap slots
// alternate: mark(i+1) and walk(i) are independent; advance waits for BOTH.
// Every descriptor has a distinct retained result/stat slot, so no result is
// overwritten while the host verifies it after the complete graph job.
#include "p0_walk_kern.cuh"
#ifdef PGN_VERIFY_GAPS
#include "pgn_mr96.cuh"
#endif
#if !defined(PGN_FUSED_SIEVE) || !defined(PGN_EULER96) || !defined(PGN_EULER96_FIXED_HIGH10)
#error Native high47 candidate adapter requires the explicit fused Euler96 arithmetic
#endif
struct PgnWalkDescriptor {
    uint64_t low, high;
    uint32_t bitmap_blocks, owned_blocks;
};
struct PgnClass30Origin {uint64_t low, high;};
__device__ __forceinline__ PgnClass30Origin pgn_class30_origin(PgnWalkDescriptor descriptor) {
    const uint64_t remainder=(descriptor.low%30+(descriptor.high%30)*16)%30;
    return {descriptor.low-remainder,descriptor.high-(descriptor.low<remainder?1:0)};
}
struct PgnWalkGraphState {
    uint32_t cursor, count, completed;
};
#ifdef PGN_PHASE_TIMING
// Diagnostic only, native sm_86: NVIDIA documents globaltimer as target-specific.
// Independent marker grids preserve data kernels and concurrent mark/walk.
struct PgnPhaseTiming { uint64_t stamp[8]; };
#ifdef PGN_CTA_CENSUS
// One atomic word contains two16-bit live CTA counts. Fixed grids5980/164
// cannot overflow either half. AtomicAdd's returned old value is the exact
// simultaneous peer count, unlike delayed post-kernel marker execution.
struct PgnCtaCensus { unsigned mark_entered,walk_entered,mark_saw_walk,walk_saw_mark; };
__device__ unsigned pgn_cta_live=0;
__device__ PgnCtaCensus pgn_cta_census[256];
__device__ __forceinline__ void pgn_cta_enter(unsigned index,bool mark){
 const unsigned old=atomicAdd(&pgn_cta_live,mark?1U:65536U);
 if(mark){atomicAdd(&pgn_cta_census[index].mark_entered,1U);if(old>>16)atomicAdd(&pgn_cta_census[index].mark_saw_walk,1U);}
 else{atomicAdd(&pgn_cta_census[index].walk_entered,1U);if(old&65535U)atomicAdd(&pgn_cta_census[index].walk_saw_mark,1U);}
}
__device__ __forceinline__ void pgn_cta_leave(bool mark){atomicSub(&pgn_cta_live,mark?1U:65536U);}
#endif
__global__ void pgn_graph_stamp(const PgnWalkGraphState* state,PgnPhaseTiming* trace,unsigned channel,int offset){
    const int index=int(state->cursor)+offset;
    if(index<0||unsigned(index)>=state->count||channel>=8)return;
    uint64_t timer;asm volatile("mov.u64 %0, %%globaltimer;" : "=l"(timer));
    trace[index].stamp[channel]=timer;
}
#endif
struct PgnWalkGraphBuffers {
    const uint64_t *primes, *invp, *r64;
    const uint32_t *item_pidx, *item_k0, *wheel_primes, *window_steps;
    uint32_t item_count, gap_min, result_capacity, blocks_per_slot;
    uint64_t *bitmaps;
    w_gaprec_t *results;
    unsigned long long *stats;
#ifdef PGN_PRP_CALL_CENSUS
    unsigned long long *call_counts;
#endif
#ifdef PGN_VERIFY_GAPS
    unsigned *proofs;
#endif
};
__global__ void pgn_graph_mark(const PgnWalkDescriptor *descriptors,
 const PgnWalkGraphState *state, PgnWalkGraphBuffers buffers, uint32_t ahead) {
    const uint32_t index=state->cursor+ahead;
    // The predicate is uniform for the entire CTA, before any shared barrier.
    if(index>=state->count)return;
    const PgnWalkDescriptor descriptor=descriptors[index];
    if(blockIdx.y>=descriptor.bitmap_blocks)return;
#ifdef PGN_CTA_CENSUS
    if(threadIdx.x==0)pgn_cta_enter(index,true);
    __syncthreads();
#endif
    // Keep the descriptor cursor separate from the sieve's class-30 base.
    // 2^64 mod30=16; rounding a sieve must not redefine reporting ownership.
    const auto origin=pgn_class30_origin(descriptor);
    constexpr uint64_t words=(1ULL<<28)/64;
    auto *bitmap=buffers.bitmaps+uint64_t(index&1u)*buffers.blocks_per_slot*words;
    class30_sieve_body(buffers.primes,buffers.invp,buffers.r64,
        buffers.item_pidx,buffers.item_k0,buffers.item_count,
        origin.low,origin.high,pgn_mark_tile_bits,pgn_mark_tile_count,bitmap,pgn_mark_tile_bits/64,
        buffers.wheel_primes,1);
#ifdef PGN_CTA_CENSUS
    __syncthreads();
    if(threadIdx.x==0)pgn_cta_leave(true);
#endif
}
__global__ void pgn_graph_walk(const PgnWalkDescriptor *descriptors,
 const PgnWalkGraphState *state, PgnWalkGraphBuffers buffers) {
    const uint32_t index=state->cursor;
    if(index>=state->count)return;
    const PgnWalkDescriptor descriptor=descriptors[index];
#ifdef PGN_CTA_CENSUS
    if(threadIdx.x==0)pgn_cta_enter(index,false);
    __syncthreads();
#endif
    const auto origin=pgn_class30_origin(descriptor);
    __shared__ uint32_t classes[8];
    __shared__ uint64_t batch_quotient;
    const uint64_t words=uint64_t(descriptor.bitmap_blocks)*((1ULL<<28)/64);
    if(threadIdx.x<8)classes[threadIdx.x]=c30_cls[threadIdx.x];
#ifdef PGN_EULER96_BATCH_Q
    if(threadIdx.x==0)batch_quotient=pgn_euler96::batchQuotient(origin.low,origin.high,words);
#else
    if(threadIdx.x==0)batch_quotient=0;
#endif
    __syncthreads();
    const auto *bitmap=buffers.bitmaps+uint64_t(index&1u)*buffers.blocks_per_slot*((1ULL<<28)/64);
    // Same ownership, Euler arithmetic and collective population as the paid
    // high47 candidate walk. The corresponding host rejects every other high word.
    w_owned_slice<3,47ULL>(bitmap,words,origin.low,origin.high,
        buffers.window_steps,buffers.gap_min,
        buffers.results+uint64_t(index)*buffers.result_capacity,
        buffers.result_capacity,buffers.stats+uint64_t(index)*2,
        classes,batch_quotient,uint64_t(gridDim.x)*blockDim.x,
        uint64_t(blockIdx.x)*blockDim.x+threadIdx.x
#ifdef PGN_PRP_CALL_CENSUS
        ,buffers.call_counts+uint64_t(index)*gridDim.x*blockDim.x
#endif
        );
#ifdef PGN_CTA_CENSUS
    __syncthreads();
    if(threadIdx.x==0)pgn_cta_leave(false);
#endif
}
struct PgnWalkMailbox {
    w_gaprec_t *results;
    unsigned long long *stats;
    volatile unsigned *ready, *stop, *done;
};
#ifdef PGN_VERIFY_GAPS
__global__ void pgn_graph_verify(const PgnWalkDescriptor *descriptors,
 const PgnWalkGraphState *state,PgnWalkGraphBuffers buffers) {
 const unsigned index=state->cursor;if(index>=state->count)return;
 const auto origin=pgn_class30_origin(descriptors[index]);
 const auto* source=buffers.results+uint64_t(index)*buffers.result_capacity;
 const unsigned count=source[0].gap;if(count>=buffers.result_capacity)return;
 for(unsigned row=1+blockIdx.x*blockDim.x+threadIdx.x;row<=count;row+=gridDim.x*blockDim.x){
  const auto gap=source[row];bool valid=gap.gap>0&&gap.slot<=UINT64_MAX-gap.gap;
  if(valid)valid=pgn_mr96::prime12(gap.slot,origin.high)==1&&pgn_mr96::prime12(gap.slot+gap.gap,origin.high)==1;
  // Every inner integer has a real compositeness witness. Basic divisors
  // reject most; a failed exact Euler/Jacobi congruence rejects the rest.
  for(unsigned delta=1;valid&&delta<gap.gap;++delta){const uint64_t n=gap.slot+delta;
   if(!(n&1)||(n%3+origin.high%3)%3==0||(n%5+origin.high%5)%5==0)continue;
   if(pgn_euler96::test(n,origin.high)&&pgn_mr96::prime12(n,origin.high)!=0)valid=false;
  }
  buffers.proofs[uint64_t(index)*buffers.result_capacity+row]=valid?1u:2u;
 }
}
#endif
__global__ void pgn_graph_advance(PgnWalkGraphState *state,unsigned *visits,
 PgnWalkGraphBuffers buffers,PgnWalkMailbox mailbox,cudaGraphConditionalHandle handle) {
    const unsigned finished=state->cursor;
    const auto *source=buffers.results+uint64_t(finished)*buffers.result_capacity;
    const unsigned reported=source[0].gap;
    // One publisher thread owns the host payload and then its release flag.
    // Worker atomics stay in device VRAM. No GPU atomic targets host memory.
    if(reported>=buffers.result_capacity){
        __threadfence_system();*mailbox.done=2u;
        cudaGraphSetConditional(handle,0);return;
    }
#ifdef PGN_VERIFY_GAPS
    // 2 means a paid, invalid proof; 0 may mean verification interrupted.
    // A known failure wins over STOP. STOP is checked BEFORE any publication,
    // cursor increment or accepted flag, so an unpaid descriptor stays absent.
    for(unsigned row=1;row<=reported;++row)if(buffers.proofs[uint64_t(finished)*buffers.result_capacity+row]==2u){
        __threadfence_system();*mailbox.done=3u;cudaGraphSetConditional(handle,0);return;
    }
#endif
    if(*mailbox.stop){__threadfence_system();*mailbox.done=1u;cudaGraphSetConditional(handle,0);return;}
#ifdef PGN_VERIFY_GAPS
    for(unsigned row=1;row<=reported;++row)if(buffers.proofs[uint64_t(finished)*buffers.result_capacity+row]!=1u){
        __threadfence_system();*mailbox.done=3u;cudaGraphSetConditional(handle,0);return;
    }
#endif
    auto *target=mailbox.results+uint64_t(finished)*buffers.result_capacity;
    for(unsigned row=0;row<=reported;++row)target[row]=source[row];
    for(unsigned s=0;s<2;++s)mailbox.stats[2*finished+s]=buffers.stats[2*finished+s];
    ++visits[finished];
    ++state->cursor;
    ++state->completed;
    // Naturally aligned single-writer 32-bit flags, not cross-device atomics.
    // The dependent walk is finished and the payload remains immutable.
    __threadfence_system();mailbox.ready[finished]=1u;
    const bool more=state->cursor<state->count&&!*mailbox.stop;
    if(!more){__threadfence_system();*mailbox.done=1u;}
    cudaGraphSetConditional(handle,more?1:0);
}
#endif
