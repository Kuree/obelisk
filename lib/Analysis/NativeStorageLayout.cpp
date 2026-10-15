//===- NativeStorageLayout.cpp - LLVM aggregate storage geometry ----------===//

#include "obelisk/Analysis/NativeStorageLayout.h"
#include "mlir/IR/AttrTypeSubElements.h"
#include "mlir/IR/BuiltinOps.h"
#include "obelisk/Analysis/SimulationAnalysis.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/Support/Error.h"

using namespace mlir;

namespace obelisk::analysis {

LogicalResult verifyNativeStorageLayouts(Operation *root,
                                         const llvm::DataLayout &dataLayout) {
  NativeStorageLayout layout(dataLayout);
  AttrTypeWalker walker;
  walker.addWalk(
      [&](Type type) {
        Attribute encoding;
        if (auto array = dyn_cast<sim::UnpackedArrayType>(type))
          encoding = array.getLayout();
        else if (auto record = dyn_cast<sim::UnpackedStructType>(type))
          encoding = record.getLayout();
        if (encoding && encoding != layout.getEncoding(type)) {
          root->emitError()
              << "aggregate storage layout disagrees with target LLVM "
                 "data layout: "
              << type;
          return WalkResult::interrupt();
        }
        return WalkResult::advance();
      });
  WalkResult result = root->walk([&](Operation *operation) {
    if (walker.walk(operation->getAttrDictionary()).wasInterrupted())
      return WalkResult::interrupt();
    for (Type type : operation->getResultTypes())
      if (walker.walk(type).wasInterrupted())
        return WalkResult::interrupt();
    for (Region &region : operation->getRegions())
      for (Block &block : region)
        for (Type type : block.getArgumentTypes())
          if (walker.walk(type).wasInterrupted())
            return WalkResult::interrupt();
    return WalkResult::advance();
  });
  return failure(result.wasInterrupted());
}

static llvm::APInt getStorageMask(Type type, bool unknownOnly) {
  unsigned width = *getSimulationStorageBitWidth(type);
  if (unknownOnly && !sim::containsFourStateLeaf(type))
    return llvm::APInt::getZero(width);
  if (auto array = dyn_cast<sim::UnpackedArrayType>(type);
      array && array.getLayout()) {
    auto stride = *sim::getArrayElementStorageStride(type);
    uint64_t count = sim::getAggregateNumElements(type);
    APInt child = getStorageMask(array.getElementType(), unknownOnly);
    if (child.isAllOnes() && child.getBitWidth() == stride)
      return APInt::getAllOnes(width);
    APInt block = child.zext(width);
    APInt result(width, 0);
    uint64_t placed = 0, blockCount = 1;
    while (count) {
      if (count & 1) {
        result |= block.shl(placed * stride);
        placed += blockCount;
      }
      count >>= 1;
      if (count) {
        block |= block.shl(blockCount * stride);
        blockCount *= 2;
      }
    }
    return result;
  }
  if (auto record = dyn_cast<sim::UnpackedStructType>(type);
      record && record.getLayout()) {
    APInt result(width, 0);
    for (unsigned i = 0; i != sim::getAggregateNumElements(type); ++i) {
      auto child =
          getStorageMask(sim::getAggregateElementType(type, i), unknownOnly);
      result |= child.zext(width).shl(
          sim::getAggregateProvenanceSubelement(type, i)->first);
    }
    return result;
  }
  return llvm::APInt::getAllOnes(width);
}

llvm::APInt getDefaultUnknownMask(Type type) {
  return getStorageMask(type, true);
}

llvm::APInt getLogicalStorageMask(Type type) {
  return getStorageMask(type, false);
}

bool hasMemberDefaults(Type type) {
  // A container or class handle owns its elements elsewhere; only fixed
  // storage carries its members' defaults inline.
  if (auto array = dyn_cast<sim::UnpackedArrayType>(type))
    return hasMemberDefaults(array.getElementType());
  if (auto record = dyn_cast<sim::UnpackedStructType>(type))
    return llvm::any_of(record.getFields(), [](Attribute attribute) {
      auto field = cast<sim::FieldAttr>(attribute);
      return field.getInitializer() || hasMemberDefaults(field.getType());
    });
  return false;
}

/// Writes the storage bits of `constant` at bit `offset` of the planes.
static void placeConstant(sim::FrozenConstantAttr constant, unsigned offset,
                          APInt &value, APInt &unknown) {
  Type type = constant.getType();
  unsigned width = *getSimulationStorageBitWidth(type);
  if (isa<sim::UnpackedArrayType, sim::UnpackedStructType>(type)) {
    for (auto [index, element] :
         llvm::enumerate(cast<ArrayAttr>(constant.getValue())))
      placeConstant(cast<sim::FrozenConstantAttr>(element),
                    offset + sim::getAggregateProvenanceSubelement(
                                 type, static_cast<unsigned>(index))
                                 ->first,
                    value, unknown);
    return;
  }
  if (auto real = dyn_cast<FloatAttr>(constant.getValue())) {
    value.insertBits(real.getValue().bitcastToAPInt().zextOrTrunc(width),
                     offset);
    return;
  }
  auto planes = cast<ArrayAttr>(constant.getValue());
  value.insertBits(cast<IntegerAttr>(planes[0]).getValue().zextOrTrunc(width),
                   offset);
  unknown.insertBits(cast<IntegerAttr>(planes[1]).getValue().zextOrTrunc(width),
                     offset);
}

DefaultStoragePlanes getDefaultStoragePlanes(Type type) {
  unsigned width = *getSimulationStorageBitWidth(type);
  if (!hasMemberDefaults(type))
    return {APInt::getZero(width), getDefaultUnknownMask(type)};
  DefaultStoragePlanes planes{APInt::getZero(width), APInt::getZero(width)};
  auto place = [&](const DefaultStoragePlanes &child, unsigned offset) {
    planes.value.insertBits(child.value, offset);
    planes.unknown.insertBits(child.unknown, offset);
  };
  if (auto array = dyn_cast<sim::UnpackedArrayType>(type)) {
    DefaultStoragePlanes child =
        getDefaultStoragePlanes(array.getElementType());
    for (unsigned index = 0, count = sim::getAggregateNumElements(type);
         index != count; ++index)
      place(child, sim::getAggregateProvenanceSubelement(type, index)->first);
    return planes;
  }
  auto record = cast<sim::UnpackedStructType>(type);
  for (auto [index, fieldAttr] : llvm::enumerate(record.getFields())) {
    auto field = cast<sim::FieldAttr>(fieldAttr);
    unsigned offset = sim::getAggregateProvenanceSubelement(
                          type, static_cast<unsigned>(index))
                          ->first;
    if (auto initializer =
            dyn_cast_or_null<sim::FrozenConstantAttr>(field.getInitializer()))
      placeConstant(initializer, offset, planes.value, planes.unknown);
    else
      place(getDefaultStoragePlanes(field.getType()), offset);
  }
  return planes;
}

llvm::Type *NativeStorageLayout::getType(Type type) {
  if (auto found = types.find(type); found != types.end())
    return found->second;
  llvm::Type *storage = nullptr;
  if (auto array = dyn_cast<sim::UnpackedArrayType>(type)) {
    auto *child = getType(array.getElementType());
    if (!child)
      return nullptr;
    storage = llvm::ArrayType::get(child, sim::getAggregateNumElements(type));
  } else if (isa<sim::UnpackedStructType>(type)) {
    SmallVector<llvm::Type *> fields;
    for (unsigned i = 0; i != sim::getAggregateNumElements(type); ++i) {
      auto *child = getType(sim::getAggregateElementType(type, i));
      if (!child)
        return nullptr;
      fields.push_back(child);
    }
    storage = llvm::StructType::get(context, fields);
  } else if (type.isF32()) {
    storage = llvm::Type::getFloatTy(context);
  } else if (type.isF64()) {
    storage = llvm::Type::getDoubleTy(context);
  } else if (auto width = getSimulationStorageBitWidth(type); width && *width) {
    storage = llvm::IntegerType::get(context, *width);
  }
  types.try_emplace(type, storage);
  return storage;
}

sim::StorageLayoutAttr NativeStorageLayout::getEncoding(Type type) {
  llvm::Type *storage = getType(type);
  if (!storage)
    return {};
  uint64_t stride = 0;
  SmallVector<int64_t> offsets;
  if (auto *array = llvm::dyn_cast<llvm::ArrayType>(storage))
    stride = dataLayout.getTypeAllocSize(array->getElementType());
  else if (auto *record = llvm::dyn_cast<llvm::StructType>(storage)) {
    const llvm::StructLayout *layout = dataLayout.getStructLayout(record);
    for (unsigned i = 0; i != record->getNumElements(); ++i)
      offsets.push_back(layout->getElementOffset(i));
  } else
    return {};
  return sim::StorageLayoutAttr::get(
      type.getContext(), dataLayout.getTypeAllocSize(storage).getFixedValue(),
      dataLayout.getABITypeAlign(storage).value(), stride,
      DenseI64ArrayAttr::get(type.getContext(), offsets));
}

FailureOr<Type> encodeNativeStorageLayout(Type type, Operation *scope) {
  auto module = dyn_cast<ModuleOp>(scope);
  if (!module)
    module = scope->getParentOfType<ModuleOp>();
  auto target = module ? module->getAttrOfType<StringAttr>("llvm.data_layout")
                       : StringAttr{};
  if (!target)
    return type;
  // Avoid constructing LLVM caches for the overwhelmingly common scalar case.
  bool hasAggregate = false;
  type.walk([&](Type child) {
    hasAggregate |= isa<sim::UnpackedArrayType, sim::UnpackedStructType>(child);
  });
  if (!hasAggregate)
    return type;
  auto parsed = llvm::DataLayout::parse(target.getValue());
  if (!parsed) {
    scope->emitError() << "invalid LLVM data layout: "
                       << llvm::toString(parsed.takeError());
    return failure();
  }
  NativeStorageLayout layout(*parsed);
  AttrTypeReplacer replacer;
  replacer.addReplacement(
      [&](sim::UnpackedArrayType array) -> std::pair<Type, WalkResult> {
        Type child = replacer.replace(array.getElementType());
        if (!child)
          return {{}, WalkResult::skip()};
        auto result = sim::UnpackedArrayType::get(
            type.getContext(), child, array.getLeft(), array.getRight());
        auto encoding = layout.getEncoding(result);
        if (!encoding)
          return {result, WalkResult::skip()};
        return {sim::UnpackedArrayType::get(type.getContext(), child,
                                            array.getLeft(), array.getRight(),
                                            encoding),
                WalkResult::skip()};
      });
  replacer.addReplacement(
      [&](sim::UnpackedStructType record) -> std::pair<Type, WalkResult> {
        auto fields =
            dyn_cast_or_null<ArrayAttr>(replacer.replace(record.getFields()));
        if (!fields)
          return {{}, WalkResult::skip()};
        auto result = sim::UnpackedStructType::get(type.getContext(), fields);
        auto encoding = layout.getEncoding(result);
        if (!encoding)
          return {result, WalkResult::skip()};
        return {
            sim::UnpackedStructType::get(type.getContext(), fields, encoding),
            WalkResult::skip()};
      });
  Type encoded = replacer.replace(type);
  if (!encoded) {
    scope->emitError("cannot derive native aggregate storage layout");
    return failure();
  }
  return encoded;
}

} // namespace obelisk::analysis
