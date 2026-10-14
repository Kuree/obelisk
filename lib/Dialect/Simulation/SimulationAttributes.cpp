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
StorageLayoutAttr::verify(llvm::function_ref<InFlightDiagnostic()> emitError,
                          uint64_t size, uint64_t alignment, uint64_t stride,
                          DenseI64ArrayAttr offsets) {
  if (!size || size > UINT64_MAX / 8 || !llvm::isPowerOf2_64(alignment) ||
      alignment > size || size % alignment || !offsets ||
      (stride && (stride > size || size % stride || !offsets.empty())))
    return emitError() << "invalid aggregate storage size, alignment or stride";
  for (int64_t offset : offsets.asArrayRef())
    if (offset < 0 || uint64_t(offset) >= size)
      return emitError() << "record field offset exceeds its storage size";
  return success();
}

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
IntegerRangeAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                         IntegerAttr umin, IntegerAttr umax, IntegerAttr smin,
                         IntegerAttr smax) {
  Type type = umin.getType();
  if (!isa<IntegerType>(type) || umax.getType() != type ||
      smin.getType() != type || smax.getType() != type)
    return emitError() << "integer range bounds must share one integer type";
  if (umin.getValue().ugt(umax.getValue()) ||
      smin.getValue().sgt(smax.getValue()))
    return emitError() << "integer range is empty";
  return success();
}

LogicalResult
IntegerExpressionAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                              IntegerExpressionKind kind, uint32_t width,
                              IntegerAttr value, std::optional<uint32_t> index,
                              ArrayAttr operands) {
  if (width == 0 || width > 128)
    return emitError() << "integer expression width must be 1 to 128 bits";
  switch (kind) {
  case IntegerExpressionKind::Constant:
    if (!value || value.getValue().getBitWidth() != width || index || operands)
      return emitError() << "constant integer expression needs only a "
                            "value of its width";
    return success();
  case IntegerExpressionKind::Argument:
    if (!index || value || operands)
      return emitError() << "argument integer expression needs only an index";
    return success();
  default:
    break;
  }
  bool unary = kind == IntegerExpressionKind::Identity ||
               kind == IntegerExpressionKind::SExt ||
               kind == IntegerExpressionKind::ZExt ||
               kind == IntegerExpressionKind::Trunc;
  if (value || index || !operands || operands.size() != (unary ? 1u : 2u) ||
      !llvm::all_of(operands, llvm::IsaPred<IntegerExpressionAttr>))
    return emitError() << "integer expression has malformed operands";
  return success();
}

/// rand_mode() controls at most 64 effective properties of one class.
static constexpr uint32_t kRandomModeBits = 64;

LogicalResult RandomObjectStepAttr::verify(
    function_ref<InFlightDiagnostic()> emitError, FlatSymbolRefAttr,
    ClassHandleType, ClassHandleType, std::optional<uint32_t> modeIndex) {
  if (modeIndex && *modeIndex >= kRandomModeBits)
    return emitError() << "random object step mode index is out of range";
  return success();
}

LogicalResult RandomNestedObjectAttr::verify(
    function_ref<InFlightDiagnostic()> emitError, FlatSymbolRefAttr,
    ClassHandleType, ClassHandleType, uint32_t outerModeIndex, ArrayAttr path) {
  if (outerModeIndex >= kRandomModeBits)
    return emitError() << "nested random object mode index is out of range";
  if (path && (path.empty() || !llvm::all_of(path, [](Attribute step) {
                 auto typed = dyn_cast<RandomObjectStepAttr>(step);
                 return typed && typed.getConcrete() && typed.getModeIndex();
               })))
    return emitError() << "nested random object path must contain "
                          "#simulation.random_object_step entries with a "
                          "concrete class and mode index";
  return success();
}

LogicalResult RandomizeHookAttr::verify(
    function_ref<InFlightDiagnostic()> emitError, FlatSymbolRefAttr,
    FlatSymbolRefAttr, FlatSymbolRefAttr, ArrayAttr captures, ArrayAttr reads) {
  for (ArrayAttr paths : {captures, reads})
    if (paths && !llvm::all_of(paths, llvm::IsaPred<StringAttr>))
      return emitError() << "randomize hook captures must be string paths";
  return success();
}

LogicalResult
RandomAliasGuardAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                             RandomNestedObjectAttr object,
                             uint32_t aliasDepth) {
  if (!object.getPath() || aliasDepth >= object.getPath().size())
    return emitError() << "random alias guard depth must index its path";
  return success();
}

