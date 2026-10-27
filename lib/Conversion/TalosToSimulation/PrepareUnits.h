//===- PrepareUnits.h - Simulation code-unit planning -----------*- C++ -*-===//
//
// Private code-unit identity and declaration planning for semantic
// preparation.
//
//===----------------------------------------------------------------------===//

#ifndef TALOS_LIB_CONVERSION_TALOSTOSIMULATION_PREPAREUNITS_H
#define TALOS_LIB_CONVERSION_TALOSTOSIMULATION_PREPAREUNITS_H

#include "Detail.h"
#include "PrepareDeclarations.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Support/LLVM.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"

namespace talos::simlowering {

struct PreparedUnit {
  mlir::Operation *source;
  uint64_t id;
  sim::EntryKind entryKind;
  std::string symbol;
  std::string hierarchy;
  sim::SimFuncOp function;
  ObserverResult observerResult = ObserverResult::None;
};

struct PreparedVirtualInterfaceCallee {
  mlir::Operation *source;
  mlir::Operation *dispatchSource;
  mlir::SymbolRefAttr interfaceIdentity;
  std::string method;
  bool externExport = false;
};

struct PreparedUnits {
  using VirtualInterfaceCalleeIndex = llvm::StringMap<
      llvm::DenseMap<mlir::SymbolRefAttr, mlir::SmallVector<unsigned>>>;

  mlir::SmallVector<PreparedUnit> units;
  llvm::StringMap<mlir::Operation *> directCalleeSources;
  llvm::DenseMap<mlir::Operation *, std::string> directCalleeNames;
  llvm::DenseMap<mlir::Operation *, mlir::Operation *> externCalleeTargets;
  llvm::DenseMap<mlir::Operation *, mlir::SmallVector<mlir::Operation *>>
      externForkJoinTargets;
  llvm::DenseMap<mlir::Operation *, sim::SimCodeUnitDeclOp> declarations;
  mlir::SmallVector<PreparedVirtualInterfaceCallee> virtualInterfaceCallees;
  VirtualInterfaceCalleeIndex virtualInterfaceCalleeIndex;
  uint64_t rootID;

  /// Resolve a direct call by semantic symbol identity, falling back to its
  /// elaborated path for legacy semantic IR.
  mlir::Operation *
  resolveDirectCallee(ir::SVCallExpressionOp call,
                      const llvm::StringMap<mlir::Operation *> &symbols) const;

  /// Resolve every elaborated implementation selected by a virtual-interface
  /// receiver. The returned units are ordered by scope ID by the caller.
  mlir::SmallVector<const PreparedVirtualInterfaceCallee *>
  resolveVirtualInterfaceCallees(ir::SVCallExpressionOp call) const;
};

/// Assign stable identities to ordinary, observer, fork, and root code units,
/// and materialize their declaration shells.
mlir::FailureOr<PreparedUnits> materializeCodeUnitDeclarations(
    mlir::ModuleOp module, ir::SVRootSymbolOp semanticRoot,
    mlir::ArrayRef<mlir::Operation *> sourceUnits,
    const llvm::StringMap<mlir::Operation *> &semanticSymbols,
    const PreparedScopeDeclarations &scopes, mlir::OpBuilder &builder);

} // namespace talos::simlowering

#endif // TALOS_LIB_CONVERSION_TALOSTOSIMULATION_PREPAREUNITS_H
