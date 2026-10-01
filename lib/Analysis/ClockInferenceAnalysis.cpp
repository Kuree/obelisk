//===- ClockInferenceAnalysis.cpp - Physical clock data flow --------------===//

#include "obelisk/Analysis/ClockInferenceAnalysis.h"
#include "obelisk/Analysis/ClassDispatchAnalysis.h"
#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/DenseAnalysis.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/SetVector.h"

using namespace mlir;

namespace obelisk::analysis {

bool ClockFact::operator==(const ClockFact &other) const {
  return kind == other.kind && source == other.source &&
         halfPeriod == other.halfPeriod && constant == other.constant;
}

ClockFact ClockFact::join(ClockFact lhs, ClockFact rhs) {
  if (lhs.kind == Kind::Bottom)
    return rhs;
  if (rhs.kind == Kind::Bottom || lhs == rhs)
    return lhs;
  if (lhs.hasTickBound() && rhs.hasTickBound() && lhs.source == rhs.source &&
      lhs.halfPeriod == rhs.halfPeriod)
    return {Kind::TickDriven, lhs.source, lhs.halfPeriod};
  return {Kind::Unknown};
}

namespace {

/// Product of per-output {0, 1, many} write counts. Bottom represents an
/// unreachable point; joins take the maximum, writes saturate at many, and
/// suspension edges start a new activation with zero counts. Both increment
/// and reset are monotone, so the solver terminates even for zero-time loops.
class WriteCountLattice : public dataflow::AbstractDenseLattice {
public:
  using AbstractDenseLattice::AbstractDenseLattice;
  bool reachable = false;
  SmallVector<uint8_t> counts;

