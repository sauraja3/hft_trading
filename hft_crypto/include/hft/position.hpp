#pragma once

namespace hft {

/**
 * Position sizing for high-frequency: Kelly-like scaling with volatility clamp.
 */
class PositionSizer {
public:
    PositionSizer(
        double kelly_fraction = 0.25,
        double vol_target = 0.15,
        double max_position = 1.0,
        double min_notional = 0.0
    );

    /**
     * Target position in [-1, 1] given raw signal and volatility (annualized).
     */
    double compute(double raw_signal, double ann_vol, double price) const;

    void set_kelly_fraction(double f) { kelly_fraction_ = f; }
    void set_vol_target(double v) { vol_target_ = v; }
    void set_max_position(double m) { max_position_ = m; }

private:
    double kelly_fraction_;
    double vol_target_;
    double max_position_;
    double min_notional_;
};

}  // namespace hft
