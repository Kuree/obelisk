//===- ActivationPhaseAnalysis.cpp - Time-slot phases of activations ------===//

#include "obelisk/Analysis/ActivationPhaseAnalysis.h"
#include "obelisk/Analysis/ActivationMemoryAnalysis.h"
#include "obelisk/Dialect/Schedule/ScheduleEnums.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Threading.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
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

/// Phases attached to an anchor: a block's activations, root changes,
/// process or task starts, and task returns.
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

/// What one block contributes to the phase solve. A block's phases are
/// constant: they change only across suspensions, so the solve is over
/// blocks, not program points. Built from the block's own function and the
/// immutable design facts, for every function in parallel.
struct BlockSummary {
  /// How the terminator carries the block's phases to its successors.
  enum class Transfer : uint8_t {
    /// No suspension: the successor continues the same activation.
    Plain,
    /// A task call: the same activation unless the callee suspends.
    TaskCall,
    /// A positive constant delay, a zero (or negative) constant delay, or a
    /// variable delay.
    Positive,
    Zero,
    Variable,
    /// Never resumes.
    Forever,
    /// Resumes when one of `watched` changes.
    Watch,
    /// Any other suspension, or a wait on an unresolved handle.
    Unknown,
  };
  /// How a write updates its root relative to the writer's phases.
  enum class Update : uint8_t { Same, AfterNBA, Any };

  Transfer transfer = Transfer::Plain;
  SmallVector<uint64_t, 1> watched;
  /// Predecessors over executable CFG edges.
  SmallVector<Block *, 2> predecessors;
  /// Callees of spawns and task calls, and the task-call terminator's
  /// callee, resolved after the parallel build.
  SmallVector<StringAttr, 1> spawnNames, taskCallNames;
  SmallVector<Operation *, 1> spawns, taskCalls;
  StringAttr calleeName;
  Operation *callee = nullptr;
  bool calleeSuspends = false;
  /// A return of a task some modeled call enters.
  bool returns = false;
  /// Reachable from the function entry over executable edges.
  bool reachable = false;
  /// The block has a compute-graph fragment, whose effects follow.
  bool hasEffects = false;
  SmallVector<std::pair<uint64_t, Update>, 2> writes;
  /// (destination, source) of the function's proven copies.
  SmallVector<std::pair<uint64_t, uint64_t>, 1> copies;
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
  DenseMap<Block *, BlockSummary> summaries;

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
        auto caller = call->getParentOfType<sim::SimFuncOp>();
        Value actual = *std::next(arguments.begin(), formal);
        // Caller-local storage is no design root: an output or inout formal's
        // own slot, which the caller copies to the actual on return (IEEE
        // 1800-2023 13.5.1) in a fragment of its own, or a caller's automatic
        // variable passed by ref (13.5.2).
        const HandleFacts &callerFacts = factsOf(caller);
        if (auto fact = callerFacts.find(actual);
            fact != callerFacts.end() &&
            fact->second.resource == schedule::ComputeResourceKind::Local)
          continue;
        std::optional<uint64_t> root = rootOf(caller, actual);
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

/// Phases of blocks, roots, process starts and task returns. Each block
/// anchors one PhaseState: its activations' phases, the join over its
/// executable predecessors of the terminator transfers; the entry block's
/// come from the process start. A visit applies the block's spawns, task
/// calls, returns and fragment writes with the block's phases.
class PhaseBlocks final : public DataFlowAnalysis {
public:
  PhaseBlocks(DataFlowSolver &solver,
              const ActivationPhaseAnalysis::Shared &shared)
      : DataFlowAnalysis(solver), shared(shared) {
    registerAnchorKind<RootAnchor>();
    registerAnchorKind<EntryAnchor>();
    registerAnchorKind<ReturnAnchor>();
  }

  LogicalResult initialize(Operation *) override {
    for (auto &[block, summary] : shared.summaries)
      visitBlock(block, summary);
    return success();
  }

  LogicalResult visit(ProgramPoint *point) override {
    if (!point->isBlockStart())
      return success();
    auto found = shared.summaries.find(point->getBlock());
    if (found != shared.summaries.end())
      visitBlock(found->first, found->second);
    return success();
  }

private:
  void visitBlock(Block *block, const BlockSummary &summary) {
    ProgramPoint *point = getProgramPointBefore(block);
    auto function = cast<sim::SimFuncOp>(block->getParentOp());
    Phases incoming;
    if (block->isEntryBlock())
      incoming = entryPhases(function, point);
    else
      for (Block *predecessor : summary.predecessors)
        incoming = incoming | transfer(predecessor, point);
    auto *state = getOrCreate<PhaseState>(point);
    propagateIfChanged(state, state->merge(incoming));
    apply(function, point, summary, state->phases);
  }

