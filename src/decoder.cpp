#include "hft/instr/decoder.hpp"

#include "hft/instr/event.hpp"
#include "hft/instr/fixed_string.hpp"
#include "hft/instr/id.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace hft::instr {
namespace {

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

template <class T>
T load_pod(const char* data) {
    T value{};
    std::memcpy(&value, data, sizeof(T));
    return value;
}

std::vector<std::string> tokenize(std::string_view line) {
    std::vector<std::string> tokens;
    std::string current;
    for (char ch : line) {
        if (ch == '#') break;
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            if (!current.empty()) {
                tokens.push_back(std::move(current));
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) tokens.push_back(std::move(current));
    return tokens;
}

std::pair<std::string, std::string> split_marker(const std::string& token) {
    const auto slash = token.find('/');
    if (slash == std::string::npos || token.find('/', slash + 1) != std::string::npos) {
        fail("marker must be domain/name, got '" + token + "'");
    }
    std::string domain = token.substr(0, slash);
    std::string name = token.substr(slash + 1);
    if (!is_identifier(domain) || !is_identifier(name)) {
        fail("marker '" + token + "' is not domain/identifier");
    }
    return {std::move(domain), std::move(name)};
}

struct Spec {
    std::string name;
    uint32_t begin_id = 0;
    uint32_t end_id = 0;
    std::optional<uint32_t> required_id;
    struct Open {
        uint64_t start = 0;
        uint32_t aux = 0;
        bool got = false;
    };
    std::vector<Open> stack;
};

struct RawSpan {
    std::string name;
    uint64_t start = 0;
    uint64_t end = 0;
    uint32_t begin_aux = 0;
    uint32_t end_aux = 0;
    int parent = -1;
    int depth = 0;
};

uint64_t duration_of(uint64_t start, uint64_t end) {
    return end >= start ? end - start : 0;
}

std::vector<Spec> load_specs(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) fail("cannot open span config " + path.string());

    struct NamedId {
        std::string key;
        uint32_t id = 0;
    };
    std::vector<NamedId> referenced;
    std::vector<Spec> specs;
    std::string line;
    int line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        const std::vector<std::string> tokens = tokenize(line);
        if (tokens.empty()) continue;
        const auto bad = [&](const std::string& why) {
            fail(path.string() + ":" + std::to_string(line_no) + ": " + why);
        };
        if (tokens[0] != "span") bad("expected 'span'");
        if (tokens.size() != 4 && tokens.size() != 6) {
            bad("expected 'span <name> <begin> <end> [requires <marker>]'");
        }
        if (!is_identifier(tokens[1])) bad("span name must be an identifier");
        const auto begin = split_marker(tokens[2]);
        const auto end = split_marker(tokens[3]);
        Spec spec;
        spec.name = tokens[1];
        spec.begin_id = make_marker_id(begin.first, begin.second);
        spec.end_id = make_marker_id(end.first, end.second);
        if (spec.begin_id == spec.end_id) bad("begin and end are the same marker");
        referenced.push_back({begin.first + "/" + begin.second, spec.begin_id});
        referenced.push_back({end.first + "/" + end.second, spec.end_id});
        if (tokens.size() == 6) {
            if (tokens[4] != "requires") bad("expected 'requires'");
            const auto mid = split_marker(tokens[5]);
            const uint32_t mid_id = make_marker_id(mid.first, mid.second);
            if (mid_id == spec.begin_id || mid_id == spec.end_id) {
                bad("required marker must be distinct from begin and end");
            }
            spec.required_id = mid_id;
            referenced.push_back({mid.first + "/" + mid.second, mid_id});
        }
        specs.push_back(std::move(spec));
    }

    std::unordered_map<uint32_t, std::string> seen;
    for (const NamedId& item : referenced) {
        const auto it = seen.find(item.id);
        if (it == seen.end()) {
            seen.emplace(item.id, item.key);
        } else if (it->second != item.key) {
            fail("span config marker id collision between " + it->second + " and " + item.key);
        }
    }
    return specs;
}

