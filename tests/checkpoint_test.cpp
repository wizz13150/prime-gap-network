#include "engine/phase0/pgn_native_io.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <iostream>
int main() {
    auto dir = std::filesystem::temp_directory_path() / ("pgn-checkpoint-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directory(dir);
    const auto final = dir / "state", temporary = dir / "state.tmp";
    for (int i = 1; i <= 3; ++i) {
        FILE* f = fopen(temporary.string().c_str(), "wb");
        if (!f) return 1;
        fprintf(f, "checkpoint-%d", i);
        if (pgn_flush_checkpoint(f) != 0 || fclose(f) != 0 ||
            pgn_replace_checkpoint(temporary.string().c_str(), final.string().c_str()) != 0) return 2;
        std::ifstream input(final); std::string content; input >> content;
        if (content != "checkpoint-" + std::to_string(i)) return 3;
    }
    std::filesystem::remove(final); std::filesystem::remove(dir);
    std::cout << "3 replacement checkpoint commits PASS\n";
}
