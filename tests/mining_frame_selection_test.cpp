#include "ui/mining_frame_selection.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
int main(){
 pgn::ui::MiningFrameSelection frames;
 assert(frames.gpu(false));
 for(int i=0;i<200;++i){assert(frames.cpu(false));assert(!frames.gpu(false));}
 // Explicit GPU ON takes the view; CPU telemetry may continue independently.
 assert(frames.gpu(true));assert(!frames.cpu(true));assert(frames.gpu(true));
 // CPU takes it again when GPU mining is OFF, and keeps the paused CPU result.
 assert(frames.cpu(false));assert(!frames.gpu(false));assert(!frames.gpu(false));
}
