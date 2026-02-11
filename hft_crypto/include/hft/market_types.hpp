#pragma once

#include <cstdint>
#include <vector>

namespace hft {

struct Tick {
    int64_t timestamp_ns;
    double price;
    double amount;
    int8_t side;   // +1 buy, -1 sell
};

struct BookLevel {
    double price;
    double quantity;
};

struct OrderBookSnapshot {
    int64_t timestamp_ns;
    std::vector<BookLevel> bids;
    std::vector<BookLevel> asks;
};

struct Bar {
    int64_t timestamp_ns;
    double open;
    double high;
    double low;
    double close;
    double volume;
    double buy_volume;
    double sell_volume;
};

}  // namespace hft
