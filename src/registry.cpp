#include "hft/instr/registry.hpp"

#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace hft::instr::detail {
namespace {

std::atomic<RegNode*>& head() noexcept {
    static std::atomic<RegNode*> nodes{nullptr};
    return nodes;
}

}  // namespace

void push_node(RegNode* node) noexcept {
    auto& top = head();
    RegNode* expected = top.load(std::memory_order_relaxed);
    do {
        node->next = expected;
    } while (!top.compare_exchange_weak(expected, node, std::memory_order_release,
                                        std::memory_order_relaxed));
}

void ensure_unique_ids(const std::vector<NameEntry>& entries) {
    std::unordered_map<uint32_t, std::string> seen;
    seen.reserve(entries.size());
    for (const NameEntry& entry : entries) {
        const std::string key = entry.domain + "/" + entry.name;
        const auto it = seen.find(entry.id);
        if (it == seen.end()) {
            seen.emplace(entry.id, key);
        } else if (it->second != key) {
            throw std::runtime_error("marker id collision between " + it->second + " and " + key);
        }
    }
}

std::vector<NameEntry> snapshot_names() {
    std::vector<NameEntry> entries;
    for (RegNode* node = head().load(std::memory_order_acquire); node != nullptr; node = node->next) {
        if (node->domain == nullptr || node->name == nullptr) {
            throw std::runtime_error("marker registry node is missing a name");
        }
        NameEntry entry;
        entry.id = node->id;
        entry.domain.assign(node->domain, node->domain_len);
        entry.name.assign(node->name, node->name_len);
        entries.push_back(std::move(entry));
    }
    ensure_unique_ids(entries);
    std::sort(entries.begin(), entries.end(), [](const NameEntry& a, const NameEntry& b) {
        if (a.domain != b.domain) return a.domain < b.domain;
        if (a.name != b.name) return a.name < b.name;
        return a.id < b.id;
    });
    entries.erase(std::unique(entries.begin(), entries.end(),
                              [](const NameEntry& a, const NameEntry& b) {
                                  return a.id == b.id && a.domain == b.domain && a.name == b.name;
                              }),
                  entries.end());
    return entries;
}

}  // namespace hft::instr::detail
