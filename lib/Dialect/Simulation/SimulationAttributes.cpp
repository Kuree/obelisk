//===- SimulationAttributes.cpp - Attribute verifiers ---------===//
//
// Verifiers for the simulation dialect attributes that describe compute
// effects, fragments, kernels, and the three-tier schedule.
//
//===----------------------------------------------------------------------===//

#include "SimulationVerifiers.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Reflection/VPIObjectModel.h"
#include "obelisk/Runtime/StableHash.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Interfaces/FunctionImplementation.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Transforms/InliningUtils.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/ADT/bit.h"

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <set>

using namespace mlir;

namespace obelisk::sim {

LogicalResult
VPIPropertyAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                        IntegerAttr selector, Attribute value) {
  if (!selector || !selector.getType().isSignlessInteger(32) ||
      selector.getValue().isNegative() || !value)
    return emitError()
           << "VPI property requires a nonnegative i32 selector and value";
  uint64_t number = selector.getValue().getZExtValue();
  if (number > UINT32_MAX)
    return emitError() << "VPI property selector exceeds 32 bits";
  const reflection::VPIPropertyDescriptor *descriptor = nullptr;
  for (const auto &candidate : reflection::vpiProperties)
    if (candidate.property == number) {
      descriptor = &candidate;
      break;
    }
  if (!descriptor)
    return emitError() << "unknown VPI property selector " << number;
  using Kind = reflection::VPIPropertyValueKind;
  bool matches = false;
  switch (descriptor->valueKind) {
  case Kind::Boolean:
    matches = isa<BoolAttr>(value);
    break;
  case Kind::Integer:
    matches = isa<IntegerAttr>(value) &&
              cast<IntegerAttr>(value).getType().isSignlessInteger(32);
    break;
  case Kind::Int64:
    matches = isa<IntegerAttr>(value) &&
              cast<IntegerAttr>(value).getType().isSignlessInteger(64);
    break;
  case Kind::String:
    matches = isa<StringAttr>(value);
    break;
  }
  if (!matches)
    return emitError() << "VPI property value does not match its generated "
                          "value kind";
  if (descriptor->valueKind == Kind::Integer &&
      reflection::hasVPIIntegerPropertyDomain(static_cast<uint32_t>(number))) {
    uint32_t integerValue = static_cast<uint32_t>(
        cast<IntegerAttr>(value).getValue().getSExtValue());
    if (!reflection::findVPIIntegerPropertyValue(static_cast<uint32_t>(number),
                                                 integerValue))
      return emitError()
             << "VPI integer property value is outside its generated domain";
  }
  return success();
}

LogicalResult
VPIPropertySetAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                           ArrayAttr properties) {
  if (!properties)
    return emitError() << "VPI property set requires an array";
  uint32_t previous = 0;
  bool first = true;
  for (Attribute attribute : properties) {
    auto property = dyn_cast<VPIPropertyAttr>(attribute);
    if (!property)
      return emitError() << "VPI property set elements must be VPI properties";
    uint32_t selector =
        static_cast<uint32_t>(property.getSelector().getValue().getZExtValue());
    if (!first && selector <= previous)
      return emitError() << "VPI property set must be sorted and unique";
    first = false;
    previous = selector;
  }
  return success();
}

LogicalResult
VPIObjectRefAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                         VPIObjectRefKind kind, IntegerAttr id) {
  (void)kind;
  if (!id || !id.getType().isSignlessInteger(64))
    return emitError() << "VPI object reference ID must be a signless i64";
  if (id.getValue().isNegative())
    return emitError() << "VPI object reference ID must be nonnegative";
  return success();
}

LogicalResult
VPIObjectBackingAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                             VPIObjectBackingKind kind, IntegerAttr id,
                             FlatSymbolRefAttr symbol) {
  switch (kind) {
  case VPIObjectBackingKind::Scope:
  case VPIObjectBackingKind::CodeUnit:
  case VPIObjectBackingKind::Net:
    if (!id || symbol)
      return emitError()
             << "VPI scope/code-unit/net backing requires only a numeric ID";
    if (!id.getType().isSignlessInteger(64))
      return emitError() << "VPI backing ID must be a signless i64";
    if (id.getValue().isNegative())
      return emitError() << "VPI backing ID must be nonnegative";
    return success();
  case VPIObjectBackingKind::Class:
    if (id || !symbol)
      return emitError()
             << "VPI class backing requires only a flat class symbol";
    return success();
  }
  llvm_unreachable("unknown VPI object backing kind");
}

