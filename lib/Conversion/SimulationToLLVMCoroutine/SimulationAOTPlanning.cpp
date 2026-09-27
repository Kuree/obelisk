//===- SimulationAOTPlanning.cpp - Native AOT plan derivation -----------===//

#include "SimulationAOTPlanning.h"
#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Analysis/SimulationScheduleAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"

using namespace mlir;

namespace obelisk::detail {

LogicalResult materializeNativePeriodicClockPlan(
    ModuleOp module, ArrayRef<NativePeriodicClock> periodicClocks) {
  if (periodicClocks.empty())
    return success();
  MLIRContext *context = module.getContext();
  OpBuilder builder(context);
  Location location = module.getLoc();
  Type i32 = builder.getI32Type();
  Type i64 = builder.getI64Type();
  Type pointer = LLVM::LLVMPointerType::get(context);
  Type entryType = LLVM::LLVMStructType::getLiteral(
      context, {i32, i32, i32, i32, i64, i64, pointer, i64});
  Type tableType = LLVM::LLVMArrayType::get(entryType, periodicClocks.size());
  if (module.lookupSymbol("__obelisk_periodic_clock_plan_v1"))
    return module.emitError("duplicate generated periodic-clock plan");
  SmallVector<std::string> coverageSymbols(periodicClocks.size());
  for (auto [index, clock] : llvm::enumerate(periodicClocks)) {
    if (clock.getCoveragePoints().empty())
      continue;
    coverageSymbols[index] =
        (Twine("__obelisk_periodic_clock_coverage_") + Twine(index)).str();
    Type pointsType =
        LLVM::LLVMArrayType::get(i64, clock.getCoveragePoints().size());
    makeConstantGlobal(
        module, location, pointsType, coverageSymbols[index],
        LLVM::Linkage::Internal, 8, [&](OpBuilder &initializer) {
          Value points =
              LLVM::ZeroOp::create(initializer, location, pointsType);
          for (auto [pointIndex, point] :
               llvm::enumerate(clock.getCoveragePoints()))
            points = insertValue(
                initializer, location, points,
                llvmConstant(initializer, location, i64, point), pointIndex);
          return points;
        });
  }
  makeConstantGlobal(
      module, location, tableType, "__obelisk_periodic_clock_plan_v1",
      LLVM::Linkage::Internal, 8, [&](OpBuilder &initializer) {
        Value table = LLVM::ZeroOp::create(initializer, location, tableType);
        for (auto [index, clock] : llvm::enumerate(periodicClocks)) {
          Value entry = LLVM::ZeroOp::create(initializer, location, entryType);
          entry = insertValue(
              initializer, location, entry,
              llvmConstant(initializer, location, i32, clock.getActorSlot()),
              0);
          entry = insertValue(
              initializer, location, entry,
              llvmConstant(initializer, location, i32, clock.getContinuation()),
              1);
          entry = insertValue(
              initializer, location, entry,
              llvmConstant(initializer, location, i32, clock.getStaticState()),
              2);
          entry = insertValue(
              initializer, location, entry,
              llvmConstant(initializer, location, i64, clock.getBitOffset()),
              4);
          entry = insertValue(
              initializer, location, entry,
              llvmConstant(initializer, location, i64, clock.getHalfPeriod()),
              5);
          if (!coverageSymbols[index].empty()) {
            entry = insertValue(
                initializer, location, entry,
                LLVM::AddressOfOp::create(initializer, location, pointer,
                                          coverageSymbols[index]),
                6);
            entry = insertValue(initializer, location, entry,
                                llvmConstant(initializer, location, i64,
                                             clock.getCoveragePoints().size()),
                                7);
          }
          table = LLVM::InsertValueOp::create(
              initializer, location, table, entry,
              ArrayRef<int64_t>{static_cast<int64_t>(index)});
        }
        return table;
      });
  return success();
}

} // namespace obelisk::detail
