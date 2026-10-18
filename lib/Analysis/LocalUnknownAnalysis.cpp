//===- LocalUnknownAnalysis.cpp - X/Z inside one gated activation ---------===//

#include "obelisk/Analysis/LocalUnknownAnalysis.h"
#include "obelisk/Analysis/KnownVersioningAnalysis.h"
#include "obelisk/Analysis/LocalStorageAnalysis.h"
#include "obelisk/Analysis/StateDomainAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Dialect/Simulation/SimulationTypes.h"

#include "mlir/Analysis/CallGraph.h"
#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/DenseAnalysis.h"
#include "mlir/Analysis/DataFlow/SparseAnalysis.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/TypeSwitch.h"

#include <algorithm>
#include <memory>

using namespace mlir;
namespace obelisk::analysis {
namespace {

using ReadKind = LocalUnknownAnalysis::ReadKind;

/// A segment's entry check tests private slots up to this width.
constexpr uint64_t maximumCheckedSlotBits = 256;

//===----------------------------------------------------------------------===//
// State domains with and without the entry check
//===----------------------------------------------------------------------===//

/// Bottom < TwoState < MayFourState.
class DomainValue {
public:
  DomainValue() = default;
  explicit DomainValue(StateDomain domain) : domain(domain) {}
  static DomainValue join(const DomainValue &lhs, const DomainValue &rhs) {
    return DomainValue(std::max(lhs.domain, rhs.domain));
  }
  bool operator==(const DomainValue &other) const {
    return domain == other.domain;
  }
  void print(raw_ostream &os) const { os << stringifyStateDomain(domain); }
  StateDomain get() const { return domain; }

private:
  StateDomain domain = StateDomain::Bottom;
};

/// Domains in the gated copy.
class GatedLattice final : public dataflow::Lattice<DomainValue> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(GatedLattice)
  using Lattice::Lattice;
};

/// Domains in the original body, where every canonical read is four-state.
class BaselineLattice final : public dataflow::Lattice<DomainValue> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(BaselineLattice)
  using Lattice::Lattice;
};

/// A canonical read's bits in one dense root numbering.
struct ReadSite {
  ReadKind kind = ReadKind::Unresolved;
  unsigned root = 0;
  uint64_t low = 0, high = 0;
};

//===----------------------------------------------------------------------===//
// X/Z writes of the activation
//===----------------------------------------------------------------------===//

struct Interval {
  unsigned root;
  uint64_t low, high;
  bool operator==(const Interval &other) const {
    return root == other.root && low == other.low && high == other.high;
  }
};
using Intervals = SmallVector<Interval, 0>;

/// Sorted, coalesced union of two interval lists.
Intervals unite(ArrayRef<Interval> lhs, ArrayRef<Interval> rhs) {
  Intervals merged;
  merged.reserve(lhs.size() + rhs.size());
  std::merge(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
             std::back_inserter(merged), [](const auto &a, const auto &b) {
               return std::tie(a.root, a.low) < std::tie(b.root, b.low);
             });
  Intervals result;
  for (const Interval &interval : merged) {
    if (!result.empty() && result.back().root == interval.root &&
        interval.low <= result.back().high) {
      result.back().high = std::max(result.back().high, interval.high);
      continue;
    }
    result.push_back(interval);
  }
  return result;
}

/// Bits of design storage that an X/Z write of the activation may have
/// written on some path to a program point. Unchanged states share their
/// interval list, so the per-point lattices of a large activation stay small.
/// Private allocations are numbered after every storage root, with this bit
/// set.
constexpr unsigned slotRootBit = 1u << 31;
bool isSlotRoot(unsigned root) { return root & slotRootBit; }

/// Remove [low, high) of `root` from `list`.
void subtract(std::shared_ptr<const Intervals> &list, unsigned root,
              uint64_t low, uint64_t high) {
  if (!list || llvm::none_of(*list, [&](const Interval &interval) {
        return interval.root == root && interval.low < high &&
               low < interval.high;
      }))
    return;
  Intervals next;
  next.reserve(list->size() + 1);
  for (const Interval &interval : *list) {
    if (interval.root != root || interval.high <= low ||
        high <= interval.low) {
      next.push_back(interval);
      continue;
    }
    if (interval.low < low)
      next.push_back({root, interval.low, low});
    if (high < interval.high)
      next.push_back({root, high, interval.high});
  }
  list = std::make_shared<const Intervals>(std::move(next));
}

/// The first interval of `root` in `list` ending after `low`.
const Interval *findInterval(const std::shared_ptr<const Intervals> &list,
                             unsigned root, uint64_t low) {
  if (!list)
    return nullptr;
  auto it = llvm::partition_point(*list, [&](const Interval &interval) {
    return std::tie(interval.root, interval.high) <= std::tie(root, low);
  });
  return it != list->end() && it->root == root ? &*it : nullptr;
}

/// Intersect `into` with `from`.
ChangeResult intersectInto(std::shared_ptr<const Intervals> &into,
                           const std::shared_ptr<const Intervals> &from) {
  if (!into || into->empty() || from == into)
    return ChangeResult::NoChange;
  if (!from || from->empty()) {
    into.reset();
    return ChangeResult::Change;
  }
  Intervals next;
  auto lhs = into->begin(), rhs = from->begin();
  while (lhs != into->end() && rhs != from->end()) {
    if (std::tie(lhs->root, lhs->high) <= std::tie(rhs->root, rhs->low)) {
      ++lhs;
      continue;
    }
    if (std::tie(rhs->root, rhs->high) <= std::tie(lhs->root, lhs->low)) {
      ++rhs;
      continue;
    }
    // Same root, overlapping.
    next.push_back({lhs->root, std::max(lhs->low, rhs->low),
                    std::min(lhs->high, rhs->high)});
    if (lhs->high < rhs->high)
      ++lhs;
    else
      ++rhs;
  }
  if (next == *into)
    return ChangeResult::NoChange;
  into = std::make_shared<const Intervals>(std::move(next));
  return ChangeResult::Change;
}

