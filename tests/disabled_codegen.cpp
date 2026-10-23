#include "hft/instr/marker.hpp"

// Compiled at -O0. A disabled domain must leave no rdtscp and no call.
// An enabled domain in the same translation unit must still emit rdtscp,
// so an empty object file cannot satisfy the check.

using Off = hft::instr::Domain<"codegen_off", false>;
using On = hft::instr::Domain<"codegen_on", true>;

extern "C" void instrument_disabled(hft::instr::Recorder* recorder) {
    constexpr hft::instr::Marker<Off, "should_vanish"> marker{};
    marker.record(*recorder);
}

extern "C" void instrument_enabled(hft::instr::Recorder* recorder) {
    constexpr hft::instr::Marker<On, "should_stay"> marker{};
    marker.record(*recorder);
}
