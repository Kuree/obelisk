//===- UnitLoweringInputs.h - Parallel lowering inputs -----------*- C++
//-*-===//

#ifndef OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_UNITLOWERINGINPUTS_H
#define OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_UNITLOWERINGINPUTS_H

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/DenseMap.h"

namespace obelisk::simlowering {

/// Module analysis computed before the nested function pipeline. Operation
/// attribute dictionaries are mutable even when their individual attributes
/// are immutable: workers must not read a sibling's symbol name, signature, or
/// callable flags while that sibling is being lowered or canonicalized.
class UnitLoweringInputs {
public:
  struct Callable {
    mlir::FunctionType type;
    bool voidFunction;
  };

  explicit UnitLoweringInputs(mlir::Operation *module);

  const Callable *getCallable(sim::SimFuncOp function) const;

  template <typename T>
  T lookupNearestSymbolFrom(mlir::Operation *from,
                            mlir::SymbolRefAttr symbol) const {
    return lockedSymbols.lookupNearestSymbolFrom<T>(from, symbol);
  }

private:
  // Tables containing mutable simulation functions are populated eagerly.
  // Other tables belong to immutable semantic declarations; MLIR's locked
  // collection synchronizes their lazy construction, without rescanning the
  // mutable design block on every lookup.
  mutable mlir::SymbolTableCollection symbols;
  mutable mlir::LockedSymbolTableCollection lockedSymbols;
  llvm::DenseMap<mlir::Operation *, Callable> callables;
};

} // namespace obelisk::simlowering

#endif
