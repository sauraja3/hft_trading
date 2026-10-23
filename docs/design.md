# Low-overhead marker instrumentation

The library records named points on a latency-sensitive path and turns them into spans offline. A marker write stores a timestamp and a 32-bit id in a single-producer ring. A background thread drains the ring to disk. Which pairs form spans is a config file, so changing that does not rebuild the tool or the engine.

## Data structures

An event is 16 bytes, four per cache line: `tsc` (8 bytes, `rdtscp` ticks, or steady-clock nanoseconds off x86), `marker_id` (4, FNV-1a of `domain/name`), and `tsc_aux` (4, `IA32_TSC_AUX` from `rdtscp`, or 0 on the fallback clock).

The ring is a power-of-two array allocated when the recorder is constructed. The producer keeps its write index and a cached copy of the consumer index on a private cache line. The published write index and the consumer read index each sit on their own line. The producer release-stores the write index after the slot is filled. The consumer acquire-loads it before reading slots and release-stores the read index only after the bytes are in the file, which is what makes the plain slot stores safe.

The file starts with a 64-byte header: magic, version, clock source, event size, a coarse TSC frequency, the TSC at open, and the CPU the recorder started on. Length-prefixed chunks then carry the name table, groups of events, and, on a clean shutdown, drop and write counts. A chunk whose payload runs past EOF is a torn tail and is ignored.

## Hot path

`Marker<Domain, "name">::record` is wrapped in `if constexpr (Domain::kEnabled)`. The flag lives once, on the domain type, so call sites do not branch and do not carry `#ifdef`s. A disabled domain discards the body. At `-O2` that function is a bare `ret`, and inlined at a call site it adds no instructions. At `-O0` the disabled function contains no `rdtscp` and no call.

The enabled success path is:

1. `rdtscp`, before any ring work. It waits for earlier instructions to retire and returns ticks plus aux. A later miss on the consumer index cannot inflate the sample.
2. Compare the local write index with the cached read index.
3. If the ring looks full, acquire-load the real read index. If it is still full, increment a producer-local drop counter and return. The caller does not wait.
4. Store ticks, the marker id (a compile-time immediate), and aux into the slot.
5. Advance the local write index and release-store the published index.

There is no mutex, syscall, heap allocation, or string copy. The only branch is the ring-full check, and it is not taken until the ring is full. On x86 the release store is an ordinary store; the machine already orders it after the slot writes.

`rdtscp` dominates. It has to serialize the pipeline so the timestamp sits after the work being marked, and that costs tens of cycles. The rest is a compare and a few stores: one streaming line per four events, plus the write index, which stays in the producer's cache until the consumer actually polls it. Only the overflow path reloads the consumer's line, and that is the path that drops.

## IDs and namespacing

The id is FNV-1a-32 over the domain, a slash, and the marker name. The domain string is the namespace: two subsystems can both define `start` and get different ids. There is no shared counter, and the hot path never looks up a string.

32 bits is what keeps the record at 16 bytes next to `TSC_AUX`. Aux is already in a register after `rdtscp`, and the decoder uses it to flag a span whose ends ran on different logical CPUs. A 64-bit hash would drop aux or push the event past a quarter of a cache line.

A 32-bit hash can collide. Opening a recorder throws if two different `domain/name` pairs share an id, and the decoder does the same for a config. A few hundred markers make that negligible. A hand-assigned ordinal is easier to misuse, and a 16-bit name hash collides inside one domain, so a loud startup failure is the better trade.

`HFT_INSTR_MARKER` registers the strings at static init. The header of the trace gets that table. `record()` does not touch it. The id is also a pure function of the two strings, so a span config still matches a marker that was never registered.

## Persistence and its limits

One background thread polls the ring (default 50 µs) and `pwrite`s each contiguous run of events. `record()` does not wait on that thread and does not wake it. `flush()`, called from the producer and not from the marker path, waits until the consumer has caught the published index and then `fsync`s.

The drain buffer is reserved at construction, and a slot is released only after `pwrite` returns, so a crash cannot acknowledge bytes that never reached disk. A short write is retried at the same offset.

- One producer thread per recorder. A second producer is a data race. Threads that do not share a recorder do not share a cache line on this path.
- The ring is the only buffer. If the consumer stalls longer than it can absorb — capacity times the gap between markers, about 6.5 ms for the default 65536 events at one marker per 100 ns — later markers increment the drop counter and are discarded. The producer does not block or overwrite a slot the consumer still owns. The newest events are the ones dropped.
- A crash loses whatever is still in the ring. The decoder drops a torn last chunk. The drop count is written only on clean shutdown, so a crash report has no loss total.
- A `pwrite` error during shutdown abandons the unflushed suffix and counts it as IO loss. While running, the consumer retries, and the producer still does not block.
- Timestamps are raw ticks, not wall time. Calibration, when enabled, is a 2 ms sample stored in the header. Ticks compare for events with the same `TSC_AUX` on an invariant TSC. Cross-socket skew and migration are reported as `cross_cpu` and are not repaired. Pin the producer. Off x86, and in the fallback build used by tests, the clock is `steady_clock` nanoseconds and aux is 0.

## Nested spans from a flat stream

The file is an ordered list of points. Each configured pair has its own stack. A begin pushes `(tsc, aux)`; an end pops. The optional required marker marks every span of that pair already open, so it lies inside those spans and not inside one that starts later. If the marker was required and never seen, the end drops the span. An end on an empty stack is ignored. A begin still open at EOF is dropped. Same-pair nesting is the stack. Different pairs do not interact.

A second pass picks a parent for each emitted span: the tightest other span that fully contains it. Depth is the length of that chain. Siblings do not contain each other, so both stay at depth 0. The marker does not carry a span id.

Lines look like `span <name> <domain/begin> <domain/end> [requires <domain/mid>]`. A `#` comments out the rest of the line. Editing the file changes the spans on the next decode.

## What we gave up

A multi-producer ring would put an atomic increment on every marker and couple threads that otherwise share nothing, so each instrumented thread owns a recorder. Refusing to drop would mean blocking or allocating when the disk stalls, which moves the latency under measurement; a full ring drops the new event and returns. Payloads and a hot-path span stack would add stores at the marker for a question — where the time went — that offline pairing already answers. A futex wake would put a syscall on the producer, while a 50 µs poll only delays durability. Strings stay in the startup name table, TSC values are not converted to nanoseconds in the marker, and ticks from different sockets are not treated as one clock.
