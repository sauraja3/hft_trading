#include "hft/instr/decoder.hpp"
#include "hft/instr/event.hpp"

#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: hft_instr_decode <trace> <spans.conf> [output]\n";
        return 2;
    }
    try {
        const hft::instr::DecodeResult result = hft::instr::decode_trace(argv[1], argv[2]);
        std::ofstream file;
        std::ostream* out = &std::cout;
        if (argc >= 4) {
            file.open(argv[3]);
            if (!file) {
                std::cerr << "hft_instr_decode: cannot open " << argv[3] << '\n';
                return 1;
            }
            out = &file;
        }
        hft::instr::write_span_report(*out, result);
        const char* clock = result.clock_source == hft::instr::kClockRdtscp ? "rdtscp"
                            : result.clock_source == hft::instr::kClockSteady ? "steady_clock"
                                                                              : "unknown";
        std::cerr << "events=" << result.events.size() << " spans=" << result.spans.size()
                  << " ring_dropped=" << result.ring_dropped << " io_dropped=" << result.io_dropped
                  << " has_stats=" << (result.has_stats ? 1 : 0) << " clock=" << clock
                  << " hz=" << result.tsc_hz << '\n';
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "hft_instr_decode: " << ex.what() << '\n';
        return 1;
    }
}
