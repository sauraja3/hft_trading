#include "hft/instr/recorder.hpp"

#include "hft/instr/registry.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <new>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sched.h>
#include <unistd.h>

namespace hft::instr {
namespace {

bool pwrite_full(int fd, const void* data, std::size_t bytes, std::int64_t offset) {
    const auto* cursor = static_cast<const char*>(data);
    std::size_t done = 0;
    while (done < bytes) {
        const ssize_t wrote = ::pwrite(fd, cursor + done, bytes - done,
                                       static_cast<off_t>(offset + static_cast<std::int64_t>(done)));
        if (wrote < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (wrote == 0) return false;
        done += static_cast<std::size_t>(wrote);
    }
    return true;
}

uint64_t calibrate_tsc_hz() {
    if (kActiveClockSource != kClockRdtscp) return 1000000000ull;
    const auto t0 = std::chrono::steady_clock::now();
    const uint64_t c0 = read_clock().ticks;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto t1 = std::chrono::steady_clock::now();
    const uint64_t c1 = read_clock().ticks;
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    if (ns <= 0 || c1 <= c0) return 0;
    return static_cast<uint64_t>((c1 - c0) * 1000000000ull / static_cast<uint64_t>(ns));
}

}  // namespace

class Recorder::Thread {
public:
    template <class F>
    explicit Thread(F&& fn) : worker_(std::forward<F>(fn)) {}

