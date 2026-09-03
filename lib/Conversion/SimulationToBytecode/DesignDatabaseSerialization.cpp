//===- DesignDatabaseSerialization.cpp - Reflection image emitter -------===//
//
// Serialize the pointer-free runtime reflection database independently from
// executable bytecode instruction selection.
//
//===----------------------------------------------------------------------===//

#include "BytecodeSerialization.h"

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

uint32_t vpiKindForCodeUnit(sim::SimCodeUnitDeclOp codeUnit) {
  if (codeUnit.getInternalAttr() ||
      !sim::isVPIVisibleEntryKind(codeUnit.getCodeUnitKind()))
    return 0;
  using VPIKind = VPIObjectKind;
  switch (codeUnit.getCodeUnitKind()) {
  case sim::EntryKind::Initial:
    return static_cast<uint16_t>(VPIKind::Initial);
  case sim::EntryKind::Final:
    return static_cast<uint16_t>(VPIKind::Final);
  case sim::EntryKind::Always:
  case sim::EntryKind::AlwaysComb:
  case sim::EntryKind::AlwaysFF:
  case sim::EntryKind::AlwaysLatch:
    return static_cast<uint16_t>(VPIKind::Always);
  case sim::EntryKind::Function:
    return static_cast<uint16_t>(VPIKind::Function);
  case sim::EntryKind::Task:
    return static_cast<uint16_t>(VPIKind::Task);
  default:
    return 0;
  }
}

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
    uint32_t targetIndex;
    uint32_t ordinal;
    uint16_t selector;
    uint16_t sourceKindAndTable;
  };
  SmallVector<sim::SimScopeDeclOp> scopes;
  SmallVector<Record> objects;
  SmallVector<StatementRecord> statements;
  SmallVector<StatementSiteRecord> statementSites;
  SmallVector<sim::SimVPIStatementRelationDeclOp> relationDeclarations;
  SmallVector<RelationRecord> relations;
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
    if (isa<IntegerType, sim::LogicType>(type) || type.isF64())
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
  llvm::DenseMap<uint64_t, PortSource> storageSources, netSources;
  llvm::DenseMap<uint64_t, sim::SimPortDeclOp> directStoragePorts,
      directNetPorts;
  for (Operation &operation : design.getBody().front()) {
    if (auto storage = dyn_cast<sim::SimStorageDeclOp>(operation)) {
      if (auto name = storage.getHierarchicalName())
        storageSources[storage.getId()] =
            PortSource{*name, storage.getScopeId(), storage.getType()};
    } else if (auto net = dyn_cast<sim::SimNetDeclOp>(operation)) {
      if (auto name = net.getHierarchicalName())
        netSources[net.getId()] =
            PortSource{*name, net.getScopeId(), net.getType()};
    }
  }
  for (sim::SimPortDeclOp port :
       design.getBody().front().getOps<sim::SimPortDeclOp>()) {
    const auto &sources = port.getSourceIsNet() ? netSources : storageSources;
    auto source = sources.find(port.getSourceId());
    if (port.getSourceLow() != 0 || source == sources.end() ||
        source->second.name != port.getHierarchicalName() ||
        source->second.scope != port.getScopeId() ||
        source->second.type != port.getType())
      continue;
    auto &directPorts =
        port.getSourceIsNet() ? directNetPorts : directStoragePorts;
    directPorts.try_emplace(port.getSourceId(), port);
  }

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
    } else if (auto storage = dyn_cast<sim::SimStorageDeclOp>(operation)) {
      if (!isReflectableType(storage.getType()))
        continue;
      uint32_t caps = profile & kDatabaseProfileWrite ? 3u : 1u;
      if (sim::SimPortDeclOp port = directStoragePorts.lookup(storage.getId()))
        caps = addPortMetadata(port, caps);
      objects.push_back({2, static_cast<uint16_t>(VPIObjectKind::Reg), caps,
                         storage.getId(), storage.getScopeId(),
                         storage.getHierarchicalName()
                             .value_or(storage.getDebugName().value_or(
                                 fallbackName("storage", storage.getId())))
                             .str(),
                         storage.getType(),
                         storageOffsets.lookup(storage.getId()),
                         sourceFor(storage)});
    } else if (auto net = dyn_cast<sim::SimNetDeclOp>(operation)) {
      if (!isReflectableType(net.getType()))
        continue;
      uint32_t caps = profile & kDatabaseProfileWrite ? 3u : 1u;
      if (sim::SimPortDeclOp port = directNetPorts.lookup(net.getId()))
        caps = addPortMetadata(port, caps);
      objects.push_back({3, static_cast<uint16_t>(VPIObjectKind::Net), caps,
                         net.getId(), net.getScopeId(),
                         net.getHierarchicalName()
                             .value_or(net.getDebugName().value_or(
                                 fallbackName("net", net.getId())))
                             .str(),
                         net.getType(), netOffsets.lookup(net.getId()),
                         sourceFor(net)});
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
                         sourceFor(driver)});
    } else if (auto port = dyn_cast<sim::SimPortDeclOp>(operation)) {
      if (!isReflectableType(port.getType()))
        continue;
      sim::SimPortDeclOp direct =
          (port.getSourceIsNet() ? directNetPorts : directStoragePorts)
              .lookup(port.getSourceId());
      if (direct == port)
        continue;
      uint32_t caps = OBELISK_RT_DESIGN_CAP_READ;
      caps = addPortMetadata(port, caps);
      uint64_t sourceOffset = port.getSourceIsNet()
                                  ? netOffsets.lookup(port.getSourceId())
                                  : storageOffsets.lookup(port.getSourceId());
      objects.push_back({OBELISK_RT_DESIGN_RECORD_PORT,
                         static_cast<uint16_t>(VPIObjectKind::Port), caps,
                         port.getId(), port.getScopeId(),
                         port.getHierarchicalName().str(), port.getType(),
                         sourceOffset + port.getSourceLow(), sourceFor(port)});
    } else if (auto codeUnit = dyn_cast<sim::SimCodeUnitDeclOp>(operation))
      objects.push_back(
          {(codeUnit.getCodeUnitKind() == sim::EntryKind::Function ||
            codeUnit.getCodeUnitKind() == sim::EntryKind::Observer)
               ? 7u
               : 5u,
           vpiKindForCodeUnit(codeUnit),
           (codeUnit.getInternalAttr() ||
            !sim::isVPIVisibleEntryKind(codeUnit.getCodeUnitKind()))
               ? static_cast<uint32_t>(OBELISK_RT_DESIGN_CAP_INTERNAL)
               : 0,
           codeUnit.getId(), codeUnit.getScopeId(),
           codeUnit.getHierarchicalName().str(), Type{}, 0,
           sourceFor(codeUnit)});
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
  llvm::sort(statements, [](StatementRecord left, StatementRecord right) {
    return left.declaration.getId() < right.declaration.getId();
  });
  llvm::sort(statementSites,
             [](StatementSiteRecord left, StatementSiteRecord right) {
               return left.declaration.getId() < right.declaration.getId();
             });
  if (scopes.size() > UINT32_MAX || objects.size() > UINT32_MAX ||
      statements.size() > UINT32_MAX || statementSites.size() > UINT32_MAX ||
      relationDeclarations.size() > UINT32_MAX) {
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
  uint64_t stringOffset =
      relationOffset + relationDeclarations.size() * RelationLayout.size;
  uint64_t indexOffset = 0;
  output.resize(stringOffset, 0);

  DenseMap<uint64_t, SmallVector<uint64_t>> children;
  for (auto scope : scopes)
    if (auto parent = scope.getParent())
      children[*parent].push_back(scopeOffsets.lookup(scope.getId()));
  for (auto [index, object] : llvm::enumerate(objects))
    children[object.scope].push_back(objectOffset + index * ObjectLayout.size);

  DenseMap<uint64_t, uint32_t> scopeIndices, codeUnitObjectIndices,
      statementIndices;
  for (auto [index, scope] : llvm::enumerate(scopes))
    scopeIndices[scope.getId()] = static_cast<uint32_t>(index);
  for (auto [index, object] : llvm::enumerate(objects))
    if (object.kind == OBELISK_RT_DESIGN_RECORD_PROCESS ||
        object.kind == OBELISK_RT_DESIGN_RECORD_FUNCTION)
      codeUnitObjectIndices[object.id] = static_cast<uint32_t>(index);
  for (auto [index, statement] : llvm::enumerate(statements))
    statementIndices[statement.declaration.getId()] =
        static_cast<uint32_t>(index);

  relations.reserve(relationDeclarations.size());
  for (sim::SimVPIStatementRelationDeclOp relation : relationDeclarations) {
    TableKind sourceTable = TableKind::Scope;
    uint32_t sourceIndex = 0;
    switch (relation.getSourceKind()) {
    case sim::VPIStatementSourceKind::Scope: {
      sourceTable = TableKind::Scope;
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
    }
    auto target = statementIndices.find(relation.getTargetStatementId());
    if (target == statementIndices.end()) {
      relation.emitOpError("relation target statement was not serialized");
      return {};
    }
    uint16_t sourceKindAndTable = 0;
    if (!tryPackTableKindPayload(sourceTable, relation.getSourceVpiKind(),
                                 sourceKindAndTable)) {
      relation.emitOpError("relation source VPI kind cannot be packed");
      return {};
    }
    relations.push_back({sourceTable, sourceIndex, target->second,
                         static_cast<uint32_t>(relation.getOrdinal()),
                         static_cast<uint16_t>(relation.getSelector()),
                         sourceKindAndTable});
  }
  llvm::sort(
      relations, [](const RelationRecord &left, const RelationRecord &right) {
        return std::tie(left.sourceTable, left.sourceIndex, left.selector,
                        left.ordinal, left.targetIndex) <
               std::tie(right.sourceTable, right.sourceIndex, right.selector,
                        right.ordinal, right.targetIndex);
      });

  for (auto scope : scopes) {
    uint64_t self = scopeOffsets.lookup(scope.getId());
    ScopeWriter writer(output.data() + self);
    uint32_t packedKind = 0;
    uint32_t vpiKind = scope.getVpiKind().value_or(
        scope.getInterfaceType()
            ? static_cast<uint16_t>(VPIObjectKind::Interface)
            : (scope.getId() == 0
                   ? 0
                   : static_cast<uint16_t>(VPIObjectKind::Module)));
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
    uint64_t sibling = 0;
    if (scope.getParent()) {
      ArrayRef<uint64_t> peers = children[*scope.getParent()];
      auto found = llvm::find(peers, self);
      if (found != peers.end() && std::next(found) != peers.end())
        sibling = *std::next(found);
    }
    writer.setNextSibling(sibling);
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
    ArrayRef<uint64_t> peers = children[object.scope];
    auto found = llvm::find(peers, self);
    writer.setNextSibling(found != peers.end() &&
                                  std::next(found) != peers.end()
                              ? *std::next(found)
                              : 0);
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
    writer.setTargetIndex(relation.targetIndex);
    writer.setOrdinal(relation.ordinal);
    writer.setSelector(relation.selector);
    writer.setSourceKindAndTable(relation.sourceKindAndTable);
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
  writer.setReserved(0);
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
