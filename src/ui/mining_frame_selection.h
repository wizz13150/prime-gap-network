#pragma once
namespace pgn::ui {
// Hardware telemetry stays independent. Only one feed owns the shared ticket
// tables/progress: a paused GPU observer cannot replace a selected CPU ticket.
class MiningFrameSelection {
    bool cpuSelected_{};
public:
    bool cpu(bool gpuRequested){if(gpuRequested)return false;cpuSelected_=true;return true;}
    bool gpu(bool gpuRequested){if(gpuRequested){cpuSelected_=false;return true;}return !cpuSelected_;}
};
}
