//===- ManagedClassLayoutAnalysis.cpp - Shared managed class layout -------===//

#include "obelisk/Analysis/ManagedClassLayoutAnalysis.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"

#include <algorithm>
#include <functional>
#include <limits>

using namespace mlir;

namespace obelisk::analysis {
namespace {

bool checkedAlignTo(uint64_t value, uint32_t alignment, uint64_t &result) {
  if (alignment == 0 || (alignment & (alignment - 1)) != 0 ||
      value > std::numeric_limits<uint64_t>::max() - (alignment - 1))
    return false;
  result = llvm::alignTo(value, static_cast<uint64_t>(alignment));
  return true;
}

} // namespace

FailureOr<ManagedClassLayoutAnalysis>
ManagedClassLayoutAnalysis::compute(sim::SimDesignOp design,
                                    const llvm::DataLayout &dataLayout) {
  ManagedClassLayoutAnalysis result;
  llvm::StringMap<sim::SimClassDeclOp> declarations;
  llvm::StringMap<SmallVector<sim::SimClassFieldDeclOp>> fields;
  design.walk([&](sim::SimClassDeclOp declaration) {
    declarations[declaration.getSymName()] = declaration;
  });
  design.walk([&](sim::SimClassFieldDeclOp field) {
    fields[field.getOwner()].push_back(field);
  });
  for (auto &entry : fields)
    llvm::sort(entry.second, [](auto lhs, auto rhs) {
      return lhs.getOrdinal() < rhs.getOrdinal();
    });

  llvm::DataLayout localDataLayout(dataLayout.getStringRepresentation());
  llvm::LLVMContext llvmContext;
  llvm::Type *pointerType = llvm::PointerType::get(llvmContext, 0);
  llvm::TypeSize pointerSize = localDataLayout.getTypeAllocSize(pointerType);
  if (pointerSize.isScalable() || pointerSize.getFixedValue() == 0 ||
      pointerSize.getFixedValue() > std::numeric_limits<uint32_t>::max()) {
    design.emitOpError("managed class layout requires a fixed pointer size");
    return failure();
  }
  uint64_t objectHeaderSize = pointerSize.getFixedValue();
  uint64_t pointerAlignment =
      localDataLayout.getABITypeAlign(pointerType).value();
  if (pointerAlignment == 0 ||
      pointerAlignment > std::numeric_limits<uint32_t>::max() ||
      (pointerAlignment & (pointerAlignment - 1)) != 0) {
    design.emitOpError("managed class layout requires pointer alignment");
    return failure();
  }
  uint32_t objectAlignment = static_cast<uint32_t>(pointerAlignment);

  llvm::StringSet<> active;
  std::function<LogicalResult(sim::SimClassDeclOp)> computeClass =
      [&](sim::SimClassDeclOp declaration) -> LogicalResult {
    if (result.indices.count(declaration.getSymName()))
      return success();
    if (!active.insert(declaration.getSymName()).second)
      return declaration.emitOpError("managed class layout contains a cycle");

    Class layout{
        declaration, objectHeaderSize, objectAlignment, std::nullopt, {}};
    if (auto baseName = declaration.getBase()) {
      auto base = declarations.find(*baseName);
      if (base == declarations.end())
        return declaration.emitOpError(
            "managed class layout references an unknown base");
      if (failed(computeClass(base->second)))
        return failure();
      const Class *baseLayout = result.lookup(base->getKey());
      if (!baseLayout)
        return failure();
      layout.size = baseLayout->size;
      layout.alignment = baseLayout->alignment;
    }

    if (declaration.getWeakReferentAttr()) {
      uint64_t referentOffset;
      if (!checkedAlignTo(layout.size, objectAlignment, referentOffset) ||
          referentOffset >
              std::numeric_limits<uint64_t>::max() - objectHeaderSize)
        return declaration.emitOpError("weak referent layout overflows");
      layout.weakReferentOffset = referentOffset;
      layout.size = referentOffset + objectHeaderSize;
      layout.alignment = std::max(layout.alignment, objectAlignment);
    }

    for (sim::SimClassFieldDeclOp field : fields[declaration.getSymName()]) {
      if (field.getIsStatic())
        continue;
      FailureOr<SimulationStorageProperties> storage =
          getSimulationStorageProperties(field.getType(), localDataLayout,
                                         llvmContext);
      if (failed(storage))
        return field.emitOpError("class property has no fixed managed layout");
      uint64_t offset;
      if (!checkedAlignTo(layout.size, storage->alignment, offset))
        return field.emitOpError("class property offset overflows");
      size_t planes = getSimulationPhysicalStorageCount(*storage);
      if (storage->size >
          (std::numeric_limits<uint64_t>::max() - offset) / planes)
        return field.emitOpError("class property layout overflows");
      layout.size = offset + storage->size * planes;
      layout.alignment = std::max(layout.alignment, storage->alignment);
      layout.fields.push_back({field, offset, std::move(*storage)});
    }

    uint64_t alignedSize;
    if (!checkedAlignTo(layout.size, layout.alignment, alignedSize))
      return declaration.emitOpError("class instance size overflows");
    layout.size = alignedSize;
    active.erase(declaration.getSymName());
    result.indices[declaration.getSymName()] = result.classes.size();
    result.classes.push_back(std::move(layout));
    return success();
  };

  SmallVector<sim::SimClassDeclOp> ordered;
  ordered.reserve(declarations.size());
  for (const auto &entry : declarations)
    ordered.push_back(entry.second);
  llvm::sort(ordered, [](auto lhs, auto rhs) {
    return std::make_pair(lhs.getId(), lhs.getSymName()) <
           std::make_pair(rhs.getId(), rhs.getSymName());
  });
  for (sim::SimClassDeclOp declaration : ordered)
    if (failed(computeClass(declaration)))
      return failure();
  return result;
}

const ManagedClassLayoutAnalysis::Class *
ManagedClassLayoutAnalysis::lookup(StringRef name) const {
  auto found = indices.find(name);
  return found == indices.end() ? nullptr : &classes[found->second];
}

LogicalResult materializeManagedClassFieldOffsets(
    const ManagedClassLayoutAnalysis &analysis) {
  for (const ManagedClassLayoutAnalysis::Class &layout : analysis.classes) {
    for (const ManagedClassLayoutAnalysis::Field &field : layout.fields) {
      sim::SimClassFieldDeclOp declaration = field.declaration;
      if (auto existing = declaration->getAttrOfType<IntegerAttr>("offset");
          existing && existing.getValue().getZExtValue() != field.offset)
        return declaration.emitOpError(
            "field offset disagrees with the shared managed class layout");
      declaration->setAttr(
          "offset",
          IntegerAttr::get(IntegerType::get(declaration.getContext(), 64),
                           field.offset));
    }
  }
  return success();
}

} // namespace obelisk::analysis
