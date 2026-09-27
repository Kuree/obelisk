//===- MaterializeComputeFusion.cpp - Fuse static process bodies ----------===//

#include "ComputeFusion.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"

#include "obelisk/Analysis/NativeAOTAnalysis.h"
#include "obelisk/Analysis/SimulationVPIAnalysis.h"
#include "obelisk/Dialect/Schedule/Transforms/Passes.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMMATERIALIZECOMPUTEFUSIONPASS
#include "obelisk/Dialect/Schedule/Transforms/Passes.h.inc"

namespace {

// Watch snapshots and driver values may be either value domain. A two-state
// packed value has no unknown plane and must use integer comparison after
// flattening; the four-state comparison retains X/Z equality semantics.
static Value createPackedCaseComparison(OpBuilder &builder, Location location,
                                        sim::CompareKind kind, Value lhs,
                                        Value rhs) {
  Type scalarType = sim::getPackedScalarType(lhs.getType());
  if (isa<IntegerType>(scalarType)) {
    if (lhs.getType() != scalarType) {
      lhs = sim::SimPackedFlattenOp::create(builder, location, scalarType, lhs);
      rhs = sim::SimPackedFlattenOp::create(builder, location, scalarType, rhs);
    }
    return arith::CmpIOp::create(builder, location,
                                 kind == sim::CompareKind::CaseEq
                                     ? arith::CmpIPredicate::eq
                                     : arith::CmpIPredicate::ne,
                                 lhs, rhs);
  }
  return sim::SimLogicCompareOp::create(builder, location, builder.getI1Type(),
                                        kind, lhs, rhs);
}

// Frozen graph membership and source identities are shared by all cohorts.
// Coverage links are mutable: move their index entries when an actor is fused.
struct FusionInputIndex {
  FusionInputIndex(sim::SimDesignOp design, schedule::ComputeGraphAttr graph) {
    for (auto [index, attribute] : llvm::enumerate(graph.getNodes()))
      if (auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(attribute))
        fragments[fragment.getFunction().getAttr()].push_back(index);
    for (Attribute attribute : graph.getEdges()) {
      auto edge = cast<schedule::ComputeEdgeAttr>(attribute);
      outgoingEdges[edge.getSource()].push_back(edge);
    }
    for (Operation &operation : design.getBody().front()) {
      if (auto keepalive = dyn_cast<sim::SimCoverageKeepaliveOp>(operation))
        keepalives[keepalive.getFunctionAttr().getAttr()].push_back(keepalive);
      if (auto function = dyn_cast<sim::SimFuncOp>(operation))
        if (auto body =
                ::obelisk::schedule::get<::obelisk::schedule::Field::EvalBody>(
                    function))
          sourceCodeUnits.try_emplace(body.getAttr(),
                                      function.getCodeUnitIdAttr());
    }
  }

  void retargetCoverageKeepalives(sim::SimFuncOp source,
                                  sim::SimFuncOp replacement) {
    auto found = keepalives.find(source.getSymNameAttr());
    if (found == keepalives.end())
      return;
    auto links = std::move(found->second);
    keepalives.erase(found);
    for (auto keepalive : links)
      keepalive.setFunctionAttr(
          FlatSymbolRefAttr::get(replacement.getSymNameAttr()));
    llvm::append_range(keepalives[replacement.getSymNameAttr()], links);
  }

  DenseMap<StringAttr, SmallVector<uint32_t>> fragments;
  DenseMap<uint32_t, SmallVector<schedule::ComputeEdgeAttr>> outgoingEdges;
  DenseMap<StringAttr, IntegerAttr> sourceCodeUnits;
  DenseMap<StringAttr, SmallVector<sim::SimCoverageKeepaliveOp>> keepalives;
};

constexpr auto evalOriginNBASiteAttr =
    ::obelisk::schedule::Field::EvalOriginNbaSite;
static void preserveEvalNBASiteOrigins(sim::SimFuncOp body) {
  body.walk([&](sim::SimNBAEnqueueOp enqueue) {
    if (::obelisk::schedule::has<evalOriginNBASiteAttr>(enqueue))
      return;
    schedule::NBASiteAttr site = enqueue.getSiteAttr();
    if (site)
      ::obelisk::schedule::set<evalOriginNBASiteAttr>(
          enqueue, IntegerAttr::get(IntegerType::get(body.getContext(), 64),
                                    site.getId()));
  });
}

bool useEvalBodyFusion(sim::SimDesignOp design) {
  ModuleOp module = design->getParentOfType<ModuleOp>();
  auto scheduler =
      ::obelisk::schedule::get<::obelisk::schedule::Field::NativeScheduler>(
          module);
  if (!scheduler)
    return false;
  bool autoRequested = ::obelisk::schedule::has<
      ::obelisk::schedule::Field::NativeSchedulerAutoRequested>(module);
  if (scheduler.getValue() == schedule::NativeSchedulerMode::Eval &&
      !autoRequested)
    return true;
  if (scheduler.getValue() != schedule::NativeSchedulerMode::Auto &&
      !(scheduler.getValue() == schedule::NativeSchedulerMode::Eval &&
        autoRequested))
    return false;

  // The driver temporarily represents Auto as Eval + auto_requested so this
  // early pipeline can prepare direct bodies before backend selection. Both
  // representations must restrict that work to designs already certified as
  // fully closed and cost-effective, so dynamic/UVM-heavy models do not pay
  // the code-size and cloning cost of an unusable evaluator.
  return analysis::NativeAOTAnalysis::compute(module).isAOTCostEffective();
}

bool isPrimitiveContinuousFusion(SymbolTable &symbols,
                                 schedule::ComputeFusionAttr fusion,
                                 schedule::ComputeGraphAttr graph) {
  if (!fusion || !graph || fusion.getFragments().empty())
    return false;
  for (int64_t member : fusion.getFragments().asArrayRef()) {
    if (member < 0 || static_cast<uint64_t>(member) >= graph.getNodes().size())
      return false;
    auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(
        graph.getNodes()[static_cast<size_t>(member)]);
    sim::SimFuncOp function =
        fragment
            ? symbols.lookup<sim::SimFuncOp>(fragment.getFunction().getValue())
            : sim::SimFuncOp{};
    if (!function || function.getEntryKind() != sim::EntryKind::Continuous ||
        !::obelisk::schedule::has<::obelisk::schedule::Field::PrimitiveName>(
            function))
      return false;
  }
  return true;
}

class ObeliskSimMaterializeComputeFusionPass final
    : public impl::ObeliskSimMaterializeComputeFusionPassBase<
          ObeliskSimMaterializeComputeFusionPass> {
public:
  using Base = impl::ObeliskSimMaterializeComputeFusionPassBase<
      ObeliskSimMaterializeComputeFusionPass>;
  using Base::Base;
  ObeliskSimMaterializeComputeFusionPass(
      const ObeliskSimMaterializeComputeFusionPass &other)
      : Base(other) {}

  void runOnOperation() override;

private:
  Statistic materializedFusions{this, "materialized-fusions",
                                "verified process-body fusions materialized"};
  Statistic rejectedFusions{
      this, "rejected-fusions",
      "planned fusions rejected by executable-structure validation"};
  Statistic eliminatedTerminationPolls{
      this, "eliminated-termination-polls",
      "redundant post-inline termination polls removed from fused bodies"};
  Statistic ifConvertedNBAs{
      this, "if-converted-nbas",
      "conditional last-write NBA diamonds converted to selects"};
  Statistic sharedStableConditions{
      this, "shared-stable-conditions",
      "equivalent stable branch conditions shared across fused actors"};
  Statistic promotedPrivateStores{
      this, "promoted-private-stores",
      "private static temporary loads forwarded from fused-activation SSA"};
};

struct BodyFusionCandidate {
  sim::SimFuncOp function;
  uint64_t instanceScope = 0;
  sim::SimSpawnOp spawn;
  Block *wait = nullptr;
  Block *body = nullptr;
  uint32_t resumeTarget = UINT32_MAX;
  uint32_t resumeOrder = UINT32_MAX;
  uint32_t entryOrder = UINT32_MAX;
  SmallVector<Operation *> entryPreamble;
  SmallVector<Block *> bodyBlocks;
  SmallVector<unsigned> fusedArguments;
  SmallVector<Value> threadedEntryValues;
};

class CodeUnitIndex {
public:
  explicit CodeUnitIndex(sim::SimDesignOp design) {
    for (auto declaration :
         design.getBody().front().getOps<sim::SimCodeUnitDeclOp>())
      scopes.try_emplace(declaration.getId(), declaration.getScopeId());
  }

  std::optional<uint64_t> getScope(sim::SimFuncOp function) const {
    std::optional<uint64_t> id = function.getCodeUnitId();
    auto found = id ? scopes.find(*id) : scopes.end();
    if (found == scopes.end())
      return std::nullopt;
    return found->second;
  }

  uint64_t allocate(uint64_t scope) {
    while (scopes.contains(nextID))
      ++nextID;
    uint64_t id = nextID++;
    scopes.try_emplace(id, scope);
    return id;
  }

