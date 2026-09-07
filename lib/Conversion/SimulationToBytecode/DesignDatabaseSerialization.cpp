//===- DesignDatabaseSerialization.cpp - Reflection image emitter -------===//
//
// Serialize the pointer-free runtime reflection database independently from
// executable bytecode instruction selection.
//
//===----------------------------------------------------------------------===//

#include "BytecodeSerialization.h"

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
using namespace obelisk::reflection;
static_assert(RelationLayout.size == 16);
static_assert(static_cast<uint8_t>(TableKind::Scope) == 0);
static_assert(static_cast<uint8_t>(TableKind::Object) == 1);
static_assert(static_cast<uint8_t>(TableKind::Statement) == 2);
static_assert(tableKindPackedWidth == 2);
static_assert(tableKindPayloadMask == 0x3fff);
static_assert(canPackTableKindPayload(tableKindPayloadMask));
static_assert(!canPackTableKindPayload(tableKindPayloadMask + 1));

constexpr bool tableKindPackingIsStable() {
  for (TableKind table :
       {TableKind::Scope, TableKind::Object, TableKind::Statement}) {
    uint16_t packed = 0;
    if (!tryPackTableKindPayload(table, tableKindPayloadMask, packed) ||
        unpackTableKind(packed) != table ||
        unpackTableKindPayload(packed) != tableKindPayloadMask)
      return false;
  }
  uint16_t rejected = 0;
  return !tryPackTableKindPayload(static_cast<TableKind>(3), 0, rejected) &&
         !tryPackTableKindPayload(TableKind::Scope, tableKindPayloadMask + 1,
                                  rejected);
}
static_assert(tableKindPackingIsStable());

static_assert(tableIndexPackedShift == 30);
static_assert(tableIndexPayloadMask == 0x3fffffff);
static_assert(canPackTableIndex(tableIndexPayloadMask));
static_assert(!canPackTableIndex(tableIndexPayloadMask + 1));

