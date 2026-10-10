//===- FuseClockedKernels.cpp - One activation per clock edge ------------===//
//
// IEEE 1800-2023 4.4.2.2 and 4.7 let a simulator take the Active events woken
// by one clock edge in any order, so every edge-triggered process of a clock
// may run as one activation (compute-body fusion relies on the same
// permission inside one instance). A port connection is an implied continuous
// assignment (23.3.3, 4.9.6). When a copy process is the only writer of a
// variable, the copy and its source have the same value sequence within each
// time slot, and an edge of the copy is an Active event the simulator may
// choose to process immediately after the source changes. This pass therefore
// forms one kernel per clock alias class and edge, across instances.
//
// Inside that activation an NBA (4.9.4, 10.4.2) normally updates its target
// in the NBA region, after every Active and Inactive event of the slot (4.5).
// Storing at the NBA statement has the same user-visible effect (4.3) when:
//   (a) every read in the kernel that can execute after an NBA site of the
//       root reads a snapshot of the root's incoming value instead;
//   (b) the kernel's NBA sites are the root's only writers, apart from static
//       initialization and activations that cannot share the kernel's phase;
//   (c) every process waiting on the root, transitively through its writes,
//       is a side-effect-free combinational process. 4.5 already lets those
//       evaluate between individual NBA updates, and they reevaluate from the
//       final values; no edge, level, event, VPI or toggle observer can see
//       the earlier update (CombinationalFusionAnalysis glitch insensitivity);
//   (d) no other activation that may share the kernel's phase reads or
//       writes the root or anything in its combinational cone.
// Phases come from ActivationPhaseAnalysis: activations in different phases
// of a slot are separated by an NBA region and cannot interleave. A root that
// nothing reads keeps its NBA, which unread-NBA elimination removes entirely.
// The stores keep the NBA execution order (4.6(b)); RHS and target operands
// are still computed at the statement (4.9.4).
//
// The kernel never suspends between its members. 4.7 permits interleaving
// other processes inside an activation but does not require it, so nothing
// can observe a member part way through. Monitors and strobes print in the
// Observed and Postponed regions (21.2.2, 21.2.3), and a VCD dump records
// the values each time increment ends with (21.7.2.1), so none of them sees
// when in the slot a store happened. VPI value-change callbacks could, which
// is why the pass requires VPI off; force, procedural continuous assignment
// and DPI calls make it leave the design unchanged.
//
//===----------------------------------------------------------------------===//

#include "ComputeFusion.h"
#include "obelisk/Analysis/ActivationMemoryAnalysis.h"
#include "obelisk/Analysis/ActivationPhaseAnalysis.h"
#include "obelisk/Analysis/HandleDataflowAnalysis.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"
#include "obelisk/Dialect/Schedule/Transforms/Passes.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Dialect/Simulation/SimulationTypes.h"

#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/DenseAnalysis.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Interfaces/ViewLikeInterface.h"

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/raw_ostream.h"

#include <queue>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMFUSECLOCKEDKERNELSPASS
#include "obelisk/Dialect/Schedule/Transforms/Passes.h.inc"