/// Unite `from` into `into`; with `dropStorage`, keep only slot intervals.
ChangeResult uniteInto(std::shared_ptr<const Intervals> &into,
                       const std::shared_ptr<const Intervals> &from,
                       bool dropStorage) {
  if (!from || from->empty() || from == into)
    return ChangeResult::NoChange;
  bool empty = !into || into->empty();
  if (empty && !dropStorage) {
    into = from;
    return ChangeResult::Change;
  }
  Intervals next = empty ? *from : unite(*into, *from);
  if (dropStorage)
    llvm::erase_if(next, [](const Interval &interval) {
      return !isSlotRoot(interval.root);
    });
  if (empty ? next.empty() : next == *into)
    return ChangeResult::NoChange;
  into = std::make_shared<const Intervals>(std::move(next));
  return ChangeResult::Change;
}

class WrittenLattice final : public dataflow::AbstractDenseLattice {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(WrittenLattice)
  using AbstractDenseLattice::AbstractDenseLattice;

  ChangeResult join(const AbstractDenseLattice &rhs) override {
    const auto &other = static_cast<const WrittenLattice &>(rhs);
    if (!other.reachable)
      return ChangeResult::NoChange;
    if (!reachable) {
      assign(other);
      return ChangeResult::Change;
    }
    ChangeResult result = ChangeResult::NoChange;
    if (other.allStorage && !allStorage)
      result = setAll();
    result |= uniteInto(intervals, other.intervals, allStorage);
    result |= uniteInto(entry, other.entry, /*dropStorage=*/false);
    result |= intersectInto(overwritten, other.overwritten);
    return result;
  }

  ChangeResult setReachable() {
    if (reachable)
      return ChangeResult::NoChange;
    reachable = true;
    return ChangeResult::Change;
  }
  /// Every storage root and net may hold X/Z; private slots keep their facts,
  /// which nothing but their own references reaches.
  ChangeResult setAll() {
    if (allStorage)
      return ChangeResult::NoChange;
    allStorage = reachable = true;
    keepSlots();
    return ChangeResult::Change;
  }
  /// Forget storage writes, which a segment's entry check observes. Either
  /// copy of the previous segment may have filled the private slots: those
  /// in `entry` are known only where the entry check tests them, and those
  /// in `unchecked` may hold X/Z.
  void resetStorage(const std::shared_ptr<const Intervals> &entrySlots,
                    const std::shared_ptr<const Intervals> &unchecked) {
    allStorage = false;
    reachable = true;
    keepSlots();
    entry = entrySlots;
    overwritten.reset();
    (void)uniteInto(intervals, unchecked, /*dropStorage=*/false);
  }
  /// Record that every path stored to [low, high) of storage root `root`.
  void overwrite(unsigned root, uint64_t low, uint64_t high) {
    if (low >= high || isSlotRoot(root) || overwrites(root, low, high))
      return;
    Intervals one{Interval{root, low, high}};
    overwritten = std::make_shared<const Intervals>(
        overwritten ? unite(*overwritten, one) : std::move(one));
  }
  /// Add [low, high) of `root`.
  void add(unsigned root, uint64_t low, uint64_t high) {
    if (low >= high || covers(root, low, high))
      return;
    Intervals one{Interval{root, low, high}};
    intervals = std::make_shared<const Intervals>(
        intervals ? unite(*intervals, one) : std::move(one));
  }
  /// Remove [low, high) of `root`: a known store overwrote it.
  void remove(unsigned root, uint64_t low, uint64_t high) {
    if (low >= high)
      return;
    subtract(entry, root, low, high);
    if (!allStorage || isSlotRoot(root))
      subtract(intervals, root, low, high);
  }
  void assign(const WrittenLattice &other) {
    reachable = other.reachable;
    allStorage = other.allStorage;
    intervals = other.intervals;
    entry = other.entry;
    overwritten = other.overwritten;
  }

  bool isReachable() const { return reachable; }
  bool covers(unsigned root, uint64_t low, uint64_t high) const {
    if (allStorage && !isSlotRoot(root))
      return true;
    const Interval *found = findInterval(intervals, root, low);
    return found && found->low <= low && high <= found->high;
  }
  bool mayWrite(unsigned root, uint64_t low, uint64_t high) const {
    if (allStorage && !isSlotRoot(root))
      return true;
    const Interval *found = findInterval(intervals, root, low);
    return found && found->low < high;
  }
  bool isAllStorage() const { return allStorage; }
  /// X/Z-written intervals, and storage bits every path stored since the
  /// segment's entry.
  ArrayRef<Interval> getWritten() const {
    return intervals ? ArrayRef<Interval>(*intervals) : ArrayRef<Interval>();
  }
  ArrayRef<Interval> getOverwritten() const {
    return overwritten ? ArrayRef<Interval>(*overwritten)
                       : ArrayRef<Interval>();
  }
  /// Whether every path since the segment's entry stored to storage bits
  /// [low, high).
  bool overwrites(unsigned root, uint64_t low, uint64_t high) const {
    const Interval *found = findInterval(overwritten, root, low);
    return found && found->low <= low && high <= found->high;
  }
  /// Whether slot bits [low, high) may still hold what the segment found at
  /// its entry.
  bool fromEntry(unsigned root, uint64_t low, uint64_t high) const {
    const Interval *found = findInterval(entry, root, low);
    return found && found->low < high;
  }
  void print(raw_ostream &os) const override {
    os << (allStorage ? "all storage, " : "")
       << (intervals ? intervals->size() : 0) << " intervals, "
       << (entry ? entry->size() : 0) << " entry slot intervals";
  }

private:
  void keepSlots() {
    if (!intervals || intervals->empty())
      return;
    Intervals slots;
    for (const Interval &interval : *intervals)
      if (isSlotRoot(interval.root))
        slots.push_back(interval);
    if (slots.size() != intervals->size())
      intervals = std::make_shared<const Intervals>(std::move(slots));
  }

