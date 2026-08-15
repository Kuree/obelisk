//===- BytecodeClassEncoding.cpp - Managed class bytecode encoding -------===//

#include "BytecodeEncoder.h"
#include "obelisk/Analysis/ManagedClassLayoutAnalysis.h"

#include "llvm/ADT/STLExtras.h"

using namespace mlir;

namespace obelisk::bytecode {

LogicalResult Encoder::prepareClassLayouts() {
  FailureOr<analysis::ManagedClassLayoutAnalysis> layouts =
      analysis::ManagedClassLayoutAnalysis::compute(design, dataLayout);
  if (failed(layouts) ||
      failed(analysis::materializeManagedClassFieldOffsets(*layouts)))
    return failure();
  for (const analysis::ManagedClassLayoutAnalysis::Class &layout :
       layouts->classes) {
    sim::SimClassDeclOp declaration = layout.declaration;
    classIDs[declaration.getSymName()] = declaration.getId();
    for (const analysis::ManagedClassLayoutAnalysis::Field &field :
         layout.fields) {
      sim::SimClassFieldDeclOp fieldDeclaration = field.declaration;
      classFieldOffsets[fieldDeclaration.getSymName()] = field.offset;
    }
  }
  for (sim::SimClassMethodDeclOp method :
       design.getBody().front().getOps<sim::SimClassMethodDeclOp>()) {
    auto owner = classIDs.find(method.getOwner());
    sim::SimClassDeclOp ownerDeclaration =
        SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
            method, method.getOwnerAttr());
    if (owner == classIDs.end() || !ownerDeclaration)
      return method.emitOpError("managed method owner is missing");
    classMethodDispatch[method.getSymName()] = {
        owner->second, method.getInterfaceOrdinal().value_or(UINT64_MAX),
        ownerDeclaration.getIsInterface()};
  }
  return success();
}

FailureOr<uint64_t> Encoder::classID(SymbolRefAttr symbol,
                                     Operation *anchor) const {
  auto found = classIDs.find(symbol.getRootReference().getValue());
  if (found == classIDs.end()) {
    anchor->emitOpError("references an unknown managed class");
    return failure();
  }
  return found->second;
}

std::optional<LogicalResult>
Encoder::encodeClassOperation(FunctionPlan &plan, Operation *operation) {
  if (isa<sim::SimClassNullOp>(operation)) {
    uint32_t destination = reg(plan, operation->getResult(0));
    emit({Constant, 0, destination, 0, 0, 0, 0,
          addZeroConstant(plan.layouts[destination])});
    return success();
  }
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
    auto offset = classFieldOffsets.find(op.getField());
    if (offset == classFieldOffsets.end())
      return op.emitOpError("managed field has no bytecode layout");
    uint32_t offsetRegister = emitU64Constant(plan, offset->second);
    return emitIntrinsicRegisters(plan, kIntrinsicClassFieldRef,
                                  {reg(plan, op.getObject()), offsetRegister},
                                  {reg(plan, op.getResult())});
  }
  if (auto op = dyn_cast<sim::SimClassDirectCallOp>(operation))
    return encodeClassDirectCall(plan, op);
  if (auto op = dyn_cast<sim::SimClassVirtualCallOp>(operation))
    return encodeClassVirtualCall(plan, op);
  if (auto op = dyn_cast<sim::SimClassVirtualTaskCallOp>(operation))
    return encodeClassVirtualTaskCall(plan, op);
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
  return std::nullopt;
}

LogicalResult Encoder::encodeClassDirectCall(FunctionPlan &plan,
                                             sim::SimClassDirectCallOp call) {
  auto found = indices.find(call.getCallee());
  if (found == indices.end())
    return call.emitOpError("class method has no bytecode body");
  FunctionPlan &callee = plans[found->second];
  SmallVector<Value> arguments{plan.function.getBody().front().getArgument(0),
                               call.getReceiver()};
  llvm::append_range(arguments, call.getArguments());
  auto inputs = addMap(callee, callee.function.getBody().front().getArguments(),
                       plan, arguments);
  uint64_t firstOutputs = operandMaps.size();
  for (auto [destination, source] :
       llvm::zip_equal(call.getResults(), callee.resultRegisters))
    operandMaps.push_back({reg(plan, destination), source});
  emit({Call, 0, 0, callee.index, static_cast<uint32_t>(inputs.first),
        static_cast<uint32_t>(inputs.second),
        static_cast<uint32_t>(firstOutputs), call.getNumResults()});
  return success();
}