constexpr bool tableIndexPackingIsStable() {
  for (TableKind table :
       {TableKind::Scope, TableKind::Object, TableKind::Statement}) {
    uint32_t packed = 0;
    if (!tryPackTableIndex(table, tableIndexPayloadMask, packed) ||
        unpackTableIndexKind(packed) != table ||
        unpackTableIndex(packed) != tableIndexPayloadMask)
      return false;
  }
  uint32_t rejected = 0;
  return !tryPackTableIndex(static_cast<TableKind>(3), 0, rejected) &&
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
  for (TableKind table :
       {TableKind::Scope, TableKind::Object, TableKind::Statement}) {
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
  return !tryPackRelationSource(static_cast<TableKind>(3), 0, false,
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
  struct Source {
    std::string file;
    uint64_t lineColumn = 0;
  };
  auto sourceFor = [](Operation *operation) {
    Source source;
    if (auto location = operation->getLoc()->findInstanceOf<FileLineColLoc>()) {
      source.file = location.getFilename().getValue().str();
      source.lineColumn =
          uint64_t{location.getLine()} << 32 | uint64_t{location.getColumn()};
    }
    return source;
  };
  auto hasFixedReflectionMetadata = [](Operation *operation) {
    return operation->hasAttr("is_protected") ||
           operation->hasAttr("definition_loc") ||
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
    std::string stringPayload;
  };
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
  SmallVector<StatementRecord> statements;
  SmallVector<StatementSiteRecord> statementSites;
  SmallVector<sim::SimVPIStatementRelationDeclOp> relationDeclarations;
  SmallVector<sim::SimVPIRelationDeclOp> generalRelationDeclarations;
  SmallVector<sim::SimVPIObjectAnchorOp> anchors;
  SmallVector<sim::SimVPITypespecDeclOp> typespecs;
  SmallVector<sim::SimVPIEnumConstDeclOp> enumConstants;
  llvm::StringMap<sim::SimVPIObjectAnchorOp> anchorsBySymbol;
  DenseMap<uint64_t, sim::SimVPIObjectAnchorOp> anchorsByInventoryId;
  llvm::StringMap<sim::SimVPITypespecDeclOp> typespecsBySymbol;
  DenseMap<uint64_t, sim::SimVPITypespecDeclOp> anonymousTypespecsByIdentity;
  SmallVector<RelationRecord> relations;
  SmallVector<FixedPropertyRecord> fixedProperties;
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
    if (isa<IntegerType, sim::LogicType>(type) || type.isF32() || type.isF64())
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
  llvm::DenseMap<uint64_t, PortSource> storageSources, netSources;
  llvm::DenseMap<uint64_t, sim::SimPortDeclOp> directStoragePorts,
      directNetPorts;
  llvm::DenseMap<uint64_t, PortConnection> wholePortConnections;
  llvm::DenseSet<uint64_t> wholeStoragePortSources, wholeNetPortSources;
  for (Operation &operation : design.getBody().front()) {
    if (auto anchor = dyn_cast<sim::SimVPIObjectAnchorOp>(operation)) {
      if (includeStatements) {
        anchors.push_back(anchor);
        anchorsBySymbol[anchor.getSymName()] = anchor;
        anchorsByInventoryId[anchor.getInventoryId()] = anchor;
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
    } else if (auto enumConstant =
                   dyn_cast<sim::SimVPIEnumConstDeclOp>(operation)) {
      if (includeStatements)
        enumConstants.push_back(enumConstant);
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

  constexpr uint64_t maxStaticInventoryID = (UINT64_MAX - 2) / 3;
  auto staticInventoryID = [&](Operation *operation, uint64_t id,
                               uint64_t tag) -> std::optional<uint64_t> {
    if (id > maxStaticInventoryID) {
      operation->emitError("VPI static inventory ID cannot be encoded");
      return std::nullopt;
    }
    return id * 3 + tag;
  };

  for (Operation &operation : design.getBody().front()) {
    if (auto scope = dyn_cast<sim::SimScopeDeclOp>(operation))
      scopes.push_back(scope);
    else if (auto statement = dyn_cast<sim::SimStatementDeclOp>(operation)) {
      if (includeStatements)
        statements.push_back({statement, sourceFor(statement)});
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
      objects.push_back({OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT,
                         anchor.getVpiKind(), 0, *id,
                         anchor.getEnclosingScopeId(), std::move(name), Type{},
                         0, sourceFor(anchor), true, anchor});
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
           typespec.getOrigin() == sim::VPITypespecOrigin::Typedef, typespec});
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
                         Type{}, 0, sourceFor(enumConstant), true,
                         enumConstant});
    } else if (auto storage = dyn_cast<sim::SimStorageDeclOp>(operation)) {
      if (!isReflectableType(storage.getType())) {
        if (includeStatements && hasFixedReflectionMetadata(storage)) {
          storage.emitOpError(
              "fixed VPI metadata would be dropped with an unsupported "
              "physical type");
          return {};
        }
        continue;
      }
      if (Attribute delegated =
              storage->getAttr(sim::metadata::vpiIdentityDelegated)) {
        auto reference = dyn_cast<FlatSymbolRefAttr>(delegated);
        auto owner = reference ? anchorsBySymbol.find(reference.getValue())
                               : anchorsBySymbol.end();
        if (includeStatements &&
            (!reference || owner == anchorsBySymbol.end() ||
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
        continue;
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
  llvm::sort(scopes, [](auto left, auto right) {
    return left.getId() < right.getId();
  });
  llvm::sort(objects, [](const Record &left, const Record &right) {
    return std::tie(left.scope, left.name, left.kind, left.id) <
           std::tie(right.scope, right.name, right.kind, right.id);
  });
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
      statements.size() > UINT32_MAX || statementSites.size() > UINT32_MAX) {
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

  DenseMap<uint64_t, uint32_t> scopeIndices, codeUnitObjectIndices,
      statementIndices, statementKinds;
  DenseMap<Operation *, uint32_t> objectIndices;
  DenseMap<uint64_t, uint32_t> canonicalStorageTargetIndices,
      canonicalNetTargetIndices, physicalNetObjectIndices;
  for (auto [index, scope] : llvm::enumerate(scopes))
    scopeIndices[scope.getId()] = static_cast<uint32_t>(index);
  for (auto [index, object] : llvm::enumerate(objects)) {
    if (object.identity)
      objectIndices[object.identity] = static_cast<uint32_t>(index);
    if (object.kind == OBELISK_RT_DESIGN_RECORD_PROCESS ||
        object.kind == OBELISK_RT_DESIGN_RECORD_FUNCTION)
      codeUnitObjectIndices[object.id] = static_cast<uint32_t>(index);
    if (isa_and_present<sim::SimNetDeclOp>(object.identity))
      physicalNetObjectIndices[object.id] = static_cast<uint32_t>(index);
    if (includeStatements && object.kind == OBELISK_RT_DESIGN_RECORD_STORAGE &&
        wholeStoragePortSources.contains(object.id))
      canonicalStorageTargetIndices.try_emplace(object.id,
                                                static_cast<uint32_t>(index));
    if (includeStatements && object.kind == OBELISK_RT_DESIGN_RECORD_NET &&
        wholeNetPortSources.contains(object.id))
      canonicalNetTargetIndices.try_emplace(object.id,
                                            static_cast<uint32_t>(index));
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

  DenseMap<uint64_t, uint32_t> automaticOrdinals;
  DenseSet<std::pair<uint64_t, uint32_t>> relationIdentities;
  auto relationIdentity = [](uint32_t sourceIndex, uint16_t selector,
                             uint16_t packedSource, uint32_t packedTarget) {
    uint64_t source = (uint64_t{sourceIndex} << 32) |
                      (uint64_t{selector} << 16) | packedSource;
    return std::pair(source, packedTarget);
  };
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
    if (!relationIdentities
             .insert(relationIdentity(sourceIndex, selector, packedSource,
                                      packedTarget))
             .second)
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
  DenseMap<Operation *, ImageObjectRef> anchorRefs, typespecRefs;
  DenseSet<uint32_t> lexicallyAnchoredObjectIndices;
  if (includeStatements) {
    for (sim::SimVPIObjectAnchorOp anchor : anchors) {
      sim::VPIObjectBackingAttr backing = anchor.getBackingAttr();
      if (!backing || backing.getKind() == sim::VPIObjectBackingKind::Class) {
        uint32_t index = objectIndices.lookup(anchor);
        anchorRefs[anchor] = {TableKind::Object, index, anchor.getVpiKind()};
        continue;
      }
      if (backing.getKind() == sim::VPIObjectBackingKind::Scope) {
        uint64_t id = backing.getId().getValue().getZExtValue();
        uint32_t index = scopeIndices.lookup(id);
        anchorRefs[anchor] = {TableKind::Scope, index,
                              sim::vpiKindForScope(scopes[index])};
        continue;
      }
      uint64_t id = backing.getId().getValue().getZExtValue();
      uint32_t index = codeUnitObjectIndices.lookup(id);
      anchorRefs[anchor] = {TableKind::Object, index, objects[index].vpiKind};
      objects[index].caps |= OBELISK_RT_DESIGN_CAP_LEXICAL_ANCHOR;
      lexicallyAnchoredObjectIndices.insert(index);
    }
    for (sim::SimVPITypespecDeclOp typespec : typespecs) {
      uint32_t index = objectIndices.lookup(typespec);
      typespecRefs[typespec] = {TableKind::Object, index,
                                objects[index].vpiKind};
    }
  }

  DenseMap<uint64_t, ImageObjectRef> storageRefs, netRefs, netIdentityRefs;
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
      }
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

  if (includeStatements) {
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
      auto parent = anchorsBySymbol.find(anchor.getParentAttr().getValue());
      if (parent == anchorsBySymbol.end()) {
        anchor.emitOpError(
            "lexical parent was not preserved for serialization");
        return {};
      }
      ImageObjectRef source = anchorRefs.lookup(parent->second);
      ImageObjectRef target = anchorRefs.lookup(anchor);
      uint32_t lexicalSelector = target.vpiKind;
      const VPIIndexedAccessDescriptor *indexed =
          findVPIIndexedAccess(source.vpiKind);
      if (indexed &&
          indexed->accessKind == VPIIndexedAccessKind::RelationElement &&
          indexedVPIResultAllowed(*indexed, target.vpiKind))
        lexicalSelector = indexed->relationSelector;
      const VPITraversalDescriptor *lexicalEdge = findVPITraversal(
          source.vpiKind, lexicalSelector, VPITraversalMode::Iterate);
      uint32_t lexicalOrdinal =
          lexicalOrdinals[{parent->second, lexicalSelector}]++;
      FailureOr<std::optional<uint32_t>> indexedOrdinal =
          arrayElementOrdinal(parent->second, anchor);
      if (failed(indexedOrdinal))
        return {};
      if (*indexedOrdinal)
        lexicalOrdinal = **indexedOrdinal;
      if (lexicalEdge &&
          vpiObjectSetContains(lexicalEdge->targets, target.vpiKind) &&
          failed(addRelation(parent->second, source.table, source.index,
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
        sim::SimVPIObjectAnchorOp ancestor = parent->second;
        while (ancestor) {
          ImageObjectRef ancestorRef = anchorRefs.lookup(ancestor);
          if (vpiObjectSetContains(edge->targets, ancestorRef.vpiKind)) {
            if (failed(addRelation(anchor, target.table, target.index,
                                   target.vpiKind, edge->selector,
                                   VPITraversalMode::Handle, ancestorRef.table,
                                   ancestorRef.index, ancestorRef.vpiKind)))
              return {};
            break;
          }
          if (!ancestor.getParentAttr())
            break;
          auto next = anchorsBySymbol.find(ancestor.getParentAttr().getValue());
          if (next == anchorsBySymbol.end()) {
            ancestor.emitOpError(
                "lexical ancestor was not preserved for serialization");
            return {};
          }
          ancestor = next->second;
        }
      }
    }

    llvm::sort(typespecs, [](sim::SimVPITypespecDeclOp left,
                             sim::SimVPITypespecDeclOp right) {
      return left.getId() < right.getId();
    });
    DenseMap<Operation *, uint32_t> typedefOrdinals, interfaceTypespecOrdinals;
    for (sim::SimVPITypespecDeclOp typespec : typespecs) {
      auto owner = anchorsBySymbol.find(typespec.getOwnerAttr().getValue());
      if (owner == anchorsBySymbol.end()) {
        typespec.emitOpError("VPI typespec owner was not preserved");
        return {};
      }
      ImageObjectRef ownerRef = anchorRefs.lookup(owner->second);
      ImageObjectRef typespecRef = typespecRefs.lookup(typespec);
      if (typespec.getOrigin() == sim::VPITypespecOrigin::Typedef) {
        uint32_t ordinal = typedefOrdinals[owner->second]++;
        if (failed(addRelation(
                owner->second, ownerRef.table, ownerRef.index, ownerRef.vpiKind,
                static_cast<uint32_t>(VPIRelationKind::TypedefRel),
                VPITraversalMode::Iterate, typespecRef.table, typespecRef.index,
                typespecRef.vpiKind, ordinal)))
          return {};
      } else if (typespec.getOrigin() == sim::VPITypespecOrigin::Interface) {
        if (failed(addRelation(owner->second, ownerRef.table, ownerRef.index,
                               ownerRef.vpiKind,
                               static_cast<uint32_t>(VPIObjectKind::Typespec),
                               VPITraversalMode::Iterate, typespecRef.table,
                               typespecRef.index, typespecRef.vpiKind,
                               interfaceTypespecOrdinals[owner->second]++)))
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

    for (auto [sourceIndex, object] : llvm::enumerate(objects)) {
      sim::VPITypeSemanticsAttr semantic;
      if (auto storage =
              dyn_cast_if_present<sim::SimStorageDeclOp>(object.identity))
        semantic = storage.getVpiTypeAttr();
      else if (auto net =
                   dyn_cast_if_present<sim::SimNetDeclOp>(object.identity))
        semantic = net.getVpiTypeAttr();
      else if (auto identity =
                   dyn_cast_if_present<sim::SimVPINetIdentityDeclOp>(
                       object.identity))
        semantic = identity.getVpiTypeAttr();
      else if (auto port =
                   dyn_cast_if_present<sim::SimPortDeclOp>(object.identity))
        semantic = port.getVpiTypeAttr();
      if (!semantic)
        continue;
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
          object.identity->emitError("VPI typespec target was not preserved");
          return {};
        }
        targetTypespec = target->second;
      } else if (semantic.getKind() == sim::VPITypeKind::Enum) {
        auto identity = object.identity->getAttrOfType<IntegerAttr>(
            sim::metadata::vpiSourceTypeIdentity);
        if (!identity) {
          object.identity->emitError(
              "anonymous enum VPI value has no source type identity");
          return {};
        }
        targetTypespec = anonymousTypespecsByIdentity.lookup(
            identity.getValue().getZExtValue());
        if (!targetTypespec) {
          object.identity->emitError(
              "anonymous enum VPI value has no matching typespec identity");
          return {};
        }
      }
      if (!targetTypespec)
        continue;
      ImageObjectRef targetRef = typespecRefs.lookup(targetTypespec);
      if (failed(addRelation(object.identity, TableKind::Object,
                             static_cast<uint32_t>(sourceIndex), object.vpiKind,
                             static_cast<uint32_t>(VPIObjectKind::Typespec),
                             VPITraversalMode::Handle, targetRef.table,
                             targetRef.index, targetRef.vpiKind)))
        return {};
    }
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
  for (sim::SimVPIRelationDeclOp relation : generalRelationDeclarations) {
    auto resolveReference =
        [&](sim::VPIObjectRefAttr reference,
            StringRef endpoint) -> std::optional<ImageObjectRef> {
      uint64_t id = reference.getId().getValue().getZExtValue();
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
      }
      if (!resolved)
        relation.emitOpError() << endpoint << " VPI reference "
                               << stringifyVPIObjectRefKind(reference.getKind())
                               << " ID " << id << " was not serialized";
      return resolved;
    };
    std::optional<ImageObjectRef> source =
        resolveReference(relation.getSource(), "source");
    std::optional<ImageObjectRef> target =
        resolveReference(relation.getTarget(), "target");
    if (!source || !target)
      return {};
    auto mode = static_cast<VPITraversalMode>(
        static_cast<uint32_t>(relation.getMode()));
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

  llvm::StringMap<uint32_t> semanticIdentityObjects;
  for (sim::SimVPIObjectAnchorOp anchor : anchors) {
    sim::VPIObjectBackingAttr backing = anchor.getBackingAttr();
    if (!backing || backing.getKind() != sim::VPIObjectBackingKind::Class ||
        !backing.getSymbol())
      continue;
    semanticIdentityObjects[backing.getSymbol().getValue()] =
        objectIndices.lookup(anchor);
  }
  for (sim::SimVPITypespecDeclOp typespec : typespecs) {
    sim::VPITypeSemanticsAttr semantic = typespec.getTargetType();
    if (semantic.getKind() != sim::VPITypeKind::VirtualInterface ||
        !semantic.getSymbol() ||
        typespec.getOrigin() != sim::VPITypespecOrigin::Interface)
      continue;
    StringRef identity = semantic.getSymbol().getRootReference().getValue();
    if (!semanticIdentityObjects
             .try_emplace(identity, objectIndices.lookup(typespec))
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
  SmallVector<SemanticTypeRecord> semanticTypes;
  SmallVector<SemanticTypeEdgeRecord> semanticTypeEdges;
  SmallVector<uint32_t> objectSemanticRoots;
  DenseMap<Attribute, uint32_t> semanticTypeIndices;
  bool semanticTypeError = false;
  auto semanticArrayShape = [&](sim::SimVPIObjectAnchorOp anchor) {
    DenseI64ArrayAttr ranges = anchor.getIndexRangesAttr();
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
      current = make(sim::VPITypeKind::UnpackedArray,
                     values.slice(offset - 2, 2), {child});
    }
    return current;
  };
  auto semanticForObject =
      [&](const Record &object) -> sim::VPITypeSemanticsAttr {
    if (!object.identity)
      return {};
    if (auto storage = dyn_cast<sim::SimStorageDeclOp>(object.identity))
      return storage.getVpiTypeAttr();
    if (auto net = dyn_cast<sim::SimNetDeclOp>(object.identity))
      return net.getVpiTypeAttr();
    if (auto identity = dyn_cast<sim::SimVPINetIdentityDeclOp>(object.identity))
      return identity.getVpiTypeAttr();
    if (auto port = dyn_cast<sim::SimPortDeclOp>(object.identity))
      return port.getVpiTypeAttr();
    if (auto anchor = dyn_cast<sim::SimVPIObjectAnchorOp>(object.identity))
      return semanticArrayShape(anchor);
    if (auto typespec = dyn_cast<sim::SimVPITypespecDeclOp>(object.identity)) {
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
    return {};
  };
  std::function<std::optional<uint32_t>(sim::VPITypeSemanticsAttr)>
      addSemanticType =
          [&](sim::VPITypeSemanticsAttr semantic) -> std::optional<uint32_t> {
    if (!semantic)
      return std::nullopt;
    if (auto found = semanticTypeIndices.find(semantic);
        found != semanticTypeIndices.end())
      return found->second;
    if (semanticTypes.size() == UINT32_MAX ||
        semanticTypeEdges.size() > UINT32_MAX - semantic.getChildren().size()) {
      semanticTypeError = true;
      return std::nullopt;
    }
    uint32_t index = static_cast<uint32_t>(semanticTypes.size());
    semanticTypeIndices[semantic] = index;
    semanticTypes.push_back({semantic});
    const uint32_t firstEdge = static_cast<uint32_t>(semanticTypeEdges.size());
    const uint32_t edgeCount =
        static_cast<uint32_t>(semantic.getChildren().size());
    semanticTypes[index].firstEdge = firstEdge;
    semanticTypes[index].edgeCount = edgeCount;
    semanticTypeEdges.resize(semanticTypeEdges.size() + edgeCount);

    if (ArrayAttr aliases = semantic.getTypedefAliases();
        aliases && !aliases.empty()) {
      auto alias = cast<SymbolRefAttr>(aliases[0]);
      auto target = typespecsBySymbol.find(alias.getRootReference().getValue());
      if (target == typespecsBySymbol.end()) {
        semanticTypeError = true;
        return std::nullopt;
      }
      semanticTypes[index].aliasObject = objectIndices.lookup(target->second);
    }
    if (SymbolRefAttr identity = semantic.getSymbol()) {
      auto target =
          semanticIdentityObjects.find(identity.getRootReference().getValue());
      if (target != semanticIdentityObjects.end()) {
        semanticTypes[index].identityTarget = target->second;
      } else if (semantic.getKind() == sim::VPITypeKind::Class ||
                 semantic.getKind() == sim::VPITypeKind::VirtualInterface) {
        semanticTypeError = true;
        return std::nullopt;
      }
    }

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
    objectSemanticRoots.assign(objects.size(), UINT32_MAX);
    for (auto [index, object] : llvm::enumerate(objects)) {
      sim::VPITypeSemanticsAttr semantic = semanticForObject(object);
      if (!semantic)
        continue;
      auto rootIndex = addSemanticType(semantic);
      if (!rootIndex) {
        if (object.identity)
          object.identity->emitError(
              "could not serialize exact VPI semantic type graph");
        return {};
      }
      objectSemanticRoots[index] = *rootIndex;
    }
    if (semanticTypeError)
      return {};
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
        descriptor->realization != VPIPropertyRealization::FixedImage ||
        selector > UINT16_MAX)
      return operation->emitError(
          "immutable property is not an encodable FixedImage property for "
          "its exact VPI kind");
    FixedPropertyRecord record;
    record.sourceIndexAndTable = packedSource;
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
    // False is the canonical sparse representation of every immutable Boolean
    // property, but it participates in duplicate/conflict detection before
    // being erased.
    fixedProperties.erase(
        std::remove_if(fixedProperties.begin(), fixedProperties.end(),
                       [](const FixedPropertyRecord &property) {
                         return property.kindAndFlags ==
                                    static_cast<uint16_t>(
                                        VPIPropertyValueKind::Boolean) &&
                                property.payload == 0;
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
          netObjects[net.getId()] =
              {net, static_cast<uint32_t>(objectIndex)};
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
            return std::pair{record.sourceIndexAndTable, record.selector} <
                   key;
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
      resolvedNetRuns.push_back(
          {objectIndex, *netType, firstBit, bitCount});
    };
    ArrayRef<analysis::NetBit> connected = connectivity.getConnectedBits();
    for (const auto &[netID, entry] : netObjects) {
      sim::SimNetDeclOp net = entry.first;
      uint32_t objectIndex = entry.second;
      std::optional<uint64_t> width = connectivity.getNetWidth(netID);
      if (!width)
        continue;
      std::optional<uint32_t> ownType = declaredNetType(netID);
      auto bit = std::lower_bound(
          connected.begin(), connected.end(), analysis::NetBit{netID, 0});
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
            std::optional<uint32_t> candidate =
                declaredNetType(dominator.net);
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
    llvm::sort(resolvedNetRuns,
               [](const ResolvedNetRunRecord &left,
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
  for (const FixedPropertyRecord &property : fixedProperties)
    if (property.kindAndFlags ==
        static_cast<uint16_t>(VPIPropertyValueKind::String))
      intern(property.stringPayload);
  if (!semanticTypes.empty() && strings.size() > UINT32_MAX) {
    design.emitOpError(
        "semantic reflection string table exceeds 32-bit offsets");
    return {};
  }

  SmallVector<uint8_t> output(HeaderLayout.size, 0);
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
  uint64_t objectSemanticRootOffset =
      semanticTypeEdgeOffset +
      semanticTypeEdges.size() * SemanticTypeEdgeLayout.size;
  uint64_t relationIndexOffset =
      objectSemanticRootOffset +
      objectSemanticRoots.size() * ObjectSemanticRootLayout.size;
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
  uint64_t stringOffset =
      netDelayRunOffset + netDelayRuns.size() * NetDelayRunLayout.size;
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
    writer.setObjectSemanticRootOffset(objectSemanticRootOffset);
    writer.setObjectSemanticRootCount(objectSemanticRoots.size());
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
    writer.setAliasObject(entry.aliasObject);
    writer.setIdentityTarget(entry.identityTarget);
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
  for (auto [index, semanticRoot] : llvm::enumerate(objectSemanticRoots)) {
    ObjectSemanticRootWriter writer(output.data() + objectSemanticRootOffset +
                                    index * ObjectSemanticRootLayout.size);
    writer.setSemanticType(semanticRoot);
  }
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
