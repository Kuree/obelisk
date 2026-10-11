//===- ActivationPhaseAnalysis.cpp - Time-slot phases of activations ------===//

#include "obelisk/Analysis/ActivationPhaseAnalysis.h"
#include "obelisk/Analysis/ActivationMemoryAnalysis.h"
#include "obelisk/Dialect/Schedule/ScheduleEnums.h"

#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/DenseAnalysis.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/Support/Format.h"

using namespace mlir;

namespace obelisk::analysis {
namespace {
using Phases = ActivationPhaseAnalysis::Phases;
using PhaseSet = ActivationPhaseAnalysis::PhaseSet;
constexpr PhaseSet anyPhase = ActivationPhaseAnalysis::anyPhase;
constexpr PhaseSet firstPhase = ActivationPhaseAnalysis::firstPhase;
/// Root key standing for every root an unresolved write may update.
constexpr uint64_t anyRoot = ~uint64_t{0};

void printPhases(raw_ostream &os, Phases phases) {
  os << llvm::format_hex(phases.zero, 10) << '|'
     << llvm::format_hex(phases.later, 10);
}

/// Phases of the running activation at a program point.
class PhaseLattice final : public dataflow::AbstractDenseLattice {
public:
  using AbstractDenseLattice::AbstractDenseLattice;
  ChangeResult merge(Phases more) {
    Phases next = phases | more;
    if (next == phases)
      return ChangeResult::NoChange;
    phases = next;
    return ChangeResult::Change;
  }
  ChangeResult join(const AbstractDenseLattice &rhs) override {
    return merge(static_cast<const PhaseLattice &>(rhs).phases);
  }
  void print(raw_ostream &os) const override { printPhases(os, phases); }
  Phases phases;
};

/// Phases attached to a generic anchor: root changes, process or task
/// starts, and task returns.
class PhaseState final : public AnalysisState {
public:
  using AnalysisState::AnalysisState;
  ChangeResult merge(Phases more) {
    Phases next = phases | more;
    if (next == phases)
      return ChangeResult::NoChange;
    phases = next;
    return ChangeResult::Change;
  }
  void print(raw_ostream &os) const override { printPhases(os, phases); }
  Phases phases;
};

class RootAnchor final
    : public GenericLatticeAnchorBase<RootAnchor,
                                      std::pair<uint64_t, Operation *>> {
public:
  using Base::Base;
  Location getLoc() const override { return getValue().second->getLoc(); }
  void print(raw_ostream &os) const override {
    os << "changes of root " << getValue().first;
  }
};

class EntryAnchor final
    : public GenericLatticeAnchorBase<EntryAnchor, Operation *> {
public:
  using Base::Base;
  Location getLoc() const override { return getValue()->getLoc(); }
  void print(raw_ostream &os) const override { os << "starts of a process"; }
};

class ReturnAnchor final
    : public GenericLatticeAnchorBase<ReturnAnchor, Operation *> {
public:
  using Base::Base;
  Location getLoc() const override { return getValue()->getLoc(); }
  void print(raw_ostream &os) const override { os << "returns of a task"; }
};
} // namespace

/// Immutable design facts shared by the solver and the queries.
struct ActivationPhaseAnalysis::Shared {
  mutable sim::SimDesignOp design;
  const HandleDataflowAnalysis &handles;
  SymbolTable symbols;
  sim::SimFuncOp rootInitializer;
  /// Effects of each compute-graph fragment, keyed by function and block.
  DenseMap<std::pair<StringAttr, uint32_t>, ArrayAttr> fragmentEffects;
  DenseMap<StringAttr, SmallVector<Block *>> blocks;
  DenseMap<Block *, uint32_t> blockIndices;
  /// Functions entered only through spawns and task calls this analysis
  /// models; every other entry may occur in any phase.
  DenseSet<StringAttr> modeledStarts;
  DenseMap<StringAttr, SmallVector<sim::SimTaskCallOp>> taskCalls;
  mutable DenseMap<StringAttr, bool> suspendingTasks;
  mutable DenseMap<StringAttr, HandleFacts> facts;
  mutable DenseMap<std::pair<StringAttr, uint32_t>,
                   std::optional<SmallVector<uint64_t>>>
      formalRoots;

  /// Copy processes, by name, to the destination and source they connect.
  DenseMap<StringAttr, SmallVector<std::pair<uint64_t, uint64_t>>> copies;
  ActivationPhaseAnalysis::Copies copyRoots;

