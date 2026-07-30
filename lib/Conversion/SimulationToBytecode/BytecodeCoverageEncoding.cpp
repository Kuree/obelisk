//===- BytecodeCoverageEncoding.cpp - Coverage instruction selection -----===//

#include "BytecodeEncoder.h"

using namespace mlir;

namespace obelisk::bytecode {

std::optional<LogicalResult>
Encoder::encodeCoverageOperation(FunctionPlan &plan, Operation *operation) {
  if (isa<sim::SimCovergroupNullOp>(operation)) {
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
    SmallVector<uint32_t> inputs{emitU64Constant(plan, declaration.getId())};
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
  return std::nullopt;
}

} // namespace obelisk::bytecode
