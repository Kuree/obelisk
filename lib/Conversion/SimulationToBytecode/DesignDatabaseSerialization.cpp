//===- DesignDatabaseSerialization.cpp - Reflection image emitter -------===//
//
// Serialize the pointer-free runtime reflection database independently from
// executable bytecode instruction selection.
//
//===----------------------------------------------------------------------===//

#include "BytecodeSerialization.h"

#include "obelisk/Analysis/ClassDispatchAnalysis.h"
#include "obelisk/Analysis/NetConnectivityAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationVPI.h"
#include "obelisk/Reflection/DesignReflection.h"
#include "obelisk/Reflection/VPIObjectModel.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/IR/BuiltinOps.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringMap.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <string>
#include <tuple>

using namespace mlir;

namespace obelisk::bytecode {
namespace {

constexpr uint32_t kDatabaseProfileWrite = OBELISK_RT_DESIGN_PROFILE_WRITE;
constexpr uint32_t kDefinitionMemberRefObjectFlag = UINT32_C(1) << 31;
constexpr uint32_t kDefinitionMemberInstanceTargetFlag = UINT32_C(1) << 31;
constexpr uint32_t kVirtualBindingLimit = UINT32_C(1) << 30;
using namespace obelisk::reflection;
static_assert(RelationLayout.size == 16);
static_assert(SemanticRootBindingLayout.size == 8);
static_assert(FrozenValueLayout.size == 32);
static_assert(FrozenValueBindingLayout.size == 8);
static_assert(DefinitionLayout.size == 32);
static_assert(DefinitionBindingLayout.size == 16);
static_assert(DefinitionMemberLayout.size == 20);
static_assert(DefinitionMemberRelationLayout.size == 12);
static_assert(DefinitionMemberRelationTargetLayout.size == 4);
static_assert(DefinitionSpecializationLayout.size == 12);
static_assert(DefinitionSpecializationBindingLayout.size == 8);
static_assert(DefinitionMemberEndpointLayout.size == 4);
static_assert(DefinitionMemberInstanceRelationLayout.size == 20);
static_assert(DefinitionMemberInstanceRelationTargetLayout.size == 4);
static_assert(DefinitionMemberInstanceRelationInverseLayout.size == 4);
static_assert(static_cast<uint8_t>(TableKind::Scope) == 0);
static_assert(static_cast<uint8_t>(TableKind::Object) == 1);
static_assert(static_cast<uint8_t>(TableKind::Statement) == 2);
static_assert(static_cast<uint8_t>(TableKind::StaticObject) == 3);
static_assert(tableKindPackedWidth == 2);
static_assert(tableKindPayloadMask == 0x3fff);
static_assert(canPackTableKindPayload(tableKindPayloadMask));
static_assert(!canPackTableKindPayload(tableKindPayloadMask + 1));

constexpr bool tableKindPackingIsStable() {
  for (TableKind table : {TableKind::Scope, TableKind::Object,
                          TableKind::Statement, TableKind::StaticObject}) {
    uint16_t packed = 0;
    if (!tryPackTableKindPayload(table, tableKindPayloadMask, packed) ||
        unpackTableKind(packed) != table ||
        unpackTableKindPayload(packed) != tableKindPayloadMask)
      return false;
  }
  uint16_t rejected = 0;
  return !tryPackTableKindPayload(static_cast<TableKind>(4), 0, rejected) &&
         !tryPackTableKindPayload(TableKind::Scope, tableKindPayloadMask + 1,
                                  rejected);
}
static_assert(tableKindPackingIsStable());

static_assert(tableIndexPackedShift == 30);
static_assert(tableIndexPayloadMask == 0x3fffffff);
static_assert(canPackTableIndex(tableIndexPayloadMask));
static_assert(!canPackTableIndex(tableIndexPayloadMask + 1));

constexpr bool tableIndexPackingIsStable() {
  for (TableKind table : {TableKind::Scope, TableKind::Object,
                          TableKind::Statement, TableKind::StaticObject}) {
    uint32_t packed = 0;
    if (!tryPackTableIndex(table, tableIndexPayloadMask, packed) ||
        unpackTableIndexKind(packed) != table ||
        unpackTableIndex(packed) != tableIndexPayloadMask)
      return false;
  }
  uint32_t rejected = 0;
  return !tryPackTableIndex(static_cast<TableKind>(4), 0, rejected) &&
         !tryPackTableIndex(TableKind::Scope, tableIndexPayloadMask + 1,
                            rejected);
}
static_assert(tableIndexPackingIsStable());

bool declaredIndexOrdinal(int64_t left, int64_t right, int64_t index,
                          uint64_t &ordinal, uint64_t &extent) {
  if (left >= right) {
    if (index > left || index < right)
      return false;
    ordinal = static_cast<uint64_t>(left) - static_cast<uint64_t>(index);
    extent = static_cast<uint64_t>(left) - static_cast<uint64_t>(right) + 1;
  } else {
    if (index < left || index > right)
      return false;
    ordinal = static_cast<uint64_t>(index) - static_cast<uint64_t>(left);
    extent = static_cast<uint64_t>(right) - static_cast<uint64_t>(left) + 1;
  }
  return extent != 0 && ordinal < extent;
}

static_assert(relationSourceKindMask == 0x1fff);
constexpr bool relationSourcePackingIsStable() {
  for (TableKind table : {TableKind::Scope, TableKind::Object,
                          TableKind::Statement, TableKind::StaticObject}) {
    for (bool iterate : {false, true}) {
      uint16_t packed = 0;
      if (!tryPackRelationSource(table, relationSourceKindMask, iterate,
                                 packed) ||
          unpackRelationSourceTable(packed) != table ||
          unpackRelationSourceKind(packed) != relationSourceKindMask ||
          relationSourceIsIterate(packed) != iterate)
        return false;
    }
  }
  uint16_t rejected = 0;
  return !tryPackRelationSource(static_cast<TableKind>(4), 0, false,
                                rejected) &&
         !tryPackRelationSource(TableKind::Scope, relationSourceKindMask + 1,
                                false, rejected);
}
static_assert(relationSourcePackingIsStable());

} // namespace

SmallVector<uint8_t> serializeDesignDatabase(
    sim::SimDesignOp design, uint32_t profile, bool includeStatements,
    const llvm::DenseMap<uint64_t, uint64_t> &storageOffsets,
    const llvm::DenseMap<uint64_t, uint64_t> &netOffsets,
    const llvm::DenseMap<uint64_t, uint64_t> &driverOffsets) {
  SymbolTableCollection classMemberSymbols;
  struct Source {
    std::string file;
    uint64_t lineColumn = 0;
  };
  auto sourceForLocation = [](LocationAttr sourceLocation) {
    Source source;
    if (sourceLocation)
      if (auto location = sourceLocation.findInstanceOf<FileLineColLoc>()) {
        source.file = location.getFilename().getValue().str();
        source.lineColumn =
            uint64_t{location.getLine()} << 32 | uint64_t{location.getColumn()};
      }
    return source;
  };
  auto sourceFor = [&](Operation *operation) {
    return sourceForLocation(operation->getLoc());
  };
  auto hasFixedReflectionMetadata = [](Operation *operation) {
    return operation->hasAttr("is_protected") ||
           (operation->hasAttr("definition_loc") &&
            !isa<sim::SimVPIDefinitionDeclOp>(operation)) ||
           operation->hasAttr("vpi_properties");
  };
  auto sameFixedReflectionMetadata = [](Operation *left, Operation *right) {
    return left->getAttr("is_protected") == right->getAttr("is_protected") &&
           left->getAttr("definition_loc") ==
               right->getAttr("definition_loc") &&
           left->getAttr("vpi_properties") == right->getAttr("vpi_properties");
  };
  DenseSet<Operation *> explicitlyHandledFixedMetadata;
  struct Record {
    uint32_t kind;
    uint32_t vpiKind;
    uint32_t caps;
    uint64_t id;
    uint64_t scope;
    std::string name;
    Type type;
    uint64_t stateOffset;
    Source source;
    bool indexName = true;
    Operation *identity = nullptr;
  };
  struct StaticObjectRecord {
    uint64_t id = 0;
    uint64_t scope = 0;
    std::string name;
    uint32_t vpiKind = 0;
    uint16_t flags = 0;
    Source source;
    Operation *identity = nullptr;
  };
  struct StatementRecord {
    sim::SimStatementDeclOp declaration;
    Source source;
  };
  struct StatementSiteRecord {
    sim::SimStatementSiteDeclOp declaration;
  };
  struct RelationRecord {
    TableKind sourceTable;
    uint32_t sourceIndex;
    bool iterate;
    uint32_t targetIndexAndTable;
    uint32_t ordinal;
    uint16_t selector;
    uint16_t sourceKindAndTable;
  };
  struct FixedPropertyRecord {
    uint32_t sourceIndexAndTable = 0;
    uint16_t selector = 0;
    uint16_t kindAndFlags = 0;
    uint64_t payload = 0;
    bool optionalImage = false;
    std::string stringPayload;
  };
  struct ClassMemberBindingRecord {
    uint32_t member, owner;
    uint64_t classID, byteOffset;
    uint32_t storage, flags;
    uint64_t eventID;
    std::string name;
  };
  SmallVector<ClassMemberBindingRecord> classMemberBindings;
  struct ModportBindingRecord {
    uint32_t modport, scope;
    std::string name;
  };
  SmallVector<ModportBindingRecord> modportBindings;
  struct BoxTypeBindingRecord {
    uint64_t id;
    uint32_t semanticType, physicalType;
  };
  SmallVector<BoxTypeBindingRecord> boxTypeBindings;
  struct ValueElementBindingRecord {
    ValueLayoutOwnerKind ownerKind;
    uint32_t ownerIndex, semanticType, physicalType;
  };
  SmallVector<ValueElementBindingRecord> valueElementBindings;
  struct EventBindingRecord {
    uint32_t event;
    uint64_t id;
  };
  SmallVector<EventBindingRecord> eventBindings;
  struct TerminalDriverBindingRecord {
    uint32_t terminal, low, high, bit;
  };
  SmallVector<TerminalDriverBindingRecord> terminalDriverBindings;
  struct EventArrayStorageBindingRecord {
    uint32_t array = 0;
    uint32_t storage = 0;
  };
  SmallVector<EventArrayStorageBindingRecord> eventArrayStorageBindings;
  struct ThreadOriginBindingRecord {
    uint64_t codeUnit;
    uint32_t statement;
  };
  SmallVector<ThreadOriginBindingRecord> threadOriginBindings;
  SmallVector<sim::SimVPILocalDeclOp> localDeclarations;
  SmallVector<sim::SimVPIForeachDeclOp> foreachDeclarations;
  DenseMap<std::pair<Operation *, uint32_t>, uint64_t> foreachNullIDs;
  DenseMap<uint64_t, uint32_t> foreachNullIndices;
  DenseMap<uint64_t, sim::SimVPIStaticLocalBindingOp> staticLocalBindings;
  llvm::StringMap<sim::SimVPILocalDeclOp> localsBySymbol;
  struct FrameLocalBindingRecord {
    uint64_t id;
    uint32_t object, owner, scope, ordinal;
  };
  SmallVector<FrameLocalBindingRecord> frameLocalBindings;
  SmallVector<sim::SimVPICallDeclOp> callDeclarations;
  llvm::StringMap<sim::SimVPICallDeclOp> callRefsBySymbol;
  struct CallSiteBindingRecord {
    uint64_t id;
    uint32_t object, owner, flags;
  };
  SmallVector<CallSiteBindingRecord> callSiteBindings;
  struct CodeUnitScopeBindingRecord {
    uint64_t codeUnitID;
    uint32_t source, flags;
  };
  SmallVector<CodeUnitScopeBindingRecord> codeUnitScopeBindings;
  SmallVector<CodeUnitScopeBindingRecord> initializerBindings;
  SmallVector<sim::SimVPIClassCallDispatchOp> classCallDispatchDeclarations;
  struct CallDispatchBindingRecord {
    uint64_t callID, codeUnitID, receiverClassID, slot, signatureID;
  };
  struct ClassDispatchMethodBindingRecord {
    uint64_t classID, slot, signatureID;
    uint32_t codeUnitIndex, flags;
  };
  struct ClassDispatchParentBindingRecord {
    uint64_t classID, parentClassID;
    uint32_t kind, flags;
  };
  SmallVector<CallDispatchBindingRecord> callDispatchBindings;
  SmallVector<ClassDispatchMethodBindingRecord> classDispatchMethodBindings;
  SmallVector<ClassDispatchParentBindingRecord> classDispatchParentBindings;
  SmallVector<sim::SimVPIInterfaceCallDispatchOp> interfaceCallDeclarations;
  struct InterfaceCallableRecord {
    uint32_t nominal;
    StringRef name;
    uint16_t kind, flags;
    uint32_t signature;
  };
  struct InterfaceInstanceRecord {
    uint32_t scope, nominal;
  };
  struct InterfaceCallableBindingRecord {
    uint32_t member, scope, declaration, implementation;
  };
  struct InterfaceCallBindingRecord {
    uint64_t id;
    uint32_t member, access;
  };
  SmallVector<InterfaceCallableRecord> interfaceCallables;
  SmallVector<InterfaceInstanceRecord> interfaceInstances;
  SmallVector<InterfaceCallableBindingRecord> interfaceCallableBindings;
  SmallVector<InterfaceCallBindingRecord> interfaceCallBindings;
  struct CallableSignatureBindingRecord {
    uint32_t unit, signature, flags;
  };
  SmallVector<CallableSignatureBindingRecord> callableSignatureBindings;
  DenseSet<uint64_t> interfaceDeclarationIDs;
  struct FrozenValueRecord {
    uint32_t kindAndFlags = 0;
    uint64_t bitWidth = 0;
    uint64_t payloadOffset = 0;
    uint64_t payloadSize = 0;
  };
  struct FrozenValueBindingRecord {
    uint32_t sourceIndexAndTable = 0;
    uint32_t value = 0;
  };
  struct DefinitionRecord {
    sim::SimVPIDefinitionDeclOp declaration;
    Source source;
    uint32_t firstMember = 0;
    uint32_t memberCount = 0;
    uint32_t firstMemberRelation = 0;
    uint32_t memberRelationCount = 0;
  };
  struct DefinitionBindingRecord {
    uint32_t sourceIndexAndTable = 0;
    uint32_t definition = 0;
    uint32_t specialization = UINT32_MAX;
    uint32_t firstMemberEndpoint = 0;
  };
  struct DefinitionMemberRecord {
    sim::SimVPIDefinitionMemberDeclOp declaration;
    Source source;
    uint32_t definition = 0;
  };
  struct DefinitionMemberRelationRecord {
    uint16_t selector = 0;
    uint32_t firstTarget = 0;
    uint32_t targetCount = 0;
  };
  struct DefinitionSpecializationRecord {
    sim::SimVPIDefinitionSpecializationDeclOp declaration;
    uint32_t definition = 0;
    uint32_t firstBinding = 0;
    uint32_t bindingCount = 0;
  };
  struct DefinitionSpecializationBindingRecord {
    uint32_t member = 0;
    uint32_t semanticType = 0;
    sim::SimVPIDefinitionMemberSpecializationOp declaration;
  };
  struct DefinitionParameterValueRecord {
    uint32_t binding = 0;
    uint32_t value = 0;
  };
  struct DefinitionMemberEndpointRecord {
    uint32_t targetIndexAndTable = UINT32_MAX;
  };
  struct DefinitionMemberInstanceRelationRecord {
    uint32_t binding = 0;
    uint32_t memberAndFlags = 0;
    uint16_t selector = 0;
    uint16_t modeAndFlags = 0;
    uint32_t firstTarget = 0;
    uint32_t targetCount = 0;
  };
  struct DefinitionMemberInstanceRelationInverseRecord {
    uint32_t target = 0;
  };
  struct ExpressionRootRecord {
    uint32_t owner, templateIndex, firstLeaf;
    uint16_t selector, mode;
    uint32_t ordinal;
    Source source;
  };
  struct ExpressionTemplateRecord {
    uint32_t definition, firstNode, nodeCount, firstOperand, operandCount;
    uint32_t leafCount, firstLeafShape;
  };
  struct ExpressionNodeRecord {
    uint16_t kind, code;
    uint32_t semantic, firstOperand, operandCount;
    Source source;
    uint32_t frozen;
  };
  struct ExpressionLeafShapeRecord {
    uint32_t semantic;
    uint16_t kind;
    StringAttr identifier;
  };
  SmallVector<ExpressionRootRecord> expressionRoots;
  SmallVector<ExpressionTemplateRecord> expressionTemplates;
  SmallVector<ExpressionNodeRecord> expressionNodes;
  SmallVector<uint32_t> expressionOperands, expressionLeaves;
  SmallVector<std::pair<uint32_t, uint32_t>> expressionBoundMembers;
  SmallVector<ExpressionLeafShapeRecord> expressionLeafShapes;
  SmallVector<sim::SimVPIRelationDeclOp> expressionRootDeclarations;
  llvm::StringMap<sim::SimVPIExpressionDeclOp> expressionsBySymbol;
  DenseMap<Operation *, SmallVector<sim::SimVPIRelationDeclOp>>
      expressionChildren;
  struct ResolvedNetRunRecord {
    uint32_t objectIndex = 0;
    uint32_t netType = 0;
    uint64_t firstBit = 0;
    uint64_t bitCount = 0;
  };
  struct NetDelayRunRecord {
    uint32_t objectIndex = 0;
    uint64_t firstBit = 0;
    uint64_t bitCount = 0;
    std::array<int64_t, 3> delays{};
  };
  SmallVector<sim::SimScopeDeclOp> scopes;
  SmallVector<Record> objects;
  SmallVector<StaticObjectRecord> staticObjects;
  SmallVector<StatementRecord> statements;
  SmallVector<StatementSiteRecord> statementSites;
  SmallVector<sim::SimVPIStatementRelationDeclOp> relationDeclarations;
  SmallVector<sim::SimVPIRelationDeclOp> generalRelationDeclarations;
  SmallVector<sim::SimVPIObjectAnchorOp> anchors;
  SmallVector<sim::SimVPINettypeDeclOp> nettypes;
  SmallVector<sim::SimVPITypespecDeclOp> typespecs;
  SmallVector<sim::SimVPIUnresolvedTypeAliasDeclOp> unresolvedAliases;
  SmallVector<Operation *> sourceInheritance;
  llvm::StringMap<Operation *> sourceInheritanceBySymbol;
  SmallVector<sim::SimVPIClassMemberDeclOp> classMembers;
  DenseMap<Operation *, sim::SimClassFieldDeclOp> classMemberFields;
  DenseSet<uint64_t> classStaticStorage;
  DenseMap<Operation *, sim::SimVPIObjectAnchorOp> eventArrayStorage;
  DenseMap<Operation *, sim::VPITypeSemanticsAttr> eventArraySemantics;
  SmallVector<sim::SimVPIEnumConstDeclOp> enumConstants;
  SmallVector<sim::SimVPIDefinitionBindingOp> primitiveDefinitionBindings;
  SmallVector<sim::SimVPIDefinitionDeclOp> definitionDeclarations;
  SmallVector<sim::SimVPIDefinitionMemberDeclOp> definitionMemberDeclarations;
  SmallVector<sim::SimVPIDefinitionSpecializationDeclOp>
      definitionSpecializationDeclarations;
  SmallVector<sim::SimVPIDefinitionMemberSpecializationOp>
      definitionMemberSpecializations;
  SmallVector<sim::SimVPIDefinitionMemberInstanceBindingOp>
      definitionMemberInstanceBindings;
  SmallVector<sim::SimVPIDefinitionMemberInstanceRelationOp>
      definitionMemberInstanceRelationDeclarations;
  llvm::StringMap<sim::SimVPIDefinitionDeclOp> definitionsBySymbol;
  llvm::StringMap<sim::SimVPIDefinitionMemberDeclOp> definitionMembersBySymbol;
  llvm::StringMap<sim::SimVPIDefinitionSpecializationDeclOp>
      definitionSpecializationsBySymbol;
  llvm::StringMap<sim::SimVPIObjectAnchorOp> anchorsBySymbol;
  DenseMap<uint64_t, sim::SimVPIObjectAnchorOp> anchorsByInventoryId;
  llvm::StringMap<sim::SimVPINettypeDeclOp> nettypesBySymbol;
  llvm::StringMap<sim::SimVPITypespecDeclOp> typespecsBySymbol;
  llvm::StringMap<sim::SimVPIUnresolvedTypeAliasDeclOp>
      unresolvedAliasesBySymbol;
  llvm::StringMap<sim::SimVPIClassMemberDeclOp> classMembersBySymbol;
  DenseMap<uint64_t, sim::SimVPITypespecDeclOp> anonymousTypespecsByIdentity;
  SmallVector<RelationRecord> relations;
  SmallVector<FixedPropertyRecord> fixedProperties;
  SmallVector<FrozenValueRecord> frozenValues;
  SmallVector<FrozenValueBindingRecord> frozenValueBindings;
  SmallVector<uint8_t> frozenValuePayload;
  SmallVector<DefinitionRecord> definitions;
  SmallVector<DefinitionBindingRecord> definitionBindings;
  SmallVector<DefinitionMemberRecord> definitionMembers;
  SmallVector<DefinitionMemberRelationRecord> definitionMemberRelations;
  SmallVector<uint32_t> definitionMemberRelationTargets;
  SmallVector<DefinitionSpecializationRecord> definitionSpecializations;
  SmallVector<DefinitionSpecializationBindingRecord>
      definitionSpecializationBindings;
  SmallVector<DefinitionParameterValueRecord> definitionParameterValues;
  SmallVector<DefinitionMemberEndpointRecord> definitionMemberEndpoints;
  DenseMap<uint32_t, uint32_t> definitionMemberEndpointOrdinals;
  SmallVector<uint32_t> definitionEndpointCounts;
  SmallVector<DefinitionMemberInstanceRelationRecord>
      definitionMemberInstanceRelations;
  SmallVector<uint32_t> definitionMemberInstanceRelationTargets;
  SmallVector<DefinitionMemberInstanceRelationInverseRecord>
      definitionMemberInstanceRelationInverses;
  DenseMap<uint32_t, uint32_t> definitionForSource;
  SmallVector<ResolvedNetRunRecord> resolvedNetRuns;
  SmallVector<NetDelayRunRecord> netDelayRuns;
  auto fallbackName = [](StringRef kind, uint64_t id) {
    return (kind + "." + Twine(id)).str();
  };
  auto addPortMetadata = [](sim::SimPortDeclOp port, uint32_t caps) {
    if (port.getDirection() != sim::PortDirection::Output)
      caps |= OBELISK_RT_DESIGN_CAP_PORT_INPUT;
    if (port.getDirection() != sim::PortDirection::Input)
      caps |= OBELISK_RT_DESIGN_CAP_PORT_OUTPUT;
    caps |= static_cast<uint32_t>(port.getOrdinal())
            << OBELISK_RT_DESIGN_CAP_PORT_ORDINAL_SHIFT;
    return caps;
  };
  std::function<bool(Type)> isReflectableType = [&](Type type) {
    if (!simulationWidth(type))
      return false;
    // IEEE 1800-2023 37.33: a class variable is a VPI object whether or not
    // it references an object; its storage is the managed reference word.
    if (isa<IntegerType, sim::LogicType, sim::StringType, sim::ClassHandleType,
            sim::DynamicArrayType, sim::QueueType, sim::MailboxType,
            sim::SemaphoreType, sim::AssocArrayType, sim::ProcessType,
            sim::CovergroupHandleType, sim::VirtualInterfaceType, sim::EventType,
            sim::ChandleType>(type) ||
        type.isF32() || type.isF64())
      return true;
    if (auto array = dyn_cast<sim::PackedArrayType>(type))
      return isReflectableType(array.getElementType());
    if (auto array = dyn_cast<sim::UnpackedArrayType>(type))
      return isReflectableType(array.getElementType());
    ArrayAttr fields;
    if (auto aggregate = dyn_cast<sim::PackedStructType>(type))
      fields = aggregate.getFields();
    else if (auto aggregate = dyn_cast<sim::UnpackedStructType>(type))
      fields = aggregate.getFields();
    else if (auto aggregate = dyn_cast<sim::PackedUnionType>(type))
      fields = aggregate.getFields();
    else if (auto aggregate = dyn_cast<sim::UnpackedUnionType>(type))
      fields = aggregate.getFields();
    else
      return false;
    return llvm::all_of(fields, [&](Attribute attribute) {
      auto field = dyn_cast<sim::FieldAttr>(attribute);
      return field && isReflectableType(field.getType());
    });
  };
  struct PortSource {
    StringRef name;
    uint64_t scope;
    Type type;
  };
  struct PortConnection {
    uint64_t sourceID;
    bool sourceIsNet;
  };
  llvm::DenseMap<uint64_t, PortSource> storageSources, netSources,
      netIdentitySources;
  llvm::DenseMap<uint64_t, sim::SimPortDeclOp> directStoragePorts,
      directNetPorts, directNetIdentityPorts;
  llvm::DenseMap<uint64_t, uint64_t> declaredPortNets;
  llvm::DenseMap<uint64_t, uint64_t> netIdentityBacking;
  llvm::DenseMap<uint64_t, PortConnection> wholePortConnections;
  llvm::DenseSet<uint64_t> wholeStoragePortSources, wholeNetPortSources;
  for (Operation &operation : design.getBody().front()) {
    if (auto expression = dyn_cast<sim::SimVPIExpressionDeclOp>(operation)) {
      if (includeStatements)
        expressionsBySymbol[expression.getSymName()] = expression;
    } else if (auto definition =
                   dyn_cast<sim::SimVPIDefinitionDeclOp>(operation)) {
      if (includeStatements) {
        definitionDeclarations.push_back(definition);
        definitionsBySymbol[definition.getSymName()] = definition;
      }
    } else if (auto binding =
                   dyn_cast<sim::SimVPIDefinitionBindingOp>(operation)) {
      if (includeStatements)
        primitiveDefinitionBindings.push_back(binding);
    } else if (auto member =
                   dyn_cast<sim::SimVPIDefinitionMemberDeclOp>(operation)) {
      if (includeStatements) {
        definitionMemberDeclarations.push_back(member);
        definitionMembersBySymbol[member.getSymName()] = member;
      }
    } else if (auto specialization =
                   dyn_cast<sim::SimVPIDefinitionSpecializationDeclOp>(
                       operation)) {
      if (includeStatements) {
        definitionSpecializationDeclarations.push_back(specialization);
        definitionSpecializationsBySymbol[specialization.getSymName()] =
            specialization;
      }
    } else if (auto binding =
                   dyn_cast<sim::SimVPIDefinitionMemberSpecializationOp>(
                       operation)) {
      if (includeStatements)
        definitionMemberSpecializations.push_back(binding);
    } else if (auto binding =
                   dyn_cast<sim::SimVPIDefinitionMemberInstanceBindingOp>(
                       operation)) {
      if (includeStatements)
        definitionMemberInstanceBindings.push_back(binding);
    } else if (auto relation =
                   dyn_cast<sim::SimVPIDefinitionMemberInstanceRelationOp>(
                       operation)) {
      if (includeStatements)
        definitionMemberInstanceRelationDeclarations.push_back(relation);
    } else if (auto anchor = dyn_cast<sim::SimVPIObjectAnchorOp>(operation)) {
      if (includeStatements) {
        anchors.push_back(anchor);
        anchorsBySymbol[anchor.getSymName()] = anchor;
        anchorsByInventoryId[anchor.getInventoryId()] = anchor;
      }
    } else if (auto nettype = dyn_cast<sim::SimVPINettypeDeclOp>(operation)) {
      if (includeStatements) {
        nettypes.push_back(nettype);
        nettypesBySymbol[nettype.getSymName()] = nettype;
      }
    } else if (isa<sim::SimVPILexicalClassTypespecDeclOp,
                   sim::SimVPIClassExtendsDeclOp,
                   sim::SimVPIClassParameterAssignDeclOp>(operation)) {
      if (includeStatements) {
        sourceInheritance.push_back(&operation);
        sourceInheritanceBySymbol
            [cast<StringAttr>(operation.getAttr("sym_name")).getValue()] =
                &operation;
      }
    } else if (auto alias =
                   dyn_cast<sim::SimVPIUnresolvedTypeAliasDeclOp>(operation)) {
      if (includeStatements) {
        unresolvedAliases.push_back(alias);
        unresolvedAliasesBySymbol[alias.getSymName()] = alias;
      }
    } else if (auto typespec = dyn_cast<sim::SimVPITypespecDeclOp>(operation)) {
      if (includeStatements) {
        typespecs.push_back(typespec);
        typespecsBySymbol[typespec.getSymName()] = typespec;
        if (typespec.getOrigin() == sim::VPITypespecOrigin::AnonymousEnum) {
          auto identity = typespec.getSourceTypeIdentity();
          if (!identity) {
            typespec.emitOpError(
                "anonymous enum typespec has no source type identity");
            return {};
          }
          if (!anonymousTypespecsByIdentity.try_emplace(*identity, typespec)
                   .second) {
            typespec.emitOpError(
                "duplicate anonymous enum source type identity");
            return {};
          }
        }
      }
    } else if (auto member =
                   dyn_cast<sim::SimVPIClassMemberDeclOp>(operation)) {
      if (includeStatements) {
        classMembers.push_back(member);
        classMembersBySymbol[member.getSymName()] = member;
        if (member.getStaticStorage())
          classStaticStorage.insert(*member.getStaticStorage());
        if (member.getFieldAttr())
          classMemberFields[member] =
              classMemberSymbols
                  .lookupNearestSymbolFrom<sim::SimClassFieldDeclOp>(
                      member, member.getFieldAttr());
      }
    } else if (auto enumConstant =
                   dyn_cast<sim::SimVPIEnumConstDeclOp>(operation)) {
      if (includeStatements)
        enumConstants.push_back(enumConstant);
    } else if (auto identity =
                   dyn_cast<sim::SimVPINetIdentityDeclOp>(operation)) {
      netIdentitySources[identity.getId()] = {identity.getHierarchicalName(),
                                              identity.getScopeId(),
                                              identity.getType()};
      netIdentityBacking[identity.getId()] = identity.getBackingNetId();
    } else if (auto relation = dyn_cast<sim::SimVPIRelationDeclOp>(operation)) {
      if (relation.getSource().getKind() == sim::VPIObjectRefKind::Port &&
          relation.getTarget().getKind() ==
              sim::VPIObjectRefKind::NetIdentity &&
          relation.getSelector() ==
              static_cast<uint32_t>(VPIRelationKind::LowConnRel) &&
          relation.getMode() == sim::VPIRelationMode::Handle)
        declaredPortNets[relation.getSource().getId().getInt()] =
            relation.getTarget().getId().getInt();
    } else if (auto storage = dyn_cast<sim::SimStorageDeclOp>(operation)) {
      if (auto name = storage.getHierarchicalName())
        storageSources[storage.getId()] =
            PortSource{*name, storage.getScopeId(), storage.getType()};
      else if (includeStatements)
        storageSources[storage.getId()] =
            PortSource{{}, storage.getScopeId(), storage.getType()};
    } else if (auto net = dyn_cast<sim::SimNetDeclOp>(operation)) {
      if (auto name = net.getHierarchicalName())
        netSources[net.getId()] =
            PortSource{*name, net.getScopeId(), net.getType()};
      else if (includeStatements)
        netSources[net.getId()] =
            PortSource{{}, net.getScopeId(), net.getType()};
    }
  }
  for (sim::SimPortDeclOp port :
       design.getBody().front().getOps<sim::SimPortDeclOp>()) {
    auto declared = declaredPortNets.find(port.getId());
    if (declared != declaredPortNets.end()) {
      auto source = netIdentitySources.find(declared->second);
      if (!port.getSourceIsNet() || port.getSourceLow() != 0 ||
          source == netIdentitySources.end() ||
          netIdentityBacking.lookup(declared->second) != port.getSourceId() ||
          source->second.scope != port.getScopeId() ||
          source->second.type != port.getType()) {
        port.emitOpError("declared low connection disagrees with net backing");
        return {};
      }
      if (includeStatements)
        wholePortConnections.try_emplace(
            port.getId(), PortConnection{port.getSourceId(), true});
      if (source->second.name == port.getHierarchicalName())
        directNetIdentityPorts.try_emplace(declared->second, port);
      continue;
    }
    const auto &sources = port.getSourceIsNet() ? netSources : storageSources;
    auto source = sources.find(port.getSourceId());
    if (port.getSourceLow() != 0 || source == sources.end() ||
        source->second.scope != port.getScopeId() ||
        source->second.type != port.getType())
      continue;
    if (includeStatements) {
      (port.getSourceIsNet() ? wholeNetPortSources : wholeStoragePortSources)
          .insert(port.getSourceId());
      wholePortConnections.try_emplace(
          port.getId(),
          PortConnection{port.getSourceId(), port.getSourceIsNet()});
    }
    if (source->second.name != port.getHierarchicalName() ||
        source->second.name.empty())
      continue;
    auto &directPorts =
        port.getSourceIsNet() ? directNetPorts : directStoragePorts;
    directPorts.try_emplace(port.getSourceId(), port);
  }

  constexpr uint64_t maxStaticInventoryID = (UINT64_MAX - 3) / 4;
  auto staticInventoryID = [&](Operation *operation, uint64_t id,
                               uint64_t tag) -> std::optional<uint64_t> {
    if (id > maxStaticInventoryID) {
      operation->emitError("VPI static inventory ID cannot be encoded");
      return std::nullopt;
    }
    return id * 4 + tag;
  };

  for (Operation &operation : design.getBody().front()) {
    if (auto scope = dyn_cast<sim::SimScopeDeclOp>(operation))
      scopes.push_back(scope);
    else if (auto statement = dyn_cast<sim::SimStatementDeclOp>(operation)) {
      if (includeStatements)
        statements.push_back({statement, sourceFor(statement)});
    } else if (auto declaration =
                   dyn_cast<sim::SimVPIForeachDeclOp>(operation)) {
      if (includeStatements)
        foreachDeclarations.push_back(declaration);
    } else if (auto local = dyn_cast<sim::SimVPILocalDeclOp>(operation)) {
      if (includeStatements) {
        localDeclarations.push_back(local);
        localsBySymbol.try_emplace(local.getSymName(), local);
      }
    } else if (auto binding =
                   dyn_cast<sim::SimVPIStaticLocalBindingOp>(operation)) {
      if (includeStatements)
        staticLocalBindings[binding.getStorageId()] = binding;
    } else if (auto call = dyn_cast<sim::SimVPICallDeclOp>(operation)) {
      if (includeStatements)
        callDeclarations.push_back(call);
    } else if (auto dispatch =
                   dyn_cast<sim::SimVPIClassCallDispatchOp>(operation)) {
      if (includeStatements)
        classCallDispatchDeclarations.push_back(dispatch);
    } else if (auto dispatch =
                   dyn_cast<sim::SimVPIInterfaceCallDispatchOp>(operation)) {
      if (includeStatements)
        interfaceCallDeclarations.push_back(dispatch);
    } else if (auto site = dyn_cast<sim::SimStatementSiteDeclOp>(operation)) {
      if (includeStatements)
        statementSites.push_back({site});
    } else if (auto relation =
                   dyn_cast<sim::SimVPIStatementRelationDeclOp>(operation)) {
      if (includeStatements)
        relationDeclarations.push_back(relation);
    } else if (auto relation = dyn_cast<sim::SimVPIRelationDeclOp>(operation)) {
      if (includeStatements)
        generalRelationDeclarations.push_back(relation);
    } else if (auto identity =
                   dyn_cast<sim::SimVPINetIdentityDeclOp>(operation)) {
      if (!includeStatements)
        continue;
      if (identity.getId() > UINT64_MAX - netSources.size()) {
        identity.emitOpError("stable VPI net identity ID cannot be encoded");
        return {};
      }
      uint64_t stableID = netSources.size() + identity.getId();
      uint32_t caps = profile & kDatabaseProfileWrite ? 3u : 1u;
      if (auto port = directNetIdentityPorts.lookup(identity.getId()))
        caps = addPortMetadata(port, caps);
      objects.push_back(
          {OBELISK_RT_DESIGN_RECORD_NET,
           sim::vpiKindForNet(identity.getVpiTypeAttr()), caps, stableID,
           identity.getScopeId(), identity.getHierarchicalName().str(),
           identity.getType(), netOffsets.lookup(identity.getBackingNetId()),
           sourceFor(identity), true, identity});
    } else if (auto anchor = dyn_cast<sim::SimVPIObjectAnchorOp>(operation)) {
      if (!includeStatements)
        continue;
      sim::VPIObjectBackingAttr backing = anchor.getBackingAttr();
      if (backing && backing.getKind() != sim::VPIObjectBackingKind::Class)
        continue;
      auto id = staticInventoryID(anchor, anchor.getInventoryId(), 0);
      if (!id)
        return {};
      std::string name = anchor.getHierarchicalName().str();
      if (anchor.getVpiKind() ==
              static_cast<uint32_t>(VPIObjectKind::Package) &&
          !StringRef(name).ends_with("::"))
        name.append("::");
      const auto *indexed = findVPIIndexedAccess(anchor.getVpiKind());
      bool hasSemanticShape =
          anchor.getVpiKind() ==
              static_cast<uint32_t>(VPIObjectKind::NamedEvent) ||
          anchor.getIndexRangesAttr() || anchor.getSparseIndicesAttr() ||
          anchor.getMemberIndicesAttr() || anchor.getPrimitiveInputCountAttr();
      bool semanticIdentity = anchor.getVpiKind() ==
                              static_cast<uint32_t>(VPIObjectKind::ClassDefn);
      bool specializedOwner = anchor.getParentAttr() &&
          typespecsBySymbol.contains(anchor.getParentAttr().getValue());
      bool sourceExpression =
          anchor.getVpiKind() ==
              static_cast<uint32_t>(VPIObjectKind::Constant) ||
          anchor.getVpiKind() ==
              static_cast<uint32_t>(VPIObjectKind::Operation);
      bool primitiveTerminal =
          anchor.getVpiKind() == static_cast<uint32_t>(VPIObjectKind::PrimTerm);
      bool compact =
          !sourceExpression &&
          hasVPIObjectRepresentation(anchor.getVpiKind(),
                                     VPIObjectRepresentation::StaticImage) &&
          (primitiveTerminal || !findVPIValuePolicy(anchor.getVpiKind()) ||
           anchor.getImmutableValueAttr()) &&
          !indexed && !hasSemanticShape && !semanticIdentity &&
          !specializedOwner;
      uint16_t staticFlags =
          anchor.getHasExplicitParameterRangeAttr()
              ? static_cast<uint16_t>(
                    OBELISK_RT_DESIGN_CAP_PARAMETER_EXPLICIT_RANGE)
              : 0;
      if (anchor.getUnresolvedTypeParameterAttr())
        staticFlags |= OBELISK_RT_DESIGN_CAP_UNRESOLVED_TYPE_PARAMETER;
      if (anchor.getUnavailableDefaultAttr())
        staticFlags |= OBELISK_RT_DESIGN_CAP_UNAVAILABLE_PARAMETER_DEFAULT;
      if (anchor.getUnresolvedValueParameterAttr())
        staticFlags |= OBELISK_RT_DESIGN_CAP_UNRESOLVED_VALUE_PARAMETER;
      if (compact)
        staticObjects.push_back({*id, anchor.getEnclosingScopeId(),
                                 std::move(name), anchor.getVpiKind(),
                                 staticFlags, sourceFor(anchor), anchor});
      else
        objects.push_back(
            {OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT, anchor.getVpiKind(),
             staticFlags, *id, anchor.getEnclosingScopeId(), std::move(name),
             Type{}, 0, sourceFor(anchor),
             !specializedOwner && !sourceExpression && !primitiveTerminal,
             anchor});
    } else if (auto member =
                   dyn_cast<sim::SimVPIClassMemberDeclOp>(operation)) {
      if (!includeStatements)
        continue;
      auto field = classMemberFields.lookup(member);
      auto id = staticInventoryID(member, member.getId(), 0);
      if (!id || (member.getFieldAttr() && !field))
        return {};
      bool specialized = typespecsBySymbol.contains(member.getOwner());
      bool indexName =
          !field || (field.getIsStatic() ? specialized : !specialized);
      uint32_t flags = field ? 0 : OBELISK_RT_DESIGN_CAP_SOURCE_CLASS_PROPERTY;
      objects.push_back({OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT,
                         sim::vpiKindForClassMember(member.getVpiType()), flags,
                         *id, member.getScopeId(),
                         member.getHierarchicalName().str(),
                         field ? field.getType() : Type{}, 0, sourceFor(member),
                         indexName, member});
    } else if (auto nettype = dyn_cast<sim::SimVPINettypeDeclOp>(operation)) {
      if (!includeStatements)
        continue;
      auto id = staticInventoryID(nettype, nettype.getId(), 3);
      if (!id)
        return {};
      objects.push_back({OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT,
                         static_cast<uint16_t>(VPIObjectKind::NettypeDecl), 0,
                         *id, nettype.getScopeId(),
                         nettype.getHierarchicalName().str(), Type{}, 0,
                         sourceFor(nettype), true, nettype});
    } else if (isa<sim::SimVPILexicalClassTypespecDeclOp,
                   sim::SimVPIClassExtendsDeclOp,
                   sim::SimVPIClassParameterAssignDeclOp>(operation)) {
      if (!includeStatements)
        continue;
      auto type = dyn_cast<sim::SimVPILexicalClassTypespecDeclOp>(operation);
      auto clause = dyn_cast<sim::SimVPIClassExtendsDeclOp>(operation);
      auto assignment =
          dyn_cast<sim::SimVPIClassParameterAssignDeclOp>(operation);
      uint64_t ordinal = cast<IntegerAttr>(operation.getAttr("id")).getInt();
      auto id = staticInventoryID(&operation, ordinal, type ? 1 : 0);
      if (!id)
        return {};
      uint32_t kind =
          static_cast<uint32_t>(type     ? VPIObjectKind::ClassTypespec
                                : clause ? VPIObjectKind::Extends
                                         : VPIObjectKind::ParamAssign);
      uint32_t caps =
          type     ? OBELISK_RT_DESIGN_CAP_LEXICAL_CLASS_TYPESPEC
          : clause ? 0
                   : OBELISK_RT_DESIGN_CAP_DEFAULT_CLASS_PARAMETER_ASSIGNMENT;
      if (assignment &&
          (assignment.getRhsAttr() || assignment.getUnavailableRhsAttr()))
        caps = OBELISK_RT_DESIGN_CAP_EXPLICIT_CLASS_PARAMETER_ASSIGNMENT;
      if (assignment && assignment.getUnavailableRhsAttr())
        caps |= OBELISK_RT_DESIGN_CAP_UNAVAILABLE_CLASS_PARAMETER_RHS;
      if (type && type.getUnavailableParametersAttr())
        caps |= OBELISK_RT_DESIGN_CAP_UNAVAILABLE_CLASS_PARAMETERS;
      if (clause && clause.getUnavailableBaseAttr())
        caps |= OBELISK_RT_DESIGN_CAP_UNAVAILABLE_CLASS_BASE;
      if (clause && clause.getUnavailableArgumentsAttr())
        caps |= OBELISK_RT_DESIGN_CAP_UNAVAILABLE_CONSTRUCTOR_ARGUMENTS;
      objects.push_back(
          {OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT, kind, caps, *id,
           static_cast<uint64_t>(
               cast<IntegerAttr>(operation.getAttr("scope_id")).getInt()),
           type ? type.getDebugName().str() : std::string{}, Type{}, 0,
           sourceFor(&operation), false, &operation});
    } else if (auto alias =
                   dyn_cast<sim::SimVPIUnresolvedTypeAliasDeclOp>(operation)) {
      if (!includeStatements)
        continue;
      auto id = staticInventoryID(alias, alias.getId(), 1);
      if (!id)
        return {};
      objects.push_back({OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT,
                         static_cast<uint16_t>(VPIObjectKind::TypeParameter),
                         OBELISK_RT_DESIGN_CAP_UNRESOLVED_TYPE_ALIAS |
                             OBELISK_RT_DESIGN_CAP_NAMED_TYPESPEC,
                         *id, alias.getScopeId(),
                         alias.getHierarchicalName().str(), Type{}, 0,
                         sourceFor(alias), true, alias});
    } else if (auto typespec = dyn_cast<sim::SimVPITypespecDeclOp>(operation)) {
      if (!includeStatements)
        continue;
      auto id = staticInventoryID(typespec, typespec.getId(), 1);
      if (!id)
        return {};
      auto vpiKind = sim::vpiKindForTypespec(typespec.getTargetType());
      if (!vpiKind) {
        typespec.emitOpError(
            "source type has no concrete IEEE VPI typespec object");
        return {};
      }
      uint32_t caps =
          typespec.getOrigin() == sim::VPITypespecOrigin::Typedef
              ? static_cast<uint32_t>(OBELISK_RT_DESIGN_CAP_NAMED_TYPESPEC)
              : 0;
      objects.push_back(
          {OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT, *vpiKind, caps, *id,
           typespec.getScopeId(), typespec.getHierarchicalName().str(), Type{},
           0, sourceFor(typespec),
           typespec.getOrigin() == sim::VPITypespecOrigin::Typedef &&
               !typespec.getStatementScopeIdAttr(),
           typespec});
    } else if (auto enumConstant =
                   dyn_cast<sim::SimVPIEnumConstDeclOp>(operation)) {
      if (!includeStatements)
        continue;
      auto owner =
          typespecsBySymbol.find(enumConstant.getEnumTypespecAttr().getValue());
      if (owner == typespecsBySymbol.end()) {
        enumConstant.emitOpError(
            "owning VPI enum typespec was not preserved for serialization");
        return {};
      }
      auto id = staticInventoryID(enumConstant, enumConstant.getId(), 2);
      if (!id)
        return {};
      std::string name =
          (owner->second.getHierarchicalName() + "::" + enumConstant.getName())
              .str();
      objects.push_back({OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT,
                         static_cast<uint16_t>(VPIObjectKind::EnumConst), 0,
                         *id, owner->second.getScopeId(), std::move(name),
                         Type{}, 0, sourceFor(enumConstant),
                         !owner->second.getStatementScopeIdAttr(),
                         enumConstant});
    } else if (auto storage = dyn_cast<sim::SimStorageDeclOp>(operation)) {
      if (!isReflectableType(storage.getType()) &&
          !classStaticStorage.contains(storage.getId())) {
        if (includeStatements && hasFixedReflectionMetadata(storage)) {
          storage.emitOpError(
              "fixed VPI metadata would be dropped with an unsupported "
              "physical type");
          return {};
        }
        continue;
      }
      if (FlatSymbolRefAttr reference =
              sim::get<sim::metadata::VpiIdentityDelegated>(storage);
          reference && !classStaticStorage.contains(storage.getId())) {
        auto owner = anchorsBySymbol.find(reference.getValue());
        if (includeStatements &&
            (owner == anchorsBySymbol.end() ||
             owner->second.getVpiKind() !=
                 static_cast<uint32_t>(VPIObjectKind::NamedEventArray) ||
             owner->second.getEnclosingScopeId() != storage.getScopeId() ||
             !storage.getHierarchicalName() ||
             owner->second.getHierarchicalName() !=
                 *storage.getHierarchicalName())) {
          storage.emitOpError(
              "delegated VPI identity does not match a named-event-array "
              "anchor");
          return {};
        }
        if (includeStatements && hasFixedReflectionMetadata(storage) &&
            (!reference || owner == anchorsBySymbol.end() ||
             !sameFixedReflectionMetadata(storage, owner->second))) {
          storage.emitOpError(
              "delegated VPI metadata must exactly match its object anchor");
          return {};
        }
        if (includeStatements && hasFixedReflectionMetadata(storage))
          explicitlyHandledFixedMetadata.insert(storage);
        if (!includeStatements)
          continue;
        eventArrayStorage[storage] = owner->second;
        eventArraySemantics[owner->second] = storage.getVpiTypeAttr();
      }
      uint32_t caps = profile & kDatabaseProfileWrite ? 3u : 1u;
      if (sim::SimPortDeclOp port = directStoragePorts.lookup(storage.getId()))
        caps = addPortMetadata(port, caps);
      objects.push_back({2, sim::vpiKindForStorage(storage.getVpiTypeAttr()),
                         caps, storage.getId(), storage.getScopeId(),
                         storage.getHierarchicalName()
                             .value_or(storage.getDebugName().value_or(
                                 fallbackName("storage", storage.getId())))
                             .str(),
                         storage.getType(),
                         storageOffsets.lookup(storage.getId()),
                         sourceFor(storage), true, storage});
    } else if (auto net = dyn_cast<sim::SimNetDeclOp>(operation)) {
      if (!isReflectableType(net.getType())) {
        if (includeStatements && hasFixedReflectionMetadata(net)) {
          net.emitOpError(
              "fixed VPI metadata would be dropped with an unsupported "
              "physical type");
          return {};
        }
        continue;
      }
      uint32_t caps = profile & kDatabaseProfileWrite ? 3u : 1u;
      if (sim::SimPortDeclOp port = directNetPorts.lookup(net.getId()))
        caps = addPortMetadata(port, caps);
      objects.push_back({3, sim::vpiKindForNet(net.getVpiTypeAttr()), caps,
                         net.getId(), net.getScopeId(),
                         net.getHierarchicalName()
                             .value_or(net.getDebugName().value_or(
                                 fallbackName("net", net.getId())))
                             .str(),
                         net.getType(), netOffsets.lookup(net.getId()),
                         sourceFor(net), true, net});
    } else if (auto driver = dyn_cast<sim::SimDriverDeclOp>(operation)) {
      if (!isReflectableType(driver.getType()))
        continue;
      objects.push_back({4, 0, profile & kDatabaseProfileWrite ? 3u : 1u,
                         driver.getId(), driver.getScopeId(),
                         (driver.getHierarchicalName().value_or(
                              driver.getDebugName().value_or(
                                  fallbackName("driver", driver.getId()))) +
                          ".$driver." + Twine(driver.getId()))
                             .str(),
                         driver.getType(), driverOffsets.lookup(driver.getId()),
                         sourceFor(driver), true, driver});
    } else if (auto port = dyn_cast<sim::SimPortDeclOp>(operation)) {
      if (!isReflectableType(port.getType())) {
        if (includeStatements && hasFixedReflectionMetadata(port)) {
          port.emitOpError(
              "fixed VPI metadata would be dropped with an unsupported "
              "physical type");
          return {};
        }
        continue;
      }
      sim::SimPortDeclOp direct =
          (port.getSourceIsNet() ? directNetPorts : directStoragePorts)
              .lookup(port.getSourceId());
      if (auto declared = declaredPortNets.find(port.getId());
          declared != declaredPortNets.end())
        direct = directNetIdentityPorts.lookup(declared->second);
      if (direct == port && !includeStatements)
        continue;
      uint32_t caps = OBELISK_RT_DESIGN_CAP_READ;
      caps = addPortMetadata(port, caps);
      bool wholeSource = wholePortConnections.count(port.getId()) != 0;
      if (wholeSource)
        caps |= OBELISK_RT_DESIGN_CAP_PORT_WHOLE_SOURCE;
      uint64_t sourceOffset = port.getSourceIsNet()
                                  ? netOffsets.lookup(port.getSourceId())
                                  : storageOffsets.lookup(port.getSourceId());
      objects.push_back({OBELISK_RT_DESIGN_RECORD_PORT,
                         static_cast<uint16_t>(VPIObjectKind::Port), caps,
                         port.getId(), port.getScopeId(),
                         port.getHierarchicalName().str(), port.getType(),
                         sourceOffset + port.getSourceLow(), sourceFor(port),
                         direct != port, port});
    } else if (auto codeUnit = dyn_cast<sim::SimCodeUnitDeclOp>(operation)) {
      bool internal = !sim::isVPIVisibleCodeUnit(codeUnit);
      objects.push_back(
          {(codeUnit.getCodeUnitKind() == sim::EntryKind::Function ||
            codeUnit.getCodeUnitKind() == sim::EntryKind::Observer)
               ? 7u
               : 5u,
           sim::vpiKindForCodeUnit(codeUnit),
           internal ? static_cast<uint32_t>(OBELISK_RT_DESIGN_CAP_INTERNAL) : 0,
           codeUnit.getId(), codeUnit.getScopeId(),
           codeUnit.getHierarchicalName().str(), Type{}, 0, sourceFor(codeUnit),
           !internal, codeUnit});
    }
  }
  if (scopes.empty())
    return {};
  // A nested call is an expression occurrence, not an invented statement.
  // Keep it unindexed and distinct even when spelling and location coincide.
  llvm::sort(callDeclarations, [](auto left, auto right) {
    return left.getId() < right.getId();
  });
  uint64_t nextCallObjectID = 0;
  for (const Record &object : objects)
    if (object.kind == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT)
      nextCallObjectID = std::max(nextCallObjectID, object.id);
  for (const StaticObjectRecord &object : staticObjects)
    nextCallObjectID = std::max(nextCallObjectID, object.id);
  llvm::sort(localDeclarations, [](auto left, auto right) {
    return left.getId() < right.getId();
  });
  for (sim::SimVPILocalDeclOp local : localDeclarations) {
    if (nextCallObjectID == UINT64_MAX) {
      local.emitOpError("local declaration object identity cannot be encoded");
      return {};
    }
    objects.push_back({OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT,
                       sim::vpiKindForClassMember(local.getVpiType()),
                       OBELISK_RT_DESIGN_CAP_FRAME_LOCAL, ++nextCallObjectID,
                       local.getScopeId(), local.getHierarchicalName().str(),
                       local.getValueType(), 0, sourceFor(local), false,
                       local});
  }
  DenseSet<StringRef> classDispatchCalls;
  for (auto dispatch : classCallDispatchDeclarations)
    classDispatchCalls.insert(dispatch.getCall());
  DenseSet<StringRef> interfaceDispatchCalls;
  for (auto dispatch : interfaceCallDeclarations)
    interfaceDispatchCalls.insert(dispatch.getCall());
  for (sim::SimVPICallDeclOp call : callDeclarations) {
    if (call.getStatementId())
      continue;
    if (nextCallObjectID == UINT64_MAX) {
      call.emitOpError("call occurrence object identity cannot be encoded");
      return {};
    }
    objects.push_back({OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT,
                       static_cast<uint32_t>(call.getVpiKind()), 0,
                       ++nextCallObjectID, call.getScopeId(),
                       call.getName().str(), Type{}, 0, sourceFor(call), false,
                       call});
  }
  llvm::sort(foreachDeclarations, [](auto left, auto right) {
    return left.getStatementId() < right.getStatementId();
  });
  for (auto declaration : foreachDeclarations) {
    auto statement = llvm::find_if(statements, [&](StatementRecord record) {
      return record.declaration.getId() == declaration.getStatementId();
    });
    if (statement == statements.end()) {
      declaration.emitOpError("foreach statement was not serialized");
      return {};
    }
    for (auto [ordinal, index] :
         llvm::enumerate(declaration.getLoopVariables())) {
      if (!isa<UnitAttr>(index))
        continue;
      if (nextCallObjectID == UINT64_MAX) {
        declaration.emitOpError("foreach skip identity cannot be encoded");
        return {};
      }
      uint64_t id = ++nextCallObjectID;
      foreachNullIDs[{declaration, ordinal}] = id;
      staticObjects.push_back({id, statement->declaration.getScopeId(), "",
                               static_cast<uint32_t>(VPIObjectKind::Operation),
                               0, sourceFor(declaration), declaration});
    }
  }
  // Direct lookup of a static property denotes its class declaration view.
  // Its backing storage is implementation identity, not a second VPI object.
  for (Record &object : objects)
    if (auto storage =
            dyn_cast_if_present<sim::SimStorageDeclOp>(object.identity);
        storage && (classStaticStorage.contains(storage.getId()) ||
                    eventArrayStorage.contains(storage))) {
      object.indexName = false;
      object.caps |= OBELISK_RT_DESIGN_CAP_LEXICAL_ANCHOR;
    }
  for (Record &object : objects)
    if (auto storage =
            dyn_cast_if_present<sim::SimStorageDeclOp>(object.identity);
        storage && staticLocalBindings.contains(storage.getId()))
      object.caps |= OBELISK_RT_DESIGN_CAP_LEXICAL_ANCHOR;
  // The compact format uses Name != 0 as its global-lookup root. Preserve
  // ambiguous static names in the full object table, where the existing
  // relation-only representation can retain a display name without indexing
  // it.
  llvm::StringMap<uint32_t> staticNameCounts;
  for (const Record &object : objects)
    if (object.kind == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT)
      ++staticNameCounts[object.name];
  for (const StaticObjectRecord &object : staticObjects)
    ++staticNameCounts[object.name];
  SmallVector<StaticObjectRecord> uniqueStaticObjects;
  uniqueStaticObjects.reserve(staticObjects.size());
  for (StaticObjectRecord &object : staticObjects) {
    if (object.name.empty() || staticNameCounts.lookup(object.name) == 1) {
      uniqueStaticObjects.push_back(std::move(object));
      continue;
    }
    objects.push_back({OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT, object.vpiKind,
                       object.flags, object.id, object.scope,
                       std::move(object.name), Type{}, 0,
                       std::move(object.source), true, object.identity});
  }
  staticObjects = std::move(uniqueStaticObjects);
  llvm::sort(scopes, [](auto left, auto right) {
    return left.getId() < right.getId();
  });
  llvm::sort(objects, [](const Record &left, const Record &right) {
    return std::tie(left.scope, left.name, left.kind, left.id) <
           std::tie(right.scope, right.name, right.kind, right.id);
  });
  llvm::sort(staticObjects, [](const StaticObjectRecord &left,
                               const StaticObjectRecord &right) {
    return left.id < right.id;
  });
  for (size_t index = 1; index < staticObjects.size(); ++index)
    if (staticObjects[index - 1].id == staticObjects[index].id) {
      staticObjects[index].identity->emitError(
          "duplicate compact VPI static object ID");
      return {};
    }
  llvm::StringMap<uint32_t> firstStaticName;
  for (auto [index, object] : llvm::enumerate(objects)) {
    if (object.kind != OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT ||
        !object.indexName)
      continue;
    auto [entry, inserted] =
        firstStaticName.try_emplace(object.name, static_cast<uint32_t>(index));
    if (inserted)
      continue;
    objects[entry->second].indexName = false;
    object.indexName = false;
  }
  llvm::sort(statements, [](StatementRecord left, StatementRecord right) {
    return left.declaration.getId() < right.declaration.getId();
  });
  llvm::sort(statementSites,
             [](StatementSiteRecord left, StatementSiteRecord right) {
               return left.declaration.getId() < right.declaration.getId();
             });
  if (scopes.size() > UINT32_MAX || objects.size() > UINT32_MAX ||
      staticObjects.size() > UINT32_MAX || statements.size() > UINT32_MAX ||
      statementSites.size() > UINT32_MAX) {
    design.emitOpError("reflection table exceeds 32-bit compact indices");
    return {};
  }
  DenseSet<uint64_t> scopeIDs;
  sim::SimScopeDeclOp root;
  for (auto scope : scopes) {
    if (!scopeIDs.insert(scope.getId()).second) {
      scope.emitOpError("duplicate scope ID in reflection database");
      return {};
    }
    if (!scope.getParent()) {
      if (root) {
        scope.emitOpError(
            "reflection database requires exactly one root scope");
        return {};
      }
      root = scope;
    }
  }
  if (!root) {
    design.emitOpError("reflection database requires a root scope");
    return {};
  }
  for (auto scope : scopes)
    if (scope.getParent() && !scopeIDs.contains(*scope.getParent())) {
      scope.emitOpError("reflection parent scope does not exist");
      return {};
    }
  for (const Record &object : objects)
    if (!scopeIDs.contains(object.scope)) {
      design.emitOpError("reflection object references an unknown scope");
      return {};
    }
  for (const StaticObjectRecord &object : staticObjects)
    if (!scopeIDs.contains(object.scope)) {
      object.identity->emitError(
          "compact VPI object references an unknown scope");
      return {};
    }

  DenseMap<uint64_t, uint32_t> scopeIndices, codeUnitObjectIndices,
      statementIndices, statementKinds;
  DenseMap<Operation *, uint32_t> objectIndices, staticObjectIndices;
  DenseMap<Operation *, uint32_t> definitionIndices, definitionMemberIndices,
      definitionSpecializationIndices;
  DenseMap<uint64_t, uint32_t> canonicalStorageTargetIndices,
      canonicalNetTargetIndices, physicalNetObjectIndices,
      physicalStorageObjectIndices;
  for (auto [index, scope] : llvm::enumerate(scopes))
    scopeIndices[scope.getId()] = static_cast<uint32_t>(index);
  for (auto [index, object] : llvm::enumerate(objects))
    if (object.identity)
      objectIndices[object.identity] = static_cast<uint32_t>(index);

  auto automaticEdges = [](uint32_t sourceKind) {
    auto first = std::lower_bound(
        std::begin(vpiTraversals), std::end(vpiTraversals), sourceKind,
        [](const VPITraversalDescriptor &edge, uint32_t kind) {
          return edge.sourceType < kind;
        });
    auto last = first;
    while (last != std::end(vpiTraversals) && last->sourceType == sourceKind)
      ++last;
    return std::make_pair(first, last);
  };

  if (includeStatements) {
    DenseSet<Operation *> referencedDefinitions;
    for (sim::SimScopeDeclOp scope : scopes) {
      FlatSymbolRefAttr reference = scope.getVpiDefinitionAttr();
      if (!reference)
        continue;
      auto found = definitionsBySymbol.find(reference.getValue());
      if (found == definitionsBySymbol.end()) {
        scope.emitOpError(
            "references a VPI definition that was not serialized");
        return {};
      }
      referencedDefinitions.insert(found->second);
    }
    for (auto binding : primitiveDefinitionBindings) {
      auto found = definitionsBySymbol.find(binding.getDefinition());
      if (found == definitionsBySymbol.end()) {
        binding.emitOpError(
            "references a VPI definition that was not serialized");
        return {};
      }
      referencedDefinitions.insert(found->second);
    }
    llvm::sort(definitionDeclarations, [](auto left, auto right) {
      return left.getSymName() < right.getSymName();
    });
    for (sim::SimVPIDefinitionDeclOp definition : definitionDeclarations) {
      if (!referencedDefinitions.contains(definition))
        continue;
      if (definitions.size() == UINT32_MAX) {
        definition.emitOpError("VPI definition table exceeds 32-bit indices");
        return {};
      }
      Source source = sourceForLocation(definition.getDefinitionLocAttr());
      if (definition.getDefinitionName().contains('\0')) {
        definition.emitOpError(
            "source definition name contains an embedded NUL");
        return {};
      }
      if (StringRef(source.file).contains('\0')) {
        definition.emitOpError(
            "source definition filename contains an embedded NUL");
        return {};
      }
      if (source.file.empty() && source.lineColumn != 0) {
        definition.emitOpError(
            "source definition location requires a filename");
        return {};
      }
      if ((source.lineColumn >> 32) > INT32_MAX) {
        definition.emitOpError(
            "definition line exceeds the VPI 32-bit integer range");
        return {};
      }
      definitionIndices[definition] = static_cast<uint32_t>(definitions.size());
      definitions.push_back({definition, std::move(source), 0, 0, 0, 0});
    }

    llvm::sort(definitionMemberDeclarations, [&](auto left, auto right) {
      auto leftDefinition =
          definitionsBySymbol.find(left.getDefinitionAttr().getValue());
      auto rightDefinition =
          definitionsBySymbol.find(right.getDefinitionAttr().getValue());
      uint32_t leftIndex =
          leftDefinition == definitionsBySymbol.end()
              ? UINT32_MAX
              : definitionIndices.lookup(leftDefinition->second);
      uint32_t rightIndex =
          rightDefinition == definitionsBySymbol.end()
              ? UINT32_MAX
              : definitionIndices.lookup(rightDefinition->second);
      return std::make_tuple(leftIndex, left.getOrdinal()) <
             std::make_tuple(rightIndex, right.getOrdinal());
    });
    for (sim::SimVPIDefinitionMemberDeclOp member :
         definitionMemberDeclarations) {
      auto definition =
          definitionsBySymbol.find(member.getDefinitionAttr().getValue());
      if (definition == definitionsBySymbol.end() ||
          !definitionIndices.count(definition->second) ||
          definitionMembers.size() == UINT32_MAX) {
        member.emitOpError("VPI definition member cannot be encoded");
        return {};
      }
      uint32_t definitionIndex = definitionIndices.lookup(definition->second);
      DefinitionRecord &owner = definitions[definitionIndex];
      if (owner.memberCount == 0)
        owner.firstMember = static_cast<uint32_t>(definitionMembers.size());
      if (member.getOrdinal() != owner.memberCount) {
        member.emitOpError(
            "VPI definition member ordinals must be dense and unique");
        return {};
      }
      Source source = sourceFor(member);
      if (member.getMemberName().contains('\0') ||
          StringRef(source.file).contains('\0') ||
          (source.file.empty() && source.lineColumn != 0) ||
          (source.lineColumn >> 32) > INT32_MAX) {
        member.emitOpError("VPI definition member source cannot be encoded");
        return {};
      }
      definitionMemberIndices[member] =
          static_cast<uint32_t>(definitionMembers.size());
      definitionMembers.push_back({member, std::move(source), definitionIndex});
      ++owner.memberCount;
    }

    definitionEndpointCounts.resize(definitions.size());
    for (auto [index, member] : llvm::enumerate(definitionMembers))
      if (member.declaration.getVpiKind() ==
          static_cast<uint32_t>(VPIObjectKind::IODecl))
        definitionMemberEndpointOrdinals[index] =
            definitionEndpointCounts[member.definition]++;

    // Build one compact target range for each generated definition-member
    // traversal. The ranges and target lists are shared by every instance.
    for (auto [definitionIndex, definition] : llvm::enumerate(definitions)) {
      definition.firstMemberRelation =
          static_cast<uint32_t>(definitionMemberRelations.size());
      auto [edgeFirst, edgeLast] = automaticEdges(
          static_cast<uint32_t>(definition.declaration.getVpiKind()));
      for (auto edge = edgeFirst; edge != edgeLast; ++edge) {
        bool parameters =
            edge->automaticRelation == VPIAutomaticRelation::DirectChild &&
            edge->mode == VPITraversalMode::Iterate &&
            (edge->selector ==
                 static_cast<uint32_t>(VPIObjectKind::Parameter) ||
             edge->selector ==
                 static_cast<uint32_t>(VPIObjectKind::TypeParameter));
        if (edge->automaticRelation != VPIAutomaticRelation::DefinitionMember &&
            !parameters)
          continue;
        uint32_t firstTarget =
            static_cast<uint32_t>(definitionMemberRelationTargets.size());
        for (uint32_t ordinal = 0; ordinal != definition.memberCount;
             ++ordinal) {
          uint32_t memberIndex = definition.firstMember + ordinal;
          if (vpiObjectSetContains(
                  edge->targets,
                  definitionMembers[memberIndex].declaration.getVpiKind())) {
            if (definitionMemberRelationTargets.size() >=
                kDefinitionMemberInstanceTargetFlag) {
              definition.declaration.emitOpError(
                  "VPI definition-member target table exceeds its compact "
                  "token encoding");
              return {};
            }
            definitionMemberRelationTargets.push_back(memberIndex);
          }
        }
        uint32_t count =
            static_cast<uint32_t>(definitionMemberRelationTargets.size()) -
            firstTarget;
        if (count == 0)
          continue;
        definitionMemberRelations.push_back(
            {static_cast<uint16_t>(edge->selector), firstTarget, count});
        ++definition.memberRelationCount;
      }
    }

    llvm::sort(definitionSpecializationDeclarations, [](auto left, auto right) {
      return left.getSymName() < right.getSymName();
    });
    for (sim::SimVPIDefinitionSpecializationDeclOp specialization :
         definitionSpecializationDeclarations) {
      auto definition = definitionsBySymbol.find(
          specialization.getDefinitionAttr().getValue());
      if (definition == definitionsBySymbol.end() ||
          !definitionIndices.count(definition->second) ||
          definitionSpecializations.size() == UINT32_MAX) {
        specialization.emitOpError(
            "VPI definition specialization cannot be encoded");
        return {};
      }
      definitionSpecializationIndices[specialization] =
          static_cast<uint32_t>(definitionSpecializations.size());
      definitionSpecializations.push_back(
          {specialization, definitionIndices.lookup(definition->second), 0, 0});
    }
    for (auto [scopeIndex, scope] : llvm::enumerate(scopes)) {
      FlatSymbolRefAttr reference = scope.getVpiDefinitionAttr();
      if (!reference)
        continue;
      auto found = definitionsBySymbol.find(reference.getValue());
      uint32_t packedSource = 0;
      if (definitionBindings.size() >= kVirtualBindingLimit ||
          found == definitionsBySymbol.end() ||
          !tryPackTableIndex(TableKind::Scope,
                             static_cast<uint32_t>(scopeIndex), packedSource)) {
        scope.emitOpError("VPI definition binding cannot be encoded");
        return {};
      }
      uint32_t specializationIndex = UINT32_MAX;
      if (FlatSymbolRefAttr specialization = scope.getVpiSpecializationAttr()) {
        auto specialized =
            definitionSpecializationsBySymbol.find(specialization.getValue());
        if (specialized == definitionSpecializationsBySymbol.end() ||
            !definitionSpecializationIndices.count(specialized->second)) {
          scope.emitOpError("VPI definition specialization cannot be encoded");
          return {};
        }
        specializationIndex =
            definitionSpecializationIndices.lookup(specialized->second);
        if (definitionSpecializations[specializationIndex].definition !=
            definitionIndices.lookup(found->second)) {
          scope.emitOpError("VPI specialization belongs to another definition");
          return {};
        }
      }
      definitionBindings.push_back({packedSource,
                                    definitionIndices.lookup(found->second),
                                    specializationIndex, 0});
      definitionForSource[packedSource] =
          definitionIndices.lookup(found->second);
    }
    for (auto binding : primitiveDefinitionBindings) {
      auto source = anchorsBySymbol.find(binding.getSource());
      auto definition = definitionsBySymbol.find(binding.getDefinition());
      uint32_t packedSource = 0;
      if (source == anchorsBySymbol.end() ||
          definition == definitionsBySymbol.end() ||
          !objectIndices.count(source->second) ||
          !definitionIndices.count(definition->second) ||
          definitionBindings.size() >= kVirtualBindingLimit ||
          !tryPackTableIndex(TableKind::Object,
                             objectIndices.lookup(source->second),
                             packedSource) ||
          definitionForSource.count(packedSource)) {
        binding.emitOpError("primitive definition binding cannot be encoded");
        return {};
      }
      uint32_t definitionIndex = definitionIndices.lookup(definition->second);
      definitionBindings.push_back(
          {packedSource, definitionIndex, UINT32_MAX, 0});
      definitionForSource[packedSource] = definitionIndex;
    }
    llvm::sort(definitionBindings, [](const DefinitionBindingRecord &left,
                                      const DefinitionBindingRecord &right) {
      return left.sourceIndexAndTable < right.sourceIndexAndTable;
    });
    for (DefinitionBindingRecord &binding : definitionBindings) {
      uint32_t endpointCount = definitionEndpointCounts[binding.definition];
      if (definitionMemberEndpoints.size() > UINT32_MAX - endpointCount) {
        design.emitOpError("VPI definition-member endpoint table exceeds "
                           "32-bit indices");
        return {};
      }
      binding.firstMemberEndpoint =
          static_cast<uint32_t>(definitionMemberEndpoints.size());
      definitionMemberEndpoints.resize(definitionMemberEndpoints.size() +
                                       endpointCount);
    }
  }
  for (auto [index, object] : llvm::enumerate(objects)) {
    if (object.kind == OBELISK_RT_DESIGN_RECORD_PROCESS ||
        object.kind == OBELISK_RT_DESIGN_RECORD_FUNCTION)
      codeUnitObjectIndices[object.id] = static_cast<uint32_t>(index);
    if (isa_and_present<sim::SimNetDeclOp>(object.identity))
      physicalNetObjectIndices[object.id] = static_cast<uint32_t>(index);
    if (isa_and_present<sim::SimStorageDeclOp>(object.identity))
      physicalStorageObjectIndices[object.id] = static_cast<uint32_t>(index);
    if (includeStatements && object.kind == OBELISK_RT_DESIGN_RECORD_STORAGE &&
        wholeStoragePortSources.contains(object.id))
      canonicalStorageTargetIndices.try_emplace(object.id,
                                                static_cast<uint32_t>(index));
    if (includeStatements && object.kind == OBELISK_RT_DESIGN_RECORD_NET &&
        wholeNetPortSources.contains(object.id))
      canonicalNetTargetIndices.try_emplace(object.id,
                                            static_cast<uint32_t>(index));
  }
  for (auto [index, object] : llvm::enumerate(staticObjects)) {
    staticObjectIndices[object.identity] = static_cast<uint32_t>(index);
    if (isa_and_present<sim::SimVPIForeachDeclOp>(object.identity))
      foreachNullIndices[object.id] = static_cast<uint32_t>(index);
  }
  for (const Record &object : objects) {
    auto identity =
        dyn_cast_if_present<sim::SimVPINetIdentityDeclOp>(object.identity);
    if (!identity)
      continue;
    if (!physicalNetObjectIndices.contains(identity.getBackingNetId())) {
      identity.emitOpError("backing net was not serialized");
      return {};
    }
  }
  for (auto [index, statement] : llvm::enumerate(statements)) {
    statementIndices[statement.declaration.getId()] =
        static_cast<uint32_t>(index);
    statementKinds[statement.declaration.getId()] =
        statement.declaration.getVpiKind();
  }

  if (includeStatements) {
    for (auto binding :
         design.getBody().front().getOps<sim::SimVPIThreadOriginBindingOp>()) {
      auto statement = statementIndices.find(binding.getStatementId());
      if (!codeUnitObjectIndices.contains(binding.getCodeUnitId()) ||
          statement == statementIndices.end()) {
        binding.emitOpError("thread-origin endpoint was not serialized");
        return {};
      }
      threadOriginBindings.push_back(
          {binding.getCodeUnitId(), statement->second});
    }
  }

  for (sim::SimVPICallDeclOp call : callDeclarations) {
    TableKind table = TableKind::Object;
    uint32_t index = objectIndices.lookup(call);
    if (auto statementID = call.getStatementId()) {
      table = TableKind::Statement;
      index = statementIndices.lookup(*statementID);
    }
    uint32_t packed = 0;
    if (!tryPackTableIndex(table, index, packed)) {
      call.emitOpError("call occurrence object reference cannot be packed");
      return {};
    }
    callRefsBySymbol[call.getSymName()] = call;
    callSiteBindings.push_back(
        {call.getId(), packed,
         codeUnitObjectIndices.lookup(call.getCodeUnitId()),
         static_cast<uint32_t>(call.getIsIndirect()) |
             (classDispatchCalls.contains(call.getSymName()) ? 2u : 0u) |
             (interfaceDispatchCalls.contains(call.getSymName()) ? 4u : 0u)});
  }

  for (sim::SimVPILocalDeclOp local : localDeclarations) {
    uint32_t owner = codeUnitObjectIndices.lookup(local.getCodeUnitId());
    uint32_t scope = 0;
    TableKind table = TableKind::Object;
    uint32_t index = owner;
    if (auto statement = local.getStatementScopeId()) {
      table = TableKind::Statement;
      index = statementIndices.lookup(*statement);
    }
    if (!tryPackTableIndex(table, index, scope)) {
      local.emitOpError("local procedural scope reference cannot be packed");
      return {};
    }
    frameLocalBindings.push_back({local.getId(), objectIndices.lookup(local),
                                  owner, scope,
                                  static_cast<uint32_t>(local.getOrdinal())});
  }

  DenseMap<uint64_t, uint32_t> automaticOrdinals;
  DenseSet<std::pair<uint64_t, uint32_t>> relationIdentities;
  auto relationIdentity = [](uint32_t sourceIndex, uint16_t selector,
                             uint16_t packedSource, uint32_t packedTarget) {
    uint64_t source = (uint64_t{sourceIndex} << 32) |
                      (uint64_t{selector} << 16) | packedSource;
    return std::pair(source, packedTarget);
  };
  auto addAutomaticRelation =
      [&](TableKind sourceTable, uint32_t sourceIndex, uint32_t sourceKind,
          TableKind targetTable, uint32_t targetIndex, uint32_t targetKind,
          VPIAutomaticRelation automaticKind) -> LogicalResult {
    if (sourceKind == 0 || targetKind == 0)
      return success();
    uint32_t packedTarget = 0;
    if (!tryPackTableIndex(targetTable, targetIndex, packedTarget))
      return design.emitOpError(
          "automatic VPI relation target index cannot be packed");
    auto [first, last] = automaticEdges(sourceKind);
    for (auto edge = first; edge != last; ++edge) {
      if (edge->automaticRelation != automaticKind ||
          !vpiObjectSetContains(edge->targets, targetKind))
        continue;
      bool iterate = edge->mode == VPITraversalMode::Iterate;
      uint16_t packedSource = 0;
      if (!tryPackRelationSource(sourceTable, sourceKind, iterate,
                                 packedSource))
        return design.emitOpError(
            "automatic VPI relation source kind cannot be packed");
      if (!relationIdentities
               .insert(relationIdentity(sourceIndex, edge->selector,
                                        packedSource, packedTarget))
               .second)
        continue;
      uint32_t ordinal = 0;
      if (iterate) {
        uint64_t ordinalKey =
            (uint64_t{static_cast<uint8_t>(sourceTable)} << 48) |
            (uint64_t{sourceIndex} << 18) | (uint64_t{edge->selector} << 2) | 1;
        ordinal = automaticOrdinals[ordinalKey]++;
      }
      if (relations.size() == UINT32_MAX)
        return design.emitOpError(
            "reflection relation table exceeds 32-bit indices");
      relations.push_back({sourceTable, sourceIndex, iterate, packedTarget,
                           ordinal, static_cast<uint16_t>(edge->selector),
                           packedSource});
    }
    return success();
  };
  auto addRelation =
      [&](Operation *sourceOperation, TableKind sourceTable,
          uint32_t sourceIndex, uint32_t sourceKind, uint32_t selector,
          VPITraversalMode mode, TableKind targetTable, uint32_t targetIndex,
          uint32_t targetKind, uint32_t ordinal = 0) -> LogicalResult {
    const VPITraversalDescriptor *edge =
        findVPITraversal(sourceKind, selector, mode);
    if (!edge || !vpiObjectSetContains(edge->targets, targetKind))
      return sourceOperation->emitError(
          "preserved VPI relation is not legal in the generated object "
          "model");
    uint32_t packedTarget = 0;
    if (!tryPackTableIndex(targetTable, targetIndex, packedTarget))
      return sourceOperation->emitError(
          "preserved VPI relation target index cannot be packed");
    bool iterate = mode == VPITraversalMode::Iterate;
    uint16_t packedSource = 0;
    if (!tryPackRelationSource(sourceTable, sourceKind, iterate, packedSource))
      return sourceOperation->emitError(
          "preserved VPI relation source kind cannot be packed");
    // Explicit operands are ordered occurrences. Repeated parameter uses
    // share their declaration identity, but each position must survive in
    // the expression (for example, both operands of P + P).
    const bool sharedParameterOperand =
        isa<sim::SimVPIRelationDeclOp>(sourceOperation) && iterate &&
        sourceKind == static_cast<uint32_t>(VPIObjectKind::Operation) &&
        selector == static_cast<uint32_t>(VPIRelationKind::OperandRel) &&
        targetKind == static_cast<uint32_t>(VPIObjectKind::Parameter);
    if (!relationIdentities
             .insert(relationIdentity(sourceIndex, selector, packedSource,
                                      packedTarget))
             .second &&
        !sharedParameterOperand)
      return success();
    if (relations.size() == UINT32_MAX)
      return sourceOperation->emitError(
          "reflection relation table exceeds 32-bit indices");
    relations.push_back({sourceTable, sourceIndex, iterate, packedTarget,
                         ordinal, static_cast<uint16_t>(selector),
                         packedSource});
    return success();
  };

  struct ImageObjectRef {
    TableKind table;
    uint32_t index;
    uint32_t vpiKind;
  };
  DenseMap<Operation *, ImageObjectRef> anchorRefs, nettypeRefs, typespecRefs,
      classMemberRefs, unresolvedAliasRefs, sourceInheritanceRefs;
  DenseSet<uint32_t> lexicallyAnchoredObjectIndices;
  DenseMap<uint64_t, ImageObjectRef> codeUnitSourceRefs;
  if (includeStatements) {
    for (auto [index, object] : llvm::enumerate(objects))
      if (auto storage =
              dyn_cast_if_present<sim::SimStorageDeclOp>(object.identity);
          storage && (classStaticStorage.contains(storage.getId()) ||
                      eventArrayStorage.contains(storage) ||
                      staticLocalBindings.contains(storage.getId())))
        lexicallyAnchoredObjectIndices.insert(static_cast<uint32_t>(index));
    for (sim::SimVPIObjectAnchorOp anchor : anchors) {
      sim::VPIObjectBackingAttr backing = anchor.getBackingAttr();
      if (!backing || backing.getKind() == sim::VPIObjectBackingKind::Class) {
        auto compact = staticObjectIndices.find(anchor);
        if (compact != staticObjectIndices.end())
          anchorRefs[anchor] = {TableKind::StaticObject, compact->second,
                                anchor.getVpiKind()};
        else
          anchorRefs[anchor] = {TableKind::Object, objectIndices.lookup(anchor),
                                anchor.getVpiKind()};
        continue;
      }
      if (backing.getKind() == sim::VPIObjectBackingKind::Scope) {
        uint64_t id = backing.getId().getValue().getZExtValue();
        uint32_t index = scopeIndices.lookup(id);
        anchorRefs[anchor] = {TableKind::Scope, index,
                              sim::vpiKindForScope(scopes[index])};
        continue;
      }
      if (backing.getKind() == sim::VPIObjectBackingKind::Storage) {
        uint64_t id = backing.getId().getValue().getZExtValue();
        auto found = physicalStorageObjectIndices.find(id);
        if (found == physicalStorageObjectIndices.end()) {
          anchor.emitOpError("backing event storage was not serialized");
          return {};
        }
        uint32_t index = found->second;
        anchorRefs[anchor] = {TableKind::Object, index, objects[index].vpiKind};
        // The event cell itself remains the public storage object. Only its
        // lexical relations come from the exact source anchor; no private
        // backing marker or second runtime identity is introduced.
        lexicallyAnchoredObjectIndices.insert(index);
        continue;
      }
      if (backing.getKind() == sim::VPIObjectBackingKind::Net) {
        uint64_t id = backing.getId().getValue().getZExtValue();
        auto found = physicalNetObjectIndices.find(id);
        if (found == physicalNetObjectIndices.end()) {
          anchor.emitOpError("backing net was not serialized");
          return {};
        }
        uint32_t index = found->second;
        objects[index].vpiKind = anchor.getVpiKind();
        objects[index].caps |= OBELISK_RT_DESIGN_CAP_LEXICAL_ANCHOR;
        anchorRefs[anchor] = {TableKind::Object, index, anchor.getVpiKind()};
        lexicallyAnchoredObjectIndices.insert(index);
        continue;
      }
      uint64_t id = backing.getId().getValue().getZExtValue();
      uint32_t index = codeUnitObjectIndices.lookup(id);
      anchorRefs[anchor] = {TableKind::Object, index, objects[index].vpiKind};
      objects[index].caps |= OBELISK_RT_DESIGN_CAP_LEXICAL_ANCHOR;
      if (anchor.getParentAttr() &&
          typespecsBySymbol.contains(anchor.getParentAttr().getValue()))
        objects[index].indexName = false;
      lexicallyAnchoredObjectIndices.insert(index);
    }
    SymbolTableCollection sourceScopeSymbols;
    for (auto binding :
         design.getBody().front().getOps<sim::SimVPICodeUnitScopeBindingOp>()) {
      auto source =
          sourceScopeSymbols.lookupNearestSymbolFrom<sim::SimVPIObjectAnchorOp>(
              binding, binding.getSourceAttr());
      auto found = anchorRefs.find(source);
      if (found == anchorRefs.end() ||
          !codeUnitObjectIndices.contains(binding.getCodeUnitId())) {
        binding.emitOpError("code-unit source scope was not serialized");
        return {};
      }
      uint32_t packed = 0;
      if (!tryPackTableIndex(found->second.table, found->second.index,
                             packed)) {
        binding.emitOpError("code-unit source scope cannot be packed");
        return {};
      }
      auto unit = cast<sim::SimCodeUnitDeclOp>(
          objects[codeUnitObjectIndices.lookup(binding.getCodeUnitId())]
              .identity);
      const bool taskViaFunction =
          unit.getCodeUnitKind() == sim::EntryKind::Function &&
          sim::vpiCallableSourceKind(unit) == sim::EntryKind::Task;
      codeUnitScopeBindings.push_back(
          {binding.getCodeUnitId(), packed, taskViaFunction ? 1u : 0u});
      codeUnitSourceRefs.try_emplace(binding.getCodeUnitId(), found->second);
    }
    llvm::sort(codeUnitScopeBindings, [](auto left, auto right) {
      return left.codeUnitID < right.codeUnitID;
    });
    for (auto [storage, anchor] : eventArrayStorage) {
      ImageObjectRef target = anchorRefs.lookup(anchor);
      uint32_t packed = 0;
      if (!tryPackTableIndex(target.table, target.index, packed)) {
        storage->emitError("named-event-array binding index cannot be packed");
        return {};
      }
      eventArrayStorageBindings.push_back(
          {packed, objectIndices.lookup(storage)});
    }
    // The declaration and its scalar members share the exact semantic type
    // retained on the executable storage, including source typedef identity.
    for (sim::SimVPIObjectAnchorOp anchor : anchors) {
      if (!anchor.getParentAttr() ||
          anchor.getVpiKind() !=
              static_cast<uint32_t>(VPIObjectKind::NamedEvent))
        continue;
      auto parent = anchorsBySymbol.find(anchor.getParentAttr().getValue());
      if (parent == anchorsBySymbol.end())
        continue;
      sim::VPITypeSemanticsAttr semantic =
          eventArraySemantics.lookup(parent->second);
      while (semantic && semantic.getKind() == sim::VPITypeKind::UnpackedArray)
        semantic = cast<sim::VPITypeSemanticsAttr>(semantic.getChildren()[0]);
      if (semantic)
        eventArraySemantics[anchor] = semantic;
    }
    DenseMap<uint64_t, uint32_t> terminalDriverObjects;
    for (auto [index, object] : llvm::enumerate(objects))
      if (auto driver = dyn_cast<sim::SimDriverDeclOp>(object.identity))
        terminalDriverObjects[driver.getId()] = index;
    for (auto binding : design.getOps<sim::SimVPITerminalDriverBindingOp>()) {
      auto terminal = anchorsBySymbol.find(binding.getTerminal());
      auto low = terminalDriverObjects.find(binding.getLowDriverId());
      auto high = binding.getHighDriverId()
                      ? terminalDriverObjects.find(*binding.getHighDriverId())
                      : terminalDriverObjects.end();
      if (terminal == anchorsBySymbol.end() ||
          low == terminalDriverObjects.end() ||
          (binding.getHighDriverId() && high == terminalDriverObjects.end())) {
        binding.emitOpError("terminal or driver was not serialized");
        return {};
      }
      auto target = anchorRefs.lookup(terminal->second);
      uint32_t packed = 0;
      if (!tryPackTableIndex(target.table, target.index, packed)) {
        binding.emitOpError("primitive terminal index cannot be packed");
        return {};
      }
      terminalDriverBindings.push_back(
          {packed, low->second,
           high == terminalDriverObjects.end() ? UINT32_MAX : high->second,
           static_cast<uint32_t>(binding.getBitOffset())});
    }
    llvm::sort(terminalDriverBindings, [](const auto &left, const auto &right) {
      return left.terminal < right.terminal;
    });
    for (auto binding :
         design.getBody().front().getOps<sim::SimVPIEventBindingOp>()) {
      auto found = anchorsBySymbol.find(binding.getEventAttr().getValue());
      if (found == anchorsBySymbol.end()) {
        binding.emitOpError("named-event anchor was not serialized");
        return {};
      }
      ImageObjectRef target = anchorRefs.lookup(found->second);
      uint32_t packed = 0;
      if (!tryPackTableIndex(target.table, target.index, packed)) {
        binding.emitOpError("named-event binding index cannot be packed");
        return {};
      }
      eventBindings.push_back({packed, binding.getEventId()});
    }
    for (Operation *declaration : sourceInheritance) {
      uint32_t index = objectIndices.lookup(declaration);
      sourceInheritanceRefs[declaration] = {TableKind::Object, index,
                                            objects[index].vpiKind};
    }
    for (auto alias : unresolvedAliases) {
      uint32_t index = objectIndices.lookup(alias);
      unresolvedAliasRefs[alias] = {TableKind::Object, index,
                                    objects[index].vpiKind};
    }
    for (sim::SimVPITypespecDeclOp typespec : typespecs) {
      uint32_t index = objectIndices.lookup(typespec);
      typespecRefs[typespec] = {TableKind::Object, index,
                                objects[index].vpiKind};
    }
    for (sim::SimVPINettypeDeclOp nettype : nettypes) {
      uint32_t index = objectIndices.lookup(nettype);
      nettypeRefs[nettype] = {TableKind::Object, index, objects[index].vpiKind};
    }
    for (sim::SimVPIClassMemberDeclOp member : classMembers) {
      uint32_t index = objectIndices.lookup(member);
      classMemberRefs[member] = {TableKind::Object, index, objects[index].vpiKind};
    }
  }

  DenseMap<uint64_t, ImageObjectRef> storageRefs, netRefs, netIdentityRefs,
      portRefs;
  if (includeStatements) {
    for (auto [index, object] : llvm::enumerate(objects)) {
      ImageObjectRef reference{TableKind::Object, static_cast<uint32_t>(index),
                               object.vpiKind};
      if (object.kind == OBELISK_RT_DESIGN_RECORD_STORAGE)
        storageRefs.try_emplace(object.id, reference);
      else if (object.kind == OBELISK_RT_DESIGN_RECORD_NET) {
        if (auto identity = dyn_cast_if_present<sim::SimVPINetIdentityDeclOp>(
                object.identity))
          netIdentityRefs.try_emplace(identity.getId(), reference);
        else
          netRefs.try_emplace(object.id, reference);
      } else if (object.kind == OBELISK_RT_DESIGN_RECORD_PORT)
        portRefs.try_emplace(object.id, reference);
    }

    DenseMap<uint64_t, uint32_t> definitionBindingByScope;
    for (auto [bindingIndex, binding] : llvm::enumerate(definitionBindings)) {
      if (unpackTableIndexKind(binding.sourceIndexAndTable) != TableKind::Scope)
        continue;
      uint32_t scopeIndex = unpackTableIndex(binding.sourceIndexAndTable);
      if (scopeIndex < scopes.size())
        definitionBindingByScope[scopes[scopeIndex].getId()] =
            static_cast<uint32_t>(bindingIndex);
    }
    DenseMap<std::pair<uint32_t, uint32_t>, sim::VPITypeSemanticsAttr>
        effectiveMemberTypes;
    for (sim::SimVPIDefinitionMemberSpecializationOp specialized :
         definitionMemberSpecializations) {
      auto specialization = definitionSpecializationsBySymbol.find(
          specialized.getSpecializationAttr().getValue());
      auto member = definitionMembersBySymbol.find(
          specialized.getMemberAttr().getValue());
      if (specialization == definitionSpecializationsBySymbol.end() ||
          member == definitionMembersBySymbol.end() ||
          !definitionSpecializationIndices.count(specialization->second) ||
          !definitionMemberIndices.count(member->second))
        continue;
      effectiveMemberTypes.try_emplace(
          std::make_pair(
              definitionSpecializationIndices.lookup(specialization->second),
              definitionMemberIndices.lookup(member->second)),
          specialized.getEffectiveType());
    }
    const VPITraversalDescriptor *exprEdge =
        findVPITraversal(static_cast<uint32_t>(VPIObjectKind::IODecl),
                         static_cast<uint32_t>(VPIRelationKind::ExprRel),
                         VPITraversalMode::Handle);
    const VPITraversalDescriptor *refActualEdge =
        findVPITraversal(static_cast<uint32_t>(VPIObjectKind::RefObj),
                         static_cast<uint32_t>(VPIRelationKind::ActualRel),
                         VPITraversalMode::Handle);
    for (sim::SimVPIDefinitionMemberInstanceBindingOp endpoint :
         definitionMemberInstanceBindings) {
      auto bindingIndex = definitionBindingByScope.find(endpoint.getScopeId());
      auto member =
          definitionMembersBySymbol.find(endpoint.getMemberAttr().getValue());
      if (bindingIndex == definitionBindingByScope.end() ||
          member == definitionMembersBySymbol.end() ||
          !definitionMemberIndices.count(member->second)) {
        endpoint.emitOpError(
            "VPI definition-member endpoint cannot be encoded");
        return {};
      }
      DefinitionBindingRecord &binding =
          definitionBindings[bindingIndex->second];
      uint32_t memberIndex = definitionMemberIndices.lookup(member->second);
      const DefinitionRecord &definition = definitions[binding.definition];
      if (memberIndex < definition.firstMember ||
          memberIndex - definition.firstMember >= definition.memberCount) {
        endpoint.emitOpError("VPI endpoint member belongs to another scope "
                             "definition");
        return {};
      }
      uint64_t id = endpoint.getExprTarget().getId().getValue().getZExtValue();
      std::optional<ImageObjectRef> target;
      switch (endpoint.getExprTarget().getKind()) {
      case sim::VPIObjectRefKind::Storage: {
        auto found = storageRefs.find(id);
        if (found != storageRefs.end())
          target = found->second;
        break;
      }
      case sim::VPIObjectRefKind::Net: {
        auto found = netRefs.find(id);
        if (found != netRefs.end())
          target = found->second;
        break;
      }
      case sim::VPIObjectRefKind::NetIdentity: {
        auto found = netIdentityRefs.find(id);
        if (found != netIdentityRefs.end())
          target = found->second;
        break;
      }
      case sim::VPIObjectRefKind::Statement:
      case sim::VPIObjectRefKind::Port:
      case sim::VPIObjectRefKind::Symbol:
        break;
      }
      bool ref =
          member->second.getDirection().value_or(
              sim::VPIIODirection::Undefined) == sim::VPIIODirection::Ref;
      if (ref) {
        auto effective = effectiveMemberTypes.find(
            std::make_pair(binding.specialization, memberIndex));
        auto storage = target && target->index < objects.size()
                           ? dyn_cast_if_present<sim::SimStorageDeclOp>(
                                 objects[target->index].identity)
                           : sim::SimStorageDeclOp{};
        if (effective == effectiveMemberTypes.end() || !storage ||
            !sim::areEquivalentVPIRefTypes(effective->second,
                                           storage.getVpiTypeAttr())) {
          endpoint.emitOpError(
              "ref actual type does not match its specialized member "
              "type: formal ")
              << (effective == effectiveMemberTypes.end()
                      ? Attribute{}
                      : Attribute(effective->second))
              << ", actual "
              << (storage ? Attribute(storage.getVpiTypeAttr()) : Attribute{});
          return {};
        }
      }
      bool legalTarget =
          target && exprEdge &&
          (ref ? endpoint.getExprTarget().getKind() ==
                         sim::VPIObjectRefKind::Storage &&
                     refActualEdge &&
                     vpiObjectSetContains(
                         exprEdge->targets,
                         static_cast<uint32_t>(VPIObjectKind::RefObj)) &&
                     vpiObjectSetContains(refActualEdge->targets,
                                          target->vpiKind)
               : vpiObjectSetContains(exprEdge->targets, target->vpiKind));
      if (!legalTarget) {
        endpoint.emitOpError(
            "expression endpoint is not legal for a VPI IO declaration");
        return {};
      }
      uint32_t packedTarget = 0;
      if (!tryPackTableIndex(target->table, target->index, packedTarget)) {
        endpoint.emitOpError("VPI expression endpoint cannot be packed");
        return {};
      }
      uint32_t slot = binding.firstMemberEndpoint +
                      definitionMemberEndpointOrdinals.lookup(memberIndex);
      if (slot >= definitionMemberEndpoints.size() ||
          definitionMemberEndpoints[slot].targetIndexAndTable != UINT32_MAX) {
        endpoint.emitOpError(
            "duplicate VPI definition-member expression endpoint");
        return {};
      }
      definitionMemberEndpoints[slot].targetIndexAndTable = packedTarget;
    }

    struct PreparedMemberInstanceRelation {
      sim::SimVPIDefinitionMemberInstanceRelationOp declaration;
      uint32_t binding = 0;
      uint32_t memberAndFlags = 0;
      uint16_t selector = 0;
      uint16_t mode = 0;
      uint32_t inverseSelector = 0;
      uint32_t ordinal = 0;
      uint32_t packedTarget = 0;
    };
    SmallVector<PreparedMemberInstanceRelation> preparedRelations;
    preparedRelations.reserve(
        definitionMemberInstanceRelationDeclarations.size());
    for (sim::SimVPIDefinitionMemberInstanceRelationOp relation :
         definitionMemberInstanceRelationDeclarations) {
      auto binding = definitionBindingByScope.find(relation.getScopeId());
      auto member =
          definitionMembersBySymbol.find(relation.getMemberAttr().getValue());
      uint64_t targetId =
          relation.getTarget().getId().getValue().getZExtValue();
      auto target = portRefs.find(targetId);
      if (binding == definitionBindingByScope.end() ||
          member == definitionMembersBySymbol.end() ||
          !definitionMemberIndices.count(member->second) ||
          relation.getTarget().getKind() != sim::VPIObjectRefKind::Port ||
          target == portRefs.end()) {
        relation.emitOpError(
            "VPI definition-member instance relation cannot be encoded");
        return {};
      }
      uint32_t memberIndex = definitionMemberIndices.lookup(member->second);
      if (memberIndex >= kDefinitionMemberRefObjectFlag ||
          target->second.vpiKind !=
              static_cast<uint32_t>(VPIObjectKind::Port)) {
        relation.emitOpError(
            "VPI definition-member instance relation endpoint is invalid");
        return {};
      }
      const DefinitionBindingRecord &sourceBinding =
          definitionBindings[binding->second];
      const DefinitionRecord &sourceDefinition =
          definitions[sourceBinding.definition];
      if (memberIndex < sourceDefinition.firstMember ||
          memberIndex - sourceDefinition.firstMember >=
              sourceDefinition.memberCount ||
          member->second.getDirection().value_or(
              sim::VPIIODirection::Undefined) != sim::VPIIODirection::Ref) {
        relation.emitOpError(
            "VPI definition-member relation source is not a RefObj");
        return {};
      }
      uint32_t endpointIndex =
          sourceBinding.firstMemberEndpoint +
          definitionMemberEndpointOrdinals.lookup(memberIndex);
      if (endpointIndex >= definitionMemberEndpoints.size() ||
          definitionMemberEndpoints[endpointIndex].targetIndexAndTable ==
              UINT32_MAX) {
        relation.emitOpError(
            "VPI definition-member relation source has no instance endpoint");
        return {};
      }
      auto mode = static_cast<VPITraversalMode>(
          static_cast<uint32_t>(relation.getMode()));
      const VPITraversalDescriptor *edge =
          findVPITraversal(static_cast<uint32_t>(VPIObjectKind::RefObj),
                           relation.getSelector(), mode);
      if (!edge ||
          edge->automaticRelation !=
              VPIAutomaticRelation::DefinitionMemberInstanceRelation ||
          edge->inverseSelector == 0 ||
          !vpiObjectSetContains(edge->targets, target->second.vpiKind)) {
        relation.emitOpError(
            "relation is not legal in the generated VPI object model");
        return {};
      }
      uint32_t packedTarget = 0;
      if (!tryPackTableIndex(target->second.table, target->second.index,
                             packedTarget)) {
        relation.emitOpError("VPI port target cannot be packed");
        return {};
      }
      auto port = dyn_cast_if_present<sim::SimPortDeclOp>(
          objects[target->second.index].identity);
      if (edge->inverseSelector ==
              static_cast<uint32_t>(VPIRelationKind::LowConnRel) &&
          (!port || port.getDirection() != sim::PortDirection::InOut)) {
        relation.emitOpError(
            "owning ref port must have inout execution direction");
        return {};
      }
      preparedRelations.push_back(
          {relation, binding->second,
           kDefinitionMemberRefObjectFlag | memberIndex,
           static_cast<uint16_t>(relation.getSelector()),
           static_cast<uint16_t>(relation.getMode()), edge->inverseSelector,
           static_cast<uint32_t>(relation.getOrdinal()), packedTarget});
    }
    llvm::sort(preparedRelations, [](const auto &left, const auto &right) {
      return std::make_tuple(left.binding, left.memberAndFlags, left.selector,
                             left.mode, left.ordinal, left.packedTarget) <
             std::make_tuple(right.binding, right.memberAndFlags,
                             right.selector, right.mode, right.ordinal,
                             right.packedTarget);
    });
    std::tuple<uint32_t, uint32_t, uint16_t, uint16_t> previousKey{};
    bool haveKey = false;
    uint32_t expectedOrdinal = 0;
    llvm::DenseSet<uint32_t> targetsInGroup;
    llvm::DenseSet<std::pair<uint32_t, uint32_t>> inverseKeys;
    llvm::DenseSet<uint32_t> refLowConnectionPorts;
    for (PreparedMemberInstanceRelation &relation : preparedRelations) {
      auto key = std::make_tuple(relation.binding, relation.memberAndFlags,
                                 relation.selector, relation.mode);
      if (!haveKey || key != previousKey) {
        if (definitionMemberInstanceRelations.size() == UINT32_MAX ||
            definitionMemberInstanceRelationTargets.size() >=
                kDefinitionMemberInstanceTargetFlag) {
          relation.declaration.emitOpError(
              "VPI definition-member relation table exceeds 32-bit indices");
          return {};
        }
        definitionMemberInstanceRelations.push_back(
            {relation.binding, relation.memberAndFlags, relation.selector,
             relation.mode,
             static_cast<uint32_t>(
                 definitionMemberInstanceRelationTargets.size()),
             0});
        previousKey = key;
        haveKey = true;
        expectedOrdinal = 0;
        targetsInGroup.clear();
      }
      if (relation.ordinal != expectedOrdinal++) {
        relation.declaration.emitOpError(
            "VPI definition-member relation ordinals must be dense");
        return {};
      }
      if (!targetsInGroup.insert(relation.packedTarget).second) {
        relation.declaration.emitOpError(
            "duplicate target in a VPI definition-member relation");
        return {};
      }
      uint32_t inverseSelector = relation.inverseSelector;
      if (!inverseKeys.insert({relation.packedTarget, inverseSelector})
               .second) {
        relation.declaration.emitOpError(
            "VPI port has more than one reference connection");
        return {};
      }
      if (definitionMemberInstanceRelationTargets.size() >=
              kDefinitionMemberInstanceTargetFlag ||
          definitionMemberInstanceRelations.back().targetCount == UINT32_MAX) {
        relation.declaration.emitOpError(
            "VPI definition-member relation target table exceeds its "
            "compact encoding");
        return {};
      }
      uint32_t targetIndex =
          static_cast<uint32_t>(definitionMemberInstanceRelationTargets.size());
      definitionMemberInstanceRelationTargets.push_back(relation.packedTarget);
      definitionMemberInstanceRelationInverses.push_back({targetIndex});
      if (inverseSelector == static_cast<uint32_t>(VPIRelationKind::LowConnRel))
        refLowConnectionPorts.insert(relation.packedTarget);
      ++definitionMemberInstanceRelations.back().targetCount;
    }
    llvm::sort(
        definitionMemberInstanceRelationInverses,
        [&](const auto &left, const auto &right) {
          return std::tie(definitionMemberInstanceRelationTargets[left.target],
                          left.target) <
                 std::tie(definitionMemberInstanceRelationTargets[right.target],
                          right.target);
        });

    // Ref ports have a synthetic RefObj as their low connection. Ordinary
    // whole ports keep the canonical net/variable connection below.
    for (uint32_t packedPort : refLowConnectionPorts) {
      Record &port = objects[unpackTableIndex(packedPort)];
      port.caps &= ~OBELISK_RT_DESIGN_CAP_PORT_WHOLE_SOURCE;
      port.caps |= OBELISK_RT_DESIGN_CAP_PORT_REF;
      wholePortConnections.erase(port.id);
    }
  }

  if (includeStatements) {
    // A whole-source port's vpiLowConn is the canonical vpiReg/vpiNet object.
    // Same-name ports have a distinct, deliberately unindexed vpiPort
    // identity; explicitly renamed ports retain their indexed formal name.
    for (auto [sourceIndex, object] : llvm::enumerate(objects)) {
      if (object.kind != OBELISK_RT_DESIGN_RECORD_PORT)
        continue;
      auto connection = wholePortConnections.find(object.id);
      if (connection == wholePortConnections.end())
        continue;
      if (declaredPortNets.contains(object.id))
        continue;
      const auto &targets = connection->second.sourceIsNet
                                ? canonicalNetTargetIndices
                                : canonicalStorageTargetIndices;
      auto target = targets.find(connection->second.sourceID);
      if (target == targets.end()) {
        design.emitOpError("direct VPI port has no canonical low connection");
        return {};
      }
      const Record &canonical = objects[target->second];
      if (failed(addAutomaticRelation(
              TableKind::Object, static_cast<uint32_t>(sourceIndex),
              object.vpiKind, TableKind::Object, target->second,
              canonical.vpiKind, VPIAutomaticRelation::DirectPortConnection)))
        return {};
    }

    // Emit forward relations in the same deterministic order as the immutable
    // child chain: child scopes by stable ID, followed by objects in the
    // normalized object-table order.
    for (auto [targetIndex, scope] : llvm::enumerate(scopes)) {
      if (!scope.getParent())
        continue;
      uint32_t ownerIndex = scopeIndices.lookup(*scope.getParent());
      sim::SimScopeDeclOp owner = scopes[ownerIndex];
      if (failed(addAutomaticRelation(
              TableKind::Scope, ownerIndex, sim::vpiKindForScope(owner),
              TableKind::Scope, static_cast<uint32_t>(targetIndex),
              sim::vpiKindForScope(scope),
              VPIAutomaticRelation::DirectChild)) ||
          failed(addAutomaticRelation(
              TableKind::Scope, static_cast<uint32_t>(targetIndex),
              sim::vpiKindForScope(scope), TableKind::Scope, ownerIndex,
              sim::vpiKindForScope(owner), VPIAutomaticRelation::ParentScope)))
        return {};
    }
    for (auto [targetIndex, object] : llvm::enumerate(objects)) {
      // Static records use explicit lexical and declaration relations below;
      // their physical owner exists only to keep the pointer-free record
      // reachable and is not necessarily their IEEE lexical owner.
      if (object.kind == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT ||
          lexicallyAnchoredObjectIndices.contains(
              static_cast<uint32_t>(targetIndex)))
        continue;
      uint32_t ownerIndex = scopeIndices.lookup(object.scope);
      sim::SimScopeDeclOp owner = scopes[ownerIndex];
      if (failed(addAutomaticRelation(
              TableKind::Scope, ownerIndex, sim::vpiKindForScope(owner),
              TableKind::Object, static_cast<uint32_t>(targetIndex),
              object.vpiKind, VPIAutomaticRelation::DirectChild)) ||
          failed(addAutomaticRelation(
              TableKind::Object, static_cast<uint32_t>(targetIndex),
              object.vpiKind, TableKind::Scope, ownerIndex,
              sim::vpiKindForScope(owner), VPIAutomaticRelation::ParentScope)))
        return {};
    }
    for (auto [sourceIndex, entry] : llvm::enumerate(statements)) {
      sim::SimStatementDeclOp statement = entry.declaration;
      TableKind targetTable = TableKind::Scope;
      uint32_t targetIndex = scopeIndices.lookup(statement.getScopeId());
      uint32_t targetKind = sim::vpiKindForScope(scopes[targetIndex]);

      for (std::optional<uint64_t> parentID = statement.getParentId();
           parentID;) {
        uint32_t parentIndex = statementIndices.lookup(*parentID);
        sim::SimStatementDeclOp parent = statements[parentIndex].declaration;
        if (parent.getIsScope()) {
          targetTable = TableKind::Statement;
          targetIndex = parentIndex;
          targetKind = static_cast<uint32_t>(parent.getVpiKind());
          break;
        }
        parentID = parent.getParentId();
      }

      if (targetTable == TableKind::Scope)
        if (auto ownerID = statement.getCodeUnitId()) {
          uint32_t ownerIndex = codeUnitObjectIndices.lookup(*ownerID);
          uint32_t ownerKind = objects[ownerIndex].vpiKind;
          const auto *ownerDescriptor = findVPIObjectKind(ownerKind);
          if (ownerDescriptor && (ownerDescriptor->families &
                                  vpiFamilyMask(VPIObjectFamily::Scope)) != 0) {
            targetTable = TableKind::Object;
            targetIndex = ownerIndex;
            targetKind = ownerKind;
          }
        }

      if (failed(addAutomaticRelation(
              TableKind::Statement, static_cast<uint32_t>(sourceIndex),
              static_cast<uint32_t>(statement.getVpiKind()), targetTable,
              targetIndex, targetKind, VPIAutomaticRelation::ParentScope)))
        return {};
    }
  }

  // Automatic and static declarations share source order, while automatic
  // frame inventory retains its independent dense activation-local order.
  struct ProceduralVariable {
    Operation *declaration;
    uint32_t scope, ordinal, object;
  };
  SmallVector<ProceduralVariable> proceduralVariables;
  for (auto [index, binding] : llvm::enumerate(frameLocalBindings)) {
    auto local = localDeclarations[index];
    proceduralVariables.push_back(
        {local, binding.scope,
         local.getSourceOrdinal().value_or(local.getOrdinal()),
         binding.object});
  }
  for (auto [storageID, binding] : staticLocalBindings) {
    auto storage = storageRefs.find(storageID);
    if (storage == storageRefs.end() ||
        storage->second.table != TableKind::Object) {
      binding.emitOpError("static local source storage was not serialized");
      return {};
    }
    uint32_t scope = 0;
    auto statement = binding.getStatementScopeId();
    if (!tryPackTableIndex(
            statement ? TableKind::Statement : TableKind::Object,
            statement ? statementIndices.lookup(*statement)
                      : codeUnitObjectIndices.lookup(binding.getCodeUnitId()),
            scope))
      return {};
    proceduralVariables.push_back(
        {binding, scope, binding.getOrdinal(), storage->second.index});
  }
  llvm::sort(proceduralVariables, [](const auto &left, const auto &right) {
    return std::tie(left.scope, left.ordinal) <
           std::tie(right.scope, right.ordinal);
  });
  DenseMap<std::pair<uint32_t, uint32_t>, uint32_t> localSelectorOrdinals;
  for (const auto &variable : proceduralVariables) {
    TableKind scopeTable = unpackTableIndexKind(variable.scope);
    uint32_t scopeIndex = unpackTableIndex(variable.scope);
    uint32_t scopeKind = scopeTable == TableKind::Statement
                             ? statements[scopeIndex].declaration.getVpiKind()
                             : objects[scopeIndex].vpiKind;
    uint32_t kind = objects[variable.object].vpiKind;
    if (failed(addAutomaticRelation(scopeTable, scopeIndex, scopeKind,
                                    TableKind::Object, variable.object, kind,
                                    VPIAutomaticRelation::DirectChild)))
      return {};
    for (uint32_t selector :
         {static_cast<uint32_t>(VPIRelationKind::VariablesRel),
          static_cast<uint32_t>(VPIObjectKind::RegArray),
          static_cast<uint32_t>(VPIObjectKind::Memory),
          static_cast<uint32_t>(VPIObjectKind::NamedEvent),
          static_cast<uint32_t>(VPIObjectKind::NamedEventArray),
          static_cast<uint32_t>(VPIObjectKind::VirtualInterfaceVar)}) {
      const auto *edge =
          findVPITraversal(scopeKind, selector, VPITraversalMode::Iterate);
      if (scopeKind == static_cast<uint32_t>(VPIObjectKind::ForeachStmt) ||
          !edge || !vpiObjectSetContains(edge->targets, kind) ||
          edge->automaticRelation == VPIAutomaticRelation::DirectChild)
        continue;
      if (failed(addRelation(
              variable.declaration, scopeTable, scopeIndex, scopeKind, selector,
              VPITraversalMode::Iterate, TableKind::Object, variable.object,
              kind, localSelectorOrdinals[{variable.scope, selector}]++)))
        return {};
    }
    const auto *scopeEdge =
        findVPITraversal(kind, static_cast<uint32_t>(VPIRelationKind::ScopeRel),
                         VPITraversalMode::Handle);
    if (scopeEdge && vpiObjectSetContains(scopeEdge->targets, scopeKind) &&
        failed(addRelation(
            variable.declaration, TableKind::Object, variable.object, kind,
            static_cast<uint32_t>(VPIRelationKind::ScopeRel),
            VPITraversalMode::Handle, scopeTable, scopeIndex, scopeKind)))
      return {};
  }

  if (includeStatements) {
    auto lookupLexicalOwner = [&](FlatSymbolRefAttr symbol) -> Operation * {
      if (auto found = anchorsBySymbol.find(symbol.getValue());
          found != anchorsBySymbol.end())
        return found->second;
      if (auto found = typespecsBySymbol.find(symbol.getValue());
          found != typespecsBySymbol.end() &&
          (found->second.getOrigin() ==
               sim::VPITypespecOrigin::ClassSpecialization ||
           found->second.getOrigin() == sim::VPITypespecOrigin::BuiltinClass))
        return found->second;
      auto *declaration = SymbolTable::lookupSymbolIn(design, symbol);
      return isa_and_nonnull<sim::SimVPIClassParameterAssignDeclOp>(declaration)
                 ? declaration
                 : nullptr;
    };
    auto lexicalOwnerRef = [&](Operation *owner) {
      if (isa<sim::SimVPIObjectAnchorOp>(owner))
        return anchorRefs.lookup(owner);
      if (isa<sim::SimVPIClassParameterAssignDeclOp>(owner))
        return sourceInheritanceRefs.lookup(owner);
      return typespecRefs.lookup(owner);
    };
    DenseMap<Operation *, DenseMap<int64_t, uint32_t>> sparseArrayOrdinals;
    auto arrayElementOrdinal = [&](sim::SimVPIObjectAnchorOp array,
                                   sim::SimVPIObjectAnchorOp member)
        -> FailureOr<std::optional<uint32_t>> {
      DenseI64ArrayAttr memberIndices = member.getMemberIndicesAttr();
      if (!memberIndices)
        return std::optional<uint32_t>{};
      ArrayRef<int64_t> indices = memberIndices.asArrayRef();
      if (DenseI64ArrayAttr ranges = array.getIndexRangesAttr()) {
        ArrayRef<int64_t> bounds = ranges.asArrayRef();
        if (bounds.size() != indices.size() * 2)
          return member.emitOpError(
              "VPI array member index rank does not match its parent");
        uint64_t flattened = 0;
        for (size_t dimension = 0; dimension != indices.size(); ++dimension) {
          uint64_t ordinal = 0, extent = 0;
          if (!declaredIndexOrdinal(bounds[dimension * 2],
                                    bounds[dimension * 2 + 1],
                                    indices[dimension], ordinal, extent) ||
              ordinal > UINT32_MAX ||
              flattened > (UINT32_MAX - ordinal) / extent)
            return member.emitOpError(
                "VPI array member index cannot be flattened");
          flattened = flattened * extent + ordinal;
        }
        return std::optional<uint32_t>(static_cast<uint32_t>(flattened));
      }
      if (DenseI64ArrayAttr sparse = array.getSparseIndicesAttr()) {
        if (indices.size() != 1)
          return member.emitOpError(
              "generate-scope array member must have one index");
        ArrayRef<int64_t> values = sparse.asArrayRef();
        auto [ordinals, inserted] = sparseArrayOrdinals.try_emplace(array);
        if (inserted)
          for (auto [ordinal, value] : llvm::enumerate(values))
            if (!ordinals->second
                     .try_emplace(value, static_cast<uint32_t>(ordinal))
                     .second)
              return array.emitOpError(
                  "generate-scope array contains duplicate source indices");
        auto found = ordinals->second.find(indices.front());
        if (found == ordinals->second.end())
          return member.emitOpError(
              "generate-scope member index is absent from its parent");
        return std::optional<uint32_t>(found->second);
      }
      return member.emitOpError(
          "indexed VPI member parent has no immutable index metadata");
    };
    llvm::sort(anchors, [](sim::SimVPIObjectAnchorOp left,
                           sim::SimVPIObjectAnchorOp right) {
      StringRef leftParent =
          left.getParentAttr() ? left.getParentAttr().getValue() : StringRef{};
      StringRef rightParent = right.getParentAttr()
                                  ? right.getParentAttr().getValue()
                                  : StringRef{};
      return std::tuple(leftParent, left.getOwnerOrdinal(),
                        left.getInventoryId()) <
             std::tuple(rightParent, right.getOwnerOrdinal(),
                        right.getInventoryId());
    });
    DenseMap<std::pair<Operation *, uint32_t>, uint32_t> lexicalOrdinals;
    DenseMap<uint32_t, uint32_t> rootOrdinals;
    for (sim::SimVPIObjectAnchorOp anchor : anchors) {
      if (!anchor.getParentAttr()) {
        ImageObjectRef target = anchorRefs.lookup(anchor);
        uint32_t selector = static_cast<uint32_t>(VPIRelationKind::InstanceRel);
        const VPITraversalDescriptor *rootEdge =
            findVPITraversal(0, selector, VPITraversalMode::Iterate);
        if (!rootEdge ||
            !vpiObjectSetContains(rootEdge->targets, target.vpiKind)) {
          selector = target.vpiKind;
          rootEdge = findVPITraversal(0, selector, VPITraversalMode::Iterate);
        }
        if (rootEdge &&
            vpiObjectSetContains(rootEdge->targets, target.vpiKind) &&
            failed(addRelation(
                anchor, TableKind::Scope, scopeIndices.lookup(root.getId()), 0,
                selector, VPITraversalMode::Iterate, target.table, target.index,
                target.vpiKind, rootOrdinals[selector]++)))
          return {};
        continue;
      }
      Operation *parent = lookupLexicalOwner(anchor.getParentAttr());
      if (!parent) {
        anchor.emitOpError(
            "lexical parent was not preserved for serialization");
        return {};
      }
      ImageObjectRef source = lexicalOwnerRef(parent);
      ImageObjectRef target = anchorRefs.lookup(anchor);
      if (target.vpiKind == static_cast<uint32_t>(VPIObjectKind::PrimTerm)) {
        if (failed(addRelation(
                anchor, target.table, target.index, target.vpiKind,
                static_cast<uint32_t>(VPIRelationKind::PrimitiveRel),
                VPITraversalMode::Handle, source.table, source.index,
                source.vpiKind)))
          return {};
      }
      if (target.vpiKind == static_cast<uint32_t>(VPIObjectKind::Modport)) {
        uint32_t packed = 0;
        if (source.table != TableKind::Scope ||
            source.vpiKind != static_cast<uint32_t>(VPIObjectKind::Interface) ||
            !tryPackTableIndex(target.table, target.index, packed)) {
          anchor.emitOpError("modport has no physical interface owner");
          return {};
        }
        modportBindings.push_back(
            {packed, source.index, anchor.getDebugName().str()});
      }
      uint32_t lexicalSelector = target.vpiKind;
      if (source.vpiKind ==
              static_cast<uint32_t>(VPIObjectKind::ClassTypespec) &&
          vpiObjectSetContains(VPIObjectSetID::ParameterIterationObjects,
                               target.vpiKind))
        lexicalSelector = static_cast<uint32_t>(VPIObjectKind::Parameter);
      const VPIIndexedAccessDescriptor *indexed =
          findVPIIndexedAccess(source.vpiKind);
      if (indexed &&
          indexed->accessKind == VPIIndexedAccessKind::RelationElement &&
          indexedVPIResultAllowed(*indexed, target.vpiKind))
        lexicalSelector = indexed->relationSelector;
      const VPITraversalDescriptor *lexicalEdge = findVPITraversal(
          source.vpiKind, lexicalSelector, VPITraversalMode::Iterate);
      uint32_t lexicalOrdinal =
          lexicalOrdinals[{parent, lexicalSelector}]++;
      FailureOr<std::optional<uint32_t>> indexedOrdinal =
          std::optional<uint32_t>{};
      if (auto array = dyn_cast<sim::SimVPIObjectAnchorOp>(parent))
        indexedOrdinal = arrayElementOrdinal(array, anchor);
      if (failed(indexedOrdinal))
        return {};
      if (*indexedOrdinal)
        lexicalOrdinal = **indexedOrdinal;
      if (lexicalEdge &&
          lexicalEdge->automaticRelation != VPIAutomaticRelation::DirectChild &&
          vpiObjectSetContains(lexicalEdge->targets, target.vpiKind) &&
          failed(addRelation(parent, source.table, source.index,
                             source.vpiKind, lexicalSelector,
                             VPITraversalMode::Iterate, target.table,
                             target.index, target.vpiKind, lexicalOrdinal)))
        return {};
      if (failed(addAutomaticRelation(
              source.table, source.index, source.vpiKind, target.table,
              target.index, target.vpiKind, VPIAutomaticRelation::DirectChild)))
        return {};
      // A reverse lexical relation may name an effective enclosing object,
      // not the immediate structural parent. For example, an instance-array
      // member and a primitive nested in a generate scope both return their
      // enclosing vpiModule. Derive each such relation from the generated
      // target set and select its nearest matching ancestor.
      auto [parentFirst, parentLast] = automaticEdges(target.vpiKind);
      for (auto edge = parentFirst; edge != parentLast; ++edge) {
        if (edge->mode != VPITraversalMode::Handle ||
            (edge->automaticRelation != VPIAutomaticRelation::ParentScope &&
             edge->automaticRelation != VPIAutomaticRelation::IndexedContainer))
          continue;
        Operation *ancestor = parent;
        while (ancestor) {
          ImageObjectRef ancestorRef = lexicalOwnerRef(ancestor);
          if (vpiObjectSetContains(edge->targets, ancestorRef.vpiKind)) {
            if (failed(addRelation(anchor, target.table, target.index,
                                   target.vpiKind, edge->selector,
                                   VPITraversalMode::Handle, ancestorRef.table,
                                   ancestorRef.index, ancestorRef.vpiKind)))
              return {};
            break;
          }
          FlatSymbolRefAttr ancestorParent;
          if (auto object = dyn_cast<sim::SimVPIObjectAnchorOp>(ancestor))
            ancestorParent = object.getParentAttr();
          else if (isa<sim::SimVPIClassParameterAssignDeclOp>(ancestor))
            break;
          else
            ancestorParent =
                cast<sim::SimVPITypespecDeclOp>(ancestor).getOwnerAttr();
          if (!ancestorParent)
            break;
          Operation *next = lookupLexicalOwner(ancestorParent);
          if (!next) {
            ancestor->emitOpError(
                "lexical ancestor was not preserved for serialization");
            return {};
          }
          ancestor = next;
        }
      }
    }

    llvm::sort(typespecs, [](sim::SimVPITypespecDeclOp left,
                             sim::SimVPITypespecDeclOp right) {
      return left.getId() < right.getId();
    });
    DenseMap<Operation *, uint32_t> typedefOrdinals, interfaceTypespecOrdinals;

    llvm::sort(nettypes, [](sim::SimVPINettypeDeclOp left,
                            sim::SimVPINettypeDeclOp right) {
      return left.getId() < right.getId();
    });
    DenseMap<Operation *, uint32_t> nettypeOrdinals;
    for (sim::SimVPINettypeDeclOp nettype : nettypes) {
      Operation *owner = lookupLexicalOwner(nettype.getOwnerAttr());
      if (!owner) {
        nettype.emitOpError("VPI nettype owner was not preserved");
        return {};
      }
      ImageObjectRef ownerRef = lexicalOwnerRef(owner);
      ImageObjectRef nettypeRef = nettypeRefs.lookup(nettype);
      if (failed(addRelation(
              owner, ownerRef.table, ownerRef.index, ownerRef.vpiKind,
              static_cast<uint32_t>(VPIRelationKind::NetTypedefRel),
              VPITraversalMode::Iterate, nettypeRef.table, nettypeRef.index,
              nettypeRef.vpiKind, nettypeOrdinals[owner]++)))
        return {};
      if (FlatSymbolRefAttr alias = nettype.getDirectAliasAttr()) {
        auto target = nettypesBySymbol.find(alias.getValue());
        if (target == nettypesBySymbol.end()) {
          nettype.emitOpError("VPI direct nettype alias was not serialized");
          return {};
        }
        ImageObjectRef targetRef = nettypeRefs.lookup(target->second);
        if (failed(addRelation(
                nettype, nettypeRef.table, nettypeRef.index, nettypeRef.vpiKind,
                static_cast<uint32_t>(VPIRelationKind::NetTypedefAliasRel),
                VPITraversalMode::Handle, targetRef.table, targetRef.index,
                targetRef.vpiKind)))
          return {};
      }
      if (FlatSymbolRefAttr resolver = nettype.getResolutionFunctionAttr()) {
        auto target = anchorsBySymbol.find(resolver.getValue());
        if (target == anchorsBySymbol.end()) {
          nettype.emitOpError("VPI nettype resolver was not serialized");
          return {};
        }
        ImageObjectRef targetRef = anchorRefs.lookup(target->second);
        if (failed(addRelation(nettype, nettypeRef.table, nettypeRef.index,
                               nettypeRef.vpiKind,
                               static_cast<uint32_t>(VPIRelationKind::WithRel),
                               VPITraversalMode::Handle, targetRef.table,
                               targetRef.index, targetRef.vpiKind)))
          return {};
      }
    }

    DenseMap<Operation *, uint32_t> classSpecializationOrdinals;
    for (sim::SimVPITypespecDeclOp typespec : typespecs) {
      Operation *owner = nullptr;
      ImageObjectRef ownerRef;
      if (auto statementID = typespec.getStatementScopeId()) {
        auto found = statementIndices.find(*statementID);
        if (found != statementIndices.end()) {
          auto statement = statements[found->second].declaration;
          owner = statement;
          ownerRef = {TableKind::Statement, found->second,
                      statement.getVpiKind()};
        }
      } else {
        owner = lookupLexicalOwner(typespec.getOwnerAttr());
        if (owner)
          ownerRef = lexicalOwnerRef(owner);
      }
      if (!owner) {
        typespec.emitOpError("VPI typespec owner was not preserved");
        return {};
      }
      ImageObjectRef typespecRef = typespecRefs.lookup(typespec);
      if (typespec.getOrigin() == sim::VPITypespecOrigin::ClassSpecialization) {
        uint32_t ordinal = classSpecializationOrdinals[owner]++;
        if (failed(addRelation(
                typespec, ownerRef.table, ownerRef.index, ownerRef.vpiKind,
                static_cast<uint32_t>(VPIObjectKind::ClassTypespec),
                VPITraversalMode::Iterate, typespecRef.table, typespecRef.index,
                typespecRef.vpiKind, ordinal)) ||
            failed(addRelation(typespec, typespecRef.table, typespecRef.index,
                               typespecRef.vpiKind,
                               static_cast<uint32_t>(VPIObjectKind::ClassDefn),
                               VPITraversalMode::Handle, ownerRef.table,
                               ownerRef.index, ownerRef.vpiKind)) ||
            failed(addRelation(typespec, typespecRef.table, typespecRef.index,
                               typespecRef.vpiKind,
                               static_cast<uint32_t>(VPIRelationKind::ScopeRel),
                               VPITraversalMode::Handle, ownerRef.table,
                               ownerRef.index, ownerRef.vpiKind)))
          return {};
      } else if (typespec.getOrigin() == sim::VPITypespecOrigin::Typedef) {
        uint32_t ordinal = typedefOrdinals[owner]++;
        if (failed(addRelation(
                owner, ownerRef.table, ownerRef.index, ownerRef.vpiKind,
                static_cast<uint32_t>(VPIRelationKind::TypedefRel),
                VPITraversalMode::Iterate, typespecRef.table, typespecRef.index,
                typespecRef.vpiKind, ordinal)))
          return {};
      } else if (typespec.getOrigin() == sim::VPITypespecOrigin::Interface) {
        if (failed(addRelation(owner, ownerRef.table, ownerRef.index,
                               ownerRef.vpiKind,
                               static_cast<uint32_t>(VPIObjectKind::Typespec),
                               VPITraversalMode::Iterate, typespecRef.table,
                               typespecRef.index, typespecRef.vpiKind,
                               interfaceTypespecOrdinals[owner]++)))
          return {};
      }

      if (const VPITraversalDescriptor *instanceEdge = findVPITraversal(
              typespecRef.vpiKind,
              static_cast<uint32_t>(VPIRelationKind::InstanceRel),
              VPITraversalMode::Handle);
          instanceEdge &&
          vpiObjectSetContains(instanceEdge->targets, ownerRef.vpiKind) &&
          failed(
              addRelation(typespec, typespecRef.table, typespecRef.index,
                          typespecRef.vpiKind,
                          static_cast<uint32_t>(VPIRelationKind::InstanceRel),
                          VPITraversalMode::Handle, ownerRef.table,
                          ownerRef.index, ownerRef.vpiKind)))
        return {};

      ArrayAttr aliases = typespec.getTargetType().getTypedefAliases();
      if (!aliases || aliases.empty())
        continue;
      size_t aliasIndex = 0;
      auto firstAlias = cast<SymbolRefAttr>(aliases[0]);
      if (firstAlias.getRootReference() == typespec.getSymNameAttr())
        aliasIndex = 1;
      if (aliasIndex == aliases.size())
        continue;
      auto alias = cast<SymbolRefAttr>(aliases[aliasIndex]);
      auto target = typespecsBySymbol.find(alias.getRootReference().getValue());
      if (target == typespecsBySymbol.end()) {
        typespec.emitOpError("VPI typedef alias target was not preserved");
        return {};
      }
      ImageObjectRef targetRef = typespecRefs.lookup(target->second);
      if (failed(addRelation(
              typespec, typespecRef.table, typespecRef.index,
              typespecRef.vpiKind,
              static_cast<uint32_t>(VPIRelationKind::TypedefAliasRel),
              VPITraversalMode::Handle, targetRef.table, targetRef.index,
              targetRef.vpiKind)))
        return {};
    }

    auto sourceSymbolRef = [&](FlatSymbolRefAttr symbol) -> ImageObjectRef {
      if (auto source = sourceInheritanceBySymbol.lookup(symbol.getValue()))
        return sourceInheritanceRefs.lookup(source);
      if (auto anchor = anchorsBySymbol.lookup(symbol.getValue()))
        return anchorRefs.lookup(anchor);
      if (auto typespec = typespecsBySymbol.lookup(symbol.getValue()))
        return typespecRefs.lookup(typespec);
      if (auto alias = unresolvedAliasesBySymbol.lookup(symbol.getValue()))
        return unresolvedAliasRefs.lookup(alias);
      llvm_unreachable(
          "verified source inheritance reference was not serialized");
    };
    auto inheritanceRelation = [&](Operation *declaration, ImageObjectRef from,
                                   uint32_t selector, VPITraversalMode mode,
                                   ImageObjectRef to, uint32_t ordinal = 0) {
      return addRelation(declaration, from.table, from.index, from.vpiKind,
                         selector, mode, to.table, to.index, to.vpiKind,
                         ordinal);
    };
    llvm::DenseMap<Attribute, sim::SimVPIClassExtendsDeclOp>
        sourceExtendsByOwner;
    for (Operation *declaration : sourceInheritance)
      if (auto clause = dyn_cast<sim::SimVPIClassExtendsDeclOp>(declaration))
        sourceExtendsByOwner[clause.getOwnerAttr()] = clause;
    auto originalHandleTarget = [&](FlatSymbolRefAttr source,
                                    uint32_t selector) {
      for (auto relation : generalRelationDeclarations)
        if (relation.getSource().getKind() == sim::VPIObjectRefKind::Symbol &&
            relation.getSource().getSymbol() == source &&
            relation.getSelector() == selector &&
            relation.getMode() == sim::VPIRelationMode::Handle &&
            relation.getTarget().getKind() == sim::VPIObjectRefKind::Symbol)
          return relation.getTarget().getSymbol();
      return FlatSymbolRefAttr{};
    };
    for (Operation *declaration : sourceInheritance) {
      ImageObjectRef self = sourceInheritanceRefs.lookup(declaration);
      if (auto type =
              dyn_cast<sim::SimVPILexicalClassTypespecDeclOp>(declaration)) {
        if (failed(inheritanceRelation(
                type, self, static_cast<uint32_t>(VPIObjectKind::ClassDefn),
                VPITraversalMode::Handle,
                sourceSymbolRef(type.getDefinitionAttr()))) ||
            failed(inheritanceRelation(
                type, self, static_cast<uint32_t>(VPIRelationKind::ScopeRel),
                VPITraversalMode::Handle,
                sourceSymbolRef(type.getOwnerAttr()))))
          return {};
        uint32_t parameterOrdinal = 0;
        for (auto parameter : anchors) {
          if (parameter.getParentAttr() != type.getDefinitionAttr() ||
              !vpiObjectSetContains(VPIObjectSetID::ParameterIterationObjects,
                                    parameter.getVpiKind()))
            continue;
          if (failed(inheritanceRelation(
                  type, self, static_cast<uint32_t>(VPIObjectKind::Parameter),
                  VPITraversalMode::Iterate, anchorRefs.lookup(parameter),
                  parameterOrdinal++)))
            return {};
        }
        FlatSymbolRefAttr inheritedType;
        if (auto base = sourceExtendsByOwner.lookup(type.getDefinitionAttr()))
          inheritedType = base.getBaseTypespecAttr();
        else if (auto clause = originalHandleTarget(
                     type.getDefinitionAttr(),
                     static_cast<uint32_t>(VPIRelationKind::ExtendsRel)))
          inheritedType = originalHandleTarget(
              clause, static_cast<uint32_t>(VPIObjectKind::ClassTypespec));
        if (inheritedType &&
            failed(inheritanceRelation(
                type, self, static_cast<uint32_t>(VPIRelationKind::ExtendsRel),
                VPITraversalMode::Handle, sourceSymbolRef(inheritedType))))
          return {};
      } else if (auto clause =
                     dyn_cast<sim::SimVPIClassExtendsDeclOp>(declaration)) {
        auto owner = sourceSymbolRef(clause.getOwnerAttr());
        if (failed(inheritanceRelation(
                clause, owner,
                static_cast<uint32_t>(VPIRelationKind::ExtendsRel),
                VPITraversalMode::Handle, self)) ||
            failed(inheritanceRelation(
                clause, self, static_cast<uint32_t>(VPIObjectKind::ClassDefn),
                VPITraversalMode::Handle, owner)))
          return {};
        if (auto base = clause.getBaseTypespecAttr())
          if (failed(inheritanceRelation(
                  clause, self,
                  static_cast<uint32_t>(VPIObjectKind::ClassTypespec),
                  VPITraversalMode::Handle, sourceSymbolRef(base))))
            return {};
      } else if (auto assignment =
                     dyn_cast<sim::SimVPIClassParameterAssignDeclOp>(
                         declaration)) {
        auto parameter = sourceSymbolRef(assignment.getParameterAttr());
        if (failed(inheritanceRelation(
                assignment, sourceSymbolRef(assignment.getOwnerAttr()),
                static_cast<uint32_t>(VPIObjectKind::ParamAssign),
                VPITraversalMode::Iterate, self, assignment.getOrdinal())) ||
            failed(inheritanceRelation(
                assignment, self,
                static_cast<uint32_t>(VPIRelationKind::LhsRel),
                VPITraversalMode::Handle, parameter)) ||
            failed(inheritanceRelation(
                assignment, self,
                static_cast<uint32_t>(VPIRelationKind::ScopeRel),
                VPITraversalMode::Handle,
                sourceSymbolRef(assignment.getOwnerAttr()))))
          return {};
        if (auto rhs = assignment.getRhsAttr()) {
          if (failed(inheritanceRelation(
                  assignment, self,
                  static_cast<uint32_t>(VPIRelationKind::RhsRel),
                  VPITraversalMode::Handle, sourceSymbolRef(rhs))))
            return {};
        }
        if (assignment.getRhsAttr() || assignment.getUnavailableRhsAttr())
          continue;
        for (auto relation : generalRelationDeclarations)
          if (relation.getSource().getKind() == sim::VPIObjectRefKind::Symbol &&
              relation.getSource().getSymbol() ==
                  assignment.getParameterAttr() &&
              relation.getSelector() ==
                  static_cast<uint32_t>(VPIRelationKind::ExprRel) &&
              relation.getMode() == sim::VPIRelationMode::Handle) {
            if (relation.getTarget().getKind() !=
                sim::VPIObjectRefKind::Symbol) {
              assignment.emitOpError(
                  "original parameter default must preserve symbolic identity");
              return {};
            }
            if (failed(inheritanceRelation(
                    assignment, self,
                    static_cast<uint32_t>(VPIRelationKind::RhsRel),
                    VPITraversalMode::Handle,
                    sourceSymbolRef(relation.getTarget().getSymbol()))))
              return {};
          }
      }
    }

    llvm::sort(unresolvedAliases, [](auto left, auto right) {
      return left.getId() < right.getId();
    });
    for (auto alias : unresolvedAliases) {
      Operation *owner = lookupLexicalOwner(alias.getOwnerAttr());
      ImageObjectRef ownerRef = lexicalOwnerRef(owner);
      ImageObjectRef aliasRef = unresolvedAliasRefs.lookup(alias);
      ImageObjectRef targetRef{};
      if (auto target = unresolvedAliasesBySymbol.lookup(alias.getTarget()))
        targetRef = unresolvedAliasRefs.lookup(target);
      else
        targetRef =
            anchorRefs.lookup(anchorsBySymbol.lookup(alias.getTarget()));
      if (failed(addRelation(
              alias, ownerRef.table, ownerRef.index, ownerRef.vpiKind,
              static_cast<uint32_t>(VPIRelationKind::TypedefRel),
              VPITraversalMode::Iterate, aliasRef.table, aliasRef.index,
              aliasRef.vpiKind, typedefOrdinals[owner]++)) ||
          failed(addRelation(
              alias, aliasRef.table, aliasRef.index, aliasRef.vpiKind,
              static_cast<uint32_t>(VPIRelationKind::TypedefAliasRel),
              VPITraversalMode::Handle, targetRef.table, targetRef.index,
              targetRef.vpiKind)) ||
          failed(addRelation(alias, aliasRef.table, aliasRef.index,
                             aliasRef.vpiKind,
                             static_cast<uint32_t>(VPIRelationKind::ScopeRel),
                             VPITraversalMode::Handle, ownerRef.table,
                             ownerRef.index, ownerRef.vpiKind)))
        return {};
    }

    llvm::sort(enumConstants, [](sim::SimVPIEnumConstDeclOp left,
                                 sim::SimVPIEnumConstDeclOp right) {
      return std::tuple(left.getEnumTypespecAttr().getValue(),
                        left.getOrdinal(), left.getId()) <
             std::tuple(right.getEnumTypespecAttr().getValue(),
                        right.getOrdinal(), right.getId());
    });
    for (sim::SimVPIEnumConstDeclOp enumConstant : enumConstants) {
      auto owner =
          typespecsBySymbol.find(enumConstant.getEnumTypespecAttr().getValue());
      ImageObjectRef ownerRef = typespecRefs.lookup(owner->second);
      uint32_t targetIndex = objectIndices.lookup(enumConstant);
      uint32_t targetKind = objects[targetIndex].vpiKind;
      if (failed(addRelation(
              owner->second, ownerRef.table, ownerRef.index, ownerRef.vpiKind,
              static_cast<uint32_t>(VPIObjectKind::EnumConst),
              VPITraversalMode::Iterate, TableKind::Object, targetIndex,
              targetKind, static_cast<uint32_t>(enumConstant.getOrdinal()))) ||
          failed(addRelation(enumConstant, TableKind::Object, targetIndex,
                             targetKind,
                             static_cast<uint32_t>(VPIObjectKind::EnumTypespec),
                             VPITraversalMode::Handle, ownerRef.table,
                             ownerRef.index, ownerRef.vpiKind)))
        return {};
    }

    auto addDeclaredTypespec = [&](Operation *identity,
                                   ImageObjectRef source) -> LogicalResult {
      sim::VPITypeSemanticsAttr semantic;
      if (auto storage =
              dyn_cast_if_present<sim::SimStorageDeclOp>(identity))
        semantic = storage.getVpiTypeAttr();
      else if (auto net =
                   dyn_cast_if_present<sim::SimNetDeclOp>(identity))
        semantic = net.getVpiTypeAttr();
      else if (auto netIdentity =
                   dyn_cast_if_present<sim::SimVPINetIdentityDeclOp>(
                       identity))
        semantic = netIdentity.getVpiTypeAttr();
      else if (auto port =
                   dyn_cast_if_present<sim::SimPortDeclOp>(identity))
        semantic = port.getVpiTypeAttr();
      else if (auto nettype = dyn_cast_if_present<sim::SimVPINettypeDeclOp>(
                   identity))
        semantic = nettype.getTargetType();
      else if (auto member = dyn_cast_if_present<sim::SimVPIClassMemberDeclOp>(
                   identity))
        semantic = member.getVpiType();
      else if (auto local =
                   dyn_cast_if_present<sim::SimVPILocalDeclOp>(identity))
        semantic = local.getVpiType();
      else if (auto anchor = dyn_cast_if_present<sim::SimVPIObjectAnchorOp>(
                   identity))
        semantic = anchor.getVpiTypeAttr();
      if (!semantic)
        semantic = eventArraySemantics.lookup(identity);
      if (!semantic)
        return success();
      FlatSymbolRefAttr declaredNettype;
      if (auto net = dyn_cast_if_present<sim::SimNetDeclOp>(identity))
        declaredNettype = net.getNettypeAttr();
      else if (auto netIdentity =
                   dyn_cast_if_present<sim::SimVPINetIdentityDeclOp>(
                       identity))
        declaredNettype = netIdentity.getNettypeAttr();
      if (declaredNettype) {
        auto target = nettypesBySymbol.find(declaredNettype.getValue());
        if (target == nettypesBySymbol.end()) {
          return identity->emitError("VPI nettype target was not serialized");
        }
        ImageObjectRef targetRef = nettypeRefs.lookup(target->second);
        if (failed(addRelation(identity, source.table, source.index,
                               source.vpiKind,
                               static_cast<uint32_t>(VPIObjectKind::Typespec),
                               VPITraversalMode::Handle, targetRef.table,
                               targetRef.index, targetRef.vpiKind)))
          return failure();
        return success();
      }
      SymbolRefAttr reference;
      if (ArrayAttr aliases = semantic.getTypedefAliases();
          aliases && !aliases.empty())
        reference = cast<SymbolRefAttr>(aliases[0]);
      else if (semantic.getKind() == sim::VPITypeKind::VirtualInterface)
        reference = semantic.getSymbol();
      sim::SimVPITypespecDeclOp targetTypespec;
      if (reference) {
        auto target =
            typespecsBySymbol.find(reference.getRootReference().getValue());
        if (target == typespecsBySymbol.end()) {
          return identity->emitError("VPI typespec target was not preserved");
        }
        targetTypespec = target->second;
      } else if (semantic.getKind() == sim::VPITypeKind::Enum) {
        auto sourceTypeIdentity =
            sim::get<sim::metadata::VpiSourceTypeIdentity>(identity);
        if (!sourceTypeIdentity) {
          return identity->emitError(
              "anonymous enum VPI value has no source type identity");
        }
        targetTypespec = anonymousTypespecsByIdentity.lookup(
            sourceTypeIdentity.getValue().getZExtValue());
        if (!targetTypespec) {
          return identity->emitError(
              "anonymous enum VPI value has no matching typespec identity");
        }
      }
      if (!targetTypespec)
        return success();
      ImageObjectRef targetRef = typespecRefs.lookup(targetTypespec);
      return addRelation(identity, source.table, source.index, source.vpiKind,
                             static_cast<uint32_t>(VPIObjectKind::Typespec),
                             VPITraversalMode::Handle, targetRef.table,
                             targetRef.index, targetRef.vpiKind);
    };
    for (auto [index, object] : llvm::enumerate(objects))
      if (failed(addDeclaredTypespec(
              object.identity, {TableKind::Object, static_cast<uint32_t>(index),
                                object.vpiKind})))
        return {};
    for (auto [index, object] : llvm::enumerate(staticObjects))
      if (failed(addDeclaredTypespec(
              object.identity,
              {TableKind::StaticObject, static_cast<uint32_t>(index),
               object.vpiKind})))
        return {};
  }

  auto memberOwnerRef = [&](FlatSymbolRefAttr owner) -> ImageObjectRef {
    if (auto found = anchorsBySymbol.find(owner.getValue());
        found != anchorsBySymbol.end())
      return anchorRefs.lookup(found->second);
    return typespecRefs.lookup(typespecsBySymbol.lookup(owner.getValue()));
  };
  auto appendMemberBinding = [&](Operation *operation, ImageObjectRef member,
                                 ImageObjectRef owner, FlatSymbolRefAttr classSymbol,
                                 bool isStatic, bool isMethod,
                                 uint64_t byteOffset, uint64_t eventID,
                                 std::optional<uint64_t> storageID,
                                 StringRef name) -> LogicalResult {
    auto declaration =
        classMemberSymbols.lookupNearestSymbolFrom<sim::SimClassDeclOp>(
            operation, classSymbol);
    ClassMemberBindingRecord binding{};
    binding.storage = UINT32_MAX;
    binding.byteOffset = byteOffset;
    binding.eventID = eventID;
    binding.name = name.str();
    binding.flags = static_cast<uint32_t>(isStatic) |
                    (static_cast<uint32_t>(isMethod) << 1);
    if (!declaration ||
        !tryPackTableIndex(member.table, member.index, binding.member) ||
        !tryPackTableIndex(owner.table, owner.index, binding.owner))
      return operation->emitError("class member identity was not serialized");
    binding.classID = declaration.getId();
    if (storageID) {
      auto storage = storageRefs.find(*storageID);
      if (storage == storageRefs.end() ||
          !tryPackTableIndex(storage->second.table, storage->second.index,
                             binding.storage))
        return operation->emitError("static class member storage was not serialized");
    }
    classMemberBindings.push_back(binding);
    return success();
  };
  llvm::sort(classMembers, [&](auto left, auto right) {
    auto ordinal = [&](auto member) -> uint64_t {
      auto field = classMemberFields.lookup(member);
      return field ? field.getOrdinal() : member.getId();
    };
    return std::make_tuple(left.getOwner(), ordinal(left), left.getId()) <
           std::make_tuple(right.getOwner(), ordinal(right), right.getId());
  });
  DenseMap<std::pair<uint32_t, uint32_t>, uint32_t> memberOrdinals;
  for (sim::SimVPIClassMemberDeclOp member : classMembers) {
    auto field = classMemberFields.lookup(member);
    ImageObjectRef source = memberOwnerRef(member.getOwnerAttr());
    ImageObjectRef target = classMemberRefs.lookup(member);
    uint32_t packedOwner = 0;
    if (!tryPackTableIndex(source.table, source.index, packedOwner))
      return {};
    if (field && !field.getIsStatic() && !field.getOffset()) {
      member.emitOpError("class field layout was not materialized");
      return {};
    }
    if (field &&
        failed(appendMemberBinding(
            member, target, source, field.getOwnerAttr(), field.getIsStatic(),
            false, field.getOffset().value_or(UINT64_MAX),
            member.getStaticEvent().value_or(UINT64_MAX),
            member.getStaticStorage(), member.getDebugName())))
      return {};
    if (failed(addAutomaticRelation(
            source.table, source.index, source.vpiKind, target.table,
            target.index, target.vpiKind, VPIAutomaticRelation::DirectChild)))
      return {};
    for (uint32_t selector : {
             static_cast<uint32_t>(VPIRelationKind::VariablesRel),
             static_cast<uint32_t>(VPIObjectKind::RegArray),
             static_cast<uint32_t>(VPIObjectKind::Memory),
             static_cast<uint32_t>(VPIObjectKind::NamedEvent),
             static_cast<uint32_t>(VPIObjectKind::NamedEventArray),
             static_cast<uint32_t>(VPIObjectKind::VirtualInterfaceVar)}) {
      auto *edge = findVPITraversal(source.vpiKind, selector,
                                    VPITraversalMode::Iterate);
      if (!edge || !vpiObjectSetContains(edge->targets, target.vpiKind))
        continue;
      if (edge->automaticRelation == VPIAutomaticRelation::DirectChild)
        continue;
      if (failed(addRelation(member, source.table, source.index, source.vpiKind,
                             selector, VPITraversalMode::Iterate, target.table,
                             target.index, target.vpiKind,
                             memberOrdinals[{packedOwner, selector}]++)))
        return {};
    }
    const auto *scopeEdge = findVPITraversal(
        target.vpiKind, static_cast<uint32_t>(VPIRelationKind::ScopeRel),
        VPITraversalMode::Handle);
    if (scopeEdge && vpiObjectSetContains(scopeEdge->targets, source.vpiKind) &&
        failed(addRelation(member, target.table, target.index, target.vpiKind,
                           static_cast<uint32_t>(VPIRelationKind::ScopeRel),
                           VPITraversalMode::Handle, source.table, source.index,
                           source.vpiKind)))
      return {};
  }
  for (sim::SimVPIObjectAnchorOp anchor : anchors) {
    if (!anchor.getClassMethodAttr())
      continue;
    auto method =
        classMemberSymbols.lookupNearestSymbolFrom<sim::SimClassMethodDeclOp>(
            anchor, anchor.getClassMethodAttr());
    if (!method || !anchor.getParentAttr() ||
        failed(appendMemberBinding(anchor, anchorRefs.lookup(anchor),
                                   memberOwnerRef(anchor.getParentAttr()),
                                   method.getOwnerAttr(), method.getIsStatic(),
                                   true, UINT64_MAX, UINT64_MAX, std::nullopt,
                                   anchor.getDebugName())))
      return {};
  }
  llvm::sort(classMemberBindings, [](const auto &left, const auto &right) {
    return left.member < right.member;
  });

  if (!classCallDispatchDeclarations.empty()) {
    // Preserve authoritative direct declarations as well as the resolved pairs.
    // The reader recomputes effective dispatch, including pure overrides and
    // interface lowest-slot selection, instead of trusting a target whitelist.
    analysis::ClassDispatchAnalysis dispatchAnalysis(design);
    SymbolTableCollection symbols;
    DenseMap<uint32_t, uint64_t> reflectedMethodOwners;
    for (const auto &member : classMemberBindings)
      if ((member.flags & 2) &&
          unpackTableIndexKind(member.member) == TableKind::Object)
        reflectedMethodOwners.try_emplace(unpackTableIndex(member.member),
                                          member.classID);
    auto reflectedCodeUnit =
        [&](sim::SimClassMethodDeclOp method) -> std::optional<uint32_t> {
      auto implementation = method.getImplementationAttr();
      if (!implementation)
        return std::nullopt;
      auto function = symbols.lookupNearestSymbolFrom<sim::SimFuncOp>(
          method, implementation);
      if (!function || !function.getCodeUnitId())
        return std::nullopt;
      auto unit = codeUnitObjectIndices.find(*function.getCodeUnitId());
      if (unit == codeUnitObjectIndices.end())
        return std::nullopt;
      auto owner = reflectedMethodOwners.find(unit->second);
      if (owner == reflectedMethodOwners.end() ||
          owner->second != dispatchAnalysis.lookup(method.getOwner()).getId())
        return std::nullopt;
      return unit->second;
    };
    for (sim::SimClassDeclOp declaration : dispatchAnalysis.getClasses()) {
      const uint64_t classID = declaration.getId();
      uint32_t flags = (declaration.getIsInterface() ? 1u : 0u) |
                       (declaration.getIsAbstract() ? 2u : 0u);
      classDispatchParentBindings.push_back({classID, 0, 0, flags});
      if (auto base = declaration.getBase())
        classDispatchParentBindings.push_back(
            {classID, dispatchAnalysis.lookup(*base).getId(), 1, 0});
      if (ArrayAttr interfaces = declaration.getInterfacesAttr())
        for (Attribute attribute : interfaces) {
          auto reference = cast<FlatSymbolRefAttr>(attribute);
          classDispatchParentBindings.push_back(
              {classID, dispatchAnalysis.lookup(reference.getValue()).getId(),
               2, 0});
        }
    }
    for (auto method :
         design.getBody().front().getOps<sim::SimClassMethodDeclOp>()) {
      if (!method.getIsVirtual())
        continue;
      uint32_t codeUnitIndex = UINT32_MAX;
      uint32_t flags =
          (method.getIsTask() ? 1u : 0u) | (method.getIsPure() ? 2u : 0u);
      if (auto index = reflectedCodeUnit(method))
        codeUnitIndex = *index;
      if (codeUnitIndex == UINT32_MAX)
        flags |= 4;
      classDispatchMethodBindings.push_back(
          {dispatchAnalysis.lookup(method.getOwner()).getId(),
           *method.getSlot(), *method.getSignatureId(), codeUnitIndex, flags});
    }
    for (auto dispatch : classCallDispatchDeclarations) {
      auto call = symbols.lookupNearestSymbolFrom<sim::SimVPICallDeclOp>(
          dispatch, dispatch.getCallAttr());
      auto method = symbols.lookupNearestSymbolFrom<sim::SimClassMethodDeclOp>(
          dispatch, dispatch.getMethodAttr());
      auto receiver = dispatchAnalysis.lookup(
          cast<sim::ClassHandleType>(dispatch.getReceiverType()));
      // A zero target is the contract header, including an empty pure domain.
      callDispatchBindings.push_back({call.getId(), 0, receiver.getId(),
                                      *method.getSlot(),
                                      *method.getSignatureId()});
      for (auto implementation : dispatchAnalysis.compatibleImplementations(
               receiver, *method.getSlot(), *method.getSignatureId(),
               method.getIsTask())) {
        auto index = reflectedCodeUnit(implementation);
        if (!index)
          continue;
        callDispatchBindings.push_back({call.getId(), objects[*index].id,
                                        receiver.getId(), *method.getSlot(),
                                        *method.getSignatureId()});
      }
    }
    llvm::sort(callDispatchBindings, [](const auto &left, const auto &right) {
      return std::tie(left.callID, left.codeUnitID) <
             std::tie(right.callID, right.codeUnitID);
    });
    llvm::sort(classDispatchMethodBindings,
               [](const auto &left, const auto &right) {
                 return std::tie(left.classID, left.slot, left.signatureID) <
                        std::tie(right.classID, right.slot, right.signatureID);
               });
    llvm::sort(
        classDispatchParentBindings, [](const auto &left, const auto &right) {
          return std::tie(left.classID, left.kind, left.parentClassID) <
                 std::tie(right.classID, right.kind, right.parentClassID);
        });
  }

  if (!interfaceCallDeclarations.empty()) {
    // Source-owned contracts have already proved the original callable ABI.
    // Retain nominal membership and declaration-owned extern implementation
    // identities so the reader can independently derive each call's targets.
    SymbolTableCollection symbols;
    DenseSet<Operation *> requiredMembers;
    for (auto contract : interfaceCallDeclarations)
      requiredMembers.insert(
          symbols.lookupNearestSymbolFrom<sim::SimVPIInterfaceMemberDeclOp>(
              contract, contract.getMemberAttr()));
    struct MemberRecord {
      sim::SimVPIInterfaceMemberDeclOp declaration;
      uint32_t nominal;
    };
    SmallVector<MemberRecord> members;
    DenseSet<uint32_t> nominalTypes;
    auto nominalReference = [&](Operation *source, FlatSymbolRefAttr reference,
                                uint32_t &packed) -> LogicalResult {
      auto type = symbols.lookupNearestSymbolFrom<sim::SimVPITypespecDeclOp>(
          source, reference);
      auto found = typespecRefs.find(type);
      if (found == typespecRefs.end() ||
          !tryPackTableIndex(found->second.table, found->second.index, packed))
        return source->emitOpError("interface nominal type was not serialized");
      return success();
    };
    for (auto member :
         design.getBody().front().getOps<sim::SimVPIInterfaceMemberDeclOp>()) {
      if (!requiredMembers.contains(member))
        continue;
      uint32_t nominal;
      if (failed(
              nominalReference(member, member.getInterfaceTypeAttr(), nominal)))
        return {};
      members.push_back({member, nominal});
      nominalTypes.insert(nominal);
    }
    llvm::sort(members, [](auto left, auto right) {
      return std::make_pair(left.nominal, left.declaration.getName()) <
             std::make_pair(right.nominal, right.declaration.getName());
    });
    DenseMap<std::pair<Attribute, Attribute>, uint32_t> signatureIDs;
    auto internSignature = [&](TypeAttr type, DenseI32ArrayAttr directions) {
      auto [entry, inserted] = signatureIDs.try_emplace(
          std::make_pair(Attribute(type), Attribute(directions)),
          static_cast<uint32_t>(signatureIDs.size()) + 1);
      return entry->second;
    };
    DenseMap<Operation *, uint32_t> memberIndices;
    for (auto [index, record] : llvm::enumerate(members)) {
      auto member = record.declaration;
      memberIndices[member] = static_cast<uint32_t>(index);
      interfaceCallables.push_back(
          {record.nominal, member.getName(),
           static_cast<uint16_t>(member.getMemberKind() == sim::EntryKind::Task
                                     ? VPIObjectKind::Task
                                     : VPIObjectKind::Function),
           static_cast<uint16_t>(member.getIsExtern()),
           internSignature(member.getSignatureAttr(),
                           member.getDirectionsAttr())});
    }
    for (auto instance : design.getBody()
                             .front()
                             .getOps<sim::SimVPIInterfaceInstanceBindingOp>()) {
      uint32_t nominal;
      if (failed(nominalReference(instance, instance.getInterfaceTypeAttr(),
                                  nominal)))
        return {};
      if (!nominalTypes.contains(nominal))
        continue;
      auto scope = scopeIndices.find(instance.getScopeId());
      if (scope == scopeIndices.end()) {
        instance.emitOpError("interface instance scope was not serialized");
        return {};
      }
      interfaceInstances.push_back({scope->second, nominal});
    }
    DenseSet<uint32_t> requiredSignatureUnits;
    for (auto callable :
         design.getBody().front().getOps<sim::SimVPICallableDeclOp>()) {
      if (!callable.getInterfaceMemberAttr())
        continue;
      auto member =
          symbols.lookupNearestSymbolFrom<sim::SimVPIInterfaceMemberDeclOp>(
              callable, callable.getInterfaceMemberAttr());
      auto index = memberIndices.find(member);
      if (index == memberIndices.end())
        continue;
      auto declaration = codeUnitObjectIndices.find(callable.getCodeUnitId());
      // Ordinary extern calls execute the resolved provider directly. An extern
      // fork/join executes its declared aggregation unit, including zero
      // exports.
      uint64_t implementationID = callable.getCodeUnitId();
      if (auto exports = callable.getInterfaceExports();
          exports && !callable.getIsForkJoin())
        implementationID = exports->front();
      auto implementation = codeUnitObjectIndices.find(implementationID);
      if (declaration == codeUnitObjectIndices.end() ||
          implementation == codeUnitObjectIndices.end()) {
        callable.emitOpError("interface callable endpoint was not serialized");
        return {};
      }
      uint32_t scope = scopeIndices.lookup(objects[declaration->second].scope);
      interfaceCallableBindings.push_back(
          {index->second, scope, declaration->second, implementation->second});
      interfaceDeclarationIDs.insert(callable.getCodeUnitId());
      requiredSignatureUnits.insert(declaration->second);
      requiredSignatureUnits.insert(implementation->second);
    }
    // Source ABI facts are emitted from declarations, independently of the
    // selected implementation rows. Omit facts with no dispatch endpoint.
    for (auto callable :
         design.getBody().front().getOps<sim::SimVPICallableDeclOp>()) {
      auto unit = codeUnitObjectIndices.find(callable.getCodeUnitId());
      if (unit == codeUnitObjectIndices.end() ||
          !requiredSignatureUnits.contains(unit->second))
        continue;
      callableSignatureBindings.push_back(
          {unit->second,
           internSignature(callable.getSignatureAttr(),
                           callable.getDirectionsAttr()),
           (callable.getInterfaceExportsAttr() ? 1u : 0u) |
               (callable.getIsForkJoin() ? 2u : 0u)});
    }
    llvm::sort(callableSignatureBindings,
               [](const auto &left, const auto &right) {
                 return left.unit < right.unit;
               });
    for (auto contract : interfaceCallDeclarations) {
      auto call = symbols.lookupNearestSymbolFrom<sim::SimVPICallDeclOp>(
          contract, contract.getCallAttr());
      auto member =
          symbols.lookupNearestSymbolFrom<sim::SimVPIInterfaceMemberDeclOp>(
              contract, contract.getMemberAttr());
      interfaceCallBindings.push_back(
          {call.getId(), memberIndices.lookup(member), contract.getAccess()});
    }
    llvm::sort(interfaceInstances, [](const auto &left, const auto &right) {
      return left.scope < right.scope;
    });
    llvm::sort(interfaceCallableBindings,
               [](const auto &left, const auto &right) {
                 return std::tie(left.member, left.scope) <
                        std::tie(right.member, right.scope);
               });
    llvm::sort(interfaceCallBindings, [](const auto &left, const auto &right) {
      return left.id < right.id;
    });
  }

  for (sim::SimVPIStatementRelationDeclOp relation : relationDeclarations) {
    TableKind sourceTable = TableKind::Scope;
    uint32_t sourceIndex = 0;
    switch (relation.getSourceKind()) {
    case sim::VPIStatementSourceKind::Scope: {
      auto source = scopeIndices.find(relation.getSourceId());
      if (source == scopeIndices.end()) {
        relation.emitOpError("relation source scope was not serialized");
        return {};
      }
      sourceIndex = source->second;
      break;
    }
    case sim::VPIStatementSourceKind::CodeUnit: {
      sourceTable = TableKind::Object;
      auto source = codeUnitObjectIndices.find(relation.getSourceId());
      if (source == codeUnitObjectIndices.end()) {
        relation.emitOpError("relation source code unit was not serialized");
        return {};
      }
      sourceIndex = source->second;
      break;
    }
    case sim::VPIStatementSourceKind::Statement: {
      sourceTable = TableKind::Statement;
      auto source = statementIndices.find(relation.getSourceId());
      if (source == statementIndices.end()) {
        relation.emitOpError("relation source statement was not serialized");
        return {};
      }
      sourceIndex = source->second;
      break;
    }
    case sim::VPIStatementSourceKind::Anchor: {
      auto source = anchorsByInventoryId.find(relation.getSourceId());
      if (source == anchorsByInventoryId.end()) {
        relation.emitOpError("relation source VPI anchor was not serialized");
        return {};
      }
      auto reference = anchorRefs.find(source->second);
      if (reference == anchorRefs.end()) {
        relation.emitOpError(
            "relation source VPI anchor has no image object reference");
        return {};
      }
      sourceTable = reference->second.table;
      sourceIndex = reference->second.index;
      break;
    }
    }
    auto target = statementIndices.find(relation.getTargetStatementId());
    if (target == statementIndices.end()) {
      relation.emitOpError("relation target statement was not serialized");
      return {};
    }
    uint32_t targetIndexAndTable = 0;
    if (!tryPackTableIndex(TableKind::Statement, target->second,
                           targetIndexAndTable)) {
      relation.emitOpError("relation target table index cannot be packed");
      return {};
    }
    for (uint32_t mode = 0; mode != 2; ++mode) {
      if ((relation.getModeMask() & (uint32_t{1} << mode)) == 0)
        continue;
      bool iterate = mode != 0;
      uint16_t sourceKindAndTable = 0;
      if (!tryPackRelationSource(sourceTable, relation.getSourceVpiKind(),
                                 iterate, sourceKindAndTable)) {
        relation.emitOpError("relation source VPI kind cannot be packed");
        return {};
      }
      if (relations.size() == UINT32_MAX) {
        relation.emitOpError(
            "reflection relation table exceeds 32-bit indices");
        return {};
      }
      relations.push_back(
          {sourceTable, sourceIndex, iterate, targetIndexAndTable,
           static_cast<uint32_t>(relation.getOrdinal()),
           static_cast<uint16_t>(relation.getSelector()), sourceKindAndTable});
    }
  }
  DenseMap<uint64_t, ImageObjectRef> initializerScopes;
  if (includeStatements) {
    DenseMap<uint32_t, ImageObjectRef> sourceScopes;
    for (const RelationRecord &relation : relations) {
      if (relation.iterate ||
          relation.selector != static_cast<uint32_t>(VPIRelationKind::ScopeRel))
        continue;
      uint32_t source = 0;
      if (!tryPackTableIndex(relation.sourceTable, relation.sourceIndex,
                             source))
        return {};
      auto table = unpackTableIndexKind(relation.targetIndexAndTable);
      auto index = unpackTableIndex(relation.targetIndexAndTable);
      uint32_t kind =
          table == TableKind::Scope ? sim::vpiKindForScope(scopes[index])
          : table == TableKind::Statement
              ? statements[index].declaration.getVpiKind()
          : table == TableKind::StaticObject ? staticObjects[index].vpiKind
                                             : objects[index].vpiKind;
      sourceScopes.try_emplace(source, ImageObjectRef{table, index, kind});
    }
    DenseMap<uint64_t, ImageObjectRef> initializerSources = storageRefs;
    for (sim::SimVPIClassMemberDeclOp member : classMembers)
      if (auto storage = member.getStaticStorage();
          storage && typespecsBySymbol.contains(member.getOwner()))
        initializerSources[*storage] = classMemberRefs.lookup(member);
    for (auto [storage, anchor] : eventArrayStorage)
      initializerSources[cast<sim::SimStorageDeclOp>(storage).getId()] =
          anchorRefs.lookup(anchor);
    for (auto binding :
         design.getBody().front().getOps<sim::SimVPIInitializerBindingOp>()) {
      auto source = initializerSources.find(binding.getStorageId());
      uint32_t packed = 0;
      if (source == initializerSources.end() ||
          !tryPackTableIndex(source->second.table, source->second.index,
                             packed) ||
          !sourceScopes.contains(packed)) {
        binding.emitOpError(
            "initializer source variable or lexical scope was not serialized");
        return {};
      }
      initializerBindings.push_back({binding.getCodeUnitId(), packed, 0});
      initializerScopes.try_emplace(binding.getCodeUnitId(),
                                    sourceScopes.lookup(packed));
    }
    llvm::sort(initializerBindings, [](auto left, auto right) {
      return left.codeUnitID < right.codeUnitID;
    });
  }

  for (auto [ordinal, call] : llvm::enumerate(callDeclarations)) {
    const auto binding = callSiteBindings[ordinal];
    TableKind sourceTable = unpackTableIndexKind(binding.object);
    uint32_t sourceIndex = unpackTableIndex(binding.object);
    const uint32_t callKind = call.getVpiKind();
    // The nearest retained scope owns the occurrence; enclosing expressions
    // and composite statements do not become false lexical scopes.
    TableKind scopeTable = TableKind::Scope;
    uint32_t scopeIndex = scopeIndices.lookup(call.getScopeId());
    uint32_t scopeKind = sim::vpiKindForScope(scopes[scopeIndex]);
    for (auto enclosing = call.getEnclosingStatementId(); enclosing;) {
      uint32_t index = statementIndices.lookup(*enclosing);
      auto statement = statements[index].declaration;
      if (statement.getIsScope()) {
        scopeTable = TableKind::Statement;
        scopeIndex = index;
        scopeKind = statement.getVpiKind();
        break;
      }
      enclosing = statement.getParentId();
    }
    if (scopeTable == TableKind::Scope) {
      auto source = codeUnitSourceRefs.find(call.getCodeUnitId());
      ImageObjectRef ownerRef =
          source != codeUnitSourceRefs.end()
              ? source->second
              : ImageObjectRef{TableKind::Object, binding.owner,
                               objects[binding.owner].vpiKind};
      const auto *owner = findVPIObjectKind(ownerRef.vpiKind);
      if (owner && (owner->families & vpiFamilyMask(VPIObjectFamily::Scope))) {
        scopeTable = ownerRef.table;
        scopeIndex = ownerRef.index;
        scopeKind = ownerRef.vpiKind;
      }
    }
    if (auto initializer = initializerScopes.find(call.getCodeUnitId());
        initializer != initializerScopes.end()) {
      scopeTable = initializer->second.table;
      scopeIndex = initializer->second.index;
      scopeKind = initializer->second.vpiKind;
    }
    if (findVPITraversal(callKind,
                         static_cast<uint32_t>(VPIRelationKind::ScopeRel),
                         VPITraversalMode::Handle) &&
        failed(addRelation(call, sourceTable, sourceIndex, callKind,
                           static_cast<uint32_t>(VPIRelationKind::ScopeRel),
                           VPITraversalMode::Handle, scopeTable, scopeIndex,
                           scopeKind)))
      return {};
    if (auto calleeID = call.getCalleeCodeUnitId()) {
      uint32_t callee = codeUnitObjectIndices.lookup(*calleeID);
      auto mapped = codeUnitSourceRefs.find(*calleeID);
      ImageObjectRef target = mapped != codeUnitSourceRefs.end()
                                  ? mapped->second
                                  : ImageObjectRef{TableKind::Object, callee,
                                                   objects[callee].vpiKind};
      if (failed(addRelation(call, sourceTable, sourceIndex, callKind,
                             target.vpiKind, VPITraversalMode::Handle,
                             target.table, target.index, target.vpiKind)))
        return {};
    }
  }

  for (auto declaration : foreachDeclarations) {
    uint32_t statement = statementIndices.lookup(declaration.getStatementId());
    uint32_t foreachKind = static_cast<uint32_t>(VPIObjectKind::ForeachStmt);
    ImageObjectRef collection;
    if (declaration.getCollection().getKind() ==
        sim::VPIObjectRefKind::Storage) {
      auto found = storageRefs.find(
          declaration.getCollection().getId().getValue().getZExtValue());
      if (found == storageRefs.end()) {
        declaration.emitOpError("foreach collection was not serialized");
        return {};
      }
      collection = found->second;
    } else {
      auto local = localsBySymbol.lookup(
          declaration.getCollection().getSymbol().getValue());
      if (!local || !objectIndices.contains(local)) {
        declaration.emitOpError("foreach local collection was not serialized");
        return {};
      }
      collection = {TableKind::Object, objectIndices.lookup(local),
                    sim::vpiKindForClassMember(local.getVpiType())};
    }
    if (failed(addRelation(declaration, TableKind::Statement, statement,
                           foreachKind,
                           static_cast<uint32_t>(VPIRelationKind::VariablesRel),
                           VPITraversalMode::Iterate, collection.table,
                           collection.index, collection.vpiKind, 0)))
      return {};
    for (auto [ordinal, index] :
         llvm::enumerate(declaration.getLoopVariables())) {
      ImageObjectRef target;
      if (auto symbol = dyn_cast<FlatSymbolRefAttr>(index)) {
        auto local = localsBySymbol.lookup(symbol.getValue());
        target = {TableKind::Object, objectIndices.lookup(local),
                  sim::vpiKindForClassMember(local.getVpiType())};
      } else {
        target = {TableKind::StaticObject,
                  foreachNullIndices.lookup(
                      foreachNullIDs.lookup({declaration, ordinal})),
                  static_cast<uint32_t>(VPIObjectKind::Operation)};
      }
      if (failed(addRelation(
              declaration, TableKind::Statement, statement, foreachKind,
              static_cast<uint32_t>(VPIRelationKind::LoopVarsRel),
              VPITraversalMode::Iterate, target.table, target.index,
              target.vpiKind, ordinal)))
        return {};
    }
  }

  // Explicit graph edges cannot replace or extend a slice owned by another
  // inventory producer. In particular, typespec owners, aliases, and lexical
  // anchors synthesize relations that are not all tagged automatic in the LRM
  // traversal model. Check the final physical identities, including backings.
  DenseSet<uint64_t> synthesizedRelationGroups;
  for (const RelationRecord &relation : relations)
    synthesizedRelationGroups.insert(
        relationIdentity(relation.sourceIndex, relation.selector,
                         relation.sourceKindAndTable, 0)
            .first);
  auto resolvePhysicalReference =
      [&](Operation *use, sim::VPIObjectRefAttr reference,
          StringRef endpoint) -> std::optional<ImageObjectRef> {
    uint64_t id =
        reference.getId() ? reference.getId().getValue().getZExtValue() : 0;
    std::optional<ImageObjectRef> resolved;
    switch (reference.getKind()) {
    case sim::VPIObjectRefKind::Statement: {
      auto found = statementIndices.find(id);
      if (found != statementIndices.end())
        resolved = ImageObjectRef{TableKind::Statement, found->second,
                                  statementKinds.lookup(id)};
      break;
    }
    case sim::VPIObjectRefKind::Storage: {
      auto found = storageRefs.find(id);
      if (found != storageRefs.end())
        resolved = found->second;
      break;
    }
    case sim::VPIObjectRefKind::Net: {
      auto found = netRefs.find(id);
      if (found != netRefs.end())
        resolved = found->second;
      break;
    }
    case sim::VPIObjectRefKind::NetIdentity: {
      auto found = netIdentityRefs.find(id);
      if (found != netIdentityRefs.end())
        resolved = found->second;
      break;
    }
    case sim::VPIObjectRefKind::Port: {
      auto found = portRefs.find(id);
      if (found != portRefs.end())
        resolved = found->second;
      break;
    }
    case sim::VPIObjectRefKind::DefinitionMember:
      break;
    case sim::VPIObjectRefKind::Symbol: {
      StringRef name = reference.getSymbol().getValue();
      if (auto anchor = anchorsBySymbol.find(name);
          anchor != anchorsBySymbol.end())
        resolved = anchorRefs.lookup(anchor->second);
      else if (auto typespec = typespecsBySymbol.find(name);
               typespec != typespecsBySymbol.end())
        resolved = typespecRefs.lookup(typespec->second);
      else if (auto source = sourceInheritanceBySymbol.find(name);
               source != sourceInheritanceBySymbol.end())
        resolved = sourceInheritanceRefs.lookup(source->second);
      else if (auto alias = unresolvedAliasesBySymbol.find(name);
               alias != unresolvedAliasesBySymbol.end())
        resolved = unresolvedAliasRefs.lookup(alias->second);
      else if (auto nettype = nettypesBySymbol.find(name);
               nettype != nettypesBySymbol.end())
        resolved = nettypeRefs.lookup(nettype->second);
      else if (auto member = classMembersBySymbol.find(name);
               member != classMembersBySymbol.end())
        resolved = classMemberRefs.lookup(member->second);
      else if (auto call = callRefsBySymbol.find(name);
               call != callRefsBySymbol.end())
        resolved = call->second.getStatementId()
                       ? ImageObjectRef{TableKind::Statement,
                                        statementIndices.lookup(
                                            *call->second.getStatementId()),
                                        call->second.getVpiKind()}
                       : ImageObjectRef{TableKind::Object,
                                        objectIndices.lookup(call->second),
                                        call->second.getVpiKind()};
      else if (auto local = localsBySymbol.find(name);
               local != localsBySymbol.end())
        resolved = ImageObjectRef{
            TableKind::Object, objectIndices.lookup(local->second),
            sim::vpiKindForClassMember(local->second.getVpiType())};
      break;
    }
    }
    if (!resolved)
      use->emitOpError() << endpoint << " VPI reference " << reference
                         << " was not serialized";
    return resolved;
  };
  auto expressionFor = [&](sim::VPIObjectRefAttr reference) {
    return reference.getKind() == sim::VPIObjectRefKind::Symbol
               ? expressionsBySymbol.lookup(reference.getSymbol().getValue())
               : sim::SimVPIExpressionDeclOp{};
  };
  auto isCaseLabel = [&](sim::SimVPIRelationDeclOp relation) {
    auto source = relation.getSource();
    if (source.getKind() != sim::VPIObjectRefKind::Statement ||
        relation.getSelector() != uint32_t(VPIRelationKind::ExprRel) ||
        relation.getMode() != sim::VPIRelationMode::Iterate)
      return false;
    auto statement =
        statementIndices.find(source.getId().getValue().getZExtValue());
    return statement != statementIndices.end() &&
           statements[statement->second].declaration.getVpiKind() ==
               uint32_t(VPIObjectKind::CaseItem);
  };
  auto isCallArgument = [&](sim::SimVPIRelationDeclOp relation) {
    if (relation.getSelector() != uint32_t(VPIRelationKind::ArgumentRel) ||
        relation.getMode() != sim::VPIRelationMode::Iterate)
      return false;
    auto source = relation.getSource();
    if (source.getKind() != sim::VPIObjectRefKind::Symbol)
      return false;
    auto found = callRefsBySymbol.find(source.getSymbol().getValue());
    return found != callRefsBySymbol.end() &&
           (found->second.getVpiKind() == uint32_t(VPIObjectKind::FuncCall) ||
            found->second.getVpiKind() == uint32_t(VPIObjectKind::TaskCall));
  };
  auto expressionUseSource = [&](sim::SimVPIRelationDeclOp relation) {
    if (!isCaseLabel(relation))
      return relation.getSource();
    auto index = statementIndices.lookup(
        relation.getSource().getId().getValue().getZExtValue());
    auto parent = statements[index].declaration.getParentIdAttr();
    return parent ? sim::VPIObjectRefAttr::get(relation.getContext(),
                                               sim::VPIObjectRefKind::Statement,
                                               parent)
                  : sim::VPIObjectRefAttr{};
  };
  DenseSet<Attribute> compactExpressionGroups;
  DenseSet<std::pair<Attribute, Attribute>> caseTargets;
  for (auto relation : generalRelationDeclarations)
    if ((isCaseLabel(relation) || isCallArgument(relation)) &&
        (!caseTargets.insert({relation.getSource(), relation.getTarget()})
              .second ||
         expressionFor(relation.getTarget()) ||
         relation.getTarget().getKind() ==
             sim::VPIObjectRefKind::DefinitionMember))
      compactExpressionGroups.insert(relation.getSource());
  auto definitionMemberFor = [&](sim::VPIObjectRefAttr reference) {
    return reference.getKind() == sim::VPIObjectRefKind::Symbol
               ? definitionMembersBySymbol.lookup(
                     reference.getSymbol().getValue())
               : sim::SimVPIDefinitionMemberDeclOp{};
  };
  for (sim::SimVPIRelationDeclOp relation : generalRelationDeclarations) {
    if (relation.getSource().getKind() ==
        sim::VPIObjectRefKind::DefinitionMember) {
      // Whole contextual declaration Uses are derived from the independently
      // validated forward root. They need no per-instance reverse record.
      bool exactUse =
          relation.getSelector() == uint32_t(VPIRelationKind::UseRel) &&
          relation.getMode() == sim::VPIRelationMode::Iterate &&
          llvm::any_of(generalRelationDeclarations, [&](auto forward) {
            return expressionUseSource(forward) == relation.getTarget() &&
                   forward.getTarget() == relation.getSource() &&
                   (forward.getMode() == sim::VPIRelationMode::Handle ||
                    isCaseLabel(forward) || isCallArgument(forward));
          });
      if (!exactUse) {
        relation.emitOpError("contextual declaration relation requires an "
                             "exact expression root");
        return {};
      }
      continue;
    }
    if (definitionMemberFor(relation.getSource())) {
      expressionRootDeclarations.push_back(relation);
      continue;
    }
    if (auto expression = expressionFor(relation.getSource())) {
      expressionChildren[expression].push_back(relation);
      continue;
    }
    if (((isCaseLabel(relation) || isCallArgument(relation)) &&
         compactExpressionGroups.contains(relation.getSource())) ||
        expressionFor(relation.getTarget()) ||
        relation.getTarget().getKind() ==
            sim::VPIObjectRefKind::DefinitionMember) {
      expressionRootDeclarations.push_back(relation);
      continue;
    }
    std::optional<ImageObjectRef> source =
        resolvePhysicalReference(relation, relation.getSource(), "source");
    std::optional<ImageObjectRef> target =
        resolvePhysicalReference(relation, relation.getTarget(), "target");
    if (!source || !target)
      return {};
    auto mode = static_cast<VPITraversalMode>(
        static_cast<uint32_t>(relation.getMode()));
    uint16_t packedSource = 0;
    if (!tryPackRelationSource(source->table, source->vpiKind,
                               mode == VPITraversalMode::Iterate,
                               packedSource)) {
      relation.emitOpError("relation source VPI kind cannot be packed");
      return {};
    }
    if (synthesizedRelationGroups.contains(
            relationIdentity(source->index, relation.getSelector(),
                             packedSource, 0)
                .first)) {
      relation.emitOpError(
          "relation source, selector, and access mode are already supplied "
          "by design inventory");
      return {};
    }
    if (failed(addRelation(relation, source->table, source->index,
                           source->vpiKind, relation.getSelector(), mode,
                           target->table, target->index, target->vpiKind,
                           static_cast<uint32_t>(relation.getOrdinal()))))
      return {};
  }
  if (relations.size() > UINT32_MAX) {
    design.emitOpError("reflection relation table exceeds 32-bit indices");
    return {};
  }
  llvm::sort(relations, [](const RelationRecord &left,
                           const RelationRecord &right) {
    return std::tie(left.sourceTable, left.sourceIndex, left.selector,
                    left.iterate, left.ordinal, left.targetIndexAndTable) <
           std::tie(right.sourceTable, right.sourceIndex, right.selector,
                    right.iterate, right.ordinal, right.targetIndexAndTable);
  });

  struct RelationIndexRecord {
    uint32_t objectIndex = 0;
    uint32_t firstDimension = UINT32_MAX;
    uint16_t dimensionCount = 0;
    uint16_t flags = 0;
    uint32_t firstKey = UINT32_MAX;
    uint32_t firstOrdinalKey = UINT32_MAX;
  };
  struct RelationIndexDimensionRecord {
    int64_t left = 0;
    int64_t right = 0;
  };
  struct RelationIndexKeyRecord {
    int64_t index = 0;
    uint32_t ordinal = 0;
  };
  struct RelationIndexMemberRecord {
    uint32_t targetIndexAndTable = 0;
    uint32_t relationIndex = 0;
    uint32_t ordinal = 0;
  };
  SmallVector<RelationIndexRecord> relationIndices;
  SmallVector<RelationIndexDimensionRecord> relationIndexDimensions;
  SmallVector<RelationIndexKeyRecord> relationIndexKeys;
  SmallVector<RelationIndexMemberRecord> relationIndexMembers;
  for (sim::SimVPIObjectAnchorOp anchor : anchors) {
    DenseI64ArrayAttr ranges = anchor.getIndexRangesAttr();
    DenseI64ArrayAttr sparse = anchor.getSparseIndicesAttr();
    if (!ranges && !sparse)
      continue;
    auto object = objectIndices.find(anchor);
    if (object == objectIndices.end()) {
      anchor.emitOpError("relation-indexed VPI array was not serialized");
      return {};
    }
    const auto *access = findVPIIndexedAccess(anchor.getVpiKind());
    if (!access ||
        access->accessKind != VPIIndexedAccessKind::RelationElement) {
      anchor.emitOpError(
          "relation-indexed VPI array has no generated access policy");
      return {};
    }
    RelationIndexRecord record;
    record.objectIndex = object->second;
    uint64_t expectedElements = 0;
    if (ranges) {
      ArrayRef<int64_t> values = ranges.asArrayRef();
      if (values.empty() || values.size() % 2 != 0 ||
          values.size() / 2 > UINT16_MAX ||
          relationIndexDimensions.size() > UINT32_MAX) {
        anchor.emitOpError("fixed VPI array dimensions cannot be encoded");
        return {};
      }
      record.firstDimension =
          static_cast<uint32_t>(relationIndexDimensions.size());
      record.dimensionCount = static_cast<uint16_t>(values.size() / 2);
      expectedElements = 1;
      for (size_t offset = 0; offset != values.size(); offset += 2) {
        uint64_t ordinal = 0, extent = 0;
        if (!declaredIndexOrdinal(values[offset], values[offset + 1],
                                  values[offset], ordinal, extent) ||
            extent > UINT32_MAX || expectedElements > UINT32_MAX / extent) {
          anchor.emitOpError("fixed VPI array extent exceeds relation image");
          return {};
        }
        relationIndexDimensions.push_back({values[offset], values[offset + 1]});
        expectedElements *= extent;
      }
    } else {
      ArrayRef<int64_t> values = sparse.asArrayRef();
      if (values.size() > UINT32_MAX ||
          relationIndexKeys.size() > UINT32_MAX - values.size() ||
          relationIndexKeys.size() + values.size() >
              UINT32_MAX - values.size()) {
        anchor.emitOpError("sparse VPI array indices cannot be encoded");
        return {};
      }
      expectedElements = values.size();
      record.dimensionCount = 1;
      record.flags = 1;
      record.firstOrdinalKey = static_cast<uint32_t>(relationIndexKeys.size());
      for (auto [ordinal, index] : llvm::enumerate(values))
        relationIndexKeys.push_back({index, static_cast<uint32_t>(ordinal)});
      record.firstKey = static_cast<uint32_t>(relationIndexKeys.size());
      for (auto [ordinal, index] : llvm::enumerate(values))
        relationIndexKeys.push_back({index, static_cast<uint32_t>(ordinal)});
      llvm::MutableArrayRef<RelationIndexKeyRecord> keys =
          llvm::MutableArrayRef(relationIndexKeys)
              .slice(record.firstKey, values.size());
      llvm::sort(keys, [](const RelationIndexKeyRecord &left,
                          const RelationIndexKeyRecord &right) {
        return left.index < right.index;
      });
      for (size_t index = 1; index < keys.size(); ++index)
        if (keys[index - 1].index == keys[index].index) {
          anchor.emitOpError("sparse VPI array contains a duplicate index");
          return {};
        }
    }

    auto firstRelation = std::lower_bound(
        relations.begin(), relations.end(), record.objectIndex,
        [&](const RelationRecord &relation, uint32_t objectIndex) {
          return std::tie(relation.sourceTable, relation.sourceIndex,
                          relation.selector, relation.iterate) <
                 std::tuple(TableKind::Object, objectIndex,
                            static_cast<uint16_t>(access->relationSelector),
                            true);
        });
    uint32_t relationCount = 0;
    for (auto relation = firstRelation;
         relation != relations.end() &&
         relation->sourceTable == TableKind::Object &&
         relation->sourceIndex == record.objectIndex && relation->iterate &&
         relation->selector == access->relationSelector;
         ++relation) {
      if (relation->ordinal != relationCount) {
        anchor.emitOpError(
            "VPI array relations are not dense in declared index order");
        return {};
      }
      ++relationCount;
    }
    if (relationCount != expectedElements) {
      anchor.emitOpError("VPI array relation count does not match its shape");
      return {};
    }
    relationIndices.push_back(record);
  }
  llvm::sort(relationIndices, [](const RelationIndexRecord &left,
                                 const RelationIndexRecord &right) {
    return left.objectIndex < right.objectIndex;
  });
  for (size_t index = 1; index < relationIndices.size(); ++index)
    if (relationIndices[index - 1].objectIndex ==
        relationIndices[index].objectIndex) {
      design.emitOpError("duplicate relation index for a VPI array object");
      return {};
    }
  for (auto [relationIndex, entry] : llvm::enumerate(relationIndices)) {
    uint32_t sourceKind = objects[entry.objectIndex].vpiKind;
    const auto *access = findVPIIndexedAccess(sourceKind);
    auto firstRelation = std::lower_bound(
        relations.begin(), relations.end(), entry.objectIndex,
        [&](const RelationRecord &relation, uint32_t objectIndex) {
          return std::tie(relation.sourceTable, relation.sourceIndex,
                          relation.selector, relation.iterate) <
                 std::tuple(TableKind::Object, objectIndex,
                            static_cast<uint16_t>(access->relationSelector),
                            true);
        });
    for (auto relation = firstRelation;
         relation != relations.end() &&
         relation->sourceTable == TableKind::Object &&
         relation->sourceIndex == entry.objectIndex && relation->iterate &&
         relation->selector == access->relationSelector;
         ++relation)
      relationIndexMembers.push_back({relation->targetIndexAndTable,
                                      static_cast<uint32_t>(relationIndex),
                                      relation->ordinal});
  }
  llvm::sort(relationIndexMembers, [](const RelationIndexMemberRecord &left,
                                      const RelationIndexMemberRecord &right) {
    return left.targetIndexAndTable < right.targetIndexAndTable;
  });
  for (size_t index = 1; index < relationIndexMembers.size(); ++index)
    if (relationIndexMembers[index - 1].targetIndexAndTable ==
        relationIndexMembers[index].targetIndexAndTable) {
      design.emitOpError("VPI object belongs to multiple static arrays");
      return {};
    }

  struct TypeRecord {
    uint32_t kind = 0;
    uint32_t flags = 0;
    uint64_t width = 0;
    int64_t left = 0;
    int64_t right = 0;
    uint32_t element = UINT32_MAX;
    uint32_t firstChild = UINT32_MAX;
    uint64_t childCount = 0;
    uint64_t ordinal = 0;
    uint64_t packedOffset = 0;
    std::string name;
  };
  SmallVector<TypeRecord> types;
  DenseMap<Type, uint32_t> typeIndices;
  bool typeError = false;
  std::function<std::optional<uint32_t>(Type)> addType =
      [&](Type type) -> std::optional<uint32_t> {
    if (auto found = typeIndices.find(type); found != typeIndices.end())
      return found->second;
    std::optional<uint32_t> width = simulationWidth(type);
    if (!width) {
      typeError = true;
      return std::nullopt;
    }
    uint32_t index = types.size();
    typeIndices[type] = index;
    types.emplace_back();
    TypeRecord &record = types[index];
    record.width = *width;
    record.left = static_cast<int64_t>(*width - 1);
    record.right = 0;
    if (containsLogic(type))
      record.flags |= 1;
    if (auto integer = dyn_cast<IntegerType>(type)) {
      record.kind = 1;
      record.flags |= integer.isSigned() ? 2 : 0;
      record.flags |= 4;
      record.name = "bits";
    } else if (type.isF32()) {
      record.kind = 1;
      record.name = "shortreal";
    } else if (type.isF64()) {
      record.kind = 1;
      record.name = "real";
    } else if (isa<sim::LogicType>(type)) {
      record.kind = 1;
      record.flags |= 4;
      record.name = "logic";
    } else if (isa<sim::StringType>(type)) {
      record.kind = 1;
      record.name = "string";
    } else if (isa<sim::ClassHandleType>(type)) {
      record.kind = 1;
      record.name = "class";
    } else if (sim::isManagedHandleType(type) ||
               sim::isSimulationHandleType(type)) {
      record.kind = 1;
      record.name = "handle";
    } else if (auto packed = dyn_cast<sim::PackedArrayType>(type)) {
      record.kind = 2;
      record.flags |= 4;
      record.left = packed.getLeft();
      record.right = packed.getRight();
      record.name = "packed_array";
      auto element = addType(packed.getElementType());
      if (!element)
        return std::nullopt;
      types[index].element = *element;
    } else if (auto unpacked = dyn_cast<sim::UnpackedArrayType>(type)) {
      record.kind = 2;
      record.left = unpacked.getLeft();
      record.right = unpacked.getRight();
      record.name = "unpacked_array";
      auto element = addType(unpacked.getElementType());
      if (!element)
        return std::nullopt;
      types[index].element = *element;
    } else {
      ArrayAttr fields;
      bool packed = false;
      bool tagged = false;
      uint64_t tagBits = 0;
      if (auto value = dyn_cast<sim::PackedStructType>(type)) {
        record.kind = 3;
        packed = true;
        fields = value.getFields();
        record.name = "packed_struct";
      } else if (auto value = dyn_cast<sim::UnpackedStructType>(type)) {
        record.kind = 3;
        fields = value.getFields();
        record.name = "unpacked_struct";
      } else if (auto value = dyn_cast<sim::PackedUnionType>(type)) {
        record.kind = 4;
        packed = true;
        tagged = value.getIsTagged();
        tagBits = value.getTagBits();
        fields = value.getFields();
        record.name = "packed_union";
      } else if (auto value = dyn_cast<sim::UnpackedUnionType>(type)) {
        record.kind = 4;
        tagged = value.getIsTagged();
        fields = value.getFields();
        if (tagged)
          tagBits =
              llvm::Log2_64_Ceil(static_cast<uint64_t>(fields.size()) + 1);
        record.name = "unpacked_union";
      } else {
        typeError = true;
        return std::nullopt;
      }
      if (packed)
        types[index].flags |= 4;
      if (tagged)
        types[index].flags |= 8;
      types[index].ordinal = tagBits;
      types[index].firstChild = types.size();
      types[index].childCount = fields.size();
      for (size_t field = 0; field != fields.size(); ++field)
        types.emplace_back();
      for (auto [ordinal, attribute] : llvm::enumerate(fields)) {
        auto field = dyn_cast<sim::FieldAttr>(attribute);
        if (!field) {
          typeError = true;
          return std::nullopt;
        }
        auto element = addType(field.getType());
        if (!element)
          return std::nullopt;
        TypeRecord &child = types[types[index].firstChild + ordinal];
        child.kind = 5;
        child.flags = containsLogic(field.getType()) ? 1 : 0;
        child.width = *simulationWidth(field.getType());
        child.left = static_cast<int64_t>(child.width - 1);
        child.right = 0;
        child.element = *element;
        child.ordinal = ordinal;
        if (auto subelement = sim::getAggregateProvenanceSubelement(
                type, static_cast<unsigned>(ordinal)))
          child.packedOffset = subelement->first;
        child.name = field.getName().getValue().str();
      }
    }
    return index;
  };
  for (const Record &object : objects)
    if (object.type && !addType(object.type))
      return {};
  if (typeError)
    return {};

  llvm::StringMap<ImageObjectRef> semanticIdentityObjects;
  llvm::StringMap<ImageObjectRef> classSpecializationObjects;
  DenseMap<Attribute, ImageObjectRef> builtinClassObjects;
  for (sim::SimVPIObjectAnchorOp anchor : anchors) {
    sim::VPIObjectBackingAttr backing = anchor.getBackingAttr();
    if (!backing || backing.getKind() != sim::VPIObjectBackingKind::Class ||
        !backing.getSymbol())
      continue;
    auto reference = anchorRefs.find(anchor);
    if (reference == anchorRefs.end()) {
      anchor.emitOpError("canonical VPI semantic identity was not serialized");
      return {};
    }
    semanticIdentityObjects[backing.getSymbol().getValue()] = reference->second;
  }
  for (sim::SimVPITypespecDeclOp typespec : typespecs) {
    sim::VPITypeSemanticsAttr semantic = typespec.getTargetType();
    if (typespec.getOrigin() == sim::VPITypespecOrigin::BuiltinClass) {
      auto reference = typespecRefs.find(typespec);
      if (reference == typespecRefs.end()) {
        typespec.emitOpError("canonical builtin class was not serialized");
        return {};
      }
      builtinClassObjects[sim::stripVPITypedefAliases(semantic)] =
          reference->second;
      continue;
    }
    if (typespec.getOrigin() == sim::VPITypespecOrigin::ClassSpecialization) {
      auto owner = anchorsBySymbol.find(typespec.getOwnerAttr().getValue());
      auto reference = typespecRefs.find(typespec);
      if (owner == anchorsBySymbol.end() || reference == typespecRefs.end()) {
        typespec.emitOpError(
            "canonical class specialization was not serialized");
        return {};
      }
      StringRef identity = semantic.getSymbol().getRootReference().getValue();
      classSpecializationObjects[identity] = reference->second;
      // Keep class semantic identity lexical. The separate specialization
      // binding below distinguishes classes instantiated from one generic defn.
      semanticIdentityObjects[identity] = anchorRefs.lookup(owner->second);
      continue;
    }
    if (semantic.getKind() != sim::VPITypeKind::VirtualInterface ||
        !semantic.getSymbol() ||
        typespec.getOrigin() != sim::VPITypespecOrigin::Interface)
      continue;
    StringRef identity = semantic.getSymbol().getRootReference().getValue();
    auto reference = typespecRefs.find(typespec);
    if (reference == typespecRefs.end()) {
      typespec.emitOpError(
          "canonical VPI interface identity was not serialized");
      return {};
    }
    if (!semanticIdentityObjects.try_emplace(identity, reference->second)
             .second) {
      typespec.emitOpError("duplicates a canonical VPI semantic identity for ")
          << identity;
      return {};
    }
  }

  struct SemanticTypeRecord {
    sim::VPITypeSemanticsAttr semantic;
    uint32_t publicVPIKind = 0;
    uint32_t firstEdge = 0;
    uint32_t edgeCount = 0;
    uint32_t aliasObject = UINT32_MAX;
    uint32_t identityTarget = UINT32_MAX;
  };
  struct SemanticTypeEdgeRecord {
    uint32_t child = UINT32_MAX;
    uint32_t role = 0;
    uint32_t flags = 0;
    uint32_t ordinal = 0;
    std::string name;
    uint64_t packedOffset = 0;
  };
  struct SemanticRootBindingRecord {
    uint32_t objectIndexAndTable = 0;
    uint32_t semanticType = 0;
  };
  SmallVector<SemanticTypeRecord> semanticTypes;
  SmallVector<SemanticTypeEdgeRecord> semanticTypeEdges;
  SmallVector<SemanticRootBindingRecord> semanticRootBindings;
  struct ClassTypeBindingRecord {
    uint64_t classID;
    uint32_t typespecIndexAndTable;
  };
  struct SemanticClassBindingRecord {
    uint32_t semanticType;
    uint32_t typespecIndexAndTable;
  };
  SmallVector<ClassTypeBindingRecord> classTypeBindings;
  SmallVector<SemanticClassBindingRecord> semanticClassBindings;
  DenseMap<std::tuple<Attribute, uint32_t, uint32_t>, uint32_t>
      semanticTypeIndices;
  bool semanticTypeError = false;
  auto packSemanticObject = [&](ImageObjectRef reference,
                                uint32_t &packed) -> bool {
    if ((reference.table != TableKind::Object &&
         reference.table != TableKind::StaticObject) ||
        !tryPackTableIndex(reference.table, reference.index, packed) ||
        packed == UINT32_MAX) {
      semanticTypeError = true;
      return false;
    }
    return true;
  };
  for (sim::SimVPITypespecDeclOp typespec : typespecs) {
    if (typespec.getOrigin() != sim::VPITypespecOrigin::ClassSpecialization)
      continue;
    auto declaration =
        SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
            typespec, typespec.getTargetType().getSymbol());
    uint32_t packed = 0;
    if (!declaration ||
        !packSemanticObject(typespecRefs.lookup(typespec), packed)) {
      typespec.emitOpError("could not bind canonical class specialization");
      return {};
    }
    classTypeBindings.push_back({declaration.getId(), packed});
  }
  llvm::sort(classTypeBindings, [](const auto &left, const auto &right) {
    return left.classID < right.classID;
  });

  auto semanticArrayShape = [&](sim::SimVPIObjectAnchorOp anchor) {
    DenseI64ArrayAttr ranges = anchor.getIndexRangesAttr();
    DenseI64ArrayAttr dimensionFlags = anchor.getIndexDimensionFlagsAttr();
    const bool namedEvent =
        anchor.getVpiKind() == static_cast<uint32_t>(VPIObjectKind::NamedEvent);
    const bool namedEventArray =
        anchor.getVpiKind() ==
        static_cast<uint32_t>(VPIObjectKind::NamedEventArray);
    if ((!ranges || ranges.empty()) && !namedEvent)
      return sim::VPITypeSemanticsAttr{};
    MLIRContext *context = anchor.getContext();
    auto make = [&](sim::VPITypeKind kind, ArrayRef<int64_t> range,
                    ArrayRef<Attribute> children) {
      return sim::VPITypeSemanticsAttr::get(
          context, kind, false, false, StringAttr{}, SymbolRefAttr{},
          StringAttr{}, DenseI64ArrayAttr::get(context, range),
          ArrayAttr::get(context, children), ArrayAttr::get(context, {}),
          BoolAttr{}, BoolAttr{}, IntegerAttr{}, IntegerAttr{}, IntegerAttr{},
          IntegerAttr{}, IntegerAttr{}, BoolAttr{}, DenseI64ArrayAttr{},
          DenseI64ArrayAttr{}, DenseI64ArrayAttr{}, ArrayAttr{});
    };
    sim::VPITypeSemanticsAttr current =
        make(namedEvent || namedEventArray ? sim::VPITypeKind::Event
                                           : sim::VPITypeKind::Untyped,
             {}, {});
    if (!ranges || ranges.empty())
      return current;
    ArrayRef<int64_t> values = ranges.asArrayRef();
    for (size_t offset = values.size(); offset != 0; offset -= 2) {
      Attribute child = current;
      bool packed =
          dimensionFlags && dimensionFlags.asArrayRef()[offset / 2 - 1] != 0;
      current = make(packed ? sim::VPITypeKind::PackedArray
                            : sim::VPITypeKind::UnpackedArray,
                     values.slice(offset - 2, 2), {child});
    }
    return current;
  };
  auto semanticForObject =
      [&](Operation *identity) -> sim::VPITypeSemanticsAttr {
    if (!identity)
      return {};
    if (auto storage = dyn_cast<sim::SimStorageDeclOp>(identity))
      return storage.getVpiTypeAttr();
    if (auto member = dyn_cast<sim::SimVPIClassMemberDeclOp>(identity))
      return member.getVpiType();
    if (auto local = dyn_cast<sim::SimVPILocalDeclOp>(identity))
      return local.getVpiType();
    if (auto net = dyn_cast<sim::SimNetDeclOp>(identity))
      return net.getVpiTypeAttr();
    if (auto netIdentity = dyn_cast<sim::SimVPINetIdentityDeclOp>(identity))
      return netIdentity.getVpiTypeAttr();
    if (auto port = dyn_cast<sim::SimPortDeclOp>(identity))
      return port.getVpiTypeAttr();
    if (auto anchor = dyn_cast<sim::SimVPIObjectAnchorOp>(identity)) {
      if (sim::VPITypeSemanticsAttr semantic = anchor.getVpiTypeAttr())
        return semantic;
      if (auto semantic = eventArraySemantics.lookup(anchor))
        return semantic;
      return semanticArrayShape(anchor);
    }
    auto typespec = dyn_cast<sim::SimVPITypespecDeclOp>(identity);
    if (auto constant = dyn_cast<sim::SimVPIEnumConstDeclOp>(identity))
      if (constant.getImmutableValueAttr())
        typespec =
            typespecsBySymbol.lookup(constant.getEnumTypespecAttr().getValue());
    if (typespec) {
      sim::VPITypeSemanticsAttr semantic = typespec.getTargetType();
      ArrayAttr aliases = semantic.getTypedefAliases();
      if (!aliases || aliases.empty())
        return semantic;
      auto first = cast<SymbolRefAttr>(aliases[0]);
      if (first.getRootReference().getValue() != typespec.getSymName())
        return semantic;

      // A declaration's own typespec starts one layer farther into the alias
      // chain than a variable whose declared type names that declaration.
      // Keeping separate semantic roots makes vpiTypespec(var) return the
      // outer typedef while vpiTypedefAlias(typedef) returns the next typedef.
      ArrayAttr remaining;
      if (aliases.size() > 1)
        remaining = ArrayAttr::get(typespec.getContext(),
                                   aliases.getValue().drop_front());
      return sim::VPITypeSemanticsAttr::get(
          typespec.getContext(), semantic.getKind(), semantic.getIsSigned(),
          semantic.getIsFourState(), semantic.getName(), semantic.getSymbol(),
          semantic.getModport(), semantic.getRange(), semantic.getChildren(),
          semantic.getChildNames(), semantic.getIsTagged(),
          semantic.getIsSoft(), semantic.getBitWidth(),
          semantic.getSelectableWidth(), semantic.getBitstreamWidth(),
          semantic.getTagBits(), semantic.getQueueBound(),
          semantic.getWildcardIndex(), semantic.getChildOrdinals(),
          semantic.getChildPackedOffsets(), semantic.getChildRandTypes(),
          remaining);
    }
    if (auto nettype = dyn_cast<sim::SimVPINettypeDeclOp>(identity))
      return nettype.getTargetType();
    return {};
  };
  std::function<std::optional<uint32_t>(sim::VPITypeSemanticsAttr)>
      addSemanticType =
          [&](sim::VPITypeSemanticsAttr semantic) -> std::optional<uint32_t> {
    if (!semantic)
      return std::nullopt;
    uint32_t aliasObject = UINT32_MAX;
    uint32_t identityTarget = UINT32_MAX;
    if (ArrayAttr aliases = semantic.getTypedefAliases();
        aliases && !aliases.empty()) {
      auto alias = cast<SymbolRefAttr>(aliases[0]);
      auto target = typespecsBySymbol.find(alias.getRootReference().getValue());
      if (target == typespecsBySymbol.end()) {
        semanticTypeError = true;
        return std::nullopt;
      }
      auto reference = typespecRefs.find(target->second);
      if (reference == typespecRefs.end() ||
          !packSemanticObject(reference->second, aliasObject))
        return std::nullopt;
    }
    if (SymbolRefAttr identity = semantic.getSymbol()) {
      auto target =
          semanticIdentityObjects.find(identity.getRootReference().getValue());
      if (target != semanticIdentityObjects.end()) {
        if (!packSemanticObject(target->second, identityTarget))
          return std::nullopt;
      } else if (semantic.getKind() == sim::VPITypeKind::Class ||
                 semantic.getKind() == sim::VPITypeKind::VirtualInterface) {
        semanticTypeError = true;
        return std::nullopt;
      }
    }
    auto key =
        std::make_tuple(Attribute(semantic), aliasObject, identityTarget);
    if (auto found = semanticTypeIndices.find(key);
        found != semanticTypeIndices.end())
      return found->second;
    if (semanticTypes.size() == UINT32_MAX ||
        semanticTypeEdges.size() > UINT32_MAX - semantic.getChildren().size()) {
      semanticTypeError = true;
      return std::nullopt;
    }
    uint32_t index = static_cast<uint32_t>(semanticTypes.size());
    semanticTypeIndices[key] = index;
    semanticTypes.push_back({semantic, 0, 0, 0, aliasObject, identityTarget});
    if (semantic.getKind() == sim::VPITypeKind::Class) {
      auto canonical = classSpecializationObjects.find(
          semantic.getSymbol().getRootReference().getValue());
      if (canonical != classSpecializationObjects.end()) {
        uint32_t packed = 0;
        if (!packSemanticObject(canonical->second, packed))
          return std::nullopt;
        semanticClassBindings.push_back({index, packed});
      }
    }
    if (semantic.getKind() == sim::VPITypeKind::Mailbox ||
        semantic.getKind() == sim::VPITypeKind::Semaphore ||
        semantic.getKind() == sim::VPITypeKind::Process) {
      auto canonical =
          builtinClassObjects.find(sim::stripVPITypedefAliases(semantic));
      if (canonical != builtinClassObjects.end()) {
        uint32_t packed = 0;
        if (!packSemanticObject(canonical->second, packed))
          return std::nullopt;
        semanticClassBindings.push_back({index, packed});
      }
    }
    const uint32_t firstEdge = static_cast<uint32_t>(semanticTypeEdges.size());
    const uint32_t edgeCount =
        static_cast<uint32_t>(semantic.getChildren().size());
    semanticTypes[index].firstEdge = firstEdge;
    semanticTypes[index].edgeCount = edgeCount;
    semanticTypeEdges.resize(semanticTypeEdges.size() + edgeCount);

    using Kind = sim::VPITypeKind;
    for (auto [ordinal, childAttr] : llvm::enumerate(semantic.getChildren())) {
      auto child = addSemanticType(cast<sim::VPITypeSemanticsAttr>(childAttr));
      if (!child)
        return std::nullopt;
      SemanticTypeEdgeRecord &edge = semanticTypeEdges[firstEdge + ordinal];
      edge.child = *child;
      edge.ordinal = static_cast<uint32_t>(ordinal);
      switch (semantic.getKind()) {
      case Kind::Enum:
        edge.role = OBELISK_RT_DESIGN_SEMANTIC_EDGE_ENUM_BASE;
        break;
      case Kind::AssocArray:
        edge.role = ordinal == 0 ? OBELISK_RT_DESIGN_SEMANTIC_EDGE_ASSOC_INDEX
                                 : OBELISK_RT_DESIGN_SEMANTIC_EDGE_ELEMENT;
        break;
      case Kind::PackedStruct:
      case Kind::UnpackedStruct:
      case Kind::PackedUnion:
      case Kind::UnpackedUnion:
        edge.role = OBELISK_RT_DESIGN_SEMANTIC_EDGE_MEMBER;
        edge.name = cast<StringAttr>(semantic.getChildNames()[ordinal])
                        .getValue()
                        .str();
        edge.ordinal = static_cast<uint32_t>(
            semantic.getChildOrdinals().asArrayRef()[ordinal]);
        edge.packedOffset = static_cast<uint64_t>(
            semantic.getChildPackedOffsets().asArrayRef()[ordinal]);
        edge.flags =
            semantic.getChildRandTypes()
                ? static_cast<uint32_t>(
                      semantic.getChildRandTypes().asArrayRef()[ordinal])
                : static_cast<uint32_t>(
                      OBELISK_RT_DESIGN_SEMANTIC_EDGE_NOT_RANDOM);
        break;
      default:
        edge.role = OBELISK_RT_DESIGN_SEMANTIC_EDGE_ELEMENT;
        break;
      }
    }
    if (semantic.getKind() == Kind::PackedArray ||
        semantic.getKind() == Kind::PackedOpenArray) {
      const SemanticTypeEdgeRecord &edge = semanticTypeEdges[firstEdge];
      uint32_t elementKind = semanticTypes[edge.child].publicVPIKind;
      semanticTypes[index].publicVPIKind =
          elementKind == static_cast<uint32_t>(VPIObjectKind::EnumTypespec) ||
                  elementKind ==
                      static_cast<uint32_t>(VPIObjectKind::StructTypespec) ||
                  elementKind ==
                      static_cast<uint32_t>(VPIObjectKind::UnionTypespec) ||
                  elementKind ==
                      static_cast<uint32_t>(VPIObjectKind::PackedArrayTypespec)
              ? static_cast<uint32_t>(VPIObjectKind::PackedArrayTypespec)
              : elementKind;
    } else if (semantic.getKind() == Kind::Untyped) {
      semanticTypes[index].publicVPIKind = 0;
    } else {
      std::optional<uint32_t> publicKind = sim::vpiKindForTypespec(semantic);
      if (!publicKind) {
        semanticTypeError = true;
        return std::nullopt;
      }
      semanticTypes[index].publicVPIKind = *publicKind;
    }
    return index;
  };
  if (includeStatements) {
    for (auto [index, object] : llvm::enumerate(objects)) {
      sim::VPITypeSemanticsAttr semantic = semanticForObject(object.identity);
      if (!semantic)
        continue;
      auto rootIndex = addSemanticType(semantic);
      if (!rootIndex) {
        if (object.identity)
          object.identity->emitError(
              "could not serialize exact VPI semantic type graph");
        return {};
      }
      uint32_t packed = 0;
      if (!packSemanticObject(
              {TableKind::Object, static_cast<uint32_t>(index), object.vpiKind},
              packed))
        return {};
      semanticRootBindings.push_back({packed, *rootIndex});
    }
    for (auto [index, object] : llvm::enumerate(staticObjects)) {
      sim::VPITypeSemanticsAttr semantic = semanticForObject(object.identity);
      if (!semantic)
        continue;
      auto rootIndex = addSemanticType(semantic);
      if (!rootIndex) {
        object.identity->emitError(
            "could not serialize exact VPI semantic type graph");
        return {};
      }
      uint32_t packed = 0;
      if (!packSemanticObject({TableKind::StaticObject,
                               static_cast<uint32_t>(index), object.vpiKind},
                              packed))
        return {};
      semanticRootBindings.push_back({packed, *rootIndex});
    }
    SmallVector<SmallVector<DefinitionSpecializationBindingRecord>>
        bindingsBySpecialization(definitionSpecializations.size());
    for (sim::SimVPIDefinitionMemberSpecializationOp binding :
         definitionMemberSpecializations) {
      auto specialization = definitionSpecializationsBySymbol.find(
          binding.getSpecializationAttr().getValue());
      auto member =
          definitionMembersBySymbol.find(binding.getMemberAttr().getValue());
      if (specialization == definitionSpecializationsBySymbol.end() ||
          member == definitionMembersBySymbol.end() ||
          !definitionSpecializationIndices.count(specialization->second) ||
          !definitionMemberIndices.count(member->second)) {
        binding.emitOpError(
            "VPI member specialization references unserialized inventory");
        return {};
      }
      uint32_t specializationIndex =
          definitionSpecializationIndices.lookup(specialization->second);
      uint32_t memberIndex = definitionMemberIndices.lookup(member->second);
      if (definitionSpecializations[specializationIndex].definition !=
          definitionMembers[memberIndex].definition) {
        binding.emitOpError("VPI member belongs to another definition");
        return {};
      }
      sim::VPITypeSemanticsAttr semantic = binding.getEffectiveTypeAttr();
      std::optional<uint32_t> rootIndex = addSemanticType(semantic);
      if (!rootIndex) {
        binding.emitOpError("could not serialize effective VPI member type");
        return {};
      }
      bindingsBySpecialization[specializationIndex].push_back(
          {memberIndex, *rootIndex, binding});
    }
    for (auto [index, bindings] : llvm::enumerate(bindingsBySpecialization)) {
      llvm::sort(bindings, [](const auto &left, const auto &right) {
        return left.member < right.member;
      });
      if (std::adjacent_find(bindings.begin(), bindings.end(),
                             [](const auto &left, const auto &right) {
                               return left.member == right.member;
                             }) != bindings.end()) {
        definitionSpecializations[index].declaration.emitOpError(
            "duplicates an effective VPI definition member binding");
        return {};
      }
      DefinitionSpecializationRecord &specialization =
          definitionSpecializations[index];
      specialization.firstBinding =
          static_cast<uint32_t>(definitionSpecializationBindings.size());
      specialization.bindingCount = static_cast<uint32_t>(bindings.size());
      llvm::append_range(definitionSpecializationBindings, bindings);
    }
    llvm::sort(semanticRootBindings,
               [](const SemanticRootBindingRecord &left,
                  const SemanticRootBindingRecord &right) {
                 return left.objectIndexAndTable < right.objectIndexAndTable;
               });
    if (std::adjacent_find(
            semanticRootBindings.begin(), semanticRootBindings.end(),
            [](const SemanticRootBindingRecord &left,
               const SemanticRootBindingRecord &right) {
              return left.objectIndexAndTable == right.objectIndexAndTable;
            }) != semanticRootBindings.end()) {
      design.emitOpError("duplicate VPI semantic root binding");
      return {};
    }
    if (semanticTypeError)
      return {};
  }

  // Pair exact source semantics with normalized storage for all reflected
  // values. Managed handles hide element/key types in the physical graph;
  // one typed owner namespace retains those child layouts without inferring
  // storage from source names or a runtime allocation's type ID.
  auto appendValueLayout =
      [&](Operation *owner, ValueLayoutOwnerKind ownerKind, uint32_t ownerIndex,
          Type rootType,
          sim::VPITypeSemanticsAttr rootSemantic) -> LogicalResult {
    using WorkItem = std::pair<Type, sim::VPITypeSemanticsAttr>;
    SmallVector<WorkItem> pending{{rootType, rootSemantic}};
    DenseMap<Attribute, Type> visited;
    while (!pending.empty()) {
      auto [physical, semantic] = pending.pop_back_val();
      auto [found, inserted] = visited.try_emplace(semantic, physical);
      if (!inserted) {
        if (found->second != physical) {
          owner->emitError(
              "value semantic node has conflicting storage layouts");
          return failure();
        }
        continue;
      }
      auto physicalIndex = addType(physical);
      auto semanticIndex = addSemanticType(semantic);
      if (!physicalIndex || !semanticIndex) {
        owner->emitError("value storage layout was not serialized");
        return failure();
      }
      valueElementBindings.push_back(
          {ownerKind, ownerIndex, *semanticIndex, *physicalIndex});
      // Executable enums may scalarize their source base; the complete enum
      // value remains an exact packed leaf with its own storage binding.
      if (semantic.getKind() == sim::VPITypeKind::Enum)
        continue;
      ArrayAttr children = semantic.getChildren();
      if (children.empty())
        continue;
      SmallVector<Type> childTypes;
      if (auto array = dyn_cast<sim::PackedArrayType>(physical))
        childTypes.push_back(array.getElementType());
      else if (auto array = dyn_cast<sim::UnpackedArrayType>(physical))
        childTypes.push_back(array.getElementType());
      else if (auto array = dyn_cast<sim::AssocArrayType>(physical)) {
        childTypes.push_back(array.getKeyType());
        childTypes.push_back(array.getElementType());
      } else if (auto mailbox = dyn_cast<sim::MailboxType>(physical))
        childTypes.push_back(mailbox.getElementType());
      else if (Type element = sim::getContainerElement(physical))
        childTypes.push_back(element);
      else {
        ArrayAttr fields;
        if (auto value = dyn_cast<sim::PackedStructType>(physical))
          fields = value.getFields();
        else if (auto value = dyn_cast<sim::UnpackedStructType>(physical))
          fields = value.getFields();
        else if (auto value = dyn_cast<sim::PackedUnionType>(physical))
          fields = value.getFields();
        else if (auto value = dyn_cast<sim::UnpackedUnionType>(physical))
          fields = value.getFields();
        if (fields)
          for (Attribute field : fields)
            childTypes.push_back(cast<sim::FieldAttr>(field).getType());
      }
      if (childTypes.size() != children.size()) {
        owner->emitError(
            "value semantic children have no exact storage layout");
        return failure();
      }
      for (auto [ordinal, child] : llvm::enumerate(children)) {
        if (semantic.getKind() == sim::VPITypeKind::AssocArray &&
            ordinal == 0 && sim::getPackedWidth(childTypes[ordinal]))
          continue;
        pending.emplace_back(childTypes[ordinal],
                             cast<sim::VPITypeSemanticsAttr>(child));
      }
    }
    return success();
  };

  if (includeStatements) {
    for (sim::SimVPITypespecDeclOp typespec : typespecs) {
      TypeAttr message = typespec.getMessageTypeAttr();
      if (!message)
        continue;
      auto reference = typespecRefs.find(typespec);
      uint32_t packed = 0;
      if (reference == typespecRefs.end() ||
          !tryPackTableIndex(reference->second.table, reference->second.index,
                             packed)) {
        typespec.emitOpError("canonical mailbox typespec was not serialized");
        return {};
      }
      if (failed(appendValueLayout(
              typespec, ValueLayoutOwnerKind::ReflectedObject, packed,
              message.getValue(),
              cast<sim::VPITypeSemanticsAttr>(
                  typespec.getTargetType().getChildren()[0]))))
        return {};
    }
    for (auto [index, object] : llvm::enumerate(objects)) {
      if (!object.identity || !object.type ||
          !isa<sim::SimStorageDeclOp, sim::SimVPIClassMemberDeclOp,
               sim::SimVPILocalDeclOp>(object.identity))
        continue;
      auto semantic = semanticForObject(object.identity);
      if (!semantic)
        continue;
      uint32_t packed = 0;
      if (!tryPackTableIndex(TableKind::Object, index, packed) ||
          failed(appendValueLayout(object.identity,
                                   ValueLayoutOwnerKind::ReflectedObject,
                                   packed, object.type, semantic)))
        return {};
    }
    SmallVector<sim::SimVPIBoxTypeDeclOp> descriptors;
    for (auto descriptor :
         design.getBody().front().getOps<sim::SimVPIBoxTypeDeclOp>())
      descriptors.push_back(descriptor);
    llvm::sort(descriptors, [](auto left, auto right) {
      return left.getId() < right.getId();
    });
    for (auto [index, descriptor] : llvm::enumerate(descriptors)) {
      auto semantic = addSemanticType(descriptor.getVpiType());
      auto physical = addType(descriptor.getValueType());
      if (!semantic || !physical || index > UINT32_MAX ||
          (index && descriptors[index - 1].getId() == descriptor.getId())) {
        descriptor.emitOpError("box type descriptor could not be serialized");
        return {};
      }
      boxTypeBindings.push_back({descriptor.getId(), *semantic, *physical});
      if (failed(appendValueLayout(
              descriptor, ValueLayoutOwnerKind::BoxDescriptor, index,
              descriptor.getValueType(), descriptor.getVpiType())))
        return {};
    }
  }
  llvm::sort(valueElementBindings, [](const auto &left, const auto &right) {
    return std::tie(left.ownerKind, left.ownerIndex, left.semanticType) <
           std::tie(right.ownerKind, right.ownerIndex, right.semanticType);
  });

  // Freeze immutable object values only after anchor identities have their
  // final physical table/index. Values are hash-consed separately from sparse
  // source bindings so later definition specializations can reuse this pool.
  // Packed planes are emitted bytewise so the image is host-endian independent.
  if (includeStatements) {
    auto appendPlane = [](SmallVectorImpl<uint8_t> &payload,
                          const llvm::APInt &plane, uint64_t bytes) {
      for (uint64_t byte = 0; byte != bytes; ++byte) {
        unsigned firstBit = static_cast<unsigned>(byte * 8);
        unsigned bits = std::min<unsigned>(8, plane.getBitWidth() - firstBit);
        payload.push_back(
            static_cast<uint8_t>(plane.extractBitsAsZExtValue(bits, firstBit)));
      }
    };
    llvm::StringMap<uint32_t> frozenValueIndices;
    SmallVector<
        std::tuple<Operation *, sim::FrozenConstantAttr, ImageObjectRef>>
        immutableSources;
    for (sim::SimVPIObjectAnchorOp anchor : anchors)
      if (auto frozen = anchor.getImmutableValueAttr()) {
        auto reference = anchorRefs.find(anchor);
        if (reference == anchorRefs.end()) {
          anchor.emitOpError("immutable VPI value source was not serialized");
          return {};
        }
        immutableSources.emplace_back(anchor, frozen, reference->second);
      }
    for (sim::SimVPIEnumConstDeclOp constant : enumConstants)
      if (auto frozen = constant.getImmutableValueAttr()) {
        auto index = objectIndices.find(constant);
        if (index == objectIndices.end()) {
          constant.emitOpError(
              "immutable enum value source was not serialized");
          return {};
        }
        immutableSources.emplace_back(
            constant, frozen,
            ImageObjectRef{TableKind::Object, index->second,
                           static_cast<uint32_t>(VPIObjectKind::EnumConst)});
      }
    auto internFrozenValue =
        [&](Operation *source,
            sim::FrozenConstantAttr frozen) -> std::optional<uint32_t> {
      auto planes = dyn_cast<ArrayAttr>(frozen.getValue());
      std::optional<unsigned> width = sim::getPackedWidth(frozen.getType());
      if (!planes || planes.size() != 2 || !width || *width == 0) {
        source->emitOpError("immutable VPI value is not a fixed packed value");
        return std::nullopt;
      }
      const llvm::APInt &value = cast<IntegerAttr>(planes[0]).getValue();
      const llvm::APInt &unknown = cast<IntegerAttr>(planes[1]).getValue();
      uint64_t planeBytes = (uint64_t{*width} + 7) / 8;
      if (planeBytes > UINT64_MAX / 2 ||
          frozenValuePayload.size() > UINT64_MAX - planeBytes * 2) {
        source->emitOpError("immutable VPI value payload exceeds image limits");
        return std::nullopt;
      }
      uint32_t kindAndFlags =
          static_cast<uint32_t>(FrozenValueKind::Packed) |
          (frozen.getIsSigned() ? frozenValueSigned : 0) |
          (containsLogic(frozen.getType()) ? frozenValueFourState : 0);
      SmallVector<uint8_t> payload;
      payload.reserve(static_cast<size_t>(planeBytes * 2));
      appendPlane(payload, value, planeBytes);
      appendPlane(payload, unknown, planeBytes);

      // StringMap owns arbitrary byte keys, including NULs. Prefixing the
      // canonical payload with fixed little-endian metadata makes equality
      // exact rather than relying on a hash collision policy.
      std::string key;
      key.reserve(12 + payload.size());
      for (unsigned byte = 0; byte != 4; ++byte)
        key.push_back(static_cast<char>(kindAndFlags >> (byte * 8)));
      for (unsigned byte = 0; byte != 8; ++byte)
        key.push_back(static_cast<char>(uint64_t{*width} >> (byte * 8)));
      key.append(reinterpret_cast<const char *>(payload.data()),
                 payload.size());
      auto [entry, inserted] = frozenValueIndices.try_emplace(
          key, static_cast<uint32_t>(frozenValues.size()));
      if (inserted) {
        FrozenValueRecord record;
        record.kindAndFlags = kindAndFlags;
        record.bitWidth = *width;
        record.payloadOffset = frozenValuePayload.size();
        record.payloadSize = payload.size();
        frozenValues.push_back(record);
        llvm::append_range(frozenValuePayload, payload);
      }
      return entry->second;
    };
    for (auto [source, frozen, reference] : immutableSources) {
      uint32_t packedSource = 0;
      if (!tryPackTableIndex(reference.table, reference.index, packedSource)) {
        source->emitOpError("immutable VPI value source cannot be packed");
        return {};
      }
      auto value = internFrozenValue(source, frozen);
      if (!value)
        return {};
      frozenValueBindings.push_back({packedSource, *value});
    }
    for (auto [index, binding] :
         llvm::enumerate(definitionSpecializationBindings))
      if (auto frozen = binding.declaration.getImmutableValueAttr()) {
        auto value = internFrozenValue(binding.declaration, frozen);
        if (!value)
          return {};
        definitionParameterValues.push_back(
            {static_cast<uint32_t>(index), *value});
      }
    // Intern syntax independently of its concrete declaration bindings. Every
    // node ordinal is a source occurrence: equal sibling subtrees stay
    // distinct.
    DenseMap<uint64_t, uint32_t> expressionScopeBindings;
    DenseMap<uint64_t, sim::SimScopeDeclOp> expressionScopes;
    for (auto scope : scopes)
      expressionScopes[scope.getId()] = scope;
    for (auto [index, binding] : llvm::enumerate(definitionBindings))
      if (unpackTableIndexKind(binding.sourceIndexAndTable) == TableKind::Scope)
        expressionScopeBindings
            [scopes[unpackTableIndex(binding.sourceIndexAndTable)].getId()] =
                index;
    DenseMap<std::pair<uint32_t, uint32_t>, uint32_t> boundMemberIndices;
    llvm::StringMap<uint32_t> expressionTemplateIndices;
    auto append32 = [](std::string &key, uint32_t value) {
      for (unsigned byte = 0; byte != 4; ++byte)
        key.push_back(static_cast<char>(value >> (byte * 8)));
    };
    auto contextScope = [&](ImageObjectRef owner,
                            uint32_t selector) -> std::optional<uint64_t> {
      uint64_t id;
      switch (owner.table) {
      case TableKind::Statement:
        id = statements[owner.index].declaration.getScopeId();
        break;
      case TableKind::Object:
        id = objects[owner.index].scope;
        break;
      case TableKind::StaticObject:
        id = staticObjects[owner.index].scope;
        break;
      default:
        return std::nullopt;
      }
      if (owner.vpiKind == uint32_t(VPIObjectKind::Port) &&
          selector == uint32_t(VPIRelationKind::HighConnRel)) {
        auto scope = expressionScopes.lookup(id);
        if (!scope || !scope.getParent())
          return std::nullopt;
        id = *scope.getParent();
      }
      return id;
    };
    auto leaf =
        [&](Operation *use, sim::VPIObjectRefAttr reference, uint64_t context,
            ExpressionLeafShapeRecord &shape) -> std::optional<uint32_t> {
      if (auto member = definitionMemberFor(reference)) {
        auto index = definitionMemberIndices.find(member);
        if (context != UINT64_MAX || index == definitionMemberIndices.end() ||
            index->second >= (uint32_t{1} << 30))
          return std::nullopt;
        shape = {UINT32_MAX, uint16_t(member.getVpiKind())};
        return index
            ->second; // Definition-local declaration, contextual at query.
      }
      if (reference.getKind() == sim::VPIObjectRefKind::DefinitionMember) {
        auto instance = expressionScopeBindings.find(
            reference.getId().getValue().getZExtValue());
        auto member =
            definitionMembersBySymbol.find(reference.getSymbol().getValue());
        if (instance == expressionScopeBindings.end() ||
            member == definitionMembersBySymbol.end() ||
            !definitionMemberIndices.count(member->second))
          return std::nullopt;
        uint32_t memberIndex = definitionMemberIndices.lookup(member->second);
        const auto &binding = definitionBindings[instance->second];
        if (binding.specialization == UINT32_MAX ||
            binding.definition != definitionMembers[memberIndex].definition)
          return std::nullopt;
        const auto &specialization =
            definitionSpecializations[binding.specialization];
        bool found = false;
        for (uint32_t offset = 0; offset != specialization.bindingCount;
             ++offset) {
          const auto &effective =
              definitionSpecializationBindings[specialization.firstBinding +
                                               offset];
          if (effective.member != memberIndex)
            continue;
          shape = {effective.semanticType,
                   uint16_t(member->second.getVpiKind())};
          found = true;
          break;
        }
        if (!found || memberIndex >= (uint32_t{1} << 30))
          return std::nullopt;
        if (reference.getId().getValue().getZExtValue() == context)
          return memberIndex; // LocalDefinitionMember.
        auto pair = std::make_pair(instance->second, memberIndex);
        auto [entry, inserted] =
            boundMemberIndices.try_emplace(pair, expressionBoundMembers.size());
        if (inserted)
          expressionBoundMembers.push_back(pair);
        if (entry->second >= (uint32_t{1} << 30))
          return std::nullopt;
        return (uint32_t{2} << 30) | entry->second;
      }
      auto object = resolvePhysicalReference(use, reference, "expression leaf");
      if (!object ||
          (object->table != TableKind::Object &&
           object->table != TableKind::StaticObject) ||
          object->index >= (uint32_t{1} << 30))
        return std::nullopt;
      Operation *identity = object->table == TableKind::Object
                                ? objects[object->index].identity
                                : staticObjects[object->index].identity;
      auto semantic = semanticForObject(identity);
      auto type = semantic ? addSemanticType(semantic) : std::nullopt;
      if (!type)
        return std::nullopt;
      shape = {*type, uint16_t(object->vpiKind)};
      return ((object->table == TableKind::Object ? uint32_t{1} : uint32_t{3})
              << 30) |
             object->index;
    };
    constexpr uint64_t expressionCursorLimit = uint64_t{1} << 30;
    for (auto relation : expressionRootDeclarations) {
      if (expressionRoots.size() >= expressionCursorLimit) {
        relation.emitOpError(
            "expression root count exceeds the compact cursor limit");
        return {};
      }
      auto memberOwner = definitionMemberFor(relation.getSource());
      std::optional<uint64_t> context;
      uint32_t packedOwner = 0, definition = UINT32_MAX;
      uint16_t rootMode = uint16_t(relation.getMode());
      if (memberOwner) {
        if (!definitionMemberIndices.count(memberOwner))
          return {};
        packedOwner = definitionMemberIndices.lookup(memberOwner);
        definition = definitionMembers[packedOwner].definition;
        context = UINT64_MAX; // Deliberately no instance scope.
        rootMode |= expressionRootDefinitionMember;
      } else {
        auto owner = resolvePhysicalReference(relation, relation.getSource(),
                                              "expression owner");
        context =
            owner ? contextScope(*owner, relation.getSelector()) : std::nullopt;
        if (!owner || !context ||
            !tryPackTableIndex(owner->table, owner->index, packedOwner)) {
          relation.emitOpError(
              "expression root requires an exact retained source context");
          return {};
        }
        if (auto instance = expressionScopeBindings.find(*context);
            instance != expressionScopeBindings.end())
          definition = definitionBindings[instance->second].definition;
      }
      SmallVector<ExpressionNodeRecord> nodes;
      SmallVector<uint32_t> operands, leaves;
      SmallVector<ExpressionLeafShapeRecord> shapes;
      SmallVector<sim::SimVPIExpressionDeclOp> order;
      DenseMap<Operation *, uint32_t> ordinals;
      auto root = expressionFor(relation.getTarget());
      if (root) {
        SmallVector<sim::SimVPIExpressionDeclOp> work{root};
        while (!work.empty()) {
          auto node = work.pop_back_val();
          if (ordinals.contains(node)) {
            node.emitOpError("expression occurrence is shared or cyclic");
            return {};
          }
          if (order.size() >= expressionCursorLimit) {
            node.emitOpError(
                "expression node count exceeds the compact cursor limit");
            return {};
          }
          ordinals[node] = static_cast<uint32_t>(order.size());
          order.push_back(node);
          auto &children = expressionChildren[node];
          auto rank = [&](sim::SimVPIRelationDeclOp child) {
            uint32_t selector = child.getSelector();
            if (node.getVpiKind() == uint32_t(VPIObjectKind::Operation))
              return uint64_t(child.getOrdinal());
            if (selector == uint32_t(VPIRelationKind::ParentRel))
              return uint64_t{0};
            if (selector == uint32_t(VPIRelationKind::IndexRel) ||
                selector == uint32_t(VPIRelationKind::LeftRangeRel) ||
                selector == uint32_t(VPIRelationKind::BaseExprRel))
              return uint64_t{1};
            return uint64_t{2};
          };
          llvm::sort(children, [&](auto left, auto right) {
            return rank(left) < rank(right);
          });
          for (auto child : llvm::reverse(children))
            if (child.getSelector() != uint32_t(VPIRelationKind::UseRel))
              if (auto expression = expressionFor(child.getTarget()))
                work.push_back(expression);
        }
        for (auto node : order) {
          auto semantic = node.getSourceOnly()
                              ? std::optional<uint32_t>(UINT32_MAX)
                              : addSemanticType(node.getVpiTypeAttr());
          auto frozen =
              node.getImmutableValueAttr()
                  ? internFrozenValue(node, node.getImmutableValueAttr())
                  : std::optional<uint32_t>(UINT32_MAX);
          if (!semantic || !frozen)
            return {};
          uint16_t code = node.getVpiKind() == uint32_t(VPIObjectKind::Constant)
                              ? uint16_t(7) // Integral frozen literal.
                              : uint16_t(node.getOpType().value_or(
                                    node.getIndexedDirection().value_or(0)));
          if (operands.size() >= expressionCursorLimit ||
              expressionChildren[node].size() >=
                  expressionCursorLimit - operands.size()) {
            node.emitOpError(
                "expression operands exceed the compact cursor limit");
            return {};
          }
          uint32_t first = static_cast<uint32_t>(operands.size());
          for (auto child : expressionChildren[node]) {
            if (child.getSelector() == uint32_t(VPIRelationKind::UseRel))
              continue;
            if (auto expression = expressionFor(child.getTarget())) {
              operands.push_back(ordinals.lookup(expression));
              continue;
            }
            ExpressionLeafShapeRecord shape;
            auto target = leaf(child, child.getTarget(), *context, shape);
            if (!target || leaves.size() >= (uint32_t{1} << 31)) {
              child.emitOpError(
                  "expression leaf has no exact compact declaration binding");
              return {};
            }
            shape.identifier =
                sim::get<sim::metadata::VpiExpressionIdentifier>(child);
            operands.push_back((uint32_t{1} << 31) | uint32_t(leaves.size()));
            leaves.push_back(*target);
            shapes.push_back(shape);
          }
          nodes.push_back({uint16_t(node.getVpiKind()), code, *semantic, first,
                           uint32_t(operands.size() - first), sourceFor(node),
                           *frozen});
        }
      } else {
        ExpressionLeafShapeRecord shape;
        auto target = leaf(relation, relation.getTarget(), *context, shape);
        if (!target) {
          relation.emitOpError(
              "contextual root has no exact declaration binding");
          return {};
        }
        leaves.push_back(*target);
      }
      uint32_t templateIndex = UINT32_MAX;
      if (root) {
        std::string key;
        append32(key, definition);
        // Source defaults have declaration-owned occurrence identity even when
        // two declarations contain equal syntax and source coordinates.
        append32(key, memberOwner ? packedOwner : UINT32_MAX);
        append32(key, nodes.size());
        append32(key, operands.size());
        append32(key, shapes.size());
        for (const auto &node : nodes) {
          append32(key, node.kind);
          append32(key, node.code);
          append32(key, node.semantic);
          append32(key, node.firstOperand);
          append32(key, node.operandCount);
          append32(key, node.source.file.size());
          key.append(node.source.file);
          append32(key, node.source.lineColumn >> 32);
          append32(key, node.source.lineColumn);
          append32(key, node.frozen);
        }
        for (auto operand : operands)
          append32(key, operand);
        for (const auto &shape : shapes) {
          append32(key, shape.semantic);
          append32(key, shape.kind);
          StringRef name =
              shape.identifier ? shape.identifier.getValue() : StringRef{};
          append32(key, name.size());
          key.append(name.str());
        }
        auto [entry, inserted] = expressionTemplateIndices.try_emplace(
            key, expressionTemplates.size());
        templateIndex = entry->second;
        if (inserted) {
          if (expressionTemplates.size() >= UINT32_MAX ||
              nodes.size() > UINT32_MAX - expressionNodes.size() ||
              expressionOperands.size() >= expressionCursorLimit ||
              operands.size() >=
                  expressionCursorLimit - expressionOperands.size() ||
              shapes.size() > UINT32_MAX - expressionLeafShapes.size()) {
            relation.emitOpError(
                "expression template pools exceed their compact limits");
            return {};
          }
          expressionTemplates.push_back(
              {definition, uint32_t(expressionNodes.size()),
               uint32_t(nodes.size()), uint32_t(expressionOperands.size()),
               uint32_t(operands.size()), uint32_t(leaves.size()),
               uint32_t(expressionLeafShapes.size())});
          llvm::append_range(expressionNodes, nodes);
          llvm::append_range(expressionOperands, operands);
          llvm::append_range(expressionLeafShapes, shapes);
        }
      }
      if (leaves.size() > UINT32_MAX - expressionLeaves.size()) {
        relation.emitOpError(
            "expression leaf bindings exceed the compact limit");
        return {};
      }
      expressionRoots.push_back({packedOwner,
                                 templateIndex,
                                 uint32_t(expressionLeaves.size()),
                                 uint16_t(relation.getSelector()),
                                 rootMode,
                                 uint32_t(relation.getOrdinal()),
                                 {}});
      llvm::append_range(expressionLeaves, leaves);
    }
    for (auto original : design.getOps<sim::SimVPIDefinitionTypeDefaultOp>()) {
      auto member =
          classMemberSymbols.lookupSymbolIn<sim::SimVPIDefinitionMemberDeclOp>(
              design, original.getMemberAttr());
      auto semantic = addSemanticType(original.getOriginalType());
      if (!member || !definitionMemberIndices.count(member) || !semantic ||
          expressionRoots.size() >= expressionCursorLimit) {
        original.emitOpError(
            "original type default has no compact source root");
        return {};
      }
      Source source = sourceFor(original);
      expressionRoots.push_back({definitionMemberIndices.lookup(member),
                                 *semantic, 0,
                                 uint16_t(VPIRelationKind::ExprRel),
                                 uint16_t(expressionRootDefinitionMember |
                                          expressionRootConcreteTypeDefault),
                                 uint32_t(source.lineColumn >> 32), source});
    }
    llvm::sort(expressionRoots, [](const auto &left, const auto &right) {
      return std::tie(left.owner, left.selector, left.mode, left.ordinal) <
             std::tie(right.owner, right.selector, right.mode, right.ordinal);
    });
    llvm::sort(frozenValueBindings, [](const FrozenValueBindingRecord &left,
                                       const FrozenValueBindingRecord &right) {
      return left.sourceIndexAndTable < right.sourceIndexAndTable;
    });
    if (std::adjacent_find(
            frozenValueBindings.begin(), frozenValueBindings.end(),
            [](const FrozenValueBindingRecord &left,
               const FrozenValueBindingRecord &right) {
              return left.sourceIndexAndTable == right.sourceIndexAndTable;
            }) != frozenValueBindings.end()) {
      design.emitOpError("duplicate immutable VPI value binding");
      return {};
    }
  }

  // Resolve immutable property ownership only after the physical tables have
  // reached their deterministic order. A backed source anchor aliases a scope
  // or code-unit record. Both operations may contribute disjoint metadata;
  // duplicate selectors must agree and the anchor is processed last so any
  // contradiction is diagnosed on the authoritative source identity.
  struct PropertySource {
    SmallVector<Operation *, 2> operations;
    uint32_t vpiKind = 0;
  };
  DenseMap<uint32_t, PropertySource> propertySources;
  auto rememberPropertySource = [&](TableKind table, uint32_t index,
                                    uint32_t vpiKind,
                                    Operation *operation) -> LogicalResult {
    uint32_t packed = 0;
    if (!tryPackTableIndex(table, index, packed))
      return operation->emitError(
          "fixed VPI property source index cannot be packed");
    PropertySource &source = propertySources[packed];
    source.operations.clear();
    source.operations.push_back(operation);
    source.vpiKind = vpiKind;
    return success();
  };
  if (includeStatements) {
    for (auto [index, scope] : llvm::enumerate(scopes))
      if (failed(rememberPropertySource(TableKind::Scope,
                                        static_cast<uint32_t>(index),
                                        sim::vpiKindForScope(scope), scope)))
        return {};
    for (auto [index, object] : llvm::enumerate(objects))
      if (object.identity &&
          failed(rememberPropertySource(TableKind::Object,
                                        static_cast<uint32_t>(index),
                                        object.vpiKind, object.identity)))
        return {};
    for (auto [index, object] : llvm::enumerate(staticObjects))
      if (failed(rememberPropertySource(TableKind::StaticObject,
                                        static_cast<uint32_t>(index),
                                        object.vpiKind, object.identity)))
        return {};
    for (auto [index, statement] : llvm::enumerate(statements))
      if (failed(rememberPropertySource(
              TableKind::Statement, static_cast<uint32_t>(index),
              statement.declaration.getVpiKind(), statement.declaration)))
        return {};

    for (const auto &[anchorOperation, reference] : anchorRefs) {
      auto anchor = cast<sim::SimVPIObjectAnchorOp>(anchorOperation);
      uint32_t packed = 0;
      if (!tryPackTableIndex(reference.table, reference.index, packed)) {
        anchor.emitOpError("backed VPI property source index cannot be packed");
        return {};
      }
      auto found = propertySources.find(packed);
      if (found == propertySources.end()) {
        anchor.emitOpError("backed VPI property source was not serialized");
        return {};
      }
      found->second.vpiKind = reference.vpiKind;
      if (found->second.operations.front() != anchorOperation)
        found->second.operations.push_back(anchorOperation);
    }

    DenseSet<Operation *> acceptedMetadataSources;
    for (Operation *operation : explicitlyHandledFixedMetadata)
      acceptedMetadataSources.insert(operation);
    for (const auto &[packedSource, source] : propertySources)
      if (source.vpiKind != 0)
        for (Operation *operation : source.operations)
          acceptedMetadataSources.insert(operation);
    WalkResult metadataWalk = design.walk([&](Operation *operation) {
      if (hasFixedReflectionMetadata(operation) &&
          !acceptedMetadataSources.contains(operation)) {
        operation->emitError(
            "fixed VPI metadata has no concrete serialized property source");
        return WalkResult::interrupt();
      }
      return WalkResult::advance();
    });
    if (metadataWalk.wasInterrupted())
      return {};
  }

  DenseMap<uint64_t, unsigned> fixedPropertyIndices;
  auto addFixedProperty = [&](Operation *operation, uint32_t packedSource,
                              uint32_t exactKind, uint32_t selector,
                              Attribute value) -> LogicalResult {
    const VPIPropertyDescriptor *descriptor =
        findVPIProperty(exactKind, selector);
    if (!descriptor ||
        (descriptor->realization != VPIPropertyRealization::FixedImage &&
         descriptor->realization != VPIPropertyRealization::DefinitionImage &&
         descriptor->realization != VPIPropertyRealization::OptionalImage) ||
        selector > UINT16_MAX)
      return operation->emitError(
          "immutable property is not an encodable image property for "
          "its exact VPI kind");
    if (descriptor->realization == VPIPropertyRealization::DefinitionImage) {
      auto binding = definitionForSource.find(packedSource);
      if (binding != definitionForSource.end()) {
        DefinitionRecord &definition = definitions[binding->second];
        bool agrees = false;
        if (selector == 9) {
          auto string = dyn_cast<StringAttr>(value);
          agrees = string && string.getValue() ==
                                 definition.declaration.getDefinitionName();
        } else if (selector == 15) {
          auto string = dyn_cast<StringAttr>(value);
          agrees = string && !definition.source.file.empty() &&
                   string.getValue() == definition.source.file;
        } else if (selector == 16) {
          auto integer = dyn_cast<IntegerAttr>(value);
          agrees = integer && definition.source.lineColumn >> 32 ==
                                  integer.getValue().getZExtValue();
        } else if (selector == 33) {
          auto integer = dyn_cast<IntegerAttr>(value);
          agrees = integer && definition.declaration.getPrimitiveType() &&
                   *definition.declaration.getPrimitiveType() ==
                       integer.getValue().getZExtValue();
        }
        if (!agrees)
          return operation->emitError(
              "instance property conflicts with its shared VPI definition");
        return success();
      }
    }
    FixedPropertyRecord record;
    record.sourceIndexAndTable = packedSource;
    record.optionalImage =
        descriptor->realization == VPIPropertyRealization::OptionalImage;
    record.selector = static_cast<uint16_t>(selector);
    record.kindAndFlags = static_cast<uint16_t>(descriptor->valueKind);
    switch (descriptor->valueKind) {
    case VPIPropertyValueKind::Boolean:
      if (!isa<BoolAttr>(value))
        return operation->emitError("fixed Boolean VPI property is not bool");
      record.payload = cast<BoolAttr>(value).getValue();
      break;
    case VPIPropertyValueKind::Integer:
      if (!isa<IntegerAttr>(value) ||
          !cast<IntegerAttr>(value).getType().isSignlessInteger(32))
        return operation->emitError("fixed integer VPI property is not i32");
      record.payload = static_cast<uint64_t>(
          cast<IntegerAttr>(value).getValue().getSExtValue());
      if (hasVPIIntegerPropertyDomain(selector) &&
          !findVPIIntegerPropertyValue(selector,
                                       static_cast<uint32_t>(record.payload)))
        return operation->emitError(
            "fixed integer VPI property value is outside its generated "
            "domain");
      break;
    case VPIPropertyValueKind::Int64:
      if (!isa<IntegerAttr>(value) ||
          !cast<IntegerAttr>(value).getType().isSignlessInteger(64))
        return operation->emitError("fixed int64 VPI property is not i64");
      record.payload = static_cast<uint64_t>(
          cast<IntegerAttr>(value).getValue().getSExtValue());
      break;
    case VPIPropertyValueKind::String:
      if (!isa<StringAttr>(value))
        return operation->emitError("fixed string VPI property is not string");
      record.stringPayload = cast<StringAttr>(value).getValue().str();
      if (StringRef(record.stringPayload).contains('\0'))
        return operation->emitError(
            "fixed string VPI property contains an embedded NUL");
      break;
    }
    uint64_t key = (uint64_t{packedSource} << 16) | record.selector;
    auto [entry, inserted] = fixedPropertyIndices.try_emplace(
        key, static_cast<unsigned>(fixedProperties.size()));
    if (!inserted) {
      const FixedPropertyRecord &existing = fixedProperties[entry->second];
      if (existing.kindAndFlags != record.kindAndFlags ||
          existing.payload != record.payload ||
          existing.stringPayload != record.stringPayload)
        return operation->emitError(
            "conflicting immutable VPI property values for one physical "
            "object");
      return success();
    }
    fixedProperties.push_back(std::move(record));
    return success();
  };
  if (includeStatements) {
    Builder builder(design.getContext());
    auto addOperationProperties = [&](Operation *operation,
                                      uint32_t packedSource,
                                      uint32_t exactKind) -> LogicalResult {
      if (auto anchor = dyn_cast<sim::SimVPIObjectAnchorOp>(operation);
          anchor &&
          (exactKind == static_cast<uint32_t>(VPIObjectKind::Parameter) ||
           exactKind == static_cast<uint32_t>(VPIObjectKind::Task) ||
           exactKind == static_cast<uint32_t>(VPIObjectKind::Function) ||
           (exactKind == static_cast<uint32_t>(VPIObjectKind::ClassDefn) &&
            llvm::any_of(sourceInheritance,
                         [&](Operation *declaration) {
                           auto type =
                               dyn_cast<sim::SimVPILexicalClassTypespecDeclOp>(
                                   declaration);
                           return type && type.getDefinitionAttr().getValue() ==
                                              anchor.getSymName();
                         }))) &&
          !anchor.getDebugName().empty())
        if (failed(addFixedProperty(operation, packedSource, exactKind, 2,
                                    anchor.getDebugNameAttr())))
          return failure();
      if (auto unit = dyn_cast<sim::SimCodeUnitDeclOp>(operation);
          unit && interfaceDeclarationIDs.contains(unit.getId()) &&
          sim::isVPIVisibleCodeUnit(unit))
        if (failed(addFixedProperty(operation, packedSource, exactKind, 2,
                                    unit.getDebugNameAttr())))
          return failure();
      if (auto constant = dyn_cast<sim::SimVPIEnumConstDeclOp>(operation))
        if (failed(addFixedProperty(operation, packedSource, exactKind, 2,
                                    constant.getNameAttr())))
          return failure();
      if (auto typespec = dyn_cast<sim::SimVPITypespecDeclOp>(operation);
          typespec && typespec.getOrigin() == sim::VPITypespecOrigin::Typedef)
        if (failed(addFixedProperty(operation, packedSource, exactKind, 2,
                                    typespec.getDebugNameAttr())))
          return failure();
      if (auto assignment =
              dyn_cast<sim::SimVPIClassParameterAssignDeclOp>(operation))
        if (assignment.getIsNamedAttr() &&
            failed(addFixedProperty(operation, packedSource, exactKind, 21,
                                    BoolAttr::get(design.getContext(), true))))
          return failure();
      if (auto type =
              dyn_cast<sim::SimVPILexicalClassTypespecDeclOp>(operation))
        if (failed(addFixedProperty(operation, packedSource, exactKind, 2,
                                    type.getDebugNameAttr())))
          return failure();
      if (auto alias =
              dyn_cast<sim::SimVPIUnresolvedTypeAliasDeclOp>(operation))
        if (failed(addFixedProperty(operation, packedSource, exactKind, 2,
                                    alias.getDebugNameAttr())))
          return failure();
      if (auto member = dyn_cast<sim::SimVPIClassMemberDeclOp>(operation))
        if (failed(addFixedProperty(operation, packedSource, exactKind, 2,
                                    member.getDebugNameAttr())))
          return failure();
      // Most declaration names are the final hierarchy component. Keep a
      // sparse exact override only when that legacy derivation loses an
      // authoritative identifier (for example an escaped dotted name).
      auto needsExactName = [](StringAttr debugName,
                               std::optional<StringRef> hierarchy) {
        if (!debugName || debugName.getValue().empty())
          return false;
        StringRef leaf = hierarchy.value_or(debugName.getValue());
        size_t dot = leaf.rfind('.'), colon = leaf.rfind("::");
        if (colon != StringRef::npos && (dot == StringRef::npos || colon > dot))
          leaf = leaf.drop_front(colon + 2);
        else if (dot != StringRef::npos)
          leaf = leaf.drop_front(dot + 1);
        return leaf != debugName.getValue();
      };
      if (auto storage = dyn_cast<sim::SimStorageDeclOp>(operation);
          storage && (staticLocalBindings.contains(storage.getId()) ||
                      needsExactName(storage.getDebugNameAttr(),
                                     storage.getHierarchicalName())))
        if (failed(addFixedProperty(operation, packedSource, exactKind, 2,
                                    storage.getDebugNameAttr())))
          return failure();
      if (auto net = dyn_cast<sim::SimNetDeclOp>(operation);
          net &&
          needsExactName(net.getDebugNameAttr(), net.getHierarchicalName()))
        if (failed(addFixedProperty(operation, packedSource, exactKind, 2,
                                    net.getDebugNameAttr())))
          return failure();
      if (isa<sim::SimVPIForeachDeclOp>(operation))
        if (failed(addFixedProperty(operation, packedSource, exactKind, 39,
                                    builder.getI32IntegerAttr(36))))
          return failure();
      if (auto local = dyn_cast<sim::SimVPILocalDeclOp>(operation)) {
        if (failed(addFixedProperty(operation, packedSource, exactKind, 2,
                                    local.getDebugNameAttr())))
          return failure();
        if (findVPIProperty(exactKind, 50) &&
            failed(addFixedProperty(operation, packedSource, exactKind, 50,
                                    builder.getBoolAttr(true))))
          return failure();
      }
      if (auto member = dyn_cast<sim::SimVPIClassMemberDeclOp>(operation);
          member && member.getFieldAttr() &&
          (exactKind == static_cast<uint32_t>(VPIObjectKind::NamedEvent) ||
           exactKind == static_cast<uint32_t>(VPIObjectKind::NamedEventArray)))
        if (failed(addFixedProperty(
                operation, packedSource, exactKind, 50,
                builder.getBoolAttr(!classMemberFields.lookup(member).getIsStatic()))))
          return failure();
      if (operation->hasAttr("is_protected") &&
          failed(addFixedProperty(operation, packedSource, exactKind, 74,
                                  builder.getBoolAttr(true))))
        return failure();
      if (auto location = dyn_cast_or_null<FileLineColLoc>(
              operation->getAttr("definition_loc"))) {
        if (location.getLine() > INT32_MAX)
          return operation->emitError(
              "definition_loc line exceeds the VPI 32-bit integer range");
        if (failed(addFixedProperty(
                operation, packedSource, exactKind, 15,
                builder.getStringAttr(location.getFilename().getValue()))) ||
            failed(addFixedProperty(
                operation, packedSource, exactKind, 16,
                builder.getI32IntegerAttr(location.getLine()))))
          return failure();
      }
      if (auto properties = operation->getAttrOfType<sim::VPIPropertySetAttr>(
              "vpi_properties")) {
        for (Attribute attribute : properties.getProperties()) {
          auto property = cast<sim::VPIPropertyAttr>(attribute);
          uint32_t selector = static_cast<uint32_t>(
              property.getSelector().getValue().getZExtValue());
          if (failed(addFixedProperty(operation, packedSource, exactKind,
                                      selector, property.getValue())))
            return failure();
        }
      }
      return success();
    };
    auto addCodeUnitProperties = [&](Operation *operation,
                                     uint32_t packedSource,
                                     uint32_t exactKind) -> LogicalResult {
      auto codeUnit = dyn_cast<sim::SimCodeUnitDeclOp>(operation);
      if (!codeUnit ||
          exactKind != static_cast<uint32_t>(VPIObjectKind::Always))
        return success();
      uint32_t alwaysType = 0;
      switch (codeUnit.getCodeUnitKind()) {
      case sim::EntryKind::Always:
        alwaysType = 1;
        break;
      case sim::EntryKind::AlwaysComb:
        alwaysType = 2;
        break;
      case sim::EntryKind::AlwaysFF:
        alwaysType = 3;
        break;
      case sim::EntryKind::AlwaysLatch:
        alwaysType = 4;
        break;
      default:
        return codeUnit.emitOpError(
            "VPI always object has a non-always code-unit kind");
      }
      return addFixedProperty(operation, packedSource, exactKind, 624,
                              builder.getI32IntegerAttr(alwaysType));
    };
    for (const auto &[packedSource, source] : propertySources) {
      if (source.vpiKind == 0)
        continue;
      for (Operation *operation : source.operations)
        if (failed(addOperationProperties(operation, packedSource,
                                          source.vpiKind)) ||
            failed(
                addCodeUnitProperties(operation, packedSource, source.vpiKind)))
          return {};
    }
    for (auto [index, call] : llvm::enumerate(callDeclarations)) {
      const auto *property = reflection::findVPIProperty(call.getVpiKind(), 2);
      if (property &&
          failed(addFixedProperty(call, callSiteBindings[index].object,
                                  call.getVpiKind(), 2, call.getNameAttr())))
        return {};
    }
    // Required image Booleans encode false by absence. Optional image facts
    // retain explicit false because absence requests the derived fallback.
    fixedProperties.erase(
        std::remove_if(
            fixedProperties.begin(), fixedProperties.end(),
            [](const FixedPropertyRecord &property) {
              return property.kindAndFlags ==
                         static_cast<uint16_t>(VPIPropertyValueKind::Boolean) &&
                     property.payload == 0 && !property.optionalImage;
            }),
        fixedProperties.end());
    llvm::sort(fixedProperties, [](const FixedPropertyRecord &left,
                                   const FixedPropertyRecord &right) {
      return std::tie(left.sourceIndexAndTable, left.selector) <
             std::tie(right.sourceIndexAndTable, right.selector);
    });

    // Freeze the exact post-collapse net subtype per physical bit. Execution
    // deliberately canonicalizes wand/triand and wor/trior into common
    // resolution categories, so the VPI image must follow the dominating
    // declaration's original selector-22 value instead. Isolated ranges stay
    // compact: only bits named by a connection split the declaration-wide
    // default run.
    analysis::NetConnectivityAnalysis connectivity(design);
    DenseMap<uint64_t, std::pair<sim::SimNetDeclOp, uint32_t>> netObjects;
    for (auto [objectIndex, object] : llvm::enumerate(objects))
      if (object.kind == OBELISK_RT_DESIGN_RECORD_NET)
        if (auto net = dyn_cast_if_present<sim::SimNetDeclOp>(object.identity))
          netObjects[net.getId()] = {net, static_cast<uint32_t>(objectIndex)};
    auto declaredNetType = [&](uint64_t netID) -> std::optional<uint32_t> {
      auto found = netObjects.find(netID);
      if (found == netObjects.end())
        return std::nullopt;
      uint32_t packedSource = 0;
      if (!tryPackTableIndex(TableKind::Object, found->second.second,
                             packedSource))
        return std::nullopt;
      auto property = std::lower_bound(
          fixedProperties.begin(), fixedProperties.end(),
          std::pair{packedSource, uint16_t{22}},
          [](const FixedPropertyRecord &record, const auto &key) {
            return std::pair{record.sourceIndexAndTable, record.selector} < key;
          });
      if (property == fixedProperties.end() ||
          property->sourceIndexAndTable != packedSource ||
          property->selector != 22 ||
          property->kindAndFlags !=
              static_cast<uint16_t>(VPIPropertyValueKind::Integer))
        return std::nullopt;
      return static_cast<uint32_t>(property->payload);
    };
    auto appendResolvedRun = [&](uint32_t objectIndex, uint64_t firstBit,
                                 uint64_t bitCount,
                                 std::optional<uint32_t> netType) {
      if (bitCount == 0 || !netType)
        return;
      if (!resolvedNetRuns.empty()) {
        ResolvedNetRunRecord &previous = resolvedNetRuns.back();
        if (previous.objectIndex == objectIndex &&
            previous.netType == *netType &&
            previous.firstBit + previous.bitCount == firstBit) {
          previous.bitCount += bitCount;
          return;
        }
      }
      resolvedNetRuns.push_back({objectIndex, *netType, firstBit, bitCount});
    };
    ArrayRef<analysis::NetBit> connected = connectivity.getConnectedBits();
    for (const auto &[netID, entry] : netObjects) {
      sim::SimNetDeclOp net = entry.first;
      uint32_t objectIndex = entry.second;
      std::optional<uint64_t> width = connectivity.getNetWidth(netID);
      if (!width)
        continue;
      std::optional<uint32_t> ownType = declaredNetType(netID);
      auto bit = std::lower_bound(connected.begin(), connected.end(),
                                  analysis::NetBit{netID, 0});
      uint64_t position = 0;
      while (bit != connected.end() && bit->net == netID) {
        if (bit->offset >= *width) {
          net.emitOpError("connected net bit exceeds its declared width");
          return {};
        }
        appendResolvedRun(objectIndex, position, bit->offset - position,
                          ownType);
        std::optional<uint32_t> resolvedType;
        analysis::NetDominance dominance = connectivity.getDominance(*bit);
        if (dominance.kind == analysis::NetDominanceKind::Isolated) {
          resolvedType = ownType;
        } else if (dominance.kind == analysis::NetDominanceKind::Unique ||
                   dominance.kind == analysis::NetDominanceKind::Ambiguous) {
          ArrayRef<analysis::NetBit> dominators =
              connectivity.getDominatingBits(*bit);
          for (analysis::NetBit dominator : dominators) {
            std::optional<uint32_t> candidate = declaredNetType(dominator.net);
            if (!candidate || (resolvedType && resolvedType != candidate)) {
              resolvedType.reset();
              break;
            }
            resolvedType = candidate;
          }
        }
        appendResolvedRun(objectIndex, bit->offset, 1, resolvedType);
        position = bit->offset + 1;
        ++bit;
      }
      appendResolvedRun(objectIndex, position, *width - position, ownType);
    }
    llvm::sort(resolvedNetRuns, [](const ResolvedNetRunRecord &left,
                                   const ResolvedNetRunRecord &right) {
      return std::tie(left.objectIndex, left.firstBit) <
             std::tie(right.objectIndex, right.firstBit);
    });

    // Freeze the effective delay of every reflected net bit. Missing delay
    // metadata and the canonical all--1 per-bit marker both mean immediate
    // propagation and are represented by gaps. Every net query remains total
    // without touching execution state or scheduler structures.
    auto appendNetDelayRun = [&](uint32_t objectIndex, uint64_t firstBit,
                                 uint64_t bitCount,
                                 std::array<int64_t, 3> delays) {
      if (bitCount == 0 || delays == std::array<int64_t, 3>{})
        return;
      if (!netDelayRuns.empty()) {
        NetDelayRunRecord &previous = netDelayRuns.back();
        if (previous.objectIndex == objectIndex && previous.delays == delays &&
            previous.firstBit + previous.bitCount == firstBit) {
          previous.bitCount += bitCount;
          return;
        }
      }
      netDelayRuns.push_back({objectIndex, firstBit, bitCount, delays});
    };
    for (auto [objectIndex, object] : llvm::enumerate(objects)) {
      if (object.kind != OBELISK_RT_DESIGN_RECORD_NET)
        continue;
      auto net = dyn_cast_if_present<sim::SimNetDeclOp>(object.identity);
      std::optional<uint64_t> width = simulationWidth(object.type);
      if (!net || !width)
        continue;
      ArrayRef<int64_t> encoded;
      if (auto delays = net.getPropagationDelays())
        encoded = *delays;
      if (encoded.empty())
        continue;
      if (encoded.size() == 3) {
        std::array<int64_t, 3> delays{};
        if (encoded[0] != -1)
          delays = {encoded[0], encoded[1], encoded[2]};
        appendNetDelayRun(static_cast<uint32_t>(objectIndex), 0, *width,
                          delays);
        continue;
      }
      for (uint64_t bit = 0; bit != *width; ++bit) {
        std::array<int64_t, 3> delays{};
        size_t base = bit * 3;
        if (encoded[base] != -1)
          delays = {encoded[base], encoded[base + 1], encoded[base + 2]};
        appendNetDelayRun(static_cast<uint32_t>(objectIndex), bit, 1, delays);
      }
    }

    // Collapsed declared-net identities intentionally do not duplicate these
    // per-bit runs. Cold VPI queries follow their generated vpiSimNet edge to
    // the canonical physical net, keeping image size linear in the physical
    // design rather than in the number of declared alias spellings.
    llvm::sort(netDelayRuns, [](const NetDelayRunRecord &left,
                                const NetDelayRunRecord &right) {
      return std::tie(left.objectIndex, left.firstBit) <
             std::tie(right.objectIndex, right.firstBit);
    });
  }

  SmallVector<uint8_t> strings(1, 0);
  llvm::StringMap<uint64_t> stringOffsets;
  auto intern = [&](StringRef value) {
    auto found = stringOffsets.find(value);
    if (found != stringOffsets.end())
      return found->second;
    uint64_t offset = strings.size();
    llvm::append_range(strings, value.bytes());
    strings.push_back(0);
    stringOffsets[value] = offset;
    return offset;
  };
  for (auto scope : scopes) {
    intern(scope.getHierarchicalName().value_or(
        scope.getDebugName().value_or(fallbackName("scope", scope.getId()))));
    Source source = sourceFor(scope);
    if (!source.file.empty())
      intern(source.file);
  }
  for (const Record &object : objects) {
    intern(object.name);
    if (!object.source.file.empty())
      intern(object.source.file);
  }
  for (const StaticObjectRecord &object : staticObjects) {
    if (!object.name.empty())
      intern(object.name);
    if (!object.source.file.empty())
      intern(object.source.file);
  }
  for (StatementRecord statement : statements) {
    if (!statement.source.file.empty())
      intern(statement.source.file);
    if (auto name = statement.declaration.getName())
      intern(*name);
  }
  for (const TypeRecord &type : types)
    intern(type.name);
  for (const SemanticTypeRecord &type : semanticTypes) {
    if (StringAttr name = type.semantic.getName())
      intern(name.getValue());
    if (StringAttr modport = type.semantic.getModport())
      intern(modport.getValue());
  }
  for (const SemanticTypeEdgeRecord &edge : semanticTypeEdges)
    if (!edge.name.empty())
      intern(edge.name);
  for (const ClassMemberBindingRecord &member : classMemberBindings)
    intern(member.name);
  for (const ModportBindingRecord &modport : modportBindings)
    intern(modport.name);
  for (const InterfaceCallableRecord &member : interfaceCallables)
    intern(member.name);
  for (DefinitionRecord &definition : definitions) {
    intern(definition.declaration.getDefinitionName());
    if (!definition.source.file.empty())
      intern(definition.source.file);
  }
  for (DefinitionMemberRecord &member : definitionMembers) {
    intern(member.declaration.getMemberName());
    if (!member.source.file.empty())
      intern(member.source.file);
  }
  for (const FixedPropertyRecord &property : fixedProperties)
    if (property.kindAndFlags ==
        static_cast<uint16_t>(VPIPropertyValueKind::String))
      intern(property.stringPayload);
  if ((!semanticTypes.empty() || !staticObjects.empty() ||
       !definitions.empty()) &&
      strings.size() > UINT32_MAX) {
    design.emitOpError(
        "semantic reflection string table exceeds 32-bit offsets");
    return {};
  }

  SmallVector<uint8_t> output(HeaderLayout.size, 0);
  for (const auto &shape : expressionLeafShapes)
    if (shape.identifier)
      intern(shape.identifier.getValue());
  for (const auto &node : expressionNodes)
    if (!node.source.file.empty())
      intern(node.source.file);
  for (const auto &root : expressionRoots)
    if (!root.source.file.empty())
      intern(root.source.file);
  uint64_t scopeOffset = output.size();
  DenseMap<uint64_t, uint64_t> scopeOffsets;
  for (auto [index, scope] : llvm::enumerate(scopes))
    scopeOffsets[scope.getId()] = scopeOffset + index * ScopeLayout.size;
  uint64_t objectOffset = scopeOffset + scopes.size() * ScopeLayout.size;
  uint64_t typeOffset = objectOffset + objects.size() * ObjectLayout.size;
  uint64_t statementOffset = typeOffset + types.size() * TypeLayout.size;
  uint64_t statementSiteOffset =
      statementOffset + statements.size() * StatementLayout.size;
  uint64_t relationOffset =
      statementSiteOffset + statementSites.size() * StatementSiteLayout.size;
  uint64_t semanticDirectoryOffset =
      includeStatements
          ? relationOffset + relations.size() * RelationLayout.size
          : 0;
  if (semanticDirectoryOffset > UINT32_MAX) {
    design.emitOpError("reflection extension directory exceeds 32-bit offset");
    return {};
  }
  uint64_t semanticTypeOffset =
      relationOffset + relations.size() * RelationLayout.size +
      (includeStatements ? SemanticDirectoryLayout.size : 0);
  uint64_t semanticTypeEdgeOffset =
      semanticTypeOffset + semanticTypes.size() * SemanticTypeLayout.size;
  uint64_t semanticRootBindingOffset =
      semanticTypeEdgeOffset +
      semanticTypeEdges.size() * SemanticTypeEdgeLayout.size;
  uint64_t classTypeBindingOffset =
      semanticRootBindingOffset +
      semanticRootBindings.size() * SemanticRootBindingLayout.size;
  uint64_t semanticClassBindingOffset =
      classTypeBindingOffset +
      classTypeBindings.size() * ClassTypeBindingLayout.size;
  uint64_t classMemberBindingOffset =
      semanticClassBindingOffset +
      semanticClassBindings.size() * SemanticClassBindingLayout.size;
  uint64_t modportBindingOffset =
      classMemberBindingOffset +
      classMemberBindings.size() * ClassMemberBindingLayout.size;
  uint64_t boxTypeBindingOffset =
      modportBindingOffset + modportBindings.size() * ModportBindingLayout.size;
  uint64_t valueElementBindingOffset =
      boxTypeBindingOffset + boxTypeBindings.size() * BoxTypeBindingLayout.size;
  uint64_t eventBindingOffset =
      valueElementBindingOffset +
      valueElementBindings.size() * ValueElementBindingLayout.size;
  uint64_t eventArrayStorageBindingOffset =
      eventBindingOffset + eventBindings.size() * EventBindingLayout.size;
  uint64_t threadOriginBindingOffset =
      eventArrayStorageBindingOffset +
      eventArrayStorageBindings.size() * EventArrayStorageBindingLayout.size;
  uint64_t callSiteBindingOffset =
      threadOriginBindingOffset +
      threadOriginBindings.size() * ThreadOriginBindingLayout.size;
  uint64_t frameLocalBindingOffset =
      callSiteBindingOffset +
      callSiteBindings.size() * CallSiteBindingLayout.size;
  uint64_t callDispatchBindingOffset =
      frameLocalBindingOffset +
      frameLocalBindings.size() * FrameLocalBindingLayout.size;
  uint64_t classDispatchMethodBindingOffset =
      callDispatchBindingOffset +
      callDispatchBindings.size() * CallDispatchBindingLayout.size;
  uint64_t classDispatchParentBindingOffset =
      classDispatchMethodBindingOffset +
      classDispatchMethodBindings.size() *
          ClassDispatchMethodBindingLayout.size;
  uint64_t interfaceCallableOffset = classDispatchParentBindingOffset +
                                     classDispatchParentBindings.size() *
                                         ClassDispatchParentBindingLayout.size;
  uint64_t interfaceInstanceOffset =
      interfaceCallableOffset +
      interfaceCallables.size() * InterfaceCallableLayout.size;
  uint64_t interfaceCallableBindingOffset =
      interfaceInstanceOffset +
      interfaceInstances.size() * InterfaceInstanceLayout.size;
  uint64_t interfaceCallBindingOffset =
      interfaceCallableBindingOffset +
      interfaceCallableBindings.size() * InterfaceCallableBindingLayout.size;
  uint64_t callableSignatureBindingOffset =
      interfaceCallBindingOffset +
      interfaceCallBindings.size() * InterfaceCallBindingLayout.size;
  uint64_t codeUnitScopeBindingOffset =
      callableSignatureBindingOffset +
      callableSignatureBindings.size() * CallableSignatureBindingLayout.size;
  uint64_t initializerBindingOffset =
      codeUnitScopeBindingOffset +
      codeUnitScopeBindings.size() * CodeUnitScopeBindingLayout.size;
  uint64_t expressionRootBindingOffset =
      initializerBindingOffset +
      initializerBindings.size() * InitializerBindingLayout.size;
  uint64_t expressionTemplateOffset =
      expressionRootBindingOffset +
      expressionRoots.size() * ExpressionRootBindingLayout.size;
  uint64_t expressionNodeOffset =
      expressionTemplateOffset +
      expressionTemplates.size() * ExpressionTemplateLayout.size;
  uint64_t expressionOperandOffset =
      expressionNodeOffset + expressionNodes.size() * ExpressionNodeLayout.size;
  uint64_t expressionLeafOffset =
      expressionOperandOffset +
      expressionOperands.size() * ExpressionOperandLayout.size;
  uint64_t expressionBoundMemberOffset =
      expressionLeafOffset +
      expressionLeaves.size() * ExpressionLeafLayout.size;
  uint64_t expressionLeafShapeOffset =
      expressionBoundMemberOffset +
      expressionBoundMembers.size() * ExpressionBoundMemberLayout.size;
  uint64_t terminalDriverBindingOffset =
      expressionLeafShapeOffset +
      expressionLeafShapes.size() * ExpressionLeafShapeLayout.size;
  uint64_t relationIndexOffset =
      terminalDriverBindingOffset +
      terminalDriverBindings.size() * TerminalDriverBindingLayout.size;
  uint64_t relationIndexDimensionOffset =
      relationIndexOffset + relationIndices.size() * RelationIndexLayout.size;
  uint64_t relationIndexKeyOffset =
      relationIndexDimensionOffset +
      relationIndexDimensions.size() * RelationIndexDimensionLayout.size;
  uint64_t relationIndexMemberOffset =
      relationIndexKeyOffset +
      relationIndexKeys.size() * RelationIndexKeyLayout.size;
  uint64_t fixedPropertyOffset =
      relationIndexMemberOffset +
      relationIndexMembers.size() * RelationIndexMemberLayout.size;
  uint64_t resolvedNetRunOffset =
      fixedPropertyOffset + fixedProperties.size() * FixedPropertyLayout.size;
  uint64_t netDelayRunOffset =
      resolvedNetRunOffset + resolvedNetRuns.size() * ResolvedNetRunLayout.size;
  uint64_t staticObjectOffset =
      netDelayRunOffset + netDelayRuns.size() * NetDelayRunLayout.size;
  uint64_t definitionOffset =
      staticObjectOffset + staticObjects.size() * StaticObjectLayout.size;
  uint64_t definitionBindingOffset =
      definitionOffset + definitions.size() * DefinitionLayout.size;
  uint64_t definitionMemberOffset =
      definitionBindingOffset +
      definitionBindings.size() * DefinitionBindingLayout.size;
  uint64_t definitionMemberRelationOffset =
      definitionMemberOffset +
      definitionMembers.size() * DefinitionMemberLayout.size;
  uint64_t definitionMemberRelationTargetOffset =
      definitionMemberRelationOffset +
      definitionMemberRelations.size() * DefinitionMemberRelationLayout.size;
  uint64_t definitionSpecializationOffset =
      definitionMemberRelationTargetOffset +
      definitionMemberRelationTargets.size() *
          DefinitionMemberRelationTargetLayout.size;
  uint64_t definitionSpecializationBindingOffset =
      definitionSpecializationOffset +
      definitionSpecializations.size() * DefinitionSpecializationLayout.size;
  uint64_t definitionParameterValueOffset =
      definitionSpecializationBindingOffset +
      definitionSpecializationBindings.size() *
          DefinitionSpecializationBindingLayout.size;
  uint64_t definitionMemberEndpointOffset =
      definitionParameterValueOffset +
      definitionParameterValues.size() * DefinitionParameterValueLayout.size;
  uint64_t definitionMemberInstanceRelationOffset =
      definitionMemberEndpointOffset +
      definitionMemberEndpoints.size() * DefinitionMemberEndpointLayout.size;
  uint64_t definitionMemberInstanceRelationTargetOffset =
      definitionMemberInstanceRelationOffset +
      definitionMemberInstanceRelations.size() *
          DefinitionMemberInstanceRelationLayout.size;
  uint64_t definitionMemberInstanceRelationInverseOffset =
      definitionMemberInstanceRelationTargetOffset +
      definitionMemberInstanceRelationTargets.size() *
          DefinitionMemberInstanceRelationTargetLayout.size;
  uint64_t frozenValueOffset =
      definitionMemberInstanceRelationInverseOffset +
      definitionMemberInstanceRelationInverses.size() *
          DefinitionMemberInstanceRelationInverseLayout.size;
  uint64_t frozenValueBindingOffset =
      frozenValueOffset + frozenValues.size() * FrozenValueLayout.size;
  uint64_t frozenValuePayloadOffset =
      frozenValueBindingOffset +
      frozenValueBindings.size() * FrozenValueBindingLayout.size;
  uint64_t stringOffset = frozenValuePayloadOffset + frozenValuePayload.size();
  uint64_t indexOffset = 0;
  output.resize(stringOffset, 0);

  DenseMap<uint64_t, SmallVector<uint64_t>> children;
  for (auto scope : scopes)
    if (auto parent = scope.getParent())
      children[*parent].push_back(scopeOffsets.lookup(scope.getId()));
  for (auto [index, object] : llvm::enumerate(objects))
    children[object.scope].push_back(objectOffset + index * ObjectLayout.size);
  DenseMap<uint64_t, uint64_t> nextSiblings;
  for (const auto &entry : children) {
    ArrayRef<uint64_t> siblings = entry.second;
    for (size_t index = 1; index < siblings.size(); ++index)
      nextSiblings[siblings[index - 1]] = siblings[index];
  }

  for (auto scope : scopes) {
    uint64_t self = scopeOffsets.lookup(scope.getId());
    ScopeWriter writer(output.data() + self);
    uint32_t packedKind = 0;
    uint32_t vpiKind = sim::vpiKindForScope(scope);
    if (!tryPackRecordKindPayload(RecordKind::Scope, vpiKind, packedKind)) {
      scope.emitOpError("scope record or VPI kind cannot be packed");
      return {};
    }
    writer.setKindAndVPIKind(packedKind);
    writer.setCaps(OBELISK_RT_DESIGN_CAP_ITERATE);
    writer.setID(scope.getId());
    writer.setParent(scope.getParent() ? scopeOffsets.lookup(*scope.getParent())
                                       : 0);
    writer.setFirstChild(
        children[scope.getId()].empty() ? 0 : children[scope.getId()][0]);
    writer.setNextSibling(nextSiblings.lookup(self));
    std::string generatedName = fallbackName("scope", scope.getId());
    StringRef name = scope.getHierarchicalName().value_or(
        scope.getDebugName().value_or(generatedName));
    writer.setName(stringOffset + intern(name));
    Source source = sourceFor(scope);
    writer.setSourceFile(
        source.file.empty() ? 0 : stringOffset + intern(source.file));
    writer.setSourceLineColumn(source.lineColumn);
  }
  for (auto [index, object] : llvm::enumerate(objects)) {
    uint64_t self = objectOffset + index * ObjectLayout.size;
    ObjectWriter writer(output.data() + self);
    uint32_t packedKind = 0;
    if (!tryPackRecordKindPayload(static_cast<RecordKind>(object.kind),
                                  object.vpiKind, packedKind)) {
      design.emitOpError("object record or VPI kind cannot be packed");
      return {};
    }
    writer.setKindAndVPIKind(packedKind);
    writer.setCaps(object.caps);
    writer.setID(object.id);
    writer.setOwner(scopeOffsets.lookup(object.scope));
    writer.setNextSibling(nextSiblings.lookup(self));
    writer.setSourceFile(object.source.file.empty()
                             ? 0
                             : stringOffset + intern(object.source.file));
    writer.setName(stringOffset + intern(object.name));
    uint64_t width = 0;
    if (object.type) {
      uint32_t typeIndex = typeIndices.lookup(object.type);
      width = *simulationWidth(object.type);
      writer.setType(typeOffset + uint64_t{typeIndex} * TypeLayout.size);
    } else {
      writer.setType(0);
      if (auto anchor =
              dyn_cast_or_null<sim::SimVPIObjectAnchorOp>(object.identity)) {
        if (DenseI64ArrayAttr ranges = anchor.getIndexRangesAttr()) {
          width = 1;
          ArrayRef<int64_t> values = ranges.asArrayRef();
          for (size_t dimension = 0; dimension != values.size();
               dimension += 2) {
            uint64_t ordinal = 0, extent = 0;
            if (!declaredIndexOrdinal(values[dimension], values[dimension + 1],
                                      values[dimension], ordinal, extent) ||
                extent > UINT64_MAX / width) {
              anchor.emitOpError("VPI array extent cannot be encoded");
              return {};
            }
            width *= extent;
          }
        } else if (DenseI64ArrayAttr indices = anchor.getSparseIndicesAttr()) {
          width = indices.size();
        } else if (auto inputs = anchor.getPrimitiveInputCount()) {
          width = *inputs;
        }
      }
    }
    writer.setWidth(width);
    int64_t left = width == 0 ? 0 : static_cast<int64_t>(width - 1);
    int64_t right = 0;
    if (auto array = dyn_cast_if_present<sim::PackedArrayType>(object.type)) {
      left = array.getLeft();
      right = array.getRight();
    } else if (auto array =
                   dyn_cast_if_present<sim::UnpackedArrayType>(object.type)) {
      left = array.getLeft();
      right = array.getRight();
    } else if (auto anchor = dyn_cast_or_null<sim::SimVPIObjectAnchorOp>(
                   object.identity)) {
      if (DenseI64ArrayAttr ranges = anchor.getIndexRangesAttr();
          ranges && !ranges.empty()) {
        left = ranges.asArrayRef()[0];
        right = ranges.asArrayRef()[1];
      } else if (anchor.getSparseIndicesAttr()) {
        left = 0;
        right = 0;
      }
    }
    writer.setLeft(left);
    writer.setRight(right);
    writer.setStateOffset(object.stateOffset);
    writer.setSourceLineColumn(object.source.lineColumn);
  }
  for (auto [index, object] : llvm::enumerate(staticObjects)) {
    if (object.vpiKind > UINT16_MAX) {
      object.identity->emitError("compact VPI object kind exceeds 16 bits");
      return {};
    }
    StaticObjectWriter writer(output.data() + staticObjectOffset +
                              index * StaticObjectLayout.size);
    writer.setID(object.id);
    writer.setScopeIndex(scopeIndices.lookup(object.scope));
    writer.setSourceFile(
        object.source.file.empty()
            ? 0
            : static_cast<uint32_t>(intern(object.source.file)));
    writer.setName(
        object.name.empty() ? 0 : static_cast<uint32_t>(intern(object.name)));
    writer.setSourceLine(static_cast<uint32_t>(object.source.lineColumn >> 32));
    writer.setSourceColumn(static_cast<uint32_t>(object.source.lineColumn));
    writer.setVPIKind(static_cast<uint16_t>(object.vpiKind));
    writer.setFlags(object.flags);
  }
  for (auto [index, entry] : llvm::enumerate(types)) {
    TypeWriter writer(output.data() + typeOffset + index * TypeLayout.size);
    writer.setRecordKind(static_cast<uint32_t>(RecordKind::Type));
    writer.setKindAndFlags(entry.kind | (entry.flags << 8));
    writer.setWidth(entry.width);
    writer.setLeft(entry.left);
    writer.setRight(entry.right);
    writer.setElement(entry.element == UINT32_MAX
                          ? 0
                          : typeOffset +
                                uint64_t{entry.element} * TypeLayout.size);
    writer.setFirstChild(entry.firstChild == UINT32_MAX
                             ? 0
                             : typeOffset + uint64_t{entry.firstChild} *
                                                TypeLayout.size);
    writer.setChildCount(entry.childCount);
    writer.setOrdinal(entry.ordinal);
    writer.setPackedOffset(entry.packedOffset);
    writer.setName(stringOffset + intern(entry.name));
  }
  for (auto [index, entry] : llvm::enumerate(statements)) {
    sim::SimStatementDeclOp statement = entry.declaration;
    auto scope = scopeIndices.find(statement.getScopeId());
    if (scope == scopeIndices.end()) {
      statement.emitOpError("statement reflection scope was not serialized");
      return {};
    }
    uint32_t ownerIndex = UINT32_MAX;
    if (auto ownerID = statement.getCodeUnitId()) {
      auto owner = codeUnitObjectIndices.find(*ownerID);
      if (owner == codeUnitObjectIndices.end()) {
        statement.emitOpError("statement reflection owner was not serialized");
        return {};
      }
      ownerIndex = owner->second;
    }
    StatementWriter writer(output.data() + statementOffset +
                           index * StatementLayout.size);
    writer.setID(statement.getId());
    writer.setOwnerObjectIndex(ownerIndex);
    writer.setScopeIndex(scope->second);
    writer.setParentIndex(
        statement.getParentId()
            ? statementIndices.lookup(*statement.getParentId())
            : UINT32_MAX);
    if (entry.source.file.empty()) {
      writer.setSourceFile(0);
      writer.setSourceLine(0);
      writer.setSourceColumn(0);
    } else {
      uint64_t relative = intern(entry.source.file);
      if (relative > UINT32_MAX) {
        statement.emitOpError("statement source string offset exceeds 32 bits");
        return {};
      }
      writer.setSourceFile(static_cast<uint32_t>(relative));
      writer.setSourceLine(
          static_cast<uint32_t>(entry.source.lineColumn >> 32));
      writer.setSourceColumn(static_cast<uint32_t>(entry.source.lineColumn));
    }
    if (auto name = statement.getName()) {
      uint64_t relative = intern(*name);
      if (relative > UINT32_MAX) {
        statement.emitOpError("statement name string offset exceeds 32 bits");
        return {};
      }
      writer.setName(static_cast<uint32_t>(relative));
    } else {
      writer.setName(0);
    }
    writer.setVPIKind(static_cast<uint16_t>(statement.getVpiKind()));
    uint16_t flags = 0;
    if (statement.getIsProtected())
      flags |= OBELISK_RT_DESIGN_STATEMENT_PROTECTED;
    if (statement.getIsScope())
      flags |= OBELISK_RT_DESIGN_STATEMENT_SCOPE;
    writer.setFlags(flags);
  }
  for (auto [index, entry] : llvm::enumerate(statementSites)) {
    sim::SimStatementSiteDeclOp site = entry.declaration;
    auto statement = statementIndices.find(site.getStatementId());
    if (statement == statementIndices.end()) {
      site.emitOpError("statement site target was not serialized");
      return {};
    }
    StatementSiteWriter writer(output.data() + statementSiteOffset +
                               index * StatementSiteLayout.size);
    writer.setID(site.getId());
    writer.setStatementIndex(statement->second);
    writer.setPhase(static_cast<uint16_t>(site.getPhase()));
    writer.setFlags(0);
  }
  for (auto [index, relation] : llvm::enumerate(relations)) {
    RelationWriter writer(output.data() + relationOffset +
                          index * RelationLayout.size);
    writer.setSourceIndex(relation.sourceIndex);
    writer.setTargetIndexAndTable(relation.targetIndexAndTable);
    writer.setOrdinal(relation.ordinal);
    writer.setSelector(relation.selector);
    writer.setSourceKindAndTable(relation.sourceKindAndTable);
  }
  if (includeStatements) {
    SemanticDirectoryWriter writer(output.data() + semanticDirectoryOffset);
    writer.setSemanticTypeOffset(semanticTypeOffset);
    writer.setSemanticTypeCount(semanticTypes.size());
    writer.setSemanticTypeEdgeOffset(semanticTypeEdgeOffset);
    writer.setSemanticTypeEdgeCount(semanticTypeEdges.size());
    writer.setSemanticRootBindingOffset(semanticRootBindingOffset);
    writer.setSemanticRootBindingCount(semanticRootBindings.size());
    writer.setClassTypeBindingOffset(classTypeBindingOffset);
    writer.setClassTypeBindingCount(classTypeBindings.size());
    writer.setSemanticClassBindingOffset(semanticClassBindingOffset);
    writer.setSemanticClassBindingCount(semanticClassBindings.size());
    writer.setClassMemberBindingOffset(classMemberBindingOffset);
    writer.setClassMemberBindingCount(classMemberBindings.size());
    writer.setModportBindingOffset(modportBindingOffset);
    writer.setModportBindingCount(modportBindings.size());
    writer.setMailboxElementBindingOffset(0);
    writer.setMailboxElementBindingCount(0);
    writer.setBoxTypeBindingOffset(boxTypeBindingOffset);
    writer.setBoxTypeBindingCount(boxTypeBindings.size());
    writer.setValueElementBindingOffset(valueElementBindingOffset);
    writer.setValueElementBindingCount(valueElementBindings.size());
    writer.setEventBindingOffset(eventBindingOffset);
    writer.setEventBindingCount(eventBindings.size());
    writer.setEventArrayStorageBindingOffset(eventArrayStorageBindingOffset);
    writer.setEventArrayStorageBindingCount(eventArrayStorageBindings.size());
    writer.setThreadOriginBindingOffset(threadOriginBindingOffset);
    writer.setThreadOriginBindingCount(threadOriginBindings.size());
    writer.setCallSiteBindingOffset(callSiteBindingOffset);
    writer.setCallSiteBindingCount(callSiteBindings.size());
    writer.setFrameLocalBindingOffset(frameLocalBindingOffset);
    writer.setFrameLocalBindingCount(frameLocalBindings.size());
    writer.setCallDispatchBindingOffset(callDispatchBindingOffset);
    writer.setCallDispatchBindingCount(callDispatchBindings.size());
    writer.setClassDispatchMethodBindingOffset(
        classDispatchMethodBindingOffset);
    writer.setClassDispatchMethodBindingCount(
        classDispatchMethodBindings.size());
    writer.setClassDispatchParentBindingOffset(
        classDispatchParentBindingOffset);
    writer.setClassDispatchParentBindingCount(
        classDispatchParentBindings.size());
    writer.setInterfaceCallableOffset(interfaceCallableOffset);
    writer.setInterfaceCallableCount(interfaceCallables.size());
    writer.setInterfaceInstanceOffset(interfaceInstanceOffset);
    writer.setInterfaceInstanceCount(interfaceInstances.size());
    writer.setInterfaceCallableBindingOffset(interfaceCallableBindingOffset);
    writer.setInterfaceCallableBindingCount(interfaceCallableBindings.size());
    writer.setInterfaceCallBindingOffset(interfaceCallBindingOffset);
    writer.setInterfaceCallBindingCount(interfaceCallBindings.size());
    writer.setCallableSignatureBindingOffset(callableSignatureBindingOffset);
    writer.setCallableSignatureBindingCount(callableSignatureBindings.size());
    writer.setCodeUnitScopeBindingOffset(codeUnitScopeBindingOffset);
    writer.setCodeUnitScopeBindingCount(codeUnitScopeBindings.size());
    writer.setInitializerBindingOffset(initializerBindingOffset);
    writer.setInitializerBindingCount(initializerBindings.size());
    writer.setExpressionRootBindingOffset(expressionRootBindingOffset);
    writer.setExpressionRootBindingCount(expressionRoots.size());
    writer.setExpressionTemplateOffset(expressionTemplateOffset);
    writer.setExpressionTemplateCount(expressionTemplates.size());
    writer.setExpressionNodeOffset(expressionNodeOffset);
    writer.setExpressionNodeCount(expressionNodes.size());
    writer.setExpressionOperandOffset(expressionOperandOffset);
    writer.setExpressionOperandCount(expressionOperands.size());
    writer.setExpressionLeafOffset(expressionLeafOffset);
    writer.setExpressionLeafCount(expressionLeaves.size());
    writer.setExpressionBoundMemberOffset(expressionBoundMemberOffset);
    writer.setExpressionBoundMemberCount(expressionBoundMembers.size());
    writer.setExpressionLeafShapeOffset(expressionLeafShapeOffset);
    writer.setExpressionLeafShapeCount(expressionLeafShapes.size());
    writer.setTerminalDriverBindingOffset(terminalDriverBindingOffset);
    writer.setTerminalDriverBindingCount(terminalDriverBindings.size());

    writer.setRelationIndexOffset(relationIndexOffset);
    writer.setRelationIndexCount(relationIndices.size());
    writer.setRelationIndexDimensionOffset(relationIndexDimensionOffset);
    writer.setRelationIndexDimensionCount(relationIndexDimensions.size());
    writer.setRelationIndexKeyOffset(relationIndexKeyOffset);
    writer.setRelationIndexKeyCount(relationIndexKeys.size());
    writer.setRelationIndexMemberOffset(relationIndexMemberOffset);
    writer.setRelationIndexMemberCount(relationIndexMembers.size());
    writer.setFixedPropertyOffset(fixedPropertyOffset);
    writer.setFixedPropertyCount(fixedProperties.size());
    writer.setResolvedNetRunOffset(resolvedNetRunOffset);
    writer.setResolvedNetRunCount(resolvedNetRuns.size());
    writer.setNetDelayRunOffset(netDelayRunOffset);
    writer.setNetDelayRunCount(netDelayRuns.size());
    writer.setStaticObjectOffset(staticObjectOffset);
    writer.setStaticObjectCount(staticObjects.size());
    writer.setDefinitionOffset(definitionOffset);
    writer.setDefinitionCount(definitions.size());
    writer.setDefinitionBindingOffset(definitionBindingOffset);
    writer.setDefinitionBindingCount(definitionBindings.size());
    writer.setDefinitionMemberOffset(definitionMemberOffset);
    writer.setDefinitionMemberCount(definitionMembers.size());
    writer.setDefinitionMemberRelationOffset(definitionMemberRelationOffset);
    writer.setDefinitionMemberRelationCount(definitionMemberRelations.size());
    writer.setDefinitionMemberRelationTargetOffset(
        definitionMemberRelationTargetOffset);
    writer.setDefinitionMemberRelationTargetCount(
        definitionMemberRelationTargets.size());
    writer.setDefinitionSpecializationOffset(definitionSpecializationOffset);
    writer.setDefinitionSpecializationCount(definitionSpecializations.size());
    writer.setDefinitionSpecializationBindingOffset(
        definitionSpecializationBindingOffset);
    writer.setDefinitionSpecializationBindingCount(
        definitionSpecializationBindings.size());
    writer.setDefinitionParameterValueOffset(definitionParameterValueOffset);
    writer.setDefinitionParameterValueCount(definitionParameterValues.size());
    writer.setDefinitionMemberEndpointOffset(definitionMemberEndpointOffset);
    writer.setDefinitionMemberEndpointCount(definitionMemberEndpoints.size());
    writer.setDefinitionMemberInstanceRelationOffset(
        definitionMemberInstanceRelationOffset);
    writer.setDefinitionMemberInstanceRelationCount(
        definitionMemberInstanceRelations.size());
    writer.setDefinitionMemberInstanceRelationTargetOffset(
        definitionMemberInstanceRelationTargetOffset);
    writer.setDefinitionMemberInstanceRelationTargetCount(
        definitionMemberInstanceRelationTargets.size());
    writer.setDefinitionMemberInstanceRelationInverseOffset(
        definitionMemberInstanceRelationInverseOffset);
    writer.setDefinitionMemberInstanceRelationInverseCount(
        definitionMemberInstanceRelationInverses.size());
    writer.setFrozenValueOffset(frozenValueOffset);
    writer.setFrozenValueCount(frozenValues.size());
    writer.setFrozenValueBindingOffset(frozenValueBindingOffset);
    writer.setFrozenValueBindingCount(frozenValueBindings.size());
    writer.setFrozenValuePayloadOffset(frozenValuePayloadOffset);
    writer.setFrozenValuePayloadSize(frozenValuePayload.size());
  }
  for (auto [index, entry] : llvm::enumerate(semanticTypes)) {
    SemanticTypeWriter writer(output.data() + semanticTypeOffset +
                              index * SemanticTypeLayout.size);
    uint32_t flags = static_cast<uint32_t>(entry.semantic.getKind());
    if (entry.semantic.getIsSigned())
      flags |= OBELISK_RT_DESIGN_SEMANTIC_SIGNED;
    if (entry.semantic.getIsFourState())
      flags |= OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE;
    // Scalar bit/logic/reg types have a one-bit intrinsic [0:0] range. Wider
    // direct integral nodes retain an explicit source packed dimension; an
    // explicitly ranged one-bit type remains a PackedArray node.
    bool directIntegralDimension = false;
    switch (entry.semantic.getKind()) {
    case sim::VPITypeKind::Bit:
    case sim::VPITypeKind::Logic:
    case sim::VPITypeKind::Reg: {
      ArrayRef<int64_t> range = entry.semantic.getRange().asArrayRef();
      directIntegralDimension = range[0] != range[1];
      break;
    }
    default:
      break;
    }
    if (entry.semantic.getKind() == sim::VPITypeKind::PackedArray ||
        entry.semantic.getKind() == sim::VPITypeKind::UnpackedArray ||
        directIntegralDimension)
      flags |= OBELISK_RT_DESIGN_SEMANTIC_HAS_RANGE;
    if (entry.semantic.getIsTagged() && entry.semantic.getIsTagged().getValue())
      flags |= OBELISK_RT_DESIGN_SEMANTIC_TAGGED;
    if (entry.semantic.getIsSoft() && entry.semantic.getIsSoft().getValue())
      flags |= OBELISK_RT_DESIGN_SEMANTIC_SOFT;
    if (entry.semantic.getWildcardIndex() &&
        entry.semantic.getWildcardIndex().getValue())
      flags |= OBELISK_RT_DESIGN_SEMANTIC_WILDCARD_INDEX;
    flags |= entry.publicVPIKind
             << OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_SHIFT;
    writer.setKindAndFlags(flags);
    writer.setFirstEdge(entry.firstEdge);
    writer.setEdgeCount(entry.edgeCount);
    writer.setAliasObjectIndexAndTable(entry.aliasObject);
    writer.setIdentityTargetIndexAndTable(entry.identityTarget);
    writer.setName(
        entry.semantic.getName()
            ? static_cast<uint32_t>(intern(entry.semantic.getName().getValue()))
            : 0);
    writer.setModport(entry.semantic.getModport()
                          ? static_cast<uint32_t>(
                                intern(entry.semantic.getModport().getValue()))
                          : 0);
    writer.setQueueBound(
        entry.semantic.getQueueBound()
            ? static_cast<uint32_t>(
                  entry.semantic.getQueueBound().getValue().getZExtValue())
            : 0);
    ArrayRef<int64_t> range = entry.semantic.getRange().asArrayRef();
    writer.setLeft(range.empty() ? 0 : range[0]);
    writer.setRight(range.empty() ? 0 : range[1]);
    writer.setBitWidth(
        entry.semantic.getBitWidth()
            ? entry.semantic.getBitWidth().getValue().getZExtValue()
            : 0);
    writer.setTagBits(
        entry.semantic.getTagBits()
            ? entry.semantic.getTagBits().getValue().getZExtValue()
            : 0);
  }
  for (auto [index, entry] : llvm::enumerate(semanticTypeEdges)) {
    SemanticTypeEdgeWriter writer(output.data() + semanticTypeEdgeOffset +
                                  index * SemanticTypeEdgeLayout.size);
    writer.setChild(entry.child);
    writer.setRoleAndFlags(entry.role | (entry.flags << 8));
    writer.setOrdinal(entry.ordinal);
    writer.setName(
        entry.name.empty() ? 0 : static_cast<uint32_t>(intern(entry.name)));
    writer.setPackedOffset(entry.packedOffset);
  }
  for (auto [index, binding] : llvm::enumerate(classTypeBindings)) {
    ClassTypeBindingWriter writer(output.data() + classTypeBindingOffset +
                                  index * ClassTypeBindingLayout.size);
    writer.setClassID(binding.classID);
    writer.setTypespecIndexAndTable(binding.typespecIndexAndTable);
  }
  for (auto [index, binding] : llvm::enumerate(boxTypeBindings)) {
    BoxTypeBindingWriter writer(output.data() + boxTypeBindingOffset +
                                index * BoxTypeBindingLayout.size);
    writer.setID(binding.id);
    writer.setSemanticTypeIndex(binding.semanticType);
    writer.setPhysicalTypeIndex(binding.physicalType);
  }
  for (auto [index, binding] : llvm::enumerate(valueElementBindings)) {
    ValueElementBindingWriter writer(output.data() + valueElementBindingOffset +
                                     index * ValueElementBindingLayout.size);
    writer.setOwnerKind(static_cast<uint32_t>(binding.ownerKind));
    writer.setOwnerIndex(binding.ownerIndex);
    writer.setSemanticTypeIndex(binding.semanticType);
    writer.setPhysicalTypeIndex(binding.physicalType);
  }
  for (auto [index, binding] : llvm::enumerate(frameLocalBindings)) {
    FrameLocalBindingWriter writer(output.data() + frameLocalBindingOffset +
                                   index * FrameLocalBindingLayout.size);
    writer.setID(binding.id);
    writer.setObjectIndex(binding.object);
    writer.setOwnerCodeUnitIndex(binding.owner);
    writer.setScopeIndexAndTable(binding.scope);
    writer.setOrdinal(binding.ordinal);
  }
  for (auto [index, binding] : llvm::enumerate(initializerBindings)) {
    InitializerBindingWriter writer(output.data() + initializerBindingOffset +
                                    index * InitializerBindingLayout.size);
    writer.setCodeUnitID(binding.codeUnitID);
    writer.setSourceIndexAndTable(binding.source);
    writer.setReserved(0);
  }
  for (auto [index, binding] : llvm::enumerate(codeUnitScopeBindings)) {
    CodeUnitScopeBindingWriter writer(output.data() +
                                      codeUnitScopeBindingOffset +
                                      index * CodeUnitScopeBindingLayout.size);
    writer.setCodeUnitID(binding.codeUnitID);
    writer.setSourceIndexAndTable(binding.source);
    writer.setFlags(binding.flags);
  }
  for (auto [index, binding] : llvm::enumerate(callSiteBindings)) {
    CallSiteBindingWriter writer(output.data() + callSiteBindingOffset +
                                 index * CallSiteBindingLayout.size);
    writer.setID(binding.id);
    writer.setObjectIndexAndTable(binding.object);
    writer.setOwnerCodeUnitIndex(binding.owner);
    writer.setFlags(binding.flags);
    writer.setReserved(0);
  }
  for (auto [index, binding] : llvm::enumerate(callDispatchBindings)) {
    CallDispatchBindingWriter writer(output.data() + callDispatchBindingOffset +
                                     index * CallDispatchBindingLayout.size);
    writer.setCallID(binding.callID);
    writer.setCodeUnitID(binding.codeUnitID);
    writer.setReceiverClassID(binding.receiverClassID);
    writer.setSlot(binding.slot);
    writer.setSignatureID(binding.signatureID);
  }
  for (auto [index, binding] : llvm::enumerate(classDispatchMethodBindings)) {
    ClassDispatchMethodBindingWriter writer(
        output.data() + classDispatchMethodBindingOffset +
        index * ClassDispatchMethodBindingLayout.size);
    writer.setClassID(binding.classID);
    writer.setSlot(binding.slot);
    writer.setSignatureID(binding.signatureID);
    writer.setCodeUnitIndex(binding.codeUnitIndex);
    writer.setFlags(binding.flags);
  }
  for (auto [index, binding] : llvm::enumerate(classDispatchParentBindings)) {
    ClassDispatchParentBindingWriter writer(
        output.data() + classDispatchParentBindingOffset +
        index * ClassDispatchParentBindingLayout.size);
    writer.setClassID(binding.classID);
    writer.setParentClassID(binding.parentClassID);
    writer.setKind(binding.kind);
    writer.setFlags(binding.flags);
  }
  for (auto [index, member] : llvm::enumerate(interfaceCallables)) {
    InterfaceCallableWriter writer(output.data() + interfaceCallableOffset +
                                   index * InterfaceCallableLayout.size);
    writer.setTypeIndexAndTable(member.nominal);
    writer.setNameOffset(stringOffsets.lookup(member.name));
    writer.setNameSize(member.name.size());
    writer.setKind(member.kind);
    writer.setFlags(member.flags);
    writer.setSignatureID(member.signature);
    writer.setReserved(0);
  }
  for (auto [index, binding] : llvm::enumerate(callableSignatureBindings)) {
    CallableSignatureBindingWriter writer(
        output.data() + callableSignatureBindingOffset +
        index * CallableSignatureBindingLayout.size);
    writer.setCodeUnitIndex(binding.unit);
    writer.setSignatureID(binding.signature);
    writer.setFlags(binding.flags);
  }
  for (auto [index, instance] : llvm::enumerate(interfaceInstances)) {
    InterfaceInstanceWriter writer(output.data() + interfaceInstanceOffset +
                                   index * InterfaceInstanceLayout.size);
    writer.setScopeIndex(instance.scope);
    writer.setTypeIndexAndTable(instance.nominal);
  }
  for (auto [index, binding] : llvm::enumerate(interfaceCallableBindings)) {
    InterfaceCallableBindingWriter writer(
        output.data() + interfaceCallableBindingOffset +
        index * InterfaceCallableBindingLayout.size);
    writer.setMemberIndex(binding.member);
    writer.setScopeIndex(binding.scope);
    writer.setDeclarationIndex(binding.declaration);
    writer.setImplementationIndex(binding.implementation);
  }
  for (auto [index, binding] : llvm::enumerate(interfaceCallBindings)) {
    InterfaceCallBindingWriter writer(output.data() +
                                      interfaceCallBindingOffset +
                                      index * InterfaceCallBindingLayout.size);
    writer.setCallID(binding.id);
    writer.setMemberIndex(binding.member);
    writer.setAccess(binding.access);
  }
  llvm::sort(threadOriginBindings, [](const auto &left, const auto &right) {
    return left.codeUnit < right.codeUnit;
  });
  for (auto [index, binding] : llvm::enumerate(threadOriginBindings)) {
    ThreadOriginBindingWriter writer(output.data() + threadOriginBindingOffset +
                                     index * ThreadOriginBindingLayout.size);
    writer.setCodeUnitID(binding.codeUnit);
    writer.setStatementIndex(binding.statement);
    writer.setReserved(0);
  }
  llvm::sort(eventArrayStorageBindings,
             [](const auto &left, const auto &right) {
               return left.array < right.array;
             });
  for (auto [index, binding] : llvm::enumerate(eventArrayStorageBindings)) {
    EventArrayStorageBindingWriter writer(
        output.data() + eventArrayStorageBindingOffset +
        index * EventArrayStorageBindingLayout.size);
    writer.setArrayIndexAndTable(binding.array);
    writer.setStorageIndex(binding.storage);
  }
  llvm::sort(eventBindings, [](const auto &left, const auto &right) {
    return left.event < right.event;
  });
  for (auto [index, binding] : llvm::enumerate(eventBindings)) {
    EventBindingWriter writer(output.data() + eventBindingOffset +
                              index * EventBindingLayout.size);
    writer.setEventIndexAndTable(binding.event);
    writer.setReserved(0);
    writer.setEventID(binding.id);
  }
  llvm::sort(modportBindings, [](const auto &left, const auto &right) {
    return left.modport < right.modport;
  });
  for (auto [index, binding] : llvm::enumerate(modportBindings)) {
    ModportBindingWriter writer(output.data() + modportBindingOffset +
                                index * ModportBindingLayout.size);
    writer.setModportIndexAndTable(binding.modport);
    writer.setScopeIndex(binding.scope);
    writer.setName(static_cast<uint32_t>(intern(binding.name)));
  }
  for (auto [index, binding] : llvm::enumerate(classMemberBindings)) {
    ClassMemberBindingWriter writer(output.data() + classMemberBindingOffset +
                                     index * ClassMemberBindingLayout.size);
    writer.setMemberIndexAndTable(binding.member);
    writer.setOwnerIndexAndTable(binding.owner);
    writer.setClassID(binding.classID);
    writer.setByteOffset(binding.byteOffset);
    writer.setStorageIndexAndTable(binding.storage);
    writer.setFlags(binding.flags);
    writer.setEventID(binding.eventID);
    writer.setName(static_cast<uint32_t>(intern(binding.name)));
  }
  for (auto [index, binding] : llvm::enumerate(semanticClassBindings)) {
    SemanticClassBindingWriter writer(output.data() +
                                      semanticClassBindingOffset +
                                      index * SemanticClassBindingLayout.size);
    writer.setSemanticType(binding.semanticType);
    writer.setTypespecIndexAndTable(binding.typespecIndexAndTable);
  }
  for (auto [index, binding] : llvm::enumerate(semanticRootBindings)) {
    SemanticRootBindingWriter writer(output.data() + semanticRootBindingOffset +
                                     index * SemanticRootBindingLayout.size);
    writer.setObjectIndexAndTable(binding.objectIndexAndTable);
    writer.setSemanticType(binding.semanticType);
  }
  for (auto [index, definition] : llvm::enumerate(definitions)) {
    DefinitionWriter writer(output.data() + definitionOffset +
                            index * DefinitionLayout.size);
    writer.setVPIKind(
        static_cast<uint16_t>(definition.declaration.getVpiKind()));
    writer.setFlags(definition.declaration.getPrimitiveType().value_or(0));
    writer.setName(static_cast<uint32_t>(
        intern(definition.declaration.getDefinitionName())));
    writer.setFile(definition.source.file.empty()
                       ? 0
                       : static_cast<uint32_t>(intern(definition.source.file)));
    writer.setLine(static_cast<uint32_t>(definition.source.lineColumn >> 32));
    writer.setFirstMember(definition.firstMember);
    writer.setMemberCount(definition.memberCount);
    writer.setFirstMemberRelation(definition.firstMemberRelation);
    writer.setMemberRelationCount(definition.memberRelationCount);
  }
  for (auto [index, binding] : llvm::enumerate(definitionBindings)) {
    DefinitionBindingWriter writer(output.data() + definitionBindingOffset +
                                   index * DefinitionBindingLayout.size);
    writer.setSourceIndexAndTable(binding.sourceIndexAndTable);
    writer.setDefinition(binding.definition);
    writer.setSpecialization(binding.specialization);
    writer.setFirstMemberEndpoint(binding.firstMemberEndpoint);
  }
  for (auto [index, member] : llvm::enumerate(definitionMembers)) {
    DefinitionMemberWriter writer(output.data() + definitionMemberOffset +
                                  index * DefinitionMemberLayout.size);
    writer.setName(
        static_cast<uint32_t>(intern(member.declaration.getMemberName())));
    writer.setFile(member.source.file.empty()
                       ? 0
                       : static_cast<uint32_t>(intern(member.source.file)));
    writer.setLine(static_cast<uint32_t>(member.source.lineColumn >> 32));
    writer.setColumn(static_cast<uint32_t>(member.source.lineColumn));
    writer.setVPIKind(static_cast<uint16_t>(member.declaration.getVpiKind()));
    uint16_t flags =
        member.declaration.getDirectionAttr()
            ? static_cast<uint16_t>(member.declaration.getDirection().value())
            : 0;
    if (member.declaration.getIsLocalParam())
      flags |= 1u << 8;
    if (member.declaration.getHasExplicitParameterRange())
      flags |= 1u << 9;
    if (member.declaration.getUnavailableDefault())
      flags |= 1u << 10;
    if (member.declaration.getHasOriginalDefault())
      flags |= 1u << 11;
    writer.setFlags(flags);
  }
  for (auto [index, relation] : llvm::enumerate(definitionMemberRelations)) {
    DefinitionMemberRelationWriter writer(
        output.data() + definitionMemberRelationOffset +
        index * DefinitionMemberRelationLayout.size);
    writer.setSelector(relation.selector);
    writer.setFlags(0);
    writer.setFirstTarget(relation.firstTarget);
    writer.setTargetCount(relation.targetCount);
  }
  for (auto [index, member] :
       llvm::enumerate(definitionMemberRelationTargets)) {
    DefinitionMemberRelationTargetWriter writer(
        output.data() + definitionMemberRelationTargetOffset +
        index * DefinitionMemberRelationTargetLayout.size);
    writer.setMember(member);
  }
  for (auto [index, specialization] :
       llvm::enumerate(definitionSpecializations)) {
    DefinitionSpecializationWriter writer(
        output.data() + definitionSpecializationOffset +
        index * DefinitionSpecializationLayout.size);
    writer.setDefinition(specialization.definition);
    writer.setFirstBinding(specialization.firstBinding);
    writer.setBindingCount(specialization.bindingCount);
  }
  for (auto [index, binding] :
       llvm::enumerate(definitionSpecializationBindings)) {
    DefinitionSpecializationBindingWriter writer(
        output.data() + definitionSpecializationBindingOffset +
        index * DefinitionSpecializationBindingLayout.size);
    writer.setMember(binding.member);
    writer.setSemanticType(binding.semanticType);
  }
  for (auto [index, value] : llvm::enumerate(definitionParameterValues)) {
    DefinitionParameterValueWriter writer(
        output.data() + definitionParameterValueOffset +
        index * DefinitionParameterValueLayout.size);
    writer.setSpecializationBinding(value.binding);
    writer.setFrozenValue(value.value);
  }
  for (auto [index, endpoint] : llvm::enumerate(definitionMemberEndpoints)) {
    DefinitionMemberEndpointWriter writer(
        output.data() + definitionMemberEndpointOffset +
        index * DefinitionMemberEndpointLayout.size);
    writer.setTargetIndexAndTable(endpoint.targetIndexAndTable);
  }
  for (auto [index, relation] :
       llvm::enumerate(definitionMemberInstanceRelations)) {
    DefinitionMemberInstanceRelationWriter writer(
        output.data() + definitionMemberInstanceRelationOffset +
        index * DefinitionMemberInstanceRelationLayout.size);
    writer.setBinding(relation.binding);
    writer.setMemberAndFlags(relation.memberAndFlags);
    writer.setSelector(relation.selector);
    writer.setModeAndFlags(relation.modeAndFlags);
    writer.setFirstTarget(relation.firstTarget);
    writer.setTargetCount(relation.targetCount);
  }
  for (auto [index, target] :
       llvm::enumerate(definitionMemberInstanceRelationTargets)) {
    DefinitionMemberInstanceRelationTargetWriter writer(
        output.data() + definitionMemberInstanceRelationTargetOffset +
        index * DefinitionMemberInstanceRelationTargetLayout.size);
    writer.setTargetIndexAndTable(target);
  }
  for (auto [index, inverse] :
       llvm::enumerate(definitionMemberInstanceRelationInverses)) {
    DefinitionMemberInstanceRelationInverseWriter writer(
        output.data() + definitionMemberInstanceRelationInverseOffset +
        index * DefinitionMemberInstanceRelationInverseLayout.size);
    writer.setTarget(inverse.target);
  }
  for (auto [index, binding] : llvm::enumerate(terminalDriverBindings)) {
    TerminalDriverBindingWriter writer(
        output.data() + terminalDriverBindingOffset +
        index * TerminalDriverBindingLayout.size);
    writer.setTerminalIndexAndTable(binding.terminal);
    writer.setLowDriverIndex(binding.low);
    writer.setHighDriverIndex(binding.high);
    writer.setBitOffset(binding.bit);
  }
  for (auto [index, root] : llvm::enumerate(expressionRoots)) {
    ExpressionRootBindingWriter writer(
        output.data() + expressionRootBindingOffset +
        index * ExpressionRootBindingLayout.size);
    writer.setOwnerIndexAndTable(root.owner);
    writer.setTemplateIndex(root.templateIndex);
    writer.setFirstLeaf(root.mode & expressionRootConcreteTypeDefault
                            ? (root.source.file.empty()
                                   ? UINT32_MAX
                                   : uint32_t(intern(root.source.file)))
                            : root.firstLeaf);
    writer.setSelector(root.selector);
    writer.setMode(root.mode);
    writer.setOrdinal(root.ordinal);
  }
  for (auto [index, recipe] : llvm::enumerate(expressionTemplates)) {
    ExpressionTemplateWriter writer(output.data() + expressionTemplateOffset +
                                    index * ExpressionTemplateLayout.size);
    writer.setDefinitionIndex(recipe.definition);
    writer.setFirstNode(recipe.firstNode);
    writer.setNodeCount(recipe.nodeCount);
    writer.setFirstOperand(recipe.firstOperand);
    writer.setOperandCount(recipe.operandCount);
    writer.setLeafCount(recipe.leafCount);
    writer.setFirstLeafShape(recipe.firstLeafShape);
  }
  for (auto [index, node] : llvm::enumerate(expressionNodes)) {
    ExpressionNodeWriter writer(output.data() + expressionNodeOffset +
                                index * ExpressionNodeLayout.size);
    writer.setKind(node.kind);
    writer.setCode(node.code);
    writer.setSemanticTypeIndex(node.semantic);
    writer.setFirstOperand(node.firstOperand);
    writer.setOperandCount(node.operandCount);
    writer.setFile(node.source.file.empty()
                       ? UINT32_MAX
                       : uint32_t(intern(node.source.file)));
    writer.setLine(node.source.lineColumn >> 32);
    writer.setColumn(node.source.lineColumn);
    writer.setFrozenValueIndex(node.frozen);
  }
  for (auto [index, target] : llvm::enumerate(expressionOperands)) {
    ExpressionOperandWriter writer(output.data() + expressionOperandOffset +
                                   index * ExpressionOperandLayout.size);
    writer.setTarget(target);
  }
  for (auto [index, target] : llvm::enumerate(expressionLeaves)) {
    ExpressionLeafWriter writer(output.data() + expressionLeafOffset +
                                index * ExpressionLeafLayout.size);
    writer.setTarget(target);
  }
  for (auto [index, pair] : llvm::enumerate(expressionBoundMembers)) {
    ExpressionBoundMemberWriter writer(
        output.data() + expressionBoundMemberOffset +
        index * ExpressionBoundMemberLayout.size);
    writer.setDefinitionBindingIndex(pair.first);
    writer.setMemberIndex(pair.second);
  }
  for (auto [index, shape] : llvm::enumerate(expressionLeafShapes)) {
    ExpressionLeafShapeWriter writer(output.data() + expressionLeafShapeOffset +
                                     index * ExpressionLeafShapeLayout.size);
    writer.setSemanticTypeIndex(shape.semantic);
    writer.setKind(shape.kind);
    writer.setReserved(0);
    writer.setIdentifier(shape.identifier
                             ? uint32_t(intern(shape.identifier.getValue()))
                             : UINT32_MAX);
  }
  for (auto [index, frozen] : llvm::enumerate(frozenValues)) {
    FrozenValueWriter writer(output.data() + frozenValueOffset +
                             index * FrozenValueLayout.size);
    writer.setKindAndFlags(frozen.kindAndFlags);
    writer.setReserved(0);
    writer.setBitWidth(frozen.bitWidth);
    writer.setPayloadOffset(frozen.payloadOffset);
    writer.setPayloadSize(frozen.payloadSize);
  }
  for (auto [index, binding] : llvm::enumerate(frozenValueBindings)) {
    FrozenValueBindingWriter writer(output.data() + frozenValueBindingOffset +
                                    index * FrozenValueBindingLayout.size);
    writer.setSourceIndexAndTable(binding.sourceIndexAndTable);
    writer.setValue(binding.value);
  }
  llvm::copy(frozenValuePayload, output.begin() + frozenValuePayloadOffset);
  for (auto [index, entry] : llvm::enumerate(relationIndices)) {
    RelationIndexWriter writer(output.data() + relationIndexOffset +
                               index * RelationIndexLayout.size);
    writer.setObjectIndex(entry.objectIndex);
    writer.setFirstDimension(entry.firstDimension);
    writer.setDimensionCount(entry.dimensionCount);
    writer.setFlags(entry.flags);
    writer.setFirstKey(entry.firstKey);
    writer.setFirstOrdinalKey(entry.firstOrdinalKey);
  }
  for (auto [index, entry] : llvm::enumerate(relationIndexDimensions)) {
    RelationIndexDimensionWriter writer(
        output.data() + relationIndexDimensionOffset +
        index * RelationIndexDimensionLayout.size);
    writer.setLeft(entry.left);
    writer.setRight(entry.right);
  }
  for (auto [index, entry] : llvm::enumerate(relationIndexKeys)) {
    RelationIndexKeyWriter writer(output.data() + relationIndexKeyOffset +
                                  index * RelationIndexKeyLayout.size);
    writer.setIndex(entry.index);
    writer.setOrdinal(entry.ordinal);
  }
  for (auto [index, entry] : llvm::enumerate(relationIndexMembers)) {
    RelationIndexMemberWriter writer(output.data() + relationIndexMemberOffset +
                                     index * RelationIndexMemberLayout.size);
    writer.setTargetIndexAndTable(entry.targetIndexAndTable);
    writer.setRelationIndex(entry.relationIndex);
    writer.setOrdinal(entry.ordinal);
  }
  for (auto [index, entry] : llvm::enumerate(fixedProperties)) {
    FixedPropertyWriter writer(output.data() + fixedPropertyOffset +
                               index * FixedPropertyLayout.size);
    writer.setSourceIndexAndTable(entry.sourceIndexAndTable);
    writer.setSelector(entry.selector);
    writer.setKindAndFlags(entry.kindAndFlags);
    writer.setPayload(entry.kindAndFlags == static_cast<uint16_t>(
                                                VPIPropertyValueKind::String)
                          ? stringOffset + intern(entry.stringPayload)
                          : entry.payload);
  }
  for (auto [index, entry] : llvm::enumerate(resolvedNetRuns)) {
    ResolvedNetRunWriter writer(output.data() + resolvedNetRunOffset +
                                index * ResolvedNetRunLayout.size);
    writer.setObjectIndex(entry.objectIndex);
    writer.setNetType(entry.netType);
    writer.setFirstBit(entry.firstBit);
    writer.setBitCount(entry.bitCount);
  }
  for (auto [index, entry] : llvm::enumerate(netDelayRuns)) {
    NetDelayRunWriter writer(output.data() + netDelayRunOffset +
                             index * NetDelayRunLayout.size);
    writer.setObjectIndex(entry.objectIndex);
    writer.setReserved(0);
    writer.setFirstBit(entry.firstBit);
    writer.setBitCount(entry.bitCount);
    writer.setRise(entry.delays[0]);
    writer.setFall(entry.delays[1]);
    writer.setThird(entry.delays[2]);
  }
  llvm::append_range(output, strings);
  alignTo(output, 8);
  indexOffset = output.size();
  struct Index {
    uint64_t hash, name, record;
    std::string text;
  };
  SmallVector<Index> names;
  for (auto scope : scopes) {
    std::string generatedName = fallbackName("scope", scope.getId());
    StringRef name = scope.getHierarchicalName().value_or(
        scope.getDebugName().value_or(generatedName));
    names.push_back({stableHash(name), stringOffset + intern(name),
                     scopeOffsets.lookup(scope.getId()), name.str()});
  }
  for (auto [index, object] : llvm::enumerate(objects))
    if (object.indexName)
      names.push_back({stableHash(object.name),
                       stringOffset + intern(object.name),
                       objectOffset + index * ObjectLayout.size, object.name});
  for (auto [index, object] : llvm::enumerate(staticObjects))
    if (!object.name.empty())
      names.push_back(
          {stableHash(object.name), stringOffset + intern(object.name),
           staticObjectOffset + index * StaticObjectLayout.size, object.name});
  llvm::sort(names, [](const Index &left, const Index &right) {
    return std::tie(left.hash, left.text) < std::tie(right.hash, right.text);
  });
  for (size_t index = 1; index < names.size(); ++index)
    if (names[index - 1].text == names[index].text) {
      design.emitOpError() << "duplicate hierarchical reflection name '"
                           << names[index].text << "'";
      return {};
    }
  output.resize(indexOffset + names.size() * IndexLayout.size, 0);
  for (auto [index, entry] : llvm::enumerate(names)) {
    IndexWriter writer(output.data() + indexOffset + index * IndexLayout.size);
    writer.setHash(entry.hash);
    writer.setName(entry.name);
    writer.setRecord(entry.record);
  }
  using Header = obelisk_rt_design_database_header_v1;
  static constexpr char magic[] = OBELISK_RT_DESIGN_DATABASE_MAGIC;
  static_assert(sizeof(magic) == sizeof(Header::magic));
  HeaderWriter writer(output.data());
  writer.setMagic(reinterpret_cast<const uint8_t *>(magic));
  writer.setVersion(OBELISK_RT_VERSION);
  writer.setReserved(static_cast<uint32_t>(semanticDirectoryOffset));
  writer.setProfile(profile);
  writer.setHeaderSize(HeaderLayout.size);
  writer.setImageSize(output.size());
  writer.setRoot(scopeOffsets.lookup(root.getId()));
  writer.setScopeOffset(scopeOffset);
  writer.setScopeCount(scopes.size());
  writer.setObjectOffset(objectOffset);
  writer.setObjectCount(objects.size());
  writer.setTypeOffset(typeOffset);
  writer.setTypeCount(types.size());
  writer.setStringOffset(stringOffset);
  writer.setStringSize(strings.size());
  writer.setIndexOffset(indexOffset);
  writer.setIndexCount(names.size());
  writer.setStatementOffset(statementOffset);
  writer.setStatementCount(statements.size());
  writer.setStatementSiteOffset(statementSiteOffset);
  writer.setStatementSiteCount(statementSites.size());
  writer.setRelationOffset(relationOffset);
  writer.setRelationCount(relations.size());
  writer.setChecksum(checksum(output, field::HeaderChecksum));
  return output;
}

} // namespace obelisk::bytecode