  void erase(uint64_t id) {
    scopes.erase(id);
    nextID = std::min(nextID, id);
  }

private:
  DenseMap<uint64_t, uint64_t> scopes;
  uint64_t nextID = 1;
};

bool isSupportedEntryKind(sim::EntryKind kind) {
  return kind == sim::EntryKind::Always || kind == sim::EntryKind::AlwaysFF;
}

bool isTypedDirectWait(Operation *operation) {
  return isa<sim::SimSuspendChangeOp, sim::SimSuspendEdgeOp>(operation);
}

bool isEvalDirectWait(Operation *operation) {
  return isTypedDirectWait(operation) ||
         isa<sim::SimSuspendAnyOp, sim::SimSuspendObserveOp>(operation);
}

bool isTypedSuspend(Operation *operation) {
  return isa<
      sim::SimSuspendDelayOp, sim::SimSuspendChangeOp, sim::SimSuspendEdgeOp,
      sim::SimSuspendEdgeIffOp, sim::SimSuspendLevelOp, sim::SimSuspendAnyOp,
      sim::SimSuspendEventOp, sim::SimSuspendEventOrderOp,
      sim::SimSuspendMailboxOp, sim::SimSuspendSemaphoreOp,
      sim::SimSuspendObserveOp, sim::SimSuspendForeverOp,
      sim::SimSuspendAwaitOp, sim::SimSuspendJoinOp, sim::SimSuspendChildrenOp>(
      operation);
}

/// Build an AOT-only, non-suspending activation body while the original CFG
/// and its typed wait are still intact.  The original actor remains the
/// coroutine/fallback identity.  This is deliberately broad in eval mode: it
/// is the experiment's Verilator-shaped executable body, not a production
/// profitability decision.
LogicalResult
materializeStandaloneEvalBody(sim::SimDesignOp design, SymbolTable &symbols,
                              CodeUnitIndex &codeUnits, sim::SimFuncOp function,
                              const DenseSet<uint64_t> &controlTargets,
                              bool foreignControl) {
  if (function.isExternal() ||
      ::obelisk::schedule::has<::obelisk::schedule::Field::EvalBody>(
          function) ||
      ::obelisk::schedule::has<
          ::obelisk::schedule::Field::EvalBorrowedCaptures>(function))
    return success();
  bool portMethod = function.getEntryKind() == sim::EntryKind::PortInput ||
                    function.getEntryKind() == sim::EntryKind::PortOutput;
  bool eventDrivenInitial = function.getEntryKind() == sim::EntryKind::Initial;
  bool combinationalProcedure =
      function.getEntryKind() == sim::EntryKind::AlwaysComb ||
      function.getEntryKind() == sim::EntryKind::AlwaysLatch;
  bool generatedRegionBody =
      ::obelisk::schedule::has<schedule::metadata::nativeRegionBody>(function);
  if (!isSupportedEntryKind(function.getEntryKind()) &&
      function.getEntryKind() != sim::EntryKind::Continuous && !portMethod &&
      !eventDrivenInitial && !generatedRegionBody && !combinationalProcedure)
    return success();
  Block *wait = nullptr;
  unsigned suspensionCount = 0;
  function.walk([&](Operation *operation) {
    if (!isTypedSuspend(operation))
      return;
    ++suspensionCount;
    if (isEvalDirectWait(operation))
      wait = operation->getBlock();
  });
  if (suspensionCount != 1 || !wait || wait->getTerminator() == nullptr ||
      !isEvalDirectWait(wait->getTerminator()) || wait->getNumSuccessors() != 1)
    return success();

  Block *activation = wait->getSuccessor(0);
  for (sim::SimSpawnOp spawn : activation->getOps<sim::SimSpawnOp>()) {
    sim::SimFuncOp target = symbols.lookup<sim::SimFuncOp>(spawn.getCallee());
    // IEEE 1800-2017 31.9.1 transport monitors must hand this activation to
    // the generic scheduler so the one-shot commit registers its calendar
    // delay before AOT may advance time.  Cloning the activation as an eval
    // body would both bypass that boundary and duplicate the certified spawn.
    if (analysis::isNegativeTimingDelayMonitorSpawn(spawn, target))
      return success();
  }
  Block &sourceEntry = function.getBody().front();
  bool clockedControl =
      ::obelisk::schedule::has<schedule::metadata::clockedControl>(function);
  SmallVector<Block *> preambleBlocks;
  llvm::SmallPtrSet<Block *, 8> preambleSeen;
  Block *preamble = &sourceEntry;
  // An always_comb procedure runs its activation once at time zero (IEEE
  // 1800-2023 9.2.2.2, 9.2.2.2.2), so its entry path leads into that
  // activation. Canonicalization folds an argument-free activation block that
  // only forwards constants (`^activation: br ^loop(%c0)`) out of the entry
  // path, leaving the entry to branch to `^loop(%c0)` directly. That branch
  // executes exactly what a branch to the forwarding block would, so it
  // reaches the activation. The eval body repeats the preamble on every
  // activation, so this holds only while the preamble itself has no effect.
  auto sameConstant = [](Value lhs, Value rhs) {
    if (lhs == rhs)
      return true;
    auto left = lhs.getDefiningOp<arith::ConstantOp>();
    auto right = rhs.getDefiningOp<arith::ConstantOp>();
    return left && right && left.getValue() == right.getValue();
  };
  auto forwardsLikeActivation = [&](cf::BranchOp branch) {
    auto forward = dyn_cast<cf::BranchOp>(activation->getTerminator());
    if (!forward || activation->getNumArguments() != 0 ||
        forward.getDest() != branch.getDest() ||
        !llvm::all_of(activation->without_terminator(),
                      [](Operation &op) { return isa<arith::ConstantOp>(op); }))
      return false;
    return llvm::all_of(
        llvm::zip_equal(branch.getDestOperands(), forward.getDestOperands()),
        [&](auto pair) {
          return sameConstant(std::get<0>(pair), std::get<1>(pair));
        });
  };
  Block *forwardedPreamble = nullptr;
  bool purePreamble = true;
  while (!clockedControl && preamble != wait && preamble != activation) {
    if (!preambleSeen.insert(preamble).second)
      return success();
    auto branch = dyn_cast<cf::BranchOp>(preamble->getTerminator());
    if (!branch || branch.getDest()->getNumArguments() !=
                       branch.getDestOperands().size()) {
      return success();
    }
    preambleBlocks.push_back(preamble);
    purePreamble &=
        llvm::all_of(preamble->without_terminator(), [](Operation &operation) {
          return isMemoryEffectFree(&operation) ||
                 isa<sim::SimCoveragePointHitOp>(operation);
        });
    if (purePreamble && forwardsLikeActivation(branch)) {
      forwardedPreamble = preamble;
      preamble = activation;
      break;
    }
    preamble = branch.getDest();
  }
  bool startsAtActivation = preamble == activation;

  SmallString<48> evalBase;
  (function.getSymName() + ".__obelisk_eval_body").toVector(evalBase);
  unsigned evalCounter = 0;
  SmallString<48> evalName = SymbolTable::generateSymbolName<48>(
      evalBase,
      [&](StringRef candidate) { return symbols.lookup(candidate) != nullptr; },
      evalCounter);
  OpBuilder builder = OpBuilder::atBlockEnd(&design.getBody().front());
  uint64_t evalScope = codeUnits.getScope(function).value_or(0);
  uint64_t evalCodeUnit = codeUnits.allocate(evalScope);
  sim::SimCodeUnitDeclOp evalDeclaration = sim::SimCodeUnitDeclOp::create(
      builder, function.getLoc(), evalCodeUnit, evalScope,
      sim::EntryKind::Function, builder.getStringAttr(evalName),
      builder.getStringAttr("generated native eval body"),
      builder.getUnitAttr());
  SmallVector<NamedAttribute> evalAttributes{builder.getNamedAttr(
      "code_unit_id", builder.getI64IntegerAttr(evalCodeUnit))};
  SmallVector<DictionaryAttr> argumentAttrs;
  for (BlockArgument argument : sourceEntry.getArguments())
    argumentAttrs.push_back(function.getArgAttrDict(argument.getArgNumber()));
  sim::SimFuncOp evalBody = sim::SimFuncOp::create(
      builder, function.getLoc(), evalName,
      FunctionType::get(design.getContext(),
                        function.getFunctionType().getInputs(), TypeRange{}),
      sim::EntryKind::Function, evalAttributes, argumentAttrs);
  symbols.insert(evalBody);
  // Every path that abandons the clone below erases both halves: a code-unit
  // declaration naming a body that was never materialized would outlive the
  // rejection and describe a symbol the design does not contain.
  auto abandon = [&] {
    symbols.erase(evalBody);
    codeUnits.erase(evalDeclaration.getId());
    evalDeclaration.erase();
  };
  ::obelisk::schedule::set<::obelisk::schedule::Field::EvalBorrowedCaptures>(
      evalBody, builder.getUnitAttr());
  ::obelisk::schedule::set<::obelisk::schedule::Field::EvalRawCaptures>(
      evalBody, builder.getUnitAttr());
  if (auto owners = ::obelisk::schedule::get<
          ::obelisk::schedule::Field::EvalSourceOwners>(function))
    ::obelisk::schedule::set<::obelisk::schedule::Field::EvalSourceOwners>(
        evalBody, owners);
  if (auto group =
          ::obelisk::schedule::get<::obelisk::schedule::Field::EvalFusionGroup>(
              function))
    ::obelisk::schedule::set<::obelisk::schedule::Field::EvalFusionGroup>(
        evalBody, group);
  schedule::ContinuationSiteAttr activationSite;
  if (auto suspend = dyn_cast<sim::SimSuspendChangeOp>(wait->getTerminator()))
    activationSite = suspend.getSiteAttr();
  else if (auto suspend =
               dyn_cast<sim::SimSuspendEdgeOp>(wait->getTerminator()))
    activationSite = suspend.getSiteAttr();
  else if (auto suspend = dyn_cast<sim::SimSuspendAnyOp>(wait->getTerminator()))
    activationSite = suspend.getSiteAttr();
  else if (auto suspend =
               dyn_cast<sim::SimSuspendObserveOp>(wait->getTerminator()))
    activationSite = suspend.getSiteAttr();
  if (!activationSite || activationSite.getId() == 0) {
    abandon();
    return success();
  }
  ::obelisk::schedule::set<::obelisk::schedule::Field::EvalContinuation>(
      evalBody, builder.getI32IntegerAttr(activationSite.getId()));
  // Carry a typed source identity from the first eval-body clone onward.
  // Later body/module-instance fusion can then combine continuations without
  // preserving graph-generation-specific fragment ordinals.
  if (IntegerAttr codeUnit = function.getCodeUnitIdAttr())
    ::obelisk::schedule::set<::obelisk::schedule::Field::EvalSourceOwners>(
        evalBody, builder.getArrayAttr({schedule::SourceOwnerAttr::get(
                      builder.getContext(), codeUnit,
                      builder.getI32IntegerAttr(activationSite.getId()))}));
  SymbolTable::setSymbolVisibility(evalBody, SymbolTable::Visibility::Private);

  IRMapping mapping;
  Block &evalEntry = evalBody.getBody().front();
  for (auto [source, destination] :
       llvm::zip_equal(sourceEntry.getArguments(), evalEntry.getArguments()))
    mapping.map(source, destination);
  builder.setInsertionPointToStart(&evalEntry);
  if (clockedControl) {
    // Startup can branch and perform cold services (plusargs, dump setup).
    // Only rematerialize pure capture-derived entry values. Clock-control
    // phase and counters have already been made canonical storage, so their
    // contents must be loaded by the activation, never recomputed at entry.
    for (Operation &operation : sourceEntry.without_terminator())
      if (operation.getNumRegions() == 0 && isMemoryEffectFree(&operation) &&
          llvm::all_of(operation.getOperands(),
                       [&](Value value) { return mapping.contains(value); }))
        builder.clone(operation, mapping);
  }
  SmallVector<Value> activationEntryOperands;
  auto appendMappedValues = [&](ValueRange values,
                                SmallVectorImpl<Value> &mappedValues) {
    for (Value value : values) {
      Value mapped = mapping.lookupOrNull(value);
      if (!mapped)
        return failure();
      mappedValues.push_back(mapped);
    }
    return success();
  };
  for (Block *source : preambleBlocks) {
    for (Operation &operation : source->without_terminator())
      if (!isa<sim::SimCoveragePointHitOp>(operation))
        builder.clone(operation, mapping);
    // The activation block itself is cloned and entered below; it takes no
    // arguments.
    if (source == forwardedPreamble)
      continue;
    auto branch = cast<cf::BranchOp>(source->getTerminator());
    if (startsAtActivation && branch.getDest() == activation) {
      if (failed(appendMappedValues(branch.getDestOperands(),
                                    activationEntryOperands))) {
        abandon();
        return success();
      }
    } else {
      for (auto [argument, value] : llvm::zip_equal(
               branch.getDest()->getArguments(), branch.getDestOperands())) {
        Value mapped = mapping.lookupOrNull(value);
        if (!mapped) {
          abandon();
          return success();
        }
        mapping.map(argument, mapped);
      }
    }
  }
  bool cloneTerminalWait =
      function.getEntryKind() == sim::EntryKind::Continuous || portMethod ||
      combinationalProcedure;
  SmallVector<Block *> activationBlocks;
  SmallVector<Block *> pending{activation};
  llvm::SmallPtrSet<Block *, 32> seen;
  while (!pending.empty()) {
    Block *source = pending.pop_back_val();
    if ((!cloneTerminalWait && source == wait) || source == &sourceEntry ||
        !seen.insert(source).second)
      continue;
    activationBlocks.push_back(source);
    // The suspension block is part of the activation: continuous assignments
    // commonly compute and drive their result immediately before suspend.any.
    // Clone those operations below, but do not follow the resume edge back
    // into the next activation.
    if (source == wait)
      continue;
    for (Block *successor : source->getSuccessors())
      pending.push_back(successor);
  }
  if (activationBlocks.empty()) {
    abandon();
    return success();
  }
  if (clockedControl) {
    // Reject any startup-produced value that was not explicitly lifted or
    // safely rematerialized. Never leave an operand pointing into the actor.
    for (Block *block : activationBlocks)
      for (Operation &operation : *block) {
        bool capturesStartup = false;
        operation.walk([&](Operation *nested) {
          for (Value operand : nested->getOperands()) {
            Block *owner = operand.getParentBlock();
            if (owner->getParent() == &function.getBody() &&
                !seen.contains(owner) && !mapping.contains(operand))
              capturesStartup = true;
          }
        });
        if (capturesStartup) {
          abandon();
          return success();
        }
      }
  }
  for (Block *source : activationBlocks) {
    Block *destination = new Block;
    evalBody.getBody().push_back(destination);
    mapping.map(source, destination);
    for (BlockArgument argument : source->getArguments())
      mapping.map(argument, destination->addArgument(argument.getType(),
                                                     argument.getLoc()));
  }

  SmallVector<Value> entryOperands;
  if (startsAtActivation) {
    entryOperands = std::move(activationEntryOperands);
  } else {
    auto forwarded = cast<BranchOpInterface>(wait->getTerminator())
                         .getSuccessorOperands(0)
                         .getForwardedOperands();
    // Values produced in the suspension block represent coroutine state that
    // was sampled before the wait (for example an intra-assignment event
    // control RHS). Recomputing them in the zero-time activation body would
    // sample after the event and change SystemVerilog semantics. Such actors
    // retain their coroutine identity instead of receiving an eval clone.
    if (failed(appendMappedValues(forwarded, entryOperands))) {
      abandon();
      return success();
    }
  }
  if (!startsAtActivation && !cloneTerminalWait)
    for (Operation &operation : wait->without_terminator())
      if (!isa<sim::SimCoveragePointHitOp>(operation))
        builder.clone(operation, mapping);
  builder.setInsertionPointToEnd(&evalEntry);
  cf::BranchOp::create(builder, function.getLoc(), mapping.lookup(activation),
                       entryOperands);

  bool supported = true;
  auto cloneWaitCoverage = [&](OpBuilder &atReturn) {
    for (auto hit : wait->getOps<sim::SimCoveragePointHitOp>())
      atReturn.clone(*hit.getOperation(), mapping);
  };
  for (Block *source : activationBlocks) {
    builder.setInsertionPointToEnd(mapping.lookup(source));
    for (Operation &operation : *source) {
      if (isTypedSuspend(&operation) && &operation != source->getTerminator()) {
        supported = false;
        break;
      }
      // IEEE 1800-2017 13.2: a task may contain time-controlling statements,
      // and a function cannot enable a task. The eval body is a zero-time
      // function entry, so an activation that calls a task keeps its
      // coroutine identity instead.
      if (isa<sim::SimTaskCallOp, sim::SimClassVirtualTaskCallOp>(operation)) {
        supported = false;
        break;
      }
      if (&operation == source->getTerminator()) {
        if (isTypedSuspend(&operation)) {
          sim::SimReturnOp::create(builder, operation.getLoc(), ValueRange{});
          continue;
        }
        if (auto branch = dyn_cast<cf::BranchOp>(operation);
            branch && branch.getDest() == wait) {
          if (!cloneTerminalWait) {
            cloneWaitCoverage(builder);
            sim::SimReturnOp::create(builder, branch.getLoc(), ValueRange{});
            continue;
          }
          SmallVector<Value> operands;
          for (Value value : branch.getDestOperands())
            operands.push_back(mapping.lookup(value));
          cf::BranchOp::create(builder, branch.getLoc(), mapping.lookup(wait),
                               operands);
          continue;
        }
        if (auto branch = dyn_cast<cf::CondBranchOp>(operation)) {
          bool trueWait = branch.getTrueDest() == wait;
          bool falseWait = branch.getFalseDest() == wait;
          if (trueWait || falseWait) {
            if (!cloneTerminalWait) {
              if (trueWait && falseWait) {
                cloneWaitCoverage(builder);
                sim::SimReturnOp::create(builder, branch.getLoc(),
                                         ValueRange{});
                continue;
              }
              Block *returnBlock = new Block;
              evalBody.getBody().push_back(returnBlock);
              OpBuilder returnBuilder = OpBuilder::atBlockEnd(returnBlock);
              cloneWaitCoverage(returnBuilder);
              sim::SimReturnOp::create(returnBuilder, branch.getLoc(),
                                       ValueRange{});
              SmallVector<Value> trueOperands;
              SmallVector<Value> falseOperands;
              for (Value value : branch.getTrueDestOperands())
                trueOperands.push_back(mapping.lookup(value));
              for (Value value : branch.getFalseDestOperands())
                falseOperands.push_back(mapping.lookup(value));
              cf::CondBranchOp::create(
                  builder, branch.getLoc(),
                  mapping.lookup(branch.getCondition()),
                  trueWait ? returnBlock : mapping.lookup(branch.getTrueDest()),
                  trueWait ? ValueRange{} : ValueRange{trueOperands},
                  falseWait ? returnBlock
                            : mapping.lookup(branch.getFalseDest()),
                  falseWait ? ValueRange{} : ValueRange{falseOperands});
              continue;
            }
            SmallVector<Value> trueOperands;
            SmallVector<Value> falseOperands;
            for (Value value : branch.getTrueDestOperands())
              trueOperands.push_back(mapping.lookup(value));
            for (Value value : branch.getFalseDestOperands())
              falseOperands.push_back(mapping.lookup(value));
            cf::CondBranchOp::create(
                builder, branch.getLoc(), mapping.lookup(branch.getCondition()),
                trueWait ? mapping.lookup(wait)
                         : mapping.lookup(branch.getTrueDest()),
                ValueRange{trueOperands},
                falseWait ? mapping.lookup(wait)
                          : mapping.lookup(branch.getFalseDest()),
                ValueRange{falseOperands});
            continue;
          }
        }
        if (llvm::is_contained(operation.getSuccessors(), wait)) {
          supported = false;
          break;
        }
      }
      builder.clone(operation, mapping);
    }
    if (!supported)
      break;
  }
  if (!supported) {
    abandon();
    return success();
  }
  // The canonical actor keeps its named-block activations for runtime
  // execution. In a zero-time eval body, an enter/leave-only activation is
  // unobservable when no language disable names it and no foreign code can
  // target it. Do not bring these scheduler calls into the closed evaluator.
  // Boundaries, escaping tokens, and targeted scopes retain their identity.
  if (!foreignControl) {
    SmallVector<sim::SimControlEnterOp> unusedControls;
    evalBody.walk([&](sim::SimControlEnterOp enter) {
      if (!controlTargets.contains(enter.getTargetId()) &&
          llvm::all_of(enter.getControl().getUsers(), [](Operation *user) {
            return isa<sim::SimControlLeaveOp>(user);
          }))
        unusedControls.push_back(enter);
    });
    for (sim::SimControlEnterOp enter : unusedControls) {
      for (Operation *leave :
           llvm::make_early_inc_range(enter.getControl().getUsers()))
        leave->erase();
      enter.erase();
    }
  }
  preserveEvalNBASiteOrigins(evalBody);
  ::obelisk::schedule::set<::obelisk::schedule::Field::EvalBody>(
      function, FlatSymbolRefAttr::get(evalBody.getSymNameAttr()));
  return success();
}

bool hasOnlyPureEntryPreamble(sim::SimFuncOp function, Block *wait) {
  Block &entry = function.getBody().front();
  auto branch = dyn_cast<cf::BranchOp>(entry.getTerminator());
  if (!branch || branch.getDest() != wait ||
      branch.getDestOperands().size() != wait->getNumArguments())
    return false;
  return llvm::all_of(entry.without_terminator(), [](Operation &operation) {
    return isMemoryEffectFree(&operation) ||
           isa<sim::SimCoveragePointHitOp>(operation);
  });
}

// Threading a process CFG turns invariant captures into block arguments. They
// can be reconstructed in a group regardless of which scheduler owns the
// surrounding design, but loop-carried state cannot be replaced by its entry
// value. Follow every incoming CFG edge (including both arms from the same
// predecessor) and require the same SSA root. Cycles of forwarding arguments
// are harmless; arithmetic updates, reloads and unresolved operands are not.
bool hasInvariantWaitArguments(BodyFusionCandidate &candidate) {
  Block &entry = candidate.function.getBody().front();
  if (candidate.body->getNumArguments() != 0 &&
      llvm::any_of(candidate.body->getPredecessors(), [&](Block *predecessor) {
        return predecessor != candidate.wait;
      }))
    return false;
  for (auto [argument, initial] : llvm::zip_equal(
           candidate.wait->getArguments(), candidate.threadedEntryValues)) {
    SmallVector<Value> pending{argument};
    llvm::SmallDenseSet<Value, 16> visited;
    bool reachedInitial = false;
    while (!pending.empty()) {
      Value value = pending.pop_back_val();
      if (value == initial) {
        reachedInitial = true;
        continue;
      }
      if (!visited.insert(value).second)
        continue;
      auto forwarded = dyn_cast<BlockArgument>(value);
      if (!forwarded || forwarded.getOwner() == &entry)
        return false;
      Block *block = forwarded.getOwner();
      if (block->hasNoPredecessors())
        return false;
      llvm::SmallPtrSet<Block *, 8> predecessors;
      for (Block *predecessor : block->getPredecessors()) {
        if (!predecessors.insert(predecessor).second)
          continue;
        auto branch = dyn_cast<BranchOpInterface>(predecessor->getTerminator());
        if (!branch)
          return false;
        for (unsigned index = 0; index < branch->getNumSuccessors(); ++index) {
          if (branch->getSuccessor(index) != block)
            continue;
          SuccessorOperands operands = branch.getSuccessorOperands(index);
          unsigned lane = forwarded.getArgNumber();
          if (lane >= operands.size() || operands.isOperandProduced(lane))
            return false;
          pending.push_back(operands[lane]);
        }
      }
    }
    if (!reachedInitial)
      return false;
  }
  return true;
}

bool collectBodyBlocks(BodyFusionCandidate &candidate) {
  SmallVector<Block *> pending{candidate.body};
  llvm::SmallPtrSet<Block *, 16> visited;
  while (!pending.empty()) {
    Block *block = pending.pop_back_val();
    if (block == candidate.wait || !visited.insert(block).second)
      continue;
    if (block == &candidate.function.getBody().front())
      return false;
    candidate.bodyBlocks.push_back(block);
    Operation *terminator = block->getTerminator();
    if (!isa<BranchOpInterface, sim::SimReturnOp>(terminator))
      return false;
    for (Block *successor : terminator->getSuccessors()) {
      if (successor == &candidate.function.getBody().front())
        return false;
      if (successor != candidate.wait)
        pending.push_back(successor);
    }
  }
  if (candidate.bodyBlocks.size() + 2 !=
      candidate.function.getBody().getBlocks().size())
    return false;
  llvm::sort(candidate.bodyBlocks, [&](Block *lhs, Block *rhs) {
    return std::distance(candidate.function.getBody().begin(),
                         Region::iterator(lhs)) <
           std::distance(candidate.function.getBody().begin(),
                         Region::iterator(rhs));
  });
  return true;
}

void collectLiveEntryPreamble(BodyFusionCandidate &candidate) {
  Block &entry = candidate.function.getBody().front();
  llvm::SmallPtrSet<Operation *, 16> needed;
  SmallVector<Value> pending;
  for (Operation &operation : entry.without_terminator())
    if (isa<sim::SimCoveragePointHitOp>(operation)) {
      needed.insert(&operation);
      llvm::append_range(pending, operation.getOperands());
    }
  pending.append(candidate.threadedEntryValues.begin(),
                 candidate.threadedEntryValues.end());
  pending.append(candidate.wait->getTerminator()->operand_begin(),
                 candidate.wait->getTerminator()->operand_end());
  for (Block *block : candidate.bodyBlocks)
    for (Operation &operation : *block)
      pending.append(operation.operand_begin(), operation.operand_end());
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
      candidate.entryPreamble.push_back(&operation);
}

bool hasOnlyTerminationReturns(const BodyFusionCandidate &candidate) {
  for (Block *block : candidate.bodyBlocks) {
    if (!isa<sim::SimReturnOp>(block->getTerminator()))
      continue;
    bool hasPredecessor = false;
    for (Block *predecessor : block->getPredecessors()) {
      hasPredecessor = true;
      auto branch = dyn_cast<cf::CondBranchOp>(predecessor->getTerminator());
      if (!branch || branch.getTrueDest() != block ||
          !branch.getCondition()
               .getDefiningOp<sim::SimTerminationRequestedOp>())
        return false;
    }
    if (!hasPredecessor)
      return false;
  }
  return true;
}

schedule::ComputeEffectAttr
getDirectSensitivity(schedule::ComputeFragmentAttr fragment) {
  schedule::ComputeEffectAttr sensitivity;
  for (Attribute attribute : fragment.getEffects()) {
    auto effect = cast<schedule::ComputeEffectAttr>(attribute);
    if (effect.getEffect() != schedule::ComputeEffectKind::Watch)
      continue;
    if (sensitivity)
      return {};
    sensitivity = effect;
  }
  return sensitivity;
}

/// Fold
///
///   enqueue %first to %destination
///   cond_br %condition, ^overwrite, ^continue
/// ^overwrite:
///   %second = <speculatable computation>
///   enqueue %second to %destination
///   br ^continue
///
/// to one unconditional enqueue of `select %condition, %second, %first`.
///
/// Both writes are separate update events (IEEE 1800-2017 4.6(b), 10.4.2):
/// when the condition holds, an event control can see the first value before
/// the second replaces it. Fold only a root whose intermediate NBA values are
/// unobservable (analysis::NBAMergeSafety). Restrict this to adjacent
/// accumulator sites for the same commit root and to a speculatable,
/// side-effect-free overwrite arm. Besides removing a hot branch, the resulting
/// straight-line arithmetic is suitable for downstream SLP/vector formation.
uint64_t
ifConvertConditionalNBAWrites(sim::SimFuncOp function, Block *protectedWait,
                              const analysis::NBAMergeSafety &mergeSafety) {
  uint64_t converted = 0;
  bool changed;
  do {
    changed = false;
    for (Block &source : function.getBody()) {
      auto conditional = dyn_cast<cf::CondBranchOp>(source.getTerminator());
      if (!conditional || !conditional.getTrueDestOperands().empty() ||
          !conditional.getFalseDestOperands().empty())
        continue;
      Block *overwrite = conditional.getTrueDest();
      Block *continuation = conditional.getFalseDest();
      if (overwrite == continuation || overwrite == protectedWait ||
          !llvm::hasSingleElement(overwrite->getPredecessors()) ||
          overwrite->getNumArguments() != 0)
        continue;
      auto join = dyn_cast<cf::BranchOp>(overwrite->getTerminator());
      if (!join || join.getDest() != continuation ||
          !join.getDestOperands().empty())
        continue;

      sim::SimNBAEnqueueOp first;
      for (Operation &operation : llvm::reverse(source.without_terminator())) {
        if (auto enqueue = dyn_cast<sim::SimNBAEnqueueOp>(operation)) {
          first = enqueue;
          break;
        }
      }
      if (!first || first.getClockingOutputAttr() || first.getDelay())
        continue;
      bool safeTail = true;
      for (Operation *operation = first->getNextNode();
           operation && operation != source.getTerminator();
           operation = operation->getNextNode())
        safeTail &=
            isa<sim::SimRefLoadOp>(operation) ||
            (isMemoryEffectFree(operation) && isSpeculatable(operation));
      if (!safeTail)
        continue;

      sim::SimNBAEnqueueOp second;
      bool safeOverwrite = true;
      for (Operation &operation : overwrite->without_terminator()) {
        if (auto enqueue = dyn_cast<sim::SimNBAEnqueueOp>(operation)) {
          if (second)
            safeOverwrite = false;
          second = enqueue;
          continue;
        }
        safeOverwrite &=
            isMemoryEffectFree(&operation) && isSpeculatable(&operation);
      }
      if (!safeOverwrite || !second || second.getClockingOutputAttr() ||
          second.getDelay() ||
          first.getDestination() != second.getDestination() ||
          first.getValue().getType() != second.getValue().getType())
        continue;
      schedule::NBASiteAttr firstSite = first.getSiteAttr();
      schedule::NBASiteAttr secondSite = second.getSiteAttr();
      if (!firstSite || !secondSite || firstSite.getTiming() ||
          secondSite.getTiming() ||
          firstSite.getStorage() !=
              schedule::ComputeNBAStorageKind::RootAccumulator ||
          secondSite.getStorage() !=
              schedule::ComputeNBAStorageKind::RootAccumulator ||
          firstSite.getCommit() != secondSite.getCommit() ||
          !mergeSafety.commitMayMerge(firstSite.getCommit()))
        continue;

      // Move only the proven-speculatable value computation. The replacement
      // enqueue retains the later site's identity, which is the observable
      // last-write position in the static NBA plan.
      for (Operation &operation :
           llvm::make_early_inc_range(overwrite->without_terminator()))
        if (&operation != second.getOperation())
          operation.moveBefore(conditional);
      OpBuilder builder(conditional);
      Value selected = arith::SelectOp::create(
          builder, conditional.getLoc(), conditional.getCondition(),
          second.getValue(), first.getValue());
      sim::SimNBAEnqueueOp::create(builder, second.getLoc(), selected,
                                   second.getDestination(), Value{}, secondSite,
                                   IntegerAttr{});
      first.erase();
      second.erase();
      cf::BranchOp::create(builder, conditional.getLoc(), continuation);
      conditional.erase();
      overwrite->erase();

      // Join the now-single-predecessor continuation locally. Running the
      // generic canonicalizer here would CSE rematerialized constants across
      // the coroutine suspension and incorrectly force non-frameable values
      // into the process frame.
      if (continuation != protectedWait &&
          llvm::hasSingleElement(continuation->getPredecessors()) &&
          continuation->getNumArguments() == 0) {
        cast<cf::BranchOp>(source.getTerminator()).erase();
        source.getOperations().splice(source.end(),
                                      continuation->getOperations());
        continuation->erase();
      }
      ++converted;
      changed = true;
      break;
    }
  } while (changed);
  return converted;
}

std::optional<uint64_t> resolveStorageRoot(Value value) {
  llvm::SmallDenseSet<Value, 8> visited;
  while (value && visited.insert(value).second) {
    if (auto argument = dyn_cast<BlockArgument>(value)) {
      auto function =
          dyn_cast<sim::SimFuncOp>(argument.getOwner()->getParentOp());
      if (!function || argument.getOwner() != &function.getBody().front())
        return std::nullopt;
      auto descriptor = function.getArgAttrOfType<IntegerAttr>(
          argument.getArgNumber(), sim::metadata::descriptorId);
      if (!descriptor || descriptor.getValue().isNegative() ||
          descriptor.getValue().getBitWidth() > 64)
        return std::nullopt;
      return descriptor.getValue().getZExtValue();
    }
    Operation *definition = value.getDefiningOp();
    if (auto context = dyn_cast_or_null<sim::SimContextStorageOp>(definition))
      return context.getId();
    if (auto view = dyn_cast_or_null<sim::SimRefExtractOp>(definition))
      value = view.getInput();
    else if (auto view = dyn_cast_or_null<sim::SimRefDynExtractOp>(definition))
      value = view.getInput();
    else if (auto view = dyn_cast_or_null<sim::SimRefSubelementOp>(definition))
      value = view.getInput();
    else if (auto view =
                 dyn_cast_or_null<sim::SimRefArrayElementOp>(definition))
      value = view.getInput();
    else
      return std::nullopt;
  }
  return std::nullopt;
}

struct ExactDriverSlice {
  uint64_t id;
  uint64_t lowBit;
};

/// Resolve a driver slice before CFG capture threading obscures its static
/// provenance. The native backend validates the frozen ID/range against its
/// independently-computed layout before using it as a resolution bound.
std::optional<ExactDriverSlice> resolveExactDriverSlice(Value value) {
  uint64_t lowBit = 0;
  llvm::SmallDenseSet<Value, 8> visited;
  while (value && visited.insert(value).second) {
    if (auto argument = dyn_cast<BlockArgument>(value)) {
      auto function =
          dyn_cast<sim::SimFuncOp>(argument.getOwner()->getParentOp());
      if (!function || argument.getOwner() != &function.getBody().front())
        return std::nullopt;
      auto descriptor = function.getArgAttrOfType<IntegerAttr>(
          argument.getArgNumber(), sim::metadata::descriptorId);
      if (!descriptor || descriptor.getValue().isNegative() ||
          descriptor.getValue().getBitWidth() > 64)
        return std::nullopt;
      return ExactDriverSlice{descriptor.getValue().getZExtValue(), lowBit};
    }
    Operation *definition = value.getDefiningOp();
    if (auto context = dyn_cast_or_null<sim::SimContextDriverOp>(definition))
      return ExactDriverSlice{context.getId(), lowBit};
    if (auto extract = dyn_cast_or_null<sim::SimDriverExtractOp>(definition)) {
      if (extract.getLowBit() > std::numeric_limits<uint64_t>::max() - lowBit)
        return std::nullopt;
      lowBit += extract.getLowBit();
      value = extract.getInput();
      continue;
    }
    if (auto subelement =
            dyn_cast_or_null<sim::SimDriverSubelementOp>(definition)) {
      Type current = subelement.getInput().getType().getElementType();
      for (int64_t rawIndex : subelement.getIndices()) {
        if (rawIndex < 0 || static_cast<uint64_t>(rawIndex) >=
                                sim::getAggregateNumElements(current))
          return std::nullopt;
        auto child = sim::getAggregateProvenanceSubelement(
            current, static_cast<unsigned>(rawIndex));
        if (!child ||
            child->first > std::numeric_limits<uint64_t>::max() - lowBit)
          return std::nullopt;
        lowBit += child->first;
        current = sim::getAggregateElementType(current,
                                               static_cast<unsigned>(rawIndex));
      }
      value = subelement.getInput();
      continue;
    }
    return std::nullopt;
  }
  return std::nullopt;
}

/// Design-wide identity/escape facts for private static storage. Keep each
/// function's contribution so replacing a cohort does not rescan the design.
/// Remove contributions before erasing functions, and refresh rewritten
/// callers and new bodies before asking for an exclusivity proof.
struct PrivateStaticAccessIndex {
  struct FunctionFacts {
    llvm::SmallDenseSet<uint64_t, 8> accesses;
    llvm::SmallDenseSet<uint64_t, 8> unsupported;
    SmallVector<std::pair<uint64_t, StringAttr>> spawns;
  };
  DenseMap<uint64_t, sim::SimStorageDeclOp> declarations;
  DenseMap<uint64_t, llvm::SmallPtrSet<Operation *, 2>> accessors;
  DenseMap<uint64_t, DenseMap<StringAttr, unsigned>> spawnTargets;
  DenseMap<uint64_t, unsigned> unsupportedUses;
  DenseMap<Operation *, FunctionFacts> contributions;

