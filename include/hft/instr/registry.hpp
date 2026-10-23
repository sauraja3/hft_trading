#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hft::instr::detail {

struct RegNode {
    uint32_t id = 0;
    uint16_t domain_len = 0;
    uint16_t name_len = 0;
    const char* domain = nullptr;
    const char* name = nullptr;
    RegNode* next = nullptr;
};

struct NameEntry {
    uint32_t id = 0;
    std::string domain;
    std::string name;
};

// Lock-free prepend. Nodes are immortal (static storage). Called only from
// static initialization, never from the marker hot path.
void push_node(RegNode* node) noexcept;

// Copies the current list and throws std::runtime_error on an id collision
// between two different domain/name pairs.
std::vector<NameEntry> snapshot_names();

void ensure_unique_ids(const std::vector<NameEntry>& entries);

}  // namespace hft::instr::detail