std::vector<DecodedSpan> match_spans(const std::vector<DecodedEvent>& events, std::vector<Spec> specs) {
    std::vector<RawSpan> raw;
    for (const DecodedEvent& event : events) {
        for (Spec& spec : specs) {
            if (spec.required_id.has_value() && event.marker_id == *spec.required_id) {
                for (Spec::Open& open : spec.stack) open.got = true;
            }
            if (event.marker_id == spec.begin_id) {
                spec.stack.push_back(Spec::Open{event.tsc, event.tsc_aux, false});
            } else if (event.marker_id == spec.end_id) {
                if (spec.stack.empty()) continue;
                const Spec::Open open = spec.stack.back();
                spec.stack.pop_back();
                if (spec.required_id.has_value() && !open.got) continue;
                RawSpan span;
                span.name = spec.name;
                span.start = open.start;
                span.end = event.tsc;
                span.begin_aux = open.aux;
                span.end_aux = event.tsc_aux;
                raw.push_back(std::move(span));
            }
        }
    }

    std::sort(raw.begin(), raw.end(), [](const RawSpan& a, const RawSpan& b) {
        if (a.start != b.start) return a.start < b.start;
        if (a.end != b.end) return a.end > b.end;
        return a.name < b.name;
    });

    const std::size_t count = raw.size();
    for (std::size_t i = 0; i < count; ++i) {
        const uint64_t mine = duration_of(raw[i].start, raw[i].end);
        uint64_t best = ~uint64_t{0};
        int parent = -1;
        for (std::size_t j = 0; j < count; ++j) {
            if (i == j) continue;
            if (raw[j].start <= raw[i].start && raw[i].end <= raw[j].end) {
                const uint64_t theirs = duration_of(raw[j].start, raw[j].end);
                if (theirs > mine && theirs < best) {
                    best = theirs;
                    parent = static_cast<int>(j);
                }
            }
        }
        raw[i].parent = parent;
    }

    std::vector<std::size_t> by_duration(count);
    for (std::size_t i = 0; i < count; ++i) by_duration[i] = i;
    std::sort(by_duration.begin(), by_duration.end(), [&](std::size_t a, std::size_t b) {
        const uint64_t da = duration_of(raw[a].start, raw[a].end);
        const uint64_t db = duration_of(raw[b].start, raw[b].end);
        if (da != db) return da > db;
        return a < b;
    });
    for (std::size_t index : by_duration) {
        if (raw[index].parent < 0) {
            raw[index].depth = 0;
        } else {
            raw[index].depth = raw[static_cast<std::size_t>(raw[index].parent)].depth + 1;
        }
    }

    std::vector<DecodedSpan> spans;
    spans.reserve(raw.size());
    for (const RawSpan& item : raw) {
        DecodedSpan span;
        span.name = item.name;
        span.start_tsc = item.start;
        span.end_tsc = item.end;
        span.duration = duration_of(item.start, item.end);
        span.depth = item.depth;
        span.begin_aux = item.begin_aux;
        span.end_aux = item.end_aux;
        span.cross_cpu = item.begin_aux != item.end_aux;
        if (item.parent >= 0) {
            const RawSpan& parent = raw[static_cast<std::size_t>(item.parent)];
            span.parent = parent.name;
            span.parent_start = parent.start;
        }
        spans.push_back(std::move(span));
    }
    return spans;
}

std::unordered_map<uint32_t, std::string> parse_names(const char* data, std::size_t bytes) {
    if (bytes < sizeof(uint32_t)) fail("truncated name table");
    const uint32_t count = load_pod<uint32_t>(data);
    std::size_t offset = sizeof(uint32_t);
    std::unordered_map<uint32_t, std::string> names;
    names.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (bytes - offset < sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint16_t)) {
            fail("truncated name entry");
        }
        const uint32_t id = load_pod<uint32_t>(data + offset);
        offset += sizeof(uint32_t);
        const uint16_t domain_len = load_pod<uint16_t>(data + offset);
        offset += sizeof(uint16_t);
        const uint16_t name_len = load_pod<uint16_t>(data + offset);
        offset += sizeof(uint16_t);
        if (bytes - offset < static_cast<std::size_t>(domain_len) + name_len) {
            fail("truncated name bytes");
        }
        std::string domain(data + offset, domain_len);
        offset += domain_len;
        std::string name(data + offset, name_len);
        offset += name_len;
        const std::string key = domain + "/" + name;
        if (make_marker_id(domain, name) != id) {
            fail("name table id does not match the marker hash for " + key);
        }
        const auto it = names.find(id);
        if (it != names.end() && it->second != key) {
            fail("trace name table collision between " + it->second + " and " + key);
        }
        names.emplace(id, key);
    }
    if (offset != bytes) fail("name table has trailing bytes");
    return names;
}

}  // namespace

