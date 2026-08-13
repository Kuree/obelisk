//===- MaterializeComputeFusion.cpp - Fuse static process bodies ----------===//

#include "ComputeFusion.h"

#include "obelisk/Analysis/SimulationVPIAnalysis.h"
#include "obelisk/Conversion/ObeliskToSimulation.h"
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

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMMATERIALIZECOMPUTEFUSIONPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

bool useEvalBodyFusion(sim::SimDesignOp design) {
  ModuleOp module = design->getParentOfType<ModuleOp>();
  auto scheduler = module->getAttrOfType<sim::NativeSchedulerModeAttr>(
      "obelisk.native_scheduler");
  return scheduler && scheduler.getValue() == sim::NativeSchedulerMode::Eval;
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

std::optional<uint64_t> getCodeUnitScope(sim::SimDesignOp design,
                                         sim::SimFuncOp function) {
  std::optional<uint64_t> codeUnit = function.getCodeUnitId();
  if (!codeUnit)
    return std::nullopt;
  for (sim::SimCodeUnitDeclOp declaration :
       design.getBody().front().getOps<sim::SimCodeUnitDeclOp>())
    if (declaration.getId() == *codeUnit)
      return declaration.getScopeId();
  return std::nullopt;
}

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
  return isa<sim::SimSuspendDelayOp, sim::SimSuspendChangeOp,
             sim::SimSuspendEdgeOp, sim::SimSuspendEdgeIffOp,
             sim::SimSuspendLevelOp, sim::SimSuspendAnyOp,
             sim::SimSuspendEventOp, sim::SimSuspendMailboxOp,
             sim::SimSuspendSemaphoreOp, sim::SimSuspendObserveOp,
             sim::SimSuspendForeverOp, sim::SimSuspendAwaitOp,
             sim::SimSuspendJoinOp, sim::SimSuspendChildrenOp>(operation);
}

/// Build an AOT-only, non-suspending activation body while the original CFG
/// and its typed wait are still intact.  The original actor remains the
/// coroutine/fallback identity.  This is deliberately broad in eval mode: it
/// is the experiment's Verilator-shaped executable body, not a production
/// profitability decision.
LogicalResult materializeStandaloneEvalBody(sim::SimDesignOp design,
                                            sim::SimFuncOp function) {
  if (function.isExternal() || function->hasAttr("obelisk.eval.body") ||
      function->hasAttr("obelisk.eval.borrowed_captures"))
    return success();
  bool portMethod = function.getEntryKind() == sim::EntryKind::PortInput ||
                    function.getEntryKind() == sim::EntryKind::PortOutput;
  bool eventDrivenInitial =
      function.getEntryKind() == sim::EntryKind::Initial;
  bool generatedRegionBody =
      function->hasAttr(sim::metadata::nativeRegionBody);
  if (!isSupportedEntryKind(function.getEntryKind()) &&
      function.getEntryKind() != sim::EntryKind::Continuous && !portMethod &&
      !eventDrivenInitial && !generatedRegionBody)
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
  Block &sourceEntry = function.getBody().front();
  SmallVector<Block *> preambleBlocks;
  llvm::SmallPtrSet<Block *, 8> preambleSeen;
  Block *preamble = &sourceEntry;
  while (preamble != wait && preamble != activation) {
    if (!preambleSeen.insert(preamble).second)
      return success();
    auto branch = dyn_cast<cf::BranchOp>(preamble->getTerminator());
    if (!branch || branch.getDest()->getNumArguments() !=
                       branch.getDestOperands().size()) {
      return success();
    }
    preambleBlocks.push_back(preamble);
    preamble = branch.getDest();
  }
  bool startsAtActivation = preamble == activation;

  SmallString<48> evalBase;
  (function.getSymName() + ".__obelisk_eval_body").toVector(evalBase);
  unsigned evalCounter = 0;
  SmallString<48> evalName = SymbolTable::generateSymbolName<48>(
      evalBase,
      [&](StringRef candidate) {
        return SymbolTable::lookupSymbolIn(design, candidate) != nullptr;
      },
      evalCounter);
  OpBuilder builder = OpBuilder::atBlockEnd(&design.getBody().front());
  llvm::SmallDenseSet<uint64_t, 32> usedCodeUnits;
  for (sim::SimCodeUnitDeclOp declaration :
       design.getBody().front().getOps<sim::SimCodeUnitDeclOp>())
    usedCodeUnits.insert(declaration.getId());
  uint64_t evalCodeUnit = 1;
  while (usedCodeUnits.contains(evalCodeUnit))
    ++evalCodeUnit;
  uint64_t evalScope = getCodeUnitScope(design, function).value_or(0);
  sim::SimCodeUnitDeclOp::create(
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
  evalBody->setAttr("obelisk.eval.borrowed_captures", builder.getUnitAttr());
  evalBody->setAttr("obelisk.eval.raw_captures", builder.getUnitAttr());
  if (Attribute owners = function->getAttr("obelisk.eval.source_owners"))
    evalBody->setAttr("obelisk.eval.source_owners", owners);
  if (Attribute group = function->getAttr("obelisk.eval.fusion_group"))
    evalBody->setAttr("obelisk.eval.fusion_group", group);
  sim::ContinuationSiteAttr activationSite;
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
    evalBody.erase();
    return success();
  }
  evalBody->setAttr("obelisk.eval.continuation",
                    builder.getI32IntegerAttr(activationSite.getId()));
  // Carry a typed source identity from the first eval-body clone onward.
  // Later body/module-instance fusion can then combine continuations without
  // preserving graph-generation-specific fragment ordinals.
  if (IntegerAttr codeUnit = function.getCodeUnitIdAttr())
    evalBody->setAttr(
        "obelisk.eval.source_owners",
        builder.getArrayAttr({builder.getDictionaryAttr(
            {builder.getNamedAttr("code_unit", codeUnit),
             builder.getNamedAttr(
                 "continuation",
                 builder.getI32IntegerAttr(activationSite.getId()))})}));
  SymbolTable::setSymbolVisibility(evalBody, SymbolTable::Visibility::Private);

  IRMapping mapping;
  Block &evalEntry = evalBody.getBody().front();
  for (auto [source, destination] :
       llvm::zip_equal(sourceEntry.getArguments(), evalEntry.getArguments()))
    mapping.map(source, destination);
  builder.setInsertionPointToStart(&evalEntry);
  SmallVector<Value> activationEntryOperands;
  for (Block *source : preambleBlocks) {
    for (Operation &operation : source->without_terminator())
      builder.clone(operation, mapping);
    auto branch = cast<cf::BranchOp>(source->getTerminator());
    if (startsAtActivation && branch.getDest() == activation) {
      for (Value value : branch.getDestOperands())
        activationEntryOperands.push_back(mapping.lookup(value));
    } else {
      for (auto [argument, value] : llvm::zip_equal(
               branch.getDest()->getArguments(), branch.getDestOperands()))
        mapping.map(argument, mapping.lookup(value));
    }
  }
  bool cloneTerminalWait =
      function.getEntryKind() == sim::EntryKind::Continuous || portMethod;
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
    evalBody.erase();
    return success();
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
    for (Value value : forwarded)
      entryOperands.push_back(mapping.lookup(value));
  }
  if (!startsAtActivation && !cloneTerminalWait)
    for (Operation &operation : wait->without_terminator())
      builder.clone(operation, mapping);
  builder.setInsertionPointToEnd(&evalEntry);
  cf::BranchOp::create(builder, function.getLoc(), mapping.lookup(activation),
                       entryOperands);

  bool supported = true;
  for (Block *source : activationBlocks) {
    builder.setInsertionPointToEnd(mapping.lookup(source));
    for (Operation &operation : *source) {
      if (isTypedSuspend(&operation) && &operation != source->getTerminator()) {
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
                sim::SimReturnOp::create(builder, branch.getLoc(),
                                         ValueRange{});
                continue;
              }
              Block *returnBlock = new Block;
              evalBody.getBody().push_back(returnBlock);
              OpBuilder returnBuilder = OpBuilder::atBlockEnd(returnBlock);
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
    evalBody.erase();
    return success();
  }
  function->setAttr("obelisk.eval.body",
                    FlatSymbolRefAttr::get(evalBody.getSymNameAttr()));
  return success();
}

