#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace hft::instr {

// Little-endian on-disk and in-ring record. 16 bytes: four events per cache line.
struct Event {
    uint64_t tsc;
    uint32_t marker_id;
    uint32_t tsc_aux;
};

static_assert(sizeof(Event) == 16, "Event must stay 16 bytes");
static_assert(std::is_trivially_copyable_v<Event>);
static_assert(alignof(Event) <= 8);

inline constexpr char kMagic[8] = {'H', 'F', 'T', 'I', 'N', 'S', 'T', '1'};
inline constexpr uint32_t kFormatVersion = 1;
inline constexpr uint32_t kClockRdtscp = 1;
inline constexpr uint32_t kClockSteady = 2;

inline constexpr uint32_t kChunkNames = 1;
inline constexpr uint32_t kChunkEvents = 2;
inline constexpr uint32_t kChunkStats = 3;

struct FileHeader {
    char magic[8];
    uint32_t version;
    uint32_t clock_source;
    uint32_t event_bytes;
    uint32_t tsc_aux_at_start;
    uint64_t tsc_hz;
    uint64_t start_tsc;
    int32_t producer_cpu;
    uint32_t reserved;
    uint8_t pad[16];
};

static_assert(sizeof(FileHeader) == 64, "FileHeader must stay 64 bytes");
static_assert(offsetof(FileHeader, tsc_hz) == 24);
static_assert(offsetof(FileHeader, start_tsc) == 32);

struct ChunkHeader {
    uint32_t type;
    uint32_t bytes;
};

static_assert(sizeof(ChunkHeader) == 8);

struct StatsPayload {
    uint64_t ring_dropped;
    uint64_t io_dropped;
    uint64_t written;
};

static_assert(sizeof(StatsPayload) == 24);
static_assert(std::endian::native == std::endian::little,
              "the trace format is little-endian");

}  // namespace hft::instr
