//===- BytecodeCoverageEncoding.cpp - Coverage instruction selection -----===//

#include "BytecodeEncoder.h"
#include "BytecodeSerialization.h"

using namespace mlir;

namespace obelisk::bytecode {

std::optional<LogicalResult>
Encoder::encodeCoverageOperation(FunctionPlan &plan, Operation *operation) {
  if (auto op = dyn_cast<sim::SimCoveragePointHitOp>(operation))
    return emitIntrinsicRegisters(
        plan, kIntrinsicCoveragePointHit,
        {emitU64Constant(plan, op.getPoint()), reg(plan, op.getEnabled())}, {});
  if (auto op = dyn_cast<sim::SimCoverageControlDefinitionOp>(operation))
    return emitIntrinsic(
        plan, kIntrinsicCoverageControlDefinition,
        {op.getControl(), op.getMetric(), op.getScope(), op.getDefinition()},
        {op.getStatus()});
  if (auto op = dyn_cast<sim::SimCoverageControlInstanceOp>(operation))
    return emitIntrinsicRegisters(
        plan, kIntrinsicCoverageControlInstance,
        {reg(plan, op.getControl()), reg(plan, op.getMetric()),
         reg(plan, op.getScope()),
         emitU64Constant(plan, op.getCoverageScopeId())},
        {reg(plan, op.getStatus())});
  if (auto op = dyn_cast<sim::SimCoverageQueryDefinitionOp>(operation))
    return emitIntrinsicRegisters(
        plan, kIntrinsicCoverageQueryDefinition,
        {reg(plan, op.getMetric()), reg(plan, op.getScope()),
         reg(plan, op.getDefinition()),
         emitU64Constant(plan, op.getMaximum() ? 1 : 0)},
        {reg(plan, op.getValue())});
  if (auto op = dyn_cast<sim::SimCoverageQueryInstanceOp>(operation))
    return emitIntrinsicRegisters(
        plan, kIntrinsicCoverageQueryInstance,
        {reg(plan, op.getMetric()), reg(plan, op.getScope()),
         emitU64Constant(plan, op.getCoverageScopeId()),
         emitU64Constant(plan, op.getMaximum() ? 1 : 0)},
        {reg(plan, op.getValue())});
  if (auto op = dyn_cast<sim::SimCoverageSaveOp>(operation))
    return emitIntrinsic(plan, kIntrinsicCoverageDatabaseSave,
                         {op.getMetric(), op.getName()}, {op.getStatus()});
  if (auto op = dyn_cast<sim::SimCoverageMergeOp>(operation))
    return emitIntrinsic(plan, kIntrinsicCoverageDatabaseMerge,
                         {op.getMetric(), op.getName()}, {op.getStatus()});
  if (auto op = dyn_cast<sim::SimFunctionalCoverageGetOp>(operation))
    return emitIntrinsic(plan, kIntrinsicFunctionalCoverageGet, {},
                         {op.getPercentage()});
  if (auto op = dyn_cast<sim::SimFunctionalCoverageSetDbNameOp>(operation))
    return emitIntrinsic(plan, kIntrinsicFunctionalCoverageSetDbName,
                         {op.getName()}, {});
  if (auto op = dyn_cast<sim::SimFunctionalCoverageLoadDbOp>(operation))
    return emitIntrinsic(plan, kIntrinsicFunctionalCoverageLoadDb,
                         {op.getName()}, {});
  if (isa<sim::SimCovergroupNullOp>(operation)) {
    uint32_t destination = reg(plan, operation->getResult(0));
    emit({Constant, 0, destination, 0, 0, 0, 0,
          addZeroConstant(plan.layouts[destination])});
    return success();
  }
  if (auto op = dyn_cast<sim::SimCovergroupCastOp>(operation)) {
    emit({Move, 0, reg(plan, op.getResult()), reg(plan, op.getInput())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimCovergroupCreateOp>(operation)) {
    auto declaration =
        SymbolTable::lookupNearestSymbolFrom<sim::SimCovergroupDeclOp>(
            op, op.getDeclarationAttr());
    if (!declaration)
      return failure();
    SmallVector<uint32_t> inputs{
        emitU64Constant(plan, declaration.getSchemaType()),
        emitU64Constant(plan, op.getArgumentCount()),
        emitU64Constant(plan, op.getExpressionIds().size())};
    for (int64_t id : op.getFormalIds())
      inputs.push_back(emitU64Constant(plan, static_cast<uint64_t>(id)));
    for (int64_t id : op.getExpressionIds())
      inputs.push_back(emitU64Constant(plan, static_cast<uint64_t>(id)));
    llvm::append_range(
        inputs, llvm::map_range(op.getPayloads(),
                                [&](Value value) { return reg(plan, value); }));
    return emitIntrinsicRegisters(plan, kIntrinsicCovergroupCreate, inputs,
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimCovergroupSampleEnabledOp>(operation))
    return emitIntrinsic(plan, kIntrinsicCovergroupSampleEnabled,
                         {op.getHandle()}, {op.getResult()});
  if (auto op = dyn_cast<sim::SimCovergroupClockEventRegisterOp>(operation)) {
    auto sampler = op.getSampler().getDefiningOp<sim::SimObserverBindOp>();
    auto evaluator =
        sampler ? indices.find(sampler.getEvaluator()) : indices.end();
    if (!sampler || evaluator == indices.end() ||
        !sampler.getDependencies().empty() ||
        sampler.getCaptures().size() > UINT32_MAX)
      return op.emitOpError("sampler observer has malformed bytecode metadata");
    SmallVector<sim::SimObserverBindOp> bindings;
    for (Value primary : op.getPrimaries()) {
      auto binding = primary.getDefiningOp<sim::SimObserverBindOp>();
      if (!binding)
        return op.emitOpError("primary is not produced by observer.bind");
      bindings.push_back(binding);
    }
    for (Value condition : op.getConditions()) {
      auto binding = condition.getDefiningOp<sim::SimObserverBindOp>();
      if (!binding)
        return op.emitOpError("condition is not produced by observer.bind");
      bindings.push_back(binding);
    }
    SmallVector<uint32_t> inputs{
        emitU64Constant(plan, op.getPrimaries().size()),
        emitU64Constant(plan, op.getConditions().size()),
        emitU64Constant(plan, op.getStrobe() ? 1 : 0),
        emitU64Constant(plan, plans[evaluator->second].stableID),
        emitU64Constant(plan, sampler.getCaptures().size())};
    auto stableHandle = [&](Value handle) -> FailureOr<uint32_t> {
      uint32_t stable = temporary(
          plan, IntegerType::get(operation->getContext(), uint32_t{64}));
      if (stable == kInvalidRegister)
        return failure();
      emit({HandleID, 0, stable, reg(plan, handle)});
      return stable;
    };
    for (sim::SimObserverBindOp binding : bindings) {
      auto found = indices.find(binding.getEvaluator());
      auto observerType = cast<sim::ObserverType>(binding.getType());
      std::optional<uint32_t> width =
          simulationWidth(observerType.getResultType());
      if (found == indices.end() || !width)
        return binding.emitOpError("observer has no bytecode metadata");
      inputs.push_back(emitU64Constant(plan, plans[found->second].stableID));
      inputs.push_back(emitU64Constant(plan, binding.getCaptures().size()));
      inputs.push_back(emitU64Constant(plan, binding.getDependencies().size()));
      inputs.push_back(emitU64Constant(plan, *width));
      inputs.push_back(emitU64Constant(
          plan, isa<sim::LogicType>(observerType.getResultType()) ? 1 : 0));
    }
    for (int32_t edge : op.getEdges())
      inputs.push_back(emitU64Constant(plan, static_cast<uint32_t>(edge)));
    for (int32_t index : op.getConditionIndices())
      inputs.push_back(emitU64Constant(plan, static_cast<uint32_t>(index)));
    for (sim::SimObserverBindOp binding : bindings)
      for (Value capture : binding.getCaptures())
        inputs.push_back(reg(plan, capture));
    uint32_t captureBase = 0;
    for (sim::SimObserverBindOp binding : bindings) {
      for (Value dependency : binding.getDependencies()) {
        uint32_t kind = OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL;
        uint32_t width = 0;
        uint32_t stable = kInvalidRegister;
        if (isa<sim::ArgumentRefType>(dependency.getType())) {
          kind = OBELISK_RT_OBSERVER_DEPENDENCY_ARGUMENT_REF;
          auto capture = llvm::find(binding.getCaptures(), dependency);
          if (capture == binding.getCaptures().end())
            return binding.emitOpError(
                "argument-ref dependency is not retained as a capture");
          uint64_t absolute =
              uint64_t{captureBase} +
              std::distance(binding.getCaptures().begin(), capture);
          if (absolute > UINT32_MAX)
            return binding.emitOpError(
                "argument-ref dependency capture index exceeds the v1 ABI");
          stable = emitU64Constant(plan, absolute);
          std::optional<uint32_t> storageWidth =
              simulationWidth(cast<sim::ArgumentRefType>(dependency.getType())
                                  .getElementType());
          if (!storageWidth)
            return op.emitOpError(
                "argument-ref dependency has no simulation storage width");
          width = *storageWidth;
        } else if (isa<sim::ManagedWatchType>(dependency.getType())) {
          kind = OBELISK_RT_OBSERVER_DEPENDENCY_MANAGED;
          width = 1;
          stable = reg(plan, dependency);
        } else if (isa<sim::EventType>(dependency.getType())) {
          kind = OBELISK_RT_OBSERVER_DEPENDENCY_EVENT;
          width = 1;
        } else {
          Type element =
              isa<sim::RefType>(dependency.getType())
                  ? cast<sim::RefType>(dependency.getType()).getElementType()
                  : cast<sim::NetType>(dependency.getType()).getElementType();
          std::optional<uint32_t> storageWidth = simulationWidth(element);
          if (!storageWidth)
            return op.emitOpError(
                "observer dependency has no simulation storage width");
          width = *storageWidth;
        }
        if (stable == kInvalidRegister) {
          FailureOr<uint32_t> encoded = stableHandle(dependency);
          if (failed(encoded))
            return failure();
          stable = *encoded;
        }
        inputs.push_back(stable);
        inputs.push_back(emitU64Constant(plan, kind));
        inputs.push_back(emitU64Constant(plan, width));
      }
      captureBase += binding.getCaptures().size();
    }
    for (Value initial : op.getInitialValues())
      inputs.push_back(reg(plan, initial));
    for (Value capture : sampler.getCaptures())
      inputs.push_back(reg(plan, capture));
    return emitIntrinsicRegisters(plan, kIntrinsicCovergroupClockEventRegister,
                                  inputs, {});
  }
  if (auto op = dyn_cast<sim::SimCovergroupBlockEventRegisterOp>(operation)) {
    auto sampler = op.getSampler().getDefiningOp<sim::SimObserverBindOp>();
    auto evaluator =
        sampler ? indices.find(sampler.getEvaluator()) : indices.end();
    if (!sampler || evaluator == indices.end() ||
        !sampler.getDependencies().empty() ||
        sampler.getCaptures().size() > UINT32_MAX)
      return op.emitOpError("sampler observer has malformed bytecode metadata");
    if (op.getTargetIds().empty() ||
        op.getTargetIds().size() != op.getEventKinds().size() ||
        op.getTargetIds().size() > UINT32_MAX)
      return op.emitOpError("block-event inventory exceeds v1 ABI");

    uint32_t receiver = op.getReceiver() ? reg(plan, op.getReceiver())
                                         : emitU64Constant(plan, 0);
    SmallVector<uint32_t> inputs{
        reg(plan, op.getHandle()), receiver,
        emitU64Constant(plan, plans[evaluator->second].stableID),
        emitU64Constant(plan, sampler.getCaptures().size()),
        emitU64Constant(plan, op.getTargetIds().size())};
    for (int64_t targetID : op.getTargetIds())
      inputs.push_back(emitU64Constant(plan, static_cast<uint64_t>(targetID)));
    for (int32_t eventKind : op.getEventKinds())
      inputs.push_back(emitU64Constant(plan, static_cast<uint32_t>(eventKind)));
    for (Value capture : sampler.getCaptures())
      inputs.push_back(reg(plan, capture));
    return emitIntrinsicRegisters(plan, kIntrinsicCovergroupBlockEventRegister,
                                  inputs, {});
  }
  if (auto op = dyn_cast<sim::SimCovergroupBlockEventFireOp>(operation)) {
    uint32_t receiver = op.getReceiver() ? reg(plan, op.getReceiver())
                                         : emitU64Constant(plan, 0);
    return emitIntrinsicRegisters(plan, kIntrinsicCovergroupBlockEventFire,
                                  {emitU64Constant(plan, op.getTargetId()),
                                   emitU64Constant(plan, op.getEventKind()),
                                   receiver},
                                  {});
  }
  if (auto op = dyn_cast<sim::SimCovergroupFormalReadOp>(operation))
    return emitIntrinsicRegisters(
        plan, kIntrinsicCovergroupFormalRead,
        {reg(plan, op.getHandle()), emitU64Constant(plan, op.getFormalId())},
        {reg(plan, op.getResult())});
  if (auto op = dyn_cast<sim::SimCovergroupSampleOp>(operation)) {
    SmallVector<uint32_t> inputs{
        reg(plan, op.getHandle()),
        emitU64Constant(plan, op.getExpressionIds().size())};
    for (int64_t id : op.getExpressionIds())
      inputs.push_back(emitU64Constant(plan, static_cast<uint64_t>(id)));
    llvm::append_range(
        inputs, llvm::map_range(op.getValues(),
                                [&](Value value) { return reg(plan, value); }));
    return emitIntrinsicRegisters(plan, kIntrinsicCovergroupSample, inputs, {});
  }
  if (auto op = dyn_cast<sim::SimCovergroupStartOp>(operation)) {
    uint32_t enabled = emitU64Constant(plan, 1);
    return emitIntrinsicRegisters(plan, kIntrinsicCovergroupSetEnabled,
                                  {reg(plan, op.getHandle()),
                                   emitU64Constant(plan, op.getItem()),
                                   enabled},
                                  {});
  }
  if (auto op = dyn_cast<sim::SimCovergroupStopOp>(operation)) {
    uint32_t enabled = emitU64Constant(plan, 0);
    return emitIntrinsicRegisters(plan, kIntrinsicCovergroupSetEnabled,
                                  {reg(plan, op.getHandle()),
                                   emitU64Constant(plan, op.getItem()),
                                   enabled},
                                  {});
  }
  if (auto op = dyn_cast<sim::SimCovergroupSetNameOp>(operation))
    return emitIntrinsic(plan, kIntrinsicCovergroupSetName,
                         {op.getHandle(), op.getName()}, {});
  if (auto op = dyn_cast<sim::SimCovergroupSetIntegerOptionOp>(operation))
    return emitIntrinsicRegisters(
        plan, kIntrinsicCovergroupSetIntegerOption,
        {reg(plan, op.getHandle()), emitU64Constant(plan, op.getItem()),
         emitU64Constant(plan, static_cast<uint32_t>(op.getOption())),
         reg(plan, op.getValue())},
        {});
  if (auto op = dyn_cast<sim::SimCovergroupGetIntegerOptionOp>(operation))
    return emitIntrinsicRegisters(
        plan, kIntrinsicCovergroupGetIntegerOption,
        {reg(plan, op.getHandle()), emitU64Constant(plan, op.getItem()),
         emitU64Constant(plan, static_cast<uint32_t>(op.getOption()))},
        {reg(plan, op.getValue())});
  if (auto op = dyn_cast<sim::SimCovergroupSetStringOptionOp>(operation))
    return emitIntrinsicRegisters(
        plan, kIntrinsicCovergroupSetStringOption,
        {reg(plan, op.getHandle()), emitU64Constant(plan, op.getItem()),
         emitU64Constant(plan, static_cast<uint32_t>(op.getOption())),
         reg(plan, op.getValue())},
        {});
  if (auto op = dyn_cast<sim::SimCovergroupSetTypeIntegerOptionOp>(operation))
    return emitIntrinsicRegisters(
        plan, kIntrinsicCovergroupSetTypeIntegerOption,
        {emitU64Constant(plan, op.getTypeId()),
         emitU64Constant(plan, op.getItem()),
         emitU64Constant(plan, static_cast<uint32_t>(op.getOption())),
         reg(plan, op.getValue())},
        {});
  if (auto op = dyn_cast<sim::SimCovergroupSetTypeStringOptionOp>(operation))
    return emitIntrinsicRegisters(
        plan, kIntrinsicCovergroupSetTypeStringOption,
        {emitU64Constant(plan, op.getTypeId()),
         emitU64Constant(plan, op.getItem()),
         emitU64Constant(plan, static_cast<uint32_t>(op.getOption())),
         reg(plan, op.getValue())},
        {});
  if (auto op = dyn_cast<sim::SimCovergroupInstanceQueryOp>(operation))
    return emitIntrinsicRegisters(
        plan, kIntrinsicCovergroupInstanceQuery,
        {reg(plan, op.getHandle()), emitU64Constant(plan, op.getItem())},
        {reg(plan, op.getPercentage()), reg(plan, op.getCovered()),
         reg(plan, op.getTotal())});
  if (auto op = dyn_cast<sim::SimCovergroupTypeQueryOp>(operation)) {
    auto declaration =
        SymbolTable::lookupNearestSymbolFrom<sim::SimCovergroupDeclOp>(
            op, op.getDeclarationAttr());
    if (!declaration)
      return failure();
    return emitIntrinsicRegisters(
        plan, kIntrinsicCovergroupTypeQuery,
        {emitU64Constant(plan, declaration.getSchemaType()),
         emitU64Constant(plan, op.getItem())},
        {reg(plan, op.getPercentage()), reg(plan, op.getCovered()),
         reg(plan, op.getTotal())});
  }
  return std::nullopt;
}

} // namespace obelisk::bytecode