  Phases entryPhases(sim::SimFuncOp function, ProgramPoint *point) {
    // Static initialization precedes every time slot.
    if (function == shared.rootInitializer)
      return {};
    if (!shared.modeledStarts.contains(function.getSymNameAttr())) {
      // Plain functions run inside their callers, whose fragments carry
      // their effects; any other entry is unconstrained.
      if (function.getEntryKind() != sim::EntryKind::Function)
        return Phases::any();
      return {};
    }
    return getOrCreateFor<PhaseState>(
               point, getLatticeAnchor<EntryAnchor>(function.getOperation()))
        ->phases;
  }

  /// Phases with which `predecessor`'s terminator enters a successor.
  /// Nothing returns to time zero after leaving it.
  Phases transfer(Block *predecessor, ProgramPoint *point) {
    const BlockSummary &from = shared.summaries.find(predecessor)->second;
    Phases waiting =
        getOrCreateFor<PhaseState>(point, getProgramPointBefore(predecessor))
            ->phases;
    using Transfer = BlockSummary::Transfer;
    if (from.transfer == Transfer::Plain)
      return waiting;
    if (waiting.empty())
      return {};
    PhaseSet atZero = waiting.zero ? anyPhase : 0;
    switch (from.transfer) {
    case Transfer::Plain:
      return waiting;
    case Transfer::TaskCall:
      // A task without suspensions returns within the caller's activation.
      if (!from.calleeSuspends)
        return waiting;
      if (!from.callee)
        return Phases::any();
      return getOrCreateFor<PhaseState>(
                 point, getLatticeAnchor<ReturnAnchor>(from.callee))
          ->phases;
    case Transfer::Positive:
      return {0, firstPhase};
    // A zero delay stays in its slot; a variable one may also leave it.
    case Transfer::Zero:
      return {atZero, waiting.later ? anyPhase : 0};
    case Transfer::Variable:
    case Transfer::Unknown:
      return {atZero, anyPhase};
    case Transfer::Forever:
      return {};
    case Transfer::Watch: {
      Phases triggers;
      for (uint64_t root : from.watched)
        triggers = triggers | changes(root, point);
      // A wait made in a later slot cannot observe a time-zero change.
      return {waiting.zero ? triggers.zero : 0, triggers.later};
    }
    }
    llvm_unreachable("unknown phase transfer");
  }

  void apply(sim::SimFuncOp function, ProgramPoint *point,
             const BlockSummary &summary, Phases phases) {
    // The root initializer starts every process at time zero.
    Phases start = function == shared.rootInitializer
                       ? (summary.reachable ? Phases{firstPhase, 0} : Phases{})
                       : phases;
    if (!start.empty())
      for (Operation *callee : summary.spawns)
        propagateIfChanged(entryState(callee), entryState(callee)->merge(start));
    if (phases.empty())
      return;
    for (Operation *callee : summary.taskCalls)
      propagateIfChanged(entryState(callee), entryState(callee)->merge(phases));
    if (summary.returns) {
      auto *returns = getOrCreate<PhaseState>(
          getLatticeAnchor<ReturnAnchor>(function.getOperation()));
      propagateIfChanged(returns, returns->merge(phases));
    }
    if (!summary.hasEffects)
      return;
    for (auto [destination, source] : summary.copies) {
      auto *state = rootState(destination);
      propagateIfChanged(
          state, state->merge(changes(source, point, /*withUnknown=*/false)));
    }
    for (auto [root, update] : summary.writes) {
      Phases updates = phases;
      if (update == BlockSummary::Update::AfterNBA)
        updates = ActivationPhaseAnalysis::afterNBA(updates);
      else if (update == BlockSummary::Update::Any)
        updates = Phases::any();
      auto *state = rootState(root);
      propagateIfChanged(state, state->merge(updates));
    }
  }

