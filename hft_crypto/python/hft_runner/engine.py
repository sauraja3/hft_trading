"""
Orchestrates the C++ HFT engine. Loads hft_py module and runs signal → position → order.
"""
from __future__ import annotations

import sys
from pathlib import Path

# Add build dir for hft_py (hft_crypto/build/)
_root = Path(__file__).resolve().parents[2]
_build = _root / "build"
if _build.exists():
    sys.path.insert(0, str(_build))

try:
    import hft_py
except ImportError:
    hft_py = None  # type: ignore


def run_tick_cycle(
    prices: list[float],
    amounts: list[float],
    sides: list[int],
    ob_bids: list[tuple[float, float]] | None = None,
    ob_asks: list[tuple[float, float]] | None = None,
    ann_vol: float = 0.50,
    current_pos: float = 0.0,
) -> dict:
    """
    Run one cycle: ticks → signals → position → order.
    Returns dict with raw_signal, target_position, order (side, qty, price).
    """
    if hft_py is None:
        return {"error": "hft_py not built", "raw_signal": 0, "target_position": 0}

    import numpy as np

    ts = np.arange(len(prices), dtype=np.float64) * 1e9
    pr = np.array(prices, dtype=np.float64)
    am = np.array(amounts, dtype=np.float64)
    sd = np.array(sides, dtype=np.int8)

    sig = hft_py.SignalEngine()
    tick_imb = sig.tick_flow_imbalance(ts, pr, am, sd)
    mom = sig.micro_momentum(pr)

    ob_imb = 0.0
    if ob_bids and ob_asks:
        book = hft_py.OrderBookSnapshot()
        book.timestamp_ns = int(ts[-1])
        book.bids = [hft_py.BookLevel(p, q) for p, q in ob_bids[:5]]
        book.asks = [hft_py.BookLevel(p, q) for p, q in ob_asks[:5]]
        ob_imb = sig.order_book_imbalance(book, 5)

    raw = sig.combined_signal(ob_imb, tick_imb, mom)
    sizer = hft_py.PositionSizer(0.25, 0.15, 1.0, 0.0)
    price = prices[-1] if prices else 0.0
    target = sizer.compute(raw, ann_vol, price)

    logic = hft_py.OrderLogic(0.0001, 0.0001)
    import time
    ts_ns = int(time.time_ns())
    intent = logic.compute_order(current_pos, target, price, ts_ns, 1)

    order = {
        "side": "buy" if intent.side == hft_py.OrderSide.Buy else "sell",
        "quantity": intent.quantity,
        "price": intent.price,
    }

    return {
        "raw_signal": raw,
        "target_position": target,
        "order": order,
        "tick_imbalance": tick_imb,
        "momentum": mom,
    }


def main():
    prices = [50000.0, 50001.0, 50000.5, 50002.0]
    amounts = [0.01] * 4
    sides = [1, 1, -1, 1]
    result = run_tick_cycle(prices, amounts, sides, ann_vol=0.50)
    if "error" in result:
        print("Error:", result["error"])
        return 1
    print("raw_signal:", result["raw_signal"])
    print("target_position:", result["target_position"])
    print("order:", result["order"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