  Shared(sim::SimDesignOp design, const HandleDataflowAnalysis &handles)
      : design(design), handles(handles), symbols(design) {}

  const HandleFacts &factsOf(sim::SimFuncOp function) const {
    auto [found, inserted] = facts.try_emplace(function.getSymNameAttr());
    if (inserted)
      found->second = handles.derive(function);
    return found->second;
  }

  std::optional<uint64_t> rootOf(sim::SimFuncOp function, Value value) const {
    const HandleFacts &known = factsOf(function);
    auto found = known.find(value);
    if (found == known.end() || !found->second.descriptor)
      return std::nullopt;
    return ActivationMemoryAnalysis::getRoot(found->second.resource,
                                             *found->second.descriptor);
  }

  /// Whether a task may suspend before returning, through nested tasks too.
  bool suspends(StringAttr task) const {
    auto [found, inserted] = suspendingTasks.try_emplace(task, true);
    if (!inserted)
      return found->second;
    auto function = symbols.lookup<sim::SimFuncOp>(task);
    bool result = !function || function.isExternal();
    if (!result)
      function.walk([&](Operation *operation) {
        if (result || !sim::isSuspensionOp(operation))
          return;
        auto call = dyn_cast<sim::SimTaskCallOp>(operation);
        result = !call || suspends(call.getCalleeAttr().getAttr());
      });
    suspendingTasks[task] = result;
    return result;
  }

  const std::optional<SmallVector<uint64_t>> &
  resolveFormal(StringAttr task, uint32_t formal) const {
    auto [found, inserted] = formalRoots.try_emplace({task, formal});
    if (!inserted)
      return found->second;
    std::optional<SmallVector<uint64_t>> result;
    auto calls = taskCalls.find(task);
    if (calls != taskCalls.end() && modeledStarts.contains(task)) {
      SmallVector<uint64_t> roots;
      bool resolved = true;
      for (sim::SimTaskCallOp call : calls->second) {
        auto arguments = call.getArguments();
        if (formal >= static_cast<uint32_t>(
                          std::distance(arguments.begin(), arguments.end()))) {
          resolved = false;
          break;
        }
        std::optional<uint64_t> root =
            rootOf(call->getParentOfType<sim::SimFuncOp>(),
                   *std::next(arguments.begin(), formal));
        if (!root) {
          resolved = false;
          break;
        }
        roots.push_back(*root);
      }
      if (resolved)
        result = std::move(roots);
    }
    formalRoots[{task, formal}] = result;
    return formalRoots[{task, formal}];
  }