  explicit PrivateStaticAccessIndex(sim::SimDesignOp design) {
    for (sim::SimStorageDeclOp declaration :
         design.getBody().front().getOps<sim::SimStorageDeclOp>())
      declarations.try_emplace(declaration.getId(), declaration);
    for (sim::SimFuncOp function :
         design.getBody().front().getOps<sim::SimFuncOp>())
      refresh(function);
  }

  void erase(sim::SimFuncOp function) {
    auto found = contributions.find(function.getOperation());
    if (found == contributions.end())
      return;
    for (uint64_t root : found->second.accesses) {
      auto owners = accessors.find(root);
      owners->second.erase(function.getOperation());
      if (owners->second.empty())
        accessors.erase(owners);
    }
    for (uint64_t root : found->second.unsupported) {
      auto uses = unsupportedUses.find(root);
      if (--uses->second == 0)
        unsupportedUses.erase(uses);
    }
    for (auto [root, target] : found->second.spawns) {
      auto targets = spawnTargets.find(root);
      auto count = targets->second.find(target);
      if (--count->second == 0)
        targets->second.erase(count);
      if (targets->second.empty())
        spawnTargets.erase(targets);
    }
    contributions.erase(found);
  }

  void refresh(sim::SimFuncOp function) {
    erase(function);
    FunctionFacts facts;
    function.walk([&](Operation *operation) {
      Value reference;
      if (auto load = dyn_cast<sim::SimRefLoadOp>(operation))
        reference = load.getReference();
      else if (auto store = dyn_cast<sim::SimRefStoreOp>(operation))
        reference = store.getReference();
      else {
        // Views are checked through their eventual users. A spawn can pass
        // a root to its exclusive accessor; any other reference consumer may
        // observe identity or state and prevents promotion.
        if (isa<sim::SimRefExtractOp, sim::SimRefDynExtractOp,
                sim::SimRefSubelementOp, sim::SimRefArrayElementOp>(operation))
          return;
        auto spawn = dyn_cast<sim::SimSpawnOp>(operation);
        for (Value operand : operation->getOperands()) {
          if (!isa<sim::RefType>(operand.getType()))
            continue;
          auto root = resolveStorageRoot(operand);
          if (!root)
            continue;
          if (!spawn) {
            facts.unsupported.insert(*root);
            continue;
          }
          StringAttr target = spawn.getCalleeAttr().getAttr();
          facts.spawns.emplace_back(*root, target);
        }
        return;
      }
      auto root = resolveStorageRoot(reference);
      if (!root)
        return;
      facts.accesses.insert(*root);
    });
    for (uint64_t root : facts.accesses)
      accessors[root].insert(function.getOperation());
    for (uint64_t root : facts.unsupported)
      ++unsupportedUses[root];
    for (auto [root, target] : facts.spawns)
      ++spawnTargets[root][target];
    contributions.try_emplace(function.getOperation(), std::move(facts));
  }