  bool reachable = false;
  bool allStorage = false;
  std::shared_ptr<const Intervals> intervals;
  /// Slot bits no store of the current segment has overwritten.
  std::shared_ptr<const Intervals> entry;
  /// Storage bits every path of the current segment stored to.
  std::shared_ptr<const Intervals> overwritten;
};

//===----------------------------------------------------------------------===//
// Shared inputs
//===----------------------------------------------------------------------===//

struct Inputs {
  Inputs(const HandleDataflowResult &handles,
         const LocalStorageAnalysis &locals,
         const LocalUnknownAnalysis::Options &options)
      : handles(handles.facts), certificates(handles.certificates),
        locals(locals), segmentation(options.segmentation),
        callees(options.callees), summary(options.summary),
        staysUnknown(options.staysUnknown) {}
  const HandleFacts &handles;
  const DenseMap<Value, HandleCertificate> &certificates;
  const LocalStorageAnalysis &locals;
  const LocalUnknownAnalysis::Segmentation *segmentation;
  const LocalUnknownAnalysis::CalleeSummaries *callees;
  bool summary;
  llvm::function_ref<bool(uint64_t, uint64_t, uint64_t)> staysUnknown;

  /// Whether a write through `reference` may reach bits that a gated read
  /// loads as the constant X of `simulation.steady_unknown`.
  bool mayWriteSteady(Value reference) const {
    auto fact = handles.find(reference);
    if (!staysUnknown || fact == handles.end() || !fact->second.descriptor ||
        fact->second.resource != schedule::ComputeResourceKind::Storage)
      return false;
    // A dynamic selection's fact is its hull.
    uint64_t width =
        fact->second.width ? fact->second.width : fact->second.rootWidth;
    return width && staysUnknown(*fact->second.descriptor, fact->second.low,
                                 fact->second.low + width);
  }

  /// Whether `value` reaches `user` from another segment. Arguments of a
  /// block a segment starts at come from the previous segment.
  bool crossesSegment(Value value, Operation *user) const {
    if (!segmentation || segmentation->count < 2)
      return false;
    auto segmentOf = [&](Operation *op) {
      return segmentation->segments.lookup(op);
    };
    unsigned use = segmentOf(user);
    if (auto argument = dyn_cast<BlockArgument>(value)) {
      Block *block = argument.getOwner();
      if (!isa<sim::SimFuncOp>(block->getParentOp()))
        return segmentOf(block->getParentOp()) != use;
      Operation *first = &block->front();
      return segmentation->begins.contains(first) || segmentOf(first) != use;
    }
    return segmentOf(value.getDefiningOp()) != use;
  }

  /// The summary of a call's callee, or null when it is unknown.
  const LocalUnknownAnalysis::CalleeSummary *lookupCallee(Operation *op) {
    auto call = dyn_cast<sim::SimCallOp>(op);
    if (!call || !callees)
      return nullptr;
    auto found = callees->find(call.getCallee());
    return found == callees->end() ? nullptr : &found->second;
  }
  /// Canonical reads and their static classification; `Gated` here means a
  /// candidate the entry check may cover.
  DenseMap<Operation *, ReadSite> sites;
  DenseMap<std::pair<unsigned, uint64_t>, unsigned> roots;
  /// Dense indices of the function's private allocations.
  DenseMap<Operation *, uint64_t> slots;
  /// Every bit of the private allocations a segment's entry check can test,
  /// and of the others, at a segment's entry.
  std::shared_ptr<const Intervals> entrySlots, uncheckedSlots;
  DenseSet<unsigned> unchecked;

  /// The selectors each gated dynamic selection is valid for once they are
  /// known (HandleCertificate::knownSelectors).
  DenseMap<Operation *, SmallVector<Value, 2>> selectors;
  /// Gated reads of steady pattern bits: four-state in the copy as well.
  DenseSet<Operation *> steady;

  /// Every bit of a private allocation.
  Interval wholeSlot(sim::SimRefAllocOp allocation) {
    auto slot = slots.try_emplace(allocation, slots.size()).first->second;
    std::optional<uint64_t> span = sim::getProvenanceSpan(
        cast<sim::RefType>(allocation.getType()).getElementType());
    return {static_cast<unsigned>(slot) | slotRootBit, 0,
            span ? *span : ~uint64_t{0} >> 1};
  }
  /// The bits of a private allocation an access through `reference` reaches.
  std::optional<Interval> resolveSlot(Value reference) {
    sim::SimRefAllocOp allocation = locals.lookup(reference);
    if (!allocation)
      return std::nullopt;
    Interval whole = wholeSlot(allocation);
    auto fact = handles.find(reference);
    if (fact == handles.end() ||
        fact->second.resource != schedule::ComputeResourceKind::Local ||
        fact->second.dynamic)
      return whole;
    uint64_t width =
        fact->second.width ? fact->second.width : fact->second.rootWidth;
    if (!width || fact->second.low > whole.high ||
        width > whole.high - fact->second.low)
      return whole;
    return Interval{whole.root, fact->second.low, fact->second.low + width};
  }

  unsigned rootIndex(schedule::ComputeResourceKind resource,
                     uint64_t descriptor) {
    return roots
        .try_emplace({static_cast<unsigned>(resource), descriptor},
                     roots.size())
        .first->second;
  }
};

