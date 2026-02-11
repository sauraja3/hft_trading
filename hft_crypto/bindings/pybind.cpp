#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include "hft/signals.hpp"
#include "hft/position.hpp"
#include "hft/order_logic.hpp"
#include "hft/market_types.hpp"

namespace py = pybind11;

PYBIND11_MODULE(hft_py, m) {
    py::class_<hft::Tick>(m, "Tick")
        .def_readwrite("timestamp_ns", &hft::Tick::timestamp_ns)
        .def_readwrite("price", &hft::Tick::price)
        .def_readwrite("amount", &hft::Tick::amount)
        .def_readwrite("side", &hft::Tick::side);

    py::class_<hft::BookLevel>(m, "BookLevel")
        .def_readwrite("price", &hft::BookLevel::price)
        .def_readwrite("quantity", &hft::BookLevel::quantity);

    py::class_<hft::OrderBookSnapshot>(m, "OrderBookSnapshot")
        .def_readwrite("timestamp_ns", &hft::OrderBookSnapshot::timestamp_ns)
        .def_readwrite("bids", &hft::OrderBookSnapshot::bids)
        .def_readwrite("asks", &hft::OrderBookSnapshot::asks);

    py::class_<hft::Bar>(m, "Bar")
        .def_readwrite("timestamp_ns", &hft::Bar::timestamp_ns)
        .def_readwrite("open", &hft::Bar::open)
        .def_readwrite("high", &hft::Bar::high)
        .def_readwrite("low", &hft::Bar::low)
        .def_readwrite("close", &hft::Bar::close)
        .def_readwrite("volume", &hft::Bar::volume)
        .def_readwrite("buy_volume", &hft::Bar::buy_volume)
        .def_readwrite("sell_volume", &hft::Bar::sell_volume);

    py::class_<hft::SignalEngine>(m, "SignalEngine")
        .def(py::init<>())
        .def("order_book_imbalance", &hft::SignalEngine::order_book_imbalance,
             py::arg("book"), py::arg("levels") = 5)
        .def("tick_flow_imbalance", [](const hft::SignalEngine& e, py::array_t<double> ts, py::array_t<double> prices, py::array_t<double> amounts, py::array_t<int8_t> sides) {
            auto ts_buf = ts.unchecked<1>();
            auto pr_buf = prices.unchecked<1>();
            auto am_buf = amounts.unchecked<1>();
            auto sd_buf = sides.unchecked<1>();
            size_t n = ts.shape(0);
            std::vector<hft::Tick> ticks(n);
            for (size_t i = 0; i < n; ++i) {
                ticks[i] = {static_cast<int64_t>(ts_buf(i)), pr_buf(i), am_buf(i), sd_buf(i)};
            }
            return e.tick_flow_imbalance(ticks.data(), n);
        })
        .def("micro_momentum", [](const hft::SignalEngine& e, py::array_t<double> prices) {
            auto buf = prices.unchecked<1>();
            size_t n = buf.shape(0);
            std::vector<hft::Tick> ticks(n);
            for (size_t i = 0; i < n; ++i) ticks[i] = {0, buf(i), 0, 0};
            return e.micro_momentum(ticks.data(), n);
        })
        .def("bar_momentum", &hft::SignalEngine::bar_momentum)
        .def("vwap_imbalance", &hft::SignalEngine::vwap_imbalance)
        .def("combined_signal", &hft::SignalEngine::combined_signal,
             py::arg("ob_imbalance"), py::arg("tick_imbalance"), py::arg("momentum"),
             py::arg("ob_w") = 0.4, py::arg("tick_w") = 0.35, py::arg("mom_w") = 0.25);

    py::class_<hft::PositionSizer>(m, "PositionSizer")
        .def(py::init<double, double, double, double>(),
             py::arg("kelly_fraction") = 0.25, py::arg("vol_target") = 0.15,
             py::arg("max_position") = 1.0, py::arg("min_notional") = 0.0)
        .def("compute", &hft::PositionSizer::compute,
             py::arg("raw_signal"), py::arg("ann_vol"), py::arg("price"));

    py::enum_<hft::OrderSide>(m, "OrderSide")
        .value("Buy", hft::OrderSide::Buy)
        .value("Sell", hft::OrderSide::Sell);

    py::class_<hft::OrderIntent>(m, "OrderIntent")
        .def_readwrite("side", &hft::OrderIntent::side)
        .def_readwrite("quantity", &hft::OrderIntent::quantity)
        .def_readwrite("price", &hft::OrderIntent::price)
        .def_readwrite("timestamp_ns", &hft::OrderIntent::timestamp_ns)
        .def_readwrite("client_id", &hft::OrderIntent::client_id);

    py::class_<hft::OrderLogic>(m, "OrderLogic")
        .def(py::init<double, double>(), py::arg("min_order_size") = 0.0001, py::arg("lot_size") = 0.0001)
        .def("compute_order", &hft::OrderLogic::compute_order,
             py::arg("current_pos"), py::arg("target_pos"), py::arg("price"),
             py::arg("ts_ns"), py::arg("client_id"));
}
