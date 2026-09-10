//===- SimulationVPI.cpp - Simulation VPI classification ----------------===//

#include "obelisk/Dialect/Simulation/SimulationVPI.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Reflection/VPIObjectModel.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/ErrorHandling.h"

namespace obelisk::sim {
namespace {

VPITypeSemanticsAttr packedArrayElement(VPITypeSemanticsAttr type) {
  while (type && type.getKind() == VPITypeKind::PackedArray) {
    if (type.getChildren().size() != 1)
      return {};
    type = mlir::dyn_cast<VPITypeSemanticsAttr>(type.getChildren()[0]);
  }
  return type;
}

bool equivalentRefType(VPITypeSemanticsAttr left, VPITypeSemanticsAttr right) {
  if (!left || !right || left.getKind() != right.getKind() ||
      left.getIsSigned() != right.getIsSigned() ||
      left.getIsFourState() != right.getIsFourState() ||
      left.getName() != right.getName() ||
      left.getSymbol() != right.getSymbol() ||
      left.getModport() != right.getModport() ||
      left.getRange() != right.getRange() ||
      left.getChildNames() != right.getChildNames() ||
      left.getIsTagged() != right.getIsTagged() ||
      left.getIsSoft() != right.getIsSoft() ||
      left.getBitWidth() != right.getBitWidth() ||
      left.getSelectableWidth() != right.getSelectableWidth() ||
      left.getBitstreamWidth() != right.getBitstreamWidth() ||
      left.getTagBits() != right.getTagBits() ||
      left.getQueueBound() != right.getQueueBound() ||
      left.getWildcardIndex() != right.getWildcardIndex() ||
      left.getChildOrdinals() != right.getChildOrdinals() ||
      left.getChildPackedOffsets() != right.getChildPackedOffsets() ||
      left.getChildRandTypes() != right.getChildRandTypes() ||
      left.getChildren().size() != right.getChildren().size())
    return false;
  for (auto [leftChild, rightChild] :
       llvm::zip_equal(left.getChildren(), right.getChildren()))
    if (!equivalentRefType(mlir::cast<VPITypeSemanticsAttr>(leftChild),
                           mlir::cast<VPITypeSemanticsAttr>(rightChild)))
      return false;
  return true;
}

} // namespace

bool isVPIVisibleCodeUnit(SimCodeUnitDeclOp codeUnit) {
  // A DPI import declaration is represented by a source-inventory anchor.
  // Its code unit is only the executable ABI thunk and must not become a
  // second VPI function/task with the same hierarchical name.
  return !codeUnit.getInternalAttr() &&
         !codeUnit->hasAttr("obelisk_sim.dpi_import") &&
         isVPIVisibleEntryKind(codeUnit.getCodeUnitKind());
}

bool areEquivalentVPIRefTypes(VPITypeSemanticsAttr left,
                              VPITypeSemanticsAttr right) {
  return equivalentRefType(left, right);
}

uint32_t vpiKindForCodeUnit(SimCodeUnitDeclOp codeUnit) {
  if (!isVPIVisibleCodeUnit(codeUnit))
    return 0;
  using VPIKind = reflection::VPIObjectKind;
  switch (codeUnit.getCodeUnitKind()) {
  case EntryKind::Initial:
    return static_cast<uint16_t>(VPIKind::Initial);
  case EntryKind::Final:
    return static_cast<uint16_t>(VPIKind::Final);
  case EntryKind::Always:
  case EntryKind::AlwaysComb:
  case EntryKind::AlwaysFF:
  case EntryKind::AlwaysLatch:
    return static_cast<uint16_t>(VPIKind::Always);
  case EntryKind::Function:
    return static_cast<uint16_t>(VPIKind::Function);
  case EntryKind::Task:
    return static_cast<uint16_t>(VPIKind::Task);
  default:
    return 0;
  }
}

uint32_t vpiKindForScope(SimScopeDeclOp scope) {
  return scope.getVpiKind().value_or(
      scope.getInterfaceType()
          ? static_cast<uint16_t>(reflection::VPIObjectKind::Interface)
          : (scope.getId() == 0
                 ? 0
                 : static_cast<uint16_t>(reflection::VPIObjectKind::Module)));
}

uint32_t vpiKindForStorage(VPITypeSemanticsAttr type) {
  using Kind = VPITypeKind;
  using VPIKind = reflection::VPIObjectKind;
  if (!type)
    return static_cast<uint16_t>(VPIKind::Reg);
  switch (type.getKind()) {
  case Kind::Unknown:
    return static_cast<uint16_t>(VPIKind::Reg);
  case Kind::GenericIntegral:
    return static_cast<uint16_t>(type.getIsFourState() ? VPIKind::LogicVar
                                                       : VPIKind::BitVar);
  case Kind::Bit:
    return static_cast<uint16_t>(VPIKind::BitVar);
  case Kind::Logic:
    return static_cast<uint16_t>(VPIKind::LogicVar);
  case Kind::Reg:
    return static_cast<uint16_t>(VPIKind::Reg);
  case Kind::Byte:
    return static_cast<uint16_t>(VPIKind::ByteVar);
  case Kind::ShortInt:
    return static_cast<uint16_t>(VPIKind::ShortIntVar);
  case Kind::Int:
    return static_cast<uint16_t>(VPIKind::IntVar);
  case Kind::LongInt:
    return static_cast<uint16_t>(VPIKind::LongIntVar);
  case Kind::Integer:
    return static_cast<uint16_t>(VPIKind::IntegerVar);
  case Kind::Enum:
    return static_cast<uint16_t>(VPIKind::EnumVar);
  case Kind::Time:
    return static_cast<uint16_t>(VPIKind::TimeVar);
  case Kind::ShortReal:
    return static_cast<uint16_t>(VPIKind::ShortRealVar);
  case Kind::Real:
  case Kind::Realtime:
    return static_cast<uint16_t>(VPIKind::RealVar);
  case Kind::String:
    return static_cast<uint16_t>(VPIKind::StringVar);
  case Kind::Chandle:
    return static_cast<uint16_t>(VPIKind::ChandleVar);
  case Kind::PackedArray: {
    VPITypeSemanticsAttr element = packedArrayElement(type);
    if (!element)
      return static_cast<uint16_t>(VPIKind::Reg);
    switch (element.getKind()) {
    case Kind::Enum:
    case Kind::PackedStruct:
    case Kind::PackedUnion:
      return static_cast<uint16_t>(VPIKind::PackedArrayVar);
    default:
      return vpiKindForStorage(element);
    }
  }
  case Kind::UnpackedArray:
  case Kind::DynamicArray:
  case Kind::Queue:
  case Kind::AssocArray:
  case Kind::PackedOpenArray:
  case Kind::UnpackedOpenArray:
    return static_cast<uint16_t>(VPIKind::ArrayVar);
  case Kind::PackedStruct:
  case Kind::UnpackedStruct:
    return static_cast<uint16_t>(VPIKind::StructVar);
  case Kind::PackedUnion:
  case Kind::UnpackedUnion:
    return static_cast<uint16_t>(VPIKind::UnionVar);
  case Kind::Class:
  case Kind::Process:
  case Kind::Covergroup:
  case Kind::Mailbox:
  case Kind::Semaphore:
    return static_cast<uint16_t>(VPIKind::ClassVar);
  case Kind::VirtualInterface:
    return static_cast<uint16_t>(VPIKind::VirtualInterfaceVar);
  case Kind::Event:
    return static_cast<uint16_t>(VPIKind::NamedEvent);
  case Kind::Void:
  case Kind::Untyped:
  case Kind::Sequence:
  case Kind::Property:
    return static_cast<uint16_t>(VPIKind::Reg);
  }
  llvm_unreachable("unhandled VPI semantic type kind");
}

uint32_t vpiKindForNet(VPITypeSemanticsAttr type) {
  using Kind = VPITypeKind;
  using VPIKind = reflection::VPIObjectKind;
  if (!type)
    return static_cast<uint16_t>(VPIKind::Net);
  switch (type.getKind()) {
  case Kind::GenericIntegral:
    return static_cast<uint16_t>(type.getIsFourState() ? VPIKind::LogicNet
                                                       : VPIKind::BitNet);
  case Kind::Bit:
    return static_cast<uint16_t>(VPIKind::BitNet);
  case Kind::Logic:
  case Kind::Reg:
    return static_cast<uint16_t>(VPIKind::LogicNet);
  case Kind::Byte:
    return static_cast<uint16_t>(VPIKind::ByteNet);
  case Kind::ShortInt:
    return static_cast<uint16_t>(VPIKind::ShortIntNet);
  case Kind::Int:
    return static_cast<uint16_t>(VPIKind::IntNet);
  case Kind::LongInt:
    return static_cast<uint16_t>(VPIKind::LongIntNet);
  case Kind::Integer:
    return static_cast<uint16_t>(VPIKind::IntegerNet);
  case Kind::Enum:
    return static_cast<uint16_t>(VPIKind::EnumNet);
  case Kind::Time:
    return static_cast<uint16_t>(VPIKind::TimeNet);
  case Kind::ShortReal:
    return static_cast<uint16_t>(VPIKind::ShortRealNet);
  case Kind::Real:
  case Kind::Realtime:
    return static_cast<uint16_t>(VPIKind::RealNet);
  case Kind::PackedArray: {
    VPITypeSemanticsAttr element = packedArrayElement(type);
    if (!element)
      return static_cast<uint16_t>(VPIKind::Net);
    switch (element.getKind()) {
    case Kind::Enum:
    case Kind::PackedStruct:
      return static_cast<uint16_t>(VPIKind::PackedArrayNet);
    default:
      return vpiKindForNet(element);
    }
  }
  case Kind::UnpackedArray:
  case Kind::DynamicArray:
  case Kind::Queue:
  case Kind::AssocArray:
  case Kind::PackedOpenArray:
  case Kind::UnpackedOpenArray:
    return static_cast<uint16_t>(VPIKind::ArrayNet);
  case Kind::PackedStruct:
  case Kind::UnpackedStruct:
    return static_cast<uint16_t>(VPIKind::StructNet);
  case Kind::PackedUnion:
  case Kind::UnpackedUnion:
    return static_cast<uint16_t>(VPIKind::UnionNet);
  case Kind::Unknown:
  case Kind::String:
  case Kind::Chandle:
  case Kind::Class:
  case Kind::VirtualInterface:
  case Kind::Event:
  case Kind::Process:
  case Kind::Covergroup:
  case Kind::Mailbox:
  case Kind::Semaphore:
  case Kind::Void:
  case Kind::Untyped:
  case Kind::Sequence:
  case Kind::Property:
    return static_cast<uint16_t>(VPIKind::Net);
  }
  llvm_unreachable("unhandled VPI semantic type kind");
}

std::optional<uint32_t> vpiKindForTypespec(VPITypeSemanticsAttr type) {
  using Kind = VPITypeKind;
  using VPIKind = reflection::VPIObjectKind;
  switch (type.getKind()) {
  case Kind::LongInt:
    return static_cast<uint16_t>(VPIKind::LongIntTypespec);
  case Kind::ShortReal:
    return static_cast<uint16_t>(VPIKind::ShortRealTypespec);
  case Kind::Byte:
    return static_cast<uint16_t>(VPIKind::ByteTypespec);
  case Kind::ShortInt:
    return static_cast<uint16_t>(VPIKind::ShortIntTypespec);
  case Kind::Int:
    return static_cast<uint16_t>(VPIKind::IntTypespec);
  case Kind::Class:
  case Kind::Process:
  case Kind::Mailbox:
  case Kind::Semaphore:
    return static_cast<uint16_t>(VPIKind::ClassTypespec);
  case Kind::String:
    return static_cast<uint16_t>(VPIKind::StringTypespec);
  case Kind::Chandle:
    return static_cast<uint16_t>(VPIKind::ChandleTypespec);
  case Kind::Enum:
    return static_cast<uint16_t>(VPIKind::EnumTypespec);
  case Kind::Integer:
    return static_cast<uint16_t>(VPIKind::IntegerTypespec);
  case Kind::Time:
    return static_cast<uint16_t>(VPIKind::TimeTypespec);
  case Kind::Real:
  case Kind::Realtime:
    return static_cast<uint16_t>(VPIKind::RealTypespec);
  case Kind::PackedStruct:
  case Kind::UnpackedStruct:
    return static_cast<uint16_t>(VPIKind::StructTypespec);
  case Kind::PackedUnion:
  case Kind::UnpackedUnion:
    return static_cast<uint16_t>(VPIKind::UnionTypespec);
  case Kind::Bit:
    return static_cast<uint16_t>(VPIKind::BitTypespec);
  case Kind::GenericIntegral:
    return static_cast<uint16_t>(type.getIsFourState() ? VPIKind::LogicTypespec
                                                       : VPIKind::BitTypespec);
  case Kind::Logic:
  case Kind::Reg:
    return static_cast<uint16_t>(VPIKind::LogicTypespec);
  case Kind::UnpackedArray:
  case Kind::DynamicArray:
  case Kind::Queue:
  case Kind::AssocArray:
  case Kind::UnpackedOpenArray:
    return static_cast<uint16_t>(VPIKind::ArrayTypespec);
  case Kind::PackedArray: {
    VPITypeSemanticsAttr element = packedArrayElement(type);
    if (!element)
      return std::nullopt;
    if (element.getKind() == Kind::Enum ||
        element.getKind() == Kind::PackedStruct ||
        element.getKind() == Kind::PackedUnion)
      return static_cast<uint16_t>(VPIKind::PackedArrayTypespec);
    return vpiKindForTypespec(element);
  }
  case Kind::PackedOpenArray: {
    if (type.getChildren().size() != 1)
      return std::nullopt;
    auto element = mlir::dyn_cast<VPITypeSemanticsAttr>(type.getChildren()[0]);
    if (!element)
      return std::nullopt;
    if (element.getKind() == Kind::Enum ||
        element.getKind() == Kind::PackedStruct ||
        element.getKind() == Kind::PackedUnion)
      return static_cast<uint16_t>(VPIKind::PackedArrayTypespec);
    return vpiKindForTypespec(element);
  }
  case Kind::Void:
    return static_cast<uint16_t>(VPIKind::VoidTypespec);
  case Kind::Sequence:
    return static_cast<uint16_t>(VPIKind::SequenceTypespec);
  case Kind::Property:
    return static_cast<uint16_t>(VPIKind::PropertyTypespec);
  case Kind::Event:
    return static_cast<uint16_t>(VPIKind::EventTypespec);
  case Kind::VirtualInterface:
    return static_cast<uint16_t>(VPIKind::InterfaceTypespec);
  case Kind::Unknown:
  case Kind::Covergroup:
  case Kind::Untyped:
    return std::nullopt;
  }
  llvm_unreachable("unhandled VPI semantic typespec kind");
}

} // namespace obelisk::sim
