#include "hft/signals.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace hft {

namespace {
constexpr double kEpsilon = 1e-12;
}

double SignalEngine::order_book_imbalance(const OrderBookSnapshot& book, size_t levels) const {
    double bid_vol = 0.0;
    double ask_vol = 0.0;
    size_t n_bids = std::min(book.bids.size(), levels);
    size_t n_asks = std::min(book.asks.size(), levels);
    for (size_t i = 0; i < n_bids; ++i)
        bid_vol += book.bids[i].quantity;
    for (size_t i = 0; i < n_asks; ++i)
        ask_vol += book.asks[i].quantity;
    double total = bid_vol + ask_vol;
    if (total < kEpsilon) return 0.0;
    return (bid_vol - ask_vol) / total;
}

double SignalEngine::tick_flow_imbalance(const Tick* ticks, size_t n) const {
    if (n == 0) return 0.0;
    double signed_vol = 0.0;
    double total_vol = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double amt = ticks[i].amount;
        signed_vol += static_cast<double>(ticks[i].side) * amt;
        total_vol += amt;
    }
    if (total_vol < kEpsilon) return 0.0;
    return std::max(-1.0, std::min(1.0, signed_vol / total_vol));
}

double SignalEngine::micro_momentum(const Tick* ticks, size_t n) const {
    if (n < 2) return 0.0;
    double open = ticks[0].price;
    double close = ticks[n - 1].price;
    if (std::abs(open) < kEpsilon) return 0.0;
    return (close - open) / open;
}

double SignalEngine::bar_momentum(const Bar& bar) const {
    double denom = bar.high - bar.low;
    if (std::abs(denom) < kEpsilon) return 0.0;
    return (bar.close - bar.open) / denom;
}

double SignalEngine::vwap_imbalance(const Bar& bar) const {
    double total = bar.buy_volume + bar.sell_volume;
    if (total < kEpsilon) return 0.0;
    return (bar.buy_volume - bar.sell_volume) / total;
}

double SignalEngine::combined_signal(
    double ob_imbalance,
    double tick_imbalance,
    double momentum,
    double ob_w,
    double tick_w,
    double mom_w
) const {
    return ob_w * ob_imbalance + tick_w * tick_imbalance + mom_w * momentum;
}

}  // namespace hft