LogicalResult VPITypeSemanticsAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, VPITypeKind kind,
    bool isSigned, bool isFourState, StringAttr name, SymbolRefAttr symbol,
    StringAttr modport, DenseI64ArrayAttr range, ArrayAttr children,
    ArrayAttr childNames, BoolAttr isTagged, BoolAttr isSoft,
    IntegerAttr bitWidth, IntegerAttr selectableWidth,
    IntegerAttr bitstreamWidth, IntegerAttr tagBits, IntegerAttr queueBound,
    BoolAttr wildcardIndex, DenseI64ArrayAttr childOrdinals,
    DenseI64ArrayAttr childPackedOffsets, DenseI64ArrayAttr childRandTypes,
    ArrayAttr typedefAliases) {
  if (!range || (range.size() != 0 && range.size() != 2))
    return emitError() << "VPI semantic type range must be empty or contain "
                          "one left/right pair";
  if (!children || llvm::any_of(children, [](Attribute child) {
        return !isa<VPITypeSemanticsAttr>(child);
      }))
    return emitError()
           << "VPI semantic type children must all be semantic type attributes";
  if (!childNames || llvm::any_of(childNames, [](Attribute childName) {
        auto string = dyn_cast<StringAttr>(childName);
        return !string || string.getValue().empty();
      }))
    return emitError() << "VPI semantic type child names must all be strings";
  if (typedefAliases && llvm::any_of(typedefAliases, [](Attribute alias) {
        return !isa<SymbolRefAttr>(alias);
      }))
    return emitError()
           << "VPI semantic type typedef aliases must all be symbol references";
  if (typedefAliases) {
    if (typedefAliases.empty())
      return emitError()
             << "VPI semantic type typedef alias chain must not be empty";
    llvm::SmallDenseSet<Attribute, 4> uniqueAliases;
    for (Attribute alias : typedefAliases)
      if (!uniqueAliases.insert(alias).second)
        return emitError() << "VPI semantic type typedef alias chain is cyclic";
  }

  const bool aggregate = kind == VPITypeKind::PackedStruct ||
                         kind == VPITypeKind::UnpackedStruct ||
                         kind == VPITypeKind::PackedUnion ||
                         kind == VPITypeKind::UnpackedUnion;
  const bool stringNamed = kind == VPITypeKind::Enum || aggregate ||
                           kind == VPITypeKind::VirtualInterface;
  if (name && (!stringNamed || name.getValue().empty()))
    return emitError()
           << "VPI semantic type has an invalid named-type identity";
  const bool symbolic = kind == VPITypeKind::Class ||
                        kind == VPITypeKind::VirtualInterface ||
                        kind == VPITypeKind::Covergroup;
  if (symbol && !symbolic)
    return emitError() << "VPI semantic type has an invalid symbol identity";
  if (symbolic && !name && !symbol)
    return emitError() << "VPI semantic type kind "
                       << stringifyVPITypeKind(kind)
                       << " requires a named-type identity";
  if (kind == VPITypeKind::VirtualInterface && !name)
    return emitError()
           << "VPI virtual-interface semantic type requires its opaque "
              "specialization identity";
  if (name && symbol && kind != VPITypeKind::VirtualInterface)
    return emitError()
           << "VPI semantic type cannot carry two named-type identities";
  if (modport && kind != VPITypeKind::VirtualInterface)
    return emitError() << "only a virtual-interface semantic type may name a "
                          "modport";
  if (kind == VPITypeKind::VirtualInterface && !modport)
    return emitError()
           << "VPI virtual-interface semantic type requires modport metadata";

  const bool ranged =
      kind == VPITypeKind::GenericIntegral || kind == VPITypeKind::Bit ||
      kind == VPITypeKind::Logic || kind == VPITypeKind::Reg ||
      kind == VPITypeKind::Byte || kind == VPITypeKind::ShortInt ||
      kind == VPITypeKind::Int || kind == VPITypeKind::LongInt ||
      kind == VPITypeKind::Integer || kind == VPITypeKind::Time ||
      kind == VPITypeKind::PackedArray || kind == VPITypeKind::UnpackedArray;
  if (range.size() != (ranged ? 2u : 0u))
    return emitError() << "VPI semantic type kind "
                       << stringifyVPITypeKind(kind)
                       << (ranged ? " requires" : " cannot have")
                       << " a source range";

  const size_t childCount = children.size();
  auto requireChildren = [&](size_t expected) -> LogicalResult {
    if (childCount != expected)
      return emitError() << "VPI semantic type kind "
                         << stringifyVPITypeKind(kind) << " requires "
                         << expected << " child type(s)";
    return success();
  };
  switch (kind) {
  case VPITypeKind::Enum:
    if (!name || name.getValue().empty())
      return emitError() << "VPI enum semantic type requires a name";
    if (failed(requireChildren(1)))
      return failure();
    break;
  case VPITypeKind::PackedArray:
  case VPITypeKind::UnpackedArray:
  case VPITypeKind::DynamicArray:
  case VPITypeKind::Queue:
  case VPITypeKind::PackedOpenArray:
  case VPITypeKind::UnpackedOpenArray:
  case VPITypeKind::Mailbox:
    if (failed(requireChildren(1)))
      return failure();
    break;
  case VPITypeKind::AssocArray:
    if (failed(requireChildren(2)))
      return failure();
    break;
  case VPITypeKind::PackedStruct:
  case VPITypeKind::UnpackedStruct:
  case VPITypeKind::PackedUnion:
  case VPITypeKind::UnpackedUnion:
    if (childCount == 0)
      return emitError() << "VPI aggregate semantic type requires a field";
    if (childNames.size() != childCount)
      return emitError()
             << "VPI aggregate semantic type requires one name per field";
    break;
  default:
    if (failed(requireChildren(0)))
      return failure();
    break;
  }
  if (!aggregate && !childNames.empty())
    return emitError()
           << "only VPI aggregate semantic types may name child fields";

  auto rejectDetail = [&](Attribute detail, StringRef detailName,
                          bool allowed) -> LogicalResult {
    if (detail && !allowed)
      return emitError() << "VPI semantic type kind "
                         << stringifyVPITypeKind(kind) << " cannot carry "
                         << detailName;
    return success();
  };
  if (failed(rejectDetail(isTagged, "tagged metadata", aggregate)) ||
      failed(rejectDetail(isSoft, "soft-tag metadata", aggregate)) ||
      failed(rejectDetail(bitWidth, "bit-width metadata", aggregate)) ||
      failed(rejectDetail(selectableWidth, "selectable-width metadata",
                          aggregate)) ||
      failed(rejectDetail(bitstreamWidth, "bitstream-width metadata",
                          aggregate)) ||
      failed(rejectDetail(tagBits, "tag-bit metadata", aggregate)) ||
      failed(rejectDetail(queueBound, "queue-bound metadata",
                          kind == VPITypeKind::Queue)) ||
      failed(rejectDetail(wildcardIndex, "wildcard-index metadata",
                          kind == VPITypeKind::AssocArray)) ||
      failed(
          rejectDetail(childOrdinals, "field-ordinal metadata", aggregate)) ||
      failed(rejectDetail(childPackedOffsets, "field-offset metadata",
                          aggregate)) ||
      failed(rejectDetail(childRandTypes, "field-randomization metadata",
                          aggregate)))
    return failure();
  if (kind == VPITypeKind::Queue && !queueBound)
    return emitError() << "VPI queue semantic type requires bound metadata";
  if (kind == VPITypeKind::AssocArray && !wildcardIndex)
    return emitError()
           << "VPI associative-array semantic type requires wildcard metadata";
  auto requireNonnegative = [&](IntegerAttr value,
                                StringRef detailName) -> LogicalResult {
    if (value && (!value.getType().isSignlessInteger(64) ||
                  value.getValue().isNegative()))
      return emitError() << "VPI semantic type " << detailName
                         << " must be a nonnegative i64";
    return success();
  };
  if (failed(requireNonnegative(bitWidth, "bit width")) ||
      failed(requireNonnegative(selectableWidth, "selectable width")) ||
      failed(requireNonnegative(bitstreamWidth, "bitstream width")) ||
      failed(requireNonnegative(tagBits, "tag width")) ||
      failed(requireNonnegative(queueBound, "queue bound")))
    return failure();
  if (queueBound && queueBound.getValue().getActiveBits() > 32)
    return emitError() << "VPI semantic type queue bound exceeds uint32";
  if (aggregate) {
    if (!isTagged || !isSoft || !bitWidth || !selectableWidth ||
        !bitstreamWidth || !tagBits || !childOrdinals || !childPackedOffsets)
      return emitError()
             << "VPI aggregate semantic type requires complete layout metadata";
    if (static_cast<size_t>(childOrdinals.size()) != childCount ||
        static_cast<size_t>(childPackedOffsets.size()) != childCount ||
        (childRandTypes &&
         static_cast<size_t>(childRandTypes.size()) != childCount))
      return emitError() << "VPI aggregate layout must describe every field";
    for (auto [index, ordinal] : llvm::enumerate(childOrdinals.asArrayRef()))
      if (ordinal != static_cast<int64_t>(index))
        return emitError() << "VPI aggregate field ordinals must be dense "
                              "declaration order";
    if (llvm::any_of(childPackedOffsets.asArrayRef(),
                     [](int64_t offset) { return offset < 0; }))
      return emitError() << "VPI aggregate field offsets must be nonnegative";
    if (childRandTypes &&
        llvm::any_of(childRandTypes.asArrayRef(), [](int64_t randType) {
          return randType < 1 || randType > 3;
        }))
      return emitError()
             << "VPI aggregate field randomization types must be vpiNotRand, "
                "vpiRand, or vpiRandC";
    const bool unionType =
        kind == VPITypeKind::PackedUnion || kind == VPITypeKind::UnpackedUnion;
    if (isTagged.getValue() && !unionType)
      return emitError() << "only VPI union semantic types may be tagged";
    if (isSoft.getValue() &&
        (kind != VPITypeKind::PackedUnion || !isTagged.getValue()))
      return emitError()
             << "soft tagged metadata requires a tagged packed union";
    if (!isTagged.getValue() && !tagBits.getValue().isZero())
      return emitError() << "untagged VPI aggregate cannot reserve tag bits";
    const bool packed =
        kind == VPITypeKind::PackedStruct || kind == VPITypeKind::PackedUnion;
    if (packed && bitWidth.getValue().isZero())
      return emitError()
             << "packed VPI aggregate must have a nonzero bit width";
    if (packed && (bitWidth.getValue() != selectableWidth.getValue() ||
                   bitWidth.getValue() != bitstreamWidth.getValue()))
      return emitError() << "packed VPI aggregate widths must agree";
    if (!packed &&
        (!bitWidth.getValue().isZero() || isSigned || isSoft.getValue()))
      return emitError() << "unpacked VPI aggregate has packed-only metadata";
  }

  auto requireFlags = [&](bool expectedSigned,
                          bool expectedFourState) -> LogicalResult {
    if (isSigned != expectedSigned || isFourState != expectedFourState)
      return emitError() << "VPI semantic type kind "
                         << stringifyVPITypeKind(kind)
                         << " has contradictory signed/four-state flags";
    return success();
  };
  switch (kind) {
  case VPITypeKind::Bit:
  case VPITypeKind::Byte:
  case VPITypeKind::ShortInt:
  case VPITypeKind::Int:
  case VPITypeKind::LongInt:
    if (isFourState)
      return emitError() << "two-state VPI semantic type has a four-state flag";
    break;
  case VPITypeKind::Logic:
  case VPITypeKind::Reg:
  case VPITypeKind::Integer:
    if (!isFourState)
      return emitError() << "four-state VPI semantic type lacks its flag";
    break;
  case VPITypeKind::Time:
    if (failed(requireFlags(false, true)))
      return failure();
    break;
  case VPITypeKind::Enum: {
    auto base = cast<VPITypeSemanticsAttr>(children[0]);
    if (isSigned != base.getIsSigned() || isFourState != base.getIsFourState())
      return emitError() << "VPI enum flags must match its base semantic type";
    break;
  }
  case VPITypeKind::PackedArray:
  case VPITypeKind::UnpackedArray: {
    auto element = cast<VPITypeSemanticsAttr>(children[0]);
    if (isFourState != element.getIsFourState() ||
        (kind == VPITypeKind::PackedArray &&
         isSigned != element.getIsSigned()) ||
        (kind == VPITypeKind::UnpackedArray && isSigned))
      return emitError()
             << "VPI fixed-array flags contradict its element or packing";
    break;
  }
  case VPITypeKind::PackedStruct:
  case VPITypeKind::UnpackedStruct:
  case VPITypeKind::PackedUnion:
  case VPITypeKind::UnpackedUnion: {
    bool containsFourState = llvm::any_of(children, [](Attribute child) {
      return cast<VPITypeSemanticsAttr>(child).getIsFourState();
    });
    if (isFourState != containsFourState ||
        ((kind == VPITypeKind::UnpackedStruct ||
          kind == VPITypeKind::UnpackedUnion) &&
         isSigned))
      return emitError()
             << "VPI aggregate flags contradict its fields or packing";
    break;
  }
  case VPITypeKind::Unknown:
  case VPITypeKind::GenericIntegral:
    break;
  default:
    if (failed(requireFlags(false, false)))
      return failure();
    break;
  }
  return success();
}

