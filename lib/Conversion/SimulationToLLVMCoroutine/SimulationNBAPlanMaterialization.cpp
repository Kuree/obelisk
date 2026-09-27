//===- SimulationNBAPlanning.cpp - Native static NBA plan support -------===//

#include "SimulationNBALowering.h"
#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Conversion/SimulationRuntime.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/LoopLikeInterface.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Twine.h"

#include <limits>

using namespace mlir;

namespace obelisk::detail {
LogicalResult
materializeGeneratedNBAAccumulators(ModuleOp module,
                                    const NativeStaticNBAPlan &plan) {
  if (plan.generatedAccumulators.size() != plan.roots.size() ||
      plan.generatedOffsets.size() != plan.roots.size() ||
      plan.generatedCommitRegions.size() != plan.roots.size() ||
      plan.generatedFullRootStages.size() != plan.roots.size() ||
      plan.generatedFixedWriteMasks.size() != plan.roots.size())
    return module.emitError("generated NBA accumulator plan is malformed");
  OpBuilder builder(module.getContext());
  Location location = module.getLoc();
  Type storageType = LLVM::LLVMArrayType::get(
      builder.getI8Type(), sizeof(obelisk_rt_generated_nba_accumulator_256));
  // Packed NBA staging references these arrays before coordinator emission.
  // Their sizes are already fixed by the native NBA plan.
  uint64_t words = (plan.roots.size() + 63) / 64;
  for (auto [name, count] :
       {std::pair<StringRef, uint64_t>{"__obelisk_aot_nba_dirty_roots_v1",
                                       words},
        {"__obelisk_aot_nba_dirty_summary_v1", (words + 63) / 64}}) {
    if (!count)
      continue;
    builder.setInsertionPointToStart(module.getBody());
    LLVM::GlobalOp::create(
        builder, location,
        LLVM::LLVMArrayType::get(builder.getI64Type(), count), false,
        LLVM::Linkage::External, name, Attribute{}, 8);
  }
  for (StringRef name : plan.generatedAccumulators) {
    if (name.empty())
      continue;
    if (module.lookupSymbol<LLVM::GlobalOp>(name))
      return module.emitError("generated NBA accumulator symbol is duplicated");
    builder.setInsertionPointToStart(module.getBody());
    auto global =
        LLVM::GlobalOp::create(builder, location, storageType, false,
                               LLVM::Linkage::Internal, name, Attribute{}, 32);
    Block *initializer = new Block;
    global.getInitializerRegion().push_back(initializer);
    builder.setInsertionPointToStart(initializer);
    LLVM::ReturnOp::create(
        builder, location,
        LLVM::ZeroOp::create(builder, location, storageType));
  }
  return success();
}

} // namespace obelisk::detail
