//===- SimulationNBALowering.h - Native NBA lowering support ----*- C++ -*-===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_NBA_LOWERING_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_NBA_LOWERING_H

#include "obelisk/Runtime/Runtime.h"

#include "mlir/IR/PatternMatch.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

#include <cstdint>
#include <string>

namespace mlir {
class TypeConverter;
}

namespace obelisk::detail {

struct NativeStaticNBAPlan {
  llvm::SmallVector<obelisk_rt_static_nba_root> roots;
  llvm::SmallVector<obelisk_rt_static_nba_site> sites;
  llvm::DenseMap<uint64_t, uint32_t> siteRoots;
  llvm::SmallVector<std::string> generatedAccumulators;
};

void populateNBAToLLVMConversionPatterns(mlir::RewritePatternSet &patterns,
                                         mlir::TypeConverter &converter,
                                         uint64_t stateBitCount,
                                         const NativeStaticNBAPlan *staticPlan,
                                         bool staticSitesEnabled,
                                         bool guardedClaims);

} // namespace obelisk::detail

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_NBA_LOWERING_H
