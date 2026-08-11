//===- BytecodeOperationEncoding.cpp - Bytecode instruction selection ----===//

#include "BytecodeEncoder.h"
#include "BytecodeSerialization.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

using namespace mlir;

namespace obelisk::bytecode {

LogicalResult Encoder::encodeOperation(FunctionPlan &plan,
                                       Operation *operation) {
  if (std::optional<LogicalResult> encoded =
          encodeArithmeticOperation(plan, operation))
    return *encoded;
  auto binary = [&](uint16_t opcode, Value result, Value left, Value right) {
    emit({opcode, 0, reg(plan, result), reg(plan, left), reg(plan, right)});
    return success();
  };
  if (auto branch = dyn_cast<cf::BranchOp>(operation)) {
    auto mapping = addMap(plan, branch.getDest()->getArguments(), plan,
                          branch.getDestOperands());
    uint64_t encoded = emit({Jump, 0, 0, static_cast<uint32_t>(mapping.first),
                             static_cast<uint32_t>(mapping.second)});
    plan.branches.push_back({encoded, branch.getDest()});
    return success();
  }
  if (auto branch = dyn_cast<cf::CondBranchOp>(operation)) {
    auto trueMap = addMap(plan, branch.getTrueDest()->getArguments(), plan,
                          branch.getTrueDestOperands());
    uint64_t conditional = emit({Branch, 0, reg(plan, branch.getCondition()),
                                 static_cast<uint32_t>(trueMap.first),
                                 static_cast<uint32_t>(trueMap.second)});
    plan.branches.push_back({conditional, branch.getTrueDest()});
    auto falseMap = addMap(plan, branch.getFalseDest()->getArguments(), plan,
                           branch.getFalseDestOperands());
    uint64_t fallback = emit({Jump, 0, 0, static_cast<uint32_t>(falseMap.first),
                              static_cast<uint32_t>(falseMap.second)});
    plan.branches.push_back({fallback, branch.getFalseDest()});
    return success();
  }
  if (auto switchOp = dyn_cast<cf::SwitchOp>(operation)) {
    SmallVector<APInt> values;
    if (auto cases = switchOp.getCaseValues())
      llvm::append_range(values, cases->getValues<APInt>());
    auto destinations = switchOp.getCaseDestinations();
    auto caseOperands = switchOp.getCaseOperands();
    if (values.size() != destinations.size() ||
        values.size() != caseOperands.size())
      return switchOp.emitOpError("malformed bytecode switch cases");
    for (auto [value, destination, operands] :
         llvm::zip_equal(values, destinations, caseOperands)) {
      uint32_t caseValue = temporary(plan, switchOp.getFlag().getType());
      uint32_t match =
          temporary(plan, IntegerType::get(operation->getContext(), 1));
      if (caseValue == kInvalidRegister || match == kInvalidRegister)
        return failure();
      emit({Constant, 0, caseValue, 0, 0, 0, 0,
            addConstant(plan.layouts[caseValue], value)});
      emit({Compare, OBELISK_RT_DB_CMP_EQ, match,
            reg(plan, switchOp.getFlag()), caseValue});
      auto mapping = addMap(plan, destination->getArguments(), plan, operands);
      uint64_t branch =
          emit({Branch, 0, match, static_cast<uint32_t>(mapping.first),
                static_cast<uint32_t>(mapping.second)});
      plan.branches.push_back({branch, destination});
    }
    auto mapping =
        addMap(plan, switchOp.getDefaultDestination()->getArguments(), plan,
               switchOp.getDefaultOperands());
    uint64_t fallback = emit({Jump, 0, 0, static_cast<uint32_t>(mapping.first),
                              static_cast<uint32_t>(mapping.second)});
    plan.branches.push_back({fallback, switchOp.getDefaultDestination()});
    return success();
  }
  if (auto constant = dyn_cast<sim::SimBytesConstantOp>(operation)) {
    ArrayRef<uint8_t> bytes(
        reinterpret_cast<const uint8_t *>(constant.getValue().data()),
        constant.getValue().size());
    emit({Constant, 0, reg(plan, constant.getResult()), 0, 0, 0, 0,
          addBytesConstant(bytes)});
    return success();
  }
  if (auto op = dyn_cast<sim::SimContextRuntimeOp>(operation)) {
    emit({Move, 0, reg(plan, op.getResult()), reg(plan, op.getContext())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimStatusCheckOp>(operation)) {
    emit({Fail, 0, 0, reg(plan, op.getStatus())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimFinishOp>(operation))
    return emitIntrinsic(plan, kIntrinsicFinish, {op.getVerbosity()}, {});
  if (auto op = dyn_cast<sim::SimStopOp>(operation))
    return emitIntrinsic(plan, kIntrinsicStop, {op.getVerbosity()}, {});
  if (auto op = dyn_cast<sim::SimFatalOp>(operation))
    return emitIntrinsic(plan, kIntrinsicFatal, {op.getVerbosity()}, {});
  if (auto op = dyn_cast<sim::SimTerminationRequestedOp>(operation))
    return emitIntrinsic(plan, kIntrinsicTerminationRequested, {},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimTimeNowOp>(operation))
    return emitIntrinsic(plan, kIntrinsicTimeNow, {}, {op.getResult()});
  if (auto op = dyn_cast<sim::SimDisplayOp>(operation))
    return encodeDisplay(plan, op);
  if (auto op = dyn_cast<sim::SimFileOpenMCDOp>(operation))
    return emitIntrinsic(plan, kIntrinsicFileOpenMCD, {op.getPath()},
                         {op.getDescriptor()});
  if (auto op = dyn_cast<sim::SimFileOpenOp>(operation))
    return emitIntrinsic(plan, kIntrinsicFileOpen, {op.getPath(), op.getMode()},
                         {op.getDescriptor()});
  if (auto op = dyn_cast<sim::SimFileOpenStringMCDOp>(operation))
    return emitIntrinsic(plan, kIntrinsicFileOpenStringMCD, {op.getPath()},
                         {op.getDescriptor()});
  if (auto op = dyn_cast<sim::SimFileOpenStringOp>(operation))
    return emitIntrinsic(plan, kIntrinsicFileOpenString,
                         {op.getPath(), op.getMode()}, {op.getDescriptor()});
  if (auto op = dyn_cast<sim::SimFileGetlineStringOp>(operation))
    return emitIntrinsic(plan, kIntrinsicFileGetlineString,
                         {op.getDescriptor()}, {op.getData(), op.getCount()});
  if (auto op = dyn_cast<sim::SimTimeFormatOp>(operation))
    return emitIntrinsic(plan, kIntrinsicTimeFormat,
                         {op.getUnits(), op.getFractionDigits(),
                          op.getSuffix(), op.getWidth()},
                         {});
  if (auto op = dyn_cast<sim::SimPlusargTestOp>(operation))
    return emitIntrinsic(plan, kIntrinsicPlusargTest, {op.getName()},
                         {op.getFound()});
  if (auto op = dyn_cast<sim::SimPlusargValueOp>(operation))
    return emitIntrinsic(plan, kIntrinsicPlusargValue, {op.getPrefix()},
                         {op.getTail(), op.getFound()});
  if (auto op = dyn_cast<sim::SimFileErrorStringOp>(operation))
    return emitIntrinsic(plan, kIntrinsicFileErrorString, {op.getDescriptor()},
                         {op.getMessage(), op.getCode()});
  if (auto op = dyn_cast<sim::SimFileCloseOp>(operation))
    return emitIntrinsic(plan, kIntrinsicFileClose, {op.getDescriptor()}, {});
  if (auto op = dyn_cast<sim::SimFileFlushOp>(operation))
    return emitIntrinsic(plan, kIntrinsicFileFlush, {op.getDescriptor()}, {});
  if (auto op = dyn_cast<sim::SimFileGetcOp>(operation))
    return emitIntrinsic(plan, kIntrinsicFileGetc, {op.getDescriptor()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimFileUngetcOp>(operation))
    return emitIntrinsic(plan, kIntrinsicFileUngetc,
                         {op.getByte(), op.getDescriptor()}, {op.getResult()});
  if (auto op = dyn_cast<sim::SimFileGetlineOp>(operation))
    return emitIntrinsic(plan, kIntrinsicFileGetline, {op.getDescriptor()},
                         {op.getData(), op.getCount()});
  if (auto op = dyn_cast<sim::SimFileReadPackedOp>(operation))
    return emitIntrinsic(plan, kIntrinsicFileReadPacked, {op.getDescriptor()},
                         {op.getData(), op.getCount()});
  if (auto op = dyn_cast<sim::SimFileEofOp>(operation))
    return emitIntrinsic(plan, kIntrinsicFileEof, {op.getDescriptor()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimFileSeekOp>(operation))
    return emitIntrinsic(plan, kIntrinsicFileSeek,
                         {op.getDescriptor(), op.getOffset(), op.getOrigin()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimFileTellOp>(operation))
    return emitIntrinsic(plan, kIntrinsicFileTell, {op.getDescriptor()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimFileRewindOp>(operation))
    return emitIntrinsic(plan, kIntrinsicFileRewind, {op.getDescriptor()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimSpawnOp>(operation)) {
    auto found = indices.find(op.getCallee());
    if (found == indices.end())
      return op.emitOpError("spawn target has no bytecode body");
    FunctionPlan &callee = plans[found->second];
    if (!callee.frame ||
        callee.function.getEntryKind() == sim::EntryKind::Function)
      return op.emitOpError("spawn target is not a simulation process");
    SmallVector<Value> captures(op.getOperands());
    if (found->second > OBELISK_RT_INTRINSIC_SPAWN_FUNCTION_MASK)
      return op.emitOpError("spawn target index exceeds bytecode encoding");
    sim::EntryKind entryKind = callee.function.getEntryKind();
    bool startup = sim::isStartupEntryKind(entryKind) ||
                   (entryKind == sim::EntryKind::Initial &&
                    callee.function.getHomeRegion() ==
                        sim::EventRegion::Active);
    bool prioritySignalResume =
        callee.function->hasAttr("obelisk_sim.priority_signal_resume");
    if (prioritySignalResume &&
        (!callee.function->hasAttr("internal") ||
         !callee.function->hasAttr("obelisk_sim.concurrent_cancel") ||
         !callee.function->hasAttr("obelisk_sim.detached_controls") ||
         entryKind != sim::EntryKind::Fork ||
         callee.function.getHomeRegion() != sim::EventRegion::Reactive))
      return op.emitOpError(
          "priority signal resume is reserved for internal concurrent-disable "
          "observers");
    uint32_t flags = found->second |
                     (startup
                          ? OBELISK_RT_INTRINSIC_SPAWN_STARTUP
                          : 0) |
                     (callee.function->hasAttr(
                          "obelisk_sim.detached_controls")
                          ? OBELISK_RT_INTRINSIC_SPAWN_DETACHED_CONTROLS
                          : 0) |
                     (prioritySignalResume
                          ? OBELISK_RT_INTRINSIC_SPAWN_PRIORITY_SIGNAL
                          : 0);
    return emitIntrinsic(plan, kIntrinsicSpawn, captures, {op.getProcess()},
                         flags);
  }
  if (auto op = dyn_cast<sim::SimNBAEnqueueOp>(operation)) {
    SmallVector<Value> inputs{op.getValue(), op.getDestination()};
    if (op.getDelay())
      inputs.push_back(op.getDelay());
    sim::NBASiteAttr site = op.getSiteAttr();
    bool staticallyStaged =
        site && staticNBASites.contains(site.getId()) &&
        !isa<sim::StringType>(op.getValue().getType()) &&
        !sim::isManagedHandleType(op.getValue().getType()) && !op.getDelay() &&
        !site.getTiming() &&
        site.getStorage() != sim::ComputeNBAStorageKind::DynamicFrontier;
    if (!staticallyStaged)
      return emitIntrinsic(plan, kIntrinsicNBA, inputs, {});
    uint32_t siteRegister = emitU64Constant(plan, site.getId());
    if (siteRegister == kInvalidRegister)
      return op.emitOpError("cannot encode static NBA site identity");
    SmallVector<uint32_t> inputRegisters;
    llvm::transform(inputs, std::back_inserter(inputRegisters),
                    [&](Value value) { return reg(plan, value); });
    inputRegisters.push_back(siteRegister);
    return emitIntrinsicRegisters(plan, kIntrinsicStaticNBA, inputRegisters,
                                  {});
  }
  if (auto op = dyn_cast<sim::SimEventTriggerOp>(operation)) {
    SmallVector<Value> inputs{op.getEvent()};
    if (op.getDelay())
      inputs.push_back(op.getDelay());
    return emitIntrinsic(plan, kIntrinsicEventTrigger, inputs, {},
                         op.getNonblocking() ? 1 : 0);
  }
  if (auto op = dyn_cast<sim::SimEventTriggeredOp>(operation))
    return emitIntrinsic(plan, kIntrinsicEventTriggered, {op.getEvent()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimEventEqualOp>(operation)) {
    emit({Compare, OBELISK_RT_DB_CMP_EQ, reg(plan, op.getResult()),
          reg(plan, op.getLhs()), reg(plan, op.getRhs())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimProcessNullOp>(operation)) {
    Layout layout = plan.layouts[reg(plan, op.getResult())];
    emit({Constant, 0, reg(plan, op.getResult()), 0, 0, 0, 0,
          addConstant(layout, APInt(64, 0))});
    return success();
  }
  if (auto op = dyn_cast<sim::SimProcessCurrentOp>(operation))
    return emitIntrinsic(plan, kIntrinsicProcessCurrent, {}, {op.getResult()});
  if (auto op = dyn_cast<sim::SimProcessEqualOp>(operation)) {
    emit({Compare, OBELISK_RT_DB_CMP_EQ, reg(plan, op.getEqual()),
          reg(plan, op.getLhs()), reg(plan, op.getRhs())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimProcessStatusOp>(operation))
    return emitIntrinsic(plan, kIntrinsicProcessStatus, {op.getProcess()},
                         {op.getStatus()});
  if (std::optional<LogicalResult> encoded =
          encodeContainerOperation(plan, operation))
    return *encoded;
  if (std::optional<LogicalResult> encoded =
          encodeStringOperation(plan, operation))
    return *encoded;
  if (std::optional<LogicalResult> encoded =
          encodeClassOperation(plan, operation))
    return *encoded;
  if (std::optional<LogicalResult> encoded =
          encodeCoverageOperation(plan, operation))
    return *encoded;
  if (std::optional<LogicalResult> encoded =
          encodeManagedReferenceOperation(plan, operation))
    return *encoded;
  if (auto call = dyn_cast<sim::SimCallOp>(operation))
    return encodeCall(plan, call);
  if (auto call = dyn_cast<sim::SimTaskCallOp>(operation))
    return encodeTaskCall(plan, call);
  if (auto call = dyn_cast<sim::SimDPICallOp>(operation))
    return encodeDPICall(plan, call);
  if (auto returnOp = dyn_cast<sim::SimReturnOp>(operation))
    return encodeReturn(plan, returnOp);
  if (auto constant = dyn_cast<sim::SimLogicConstantOp>(operation)) {
    APInt unknown = constant.getUnknown();
    return emitConstant(plan, constant.getResult(), constant.getValue(),
                        &unknown);
  }
  if (auto op = dyn_cast<sim::SimLogicCountBitsOp>(operation)) {
    SmallVector<Value> inputs{op.getInput()};
    llvm::append_range(inputs, op.getControls());
    return emitIntrinsic(plan, kIntrinsicCountBits, inputs, {op.getResult()});
  }
  if (auto op = dyn_cast<sim::SimLogicClog2Op>(operation))
    return emitIntrinsic(plan, kIntrinsicClog2, {op.getInput()},
                         {op.getResult()});
  if (auto constant = dyn_cast<sim::SimTimeConstantOp>(operation)) {
    APInt value(64, constant.getValue());
    return emitConstant(plan, constant.getResult(), value);
  }
  if (auto add = dyn_cast<sim::SimTimeAddOp>(operation))
    return binary(Add, add.getResult(), add.getLhs(), add.getRhs());
  if (auto op = dyn_cast<sim::SimTimeToRealOp>(operation)) {
    uint32_t scale = temporary(plan, IntegerType::get(op.getContext(), 64));
    if (scale == kInvalidRegister)
      return failure();
    emit({Constant, 0, scale, 0, 0, 0, 0,
          addConstant(plan.layouts[scale], APInt(64, op.getScale()))});
    return emitIntrinsicRegisters(plan, kIntrinsicTimeToReal,
                                  {reg(plan, op.getInput()), scale},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimTimeFromRealOp>(operation)) {
    Type i64 = IntegerType::get(op.getContext(), 64);
    uint32_t scale = temporary(plan, i64);
    uint32_t quantum = temporary(plan, i64);
    if (scale == kInvalidRegister || quantum == kInvalidRegister)
      return failure();
    emit({Constant, 0, scale, 0, 0, 0, 0,
          addConstant(plan.layouts[scale], APInt(64, op.getScale()))});
    emit({Constant, 0, quantum, 0, 0, 0, 0,
          addConstant(plan.layouts[quantum], APInt(64, op.getQuantum()))});
    return emitIntrinsicRegisters(plan, kIntrinsicTimeFromReal,
                                  {reg(plan, op.getInput()), scale, quantum},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimRealFromIntegerOp>(operation))
    return emitIntrinsic(plan, kIntrinsicRealFromInteger, {op.getInput()},
                         {op.getResult()}, op.getIsSigned() ? 1 : 0);
  if (auto op = dyn_cast<sim::SimRealToIntegerOp>(operation))
    return emitIntrinsic(plan, kIntrinsicRealToInteger, {op.getInput()},
                         {op.getResult()}, op.getIsSigned() ? 1 : 0);
  if (auto scale = dyn_cast<sim::SimTimeScaleOp>(operation)) {
    uint32_t multiplier = temporary(plan, scale.getResult().getType());
    if (multiplier == kInvalidRegister)
      return failure();
    APInt value(64, scale.getScale());
    emit({Constant, 0, multiplier, 0, 0, 0, 0,
          addConstant(plan.layouts[multiplier], value)});
    emit({Mul, 0, reg(plan, scale.getResult()), reg(plan, scale.getInput()),
          multiplier});
    return success();
  }
  if (isa<sim::SimLogicFromBitsOp, sim::SimLogicToBitsOp,
          sim::SimPackedFlattenOp, sim::SimPackedUnflattenOp>(operation)) {
    emit({Extract, OBELISK_RT_DB_EXTRACT_ZERO_EXTEND,
          reg(plan, operation->getResult(0)),
          reg(plan, operation->getOperand(0)), kInvalidRegister});
    return success();
  }
  if (auto op = dyn_cast<sim::SimLogicResizeOp>(operation)) {
    emit({Extract,
          static_cast<uint16_t>(op.getIsSigned()
                                    ? OBELISK_RT_DB_EXTRACT_SIGN_EXTEND
                                    : OBELISK_RT_DB_EXTRACT_ZERO_EXTEND),
          reg(plan, op.getResult()), reg(plan, op.getInput()),
          kInvalidRegister});
    return success();
  }
  if (auto op = dyn_cast<sim::SimLogicIsTrueOp>(operation)) {
    emit({Reduce, OBELISK_RT_DB_REDUCE_IS_TRUE, reg(plan, op.getResult()),
          reg(plan, op.getInput())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimLogicMuxOp>(operation)) {
    // An unknown condition merges the arms bitwise, which writes X into the
    // destination. That is only encodable while the value registers keep a
    // four-state plane; two-state ones were proven X-free, and because the
    // result's proof folds in the condition, the merge is then unreachable.
    uint32_t result = reg(plan, op.getResult());
    bool fourState =
        result == kInvalidRegister || plan.layouts[result].kind == Logic;
    emit({Select,
          static_cast<uint16_t>(fourState ? OBELISK_RT_DB_SELECT_FOUR_STATE
                                          : OBELISK_RT_DB_SELECT_BINARY),
          result, reg(plan, op.getTrueValue()), reg(plan, op.getFalseValue()),
          reg(plan, op.getCondition())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimLogicUnaryOp>(operation)) {
    switch (op.getKind()) {
    case sim::UnaryKind::Plus:
      emit({Move, 0, reg(plan, op.getResult()), reg(plan, op.getInput())});
      break;
    case sim::UnaryKind::BitNot:
      emit({Not, 0, reg(plan, op.getResult()), reg(plan, op.getInput())});
      break;
    case sim::UnaryKind::LogicalNot:
      emit({Reduce, OBELISK_RT_DB_REDUCE_LOGICAL_NOT,
            reg(plan, op.getResult()), reg(plan, op.getInput())});
      break;
    case sim::UnaryKind::Negate: {
      uint32_t zero =
          temporaryLike(plan, op.getInput().getType(), op.getResult());
      if (zero == kInvalidRegister)
        return failure();
      emit(
          {Constant, 0, zero, 0, 0, 0, 0, addZeroConstant(plan.layouts[zero])});
      emit({Sub, 0, reg(plan, op.getResult()), zero, reg(plan, op.getInput())});
      break;
    }
    }
    return success();
  }
  if (auto op = dyn_cast<sim::SimLogicReductionOp>(operation)) {
    emit({Reduce, static_cast<uint16_t>(op.getKind()),
          reg(plan, op.getResult()), reg(plan, op.getInput())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimLogicBinaryOp>(operation))
    return encodeLogicBinary(plan, op);
  if (auto op = dyn_cast<sim::SimLogicLogicalOp>(operation)) {
    Type truthType = sim::LogicType::get(op.getContext(), 1);
    uint32_t leftTruth = temporaryLike(plan, truthType, op.getResult());
    uint32_t rightTruth = temporaryLike(plan, truthType, op.getResult());
    if (leftTruth == kInvalidRegister || rightTruth == kInvalidRegister)
      return failure();
    emit({Reduce, OBELISK_RT_DB_REDUCE_LOGICAL_VALUE, leftTruth,
          reg(plan, op.getLhs())});
    emit({Reduce, OBELISK_RT_DB_REDUCE_LOGICAL_VALUE, rightTruth,
          reg(plan, op.getRhs())});
    emit({op.getKind() == sim::LogicalKind::And ? And : Or, 0,
          reg(plan, op.getResult()), leftTruth, rightTruth});
    return success();
  }
  if (auto op = dyn_cast<sim::SimLogicShiftOp>(operation)) {
    uint16_t opcode = op.getKind() == sim::ShiftKind::Left    ? Shl
                      : op.getKind() == sim::ShiftKind::Right ? LShr
                                                              : AShr;
    return binary(opcode, op.getResult(), op.getInput(), op.getAmount());
  }
  if (auto op = dyn_cast<sim::SimLogicCompareOp>(operation))
    return encodeLogicCompare(plan, op);
  if (auto op = dyn_cast<sim::SimLogicExtractOp>(operation)) {
    emit({Extract, OBELISK_RT_DB_EXTRACT_ZERO_EXTEND,
          reg(plan, op.getResult()), reg(plan, op.getInput()),
          kInvalidRegister, 0, 0, op.getLowBit()});
    return success();
  }
  if (auto op = dyn_cast<sim::SimLogicDynExtractOp>(operation)) {
    emit({Extract, OBELISK_RT_DB_EXTRACT_ZERO_EXTEND,
          reg(plan, op.getResult()), reg(plan, op.getInput()),
          reg(plan, op.getLowBit())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimBitsDynExtractOp>(operation)) {
    emit({Extract, OBELISK_RT_DB_EXTRACT_ZERO_EXTEND,
          reg(plan, op.getResult()), reg(plan, op.getInput()),
          reg(plan, op.getLowBit())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimLogicInsertOp>(operation)) {
    emit({Insert, 0, reg(plan, op.getResult()), reg(plan, op.getInput()),
          reg(plan, op.getReplacement()), 0, 0, op.getLowBit()});
    return success();
  }
  if (auto op = dyn_cast<sim::SimLogicConcatOp>(operation))
    return encodeConcat(plan, op);
  if (auto op = dyn_cast<sim::SimLogicReplicateOp>(operation))
    return encodeReplicate(plan, op);
  if (auto op = dyn_cast<sim::SimAggregateDefaultOp>(operation)) {
    uint32_t destination = reg(plan, op.getResult());
    Layout layout = plan.layouts[destination];
    APInt value = APInt::getZero(layout.width);
    APInt unknown = layout.kind == Logic ? APInt::getAllOnes(layout.width)
                                         : APInt::getZero(layout.width);
    return emitConstant(plan, op.getResult(), value,
                        layout.kind == Logic ? &unknown : nullptr);
  }
  if (auto op = dyn_cast<sim::SimAggregateConstructOp>(operation)) {
    uint32_t destination = reg(plan, op.getResult());
    emit({Constant, 0, destination, 0, 0, 0, 0,
          addZeroConstant(plan.layouts[destination])});
    for (auto [index, element] : llvm::enumerate(op.getElements())) {
      auto subelement = sim::getAggregateProvenanceSubelement(
          op.getResult().getType(), index);
      if (!subelement)
        return op.emitOpError("aggregate element has no packed provenance");
      uint32_t elementRegister = reg(plan, element);
      uint16_t flags =
          isManagedAggregateWord(plan.layouts[elementRegister].kind)
              ? OBELISK_RT_DB_AGGREGATE_MANAGED
              : 0;
      emit({Insert, flags, destination, destination, elementRegister, 0, 0,
            subelement->first});
    }
    return success();
  }
  if (auto op = dyn_cast<sim::SimAggregateExtractOp>(operation)) {
    auto subelement = sim::getAggregateProvenanceSubelement(
        op.getInput().getType(), static_cast<unsigned>(op.getIndex()));
    if (!subelement)
      return op.emitOpError("aggregate element has no packed provenance");
    uint32_t destination = reg(plan, op.getResult());
    uint16_t flags = isManagedAggregateWord(plan.layouts[destination].kind)
                         ? OBELISK_RT_DB_AGGREGATE_MANAGED
                         : 0;
    emit({Extract, flags, destination, reg(plan, op.getInput()),
          kInvalidRegister, 0, 0, subelement->first});
    return success();
  }
  if (auto op = dyn_cast<sim::SimAggregateInsertOp>(operation)) {
    auto subelement = sim::getAggregateProvenanceSubelement(
        op.getInput().getType(), static_cast<unsigned>(op.getIndex()));
    if (!subelement)
      return op.emitOpError("aggregate element has no packed provenance");
    uint32_t replacement = reg(plan, op.getReplacement());
    uint16_t flags = isManagedAggregateWord(plan.layouts[replacement].kind)
                         ? OBELISK_RT_DB_AGGREGATE_MANAGED
                         : 0;
    emit({Insert, flags, reg(plan, op.getResult()), reg(plan, op.getInput()),
          replacement, 0, 0, subelement->first});
    return success();
  }
  if (auto op = dyn_cast<sim::SimArrayDynExtractOp>(operation))
    return encodeArrayExtract(plan, op);
  if (auto op = dyn_cast<sim::SimUnionConstructOp>(operation))
    return encodeUnionConstruct(plan, op);
  if (auto op = dyn_cast<sim::SimUnionExtractOp>(operation)) {
    uint32_t destination = reg(plan, op.getResult());
    uint16_t flags = isManagedAggregateWord(plan.layouts[destination].kind)
                         ? OBELISK_RT_DB_AGGREGATE_MANAGED
                         : 0;
    emit({Extract, flags, destination, reg(plan, op.getInput()),
          kInvalidRegister});
    return success();
  }
  if (auto op = dyn_cast<sim::SimUnionIsActiveOp>(operation))
    return encodeUnionIsActive(plan, op);
  if (auto op = dyn_cast<sim::SimRefAllocOp>(operation)) {
    std::optional<uint32_t> width =
        simulationWidth(op.getInitialValue().getType());
    if (!width)
      return op.emitOpError("automatic storage has no fixed width");
    SmallVector<uint32_t> inputs{reg(plan, op.getInitialValue())};
    SmallVector<uint64_t, 2> rootOffsets;
    if (!sim::getManagedHandleOffsets(op.getInitialValue().getType(),
                                      rootOffsets))
      return op.emitOpError("automatic storage has no managed root layout");
    if (plan.layouts[inputs.front()].kind != Managed)
      for (uint64_t offset : rootOffsets)
        inputs.push_back(emitU64Constant(plan, offset));
    return emitIntrinsicRegisters(plan, kIntrinsicStateAlloc, inputs,
                                  {reg(plan, op.getResult())});
  }
  if (isa<sim::SimDisableChildrenOp>(operation))
    return emitIntrinsic(plan, kIntrinsicDisableChildren, {}, {});
  if (auto op = dyn_cast<sim::SimControlEnterOp>(operation)) {
    if (op.getTargetId() == 0 || op.getTargetId() > UINT32_MAX)
      return op.emitOpError("control target ID does not fit bytecode");
    return emitIntrinsic(plan, kIntrinsicControlEnter, {}, {op.getControl()},
                         static_cast<uint32_t>(op.getTargetId()));
  }
  if (auto op = dyn_cast<sim::SimControlLeaveOp>(operation))
    return emitIntrinsic(plan, kIntrinsicControlLeave, {op.getControl()}, {});
  if (auto op = dyn_cast<sim::SimControlDisableOp>(operation)) {
    if (op.getTargetId() == 0 || op.getTargetId() > INT32_MAX)
      return op.emitOpError("control target ID does not fit bytecode");
    SmallVector<Value> inputs;
    if (op.getActivation())
      inputs.push_back(op.getActivation());
    uint32_t flags = static_cast<uint32_t>(op.getTargetId());
    if (op.getHierarchical())
      flags |= UINT32_C(1) << 31;
    return emitIntrinsic(plan, kIntrinsicControlDisable, inputs, {}, flags);
  }
  if (auto op = dyn_cast<sim::SimStaticOnceOp>(operation)) {
    if (op.getId() == 0 || op.getId() > UINT32_MAX)
      return op.emitOpError("static initialization ID does not fit bytecode");
    return emitIntrinsic(plan, kIntrinsicStaticOnce, {}, {op.getFirst()},
                         static_cast<uint32_t>(op.getId()));
  }
  if (auto op = dyn_cast<sim::SimDeferredOnceOp>(operation)) {
    if (op.getId() == 0)
      return op.emitOpError("deferred assertion ID must be positive");
    return emitIntrinsicRegisters(
        plan, kIntrinsicDeferredOnce,
        {emitU64Constant(plan, static_cast<uint64_t>(op.getId()))},
        {reg(plan, op.getFirst())});
  }
  if (auto op = dyn_cast<sim::SimDeferredEnqueueOp>(operation)) {
    if (op.getId() == 0)
      return op.emitOpError("deferred assertion ID must be positive");
    SmallVector<uint32_t> inputs{
        emitU64Constant(plan, static_cast<uint64_t>(op.getId()))};
    if (auto assertionID = op->getAttrOfType<IntegerAttr>(
            "obelisk_sim.assertion_control_target_id"))
      inputs.push_back(emitU64Constant(
          plan, assertionID.getValue().getZExtValue()));
    return emitIntrinsicRegisters(plan, kIntrinsicDeferredEnqueue, inputs,
                                  {reg(plan, op.getTicket())});
  }
  if (auto op = dyn_cast<sim::SimDeferredMatureOp>(operation))
    return emitIntrinsicRegisters(plan, kIntrinsicDeferredMature,
                                  {reg(plan, op.getTicket())},
                                  {reg(plan, op.getCurrent())});
  if (auto op = dyn_cast<sim::SimAssertionControlOp>(operation))
    return emitIntrinsicRegisters(
        plan, kIntrinsicAssertionControl,
        {emitU64Constant(plan, static_cast<uint64_t>(op.getAssertionId()))}, {},
        static_cast<uint32_t>(op.getAction()));
  if (auto op = dyn_cast<sim::SimAssertionEnabledOp>(operation))
    return emitIntrinsicRegisters(
        plan, kIntrinsicAssertionEnabled,
        {emitU64Constant(plan, static_cast<uint64_t>(op.getAssertionId()))},
        {reg(plan, op.getEnabled())});
  if (auto op = dyn_cast<sim::SimAssertionActionStateOp>(operation))
    return emitIntrinsicRegisters(
        plan, kIntrinsicAssertionActionState,
        {emitU64Constant(plan, static_cast<uint64_t>(op.getAssertionId()))},
        {reg(plan, op.getState())});
  if (auto op = dyn_cast<sim::SimDumpOpenOp>(operation))
    return emitIntrinsic(plan, kIntrinsicDumpOpen, {op.getPath()}, {});
  if (auto op = dyn_cast<sim::SimDumpOpenStringOp>(operation))
    return emitIntrinsicRegisters(plan, kIntrinsicDumpOpenString,
                                  {reg(plan, op.getPath())}, {});
  if (auto op = dyn_cast<sim::SimDumpTimescaleOp>(operation))
    return emitIntrinsic(plan, kIntrinsicDumpTimescale, {op.getExponent()}, {});
  if (auto op = dyn_cast<sim::SimDumpVarsOp>(operation))
    return emitIntrinsic(plan, kIntrinsicDumpVars,
                         {op.getLevels(), op.getScope()}, {});
  if (isa<sim::SimDumpAllOp>(operation))
    return emitIntrinsic(plan, kIntrinsicDumpAll, {}, {});
  if (auto op = dyn_cast<sim::SimDumpControlOp>(operation))
    return emitIntrinsic(plan, kIntrinsicDumpControl, {}, {},
                         op.getEnabled() ? 1 : 0);
  if (auto op = dyn_cast<sim::SimDumpLimitOp>(operation))
    return emitIntrinsic(plan, kIntrinsicDumpLimit, {op.getBytes()}, {});
  if (isa<sim::SimDumpFlushOp>(operation))
    return emitIntrinsic(plan, kIntrinsicDumpFlush, {}, {});
  if (auto op = dyn_cast<sim::SimMonitorRegisterOp>(operation))
    return emitIntrinsic(plan, kIntrinsicMonitorRegister, {op.getProcess()},
                         {});
  if (auto op = dyn_cast<sim::SimMonitorControlOp>(operation))
    return emitIntrinsic(plan, kIntrinsicMonitorControl, {}, {},
                         op.getEnabled() ? 1 : 0);
  if (auto op = dyn_cast<sim::SimMonitorCurrentOp>(operation))
    return emitIntrinsic(plan, kIntrinsicMonitorCurrent, {}, {op.getCurrent()});
  if (std::optional<LogicalResult> encoded =
          encodeStateOperation(plan, operation))
    return *encoded;
  if (std::optional<LogicalResult> encoded =
          encodeSuspensionOperation(plan, operation))
    return *encoded;
  return operation->emitOpError()
         << "has no design-bytecode semantics (the normalized legality set "
            "is closed, so executable fallback is forbidden)";
}

} // namespace obelisk::bytecode