/// The written root bits of a reference: false when the reference may
/// address something other than one fixed design root.
bool resolveWrite(Inputs &inputs, Value reference, Interval &interval) {
  auto fact = inputs.handles.find(reference);
  if (fact == inputs.handles.end() || !fact->second.descriptor ||
      fact->second.resource == schedule::ComputeResourceKind::Unknown)
    return false;
  uint64_t width =
      fact->second.width ? fact->second.width : fact->second.rootWidth;
  interval = {inputs.rootIndex(fact->second.resource, *fact->second.descriptor),
              fact->second.low, fact->second.low + width};
  return width != 0;
}

/// Whether `op` may write design storage or a net in a way the dense
/// transfer does not model: unknown effects, foreign code and overrides.
bool mayWriteUnmodeled(Operation *op) {
  if (isa<sim::SimNBAEnqueueOp, sim::SimRefAllocOp>(op))
    return false;
  auto effects = dyn_cast<MemoryEffectOpInterface>(op);
  if (!effects)
    return !isMemoryEffectFree(op);
  SmallVector<MemoryEffects::EffectInstance> instances;
  effects.getEffects(instances);
  return llvm::any_of(instances, [](const MemoryEffects::EffectInstance &e) {
    if (!isa<MemoryEffects::Write>(e.getEffect()))
      return false;
    SideEffects::Resource *resource = e.getResource();
    return isa<sim::StorageResource, sim::NetResource, sim::ExternalResource,
               SideEffects::DefaultResource>(resource);
  });
}

/// The X/Z writes `op` observes, given those before it: the entry check of a
/// segment, which runs before its first operation, observes every earlier
/// storage write (WrittenLattice::resetStorage).
WrittenLattice observedWrites(const WrittenLattice &before, Operation *op,
                              const Inputs &inputs) {
  WrittenLattice observed(before.getAnchor());
  observed.assign(before);
  if (inputs.segmentation && inputs.segmentation->begins.contains(op))
    observed.resetStorage(inputs.entrySlots, inputs.uncheckedSlots);
  return observed;
}

//===----------------------------------------------------------------------===//
// Analyses
//===----------------------------------------------------------------------===//

template <typename LatticeT, bool Gated>
class DomainAnalysis final
    : public dataflow::SparseForwardDataFlowAnalysis<LatticeT> {
  using Base = dataflow::SparseForwardDataFlowAnalysis<LatticeT>;

public:
  DomainAnalysis(DataFlowSolver &solver, Inputs &inputs)
      : Base(solver), inputs(inputs) {}

  LogicalResult visitOperation(Operation *op,
                               ArrayRef<const LatticeT *> operands,
                               ArrayRef<LatticeT *> results) override {
    for (auto [operand, lattice] : llvm::zip(op->getOperands(), operands))
      if (hasUnknownPlane(operand.getType()) &&
          lattice->getValue().get() == StateDomain::Bottom)
        return success();
    StateDomain domain;
    if (isa<sim::SimRefLoadOp, sim::SimNetReadOp>(op)) {
      domain = readDomain(op);
    } else {
      auto operandFact = [&](Value value) {
        for (auto [operand, lattice] : llvm::zip(op->getOperands(), operands))
          if (operand == value)
            return StateDomainFact{lattice->getValue().get(),
                                   StateDomainReason::CFGJoin};
        return StateDomainFact{StateDomain::MayFourState,
                               StateDomainReason::UnsupportedProducer};
      };
      domain = transferStateDomain(
                   op, operandFact, [](Operation *) { return false; },
                   [](Operation *selection) {
                     return isConstantInRangeSelection(selection);
                   })
                   .domain;
    }
    if (domain == StateDomain::Bottom)
      return success();
    for (auto [result, lattice] : llvm::zip(op->getResults(), results))
      this->propagateIfChanged(
          lattice, lattice->join(DomainValue(hasUnknownPlane(result.getType())
                                                 ? domain
                                                 : StateDomain::TwoState)));
    return success();
  }

  void visitExternalCall(CallOpInterface call,
                         ArrayRef<const LatticeT *> arguments,
                         ArrayRef<LatticeT *> results) override {
    // A summarized callee returns known results for known arguments.
    const auto *callee = inputs.lookupCallee(call);
    bool known = callee && callee->knownResults;
    for (auto [operand, lattice] :
         llvm::zip(call.getArgOperands(), arguments)) {
      if (!hasUnknownPlane(operand.getType()))
        continue;
      StateDomain domain = lattice->getValue().get();
      if (domain == StateDomain::Bottom)
        return;
      known &= domain == StateDomain::TwoState;
    }
    for (LatticeT *lattice : results) {
      Value result = cast<Value>(lattice->getAnchor());
      this->propagateIfChanged(
          lattice, lattice->join(DomainValue(
                       known || !hasUnknownPlane(result.getType())
                           ? StateDomain::TwoState
                           : StateDomain::MayFourState)));
    }
  }

  void setToEntryState(LatticeT *lattice) override {
    auto value = dyn_cast<Value>(lattice->getAnchor());
    // A summary assumes known arguments of the function itself.
    auto argument = dyn_cast_or_null<BlockArgument>(value);
    bool knownArgument =
        inputs.summary && argument && argument.getOwner()->isEntryBlock() &&
        isa<sim::SimFuncOp>(argument.getOwner()->getParentOp());
    this->propagateIfChanged(
        lattice, lattice->join(DomainValue(value &&
                                                   hasUnknownPlane(
                                                       value.getType()) &&
                                                   !knownArgument
                                               ? StateDomain::MayFourState
                                               : StateDomain::TwoState)));
  }

private:
  StateDomain readDomain(Operation *op) {
    if (!hasUnknownPlane(op->getResult(0).getType()))
      return StateDomain::TwoState;
    if (auto site = inputs.sites.find(op);
        site != inputs.sites.end() &&
        (site->second.kind == ReadKind::TwoState ||
         (Gated && site->second.kind == ReadKind::Checked)))
      return StateDomain::TwoState;
    if constexpr (!Gated) {
      return StateDomain::MayFourState;
    } else {
      // A gated read observes entry state; a private read, whatever the
      // activation stored. Either holds X/Z only where a write may have
      // put it.
      auto site = inputs.sites.find(op);
      if (site == inputs.sites.end() ||
          (site->second.kind != ReadKind::Gated &&
           site->second.kind != ReadKind::Local) ||
          (inputs.summary && site->second.kind == ReadKind::Gated) ||
          inputs.steady.contains(op))
        return StateDomain::MayFourState;
      // A dynamic selection reads its gated hull only while it is valid:
      // whenever its selectors are known (IEEE 1800-2023 7.4.6, 11.5.1).
      if (auto selectors = inputs.selectors.find(op);
          selectors != inputs.selectors.end())
        for (Value selector : selectors->second) {
          if (!hasUnknownPlane(selector.getType()))
            continue;
          StateDomain domain =
              this->getLatticeElementFor(this->getProgramPointAfter(op),
                                         selector)
                  ->getValue()
                  .get();
          if (domain != StateDomain::TwoState)
            return domain;
        }
      const auto *before = this->template getOrCreateFor<WrittenLattice>(
          this->getProgramPointAfter(op), this->getProgramPointBefore(op));
      return observedWrites(*before, op, inputs)
                     .mayWrite(site->second.root, site->second.low,
                               site->second.high)
                 ? StateDomain::MayFourState
                 : StateDomain::TwoState;
    }
  }

  Inputs &inputs;
};

