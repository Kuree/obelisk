//===- SimulationABIValidation.cpp - Validate native process ABI --------===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "mlir/IR/Diagnostics.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/TargetParser/Triple.h"

using namespace mlir;

namespace obelisk::detail {

LogicalResult validateProcessABI(ModuleOp module,
                                 const llvm::DataLayout &layout) {
  // DataLayout caches StructLayout objects by LLVM type identity. Keep those
  // entries local to the LLVMContext that owns the validation-only types.
  llvm::DataLayout validationLayout(layout.getStringRepresentation());
  llvm::LLVMContext context;
  llvm::Type *pointer = llvm::PointerType::get(context, 0);
  llvm::Type *i32 = llvm::Type::getInt32Ty(context);
  llvm::Type *i64 = llvm::Type::getInt64Ty(context);
  auto checkType = [&](llvm::Type *type, uint64_t size, uint64_t alignment) {
    llvm::TypeSize actualSize = validationLayout.getTypeAllocSize(type);
    return !actualSize.isScalable() && actualSize.getFixedValue() == size &&
           validationLayout.getABITypeAlign(type).value() == alignment;
  };
  auto checkStruct = [&](llvm::ArrayRef<llvm::Type *> elements,
                         llvm::ArrayRef<uint64_t> offsets, uint64_t size,
                         uint64_t alignment) {
    auto *type = llvm::StructType::get(context, elements);
    if (!checkType(type, size, alignment))
      return false;
    const llvm::StructLayout *structLayout =
        validationLayout.getStructLayout(type);
    return llvm::all_of(llvm::enumerate(offsets), [&](auto indexedOffset) {
      return structLayout->getElementOffset(indexedOffset.index()) ==
             indexedOffset.value();
    });
  };
  auto *handle = llvm::StructType::get(context, {i32, i32, i64});
  bool compatible =
      checkType(pointer, 8, 8) && checkType(i32, 4, 4) &&
      checkType(i64, 8, 8) &&
      checkStruct({i32, i32, i32, i32, i64, i64}, {0, 4, 8, 12, 16, 24}, 32,
                  8) &&
      checkStruct({i64, i32, i32}, {0, 8, 12}, 16, 8) &&
      checkStruct({i32, i32, i64, i64, i32, i32}, {0, 4, 8, 16, 24, 28}, 32,
                  8) &&
      checkStruct({i32, i32, i64, i64, pointer, i32, i32, pointer, i64},
                  {0, 4, 8, 16, 24, 32, 36, 40, 48}, 56, 8) &&
      checkStruct({i64, pointer, i32, i32}, {0, 8, 16, 20}, 24, 8) &&
      checkStruct({i32, i32, i32, i32, pointer, i64, pointer, i64, i64, i64,
                   pointer, i64, i32, i32, pointer, i64},
                  {0, 4, 8, 12, 16, 24, 32, 40, 48, 56, 64, 72, 80, 84, 88, 96},
                  104, 8) &&
      checkStruct({pointer, i32, i32}, {0, 8, 12}, 16, 8) &&
      checkStruct({handle, i32, i32, i32, i32, pointer, pointer, pointer,
                   pointer, pointer, pointer, pointer},
                  {0, 16, 20, 24, 28, 32, 40, 48, 56, 64, 72, 80}, 88, 8) &&
      checkStruct({pointer, pointer, pointer, i64, i64, i64, pointer, i32, i32,
                   i32, i32, pointer, pointer, pointer},
                  {0, 8, 16, 24, 32, 40, 48, 56, 60, 64, 68, 72, 80, 88}, 96,
                  8);
  if (!compatible)
    return module.emitError(
        "LLVM data layout is incompatible with the Obelisk process ABI");
  if (auto tripleAttr =
          module->getAttrOfType<StringAttr>("llvm.target_triple")) {
    llvm::Triple triple(tripleAttr.getValue());
    if (!triple.isArch64Bit() || !triple.isLittleEndian())
      return module.emitError(
          "llvm.target_triple is inconsistent with the Obelisk process ABI");
  }
  return success();
}

} // namespace obelisk::detail
