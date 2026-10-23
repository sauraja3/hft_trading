#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace hft::instr {

struct DecodedEvent {
    uint64_t tsc = 0;
    uint32_t marker_id = 0;
    uint32_t tsc_aux = 0;
    std::string name;
};

struct DecodedSpan {
    std::string name;
    uint64_t start_tsc = 0;
    uint64_t end_tsc = 0;
    uint64_t duration = 0;
    int depth = 0;
    std::string parent;
    uint64_t parent_start = 0;
    uint32_t begin_aux = 0;
    uint32_t end_aux = 0;
    bool cross_cpu = false;
};

struct DecodeResult {
    std::vector<DecodedEvent> events;
    std::vector<DecodedSpan> spans;
    uint64_t ring_dropped = 0;
    uint64_t io_dropped = 0;
    uint64_t dropped = 0;
    uint64_t written = 0;
    bool has_stats = false;
    uint32_t clock_source = 0;
    uint64_t tsc_hz = 0;
    int32_t producer_cpu = -1;
    uint32_t tsc_aux_at_start = 0;
};

// Offline. Reads a trace and a span config. Span definitions are data:
// changing them does not require rebuilding the tool.
//
// Config lines (blank lines and # comments ignored):
//   span <name> <domain/begin> <domain/end> [requires <domain/mid>]
// A span is emitted only when begin and end match as a stack pair. If
// `requires` is set and that marker was not observed strictly inside the
// pair, the span is dropped. Nested pairs of the same markers use a stack.
// Incomplete pairs at end of trace are dropped. Unmatched ends are ignored.
DecodeResult decode_trace(const std::filesystem::path& trace,
                          const std::filesystem::path& span_config);

void write_span_report(std::ostream& out, const DecodeResult& result);

}  // namespace hft::instr
