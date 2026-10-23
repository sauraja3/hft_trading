#include "hft/instr/marker.hpp"

// Compiled at -O0. A disabled domain must leave no rdtscp and no call.
// An enabled domain in the same translation unit must still emit rdtscp,
// so an empty object file cannot satisfy the check.

HFT_INSTR_DOMAIN(Off, "codegen_off", false);
HFT_INSTR_DOMAIN(On, "codegen_on", true);
HFT_INSTR_MARKER(disabled_marker, Off, "should_vanish");
HFT_INSTR_MARKER(enabled_marker, On, "should_stay");

extern "C" void instrument_disabled(hft::instr::Recorder* recorder) {
    disabled_marker.record(*recorder);
}

extern "C" void instrument_enabled(hft::instr::Recorder* recorder) {
    enabled_marker.record(*recorder);
}
