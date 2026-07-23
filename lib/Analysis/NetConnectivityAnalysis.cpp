//===- NetConnectivityAnalysis.cpp - Static net topology facts ----------===//

#include "obelisk/Analysis/NetConnectivityAnalysis.h"

#include <algorithm>

using namespace mlir;

namespace obelisk::analysis {

NetConnectivityAnalysis::NetConnectivityAnalysis(sim::SimDesignOp design) {
  uint64_t total = 0;
  for (Operation &operation : design.getBody().front()) {
    auto net = dyn_cast<sim::SimNetDeclOp>(operation);
    if (!net)
      continue;
    std::optional<unsigned> width = sim::getPackedWidth(net.getType());
    if (!width || total > UINT64_MAX - *width)
      continue;
    netBases[net.getId()] = total;
    netWidths[net.getId()] = *width;
    total += *width;
  }
  parents.resize(total);
  for (uint64_t index = 0; index != total; ++index)
    parents[index] = index;

  auto find = [&](uint64_t value) {
    uint64_t root = value;
    while (parents[root] != root)
      root = parents[root];
    while (parents[value] != value) {
      uint64_t next = parents[value];
      parents[value] = root;
      value = next;
    }
    return root;
  };
  for (Operation &operation : design.getBody().front()) {
    auto connection = dyn_cast<sim::SimNetConnectDeclOp>(operation);
    if (!connection || !netBases.count(connection.getLhsNetId()) ||
        !netBases.count(connection.getRhsNetId()))
      continue;
    for (uint64_t index = 0; index != connection.getWidth(); ++index) {
      uint64_t lhs = netBases.lookup(connection.getLhsNetId()) +
                     connection.getLhsOffset() + index;
      uint64_t rhsOffset = connection.getRhsReversed()
                               ? connection.getRhsOffset() - index
                               : connection.getRhsOffset() + index;
      uint64_t rhs = netBases.lookup(connection.getRhsNetId()) + rhsOffset;
      uint64_t lhsRoot = find(lhs);
      uint64_t rhsRoot = find(rhs);
      if (lhsRoot != rhsRoot)
        parents[std::max(lhsRoot, rhsRoot)] = std::min(lhsRoot, rhsRoot);
    }
  }
  for (auto [net, base] : netBases)
    for (uint64_t offset = 0; offset != netWidths.lookup(net); ++offset)
      components[find(base + offset)].push_back({net, offset});
  for (auto &[root, members] : components)
    llvm::sort(members);
}

ArrayRef<NetBit> NetConnectivityAnalysis::getComponent(NetBit bit) const {
  auto base = netBases.find(bit.net);
  auto width = netWidths.find(bit.net);
  if (base == netBases.end() || width == netWidths.end() ||
      bit.offset >= width->second)
    return {};
  uint64_t root = base->second + bit.offset;
  while (parents[root] != root)
    root = parents[root];
  auto found = components.find(root);
  return found == components.end() ? ArrayRef<NetBit>()
                                   : ArrayRef<NetBit>(found->second);
}

NetBit NetConnectivityAnalysis::getCanonical(NetBit bit) const {
  ArrayRef<NetBit> members = getComponent(bit);
  return members.empty() ? bit : members.front();
}

std::optional<uint64_t>
NetConnectivityAnalysis::getNetWidth(uint64_t net) const {
  auto found = netWidths.find(net);
  if (found == netWidths.end())
    return std::nullopt;
  return found->second;
}

} // namespace obelisk::analysis