  PhaseState *entryState(Operation *function) {
    return getOrCreate<PhaseState>(getLatticeAnchor<EntryAnchor>(function));
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

  const ActivationPhaseAnalysis::Shared &shared;
};

/// Successors of `block` over executable edges. A branch whose constant
/// operands select one successor takes only that edge, as
/// DeadCodeAnalysis decides from constant operands.
SmallVector<Block *, 2> executableSuccessors(Block &block) {
  Operation *terminator = block.getTerminator();
  SmallVector<Block *, 2> successors;
  if (auto branch = dyn_cast<BranchOpInterface>(terminator)) {
    SmallVector<Attribute> operands;
    for (Value operand : terminator->getOperands()) {
      Attribute constant;
      operands.push_back(matchPattern(operand, m_Constant(&constant))
                             ? constant
                             : Attribute{});
    }
    if (Block *only = branch.getSuccessorForOperands(operands)) {
      successors.push_back(only);
      return successors;
    }
  }
  llvm::append_range(successors, terminator->getSuccessors());
  return successors;
}

/// One function's block summaries and, when it needs them, its handle facts.
struct FunctionSummary {
  sim::SimFuncOp function;
  std::optional<HandleFacts> facts;
  std::vector<std::pair<Block *, BlockSummary>> blocks;
};

/// Summarizes `function` reading only its own body and immutable design
/// facts, so functions summarize in parallel.
void summarize(const ActivationPhaseAnalysis::Shared &shared,
               FunctionSummary &result) {
  sim::SimFuncOp function = result.function;
  StringAttr name = function.getSymNameAttr();
  auto factsOf = [&]() -> const HandleFacts & {
    if (!result.facts)
      result.facts = shared.handles.derive(function);
    return *result.facts;
  };
  // Copy matching and task-formal resolution read these functions' facts.
  sim::EntryKind kind = function.getEntryKind();
  if (kind == sim::EntryKind::Continuous || kind == sim::EntryKind::PortInput ||
      kind == sim::EntryKind::PortOutput ||
      !function.getBody().getOps<sim::SimTaskCallOp>().empty())
    (void)factsOf();
  auto rootOf = [&](Value value) -> std::optional<uint64_t> {
    const HandleFacts &known = factsOf();
    auto found = known.find(value);
    if (found == known.end() || !found->second.descriptor)
      return std::nullopt;
    return ActivationMemoryAnalysis::getRoot(found->second.resource,
                                             *found->second.descriptor);
  };

  DenseMap<Block *, unsigned> positions;
  for (Block &block : function.getBody()) {
    positions[&block] = result.blocks.size();
    result.blocks.push_back({&block, BlockSummary{}});
  }
  SmallVector<SmallVector<Block *, 2>> successors;
  successors.reserve(result.blocks.size());
  bool tasked = shared.taskCalls.count(name);
  for (auto &[block, summary] : result.blocks) {
    for (Operation &operation : *block) {
      if (auto spawn = dyn_cast<sim::SimSpawnOp>(operation))
        summary.spawnNames.push_back(spawn.getCalleeAttr().getAttr());
      else if (auto call = dyn_cast<sim::SimTaskCallOp>(operation))
        summary.taskCallNames.push_back(call.getCalleeAttr().getAttr());
    }
    Operation *terminator = block->getTerminator();
    summary.returns = tasked && isa<sim::SimReturnOp>(terminator);
    if (auto index = shared.blockIndices.find(block);
        index != shared.blockIndices.end())
      summary.hasEffects =
          shared.fragmentEffects.count({name, index->second});
    successors.push_back(executableSuccessors(*block));

    using Transfer = BlockSummary::Transfer;
    if (!sim::isSuspensionOp(terminator)) {
      summary.transfer = Transfer::Plain;
    } else if (auto call = dyn_cast<sim::SimTaskCallOp>(terminator)) {
      summary.transfer = Transfer::TaskCall;
      summary.calleeName = call.getCalleeAttr().getAttr();
    } else if (auto delay = dyn_cast<sim::SimSuspendDelayOp>(terminator)) {
      auto constant = delay.getDelay().getDefiningOp<sim::SimTimeConstantOp>();
      summary.transfer = !constant                ? Transfer::Variable
                         : constant.getValue() > 0 ? Transfer::Positive
                                                   : Transfer::Zero;
    } else if (isa<sim::SimSuspendForeverOp>(terminator)) {
      summary.transfer = Transfer::Forever;
    } else {
      SmallVector<Value> watched;
      if (auto edge = dyn_cast<sim::SimSuspendEdgeOp>(terminator))
        watched.push_back(edge.getWatched());
      else if (auto change = dyn_cast<sim::SimSuspendChangeOp>(terminator))
        watched.push_back(change.getWatched());
      else if (auto any = dyn_cast<sim::SimSuspendAnyOp>(terminator))
        llvm::append_range(watched, any.getWatched());
      else if (auto edge = dyn_cast<sim::SimSuspendEdgeIffOp>(terminator))
        watched.push_back(edge.getWatched());
      else if (auto level = dyn_cast<sim::SimSuspendLevelOp>(terminator))
        watched.push_back(level.getWatched());
      summary.transfer = watched.empty() ? Transfer::Unknown : Transfer::Watch;
      for (Value value : watched) {
        std::optional<uint64_t> root = rootOf(value);
        if (!root) {
          summary.transfer = Transfer::Unknown;
          summary.watched.clear();
          break;
        }
        summary.watched.push_back(*root);
      }
    }
  }
  for (auto [position, entry] : llvm::enumerate(result.blocks))
    for (Block *successor : successors[position])
      if (auto found = positions.find(successor); found != positions.end())
        result.blocks[found->second].second.predecessors.push_back(
            entry.first);
  // Executability of the root initializer's blocks decides its spawns.
  if (!result.blocks.empty()) {
    SmallVector<unsigned> pending{0};
    result.blocks.front().second.reachable = true;
    while (!pending.empty()) {
      unsigned position = pending.pop_back_val();
      for (Block *successor : successors[position]) {
        auto found = positions.find(successor);
        if (found == positions.end() ||
            result.blocks[found->second].second.reachable)
          continue;
        result.blocks[found->second].second.reachable = true;
        pending.push_back(found->second);
      }
    }
  }
}
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

