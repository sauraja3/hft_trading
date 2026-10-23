#include "hft/instr.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sched.h>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

int g_failed = 0;

void expect(bool cond, const char* expr, const char* file, int line) {
    if (cond) return;
    std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
    ++g_failed;
}

#define EXPECT(cond) expect(static_cast<bool>(cond), #cond, __FILE__, __LINE__)

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
    EXPECT(static_cast<bool>(out));
    out << text;
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

using MarketData = hft::instr::Domain<"market_data", true>;
using Strategy = hft::instr::Domain<"strategy", true>;
using MarketDataDebug = hft::instr::Domain<"market_data_debug", false>;

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

void test_id_collision_is_rejected() {
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
    EXPECT(threw);
}

void test_disabled_records_nothing() {
    TempDir dir;
    const auto trace = dir.file("disabled.bin");
    {
        hft::instr::Recorder recorder(make_cfg(trace, 64, false));
        noise.record(recorder);
        kept.record(recorder);
        noise.record(recorder);
        EXPECT(recorder.dropped() == 0);
        recorder.flush();
    }
    write_text(dir.file("spans.txt"), "# none\n");
    const auto result = hft::instr::decode_trace(trace, dir.file("spans.txt"));
    EXPECT(result.events.size() == 1);
    EXPECT(result.events[0].name == "market_data/kept");
    EXPECT(result.events[0].marker_id == kept.kId);
    EXPECT(result.has_stats);
    EXPECT(result.ring_dropped == 0);
    EXPECT(result.written == 1);
}

void test_overflow_drops() {
    TempDir dir;
    const auto trace = dir.file("overflow.bin");
    {
        hft::instr::Recorder recorder(make_cfg(trace, 8, false));
        for (int i = 0; i < 8; ++i) fill_a.record(recorder);
        for (int i = 0; i < 5; ++i) fill_b.record(recorder);
        EXPECT(recorder.dropped() == 5);
        recorder.flush();
    }
    write_text(dir.file("spans.txt"), "# none\n");
    const auto result = hft::instr::decode_trace(trace, dir.file("spans.txt"));
    EXPECT(result.events.size() == 8);
    for (const auto& event : result.events) {
        EXPECT(event.name == "market_data/fill_a");
        EXPECT(event.marker_id == fill_a.kId);
    }
    EXPECT(result.has_stats);
    EXPECT(result.ring_dropped == 5);
    EXPECT(result.written == 8);
    EXPECT(result.dropped == 5);
}

void test_round_trip_flush() {
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
        EXPECT(live.events.size() == 2);
        EXPECT(live.events[0].name == "market_data/trip_b");
        EXPECT(live.events[1].name == "market_data/trip_e");
        EXPECT(live.events[0].tsc <= live.events[1].tsc);
        EXPECT(live.spans.size() == 1);
        EXPECT(live.spans[0].name == "trip");
        EXPECT(live.spans[0].start_tsc == live.events[0].tsc);
        EXPECT(live.spans[0].end_tsc == live.events[1].tsc);
        EXPECT(live.spans[0].duration == live.spans[0].end_tsc - live.spans[0].start_tsc);
        EXPECT(live.spans[0].depth == 0);
        EXPECT(live.spans[0].parent.empty());

        const auto bare = hft::instr::decode_trace(trace, empty);
        EXPECT(bare.events.size() == 2);
        EXPECT(bare.spans.empty());
    }

    const auto closed = hft::instr::decode_trace(trace, spans);
    EXPECT(closed.has_stats);
    EXPECT(closed.ring_dropped == 0);
    EXPECT(closed.written == 2);
    EXPECT(closed.spans.size() == 1);
}

void test_two_domains() {
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
    EXPECT(result.events.size() == 2);
    EXPECT(result.events[0].name == "market_data/start");
    EXPECT(result.events[1].name == "strategy/start");
    EXPECT(result.events[0].marker_id != result.events[1].marker_id);
    EXPECT(result.events[0].marker_id == md_start.kId);
    EXPECT(result.events[1].marker_id == strat_start.kId);
}