  std::optional<SmallVector<uint64_t>>
  effectRoots(StringAttr function, schedule::ComputeEffectAttr effect) const {
    if (effect.getTarget() == schedule::ComputeTargetKind::Formal)
      return resolveFormal(function, effect.getFormal());
    if (effect.getTarget() != schedule::ComputeTargetKind::Descriptor)
      return std::nullopt;
    std::optional<uint64_t> root = ActivationMemoryAnalysis::getRoot(
        effect.getResource(), effect.getDescriptor());
    if (!root)
      return std::nullopt;
    return SmallVector<uint64_t>{*root};
  }
};

namespace {
bool isWriteEffect(schedule::ComputeEffectKind kind) {
  return kind == schedule::ComputeEffectKind::Write ||
         kind == schedule::ComputeEffectKind::Drive ||
         kind == schedule::ComputeEffectKind::NBA;
}

class PhaseDataflow final
    : public dataflow::DenseForwardDataFlowAnalysis<PhaseLattice> {
public:
  PhaseDataflow(DataFlowSolver &solver,
                const ActivationPhaseAnalysis::Shared &shared)
      : DenseForwardDataFlowAnalysis(solver), shared(shared) {
    registerAnchorKind<RootAnchor>();
    registerAnchorKind<EntryAnchor>();
    registerAnchorKind<ReturnAnchor>();
  }

  LogicalResult visitOperation(Operation *op, const PhaseLattice &before,
                               PhaseLattice *after) override {
    propagateIfChanged(after, after->merge(before.phases));
    auto function = op->getParentOfType<sim::SimFuncOp>();
    if (!function)
      return success();
    bool initializer = function == shared.rootInitializer;
    if (auto spawn = dyn_cast<sim::SimSpawnOp>(op)) {
      // The root initializer starts every process at time zero.
      Phases start = initializer ? Phases{firstPhase, 0} : before.phases;
      if (auto callee = shared.symbols.lookup<sim::SimFuncOp>(
              spawn.getCalleeAttr().getAttr()))
        propagateIfChanged(entryState(callee),
                           entryState(callee)->merge(start));
      return success();
    }
    if (before.phases.empty())
      return success();
    if (auto call = dyn_cast<sim::SimTaskCallOp>(op)) {
      if (auto callee = shared.symbols.lookup<sim::SimFuncOp>(
              call.getCalleeAttr().getAttr()))
        propagateIfChanged(entryState(callee),
                           entryState(callee)->merge(before.phases));
    }
    if (isa<sim::SimReturnOp>(op) &&
        shared.taskCalls.count(function.getSymNameAttr())) {
      auto *returns = getOrCreate<PhaseState>(
          getLatticeAnchor<ReturnAnchor>(function.getOperation()));
      propagateIfChanged(returns, returns->merge(before.phases));
    }
    if (op != op->getBlock()->getTerminator())
      return success();
    // A block's phases are constant: they change only across suspensions.
    // Apply the fragment's effects, including callee summaries, once here.
    auto index = shared.blockIndices.find(op->getBlock());
    if (index == shared.blockIndices.end())
      return success();
    auto effects =
        shared.fragmentEffects.find({function.getSymNameAttr(), index->second});
    if (effects == shared.fragmentEffects.end())
      return success();
    auto copied = shared.copies.find(function.getSymNameAttr());
    if (copied != shared.copies.end()) {
      for (auto [destination, source] : copied->second) {
        Phases sourceChanges =
            changes(source, getProgramPointAfter(op), /*withUnknown=*/false);
        auto *state = rootState(destination);
        propagateIfChanged(state, state->merge(sourceChanges));
      }
    }
    for (auto effect :
         effects->second.getAsRange<schedule::ComputeEffectAttr>()) {
      if (!isWriteEffect(effect.getEffect()) ||
          effect.getTarget() == schedule::ComputeTargetKind::Local)
        continue;
      if (copied != shared.copies.end() &&
          effect.getTarget() == schedule::ComputeTargetKind::Descriptor &&
          llvm::any_of(copied->second, [&](auto pair) {
            return ActivationMemoryAnalysis::getRoot(effect.getResource(),
                                                     effect.getDescriptor()) ==
                   pair.first;
          }))
        continue;
      Phases updates = before.phases;
      if (effect.getEffect() == schedule::ComputeEffectKind::NBA)
        updates = effect.getDeferred()
                      ? Phases::any()
                      : ActivationPhaseAnalysis::afterNBA(updates);
      std::optional<SmallVector<uint64_t>> roots =
          shared.effectRoots(function.getSymNameAttr(), effect);
      SmallVector<uint64_t> targets =
          roots ? *roots : SmallVector<uint64_t>{anyRoot};
      for (uint64_t root : targets) {
        auto *state = rootState(root);
        propagateIfChanged(state, state->merge(updates));
      }
    }
    return success();
  }

  void visitBlockTransfer(Block *block, ProgramPoint *point, Block *predecessor,
                          const PhaseLattice &before,
                          PhaseLattice *after) override {
    Operation *terminator = predecessor->getTerminator();
    if (!terminator || !sim::isSuspensionOp(terminator)) {
      propagateIfChanged(after, after->merge(before.phases));
      return;
    }
    if (before.phases.empty())
      return;
    if (auto call = dyn_cast<sim::SimTaskCallOp>(terminator)) {
      // A task without suspensions returns within the caller's activation.
      StringAttr callee = call.getCalleeAttr().getAttr();
      Phases phases = before.phases;
      if (shared.suspends(callee)) {
        auto function = shared.symbols.lookup<sim::SimFuncOp>(callee);
        phases = function ? getOrCreateFor<PhaseState>(
                                point, getLatticeAnchor<ReturnAnchor>(
                                           function.getOperation()))
                                ->phases
                          : Phases::any();
      }
      propagateIfChanged(after, after->merge(phases));
      return;
    }
    propagateIfChanged(
        after, after->merge(resumePhases(terminator, point, before.phases)));
  }

  void setToEntryState(PhaseLattice *lattice) override {
    auto *point = dyn_cast_if_present<ProgramPoint *>(lattice->getAnchor());
    Block *block = point ? point->getBlock() : nullptr;
    auto function = block ? dyn_cast<sim::SimFuncOp>(block->getParentOp())
                          : sim::SimFuncOp{};
    if (!function) {
      propagateIfChanged(lattice, lattice->merge(Phases::any()));
      return;
    }
    // Static initialization precedes every time slot.
    if (function == shared.rootInitializer)
      return;
    if (!shared.modeledStarts.contains(function.getSymNameAttr())) {
      // Plain functions run inside their callers, whose fragments carry
      // their effects; any other entry is unconstrained.
      if (function.getEntryKind() != sim::EntryKind::Function)
        propagateIfChanged(lattice, lattice->merge(Phases::any()));
      return;
    }
    Phases start =
        getOrCreateFor<PhaseState>(
            point, getLatticeAnchor<EntryAnchor>(function.getOperation()))
            ->phases;
    propagateIfChanged(lattice, lattice->merge(start));
  }

  void visitCallControlFlowTransfer(CallOpInterface call,
                                    dataflow::CallControlFlowAction action,
                                    const PhaseLattice &before,
                                    PhaseLattice *after) override {
    // Spawns and calls are not interprocedural edges here: process and task
    // entries are modeled by their anchors, plain calls by fragment effects.
    (void)action;
    (void)visitOperation(call, before, after);
  }

private:
  PhaseState *entryState(sim::SimFuncOp function) {
    return getOrCreate<PhaseState>(
        getLatticeAnchor<EntryAnchor>(function.getOperation()));
  }
  PhaseState *rootState(uint64_t root) {
    return getOrCreate<PhaseState>(getLatticeAnchor<RootAnchor>(
        std::make_pair(root, shared.design.getOperation())));
  }
  Phases changes(uint64_t root, ProgramPoint *point, bool withUnknown = true) {
    auto anchor = [&](uint64_t key) {
      return getLatticeAnchor<RootAnchor>(
          std::make_pair(key, shared.design.getOperation()));
    };
    Phases phases = getOrCreateFor<PhaseState>(point, anchor(root))->phases;
    if (withUnknown)
      phases =
          phases | getOrCreateFor<PhaseState>(point, anchor(anyRoot))->phases;
    return phases;
  }
  /// Phases of the activation a suspension resumes, given the phases of the
  /// activation that suspends. Nothing returns to time zero after leaving it.
  Phases resumePhases(Operation *suspend, ProgramPoint *point, Phases waiting) {
    PhaseSet atZero = waiting.zero ? anyPhase : 0;
    if (auto delay = dyn_cast<sim::SimSuspendDelayOp>(suspend)) {
      auto constant = delay.getDelay().getDefiningOp<sim::SimTimeConstantOp>();
      if (constant && constant.getValue() > 0)
        return {0, firstPhase};
      // A zero delay stays in its slot; a variable one may also leave it.
      if (constant)
        return {atZero, waiting.later ? anyPhase : 0};
      return {atZero, anyPhase};
    }
    if (isa<sim::SimSuspendForeverOp>(suspend))
      return {};
    SmallVector<Value> watched;
    if (auto edge = dyn_cast<sim::SimSuspendEdgeOp>(suspend))
      watched.push_back(edge.getWatched());
    else if (auto change = dyn_cast<sim::SimSuspendChangeOp>(suspend))
      watched.push_back(change.getWatched());
    else if (auto any = dyn_cast<sim::SimSuspendAnyOp>(suspend))
      llvm::append_range(watched, any.getWatched());
    else if (auto edge = dyn_cast<sim::SimSuspendEdgeIffOp>(suspend))
      watched.push_back(edge.getWatched());
    else if (auto level = dyn_cast<sim::SimSuspendLevelOp>(suspend))
      watched.push_back(level.getWatched());
    else
      return {atZero, anyPhase};
    auto function = suspend->getParentOfType<sim::SimFuncOp>();
    Phases triggers;
    for (Value value : watched) {
      std::optional<uint64_t> root = shared.rootOf(function, value);
      if (!root)
        return {atZero, anyPhase};
      triggers = triggers | changes(*root, point);
    }
    // A wait made in a later slot cannot observe a time-zero change.
    return {waiting.zero ? triggers.zero : 0, triggers.later};
  }

  const ActivationPhaseAnalysis::Shared &shared;
};
} // namespace

namespace {
/// `loop: %v = load %src; store %v to %dst; wait change %src`, entered by an
/// empty entry block. Returns the whole-root (source, destination).
std::optional<std::pair<uint64_t, uint64_t>>
matchCopy(sim::SimFuncOp function, const HandleFacts &facts) {
  if (function.getBody().getBlocks().size() != 2)
    return std::nullopt;
  Block &entry = function.getBody().front();
  Block &loop = function.getBody().back();
  auto branch = dyn_cast<cf::BranchOp>(entry.getTerminator());
  if (!branch || branch.getDest() != &loop ||
      !entry.without_terminator().empty() || loop.getNumArguments() != 0 ||
      !branch.getDestOperands().empty())
    return std::nullopt;
  auto operations = loop.without_terminator();
  if (std::distance(operations.begin(), operations.end()) != 2)
    return std::nullopt;
  auto load = dyn_cast<sim::SimRefLoadOp>(*operations.begin());
  auto store = dyn_cast<sim::SimRefStoreOp>(*std::next(operations.begin()));
  auto wait = dyn_cast<sim::SimSuspendChangeOp>(loop.getTerminator());
  if (!load || !store || !wait || store.getValue() != load.getResult() ||
      wait.getWatched() != load.getReference() ||
      wait.getContinuation() != &loop ||
      !wait.getContinuationOperands().empty())
    return std::nullopt;
  auto whole = [&](Value value) -> std::optional<uint64_t> {
    auto found = facts.find(value);
    if (found == facts.end() || !found->second.descriptor ||
        found->second.resource != schedule::ComputeResourceKind::Storage ||
        found->second.dynamic || found->second.low != 0 ||
        found->second.width != found->second.rootWidth)
      return std::nullopt;
    return *found->second.descriptor;
  };
  std::optional<uint64_t> source = whole(load.getReference());
  std::optional<uint64_t> destination = whole(store.getReference());
  if (!source || !destination || *source == *destination)
    return std::nullopt;
  return std::make_pair(*source, *destination);
}
} // namespace

void ActivationPhaseAnalysis::findCopies(Shared &facts) {
  // Every writer of every root; an unresolved write could reach any root.
  DenseMap<uint64_t, DenseSet<StringAttr>> writers;
  for (auto &[key, effects] : facts.fragmentEffects)
    for (auto effect : effects.getAsRange<schedule::ComputeEffectAttr>()) {
      if (!isWriteEffect(effect.getEffect()) ||
          effect.getTarget() == schedule::ComputeTargetKind::Local)
        continue;
      std::optional<SmallVector<uint64_t>> roots =
          facts.effectRoots(key.first, effect);
      if (!roots)
        return;
      for (uint64_t root : *roots)
        writers[root].insert(key.first);
    }
  for (sim::SimFuncOp function :
       facts.design.getBody().front().getOps<sim::SimFuncOp>()) {
    sim::EntryKind kind = function.getEntryKind();
    if (function.isExternal() ||
        (kind != sim::EntryKind::Continuous &&
         kind != sim::EntryKind::PortInput &&
         kind != sim::EntryKind::PortOutput) ||
        !facts.modeledStarts.contains(function.getSymNameAttr()))
      continue;
    std::optional<std::pair<uint64_t, uint64_t>> copy =
        matchCopy(function, facts.factsOf(function));
    if (!copy)
      continue;
    auto [source, destination] = *copy;
    const DenseSet<StringAttr> &destinationWriters = writers[destination];
    bool initialized =
        facts.rootInitializer &&
        writers[source].contains(facts.rootInitializer.getSymNameAttr());
    if (initialized || destinationWriters.size() != 1 ||
        !destinationWriters.contains(function.getSymNameAttr()))
      continue;
    facts.copies[function.getSymNameAttr()].push_back({destination, source});
    facts.copyRoots[destination] = {source, function.getSymNameAttr()};
  }
}

ActivationPhaseAnalysis::ActivationPhaseAnalysis(
    sim::SimDesignOp design, const HandleDataflowAnalysis &handles)
    : shared(std::make_unique<Shared>(design, handles)) {
  schedule::ComputeGraphAttr graph = design.getComputeGraphAttr();
  if (!graph)
    return;
  Shared &facts = *shared;
  for (sim::SimFuncOp function :
       design.getBody().front().getOps<sim::SimFuncOp>()) {
    if (function.getEntryKind() == sim::EntryKind::RootInitializer)
      facts.rootInitializer = function;
    auto &blocks = facts.blocks[function.getSymNameAttr()];
    for (Block &block : function.getBody()) {
      facts.blockIndices[&block] = blocks.size();
      blocks.push_back(&block);
    }
  }
  for (Attribute attribute : graph.getNodes())
    if (auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(attribute))
      facts.fragmentEffects[{fragment.getFunction().getAttr(),
                             fragment.getBlock()}] = fragment.getEffects();
  // A process or task start is modeled when every symbol use is a spawn or
  // a task call; foreign, virtual or otherwise opaque entries are not. A
  // plain function runs in its callers' phases, which its blocks do not
  // carry, so a start inside one (a `fork ... join_none` in a function) is
  // not modeled either.
  DenseMap<StringAttr, bool> starts;
  if (auto uses = SymbolTable::getSymbolUses(&design.getBody()))
    for (const SymbolTable::SymbolUse &use : *uses) {
      StringAttr name = use.getSymbolRef().getRootReference();
      Operation *user = use.getUser();
      auto owner = user->getParentOfType<sim::SimFuncOp>();
      bool modeled = isa<sim::SimSpawnOp, sim::SimTaskCallOp>(user) && owner &&
                     owner.getEntryKind() != sim::EntryKind::Function;
      if (auto call = dyn_cast<sim::SimTaskCallOp>(user))
        facts.taskCalls[name].push_back(call);
      auto [found, inserted] = starts.try_emplace(name, modeled);
      if (!inserted)
        found->second &= modeled;
    }
  for (auto &[name, modeled] : starts)
    if (modeled)
      facts.modeledStarts.insert(name);
  findCopies(facts);

  solver = std::make_unique<DataFlowSolver>(
      DataFlowConfig().setInterprocedural(false));
  solver->load<dataflow::DeadCodeAnalysis>();
  solver->load<dataflow::SparseConstantPropagation>();
  solver->load<PhaseDataflow>(facts);
  valid = succeeded(solver->initializeAndRun(design));
}

const ActivationPhaseAnalysis::Copies &
ActivationPhaseAnalysis::getCopies() const {
  return shared->copyRoots;
}

std::optional<uint64_t>
ActivationPhaseAnalysis::getRoot(sim::SimFuncOp function, Value handle) const {
  return shared->rootOf(function, handle);
}

bool ActivationPhaseAnalysis::isModeledStart(StringAttr function) const {
  return shared->modeledStarts.contains(function);
}

bool ActivationPhaseAnalysis::taskSuspends(StringAttr task) const {
  return shared->suspends(task);
}

ArrayAttr ActivationPhaseAnalysis::getFragmentEffects(Block *block) const {
  auto index = shared->blockIndices.find(block);
  if (index == shared->blockIndices.end())
    return {};
  auto function = cast<sim::SimFuncOp>(block->getParentOp());
  return shared->fragmentEffects.lookup(
      {function.getSymNameAttr(), index->second});
}

ActivationPhaseAnalysis::~ActivationPhaseAnalysis() = default;
ActivationPhaseAnalysis::ActivationPhaseAnalysis(ActivationPhaseAnalysis &&) =
    default;

ActivationPhaseAnalysis::Phases
ActivationPhaseAnalysis::getPhases(Block *block) const {
  if (!valid || !block)
    return Phases::any();
  const auto *state =
      solver->lookupState<PhaseLattice>(solver->getProgramPointBefore(block));
  return state ? state->phases : Phases{};
}

ActivationPhaseAnalysis::Phases
ActivationPhaseAnalysis::getPhases(StringAttr function,
                                   uint32_t blockIndex) const {
  auto blocks = shared->blocks.find(function);
  if (blocks == shared->blocks.end() || blockIndex >= blocks->second.size())
    return Phases::any();
  return getPhases(blocks->second[blockIndex]);
}

ActivationPhaseAnalysis::Phases
ActivationPhaseAnalysis::getChangePhases(uint64_t root) const {
  if (!valid)
    return Phases::any();
  auto lookup = [&](uint64_t key) -> Phases {
    const auto *state =
        solver->lookupState<PhaseState>(solver->getLatticeAnchor<RootAnchor>(
            std::make_pair(key, shared->design.getOperation())));
    return state ? state->phases : Phases{};
  };
  return lookup(root) | lookup(anyRoot);
}

std::optional<SmallVector<uint64_t>> ActivationPhaseAnalysis::getEffectRoots(
    StringAttr function, schedule::ComputeEffectAttr effect) const {
  return shared->effectRoots(function, effect);
}

} // namespace obelisk::analysis
