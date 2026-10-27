//===- RuntimeToLLVMABI.h - Runtime LLVM ABI model --------------*- C++ -*-===//

#ifndef TALOS_LIB_CONVERSION_RUNTIMETOLLVM_RUNTIMETOLLVMABI_H
#define TALOS_LIB_CONVERSION_RUNTIMETOLLVM_RUNTIMETOLLVMABI_H

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Support/LLVM.h"

#include "llvm/ADT/StringMap.h"

#include <memory>
#include <mutex>

namespace llvm {
class DataLayout;
}

namespace talos::runtimelowering {

struct ABIAlignments {
  unsigned pointer = 0;
  unsigned i8 = 0;
  unsigned i32 = 0;
  unsigned i64 = 0;
  unsigned span = 0;
  unsigned argument = 0;
  unsigned environment = 0;
  unsigned action = 0;
};

/// Runtime entry points of one module, by symbol name. The runtime-call
/// patterns share it, so each name is looked up in the module once instead of
/// once per call.
struct RuntimeDeclarations {
  std::mutex mutex;
  mlir::Operation *module = nullptr;
  llvm::StringMap<mlir::Operation *> symbols;
};

struct ABITypes {
  explicit ABITypes(mlir::MLIRContext *context, ABIAlignments alignments,
                    const llvm::DataLayout &layout);

  mlir::Type pointer;
  mlir::Type voidType;
  mlir::Type i1;
  mlir::Type i8;
  mlir::Type i32;
  mlir::Type i64;
  mlir::Type span;
  mlir::Type argument;
  mlir::Type enumArgument;
  mlir::Type rawAggregateArgument;
  mlir::Type formatEnvironment;
  mlir::Type handle;
  mlir::Type action;
  mlir::Type bytecodeEntry;
  mlir::Type bytecodeValidation;
  mlir::Type bytecodeOperand;
  mlir::Type bytecodeServiceSite;
  ABIAlignments alignments;
  const llvm::DataLayout &layout;
  /// Shared by every copy made from one pattern population.
  std::shared_ptr<RuntimeDeclarations> declarations =
      std::make_shared<RuntimeDeclarations>();
};

mlir::FailureOr<ABIAlignments>
validateTargetABI(mlir::ModuleOp module, const llvm::DataLayout &layout);

ABIAlignments getABIAlignments(const llvm::DataLayout &layout);

bool containsRuntimeType(mlir::Type type);

std::optional<mlir::Type> convertRuntimeType(mlir::Type type,
                                             const ABITypes &abi);

} // namespace talos::runtimelowering

#endif // TALOS_LIB_CONVERSION_RUNTIMETOLLVM_RUNTIMETOLLVMABI_H
