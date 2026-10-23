#include "hft/instr.hpp"

#include "decode_path.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sched.h>
#include <sstream>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

int g_seq = 0;

class TempDir {
public:
    TempDir() {
        path_ = std::filesystem::temp_directory_path() /
                ("hft-instr-" + std::to_string(::getpid()) + "-" + std::to_string(++g_seq));
        std::filesystem::create_directories(path_);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    std::filesystem::path file(const std::string& name) const { return path_ / name; }

private:
    std::filesystem::path path_;
};

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path);
    ASSERT_TRUE(static_cast<bool>(out));
    out << text;
}

std::string shell_quote(const std::string& text) {
    std::string quoted = "'";
    for (char ch : text) {
        if (ch == '\'') quoted += "'\\''";
        else quoted += ch;
    }
    quoted += "'";
    return quoted;
}

hft::instr::Recorder::Config make_cfg(std::filesystem::path path, std::size_t capacity, bool thread,
                                      bool calibrate = false) {
    hft::instr::Recorder::Config config;
    config.path = std::move(path);
    config.capacity = capacity;
    config.start_thread = thread;
    config.calibrate_hz = calibrate;
    config.poll = std::chrono::microseconds(50);
    return config;
}

bool pin_cpu0() {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    return ::sched_setaffinity(0, sizeof(set), &set) == 0;
}

HFT_INSTR_DOMAIN(MarketData, "market_data", true);
HFT_INSTR_DOMAIN(Strategy, "strategy", true);
HFT_INSTR_DOMAIN(MarketDataDebug, "market_data_debug", false);

HFT_INSTR_MARKER(outer_b, MarketData, "outer_b");
HFT_INSTR_MARKER(outer_e, MarketData, "outer_e");
HFT_INSTR_MARKER(mid_b, MarketData, "mid_b");
HFT_INSTR_MARKER(mid_e, MarketData, "mid_e");
HFT_INSTR_MARKER(inner_b, MarketData, "inner_b");
HFT_INSTR_MARKER(inner_e, MarketData, "inner_e");
HFT_INSTR_MARKER(sib_b, MarketData, "sib_b");
HFT_INSTR_MARKER(sib_e, MarketData, "sib_e");
HFT_INSTR_MARKER(nest_b, MarketData, "nest_b");
HFT_INSTR_MARKER(nest_e, MarketData, "nest_e");
HFT_INSTR_MARKER(gate_b, MarketData, "gate_b");
HFT_INSTR_MARKER(gate_e, MarketData, "gate_e");
HFT_INSTR_MARKER(need, MarketData, "need");
HFT_INSTR_MARKER(kept, MarketData, "kept");
HFT_INSTR_MARKER(fill_a, MarketData, "fill_a");
HFT_INSTR_MARKER(fill_b, MarketData, "fill_b");
HFT_INSTR_MARKER(trip_b, MarketData, "trip_b");
HFT_INSTR_MARKER(trip_e, MarketData, "trip_e");
HFT_INSTR_MARKER(md_start, MarketData, "start");
HFT_INSTR_MARKER(strat_start, Strategy, "start");
HFT_INSTR_MARKER(noise, MarketDataDebug, "noise");
HFT_INSTR_MARKER(loop_b, MarketData, "loop_b");
HFT_INSTR_MARKER(loop_e, MarketData, "loop_e");

static_assert(md_start.kId != strat_start.kId, "domains namespace marker ids");
static_assert(md_start.kId == hft::instr::make_marker_id("market_data", "start"));
static_assert(strat_start.kId == hft::instr::make_marker_id("strategy", "start"));
static_assert(!noise.kEnabled);
static_assert(kept.kEnabled);

std::vector<const hft::instr::DecodedSpan*> named(const hft::instr::DecodeResult& result,
                                                  const std::string& name) {
    std::vector<const hft::instr::DecodedSpan*> out;
    for (const auto& span : result.spans) {
        if (span.name == name) out.push_back(&span);
    }
    return out;
}

bool contains(const hft::instr::DecodedSpan& outer, const hft::instr::DecodedSpan& inner) {
    return outer.start_tsc <= inner.start_tsc && inner.end_tsc <= outer.end_tsc &&
           (outer.start_tsc < inner.start_tsc || inner.end_tsc < outer.end_tsc);
}

