# HFT Crypto

High-frequency crypto trading engine. **Mission-critical logic is in C++**; Python orchestrates configuration and optional backtesting.

## Assumptions

- Co-located infrastructure
- Tick-level market data feed (WebSocket/FIX)
- Low-latency execution (FIX, DMA)
- Python for config, monitoring, backtest

## Project Structure

```
hft_crypto/
├── include/hft/           # C++ headers
│   ├── market_types.hpp
│   ├── signals.hpp
│   ├── position.hpp
│   └── order_logic.hpp
├── src/                   # C++ implementation
│   ├── signals.cpp
│   ├── position.cpp
│   └── order_logic.cpp
├── bindings/              # pybind11 → Python
│   └── pybind.cpp
├── main/
│   └── engine_main.cpp    # Standalone C++ executable
├── python/hft_runner/     # Python glue
├── config/
└── CMakeLists.txt
```

## Build

```bash
mkdir build && cd build
cmake .. -DBUILD_PYTHON_BINDINGS=ON
cmake --build .
```

### Run standalone C++ engine

```bash
./build/hft_engine
```

### Run via Python (after build)

```bash
cd python && python -m hft_runner.engine
```

## Signals (C++)

- **Order book imbalance**: `(bid_vol - ask_vol) / total` over top N levels
- **Tick flow imbalance**: signed volume over recent ticks
- **Micro-momentum**: `(close - open) / open` over tick window or bar

Signals are combined with configurable weights. Position sizing uses Kelly fraction and volatility targeting.

## Extending

1. **Add signals**: Extend `SignalEngine` in `include/hft/signals.hpp` and `src/signals.cpp`
2. **Connect feeds**: Replace `engine_main.cpp` loop with your FIX/WebSocket handlers; call `SignalEngine`, `PositionSizer`, `OrderLogic`
3. **Risk**: Add pre-trade checks in `OrderLogic::compute_order` (position limits, notional caps)

## Dependencies

- C++17 compiler
- CMake 3.14+
- pybind11 (optional, for Python bindings)