void test_nested_sibling_required() {
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
    EXPECT(result.events.size() == static_cast<std::size_t>(enabled_records));
    EXPECT(result.has_stats);
    EXPECT(result.ring_dropped == 0);
    EXPECT(result.written == result.events.size());

    for (const auto& event : result.events) {
        EXPECT(!event.name.empty());
        EXPECT(event.name.find("market_data_debug") == std::string::npos);
    }
    for (std::size_t i = 1; i < result.events.size(); ++i) {
        EXPECT(result.events[i - 1].tsc <= result.events[i].tsc);
    }

    const auto outers = named(result, "outer");
    const auto mids = named(result, "mid");
    const auto inners = named(result, "inner");
    EXPECT(outers.size() == 1);
    EXPECT(mids.size() == 1);
    EXPECT(inners.size() == 1);
    if (outers.size() == 1 && mids.size() == 1 && inners.size() == 1) {
        EXPECT(contains(*outers[0], *mids[0]));
        EXPECT(contains(*mids[0], *inners[0]));
        EXPECT(outers[0]->depth == 0);
        EXPECT(outers[0]->parent.empty());
        EXPECT(mids[0]->depth == 1);
        EXPECT(mids[0]->parent == "outer");
        EXPECT(mids[0]->parent_start == outers[0]->start_tsc);
        EXPECT(inners[0]->depth == 2);
        EXPECT(inners[0]->parent == "mid");
        EXPECT(inners[0]->parent_start == mids[0]->start_tsc);
    }

    const auto siblings = named(result, "sibling");
    EXPECT(siblings.size() == 2);
    if (siblings.size() == 2) {
        EXPECT(siblings[0]->depth == 0);
        EXPECT(siblings[1]->depth == 0);
        EXPECT(siblings[0]->parent.empty());
        EXPECT(siblings[1]->parent.empty());
        EXPECT(siblings[0]->end_tsc <= siblings[1]->start_tsc);
        EXPECT(!contains(*siblings[0], *siblings[1]));
    }

    const auto nests = named(result, "nest");
    EXPECT(nests.size() == 2);
    if (nests.size() == 2) {
        EXPECT(contains(*nests[0], *nests[1]));
        EXPECT(nests[0]->depth == 0);
        EXPECT(nests[0]->parent.empty());
        EXPECT(nests[1]->depth == 1);
        EXPECT(nests[1]->parent == "nest");
        EXPECT(nests[1]->parent_start == nests[0]->start_tsc);
    }

    // absent, present, outer-of-nested kept; inner-of-nested dropped; need-after-end dropped.
    const auto gated = named(result, "gated");
    EXPECT(gated.size() == 2);
    for (const auto* span : gated) {
        EXPECT(span->depth == 0);
        EXPECT(span->parent.empty());
        EXPECT(span->end_tsc >= span->start_tsc);
    }

    if (pinned) {
        for (const auto& span : result.spans) EXPECT(!span.cross_cpu);
    }

    if (g_failed) {
        std::cerr << "span dump:\n";
        hft::instr::write_span_report(std::cerr, result);
    }
}

void test_bad_config_throws() {
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
    EXPECT(threw);
}

void test_calibrated_hz() {
    if (hft::instr::kActiveClockSource != hft::instr::kClockRdtscp) return;
    TempDir dir;
    const auto trace = dir.file("hz.bin");
    {
        hft::instr::Recorder recorder(make_cfg(trace, 32, false, true));
        kept.record(recorder);
        recorder.flush();
    }
    write_text(dir.file("spans.txt"), "# none\n");
    const auto result = hft::instr::decode_trace(trace, dir.file("spans.txt"));
    EXPECT(result.clock_source == hft::instr::kClockRdtscp);
    EXPECT(result.tsc_hz > 0);
}

}  // namespace

int main() {
    test_id_collision_is_rejected();
    test_disabled_records_nothing();
    test_overflow_drops();
    test_round_trip_flush();
    test_two_domains();
    test_nested_sibling_required();
    test_bad_config_throws();
    test_calibrated_hz();
    if (g_failed != 0) {
        std::fprintf(stderr, "%d assertion(s) failed\n", g_failed);
        return 1;
    }
    std::printf("instr tests ok\n");
    return 0;
}
