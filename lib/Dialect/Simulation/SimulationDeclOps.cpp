//===- SimulationDeclOps.cpp - Declaration op verifiers -------===//
//
// Verifiers for scope, storage, net, driver, covergroup, class, and reference
// declarations, and the design-level structural verification.
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
    if (interfaceType && StringRef(kind->apiName) != "vpiInterface")
      return emitOpError(
          "interface scope metadata and intrinsic VPI kind disagree");
  }
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
  return success();
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
  bool named = kind && (StringRef(kind->apiName) == "vpiNamedBegin" ||
                        StringRef(kind->apiName) == "vpiNamedFork");
  bool requiresScope =
      named || getVpiKind() == static_cast<uint16_t>(
                                   reflection::VPIObjectKind::ForeachStmt);
  if (named != getName().has_value() || (getName() && getName()->empty()))
    return emitOpError(named ? "named block requires a nonempty name"
                             : "only named begin/fork may carry a name");
  bool mayBeScope =
      (kind->families &
       reflection::vpiFamilyMask(reflection::VPIObjectFamily::Scope)) != 0;
  if (getIsScope() && !mayBeScope)
    return emitOpError("only a scope-capable statement may be marked is_scope");
  if (requiresScope && !getIsScope())
    return emitOpError(
        "named begin/fork and foreach statements must be marked is_scope");
  auto source = getLoc()->findInstanceOf<FileLineColLoc>();
  if (!source || source.getLine() == 0 || source.getColumn() == 0)
    return emitOpError("requires a concrete source file, line, and column");
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

LogicalResult SimVPIObjectAnchorOp::verify() {
  if (failed(verifyNonnegative(*this, getInventoryIdAttr(),
                               "VPI anchor inventory ID")) ||
      failed(verifyNonnegative(*this, getVpiKindAttr(), "VPI object kind")) ||
      failed(verifyNonnegative(*this, getEnclosingScopeIdAttr(),
                               "enclosing scope ID")) ||
      failed(verifyNonnegative(*this, getOwnerOrdinalAttr(), "owner ordinal")))
    return failure();
  if (getHierarchicalName().empty())
    return emitOpError("requires a nonempty source hierarchy name");
  const auto *kind = reflection::findVPIObjectKind(getVpiKind());
  if (getVpiKind() > UINT16_MAX || !kind ||
      kind->role != reflection::VPIObjectRole::Concrete)
    return emitOpError("VPI kind is not a concrete object");
  using Kind = reflection::VPIObjectKind;
  switch (static_cast<Kind>(getVpiKind())) {
  case Kind::Module:
  case Kind::Interface:
  case Kind::Program:
  case Kind::Package:
  case Kind::ClassDefn:
  case Kind::Task:
  case Kind::Function:
  case Kind::ClockingBlock:
  case Kind::GenScope:
  case Kind::PropertyDecl:
  case Kind::SequenceDecl:
    break;
  default:
    return emitOpError("kind cannot be a persistent lexical source anchor");
  }
  if (getIsCompilationUnitAttr() &&
      getVpiKind() != static_cast<uint32_t>(reflection::VPIObjectKind::Package))
    return emitOpError("compilation-unit anchor must have vpiPackage kind");
  if (getIsCompilationUnitAttr() && getParentAttr())
    return emitOpError("compilation-unit anchor cannot have a lexical parent");
  if (getParentAttr() &&
      getParentAttr() == FlatSymbolRefAttr::get(getSymNameAttr()))
    return emitOpError("cannot be its own lexical parent");
  if (VPIObjectBackingAttr backing = getBackingAttr()) {
    Kind anchorKind = static_cast<Kind>(getVpiKind());
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
    }
  } else {
    Kind anchorKind = static_cast<Kind>(getVpiKind());
    if (anchorKind == Kind::Module || anchorKind == Kind::Interface ||
        anchorKind == Kind::Program)
      return emitOpError(
          "module, interface, and program anchors require scope backing");
  }
  return success();
}

