//===- BytecodeCallEncoding.cpp - Bytecode call instruction selection ----===//

#include "BytecodeEncoder.h"
#include "BytecodeSerialization.h"

using namespace mlir;

namespace obelisk::bytecode {
namespace {

uint32_t stableImportID(StringRef text) {
  uint64_t hash = stableHash(text);
  uint32_t result = static_cast<uint32_t>(hash ^ (hash >> 32));
  return result == 0 ? 1 : result;
}

} // namespace

LogicalResult Encoder::encodeCall(FunctionPlan &plan, sim::SimCallOp call) {
  auto found = indices.find(call.getCallee());
  if (found == indices.end()) {
    sim::SimFuncOp declaration = externalFunctions.lookup(call.getCallee());
    if (!declaration)
      return call.emitOpError(
          "callee has no bytecode body or import declaration");
    uint32_t importID = stableImportID(declaration.getSymName());
    auto inserted =
        importSymbols.try_emplace(importID, declaration.getSymName().str());
    if (!inserted.second && inserted.first->second != declaration.getSymName())
      return call.emitOpError()
             << "import ID collision between '" << inserted.first->second
             << "' and '" << declaration.getSymName() << "'";
    SmallVector<uint32_t> inputs;
    for (Value operand : call.getOperands()) {
      if (isa<sim::ContextType, runtime::ContextType>(operand.getType()))
        continue;
      uint32_t input = reg(plan, operand);
      if (input == kInvalidRegister || (plan.layouts[input].kind != Bits &&
                                        plan.layouts[input].kind != Logic &&
                                        plan.layouts[input].kind != Handle &&
                                        plan.layouts[input].kind != Status))
        return call.emitOpError() << "generation-one imports require "
                                     "numeric, handle, or status inputs";
      inputs.push_back(input);
    }
    SmallVector<uint32_t> outputs;
    for (Value result : call.getResults()) {
      uint32_t output = reg(plan, result);
      if (output == kInvalidRegister || (plan.layouts[output].kind != Bits &&
                                         plan.layouts[output].kind != Logic &&
                                         plan.layouts[output].kind != Handle &&
                                         plan.layouts[output].kind != Status))
        return call.emitOpError() << "generation-one imports require "
                                     "numeric, handle, or status results";
      outputs.push_back(output);
    }
    return emitIntrinsicRegisters(plan, kIntrinsicImport, inputs, outputs,
                                  importID);
  }
  FunctionPlan &callee = plans[found->second];
  Block &calleeEntry = callee.function.getBody().front();
  auto inputs =
      addMap(callee, calleeEntry.getArguments(), plan, call.getOperands());
  SmallVector<Value> synthetic;
  uint64_t firstOutputs = operandMaps.size();
  for (auto [destination, source] :
       llvm::zip_equal(call.getResults(), callee.resultRegisters))
    operandMaps.push_back({reg(plan, destination), source});
  emit({Call, 0, 0, callee.index, static_cast<uint32_t>(inputs.first),
        static_cast<uint32_t>(inputs.second),
        static_cast<uint32_t>(firstOutputs), call.getNumResults()});
  return success();
}

LogicalResult Encoder::encodeTaskCall(FunctionPlan &plan,
                                      sim::SimTaskCallOp call) {
  if (!plan.frame)
    return call.emitOpError("task call has no canonical caller frame");
  const ProcessSuspension *suspension =
      plan.frame->getSuspension(call.getOperation());
  if (!suspension)
    return call.emitOpError("task call is missing frame analysis");
  ArrayRef<ProcessFrameValue> slots =
      plan.frame->getContinuationLayout(suspension->continuationID);
  if (slots.size() != call.getContinuationOperands().size())
    return call.emitOpError("task continuation frame arity mismatch");
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
  auto found = indices.find(call.getCallee());
  if (found == indices.end())
    return call.emitOpError("task target has no bytecode body");
  FunctionPlan &callee = plans[found->second];
  if (!callee.frame || callee.function.getEntryKind() != sim::EntryKind::Task)
    return call.emitOpError("task target is not an activation entry");
  Block &calleeEntry = callee.function.getBody().front();
  auto inputs =
      addMap(callee, calleeEntry.getArguments(), plan, call.getArguments());
  emit({TaskCall, 0, 0, callee.index, static_cast<uint32_t>(inputs.first),
        static_cast<uint32_t>(inputs.second), 0, suspension->continuationID});
  return success();
}

LogicalResult Encoder::encodeDPICall(FunctionPlan &plan,
                                     sim::SimDPICallOp call) {
  uint32_t importID = call.getImportId();
  auto inserted =
      importSymbols.try_emplace(importID, call.getCIdentifier().str());
  if (!inserted.second && inserted.first->second != call.getCIdentifier())
    return call.emitOpError()
           << "import ID collision between '" << inserted.first->second
           << "' and '" << call.getCIdentifier() << "'";
  SmallVector<uint8_t> metadata;
  append32(metadata, OBELISK_RT_VERSION);
  uint32_t flags = (call.getIsPure() ? OBELISK_RT_IMPORT_PURE : 0) |
                   (call.getIsContext() ? OBELISK_RT_IMPORT_CONTEXT : 0) |
                   (call.getIsTask() ? OBELISK_RT_IMPORT_TASK : 0);
  append32(metadata, flags);
  append32(metadata, importID);
  append32(metadata, 0);
  append64(metadata, call.getScopeId());
  append32(metadata, call.getSourceLine());
  append32(metadata, call.getSourceColumn());
  append64(metadata, call.getSourceFile().size());
  uint64_t logicalInputs = call.getArguments().size();
  if (logicalInputs > call.getAbiSignature().size())
    return call.emitOpError("DPI ABI signature has too few inputs");
  uint64_t logicalOutputs = call.getAbiSignature().size() - logicalInputs;
  append64(metadata,
           sim::getDPISignatureHash(call.getAbiSignature(), logicalInputs));
  append32(metadata, static_cast<uint32_t>(logicalInputs));
  append32(metadata, static_cast<uint32_t>(logicalOutputs));
  for (Attribute attribute : call.getAbiSignature()) {
    auto abi = cast<sim::DPIABIAttr>(attribute);
    append32(metadata, static_cast<uint32_t>(abi.getKind()));
    append32(metadata, static_cast<uint32_t>(abi.getDirection()));
    append32(metadata, abi.getWidth());
    append32(metadata,
             (abi.getFourState() ? 1u : 0u) | (abi.getIsSigned() ? 2u : 0u));
  }
  llvm::append_range(metadata,
                     ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(
                                           call.getSourceFile().data()),
                                       call.getSourceFile().size()));
  SmallVector<uint32_t> inputs{emitBytesConstant(plan, metadata)};
  for (Value operand : call.getArguments()) {
    uint32_t input = reg(plan, operand);
    if (input == kInvalidRegister || (plan.layouts[input].kind != Bits &&
                                      plan.layouts[input].kind != Logic &&
                                      plan.layouts[input].kind != Status))
      return call.emitOpError(
          "DPI imports require fixed packed integral inputs");
    inputs.push_back(input);
  }
  SmallVector<uint32_t> outputs;
  for (Value result : call.getResults()) {
    uint32_t output = reg(plan, result);
    if (output == kInvalidRegister || (plan.layouts[output].kind != Bits &&
                                       plan.layouts[output].kind != Logic &&
                                       plan.layouts[output].kind != Status))
      return call.emitOpError(
          "DPI imports require fixed packed integral results");
    outputs.push_back(output);
  }
  if (outputs.size() != logicalOutputs + 1 ||
      plan.layouts[outputs.back()].kind != Status)
    return call.emitOpError(
        "DPI import must return data results followed by runtime status");
  return emitIntrinsicRegisters(plan, kIntrinsicDPIImport, inputs, outputs);
}

LogicalResult Encoder::encodeReturn(FunctionPlan &plan, sim::SimReturnOp op) {
  if (plan.function.getEntryKind() != sim::EntryKind::Function &&
      plan.function.getEntryKind() != sim::EntryKind::Observer) {
    if (!op.getOperands().empty())
      return op.emitOpError("process return cannot carry values");
    emit({Terminate});
    return success();
  }
  auto results = addRegistersMap(plan.resultRegisters, plan, op.getOperands());
  emit({Return, 0, 0, static_cast<uint32_t>(results.first),
        static_cast<uint32_t>(results.second)});
  return success();
}

} // namespace obelisk::bytecode