namespace {

using analysis::ActivationMemoryAnalysis;

class ObeliskSimFuseClockedKernelsPass final
    : public impl::ObeliskSimFuseClockedKernelsPassBase<
          ObeliskSimFuseClockedKernelsPass> {
public:
  using Base::Base;
  void runOnOperation() override;

private:
  Statistic kernels{this, "kernels", "clocked kernels formed"};
  Statistic fusedMembers{this, "fused-members",
                         "edge-triggered processes fused into kernels"};
  Statistic directRoots{this, "direct-roots",
                        "NBA roots stored at their statements"};
  Statistic snapshotRoots{this, "snapshot-roots",
                          "direct NBA roots read through a snapshot"};
  Statistic retainedRoots{this, "retained-roots",
                          "kernel NBA roots kept on the NBA region"};
  Statistic directSites{this, "direct-sites",
                        "NBA statements replaced by direct stores"};
};

/// Snapshots copy the root once per activation. Wider roots keep their NBA.
constexpr uint64_t maximumSnapshotBits = 2048;

struct Member {
  sim::SimFuncOp function;
  sim::SimSpawnOp spawn;
  Block *wait = nullptr;
  Block *body = nullptr;
  SmallVector<Block *> bodyBlocks;
  SmallVector<Operation *> preamble;
  uint64_t clock = 0;
  sim::EdgeKind edge = sim::EdgeKind::Change;
  uint32_t order = UINT32_MAX;
  llvm::DenseSet<uint64_t> reads, nbaWrites, writes;
  SmallVector<unsigned> fusedArguments;
};

bool isSuspension(Operation *operation) {
  return sim::isSuspensionOp(operation);
}

std::optional<uint64_t> effectRoot(schedule::ComputeEffectAttr effect) {
  if (effect.getTarget() != schedule::ComputeTargetKind::Descriptor)
    return std::nullopt;
  return ActivationMemoryAnalysis::getRoot(effect.getResource(),
                                           effect.getDescriptor());
}

bool isAccess(schedule::ComputeEffectKind kind) {
  return kind == schedule::ComputeEffectKind::Read ||
         kind == schedule::ComputeEffectKind::Write ||
         kind == schedule::ComputeEffectKind::Drive ||
         kind == schedule::ComputeEffectKind::NBA ||
         kind == schedule::ComputeEffectKind::Trigger;
}

bool isWrite(schedule::ComputeEffectKind kind) {
  return kind == schedule::ComputeEffectKind::Write ||
         kind == schedule::ComputeEffectKind::Drive ||
         kind == schedule::ComputeEffectKind::NBA;
}

/// The storage root of a whole-variable, constant-address handle.
std::optional<uint64_t> wholeStorageRoot(Value value,
                                         const analysis::HandleFacts &facts) {
  auto found = facts.find(value);
  if (found == facts.end() || !found->second.descriptor ||
      found->second.resource != schedule::ComputeResourceKind::Storage ||
      found->second.dynamic || found->second.low != 0 ||
      found->second.width != found->second.rootWidth)
    return std::nullopt;
  return *found->second.descriptor;
}

/// The storage root a handle may select, if it is a known storage root.
std::optional<uint64_t> storageRoot(Value value,
                                    const analysis::HandleFacts &facts) {
  auto found = facts.find(value);
  if (found == facts.end() || !found->second.descriptor ||
      found->second.resource != schedule::ComputeResourceKind::Storage)
    return std::nullopt;
  return *found->second.descriptor;
}

/// A one-bit clock: four-state `logic` or two-state `bit`.
bool isClockBit(Type type) {
  return isa<sim::LogicType, IntegerType>(type) &&
         sim::getPackedWidth(type) == 1u;
}

/// Remove the termination polls LowerUnit leaves after direct calls. Fusion
/// eligibility excludes every scheduler-writing operation from the members
/// and their callees, so these polls cannot observe a new request during the
/// kernel activation.
void removeTerminationPolls(sim::SimFuncOp function) {
  SmallVector<cf::CondBranchOp> polls;
  function.walk([&](cf::CondBranchOp branch) {
    auto requested =
        branch.getCondition().getDefiningOp<sim::SimTerminationRequestedOp>();
    if (requested &&
        isa<sim::SimReturnOp>(branch.getTrueDest()->getTerminator()))
      polls.push_back(branch);
  });
  OpBuilder builder(function.getContext());
  for (cf::CondBranchOp branch : polls) {
    auto requested =
        branch.getCondition().getDefiningOp<sim::SimTerminationRequestedOp>();
    Block *source = branch->getBlock();
    Block *continuation = branch.getFalseDest();
    SmallVector<Value> operands(branch.getFalseDestOperands());
    Location location = branch.getLoc();
    branch.erase();
    if (requested->use_empty())
      requested.erase();
    builder.setInsertionPointToEnd(source);
    cf::BranchOp::create(builder, location, continuation, operands);
  }
  for (auto block = function.getBody().begin(), end = function.getBody().end();
       block != end;) {
    Block &current = *block++;
    if (&current != &function.getBody().front() &&
        current.hasNoPredecessors() && current.without_terminator().empty() &&
        isa<sim::SimReturnOp>(current.getTerminator()))
      current.erase();
  }
}

bool collectBodyBlocks(Member &member) {
  SmallVector<Block *> pending{member.body};
  llvm::SmallPtrSet<Block *, 16> visited;
  Block *entry = &member.function.getBody().front();
  while (!pending.empty()) {
    Block *block = pending.pop_back_val();
    if (block == member.wait || !visited.insert(block).second)
      continue;
    if (block == entry)
      return false;
    member.bodyBlocks.push_back(block);
    Operation *terminator = block->getTerminator();
    if (isSuspension(terminator))
      return false;
    if (isa<sim::SimReturnOp>(terminator)) {
      // Only termination-poll exits are accepted; they are removed after
      // fusion.
      for (Block *predecessor : block->getPredecessors()) {
        auto branch = dyn_cast<cf::CondBranchOp>(predecessor->getTerminator());
        if (!branch || branch.getTrueDest() != block ||
            !branch.getCondition()
                 .getDefiningOp<sim::SimTerminationRequestedOp>())
          return false;
      }
      if (block->hasNoPredecessors())
        return false;
      continue;
    }
    if (!isa<BranchOpInterface>(terminator))
      return false;
    for (Block *successor : terminator->getSuccessors())
      if (successor != member.wait)
        pending.push_back(successor);
  }
  if (member.bodyBlocks.size() + 2 !=
      member.function.getBody().getBlocks().size())
    return false;
  DenseMap<Block *, unsigned> position;
  for (auto [index, block] : llvm::enumerate(member.function.getBody()))
    position[&block] = index;
  llvm::sort(member.bodyBlocks, [&](Block *lhs, Block *rhs) {
    return position[lhs] < position[rhs];
  });
  return true;
}

/// Operations defined in the entry block and used by the activation.
void collectPreamble(Member &member) {
  Block &entry = member.function.getBody().front();
  llvm::SmallPtrSet<Operation *, 16> needed;
  SmallVector<Value> pending;
  pending.append(member.wait->getTerminator()->operand_begin(),
                 member.wait->getTerminator()->operand_end());
  for (Operation &operation : member.wait->without_terminator())
    pending.append(operation.operand_begin(), operation.operand_end());
  for (Block *block : member.bodyBlocks)
    block->walk([&](Operation *operation) {
      pending.append(operation->operand_begin(), operation->operand_end());
    });
  while (!pending.empty()) {
    Operation *definition = pending.pop_back_val().getDefiningOp();
    if (!definition || definition->getBlock() != &entry ||
        definition == entry.getTerminator() ||
        !needed.insert(definition).second)
      continue;
    pending.append(definition->operand_begin(), definition->operand_end());
  }
  for (Operation &operation : entry.without_terminator())
    if (needed.contains(&operation))
      member.preamble.push_back(&operation);
}

/// Roots an NBA of the current kernel activation may already have targeted.
/// Such an NBA has not updated its root yet (4.9.4), so a direct store there
/// would be visible early to a later read of the root.
class PendingLattice final : public dataflow::AbstractDenseLattice {
public:
  using AbstractDenseLattice::AbstractDenseLattice;
  ChangeResult join(const llvm::BitVector &more) {
    if (more.size() > roots.size())
      roots.resize(more.size());
    llvm::BitVector next = roots;
    next |= more;
    if (next == roots)
      return ChangeResult::NoChange;
    roots = std::move(next);
    return ChangeResult::Change;
  }
  ChangeResult join(const AbstractDenseLattice &rhs) override {
    return join(static_cast<const PendingLattice &>(rhs).roots);
  }
  void print(raw_ostream &os) const override { os << roots.count(); }
  llvm::BitVector roots;
};

class PendingNBAAnalysis final
    : public dataflow::DenseForwardDataFlowAnalysis<PendingLattice> {
public:
  PendingNBAAnalysis(DataFlowSolver &solver,
                     const DenseMap<Operation *, unsigned> &enqueues,
                     unsigned roots)
      : DenseForwardDataFlowAnalysis(solver), enqueues(enqueues), roots(roots) {
  }
  LogicalResult visitOperation(Operation *op, const PendingLattice &before,
                               PendingLattice *after) override {
    llvm::BitVector state = before.roots;
    state.resize(roots);
    if (auto found = enqueues.find(op); found != enqueues.end())
      state.set(found->second);
    propagateIfChanged(after, after->join(state));
    return success();
  }
  void visitBlockTransfer(Block *block, ProgramPoint *point, Block *predecessor,
                          const PendingLattice &before,
                          PendingLattice *after) override {
    // The kernel's wait starts a new activation after the NBA region.
    if (sim::isSuspensionOp(predecessor->getTerminator())) {
      propagateIfChanged(after, after->join(llvm::BitVector(roots)));
      return;
    }
    propagateIfChanged(after, after->join(before.roots));
  }
  void setToEntryState(PendingLattice *lattice) override {
    propagateIfChanged(lattice, lattice->join(llvm::BitVector(roots)));
  }

private:
  const DenseMap<Operation *, unsigned> &enqueues;
  unsigned roots;
};

/// Whether an activation that may share the kernel's phases can observe a
/// root, directly or through its combinational cone.
class ExposedState final : public AnalysisState {
public:
  using AnalysisState::AnalysisState;
  ChangeResult expose() {
    if (exposed)
      return ChangeResult::NoChange;
    exposed = true;
    return ChangeResult::Change;
  }
  void print(raw_ostream &os) const override { os << exposed; }
  bool exposed = false;
};

class ExposureRoot final
    : public GenericLatticeAnchorBase<ExposureRoot,
                                      std::pair<uint64_t, Operation *>> {
public:
  using Base::Base;
  Location getLoc() const override { return getValue().second->getLoc(); }
  void print(raw_ostream &os) const override {
    os << "exposure of root " << getValue().first;
  }
};

/// Backward propagation through side-effect-free combinational processes:
/// when one of them writes an exposed root, every root it waits on is
/// exposed as well (it republishes their changes).
class ExposureAnalysis final : public DataFlowAnalysis {
public:
  struct Process {
    sim::SimFuncOp function;
    SmallVector<uint64_t> watched, written;
  };
  ExposureAnalysis(DataFlowSolver &solver, Operation *design,
                   ArrayRef<Process> processes, ArrayRef<uint64_t> seeds)
      : DataFlowAnalysis(solver), design(design), processes(processes),
        seeds(seeds) {
    registerAnchorKind<ExposureRoot>();
  }
  LogicalResult initialize(Operation *) override {
    for (uint64_t root : seeds)
      propagateIfChanged(state(root), state(root)->expose());
    for (const Process &process : processes)
      if (failed(visit(pointOf(process))))
        return failure();
    return success();
  }
  LogicalResult visit(ProgramPoint *point) override {
    auto found = byPoint.find(point);
    if (found == byPoint.end())
      return success();
    const Process &process = *found->second;
    if (llvm::none_of(process.written, [&](uint64_t root) {
          return getOrCreateFor<ExposedState>(point, anchor(root))->exposed;
        }))
      return success();
    for (uint64_t root : process.watched)
      propagateIfChanged(state(root), state(root)->expose());
    return success();
  }

private:
  ExposureRoot *anchor(uint64_t root) {
    return getLatticeAnchor<ExposureRoot>(std::make_pair(root, design));
  }
  ExposedState *state(uint64_t root) {
    return getOrCreate<ExposedState>(anchor(root));
  }
  ProgramPoint *pointOf(const Process &process) {
    sim::SimFuncOp function = process.function;
    ProgramPoint *point = getProgramPointBefore(&function.getBody().front());
    byPoint[point] = &process;
    return point;
  }
  Operation *design;
  ArrayRef<Process> processes;
  ArrayRef<uint64_t> seeds;
  DenseMap<ProgramPoint *, const Process *> byPoint;
};

/// Union-find over storage roots connected by clock copies.
struct ClockClasses {
  DenseMap<uint64_t, uint64_t> parent;
  uint64_t find(uint64_t root) {
    auto found = parent.find(root);
    if (found == parent.end() || found->second == root)
      return root;
    uint64_t top = find(found->second);
    parent[root] = top;
    return top;
  }
  void join(uint64_t source, uint64_t destination) {
    parent.try_emplace(source, source);
    parent[find(destination)] = find(source);
  }
};

void ObeliskSimFuseClockedKernelsPass::runOnOperation() {
  sim::SimDesignOp design = getOperation();
  ModuleOp module = design->getParentOfType<ModuleOp>();
  bool report = module && module->hasAttr("obelisk.debug.native_timing");
  schedule::ComputeGraphAttr graph = design.getComputeGraphAttr();
  if (!graph || graph.getVpi() != schedule::ComputeVPIMode::Off ||
      graph.getWorkers() != 1) {
    if (report)
      llvm::errs() << "obelisk clocked kernel: skipped design: graph\n";
    return;
  }
  llvm::StringMap<uint64_t> rejections;
  auto reject = [&](StringRef reason) { ++rejections[reason]; };

  // Force, procedural continuous assignment and foreign code can update or
  // observe any root outside the scheduler regions modeled below.
  bool unsupportedDesign = false;
  design.walk([&](Operation *operation) {
    unsupportedDesign |=
        isa<sim::SimOverrideOp, sim::SimDynamicOverrideOp,
            sim::SimReleaseOverrideOp, sim::SimDPICallOp>(operation);
  });
  auto bail = [&](StringRef reason) {
    if (report)
      llvm::errs() << "obelisk clocked kernel: skipped design: " << reason
                   << '\n';
  };
  if (unsupportedDesign)
    return bail("override or foreign call");

  SymbolTable symbols(design);
  analysis::HandleDataflowAnalysis provenance(design);
  ArrayAttr nodes = graph.getNodes();

  sim::SimFuncOp rootInitializer;
  for (sim::SimFuncOp function :
       design.getBody().front().getOps<sim::SimFuncOp>())
    if (function.getEntryKind() == sim::EntryKind::RootInitializer)
      rootInitializer = function;
  if (!rootInitializer)
    return bail("no root initializer");

  DenseMap<StringAttr, SmallVector<sim::SimSpawnOp>> spawns;
  design.walk([&](sim::SimSpawnOp spawn) {
    spawns[spawn.getCalleeAttr().getAttr()].push_back(spawn);
  });
  auto rootSpawn = [&](sim::SimFuncOp function) -> sim::SimSpawnOp {
    auto found = spawns.find(function.getSymNameAttr());
    if (found == spawns.end() || found->second.size() != 1)
      return {};
    sim::SimSpawnOp spawn = found->second.front();
    if (spawn->getBlock() != &rootInitializer.getBody().front() ||
        !spawn.getResult().use_empty())
      return {};
    return spawn;
  };
  // Every symbol reference other than the root spawn keeps a function alive.
  DenseMap<StringAttr, unsigned> symbolUseCounts;
  if (auto uses = SymbolTable::getSymbolUses(&design.getBody()))
    for (const SymbolTable::SymbolUse &use : *uses)
      ++symbolUseCounts[use.getSymbolRef().getRootReference()];

  // Fragment inventory by function and block.
  DenseMap<StringAttr, SmallVector<schedule::ComputeFragmentAttr>> fragments;
  for (Attribute attribute : nodes)
    if (auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(attribute))
      fragments[fragment.getFunction().getAttr()].push_back(fragment);
  auto blockAt = [](sim::SimFuncOp function, uint32_t index) -> Block * {
    for (Block &block : function.getBody())
      if (index-- == 0)
        return &block;
    return nullptr;
  };

  // Schedule order of Active fragments, used to keep the existing relative
  // order of cohort members wherever no dependence decides it.
  DenseMap<uint32_t, uint32_t> scheduleOrder;
  {
    uint32_t next = 0;
    for (Attribute regionAttribute : graph.getRegions()) {
      auto region = cast<schedule::ComputeRegionAttr>(regionAttribute);
      if (region.getKind() != schedule::ComputeRegionKind::Active)
        continue;
      for (Attribute groupAttribute : region.getGroups())
        for (int64_t member : cast<schedule::ComputeGroupAttr>(groupAttribute)
                                  .getFragments()
                                  .asArrayRef())
          scheduleOrder.try_emplace(static_cast<uint32_t>(member), next++);
    }
  }

  // Time-slot phases of every activation and root change (4.5).
  analysis::ActivationPhaseAnalysis phases(design, provenance);
  if (!phases.isValid())
    return bail("phase analysis");
  using Phases = analysis::ActivationPhaseAnalysis::Phases;

  // Writers and watchers of each root from the verified graph.
  struct Writer {
    StringAttr function;
    uint32_t block;
    schedule::ComputeEffectKind kind;
  };
  DenseMap<uint64_t, SmallVector<Writer>> writers;
  DenseMap<uint64_t, SmallVector<StringAttr>> watchers;
  DenseSet<uint64_t> readRoots;
  bool unknownRead = false;
  // Writes of an unresolved target may update any root.
  SmallVector<Writer> unknownWriters;
  auto effectRoots = [&](StringAttr function,
                         schedule::ComputeEffectAttr effect) {
    return phases.getEffectRoots(function, effect);
  };
  for (Attribute attribute : nodes) {
    auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(attribute);
    if (!fragment)
      continue;
    StringAttr function = fragment.getFunction().getAttr();
    for (auto effect :
         fragment.getEffects().getAsRange<schedule::ComputeEffectAttr>()) {
      if (effect.getTarget() == schedule::ComputeTargetKind::Local)
        continue;
      std::optional<SmallVector<uint64_t>> roots =
          effectRoots(function, effect);
      if (isWrite(effect.getEffect()) && !roots) {
        unknownWriters.push_back(
            {function, fragment.getBlock(), effect.getEffect()});
        continue;
      }
      bool reads = effect.getEffect() == schedule::ComputeEffectKind::Read ||
                   effect.getEffect() == schedule::ComputeEffectKind::Watch;
      if (!roots) {
        unknownRead |= reads;
        continue;
      }
      for (uint64_t root : *roots) {
        if (reads)
          readRoots.insert(root);
        if (isWrite(effect.getEffect()))
          writers[root].push_back(
              {function, fragment.getBlock(), effect.getEffect()});
        if (effect.getEffect() == schedule::ComputeEffectKind::Watch)
          watchers[root].push_back(function);
      }
    }
  }

  CombinationalFusionAnalysis combinational(
      design, provenance,
      ActivationMemoryAnalysis::DependencyMode::ReachingValues);
  const llvm::DenseSet<uint64_t> &glitchInsensitive =
      combinational.getGlitchInsensitiveStorage();
  DenseMap<StringAttr, bool> transparentCache;
  auto isTransparent = [&](StringAttr name) {
    auto [found, inserted] = transparentCache.try_emplace(name, false);
    if (inserted)
      if (auto function = symbols.lookup<sim::SimFuncOp>(name))
        found->second =
            combinational
                .analyze(function, provenance, /*interleaved=*/true, nullptr,
                         /*finalValuesOnly=*/true)
                .has_value();
    return found->second;
  };

  // 1. Clock alias classes: roots connected by proven copies, which change
  // in exactly the phases of their source.
  ClockClasses classes;
  for (auto &[destination, copy] : phases.getCopies())
    classes.join(copy.source, destination);

  // 2. Clocked members, grouped by clock class and edge.
  using GroupKey = std::pair<uint64_t, unsigned>;
  llvm::MapVector<GroupKey, std::vector<Member>> groups;
  for (sim::SimFuncOp function :
       design.getBody().front().getOps<sim::SimFuncOp>()) {
    if (function.isExternal() ||
        (function.getEntryKind() != sim::EntryKind::AlwaysFF &&
         function.getEntryKind() != sim::EntryKind::Always))
      continue;
    Member member;
    member.function = function;
    member.spawn = rootSpawn(function);
    if (!member.spawn) {
      reject("member spawn");
      continue;
    }
    if (symbolUseCounts.lookup(function.getSymNameAttr()) != 1) {
      reject("member symbol uses");
      continue;
    }
    if (function.getHomeRegion() != sim::EventRegion::Active ||
        function.getDomain() != sim::ExecutionDomain::Design) {
      reject("member region");
      continue;
    }
    sim::SimSuspendEdgeOp wait;
    unsigned suspensions = 0;
    function.walk([&](Operation *operation) {
      if (!isSuspension(operation))
        return;
      ++suspensions;
      wait = dyn_cast<sim::SimSuspendEdgeOp>(operation);
    });
    if (suspensions != 1 || !wait || wait.getResumeRegionAttr() ||
        !wait.getContinuationOperands().empty() ||
        wait->getParentRegion() != &function.getBody()) {
      reject("member wait shape");
      continue;
    }
    member.wait = wait->getBlock();
    member.body = wait.getContinuation();
    member.edge = wait.getEdge();
    Block &entry = function.getBody().front();
    auto entryBranch = dyn_cast<cf::BranchOp>(entry.getTerminator());
    if (!entryBranch || entryBranch.getDest() != member.wait ||
        !entryBranch.getDestOperands().empty() ||
        member.wait->getNumArguments() != 0 ||
        member.body->getNumArguments() != 0 || member.body == member.wait ||
        !llvm::all_of(entry.without_terminator(),
                      [](Operation &operation) {
                        return isMemoryEffectFree(&operation);
                      }) ||
        !llvm::all_of(member.wait->without_terminator(),
                      [](Operation &operation) {
                        return isMemoryEffectFree(&operation);
                      })) {
      reject("member entry shape");
      continue;
    }
    if (!collectBodyBlocks(member)) {
      reject("member body shape");
      continue;
    }
    if (!isComputeBodyFusionEligible(function, provenance)) {
      reject("member body eligibility");
      continue;
    }
    analysis::HandleFacts facts = provenance.derive(function);
    std::optional<uint64_t> clock = wholeStorageRoot(wait.getWatched(), facts);
    if (!clock ||
        !isClockBit(
            cast<sim::RefType>(wait.getWatched().getType()).getElementType())) {
      reject("member clock");
      continue;
    }
    member.clock = *clock;
    bool native = true;
    uint32_t bodyIndex = 0;
    for (auto [index, block] : llvm::enumerate(function.getBody()))
      if (&block == member.body)
        bodyIndex = index;
    llvm::DenseSet<Block *> body(member.bodyBlocks.begin(),
                                 member.bodyBlocks.end());
    for (schedule::ComputeFragmentAttr fragment :
         fragments.lookup(function.getSymNameAttr())) {
      native &= fragment.getTier() == schedule::ComputeTierKind::Native;
      if (fragment.getBlock() == bodyIndex) {
        auto order = scheduleOrder.find(fragment.getId());
        if (order != scheduleOrder.end())
          member.order = order->second;
      }
      Block *block = blockAt(function, fragment.getBlock());
      if (!block || !body.contains(block))
        continue;
      for (auto effect :
           fragment.getEffects().getAsRange<schedule::ComputeEffectAttr>()) {
        std::optional<uint64_t> root = effectRoot(effect);
        if (!root)
          continue;
        switch (effect.getEffect()) {
        case schedule::ComputeEffectKind::Read:
          member.reads.insert(*root);
          break;
        case schedule::ComputeEffectKind::NBA:
          member.nbaWrites.insert(*root);
          break;
        case schedule::ComputeEffectKind::Write:
        case schedule::ComputeEffectKind::Drive:
          member.writes.insert(*root);
          break;
        default:
          break;
        }
      }
    }
    if (!native) {
      reject("member tier");
      continue;
    }
    collectPreamble(member);
    groups[{classes.find(member.clock), static_cast<unsigned>(member.edge)}]
        .push_back(std::move(member));
  }

  // Transparent combinational processes: the roots each one waits on and
  // the roots it writes.
  std::vector<ExposureAnalysis::Process> combinationalProcesses;
  {
    DenseMap<StringAttr, unsigned> index;
    for (auto &[root, list] : watchers)
      for (StringAttr watcher : list)
        if (isTransparent(watcher)) {
          auto [found, inserted] =
              index.try_emplace(watcher, combinationalProcesses.size());
          if (inserted)
            combinationalProcesses.push_back(
                {symbols.lookup<sim::SimFuncOp>(watcher), {}, {}});
          combinationalProcesses[found->second].watched.push_back(root);
        }
    for (auto &[root, list] : writers)
      for (const Writer &write : list)
        if (auto found = index.find(write.function); found != index.end())
          combinationalProcesses[found->second].written.push_back(root);
  }

  // Every root of each clock alias class.
  DenseMap<uint64_t, SmallVector<uint64_t>> classMembers;
  for (auto &[destination, copy] : phases.getCopies()) {
    classMembers[classes.find(destination)].push_back(destination);
    classMembers[classes.find(copy.source)].push_back(copy.source);
  }

  // 3. Plan every kernel on the unmodified design: the analyses above
  // describe the original processes and graph, which materialization erases.
  struct Plan {
    GroupKey key;
    std::vector<Member> *members;
    DenseSet<uint64_t> eligibleRoots;
    SmallVector<unsigned> sequence;
    unsigned waitMember = 0;
    SmallVector<Value> operands;
    SmallVector<Type> inputTypes;
    SmallVector<DictionaryAttr> argumentAttrs;
    sim::SimSpawnOp insertionSpawn;
  };
  std::vector<Plan> plans;
  for (auto &[key, members] : groups) {
    uint64_t classRoot = key.first;
    DenseSet<StringAttr> memberNames;
    for (Member &member : members)
      memberNames.insert(member.function.getSymNameAttr());

    // The phases in which this kernel's clock edges occur. Every alias in the
    // class changes in exactly the phases of the class root.
    Phases kernelPhases = phases.getChangePhases(classRoot);
    auto meetsKernel = [&](StringAttr function, uint32_t blockIndex) {
      return phases.getPhases(function, blockIndex).meets(kernelPhases);
    };

    // (d) Accesses by activations that may run in the kernel's phase, which
    // the scheduler may order between the kernel and its NBA region.
    SmallVector<uint64_t> seeds;
    bool unknownAccess = false;
    for (Attribute attribute : nodes) {
      auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(attribute);
      if (!fragment)
        continue;
      StringAttr name = fragment.getFunction().getAttr();
      if (memberNames.contains(name) ||
          name == rootInitializer.getSymNameAttr())
        continue;
      if (auto function = symbols.lookup<sim::SimFuncOp>(name))
        if (function.getEntryKind() == sim::EntryKind::Final)
          continue;
      if (!meetsKernel(name, fragment.getBlock()))
        continue;
      if (isTransparent(name))
        continue;
      for (auto effect :
           fragment.getEffects().getAsRange<schedule::ComputeEffectAttr>()) {
        if (!isAccess(effect.getEffect()) ||
            effect.getTarget() == schedule::ComputeTargetKind::Local)
          continue;
        std::optional<SmallVector<uint64_t>> roots = effectRoots(name, effect);
        if (!roots) {
          unknownAccess = true;
          break;
        }
        llvm::append_range(seeds, *roots);
      }
      if (unknownAccess)
        break;
    }
    if (unknownAccess) {
      reject("unknown event access");
      continue;
    }
    // A root is exposed when anything in its combinational cone is accessed
    // by such an activation, or feeds this kernel's clock.
    seeds.push_back(classRoot);
    for (Member &member : members)
      seeds.push_back(member.clock);
    DataFlowSolver exposure;
    exposure.load<ExposureAnalysis>(design.getOperation(),
                                    combinationalProcesses, seeds);
    if (failed(exposure.initializeAndRun(design))) {
      reject("exposure analysis");
      continue;
    }
    auto isExposed = [&](uint64_t root) {
      const auto *state = exposure.lookupState<ExposedState>(
          exposure.getLatticeAnchor<ExposureRoot>(
              std::make_pair(root, design.getOperation())));
      return state && state->exposed;
    };

    // (b)/(c)/(d) per root.
    DenseSet<uint64_t> eligibleRoots;
    for (Member &member : members)
      for (uint64_t root : member.nbaWrites) {
        if (eligibleRoots.contains(root))
          continue;
        if (root >= ActivationMemoryAnalysis::netRootTag) {
          reject("root net");
          continue;
        }
        // Nothing reads the root: unread-NBA elimination removes the update
        // entirely (4.3), which is better than a store.
        if (!unknownRead && !readRoots.contains(root)) {
          reject("root unread");
          continue;
        }
        if (!glitchInsensitive.contains(root)) {
          reject("root observed");
          continue;
        }
        if (isExposed(root)) {
          reject("root accessed by event activation");
          continue;
        }
        bool writersOk = true;
        for (const Writer &write : writers.lookup(root)) {
          if (write.function == rootInitializer.getSymNameAttr())
            continue;
          if (memberNames.contains(write.function)) {
            writersOk &= write.kind == schedule::ComputeEffectKind::NBA;
            continue;
          }
          writersOk &= !meetsKernel(write.function, write.block);
        }
        for (const Writer &write : unknownWriters)
          writersOk &= !meetsKernel(write.function, write.block);
        if (!writersOk) {
          reject("root writers");
          continue;
        }
        eligibleRoots.insert(root);
      }

    // Kernel order: readers of an eligible root before its NBA writers;
    // otherwise keep the existing schedule order, including between writers
    // and readers of immediate stores.
    size_t count = members.size();
    llvm::sort(members, [](const Member &lhs, const Member &rhs) {
      return lhs.order < rhs.order;
    });
    SmallVector<SmallVector<unsigned>> successors(count);
    SmallVector<unsigned> indegree(count, 0);
    {
      DenseMap<uint64_t, SmallVector<unsigned>> readersOf, nbaWritersOf,
          writersOf;
      for (auto [index, member] : llvm::enumerate(members)) {
        for (uint64_t root : member.reads)
          readersOf[root].push_back(index);
        for (uint64_t root : member.nbaWrites)
          if (eligibleRoots.contains(root))
            nbaWritersOf[root].push_back(index);
        for (uint64_t root : member.writes)
          writersOf[root].push_back(index);
      }
      llvm::DenseSet<std::pair<unsigned, unsigned>> edges;
      auto addEdge = [&](unsigned from, unsigned to) {
        if (from != to && edges.insert({from, to}).second) {
          successors[from].push_back(to);
          ++indegree[to];
        }
      };
      for (auto &[root, nbaWriters] : nbaWritersOf)
        for (unsigned reader : readersOf.lookup(root))
          for (unsigned writer : nbaWriters)
            addEdge(reader, writer);
      for (auto &[root, immediate] : writersOf) {
        SmallVector<unsigned> touching(immediate.begin(), immediate.end());
        llvm::append_range(touching, readersOf.lookup(root));
        llvm::sort(touching);
        touching.erase(std::unique(touching.begin(), touching.end()),
                       touching.end());
        for (unsigned writer : immediate)
          for (unsigned other : touching)
            if (writer < other)
              addEdge(writer, other);
            else if (other < writer)
              addEdge(other, writer);
      }
    }
    SmallVector<unsigned> sequence;
    {
      std::priority_queue<unsigned, std::vector<unsigned>,
                          std::greater<unsigned>>
          ready;
      SmallVector<bool> done(count, false);
      for (unsigned index = 0; index != count; ++index)
        if (indegree[index] == 0)
          ready.push(index);
      while (sequence.size() != count) {
        if (ready.empty()) {
          // A cycle: take the earliest remaining member. Its late reads are
          // resolved by snapshots or retained NBAs below.
          for (unsigned index = 0; index != count; ++index)
            if (!done[index]) {
              ready.push(index);
              indegree[index] = 0;
              break;
            }
        }
        unsigned next = ready.top();
        ready.pop();
        if (done[next])
          continue;
        done[next] = true;
        sequence.push_back(next);
        for (unsigned successor : successors[next])
          if (!done[successor] && --indegree[successor] == 0)
            ready.push(successor);
      }
    }

    // Choose the clock root most members already wait on.
    DenseMap<uint64_t, unsigned> clockVotes;
    for (Member &member : members)
      ++clockVotes[member.clock];
    unsigned waitMember = 0;
    for (auto [index, member] : llvm::enumerate(members))
      if (clockVotes[member.clock] > clockVotes[members[waitMember].clock] ||
          (clockVotes[member.clock] == clockVotes[members[waitMember].clock] &&
           member.clock < members[waitMember].clock))
        waitMember = index;
    // A member that accesses any root of its clock class could observe a
    // copy that has not run yet when the kernel starts.
    DenseSet<uint64_t> aliasRoots;
    if (auto found = classMembers.find(classRoot); found != classMembers.end())
      aliasRoots.insert(found->second.begin(), found->second.end());
    for (Member &member : members)
      aliasRoots.insert(member.clock);
    aliasRoots.insert(classRoot);
    if (llvm::any_of(members, [&](const Member &member) {
          return llvm::any_of(member.reads,
                              [&](uint64_t root) {
                                return aliasRoots.contains(root);
                              }) ||
                 llvm::any_of(member.writes,
                              [&](uint64_t root) {
                                return aliasRoots.contains(root);
                              }) ||
                 llvm::any_of(member.nbaWrites, [&](uint64_t root) {
                   return aliasRoots.contains(root);
                 });
        })) {
      reject("member accesses its clock");
      continue;
    }
    // The kernel exists to store NBAs directly; without an eligible root the
    // existing per-instance process fusion is left in place.
    if (eligibleRoots.empty())
      continue;

    // The kernel's captures: the union of the members' spawn operands.
    SmallVector<Value> operands;
    SmallVector<Type> inputTypes;
    SmallVector<DictionaryAttr> argumentAttrs;
    DenseMap<Value, unsigned> operandIndices;
    sim::SimSpawnOp insertionSpawn = members.front().spawn;
    bool compatible = true;
    for (Member &member : members) {
      if (insertionSpawn->isBeforeInBlock(member.spawn))
        insertionSpawn = member.spawn;
      Block &entry = member.function.getBody().front();
      member.fusedArguments.clear();
      for (auto [argument, operand] :
           llvm::zip_equal(entry.getArguments(), member.spawn.getOperands())) {
        DictionaryAttr attrs =
            member.function.getArgAttrDict(argument.getArgNumber());
        auto [found, inserted] =
            operandIndices.try_emplace(operand, operands.size());
        if (inserted) {
          operands.push_back(operand);
          inputTypes.push_back(argument.getType());
          argumentAttrs.push_back(attrs);
        } else if (inputTypes[found->second] != argument.getType() ||
                   argumentAttrs[found->second] != attrs) {
          compatible = false;
        }
        member.fusedArguments.push_back(found->second);
      }
    }
    if (!compatible) {
      reject("incompatible captures");
      continue;
    }
    plans.push_back({key, &members, std::move(eligibleRoots),
                     std::move(sequence), waitMember, std::move(operands),
                     std::move(inputTypes), std::move(argumentAttrs),
                     insertionSpawn});
  }

  // 4. Build each planned kernel.
  bool changed = false;
  for (Plan &plan : plans) {
    std::vector<Member> &members = *plan.members;
    size_t count = members.size();
    ArrayRef<unsigned> sequence = plan.sequence;
    unsigned waitMember = plan.waitMember;
    uint64_t classRoot = plan.key.first;
    Member &first = members[sequence.front()];
    Location location = first.function.getLoc();
    OpBuilder builder = OpBuilder::atBlockEnd(&design.getBody().front());
    SmallVector<NamedAttribute> attributes;
    if (IntegerAttr codeUnit = first.function.getCodeUnitIdAttr())
      attributes.emplace_back(first.function.getCodeUnitIdAttrName(), codeUnit);
    unsigned symbolCounter = 0;
    SmallString<48> base;
    ("__obelisk_clocked_" + Twine(classRoot) + "_" + Twine(plan.key.second))
        .toVector(base);
    SmallString<48> name = SymbolTable::generateSymbolName<48>(
        base,
        [&](StringRef candidate) {
          return symbols.lookup(candidate) != nullptr;
        },
        symbolCounter);
    sim::SimFuncOp kernel = sim::SimFuncOp::create(
        builder, location, name,
        FunctionType::get(design.getContext(), plan.inputTypes, TypeRange{}),
        first.function.getEntryKind(), attributes, plan.argumentAttrs);
    symbols.insert(kernel);
    SymbolTable::setSymbolVisibility(kernel, SymbolTable::Visibility::Private);

    Block &entry = kernel.getBody().front();
    Block *wait = new Block;
    kernel.getBody().push_back(wait);
    std::vector<IRMapping> mappings(count);
    for (auto [index, member] : llvm::enumerate(members))
      for (auto [argument, fused] :
           llvm::zip_equal(member.function.getBody().front().getArguments(),
                           member.fusedArguments))
        mappings[index].map(argument, entry.getArgument(fused));
    for (unsigned index : sequence) {
      Member &member = members[index];
      for (Block *source : member.bodyBlocks) {
        Block *cloned = new Block;
        kernel.getBody().push_back(cloned);
        mappings[index].map(source, cloned);
        for (BlockArgument argument : source->getArguments())
          mappings[index].map(argument, cloned->addArgument(argument.getType(),
                                                            argument.getLoc()));
      }
    }
    for (auto [position, index] : llvm::enumerate(sequence)) {
      Block *next = position + 1 == count
                        ? wait
                        : mappings[sequence[position + 1]].lookup(
                              members[sequence[position + 1]].body);
      mappings[index].map(members[index].wait, next);
    }
    // The wait resumes into a block of its own, so state captured at the
    // start of an activation is captured exactly once per activation even if
    // the first member begins with a loop.
    Block *activation = new Block;
    kernel.getBody().getBlocks().insert(std::next(Region::iterator(wait)),
                                        activation);
    {
      OpBuilder start = OpBuilder::atBlockEnd(activation);
      cf::BranchOp::create(start, location,
                           mappings[sequence.front()].lookup(first.body));
    }

    builder.setInsertionPointToStart(&entry);
    for (unsigned index : sequence)
      for (Operation *operation : members[index].preamble)
        builder.clone(*operation, mappings[index]);
    cf::BranchOp::create(builder, location, wait);
    builder.setInsertionPointToStart(wait);
    for (unsigned index : sequence)
      for (Operation &operation : members[index].wait->without_terminator())
        builder.clone(operation, mappings[index]);
    Operation *suspend = builder.clone(
        *members[waitMember].wait->getTerminator(), mappings[waitMember]);
    suspend->setSuccessor(activation, 0);
    for (unsigned index : sequence) {
      Member &member = members[index];
      for (Block *source : member.bodyBlocks) {
        builder.setInsertionPointToEnd(mappings[index].lookup(source));
        for (Operation &operation : *source)
          builder.clone(operation, mappings[index]);
      }
    }
    removeTerminationPolls(kernel);

    builder.setInsertionPoint(plan.insertionSpawn);
    sim::SimSpawnOp::create(builder, location, kernel.getSymNameAttr(),
                            plan.operands, ArrayAttr{}, ArrayAttr{});
    for (Member &member : members) {
      member.spawn.erase();
      symbols.erase(member.function);
    }
    changed = true;
    ++kernels;
    fusedMembers += count;

    // 5. Late reads. A read executed after an NBA site of its root must see
    // the incoming value (4.9.4); find them with a forward may-dataflow over
    // the activation CFG.
    SmallVector<uint64_t> rootList(plan.eligibleRoots.begin(),
                                   plan.eligibleRoots.end());
    llvm::sort(rootList);
    DenseMap<uint64_t, unsigned> rootIndex;
    for (auto [index, root] : llvm::enumerate(rootList))
      rootIndex[root] = index;
    analysis::HandleFacts facts = provenance.derive(kernel);
    llvm::BitVector unsupported(rootList.size());
    enum class EventKind { Enqueue, Read, OpaqueRead };
    struct Event {
      EventKind kind;
      unsigned root;
      Operation *operation;
    };
    SmallVector<Block *> blocks;
    {
      SmallVector<Block *> pending{activation};
      DenseSet<Block *> seen;
      while (!pending.empty()) {
        Block *block = pending.pop_back_val();
        if (block == wait || !seen.insert(block).second)
          continue;
        blocks.push_back(block);
        for (Block *successor : block->getSuccessors())
          pending.push_back(successor);
      }
    }
    DenseMap<Block *, SmallVector<Event>> events;
    auto eligibleIndex = [&](Value value) -> std::optional<unsigned> {
      std::optional<uint64_t> root = storageRoot(value, facts);
      if (!root)
        return std::nullopt;
      auto found = rootIndex.find(*root);
      if (found == rootIndex.end())
        return std::nullopt;
      return found->second;
    };
    auto markAllUnsupported = [&] { unsupported.set(); };
    for (Block *block : blocks) {
      SmallVector<Event> &list = events[block];
      block->walk([&](Operation *operation) {
        bool nested = operation->getBlock() != block;
        if (auto enqueue = dyn_cast<sim::SimNBAEnqueueOp>(operation)) {
          std::optional<unsigned> index =
              eligibleIndex(enqueue.getDestination());
          if (!index)
            return;
          auto reference =
              dyn_cast<sim::RefType>(enqueue.getDestination().getType());
          if (nested || enqueue.getDelay() || enqueue.getClockingOutputAttr() ||
              !reference ||
              reference.getElementType() != enqueue.getValue().getType())
            unsupported.set(*index);
          else
            list.push_back({EventKind::Enqueue, *index, operation});
          if (eligibleIndex(enqueue.getValue()))
            unsupported.set(*eligibleIndex(enqueue.getValue()));
          return;
        }
        if (auto load = dyn_cast<sim::SimRefLoadOp>(operation)) {
          if (std::optional<unsigned> index =
                  eligibleIndex(load.getReference()))
            list.push_back({EventKind::Read, *index, operation});
          return;
        }
        if (auto copy = dyn_cast<sim::SimRefCopyOp>(operation)) {
          if (std::optional<unsigned> index = eligibleIndex(copy.getSource()))
            list.push_back({EventKind::Read, *index, operation});
          if (std::optional<unsigned> index =
                  eligibleIndex(copy.getDestination()))
            unsupported.set(*index);
          return;
        }
        if (auto call = dyn_cast<sim::SimCallOp>(operation)) {
          for (Value operand : call->getOperands())
            if (std::optional<unsigned> index = eligibleIndex(operand))
              unsupported.set(*index);
          auto callee =
              symbols.lookup<sim::SimFuncOp>(call.getCalleeAttr().getValue());
          ArrayAttr summary = callee ? callee.getEffectSummaryAttr() : nullptr;
          if (!summary) {
            markAllUnsupported();
            return;
          }
          for (auto effect :
               summary.getAsRange<schedule::ComputeEffectAttr>()) {
            if (effect.getTarget() == schedule::ComputeTargetKind::Formal ||
                effect.getTarget() == schedule::ComputeTargetKind::Local)
              continue;
            std::optional<uint64_t> root = effectRoot(effect);
            if (!root) {
              if (isAccess(effect.getEffect()))
                markAllUnsupported();
              continue;
            }
            auto found = rootIndex.find(*root);
            if (found == rootIndex.end())
              continue;
            if (isWrite(effect.getEffect()))
              unsupported.set(found->second);
            else if (effect.getEffect() == schedule::ComputeEffectKind::Read)
              list.push_back({EventKind::OpaqueRead, found->second, operation});
          }
          return;
        }
        if (isa<ViewLikeOpInterface>(operation))
          return;
        // Any other use of an eligible root's handle, including immediate
        // stores, is outside the contract.
        for (Value operand : operation->getOperands())
          if (std::optional<unsigned> index = eligibleIndex(operand))
            unsupported.set(*index);
      });
    }
    DenseMap<Operation *, unsigned> enqueues;
    for (Block *block : blocks)
      for (const Event &event : events[block])
        if (event.kind == EventKind::Enqueue)
          enqueues[event.operation] = event.root;
    DataFlowSolver pending;
    pending.load<dataflow::DeadCodeAnalysis>();
    pending.load<dataflow::SparseConstantPropagation>();
    pending.load<PendingNBAAnalysis>(enqueues, rootList.size());
    if (failed(pending.initializeAndRun(kernel)))
      unsupported.set();
    SmallVector<SmallVector<Operation *>> lateReads(rootList.size());
    for (Block *block : blocks)
      for (const Event &event : events[block]) {
        if (event.kind == EventKind::Enqueue)
          continue;
        const auto *state = pending.lookupState<PendingLattice>(
            pending.getProgramPointBefore(event.operation));
        // Without a state the read is unreachable; keep the NBA anyway.
        if (!state) {
          unsupported.set(event.root);
          continue;
        }
        if (event.root >= state->roots.size() || !state->roots.test(event.root))
          continue;
        if (event.kind == EventKind::OpaqueRead)
          unsupported.set(event.root);
        else
          lateReads[event.root].push_back(event.operation);
      }

    // 6. Snapshot roots whose late reads are views of the whole root.
    struct Snapshot {
      Value base;
      SmallVector<std::pair<Operation *, SmallVector<Operation *>>> reads;
    };
    DenseMap<unsigned, Snapshot> snapshots;
    for (unsigned index = 0; index != rootList.size(); ++index) {
      if (unsupported.test(index) || lateReads[index].empty())
        continue;
      Snapshot snapshot;
      bool ok = true;
      for (Operation *read : lateReads[index]) {
        Value reference = read->getOperand(0);
        SmallVector<Operation *> chain;
        while (Operation *definition = reference.getDefiningOp()) {
          if (!isa<sim::SimRefSubelementOp, sim::SimRefArrayElementOp,
                   sim::SimRefExtractOp, sim::SimRefDynExtractOp>(definition))
            break;
          chain.push_back(definition);
          reference = cast<ViewLikeOpInterface>(definition).getViewSource();
        }
        bool argument = isa<BlockArgument>(reference) &&
                        cast<BlockArgument>(reference).getOwner() == &entry;
        bool context = static_cast<bool>(
            reference.getDefiningOp<sim::SimContextStorageOp>());
        std::optional<uint64_t> whole = wholeStorageRoot(reference, facts);
        auto type = dyn_cast<sim::RefType>(reference.getType());
        std::optional<uint64_t> span =
            type ? sim::getProvenanceSpan(type.getElementType()) : std::nullopt;
        if ((!argument && !context) || !whole || *whole != rootList[index] ||
            !span || *span > maximumSnapshotBits ||
            (snapshot.base && snapshot.base.getType() != reference.getType())) {
          ok = false;
          break;
        }
        if (!snapshot.base || argument)
          snapshot.base = reference;
        snapshot.reads.push_back({read, std::move(chain)});
      }
      if (ok)
        snapshots[index] = std::move(snapshot);
      else
        unsupported.set(index);
    }
    builder.setInsertionPoint(activation->getTerminator());
    for (auto &[index, snapshot] : snapshots) {
      Value base = snapshot.base;
      if (auto context = base.getDefiningOp<sim::SimContextStorageOp>())
        base = builder.clone(*context)->getResult(0);
      auto type = cast<sim::RefType>(base.getType());
      Value old = sim::SimRefLoadOp::create(builder, location,
                                            type.getElementType(), base);
      Value copy = sim::SimRefAllocOp::create(builder, location, type, old);
      for (auto &[read, chain] : snapshot.reads) {
        OpBuilder viewBuilder(read);
        Value current = copy;
        for (Operation *view : llvm::reverse(chain)) {
          IRMapping mapping;
          mapping.map(cast<ViewLikeOpInterface>(view).getViewSource(), current);
          current = viewBuilder.clone(*view, mapping)->getResult(0);
        }
        read->setOperand(0, current);
      }
    }

    // 7. Store at the NBA statements of every remaining root.
    for (Block *block : blocks)
      for (const Event &event : events[block]) {
        if (event.kind != EventKind::Enqueue || unsupported.test(event.root))
          continue;
        auto enqueue = cast<sim::SimNBAEnqueueOp>(event.operation);
        OpBuilder storeBuilder(enqueue);
        sim::SimRefStoreOp::create(storeBuilder, enqueue.getLoc(),
                                   enqueue.getValue(),
                                   enqueue.getDestination());
        enqueue.erase();
        ++directSites;
      }
    for (unsigned index = 0; index != rootList.size(); ++index) {
      if (unsupported.test(index))
        ++retainedRoots;
      else {
        ++directRoots;
        if (snapshots.count(index))
          ++snapshotRoots;
      }
    }
    if (report)
      llvm::errs() << "obelisk clocked kernel: " << kernel.getSymName()
                   << " members=" << count << " eligible=" << rootList.size()
                   << " retained=" << unsupported.count()
                   << " snapshots=" << snapshots.size() << '\n';
  }
  if (report)
    for (auto &[reason, total] : rejections)
      llvm::errs() << "obelisk clocked kernel rejection: " << reason << " = "
                   << total << '\n';
  if (changed)
    design.removeComputeGraphAttr();
}

} // namespace
} // namespace obelisk