LogicalResult RandomVariableReferenceAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError,
    ArrayRef<FlatSymbolRefAttr> path, FlatSymbolRefAttr target) {
  if (!target)
    return emitError() << "random-variable reference requires a target field";
  if (llvm::any_of(path, [](FlatSymbolRefAttr field) { return !field; }))
    return emitError()
           << "random-variable reference path contains a null field";
  return success();
}

LogicalResult RandomValueReferenceAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError,
    RandomValueReferenceKind kind, ArrayRef<FlatSymbolRefAttr> path,
    FlatSymbolRefAttr target, IntegerAttr storage, uint64_t low,
    uint64_t width) {
  if (width == 0)
    return emitError() << "random-value reference requires a nonzero width";
  if (low > std::numeric_limits<uint64_t>::max() - width)
    return emitError() << "random-value reference bit range overflows";
  if (llvm::any_of(path, [](FlatSymbolRefAttr field) { return !field; }))
    return emitError() << "random-value reference path contains a null field";

  switch (kind) {
  case RandomValueReferenceKind::ObjectField:
    if (!target)
      return emitError()
             << "object-field random-value reference requires a target";
    if (storage)
      return emitError()
             << "object-field random-value reference cannot name storage";
    return success();
  case RandomValueReferenceKind::Storage:
    if (!path.empty() || target)
      return emitError()
             << "storage random-value reference cannot name an object field";
    if (!storage || storage.getValue().isNegative() ||
        storage.getValue().getActiveBits() > 64)
      return emitError()
             << "storage random-value reference requires a nonnegative "
                "64-bit descriptor ID";
    return success();
  }
  llvm_unreachable("unknown random-value reference kind");
}

LogicalResult RandomConstraintBlockReferenceAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError,
    RandomConstraintBlockReferenceKind kind, IntegerAttr index,
    IntegerAttr storage) {
  switch (kind) {
  case RandomConstraintBlockReferenceKind::ObjectBlock:
    if (!index || index.getValue().isNegative() ||
        index.getValue().getActiveBits() > 6)
      return emitError()
             << "object constraint-block reference requires an index from 0 "
                "through 63";
    if (storage)
      return emitError()
             << "object constraint-block reference cannot name storage";
    return success();
  case RandomConstraintBlockReferenceKind::Storage:
    if (index)
      return emitError()
             << "storage constraint-block reference cannot name an object "
                "index";
    if (!storage || storage.getValue().isNegative() ||
        storage.getValue().getActiveBits() > 64)
      return emitError()
             << "storage constraint-block reference requires a nonnegative "
                "64-bit descriptor ID";
    return success();
  }
  llvm_unreachable("unknown random constraint-block reference kind");
}

LogicalResult
DPIABIAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                   DPIABIKind kind, DPIArgumentDirection, uint32_t width,
                   bool fourState, bool isSigned) {
  if (width == 0)
    return emitError() << "DPI ABI width must be nonzero";
  auto require = [&](uint32_t expectedWidth,
                     bool expectedFourState) -> LogicalResult {
    if (width != expectedWidth)
      return emitError() << "DPI ABI category requires width " << expectedWidth;
    if (fourState != expectedFourState)
      return emitError() << "DPI ABI category has incompatible state kind";
    return success();
  };
  switch (kind) {
  case DPIABIKind::Bit:
    return require(1, false);
  case DPIABIKind::Logic:
    return require(1, true);
  case DPIABIKind::Byte:
    return require(8, false);
  case DPIABIKind::ShortInt:
    return require(16, false);
  case DPIABIKind::Int:
    return require(32, false);
  case DPIABIKind::LongInt:
    return require(64, false);
  case DPIABIKind::BitVector:
    return fourState
               ? emitError() << "bit-vector DPI ABI category must be two-state"
               : success();
  case DPIABIKind::LogicVector:
    return !fourState
               ? emitError()
                     << "logic-vector DPI ABI category must be four-state"
               : success();
  case DPIABIKind::String:
  case DPIABIKind::Chandle:
    if (isSigned)
      return emitError() << "DPI handle category cannot be signed";
    return require(64, false);
  case DPIABIKind::ShortReal:
    if (isSigned)
      return emitError() << "DPI floating category cannot be signed";
    return require(32, false);
  case DPIABIKind::Real:
    if (isSigned)
      return emitError() << "DPI floating category cannot be signed";
    return require(64, false);
  case DPIABIKind::OpenArray:
    if (isSigned)
      return emitError() << "DPI open-array category cannot be signed";
    return success();
  case DPIABIKind::UnpackedAggregate:
    return success();
  }
  llvm_unreachable("unknown DPI ABI category");
}

