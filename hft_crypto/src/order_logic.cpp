#include "hft/order_logic.hpp"
#include <cmath>

namespace hft {

OrderLogic::OrderLogic(double min_order_size, double lot_size)
    : min_order_size_(min_order_size)
    , lot_size_(lot_size)
{}

OrderIntent OrderLogic::compute_order(
    double current_pos,
    double target_pos,
    double price,
    int64_t ts_ns,
    uint64_t client_id
) const {
    double delta = target_pos - current_pos;

    if (std::abs(delta) < min_order_size_) {
        return {OrderSide::Buy, 0.0, price, ts_ns, client_id};  // no order
    }

    double qty = std::abs(delta);
    if (lot_size_ > 0) {
        qty = std::floor(qty / lot_size_) * lot_size_;
    }
    if (qty < min_order_size_) {
        return {OrderSide::Buy, 0.0, price, ts_ns, client_id};
    }

    OrderSide side = delta > 0 ? OrderSide::Buy : OrderSide::Sell;
    return {side, qty, price, ts_ns, client_id};
}

}  // namespace hft
