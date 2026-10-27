//===- NetConnectivityAnalysis.cpp - Static net topology facts ----------===//

#include "talos/Analysis/NetConnectivityAnalysis.h"

#include "talos/Analysis/SimulationAnalysis.h"
#include "talos/Dialect/Simulation/SimulationOps.h"

#include "llvm/ADT/DenseSet.h"

#include <algorithm>
#include <memory>
#include <mutex>

using namespace mlir;

namespace talos::analysis {

namespace {
/// Content digest of the declarations the topology depends on. Attribute
/// dictionaries are uniqued, so their identity covers ids, offsets, widths,
/// strengths, delays and types.
uint64_t inventoryDigest(sim::SimDesignOp design) {
  llvm::hash_code digest = llvm::hash_value(size_t{0});
  for (Operation &op : design.getBody().front())
    if (isa<sim::SimNetDeclOp, sim::SimDriverDeclOp, sim::SimNetConnectDeclOp,
            sim::SimPassSwitchDeclOp>(op))
      digest = llvm::hash_combine(digest, op.getName().getAsOpaquePointer(),
                                  op.getAttrDictionary().getAsOpaquePointer());
  return digest;
}
struct TopologyCache {
  std::mutex mutex;
  llvm::DenseMap<Operation *,
                 std::pair<uint64_t,
                           std::shared_ptr<const IsolatedWireDriverTopology>>>
      entries;
};
TopologyCache &getTopologyCache() {
  static TopologyCache cache;
  return cache;
}
IsolatedWireDriverTopology computeIsolatedWireDriverTopology(
    sim::SimDesignOp design);
} // namespace

std::shared_ptr<const IsolatedWireDriverTopology>
getIsolatedWireDriverTopology(sim::SimDesignOp design) {
  uint64_t digest = inventoryDigest(design);
  TopologyCache &cache = getTopologyCache();
  {
    std::lock_guard<std::mutex> lock(cache.mutex);
    auto found = cache.entries.find(design.getOperation());
    if (found != cache.entries.end() && found->second.first == digest)
      return found->second.second;
  }
  auto result = std::make_shared<const IsolatedWireDriverTopology>(
      computeIsolatedWireDriverTopology(design));
  std::lock_guard<std::mutex> lock(cache.mutex);
  cache.entries[design.getOperation()] = {digest, result};
  return result;
}

namespace {
IsolatedWireDriverTopology
computeIsolatedWireDriverTopology(sim::SimDesignOp design) {
  IsolatedWireDriverTopology result;
  DenseSet<uint64_t> connected;
  DenseMap<uint64_t, SmallVector<sim::SimDriverDeclOp>> drivers;
  for (Operation &op : design.getBody().front()) {
    if (isa<sim::SimPassSwitchDeclOp>(op))
      return result;
    if (auto connection = dyn_cast<sim::SimNetConnectDeclOp>(op)) {
      connected.insert(connection.getLhsNetId());
      connected.insert(connection.getRhsNetId());
    }
    if (auto driver = dyn_cast<sim::SimDriverDeclOp>(op))
      drivers[driver.getNetId()].push_back(driver);
  }
  for (auto net : design.getOps<sim::SimNetDeclOp>()) {
    auto width = sim::getProvenanceSpan(net.getType());
    auto found = drivers.find(net.getId());
    if (!width || connected.contains(net.getId()) || net.getNettypeAttr() ||
        net.getResolutionKind() != sim::NetResolutionKind::Wire ||
        found == drivers.end())
      continue;
    if (auto delays = net.getPropagationDelays())
      if (llvm::any_of(*delays, [](int64_t delay) { return delay != -1; }))
        continue;
    SmallVector<std::pair<uint64_t, uint64_t>> ranges;
    bool valid = true;
    for (auto driver : found->second) {
      uint64_t low = driver.getDrivenLow().value_or(0);
      uint64_t size = driver.getDrivenWidth().value_or(*width);
      if (sim::getProvenanceSpan(driver.getType()) != width || !size ||
          low > *width || size > *width - low ||
          driver.getStrength0() != sim::Strength::Strong ||
          driver.getStrength1() != sim::Strength::Strong) {
        valid = false;
        break;
      }
      ranges.emplace_back(low, low + size);
    }
    if (!valid)
      continue;
    llvm::sort(ranges);
    uint64_t end = 0;
    bool complete = true;
    for (auto range : ranges) {
      if (range.first < end) {
        valid = false;
        break;
      }
      complete &= range.first == end;
      end = range.second;
    }
    if (!valid)
      continue;
    // IEEE 1800-2023 6.6.1: resolution is per bit. A partition into strong
    // contributions is a collection of independent wire copies. Requiring
    // full coverage also excludes persistent undriven Z holes from this proof.
    if (complete && end == *width) {
      result.partitionedDriverNets.insert(net.getId());
      if (ranges.size() == 1)
        result.singleDriverNets.insert(net.getId());
    } else {
      // LRM 6.6 and 6.7: the remaining bits stay Z. Keep sparse exclusivity
      // separate from complete coverage so consumers cannot infer that a
      // known contribution makes the whole net a two-state value.
      result.sparseDriverRanges.try_emplace(net.getId(), std::move(ranges));
    }
  }
  if (connected.empty())
    return result;

  // IEEE 1800-2023 23.3.3.7: nets connected through a port may be merged into
  // a single simulated net, which takes the dominating net's delay; the
  // drivers of every collapsed member are then contributions to that one net.
  // A bit is exclusively driven when its whole component is plain wire
  // (Table 23-1 keeps wire/tri for wire/tri members), no member declares a
  // delay (stricter than the dominating net's delay) and exactly one strong
  // contribution reaches it: Table 6-2 (6.6.1) resolves a lone contribution
  // against z to its own value in every member. Driver legality is checked
  // for the whole member net, as above, so a malformed driver keeps its
  // component resolved.
  struct ConnectedNet {
    uint64_t width = 0;
    bool plain = false;
  };
  DenseMap<uint64_t, ConnectedNet> connectedNets;
  for (auto net : design.getOps<sim::SimNetDeclOp>()) {
    if (!connected.contains(net.getId()))
      continue;
    auto width = sim::getProvenanceSpan(net.getType());
    auto storageWidth = getSimulationStorageBitWidth(net.getType());
    ConnectedNet info;
    info.width = width.value_or(0);
    info.plain = width && storageWidth && *width == *storageWidth &&
                 !net.getNettypeAttr() &&
                 net.getResolutionKind() == sim::NetResolutionKind::Wire;
    if (auto delays = net.getPropagationDelays())
      info.plain &= llvm::none_of(*delays,
                                  [](int64_t delay) { return delay != -1; });
    connectedNets[net.getId()] = info;
  }
  // Strong contributions covering one bit, or nothing for a malformed or
  // non-strong driver of the net.
  auto contributions = [&](uint64_t net,
                           uint64_t bit) -> std::optional<unsigned> {
    uint64_t width = connectedNets.lookup(net).width;
    auto found = drivers.find(net);
    unsigned count = 0;
    if (found == drivers.end())
      return count;
    for (auto driver : found->second) {
      uint64_t low = driver.getDrivenLow().value_or(0);
      uint64_t size = driver.getDrivenWidth().value_or(width);
      if (sim::getProvenanceSpan(driver.getType()) != width || !size ||
          low > width || size > width - low ||
          driver.getStrength0() != sim::Strength::Strong ||
          driver.getStrength1() != sim::Strength::Strong)
        return std::nullopt;
      count += bit >= low && bit < low + size;
    }
    return count;
  };
  NetConnectivityAnalysis connectivity(design);
  DenseMap<std::pair<uint64_t, uint64_t>, bool> exclusiveComponents;
  for (const auto &[netId, info] : connectedNets) {
    if (!info.plain)
      continue;
    SmallVector<std::pair<uint64_t, uint64_t>> ranges;
    SmallVector<IsolatedWireDriverTopology::AliasRun> runs;
    // Runs ending at the current bit, extended before new ones start.
    SmallVector<unsigned> open, nextOpen;
    for (uint64_t bit = 0; bit != info.width; ++bit) {
      NetBit self{netId, bit};
      ArrayRef<NetBit> component = connectivity.getComponent(self);
      if (component.empty())
        component = ArrayRef(self);
      NetBit canonical = component.front();
      auto [slot, inserted] = exclusiveComponents.try_emplace(
          std::make_pair(canonical.net, canonical.offset), false);
      if (inserted) {
        unsigned total = 0;
        bool plain = true;
        for (NetBit member : component) {
          auto memberInfo = connectedNets.find(member.net);
          std::optional<unsigned> count;
          if (memberInfo != connectedNets.end() && memberInfo->second.plain &&
              member.offset < memberInfo->second.width)
            count = contributions(member.net, member.offset);
          if (!count) {
            plain = false;
            break;
          }
          total += *count;
        }
        slot->second = plain && total == 1;
      }
      nextOpen.clear();
      if (!slot->second) {
        open.clear();
        continue;
      }
      if (!ranges.empty() && ranges.back().second == bit)
        ++ranges.back().second;
      else
        ranges.emplace_back(bit, bit + 1);
      for (NetBit member : component) {
        if (member == self)
          continue;
        bool extended = false;
        for (unsigned index : open) {
          auto &run = runs[index];
          if (run.member != member.net)
            continue;
          // A one-bit run takes the direction of its second bit.
          bool ascending = (run.width == 1 || !run.reversed) &&
                           member.offset == run.memberLow + run.width;
          bool descending = (run.width == 1 || run.reversed) &&
                            run.memberLow != 0 &&
                            member.offset == run.memberLow - 1;
          if (!ascending && !descending)
            continue;
          if (descending)
            run.memberLow = member.offset;
          run.reversed = descending;
          ++run.width;
          nextOpen.push_back(index);
          extended = true;
          break;
        }
        if (extended)
          continue;
        nextOpen.push_back(runs.size());
        runs.push_back({bit, 1, member.net, member.offset,
                        connectedNets.lookup(member.net).width, false});
      }
      std::swap(open, nextOpen);
    }
    if (ranges.empty())
      continue;
    result.connectedExclusiveRanges.try_emplace(netId, std::move(ranges));
    if (!runs.empty())
      result.connectedExclusiveAliases.try_emplace(netId, std::move(runs));
  }
  return result;
}
} // namespace

DenseSet<uint64_t> getIsolatedSingleDriverNets(sim::SimDesignOp design) {
  return getIsolatedWireDriverTopology(design)->singleDriverNets;
}

DenseSet<uint64_t> getIsolatedPartitionedDriverNets(sim::SimDesignOp design) {
  return getIsolatedWireDriverTopology(design)->partitionedDriverNets;
}

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

  // Partition each directed connection by its canonical component once.
  DenseMap<uint64_t, SmallVector<DirectedConnection>> connectionsByRoot;
  for (const DirectedConnection &connection : directedConnections)
    connectionsByRoot[find(connection.lhs)].push_back(connection);
  for (const auto &[root, members] : components) {
    if (members.size() == 1) {
      dominance[root] = {NetDominanceKind::Isolated, members.front()};
      dominatingBits[root].push_back(members.front());
      continue;
    }
    bool incomplete = false;
    DenseMap<uint64_t, SmallVector<uint64_t, 2>> outgoing;
    for (const DirectedConnection &connection :
         connectionsByRoot.lookup(root)) {
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

} // namespace talos::analysis
