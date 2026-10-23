# Low-overhead instrumentation

C++17 library for recording named markers on a latency-sensitive path and turning those markers into spans offline. It is not a trading engine.

A marker write is a timestamp and a 32-bit id into a single-producer ring. A background thread writes the ring to disk. Span definitions live in a text file, so changing them does not rebuild the decoder. The design — hot-path cost, id scheme, storage limits, nested-span reconstruction, and tradeoffs — is in [docs/design.md](docs/design.md).

Linux. The production clock is the x86 TSC (`rdtscp`). Other hosts, and one test build, use `std::chrono::steady_clock`.

## Build and test

CMake 3.16+, a C++17 compiler, pthreads, and Python 3 (the disabled-domain check compiles a file and reads the assembly). Configure downloads GoogleTest 1.15.2; the tests link `GTest::gtest_main`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

`hft_instr_decode` reads a trace and a span config:

```bash
./build/hft_instr_decode trace.bin spans.conf
```

Span lines look like `span <name> <domain/begin> <domain/end> [requires <domain/mid>]`. `#` comments out the rest of a line.

## Placing a marker

One constexpr bool enables or removes a whole domain. Call sites do not use `#ifdef`.

```cpp
inline constexpr bool kMarketDataMarkers = true;
HFT_INSTR_DOMAIN(MarketData, "market_data", kMarketDataMarkers);

HFT_INSTR_MARKER(decode_begin, MarketData, "decode_begin");
HFT_INSTR_MARKER(decode_end, MarketData, "decode_end");

hft::instr::Recorder::Config cfg;
cfg.path = "trace.bin";
hft::instr::Recorder recorder(cfg);

decode_begin.record(recorder);
decode_end.record(recorder);
recorder.flush();
```

`record()` does not take a lock, make a syscall, allocate, or copy a string. One recorder has one producer thread. A disabled domain compiles away.

Include `hft/instr.hpp`.
