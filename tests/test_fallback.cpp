#include "hft/instr.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

HFT_INSTR_DOMAIN(Fallback, "fallback", true);
HFT_INSTR_MARKER(begin_marker, Fallback, "begin");
HFT_INSTR_MARKER(end_marker, Fallback, "end");

static_assert(hft::instr::kActiveClockSource == hft::instr::kClockSteady);

}  // namespace

TEST(FallbackClock, RoundTrip) {
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
    EXPECT_EQ(result.clock_source, hft::instr::kClockSteady);
    EXPECT_EQ(result.tsc_hz, 1000000000ull);
    ASSERT_EQ(result.events.size(), 2u);
    EXPECT_EQ(result.events[0].tsc_aux, 0u);
    EXPECT_EQ(result.events[1].tsc_aux, 0u);
    EXPECT_EQ(result.events[0].name, "fallback/begin");
    EXPECT_EQ(result.events[1].name, "fallback/end");
    ASSERT_EQ(result.spans.size(), 1u);
    EXPECT_EQ(result.spans[0].name, "work");
    EXPECT_GE(result.spans[0].end_tsc, result.spans[0].start_tsc);
    EXPECT_FALSE(result.spans[0].cross_cpu);
    EXPECT_TRUE(result.has_stats);
    EXPECT_EQ(result.written, 2u);
    EXPECT_EQ(result.ring_dropped, 0u);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
