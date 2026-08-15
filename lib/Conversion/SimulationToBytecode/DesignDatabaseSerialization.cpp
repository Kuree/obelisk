//===- DesignDatabaseSerialization.cpp - Reflection image emitter -------===//
//
// Serialize the pointer-free runtime reflection database independently from
// executable bytecode instruction selection.
//
//===----------------------------------------------------------------------===//

#include "BytecodeSerialization.h"

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

} // namespace

SmallVector<uint8_t> serializeDesignDatabase(
    sim::SimDesignOp design, uint32_t profile,
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
    uint32_t caps;
    uint64_t id;
    uint64_t scope;
    std::string name;
    Type type;
    uint64_t stateOffset;
    Source source;
  };
  SmallVector<sim::SimScopeDeclOp> scopes;
  SmallVector<Record> objects;
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
    const auto &sources =
        port.getSourceIsNet() ? netSources : storageSources;
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
    else if (auto storage = dyn_cast<sim::SimStorageDeclOp>(operation)) {
      if (!isReflectableType(storage.getType()))
        continue;
      uint32_t caps = profile & kDatabaseProfileWrite ? 3u : 1u;
      if (sim::SimPortDeclOp port = directStoragePorts.lookup(storage.getId()))
        caps = addPortMetadata(port, caps);
      objects.push_back({2, caps,
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
      objects.push_back({3, caps,
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
      objects.push_back({4, profile & kDatabaseProfileWrite ? 3u : 1u,
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
      uint64_t sourceOffset =
          port.getSourceIsNet() ? netOffsets.lookup(port.getSourceId())
                                : storageOffsets.lookup(port.getSourceId());
      objects.push_back({OBELISK_RT_DESIGN_RECORD_PORT, caps, port.getId(),
                         port.getScopeId(), port.getHierarchicalName().str(),
                         port.getType(), sourceOffset + port.getSourceLow(),
                         sourceFor(port)});
    } else if (auto codeUnit = dyn_cast<sim::SimCodeUnitDeclOp>(operation))
      objects.push_back(
          {(codeUnit.getCodeUnitKind() == sim::EntryKind::Function ||
            codeUnit.getCodeUnitKind() == sim::EntryKind::Observer)
               ? 7u
               : 5u,
           0, codeUnit.getId(), codeUnit.getScopeId(),
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
  for (const TypeRecord &type : types)
    intern(type.name);

  SmallVector<uint8_t> output(128, 0);
  uint64_t scopeOffset = output.size();
  DenseMap<uint64_t, uint64_t> scopeOffsets;
  for (auto [index, scope] : llvm::enumerate(scopes))
    scopeOffsets[scope.getId()] = scopeOffset + index * 64;
  uint64_t objectOffset = scopeOffset + scopes.size() * 64;
  uint64_t typeOffset = objectOffset + objects.size() * 96;
  uint64_t stringOffset = typeOffset + types.size() * 80;
  uint64_t indexOffset = 0;

  DenseMap<uint64_t, SmallVector<uint64_t>> children;
  for (auto scope : scopes)
    if (auto parent = scope.getParent())
      children[*parent].push_back(scopeOffsets.lookup(scope.getId()));
  for (auto [index, object] : llvm::enumerate(objects))
    children[object.scope].push_back(objectOffset + index * 96);

  for (auto scope : scopes) {
    uint64_t self = scopeOffsets.lookup(scope.getId());
    append32(output, 1);
    append32(output, 4);
    append64(output, scope.getId());
    append64(output,
             scope.getParent() ? scopeOffsets.lookup(*scope.getParent()) : 0);
    append64(output,
             children[scope.getId()].empty() ? 0 : children[scope.getId()][0]);
    uint64_t sibling = 0;
    if (scope.getParent()) {
      ArrayRef<uint64_t> peers = children[*scope.getParent()];
      auto found = llvm::find(peers, self);
      if (found != peers.end() && std::next(found) != peers.end())
        sibling = *std::next(found);
    }
    append64(output, sibling);
    std::string generatedName = fallbackName("scope", scope.getId());
    StringRef name = scope.getHierarchicalName().value_or(
        scope.getDebugName().value_or(generatedName));
    append64(output, stringOffset + intern(name));
    Source source = sourceFor(scope);
    append64(output,
             source.file.empty() ? 0 : stringOffset + intern(source.file));
    append64(output, source.lineColumn);
  }
  for (auto [index, object] : llvm::enumerate(objects)) {
    append32(output, object.kind);
    append32(output, object.caps);
    append64(output, object.id);
    append64(output, scopeOffsets.lookup(object.scope));
    ArrayRef<uint64_t> peers = children[object.scope];
    uint64_t self = objectOffset + index * 96;
    auto found = llvm::find(peers, self);
    append64(output, found != peers.end() && std::next(found) != peers.end()
                         ? *std::next(found)
                         : 0);
    append64(output, object.source.file.empty()
                         ? 0
                         : stringOffset + intern(object.source.file));
    append64(output, stringOffset + intern(object.name));
    uint64_t width = 0;
    if (object.type) {
      uint32_t typeIndex = typeIndices.lookup(object.type);
      width = *simulationWidth(object.type);
      append64(output, typeOffset + uint64_t{typeIndex} * 80);
    } else {
      append64(output, 0);
    }
    append64(output, width);
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
    append64(output, static_cast<uint64_t>(left));
    append64(output, static_cast<uint64_t>(right));
    append64(output, object.stateOffset);
    append64(output, object.source.lineColumn);
  }
  for (const TypeRecord &entry : types) {
    append32(output, 6);
    append32(output, entry.kind | (entry.flags << 8));
    append64(output, entry.width);
    append64(output, static_cast<uint64_t>(entry.left));
    append64(output, static_cast<uint64_t>(entry.right));
    append64(output, entry.element == UINT32_MAX
                         ? 0
                         : typeOffset + uint64_t{entry.element} * 80);
    append64(output, entry.firstChild == UINT32_MAX
                         ? 0
                         : typeOffset + uint64_t{entry.firstChild} * 80);
    append64(output, entry.childCount);
    append64(output, entry.ordinal);
    append64(output, entry.packedOffset);
    append64(output, stringOffset + intern(entry.name));
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
                     objectOffset + index * 96, object.name});
  llvm::sort(names, [](const Index &left, const Index &right) {
    return std::tie(left.hash, left.text) < std::tie(right.hash, right.text);
  });
  for (size_t index = 1; index < names.size(); ++index)
    if (names[index - 1].text == names[index].text) {
      design.emitOpError() << "duplicate hierarchical reflection name '"
                           << names[index].text << "'";
      return {};
    }
  for (const Index &entry : names) {
    append64(output, entry.hash);
    append64(output, entry.name);
    append64(output, entry.record);
  }
  using Header = obelisk_rt_design_database_header_v1;
  static constexpr char magic[] = OBELISK_RT_DESIGN_DATABASE_MAGIC;
  static_assert(sizeof(magic) == sizeof(Header::magic));
  std::copy(std::begin(magic), std::end(magic),
            output.begin() + offsetof(Header, magic));
  write32(output, offsetof(Header, version), OBELISK_RT_VERSION);
  write32(output, offsetof(Header, reserved), 0);
  write32(output, offsetof(Header, profile), profile);
  write32(output, offsetof(Header, header_size),
          OBELISK_RT_DESIGN_DATABASE_HEADER_SIZE);
  write64(output, offsetof(Header, image_size), output.size());
  write64(output, offsetof(Header, root_offset),
          scopeOffsets.lookup(root.getId()));
  write64(output, offsetof(Header, scope_offset), scopeOffset);
  write64(output, offsetof(Header, scope_count), scopes.size());
  write64(output, offsetof(Header, object_offset), objectOffset);
  write64(output, offsetof(Header, object_count), objects.size());
  write64(output, offsetof(Header, type_offset), typeOffset);
  write64(output, offsetof(Header, type_count), types.size());
  write64(output, offsetof(Header, string_offset), stringOffset);
  write64(output, offsetof(Header, string_size), strings.size());
  write64(output, offsetof(Header, index_offset), indexOffset);
  write64(output, offsetof(Header, index_count), names.size());
  write64(output, offsetof(Header, checksum),
          checksum(output, offsetof(Header, checksum)));
  return output;
}

} // namespace obelisk::bytecode