  // Each function's blocks summarize from its own body; the solve below
  // joins them across processes through roots, starts and returns.
  std::vector<FunctionSummary> functions;
  for (sim::SimFuncOp function :
       design.getBody().front().getOps<sim::SimFuncOp>())
    if (!function.isExternal())
      functions.emplace_back().function = function;
  parallelForEach(design.getContext(), functions,
                  [&](FunctionSummary &function) {
                    summarize(facts, function);
                  });
  for (FunctionSummary &function : functions) {
    if (function.facts)
      facts.facts[function.function.getSymNameAttr()] =
          std::move(*function.facts);
    for (auto &[block, summary] : function.blocks)
      facts.summaries[block] = std::move(summary);
  }
  findCopies(facts);

  for (auto &[block, summary] : facts.summaries) {
    auto function = cast<sim::SimFuncOp>(block->getParentOp());
    StringAttr name = function.getSymNameAttr();
    auto lookup = [&](StringAttr callee) -> Operation * {
      return facts.symbols.lookup<sim::SimFuncOp>(callee);
    };
    for (StringAttr callee : summary.spawnNames)
      if (Operation *found = lookup(callee))
        summary.spawns.push_back(found);
    for (StringAttr callee : summary.taskCallNames)
      if (Operation *found = lookup(callee))
        summary.taskCalls.push_back(found);
    if (summary.calleeName) {
      summary.calleeSuspends = facts.suspends(summary.calleeName);
      summary.callee = lookup(summary.calleeName);
    }
    if (!summary.hasEffects)
      continue;
    // The fragment's effects, including callee summaries. A copy process's
    // write to its destination follows its source instead.
    auto copied = facts.copies.find(name);
    if (copied != facts.copies.end())
      summary.copies.assign(copied->second.begin(), copied->second.end());
    ArrayAttr effects =
        facts.fragmentEffects.lookup({name, facts.blockIndices.lookup(block)});
    for (auto effect : effects.getAsRange<schedule::ComputeEffectAttr>()) {
      if (!isWriteEffect(effect.getEffect()) ||
          effect.getTarget() == schedule::ComputeTargetKind::Local)
        continue;
      if (copied != facts.copies.end() &&
          effect.getTarget() == schedule::ComputeTargetKind::Descriptor &&
          llvm::any_of(copied->second, [&](auto pair) {
            return ActivationMemoryAnalysis::getRoot(effect.getResource(),
                                                     effect.getDescriptor()) ==
                   pair.first;
          }))
        continue;
      auto update = BlockSummary::Update::Same;
      if (effect.getEffect() == schedule::ComputeEffectKind::NBA)
        update = effect.getDeferred() ? BlockSummary::Update::Any
                                      : BlockSummary::Update::AfterNBA;
      std::optional<SmallVector<uint64_t>> roots =
          facts.effectRoots(name, effect);
      if (!roots) {
        summary.writes.push_back({anyRoot, update});
        continue;
      }
      for (uint64_t root : *roots)
        summary.writes.push_back({root, update});
    }
  }

  solver = std::make_unique<DataFlowSolver>(
      DataFlowConfig().setInterprocedural(false));
  solver->load<PhaseBlocks>(facts);
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
      solver->lookupState<PhaseState>(solver->getProgramPointBefore(block));
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
