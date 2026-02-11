#include "hft/position.hpp"
#include <algorithm>
#include <cmath>

namespace hft {

PositionSizer::PositionSizer(
    double kelly_fraction,
    double vol_target,
    double max_position,
    double min_notional
)
    : kelly_fraction_(kelly_fraction)
    , vol_target_(vol_target)
    , max_position_(max_position)
    , min_notional_(min_notional)
{}

double PositionSizer::compute(double raw_signal, double ann_vol, double price) const {
    if (std::abs(price) < 1e-12) return 0.0;
    if (ann_vol < 1e-12) ann_vol = 0.20;  // fallback

    double scale = vol_target_ / ann_vol;
    double raw_pos = kelly_fraction_ * raw_signal * scale;
    double clamped = std::max(-max_position_, std::min(max_position_, raw_pos));

    return clamped;
}

}  // namespace hft