LogicalResult
RandomDomainAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                         uint64_t offset, uint64_t width, ArrayAttr patterns) {
  if (width == 0 || patterns.empty() ||
      !llvm::all_of(patterns, llvm::IsaPred<RandomPatternAttr>))
    return emitError() << "random domain needs a width and "
                          "#simulation.random_pattern entries";
  return success();
}

LogicalResult
RandomCycleStateAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                             Attribute key, Attribute position,
                             Attribute domain) {
  bool fields = llvm::all_of(ArrayRef<Attribute>{key, position, domain},
                             llvm::IsaPred<FlatSymbolRefAttr>);
  bool paths = llvm::all_of(ArrayRef<Attribute>{key, position, domain},
                            llvm::IsaPred<StringAttr>);
  if (!fields && !paths)
    return emitError() << "randc state must name three class fields or three "
                          "storage paths";
  return success();
}

LogicalResult
RandomMemberStorageAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                                DenseI64ArrayAttr path, Type, uint64_t,
                                uint64_t) {
  if (path.empty() || llvm::any_of(path.asArrayRef(),
                                   [](int64_t ordinal) { return ordinal < 0; }))
    return emitError() << "random member storage path is malformed";
  return success();
}

/// Checks the per-element rand_mode() state of a fixed-array random variable
/// whose own storage is a class field when `instance` is set.
static LogicalResult
verifyElementModes(function_ref<InFlightDiagnostic()> emitError, bool instance,
                   FlatSymbolRefAttr field, std::optional<uint64_t> storage) {
  if (field && storage)
    return emitError() << "random element modes need one state location";
  if ((field && !instance) || (storage && instance))
    return emitError()
           << "random element modes must live beside their variable";
  return success();
}

LogicalResult RandomPropertyAttr::verify(
    function_ref<InFlightDiagnostic()> emitError, FlatSymbolRefAttr field,
    StringAttr path, SymbolRefAttr, Type, uint64_t width, uint32_t modeIndex,
    bool, bool isRandC, std::optional<uint64_t> modeStorage,
    RandomContainerSizeAttr containerSize, RandomNestedObjectAttr nested,
    RandomMemberStorageAttr member, FlatSymbolRefAttr elementModeField,
    std::optional<uint64_t> elementModeStorage,
    std::optional<uint32_t> elementWidth, RandomCycleStateAttr cycle,
    ArrayAttr domains) {
  if (static_cast<bool>(field) == static_cast<bool>(path))
    return emitError() << "random property must name exactly one field or path";
  if (width == 0 || modeIndex >= kRandomModeBits)
    return emitError() << "random property width or mode index is invalid";
  if ((containerSize || nested) && !field)
    return emitError() << "random container sizes and nested properties are "
                          "class fields";
  if (member && (containerSize || nested))
    return emitError() << "random structure members are direct properties";
  if (modeStorage && field)
    return emitError() << "only static random properties keep rand_mode() "
                          "storage";
  bool elementModes = elementModeField || elementModeStorage;
  if (elementModes != elementWidth.has_value() ||
      (elementModes && (containerSize || nested)))
    return emitError() << "random element modes are malformed";
  if (failed(verifyElementModes(emitError, static_cast<bool>(field),
                                elementModeField, elementModeStorage)))
    return failure();
  if (cycle &&
      (!isRandC || cycle.isInstanceState() != static_cast<bool>(field)))
    return emitError() << "randc state must match its variable";
  if (domains && !llvm::all_of(domains, llvm::IsaPred<RandomDomainAttr>))
    return emitError() << "random property domains must be "
                          "#simulation.random_domain entries";
  return success();
}

RandomPropertyAttr RandomPropertyAttr::withPath(StringAttr path) const {
  return get(getContext(), getField(), path, getSymbol(), getType(), getWidth(),
             getModeIndex(), getIsSigned(), getIsRandC(), getModeStorage(),
             getContainerSize(), getNested(), getMember(),
             getElementModeField(), getElementModeStorage(), getElementWidth(),
             getCycle(), getDomains());
}