  bool isPrivateTo(uint64_t root, sim::SimFuncOp function) const {
    auto owners = accessors.find(root);
    if (unsupportedUses.contains(root) || owners == accessors.end() ||
        owners->second.size() != 1 ||
        !owners->second.contains(function.getOperation()))
      return false;
    auto spawn = spawnTargets.find(root);
    return spawn == spawnTargets.end() ||
           (spawn->second.size() == 1 &&
            spawn->second.contains(function.getSymNameAttr()));
  }
};

/// Promote a static procedure temporary when this fused function is its sole
/// executable accessor and one store dominates every read. Such a declaration
/// is state only because its source-level lifetime spans activations; if every
/// activation overwrites it before use, retaining the canonical store would
/// add a signal-transition publication with no observer or semantic consumer.
uint64_t
promotePrivateStaticTemporaries(sim::SimFuncOp function,
                                const PrivateStaticAccessIndex &accessIndex,
                                bool markDormantTier1 = false) {
  // A recursive or foreign reentry can overwrite a static temporary in this
  // same function. A dominating store and exclusive accessor identity alone
  // do not prove that its value survives a call. Retry after inlining rather
  // than forwarding across a call without an interprocedural reentry proof.
  if (function
          .walk([](Operation *operation) {
            return isa<sim::SimCallOp, sim::SimTaskCallOp,
                       sim::SimClassDirectCallOp, sim::SimClassVirtualCallOp,
                       sim::SimClassVirtualTaskCallOp, sim::SimDPICallOp,
                       sim::SimSpawnOp>(operation)
                       ? WalkResult::interrupt()
                       : WalkResult::advance();
          })
          .wasInterrupted())
    return 0;

  DenseMap<uint64_t, SmallVector<sim::SimRefLoadOp>> loads;
  DenseMap<uint64_t, SmallVector<sim::SimRefStoreOp>> stores;
  function.walk([&](Operation *operation) {
    Value reference;
    if (auto load = dyn_cast<sim::SimRefLoadOp>(operation))
      reference = load.getReference();
    else if (auto store = dyn_cast<sim::SimRefStoreOp>(operation))
      reference = store.getReference();
    else
      return;
    auto root = resolveStorageRoot(reference);
    if (!root)
      return;
    if (auto load = dyn_cast<sim::SimRefLoadOp>(operation))
      loads[*root].push_back(load);
    else
      stores[*root].push_back(cast<sim::SimRefStoreOp>(operation));
  });

  DominanceInfo dominance(function);
  auto staysInActivation = [](sim::SimRefStoreOp store,
                              sim::SimRefLoadOp load) {
    // Follow every incoming path back to its defining store. Dominance alone
    // permits a wait between store and load, during which another invocation
    // of this same actor/task could overwrite the shared static root.
    SmallVector<std::pair<Block *, Operation *>> worklist{
        {load->getBlock(), load->getPrevNode()}};
    llvm::SmallPtrSet<Block *, 8> visitedPredecessors;
    while (!worklist.empty()) {
      auto [block, cursor] = worklist.pop_back_val();
      bool defined = false;
      for (; cursor; cursor = cursor->getPrevNode()) {
        if (cursor == store.getOperation()) {
          defined = true;
          break;
        }
        if (isTypedSuspend(cursor))
          return false;
      }
      if (defined)
        continue;
      if (block->hasNoPredecessors())
        return false;
      for (Block *predecessor : block->getPredecessors())
        if (visitedPredecessors.insert(predecessor).second)
          worklist.emplace_back(predecessor, predecessor->getTerminator());
    }
    return true;
  };
  uint64_t promoted = 0;
  for (auto &[descriptor, rootStores] : stores) {
    auto declaration = accessIndex.declarations.lookup(descriptor);
    auto rootLoads = loads.find(descriptor);
    if (!declaration || rootLoads == loads.end() || rootStores.size() != 1 ||
        rootLoads->second.empty() ||
        !accessIndex.isPrivateTo(descriptor, function) ||
        declaration.getLifetime() != sim::Lifetime::Static ||
        declaration->hasAttr(sim::metadata::coverageToggleObservable))
      continue;
    std::optional<schedule::ComputeObservabilityKind> observability =
        declaration.getObservability();
    if (!observability ||
        *observability ==
            schedule::ComputeObservabilityKind::ExternallyWritable)
      continue;
    sim::SimRefStoreOp store = rootStores.front();
    Value rootReference = store.getReference();
    std::optional<unsigned> totalWidth =
        sim::getPackedWidth(store.getValue().getType());
    if (!totalWidth || *totalWidth == 0)
      continue;

    // Accept only a tree of static subelement views, loads, and the one
    // dominating store. This excludes escapes, NBA destinations, dynamic
    // indexing, and any use whose identity could be observed elsewhere.
    llvm::SetVector<Value> family;
    family.insert(rootReference);
    bool closed = true;
    for (size_t index = 0; index < family.size() && closed; ++index) {
      for (OpOperand &use : family[index].getUses()) {
        Operation *user = use.getOwner();
        if (auto view = dyn_cast<sim::SimRefSubelementOp>(user)) {
          family.insert(view.getResult());
          continue;
        }
        if (isa<sim::SimRefLoadOp>(user) || user == store.getOperation())
          continue;
        closed = false;
        break;
      }
    }
    if (!closed ||
        !llvm::all_of(rootLoads->second, [&](sim::SimRefLoadOp load) {
          return dominance.dominates(store.getOperation(),
                                     load.getOperation()) &&
                 family.contains(load.getReference()) &&
                 staysInActivation(store, load);
        }))
      continue;

    auto getPackedPath =
        [&](Value reference) -> std::optional<SmallVector<int64_t>> {
      SmallVector<sim::SimRefSubelementOp> path;
      Value current = reference;
      while (current != rootReference) {
        auto view = current.getDefiningOp<sim::SimRefSubelementOp>();
        if (!view)
          return std::nullopt;
        path.push_back(view);
        current = view.getInput();
      }
      SmallVector<int64_t> indices;
      Type type = store.getValue().getType();
      for (sim::SimRefSubelementOp view : llvm::reverse(path)) {
        for (int64_t index : view.getIndices()) {
          // Union reference views and active-member value extraction have
          // different rules. Keep them in storage until that equivalence is
          // proved, rather than treating a union like a struct.
          if (index < 0 || isa<sim::PackedUnionType>(type))
            return std::nullopt;
          type =
              sim::getAggregateElementType(type, static_cast<unsigned>(index));
          if (!type || !sim::getPackedWidth(type))
            return std::nullopt;
          indices.push_back(index);
        }
      }
      if (type != cast<sim::RefType>(reference.getType()).getElementType())
        return std::nullopt;
      return indices;
    };

    SmallVector<std::pair<sim::SimRefLoadOp, SmallVector<int64_t>>>
        replacements;
    bool representable = true;
    for (sim::SimRefLoadOp load : rootLoads->second) {
      auto selected = getPackedPath(load.getReference());
      if (!selected) {
        representable = false;
        break;
      }
      replacements.push_back({load, *selected});
    }
    if (!representable)
      continue;

    for (auto &[load, indices] : replacements) {
      OpBuilder builder(load);
      // Follow the same typed subelement path in SSA. Packed lowering retains
      // both value and unknown planes, including wide and mixed-domain
      // aggregates; integer shifts here would silently lose X/Z information.
      Value replacement = store.getValue();
      for (int64_t index : indices)
        replacement = sim::SimAggregateExtractOp::create(
            builder, load.getLoc(),
            sim::getAggregateElementType(replacement.getType(),
                                         static_cast<unsigned>(index)),
            replacement, index);
      load.getResult().replaceAllUsesWith(replacement);
      load.erase();
    }
    // Read-only VPI must observe the last procedural value at a safe point, so
    // retain its one canonical store while forwarding all intra-activation
    // loads from the dominating SSA value. Invisible state can discard both
    // the store and the now-dead reference-view family.
    if (*observability == schedule::ComputeObservabilityKind::Invisible) {
      store.erase();
      for (Value reference : llvm::reverse(family))
        if (Operation *definition = reference.getDefiningOp();
            definition && definition->use_empty())
          definition->erase();
    } else if (markDormantTier1)
      ::obelisk::schedule::set<schedule::metadata::evalDiscardableStore>(
          store, UnitAttr::get(function.getContext()));
    ++promoted;
  }
  return promoted;
}

/// Remove stores that are needed only by the canonical read-observable body
/// from dormant eval clones, then erase the transient marker everywhere.  The
/// marker is assigned only after the promotion proof has established that the
/// reference family is private and every activation forwards reads from the
/// dominating SSA value.
void finalizeDormantTier1Stores(sim::SimDesignOp design) {
  SmallVector<sim::SimFuncOp> evalBodies;
  for (sim::SimFuncOp function :
       design.getBody().front().getOps<sim::SimFuncOp>())
    if (::obelisk::schedule::has<
            ::obelisk::schedule::Field::EvalBorrowedCaptures>(function))
      evalBodies.push_back(function);

  for (sim::SimFuncOp evalBody : evalBodies) {
    SmallVector<sim::SimRefStoreOp> stores;
    evalBody.walk([&](sim::SimRefStoreOp store) {
      if (::obelisk::schedule::has<schedule::metadata::evalDiscardableStore>(
              store))
        stores.push_back(store);
    });
    for (sim::SimRefStoreOp store : stores)
      store.erase();

    // Match the ordinary Invisible-state cleanup structurally.  Loads were
    // already forwarded by the promotion proof, so iterating to a fixed point
    // removes every now-dead subelement view, including sibling load paths.
    bool changed = true;
    while (changed) {
      changed = false;
      SmallVector<Operation *> deadViews;
      evalBody.walk([&](Operation *operation) {
        if (isa<sim::SimContextStorageOp, sim::SimRefExtractOp,
                sim::SimRefDynExtractOp, sim::SimRefSubelementOp,
                sim::SimRefArrayElementOp>(operation) &&
            operation->getNumResults() == 1 && operation->use_empty())
          deadViews.push_back(operation);
      });
      for (Operation *operation : deadViews) {
        operation->erase();
        changed = true;
      }
    }
  }

  for (sim::SimFuncOp function :
       design.getBody().front().getOps<sim::SimFuncOp>()) {
    // Canonical function helpers are cloned into a private eval call closure
    // later. Keep their proof marker until that specialization consumes it;
    // every actor/coroutine canonical body is complete here and must not leak
    // the transient marker into emitted Simulation IR.
    if (!::obelisk::schedule::has<
            ::obelisk::schedule::Field::EvalBorrowedCaptures>(function) &&
        function.getEntryKind() == sim::EntryKind::Function)
      continue;
    function.walk([&](sim::SimRefStoreOp store) {
      ::obelisk::schedule::remove<schedule::metadata::evalDiscardableStore>(
          store);
    });
  }
}

/// Share branch conditions whose complete expression trees are structurally
/// identical and read only storage roots that this fused activation cannot
/// update immediately. NBA enqueues do not modify canonical state until the
/// barrier, so they do not invalidate such a condition.
uint64_t shareStableBranchConditions(sim::SimFuncOp function,
                                     Block *bodyEntry) {
  // A remaining call can mutate a captured root even when the fused body has
  // no direct store. Avoid hoisting loads across calls until interprocedural
  // mod/ref information is available here.
  bool hasCalls = false;
  function.walk([&](sim::SimCallOp) { hasCalls = true; });
  if (hasCalls)
    return 0;

  llvm::SmallDenseSet<uint64_t, 8> writtenRoots;
  function.walk([&](sim::SimRefStoreOp store) {
    if (std::optional<uint64_t> root = resolveStorageRoot(store.getReference()))
      writtenRoots.insert(*root);
  });

  llvm::DenseMap<Value, bool> stableCache;
  std::function<bool(Value)> isStable = [&](Value value) {
    if (isa<BlockArgument>(value))
      return true;
    if (auto cached = stableCache.find(value); cached != stableCache.end())
      return cached->second;
    Operation *definition = value.getDefiningOp();
    bool stable = false;
    if (auto load = dyn_cast_or_null<sim::SimRefLoadOp>(definition)) {
      std::optional<uint64_t> root = resolveStorageRoot(load.getReference());
      stable = root && !writtenRoots.contains(*root);
    } else if (definition && definition->getNumRegions() == 0 &&
               definition->getNumResults() == 1 &&
               isMemoryEffectFree(definition) && isSpeculatable(definition)) {
      stable = llvm::all_of(definition->getOperands(), isStable);
    }
    stableCache[value] = stable;
    return stable;
  };

  using ValuePair = std::pair<Value, Value>;
  llvm::DenseMap<ValuePair, bool> equivalentCache;
  std::function<bool(Value, Value)> equivalent = [&](Value lhs, Value rhs) {
    if (lhs == rhs)
      return true;
    ValuePair pair{lhs, rhs};
    if (auto cached = equivalentCache.find(pair);
        cached != equivalentCache.end())
      return cached->second;
    Operation *left = lhs.getDefiningOp();
    Operation *right = rhs.getDefiningOp();
    bool same = left && right && left->getName() == right->getName() &&
                left->getAttrs() == right->getAttrs() &&
                left->getResultTypes() == right->getResultTypes() &&
                left->getNumOperands() == right->getNumOperands();
    if (same) {
      if (auto leftLoad = dyn_cast<sim::SimRefLoadOp>(left)) {
        auto rightLoad = cast<sim::SimRefLoadOp>(right);
        same = leftLoad.getReference() == rightLoad.getReference();
      } else {
        for (auto [leftOperand, rightOperand] :
             llvm::zip_equal(left->getOperands(), right->getOperands()))
          same &= equivalent(leftOperand, rightOperand);
      }
    }
    equivalentCache[pair] = same;
    return same;
  };

  SmallVector<cf::CondBranchOp> branches;
  function.walk([&](cf::CondBranchOp branch) {
    if (isStable(branch.getCondition()))
      branches.push_back(branch);
  });
  SmallVector<SmallVector<cf::CondBranchOp>> groups;
  for (cf::CondBranchOp branch : branches) {
    auto group = llvm::find_if(groups, [&](auto &candidate) {
      return equivalent(candidate.front().getCondition(),
                        branch.getCondition());
    });
    if (group == groups.end())
      groups.push_back({branch});
    else
      group->push_back(branch);
  }

  uint64_t shared = 0;
  OpBuilder builder = OpBuilder::atBlockBegin(bodyEntry);
  for (auto &group : groups) {
    if (group.size() < 2)
      continue;
    IRMapping mapping;
    std::function<Value(Value)> cloneTree = [&](Value value) -> Value {
      if (isa<BlockArgument>(value))
        return value;
      if (Value mapped = mapping.lookupOrNull(value))
        return mapped;
      Operation *definition = value.getDefiningOp();
      for (Value operand : definition->getOperands())
        (void)cloneTree(operand);
      Operation *cloned = builder.clone(*definition, mapping);
      return cloned->getResult(0);
    };
    Value common = cloneTree(group.front().getCondition());
    for (cf::CondBranchOp branch : group)
      branch.getConditionMutable().assign(common);
    shared += group.size() - 1;
  }
  return shared;
}

FailureOr<sim::SimFuncOp> materializeStraightLineKernel(
    sim::SimDesignOp design, SymbolTable &symbols, CodeUnitIndex &codeUnits,
    FusionInputIndex &inputIndex, schedule::ComputeFusionAttr fusion,
    schedule::ComputeGraphAttr graph,
    const analysis::DescriptorProvenanceAnalysis &provenance,
    const CombinationalFusionAnalysis &combinational,
    const DenseMap<int64_t, int64_t> &resumeTargets,
    const DenseMap<StringAttr, SmallVector<sim::SimSpawnOp>> &spawnsByCallee) {
  struct Candidate {
    sim::SimFuncOp function;
    sim::SimSpawnOp spawn;
    Block *body;
    Operation *suspend;
    SmallVector<unsigned> fusedArguments;
    SmallVector<Value> initialState;
    SmallVector<Value> nextState;
    int64_t fragment;
    int64_t resumeTarget;
    std::optional<CombinationalFusionBody> combinationalBody;
  };
  SmallVector<Candidate, 4> candidates;
  for (int64_t member : fusion.getFragments().asArrayRef()) {
    if (member < 0 || static_cast<uint64_t>(member) >= graph.getNodes().size())
      return failure();
    auto fragment =
        dyn_cast<schedule::ComputeFragmentAttr>(graph.getNodes()[member]);
    if (!fragment)
      return failure();
    sim::SimFuncOp function =
        symbols.lookup<sim::SimFuncOp>(fragment.getFunction().getValue());
    auto spawns = spawnsByCallee.find(fragment.getFunction().getAttr());
    bool primitive =
        function &&
        ::obelisk::schedule::has<::obelisk::schedule::Field::PrimitiveName>(
            function);
    auto combinationalBody = combinational.analyze(function, provenance);
    if (!function ||
        (!combinationalBody &&
         function.getEntryKind() != sim::EntryKind::Continuous) ||
        !(primitive ? isPrimitiveComputeBodyFusionEligible(function, provenance)
                    : isComputeBodyFusionEligible(function, provenance)) ||
        (!combinationalBody && function.getBody().getBlocks().size() != 2) ||
        spawns == spawnsByCallee.end() || spawns->second.size() != 1 ||
        !spawns->second.front()->getResult(0).use_empty())
      return failure();
    if (combinationalBody &&
        spawns->second.front()
                ->getParentOfType<sim::SimFuncOp>()
                .getEntryKind() != sim::EntryKind::RootInitializer)
      return failure();
    Block &entry = function.getBody().front();
    Block &body = combinationalBody ? *combinationalBody->activation
                                    : function.getBody().back();
    if (!primitive && body.getNumArguments() != 0)
      return failure();
    auto branch = dyn_cast<cf::BranchOp>(entry.getTerminator());
    Operation *suspend =
        combinationalBody ? combinationalBody->suspend : body.getTerminator();
    auto resume = resumeTargets.find(member);
    bool changeWait = isa<sim::SimSuspendChangeOp>(suspend);
    if (auto any = dyn_cast<sim::SimSuspendAnyOp>(suspend))
      changeWait = llvm::all_of(any.getEdges(), [](int32_t edge) {
        return edge == static_cast<int32_t>(sim::EdgeKind::Change);
      });
    if (!isa<sim::SimSuspendChangeOp, sim::SimSuspendAnyOp>(suspend))
      return failure();
    auto successorOperands = cast<BranchOpInterface>(suspend)
                                 .getSuccessorOperands(0)
                                 .getForwardedOperands();
    if (!branch || branch.getDest() != &body ||
        branch.getDestOperands().size() != body.getNumArguments() ||
        successorOperands.size() != body.getNumArguments() ||
        !llvm::all_of(llvm::zip_equal(body.getArguments(),
                                      branch.getDestOperands(),
                                      successorOperands),
                      [](auto values) {
                        Type type = std::get<0>(values).getType();
                        return std::get<1>(values).getType() == type &&
                               std::get<2>(values).getType() == type;
                      }) ||
        suspend->getSuccessor(0) != &body || !changeWait ||
        resume == resumeTargets.end())
      return failure();
    // The local dirty mask must observe every immediate publication made by
    // the body. Plain and changed driver drives have an exact transition
    // result, but ref stores and transitive calls currently do not. Keep those
    // operations at an explicit kernel boundary until region lowering can
    // return their changed ranges as SSA values. Otherwise an internal store
    // or a drive in a callee can fail to select a fused downstream consumer.
    bool hasUntrackedPublication = false;
    function.walk([&](Operation *operation) {
      hasUntrackedPublication |=
          isa<sim::SimRefStoreOp, sim::SimCallOp>(operation);
    });
    if (hasUntrackedPublication && !combinationalBody)
      return failure();
    Candidate candidate{function,
                        spawns->second.front(),
                        &body,
                        suspend,
                        {},
                        {},
                        {},
                        member,
                        resume->second,
                        std::move(combinationalBody)};
    candidate.initialState.append(branch.getDestOperands().begin(),
                                  branch.getDestOperands().end());
    candidate.nextState.append(successorOperands.begin(),
                               successorOperands.end());
    candidates.push_back(std::move(candidate));
  }
  if (candidates.size() < 2 || candidates.size() > 64)
    return failure();
  bool rankedCombinational = candidates.front().combinationalBody.has_value();
  if (llvm::any_of(candidates, [&](const Candidate &candidate) {
        return candidate.combinationalBody.has_value() != rankedCombinational;
      }))
    return failure();
  for (Candidate &candidate : candidates)
    if (candidate.combinationalBody &&
        (candidate.function.getEntryKind() !=
             candidates.front().function.getEntryKind() ||
         candidate.function.getDomain() !=
             candidates.front().function.getDomain() ||
         candidate.function.getHomeRegion() !=
             candidates.front().function.getHomeRegion() ||
         codeUnits.getScope(candidate.function) !=
             codeUnits.getScope(candidates.front().function)))
      return failure();

  SmallVector<Value> operands;
  SmallVector<Type> inputTypes;
  SmallVector<DictionaryAttr> argumentAttrs;
  DenseMap<Value, unsigned> operandIndices;
  sim::SimSpawnOp insertionSpawn = candidates.front().spawn;
  for (Candidate &candidate : candidates) {
    if (candidate.spawn->getBlock() != insertionSpawn->getBlock())
      return failure();
    if (insertionSpawn->isBeforeInBlock(candidate.spawn))
      insertionSpawn = candidate.spawn;
    Block &entry = candidate.function.getBody().front();
    for (auto [argument, operand] :
         llvm::zip_equal(entry.getArguments(), candidate.spawn.getOperands())) {
      auto [found, inserted] =
          operandIndices.try_emplace(operand, operands.size());
      unsigned index = found->second;
      DictionaryAttr attrs =
          candidate.function.getArgAttrDict(argument.getArgNumber());
      if (inserted) {
        operands.push_back(operand);
        inputTypes.push_back(argument.getType());
        argumentAttrs.push_back(attrs);
      } else if (inputTypes[index] != argument.getType() ||
                 argumentAttrs[index] != attrs) {
        return failure();
      }
      candidate.fusedArguments.push_back(index);
    }
  }

  OpBuilder builder = OpBuilder::atBlockEnd(&design.getBody().front());
  SmallString<40> name;
  ("__obelisk_region_kernel_" + Twine(fusion.getId())).toVector(name);
  unsigned symbolCounter = 0;
  name = SymbolTable::generateSymbolName<40>(
      name,
      [&](StringRef candidate) { return symbols.lookup(candidate) != nullptr; },
      symbolCounter);
  sim::SimFuncOp first = candidates.front().function;
  SmallVector<NamedAttribute> attributes;
  if (IntegerAttr codeUnit = first.getCodeUnitIdAttr())
    attributes.emplace_back(first.getCodeUnitIdAttrName(), codeUnit);
  attributes.emplace_back(first.getDomainAttrName(), first.getDomainAttr());
  attributes.emplace_back(first.getHomeRegionAttrName(),
                          first.getHomeRegionAttr());
  sim::SimFuncOp kernel = sim::SimFuncOp::create(
      builder, first.getLoc(), name,
      FunctionType::get(design.getContext(), inputTypes, TypeRange{}),
      first.getEntryKind(), attributes, argumentAttrs);
  symbols.insert(kernel);
  SymbolTable::setSymbolVisibility(kernel, SymbolTable::Visibility::Private);
  ::obelisk::schedule::set<schedule::metadata::nativeRegionBody>(
      kernel, builder.getUnitAttr());
  ::obelisk::schedule::set<
      schedule::metadata::evalReconstructsContinuationArgs>(
      kernel, builder.getUnitAttr());
  ::obelisk::schedule::set<::obelisk::schedule::Field::EvalFusionGroup>(
      kernel, builder.getI32IntegerAttr(fusion.getId()));
  SmallVector<Attribute> sourceOwners;
  sourceOwners.reserve(candidates.size());
  for (Candidate &candidate : candidates) {
    schedule::ContinuationSiteAttr site;
    if (auto suspend = dyn_cast<sim::SimSuspendChangeOp>(candidate.suspend))
      site = suspend.getSiteAttr();
    else if (auto suspend = dyn_cast<sim::SimSuspendAnyOp>(candidate.suspend))
      site = suspend.getSiteAttr();
    sim::SimFuncOp sourceFunction = candidate.function;
    for (sim::SimFuncOp function :
         design.getBody().front().getOps<sim::SimFuncOp>())
      if (auto evalBody =
              ::obelisk::schedule::get<::obelisk::schedule::Field::EvalBody>(
                  function);
          evalBody && evalBody.getValue() == candidate.function.getSymName()) {
        sourceFunction = function;
        break;
      }
    IntegerAttr codeUnit = sourceFunction.getCodeUnitIdAttr();
    if (!site || !codeUnit) {
      symbols.erase(kernel);
      return failure();
    }
    sourceOwners.push_back(schedule::SourceOwnerAttr::get(
        builder.getContext(), codeUnit,
        builder.getI32IntegerAttr(site.getId())));
  }
  ::obelisk::schedule::set<::obelisk::schedule::Field::EvalSourceOwners>(
      kernel, builder.getArrayAttr(sourceOwners));
  sim::SimFuncOp memberHelper;
  sim::SimCodeUnitDeclOp memberHelperDeclaration;
  // The kernel is built incrementally, so any later rejection must remove the
  // partially populated symbol again. Leaving it behind would publish a
  // terminator-less function to a caller that only checks for success.
  auto bail = [&]() -> FailureOr<sim::SimFuncOp> {
    if (memberHelper)
      symbols.erase(memberHelper);
    if (memberHelperDeclaration) {
      codeUnits.erase(memberHelperDeclaration.getId());
      memberHelperDeclaration.erase();
    }
    symbols.erase(kernel);
    return failure();
  };
  Block &entry = kernel.getBody().front();
  Block *body = new Block;
  Block *wait = new Block;
  kernel.getBody().push_back(body);
  kernel.getBody().push_back(wait);

  SmallVector<std::unique_ptr<IRMapping>> mappings;
  llvm::SmallDenseSet<Operation *> hoistedWatchOps;
  builder.setInsertionPointToStart(&entry);
  for (Candidate &candidate : candidates) {
    auto mapping = std::make_unique<IRMapping>();
    for (auto [argument, index] :
         llvm::zip_equal(candidate.function.getBody().front().getArguments(),
                         candidate.fusedArguments))
      mapping->map(argument, entry.getArgument(index));
    for (Operation &operation :
         candidate.function.getBody().front().without_terminator())
      builder.clone(operation, *mapping);
    mappings.push_back(std::move(mapping));
  }
  // A frontend primitive commonly watches a statically selected bit of a
  // packed capture.  The selected handle is defined in the activation block,
  // not the entry preamble, but the union wait and its snapshots need that
  // handle outside the conditional member body. Hoist only pure speculatable
  // expression trees; descriptor views meet this contract and remain stable
  // for the lifetime of the spawned kernel.
  auto mapWatchHandle = [&](Value value, IRMapping &mapping,
                            auto &mapWatchHandle) -> Value {
    if (Value mapped = mapping.lookupOrNull(value))
      return mapped;
    auto argument = dyn_cast<BlockArgument>(value);
    if (argument)
      return {};
    Operation *definition = value.getDefiningOp();
    if (!definition || !isMemoryEffectFree(definition) ||
        !isSpeculatable(definition))
      return {};
    for (Value operand : definition->getOperands())
      if (!mapWatchHandle(operand, mapping, mapWatchHandle))
        return {};
    Operation *cloned = builder.clone(*definition, mapping);
    hoistedWatchOps.insert(definition);
    unsigned result = cast<OpResult>(value).getResultNumber();
    return cloned->getResult(result);
  };
  if (rankedCombinational) {
    DenseMap<StringAttr, unsigned> owners;
    for (auto [index, candidate] : llvm::enumerate(candidates))
      owners[candidate.function.getSymNameAttr()] = index;
    for (Attribute attribute : graph.getEdges()) {
      auto edge = cast<schedule::ComputeEdgeAttr>(attribute);
      if (edge.getKind() != schedule::ComputeEdgeKind::Sensitivity)
        continue;
      auto source = cast<schedule::ComputeFragmentAttr>(
          graph.getNodes()[edge.getSource()]);
      auto target = cast<schedule::ComputeFragmentAttr>(
          graph.getNodes()[edge.getTarget()]);
      auto from = owners.find(source.getFunction().getAttr());
      auto to = owners.find(target.getFunction().getAttr());
      if (from != owners.end() && to != owners.end() &&
          to->second <= from->second)
        return bail();
    }
    SmallVector<Value> watched;
    for (auto [candidate, mapping] : llvm::zip_equal(candidates, mappings)) {
      SmallVector<Value> memberWatches;
      if (auto change = dyn_cast<sim::SimSuspendChangeOp>(candidate.suspend))
        memberWatches.push_back(change.getWatched());
      else
        llvm::append_range(
            memberWatches,
            cast<sim::SimSuspendAnyOp>(candidate.suspend).getWatched());
      for (Value watch : memberWatches) {
        Value handle = mapWatchHandle(watch, *mapping, mapWatchHandle);
        if (!handle)
          return bail();
        watched.push_back(handle);
      }
    }
    // Exclusive, complete, deterministic assignments make an unchanged-input
    // activation idempotent: every store writes its existing four-state value
    // and publishes no transition. It is therefore observationally equivalent
    // to evaluate this bounded forward segment at each union activation. This
    // proof removes snapshots and per-member readiness; it does not authorize
    // extra executions of tasks, coverage, latches or other effects (4.3, 4.6).
    cf::BranchOp::create(builder, kernel.getLoc(), body);
    Block *tail = body;
    for (auto [candidate, mapping] : llvm::zip_equal(candidates, mappings)) {
      for (Block *source : candidate.combinationalBody->blocks) {
        Block *cloned = new Block;
        kernel.getBody().push_back(cloned);
        mapping->map(source, cloned);
        for (BlockArgument argument : source->getArguments())
          mapping->map(argument, cloned->addArgument(argument.getType(),
                                                     argument.getLoc()));
      }
      builder.setInsertionPointToEnd(tail);
      cf::BranchOp::create(
          builder, kernel.getLoc(),
          mapping->lookup(candidate.combinationalBody->activation));
      for (Block *source : candidate.combinationalBody->blocks) {
        builder.setInsertionPointToStart(mapping->lookup(source));
        for (Operation &op : *source)
          if (&op != candidate.suspend && !hoistedWatchOps.contains(&op))
            builder.clone(op, *mapping);
      }
      tail = mapping->lookup(candidate.suspend->getBlock());
    }
    builder.setInsertionPointToEnd(tail);
    cf::BranchOp::create(builder, kernel.getLoc(), wait);
    builder.setInsertionPointToStart(wait);
    SmallVector<int32_t> edges(watched.size(),
                               static_cast<int32_t>(sim::EdgeKind::Change));
    sim::SimSuspendAnyOp::create(
        builder, kernel.getLoc(), watched, builder.getDenseI32ArrayAttr(edges),
        schedule::ContinuationSiteAttr{}, sim::EventRegionAttr{}, body);
    builder.setInsertionPoint(insertionSpawn);
    sim::SimSpawnOp::create(builder, kernel.getLoc(), kernel.getSymNameAttr(),
                            operands, ArrayAttr{}, ArrayAttr{});
    for (Candidate &candidate : candidates)
      candidate.spawn.erase();
    for (Candidate &candidate : candidates) {
      inputIndex.retargetCoverageKeepalives(candidate.function, kernel);
      symbols.erase(candidate.function);
    }
    return kernel;
  }
  BlockArgument initialize =
      body->addArgument(builder.getI1Type(), kernel.getLoc());
  struct Watch {
    Value handle;
    BlockArgument previous;
    unsigned candidate;
  };
  auto loadWatched = [&](OpBuilder &builder, Location location,
                         Value handle) -> Value {
    Value value;
    if (auto reference = dyn_cast<sim::RefType>(handle.getType()))
      value = sim::SimRefLoadOp::create(builder, location,
                                        reference.getElementType(), handle);
    else if (auto net = dyn_cast<sim::NetType>(handle.getType()))
      value = sim::SimNetReadOp::create(builder, location, net.getElementType(),
                                        handle);
    else
      return {};
    Type scalarType = sim::getPackedScalarType(value.getType());
    if (!scalarType)
      return {};
    if (value.getType() != scalarType)
      value =
          sim::SimPackedFlattenOp::create(builder, location, scalarType, value);
    return value;
  };

  SmallVector<Watch> watchSnapshots;
  SmallVector<Value> entryOperands;
  Value initial = arith::ConstantOp::create(
      builder, kernel.getLoc(), builder.getI1Type(), builder.getBoolAttr(true));
  entryOperands.push_back(initial);
  for (auto [candidateIndex, pair] :
       llvm::enumerate(llvm::zip_equal(candidates, mappings))) {
    auto &[candidate, mapping] = pair;
    SmallVector<Value> handles;
    if (auto change = dyn_cast<sim::SimSuspendChangeOp>(candidate.suspend)) {
      Value handle =
          mapWatchHandle(change.getWatched(), *mapping, mapWatchHandle);
      if (!handle)
        return bail();
      handles.push_back(handle);
    } else {
      for (Value watched :
           cast<sim::SimSuspendAnyOp>(candidate.suspend).getWatched()) {
        Value handle = mapWatchHandle(watched, *mapping, mapWatchHandle);
        if (!handle)
          return bail();
        handles.push_back(handle);
      }
    }
    for (Value handle : handles) {
      Value snapshot = loadWatched(builder, kernel.getLoc(), handle);
      if (!snapshot)
        return bail();
      BlockArgument previous =
          body->addArgument(snapshot.getType(), kernel.getLoc());
      watchSnapshots.push_back(
          {handle, previous, static_cast<unsigned>(candidateIndex)});
      entryOperands.push_back(snapshot);
    }
  }

  // Preserve the established stateless primitive/general kernel shape. The
  // outlined evaluator exists solely to bound compile space for loop-carried
  // stateful primitives; adding a noinline call to the existing acyclic hot
  // path would trade away its straight-line native performance.
  bool carriesState = llvm::any_of(candidates, [](const Candidate &candidate) {
    return !candidate.initialState.empty();
  });
  if (!carriesState) {
    cf::BranchOp::create(builder, kernel.getLoc(), body, entryOperands);

    builder.setInsertionPointToStart(body);
    Type maskType = builder.getI64Type();
    Value dirty = arith::ConstantOp::create(builder, kernel.getLoc(), maskType,
                                            builder.getI64IntegerAttr(0));
    for (const Watch &watch : watchSnapshots) {
      Value current = loadWatched(builder, kernel.getLoc(), watch.handle);
      if (!current)
        return bail();
      Value equal = createPackedCaseComparison(builder, kernel.getLoc(),
                                               sim::CompareKind::CaseEq,
                                               current, watch.previous);
      Value changed = arith::XOrIOp::create(
          builder, kernel.getLoc(), equal,
          arith::ConstantOp::create(builder, kernel.getLoc(),
                                    builder.getI1Type(),
                                    builder.getBoolAttr(true)));
      Value bit = arith::ConstantOp::create(
          builder, kernel.getLoc(), maskType,
          builder.getI64IntegerAttr(uint64_t{1} << watch.candidate));
      Value selected = arith::SelectOp::create(builder, kernel.getLoc(),
                                               changed, bit, dirty);
      dirty = arith::OrIOp::create(builder, kernel.getLoc(), dirty, selected);
    }
    Value allDirty = arith::ConstantOp::create(
        builder, kernel.getLoc(), maskType,
        builder.getI64IntegerAttr(
            candidates.size() == 64 ? UINT64_MAX
                                    : (uint64_t{1} << candidates.size()) - 1));
    dirty = arith::SelectOp::create(builder, kernel.getLoc(), initialize,
                                    allDirty, dirty);

    SmallVector<uint64_t> downstreamMasks(candidates.size(), 0);
    for (Attribute attribute : graph.getEdges()) {
      auto edge = cast<schedule::ComputeEdgeAttr>(attribute);
      if (edge.getKind() != schedule::ComputeEdgeKind::Sensitivity)
        continue;
      for (auto [sourceIndex, source] : llvm::enumerate(candidates)) {
        if (edge.getSource() != source.resumeTarget)
          continue;
        for (auto [targetIndex, target] : llvm::enumerate(candidates)) {
          if (edge.getTarget() != target.fragment)
            continue;
          if (targetIndex <= sourceIndex)
            return bail();
          downstreamMasks[sourceIndex] |= uint64_t{1} << targetIndex;
        }
      }
    }

    Value currentMask = dirty;
    Block *test = body;
    for (auto [candidateIndex, pair] :
         llvm::enumerate(llvm::zip_equal(candidates, mappings))) {
      auto &[candidate, mapping] = pair;
      builder.setInsertionPointToEnd(test);
      Value bit = arith::ConstantOp::create(
          builder, kernel.getLoc(), maskType,
          builder.getI64IntegerAttr(uint64_t{1} << candidateIndex));
      Value selectedBits =
          arith::AndIOp::create(builder, kernel.getLoc(), currentMask, bit);
      Value selected = arith::CmpIOp::create(
          builder, kernel.getLoc(), arith::CmpIPredicate::ne, selectedBits,
          arith::ConstantOp::create(builder, kernel.getLoc(), maskType,
                                    builder.getI64IntegerAttr(0)));
      Block *execute = new Block;
      Block *next = new Block;
      BlockArgument nextMask = next->addArgument(maskType, kernel.getLoc());
      kernel.getBody().push_back(execute);
      kernel.getBody().push_back(next);
      cf::CondBranchOp::create(builder, kernel.getLoc(), selected, execute,
                               ValueRange{}, next, ValueRange{currentMask});

      builder.setInsertionPointToStart(execute);
      Value changed = arith::ConstantOp::create(builder, kernel.getLoc(),
                                                builder.getI1Type(),
                                                builder.getBoolAttr(false));
      for (Operation &operation : candidate.body->without_terminator()) {
        if (hoistedWatchOps.contains(&operation))
          continue;
        if (auto drive = dyn_cast<sim::SimDriverDriveOp>(operation)) {
          auto replacement = sim::SimDriverDriveChangedOp::create(
              builder, drive.getLoc(), mapping->lookup(drive.getDriver()),
              mapping->lookup(drive.getValue()));
          if (auto defer = ::obelisk::schedule::get<
                  ::obelisk::schedule::Field::DeferNetResolution>(drive))
            ::obelisk::schedule::set<
                ::obelisk::schedule::Field::DeferNetResolution>(replacement,
                                                                defer);
          changed = arith::OrIOp::create(builder, drive.getLoc(), changed,
                                         replacement.getChanged());
        } else {
          Operation *cloned = builder.clone(operation, *mapping);
          if (auto drive = dyn_cast<sim::SimDriverDriveChangedOp>(cloned))
            changed = arith::OrIOp::create(builder, drive.getLoc(), changed,
                                           drive.getChanged());
        }
      }
      Value nextValue = currentMask;
      if (downstreamMasks[candidateIndex] != 0) {
        Value downstream = arith::ConstantOp::create(
            builder, kernel.getLoc(), maskType,
            builder.getI64IntegerAttr(downstreamMasks[candidateIndex]));
        Value propagated = arith::OrIOp::create(builder, kernel.getLoc(),
                                                currentMask, downstream);
        nextValue = arith::SelectOp::create(builder, kernel.getLoc(), changed,
                                            propagated, currentMask);
      }
      cf::BranchOp::create(builder, kernel.getLoc(), next,
                           ValueRange{nextValue});
      test = next;
      currentMask = nextMask;
    }
    builder.setInsertionPointToEnd(test);
    cf::BranchOp::create(builder, kernel.getLoc(), wait);

    SmallVector<Value> watched;
    SmallVector<int32_t> edges;
    for (auto [candidate, mapping] : llvm::zip_equal(candidates, mappings)) {
      if (auto change = dyn_cast<sim::SimSuspendChangeOp>(candidate.suspend)) {
        watched.push_back(mapping->lookup(change.getWatched()));
        edges.push_back(static_cast<int32_t>(sim::EdgeKind::Change));
      } else {
        auto any = cast<sim::SimSuspendAnyOp>(candidate.suspend);
        for (auto [value, edge] :
             llvm::zip_equal(any.getWatched(), any.getEdges())) {
          watched.push_back(mapping->lookup(value));
          edges.push_back(edge);
        }
      }
    }
    builder.setInsertionPointToStart(wait);
    Value resumed =
        arith::ConstantOp::create(builder, kernel.getLoc(), builder.getI1Type(),
                                  builder.getBoolAttr(false));
    SmallVector<Value> waitOperands(watched);
    waitOperands.push_back(resumed);
    for (const Watch &watch : watchSnapshots) {
      Value snapshot = loadWatched(builder, kernel.getLoc(), watch.handle);
      if (!snapshot)
        return bail();
      waitOperands.push_back(snapshot);
    }
    sim::SimSuspendAnyOp::create(builder, kernel.getLoc(), waitOperands,
                                 builder.getDenseI32ArrayAttr(edges),
                                 schedule::ContinuationSiteAttr{},
                                 sim::EventRegionAttr{}, body);

    builder.setInsertionPoint(insertionSpawn);
    sim::SimSpawnOp::create(builder, kernel.getLoc(), kernel.getSymNameAttr(),
                            operands, ArrayAttr{}, ArrayAttr{});
    for (Candidate &candidate : candidates)
      candidate.spawn.erase();
    for (Candidate &candidate : candidates) {
      inputIndex.retargetCoverageKeepalives(candidate.function, kernel);
      symbols.erase(candidate.function);
    }
    return kernel;
  }

  // Outline one shared zero-time member evaluator. Array instances have the
  // same primitive body, but their statically selected ref/driver views carry
  // different indices. Hoist those handles into the kernel and pass them (and
  // all other body-external values) to one helper, so LLVM compiles the UDP
  // table once per bounded cohort instead of cloning it into every coroutine
  // arm and every coro resume/destroy split.
  for (auto [candidate, mapping] : llvm::zip_equal(candidates, mappings))
    for (Operation &operation : candidate.body->without_terminator())
      for (Value operand : operation.getOperands())
        if (isa<sim::RefType, sim::NetType, sim::DriverType>(
                operand.getType()) &&
            operand.getDefiningOp() &&
            operand.getDefiningOp()->getBlock() == candidate.body &&
            !mapWatchHandle(operand, *mapping, mapWatchHandle))
          return bail();

  SmallVector<SmallVector<Operation *>> memberOperations(candidates.size());
  for (auto [index, candidate] : llvm::enumerate(candidates))
    for (Operation &operation : candidate.body->without_terminator())
      if (!hoistedWatchOps.contains(&operation))
        memberOperations[index].push_back(&operation);
  ArrayRef<Operation *> templateOperations = memberOperations.front();
  auto areEquivalentMemberOperations = [](Operation *lhs, Operation *rhs) {
    auto lhsInertial = dyn_cast<sim::SimDriverDriveInertialOp>(lhs);
    auto rhsInertial = dyn_cast<sim::SimDriverDriveInertialOp>(rhs);
    if (!lhsInertial && !rhsInertial)
      return OperationEquivalence::isEquivalentTo(
          lhs, rhs, OperationEquivalence::ignoreValueEquivalence, nullptr,
          OperationEquivalence::IgnoreLocations);
    if (!lhsInertial || !rhsInertial ||
        lhs->getOperandTypes() != rhs->getOperandTypes() ||
        lhs->getResultTypes() != rhs->getResultTypes() ||
        lhs->getAttrs().size() != rhs->getAttrs().size())
      return false;
    for (NamedAttribute attribute : lhs->getAttrs()) {
      if (attribute.getName() == "code_unit_id")
        continue;
      if (rhs->getAttr(attribute.getName()) != attribute.getValue())
        return false;
    }
    return true;
  };
  for (ArrayRef<Operation *> operations :
       ArrayRef(memberOperations).drop_front()) {
    if (operations.size() != templateOperations.size())
      return bail();
    for (auto [templateOperation, operation] :
         llvm::zip_equal(templateOperations, operations))
      if (!areEquivalentMemberOperations(templateOperation, operation))
        return bail();
  }

  SmallVector<unsigned> resolveDriveOperations;
  SetVector<Value> resolveDrivers;
  SmallVector<unsigned> deferredDriveOperations;
  for (auto [operationIndex, operation] : llvm::enumerate(templateOperations))
    if (auto drive = dyn_cast<sim::SimDriverDriveOp>(operation)) {
      if (::obelisk::schedule::has<
              ::obelisk::schedule::Field::DeferNetResolution>(drive))
        return bail();
      if (resolveDrivers.insert(drive.getDriver()))
        resolveDriveOperations.push_back(operationIndex);
    } else if (isa<sim::SimDriverDriveInertialOp>(operation))
      deferredDriveOperations.push_back(operationIndex);
    else if (auto drive = dyn_cast<sim::SimDriverDriveChangedOp>(operation);
             drive && !drive.getChanged().use_empty())
      return bail();
  // Sequential UDP lowering emits at most one unconditional inertial
  // publication per activation. Keep that exact, auditable shape: additional
  // sites need a richer returned-publication ABI and remain separate actors.
  if (deferredDriveOperations.size() > 1)
    return bail();
  for (ArrayRef<Operation *> operations : memberOperations)
    for (unsigned operationIndex : deferredDriveOperations) {
      auto drive =
          cast<sim::SimDriverDriveInertialOp>(operations[operationIndex]);
      if (!drive.getRiseDelay().getDefiningOp<sim::SimTimeConstantOp>() ||
          !drive.getFallDelay().getDefiningOp<sim::SimTimeConstantOp>() ||
          !drive.getTurnoffDelay().getDefiningOp<sim::SimTimeConstantOp>())
        return bail();
    }

  struct ResultIdentity {
    unsigned operation;
    unsigned result;
  };
  DenseMap<Value, ResultIdentity> internalResults;
  for (auto [operationIndex, operation] : llvm::enumerate(templateOperations))
    for (auto [resultIndex, result] : llvm::enumerate(operation->getResults()))
      internalResults.try_emplace(
          result, ResultIdentity{static_cast<unsigned>(operationIndex),
                                 static_cast<unsigned>(resultIndex)});
  std::string memberFingerprint;
  llvm::raw_string_ostream fingerprint(memberFingerprint);
  DenseMap<Value, unsigned> fingerprintExternals;
  auto appendValueIdentity = [&](Value value) {
    assert(value && "fingerprinted member value must exist");
    if (auto result = internalResults.find(value);
        result != internalResults.end())
      fingerprint << 'r' << result->second.operation << '.'
                  << result->second.result;
    else if (auto argument = dyn_cast<BlockArgument>(value);
             argument && argument.getOwner() == candidates.front().body)
      fingerprint << 'b' << argument.getArgNumber();
    else {
      auto [external, inserted] =
          fingerprintExternals.try_emplace(value, fingerprintExternals.size());
      (void)inserted;
      fingerprint << 'e' << external->second;
    }
    fingerprint << ':';
    value.getType().print(fingerprint);
  };
  bool reusableMember = true;
  for (auto [operationIndex, operation] : llvm::enumerate(templateOperations)) {
    reusableMember &= operation->getNumRegions() == 0;
    fingerprint << operation->getName().getStringRef() << '(';
    for (Value operand : operation->getOperands()) {
      appendValueIdentity(operand);
      fingerprint << ',';
    }
    fingerprint << ")->(";
    for (Type type : operation->getResultTypes()) {
      type.print(fingerprint);
      fingerprint << ',';
    }
    fingerprint << "){";
    for (NamedAttribute attribute : operation->getAttrs()) {
      if (isa<sim::SimDriverDriveInertialOp>(operation) &&
          attribute.getName() == "code_unit_id")
        continue;
      fingerprint << attribute.getName().strref() << '=';
      attribute.getValue().print(fingerprint);
      fingerprint << ',';
    }
    fingerprint << "};";
  }
  fingerprint << "returns(";
  for (Value state : candidates.front().nextState) {
    appendValueIdentity(state);
    fingerprint << ',';
  }
  for (unsigned operationIndex : deferredDriveOperations) {
    auto drive =
        cast<sim::SimDriverDriveInertialOp>(templateOperations[operationIndex]);
    SmallVector<Value, 4> returned{drive.getValue(), drive.getRiseDelay(),
                                   drive.getFallDelay(),
                                   drive.getTurnoffDelay()};
    for (Value value : returned) {
      appendValueIdentity(value);
      fingerprint << ',';
    }
  }
  fingerprint << ");";
  if (!reusableMember)
    fingerprint << "unique=" << kernel.getSymName();
  fingerprint.flush();
  DenseMap<Value, unsigned> externalIndices;
  SmallVector<SmallVector<Value>> memberExternals(candidates.size());
  auto registerValues = [&](Value templateValue,
                            ArrayRef<Value> values) -> LogicalResult {
    if (isa<sim::ContextType>(templateValue.getType())) {
      for (auto [member, value] : llvm::enumerate(values))
        if (value !=
            candidates[member].function.getBody().front().getArgument(0))
          return failure();
      return success();
    }
    auto internal = internalResults.find(templateValue);
    if (internal != internalResults.end()) {
      for (auto [member, value] : llvm::enumerate(values))
        if (value !=
            memberOperations[member][internal->second.operation]->getResult(
                internal->second.result))
          return failure();
      return success();
    }
    auto [external, inserted] =
        externalIndices.try_emplace(templateValue, externalIndices.size());
    unsigned index = external->second;
    for (auto [member, value] : llvm::enumerate(values)) {
      if (inserted)
        memberExternals[member].push_back(value);
      else if (memberExternals[member][index] != value)
        return failure();
    }
    return success();
  };
  for (auto [operationIndex, operation] : llvm::enumerate(templateOperations))
    for (auto [operandIndex, operand] :
         llvm::enumerate(operation->getOperands())) {
      SmallVector<Value> values;
      for (ArrayRef<Operation *> operations : memberOperations)
        values.push_back(operations[operationIndex]->getOperand(operandIndex));
      if (failed(registerValues(operand, values)))
        return bail();
    }
  for (auto [stateIndex, state] :
       llvm::enumerate(candidates.front().nextState)) {
    SmallVector<Value> values;
    for (Candidate &candidate : candidates) {
      if (candidate.nextState.size() != candidates.front().nextState.size())
        return bail();
      values.push_back(candidate.nextState[stateIndex]);
    }
    if (failed(registerValues(state, values)))
      return bail();
  }

  SmallVector<Type> helperInputs;
  helperInputs.reserve(externalIndices.size() + 1);
  helperInputs.push_back(
      candidates.front().function.getBody().front().getArgument(0).getType());
  SmallVector<Value> templateExternals(externalIndices.size());
  for (auto [value, index] : externalIndices) {
    helperInputs.push_back({});
    templateExternals[index] = value;
  }
  for (auto [index, value] : llvm::enumerate(templateExternals))
    helperInputs[index + 1] = value.getType();
  SmallVector<Type> helperResults;
  for (Value state : candidates.front().nextState)
    helperResults.push_back(state.getType());
  for (unsigned operationIndex : deferredDriveOperations) {
    auto drive =
        cast<sim::SimDriverDriveInertialOp>(templateOperations[operationIndex]);
    helperResults.push_back(drive.getValue().getType());
  }
  helperResults.push_back(builder.getI1Type());

  OpBuilder helperBuilder = OpBuilder::atBlockEnd(&design.getBody().front());
  FunctionType helperType =
      FunctionType::get(design.getContext(), helperInputs, helperResults);
  StringAttr fingerprintAttr = helperBuilder.getStringAttr(memberFingerprint);
  sim::SimFuncOp helper;
  for (sim::SimFuncOp existing :
       design.getBody().front().getOps<sim::SimFuncOp>())
    if (::obelisk::schedule::has<
            ::obelisk::schedule::Field::OutlinedPrimitiveMember>(existing) &&
        ::obelisk::schedule::get<
            ::obelisk::schedule::Field::OutlinedPrimitiveFingerprint>(
            existing) == fingerprintAttr &&
        existing.getFunctionType() == helperType) {
      helper = existing;
      break;
    }
  if (!helper) {
    SmallString<48> helperName;
    (kernel.getSymName() + ".__member").toVector(helperName);
    if (symbols.lookup(helperName)) {
      unsigned helperCounter = 0;
      helperName = SymbolTable::generateSymbolName<48>(
          helperName,
          [&](StringRef candidate) {
            return symbols.lookup(candidate) != nullptr;
          },
          helperCounter);
    }
    uint64_t helperScope =
        codeUnits.getScope(candidates.front().function).value_or(0);
    uint64_t helperCodeUnit = codeUnits.allocate(helperScope);
    memberHelperDeclaration = sim::SimCodeUnitDeclOp::create(
        helperBuilder, kernel.getLoc(), helperCodeUnit, helperScope,
        sim::EntryKind::Function, helperBuilder.getStringAttr(helperName),
        helperBuilder.getStringAttr("generated primitive cohort member"),
        helperBuilder.getUnitAttr());
    SmallVector<DictionaryAttr> helperArgumentAttrs(
        helperInputs.size(), helperBuilder.getDictionaryAttr({}));
    helperArgumentAttrs.front() = candidates.front().function.getArgAttrDict(0);
    for (DictionaryAttr &attrs :
         MutableArrayRef(helperArgumentAttrs).drop_front())
      attrs = helperBuilder.getDictionaryAttr(helperBuilder.getNamedAttr(
          "simulation.capture_kind",
          helperBuilder.getI32IntegerAttr(
              static_cast<int32_t>(sim::CaptureKind::Formal))));
    SmallVector<NamedAttribute> helperAttributes{helperBuilder.getNamedAttr(
        "code_unit_id", helperBuilder.getI64IntegerAttr(helperCodeUnit))};
    helper = sim::SimFuncOp::create(helperBuilder, kernel.getLoc(), helperName,
                                    helperType, sim::EntryKind::Function,
                                    helperAttributes, helperArgumentAttrs);
    memberHelper = helper;
    symbols.insert(helper);
    SymbolTable::setSymbolVisibility(helper, SymbolTable::Visibility::Private);
    ::obelisk::schedule::set<
        ::obelisk::schedule::Field::OutlinedPrimitiveMember>(
        helper, helperBuilder.getUnitAttr());
    ::obelisk::schedule::set<
        ::obelisk::schedule::Field::OutlinedPrimitiveFingerprint>(
        helper, fingerprintAttr);
    helper->setAttr(
        "passthrough",
        helperBuilder.getArrayAttr({helperBuilder.getStringAttr("noinline")}));
    IRMapping helperMapping;
    helperMapping.map(
        candidates.front().function.getBody().front().getArgument(0),
        helper.getBody().front().getArgument(0));
    for (auto [value, argument] :
         llvm::zip_equal(templateExternals,
                         helper.getBody().front().getArguments().drop_front()))
      helperMapping.map(value, argument);
    helperBuilder.setInsertionPointToStart(&helper.getBody().front());
    Value helperChanged = arith::ConstantOp::create(
        helperBuilder, kernel.getLoc(), helperBuilder.getI1Type(),
        helperBuilder.getBoolAttr(false));
    auto recordRawChange = [&](Location location, Value driver, Value value) {
      Value previous = sim::SimDriverReadOp::create(helperBuilder, location,
                                                    value.getType(), driver);
      Value rawChanged = createPackedCaseComparison(
          helperBuilder, location, sim::CompareKind::CaseNe, previous, value);
      helperChanged = arith::OrIOp::create(helperBuilder, location,
                                           helperChanged, rawChanged);
    };
    for (Operation *operation : templateOperations) {
      if (isa<sim::SimDriverDriveInertialOp>(operation))
        continue;
      if (auto drive = dyn_cast<sim::SimDriverDriveOp>(operation)) {
        Value driver = helperMapping.lookup(drive.getDriver());
        Value value = helperMapping.lookup(drive.getValue());
        recordRawChange(drive.getLoc(), driver, value);
        auto replacement = sim::SimDriverDriveChangedOp::create(
            helperBuilder, drive.getLoc(), driver, value);
        replacement->setAttrs(drive->getAttrs());
        ::obelisk::schedule::set<
            ::obelisk::schedule::Field::DeferNetResolution>(
            replacement, helperBuilder.getUnitAttr());
        continue;
      }
      if (auto drive = dyn_cast<sim::SimDriverDriveChangedOp>(operation)) {
        // A delayed-only sequential UDP's unused initial-state
        // canonicalization must not pre-write the raw driver plane: the
        // selected inertial site compares against that plane to schedule its
        // first publication. No immediate resolver exists in this shape.
        if (resolveDriveOperations.empty() &&
            !deferredDriveOperations.empty() && drive.getChanged().use_empty())
          continue;
        Value driver = helperMapping.lookup(drive.getDriver());
        Value value = helperMapping.lookup(drive.getValue());
        recordRawChange(drive.getLoc(), driver, value);
        Operation *cloned = helperBuilder.clone(*operation, helperMapping);
        ::obelisk::schedule::set<
            ::obelisk::schedule::Field::DeferNetResolution>(
            cloned, helperBuilder.getUnitAttr());
        continue;
      }
      helperBuilder.clone(*operation, helperMapping);
      // Sequential UDP initialization canonicalizes its private Z sentinel
      // through an unused drive_changed before the table evaluation. Keep the
      // raw write in program order, but let the concrete final publication in
      // the selected kernel arm resolve and notify the component once.
    }
    SmallVector<Value> helperReturn;
    for (Value state : candidates.front().nextState) {
      Value mapped = helperMapping.lookupOrNull(state);
      if (!mapped)
        return bail();
      helperReturn.push_back(mapped);
    }
    for (unsigned operationIndex : deferredDriveOperations) {
      auto drive = cast<sim::SimDriverDriveInertialOp>(
          templateOperations[operationIndex]);
      SmallVector<Value, 4> returned{drive.getValue(), drive.getRiseDelay(),
                                     drive.getFallDelay(),
                                     drive.getTurnoffDelay()};
      Value mapped = helperMapping.lookupOrNull(returned.front());
      if (!mapped)
        return bail();
      helperReturn.push_back(mapped);
    }
    helperReturn.push_back(helperChanged);
    sim::SimReturnOp::create(helperBuilder, kernel.getLoc(), helperReturn);
  }

  // Keep continuation state in independent typed lanes. A UDP's carried
  // previous input is normalized (in particular Z becomes X), whereas the
  // union-watch snapshot above is the raw watched value. Conflating the two
  // would corrupt edge matching and would also let an idle member inherit a
  // different member's state.
  SmallVector<Value> currentStates;
  SmallVector<unsigned> stateOffsets;
  for (auto [candidate, mapping] : llvm::zip_equal(candidates, mappings)) {
    stateOffsets.push_back(currentStates.size());
    for (auto [argument, initial] : llvm::zip_equal(
             candidate.body->getArguments(), candidate.initialState)) {
      BlockArgument state =
          body->addArgument(argument.getType(), kernel.getLoc());
      currentStates.push_back(state);
      Value mappedInitial = mapping->lookupOrNull(initial);
      if (!mappedInitial)
        return bail();
      entryOperands.push_back(mappedInitial);
    }
  }
  cf::BranchOp::create(builder, kernel.getLoc(), body, entryOperands);

  builder.setInsertionPointToStart(body);
  Type maskType = builder.getI64Type();
  Value dirty = arith::ConstantOp::create(builder, kernel.getLoc(), maskType,
                                          builder.getI64IntegerAttr(0));
  for (const Watch &watch : watchSnapshots) {
    Value current = loadWatched(builder, kernel.getLoc(), watch.handle);
    if (!current)
      return bail();
    Value equal = createPackedCaseComparison(builder, kernel.getLoc(),
                                             sim::CompareKind::CaseEq, current,
                                             watch.previous);
    Value changed = arith::XOrIOp::create(
        builder, kernel.getLoc(), equal,
        arith::ConstantOp::create(builder, kernel.getLoc(), builder.getI1Type(),
                                  builder.getBoolAttr(true)));
    Value bit = arith::ConstantOp::create(
        builder, kernel.getLoc(), maskType,
        builder.getI64IntegerAttr(uint64_t{1} << watch.candidate));
    Value selected =
        arith::SelectOp::create(builder, kernel.getLoc(), changed, bit, dirty);
    dirty = arith::OrIOp::create(builder, kernel.getLoc(), dirty, selected);
  }
  Value allDirty = arith::ConstantOp::create(
      builder, kernel.getLoc(), maskType,
      builder.getI64IntegerAttr(candidates.size() == 64
                                    ? UINT64_MAX
                                    : (uint64_t{1} << candidates.size()) - 1));
  dirty = arith::SelectOp::create(builder, kernel.getLoc(), initialize,
                                  allDirty, dirty);

  // One pass over the members must settle every internal sensitivity edge:
  // the snapshots taken at the wait boundary already observe the publications
  // this kernel performed, so a consumer that is not reactivated through the
  // mask is never woken for them again. That holds only while every internal
  // edge runs forward, which the topological SCC schedule guarantees. Reject
  // the fusion rather than silently dropping a backward edge.
  SmallVector<uint64_t> downstreamMasks(candidates.size(), 0);
  for (Attribute attribute : graph.getEdges()) {
    auto edge = cast<schedule::ComputeEdgeAttr>(attribute);
    if (edge.getKind() != schedule::ComputeEdgeKind::Sensitivity)
      continue;
    for (auto [sourceIndex, source] : llvm::enumerate(candidates)) {
      if (edge.getSource() != source.resumeTarget)
        continue;
      for (auto [targetIndex, target] : llvm::enumerate(candidates)) {
        if (edge.getTarget() != target.fragment)
          continue;
        if (targetIndex <= sourceIndex)
          return bail();
        downstreamMasks[sourceIndex] |= uint64_t{1} << targetIndex;
      }
    }
  }

  Value currentMask = dirty;
  Block *test = body;
  for (auto [candidateIndex, pair] :
       llvm::enumerate(llvm::zip_equal(candidates, mappings))) {
    auto &[candidate, mapping] = pair;
    builder.setInsertionPointToEnd(test);
    Value bit = arith::ConstantOp::create(
        builder, kernel.getLoc(), maskType,
        builder.getI64IntegerAttr(uint64_t{1} << candidateIndex));
    Value selectedBits =
        arith::AndIOp::create(builder, kernel.getLoc(), currentMask, bit);
    Value selected = arith::CmpIOp::create(
        builder, kernel.getLoc(), arith::CmpIPredicate::ne, selectedBits,
        arith::ConstantOp::create(builder, kernel.getLoc(), maskType,
                                  builder.getI64IntegerAttr(0)));
    Block *execute = new Block;
    Block *next = new Block;
    BlockArgument nextMask = next->addArgument(maskType, kernel.getLoc());
    SmallVector<BlockArgument> nextStates;
    for (Value state : currentStates)
      nextStates.push_back(next->addArgument(state.getType(), kernel.getLoc()));
    kernel.getBody().push_back(execute);
    kernel.getBody().push_back(next);
    SmallVector<Value> skippedStates{currentMask};
    llvm::append_range(skippedStates, currentStates);
    cf::CondBranchOp::create(builder, kernel.getLoc(), selected, execute,
                             ValueRange{}, next, skippedStates);

    builder.setInsertionPointToStart(execute);
    unsigned stateOffset = stateOffsets[candidateIndex];
    for (auto [argument, state] : llvm::zip_equal(
             candidate.body->getArguments(),
             ArrayRef<Value>(currentStates)
                 .slice(stateOffset, candidate.initialState.size())))
      mapping->map(argument, state);
    SmallVector<Value> callOperands;
    callOperands.push_back(
        mapping->lookup(candidate.function.getBody().front().getArgument(0)));
    for (Value value : memberExternals[candidateIndex]) {
      Value mapped = mapping->lookupOrNull(value);
      if (!mapped)
        return bail();
      callOperands.push_back(mapped);
    }
    sim::SimCallOp call = sim::SimCallOp::create(
        builder, kernel.getLoc(), helperResults, helper.getSymNameAttr(),
        callOperands, ArrayAttr{}, ArrayAttr{});
    Value rawChanged = call.getResults().back();
    Value changed =
        arith::ConstantOp::create(builder, kernel.getLoc(), builder.getI1Type(),
                                  builder.getBoolAttr(false));
    unsigned resultIndex = candidate.nextState.size();
    if (!resolveDriveOperations.empty()) {
      Block *resolve = new Block;
      Block *afterResolve = new Block;
      BlockArgument resolvedChanged =
          afterResolve->addArgument(builder.getI1Type(), kernel.getLoc());
      kernel.getBody().push_back(resolve);
      kernel.getBody().push_back(afterResolve);
      Value unchanged = arith::ConstantOp::create(builder, kernel.getLoc(),
                                                  builder.getI1Type(),
                                                  builder.getBoolAttr(false));
      cf::CondBranchOp::create(builder, kernel.getLoc(), rawChanged, resolve,
                               ValueRange{}, afterResolve,
                               ValueRange{unchanged});
      builder.setInsertionPointToStart(resolve);
      Value publishedChanged = unchanged;
      for (unsigned operationIndex : resolveDriveOperations) {
        auto drive = cast<sim::SimDriverDriveOp>(
            memberOperations[candidateIndex][operationIndex]);
        Value driver = mapping->lookupOrNull(drive.getDriver());
        if (!driver)
          return bail();
        Value value = sim::SimDriverReadOp::create(
            builder, drive.getLoc(), drive.getValue().getType(), driver);
        auto resolver = sim::SimDriverDriveChangedOp::create(
            builder, drive.getLoc(), driver, value);
        resolver->setAttrs(drive->getAttrs());
        if (std::optional<ExactDriverSlice> exact =
                resolveExactDriverSlice(driver)) {
          ::obelisk::schedule::set<::obelisk::schedule::Field::ExactDriverId>(
              resolver, builder.getI64IntegerAttr(exact->id));
          ::obelisk::schedule::set<::obelisk::schedule::Field::ExactDriverLow>(
              resolver, builder.getI64IntegerAttr(exact->lowBit));
        }
        publishedChanged = arith::OrIOp::create(
            builder, drive.getLoc(), publishedChanged, resolver.getChanged());
      }
      cf::BranchOp::create(builder, kernel.getLoc(), afterResolve,
                           ValueRange{publishedChanged});
      builder.setInsertionPointToStart(afterResolve);
      changed = resolvedChanged;
    }
    for (unsigned operationIndex : deferredDriveOperations) {
      auto drive = cast<sim::SimDriverDriveInertialOp>(
          memberOperations[candidateIndex][operationIndex]);
      IRMapping driveMapping;
      Value driver = mapping->lookupOrNull(drive.getDriver());
      if (!driver)
        return bail();
      driveMapping.map(drive.getDriver(), driver);
      driveMapping.map(drive.getValue(), call.getResult(resultIndex++));
      for (Value delay : {drive.getRiseDelay(), drive.getFallDelay(),
                          drive.getTurnoffDelay()}) {
        Value mapped = driveMapping.lookupOrNull(delay);
        if (!mapped) {
          auto constant = delay.getDefiningOp<sim::SimTimeConstantOp>();
          if (!constant)
            return bail();
          Operation *cloned = builder.clone(*constant, driveMapping);
          mapped = cloned->getResult(0);
        }
      }
      builder.clone(*drive, driveMapping);
    }
    if (resultIndex + 1 != call.getNumResults())
      return bail();
    Value nextValue = currentMask;
    if (downstreamMasks[candidateIndex] != 0) {
      Value downstream = arith::ConstantOp::create(
          builder, kernel.getLoc(), maskType,
          builder.getI64IntegerAttr(downstreamMasks[candidateIndex]));
      Value propagated = arith::OrIOp::create(builder, kernel.getLoc(),
                                              currentMask, downstream);
      nextValue = arith::SelectOp::create(builder, kernel.getLoc(), changed,
                                          propagated, currentMask);
    }
    SmallVector<Value> updatedStates(currentStates);
    for (auto [index, state] : llvm::enumerate(
             call.getResults().take_front(candidate.nextState.size())))
      updatedStates[stateOffset + index] = state;
    SmallVector<Value> nextOperands{nextValue};
    llvm::append_range(nextOperands, updatedStates);
    cf::BranchOp::create(builder, kernel.getLoc(), next, nextOperands);
    test = next;
    currentMask = nextMask;
    currentStates.assign(nextStates.begin(), nextStates.end());
  }
  builder.setInsertionPointToEnd(test);
  cf::BranchOp::create(builder, kernel.getLoc(), wait);

  SmallVector<Value> watched;
  SmallVector<int32_t> edges;
  for (auto [candidate, mapping] : llvm::zip_equal(candidates, mappings)) {
    if (auto change = dyn_cast<sim::SimSuspendChangeOp>(candidate.suspend)) {
      watched.push_back(mapping->lookup(change.getWatched()));
      edges.push_back(static_cast<int32_t>(sim::EdgeKind::Change));
    } else {
      auto any = cast<sim::SimSuspendAnyOp>(candidate.suspend);
      for (auto [value, edge] :
           llvm::zip_equal(any.getWatched(), any.getEdges())) {
        watched.push_back(mapping->lookup(value));
        edges.push_back(edge);
      }
    }
  }
  builder.setInsertionPointToStart(wait);
  Value resumed =
      arith::ConstantOp::create(builder, kernel.getLoc(), builder.getI1Type(),
                                builder.getBoolAttr(false));
  SmallVector<Value> waitOperands(watched);
  waitOperands.push_back(resumed);
  for (const Watch &watch : watchSnapshots) {
    Value snapshot = loadWatched(builder, kernel.getLoc(), watch.handle);
    if (!snapshot)
      return bail();
    waitOperands.push_back(snapshot);
  }
  llvm::append_range(waitOperands, currentStates);
  sim::SimSuspendAnyOp::create(builder, kernel.getLoc(), waitOperands,
                               builder.getDenseI32ArrayAttr(edges),
                               schedule::ContinuationSiteAttr{},
                               sim::EventRegionAttr{}, body);

  builder.setInsertionPoint(insertionSpawn);
  sim::SimSpawnOp::create(builder, kernel.getLoc(), kernel.getSymNameAttr(),
                          operands, ArrayAttr{}, ArrayAttr{});
  for (Candidate &candidate : candidates)
    candidate.spawn.erase();
  for (Candidate &candidate : candidates) {
    inputIndex.retargetCoverageKeepalives(candidate.function, kernel);
    symbols.erase(candidate.function);
  }
  return kernel;
}

FailureOr<sim::SimFuncOp> materializeFusion(
    sim::SimDesignOp design, SymbolTable &symbols, CodeUnitIndex &codeUnits,
    FusionInputIndex &inputIndex, schedule::ComputeFusionAttr fusion,
    schedule::ComputeGraphAttr graph,
    const analysis::DescriptorProvenanceAnalysis &provenance,
    const DenseMap<uint32_t, uint32_t> &scheduleOrder,
    const DenseMap<uint32_t, uint32_t> &resumeTargets,
    const DenseMap<StringAttr, uint32_t> &entryOrder,
    const DenseMap<StringAttr, SmallVector<sim::SimSpawnOp>> &spawnsByCallee,
    bool evalBodyFusion, uint64_t &eliminatedTerminationPolls,
    uint64_t &ifConvertedNBAs, uint64_t &sharedStableConditions,
    uint64_t &promotedPrivateStores,
    std::unique_ptr<PrivateStaticAccessIndex> &accessIndex,
    llvm::StringMap<uint64_t> &rejections) {
  // Tally the reason instead of emitting a diagnostic per rejection: a remark
  // here is attached to design IR and would serialize the module once per
  // rejected cohort. The caller reports the distribution once.
  auto rejectEval = [&](StringRef reason) -> FailureOr<sim::SimFuncOp> {
    ++rejections[reason];
    return failure();
  };
  SmallVector<BodyFusionCandidate, 4> candidates;
  schedule::ComputeEffectAttr commonSensitivity;
  for (int64_t member : fusion.getFragments().asArrayRef()) {
    if (member < 0 || static_cast<uint64_t>(member) >= graph.getNodes().size())
      return rejectEval("invalid member");
    auto fragment = dyn_cast<schedule::ComputeFragmentAttr>(
        graph.getNodes()[static_cast<size_t>(member)]);
    if (!fragment)
      return rejectEval("member is not a fragment");
    schedule::ComputeEffectAttr sensitivity = getDirectSensitivity(fragment);
    if (!sensitivity || (commonSensitivity && sensitivity != commonSensitivity))
      return rejectEval("sensitivity mismatch");
    commonSensitivity = sensitivity;
    sim::SimFuncOp function =
        symbols.lookup<sim::SimFuncOp>(fragment.getFunction().getValue());
    auto spawns = function ? spawnsByCallee.find(function.getSymNameAttr())
                           : spawnsByCallee.end();
    auto resume = resumeTargets.find(static_cast<uint32_t>(member));
    sim::SimFuncOp spawningFunction =
        function && spawns != spawnsByCallee.end() && spawns->second.size() == 1
            ? spawns->second.front()->getParentOfType<sim::SimFuncOp>()
            : sim::SimFuncOp{};
    if (!function || !isSupportedEntryKind(function.getEntryKind()) ||
        !isComputeBodyFusionEligible(function, provenance) ||
        spawns == spawnsByCallee.end() || spawns->second.size() != 1 ||
        !spawningFunction ||
        spawningFunction.getEntryKind() != sim::EntryKind::RootInitializer ||
        !spawns->second.front()->getResult(0).use_empty() ||
        resume == resumeTargets.end())
      return rejectEval("actor/spawn/resume eligibility");

    Block *wait = nullptr;
    uint32_t blockIndex = 0;
    for (Block &block : function.getBody()) {
      if (blockIndex++ == fragment.getBlock()) {
        wait = &block;
        break;
      }
    }
    bool coverageWait =
        wait &&
        llvm::all_of(wait->without_terminator(), [](Operation &operation) {
          return isa<arith::ConstantOp, sim::SimCoveragePointHitOp>(operation);
        });
    if (!coverageWait || !isTypedDirectWait(wait->getTerminator()) ||
        wait->getNumSuccessors() != 1 ||
        !hasOnlyPureEntryPreamble(function, wait)) {
      return rejectEval("wait shape");
    }
    unsigned suspensionCount = 0;
    function.walk([&](Operation *operation) {
      suspensionCount += isTypedSuspend(operation);
    });
    if (suspensionCount != 1)
      return rejectEval("multiple suspensions");

    BodyFusionCandidate candidate;
    candidate.function = function;
    candidate.instanceScope = codeUnits.getScope(function).value_or(0);
    candidate.spawn = spawns->second.front();
    candidate.wait = wait;
    candidate.body = wait->getSuccessor(0);
    auto entryBranch = cast<cf::BranchOp>(
        candidate.function.getBody().front().getTerminator());
    candidate.threadedEntryValues.append(entryBranch.getDestOperands().begin(),
                                         entryBranch.getDestOperands().end());
    if (candidate.threadedEntryValues.size() != wait->getNumArguments() ||
        candidate.body->getNumArguments() != wait->getNumArguments())
      return rejectEval("threaded wait/body arity mismatch");
    if (!hasInvariantWaitArguments(candidate))
      return rejectEval("changing wait arguments");
    candidate.resumeTarget = resume->second;
    auto functionEntry = entryOrder.find(function.getSymNameAttr());
    if (functionEntry == entryOrder.end())
      return rejectEval("missing entry order");
    candidate.entryOrder = functionEntry->second;
    if (!collectBodyBlocks(candidate) || !hasOnlyTerminationReturns(candidate))
      return rejectEval("body reachability/return shape");
    collectLiveEntryPreamble(candidate);
    candidates.push_back(std::move(candidate));
  }
  if (candidates.size() < 2)
    return rejectEval("too few candidates");
  for (BodyFusionCandidate &candidate : candidates) {
    auto order = scheduleOrder.find(candidate.resumeTarget);
    if (order == scheduleOrder.end())
      return rejectEval("missing ready order");
    candidate.resumeOrder = order->second;
  }
  llvm::sort(candidates, [](const auto &lhs, const auto &rhs) {
    return lhs.resumeOrder < rhs.resumeOrder;
  });

  {
    uint64_t instanceScope = candidates.front().instanceScope;
    sim::EventRegion homeRegion = candidates.front().function.getHomeRegion();
    sim::ExecutionDomain domain = candidates.front().function.getDomain();
    if (llvm::any_of(candidates, [&](BodyFusionCandidate &candidate) {
          return candidate.instanceScope != instanceScope ||
                 candidate.function.getHomeRegion() != homeRegion ||
                 candidate.function.getDomain() != domain;
        }))
      return rejectEval("members cross an elaborated instance or domain");
  }

  // IEEE 1800-2023 4.6-4.7 permit choosing these same-trigger Active events
  // consecutively, even if an earlier member wakes an outside consumer. Keep
  // every publication and enqueue; the common loop runs those consumers after
  // this activation. This changes only cross-process race ordering.
  //
  // A member must not change the cohort's own trigger: an earlier member can
  // already be waiting again when a later member publishes that transition.
  // Collapsing their rearm points would lose that activation. NBA edges are
  // different: commits occur only after all Active work has returned.
  llvm::SmallDenseSet<uint32_t> candidateFragments;
  for (BodyFusionCandidate &candidate : candidates) {
    auto found = inputIndex.fragments.find(candidate.function.getSymNameAttr());
    if (found == inputIndex.fragments.end())
      continue;
    for (uint32_t index : found->second) {
      auto fragment =
          cast<schedule::ComputeFragmentAttr>(graph.getNodes()[index]);
      if (fragment.getTier() != schedule::ComputeTierKind::Native)
        return rejectEval("non-native member fragment");
      candidateFragments.insert(index);
    }
  }
  for (uint32_t fragment : candidateFragments) {
    auto outgoing = inputIndex.outgoingEdges.find(fragment);
    if (outgoing == inputIndex.outgoingEdges.end())
      continue;
    for (schedule::ComputeEdgeAttr edge : outgoing->second) {
      if (edge.getKind() == schedule::ComputeEdgeKind::Spawn)
        return rejectEval("spawn within cohort");
      if (edge.getKind() == schedule::ComputeEdgeKind::Sensitivity &&
          candidateFragments.contains(edge.getTarget()))
        return rejectEval("cohort changes its own trigger");
    }
  }

  // Pure entry preambles and unobserved root-spawn handles allow the common
  // wait to be registered once. Eligibility excludes task/control boundaries;
  // always_comb/always_latch startup and sensitivity retain their own path.

  SmallVector<Value> operands;
  SmallVector<Type> inputTypes;
  SmallVector<DictionaryAttr> argumentAttrs;
  DenseMap<Value, unsigned> operandIndices;
  sim::SimSpawnOp insertionSpawn = candidates.front().spawn;
  for (BodyFusionCandidate &candidate : candidates) {
    if (candidate.spawn->getBlock() != insertionSpawn->getBlock())
      return rejectEval("spawns in different blocks");
    if (insertionSpawn->isBeforeInBlock(candidate.spawn))
      insertionSpawn = candidate.spawn;
    Block &entry = candidate.function.getBody().front();
    if (entry.getNumArguments() != candidate.spawn.getNumOperands())
      return rejectEval("spawn arity mismatch");
    for (auto [argument, operand] :
         llvm::zip_equal(entry.getArguments(), candidate.spawn.getOperands())) {
      auto [found, inserted] =
          operandIndices.try_emplace(operand, operands.size());
      unsigned index = found->second;
      DictionaryAttr attrs =
          candidate.function.getArgAttrDict(argument.getArgNumber());
      if (inserted) {
        operands.push_back(operand);
        inputTypes.push_back(argument.getType());
        argumentAttrs.push_back(attrs);
      } else if (inputTypes[index] != argument.getType() ||
                 argumentAttrs[index] != attrs) {
        return rejectEval("incompatible shared capture");
      }
      candidate.fusedArguments.push_back(index);
    }
  }

  sim::SimFuncOp first = candidates.front().function;
  unsigned symbolCounter = 0;
  SmallString<32> symbolBase;
  ("__obelisk_fused_" + Twine(fusion.getId())).toVector(symbolBase);
  SmallString<32> name = SymbolTable::generateSymbolName<32>(
      symbolBase,
      [&](StringRef candidate) { return symbols.lookup(candidate) != nullptr; },
      symbolCounter);
  SmallVector<NamedAttribute> fusedAttributes;
  if (IntegerAttr codeUnit = first.getCodeUnitIdAttr())
    fusedAttributes.emplace_back(first.getCodeUnitIdAttrName(), codeUnit);
  OpBuilder builder = OpBuilder::atBlockEnd(&design.getBody().front());
  sim::SimFuncOp fused = sim::SimFuncOp::create(
      builder, first.getLoc(), name,
      FunctionType::get(design.getContext(), inputTypes, TypeRange{}),
      first.getEntryKind(), fusedAttributes, argumentAttrs);
  symbols.insert(fused);
  SymbolTable::setSymbolVisibility(fused, SymbolTable::Visibility::Private);
  ::obelisk::schedule::set<schedule::metadata::nativeRegionBody>(
      fused, builder.getUnitAttr());
  ::obelisk::schedule::set<
      schedule::metadata::evalReconstructsContinuationArgs>(
      fused, builder.getUnitAttr());
  ::obelisk::schedule::set<::obelisk::schedule::Field::EvalFusionGroup>(
      fused, builder.getI32IntegerAttr(fusion.getId()));
  // Preserve typed source identities across graph rebuilding. Native eval
  // lowering resolves these actor continuations into the new graph's fragment
  // inventory; it never compares the old fragment ordinals directly.
  SmallVector<Attribute> sourceOwners;
  sourceOwners.reserve(candidates.size());
  for (BodyFusionCandidate &candidate : candidates) {
    schedule::ContinuationSiteAttr site;
    if (auto suspend =
            dyn_cast<sim::SimSuspendChangeOp>(candidate.wait->getTerminator()))
      site = suspend.getSiteAttr();
    else if (auto suspend = dyn_cast<sim::SimSuspendEdgeOp>(
                 candidate.wait->getTerminator()))
      site = suspend.getSiteAttr();
    if (!site)
      return rejectEval("source owner has no stable continuation");
    auto source =
        inputIndex.sourceCodeUnits.find(candidate.function.getSymNameAttr());
    IntegerAttr codeUnit = source == inputIndex.sourceCodeUnits.end()
                               ? candidate.function.getCodeUnitIdAttr()
                               : source->second;
    if (!codeUnit)
      return rejectEval("source owner has no stable code unit");
    sourceOwners.push_back(schedule::SourceOwnerAttr::get(
        builder.getContext(), codeUnit,
        builder.getI32IntegerAttr(site.getId())));
  }
  ::obelisk::schedule::set<::obelisk::schedule::Field::EvalSourceOwners>(
      fused, builder.getArrayAttr(sourceOwners));
  // This closed-world fused activation cannot call foreign code or suspend
  // while its body is running. Mark it so native lowering can prove which NBA
  // sites are safe in the clean body selected by AOT actor dispatch.
  if (analysis::SimulationVPIAnalysis::compute(design).allowsWrite())
    ::obelisk::schedule::set<
        schedule::metadata::nativeGuardedSpecializationBody>(
        fused, builder.getUnitAttr());

  Block &entry = fused.getBody().front();
  Block *wait = new Block;
  fused.getBody().push_back(wait);
  SmallVector<std::unique_ptr<IRMapping>> mappings;
  mappings.reserve(candidates.size());
  SmallVector<DenseMap<Block *, Block *>> clonedBlocks(candidates.size());
  for (auto [candidateIndex, candidate] : llvm::enumerate(candidates)) {
    auto mapping = std::make_unique<IRMapping>();
    for (auto [argument, fusedIndex] :
         llvm::zip_equal(candidate.function.getBody().front().getArguments(),
                         candidate.fusedArguments))
      mapping->map(argument, entry.getArgument(fusedIndex));
    for (Block *source : candidate.bodyBlocks) {
      Block *cloned = new Block;
      fused.getBody().push_back(cloned);
      mapping->map(source, cloned);
      if (source == candidate.body && !candidate.threadedEntryValues.empty()) {
        clonedBlocks[candidateIndex][source] = cloned;
        continue;
      }
      for (BlockArgument argument : source->getArguments()) {
        BlockArgument clonedArgument =
            cloned->addArgument(argument.getType(), argument.getLoc());
        mapping->map(argument, clonedArgument);
      }
      clonedBlocks[candidateIndex][source] = cloned;
    }
    mappings.push_back(std::move(mapping));
  }
  for (auto [index, candidate] : llvm::enumerate(candidates)) {
    Block *next =
        index + 1 == candidates.size()
            ? wait
            : clonedBlocks[index + 1].lookup(candidates[index + 1].body);
    mappings[index]->map(candidate.wait, next);
  }

  auto applySourceOwner = [](Operation *root, Attribute sourceOwner) {
    root->walk([&](Operation *operation) {
      if (!::obelisk::schedule::has<schedule::metadata::evalSourceOwner>(
              operation))
        ::obelisk::schedule::set<schedule::metadata::evalSourceOwner>(
            operation, cast<schedule::SourceOwnerAttr>(sourceOwner));
    });
  };

  builder.setInsertionPointToStart(&entry);
  for (auto [candidateIndex, pair] :
       llvm::enumerate(llvm::zip_equal(candidates, mappings))) {
    auto &[candidate, mapping] = pair;
    for (Operation *operation : candidate.entryPreamble) {
      Operation *cloned = builder.clone(*operation, *mapping);
      applySourceOwner(cloned, sourceOwners[candidateIndex]);
    }
  }
  for (auto [candidate, mapping] : llvm::zip_equal(candidates, mappings)) {
    for (auto [argument, value] : llvm::zip_equal(
             candidate.wait->getArguments(), candidate.threadedEntryValues))
      mapping->map(argument, mapping->lookup(value));
  }
  cf::BranchOp::create(builder, fused.getLoc(), wait);

  builder.setInsertionPointToStart(wait);
  // Each original actor executes its wait instrumentation at bootstrap and
  // after an activation. Keep those hits in the shared suspension block.
  for (auto [candidate, mapping] : llvm::zip_equal(candidates, mappings))
    for (Operation &operation : candidate.wait->without_terminator())
      builder.clone(operation, *mapping);
  // A forwarded value may also be a constant defined in the wait block.
  // Reconstruct activation arguments only after cloning that preamble.
  for (auto [candidate, mapping] : llvm::zip_equal(candidates, mappings)) {
    auto forwarded = cast<BranchOpInterface>(candidate.wait->getTerminator())
                         .getSuccessorOperands(0)
                         .getForwardedOperands();
    for (auto [argument, value] :
         llvm::zip_equal(candidate.body->getArguments(), forwarded))
      mapping->map(argument, mapping->lookup(value));
  }
  Operation *fusedWait = builder.clone(
      *candidates.front().wait->getTerminator(), *mappings.front());
  cast<BranchOpInterface>(fusedWait)
      .getSuccessorOperands(0)
      .getMutableForwardedOperands()
      .clear();
  for (auto [candidateIndex, pair] :
       llvm::enumerate(llvm::zip_equal(candidates, mappings))) {
    auto &[candidate, mapping] = pair;
    for (Block *source : candidate.bodyBlocks) {
      Block *destination = mapping->lookup(source);
      builder.setInsertionPointToEnd(destination);
      for (Operation &operation : *source) {
        Operation *cloned = builder.clone(operation, *mapping);
        if (auto branch = dyn_cast<BranchOpInterface>(cloned))
          for (auto [index, successor] :
               llvm::enumerate(operation.getSuccessors()))
            if (successor == candidate.wait)
              branch.getSuccessorOperands(index)
                  .getMutableForwardedOperands()
                  .clear();
        applySourceOwner(cloned, sourceOwners[candidateIndex]);
      }
    }
  }

  // LowerUnit inserts a termination poll after every direct function call.
  // Inlining intentionally leaves that control boundary behind because a
  // general callee may request termination. Compute-body fusion has a stronger
  // closed-world proof: every operation in every transitive callee was checked
  // by isComputeBodyFusionEligible, which excludes finish, stop, fatal, task
  // calls, and every other scheduler-writing operation. The scheduler also
  // never starts ordinary Active work after a pre-existing termination
  // request. Consequently these post-inline polls are invariantly false for
  // the duration of the fused activation.
  //
  // Remove the return diamonds here, before rebuilding the compute graph. In
  // addition to avoiding runtime scheduler reads, this joins the arithmetic
  // into larger basic blocks that downstream scalar and vector optimizers can
  // analyze together.
  SmallVector<cf::CondBranchOp> redundantPolls;
  fused.walk([&](cf::CondBranchOp branch) {
    auto requested =
        branch.getCondition().getDefiningOp<sim::SimTerminationRequestedOp>();
    if (!requested ||
        !isa<sim::SimReturnOp>(branch.getTrueDest()->getTerminator()))
      return;
    redundantPolls.push_back(branch);
  });
  for (cf::CondBranchOp branch : redundantPolls) {
    sim::SimTerminationRequestedOp requested =
        branch.getCondition().getDefiningOp<sim::SimTerminationRequestedOp>();
    Block *source = branch->getBlock();
    Block *continuation = branch.getFalseDest();
    bool canMerge = llvm::hasSingleElement(continuation->getPredecessors()) &&
                    continuation->getNumArguments() == 0 &&
                    branch.getFalseDestOperands().empty();
    branch.erase();
    if (requested->use_empty())
      requested.erase();
    if (canMerge) {
      source->getOperations().splice(source->end(),
                                     continuation->getOperations());
      continuation->erase();
    } else {
      builder.setInsertionPointToEnd(source);
      cf::BranchOp::create(builder, fused.getLoc(), continuation, ValueRange{});
    }
    ++eliminatedTerminationPolls;
  }

  builder.setInsertionPoint(insertionSpawn);
  sim::SimFuncOp spawningFunction =
      insertionSpawn->getParentOfType<sim::SimFuncOp>();
  sim::SimSpawnOp::create(builder, fused.getLoc(), fused.getSymNameAttr(),
                          operands, ArrayAttr{}, ArrayAttr{});
  for (BodyFusionCandidate &candidate : candidates)
    candidate.spawn.erase();
  for (BodyFusionCandidate &candidate : candidates) {
    if (accessIndex)
      accessIndex->erase(candidate.function);
    inputIndex.retargetCoverageKeepalives(candidate.function, fused);
    symbols.erase(candidate.function);
  }

  // Remove private activation temporaries before if-converting NBA diamonds.
  // Besides avoiding canonical state publication, this turns overwrite-arm
  // loads into SSA values so only genuinely speculatable arithmetic is moved
  // out of the branch.
  if (!accessIndex)
    accessIndex = std::make_unique<PrivateStaticAccessIndex>(design);
  else {
    accessIndex->refresh(spawningFunction);
    accessIndex->refresh(fused);
  }
  promotedPrivateStores += promotePrivateStaticTemporaries(fused, *accessIndex);
  ifConvertedNBAs += ifConvertConditionalNBAWrites(
      fused, wait, analysis::NBAMergeSafety(design));
  sharedStableConditions += shareStableBranchConditions(
      fused, clonedBlocks.front().lookup(candidates.front().body));

  // The true arms above are now unreachable single-return blocks. Erase only
  // blocks with no predecessors; any unexpected structure remains intact and
  // will be validated by the rebuilt graph.
  for (auto block = fused.getBody().begin(), end = fused.getBody().end();
       block != end;) {
    Block &current = *block++;
    if (&current != &entry && &current != wait && current.hasNoPredecessors() &&
        current.without_terminator().empty() &&
        isa<sim::SimReturnOp>(current.getTerminator()))
      current.erase();
  }

  if (evalBodyFusion) {
    SmallString<40> evalBase;
    (fused.getSymName() + ".__obelisk_eval_body").toVector(evalBase);
    unsigned evalCounter = 0;
    SmallString<40> evalName = SymbolTable::generateSymbolName<40>(
        evalBase,
        [&](StringRef candidate) {
          return symbols.lookup(candidate) != nullptr;
        },
        evalCounter);
    builder.setInsertionPointToEnd(&design.getBody().front());
    uint64_t evalScope = candidates.front().instanceScope;
    uint64_t evalCodeUnit = codeUnits.allocate(evalScope);
    sim::SimCodeUnitDeclOp::create(
        builder, fused.getLoc(), evalCodeUnit, evalScope,
        sim::EntryKind::Function, builder.getStringAttr(evalName),
        builder.getStringAttr("generated native eval body"),
        builder.getUnitAttr());
    SmallVector<NamedAttribute> evalAttributes{builder.getNamedAttr(
        "code_unit_id", builder.getI64IntegerAttr(evalCodeUnit))};
    sim::SimFuncOp evalBody = sim::SimFuncOp::create(
        builder, fused.getLoc(), evalName,
        FunctionType::get(design.getContext(), inputTypes, TypeRange{}),
        sim::EntryKind::Function, evalAttributes, argumentAttrs);
    symbols.insert(evalBody);
    ::obelisk::schedule::set<::obelisk::schedule::Field::EvalBorrowedCaptures>(
        evalBody, builder.getUnitAttr());
    ::obelisk::schedule::set<::obelisk::schedule::Field::EvalRawCaptures>(
        evalBody, builder.getUnitAttr());
    ::obelisk::schedule::set<
        ::obelisk::schedule::Field::EvalInstanceCoordinator>(
        evalBody, builder.getUnitAttr());
    ::obelisk::schedule::set<::obelisk::schedule::Field::EvalFusionGroup>(
        evalBody,
        ::obelisk::schedule::get<::obelisk::schedule::Field::EvalFusionGroup>(
            fused));
    ::obelisk::schedule::set<::obelisk::schedule::Field::EvalSourceOwners>(
        evalBody,
        ::obelisk::schedule::get<::obelisk::schedule::Field::EvalSourceOwners>(
            fused));
    schedule::ContinuationSiteAttr activationSite;
    if (auto suspend = dyn_cast<sim::SimSuspendChangeOp>(wait->getTerminator()))
      activationSite = suspend.getSiteAttr();
    else if (auto suspend =
                 dyn_cast<sim::SimSuspendEdgeOp>(wait->getTerminator()))
      activationSite = suspend.getSiteAttr();
    else if (auto suspend =
                 dyn_cast<sim::SimSuspendAnyOp>(wait->getTerminator()))
      activationSite = suspend.getSiteAttr();
    else if (auto suspend =
                 dyn_cast<sim::SimSuspendObserveOp>(wait->getTerminator()))
      activationSite = suspend.getSiteAttr();
    if (!activationSite || activationSite.getId() == 0) {
      symbols.erase(evalBody);
      return fused;
    }
    // The source suspension may be erased by later fusion and CFG cleanup.
    // Carry its stable identity on the generated body so native scheduling
    // never has to retain or dereference transformation-owned operations.
    ::obelisk::schedule::set<::obelisk::schedule::Field::EvalContinuation>(
        evalBody, builder.getI32IntegerAttr(activationSite.getId()));
    SymbolTable::setSymbolVisibility(evalBody,
                                     SymbolTable::Visibility::Private);

    // The fusion plan is partitioned by elaborated instance.  Clone the
    // instance's scheduled activation bodies into one owner so the native
    // backend can optimize across process boundaries just as it can across
    // ordinary inlined module methods.  Large helpers within those bodies
    // remain subject to the normal inliner profitability model.
    Block &evalEntry = evalBody.getBody().front();
    IRMapping evalMapping;
    for (auto [source, destination] : llvm::zip_equal(
             fused.getBody().front().getArguments(), evalEntry.getArguments()))
      evalMapping.map(source, destination);
    builder.setInsertionPointToStart(&evalEntry);
    for (Operation &operation : fused.getBody().front().without_terminator())
      if (!isa<sim::SimCoveragePointHitOp>(operation))
        builder.clone(operation, evalMapping);
    // The shared wait may define constants forwarded into an activation.
    // Recreate them in the eval entry so cloned bodies never capture a value
    // from the original coroutine region. Coverage still belongs at rearm.
    for (Operation &operation : wait->without_terminator())
      if (isa<arith::ConstantOp>(operation))
        builder.clone(operation, evalMapping);

    Block *activation = wait->getSuccessor(0);
    SmallVector<Block *> activationBlocks;
    SmallVector<Block *> pending{activation};
    llvm::SmallPtrSet<Block *, 32> seen;
    while (!pending.empty()) {
      Block *source = pending.pop_back_val();
      if (source == wait || !seen.insert(source).second)
        continue;
      activationBlocks.push_back(source);
      for (Block *successor : source->getSuccessors())
        if (successor != wait)
          pending.push_back(successor);
    }
    for (Block *source : activationBlocks) {
      Block *destination = new Block;
      evalBody.getBody().push_back(destination);
      evalMapping.map(source, destination);
      for (BlockArgument argument : source->getArguments())
        evalMapping.map(argument, destination->addArgument(argument.getType(),
                                                           argument.getLoc()));
    }
    builder.setInsertionPointToEnd(&evalEntry);
    auto forwarded = cast<BranchOpInterface>(wait->getTerminator())
                         .getSuccessorOperands(0)
                         .getForwardedOperands();
    SmallVector<Value> entryOperands;
    for (Value value : forwarded)
      entryOperands.push_back(evalMapping.lookup(value));
    cf::BranchOp::create(builder, fused.getLoc(),
                         evalMapping.lookup(activation), entryOperands);

    bool cloneSupported = true;
    for (Block *source : activationBlocks) {
      builder.setInsertionPointToEnd(evalMapping.lookup(source));
      for (Operation &operation : *source) {
        if (&operation == source->getTerminator()) {
          if (auto branch = dyn_cast<cf::BranchOp>(operation);
              branch && branch.getDest() == wait) {
            for (Operation &waitOperation : wait->without_terminator())
              builder.clone(waitOperation, evalMapping);
            sim::SimReturnOp::create(builder, branch.getLoc(), ValueRange{});
            continue;
          }
          if (auto branch = dyn_cast<cf::CondBranchOp>(operation)) {
            bool trueWait = branch.getTrueDest() == wait;
            bool falseWait = branch.getFalseDest() == wait;
            if (trueWait || falseWait) {
              if (trueWait && falseWait) {
                for (Operation &waitOperation : wait->without_terminator())
                  builder.clone(waitOperation, evalMapping);
                sim::SimReturnOp::create(builder, branch.getLoc(),
                                         ValueRange{});
                continue;
              }
              Block *returnBlock = new Block;
              evalBody.getBody().push_back(returnBlock);
              OpBuilder returnBuilder = OpBuilder::atBlockEnd(returnBlock);
              for (Operation &waitOperation : wait->without_terminator())
                returnBuilder.clone(waitOperation, evalMapping);
              sim::SimReturnOp::create(returnBuilder, branch.getLoc(),
                                       ValueRange{});
              SmallVector<Value> trueOperands;
              SmallVector<Value> falseOperands;
              for (Value value : branch.getTrueDestOperands())
                trueOperands.push_back(evalMapping.lookup(value));
              for (Value value : branch.getFalseDestOperands())
                falseOperands.push_back(evalMapping.lookup(value));
              cf::CondBranchOp::create(
                  builder, branch.getLoc(),
                  evalMapping.lookup(branch.getCondition()),
                  trueWait ? returnBlock
                           : evalMapping.lookup(branch.getTrueDest()),
                  trueWait ? ValueRange{} : ValueRange{trueOperands},
                  falseWait ? returnBlock
                            : evalMapping.lookup(branch.getFalseDest()),
                  falseWait ? ValueRange{} : ValueRange{falseOperands});
              continue;
            }
          }
          if (llvm::is_contained(operation.getSuccessors(), wait)) {
            cloneSupported = false;
            break;
          }
        }
        builder.clone(operation, evalMapping);
      }
      if (!cloneSupported)
        break;
    }
    if (!cloneSupported) {
      symbols.erase(evalBody);
    } else {
      preserveEvalNBASiteOrigins(evalBody);
      ::obelisk::schedule::set<::obelisk::schedule::Field::EvalBody>(
          fused, FlatSymbolRefAttr::get(evalBody.getSymNameAttr()));
      accessIndex->refresh(evalBody);
    }
  }

  accessIndex->refresh(fused);
  return fused;
}

void ObeliskSimMaterializeComputeFusionPass::runOnOperation() {
  sim::SimDesignOp design = getOperation();
  // One mutable index serves every cohort and standalone activation. Keep
  // creations and rejected clones in sync so lookup is independent of the
  // total design size without retaining erased operations.
  SymbolTable symbols(design);
  // Preserve smallest-free-ID allocation, including rejected helper rollback,
  // without rebuilding the complete declaration inventory for each clone.
  CodeUnitIndex codeUnits(design);
  // Fusion changes function bodies but never driver declarations. Reuse the
  // immutable driver-to-net index while deriving fresh per-body value facts.
  analysis::DescriptorProvenanceAnalysis provenance(design);
  DenseSet<uint64_t> controlTargets;
  bool foreignControl = false;
  design.walk([&](Operation *operation) {
    if (auto disable = dyn_cast<sim::SimControlDisableOp>(operation))
      controlTargets.insert(disable.getTargetId());
    foreignControl |= isa<sim::SimDPICallOp>(operation);
    // Export capability is not a foreign call. An external invocation enters
    // at a runtime boundary, where canonical actors retain these scopes; it
    // cannot interleave with a closed zero-time evaluator activation.
  });
  bool forgedDiscardableStore = false;
  design.walk([&](sim::SimRefStoreOp store) {
    forgedDiscardableStore |=
        ::obelisk::schedule::has<schedule::metadata::evalDiscardableStore>(
            store);
  });
  if (forgedDiscardableStore) {
    design.emitOpError("contains a preexisting internal eval-store proof");
    signalPassFailure();
    return;
  }
  ArrayAttr fusions =
      ::obelisk::schedule::get<schedule::metadata::staticBodyFusion>(design);
  schedule::ComputeGraphAttr graph = design.getComputeGraphAttr();
  bool evalScheduler = useEvalBodyFusion(design);
  // Inventory NBA transient observers while every process still has its
  // source shape. Fusion merges bodies and their waits, which would hide an
  // idempotent observer inside a larger activation.
  if (!::obelisk::schedule::has<schedule::metadata::nbaTransientObservable>(
          design))
    if (std::optional<NBATransientObservers> observers =
            computeNBATransientObservers(design)) {
      auto sorted = [&](const llvm::DenseSet<uint64_t> &set) {
        SmallVector<int64_t> values(set.begin(), set.end());
        llvm::sort(values);
        return DenseI64ArrayAttr::get(design.getContext(), values);
      };
      ::obelisk::schedule::set<schedule::metadata::nbaTransientObservable>(
          design, sorted(observers->observable));
      ::obelisk::schedule::set<schedule::metadata::nbaChangeWatched>(
          design, sorted(observers->changeWatched));
    }
  // Graph rebuilding renumbers the canonical actors as well as their eval
  // clones. Preserve both sides before fusion changes traversal order;
  // tagging only the clone makes one source NBA look like two distinct sites.
  if (evalScheduler)
    for (sim::SimFuncOp function :
         design.getBody().front().getOps<sim::SimFuncOp>())
      preserveEvalNBASiteOrigins(function);
  analysis::SimulationVPIAnalysis vpi =
      analysis::SimulationVPIAnalysis::compute(design);
  bool prepareTier1Promotion = evalScheduler && !vpi.allowsWrite();
  bool prepareDormantTier1 =
      prepareTier1Promotion && vpi.getMode() == schedule::ComputeVPIMode::Read;
  uint64_t preparedPrivateStores = 0;
  if (prepareTier1Promotion) {
    // Promote before any activation cloning.  Off mode erases Invisible
    // publication immediately.  Read mode keeps canonical safe-point stores
    // with a transient tag, while every cloned Tier-1 body inherits enough
    // proof to discard its hot publication afterward.
    SmallVector<sim::SimFuncOp> functions;
    for (sim::SimFuncOp function :
         design.getBody().front().getOps<sim::SimFuncOp>())
      if (!function.isExternal() &&
          !::obelisk::schedule::has<
              ::obelisk::schedule::Field::EvalBorrowedCaptures>(function))
        functions.push_back(function);
    PrivateStaticAccessIndex accessIndex(design);
    for (sim::SimFuncOp function : functions)
      preparedPrivateStores += promotePrivateStaticTemporaries(
          function, accessIndex, prepareDormantTier1);
  }
  if ((!fusions || !graph || graph.getWorkers() != 1) && evalScheduler) {
    // Standalone activation cloning is not conditional on finding a profitable
    // multi-actor fusion.  Keeping it behind the fusion-inventory early return
    // made explicit eval plans depend on an unrelated optimization decision.
    SmallVector<sim::SimFuncOp> actors;
    for (sim::SimFuncOp function :
         design.getBody().front().getOps<sim::SimFuncOp>())
      actors.push_back(function);
    for (sim::SimFuncOp function : actors)
      if (failed(materializeStandaloneEvalBody(design, symbols, codeUnits,
                                               function, controlTargets,
                                               foreignControl))) {
        if (prepareDormantTier1)
          finalizeDormantTier1Stores(design);
        signalPassFailure();
        return;
      }
    if (prepareDormantTier1)
      finalizeDormantTier1Stores(design);
    promotedPrivateStores += preparedPrivateStores;
  }
  if (!fusions || !graph || graph.getWorkers() != 1)
    return;

  DenseMap<uint32_t, uint32_t> scheduleOrder;
  uint32_t nextOrder = 0;
  for (Attribute regionAttribute : graph.getRegions()) {
    auto region = cast<schedule::ComputeRegionAttr>(regionAttribute);
    if (region.getKind() != schedule::ComputeRegionKind::Active)
      continue;
    for (Attribute groupAttribute : region.getGroups())
      for (int64_t member : cast<schedule::ComputeGroupAttr>(groupAttribute)
                                .getFragments()
                                .asArrayRef())
        scheduleOrder[static_cast<uint32_t>(member)] = nextOrder++;
  }

  // These indices describe the frozen input graph, not the progressively
  // rewritten functions. Build them once, preserving each consumer's resume
  // selection and the region's stable source activation order.
  DenseMap<int64_t, int64_t> firstResumeTargets;
  DenseMap<uint32_t, uint32_t> resumeTargets;
  SmallVector<uint32_t> entryTargets;
  for (Attribute attribute : graph.getEdges()) {
    auto edge = cast<schedule::ComputeEdgeAttr>(attribute);
    if (edge.getKind() == schedule::ComputeEdgeKind::Resume) {
      firstResumeTargets.try_emplace(edge.getSource(), edge.getTarget());
      resumeTargets[edge.getSource()] = edge.getTarget();
    }
    if (edge.getKind() == schedule::ComputeEdgeKind::Spawn &&
        scheduleOrder.contains(edge.getTarget()))
      entryTargets.push_back(edge.getTarget());
  }
  llvm::sort(entryTargets, [&](uint32_t lhs, uint32_t rhs) {
    return scheduleOrder.at(lhs) < scheduleOrder.at(rhs);
  });
  entryTargets.erase(std::unique(entryTargets.begin(), entryTargets.end()),
                     entryTargets.end());
  DenseMap<StringAttr, uint32_t> entryOrder;
  for (auto [order, target] : llvm::enumerate(entryTargets)) {
    auto fragment =
        target < graph.getNodes().size()
            ? dyn_cast<schedule::ComputeFragmentAttr>(graph.getNodes()[target])
            : schedule::ComputeFragmentAttr{};
    if (fragment)
      entryOrder.try_emplace(fragment.getFunction().getAttr(),
                             static_cast<uint32_t>(order));
  }

  DenseMap<StringAttr, SmallVector<sim::SimSpawnOp>> spawnsByCallee;
  design.walk([&](sim::SimSpawnOp spawn) {
    spawnsByCallee[spawn.getCalleeAttr().getAttr()].push_back(spawn);
  });

  bool changed = false;
  uint64_t removedPolls = 0;
  uint64_t convertedNBAs = 0;
  uint64_t sharedConditions = 0;
  uint64_t promotedStores = 0;
  std::unique_ptr<PrivateStaticAccessIndex> fusionAccessIndex;
  llvm::StringMap<uint64_t> fusionRejections;
  CombinationalFusionAnalysis combinational(design, provenance);
  FusionInputIndex inputIndex(design, graph);
  for (Attribute attribute : fusions) {
    auto fusion = dyn_cast<schedule::ComputeFusionAttr>(attribute);
    if (!fusion)
      continue;
    bool primitiveContinuous =
        isPrimitiveContinuousFusion(symbols, fusion, graph);
    FailureOr<sim::SimFuncOp> fused = materializeFusion(
        design, symbols, codeUnits, inputIndex, fusion, graph, provenance,
        scheduleOrder, resumeTargets, entryOrder, spawnsByCallee, evalScheduler,
        removedPolls, convertedNBAs, sharedConditions, promotedStores,
        fusionAccessIndex, fusionRejections);
    // The model-wide eval coordinator already owns a fine dirty bit for each
    // ordinary activation, so keep its general straight-line region fusion in
    // the actor scheduler.  A primitive-only cohort is different: replacing
    // a bounded cohort of independent coroutines and eval bodies with one
    // exact union-wait kernel is the forced-native compile-space bound. Its
    // typed source owners preserve the original fine identities for eval
    // handoff.
    if (failed(fused) && (!evalScheduler || primitiveContinuous)) {
      fused = materializeStraightLineKernel(
          design, symbols, codeUnits, inputIndex, fusion, graph, provenance,
          combinational, firstResumeTargets, spawnsByCallee);
      // This path can also outline shared member helpers. Rebuild lazily if a
      // later clocked cohort needs storage proofs; do not retain erased owners
      // or overlook accessors introduced by a different transformation.
      if (succeeded(fused))
        fusionAccessIndex.reset();
    }
    changed |= succeeded(fused);
    if (succeeded(fused))
      ++materializedFusions;
    else
      ++rejectedFusions;
  }
  // A planned cohort that fails to materialize is Tier-1 work lost after
  // planning already accepted it, and the reason was previously discarded.
  if (design->getParentOfType<ModuleOp>()->hasAttr(
          "obelisk.debug.native_timing")) {
    SmallVector<StringRef> ordered;
    for (const auto &entry : fusionRejections)
      ordered.push_back(entry.first());
    llvm::sort(ordered, [&](StringRef lhs, StringRef rhs) {
      return std::make_pair(fusionRejections.lookup(rhs), rhs) <
             std::make_pair(fusionRejections.lookup(lhs), lhs);
    });
    for (StringRef reason : ordered)
      llvm::errs() << "obelisk fusion rejection: count="
                   << fusionRejections.lookup(reason) << " reason=" << reason
                   << '\n';
  }
  // The eval scheduler is a deliberately closed generated-model experiment.
  // Materialize every eligible actor body: selectively retaining coroutine
  // actors here recreates the fine-grained runtime dispatch that this mode is
  // intended to measure without.
  if (evalScheduler) {
    SmallVector<sim::SimFuncOp> actors;
    for (sim::SimFuncOp function :
         design.getBody().front().getOps<sim::SimFuncOp>())
      actors.push_back(function);
    for (sim::SimFuncOp function : actors) {
      // Eligibility is entirely structural. Symbol spelling is an identity
      // and debugging concern; generated bodies must not depend on the
      // frontend's current `unit_N` naming convention.
      if (failed(materializeStandaloneEvalBody(design, symbols, codeUnits,
                                               function, controlTargets,
                                               foreignControl))) {
        if (prepareDormantTier1)
          finalizeDormantTier1Stores(design);
        signalPassFailure();
        return;
      }
    }
  }
  if (prepareDormantTier1)
    finalizeDormantTier1Stores(design);
  eliminatedTerminationPolls += removedPolls;
  ifConvertedNBAs += convertedNBAs;
  sharedStableConditions += sharedConditions;
  promotedPrivateStores += promotedStores + preparedPrivateStores;
  ::obelisk::schedule::remove<schedule::metadata::staticBodyFusion>(design);
  if (!changed)
    return;
  // Body fusion changes call ownership, blocks, and continuation ordinals.
  // Invalidate only metadata derived from that executable CFG before the
  // pipeline performs its late inline round and rebuilds the graph. Immutable
  // hierarchy/code-unit declarations and descriptor observability remain the
  // identity layer, analogous to debug metadata surviving machine inlining.
  design.walk([&](Operation *operation) {
    if (auto function = dyn_cast<sim::SimFuncOp>(operation)) {
      function.removeEffectSummaryAttr();
      function.removeFragmentAbiAttr();
    }
    SmallVector<StringAttr> derivedAttributes;
    for (NamedAttribute named : operation->getAttrs())
      if (isa<schedule::ContinuationSiteAttr, schedule::TimingSiteAttr,
              schedule::NBASiteAttr, schedule::EventSiteAttr>(named.getValue()))
        derivedAttributes.push_back(named.getName());
    for (StringAttr name : derivedAttributes)
      operation->removeAttr(name);
  });
  design->removeAttr(
      sim::SimDesignOp::getComputeGraphAttrName(design->getName()));
}

} // namespace
} // namespace obelisk
