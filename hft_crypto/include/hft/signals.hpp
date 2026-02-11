#pragma once

#include "market_types.hpp"
#include <cstddef>
#include <vector>

namespace hft {

/**
 * High-frequency signal engine. All functions are designed for low latency.
 * No heap allocation in hot path; caller provides buffers.
 */
class SignalEngine {
public:
    SignalEngine() = default;

    /**
     * Order book imbalance: (bid_vol - ask_vol) / (bid_vol + ask_vol)
     * Uses top N levels. Returns in [-1, 1].
     */
    double order_book_imbalance(const OrderBookSnapshot& book, size_t levels = 5) const;

    /**
     * Tick flow imbalance: sum(side * amount) over last N ticks, normalized.
     */
    double tick_flow_imbalance(const Tick* ticks, size_t n) const;

    /**
     * Micro-momentum: (close - open) / open over last N ticks or one bar.
     */
    double micro_momentum(const Tick* ticks, size_t n) const;

    /**
     * From OHLC bar: (close - open) / (high - low + epsilon).
     */
    double bar_momentum(const Bar& bar) const;

    /**
     * VWAP imbalance: buy_vol - sell_vol over bar, normalized by total.
     */
    double vwap_imbalance(const Bar& bar) const;

    /**
     * Combined raw signal: weighted sum of available inputs.
     */
    double combined_signal(
        double ob_imbalance,
        double tick_imbalance,
        double momentum,
        double ob_w = 0.4,
        double tick_w = 0.35,
        double mom_w = 0.25
    ) const;
};

}  // namespace hft
