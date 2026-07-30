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
  emit({VirtualCall, static_cast<uint16_t>(call.getNumResults()),
        static_cast<uint32_t>(call.getSlot()), reg(plan, call.getReceiver()),
        firstInputs, static_cast<uint32_t>(arguments.size()), firstOutputs,
        call.getSignatureId()});
  return success();
}

} // namespace obelisk::bytecode