/// The bits X/Z writes of the activation may have stored so far.
class WrittenAnalysis final
    : public dataflow::DenseForwardDataFlowAnalysis<WrittenLattice> {
public:
  WrittenAnalysis(DataFlowSolver &solver, Inputs &inputs)
      : DenseForwardDataFlowAnalysis(solver), inputs(inputs) {}

  LogicalResult visitOperation(Operation *op, const WrittenLattice &before,
                               WrittenLattice *after) override {
    if (!before.isReachable())
      return success();
    WrittenLattice next = observedWrites(before, op, inputs);
    // A store of a possibly X/Z value adds its bits; a known store to an
    // exact location removes them (a value not yet solved counts as known
    // until it is).
    auto write = [&](Value reference, Value value) {
      Interval interval;
      bool exact;
      if (std::optional<Interval> slot = inputs.resolveSlot(reference)) {
        interval = *slot;
        auto fact = inputs.handles.find(reference);
        exact = fact != inputs.handles.end() && !fact->second.dynamic &&
                fact->second.resource == schedule::ComputeResourceKind::Local;
      } else if (resolveWrite(inputs, reference, interval)) {
        exact = !inputs.handles.lookup(reference).dynamic;
      } else {
        (void)next.setAll();
        return;
      }
      // A write of steady-unknown bits may make them known, so a gated read
      // after it cannot load them as the pattern's constant X.
      StateDomain domain = StateDomain::MayFourState;
      if (value) {
        if (!hasUnknownPlane(value.getType()))
          return;
        if (!inputs.mayWriteSteady(reference))
          domain = getOrCreateFor<GatedLattice>(getProgramPointAfter(op), value)
                       ->getValue()
                       .get();
      }
      if (domain == StateDomain::MayFourState)
        next.add(interval.root, interval.low, interval.high);
      else if (exact)
        next.remove(interval.root, interval.low, interval.high);
      if (exact)
        next.overwrite(interval.root, interval.low, interval.high);
    };
    // Each execution of an allocation starts a lifetime holding its
    // initializer.
    if (auto allocation = dyn_cast<sim::SimRefAllocOp>(op))
      if (inputs.locals.lookup(allocation.getResult()))
        write(allocation.getResult(), allocation.getInitialValue());
    if (auto store = dyn_cast<sim::SimRefStoreOp>(op))
      write(store.getReference(), store.getValue());
    else if (auto copy = dyn_cast<sim::SimRefCopyOp>(op))
      write(copy.getDestination(), Value());
    else if (auto netWrite = dyn_cast<sim::SimNetWriteOp>(op))
      write(netWrite.getNet(), netWrite.getValue());
    else if (mayWriteUnmodeled(op))
      (void)next.setAll();
    propagateIfChanged(after, after->join(next));
    return success();
  }

  void visitCallControlFlowTransfer(CallOpInterface call,
                                    dataflow::CallControlFlowAction,
                                    const WrittenLattice &before,
                                    WrittenLattice *after) override {
    // A callee that may write design storage may write X/Z anywhere.
    if (!before.isReachable())
      return;
    WrittenLattice next(after->getAnchor());
    next.assign(before);
    const auto *callee = inputs.lookupCallee(call);
    if (!callee || callee->writes)
      (void)next.setAll();
    propagateIfChanged(after, after->join(next));
  }

private:
  void setToEntryState(WrittenLattice *lattice) override {
    propagateIfChanged(lattice, lattice->setReachable());
  }

  Inputs &inputs;
};

