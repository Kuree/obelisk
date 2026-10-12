//===- UnitLoweringInputs.h - Parallel lowering inputs -----------*- C++
//-*-===//

#ifndef OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_UNITLOWERINGINPUTS_H
#define OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_UNITLOWERINGINPUTS_H

#include "ContainerElementDescriptor.h"
#include "SemanticTypeIndex.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace obelisk::simlowering {

/// Module analysis computed before the nested function pipeline. Operation
/// attribute dictionaries are mutable even when their individual attributes
/// are immutable: workers must not read a sibling's symbol name, signature, or
/// callable flags while that sibling is being lowered or canonicalized.
class UnitLoweringInputs {
public:
  SemanticTypeIndex semanticTypes;
  struct Callable {
    mlir::FunctionType type;
    bool voidFunction;
  };

  struct AggregateMember {
    uint64_t bitOffset;
    const ContainerElementDescriptor *element;
  };
  struct AggregateMembers {
    ContainerElementDescriptor owner;
    llvm::SmallVector<AggregateMember> members;
  };
  const AggregateMembers *getAggregateMembers(mlir::Type type) const;

  explicit UnitLoweringInputs(mlir::Operation *module);

  const Callable *getCallable(sim::SimFuncOp function) const;
  /// Semantic handle paths describe elaborated types and may omit instance
  /// wrappers in the symbol tree. Resolve the complete type identity directly.
  mlir::Operation *getCovergroup(mlir::Type handle) const;
  /// A leaf-name fallback is valid only when that name is unambiguous.
  mlir::Operation *getCovergroup(llvm::StringRef symbolName) const;
  /// Direct children in deterministic semantic declaration order.
  llvm::ArrayRef<mlir::Operation *>
  getDerivedCovergroups(mlir::Type baseHandle) const;
  std::optional<uint64_t>
  getCoverageInstanceID(sim::SimDesignOp design,
                        llvm::StringRef hierarchy) const;

  template <typename T>
  T lookupNearestSymbolFrom(mlir::Operation *from,
                            mlir::SymbolRefAttr symbol) const {
    return lockedSymbols.lookupNearestSymbolFrom<T>(from, symbol);
  }

private:
  // Call signatures and coverage indexes are frozen once before workers run.
  // Other symbol tables belong to immutable semantic declarations; MLIR's
  // locked collection synchronizes their lazy construction, without rescanning
  // the mutable design block on every lookup.
  llvm::DenseMap<mlir::Type, std::unique_ptr<ContainerElementDescriptor>>
      memberElements;
  llvm::DenseMap<mlir::Type, AggregateMembers> aggregateMembers;
  mutable mlir::SymbolTableCollection symbols;
  mutable mlir::LockedSymbolTableCollection lockedSymbols;
  llvm::DenseMap<mlir::Operation *, Callable> callables;
  llvm::DenseMap<mlir::Type, mlir::Operation *> covergroups;
  llvm::StringMap<mlir::Operation *> covergroupsByName;
  llvm::DenseMap<mlir::Type, llvm::SmallVector<mlir::Operation *>>
      derivedCovergroups;
  llvm::DenseMap<mlir::Operation *, llvm::StringMap<uint64_t>>
      coverageInstances;
};

} // namespace obelisk::simlowering

#endif