LogicalResult
RandomContainerAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                            FlatSymbolRefAttr, Type, Type,
                            uint32_t elementWidth, uint32_t modeIndex,
                            bool inertClassHandles, RandomNestedObjectAttr) {
  if (modeIndex >= kRandomModeBits || elementWidth > 64 ||
      (elementWidth == 0 && !inertClassHandles))
    return emitError() << "random container element width or mode index is "
                          "invalid";
  return success();
}

LogicalResult
RandomStructMemberAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                               DenseI64ArrayAttr path, Type, Type, bool isRandC,
                               uint32_t modeIndex, RandomCycleStateAttr cycle,
                               FlatSymbolRefAttr elementModeField,
                               std::optional<uint64_t> elementModeStorage) {
  if (path.empty() || modeIndex >= kRandomModeBits)
    return emitError() << "random structure member path or mode index is "
                          "invalid";
  if (cycle && !isRandC)
    return emitError() << "only randc structure members keep randc state";
  if (elementModeField && elementModeStorage)
    return emitError() << "random element modes need one state location";
  return success();
}

LogicalResult VirtualInterfaceCalleeAttr::verify(
    function_ref<InFlightDiagnostic()> emitError, uint64_t, FlatSymbolRefAttr,
    ArrayAttr captures, ArrayAttr reads, ArrayAttr writes) {
  for (ArrayAttr paths : {captures, reads, writes})
    if (!llvm::all_of(paths, llvm::IsaPred<StringAttr>))
      return emitError()
             << "virtual interface callee captures must be string paths";
  return success();
}

LogicalResult TimingPathConditionAttr::verify(
    function_ref<InFlightDiagnostic()> emitError, TimingPathConditionKind kind,
    uint32_t, std::optional<uint64_t>, FlatSymbolRefAttr evaluator,
    ArrayAttr captures) {
  if (static_cast<bool>(evaluator) != static_cast<bool>(captures) ||
      (evaluator && kind != TimingPathConditionKind::If) ||
      (captures && !llvm::all_of(captures, llvm::IsaPred<StringAttr>)))
    return emitError() << "timing path condition evaluator is malformed";
  return success();
}

LogicalResult TimingPathSelectionAttr::verify(
    function_ref<InFlightDiagnostic()> emitError, DenseI64ArrayAttr inputLows,
    DenseI64ArrayAttr inputWidths, DenseI64ArrayAttr inputLsbs,
    uint64_t outputLow, uint64_t outputWidth, uint64_t outputRootWidth, bool,
    std::optional<uint64_t>) {
  if (inputLows.size() != inputWidths.size() ||
      (inputLsbs && inputLsbs.size() != inputLows.size()) ||
      llvm::any_of(inputLows.asArrayRef(),
                   [](int64_t low) { return low < 0; }) ||
      llvm::any_of(inputWidths.asArrayRef(),
                   [](int64_t width) { return width <= 0; }) ||
      (inputLsbs && llvm::any_of(inputLsbs.asArrayRef(),
                                 [](int64_t lsb) { return lsb < 0; })))
    return emitError() << "timing path source selection is malformed";
  if (outputWidth == 0 || outputLow >= outputRootWidth ||
      outputWidth > outputRootWidth - outputLow)
    return emitError() << "timing path destination is out of bounds";
  return success();
}

LogicalResult
TimingPathEdgeAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                           uint32_t identifier, uint32_t polarity, StringAttr,
                           StringAttr) {
  if (identifier > 3 || polarity > 2)
    return emitError() << "timing path edge is malformed";
  return success();
}

LogicalResult
TimingPathProceduralAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                                 uint64_t, uint32_t wakeKind, bool) {
  if (wakeKind == 0 || wakeKind > 3)
    return emitError() << "procedural timing path wake kind is malformed";
  return success();
}

/// Checks the source terminals of a module path and their selection.
static LogicalResult
verifyTimingPathTerminals(function_ref<InFlightDiagnostic()> emitError,
                          ArrayAttr inputs, ArrayAttr snapshots,
                          TimingPathSelectionAttr selection,
                          TimingPathEdgeAttr edge) {
  if (inputs.empty() || inputs.size() != snapshots.size() ||
      !llvm::all_of(inputs, llvm::IsaPred<StringAttr>) ||
      !llvm::all_of(snapshots, llvm::IsaPred<StringAttr>))
    return emitError() << "timing path terminals are malformed";
  if (selection &&
      static_cast<size_t>(selection.getInputLows().size()) != inputs.size())
    return emitError() << "timing path selection does not match its terminals";
  if (edge && (!selection || !selection.getInputLsbs()))
    return emitError() << "edge-sensitive timing path needs source LSBs";
  return success();
}