LogicalResult DPIOpenArrayABIAttr::verify(
    llvm::function_ref<InFlightDiagnostic()> emitError, uint32_t storage,
    DPIABIKind elementKind, uint32_t elementWidth, uint64_t transportWidth,
    bool transportFourState, bool fourState, uint64_t elementCSize,
    uint32_t elementCAlignment, uint64_t elementStringCount,
    DenseI64ArrayAttr elementLeaves, int64_t packedLeft, int64_t packedRight,
    DenseI64ArrayAttr ranges, DenseI64ArrayAttr sourceRanges,
    DenseI64ArrayAttr shapePlan) {
  if (storage > 2)
    return emitError() << "DPI open-array storage kind is invalid";
  if (elementWidth == 0)
    return emitError() << "DPI open-array element width must be nonzero";
  if (!ranges || (ranges.size() & 1) != 0)
    return emitError() << "DPI open-array ranges must be left/right pairs";
  if (ranges.size() / 2 > UINT32_MAX)
    return emitError() << "DPI open-array dimension count is too large";
  if (!sourceRanges || sourceRanges.size() != ranges.size())
    return emitError() << "DPI open-array source ranges are incomplete";
  if (storage == 0 && transportWidth == 0)
    return emitError() << "fixed DPI open-array transport cannot be empty";
  if (storage == 1 && transportWidth != 64)
    return emitError() << "dynamic DPI open-array transport must be a handle";
  if (storage == 1 && transportFourState)
    return emitError() << "dynamic DPI open-array handle cannot be four-state";
  if (!shapePlan || shapePlan.size() % 8 != 0)
    return emitError()
           << "DPI open-array shape plan must use eight-word records";
  if (storage == 2 && shapePlan.size() / 8 != ranges.size() / 2)
    return emitError() << "recursive DPI open-array shape is incomplete";
  if (storage != 2 && !shapePlan.empty())
    return emitError() << "non-recursive DPI open-array has a shape plan";
  if (elementKind != DPIABIKind::UnpackedAggregate &&
      ((elementKind == DPIABIKind::Logic ||
        elementKind == DPIABIKind::LogicVector) != fourState))
    return emitError() << "DPI open-array element state kind is inconsistent";
  if (elementCSize == 0 || elementCAlignment == 0 ||
      (elementCAlignment & (elementCAlignment - 1)) != 0)
    return emitError() << "DPI open-array element C layout is invalid";
  if (!elementLeaves || elementLeaves.empty() || elementLeaves.size() % 8 != 0)
    return emitError() << "DPI open-array element plan is incomplete";
  if (elementStringCount > (UINT64_MAX - elementCSize) / 8)
    return emitError() << "DPI open-array element string layout overflows";
  if (packedLeft < INT32_MIN || packedLeft > INT32_MAX ||
      packedRight < INT32_MIN || packedRight > INT32_MAX)
    return emitError() << "DPI packed range exceeds the standardized C ABI";
  auto verifyBounds = [&](ArrayRef<int64_t> bounds) {
    return llvm::all_of(bounds, [](int64_t bound) {
      return bound >= INT32_MIN && bound <= INT32_MAX;
    });
  };
  if (!verifyBounds(ranges.asArrayRef()) ||
      !verifyBounds(sourceRanges.asArrayRef()))
    return emitError() << "DPI unpacked range exceeds the standardized C ABI";
  for (int64_t index = 0; index != shapePlan.size(); index += 8) {
    ArrayRef<int64_t> record = shapePlan.asArrayRef().slice(index, 8);
    if (record[0] < 0 || record[0] > 2 || record[1] < INT32_MIN ||
        record[1] > INT32_MAX || record[2] < INT32_MIN ||
        record[2] > INT32_MAX || record[3] < 0 || record[5] <= 0 ||
        (record[6] != 0 && record[6] != 1) ||
        (record[7] != 0 && record[7] != 1))
      return emitError() << "DPI open-array shape record " << index / 8
                         << " is malformed";
    if (record[0] == 0 && record[4] == 0)
      return emitError() << "fixed DPI open-array shape has zero stride";
    if (record[0] != 0 && (record[3] != 0 || record[4] != 0))
      return emitError() << "runtime DPI open-array shape has fixed offsets";
  }
  return success();
}

