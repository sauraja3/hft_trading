/**
 * HFT Engine - standalone entry point for production.
 * Assumes market data and orders are handled by external infrastructure
 * (FIX, WebSocket, shared memory, etc.). This executable demonstrates
 * the core logic in a minimal loop for testing.
 */
#include "hft/signals.hpp"
#include "hft/position.hpp"
#include "hft/order_logic.hpp"
#include <iostream>
#include <vector>
#include <chrono>

int main() {
    hft::SignalEngine signals;
    hft::PositionSizer sizer(0.25, 0.15, 1.0, 0.0);
    hft::OrderLogic order_logic(0.0001, 0.0001);

    // Simulate a few ticks
    std::vector<hft::Tick> ticks = {
        {1000000000, 50000.0, 0.01, 1},
        {1000001000, 50001.0, 0.02, 1},
        {1000002000, 50000.5, 0.015, -1},
        {1000003000, 50002.0, 0.03, 1},
    };

    double tick_imb = signals.tick_flow_imbalance(ticks.data(), ticks.size());
    double mom = signals.micro_momentum(ticks.data(), ticks.size());
    double raw = signals.combined_signal(0.0, tick_imb, mom);

    double ann_vol = 0.50;
    double price = ticks.back().price;
    double target = sizer.compute(raw, ann_vol, price);

    double current_pos = 0.0;
    auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto intent = order_logic.compute_order(current_pos, target, price, now, 1);

    std::cout << "tick_imbalance=" << tick_imb << " momentum=" << mom << " raw_signal=" << raw
              << " target_pos=" << target << std::endl;
    std::cout << "order: side=" << (intent.side == hft::OrderSide::Buy ? "BUY" : "SELL")
              << " qty=" << intent.quantity << " price=" << intent.price << std::endl;

    return 0;
}
