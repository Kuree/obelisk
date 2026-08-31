//===- NetConnectivityAnalysis.cpp - Static net topology facts ----------===//

#include "obelisk/Analysis/NetConnectivityAnalysis.h"

#include "obelisk/Analysis/SimulationAnalysis.h"

#include "llvm/ADT/DenseSet.h"

#include <algorithm>

using namespace mlir;

namespace obelisk::analysis {

NetConnectivityAnalysis::NetConnectivityAnalysis(sim::SimDesignOp design) {
  DenseSet<uint64_t> connectedNets;
  for (Operation &operation : design.getBody().front())
    if (auto connection = dyn_cast<sim::SimNetConnectDeclOp>(operation)) {
      connectedNets.insert(connection.getLhsNetId());
      connectedNets.insert(connection.getRhsNetId());
    }

  uint64_t total = 0;
  for (Operation &operation : design.getBody().front()) {
    auto net = dyn_cast<sim::SimNetDeclOp>(operation);
    if (!net)
      continue;
    std::optional<unsigned> width = getSimulationStorageBitWidth(net.getType());
    if (!width)
      continue;
    netWidths[net.getId()] = *width;
    if (!connectedNets.contains(net.getId()) || total > UINT64_MAX - *width)
      continue;
    netBases[net.getId()] = total;
    total += *width;
  }
  auto find = [&](uint64_t value) {
    parents.try_emplace(value, value);
    uint64_t root = value;
    while (parents.lookup(root) != root)
      root = parents.lookup(root);
    while (parents.lookup(value) != value) {
      uint64_t next = parents.lookup(value);
      parents[value] = root;
      value = next;
    }
    return root;
  };
  struct DirectedConnection {
    uint64_t lhs;
    uint64_t rhs;
    std::optional<bool> rhsDominates;
  };
  SmallVector<DirectedConnection> directedConnections;
  DenseMap<uint64_t, NetBit> connectedByFlat;
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
      connectedByFlat.try_emplace(
          lhs,
          NetBit{connection.getLhsNetId(), connection.getLhsOffset() + index});
      connectedByFlat.try_emplace(rhs,
                                  NetBit{connection.getRhsNetId(), rhsOffset});
      directedConnections.push_back({lhs, rhs, connection.getRhsDominates()});
      uint64_t lhsRoot = find(lhs);
      uint64_t rhsRoot = find(rhs);
      if (lhsRoot != rhsRoot)
        parents[std::max(lhsRoot, rhsRoot)] = std::min(lhsRoot, rhsRoot);
    }
  }
  for (const auto &[flat, bit] : connectedByFlat) {
    components[find(flat)].push_back(bit);
    connectedBits.push_back(bit);
  }
  llvm::sort(connectedBits);
  for (auto &[root, members] : components)
    llvm::sort(members);

  for (const auto &[root, members] : components) {
    if (members.size() == 1) {
      dominance[root] = {NetDominanceKind::Isolated, members.front()};
      dominatingBits[root].push_back(members.front());
      continue;
    }
    bool incomplete = false;
    DenseMap<uint64_t, SmallVector<uint64_t, 2>> outgoing;
    for (const DirectedConnection &connection : directedConnections) {
      if (find(connection.lhs) != root)
        continue;
      if (!connection.rhsDominates) {
        incomplete = true;
        continue;
      }
      uint64_t dominated =
          *connection.rhsDominates ? connection.lhs : connection.rhs;
      uint64_t dominating =
          *connection.rhsDominates ? connection.rhs : connection.lhs;
      outgoing[dominated].push_back(dominating);
    }
    if (incomplete) {
      dominance[root] = {NetDominanceKind::Incomplete, members.front()};
      continue;
    }
    SmallVector<uint64_t> sinks;
    for (NetBit member : members) {
      uint64_t flat = netBases.lookup(member.net) + member.offset;
      if (!outgoing.count(flat))
        sinks.push_back(flat);
    }
    DenseMap<uint64_t, uint64_t> incomingCount;
    SmallVector<uint64_t> pending;
    for (NetBit member : members)
      incomingCount[netBases.lookup(member.net) + member.offset] = 0;
    for (const auto &entry : outgoing)
      for (uint64_t target : entry.second)
        ++incomingCount[target];
    for (const auto &[bit, count] : incomingCount)
      if (count == 0)
        pending.push_back(bit);
    size_t visited = 0;
    while (!pending.empty()) {
      uint64_t bit = pending.pop_back_val();
      ++visited;
      auto targets = outgoing.find(bit);
      if (targets != outgoing.end())
        for (uint64_t target : targets->second)
          if (--incomingCount[target] == 0)
            pending.push_back(target);
    }
    // In a finite acyclic graph with exactly one sink, every member reaches
    // that sink. A residual node therefore identifies a dominance cycle.
    if (visited != members.size()) {
      dominance[root] = {NetDominanceKind::Ambiguous, members.front()};
      continue;
    }
    for (uint64_t sink : sinks)
      for (NetBit member : members)
        if (netBases.lookup(member.net) + member.offset == sink) {
          dominatingBits[root].push_back(member);
          break;
        }
    if (dominatingBits[root].size() == 1)
      dominance[root] = {NetDominanceKind::Unique,
                         dominatingBits[root].front()};
    else
      dominance[root] = {NetDominanceKind::Ambiguous, members.front()};
  }
}

