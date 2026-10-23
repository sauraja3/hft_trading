#include "hft/instr.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

int g_failed = 0;

void expect(bool cond, const char* expr, const char* file, int line) {
    if (cond) return;
    std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
    ++g_failed;
}

#define EXPECT(cond) expect(static_cast<bool>(cond), #cond, __FILE__, __LINE__)

HFT_INSTR_DOMAIN(Fallback, "fallback", true);
HFT_INSTR_MARKER(begin_marker, Fallback, "begin");
HFT_INSTR_MARKER(end_marker, Fallback, "end");

static_assert(hft::instr::kActiveClockSource == hft::instr::kClockSteady);

}  // namespace

int main() {
    const auto dir = std::filesystem::temp_directory_path() /
                     ("hft-instr-fallback-" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    const auto trace = dir / "fallback.bin";
    const auto config = dir / "spans.txt";
    {
        std::ofstream out(config);
        out << "span work fallback/begin fallback/end\n";
    }

    {
        hft::instr::Recorder::Config cfg;
        cfg.path = trace;
        cfg.capacity = 64;
        cfg.start_thread = true;
        cfg.calibrate_hz = false;
        cfg.poll = std::chrono::microseconds(50);
        hft::instr::Recorder recorder(cfg);
        begin_marker.record(recorder);
        end_marker.record(recorder);
        recorder.flush();
    }

    const auto result = hft::instr::decode_trace(trace, config);
    EXPECT(result.clock_source == hft::instr::kClockSteady);
    EXPECT(result.tsc_hz == 1000000000ull);
    EXPECT(result.events.size() == 2);
    EXPECT(result.events[0].tsc_aux == 0);
    EXPECT(result.events[1].tsc_aux == 0);
    EXPECT(result.events[0].name == "fallback/begin");
    EXPECT(result.events[1].name == "fallback/end");
    EXPECT(result.spans.size() == 1);
    EXPECT(result.spans[0].name == "work");
    EXPECT(result.spans[0].end_tsc >= result.spans[0].start_tsc);
    EXPECT(!result.spans[0].cross_cpu);
    EXPECT(result.has_stats);
    EXPECT(result.written == 2);
    EXPECT(result.ring_dropped == 0);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);

    if (g_failed != 0) {
        std::fprintf(stderr, "%d assertion(s) failed\n", g_failed);
        return 1;
    }
    std::printf("fallback tests ok\n");
    return 0;
}