    ~Thread() {
        if (worker_.joinable()) worker_.join();
    }

    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

private:
    std::thread worker_;
};

Recorder::Recorder(Config config) : config_(std::move(config)) {
    if (config_.path.empty()) {
        throw std::invalid_argument("recorder path is empty");
    }
    if (config_.capacity < 1 || (config_.capacity & (config_.capacity - 1)) != 0) {
        throw std::invalid_argument("recorder capacity must be a power of two");
    }
    capacity_ = config_.capacity;
    mask_ = capacity_ - 1;

    const std::size_t bytes = static_cast<std::size_t>(capacity_) * sizeof(Event);
    const std::size_t alloc_bytes = (bytes + 63u) & ~std::size_t{63};
    slots_ = static_cast<Event*>(std::aligned_alloc(64, alloc_bytes));
    if (slots_ == nullptr) throw std::bad_alloc();
    std::memset(slots_, 0, alloc_bytes);

    try {
        io_buf_.resize(sizeof(ChunkHeader) + static_cast<std::size_t>(capacity_) * sizeof(Event));
        open_file();
        write_preamble();
        if (config_.start_thread) {
            thread_ = new Thread([this] { run(); });
            thread_started_ = true;
        }
    } catch (...) {
        delete thread_;
        thread_ = nullptr;
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        std::free(slots_);
        slots_ = nullptr;
        throw;
    }
}

Recorder::~Recorder() {
    stop_.store(true, std::memory_order_release);
    delete thread_;
    thread_ = nullptr;
    thread_started_ = false;

    if (fd_ >= 0 && preamble_ok_) {
        while (drain_once()) {
        }
        write_stats();
        ::fsync(fd_);
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    std::free(slots_);
    slots_ = nullptr;
}

void Recorder::flush() {
    if (thread_started_) {
        const uint64_t target = write_seq_.load(std::memory_order_acquire);
        while (read_seq_.load(std::memory_order_acquire) < target) {
            std::this_thread::sleep_for(std::chrono::microseconds(20));
        }
    } else {
        while (drain_once()) {
        }
    }
    if (fd_ >= 0) ::fsync(fd_);
}

void Recorder::open_file() {
    fd_ = ::open(config_.path.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0644);
    if (fd_ < 0) {
        throw std::system_error(errno, std::generic_category(), "open " + config_.path);
    }
}

void Recorder::write_preamble() {
    FileHeader header{};
    std::memcpy(header.magic, kMagic, sizeof(kMagic));
    header.version = kFormatVersion;
    header.clock_source = kActiveClockSource;
    header.event_bytes = static_cast<uint32_t>(sizeof(Event));
    const ClockReading started = read_clock();
    header.tsc_aux_at_start = started.aux;
    header.start_tsc = started.ticks;
    if (kActiveClockSource == kClockSteady) {
        header.tsc_hz = 1000000000ull;
    } else if (config_.calibrate_hz) {
        header.tsc_hz = calibrate_tsc_hz();
    }
    const int cpu = ::sched_getcpu();
    header.producer_cpu = cpu < 0 ? -1 : cpu;

    if (!pwrite_full(fd_, &header, sizeof(header), 0)) {
        throw std::system_error(errno, std::generic_category(), "write trace header");
    }
    end_offset_ = static_cast<std::int64_t>(sizeof(header));

    const std::vector<detail::NameEntry> names = detail::snapshot_names();
    std::size_t payload_bytes = sizeof(uint32_t);
    for (const detail::NameEntry& name : names) {
        payload_bytes += sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint16_t) + name.domain.size() +
                         name.name.size();
    }
    std::vector<char> payload(payload_bytes);
    char* cursor = payload.data();
    const uint32_t count = static_cast<uint32_t>(names.size());
    std::memcpy(cursor, &count, sizeof(count));
    cursor += sizeof(count);
    for (const detail::NameEntry& name : names) {
        std::memcpy(cursor, &name.id, sizeof(name.id));
        cursor += sizeof(name.id);
        const uint16_t domain_len = static_cast<uint16_t>(name.domain.size());
        const uint16_t name_len = static_cast<uint16_t>(name.name.size());
        std::memcpy(cursor, &domain_len, sizeof(domain_len));
        cursor += sizeof(domain_len);
        std::memcpy(cursor, &name_len, sizeof(name_len));
        cursor += sizeof(name_len);
        std::memcpy(cursor, name.domain.data(), domain_len);
        cursor += domain_len;
        std::memcpy(cursor, name.name.data(), name_len);
        cursor += name_len;
    }
    if (!write_chunk(kChunkNames, payload.data(), payload.size())) {
        throw std::system_error(errno, std::generic_category(), "write marker name table");
    }
    preamble_ok_ = true;
}

void Recorder::run() {
    while (true) {
        const bool progressed = drain_once();
        if (progressed) continue;
        if (stop_.load(std::memory_order_acquire)) {
            drain_once();
            const uint64_t published = write_seq_.load(std::memory_order_acquire);
            if (read_local_ == published) break;
            continue;
        }
        if (config_.poll.count() > 0) {
            std::this_thread::sleep_for(config_.poll);
        } else {
            std::this_thread::yield();
        }
    }
}

bool Recorder::drain_once() {
    const uint64_t published = write_seq_.load(std::memory_order_acquire);
    if (read_local_ == published) return false;

    const uint64_t index = read_local_ & mask_;
    const uint64_t contiguous = std::min(published - read_local_, capacity_ - index);
    const std::size_t bytes = static_cast<std::size_t>(contiguous) * sizeof(Event);
    if (!write_chunk(kChunkEvents, &slots_[index], bytes)) {
        if (stop_.load(std::memory_order_acquire)) {
            io_dropped_ += published - read_local_;
            read_local_ = published;
            read_seq_.store(read_local_, std::memory_order_release);
        }
        return false;
    }
    read_local_ += contiguous;
    written_ += contiguous;
    read_seq_.store(read_local_, std::memory_order_release);
    return true;
}

bool Recorder::write_chunk(uint32_t type, const void* payload, std::size_t bytes) {
    if (bytes > 0xffffffffu) return false;
    const std::size_t total = sizeof(ChunkHeader) + bytes;
    if (io_buf_.size() < total) io_buf_.resize(total);
    ChunkHeader header;
    header.type = type;
    header.bytes = static_cast<uint32_t>(bytes);
    std::memcpy(io_buf_.data(), &header, sizeof(header));
    if (bytes != 0 && payload != nullptr) {
        std::memcpy(io_buf_.data() + sizeof(header), payload, bytes);
    }
    if (!pwrite_full(fd_, io_buf_.data(), total, end_offset_)) return false;
    end_offset_ += static_cast<std::int64_t>(total);
    return true;
}

void Recorder::write_stats() {
    StatsPayload stats;
    stats.ring_dropped = producer_.drops;
    stats.io_dropped = io_dropped_;
    stats.written = written_;
    (void)write_chunk(kChunkStats, &stats, sizeof(stats));
}

}  // namespace hft::instr
