//===- PackedStorageLayout.cpp - Packed access units ----------------------===//

#include "PackedStorageLayout.h"
#include "mlir/IR/BuiltinOps.h"
#include "talos/Analysis/SimulationAnalysis.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/TargetParser/Triple.h"

using namespace mlir;
namespace talos::detail {

PackedStorageLayout::PackedStorageLayout(ModuleOp module,
                                         const llvm::DataLayout &layout)
    : registerBytes(layout.getPointerSize()),
      littleEndian(layout.isLittleEndian()) {
  auto attr = module->getAttrOfType<StringAttr>("llvm.target_triple");
  llvm::Triple triple(attr ? attr.getValue() : "");
  // These targets permit the unaligned accesses used by Clang's SysV packed
  // bit-field lowering. On other targets, retain the mandatory shared-byte
  // units without merging them into potentially expensive unaligned accesses.
  cheapUnaligned = triple.isX86() || triple.isAArch64() || triple.isWasm();
}

std::optional<PackedAccessUnit>
PackedStorageLayout::get(Type type, uint64_t physicalOffset, uint64_t offset,
                         unsigned width) const {
  if (!littleEndian || !type || !width)
    return std::nullopt;
  auto objectWidth = analysis::getSimulationStorageBitWidth(type);
  if (!objectWidth || offset > *objectWidth || width > *objectWidth - offset)
    return std::nullopt;
  if (isa<sim::PackedStructType>(type)) {
    // Clang CGRecordLowering::accumulateBitFields (SysV ABI): fields sharing
    // a byte must share an access unit, even when it is wider than a register.
    // Adjacent units can merge only into a naturally sized, register-sized
    // access. Look ahead: 2 + 1 + 1 bytes can merge, whereas 2 + 1 cannot.
    // SV packed records have no internal padding (IEEE 1800-2023 7.2.1).
    // Consequently a byte boundary is usable exactly when a field starts
    // there, and only the final partial byte supplies tail padding.
    unsigned phase = physicalOffset % 8;
    SmallVector<uint64_t> boundaries{0};
    for (auto attr : cast<sim::PackedStructType>(type).getFields()) {
      auto field = cast<sim::FieldAttr>(attr);
      uint64_t start = phase + field.getPackedOffset();
      if (start && start % 8 == 0)
        boundaries.push_back(start / 8);
    }
    boundaries.push_back(llvm::divideCeil(phase + *objectWidth, 8u));
    llvm::sort(boundaries);
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()),
                     boundaries.end());
    for (unsigned begin = 0; begin + 1 < boundaries.size();) {
      unsigned best = begin + 1;
      if (cheapUnaligned)
        for (unsigned end = best; end < boundaries.size(); ++end) {
          uint64_t bytes = boundaries[end] - boundaries[begin];
          if (bytes > registerBytes)
            break;
          if (llvm::isPowerOf2_64(bytes))
            best = end;
        }
      uint64_t start = boundaries[begin] * 8;
      uint64_t end = boundaries[best] * 8;
      if (end - start <= llvm::IntegerType::MAX_INT_BITS &&
          phase + offset >= start && phase + offset + width <= end)
        return PackedAccessUnit{static_cast<unsigned>(phase + offset - start),
                                static_cast<unsigned>(end - start)};
      begin = best;
    }
    return std::nullopt;
  }
  // Locate a containing record without enumerating large arrays. Reference
  // provenance includes any alignment of unpacked managed elements.
  if (isa<sim::PackedArrayType, sim::UnpackedArrayType>(type)) {
    unsigned count = sim::getAggregateNumElements(type);
    if (!count)
      return std::nullopt;
    auto first = sim::getAggregateProvenanceSubelement(type, 0);
    uint64_t stride = *sim::getArrayElementStorageStride(type);
    if (!stride)
      return std::nullopt;
    uint64_t base = offset / stride * stride;
    if (isa<sim::UnpackedArrayType>(type) && offset == base &&
        width == first->second && stride <= 64 && stride % 8 == 0 &&
        (physicalOffset + base) % 8 == 0)
      return PackedAccessUnit{0, static_cast<unsigned>(stride), true};
    return get(sim::getAggregateElementType(type, 0), physicalOffset + base,
               offset - base, width);
  }
  if (isa<sim::UnpackedStructType>(type))
    for (unsigned index = 0; index != sim::getAggregateNumElements(type);
         ++index) {
      auto field = sim::getAggregateProvenanceSubelement(type, index);
      if (offset >= field->first && offset - field->first < field->second)
        return get(sim::getAggregateElementType(type, index),
                   physicalOffset + field->first, offset - field->first, width);
    }
  return std::nullopt;
}

std::optional<PackedAccessUnit> PackedStorageLayout::get(
    Value reference, llvm::function_ref<bool(Value)> isByteAddressable) const {
  Type selected = cast<sim::RefType>(reference.getType()).getElementType();
  auto width = analysis::getSimulationStorageBitWidth(selected);
  if (!width)
    return std::nullopt;
  uint64_t offset = 0;
  while (reference) {
    Type type = cast<sim::RefType>(reference.getType()).getElementType();
    if (!offset && type == selected && *width <= 64)
      if (auto element = reference.getDefiningOp<sim::SimRefArrayElementOp>()) {
        Type parent = element.getInput().getType().getElementType();
        auto stride = sim::getArrayElementStorageStride(parent);
        if (isa<sim::UnpackedArrayType>(parent) && stride && *stride <= 64 &&
            *stride >= *width && *stride % 8 == 0 &&
            isByteAddressable(reference))
          return PackedAccessUnit{0, static_cast<unsigned>(*stride), true};
      }
    if (isa<sim::PackedStructType>(type))
      return isByteAddressable(reference) ? get(type, 0, offset, *width)
                                          : std::nullopt;
    if (auto field = reference.getDefiningOp<sim::SimRefSubelementOp>()) {
      Type parent = field.getInput().getType().getElementType();
      // Each index may descend into another packed record. Use the outer
      // record's unit so siblings in a nested path retain a common address.
      for (int64_t index : field.getIndices()) {
        if (!isa<sim::PackedStructType, sim::PackedArrayType,
                 sim::PackedUnionType>(parent))
          return std::nullopt;
        offset += sim::getAggregateProvenanceSubelement(parent, index)->first;
        parent = sim::getAggregateElementType(parent, index);
      }
      reference = field.getInput();
    } else if (auto extract = reference.getDefiningOp<sim::SimRefExtractOp>()) {
      offset += extract.getLowBit();
      reference = extract.getInput();
    } else {
      break;
    }
  }
  return std::nullopt;
}

} // namespace talos::detail