ArrayRef<NetBit> NetConnectivityAnalysis::getComponent(NetBit bit) const {
  auto base = netBases.find(bit.net);
  auto width = netWidths.find(bit.net);
  if (base == netBases.end() || width == netWidths.end() ||
      bit.offset >= width->second)
    return {};
  uint64_t root = base->second + bit.offset;
  auto parent = parents.find(root);
  if (parent == parents.end())
    return {};
  while (parent->second != root) {
    root = parent->second;
    parent = parents.find(root);
  }
  auto found = components.find(root);
  return found == components.end() ? ArrayRef<NetBit>()
                                   : ArrayRef<NetBit>(found->second);
}

NetBit NetConnectivityAnalysis::getCanonical(NetBit bit) const {
  ArrayRef<NetBit> members = getComponent(bit);
  return members.empty() ? bit : members.front();
}

NetDominance NetConnectivityAnalysis::getDominance(NetBit bit) const {
  auto base = netBases.find(bit.net);
  auto width = netWidths.find(bit.net);
  if (width == netWidths.end() || bit.offset >= width->second)
    return {NetDominanceKind::Incomplete, bit};
  if (base == netBases.end())
    return {NetDominanceKind::Isolated, bit};
  uint64_t root = base->second + bit.offset;
  auto parent = parents.find(root);
  if (parent == parents.end())
    return {NetDominanceKind::Isolated, bit};
  while (parent->second != root) {
    root = parent->second;
    parent = parents.find(root);
  }
  auto found = dominance.find(root);
  return found == dominance.end()
             ? NetDominance{NetDominanceKind::Incomplete, bit}
             : found->second;
}

ArrayRef<NetBit> NetConnectivityAnalysis::getDominatingBits(NetBit bit) const {
  auto base = netBases.find(bit.net);
  auto width = netWidths.find(bit.net);
  if (base == netBases.end() || width == netWidths.end() ||
      bit.offset >= width->second)
    return {};
  uint64_t root = base->second + bit.offset;
  auto parent = parents.find(root);
  if (parent == parents.end())
    return {};
  while (parent->second != root) {
    root = parent->second;
    parent = parents.find(root);
  }
  auto found = dominatingBits.find(root);
  return found == dominatingBits.end() ? ArrayRef<NetBit>()
                                       : ArrayRef<NetBit>(found->second);
}

std::optional<uint64_t>
NetConnectivityAnalysis::getNetWidth(uint64_t net) const {
  auto found = netWidths.find(net);
  if (found == netWidths.end())
    return std::nullopt;
  return found->second;
}

} // namespace obelisk::analysis
