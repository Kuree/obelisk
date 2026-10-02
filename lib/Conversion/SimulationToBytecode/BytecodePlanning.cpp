//===- BytecodePlanning.cpp - Design-wide bytecode planning --------------===//

#include "BytecodeEncoder.h"
#include "BytecodeRegisterPlanning.h"
#include "BytecodeSerialization.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "obelisk/Analysis/NativeAOTAnalysis.h"
#include "obelisk/Analysis/SimulationEffectAnalysis.h"
#include "obelisk/Analysis/SimulationScheduleAnalysis.h"
#include "obelisk/Analysis/StaticSpecializationAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Analysis/Liveness.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"

#include <limits>
#include <tuple>

using namespace mlir;

namespace obelisk::bytecode {

LogicalResult Encoder::prepareStaticSpecializationSites() {
  FailureOr<analysis::StaticSpecializationAnalysis> specialization =
      analysis::StaticSpecializationAnalysis::compute(design);
  if (failed(specialization))
    return failure();
  staticNBASites = specialization->getNBASites();
  return success();
}

LogicalResult Encoder::planTwoStateRegisters() {
  FailureOr<llvm::DenseSet<Value>> planned =
      bytecode::planTwoStateRegisters(design);
  if (failed(planned))
    return failure();
  twoStateLogicRegisters = std::move(*planned);
  return success();
}

FailureOr<Layout> Encoder::getValueLayout(Value value) const {
  FailureOr<Layout> layout = getLayout(value.getType());
  if (failed(layout) || !isa<sim::LogicType>(value.getType()) ||
      !twoStateLogicRegisters.contains(value))
    return layout;
  layout->kind = Bits;
  layout->size = ((uint64_t{layout->width} + 63) / 64) * 8;
  return layout;
}

namespace {

// IEEE 1800-2023, Clauses 4.5 and 4.6: changing an execution tier must
// preserve the process continuation and its scheduled effects.
void pruneNativeFunctions(sim::SimDesignOp design,
                          SmallVectorImpl<sim::SimFuncOp> &functions) {
  auto module = design->getParentOfType<ModuleOp>();
  if (!module)
    return;
  auto native = analysis::NativeAOTAnalysis::compute(module);
  if (!native.isEligible())
    return;

  llvm::StringMap<sim::SimFuncOp> symbols;
  llvm::DenseMap<Operation *, SmallVector<Operation *>> dependencies, callers;
  llvm::DenseSet<Operation *> fallback, retained;
  SmallVector<Operation *> pendingFallback, worklist;
  auto retain = [&](Operation *function) {
    if (retained.insert(function).second)
      worklist.push_back(function);
  };
  llvm::DenseSet<Operation *> nativeBootstraps;
  for (auto function : functions) {
    auto slot = native.getActorSlots().find(function);
    if (function.getEntryKind() != sim::EntryKind::RootInitializer ||
        (slot != native.getActorSlots().end() &&
         native.getBytecodeFragments().contains(function)))
      continue;
    bool suspends = false;
    function.walk([&](Operation *op) { suspends |= sim::isSuspensionOp(op); });
    if (!suspends)
      nativeBootstraps.insert(function);
  }
  auto requireFallback = [&](Operation *function) {
    // IEEE 1800-2023 6.8: declaration initialization precedes process startup.
    // An unsuspended root uses the ordinary native executor, including when
    // managed captures exclude it from AOT scheduling. Only admitted bytecode
    // continuations can select the interpreter during native startup.
    if (nativeBootstraps.contains(function))
      return;
    if (fallback.insert(function).second)
      pendingFallback.push_back(function);
  };
  for (auto function : functions)
    symbols[function.getSymName()] = function;
  for (auto function : functions) {
    auto uses = SymbolTable::getSymbolUses(function);
    if (!uses)
      return;
    for (const auto &use : *uses)
      if (auto target = symbols.lookup(use.getSymbolRef().getRootReference())) {
        dependencies[function].push_back(target);
        if (isa<sim::SimCallOp, sim::SimTaskCallOp>(use.getUser()))
          callers[target].push_back(function);
      }

    // Keep callback and helper entry points conservatively. Their native
    // callers need bytecode only when a call can reach an interpreter boundary.
    if (!native.getActorSlots().contains(function) &&
        !nativeBootstraps.contains(function))
      retain(function);
    bool needsFallback =
        native.getBytecodeFragments().contains(function) ||
        native.getRuntimeObservedWriterActors().contains(function);
    function.walk([&](Operation *op) {
      needsFallback |=
          isa<sim::SimTaskCallOp, sim::SimDisplayOp, sim::SimFinishOp,
              sim::SimStopOp, sim::SimProgramExitOp, sim::SimFatalOp,
              sim::SimErrorOp, sim::SimStatusCheckOp, sim::SimSampledReadOp,
              sim::SimSampledHistoryOp>(op);
      if (auto call = dyn_cast<sim::SimCallOp>(op))
        needsFallback |= !symbols.contains(call.getCallee());
    });
    if (needsFallback)
      requireFallback(function);
  }
  while (!pendingFallback.empty()) {
    Operation *function = pendingFallback.pop_back_val();
    retain(function);
    for (Operation *caller : callers[function])
      requireFallback(caller);
  }
  while (!worklist.empty())
    for (Operation *callee : dependencies[worklist.pop_back_val()])
      retain(callee);
  if (module->hasAttr("obelisk.debug.native_timing"))
    llvm::errs() << "obelisk bytecode retention: " << retained.size() << "/"
                 << functions.size() << " functions\n";
  llvm::erase_if(functions, [&](sim::SimFuncOp function) {
    return !retained.contains(function);
  });
}

} // namespace

LogicalResult Encoder::planFunctions() {
  analysis::SimulationEffectAnalysis effects(design);
  design.walk([&](sim::SimFuncOp function) {
    function->removeAttr(sim::metadata::bytecodeReadOnly);
    if (effects.isReadOnly(function))
      function->setAttr(sim::metadata::bytecodeReadOnly,
                        UnitAttr::get(design.getContext()));
  });
  SmallVector<sim::SimFuncOp> functions;
  for (sim::SimFuncOp function : design.getBody().getOps<sim::SimFuncOp>()) {
    if (function.isExternal())
      externalFunctions[function.getSymName()] = function;
    else
      functions.push_back(function);
  }
  if (options.pruneNative && !options.requireBytecode)
    pruneNativeFunctions(design, functions);
  auto getStableID = [](sim::SimFuncOp function) {
    return function.getCodeUnitId().value_or(
        stableHash(function.getSymName()) &
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
  };
  llvm::sort(functions, [&](sim::SimFuncOp left, sim::SimFuncOp right) {
    return std::make_tuple(getStableID(left), left.getSymName()) <
           std::make_tuple(getStableID(right), right.getSymName());
  });
  if (functions.empty() && (!options.pruneNative || options.requireBytecode))
    return design.emitOpError("contains no executable functions");
  plans.reserve(functions.size());
  llvm::DenseMap<uint64_t, sim::SimFuncOp> stableIDs;
  for (auto [index, function] : llvm::enumerate(functions)) {
    FunctionPlan &plan = plans.emplace_back();
    plan.function = function;
    plan.liveness = std::make_unique<Liveness>(function);
    plan.index = static_cast<uint32_t>(index);
    plan.stableID = getStableID(function);
    if (plan.stableID == 0)
      return function.emitOpError("executable code-unit ID must be nonzero");
    auto [collision, inserted] = stableIDs.try_emplace(plan.stableID, function);
    if (!inserted) {
      function.emitOpError()
          << "duplicate executable code-unit ID " << plan.stableID;
      collision->second.emitRemark("first function with this ID is here");
      return failure();
    }
    indices[function.getSymName()] = plan.index;
  }
  for (FunctionPlan &plan : plans) {
    FunctionType type = plan.function.getFunctionType();
    auto allocateLayout = [&](Layout layout) -> uint32_t {
      uint64_t aligned = llvm::alignTo(plan.scratchSize, uint64_t{8});
      layout.offset = aligned;
      plan.scratchSize = aligned + layout.size;
      plan.layouts.push_back(layout);
      return plan.layouts.size() - 1;
    };
    auto allocateType = [&](Type type) -> FailureOr<uint32_t> {
      FailureOr<Layout> layout = getLayout(type);
      if (failed(layout))
        return failure();
      return allocateLayout(*layout);
    };
    auto allocateValue = [&](Value value) -> FailureOr<uint32_t> {
      FailureOr<Layout> layout = getValueLayout(value);
      if (failed(layout))
        return failure();
      if (layout->kind == Bits && isa<sim::LogicType>(value.getType()))
        ++plan.twoStateLogicRegisters;
      return allocateLayout(*layout);
    };
    Block &entry = plan.function.getBody().front();
    if (entry.getNumArguments() != type.getNumInputs())
      return plan.function.emitOpError("entry signature is inconsistent");
    for (BlockArgument argument : entry.getArguments()) {
      FailureOr<uint32_t> reg = allocateValue(argument);
      if (failed(reg))
        return argument.getOwner()->getParentOp()->emitError()
               << "cannot encode argument type " << argument.getType();
      plan.registers.insert({argument, *reg});
    }
    for (Type result : type.getResults()) {
      FailureOr<uint32_t> reg = allocateType(result);
      if (failed(reg))
        return plan.function.emitOpError()
               << "cannot encode result type " << result;
      plan.resultRegisters.push_back(*reg);
    }
    for (Block &block : plan.function.getBody()) {
      if (&block != &entry)
        for (BlockArgument argument : block.getArguments()) {
          FailureOr<uint32_t> reg = allocateValue(argument);
          if (failed(reg))
            return plan.function.emitOpError()
                   << "cannot encode block argument type "
                   << argument.getType();
          plan.registers.insert({argument, *reg});
        }
      for (Operation &operation : block)
        for (Value result : operation.getResults()) {
          FailureOr<uint32_t> reg = allocateValue(result);
          if (failed(reg))
            return operation.emitOpError()
                   << "cannot encode result type " << result.getType();
          plan.registers.insert({result, *reg});
        }
    }
    plan.scratchSize = llvm::alignTo(plan.scratchSize, uint64_t{8});
    if (plan.function.getEntryKind() != sim::EntryKind::Function &&
        plan.function.getEntryKind() != sim::EntryKind::Observer) {
      FailureOr<std::unique_ptr<SimulationProcessFrameAnalysis>> frame =
          SimulationProcessFrameAnalysis::create(plan.function, dataLayout);
      if (failed(frame))
        return failure();
      plan.frame = std::move(*frame);
      if (plan.frame->getFrameSize() >=
          OBELISK_RT_DESIGN_FUNCTION_FRAME_SIZE_LIMIT)
        return plan.function.emitOpError(
            "process frame is too large for bytecode function flags");
      ArrayRef<ProcessFrameValue> captures =
          plan.frame->getEntryCaptureLayout();
      if (captures.size() != entry.getNumArguments())
        return plan.function.emitOpError("entry capture layout is incomplete");
      for (auto [argument, capture] : llvm::enumerate(captures))
        captureRecords.push_back(
            {plan.index, static_cast<uint32_t>(argument), capture.valueOffset,
             capture.hasSecondaryStorage() ? capture.getSecondaryOffset()
                                           : UINT64_MAX,
             capture.storageSize});
    }
  }
  return success();
}

LogicalResult Encoder::planScheduleRanks() {
  FailureOr<analysis::SimulationScheduleAnalysis> schedule =
      analysis::SimulationScheduleAnalysis::compute(design);
  if (failed(schedule))
    return failure();
  for (FunctionPlan &plan : plans) {
    if (std::optional<uint32_t> rank =
            schedule->getEntryRank(plan.function.getOperation()))
      plan.initialScheduleRank = *rank;
    for (Block &block : plan.function.getBody())
      if (std::optional<uint32_t> rank = schedule->getBlockRank(&block))
        plan.blockScheduleRanks[&block] = *rank;
  }
  return success();
}

} // namespace obelisk::bytecode
