#pragma once

#include <cstdint>
#include <string_view>

namespace hft::instr {

// FNV-1a 32-bit over domain, a slash, then the marker name.
// The slash keeps ("ab","c") distinct from ("a","bc"). Names are identifiers,
// so the encoding is unambiguous and needs no length prefixes.
constexpr uint32_t make_marker_id(std::string_view domain, std::string_view name) noexcept {
    uint32_t hash = 2166136261u;
    auto mix = [&](unsigned char byte) noexcept {
        hash ^= byte;
        hash *= 16777619u;
    };
    for (unsigned char byte : domain) mix(byte);
    mix('/');
    for (unsigned char byte : name) mix(byte);
    return hash;
}

}  // namespace hft::instr
