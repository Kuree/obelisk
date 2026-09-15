//===- NativePartitionCost.h - Definition-local partition weights -*- C++ -*-===//

#ifndef OBELISK_TOOLS_DRIVER_NATIVEPARTITIONCOST_H
#define OBELISK_TOOLS_DRIVER_NATIVEPARTITIONCOST_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"

#include <algorithm>
#include <cstdint>

namespace obelisk::driver::detail {

inline uint64_t estimateNativeGlobalWeight(const llvm::GlobalVariable &global) {
  if (!global.hasInitializer())
    return 1;
  uint64_t weight = 1;
  llvm::SmallVector<const llvm::Constant *> worklist{global.getInitializer()};
  llvm::SmallPtrSet<const llvm::Constant *, 32> visited;
  while (!worklist.empty()) {
    const llvm::Constant *constant = worklist.pop_back_val();
    if (!visited.insert(constant).second)
      continue;
    // Referenced definitions receive their own SplitUnit. Charge the address
    // here, not the referenced initializer graph: following GlobalValue
    // operands both double-counts shared state and repeatedly walks cycles in
    // reflection/dispatch tables for every incoming reference.
    if (llvm::isa<llvm::GlobalValue>(constant)) {
      ++weight;
      continue;
    }
    if (auto *data = llvm::dyn_cast<llvm::ConstantDataSequential>(constant))
      weight += std::max<uint64_t>(1, (data->getNumElements() + 31) / 32);
    else
      weight += std::max<unsigned>(1, constant->getNumOperands());
    for (const llvm::Use &operand : constant->operands())
      if (auto *child = llvm::dyn_cast<llvm::Constant>(operand.get()))
        worklist.push_back(child);
  }
  return weight;
}

} // namespace obelisk::driver::detail

#endif
