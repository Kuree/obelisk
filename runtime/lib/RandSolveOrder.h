//===- RandSolveOrder.h - Constraint variable ordering --------------------===//

#ifndef OBELISK_RUNTIME_RANDSOLVEORDER_H
#define OBELISK_RUNTIME_RANDSOLVEORDER_H

#include "obelisk/Runtime/Runtime.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <unordered_map>
#include <vector>

namespace obelisk::runtime {

using OrderMask = std::vector<uint64_t>;
struct PriorityInput {
  OrderMask references, arguments;
  uint32_t maskCapture = 0;
};
using PriorityInputs = std::unordered_map<uint32_t, PriorityInput>;

inline uint32_t orderRead32(const uint8_t *p) {
  uint32_t value = 0;
  for (unsigned byte = 0; byte != 4; ++byte)
    value |= uint32_t(p[byte]) << (8 * byte);
  return value;
}

inline uint64_t orderRead64(const uint8_t *p) {
  uint64_t value = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    value |= uint64_t(p[byte]) << (8 * byte);
  return value;
}

inline bool readPriorityInputs(const uint8_t *program, size_t size,
                               size_t header, uint32_t instructionCount,
                               uint32_t width, size_t &offset,
                               PriorityInputs &inputs, size_t captureCount = 0,
                               bool dynamicMasks = false) {
  if (offset > size ||
      size - offset < OBELISK_RT_RANDOM_PRIORITY_INPUT_HEADER_SIZE)
    return false;
  uint32_t count = orderRead32(program + offset);
  offset += OBELISK_RT_RANDOM_PRIORITY_INPUT_HEADER_SIZE;
  size_t words = (uint64_t(width) + 63) / 64;
  uint64_t stride = OBELISK_RT_RANDOM_PRIORITY_INPUT_RECORD_HEADER_SIZE +
                    uint64_t(words) * 16;
  if (count > (size - offset) / stride)
    return false;
  bool sawDynamicMask = false;
  for (uint32_t record = 0; record != count; ++record) {
    uint32_t index = orderRead32(program + offset);
    uint32_t maskCapture = orderRead32(program + offset + 4);
    if (index >= instructionCount ||
        (maskCapture && (!dynamicMasks || maskCapture >= captureCount)))
      return false;
    uint8_t opcode = program[header + size_t(index) * 16];
    if (opcode != OBELISK_RT_RANDOM_END_HARD_V1 &&
        opcode != OBELISK_RT_RANDOM_END_SOFT_V1)
      return false;
    PriorityInput input;
    input.maskCapture = maskCapture;
    sawDynamicMask |= maskCapture != 0;
    size_t cursor = offset + 8;
    for (OrderMask *mask : {&input.references, &input.arguments}) {
      for (size_t word = 0; word != words; ++word) {
        mask->push_back(orderRead64(program + cursor));
        cursor += 8;
      }
      if (width % 64 && (mask->back() >> (width % 64)) != 0)
        return false;
    }
    for (size_t word = 0; word != words; ++word)
      if (input.arguments[word] & ~input.references[word])
        return false;
    if (!inputs.emplace(index, std::move(input)).second)
      return false;
    offset += stride;
  }
  if (dynamicMasks && !sawDynamicMask)
    return false;
  if (count != 0)
    for (uint32_t index = 0; index != instructionCount; ++index) {
      uint8_t opcode = program[header + size_t(index) * 16];
      if ((opcode == OBELISK_RT_RANDOM_END_HARD_V1 ||
           opcode == OBELISK_RT_RANDOM_END_SOFT_V1) &&
          inputs.find(index) == inputs.end())
        return false;
    }
  return true;
}

struct SolveOrderEdge {
  OrderMask before, after;
  uint32_t block;
  bool functionPriority = false;
};

inline bool orderIntersects(const OrderMask &a, const OrderMask &b) {
  for (size_t word = 0; word != a.size(); ++word)
    if ((a[word] & b[word]) != 0)
      return true;
  return false;
}

inline void orderUnite(OrderMask &a, const OrderMask &b) {
  for (size_t word = 0; word != a.size(); ++word)
    a[word] |= b[word];
}

/// Explicit orders advance a conditional projection. Function argument edges
/// additionally separate constraint-solving stages (IEEE 1800-2023 18.5.11).
/// An explicit edge inside a function stage is handled by that stage's own
/// conditional sampler, rather than creating another constraint subdivision.
inline bool
buildSolveOrderLayers(const std::vector<SolveOrderEdge> &edges,
                      const OrderMask &mutableMask, uint64_t disabledBlocks,
                      bool functionStages, std::vector<OrderMask> &layers,
                      bool explicitOnly = false,
                      const OrderMask *functionArguments = nullptr) {
  std::vector<SolveOrderEdge> active;
  std::vector<OrderMask> nodes;
  auto addNode = [&](OrderMask node) {
    for (size_t index = 0; index != nodes.size();) {
      if (!orderIntersects(nodes[index], node)) {
        ++index;
        continue;
      }
      orderUnite(node, nodes[index]);
      nodes.erase(nodes.begin() + index);
    }
    nodes.push_back(std::move(node));
  };
  for (const SolveOrderEdge &edge : edges) {
    if (edge.before.size() != mutableMask.size() ||
        edge.after.size() != mutableMask.size() ||
        (edge.block != OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1 &&
         edge.block >= 64))
      return false;
    if ((explicitOnly && edge.functionPriority) ||
        (edge.block != OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1 &&
         ((disabledBlocks >> edge.block) & 1) != 0))
      continue;
    SolveOrderEdge filtered{OrderMask(mutableMask.size()),
                            OrderMask(mutableMask.size()), edge.block,
                            edge.functionPriority};
    for (size_t word = 0; word != mutableMask.size(); ++word) {
      filtered.before[word] = edge.before[word] & mutableMask[word];
      filtered.after[word] = edge.after[word] & mutableMask[word];
    }
    if (!std::any_of(filtered.before.begin(), filtered.before.end(),
                     [](uint64_t word) { return word != 0; }) ||
        !std::any_of(filtered.after.begin(), filtered.after.end(),
                     [](uint64_t word) { return word != 0; }))
      continue;
    addNode(filtered.before);
    addNode(filtered.after);
    active.push_back(std::move(filtered));
  }
  OrderMask arguments(mutableMask.size(), 0);
  if (functionStages) {
    if (functionArguments) {
      if (functionArguments->size() != mutableMask.size())
        return false;
      arguments = *functionArguments;
    }
    for (const SolveOrderEdge &edge : active)
      if (edge.functionPriority)
        orderUnite(arguments, edge.before);
    for (size_t word = 0; word != arguments.size(); ++word)
      arguments[word] &= mutableMask[word];
    OrderMask uncovered = arguments;
    for (const OrderMask &node : nodes)
      for (size_t word = 0; word != uncovered.size(); ++word)
        uncovered[word] &= ~node[word];
    if (std::any_of(uncovered.begin(), uncovered.end(),
                    [](uint64_t word) { return word != 0; }))
      addNode(std::move(uncovered));
  }
  // Build a graph of the transitive overlap classes. Self edges are cycles.
  struct Arc {
    size_t target;
    unsigned distance;
  };
  std::vector<std::vector<Arc>> successors(nodes.size());
  for (const SolveOrderEdge &edge : active) {
    size_t before = 0, after = 0;
    while (!orderIntersects(nodes[before], edge.before))
      ++before;
    while (!orderIntersects(nodes[after], edge.after))
      ++after;
    successors[before].push_back(
        {after, functionStages ? unsigned(edge.functionPriority) : 1u});
  }
  size_t unset = std::numeric_limits<size_t>::max();
  std::vector<size_t> ranks(nodes.size(), unset);
  size_t remaining = nodes.size(), maximum = 0;
  while (remaining != 0) {
    bool progress = false;
    for (size_t node = 0; node != nodes.size(); ++node) {
      if (ranks[node] != unset)
        continue;
      size_t rank =
          functionStages && orderIntersects(nodes[node], arguments) ? 1 : 0;
      bool ready = true;
      for (Arc arc : successors[node]) {
        if (ranks[arc.target] == unset) {
          ready = false;
          break;
        }
        rank = std::max(rank, ranks[arc.target] + arc.distance);
      }
      if (!ready)
        continue;
      ranks[node] = rank;
      maximum = std::max(maximum, rank);
      --remaining;
      progress = true;
    }
    if (!progress)
      return false;
  }
  layers.clear();
  if (!nodes.empty()) {
    layers.assign(maximum + 1, OrderMask(mutableMask.size(), 0));
    OrderMask ordered(mutableMask.size(), 0);
    for (size_t node = 0; node != nodes.size(); ++node) {
      orderUnite(layers[maximum - ranks[node]], nodes[node]);
      orderUnite(ordered, nodes[node]);
    }
    for (size_t word = 0; word != mutableMask.size(); ++word)
      layers.back()[word] |= mutableMask[word] & ~ordered[word];
  } else if (functionStages) {
    layers.push_back(mutableMask);
  }
  return true;
}

/// IEEE 1800-2023 18.9: the edges of a constraint block are active only while
/// it is enabled, so declared metadata is invalid only for a cycle within one
/// block or within the unmaskable inline edges. A cycle spanning blocks is an
/// error only once all of them are enabled, and is reported at solve time.
inline bool
validateSolveOrderDeclarations(const std::vector<SolveOrderEdge> &edges,
                               const OrderMask &mutableMask) {
  std::vector<uint32_t> blocks;
  for (const SolveOrderEdge &edge : edges)
    if (std::find(blocks.begin(), blocks.end(), edge.block) == blocks.end())
      blocks.push_back(edge.block);
  std::vector<SolveOrderEdge> declared;
  std::vector<OrderMask> layers;
  for (uint32_t block : blocks) {
    declared.clear();
    for (const SolveOrderEdge &edge : edges)
      if (edge.block == block)
        declared.push_back(edge);
    if (!buildSolveOrderLayers(declared, mutableMask, 0, false, layers))
      return false;
  }
  return true;
}

// IEEE 1800-2023 18.5.9: a cycle among the solve before edges of the active
// constraint blocks is an error, and randomize() then fails (18.6.3). Declared
// metadata is validated per block, so a layering failure at solve time is
// such a cycle.
inline void reportActiveSolveOrderCycle(obelisk_rt_context *context) {
  std::fprintf(stderr, "error: solve before ordering of the active "
                       "constraint blocks contains a cycle\n");
  obelisk_rt_v1_scheduler_error(context);
}

} // namespace obelisk::runtime

#endif