const char* kSpanConfig = R"(
# pairs are data. The decoder is not rebuilt to change them.
span outer market_data/outer_b market_data/outer_e
span mid market_data/mid_b market_data/mid_e
span inner market_data/inner_b market_data/inner_e
span sibling market_data/sib_b market_data/sib_e
span nest market_data/nest_b market_data/nest_e
span gated market_data/gate_b market_data/gate_e requires market_data/need
)";

TEST(Instr, IdCollisionIsRejected) {
    std::vector<hft::instr::detail::NameEntry> entries = {
        {1u, "book", "start"},
        {1u, "strategy", "start"},
    };
    bool threw = false;
    try {
        hft::instr::detail::ensure_unique_ids(entries);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

TEST(Instr, DisabledRecordsNothing) {
    TempDir dir;
    const auto trace = dir.file("disabled.bin");
    {
        hft::instr::Recorder recorder(make_cfg(trace, 64, false));
        noise.record(recorder);
        kept.record(recorder);
        noise.record(recorder);
        EXPECT_TRUE(recorder.dropped() == 0);
        recorder.flush();
    }
    write_text(dir.file("spans.txt"), "# none\n");
    const auto result = hft::instr::decode_trace(trace, dir.file("spans.txt"));
    EXPECT_TRUE(result.events.size() == 1);
    EXPECT_TRUE(result.events[0].name == "market_data/kept");
    EXPECT_TRUE(result.events[0].marker_id == kept.kId);
    EXPECT_TRUE(result.has_stats);
    EXPECT_TRUE(result.ring_dropped == 0);
    EXPECT_TRUE(result.written == 1);
}

TEST(Instr, OverflowDrops) {
    TempDir dir;
    const auto trace = dir.file("overflow.bin");
    {
        hft::instr::Recorder recorder(make_cfg(trace, 8, false));
        for (int i = 0; i < 8; ++i) fill_a.record(recorder);
        for (int i = 0; i < 5; ++i) fill_b.record(recorder);
        EXPECT_TRUE(recorder.dropped() == 5);
        recorder.flush();
    }
    write_text(dir.file("spans.txt"), "# none\n");
    const auto result = hft::instr::decode_trace(trace, dir.file("spans.txt"));
    EXPECT_TRUE(result.events.size() == 8);
    for (const auto& event : result.events) {
        EXPECT_TRUE(event.name == "market_data/fill_a");
        EXPECT_TRUE(event.marker_id == fill_a.kId);
    }
    EXPECT_TRUE(result.has_stats);
    EXPECT_TRUE(result.ring_dropped == 5);
    EXPECT_TRUE(result.written == 8);
    EXPECT_TRUE(result.dropped == 5);
}

TEST(Instr, RoundTripFlush) {
    TempDir dir;
    const auto trace = dir.file("trip.bin");
    const auto spans = dir.file("spans.txt");
    const auto empty = dir.file("empty.txt");
    write_text(spans, "span trip market_data/trip_b market_data/trip_e\n");
    write_text(empty, "# no spans\n");

    {
        hft::instr::Recorder recorder(make_cfg(trace, 128, true));
        trip_b.record(recorder);
        trip_e.record(recorder);
        recorder.flush();

        const auto live = hft::instr::decode_trace(trace, spans);
        EXPECT_TRUE(live.events.size() == 2);
        EXPECT_TRUE(live.events[0].name == "market_data/trip_b");
        EXPECT_TRUE(live.events[1].name == "market_data/trip_e");
        EXPECT_TRUE(live.events[0].tsc <= live.events[1].tsc);
        EXPECT_TRUE(live.spans.size() == 1);
        EXPECT_TRUE(live.spans[0].name == "trip");
        EXPECT_TRUE(live.spans[0].start_tsc == live.events[0].tsc);
        EXPECT_TRUE(live.spans[0].end_tsc == live.events[1].tsc);
        EXPECT_TRUE(live.spans[0].duration == live.spans[0].end_tsc - live.spans[0].start_tsc);
        EXPECT_TRUE(live.spans[0].depth == 0);
        EXPECT_TRUE(live.spans[0].parent.empty());

        const auto bare = hft::instr::decode_trace(trace, empty);
        EXPECT_TRUE(bare.events.size() == 2);
        EXPECT_TRUE(bare.spans.empty());
    }

    const auto closed = hft::instr::decode_trace(trace, spans);
    EXPECT_TRUE(closed.has_stats);
    EXPECT_TRUE(closed.ring_dropped == 0);
    EXPECT_TRUE(closed.written == 2);
    EXPECT_TRUE(closed.spans.size() == 1);
}

TEST(Instr, TwoDomains) {
    TempDir dir;
    const auto trace = dir.file("domains.bin");
    {
        hft::instr::Recorder recorder(make_cfg(trace, 64, true));
        md_start.record(recorder);
        strat_start.record(recorder);
        recorder.flush();
    }
    write_text(dir.file("spans.txt"), "# none\n");
    const auto result = hft::instr::decode_trace(trace, dir.file("spans.txt"));
    EXPECT_TRUE(result.events.size() == 2);
    EXPECT_TRUE(result.events[0].name == "market_data/start");
    EXPECT_TRUE(result.events[1].name == "strategy/start");
    EXPECT_TRUE(result.events[0].marker_id != result.events[1].marker_id);
    EXPECT_TRUE(result.events[0].marker_id == md_start.kId);
    EXPECT_TRUE(result.events[1].marker_id == strat_start.kId);
}

TEST(Instr, NestedSiblingRequired) {
    const bool pinned = pin_cpu0();
    TempDir dir;
    const auto trace = dir.file("spans.bin");
    const auto config = dir.file("spans.txt");
    write_text(config, kSpanConfig);

    int enabled_records = 0;
    {
        hft::instr::Recorder recorder(make_cfg(trace, 256, true));
        auto mark = [&](auto marker) {
            marker.record(recorder);
            if (marker.kEnabled) ++enabled_records;
        };

        // Three-level nest: outer contains mid contains inner.
        mark(outer_b);
        mark(mid_b);
        mark(inner_b);
        mark(inner_e);
        mark(mid_e);
        mark(outer_e);

        // Two siblings. Neither contains the other.
        mark(sib_b);
        mark(sib_e);
        mark(sib_b);
        mark(sib_e);

        // Same marker pair, nested.
        mark(nest_b);
        mark(nest_b);
        mark(nest_e);
        mark(nest_e);

        // Required marker absent: drop. Disabled marker must not count as need.
        mark(gate_b);
        mark(noise);
        mark(gate_e);

        // Required marker present: keep.
        mark(gate_b);
        mark(need);
        mark(gate_e);

        // need hits the outer gate only. The inner gate opens after it and is dropped.
        mark(gate_b);
        mark(need);
        mark(gate_b);
        mark(gate_e);
        mark(gate_e);

        // need after end does not revive the span.
        mark(gate_b);
        mark(gate_e);
        mark(need);

        // Unmatched end, then an unmatched begin. Neither becomes a span.
        mark(sib_e);
        mark(sib_b);

        recorder.flush();
    }

    const auto result = hft::instr::decode_trace(trace, config);
    EXPECT_TRUE(result.events.size() == static_cast<std::size_t>(enabled_records));
    EXPECT_TRUE(result.has_stats);
    EXPECT_TRUE(result.ring_dropped == 0);
    EXPECT_TRUE(result.written == result.events.size());

    for (const auto& event : result.events) {
        EXPECT_TRUE(!event.name.empty());
        EXPECT_TRUE(event.name.find("market_data_debug") == std::string::npos);
    }
    for (std::size_t i = 1; i < result.events.size(); ++i) {
        EXPECT_TRUE(result.events[i - 1].tsc <= result.events[i].tsc);
    }

    const auto outers = named(result, "outer");
    const auto mids = named(result, "mid");
    const auto inners = named(result, "inner");
    EXPECT_TRUE(outers.size() == 1);
    EXPECT_TRUE(mids.size() == 1);
    EXPECT_TRUE(inners.size() == 1);
    if (outers.size() == 1 && mids.size() == 1 && inners.size() == 1) {
        EXPECT_TRUE(contains(*outers[0], *mids[0]));
        EXPECT_TRUE(contains(*mids[0], *inners[0]));
        EXPECT_TRUE(outers[0]->depth == 0);
        EXPECT_TRUE(outers[0]->parent.empty());
        EXPECT_TRUE(mids[0]->depth == 1);
        EXPECT_TRUE(mids[0]->parent == "outer");
        EXPECT_TRUE(mids[0]->parent_start == outers[0]->start_tsc);
        EXPECT_TRUE(inners[0]->depth == 2);
        EXPECT_TRUE(inners[0]->parent == "mid");
        EXPECT_TRUE(inners[0]->parent_start == mids[0]->start_tsc);
    }

    const auto siblings = named(result, "sibling");
    EXPECT_TRUE(siblings.size() == 2);
    if (siblings.size() == 2) {
        EXPECT_TRUE(siblings[0]->depth == 0);
        EXPECT_TRUE(siblings[1]->depth == 0);
        EXPECT_TRUE(siblings[0]->parent.empty());
        EXPECT_TRUE(siblings[1]->parent.empty());
        EXPECT_TRUE(siblings[0]->end_tsc <= siblings[1]->start_tsc);
        EXPECT_TRUE(!contains(*siblings[0], *siblings[1]));
    }

    const auto nests = named(result, "nest");
    EXPECT_TRUE(nests.size() == 2);
    if (nests.size() == 2) {
        EXPECT_TRUE(contains(*nests[0], *nests[1]));
        EXPECT_TRUE(nests[0]->depth == 0);
        EXPECT_TRUE(nests[0]->parent.empty());
        EXPECT_TRUE(nests[1]->depth == 1);
        EXPECT_TRUE(nests[1]->parent == "nest");
        EXPECT_TRUE(nests[1]->parent_start == nests[0]->start_tsc);
    }

    // absent, present, outer-of-nested kept; inner-of-nested dropped; need-after-end dropped.
    const auto gated = named(result, "gated");
    EXPECT_TRUE(gated.size() == 2);
    for (const auto* span : gated) {
        EXPECT_TRUE(span->depth == 0);
        EXPECT_TRUE(span->parent.empty());
        EXPECT_TRUE(span->end_tsc >= span->start_tsc);
    }

    if (pinned) {
        for (const auto& span : result.spans) EXPECT_TRUE(!span.cross_cpu);
    }

    if (HasFailure()) {
        std::cerr << "span dump:\n";
        hft::instr::write_span_report(std::cerr, result);
    }
}

TEST(Instr, BadConfigThrows) {
    TempDir dir;
    const auto trace = dir.file("one.bin");
    {
        hft::instr::Recorder recorder(make_cfg(trace, 32, false));
        kept.record(recorder);
        recorder.flush();
    }
    write_text(dir.file("bad.txt"), "span same market_data/kept market_data/kept\n");
    bool threw = false;
    try {
        (void)hft::instr::decode_trace(trace, dir.file("bad.txt"));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

TEST(Instr, CalibratedHz) {
    if (hft::instr::kActiveClockSource != hft::instr::kClockRdtscp) {
        GTEST_SKIP() << "TSC calibration runs only on the rdtscp build";
    }
    TempDir dir;
    const auto trace = dir.file("hz.bin");
    {
        hft::instr::Recorder recorder(make_cfg(trace, 32, false, true));
        kept.record(recorder);
        recorder.flush();
    }
    write_text(dir.file("spans.txt"), "# none\n");
    const auto result = hft::instr::decode_trace(trace, dir.file("spans.txt"));
    EXPECT_TRUE(result.clock_source == hft::instr::kClockRdtscp);
    EXPECT_TRUE(result.tsc_hz > 0);
}

TEST(Instr, BeginEndInALoop) {
    constexpr int kPairs = 100;
    TempDir dir;
    const auto trace = dir.file("loop.bin");
    const auto config = dir.file("spans.txt");
    write_text(config, "span lap market_data/loop_b market_data/loop_e\n");

    {
        hft::instr::Recorder recorder(make_cfg(trace, 1024, true));
        for (int i = 0; i < kPairs; ++i) {
            loop_b.record(recorder);
            loop_e.record(recorder);
        }
        recorder.flush();
    }

    const auto result = hft::instr::decode_trace(trace, config);
    ASSERT_EQ(result.events.size(), static_cast<std::size_t>(kPairs * 2));
    ASSERT_EQ(result.spans.size(), static_cast<std::size_t>(kPairs));
    EXPECT_TRUE(result.has_stats);
    EXPECT_EQ(result.ring_dropped, 0u);
    for (std::size_t i = 0; i < result.spans.size(); ++i) {
        const auto& span = result.spans[i];
        EXPECT_EQ(span.name, "lap");
        EXPECT_EQ(span.depth, 0);
        EXPECT_TRUE(span.parent.empty());
        EXPECT_GE(span.end_tsc, span.start_tsc);
        EXPECT_EQ(span.start_tsc, result.events[i * 2].tsc);
        EXPECT_EQ(span.end_tsc, result.events[i * 2 + 1].tsc);
        if (i > 0) {
            EXPECT_LE(result.spans[i - 1].end_tsc, span.start_tsc);
            EXPECT_FALSE(contains(result.spans[i - 1], span));
        }
    }
}

std::vector<std::string> span_names_from_report(const std::filesystem::path& path) {
    std::ifstream in(path);
    EXPECT_TRUE(static_cast<bool>(in));
    std::vector<std::string> names;
    std::string line;
    bool header = true;
    while (std::getline(in, line)) {
        if (header) {
            header = false;
            continue;
        }
        if (line.empty()) continue;
        names.push_back(line.substr(0, line.find('\t')));
    }
    return names;
}

int count_name(const std::vector<std::string>& names, const std::string& name) {
    int count = 0;
    for (const auto& item : names) {
        if (item == name) ++count;
    }
    return count;
}

TEST(Instr, DecodeExecutableMultipleConfigs) {
    TempDir dir;
    const auto trace = dir.file("cli.bin");
    const auto full_config = dir.file("full.txt");
    const auto sibling_only = dir.file("siblings.txt");
    const auto full_out = dir.file("full.tsv");
    const auto sibling_out = dir.file("siblings.tsv");
    write_text(full_config, kSpanConfig);
    write_text(sibling_only, "span sibling market_data/sib_b market_data/sib_e\n");

    {
        hft::instr::Recorder recorder(make_cfg(trace, 256, true));
        outer_b.record(recorder);
        inner_b.record(recorder);
        inner_e.record(recorder);
        outer_e.record(recorder);
        sib_b.record(recorder);
        sib_e.record(recorder);
        sib_b.record(recorder);
        sib_e.record(recorder);
        gate_b.record(recorder);
        gate_e.record(recorder);
        gate_b.record(recorder);
        need.record(recorder);
        gate_e.record(recorder);
        recorder.flush();
    }

    const std::string binary = HFT_INSTR_DECODE_EXECUTABLE;
    const std::string full_cmd = shell_quote(binary) + " " + shell_quote(trace.string()) + " " +
                                 shell_quote(full_config.string()) + " " + shell_quote(full_out.string());
    const std::string sibling_cmd = shell_quote(binary) + " " + shell_quote(trace.string()) + " " +
                                    shell_quote(sibling_only.string()) + " " +
                                    shell_quote(sibling_out.string());
    ASSERT_EQ(std::system(full_cmd.c_str()), 0);
    ASSERT_EQ(std::system(sibling_cmd.c_str()), 0);

    const auto full_names = span_names_from_report(full_out);
    EXPECT_EQ(count_name(full_names, "outer"), 1);
    EXPECT_EQ(count_name(full_names, "inner"), 1);
    EXPECT_EQ(count_name(full_names, "sibling"), 2);
    EXPECT_EQ(count_name(full_names, "gated"), 1);
    EXPECT_EQ(count_name(full_names, "mid"), 0);
    EXPECT_EQ(count_name(full_names, "nest"), 0);

    const auto sibling_names = span_names_from_report(sibling_out);
    ASSERT_EQ(sibling_names.size(), 2u);
    EXPECT_EQ(sibling_names[0], "sibling");
    EXPECT_EQ(sibling_names[1], "sibling");
}

}  // namespace