LogicalResult
SimVPIObjectAnchorOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  if (FlatSymbolRefAttr parent = getParentAttr()) {
    SimVPIObjectAnchorOp target =
        symbolTable.lookupNearestSymbolFrom<SimVPIObjectAnchorOp>(*this,
                                                                  parent);
    if (!target)
      return emitOpError("references an unknown VPI parent anchor");
    using Kind = reflection::VPIObjectKind;
    Kind childKind = static_cast<Kind>(getVpiKind());
    Kind parentKind = static_cast<Kind>(target.getVpiKind());
    auto isDesignScope = [](Kind kind) {
      return kind == Kind::Module || kind == Kind::Interface ||
             kind == Kind::Program;
    };
    auto isDeclarationScope = [&](Kind kind) {
      return isDesignScope(kind) || kind == Kind::Package ||
             kind == Kind::ClassDefn || kind == Kind::Task ||
             kind == Kind::Function || kind == Kind::GenScope;
    };
    bool legal = false;
    switch (childKind) {
    case Kind::Module:
    case Kind::Interface:
    case Kind::Program:
      legal = isDesignScope(parentKind) || parentKind == Kind::GenScope;
      break;
    case Kind::ClassDefn:
    case Kind::Task:
    case Kind::Function:
      legal = isDeclarationScope(parentKind);
      break;
    case Kind::PropertyDecl:
    case Kind::SequenceDecl:
      legal =
          isDeclarationScope(parentKind) || parentKind == Kind::ClockingBlock;
      break;
    case Kind::ClockingBlock:
      legal = isDesignScope(parentKind);
      break;
    case Kind::GenScope:
      legal = isDesignScope(parentKind) || parentKind == Kind::GenScope;
      break;
    case Kind::Package:
      // Compilation units use the vpiPackage kind internally so that they
      // can own declarations without manufacturing a fake vpiModule.  A
      // source package may therefore be nested under that wrapper, but
      // packages cannot otherwise be nested.
      legal = parentKind == Kind::Package && target.getIsCompilationUnitAttr();
      break;
    default:
      llvm_unreachable("anchor kind was checked by verify()");
    }
    if (!legal)
      return emitOpError("has an illegal lexical parent kind");
  } else {
    using Kind = reflection::VPIObjectKind;
    Kind kind = static_cast<Kind>(getVpiKind());
    if (kind != Kind::Package && kind != Kind::Module &&
        kind != Kind::Interface && kind != Kind::Program)
      return emitOpError("kind requires a lexical parent anchor");
  }
  VPIObjectBackingAttr backing = getBackingAttr();
  if (backing && backing.getKind() == VPIObjectBackingKind::Class) {
    SimClassDeclOp target = symbolTable.lookupNearestSymbolFrom<SimClassDeclOp>(
        *this, backing.getSymbol());
    if (!target)
      return emitOpError("references an unknown backing class declaration");
  }
  return success();
}

LogicalResult SimVPITypespecDeclOp::verify() {
  if (failed(verifyNonnegative(*this, getIdAttr(), "typespec ID")) ||
      failed(verifyNonnegative(*this, getScopeIdAttr(), "scope ID")) ||
      (getSourceTypeIdentityAttr() &&
       failed(verifyNonnegative(*this, getSourceTypeIdentityAttr(),
                                "source type identity"))))
    return failure();
  if (getHierarchicalName().empty() || getDebugName().empty())
    return emitOpError("requires nonempty source names");
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
  if (getOrigin() == VPITypespecOrigin::AnonymousEnum &&
      (getTargetType().getKind() != VPITypeKind::Enum ||
       !getSourceTypeIdentityAttr()))
    return emitOpError(
        "anonymous-enum origin requires an enum typespec and exact source "
        "type identity");
  return success();
}