/// Approximate unknown-plane work that depends on `value` within its
/// function: the operations its four-state uses reach through results and
/// branch arguments, up to a budget. Stores end a chain.
unsigned downstreamWork(Value value, unsigned budget = 256) {
  unsigned work = 0, visited = 0;
  SmallVector<Value> pending{value};
  llvm::SmallPtrSet<Operation *, 16> seen;
  while (!pending.empty() && visited < budget) {
    Value next = pending.pop_back_val();
    for (Operation *user : next.getUsers()) {
      if (!seen.insert(user).second || ++visited > budget)
        continue;
      if (isa<sim::SimRefStoreOp, sim::SimNBAEnqueueOp, CallOpInterface>(
              user))
        continue;
      work += getUnknownPlaneWork(user);
      if (auto branch = dyn_cast<BranchOpInterface>(user)) {
        for (unsigned index = 0; index != user->getNumSuccessors(); ++index) {
          SuccessorOperands operands = branch.getSuccessorOperands(index);
          for (unsigned position = 0; position != operands.size(); ++position)
            if (operands[position] == next)
              pending.push_back(
                  user->getSuccessor(index)->getArgument(position));
        }
        continue;
      }
      for (Value result : user->getResults())
        if (hasUnknownPlane(result.getType()))
          pending.push_back(result);
    }
  }
  return work;
}

bool isExecutable(DataFlowSolver &solver, Block *block) {
  auto *state = solver.lookupState<dataflow::Executable>(
      solver.getProgramPointBefore(block));
  return state && state->isLive();
}

} // namespace

StringRef LocalUnknownAnalysis::stringify(ReadKind kind) {
  switch (kind) {
  case ReadKind::Gated:
    return "gated";
  case ReadKind::TwoState:
    return "two-state";
  case ReadKind::Dead:
    return "dead";
  case ReadKind::Local:
    return "local";
  case ReadKind::Unresolved:
    return "unresolved";
  case ReadKind::Dynamic:
    return "dynamic";
  case ReadKind::Written:
    return "written";
  case ReadKind::Checked:
    return "checked";
  }
  llvm_unreachable("unknown read kind");
}

LocalUnknownAnalysis::ReadKind
LocalUnknownAnalysis::getKind(Operation *read) const {
  auto found = kinds.find(read);
  return found == kinds.end() ? ReadKind::Unresolved : found->second;
}