  ChangeResult joinCounts(ArrayRef<uint8_t> rhs) {
    if (!reachable) {
      reachable = true;
      counts.assign(rhs.begin(), rhs.end());
      return ChangeResult::Change;
    }
    bool changed = false;
    for (auto [count, incoming] : llvm::zip(counts, rhs))
      if (incoming > count) {
        count = incoming;
        changed = true;
      }
    return changed ? ChangeResult::Change : ChangeResult::NoChange;
  }
  ChangeResult join(const AbstractDenseLattice &rhs) override {
    const auto &other = static_cast<const WriteCountLattice &>(rhs);
    return other.reachable ? joinCounts(other.counts) : ChangeResult::NoChange;
  }
  void print(raw_ostream &os) const override {
    if (!reachable)
      os << "unreachable";
    else
      llvm::interleaveComma(counts, os);
  }
};

class ActivationWriteAnalysis
    : public dataflow::DenseForwardDataFlowAnalysis<WriteCountLattice> {
public:
  ActivationWriteAnalysis(
      DataFlowSolver &solver,
      const DenseMap<Operation *, SmallVector<unsigned>> &writes,
      unsigned count)
      : DenseForwardDataFlowAnalysis(solver), writes(writes), count(count) {}

  LogicalResult visitOperation(Operation *op, const WriteCountLattice &before,
                               WriteCountLattice *after) override {
    if (!before.reachable)
      return success();
    SmallVector<uint8_t> next = before.counts;
    if (sim::isSuspensionOp(op))
      std::fill(next.begin(), next.end(), 0);
    else if (auto write = writes.find(op); write != writes.end())
      for (unsigned index : write->second)
        next[index] = std::min<unsigned>(2, next[index] + 1);
    propagateIfChanged(after, after->joinCounts(next));
    return success();
  }

private:
  void setToEntryState(WriteCountLattice *lattice) override {
    SmallVector<uint8_t> zero(count, 0);
    propagateIfChanged(lattice, lattice->joinCounts(zero));
  }
  const DenseMap<Operation *, SmallVector<unsigned>> &writes;
  unsigned count;
};

SmallVector<bool> proveSingleWrites(
    sim::SimFuncOp function,
    const DenseMap<Operation *, std::pair<uint64_t, uint64_t>> &ranges,
    ArrayRef<ClockBit> candidates) {
  DenseMap<Operation *, SmallVector<unsigned>> writes;
  for (auto [op, range] : ranges)
    for (auto [index, bit] : llvm::enumerate(candidates))
      if (bit.second >= range.first && bit.second - range.first < range.second)
        writes[op].push_back(index);

  DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
  solver.load<dataflow::DeadCodeAnalysis>();
  solver.load<dataflow::SparseConstantPropagation>();
  solver.load<ActivationWriteAnalysis>(writes, candidates.size());
  SmallVector<bool> single(candidates.size(), true);
  if (failed(solver.initializeAndRun(function)))
    return SmallVector<bool>(candidates.size(), false);
  for (const auto &[op, indices] : writes) {
    auto *state =
        solver.lookupState<WriteCountLattice>(solver.getProgramPointAfter(op));
    // Unreachable writes impose no bound. A missing analysis state cannot
    // establish a certificate, whereas any reachable count of many rejects it.
    for (unsigned index : indices)
      if (!state || (state->reachable && state->counts[index] > 1))
        single[index] = false;
  }
  return single;
}

struct Writer {
  Operation *owner;
  uint64_t low, width;
};
struct Transfer {
  ClockBit source, target;
  Operation *owner;
  bool cadence;
};

std::optional<uint8_t> constantBit(Value value) {
  if (auto constant = value.getDefiningOp<sim::SimLogicConstantOp>();
      constant && constant.getValue().getBitWidth() == 1)
    return constant.getValue().getZExtValue() |
           (constant.getUnknown().getZExtValue() << 1);
  if (auto constant = value.getDefiningOp<arith::ConstantOp>())
    if (auto integer = dyn_cast<IntegerAttr>(constant.getValue());
        integer && integer.getValue().getBitWidth() == 1)
      return integer.getValue().getZExtValue();
  return std::nullopt;
}

} // namespace

ClockInferenceAnalysis::ClockInferenceAnalysis(
    ModuleOp module, const NativeStateLayoutAnalysis &layout,
    ArrayRef<PeriodicClockSeed> seeds) {
  SmallVector<Transfer> transfers;
  DenseMap<uint32_t, SmallVector<Writer>> writers;
  DenseMap<ClockBit, ClockFact> initialConstants;
  SmallVector<const NativeStateLayoutAnalysis::Bound *> boundsByOffset;
  for (const auto &bound : layout.bounds)
    if (bound.width)
      boundsByOffset.push_back(&bound);
  llvm::sort(boundsByOffset, [](const auto *lhs, const auto *rhs) {
    return lhs->offset < rhs->offset;
  });
  auto boundAt = [&](uint64_t bit) -> const NativeStateLayoutAnalysis::Bound * {
    auto end = llvm::upper_bound(
        boundsByOffset, bit,
        [](uint64_t bit, const auto *bound) { return bit < bound->offset; });
    if (end == boundsByOffset.begin())
      return nullptr;
    const auto *bound = *std::prev(end);
    return bit - bound->offset < bound->width ? bound : nullptr;
  };
  DenseMap<uint64_t, SmallVector<const NativeStateLayoutAnalysis::Driver *>>
      driversByNet;
  DenseMap<uint64_t, const NativeStateLayoutAnalysis::Net *> netsByID;
  for (const auto &driver : layout.driverLayouts)
    driversByNet[driver.netId].push_back(&driver);
  for (const auto &net : layout.netLayouts)
    netsByID[net.id] = &net;
  module.walk([&](sim::SimDesignOp design) {
    HandleDataflowAnalysis analysis(design);
    // Outlined activations are another representation of their owning actor,
    // not additional physical drivers. Ordinary subroutines still contribute
    // conservative writer coverage below.
    llvm::SmallPtrSet<Operation *, 16> outlinedBodies;
    SymbolTable symbols(design);
    ClassDispatchAnalysis dispatch(design);
    for (sim::SimFuncOp actor : design.getOps<sim::SimFuncOp>())
      if (auto body = schedule::get<schedule::Field::EvalBody>(actor))
        if (Operation *function = symbols.lookup(body.getValue()))
          outlinedBodies.insert(function);
    DenseMap<Operation *, SmallVector<Operation *>> callees;
    SmallVector<Operation *> initialEntries, runtimeEntries;
    for (sim::SimFuncOp function : design.getOps<sim::SimFuncOp>()) {
      if (function.getEntryKind() == sim::EntryKind::RootInitializer)
        initialEntries.push_back(function);
      else if (function.getEntryKind() != sim::EntryKind::Function)
        runtimeEntries.push_back(function);
      function.walk([&](sim::SimCallOp call) {
        if (Operation *callee = symbols.lookup(call.getCallee()))
          callees[function].push_back(callee);
      });
      function.walk([&](sim::SimClassDirectCallOp call) {
        if (Operation *callee = symbols.lookup(call.getCallee()))
          callees[function].push_back(callee);
      });
      // IEEE 1800-2023 8.20 selects the dynamic receiver's implementation.
      // A method called during bootstrap can also be a runtime writer through
      // any compatible virtual target, including writes to hidden globals.
      function.walk([&](sim::SimClassVirtualCallOp call) {
        auto receiver =
            cast<sim::ClassHandleType>(call.getReceiver().getType());
        for (auto method : dispatch.compatibleImplementations(
                 dispatch.lookup(receiver), call.getSlot(),
                 call.getSignatureId(), false))
          if (Operation *callee = symbols.lookup(*method.getImplementation()))
            callees[function].push_back(callee);
      });
    }
    auto closure = [&](SmallVector<Operation *> pending) {
      llvm::DenseSet<Operation *> reached;
      while (!pending.empty()) {
        Operation *op = pending.pop_back_val();
        if (reached.insert(op).second)
          llvm::append_range(pending, callees[op]);
      }
      return reached;
    };
    auto initialization = closure(std::move(initialEntries));
    auto runtime = closure(std::move(runtimeEntries));
    for (sim::SimFuncOp function : design.getOps<sim::SimFuncOp>()) {
      if (function.isExternal() || function.getBody().empty() ||
          outlinedBodies.contains(function.getOperation()))
        continue;
      // Declaration initializers called only by bootstrap execute before the
      // periodic regime. A subroutine reachable from a running actor remains
      // a writer even if bootstrap calls it too.
      bool initializer =
          function.getEntryKind() == sim::EntryKind::RootInitializer ||
          (function.isPrivate() && initialization.contains(function) &&
           !runtime.contains(function));
      auto provenance = analysis.derive(function);
      auto range = [&](Value reference) -> std::optional<Writer> {
        auto found = provenance.find(reference);
        if (found == provenance.end() || !found->second.descriptor ||
            !found->second.width)
          return std::nullopt;
        const auto &p = found->second;
        const auto &offsets = p.resource == schedule::ComputeResourceKind::Net
                                  ? layout.netOffsets
                                  : layout.storageOffsets;
        auto offset = offsets.find(*p.descriptor);
        if (offset == offsets.end() || p.low > UINT64_MAX - offset->second)
          return std::nullopt;
        return Writer{function.getOperation(),
                      offset->second + (p.dynamic ? 0 : p.low),
                      p.dynamic ? p.rootWidth : p.width};
      };
      auto bit = [&](Value reference) -> std::optional<ClockBit> {
        auto p = provenance.find(reference);
        if (p == provenance.end() || p->second.dynamic)
          return std::nullopt;
        auto r = range(reference);
        if (!r || r->width != 1)
          return std::nullopt;
        const auto *bound = boundAt(r->low);
        return bound ? std::optional<ClockBit>{{bound->handleID, r->low}}
                     : std::nullopt;
      };
      auto recordWriter = [&](Value reference) {
        if (initializer)
          return;
        if (auto r = range(reference))
          if (const auto *bound = boundAt(r->low))
            writers[bound->handleID].push_back(*r);
      };
      Value input, output;
      Operation *wait = nullptr;
      SmallVector<Value> watched;
      bool pure = true;
      bool unsupportedWrite = false;
      unsigned waits = 0;
      DenseMap<Operation *, ClockBit> destinations;
      DenseMap<Operation *, std::pair<uint64_t, uint64_t>> writeRanges;
      function.walk([&](Operation *op) {
        Value destination, value;
        if (auto store = dyn_cast<sim::SimRefStoreOp>(op)) {
          destination = store.getReference();
          value = store.getValue();
        } else if (auto drive = dyn_cast<sim::SimDriverDriveOp>(op)) {
          destination = drive.getDriver();
          value = drive.getValue();
          auto p = provenance.find(destination);
          if (p == provenance.end() || !p->second.descriptor) {
            pure = false;
          } else {
            ArrayRef<const NativeStateLayoutAnalysis::Driver *> drivers =
                driversByNet[*p->second.descriptor];
            pure &= drivers.size() == 1;
            for (const auto *driver : drivers)
              pure &= driver->width == 1 && driver->drivenWidth == 1 &&
                      driver->strength0 != sim::Strength::HighZ &&
                      driver->strength1 != sim::Strength::HighZ;
            if (const auto *net = netsByID.lookup(*p->second.descriptor))
              pure &=
                  llvm::none_of(net->propagationDelays, [](const auto &delay) {
                    return delay.has_value();
                  });
          }
        } else if (auto nba = dyn_cast<sim::SimNBAEnqueueOp>(op)) {
          destination = nba.getDestination();
        }
        if (destination) {
          if (auto r = range(destination))
            writeRanges.try_emplace(op, r->low, r->width);
          if (auto target = bit(destination)) {
            destinations.try_emplace(op, *target);
            if (initializer) {
              ClockFact fact{ClockFact::Kind::Unknown};
              if (auto constant = value ? constantBit(value) : std::nullopt)
                fact = {ClockFact::Kind::Constant, {}, 0, *constant};
              auto &existing = initialConstants[*target];
              existing = ClockFact::join(existing, fact);
            }
          }
          recordWriter(destination);
          auto load = value ? value.getDefiningOp<sim::SimRefLoadOp>()
                            : sim::SimRefLoadOp{};
          auto read = value ? value.getDefiningOp<sim::SimNetReadOp>()
                            : sim::SimNetReadOp{};
          Value source = load   ? load.getReference()
                         : read ? read.getNet()
                                : Value{};
          pure &= source && (!input || bit(input) == bit(source)) &&
                  (!output || bit(output) == bit(destination));
          input = source;
          output = destination;
        } else if (sim::isSuspensionOp(op)) {
          ++waits;
          wait = op;
          auto any = dyn_cast<sim::SimSuspendAnyOp>(op);
          if (auto change = dyn_cast<sim::SimSuspendChangeOp>(op))
            watched.push_back(change.getWatched());
          else if (any)
            watched.append(any.getWatched().begin(), any.getWatched().end());
          pure &= isa<sim::SimSuspendChangeOp>(op) ||
                  (any && llvm::all_of(any.getEdges(), [](int32_t edge) {
                     return edge == static_cast<int32_t>(sim::EdgeKind::Change);
                   }));
        } else if (op != function.getOperation() &&
                   !isa<sim::SimRefLoadOp, sim::SimNetReadOp,
                        sim::SimContextStorageOp, sim::SimContextDriverOp,
                        sim::SimContextNetOp, sim::SimRefSubelementOp,
                        arith::ConstantOp, cf::BranchOp>(op))
          pure = false;
        // Include other physical writes (copy, overrides, inertial stores,
        // net writes, etc.) in coverage even though they have no clock transfer
        // rule. An unsupported write in this process cannot establish cadence.
        if (auto effects = dyn_cast<MemoryEffectOpInterface>(op)) {
          SmallVector<MemoryEffects::EffectInstance> instances;
          effects.getEffects(instances);
          for (const auto &effect : instances)
            if (isa<MemoryEffects::Write>(effect.getEffect()))
              if (Value reference = effect.getValue();
                  reference && reference != destination)
                if (range(reference)) {
                  recordWriter(reference);
                  unsupportedWrite = true;
                }
        }
        // Project formal writes through each actual reference. Outlining does
        // not make a subroutine that can write a clock into a separate domain.
        if (auto call = dyn_cast<sim::SimCallOp>(op)) {
          auto callee = dyn_cast_or_null<sim::SimFuncOp>(
              symbols.lookup(call.getCallee()));
          ArrayAttr summary =
              callee ? callee.getEffectSummaryAttr() : ArrayAttr{};
          for (auto [index, argument] : llvm::enumerate(call.getOperands())) {
            if (!range(argument))
              continue;
            bool mayWrite =
                !summary || llvm::any_of(summary, [&](Attribute attr) {
                  auto effect = cast<schedule::ComputeEffectAttr>(attr);
                  return (effect.getEffect() ==
                              schedule::ComputeEffectKind::Write ||
                          effect.getEffect() ==
                              schedule::ComputeEffectKind::NBA ||
                          effect.getEffect() ==
                              schedule::ComputeEffectKind::Drive) &&
                         (effect.getTarget() ==
                              schedule::ComputeTargetKind::Unknown ||
                          (effect.getTarget() ==
                               schedule::ComputeTargetKind::Formal &&
                           effect.getFormal() == index));
                });
            if (mayWrite) {
              recordWriter(argument);
              unsupportedWrite = true;
            }
          }
        }
        if (isa<sim::SimClassDirectCallOp, sim::SimClassVirtualCallOp>(op))
          for (Value argument : op->getOperands())
            if (range(argument)) {
              recordWriter(argument);
              unsupportedWrite = true;
            }
      });
      if (initializer || function.getEntryKind() == sim::EntryKind::Function)
        continue;
      auto source = input ? bit(input) : std::nullopt;
      auto target = output ? bit(output) : std::nullopt;
      if (pure && waits == 1 && source && target &&
          wait->getNumSuccessors() == 1 &&
          wait->getSuccessor(0) == wait->getBlock() && watched.size() == 1 &&
          bit(watched.front()) == source) {
        transfers.push_back({*source, *target, function.getOperation(), false});
        ++copyCount;
      }
      std::optional<ClockBit> cadence;
      bool uniform = !unsupportedWrite;
      function.walk([&](Operation *op) {
        if (sim::isSuspensionOp(op)) {
          auto edge = dyn_cast<sim::SimSuspendEdgeOp>(op);
          auto source = edge ? bit(edge.getWatched()) : std::nullopt;
          uniform &= source && (!cadence || cadence == source) &&
                     op->getNumSuccessors() == 1;
          if (source)
            cadence = source;
        } else if (op != function.getOperation()) {
          // Unsummarized calls or nested regions may write behind a handle.
          // They cannot establish a per-activation write certificate.
          uniform &= op->getNumRegions() == 0 && !isa<CallOpInterface>(op) &&
                     !isa<sim::SimCallOp, sim::SimClassDirectCallOp,
                          sim::SimClassVirtualCallOp>(op);
          if (auto nba = dyn_cast<sim::SimNBAEnqueueOp>(op))
            uniform &= !nba.getDelay();
        }
      });
      if (!uniform || !cadence || destinations.empty())
        continue;
      SmallVector<ClockBit> candidates;
      for (const auto &entry : destinations)
        candidates.push_back(entry.second);
      llvm::sort(candidates);
      candidates.erase(std::unique(candidates.begin(), candidates.end()),
                       candidates.end());
      // Count every overlapping write, including wider stores and dynamic
      // selections through another view of a candidate's physical root.
      auto single = proveSingleWrites(function, writeRanges, candidates);
      for (auto [index, candidate] : llvm::enumerate(candidates))
        if (single[index]) {
          transfers.push_back(
              {*cadence, candidate, function.getOperation(), true});
          ++cadenceCount;
        }
    }
  });

  auto overlaps = [](ClockBit bit, const Writer &writer) {
    return bit.second >= writer.low && bit.second - writer.low < writer.width;
  };
  auto exclusive = [&](ClockBit bit, Operation *owner) {
    return llvm::all_of(writers[bit.first], [&](const Writer &writer) {
      return !overlaps(bit, writer) || writer.owner == owner;
    });
  };
  if (module->hasAttr("obelisk.debug.native_timing"))
    for (const auto &seed : seeds)
      for (const auto &writer : writers[seed.bit.first])
        if (overlaps(seed.bit, writer) && writer.owner != seed.writer)
          llvm::errs() << "obelisk clock writer conflict: state="
                       << seed.bit.first << " owner="
                       << cast<sim::SimFuncOp>(writer.owner).getSymName()
                       << '\n';
  DenseMap<ClockBit, SmallVector<unsigned>> dependents;
  llvm::DenseSet<ClockBit> modeled;
  llvm::SmallSetVector<ClockBit, 32> worklist;
  auto join = [&](ClockBit bit, ClockFact fact) {
    auto &current = facts[bit];
    auto next = ClockFact::join(current, fact);
    if (!(current == next)) {
      current = next;
      worklist.insert(bit);
    }
  };
  for (auto [index, transfer] : llvm::enumerate(transfers)) {
    modeled.insert(transfer.target);
    dependents[transfer.source].push_back(index);
    facts.try_emplace(transfer.source);
    facts.try_emplace(transfer.target);
    if (!exclusive(transfer.target, transfer.owner))
      join(transfer.target, {ClockFact::Kind::Unknown});
  }
  for (const auto &seed : seeds) {
    modeled.insert(seed.bit);
    join(seed.bit,
         seed.halfPeriod && exclusive(seed.bit, seed.writer)
             ? ClockFact{ClockFact::Kind::Periodic, seed.bit, seed.halfPeriod}
             : ClockFact{ClockFact::Kind::Unknown});
  }
  for (auto [bit, constant] : initialConstants)
    if (llvm::none_of(writers[bit.first], [&](const Writer &writer) {
          return overlaps(bit, writer);
        })) {
      modeled.insert(bit);
      join(bit, constant);
    }
  // Unmodeled inputs are top, not bottom. Bottom is reserved for cycles whose
  // equations have not received information, so it cannot certify a clock.
  for (auto &entry : facts) {
    if (!modeled.contains(entry.first))
      join(entry.first, {ClockFact::Kind::Unknown});
  }
  while (!worklist.empty()) {
    ClockBit bit = worklist.pop_back_val();
    for (unsigned index : dependents[bit]) {
      const auto &transfer = transfers[index];
      ClockFact fact = lookup(bit);
      if (transfer.cadence && fact.hasTickBound())
        fact.kind = ClockFact::Kind::TickDriven;
      // A constant trigger does not imply its consumer's initial output value.
      if (transfer.cadence && fact.kind == ClockFact::Kind::Constant)
        fact = {ClockFact::Kind::Unknown};
      join(transfer.target, fact);
    }
  }
}

ClockFact ClockInferenceAnalysis::lookup(ClockBit bit) const {
  auto found = facts.find(bit);
  return found == facts.end() ? ClockFact{ClockFact::Kind::Unknown}
                              : found->second;
}

} // namespace obelisk::analysis