LogicalResult SimVPIEnumConstDeclOp::verify() {
  if (failed(verifyNonnegative(*this, getIdAttr(), "enum-constant ID")) ||
      failed(verifyNonnegative(*this, getOrdinalAttr(), "enum ordinal")))
    return failure();
  if (getName().empty() || getValue().empty())
    return emitOpError("requires a nonempty name and constant value");
  return success();
}

LogicalResult
SimVPIEnumConstDeclOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  SimVPITypespecDeclOp target =
      symbolTable.lookupNearestSymbolFrom<SimVPITypespecDeclOp>(
          *this, getEnumTypespecAttr());
  if (!target || target.getTargetType().getKind() != VPITypeKind::Enum)
    return emitOpError("references an unknown or non-enum typespec");
  return success();
}

LogicalResult
SimVPITypespecDeclOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  SimVPIObjectAnchorOp anchor =
      symbolTable.lookupNearestSymbolFrom<SimVPIObjectAnchorOp>(*this,
                                                                getOwnerAttr());
  if (!anchor)
    return emitOpError("typespec must be owned by a VPI anchor");
  if (anchor.getEnclosingScopeId() != getScopeId())
    return emitOpError("scope ID does not match the owner anchor");
  if (getOrigin() == VPITypespecOrigin::Typedef &&
      !reflection::findVPITraversal(anchor.getVpiKind(), 725,
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
  return getVpiType() ? verifyVPITypeSemantics(emit, getType(), *getVpiType())
                      : success();
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
  return getVpiType() ? verifyVPITypeSemantics(emit, getType(), *getVpiType())
                      : success();
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
  return getVpiType() ? verifyVPITypeSemantics(emit, getType(), *getVpiType())
                      : success();
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
  if (failed(verifyPositive(*this, getIdAttr(), "covergroup ID")))
    return failure();
  if (getCoverpointBins().empty())
    return emitOpError("requires at least one coverpoint");
  for (int64_t bins : getCoverpointBins())
    if (bins < 0 || static_cast<uint64_t>(bins) > UINT32_MAX)
      return emitOpError(
          "every coverpoint requires a nonnegative 32-bit contributing-bin "
          "count");
  return success();
}

LogicalResult SimCovergroupNullOp::verify() {
  return verifyCovergroupHandle(*this, getResult().getType());
}

LogicalResult SimVirtualInterfaceBindOp::verify() {
  if (failed(verifyPositive(*this, getScopeIdAttr(), "interface scope ID")))
    return failure();
  SimDesignOp design = (*this)->getParentOfType<SimDesignOp>();
  if (!design)
    return emitOpError("requires an enclosing simulation design");
  SimScopeDeclOp found;
  for (SimScopeDeclOp scope : design.getBody().front().getOps<SimScopeDeclOp>())
    if (scope.getId() == getScopeId()) {
      found = scope;
      break;
    }
  if (!found)
    return emitOpError("references an unknown interface scope ID ")
           << getScopeId();
  if (!found.getInterfaceTypeAttr())
    return emitOpError("scope ID does not identify an interface instance");
  if (found.getInterfaceTypeAttr() != getResult().getType().getInterfaceName())
    return emitOpError(
        "scope interface specialization does not match result type");
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

LogicalResult SimCovergroupCreateOp::verify() {
  SimCovergroupDeclOp declaration =
      lookupCovergroup(*this, getDeclarationAttr());
  if (!declaration)
    return emitOpError("references an unknown covergroup declaration");
  auto expected = FlatSymbolRefAttr::get(declaration.getOperation());
  if (getResult().getType().getCovergroupName() != expected)
    return emitOpError("result type must name the selected declaration");
  return success();
}

LogicalResult SimCovergroupSampleEnabledOp::verify() {
  return verifyCovergroupHandle(*this, getHandle().getType());
}

LogicalResult SimCovergroupBinHitOp::verify() {
  SimCovergroupDeclOp declaration =
      lookupCovergroup(*this, getHandle().getType().getCovergroupName());
  if (!declaration)
    return emitOpError("handle type references an unknown declaration");
  uint64_t coverpoint = getCoverpoint();
  if (coverpoint >= declaration.getCoverpointBins().size())
    return emitOpError("coverpoint index is outside the declaration");
  uint64_t bin = getBin();
  if (bin >= static_cast<uint64_t>(declaration.getCoverpointBins()[coverpoint]))
    return emitOpError("bin index is outside the selected coverpoint");
  return success();
}

LogicalResult SimCovergroupSampleOp::verify() {
  SimCovergroupDeclOp declaration =
      lookupCovergroup(*this, getHandle().getType().getCovergroupName());
  if (!declaration)
    return emitOpError(
        "handle type references an unknown covergroup declaration");
  uint64_t expected = 0;
  for (int64_t bins : declaration.getCoverpointBins()) {
    if (static_cast<uint64_t>(bins) > UINT64_MAX - expected)
      return emitOpError("declaration bin inventory is too large");
    expected += static_cast<uint64_t>(bins);
  }
  if (getHits().size() != expected)
    return emitOpError() << "requires exactly " << expected
                         << " flattened bin-hit operands";
  return success();
}

LogicalResult SimCovergroupStartOp::verify() {
  return verifyCovergroupHandle(*this, getHandle().getType());
}

LogicalResult SimCovergroupStopOp::verify() {
  return verifyCovergroupHandle(*this, getHandle().getType());
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
  if (Attribute attribute = (*this)->getAttr(metadata::randomModeField)) {
    auto reference = dyn_cast<FlatSymbolRefAttr>(attribute);
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
            !field->hasAttr(metadata::randomObjectEdge))
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
          !target->getAttrOfType<RandomVariableKindAttr>(
              metadata::randomVariableKind))
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
  Attribute bitstreamMember = (*this)->getAttr(metadata::classBitstreamMember);
  Attribute bitstreamVisibility =
      (*this)->getAttr(metadata::classBitstreamVisibility);
  if (bitstreamMember && !isa<UnitAttr>(bitstreamMember))
    return emitOpError(
        "class bit-stream member marker must be a unit attribute");
  if (bitstreamMember && getIsStatic())
    return emitOpError("static properties cannot be object bit-stream members");
  auto visibility = dyn_cast_or_null<IntegerAttr>(bitstreamVisibility);
  if (bitstreamVisibility &&
      (!visibility || !visibility.getType().isInteger(32) ||
       visibility.getValue().isNegative() ||
       visibility.getValue().getZExtValue() > 2))
    return emitOpError("class bit-stream visibility must be an i32 "
                       "public/protected/local value");
  if (static_cast<bool>(bitstreamMember) !=
      static_cast<bool>(bitstreamVisibility))
    return emitOpError(
        "class bit-stream member and visibility metadata must be paired");
  Attribute modeAttribute = (*this)->getAttr(metadata::randomModeIndex);
  auto modeIndex = dyn_cast_or_null<IntegerAttr>(modeAttribute);
  if (modeAttribute && !modeIndex)
    return emitOpError("random mode index must be an integer attribute");
  if (modeIndex && (modeIndex.getValue().isNegative() ||
                    modeIndex.getValue().getActiveBits() > 6))
    return emitOpError(
        "random mode index exceeds the 64-property executable boundary");
  if (Attribute edge = (*this)->getAttr(metadata::randomObjectEdge)) {
    if (!isa<UnitAttr>(edge))
      return emitOpError("random object edge must be a unit attribute");
    if (!modeIndex || getIsStatic() || getIsWeak() ||
        !isa<ClassHandleType>(getType()))
      return emitOpError(
          "random object edge requires an indexed, strong instance "
          "class-handle field");
  }
  Attribute variableAttribute = (*this)->getAttr(metadata::randomVariableKind);
  auto variableKind =
      dyn_cast_or_null<RandomVariableKindAttr>(variableAttribute);
  if (variableAttribute && !variableKind)
    return emitOpError(
        "random variable kind must be a RandomVariableKind attribute");
  Attribute signedAttribute = (*this)->getAttr(metadata::randomVariableSigned);
  auto isSigned = dyn_cast_or_null<BoolAttr>(signedAttribute);
  Attribute keyAttribute = (*this)->getAttr(metadata::randomCycleKeyField);
  Attribute positionAttribute =
      (*this)->getAttr(metadata::randomCyclePositionField);
  auto key = dyn_cast_or_null<FlatSymbolRefAttr>(keyAttribute);
  auto position = dyn_cast_or_null<FlatSymbolRefAttr>(positionAttribute);
  if (signedAttribute && !isSigned)
    return emitOpError("random variable signedness must be a boolean");
  if ((keyAttribute && !key) || (positionAttribute && !position))
    return emitOpError("randc state fields must be flat symbol references");
  if (variableKind) {
    std::optional<unsigned> width = getPackedWidth(getType());
    if (!modeIndex || getIsStatic() || !width || *width == 0 ||
        (*this)->hasAttr(metadata::randomObjectEdge))
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
  } else if (signedAttribute || keyAttribute || positionAttribute) {
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
  auto key = dyn_cast_or_null<FlatSymbolRefAttr>(
      (*this)->getAttr(metadata::randomCycleKeyField));
  auto position = dyn_cast_or_null<FlatSymbolRefAttr>(
      (*this)->getAttr(metadata::randomCyclePositionField));
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
SimClassIsInstanceOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  if (!lookupClass(symbolTable, *this, getTargetAttr()))
    return emitOpError("references an unknown target class");
  return success();
}

LogicalResult
SimClassCastOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  auto source = getObject().getType();
  auto target = getResult().getType();
  SimClassDeclOp sourceClass =
      lookupClass(symbolTable, *this, source.getClassName());
  SimClassDeclOp targetClass =
      lookupClass(symbolTable, *this, target.getClassName());
  if (!sourceClass || !targetClass)
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
  if (getOwnerReference().getType().getElementType() != containerType)
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
  std::optional<uint64_t> span = getProvenanceSpan(array.getElementType());
  if (!span || getElementSpan() != *span || getElementSpan() == 0)
    return emitOpError("element span does not match the owner array layout");
  if (getTypeId() == 0 || getValueSize() == 0 || getAlignment() == 0 ||
      getValueSize() % getAlignment() != 0)
    return emitOpError("element metadata is invalid");
  if (getTraceOffsets().size() != getTraceKinds().size())
    return emitOpError("trace offset and kind inventories must match");
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

LogicalResult SimDesignOp::verifyRegions() {
  // One cached table for the whole design walk below. The uncached lookups
  // rescan this design's block per call, which is quadratic across classes.
  SymbolTableCollection symbolTable;
  if (auto precision = getTimePrecisionFsAttr();
      precision &&
      (precision.getValue().isNegative() || precision.getValue().isZero()))
    return emitOpError("time precision must be a positive femtosecond value");
  llvm::DenseSet<uint64_t> scopeIds, codeUnitIds, statementIds,
      statementSiteIds, storageIds, netIds, driverIds, portIds, connectionIds,
      covergroupIds, classIds, vpiAnchorIds, typespecIds, enumConstIds;
  llvm::DenseMap<uint64_t, SimScopeDeclOp> scopes;
  llvm::DenseMap<uint64_t, SimCodeUnitDeclOp> codeUnits;
  llvm::DenseMap<uint64_t, SimStatementDeclOp> statements;
  SmallVector<SimStatementDeclOp> statementInventory;
  SmallVector<SimStatementSiteDeclOp> statementSites;
  SmallVector<SimVPIStatementRelationDeclOp> statementRelations;
  SmallVector<SimVPIObjectAnchorOp> vpiAnchors;
  SmallVector<SimVPITypespecDeclOp> typespecs;
  SmallVector<SimVPIEnumConstDeclOp> enumConstants;
  llvm::DenseMap<uint64_t, Type> storageTypes, netTypes, driverTypes;
  llvm::DenseMap<uint64_t, SimNetDeclOp> nets;
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
    } else if (auto site = dyn_cast<SimStatementSiteDeclOp>(op)) {
      if (failed(addId(site.getIdAttr(), statementSiteIds, "statement-site")))
        return failure();
      statementSites.push_back(site);
    } else if (auto relation = dyn_cast<SimVPIStatementRelationDeclOp>(op)) {
      statementRelations.push_back(relation);
    } else if (auto anchor = dyn_cast<SimVPIObjectAnchorOp>(op)) {
      if (failed(addId(anchor.getInventoryIdAttr(), vpiAnchorIds,
                       "VPI object anchor")))
        return failure();
      vpiAnchors.push_back(anchor);
    } else if (auto typespec = dyn_cast<SimVPITypespecDeclOp>(op)) {
      if (failed(addId(typespec.getIdAttr(), typespecIds, "VPI typespec")))
        return failure();
      typespecs.push_back(typespec);
    } else if (auto enumConstant = dyn_cast<SimVPIEnumConstDeclOp>(op)) {
      if (failed(addId(enumConstant.getIdAttr(), enumConstIds,
                       "VPI enum constant")))
        return failure();
      enumConstants.push_back(enumConstant);
    } else if (auto storage = dyn_cast<SimStorageDeclOp>(op)) {
      if (failed(addId(storage.getIdAttr(), storageIds, "storage")))
        return failure();
      storageTypes[storage.getId()] = storage.getType();
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
    } else if (auto connection = dyn_cast<SimNetConnectDeclOp>(op)) {
      if (failed(
              addId(connection.getIdAttr(), connectionIds, "net connection")))
        return failure();
      netConnections.push_back(connection);
    } else if (auto covergroup = dyn_cast<SimCovergroupDeclOp>(op)) {
      if (failed(addId(covergroup.getIdAttr(), covergroupIds, "covergroup")))
        return failure();
    } else if (auto classDecl = dyn_cast<SimClassDeclOp>(op)) {
      if (failed(addId(classDecl.getIdAttr(), classIds, "class")))
        return failure();
      classes[classDecl.getSymName()] = classDecl;
    } else if (auto function = dyn_cast<SimFuncOp>(op)) {
      functions.push_back(function);
    }
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

  llvm::SmallDenseSet<Attribute, 8> classVPIIdentities;
  for (SimVPIObjectAnchorOp anchor : vpiAnchors) {
    VPIObjectBackingAttr backing = anchor.getBackingAttr();
    if (backing && backing.getKind() == VPIObjectBackingKind::Class &&
        backing.getSymbol())
      classVPIIdentities.insert(backing.getSymbol());
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
  for (Operation &op : getBody().front()) {
    VPITypeSemanticsAttr semantic;
    if (auto storage = dyn_cast<SimStorageDeclOp>(op))
      semantic = storage.getVpiTypeAttr();
    else if (auto net = dyn_cast<SimNetDeclOp>(op))
      semantic = net.getVpiTypeAttr();
    else if (auto port = dyn_cast<SimPortDeclOp>(op))
      semantic = port.getVpiTypeAttr();
    if (!semantic)
      continue;
    if (failed(verifyTypeReferences(&op, semantic)))
      return failure();
    if (semantic.getKind() == VPITypeKind::Enum &&
        !semantic.getTypedefAliases()) {
      auto identity =
          op.getAttrOfType<IntegerAttr>(metadata::vpiSourceTypeIdentity);
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
        if (source->second.getInterfaceType().has_value() !=
            (StringRef(sourceKind->apiName) == "vpiInterface"))
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
    case VPIObjectBackingKind::CodeUnit: {
      if (!codeUnitIds.count(backing.getId().getValue().getZExtValue()))
        return anchor.emitOpError("references an unknown backing code-unit ID");
      SimCodeUnitDeclOp codeUnit =
          codeUnits.lookup(backing.getId().getValue().getZExtValue());
      if (codeUnit.getInternalAttr() ||
          codeUnit->hasAttr("obelisk_sim.dpi_import") ||
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
  for (const auto &entry : anchorOrdinals)
    for (uint64_t ordinal = 0; ordinal != entry.second.size(); ++ordinal)
      if (!entry.second.contains(ordinal))
        return emitOpError(
            "VPI anchor ordinals must be dense under each lexical parent");

  for (SimVPIObjectAnchorOp anchor : vpiAnchors) {
    llvm::SmallPtrSet<Operation *, 8> path;
    for (SimVPIObjectAnchorOp cursor = anchor; cursor;) {
      if (!path.insert(cursor).second)
        return anchor.emitOpError("lexical parent relation contains a cycle");
      FlatSymbolRefAttr parent = cursor.getParentAttr();
      cursor = parent
                   ? symbolTable.lookupNearestSymbolFrom<SimVPIObjectAnchorOp>(
                         cursor, parent)
                   : SimVPIObjectAnchorOp{};
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
      if (classDecl.getIsInterface()) {
        llvm::SmallPtrSet<Operation *, 8> reached;
        SmallVector<SimClassDeclOp> pending;
        auto appendInterfaces = [&](SimClassDeclOp declaration) {
          if (ArrayAttr interfaces = declaration.getInterfacesAttr())
            for (Attribute attribute : interfaces) {
              auto reference = cast<FlatSymbolRefAttr>(attribute);
              auto found = classes.find(reference.getValue());
              if (found != classes.end() && found->second.getIsInterface())
                pending.push_back(found->second);
            }
        };
        appendInterfaces(classDecl);
        while (!pending.empty()) {
          SimClassDeclOp current = pending.pop_back_val();
          if (current == classDecl)
            return classDecl.emitOpError(
                "interface inheritance contains a cycle");
          if (reached.insert(current).second)
            appendInterfaces(current);
        }
      }
      llvm::SmallPtrSet<Operation *, 8> path;
      for (SimClassDeclOp current = classDecl; current;
           current = current.getBaseAttr() ? lookupClass(symbolTable, current,
                                                         current.getBaseAttr())
                                           : SimClassDeclOp{})
        if (!path.insert(current).second)
          return classDecl.emitOpError("class inheritance contains a cycle");
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
    } else if (auto typespec = dyn_cast<SimVPITypespecDeclOp>(op)) {
      if (!scopeIds.count(typespec.getScopeId()))
        return typespec.emitOpError("references an unknown scope ID");
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
    bool pendingClockedSamplePlan =
        static_cast<bool>(function->getAttrOfType<DictionaryAttr>(
            "obelisk_sim.clocked_sample_plan"));
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
    if (ArrayAttr bindings =
            function->getAttrOfType<ArrayAttr>(metadata::bindings))
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
      auto descriptor =
          function.getArgAttrOfType<IntegerAttr>(index, metadata::descriptorId);
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
              index, metadata::descriptorRootType);
          auto low = function.getArgAttrOfType<IntegerAttr>(
              index, metadata::descriptorLow);
          if (!rootType) {
            if (low ||
                function.getArgAttr(index, metadata::descriptorIndices) ||
                function.getArgAttr(index, metadata::descriptorAggregateType) ||
                function.getArgAttr(index, metadata::descriptorPackedLow))
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
              index, metadata::descriptorIndices);
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
              index, metadata::descriptorAggregateType);
          if ((indices && !aggregateType) ||
              (aggregateType && aggregateType.getValue() != selected))
            validView = false;

          auto packedLow = function.getArgAttrOfType<IntegerAttr>(
              index, metadata::descriptorPackedLow);
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
