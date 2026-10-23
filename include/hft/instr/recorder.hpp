#pragma once

#include "hft/instr/clock.hpp"
#include "hft/instr/event.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hft::instr {

// Single-producer ring. record() is the hot path: no mutex, no syscall, no
// heap, no string work. A background thread (or flush(), when the thread is
// not running) drains slots to disk.
//
// One producer thread per Recorder. A second producer is a data race.
class Recorder {
public:
    struct Config {
        std::string path;
        // Power of two. Depth of the queue between the producer and the disk thread.
        std::size_t capacity = 1u << 16;
        bool start_thread = true;
        // Samples TSC rate for about 2ms. Not used on the hot path.
        bool calibrate_hz = true;
        std::chrono::microseconds poll{50};
    };

    explicit Recorder(Config config);
    ~Recorder();

    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;
    Recorder(Recorder&&) = delete;
    Recorder& operator=(Recorder&&) = delete;

    // Hot path. `marker_id` must be a compile-time constant at the call site
    // (Marker::kId) so it becomes an immediate store.
    [[gnu::always_inline]] void record(uint32_t marker_id) noexcept {
        // Timestamp before any ring bookkeeping. A full-ring reload of the
        // consumer index is a possible cache miss and must not land inside
        // the measurement.
        const ClockReading clock = read_clock();

        const uint64_t write = producer_.write_local;
        if (write - producer_.cached_read == capacity_) {
            producer_.cached_read = read_seq_.load(std::memory_order_acquire);
            if (write - producer_.cached_read == capacity_) {
                ++producer_.drops;
                return;
            }
        }

        Event& slot = slots_[write & mask_];
        slot.tsc = clock.ticks;
        slot.marker_id = marker_id;
        slot.tsc_aux = clock.aux;

        producer_.write_local = write + 1;
        // Publish the slot. Pairs with the consumer's acquire load.
        write_seq_.store(producer_.write_local, std::memory_order_release);
    }

    // Blocks the caller until events recorded so far have been written.
    // Call from the producer thread. This is not part of the hot path.
    void flush();

    // Producer-thread observation of ring drops. Not synchronized for other threads.
    uint64_t dropped() const noexcept { return producer_.drops; }

    uint64_t capacity() const noexcept { return capacity_; }

private:
    struct alignas(64) ProducerState {
        uint64_t write_local = 0;
        uint64_t cached_read = 0;
        uint64_t drops = 0;
    };

    void open_file();
    void write_preamble();
    void run();
    // Returns false when nothing was pending. On a persistent write error while
    // stopping, abandons the remaining suffix and counts it as io_dropped.
    bool drain_once();
    bool write_chunk(uint32_t type, const void* payload, std::size_t bytes);
    void write_stats();

    Config config_;
    uint64_t capacity_ = 0;
    uint64_t mask_ = 0;
    Event* slots_ = nullptr;

    ProducerState producer_{};

    // Exclusive cache lines: the consumer polls write_seq_ and publishes read_seq_.
    alignas(64) std::atomic<uint64_t> write_seq_{0};
    alignas(64) std::atomic<uint64_t> read_seq_{0};
    alignas(64) std::atomic<bool> stop_{false};

    int fd_ = -1;
    std::int64_t end_offset_ = 0;
    // Reused by the consumer. Sized for a full ring in the constructor so
    // draining does not allocate on the steady path.
    std::vector<char> io_buf_;
    uint64_t read_local_ = 0;
    uint64_t written_ = 0;
    uint64_t io_dropped_ = 0;
    bool thread_started_ = false;
    bool preamble_ok_ = false;

    class Thread;
    Thread* thread_ = nullptr;
};

}  // namespace hft::instr
