//===- BytecodeOperationEncoding.cpp - Bytecode instruction selection ----===//

#include "BytecodeEncoder.h"
#include "BytecodeSerialization.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Math/IR/Math.h"

#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;

namespace obelisk::bytecode {

LogicalResult Encoder::encodeOperation(FunctionPlan &plan,
                                       Operation *operation) {
  if (auto constant = dyn_cast<arith::ConstantOp>(operation)) {
    if (auto integer = dyn_cast<IntegerAttr>(constant.getValue()))
      return emitConstant(plan, constant.getResult(), integer.getValue());
    if (auto floating = dyn_cast<FloatAttr>(constant.getValue()))
      return emitConstant(plan, constant.getResult(),
                          floating.getValue().bitcastToAPInt());
    return operation->emitOpError(
        "bytecode requires integer or floating-point constants");
  }
  auto binary = [&](uint16_t opcode, Value result, Value left, Value right) {
    emit({opcode, 0, reg(plan, result), reg(plan, left), reg(plan, right)});
    return success();
  };
  if (auto op = dyn_cast<arith::AddIOp>(operation))
    return binary(Add, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::SubIOp>(operation))
    return binary(Sub, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::MulIOp>(operation))
    return binary(Mul, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::AddFOp>(operation))
    return binary(FAdd, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::SubFOp>(operation))
    return binary(FSub, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::MulFOp>(operation))
    return binary(FMul, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::DivFOp>(operation))
    return binary(FDiv, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::NegFOp>(operation)) {
    emit({FNeg, 0, reg(plan, op.getResult()), reg(plan, op.getOperand())});
    return success();
  }
  if (auto op = dyn_cast<arith::DivUIOp>(operation))
    return binary(UDiv, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::DivSIOp>(operation))
    return binary(SDiv, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::RemUIOp>(operation))
    return binary(URem, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::RemSIOp>(operation))
    return binary(SRem, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::AndIOp>(operation))
    return binary(And, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::OrIOp>(operation))
    return binary(Or, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::XOrIOp>(operation))
    return binary(Xor, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::ShLIOp>(operation))
    return binary(Shl, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::ShRUIOp>(operation))
    return binary(LShr, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::ShRSIOp>(operation))
    return binary(AShr, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::CmpIOp>(operation)) {
    uint16_t predicate = 0;
    switch (op.getPredicate()) {
    case arith::CmpIPredicate::eq:
      predicate = 0;
      break;
    case arith::CmpIPredicate::ne:
      predicate = 1;
      break;
    case arith::CmpIPredicate::ult:
      predicate = 2;
      break;
    case arith::CmpIPredicate::ule:
      predicate = 3;
      break;
    case arith::CmpIPredicate::ugt:
      predicate = 4;
      break;
    case arith::CmpIPredicate::uge:
      predicate = 5;
      break;
    case arith::CmpIPredicate::slt:
      predicate = 6;
      break;
    case arith::CmpIPredicate::sle:
      predicate = 7;
      break;
    case arith::CmpIPredicate::sgt:
      predicate = 8;
      break;
    case arith::CmpIPredicate::sge:
      predicate = 9;
      break;
    }
    emit({Compare, predicate, reg(plan, op.getResult()), reg(plan, op.getLhs()),
          reg(plan, op.getRhs())});
    return success();
  }
  if (auto op = dyn_cast<arith::CmpFOp>(operation)) {
    uint32_t predicate;
    switch (op.getPredicate()) {
    case arith::CmpFPredicate::OEQ:
      predicate = 0;
      break;
    case arith::CmpFPredicate::UNE:
      predicate = 1;
      break;
    case arith::CmpFPredicate::OLT:
      predicate = 2;
      break;
    case arith::CmpFPredicate::OLE:
      predicate = 3;
      break;
    case arith::CmpFPredicate::OGT:
      predicate = 4;
      break;
    case arith::CmpFPredicate::OGE:
      predicate = 5;
      break;
    default:
      return op.emitOpError("floating comparison predicate is not executable");
    }
    emit({FCompare, static_cast<uint16_t>(predicate), reg(plan, op.getResult()),
          reg(plan, op.getLhs()), reg(plan, op.getRhs())});
    return success();
  }
  if (auto op = dyn_cast<arith::SelectOp>(operation)) {
    emit({Select, 0, reg(plan, op.getResult()), reg(plan, op.getTrueValue()),
          reg(plan, op.getFalseValue()), reg(plan, op.getCondition())});
    return success();
  }
  if (auto op = dyn_cast<arith::ExtUIOp>(operation)) {
    emit({Extract, 0, reg(plan, op.getResult()), reg(plan, op.getIn()),
          kInvalidRegister});
    return success();
  }
  if (auto op = dyn_cast<arith::ExtSIOp>(operation)) {
    emit({Extract, 1, reg(plan, op.getResult()), reg(plan, op.getIn()),
          kInvalidRegister});
    return success();
  }
  if (auto op = dyn_cast<arith::TruncIOp>(operation)) {
    emit({Extract, 0, reg(plan, op.getResult()), reg(plan, op.getIn()),
          kInvalidRegister});
    return success();
  }
  if (auto op = dyn_cast<arith::ExtFOp>(operation)) {
    emit({FExt, 0, reg(plan, op.getResult()), reg(plan, op.getIn())});
    return success();
  }
  if (auto op = dyn_cast<arith::TruncFOp>(operation)) {
    emit({FTrunc, 0, reg(plan, op.getResult()), reg(plan, op.getIn())});
    return success();
  }
  if (auto op = dyn_cast<math::PowFOp>(operation))
    return binary(FPow, op.getResult(), op.getLhs(), op.getRhs());
  if (auto op = dyn_cast<arith::IndexCastOp>(operation)) {
    emit({Extract, 0, reg(plan, op.getResult()), reg(plan, op.getIn()),
          kInvalidRegister});
    return success();
  }
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
      emit({Compare, 0, match, reg(plan, switchOp.getFlag()), caseValue});
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
    return emitIntrinsic(plan, kIntrinsicFinish, {op.getVerbosity()}, {});
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
    return emitIntrinsic(plan, kIntrinsicSpawn, captures, {op.getProcess()},
                         found->second);
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
    emit({Compare, 0, reg(plan, op.getResult()), reg(plan, op.getLhs()),
          reg(plan, op.getRhs())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimContainerSizeOp>(operation))
    return emitIntrinsic(plan, kIntrinsicContainerSize, {op.getContainer()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimContainerCreateLikeOp>(operation))
    return emitIntrinsic(plan, kIntrinsicContainerCreateLike,
                         {op.getPreferred(), op.getFallback(), op.getSize()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimContainerCreateOp>(operation)) {
    SmallVector<uint8_t> traceSlots;
    for (auto [offset, kind] :
         llvm::zip_equal(op.getTraceOffsets(), op.getTraceKinds())) {
      append64(traceSlots, static_cast<uint64_t>(offset));
      append32(traceSlots, static_cast<uint32_t>(kind));
      append32(traceSlots, 0);
    }
    return emitIntrinsicRegisters(plan, kIntrinsicContainerCreate,
                                  {emitU64Constant(plan, op.getContainerKind()),
                                   emitU64Constant(plan, op.getTypeId()),
                                   emitU64Constant(plan, op.getElementKind()),
                                   emitU64Constant(plan, op.getElementFlags()),
                                   emitU64Constant(plan, op.getValueSize()),
                                   emitU64Constant(plan, op.getAlignment()),
                                   emitU64Constant(plan, op.getBitWidth()),
                                   emitBytesConstant(plan, traceSlots),
                                   reg(plan, op.getSize()),
                                   emitU64Constant(plan, op.getBound())},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimContainerCloneOp>(operation))
    return emitIntrinsic(plan, kIntrinsicContainerClone, {op.getInput()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimContainerDeleteOp>(operation))
    return emitIntrinsic(plan, kIntrinsicContainerDelete, {op.getContainer()},
                         {});
  if (auto op = dyn_cast<sim::SimQueueDeleteOp>(operation))
    return emitIntrinsic(plan, kIntrinsicQueueDelete,
                         {op.getQueue(), op.getIndex()}, {});
  if (auto op = dyn_cast<sim::SimQueueInsertOp>(operation))
    return emitIntrinsic(plan, kIntrinsicQueueInsert,
                         {op.getQueue(), op.getIndex(), op.getValue()}, {});
  if (auto op = dyn_cast<sim::SimRandomNextOp>(operation))
    return emitIntrinsic(plan, kIntrinsicRandomNext, {}, {op.getResult()});
  if (auto op = dyn_cast<sim::SimRandomSeedOp>(operation))
    return emitIntrinsic(plan, kIntrinsicRandomSeed, {op.getSeed()}, {});
  if (auto op = dyn_cast<sim::SimRandomBoundedOp>(operation))
    return emitIntrinsic(plan, kIntrinsicRandomBounded, {op.getBound()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimContainerReadOp>(operation))
    return emitIntrinsic(plan, kIntrinsicContainerRead,
                         {op.getContainer(), op.getIndex()}, {op.getResult()});
  if (auto op = dyn_cast<sim::SimContainerWriteOp>(operation))
    return emitIntrinsic(plan, kIntrinsicContainerWrite,
                         {op.getContainer(), op.getIndex(), op.getValue()}, {});
  if (auto op = dyn_cast<sim::SimAssocCreateOp>(operation)) {
    SmallVector<uint8_t> traceSlots;
    for (auto [offset, kind] :
         llvm::zip_equal(op.getTraceOffsets(), op.getTraceKinds())) {
      append64(traceSlots, static_cast<uint64_t>(offset));
      append32(traceSlots, static_cast<uint32_t>(kind));
      append32(traceSlots, 0);
    }
    return emitIntrinsicRegisters(plan, kIntrinsicAssocCreate,
                                  {emitU64Constant(plan, op.getTypeId()),
                                   emitU64Constant(plan, op.getElementKind()),
                                   emitU64Constant(plan, op.getElementFlags()),
                                   emitU64Constant(plan, op.getValueSize()),
                                   emitU64Constant(plan, op.getAlignment()),
                                   emitU64Constant(plan, op.getBitWidth()),
                                   emitBytesConstant(plan, traceSlots),
                                   emitU64Constant(plan, op.getKeyKind()),
                                   emitU64Constant(plan, op.getKeyWidth())},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimAssocReadOp>(operation))
    return emitIntrinsic(plan, kIntrinsicAssocRead,
                         {op.getArray(), op.getKey()}, {op.getResult()});
  if (auto op = dyn_cast<sim::SimAssocWriteOp>(operation))
    return emitIntrinsic(plan, kIntrinsicAssocWrite,
                         {op.getArray(), op.getKey(), op.getValue()}, {});
  if (auto op = dyn_cast<sim::SimAssocExistsOp>(operation))
    return emitIntrinsic(plan, kIntrinsicAssocExists,
                         {op.getArray(), op.getKey()}, {op.getResult()});
  if (auto op = dyn_cast<sim::SimAssocDeleteOp>(operation))
    return emitIntrinsic(plan, kIntrinsicAssocDelete,
                         {op.getArray(), op.getKey()}, {});
  if (auto op = dyn_cast<sim::SimAssocSetDefaultOp>(operation))
    return emitIntrinsic(plan, kIntrinsicAssocDefault,
                         {op.getArray(), op.getValue()}, {});
  if (auto op = dyn_cast<sim::SimAssocTraverseOp>(operation))
    return emitIntrinsicRegisters(
        plan, kIntrinsicAssocTraverse,
        {reg(plan, op.getArray()), reg(plan, op.getKey()),
         emitU64Constant(plan, static_cast<uint64_t>(static_cast<int64_t>(
                                   static_cast<int32_t>(op.getDirection())))),
         emitU64Constant(plan, op.getEndpoint() ? 1 : 0)},
        {reg(plan, op.getResultKey()), reg(plan, op.getSuccess())});
  if (auto op = dyn_cast<sim::SimStringLiteralOp>(operation)) {
    StringRef value = op.getValue();
    uint32_t bytes = emitBytesConstant(
        plan, ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(value.data()),
                                value.size()));
    if (bytes == kInvalidRegister)
      return op.emitOpError("cannot allocate literal byte register");
    return emitIntrinsicRegisters(plan, kIntrinsicStringLiteral, {bytes},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimStringFromPackedOp>(operation))
    return emitIntrinsic(plan, kIntrinsicStringFromPacked, {op.getInput()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimStringToPackedOp>(operation))
    return emitIntrinsic(plan, kIntrinsicStringToPacked, {op.getInput()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimStringConcatOp>(operation)) {
    SmallVector<Value> inputs(op.getInputs());
    return emitIntrinsic(plan, kIntrinsicStringConcat, inputs,
                         {op.getResult()});
  }
  if (auto op = dyn_cast<sim::SimStringRepeatOp>(operation))
    return emitIntrinsic(plan, kIntrinsicStringRepeat,
                         {op.getInput(), op.getCount()}, {op.getResult()});
  if (auto op = dyn_cast<sim::SimStringLengthOp>(operation))
    return emitIntrinsic(plan, kIntrinsicStringLength, {op.getInput()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimStringGetcOp>(operation))
    return emitIntrinsic(plan, kIntrinsicStringGetc,
                         {op.getInput(), op.getIndex()}, {op.getResult()});
  if (auto op = dyn_cast<sim::SimStringPutcOp>(operation))
    return emitIntrinsic(plan, kIntrinsicStringPutc,
                         {op.getInput(), op.getIndex(), op.getCharacter()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimStringSubstrOp>(operation))
    return emitIntrinsic(plan, kIntrinsicStringSubstr,
                         {op.getInput(), op.getLeft(), op.getRight()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimStringCompareOp>(operation)) {
    uint32_t mode = emitU64Constant(plan, op.getCaseInsensitive() ? 1 : 0);
    return emitIntrinsicRegisters(
        plan, kIntrinsicStringCompare,
        {reg(plan, op.getLhs()), reg(plan, op.getRhs()), mode},
        {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimStringCaseConvertOp>(operation)) {
    uint32_t mode = emitU64Constant(plan, op.getToUpper() ? 1 : 0);
    return emitIntrinsicRegisters(plan, kIntrinsicStringCaseConvert,
                                  {reg(plan, op.getInput()), mode},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimStringParseIntegerOp>(operation)) {
    uint32_t radix = emitU64Constant(plan, op.getRadix());
    return emitIntrinsicRegisters(plan, kIntrinsicStringParseInteger,
                                  {reg(plan, op.getInput()), radix},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimStringParseRealOp>(operation))
    return emitIntrinsic(plan, kIntrinsicStringParseReal, {op.getInput()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimStringFormatIntegerOp>(operation)) {
    uint32_t radix = emitU64Constant(plan, op.getRadix());
    uint32_t signedMode = emitU64Constant(plan, op.getIsSigned() ? 1 : 0);
    return emitIntrinsicRegisters(plan, kIntrinsicStringFormatInteger,
                                  {reg(plan, op.getInput()), radix, signedMode},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimStringFormatRealOp>(operation))
    return emitIntrinsic(plan, kIntrinsicStringFormatReal, {op.getInput()},
                         {op.getResult()});
  if (isa<sim::SimClassNullOp, sim::SimManagedNullOp, sim::SimCovergroupNullOp>(
          operation)) {
    uint32_t destination = reg(plan, operation->getResult(0));
    emit({Constant, 0, destination, 0, 0, 0, 0,
          addZeroConstant(plan.layouts[destination])});
    return success();
  }
  if (auto op = dyn_cast<sim::SimCovergroupCreateOp>(operation)) {
    auto declaration =
        SymbolTable::lookupNearestSymbolFrom<sim::SimCovergroupDeclOp>(
            op, op.getDeclarationAttr());
    if (!declaration)
      return op.emitOpError("references an unknown covergroup declaration");
    SmallVector<uint32_t> inputs;
    inputs.push_back(emitU64Constant(plan, declaration.getId()));
    for (int64_t bins : declaration.getCoverpointBins())
      inputs.push_back(emitU64Constant(plan, static_cast<uint64_t>(bins)));
    return emitIntrinsicRegisters(plan, kIntrinsicCovergroupCreate, inputs,
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimCovergroupSampleEnabledOp>(operation))
    return emitIntrinsic(plan, kIntrinsicCovergroupSampleEnabled,
                         {op.getHandle()}, {op.getResult()});
  if (auto op = dyn_cast<sim::SimCovergroupBinHitOp>(operation)) {
    uint32_t coverpoint = emitU64Constant(plan, op.getCoverpoint());
    uint32_t bin = emitU64Constant(plan, op.getBin());
    return emitIntrinsicRegisters(plan, kIntrinsicCovergroupBinHit,
                                  {reg(plan, op.getHandle()), coverpoint, bin},
                                  {});
  }
  if (auto op = dyn_cast<sim::SimCovergroupSampleOp>(operation)) {
    SmallVector<uint32_t> inputs{reg(plan, op.getHandle())};
    llvm::append_range(inputs, llvm::map_range(op.getHits(), [&](Value hit) {
                         return reg(plan, hit);
                       }));
    return emitIntrinsicRegisters(plan, kIntrinsicCovergroupSample, inputs, {});
  }
  if (auto op = dyn_cast<sim::SimCovergroupStartOp>(operation)) {
    uint32_t enabled = emitU64Constant(plan, 1);
    return emitIntrinsicRegisters(plan, kIntrinsicCovergroupSetEnabled,
                                  {reg(plan, op.getHandle()), enabled}, {});
  }
  if (auto op = dyn_cast<sim::SimCovergroupStopOp>(operation)) {
    uint32_t enabled = emitU64Constant(plan, 0);
    return emitIntrinsicRegisters(plan, kIntrinsicCovergroupSetEnabled,
                                  {reg(plan, op.getHandle()), enabled}, {});
  }
  if (auto op = dyn_cast<sim::SimCovergroupInstanceQueryOp>(operation))
    return emitIntrinsic(plan, kIntrinsicCovergroupInstanceQuery,
                         {op.getHandle()},
                         {op.getPercentage(), op.getCovered(), op.getTotal()});
  if (auto op = dyn_cast<sim::SimCovergroupTypeQueryOp>(operation)) {
    auto declaration =
        SymbolTable::lookupNearestSymbolFrom<sim::SimCovergroupDeclOp>(
            op, op.getDeclarationAttr());
    if (!declaration)
      return op.emitOpError("references an unknown covergroup declaration");
    SmallVector<uint32_t> inputs{emitU64Constant(plan, declaration.getId())};
    for (int64_t bins : declaration.getCoverpointBins())
      inputs.push_back(emitU64Constant(plan, static_cast<uint64_t>(bins)));
    return emitIntrinsicRegisters(plan, kIntrinsicCovergroupTypeQuery, inputs,
                                  {reg(plan, op.getPercentage()),
                                   reg(plan, op.getCovered()),
                                   reg(plan, op.getTotal())});
  }
  if (auto op = dyn_cast<sim::SimManagedIsNullOp>(operation)) {
    uint32_t input = reg(plan, op.getInput());
    uint32_t zero = addZeroConstant(plan.layouts[input]);
    uint32_t constant = temporary(plan, op.getInput().getType());
    if (constant == kInvalidRegister)
      return failure();
    emit({Constant, 0, constant, 0, 0, 0, 0, zero});
    emit({Compare, OBELISK_RT_DB_CMP_EQ, reg(plan, op.getResult()), input,
          constant});
    return success();
  }
  if (isa<sim::SimEventNullOp>(operation)) {
    uint32_t destination = reg(plan, operation->getResult(0));
    const Layout &layout = plan.layouts[destination];
    if (layout.kind != Handle || layout.size != 32)
      return operation->emitOpError(
          "event null requires the canonical handle layout");
    SmallVector<uint8_t, 32> bytes(layout.size, 0);
    write32(bytes, 0, OBELISK_RT_DESCRIPTOR_EVENT);
    write64(bytes, 8, UINT64_MAX);
    write64(bytes, 16, UINT64_MAX);
    emit({Constant, 0, destination, 0, 0, 0, 0, addRawConstant(bytes)});
    return success();
  }
  if (auto op = dyn_cast<sim::SimReferencePathIndexOp>(operation))
    return emitIntrinsic(
        plan, kIntrinsicReferencePathIndex,
        {op.getContainer(), op.getIndex(), op.getOwnerReference()},
        {op.getResult()});
  if (auto op = dyn_cast<sim::SimReferencePathAssocOp>(operation))
    return emitIntrinsic(plan, kIntrinsicReferencePathAssoc,
                         {op.getArray(), op.getKey(), op.getOwnerReference()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimArgumentRefFromPathOp>(operation))
    return emitIntrinsic(plan, kIntrinsicArgumentRefFromPath, {op.getInput()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimClassAllocOp>(operation)) {
    auto type = cast<sim::ClassHandleType>(op.getResult().getType());
    FailureOr<uint64_t> id = classID(type.getClassName(), operation);
    if (failed(id))
      return failure();
    uint32_t classRegister = emitU64Constant(plan, *id);
    return emitIntrinsicRegisters(plan, kIntrinsicClassAlloc, {classRegister},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimClassCopyOp>(operation)) {
    auto type = cast<sim::ClassHandleType>(op.getResult().getType());
    FailureOr<uint64_t> id = classID(type.getClassName(), operation);
    if (failed(id))
      return failure();
    uint32_t classRegister = emitU64Constant(plan, *id);
    return emitIntrinsicRegisters(plan, kIntrinsicClassCopy,
                                  {reg(plan, op.getSource()), classRegister},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimClassIsInstanceOp>(operation)) {
    FailureOr<uint64_t> id = classID(op.getTargetAttr(), operation);
    if (failed(id))
      return failure();
    uint32_t classRegister = emitU64Constant(plan, *id);
    return emitIntrinsicRegisters(plan, kIntrinsicClassIsInstance,
                                  {reg(plan, op.getObject()), classRegister},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimClassIdOp>(operation))
    return emitIntrinsic(plan, kIntrinsicClassID, {op.getObject()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimClassCastOp>(operation)) {
    auto type = cast<sim::ClassHandleType>(op.getResult().getType());
    FailureOr<uint64_t> id = classID(type.getClassName(), operation);
    if (failed(id))
      return failure();
    uint32_t classRegister = emitU64Constant(plan, *id);
    return emitIntrinsicRegisters(plan, kIntrinsicClassCast,
                                  {reg(plan, op.getObject()), classRegister},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimClassFieldRefOp>(operation)) {
    auto field = SymbolTable::lookupNearestSymbolFrom<sim::SimClassFieldDeclOp>(
        op, op.getFieldAttr());
    auto offset =
        field ? field->getAttrOfType<IntegerAttr>("offset") : IntegerAttr{};
    if (!field || !offset)
      return op.emitOpError("managed field has no bytecode layout");
    uint32_t offsetRegister =
        emitU64Constant(plan, offset.getValue().getZExtValue());
    return emitIntrinsicRegisters(plan, kIntrinsicClassFieldRef,
                                  {reg(plan, op.getObject()), offsetRegister},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimArgumentRefFromRefOp>(operation))
    return emitIntrinsic(plan, kIntrinsicArgumentRefFromRef, {op.getInput()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimArgumentRefFromManagedOp>(operation))
    return emitIntrinsic(plan, kIntrinsicArgumentRefFromManaged,
                         {op.getInput()}, {op.getResult()});
  if (auto op = dyn_cast<sim::SimArgumentRefLoadOp>(operation)) {
    FailureOr<ManagedValueStorage> storage =
        getManagedValueStorage(op.getResult().getType(), dataLayout);
    std::optional<uint32_t> width = simulationWidth(op.getResult().getType());
    if (failed(storage) || !width)
      return op.emitOpError("argument reference has no bytecode layout");
    uint32_t flags = (storage->fourState ? 1u : 0u) |
                     ((isa<sim::StringType>(op.getResult().getType())
                           ? OBELISK_RT_ARGUMENT_VALUE_STRING
                       : sim::isManagedHandleType(op.getResult().getType())
                           ? OBELISK_RT_ARGUMENT_VALUE_CLASS
                           : OBELISK_RT_ARGUMENT_VALUE_BITS)
                      << 1);
    return emitIntrinsicRegisters(plan, kIntrinsicArgumentRefLoad,
                                  {reg(plan, op.getReference()),
                                   emitU64Constant(plan, storage->planeSize),
                                   emitU64Constant(plan, *width),
                                   emitU64Constant(plan, flags)},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimArgumentRefStoreOp>(operation)) {
    FailureOr<ManagedValueStorage> storage =
        getManagedValueStorage(op.getValue().getType(), dataLayout);
    std::optional<uint32_t> width = simulationWidth(op.getValue().getType());
    if (failed(storage) || !width)
      return op.emitOpError("argument reference has no bytecode layout");
    uint32_t flags = (storage->fourState ? 1u : 0u) |
                     ((isa<sim::StringType>(op.getValue().getType())
                           ? OBELISK_RT_ARGUMENT_VALUE_STRING
                       : sim::isManagedHandleType(op.getValue().getType())
                           ? OBELISK_RT_ARGUMENT_VALUE_CLASS
                           : OBELISK_RT_ARGUMENT_VALUE_BITS)
                      << 1);
    return emitIntrinsicRegisters(
        plan, kIntrinsicArgumentRefStore,
        {reg(plan, op.getReference()), reg(plan, op.getValue()),
         emitU64Constant(plan, storage->planeSize),
         emitU64Constant(plan, *width), emitU64Constant(plan, flags)},
        {});
  }
  if (auto op = dyn_cast<sim::SimManagedLoadOp>(operation)) {
    FailureOr<ManagedValueStorage> storage =
        getManagedValueStorage(op.getResult().getType(), dataLayout);
    if (failed(storage))
      return op.emitOpError("managed result has no bytecode field layout");
    uint32_t sizeRegister = emitU64Constant(plan, storage->planeSize);
    return emitIntrinsicRegisters(plan, kIntrinsicManagedLoad,
                                  {reg(plan, op.getReference()), sizeRegister},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimManagedStoreOp>(operation)) {
    FailureOr<ManagedValueStorage> storage =
        getManagedValueStorage(op.getValue().getType(), dataLayout);
    if (failed(storage))
      return op.emitOpError("managed value has no bytecode field layout");
    uint32_t sizeRegister = emitU64Constant(plan, storage->planeSize);
    return emitIntrinsicRegisters(
        plan, kIntrinsicManagedStore,
        {reg(plan, op.getReference()), reg(plan, op.getValue()), sizeRegister},
        {});
  }
  if (auto op = dyn_cast<sim::SimManagedNBAEnqueueOp>(operation)) {
    FailureOr<ManagedValueStorage> storage =
        getManagedValueStorage(op.getValue().getType(), dataLayout);
    if (failed(storage))
      return op.emitOpError("managed NBA value has no bytecode field layout");
    SmallVector<uint32_t> inputs{reg(plan, op.getDestination()),
                                 reg(plan, op.getValue()),
                                 emitU64Constant(plan, storage->planeSize)};
    if (op.getDelay())
      inputs.push_back(reg(plan, op.getDelay()));
    return emitIntrinsicRegisters(plan, kIntrinsicManagedNBA, inputs, {});
  }
  if (auto op = dyn_cast<sim::SimReferencePathNBAEnqueueOp>(operation)) {
    FailureOr<ManagedValueStorage> storage =
        getManagedValueStorage(op.getValue().getType(), dataLayout);
    if (failed(storage))
      return op.emitOpError(
          "reference-path NBA value has no bytecode field layout");
    SmallVector<uint32_t> inputs{reg(plan, op.getDestination()),
                                 reg(plan, op.getValue()),
                                 emitU64Constant(plan, storage->planeSize)};
    if (op.getDelay())
      inputs.push_back(reg(plan, op.getDelay()));
    return emitIntrinsicRegisters(plan, kIntrinsicManagedNBA, inputs, {});
  }
  if (auto op = dyn_cast<sim::SimClassDirectCallOp>(operation))
    return encodeClassDirectCall(plan, op);
  if (auto op = dyn_cast<sim::SimClassVirtualCallOp>(operation))
    return encodeClassVirtualCall(plan, op);
  if (auto op = dyn_cast<sim::SimWeakCreateOp>(operation)) {
    auto wrapperType = cast<sim::ClassHandleType>(op.getResult().getType());
    FailureOr<uint64_t> id = classID(wrapperType.getClassName(), operation);
    if (failed(id))
      return failure();
    return emitIntrinsicRegisters(
        plan, kIntrinsicWeakCreate,
        {reg(plan, op.getReferent()), emitU64Constant(plan, *id)},
        {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimWeakGetOp>(operation))
    return emitIntrinsic(plan, kIntrinsicWeakGet, {op.getWeak()},
                         {op.getResult()});
  if (auto op = dyn_cast<sim::SimWeakClearOp>(operation))
    return emitIntrinsic(plan, kIntrinsicWeakClear, {op.getWeak()}, {});
  if (isa<sim::SimGCSafepointOp>(operation))
    return emitIntrinsic(plan, kIntrinsicGCSafepoint, {}, {});
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
    emit({Extract, 0, reg(plan, operation->getResult(0)),
          reg(plan, operation->getOperand(0)), kInvalidRegister});
    return success();
  }
  if (auto op = dyn_cast<sim::SimLogicResizeOp>(operation)) {
    emit({Extract, static_cast<uint16_t>(op.getIsSigned()),
          reg(plan, op.getResult()), reg(plan, op.getInput()),
          kInvalidRegister});
    return success();
  }
  if (auto op = dyn_cast<sim::SimLogicIsTrueOp>(operation)) {
    emit({Reduce, 6, reg(plan, op.getResult()), reg(plan, op.getInput())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimLogicMuxOp>(operation)) {
    emit({Select, 1, reg(plan, op.getResult()), reg(plan, op.getTrueValue()),
          reg(plan, op.getFalseValue()), reg(plan, op.getCondition())});
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
      emit({Reduce, 7, reg(plan, op.getResult()), reg(plan, op.getInput())});
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
    emit({Reduce, 8, leftTruth, reg(plan, op.getLhs())});
    emit({Reduce, 8, rightTruth, reg(plan, op.getRhs())});
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
    emit({Extract, 0, reg(plan, op.getResult()), reg(plan, op.getInput()),
          kInvalidRegister, 0, 0, op.getLowBit()});
    return success();
  }
  if (auto op = dyn_cast<sim::SimLogicDynExtractOp>(operation)) {
    emit({Extract, 0, reg(plan, op.getResult()), reg(plan, op.getInput()),
          reg(plan, op.getLowBit())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimBitsDynExtractOp>(operation)) {
    emit({Extract, 0, reg(plan, op.getResult()), reg(plan, op.getInput()),
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
  if (auto op = dyn_cast<sim::SimMonitorRegisterOp>(operation))
    return emitIntrinsic(plan, kIntrinsicMonitorRegister, {op.getProcess()},
                         {});
  if (auto op = dyn_cast<sim::SimMonitorControlOp>(operation))
    return emitIntrinsic(plan, kIntrinsicMonitorControl, {}, {},
                         op.getEnabled() ? 1 : 0);
  if (auto op = dyn_cast<sim::SimMonitorCurrentOp>(operation))
    return emitIntrinsic(plan, kIntrinsicMonitorCurrent, {}, {op.getCurrent()});
  if (auto op = dyn_cast<sim::SimContextStorageOp>(operation))
    return encodeHandle(plan, op.getResult(), op.getId(), state.storage, 2);
  if (auto op = dyn_cast<sim::SimContextNetOp>(operation))
    return encodeHandle(plan, op.getResult(), op.getId(), state.nets, 3);
  if (auto op = dyn_cast<sim::SimContextDriverOp>(operation))
    return encodeHandle(plan, op.getResult(), op.getId(), state.drivers, 4);
  if (auto op = dyn_cast<sim::SimContextEventOp>(operation)) {
    emit({MakeHandle, 0, reg(plan, op.getResult()), 5, 0, 0, 0, op.getId()});
    return success();
  }
  if (auto op = dyn_cast<sim::SimRefExtractOp>(operation))
    return encodeHandleOffset(plan, op.getResult(), op.getInput(),
                              op.getLowBit(), Value{});
  if (auto op = dyn_cast<sim::SimNetExtractOp>(operation))
    return encodeHandleOffset(plan, op.getResult(), op.getInput(),
                              op.getLowBit(), Value{});
  if (auto op = dyn_cast<sim::SimDriverExtractOp>(operation))
    return encodeHandleOffset(plan, op.getResult(), op.getInput(),
                              op.getLowBit(), Value{});
  if (auto op = dyn_cast<sim::SimRefDynExtractOp>(operation))
    return encodeHandleOffset(plan, op.getResult(), op.getInput(), 0,
                              op.getLowBit());
  if (auto op = dyn_cast<sim::SimDriverDynExtractOp>(operation))
    return encodeHandleOffset(plan, op.getResult(), op.getInput(), 0,
                              op.getLowBit());
  if (auto op = dyn_cast<sim::SimRefSubelementOp>(operation))
    return encodeSubelementView(plan, op.getResult(), op.getInput(),
                                op.getIndices(), op.getOperation());
  if (auto op = dyn_cast<sim::SimDriverSubelementOp>(operation))
    return encodeSubelementView(plan, op.getResult(), op.getInput(),
                                op.getIndices(), op.getOperation());
  if (auto op = dyn_cast<sim::SimRefArrayElementOp>(operation))
    return encodeArrayView(plan, op.getResult(), op.getInput(), op.getIndex(),
                           op.getOperation());
  if (auto op = dyn_cast<sim::SimDriverArrayElementOp>(operation))
    return encodeArrayView(plan, op.getResult(), op.getInput(), op.getIndex(),
                           op.getOperation());
  if (auto op = dyn_cast<sim::SimRefLoadOp>(operation)) {
    emit({LoadState, 0, reg(plan, op.getResult()),
          reg(plan, op.getReference())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimNetReadOp>(operation)) {
    emit({LoadState, 0, reg(plan, op.getResult()), reg(plan, op.getNet())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimRefStoreOp>(operation)) {
    emit({StoreState, 0, 0, reg(plan, op.getReference()),
          reg(plan, op.getValue())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimOverrideOp>(operation)) {
    emit({OverrideState, static_cast<uint16_t>(op.getIsAssign() ? 1 : 0), 0,
          reg(plan, op.getTarget()), reg(plan, op.getValue())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimReleaseOverrideOp>(operation)) {
    emit({ReleaseState, static_cast<uint16_t>(op.getIsAssign() ? 1 : 0), 0,
          reg(plan, op.getTarget())});
    return success();
  }
  if (auto op = dyn_cast<sim::SimDriverDriveOp>(operation)) {
    emit({StoreState, 0, 0, reg(plan, op.getDriver()),
          reg(plan, op.getValue())});
    return success();
  }
  if (isa<sim::SimObserverBindOp>(operation))
    return success();
  if (auto suspend = dyn_cast<sim::SimSuspendDelayOp>(operation))
    return encodeWait(plan, suspend.getOperation(),
                      suspend.getContinuationOperands(), 1, 0, {}, {},
                      suspend.getDelay());
  if (auto suspend = dyn_cast<sim::SimSuspendChangeOp>(operation)) {
    uint32_t edge = 0;
    return encodeWait(plan, suspend.getOperation(),
                      suspend.getContinuationOperands(), 2, 0,
                      ArrayRef<uint32_t>(&edge, 1), {suspend.getWatched()});
  }
  if (auto suspend = dyn_cast<sim::SimSuspendLevelOp>(operation)) {
    uint32_t edge = 0;
    return encodeWait(plan, suspend.getOperation(),
                      suspend.getContinuationOperands(), 2,
                      OBELISK_RT_WAIT_LEVEL_TRUE, ArrayRef<uint32_t>(&edge, 1),
                      {suspend.getWatched()});
  }
  if (auto suspend = dyn_cast<sim::SimSuspendEdgeOp>(operation)) {
    uint32_t edge = static_cast<uint32_t>(suspend.getEdge());
    return encodeWait(plan, suspend.getOperation(),
                      suspend.getContinuationOperands(), 3, 0,
                      ArrayRef<uint32_t>(&edge, 1), {suspend.getWatched()});
  }
  if (auto suspend = dyn_cast<sim::SimSuspendEdgeIffOp>(operation)) {
    SmallVector<uint32_t> edges{static_cast<uint32_t>(suspend.getEdge()),
                                OBELISK_RT_WAIT_EDGE_NONE};
    SmallVector<Value> watched{suspend.getWatched(), suspend.getCondition()};
    return encodeWait(plan, suspend.getOperation(),
                      suspend.getContinuationOperands(), 3,
                      OBELISK_RT_WAIT_EDGE_IFF, edges, watched);
  }
  if (auto suspend = dyn_cast<sim::SimSuspendAnyOp>(operation)) {
    SmallVector<uint32_t> edges;
    for (int32_t edge : suspend.getEdges())
      edges.push_back(static_cast<uint32_t>(edge));
    SmallVector<Value> watched(suspend.getWatched());
    return encodeWait(plan, suspend.getOperation(),
                      suspend.getContinuationOperands(), 3, 0, edges, watched);
  }
  if (auto suspend = dyn_cast<sim::SimSuspendEventOp>(operation)) {
    uint32_t edge = UINT32_MAX;
    return encodeWait(plan, suspend.getOperation(),
                      suspend.getContinuationOperands(), 4, 0,
                      ArrayRef<uint32_t>(&edge, 1), {suspend.getEvent()});
  }
  if (auto suspend = dyn_cast<sim::SimSuspendForeverOp>(operation))
    return encodeWait(plan, suspend.getOperation(),
                      suspend.getContinuationOperands(), 7, 0, {}, {});
  if (auto suspend = dyn_cast<sim::SimSuspendAwaitOp>(operation)) {
    uint32_t edge = UINT32_MAX;
    return encodeWait(plan, suspend.getOperation(),
                      suspend.getContinuationOperands(), 5, 0,
                      ArrayRef<uint32_t>(&edge, 1), {suspend.getProcess()});
  }
  if (auto suspend = dyn_cast<sim::SimSuspendJoinOp>(operation)) {
    SmallVector<uint32_t> edges(suspend.getProcesses().size(), UINT32_MAX);
    SmallVector<Value> processes(suspend.getProcesses());
    return encodeWait(
        plan, suspend.getOperation(), suspend.getContinuationOperands(), 6,
        static_cast<uint32_t>(suspend.getKind()), edges, processes);
  }
  if (auto suspend = dyn_cast<sim::SimSuspendChildrenOp>(operation))
    return encodeWait(plan, suspend.getOperation(),
                      suspend.getContinuationOperands(), 9, 0, {}, {});
  if (auto suspend = dyn_cast<sim::SimSuspendObserveOp>(operation))
    return encodeObserverWait(plan, suspend);
  return operation->emitOpError()
         << "has no design-bytecode semantics (the normalized legality set "
            "is closed, so executable fallback is forbidden)";
}

} // namespace obelisk::bytecode
