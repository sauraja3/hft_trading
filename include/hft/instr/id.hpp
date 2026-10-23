#pragma once

#include <cstdint>
#include <string_view>

namespace hft::instr {

constexpr bool is_identifier(std::string_view s) noexcept {
    if (s.empty()) return false;
    auto head = [](char c) noexcept {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    };
    auto tail = [&](char c) noexcept {
        return head(c) || (c >= '0' && c <= '9');
    };
    if (!head(s.front())) return false;
    for (char c : s) {
        if (!tail(c)) return false;
    }
    return true;
}

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