LocalUnknownAnalysis::LocalUnknownAnalysis(sim::SimFuncOp function,
                                           const HandleDataflowResult &handles,
                                           const Options &options) {
  const Segmentation *segmentation = options.segmentation;
  if (function.isExternal())
    return;
  // The solvers follow region-based control flow through its interface.
  bool nested = false;
  SmallVector<sim::SimRefStoreOp> stores;
  function.walk([&](Operation *op) {
    if (op != function.getOperation() && op->getNumRegions() &&
        !isa<RegionBranchOpInterface>(op))
      nested = true;
    if (isa<sim::SimRefLoadOp, sim::SimNetReadOp>(op))
      reads.push_back(op);
    if (auto store = dyn_cast<sim::SimRefStoreOp>(op))
      stores.push_back(store);
  });
  if (nested) {
    failure = "nested region";
    for (Operation *read : reads)
      kinds[read] = ReadKind::Unresolved;
    return;
  }

  LocalStorageAnalysis locals(function);
  Inputs inputs(handles, locals, options);
  DenseMap<unsigned, uint64_t> descriptors;
  for (Operation *read : reads) {
    Value result = read->getResult(0);
    ReadSite site;
    Value reference = read->getOperand(0);
    auto fact = handles.facts.find(reference);
    if (!hasUnknownPlane(result.getType()) ||
        (options.provenKnownRead && options.provenKnownRead(read)))
      site.kind = ReadKind::TwoState;
    else if (std::optional<Interval> slot = inputs.resolveSlot(reference)) {
      site.kind = ReadKind::Local;
      site.root = slot->root;
      site.low = slot->low;
      site.high = slot->high;
    } else if (!isa<sim::SimRefLoadOp>(read) || fact == handles.facts.end() ||
               !fact->second.descriptor ||
               fact->second.resource != schedule::ComputeResourceKind::Storage)
      site.kind = ReadKind::Unresolved;
    else {
      uint64_t width =
          fact->second.width ? fact->second.width : fact->second.rootWidth;
      // A dynamic selection is gated over its hull, and only where it is
      // valid for every known value of its selectors (IEEE 1800-2023 7.4.5,
      // 11.5.1).
      auto certificate = handles.certificates.find(reference);
      bool steady =
          options.staysUnknown &&
          options.staysUnknown(*fact->second.descriptor, fact->second.low,
                               fact->second.low + width);
      if (width == 0)
        site.kind = ReadKind::Unresolved;
      else if (fact->second.dynamic &&
               (certificate == handles.certificates.end() ||
                !certificate->second.inBoundsIfKnown || steady))
        site.kind = ReadKind::Dynamic;
      else {
        if (steady)
          inputs.steady.insert(read);
        site.kind = ReadKind::Gated;
        site.root = inputs.rootIndex(fact->second.resource,
                                     *fact->second.descriptor);
        site.low = fact->second.low;
        site.high = fact->second.low + width;
        descriptors[site.root] = *fact->second.descriptor;
        if (fact->second.dynamic && !certificate->second.knownSelectors.empty())
          inputs.selectors[read] = certificate->second.knownSelectors;
      }
    }
    inputs.sites[read] = site;
  }

  // A segment's entry check tests the narrow slots its known reads find as
  // the previous segment left them.
  DenseMap<unsigned, sim::SimRefAllocOp> slotAllocations;
  if (segmentation && segmentation->count > 1) {
    Intervals checkable, unchecked;
    function.walk([&](sim::SimRefAllocOp allocation) {
      if (!locals.lookup(allocation.getResult()))
        return;
      Interval whole = inputs.wholeSlot(allocation);
      slotAllocations[whole.root] = allocation;
      Type type = allocation.getType().getElementType();
      if (KnownVersioningAnalysis::isCastable(type) &&
          whole.high <= maximumCheckedSlotBits) {
        checkable.push_back(whole);
      } else {
        unchecked.push_back(whole);
        inputs.unchecked.insert(whole.root);
      }
    });
    auto byRoot = [](const Interval &a, const Interval &b) {
      return a.root < b.root;
    };
    llvm::sort(checkable, byRoot);
    llvm::sort(unchecked, byRoot);
    inputs.entrySlots = std::make_shared<const Intervals>(std::move(checkable));
    inputs.uncheckedSlots =
        std::make_shared<const Intervals>(std::move(unchecked));
  }

  unsigned count = segmentation ? segmentation->count : 1;
  auto segmentOf = [&](Operation *op) {
    return segmentation ? segmentation->segments.lookup(op) : 0u;
  };
  segments.resize(count);
  // Reads tested where they load: the copy continues in the original body at
  // the read when it holds X/Z. Only reads at the body's top level qualify,
  // since a branch cannot leave a structured region, and only where enough
  // four-state work depends on the read to pay for the test.
  constexpr unsigned minimumCheckedWork = 16;
  auto checkable = [&](Operation *read) {
    return !options.summary && options.checks &&
           read->getParentRegion() == &function.getBody() &&
           KnownVersioningAnalysis::isCastable(read->getResult(0).getType()) &&
           downstreamWork(read->getResult(0)) >= minimumCheckedWork;
  };
  for (Operation *read : reads) {
    ReadSite &site = inputs.sites[read];
    if ((site.kind == ReadKind::Dynamic || site.kind == ReadKind::Unresolved) &&
        checkable(read))
      site.kind = ReadKind::Checked;
  }

  DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
  solver.load<dataflow::DeadCodeAnalysis>();
  solver.load<dataflow::SparseConstantPropagation>();
  solver.load<DomainAnalysis<GatedLattice, true>>(inputs);
  solver.load<DomainAnalysis<BaselineLattice, false>>(inputs);
  solver.load<WrittenAnalysis>(inputs);
  if (failed(solver.initializeAndRun(function))) {
    failure = "domain analysis failed";
    return;
  }

  auto domainOf = [&](auto *lattice) {
    return lattice ? lattice->getValue().get() : StateDomain::Bottom;
  };
  auto knownInGatedCopy = [&](Value value) {
    return !hasUnknownPlane(value.getType()) ||
           domainOf(solver.lookupState<GatedLattice>(value)) ==
               StateDomain::TwoState;
  };
  // The writes `op` observes; without a solved state, assume private slots
  // hold entry contents and storage was neither written nor overwritten.
  auto observedBefore = [&](Operation *op) -> std::optional<WrittenLattice> {
    const auto *before =
        solver.lookupState<WrittenLattice>(solver.getProgramPointBefore(op));
    if (!before)
      return std::nullopt;
    return observedWrites(*before, op, inputs);
  };
  SmallVector<llvm::SetVector<uint64_t>> gated(count);
  for (Operation *read : reads) {
    ReadSite site = inputs.sites.lookup(read);
    ReadKind kind = site.kind;
    if (!isExecutable(solver, read->getBlock())) {
      kind = ReadKind::Dead;
    } else if (kind == ReadKind::Gated && inputs.steady.contains(read)) {
      // Its value is four-state anyway; it stays value-only unless an X/Z
      // write of the segment, or any write of its pattern bits, may reach
      // it.
      std::optional<WrittenLattice> observed = observedBefore(read);
      if (!observed || observed->mayWrite(site.root, site.low, site.high))
        kind = ReadKind::Written;
    } else if (kind == ReadKind::Gated && !knownInGatedCopy(read->getResult(0)))
      kind = llvm::all_of(inputs.selectors.lookup(read), knownInGatedCopy)
                 ? ReadKind::Written
                 : ReadKind::Dynamic;
    kinds[read] = kind;
    if (kind == ReadKind::Checked)
      ++segments[segmentOf(read)].checks;
    if (kind != ReadKind::Local && kind != ReadKind::Gated)
      continue;
    std::optional<WrittenLattice> observed = observedBefore(read);
    // A known private read whose bits may come from before its segment
    // needs the slot tested at the segment's entry.
    if (kind == ReadKind::Local) {
      if (segmentation && knownInGatedCopy(read->getResult(0)) &&
          !inputs.unchecked.contains(site.root) &&
          (!observed || observed->fromEntry(site.root, site.low, site.high)))
        if (sim::SimRefAllocOp allocation = slotAllocations.lookup(site.root))
          segments[segmentOf(read)].slotChecks.insert(allocation);
      continue;
    }
    unsigned segment = segmentOf(read);
    segments[segment].benefit += 1 + (site.high - site.low) / 64;
    // Bits every path of the segment stored before the read need no check.
    if (observed && observed->overwrites(site.root, site.low, site.high))
      continue;
    gated[segment].insert(descriptors.lookup(site.root));
  }

  // A known store writes only the value plane of a tested root whose bits no
  // X/Z write of the segment may have reached: they stay zero. A write with
  // an invalid index writes nothing (7.4.5), so a dynamic one qualifies too.
  // Steady-unknown bits hold X/Z at the check, so a store reaching them
  // keeps both planes.
  if (!options.summary)
    for (sim::SimRefStoreOp store : stores) {
      Value reference = store.getReference();
      auto fact = handles.facts.find(reference);
      if (!isExecutable(solver, store->getBlock()) ||
          !hasUnknownPlane(
              cast<sim::RefType>(reference.getType()).getElementType()) ||
          !knownInGatedCopy(store.getValue()) ||
          inputs.resolveSlot(reference) || inputs.mayWriteSteady(reference) ||
          fact == handles.facts.end() || !fact->second.descriptor ||
          fact->second.resource != schedule::ComputeResourceKind::Storage)
        continue;
      uint64_t width =
          fact->second.width ? fact->second.width : fact->second.rootWidth;
      unsigned root =
          inputs.rootIndex(fact->second.resource, *fact->second.descriptor);
      std::optional<WrittenLattice> observed = observedBefore(store);
      if (width == 0 || !observed ||
          observed->mayWrite(root, fact->second.low, fact->second.low + width))
        continue;
      knownStores.insert(store);
      Segment &segment = segments[segmentOf(store)];
      segment.benefit += 1 + width / 64;
      gated[segmentOf(store)].insert(*fact->second.descriptor);
    }

  // Each operation with a four-state operand or result that the copy makes
  // two-state stops computing its unknown plane.
  auto record = [&](Value value) {
    if (!hasUnknownPlane(value.getType()))
      return;
    if (domainOf(solver.lookupState<GatedLattice>(value)) ==
        StateDomain::TwoState)
      knownInCopy.insert(value);
    if (domainOf(solver.lookupState<BaselineLattice>(value)) ==
        StateDomain::TwoState)
      knownInOriginal.insert(value);
  };
  function.walk([&](Block *block) {
    for (BlockArgument argument : block->getArguments())
      record(argument);
  });
  function.walk([&](Operation *op) {
    for (Value result : op->getResults())
      record(result);
  });
  auto improved = [&](Value value) {
    return knownInCopy.contains(value) && !knownInOriginal.contains(value);
  };
  function.walk([&](Operation *op) {
    if (op == function.getOperation() || !op->getBlock() ||
        !isExecutable(solver, op->getBlock()))
      return;
    Segment &segment = segments[segmentOf(op)];
    if (!isa<sim::SimRefLoadOp>(op) &&
        (llvm::any_of(op->getResults(), improved) ||
         llvm::any_of(op->getOperands(), improved)))
      segment.benefit += getUnknownPlaneWork(op);
    // A value from another segment comes from whichever copy ran there:
    // the entry check tests each one only the copy proves known.
    for (Value operand : op->getOperands())
      if (improved(operand) && inputs.crossesSegment(operand, op)) {
        segment.liveIns.insert(operand);
        segment.untestable |= !sim::hasFixedBitValue(operand.getType());
      }
  });

  for (auto [segment, roots] : llvm::enumerate(gated)) {
    SmallVector<uint64_t, 0> &gate = segments[segment].gate;
    gate.assign(roots.begin(), roots.end());
    llvm::sort(gate);
  }
}

