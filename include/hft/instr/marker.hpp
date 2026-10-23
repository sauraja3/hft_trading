#pragma once

#include "hft/instr/fixed_string.hpp"
#include "hft/instr/id.hpp"
#include "hft/instr/recorder.hpp"
#include "hft/instr/registry.hpp"

namespace hft::instr {

// One bool for the whole domain. Call sites do not branch and do not carry ifdefs.
//   inline constexpr bool kMarketDataMarkers = true;
//   using MarketData = hft::instr::Domain<"market_data", kMarketDataMarkers>;
template <FixedString Name, bool Enabled>
struct Domain {
    static_assert(is_identifier(Name.view()), "domain name must be an identifier");
    static constexpr bool kEnabled = Enabled;
    static constexpr FixedString kName = Name;
};

// A named point inside a domain. record() compiles away completely when the
// domain is disabled (if constexpr, not a runtime check).
template <class DomainType, FixedString Name>
struct Marker {
    static_assert(is_identifier(Name.view()), "marker name must be an identifier");
    static_assert(DomainType::kName.view().size() < 1024, "domain name is too long");
    static_assert(Name.view().size() < 1024, "marker name is too long");

    static constexpr bool kEnabled = DomainType::kEnabled;
    static constexpr uint32_t kId = make_marker_id(DomainType::kName.view(), Name.view());
    static constexpr auto kDomainName = DomainType::kName;
    static constexpr auto kMarkerName = Name;

    [[gnu::always_inline]] void record(Recorder& recorder) const noexcept {
        if constexpr (kEnabled) {
            recorder.record(kId);
        } else {
            (void)recorder;
        }
    }
};

namespace detail {

// Static-init registration. Not referenced by record(), so a disabled or
// unregistered marker still has a pure-function id the decoder can recompute.
template <class DomainType, FixedString Name>
struct Registrar {
    static constexpr auto kDomain = DomainType::kName;
    static constexpr auto kMarker = Name;
    RegNode node{};

    Registrar() {
        if constexpr (DomainType::kEnabled) {
            node.id = Marker<DomainType, Name>::kId;
            node.domain = kDomain.data;
            node.domain_len = static_cast<uint16_t>(kDomain.view().size());
            node.name = kMarker.data;
            node.name_len = static_cast<uint16_t>(kMarker.view().size());
            push_node(&node);
        }
    }
};

}  // namespace detail

// Defines `var` and registers it for the trace name table.
// Use at namespace scope:
//   HFT_INSTR_MARKER(decode_begin, MarketData, "decode_begin");
//   decode_begin.record(recorder);
#define HFT_INSTR_MARKER(var, DomainType, name_lit)                                    \
    inline constexpr ::hft::instr::Marker<DomainType, name_lit> var{};                \
    [[gnu::used]] inline const ::hft::instr::detail::Registrar<DomainType, name_lit>  \
        var##_hft_reg_ {}

}  // namespace hft::instr