LogicalResult
DPIAggregateABIAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                            uint64_t cSize, uint32_t cAlignment,
                            uint64_t stringCount, DenseI64ArrayAttr leaves) {
  if (cSize == 0 || cAlignment == 0 || (cAlignment & (cAlignment - 1)) != 0)
    return emitError() << "requires nonzero power-of-two C layout";
  ArrayRef<int64_t> words = leaves.asArrayRef();
  if (words.empty() || words.size() % 8 != 0)
    return emitError() << "requires complete eight-word layout records";
  for (size_t index = 0; index != words.size(); index += 8) {
    if (words[index] == 0) {
      if (words[index + 1] < 0 || words[index + 2] < 0 ||
          words[index + 3] < 0 ||
          words[index + 3] > static_cast<int64_t>(DPIABIKind::Real) ||
          words[index + 4] <= 0 || words[index + 4] > UINT32_MAX ||
          (words[index + 5] != 0 && words[index + 5] != 1) ||
          (words[index + 6] != 0 && words[index + 6] != 1) ||
          words[index + 7] != 0)
        return emitError() << "has malformed leaf record " << index / 8;
    } else if (words[index] == 1) {
      if (words[index + 1] < 0 || words[index + 2] < 0 ||
          words[index + 3] <= 0 || words[index + 4] == 0 ||
          words[index + 5] <= 0 || words[index + 6] != 0 ||
          words[index + 7] <= 0)
        return emitError() << "has malformed repeat record " << index / 8;
    } else {
      return emitError() << "has unknown layout opcode " << words[index];
    }
  }
  if (stringCount > (UINT64_MAX - cSize) / 8)
    return emitError() << "has an unrepresentable string scratch layout";
  return success();
}

uint64_t getDPISignatureHash(ArrayAttr signature, uint64_t logicalInputs) {
  uint64_t hash = OBELISK_STABLE_HASH_OFFSET_BASIS;
  hash = obelisk_stable_hash_append_uint_le(hash, logicalInputs, 8);
  hash = obelisk_stable_hash_append_uint_le(
      hash, signature.size() - logicalInputs, 8);
  for (Attribute attribute : signature) {
    auto abi = cast<DPIABIAttr>(attribute);
    hash = obelisk_stable_hash_append_uint_le(
        hash, static_cast<uint32_t>(abi.getKind()), 4);
    hash = obelisk_stable_hash_append_uint_le(
        hash, static_cast<uint32_t>(abi.getDirection()), 4);
    hash = obelisk_stable_hash_append_uint_le(hash, abi.getWidth(), 4);
    hash =
        obelisk_stable_hash_append_uint_le(hash, abi.getFourState() ? 1 : 0, 1);
    hash =
        obelisk_stable_hash_append_uint_le(hash, abi.getIsSigned() ? 1 : 0, 1);
  }
  return hash ? hash : 1;
}