LogicalResult Encoder::encodeClassVirtualCall(FunctionPlan &plan,
                                              sim::SimClassVirtualCallOp call) {
  if (call.getSlot() > UINT32_MAX || call.getNumResults() > UINT16_MAX)
    return call.emitOpError("virtual call exceeds the bytecode ABI");
  Opcode opcode = VirtualCall;
  uint32_t dispatch = static_cast<uint32_t>(call.getSlot());
  auto method = classMethodDispatch.find(call.getMethod());
  if (method == classMethodDispatch.end())
    return call.emitOpError("virtual method descriptor is missing");
  if (method->second.isInterface) {
    if (method->second.interfaceOrdinal == UINT64_MAX)
      return call.emitOpError("interface method has no dispatch ordinal");
    if (method->second.ownerID > UINT32_MAX ||
        method->second.interfaceOrdinal > UINT32_MAX ||
        operandMaps.size() >= UINT32_MAX)
      return call.emitOpError("interface dispatch exceeds the bytecode ABI");
    opcode = InterfaceCall;
    dispatch = static_cast<uint32_t>(operandMaps.size());
    operandMaps.push_back(
        {static_cast<uint32_t>(method->second.ownerID),
         static_cast<uint32_t>(method->second.interfaceOrdinal)});
  }
  SmallVector<Value> arguments{plan.function.getBody().front().getArgument(0),
                               call.getReceiver()};
  llvm::append_range(arguments, call.getArguments());
  if (arguments.size() > UINT32_MAX || operandMaps.size() > UINT32_MAX)
    return call.emitOpError("virtual argument map exceeds the bytecode ABI");
  uint32_t firstInputs = operandMaps.size();
  for (auto [index, argument] : llvm::enumerate(arguments))
    operandMaps.push_back({static_cast<uint32_t>(index), reg(plan, argument)});
  uint32_t firstOutputs = operandMaps.size();
  for (auto [index, result] : llvm::enumerate(call.getResults()))
    operandMaps.push_back(
        {reg(plan, result), static_cast<uint32_t>(arguments.size() + index)});
  emit({opcode, static_cast<uint16_t>(call.getNumResults()), dispatch,
        reg(plan, call.getReceiver()), firstInputs,
        static_cast<uint32_t>(arguments.size()), firstOutputs,
        call.getSignatureId()});
  return success();
}

LogicalResult
Encoder::encodeClassVirtualTaskCall(FunctionPlan &plan,
                                    sim::SimClassVirtualTaskCallOp call) {
  if (!plan.frame)
    return call.emitOpError("virtual task call has no canonical caller frame");
  const ProcessSuspension *suspension =
      plan.frame->getSuspension(call.getOperation());
  if (!suspension)
    return call.emitOpError("virtual task call is missing frame analysis");
  ArrayRef<ProcessFrameValue> slots =
      plan.frame->getContinuationLayout(suspension->continuationID);
  if (slots.size() != call.getContinuationOperands().size())
    return call.emitOpError("virtual task continuation frame arity mismatch");
  for (auto [value, slot] :
       llvm::zip_equal(call.getContinuationOperands(), slots)) {
    if (slot.storageSize > UINT32_MAX ||
        (slot.hasSecondaryStorage() && slot.storageSize > UINT32_MAX / 2))
      return call.emitOpError(
          "canonical frame transfer exceeds the bytecode ABI limit");
    uint64_t transferSize =
        slot.storageSize * (slot.hasSecondaryStorage() ? 2 : 1);
    emitFrameTransfer(plan, StoreFrame, value, slot.valueOffset,
                      static_cast<uint32_t>(transferSize));
  }

  if (call.getSlot() > UINT32_MAX || call.getArguments().size() > UINT32_MAX ||
      operandMaps.size() > UINT32_MAX)
    return call.emitOpError("virtual task call exceeds the bytecode ABI");
  Opcode opcode = VirtualTaskCall;
  uint32_t dispatch = static_cast<uint32_t>(call.getSlot());
  auto method = classMethodDispatch.find(call.getMethod());
  if (method == classMethodDispatch.end())
    return call.emitOpError("virtual task descriptor is missing");
  if (method->second.isInterface) {
    if (method->second.interfaceOrdinal == UINT64_MAX)
      return call.emitOpError("interface task has no dispatch ordinal");
    if (method->second.ownerID > UINT32_MAX ||
        method->second.interfaceOrdinal > UINT32_MAX ||
        operandMaps.size() >= UINT32_MAX)
      return call.emitOpError("interface dispatch exceeds the bytecode ABI");
    opcode = InterfaceTaskCall;
    dispatch = static_cast<uint32_t>(operandMaps.size());
    operandMaps.push_back(
        {static_cast<uint32_t>(method->second.ownerID),
         static_cast<uint32_t>(method->second.interfaceOrdinal)});
  }
  SmallVector<Value> arguments{plan.function.getBody().front().getArgument(0),
                               call.getReceiver()};
  llvm::append_range(arguments, call.getArguments());
  if (arguments.size() > UINT32_MAX)
    return call.emitOpError(
        "virtual task argument map exceeds the bytecode ABI");
  uint32_t firstInputs = operandMaps.size();
  for (auto [index, argument] : llvm::enumerate(arguments))
    operandMaps.push_back({static_cast<uint32_t>(index), reg(plan, argument)});
  emit({opcode, 0, dispatch, reg(plan, call.getReceiver()), firstInputs,
        static_cast<uint32_t>(arguments.size()), suspension->continuationID,
        call.getSignatureId()});
  return success();
}

} // namespace obelisk::bytecode
