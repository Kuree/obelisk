//===- BytecodePlanning.cpp - Design-wide bytecode planning --------------===//

#include "BytecodeEncoder.h"
#include "BytecodeRegisterPlanning.h"
#include "BytecodeSerialization.h"
#include "obelisk/Analysis/SimulationScheduleAnalysis.h"
#include "obelisk/Analysis/StaticSpecializationAnalysis.h"
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

LogicalResult Encoder::planFunctions() {
  SmallVector<sim::SimFuncOp> functions;
  for (sim::SimFuncOp function : design.getBody().getOps<sim::SimFuncOp>()) {
    if (function.isExternal())
      externalFunctions[function.getSymName()] = function;
    else
      functions.push_back(function);
  }
  auto getStableID = [](sim::SimFuncOp function) {
    return function.getCodeUnitId().value_or(
        stableHash(function.getSymName()) &
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
  };
  llvm::sort(functions, [&](sim::SimFuncOp left, sim::SimFuncOp right) {
    return std::make_tuple(getStableID(left), left.getSymName()) <
           std::make_tuple(getStableID(right), right.getSymName());
  });
  if (functions.empty())
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