LocalUnknownAnalysis::CalleeSummaries
LocalUnknownAnalysis::summarize(sim::SimDesignOp design,
                                const HandleDataflowAnalysis &handles) {
  CalleeSummaries summaries;
  if (design.getBody().empty())
    return summaries;
  // Only called functions need summaries. A large callee keeps the
  // conservative default rather than a whole-body solve per caller design.
  constexpr unsigned maximumSummarizedOperations = 4096;
  llvm::StringSet<> called;
  design.walk([&](sim::SimCallOp call) { called.insert(call.getCallee()); });
  llvm::StringSet<> functions;
  for (auto function : design.getBody().front().getOps<sim::SimFuncOp>()) {
    if (!called.contains(function.getSymName()))
      continue;
    unsigned size = 0;
    function.walk([&](Operation *) { ++size; });
    if (size <= maximumSummarizedOperations)
      functions.insert(function.getSymName());
  }
  auto summarizeOne = [&](sim::SimFuncOp function) {
    CalleeSummary summary;
    if (function.isExternal())
      return summary;
    LocalUnknownAnalysis analysis(function, handles.analyze(function),
                                  Options{nullptr, &summaries, true});
    if (!analysis.getFailure().empty())
      return summary;
    LocalStorageAnalysis locals(function);
    summary.writes = function
                         .walk([&](Operation *op) {
                           bool canonical =
                               isa<sim::SimRefStoreOp, sim::SimRefCopyOp>(op) &&
                               !locals.isLocalAccess(op) &&
                               !(isa<sim::SimRefCopyOp>(op) &&
                                 locals.lookup(op->getOperand(1)));
                           bool call = false;
                           if (auto invoke = dyn_cast<sim::SimCallOp>(op)) {
                             auto callee = summaries.find(invoke.getCallee());
                             call = callee == summaries.end() ||
                                    callee->second.writes;
                           }
                           return canonical || call ||
                                          isa<sim::SimNetWriteOp>(op) ||
                                          (!isa<sim::SimRefStoreOp,
                                                sim::SimRefCopyOp,
                                                sim::SimCallOp>(op) &&
                                           mayWriteUnmodeled(op))
                                      ? WalkResult::interrupt()
                                      : WalkResult::advance();
                         })
                         .wasInterrupted();
    summary.knownResults = !function
                                .walk([&](sim::SimReturnOp returnOp) {
                                  for (Value operand : returnOp.getOperands())
                                    if (hasUnknownPlane(operand.getType()) &&
                                        !analysis.isKnownInCopy(operand))
                                      return WalkResult::interrupt();
                                  return WalkResult::advance();
                                })
                                .wasInterrupted();
    return summary;
  };
  // Callees first: the call graph's strongly connected components come in
  // post-order. A function without a summary keeps the conservative default,
  // so a recursive component stays unsummarized.
  CallGraph graph(design);
  for (auto scc = llvm::scc_begin(static_cast<const CallGraph *>(&graph));
       !scc.isAtEnd(); ++scc) {
    if (scc.hasCycle())
      continue;
    for (CallGraphNode *node : *scc) {
      if (node->isExternal())
        continue;
      auto function =
          dyn_cast<sim::SimFuncOp>(node->getCallableRegion()->getParentOp());
      if (function && functions.contains(function.getSymName()))
        summaries[function.getSymName()] = summarizeOne(function);
    }
  }
  return summaries;
}

} // namespace obelisk::analysis
