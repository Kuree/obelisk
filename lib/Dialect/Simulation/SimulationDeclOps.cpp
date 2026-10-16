//===- SimulationDeclOps.cpp - Declaration op verifiers -------===//
//
// Verifiers for scope, storage, net, driver, covergroup, class, and reference
// declarations, and the design-level structural verification.
//
//===----------------------------------------------------------------------===//

#include "SimulationVerifiers.h"
#include "obelisk/Coverage/CoverageLimits.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Dialect/Simulation/SimulationVPI.h"
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
#include <optional>
#include <tuple>

using namespace mlir;

namespace obelisk::sim {

static constexpr uint64_t interfaceDispatchSlot =
    std::numeric_limits<uint32_t>::max();

static uint32_t effectiveScopeVPIKind(SimScopeDeclOp scope) {
  using VPIKind = reflection::VPIObjectKind;
  if (scope.getVpiKind())
    return *scope.getVpiKind();
  if (scope.getInterfaceType())
    return static_cast<uint16_t>(VPIKind::Interface);
  return scope.getId() == 0 ? 0 : static_cast<uint16_t>(VPIKind::Module);
}

static uint32_t effectiveCodeUnitVPIKind(SimCodeUnitDeclOp codeUnit) {
  using Kind = reflection::VPIObjectKind;
  // Inspect the marker directly so hidden-object classification remains
  // independent of the optional reflection metadata on this operation.
  if (codeUnit->hasAttr("internal"))
    return 0;
  switch (codeUnit.getCodeUnitKind()) {
  case EntryKind::Initial:
    return static_cast<uint16_t>(Kind::Initial);
  case EntryKind::Final:
    return static_cast<uint16_t>(Kind::Final);
  case EntryKind::Always:
  case EntryKind::AlwaysComb:
  case EntryKind::AlwaysFF:
  case EntryKind::AlwaysLatch:
    return static_cast<uint16_t>(Kind::Always);
  case EntryKind::Function:
    return static_cast<uint16_t>(Kind::Function);
  case EntryKind::Task:
    return static_cast<uint16_t>(Kind::Task);
  default:
    return 0;
  }
}

static LogicalResult verifyVPIProperties(Operation *operation,
                                         uint32_t exactKind,
                                         VPIPropertySetAttr properties) {
  if (!properties)
    return success();
  for (Attribute attribute : properties.getProperties()) {
    auto property = cast<VPIPropertyAttr>(attribute);
    uint32_t selector =
        static_cast<uint32_t>(property.getSelector().getValue().getZExtValue());
    if (selector == 15 || selector == 16)
      return operation->emitOpError()
             << "VPI definition file and line properties must use "
                "definition_loc as their canonical IR representation";
    if (selector == 624)
      return operation->emitOpError()
             << "vpiAlwaysType must use code_unit_kind as its canonical IR "
                "representation";
    const auto *descriptor = reflection::findVPIProperty(exactKind, selector);
    if (!descriptor)
      return operation->emitOpError()
             << "VPI property " << selector
             << " is not applicable to exact object kind " << exactKind;
    if (descriptor->realization !=
            reflection::VPIPropertyRealization::FixedImage &&
        descriptor->realization !=
            reflection::VPIPropertyRealization::DefinitionImage &&
        descriptor->realization !=
            reflection::VPIPropertyRealization::OptionalImage)
      return operation->emitOpError()
             << "VPI property " << selector << " is not a FixedImage property";
    if (selector == 74 && cast<BoolAttr>(property.getValue()).getValue() !=
                              operation->hasAttr("is_protected"))
      return operation->emitOpError(
          "explicit vpiIsProtected value conflicts with is_protected");
  }
  return success();
}

static LogicalResult verifyDefinitionLocation(Operation *operation,
                                              uint32_t exactKind,
                                              LocationAttr definitionLoc) {
  if (!definitionLoc)
    return success();
  if (!reflection::findVPIProperty(exactKind, 15) ||
      !reflection::findVPIProperty(exactKind, 16))
    return operation->emitOpError(
        "definition_loc is not applicable to this exact VPI object kind");
  auto file = dyn_cast<FileLineColLoc>(definitionLoc);
  if (!file || file.getFilename().empty() || file.getLine() == 0)
    return operation->emitOpError(
        "definition_loc requires a concrete source file and line");
  if (file.getFilename().getValue().contains('\0'))
    return operation->emitOpError(
        "definition_loc filename contains an embedded NUL");
  if (file.getLine() > INT32_MAX)
    return operation->emitOpError(
        "definition_loc line exceeds the VPI 32-bit integer range");
  return success();
}

static LogicalResult verifyFixedReflection(Operation *operation,
                                           uint32_t exactKind,
                                           VPIPropertySetAttr properties,
                                           LocationAttr definitionLoc) {
  if (Attribute attribute = operation->getAttr("vpi_properties");
      attribute && !isa<VPIPropertySetAttr>(attribute))
    return operation->emitOpError(
        "vpi_properties must be a #simulation.vpi_properties attribute");
  if (Attribute attribute = operation->getAttr("definition_loc");
      attribute && !isa<LocationAttr>(attribute))
    return operation->emitOpError(
        "definition_loc must be a location attribute");
  if (Attribute attribute = operation->getAttr("is_protected")) {
    if (!isa<UnitAttr>(attribute))
      return operation->emitOpError("is_protected must be a unit attribute");
    const auto *descriptor = reflection::findVPIProperty(exactKind, 74);
    if (!descriptor || descriptor->realization !=
                           reflection::VPIPropertyRealization::FixedImage)
      return operation->emitOpError(
          "is_protected is not applicable to this exact VPI object kind");
  }
  if (failed(verifyVPIProperties(operation, exactKind, properties)))
    return failure();
  return verifyDefinitionLocation(operation, exactKind, definitionLoc);
}

static VPIPropertySetAttr reflectionProperties(Operation *operation) {
  return operation->getAttrOfType<VPIPropertySetAttr>("vpi_properties");
}

static LocationAttr reflectionDefinitionLoc(Operation *operation) {
  return operation->getAttrOfType<LocationAttr>("definition_loc");
}

static bool hasNetSubtype(Operation *operation, int64_t expected) {
  VPIPropertySetAttr properties = reflectionProperties(operation);
  if (!properties)
    return false;
  for (Attribute attribute : properties.getProperties()) {
    auto property = cast<VPIPropertyAttr>(attribute);
    if (property.getSelector().getValue().getZExtValue() != 22)
      continue;
    auto value = dyn_cast<IntegerAttr>(property.getValue());
    return value && value.getValue().getSExtValue() == expected;
  }
  return false;
}

static bool isEntirelyFourState(Type type) {
  if (isa<LogicType>(type))
    return true;
  if (!isAggregateType(type))
    return false;
  for (unsigned index = 0, end = getAggregateNumElements(type); index != end;
       ++index)
    if (!isEntirelyFourState(getAggregateElementType(type, index)))
      return false;
  return true;
}

// Classify the physical representation of one fixed packed subrange. A port
// view may select a two-state member from a mixed aggregate, so comparing the
// port against the whole source type is both too strict and too imprecise.
static bool packedRangeContainsFourState(Type type, uint64_t low,
                                         uint64_t width) {
  if (width == 0)
    return false;
  if (auto array = dyn_cast<PackedArrayType>(type)) {
    uint64_t elementWidth = *getPackedWidth(array.getElementType());
    uint64_t end = low + width;
    uint64_t first = low / elementWidth;
    uint64_t last = (end - 1) / elementWidth;
    if (first == last)
      return packedRangeContainsFourState(array.getElementType(),
                                          low % elementWidth, width);
    uint64_t firstWidth = elementWidth - low % elementWidth;
    if (packedRangeContainsFourState(array.getElementType(), low % elementWidth,
                                     firstWidth))
      return true;
    if (last > first + 1 && containsFourStateLeaf(array.getElementType()))
      return true;
    return packedRangeContainsFourState(array.getElementType(), 0,
                                        end - last * elementWidth);
  }
  ArrayAttr fields;
  if (auto aggregate = dyn_cast<PackedStructType>(type))
    fields = aggregate.getFields();
  else if (auto aggregate = dyn_cast<PackedUnionType>(type))
    fields = aggregate.getFields();
  if (fields) {
    uint64_t end = low + width;
    for (Attribute attribute : fields) {
      auto field = cast<FieldAttr>(attribute);
      uint64_t fieldWidth = *getPackedWidth(field.getType());
      uint64_t fieldLow = field.getPackedOffset();
      uint64_t fieldEnd = fieldLow + fieldWidth;
      uint64_t overlapLow = std::max(low, fieldLow);
      uint64_t overlapEnd = std::min(end, fieldEnd);
      if (overlapLow < overlapEnd &&
          packedRangeContainsFourState(field.getType(), overlapLow - fieldLow,
                                       overlapEnd - overlapLow))
        return true;
    }
    return false;
  }
  return containsFourStateLeaf(type);
}

LogicalResult SimScopeDeclOp::verify() {
  if (failed(verifyNonnegative(*this, getIdAttr(), "scope ID")))
    return failure();
  if (getParentAttr() &&
      failed(verifyNonnegative(*this, getParentAttr(), "parent scope ID")))
    return failure();
  if (getParentAttr() && getParentAttr() == getIdAttr())
    return emitOpError("scope cannot be its own parent");
  if (auto definition = getDefinitionName(); definition) {
    if (definition->empty())
      return emitOpError("module definition name cannot be empty");
    if (getId() == 0)
      return emitOpError("root scope cannot carry a module definition name");
    if (effectiveScopeVPIKind(*this) !=
        static_cast<uint16_t>(reflection::VPIObjectKind::Module))
      return emitOpError(
          "only module scopes may carry a module definition name");
  }
  if (auto coverageID = getCoverageIdAttr();
      coverageID && coverageID.getValue().isZero())
    return emitOpError("coverage scope ID must be nonzero");
  StringAttr interfaceType = getInterfaceTypeAttr();
  if (interfaceType) {
    if (interfaceType.getValue().empty())
      return emitOpError("interface specialization key cannot be empty");
    if (getId() == 0)
      return emitOpError("root scope cannot be an interface instance");
  }
  if (IntegerAttr vpiKindAttr = getVpiKindAttr()) {
    if (failed(verifyNonnegative(*this, vpiKindAttr, "VPI object kind")))
      return failure();
    uint64_t value = getVpiKind().value();
    const auto *kind = reflection::findVPIObjectKind(value);
    if (value > UINT16_MAX || !kind ||
        kind->role != reflection::VPIObjectRole::Concrete ||
        (kind->families &
         reflection::vpiFamilyMask(reflection::VPIObjectFamily::Scope)) == 0)
      return emitOpError("VPI kind is not a concrete scope object");
    // `interface_type` is the optional virtual-interface specialization key,
    // not the source scope category. Every keyed scope must be an interface,
    // while an authored or legacy interface scope may legitimately lack a key
    // until virtual-interface binding metadata is available.
    if (interfaceType &&
        kind->value !=
            static_cast<uint32_t>(reflection::VPIObjectKind::Interface))
      return emitOpError(
          "interface scope metadata and intrinsic VPI kind disagree");
  }
  return verifyFixedReflection(*this, effectiveScopeVPIKind(*this),
                               reflectionProperties(*this),
                               reflectionDefinitionLoc(*this));
}

LogicalResult SimVPIDefinitionDeclOp::verify() {
  if (getDefinitionName().empty())
    return emitOpError("requires a nonempty source definition name");
  if (getDefinitionName().contains('\0'))
    return emitOpError("source definition name contains an embedded NUL");
  if (failed(verifyNonnegative(*this, getVpiKindAttr(), "VPI object kind")))
    return failure();
  using Kind = reflection::VPIObjectKind;
  switch (static_cast<Kind>(getVpiKind())) {
  case Kind::Module:
  case Kind::Interface:
  case Kind::Program:
    if (getPrimitiveTypeAttr())
      return emitOpError(
          "only primitive definitions may specify a primitive type");
    return verifyDefinitionLocation(*this, getVpiKind(),
                                    getDefinitionLocAttr());
  case Kind::Gate:
  case Kind::Switch: {
    if (!getPrimitiveTypeAttr())
      return emitOpError("primitive definition requires a primitive type");
    const auto *primitive =
        reflection::findVPIBuiltinPrimitive(*getPrimitiveType());
    if (!primitive || primitive->objectKind != getVpiKind() ||
        primitive->sourceName != getDefinitionName())
      return emitOpError(
          "primitive type, object kind, and definition name must agree");
    if (getDefinitionLocAttr())
      return emitOpError(
          "built-in primitive definitions have no source definition location");
    return success();
  }
  default:
    return emitOpError("VPI definition kind must be a module, interface, "
                       "program, gate, or switch");
  }
}

LogicalResult SimVPIDefinitionBindingOp::verifySymbolUses(
    SymbolTableCollection &symbolTable) {
  auto source = symbolTable.lookupNearestSymbolFrom<SimVPIObjectAnchorOp>(
      *this, getSourceAttr());
  auto definition = symbolTable.lookupNearestSymbolFrom<SimVPIDefinitionDeclOp>(
      *this, getDefinitionAttr());
  if (!source || !definition)
    return emitOpError("requires a primitive anchor and a shared definition");
  using Kind = reflection::VPIObjectKind;
  if ((source.getVpiKind() != static_cast<uint32_t>(Kind::Gate) &&
       source.getVpiKind() != static_cast<uint32_t>(Kind::Switch)) ||
      source.getBackingAttr() || source.getVpiKind() != definition.getVpiKind())
    return emitOpError(
        "primitive source and shared definition kinds must agree");
  return success();
}

LogicalResult SimVPIDefinitionMemberDeclOp::verify() {
  if (getMemberName().empty())
    return emitOpError("requires a nonempty member name");
  if (getMemberName().contains('\0'))
    return emitOpError("member name contains an embedded NUL");
  if (failed(verifyNonnegative(*this, getVpiKindAttr(), "VPI object kind")) ||
      failed(verifyNonnegative(*this, getOrdinalAttr(), "member ordinal")))
    return failure();
  using Kind = reflection::VPIObjectKind;
  Kind kind = static_cast<Kind>(getVpiKind());
  if (kind == Kind::IODecl) {
    if (!getDirectionAttr())
      return emitOpError("vpiIODecl member requires a direction");
    if (getIsLocalParamAttr() || getHasExplicitParameterRangeAttr() ||
        getUnavailableDefaultAttr() || getHasOriginalDefaultAttr())
      return emitOpError("IO declaration cannot carry parameter facts");
  } else if (kind == Kind::Parameter || kind == Kind::TypeParameter) {
    if (getDirectionAttr())
      return emitOpError("parameter declaration cannot carry an IO direction");
    if (getHasOriginalDefaultAttr() && getUnavailableDefaultAttr())
      return emitOpError("original default is exclusive with unavailability");
    if (kind == Kind::TypeParameter && getHasExplicitParameterRangeAttr())
      return emitOpError("type parameter cannot carry a value range");
  } else {
    return emitOpError("requires IODecl, Parameter, or TypeParameter kind");
  }
  return success();
}

LogicalResult SimVPIDefinitionMemberDeclOp::verifySymbolUses(
    SymbolTableCollection &symbolTable) {
  auto definition = symbolTable.lookupNearestSymbolFrom<SimVPIDefinitionDeclOp>(
      *this, getDefinitionAttr());
  if (!definition)
    return emitOpError("references an unknown VPI definition");
  if (definition.getPrimitiveTypeAttr())
    return emitOpError("built-in primitive definitions cannot have members");
  return success();
}

LogicalResult SimVPIDefinitionSpecializationDeclOp::verifySymbolUses(
    SymbolTableCollection &symbolTable) {
  auto definition = symbolTable.lookupNearestSymbolFrom<SimVPIDefinitionDeclOp>(
      *this, getDefinitionAttr());
  if (!definition)
    return emitOpError("references an unknown VPI definition");
  if (definition.getPrimitiveTypeAttr())
    return emitOpError(
        "built-in primitive definitions cannot have specializations");
  return success();
}

LogicalResult SimVPIDefinitionMemberSpecializationOp::verifySymbolUses(
    SymbolTableCollection &symbolTable) {
  auto specialization =
      symbolTable.lookupNearestSymbolFrom<SimVPIDefinitionSpecializationDeclOp>(
          *this, getSpecializationAttr());
  auto member =
      symbolTable.lookupNearestSymbolFrom<SimVPIDefinitionMemberDeclOp>(
          *this, getMemberAttr());
  if (!specialization)
    return emitOpError("references an unknown VPI definition specialization");
  if (!member)
    return emitOpError("references an unknown VPI definition member");
  if (specialization.getDefinitionAttr() != member.getDefinitionAttr())
    return emitOpError(
        "member and specialization reference different VPI definitions");
  auto value = getImmutableValueAttr();
  bool parameter = member.getVpiKind() ==
                   static_cast<uint32_t>(reflection::VPIObjectKind::Parameter);
  if (static_cast<bool>(value) != parameter)
    return emitOpError("exactly value parameters require an immutable value");
  if (value) {
    auto width = getPackedWidth(value.getType());
    if (!width || !*width)
      return emitOpError("parameter requires a fixed packed immutable value");
    if (member.getHasExplicitParameterRangeAttr() &&
        getEffectiveType().getKind() != VPITypeKind::PackedArray)
      return emitOpError(
          "explicit parameter range requires a packed-array semantic root");
    if (value.getIsSigned() != getEffectiveType().getIsSigned())
      return emitOpError(
          "parameter value and effective type signedness differ");
    if (failed(verifyVPITypeSemantics([&] { return emitOpError(); },
                                      value.getType(), getEffectiveType())))
      return failure();
  }
  return success();
}

LogicalResult SimVPIDefinitionMemberInstanceBindingOp::verifySymbolUses(
    SymbolTableCollection &symbolTable) {
  if (!symbolTable.lookupNearestSymbolFrom<SimVPIDefinitionMemberDeclOp>(
          *this, getMemberAttr()))
    return emitOpError("references an unknown VPI definition member");
  return success();
}

LogicalResult SimVPIDefinitionMemberInstanceBindingOp::verify() {
  if (getExprTarget().getKind() == VPIObjectRefKind::Symbol ||
      getExprTarget().getKind() == VPIObjectRefKind::DefinitionMember)
    return emitOpError(
        "expression endpoint must be whole storage or a declared net");
  return verifyNonnegative(*this, getScopeIdAttr(), "scope ID");
}

LogicalResult SimVPIDefinitionMemberInstanceRelationOp::verify() {
  if (failed(verifyNonnegative(*this, getScopeIdAttr(), "scope ID")) ||
      failed(verifyNonnegative(*this, getSelectorAttr(), "VPI selector")) ||
      failed(verifyNonnegative(*this, getOrdinalAttr(), "ordinal")))
    return failure();
  if (getSelector() > UINT16_MAX)
    return emitOpError("VPI selector exceeds the reflection encoding");
  if (getOrdinal() > UINT32_MAX)
    return emitOpError("ordinal exceeds the reflection encoding");
  if (getMode() == VPIRelationMode::Handle && getOrdinal() != 0)
    return emitOpError("vpi_handle relation must use ordinal zero");
  return success();
}

LogicalResult SimVPIDefinitionMemberInstanceRelationOp::verifySymbolUses(
    SymbolTableCollection &symbolTable) {
  if (!symbolTable.lookupNearestSymbolFrom<SimVPIDefinitionMemberDeclOp>(
          *this, getMemberAttr()))
    return emitOpError("references an unknown VPI definition member");
  return success();
}

LogicalResult
SimScopeDeclOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  FlatSymbolRefAttr reference = getVpiDefinitionAttr();
  FlatSymbolRefAttr specializationRef = getVpiSpecializationAttr();
  if (specializationRef && !reference)
    return emitOpError("VPI specialization requires a VPI definition");
  if (!reference)
    return success();
  if (!getParentAttr())
    return emitOpError("root scope cannot reference a VPI definition");
  auto definition = symbolTable.lookupNearestSymbolFrom<SimVPIDefinitionDeclOp>(
      *this, reference);
  if (!definition)
    return emitOpError("references an unknown VPI definition");
  if (definition.getVpiKind() != effectiveScopeVPIKind(*this))
    return emitOpError("VPI definition kind disagrees with the scope kind");
  if (!specializationRef)
    return success();
  auto specialization =
      symbolTable.lookupNearestSymbolFrom<SimVPIDefinitionSpecializationDeclOp>(
          *this, specializationRef);
  if (!specialization)
    return emitOpError("references an unknown VPI definition specialization");
  if (specialization.getDefinitionAttr() != reference)
    return emitOpError(
        "VPI specialization and scope reference different definitions");
  return success();
}

LogicalResult SimCodeUnitDeclOp::verify() {
  if (failed(verifyNonnegative(*this, getIdAttr(), "code-unit ID")) ||
      failed(verifyNonnegative(*this, getScopeIdAttr(), "scope ID")))
    return failure();
  if (getId() == 0)
    return emitOpError("code-unit ID must be nonzero");
  if (getHierarchicalName().empty())
    return emitOpError("requires a nonempty hierarchical name");
  return verifyFixedReflection(*this, effectiveCodeUnitVPIKind(*this),
                               reflectionProperties(*this),
                               reflectionDefinitionLoc(*this));
}

LogicalResult SimStatementDeclOp::verify() {
  if (failed(verifyNonnegative(*this, getIdAttr(), "statement ID")) ||
      (getCodeUnitIdAttr() &&
       failed(verifyNonnegative(*this, getCodeUnitIdAttr(), "code-unit ID"))) ||
      failed(verifyNonnegative(*this, getScopeIdAttr(), "scope ID")) ||
      failed(verifyNonnegative(*this, getVpiKindAttr(), "VPI object kind")) ||
      (getParentIdAttr() && failed(verifyNonnegative(*this, getParentIdAttr(),
                                                     "parent statement ID"))))
    return failure();
  if (getId() == 0)
    return emitOpError("statement ID must be nonzero");
  if (getCodeUnitId() && *getCodeUnitId() == 0)
    return emitOpError("code-unit ID must be nonzero");
  if (getParentIdAttr() && *getParentId() == getId())
    return emitOpError("cannot be its own parent");
  const auto *kind = reflection::findVPIObjectKind(getVpiKind());
  if (getVpiKind() > UINT16_MAX || !kind ||
      kind->role != reflection::VPIObjectRole::Concrete ||
      (kind->families &
       reflection::vpiFamilyMask(reflection::VPIObjectFamily::Statement)) == 0)
    return emitOpError("VPI kind is not a concrete statement object");
  bool scopeOwned =
      (kind->families &
       reflection::vpiFamilyMask(
           reflection::VPIObjectFamily::ScopeOwnedStatement)) != 0;
  if (scopeOwned == getCodeUnitId().has_value())
    return emitOpError(scopeOwned
                           ? "scope-owned statement must omit a code-unit ID"
                           : "behavioral statement requires a code-unit ID");
  bool named =
      kind && (kind->value == static_cast<uint32_t>(
                                  reflection::VPIObjectKind::NamedBegin) ||
               kind->value ==
                   static_cast<uint32_t>(reflection::VPIObjectKind::NamedFork));
  bool requiresScope =
      named || getVpiKind() == static_cast<uint16_t>(
                                   reflection::VPIObjectKind::ForeachStmt);
  bool loopScope =
      getIsScope() &&
      (getVpiKind() == static_cast<uint32_t>(reflection::VPIObjectKind::For) ||
       getVpiKind() ==
           static_cast<uint32_t>(reflection::VPIObjectKind::ForeachStmt));
  if ((named && !getName()) ||
      (getName() && ((!named && !loopScope) || getName()->empty())))
    return emitOpError(
        named ? "named block requires a nonempty name"
              : "only named begin/fork or loop scopes may carry a name");
  bool mayBeScope =
      (kind->families &
       reflection::vpiFamilyMask(reflection::VPIObjectFamily::Scope)) != 0;
  if (getIsScope() && !mayBeScope)
    return emitOpError("only a scope-capable statement may be marked is_scope");
  if (requiresScope && !getIsScope())
    return emitOpError(
        "named begin/fork and foreach statements must be marked is_scope");
  return verifyFixedReflection(*this, getVpiKind(), reflectionProperties(*this),
                               reflectionDefinitionLoc(*this));
}

LogicalResult SimVPILocalDeclOp::verify() {
  if (!isa<SimDesignOp>((*this)->getParentOp()))
    return emitOpError("requires a simulation.design owner");
  if (failed(verifyNonnegative(*this, getIdAttr(), "local declaration ID")) ||
      failed(verifyNonnegative(*this, getCodeUnitIdAttr(), "code-unit ID")) ||
      failed(verifyNonnegative(*this, getScopeIdAttr(), "scope ID")) ||
      failed(verifyNonnegative(*this, getOrdinalAttr(), "declaration ordinal")))
    return failure();
  if (!getId() || !getCodeUnitId())
    return emitOpError("local declaration and code-unit IDs must be nonzero");
  if (getStatementScopeIdAttr() &&
      (failed(verifyNonnegative(*this, getStatementScopeIdAttr(),
                                "statement scope ID")) ||
       !*getStatementScopeId()))
    return emitOpError("statement scope ID must be positive");
  if (getHierarchicalName().empty() || getDebugName().empty() ||
      getHierarchicalName().contains('\0') || getDebugName().contains('\0'))
    return emitOpError("requires nonempty source names without NUL bytes");
  auto emit = [&] { return emitOpError(); };
  if (failed(verifyElementType(emit, getValueType())) ||
      failed(verifyVPITypeSemantics(emit, getValueType(), getVpiType())))
    return failure();
  const uint32_t kind = vpiKindForClassMember(getVpiType());
  const auto *edge = reflection::findVPITraversal(
      static_cast<uint32_t>(reflection::VPIObjectKind::Frame),
      static_cast<uint32_t>(reflection::VPIRelationKind::AutomaticsRel),
      reflection::VPITraversalMode::Iterate);
  if (!edge || !reflection::vpiObjectSetContains(edge->targets, kind))
    return emitOpError("type is not a legal frame automatic declaration");
  if (auto properties = reflectionProperties(*this))
    for (Attribute attribute : properties.getProperties()) {
      auto property = cast<VPIPropertyAttr>(attribute);
      const uint64_t selector =
          property.getSelector().getValue().getZExtValue();
      if (selector == 2 && property.getValue() != getDebugNameAttr())
        return emitOpError(
            "vpiName must match the exact local declaration name");
      if (selector == 50 &&
          property.getValue() != BoolAttr::get(getContext(), true))
        return emitOpError("frame local declarations must be automatic");
    }
  return verifyFixedReflection(*this, kind, reflectionProperties(*this),
                               reflectionDefinitionLoc(*this));
}

LogicalResult SimVPICodeUnitScopeBindingOp::verify() {
  if (!isa<SimDesignOp>((*this)->getParentOp()))
    return emitOpError("requires a simulation.design owner");
  if (failed(verifyNonnegative(*this, getCodeUnitIdAttr(), "code-unit ID")))
    return failure();
  if (!getCodeUnitId())
    return emitOpError("code-unit ID must be positive");
  return success();
}

LogicalResult SimVPIForeachDeclOp::verify() {
  if (!isa<SimDesignOp>((*this)->getParentOp()))
    return emitOpError("requires a simulation.design owner");
  if (getStatementIdAttr().getValue().isNegative() || !getStatementId())
    return emitOpError("foreach statement ID must be positive");
  if (getCollection().getKind() != VPIObjectRefKind::Storage &&
      getCollection().getKind() != VPIObjectRefKind::Symbol)
    return emitOpError("collection must name a whole fixed-array variable");
  if (getLoopVariables().empty() || getLoopVariables().size() > UINT32_MAX)
    return emitOpError("requires a bounded nonempty index list");
  for (Attribute index : getLoopVariables())
    if (!isa<UnitAttr, FlatSymbolRefAttr>(index))
      return emitOpError("index must be an exact local symbol or unit skip");
  return success();
}

LogicalResult
SimVPIForeachDeclOp::verifySymbolUses(SymbolTableCollection &symbols) {
  if (getCollection().getKind() == VPIObjectRefKind::Symbol &&
      !symbols.lookupNearestSymbolFrom<SimVPILocalDeclOp>(
          *this, getCollection().getSymbol()))
    return emitOpError(
        "symbolic collection must name an exact local declaration");
  for (Attribute index : getLoopVariables())
    if (auto reference = dyn_cast<FlatSymbolRefAttr>(index);
        reference &&
        !symbols.lookupNearestSymbolFrom<SimVPILocalDeclOp>(*this, reference))
      return emitOpError("index must name an exact local declaration");
  return success();
}

LogicalResult
SimVPICodeUnitScopeBindingOp::verifySymbolUses(SymbolTableCollection &symbols) {
  auto anchor = symbols.lookupNearestSymbolFrom<SimVPIObjectAnchorOp>(
      *this, getSourceAttr());
  using Kind = reflection::VPIObjectKind;
  if (!anchor || anchor.getBackingAttr() ||
      (anchor.getVpiKind() != static_cast<uint32_t>(Kind::Task) &&
       anchor.getVpiKind() != static_cast<uint32_t>(Kind::Function)))
    return emitOpError("source must name an unbacked task or function anchor");
  if (anchor.getDebugName().empty() || anchor.getDebugName().contains('\0'))
    return emitOpError("mapped source callable requires a nonempty exact name "
                       "without NUL bytes");
  return success();
}

LogicalResult SimVPIInitializerBindingOp::verify() {
  if (!isa<SimDesignOp>((*this)->getParentOp()))
    return emitOpError("requires a simulation.design owner");
  if (failed(verifyNonnegative(*this, getCodeUnitIdAttr(), "code-unit ID")) ||
      failed(verifyNonnegative(*this, getStorageIdAttr(), "storage ID")))
    return failure();
  if (!getCodeUnitId())
    return emitOpError("code-unit ID must be positive");
  return success();
}

LogicalResult SimVPIStaticLocalBindingOp::verify() {
  if (!isa<SimDesignOp>((*this)->getParentOp()))
    return emitOpError("requires a simulation.design owner");
  if (failed(verifyNonnegative(*this, getStorageIdAttr(), "storage ID")) ||
      failed(verifyNonnegative(*this, getCodeUnitIdAttr(), "code-unit ID")) ||
      failed(verifyNonnegative(*this, getOrdinalAttr(), "source ordinal")))
    return failure();
  if (!getCodeUnitId())
    return emitOpError("code-unit ID must be positive");
  if (auto statement = getStatementScopeIdAttr();
      statement &&
      (statement.getValue().isNegative() || statement.getValue().isZero()))
    return emitOpError("statement scope ID must be positive");
  return success();
}

LogicalResult SimVPICallDeclOp::verify() {
  if (!isa<SimDesignOp>((*this)->getParentOp()))
    return emitOpError("requires a simulation.design owner");
  if (failed(verifyNonnegative(*this, getIdAttr(), "call-site ID")) ||
      failed(verifyNonnegative(*this, getCodeUnitIdAttr(), "code-unit ID")) ||
      failed(verifyNonnegative(*this, getScopeIdAttr(), "scope ID")) ||
      failed(verifyNonnegative(*this, getVpiKindAttr(), "VPI call kind")))
    return failure();
  if (!getId() || !getCodeUnitId())
    return emitOpError("call-site and code-unit IDs must be nonzero");
  for (auto [attribute, name] :
       {std::pair{getStatementIdAttr(), "statement ID"},
        std::pair{getEnclosingStatementIdAttr(), "enclosing statement ID"},
        std::pair{getCalleeCodeUnitIdAttr(), "callee code-unit ID"}})
    if (attribute) {
      if (failed(verifyNonnegative(*this, attribute, name)))
        return failure();
      if (attribute.getValue().isZero())
        return emitOpError() << name << " must be positive";
    }
  using Kind = reflection::VPIObjectKind;
  switch (static_cast<Kind>(getVpiKind())) {
  case Kind::FuncCall:
  case Kind::MethodFuncCall:
  case Kind::SysFuncCall:
  case Kind::TaskCall:
  case Kind::MethodTaskCall:
  case Kind::SysTaskCall:
    break;
  default:
    return emitOpError("kind must identify an exact task/function call");
  }
  if (getName().empty() || getName().contains('\0'))
    return emitOpError(
        "requires the nonempty source callee name without NUL bytes");
  if (getIsIndirect() &&
      getVpiKind() != static_cast<uint32_t>(Kind::TaskCall) &&
      getVpiKind() != static_cast<uint32_t>(Kind::FuncCall) &&
      getVpiKind() != static_cast<uint32_t>(Kind::MethodTaskCall) &&
      getVpiKind() != static_cast<uint32_t>(Kind::MethodFuncCall))
    return emitOpError(
        "only a user task/function call may use indirect dispatch");
  if ((getVpiKind() == static_cast<uint32_t>(Kind::SysFuncCall) ||
       getVpiKind() == static_cast<uint32_t>(Kind::SysTaskCall)) &&
      getCalleeCodeUnitIdAttr())
    return emitOpError("system call cannot name a source code-unit callee");
  return success();
}

LogicalResult SimVPIThreadOriginBindingOp::verify() {
  if (!isa<SimDesignOp>((*this)->getParentOp()))
    return emitOpError("requires a simulation.design owner");
  if (failed(verifyNonnegative(*this, getCodeUnitIdAttr(), "code-unit ID")) ||
      failed(verifyNonnegative(*this, getStatementIdAttr(), "statement ID")))
    return failure();
  if (!getCodeUnitId() || !getStatementId())
    return emitOpError("code-unit and statement IDs must be nonzero");
  return success();
}

LogicalResult SimStatementSiteDeclOp::verify() {
  if (failed(verifyNonnegative(*this, getIdAttr(), "statement-site ID")) ||
      failed(verifyNonnegative(*this, getStatementIdAttr(), "statement ID")) ||
      failed(verifyNonnegative(*this, getPhaseAttr(), "callback phase")))
    return failure();
  if (getId() == 0)
    return emitOpError("statement-site ID must be nonzero");
  if (getStatementId() == 0)
    return emitOpError("statement ID must be nonzero");
  if (getPhase() > UINT16_MAX)
    return emitOpError("callback phase exceeds the reflection encoding");
  return success();
}

LogicalResult SimVPIStatementRelationDeclOp::verify() {
  if (failed(verifyNonnegative(*this, getSourceIdAttr(), "source ID")) ||
      failed(verifyNonnegative(*this, getSourceVpiKindAttr(),
                               "source VPI object kind")) ||
      failed(verifyNonnegative(*this, getSelectorAttr(), "VPI selector")) ||
      failed(verifyNonnegative(*this, getOrdinalAttr(), "ordinal")) ||
      failed(verifyNonnegative(*this, getModeMaskAttr(), "mode mask")) ||
      failed(verifyNonnegative(*this, getTargetStatementIdAttr(),
                               "target statement ID")))
    return failure();
  if (getTargetStatementId() == 0)
    return emitOpError("target statement ID must be nonzero");
  if (getSourceVpiKind() > UINT16_MAX)
    return emitOpError("source VPI kind exceeds the reflection encoding");
  if (getSelector() > UINT16_MAX)
    return emitOpError("VPI selector exceeds the reflection encoding");
  if (getOrdinal() > UINT32_MAX)
    return emitOpError("ordinal exceeds the reflection encoding");
  if (getModeMask() == 0 || (getModeMask() & ~uint32_t{3}) != 0)
    return emitOpError(
        "mode mask must contain only vpi_handle and/or vpi_iterate");
  const auto *source = reflection::findVPIObjectKind(getSourceVpiKind());
  if (!source || source->role != reflection::VPIObjectRole::Concrete)
    return emitOpError("source VPI kind is not a concrete object");
  return success();
}

LogicalResult SimVPIRelationDeclOp::verify() {
  if (failed(verifyNonnegative(*this, getSelectorAttr(), "VPI selector")) ||
      failed(verifyNonnegative(*this, getOrdinalAttr(), "ordinal")))
    return failure();
  if (getSelector() > UINT16_MAX)
    return emitOpError("VPI selector exceeds the reflection encoding");
  if (getOrdinal() > UINT32_MAX)
    return emitOpError("ordinal exceeds the reflection encoding");
  if (getMode() == VPIRelationMode::Handle && getOrdinal() != 0)
    return emitOpError("vpi_handle relation must use ordinal zero");
  return success();
}

LogicalResult
SimVPIRelationDeclOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  for (auto [reference, endpoint] :
       {std::pair(getSource(), StringRef("source")),
        std::pair(getTarget(), StringRef("target"))}) {
    if (reference.getKind() == VPIObjectRefKind::DefinitionMember) {
      if (failed(verifyVPIDefinitionMemberRef(
              *this, (*this)->getParentOfType<SimDesignOp>(), reference,
              symbolTable)))
        return failure();
      continue;
    }
    if (reference.getKind() != VPIObjectRefKind::Symbol)
      continue;
    Operation *declaration =
        symbolTable.lookupNearestSymbolFrom(*this, reference.getSymbol());
    if (!declaration)
      return emitOpError() << "references an unknown " << endpoint
                           << " VPI symbol " << reference.getSymbol();
    if (!isa<SimVPIObjectAnchorOp, SimVPITypespecDeclOp, SimVPINettypeDeclOp,
             SimVPIClassMemberDeclOp, SimVPIUnresolvedTypeAliasDeclOp,
             SimVPILexicalClassTypespecDeclOp, SimVPIClassExtendsDeclOp,
             SimVPIClassParameterAssignDeclOp, SimVPILocalDeclOp,
             SimVPIExpressionDeclOp, SimVPIDefinitionMemberDeclOp,
             SimVPICallDeclOp>(declaration))
      return emitOpError()
             << endpoint << " VPI symbol " << reference.getSymbol()
             << " must name an object anchor, typespec, or nettype";
  }
  if (getSource().getKind() == VPIObjectRefKind::Symbol &&
      getTarget().getKind() == VPIObjectRefKind::Symbol) {
    auto source = symbolTable.lookupNearestSymbolFrom<SimVPIObjectAnchorOp>(
        *this, getSource().getSymbol());
    auto target = symbolTable.lookupNearestSymbolFrom<SimVPIObjectAnchorOp>(
        *this, getTarget().getSymbol());
    using Kind = reflection::VPIObjectKind;
    if (source &&
        source.getVpiKind() == static_cast<uint32_t>(Kind::PrimTerm) &&
        getSelector() ==
            static_cast<uint32_t>(reflection::VPIRelationKind::PrimitiveRel) &&
        (getMode() != VPIRelationMode::Handle || !target ||
         source.getParentAttr() != getTarget().getSymbol()))
      return emitOpError(
          "primitive terminal relation must name its exact owner");
    if (target &&
        target.getVpiKind() == static_cast<uint32_t>(Kind::PrimTerm) &&
        source &&
        (source.getVpiKind() == static_cast<uint32_t>(Kind::Gate) ||
         source.getVpiKind() == static_cast<uint32_t>(Kind::Switch) ||
         source.getVpiKind() == static_cast<uint32_t>(Kind::Udp)) &&
        getSelector() == static_cast<uint32_t>(Kind::PrimTerm) &&
        (getMode() != VPIRelationMode::Iterate ||
         target.getParentAttr() != getSource().getSymbol() ||
         getOrdinal() != target.getOwnerOrdinal()))
      return emitOpError(
          "primitive terminal inventory must preserve exact owner and index");
  }
  if (getSelector() ==
          static_cast<uint32_t>(reflection::VPIRelationKind::ExprRel) &&
      getMode() == VPIRelationMode::Handle &&
      getSource().getKind() == VPIObjectRefKind::Symbol) {
    if (auto source = symbolTable.lookupNearestSymbolFrom<SimVPIObjectAnchorOp>(
            *this, getSource().getSymbol());
        source && source.getUnavailableDefaultAttr())
      return emitOpError(
          "unavailable source default cannot have an Expr relation");
  }
  if (getSelector() ==
          static_cast<uint32_t>(reflection::VPIObjectKind::Typespec) &&
      getMode() == VPIRelationMode::Handle &&
      getSource().getKind() == VPIObjectRefKind::Symbol) {
    if (auto source = symbolTable.lookupNearestSymbolFrom<SimVPIObjectAnchorOp>(
            *this, getSource().getSymbol());
        source && source.getUnresolvedTypeParameterAttr() &&
        getTarget() != getSource())
      return emitOpError(
          "unresolved type parameter cannot reference an effective typespec");
  }
  return success();
}

LogicalResult SimVPINetIdentityDeclOp::verify() {
  if (failed(verifyNonnegative(*this, getIdAttr(), "VPI net identity ID")) ||
      failed(
          verifyNonnegative(*this, getBackingNetIdAttr(), "backing net ID")) ||
      failed(verifyNonnegative(*this, getScopeIdAttr(), "scope ID")))
    return failure();
  if (getHierarchicalName().empty())
    return emitOpError("requires a nonempty source hierarchy name");
  auto emit = [&] { return emitOpError(); };
  if (failed(verifyElementType(emit, getType())))
    return failure();
  if (failed(verifyVPITypeSemantics(emit, getType(), getVpiType())))
    return failure();
  uint32_t kind = vpiKindForNet(getVpiTypeAttr());
  const auto *object = reflection::findVPIObjectKind(kind);
  if (!kind || !object || object->role != reflection::VPIObjectRole::Concrete)
    return emitOpError("source type has no concrete VPI net object");
  return verifyFixedReflection(*this, kind, reflectionProperties(*this),
                               reflectionDefinitionLoc(*this));
}

LogicalResult SimVPIObjectAnchorOp::verify() {
  if (failed(verifyNonnegative(*this, getInventoryIdAttr(),
                               "VPI anchor inventory ID")) ||
      failed(verifyNonnegative(*this, getVpiKindAttr(), "VPI object kind")) ||
      failed(verifyNonnegative(*this, getEnclosingScopeIdAttr(),
                               "enclosing scope ID")) ||
      failed(verifyNonnegative(*this, getOwnerOrdinalAttr(), "owner ordinal")))
    return failure();
  const auto *kind = reflection::findVPIObjectKind(getVpiKind());
  if (getVpiKind() > UINT16_MAX || !kind ||
      kind->role != reflection::VPIObjectRole::Concrete)
    return emitOpError("VPI kind is not a concrete object");
  using Kind = reflection::VPIObjectKind;
  Kind anchorKind = static_cast<Kind>(getVpiKind());
  const bool sourceExpression =
      anchorKind == Kind::Constant || anchorKind == Kind::Operation;
  if (sourceExpression || anchorKind == Kind::PrimTerm) {
    if (!getHierarchicalName().empty() || !getDebugName().empty() ||
        !getParentAttr() || getBackingAttr())
      return emitOpError("source expression or primitive terminal requires "
                         "unnamed immutable ownership");
  } else if (getHierarchicalName().empty()) {
    return emitOpError("requires a nonempty source hierarchy name");
  }
  if (anchorKind == Kind::Parameter || anchorKind == Kind::Task ||
      anchorKind == Kind::Function)
    if (auto properties = reflectionProperties(*this))
      for (Attribute attribute : properties.getProperties()) {
        auto property = cast<VPIPropertyAttr>(attribute);
        if (property.getSelector().getValue().getZExtValue() == 2 &&
            property.getValue() != getDebugNameAttr())
          return emitOpError(
              "Name property must equal its exact declaration name");
      }
  if (getClassMethodAttr() && anchorKind != Kind::Task &&
      anchorKind != Kind::Function)
    return emitOpError("class_method requires a task or function anchor");
  if (getClassMethodAttr() && getDebugName().empty())
    return emitOpError("class_method requires a nonempty declaration name");
  if (anchorKind == Kind::Modport &&
      (!getParentAttr() || getDebugName().empty()))
    return emitOpError(
        "modport requires an interface parent and a nonempty declaration name");
  if (anchorKind == Kind::Constraint &&
      (!getParentAttr() || getDebugName().empty()))
    return emitOpError("constraint requires a named class-owned declaration");
  if (failed(verifyFixedReflection(*this, getVpiKind(),
                                   reflectionProperties(*this),
                                   reflectionDefinitionLoc(*this))))
    return failure();
  switch (anchorKind) {
  case Kind::Module:
  case Kind::Interface:
  case Kind::Modport:
  case Kind::Program:
  case Kind::Package:
  case Kind::ClassDefn:
  case Kind::Extends:
  case Kind::Constraint:
  case Kind::Task:
  case Kind::Function:
  case Kind::ClockingBlock:
  case Kind::GenScope:
  case Kind::PropertyDecl:
  case Kind::SequenceDecl:
  case Kind::ModuleArray:
  case Kind::InterfaceArray:
  case Kind::ProgramArray:
  case Kind::GateArray:
  case Kind::SwitchArray:
  case Kind::UdpArray:
  case Kind::NamedEventArray:
  case Kind::GenScopeArray:
  case Kind::InterconnectArray:
  case Kind::InterconnectNet:
  case Kind::Gate:
  case Kind::Switch:
  case Kind::Udp:
  case Kind::PrimTerm:
  case Kind::NamedEvent:
  case Kind::Parameter:
  case Kind::TypeParameter:
  case Kind::Constant:
  case Kind::Operation:
  case Kind::Assert:
  case Kind::Assume:
  case Kind::Cover:
  case Kind::Restrict:
    break;
  default:
    return emitOpError("kind cannot be a persistent lexical source anchor");
  }
  bool primitive = anchorKind == Kind::Gate || anchorKind == Kind::Switch ||
                   anchorKind == Kind::Udp;
  if (primitive != static_cast<bool>(getPrimitiveInputCountAttr()))
    return emitOpError(
        primitive ? "scalar primitive anchor requires an input count"
                  : "primitive_input_count requires a scalar primitive");
  if (getPrimitiveInputCount() && *getPrimitiveInputCount() > UINT32_MAX)
    return emitOpError("primitive input count exceeds the reflection encoding");
  if (getIsCompilationUnitAttr() &&
      getVpiKind() != static_cast<uint32_t>(reflection::VPIObjectKind::Package))
    return emitOpError("compilation-unit anchor must have vpiPackage kind");
  if (getIsCompilationUnitAttr() && getParentAttr())
    return emitOpError("compilation-unit anchor cannot have a lexical parent");
  if (getParentAttr() &&
      getParentAttr() == FlatSymbolRefAttr::get(getSymNameAttr()))
    return emitOpError("cannot be its own lexical parent");
  if (VPIObjectBackingAttr backing = getBackingAttr()) {
    switch (backing.getKind()) {
    case VPIObjectBackingKind::Scope:
      if (anchorKind != Kind::Module && anchorKind != Kind::Interface &&
          anchorKind != Kind::Program)
        return emitOpError(
            "scope backing requires a module, interface, or program anchor");
      break;
    case VPIObjectBackingKind::Class:
      if (anchorKind != Kind::ClassDefn)
        return emitOpError("class backing requires a class-definition anchor");
      break;
    case VPIObjectBackingKind::CodeUnit:
      if (anchorKind != Kind::Task && anchorKind != Kind::Function)
        return emitOpError(
            "code-unit backing requires a task or function anchor");
      break;
    case VPIObjectBackingKind::Storage:
      if (anchorKind != Kind::NamedEvent)
        return emitOpError(
            "storage backing requires a scalar named-event anchor");
      break;
    case VPIObjectBackingKind::Net:
      if (anchorKind != Kind::InterconnectNet)
        return emitOpError("net backing requires an interconnect-net anchor");
      break;
    }
  } else {
    if (anchorKind == Kind::Module || anchorKind == Kind::Interface ||
        anchorKind == Kind::Program)
      return emitOpError(
          "module, interface, and program anchors require scope backing");
  }
  if (anchorKind == Kind::InterconnectNet &&
      (!getBackingAttr() ||
       getBackingAttr().getKind() != VPIObjectBackingKind::Net))
    return emitOpError("interconnect-net anchor requires a net backing");
  const bool hasVPIType = static_cast<bool>(getVpiTypeAttr());
  const bool hasImmutableValue = static_cast<bool>(getImmutableValueAttr());
  if (getUnresolvedValueParameterAttr() &&
      (anchorKind != Kind::Parameter || hasVPIType || hasImmutableValue ||
       getBackingAttr() || !getParentAttr()))
    return emitOpError("unresolved value parameter requires a lexical owner "
                       "and no effective payload");
  if (anchorKind == Kind::PrimTerm) {
    if (hasVPIType || hasImmutableValue || getOwnerOrdinal() > INT32_MAX)
      return emitOpError(
          "primitive terminal requires no payload and an encodable index");
    IntegerAttr direction;
    if (auto properties = reflectionProperties(*this))
      for (Attribute attribute : properties.getProperties()) {
        auto property = cast<VPIPropertyAttr>(attribute);
        uint64_t selector = property.getSelector().getValue().getZExtValue();
        if (selector == 20)
          direction = dyn_cast<IntegerAttr>(property.getValue());
      }
    if (!direction || direction.getInt() < 1 || direction.getInt() > 3)
      return emitOpError("primitive terminal requires exact input/output/inout "
                         "direction and owner index");
  }
  if (anchorKind == Kind::Constant && !hasImmutableValue)
    return emitOpError("source constant requires an exact immutable value");
  if (anchorKind == Kind::Operation && (hasVPIType || hasImmutableValue))
    return emitOpError(
        "unbound source expression cannot carry an effective payload");
  if (anchorKind == Kind::Operation || anchorKind == Kind::Constant) {
    VPIPropertySetAttr properties = reflectionProperties(*this);
    IntegerAttr classification;
    if (properties)
      for (Attribute attribute : properties.getProperties()) {
        auto property = cast<VPIPropertyAttr>(attribute);
        if (property.getSelector().getValue().getZExtValue() ==
            (anchorKind == Kind::Operation ? 39 : 40))
          classification = dyn_cast<IntegerAttr>(property.getValue());
      }
    if (!classification || classification.getValue().isNegative())
      return emitOpError("source expression requires an exact operator or "
                         "constant classification");
    uint64_t value = classification.getValue().getZExtValue();
    if (anchorKind == Kind::Operation &&
        (value > UINT32_MAX || !reflection::findVPISourceOperator(value)))
      return emitOpError(
          "source operation has an unsupported operator classification");
    if (anchorKind == Kind::Constant && value != 7)
      return emitOpError(
          "source packed constant requires integer constant classification");
  }
  if (getUnresolvedTypeParameterAttr() &&
      (anchorKind != Kind::TypeParameter || hasVPIType || hasImmutableValue ||
       getBackingAttr() || !getParentAttr()))
    return emitOpError("unresolved type parameter requires a lexical owner and "
                       "no effective payload");
  if (anchorKind == Kind::TypeParameter &&
      ((!hasVPIType && !getUnresolvedTypeParameterAttr()) || hasImmutableValue))
    return emitOpError(
        "type parameter requires a source type and no immutable value");
  if (getUnavailableDefaultAttr() &&
      ((anchorKind != Kind::Parameter && anchorKind != Kind::TypeParameter) ||
       getUnavailableDefaultAttr().getValue().empty()))
    return emitOpError("unavailable default requires a parameter and nonempty "
                       "original syntax");
  if (anchorKind != Kind::TypeParameter && hasVPIType != hasImmutableValue)
    return emitOpError(
        "immutable source type and value must be present together");
  if (getHasExplicitParameterRangeAttr() && anchorKind != Kind::Parameter)
    return emitOpError(
        "explicit parameter range marker requires a parameter anchor");
  if (getHasExplicitParameterRangeAttr() && !hasImmutableValue)
    return emitOpError(
        "explicit parameter range marker requires immutable type and value");
  if (hasImmutableValue) {
    if (anchorKind != Kind::Parameter && anchorKind != Kind::Constant)
      return emitOpError(
          "immutable value is supported only on parameters and constants");
    FrozenConstantAttr value = getImmutableValueAttr();
    if (anchorKind == Kind::Constant) {
      auto type = getVpiTypeAttr();
      if (!type.getChildren().empty() ||
          (type.getTypedefAliases() && !type.getTypedefAliases().empty()))
        return emitOpError("source integer literal requires an unaliased "
                           "scalar integral type");
      switch (type.getKind()) {
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
        break;
      default:
        return emitOpError("source integer literal requires an unaliased "
                           "scalar integral type");
      }
    }
    if (!getPackedScalarType(value.getType()))
      return emitOpError("immutable parameter requires a fixed packed value");
    if (value.getIsSigned() != getVpiTypeAttr().getIsSigned())
      return emitOpError(
          "immutable parameter value and VPI type signedness must agree");
    auto emit = [&] { return emitOpError(); };
    if (failed(verifyVPITypeSemantics(emit, value.getType(), getVpiTypeAttr())))
      return failure();
    if (getHasExplicitParameterRangeAttr() &&
        getVpiTypeAttr().getKind() != VPITypeKind::PackedArray)
      return emitOpError(
          "explicit parameter range requires a packed-array semantic root");
  }
  if (anchorKind == Kind::InterconnectArray && !hasNetSubtype(*this, 16))
    return emitOpError("interconnect-array requires vpiInterconnect subtype");
  DenseI64ArrayAttr rangesAttr = getIndexRangesAttr();
  DenseI64ArrayAttr dimensionFlagsAttr = getIndexDimensionFlagsAttr();
  DenseI64ArrayAttr sparseAttr = getSparseIndicesAttr();
  ArrayRef<int64_t> ranges =
      rangesAttr ? rangesAttr.asArrayRef() : ArrayRef<int64_t>{};
  ArrayRef<int64_t> sparse =
      sparseAttr ? sparseAttr.asArrayRef() : ArrayRef<int64_t>{};
  bool fixedArray =
      anchorKind == Kind::ModuleArray || anchorKind == Kind::InterfaceArray ||
      anchorKind == Kind::ProgramArray || anchorKind == Kind::GateArray ||
      anchorKind == Kind::SwitchArray || anchorKind == Kind::UdpArray ||
      anchorKind == Kind::NamedEventArray ||
      anchorKind == Kind::InterconnectArray;
  if (rangesAttr && sparseAttr)
    return emitOpError("VPI array cannot be both fixed and sparse");
  if (rangesAttr && !fixedArray)
    return emitOpError("index_ranges is only valid on a fixed array anchor");
  if (rangesAttr && ranges.size() % 2 != 0)
    return emitOpError(
        "index_ranges requires left/right pairs on a fixed array anchor");
  if (fixedArray && (!rangesAttr || ranges.empty()))
    return emitOpError("fixed VPI array requires nonempty index_ranges");
  if (anchorKind == Kind::InterconnectArray && !dimensionFlagsAttr)
    return emitOpError("interconnect-array requires index dimension flags");
  if (anchorKind != Kind::InterconnectArray && dimensionFlagsAttr)
    return emitOpError(
        "index_dimension_flags is only valid on an interconnect-array anchor");
  if (dimensionFlagsAttr) {
    ArrayRef<int64_t> flags = dimensionFlagsAttr.asArrayRef();
    if (!rangesAttr || flags.size() * 2 != ranges.size())
      return emitOpError(
          "index_dimension_flags must have one entry per index range");
    if (llvm::any_of(flags,
                     [](int64_t flag) { return flag != 0 && flag != 1; }))
      return emitOpError("index dimension flags must be zero or one");
  }
  if (sparseAttr && dimensionFlagsAttr)
    return emitOpError("sparse VPI arrays cannot carry fixed-dimension flags");
  if (sparseAttr && anchorKind != Kind::GenScopeArray)
    return emitOpError(
        "sparse_indices is only valid on a generate-scope array anchor");
  if (anchorKind == Kind::GenScopeArray && !sparseAttr)
    return emitOpError("generate-scope array requires sparse_indices");
  if ((anchorKind == Kind::GateArray || anchorKind == Kind::SwitchArray ||
       anchorKind == Kind::UdpArray) &&
      ranges.size() != 2)
    return emitOpError("primitive VPI array must be one-dimensional");
  uint64_t elements = 1;
  for (size_t offset = 0; offset != ranges.size(); offset += 2) {
    uint64_t distance = ranges[offset] >= ranges[offset + 1]
                            ? static_cast<uint64_t>(ranges[offset]) -
                                  static_cast<uint64_t>(ranges[offset + 1])
                            : static_cast<uint64_t>(ranges[offset + 1]) -
                                  static_cast<uint64_t>(ranges[offset]);
    if (distance == UINT64_MAX || distance + 1 > UINT32_MAX ||
        elements > UINT32_MAX / (distance + 1))
      return emitOpError("fixed VPI array shape exceeds relation encoding");
    elements *= distance + 1;
  }
  llvm::DenseSet<int64_t> sparseSet;
  for (int64_t index : sparse)
    if (!sparseSet.insert(index).second)
      return emitOpError("sparse_indices contains a duplicate index");
  if (getMemberIndicesAttr() &&
      ((fixedArray && anchorKind != Kind::InterconnectArray) ||
       anchorKind == Kind::GenScopeArray))
    return emitOpError("array aggregates cannot carry member_indices");
  return success();
}

static Operation *lookupVPILexicalOwner(Operation *from,
                                        FlatSymbolRefAttr reference,
                                        SymbolTableCollection &symbolTable) {
  Operation *owner = symbolTable.lookupNearestSymbolFrom(from, reference);
  if (isa_and_nonnull<SimVPIObjectAnchorOp, SimVPIClassParameterAssignDeclOp>(
          owner))
    return owner;
  auto typespec = dyn_cast_or_null<SimVPITypespecDeclOp>(owner);
  return typespec && (typespec.getOrigin() ==
                          VPITypespecOrigin::ClassSpecialization ||
                      typespec.getOrigin() == VPITypespecOrigin::BuiltinClass)
             ? owner
             : nullptr;
}

static uint32_t getVPILexicalOwnerKind(Operation *owner) {
  if (auto anchor = dyn_cast<SimVPIObjectAnchorOp>(owner))
    return anchor.getVpiKind();
  return static_cast<uint32_t>(isa<SimVPIClassParameterAssignDeclOp>(owner)
                                   ? reflection::VPIObjectKind::ParamAssign
                                   : reflection::VPIObjectKind::ClassTypespec);
}

static uint64_t getVPILexicalOwnerScope(Operation *owner) {
  if (auto anchor = dyn_cast<SimVPIObjectAnchorOp>(owner))
    return anchor.getEnclosingScopeId();
  if (auto assignment = dyn_cast<SimVPIClassParameterAssignDeclOp>(owner))
    return assignment.getScopeId();
  return cast<SimVPITypespecDeclOp>(owner).getScopeId();
}

LogicalResult
SimVPIObjectAnchorOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  if (FlatSymbolRefAttr parent = getParentAttr()) {
    Operation *owner = lookupVPILexicalOwner(*this, parent, symbolTable);
    if (!owner)
      return emitOpError("references an unknown VPI parent anchor");
    auto target = dyn_cast<SimVPIObjectAnchorOp>(owner);
    using Kind = reflection::VPIObjectKind;
    Kind childKind = static_cast<Kind>(getVpiKind());
    Kind parentKind = static_cast<Kind>(getVPILexicalOwnerKind(owner));
    if ((childKind == Kind::Constant || childKind == Kind::Operation ||
         childKind == Kind::PrimTerm) &&
        getEnclosingScopeId() != getVPILexicalOwnerScope(owner))
      return emitOpError("source expression scope must match its exact owner");
    if (getUnresolvedValueParameterAttr() && parentKind != Kind::ClassDefn)
      return emitOpError("unresolved value parameter requires a lexical class "
                         "definition owner");
    if (getUnresolvedTypeParameterAttr() && parentKind != Kind::ClassDefn)
      return emitOpError("unresolved type parameter requires a lexical class "
                         "definition owner");
    bool parentFixedArray =
        parentKind == Kind::ModuleArray || parentKind == Kind::InterfaceArray ||
        parentKind == Kind::ProgramArray || parentKind == Kind::GateArray ||
        parentKind == Kind::SwitchArray || parentKind == Kind::UdpArray ||
        parentKind == Kind::NamedEventArray ||
        parentKind == Kind::InterconnectArray;
    DenseI64ArrayAttr memberAttr = getMemberIndicesAttr();
    ArrayRef<int64_t> member =
        memberAttr ? memberAttr.asArrayRef() : ArrayRef<int64_t>{};
    if (parentFixedArray) {
      DenseI64ArrayAttr rangesAttr = target.getIndexRangesAttr();
      if (!rangesAttr)
        return emitOpError("fixed-array parent has no index_ranges");
      ArrayRef<int64_t> ranges = rangesAttr.asArrayRef();
      size_t expectedRank = ranges.size() / 2;
      if (member.size() != expectedRank)
        return emitOpError(
            "fixed-array child member_indices rank does not match parent");
      for (size_t dimension = 0; dimension != member.size(); ++dimension) {
        int64_t left = ranges[dimension * 2];
        int64_t right = ranges[dimension * 2 + 1];
        if (member[dimension] < std::min(left, right) ||
            member[dimension] > std::max(left, right))
          return emitOpError(
              "fixed-array child member index is outside parent range");
      }
    } else if (parentKind == Kind::GenScopeArray) {
      DenseI64ArrayAttr sparseAttr = target.getSparseIndicesAttr();
      if (!sparseAttr || member.size() != 1 ||
          !llvm::is_contained(sparseAttr.asArrayRef(), member.front()))
        return emitOpError(
            "generate-array child member index is absent from parent");
    } else if (memberAttr) {
      return emitOpError(
          "member_indices requires a fixed or generate array parent");
    }
    auto isDesignScope = [](Kind kind) {
      return kind == Kind::Module || kind == Kind::Interface ||
             kind == Kind::Program;
    };
    auto isDeclarationScope = [&](Kind kind) {
      return isDesignScope(kind) || kind == Kind::Package ||
             kind == Kind::ClassDefn || kind == Kind::ClassTypespec ||
             kind == Kind::Task ||
             kind == Kind::Function || kind == Kind::GenScope;
    };
    bool legal = false;
    switch (childKind) {
    case Kind::Constant:
    case Kind::Operation:
      legal = parentKind == Kind::Parameter || parentKind == Kind::Operation ||
              parentKind == Kind::ParamAssign;
      break;
    case Kind::Module:
    case Kind::Interface:
    case Kind::Program:
      legal = isDesignScope(parentKind) || parentKind == Kind::GenScope ||
              (childKind == Kind::Module && parentKind == Kind::ModuleArray) ||
              (childKind == Kind::Interface &&
               parentKind == Kind::InterfaceArray) ||
              (childKind == Kind::Program && parentKind == Kind::ProgramArray);
      break;
    case Kind::ClassDefn:
    case Kind::Task:
    case Kind::Function:
      legal = isDeclarationScope(parentKind);
      break;
    case Kind::Extends:
      legal = parentKind == Kind::ClassDefn;
      break;
    case Kind::Modport: {
      auto interface = dyn_cast<SimVPIObjectAnchorOp>(owner);
      legal =
          parentKind == Kind::Interface && interface &&
          interface.getBackingAttr() &&
          interface.getBackingAttr().getKind() == VPIObjectBackingKind::Scope &&
          interface.getBackingAttr().getId().getValue().getZExtValue() ==
              getEnclosingScopeId();
      break;
    }
    case Kind::Constraint:
      legal =
          parentKind == Kind::ClassDefn || parentKind == Kind::ClassTypespec;
      break;
    case Kind::PropertyDecl:
    case Kind::SequenceDecl:
      legal =
          isDeclarationScope(parentKind) || parentKind == Kind::ClockingBlock;
      break;
    case Kind::Parameter:
    case Kind::TypeParameter:
      legal = isDeclarationScope(parentKind);
      break;
    case Kind::ClockingBlock:
      legal = isDesignScope(parentKind);
      break;
    // IEEE 1800-2023 39.3.1: a named concurrent assertion belongs to the
    // module, interface, program or generate scope that declares it.
    case Kind::Assert:
    case Kind::Assume:
    case Kind::Cover:
    case Kind::Restrict:
      legal = isDesignScope(parentKind) || parentKind == Kind::GenScope;
      break;
    case Kind::GenScope:
      legal = isDesignScope(parentKind) || parentKind == Kind::GenScope ||
              parentKind == Kind::GenScopeArray;
      break;
    case Kind::PrimTerm:
      legal = parentKind == Kind::Gate || parentKind == Kind::Switch ||
              parentKind == Kind::Udp;
      break;
    case Kind::Gate:
      legal = parentKind == Kind::GateArray || isDeclarationScope(parentKind);
      break;
    case Kind::Switch:
      legal = parentKind == Kind::SwitchArray || isDeclarationScope(parentKind);
      break;
    case Kind::Udp:
      legal = parentKind == Kind::UdpArray || isDeclarationScope(parentKind);
      break;
    case Kind::NamedEvent:
      legal =
          parentKind == Kind::NamedEventArray || isDeclarationScope(parentKind);
      break;
    case Kind::InterconnectNet:
      legal = parentKind == Kind::InterconnectArray ||
              isDeclarationScope(parentKind);
      break;
    case Kind::ModuleArray:
    case Kind::InterfaceArray:
    case Kind::ProgramArray:
    case Kind::GateArray:
    case Kind::SwitchArray:
    case Kind::UdpArray:
    case Kind::NamedEventArray:
    case Kind::GenScopeArray:
      legal = isDeclarationScope(parentKind);
      break;
    case Kind::InterconnectArray:
      legal = isDeclarationScope(parentKind) ||
              parentKind == Kind::InterconnectArray;
      break;
    case Kind::Package:
      // Compilation units use the vpiPackage kind internally so that they
      // can own declarations without manufacturing a fake vpiModule.  A
      // source package may therefore be nested under that wrapper, but
      // packages cannot otherwise be nested.
      legal = parentKind == Kind::Package && target.getIsCompilationUnitAttr();
      break;
    default:
      return emitOpError("kind cannot be a persistent lexical source anchor");
    }
    if (!legal)
      return emitOpError("has an illegal lexical parent kind: child ")
             << getVpiKind() << ", parent " << getVPILexicalOwnerKind(owner);
  } else {
    using Kind = reflection::VPIObjectKind;
    Kind kind = static_cast<Kind>(getVpiKind());
    if (kind != Kind::Package && kind != Kind::Module &&
        kind != Kind::Interface && kind != Kind::Program)
      return emitOpError("kind requires a lexical parent anchor");
    if (getMemberIndicesAttr())
      return emitOpError(
          "member_indices requires a fixed or generate array parent");
  }
  VPIObjectBackingAttr backing = getBackingAttr();
  if (backing && backing.getKind() == VPIObjectBackingKind::Class) {
    SimClassDeclOp target = symbolTable.lookupNearestSymbolFrom<SimClassDeclOp>(
        *this, backing.getSymbol());
    if (!target)
      return emitOpError("references an unknown backing class declaration");
  }
  if (auto methodRef = (*this)->getAttrOfType<FlatSymbolRefAttr>("class_method")) {
    auto method = symbolTable.lookupNearestSymbolFrom<SimClassMethodDeclOp>(
        *this, methodRef);
    if (!method)
      return emitOpError("references an unknown class method declaration");
    uint32_t expected = static_cast<uint32_t>(
        method.getIsTask() ? reflection::VPIObjectKind::Task
                           : reflection::VPIObjectKind::Function);
    if (getVpiKind() != expected)
      return emitOpError("kind does not match its class method declaration");
    Operation *owner = getParentAttr()
                           ? lookupVPILexicalOwner(*this, getParentAttr(),
                                                  symbolTable)
                           : nullptr;
    if (auto typespec = dyn_cast_or_null<SimVPITypespecDeclOp>(owner))
      if (typespec.getTargetType().getSymbol() != method.getOwnerAttr())
        return emitOpError(
            "class method owner does not match its parent specialization");
  }
  return verifyFixedReflection(*this, getVpiKind(), reflectionProperties(*this),
                               reflectionDefinitionLoc(*this));
}

LogicalResult SimVPIBoxTypeDeclOp::verify() {
  if (getId() <= 0)
    return emitOpError("box type ID must be positive");
  if (!isa<SimDesignOp>((*this)->getParentOp()))
    return emitOpError("must be directly contained in a simulation design");
  auto emit = [&] { return emitOpError(); };
  if (!getElementDescriptor(getValueType()))
    return emitOpError("requires exact concrete value storage");
  return verifyVPITypeSemantics(emit, getValueType(), getVpiType());
}

LogicalResult
SimVPIBoxTypeDeclOp::verifySymbolUses(SymbolTableCollection &symbols) {
  SmallVector<VPITypeSemanticsAttr> pending{getVpiType()};
  DenseSet<Attribute> visited;
  while (!pending.empty()) {
    auto semantic = pending.pop_back_val();
    if (!visited.insert(semantic).second)
      continue;
    if (auto symbol = semantic.getSymbol();
        symbol && !symbols.lookupNearestSymbolFrom(*this, symbol))
      return emitOpError("references an unknown semantic type symbol ")
             << symbol;
    if (ArrayAttr aliases = semantic.getTypedefAliases())
      for (Attribute alias : aliases)
        if (!symbols.lookupNearestSymbolFrom<SimVPITypespecDeclOp>(
                *this, cast<SymbolRefAttr>(alias)))
          return emitOpError("references an unknown typedef symbol ") << alias;
    for (Attribute child : semantic.getChildren())
      pending.push_back(cast<VPITypeSemanticsAttr>(child));
  }
  return success();
}

LogicalResult
SimBoxBindTypeOp::verifySymbolUses(SymbolTableCollection &symbols) {
  auto descriptor = symbols.lookupNearestSymbolFrom<SimVPIBoxTypeDeclOp>(
      *this, getDescriptorAttr());
  if (!descriptor)
    return emitOpError("descriptor must name a VPI box type declaration");
  if (cast<DynamicArrayType>(getInput().getType()).getElementType() !=
      descriptor.getValueType())
    return emitOpError("input element type must exactly match the descriptor");
  return success();
}

static LogicalResult verifySourceClassInventory(Operation *op, IntegerAttr id,
                                                IntegerAttr scope) {
  if (!isa<SimDesignOp>(op->getParentOp()))
    return op->emitOpError("requires a simulation.design owner");
  if (failed(verifyNonnegative(op, id, "source inventory ID")) ||
      failed(verifyNonnegative(op, scope, "scope ID")))
    return failure();
  for (StringRef name :
       {"vpi_type", "target_type", "constant_value", "vpi_properties"})
    if (op->hasAttr(name))
      return op->emitOpError("source inheritance cannot carry an effective "
                             "type, value, or arbitrary properties");
  return success();
}

static LogicalResult verifySourceClassSyntax(Operation *op, StringAttr syntax) {
  if (syntax && (syntax.getValue().empty() || syntax.getValue().contains('\0')))
    return op->emitOpError("unavailable source syntax must be nonempty without "
                           "NUL bytes");
  return success();
}

static LogicalResult verifySourceClassOwner(Operation *op,
                                            FlatSymbolRefAttr reference,
                                            uint64_t scope,
                                            SymbolTableCollection &symbols) {
  auto owner =
      symbols.lookupNearestSymbolFrom<SimVPIObjectAnchorOp>(op, reference);
  if (!owner || owner.getVpiKind() !=
                    static_cast<uint32_t>(reflection::VPIObjectKind::ClassDefn))
    return op->emitOpError("owner must be a lexical class definition");
  if (owner.getEnclosingScopeId() != scope)
    return op->emitOpError("scope ID must match the lexical class owner");
  return success();
}

LogicalResult SimVPILexicalClassTypespecDeclOp::verify() {
  if (failed(
          verifySourceClassInventory(*this, getIdAttr(), getScopeIdAttr())) ||
      failed(verifySourceClassSyntax(*this, getUnavailableParametersAttr())))
    return failure();
  if (getDebugName().empty() || getDebugName().contains('\0'))
    return emitOpError(
        "requires the exact source class name without NUL bytes");
  return success();
}

LogicalResult SimVPILexicalClassTypespecDeclOp::verifySymbolUses(
    SymbolTableCollection &symbols) {
  if (failed(
          verifySourceClassOwner(*this, getOwnerAttr(), getScopeId(), symbols)))
    return failure();
  auto definition = symbols.lookupNearestSymbolFrom<SimVPIObjectAnchorOp>(
      *this, getDefinitionAttr());
  if (!definition ||
      definition.getVpiKind() !=
          static_cast<uint32_t>(reflection::VPIObjectKind::ClassDefn))
    return emitOpError("definition must name a lexical class definition");
  if (getDebugName() != definition.getDebugName())
    return emitOpError("debug name must match the exact class declaration");
  if (getOwnerAttr() == getDefinitionAttr())
    return emitOpError("class cannot extend its own lexical definition");
  return success();
}

LogicalResult SimVPIClassExtendsDeclOp::verify() {
  if (failed(
          verifySourceClassInventory(*this, getIdAttr(), getScopeIdAttr())) ||
      failed(verifySourceClassSyntax(*this, getUnavailableBaseAttr())) ||
      failed(verifySourceClassSyntax(*this, getUnavailableArgumentsAttr())))
    return failure();
  if (bool(getBaseTypespecAttr()) == bool(getUnavailableBaseAttr()))
    return emitOpError(
        "requires exactly one base typespec or unavailable base");
  return success();
}

LogicalResult
SimVPIClassExtendsDeclOp::verifySymbolUses(SymbolTableCollection &symbols) {
  if (failed(
          verifySourceClassOwner(*this, getOwnerAttr(), getScopeId(), symbols)))
    return failure();
  if (auto reference = getBaseTypespecAttr()) {
    auto type =
        symbols.lookupNearestSymbolFrom<SimVPILexicalClassTypespecDeclOp>(
            *this, reference);
    if (!type || type.getOwnerAttr() != getOwnerAttr() ||
        type.getScopeId() != getScopeId())
      return emitOpError("base typespec must be the lexical type occurrence "
                         "owned by this class");
  }
  return success();
}

LogicalResult SimVPIClassParameterAssignDeclOp::verify() {
  if (failed(verifySourceClassInventory(*this, getIdAttr(), getScopeIdAttr())))
    return failure();
  if (failed(verifySourceClassSyntax(*this, getUnavailableRhsAttr())))
    return failure();
  if (getRhsAttr() && getUnavailableRhsAttr())
    return emitOpError(
        "explicit RHS and unavailable RHS are mutually exclusive");
  return verifyNonnegative(*this, getOrdinalAttr(),
                           "parameter assignment ordinal");
}

LogicalResult SimVPIClassParameterAssignDeclOp::verifySymbolUses(
    SymbolTableCollection &symbols) {
  auto owner =
      symbols.lookupNearestSymbolFrom<SimVPILexicalClassTypespecDeclOp>(
          *this, getOwnerAttr());
  if (!owner || owner.getScopeId() != getScopeId() ||
      owner.getUnavailableParametersAttr())
    return emitOpError(
        "owner must be a lexical class type with known formal assignments "
        "in the same scope");
  auto parameter = symbols.lookupNearestSymbolFrom<SimVPIObjectAnchorOp>(
      *this, getParameterAttr());
  using Kind = reflection::VPIObjectKind;
  if (!parameter ||
      (parameter.getVpiKind() != static_cast<uint32_t>(Kind::Parameter) &&
       parameter.getVpiKind() != static_cast<uint32_t>(Kind::TypeParameter)) ||
      parameter.getParentAttr() != owner.getDefinitionAttr())
    return emitOpError(
        "parameter must be declared in the exact base definition");
  if (auto properties = reflectionProperties(parameter))
    for (Attribute attribute : properties.getProperties()) {
      auto property = cast<VPIPropertyAttr>(attribute);
      if (property.getSelector().getValue().getZExtValue() == 70 &&
          cast<BoolAttr>(property.getValue()).getValue())
        return emitOpError(
            "local parameter cannot have a class actual assignment");
    }
  if (auto rhs = getRhsAttr()) {
    Operation *target = symbols.lookupNearestSymbolFrom(*this, rhs);
    auto anchor = dyn_cast_or_null<SimVPIObjectAnchorOp>(target);
    bool typeParameter =
        parameter.getVpiKind() == static_cast<uint32_t>(Kind::TypeParameter);
    if (auto concrete = dyn_cast_or_null<SimVPITypespecDeclOp>(target)) {
      if (!typeParameter ||
          concrete.getOrigin() != VPITypespecOrigin::SourceDefault ||
          concrete.getOwnerAttr() != FlatSymbolRefAttr::get(getSymNameAttr()) ||
          concrete.getScopeId() != getScopeId())
        return emitOpError(
            "type actual must preserve its exact owned source typespec");
    } else if (anchor &&
               anchor.getVpiKind() ==
                   static_cast<uint32_t>(typeParameter ? Kind::TypeParameter
                                                       : Kind::Parameter)) {
      if (anchor.getParentAttr() != owner.getOwnerAttr())
        return emitOpError(
            "actual reference must name a parameter in the derived class");
      if (auto properties = reflectionProperties(anchor))
        for (Attribute attribute : properties.getProperties()) {
          auto property = cast<VPIPropertyAttr>(attribute);
          if (property.getSelector().getValue().getZExtValue() == 70 &&
              cast<BoolAttr>(property.getValue()).getValue())
            return emitOpError(
                "actual reference cannot name a body local parameter");
        }
    } else if (!typeParameter && anchor &&
               (anchor.getVpiKind() == static_cast<uint32_t>(Kind::Constant) ||
                anchor.getVpiKind() ==
                    static_cast<uint32_t>(Kind::Operation)) &&
               anchor.getParentAttr() ==
                   FlatSymbolRefAttr::get(getSymNameAttr()) &&
               anchor.getOwnerOrdinal() == 0 &&
               anchor.getEnclosingScopeId() == getScopeId()) {
      // The design-level expression proof validates the entire occurrence tree.
    } else {
      return emitOpError(
          "explicit RHS must match the exact formal parameter kind");
    }
  }
  return success();
}

LogicalResult SimVPIUnresolvedTypeAliasDeclOp::verify() {
  if (!isa<SimDesignOp>((*this)->getParentOp()))
    return emitOpError("requires a simulation.design owner");
  if (failed(verifyNonnegative(*this, getIdAttr(), "typespec ID")) ||
      failed(verifyNonnegative(*this, getScopeIdAttr(), "scope ID")))
    return failure();
  if (getDebugName().empty() || getHierarchicalName().empty() ||
      getDebugName().contains('\0') || getHierarchicalName().contains('\0'))
    return emitOpError(
        "requires nonempty source typedef names without NUL bytes");
  if ((*this)->hasAttr("vpi_type") || (*this)->hasAttr("target_type") ||
      (*this)->hasAttr("constant_value") || (*this)->hasAttr("vpi_properties"))
    return emitOpError("unresolved typedef cannot carry an effective type or "
                       "parameter payload");
  return success();
}

LogicalResult SimVPIUnresolvedTypeAliasDeclOp::verifySymbolUses(
    SymbolTableCollection &symbols) {
  auto owner = symbols.lookupNearestSymbolFrom<SimVPIObjectAnchorOp>(
      *this, getOwnerAttr());
  if (!owner || owner.getVpiKind() !=
                    static_cast<uint32_t>(reflection::VPIObjectKind::ClassDefn))
    return emitOpError("owner must be a lexical class definition");
  if (owner.getEnclosingScopeId() != getScopeId())
    return emitOpError("scope ID must match the lexical class owner");
  Operation *target = symbols.lookupNearestSymbolFrom(*this, getTargetAttr());
  FlatSymbolRefAttr targetOwner;
  if (auto parameter = dyn_cast_or_null<SimVPIObjectAnchorOp>(target)) {
    if (parameter.getVpiKind() !=
            static_cast<uint32_t>(reflection::VPIObjectKind::TypeParameter) ||
        !parameter.getUnresolvedTypeParameterAttr())
      return emitOpError(
          "target must be an unresolved type parameter or alias");
    targetOwner = parameter.getParentAttr();
  } else if (auto alias =
                 dyn_cast_or_null<SimVPIUnresolvedTypeAliasDeclOp>(target)) {
    targetOwner = alias.getOwnerAttr();
  } else {
    return emitOpError("target must be an unresolved type parameter or alias");
  }
  if (targetOwner != getOwnerAttr())
    return emitOpError("target must belong to the same lexical class");
  if (target == getOperation() ||
      target->getBlock() != getOperation()->getBlock() ||
      !target->isBeforeInBlock(getOperation()))
    return emitOpError("target must precede the typedef declaration");
  return success();
}

LogicalResult SimVPITypespecDeclOp::verify() {
  if (bool(getOwnerAttr()) == bool(getStatementScopeIdAttr()))
    return emitOpError(
        "requires exactly one symbolic or statement-scope owner");
  if (getStatementScopeIdAttr()) {
    if (failed(verifyNonnegative(*this, getStatementScopeIdAttr(),
                                 "statement-scope ID")))
      return failure();
    if (getOrigin() != VPITypespecOrigin::Typedef &&
        getOrigin() != VPITypespecOrigin::AnonymousEnum)
      return emitOpError("statement-scope ownership is only valid for typedef "
                         "and anonymous enum typespecs");
  }
  if (failed(verifyNonnegative(*this, getIdAttr(), "typespec ID")) ||
      failed(verifyNonnegative(*this, getScopeIdAttr(), "scope ID")) ||
      (getSourceTypeIdentityAttr() &&
       failed(verifyNonnegative(*this, getSourceTypeIdentityAttr(),
                                "source type identity"))))
    return failure();
  if (getOrigin() == VPITypespecOrigin::SourceDefault) {
    if (!getHierarchicalName().empty() || !getDebugName().empty())
      return emitOpError("source default typespec must be unnamed");
  } else if (getHierarchicalName().empty() || getDebugName().empty()) {
    return emitOpError("requires nonempty source names");
  }
  if (getOrigin() == VPITypespecOrigin::Interface) {
    if (getTargetType().getKind() != VPITypeKind::VirtualInterface)
      return emitOpError("interface typespec origin requires an interface");
    SymbolRefAttr symbol = getTargetType().getSymbol();
    if (!symbol || !symbol.getNestedReferences().empty() ||
        symbol.getRootReference() != getSymNameAttr())
      return emitOpError(
          "raw interface typespec identity must exactly reference itself");
    if (!getTargetType().getName() ||
        getTargetType().getName().getValue() != getHierarchicalName())
      return emitOpError(
          "raw interface hierarchy must equal its specialization identity");
  }
  if (getOrigin() == VPITypespecOrigin::ClassSpecialization &&
      (getTargetType().getKind() != VPITypeKind::Class ||
       !getTargetType().getSymbol() ||
       !getTargetType().getSymbol().getNestedReferences().empty() ||
       (getTargetType().getTypedefAliases() &&
        !getTargetType().getTypedefAliases().empty())))
    return emitOpError(
        "class-specialization origin requires an exact class symbol and no "
        "typedef aliases");
  if (getOrigin() == VPITypespecOrigin::BuiltinClass &&
      ((getTargetType().getKind() != VPITypeKind::Process &&
        getTargetType().getKind() != VPITypeKind::Semaphore &&
        getTargetType().getKind() != VPITypeKind::Mailbox) ||
       getTargetType().getSymbol() ||
       stripVPITypedefAliases(getTargetType()) != getTargetType()))
    return emitOpError(
        "builtin-class origin requires an unaliased process, semaphore, or "
        "mailbox type");
  if (TypeAttr message = getMessageTypeAttr()) {
    if (getOrigin() != VPITypespecOrigin::BuiltinClass ||
        getTargetType().getKind() != VPITypeKind::Mailbox)
      return emitOpError(
          "message_type requires a canonical builtin mailbox typespec");
    auto semantic =
        cast<VPITypeSemanticsAttr>(getTargetType().getChildren()[0]);
    if (failed(verifyVPITypeSemantics([&] { return emitOpError(); },
                                      message.getValue(), semantic)))
      return failure();
    if (!getElementDescriptor(message.getValue()))
      return emitOpError("message_type requires supported element storage");
    // The executable i1 representation of void is only a tagged-union layout
    // placeholder. It does not introduce a value into the message type.
    SmallVector<VPITypeSemanticsAttr> pending{semantic};
    llvm::DenseSet<Attribute> visited;
    if (semantic.getKind() == VPITypeKind::Void)
      return emitOpError("mailbox void storage requires a tagged union member");
    while (!pending.empty()) {
      auto parent = pending.pop_back_val();
      if (!visited.insert(parent).second)
        continue;
      for (Attribute attribute : parent.getChildren()) {
        auto child = cast<VPITypeSemanticsAttr>(attribute);
        if (child.getKind() == VPITypeKind::Void &&
            ((parent.getKind() != VPITypeKind::PackedUnion &&
              parent.getKind() != VPITypeKind::UnpackedUnion) ||
             !parent.getIsTagged().getValue()))
          return emitOpError(
              "mailbox void storage requires a tagged union member");
        pending.push_back(child);
      }
    }
  }
  if (getOrigin() == VPITypespecOrigin::AnonymousEnum &&
      (getTargetType().getKind() != VPITypeKind::Enum ||
       !getSourceTypeIdentityAttr()))
    return emitOpError(
        "anonymous-enum origin requires an enum typespec and exact source "
        "type identity");
  auto kind = vpiKindForTypespec(getTargetType());
  if (!kind)
    return emitOpError("declared data type has no concrete VPI typespec");
  return verifyFixedReflection(*this, *kind, reflectionProperties(*this),
                               reflectionDefinitionLoc(*this));
}

LogicalResult SimVPITerminalDriverBindingOp::verify() {
  if (!isa<SimDesignOp>((*this)->getParentOp()))
    return emitOpError("requires a simulation.design owner");
  if (failed(verifyNonnegative(*this, getLowDriverIdAttr(), "low driver ID")) ||
      failed(
          verifyNonnegative(*this, getBitOffsetAttr(), "driver bit offset")) ||
      (getHighDriverIdAttr() &&
       failed(
           verifyNonnegative(*this, getHighDriverIdAttr(), "high driver ID"))))
    return failure();
  if (getBitOffset() > UINT32_MAX)
    return emitOpError("driver bit offset exceeds the 32-bit reflection field");
  for (auto other : (*this)
                        ->getParentOp()
                        ->getRegion(0)
                        .getOps<SimVPITerminalDriverBindingOp>())
    if (other != *this && other.getTerminalAttr() == getTerminalAttr())
      return emitOpError("duplicates a primitive output binding");
  return success();
}

LogicalResult SimVPITerminalDriverBindingOp::verifySymbolUses(
    SymbolTableCollection &symbols) {
  auto terminal = symbols.lookupNearestSymbolFrom<SimVPIObjectAnchorOp>(
      *this, getTerminalAttr());
  using Kind = reflection::VPIObjectKind;
  if (!terminal || terminal->getParentOp() != (*this)->getParentOp() ||
      terminal.getVpiKind() != static_cast<uint32_t>(Kind::PrimTerm) ||
      !terminal.getParentAttr())
    return emitOpError(
        "terminal must name a primitive terminal in this design");
  auto primitive = symbols.lookupNearestSymbolFrom<SimVPIObjectAnchorOp>(
      terminal, terminal.getParentAttr());
  if (!primitive ||
      (primitive.getVpiKind() != static_cast<uint32_t>(Kind::Gate) &&
       primitive.getVpiKind() != static_cast<uint32_t>(Kind::Udp)))
    return emitOpError("requires a gate or UDP output terminal");
  bool output = false;
  if (auto properties = reflectionProperties(terminal))
    for (Attribute attribute : properties.getProperties()) {
      auto property = cast<VPIPropertyAttr>(attribute);
      if (property.getSelector().getValue().getZExtValue() == 20)
        if (auto direction = dyn_cast<IntegerAttr>(property.getValue()))
          output = direction.getValue().getZExtValue() == 2;
    }
  if (!output)
    return emitOpError("terminal must retain output direction");
  // The retained primitive subtype determines whether one logical output uses
  // an ordinary driver or the two complementary conditional-gate banks.
  uint32_t primitiveType = 0;
  if (primitive.getVpiKind() == static_cast<uint32_t>(Kind::Gate)) {
    if (auto properties = reflectionProperties(primitive))
      for (Attribute attribute : properties.getProperties()) {
        auto property = cast<VPIPropertyAttr>(attribute);
        if (property.getSelector().getValue().getZExtValue() == 33)
          if (auto value = dyn_cast<IntegerAttr>(property.getValue()))
            primitiveType = value.getValue().getZExtValue();
      }
    for (auto binding : (*this)
                            ->getParentOp()
                            ->getRegion(0)
                            .getOps<SimVPIDefinitionBindingOp>())
      if (binding.getSource() == primitive.getSymName()) {
        auto definition =
            symbols.lookupNearestSymbolFrom<SimVPIDefinitionDeclOp>(
                binding, binding.getDefinitionAttr());
        if (definition)
          primitiveType = definition.getPrimitiveType().value_or(0);
      }
    const auto *descriptor = reflection::findVPIBuiltinPrimitive(primitiveType);
    if (!descriptor ||
        descriptor->objectKind != static_cast<uint32_t>(Kind::Gate))
      return emitOpError("requires the exact retained gate primitive type");
  }
  bool conditional = primitiveType >= 9 && primitiveType <= 12;
  if (conditional != bool(getHighDriverId()))
    return emitOpError("driver bank count must match the primitive subtype");
  SimDriverDeclOp low, high;
  for (auto driver :
       (*this)->getParentOp()->getRegion(0).getOps<SimDriverDeclOp>()) {
    if (driver.getId() == getLowDriverId())
      low = driver;
    if (getHighDriverId() && driver.getId() == *getHighDriverId())
      high = driver;
  }
  auto valid = [&](SimDriverDeclOp driver) {
    auto width = driver ? getPackedWidth(driver.getType()) : std::nullopt;
    uint64_t begin = driver ? driver.getDrivenLow().value_or(0) : 0;
    uint64_t extent =
        driver ? driver.getDrivenWidth().value_or(width.value_or(0)) : 0;
    return driver && width && driver.getLifetime() == Lifetime::Design &&
           driver.getScopeId() == terminal.getEnclosingScopeId() &&
           getBitOffset() < *width && getBitOffset() >= begin &&
           getBitOffset() - begin < extent;
  };
  if (!valid(low) || (getHighDriverId() && (!valid(high) || low == high)))
    return emitOpError("drivers must contain the selected bit in the "
                       "terminal's physical scope");
  auto lowBank = get<metadata::StrengthBank>(low);
  auto lowGroup = get<metadata::StrengthGroup>(low);
  if (!getHighDriverId()) {
    if (lowBank || lowGroup)
      return emitOpError(
          "a polarity bank requires its exact complementary driver");
  } else {
    auto highBank = get<metadata::StrengthBank>(high);
    auto highGroup = get<metadata::StrengthGroup>(high);
    if (!lowBank || !highBank || !lowGroup || !highGroup ||
        lowBank.getInt() != 0 || highBank.getInt() != 1 ||
        lowGroup != highGroup || low.getType() != high.getType() ||
        low.getNetId() != high.getNetId() ||
        low.getDrivenLow() != high.getDrivenLow() ||
        low.getDrivenWidth() != high.getDrivenWidth() ||
        low.getStrength1() != Strength::HighZ ||
        high.getStrength0() != Strength::HighZ)
      return emitOpError("drivers must be one exact low/high polarity pair");
  }
  return success();
}

LogicalResult SimVPIEventBindingOp::verify() {
  if (!isa<SimDesignOp>((*this)->getParentOp()))
    return emitOpError("requires a simulation.design owner");
  if (failed(verifyNonnegative(*this, getEventIdAttr(), "event ID")))
    return failure();
  for (auto other :
       (*this)->getParentOp()->getRegion(0).getOps<SimVPIEventBindingOp>())
    if (other != *this && other.getEventAttr() == getEventAttr())
      return emitOpError("duplicates a named-event binding");
  return success();
}

LogicalResult
SimVPIEventBindingOp::verifySymbolUses(SymbolTableCollection &symbols) {
  auto anchor = symbols.lookupNearestSymbolFrom<SimVPIObjectAnchorOp>(
      *this, getEventAttr());
  if (!anchor || anchor->getParentOp() != (*this)->getParentOp() ||
      anchor.getVpiKind() !=
          static_cast<uint32_t>(
              obelisk::reflection::VPIObjectKind::NamedEvent) ||
      anchor.getBackingAttr() || anchor.getVpiTypeAttr() ||
      anchor.getMemberIndicesAttr())
    return emitOpError("event must name an unbacked scalar named-event anchor "
                       "in the same design");
  return success();
}

LogicalResult SimVPIClassMemberDeclOp::verify() {
  if (failed(verifyNonnegative(*this, getIdAttr(), "VPI source inventory ID")) ||
      failed(verifyNonnegative(*this, getScopeIdAttr(), "scope ID")) ||
      (getStaticStorageAttr() &&
       failed(verifyNonnegative(*this, getStaticStorageAttr(), "static storage ID"))) ||
      (getStaticEventAttr() &&
       failed(verifyNonnegative(*this, getStaticEventAttr(), "static event ID"))))
    return failure();
  if (getHierarchicalName().empty() || getDebugName().empty())
    return emitOpError("requires nonempty hierarchy and property names");
  if (getStaticStorageAttr() && getStaticEventAttr())
    return emitOpError("static storage and event bindings are mutually exclusive");
  if (!getFieldAttr() && (getStaticStorageAttr() || getStaticEventAttr()))
    return emitOpError("a lexical property cannot have executable backing");
  // An unresolved type parameter has no generic public variable kind. Do not
  // manufacture a reg declaration from the storage helper's legacy fallback.
  if (!getFieldAttr() && (!vpiKindForTypespec(getVpiType()) ||
                          getVpiType().getKind() == VPITypeKind::Void ||
                          getVpiType().getKind() == VPITypeKind::Sequence ||
                          getVpiType().getKind() == VPITypeKind::Property))
    return emitOpError("class property requires a concrete declared type");
  if (auto properties = reflectionProperties(*this))
    for (Attribute attribute : properties.getProperties()) {
      auto property = cast<VPIPropertyAttr>(attribute);
      if (property.getSelector().getValue().getZExtValue() == 2 &&
          property.getValue() != getDebugNameAttr())
        return emitOpError(
            "vpiName must match the exact property declaration name");
    }
  return verifyFixedReflection(*this, vpiKindForClassMember(getVpiType()),
                               reflectionProperties(*this),
                               reflectionDefinitionLoc(*this));
}

LogicalResult SimVPIClassMemberDeclOp::verifySymbolUses(
    SymbolTableCollection &symbolTable) {
  auto field = getFieldAttr()
                   ? symbolTable.lookupNearestSymbolFrom<SimClassFieldDeclOp>(
                         *this, getFieldAttr())
                   : SimClassFieldDeclOp{};
  if (getFieldAttr() && !field)
    return emitOpError("references an unknown class field");
  Operation *owner = symbolTable.lookupNearestSymbolFrom(*this, getOwnerAttr());
  uint64_t ownerScope = UINT64_MAX;
  if (auto anchor = dyn_cast_or_null<SimVPIObjectAnchorOp>(owner)) {
    if (anchor.getVpiKind() !=
        static_cast<uint32_t>(reflection::VPIObjectKind::ClassDefn))
      return emitOpError("owner anchor must be a class definition");
    ownerScope = anchor.getEnclosingScopeId();
  } else if (auto typespec = dyn_cast_or_null<SimVPITypespecDeclOp>(owner)) {
    if (!field ||
        typespec.getOrigin() != VPITypespecOrigin::ClassSpecialization ||
        typespec.getTargetType().getSymbol() != field.getOwnerAttr())
      return emitOpError("owner must be the field's canonical class specialization");
    ownerScope = typespec.getScopeId();
  } else {
    return emitOpError("owner must name a class definition or specialization");
  }
  if (ownerScope != getScopeId())
    return emitOpError("scope ID does not match the class owner");
  if (!field)
    return success();
  if (field.getIsStatic() !=
      static_cast<bool>(getStaticStorageAttr() || getStaticEventAttr()))
    return emitOpError("static class fields require exactly one static backing");
  if (getStaticEventAttr() && getVpiType().getKind() != VPITypeKind::Event)
    return emitOpError("static event backing requires a scalar named event");
  return verifyVPITypeSemantics([&] { return emitOpError(); }, field.getType(),
                                getVpiType());
}

LogicalResult SimVPINettypeDeclOp::verify() {
  if (failed(verifyNonnegative(*this, getIdAttr(), "nettype ID")) ||
      failed(verifyNonnegative(*this, getScopeIdAttr(), "scope ID")))
    return failure();
  if (getHierarchicalName().empty() || getDebugName().empty())
    return emitOpError("requires nonempty source names");
  std::optional<uint32_t> kind = vpiKindForTypespec(getTargetType());
  if (!kind)
    return emitOpError("declared data type has no concrete VPI typespec");
  if (getDirectAliasAttr() == FlatSymbolRefAttr::get(getSymNameAttr()))
    return emitOpError("cannot directly alias itself");
  return verifyFixedReflection(
      *this, static_cast<uint16_t>(reflection::VPIObjectKind::NettypeDecl),
      reflectionProperties(*this), reflectionDefinitionLoc(*this));
}

LogicalResult
SimVPINettypeDeclOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  Operation *owner = lookupVPILexicalOwner(*this, getOwnerAttr(), symbolTable);
  if (!owner)
    return emitOpError("nettype must be owned by a VPI anchor");
  if (getVPILexicalOwnerScope(owner) != getScopeId())
    return emitOpError("scope ID does not match the owner anchor");
  if (!reflection::findVPITraversal(
          getVPILexicalOwnerKind(owner),
          static_cast<uint32_t>(reflection::VPIRelationKind::NetTypedefRel),
          reflection::VPITraversalMode::Iterate))
    return emitOpError("owner kind does not support vpiNetTypedef traversal");
  if (FlatSymbolRefAttr alias = getDirectAliasAttr()) {
    SimVPINettypeDeclOp target =
        symbolTable.lookupNearestSymbolFrom<SimVPINettypeDeclOp>(*this, alias);
    if (!target)
      return emitOpError("references an unknown direct nettype alias");
    if (target.getTargetType() != getTargetType())
      return emitOpError(
          "direct nettype alias target has a different declared data type");
    if (target.getResolutionFunctionAttr() != getResolutionFunctionAttr())
      return emitOpError(
          "direct nettype alias must preserve the effective resolution "
          "function");
  }
  if (FlatSymbolRefAttr resolver = getResolutionFunctionAttr()) {
    SimVPIObjectAnchorOp target =
        symbolTable.lookupNearestSymbolFrom<SimVPIObjectAnchorOp>(*this,
                                                                  resolver);
    if (!target ||
        target.getVpiKind() !=
            static_cast<uint32_t>(reflection::VPIObjectKind::Function))
      return emitOpError("resolution function must reference a function VPI "
                         "anchor");
  }
  return success();
}

static bool hasUserDefinedNetSubtype(Operation *operation) {
  return hasNetSubtype(operation, 14); // vpiNettypeNet
}

static bool matchesDeclaredNettype(VPITypeSemanticsAttr declaration,
                                   VPITypeSemanticsAttr nettype) {
  // IEEE 1800-2023 6.7.2 permits declaration-added unpacked dimensions on a
  // user-defined nettype. Stop as soon as the remaining semantic subtree is
  // the nettype's exact data type so a nettype whose data type is itself an
  // unpacked array is not stripped accidentally.
  while (declaration && declaration != nettype &&
         declaration.getKind() == VPITypeKind::UnpackedArray)
    declaration = cast<VPITypeSemanticsAttr>(declaration.getChildren()[0]);
  return declaration == nettype;
}

LogicalResult
SimVPINetIdentityDeclOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  FlatSymbolRefAttr nettype = getNettypeAttr();
  if (!nettype)
    return success();
  SimVPINettypeDeclOp target =
      symbolTable.lookupNearestSymbolFrom<SimVPINettypeDeclOp>(*this, nettype);
  if (!target)
    return emitOpError("references an unknown user-defined nettype");
  if (!matchesDeclaredNettype(getVpiTypeAttr(), target.getTargetType()))
    return emitOpError(
        "VPI type semantics do not match the referenced nettype");
  if (!hasUserDefinedNetSubtype(*this))
    return emitOpError(
        "user-defined nettype reference requires vpiNettypeNet subtype");
  return success();
}

LogicalResult SimVPIEnumConstDeclOp::verify() {
  if (failed(verifyNonnegative(*this, getIdAttr(), "enum-constant ID")) ||
      failed(verifyNonnegative(*this, getOrdinalAttr(), "enum ordinal")))
    return failure();
  if (getName().empty() || getName().contains('\0') || getValue().empty())
    return emitOpError("requires a nonempty NUL-free name and constant value");
  return verifyFixedReflection(
      *this, static_cast<uint16_t>(reflection::VPIObjectKind::EnumConst),
      reflectionProperties(*this), reflectionDefinitionLoc(*this));
}

LogicalResult
SimVPIEnumConstDeclOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  SimVPITypespecDeclOp target =
      symbolTable.lookupNearestSymbolFrom<SimVPITypespecDeclOp>(
          *this, getEnumTypespecAttr());
  if (!target || target.getTargetType().getKind() != VPITypeKind::Enum)
    return emitOpError("references an unknown or non-enum typespec");
  if (auto value = getImmutableValueAttr()) {
    if (!getPackedScalarType(value.getType()) ||
        value.getIsSigned() != target.getTargetType().getIsSigned())
      return emitOpError(
          "immutable enum value must match its packed enum base");
    if (failed(verifyVPITypeSemantics([&] { return emitOpError(); },
                                      value.getType(), target.getTargetType())))
      return failure();
  }
  return success();
}

LogicalResult
SimVPITypespecDeclOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  if (getStatementScopeIdAttr()) {
    auto design = (*this)->getParentOfType<SimDesignOp>();
    if (!design)
      return emitOpError("statement owner requires a simulation.design");
    for (auto statement : design.getOps<SimStatementDeclOp>()) {
      if (statement.getId() != *getStatementScopeId())
        continue;
      if (!statement.getCodeUnitId() || !statement.getIsScope().value_or(false))
        return emitOpError("owner must name a procedural statement scope");
      if (statement.getScopeId() != getScopeId())
        return emitOpError("scope ID does not match the statement owner");
      if (getOrigin() == VPITypespecOrigin::Typedef &&
          !reflection::findVPITraversal(statement.getVpiKind(), 725,
                                        reflection::VPITraversalMode::Iterate))
        return emitOpError("owner kind does not support vpiTypedef traversal");
      return success();
    }
    return emitOpError("references an unknown statement-scope owner");
  }
  Operation *owner = lookupVPILexicalOwner(*this, getOwnerAttr(), symbolTable);
  if (!owner)
    return emitOpError("typespec must be owned by a VPI anchor");
  if (getVPILexicalOwnerScope(owner) != getScopeId())
    return emitOpError("scope ID does not match the owner anchor");
  if (isa<SimVPIClassParameterAssignDeclOp>(owner) &&
      getOrigin() != VPITypespecOrigin::SourceDefault)
    return emitOpError("class actual owner requires source-default origin");
  if (getOrigin() == VPITypespecOrigin::SourceDefault) {
    auto parameter = dyn_cast<SimVPIObjectAnchorOp>(owner);
    auto assignment = dyn_cast<SimVPIClassParameterAssignDeclOp>(owner);
    if (assignment) {
      parameter = symbolTable.lookupNearestSymbolFrom<SimVPIObjectAnchorOp>(
          assignment, assignment.getParameterAttr());
      if (assignment.getRhsAttr() != FlatSymbolRefAttr::get(getSymNameAttr()))
        return emitOpError(
            "source actual typespec must be its owner's exact RHS");
    }
    if (!parameter ||
        parameter.getVpiKind() !=
            static_cast<uint32_t>(reflection::VPIObjectKind::TypeParameter))
      return emitOpError(
          "source default requires a type parameter or type actual owner");
  }
  if (getOrigin() == VPITypespecOrigin::ClassSpecialization) {
    if (getVPILexicalOwnerKind(owner) !=
        static_cast<uint32_t>(reflection::VPIObjectKind::ClassDefn))
      return emitOpError(
          "class specialization requires a class-definition owner");
    if (!symbolTable.lookupNearestSymbolFrom<SimClassDeclOp>(
            *this, getTargetType().getSymbol()))
      return emitOpError("class specialization references an unknown class");
  }
  if (getOrigin() == VPITypespecOrigin::Typedef &&
      !reflection::findVPITraversal(getVPILexicalOwnerKind(owner), 725,
                                    reflection::VPITraversalMode::Iterate))
    return emitOpError("owner kind does not support vpiTypedef traversal");
  return success();
}

LogicalResult SimStorageDeclOp::verify() {
  if (failed(verifyNonnegative(*this, getIdAttr(), "storage ID")) ||
      failed(verifyNonnegative(*this, getScopeIdAttr(), "scope ID")))
    return failure();
  auto emit = [&] { return emitOpError(); };
  if (failed(verifyElementType(emit, getType())))
    return failure();
  if (getVpiType() &&
      failed(verifyVPITypeSemantics(emit, getType(), *getVpiType())))
    return failure();
  if (failed(verifyFixedReflection(
          *this, static_cast<uint16_t>(reflection::VPIObjectKind::Reg),
          reflectionProperties(*this), reflectionDefinitionLoc(*this))))
    return failure();
  FlatSymbolRefAttr delegated = get<metadata::VpiIdentityDelegated>(*this);
  if (!delegated)
    return success();
  VPITypeSemanticsAttr semantic = getVpiTypeAttr();
  bool sawArray = false;
  while (semantic && semantic.getKind() == VPITypeKind::UnpackedArray) {
    sawArray = true;
    semantic = cast<VPITypeSemanticsAttr>(semantic.getChildren()[0]);
  }
  if (!sawArray || !semantic || semantic.getKind() != VPITypeKind::Event ||
      !getHierarchicalName())
    return emitOpError(
        "delegated VPI identity requires named unpacked event-array storage");
  return success();
}

LogicalResult SimNetDeclOp::verify() {
  if (failed(verifyNonnegative(*this, getIdAttr(), "net ID")) ||
      failed(verifyNonnegative(*this, getScopeIdAttr(), "scope ID")))
    return failure();
  if (getResolutionKind() == NetResolutionKind::TriReg &&
      !isEntirelyFourState(getType()))
    return emitOpError("trireg nets require an entirely four-state type");
  if (getResolutionKind() == NetResolutionKind::TriReg) {
    if (getChargeStrength() && *getChargeStrength() != Strength::Small &&
        *getChargeStrength() != Strength::Medium &&
        *getChargeStrength() != Strength::Large)
      return emitOpError(
          "trireg charge strength must be small, medium, or large");
  } else if (getChargeStrength()) {
    return emitOpError("only trireg nets may declare charge strength");
  }
  if (auto delays = getPropagationDelays()) {
    std::optional<unsigned> width = getPackedWidth(getType());
    if (auto floating = dyn_cast<FloatType>(getType()))
      width = floating.getWidth();
    if (!width ||
        (delays->size() != 3 && delays->size() != uint64_t{*width} * 3))
      return emitOpError(
          "propagation delays must contain one uniform triple or one triple "
          "per net bit");
    for (size_t index = 0; index != delays->size(); index += 3) {
      bool absent = (*delays)[index] == -1 && (*delays)[index + 1] == -1 &&
                    (*delays)[index + 2] == -1;
      if (absent)
        continue;
      // IEEE 1800-2017 28.16.2 assigns -1 in the third slot to omitted
      // trireg charge decay. A collapsed alias can carry this normalized
      // triple while retaining its declared non-trireg resolution kind, so
      // the design verifier checks its effective scalar component kind.
      if ((*delays)[index] < 0 || (*delays)[index + 1] < 0 ||
          (*delays)[index + 2] < -1)
        return emitOpError(
            "each net-delay triple must have nonnegative rise/fall delays "
            "and a nonnegative or -1 third delay");
    }
  }
  auto emit = [&] { return emitOpError(); };
  if (failed(verifyElementType(emit, getType())))
    return failure();
  if (getVpiType() &&
      failed(verifyVPITypeSemantics(emit, getType(), *getVpiType())))
    return failure();
  return verifyFixedReflection(
      *this, static_cast<uint16_t>(reflection::VPIObjectKind::Net),
      reflectionProperties(*this), reflectionDefinitionLoc(*this));
}

LogicalResult
SimNetDeclOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  FlatSymbolRefAttr nettype = getNettypeAttr();
  if (!nettype)
    return success();
  SimVPINettypeDeclOp target =
      symbolTable.lookupNearestSymbolFrom<SimVPINettypeDeclOp>(*this, nettype);
  if (!target)
    return emitOpError("references an unknown user-defined nettype");
  if (!getVpiTypeAttr())
    return emitOpError("user-defined nettype reference requires VPI type "
                       "semantics");
  if (!matchesDeclaredNettype(getVpiTypeAttr(), target.getTargetType()))
    return emitOpError(
        "VPI type semantics do not match the referenced nettype");
  if (!hasUserDefinedNetSubtype(*this) &&
      !hasNetSubtype(*this, 16)) // vpiInterconnect
    return emitOpError(
        "user-defined nettype reference requires vpiNettypeNet or "
        "vpiInterconnect subtype");
  return success();
}

LogicalResult SimNetConnectDeclOp::verify() {
  if (failed(verifyNonnegative(*this, getIdAttr(), "connection ID")) ||
      failed(verifyNonnegative(*this, getScopeIdAttr(), "scope ID")) ||
      failed(verifyNonnegative(*this, getLhsNetIdAttr(), "left net ID")) ||
      failed(verifyNonnegative(*this, getLhsOffsetAttr(), "left offset")) ||
      failed(verifyNonnegative(*this, getRhsNetIdAttr(), "right net ID")) ||
      failed(verifyNonnegative(*this, getRhsOffsetAttr(), "right offset")) ||
      failed(verifyNonnegative(*this, getWidthAttr(), "width")))
    return failure();
  if (getWidth() == 0)
    return emitOpError("width must be positive");
  return success();
}

LogicalResult SimPassSwitchDeclOp::verify() {
  if (failed(verifyNonnegative(*this, getIdAttr(), "pass-switch ID")) ||
      failed(verifyNonnegative(*this, getScopeIdAttr(), "scope ID")) ||
      failed(verifyNonnegative(*this, getLhsNetIdAttr(), "left net ID")) ||
      failed(verifyNonnegative(*this, getLhsOffsetAttr(), "left offset")) ||
      failed(verifyNonnegative(*this, getRhsNetIdAttr(), "right net ID")) ||
      failed(verifyNonnegative(*this, getRhsOffsetAttr(), "right offset")) ||
      failed(verifyNonnegative(*this, getWidthAttr(), "width")))
    return failure();
  if (getWidth() == 0)
    return emitOpError("width must be positive");
  if (Attribute resistive = (*this)->getAttr("resistive");
      resistive && !isa<BoolAttr>(resistive))
    return emitOpError("resistive attribute must be boolean");
  if (Attribute controlled = (*this)->getAttr("controlled");
      controlled && !isa<BoolAttr>(controlled))
    return emitOpError("controlled attribute must be boolean");
  if (Attribute directed = (*this)->getAttr("directed");
      directed && !isa<BoolAttr>(directed))
    return emitOpError("directed attribute must be boolean");
  if (Attribute delayed = (*this)->getAttr("delayed");
      delayed && !isa<BoolAttr>(delayed))
    return emitOpError("delayed attribute must be boolean");
  if (auto directed = (*this)->getAttrOfType<BoolAttr>("directed");
      directed && directed.getValue()) {
    auto controlled = (*this)->getAttrOfType<BoolAttr>("controlled");
    if (!controlled || !controlled.getValue())
      return emitOpError("directed pass switch requires controlled = true");
  }
  if (auto delayed = (*this)->getAttrOfType<BoolAttr>("delayed");
      delayed && delayed.getValue()) {
    auto directed = (*this)->getAttrOfType<BoolAttr>("directed");
    auto controlled = (*this)->getAttrOfType<BoolAttr>("controlled");
    if (!directed || !directed.getValue() || !controlled ||
        !controlled.getValue())
      return emitOpError(
          "delayed pass switch requires directed = true and controlled = true");
  }
  if (Attribute group = (*this)->getAttr("control_group")) {
    auto integer = dyn_cast<IntegerAttr>(group);
    if (!integer || integer.getValue().isNegative())
      return emitOpError("control_group attribute must be nonnegative integer");
    auto controlled = (*this)->getAttrOfType<BoolAttr>("controlled");
    if (!controlled || !controlled.getValue())
      return emitOpError("control_group requires controlled = true");
  }
  return success();
}

LogicalResult SimDriverDeclOp::verify() {
  if (failed(verifyNonnegative(*this, getIdAttr(), "driver ID")) ||
      failed(verifyNonnegative(*this, getScopeIdAttr(), "scope ID")) ||
      failed(verifyNonnegative(*this, getNetIdAttr(), "net ID")))
    return failure();
  if (static_cast<bool>(getDrivenLowAttr()) !=
      static_cast<bool>(getDrivenWidthAttr()))
    return emitOpError(
        "driven low and width must either both be present or both be absent");
  if (getDrivenLowAttr()) {
    if (failed(verifyNonnegative(*this, getDrivenLowAttr(), "driven low")) ||
        failed(verifyNonnegative(*this, getDrivenWidthAttr(), "driven width")))
      return failure();
    uint64_t low = getDrivenLowAttr().getValue().getZExtValue();
    uint64_t width = getDrivenWidthAttr().getValue().getZExtValue();
    // A driver on one element of an unpacked array of nets covers a run of the
    // array's storage, which is laid out by provenance span rather than packed
    // width. Both agree for a packed net.
    std::optional<uint64_t> typeWidth = getProvenanceSpan(getType());
    if (width == 0)
      return emitOpError("driven width must be positive");
    if (!typeWidth || low > *typeWidth || width > *typeWidth - low)
      return emitOpError("driven range exceeds the driver type");
  }
  return verifyElementType([&] { return emitOpError(); }, getType());
}

LogicalResult SimPortDeclOp::verify() {
  if (failed(verifyNonnegative(*this, getIdAttr(), "port ID")) ||
      failed(verifyNonnegative(*this, getScopeIdAttr(), "scope ID")) ||
      failed(verifyNonnegative(*this, getSourceIdAttr(), "source ID")) ||
      failed(verifyNonnegative(*this, getSourceLowAttr(), "source offset")) ||
      failed(verifyNonnegative(*this, getOrdinalAttr(), "port ordinal")))
    return failure();
  if (getHierarchicalName().empty())
    return emitOpError("requires a nonempty hierarchical name");
  if (getOrdinalAttr().getValue().getActiveBits() > 24)
    return emitOpError("port ordinal must be an unsigned 24-bit integer");
  auto emit = [&] { return emitOpError(); };
  if (failed(verifyElementType(emit, getType())))
    return failure();
  if (getVpiType() &&
      failed(verifyVPITypeSemantics(emit, getType(), *getVpiType())))
    return failure();
  return verifyFixedReflection(
      *this, static_cast<uint16_t>(reflection::VPIObjectKind::Port),
      reflectionProperties(*this), reflectionDefinitionLoc(*this));
}

static SimCovergroupDeclOp lookupCovergroup(Operation *operation,
                                            SymbolRefAttr symbol) {
  return symbol ? SymbolTable::lookupNearestSymbolFrom<SimCovergroupDeclOp>(
                      operation, symbol)
                : SimCovergroupDeclOp{};
}

static LogicalResult verifyCovergroupHandle(Operation *operation,
                                            CovergroupHandleType handle) {
  if (!lookupCovergroup(operation, handle.getCovergroupName()))
    return operation->emitOpError(
        "handle type references an unknown covergroup declaration");
  return success();
}

LogicalResult SimCovergroupDeclOp::verify() {
  return verifyPositive(*this, getSchemaTypeAttr(),
                        "covergroup schema type ID");
}

static bool covergroupDerivesFrom(SymbolTableCollection &symbolTable,
                                  Operation *from, SimCovergroupDeclOp derived,
                                  SimCovergroupDeclOp base) {
  llvm::SmallPtrSet<Operation *, 8> visited;
  while (derived && visited.insert(derived).second) {
    if (derived == base)
      return true;
    FlatSymbolRefAttr parent = derived.getBaseAttr();
    derived = parent ? symbolTable.lookupNearestSymbolFrom<SimCovergroupDeclOp>(
                           from, parent)
                     : SimCovergroupDeclOp{};
  }
  return false;
}

LogicalResult
SimCovergroupDeclOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  if (!getBaseAttr())
    return success();
  SimCovergroupDeclOp base =
      symbolTable.lookupNearestSymbolFrom<SimCovergroupDeclOp>(*this,
                                                               getBaseAttr());
  if (!base)
    return emitOpError("references an unknown base covergroup declaration");
  if (covergroupDerivesFrom(symbolTable, *this, base, *this))
    return emitOpError("base covergroup chain contains a cycle");
  return success();
}

LogicalResult SimCovergroupNullOp::verify() {
  return verifyCovergroupHandle(*this, getResult().getType());
}

LogicalResult
SimCovergroupCastOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  SimCovergroupDeclOp source =
      lookupCovergroup(*this, getInput().getType().getCovergroupName());
  SimCovergroupDeclOp target =
      lookupCovergroup(*this, getResult().getType().getCovergroupName());
  if (!source || !target)
    return emitOpError("references an unknown covergroup declaration");
  if (!covergroupDerivesFrom(symbolTable, *this, source, target) &&
      !covergroupDerivesFrom(symbolTable, *this, target, source))
    return emitOpError("cannot cast between unrelated covergroup types");
  return success();
}

LogicalResult
SimCovergroupEqualOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  for (CovergroupHandleType type : {getLhs().getType(), getRhs().getType()})
    if (!symbolTable.lookupNearestSymbolFrom<SimCovergroupDeclOp>(
            *this, type.getCovergroupName()))
      return emitOpError("references an unknown covergroup declaration");
  return success();
}

LogicalResult SimVirtualInterfaceBindOp::verify() {
  if (failed(verifyPositive(*this, getScopeIdAttr(), "interface scope ID")))
    return failure();
  SimDesignOp design = (*this)->getParentOfType<SimDesignOp>();
  if (!design)
    return emitOpError("requires an enclosing simulation design");
  // The design's region verifier checks scope references using its shared
  // inventory. Scanning every scope here would make B binds over S scopes B*S.
  return success();
}

LogicalResult SimVirtualInterfaceCastOp::verify() {
  if (getInput().getType().getInterfaceName() !=
      getResult().getType().getInterfaceName())
    return emitOpError("cannot change the interface specialization");
  StringRef source = getInput().getType().getModport().getValue();
  StringRef target = getResult().getType().getModport().getValue();
  if (!source.empty() && source != target)
    return emitOpError("cannot remove or change a selected modport");
  return success();
}

LogicalResult SimVirtualInterfaceEqualOp::verify() {
  if (getLhs().getType().getInterfaceName() !=
      getRhs().getType().getInterfaceName())
    return emitOpError("cannot compare different interface specializations");
  return success();
}

LogicalResult
SimCovergroupPrepareOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  if (!symbolTable.lookupNearestSymbolFrom<SimCovergroupDeclOp>(
          *this, getDeclarationAttr()))
    return emitOpError("references an unknown covergroup declaration");
  return success();
}

LogicalResult SimCovergroupPrepareOp::verify() {
  if (getExpressionIds().size() != getPayloads().size())
    return emitOpError("requires one FunctionalExpression ID per payload");
  llvm::DenseSet<int64_t> seen;
  for (int64_t id : getExpressionIds())
    if (id <= 0 || !seen.insert(id).second)
      return emitOpError("requires unique positive FunctionalExpression IDs");
  return success();
}

LogicalResult SimCovergroupCandidatesOp::verify() {
  auto queue = dyn_cast<QueueType>(getQueue().getType());
  if (!queue || queue.getBound() ||
      !isa<UnpackedStructType>(queue.getElementType()))
    return emitOpError("requires an unbounded CrossQueueType");
  if (!getSelector() || getSelectorAttr().getValue().isNegative() ||
      getMaximumAttr().getValue().isNegative() ||
      getMaximum() > coverage::MaxFunctionalCrossWithCandidates)
    return emitOpError("requires a positive selector and maximum in [0, ")
           << coverage::MaxFunctionalCrossWithCandidates << "]";
  return success();
}

LogicalResult SimCovergroupBindWithOp::verify() {
  auto queue = dyn_cast<QueueType>(getQueue().getType());
  if (!queue || queue.getBound() ||
      !isa<UnpackedStructType>(queue.getElementType()))
    return emitOpError("requires an unbounded CrossQueueType");
  if (!getSelector() || getSelectorAttr().getValue().isNegative())
    return emitOpError("requires a positive selector");
  return success();
}

LogicalResult
SimCovergroupCreateOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  SimCovergroupDeclOp declaration =
      symbolTable.lookupNearestSymbolFrom<SimCovergroupDeclOp>(
          *this, getDeclarationAttr());
  if (!declaration)
    return emitOpError("references an unknown covergroup declaration");
  auto expected = FlatSymbolRefAttr::get(declaration.getOperation());
  if (getResult().getType().getCovergroupName() != expected)
    return emitOpError("result type must name the selected declaration");
  ArrayRef<int64_t> formalIDs = getFormalIds();
  if (static_cast<uint64_t>(getArgumentCount()) > getPayloads().size())
    return emitOpError("constructor argument count exceeds the payload batch");
  size_t argumentCount = static_cast<size_t>(getArgumentCount());
  if (formalIDs.size() != argumentCount)
    return emitOpError() << "requires one FunctionalFormal ID for each "
                            "constructor argument; got "
                         << formalIDs.size() << " IDs and " << argumentCount
                         << " arguments";
  llvm::DenseSet<uint64_t> seenFormals;
  for (auto [index, id] : llvm::enumerate(formalIDs)) {
    if (id <= 0)
      return emitOpError() << "constructor FunctionalFormal ID " << index
                           << " must be a positive signed 64-bit value";
    if (!seenFormals.insert(static_cast<uint64_t>(id)).second)
      return emitOpError() << "constructor FunctionalFormal IDs must be "
                              "unique; ID "
                           << id << " is repeated";
  }
  ArrayRef<int64_t> ids = getExpressionIds();
  size_t valueCount = getPayloads().size() - argumentCount;
  if (ids.size() != valueCount)
    return emitOpError() << "requires one constructor FunctionalExpression "
                            "ID for each value; got "
                         << ids.size() << " IDs and " << valueCount
                         << " values";
  llvm::DenseSet<uint64_t> seen;
  for (auto [index, id] : llvm::enumerate(ids)) {
    if (id <= 0)
      return emitOpError() << "constructor FunctionalExpression ID " << index
                           << " must be a positive signed 64-bit value";
    if (!seen.insert(static_cast<uint64_t>(id)).second)
      return emitOpError()
             << "constructor FunctionalExpression IDs must be unique; ID " << id
             << " is repeated";
  }
  return success();
}

LogicalResult SimCoveragePointHitOp::verify() {
  bool assertion = getKind() == CoveragePointKind::Assertion;
  StringRef inventory = assertion ? "assertion-point" : "line-point";
  ModuleOp module = (*this)->getParentOfType<ModuleOp>();
  if (!module)
    return emitOpError("must be nested in a module with a ")
           << inventory << " inventory";
  auto count = assertion ? cov::get<cov::AssertionPointCount>(module)
                         : cov::get<cov::LinePointCount>(module);
  if (!count)
    return emitOpError("requires the module ")
           << inventory << " inventory attribute '"
           << (assertion ? cov::AssertionPointCount::getNameStr()
                         : cov::LinePointCount::getNameStr())
           << "'";
  if (!count.getType().isSignlessInteger(64))
    return emitOpError("requires a 64-bit module ")
           << inventory << " inventory";
  if (count.getValue().isNegative())
    return emitOpError("point index is outside the module inventory");
  // Assertion points follow the line points in the dense index space.
  uint64_t first = 0;
  if (auto lines = cov::get<cov::LinePointCount>(module);
      assertion && lines && !lines.getValue().isNegative())
    first = lines.getValue().getZExtValue();
  if (getPoint() < first ||
      getPoint() - first >= count.getValue().getZExtValue())
    return emitOpError("point index is outside the module inventory");
  return success();
}

LogicalResult SimCoverageKeepaliveOp::verify() {
  if (!isa_and_nonnull<SimDesignOp>((*this)->getParentOp()))
    return emitOpError("must be directly nested in simulation.design");
  return success();
}

LogicalResult
SimCoverageKeepaliveOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  if (!symbolTable.lookupNearestSymbolFrom<SimFuncOp>(getOperation(),
                                                      getFunctionAttr()))
    return emitOpError("must reference a sibling simulation.func");
  return success();
}

static LogicalResult verifyCoverageScopeTarget(Operation *operation,
                                               IntegerAttr idAttr) {
  if (idAttr.getValue().isZero())
    return operation->emitOpError("coverage scope ID must be nonzero");
  auto design = operation->getParentOfType<SimDesignOp>();
  if (!design)
    return operation->emitOpError("must be nested in an simulation.design");
  uint64_t id = idAttr.getValue().getZExtValue();
  for (SimScopeDeclOp scope : design.getBody().front().getOps<SimScopeDeclOp>())
    if (scope.getCoverageId().value_or(0) == id)
      return success();
  return operation->emitOpError()
         << "references unknown coverage scope ID " << id;
}

LogicalResult SimCoverageControlInstanceOp::verify() {
  return verifyCoverageScopeTarget(getOperation(), getCoverageScopeIdAttr());
}

LogicalResult SimCoverageQueryInstanceOp::verify() {
  return verifyCoverageScopeTarget(getOperation(), getCoverageScopeIdAttr());
}

LogicalResult SimCovergroupSampleEnabledOp::verify() {
  return verifyCovergroupHandle(*this, getHandle().getType());
}

LogicalResult SimCovergroupFormalReadOp::verify() {
  if (failed(verifyCovergroupHandle(*this, getHandle().getType())))
    return failure();
  if (getFormalId() <= 0)
    return emitOpError(
        "FunctionalFormal ID must be a positive signed 64-bit value");
  Type result = getResult().getType();
  if (auto ref = dyn_cast<ArgumentRefType>(result))
    result = ref.getElementType();
  if (!getPackedWidth(result) && !isa<Float32Type, Float64Type>(result) &&
      !(isa<ArgumentRefType>(getResult().getType()) &&
        isAggregateType(result)) &&
      !isa<StringType, DynamicArrayType, QueueType, AssocArrayType,
           ClassHandleType, ChandleType, EventType, CovergroupHandleType,
           ProcessType, VirtualInterfaceType, SemaphoreType, MailboxType>(
          result))
    return emitOpError(
        "requires an integral, real, string, container, class, chandle, event, "
        "covergroup, process, virtual interface, semaphore, or mailbox formal "
        "value or "
        "argument reference");
  return success();
}

LogicalResult SimCovergroupSampleOp::verify() {
  if (!lookupCovergroup(*this, getHandle().getType().getCovergroupName()))
    return emitOpError(
        "handle type references an unknown covergroup declaration");
  ArrayRef<int64_t> ids = getExpressionIds();
  if (ids.size() != getValues().size())
    return emitOpError() << "requires one FunctionalExpression ID for each "
                            "sample value; got "
                         << ids.size() << " IDs and " << getValues().size()
                         << " values";

  llvm::DenseSet<uint64_t> seen;
  for (auto [index, id] : llvm::enumerate(ids)) {
    if (id <= 0)
      return emitOpError() << "FunctionalExpression ID " << index
                           << " must be a positive signed 64-bit value";
    uint64_t stableID = static_cast<uint64_t>(id);
    if (!seen.insert(stableID).second)
      return emitOpError() << "FunctionalExpression IDs must be unique; ID "
                           << stableID << " is repeated";
  }
  for (auto [index, value] : llvm::enumerate(getValues())) {
    Type type = value.getType();
    if (!type.isSignlessIntOrIndex() || isa<IndexType>(type)) {
      if (isa<LogicType, StringType, DynamicArrayType, QueueType,
              AssocArrayType, ClassHandleType, ChandleType, EventType,
              CovergroupHandleType, ProcessType, VirtualInterfaceType,
              SemaphoreType, MailboxType>(type) ||
          (isa<ReferencePathType>(type) &&
           isAggregateType(cast<ReferencePathType>(type).getElementType()) &&
           !getPackedScalarType(
               cast<ReferencePathType>(type).getElementType())) ||
          isa<Float32Type, Float64Type>(type))
        continue;
      return emitOpError() << "sample value " << index
                           << " must have signless integer, !simulation.logic, "
                              "!simulation.string, container, class handle, "
                              "chandle, event, covergroup, process, virtual "
                              "interface, semaphore, mailbox, or "
                              "f32 or f64 type, but has "
                           << type;
    }
  }
  return success();
}

LogicalResult SimCovergroupStartOp::verify() {
  return verifyCovergroupHandle(*this, getHandle().getType());
}

LogicalResult SimCovergroupStopOp::verify() {
  return verifyCovergroupHandle(*this, getHandle().getType());
}

LogicalResult SimCovergroupSetNameOp::verify() {
  return verifyCovergroupHandle(*this, getHandle().getType());
}

LogicalResult SimCovergroupSetIntegerOptionOp::verify() {
  if (failed(verifyCovergroupHandle(*this, getHandle().getType())))
    return failure();
  if (getOption() != CovergroupInstanceOptionKind::Weight &&
      getOption() != CovergroupInstanceOptionKind::Goal &&
      getOption() != CovergroupInstanceOptionKind::AtLeast &&
      getOption() != CovergroupInstanceOptionKind::CrossNumPrintMissing)
    return emitOpError("requires an integral option kind");
  return success();
}

LogicalResult SimCovergroupGetIntegerOptionOp::verify() {
  if (failed(verifyCovergroupHandle(*this, getHandle().getType())))
    return failure();
  if (getOption() == CovergroupInstanceOptionKind::Comment ||
      getOption() == CovergroupInstanceOptionKind::Name ||
      getOption() == CovergroupInstanceOptionKind::MergeInstances ||
      getOption() == CovergroupInstanceOptionKind::DistributeFirst ||
      (getItem() &&
       (getOption() == CovergroupInstanceOptionKind::PerInstance ||
        getOption() == CovergroupInstanceOptionKind::GetInstCoverage)))
    return emitOpError("requires an integral instance-option kind");
  return success();
}

LogicalResult SimCovergroupGetStringOptionOp::verify() {
  if (failed(verifyCovergroupHandle(*this, getHandle().getType())))
    return failure();
  if (getOption() != CovergroupInstanceOptionKind::Comment &&
      (getOption() != CovergroupInstanceOptionKind::Name || getItem()))
    return emitOpError("requires a comment or group-name option kind");
  return success();
}

LogicalResult SimCovergroupSetStringOptionOp::verify() {
  if (failed(verifyCovergroupHandle(*this, getHandle().getType())))
    return failure();
  if (getOption() != CovergroupInstanceOptionKind::Comment)
    return emitOpError("requires the comment option kind");
  return success();
}

LogicalResult SimCovergroupSetTypeIntegerOptionOp::verify() {
  if (!getTypeId() ||
      (getOption() != CovergroupInstanceOptionKind::Weight &&
       getOption() != CovergroupInstanceOptionKind::Goal &&
       getOption() != CovergroupInstanceOptionKind::MergeInstances &&
       getOption() != CovergroupInstanceOptionKind::DistributeFirst) ||
      (getItem() &&
       (getOption() == CovergroupInstanceOptionKind::MergeInstances ||
        getOption() == CovergroupInstanceOptionKind::DistributeFirst)))
    return emitOpError("requires a valid integral type-option owner and kind");
  return success();
}

LogicalResult SimCovergroupSetTypeStringOptionOp::verify() {
  if (!getTypeId() || getOption() != CovergroupInstanceOptionKind::Comment)
    return emitOpError("requires a valid comment type-option owner");
  return success();
}

static LogicalResult verifyOptionReference(Operation *op, Type result,
                                           CovergroupInstanceOptionKind kind,
                                           bool character) {
  bool string = kind == CovergroupInstanceOptionKind::Comment ||
                kind == CovergroupInstanceOptionKind::Name;
  Type element = cast<ReferencePathType>(result).getElementType();
  Type expected = string && !character
                      ? Type(StringType::get(op->getContext()))
                      : IntegerType::get(op->getContext(), character ? 8 : 64);
  if ((character && !string) || element != expected)
    return op->emitOpError("has an incompatible option path element type");
  return success();
}

LogicalResult SimCovergroupOptionReferenceOp::verify() {
  if (failed(verifyCovergroupHandle(*this, getHandle().getType())))
    return failure();
  using Kind = CovergroupInstanceOptionKind;
  if (getOption() != Kind::Weight && getOption() != Kind::Goal &&
      getOption() != Kind::AtLeast && getOption() != Kind::Comment &&
      getOption() != Kind::CrossNumPrintMissing &&
      (getOption() != Kind::Name || getItem()))
    return emitOpError("requires a mutable instance option kind and owner");
  return verifyOptionReference(*this, getResult().getType(), getOption(),
                               getCharacter());
}

LogicalResult SimCovergroupTypeOptionReferenceOp::verify() {
  using Kind = CovergroupInstanceOptionKind;
  if (!getTypeId() ||
      (getOption() != Kind::Weight && getOption() != Kind::Goal &&
       getOption() != Kind::Comment && getOption() != Kind::MergeInstances &&
       getOption() != Kind::DistributeFirst) ||
      (getItem() && (getOption() == Kind::MergeInstances ||
                     getOption() == Kind::DistributeFirst)))
    return emitOpError("requires a mutable type option kind and owner");
  Type storage =
      cast<ArgumentRefType>(getReference().getType()).getElementType();
  Type expected =
      getOption() == Kind::Comment
          ? Type(StringType::get((*this)->getContext()))
          : IntegerType::get((*this)->getContext(),
                             getOption() == Kind::MergeInstances ||
                                     getOption() == Kind::DistributeFirst
                                 ? 1
                                 : 32);
  if (storage != expected)
    return emitOpError("has an incompatible static option storage type");
  return verifyOptionReference(*this, getResult().getType(), getOption(),
                               getCharacter());
}

LogicalResult SimCovergroupInstanceQueryOp::verify() {
  return verifyCovergroupHandle(*this, getHandle().getType());
}

LogicalResult
SimCovergroupTypeQueryOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  if (!symbolTable.lookupNearestSymbolFrom<SimCovergroupDeclOp>(
          *this, getDeclarationAttr()))
    return emitOpError("references an unknown covergroup declaration");
  return success();
}

// Cached variants for verifiers that run once per operation. The uncached
// SymbolTable lookups scan the enclosing design's block on every call, so an
// op-count-proportional number of calls costs design-size each and makes
// verification quadratic in the number of declarations.
static SimClassDeclOp lookupClass(SymbolTableCollection &symbolTable,
                                  Operation *operation, SymbolRefAttr symbol) {
  return symbol ? symbolTable.lookupNearestSymbolFrom<SimClassDeclOp>(operation,
                                                                      symbol)
                : SimClassDeclOp{};
}

static SimClassFieldDeclOp lookupClassField(SymbolTableCollection &symbolTable,
                                            Operation *operation,
                                            SymbolRefAttr symbol) {
  return symbol ? symbolTable.lookupNearestSymbolFrom<SimClassFieldDeclOp>(
                      operation, symbol)
                : SimClassFieldDeclOp{};
}

static bool classDerivesFrom(SymbolTableCollection &symbolTable,
                             SimClassDeclOp derived, SimClassDeclOp base) {
  llvm::SmallPtrSet<Operation *, 8> visited;
  for (SimClassDeclOp current = derived;
       current && visited.insert(current).second;) {
    if (current == base)
      return true;
    current = current.getBaseAttr()
                  ? lookupClass(symbolTable, current, current.getBaseAttr())
                  : SimClassDeclOp{};
  }
  return false;
}

LogicalResult SimClassDeclOp::verify() {
  if (failed(verifyPositive(*this, getIdAttr(), "class ID")))
    return failure();
  if (getIsInterface() && !getIsAbstract())
    return emitOpError("interface classes must be abstract");
  if (getIsInterface() && getBaseAttr())
    return emitOpError("interface classes cannot have a base class");
  if (getBuiltinBaseAttr() &&
      (getBaseAttr() || getWeakReferentAttr() || getIsInterface()))
    return emitOpError(
        "builtin base requires a root class without a weak referent");
  if (getBaseAttr() && getBase() == getSymName())
    return emitOpError("class cannot extend itself");
  if (ArrayAttr interfaces = getInterfacesAttr()) {
    SmallVector<StringRef> unique;
    for (Attribute attribute : interfaces) {
      auto interface = dyn_cast<FlatSymbolRefAttr>(attribute);
      if (!interface)
        return emitOpError(
            "implemented interface list must contain flat symbol references");
      if (llvm::is_contained(unique, interface.getValue()))
        return emitOpError("implemented interface list contains a duplicate");
      unique.push_back(interface.getValue());
    }
  }
  return success();
}

LogicalResult
SimClassDeclOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  if (FlatSymbolRefAttr reference = getBuiltinBaseAttr()) {
    SimClassFieldDeclOp field = lookupClassField(symbolTable, *this, reference);
    if (!field || field.getOwner() != getSymName() || field.getIsStatic() ||
        field.getIsWeak() || !isa<MailboxType, SemaphoreType>(field.getType()))
      return emitOpError("builtin base must name an own strong instance "
                         "mailbox or semaphore field");
  }

  if (getWeakReferentAttr() &&
      !lookupClass(symbolTable, *this, getWeakReferentAttr()))
    return emitOpError("weak wrapper references an unknown referent class");
  if (FlatSymbolRefAttr reference = getImplicitConstructorAttr()) {
    auto constructor =
        symbolTable.lookupNearestSymbolFrom<SimFuncOp>(*this, reference);
    if (!constructor)
      return emitOpError("implicit constructor references an unknown function");
    if (constructor.getEntryKind() != EntryKind::Function)
      return emitOpError(
          "implicit constructor must reference a function code unit");
    Type receiverType = ClassHandleType::get(
        getContext(), FlatSymbolRefAttr::get(getContext(), getSymName()));
    if (constructor.getFunctionType().getNumInputs() < 2 ||
        constructor.getFunctionType().getInput(1) != receiverType ||
        constructor.getFunctionType().getNumResults() != 0)
      return emitOpError(
          "implicit constructor must reference a void constructor function "
          "for this class");
  }
  if (FlatSymbolRefAttr reference = get<metadata::RandomModeField>(*this)) {
    SimClassFieldDeclOp field = lookupClassField(symbolTable, *this, reference);
    if (getBaseAttr() || !field || field.getOwner() != getSymName() ||
        field.getIsStatic() || !field.getType().isInteger(64))
      return emitOpError(
          "random mode field must name an instance i64 field owned by the "
          "root class");
  }
  if (ArrayAttr references = getRandomVariableReferencesAttr()) {
    if (references.empty())
      return emitOpError(
          "random-variable reference inventory must be absent when empty");
    llvm::SmallDenseSet<Attribute> unique;
    for (Attribute attribute : references) {
      auto reference = cast<RandomVariableReferenceAttr>(attribute);
      if (!unique.insert(reference).second)
        return emitOpError(
            "random-variable reference inventory contains a duplicate");

      SimClassDeclOp current = *this;
      for (FlatSymbolRefAttr pathComponent : reference.getPath()) {
        SimClassFieldDeclOp field =
            lookupClassField(symbolTable, *this, pathComponent);
        SimClassDeclOp fieldOwner =
            field ? lookupClass(symbolTable, field, field.getOwnerAttr())
                  : SimClassDeclOp{};
        if (!field || !fieldOwner ||
            !classDerivesFrom(symbolTable, current, fieldOwner))
          return emitOpError()
                 << "random-variable reference path field " << pathComponent
                 << " is not visible from class " << current.getSymName();
        auto handle = dyn_cast<ClassHandleType>(field.getType());
        if (field.getIsStatic() || field.getIsWeak() || !handle ||
            !sim::has<sim::metadata::RandomObjectEdge>(field))
          return emitOpError()
                 << "random-variable reference path field " << pathComponent
                 << " must be a strong rand object edge";
        current = lookupClass(symbolTable, field, handle.getClassName());
        if (!current)
          return emitOpError() << "random-variable reference path field "
                               << pathComponent << " has an unknown class type";
      }

      SimClassFieldDeclOp target =
          lookupClassField(symbolTable, *this, reference.getTarget());
      SimClassDeclOp targetOwner =
          target ? lookupClass(symbolTable, target, target.getOwnerAttr())
                 : SimClassDeclOp{};
      if (!target || !targetOwner ||
          !classDerivesFrom(symbolTable, current, targetOwner))
        return emitOpError()
               << "random-variable reference target " << reference.getTarget()
               << " is not visible from class " << current.getSymName();
      if (target.getIsStatic() ||
          !sim::get<sim::metadata::RandomVariableKind>(target))
        return emitOpError()
               << "random-variable reference target " << reference.getTarget()
               << " must be a packed instance rand or randc field";
    }
  }
  if (FlatSymbolRefAttr reference = getRandomConstraintTemplateAttr()) {
    auto templateOp =
        symbolTable.lookupNearestSymbolFrom<SimRandomConstraintTemplateOp>(
            *this, reference);
    if (!templateOp)
      return emitOpError(
          "random constraint template references an unknown template");
    if (templateOp.getOwner() != getSymName())
      return emitOpError(
          "random constraint template must be owned by this class");
  }
  return success();
}

LogicalResult SimClassFieldDeclOp::verify() {
  if (getOffsetAttr() &&
      failed(verifyNonnegative(*this, getOffsetAttr(), "field offset")))
    return failure();
  if (getIsStatic() && getOffsetAttr())
    return emitOpError("static properties cannot have an instance offset");
  if (getIsWeak() && !isa<ClassHandleType>(getType()))
    return emitOpError("weak properties must have class-handle type");
  bool bitstreamMember = has<metadata::ClassBitstreamMember>(*this);
  MemberVisibilityAttr visibility =
      get<metadata::ClassBitstreamVisibility>(*this);
  if (bitstreamMember && getIsStatic())
    return emitOpError("static properties cannot be object bit-stream members");
  if (bitstreamMember != static_cast<bool>(visibility))
    return emitOpError(
        "class bit-stream member and visibility metadata must be paired");
  if (has<metadata::EmbeddedCovergroup>(*this) &&
      (getIsStatic() || !isa<CovergroupHandleType>(getType())))
    return emitOpError(
        "embedded covergroup requires an instance covergroup-handle field");
  // The simulation dialect verifies the payload kinds of this metadata.
  IntegerAttr modeIndex = get<metadata::RandomModeIndex>(*this);
  if (modeIndex && (modeIndex.getValue().isNegative() ||
                    modeIndex.getValue().getActiveBits() > 6))
    return emitOpError(
        "random mode index exceeds the 64-property executable boundary");
  if (has<metadata::RandomObjectEdge>(*this)) {
    if (!modeIndex || getIsStatic() || getIsWeak() ||
        !isa<ClassHandleType>(getType()))
      return emitOpError(
          "random object edge requires an indexed, strong instance "
          "class-handle field");
  }
  RandomVariableKindAttr variableKind =
      get<metadata::RandomVariableKind>(*this);
  BoolAttr isSigned = get<metadata::RandomVariableSigned>(*this);
  FlatSymbolRefAttr key = get<metadata::RandomCycleKeyField>(*this);
  FlatSymbolRefAttr position = get<metadata::RandomCyclePositionField>(*this);
  if (variableKind) {
    std::optional<unsigned> width = getPackedWidth(getType());
    if (!modeIndex || getIsStatic() || !width || *width == 0 ||
        has<metadata::RandomObjectEdge>(*this))
      return emitOpError(
          "random variable metadata requires an indexed, packed instance "
          "field distinct from an object edge");
    if (!isSigned)
      return emitOpError("random variable metadata requires signedness");
    bool isRandC = variableKind.getValue() == RandomVariableKind::RandC;
    if (isRandC != static_cast<bool>(key) ||
        isRandC != static_cast<bool>(position))
      return emitOpError(
          "randc variables require key and position fields; rand variables "
          "forbid them");
    // The randc state fields themselves are resolved in verifySymbolUses.
  } else if (isSigned || key || position) {
    return emitOpError(
        "random variable auxiliary metadata requires a variable kind");
  }
  if (!isNormalizedValueType(getType()))
    return emitOpError("property must have a normalized executable type");
  return success();
}

LogicalResult
SimClassFieldDeclOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  if (!lookupClass(symbolTable, *this, getOwnerAttr()))
    return emitOpError("references an unknown owner class");
  // Structural verification already rejected a randc field that is missing
  // either state reference, so only a complete pair reaches a lookup here.
  FlatSymbolRefAttr key = get<metadata::RandomCycleKeyField>(*this);
  FlatSymbolRefAttr position = get<metadata::RandomCyclePositionField>(*this);
  if (!key || !position)
    return success();
  SimClassFieldDeclOp keyField = lookupClassField(symbolTable, *this, key);
  SimClassFieldDeclOp positionField =
      lookupClassField(symbolTable, *this, position);
  auto validStateField = [&](SimClassFieldDeclOp state) {
    return state && state.getOwner() == getOwner() && !state.getIsStatic() &&
           state.getType().isInteger(64);
  };
  if (!validStateField(keyField) || !validStateField(positionField) ||
      keyField == positionField || keyField == *this || positionField == *this)
    return emitOpError(
        "randc state must name distinct owned instance i64 fields");
  return success();
}

LogicalResult SimClassMethodDeclOp::verify() {
  auto functionType = dyn_cast<FunctionType>(getFunctionType());
  if (!functionType)
    return emitOpError("method signature must be a function type");
  if (functionType.getNumInputs() == 0 ||
      !isa<ContextType>(functionType.getInput(0)))
    return emitOpError("method signature must begin with context");
  if (!getIsStatic()) {
    if (functionType.getNumInputs() < 2)
      return emitOpError("instance method signature requires explicit this");
    auto thisType = dyn_cast<ClassHandleType>(functionType.getInput(1));
    // The owner attribute is the class's name, so this check needs no lookup.
    if (!thisType ||
        thisType.getClassName().getRootReference() != getOwnerAttr().getValue())
      return emitOpError("instance method this type must name its owner class");
  }
  if (getIsTask() && functionType.getNumResults() != 0)
    return emitOpError("task method cannot have value results");
  if (getIsPure() && !getIsVirtual())
    return emitOpError("pure methods must be virtual");
  if (getIsStatic() && getIsVirtual())
    return emitOpError("static methods cannot be virtual");
  if (getIsFinal() && !getIsVirtual())
    return emitOpError("final methods must be virtual");
  if (getIsVirtual() != static_cast<bool>(getSlotAttr()))
    return emitOpError(
        "virtual methods require a slot and nonvirtual methods forbid one");
  if (getSlotAttr()) {
    if (getSlotAttr().getValue().isNegative() ||
        getSlot() > interfaceDispatchSlot)
      return emitOpError("virtual-method slot exceeds the 32-bit dispatch ABI");
  }
  if (getIsVirtual() != static_cast<bool>(getSignatureIdAttr()) ||
      (getSignatureIdAttr() && getSignatureId() == 0))
    return emitOpError(
        "virtual methods require a nonzero signature ID and nonvirtual "
        "methods forbid one");
  if (getIsPure() == static_cast<bool>(getImplementationAttr()))
    return emitOpError(
        "pure methods forbid an implementation and concrete methods require "
        "one");
  return success();
}

LogicalResult
SimClassMethodDeclOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  SimClassDeclOp owner = lookupClass(symbolTable, *this, getOwnerAttr());
  if (!owner)
    return emitOpError("references an unknown owner class");
  if (getSlotAttr()) {
    if (owner.getIsInterface() && getSlot() != interfaceDispatchSlot)
      return emitOpError(
          "interface virtual methods require the interface dispatch slot");
    if (!owner.getIsInterface() && getSlot() == interfaceDispatchSlot)
      return emitOpError(
          "non-interface virtual methods cannot use the interface dispatch "
          "slot");
  }
  if (owner.getIsInterface() && getIsVirtual()) {
    if (!getInterfaceOrdinalAttr() ||
        getInterfaceOrdinalAttr().getValue().isNegative() ||
        getInterfaceOrdinal() > UINT32_MAX)
      return emitOpError(
          "interface virtual methods require a 32-bit interface ordinal");
  } else if (getInterfaceOrdinalAttr()) {
    return emitOpError(
        "only interface virtual methods may have an interface ordinal");
  }
  return success();
}

static SimStorageDeclOp lookupStorage(Operation *operation, uint64_t id) {
  SimDesignOp design = operation->getParentOfType<SimDesignOp>();
  if (!design)
    return {};
  for (Operation &candidate : design.getBody().front())
    if (auto storage = dyn_cast<SimStorageDeclOp>(candidate);
        storage && storage.getId() == id)
      return storage;
  return {};
}

static LogicalResult
verifyRandomValueReference(SymbolTableCollection &symbolTable,
                           SimRandomConstraintTemplateOp templateOp,
                           SimClassDeclOp owner,
                           RandomValueReferenceAttr reference) {
  auto verifyRange = [&](Type type, StringRef source) -> LogicalResult {
    std::optional<unsigned> packedWidth = getPackedWidth(type);
    if (!packedWidth || reference.getWidth() > *packedWidth ||
        reference.getLow() > *packedWidth - reference.getWidth())
      return templateOp.emitOpError()
             << source << " does not contain packed bit range ["
             << reference.getLow() << ", "
             << reference.getLow() + reference.getWidth() << ")";
    return success();
  };

  switch (reference.getKind()) {
  case RandomValueReferenceKind::ObjectField: {
    SimClassDeclOp current = owner;
    for (FlatSymbolRefAttr pathComponent : reference.getPath()) {
      SimClassFieldDeclOp field =
          lookupClassField(symbolTable, templateOp, pathComponent);
      SimClassDeclOp fieldOwner =
          field ? lookupClass(symbolTable, field, field.getOwnerAttr())
                : SimClassDeclOp{};
      if (!field || !fieldOwner ||
          !classDerivesFrom(symbolTable, current, fieldOwner))
        return templateOp.emitOpError()
               << "random-value path field " << pathComponent
               << " is not visible from class " << current.getSymName();
      auto handle = dyn_cast<ClassHandleType>(field.getType());
      if (field.getIsStatic() || field.getIsWeak() || !handle)
        return templateOp.emitOpError()
               << "random-value path field " << pathComponent
               << " must be a strong instance class handle";
      current = lookupClass(symbolTable, field, handle.getClassName());
      if (!current)
        return templateOp.emitOpError()
               << "random-value path field " << pathComponent
               << " has an unknown class type";
    }

    SimClassFieldDeclOp target =
        lookupClassField(symbolTable, templateOp, reference.getTarget());
    SimClassDeclOp targetOwner =
        target ? lookupClass(symbolTable, target, target.getOwnerAttr())
               : SimClassDeclOp{};
    if (!target || !targetOwner ||
        !classDerivesFrom(symbolTable, current, targetOwner))
      return templateOp.emitOpError()
             << "random-value target " << reference.getTarget()
             << " is not visible from class " << current.getSymName();
    if (target.getIsStatic())
      return templateOp.emitOpError()
             << "object-field random-value target " << reference.getTarget()
             << " must be an instance field";
    return verifyRange(target.getType(), "random-value target");
  }
  case RandomValueReferenceKind::Storage: {
    uint64_t id = reference.getStorage().getValue().getZExtValue();
    SimStorageDeclOp storage = lookupStorage(templateOp, id);
    if (!storage)
      return templateOp.emitOpError()
             << "random-value reference names unknown storage ID " << id;
    return verifyRange(storage.getType(), "random-value storage");
  }
  }
  llvm_unreachable("unknown random-value reference kind");
}

LogicalResult SimRandomConstraintTemplateOp::verifySymbolUses(
    SymbolTableCollection &symbolTable) {
  SimClassDeclOp owner = lookupClass(symbolTable, *this, getOwnerAttr());
  if (!owner)
    return emitOpError("references an unknown owner class");

  ArrayAttr references =
      (*this)->getAttrOfType<ArrayAttr>(getReferencesAttrName());
  if (references && references.empty())
    return emitOpError("random-value references must be absent when empty");
  llvm::SmallDenseSet<Attribute> uniqueReferences;
  if (references)
    for (Attribute attribute : references) {
      auto reference = cast<RandomValueReferenceAttr>(attribute);
      if (!uniqueReferences.insert(reference).second)
        return emitOpError("random-value references contain a duplicate");
      if (failed(
              verifyRandomValueReference(symbolTable, *this, owner, reference)))
        return failure();
    }

  ArrayAttr blocks =
      (*this)->getAttrOfType<ArrayAttr>(getConstraintBlocksAttrName());
  if (!blocks || blocks.empty())
    return emitOpError("requires at least one constraint-block reference");
  llvm::SmallDenseSet<Attribute> uniqueBlocks;
  for (Attribute attribute : blocks) {
    auto block = cast<RandomConstraintBlockReferenceAttr>(attribute);
    if (!uniqueBlocks.insert(block).second)
      return emitOpError("constraint-block references contain a duplicate");
    if (block.getKind() == RandomConstraintBlockReferenceKind::Storage) {
      uint64_t id = block.getStorage().getValue().getZExtValue();
      SimStorageDeclOp storage = lookupStorage(*this, id);
      if (!storage)
        return emitOpError()
               << "constraint-block reference names unknown storage ID " << id;
      if (!storage.getType().isInteger(64))
        return emitOpError()
               << "constraint-block storage ID " << id << " must be i64";
    }
  }

  if (getBody().empty())
    return emitOpError("requires one dataflow block");
  Block &body = getBody().front();
  if (body.getNumArguments() != 0)
    return emitOpError("dataflow block cannot have arguments");

  unsigned constraintCount = 0;
  llvm::SmallDenseSet<uint32_t> softPriorities;
  for (Operation &operation : body) {
    if (auto hard = dyn_cast<SimRandomHardConstraintOp>(operation)) {
      ++constraintCount;
      continue;
    }
    if (auto soft = dyn_cast<SimRandomSoftConstraintOp>(operation)) {
      ++constraintCount;
      if (!softPriorities.insert(soft.getPriority()).second)
        return soft.emitOpError("soft priority is duplicated in its template");
      continue;
    }
    if (isa<SimRandomConstraintValueOp>(operation))
      continue;
    if (operation.getNumRegions() != 0 || operation.getNumSuccessors() != 0 ||
        !isMemoryEffectFree(&operation))
      return operation.emitOpError(
          "random constraint template dataflow must be pure and regionless");
    if (llvm::any_of(operation.getOperandTypes(),
                     [](Type type) { return !type.isSignlessInteger(); }) ||
        llvm::any_of(operation.getResultTypes(),
                     [](Type type) { return !type.isSignlessInteger(); }))
      return operation.emitOpError(
          "random constraint template dataflow must use signless integers");
  }
  if (constraintCount == 0)
    return emitOpError("requires at least one hard or soft constraint");
  for (uint32_t priority = 0; priority < softPriorities.size(); ++priority)
    if (!softPriorities.contains(priority))
      return emitOpError(
          "soft priorities must be dense from zero in declaration order");
  return success();
}

LogicalResult SimRandomConstraintValueOp::verify() {
  auto templateOp = (*this)->getParentOfType<SimRandomConstraintTemplateOp>();
  if (!templateOp)
    return emitOpError("must be nested in a random constraint template");
  if (getIndexAttr().getValue().isNegative() ||
      getIndexAttr().getValue().getActiveBits() > 32)
    return emitOpError("reference index exceeds 32 bits");
  ArrayAttr references =
      templateOp->getAttrOfType<ArrayAttr>(templateOp.getReferencesAttrName());
  uint64_t index = getIndexAttr().getValue().getZExtValue();
  if (!references || index >= references.size())
    return emitOpError("reference index is outside the template inventory");
  auto reference = cast<RandomValueReferenceAttr>(references[index]);
  if (getResult().getType().getWidth() != reference.getWidth())
    return emitOpError("result width does not match the symbolic reference");
  return success();
}

static LogicalResult verifyRandomConstraintSink(Operation *operation,
                                                IntegerAttr block) {
  auto templateOp = operation->getParentOfType<SimRandomConstraintTemplateOp>();
  if (!templateOp)
    return operation->emitOpError(
        "must be nested in a random constraint template");
  ArrayAttr blocks = templateOp->getAttrOfType<ArrayAttr>(
      templateOp.getConstraintBlocksAttrName());
  if (!blocks || block.getValue().isNegative() ||
      block.getValue().getActiveBits() > 32 ||
      block.getValue().getZExtValue() >= blocks.size())
    return operation->emitOpError(
        "constraint-block index is outside the template inventory");
  return success();
}

LogicalResult SimRandomHardConstraintOp::verify() {
  return verifyRandomConstraintSink(*this, getBlockAttr());
}

LogicalResult SimRandomSoftConstraintOp::verify() {
  if (getPriorityAttr().getValue().isNegative() ||
      getPriorityAttr().getValue().getActiveBits() > 32)
    return emitOpError("soft priority exceeds 32 bits");
  return verifyRandomConstraintSink(*this, getBlockAttr());
}

LogicalResult
SimClassAllocOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  auto type = getResult().getType();
  SimClassDeclOp descriptor =
      lookupClass(symbolTable, *this, type.getClassName());
  if (!descriptor)
    return emitOpError("result type references an unknown class");
  if (descriptor.getIsAbstract() || descriptor.getIsInterface())
    return emitOpError("cannot allocate an abstract or interface class");
  return success();
}

LogicalResult SimClassCopyOp::verify() {
  if (getSource().getType() != getResult().getType())
    return emitOpError(
        "source and result must have the same static class type");
  return success();
}

LogicalResult
SimWeakCreateOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  SimClassDeclOp wrapper =
      lookupClass(symbolTable, *this, getResult().getType().getClassName());
  if (!wrapper || !wrapper.getWeakReferentAttr())
    return emitOpError("result must be a declared weak_reference wrapper");
  if (wrapper.getWeakReferentAttr() != getReferent().getType().getClassName())
    return emitOpError(
        "referent type does not match the weak_reference specialization");
  return success();
}

LogicalResult
SimWeakGetOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  SimClassDeclOp wrapper =
      lookupClass(symbolTable, *this, getWeak().getType().getClassName());
  if (!wrapper || !wrapper.getWeakReferentAttr())
    return emitOpError("operand must be a declared weak_reference wrapper");
  if (wrapper.getWeakReferentAttr() != getResult().getType().getClassName())
    return emitOpError(
        "result type does not match the weak_reference specialization");
  return success();
}

LogicalResult
SimWeakClearOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  SimClassDeclOp wrapper =
      lookupClass(symbolTable, *this, getWeak().getType().getClassName());
  if (!wrapper || !wrapper.getWeakReferentAttr())
    return emitOpError("operand must be a declared weak_reference wrapper");
  return success();
}

LogicalResult
SimClassBuiltinUpcastOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  SimClassDeclOp declaration =
      lookupClass(symbolTable, *this, getObject().getType().getClassName());
  llvm::SmallPtrSet<Operation *, 8> visited;
  while (declaration && visited.insert(declaration).second) {
    if (FlatSymbolRefAttr base = declaration.getBuiltinBaseAttr()) {
      SimClassFieldDeclOp field = lookupClassField(symbolTable, *this, base);
      if (!field || field.getType() != getResult().getType())
        return emitOpError(
            "result does not match the declared builtin base type");
      return success();
    }
    declaration =
        declaration.getBaseAttr()
            ? lookupClass(symbolTable, *this, declaration.getBaseAttr())
            : SimClassDeclOp{};
  }
  return emitOpError("operand class has no declared builtin base");
}

LogicalResult
SimClassIsInstanceOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  if (!lookupClass(symbolTable, *this, getTargetAttr()))
    return emitOpError("references an unknown target class");
  return success();
}

LogicalResult
SimClassCastOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  auto source = dyn_cast<ClassHandleType>(getObject().getType());
  auto target = getResult().getType();
  SimClassDeclOp targetClass =
      lookupClass(symbolTable, *this, target.getClassName());
  if (!targetClass)
    return emitOpError("cast references an unknown class");
  if (!source) {
    llvm::SmallPtrSet<Operation *, 8> visited;
    for (SimClassDeclOp ancestor = targetClass;
         ancestor && visited.insert(ancestor).second;
         ancestor = ancestor.getBaseAttr() ? lookupClass(symbolTable, *this,
                                                         ancestor.getBaseAttr())
                                           : SimClassDeclOp{}) {
      if (FlatSymbolRefAttr base = ancestor.getBuiltinBaseAttr()) {
        SimClassFieldDeclOp field = lookupClassField(symbolTable, *this, base);
        if (field && field.getType() == getObject().getType())
          return success();
        break;
      }
    }
    return emitOpError(
        "target class does not derive from the builtin source type");
  }
  SimClassDeclOp sourceClass =
      lookupClass(symbolTable, *this, source.getClassName());
  if (!sourceClass)
    return emitOpError("cast references an unknown class");
  bool targetInterface = targetClass.getIsInterface();
  bool sourceInterface = sourceClass.getIsInterface();
  if (!targetInterface && !sourceInterface &&
      !classDerivesFrom(symbolTable, sourceClass, targetClass) &&
      !classDerivesFrom(symbolTable, targetClass, sourceClass))
    return emitOpError("cast classes are unrelated");
  return success();
}

LogicalResult
SimClassFieldRefOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  SimClassFieldDeclOp field =
      lookupClassField(symbolTable, *this, getFieldAttr());
  if (!field)
    return emitOpError("references an unknown class property");
  if (field.getIsStatic())
    return emitOpError(
        "cannot form an instance reference to a static property");
  auto objectType = getObject().getType();
  SimClassDeclOp objectClass =
      lookupClass(symbolTable, *this, objectType.getClassName());
  SimClassDeclOp fieldOwner =
      lookupClass(symbolTable, *this, field.getOwnerAttr());
  if (!objectClass || !fieldOwner ||
      !classDerivesFrom(symbolTable, objectClass, fieldOwner))
    return emitOpError("property is not a member of the receiver class");
  auto resultType = getResult().getType();
  if (resultType.getElementType() != field.getType() ||
      resultType.getOwnerClass() != objectType.getClassName())
    return emitOpError("managed reference type does not match the property");
  return success();
}

LogicalResult SimManagedWatchOp::verify() {
  Type input = getInput().getType();
  switch (getKind()) {
  case ManagedWatchKind::Field:
    if (!isa<ManagedRefType>(input))
      return emitOpError("field watches require a managed reference");
    break;
  case ManagedWatchKind::ContainerSize:
    if (!isa<DynamicArrayType, QueueType, AssocArrayType>(input))
      return emitOpError(
          "container-size watches require a dynamic, queue, or associative "
          "array handle");
    break;
  }
  return success();
}

LogicalResult SimManagedLoadOp::verify() {
  if (getReference().getType().getElementType() != getResult().getType())
    return emitOpError("result type must match the referenced element");
  return success();
}

LogicalResult SimManagedStoreOp::verify() {
  if (getReference().getType().getElementType() != getValue().getType())
    return emitOpError("value type must match the referenced element");
  return success();
}

LogicalResult SimManagedBitsDynStoreOp::verify() {
  Type element = getReference().getType().getElementType();
  std::optional<unsigned> fieldWidth = getPackedWidth(element);
  if (!fieldWidth || !isa<IntegerType>(getPackedScalarType(element)))
    return emitOpError("reference must select a two-state packed field");
  if (!getReplacement().getType().isSignless() ||
      getReplacement().getType().getWidth() > 64)
    return emitOpError(
        "replacement must be a signless integer no wider than 64 bits");
  if (getReplacement().getType().getWidth() > *fieldWidth)
    return emitOpError("replacement width exceeds the packed field width");
  if (!getLowBit().getType().isSignless())
    return emitOpError("low-bit index must be a signless integer");
  return success();
}

LogicalResult SimManagedNBAEnqueueOp::verify() {
  if (getDestination().getType().getElementType() != getValue().getType())
    return emitOpError("value type must match the referenced element");
  return success();
}

// Packed and real values, and unpacked structs and arrays of them, which a
// mask covers.
static bool hasFixedBitLayout(Type type) {
  if (getPackedWidth(type) || isa<Float32Type, Float64Type>(type))
    return true;
  if (auto array = dyn_cast<UnpackedArrayType>(type))
    return hasFixedBitLayout(array.getElementType());
  if (!isa<UnpackedStructType>(type))
    return false;
  for (unsigned index = 0, count = getAggregateNumElements(type);
       index != count; ++index)
    if (!hasFixedBitLayout(getAggregateElementType(type, index)))
      return false;
  return true;
}

LogicalResult SimManagedNBAEnqueueMaskedOp::verify() {
  Type element = TypeSwitch<Type, Type>(getDestination().getType())
                     .Case<ManagedRefType, ReferencePathType>(
                         [](auto type) { return type.getElementType(); })
                     .Default([](Type) { return Type{}; });
  if (element != getValue().getType())
    return emitOpError("value type must match the referenced element");
  if (getMask().getType() != getValue().getType() ||
      !hasFixedBitLayout(getValue().getType()))
    return emitOpError("mask must have the value's fixed-layout type");
  return success();
}

LogicalResult SimReferencePathNBAEnqueueOp::verify() {
  if (getDestination().getType().getElementType() != getValue().getType())
    return emitOpError("value type must match the referenced element");
  return success();
}

LogicalResult SimArgumentRefFromRefOp::verify() {
  if (!haveCompatibleArgumentRefLayout(getInput().getType().getElementType(),
                                       getResult().getType().getElementType()))
    return emitOpError(
        "input and result element types must have equivalent packed layouts");
  return success();
}

LogicalResult SimArgumentRefFromManagedOp::verify() {
  if (!haveCompatibleArgumentRefLayout(getInput().getType().getElementType(),
                                       getResult().getType().getElementType()))
    return emitOpError(
        "input and result element types must have equivalent packed layouts");
  return success();
}

LogicalResult SimArgumentRefRetypeOp::verify() {
  if (!haveCompatibleArgumentRefLayout(getInput().getType().getElementType(),
                                       getResult().getType().getElementType()))
    return emitOpError(
        "input and result element types must have equivalent packed layouts");
  return success();
}

LogicalResult SimReferencePathIndexOp::verify() {
  Type containerType = getContainer().getType();
  Type elementType;
  if (auto array = dyn_cast<DynamicArrayType>(containerType))
    elementType = array.getElementType();
  else if (auto queue = dyn_cast<QueueType>(containerType))
    elementType = queue.getElementType();
  else
    return emitOpError("container must be a dynamic array or queue");
  if (elementType != getResult().getType().getElementType())
    return emitOpError("result element must match the container element");
  if (getOwnerReference() &&
      getOwnerReference().getType().getElementType() != containerType)
    return emitOpError("owner reference must refer to the container type");
  return success();
}

LogicalResult SimReferencePathAssocOp::verify() {
  AssocArrayType array = getArray().getType();
  if (failed(verifyAssocKey(getOperation(), array, getKey().getType())))
    return failure();
  if (array.getElementType() != getResult().getType().getElementType())
    return emitOpError("result element must match the associative element");
  if (getOwnerReference().getType().getElementType() != array)
    return emitOpError("owner reference must refer to the associative array");
  return success();
}

LogicalResult SimReferencePathStringCharacterOp::verify() {
  if (!getResult().getType().getElementType().isInteger(8))
    return emitOpError("result must refer to one eight-bit character");
  if (!isa<StringType>(getOwnerReference().getType().getElementType()))
    return emitOpError("owner reference must refer to a string");
  return success();
}

LogicalResult SimReferencePathAggregateElementOp::verify() {
  auto array = dyn_cast<UnpackedArrayType>(
      getOwnerReference().getType().getElementType());
  if (!array)
    return emitOpError("owner reference must refer to a fixed unpacked array");
  if (array.getElementType() != getResult().getType().getElementType())
    return emitOpError("result element must match the array element");
  if (static_cast<int64_t>(getLeft()) != array.getLeft() ||
      static_cast<int64_t>(getRight()) != array.getRight())
    return emitOpError("declared bounds do not match the owner array");
  std::optional<uint64_t> span = getArrayElementStorageStride(array);
  if (!span || getElementSpan() != *span || getElementSpan() == 0)
    return emitOpError("element span does not match the owner array layout");
  if (getTypeId() == 0 || getValueSize() == 0 || getAlignment() == 0 ||
      getValueSize() % getAlignment() != 0)
    return emitOpError("element metadata is invalid");
  if (getTraceOffsets().size() != getTraceKinds().size())
    return emitOpError("trace offset and kind inventories must match");
  return success();
}

LogicalResult SimReferencePathAggregateSliceOp::verify() {
  if (!isAggregateType(getOwnerReference().getType().getElementType()))
    return emitOpError("owner reference must refer to a fixed aggregate");
  if (getValueSize() > UINT64_MAX / 8)
    return emitOpError("selected element size overflows the bit layout");
  uint64_t width = getBitWidth() ? getBitWidth() : getValueSize() * 8;
  if (!getOwnerBitWidth() || !getOwnerPlaneSize() ||
      getOwnerPlaneSize() > UINT64_MAX / 8 ||
      getOwnerBitWidth() > getOwnerPlaneSize() * 8 ||
      getBitOffset() > getOwnerBitWidth() ||
      width > getOwnerBitWidth() - getBitOffset())
    return emitOpError("selected bit window is outside the owner layout");
  if (!getTypeId() || !getValueSize() || !getAlignment() ||
      (getAlignment() & (getAlignment() - 1)) ||
      getValueSize() % getAlignment() != 0 || width > getValueSize() * 8 ||
      getTraceOffsets().size() != getTraceKinds().size())
    return emitOpError("selected element metadata is invalid");
  auto resultWidth = getPackedWidth(getResult().getType().getElementType());
  if (resultWidth && *resultWidth != width)
    return emitOpError("selected bit width must match the result element type");
  bool fourState = static_cast<uint32_t>(getElementFlags()) &
                   static_cast<uint32_t>(ElementFlags::FourState);
  if (fourState && !getOwnerFourState())
    return emitOpError("four-state selection requires an owner unknown plane");
  return success();
}

LogicalResult SimArgumentRefFromPathOp::verify() {
  if (!haveCompatibleArgumentRefLayout(getInput().getType().getElementType(),
                                       getResult().getType().getElementType()))
    return emitOpError(
        "input and result element types must have equivalent packed layouts");
  return success();
}

LogicalResult SimArgumentRefLoadOp::verify() {
  if (getReference().getType().getElementType() != getResult().getType())
    return emitOpError("result type must match the referenced element");
  return success();
}

LogicalResult SimArgumentRefStoreOp::verify() {
  if (getReference().getType().getElementType() != getValue().getType())
    return emitOpError("value type must match the referenced element");
  return success();
}

LogicalResult SimClassRootBindOp::verify() {
  Type type = getObject().getType();
  SmallVector<ManagedHandleSlot, 2> slots;
  if (isa<ManagedRefType>(type))
    slots.push_back(
        {0, static_cast<uint32_t>(ManagedHandleKind::Class), false});
  else if (isa<ArgumentRefType>(type))
    slots.push_back(
        {0,
         static_cast<uint32_t>(ManagedHandleKind::Class) |
             static_cast<uint32_t>(ManagedHandleKind::ReferencePath),
         false});
  else if (!getManagedHandleSlots(type, slots))
    return emitOpError("rooted value has no fixed managed layout");
  auto selected = llvm::find_if(slots, [&](const ManagedHandleSlot &slot) {
    return slot.bitOffset == getBitOffset();
  });
  if (selected == slots.end())
    return emitOpError("bit offset does not select a managed handle");
  ManagedRootMode expectedMode = selected->conditional
                                     ? ManagedRootMode::Candidate
                                     : ManagedRootMode::Exact;
  if (getMode() != expectedMode || getKindMask() != selected->kindMask)
    return emitOpError("root mode or managed-kind mask disagrees with type");
  return success();
}

LogicalResult
SimClassDirectCallOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  if (failed(verifyVPICallSite(*this, symbolTable, false)))
    return failure();
  auto callee =
      symbolTable.lookupNearestSymbolFrom<SimFuncOp>(*this, getCalleeAttr());
  if (!callee)
    return emitOpError("references an unknown method implementation");
  if (callee.getEntryKind() != EntryKind::Function)
    return emitOpError("must reference a zero-time function implementation");
  FunctionType type = callee.getFunctionType();
  SmallVector<Type> inputs;
  inputs.push_back(getReceiver().getType());
  llvm::append_range(inputs, getArguments().getTypes());
  // Context is supplied by the containing executable function.
  if (type.getNumInputs() != inputs.size() + 1 ||
      !isa<ContextType>(type.getInput(0)) ||
      !llvm::equal(type.getInputs().drop_front(), inputs) ||
      !llvm::equal(type.getResults(), getResultTypes()))
    return emitOpError("operands or results do not match the method");
  return success();
}

LogicalResult SimClassDispatchTargetsOp::verifySymbolUses(
    SymbolTableCollection &symbolTable) {
  for (Attribute attribute : getTargets()) {
    auto reference = dyn_cast<SymbolRefAttr>(attribute);
    auto method =
        reference ? symbolTable.lookupNearestSymbolFrom<SimClassMethodDeclOp>(
                        *this, reference)
                  : SimClassMethodDeclOp{};
    if (!method || !method.getIsVirtual())
      return emitOpError("references an unknown non-virtual dispatch target");
  }
  return success();
}

LogicalResult
SimClassVirtualCallOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  if (failed(verifyVPICallSite(*this, symbolTable, false)))
    return failure();
  auto method = symbolTable.lookupNearestSymbolFrom<SimClassMethodDeclOp>(
      *this, getMethodAttr());
  if (!method || !method.getIsVirtual() || !method.getSlot() ||
      *method.getSlot() != getSlot())
    return emitOpError("references an unknown or incompatible virtual slot");
  if (getSignatureId() == 0 || !method.getSignatureIdAttr() ||
      *method.getSignatureId() != getSignatureId())
    return emitOpError("signature ID does not match the virtual method");
  auto type = cast<FunctionType>(method.getFunctionType());
  SmallVector<Type> inputs;
  inputs.push_back(getReceiver().getType());
  llvm::append_range(inputs, getArguments().getTypes());
  if (type.getNumInputs() != inputs.size() + 1 ||
      !llvm::equal(type.getInputs().drop_front(), inputs) ||
      !llvm::equal(type.getResults(), getResultTypes()))
    return emitOpError()
           << "operands or results do not match the method slot (expected "
           << type << ", got inputs " << TypeRange(inputs) << " and results "
           << getResultTypes() << ")";
  return success();
}

LogicalResult
SimVPIClassCallDispatchOp::verifySymbolUses(SymbolTableCollection &symbols) {
  if (!isa<SimDesignOp>((*this)->getParentOp()))
    return emitOpError("must be directly contained in a simulation design");
  auto call =
      symbols.lookupNearestSymbolFrom<SimVPICallDeclOp>(*this, getCallAttr());
  if (!call || !call.getIsIndirect())
    return emitOpError("must name an indirect VPI call declaration");
  auto method = symbols.lookupNearestSymbolFrom<SimClassMethodDeclOp>(
      *this, getMethodAttr());
  if (!method || !method.getIsVirtual() || method.getIsStatic() ||
      !method.getSlot() || !method.getSignatureId() ||
      !*method.getSignatureId())
    return emitOpError("must name an exact virtual method slot and signature");
  auto receiver = cast<ClassHandleType>(getReceiverType());
  auto declaration = symbols.lookupNearestSymbolFrom<SimClassDeclOp>(
      *this, receiver.getClassName());
  if (!declaration)
    return emitOpError("receiver type must name a declared class");
  auto signature = cast<FunctionType>(method.getFunctionType());
  if (signature.getNumInputs() < 2 || signature.getInput(1) != receiver)
    return emitOpError("receiver type disagrees with the virtual method ABI");
  using Kind = reflection::VPIObjectKind;
  auto kind = static_cast<Kind>(call.getVpiKind());
  bool task = kind == Kind::TaskCall || kind == Kind::MethodTaskCall;
  if (method.getIsTask() != task ||
      (kind != Kind::FuncCall && kind != Kind::MethodFuncCall && !task))
    return emitOpError("method task/function kind disagrees with the call");
  return success();
}

LogicalResult SimDesignOp::verifyRegions() {
  if (failed(verifyVPIExpressionInventory(*this)))
    return failure();
  if (failed(verifyVPIInterfaceDispatch(*this)))
    return failure();
  // One cached table for the whole design walk below. The uncached lookups
  // rescan this design's block per call, which is quadratic across classes.
  SymbolTableCollection symbolTable;
  if (auto precision = getTimePrecisionFsAttr();
      precision &&
      (precision.getValue().isNegative() || precision.getValue().isZero()))
    return emitOpError("time precision must be a positive femtosecond value");
  llvm::DenseSet<uint64_t> scopeIds, coverageScopeIds, codeUnitIds,
      statementIds, statementSiteIds, storageIds, netIds, driverIds, portIds,
      connectionIds, covergroupIds, classIds, vpiAnchorIds, vpiNetIdentityIds,
      typespecIds, nettypeIds, enumConstIds, vpiCallIds;
  llvm::DenseMap<uint64_t, SimScopeDeclOp> scopes;
  llvm::DenseMap<uint64_t, SimCodeUnitDeclOp> codeUnits;
  llvm::DenseMap<uint64_t, SimStatementDeclOp> statements;
  llvm::DenseMap<uint64_t, SimVPIObjectAnchorOp> vpiAnchorsById;
  llvm::DenseMap<uint64_t, SimStorageDeclOp> storagesById;
  SmallVector<SimStatementDeclOp> statementInventory;
  SmallVector<SimStatementSiteDeclOp> statementSites;
  SmallVector<SimVPIThreadOriginBindingOp> threadOrigins;
  SmallVector<SimVPICodeUnitScopeBindingOp> codeUnitScopes;
  SmallVector<SimVPIInitializerBindingOp> initializerBindings;
  SmallVector<SimVPICallDeclOp> vpiCalls;
  SmallVector<SimVPILocalDeclOp> vpiLocals;
  SmallVector<SimVPIStaticLocalBindingOp> staticLocals;
  SmallVector<SimVPIForeachDeclOp> foreachDeclarations;
  llvm::DenseSet<uint64_t> vpiLocalIds;
  llvm::DenseMap<Attribute, SimVPIClassCallDispatchOp> vpiClassDispatchCalls;
  SmallVector<SimVPIStatementRelationDeclOp> statementRelations;
  SmallVector<SimVPIRelationDeclOp> vpiRelations;
  DenseSet<Attribute> primitiveDefinitionSources;
  SmallVector<SimVPIDefinitionMemberDeclOp> definitionMembers;
  SmallVector<SimVPIDefinitionSpecializationDeclOp> definitionSpecializations;
  SmallVector<SimVPIDefinitionMemberSpecializationOp>
      definitionMemberSpecializations;
  SmallVector<SimVPIDefinitionMemberInstanceBindingOp>
      definitionMemberInstanceBindings;
  SmallVector<SimVPIDefinitionMemberInstanceRelationOp>
      definitionMemberInstanceRelations;
  llvm::DenseMap<uint64_t, SimVPINetIdentityDeclOp> vpiNetIdentitiesById;
  SmallVector<SimVPINetIdentityDeclOp> vpiNetIdentities;
  SmallVector<SimVPIObjectAnchorOp> vpiAnchors;
  SmallVector<SimVPINettypeDeclOp> nettypes;
  SmallVector<SimVPITypespecDeclOp> typespecs;
  SmallVector<SimVPIUnresolvedTypeAliasDeclOp> unresolvedAliases;
  SmallVector<SimVPILexicalClassTypespecDeclOp> lexicalClassTypes;
  SmallVector<SimVPIClassExtendsDeclOp> lexicalExtends;
  SmallVector<SimVPIClassParameterAssignDeclOp> lexicalClassAssignments;
  DenseSet<uint64_t> boxTypeIDs;
  SmallVector<SimVPIClassMemberDeclOp> classMembers;
  SmallVector<SimVPIEnumConstDeclOp> enumConstants;
  SmallVector<SimStorageDeclOp> storages;
  llvm::DenseMap<uint64_t, Type> storageTypes, netTypes, driverTypes;
  llvm::DenseMap<uint64_t, SimNetDeclOp> nets;
  llvm::DenseMap<uint64_t, SimPortDeclOp> portsById;
  llvm::DenseMap<uint64_t, NetResolutionKind> netResolutions;
  SmallVector<SimNetConnectDeclOp> netConnections;
  llvm::StringMap<SimClassDeclOp> classes;
  SmallVector<SimFuncOp> functions;
  bool sawRoot = false;
  for (Operation &op : getBody().front()) {
    auto addId = [&](IntegerAttr id, llvm::DenseSet<uint64_t> &ids,
                     StringRef kind) -> LogicalResult {
      uint64_t value = id.getValue().getZExtValue();
      if (!ids.insert(value).second)
        return op.emitOpError() << "duplicate " << kind << " ID " << value;
      return success();
    };
    if (auto scope = dyn_cast<SimScopeDeclOp>(op)) {
      if (failed(addId(scope.getIdAttr(), scopeIds, "scope")))
        return failure();
      if (scope.getCoverageIdAttr() &&
          failed(addId(scope.getCoverageIdAttr(), coverageScopeIds,
                       "coverage scope")))
        return failure();
      scopes[scope.getId()] = scope;
      if (!scope.getParentAttr()) {
        if (sawRoot)
          return scope.emitOpError(
              "design must contain exactly one root scope");
        sawRoot = true;
      }
    } else if (auto codeUnit = dyn_cast<SimCodeUnitDeclOp>(op)) {
      if (failed(addId(codeUnit.getIdAttr(), codeUnitIds, "code-unit")))
        return failure();
      codeUnits[codeUnit.getId()] = codeUnit;
    } else if (auto statement = dyn_cast<SimStatementDeclOp>(op)) {
      if (failed(addId(statement.getIdAttr(), statementIds, "statement")))
        return failure();
      statements[statement.getId()] = statement;
      statementInventory.push_back(statement);
    } else if (auto local = dyn_cast<SimVPILocalDeclOp>(op)) {
      if (failed(
              addId(local.getIdAttr(), vpiLocalIds, "VPI local declaration")))
        return failure();
      vpiLocals.push_back(local);
    } else if (auto declaration = dyn_cast<SimVPIForeachDeclOp>(op)) {
      foreachDeclarations.push_back(declaration);
    } else if (auto binding = dyn_cast<SimVPIStaticLocalBindingOp>(op)) {
      staticLocals.push_back(binding);
    } else if (auto call = dyn_cast<SimVPICallDeclOp>(op)) {
      if (failed(addId(call.getIdAttr(), vpiCallIds, "VPI call-site")))
        return failure();
      vpiCalls.push_back(call);
    } else if (auto dispatch = dyn_cast<SimVPIClassCallDispatchOp>(op)) {
      if (!vpiClassDispatchCalls.try_emplace(dispatch.getCallAttr(), dispatch)
               .second)
        return dispatch.emitOpError(
            "duplicates a class call dispatch contract");
    } else if (auto binding = dyn_cast<SimVPIInitializerBindingOp>(op)) {
      initializerBindings.push_back(binding);
    } else if (auto binding = dyn_cast<SimVPICodeUnitScopeBindingOp>(op)) {
      codeUnitScopes.push_back(binding);
    } else if (auto binding = dyn_cast<SimVPIThreadOriginBindingOp>(op)) {
      threadOrigins.push_back(binding);
    } else if (auto site = dyn_cast<SimStatementSiteDeclOp>(op)) {
      if (failed(addId(site.getIdAttr(), statementSiteIds, "statement-site")))
        return failure();
      statementSites.push_back(site);
    } else if (auto relation = dyn_cast<SimVPIStatementRelationDeclOp>(op)) {
      statementRelations.push_back(relation);
    } else if (auto relation = dyn_cast<SimVPIRelationDeclOp>(op)) {
      vpiRelations.push_back(relation);
    } else if (auto binding = dyn_cast<SimVPIDefinitionBindingOp>(op)) {
      if (!primitiveDefinitionSources.insert(binding.getSourceAttr()).second)
        return binding.emitOpError("duplicate primitive definition binding");
    } else if (auto member = dyn_cast<SimVPIDefinitionMemberDeclOp>(op)) {
      definitionMembers.push_back(member);
    } else if (auto specialization =
                   dyn_cast<SimVPIDefinitionSpecializationDeclOp>(op)) {
      definitionSpecializations.push_back(specialization);
    } else if (auto binding =
                   dyn_cast<SimVPIDefinitionMemberSpecializationOp>(op)) {
      definitionMemberSpecializations.push_back(binding);
    } else if (auto binding =
                   dyn_cast<SimVPIDefinitionMemberInstanceBindingOp>(op)) {
      definitionMemberInstanceBindings.push_back(binding);
    } else if (auto relation =
                   dyn_cast<SimVPIDefinitionMemberInstanceRelationOp>(op)) {
      definitionMemberInstanceRelations.push_back(relation);
    } else if (auto identity = dyn_cast<SimVPINetIdentityDeclOp>(op)) {
      if (failed(addId(identity.getIdAttr(), vpiNetIdentityIds,
                       "VPI net identity")))
        return failure();
      vpiNetIdentities.push_back(identity);
      vpiNetIdentitiesById[identity.getId()] = identity;
    } else if (auto anchor = dyn_cast<SimVPIObjectAnchorOp>(op)) {
      if (failed(addId(anchor.getInventoryIdAttr(), vpiAnchorIds,
                       "VPI object anchor")))
        return failure();
      vpiAnchors.push_back(anchor);
      vpiAnchorsById[anchor.getInventoryId()] = anchor;
    } else if (auto member = dyn_cast<SimVPIClassMemberDeclOp>(op)) {
      if (failed(addId(member.getIdAttr(), vpiAnchorIds, "VPI source inventory")))
        return failure();
      classMembers.push_back(member);
    } else if (auto type = dyn_cast<SimVPILexicalClassTypespecDeclOp>(op)) {
      if (failed(addId(type.getIdAttr(), typespecIds, "VPI typespec")))
        return failure();
      lexicalClassTypes.push_back(type);
    } else if (auto clause = dyn_cast<SimVPIClassExtendsDeclOp>(op)) {
      if (failed(
              addId(clause.getIdAttr(), vpiAnchorIds, "VPI source inventory")))
        return failure();
      lexicalExtends.push_back(clause);
    } else if (auto assignment =
                   dyn_cast<SimVPIClassParameterAssignDeclOp>(op)) {
      if (failed(addId(assignment.getIdAttr(), vpiAnchorIds,
                       "VPI source inventory")))
        return failure();
      lexicalClassAssignments.push_back(assignment);
    } else if (auto alias = dyn_cast<SimVPIUnresolvedTypeAliasDeclOp>(op)) {
      if (failed(addId(alias.getIdAttr(), typespecIds, "typespec")))
        return failure();
      unresolvedAliases.push_back(alias);
    } else if (auto typespec = dyn_cast<SimVPITypespecDeclOp>(op)) {
      if (failed(addId(typespec.getIdAttr(), typespecIds, "VPI typespec")))
        return failure();
      typespecs.push_back(typespec);
    } else if (auto nettype = dyn_cast<SimVPINettypeDeclOp>(op)) {
      if (failed(addId(nettype.getIdAttr(), nettypeIds, "VPI nettype")))
        return failure();
      nettypes.push_back(nettype);
    } else if (auto enumConstant = dyn_cast<SimVPIEnumConstDeclOp>(op)) {
      if (failed(addId(enumConstant.getIdAttr(), enumConstIds,
                       "VPI enum constant")))
        return failure();
      enumConstants.push_back(enumConstant);
    } else if (auto storage = dyn_cast<SimStorageDeclOp>(op)) {
      if (failed(addId(storage.getIdAttr(), storageIds, "storage")))
        return failure();
      storageTypes[storage.getId()] = storage.getType();
      storages.push_back(storage);
      storagesById[storage.getId()] = storage;
    } else if (auto net = dyn_cast<SimNetDeclOp>(op)) {
      if (failed(addId(net.getIdAttr(), netIds, "net")))
        return failure();
      nets[net.getId()] = net;
      netTypes[net.getId()] = net.getType();
      netResolutions[net.getId()] = net.getResolutionKind();
    } else if (auto driver = dyn_cast<SimDriverDeclOp>(op)) {
      if (failed(addId(driver.getIdAttr(), driverIds, "driver")))
        return failure();
      driverTypes[driver.getId()] = driver.getType();
    } else if (auto port = dyn_cast<SimPortDeclOp>(op)) {
      if (failed(addId(port.getIdAttr(), portIds, "port")))
        return failure();
      portsById[port.getId()] = port;
    } else if (auto connection = dyn_cast<SimNetConnectDeclOp>(op)) {
      if (failed(
              addId(connection.getIdAttr(), connectionIds, "net connection")))
        return failure();
      netConnections.push_back(connection);
    } else if (auto covergroup = dyn_cast<SimCovergroupDeclOp>(op)) {
      if (failed(addId(covergroup.getSchemaTypeAttr(), covergroupIds,
                       "covergroup schema type")))
        return failure();
    } else if (auto classDecl = dyn_cast<SimClassDeclOp>(op)) {
      if (failed(addId(classDecl.getIdAttr(), classIds, "class")))
        return failure();
      classes[classDecl.getSymName()] = classDecl;
    } else if (auto function = dyn_cast<SimFuncOp>(op)) {
      functions.push_back(function);
    }
  }

  llvm::DenseMap<Attribute,
                 llvm::DenseMap<uint64_t, SimVPIDefinitionMemberDeclOp>>
      definitionMemberOrdinals;
  SmallVector<Attribute> definitionMemberOrder;
  llvm::DenseMap<Attribute, llvm::DenseSet<StringAttr>> definitionMemberNames;
  for (SimVPIDefinitionMemberDeclOp member : definitionMembers) {
    if (!definitionMemberNames[member.getDefinitionAttr()]
             .insert(member.getMemberNameAttr())
             .second)
      return member.emitOpError(
          "duplicates a declaration name in the same VPI definition");
    auto [definitionEntry, newDefinition] =
        definitionMemberOrdinals.try_emplace(member.getDefinitionAttr());
    if (newDefinition)
      definitionMemberOrder.push_back(member.getDefinitionAttr());
    auto &ordinals = definitionEntry->second;
    auto [sameOrdinal, inserted] =
        ordinals.try_emplace(member.getOrdinal(), member);
    if (!inserted)
      return member.emitOpError()
             << "duplicates member ordinal " << member.getOrdinal()
             << " in the same VPI definition (first declared by "
             << sameOrdinal->second.getSymName() << ")";
  }
  for (Attribute definition : definitionMemberOrder) {
    const auto &ordinals = definitionMemberOrdinals.find(definition)->second;
    for (uint64_t ordinal = 0; ordinal != ordinals.size(); ++ordinal)
      if (!ordinals.contains(ordinal))
        return emitOpError()
               << "VPI definition " << definition
               << " member ordinals must be dense declaration order";
  }

  for (auto anchor : vpiAnchors) {
    using Kind = reflection::VPIObjectKind;
    if (anchor.getVpiKind() != static_cast<uint32_t>(Kind::Parameter) &&
        anchor.getVpiKind() != static_cast<uint32_t>(Kind::TypeParameter))
      continue;
    auto scope = scopes.find(anchor.getEnclosingScopeId());
    if (scope == scopes.end() || !scope->second.getVpiDefinitionAttr())
      continue;
    if (auto parent = anchor.getParentAttr()) {
      auto owner = symbolTable.lookupNearestSymbolFrom<SimVPIObjectAnchorOp>(
          anchor, parent);
      auto backing = owner ? owner.getBackingAttr() : VPIObjectBackingAttr{};
      if (!backing || backing.getKind() != VPIObjectBackingKind::Scope ||
          backing.getId().getValue().getZExtValue() !=
              anchor.getEnclosingScopeId())
        continue;
    }
    auto members =
        definitionMemberOrdinals.find(scope->second.getVpiDefinitionAttr());
    if (members == definitionMemberOrdinals.end())
      continue;
    if (llvm::any_of(members->second, [](const auto &entry) {
          auto member = entry.second;
          uint32_t kind = member.getVpiKind();
          return kind == static_cast<uint32_t>(Kind::Parameter) ||
                 kind == static_cast<uint32_t>(Kind::TypeParameter);
        }))
      return anchor.emitOpError("parameter inventory cannot mix shared "
                                "definition members and instance anchors");
  }

  llvm::DenseMap<Attribute, llvm::DenseSet<Attribute>> specializedMembers;
  llvm::DenseMap<std::pair<Attribute, Attribute>, VPITypeSemanticsAttr>
      specializedMemberTypes;
  for (SimVPIDefinitionMemberSpecializationOp binding :
       definitionMemberSpecializations) {
    auto &members = specializedMembers[binding.getSpecializationAttr()];
    if (!members.insert(binding.getMemberAttr()).second)
      return binding.emitOpError(
          "duplicates a member binding in the same VPI specialization");
    specializedMemberTypes.try_emplace(
        std::make_pair(Attribute(binding.getSpecializationAttr()),
                       Attribute(binding.getMemberAttr())),
        binding.getEffectiveType());
  }
  for (SimVPIDefinitionSpecializationDeclOp specialization :
       definitionSpecializations) {
    size_t expectedMemberCount = 0;
    if (auto definition =
            definitionMemberOrdinals.find(specialization.getDefinitionAttr());
        definition != definitionMemberOrdinals.end())
      expectedMemberCount = definition->second.size();
    size_t actualMemberCount = 0;
    auto specializationMembers = specializedMembers.find(
        FlatSymbolRefAttr::get(specialization.getSymNameAttr()));
    if (specializationMembers != specializedMembers.end())
      actualMemberCount = specializationMembers->second.size();
    if (actualMemberCount != expectedMemberCount)
      return specialization.emitOpError()
             << "must bind every member in its VPI definition (expected "
             << expectedMemberCount << ", got " << actualMemberCount << ")";
  }
  for (Operation &op : getBody().front()) {
    auto scope = dyn_cast<SimScopeDeclOp>(op);
    if (!scope || !scope.getVpiDefinitionAttr() ||
        scope.getVpiSpecializationAttr())
      continue;
    auto definition =
        definitionMemberOrdinals.find(scope.getVpiDefinitionAttr());
    if (definition != definitionMemberOrdinals.end() &&
        !definition->second.empty())
      return scope.emitOpError(
          "VPI definition with members requires a specialization");
  }
  llvm::DenseMap<std::pair<uint64_t, Attribute>,
                 SimVPIDefinitionMemberInstanceBindingOp>
      instanceMemberBindings;
  for (SimVPIDefinitionMemberInstanceBindingOp binding :
       definitionMemberInstanceBindings) {
    auto scope = scopes.find(binding.getScopeId());
    if (scope == scopes.end())
      return binding.emitOpError("references an unknown scope ID");
    auto member =
        symbolTable.lookupNearestSymbolFrom<SimVPIDefinitionMemberDeclOp>(
            binding, binding.getMemberAttr());
    if (!member)
      continue;
    if (scope->second.getVpiDefinitionAttr() != member.getDefinitionAttr())
      return binding.emitOpError(
          "member does not belong to the scope's VPI definition");
    if (member.getVpiKind() !=
        static_cast<uint32_t>(reflection::VPIObjectKind::IODecl))
      return binding.emitOpError(
          "expression endpoint member is not a VPI IO declaration");

    uint64_t targetId =
        binding.getExprTarget().getId().getValue().getZExtValue();
    uint64_t targetScope = 0;
    uint32_t targetKind = 0;
    VPITypeSemanticsAttr targetType;
    auto missingTarget = [&]() -> LogicalResult {
      return binding.emitOpError()
             << "references an unknown expression "
             << stringifyVPIObjectRefKind(binding.getExprTarget().getKind())
             << " ID " << targetId;
    };
    switch (binding.getExprTarget().getKind()) {
    case VPIObjectRefKind::Storage: {
      auto found = storagesById.find(targetId);
      if (found == storagesById.end())
        return missingTarget();
      targetScope = found->second.getScopeId();
      targetType = found->second.getVpiTypeAttr();
      targetKind = vpiKindForStorage(targetType);
      break;
    }
    case VPIObjectRefKind::Net: {
      auto found = nets.find(targetId);
      if (found == nets.end())
        return missingTarget();
      targetScope = found->second.getScopeId();
      targetType = found->second.getVpiTypeAttr();
      targetKind = vpiKindForNet(targetType);
      break;
    }
    case VPIObjectRefKind::NetIdentity: {
      auto found = vpiNetIdentitiesById.find(targetId);
      if (found == vpiNetIdentitiesById.end())
        return missingTarget();
      targetScope = found->second.getScopeId();
      targetType = found->second.getVpiTypeAttr();
      targetKind = vpiKindForNet(targetType);
      break;
    }
    case VPIObjectRefKind::Statement:
    case VPIObjectRefKind::Port:
    case VPIObjectRefKind::Symbol:
    case VPIObjectRefKind::DefinitionMember:
      return binding.emitOpError(
          "expression endpoint must be whole storage or a declared net");
    }
    VPIIODirection direction =
        member.getDirection().value_or(VPIIODirection::Undefined);
    if (direction == VPIIODirection::Ref) {
      auto specialization = scope->second.getVpiSpecializationAttr();
      auto effective = specialization
                           ? specializedMemberTypes.find(std::make_pair(
                                 Attribute(specialization),
                                 Attribute(binding.getMemberAttr())))
                           : specializedMemberTypes.end();
      if (effective == specializedMemberTypes.end() ||
          !areEquivalentVPIRefTypes(effective->second, targetType))
        return binding.emitOpError(
                   "ref actual type does not match its specialized member "
                   "type: formal ")
               << (effective == specializedMemberTypes.end()
                       ? Attribute{}
                       : Attribute(effective->second))
               << ", actual " << Attribute(targetType);
    }
    // An ordinary formal resolves inside its elaborated instance. A ref
    // formal instead resolves to the caller's actual and is normally owned by
    // another scope.
    if (direction != VPIIODirection::Ref && targetScope != binding.getScopeId())
      return binding.emitOpError(
          "non-ref expression endpoint belongs to a different scope");
    const auto *exprEdge = reflection::findVPITraversal(
        static_cast<uint32_t>(reflection::VPIObjectKind::IODecl),
        static_cast<uint32_t>(reflection::VPIRelationKind::ExprRel),
        reflection::VPITraversalMode::Handle);
    const auto *actualEdge = reflection::findVPITraversal(
        static_cast<uint32_t>(reflection::VPIObjectKind::RefObj),
        static_cast<uint32_t>(reflection::VPIRelationKind::ActualRel),
        reflection::VPITraversalMode::Handle);
    bool legalEndpoint =
        direction == VPIIODirection::Ref
            ? binding.getExprTarget().getKind() == VPIObjectRefKind::Storage &&
                  exprEdge && actualEdge &&
                  reflection::vpiObjectSetContains(
                      exprEdge->targets,
                      static_cast<uint32_t>(
                          reflection::VPIObjectKind::RefObj)) &&
                  reflection::vpiObjectSetContains(actualEdge->targets,
                                                   targetKind)
            : exprEdge && reflection::vpiObjectSetContains(exprEdge->targets,
                                                           targetKind);
    if (!legalEndpoint)
      return binding.emitOpError(
          "expression endpoint is not legal for a VPI IO declaration");
    bool virtualInterface =
        targetKind ==
        static_cast<uint32_t>(reflection::VPIObjectKind::VirtualInterfaceVar);
    if (virtualInterface ? direction != VPIIODirection::Undefined &&
                               direction != VPIIODirection::Ref
                         : direction != VPIIODirection::Input &&
                               direction != VPIIODirection::Output &&
                               direction != VPIIODirection::InOut &&
                               direction != VPIIODirection::Ref)
      return binding.emitOpError(
          "expression endpoint is incompatible with the IO declaration "
          "direction");
    auto [sameBinding, inserted] = instanceMemberBindings.try_emplace(
        std::make_pair(binding.getScopeId(),
                       Attribute(binding.getMemberAttr())),
        binding);
    if (!inserted)
      return binding.emitOpError(
          "duplicates an instance binding for the same VPI member");
  }

  auto memberRelationKey = [](uint64_t scope, Attribute member,
                              uint32_t selector, uint32_t mode) {
    return std::make_tuple(scope, member, selector, mode);
  };
  llvm::DenseMap<
      std::tuple<uint64_t, Attribute, uint32_t, uint32_t>,
      llvm::DenseMap<uint64_t, SimVPIDefinitionMemberInstanceRelationOp>>
      memberRelationOrdinals;
  llvm::DenseMap<std::tuple<uint64_t, Attribute, uint32_t, uint32_t>,
                 llvm::DenseSet<uint64_t>>
      memberRelationTargets;
  llvm::DenseSet<std::pair<uint64_t, uint32_t>> memberRelationInverseKeys;
  auto isStrictDescendantScope = [&](uint64_t candidate, uint64_t ancestor) {
    if (candidate == ancestor)
      return false;
    for (size_t depth = 0; depth != scopes.size(); ++depth) {
      auto scope = scopes.find(candidate);
      if (scope == scopes.end() || !scope->second.getParentAttr())
        return false;
      candidate = *scope->second.getParent();
      if (candidate == ancestor)
        return true;
    }
    return false;
  };
  for (SimVPIDefinitionMemberInstanceRelationOp relation :
       definitionMemberInstanceRelations) {
    auto scope = scopes.find(relation.getScopeId());
    if (scope == scopes.end())
      return relation.emitOpError("references an unknown scope ID");
    auto member =
        symbolTable.lookupNearestSymbolFrom<SimVPIDefinitionMemberDeclOp>(
            relation, relation.getMemberAttr());
    if (!member)
      continue;
    if (scope->second.getVpiDefinitionAttr() != member.getDefinitionAttr())
      return relation.emitOpError(
          "member does not belong to the scope's VPI definition");
    if (member.getVpiKind() !=
            static_cast<uint32_t>(reflection::VPIObjectKind::IODecl) ||
        member.getDirection().value_or(VPIIODirection::Undefined) !=
            VPIIODirection::Ref)
      return relation.emitOpError(
          "source member does not materialize a RefObj");
    if (!instanceMemberBindings.contains(std::make_pair(
            relation.getScopeId(), Attribute(relation.getMemberAttr()))))
      return relation.emitOpError(
          "source member has no expression endpoint in this instance");
    if (relation.getTarget().getKind() != VPIObjectRefKind::Port)
      return relation.emitOpError("target must be a canonical VPI port");
    uint64_t targetId = relation.getTarget().getId().getValue().getZExtValue();
    auto port = portsById.find(targetId);
    if (port == portsById.end())
      return relation.emitOpError("references an unknown port target ID");
    auto mode = static_cast<reflection::VPITraversalMode>(
        static_cast<uint32_t>(relation.getMode()));
    const auto *edge = reflection::findVPITraversal(
        static_cast<uint32_t>(reflection::VPIObjectKind::RefObj),
        relation.getSelector(), mode);
    if (!edge ||
        edge->automaticRelation != reflection::VPIAutomaticRelation::
                                       DefinitionMemberInstanceRelation ||
        !reflection::vpiObjectSetContains(
            edge->targets,
            static_cast<uint32_t>(reflection::VPIObjectKind::Port)))
      return relation.emitOpError(
          "relation is not legal for a generated RefObj traversal");
    uint64_t targetScope = port->second.getScopeId();
    if (relation.getSelector() ==
            static_cast<uint32_t>(reflection::VPIObjectKind::Port) &&
        targetScope != relation.getScopeId())
      return relation.emitOpError(
          "vpiPort target must belong to the RefObj instance");
    if (relation.getSelector() ==
            static_cast<uint32_t>(reflection::VPIRelationKind::PortInstRel) &&
        !isStrictDescendantScope(targetScope, relation.getScopeId()))
      return relation.emitOpError(
          "vpiPortInst target must belong to a descendant instance");
    uint32_t inverseSelector = edge->inverseSelector;
    if (inverseSelector == 0)
      return relation.emitOpError(
          "generated RefObj traversal has no inverse selector");
    if (inverseSelector ==
            static_cast<uint32_t>(reflection::VPIRelationKind::LowConnRel) &&
        port->second.getDirection() != PortDirection::InOut)
      return relation.emitOpError(
          "owning ref port must have inout execution direction");
    if (!memberRelationInverseKeys.insert({targetId, inverseSelector}).second)
      return relation.emitOpError(
          "target port already has a reference connection");
    auto key = memberRelationKey(
        relation.getScopeId(), Attribute(relation.getMemberAttr()),
        relation.getSelector(), static_cast<uint32_t>(relation.getMode()));
    auto [sameOrdinal, insertedOrdinal] =
        memberRelationOrdinals[key].try_emplace(relation.getOrdinal(),
                                                relation);
    if (!insertedOrdinal)
      return relation.emitOpError("duplicates an ordinal in the same relation");
    if (!memberRelationTargets[key].insert(targetId).second)
      return relation.emitOpError("duplicates a target in the same relation");
  }
  for (const auto &entry : memberRelationOrdinals) {
    const auto &ordinals = entry.second;
    for (uint64_t ordinal = 0; ordinal != ordinals.size(); ++ordinal)
      if (!ordinals.contains(ordinal))
        return emitOpError(
            "VPI definition-member relation ordinals must be dense");
  }

  llvm::DenseMap<Attribute, SimVPIObjectAnchorOp> anchorsBySymbol;
  DenseSet<std::pair<Attribute, Attribute>> modportNames;
  DenseSet<std::pair<Attribute, Attribute>> constraintNames;
  for (SimVPIObjectAnchorOp anchor : vpiAnchors) {
    anchorsBySymbol[FlatSymbolRefAttr::get(anchor.getSymNameAttr())] = anchor;
    if (anchor.getVpiKind() ==
            static_cast<uint32_t>(reflection::VPIObjectKind::Modport) &&
        !modportNames
             .insert({anchor.getParentAttr(), anchor.getDebugNameAttr()})
             .second)
      return anchor.emitOpError(
          "duplicates a modport declaration name in the same interface");
    if (anchor.getVpiKind() ==
            static_cast<uint32_t>(reflection::VPIObjectKind::Constraint) &&
        !constraintNames
             .insert({anchor.getParentAttr(), anchor.getDebugNameAttr()})
             .second)
      return anchor.emitOpError(
          "duplicates a constraint declaration name in the same class");
  }

  llvm::DenseMap<Operation *, llvm::DenseSet<uint32_t>> arrayMemberOrdinals;
  llvm::DenseMap<Operation *, uint32_t> arrayElementCounts;
  llvm::DenseMap<Operation *, llvm::DenseMap<int64_t, uint32_t>> sparseOrdinals;
  for (SimVPIObjectAnchorOp array : vpiAnchors) {
    uint64_t count = 0;
    if (DenseI64ArrayAttr ranges = array.getIndexRangesAttr()) {
      count = 1;
      ArrayRef<int64_t> bounds = ranges.asArrayRef();
      for (size_t dimension = 0; dimension != bounds.size(); dimension += 2) {
        uint64_t distance =
            bounds[dimension] >= bounds[dimension + 1]
                ? static_cast<uint64_t>(bounds[dimension]) -
                      static_cast<uint64_t>(bounds[dimension + 1])
                : static_cast<uint64_t>(bounds[dimension + 1]) -
                      static_cast<uint64_t>(bounds[dimension]);
        count *= distance + 1;
      }
    } else if (DenseI64ArrayAttr sparse = array.getSparseIndicesAttr()) {
      count = sparse.size();
      auto &ordinals = sparseOrdinals[array.getOperation()];
      for (auto [ordinal, value] : llvm::enumerate(sparse.asArrayRef()))
        ordinals.try_emplace(value, static_cast<uint32_t>(ordinal));
    } else {
      continue;
    }
    arrayElementCounts[array.getOperation()] = static_cast<uint32_t>(count);
  }
  for (SimVPIObjectAnchorOp member : vpiAnchors) {
    FlatSymbolRefAttr parent = member.getParentAttr();
    if (!parent)
      continue;
    auto parentIt = anchorsBySymbol.find(parent);
    if (parentIt == anchorsBySymbol.end())
      continue;
    SimVPIObjectAnchorOp array = parentIt->second;
    auto countIt = arrayElementCounts.find(array.getOperation());
    if (countIt == arrayElementCounts.end())
      continue;
    DenseI64ArrayAttr memberAttr = member.getMemberIndicesAttr();
    if (!memberAttr)
      return member.emitOpError(
          "array member must carry its exact source indices");
    ArrayRef<int64_t> indices = memberAttr.asArrayRef();
    uint64_t ordinal = 0;
    if (DenseI64ArrayAttr ranges = array.getIndexRangesAttr()) {
      ArrayRef<int64_t> bounds = ranges.asArrayRef();
      if (indices.size() * 2 != bounds.size())
        return member.emitOpError(
            "array member index rank does not match its parent");
      for (size_t dimension = 0; dimension != indices.size(); ++dimension) {
        int64_t left = bounds[dimension * 2];
        int64_t right = bounds[dimension * 2 + 1];
        if (indices[dimension] < std::min(left, right) ||
            indices[dimension] > std::max(left, right))
          return member.emitOpError(
              "array member index is outside its parent range");
        uint64_t extent = left >= right ? static_cast<uint64_t>(left) -
                                              static_cast<uint64_t>(right) + 1
                                        : static_cast<uint64_t>(right) -
                                              static_cast<uint64_t>(left) + 1;
        uint64_t coordinate =
            left >= right ? static_cast<uint64_t>(left) -
                                static_cast<uint64_t>(indices[dimension])
                          : static_cast<uint64_t>(indices[dimension]) -
                                static_cast<uint64_t>(left);
        ordinal = ordinal * extent + coordinate;
      }
    } else {
      auto &ordinals = sparseOrdinals[array.getOperation()];
      auto found =
          indices.size() == 1 ? ordinals.find(indices.front()) : ordinals.end();
      if (found == ordinals.end())
        return member.emitOpError(
            "generate-array member index is absent from its parent");
      ordinal = found->second;
    }
    if (ordinal >= countIt->second ||
        !arrayMemberOrdinals[array.getOperation()]
             .insert(static_cast<uint32_t>(ordinal))
             .second)
      return member.emitOpError(
          "duplicates a source coordinate in its VPI array parent");
  }
  for (const auto &[array, count] : arrayElementCounts)
    if (arrayMemberOrdinals[array].size() != count)
      return cast<SimVPIObjectAnchorOp>(array).emitOpError(
          "does not have exactly one child for every source coordinate");

  llvm::DenseSet<Attribute> delegatedAnchors;
  for (SimStorageDeclOp storage : storages) {
    auto delegated = sim::get<sim::metadata::VpiIdentityDelegated>(storage);
    if (!delegated)
      continue;
    SimVPIObjectAnchorOp anchor =
        symbolTable.lookupNearestSymbolFrom<SimVPIObjectAnchorOp>(storage,
                                                                  delegated);
    if (!anchor ||
        anchor.getVpiKind() !=
            static_cast<uint32_t>(reflection::VPIObjectKind::NamedEventArray))
      return storage.emitOpError(
          "delegated VPI identity must reference a named-event-array anchor");
    if (storage.getScopeId() != anchor.getEnclosingScopeId() ||
        !storage.getHierarchicalName() ||
        *storage.getHierarchicalName() != anchor.getHierarchicalName())
      return storage.emitOpError(
          "delegated VPI identity disagrees with its anchor scope or name");
    SmallVector<int64_t> ranges;
    VPITypeSemanticsAttr semantic = storage.getVpiTypeAttr();
    while (semantic && semantic.getKind() == VPITypeKind::UnpackedArray) {
      llvm::append_range(ranges, semantic.getRange().asArrayRef());
      semantic = cast<VPITypeSemanticsAttr>(semantic.getChildren()[0]);
    }
    if (!semantic || semantic.getKind() != VPITypeKind::Event ||
        !anchor.getIndexRangesAttr() ||
        !llvm::equal(ranges, anchor.getIndexRangesAttr().asArrayRef()))
      return storage.emitOpError(
          "delegated VPI identity disagrees with its anchor array shape");
    if (!delegatedAnchors.insert(delegated).second)
      return storage.emitOpError(
          "multiple storages delegate the same public VPI identity");
  }

  llvm::DenseMap<Attribute, SmallVector<SimVPITypespecDeclOp>>
      interfaceTypespecs;
  llvm::DenseMap<uint64_t, SimVPITypespecDeclOp> anonymousEnumTypespecs;
  for (SimVPITypespecDeclOp typespec : typespecs) {
    VPITypeSemanticsAttr target = typespec.getTargetType();
    if (typespec.getOrigin() == VPITypespecOrigin::Interface &&
        target.getKind() == VPITypeKind::VirtualInterface) {
      Attribute specializationOwner = ArrayAttr::get(
          getContext(), {typespec.getOwnerAttr(), target.getName()});
      interfaceTypespecs[specializationOwner].push_back(typespec);
    }
    if (typespec.getOrigin() == VPITypespecOrigin::AnonymousEnum) {
      auto [entry, inserted] = anonymousEnumTypespecs.try_emplace(
          *typespec.getSourceTypeIdentity(), typespec);
      if (!inserted)
        return typespec.emitOpError(
            "duplicates an anonymous enum source type identity");
    }
  }
  for (const auto &entry : interfaceTypespecs) {
    llvm::SmallDenseSet<Attribute, 4> modports;
    bool hasBase = false;
    for (SimVPITypespecDeclOp typespec : entry.second) {
      StringAttr modport = typespec.getTargetType().getModport();
      if (!modports.insert(modport).second)
        return typespec.emitOpError(
            "duplicates an interface/modport typespec specialization");
      hasBase |= modport.getValue().empty();
    }
    if (!hasBase && llvm::any_of(entry.second, [](SimVPITypespecDeclOp op) {
          return !op.getTargetType().getModport().getValue().empty();
        }))
      return SimVPITypespecDeclOp(entry.second.front())
          .emitOpError(
              "modport typespec requires its parent interface typespec");
  }

  llvm::DenseMap<Attribute, SimVPIObjectAnchorOp> classVPIIdentities;
  for (SimVPIObjectAnchorOp anchor : vpiAnchors) {
    VPIObjectBackingAttr backing = anchor.getBackingAttr();
    if (backing && backing.getKind() == VPIObjectBackingKind::Class &&
        backing.getSymbol())
      classVPIIdentities[backing.getSymbol()] = anchor;
  }

  llvm::DenseMap<Attribute, SimVPIClassExtendsDeclOp> classExtendsByOwner;
  llvm::DenseSet<Attribute> usedLexicalTypes;
  for (auto clause : lexicalExtends) {
    if (!classExtendsByOwner.try_emplace(clause.getOwnerAttr(), clause).second)
      return clause.emitOpError("duplicates a class extends occurrence");
    if (auto type = clause.getBaseTypespecAttr())
      if (!usedLexicalTypes.insert(type).second)
        return clause.emitOpError("duplicates a lexical class type occurrence");
  }
  for (auto type : lexicalClassTypes) {
    if (!usedLexicalTypes.contains(
            FlatSymbolRefAttr::get(type.getSymNameAttr())))
      return type.emitOpError(
          "lexical class type requires its exact extends occurrence");
    llvm::DenseSet<Attribute> ancestors;
    Attribute owner = type.getOwnerAttr();
    while (auto clause = classExtendsByOwner.lookup(owner)) {
      if (!ancestors.insert(owner).second)
        return type.emitOpError("source class inheritance contains a cycle");
      if (!clause.getBaseTypespecAttr())
        break;
      auto base =
          symbolTable.lookupNearestSymbolFrom<SimVPILexicalClassTypespecDeclOp>(
              clause, clause.getBaseTypespecAttr());
      if (!base)
        return clause.emitOpError("references an unknown lexical class type");
      owner = base.getDefinitionAttr();
    }
    SmallVector<SimVPIObjectAnchorOp> formals;
    if (!type.getUnavailableParametersAttr())
      for (auto anchor : vpiAnchors) {
        using Kind = reflection::VPIObjectKind;
        if (anchor.getParentAttr() != type.getDefinitionAttr() ||
            (anchor.getVpiKind() != static_cast<uint32_t>(Kind::Parameter) &&
             anchor.getVpiKind() != static_cast<uint32_t>(Kind::TypeParameter)))
          continue;
        bool local = false;
        if (auto properties = reflectionProperties(anchor))
          for (Attribute attribute : properties.getProperties()) {
            auto property = cast<VPIPropertyAttr>(attribute);
            if (property.getSelector().getValue().getZExtValue() == 70)
              local = cast<BoolAttr>(property.getValue()).getValue();
          }
        if (!local)
          formals.push_back(anchor);
      }
    llvm::sort(formals, [](auto left, auto right) {
      return left.getOwnerOrdinal() < right.getOwnerOrdinal();
    });
    SmallVector<SimVPIClassParameterAssignDeclOp> assignments;
    for (auto assignment : lexicalClassAssignments)
      if (assignment.getOwnerAttr() ==
          FlatSymbolRefAttr::get(type.getSymNameAttr()))
        assignments.push_back(assignment);
    llvm::sort(assignments, [](auto left, auto right) {
      return left.getOrdinal() < right.getOrdinal();
    });
    if (assignments.size() != formals.size())
      return type.emitOpError("class actual assignments must cover every "
                              "non-local base parameter");
    for (auto [ordinal, assignment] : llvm::enumerate(assignments))
      if (assignment.getOrdinal() != ordinal ||
          assignment.getParameterAttr() !=
              FlatSymbolRefAttr::get(formals[ordinal].getSymNameAttr()))
        return assignment.emitOpError("class assignments must preserve dense "
                                      "parameter declaration order");
  }

  llvm::DenseSet<std::pair<Attribute, Attribute>> typedefNames;
  for (SimVPITypespecDeclOp typespec : typespecs)
    if (typespec.getOrigin() == VPITypespecOrigin::Typedef)
      if (!typedefNames
               .insert({typespec.getOwnerAttr()
                            ? Attribute(typespec.getOwnerAttr())
                            : Attribute(typespec.getStatementScopeIdAttr()),
                        typespec.getDebugNameAttr()})
               .second)
        return typespec.emitOpError(
            "duplicates a typedef name in its lexical owner");
  for (SimVPIUnresolvedTypeAliasDeclOp alias : unresolvedAliases)
    if (!typedefNames.insert({alias.getOwnerAttr(), alias.getDebugNameAttr()})
             .second)
      return alias.emitOpError(
          "duplicates a typedef name in its lexical owner");

  llvm::SmallDenseSet<Attribute, 8> classSpecializations;
  llvm::SmallDenseSet<Attribute, 8> builtinClassTypes;
  for (SimVPITypespecDeclOp typespec : typespecs) {
    if (typespec.getOrigin() == VPITypespecOrigin::BuiltinClass &&
        !builtinClassTypes.insert(typespec.getTargetType()).second)
      return typespec.emitOpError("duplicates a canonical builtin class type");
    if (typespec.getOrigin() != VPITypespecOrigin::ClassSpecialization)
      continue;
    Attribute symbol = typespec.getTargetType().getSymbol();
    if (!classSpecializations.insert(symbol).second)
      return typespec.emitOpError(
          "duplicates a canonical class specialization");
    auto owner = symbolTable.lookupNearestSymbolFrom<SimVPIObjectAnchorOp>(
        typespec, typespec.getOwnerAttr());
    auto [existing, inserted] = classVPIIdentities.try_emplace(symbol, owner);
    if (!inserted && existing->second != owner)
      return typespec.emitOpError(
          "class specialization owner disagrees with the class-backed anchor");
  }

  std::function<LogicalResult(Operation *, VPITypeSemanticsAttr)>
      verifyTypeReferences =
          [&](Operation *owner,
              VPITypeSemanticsAttr semantic) -> LogicalResult {
    if (ArrayAttr aliases = semantic.getTypedefAliases()) {
      for (Attribute alias : aliases) {
        auto reference = cast<SymbolRefAttr>(alias);
        SimVPITypespecDeclOp target =
            symbolTable.lookupNearestSymbolFrom<SimVPITypespecDeclOp>(
                owner, reference);
        if (!target || target.getOrigin() != VPITypespecOrigin::Typedef)
          return owner->emitError()
                 << "VPI typedef alias " << reference
                 << " does not reference a typedef typespec declaration";
      }
    }
    if (semantic.getKind() == VPITypeKind::VirtualInterface &&
        !semantic.getSymbol())
      return owner->emitError(
          "VPI virtual-interface semantics require an exact typespec identity");
    if (semantic.getKind() == VPITypeKind::VirtualInterface) {
      SimVPITypespecDeclOp target =
          symbolTable.lookupNearestSymbolFrom<SimVPITypespecDeclOp>(
              owner, semantic.getSymbol());
      if (!target)
        return owner->emitError()
               << "VPI virtual-interface identity " << semantic.getSymbol()
               << " does not reference a typespec declaration";
      VPITypeSemanticsAttr targetType = target.getTargetType();
      if (target.getOrigin() != VPITypespecOrigin::Interface ||
          targetType.getKind() != VPITypeKind::VirtualInterface ||
          targetType.getName() != semantic.getName() ||
          targetType.getModport() != semantic.getModport())
        return owner->emitError()
               << "VPI virtual-interface identity " << semantic.getSymbol()
               << " references a different interface specialization";
    }
    if (semantic.getKind() == VPITypeKind::Class &&
        (!semantic.getSymbol() ||
         !classVPIIdentities.contains(FlatSymbolRefAttr::get(
             getContext(),
             semantic.getSymbol().getRootReference().getValue()))))
      return owner->emitError()
             << "VPI class semantics require a class-definition identity "
                "anchor for "
             << semantic.getSymbol();
    for (Attribute child : semantic.getChildren())
      if (failed(
              verifyTypeReferences(owner, cast<VPITypeSemanticsAttr>(child))))
        return failure();
    return success();
  };
  for (SimVPITypespecDeclOp typespec : typespecs)
    if (failed(verifyTypeReferences(typespec, typespec.getTargetType())))
      return failure();
  for (SimVPINettypeDeclOp nettype : nettypes)
    if (failed(verifyTypeReferences(nettype, nettype.getTargetType())))
      return failure();

  DenseSet<std::pair<Attribute, Attribute>> classMemberIdentities;
  DenseSet<std::pair<Attribute, Attribute>> classMethodIdentities;
  for (SimVPIObjectAnchorOp anchor : vpiAnchors) {
    if (!anchor.getClassMethodAttr())
      continue;
    if (!classMethodIdentities
             .insert({anchor.getParentAttr(), anchor.getClassMethodAttr()})
             .second)
      return anchor.emitOpError("duplicates a class method in the same owner");
    auto method = symbolTable.lookupNearestSymbolFrom<SimClassMethodDeclOp>(
        anchor, anchor.getClassMethodAttr());
    Operation *owner = anchor.getParentAttr()
                           ? symbolTable.lookupNearestSymbolFrom(
                                 anchor, anchor.getParentAttr())
                           : nullptr;
    if (!method || !classSpecializations.contains(method.getOwnerAttr()))
      return anchor.emitOpError("class method requires a canonical class specialization");
    if (auto lexical = dyn_cast_or_null<SimVPIObjectAnchorOp>(owner);
        lexical && classVPIIdentities.lookup(method.getOwnerAttr()) != lexical)
      return anchor.emitOpError("lexical owner does not define the method's class");
  }
  llvm::DenseSet<std::pair<Attribute, Attribute>> classPropertyNames;
  for (SimVPIClassMemberDeclOp member : classMembers) {
    if (!classPropertyNames
             .insert({member.getOwnerAttr(), member.getDebugNameAttr()})
             .second)
      return member.emitOpError(
          "duplicates a property name in the same class owner");
    if (!scopes.contains(member.getScopeId()))
      return member.emitOpError("references an unknown enclosing scope");
    if (failed(verifyTypeReferences(member, member.getVpiType())))
      return failure();
    if (!member.getFieldAttr())
      continue;
    auto field = symbolTable.lookupNearestSymbolFrom<SimClassFieldDeclOp>(
        member, member.getFieldAttr());
    Operation *owner =
        symbolTable.lookupNearestSymbolFrom(member, member.getOwnerAttr());
    if (!classMemberIdentities
             .insert({member.getOwnerAttr(), member.getFieldAttr()})
             .second)
      return member.emitOpError("duplicates a class field in the same owner");
    if (!scopes.contains(member.getScopeId()))
      return member.emitOpError("references an unknown enclosing scope");
    if (!classSpecializations.contains(field.getOwnerAttr()))
      return member.emitOpError("class field requires a canonical class specialization");
    if (auto anchor = dyn_cast<SimVPIObjectAnchorOp>(owner);
        anchor && classVPIIdentities.lookup(field.getOwnerAttr()) != anchor)
      return member.emitOpError("lexical owner does not define the field's class");
    if (auto storageID = member.getStaticStorage()) {
      auto storage = storagesById.lookup(*storageID);
      if (!storage || storage.getType() != field.getType())
        return member.emitOpError("static storage must exist and match the class field type");
    }
    if (failed(verifyTypeReferences(member, member.getVpiType())))
      return failure();
  }

  // Validate the whole direct-alias forest once. Memoizing completed paths
  // keeps a long legal alias chain linear rather than retraversing every
  // suffix from every declaration.
  llvm::DenseMap<Operation *, uint8_t> nettypeAliasState;
  std::function<LogicalResult(SimVPINettypeDeclOp)> verifyNettypeAlias =
      [&](SimVPINettypeDeclOp nettype) -> LogicalResult {
    uint8_t &state = nettypeAliasState[nettype];
    if (state == 2)
      return success();
    if (state == 1)
      return nettype.emitOpError("direct nettype aliases contain a cycle");
    state = 1;
    if (FlatSymbolRefAttr alias = nettype.getDirectAliasAttr()) {
      SimVPINettypeDeclOp target =
          symbolTable.lookupNearestSymbolFrom<SimVPINettypeDeclOp>(nettype,
                                                                   alias);
      if (target && failed(verifyNettypeAlias(target)))
        return failure();
    }
    state = 2;
    return success();
  };
  for (SimVPINettypeDeclOp nettype : nettypes)
    if (failed(verifyNettypeAlias(nettype)))
      return failure();
  for (Operation &op : getBody().front()) {
    VPITypeSemanticsAttr semantic;
    if (auto descriptor = dyn_cast<SimVPIBoxTypeDeclOp>(op)) {
      if (!boxTypeIDs.insert(descriptor.getId()).second)
        return descriptor.emitOpError("duplicates a box type ID");
      semantic = descriptor.getVpiType();
    } else if (auto storage = dyn_cast<SimStorageDeclOp>(op))
      semantic = storage.getVpiTypeAttr();
    else if (auto net = dyn_cast<SimNetDeclOp>(op))
      semantic = net.getVpiTypeAttr();
    else if (auto identity = dyn_cast<SimVPINetIdentityDeclOp>(op))
      semantic = identity.getVpiTypeAttr();
    else if (auto port = dyn_cast<SimPortDeclOp>(op))
      semantic = port.getVpiTypeAttr();
    else if (auto specialization =
                 dyn_cast<SimVPIDefinitionMemberSpecializationOp>(op))
      semantic = specialization.getEffectiveTypeAttr();
    else if (auto member = dyn_cast<SimVPIClassMemberDeclOp>(op))
      semantic = member.getVpiType();
    else if (auto local = dyn_cast<SimVPILocalDeclOp>(op))
      semantic = local.getVpiType();
    else if (auto anchor = dyn_cast<SimVPIObjectAnchorOp>(op))
      semantic = anchor.getVpiTypeAttr();
    if (!semantic)
      continue;
    if (failed(verifyTypeReferences(&op, semantic)))
      return failure();
    if (semantic.getKind() == VPITypeKind::Enum &&
        !semantic.getTypedefAliases()) {
      IntegerAttr identity = get<metadata::VpiSourceTypeIdentity>(&op);
      if (!identity)
        return op.emitError(
            "anonymous enum VPI value requires an exact source type "
            "identity");
      if (!anonymousEnumTypespecs.contains(identity.getValue().getZExtValue()))
        return op.emitError(
            "anonymous enum VPI value has no matching typespec identity");
    }
  }
  llvm::DenseMap<Attribute, llvm::DenseSet<uint64_t>> enumOrdinals;
  for (SimVPIEnumConstDeclOp enumConstant : enumConstants) {
    Attribute key = enumConstant.getEnumTypespecAttr();
    auto &ordinals = enumOrdinals[key];
    if (!ordinals.insert(enumConstant.getOrdinal()).second)
      return enumConstant.emitOpError(
          "duplicates an ordinal in the same enum typespec");
  }
  for (const auto &entry : enumOrdinals)
    for (uint64_t ordinal = 0; ordinal != entry.second.size(); ++ordinal)
      if (!entry.second.contains(ordinal))
        return emitOpError(
            "VPI enum-constant ordinals must be dense declaration order");

  llvm::DenseSet<uint64_t> threadOriginCodeUnits;
  const auto *originEdge = reflection::findVPITraversal(
      static_cast<uint32_t>(reflection::VPIObjectKind::Thread),
      static_cast<uint32_t>(reflection::VPIRelationKind::OriginRel),
      reflection::VPITraversalMode::Handle);
  llvm::DenseMap<std::pair<uint64_t, uint64_t>, SmallVector<uint32_t>>
      localOrdinals, sourceLocalOrdinals;
  llvm::DenseSet<std::pair<Operation *, Attribute>> localNames;
  for (SimVPILocalDeclOp local : vpiLocals) {
    auto owner = codeUnits.lookup(local.getCodeUnitId());
    if (!owner || owner.getScopeId() != local.getScopeId())
      return local.emitOpError(
          "owner must name a code unit in its physical scope");
    Operation *scope = owner;
    if (auto statementID = local.getStatementScopeId()) {
      auto statement = statements.lookup(*statementID);
      if (!statement || !statement.getIsScope() ||
          statement.getCodeUnitId() != local.getCodeUnitId())
        return local.emitOpError(
            "statement scope must belong to the exact lexical code unit");
      scope = statement;
    } else if (owner.getCodeUnitKind() != EntryKind::Task &&
               owner.getCodeUnitKind() != EntryKind::Function) {
      return local.emitOpError(
          "local without a statement scope requires a task or function owner");
    }
    if (!localNames.insert({scope, local.getDebugNameAttr()}).second)
      return local.emitOpError(
          "duplicates a declaration name in the same procedural scope");
    if (failed(verifyTypeReferences(local, local.getVpiType())))
      return failure();
    localOrdinals[{local.getCodeUnitId(),
                   local.getStatementScopeId().value_or(0)}]
        .push_back(local.getOrdinal());
    if (auto ordinal = local.getSourceOrdinalAttr();
        ordinal && ordinal.getValue().isNegative())
      return local.emitOpError("source ordinal must be nonnegative");
    sourceLocalOrdinals[{local.getCodeUnitId(),
                         local.getStatementScopeId().value_or(0)}]
        .push_back(local.getSourceOrdinal().value_or(local.getOrdinal()));
  }
  llvm::DenseSet<uint64_t> foreachStatements;
  llvm::DenseSet<Operation *> foreachIndices;
  for (auto declaration : foreachDeclarations) {
    auto statement = statements.lookup(declaration.getStatementId());
    if (!statement ||
        statement.getVpiKind() !=
            static_cast<uint32_t>(reflection::VPIObjectKind::ForeachStmt) ||
        !statement.getIsScope() || !statement.getCodeUnitId() ||
        !foreachStatements.insert(declaration.getStatementId()).second)
      return declaration.emitOpError(
          "requires one exact retained foreach statement");
    VPITypeSemanticsAttr semantic;
    if (declaration.getCollection().getKind() == VPIObjectRefKind::Storage) {
      auto storage = storagesById.lookup(
          declaration.getCollection().getId().getValue().getZExtValue());
      if (storage)
        semantic = storage.getVpiTypeAttr();
    } else if (auto local =
                   symbolTable.lookupNearestSymbolFrom<SimVPILocalDeclOp>(
                       declaration, declaration.getCollection().getSymbol())) {
      semantic = local.getVpiType();
    }
    uint64_t dimensions = 0;
    auto element = semantic;
    while (element && (element.getKind() == VPITypeKind::UnpackedArray ||
                       element.getKind() == VPITypeKind::PackedArray)) {
      if (!element.getRange() || !element.getChildren() ||
          element.getChildren().size() != 1)
        break;
      ++dimensions;
      element = cast<VPITypeSemanticsAttr>(element.getChildren()[0]);
    }
    const uint32_t collectionKind =
        semantic ? vpiKindForClassMember(semantic) : 0;
    if (!dimensions || declaration.getLoopVariables().size() > dimensions ||
        (collectionKind !=
             static_cast<uint32_t>(reflection::VPIObjectKind::RegArray) &&
         collectionKind !=
             static_cast<uint32_t>(reflection::VPIObjectKind::PackedArrayVar) &&
         collectionKind !=
             static_cast<uint32_t>(reflection::VPIObjectKind::BitVar) &&
         collectionKind !=
             static_cast<uint32_t>(reflection::VPIObjectKind::Reg)))
      return declaration.emitOpError(
          "collection must prove all retained fixed-array dimensions");
    for (Attribute index : declaration.getLoopVariables()) {
      auto reference = dyn_cast<FlatSymbolRefAttr>(index);
      if (!reference)
        continue;
      auto local = symbolTable.lookupNearestSymbolFrom<SimVPILocalDeclOp>(
          declaration, reference);
      if (!local || local.getStatementScopeId() != statement.getId() ||
          local.getCodeUnitId() != statement.getCodeUnitId() ||
          local.getScopeId() != statement.getScopeId() ||
          local.getVpiType().getKind() != VPITypeKind::Int ||
          !foreachIndices.insert(local).second)
        return declaration.emitOpError(
            "index must be a unique int declaration of this foreach scope");
    }
  }
  for (auto local : vpiLocals)
    if (local.getStatementScopeId() &&
        foreachStatements.contains(*local.getStatementScopeId()) &&
        !foreachIndices.contains(local))
      return local.emitOpError(
          "foreach local declaration is absent from its index list");
  llvm::DenseSet<uint64_t> staticLocalStorage;
  for (auto binding : staticLocals) {
    auto storage = storagesById.lookup(binding.getStorageId());
    auto owner = codeUnits.lookup(binding.getCodeUnitId());
    if (!storage || storage.getLifetime() == Lifetime::Automatic || !owner ||
        owner.getInternal() || storage.getScopeId() != owner.getScopeId())
      return binding.emitOpError("requires static storage in the exact source "
                                 "code unit's physical scope");
    if (sim::has<metadata::VpiIdentityDelegated>(storage) ||
        llvm::any_of(classMembers, [&](auto member) {
          return member.getStaticStorage() == binding.getStorageId();
        }))
      return binding.emitOpError(
          "delegated storage retains its canonical owner");
    if (!storage.getVpiTypeAttr() ||
        !reflection::vpiObjectSetContains(
            reflection::VPIObjectSetID::DataVariableObjects,
            vpiKindForStorage(storage.getVpiTypeAttr())))
      return binding.emitOpError("requires a reflected data variable");
    Operation *scope = owner;
    if (auto id = binding.getStatementScopeId()) {
      auto statement = statements.lookup(*id);
      if (!statement || !statement.getIsScope() ||
          statement.getCodeUnitId() != binding.getCodeUnitId())
        return binding.emitOpError(
            "statement scope must belong to the exact lexical code unit");
      scope = statement;
    } else if (owner.getCodeUnitKind() != EntryKind::Task &&
               owner.getCodeUnitKind() != EntryKind::Function) {
      return binding.emitOpError("static local without a statement scope "
                                 "requires a task or function owner");
    }
    if (!staticLocalStorage.insert(binding.getStorageId()).second)
      return binding.emitOpError("duplicates a static local storage binding");
    auto name = storage.getDebugNameAttr();
    if (!name || name.getValue().empty() || name.getValue().contains('\0') ||
        !localNames.insert({scope, name}).second)
      return binding.emitOpError("requires a unique exact declaration name");
    sourceLocalOrdinals[{binding.getCodeUnitId(),
                         binding.getStatementScopeId().value_or(0)}]
        .push_back(binding.getOrdinal());
  }
  for (auto &entry : sourceLocalOrdinals) {
    llvm::sort(entry.second);
    for (auto [index, ordinal] : llvm::enumerate(entry.second))
      if (index != ordinal)
        return emitOpError(
            "procedural source declaration ordinals must be dense "
            "and unique within each scope");
  }
  for (auto &entry : localOrdinals) {
    llvm::sort(entry.second);
    for (auto [index, ordinal] : llvm::enumerate(entry.second))
      if (index != ordinal)
        return emitOpError("VPI local declaration ordinals must be dense and "
                           "unique within each scope");
  }

  if (getOperation()
          ->walk([&](Operation *operation) -> WalkResult {
            auto source = get<metadata::VpiCallSite>(operation);
            if (!source)
              return WalkResult::advance();
            auto contract = vpiClassDispatchCalls.find(source);
            if (contract == vpiClassDispatchCalls.end())
              return WalkResult::advance();
            Value receiver;
            FlatSymbolRefAttr method;
            if (auto call = dyn_cast<SimClassVirtualCallOp>(operation)) {
              receiver = call.getReceiver();
              method = call.getMethodAttr();
            } else if (auto call =
                           dyn_cast<SimClassVirtualTaskCallOp>(operation)) {
              receiver = call.getReceiver();
              method = call.getMethodAttr();
            } else {
              // Devirtualized executable calls retain the original indirect
              // source contract; their direct callee is checked independently.
              return WalkResult::advance();
            }
            if (receiver.getType() != contract->second.getReceiverType() ||
                method != contract->second.getMethodAttr()) {
              operation->emitOpError("executable dispatch disagrees with its "
                                     "VPI class call contract");
              return WalkResult::interrupt();
            }
            return WalkResult::advance();
          })
          .wasInterrupted())
    return failure();
  llvm::DenseSet<uint64_t> boundCodeUnitScopes;
  llvm::DenseSet<Operation *> boundSourceScopes;
  for (SimVPICodeUnitScopeBindingOp binding : codeUnitScopes) {
    auto unit = codeUnits.lookup(binding.getCodeUnitId());
    auto source = symbolTable.lookupNearestSymbolFrom<SimVPIObjectAnchorOp>(
        binding, binding.getSourceAttr());
    if (!unit || isVPIVisibleCodeUnit(unit) || !vpiCallableSourceKind(unit))
      return binding.emitOpError(
          "requires an internal task/function code unit");
    if (!source)
      return binding.emitOpError("source must name a callable anchor");
    uint32_t expected =
        static_cast<uint32_t>(vpiCallableSourceKind(unit) == EntryKind::Task
                                  ? reflection::VPIObjectKind::Task
                                  : reflection::VPIObjectKind::Function);
    if (source.getVpiKind() != expected ||
        source.getEnclosingScopeId() != unit.getScopeId() ||
        source.getHierarchicalName() != unit.getHierarchicalName())
      return binding.emitOpError("source callable kind, physical scope, and "
                                 "hierarchy must match its code unit");
    if (!boundCodeUnitScopes.insert(binding.getCodeUnitId()).second ||
        !boundSourceScopes.insert(source).second)
      return binding.emitOpError(
          "duplicates a code-unit or source scope binding");
  }

  llvm::DenseSet<uint64_t> initializerUnits, initializedStorages;
  for (SimVPIInitializerBindingOp binding : initializerBindings) {
    auto unit = codeUnits.lookup(binding.getCodeUnitId());
    auto storage = storagesById.lookup(binding.getStorageId());
    if (!unit || !unit.getInternal() ||
        unit.getCodeUnitKind() != EntryKind::Function ||
        boundCodeUnitScopes.contains(binding.getCodeUnitId()))
      return binding.emitOpError("initializer requires an internal function "
                                 "without a callable source scope");
    if (!storage || storage.getLifetime() == Lifetime::Automatic ||
        storage.getScopeId() != unit.getScopeId())
      return binding.emitOpError(
          "initializer requires static storage in its exact physical scope");
    if (!initializerUnits.insert(binding.getCodeUnitId()).second ||
        !initializedStorages.insert(binding.getStorageId()).second)
      return binding.emitOpError(
          "duplicates an initializer or source storage binding");
  }

  llvm::DenseSet<uint64_t> callStatementAliases;
  for (SimVPICallDeclOp call : vpiCalls) {
    auto owner = codeUnits.lookup(call.getCodeUnitId());
    if (!owner || owner.getScopeId() != call.getScopeId())
      return call.emitOpError(
          "owner must name a code unit in its physical scope");
    if (initializerUnits.contains(call.getCodeUnitId()) &&
        (call.getStatementId() || call.getEnclosingStatementId()))
      return call.emitOpError("a nonprocedural initializer call cannot name a "
                              "procedural statement");
    auto checkStatement = [&](uint64_t id) -> SimStatementDeclOp {
      auto statement = statements.lookup(id);
      return statement && statement.getCodeUnitId() &&
                     *statement.getCodeUnitId() == call.getCodeUnitId() &&
                     statement.getScopeId() == call.getScopeId()
                 ? statement
                 : SimStatementDeclOp{};
    };
    if (auto id = call.getStatementId()) {
      auto statement = checkStatement(*id);
      if (!statement || statement.getVpiKind() != call.getVpiKind())
        return call.emitOpError(
            "statement alias must name the same call kind and owner");
      if (!callStatementAliases.insert(*id).second)
        return call.emitOpError("duplicates a source call statement identity");
    }
    if (auto id = call.getEnclosingStatementId(); id && !checkStatement(*id))
      return call.emitOpError(
          "enclosing statement must have the same lexical owner");
    if (auto id = call.getCalleeCodeUnitId()) {
      auto callee = codeUnits.lookup(*id);
      using Kind = reflection::VPIObjectKind;
      bool task =
          call.getVpiKind() == static_cast<uint32_t>(Kind::TaskCall) ||
          call.getVpiKind() == static_cast<uint32_t>(Kind::MethodTaskCall);
      if (vpiCallableSourceKind(callee) !=
          (task ? EntryKind::Task : EntryKind::Function))
        return call.emitOpError(
            "callee must name the corresponding task/function declaration");
    }
  }

  for (SimVPIThreadOriginBindingOp binding : threadOrigins) {
    auto codeUnit = codeUnits.find(binding.getCodeUnitId());
    auto statement = statements.find(binding.getStatementId());
    if (codeUnit == codeUnits.end())
      return binding.emitOpError("references an unknown code-unit ID");
    if (statement == statements.end() || !statement->second.getCodeUnitId())
      return binding.emitOpError(
          "origin must name an existing behavioral statement");
    if (!threadOriginCodeUnits.insert(binding.getCodeUnitId()).second)
      return binding.emitOpError(
          "duplicates a thread-origin code-unit binding");
    if (statement->second.getScopeId() != codeUnit->second.getScopeId())
      return binding.emitOpError(
          "origin and code unit must have the same scope");
    if (*statement->second.getCodeUnitId() != binding.getCodeUnitId() &&
        isVPIVisibleCodeUnit(codeUnit->second))
      return binding.emitOpError(
          "only an internal code unit may reference another owner's statement");
    if (!originEdge || !reflection::vpiObjectSetContains(
                           originEdge->targets, statement->second.getVpiKind()))
      return binding.emitOpError("statement is not a legal thread origin");
  }

  llvm::DenseMap<uint64_t, uint8_t> statementSiteMasks;
  for (SimStatementSiteDeclOp site : statementSites) {
    auto statement = statements.find(site.getStatementId());
    if (statement == statements.end())
      return site.emitOpError("references an unknown statement ID");
    if (site.getPhase() > UINT16_MAX)
      return site.emitOpError("callback phase exceeds the reflection encoding");
    auto phase = static_cast<reflection::VPIStatementCallbackPhase>(
        static_cast<uint16_t>(site.getPhase()));
    if (!reflection::isVPIStatementCallbackPhase(statement->second.getVpiKind(),
                                                 phase))
      return site.emitOpError(
          "phase is not legal for the statement's Table 38-6 policy");
    uint8_t bit = uint8_t{1} << static_cast<unsigned>(phase);
    uint8_t &mask = statementSiteMasks[site.getStatementId()];
    if (mask & bit)
      return site.emitOpError(
          "duplicates a semantic callback phase for the statement");
    mask |= bit;
  }
  for (SimStatementDeclOp statement : statementInventory) {
    uint64_t id = statement.getId();
    const auto *callback =
        reflection::findVPIStatementCallback(statement.getVpiKind());
    uint8_t expectedMask = callback ? callback->phaseMask : 0;
    if (statementSiteMasks.lookup(id) != expectedMask)
      return statement.emitOpError(
          "does not declare exactly the callback sites required by its "
          "Table 38-6 policy");
  }
  llvm::DenseMap<uint64_t, uint8_t> parentStates;
  for (SimStatementDeclOp root : statementInventory) {
    if (parentStates.lookup(root.getId()) == 2)
      continue;
    SmallVector<std::pair<SimStatementDeclOp, bool>> worklist{{root, false}};
    while (!worklist.empty()) {
      auto [statement, finish] = worklist.pop_back_val();
      uint8_t &state = parentStates[statement.getId()];
      if (finish) {
        state = 2;
        continue;
      }
      if (state == 2)
        continue;
      if (state == 1)
        return statement.emitOpError("parent statements contain a cycle");
      state = 1;
      worklist.push_back({statement, true});
      if (auto parentID = statement.getParentId()) {
        auto parent = statements.find(*parentID);
        if (parent == statements.end() ||
            parent->second.getCodeUnitId() != statement.getCodeUnitId() ||
            parent->second.getScopeId() != statement.getScopeId())
          return statement.emitOpError(
              "references an unknown or cross-owner/scope parent statement");
        worklist.push_back({parent->second, false});
      }
    }
  }

  // Statement relations are an optional, all-or-nothing inventory while the
  // lowering that produces them is introduced. Once present, every statement
  // has exactly one semantic incoming containment edge. Handle and iterate
  // access to the same child are represented by one merged mode mask.
  if (!statementRelations.empty()) {
    llvm::DenseMap<uint64_t, SimVPIStatementRelationDeclOp> incomingRelations;
    llvm::DenseMap<uint64_t, uint32_t> scopeSourceKinds;
    struct ModeRelation {
      uint32_t sourceTag;
      uint64_t sourceId;
      uint32_t sourceVPIKind;
      uint32_t selector;
      uint32_t mode;
      uint64_t ordinal;
      uint64_t targetId;
      SimVPIStatementRelationDeclOp relation;
    };
    SmallVector<ModeRelation> modeRelations;
    incomingRelations.reserve(statementRelations.size());
    scopeSourceKinds.reserve(
        std::min<size_t>(scopeIds.size(), statementRelations.size()));
    modeRelations.reserve(statementRelations.size() * 2);
    for (SimVPIStatementRelationDeclOp relation : statementRelations) {
      auto target = statements.find(relation.getTargetStatementId());
      if (target == statements.end())
        return relation.emitOpError(
            "references an unknown target statement ID");
      if (!incomingRelations
               .try_emplace(relation.getTargetStatementId(), relation)
               .second)
        return relation.emitOpError(
            "duplicates the target statement's semantic containment edge; "
            "merge access modes into one record");

      const auto *sourceKind =
          reflection::findVPIObjectKind(relation.getSourceVpiKind());
      switch (relation.getSourceKind()) {
      case VPIStatementSourceKind::Scope: {
        auto source = scopes.find(relation.getSourceId());
        if (source == scopes.end())
          return relation.emitOpError("references an unknown source scope ID");
        if ((sourceKind->families &
             reflection::vpiFamilyMask(reflection::VPIObjectFamily::Scope)) ==
            0)
          return relation.emitOpError(
              "scope source VPI kind is not a scope object");
        if (source->second.getInterfaceType() &&
            sourceKind->value !=
                static_cast<uint32_t>(reflection::VPIObjectKind::Interface))
          return relation.emitOpError(
              "interface scope metadata and source VPI kind disagree");
        if (effectiveScopeVPIKind(source->second) !=
            relation.getSourceVpiKind())
          return relation.emitOpError(
              "source VPI kind does not match the scope declaration");
        if (auto [iterator, inserted] = scopeSourceKinds.try_emplace(
                relation.getSourceId(), relation.getSourceVpiKind());
            !inserted && iterator->second != relation.getSourceVpiKind())
          return relation.emitOpError(
              "source scope has inconsistent exact VPI kinds");
        if (target->second.getCodeUnitId() || target->second.getParentId() ||
            target->second.getScopeId() != relation.getSourceId())
          return relation.emitOpError(
              "scope source does not own the root scope-owned statement");
        break;
      }
      case VPIStatementSourceKind::CodeUnit: {
        auto source = codeUnits.find(relation.getSourceId());
        if (source == codeUnits.end())
          return relation.emitOpError(
              "references an unknown source code-unit ID");
        StringRef expectedKind;
        switch (source->second.getCodeUnitKind()) {
        case EntryKind::Initial:
          expectedKind = "vpiInitial";
          break;
        case EntryKind::Final:
          expectedKind = "vpiFinal";
          break;
        case EntryKind::Always:
        case EntryKind::AlwaysComb:
        case EntryKind::AlwaysFF:
        case EntryKind::AlwaysLatch:
          expectedKind = "vpiAlways";
          break;
        case EntryKind::Function:
          expectedKind = "vpiFunction";
          break;
        case EntryKind::Task:
          expectedKind = "vpiTask";
          break;
        default:
          return relation.emitOpError(
              "source code-unit kind cannot own VPI statements");
        }
        if (expectedKind != sourceKind->apiName)
          return relation.emitOpError(
              "source VPI kind does not match the code-unit kind");
        if (target->second.getCodeUnitId() != relation.getSourceId() ||
            target->second.getParentId() ||
            target->second.getScopeId() != source->second.getScopeId())
          return relation.emitOpError(
              "code-unit source does not own the root behavioral statement");
        break;
      }
      case VPIStatementSourceKind::Statement: {
        auto source = statements.find(relation.getSourceId());
        if (source == statements.end())
          return relation.emitOpError(
              "references an unknown source statement ID");
        if (source->second.getVpiKind() != relation.getSourceVpiKind())
          return relation.emitOpError(
              "source VPI kind does not match the statement declaration");
        if (target->second.getParentId() != relation.getSourceId() ||
            target->second.getCodeUnitId() != source->second.getCodeUnitId() ||
            target->second.getScopeId() != source->second.getScopeId())
          return relation.emitOpError(
              "statement source does not match the target's structural parent");
        break;
      }
      case VPIStatementSourceKind::Anchor: {
        auto source = vpiAnchorsById.find(relation.getSourceId());
        if (source == vpiAnchorsById.end())
          return relation.emitOpError(
              "references an unknown source VPI anchor inventory ID");
        if (source->second.getVpiKind() != relation.getSourceVpiKind())
          return relation.emitOpError(
              "source VPI kind does not match the anchor declaration");
        if (source->second.getBackingAttr())
          return relation.emitOpError(
              "backed anchor source must use its canonical scope or "
              "code-unit ID");
        if ((sourceKind->families &
             reflection::vpiFamilyMask(reflection::VPIObjectFamily::Scope)) ==
            0)
          return relation.emitOpError(
              "anchor source VPI kind is not a scope object");
        if (target->second.getCodeUnitId() || target->second.getParentId() ||
            target->second.getScopeId() != source->second.getEnclosingScopeId())
          return relation.emitOpError(
              "anchor source does not own the root scope-owned statement");
        break;
      }
      }

      for (uint32_t modeValue = 0; modeValue != 2; ++modeValue) {
        uint32_t modeBit = uint32_t{1} << modeValue;
        if ((relation.getModeMask() & modeBit) == 0)
          continue;
        auto mode = static_cast<reflection::VPITraversalMode>(modeValue);
        const auto *edge = reflection::findVPITraversal(
            relation.getSourceVpiKind(), relation.getSelector(), mode);
        if (!edge || !edge->statementContainment ||
            !reflection::vpiObjectSetContains(edge->targets,
                                              target->second.getVpiKind()))
          return relation.emitOpError(
              "is not a legal statement-containment traversal in the "
              "generated VPI model");
        modeRelations.push_back(
            {static_cast<uint32_t>(relation.getSourceKind()),
             relation.getSourceId(), relation.getSourceVpiKind(),
             relation.getSelector(), modeValue, relation.getOrdinal(),
             relation.getTargetStatementId(), relation});
      }
    }

    llvm::sort(modeRelations, [](const ModeRelation &left,
                                 const ModeRelation &right) {
      return std::tie(left.sourceTag, left.sourceId, left.sourceVPIKind,
                      left.selector, left.mode, left.ordinal, left.targetId) <
             std::tie(right.sourceTag, right.sourceId, right.sourceVPIKind,
                      right.selector, right.mode, right.ordinal,
                      right.targetId);
    });
    auto sameModeGroup = [](const ModeRelation &left,
                            const ModeRelation &right) {
      return std::tie(left.sourceTag, left.sourceId, left.sourceVPIKind,
                      left.selector, left.mode) ==
             std::tie(right.sourceTag, right.sourceId, right.sourceVPIKind,
                      right.selector, right.mode);
    };
    for (size_t begin = 0; begin != modeRelations.size();) {
      size_t end = begin + 1;
      while (end != modeRelations.size() &&
             sameModeGroup(modeRelations[begin], modeRelations[end]))
        ++end;
      uint64_t expectedOrdinal = 0;
      for (size_t index = begin; index != end; ++index) {
        const ModeRelation &entry = modeRelations[index];
        if (index != begin &&
            entry.ordinal == modeRelations[index - 1].ordinal) {
          SimVPIStatementRelationDeclOp relation = entry.relation;
          return relation.emitOpError(
              "overlaps another target at the same source, selector, mode, "
              "and ordinal");
        }
        if (entry.ordinal != expectedOrdinal) {
          SimVPIStatementRelationDeclOp relation = entry.relation;
          return relation.emitOpError(
              "ordinals must be dense from zero for each source, selector, "
              "and access mode");
        }
        ++expectedOrdinal;
      }
      if (modeRelations[begin].mode ==
              static_cast<uint32_t>(reflection::VPITraversalMode::Handle) &&
          end - begin > 1)
        return modeRelations[end - 1].relation.emitOpError(
            "vpi_handle relation may expose at most one target");
      begin = end;
    }

    // If a generated containment relation provides both singular and
    // iterative access (currently for-init and for-inc), the singular child is
    // exactly iterative ordinal zero. Later iterative children remain
    // iterate-only.
    auto sameSelectorGroup = [](const ModeRelation &left,
                                const ModeRelation &right) {
      return std::tie(left.sourceTag, left.sourceId, left.sourceVPIKind,
                      left.selector) ==
             std::tie(right.sourceTag, right.sourceId, right.sourceVPIKind,
                      right.selector);
    };
    for (size_t begin = 0; begin != modeRelations.size();) {
      size_t end = begin + 1;
      while (end != modeRelations.size() &&
             sameSelectorGroup(modeRelations[begin], modeRelations[end]))
        ++end;
      const ModeRelation &first = modeRelations[begin];
      const auto *handleEdge =
          reflection::findVPITraversal(first.sourceVPIKind, first.selector,
                                       reflection::VPITraversalMode::Handle);
      const auto *iterateEdge =
          reflection::findVPITraversal(first.sourceVPIKind, first.selector,
                                       reflection::VPITraversalMode::Iterate);
      bool dualContainment = handleEdge && handleEdge->statementContainment &&
                             iterateEdge && iterateEdge->statementContainment;
      if (dualContainment) {
        const ModeRelation *handle = nullptr;
        const ModeRelation *iterateZero = nullptr;
        for (size_t index = begin; index != end; ++index) {
          const ModeRelation &entry = modeRelations[index];
          if (entry.mode ==
              static_cast<uint32_t>(reflection::VPITraversalMode::Handle))
            handle = &entry;
          else if (entry.ordinal == 0)
            iterateZero = &entry;
        }
        if (!handle || !iterateZero ||
            handle->relation != iterateZero->relation) {
          SimVPIStatementRelationDeclOp relation =
              handle ? handle->relation : iterateZero->relation;
          return relation.emitOpError(
              "ordinal zero must merge vpi_handle and vpi_iterate for a "
              "dual-mode statement relation");
        }
      }
      begin = end;
    }
    for (SimStatementDeclOp statement : statementInventory)
      if (!incomingRelations.count(statement.getId()))
        return statement.emitOpError(
            "is missing its semantic VPI statement-containment relation");
  }

  llvm::StringMap<Operation *> declaredNetNames;
  for (auto [id, net] : nets)
    if (auto hierarchy = net.getHierarchicalName())
      declaredNetNames.try_emplace(*hierarchy, net);
  for (SimVPINetIdentityDeclOp identity : vpiNetIdentities) {
    if (!scopeIds.count(identity.getScopeId()))
      return identity.emitOpError("references an unknown scope ID");
    auto backing = nets.find(identity.getBackingNetId());
    if (backing == nets.end())
      return identity.emitOpError("references an unknown backing net ID");
    std::optional<uint64_t> backingWidth =
        getProvenanceSpan(backing->second.getType());
    std::optional<uint64_t> identityWidth =
        getProvenanceSpan(identity.getType());
    if (!backingWidth || !identityWidth || *backingWidth != *identityWidth)
      return identity.emitOpError(
          "net identity width does not match its backing net");
    auto [sameName, inserted] =
        declaredNetNames.try_emplace(identity.getHierarchicalName(), identity);
    if (!inserted)
      return identity.emitOpError()
             << "duplicates declared net hierarchy owned by "
             << sameName->second->getName();
  }

  // General relations describe immutable, non-containment graph edges. Their
  // exact endpoint kinds are derived from inventory declarations so stale or
  // hand-authored metadata cannot smuggle a mismatched kind into the image.
  struct GeneralRelation {
    uint64_t sourceIdentity;
    uint64_t targetIdentity;
    uint32_t sourceTag;
    uint64_t sourceId;
    uint32_t selector;
    uint32_t mode;
    uint64_t ordinal;
    uint32_t targetTag;
    uint64_t targetId;
    bool sharedDeclarationSlot;
    SimVPIRelationDeclOp relation;
  };
  // Resolve aliases to the same inventory declaration before grouping. Stable
  // block-order IDs avoid pointer ordering and preserve deterministic
  // diagnostics.
  DenseMap<uint64_t, uint32_t> anchoredNetKinds;
  for (SimVPIObjectAnchorOp anchor : vpiAnchors)
    if (auto backing = anchor.getBackingAttr();
        backing && backing.getKind() == VPIObjectBackingKind::Net)
      anchoredNetKinds[backing.getId().getValue().getZExtValue()] =
          anchor.getVpiKind();
  DenseMap<Operation *, uint64_t> declarationIds;
  for (auto [index, operation] : llvm::enumerate(getBody().front()))
    declarationIds[&operation] = index;
  auto canonicalDeclaration = [&](Operation *operation) -> Operation * {
    if (auto call = dyn_cast<SimVPICallDeclOp>(operation))
      if (auto statement = call.getStatementId())
        return statements.lookup(*statement);
    auto anchor = dyn_cast<SimVPIObjectAnchorOp>(operation);
    if (!anchor || !anchor.getBackingAttr())
      return operation;
    auto backing = anchor.getBackingAttr();
    if (backing.getKind() == VPIObjectBackingKind::Class)
      return operation;
    uint64_t id = backing.getId().getValue().getZExtValue();
    switch (backing.getKind()) {
    case VPIObjectBackingKind::Scope:
      return scopes.lookup(id);
    case VPIObjectBackingKind::CodeUnit:
      return codeUnits.lookup(id);
    case VPIObjectBackingKind::Storage:
      return storagesById.lookup(id);
    case VPIObjectBackingKind::Net:
      return nets.lookup(id);
    case VPIObjectBackingKind::Class:
      llvm_unreachable("class anchor handled above");
    }
    llvm_unreachable("unknown backing kind");
  };
  DenseMap<Attribute, uint64_t> contextualDeclarationIds;
  uint64_t nextContextualDeclarationId = declarationIds.size();
  SmallVector<GeneralRelation> generalRelations;
  generalRelations.reserve(vpiRelations.size());
  for (SimVPIRelationDeclOp relation : vpiRelations) {
    auto resolveKind = [&](VPIObjectRefAttr reference, StringRef endpoint,
                           uint64_t &identity) -> FailureOr<uint32_t> {
      uint64_t id =
          reference.getId() ? reference.getId().getValue().getZExtValue() : 0;
      Operation *declaration = nullptr;
      auto missing = [&]() -> FailureOr<uint32_t> {
        relation.emitOpError()
            << "references an unknown " << endpoint << " "
            << stringifyVPIObjectRefKind(reference.getKind()) << " ID " << id;
        return failure();
      };
      uint32_t kind = 0;
      switch (reference.getKind()) {
      case VPIObjectRefKind::Statement: {
        auto found = statements.find(id);
        if (found == statements.end())
          return missing();
        kind = found->second.getVpiKind();
        declaration = found->second;
        break;
      }
      case VPIObjectRefKind::Storage: {
        auto found = storagesById.find(id);
        if (found == storagesById.end())
          return missing();
        kind = vpiKindForStorage(found->second.getVpiTypeAttr());
        declaration = found->second;
        break;
      }
      case VPIObjectRefKind::Net: {
        auto found = nets.find(id);
        if (found == nets.end())
          return missing();
        auto anchored = anchoredNetKinds.find(id);
        kind = anchored != anchoredNetKinds.end()
                   ? anchored->second
                   : vpiKindForNet(found->second.getVpiTypeAttr());
        declaration = found->second;
        break;
      }
      case VPIObjectRefKind::NetIdentity: {
        auto found = vpiNetIdentitiesById.find(id);
        if (found == vpiNetIdentitiesById.end())
          return missing();
        kind = vpiKindForNet(found->second.getVpiTypeAttr());
        declaration = found->second;
        break;
      }
      case VPIObjectRefKind::Port: {
        auto found = portsById.find(id);
        if (found == portsById.end())
          return missing();
        kind = static_cast<uint32_t>(reflection::VPIObjectKind::Port);
        declaration = found->second;
        break;
      }
      case VPIObjectRefKind::DefinitionMember: {
        auto member =
            symbolTable.lookupNearestSymbolFrom<SimVPIDefinitionMemberDeclOp>(
                relation, reference.getSymbol());
        declaration = member;
        kind = member.getVpiKind();
        break;
      }
      case VPIObjectRefKind::Symbol: {
        declaration = symbolTable.lookupNearestSymbolFrom(
            relation, reference.getSymbol());
        // SymbolUserOpInterface validates the declaration category first.
        if (auto anchor = dyn_cast<SimVPIObjectAnchorOp>(declaration))
          kind = anchor.getVpiKind();
        else if (auto call = dyn_cast<SimVPICallDeclOp>(declaration))
          kind = call.getVpiKind();
        else if (auto expression =
                     dyn_cast<SimVPIExpressionDeclOp>(declaration))
          kind = expression.getVpiKind();
        else if (auto member =
                     dyn_cast<SimVPIDefinitionMemberDeclOp>(declaration))
          kind = member.getVpiKind();
        else if (auto typespec = dyn_cast<SimVPITypespecDeclOp>(declaration))
          kind = vpiKindForTypespec(typespec.getTargetType()).value_or(0);
        else if (isa<SimVPILexicalClassTypespecDeclOp>(declaration))
          kind =
              static_cast<uint32_t>(reflection::VPIObjectKind::ClassTypespec);
        else if (isa<SimVPIClassExtendsDeclOp>(declaration))
          kind = static_cast<uint32_t>(reflection::VPIObjectKind::Extends);
        else if (isa<SimVPIClassParameterAssignDeclOp>(declaration))
          kind = static_cast<uint32_t>(reflection::VPIObjectKind::ParamAssign);
        else if (isa<SimVPIUnresolvedTypeAliasDeclOp>(declaration))
          kind =
              static_cast<uint32_t>(reflection::VPIObjectKind::TypeParameter);
        else if (isa<SimVPINettypeDeclOp>(declaration))
          kind = static_cast<uint32_t>(reflection::VPIObjectKind::NettypeDecl);
        else if (auto member = dyn_cast<SimVPIClassMemberDeclOp>(declaration))
          kind = vpiKindForClassMember(member.getVpiType());
        else if (auto local = dyn_cast<SimVPILocalDeclOp>(declaration))
          kind = vpiKindForClassMember(local.getVpiType());
        break;
      }
      }
      const auto *object = reflection::findVPIObjectKind(kind);
      if (!kind || !object ||
          object->role != reflection::VPIObjectRole::Concrete) {
        relation.emitOpError()
            << endpoint << " " << stringifyVPIObjectRefKind(reference.getKind())
            << " ID " << id << " is not VPI-visible";
        return failure();
      }
      if (reference.getKind() == VPIObjectRefKind::DefinitionMember) {
        auto [entry, inserted] = contextualDeclarationIds.try_emplace(
            reference, nextContextualDeclarationId);
        if (inserted)
          ++nextContextualDeclarationId;
        identity = entry->second;
      } else
        identity = declarationIds.lookup(canonicalDeclaration(declaration));
      return kind;
    };

    uint64_t sourceIdentity = 0, targetIdentity = 0;
    FailureOr<uint32_t> sourceKind =
        resolveKind(relation.getSource(), "source", sourceIdentity);
    if (failed(sourceKind))
      return failure();
    FailureOr<uint32_t> targetKind =
        resolveKind(relation.getTarget(), "target", targetIdentity);
    if (failed(targetKind))
      return failure();
    auto mode = static_cast<reflection::VPITraversalMode>(
        static_cast<uint32_t>(relation.getMode()));
    auto symbolDeclaration = [&](VPIObjectRefAttr reference) -> Operation * {
      return reference.getKind() == VPIObjectRefKind::Symbol
                 ? symbolTable.lookupNearestSymbolFrom(relation,
                                                       reference.getSymbol())
                 : nullptr;
    };
    if (isa_and_nonnull<SimVPILexicalClassTypespecDeclOp,
                        SimVPIClassExtendsDeclOp,
                        SimVPIClassParameterAssignDeclOp>(
            symbolDeclaration(relation.getSource())))
      return relation.emitOpError("source inheritance relations are supplied "
                                  "by their typed declarations");
    if (isa_and_nonnull<SimVPILexicalClassTypespecDeclOp,
                        SimVPIClassExtendsDeclOp,
                        SimVPIClassParameterAssignDeclOp>(
            symbolDeclaration(relation.getTarget())))
      return relation.emitOpError("source inheritance objects are reached "
                                  "through their typed declarations");
    if (relation.getSource().getKind() == VPIObjectRefKind::Symbol &&
        classExtendsByOwner.contains(relation.getSource().getSymbol()) &&
        relation.getSelector() ==
            static_cast<uint32_t>(reflection::VPIRelationKind::ExtendsRel))
      return relation.emitOpError("duplicates a class extends occurrence");
    if (relation.getSelector() ==
            static_cast<uint32_t>(reflection::VPIRelationKind::ExprRel) &&
        relation.getSource().getKind() == VPIObjectRefKind::Symbol &&
        llvm::any_of(lexicalClassAssignments,
                     [&](auto assignment) {
                       return assignment.getParameterAttr() ==
                              relation.getSource().getSymbol();
                     }) &&
        !isa_and_nonnull<SimVPIObjectAnchorOp, SimVPITypespecDeclOp,
                         SimVPIUnresolvedTypeAliasDeclOp>(
            symbolDeclaration(relation.getTarget())))
      return relation.emitOpError("original base parameter default must retain "
                                  "a symbolic source expression or type");
    if (isa_and_nonnull<SimVPIUnresolvedTypeAliasDeclOp>(
            symbolDeclaration(relation.getSource())))
      return relation.emitOpError(
          "unresolved typedef relations are supplied by its declaration");
    if (isa_and_nonnull<SimVPIUnresolvedTypeAliasDeclOp>(
            symbolDeclaration(relation.getTarget())) &&
        relation.getSelector() ==
            static_cast<uint32_t>(reflection::VPIObjectKind::Parameter) &&
        mode == reflection::VPITraversalMode::Iterate)
      return relation.emitOpError(
          "unresolved typedef is not a parameter declaration");
    if (*sourceKind ==
            static_cast<uint32_t>(reflection::VPIObjectKind::ClassTypespec) &&
        relation.getSelector() ==
            static_cast<uint32_t>(reflection::VPIObjectKind::Parameter) &&
        mode == reflection::VPITraversalMode::Iterate)
      return relation.emitOpError(
          "class parameter inventory is supplied by exact declaration owners");
    const auto *edge =
        reflection::findVPITraversal(*sourceKind, relation.getSelector(), mode);
    bool declaredNetConnection =
        relation.getSource().getKind() == VPIObjectRefKind::Port &&
        relation.getTarget().getKind() == VPIObjectRefKind::NetIdentity &&
        relation.getSelector() ==
            static_cast<uint32_t>(reflection::VPIRelationKind::LowConnRel) &&
        mode == reflection::VPITraversalMode::Handle;
    if (!edge || edge->statementContainment ||
        (edge->automaticRelation != reflection::VPIAutomaticRelation::None &&
         !(declaredNetConnection &&
           edge->automaticRelation ==
               reflection::VPIAutomaticRelation::DirectPortConnection)) ||
        !reflection::vpiObjectSetContains(edge->targets, *targetKind))
      return relation.emitOpError(
          "is not a legal non-containment traversal in the generated VPI "
          "model");

    auto numericId = [](VPIObjectRefAttr reference) {
      return reference.getId() ? reference.getId().getValue().getZExtValue()
                               : 0;
    };
    uint64_t sourceId = numericId(relation.getSource());
    uint64_t targetId = numericId(relation.getTarget());
    if (relation.getSource().getKind() == VPIObjectRefKind::Port &&
        memberRelationInverseKeys.contains(
            {sourceId, static_cast<uint32_t>(relation.getSelector())}))
      return relation.emitOpError(
          "port connection is already supplied by a synthetic RefObj");
    const auto *use = reflection::findVPITraversal(
        *targetKind, static_cast<uint32_t>(reflection::VPIRelationKind::UseRel),
        reflection::VPITraversalMode::Iterate);
    bool repeatedCaseDeclaration =
        *sourceKind ==
            static_cast<uint32_t>(reflection::VPIObjectKind::CaseItem) &&
        relation.getSelector() ==
            static_cast<uint32_t>(reflection::VPIRelationKind::ExprRel) &&
        mode == reflection::VPITraversalMode::Iterate && use &&
        reflection::vpiObjectSetContains(
            use->targets,
            static_cast<uint32_t>(reflection::VPIObjectKind::Case));
    using ExpressionKind = reflection::VPIObjectKind;
    bool repeatedCallDeclaration =
        (*sourceKind == uint32_t(ExpressionKind::FuncCall) ||
         *sourceKind == uint32_t(ExpressionKind::TaskCall)) &&
        relation.getSelector() ==
            uint32_t(reflection::VPIRelationKind::ArgumentRel) &&
        mode == reflection::VPITraversalMode::Iterate && use &&
        reflection::vpiObjectSetContains(use->targets, *sourceKind);
    bool repeatedCompactDeclaration =
        isa_and_nonnull<SimVPIExpressionDeclOp>(
            symbolDeclaration(relation.getSource())) &&
        *sourceKind == uint32_t(ExpressionKind::Operation) &&
        relation.getSelector() ==
            uint32_t(reflection::VPIRelationKind::OperandRel) &&
        mode == reflection::VPITraversalMode::Iterate &&
        *targetKind != uint32_t(ExpressionKind::Constant) &&
        *targetKind != uint32_t(ExpressionKind::Operation) &&
        *targetKind != uint32_t(ExpressionKind::PartSelect) &&
        *targetKind != uint32_t(ExpressionKind::IndexedPartSelect);
    generalRelations.push_back(
        {sourceIdentity, targetIdentity,
         static_cast<uint32_t>(relation.getSource().getKind()), sourceId,
         static_cast<uint32_t>(relation.getSelector()),
         static_cast<uint32_t>(relation.getMode()), relation.getOrdinal(),
         static_cast<uint32_t>(relation.getTarget().getKind()), targetId,
         repeatedCaseDeclaration || repeatedCallDeclaration ||
             repeatedCompactDeclaration ||
             (*sourceKind ==
                  static_cast<uint32_t>(reflection::VPIObjectKind::Operation) &&
              *targetKind ==
                  static_cast<uint32_t>(reflection::VPIObjectKind::Parameter) &&
              relation.getSelector() ==
                  static_cast<uint32_t>(
                      reflection::VPIRelationKind::OperandRel) &&
              mode == reflection::VPITraversalMode::Iterate),
         relation});
  }
  llvm::sort(generalRelations,
             [](const GeneralRelation &left, const GeneralRelation &right) {
               return std::tie(left.sourceIdentity, left.selector, left.mode,
                               left.ordinal, left.targetIdentity) <
                      std::tie(right.sourceIdentity, right.selector, right.mode,
                               right.ordinal, right.targetIdentity);
             });
  auto sameGeneralGroup = [](const GeneralRelation &left,
                             const GeneralRelation &right) {
    return std::tie(left.sourceIdentity, left.selector, left.mode) ==
           std::tie(right.sourceIdentity, right.selector, right.mode);
  };
  constexpr uint32_t simNetSelector =
      static_cast<uint32_t>(reflection::VPIRelationKind::SimNetRel);
  llvm::DenseMap<uint64_t, GeneralRelation *> identitySimNets;
  for (size_t begin = 0; begin != generalRelations.size();) {
    size_t end = begin + 1;
    while (end != generalRelations.size() &&
           sameGeneralGroup(generalRelations[begin], generalRelations[end]))
      ++end;
    if (generalRelations[begin].mode ==
            static_cast<uint32_t>(VPIRelationMode::Handle) &&
        end - begin > 1)
      return generalRelations[end - 1].relation.emitOpError(
          "vpi_handle relation may expose at most one target");
    llvm::DenseSet<uint64_t> targets;
    for (size_t index = begin; index != end; ++index) {
      GeneralRelation &entry = generalRelations[index];
      // Repeated operands and case labels can name one declaration. Owned
      // Constant/Operation occurrences must still have distinct identities.
      if (!targets.insert(entry.targetIdentity).second &&
          !entry.sharedDeclarationSlot)
        return entry.relation.emitOpError(
            "duplicates a target in the same source, selector, and access "
            "mode");
      uint64_t expectedOrdinal = index - begin;
      if (entry.ordinal != expectedOrdinal)
        return entry.relation.emitOpError(
            entry.ordinal < expectedOrdinal
                ? "overlaps another target at the same source, selector, "
                  "mode, and ordinal"
                : "ordinals must be dense from zero for each source, "
                  "selector, and access mode");
      if (entry.sourceTag ==
              static_cast<uint32_t>(VPIObjectRefKind::NetIdentity) &&
          entry.selector == simNetSelector &&
          entry.mode == static_cast<uint32_t>(VPIRelationMode::Handle))
        identitySimNets[entry.sourceId] = &entry;
    }
    begin = end;
  }
  // Original expressions contain owned Constant/Operation occurrences and
  // shared terminal Parameter declarations (37.28 and 37.58). Parameter leaves
  // retain declaration identity; their own Expr relation is not a child edge.
  DenseMap<uint64_t, SmallVector<GeneralRelation *>> expressionIncoming,
      expressionOutgoing;
  for (GeneralRelation &relation : generalRelations) {
    expressionOutgoing[relation.sourceIdentity].push_back(&relation);
    expressionIncoming[relation.targetIdentity].push_back(&relation);
  }
  auto expressionRoot = [&](SimVPIObjectAnchorOp anchor) -> Operation * {
    SmallPtrSet<Operation *, 8> ancestry;
    Operation *cursor = anchor;
    while (cursor) {
      if (!ancestry.insert(cursor).second)
        return nullptr;
      if (isa<SimVPIClassParameterAssignDeclOp>(cursor))
        return cursor;
      auto declaration = dyn_cast<SimVPIObjectAnchorOp>(cursor);
      if (!declaration)
        return nullptr;
      if (declaration.getVpiKind() ==
          static_cast<uint32_t>(reflection::VPIObjectKind::Parameter))
        return declaration;
      cursor = declaration.getParentAttr()
                   ? symbolTable.lookupNearestSymbolFrom(
                         declaration, declaration.getParentAttr())
                   : nullptr;
    }
    return nullptr;
  };
  for (SimVPIObjectAnchorOp anchor : vpiAnchors) {
    using Kind = reflection::VPIObjectKind;
    Kind kind = static_cast<Kind>(anchor.getVpiKind());
    bool expression = kind == Kind::Constant || kind == Kind::Operation;
    if (!expression && kind != Kind::Parameter)
      continue;
    uint64_t identity = declarationIds.lookup(anchor);
    Operation *root = expressionRoot(anchor);
    if (!root)
      return anchor.emitOpError("source expression requires an acyclic "
                                "parameter default or class actual root");
    if (expression) {
      Operation *parent =
          symbolTable.lookupNearestSymbolFrom(anchor, anchor.getParentAttr());
      auto incoming = expressionIncoming.lookup(identity);
      if (auto assignment =
              dyn_cast_or_null<SimVPIClassParameterAssignDeclOp>(parent)) {
        if (assignment.getRhsAttr() !=
                FlatSymbolRefAttr::get(anchor.getSymNameAttr()) ||
            !incoming.empty() || anchor.getOwnerOrdinal() != 0 ||
            anchor.getEnclosingScopeId() != assignment.getScopeId())
          return anchor.emitOpError(
              "source actual requires its exact owning RHS relation");
      } else {
        auto owner = dyn_cast_or_null<SimVPIObjectAnchorOp>(parent);
        if (!owner)
          return anchor.emitOpError(
              "source expression requires an anchor or class actual owner");
        bool operand =
            owner.getVpiKind() == static_cast<uint32_t>(Kind::Operation);
        uint32_t selector = static_cast<uint32_t>(
            operand ? reflection::VPIRelationKind::OperandRel
                    : reflection::VPIRelationKind::ExprRel);
        uint32_t mode = static_cast<uint32_t>(
            operand ? VPIRelationMode::Iterate : VPIRelationMode::Handle);
        if (incoming.size() != 1)
          return anchor.emitOpError("source expression requires one owning "
                                    "default or operand relation");
        auto &edge = *incoming.front();
        if (edge.sourceIdentity != declarationIds.lookup(parent) ||
            edge.selector != selector || edge.mode != mode ||
            (!operand && edge.ordinal != anchor.getOwnerOrdinal()))
          return edge.relation.emitOpError(
              "source expression relation disagrees with exact ownership");
      }
    }
    unsigned operands = 0;
    uint64_t ownedOrdinal = 0;
    for (GeneralRelation *entry : expressionOutgoing.lookup(identity)) {
      auto relation = entry->relation;
      bool operand =
          entry->selector ==
          static_cast<uint32_t>(reflection::VPIRelationKind::OperandRel);
      bool defaultExpr =
          entry->selector ==
          static_cast<uint32_t>(reflection::VPIRelationKind::ExprRel);
      if (expression && !operand)
        return relation.emitOpError(
            "source operation permits only Operand relations");
      if (!operand && !defaultExpr)
        continue;
      auto target =
          relation.getTarget().getKind() == VPIObjectRefKind::Symbol
              ? symbolTable.lookupNearestSymbolFrom<SimVPIObjectAnchorOp>(
                    relation, relation.getTarget().getSymbol())
              : SimVPIObjectAnchorOp{};
      if (!target)
        return relation.emitOpError(
            "original value expression requires an exact anchor target");
      if (target.getVpiKind() == static_cast<uint32_t>(Kind::Parameter)) {
        if (auto parameter = dyn_cast<SimVPIObjectAnchorOp>(root)) {
          if (target.getParentAttr() != parameter.getParentAttr() ||
              target.getOwnerOrdinal() >= parameter.getOwnerOrdinal() ||
              target.getDebugName().empty())
            return relation.emitOpError(
                "source reference requires an exact preceding parameter in the "
                "same declaration scope");
        } else {
          auto assignment = cast<SimVPIClassParameterAssignDeclOp>(root);
          auto type =
              symbolTable
                  .lookupNearestSymbolFrom<SimVPILexicalClassTypespecDeclOp>(
                      assignment, assignment.getOwnerAttr());
          if (!type || target.getParentAttr() != type.getOwnerAttr() ||
              target.getDebugName().empty())
            return relation.emitOpError(
                "class actual reference must name an exact derived parameter");
          if (auto properties = reflectionProperties(target))
            for (Attribute attribute : properties.getProperties()) {
              auto property = cast<VPIPropertyAttr>(attribute);
              if (property.getSelector().getValue().getZExtValue() == 70 &&
                  cast<BoolAttr>(property.getValue()).getValue())
                return relation.emitOpError("class actual reference cannot "
                                            "name a body local parameter");
            }
        }
      } else if ((target.getVpiKind() !=
                      static_cast<uint32_t>(Kind::Constant) &&
                  target.getVpiKind() !=
                      static_cast<uint32_t>(Kind::Operation)) ||
                 target.getParentAttr() !=
                     FlatSymbolRefAttr::get(anchor.getSymNameAttr())) {
        return relation.emitOpError(
            "source operand requires an owned expression occurrence or "
            "parameter declaration");
      } else if (target.getOwnerOrdinal() != ownedOrdinal++) {
        return relation.emitOpError(
            "source expression ownership ordinals must follow owned operand "
            "order");
      }
      operands += operand;
    }
    if (kind == Kind::Operation) {
      uint32_t opcode = 0;
      for (Attribute attribute : reflectionProperties(anchor).getProperties()) {
        auto property = cast<VPIPropertyAttr>(attribute);
        if (property.getSelector().getValue().getZExtValue() == 39)
          opcode = static_cast<uint32_t>(
              cast<IntegerAttr>(property.getValue()).getInt());
      }
      const auto *descriptor = reflection::findVPISourceOperator(opcode);
      if (!descriptor || operands != descriptor->arity)
        return anchor.emitOpError(
            "source operation operand count does not match its operator");
    }
  }

  for (SimVPINetIdentityDeclOp identity : vpiNetIdentities) {
    auto found = identitySimNets.find(identity.getId());
    if (found == identitySimNets.end())
      return identity.emitOpError(
          "requires one vpiSimNet relation to its backing net");
    GeneralRelation *simNet = found->second;
    if (simNet->targetTag != static_cast<uint32_t>(VPIObjectRefKind::Net) ||
        simNet->targetId != identity.getBackingNetId())
      return simNet->relation.emitOpError(
          "vpiSimNet target does not match the net identity's backing net");
  }

  struct ElementShape {
    Type type;
    uint32_t kind;
    uint32_t flags;
    uint64_t valueSize;
    uint64_t alignment;
    uint64_t bitWidth;
    SmallVector<int64_t, 2> traceOffsets;
    SmallVector<int32_t, 2> traceKinds;
  };
  llvm::DenseMap<uint64_t, ElementShape> elementShapes;
  auto recordElementShape =
      [&](Operation *operation, uint64_t typeId, Type type, uint32_t kind,
          uint32_t flags, uint64_t valueSize, uint64_t alignment,
          uint64_t bitWidth, ArrayRef<int64_t> traceOffsets,
          ArrayRef<int32_t> traceKinds) -> WalkResult {
    ElementShape shape{type,
                       kind,
                       flags,
                       valueSize,
                       alignment,
                       bitWidth,
                       SmallVector<int64_t, 2>(traceOffsets),
                       SmallVector<int32_t, 2>(traceKinds)};
    auto [found, inserted] = elementShapes.try_emplace(typeId, shape);
    if (!inserted &&
        (found->second.type != shape.type || found->second.kind != shape.kind ||
         found->second.flags != shape.flags ||
         found->second.valueSize != shape.valueSize ||
         found->second.alignment != shape.alignment ||
         found->second.bitWidth != shape.bitWidth ||
         found->second.traceOffsets != shape.traceOffsets ||
         found->second.traceKinds != shape.traceKinds)) {
      operation->emitOpError()
          << "element type ID " << typeId
          << " conflicts with another container descriptor in the design";
      return WalkResult::interrupt();
    }
    return WalkResult::advance();
  };
  WalkResult descriptors = walk([&](Operation *operation) {
    if (auto bind = dyn_cast<SimVirtualInterfaceBindOp>(operation)) {
      auto found = scopes.find(bind.getScopeId());
      if (found == scopes.end()) {
        bind.emitOpError("references an unknown interface scope ID ")
            << bind.getScopeId();
        return WalkResult::interrupt();
      }
      auto interfaceType = found->second.getInterfaceTypeAttr();
      if (!interfaceType) {
        bind.emitOpError("scope ID does not identify an interface instance");
        return WalkResult::interrupt();
      }
      if (interfaceType != bind.getResult().getType().getInterfaceName()) {
        bind.emitOpError(
            "scope interface specialization does not match result type");
        return WalkResult::interrupt();
      }
    }
    if (auto create = dyn_cast<SimContainerCreateOp>(operation))
      return recordElementShape(
          operation, create.getTypeId(),
          getContainerElement(create.getResult().getType()),
          static_cast<uint32_t>(create.getElementKind()),
          static_cast<uint32_t>(create.getElementFlags()),
          create.getValueSize(), create.getAlignment(), create.getBitWidth(),
          create.getTraceOffsets(), create.getTraceKinds());
    if (auto create = dyn_cast<SimAssocCreateOp>(operation))
      return recordElementShape(operation, create.getTypeId(),
                                create.getResult().getType().getElementType(),
                                static_cast<uint32_t>(create.getElementKind()),
                                static_cast<uint32_t>(create.getElementFlags()),
                                create.getValueSize(), create.getAlignment(),
                                create.getBitWidth(), create.getTraceOffsets(),
                                create.getTraceKinds());
    return WalkResult::advance();
  });
  if (descriptors.wasInterrupted())
    return failure();
  if (!sawRoot)
    return emitOpError("design must contain a root scope descriptor");
  auto verifyDense = [&](const llvm::DenseSet<uint64_t> &ids,
                         StringRef kind) -> LogicalResult {
    for (uint64_t id = 0; id < ids.size(); ++id)
      if (!ids.count(id))
        return emitOpError()
               << kind << " IDs must be dense from zero; missing " << id;
    return success();
  };
  if (failed(verifyDense(scopeIds, "scope")) ||
      failed(verifyDense(vpiAnchorIds, "VPI object anchor")) ||
      failed(verifyDense(vpiNetIdentityIds, "VPI net identity")) ||
      failed(verifyDense(nettypeIds, "VPI nettype")) ||
      failed(verifyDense(typespecIds, "VPI typespec")) ||
      failed(verifyDense(enumConstIds, "VPI enum constant")) ||
      failed(verifyDense(storageIds, "storage")) ||
      failed(verifyDense(netIds, "net")) ||
      failed(verifyDense(driverIds, "driver")) ||
      failed(verifyDense(portIds, "port")) ||
      failed(verifyDense(connectionIds, "net connection")))
    return failure();

  llvm::DenseMap<Attribute, llvm::DenseSet<uint64_t>> anchorOrdinals;
  llvm::DenseMap<Attribute, SimVPIObjectAnchorOp> backedAnchors;
  for (SimVPIObjectAnchorOp anchor : vpiAnchors) {
    if (!scopeIds.count(anchor.getEnclosingScopeId()))
      return anchor.emitOpError("references an unknown enclosing scope ID");
    Attribute parent = anchor.getParentAttr();
    if (!parent)
      parent = UnitAttr::get(getContext());
    if (!anchorOrdinals[parent].insert(anchor.getOwnerOrdinal()).second)
      return anchor.emitOpError(
          "duplicates an ordinal under the same lexical parent");

    VPIObjectBackingAttr backing = anchor.getBackingAttr();
    if (!backing)
      continue;
    Attribute key = ArrayAttr::get(
        getContext(),
        {IntegerAttr::get(IntegerType::get(getContext(), 32),
                          static_cast<uint32_t>(backing.getKind())),
         backing.getId() ? Attribute(backing.getId())
                         : Attribute(backing.getSymbol())});
    if (auto [it, inserted] = backedAnchors.try_emplace(key, anchor); !inserted)
      return anchor.emitOpError() << "duplicates physical backing used by "
                                  << it->second.getSymNameAttr();
    switch (backing.getKind()) {
    case VPIObjectBackingKind::Scope:
      if (!scopeIds.count(backing.getId().getValue().getZExtValue()))
        return anchor.emitOpError("references an unknown backing scope ID");
      if (backing.getId().getValue().getZExtValue() !=
          anchor.getEnclosingScopeId())
        return anchor.emitOpError(
            "backing scope must equal the anchor's enclosing scope");
      if (SimScopeDeclOp scope =
              scopes.lookup(backing.getId().getValue().getZExtValue());
          effectiveScopeVPIKind(scope) != anchor.getVpiKind())
        return anchor.emitOpError(
            "VPI anchor kind does not match its backing scope kind");
      if (SimScopeDeclOp scope =
              scopes.lookup(backing.getId().getValue().getZExtValue());
          scope.getHierarchicalNameAttr() != anchor.getHierarchicalNameAttr())
        return anchor.emitOpError(
            "backing scope hierarchy must equal the anchor hierarchy");
      break;
    case VPIObjectBackingKind::Class:
      break;
    case VPIObjectBackingKind::Storage: {
      auto storage =
          storagesById.lookup(backing.getId().getValue().getZExtValue());
      if (!storage)
        return anchor.emitOpError("references an unknown backing storage ID");
      if (!isa<EventType>(storage.getType()) || !storage.getVpiTypeAttr() ||
          storage.getVpiTypeAttr().getKind() != VPITypeKind::Event)
        return anchor.emitOpError("backing named event requires exact event "
                                  "storage and VPI semantics");
      if (storage.getLifetime() == Lifetime::Automatic)
        return anchor.emitOpError(
            "backing named event requires static storage");
      if (storage.getScopeId() != anchor.getEnclosingScopeId())
        return anchor.emitOpError(
            "backing storage scope must equal the anchor's enclosing scope");
      if (!storage.getHierarchicalName() ||
          *storage.getHierarchicalName() != anchor.getHierarchicalName())
        return anchor.emitOpError(
            "backing storage hierarchy must equal the anchor hierarchy");
      break;
    }
    case VPIObjectBackingKind::Net: {
      uint64_t id = backing.getId().getValue().getZExtValue();
      auto found = nets.find(id);
      if (found == nets.end())
        return anchor.emitOpError("references an unknown backing net ID");
      SimNetDeclOp net = found->second;
      if (anchor.getEnclosingScopeId() != net.getScopeId())
        return anchor.emitOpError(
            "backing net scope must equal the anchor's enclosing scope");
      if (!net.getHierarchicalName() ||
          *net.getHierarchicalName() != anchor.getHierarchicalName())
        return anchor.emitOpError(
            "backing net hierarchy must equal the anchor hierarchy");
      if (!net.getVpiTypeAttr())
        return anchor.emitOpError(
            "backing interconnect net requires VPI type semantics");
      if (!hasNetSubtype(net, 16)) // vpiInterconnect
        return anchor.emitOpError(
            "backing interconnect net requires vpiInterconnect subtype");
      break;
    }
    case VPIObjectBackingKind::CodeUnit: {
      if (!codeUnitIds.count(backing.getId().getValue().getZExtValue()))
        return anchor.emitOpError("references an unknown backing code-unit ID");
      SimCodeUnitDeclOp codeUnit =
          codeUnits.lookup(backing.getId().getValue().getZExtValue());
      if (codeUnit.getInternalAttr() || dpi::has<dpi::Import>(codeUnit) ||
          !isVPIVisibleEntryKind(codeUnit.getCodeUnitKind()))
        return anchor.emitOpError(
            "backing code unit must be a VPI-visible task or function");
      if (codeUnit.getScopeId() != anchor.getEnclosingScopeId())
        return anchor.emitOpError(
            "backing code-unit scope must equal the anchor's enclosing "
            "scope");
      if (codeUnit.getHierarchicalNameAttr() !=
          anchor.getHierarchicalNameAttr())
        return anchor.emitOpError(
            "backing code-unit hierarchy must equal the anchor hierarchy");
      if ((anchor.getVpiKind() ==
               static_cast<uint32_t>(reflection::VPIObjectKind::Task) &&
           codeUnit.getCodeUnitKind() != EntryKind::Task) ||
          (anchor.getVpiKind() ==
               static_cast<uint32_t>(reflection::VPIObjectKind::Function) &&
           codeUnit.getCodeUnitKind() != EntryKind::Function))
        return anchor.emitOpError(
            "VPI anchor kind does not match its backing code-unit kind");
      break;
    }
    }
  }
  llvm::DenseMap<Operation *, uint64_t> terminalInputCounts;
  for (auto terminal : vpiAnchors) {
    if (terminal.getVpiKind() !=
        static_cast<uint32_t>(reflection::VPIObjectKind::PrimTerm))
      continue;
    auto primitive = symbolTable.lookupNearestSymbolFrom<SimVPIObjectAnchorOp>(
        terminal, terminal.getParentAttr());
    if (!primitive)
      continue; // The symbol verifier diagnoses the missing owner.
    auto properties = reflectionProperties(terminal);
    if (!properties)
      continue;
    auto &count = terminalInputCounts[primitive];
    for (Attribute attribute : properties.getProperties()) {
      auto property = cast<VPIPropertyAttr>(attribute);
      if (property.getSelector().getInt() == 20 &&
          cast<IntegerAttr>(property.getValue()).getInt() == 1)
        ++count;
    }
  }
  for (auto [owner, count] : terminalInputCounts)
    if (cast<SimVPIObjectAnchorOp>(owner).getPrimitiveInputCount() != count)
      return owner->emitOpError(
          "primitive input count must match its terminal directions");
  for (const auto &entry : anchorOrdinals) {
    for (uint64_t ordinal = 0; ordinal != entry.second.size(); ++ordinal)
      if (!entry.second.contains(ordinal))
        return emitOpError(
            "VPI anchor ordinals must be dense under each lexical parent");
  }

  for (SimVPIObjectAnchorOp anchor : vpiAnchors) {
    llvm::SmallPtrSet<Operation *, 8> path;
    for (Operation *cursor = anchor; cursor;) {
      if (!path.insert(cursor).second)
        return anchor.emitOpError("lexical parent relation contains a cycle");
      FlatSymbolRefAttr parent;
      if (auto object = dyn_cast<SimVPIObjectAnchorOp>(cursor))
        parent = object.getParentAttr();
      else if (isa<SimVPIClassParameterAssignDeclOp>(cursor))
        break; // Its lexical type owner has a separate acyclic inheritance
               // proof.
      else
        parent = cast<SimVPITypespecDeclOp>(cursor).getOwnerAttr();
      cursor = parent ? lookupVPILexicalOwner(cursor, parent, symbolTable)
                      : nullptr;
    }
  }
  for (uint64_t id = 1; id <= classIds.size(); ++id)
    if (!classIds.count(id))
      return emitOpError() << "class IDs must be dense from one; missing "
                           << id;

  llvm::StringMap<SimFuncOp> functionsByName;
  for (SimFuncOp function : functions)
    functionsByName[function.getSymName()] = function;
  llvm::StringMap<llvm::DenseSet<uint64_t>> fieldOrdinals, methodSlots,
      interfaceMethodOrdinals;
  for (Operation &op : getBody().front()) {
    if (auto classDecl = dyn_cast<SimClassDeclOp>(op)) {
      if (auto base = classDecl.getBase()) {
        auto found = classes.find(*base);
        if (found == classes.end())
          return classDecl.emitOpError("references an unknown base class");
        if (found->second.getIsInterface())
          return classDecl.emitOpError("cannot extend an interface class");
        if (found->second.getIsFinal())
          return classDecl.emitOpError("cannot extend a final class");
      }
      if (ArrayAttr interfaces = classDecl.getInterfacesAttr()) {
        for (Attribute attribute : interfaces) {
          auto reference = cast<FlatSymbolRefAttr>(attribute);
          auto found = classes.find(reference.getValue());
          if (found == classes.end() || !found->second.getIsInterface())
            return classDecl.emitOpError(
                "implements list references a non-interface class");
        }
      }
    } else if (auto field = dyn_cast<SimClassFieldDeclOp>(op)) {
      if (!fieldOrdinals[field.getOwner()].insert(field.getOrdinal()).second)
        return field.emitOpError(
            "owner class contains a duplicate direct-property ordinal");
    } else if (auto method = dyn_cast<SimClassMethodDeclOp>(op)) {
      if (method.getSlot() && *method.getSlot() != interfaceDispatchSlot &&
          !methodSlots[method.getOwner()].insert(*method.getSlot()).second)
        return method.emitOpError(
            "owner class contains a duplicate virtual-method slot");
      if (method.getInterfaceOrdinalAttr() &&
          !interfaceMethodOrdinals[method.getOwner()]
               .insert(*method.getInterfaceOrdinal())
               .second)
        return method.emitOpError(
            "owner interface contains a duplicate method ordinal");
      if (auto implementation = method.getImplementation()) {
        auto found = functionsByName.find(*implementation);
        if (found == functionsByName.end() ||
            found->second.getFunctionType() != method.getFunctionType())
          return method.emitOpError(
              "implementation is missing or has an incompatible signature");
        EntryKind expected =
            method.getIsTask() ? EntryKind::Task : EntryKind::Function;
        if (found->second.getEntryKind() != expected)
          return method.emitOpError(
              "implementation entry kind does not match the method kind");
      }
    }
  }
  // Memoize completed paths rather than retracing every ancestor chain or
  // shared interface diamond at each declaration. Both checks are iterative
  // and visit each vertex and direct inheritance edge once.
  llvm::DenseMap<Operation *, uint8_t> baseState, interfaceState;
  for (Operation &op : getBody().front()) {
    auto root = dyn_cast<SimClassDeclOp>(op);
    if (!root || baseState.lookup(root) == 2)
      continue;
    SmallVector<SimClassDeclOp, 16> path;
    SimClassDeclOp current = root;
    while (current && !baseState.lookup(current)) {
      baseState[current] = 1;
      path.push_back(current);
      auto base = current.getBase();
      current = base ? classes.lookup(*base) : SimClassDeclOp{};
    }
    if (current && baseState.lookup(current) == 1)
      return root.emitOpError("class inheritance contains a cycle");
    for (auto declaration : path)
      baseState[declaration] = 2;
  }
  struct InterfaceFrame {
    SimClassDeclOp declaration;
    ArrayAttr parents;
    size_t next = 0;
  };
  for (Operation &op : getBody().front()) {
    auto root = dyn_cast<SimClassDeclOp>(op);
    if (!root || !root.getIsInterface() || interfaceState.lookup(root) == 2)
      continue;
    interfaceState[root] = 1;
    SmallVector<InterfaceFrame, 16> pending{{root, root.getInterfacesAttr()}};
    while (!pending.empty()) {
      auto &frame = pending.back();
      if (!frame.parents || frame.next == frame.parents.size()) {
        interfaceState[frame.declaration] = 2;
        pending.pop_back();
        continue;
      }
      auto reference = cast<FlatSymbolRefAttr>(frame.parents[frame.next++]);
      auto parent = classes.lookup(reference.getValue());
      uint8_t state = interfaceState.lookup(parent);
      if (state == 1)
        return root.emitOpError("interface inheritance contains a cycle");
      if (!state) {
        interfaceState[parent] = 1;
        pending.push_back({parent, parent.getInterfacesAttr()});
      }
    }
  }
  for (auto &entry : interfaceMethodOrdinals)
    for (uint64_t ordinal = 0; ordinal != entry.second.size(); ++ordinal)
      if (!entry.second.count(ordinal))
        return emitOpError() << "interface " << entry.first()
                             << " contains a non-dense method ordinal set";
  for (Operation &op : getBody().front()) {
    if (auto scope = dyn_cast<SimScopeDeclOp>(op)) {
      if (scope.getParentAttr() && !scopeIds.count(*scope.getParent()))
        return scope.emitOpError("references an unknown parent scope ID");
      if (scope.getParentAttr() && *scope.getParent() >= scope.getId())
        return scope.emitOpError(
            "parent scope ID must precede the child scope ID");
    } else if (auto codeUnit = dyn_cast<SimCodeUnitDeclOp>(op)) {
      if (!scopeIds.count(codeUnit.getScopeId()))
        return codeUnit.emitOpError("references an unknown scope ID");
    } else if (auto statement = dyn_cast<SimStatementDeclOp>(op)) {
      if (!scopeIds.count(statement.getScopeId()))
        return statement.emitOpError("references an unknown scope ID");
      if (auto ownerID = statement.getCodeUnitId()) {
        auto owner = codeUnits.find(*ownerID);
        if (owner == codeUnits.end())
          return statement.emitOpError("references an unknown code-unit ID");
        if (owner->second.getScopeId() != statement.getScopeId())
          return statement.emitOpError(
              "scope ID must match the owning code unit's scope");
      }
      if (auto parentID = statement.getParentId()) {
        auto parent = statements.find(*parentID);
        if (parent == statements.end() ||
            parent->second.getCodeUnitId() != statement.getCodeUnitId() ||
            parent->second.getScopeId() != statement.getScopeId())
          return statement.emitOpError(
              "references an unknown or cross-owner/scope parent statement");
      }
    } else if (auto site = dyn_cast<SimStatementSiteDeclOp>(op)) {
      auto statement = statements.find(site.getStatementId());
      if (statement == statements.end())
        return site.emitOpError("references an unknown statement ID");
      auto phase = static_cast<reflection::VPIStatementCallbackPhase>(
          static_cast<uint16_t>(site.getPhase()));
      if (!reflection::isVPIStatementCallbackPhase(
              statement->second.getVpiKind(), phase))
        return site.emitOpError(
            "phase is not legal for the statement's Table 38-6 policy");
    } else if (isa<SimVPILexicalClassTypespecDeclOp, SimVPIClassExtendsDeclOp,
                   SimVPIClassParameterAssignDeclOp>(op)) {
      if (!scopeIds.count(cast<IntegerAttr>(op.getAttr("scope_id")).getInt()))
        return op.emitOpError("references an unknown scope ID");
    } else if (auto alias = dyn_cast<SimVPIUnresolvedTypeAliasDeclOp>(op)) {
      if (!scopeIds.count(alias.getScopeId()))
        return alias.emitOpError("references an unknown scope ID");
    } else if (auto typespec = dyn_cast<SimVPITypespecDeclOp>(op)) {
      if (!scopeIds.count(typespec.getScopeId()))
        return typespec.emitOpError("references an unknown scope ID");
    } else if (auto nettype = dyn_cast<SimVPINettypeDeclOp>(op)) {
      if (!scopeIds.count(nettype.getScopeId()))
        return nettype.emitOpError("references an unknown scope ID");
    } else if (auto storage = dyn_cast<SimStorageDeclOp>(op)) {
      if (!scopeIds.count(storage.getScopeId()))
        return storage.emitOpError("references an unknown scope ID");
    } else if (auto net = dyn_cast<SimNetDeclOp>(op)) {
      if (!scopeIds.count(net.getScopeId()))
        return net.emitOpError("references an unknown scope ID");
    } else if (auto driver = dyn_cast<SimDriverDeclOp>(op)) {
      auto netType = netTypes.find(driver.getNetId());
      if (!scopeIds.count(driver.getScopeId()) || netType == netTypes.end() ||
          netType->second != driver.getType())
        return driver.emitOpError(
            "references an incompatible scope or net descriptor");
    } else if (auto port = dyn_cast<SimPortDeclOp>(op)) {
      auto &sourceTypes = port.getSourceIsNet() ? netTypes : storageTypes;
      auto source = sourceTypes.find(port.getSourceId());
      std::optional<unsigned> sourceWidth;
      if (source != sourceTypes.end())
        sourceWidth = getPackedWidth(source->second);
      std::optional<unsigned> portWidth = getPackedWidth(port.getType());
      uint64_t low = port.getSourceLow();
      bool invalidRange = !sourceWidth || !portWidth;
      if (!invalidRange)
        invalidRange = low > sourceWidth.value() ||
                       portWidth.value() > sourceWidth.value() - low;
      if (!scopeIds.count(port.getScopeId()) || source == sourceTypes.end() ||
          invalidRange ||
          (!invalidRange && packedRangeContainsFourState(source->second, low,
                                                         portWidth.value()) !=
                                containsFourStateLeaf(port.getType())))
        return port.emitOpError(
            "references an incompatible scope or source descriptor");
    } else if (auto connection = dyn_cast<SimNetConnectDeclOp>(op)) {
      auto lhs = netTypes.find(connection.getLhsNetId());
      auto rhs = netTypes.find(connection.getRhsNetId());
      if (!scopeIds.count(connection.getScopeId()) || lhs == netTypes.end() ||
          rhs == netTypes.end())
        return connection.emitOpError(
            "references an unknown scope or net descriptor");
      // A connection can name a run of an unpacked array of nets, whose
      // storage is laid out by provenance span. Both agree for a packed net.
      std::optional<uint64_t> lhsWidth = getProvenanceSpan(lhs->second);
      std::optional<uint64_t> rhsWidth = getProvenanceSpan(rhs->second);
      uint64_t width = connection.getWidth();
      uint64_t lhsOffset = connection.getLhsOffset();
      uint64_t rhsOffset = connection.getRhsOffset();
      bool lhsValid =
          lhsWidth && lhsOffset <= *lhsWidth && width <= *lhsWidth - lhsOffset;
      bool rhsValid = rhsWidth && (connection.getRhsReversed()
                                       ? width <= rhsOffset + 1
                                       : rhsOffset <= *rhsWidth &&
                                             width <= *rhsWidth - rhsOffset);
      if (!lhsValid || !rhsValid)
        return connection.emitOpError("contains an out-of-range bit run");
      if (containsFourStateLeaf(lhs->second) !=
          containsFourStateLeaf(rhs->second))
        return connection.emitOpError(
            "connects incompatible two-state and four-state nets");
      NetResolutionKind lhsResolution =
          netResolutions.lookup(connection.getLhsNetId());
      NetResolutionKind rhsResolution =
          netResolutions.lookup(connection.getRhsNetId());
      auto category = [](NetResolutionKind kind) {
        return kind == NetResolutionKind::Tri ? NetResolutionKind::Wire : kind;
      };
      lhsResolution = category(lhsResolution);
      rhsResolution = category(rhsResolution);
      bool mixed = lhsResolution != rhsResolution;
      if (mixed && !connection.getRhsDominates())
        return connection.emitOpError(
            "must identify the dominant endpoint in mixed net topology");
      auto isSupply = [](NetResolutionKind kind) {
        return kind == NetResolutionKind::Supply0 ||
               kind == NetResolutionKind::Supply1;
      };
      auto isWire = [](NetResolutionKind kind) {
        return kind == NetResolutionKind::Wire;
      };
      auto isPull = [](NetResolutionKind kind) {
        return kind == NetResolutionKind::Tri0 ||
               kind == NetResolutionKind::Tri1;
      };
      std::optional<bool> requiredDominance;
      if (isSupply(lhsResolution) != isSupply(rhsResolution))
        requiredDominance = isSupply(rhsResolution);
      else if (!isSupply(lhsResolution) && !isSupply(rhsResolution) &&
               ((lhsResolution == NetResolutionKind::UWire) !=
                (rhsResolution == NetResolutionKind::UWire)))
        requiredDominance = rhsResolution == NetResolutionKind::UWire;
      else if ((lhsResolution == NetResolutionKind::TriReg &&
                isPull(rhsResolution)) ||
               (rhsResolution == NetResolutionKind::TriReg &&
                isPull(lhsResolution)))
        requiredDominance = isPull(rhsResolution);
      else if (isWire(lhsResolution) != isWire(rhsResolution))
        requiredDominance = isWire(lhsResolution);
      if (requiredDominance &&
          *connection.getRhsDominates() != *requiredDominance)
        return connection.emitOpError(
            "identifies the wrong dominant endpoint for these net types");
    }
  }

  // IEEE 1800-2017 28.16.2 gives -1 in a normalized triple the special
  // meaning "no charge decay". Port collapsing copies the dominating net's
  // delay to every logical alias, so legality is a property of each scalar
  // connectivity component's effective resolution rather than of the alias's
  // declared resolution kind.
  bool hasOmittedDecay = false;
  for (auto entry : nets) {
    auto delays = entry.second.getPropagationDelays();
    if (!delays)
      continue;
    for (size_t index = 0; index != delays->size(); index += 3)
      hasOmittedDecay |= (*delays)[index] != -1 && (*delays)[index + 2] == -1;
  }
  if (hasOmittedDecay) {
    llvm::DenseMap<uint64_t, uint64_t> netBases;
    SmallVector<NetResolutionKind> resolutionByBit;
    for (uint64_t id = 0; id != netIds.size(); ++id) {
      SimNetDeclOp net = nets.lookup(id);
      std::optional<uint64_t> width = getProvenanceSpan(net.getType());
      if (!width || *width > UINT64_MAX - resolutionByBit.size())
        return net.emitOpError(
            "cannot verify omitted charge decay for this net type");
      netBases[id] = resolutionByBit.size();
      resolutionByBit.append(*width, net.getResolutionKind());
    }
    SmallVector<uint64_t> parents(resolutionByBit.size());
    for (uint64_t bit = 0; bit != parents.size(); ++bit)
      parents[bit] = bit;
    auto findRoot = [&](uint64_t bit) {
      uint64_t root = bit;
      while (parents[root] != root)
        root = parents[root];
      while (parents[bit] != bit) {
        uint64_t next = parents[bit];
        parents[bit] = root;
        bit = next;
      }
      return root;
    };
    struct DominanceEdge {
      uint64_t dominated;
      uint64_t dominating;
    };
    SmallVector<DominanceEdge> dominanceEdges;
    SmallVector<uint64_t> incompleteBits;
    for (SimNetConnectDeclOp connection : netConnections) {
      for (uint64_t index = 0; index != connection.getWidth(); ++index) {
        uint64_t lhs = netBases.lookup(connection.getLhsNetId()) +
                       connection.getLhsOffset() + index;
        uint64_t rhsOffset = connection.getRhsReversed()
                                 ? connection.getRhsOffset() - index
                                 : connection.getRhsOffset() + index;
        uint64_t rhs = netBases.lookup(connection.getRhsNetId()) + rhsOffset;
        uint64_t lhsRoot = findRoot(lhs);
        uint64_t rhsRoot = findRoot(rhs);
        if (lhsRoot != rhsRoot)
          parents[std::max(lhsRoot, rhsRoot)] = std::min(lhsRoot, rhsRoot);
        if (std::optional<bool> rhsDominates = connection.getRhsDominates())
          dominanceEdges.push_back(*rhsDominates ? DominanceEdge{lhs, rhs}
                                                 : DominanceEdge{rhs, lhs});
        else
          incompleteBits.push_back(lhs);
      }
    }
    llvm::DenseMap<uint64_t, SmallVector<uint64_t>> componentBits;
    llvm::DenseMap<uint64_t, llvm::SmallSet<NetResolutionKind, 2>>
        componentKinds;
    for (uint64_t bit = 0; bit != resolutionByBit.size(); ++bit) {
      uint64_t root = findRoot(bit);
      componentBits[root].push_back(bit);
      NetResolutionKind kind = resolutionByBit[bit];
      if (kind == NetResolutionKind::Tri)
        kind = NetResolutionKind::Wire;
      componentKinds[root].insert(kind);
    }
    llvm::DenseMap<uint64_t, llvm::DenseSet<uint64_t>> dominatedBits;
    llvm::DenseMap<uint64_t, llvm::DenseMap<uint64_t, SmallVector<uint64_t, 2>>>
        dominanceOutgoing;
    for (const DominanceEdge &edge : dominanceEdges) {
      uint64_t root = findRoot(edge.dominated);
      dominatedBits[root].insert(edge.dominated);
      dominanceOutgoing[root][edge.dominated].push_back(edge.dominating);
    }
    llvm::DenseSet<uint64_t> incompleteComponents;
    for (uint64_t bit : incompleteBits)
      incompleteComponents.insert(findRoot(bit));
    llvm::DenseMap<uint64_t, NetResolutionKind> effectiveResolution;
    for (const auto &[root, kinds] : componentKinds) {
      if (kinds.size() == 1) {
        effectiveResolution[root] = *kinds.begin();
        continue;
      }
      if (incompleteComponents.count(root))
        continue;
      llvm::DenseMap<uint64_t, uint64_t> incomingCount;
      for (uint64_t bit : componentBits.lookup(root))
        incomingCount[bit] = 0;
      for (const auto &entry : dominanceOutgoing[root])
        for (uint64_t target : entry.second)
          ++incomingCount[target];
      SmallVector<uint64_t> pending;
      for (const auto &[bit, count] : incomingCount)
        if (count == 0)
          pending.push_back(bit);
      uint64_t visited = 0;
      while (!pending.empty()) {
        uint64_t bit = pending.pop_back_val();
        ++visited;
        auto outgoing = dominanceOutgoing[root].find(bit);
        if (outgoing != dominanceOutgoing[root].end())
          for (uint64_t target : outgoing->second)
            if (--incomingCount[target] == 0)
              pending.push_back(target);
      }
      if (visited != componentBits.lookup(root).size())
        continue;
      std::optional<NetResolutionKind> effective;
      for (uint64_t bit : componentBits.lookup(root)) {
        if (dominatedBits[root].count(bit))
          continue;
        NetResolutionKind kind = resolutionByBit[bit];
        if (kind == NetResolutionKind::Tri)
          kind = NetResolutionKind::Wire;
        if (effective && *effective != kind) {
          effective.reset();
          break;
        }
        effective = kind;
      }
      if (effective)
        effectiveResolution[root] = *effective;
    }
    for (uint64_t id = 0; id != netIds.size(); ++id) {
      SimNetDeclOp net = nets.lookup(id);
      auto delays = net.getPropagationDelays();
      if (!delays)
        continue;
      uint64_t width = *getPackedWidth(net.getType());
      for (uint64_t bit = 0; bit != width; ++bit) {
        size_t index = delays->size() == 3 ? 0 : static_cast<size_t>(bit) * 3;
        if ((*delays)[index] == -1 || (*delays)[index + 2] != -1)
          continue;
        uint64_t root = findRoot(netBases.lookup(id) + bit);
        if (effectiveResolution.lookup(root) != NetResolutionKind::TriReg)
          return net.emitOpError(
              "omitted charge decay requires an effective trireg component");
      }
    }
  }

  // Descriptor tables live on this operation, so descriptor references are
  // resolved here rather than in a function-local verifier: an operation pass
  // on one function may run concurrently with passes on its siblings, and a
  // nested verifier must not reach into shared parent state. Callee symbols
  // instead use SymbolUserOpInterface, which the framework verifies against
  // this symbol table with a cached SymbolTableCollection.
  llvm::DenseMap<uint64_t, SimFuncOp> executableCodeUnits;
  for (SimFuncOp function : functions) {
    bool pendingClockedSamplePlan = static_cast<bool>(
        ::obelisk::schedule::get<::obelisk::schedule::Field::ClockedSamplePlan>(
            function));
    if (!function.isExternal() &&
        function.getEntryKind() != EntryKind::RootInitializer &&
        !function.getCodeUnitIdAttr() && !pendingClockedSamplePlan)
      return function.emitOpError(
          "defined non-root function requires a code-unit ID");
    if (auto id = function.getCodeUnitId()) {
      auto declaration = codeUnits.find(*id);
      if (declaration == codeUnits.end())
        return function.emitOpError("references an unknown code-unit ID");
      if (declaration->second.getCodeUnitKind() != function.getEntryKind())
        return function.emitOpError(
            "entry kind does not match its code-unit declaration");
      if (!function.isExternal()) {
        auto [first, inserted] = executableCodeUnits.try_emplace(*id, function);
        if (!inserted) {
          function.emitOpError()
              << "code-unit ID " << *id
              << " is referenced by multiple executable functions";
          first->second.emitRemark("first executable function is here");
          return failure();
        }
      }
    }
    if (ArrayAttr bindings = sim::get<sim::metadata::Bindings>(function))
      for (Attribute attribute : bindings) {
        auto binding = dyn_cast<DescriptorBindingAttr>(attribute);
        if (!binding)
          continue;
        auto reference = dyn_cast<RefType>(binding.getType());
        auto descriptor = storageTypes.find(binding.getDescriptor());
        if (!reference || descriptor == storageTypes.end() ||
            descriptor->second != reference.getElementType())
          return function.emitOpError()
                 << "descriptor binding for path '"
                 << binding.getPath().getValue()
                 << "' references an unknown or incompatible storage "
                    "descriptor";
      }
    WalkResult result = function.walk([&](Operation *op) {
      auto verifyDescriptor = [&](uint64_t id, Type elementType,
                                  const llvm::DenseMap<uint64_t, Type> &table,
                                  StringRef kind) {
        auto descriptor = table.find(id);
        if (descriptor != table.end() && descriptor->second == elementType)
          return WalkResult::advance();
        op->emitOpError() << "references an unknown or incompatible " << kind
                          << " descriptor";
        return WalkResult::interrupt();
      };
      if (auto lookup = dyn_cast<SimContextStorageOp>(op))
        return verifyDescriptor(lookup.getId(),
                                lookup.getResult().getType().getElementType(),
                                storageTypes, "storage");
      if (auto lookup = dyn_cast<SimContextNetOp>(op))
        return verifyDescriptor(lookup.getId(),
                                lookup.getResult().getType().getElementType(),
                                netTypes, "net");
      if (auto lookup = dyn_cast<SimContextDriverOp>(op))
        return verifyDescriptor(lookup.getId(),
                                lookup.getResult().getType().getElementType(),
                                driverTypes, "driver");
      return WalkResult::advance();
    });
    if (result.wasInterrupted())
      return failure();

    for (unsigned index = 1; index < function.getNumArguments(); ++index) {
      std::optional<CaptureKind> kind =
          getCaptureKind(function.getArgAttrDict(index));
      if (!kind)
        return failure(); // Already diagnosed by the function verifier.
      auto descriptor = function.getArgAttrOfType<IntegerAttr>(
          index, sim::metadata::DescriptorId::getNameStr());
      std::optional<uint64_t> descriptorId;
      if (descriptor && !descriptor.getValue().isNegative() &&
          descriptor.getValue().getBitWidth() <= 64)
        descriptorId = descriptor.getValue().getZExtValue();
      Type argument = function.getArgumentTypes()[index];
      Type expected;
      switch (*kind) {
      case CaptureKind::Storage:
        if (descriptorId && storageTypes.count(*descriptorId)) {
          Type storageType = storageTypes.lookup(*descriptorId);
          auto rootType = function.getArgAttrOfType<TypeAttr>(
              index, sim::metadata::DescriptorRootType::getNameStr());
          auto low = function.getArgAttrOfType<IntegerAttr>(
              index, sim::metadata::DescriptorLow::getNameStr());
          if (!rootType) {
            if (low ||
                function.getArgAttr(
                    index, sim::metadata::DescriptorIndices::getNameStr()) ||
                function.getArgAttr(
                    index,
                    sim::metadata::DescriptorAggregateType::getNameStr()) ||
                function.getArgAttr(
                    index, sim::metadata::DescriptorPackedLow::getNameStr()))
              break;
            expected = RefType::get(getContext(), storageType);
            break;
          }
          auto reference = dyn_cast<RefType>(argument);
          std::optional<uint64_t> rootSpan = getProvenanceSpan(storageType);
          std::optional<uint64_t> viewSpan =
              reference ? getProvenanceSpan(reference.getElementType())
                        : std::nullopt;
          if (rootType.getValue() != storageType || !low ||
              low.getValue().isNegative() ||
              low.getValue().getActiveBits() > 64 || !rootSpan || !viewSpan)
            break;

          Type selected = storageType;
          uint64_t computedLow = 0;
          auto indices = function.getArgAttrOfType<DenseI64ArrayAttr>(
              index, sim::metadata::DescriptorIndices::getNameStr());
          bool validView = true;
          if (indices) {
            for (int64_t rawIndex : indices.asArrayRef()) {
              if (rawIndex < 0 || static_cast<uint64_t>(rawIndex) >
                                      std::numeric_limits<unsigned>::max()) {
                validView = false;
                break;
              }
              auto subelement = getAggregateProvenanceSubelement(
                  selected, static_cast<unsigned>(rawIndex));
              if (!subelement || subelement->first > UINT64_MAX - computedLow) {
                validView = false;
                break;
              }
              computedLow += subelement->first;
              selected = getAggregateElementType(
                  selected, static_cast<unsigned>(rawIndex));
            }
          }
          auto aggregateType = function.getArgAttrOfType<TypeAttr>(
              index, sim::metadata::DescriptorAggregateType::getNameStr());
          if ((indices && !aggregateType) ||
              (aggregateType && aggregateType.getValue() != selected))
            validView = false;

          auto packedLow = function.getArgAttrOfType<IntegerAttr>(
              index, sim::metadata::DescriptorPackedLow::getNameStr());
          Type viewElement = reference ? reference.getElementType() : Type{};
          if (validView && selected != viewElement) {
            std::optional<unsigned> selectedWidth = getPackedWidth(selected);
            std::optional<unsigned> resultWidth = getPackedWidth(viewElement);
            Type selectedScalar = getPackedScalarType(selected);
            Type resultScalar = getPackedScalarType(viewElement);
            if (!packedLow || packedLow.getValue().isNegative() ||
                packedLow.getValue().getActiveBits() > 64 || !selectedWidth ||
                !resultWidth || !selectedScalar || !resultScalar ||
                isa<LogicType>(selectedScalar) !=
                    isa<LogicType>(resultScalar)) {
              validView = false;
            } else {
              uint64_t packed = packedLow.getValue().getZExtValue();
              if (packed > *selectedWidth ||
                  *resultWidth > *selectedWidth - packed ||
                  packed > UINT64_MAX - computedLow)
                validView = false;
              else
                computedLow += packed;
            }
          } else if (packedLow && (packedLow.getValue().isNegative() ||
                                   packedLow.getValue().getActiveBits() > 64 ||
                                   packedLow.getValue().getZExtValue() != 0)) {
            validView = false;
          }

          uint64_t encodedLow = low.getValue().getZExtValue();
          if (validView && encodedLow == computedLow &&
              encodedLow <= *rootSpan && *viewSpan <= *rootSpan - encodedLow)
            expected = argument;
        }
        break;
      case CaptureKind::Net:
        if (descriptorId && netTypes.count(*descriptorId))
          expected = NetType::get(getContext(), netTypes.lookup(*descriptorId));
        break;
      case CaptureKind::Driver:
        if (descriptorId && driverTypes.count(*descriptorId))
          expected =
              DriverType::get(getContext(), driverTypes.lookup(*descriptorId));
        break;
      case CaptureKind::Event:
        if (isa<EventType>(argument))
          expected = argument;
        break;
      case CaptureKind::Context:
      case CaptureKind::Formal:
      case CaptureKind::Value:
        continue;
      }
      if (!expected || expected != argument)
        return function.emitOpError()
               << "argument #" << index
               << " has an incompatible capture descriptor";
    }
  }
  return success();
}

} // namespace obelisk::sim
