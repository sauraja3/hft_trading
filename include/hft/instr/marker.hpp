#pragma once

#include "hft/instr/id.hpp"
#include "hft/instr/recorder.hpp"
#include "hft/instr/registry.hpp"

#include <string_view>

namespace hft::instr {
namespace detail {

// Static-init registration. Not referenced by record(), so a disabled marker
// still has a pure-function id the decoder can recompute.
template <class DomainType, class MarkerType>
struct Registrar {
    RegNode node{};

    Registrar() {
        if constexpr (DomainType::kEnabled) {
            node.id = MarkerType::kId;
            node.domain = DomainType::kNameStorage;
            node.domain_len = static_cast<uint16_t>(sizeof(DomainType::kNameStorage) - 1);
            node.name = MarkerType::kStorage;
            node.name_len = static_cast<uint16_t>(sizeof(MarkerType::kStorage) - 1);
            push_node(&node);
        }
    }
};

}  // namespace detail

// A domain is a type with a constexpr name and one enable flag. The flag is
// not a class-type template argument: C++17 cannot use a string literal as a
// non-type template parameter. The macro stores the literal in the type.
//
//   inline constexpr bool kMarketDataMarkers = true;
//   HFT_INSTR_DOMAIN(MarketData, "market_data", kMarketDataMarkers);
#define HFT_INSTR_DOMAIN(alias, name_lit, enabled_value)                               \
    struct alias {                                                                     \
        static_assert(::hft::instr::is_identifier(name_lit),                           \
                      "domain name must be an identifier");                            \
        static_assert(sizeof(name_lit) <= 1024, "domain name is too long");           \
        static constexpr bool kEnabled = static_cast<bool>(enabled_value);            \
        static constexpr char kNameStorage[] = name_lit;                              \
    }

// Defines `var` and registers it for the trace name table.
// Use at namespace scope:
//   HFT_INSTR_MARKER(decode_begin, MarketData, "decode_begin");
//   decode_begin.record(recorder);
// record() compiles away completely when the domain is disabled (if constexpr).
#define HFT_INSTR_MARKER(var, DomainType, name_lit)                                    \
    struct var##_hft_marker_ {                                                         \
        static_assert(::hft::instr::is_identifier(name_lit),                           \
                      "marker name must be an identifier");                            \
        static_assert(sizeof(name_lit) <= 1024, "marker name is too long");           \
        static constexpr char kStorage[] = name_lit;                                   \
        static constexpr bool kEnabled = DomainType::kEnabled;                        \
        static constexpr uint32_t kId = ::hft::instr::make_marker_id(                 \
            std::string_view(DomainType::kNameStorage,                                 \
                             sizeof(DomainType::kNameStorage) - 1),                    \
            std::string_view(kStorage, sizeof(kStorage) - 1));                         \
        [[gnu::always_inline]] void record(::hft::instr::Recorder& recorder) const noexcept { \
            if constexpr (kEnabled) {                                                  \
                recorder.record(kId);                                                  \
            } else {                                                                   \
                (void)recorder;                                                        \
            }                                                                          \
        }                                                                              \
    };                                                                                 \
    inline constexpr var##_hft_marker_ var{};                                          \
    [[gnu::used]] inline const ::hft::instr::detail::Registrar<DomainType, var##_hft_marker_> \
        var##_hft_reg_ {}

}  // namespace hft::instr