LogicalResult TimingPathRuleAttr::verify(
    function_ref<InFlightDiagnostic()> emitError, ArrayAttr inputs,
    ArrayAttr snapshots, uint32_t polarity, DenseI64ArrayAttr delays,
    DenseI64ArrayAttr pulseReject, DenseI64ArrayAttr pulseError, bool, bool,
    TimingPathSelectionAttr selection, TimingPathEdgeAttr edge,
    TimingPathProceduralAttr, TimingPathConditionAttr) {
  if (failed(verifyTimingPathTerminals(emitError, inputs, snapshots, selection,
                                       edge)))
    return failure();
  size_t count = delays.size();
  if (polarity > 2 ||
      (count != 1 && count != 2 && count != 3 && count != 6 && count != 12) ||
      llvm::any_of(delays.asArrayRef(),
                   [](int64_t delay) { return delay < 0; }))
    return emitError() << "timing path delays are malformed";
  if (static_cast<bool>(pulseReject) != static_cast<bool>(pulseError) ||
      (pulseReject && (static_cast<size_t>(pulseReject.size()) != count ||
                       static_cast<size_t>(pulseError.size()) != count)))
    return emitError() << "timing path pulse limits are malformed";
  return success();
}

TimingPathRuleAttr
TimingPathRuleAttr::withCondition(TimingPathConditionAttr condition) const {
  return get(getContext(), getInputs(), getSnapshots(), getPolarity(),
             getDelays(), getPulseReject(), getPulseError(), getPulseOnDetect(),
             getPulseShowCancelled(), getSelection(), getEdge(),
             getProcedural(), condition);
}

TimingPathRuleAttr
TimingPathRuleAttr::withDelays(DenseI64ArrayAttr delays,
                               DenseI64ArrayAttr pulseReject,
                               DenseI64ArrayAttr pulseError) const {
  return get(getContext(), getInputs(), getSnapshots(), getPolarity(), delays,
             pulseReject, pulseError, getPulseOnDetect(),
             getPulseShowCancelled(), getSelection(), getEdge(),
             getProcedural(), getCondition());
}

TimingPathMonitorAttr
TimingPathMonitorAttr::withCondition(TimingPathConditionAttr condition) const {
  return get(getContext(), getInputs(), getSnapshots(), getSelection(),
             getEdge(), condition);
}

LogicalResult TimingPathMonitorAttr::verify(
    function_ref<InFlightDiagnostic()> emitError, ArrayAttr inputs,
    ArrayAttr snapshots, TimingPathSelectionAttr selection,
    TimingPathEdgeAttr edge, TimingPathConditionAttr) {
  return verifyTimingPathTerminals(emitError, inputs, snapshots, selection,
                                   edge);
}

LogicalResult
RandomElementModesAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                               uint32_t width, FlatSymbolRefAttr field,
                               std::optional<uint64_t> storage) {
  if (width == 0 || static_cast<bool>(field) == storage.has_value() ||
      (storage && *storage > INT64_MAX))
    return emitError() << "random element modes need a width and one state "
                          "location";
  return success();
}

LogicalResult
RandModeDispatchAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                             FlatSymbolRefAttr, DenseI64ArrayAttr storages,
                             ArrayAttr elements) {
  if (llvm::any_of(storages.asArrayRef(),
                   [](int64_t storage) { return storage < 0; }) ||
      !llvm::all_of(elements, llvm::IsaPred<RandomElementModesAttr>))
    return emitError() << "rand_mode dispatch is malformed";
  return success();
}

LogicalResult UdpRowAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                                 StringAttr, StringAttr state,
                                 StringAttr output, bool) {
  if (state.size() > 1 || output.size() != 1)
    return emitError() << "UDP table symbols must be single characters";
  return success();
}

LogicalResult UdpAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                              StringAttr, ArrayAttr ports,
                              DenseI64ArrayAttr portDirections, bool, bool,
                              StringAttr, ArrayAttr rows) {
  if (static_cast<int64_t>(ports.size()) != portDirections.size() ||
      !llvm::all_of(ports, llvm::IsaPred<StringAttr>) ||
      !llvm::all_of(rows, llvm::IsaPred<UdpRowAttr>))
    return emitError() << "UDP declaration is malformed";
  return success();
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
