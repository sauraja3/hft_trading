#pragma once

#include <cstddef>
#include <string_view>

namespace hft::instr {

// Structural NTTP string (C++20). The trailing NUL is stored and excluded from view().
template <std::size_t N>
struct FixedString {
    char data[N]{};

    constexpr FixedString(const char (&literal)[N]) noexcept {
        for (std::size_t i = 0; i < N; ++i) data[i] = literal[i];
    }

    constexpr std::string_view view() const noexcept {
        return std::string_view(data, N - 1);
    }
};

template <std::size_t N>
FixedString(const char (&)[N]) -> FixedString<N>;

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

}  // namespace hft::instr
