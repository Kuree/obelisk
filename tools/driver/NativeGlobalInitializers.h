//===- NativeGlobalInitializers.h - Constant trees --------------*- C++ -*-===//

#ifndef OBELISK_TOOLS_DRIVER_NATIVEGLOBALINITIALIZERS_H
#define OBELISK_TOOLS_DRIVER_NATIVEGLOBALINITIALIZERS_H

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Target/LLVMIR/TypeToLLVM.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/MathExtras.h"

#include <algorithm>
#include <cstdint>

namespace obelisk::driver::detail {

/// LLVM folds each insertvalue in a global initializer into a new constant.
/// A flat N-element table therefore repeatedly copies N elements. Construct
/// the same contiguous storage as a tree with at most 64 fields per node.
/// The caller proves that adding unpacked structs preserves ABI alignment.
/// Every child then contains a whole number of array elements, so their
/// offsets and allocation sizes stay unchanged without extra padding.
inline mlir::Value buildNativeConstantTree(mlir::OpBuilder &builder,
                                           mlir::Location location,
                                           mlir::Type elementType,
                                           llvm::ArrayRef<mlir::Value> values) {
  constexpr uint64_t fanout = 64;
  if (values.size() <= fanout) {
    auto type = mlir::LLVM::LLVMArrayType::get(elementType, values.size());
    mlir::Value array = mlir::LLVM::ZeroOp::create(builder, location, type);
    for (auto [index, value] : llvm::enumerate(values))
      if (value)
        array = mlir::LLVM::InsertValueOp::create(
            builder, location, array, value,
            llvm::ArrayRef<int64_t>{static_cast<int64_t>(index)});
    return array;
  }
  uint64_t span = fanout;
  while (llvm::divideCeil(values.size(), span) > fanout)
    span *= fanout;
  llvm::SmallVector<mlir::Value> children;
  llvm::SmallVector<mlir::Type> types;
  for (size_t offset = 0; offset < values.size(); offset += span) {
    auto child = buildNativeConstantTree(
        builder, location, elementType,
        values.slice(offset, std::min<uint64_t>(span, values.size() - offset)));
    children.push_back(child);
    types.push_back(child.getType());
  }
  auto type =
      mlir::LLVM::LLVMStructType::getLiteral(builder.getContext(), types);
  mlir::Value aggregate = mlir::LLVM::ZeroOp::create(builder, location, type);
  for (auto [index, child] : llvm::enumerate(children))
    aggregate = mlir::LLVM::InsertValueOp::create(
        builder, location, aggregate, child,
        llvm::ArrayRef<int64_t>{static_cast<int64_t>(index)});
  return aggregate;
}

inline unsigned
balanceNativeGlobalInitializers(mlir::ModuleOp module,
                                const llvm::DataLayout &layout) {
  llvm::LLVMContext llvmContext;
  mlir::LLVM::TypeToLLVMIRTranslator types(llvmContext);
  unsigned changed = 0;
  for (auto global : module.getOps<mlir::LLVM::GlobalOp>()) {
    auto array =
        mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(global.getGlobalType());
    auto &region = global.getInitializerRegion();
    if (!array || array.getNumElements() < 256 || !global.getConstant() ||
        region.empty() || !region.hasOneBlock() ||
        (global.getLinkage() != mlir::LLVM::Linkage::Internal &&
         global.getLinkage() != mlir::LLVM::Linkage::Private))
      continue;
    // A target can impose a minimum struct alignment greater than the
    // element's alignment. Reject it before changing the global's type:
    // otherwise the new aggregate can introduce padding or alignment.
    llvm::Type *element = types.translateType(array.getElementType());
    if (!element || !element->isSized() ||
        layout.getABITypeAlign(element) !=
            layout.getABITypeAlign(llvm::StructType::get(
                llvmContext, llvm::ArrayRef<llvm::Type *>{element})))
      continue;
    auto result =
        mlir::dyn_cast<mlir::LLVM::ReturnOp>(region.front().getTerminator());
    if (!result || result.getNumOperands() != 1)
      continue;
    mlir::Value value = result.getOperand(0);
    llvm::SmallVector<mlir::LLVM::InsertValueOp> inserts;
    llvm::SmallVector<mlir::Value> values(array.getNumElements());
    bool supported = true;
    while (auto insert = value.getDefiningOp<mlir::LLVM::InsertValueOp>()) {
      auto position = insert.getPosition();
      if (!value.hasOneUse() || position.size() != 1 || position[0] < 0 ||
          uint64_t(position[0]) >= values.size()) {
        supported = false;
        break;
      }
      // Walk backwards, retaining the final assignment to each element.
      if (!values[position[0]])
        values[position[0]] = insert.getValue();
      inserts.push_back(insert);
      value = insert.getContainer();
    }
    if (!supported || inserts.size() < 256 ||
        !value.getDefiningOp<mlir::LLVM::ZeroOp>())
      continue;
    mlir::OpBuilder builder(result);
    mlir::Value tree = buildNativeConstantTree(builder, global.getLoc(),
                                               array.getElementType(), values);
    global.setGlobalType(tree.getType());
    result->setOperand(0, tree);
    for (auto insert : inserts)
      insert.erase();
    if (value.use_empty())
      value.getDefiningOp()->erase();
    ++changed;
  }
  return changed;
}

} // namespace obelisk::driver::detail

#endif
