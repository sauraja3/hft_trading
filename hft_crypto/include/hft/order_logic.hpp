#pragma once

#include "market_types.hpp"
#include <cstdint>

namespace hft {

enum class OrderSide { Buy = 1, Sell = -1 };

struct OrderIntent {
    OrderSide side;
    double quantity;
    double price;           // 0 = market
    int64_t timestamp_ns;
    uint64_t client_id;
};

/**
 * Order generation: target position → order intents.
 * Handles delta from current position to target; produces orders for execution layer.
 */
class OrderLogic {
public:
    OrderLogic(double min_order_size = 0.0001, double lot_size = 0.0001);

    /**
     * Compute order intent to move from current_pos to target_pos.
     * quantity is in base asset; price 0 means market.
     */
    OrderIntent compute_order(
        double current_pos,
        double target_pos,
        double price,
        int64_t ts_ns,
        uint64_t client_id
    ) const;

    void set_min_order_size(double s) { min_order_size_ = s; }
    void set_lot_size(double s) { lot_size_ = s; }

private:
    double min_order_size_;
    double lot_size_;
};

}  // namespace hft