DecodeResult decode_trace(const std::filesystem::path& trace, const std::filesystem::path& span_config) {
    std::ifstream in(trace, std::ios::binary);
    if (!in) fail("cannot open trace " + trace.string());
    std::vector<char> file(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>{});
    if (file.size() < sizeof(FileHeader)) fail("trace is shorter than the header");

    const FileHeader header = load_pod<FileHeader>(file.data());
    if (std::memcmp(header.magic, kMagic, sizeof(kMagic)) != 0) fail("bad trace magic");
    if (header.version != kFormatVersion) fail("unsupported trace version");
    if (header.event_bytes != sizeof(Event)) fail("unexpected event size");

    DecodeResult result;
    result.clock_source = header.clock_source;
    result.tsc_hz = header.tsc_hz;
    result.producer_cpu = header.producer_cpu;
    result.tsc_aux_at_start = header.tsc_aux_at_start;

    std::unordered_map<uint32_t, std::string> names;
    std::size_t offset = sizeof(FileHeader);
    while (offset + sizeof(ChunkHeader) <= file.size()) {
        const ChunkHeader chunk = load_pod<ChunkHeader>(file.data() + offset);
        offset += sizeof(ChunkHeader);
        if (chunk.bytes > (1u << 28)) fail("chunk is too large");
        if (offset + chunk.bytes > file.size()) break;
        const char* payload = file.data() + offset;
        offset += chunk.bytes;

        if (chunk.type == kChunkNames) {
            names = parse_names(payload, chunk.bytes);
        } else if (chunk.type == kChunkEvents) {
            if (chunk.bytes % sizeof(Event) != 0) fail("event chunk is not a multiple of the event size");
            const std::size_t count = chunk.bytes / sizeof(Event);
            result.events.reserve(result.events.size() + count);
            for (std::size_t i = 0; i < count; ++i) {
                const Event event = load_pod<Event>(payload + i * sizeof(Event));
                DecodedEvent decoded;
                decoded.tsc = event.tsc;
                decoded.marker_id = event.marker_id;
                decoded.tsc_aux = event.tsc_aux;
                const auto it = names.find(event.marker_id);
                if (it != names.end()) decoded.name = it->second;
                result.events.push_back(std::move(decoded));
            }
        } else if (chunk.type == kChunkStats) {
            if (chunk.bytes != sizeof(StatsPayload)) fail("bad stats chunk");
            const StatsPayload stats = load_pod<StatsPayload>(payload);
            result.ring_dropped = stats.ring_dropped;
            result.io_dropped = stats.io_dropped;
            result.written = stats.written;
            result.dropped = stats.ring_dropped + stats.io_dropped;
            result.has_stats = true;
        }
    }

    result.spans = match_spans(result.events, load_specs(span_config));
    return result;
}

void write_span_report(std::ostream& out, const DecodeResult& result) {
    out << "name\tstart_tsc\tend_tsc\tduration\tdepth\tparent\tparent_start\tcross_cpu\n";
    for (const DecodedSpan& span : result.spans) {
        out << span.name << '\t' << span.start_tsc << '\t' << span.end_tsc << '\t' << span.duration
            << '\t' << span.depth << '\t' << (span.parent.empty() ? "-" : span.parent) << '\t';
        if (span.parent.empty()) out << '-';
        else out << span.parent_start;
        out << '\t' << (span.cross_cpu ? 1 : 0) << '\n';
    }
}

}  // namespace hft::instr