LogicalResult
verifyVPITypeSemantics(llvm::function_ref<InFlightDiagnostic()> emitError,
                       Type executableType, VPITypeSemanticsAttr semantics) {
  auto rangeWidth = [](DenseI64ArrayAttr range) -> std::optional<uint64_t> {
    if (!range || range.size() != 2)
      return std::nullopt;
    APInt left(65, static_cast<uint64_t>(range[0]), true);
    APInt right(65, static_cast<uint64_t>(range[1]), true);
    APInt distance = left.sge(right) ? left - right : right - left;
    if (distance.getActiveBits() > 64 || distance.isAllOnes())
      return std::nullopt;
    return distance.getZExtValue() + 1;
  };
  std::function<std::optional<uint64_t>(VPITypeSemanticsAttr)>
      semanticPackedWidth =
          [&](VPITypeSemanticsAttr semantic) -> std::optional<uint64_t> {
    switch (semantic.getKind()) {
    case VPITypeKind::Enum:
      return semanticPackedWidth(
          cast<VPITypeSemanticsAttr>(semantic.getChildren()[0]));
    case VPITypeKind::PackedArray: {
      std::optional<uint64_t> extent = rangeWidth(semantic.getRange());
      std::optional<uint64_t> element = semanticPackedWidth(
          cast<VPITypeSemanticsAttr>(semantic.getChildren()[0]));
      if (!extent || !element || *extent == 0 || *element == 0 ||
          *extent > std::numeric_limits<uint64_t>::max() / *element)
        return std::nullopt;
      return *extent * *element;
    }
    case VPITypeKind::PackedStruct:
    case VPITypeKind::PackedUnion:
      return semantic.getBitWidth().getValue().getZExtValue();
    default:
      return rangeWidth(semantic.getRange());
    }
  };
  auto scalarMatches = [&](Type type, VPITypeSemanticsAttr semantic) {
    std::optional<uint64_t> width = rangeWidth(semantic.getRange());
    std::optional<unsigned> executableWidth = getPackedWidth(type);
    if (!width || !executableWidth || *width != *executableWidth)
      return false;
    return semantic.getIsFourState() ? isa<LogicType>(type)
                                     : isa<IntegerType>(type);
  };

  std::function<bool(Type, VPITypeSemanticsAttr)> matches =
      [&](Type type, VPITypeSemanticsAttr semantic) -> bool {
    VPITypeKind kind = semantic.getKind();
    switch (kind) {
    case VPITypeKind::Unknown:
      return false;
    case VPITypeKind::GenericIntegral:
    case VPITypeKind::Bit:
    case VPITypeKind::Logic:
    case VPITypeKind::Reg:
    case VPITypeKind::Byte:
    case VPITypeKind::ShortInt:
    case VPITypeKind::Int:
    case VPITypeKind::LongInt:
    case VPITypeKind::Integer:
    case VPITypeKind::Time:
      return scalarMatches(type, semantic);
    case VPITypeKind::Enum: {
      auto base = cast<VPITypeSemanticsAttr>(semantic.getChildren()[0]);
      if (matches(type, base))
        return true;
      // Enum declarations retain their exact packed base shape for VPI, but
      // executable storage deliberately canonicalizes that shape to one
      // integer or logic scalar. Verify the canonical representation by total
      // packed width and state domain without discarding the VPI shape.
      std::optional<uint64_t> sourceWidth = semanticPackedWidth(base);
      std::optional<unsigned> executableWidth = getPackedWidth(type);
      return sourceWidth && executableWidth &&
             isa<IntegerType, LogicType>(type) &&
             *sourceWidth == *executableWidth &&
             base.getIsFourState() == isa<LogicType>(type);
    }
    case VPITypeKind::ShortReal:
      return type.isF32();
    case VPITypeKind::Real:
    case VPITypeKind::Realtime:
      return type.isF64();
    case VPITypeKind::PackedArray:
    case VPITypeKind::UnpackedArray: {
      auto child = cast<VPITypeSemanticsAttr>(semantic.getChildren()[0]);
      if (kind == VPITypeKind::PackedArray) {
        auto array = dyn_cast<PackedArrayType>(type);
        return array && array.getLeft() == semantic.getRange()[0] &&
               array.getRight() == semantic.getRange()[1] &&
               matches(array.getElementType(), child);
      }
      auto array = dyn_cast<UnpackedArrayType>(type);
      return array && array.getLeft() == semantic.getRange()[0] &&
             array.getRight() == semantic.getRange()[1] &&
             matches(array.getElementType(), child);
    }
    case VPITypeKind::PackedStruct:
    case VPITypeKind::UnpackedStruct:
    case VPITypeKind::PackedUnion:
    case VPITypeKind::UnpackedUnion: {
      ArrayAttr fields;
      bool executableTagged = false;
      uint64_t executableTagBits = 0;
      if (auto value = dyn_cast<PackedStructType>(type))
        fields =
            kind == VPITypeKind::PackedStruct ? value.getFields() : ArrayAttr{};
      else if (auto value = dyn_cast<UnpackedStructType>(type))
        fields = kind == VPITypeKind::UnpackedStruct ? value.getFields()
                                                     : ArrayAttr{};
      else if (auto value = dyn_cast<PackedUnionType>(type)) {
        if (kind == VPITypeKind::PackedUnion) {
          fields = value.getFields();
          executableTagged = value.getIsTagged();
          executableTagBits = value.getTagBits();
        }
      } else if (auto value = dyn_cast<UnpackedUnionType>(type)) {
        if (kind == VPITypeKind::UnpackedUnion) {
          fields = value.getFields();
          executableTagged = value.getIsTagged();
        }
      }
      if (!fields || fields.size() != semantic.getChildren().size() ||
          executableTagged != semantic.getIsTagged().getValue())
        return false;
      if (kind == VPITypeKind::PackedUnion &&
          executableTagBits != semantic.getTagBits().getValue().getZExtValue())
        return false;
      if (kind == VPITypeKind::PackedStruct ||
          kind == VPITypeKind::PackedUnion) {
        std::optional<unsigned> width = getPackedWidth(type);
        if (!width ||
            semantic.getBitWidth().getValue().getZExtValue() != *width)
          return false;
      } else if (!semantic.getBitWidth().getValue().isZero()) {
        return false;
      }
      for (auto [index, fieldAttr] : llvm::enumerate(fields)) {
        auto field = cast<FieldAttr>(fieldAttr);
        auto child = cast<VPITypeSemanticsAttr>(semantic.getChildren()[index]);
        auto childName = cast<StringAttr>(semantic.getChildNames()[index]);
        if (field.getName() != childName ||
            field.getOrdinal() != semantic.getChildOrdinals()[index] ||
            field.getPackedOffset() !=
                static_cast<uint64_t>(
                    semantic.getChildPackedOffsets()[index]) ||
            !matches(field.getType(), child))
          return false;
      }
      return true;
    }
    case VPITypeKind::DynamicArray: {
      auto value = dyn_cast<DynamicArrayType>(type);
      return value &&
             matches(value.getElementType(),
                     cast<VPITypeSemanticsAttr>(semantic.getChildren()[0]));
    }
    case VPITypeKind::Queue: {
      auto value = dyn_cast<QueueType>(type);
      return value &&
             value.getBound() ==
                 semantic.getQueueBound().getValue().getZExtValue() &&
             matches(value.getElementType(),
                     cast<VPITypeSemanticsAttr>(semantic.getChildren()[0]));
    }
    case VPITypeKind::AssocArray: {
      auto value = dyn_cast<AssocArrayType>(type);
      if (!value ||
          value.getWildcardIndex() != semantic.getWildcardIndex().getValue())
        return false;
      auto key = cast<VPITypeSemanticsAttr>(semantic.getChildren()[0]);
      auto element = cast<VPITypeSemanticsAttr>(semantic.getChildren()[1]);
      bool keyMatches = matches(value.getKeyType(), key);
      // Associative integral keys deliberately normalize a packed declaration
      // shape to its scalar lookup domain.
      if (!keyMatches && getPackedWidth(value.getKeyType())) {
        std::optional<uint64_t> sourceWidth = semanticPackedWidth(key);
        keyMatches =
            sourceWidth &&
            *sourceWidth == *getPackedWidth(value.getKeyType()) &&
            (key.getIsFourState() == isa<LogicType>(value.getKeyType()));
      }
      return keyMatches && matches(value.getElementType(), element);
    }
    case VPITypeKind::Class: {
      auto value = dyn_cast<ClassHandleType>(type);
      return value && value.getClassName() == semantic.getSymbol();
    }
    case VPITypeKind::VirtualInterface: {
      auto value = dyn_cast<VirtualInterfaceType>(type);
      if (!value || value.getModport() != semantic.getModport())
        return false;
      if (semantic.getName())
        return value.getInterfaceName() == semantic.getName();
      std::string identity;
      llvm::raw_string_ostream stream(identity);
      stream << semantic.getSymbol();
      return value.getInterfaceName().getValue() == identity;
    }
    case VPITypeKind::Event:
      return isa<EventType>(type);
    case VPITypeKind::Process:
      return isa<ProcessType>(type);
    case VPITypeKind::Covergroup: {
      auto value = dyn_cast<CovergroupHandleType>(type);
      return value && value.getCovergroupName() == semantic.getSymbol();
    }
    case VPITypeKind::Mailbox: {
      auto value = dyn_cast<MailboxType>(type);
      return value &&
             matches(value.getElementType(),
                     cast<VPITypeSemanticsAttr>(semantic.getChildren()[0]));
    }
    case VPITypeKind::Semaphore:
      return isa<SemaphoreType>(type);
    case VPITypeKind::PackedOpenArray:
    case VPITypeKind::UnpackedOpenArray: {
      auto value = dyn_cast<DPIOpenArrayType>(type);
      return value &&
             value.getIsPacked() == (kind == VPITypeKind::PackedOpenArray) &&
             matches(value.getElementType(),
                     cast<VPITypeSemanticsAttr>(semantic.getChildren()[0]));
    }
    case VPITypeKind::String:
      return isa<StringType>(type);
    case VPITypeKind::Chandle:
      return isa<ChandleType>(type);
    case VPITypeKind::Void:
      return isa<IntegerType>(type) && cast<IntegerType>(type).getWidth() == 1;
    case VPITypeKind::Untyped:
      return isa<BoxType>(type);
    case VPITypeKind::Sequence:
    case VPITypeKind::Property:
      return false;
    }
    llvm_unreachable("unhandled VPI semantic type kind");
  };

  if (!matches(executableType, semantics))
    return emitError() << "VPI semantic type " << semantics
                       << " does not normalize to executable type "
                       << executableType;
  return success();
}

