//===- ActivationOrder.h - Deterministic activation graph ordering -*- C++
//-*-===//

#ifndef OBELISK_RUNTIME_ACTIVATION_ORDER_H
#define OBELISK_RUNTIME_ACTIVATION_ORDER_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <queue>
#include <utility>
#include <vector>

namespace obelisk::runtime {

/// Ordering of possible zero-time activation dependencies. This is not an
/// activation/coalescing certificate. Edges within a component remain live:
/// publications to an already visited member require another sweep. In
/// particular, a mux edge cannot be removed from this graph just because it
/// did not fire in a previous activation (IEEE 1800-2023 4.3--4.7).
///
/// No LLVM or scheduler state is needed, so the same ordering can be used by
/// compilation and by boundary-time rebuilding of a proved active graph.
struct ActivationOrder {
  std::vector<uint32_t> nodes;
  std::vector<uint32_t> componentOffsets;
  std::vector<uint32_t> componentOf;

  /// Dense node IDs, sorted CSR forward/reverse adjacency, iterative SCC
  /// condensation. Stable node identity breaks otherwise unconstrained ties.
  /// Invalid endpoints fail without publishing a partial result.
  static bool build(uint32_t count,
                    std::vector<std::pair<uint32_t, uint32_t>> edges,
                    ActivationOrder &result) {
    for (auto [from, to] : edges)
      if (from >= count || to >= count)
        return false;
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    struct CSR {
      std::vector<size_t> offsets;
      std::vector<uint32_t> targets;
    };
    auto makeCSR = [&](bool reverse) {
      CSR csr{std::vector<size_t>(size_t(count) + 1, 0),
              std::vector<uint32_t>(edges.size())};
      for (auto edge : edges)
        ++csr.offsets[size_t(reverse ? edge.second : edge.first) + 1];
      for (size_t i = 1; i < csr.offsets.size(); ++i)
        csr.offsets[i] += csr.offsets[i - 1];
      auto cursor = csr.offsets;
      for (auto [from, to] : edges) {
        if (reverse)
          std::swap(from, to);
        csr.targets[cursor[from]++] = to;
      }
      return csr;
    };
    CSR forward = makeCSR(false), reverse = makeCSR(true);
    std::vector<uint8_t> seen(count, 0);
    std::vector<uint32_t> finish;
    std::vector<std::pair<uint32_t, size_t>> walk;
    finish.reserve(count);
    for (uint32_t root = 0; root < count; ++root) {
      if (seen[root])
        continue;
      seen[root] = 1;
      walk.emplace_back(root, forward.offsets[root]);
      while (!walk.empty()) {
        auto &[node, cursor] = walk.back();
        if (cursor == forward.offsets[size_t(node) + 1]) {
          finish.push_back(node);
          walk.pop_back();
          continue;
        }
        uint32_t child = forward.targets[cursor++];
        if (!seen[child]) {
          seen[child] = 1;
          walk.emplace_back(child, forward.offsets[child]);
        }
      }
    }
    std::vector<uint32_t> component(count, UINT32_MAX), pending;
    std::vector<std::vector<uint32_t>> members;
    for (auto it = finish.rbegin(); it != finish.rend(); ++it) {
      if (component[*it] != UINT32_MAX)
        continue;
      uint32_t id = members.size();
      members.emplace_back();
      pending.push_back(*it);
      component[*it] = id;
      while (!pending.empty()) {
        uint32_t node = pending.back();
        pending.pop_back();
        members.back().push_back(node);
        for (size_t i = reverse.offsets[node];
             i != reverse.offsets[size_t(node) + 1]; ++i) {
          uint32_t child = reverse.targets[i];
          if (component[child] == UINT32_MAX) {
            component[child] = id;
            pending.push_back(child);
          }
        }
      }
    }
    // Follow dependencies inside a potential SCC instead of sorting its
    // actors by identity. Reverse DFS finish order leaves tree/forward edges
    // in sweep order; actual backward publications still request another
    // sweep. This is a deterministic scheduling heuristic, not a proof that
    // feedback can be discarded or that each actor executes only once.
    std::vector<uint32_t> componentKeys;
    componentKeys.reserve(members.size());
    for (auto &group : members) {
      componentKeys.push_back(*std::min_element(group.begin(), group.end()));
      group.clear();
    }
    for (auto it = finish.rbegin(); it != finish.rend(); ++it)
      members[component[*it]].push_back(*it);
    std::vector<std::vector<uint32_t>> successors(members.size());
    std::vector<size_t> indegree(members.size(), 0);
    for (auto [from, to] : edges)
      if (component[from] != component[to])
        successors[component[from]].push_back(component[to]);
    for (auto &targets : successors) {
      std::sort(targets.begin(), targets.end());
      targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
      for (uint32_t target : targets)
        ++indegree[target];
    }
    using Key = std::pair<uint32_t, uint32_t>;
    std::priority_queue<Key, std::vector<Key>, std::greater<Key>> ready;
    for (uint32_t i = 0; i < members.size(); ++i)
      if (!indegree[i])
        ready.emplace(componentKeys[i], i);
    ActivationOrder ordered;
    ordered.nodes.reserve(count);
    ordered.componentOf.resize(count);
    ordered.componentOffsets.push_back(0);
    while (!ready.empty()) {
      uint32_t id = ready.top().second;
      ready.pop();
      uint32_t rank = ordered.componentOffsets.size() - 1;
      for (uint32_t node : members[id]) {
        ordered.nodes.push_back(node);
        ordered.componentOf[node] = rank;
      }
      ordered.componentOffsets.push_back(ordered.nodes.size());
      for (uint32_t target : successors[id])
        if (!--indegree[target])
          ready.emplace(componentKeys[target], target);
    }
    result = std::move(ordered);
    return true;
  }
};

} // namespace obelisk::runtime

#endif
