//===- RuntimeToLLVMABI.h - Runtime LLVM ABI model --------------*- C++ -*-===//

#ifndef OBELISK_LIB_CONVERSION_RUNTIMETOLLVM_RUNTIMETOLLVMABI_H
#define OBELISK_LIB_CONVERSION_RUNTIMETOLLVM_RUNTIMETOLLVMABI_H

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Support/LLVM.h"

namespace llvm {
class DataLayout;
}

namespace obelisk::runtimelowering {

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
  mlir::Type formatEnvironment;
  mlir::Type handle;
  mlir::Type action;
  mlir::Type bytecodeEntry;
  mlir::Type bytecodeValidation;
  mlir::Type bytecodeOperand;
  mlir::Type bytecodeServiceSite;
  ABIAlignments alignments;
  const llvm::DataLayout &layout;
};

mlir::FailureOr<ABIAlignments>
validateTargetABI(mlir::ModuleOp module, const llvm::DataLayout &layout);

ABIAlignments getABIAlignments(const llvm::DataLayout &layout);

bool containsRuntimeType(mlir::Type type);

std::optional<mlir::Type> convertRuntimeType(mlir::Type type,
                                             const ABITypes &abi);

} // namespace obelisk::runtimelowering

#endif // OBELISK_LIB_CONVERSION_RUNTIMETOLLVM_RUNTIMETOLLVMABI_H
