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
                      static_cast<uint32_t>(transferSize),
                      slot.isFourState() ? slot.unknownOffset : UINT64_MAX);
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
  if (ModuleOp module = design->getParentOfType<ModuleOp>())
    module->setAttr("obelisk.feature.dpi_import_bytecode",
                    UnitAttr::get(call.getContext()));
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
  bool openSignature = llvm::any_of(call.getAbiSignature(), [](Attribute attr) {
    return cast<sim::DPIABIAttr>(attr).getKind() == sim::DPIABIKind::OpenArray;
  });
  append64(metadata, openSignature
                         ? 0
                         : sim::getDPISignatureHash(call.getAbiSignature(),
                                                    logicalInputs));
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
  ArrayAttr openLayouts =
      call->getAttrOfType<ArrayAttr>("obelisk.dpi.open_array_layouts");
  if (!openLayouts) {
    SmallVector<Attribute> empty(call.getAbiSignature().size(),
                                 UnitAttr::get(call.getContext()));
    openLayouts = ArrayAttr::get(call.getContext(), empty);
  }
  if (openLayouts.size() != call.getAbiSignature().size())
    return call.emitOpError("has no complete DPI open-array inventory");
  for (auto [attribute, layoutAttribute] :
       llvm::zip_equal(call.getAbiSignature(), openLayouts)) {
    auto abi = cast<sim::DPIABIAttr>(attribute);
    if (abi.getKind() != sim::DPIABIKind::OpenArray) {
      append32(metadata, 0);
      continue;
    }
    auto layout = dyn_cast<sim::DPIOpenArrayABIAttr>(layoutAttribute);
    if (!layout)
      return call.emitOpError("has malformed open-array metadata");
    uint64_t dimensions = layout.getRanges().size() / 2;
    uint64_t planWords = layout.getElementLeaves().size();
    uint64_t shapeWords = layout.getShapePlan().size();
    if (planWords > (UINT32_MAX - 80) / 8 ||
        dimensions > (UINT32_MAX - 80 - planWords * 8) / 32 ||
        shapeWords >
            (UINT32_MAX - 80 - planWords * 8 - dimensions * 32) / 8)
      return call.emitOpError("open-array metadata is too large");
    uint64_t recordSize =
        80 + planWords * 8 + dimensions * 32 + shapeWords * 8;
    if (recordSize > UINT32_MAX)
      return call.emitOpError("open-array metadata is too large");
    append32(metadata, static_cast<uint32_t>(recordSize));
    append32(metadata, layout.getStorage());
    append32(metadata, static_cast<uint32_t>(layout.getElementKind()));
    append32(metadata, layout.getElementWidth());
    append32(metadata, layout.getFourState() ? 1 : 0);
    append64(metadata, layout.getTransportWidth());
    append64(metadata, static_cast<uint64_t>(layout.getPackedLeft()));
    append64(metadata, static_cast<uint64_t>(layout.getPackedRight()));
    append32(metadata, static_cast<uint32_t>(dimensions));
    append32(metadata, layout.getElementCAlignment());
    append32(metadata, layout.getTransportFourState() ? 1 : 0);
    append64(metadata, layout.getElementCSize());
    append64(metadata, layout.getElementStringCount());
    append64(metadata, planWords);
    for (int64_t word : layout.getElementLeaves().asArrayRef())
      append64(metadata, static_cast<uint64_t>(word));
    for (int64_t bound : layout.getRanges().asArrayRef())
      append64(metadata, static_cast<uint64_t>(bound));
    for (int64_t bound : layout.getSourceRanges().asArrayRef())
      append64(metadata, static_cast<uint64_t>(bound));
    for (int64_t word : layout.getShapePlan().asArrayRef())
      append64(metadata, static_cast<uint64_t>(word));
  }
  ArrayAttr aggregateLayouts =
      call->getAttrOfType<ArrayAttr>("obelisk.dpi.aggregate_layouts");
  if (!aggregateLayouts) {
    SmallVector<Attribute> empty(call.getAbiSignature().size(),
                                 UnitAttr::get(call.getContext()));
    aggregateLayouts = ArrayAttr::get(call.getContext(), empty);
  }
  if (aggregateLayouts.size() != call.getAbiSignature().size())
    return call.emitOpError("has no complete DPI aggregate inventory");
  for (auto [attribute, layoutAttribute] :
       llvm::zip_equal(call.getAbiSignature(), aggregateLayouts)) {
    auto abi = cast<sim::DPIABIAttr>(attribute);
    if (abi.getKind() != sim::DPIABIKind::UnpackedAggregate) {
      append32(metadata, 0);
      continue;
    }
    auto layout = dyn_cast<sim::DPIAggregateABIAttr>(layoutAttribute);
    if (!layout)
      return call.emitOpError("has malformed aggregate metadata");
    uint64_t planWords = layout.getLeaves().size();
    if (planWords > (UINT32_MAX - 32) / 8)
      return call.emitOpError("aggregate metadata is too large");
    append32(metadata, static_cast<uint32_t>(32 + planWords * 8));
    append32(metadata, layout.getCAlignment());
    append64(metadata, layout.getCSize());
    append64(metadata, layout.getStringCount());
    append64(metadata, planWords);
    for (int64_t word : layout.getLeaves().asArrayRef())
      append64(metadata, static_cast<uint64_t>(word));
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
                                      plan.layouts[input].kind != String &&
                                      plan.layouts[input].kind != Real32 &&
                                      plan.layouts[input].kind != Real64 &&
                                      plan.layouts[input].kind != Status &&
                                      plan.layouts[input].kind != Managed))
      return call.emitOpError(
          "DPI imports require supported scalar or packed inputs");
    inputs.push_back(input);
  }
  SmallVector<uint32_t> outputs;
  for (Value result : call.getResults()) {
    uint32_t output = reg(plan, result);
    if (output == kInvalidRegister || (plan.layouts[output].kind != Bits &&
                                       plan.layouts[output].kind != Logic &&
                                       plan.layouts[output].kind != String &&
                                       plan.layouts[output].kind != Real32 &&
                                       plan.layouts[output].kind != Real64 &&
                                       plan.layouts[output].kind != Status &&
                                       plan.layouts[output].kind != Managed))
      return call.emitOpError(
          "DPI imports require supported scalar or packed results");
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