bool hasOnlyPureEntryPreamble(sim::SimFuncOp function, Block *wait,
                              bool allowThreadedValues = false) {
  Block &entry = function.getBody().front();
  auto branch = dyn_cast<cf::BranchOp>(entry.getTerminator());
  if (!branch || branch.getDest() != wait ||
      branch.getDestOperands().size() != wait->getNumArguments() ||
      (!allowThreadedValues && !branch.getDestOperands().empty()))
    return false;
  return llvm::all_of(entry.without_terminator(), [](Operation &operation) {
    return isMemoryEffectFree(&operation);
  });
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

sim::ComputeEffectAttr getDirectSensitivity(sim::ComputeFragmentAttr fragment) {
  sim::ComputeEffectAttr sensitivity;
  for (Attribute attribute : fragment.getEffects()) {
    auto effect = cast<sim::ComputeEffectAttr>(attribute);
    if (effect.getEffect() != sim::ComputeEffectKind::Watch)
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
/// NBA values are not observable until the region barrier, so the two writes
/// have ordinary last-write semantics. Restrict this to adjacent accumulator
/// sites for the same commit root and to a speculatable, side-effect-free
/// overwrite arm. Besides removing a hot branch, the resulting straight-line
/// arithmetic is suitable for downstream SLP/vector formation.
uint64_t ifConvertConditionalNBAWrites(sim::SimFuncOp function,
                                       Block *protectedWait) {
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
      if (!first || first.getDelay())
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
      if (!safeOverwrite || !second || second.getDelay() ||
          first.getDestination() != second.getDestination() ||
          first.getValue().getType() != second.getValue().getType())
        continue;
      sim::NBASiteAttr firstSite = first.getSiteAttr();
      sim::NBASiteAttr secondSite = second.getSiteAttr();
      if (!firstSite || !secondSite || firstSite.getTiming() ||
          secondSite.getTiming() ||
          firstSite.getStorage() !=
              sim::ComputeNBAStorageKind::RootAccumulator ||
          secondSite.getStorage() !=
              sim::ComputeNBAStorageKind::RootAccumulator ||
          firstSite.getCommit() != secondSite.getCommit())
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
                                   second.getDestination(), Value{},
                                   secondSite);
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

/// Promote a static procedure temporary when this fused function is its sole
/// executable accessor and one store dominates every read. Such a declaration
/// is state only because its source-level lifetime spans activations; if every
/// activation overwrites it before use, retaining the canonical store would
/// add a signal-transition publication with no observer or semantic consumer.
uint64_t promotePrivateStaticTemporaries(sim::SimDesignOp design,
                                         sim::SimFuncOp function) {
  DenseMap<uint64_t, sim::SimStorageDeclOp> declarations;
  for (sim::SimStorageDeclOp declaration :
       design.getBody().front().getOps<sim::SimStorageDeclOp>())
    declarations.try_emplace(declaration.getId(), declaration);

  DenseMap<uint64_t, SmallVector<sim::SimRefLoadOp>> loads;
  DenseMap<uint64_t, SmallVector<sim::SimRefStoreOp>> stores;
  llvm::SmallDenseSet<uint64_t, 8> accessedElsewhere;
  llvm::SmallDenseSet<uint64_t, 8> unsupportedUses;
  design.walk([&](Operation *operation) {
    Value reference;
    if (auto load = dyn_cast<sim::SimRefLoadOp>(operation))
      reference = load.getReference();
    else if (auto store = dyn_cast<sim::SimRefStoreOp>(operation))
      reference = store.getReference();
    else {
      // Reference views are checked through their eventual users below, and
      // the root initializer must pass each reference to the replacement
      // fused process. Any other reference-consuming operation can observe
      // identity or state (for example an NBA enqueue, force, or foreign
      // call), so conservatively exclude its root from promotion.
      if (isa<sim::SimRefExtractOp, sim::SimRefDynExtractOp,
              sim::SimRefSubelementOp, sim::SimRefArrayElementOp>(operation))
        return;
      if (auto spawn = dyn_cast<sim::SimSpawnOp>(operation);
          spawn && spawn.getCalleeAttr() == function.getSymNameAttr())
        return;
      for (Value operand : operation->getOperands()) {
        if (!isa<sim::RefType>(operand.getType()))
          continue;
        if (std::optional<uint64_t> root = resolveStorageRoot(operand))
          unsupportedUses.insert(*root);
      }
      return;
    }
    std::optional<uint64_t> root = resolveStorageRoot(reference);
    if (!root)
      return;
    if (operation->getParentOfType<sim::SimFuncOp>() != function) {
      accessedElsewhere.insert(*root);
      return;
    }
    if (auto load = dyn_cast<sim::SimRefLoadOp>(operation))
      loads[*root].push_back(load);
    else
      stores[*root].push_back(cast<sim::SimRefStoreOp>(operation));
  });

  DominanceInfo dominance(function);
  uint64_t promoted = 0;
  for (auto &[descriptor, rootStores] : stores) {
    auto declaration = declarations.find(descriptor);
    auto rootLoads = loads.find(descriptor);
    if (declaration == declarations.end() || rootLoads == loads.end() ||
        rootStores.size() != 1 || rootLoads->second.empty() ||
        accessedElsewhere.contains(descriptor) ||
        unsupportedUses.contains(descriptor) ||
        declaration->second.getLifetime() != sim::Lifetime::Static)
      continue;
    std::optional<sim::ComputeObservabilityKind> observability =
        declaration->second.getObservability();
    if (!observability ||
        *observability == sim::ComputeObservabilityKind::ExternallyWritable)
      continue;
    sim::SimRefStoreOp store = rootStores.front();
    Value rootReference = store.getReference();
    std::optional<unsigned> totalWidth =
        sim::getPackedWidth(store.getValue().getType());
    if (!totalWidth || *totalWidth == 0 || *totalWidth > 64)
      continue;
    // The slicing sequence below operates on one integer plane. Four-state
    // values have distinct value/unknown planes and require a plane-aware
    // implementation rather than integer shifts and truncations.
    Type packedScalar = sim::getPackedScalarType(store.getValue().getType());
    if (!isa<IntegerType>(packedScalar))
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
                 family.contains(load.getReference());
        }))
      continue;

    auto getPackedOffset =
        [&](Value reference) -> std::optional<std::pair<uint64_t, Type>> {
      SmallVector<sim::SimRefSubelementOp> path;
      Value current = reference;
      while (current != rootReference) {
        auto view = current.getDefiningOp<sim::SimRefSubelementOp>();
        if (!view)
          return std::nullopt;
        path.push_back(view);
        current = view.getInput();
      }
      uint64_t offset = 0;
      Type type = store.getValue().getType();
      for (sim::SimRefSubelementOp view : llvm::reverse(path)) {
        for (int64_t index : view.getIndices()) {
          if (index < 0)
            return std::nullopt;
          auto child = sim::getAggregateProvenanceSubelement(
              type, static_cast<unsigned>(index));
          if (!child ||
              child->first > std::numeric_limits<uint64_t>::max() - offset)
            return std::nullopt;
          offset += child->first;
          type =
              sim::getAggregateElementType(type, static_cast<unsigned>(index));
        }
      }
      return std::pair{offset, type};
    };

    SmallVector<std::pair<sim::SimRefLoadOp, std::pair<uint64_t, Type>>>
        replacements;
    bool representable = true;
    for (sim::SimRefLoadOp load : rootLoads->second) {
      auto selected = getPackedOffset(load.getReference());
      if (!selected || selected->second != load.getResult().getType()) {
        representable = false;
        break;
      }
      std::optional<unsigned> width = sim::getPackedWidth(selected->second);
      if (!width || selected->first > *totalWidth ||
          *width > *totalWidth - selected->first) {
        representable = false;
        break;
      }
      replacements.push_back({load, *selected});
    }
    if (!representable)
      continue;

    IntegerType flattenedType = cast<IntegerType>(packedScalar);
    OpBuilder storeBuilder(store);
    Value flattened = sim::SimPackedFlattenOp::create(
        storeBuilder, store.getLoc(), flattenedType, store.getValue());
    for (auto &[load, selected] : replacements) {
      if (selected.first == 0 &&
          selected.second == store.getValue().getType()) {
        load.getResult().replaceAllUsesWith(store.getValue());
        load.erase();
        continue;
      }
      OpBuilder builder(load);
      Value bits = flattened;
      if (selected.first != 0)
        bits = arith::ShRUIOp::create(
            builder, load.getLoc(), bits,
            arith::ConstantOp::create(
                builder, load.getLoc(), flattenedType,
                builder.getIntegerAttr(flattenedType, selected.first)));
      unsigned selectedWidth = *sim::getPackedWidth(selected.second);
      if (selectedWidth != *totalWidth)
        bits = arith::TruncIOp::create(
            builder, load.getLoc(),
            IntegerType::get(function.getContext(), selectedWidth), bits);
      Value replacement =
          isa<IntegerType>(selected.second)
              ? bits
              : sim::SimPackedUnflattenOp::create(builder, load.getLoc(),
                                                  selected.second, bits)
                    .getResult();
      load.getResult().replaceAllUsesWith(replacement);
      load.erase();
    }
    // Read-only VPI must observe the last procedural value at a safe point, so
    // retain its one canonical store while forwarding all intra-activation
    // loads from the dominating SSA value. Invisible state can discard both
    // the store and the now-dead reference-view family.
    if (*observability == sim::ComputeObservabilityKind::Invisible) {
      store.erase();
      for (Value reference : llvm::reverse(family))
        if (Operation *definition = reference.getDefiningOp();
            definition && definition->use_empty())
          definition->erase();
    }
    if (flattened.use_empty())
      flattened.getDefiningOp()->erase();
    ++promoted;
  }
  return promoted;
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
    sim::SimDesignOp design, sim::ComputeFusionAttr fusion,
    sim::ComputeGraphAttr graph,
    const DenseMap<StringAttr, SmallVector<sim::SimSpawnOp>> &spawnsByCallee) {
  struct Candidate {
    sim::SimFuncOp function;
    sim::SimSpawnOp spawn;
    Block *body;
    Operation *suspend;
    SmallVector<unsigned> fusedArguments;
    int64_t fragment;
    int64_t resumeTarget;
  };
  DenseMap<int64_t, int64_t> resumeTargets;
  for (Attribute attribute : graph.getEdges()) {
    auto edge = cast<sim::ComputeEdgeAttr>(attribute);
    if (edge.getKind() == sim::ComputeEdgeKind::Resume)
      resumeTargets.try_emplace(edge.getSource(), edge.getTarget());
  }
  SmallVector<Candidate> candidates;
  for (int64_t member : fusion.getFragments().asArrayRef()) {
    if (member < 0 || static_cast<uint64_t>(member) >= graph.getNodes().size())
      return failure();
    auto fragment =
        dyn_cast<sim::ComputeFragmentAttr>(graph.getNodes()[member]);
    if (!fragment)
      return failure();
    sim::SimFuncOp function =
        design.lookupSymbol<sim::SimFuncOp>(fragment.getFunction().getValue());
    auto spawns = spawnsByCallee.find(fragment.getFunction().getAttr());
    if (!function || function.getEntryKind() != sim::EntryKind::Continuous ||
        !isComputeBodyFusionEligible(function) ||
        function.getBody().getBlocks().size() != 2 ||
        spawns == spawnsByCallee.end() || spawns->second.size() != 1)
      return failure();
    Block &entry = function.getBody().front();
    Block &body = function.getBody().back();
    auto branch = dyn_cast<cf::BranchOp>(entry.getTerminator());
    Operation *suspend = body.getTerminator();
    auto resume = resumeTargets.find(member);
    bool changeWait = isa<sim::SimSuspendChangeOp>(suspend);
    if (auto any = dyn_cast<sim::SimSuspendAnyOp>(suspend))
      changeWait = llvm::all_of(any.getEdges(), [](int32_t edge) {
        return edge == static_cast<int32_t>(sim::EdgeKind::Change);
      });
    if (!branch || branch.getDest() != &body ||
        !branch.getDestOperands().empty() || body.getNumArguments() != 0 ||
        !isa<sim::SimSuspendChangeOp, sim::SimSuspendAnyOp>(suspend) ||
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
    if (hasUntrackedPublication)
      return failure();
    candidates.push_back({function,
                          spawns->second.front(),
                          &body,
                          suspend,
                          {},
                          member,
                          resume->second});
  }
  if (candidates.size() < 2 || candidates.size() > 64)
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
      [&](StringRef candidate) {
        return SymbolTable::lookupSymbolIn(design, candidate) != nullptr;
      },
      symbolCounter);
  sim::SimFuncOp first = candidates.front().function;
  SmallVector<NamedAttribute> attributes;
  if (IntegerAttr codeUnit = first.getCodeUnitIdAttr())
    attributes.emplace_back(first.getCodeUnitIdAttrName(), codeUnit);
  sim::SimFuncOp kernel = sim::SimFuncOp::create(
      builder, first.getLoc(), name,
      FunctionType::get(design.getContext(), inputTypes, TypeRange{}),
      sim::EntryKind::Continuous, attributes, argumentAttrs);
  SymbolTable::setSymbolVisibility(kernel, SymbolTable::Visibility::Private);
  kernel->setAttr(sim::metadata::nativeRegionBody, builder.getUnitAttr());
  kernel->setAttr(sim::metadata::evalReconstructsContinuationArgs,
                  builder.getUnitAttr());
  kernel->setAttr("obelisk.eval.fusion_group",
                  builder.getI32IntegerAttr(fusion.getId()));
  SmallVector<Attribute> sourceOwners;
  sourceOwners.reserve(candidates.size());
  for (Candidate &candidate : candidates) {
    sim::ContinuationSiteAttr site;
    if (auto suspend = dyn_cast<sim::SimSuspendChangeOp>(candidate.suspend))
      site = suspend.getSiteAttr();
    else if (auto suspend = dyn_cast<sim::SimSuspendAnyOp>(candidate.suspend))
      site = suspend.getSiteAttr();
    sim::SimFuncOp sourceFunction = candidate.function;
    for (sim::SimFuncOp function :
         design.getBody().front().getOps<sim::SimFuncOp>())
      if (auto evalBody =
              function->getAttrOfType<FlatSymbolRefAttr>("obelisk.eval.body");
          evalBody && evalBody.getValue() == candidate.function.getSymName()) {
        sourceFunction = function;
        break;
      }
    IntegerAttr codeUnit = sourceFunction.getCodeUnitIdAttr();
    if (!site || !codeUnit) {
      kernel.erase();
      return failure();
    }
    sourceOwners.push_back(builder.getDictionaryAttr(
        {builder.getNamedAttr("code_unit", codeUnit),
         builder.getNamedAttr("continuation",
                              builder.getI32IntegerAttr(site.getId()))}));
  }
  kernel->setAttr("obelisk.eval.source_owners",
                  builder.getArrayAttr(sourceOwners));
  // The kernel is built incrementally, so any later rejection must remove the
  // partially populated symbol again. Leaving it behind would publish a
  // terminator-less function to a caller that only checks for success.
  auto bail = [&]() -> FailureOr<sim::SimFuncOp> {
    kernel.erase();
    return failure();
  };
  Block &entry = kernel.getBody().front();
  Block *body = new Block;
  BlockArgument initialize =
      body->addArgument(builder.getI1Type(), kernel.getLoc());
  Block *wait = new Block;
  kernel.getBody().push_back(body);
  kernel.getBody().push_back(wait);

  SmallVector<std::unique_ptr<IRMapping>> mappings;
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
    if (auto change = dyn_cast<sim::SimSuspendChangeOp>(candidate.suspend))
      handles.push_back(mapping->lookup(change.getWatched()));
    else
      llvm::append_range(
          handles,
          llvm::map_range(
              cast<sim::SimSuspendAnyOp>(candidate.suspend).getWatched(),
              [&](Value value) { return mapping->lookup(value); }));
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
  cf::BranchOp::create(builder, kernel.getLoc(), body, entryOperands);

  builder.setInsertionPointToStart(body);
  Type maskType = builder.getI64Type();
  Value dirty = arith::ConstantOp::create(builder, kernel.getLoc(), maskType,
                                          builder.getI64IntegerAttr(0));
  for (const Watch &watch : watchSnapshots) {
    Value current = loadWatched(builder, kernel.getLoc(), watch.handle);
    if (!current)
      return bail();
    Value equal = sim::SimLogicCompareOp::create(
        builder, kernel.getLoc(), builder.getI1Type(), sim::CompareKind::CaseEq,
        current, watch.previous);
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
    auto edge = cast<sim::ComputeEdgeAttr>(attribute);
    if (edge.getKind() != sim::ComputeEdgeKind::Sensitivity)
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
    Value changed =
        arith::ConstantOp::create(builder, kernel.getLoc(), builder.getI1Type(),
                                  builder.getBoolAttr(false));
    for (Operation &operation : candidate.body->without_terminator()) {
      if (auto drive = dyn_cast<sim::SimDriverDriveOp>(operation)) {
        Value transition = sim::SimDriverDriveChangedOp::create(
            builder, drive.getLoc(), mapping->lookup(drive.getDriver()),
            mapping->lookup(drive.getValue()));
        changed =
            arith::OrIOp::create(builder, drive.getLoc(), changed, transition);
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
    cf::BranchOp::create(builder, kernel.getLoc(), next, ValueRange{nextValue});
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
                               sim::ContinuationSiteAttr{},
                               sim::EventRegionAttr{}, body);

  builder.setInsertionPoint(insertionSpawn);
  sim::SimSpawnOp::create(builder, kernel.getLoc(), kernel.getSymNameAttr(),
                          operands, ArrayAttr{}, ArrayAttr{});
  for (Candidate &candidate : candidates)
    candidate.spawn.erase();
  for (Candidate &candidate : candidates)
    candidate.function.erase();
  return kernel;
}

FailureOr<sim::SimFuncOp> materializeFusion(
    sim::SimDesignOp design, sim::ComputeFusionAttr fusion,
    sim::ComputeGraphAttr graph,
    const DenseMap<uint32_t, uint32_t> &scheduleOrder,
    const DenseMap<StringAttr, SmallVector<sim::SimSpawnOp>> &spawnsByCallee,
    uint64_t &eliminatedTerminationPolls, uint64_t &ifConvertedNBAs,
    uint64_t &sharedStableConditions, uint64_t &promotedPrivateStores) {
  bool evalBodyFusion = useEvalBodyFusion(design);
  auto rejectEval = [&](StringRef) -> FailureOr<sim::SimFuncOp> {
    return failure();
  };
  DenseMap<uint32_t, uint32_t> resumeTargets;
  for (Attribute attribute : graph.getEdges()) {
    auto edge = cast<sim::ComputeEdgeAttr>(attribute);
    if (edge.getKind() == sim::ComputeEdgeKind::Resume)
      resumeTargets[edge.getSource()] = edge.getTarget();
  }
  SmallVector<uint32_t> entryTargets;
  for (Attribute attribute : graph.getEdges()) {
    auto edge = cast<sim::ComputeEdgeAttr>(attribute);
    if (edge.getKind() == sim::ComputeEdgeKind::Spawn &&
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
            ? dyn_cast<sim::ComputeFragmentAttr>(graph.getNodes()[target])
            : sim::ComputeFragmentAttr{};
    if (fragment)
      entryOrder.try_emplace(fragment.getFunction().getAttr(),
                             static_cast<uint32_t>(order));
  }

  SmallVector<BodyFusionCandidate, 4> candidates;
  sim::ComputeEffectAttr commonSensitivity;
  for (int64_t member : fusion.getFragments().asArrayRef()) {
    if (member < 0 || static_cast<uint64_t>(member) >= graph.getNodes().size())
      return rejectEval("invalid member");
    auto fragment = dyn_cast<sim::ComputeFragmentAttr>(
        graph.getNodes()[static_cast<size_t>(member)]);
    if (!fragment)
      return rejectEval("member is not a fragment");
    sim::ComputeEffectAttr sensitivity = getDirectSensitivity(fragment);
    if (!sensitivity || (commonSensitivity && sensitivity != commonSensitivity))
      return rejectEval("sensitivity mismatch");
    commonSensitivity = sensitivity;
    sim::SimFuncOp function =
        design.lookupSymbol<sim::SimFuncOp>(fragment.getFunction().getValue());
    auto spawns = function ? spawnsByCallee.find(function.getSymNameAttr())
                           : spawnsByCallee.end();
    auto resume = resumeTargets.find(static_cast<uint32_t>(member));
    sim::SimFuncOp spawningFunction =
        function && spawns != spawnsByCallee.end() && spawns->second.size() == 1
            ? spawns->second.front()->getParentOfType<sim::SimFuncOp>()
            : sim::SimFuncOp{};
    if (!function || !isSupportedEntryKind(function.getEntryKind()) ||
        !isComputeBodyFusionEligible(function) ||
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
    if (!wait || !wait->without_terminator().empty() ||
        (!evalBodyFusion && wait->getNumArguments() != 0) ||
        !isTypedDirectWait(wait->getTerminator()) ||
        wait->getNumSuccessors() != 1 ||
        (!evalBodyFusion && wait->getSuccessor(0)->getNumArguments() != 0) ||
        !hasOnlyPureEntryPreamble(function, wait, evalBodyFusion)) {
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
    candidate.instanceScope = getCodeUnitScope(design, function).value_or(0);
    candidate.spawn = spawns->second.front();
    candidate.wait = wait;
    candidate.body = wait->getSuccessor(0);
    if (evalBodyFusion) {
      auto entryBranch = cast<cf::BranchOp>(
          candidate.function.getBody().front().getTerminator());
      candidate.threadedEntryValues.append(
          entryBranch.getDestOperands().begin(),
          entryBranch.getDestOperands().end());
      if (candidate.threadedEntryValues.size() != wait->getNumArguments() ||
          candidate.body->getNumArguments() != wait->getNumArguments())
        return rejectEval("threaded wait/body arity mismatch");
    }
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
  SmallVector<uint32_t> readyTargets =
      getComputeFusionReadyTargets(graph, commonSensitivity);
  llvm::erase_if(readyTargets, [&](uint32_t target) {
    return !scheduleOrder.contains(target);
  });
  llvm::sort(readyTargets, [&](uint32_t lhs, uint32_t rhs) {
    return scheduleOrder.at(lhs) < scheduleOrder.at(rhs);
  });
  readyTargets.erase(std::unique(readyTargets.begin(), readyTargets.end()),
                     readyTargets.end());
  DenseMap<uint32_t, uint32_t> readyOrder;
  for (auto [order, target] : llvm::enumerate(readyTargets))
    readyOrder.try_emplace(target, static_cast<uint32_t>(order));
  for (BodyFusionCandidate &candidate : candidates) {
    auto order = readyOrder.find(candidate.resumeTarget);
    if (order == readyOrder.end())
      return rejectEval("missing ready order");
    candidate.resumeOrder = order->second;
  }
  llvm::sort(candidates, [](const auto &lhs, const auto &rhs) {
    return lhs.resumeOrder < rhs.resumeOrder;
  });

  if (evalBodyFusion) {
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

  // A body that publishes an Active-region sensitivity can make another actor
  // runnable between two members. The verified schedule is precise enough to
  // retain the fusion only when it is published by the final member. That
  // activation is observed after the fused actor returns to the scheduler;
  // publication by an earlier member could require an external actor to run
  // before the next member and remains a hard boundary.
  llvm::SmallDenseSet<uint32_t> candidateFragments;
  llvm::SmallDenseSet<uint32_t> finalCandidateFragments;
  llvm::SmallDenseSet<StringAttr> candidateFunctions;
  for (BodyFusionCandidate &candidate : candidates)
    candidateFunctions.insert(candidate.function.getSymNameAttr());
  for (auto [index, attribute] : llvm::enumerate(graph.getNodes())) {
    auto fragment = dyn_cast<sim::ComputeFragmentAttr>(attribute);
    if (!fragment ||
        !candidateFunctions.contains(fragment.getFunction().getAttr()))
      continue;
    if (fragment.getTier() != sim::ComputeTierKind::Native)
      return rejectEval("non-native member fragment");
    candidateFragments.insert(static_cast<uint32_t>(index));
    if (fragment.getFunction().getAttr() ==
        candidates.back().function.getSymNameAttr())
      finalCandidateFragments.insert(static_cast<uint32_t>(index));
  }
  for (Attribute attribute : graph.getEdges()) {
    auto edge = cast<sim::ComputeEdgeAttr>(attribute);
    if (!candidateFragments.contains(edge.getSource()) ||
        (edge.getKind() != sim::ComputeEdgeKind::Sensitivity &&
         edge.getKind() != sim::ComputeEdgeKind::Spawn) ||
        candidateFragments.contains(edge.getTarget()))
      continue;
    if (!evalBodyFusion && !finalCandidateFragments.contains(edge.getSource()))
      return rejectEval("external publication ordering");
  }

  // Fusing two actors makes their bodies indivisible. They must therefore be
  // adjacent in the complete deterministic Active resume schedule, not merely
  // among actors sharing this sensitivity: another sensitivity can become
  // ready in the same slot and occupy an intervening schedule rank. Their root
  // spawn entries need not be adjacent because eligibility proved every entry
  // preamble pure; the fused actor executes those preambles before registering
  // the common wait and therefore introduces no initial-region effect.
  for (auto [index, candidate] : llvm::enumerate(candidates)) {
    if (!evalBodyFusion &&
        candidate.resumeOrder !=
            static_cast<uint64_t>(candidates.front().resumeOrder) + index)
      return rejectEval("non-adjacent resume order");
  }

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
      [&](StringRef candidate) {
        return SymbolTable::lookupSymbolIn(design, candidate) != nullptr;
      },
      symbolCounter);
  SmallVector<NamedAttribute> fusedAttributes;
  if (IntegerAttr codeUnit = first.getCodeUnitIdAttr())
    fusedAttributes.emplace_back(first.getCodeUnitIdAttrName(), codeUnit);
  OpBuilder builder = OpBuilder::atBlockEnd(&design.getBody().front());
  sim::SimFuncOp fused = sim::SimFuncOp::create(
      builder, first.getLoc(), name,
      FunctionType::get(design.getContext(), inputTypes, TypeRange{}),
      first.getEntryKind(), fusedAttributes, argumentAttrs);
  SymbolTable::setSymbolVisibility(fused, SymbolTable::Visibility::Private);
  fused->setAttr(sim::metadata::nativeRegionBody, builder.getUnitAttr());
  fused->setAttr(sim::metadata::evalReconstructsContinuationArgs,
                 builder.getUnitAttr());
  fused->setAttr("obelisk.eval.fusion_group",
                 builder.getI32IntegerAttr(fusion.getId()));
  // Preserve typed source identities across graph rebuilding. Native eval
  // lowering resolves these actor continuations into the new graph's fragment
  // inventory; it never compares the old fragment ordinals directly.
  SmallVector<Attribute> sourceOwners;
  sourceOwners.reserve(candidates.size());
  for (BodyFusionCandidate &candidate : candidates) {
    sim::ContinuationSiteAttr site;
    if (auto suspend =
            dyn_cast<sim::SimSuspendChangeOp>(candidate.wait->getTerminator()))
      site = suspend.getSiteAttr();
    else if (auto suspend =
                 dyn_cast<sim::SimSuspendEdgeOp>(candidate.wait->getTerminator()))
      site = suspend.getSiteAttr();
    if (!site)
      return rejectEval("source owner has no stable continuation");
    sim::SimFuncOp sourceFunction = candidate.function;
    for (sim::SimFuncOp function :
         design.getBody().front().getOps<sim::SimFuncOp>())
      if (auto evalBody =
              function->getAttrOfType<FlatSymbolRefAttr>("obelisk.eval.body");
          evalBody && evalBody.getValue() == candidate.function.getSymName()) {
        sourceFunction = function;
        break;
      }
    IntegerAttr codeUnit = sourceFunction.getCodeUnitIdAttr();
    if (!codeUnit)
      return rejectEval("source owner has no stable code unit");
    sourceOwners.push_back(builder.getDictionaryAttr(
        {builder.getNamedAttr("code_unit", codeUnit),
         builder.getNamedAttr("continuation",
                              builder.getI32IntegerAttr(site.getId()))}));
  }
  fused->setAttr("obelisk.eval.source_owners",
                 builder.getArrayAttr(sourceOwners));
  // This closed-world fused activation cannot call foreign code or suspend
  // while its body is running. Mark it so native lowering can prove which NBA
  // sites are safe in the clean body selected by AOT actor dispatch.
  if (analysis::SimulationVPIAnalysis::compute(design).allowsWrite())
    fused->setAttr(sim::metadata::nativeGuardedSpecializationBody,
                   builder.getUnitAttr());

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
      if (evalBodyFusion && source == candidate.body &&
          !candidate.threadedEntryValues.empty()) {
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
  SmallVector<Block *> nextBlocks;
  nextBlocks.reserve(candidates.size());
  for (auto [index, candidate] : llvm::enumerate(candidates)) {
    Block *next =
        index + 1 == candidates.size()
            ? wait
            : clonedBlocks[index + 1].lookup(candidates[index + 1].body);
    nextBlocks.push_back(next);
    mappings[index]->map(candidate.wait, next);
  }

  builder.setInsertionPointToStart(&entry);
  for (auto [candidate, mapping] : llvm::zip_equal(candidates, mappings))
    for (Operation *operation : candidate.entryPreamble)
      builder.clone(*operation, *mapping);
  if (evalBodyFusion) {
    for (auto [candidate, mapping] : llvm::zip_equal(candidates, mappings)) {
      for (auto [argument, value] : llvm::zip_equal(
               candidate.wait->getArguments(), candidate.threadedEntryValues))
        mapping->map(argument, mapping->lookup(value));
      auto forwarded = cast<BranchOpInterface>(candidate.wait->getTerminator())
                           .getSuccessorOperands(0)
                           .getForwardedOperands();
      for (auto [argument, value] :
           llvm::zip_equal(candidate.body->getArguments(), forwarded))
        mapping->map(argument, mapping->lookup(value));
    }
  }
  cf::BranchOp::create(builder, fused.getLoc(), wait);

  builder.setInsertionPointToStart(wait);
  builder.clone(*candidates.front().wait->getTerminator(), *mappings.front());
  for (auto [candidateIndex, pair] :
       llvm::enumerate(llvm::zip_equal(candidates, mappings))) {
    auto &[candidate, mapping] = pair;
    for (Block *source : candidate.bodyBlocks) {
      Block *destination = mapping->lookup(source);
      builder.setInsertionPointToEnd(destination);
      for (Operation &operation : *source) {
        if (evalBodyFusion) {
          if (auto branch = dyn_cast<cf::BranchOp>(operation);
              branch && branch.getDest() == candidate.wait) {
            cf::BranchOp::create(builder, branch.getLoc(),
                                 nextBlocks[candidateIndex]);
            continue;
          }
        }
        builder.clone(operation, *mapping);
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
  sim::SimSpawnOp::create(builder, fused.getLoc(), fused.getSymNameAttr(),
                          operands, ArrayAttr{}, ArrayAttr{});
  for (BodyFusionCandidate &candidate : candidates)
    candidate.spawn.erase();
  for (BodyFusionCandidate &candidate : candidates)
    candidate.function.erase();

  // Remove private activation temporaries before if-converting NBA diamonds.
  // Besides avoiding canonical state publication, this turns overwrite-arm
  // loads into SSA values so only genuinely speculatable arithmetic is moved
  // out of the branch.
  promotedPrivateStores += promotePrivateStaticTemporaries(design, fused);
  ifConvertedNBAs += ifConvertConditionalNBAWrites(fused, wait);
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
          return SymbolTable::lookupSymbolIn(design, candidate) != nullptr;
        },
        evalCounter);
    builder.setInsertionPointToEnd(&design.getBody().front());
    llvm::SmallDenseSet<uint64_t, 32> usedCodeUnits;
    for (sim::SimCodeUnitDeclOp declaration :
         design.getBody().front().getOps<sim::SimCodeUnitDeclOp>())
      usedCodeUnits.insert(declaration.getId());
    uint64_t evalCodeUnit = 1;
    while (usedCodeUnits.contains(evalCodeUnit))
      ++evalCodeUnit;
    uint64_t evalScope = candidates.front().instanceScope;
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
    evalBody->setAttr("obelisk.eval.borrowed_captures", builder.getUnitAttr());
    evalBody->setAttr("obelisk.eval.raw_captures", builder.getUnitAttr());
    evalBody->setAttr("obelisk.eval.instance_coordinator", builder.getUnitAttr());
    evalBody->setAttr("obelisk.eval.fusion_group",
                      fused->getAttr("obelisk.eval.fusion_group"));
    evalBody->setAttr("obelisk.eval.source_owners",
                      fused->getAttr("obelisk.eval.source_owners"));
    sim::ContinuationSiteAttr activationSite;
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
      evalBody.erase();
      return fused;
    }
    // The source suspension may be erased by later fusion and CFG cleanup.
    // Carry its stable identity on the generated body so native scheduling
    // never has to retain or dereference transformation-owned operations.
    evalBody->setAttr("obelisk.eval.continuation",
                      builder.getI32IntegerAttr(activationSite.getId()));
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
            sim::SimReturnOp::create(builder, branch.getLoc(), ValueRange{});
            continue;
          }
          if (auto branch = dyn_cast<cf::CondBranchOp>(operation)) {
            bool trueWait = branch.getTrueDest() == wait;
            bool falseWait = branch.getFalseDest() == wait;
            if (trueWait || falseWait) {
              if (trueWait && falseWait) {
                sim::SimReturnOp::create(builder, branch.getLoc(),
                                         ValueRange{});
                continue;
              }
              Block *returnBlock = new Block;
              evalBody.getBody().push_back(returnBlock);
              OpBuilder returnBuilder = OpBuilder::atBlockEnd(returnBlock);
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
      evalBody.erase();
    } else {
      fused->setAttr("obelisk.eval.body",
                     FlatSymbolRefAttr::get(evalBody.getSymNameAttr()));
    }
  }

  return fused;
}

void ObeliskSimMaterializeComputeFusionPass::runOnOperation() {
  sim::SimDesignOp design = getOperation();
  ArrayAttr fusions =
      design->getAttrOfType<ArrayAttr>(sim::metadata::staticBodyFusion);
  sim::ComputeGraphAttr graph = design.getComputeGraphAttr();
  bool evalScheduler = useEvalBodyFusion(design);
  if ((!fusions || !graph || graph.getWorkers() != 1) && evalScheduler) {
    // Standalone activation cloning is not conditional on finding a profitable
    // multi-actor fusion.  Keeping it behind the fusion-inventory early return
    // made explicit eval plans depend on an unrelated optimization decision.
    SmallVector<sim::SimFuncOp> actors;
    for (sim::SimFuncOp function :
         design.getBody().front().getOps<sim::SimFuncOp>())
      actors.push_back(function);
    for (sim::SimFuncOp function : actors)
      if (failed(materializeStandaloneEvalBody(design, function))) {
        signalPassFailure();
        return;
      }
  }
  if (!fusions || !graph || graph.getWorkers() != 1)
    return;

  DenseMap<uint32_t, uint32_t> scheduleOrder;
  uint32_t nextOrder = 0;
  for (Attribute regionAttribute : graph.getRegions()) {
    auto region = cast<sim::ComputeRegionAttr>(regionAttribute);
    if (region.getKind() != sim::ComputeRegionKind::Active)
      continue;
    for (Attribute groupAttribute : region.getGroups())
      for (int64_t member : cast<sim::ComputeGroupAttr>(groupAttribute)
                                .getFragments()
                                .asArrayRef())
        scheduleOrder[static_cast<uint32_t>(member)] = nextOrder++;
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
  for (Attribute attribute : fusions) {
    auto fusion = dyn_cast<sim::ComputeFusionAttr>(attribute);
    if (!fusion)
      continue;
    FailureOr<sim::SimFuncOp> fused = materializeFusion(
        design, fusion, graph, scheduleOrder, spawnsByCallee, removedPolls,
        convertedNBAs, sharedConditions, promotedStores);
    // The model-wide eval coordinator already owns a fine dirty bit for each
    // original activation. Replacing several of those bodies with a second
    // snapshot-and-mask dispatcher adds redundant work to the hot loop and
    // obscures the original fragment ownership. Keep straight-line region
    // fusion for the actor scheduler, where it removes dispatch overhead.
    if (failed(fused) && !evalScheduler)
      fused =
          materializeStraightLineKernel(design, fusion, graph, spawnsByCallee);
    changed |= succeeded(fused);
    if (succeeded(fused))
      ++materializedFusions;
    else
      ++rejectedFusions;
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
      if (failed(materializeStandaloneEvalBody(design, function))) {
        signalPassFailure();
        return;
      }
    }
  }
  eliminatedTerminationPolls += removedPolls;
  ifConvertedNBAs += convertedNBAs;
  sharedStableConditions += sharedConditions;
  promotedPrivateStores += promotedStores;
  design->removeAttr(sim::metadata::staticBodyFusion);
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
      if (isa<sim::ContinuationSiteAttr, sim::TimingSiteAttr, sim::NBASiteAttr,
              sim::EventSiteAttr>(named.getValue()))
        derivedAttributes.push_back(named.getName());
    for (StringAttr name : derivedAttributes)
      operation->removeAttr(name);
  });
  design->removeAttr(
      sim::SimDesignOp::getComputeGraphAttrName(design->getName()));
}

} // namespace
} // namespace obelisk