std::optional<unsigned> getPackedWidth(Type type) {
  if (auto integer = dyn_cast<IntegerType>(type))
    return integer.isSignless() ? std::optional<unsigned>(integer.getWidth())
                                : std::nullopt;
  if (auto logic = dyn_cast<LogicType>(type))
    return logic.getWidth();
  auto checkedProduct = [](uint64_t lhs,
                           uint64_t rhs) -> std::optional<unsigned> {
    if (lhs && rhs > std::numeric_limits<unsigned>::max() / lhs)
      return std::nullopt;
    uint64_t result = lhs * rhs;
    if (result == 0 || result > std::numeric_limits<unsigned>::max())
      return std::nullopt;
    return static_cast<unsigned>(result);
  };
  if (auto array = dyn_cast<PackedArrayType>(type)) {
    std::optional<unsigned> element = getPackedWidth(array.getElementType());
    std::optional<unsigned> count =
        getArrayElementOrdinal(array, array.getRight());
    if (!element || !count)
      return std::nullopt;
    return checkedProduct(static_cast<uint64_t>(*count) + 1, *element);
  }
  auto aggregateWidth = [&](ArrayAttr fields) -> std::optional<unsigned> {
    uint64_t width = 0;
    for (Attribute attribute : fields) {
      auto field = dyn_cast<FieldAttr>(attribute);
      std::optional<unsigned> fieldWidth =
          field ? getPackedWidth(field.getType()) : std::nullopt;
      if (!fieldWidth || field.getPackedOffset() >
                             std::numeric_limits<unsigned>::max() - *fieldWidth)
        return std::nullopt;
      width = std::max<uint64_t>(width, field.getPackedOffset() +
                                            static_cast<uint64_t>(*fieldWidth));
    }
    if (width == 0 || width > std::numeric_limits<unsigned>::max())
      return std::nullopt;
    return static_cast<unsigned>(width);
  };
  if (auto structure = dyn_cast<PackedStructType>(type))
    return aggregateWidth(structure.getFields());
  if (auto unionType = dyn_cast<PackedUnionType>(type)) {
    std::optional<unsigned> payload = aggregateWidth(unionType.getFields());
    if (!payload || unionType.getTagBits() >
                        std::numeric_limits<unsigned>::max() - *payload)
      return std::nullopt;
    return *payload + unionType.getTagBits();
  }
  return std::nullopt;
}

} // namespace obelisk::sim
