//===- DesignDatabase.cpp - Checked DWARF-like design reflection ----------===//

#include "ProcessPacking.h"
#include "ProcessShared.h"
#include "RuntimeInternal.h"
#include "obelisk/Reflection/DesignReflection.h"
#include "obelisk/Reflection/VPIObjectModel.h"
#include "obelisk/Runtime/StableHash.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

using DatabaseHeader = obelisk_rt_design_database_header_v1;

constexpr char kMagic[] = OBELISK_RT_DESIGN_DATABASE_MAGIC;
static_assert(sizeof(kMagic) == sizeof(DatabaseHeader::magic));
constexpr uint64_t kHeaderSize = OBELISK_RT_DESIGN_DATABASE_HEADER_SIZE;
constexpr uint64_t kScopeSize = 64;
constexpr uint64_t kObjectSize = 96;
constexpr uint64_t kTypeSize = 80;
constexpr uint64_t kIndexSize = 24;
constexpr uint64_t kStatementSize = 40;
constexpr uint64_t kStaticObjectSize =
    obelisk::reflection::StaticObjectLayout.size;
constexpr uint64_t kStatementSiteSize = 16;
constexpr uint64_t kRelationSize = obelisk::reflection::RelationLayout.size;
constexpr uint64_t kSemanticTypeSize =
    obelisk::reflection::SemanticTypeLayout.size;
constexpr uint64_t kSemanticTypeEdgeSize =
    obelisk::reflection::SemanticTypeEdgeLayout.size;
constexpr uint64_t kSemanticRootBindingSize =
    obelisk::reflection::SemanticRootBindingLayout.size;
constexpr uint64_t kRelationIndexSize =
    obelisk::reflection::RelationIndexLayout.size;
constexpr uint64_t kRelationIndexDimensionSize =
    obelisk::reflection::RelationIndexDimensionLayout.size;
constexpr uint64_t kRelationIndexKeySize =
    obelisk::reflection::RelationIndexKeyLayout.size;
constexpr uint64_t kRelationIndexMemberSize =
    obelisk::reflection::RelationIndexMemberLayout.size;
constexpr uint64_t kFixedPropertySize =
    obelisk::reflection::FixedPropertyLayout.size;
constexpr uint64_t kFrozenValueSize =
    obelisk::reflection::FrozenValueLayout.size;
constexpr uint64_t kFrozenValueBindingSize =
    obelisk::reflection::FrozenValueBindingLayout.size;
constexpr uint64_t kResolvedNetRunSize =
    obelisk::reflection::ResolvedNetRunLayout.size;
constexpr uint64_t kNetDelayRunSize =
    obelisk::reflection::NetDelayRunLayout.size;
constexpr uint64_t kDefinitionSize = obelisk::reflection::DefinitionLayout.size;
constexpr uint64_t kDefinitionBindingSize =
    obelisk::reflection::DefinitionBindingLayout.size;
constexpr uint64_t kDefinitionMemberSize =
    obelisk::reflection::DefinitionMemberLayout.size;
constexpr uint64_t kDefinitionMemberRelationSize =
    obelisk::reflection::DefinitionMemberRelationLayout.size;
constexpr uint64_t kDefinitionMemberRelationTargetSize =
    obelisk::reflection::DefinitionMemberRelationTargetLayout.size;
constexpr uint64_t kDefinitionSpecializationSize =
    obelisk::reflection::DefinitionSpecializationLayout.size;
constexpr uint64_t kDefinitionSpecializationBindingSize =
    obelisk::reflection::DefinitionSpecializationBindingLayout.size;
constexpr uint64_t kDefinitionMemberEndpointSize =
    obelisk::reflection::DefinitionMemberEndpointLayout.size;
constexpr uint64_t kDefinitionMemberInstanceRelationSize =
    obelisk::reflection::DefinitionMemberInstanceRelationLayout.size;
constexpr uint64_t kDefinitionMemberInstanceRelationTargetSize =
    obelisk::reflection::DefinitionMemberInstanceRelationTargetLayout.size;
constexpr uint64_t kDefinitionMemberInstanceRelationInverseSize =
    obelisk::reflection::DefinitionMemberInstanceRelationInverseLayout.size;

// Physical image offsets occupy the 00 prefix. The other prefixes provide
// allocation-free cursors and relation tokens for per-instance views of
// definition-owned declarations. The upper token payload names the compact
// instance-binding row, so member validation, parent recovery, and type lookup
// are constant time after the owning scope performs one binary search.
constexpr uint64_t kVirtualTagMask = UINT64_C(3) << 62;
constexpr uint64_t kVirtualMemberTag = UINT64_C(2) << 62;
constexpr uint64_t kVirtualForwardRelationTag = UINT64_C(1) << 62;
constexpr uint64_t kVirtualReverseRelationTag = UINT64_C(3) << 62;
constexpr uint64_t kVirtualScopeMask = (UINT64_C(1) << 30) - 1;
constexpr uint32_t kVirtualEndpointRelationFlag = UINT32_C(1) << 31;
constexpr uint32_t kVirtualRefObjectFlag = UINT32_C(1) << 31;
constexpr uint32_t kVirtualRefObjectRelationFlag = UINT32_C(1) << 30;
constexpr uint32_t kVirtualInstanceRelationTargetFlag = UINT32_C(1) << 31;
constexpr uint16_t kVPIIODirectionRef = 6;

uint64_t virtualToken(uint64_t tag, uint32_t scope, uint32_t payload) {
  return tag | (uint64_t{scope} << 32) | payload;
}

bool decodeVirtualToken(uint64_t token, uint64_t tag, uint32_t &scope,
                        uint32_t &payload) {
  if ((token & kVirtualTagMask) != tag)
    return false;
  scope = static_cast<uint32_t>((token >> 32) & kVirtualScopeMask);
  payload = static_cast<uint32_t>(token);
  return true;
}

uint16_t read16(const uint8_t *data) {
  return uint16_t{data[0]} | (uint16_t{data[1]} << 8);
}

uint32_t read32(const uint8_t *data) {
  uint32_t value = 0;
  for (unsigned byte = 0; byte != 4; ++byte)
    value |= uint32_t{data[byte]} << (byte * 8);
  return value;
}

uint32_t recordKind(const uint8_t *record) {
  return static_cast<uint32_t>(
      obelisk::reflection::unpackRecordKind(read32(record)));
}

uint32_t recordVPIKind(const uint8_t *record) {
  return obelisk::reflection::unpackRecordKindPayload(read32(record));
}

bool recordKindSupportsVPI(uint32_t physicalKind, uint32_t vpiKind) {
  if (vpiKind == 0)
    return physicalKind == OBELISK_RT_DESIGN_RECORD_DRIVER;
  const auto *descriptor = obelisk::reflection::findVPIObjectKind(vpiKind);
  if (!descriptor ||
      descriptor->role != obelisk::reflection::VPIObjectRole::Concrete)
    return false;
  auto hasFamily = [&](obelisk::reflection::VPIObjectFamily family) {
    return (descriptor->families &
            obelisk::reflection::vpiFamilyMask(family)) != 0;
  };
  using VPIKind = obelisk::reflection::VPIObjectKind;
  switch (physicalKind) {
  case OBELISK_RT_DESIGN_RECORD_SCOPE:
    return hasFamily(obelisk::reflection::VPIObjectFamily::Scope);
  case OBELISK_RT_DESIGN_RECORD_STORAGE:
    return hasFamily(obelisk::reflection::VPIObjectFamily::Variable);
  case OBELISK_RT_DESIGN_RECORD_NET:
    return hasFamily(obelisk::reflection::VPIObjectFamily::Net);
  case OBELISK_RT_DESIGN_RECORD_DRIVER:
    return false;
  case OBELISK_RT_DESIGN_RECORD_PROCESS:
    return vpiKind == static_cast<uint16_t>(VPIKind::Initial) ||
           vpiKind == static_cast<uint16_t>(VPIKind::Final) ||
           vpiKind == static_cast<uint16_t>(VPIKind::Always) ||
           vpiKind == static_cast<uint16_t>(VPIKind::Task);
  case OBELISK_RT_DESIGN_RECORD_FUNCTION:
    return vpiKind == static_cast<uint16_t>(VPIKind::Function);
  case OBELISK_RT_DESIGN_RECORD_PORT:
    return vpiKind == static_cast<uint16_t>(VPIKind::Port);
  case OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT:
    return obelisk::reflection::vpiObjectSetContains(
        obelisk::reflection::VPIObjectSetID::StaticImageObjects, vpiKind);
  default:
    return false;
  }
}

uint64_t read64(const uint8_t *data) {
  uint64_t value = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    value |= uint64_t{data[byte]} << (byte * 8);
  return value;
}

int64_t readI64(const uint8_t *data) {
  return static_cast<int64_t>(read64(data));
}

bool validRange(uint64_t offset, uint64_t count, uint64_t stride,
                uint64_t size) {
  if (count != 0 && stride > std::numeric_limits<uint64_t>::max() / count)
    return false;
  uint64_t bytes = count * stride;
  return offset <= size && bytes <= size - offset;
}

bool rangesDisjoint(uint64_t leftOffset, uint64_t leftCount,
                    uint64_t leftStride, uint64_t rightOffset,
                    uint64_t rightCount, uint64_t rightStride) {
  if (leftCount == 0 || rightCount == 0)
    return true;
  uint64_t leftEnd = leftOffset + leftCount * leftStride;
  uint64_t rightEnd = rightOffset + rightCount * rightStride;
  return leftEnd <= rightOffset || rightEnd <= leftOffset;
}

uint64_t nameHash(const uint8_t *name, uint64_t size);

uint64_t checksum(const uint8_t *data, uint64_t size) {
  uint64_t hash = OBELISK_STABLE_HASH_OFFSET_BASIS;
  for (uint64_t index = 0; index != size; ++index) {
    // The checksum field is treated as zero while hashing.
    uint8_t byte = index >= 32 && index < 40 ? 0 : data[index];
    hash = obelisk_stable_hash_append_byte(hash, byte);
  }
  return hash;
}

using Database = DesignDatabaseCache;

bool parseHeader(const obelisk_rt_execution_descriptor_v1 *execution,
                 Database &database) {
  if (!execution || execution->version != OBELISK_RT_VERSION ||
      (execution->flags & OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE) == 0 ||
      !execution->design_database ||
      execution->design_database_size < kHeaderSize)
    return false;
  const uint8_t *data = execution->design_database;
  if (std::memcmp(data + offsetof(DatabaseHeader, magic), kMagic,
                  sizeof(kMagic)) != 0 ||
      read32(data + offsetof(DatabaseHeader, version)) != OBELISK_RT_VERSION ||
      read32(data + offsetof(DatabaseHeader, header_size)) != kHeaderSize ||
      read64(data + offsetof(DatabaseHeader, image_size)) !=
          execution->design_database_size ||
      read64(data + offsetof(DatabaseHeader, checksum)) == 0 ||
      read64(data + offsetof(DatabaseHeader, checksum)) !=
          checksum(data, execution->design_database_size))
    return false;
  uint32_t semanticDirectory =
      read32(data + offsetof(DatabaseHeader, reserved));
  uint64_t semanticTypeOffset = 0, semanticTypeCount = 0;
  uint64_t semanticTypeEdgeOffset = 0, semanticTypeEdgeCount = 0;
  uint64_t semanticRootBindingOffset = 0, semanticRootBindingCount = 0;
  uint64_t relationIndexOffset = 0, relationIndexCount = 0;
  uint64_t relationIndexDimensionOffset = 0, relationIndexDimensionCount = 0;
  uint64_t relationIndexKeyOffset = 0, relationIndexKeyCount = 0;
  uint64_t relationIndexMemberOffset = 0, relationIndexMemberCount = 0;
  uint64_t fixedPropertyOffset = 0, fixedPropertyCount = 0;
  uint64_t resolvedNetRunOffset = 0, resolvedNetRunCount = 0;
  uint64_t netDelayRunOffset = 0, netDelayRunCount = 0;
  uint64_t staticObjectOffset = 0, staticObjectCount = 0;
  uint64_t definitionOffset = 0, definitionCount = 0;
  uint64_t definitionBindingOffset = 0, definitionBindingCount = 0;
  uint64_t definitionMemberOffset = 0, definitionMemberCount = 0;
  uint64_t definitionMemberRelationOffset = 0;
  uint64_t definitionMemberRelationCount = 0;
  uint64_t definitionMemberRelationTargetOffset = 0;
  uint64_t definitionMemberRelationTargetCount = 0;
  uint64_t definitionSpecializationOffset = 0;
  uint64_t definitionSpecializationCount = 0;
  uint64_t definitionSpecializationBindingOffset = 0;
  uint64_t definitionSpecializationBindingCount = 0;
  uint64_t definitionMemberEndpointOffset = 0;
  uint64_t definitionMemberEndpointCount = 0;
  uint64_t definitionMemberInstanceRelationOffset = 0;
  uint64_t definitionMemberInstanceRelationCount = 0;
  uint64_t definitionMemberInstanceRelationTargetOffset = 0;
  uint64_t definitionMemberInstanceRelationTargetCount = 0;
  uint64_t definitionMemberInstanceRelationInverseOffset = 0;
  uint64_t definitionMemberInstanceRelationInverseCount = 0;
  uint64_t frozenValueOffset = 0, frozenValueCount = 0;
  uint64_t frozenValueBindingOffset = 0, frozenValueBindingCount = 0;
  uint64_t frozenValuePayloadOffset = 0, frozenValuePayloadSize = 0;
  if (semanticDirectory != 0) {
    if (!validRange(semanticDirectory, 1,
                    obelisk::reflection::SemanticDirectoryLayout.size,
                    execution->design_database_size))
      return false;
    const uint8_t *directory = data + semanticDirectory;
    semanticTypeOffset = read64(directory);
    semanticTypeCount = read64(directory + 8);
    semanticTypeEdgeOffset = read64(directory + 16);
    semanticTypeEdgeCount = read64(directory + 24);
    semanticRootBindingOffset = read64(directory + 32);
    semanticRootBindingCount = read64(directory + 40);
    relationIndexOffset = read64(directory + 48);
    relationIndexCount = read64(directory + 56);
    relationIndexDimensionOffset = read64(directory + 64);
    relationIndexDimensionCount = read64(directory + 72);
    relationIndexKeyOffset = read64(directory + 80);
    relationIndexKeyCount = read64(directory + 88);
    relationIndexMemberOffset = read64(directory + 96);
    relationIndexMemberCount = read64(directory + 104);
    fixedPropertyOffset = read64(directory + 112);
    fixedPropertyCount = read64(directory + 120);
    resolvedNetRunOffset = read64(directory + 128);
    resolvedNetRunCount = read64(directory + 136);
    netDelayRunOffset = read64(directory + 144);
    netDelayRunCount = read64(directory + 152);
    staticObjectOffset = read64(directory + 160);
    staticObjectCount = read64(directory + 168);
    definitionOffset = read64(directory + 176);
    definitionCount = read64(directory + 184);
    definitionBindingOffset = read64(directory + 192);
    definitionBindingCount = read64(directory + 200);
    definitionMemberOffset = read64(directory + 208);
    definitionMemberCount = read64(directory + 216);
    definitionMemberRelationOffset = read64(directory + 224);
    definitionMemberRelationCount = read64(directory + 232);
    definitionMemberRelationTargetOffset = read64(directory + 240);
    definitionMemberRelationTargetCount = read64(directory + 248);
    definitionSpecializationOffset = read64(directory + 256);
    definitionSpecializationCount = read64(directory + 264);
    definitionSpecializationBindingOffset = read64(directory + 272);
    definitionSpecializationBindingCount = read64(directory + 280);
    definitionMemberEndpointOffset = read64(directory + 288);
    definitionMemberEndpointCount = read64(directory + 296);
    definitionMemberInstanceRelationOffset = read64(directory + 304);
    definitionMemberInstanceRelationCount = read64(directory + 312);
    definitionMemberInstanceRelationTargetOffset = read64(directory + 320);
    definitionMemberInstanceRelationTargetCount = read64(directory + 328);
    definitionMemberInstanceRelationInverseOffset = read64(directory + 336);
    definitionMemberInstanceRelationInverseCount = read64(directory + 344);
    frozenValueOffset = read64(directory + 352);
    frozenValueCount = read64(directory + 360);
    frozenValueBindingOffset = read64(directory + 368);
    frozenValueBindingCount = read64(directory + 376);
    frozenValuePayloadOffset = read64(directory + 384);
    frozenValuePayloadSize = read64(directory + 392);
  }
  database = {data,
              execution->design_database_size,
              read32(data + offsetof(DatabaseHeader, profile)),
              read64(data + offsetof(DatabaseHeader, root_offset)),
              read64(data + offsetof(DatabaseHeader, scope_offset)),
              read64(data + offsetof(DatabaseHeader, scope_count)),
              read64(data + offsetof(DatabaseHeader, object_offset)),
              read64(data + offsetof(DatabaseHeader, object_count)),
              read64(data + offsetof(DatabaseHeader, type_offset)),
              read64(data + offsetof(DatabaseHeader, type_count)),
              read64(data + offsetof(DatabaseHeader, string_offset)),
              read64(data + offsetof(DatabaseHeader, string_size)),
              read64(data + offsetof(DatabaseHeader, index_offset)),
              read64(data + offsetof(DatabaseHeader, index_count)),
              read64(data + offsetof(DatabaseHeader, statement_offset)),
              read64(data + offsetof(DatabaseHeader, statement_count)),
              read64(data + offsetof(DatabaseHeader, statement_site_offset)),
              read64(data + offsetof(DatabaseHeader, statement_site_count)),
              read64(data + offsetof(DatabaseHeader, relation_offset)),
              read64(data + offsetof(DatabaseHeader, relation_count)),
              semanticTypeOffset,
              semanticTypeCount,
              semanticTypeEdgeOffset,
              semanticTypeEdgeCount,
              semanticRootBindingOffset,
              semanticRootBindingCount,
              relationIndexOffset,
              relationIndexCount,
              relationIndexDimensionOffset,
              relationIndexDimensionCount,
              relationIndexKeyOffset,
              relationIndexKeyCount,
              relationIndexMemberOffset,
              relationIndexMemberCount,
              fixedPropertyOffset,
              fixedPropertyCount,
              resolvedNetRunOffset,
              resolvedNetRunCount,
              netDelayRunOffset,
              netDelayRunCount,
              staticObjectOffset,
              staticObjectCount,
              definitionOffset,
              definitionCount,
              definitionBindingOffset,
              definitionBindingCount,
              definitionMemberOffset,
              definitionMemberCount,
              definitionMemberRelationOffset,
              definitionMemberRelationCount,
              definitionMemberRelationTargetOffset,
              definitionMemberRelationTargetCount,
              definitionSpecializationOffset,
              definitionSpecializationCount,
              definitionSpecializationBindingOffset,
              definitionSpecializationBindingCount,
              definitionMemberEndpointOffset,
              definitionMemberEndpointCount,
              definitionMemberInstanceRelationOffset,
              definitionMemberInstanceRelationCount,
              definitionMemberInstanceRelationTargetOffset,
              definitionMemberInstanceRelationTargetCount,
              definitionMemberInstanceRelationInverseOffset,
              definitionMemberInstanceRelationInverseCount,
              frozenValueOffset,
              frozenValueCount,
              frozenValueBindingOffset,
              frozenValueBindingCount,
              frozenValuePayloadOffset,
              frozenValuePayloadSize,
              execution->state_bit_count};
  if (semanticDirectory != 0) {
    struct Section {
      uint64_t offset;
      uint64_t count;
      uint64_t stride;
    };
    const Section sections[] = {
        {database.scopes, database.scopeCount, kScopeSize},
        {database.objects, database.objectCount, kObjectSize},
        {database.types, database.typeCount, kTypeSize},
        {database.strings, database.stringSize, 1},
        {database.index, database.indexCount, kIndexSize},
        {database.statements, database.statementCount, kStatementSize},
        {database.statementSites, database.statementSiteCount,
         kStatementSiteSize},
        {database.relations, database.relationCount, kRelationSize},
        {database.semanticTypes, database.semanticTypeCount, kSemanticTypeSize},
        {database.semanticTypeEdges, database.semanticTypeEdgeCount,
         kSemanticTypeEdgeSize},
        {database.semanticRootBindings, database.semanticRootBindingCount,
         kSemanticRootBindingSize},
        {database.relationIndices, database.relationIndexCount,
         kRelationIndexSize},
        {database.relationIndexDimensions, database.relationIndexDimensionCount,
         kRelationIndexDimensionSize},
        {database.relationIndexKeys, database.relationIndexKeyCount,
         kRelationIndexKeySize},
        {database.relationIndexMembers, database.relationIndexMemberCount,
         kRelationIndexMemberSize},
        {database.fixedProperties, database.fixedPropertyCount,
         kFixedPropertySize},
        {database.resolvedNetRuns, database.resolvedNetRunCount,
         kResolvedNetRunSize},
        {database.netDelayRuns, database.netDelayRunCount, kNetDelayRunSize},
        {database.staticObjects, database.staticObjectCount, kStaticObjectSize},
        {database.definitions, database.definitionCount, kDefinitionSize},
        {database.definitionBindings, database.definitionBindingCount,
         kDefinitionBindingSize},
        {database.definitionMembers, database.definitionMemberCount,
         kDefinitionMemberSize},
        {database.definitionMemberRelations,
         database.definitionMemberRelationCount, kDefinitionMemberRelationSize},
        {database.definitionMemberRelationTargets,
         database.definitionMemberRelationTargetCount,
         kDefinitionMemberRelationTargetSize},
        {database.definitionSpecializations,
         database.definitionSpecializationCount, kDefinitionSpecializationSize},
        {database.definitionSpecializationBindings,
         database.definitionSpecializationBindingCount,
         kDefinitionSpecializationBindingSize},
        {database.definitionMemberEndpoints,
         database.definitionMemberEndpointCount, kDefinitionMemberEndpointSize},
        {database.definitionMemberInstanceRelations,
         database.definitionMemberInstanceRelationCount,
         kDefinitionMemberInstanceRelationSize},
        {database.definitionMemberInstanceRelationTargets,
         database.definitionMemberInstanceRelationTargetCount,
         kDefinitionMemberInstanceRelationTargetSize},
        {database.definitionMemberInstanceRelationInverses,
         database.definitionMemberInstanceRelationInverseCount,
         kDefinitionMemberInstanceRelationInverseSize},
        {database.frozenValues, database.frozenValueCount, kFrozenValueSize},
        {database.frozenValueBindings, database.frozenValueBindingCount,
         kFrozenValueBindingSize},
        {database.frozenValuePayload, database.frozenValuePayloadSize, 1},
    };
    for (const Section &section : sections)
      if (!rangesDisjoint(semanticDirectory, 1,
                          obelisk::reflection::SemanticDirectoryLayout.size,
                          section.offset, section.count, section.stride))
        return false;
  }
  uint32_t supportedProfile =
      OBELISK_RT_DESIGN_PROFILE_READ | OBELISK_RT_DESIGN_PROFILE_WRITE;
  if ((database.profile & ~supportedProfile) != 0 ||
      (database.profile & OBELISK_RT_DESIGN_PROFILE_READ) == 0 ||
      ((database.profile & OBELISK_RT_DESIGN_PROFILE_WRITE) != 0 &&
       (database.profile & OBELISK_RT_DESIGN_PROFILE_READ) == 0) ||
      ((database.profile & OBELISK_RT_DESIGN_PROFILE_READ) != 0) !=
          ((execution->flags & (OBELISK_RT_EXECUTION_VPI_READ |
                                OBELISK_RT_EXECUTION_WAVEFORM_METADATA)) !=
           0) ||
      ((database.profile & OBELISK_RT_DESIGN_PROFILE_WRITE) != 0) !=
          ((execution->flags & OBELISK_RT_EXECUTION_VPI_WRITE) != 0) ||
      ((execution->flags & OBELISK_RT_EXECUTION_VPI_READ) == 0 &&
       (semanticDirectory != 0 || database.statementCount != 0 ||
        database.statementSiteCount != 0 || database.relationCount != 0 ||
        database.semanticTypeCount != 0 ||
        database.semanticTypeEdgeCount != 0 ||
        database.semanticRootBindingCount != 0 ||
        database.relationIndexCount != 0 ||
        database.relationIndexDimensionCount != 0 ||
        database.relationIndexKeyCount != 0 ||
        database.relationIndexMemberCount != 0 ||
        database.fixedPropertyCount != 0 || database.resolvedNetRunCount != 0 ||
        database.netDelayRunCount != 0 || database.staticObjectCount != 0 ||
        database.definitionCount != 0 || database.definitionBindingCount != 0 ||
        database.definitionMemberCount != 0 ||
        database.definitionMemberRelationCount != 0 ||
        database.definitionMemberRelationTargetCount != 0 ||
        database.definitionSpecializationCount != 0 ||
        database.definitionSpecializationBindingCount != 0 ||
        database.definitionMemberEndpointCount != 0 ||
        database.definitionMemberInstanceRelationCount != 0 ||
        database.definitionMemberInstanceRelationTargetCount != 0 ||
        database.definitionMemberInstanceRelationInverseCount != 0 ||
        database.frozenValueCount != 0 ||
        database.frozenValueBindingCount != 0 ||
        database.frozenValuePayloadSize != 0)) ||
      !validRange(database.scopes, database.scopeCount, kScopeSize,
                  database.size) ||
      !validRange(database.objects, database.objectCount, kObjectSize,
                  database.size) ||
      !validRange(database.types, database.typeCount, kTypeSize,
                  database.size) ||
      !validRange(database.strings, database.stringSize, 1, database.size) ||
      !validRange(database.index, database.indexCount, kIndexSize,
                  database.size) ||
      !validRange(database.statements, database.statementCount, kStatementSize,
                  database.size) ||
      !validRange(database.statementSites, database.statementSiteCount,
                  kStatementSiteSize, database.size) ||
      !validRange(database.relations, database.relationCount, kRelationSize,
                  database.size) ||
      !validRange(database.semanticTypes, database.semanticTypeCount,
                  kSemanticTypeSize, database.size) ||
      !validRange(database.semanticTypeEdges, database.semanticTypeEdgeCount,
                  kSemanticTypeEdgeSize, database.size) ||
      !validRange(database.semanticRootBindings,
                  database.semanticRootBindingCount, kSemanticRootBindingSize,
                  database.size) ||
      !validRange(database.relationIndices, database.relationIndexCount,
                  kRelationIndexSize, database.size) ||
      !validRange(database.relationIndexDimensions,
                  database.relationIndexDimensionCount,
                  kRelationIndexDimensionSize, database.size) ||
      !validRange(database.relationIndexKeys, database.relationIndexKeyCount,
                  kRelationIndexKeySize, database.size) ||
      !validRange(database.relationIndexMembers,
                  database.relationIndexMemberCount, kRelationIndexMemberSize,
                  database.size) ||
      !validRange(database.fixedProperties, database.fixedPropertyCount,
                  kFixedPropertySize, database.size) ||
      !validRange(database.resolvedNetRuns, database.resolvedNetRunCount,
                  kResolvedNetRunSize, database.size) ||
      !validRange(database.netDelayRuns, database.netDelayRunCount,
                  kNetDelayRunSize, database.size) ||
      !validRange(database.staticObjects, database.staticObjectCount,
                  kStaticObjectSize, database.size) ||
      !validRange(database.definitions, database.definitionCount,
                  kDefinitionSize, database.size) ||
      !validRange(database.definitionBindings, database.definitionBindingCount,
                  kDefinitionBindingSize, database.size) ||
      !validRange(database.definitionMembers, database.definitionMemberCount,
                  kDefinitionMemberSize, database.size) ||
      !validRange(database.definitionMemberRelations,
                  database.definitionMemberRelationCount,
                  kDefinitionMemberRelationSize, database.size) ||
      !validRange(database.definitionMemberRelationTargets,
                  database.definitionMemberRelationTargetCount,
                  kDefinitionMemberRelationTargetSize, database.size) ||
      !validRange(database.definitionSpecializations,
                  database.definitionSpecializationCount,
                  kDefinitionSpecializationSize, database.size) ||
      !validRange(database.definitionSpecializationBindings,
                  database.definitionSpecializationBindingCount,
                  kDefinitionSpecializationBindingSize, database.size) ||
      !validRange(database.definitionMemberEndpoints,
                  database.definitionMemberEndpointCount,
                  kDefinitionMemberEndpointSize, database.size) ||
      !validRange(database.definitionMemberInstanceRelations,
                  database.definitionMemberInstanceRelationCount,
                  kDefinitionMemberInstanceRelationSize, database.size) ||
      !validRange(database.definitionMemberInstanceRelationTargets,
                  database.definitionMemberInstanceRelationTargetCount,
                  kDefinitionMemberInstanceRelationTargetSize, database.size) ||
      !validRange(database.definitionMemberInstanceRelationInverses,
                  database.definitionMemberInstanceRelationInverseCount,
                  kDefinitionMemberInstanceRelationInverseSize,
                  database.size) ||
      !validRange(database.frozenValues, database.frozenValueCount,
                  kFrozenValueSize, database.size) ||
      !validRange(database.frozenValueBindings,
                  database.frozenValueBindingCount, kFrozenValueBindingSize,
                  database.size) ||
      !validRange(database.frozenValuePayload, database.frozenValuePayloadSize,
                  1, database.size) ||
      database.scopes < kHeaderSize || database.objects < kHeaderSize ||
      database.types < kHeaderSize || database.strings < kHeaderSize ||
      database.index < kHeaderSize || database.statements < kHeaderSize ||
      database.statementSites < kHeaderSize ||
      database.relations < kHeaderSize ||
      (database.semanticTypeCount != 0 &&
       database.semanticTypes < kHeaderSize) ||
      (database.semanticTypeEdgeCount != 0 &&
       database.semanticTypeEdges < kHeaderSize) ||
      (database.semanticRootBindingCount != 0 &&
       database.semanticRootBindings < kHeaderSize) ||
      (database.relationIndexCount != 0 &&
       database.relationIndices < kHeaderSize) ||
      (database.relationIndexDimensionCount != 0 &&
       database.relationIndexDimensions < kHeaderSize) ||
      (database.relationIndexKeyCount != 0 &&
       database.relationIndexKeys < kHeaderSize) ||
      (database.relationIndexMemberCount != 0 &&
       database.relationIndexMembers < kHeaderSize) ||
      ((database.fixedPropertyCount != 0 || database.fixedProperties != 0) &&
       database.fixedProperties < kHeaderSize) ||
      ((database.resolvedNetRunCount != 0 || database.resolvedNetRuns != 0) &&
       database.resolvedNetRuns < kHeaderSize) ||
      ((database.netDelayRunCount != 0 || database.netDelayRuns != 0) &&
       database.netDelayRuns < kHeaderSize) ||
      ((database.staticObjectCount != 0 || database.staticObjects != 0) &&
       database.staticObjects < kHeaderSize) ||
      ((database.definitionCount != 0 || database.definitions != 0) &&
       database.definitions < kHeaderSize) ||
      ((database.definitionBindingCount != 0 ||
        database.definitionBindings != 0) &&
       database.definitionBindings < kHeaderSize) ||
      ((database.definitionMemberCount != 0 ||
        database.definitionMembers != 0) &&
       database.definitionMembers < kHeaderSize) ||
      ((database.definitionMemberRelationCount != 0 ||
        database.definitionMemberRelations != 0) &&
       database.definitionMemberRelations < kHeaderSize) ||
      ((database.definitionMemberRelationTargetCount != 0 ||
        database.definitionMemberRelationTargets != 0) &&
       database.definitionMemberRelationTargets < kHeaderSize) ||
      ((database.definitionSpecializationCount != 0 ||
        database.definitionSpecializations != 0) &&
       database.definitionSpecializations < kHeaderSize) ||
      ((database.definitionSpecializationBindingCount != 0 ||
        database.definitionSpecializationBindings != 0) &&
       database.definitionSpecializationBindings < kHeaderSize) ||
      ((database.definitionMemberEndpointCount != 0 ||
        database.definitionMemberEndpoints != 0) &&
       database.definitionMemberEndpoints < kHeaderSize) ||
      ((database.definitionMemberInstanceRelationCount != 0 ||
        database.definitionMemberInstanceRelations != 0) &&
       database.definitionMemberInstanceRelations < kHeaderSize) ||
      ((database.definitionMemberInstanceRelationTargetCount != 0 ||
        database.definitionMemberInstanceRelationTargets != 0) &&
       database.definitionMemberInstanceRelationTargets < kHeaderSize) ||
      ((database.definitionMemberInstanceRelationInverseCount != 0 ||
        database.definitionMemberInstanceRelationInverses != 0) &&
       database.definitionMemberInstanceRelationInverses < kHeaderSize) ||
      ((database.frozenValueCount != 0 || database.frozenValues != 0) &&
       database.frozenValues < kHeaderSize) ||
      ((database.frozenValueBindingCount != 0 ||
        database.frozenValueBindings != 0) &&
       database.frozenValueBindings < kHeaderSize) ||
      ((database.frozenValuePayloadSize != 0 ||
        database.frozenValuePayload != 0) &&
       database.frozenValuePayload < kHeaderSize) ||
      (semanticDirectory != 0 && semanticDirectory < kHeaderSize) ||
      database.size >= kVirtualForwardRelationTag || database.stringSize == 0 ||
      database.scopeCount > kVirtualScopeMask ||
      database.objectCount > UINT32_MAX ||
      database.statementCount > UINT32_MAX ||
      database.statementSiteCount > UINT32_MAX ||
      database.relationCount > UINT32_MAX ||
      database.semanticTypeCount > UINT32_MAX ||
      database.semanticTypeEdgeCount > UINT32_MAX ||
      database.semanticRootBindingCount > UINT32_MAX ||
      database.relationIndexCount > UINT32_MAX ||
      database.relationIndexDimensionCount > UINT32_MAX ||
      database.relationIndexKeyCount > UINT32_MAX ||
      database.relationIndexMemberCount > UINT32_MAX ||
      database.fixedPropertyCount > UINT32_MAX ||
      database.resolvedNetRunCount > UINT32_MAX ||
      database.netDelayRunCount > UINT32_MAX ||
      database.staticObjectCount > UINT32_MAX ||
      database.definitionCount > UINT32_MAX ||
      database.definitionBindingCount > uint64_t{kVirtualScopeMask} + 1 ||
      database.definitionMemberCount >= kVirtualRefObjectRelationFlag ||
      database.definitionMemberRelationCount > UINT32_MAX ||
      database.definitionMemberRelationTargetCount >=
          kVirtualInstanceRelationTargetFlag ||
      database.definitionSpecializationCount > UINT32_MAX ||
      database.definitionSpecializationBindingCount > UINT32_MAX ||
      database.definitionMemberEndpointCount > UINT32_MAX ||
      database.definitionMemberEndpointCount >= kVirtualEndpointRelationFlag ||
      database.definitionMemberInstanceRelationCount > UINT32_MAX ||
      database.definitionMemberInstanceRelationTargetCount >=
          kVirtualInstanceRelationTargetFlag ||
      database.definitionMemberInstanceRelationInverseCount > UINT32_MAX ||
      database.frozenValueCount > UINT32_MAX ||
      database.frozenValueBindingCount > UINT32_MAX ||
      database.indexCount > database.scopeCount + database.objectCount +
                                database.staticObjectCount ||
      !rangesDisjoint(database.scopes, database.scopeCount, kScopeSize,
                      database.objects, database.objectCount, kObjectSize) ||
      !rangesDisjoint(database.scopes, database.scopeCount, kScopeSize,
                      database.types, database.typeCount, kTypeSize) ||
      !rangesDisjoint(database.scopes, database.scopeCount, kScopeSize,
                      database.strings, database.stringSize, 1) ||
      !rangesDisjoint(database.scopes, database.scopeCount, kScopeSize,
                      database.index, database.indexCount, kIndexSize) ||
      !rangesDisjoint(database.objects, database.objectCount, kObjectSize,
                      database.types, database.typeCount, kTypeSize) ||
      !rangesDisjoint(database.objects, database.objectCount, kObjectSize,
                      database.strings, database.stringSize, 1) ||
      !rangesDisjoint(database.objects, database.objectCount, kObjectSize,
                      database.index, database.indexCount, kIndexSize) ||
      !rangesDisjoint(database.types, database.typeCount, kTypeSize,
                      database.strings, database.stringSize, 1) ||
      !rangesDisjoint(database.types, database.typeCount, kTypeSize,
                      database.index, database.indexCount, kIndexSize) ||
      !rangesDisjoint(database.strings, database.stringSize, 1, database.index,
                      database.indexCount, kIndexSize) ||
      !rangesDisjoint(database.statements, database.statementCount,
                      kStatementSize, database.statementSites,
                      database.statementSiteCount, kStatementSiteSize) ||
      !rangesDisjoint(database.statements, database.statementCount,
                      kStatementSize, database.scopes, database.scopeCount,
                      kScopeSize) ||
      !rangesDisjoint(database.statements, database.statementCount,
                      kStatementSize, database.objects, database.objectCount,
                      kObjectSize) ||
      !rangesDisjoint(database.statements, database.statementCount,
                      kStatementSize, database.types, database.typeCount,
                      kTypeSize) ||
      !rangesDisjoint(database.statements, database.statementCount,
                      kStatementSize, database.strings, database.stringSize,
                      1) ||
      !rangesDisjoint(database.statements, database.statementCount,
                      kStatementSize, database.index, database.indexCount,
                      kIndexSize) ||
      !rangesDisjoint(database.statementSites, database.statementSiteCount,
                      kStatementSiteSize, database.scopes, database.scopeCount,
                      kScopeSize) ||
      !rangesDisjoint(database.statementSites, database.statementSiteCount,
                      kStatementSiteSize, database.objects,
                      database.objectCount, kObjectSize) ||
      !rangesDisjoint(database.statementSites, database.statementSiteCount,
                      kStatementSiteSize, database.types, database.typeCount,
                      kTypeSize) ||
      !rangesDisjoint(database.statementSites, database.statementSiteCount,
                      kStatementSiteSize, database.strings, database.stringSize,
                      1) ||
      !rangesDisjoint(database.statementSites, database.statementSiteCount,
                      kStatementSiteSize, database.index, database.indexCount,
                      kIndexSize) ||
      !rangesDisjoint(database.relations, database.relationCount, kRelationSize,
                      database.scopes, database.scopeCount, kScopeSize) ||
      !rangesDisjoint(database.relations, database.relationCount, kRelationSize,
                      database.objects, database.objectCount, kObjectSize) ||
      !rangesDisjoint(database.relations, database.relationCount, kRelationSize,
                      database.types, database.typeCount, kTypeSize) ||
      !rangesDisjoint(database.relations, database.relationCount, kRelationSize,
                      database.strings, database.stringSize, 1) ||
      !rangesDisjoint(database.relations, database.relationCount, kRelationSize,
                      database.index, database.indexCount, kIndexSize) ||
      !rangesDisjoint(database.relations, database.relationCount, kRelationSize,
                      database.statements, database.statementCount,
                      kStatementSize) ||
      !rangesDisjoint(database.relations, database.relationCount, kRelationSize,
                      database.statementSites, database.statementSiteCount,
                      kStatementSiteSize) ||
      !rangesDisjoint(database.semanticTypes, database.semanticTypeCount,
                      kSemanticTypeSize, database.scopes, database.scopeCount,
                      kScopeSize) ||
      !rangesDisjoint(database.semanticTypes, database.semanticTypeCount,
                      kSemanticTypeSize, database.objects, database.objectCount,
                      kObjectSize) ||
      !rangesDisjoint(database.semanticTypes, database.semanticTypeCount,
                      kSemanticTypeSize, database.types, database.typeCount,
                      kTypeSize) ||
      !rangesDisjoint(database.semanticTypes, database.semanticTypeCount,
                      kSemanticTypeSize, database.statements,
                      database.statementCount, kStatementSize) ||
      !rangesDisjoint(database.semanticTypes, database.semanticTypeCount,
                      kSemanticTypeSize, database.statementSites,
                      database.statementSiteCount, kStatementSiteSize) ||
      !rangesDisjoint(database.semanticTypes, database.semanticTypeCount,
                      kSemanticTypeSize, database.relations,
                      database.relationCount, kRelationSize) ||
      !rangesDisjoint(database.semanticTypes, database.semanticTypeCount,
                      kSemanticTypeSize, database.strings, database.stringSize,
                      1) ||
      !rangesDisjoint(database.semanticTypes, database.semanticTypeCount,
                      kSemanticTypeSize, database.index, database.indexCount,
                      kIndexSize) ||
      !rangesDisjoint(database.semanticTypeEdges,
                      database.semanticTypeEdgeCount, kSemanticTypeEdgeSize,
                      database.semanticTypes, database.semanticTypeCount,
                      kSemanticTypeSize) ||
      !rangesDisjoint(database.semanticTypeEdges,
                      database.semanticTypeEdgeCount, kSemanticTypeEdgeSize,
                      database.scopes, database.scopeCount, kScopeSize) ||
      !rangesDisjoint(database.semanticTypeEdges,
                      database.semanticTypeEdgeCount, kSemanticTypeEdgeSize,
                      database.objects, database.objectCount, kObjectSize) ||
      !rangesDisjoint(database.semanticTypeEdges,
                      database.semanticTypeEdgeCount, kSemanticTypeEdgeSize,
                      database.types, database.typeCount, kTypeSize) ||
      !rangesDisjoint(database.semanticTypeEdges,
                      database.semanticTypeEdgeCount, kSemanticTypeEdgeSize,
                      database.statements, database.statementCount,
                      kStatementSize) ||
      !rangesDisjoint(database.semanticTypeEdges,
                      database.semanticTypeEdgeCount, kSemanticTypeEdgeSize,
                      database.statementSites, database.statementSiteCount,
                      kStatementSiteSize) ||
      !rangesDisjoint(database.semanticTypeEdges,
                      database.semanticTypeEdgeCount, kSemanticTypeEdgeSize,
                      database.relations, database.relationCount,
                      kRelationSize) ||
      !rangesDisjoint(database.semanticTypeEdges,
                      database.semanticTypeEdgeCount, kSemanticTypeEdgeSize,
                      database.strings, database.stringSize, 1) ||
      !rangesDisjoint(database.semanticTypeEdges,
                      database.semanticTypeEdgeCount, kSemanticTypeEdgeSize,
                      database.index, database.indexCount, kIndexSize) ||
      !rangesDisjoint(database.semanticRootBindings,
                      database.semanticRootBindingCount,
                      kSemanticRootBindingSize, database.semanticTypes,
                      database.semanticTypeCount, kSemanticTypeSize) ||
      !rangesDisjoint(database.semanticRootBindings,
                      database.semanticRootBindingCount,
                      kSemanticRootBindingSize, database.semanticTypeEdges,
                      database.semanticTypeEdgeCount, kSemanticTypeEdgeSize) ||
      !rangesDisjoint(database.semanticRootBindings,
                      database.semanticRootBindingCount,
                      kSemanticRootBindingSize, database.scopes,
                      database.scopeCount, kScopeSize) ||
      !rangesDisjoint(database.semanticRootBindings,
                      database.semanticRootBindingCount,
                      kSemanticRootBindingSize, database.objects,
                      database.objectCount, kObjectSize) ||
      !rangesDisjoint(database.semanticRootBindings,
                      database.semanticRootBindingCount,
                      kSemanticRootBindingSize, database.types,
                      database.typeCount, kTypeSize) ||
      !rangesDisjoint(database.semanticRootBindings,
                      database.semanticRootBindingCount,
                      kSemanticRootBindingSize, database.statements,
                      database.statementCount, kStatementSize) ||
      !rangesDisjoint(database.semanticRootBindings,
                      database.semanticRootBindingCount,
                      kSemanticRootBindingSize, database.statementSites,
                      database.statementSiteCount, kStatementSiteSize) ||
      !rangesDisjoint(database.semanticRootBindings,
                      database.semanticRootBindingCount,
                      kSemanticRootBindingSize, database.relations,
                      database.relationCount, kRelationSize) ||
      !rangesDisjoint(
          database.semanticRootBindings, database.semanticRootBindingCount,
          kSemanticRootBindingSize, database.strings, database.stringSize, 1) ||
      !rangesDisjoint(database.semanticRootBindings,
                      database.semanticRootBindingCount,
                      kSemanticRootBindingSize, database.index,
                      database.indexCount, kIndexSize))
    return false;
  struct Section {
    uint64_t offset;
    uint64_t count;
    uint64_t stride;
  };
  const Section sections[] = {
      {database.scopes, database.scopeCount, kScopeSize},
      {database.objects, database.objectCount, kObjectSize},
      {database.types, database.typeCount, kTypeSize},
      {database.strings, database.stringSize, 1},
      {database.index, database.indexCount, kIndexSize},
      {database.statements, database.statementCount, kStatementSize},
      {database.statementSites, database.statementSiteCount,
       kStatementSiteSize},
      {database.relations, database.relationCount, kRelationSize},
      {database.semanticTypes, database.semanticTypeCount, kSemanticTypeSize},
      {database.semanticTypeEdges, database.semanticTypeEdgeCount,
       kSemanticTypeEdgeSize},
      {database.semanticRootBindings, database.semanticRootBindingCount,
       kSemanticRootBindingSize},
      {database.relationIndices, database.relationIndexCount,
       kRelationIndexSize},
      {database.relationIndexDimensions, database.relationIndexDimensionCount,
       kRelationIndexDimensionSize},
      {database.relationIndexKeys, database.relationIndexKeyCount,
       kRelationIndexKeySize},
      {database.relationIndexMembers, database.relationIndexMemberCount,
       kRelationIndexMemberSize},
      {database.fixedProperties, database.fixedPropertyCount,
       kFixedPropertySize},
      {database.resolvedNetRuns, database.resolvedNetRunCount,
       kResolvedNetRunSize},
      {database.netDelayRuns, database.netDelayRunCount, kNetDelayRunSize},
      {database.staticObjects, database.staticObjectCount, kStaticObjectSize},
      {database.definitions, database.definitionCount, kDefinitionSize},
      {database.definitionBindings, database.definitionBindingCount,
       kDefinitionBindingSize},
      {database.definitionMembers, database.definitionMemberCount,
       kDefinitionMemberSize},
      {database.definitionMemberRelations,
       database.definitionMemberRelationCount, kDefinitionMemberRelationSize},
      {database.definitionMemberRelationTargets,
       database.definitionMemberRelationTargetCount,
       kDefinitionMemberRelationTargetSize},
      {database.definitionSpecializations,
       database.definitionSpecializationCount, kDefinitionSpecializationSize},
      {database.definitionSpecializationBindings,
       database.definitionSpecializationBindingCount,
       kDefinitionSpecializationBindingSize},
      {database.definitionMemberEndpoints,
       database.definitionMemberEndpointCount, kDefinitionMemberEndpointSize},
      {database.definitionMemberInstanceRelations,
       database.definitionMemberInstanceRelationCount,
       kDefinitionMemberInstanceRelationSize},
      {database.definitionMemberInstanceRelationTargets,
       database.definitionMemberInstanceRelationTargetCount,
       kDefinitionMemberInstanceRelationTargetSize},
      {database.definitionMemberInstanceRelationInverses,
       database.definitionMemberInstanceRelationInverseCount,
       kDefinitionMemberInstanceRelationInverseSize},
      {database.frozenValues, database.frozenValueCount, kFrozenValueSize},
      {database.frozenValueBindings, database.frozenValueBindingCount,
       kFrozenValueBindingSize},
      {database.frozenValuePayload, database.frozenValuePayloadSize, 1},
  };
  for (size_t left = 0; left != std::size(sections); ++left)
    for (size_t right = left + 1; right != std::size(sections); ++right)
      if (!rangesDisjoint(sections[left].offset, sections[left].count,
                          sections[left].stride, sections[right].offset,
                          sections[right].count, sections[right].stride))
        return false;
  return true;
}

bool isScopeOffset(const Database &database, uint64_t offset) {
  return offset >= database.scopes &&
         offset - database.scopes < database.scopeCount * kScopeSize &&
         (offset - database.scopes) % kScopeSize == 0;
}

bool isObjectOffset(const Database &database, uint64_t offset) {
  return offset >= database.objects &&
         offset - database.objects < database.objectCount * kObjectSize &&
         (offset - database.objects) % kObjectSize == 0;
}

bool isTypeOffset(const Database &database, uint64_t offset) {
  return offset >= database.types &&
         offset - database.types < database.typeCount * kTypeSize &&
         (offset - database.types) % kTypeSize == 0;
}

bool isStatementOffset(const Database &database, uint64_t offset) {
  return offset >= database.statements &&
         offset - database.statements <
             database.statementCount * kStatementSize &&
         (offset - database.statements) % kStatementSize == 0;
}

bool isStaticObjectOffset(const Database &database, uint64_t offset) {
  return offset >= database.staticObjects &&
         offset - database.staticObjects <
             database.staticObjectCount * kStaticObjectSize &&
         (offset - database.staticObjects) % kStaticObjectSize == 0;
}

bool effectiveStatementScope(const Database &database, uint32_t sourceIndex,
                             obelisk::reflection::TableKind &targetTable,
                             uint32_t &targetIndex) {
  if (sourceIndex >= database.statementCount)
    return false;
  const uint8_t *source = database.data + database.statements +
                          uint64_t{sourceIndex} * kStatementSize;
  uint32_t parent = read32(source + 16);
  for (uint64_t depth = 0; parent != UINT32_MAX; ++depth) {
    if (depth >= database.statementCount || parent >= database.statementCount)
      return false;
    const uint8_t *record =
        database.data + database.statements + uint64_t{parent} * kStatementSize;
    if ((read16(record + 38) & OBELISK_RT_DESIGN_STATEMENT_SCOPE) != 0) {
      targetTable = obelisk::reflection::TableKind::Statement;
      targetIndex = parent;
      return true;
    }
    parent = read32(record + 16);
  }

  uint32_t owner = read32(source + 8);
  if (owner != UINT32_MAX) {
    if (owner >= database.objectCount)
      return false;
    const uint8_t *record =
        database.data + database.objects + uint64_t{owner} * kObjectSize;
    const auto *kind =
        obelisk::reflection::findVPIObjectKind(recordVPIKind(record));
    if (kind && (kind->families &
                 obelisk::reflection::vpiFamilyMask(
                     obelisk::reflection::VPIObjectFamily::Scope)) != 0) {
      targetTable = obelisk::reflection::TableKind::Object;
      targetIndex = owner;
      return true;
    }
  }

  uint32_t scope = read32(source + 12);
  if (scope >= database.scopeCount)
    return false;
  targetTable = obelisk::reflection::TableKind::Scope;
  targetIndex = scope;
  return true;
}

uint64_t tableOffset(const Database &database,
                     obelisk::reflection::TableKind table, uint32_t index) {
  switch (table) {
  case obelisk::reflection::TableKind::Scope:
    return database.scopes + uint64_t{index} * kScopeSize;
  case obelisk::reflection::TableKind::Object:
    return database.objects + uint64_t{index} * kObjectSize;
  case obelisk::reflection::TableKind::Statement:
    return database.statements + uint64_t{index} * kStatementSize;
  case obelisk::reflection::TableKind::StaticObject:
    return database.staticObjects + uint64_t{index} * kStaticObjectSize;
  }
  return 0;
}

bool relationSourceForCursor(const Database &database, uint64_t offset,
                             obelisk::reflection::TableKind &table,
                             uint32_t &index) {
  if (isScopeOffset(database, offset)) {
    table = obelisk::reflection::TableKind::Scope;
    index = static_cast<uint32_t>((offset - database.scopes) / kScopeSize);
    return true;
  }
  if (isObjectOffset(database, offset)) {
    table = obelisk::reflection::TableKind::Object;
    index = static_cast<uint32_t>((offset - database.objects) / kObjectSize);
    return true;
  }
  if (isStatementOffset(database, offset)) {
    table = obelisk::reflection::TableKind::Statement;
    index =
        static_cast<uint32_t>((offset - database.statements) / kStatementSize);
    return true;
  }
  if (isStaticObjectOffset(database, offset)) {
    table = obelisk::reflection::TableKind::StaticObject;
    index = static_cast<uint32_t>((offset - database.staticObjects) /
                                  kStaticObjectSize);
    return true;
  }
  return false;
}

bool findDefinitionBinding(const Database &database, uint32_t scopeIndex,
                           uint32_t &definition, uint32_t &specialization,
                           uint32_t *bindingIndex = nullptr) {
  uint32_t packedSource = 0;
  if (!obelisk::reflection::tryPackTableIndex(
          obelisk::reflection::TableKind::Scope, scopeIndex, packedSource))
    return false;
  uint64_t low = 0;
  uint64_t high = database.definitionBindingCount;
  while (low != high) {
    uint64_t middle = low + (high - low) / 2;
    const uint8_t *binding = database.data + database.definitionBindings +
                             middle * kDefinitionBindingSize;
    if (read32(binding) < packedSource)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == database.definitionBindingCount)
    return false;
  const uint8_t *binding = database.data + database.definitionBindings +
                           low * kDefinitionBindingSize;
  if (read32(binding) != packedSource)
    return false;
  definition = read32(binding + 4);
  specialization = read32(binding + 8);
  if (bindingIndex)
    *bindingIndex = static_cast<uint32_t>(low);
  return true;
}

bool virtualMemberForCursor(const Database &database, uint64_t offset,
                            uint32_t &scopeIndex, uint32_t &memberIndex,
                            const uint8_t *&member,
                            uint32_t *outBindingIndex = nullptr,
                            uint32_t *outSpecialization = nullptr,
                            bool *outRefObject = nullptr) {
  uint32_t bindingIndex = 0;
  uint32_t payload = 0;
  if (!decodeVirtualToken(offset, kVirtualMemberTag, bindingIndex, payload) ||
      bindingIndex >= database.definitionBindingCount ||
      (payload & ~kVirtualRefObjectFlag) >= database.definitionMemberCount)
    return false;
  bool refObject = (payload & kVirtualRefObjectFlag) != 0;
  memberIndex = payload & ~kVirtualRefObjectFlag;
  const uint8_t *binding = database.data + database.definitionBindings +
                           uint64_t{bindingIndex} * kDefinitionBindingSize;
  uint32_t packedSource = read32(binding);
  if (obelisk::reflection::unpackTableIndexKind(packedSource) !=
      obelisk::reflection::TableKind::Scope)
    return false;
  scopeIndex = obelisk::reflection::unpackTableIndex(packedSource);
  uint32_t definitionIndex = read32(binding + 4);
  uint32_t specializationIndex = read32(binding + 8);
  if (scopeIndex >= database.scopeCount ||
      definitionIndex >= database.definitionCount)
    return false;
  const uint8_t *definition = database.data + database.definitions +
                              uint64_t{definitionIndex} * kDefinitionSize;
  uint32_t firstMember = read32(definition + 16);
  uint32_t memberCount = read32(definition + 20);
  if (memberIndex < firstMember || memberIndex - firstMember >= memberCount)
    return false;
  member = database.data + database.definitionMembers +
           uint64_t{memberIndex} * kDefinitionMemberSize;
  if (refObject &&
      (read16(member + 16) !=
           static_cast<uint16_t>(obelisk::reflection::VPIObjectKind::IODecl) ||
       read16(member + 18) != kVPIIODirectionRef))
    return false;
  if (outBindingIndex)
    *outBindingIndex = bindingIndex;
  if (outSpecialization)
    *outSpecialization = specializationIndex;
  if (outRefObject)
    *outRefObject = refObject;
  return true;
}

bool findDefinitionMemberSemanticRoot(const Database &database,
                                      uint32_t specializationIndex,
                                      uint32_t memberIndex,
                                      uint32_t &semanticType) {
  if (specializationIndex == UINT32_MAX ||
      specializationIndex >= database.definitionSpecializationCount)
    return false;
  const uint8_t *specialization =
      database.data + database.definitionSpecializations +
      uint64_t{specializationIndex} * kDefinitionSpecializationSize;
  uint32_t first = read32(specialization + 4);
  uint32_t count = read32(specialization + 8);
  uint32_t low = first;
  uint32_t high = first + count;
  while (low != high) {
    uint32_t middle = low + (high - low) / 2;
    const uint8_t *binding =
        database.data + database.definitionSpecializationBindings +
        uint64_t{middle} * kDefinitionSpecializationBindingSize;
    if (read32(binding) < memberIndex)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == first + count)
    return false;
  const uint8_t *binding = database.data +
                           database.definitionSpecializationBindings +
                           uint64_t{low} * kDefinitionSpecializationBindingSize;
  if (read32(binding) != memberIndex)
    return false;
  semanticType = read32(binding + 4);
  return true;
}

bool semanticBitWidth(const Database &database, uint32_t semanticType,
                      uint64_t &bitWidth) {
  if (semanticType >= database.semanticTypeCount)
    return false;
  const uint8_t *semantic = database.data + database.semanticTypes +
                            uint64_t{semanticType} * kSemanticTypeSize;
  bitWidth = read64(semantic + 48);
  uint32_t encoded = read32(semantic);
  uint32_t kind = encoded & UINT32_C(0xff);
  if (bitWidth == 0 &&
      (kind == OBELISK_RT_DESIGN_SEMANTIC_BIT ||
       kind == OBELISK_RT_DESIGN_SEMANTIC_LOGIC ||
       kind == OBELISK_RT_DESIGN_SEMANTIC_REG ||
       (encoded & OBELISK_RT_DESIGN_SEMANTIC_HAS_RANGE) != 0)) {
    int64_t left = readI64(semantic + 32);
    int64_t right = readI64(semantic + 40);
    uint64_t distance =
        left >= right
            ? static_cast<uint64_t>(left) - static_cast<uint64_t>(right)
            : static_cast<uint64_t>(right) - static_cast<uint64_t>(left);
    if (distance != UINT64_MAX)
      bitWidth = distance + 1;
  }
  if (bitWidth == 0)
    switch (kind) {
    case OBELISK_RT_DESIGN_SEMANTIC_BYTE:
      bitWidth = 8;
      break;
    case OBELISK_RT_DESIGN_SEMANTIC_SHORT_INT:
      bitWidth = 16;
      break;
    case OBELISK_RT_DESIGN_SEMANTIC_INT:
    case OBELISK_RT_DESIGN_SEMANTIC_INTEGER:
    case OBELISK_RT_DESIGN_SEMANTIC_SHORT_REAL:
      bitWidth = 32;
      break;
    case OBELISK_RT_DESIGN_SEMANTIC_LONG_INT:
    case OBELISK_RT_DESIGN_SEMANTIC_TIME:
    case OBELISK_RT_DESIGN_SEMANTIC_REAL:
    case OBELISK_RT_DESIGN_SEMANTIC_REALTIME:
      bitWidth = 64;
      break;
    default:
      break;
    }
  return true;
}

bool packedObjectReferenceOffset(const Database &database, uint32_t packed,
                                 uint64_t &offset) {
  using obelisk::reflection::TableKind;
  const TableKind table = obelisk::reflection::unpackTableIndexKind(packed);
  const uint32_t index = obelisk::reflection::unpackTableIndex(packed);
  switch (table) {
  case TableKind::Object:
    if (index >= database.objectCount)
      return false;
    offset = database.objects + uint64_t{index} * kObjectSize;
    return true;
  case TableKind::StaticObject:
    if (index >= database.staticObjectCount)
      return false;
    offset = database.staticObjects + uint64_t{index} * kStaticObjectSize;
    return true;
  case TableKind::Scope:
  case TableKind::Statement:
    return false;
  }
  return false;
}

bool packedObjectReferenceForCursor(const Database &database, uint64_t offset,
                                    uint32_t &packed) {
  obelisk::reflection::TableKind table;
  uint32_t index = 0;
  if (!relationSourceForCursor(database, offset, table, index) ||
      (table != obelisk::reflection::TableKind::Object &&
       table != obelisk::reflection::TableKind::StaticObject) ||
      !obelisk::reflection::tryPackTableIndex(table, index, packed) ||
      packed == UINT32_MAX)
    return false;
  return true;
}

uint32_t objectReferenceVPIKind(const Database &database, uint64_t offset) {
  if (isStaticObjectOffset(database, offset))
    return read16(database.data + offset + 28);
  return recordVPIKind(database.data + offset);
}

bool findSemanticRootBinding(const Database &database, uint32_t packedObject,
                             uint32_t &semanticType) {
  uint64_t low = 0;
  uint64_t high = database.semanticRootBindingCount;
  while (low != high) {
    uint64_t middle = low + (high - low) / 2;
    const uint8_t *binding = database.data + database.semanticRootBindings +
                             middle * kSemanticRootBindingSize;
    if (read32(binding) < packedObject)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == database.semanticRootBindingCount)
    return false;
  const uint8_t *binding = database.data + database.semanticRootBindings +
                           low * kSemanticRootBindingSize;
  if (read32(binding) != packedObject)
    return false;
  semanticType = read32(binding + 4);
  return true;
}

bool findFrozenValue(const Database &database, uint32_t packedObject,
                     VPIFrozenValue &value) {
  uint64_t low = 0;
  uint64_t high = database.frozenValueBindingCount;
  while (low != high) {
    uint64_t middle = low + (high - low) / 2;
    const uint8_t *binding = database.data + database.frozenValueBindings +
                             middle * kFrozenValueBindingSize;
    if (read32(binding) < packedObject)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == database.frozenValueBindingCount)
    return false;
  const uint8_t *binding = database.data + database.frozenValueBindings +
                           low * kFrozenValueBindingSize;
  if (read32(binding) != packedObject)
    return false;
  uint32_t valueIndex = read32(binding + 4);
  if (valueIndex >= database.frozenValueCount)
    return false;
  const uint8_t *record = database.data + database.frozenValues +
                          uint64_t{valueIndex} * kFrozenValueSize;
  value.kindAndFlags = read32(record);
  value.bitWidth = read64(record + 8);
  value.payload =
      database.data + database.frozenValuePayload + read64(record + 16);
  value.payloadSize = read64(record + 24);
  return true;
}

obelisk::reflection::RelationView relationAt(const Database &database,
                                             uint64_t index) {
  return obelisk::reflection::RelationView(database.data + database.relations +
                                           index * kRelationSize);
}

bool relationKeyLess(const obelisk::reflection::RelationView &relation,
                     obelisk::reflection::TableKind table, uint32_t sourceIndex,
                     uint16_t selector, bool iterate) {
  auto relationTable = obelisk::reflection::unpackRelationSourceTable(
      relation.getSourceKindAndTable());
  if (relationTable != table)
    return static_cast<uint8_t>(relationTable) < static_cast<uint8_t>(table);
  if (relation.getSourceIndex() != sourceIndex)
    return relation.getSourceIndex() < sourceIndex;
  if (relation.getSelector() != selector)
    return relation.getSelector() < selector;
  return obelisk::reflection::relationSourceIsIterate(
             relation.getSourceKindAndTable()) < iterate;
}

bool relationKeyAtMost(const obelisk::reflection::RelationView &relation,
                       obelisk::reflection::TableKind table,
                       uint32_t sourceIndex, uint16_t selector, bool iterate) {
  auto relationTable = obelisk::reflection::unpackRelationSourceTable(
      relation.getSourceKindAndTable());
  if (relationTable != table)
    return static_cast<uint8_t>(relationTable) < static_cast<uint8_t>(table);
  if (relation.getSourceIndex() != sourceIndex)
    return relation.getSourceIndex() < sourceIndex;
  if (relation.getSelector() != selector)
    return relation.getSelector() < selector;
  return obelisk::reflection::relationSourceIsIterate(
             relation.getSourceKindAndTable()) <= iterate;
}

uint64_t lowerBoundRelation(const Database &database,
                            obelisk::reflection::TableKind table,
                            uint32_t sourceIndex, uint16_t selector,
                            bool iterate) {
  uint64_t low = 0;
  uint64_t high = database.relationCount;
  while (low != high) {
    uint64_t middle = low + (high - low) / 2;
    if (relationKeyLess(relationAt(database, middle), table, sourceIndex,
                        selector, iterate))
      low = middle + 1;
    else
      high = middle;
  }
  return low;
}

uint64_t upperBoundRelation(const Database &database, uint64_t low,
                            obelisk::reflection::TableKind table,
                            uint32_t sourceIndex, uint16_t selector,
                            bool iterate) {
  uint64_t high = database.relationCount;
  while (low != high) {
    uint64_t middle = low + (high - low) / 2;
    if (relationKeyAtMost(relationAt(database, middle), table, sourceIndex,
                          selector, iterate))
      low = middle + 1;
    else
      high = middle;
  }
  return low;
}

bool relationMatches(const obelisk::reflection::RelationView &relation,
                     obelisk::reflection::TableKind table, uint32_t sourceIndex,
                     uint16_t selector, bool iterate) {
  return obelisk::reflection::unpackRelationSourceTable(
             relation.getSourceKindAndTable()) == table &&
         relation.getSourceIndex() == sourceIndex &&
         relation.getSelector() == selector &&
         obelisk::reflection::relationSourceIsIterate(
             relation.getSourceKindAndTable()) == iterate;
}

// Resolve the immutable physical net behind a declared net spelling. The
// common case has no stored vpiSimNet edge and therefore remains the source
// object itself. Only collapsed aliases pay one relation-table binary search
// on this cold VPI/debugger path.
obelisk_rt_status simulatedNetObjectIndex(const Database &database,
                                          uint32_t sourceIndex,
                                          uint32_t &targetIndex) {
  if (sourceIndex >= database.objectCount)
    return OBELISK_RT_INVALID_HANDLE;
  targetIndex = sourceIndex;
  constexpr uint16_t selector =
      static_cast<uint16_t>(obelisk::reflection::VPIRelationKind::SimNetRel);
  uint64_t index =
      lowerBoundRelation(database, obelisk::reflection::TableKind::Object,
                         sourceIndex, selector, false);
  if (index == database.relationCount)
    return OBELISK_RT_OK;
  auto relation = relationAt(database, index);
  if (!relationMatches(relation, obelisk::reflection::TableKind::Object,
                       sourceIndex, selector, false))
    return OBELISK_RT_OK;
  uint32_t packedTarget = relation.getTargetIndexAndTable();
  if (obelisk::reflection::unpackTableIndexKind(packedTarget) !=
      obelisk::reflection::TableKind::Object)
    return OBELISK_RT_INVALID_DESIGN;
  uint32_t candidate = obelisk::reflection::unpackTableIndex(packedTarget);
  if (candidate >= database.objectCount ||
      recordKind(database.data + database.objects +
                 uint64_t{candidate} * kObjectSize) !=
          OBELISK_RT_DESIGN_RECORD_NET)
    return OBELISK_RT_INVALID_DESIGN;
  targetIndex = candidate;
  return OBELISK_RT_OK;
}

bool getRecord(const Database &database, uint64_t offset,
               const uint8_t *&record, uint32_t &kind) {
  if (!isScopeOffset(database, offset) && !isObjectOffset(database, offset) &&
      !isTypeOffset(database, offset))
    return false;
  record = database.data + offset;
  kind = recordKind(record);
  if (isScopeOffset(database, offset))
    return kind == OBELISK_RT_DESIGN_RECORD_SCOPE;
  if (isTypeOffset(database, offset))
    return kind == OBELISK_RT_DESIGN_RECORD_TYPE;
  return (kind >= OBELISK_RT_DESIGN_RECORD_STORAGE &&
          kind <= OBELISK_RT_DESIGN_RECORD_PROCESS) ||
         kind == OBELISK_RT_DESIGN_RECORD_FUNCTION ||
         kind == OBELISK_RT_DESIGN_RECORD_PORT ||
         kind == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT;
}

bool getString(const Database &database, uint64_t offset,
               std::string_view &result) {
  if (offset < database.strings ||
      offset - database.strings >= database.stringSize)
    return false;
  const char *begin = reinterpret_cast<const char *>(database.data + offset);
  uint64_t remaining = database.stringSize - (offset - database.strings);
  const void *end = std::memchr(begin, 0, static_cast<size_t>(remaining));
  if (!end)
    return false;
  result = std::string_view(begin, static_cast<const char *>(end) - begin);
  return true;
}

bool validSource(const Database &database, uint64_t fileOffset,
                 uint64_t lineColumn) {
  if (fileOffset == 0)
    return lineColumn == 0;
  std::string_view file;
  return getString(database, fileOffset, file) && !file.empty() &&
         static_cast<uint32_t>(lineColumn >> 32) != 0 &&
         static_cast<uint32_t>(lineColumn) != 0;
}

bool rangeExtent(const uint8_t *record, uint64_t &extent) {
  int64_t left = readI64(record + 16);
  int64_t right = readI64(record + 24);
  uint64_t distance =
      left >= right
          ? static_cast<uint64_t>(left) - static_cast<uint64_t>(right)
          : static_cast<uint64_t>(right) - static_cast<uint64_t>(left);
  if (distance == UINT64_MAX)
    return false;
  extent = distance + 1;
  return true;
}

uint64_t nextOffset(const uint8_t *record, uint32_t kind) {
  return read64(record + (kind == OBELISK_RT_DESIGN_RECORD_SCOPE ? 32 : 24));
}

constexpr uint32_t kPortIdentityCaps =
    OBELISK_RT_DESIGN_CAP_PORT_INPUT | OBELISK_RT_DESIGN_CAP_PORT_OUTPUT |
    OBELISK_RT_DESIGN_CAP_PORT_REF | OBELISK_RT_DESIGN_CAP_PORT_ORDINAL_MASK;

bool isWholePortConnection(const uint8_t *port, const uint8_t *canonical) {
  uint32_t canonicalKind = recordKind(canonical);
  if (recordKind(port) != OBELISK_RT_DESIGN_RECORD_PORT ||
      (read32(port + 4) & OBELISK_RT_DESIGN_CAP_PORT_WHOLE_SOURCE) == 0 ||
      (canonicalKind != OBELISK_RT_DESIGN_RECORD_STORAGE &&
       canonicalKind != OBELISK_RT_DESIGN_RECORD_NET) ||
      read64(port + 16) != read64(canonical + 16) ||
      read64(port + 48) != read64(canonical + 48) ||
      read64(port + 56) != read64(canonical + 56) ||
      read64(port + 64) != read64(canonical + 64) ||
      read64(port + 72) != read64(canonical + 72) ||
      read64(port + 80) != read64(canonical + 80))
    return false;
  // Same-name port aliases duplicate the canonical object's port metadata;
  // explicitly renamed formals keep their own direction and ordinal.
  return read64(port + 40) != read64(canonical + 40) ||
         (read32(port + 4) & kPortIdentityCaps) ==
             (read32(canonical + 4) & kPortIdentityCaps);
}

bool validateDatabaseImpl(const Database &database) {
  if (database.scopeCount == 0 || !isScopeOffset(database, database.root))
    return false;
  std::array<std::unordered_set<uint64_t>,
             OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT + 1>
      stableIDs;
  std::unordered_set<uint64_t> reached;
  std::vector<uint64_t> pending{database.root};
  while (!pending.empty()) {
    uint64_t offset = pending.back();
    pending.pop_back();
    if (!reached.insert(offset).second)
      return false;
    const uint8_t *record;
    uint32_t kind;
    if (!getRecord(database, offset, record, kind) ||
        kind == OBELISK_RT_DESIGN_RECORD_TYPE)
      return false;
    uint32_t caps = read32(record + 4);
    uint32_t intrinsicKind = recordVPIKind(record);
    bool internal = (caps & OBELISK_RT_DESIGN_CAP_INTERNAL) != 0;
    using VPIKind = obelisk::reflection::VPIObjectKind;
    bool lexicalAnchor =
        (caps & OBELISK_RT_DESIGN_CAP_LEXICAL_ANCHOR) != 0 &&
        (kind == OBELISK_RT_DESIGN_RECORD_PROCESS ||
         kind == OBELISK_RT_DESIGN_RECORD_FUNCTION ||
         (kind == OBELISK_RT_DESIGN_RECORD_NET &&
          intrinsicKind == static_cast<uint32_t>(VPIKind::InterconnectNet)));
    bool explicitParameterRange =
        kind == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT &&
        intrinsicKind == static_cast<uint32_t>(VPIKind::Parameter) &&
        (caps & OBELISK_RT_DESIGN_CAP_PARAMETER_EXPLICIT_RANGE) != 0;
    bool mayOmitIntrinsic =
        (kind == OBELISK_RT_DESIGN_RECORD_SCOPE && offset == database.root) ||
        kind == OBELISK_RT_DESIGN_RECORD_DRIVER ||
        (internal && (kind == OBELISK_RT_DESIGN_RECORD_PROCESS ||
                      kind == OBELISK_RT_DESIGN_RECORD_FUNCTION));
    if ((intrinsicKind == 0
             ? !mayOmitIntrinsic
             : internal || !recordKindSupportsVPI(kind, intrinsicKind)))
      return false;
    uint32_t supportedCaps =
        OBELISK_RT_DESIGN_CAP_READ | OBELISK_RT_DESIGN_CAP_WRITE |
        OBELISK_RT_DESIGN_CAP_ITERATE | OBELISK_RT_DESIGN_CAP_PORT_INPUT |
        OBELISK_RT_DESIGN_CAP_PORT_OUTPUT | OBELISK_RT_DESIGN_CAP_INTERNAL |
        OBELISK_RT_DESIGN_CAP_PORT_WHOLE_SOURCE |
        OBELISK_RT_DESIGN_CAP_NAMED_TYPESPEC |
        OBELISK_RT_DESIGN_CAP_LEXICAL_ANCHOR |
        OBELISK_RT_DESIGN_CAP_PORT_ORDINAL_MASK;
    if ((caps & ~supportedCaps) != 0 ||
        ((caps & OBELISK_RT_DESIGN_CAP_WRITE) != 0 &&
         (database.profile & OBELISK_RT_DESIGN_PROFILE_WRITE) == 0))
      return false;
    if ((caps & OBELISK_RT_DESIGN_CAP_NAMED_TYPESPEC) != 0 && !lexicalAnchor &&
        !explicitParameterRange && kind != OBELISK_RT_DESIGN_RECORD_PORT) {
      const auto *descriptor =
          obelisk::reflection::findVPIObjectKind(intrinsicKind);
      if (kind != OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT || !descriptor ||
          (descriptor->families &
           obelisk::reflection::vpiFamilyMask(
               obelisk::reflection::VPIObjectFamily::Typespec)) == 0)
        return false;
    }
    uint64_t stableID = read64(record + 8);
    if (!stableIDs[kind].insert(stableID).second)
      return false;
    std::string_view name;
    uint64_t nameOffset = read64(record + 40);
    if (!getString(database, nameOffset, name) || name.empty())
      return false;
    if (kind == OBELISK_RT_DESIGN_RECORD_SCOPE) {
      if (caps != OBELISK_RT_DESIGN_CAP_ITERATE ||
          !validSource(database, read64(record + 48), read64(record + 56)))
        return false;
      uint64_t parent = read64(record + 16);
      if (offset == database.root ? parent != 0
                                  : !isScopeOffset(database, parent))
        return false;
      uint64_t child = read64(record + 24);
      uint64_t childCount = 0;
      while (child != 0) {
        if (++childCount > database.scopeCount + database.objectCount)
          return false;
        const uint8_t *childRecord;
        uint32_t childKind;
        if (!getRecord(database, child, childRecord, childKind) ||
            childKind == OBELISK_RT_DESIGN_RECORD_TYPE ||
            read64(childRecord + 16) != offset)
          return false;
        pending.push_back(child);
        child = nextOffset(childRecord, childKind);
      }
    } else {
      uint64_t typeOffset = read64(record + 48);
      if (!isScopeOffset(database, read64(record + 16)) ||
          !validSource(database, read64(record + 32), read64(record + 88)))
        return false;
      if (kind == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT) {
        const auto *indexed =
            obelisk::reflection::findVPIIndexedAccess(intrinsicKind);
        bool relationIndexed =
            indexed &&
            indexed->accessKind ==
                obelisk::reflection::VPIIndexedAccessKind::RelationElement;
        using VPIKind = obelisk::reflection::VPIObjectKind;
        bool scalarPrimitive =
            intrinsicKind == static_cast<uint32_t>(VPIKind::Gate) ||
            intrinsicKind == static_cast<uint32_t>(VPIKind::Switch) ||
            intrinsicKind == static_cast<uint32_t>(VPIKind::Udp);
        if ((caps & ~OBELISK_RT_DESIGN_CAP_NAMED_TYPESPEC) != 0 ||
            read64(record + 80) != 0 ||
            (typeOffset == 0
                 ? relationIndexed
                       ? intrinsicKind !=
                                 static_cast<uint32_t>(
                                     obelisk::reflection::VPIObjectKind::
                                         GenScopeArray) &&
                             read64(record + 56) == 0
                   : scalarPrimitive
                       ? read64(record + 56) > INT64_MAX ||
                             readI64(record + 64) !=
                                 (read64(record + 56) == 0
                                      ? 0
                                      : static_cast<int64_t>(
                                            read64(record + 56) - 1)) ||
                             readI64(record + 72) != 0
                       : read64(record + 56) != 0 || read64(record + 64) != 0 ||
                             read64(record + 72) != 0
                 : !isTypeOffset(database, typeOffset) ||
                       read64(record + 56) == 0 ||
                       read64(database.data + typeOffset + 8) !=
                           read64(record + 56)))
          return false;
      } else if (kind == OBELISK_RT_DESIGN_RECORD_PROCESS ||
                 kind == OBELISK_RT_DESIGN_RECORD_FUNCTION) {
        if ((caps & ~(OBELISK_RT_DESIGN_CAP_INTERNAL |
                      OBELISK_RT_DESIGN_CAP_LEXICAL_ANCHOR)) != 0 ||
            (internal && lexicalAnchor) || typeOffset != 0 ||
            read64(record + 56) != 0 || read64(record + 64) != 0 ||
            read64(record + 72) != 0 || read64(record + 80) != 0)
          return false;
      } else if (internal || (caps & OBELISK_RT_DESIGN_CAP_READ) == 0 ||
                 (caps &
                  ~(OBELISK_RT_DESIGN_CAP_READ | OBELISK_RT_DESIGN_CAP_WRITE |
                    OBELISK_RT_DESIGN_CAP_PORT_INPUT |
                    OBELISK_RT_DESIGN_CAP_PORT_OUTPUT |
                    OBELISK_RT_DESIGN_CAP_PORT_REF |
                    OBELISK_RT_DESIGN_CAP_PORT_WHOLE_SOURCE |
                    OBELISK_RT_DESIGN_CAP_LEXICAL_ANCHOR |
                    OBELISK_RT_DESIGN_CAP_PORT_ORDINAL_MASK)) != 0 ||
                 !isTypeOffset(database, typeOffset) ||
                 read64(record + 56) == 0 ||
                 read64(database.data + typeOffset + 8) !=
                     read64(record + 56)) {
        return false;
      }
      uint32_t portCaps = caps & (OBELISK_RT_DESIGN_CAP_PORT_INPUT |
                                  OBELISK_RT_DESIGN_CAP_PORT_OUTPUT);
      uint32_t ordinalCaps = caps & OBELISK_RT_DESIGN_CAP_PORT_ORDINAL_MASK;
      bool wholePortSource =
          (caps & OBELISK_RT_DESIGN_CAP_PORT_WHOLE_SOURCE) != 0;
      bool refPort = (caps & OBELISK_RT_DESIGN_CAP_PORT_REF) != 0;
      if (wholePortSource && kind != OBELISK_RT_DESIGN_RECORD_PORT)
        return false;
      if (kind == OBELISK_RT_DESIGN_RECORD_PORT) {
        if (portCaps == 0 ||
            (refPort &&
             portCaps != (OBELISK_RT_DESIGN_CAP_PORT_INPUT |
                          OBELISK_RT_DESIGN_CAP_PORT_OUTPUT)) ||
            caps != (OBELISK_RT_DESIGN_CAP_READ | portCaps | ordinalCaps |
                     (caps & OBELISK_RT_DESIGN_CAP_PORT_REF) |
                     (caps & OBELISK_RT_DESIGN_CAP_PORT_WHOLE_SOURCE)))
          return false;
      } else if (portCaps != 0 && kind != OBELISK_RT_DESIGN_RECORD_STORAGE &&
                 kind != OBELISK_RT_DESIGN_RECORD_NET) {
        return false;
      } else if (portCaps == 0 && ordinalCaps != 0) {
        return false;
      }
      uint64_t stateOffset = read64(record + 80);
      uint64_t width = read64(record + 56);
      if (kind != OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT &&
          (stateOffset > database.stateBitCount ||
           width > database.stateBitCount - stateOffset))
        return false;
    }
  }
  if (reached.size() != database.scopeCount + database.objectCount)
    return false;

  // Fixed properties are a sparse, globally sorted map.  Validate against the
  // generated exact-kind catalog once when publishing the immutable image so
  // every VPI query can use a zero-allocation binary search.
  bool havePreviousFixedProperty = false;
  uint32_t previousFixedSource = 0;
  uint16_t previousFixedSelector = 0;
  std::vector<bool> protectedStatements(database.statementCount, false);
  for (uint64_t index = 0; index != database.fixedPropertyCount; ++index) {
    const uint8_t *record =
        database.data + database.fixedProperties + index * kFixedPropertySize;
    uint32_t packedSource = read32(record);
    auto table = obelisk::reflection::unpackTableIndexKind(packedSource);
    uint32_t sourceIndex = obelisk::reflection::unpackTableIndex(packedSource);
    uint16_t selector = read16(record + 4);
    uint16_t kindAndFlags = read16(record + 6);
    uint64_t payload = read64(record + 8);
    if (!obelisk::reflection::isValidTableKind(table) ||
        (havePreviousFixedProperty &&
         std::tie(packedSource, selector) <=
             std::tie(previousFixedSource, previousFixedSelector)))
      return false;
    havePreviousFixedProperty = true;
    previousFixedSource = packedSource;
    previousFixedSelector = selector;

    uint32_t exactKind = 0;
    switch (table) {
    case obelisk::reflection::TableKind::Scope:
      if (sourceIndex >= database.scopeCount)
        return false;
      exactKind = recordVPIKind(database.data + database.scopes +
                                uint64_t{sourceIndex} * kScopeSize);
      break;
    case obelisk::reflection::TableKind::Object:
      if (sourceIndex >= database.objectCount)
        return false;
      exactKind = recordVPIKind(database.data + database.objects +
                                uint64_t{sourceIndex} * kObjectSize);
      break;
    case obelisk::reflection::TableKind::Statement:
      if (sourceIndex >= database.statementCount)
        return false;
      exactKind = read16(database.data + database.statements +
                         uint64_t{sourceIndex} * kStatementSize + 36);
      break;
    case obelisk::reflection::TableKind::StaticObject:
      if (sourceIndex >= database.staticObjectCount)
        return false;
      exactKind = read16(database.data + database.staticObjects +
                         uint64_t{sourceIndex} * kStaticObjectSize + 28);
      break;
    }
    const auto *descriptor =
        obelisk::reflection::findVPIProperty(exactKind, selector);
    if (!descriptor ||
        (descriptor->realization !=
             obelisk::reflection::VPIPropertyRealization::FixedImage &&
         descriptor->realization !=
             obelisk::reflection::VPIPropertyRealization::DefinitionImage) ||
        kindAndFlags != static_cast<uint16_t>(descriptor->valueKind))
      return false;
    using ValueKind = obelisk::reflection::VPIPropertyValueKind;
    switch (descriptor->valueKind) {
    case ValueKind::Boolean:
      // Immutable false values have one canonical encoding: absence from the
      // sparse table. Reject redundant false records for every generated
      // FixedImage Boolean, not only vpiIsProtected.
      if (payload != 1)
        return false;
      break;
    case ValueKind::Integer:
      if (payload != static_cast<uint64_t>(
                         static_cast<int64_t>(static_cast<int32_t>(payload))))
        return false;
      if (obelisk::reflection::hasVPIIntegerPropertyDomain(selector) &&
          !obelisk::reflection::findVPIIntegerPropertyValue(
              selector, static_cast<uint32_t>(payload)))
        return false;
      break;
    case ValueKind::Int64:
      break;
    case ValueKind::String: {
      std::string_view value;
      if (!getString(database, payload, value) ||
          (payload != database.strings && database.data[payload - 1] != 0))
        return false;
      break;
    }
    }
    if (selector == 74) {
      if (table == obelisk::reflection::TableKind::Statement)
        protectedStatements[sourceIndex] = true;
    } else if (selector == 15) {
      std::string_view file;
      if (!getString(database, payload, file) || file.empty())
        return false;
    } else if (selector == 16) {
      if (payload == 0 || payload > INT32_MAX)
        return false;
    }
  }
  for (uint32_t index = 0; index != database.statementCount; ++index) {
    bool protectedFlag = (read16(database.data + database.statements +
                                 uint64_t{index} * kStatementSize + 38) &
                          OBELISK_RT_DESIGN_STATEMENT_PROTECTED) != 0;
    if (database.fixedProperties != 0 &&
        protectedFlag != protectedStatements[index])
      return false;
  }

  // Resolved net types are a canonical per-object run map. Gaps are
  // meaningful (the exact dominating subtype is unavailable), so adjacent
  // equal runs must be coalesced while unlike adjacent runs remain distinct.
  bool havePreviousResolvedRun = false;
  uint32_t previousResolvedObject = 0;
  uint32_t previousResolvedType = 0;
  uint64_t previousResolvedEnd = 0;
  for (uint64_t index = 0; index != database.resolvedNetRunCount; ++index) {
    const uint8_t *run =
        database.data + database.resolvedNetRuns + index * kResolvedNetRunSize;
    uint32_t objectIndex = read32(run);
    uint32_t netType = read32(run + 4);
    uint64_t firstBit = read64(run + 8);
    uint64_t bitCount = read64(run + 16);
    if (objectIndex >= database.objectCount || bitCount == 0)
      return false;
    const uint8_t *object =
        database.data + database.objects + uint64_t{objectIndex} * kObjectSize;
    uint64_t width = read64(object + 56);
    uint32_t exactKind = recordVPIKind(object);
    const auto *descriptor =
        obelisk::reflection::findVPIProperty(exactKind, 61);
    if (recordKind(object) != OBELISK_RT_DESIGN_RECORD_NET || !descriptor ||
        descriptor->realization !=
            obelisk::reflection::VPIPropertyRealization::IndexedImage ||
        firstBit > width || bitCount > width - firstBit ||
        !obelisk::reflection::findVPIIntegerPropertyValue(61, netType))
      return false;
    if (havePreviousResolvedRun) {
      if (objectIndex < previousResolvedObject ||
          (objectIndex == previousResolvedObject &&
           (firstBit < previousResolvedEnd ||
            (firstBit == previousResolvedEnd &&
             netType == previousResolvedType))))
        return false;
    }
    havePreviousResolvedRun = true;
    previousResolvedObject = objectIndex;
    previousResolvedType = netType;
    previousResolvedEnd = firstBit + bitCount;
  }

  // Net delays form a canonical sparse per-object run map. Gaps represent the
  // default zero delay. The third value may be -1 only for indefinite trireg
  // charge retention.
  bool havePreviousDelayRun = false;
  uint32_t previousDelayObject = 0;
  uint64_t previousDelayEnd = 0;
  std::array<int64_t, 3> previousDelays{};
  for (uint64_t index = 0; index != database.netDelayRunCount; ++index) {
    const uint8_t *run =
        database.data + database.netDelayRuns + index * kNetDelayRunSize;
    uint32_t objectIndex = read32(run);
    uint32_t reserved = read32(run + 4);
    uint64_t firstBit = read64(run + 8);
    uint64_t bitCount = read64(run + 16);
    std::array<int64_t, 3> delays{readI64(run + 24), readI64(run + 32),
                                  readI64(run + 40)};
    if (objectIndex >= database.objectCount || reserved != 0 || bitCount == 0 ||
        delays == std::array<int64_t, 3>{} || delays[0] < 0 || delays[1] < 0 ||
        delays[2] < -1)
      return false;
    const uint8_t *object =
        database.data + database.objects + uint64_t{objectIndex} * kObjectSize;
    uint64_t width = read64(object + 56);
    if (recordKind(object) != OBELISK_RT_DESIGN_RECORD_NET ||
        firstBit > width || bitCount > width - firstBit)
      return false;
    if (havePreviousDelayRun &&
        (objectIndex < previousDelayObject ||
         (objectIndex == previousDelayObject &&
          (firstBit < previousDelayEnd ||
           (firstBit == previousDelayEnd && delays == previousDelays)))))
      return false;
    havePreviousDelayRun = true;
    previousDelayObject = objectIndex;
    previousDelayEnd = firstBit + bitCount;
    previousDelays = delays;
  }

  for (uint64_t index = 0; index != database.typeCount; ++index) {
    const uint8_t *record = database.data + database.types + index * kTypeSize;
    uint32_t encoded = read32(record + 4);
    uint32_t typeKind = encoded & UINT32_C(0xff);
    uint32_t flags = encoded >> 8;
    uint64_t width = read64(record + 8);
    uint64_t element = read64(record + 32);
    uint64_t firstChild = read64(record + 40);
    uint64_t childCount = read64(record + 48);
    uint64_t extent = 0;
    if (read32(record) != OBELISK_RT_DESIGN_RECORD_TYPE ||
        typeKind < OBELISK_RT_DESIGN_TYPE_SCALAR ||
        typeKind > OBELISK_RT_DESIGN_TYPE_FIELD ||
        (flags &
         ~(OBELISK_RT_DESIGN_TYPE_FOUR_STATE | OBELISK_RT_DESIGN_TYPE_SIGNED |
           OBELISK_RT_DESIGN_TYPE_PACKED | OBELISK_RT_DESIGN_TYPE_TAGGED)) !=
            0 ||
        width == 0)
      return false;
    bool hasElement = element != 0;
    bool hasChildren = firstChild != 0 || childCount != 0;
    if ((hasElement && !isTypeOffset(database, element)) ||
        (hasChildren &&
         (!isTypeOffset(database, firstChild) || childCount == 0 ||
          childCount > database.typeCount ||
          (childCount - 1) >
              (std::numeric_limits<uint64_t>::max() - firstChild) / kTypeSize ||
          !isTypeOffset(database, firstChild + (childCount - 1) * kTypeSize))))
      return false;
    switch (typeKind) {
    case OBELISK_RT_DESIGN_TYPE_SCALAR:
      if (hasElement || hasChildren || read64(record + 56) != 0 ||
          read64(record + 64) != 0 ||
          (flags & OBELISK_RT_DESIGN_TYPE_TAGGED) != 0 ||
          !rangeExtent(record, extent) || extent != width)
        return false;
      break;
    case OBELISK_RT_DESIGN_TYPE_ARRAY:
      if (!hasElement || hasChildren || read64(record + 56) != 0 ||
          read64(record + 64) != 0 ||
          (flags & (OBELISK_RT_DESIGN_TYPE_SIGNED |
                    OBELISK_RT_DESIGN_TYPE_TAGGED)) != 0 ||
          !rangeExtent(record, extent))
        return false;
      {
        const uint8_t *elementRecord = database.data + element;
        uint64_t elementWidth = read64(elementRecord + 8);
        uint32_t elementKind = read32(elementRecord + 4) & UINT32_C(0xff);
        uint32_t elementFlags = read32(elementRecord + 4) >> 8;
        if (elementKind == OBELISK_RT_DESIGN_TYPE_FIELD || elementWidth == 0 ||
            extent > UINT64_MAX / elementWidth ||
            extent * elementWidth != width ||
            ((flags & OBELISK_RT_DESIGN_TYPE_FOUR_STATE) != 0) !=
                ((elementFlags & OBELISK_RT_DESIGN_TYPE_FOUR_STATE) != 0))
          return false;
      }
      break;
    case OBELISK_RT_DESIGN_TYPE_STRUCT:
      if (hasElement || !hasChildren || read64(record + 56) != 0 ||
          read64(record + 64) != 0 ||
          (flags & (OBELISK_RT_DESIGN_TYPE_SIGNED |
                    OBELISK_RT_DESIGN_TYPE_TAGGED)) != 0 ||
          !rangeExtent(record, extent) || extent != width)
        return false;
      break;
    case OBELISK_RT_DESIGN_TYPE_UNION:
      if (hasElement || !hasChildren || read64(record + 64) != 0 ||
          (flags & OBELISK_RT_DESIGN_TYPE_SIGNED) != 0 ||
          (((flags & OBELISK_RT_DESIGN_TYPE_TAGGED) != 0) !=
           (read64(record + 56) != 0)) ||
          !rangeExtent(record, extent) || extent != width)
        return false;
      break;
    case OBELISK_RT_DESIGN_TYPE_FIELD:
      if (!hasElement || hasChildren ||
          (flags &
           (OBELISK_RT_DESIGN_TYPE_SIGNED | OBELISK_RT_DESIGN_TYPE_PACKED |
            OBELISK_RT_DESIGN_TYPE_TAGGED)) != 0 ||
          !rangeExtent(record, extent) || extent != width)
        return false;
      {
        const uint8_t *elementRecord = database.data + element;
        uint32_t elementKind = read32(elementRecord + 4) & UINT32_C(0xff);
        uint32_t elementFlags = read32(elementRecord + 4) >> 8;
        if (elementKind == OBELISK_RT_DESIGN_TYPE_FIELD ||
            read64(elementRecord + 8) != width ||
            ((flags & OBELISK_RT_DESIGN_TYPE_FOUR_STATE) != 0) !=
                ((elementFlags & OBELISK_RT_DESIGN_TYPE_FOUR_STATE) != 0))
          return false;
      }
      break;
    }
    std::string_view name;
    if (!getString(database, read64(record + 72), name) || name.empty())
      return false;
  }

  for (uint64_t index = 0; index != database.typeCount; ++index) {
    const uint8_t *record = database.data + database.types + index * kTypeSize;
    uint32_t encoded = read32(record + 4);
    uint32_t kind = encoded & UINT32_C(0xff);
    if (kind != OBELISK_RT_DESIGN_TYPE_STRUCT &&
        kind != OBELISK_RT_DESIGN_TYPE_UNION)
      continue;
    uint32_t flags = encoded >> 8;
    uint64_t width = read64(record + 8);
    uint64_t firstChild = read64(record + 40);
    uint64_t childCount = read64(record + 48);
    uint64_t sum = 0, maximum = 0;
    bool fourState = false;
    std::vector<std::pair<uint64_t, uint64_t>> packedRanges;
    packedRanges.reserve(static_cast<size_t>(childCount));
    for (uint64_t child = 0; child != childCount; ++child) {
      const uint8_t *field = database.data + firstChild + child * kTypeSize;
      uint64_t fieldWidth = read64(field + 8);
      uint64_t packedOffset = read64(field + 64);
      fourState |=
          ((read32(field + 4) >> 8) & OBELISK_RT_DESIGN_TYPE_FOUR_STATE) != 0;
      if (fieldWidth > UINT64_MAX - sum)
        return false;
      sum += fieldWidth;
      maximum = std::max(maximum, fieldWidth);
      if (kind == OBELISK_RT_DESIGN_TYPE_STRUCT ||
          (flags & OBELISK_RT_DESIGN_TYPE_PACKED) != 0) {
        uint64_t tagBits = read64(record + 56);
        if (kind == OBELISK_RT_DESIGN_TYPE_UNION && tagBits > width)
          return false;
        uint64_t payloadWidth =
            kind == OBELISK_RT_DESIGN_TYPE_UNION ? width - tagBits : width;
        if (packedOffset > payloadWidth ||
            fieldWidth > payloadWidth - packedOffset)
          return false;
        packedRanges.emplace_back(packedOffset, packedOffset + fieldWidth);
      }
    }
    if (((flags & OBELISK_RT_DESIGN_TYPE_FOUR_STATE) != 0) != fourState)
      return false;
    if (kind == OBELISK_RT_DESIGN_TYPE_STRUCT) {
      // Unpacked storage may contain alignment and tail padding, notably
      // around string handles and real fields in covergroup options. Only
      // packed structs require contiguous bits (IEEE 1800-2023 7.2/7.2.1).
      bool packed = (flags & OBELISK_RT_DESIGN_TYPE_PACKED) != 0;
      if (packed && sum != width)
        return false;
      std::sort(packedRanges.begin(), packedRanges.end());
      uint64_t cursor = 0;
      for (auto [begin, end] : packedRanges) {
        if (begin < cursor || (packed && begin != cursor))
          return false;
        cursor = end;
      }
      if (packed && cursor != width)
        return false;
    } else {
      uint64_t tagBits = read64(record + 56);
      if (maximum > UINT64_MAX - tagBits || maximum + tagBits != width)
        return false;
    }
  }

  std::vector<uint8_t> typeState(static_cast<size_t>(database.typeCount));
  auto visitType = [&](uint64_t root) {
    struct WorkItem {
      uint64_t offset;
      bool finish;
    };
    std::vector<WorkItem> worklist{{root, false}};
    while (!worklist.empty()) {
      WorkItem item = worklist.back();
      worklist.pop_back();
      if (!isTypeOffset(database, item.offset))
        return false;
      size_t index =
          static_cast<size_t>((item.offset - database.types) / kTypeSize);
      if (item.finish) {
        typeState[index] = 2;
        continue;
      }
      if (typeState[index] == 1)
        return false;
      if (typeState[index] == 2)
        continue;
      typeState[index] = 1;
      worklist.push_back({item.offset, true});
      const uint8_t *record = database.data + item.offset;
      uint64_t firstChild = read64(record + 40);
      uint64_t childCount = read64(record + 48);
      for (uint64_t child = childCount; child != 0; --child) {
        uint64_t ordinal = child - 1;
        uint64_t childOffset = firstChild + ordinal * kTypeSize;
        const uint8_t *childRecord = database.data + childOffset;
        if ((read32(childRecord + 4) & UINT32_C(0xff)) !=
                OBELISK_RT_DESIGN_TYPE_FIELD ||
            read64(childRecord + 56) != ordinal)
          return false;
        worklist.push_back({childOffset, false});
      }
      uint64_t element = read64(record + 32);
      if (element != 0)
        worklist.push_back({element, false});
    }
    return true;
  };
  for (uint64_t index = 0; index != database.objectCount; ++index) {
    const uint8_t *record =
        database.data + database.objects + index * kObjectSize;
    uint32_t kind = recordKind(record);
    if (kind != OBELISK_RT_DESIGN_RECORD_PROCESS &&
        kind != OBELISK_RT_DESIGN_RECORD_FUNCTION &&
        !(kind == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT &&
          read64(record + 48) == 0) &&
        !visitType(read64(record + 48)))
      return false;
  }
  if (std::any_of(typeState.begin(), typeState.end(),
                  [](uint8_t state) { return state != 2; }))
    return false;

  auto getRelativeString = [&](uint32_t relative, std::string_view &result) {
    if (relative == 0 || relative >= database.stringSize)
      return false;
    return getString(database, database.strings + relative, result) &&
           !result.empty();
  };
  auto getRelativeStringAllowEmpty = [&](uint32_t relative,
                                         std::string_view &result) {
    return relative != 0 && relative < database.stringSize &&
           getString(database, database.strings + relative, result);
  };

  uint32_t previousSemanticRootSource = 0;
  std::vector<bool> objectSemanticRoots(database.objectCount, false);
  std::vector<bool> staticObjectSemanticRoots(database.staticObjectCount,
                                              false);
  for (uint64_t index = 0; index != database.semanticRootBindingCount;
       ++index) {
    const uint8_t *binding = database.data + database.semanticRootBindings +
                             index * kSemanticRootBindingSize;
    const uint32_t packedSource = read32(binding);
    const uint32_t semanticType = read32(binding + 4);
    uint64_t sourceOffset = 0;
    if (packedSource == UINT32_MAX ||
        (index != 0 && packedSource <= previousSemanticRootSource) ||
        !packedObjectReferenceOffset(database, packedSource, sourceOffset) ||
        semanticType >= database.semanticTypeCount)
      return false;
    previousSemanticRootSource = packedSource;
    const auto table = obelisk::reflection::unpackTableIndexKind(packedSource);
    const uint32_t sourceIndex =
        obelisk::reflection::unpackTableIndex(packedSource);
    if (table == obelisk::reflection::TableKind::Object)
      objectSemanticRoots[sourceIndex] = true;
    else
      staticObjectSemanticRoots[sourceIndex] = true;
  }

  const uint32_t semanticFlagMask = OBELISK_RT_DESIGN_SEMANTIC_SIGNED |
                                    OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE |
                                    OBELISK_RT_DESIGN_SEMANTIC_HAS_RANGE |
                                    OBELISK_RT_DESIGN_SEMANTIC_TAGGED |
                                    OBELISK_RT_DESIGN_SEMANTIC_SOFT |
                                    OBELISK_RT_DESIGN_SEMANTIC_WILDCARD_INDEX;
  std::vector<uint8_t> semanticState(database.semanticTypeCount, 0);
  for (uint32_t index = 0; index != database.semanticTypeCount; ++index) {
    const uint8_t *type = database.data + database.semanticTypes +
                          uint64_t{index} * kSemanticTypeSize;
    uint32_t encoded = read32(type);
    uint32_t kind = encoded & UINT32_C(0xff);
    uint32_t flags = encoded & semanticFlagMask;
    uint32_t publicVPIKind =
        (encoded & OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_MASK) >>
        OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_SHIFT;
    uint32_t firstEdge = read32(type + 4);
    uint32_t edgeCount = read32(type + 8);
    uint32_t aliasObject = read32(type + 12);
    uint32_t identityTarget = read32(type + 16);
    uint32_t name = read32(type + 20);
    uint32_t modport = read32(type + 24);
    uint32_t queueBound = read32(type + 28);
    int64_t rangeLeft = readI64(type + 32);
    int64_t rangeRight = readI64(type + 40);
    uint64_t bitWidth = read64(type + 48);
    uint64_t tagBits = read64(type + 56);
    std::string_view semanticText;
    bool ranged = kind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY ||
                  kind == OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY ||
                  ((kind == OBELISK_RT_DESIGN_SEMANTIC_BIT ||
                    kind == OBELISK_RT_DESIGN_SEMANTIC_LOGIC ||
                    kind == OBELISK_RT_DESIGN_SEMANTIC_REG) &&
                   rangeLeft != rangeRight);
    bool aggregate = kind >= OBELISK_RT_DESIGN_SEMANTIC_PACKED_STRUCT &&
                     kind <= OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_UNION;
    const auto *publicKind =
        obelisk::reflection::findVPIObjectKind(publicVPIKind);
    bool internalUntypedShape =
        publicVPIKind == 0 &&
        (kind == OBELISK_RT_DESIGN_SEMANTIC_UNTYPED ||
         kind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY ||
         kind == OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY);
    if (kind > OBELISK_RT_DESIGN_SEMANTIC_PROPERTY ||
        (!internalUntypedShape &&
         (!publicKind ||
          publicKind->role != obelisk::reflection::VPIObjectRole::Concrete ||
          (publicKind->families &
           obelisk::reflection::vpiFamilyMask(
               obelisk::reflection::VPIObjectFamily::Typespec)) == 0)) ||
        (encoded & ~(UINT32_C(0xff) | semanticFlagMask |
                     OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_MASK)) != 0 ||
        ((flags & OBELISK_RT_DESIGN_SEMANTIC_HAS_RANGE) != 0) != ranged ||
        firstEdge > database.semanticTypeEdgeCount ||
        edgeCount > database.semanticTypeEdgeCount - firstEdge ||
        (name != 0 && !getRelativeString(name, semanticText)) ||
        (modport != 0 && !getRelativeStringAllowEmpty(modport, semanticText)))
      return false;
    if (identityTarget != UINT32_MAX) {
      uint64_t targetOffset = 0;
      if (!packedObjectReferenceOffset(database, identityTarget,
                                       targetOffset) ||
          (kind != OBELISK_RT_DESIGN_SEMANTIC_CLASS &&
           kind != OBELISK_RT_DESIGN_SEMANTIC_VIRTUAL_INTERFACE))
        return false;
      uint32_t expectedTarget =
          kind == OBELISK_RT_DESIGN_SEMANTIC_CLASS
              ? static_cast<uint32_t>(
                    obelisk::reflection::VPIObjectKind::ClassDefn)
              : static_cast<uint32_t>(
                    obelisk::reflection::VPIObjectKind::InterfaceTypespec);
      if (objectReferenceVPIKind(database, targetOffset) != expectedTarget)
        return false;
      if (kind == OBELISK_RT_DESIGN_SEMANTIC_VIRTUAL_INTERFACE) {
        uint32_t targetRoot = UINT32_MAX;
        if (!findSemanticRootBinding(database, identityTarget, targetRoot) ||
            targetRoot >= database.semanticTypeCount)
          return false;
        const uint8_t *canonical = database.data + database.semanticTypes +
                                   uint64_t{targetRoot} * kSemanticTypeSize;
        uint32_t canonicalEncoded = read32(canonical);
        if ((canonicalEncoded & UINT32_C(0xff)) != kind ||
            (canonicalEncoded &
             OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_MASK) !=
                (encoded & OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_MASK) ||
            read32(canonical + 12) != UINT32_MAX ||
            read32(canonical + 16) != identityTarget ||
            read32(canonical + 20) != name || read32(canonical + 24) != modport)
          return false;
      }
    } else if (kind == OBELISK_RT_DESIGN_SEMANTIC_CLASS ||
               kind == OBELISK_RT_DESIGN_SEMANTIC_VIRTUAL_INTERFACE) {
      return false;
    }
    if ((modport != 0) !=
        (kind == OBELISK_RT_DESIGN_SEMANTIC_VIRTUAL_INTERFACE))
      return false;
    bool tagged = (flags & OBELISK_RT_DESIGN_SEMANTIC_TAGGED) != 0;
    bool soft = (flags & OBELISK_RT_DESIGN_SEMANTIC_SOFT) != 0;
    bool isSigned = (flags & OBELISK_RT_DESIGN_SEMANTIC_SIGNED) != 0;
    bool isFourState = (flags & OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE) != 0;
    bool packedAggregate = kind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_STRUCT ||
                           kind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_UNION;
    bool unionType = kind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_UNION ||
                     kind == OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_UNION;
    if ((tagged && !unionType) ||
        (soft &&
         (kind != OBELISK_RT_DESIGN_SEMANTIC_PACKED_UNION || !tagged)) ||
        (!tagged && tagBits != 0) || (packedAggregate && bitWidth == 0) ||
        (aggregate && !packedAggregate &&
         (bitWidth != 0 || isSigned || soft)) ||
        (!aggregate && (tagged || soft || bitWidth != 0 || tagBits != 0)) ||
        (kind != OBELISK_RT_DESIGN_SEMANTIC_QUEUE && queueBound != 0))
      return false;
    if ((flags & OBELISK_RT_DESIGN_SEMANTIC_WILDCARD_INDEX) != 0 &&
        kind != OBELISK_RT_DESIGN_SEMANTIC_ASSOC_ARRAY)
      return false;
    if (aliasObject != UINT32_MAX) {
      uint64_t aliasOffset = 0;
      if (!packedObjectReferenceOffset(database, aliasObject, aliasOffset))
        return false;
      const uint8_t *alias = database.data + aliasOffset;
      const auto *aliasKind = obelisk::reflection::findVPIObjectKind(
          objectReferenceVPIKind(database, aliasOffset));
      bool namedTypespec =
          isStaticObjectOffset(database, aliasOffset)
              ? read32(alias + 16) != 0
              : recordKind(alias) == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT &&
                    (read32(alias + 4) &
                     OBELISK_RT_DESIGN_CAP_NAMED_TYPESPEC) != 0;
      if (!namedTypespec ||
          objectReferenceVPIKind(database, aliasOffset) != publicVPIKind ||
          !aliasKind ||
          (aliasKind->families &
           obelisk::reflection::vpiFamilyMask(
               obelisk::reflection::VPIObjectFamily::Typespec)) == 0)
        return false;
    }
    uint32_t expectedEdges = 0;
    switch (kind) {
    case OBELISK_RT_DESIGN_SEMANTIC_ENUM:
    case OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY:
    case OBELISK_RT_DESIGN_SEMANTIC_PACKED_OPEN_ARRAY:
    case OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY:
    case OBELISK_RT_DESIGN_SEMANTIC_DYNAMIC_ARRAY:
    case OBELISK_RT_DESIGN_SEMANTIC_QUEUE:
    case OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_OPEN_ARRAY:
    case OBELISK_RT_DESIGN_SEMANTIC_MAILBOX:
      expectedEdges = 1;
      break;
    case OBELISK_RT_DESIGN_SEMANTIC_ASSOC_ARRAY:
      expectedEdges = 2;
      break;
    case OBELISK_RT_DESIGN_SEMANTIC_PACKED_STRUCT:
    case OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_STRUCT:
    case OBELISK_RT_DESIGN_SEMANTIC_PACKED_UNION:
    case OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_UNION:
      if (edgeCount == 0)
        return false;
      expectedEdges = edgeCount;
      break;
    default:
      break;
    }
    if (edgeCount != expectedEdges)
      return false;
    bool childFourState = false;
    for (uint32_t ordinal = 0; ordinal != edgeCount; ++ordinal) {
      const uint8_t *edge =
          database.data + database.semanticTypeEdges +
          uint64_t{firstEdge + ordinal} * kSemanticTypeEdgeSize;
      uint32_t child = read32(edge);
      uint32_t roleAndFlags = read32(edge + 4);
      uint32_t role = roleAndFlags & UINT32_C(0xff);
      uint32_t edgeFlags = roleAndFlags >> 8;
      uint32_t edgeOrdinal = read32(edge + 8);
      uint32_t edgeName = read32(edge + 12);
      uint32_t expectedRole = OBELISK_RT_DESIGN_SEMANTIC_EDGE_ELEMENT;
      if (kind == OBELISK_RT_DESIGN_SEMANTIC_ENUM)
        expectedRole = OBELISK_RT_DESIGN_SEMANTIC_EDGE_ENUM_BASE;
      else if (kind == OBELISK_RT_DESIGN_SEMANTIC_ASSOC_ARRAY && ordinal == 0)
        expectedRole = OBELISK_RT_DESIGN_SEMANTIC_EDGE_ASSOC_INDEX;
      else if (aggregate)
        expectedRole = OBELISK_RT_DESIGN_SEMANTIC_EDGE_MEMBER;
      std::string_view text;
      if (child >= database.semanticTypeCount || role != expectedRole ||
          (aggregate
               ? edgeFlags < OBELISK_RT_DESIGN_SEMANTIC_EDGE_NOT_RANDOM ||
                     edgeFlags > OBELISK_RT_DESIGN_SEMANTIC_EDGE_RANDOM_CYCLIC
               : edgeFlags != 0) ||
          edgeOrdinal != ordinal ||
          (aggregate ? !getRelativeString(edgeName, text) : edgeName != 0) ||
          (!aggregate && read64(edge + 16) != 0))
        return false;
      uint32_t childKind = read32(database.data + database.semanticTypes +
                                  uint64_t{child} * kSemanticTypeSize) &
                           UINT32_C(0xff);
      uint32_t childFlags = read32(database.data + database.semanticTypes +
                                   uint64_t{child} * kSemanticTypeSize) &
                            semanticFlagMask;
      childFourState |=
          (childFlags & OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE) != 0;
      if (kind == OBELISK_RT_DESIGN_SEMANTIC_ASSOC_ARRAY && ordinal == 0 &&
          (((flags & OBELISK_RT_DESIGN_SEMANTIC_WILDCARD_INDEX) != 0) !=
           (childKind == OBELISK_RT_DESIGN_SEMANTIC_UNTYPED)))
        return false;
      if (kind == OBELISK_RT_DESIGN_SEMANTIC_ENUM &&
          ((flags ^ childFlags) & (OBELISK_RT_DESIGN_SEMANTIC_SIGNED |
                                   OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE)) != 0)
        return false;
      if ((kind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY ||
           kind == OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY) &&
          (((flags ^ childFlags) & OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE) !=
               0 ||
           (kind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY
                ? ((flags ^ childFlags) & OBELISK_RT_DESIGN_SEMANTIC_SIGNED) !=
                      0
                : isSigned)))
        return false;
    }
    if (aggregate && childFourState != isFourState)
      return false;
    switch (kind) {
    case OBELISK_RT_DESIGN_SEMANTIC_BIT:
    case OBELISK_RT_DESIGN_SEMANTIC_BYTE:
    case OBELISK_RT_DESIGN_SEMANTIC_SHORT_INT:
    case OBELISK_RT_DESIGN_SEMANTIC_INT:
    case OBELISK_RT_DESIGN_SEMANTIC_LONG_INT:
      if (isFourState)
        return false;
      break;
    case OBELISK_RT_DESIGN_SEMANTIC_LOGIC:
    case OBELISK_RT_DESIGN_SEMANTIC_REG:
    case OBELISK_RT_DESIGN_SEMANTIC_INTEGER:
      if (!isFourState)
        return false;
      break;
    case OBELISK_RT_DESIGN_SEMANTIC_TIME:
      if (isSigned || !isFourState)
        return false;
      break;
    case OBELISK_RT_DESIGN_SEMANTIC_UNKNOWN:
    case OBELISK_RT_DESIGN_SEMANTIC_GENERIC_INTEGRAL:
      break;
    default:
      if (!aggregate && kind != OBELISK_RT_DESIGN_SEMANTIC_ENUM &&
          kind != OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY &&
          kind != OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY &&
          (isSigned || isFourState))
        return false;
      break;
    }
  }
  for (uint32_t index = 0; index != database.objectCount; ++index) {
    const uint8_t *object =
        database.data + database.objects + uint64_t{index} * kObjectSize;
    const auto *kind =
        obelisk::reflection::findVPIObjectKind(recordVPIKind(object));
    bool typespec =
        kind && (kind->families &
                 obelisk::reflection::vpiFamilyMask(
                     obelisk::reflection::VPIObjectFamily::Typespec)) != 0;
    if (typespec && !objectSemanticRoots[index])
      return false;
  }
  for (uint32_t index = 0; index != database.staticObjectCount; ++index) {
    const uint8_t *object = database.data + database.staticObjects +
                            uint64_t{index} * kStaticObjectSize;
    const auto *kind =
        obelisk::reflection::findVPIObjectKind(read16(object + 28));
    bool typespec =
        kind && (kind->families &
                 obelisk::reflection::vpiFamilyMask(
                     obelisk::reflection::VPIObjectFamily::Typespec)) != 0;
    if (typespec && !staticObjectSemanticRoots[index])
      return false;
  }
  struct SemanticWorkItem {
    uint32_t index;
    bool finish;
  };
  auto expectedPublicKind = [](uint32_t kind, uint32_t flags,
                               uint32_t elementKind) -> uint32_t {
    using VPIKind = obelisk::reflection::VPIObjectKind;
    switch (kind) {
    case OBELISK_RT_DESIGN_SEMANTIC_GENERIC_INTEGRAL:
      return static_cast<uint32_t>(
          (flags & OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE) != 0
              ? VPIKind::LogicTypespec
              : VPIKind::BitTypespec);
    case OBELISK_RT_DESIGN_SEMANTIC_BIT:
      return static_cast<uint32_t>(VPIKind::BitTypespec);
    case OBELISK_RT_DESIGN_SEMANTIC_LOGIC:
    case OBELISK_RT_DESIGN_SEMANTIC_REG:
      return static_cast<uint32_t>(VPIKind::LogicTypespec);
    case OBELISK_RT_DESIGN_SEMANTIC_BYTE:
      return static_cast<uint32_t>(VPIKind::ByteTypespec);
    case OBELISK_RT_DESIGN_SEMANTIC_SHORT_INT:
      return static_cast<uint32_t>(VPIKind::ShortIntTypespec);
    case OBELISK_RT_DESIGN_SEMANTIC_INT:
      return static_cast<uint32_t>(VPIKind::IntTypespec);
    case OBELISK_RT_DESIGN_SEMANTIC_LONG_INT:
      return static_cast<uint32_t>(VPIKind::LongIntTypespec);
    case OBELISK_RT_DESIGN_SEMANTIC_INTEGER:
      return static_cast<uint32_t>(VPIKind::IntegerTypespec);
    case OBELISK_RT_DESIGN_SEMANTIC_ENUM:
      return static_cast<uint32_t>(VPIKind::EnumTypespec);
    case OBELISK_RT_DESIGN_SEMANTIC_TIME:
      return static_cast<uint32_t>(VPIKind::TimeTypespec);
    case OBELISK_RT_DESIGN_SEMANTIC_SHORT_REAL:
      return static_cast<uint32_t>(VPIKind::ShortRealTypespec);
    case OBELISK_RT_DESIGN_SEMANTIC_REAL:
    case OBELISK_RT_DESIGN_SEMANTIC_REALTIME:
      return static_cast<uint32_t>(VPIKind::RealTypespec);
    case OBELISK_RT_DESIGN_SEMANTIC_STRING:
      return static_cast<uint32_t>(VPIKind::StringTypespec);
    case OBELISK_RT_DESIGN_SEMANTIC_CHANDLE:
      return static_cast<uint32_t>(VPIKind::ChandleTypespec);
    case OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY:
    case OBELISK_RT_DESIGN_SEMANTIC_PACKED_OPEN_ARRAY:
      return elementKind == static_cast<uint32_t>(VPIKind::EnumTypespec) ||
                     elementKind ==
                         static_cast<uint32_t>(VPIKind::StructTypespec) ||
                     elementKind ==
                         static_cast<uint32_t>(VPIKind::UnionTypespec) ||
                     elementKind ==
                         static_cast<uint32_t>(VPIKind::PackedArrayTypespec)
                 ? static_cast<uint32_t>(VPIKind::PackedArrayTypespec)
                 : elementKind;
    case OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY:
    case OBELISK_RT_DESIGN_SEMANTIC_DYNAMIC_ARRAY:
    case OBELISK_RT_DESIGN_SEMANTIC_QUEUE:
    case OBELISK_RT_DESIGN_SEMANTIC_ASSOC_ARRAY:
    case OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_OPEN_ARRAY:
      return static_cast<uint32_t>(VPIKind::ArrayTypespec);
    case OBELISK_RT_DESIGN_SEMANTIC_PACKED_STRUCT:
    case OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_STRUCT:
      return static_cast<uint32_t>(VPIKind::StructTypespec);
    case OBELISK_RT_DESIGN_SEMANTIC_PACKED_UNION:
    case OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_UNION:
      return static_cast<uint32_t>(VPIKind::UnionTypespec);
    case OBELISK_RT_DESIGN_SEMANTIC_CLASS:
    case OBELISK_RT_DESIGN_SEMANTIC_PROCESS:
    case OBELISK_RT_DESIGN_SEMANTIC_MAILBOX:
    case OBELISK_RT_DESIGN_SEMANTIC_SEMAPHORE:
      return static_cast<uint32_t>(VPIKind::ClassTypespec);
    case OBELISK_RT_DESIGN_SEMANTIC_VIRTUAL_INTERFACE:
      return static_cast<uint32_t>(VPIKind::InterfaceTypespec);
    case OBELISK_RT_DESIGN_SEMANTIC_EVENT:
      return static_cast<uint32_t>(VPIKind::EventTypespec);
    case OBELISK_RT_DESIGN_SEMANTIC_VOID:
      return static_cast<uint32_t>(VPIKind::VoidTypespec);
    case OBELISK_RT_DESIGN_SEMANTIC_SEQUENCE:
      return static_cast<uint32_t>(VPIKind::SequenceTypespec);
    case OBELISK_RT_DESIGN_SEMANTIC_PROPERTY:
      return static_cast<uint32_t>(VPIKind::PropertyTypespec);
    case OBELISK_RT_DESIGN_SEMANTIC_UNTYPED:
      return 0;
    default:
      return 0;
    }
  };
  std::vector<SemanticWorkItem> semanticWorklist;
  for (uint32_t root = 0; root != database.semanticTypeCount; ++root) {
    if (semanticState[root] == 2)
      continue;
    semanticWorklist.push_back({root, false});
    while (!semanticWorklist.empty()) {
      SemanticWorkItem item = semanticWorklist.back();
      semanticWorklist.pop_back();
      if (item.finish) {
        const uint8_t *type = database.data + database.semanticTypes +
                              uint64_t{item.index} * kSemanticTypeSize;
        uint32_t encoded = read32(type);
        uint32_t kind = encoded & UINT32_C(0xff);
        uint32_t flags = encoded & semanticFlagMask;
        uint32_t elementKind = 0;
        if (kind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY ||
            kind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_OPEN_ARRAY) {
          uint32_t firstEdge = read32(type + 4);
          const uint8_t *edge = database.data + database.semanticTypeEdges +
                                uint64_t{firstEdge} * kSemanticTypeEdgeSize;
          const uint8_t *element = database.data + database.semanticTypes +
                                   uint64_t{read32(edge)} * kSemanticTypeSize;
          elementKind = (read32(element) &
                         OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_MASK) >>
                        OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_SHIFT;
        }
        uint32_t actual =
            (encoded & OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_MASK) >>
            OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_SHIFT;
        if (actual != expectedPublicKind(kind, flags, elementKind))
          return false;
        semanticState[item.index] = 2;
        continue;
      }
      if (semanticState[item.index] == 1)
        return false;
      if (semanticState[item.index] == 2)
        continue;
      semanticState[item.index] = 1;
      semanticWorklist.push_back({item.index, true});
      const uint8_t *type = database.data + database.semanticTypes +
                            uint64_t{item.index} * kSemanticTypeSize;
      uint32_t firstEdge = read32(type + 4);
      uint32_t edgeCount = read32(type + 8);
      for (uint32_t ordinal = edgeCount; ordinal != 0; --ordinal) {
        const uint8_t *edge =
            database.data + database.semanticTypeEdges +
            uint64_t{firstEdge + ordinal - 1} * kSemanticTypeEdgeSize;
        semanticWorklist.push_back({read32(edge), false});
      }
    }
  }

  // A claimed directly declared parameter range must resolve to the exact
  // packed-array semantic root whose endpoints answer vpiLeftRange and
  // vpiRightRange. Validate the compact and promoted full-record forms here,
  // after the root-binding and semantic graphs are known to be well formed.
  auto hasValidExplicitParameterRange =
      [&](obelisk::reflection::TableKind table, uint32_t index) {
        uint32_t packedSource = 0;
        uint32_t semanticRoot = 0;
        if (!obelisk::reflection::tryPackTableIndex(table, index,
                                                    packedSource) ||
            !findSemanticRootBinding(database, packedSource, semanticRoot))
          return false;
        const uint8_t *semantic = database.data + database.semanticTypes +
                                  uint64_t{semanticRoot} * kSemanticTypeSize;
        uint32_t encoded = read32(semantic);
        return (encoded & UINT32_C(0xff)) ==
                   OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY &&
               (encoded & OBELISK_RT_DESIGN_SEMANTIC_HAS_RANGE) != 0;
      };
  for (uint32_t index = 0; index != database.objectCount; ++index) {
    const uint8_t *object =
        database.data + database.objects + uint64_t{index} * kObjectSize;
    if (recordKind(object) == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT &&
        recordVPIKind(object) ==
            static_cast<uint32_t>(
                obelisk::reflection::VPIObjectKind::Parameter) &&
        (read32(object + 4) & OBELISK_RT_DESIGN_CAP_PARAMETER_EXPLICIT_RANGE) !=
            0 &&
        !hasValidExplicitParameterRange(obelisk::reflection::TableKind::Object,
                                        index))
      return false;
  }
  for (uint32_t index = 0; index != database.staticObjectCount; ++index) {
    const uint8_t *object = database.data + database.staticObjects +
                            uint64_t{index} * kStaticObjectSize;
    if ((read16(object + 30) &
         OBELISK_RT_DESIGN_CAP_PARAMETER_EXPLICIT_RANGE) != 0 &&
        !hasValidExplicitParameterRange(
            obelisk::reflection::TableKind::StaticObject, index))
      return false;
  }

  for (uint32_t index = 0; index != database.semanticRootBindingCount;
       ++index) {
    const uint8_t *binding = database.data + database.semanticRootBindings +
                             uint64_t{index} * kSemanticRootBindingSize;
    uint64_t objectOffset = 0;
    if (!packedObjectReferenceOffset(database, read32(binding), objectOffset))
      return false;
    uint32_t root = read32(binding + 4);
    const auto *kind = obelisk::reflection::findVPIObjectKind(
        objectReferenceVPIKind(database, objectOffset));
    if (!kind || (kind->families &
                  obelisk::reflection::vpiFamilyMask(
                      obelisk::reflection::VPIObjectFamily::Typespec)) == 0)
      continue;
    uint32_t rootKind = (read32(database.data + database.semanticTypes +
                                uint64_t{root} * kSemanticTypeSize) &
                         OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_MASK) >>
                        OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_SHIFT;
    if (rootKind != objectReferenceVPIKind(database, objectOffset))
      return false;
  }

  // Validate the hash-consed immutable value pool once. Query-time lookup is
  // then one binary search over sparse source bindings and one indexed load.
  std::vector<uint8_t> frozenValueReferenced(database.frozenValueCount, 0);
  std::unordered_map<uint64_t, std::vector<uint32_t>> frozenValueKeys;
  uint64_t nextFrozenPayload = 0;
  for (uint64_t index = 0; index != database.frozenValueCount; ++index) {
    const uint8_t *record =
        database.data + database.frozenValues + index * kFrozenValueSize;
    uint32_t kindAndFlags = read32(record);
    uint32_t reserved = read32(record + 4);
    uint64_t bitWidth = read64(record + 8);
    uint64_t payloadOffset = read64(record + 16);
    uint64_t payloadSize = read64(record + 24);
    if (reserved != 0 ||
        (kindAndFlags & obelisk::reflection::frozenValueKindMask) !=
            static_cast<uint32_t>(
                obelisk::reflection::FrozenValueKind::Packed) ||
        (kindAndFlags & ~(obelisk::reflection::frozenValueKindMask |
                          obelisk::reflection::frozenValueSigned |
                          obelisk::reflection::frozenValueFourState)) != 0 ||
        bitWidth == 0 || bitWidth > UINT64_MAX - 7)
      return false;
    uint64_t planeBytes = (bitWidth + 7) / 8;
    if (planeBytes > UINT64_MAX / 2 || payloadSize != planeBytes * 2 ||
        payloadOffset != nextFrozenPayload ||
        !validRange(payloadOffset, payloadSize, 1,
                    database.frozenValuePayloadSize))
      return false;
    nextFrozenPayload += payloadSize;
    const uint8_t *payload =
        database.data + database.frozenValuePayload + payloadOffset;
    bool anyUnknown = false;
    for (uint64_t byte = 0; byte != planeBytes; ++byte)
      anyUnknown |= payload[planeBytes + byte] != 0;
    bool fourState =
        (kindAndFlags & obelisk::reflection::frozenValueFourState) != 0;
    if (anyUnknown && !fourState)
      return false;
    unsigned tail = static_cast<unsigned>(bitWidth % 8);
    if (tail != 0) {
      uint8_t paddingMask = static_cast<uint8_t>(~((uint16_t{1} << tail) - 1));
      if ((payload[planeBytes - 1] & paddingMask) != 0 ||
          (payload[payloadSize - 1] & paddingMask) != 0)
        return false;
    }
    uint64_t key = nameHash(payload, payloadSize) ^
                   (uint64_t{kindAndFlags} << 32) ^ bitWidth;
    std::vector<uint32_t> &bucket = frozenValueKeys[key];
    for (uint32_t candidate : bucket) {
      const uint8_t *other = database.data + database.frozenValues +
                             uint64_t{candidate} * kFrozenValueSize;
      if (read32(other) == kindAndFlags && read64(other + 8) == bitWidth &&
          read64(other + 24) == payloadSize &&
          std::memcmp(database.data + database.frozenValuePayload +
                          read64(other + 16),
                      payload, payloadSize) == 0)
        return false;
    }
    bucket.push_back(static_cast<uint32_t>(index));
  }
  if (nextFrozenPayload != database.frozenValuePayloadSize)
    return false;
  uint32_t previousFrozenSource = 0;
  for (uint64_t index = 0; index != database.frozenValueBindingCount; ++index) {
    const uint8_t *binding = database.data + database.frozenValueBindings +
                             index * kFrozenValueBindingSize;
    uint32_t packedSource = read32(binding);
    uint32_t valueIndex = read32(binding + 4);
    uint64_t objectOffset = 0;
    if (packedSource == UINT32_MAX ||
        (index != 0 && packedSource <= previousFrozenSource) ||
        !packedObjectReferenceOffset(database, packedSource, objectOffset) ||
        objectReferenceVPIKind(database, objectOffset) !=
            static_cast<uint32_t>(
                obelisk::reflection::VPIObjectKind::Parameter) ||
        valueIndex >= database.frozenValueCount)
      return false;
    previousFrozenSource = packedSource;
    frozenValueReferenced[valueIndex] = 1;
    const uint8_t *record = database.data + database.frozenValues +
                            uint64_t{valueIndex} * kFrozenValueSize;
    uint32_t kindAndFlags = read32(record);
    uint32_t semanticRoot = 0;
    if (!findSemanticRootBinding(database, packedSource, semanticRoot))
      return false;
    const uint8_t *semantic = database.data + database.semanticTypes +
                              uint64_t{semanticRoot} * kSemanticTypeSize;
    uint32_t semanticFlags = read32(semantic);
    uint64_t semanticWidth = 0;
    if (!semanticBitWidth(database, semanticRoot, semanticWidth) ||
        semanticWidth != read64(record + 8) ||
        ((semanticFlags & OBELISK_RT_DESIGN_SEMANTIC_SIGNED) != 0) !=
            ((kindAndFlags & obelisk::reflection::frozenValueSigned) != 0) ||
        ((semanticFlags & OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE) != 0) !=
            ((kindAndFlags & obelisk::reflection::frozenValueFourState) != 0))
      return false;
  }
  if (std::find(frozenValueReferenced.begin(), frozenValueReferenced.end(),
                uint8_t{0}) != frozenValueReferenced.end())
    return false;
  for (uint32_t index = 0; index != database.staticObjectCount; ++index) {
    const uint8_t *object = database.data + database.staticObjects +
                            uint64_t{index} * kStaticObjectSize;
    if (!obelisk::reflection::findVPIValuePolicy(read16(object + 28)))
      continue;
    uint32_t packedSource = 0;
    VPIFrozenValue value{};
    if (!obelisk::reflection::tryPackTableIndex(
            obelisk::reflection::TableKind::StaticObject, index,
            packedSource) ||
        !findFrozenValue(database, packedSource, value))
      return false;
  }
  std::vector<uint8_t> parentState(database.statementCount, 0);
  std::vector<uint8_t> siteMasks(database.statementCount, 0);
  uint64_t previousStatementID = 0;
  for (uint64_t index = 0; index != database.statementCount; ++index) {
    const uint8_t *statement =
        database.data + database.statements + index * kStatementSize;
    uint64_t id = read64(statement);
    uint32_t ownerIndex = read32(statement + 8);
    uint32_t scopeIndex = read32(statement + 12);
    uint32_t parentIndex = read32(statement + 16);
    uint32_t sourceFile = read32(statement + 20);
    uint32_t name = read32(statement + 24);
    uint32_t line = read32(statement + 28);
    uint32_t column = read32(statement + 32);
    uint16_t vpiKind = read16(statement + 36);
    uint16_t flags = read16(statement + 38);
    if (id == 0 || (index != 0 && id <= previousStatementID) ||
        scopeIndex >= database.scopeCount ||
        (parentIndex != UINT32_MAX && parentIndex >= database.statementCount) ||
        (flags & ~(OBELISK_RT_DESIGN_STATEMENT_PROTECTED |
                   OBELISK_RT_DESIGN_STATEMENT_SCOPE)) != 0)
      return false;
    previousStatementID = id;
    const auto *kind = obelisk::reflection::findVPIObjectKind(vpiKind);
    if (!kind || kind->role != obelisk::reflection::VPIObjectRole::Concrete ||
        (kind->families &
         obelisk::reflection::vpiFamilyMask(
             obelisk::reflection::VPIObjectFamily::Statement)) == 0)
      return false;
    bool scopeOwned =
        (kind->families &
         obelisk::reflection::vpiFamilyMask(
             obelisk::reflection::VPIObjectFamily::ScopeOwnedStatement)) != 0;
    if (scopeOwned != (ownerIndex == UINT32_MAX))
      return false;
    bool scopeCapable = (kind->families &
                         obelisk::reflection::vpiFamilyMask(
                             obelisk::reflection::VPIObjectFamily::Scope)) != 0;
    bool isScope = (flags & OBELISK_RT_DESIGN_STATEMENT_SCOPE) != 0;
    std::string_view kindName(kind->apiName);
    bool namedScope = kindName == "vpiNamedBegin" || kindName == "vpiNamedFork";
    bool requiresScope =
        namedScope ||
        vpiKind == static_cast<uint16_t>(
                       obelisk::reflection::VPIObjectKind::ForeachStmt);
    if ((isScope && !scopeCapable) || (requiresScope && !isScope))
      return false;
    if (!scopeOwned) {
      if (ownerIndex >= database.objectCount)
        return false;
      const uint8_t *owner =
          database.data + database.objects + uint64_t{ownerIndex} * kObjectSize;
      uint32_t ownerKind = recordKind(owner);
      if (ownerKind != OBELISK_RT_DESIGN_RECORD_PROCESS &&
          ownerKind != OBELISK_RT_DESIGN_RECORD_FUNCTION)
        return false;
      if (read64(owner + 16) !=
          database.scopes + uint64_t{scopeIndex} * kScopeSize)
        return false;
    }
    if ((sourceFile == 0 && (line != 0 || column != 0)) ||
        (sourceFile != 0 && (line == 0 || column == 0)))
      return false;
    std::string_view text;
    if (sourceFile != 0 && !getRelativeString(sourceFile, text))
      return false;
    if (name != 0 && !getRelativeString(name, text))
      return false;
    bool named = kind && (std::string_view(kind->apiName) == "vpiNamedBegin" ||
                          std::string_view(kind->apiName) == "vpiNamedFork");
    if (named != (name != 0))
      return false;
  }
  struct ParentWorkItem {
    uint32_t index;
    bool finish;
  };
  std::vector<ParentWorkItem> parentWorklist;
  auto validateParent = [&](uint32_t root) {
    parentWorklist.clear();
    parentWorklist.push_back({root, false});
    while (!parentWorklist.empty()) {
      ParentWorkItem item = parentWorklist.back();
      parentWorklist.pop_back();
      if (item.finish) {
        parentState[item.index] = 2;
        continue;
      }
      if (parentState[item.index] == 1)
        return false;
      if (parentState[item.index] == 2)
        continue;
      parentState[item.index] = 1;
      parentWorklist.push_back({item.index, true});
      const uint8_t *statement = database.data + database.statements +
                                 uint64_t{item.index} * kStatementSize;
      uint32_t parent = read32(statement + 16);
      if (parent == UINT32_MAX)
        continue;
      const uint8_t *parentStatement = database.data + database.statements +
                                       uint64_t{parent} * kStatementSize;
      if (read32(parentStatement + 8) != read32(statement + 8) ||
          read32(parentStatement + 12) != read32(statement + 12))
        return false;
      parentWorklist.push_back({parent, false});
    }
    return true;
  };
  for (uint32_t index = 0; index != database.statementCount; ++index)
    if (parentState[index] != 2 && !validateParent(index))
      return false;

  uint64_t previousStaticObjectID = 0;
  for (uint64_t index = 0; index != database.staticObjectCount; ++index) {
    const uint8_t *object =
        database.data + database.staticObjects + index * kStaticObjectSize;
    uint64_t id = read64(object);
    uint32_t scopeIndex = read32(object + 8);
    uint32_t sourceFile = read32(object + 12);
    uint32_t name = read32(object + 16);
    uint32_t line = read32(object + 20);
    uint32_t column = read32(object + 24);
    uint16_t vpiKind = read16(object + 28);
    uint16_t flags = read16(object + 30);
    bool explicitParameterRange =
        vpiKind == static_cast<uint32_t>(
                       obelisk::reflection::VPIObjectKind::Parameter) &&
        flags == OBELISK_RT_DESIGN_CAP_PARAMETER_EXPLICIT_RANGE;
    if ((index != 0 && id <= previousStaticObjectID) ||
        (scopeIndex != UINT32_MAX && scopeIndex >= database.scopeCount) ||
        (flags != 0 && !explicitParameterRange) ||
        (sourceFile == 0 && (line != 0 || column != 0)) ||
        (sourceFile != 0 && (line == 0 || column == 0)))
      return false;
    previousStaticObjectID = id;
    const auto *kind = obelisk::reflection::findVPIObjectKind(vpiKind);
    if (!kind ||
        !obelisk::reflection::hasVPIObjectRepresentation(
            vpiKind, obelisk::reflection::VPIObjectRepresentation::StaticImage))
      return false;
    std::string_view text;
    if ((sourceFile != 0 && !getRelativeString(sourceFile, text)) ||
        (name != 0 && !getRelativeString(name, text)))
      return false;
  }

  std::vector<bool> boundDefinitions(database.definitionCount, false);
  uint32_t nextDefinitionMember = 0;
  uint32_t nextDefinitionMemberRelation = 0;
  uint32_t nextDefinitionMemberRelationTarget = 0;
  for (uint64_t index = 0; index != database.definitionCount; ++index) {
    const uint8_t *definition =
        database.data + database.definitions + index * kDefinitionSize;
    uint16_t kind = read16(definition);
    uint16_t flags = read16(definition + 2);
    uint32_t name = read32(definition + 4);
    uint32_t file = read32(definition + 8);
    uint32_t line = read32(definition + 12);
    uint32_t firstMember = read32(definition + 16);
    uint32_t memberCount = read32(definition + 20);
    uint32_t firstRelation = read32(definition + 24);
    uint32_t relationCount = read32(definition + 28);
    using Kind = obelisk::reflection::VPIObjectKind;
    if ((kind != static_cast<uint16_t>(Kind::Module) &&
         kind != static_cast<uint16_t>(Kind::Interface) &&
         kind != static_cast<uint16_t>(Kind::Program)) ||
        flags != 0 || name == 0 || line > INT32_MAX ||
        (file == 0) != (line == 0) ||
        (memberCount != 0 && firstMember != nextDefinitionMember) ||
        firstMember > database.definitionMemberCount ||
        memberCount > database.definitionMemberCount - firstMember ||
        firstRelation != nextDefinitionMemberRelation ||
        firstRelation > database.definitionMemberRelationCount ||
        relationCount > database.definitionMemberRelationCount - firstRelation)
      return false;
    std::string_view text;
    if (!getRelativeString(name, text) || text.empty() ||
        (file != 0 && !getRelativeString(file, text)))
      return false;

    for (uint32_t ordinal = 0; ordinal != memberCount; ++ordinal) {
      const uint8_t *member =
          database.data + database.definitionMembers +
          uint64_t{firstMember + ordinal} * kDefinitionMemberSize;
      uint32_t memberName = read32(member);
      uint32_t memberFile = read32(member + 4);
      uint32_t memberLine = read32(member + 8);
      uint32_t memberColumn = read32(member + 12);
      uint16_t memberKind = read16(member + 16);
      uint16_t memberFlags = read16(member + 18);
      const auto *descriptor =
          obelisk::reflection::findVPIObjectKind(memberKind);
      if (!descriptor ||
          descriptor->role != obelisk::reflection::VPIObjectRole::Concrete ||
          memberName == 0 || memberLine > INT32_MAX ||
          (memberFile == 0 ? memberLine != 0 || memberColumn != 0
                           : memberLine == 0 || memberColumn == 0) ||
          (memberKind == static_cast<uint16_t>(Kind::IODecl)
               ? memberFlags > 6
               : memberFlags != 0) ||
          !getRelativeString(memberName, text) ||
          (memberFile != 0 && !getRelativeString(memberFile, text)))
        return false;
    }

    uint16_t previousSelector = 0;
    for (uint32_t ordinal = 0; ordinal != relationCount; ++ordinal) {
      const uint8_t *relation =
          database.data + database.definitionMemberRelations +
          uint64_t{firstRelation + ordinal} * kDefinitionMemberRelationSize;
      uint16_t selector = read16(relation);
      uint16_t relationFlags = read16(relation + 2);
      uint32_t firstTarget = read32(relation + 4);
      uint32_t targetCount = read32(relation + 8);
      auto *edge = obelisk::reflection::findVPITraversal(
          kind, selector, obelisk::reflection::VPITraversalMode::Iterate);
      if (relationFlags != 0 || targetCount == 0 ||
          (ordinal != 0 && selector <= previousSelector) ||
          firstTarget != nextDefinitionMemberRelationTarget ||
          firstTarget > database.definitionMemberRelationTargetCount ||
          targetCount >
              database.definitionMemberRelationTargetCount - firstTarget ||
          !edge ||
          edge->automaticRelation !=
              obelisk::reflection::VPIAutomaticRelation::DefinitionMember)
        return false;
      previousSelector = selector;
      uint32_t previousTarget = 0;
      for (uint32_t targetOrdinal = 0; targetOrdinal != targetCount;
           ++targetOrdinal) {
        const uint8_t *target = database.data +
                                database.definitionMemberRelationTargets +
                                uint64_t{firstTarget + targetOrdinal} *
                                    kDefinitionMemberRelationTargetSize;
        uint32_t memberIndex = read32(target);
        if (memberIndex < firstMember ||
            memberIndex - firstMember >= memberCount ||
            (targetOrdinal != 0 && memberIndex <= previousTarget))
          return false;
        const uint8_t *member = database.data + database.definitionMembers +
                                uint64_t{memberIndex} * kDefinitionMemberSize;
        if (!obelisk::reflection::vpiObjectSetContains(edge->targets,
                                                       read16(member + 16)))
          return false;
        previousTarget = memberIndex;
      }
      nextDefinitionMemberRelationTarget += targetCount;
    }
    nextDefinitionMember += memberCount;
    nextDefinitionMemberRelation += relationCount;
  }
  if (nextDefinitionMember != database.definitionMemberCount ||
      nextDefinitionMemberRelation != database.definitionMemberRelationCount ||
      nextDefinitionMemberRelationTarget !=
          database.definitionMemberRelationTargetCount)
    return false;

  uint32_t nextSpecializationBinding = 0;
  for (uint32_t index = 0; index != database.definitionSpecializationCount;
       ++index) {
    const uint8_t *specialization =
        database.data + database.definitionSpecializations +
        uint64_t{index} * kDefinitionSpecializationSize;
    uint32_t definitionIndex = read32(specialization);
    uint32_t firstBinding = read32(specialization + 4);
    uint32_t bindingCount = read32(specialization + 8);
    if (definitionIndex >= database.definitionCount ||
        firstBinding != nextSpecializationBinding ||
        firstBinding > database.definitionSpecializationBindingCount ||
        bindingCount >
            database.definitionSpecializationBindingCount - firstBinding)
      return false;
    const uint8_t *definition = database.data + database.definitions +
                                uint64_t{definitionIndex} * kDefinitionSize;
    uint32_t firstMember = read32(definition + 16);
    uint32_t memberCount = read32(definition + 20);
    if (bindingCount != memberCount)
      return false;
    uint32_t previousMember = 0;
    for (uint32_t ordinal = 0; ordinal != bindingCount; ++ordinal) {
      const uint8_t *binding = database.data +
                               database.definitionSpecializationBindings +
                               uint64_t{firstBinding + ordinal} *
                                   kDefinitionSpecializationBindingSize;
      uint32_t memberIndex = read32(binding);
      uint32_t semanticType = read32(binding + 4);
      if (memberIndex < firstMember ||
          memberIndex - firstMember >= memberCount ||
          semanticType >= database.semanticTypeCount ||
          (ordinal != 0 && memberIndex <= previousMember))
        return false;
      previousMember = memberIndex;
    }
    nextSpecializationBinding += bindingCount;
  }
  if (nextSpecializationBinding !=
      database.definitionSpecializationBindingCount)
    return false;

  uint32_t previousDefinitionSource = 0;
  uint32_t nextDefinitionMemberEndpoint = 0;
  uint64_t fixedPropertyIndex = 0;
  for (uint64_t index = 0; index != database.definitionBindingCount; ++index) {
    const uint8_t *binding = database.data + database.definitionBindings +
                             index * kDefinitionBindingSize;
    uint32_t packedSource = read32(binding);
    uint32_t definitionIndex = read32(binding + 4);
    uint32_t specializationIndex = read32(binding + 8);
    uint32_t firstMemberEndpoint = read32(binding + 12);
    auto table = obelisk::reflection::unpackTableIndexKind(packedSource);
    uint32_t sourceIndex = obelisk::reflection::unpackTableIndex(packedSource);
    if ((index != 0 && packedSource <= previousDefinitionSource) ||
        table != obelisk::reflection::TableKind::Scope ||
        sourceIndex >= database.scopeCount ||
        definitionIndex >= database.definitionCount ||
        firstMemberEndpoint != nextDefinitionMemberEndpoint ||
        (specializationIndex != UINT32_MAX &&
         specializationIndex >= database.definitionSpecializationCount))
      return false;
    previousDefinitionSource = packedSource;
    const uint8_t *scope =
        database.data + database.scopes + uint64_t{sourceIndex} * kScopeSize;
    const uint8_t *definition = database.data + database.definitions +
                                uint64_t{definitionIndex} * kDefinitionSize;
    uint32_t memberCount = read32(definition + 20);
    if (firstMemberEndpoint > database.definitionMemberEndpointCount ||
        memberCount >
            database.definitionMemberEndpointCount - firstMemberEndpoint)
      return false;
    if (recordVPIKind(scope) != read16(definition))
      return false;
    if (specializationIndex == UINT32_MAX && read32(definition + 20) != 0)
      return false;
    if (specializationIndex != UINT32_MAX) {
      const uint8_t *specialization =
          database.data + database.definitionSpecializations +
          uint64_t{specializationIndex} * kDefinitionSpecializationSize;
      if (read32(specialization) != definitionIndex)
        return false;
    }
    const auto *exprEdge = obelisk::reflection::findVPITraversal(
        static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::IODecl),
        static_cast<uint32_t>(obelisk::reflection::VPIRelationKind::ExprRel),
        obelisk::reflection::VPITraversalMode::Handle);
    const auto *refActualEdge = obelisk::reflection::findVPITraversal(
        static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::RefObj),
        static_cast<uint32_t>(obelisk::reflection::VPIRelationKind::ActualRel),
        obelisk::reflection::VPITraversalMode::Handle);
    for (uint32_t ordinal = 0; ordinal != memberCount; ++ordinal) {
      const uint8_t *endpoint = database.data +
                                database.definitionMemberEndpoints +
                                uint64_t{firstMemberEndpoint + ordinal} *
                                    kDefinitionMemberEndpointSize;
      uint32_t packedTarget = read32(endpoint);
      if (packedTarget == UINT32_MAX)
        continue;
      auto targetTable =
          obelisk::reflection::unpackTableIndexKind(packedTarget);
      uint64_t targetOffset = 0;
      if (!exprEdge || targetTable != obelisk::reflection::TableKind::Object ||
          !packedObjectReferenceOffset(database, packedTarget, targetOffset))
        return false;
      const uint8_t *target = database.data + targetOffset;
      uint32_t targetRecordKind = recordKind(target);
      if (targetRecordKind != OBELISK_RT_DESIGN_RECORD_STORAGE &&
          targetRecordKind != OBELISK_RT_DESIGN_RECORD_NET)
        return false;
      uint32_t targetType = recordVPIKind(target);
      const uint8_t *member =
          database.data + database.definitionMembers +
          uint64_t{read32(definition + 16) + ordinal} * kDefinitionMemberSize;
      uint16_t direction = read16(member + 18);
      if (direction == kVPIIODirectionRef) {
        uint32_t memberSemanticType = 0;
        uint64_t memberBitWidth = 0;
        if (targetRecordKind != OBELISK_RT_DESIGN_RECORD_STORAGE ||
            !refActualEdge ||
            !obelisk::reflection::vpiObjectSetContains(
                exprEdge->targets,
                static_cast<uint32_t>(
                    obelisk::reflection::VPIObjectKind::RefObj)) ||
            !obelisk::reflection::vpiObjectSetContains(refActualEdge->targets,
                                                       targetType) ||
            !findDefinitionMemberSemanticRoot(database, specializationIndex,
                                              read32(definition + 16) + ordinal,
                                              memberSemanticType) ||
            !semanticBitWidth(database, memberSemanticType, memberBitWidth) ||
            (memberBitWidth != 0 && read64(target + 56) != memberBitWidth))
          return false;
      } else if (!obelisk::reflection::vpiObjectSetContains(exprEdge->targets,
                                                            targetType)) {
        return false;
      }
      if (direction != kVPIIODirectionRef &&
          read64(target + 16) !=
              database.scopes + uint64_t{sourceIndex} * kScopeSize)
        return false;
      bool virtualInterface =
          targetType ==
          static_cast<uint32_t>(
              obelisk::reflection::VPIObjectKind::VirtualInterfaceVar);
      if (virtualInterface ? direction != 0 && direction != kVPIIODirectionRef
                           : (direction < 1 || direction > 3) &&
                                 direction != kVPIIODirectionRef)
        return false;
    }
    nextDefinitionMemberEndpoint += memberCount;
    while (fixedPropertyIndex != database.fixedPropertyCount) {
      const uint8_t *property = database.data + database.fixedProperties +
                                fixedPropertyIndex * kFixedPropertySize;
      uint32_t propertySource = read32(property);
      if (propertySource < packedSource) {
        ++fixedPropertyIndex;
        continue;
      }
      if (propertySource != packedSource)
        break;
      uint16_t selector = read16(property + 4);
      if (selector == 9 || selector == 15 || selector == 16)
        return false;
      ++fixedPropertyIndex;
    }
    boundDefinitions[definitionIndex] = true;
  }
  if (database.definitionCount != 0 &&
      std::find(boundDefinitions.begin(), boundDefinitions.end(), false) !=
          boundDefinitions.end())
    return false;
  if (nextDefinitionMemberEndpoint != database.definitionMemberEndpointCount)
    return false;

  uint32_t nextMemberInstanceTarget = 0;
  uint32_t previousBinding = 0;
  uint32_t previousMemberAndFlags = 0;
  uint16_t previousInstanceSelector = 0;
  uint16_t previousInstanceMode = 0;
  std::unordered_set<uint32_t> memberInstanceTargets;
  for (uint32_t index = 0;
       index != database.definitionMemberInstanceRelationCount; ++index) {
    const uint8_t *relation =
        database.data + database.definitionMemberInstanceRelations +
        uint64_t{index} * kDefinitionMemberInstanceRelationSize;
    uint32_t bindingIndex = read32(relation);
    uint32_t memberAndFlags = read32(relation + 4);
    uint16_t selector = read16(relation + 8);
    uint16_t modeAndFlags = read16(relation + 10);
    uint32_t firstTarget = read32(relation + 12);
    uint32_t targetCount = read32(relation + 16);
    bool ordered = index == 0 || previousBinding < bindingIndex ||
                   (previousBinding == bindingIndex &&
                    (previousMemberAndFlags < memberAndFlags ||
                     (previousMemberAndFlags == memberAndFlags &&
                      (previousInstanceSelector < selector ||
                       (previousInstanceSelector == selector &&
                        previousInstanceMode < modeAndFlags)))));
    if (!ordered || bindingIndex >= database.definitionBindingCount ||
        (memberAndFlags & kVirtualRefObjectFlag) == 0 ||
        (memberAndFlags & ~(kVirtualRefObjectFlag - 1)) !=
            kVirtualRefObjectFlag ||
        modeAndFlags > 1 || firstTarget != nextMemberInstanceTarget ||
        targetCount == 0 ||
        firstTarget > database.definitionMemberInstanceRelationTargetCount ||
        targetCount >
            database.definitionMemberInstanceRelationTargetCount - firstTarget)
      return false;
    uint32_t memberIndex = memberAndFlags & ~kVirtualRefObjectFlag;
    const uint8_t *binding = database.data + database.definitionBindings +
                             uint64_t{bindingIndex} * kDefinitionBindingSize;
    uint32_t packedSource = read32(binding);
    uint32_t sourceScopeIndex =
        obelisk::reflection::unpackTableIndex(packedSource);
    if (obelisk::reflection::unpackTableIndexKind(packedSource) !=
            obelisk::reflection::TableKind::Scope ||
        sourceScopeIndex >= database.scopeCount)
      return false;
    uint64_t sourceScopeOffset =
        database.scopes + uint64_t{sourceScopeIndex} * kScopeSize;
    uint32_t definitionIndex = read32(binding + 4);
    if (definitionIndex >= database.definitionCount)
      return false;
    const uint8_t *definition = database.data + database.definitions +
                                uint64_t{definitionIndex} * kDefinitionSize;
    uint32_t firstMember = read32(definition + 16);
    uint32_t memberCount = read32(definition + 20);
    if (memberIndex < firstMember || memberIndex - firstMember >= memberCount)
      return false;
    uint32_t endpointIndex = read32(binding + 12) + (memberIndex - firstMember);
    if (endpointIndex >= database.definitionMemberEndpointCount)
      return false;
    const uint8_t *endpoint =
        database.data + database.definitionMemberEndpoints +
        uint64_t{endpointIndex} * kDefinitionMemberEndpointSize;
    if (read32(endpoint) == UINT32_MAX)
      return false;
    const uint8_t *member = database.data + database.definitionMembers +
                            uint64_t{memberIndex} * kDefinitionMemberSize;
    const auto *edge = obelisk::reflection::findVPITraversal(
        static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::RefObj),
        selector,
        modeAndFlags == 0 ? obelisk::reflection::VPITraversalMode::Handle
                          : obelisk::reflection::VPITraversalMode::Iterate);
    if (read16(member + 16) !=
            static_cast<uint16_t>(obelisk::reflection::VPIObjectKind::IODecl) ||
        read16(member + 18) != kVPIIODirectionRef || !edge ||
        edge->automaticRelation != obelisk::reflection::VPIAutomaticRelation::
                                       DefinitionMemberInstanceRelation)
      return false;
    memberInstanceTargets.clear();
    memberInstanceTargets.reserve(targetCount);
    for (uint32_t ordinal = 0; ordinal != targetCount; ++ordinal) {
      const uint8_t *target = database.data +
                              database.definitionMemberInstanceRelationTargets +
                              uint64_t{firstTarget + ordinal} *
                                  kDefinitionMemberInstanceRelationTargetSize;
      uint32_t packedTarget = read32(target);
      uint64_t targetOffset = 0;
      if (obelisk::reflection::unpackTableIndexKind(packedTarget) !=
              obelisk::reflection::TableKind::Object ||
          !packedObjectReferenceOffset(database, packedTarget, targetOffset))
        return false;
      const uint8_t *object = database.data + targetOffset;
      if (recordKind(object) != OBELISK_RT_DESIGN_RECORD_PORT ||
          recordVPIKind(object) !=
              static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::Port) ||
          !obelisk::reflection::vpiObjectSetContains(edge->targets,
                                                     recordVPIKind(object)))
        return false;
      uint64_t targetScopeOffset = read64(object + 16);
      if (selector ==
          static_cast<uint16_t>(obelisk::reflection::VPIObjectKind::Port)) {
        if (targetScopeOffset != sourceScopeOffset)
          return false;
      } else if (selector ==
                 static_cast<uint16_t>(
                     obelisk::reflection::VPIRelationKind::PortInstRel)) {
        if (targetScopeOffset == sourceScopeOffset)
          return false;
        bool descendant = false;
        uint64_t current = targetScopeOffset;
        for (uint64_t depth = 0; depth != database.scopeCount; ++depth) {
          if (!isScopeOffset(database, current))
            return false;
          current = read64(database.data + current + 16);
          if (current == sourceScopeOffset) {
            descendant = true;
            break;
          }
          if (current == 0)
            break;
        }
        if (!descendant)
          return false;
      }
      if (!memberInstanceTargets.insert(packedTarget).second)
        return false;
    }
    previousBinding = bindingIndex;
    previousMemberAndFlags = memberAndFlags;
    previousInstanceSelector = selector;
    previousInstanceMode = modeAndFlags;
    nextMemberInstanceTarget += targetCount;
  }
  if (nextMemberInstanceTarget !=
      database.definitionMemberInstanceRelationTargetCount)
    return false;
  if (database.definitionMemberInstanceRelationInverseCount !=
      database.definitionMemberInstanceRelationTargetCount)
    return false;
  std::vector<uint8_t> inverseTargets(
      database.definitionMemberInstanceRelationTargetCount, 0);
  std::unordered_set<uint64_t> inverseConnectionKeys;
  uint32_t previousInversePort = 0;
  uint32_t previousInverseTarget = 0;
  for (uint32_t index = 0;
       index != database.definitionMemberInstanceRelationInverseCount;
       ++index) {
    const uint8_t *inverse =
        database.data + database.definitionMemberInstanceRelationInverses +
        uint64_t{index} * kDefinitionMemberInstanceRelationInverseSize;
    uint32_t targetIndex = read32(inverse);
    if (targetIndex >= database.definitionMemberInstanceRelationTargetCount ||
        inverseTargets[targetIndex] != 0)
      return false;
    const uint8_t *target =
        database.data + database.definitionMemberInstanceRelationTargets +
        uint64_t{targetIndex} * kDefinitionMemberInstanceRelationTargetSize;
    uint32_t packedPort = read32(target);
    if (index != 0 && std::tie(packedPort, targetIndex) <=
                          std::tie(previousInversePort, previousInverseTarget))
      return false;
    uint32_t low = 0;
    uint32_t high =
        static_cast<uint32_t>(database.definitionMemberInstanceRelationCount);
    while (low != high) {
      uint32_t middle = low + (high - low) / 2;
      const uint8_t *relation =
          database.data + database.definitionMemberInstanceRelations +
          uint64_t{middle} * kDefinitionMemberInstanceRelationSize;
      uint32_t first = read32(relation + 12);
      uint32_t count = read32(relation + 16);
      if (targetIndex >= first + count)
        low = middle + 1;
      else
        high = middle;
    }
    if (low == database.definitionMemberInstanceRelationCount)
      return false;
    const uint8_t *relation =
        database.data + database.definitionMemberInstanceRelations +
        uint64_t{low} * kDefinitionMemberInstanceRelationSize;
    uint32_t first = read32(relation + 12);
    uint16_t reverseSelector = read16(relation + 8);
    const auto *reverseEdge = obelisk::reflection::findVPITraversal(
        static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::RefObj),
        reverseSelector, obelisk::reflection::VPITraversalMode::Iterate);
    uint32_t forwardSelector = reverseEdge ? reverseEdge->inverseSelector : 0;
    const auto *forwardEdge = obelisk::reflection::findVPITraversal(
        static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::Port),
        forwardSelector, obelisk::reflection::VPITraversalMode::Handle);
    uint64_t connectionKey = (uint64_t{packedPort} << 16) | forwardSelector;
    if (targetIndex < first || forwardSelector == 0 ||
        forwardSelector > UINT16_MAX || !forwardEdge ||
        !obelisk::reflection::vpiObjectSetContains(
            forwardEdge->targets,
            static_cast<uint32_t>(
                obelisk::reflection::VPIObjectKind::RefObj)) ||
        !inverseConnectionKeys.insert(connectionKey).second)
      return false;
    inverseTargets[targetIndex] = 1;
    previousInversePort = packedPort;
    previousInverseTarget = targetIndex;
  }

  uint64_t previousSiteID = 0;
  for (uint64_t index = 0; index != database.statementSiteCount; ++index) {
    const uint8_t *site =
        database.data + database.statementSites + index * kStatementSiteSize;
    uint64_t id = read64(site);
    uint32_t statementIndex = read32(site + 8);
    uint16_t phase = read16(site + 12);
    if (id == 0 || (index != 0 && id <= previousSiteID) ||
        statementIndex >= database.statementCount || read16(site + 14) != 0)
      return false;
    previousSiteID = id;
    const uint8_t *statement = database.data + database.statements +
                               uint64_t{statementIndex} * kStatementSize;
    uint16_t kind = read16(statement + 36);
    auto callbackPhase =
        static_cast<obelisk::reflection::VPIStatementCallbackPhase>(phase);
    if (!obelisk::reflection::isVPIStatementCallbackPhase(kind,
                                                          callbackPhase) ||
        phase >= 8 || (siteMasks[statementIndex] & (uint8_t{1} << phase)) != 0)
      return false;
    siteMasks[statementIndex] |= uint8_t{1} << phase;
  }
  for (uint32_t index = 0; index != database.statementCount; ++index) {
    const uint8_t *statement =
        database.data + database.statements + uint64_t{index} * kStatementSize;
    const auto *callback =
        obelisk::reflection::findVPIStatementCallback(read16(statement + 36));
    uint8_t expectedMask = callback ? callback->phaseMask : 0;
    if (siteMasks[index] != expectedMask)
      return false;
  }

  using PortConnectionIdentity =
      std::tuple<uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t>;
  auto portConnectionIdentity = [](const uint8_t *record) {
    return PortConnectionIdentity{read64(record + 16), read64(record + 48),
                                  read64(record + 56), read64(record + 64),
                                  read64(record + 72), read64(record + 80)};
  };
  std::optional<std::vector<std::pair<PortConnectionIdentity, uint32_t>>>
      connectionObjects;
  auto hasUniqueConnectionTarget = [&](const uint8_t *port,
                                       uint32_t targetIndex) {
    if (!connectionObjects) {
      connectionObjects.emplace();
      connectionObjects->reserve(static_cast<size_t>(database.objectCount));
      for (uint32_t index = 0; index != database.objectCount; ++index) {
        const uint8_t *record =
            database.data + database.objects + uint64_t{index} * kObjectSize;
        uint32_t kind = recordKind(record);
        if (kind == OBELISK_RT_DESIGN_RECORD_STORAGE ||
            kind == OBELISK_RT_DESIGN_RECORD_NET)
          connectionObjects->emplace_back(portConnectionIdentity(record),
                                          index);
      }
      std::sort(connectionObjects->begin(), connectionObjects->end());
    }
    PortConnectionIdentity identity = portConnectionIdentity(port);
    auto first = std::lower_bound(
        connectionObjects->begin(), connectionObjects->end(), identity,
        [](const auto &entry, const PortConnectionIdentity &key) {
          return entry.first < key;
        });
    auto last =
        std::upper_bound(first, connectionObjects->end(), identity,
                         [](const PortConnectionIdentity &key,
                            const auto &entry) { return key < entry.first; });
    return last - first == 1 && first->second == targetIndex;
  };

  struct IncomingRelation {
    uint8_t modeMask = 0;
    uint8_t sourceTable = 0;
    uint32_t sourceIndex = 0;
    uint16_t sourceKind = 0;
    uint16_t selector = 0;
    uint32_t ordinal = 0;
  };
  std::vector<IncomingRelation> incomingRelations(database.statementCount);
  bool haveStatementContainment = false;
  bool havePreviousRelation = false;
  uint8_t previousTable = 0;
  uint32_t previousSource = 0;
  uint16_t previousSelector = 0;
  bool previousIterate = false;
  uint32_t previousOrdinal = 0;
  uint16_t currentSourceKind = 0;
  uint32_t expectedOrdinal = 0;
  const obelisk::reflection::VPITraversalDescriptor *automaticEdge = nullptr;
  uint64_t automaticChildCursor = 0;
  std::unordered_set<uint64_t> staticLexicalChildren;
  std::unordered_set<uint64_t> staticLexicalParents;
  std::vector<uint32_t> firstStaticSuccessor(database.staticObjectCount,
                                             UINT32_MAX);
  std::vector<uint32_t> staticSuccessorCount(database.staticObjectCount, 0);
  std::vector<uint32_t> staticSuccessors;
  std::vector<bool> reachableStatic(database.staticObjectCount, false);
  auto relationEndpoint = [](obelisk::reflection::TableKind table,
                             uint32_t index) {
    return (static_cast<uint32_t>(table) << 30) | index;
  };
  auto lexicalPair = [](uint32_t parent, uint32_t child) {
    return (uint64_t{parent} << 32) | child;
  };
  // Once an automatic group is present, it must be the exact filtered child
  // chain. An entirely absent group remains valid because VPI deliberately
  // falls back to walking that immutable chain for compatibility.
  auto nextAutomaticChild = [&](uint64_t &cursor) {
    while (cursor != 0) {
      const uint8_t *record;
      uint32_t kind;
      if (!getRecord(database, cursor, record, kind) ||
          kind == OBELISK_RT_DESIGN_RECORD_TYPE)
        return uint64_t{0};
      uint64_t current = cursor;
      cursor = nextOffset(record, kind);
      // Static records are physically chained below a scope only to make the
      // pointer-free image reachable. Their generated relations carry the
      // actual lexical owner and declaration order.
      if (kind == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT ||
          (read32(record + 4) & OBELISK_RT_DESIGN_CAP_LEXICAL_ANCHOR) != 0)
        continue;
      if (obelisk::reflection::vpiObjectSetContains(automaticEdge->targets,
                                                    recordVPIKind(record)))
        return current;
    }
    return uint64_t{0};
  };
  auto automaticGroupComplete = [&] {
    return !automaticEdge ||
           automaticEdge->automaticRelation !=
               obelisk::reflection::VPIAutomaticRelation::DirectChild ||
           nextAutomaticChild(automaticChildCursor) == 0;
  };
  for (uint64_t index = 0; index != database.relationCount; ++index) {
    const uint8_t *bytes =
        database.data + database.relations + index * kRelationSize;
    obelisk::reflection::RelationView relation(bytes);
    uint32_t sourceIndex = relation.getSourceIndex();
    uint32_t packedTarget = relation.getTargetIndexAndTable();
    auto targetTable = obelisk::reflection::unpackTableIndexKind(packedTarget);
    uint32_t targetIndex = obelisk::reflection::unpackTableIndex(packedTarget);
    uint32_t ordinal = relation.getOrdinal();
    uint16_t selector = relation.getSelector();
    uint16_t packedSource = relation.getSourceKindAndTable();
    auto sourceTable =
        obelisk::reflection::unpackRelationSourceTable(packedSource);
    bool iterate = obelisk::reflection::relationSourceIsIterate(packedSource);
    if (!obelisk::reflection::isValidTableKind(sourceTable) ||
        !obelisk::reflection::isValidTableKind(targetTable))
      return false;
    uint8_t table = static_cast<uint8_t>(sourceTable);
    uint16_t sourceKind =
        obelisk::reflection::unpackRelationSourceKind(packedSource);
    if (havePreviousRelation &&
        std::tie(table, sourceIndex, selector, iterate, ordinal) <=
            std::tie(previousTable, previousSource, previousSelector,
                     previousIterate, previousOrdinal))
      return false;
    bool sameSource = havePreviousRelation && table == previousTable &&
                      sourceIndex == previousSource;
    bool sameGroup = sameSource && selector == previousSelector &&
                     iterate == previousIterate;
    if (sameSource) {
      if (sourceKind != currentSourceKind)
        return false;
    } else {
      currentSourceKind = sourceKind;
    }
    if (!sameGroup)
      expectedOrdinal = 0;
    havePreviousRelation = true;
    previousTable = table;
    previousSource = sourceIndex;
    previousSelector = selector;
    previousIterate = iterate;
    previousOrdinal = ordinal;

    const uint8_t *sourceRecord = nullptr;
    const auto *sourceDescriptor =
        obelisk::reflection::findVPIObjectKind(sourceKind);
    bool rootSource = sourceKind == 0;
    if (!rootSource &&
        (!sourceDescriptor || sourceDescriptor->role !=
                                  obelisk::reflection::VPIObjectRole::Concrete))
      return false;
    switch (sourceTable) {
    case obelisk::reflection::TableKind::Scope:
      if (sourceIndex >= database.scopeCount ||
          (!rootSource &&
           (sourceDescriptor->families &
            obelisk::reflection::vpiFamilyMask(
                obelisk::reflection::VPIObjectFamily::Scope)) == 0))
        return false;
      sourceRecord =
          database.data + database.scopes + uint64_t{sourceIndex} * kScopeSize;
      if (rootSource && sourceRecord != database.data + database.root)
        return false;
      break;
    case obelisk::reflection::TableKind::Object: {
      if (rootSource || sourceIndex >= database.objectCount)
        return false;
      sourceRecord = database.data + database.objects +
                     uint64_t{sourceIndex} * kObjectSize;
      break;
    }
    case obelisk::reflection::TableKind::Statement:
      if (rootSource || sourceIndex >= database.statementCount)
        return false;
      sourceRecord = database.data + database.statements +
                     uint64_t{sourceIndex} * kStatementSize;
      if (read16(sourceRecord + 36) != sourceKind)
        return false;
      break;
    case obelisk::reflection::TableKind::StaticObject:
      if (rootSource || sourceIndex >= database.staticObjectCount)
        return false;
      sourceRecord = database.data + database.staticObjects +
                     uint64_t{sourceIndex} * kStaticObjectSize;
      if (read16(sourceRecord + 28) != sourceKind)
        return false;
      break;
    }
    if (sourceTable != obelisk::reflection::TableKind::Statement &&
        sourceTable != obelisk::reflection::TableKind::StaticObject) {
      uint32_t intrinsicKind = recordVPIKind(sourceRecord);
      if (intrinsicKind != sourceKind)
        return false;
    }

    const uint8_t *target = nullptr;
    uint32_t targetKind = 0;
    switch (targetTable) {
    case obelisk::reflection::TableKind::Scope:
      if (targetIndex >= database.scopeCount)
        return false;
      target =
          database.data + database.scopes + uint64_t{targetIndex} * kScopeSize;
      targetKind = recordVPIKind(target);
      break;
    case obelisk::reflection::TableKind::Object:
      if (targetIndex >= database.objectCount)
        return false;
      target = database.data + database.objects +
               uint64_t{targetIndex} * kObjectSize;
      targetKind = recordVPIKind(target);
      break;
    case obelisk::reflection::TableKind::Statement:
      if (targetIndex >= database.statementCount)
        return false;
      target = database.data + database.statements +
               uint64_t{targetIndex} * kStatementSize;
      targetKind = read16(target + 36);
      break;
    case obelisk::reflection::TableKind::StaticObject:
      if (targetIndex >= database.staticObjectCount)
        return false;
      target = database.data + database.staticObjects +
               uint64_t{targetIndex} * kStaticObjectSize;
      targetKind = read16(target + 28);
      break;
    }
    auto mode = iterate ? obelisk::reflection::VPITraversalMode::Iterate
                        : obelisk::reflection::VPITraversalMode::Handle;
    const auto *edge =
        obelisk::reflection::findVPITraversal(sourceKind, selector, mode);
    if (!edge ||
        !obelisk::reflection::vpiObjectSetContains(edge->targets, targetKind) ||
        ordinal != expectedOrdinal || (!iterate && ordinal != 0))
      return false;
    ++expectedOrdinal;
    if (targetTable == obelisk::reflection::TableKind::StaticObject) {
      if (sourceTable == obelisk::reflection::TableKind::StaticObject) {
        if (firstStaticSuccessor[sourceIndex] == UINT32_MAX)
          firstStaticSuccessor[sourceIndex] =
              static_cast<uint32_t>(staticSuccessors.size());
        ++staticSuccessorCount[sourceIndex];
        staticSuccessors.push_back(targetIndex);
      } else {
        reachableStatic[targetIndex] = true;
      }
    }

    auto relationBacked = [](const uint8_t *record) {
      uint32_t kind = recordKind(record);
      return kind == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT ||
             (read32(record + 4) & OBELISK_RT_DESIGN_CAP_LEXICAL_ANCHOR) != 0;
    };
    bool relationBackedTarget =
        targetTable == obelisk::reflection::TableKind::StaticObject ||
        (targetTable == obelisk::reflection::TableKind::Object &&
         relationBacked(target));
    bool lexicalForward =
        selector == targetKind ||
        edge->automaticRelation ==
            obelisk::reflection::VPIAutomaticRelation::DirectChild;
    // Relation-backed lexical containment is independent of the physical
    // record chain. Pair each real declaration edge with the generated
    // reverse vpiScope relation so malformed images cannot disagree by query
    // direction. Semantic cross-references such as vpiDerivedClasses are not
    // declaration edges even though they also target a class definition.
    if (iterate && !rootSource && relationBackedTarget && lexicalForward) {
      bool hasMatchingParent = std::any_of(
          std::begin(obelisk::reflection::vpiTraversals),
          std::end(obelisk::reflection::vpiTraversals),
          [&](const obelisk::reflection::VPITraversalDescriptor &parentEdge) {
            return parentEdge.sourceType == targetKind &&
                   parentEdge.mode ==
                       obelisk::reflection::VPITraversalMode::Handle &&
                   parentEdge.automaticRelation ==
                       obelisk::reflection::VPIAutomaticRelation::ParentScope &&
                   obelisk::reflection::vpiObjectSetContains(parentEdge.targets,
                                                             sourceKind);
          });
      if (hasMatchingParent)
        staticLexicalChildren.insert(lexicalPair(
            relationEndpoint(sourceTable, sourceIndex), packedTarget));
    }

    uint64_t sourceOffset = static_cast<uint64_t>(sourceRecord - database.data);
    uint64_t targetOffset = static_cast<uint64_t>(target - database.data);
    if (!sameGroup) {
      if (!automaticGroupComplete())
        return false;
      automaticEdge = edge;
      automaticChildCursor =
          edge->automaticRelation ==
                      obelisk::reflection::VPIAutomaticRelation::DirectChild &&
                  sourceTable == obelisk::reflection::TableKind::Scope
              ? read64(sourceRecord + 24)
              : 0;
    } else if (automaticEdge != edge) {
      return false;
    }
    if (inverseConnectionKeys.find(
            (uint64_t{relationEndpoint(sourceTable, sourceIndex)} << 16) |
            selector) != inverseConnectionKeys.end())
      return false;
    switch (edge->automaticRelation) {
    case obelisk::reflection::VPIAutomaticRelation::None:
      break;
    case obelisk::reflection::VPIAutomaticRelation::DirectChild:
      if (relationBackedTarget) {
        break;
      }
      // Generated scopes are lexical records, while an instantiated module,
      // interface or program keeps its enclosing execution scope as physical
      // parent. IEEE 1800-2023 37.85 still exposes the instance through the
      // generate scope's vpiInternalScope relation. Check their common
      // enclosing scope instead of requiring a physical scope-table source.
      if (sourceKind == static_cast<uint32_t>(
                            obelisk::reflection::VPIObjectKind::GenScope) &&
          targetTable == obelisk::reflection::TableKind::Scope &&
          sourceTable != obelisk::reflection::TableKind::Scope) {
        uint64_t enclosingScope =
            sourceTable == obelisk::reflection::TableKind::StaticObject
                ? database.scopes +
                      uint64_t{read32(sourceRecord + 8)} * kScopeSize
                : read64(sourceRecord + 16);
        if (read64(target + 16) != enclosingScope)
          return false;
        break;
      }
      if (sourceTable != obelisk::reflection::TableKind::Scope ||
          targetTable == obelisk::reflection::TableKind::Statement ||
          read64(target + 16) != sourceOffset ||
          nextAutomaticChild(automaticChildCursor) != targetOffset)
        return false;
      break;
    case obelisk::reflection::VPIAutomaticRelation::ParentScope:
      if (sourceTable == obelisk::reflection::TableKind::Statement) {
        obelisk::reflection::TableKind expectedTable;
        uint32_t expectedIndex = 0;
        if (!effectiveStatementScope(database, sourceIndex, expectedTable,
                                     expectedIndex) ||
            targetTable != expectedTable || targetIndex != expectedIndex)
          return false;
      } else if (sourceTable == obelisk::reflection::TableKind::Object &&
                 relationBacked(sourceRecord)) {
        // The relation itself is the canonical lexical owner. The physical
        // scope field only keeps this immutable record reachable and may name
        // a different container (for example a package or enclosing class).
        staticLexicalParents.insert(lexicalPair(
            packedTarget, relationEndpoint(sourceTable, sourceIndex)));
      } else if (sourceTable == obelisk::reflection::TableKind::StaticObject) {
        staticLexicalParents.insert(lexicalPair(
            packedTarget, relationEndpoint(sourceTable, sourceIndex)));
      } else if (targetTable != obelisk::reflection::TableKind::Scope ||
                 read64(sourceRecord + 16) != targetOffset) {
        return false;
      }
      break;
    case obelisk::reflection::VPIAutomaticRelation::DirectPortConnection:
      if (sourceTable != obelisk::reflection::TableKind::Object ||
          targetTable != obelisk::reflection::TableKind::Object ||
          !isWholePortConnection(sourceRecord, target) ||
          !hasUniqueConnectionTarget(sourceRecord, targetIndex))
        return false;
      break;
    case obelisk::reflection::VPIAutomaticRelation::IndexedContainer:
      // The relation-index member table below is the canonical bidirectional
      // proof that this selected object belongs to the target array.
      if (targetTable != obelisk::reflection::TableKind::Object ||
          !relationBacked(target))
        return false;
      break;
    case obelisk::reflection::VPIAutomaticRelation::DefinitionMember:
    case obelisk::reflection::VPIAutomaticRelation::DefinitionMemberParent:
    case obelisk::reflection::VPIAutomaticRelation::DefinitionMemberExpr:
    case obelisk::reflection::VPIAutomaticRelation::
        DefinitionMemberInstanceRelation:
      // These edges are reconstructed from the compact definition tables and
      // must not also appear in the per-instance general relation table.
      return false;
    }

    if (!edge->statementContainment)
      continue;
    if (targetTable != obelisk::reflection::TableKind::Statement)
      return false;
    IncomingRelation &incoming = incomingRelations[targetIndex];
    uint8_t modeBit = iterate ? 2 : 1;
    if (incoming.modeMask == 0) {
      incoming.sourceTable = table;
      incoming.sourceIndex = sourceIndex;
      incoming.sourceKind = sourceKind;
      incoming.selector = selector;
      incoming.ordinal = ordinal;
    } else if ((incoming.modeMask & modeBit) != 0 ||
               std::tie(incoming.sourceTable, incoming.sourceIndex,
                        incoming.sourceKind, incoming.selector,
                        incoming.ordinal) != std::tie(table, sourceIndex,
                                                      sourceKind, selector,
                                                      ordinal)) {
      return false;
    }
    incoming.modeMask |= modeBit;
    haveStatementContainment = true;
    uint32_t targetOwner = read32(target + 8);
    uint32_t targetScope = read32(target + 12);
    uint32_t targetParent = read32(target + 16);
    switch (sourceTable) {
    case obelisk::reflection::TableKind::Scope:
      if (targetOwner != UINT32_MAX || targetParent != UINT32_MAX ||
          targetScope != sourceIndex)
        return false;
      break;
    case obelisk::reflection::TableKind::Object: {
      uint64_t sourceScope = read64(sourceRecord + 16);
      uint32_t sourceScopeIndex =
          static_cast<uint32_t>((sourceScope - database.scopes) / kScopeSize);
      if (targetParent != UINT32_MAX || targetScope != sourceScopeIndex)
        return false;
      if (targetOwner == UINT32_MAX) {
        const auto *sourceDescriptor =
            obelisk::reflection::findVPIObjectKind(sourceKind);
        if (!sourceDescriptor ||
            (sourceDescriptor->families &
             obelisk::reflection::vpiFamilyMask(
                 obelisk::reflection::VPIObjectFamily::Scope)) == 0)
          return false;
      } else if (targetOwner != sourceIndex) {
        return false;
      }
      break;
    }
    case obelisk::reflection::TableKind::Statement:
      if (targetParent != sourceIndex ||
          targetOwner != read32(sourceRecord + 8) ||
          targetScope != read32(sourceRecord + 12))
        return false;
      break;
    case obelisk::reflection::TableKind::StaticObject: {
      uint32_t sourceScope = read32(sourceRecord + 8);
      if (sourceScope >= database.scopeCount || targetOwner != UINT32_MAX ||
          targetParent != UINT32_MAX || targetScope != sourceScope)
        return false;
      break;
    }
    }
  }
  if (!automaticGroupComplete())
    return false;
  // ParentScope also represents an effective enclosing scope. An indexed
  // member can therefore have an additional vpiModule/vpiInstance relation
  // that skips its immediate array container. Require the reverse relation
  // for every actual lexical child without rejecting those generated
  // enclosing-scope relations. Relation-index validation below separately
  // proves the member-to-array edge.
  if (!std::all_of(staticLexicalChildren.begin(), staticLexicalChildren.end(),
                   [&](uint64_t child) {
                     return staticLexicalParents.count(child) != 0;
                   }))
    return false;
  if (haveStatementContainment &&
      std::any_of(incomingRelations.begin(), incomingRelations.end(),
                  [](const IncomingRelation &incoming) {
                    return incoming.modeMask == 0;
                  }))
    return false;

  std::vector<bool> relationIndexedObjects(database.objectCount, false);
  std::vector<bool> usedDimensions(database.relationIndexDimensionCount, false);
  std::vector<bool> usedKeys(database.relationIndexKeyCount, false);
  uint32_t previousRelationIndexObject = 0;
  for (uint32_t index = 0; index != database.relationIndexCount; ++index) {
    const uint8_t *entry = database.data + database.relationIndices +
                           uint64_t{index} * kRelationIndexSize;
    uint32_t objectIndex = read32(entry);
    uint32_t firstDimension = read32(entry + 4);
    uint16_t dimensionCount = read16(entry + 8);
    uint16_t flags = read16(entry + 10);
    uint32_t firstKey = read32(entry + 12);
    uint32_t firstOrdinalKey = read32(entry + 16);
    if (objectIndex >= database.objectCount || dimensionCount == 0 ||
        (index != 0 && objectIndex <= previousRelationIndexObject) ||
        relationIndexedObjects[objectIndex])
      return false;
    previousRelationIndexObject = objectIndex;
    relationIndexedObjects[objectIndex] = true;
    const uint8_t *object =
        database.data + database.objects + uint64_t{objectIndex} * kObjectSize;
    uint32_t vpiKind = recordVPIKind(object);
    const auto *access = obelisk::reflection::findVPIIndexedAccess(vpiKind);
    using VPIKind = obelisk::reflection::VPIObjectKind;
    bool primitiveArray =
        vpiKind == static_cast<uint32_t>(VPIKind::GateArray) ||
        vpiKind == static_cast<uint32_t>(VPIKind::SwitchArray) ||
        vpiKind == static_cast<uint32_t>(VPIKind::UdpArray);
    uint64_t elementCount = read64(object + 56);
    bool sparse = flags == 1;
    bool generateArray =
        vpiKind == static_cast<uint32_t>(VPIKind::GenScopeArray);
    if (recordKind(object) != OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT ||
        read64(object + 48) != 0 || (!sparse && elementCount == 0) ||
        elementCount > UINT32_MAX || !access ||
        access->accessKind !=
            obelisk::reflection::VPIIndexedAccessKind::RelationElement ||
        (flags != 0 && !sparse))
      return false;
    if (sparse != generateArray)
      return false;
    if (primitiveArray && (sparse || dimensionCount != 1))
      return false;

    uint64_t firstRelation = lowerBoundRelation(
        database, obelisk::reflection::TableKind::Object, objectIndex,
        static_cast<uint16_t>(access->relationSelector), true);
    bool hasRelation =
        firstRelation != database.relationCount &&
        relationMatches(relationAt(database, firstRelation),
                        obelisk::reflection::TableKind::Object, objectIndex,
                        static_cast<uint16_t>(access->relationSelector), true);
    if ((elementCount == 0 && hasRelation) ||
        (elementCount != 0 &&
         (!hasRelation ||
          upperBoundRelation(
              database, firstRelation, obelisk::reflection::TableKind::Object,
              objectIndex, static_cast<uint16_t>(access->relationSelector),
              true) -
                  firstRelation !=
              elementCount)))
      return false;

    if (sparse) {
      if (dimensionCount != 1 || firstDimension != UINT32_MAX ||
          firstKey > database.relationIndexKeyCount ||
          elementCount > database.relationIndexKeyCount - firstKey ||
          firstOrdinalKey > database.relationIndexKeyCount ||
          elementCount > database.relationIndexKeyCount - firstOrdinalKey ||
          readI64(object + 64) != 0 || readI64(object + 72) != 0)
        return false;
      std::vector<bool> ordinals(static_cast<size_t>(elementCount), false);
      std::vector<int64_t> valuesByOrdinal(static_cast<size_t>(elementCount));
      int64_t previousKey = 0;
      for (uint32_t key = 0; key != elementCount; ++key) {
        uint32_t keyIndex = firstKey + key;
        if (usedKeys[keyIndex])
          return false;
        usedKeys[keyIndex] = true;
        const uint8_t *keyRecord = database.data + database.relationIndexKeys +
                                   uint64_t{keyIndex} * kRelationIndexKeySize;
        int64_t value = readI64(keyRecord);
        uint32_t ordinal = read32(keyRecord + 8);
        if ((key != 0 && value <= previousKey) || ordinal >= elementCount ||
            ordinals[ordinal])
          return false;
        previousKey = value;
        ordinals[ordinal] = true;
        valuesByOrdinal[ordinal] = value;
      }
      for (uint32_t ordinal = 0; ordinal != elementCount; ++ordinal) {
        uint32_t keyIndex = firstOrdinalKey + ordinal;
        if (usedKeys[keyIndex])
          return false;
        usedKeys[keyIndex] = true;
        const uint8_t *keyRecord = database.data + database.relationIndexKeys +
                                   uint64_t{keyIndex} * kRelationIndexKeySize;
        if (read32(keyRecord + 8) != ordinal ||
            readI64(keyRecord) != valuesByOrdinal[ordinal])
          return false;
      }
      continue;
    }

    if (firstDimension > database.relationIndexDimensionCount ||
        dimensionCount >
            database.relationIndexDimensionCount - firstDimension ||
        firstKey != UINT32_MAX || firstOrdinalKey != UINT32_MAX)
      return false;
    uint64_t product = 1;
    for (uint32_t dimension = 0; dimension != dimensionCount; ++dimension) {
      uint32_t dimensionIndex = firstDimension + dimension;
      if (usedDimensions[dimensionIndex])
        return false;
      usedDimensions[dimensionIndex] = true;
      const uint8_t *range =
          database.data + database.relationIndexDimensions +
          uint64_t{dimensionIndex} * kRelationIndexDimensionSize;
      int64_t left = readI64(range);
      int64_t right = readI64(range + 8);
      uint64_t distance =
          left >= right
              ? static_cast<uint64_t>(left) - static_cast<uint64_t>(right)
              : static_cast<uint64_t>(right) - static_cast<uint64_t>(left);
      if (distance == UINT64_MAX || distance + 1 > UINT32_MAX ||
          product > UINT32_MAX / (distance + 1))
        return false;
      product *= distance + 1;
      if (dimension == 0 &&
          (readI64(object + 64) != left || readI64(object + 72) != right))
        return false;
    }
    if (product != elementCount)
      return false;
  }
  for (uint32_t index = 0; index != database.objectCount; ++index) {
    const uint8_t *object =
        database.data + database.objects + uint64_t{index} * kObjectSize;
    const auto *access =
        obelisk::reflection::findVPIIndexedAccess(recordVPIKind(object));
    bool requiresIndex =
        recordKind(object) == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT &&
        access &&
        access->accessKind ==
            obelisk::reflection::VPIIndexedAccessKind::RelationElement;
    if (requiresIndex != relationIndexedObjects[index])
      return false;
  }
  if (std::any_of(usedDimensions.begin(), usedDimensions.end(),
                  [](bool used) { return !used; }) ||
      std::any_of(usedKeys.begin(), usedKeys.end(),
                  [](bool used) { return !used; }))
    return false;
  uint64_t indexedElementCount = 0;
  for (uint32_t index = 0; index != database.relationIndexCount; ++index) {
    const uint8_t *entry = database.data + database.relationIndices +
                           uint64_t{index} * kRelationIndexSize;
    uint32_t objectIndex = read32(entry);
    indexedElementCount += read64(database.data + database.objects +
                                  uint64_t{objectIndex} * kObjectSize + 56);
  }
  if (indexedElementCount != database.relationIndexMemberCount)
    return false;
  uint32_t previousMemberTarget = 0;
  for (uint32_t index = 0; index != database.relationIndexMemberCount;
       ++index) {
    const uint8_t *member = database.data + database.relationIndexMembers +
                            uint64_t{index} * kRelationIndexMemberSize;
    uint32_t packedTarget = read32(member);
    uint32_t relationIndex = read32(member + 4);
    uint32_t ordinal = read32(member + 8);
    if ((index != 0 && packedTarget <= previousMemberTarget) ||
        relationIndex >= database.relationIndexCount)
      return false;
    previousMemberTarget = packedTarget;
    auto targetTable = obelisk::reflection::unpackTableIndexKind(packedTarget);
    uint32_t targetIndex = obelisk::reflection::unpackTableIndex(packedTarget);
    if ((targetTable == obelisk::reflection::TableKind::Scope
             ? targetIndex >= database.scopeCount
         : targetTable == obelisk::reflection::TableKind::Object
             ? targetIndex >= database.objectCount
             : true))
      return false;
    const uint8_t *relationIndexRecord =
        database.data + database.relationIndices +
        uint64_t{relationIndex} * kRelationIndexSize;
    uint32_t sourceIndex = read32(relationIndexRecord);
    const uint8_t *source =
        database.data + database.objects + uint64_t{sourceIndex} * kObjectSize;
    uint64_t elementCount = read64(source + 56);
    uint32_t sourceKind = recordVPIKind(source);
    uint32_t targetKind = recordVPIKind(
        database.data +
        (targetTable == obelisk::reflection::TableKind::Scope
             ? database.scopes + uint64_t{targetIndex} * kScopeSize
             : database.objects + uint64_t{targetIndex} * kObjectSize));
    const auto *access = obelisk::reflection::findVPIIndexedAccess(sourceKind);
    if (ordinal >= elementCount || !access)
      return false;
    uint64_t firstRelation = lowerBoundRelation(
        database, obelisk::reflection::TableKind::Object, sourceIndex,
        static_cast<uint16_t>(access->relationSelector), true);
    if (firstRelation >= database.relationCount ||
        relationAt(database, firstRelation + ordinal)
                .getTargetIndexAndTable() != packedTarget)
      return false;
    uint32_t packedArray = 0;
    if (!obelisk::reflection::tryPackTableIndex(
            obelisk::reflection::TableKind::Object, sourceIndex, packedArray))
      return false;
    auto firstEdge = std::lower_bound(
        std::begin(obelisk::reflection::vpiTraversals),
        std::end(obelisk::reflection::vpiTraversals), targetKind,
        [](const obelisk::reflection::VPITraversalDescriptor &edge,
           uint32_t kind) { return edge.sourceType < kind; });
    for (auto edgeIterator = firstEdge;
         edgeIterator != std::end(obelisk::reflection::vpiTraversals) &&
         edgeIterator->sourceType == targetKind;
         ++edgeIterator) {
      const auto &edge = *edgeIterator;
      if (edge.mode != obelisk::reflection::VPITraversalMode::Handle ||
          edge.automaticRelation !=
              obelisk::reflection::VPIAutomaticRelation::IndexedContainer ||
          !obelisk::reflection::vpiObjectSetContains(edge.targets, sourceKind))
        continue;
      uint64_t reverse =
          lowerBoundRelation(database, targetTable, targetIndex,
                             static_cast<uint16_t>(edge.selector), false);
      if (reverse == database.relationCount ||
          !relationMatches(relationAt(database, reverse), targetTable,
                           targetIndex, static_cast<uint16_t>(edge.selector),
                           false) ||
          relationAt(database, reverse).getTargetIndexAndTable() != packedArray)
        return false;
    }
  }

  uint64_t previousHash = 0;
  std::string_view previousName;
  std::unordered_set<uint64_t> indexedRecords;
  std::unordered_map<uint64_t, uint64_t> indexedNames;
  indexedRecords.reserve(static_cast<size_t>(database.indexCount));
  indexedNames.reserve(static_cast<size_t>(database.indexCount));
  for (uint64_t index = 0; index != database.indexCount; ++index) {
    const uint8_t *entry = database.data + database.index + index * kIndexSize;
    uint64_t hash = read64(entry);
    std::string_view name;
    if (!getString(database, read64(entry + 8), name))
      return false;
    const uint8_t *record;
    uint64_t recordOffset = read64(entry + 16);
    uint64_t recordName = 0;
    if (isStaticObjectOffset(database, recordOffset)) {
      record = database.data + recordOffset;
      uint32_t relativeName = read32(record + 16);
      if (relativeName == 0 || relativeName >= database.stringSize)
        return false;
      recordName = database.strings + relativeName;
    } else {
      uint32_t kind = 0;
      if (!getRecord(database, recordOffset, record, kind) ||
          kind == OBELISK_RT_DESIGN_RECORD_TYPE)
        return false;
      recordName = read64(record + 40);
    }
    if (hash != nameHash(reinterpret_cast<const uint8_t *>(name.data()),
                         name.size()) ||
        !indexedRecords.insert(recordOffset).second ||
        !indexedNames.emplace(read64(entry + 8), recordOffset).second ||
        recordName != read64(entry + 8) ||
        (index != 0 && (hash < previousHash ||
                        (hash == previousHash && name <= previousName))))
      return false;
    previousHash = hash;
    previousName = name;
  }
  for (uint64_t index = 0; index != database.scopeCount; ++index)
    if (indexedRecords.find(database.scopes + index * kScopeSize) ==
        indexedRecords.end())
      return false;
  for (uint64_t index = 0; index != database.staticObjectCount; ++index) {
    uint64_t offset = database.staticObjects + index * kStaticObjectSize;
    uint32_t name = read32(database.data + offset + 16);
    bool indexed = indexedRecords.find(offset) != indexedRecords.end();
    if ((name != 0) != indexed)
      return false;
    if (indexed)
      reachableStatic[index] = true;
  }
  // Compact identities are not part of the physical child chain. Every one
  // must therefore be discoverable either by name or by following relations
  // from another discoverable record; an unreachable relation cycle does not
  // make its members queryable.
  std::vector<uint32_t> pendingStatic;
  for (uint32_t index = 0; index != database.staticObjectCount; ++index)
    if (reachableStatic[index])
      pendingStatic.push_back(index);
  while (!pendingStatic.empty()) {
    uint32_t source = pendingStatic.back();
    pendingStatic.pop_back();
    uint32_t first = firstStaticSuccessor[source];
    if (first == UINT32_MAX)
      continue;
    for (uint32_t edge = 0; edge != staticSuccessorCount[source]; ++edge) {
      uint32_t target = staticSuccessors[first + edge];
      if (!reachableStatic[target]) {
        reachableStatic[target] = true;
        pendingStatic.push_back(target);
      }
    }
  }
  if (std::find(reachableStatic.begin(), reachableStatic.end(), false) !=
      reachableStatic.end())
    return false;
  const obelisk::reflection::VPITraversalDescriptor *portConnectionEdge =
      nullptr;
  for (const auto &edge : obelisk::reflection::vpiTraversals) {
    if (edge.sourceType !=
            static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::Port) ||
        edge.automaticRelation !=
            obelisk::reflection::VPIAutomaticRelation::DirectPortConnection)
      continue;
    if (portConnectionEdge)
      return false;
    portConnectionEdge = &edge;
  }
  if (!portConnectionEdge ||
      portConnectionEdge->mode != obelisk::reflection::VPITraversalMode::Handle)
    return false;
  auto lowConnectionTarget = [&](uint32_t sourceIndex, uint32_t &targetIndex) {
    uint64_t relationIndex = lowerBoundRelation(
        database, obelisk::reflection::TableKind::Object, sourceIndex,
        static_cast<uint16_t>(portConnectionEdge->selector), false);
    if (relationIndex == database.relationCount)
      return false;
    auto relation = relationAt(database, relationIndex);
    if (!relationMatches(
            relation, obelisk::reflection::TableKind::Object, sourceIndex,
            static_cast<uint16_t>(portConnectionEdge->selector), false) ||
        obelisk::reflection::unpackTableIndexKind(
            relation.getTargetIndexAndTable()) !=
            obelisk::reflection::TableKind::Object)
      return false;
    targetIndex = obelisk::reflection::unpackTableIndex(
        relation.getTargetIndexAndTable());
    return true;
  };
  // Whole-source ports require an exact generated vpiLowConn relation.
  // Same-name ports are the only objects intentionally omitted from name
  // lookup; explicitly renamed whole ports remain ordinary indexed records.
  for (uint64_t index = 0; index != database.objectCount; ++index) {
    uint64_t offset = database.objects + index * kObjectSize;
    const uint8_t *port = database.data + offset;
    bool indexed = indexedRecords.find(offset) != indexedRecords.end();
    uint64_t refLowConnectionKey =
        (uint64_t{relationEndpoint(obelisk::reflection::TableKind::Object,
                                   static_cast<uint32_t>(index))}
         << 16) |
        static_cast<uint16_t>(obelisk::reflection::VPIRelationKind::LowConnRel);
    bool hasRefLowConnection =
        inverseConnectionKeys.find(refLowConnectionKey) !=
        inverseConnectionKeys.end();
    uint32_t caps = read32(port + 4);
    bool isRefPort = (caps & OBELISK_RT_DESIGN_CAP_PORT_REF) != 0;
    if (recordKind(port) == OBELISK_RT_DESIGN_RECORD_PORT &&
        (isRefPort != hasRefLowConnection ||
         (isRefPort && (caps & OBELISK_RT_DESIGN_CAP_PORT_WHOLE_SOURCE) != 0)))
      return false;
    if (indexed) {
      if (recordKind(port) == OBELISK_RT_DESIGN_RECORD_PORT &&
          (read32(port + 4) & OBELISK_RT_DESIGN_CAP_PORT_WHOLE_SOURCE) != 0 &&
          !hasRefLowConnection) {
        uint32_t targetIndex = 0;
        if (!lowConnectionTarget(static_cast<uint32_t>(index), targetIndex))
          return false;
      }
      continue;
    }
    // Compiler/runtime machinery is deliberately absent from the public name
    // index. Static records without the named-typespec capability are reached
    // exclusively through generated VPI relations (for example anonymous
    // typespecs and interface-type variants).
    if ((caps & OBELISK_RT_DESIGN_CAP_INTERNAL) != 0 ||
        (recordKind(port) == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT &&
         (caps & OBELISK_RT_DESIGN_CAP_NAMED_TYPESPEC) == 0))
      continue;
    if (recordKind(port) != OBELISK_RT_DESIGN_RECORD_PORT)
      return false;
    auto canonicalName = indexedNames.find(read64(port + 40));
    if (canonicalName == indexedNames.end() ||
        !isObjectOffset(database, canonicalName->second))
      return false;
    const uint8_t *canonical = database.data + canonicalName->second;
    if (!isWholePortConnection(port, canonical))
      return false;
    uint32_t sourceIndex = static_cast<uint32_t>(index);
    uint32_t targetIndex = 0;
    if ((!hasRefLowConnection &&
         !lowConnectionTarget(sourceIndex, targetIndex)) ||
        (!hasRefLowConnection &&
         targetIndex !=
             static_cast<uint32_t>((canonicalName->second - database.objects) /
                                   kObjectSize)))
      return false;
  }
  return true;
}

// Reflection entry points have a C ABI. Keep allocator failures and malformed
// graph corner cases from unwinding through callers that cannot catch C++
// exceptions; design_validate retains its explicit outer guard as well.
obelisk_rt_status validateDatabase(const Database &database) noexcept {
  OBELISK_RT_TRY {
    return validateDatabaseImpl(database) ? OBELISK_RT_OK
                                          : OBELISK_RT_INVALID_DESIGN;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_DESIGN; }
}

uint64_t nameHash(const uint8_t *name, uint64_t size) {
  return obelisk_stable_hash(name, size);
}

uint32_t descriptorKind(uint32_t recordKind) {
  switch (recordKind) {
  case OBELISK_RT_DESIGN_RECORD_SCOPE:
    return OBELISK_RT_DESCRIPTOR_SCOPE;
  case OBELISK_RT_DESIGN_RECORD_STORAGE:
    return OBELISK_RT_DESCRIPTOR_STORAGE;
  case OBELISK_RT_DESIGN_RECORD_NET:
    return OBELISK_RT_DESCRIPTOR_NET;
  case OBELISK_RT_DESIGN_RECORD_DRIVER:
    return OBELISK_RT_DESCRIPTOR_DRIVER;
  case OBELISK_RT_DESIGN_RECORD_PROCESS:
    return OBELISK_RT_DESCRIPTOR_PROCESS;
  case OBELISK_RT_DESIGN_RECORD_FUNCTION:
    return OBELISK_RT_DESCRIPTOR_FUNCTION;
  case OBELISK_RT_DESIGN_RECORD_PORT:
    return OBELISK_RT_DESCRIPTOR_PORT;
  case OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT:
    return OBELISK_RT_DESCRIPTOR_INVALID;
  default:
    return OBELISK_RT_DESCRIPTOR_INVALID;
  }
}

uint32_t transitionEdges(bool oldValue, bool oldUnknown, bool newValue,
                         bool newUnknown) {
  if (oldValue == newValue && oldUnknown == newUnknown)
    return 0;
  uint32_t result = OBELISK_RT_SIGNAL_CHANGE;
  bool oldZero = !oldUnknown && !oldValue;
  bool oldOne = !oldUnknown && oldValue;
  bool newZero = !newUnknown && !newValue;
  bool newOne = !newUnknown && newValue;
  if ((oldZero && !newZero) || (oldUnknown && newOne))
    result |= OBELISK_RT_SIGNAL_POSEDGE;
  if ((oldOne && !newOne) || (oldUnknown && newZero))
    result |= OBELISK_RT_SIGNAL_NEGEDGE;
  return result;
}

struct RegisteredDatabase {
  Database database;
  size_t contextCount = 0;
};

// Descriptor-only reflection calls can share validation with a live context,
// but the descriptor ABI has no cache slot or standalone lifetime token.
// Retain views only while contexts establish that the immutable descriptor and
// image are alive, and remove the final reference during context destruction.
struct DatabaseRegistry {
  std::mutex mutex;
  std::unordered_map<const obelisk_rt_execution_descriptor_v1 *,
                     RegisteredDatabase>
      databases;
};

DatabaseRegistry &databaseRegistry() {
  static DatabaseRegistry registry;
  return registry;
}

bool matchesExecution(
    const Database &database,
    const obelisk_rt_execution_descriptor_v1 *execution) noexcept {
  return execution && database.validated &&
         database.data == execution->design_database &&
         database.size == execution->design_database_size &&
         database.stateBitCount == execution->state_bit_count;
}

bool sameDatabase(const Database &left, const Database &right) noexcept {
  return left.data == right.data && left.size == right.size &&
         left.profile == right.profile && left.root == right.root &&
         left.scopes == right.scopes && left.scopeCount == right.scopeCount &&
         left.objects == right.objects &&
         left.objectCount == right.objectCount && left.types == right.types &&
         left.typeCount == right.typeCount && left.strings == right.strings &&
         left.stringSize == right.stringSize && left.index == right.index &&
         left.indexCount == right.indexCount &&
         left.statements == right.statements &&
         left.statementCount == right.statementCount &&
         left.statementSites == right.statementSites &&
         left.statementSiteCount == right.statementSiteCount &&
         left.relations == right.relations &&
         left.relationCount == right.relationCount &&
         left.semanticTypes == right.semanticTypes &&
         left.semanticTypeCount == right.semanticTypeCount &&
         left.semanticTypeEdges == right.semanticTypeEdges &&
         left.semanticTypeEdgeCount == right.semanticTypeEdgeCount &&
         left.semanticRootBindings == right.semanticRootBindings &&
         left.semanticRootBindingCount == right.semanticRootBindingCount &&
         left.relationIndices == right.relationIndices &&
         left.relationIndexCount == right.relationIndexCount &&
         left.relationIndexDimensions == right.relationIndexDimensions &&
         left.relationIndexDimensionCount ==
             right.relationIndexDimensionCount &&
         left.relationIndexKeys == right.relationIndexKeys &&
         left.relationIndexKeyCount == right.relationIndexKeyCount &&
         left.relationIndexMembers == right.relationIndexMembers &&
         left.relationIndexMemberCount == right.relationIndexMemberCount &&
         left.fixedProperties == right.fixedProperties &&
         left.fixedPropertyCount == right.fixedPropertyCount &&
         left.resolvedNetRuns == right.resolvedNetRuns &&
         left.resolvedNetRunCount == right.resolvedNetRunCount &&
         left.netDelayRuns == right.netDelayRuns &&
         left.netDelayRunCount == right.netDelayRunCount &&
         left.staticObjects == right.staticObjects &&
         left.staticObjectCount == right.staticObjectCount &&
         left.definitions == right.definitions &&
         left.definitionCount == right.definitionCount &&
         left.definitionBindings == right.definitionBindings &&
         left.definitionBindingCount == right.definitionBindingCount &&
         left.definitionMembers == right.definitionMembers &&
         left.definitionMemberCount == right.definitionMemberCount &&
         left.definitionMemberRelations == right.definitionMemberRelations &&
         left.definitionMemberRelationCount ==
             right.definitionMemberRelationCount &&
         left.definitionMemberRelationTargets ==
             right.definitionMemberRelationTargets &&
         left.definitionMemberRelationTargetCount ==
             right.definitionMemberRelationTargetCount &&
         left.definitionSpecializations == right.definitionSpecializations &&
         left.definitionSpecializationCount ==
             right.definitionSpecializationCount &&
         left.definitionSpecializationBindings ==
             right.definitionSpecializationBindings &&
         left.definitionSpecializationBindingCount ==
             right.definitionSpecializationBindingCount &&
         left.definitionMemberEndpoints == right.definitionMemberEndpoints &&
         left.definitionMemberEndpointCount ==
             right.definitionMemberEndpointCount &&
         left.definitionMemberInstanceRelations ==
             right.definitionMemberInstanceRelations &&
         left.definitionMemberInstanceRelationCount ==
             right.definitionMemberInstanceRelationCount &&
         left.definitionMemberInstanceRelationTargets ==
             right.definitionMemberInstanceRelationTargets &&
         left.definitionMemberInstanceRelationTargetCount ==
             right.definitionMemberInstanceRelationTargetCount &&
         left.definitionMemberInstanceRelationInverses ==
             right.definitionMemberInstanceRelationInverses &&
         left.definitionMemberInstanceRelationInverseCount ==
             right.definitionMemberInstanceRelationInverseCount &&
         left.frozenValues == right.frozenValues &&
         left.frozenValueCount == right.frozenValueCount &&
         left.frozenValueBindings == right.frozenValueBindings &&
         left.frozenValueBindingCount == right.frozenValueBindingCount &&
         left.frozenValuePayload == right.frozenValuePayload &&
         left.frozenValuePayloadSize == right.frozenValuePayloadSize &&
         left.stateBitCount == right.stateBitCount &&
         left.validated == right.validated;
}

bool registeredDatabase(const obelisk_rt_execution_descriptor_v1 *execution,
                        Database &database) {
  if (!execution)
    return false;
  DatabaseRegistry &registry = databaseRegistry();
  std::lock_guard<std::mutex> lock(registry.mutex);
  auto found = registry.databases.find(execution);
  if (found == registry.databases.end() ||
      !matchesExecution(found->second.database, execution))
    return false;
  database = found->second.database;
  return true;
}

obelisk_rt_status
loadValidatedDatabase(const obelisk_rt_execution_descriptor_v1 *execution,
                      Database &database) noexcept {
  OBELISK_RT_TRY {
    if (registeredDatabase(execution, database))
      return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_DESIGN; }
  if (!parseHeader(execution, database))
    return OBELISK_RT_INVALID_DESIGN;
  return validateDatabase(database);
}

obelisk_rt_status mapInvalidDatabase(obelisk_rt_status status,
                                     obelisk_rt_status invalidStatus) noexcept {
  return status == OBELISK_RT_INVALID_DESIGN ? invalidStatus : status;
}

const Database *cachedDatabase(const obelisk_rt_context *context) {
  if (!context ||
      !matchesExecution(context->designDatabase, context->execution))
    return nullptr;
  return &context->designDatabase;
}

obelisk_rt_status designRoot(const Database &database,
                             obelisk_rt_design_cursor_v1 *outCursor) {
  outCursor->offset = database.root;
  return OBELISK_RT_OK;
}

obelisk_rt_status designParent(const Database &database,
                               obelisk_rt_design_cursor_v1 cursor,
                               obelisk_rt_design_cursor_v1 *outCursor) {
  uint32_t scopeIndex = 0;
  uint32_t memberIndex = 0;
  const uint8_t *member = nullptr;
  bool refObject = false;
  if (virtualMemberForCursor(database, cursor.offset, scopeIndex, memberIndex,
                             member, nullptr, nullptr, &refObject)) {
    if (refObject) {
      *outCursor = {};
      return OBELISK_RT_EOF;
    }
    outCursor->offset = database.scopes + uint64_t{scopeIndex} * kScopeSize;
    return OBELISK_RT_OK;
  }
  if (isStaticObjectOffset(database, cursor.offset)) {
    uint32_t scope = read32(database.data + cursor.offset + 8);
    if (scope == UINT32_MAX) {
      *outCursor = {};
      return OBELISK_RT_EOF;
    }
    outCursor->offset = database.scopes + uint64_t{scope} * kScopeSize;
    return OBELISK_RT_OK;
  }
  const uint8_t *record;
  uint32_t kind;
  if (!getRecord(database, cursor.offset, record, kind) ||
      (kind != OBELISK_RT_DESIGN_RECORD_SCOPE &&
       !isObjectOffset(database, cursor.offset)))
    return OBELISK_RT_INVALID_HANDLE;
  outCursor->offset = read64(record + 16);
  return outCursor->offset == 0 ? OBELISK_RT_EOF : OBELISK_RT_OK;
}

obelisk_rt_status designChild(const Database &database,
                              obelisk_rt_design_cursor_v1 cursor,
                              obelisk_rt_design_cursor_v1 *outCursor) {
  const uint8_t *record;
  uint32_t kind;
  if (!getRecord(database, cursor.offset, record, kind) ||
      kind != OBELISK_RT_DESIGN_RECORD_SCOPE)
    return OBELISK_RT_INVALID_HANDLE;
  outCursor->offset = read64(record + 24);
  return outCursor->offset == 0 ? OBELISK_RT_EOF : OBELISK_RT_OK;
}

obelisk_rt_status designChildAt(const Database &database,
                                obelisk_rt_design_cursor_v1 cursor,
                                uint64_t index,
                                obelisk_rt_design_cursor_v1 *outCursor) {
  const uint8_t *record;
  uint32_t kind;
  if (!getRecord(database, cursor.offset, record, kind) ||
      kind != OBELISK_RT_DESIGN_RECORD_SCOPE)
    return OBELISK_RT_INVALID_HANDLE;
  uint64_t child = read64(record + 24);
  for (uint64_t ordinal = 0; ordinal != index && child != 0; ++ordinal) {
    const uint8_t *childRecord;
    uint32_t childKind;
    if (!getRecord(database, child, childRecord, childKind) ||
        childKind == OBELISK_RT_DESIGN_RECORD_TYPE)
      return OBELISK_RT_INVALID_DESIGN;
    child = nextOffset(childRecord, childKind);
  }
  outCursor->offset = child;
  return child == 0 ? OBELISK_RT_EOF : OBELISK_RT_OK;
}

obelisk_rt_status designSibling(const Database &database,
                                obelisk_rt_design_cursor_v1 cursor,
                                obelisk_rt_design_cursor_v1 *outCursor) {
  const uint8_t *record;
  uint32_t kind;
  if (!getRecord(database, cursor.offset, record, kind) ||
      kind == OBELISK_RT_DESIGN_RECORD_TYPE)
    return OBELISK_RT_INVALID_HANDLE;
  outCursor->offset = nextOffset(record, kind);
  return outCursor->offset == 0 ? OBELISK_RT_EOF : OBELISK_RT_OK;
}

obelisk_rt_status designLookup(const Database &database, const uint8_t *name,
                               uint64_t nameSize,
                               obelisk_rt_design_cursor_v1 *outCursor) {
  uint64_t wantedHash = nameHash(name, nameSize);
  uint64_t low = 0, high = database.indexCount;
  while (low != high) {
    uint64_t middle = low + (high - low) / 2;
    uint64_t hash =
        read64(database.data + database.index + middle * kIndexSize);
    if (hash < wantedHash)
      low = middle + 1;
    else
      high = middle;
  }
  for (uint64_t index = low; index != database.indexCount; ++index) {
    const uint8_t *entry = database.data + database.index + index * kIndexSize;
    if (read64(entry) != wantedHash)
      break;
    std::string_view candidate;
    if (!getString(database, read64(entry + 8), candidate))
      return OBELISK_RT_INVALID_DESIGN;
    if (candidate.size() == nameSize &&
        (nameSize == 0 || std::memcmp(candidate.data(), name, nameSize) == 0)) {
      outCursor->offset = read64(entry + 16);
      return OBELISK_RT_OK;
    }
  }
  outCursor->offset = 0;
  return OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status designInfo(const Database &database,
                             obelisk_rt_design_cursor_v1 cursor,
                             obelisk_rt_design_info_v1 *outInfo) {
  uint32_t scopeIndex = 0;
  uint32_t memberIndex = 0;
  uint32_t specializationIndex = UINT32_MAX;
  const uint8_t *member = nullptr;
  bool refObject = false;
  if (virtualMemberForCursor(database, cursor.offset, scopeIndex, memberIndex,
                             member, nullptr, &specializationIndex,
                             &refObject)) {
    *outInfo = {};
    uint16_t exactKind =
        refObject
            ? static_cast<uint16_t>(obelisk::reflection::VPIObjectKind::RefObj)
            : read16(member + 16);
    outInfo->kind = exactKind == static_cast<uint16_t>(
                                     obelisk::reflection::VPIObjectKind::IODecl)
                        ? OBELISK_RT_DESIGN_RECORD_PORT
                        : OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT;
    if (!refObject &&
        exactKind ==
            static_cast<uint16_t>(obelisk::reflection::VPIObjectKind::IODecl)) {
      uint16_t direction = read16(member + 18);
      outInfo->capabilities = static_cast<uint32_t>(direction)
                              << OBELISK_RT_DESIGN_CAP_IO_DIRECTION_SHIFT;
      switch (direction) {
      case 1:
        outInfo->capabilities |= OBELISK_RT_DESIGN_CAP_PORT_INPUT;
        break;
      case 2:
        outInfo->capabilities |= OBELISK_RT_DESIGN_CAP_PORT_OUTPUT;
        break;
      case 3:
        outInfo->capabilities |= OBELISK_RT_DESIGN_CAP_PORT_INPUT |
                                 OBELISK_RT_DESIGN_CAP_PORT_OUTPUT;
        break;
      default:
        break;
      }
    }
    uint32_t semanticType = 0;
    if (findDefinitionMemberSemanticRoot(database, specializationIndex,
                                         memberIndex, semanticType)) {
      const uint8_t *semantic = database.data + database.semanticTypes +
                                uint64_t{semanticType} * kSemanticTypeSize;
      outInfo->bit_width = read64(semantic + 48);
      outInfo->range_left = readI64(semantic + 32);
      outInfo->range_right = readI64(semantic + 40);
      uint32_t encodedSemantic = read32(semantic);
      uint32_t semanticKind = encodedSemantic & UINT32_C(0xff);
      if (outInfo->bit_width == 0 &&
          (semanticKind == OBELISK_RT_DESIGN_SEMANTIC_BIT ||
           semanticKind == OBELISK_RT_DESIGN_SEMANTIC_LOGIC ||
           semanticKind == OBELISK_RT_DESIGN_SEMANTIC_REG ||
           (encodedSemantic & OBELISK_RT_DESIGN_SEMANTIC_HAS_RANGE) != 0)) {
        uint64_t distance =
            outInfo->range_left >= outInfo->range_right
                ? static_cast<uint64_t>(outInfo->range_left) -
                      static_cast<uint64_t>(outInfo->range_right)
                : static_cast<uint64_t>(outInfo->range_right) -
                      static_cast<uint64_t>(outInfo->range_left);
        if (distance != UINT64_MAX)
          outInfo->bit_width = distance + 1;
      }
      if (outInfo->bit_width == 0)
        switch (semanticKind) {
        case OBELISK_RT_DESIGN_SEMANTIC_BYTE:
          outInfo->bit_width = 8;
          break;
        case OBELISK_RT_DESIGN_SEMANTIC_SHORT_INT:
          outInfo->bit_width = 16;
          break;
        case OBELISK_RT_DESIGN_SEMANTIC_INT:
        case OBELISK_RT_DESIGN_SEMANTIC_INTEGER:
        case OBELISK_RT_DESIGN_SEMANTIC_SHORT_REAL:
          outInfo->bit_width = 32;
          break;
        case OBELISK_RT_DESIGN_SEMANTIC_LONG_INT:
        case OBELISK_RT_DESIGN_SEMANTIC_TIME:
        case OBELISK_RT_DESIGN_SEMANTIC_REAL:
        case OBELISK_RT_DESIGN_SEMANTIC_REALTIME:
          outInfo->bit_width = 64;
          break;
        default:
          break;
        }
    }
    outInfo->handle = {OBELISK_RT_DESCRIPTOR_INVALID, 0, cursor.offset};
    return OBELISK_RT_OK;
  }
  if (isStaticObjectOffset(database, cursor.offset)) {
    const uint8_t *record = database.data + cursor.offset;
    *outInfo = {};
    outInfo->kind = OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT;
    outInfo->capabilities = read16(record + 30);
    uint16_t exactKind = read16(record + 28);
    const auto *descriptor = obelisk::reflection::findVPIObjectKind(exactKind);
    if (read32(record + 16) != 0 && descriptor &&
        (descriptor->families &
         obelisk::reflection::vpiFamilyMask(
             obelisk::reflection::VPIObjectFamily::Typespec)) != 0)
      outInfo->capabilities |= OBELISK_RT_DESIGN_CAP_NAMED_TYPESPEC;
    outInfo->handle = {OBELISK_RT_DESCRIPTOR_INVALID, 0, read64(record)};
    uint32_t packedSource = 0;
    VPIFrozenValue frozen{};
    if (packedObjectReferenceForCursor(database, cursor.offset, packedSource) &&
        findFrozenValue(database, packedSource, frozen))
      outInfo->bit_width = frozen.bitWidth;
    return OBELISK_RT_OK;
  }
  const uint8_t *record;
  uint32_t kind;
  if (!getRecord(database, cursor.offset, record, kind) ||
      kind == OBELISK_RT_DESIGN_RECORD_TYPE)
    return OBELISK_RT_INVALID_HANDLE;
  *outInfo = {};
  outInfo->kind = kind;
  outInfo->capabilities = read32(record + 4);
  outInfo->handle = {descriptorKind(kind), 0, read64(record + 8)};
  if (kind != OBELISK_RT_DESIGN_RECORD_SCOPE) {
    outInfo->type_offset = read64(record + 48);
    outInfo->bit_width = read64(record + 56);
    outInfo->range_left = readI64(record + 64);
    outInfo->range_right = readI64(record + 72);
  }
  return OBELISK_RT_OK;
}

obelisk_rt_status designTypeInfo(const Database &database,
                                 obelisk_rt_design_cursor_v1 cursor,
                                 obelisk_rt_design_type_info_v1 *outInfo) {
  const uint8_t *record;
  uint32_t recordKind;
  if (!getRecord(database, cursor.offset, record, recordKind) ||
      recordKind != OBELISK_RT_DESIGN_RECORD_TYPE)
    return OBELISK_RT_INVALID_HANDLE;
  uint32_t encoded = read32(record + 4);
  *outInfo = {};
  outInfo->kind = encoded & UINT32_C(0xff);
  outInfo->flags = encoded >> 8;
  outInfo->bit_width = read64(record + 8);
  outInfo->range_left = readI64(record + 16);
  outInfo->range_right = readI64(record + 24);
  outInfo->element_type.offset = read64(record + 32);
  outInfo->first_child.offset = read64(record + 40);
  outInfo->child_count = read64(record + 48);
  if (outInfo->kind == OBELISK_RT_DESIGN_TYPE_FIELD)
    outInfo->ordinal = read64(record + 56);
  if (outInfo->kind == OBELISK_RT_DESIGN_TYPE_UNION)
    outInfo->tag_bits = read64(record + 56);
  outInfo->packed_offset = read64(record + 64);
  return OBELISK_RT_OK;
}

obelisk_rt_status designTypeChild(const Database &database,
                                  obelisk_rt_design_cursor_v1 cursor,
                                  uint64_t index,
                                  obelisk_rt_design_cursor_v1 *outCursor) {
  const uint8_t *record;
  uint32_t recordKind;
  if (!getRecord(database, cursor.offset, record, recordKind) ||
      recordKind != OBELISK_RT_DESIGN_RECORD_TYPE)
    return OBELISK_RT_INVALID_HANDLE;
  uint64_t count = read64(record + 48);
  if (index >= count) {
    outCursor->offset = 0;
    return OBELISK_RT_EOF;
  }
  uint64_t child = read64(record + 40) + index * kTypeSize;
  if (!isTypeOffset(database, child) ||
      (read32(database.data + child + 4) & UINT32_C(0xff)) !=
          OBELISK_RT_DESIGN_TYPE_FIELD)
    return OBELISK_RT_INVALID_DESIGN;
  outCursor->offset = child;
  return OBELISK_RT_OK;
}

obelisk_rt_status designSemanticRoot(const Database &database,
                                     obelisk_rt_design_cursor_v1 object,
                                     obelisk_rt_design_cursor_v1 *outCursor) {
  uint32_t scopeIndex = 0;
  uint32_t memberIndex = 0;
  uint32_t specializationIndex = UINT32_MAX;
  const uint8_t *member = nullptr;
  if (virtualMemberForCursor(database, object.offset, scopeIndex, memberIndex,
                             member, nullptr, &specializationIndex)) {
    uint32_t root = UINT32_MAX;
    if (!findDefinitionMemberSemanticRoot(database, specializationIndex,
                                          memberIndex, root)) {
      outCursor->offset = 0;
      return OBELISK_RT_EOF;
    }
    outCursor->offset = uint64_t{root} + 1;
    return OBELISK_RT_OK;
  }
  uint32_t packedObject = 0;
  if (!packedObjectReferenceForCursor(database, object.offset, packedObject))
    return OBELISK_RT_INVALID_HANDLE;
  uint32_t root = UINT32_MAX;
  if (!findSemanticRootBinding(database, packedObject, root)) {
    outCursor->offset = 0;
    return OBELISK_RT_EOF;
  }
  if (root >= database.semanticTypeCount)
    return OBELISK_RT_INVALID_DESIGN;
  outCursor->offset = uint64_t{root} + 1;
  return OBELISK_RT_OK;
}

obelisk_rt_status
designSemanticTypeInfo(const Database &database,
                       obelisk_rt_design_cursor_v1 cursor,
                       obelisk_rt_design_semantic_type_info_v1 *outInfo) {
  if (cursor.offset == 0 || cursor.offset > database.semanticTypeCount)
    return OBELISK_RT_INVALID_HANDLE;
  const uint8_t *record = database.data + database.semanticTypes +
                          (cursor.offset - 1) * kSemanticTypeSize;
  uint32_t encoded = read32(record);
  *outInfo = {};
  outInfo->kind = encoded & UINT32_C(0xff);
  outInfo->flags = encoded & (OBELISK_RT_DESIGN_SEMANTIC_SIGNED |
                              OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE |
                              OBELISK_RT_DESIGN_SEMANTIC_HAS_RANGE |
                              OBELISK_RT_DESIGN_SEMANTIC_TAGGED |
                              OBELISK_RT_DESIGN_SEMANTIC_SOFT |
                              OBELISK_RT_DESIGN_SEMANTIC_WILDCARD_INDEX);
  outInfo->public_vpi_kind =
      (encoded & OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_MASK) >>
      OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_SHIFT;
  outInfo->first_edge = read32(record + 4);
  outInfo->edge_count = read32(record + 8);
  uint32_t aliasObject = read32(record + 12);
  uint32_t identityTarget = read32(record + 16);
  auto setObjectReference = [&](uint32_t packed,
                                obelisk_rt_design_cursor_v1 &result) {
    if (packed == UINT32_MAX) {
      result.offset = 0;
      return true;
    }
    return packedObjectReferenceOffset(database, packed, result.offset);
  };
  if (!setObjectReference(aliasObject, outInfo->alias_object) ||
      !setObjectReference(identityTarget, outInfo->identity_target))
    return OBELISK_RT_INVALID_DESIGN;
  auto setString = [&](uint32_t relative, const uint8_t *&data,
                       uint64_t &size) {
    if (relative == 0) {
      data = nullptr;
      size = 0;
      return true;
    }
    std::string_view text;
    if (!getString(database, database.strings + relative, text))
      return false;
    data = reinterpret_cast<const uint8_t *>(text.data());
    size = text.size();
    return true;
  };
  if (!setString(read32(record + 20), outInfo->name, outInfo->name_size) ||
      !setString(read32(record + 24), outInfo->modport, outInfo->modport_size))
    return OBELISK_RT_INVALID_DESIGN;
  outInfo->queue_bound = read32(record + 28);
  outInfo->range_left = readI64(record + 32);
  outInfo->range_right = readI64(record + 40);
  outInfo->bit_width = read64(record + 48);
  outInfo->tag_bits = read64(record + 56);
  return OBELISK_RT_OK;
}

obelisk_rt_status
designSemanticTypeEdge(const Database &database,
                       obelisk_rt_design_cursor_v1 cursor, uint64_t index,
                       obelisk_rt_design_semantic_type_edge_v1 *outEdge) {
  if (cursor.offset == 0 || cursor.offset > database.semanticTypeCount)
    return OBELISK_RT_INVALID_HANDLE;
  const uint8_t *type = database.data + database.semanticTypes +
                        (cursor.offset - 1) * kSemanticTypeSize;
  uint32_t first = read32(type + 4);
  uint32_t count = read32(type + 8);
  if (index >= count)
    return OBELISK_RT_EOF;
  const uint8_t *edge =
      database.data + database.semanticTypeEdges +
      uint64_t{first + static_cast<uint32_t>(index)} * kSemanticTypeEdgeSize;
  *outEdge = {};
  outEdge->child.offset = uint64_t{read32(edge)} + 1;
  uint32_t roleAndFlags = read32(edge + 4);
  outEdge->role = roleAndFlags & UINT32_C(0xff);
  outEdge->flags = roleAndFlags >> 8;
  outEdge->ordinal = read32(edge + 8);
  uint32_t name = read32(edge + 12);
  if (name != 0) {
    std::string_view text;
    if (!getString(database, database.strings + name, text))
      return OBELISK_RT_INVALID_DESIGN;
    outEdge->name = reinterpret_cast<const uint8_t *>(text.data());
    outEdge->name_size = text.size();
  }
  outEdge->packed_offset = read64(edge + 16);
  return OBELISK_RT_OK;
}

obelisk_rt_status designName(const Database &database,
                             obelisk_rt_design_cursor_v1 cursor,
                             const uint8_t **outData, uint64_t *outSize) {
  uint32_t scopeIndex = 0;
  uint32_t memberIndex = 0;
  const uint8_t *member = nullptr;
  if (virtualMemberForCursor(database, cursor.offset, scopeIndex, memberIndex,
                             member)) {
    std::string_view name;
    if (!getString(database, database.strings + read32(member), name))
      return OBELISK_RT_INVALID_DESIGN;
    *outData = reinterpret_cast<const uint8_t *>(name.data());
    *outSize = name.size();
    return OBELISK_RT_OK;
  }
  if (isStatementOffset(database, cursor.offset)) {
    const uint8_t *statement = database.data + cursor.offset;
    uint32_t relative = read32(statement + 24);
    if (relative == 0) {
      *outData = nullptr;
      *outSize = 0;
      return OBELISK_RT_OK;
    }
    std::string_view name;
    if (!getString(database, database.strings + relative, name))
      return OBELISK_RT_INVALID_HANDLE;
    *outData = reinterpret_cast<const uint8_t *>(name.data());
    *outSize = name.size();
    return OBELISK_RT_OK;
  }
  if (isStaticObjectOffset(database, cursor.offset)) {
    uint32_t relative = read32(database.data + cursor.offset + 16);
    if (relative == 0) {
      *outData = nullptr;
      *outSize = 0;
      return OBELISK_RT_OK;
    }
    std::string_view name;
    if (!getString(database, database.strings + relative, name))
      return OBELISK_RT_INVALID_HANDLE;
    *outData = reinterpret_cast<const uint8_t *>(name.data());
    *outSize = name.size();
    return OBELISK_RT_OK;
  }
  const uint8_t *record;
  uint32_t kind;
  std::string_view name;
  if (!getRecord(database, cursor.offset, record, kind) ||
      !getString(
          database,
          read64(record + (kind == OBELISK_RT_DESIGN_RECORD_TYPE ? 72 : 40)),
          name))
    return OBELISK_RT_INVALID_HANDLE;
  *outData = reinterpret_cast<const uint8_t *>(name.data());
  *outSize = name.size();
  return OBELISK_RT_OK;
}

obelisk_rt_status designVPIType(const Database &database,
                                obelisk_rt_design_cursor_v1 cursor,
                                uint32_t *outType) {
  uint32_t scopeIndex = 0;
  uint32_t memberIndex = 0;
  const uint8_t *member = nullptr;
  bool refObject = false;
  if (virtualMemberForCursor(database, cursor.offset, scopeIndex, memberIndex,
                             member, nullptr, nullptr, &refObject)) {
    *outType =
        refObject
            ? static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::RefObj)
            : read16(member + 16);
    return OBELISK_RT_OK;
  }
  if (isStatementOffset(database, cursor.offset)) {
    *outType = read16(database.data + cursor.offset + 36);
    return OBELISK_RT_OK;
  }
  if (isStaticObjectOffset(database, cursor.offset)) {
    *outType = read16(database.data + cursor.offset + 28);
    return OBELISK_RT_OK;
  }
  if (!isScopeOffset(database, cursor.offset) &&
      !isObjectOffset(database, cursor.offset))
    return OBELISK_RT_INVALID_HANDLE;
  *outType = recordVPIKind(database.data + cursor.offset);
  return OBELISK_RT_OK;
}

obelisk_rt_status designVPIFixedProperty(const Database &database,
                                         obelisk_rt_design_cursor_v1 cursor,
                                         uint32_t selector,
                                         VPIFixedPropertyValue *outValue) {
  *outValue = {};
  if (selector > UINT16_MAX)
    return OBELISK_RT_EOF;
  uint32_t virtualScope = 0;
  uint32_t virtualMember = 0;
  const uint8_t *member = nullptr;
  if (virtualMemberForCursor(database, cursor.offset, virtualScope,
                             virtualMember, member))
    return OBELISK_RT_EOF;
  obelisk::reflection::TableKind table;
  uint32_t sourceIndex = 0;
  if (!relationSourceForCursor(database, cursor.offset, table, sourceIndex))
    return OBELISK_RT_INVALID_HANDLE;
  uint32_t packedSource = 0;
  if (!obelisk::reflection::tryPackTableIndex(table, sourceIndex, packedSource))
    return OBELISK_RT_INVALID_HANDLE;

  // Definition-backed properties are the common path for elaborated module,
  // interface, and program instances. Validated images cannot also carry a
  // fixed row for these selectors on a bound source, so avoid a guaranteed
  // miss in the larger general property table.
  if (selector == 9 || selector == 15 || selector == 16) {
    uint32_t low = 0;
    uint32_t high = static_cast<uint32_t>(database.definitionBindingCount);
    while (low != high) {
      uint32_t middle = low + (high - low) / 2;
      const uint8_t *binding = database.data + database.definitionBindings +
                               uint64_t{middle} * kDefinitionBindingSize;
      if (read32(binding) < packedSource)
        low = middle + 1;
      else
        high = middle;
    }
    if (low != database.definitionBindingCount) {
      const uint8_t *binding = database.data + database.definitionBindings +
                               uint64_t{low} * kDefinitionBindingSize;
      if (read32(binding) == packedSource) {
        uint32_t definitionIndex = read32(binding + 4);
        if (definitionIndex >= database.definitionCount)
          return OBELISK_RT_INVALID_DESIGN;
        const uint8_t *definition = database.data + database.definitions +
                                    uint64_t{definitionIndex} * kDefinitionSize;
        if (selector == 16) {
          uint32_t line = read32(definition + 12);
          if (line == 0)
            return OBELISK_RT_EOF;
          outValue->kind = static_cast<uint8_t>(
              obelisk::reflection::VPIPropertyValueKind::Integer);
          outValue->payload = line;
          return OBELISK_RT_OK;
        }
        uint32_t relative = read32(definition + (selector == 9 ? 4 : 8));
        if (relative == 0)
          return OBELISK_RT_EOF;
        outValue->kind = static_cast<uint8_t>(
            obelisk::reflection::VPIPropertyValueKind::String);
        outValue->payload = database.strings + relative;
        std::string_view value;
        if (!getString(database, outValue->payload, value))
          return OBELISK_RT_INVALID_DESIGN;
        outValue->stringData = reinterpret_cast<const uint8_t *>(value.data());
        outValue->stringSize = value.size();
        return OBELISK_RT_OK;
      }
    }
  }

  uint32_t low = 0;
  uint32_t high = static_cast<uint32_t>(database.fixedPropertyCount);
  uint16_t compactSelector = static_cast<uint16_t>(selector);
  while (low != high) {
    uint32_t middle = low + (high - low) / 2;
    const uint8_t *record = database.data + database.fixedProperties +
                            uint64_t{middle} * kFixedPropertySize;
    uint32_t recordSource = read32(record);
    uint16_t recordSelector = read16(record + 4);
    if (recordSource < packedSource ||
        (recordSource == packedSource && recordSelector < compactSelector))
      low = middle + 1;
    else
      high = middle;
  }
  if (low != database.fixedPropertyCount) {
    const uint8_t *record = database.data + database.fixedProperties +
                            uint64_t{low} * kFixedPropertySize;
    if (read32(record) == packedSource &&
        read16(record + 4) == compactSelector) {
      outValue->kind = static_cast<uint8_t>(read16(record + 6));
      outValue->payload = read64(record + 8);
      if (outValue->kind ==
          static_cast<uint8_t>(
              obelisk::reflection::VPIPropertyValueKind::String)) {
        std::string_view value;
        if (!getString(database, outValue->payload, value))
          return OBELISK_RT_INVALID_DESIGN;
        outValue->stringData = reinterpret_cast<const uint8_t *>(value.data());
        outValue->stringSize = value.size();
      }
      return OBELISK_RT_OK;
    }
  }

  return OBELISK_RT_EOF;
}

obelisk_rt_status designVPIResolvedNetType(const Database &database,
                                           obelisk_rt_design_cursor_v1 cursor,
                                           uint64_t bitOffset,
                                           uint64_t bitWidth,
                                           uint32_t *outType) {
  if (!isObjectOffset(database, cursor.offset) || bitWidth == 0)
    return OBELISK_RT_INVALID_HANDLE;
  uint32_t objectIndex =
      static_cast<uint32_t>((cursor.offset - database.objects) / kObjectSize);
  const uint8_t *object = database.data + cursor.offset;
  uint64_t width = read64(object + 56);
  if (recordKind(object) != OBELISK_RT_DESIGN_RECORD_NET || bitOffset > width ||
      bitWidth > width - bitOffset)
    return OBELISK_RT_INVALID_HANDLE;
  obelisk_rt_status simulated =
      simulatedNetObjectIndex(database, objectIndex, objectIndex);
  if (simulated != OBELISK_RT_OK)
    return simulated;
  object =
      database.data + database.objects + uint64_t{objectIndex} * kObjectSize;
  uint64_t simulatedWidth = read64(object + 56);
  if (bitOffset > simulatedWidth || bitWidth > simulatedWidth - bitOffset)
    return OBELISK_RT_INVALID_DESIGN;

  // Find the last run whose start is not after the selected first bit.
  uint64_t low = 0;
  uint64_t high = database.resolvedNetRunCount;
  while (low != high) {
    uint64_t middle = low + (high - low) / 2;
    const uint8_t *run = database.data + database.resolvedNetRuns +
                         uint64_t{middle} * kResolvedNetRunSize;
    uint32_t runObject = read32(run);
    uint64_t runFirst = read64(run + 8);
    if (runObject < objectIndex ||
        (runObject == objectIndex && runFirst <= bitOffset))
      low = middle + 1;
    else
      high = middle;
  }
  if (low == 0)
    return OBELISK_RT_EOF;
  uint64_t index = low - 1;
  uint64_t selectedEnd = bitOffset + bitWidth;
  uint64_t covered = bitOffset;
  uint32_t selectedType = 0;
  while (covered != selectedEnd && index < database.resolvedNetRunCount) {
    const uint8_t *run = database.data + database.resolvedNetRuns +
                         uint64_t{index} * kResolvedNetRunSize;
    uint32_t runObject = read32(run);
    uint32_t runType = read32(run + 4);
    uint64_t runFirst = read64(run + 8);
    uint64_t runEnd = runFirst + read64(run + 16);
    if (runObject != objectIndex || runFirst > covered || runEnd <= covered ||
        (selectedType != 0 && runType != selectedType))
      return OBELISK_RT_EOF;
    selectedType = runType;
    covered = std::min(selectedEnd, runEnd);
    ++index;
  }
  if (covered != selectedEnd || selectedType == 0)
    return OBELISK_RT_EOF;
  *outType = selectedType;
  return OBELISK_RT_OK;
}

obelisk_rt_status designVPINetDelay(const Database &database,
                                    obelisk_rt_design_cursor_v1 cursor,
                                    uint64_t bitOffset, uint64_t bitWidth,
                                    VPINetDelayValue *outDelay) {
  if (!isObjectOffset(database, cursor.offset) || bitWidth == 0)
    return OBELISK_RT_INVALID_HANDLE;
  uint32_t objectIndex =
      static_cast<uint32_t>((cursor.offset - database.objects) / kObjectSize);
  const uint8_t *object = database.data + cursor.offset;
  uint64_t width = read64(object + 56);
  if (recordKind(object) != OBELISK_RT_DESIGN_RECORD_NET || bitOffset > width ||
      bitWidth > width - bitOffset)
    return OBELISK_RT_INVALID_HANDLE;
  obelisk_rt_status simulated =
      simulatedNetObjectIndex(database, objectIndex, objectIndex);
  if (simulated != OBELISK_RT_OK)
    return simulated;
  object =
      database.data + database.objects + uint64_t{objectIndex} * kObjectSize;
  uint64_t simulatedWidth = read64(object + 56);
  if (bitOffset > simulatedWidth || bitWidth > simulatedWidth - bitOffset)
    return OBELISK_RT_INVALID_DESIGN;

  uint64_t low = 0;
  uint64_t high = database.netDelayRunCount;
  while (low != high) {
    uint64_t middle = low + (high - low) / 2;
    const uint8_t *run =
        database.data + database.netDelayRuns + middle * kNetDelayRunSize;
    uint32_t runObject = read32(run);
    uint64_t runFirst = read64(run + 8);
    if (runObject < objectIndex ||
        (runObject == objectIndex && runFirst <= bitOffset))
      low = middle + 1;
    else
      high = middle;
  }
  uint64_t index = low;
  uint64_t selectedEnd = bitOffset + bitWidth;
  uint64_t covered = bitOffset;
  VPINetDelayValue selected{};
  if (low != 0) {
    const uint8_t *previous =
        database.data + database.netDelayRuns + (low - 1) * kNetDelayRunSize;
    uint64_t previousFirst = read64(previous + 8);
    uint64_t previousEnd = previousFirst + read64(previous + 16);
    if (read32(previous) == objectIndex && previousFirst <= bitOffset &&
        previousEnd > bitOffset) {
      selected = {readI64(previous + 24), readI64(previous + 32),
                  readI64(previous + 40)};
      covered = std::min(selectedEnd, previousEnd);
    }
  }
  while (covered != selectedEnd && index < database.netDelayRunCount) {
    const uint8_t *run =
        database.data + database.netDelayRuns + index * kNetDelayRunSize;
    uint32_t runObject = read32(run);
    uint64_t runFirst = read64(run + 8);
    uint64_t runEnd = runFirst + read64(run + 16);
    VPINetDelayValue delay{readI64(run + 24), readI64(run + 32),
                           readI64(run + 40)};
    if (runObject != objectIndex || runFirst >= selectedEnd)
      break;
    if (runEnd <= covered) {
      ++index;
      continue;
    }
    // A gap is an explicit zero-delay region.
    if (runFirst > covered) {
      if (selected.rise != 0 || selected.fall != 0 || selected.third != 0)
        return OBELISK_RT_EOF;
      covered = runFirst;
    }
    if (selected.rise != delay.rise || selected.fall != delay.fall ||
        selected.third != delay.third)
      return OBELISK_RT_EOF;
    covered = std::min(selectedEnd, runEnd);
    ++index;
  }
  if (covered != selectedEnd &&
      (selected.rise != 0 || selected.fall != 0 || selected.third != 0))
    return OBELISK_RT_EOF;
  *outDelay = selected;
  return OBELISK_RT_OK;
}

obelisk_rt_status designVPIRelationIndex(const Database &database,
                                         obelisk_rt_design_cursor_v1 source,
                                         VPIRelationIndexInfo *outInfo) {
  *outInfo = {};
  if (!isObjectOffset(database, source.offset))
    return OBELISK_RT_INVALID_HANDLE;
  uint32_t objectIndex =
      static_cast<uint32_t>((source.offset - database.objects) / kObjectSize);
  uint32_t low = 0;
  uint32_t high = static_cast<uint32_t>(database.relationIndexCount);
  while (low != high) {
    uint32_t middle = low + (high - low) / 2;
    const uint8_t *record = database.data + database.relationIndices +
                            uint64_t{middle} * kRelationIndexSize;
    if (read32(record) < objectIndex)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == database.relationIndexCount)
    return OBELISK_RT_EOF;
  const uint8_t *record = database.data + database.relationIndices +
                          uint64_t{low} * kRelationIndexSize;
  if (read32(record) != objectIndex)
    return OBELISK_RT_EOF;
  uint64_t elementCount = read64(database.data + database.objects +
                                 uint64_t{objectIndex} * kObjectSize + 56);
  outInfo->firstDimension = read32(record + 4);
  outInfo->dimensionCount = read16(record + 8);
  outInfo->sparse = read16(record + 10) != 0;
  outInfo->firstKey = read32(record + 12);
  outInfo->firstOrdinalKey = read32(record + 16);
  outInfo->elementCount = static_cast<uint32_t>(elementCount);
  return OBELISK_RT_OK;
}

obelisk_rt_status designVPIRelationIndexDimension(
    const Database &database, const VPIRelationIndexInfo &info,
    uint32_t dimension, int64_t *outLeft, int64_t *outRight) {
  if (info.sparse || dimension >= info.dimensionCount ||
      info.firstDimension > database.relationIndexDimensionCount ||
      dimension >= database.relationIndexDimensionCount - info.firstDimension)
    return OBELISK_RT_INVALID_HANDLE;
  const uint8_t *record =
      database.data + database.relationIndexDimensions +
      uint64_t{info.firstDimension + dimension} * kRelationIndexDimensionSize;
  *outLeft = readI64(record);
  *outRight = readI64(record + 8);
  return OBELISK_RT_OK;
}

obelisk_rt_status designVPIRelationIndexKey(const Database &database,
                                            const VPIRelationIndexInfo &info,
                                            int64_t index,
                                            uint32_t *outOrdinal) {
  if (!info.sparse || info.firstKey > database.relationIndexKeyCount ||
      info.elementCount > database.relationIndexKeyCount - info.firstKey)
    return OBELISK_RT_INVALID_HANDLE;
  uint32_t low = 0;
  uint32_t high = info.elementCount;
  while (low != high) {
    uint32_t middle = low + (high - low) / 2;
    const uint8_t *record =
        database.data + database.relationIndexKeys +
        uint64_t{info.firstKey + middle} * kRelationIndexKeySize;
    if (readI64(record) < index)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == info.elementCount)
    return OBELISK_RT_EOF;
  const uint8_t *record = database.data + database.relationIndexKeys +
                          uint64_t{info.firstKey + low} * kRelationIndexKeySize;
  if (readI64(record) != index)
    return OBELISK_RT_EOF;
  *outOrdinal = read32(record + 8);
  return OBELISK_RT_OK;
}

obelisk_rt_status
designVPIRelationIndexOrdinalKey(const Database &database,
                                 const VPIRelationIndexInfo &info,
                                 uint32_t ordinal, int64_t *outIndex) {
  if (!info.sparse || ordinal >= info.elementCount ||
      info.firstOrdinalKey > database.relationIndexKeyCount ||
      info.elementCount > database.relationIndexKeyCount - info.firstOrdinalKey)
    return OBELISK_RT_INVALID_HANDLE;
  const uint8_t *record =
      database.data + database.relationIndexKeys +
      uint64_t{info.firstOrdinalKey + ordinal} * kRelationIndexKeySize;
  if (read32(record + 8) != ordinal)
    return OBELISK_RT_INVALID_DESIGN;
  *outIndex = readI64(record);
  return OBELISK_RT_OK;
}

obelisk_rt_status designVPIArrayMember(const Database &database,
                                       obelisk_rt_design_cursor_v1 member,
                                       VPIArrayMemberInfo *outInfo) {
  *outInfo = {};
  obelisk::reflection::TableKind targetTable;
  uint32_t targetIndex = 0;
  if (!relationSourceForCursor(database, member.offset, targetTable,
                               targetIndex) ||
      targetTable == obelisk::reflection::TableKind::Statement)
    return OBELISK_RT_INVALID_HANDLE;
  uint32_t packedTarget = 0;
  if (!obelisk::reflection::tryPackTableIndex(targetTable, targetIndex,
                                              packedTarget))
    return OBELISK_RT_INVALID_HANDLE;
  uint32_t low = 0;
  uint32_t high = static_cast<uint32_t>(database.relationIndexMemberCount);
  while (low != high) {
    uint32_t middle = low + (high - low) / 2;
    const uint8_t *record = database.data + database.relationIndexMembers +
                            uint64_t{middle} * kRelationIndexMemberSize;
    if (read32(record) < packedTarget)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == database.relationIndexMemberCount)
    return OBELISK_RT_EOF;
  const uint8_t *record = database.data + database.relationIndexMembers +
                          uint64_t{low} * kRelationIndexMemberSize;
  if (read32(record) != packedTarget)
    return OBELISK_RT_EOF;
  uint32_t relationIndex = read32(record + 4);
  const uint8_t *indexRecord = database.data + database.relationIndices +
                               uint64_t{relationIndex} * kRelationIndexSize;
  uint32_t arrayIndex = read32(indexRecord);
  outInfo->array.offset = database.objects + uint64_t{arrayIndex} * kObjectSize;
  outInfo->arrayType = recordVPIKind(database.data + outInfo->array.offset);
  outInfo->ordinal = read32(record + 8);
  return designVPIRelationIndex(database, outInfo->array, &outInfo->index);
}

bool findDefinitionMemberInstanceInverse(const Database &database,
                                         uint32_t packedPort,
                                         uint16_t forwardSelector,
                                         uint32_t &bindingIndex,
                                         uint32_t &memberIndex) {
  uint32_t low = 0;
  uint32_t high = static_cast<uint32_t>(
      database.definitionMemberInstanceRelationInverseCount);
  while (low != high) {
    uint32_t middle = low + (high - low) / 2;
    const uint8_t *inverse =
        database.data + database.definitionMemberInstanceRelationInverses +
        uint64_t{middle} * kDefinitionMemberInstanceRelationInverseSize;
    uint32_t targetIndex = read32(inverse);
    const uint8_t *target =
        database.data + database.definitionMemberInstanceRelationTargets +
        uint64_t{targetIndex} * kDefinitionMemberInstanceRelationTargetSize;
    if (read32(target) < packedPort)
      low = middle + 1;
    else
      high = middle;
  }
  for (uint32_t inverseIndex = low;
       inverseIndex != database.definitionMemberInstanceRelationInverseCount;
       ++inverseIndex) {
    const uint8_t *inverse =
        database.data + database.definitionMemberInstanceRelationInverses +
        uint64_t{inverseIndex} * kDefinitionMemberInstanceRelationInverseSize;
    uint32_t targetIndex = read32(inverse);
    const uint8_t *target =
        database.data + database.definitionMemberInstanceRelationTargets +
        uint64_t{targetIndex} * kDefinitionMemberInstanceRelationTargetSize;
    if (read32(target) != packedPort)
      break;
    uint32_t relationLow = 0;
    uint32_t relationHigh =
        static_cast<uint32_t>(database.definitionMemberInstanceRelationCount);
    while (relationLow != relationHigh) {
      uint32_t middle = relationLow + (relationHigh - relationLow) / 2;
      const uint8_t *relation =
          database.data + database.definitionMemberInstanceRelations +
          uint64_t{middle} * kDefinitionMemberInstanceRelationSize;
      if (targetIndex >= read32(relation + 12) + read32(relation + 16))
        relationLow = middle + 1;
      else
        relationHigh = middle;
    }
    if (relationLow == database.definitionMemberInstanceRelationCount)
      return false;
    const uint8_t *relation =
        database.data + database.definitionMemberInstanceRelations +
        uint64_t{relationLow} * kDefinitionMemberInstanceRelationSize;
    uint16_t reverseSelector = read16(relation + 8);
    const auto *reverseEdge = obelisk::reflection::findVPITraversal(
        static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::RefObj),
        reverseSelector, obelisk::reflection::VPITraversalMode::Iterate);
    if (!reverseEdge || reverseEdge->inverseSelector != forwardSelector)
      continue;
    uint32_t memberAndFlags = read32(relation + 4);
    if ((memberAndFlags & kVirtualRefObjectFlag) == 0)
      return false;
    bindingIndex = read32(relation);
    memberIndex = memberAndFlags & ~kVirtualRefObjectFlag;
    return true;
  }
  return false;
}

obelisk_rt_status designVPIRelationRange(const Database &database,
                                         obelisk_rt_design_cursor_v1 source,
                                         uint32_t selector, bool iterate,
                                         VPIRelationRange *outRange) {
  *outRange = {};
  if (selector > UINT16_MAX)
    return OBELISK_RT_EOF;
  auto mode = iterate ? obelisk::reflection::VPITraversalMode::Iterate
                      : obelisk::reflection::VPITraversalMode::Handle;
  uint32_t virtualScope = 0;
  uint32_t virtualMember = 0;
  uint32_t virtualBinding = 0;
  const uint8_t *member = nullptr;
  bool virtualRefObject = false;
  if (virtualMemberForCursor(database, source.offset, virtualScope,
                             virtualMember, member, &virtualBinding, nullptr,
                             &virtualRefObject)) {
    uint32_t sourceType =
        virtualRefObject
            ? static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::RefObj)
            : read16(member + 16);
    const auto *edge =
        obelisk::reflection::findVPITraversal(sourceType, selector, mode);
    if (!edge)
      return OBELISK_RT_EOF;
    if (virtualRefObject) {
      if (edge->automaticRelation == obelisk::reflection::VPIAutomaticRelation::
                                         DefinitionMemberInstanceRelation) {
        uint32_t memberAndFlags = kVirtualRefObjectFlag | virtualMember;
        uint16_t compactSelector = static_cast<uint16_t>(selector);
        uint16_t compactMode = iterate ? 1 : 0;
        uint32_t low = 0;
        uint32_t high = static_cast<uint32_t>(
            database.definitionMemberInstanceRelationCount);
        while (low != high) {
          uint32_t middle = low + (high - low) / 2;
          const uint8_t *relation =
              database.data + database.definitionMemberInstanceRelations +
              uint64_t{middle} * kDefinitionMemberInstanceRelationSize;
          auto recordKey =
              std::make_tuple(read32(relation), read32(relation + 4),
                              read16(relation + 8), read16(relation + 10));
          auto wantedKey = std::make_tuple(virtualBinding, memberAndFlags,
                                           compactSelector, compactMode);
          if (recordKey < wantedKey)
            low = middle + 1;
          else
            high = middle;
        }
        if (low == database.definitionMemberInstanceRelationCount)
          return OBELISK_RT_EOF;
        const uint8_t *relation =
            database.data + database.definitionMemberInstanceRelations +
            uint64_t{low} * kDefinitionMemberInstanceRelationSize;
        if (read32(relation) != virtualBinding ||
            read32(relation + 4) != memberAndFlags ||
            read16(relation + 8) != compactSelector ||
            read16(relation + 10) != compactMode)
          return OBELISK_RT_EOF;
        outRange->first = virtualToken(
            kVirtualForwardRelationTag, virtualBinding,
            kVirtualInstanceRelationTargetFlag | read32(relation + 12));
        outRange->count = read32(relation + 16);
        return OBELISK_RT_OK;
      }
      if (edge->automaticRelation ==
          obelisk::reflection::VPIAutomaticRelation::DefinitionMemberParent) {
        outRange->first = virtualToken(kVirtualReverseRelationTag,
                                       virtualBinding, virtualMember);
        outRange->count = 1;
        return OBELISK_RT_OK;
      }
      if (edge->automaticRelation !=
          obelisk::reflection::VPIAutomaticRelation::DefinitionMemberExpr)
        return OBELISK_RT_EOF;
    }
    if (edge->automaticRelation ==
        obelisk::reflection::VPIAutomaticRelation::DefinitionMemberParent) {
      outRange->first = virtualToken(kVirtualReverseRelationTag, virtualBinding,
                                     virtualMember);
      outRange->count = 1;
      return OBELISK_RT_OK;
    }
    if (!virtualRefObject &&
        edge->automaticRelation !=
            obelisk::reflection::VPIAutomaticRelation::DefinitionMemberExpr)
      return OBELISK_RT_EOF;
    const uint8_t *binding = database.data + database.definitionBindings +
                             uint64_t{virtualBinding} * kDefinitionBindingSize;
    uint32_t definitionIndex = read32(binding + 4);
    if (definitionIndex >= database.definitionCount)
      return OBELISK_RT_INVALID_DESIGN;
    const uint8_t *definition = database.data + database.definitions +
                                uint64_t{definitionIndex} * kDefinitionSize;
    uint32_t firstMember = read32(definition + 16);
    uint32_t memberCount = read32(definition + 20);
    if (virtualMember < firstMember ||
        virtualMember - firstMember >= memberCount)
      return OBELISK_RT_INVALID_HANDLE;
    uint32_t endpointIndex =
        read32(binding + 12) + (virtualMember - firstMember);
    if (endpointIndex >= database.definitionMemberEndpointCount)
      return OBELISK_RT_INVALID_DESIGN;
    const uint8_t *endpoint =
        database.data + database.definitionMemberEndpoints +
        uint64_t{endpointIndex} * kDefinitionMemberEndpointSize;
    if (read32(endpoint) == UINT32_MAX)
      return OBELISK_RT_EOF;
    uint32_t relationPayload = kVirtualEndpointRelationFlag | endpointIndex;
    if (!virtualRefObject && read16(member + 18) == kVPIIODirectionRef)
      relationPayload = kVirtualRefObjectRelationFlag | virtualMember;
    outRange->first = virtualToken(kVirtualReverseRelationTag, virtualBinding,
                                   relationPayload);
    outRange->count = 1;
    return OBELISK_RT_OK;
  }
  obelisk::reflection::TableKind table;
  uint32_t sourceIndex = 0;
  if (!relationSourceForCursor(database, source.offset, table, sourceIndex))
    return OBELISK_RT_INVALID_HANDLE;
  uint16_t compactSelector = static_cast<uint16_t>(selector);

  if (!iterate && table == obelisk::reflection::TableKind::Object &&
      (compactSelector ==
           static_cast<uint16_t>(
               obelisk::reflection::VPIRelationKind::LowConnRel) ||
       compactSelector ==
           static_cast<uint16_t>(
               obelisk::reflection::VPIRelationKind::HighConnRel))) {
    const uint8_t *record =
        database.data + database.objects + uint64_t{sourceIndex} * kObjectSize;
    uint32_t packedPort = 0;
    uint32_t bindingIndex = 0;
    uint32_t memberIndex = 0;
    if (recordVPIKind(record) ==
            static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::Port) &&
        obelisk::reflection::tryPackTableIndex(table, sourceIndex,
                                               packedPort) &&
        findDefinitionMemberInstanceInverse(
            database, packedPort, compactSelector, bindingIndex, memberIndex)) {
      outRange->first =
          virtualToken(kVirtualReverseRelationTag, bindingIndex,
                       kVirtualRefObjectRelationFlag | memberIndex);
      outRange->count = 1;
      return OBELISK_RT_OK;
    }
  }

  if (table == obelisk::reflection::TableKind::Scope) {
    uint16_t sourceType = static_cast<uint16_t>(recordVPIKind(
        database.data + database.scopes + uint64_t{sourceIndex} * kScopeSize));
    const auto *edge = obelisk::reflection::findVPITraversal(
        sourceType, compactSelector, mode);
    if (edge &&
        edge->automaticRelation ==
            obelisk::reflection::VPIAutomaticRelation::DefinitionMember) {
      uint32_t definitionIndex = 0;
      uint32_t specializationIndex = 0;
      uint32_t bindingIndex = 0;
      if (!findDefinitionBinding(database, sourceIndex, definitionIndex,
                                 specializationIndex, &bindingIndex))
        return OBELISK_RT_EOF;
      const uint8_t *definition = database.data + database.definitions +
                                  uint64_t{definitionIndex} * kDefinitionSize;
      uint32_t first = read32(definition + 24);
      uint32_t count = read32(definition + 28);
      uint32_t low = first;
      uint32_t high = first + count;
      while (low != high) {
        uint32_t middle = low + (high - low) / 2;
        const uint8_t *relation =
            database.data + database.definitionMemberRelations +
            uint64_t{middle} * kDefinitionMemberRelationSize;
        if (read16(relation) < compactSelector)
          low = middle + 1;
        else
          high = middle;
      }
      if (low == first + count)
        return OBELISK_RT_EOF;
      const uint8_t *relation = database.data +
                                database.definitionMemberRelations +
                                uint64_t{low} * kDefinitionMemberRelationSize;
      if (read16(relation) != compactSelector)
        return OBELISK_RT_EOF;
      outRange->first = virtualToken(kVirtualForwardRelationTag, bindingIndex,
                                     read32(relation + 4));
      outRange->count = read32(relation + 8);
      return OBELISK_RT_OK;
    }
  }

  uint64_t first = lowerBoundRelation(database, table, sourceIndex,
                                      compactSelector, iterate);
  if (first == database.relationCount)
    return OBELISK_RT_EOF;
  auto relation = relationAt(database, first);
  if (!relationMatches(relation, table, sourceIndex, compactSelector, iterate))
    return OBELISK_RT_EOF;
  uint16_t sourceType = obelisk::reflection::unpackRelationSourceKind(
      relation.getSourceKindAndTable());
  const auto *edge =
      obelisk::reflection::findVPITraversal(sourceType, selector, mode);
  if (!edge)
    return OBELISK_RT_EOF;
  uint64_t end = upperBoundRelation(database, first, table, sourceIndex,
                                    compactSelector, iterate);
  outRange->first = first;
  outRange->count = iterate ? end - first : 1;
  return OBELISK_RT_OK;
}

obelisk_rt_status
designVPIRelationTarget(const Database &database, uint64_t relationIndex,
                        obelisk_rt_design_cursor_v1 *outCursor,
                        uint32_t *outType, bool *outStatement) {
  uint32_t bindingIndex = 0;
  uint32_t payload = 0;
  if (decodeVirtualToken(relationIndex, kVirtualForwardRelationTag,
                         bindingIndex, payload)) {
    if ((payload & kVirtualInstanceRelationTargetFlag) != 0) {
      uint32_t targetIndex = payload & ~kVirtualInstanceRelationTargetFlag;
      if (bindingIndex >= database.definitionBindingCount ||
          targetIndex >= database.definitionMemberInstanceRelationTargetCount)
        return OBELISK_RT_INVALID_HANDLE;
      const uint8_t *target =
          database.data + database.definitionMemberInstanceRelationTargets +
          uint64_t{targetIndex} * kDefinitionMemberInstanceRelationTargetSize;
      uint32_t packedTarget = read32(target);
      if (!packedObjectReferenceOffset(database, packedTarget,
                                       outCursor->offset))
        return OBELISK_RT_INVALID_DESIGN;
      auto table = obelisk::reflection::unpackTableIndexKind(packedTarget);
      *outStatement = table == obelisk::reflection::TableKind::Statement;
      *outType = *outStatement
                     ? read16(database.data + outCursor->offset + 36)
                     : recordVPIKind(database.data + outCursor->offset);
      return OBELISK_RT_OK;
    }
    if (bindingIndex >= database.definitionBindingCount ||
        payload >= database.definitionMemberRelationTargetCount)
      return OBELISK_RT_INVALID_HANDLE;
    const uint8_t *target =
        database.data + database.definitionMemberRelationTargets +
        uint64_t{payload} * kDefinitionMemberRelationTargetSize;
    uint32_t memberIndex = read32(target);
    const uint8_t *member = nullptr;
    uint32_t scopeIndex = 0;
    uint32_t checkedMember = 0;
    uint64_t cursor =
        virtualToken(kVirtualMemberTag, bindingIndex, memberIndex);
    if (!virtualMemberForCursor(database, cursor, scopeIndex, checkedMember,
                                member))
      return OBELISK_RT_INVALID_HANDLE;
    outCursor->offset = cursor;
    *outType = read16(member + 16);
    *outStatement = false;
    return OBELISK_RT_OK;
  }
  if (decodeVirtualToken(relationIndex, kVirtualReverseRelationTag,
                         bindingIndex, payload)) {
    if ((payload & kVirtualEndpointRelationFlag) != 0) {
      uint32_t endpointIndex = payload & ~kVirtualEndpointRelationFlag;
      if (bindingIndex >= database.definitionBindingCount ||
          endpointIndex >= database.definitionMemberEndpointCount)
        return OBELISK_RT_INVALID_HANDLE;
      const uint8_t *endpoint =
          database.data + database.definitionMemberEndpoints +
          uint64_t{endpointIndex} * kDefinitionMemberEndpointSize;
      uint32_t packedTarget = read32(endpoint);
      if (packedTarget == UINT32_MAX ||
          !packedObjectReferenceOffset(database, packedTarget,
                                       outCursor->offset))
        return OBELISK_RT_INVALID_DESIGN;
      auto table = obelisk::reflection::unpackTableIndexKind(packedTarget);
      *outStatement = table == obelisk::reflection::TableKind::Statement;
      *outType = *outStatement
                     ? read16(database.data + outCursor->offset + 36)
                     : recordVPIKind(database.data + outCursor->offset);
      return OBELISK_RT_OK;
    }
    if ((payload & kVirtualRefObjectRelationFlag) != 0) {
      uint32_t memberIndex = payload & ~kVirtualRefObjectRelationFlag;
      const uint8_t *member = nullptr;
      uint32_t scopeIndex = 0;
      uint32_t checkedMember = 0;
      uint64_t cursor = virtualToken(kVirtualMemberTag, bindingIndex,
                                     kVirtualRefObjectFlag | memberIndex);
      if (!virtualMemberForCursor(database, cursor, scopeIndex, checkedMember,
                                  member) ||
          read16(member + 16) !=
              static_cast<uint16_t>(
                  obelisk::reflection::VPIObjectKind::IODecl) ||
          read16(member + 18) != kVPIIODirectionRef)
        return OBELISK_RT_INVALID_HANDLE;
      outCursor->offset = cursor;
      *outType =
          static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::RefObj);
      *outStatement = false;
      return OBELISK_RT_OK;
    }
    const uint8_t *member = nullptr;
    uint32_t scopeIndex = 0;
    uint32_t checkedMember = 0;
    uint64_t cursor = virtualToken(kVirtualMemberTag, bindingIndex, payload);
    if (!virtualMemberForCursor(database, cursor, scopeIndex, checkedMember,
                                member))
      return OBELISK_RT_INVALID_HANDLE;
    outCursor->offset = database.scopes + uint64_t{scopeIndex} * kScopeSize;
    *outType = recordVPIKind(database.data + outCursor->offset);
    *outStatement = false;
    return OBELISK_RT_OK;
  }
  if (relationIndex >= database.relationCount)
    return OBELISK_RT_INVALID_HANDLE;
  auto relation = relationAt(database, relationIndex);
  uint32_t packedTarget = relation.getTargetIndexAndTable();
  auto table = obelisk::reflection::unpackTableIndexKind(packedTarget);
  uint32_t target = obelisk::reflection::unpackTableIndex(packedTarget);
  *outStatement = false;
  switch (table) {
  case obelisk::reflection::TableKind::Scope:
    if (target >= database.scopeCount)
      return OBELISK_RT_INVALID_DESIGN;
    outCursor->offset = database.scopes + uint64_t{target} * kScopeSize;
    *outType = recordVPIKind(database.data + outCursor->offset);
    return OBELISK_RT_OK;
  case obelisk::reflection::TableKind::Object:
    if (target >= database.objectCount)
      return OBELISK_RT_INVALID_DESIGN;
    outCursor->offset = database.objects + uint64_t{target} * kObjectSize;
    *outType = recordVPIKind(database.data + outCursor->offset);
    return OBELISK_RT_OK;
  case obelisk::reflection::TableKind::Statement:
    if (target >= database.statementCount)
      return OBELISK_RT_INVALID_DESIGN;
    outCursor->offset = database.statements + uint64_t{target} * kStatementSize;
    *outType = read16(database.data + outCursor->offset + 36);
    *outStatement = true;
    return OBELISK_RT_OK;
  case obelisk::reflection::TableKind::StaticObject:
    if (target >= database.staticObjectCount)
      return OBELISK_RT_INVALID_DESIGN;
    outCursor->offset =
        database.staticObjects + uint64_t{target} * kStaticObjectSize;
    *outType = read16(database.data + outCursor->offset + 28);
    return OBELISK_RT_OK;
  }
  // The database validator rejects reserved table tags before publication.
  return OBELISK_RT_INVALID_DESIGN;
}

obelisk_rt_status
designVPIStatementScope(const Database &database,
                        obelisk_rt_design_cursor_v1 statement,
                        obelisk_rt_design_cursor_v1 *outScope) {
  if (!isStatementOffset(database, statement.offset))
    return OBELISK_RT_INVALID_HANDLE;
  uint32_t scope = read32(database.data + statement.offset + 12);
  if (scope >= database.scopeCount)
    return OBELISK_RT_INVALID_DESIGN;
  outScope->offset = database.scopes + uint64_t{scope} * kScopeSize;
  return OBELISK_RT_OK;
}

obelisk_rt_status designVPIStatementEnclosingScope(
    const Database &database, obelisk_rt_design_cursor_v1 statement,
    obelisk_rt_design_cursor_v1 *outScope, bool *outStatement) {
  if (!isStatementOffset(database, statement.offset))
    return OBELISK_RT_INVALID_HANDLE;
  uint32_t sourceIndex = static_cast<uint32_t>(
      (statement.offset - database.statements) / kStatementSize);
  obelisk::reflection::TableKind targetTable;
  uint32_t targetIndex = 0;
  if (!effectiveStatementScope(database, sourceIndex, targetTable, targetIndex))
    return OBELISK_RT_INVALID_DESIGN;
  outScope->offset = tableOffset(database, targetTable, targetIndex);
  *outStatement = targetTable == obelisk::reflection::TableKind::Statement;
  return OBELISK_RT_OK;
}

obelisk_rt_status
designVPIStatementParent(const Database &database,
                         obelisk_rt_design_cursor_v1 statement,
                         obelisk_rt_design_cursor_v1 *outParent) {
  if (!isStatementOffset(database, statement.offset))
    return OBELISK_RT_INVALID_HANDLE;
  uint32_t parent = read32(database.data + statement.offset + 16);
  if (parent == UINT32_MAX) {
    *outParent = {};
    return OBELISK_RT_EOF;
  }
  if (parent >= database.statementCount)
    return OBELISK_RT_INVALID_DESIGN;
  outParent->offset = database.statements + uint64_t{parent} * kStatementSize;
  return OBELISK_RT_OK;
}

obelisk_rt_status
designVPIStatementOwner(const Database &database,
                        obelisk_rt_design_cursor_v1 statement,
                        obelisk_rt_design_cursor_v1 *outOwner) {
  if (!isStatementOffset(database, statement.offset))
    return OBELISK_RT_INVALID_HANDLE;
  uint32_t owner = read32(database.data + statement.offset + 8);
  if (owner == UINT32_MAX) {
    *outOwner = {};
    return OBELISK_RT_EOF;
  }
  if (owner >= database.objectCount)
    return OBELISK_RT_INVALID_DESIGN;
  outOwner->offset = database.objects + uint64_t{owner} * kObjectSize;
  return OBELISK_RT_OK;
}

obelisk_rt_status
designVPIStatementIsScope(const Database &database,
                          obelisk_rt_design_cursor_v1 statement,
                          bool *outIsScope) {
  if (!isStatementOffset(database, statement.offset))
    return OBELISK_RT_INVALID_HANDLE;
  *outIsScope = (read16(database.data + statement.offset + 38) &
                 OBELISK_RT_DESIGN_STATEMENT_SCOPE) != 0;
  return OBELISK_RT_OK;
}

obelisk_rt_status
designVPIStatementIsProtected(const Database &database,
                              obelisk_rt_design_cursor_v1 statement,
                              bool *outIsProtected) {
  if (!isStatementOffset(database, statement.offset))
    return OBELISK_RT_INVALID_HANDLE;
  *outIsProtected = (read16(database.data + statement.offset + 38) &
                     OBELISK_RT_DESIGN_STATEMENT_PROTECTED) != 0;
  return OBELISK_RT_OK;
}

} // namespace

obelisk_rt_status obelisk_rt_initialize_design_database(
    const obelisk_rt_execution_descriptor_v1 *execution,
    DesignDatabaseCache &cache) noexcept {
  if (!execution)
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    Database database;
    obelisk_rt_status status = loadValidatedDatabase(execution, database);
    if (status != OBELISK_RT_OK)
      return status;
    database.validated = true;
    cache = database;
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_DESIGN; }
}

obelisk_rt_status obelisk_rt_register_design_database(
    const obelisk_rt_execution_descriptor_v1 *execution,
    const DesignDatabaseCache &cache) noexcept {
  if (!matchesExecution(cache, execution))
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    DatabaseRegistry &registry = databaseRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    auto [entry, inserted] = registry.databases.try_emplace(execution);
    if (inserted)
      entry->second.database = cache;
    else if (!sameDatabase(entry->second.database, cache))
      return OBELISK_RT_INVALID_DESIGN;
    if (entry->second.contextCount == std::numeric_limits<size_t>::max()) {
      if (inserted)
        registry.databases.erase(entry);
      return OBELISK_RT_OUT_OF_RESOURCES;
    }
    ++entry->second.contextCount;
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_DESIGN; }
}

void obelisk_rt_unregister_design_database(
    const obelisk_rt_execution_descriptor_v1 *execution) noexcept {
  if (!execution)
    return;
  OBELISK_RT_TRY {
    DatabaseRegistry &registry = databaseRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    auto found = registry.databases.find(execution);
    if (found == registry.databases.end())
      return;
    if (found->second.contextCount > 1) {
      --found->second.contextCount;
      return;
    }
    registry.databases.erase(found);
  }
  OBELISK_RT_CATCH_ALL {
    // Context teardown cannot recover from a registry synchronization failure.
  }
}

obelisk_rt_status
obelisk_rt_cached_design_root(const obelisk_rt_context *context,
                              obelisk_rt_design_cursor_v1 *outCursor) noexcept {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designRoot(*database, outCursor)
                  : OBELISK_RT_INVALID_DESIGN;
}

obelisk_rt_status obelisk_rt_cached_design_parent(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    obelisk_rt_design_cursor_v1 *outCursor) noexcept {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designParent(*database, cursor, outCursor)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_design_child(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    obelisk_rt_design_cursor_v1 *outCursor) noexcept {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designChild(*database, cursor, outCursor)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_design_child_at(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    uint64_t index, obelisk_rt_design_cursor_v1 *outCursor) noexcept {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designChildAt(*database, cursor, index, outCursor)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_design_sibling(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    obelisk_rt_design_cursor_v1 *outCursor) noexcept {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designSibling(*database, cursor, outCursor)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_design_lookup(
    const obelisk_rt_context *context, const uint8_t *name, uint64_t nameSize,
    obelisk_rt_design_cursor_v1 *outCursor) noexcept {
  if (!outCursor || (nameSize != 0 && !name))
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designLookup(*database, name, nameSize, outCursor)
                  : OBELISK_RT_INVALID_DESIGN;
}

obelisk_rt_status
obelisk_rt_cached_design_info(const obelisk_rt_context *context,
                              obelisk_rt_design_cursor_v1 cursor,
                              obelisk_rt_design_info_v1 *outInfo) noexcept {
  if (!outInfo)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designInfo(*database, cursor, outInfo)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_design_type_info(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    obelisk_rt_design_type_info_v1 *outInfo) noexcept {
  if (!outInfo)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designTypeInfo(*database, cursor, outInfo)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_design_type_child(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    uint64_t index, obelisk_rt_design_cursor_v1 *outCursor) noexcept {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designTypeChild(*database, cursor, index, outCursor)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_design_semantic_root(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 object,
    obelisk_rt_design_cursor_v1 *outCursor) noexcept {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designSemanticRoot(*database, object, outCursor)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_design_semantic_type_info(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    obelisk_rt_design_semantic_type_info_v1 *outInfo) noexcept {
  if (!outInfo)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designSemanticTypeInfo(*database, cursor, outInfo)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_design_semantic_type_edge(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    uint64_t index, obelisk_rt_design_semantic_type_edge_v1 *outEdge) noexcept {
  if (!outEdge)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designSemanticTypeEdge(*database, cursor, index, outEdge)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_design_source(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    const uint8_t **outFile, uint64_t *outFileSize, uint32_t *outLine,
    uint32_t *outColumn) noexcept {
  if (!outFile || !outFileSize || !outLine || !outColumn)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  const uint8_t *record = nullptr;
  uint32_t kind = 0;
  if (!database)
    return OBELISK_RT_INVALID_HANDLE;
  uint64_t fileOffset = 0;
  uint32_t line = 0;
  uint32_t column = 0;
  uint32_t virtualScope = 0;
  uint32_t virtualMember = 0;
  const uint8_t *memberRecord = nullptr;
  if (virtualMemberForCursor(*database, cursor.offset, virtualScope,
                             virtualMember, memberRecord)) {
    uint32_t relative = read32(memberRecord + 4);
    fileOffset = relative == 0 ? 0 : database->strings + relative;
    line = read32(memberRecord + 8);
    column = read32(memberRecord + 12);
  } else if (isStatementOffset(*database, cursor.offset)) {
    record = database->data + cursor.offset;
    uint32_t relative = read32(record + 20);
    fileOffset = relative == 0 ? 0 : database->strings + relative;
    line = read32(record + 28);
    column = read32(record + 32);
  } else if (isStaticObjectOffset(*database, cursor.offset)) {
    record = database->data + cursor.offset;
    uint32_t relative = read32(record + 12);
    fileOffset = relative == 0 ? 0 : database->strings + relative;
    line = read32(record + 20);
    column = read32(record + 24);
  } else {
    if (!getRecord(*database, cursor.offset, record, kind) ||
        kind == OBELISK_RT_DESIGN_RECORD_TYPE)
      return OBELISK_RT_INVALID_HANDLE;
    fileOffset =
        read64(record + (kind == OBELISK_RT_DESIGN_RECORD_SCOPE ? 48 : 32));
    uint64_t lineColumn =
        read64(record + (kind == OBELISK_RT_DESIGN_RECORD_SCOPE ? 56 : 88));
    line = static_cast<uint32_t>(lineColumn >> 32);
    column = static_cast<uint32_t>(lineColumn);
  }
  if (fileOffset == 0) {
    *outFile = nullptr;
    *outFileSize = 0;
  } else {
    std::string_view file;
    if (!getString(*database, fileOffset, file))
      return OBELISK_RT_INVALID_DESIGN;
    *outFile = reinterpret_cast<const uint8_t *>(file.data());
    *outFileSize = file.size();
  }
  *outLine = line;
  *outColumn = column;
  return OBELISK_RT_OK;
}

obelisk_rt_status obelisk_rt_cached_design_name(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    const uint8_t **outData, uint64_t *outSize) noexcept {
  if (!outData || !outSize)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designName(*database, cursor, outData, outSize)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_vpi_type(const obelisk_rt_context *context,
                                             obelisk_rt_design_cursor_v1 cursor,
                                             uint32_t *outType) noexcept {
  if (!outType)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designVPIType(*database, cursor, outType)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_vpi_fixed_property(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    uint32_t selector, VPIFixedPropertyValue *outValue) noexcept {
  if (!outValue)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database
             ? designVPIFixedProperty(*database, cursor, selector, outValue)
             : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status
obelisk_rt_cached_vpi_frozen_value(const obelisk_rt_context *context,
                                   obelisk_rt_design_cursor_v1 cursor,
                                   VPIFrozenValue *outValue) noexcept {
  if (!outValue)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  uint32_t packedSource = 0;
  if (!database ||
      !packedObjectReferenceForCursor(*database, cursor.offset, packedSource))
    return OBELISK_RT_INVALID_HANDLE;
  return findFrozenValue(*database, packedSource, *outValue)
             ? OBELISK_RT_OK
             : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_vpi_resolved_net_type(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    uint64_t bitOffset, uint64_t bitWidth, uint32_t *outType) noexcept {
  if (!outType)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designVPIResolvedNetType(*database, cursor, bitOffset,
                                             bitWidth, outType)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status
obelisk_rt_cached_vpi_net_delay(const obelisk_rt_context *context,
                                obelisk_rt_design_cursor_v1 cursor,
                                uint64_t bitOffset, uint64_t bitWidth,
                                VPINetDelayValue *outDelay) noexcept {
  if (!outDelay)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designVPINetDelay(*database, cursor, bitOffset, bitWidth,
                                      outDelay)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_vpi_relation_range(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 source,
    uint32_t selector, bool iterate, VPIRelationRange *outRange) noexcept {
  if (!outRange)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designVPIRelationRange(*database, source, selector, iterate,
                                           outRange)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_vpi_relation_target(
    const obelisk_rt_context *context, uint64_t relationIndex,
    obelisk_rt_design_cursor_v1 *outCursor, uint32_t *outType,
    bool *outStatement) noexcept {
  if (!outCursor || !outType || !outStatement)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designVPIRelationTarget(*database, relationIndex, outCursor,
                                            outType, outStatement)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status
obelisk_rt_cached_vpi_relation_index(const obelisk_rt_context *context,
                                     obelisk_rt_design_cursor_v1 source,
                                     VPIRelationIndexInfo *outInfo) noexcept {
  if (!outInfo)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designVPIRelationIndex(*database, source, outInfo)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_vpi_relation_index_dimension(
    const obelisk_rt_context *context, const VPIRelationIndexInfo &info,
    uint32_t dimension, int64_t *outLeft, int64_t *outRight) noexcept {
  if (!outLeft || !outRight)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designVPIRelationIndexDimension(*database, info, dimension,
                                                    outLeft, outRight)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_vpi_relation_index_key(
    const obelisk_rt_context *context, const VPIRelationIndexInfo &info,
    int64_t index, uint32_t *outOrdinal) noexcept {
  if (!outOrdinal)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database
             ? designVPIRelationIndexKey(*database, info, index, outOrdinal)
             : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_vpi_relation_index_ordinal_key(
    const obelisk_rt_context *context, const VPIRelationIndexInfo &info,
    uint32_t ordinal, int64_t *outIndex) noexcept {
  if (!outIndex)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designVPIRelationIndexOrdinalKey(*database, info, ordinal,
                                                     outIndex)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status
obelisk_rt_cached_vpi_array_member(const obelisk_rt_context *context,
                                   obelisk_rt_design_cursor_v1 member,
                                   VPIArrayMemberInfo *outInfo) noexcept {
  if (!outInfo)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designVPIArrayMember(*database, member, outInfo)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_vpi_statement_scope(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 statement,
    obelisk_rt_design_cursor_v1 *outScope) noexcept {
  if (!outScope)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designVPIStatementScope(*database, statement, outScope)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_vpi_statement_enclosing_scope(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 statement,
    obelisk_rt_design_cursor_v1 *outScope, bool *outStatement) noexcept {
  if (!outScope || !outStatement)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designVPIStatementEnclosingScope(*database, statement,
                                                     outScope, outStatement)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_vpi_statement_parent(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 statement,
    obelisk_rt_design_cursor_v1 *outParent) noexcept {
  if (!outParent)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designVPIStatementParent(*database, statement, outParent)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_vpi_statement_owner(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 statement,
    obelisk_rt_design_cursor_v1 *outOwner) noexcept {
  if (!outOwner)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designVPIStatementOwner(*database, statement, outOwner)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status
obelisk_rt_cached_vpi_statement_is_scope(const obelisk_rt_context *context,
                                         obelisk_rt_design_cursor_v1 statement,
                                         bool *outIsScope) noexcept {
  if (!outIsScope)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designVPIStatementIsScope(*database, statement, outIsScope)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_vpi_statement_is_protected(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 statement,
    bool *outIsProtected) noexcept {
  if (!outIsProtected)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designVPIStatementIsProtected(*database, statement,
                                                  outIsProtected)
                  : OBELISK_RT_INVALID_HANDLE;
}

bool obelisk_rt_checked_design_record(
    const obelisk_rt_execution_descriptor_v1 *execution, uint64_t offset,
    const uint8_t *&record, uint32_t &kind) noexcept {
  OBELISK_RT_TRY {
    Database database;
    return loadValidatedDatabase(execution, database) == OBELISK_RT_OK &&
           getRecord(database, offset, record, kind);
  }
  OBELISK_RT_CATCH_ALL { return false; }
}

extern "C" obelisk_rt_status obelisk_rt_v1_design_validate(
    const obelisk_rt_execution_descriptor_v1 *execution) {
  OBELISK_RT_TRY {
    Database database;
    return loadValidatedDatabase(execution, database);
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_DESIGN; }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_design_root(const obelisk_rt_execution_descriptor_v1 *execution,
                          obelisk_rt_design_cursor_v1 *outCursor) {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  obelisk_rt_status status = loadValidatedDatabase(execution, database);
  if (status != OBELISK_RT_OK)
    return status;
  return designRoot(database, outCursor);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_design_child(const obelisk_rt_execution_descriptor_v1 *execution,
                           obelisk_rt_design_cursor_v1 cursor,
                           obelisk_rt_design_cursor_v1 *outCursor) {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  obelisk_rt_status status = loadValidatedDatabase(execution, database);
  if (status != OBELISK_RT_OK)
    return mapInvalidDatabase(status, OBELISK_RT_INVALID_HANDLE);
  return designChild(database, cursor, outCursor);
}

extern "C" obelisk_rt_status obelisk_rt_v1_design_child_at(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_design_cursor_v1 cursor, uint64_t index,
    obelisk_rt_design_cursor_v1 *outCursor) {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  obelisk_rt_status status = loadValidatedDatabase(execution, database);
  if (status != OBELISK_RT_OK)
    return mapInvalidDatabase(status, OBELISK_RT_INVALID_HANDLE);
  return designChildAt(database, cursor, index, outCursor);
}

extern "C" obelisk_rt_status obelisk_rt_v1_design_sibling(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_design_cursor_v1 cursor,
    obelisk_rt_design_cursor_v1 *outCursor) {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  obelisk_rt_status status = loadValidatedDatabase(execution, database);
  if (status != OBELISK_RT_OK)
    return mapInvalidDatabase(status, OBELISK_RT_INVALID_HANDLE);
  return designSibling(database, cursor, outCursor);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_design_lookup(const obelisk_rt_execution_descriptor_v1 *execution,
                            const uint8_t *name, uint64_t nameSize,
                            obelisk_rt_design_cursor_v1 *outCursor) {
  if (!outCursor || (nameSize != 0 && !name))
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  obelisk_rt_status status = loadValidatedDatabase(execution, database);
  if (status != OBELISK_RT_OK)
    return status;
  return designLookup(database, name, nameSize, outCursor);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_design_info(const obelisk_rt_execution_descriptor_v1 *execution,
                          obelisk_rt_design_cursor_v1 cursor,
                          obelisk_rt_design_info_v1 *outInfo) {
  if (!outInfo)
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  obelisk_rt_status status = loadValidatedDatabase(execution, database);
  if (status != OBELISK_RT_OK)
    return mapInvalidDatabase(status, OBELISK_RT_INVALID_HANDLE);
  return designInfo(database, cursor, outInfo);
}

extern "C" obelisk_rt_status obelisk_rt_v1_design_type_info(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_design_cursor_v1 cursor,
    obelisk_rt_design_type_info_v1 *outInfo) {
  if (!outInfo)
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  obelisk_rt_status status = loadValidatedDatabase(execution, database);
  if (status != OBELISK_RT_OK)
    return mapInvalidDatabase(status, OBELISK_RT_INVALID_HANDLE);
  return designTypeInfo(database, cursor, outInfo);
}

extern "C" obelisk_rt_status obelisk_rt_v1_design_type_child(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_design_cursor_v1 cursor, uint64_t index,
    obelisk_rt_design_cursor_v1 *outCursor) {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  obelisk_rt_status status = loadValidatedDatabase(execution, database);
  if (status != OBELISK_RT_OK)
    return mapInvalidDatabase(status, OBELISK_RT_INVALID_HANDLE);
  return designTypeChild(database, cursor, index, outCursor);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_design_name(const obelisk_rt_execution_descriptor_v1 *execution,
                          obelisk_rt_design_cursor_v1 cursor,
                          const uint8_t **outData, uint64_t *outSize) {
  if (!outData || !outSize)
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  obelisk_rt_status status = loadValidatedDatabase(execution, database);
  if (status != OBELISK_RT_OK)
    return mapInvalidDatabase(status, OBELISK_RT_INVALID_HANDLE);
  return designName(database, cursor, outData, outSize);
}

uint64_t packedMask(uint64_t width) {
  return width == 64 ? UINT64_MAX : (uint64_t{1} << width) - 1;
}

template <typename Plane>
uint64_t loadPackedState(const Plane &plane, uint64_t offset,
                         uint64_t width) {
  size_t word = static_cast<size_t>(offset / 64);
  unsigned shift = static_cast<unsigned>(offset % 64);
  uint64_t result = plane[word] >> shift;
  if (shift != 0 && width > 64 - shift)
    result |= plane[word + 1] << (64 - shift);
  return result & packedMask(width);
}

template <typename Plane>
void storePackedState(Plane &plane, uint64_t offset,
                      uint64_t width, uint64_t value) {
  size_t word = static_cast<size_t>(offset / 64);
  unsigned shift = static_cast<unsigned>(offset % 64);
  uint64_t mask = packedMask(width);
  value &= mask;
  uint64_t lowMask = mask << shift;
  plane[word] = (plane[word] & ~lowMask) | (value << shift);
  if (shift != 0 && width > 64 - shift) {
    unsigned lowBits = 64 - shift;
    uint64_t highMask = mask >> lowBits;
    plane[word + 1] = (plane[word + 1] & ~highMask) | (value >> lowBits);
  }
}

void storePackedBytes(uint8_t *bytes, uint64_t value) {
  for (unsigned byte = 0; byte != 8; ++byte)
    bytes[byte] = static_cast<uint8_t>(value >> (byte * 8));
}

void setPackedByte(uint8_t *bytes, uint64_t bit, bool value) {
  uint8_t mask = static_cast<uint8_t>(1u << (bit % 8));
  if (value)
    bytes[bit / 8] |= mask;
  else
    bytes[bit / 8] &= static_cast<uint8_t>(~mask);
}

static obelisk_rt_status accessState(obelisk_rt_context *context,
                                     obelisk_rt_design_cursor_v1 cursor,
                                     uint64_t *value, uint64_t *unknown,
                                     uint64_t bitWidth, bool write,
                                     bool overrideForce = false) {
  if (!context || !value || bitWidth == 0 || !context->execution)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  const Database *database = cachedDatabase(context);
  const uint8_t *record;
  uint32_t kind;
  if (!database || !getRecord(*database, cursor.offset, record, kind) ||
      kind < OBELISK_RT_DESIGN_RECORD_STORAGE ||
      (kind > OBELISK_RT_DESIGN_RECORD_DRIVER &&
       kind != OBELISK_RT_DESIGN_RECORD_PORT))
    return OBELISK_RT_INVALID_HANDLE;
  uint32_t capabilities = read32(record + 4);
  if ((capabilities &
       (write ? OBELISK_RT_DESIGN_CAP_WRITE : OBELISK_RT_DESIGN_CAP_READ)) == 0)
    return OBELISK_RT_PERMISSION_DENIED;
  uint64_t stateOffset = read64(record + 80);
  uint64_t width = read64(record + 56);
  if (bitWidth != width || stateOffset > context->execution->state_bit_count ||
      width > context->execution->state_bit_count - stateOffset)
    return OBELISK_RT_INVALID_HANDLE;
  uint64_t limbs = (width + 63) / 64;
  const uint8_t *typeRecord = database->data + read64(record + 48);
  bool fourState =
      ((read32(typeRecord + 4) >> 8) & OBELISK_RT_DESIGN_TYPE_FOUR_STATE) != 0;
  if (write && !overrideForce && kind == OBELISK_RT_DESIGN_RECORD_NET) {
    bool connected = false;
    obelisk_rt_status status = obelisk_rt_design_net_is_connected(
        context, stateOffset, stateOffset + width, &connected);
    if (status != OBELISK_RT_OK)
      return status;
    // A direct deposit cannot update one logical alias in isolation.
    if (connected)
      return OBELISK_RT_PERMISSION_DENIED;
  }
  std::optional<PackedSignalTransitionBuffer> wideTransitions;
  constexpr size_t inlinePublishedBytes = 32;
  std::array<uint8_t, inlinePublishedBytes * 4> inlinePublishedPlanes;
  std::vector<uint8_t> overflowPublishedPlanes;
  uint8_t *publishedPlanes = nullptr;
  if (write && width > 64) {
    if (width > UINT64_MAX - 7 ||
        (width + 7) / 8 > std::numeric_limits<size_t>::max() / 4)
      return OBELISK_RT_OUT_OF_RESOURCES;
    OBELISK_RT_TRY {
      wideTransitions.emplace(width);
      size_t bytes = static_cast<size_t>((width + 7) / 8);
      if (bytes <= inlinePublishedBytes) {
        std::fill_n(inlinePublishedPlanes.data(), bytes * 4, uint8_t{0});
        publishedPlanes = inlinePublishedPlanes.data();
      } else {
        overflowPublishedPlanes.assign(bytes * 4, 0);
        publishedPlanes = overflowPublishedPlanes.data();
      }
    }
    OBELISK_RT_CATCH(const std::bad_alloc &) {
      return OBELISK_RT_OUT_OF_MEMORY;
    }
    OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_DESIGN; }
  }
  bool stateChanged = false;
  bool runClockCoordinator = false;
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    uint64_t signalBase = UINT64_MAX;
    if (write) {
      signalBase = obelisk_rt_canonical_state_handle_unlocked(
          context, stateOffset, width);
      if (signalBase == UINT64_MAX)
        return OBELISK_RT_INVALID_HANDLE;
    }
    const uint8_t *canonicalValuePlane = nullptr;
    const uint8_t *canonicalUnknownPlane = nullptr;
    const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
    if (!write && plan &&
        (plan->flags & OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE) != 0 &&
        !context->nativeScheduleDeoptimized &&
        plan->state_bit_count == context->execution->state_bit_count &&
        plan->state_value && plan->state_unknown) {
      bool dirty = context->nativeScheduleDirtyRootsPresent;
      if (dirty) {
        dirty = false;
        __int128 accessEnd = static_cast<__int128>(stateOffset) + width;
        for (const auto &[id, state] : context->nativeStaticStates)
          if (static_cast<__int128>(state.bitOffset) < accessEnd &&
              static_cast<__int128>(stateOffset) <
                  static_cast<__int128>(state.bitOffset) + state.bitWidth &&
              (context->nativeScheduleTransientDirtyRoots.find(id) !=
                   context->nativeScheduleTransientDirtyRoots.end() ||
               context->nativeSchedulePersistentDirtyRoots.find(id) !=
                   context->nativeSchedulePersistentDirtyRoots.end())) {
            dirty = true;
            break;
          }
      }
      if (!dirty) {
        canonicalValuePlane = plan->state_value;
        canonicalUnknownPlane = plan->state_unknown;
      }
    }
    if (!write && width <= 64) {
      value[0] = canonicalValuePlane
                     ? loadPackedBytes(canonicalValuePlane, stateOffset, width)
                     : loadPackedState(context->stateValue, stateOffset, width);
      if (unknown)
        unknown[0] = fourState ? (canonicalUnknownPlane
                                      ? loadPackedBytes(canonicalUnknownPlane,
                                                        stateOffset, width)
                                      : loadPackedState(context->stateUnknown,
                                                        stateOffset, width))
                               : 0;
      return OBELISK_RT_OK;
    }
    if (write && width <= 64) {
      uint64_t mask = packedMask(width);
      uint64_t oldValue =
          loadPackedState(context->stateValue, stateOffset, width);
      uint64_t oldUnknown =
          loadPackedState(context->stateUnknown, stateOffset, width);
      uint64_t blocked = 0;
      if (!overrideForce) {
        if (!context->forceMask.empty())
          blocked |= loadPackedState(context->forceMask, stateOffset, width);
        if (!context->assignMask.empty())
          blocked |= loadPackedState(context->assignMask, stateOffset, width);
      }
      uint64_t writable = mask & ~blocked;
      uint64_t newValue = (oldValue & ~writable) | (value[0] & writable);
      uint64_t inputUnknown = fourState && unknown ? unknown[0] : 0;
      uint64_t newUnknown =
          (oldUnknown & ~writable) | (inputUnknown & writable);
      uint64_t changed = (oldValue ^ newValue) | (oldUnknown ^ newUnknown);
      if (changed != 0) {
        uint64_t oldZero = ~oldUnknown & ~oldValue & mask;
        uint64_t oldOne = ~oldUnknown & oldValue & mask;
        uint64_t newZero = ~newUnknown & ~newValue & mask;
        uint64_t newOne = ~newUnknown & newValue & mask;
        uint64_t posedge =
            ((oldZero & ~newZero) | (oldUnknown & newOne)) & mask;
        uint64_t negedge = ((oldOne & ~newOne) | (oldUnknown & newZero)) & mask;
        std::array<uint8_t, 8> changedBytes{}, posedgeBytes{}, negedgeBytes{};
        std::array<uint8_t, 8> oldValueBytes{}, oldUnknownBytes{};
        std::array<uint8_t, 8> valueBytes{}, unknownBytes{};
        storePackedBytes(changedBytes.data(), changed);
        storePackedBytes(posedgeBytes.data(), posedge);
        storePackedBytes(negedgeBytes.data(), negedge);
        storePackedBytes(oldValueBytes.data(), oldValue);
        storePackedBytes(oldUnknownBytes.data(), oldUnknown);
        storePackedBytes(valueBytes.data(), newValue);
        storePackedBytes(unknownBytes.data(), newUnknown);
        storePackedState(context->stateValue, stateOffset, width, newValue);
        storePackedState(context->stateUnknown, stateOffset, width, newUnknown);
        obelisk_rt_sync_native_state_range_unlocked(context, stateOffset,
                                                    width);
        // Force/release changes the persistent override state and must retain
        // the transactional generic handoff. Only an exact deposit may enter
        // the generated clock coordinator directly.
        bool synchronized =
            !overrideForce && obelisk_rt_aot_external_deposit_unlocked(
                                  context, signalBase, stateOffset, width);
        if (!synchronized)
          obelisk_rt_aot_external_write_handle_unlocked(
              context, signalBase, stateOffset, width, false);
        if (!obelisk_rt_publish_native_signal_transition_unlocked(
                context, signalBase, width, changedBytes.data(),
                posedgeBytes.data(), negedgeBytes.data(), oldValueBytes.data(),
                oldUnknownBytes.data(), valueBytes.data(), unknownBytes.data(),
                synchronized, overrideForce))
          return context->schedulerStatus;
        runClockCoordinator |=
            synchronized && context->nativeSchedulePlan &&
            context->nativeSchedulePlan->clock_kernel_count != 0;
        stateChanged = true;
      }
    } else {
      size_t publishedBytes = static_cast<size_t>((width + 7) / 8);
      uint8_t *publishedOldValue =
          write ? publishedPlanes : static_cast<uint8_t *>(nullptr);
      uint8_t *publishedOldUnknown =
          write ? publishedPlanes + publishedBytes
                : static_cast<uint8_t *>(nullptr);
      uint8_t *publishedValue =
          write ? publishedPlanes + publishedBytes * 2
                : static_cast<uint8_t *>(nullptr);
      uint8_t *publishedUnknown =
          write ? publishedPlanes + publishedBytes * 3
                : static_cast<uint8_t *>(nullptr);
      for (uint64_t bit = 0; bit != width; ++bit) {
        uint64_t sourceLimb = bit / 64;
        uint64_t sourceMask = uint64_t{1} << (bit % 64);
        uint64_t absolute = stateOffset + bit;
        uint64_t stateMask = uint64_t{1} << (absolute % 64);
        uint64_t &stateValue = context->stateValue[absolute / 64];
        uint64_t &stateUnknown = context->stateUnknown[absolute / 64];
        if (write) {
          bool oldValue = (stateValue & stateMask) != 0;
          bool oldUnknown = (stateUnknown & stateMask) != 0;
          setPackedByte(publishedOldValue, bit, oldValue);
          setPackedByte(publishedOldUnknown, bit, oldUnknown);
          bool forced = absolute / 64 < context->forceMask.size() &&
                        (context->forceMask[absolute / 64] & stateMask) != 0;
          bool assigned = absolute / 64 < context->assignMask.size() &&
                          (context->assignMask[absolute / 64] & stateMask) != 0;
          if ((forced || assigned) && !overrideForce) {
            setPackedByte(publishedValue, bit, oldValue);
            setPackedByte(publishedUnknown, bit, oldUnknown);
            continue;
          }
          bool newValue = (value[sourceLimb] & sourceMask) != 0;
          bool newUnknown =
              fourState && unknown && (unknown[sourceLimb] & sourceMask) != 0;
          stateValue =
              newValue ? stateValue | stateMask : stateValue & ~stateMask;
          stateUnknown =
              newUnknown ? stateUnknown | stateMask : stateUnknown & ~stateMask;
          uint32_t edges =
              transitionEdges(oldValue, oldUnknown, newValue, newUnknown);
          if (edges != 0) {
            wideTransitions->record(bit, edges);
            stateChanged = true;
          }
          setPackedByte(publishedValue, bit, newValue);
          setPackedByte(publishedUnknown, bit, newUnknown);
        } else {
          bool readValue =
              canonicalValuePlane
                  ? ((canonicalValuePlane[absolute / 8] >> (absolute % 8)) &
                     1) != 0
                  : (stateValue & stateMask) != 0;
          bool readUnknown =
              canonicalUnknownPlane
                  ? ((canonicalUnknownPlane[absolute / 8] >> (absolute % 8)) &
                     1) != 0
                  : (stateUnknown & stateMask) != 0;
          value[sourceLimb] =
              (value[sourceLimb] & ~sourceMask) | (readValue ? sourceMask : 0);
          if (unknown && fourState)
            unknown[sourceLimb] = (unknown[sourceLimb] & ~sourceMask) |
                                  (readUnknown ? sourceMask : 0);
          else if (unknown)
            unknown[sourceLimb] &= ~sourceMask;
        }
      }
      if (write && stateChanged) {
        obelisk_rt_sync_native_state_range_unlocked(context, stateOffset,
                                                    width);
        bool synchronized =
            !overrideForce && obelisk_rt_aot_external_deposit_unlocked(
                                  context, signalBase, stateOffset, width);
        if (!synchronized)
          obelisk_rt_aot_external_write_handle_unlocked(
              context, signalBase, stateOffset, width, false);
        if (!obelisk_rt_publish_native_signal_transition_unlocked(
                context, signalBase, width, wideTransitions->changed(),
                wideTransitions->posedge(), wideTransitions->negedge(),
                publishedOldValue, publishedOldUnknown, publishedValue,
                publishedUnknown, synchronized, overrideForce))
          return context->schedulerStatus;
        runClockCoordinator |=
            synchronized && context->nativeSchedulePlan &&
            context->nativeSchedulePlan->clock_kernel_count != 0;
      }
    }
  }
  if (!write && width % 64 != 0) {
    uint64_t mask = (uint64_t{1} << (width % 64)) - 1;
    value[limbs - 1] &= mask;
    if (unknown)
      unknown[limbs - 1] &= mask;
  }
  if (runClockCoordinator) {
    obelisk_rt_status status =
        obelisk_rt_v1_scheduler_run_clock_coordinator(context);
    if (status != OBELISK_RT_OK)
      return status;
  }
  if (write && kind == OBELISK_RT_DESIGN_RECORD_DRIVER)
    return obelisk_rt_resolve_design_drivers(context, stateOffset,
                                             stateOffset + width);
  return OBELISK_RT_OK;
}

obelisk_rt_status obelisk_rt_design_state_offset(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    uint64_t bitOffset, uint64_t *outStateOffset) noexcept {
  if (!context || !context->execution || !outStateOffset)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  const uint8_t *record = nullptr;
  uint32_t kind = 0;
  if (!database || !getRecord(*database, cursor.offset, record, kind) ||
      (kind != OBELISK_RT_DESIGN_RECORD_STORAGE &&
       kind != OBELISK_RT_DESIGN_RECORD_NET &&
       kind != OBELISK_RT_DESIGN_RECORD_PORT))
    return OBELISK_RT_INVALID_HANDLE;
  const uint64_t width = read64(record + 56);
  const uint64_t stateOffset = read64(record + 80);
  if (bitOffset >= width || stateOffset > context->execution->state_bit_count ||
      width > context->execution->state_bit_count - stateOffset)
    return OBELISK_RT_INVALID_HANDLE;
  *outStateOffset = stateOffset + bitOffset;
  return OBELISK_RT_OK;
}

obelisk_rt_status
obelisk_rt_read_design_slice(obelisk_rt_context *context,
                             obelisk_rt_design_cursor_v1 cursor,
                             uint64_t bitOffset, uint64_t bitWidth,
                             uint64_t *value, uint64_t *unknown) noexcept {
  if (!context || !value || bitWidth == 0 || !context->execution)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  const Database *database = cachedDatabase(context);
  const uint8_t *record = nullptr;
  uint32_t kind = 0;
  if (!database || !getRecord(*database, cursor.offset, record, kind) ||
      kind < OBELISK_RT_DESIGN_RECORD_STORAGE ||
      (kind > OBELISK_RT_DESIGN_RECORD_DRIVER &&
       kind != OBELISK_RT_DESIGN_RECORD_PORT) ||
      (read32(record + 4) & OBELISK_RT_DESIGN_CAP_READ) == 0)
    return OBELISK_RT_INVALID_HANDLE;
  uint64_t stateOffset = read64(record + 80);
  uint64_t rootWidth = read64(record + 56);
  if (bitOffset > rootWidth || bitWidth > rootWidth - bitOffset ||
      stateOffset > context->execution->state_bit_count ||
      rootWidth > context->execution->state_bit_count - stateOffset)
    return OBELISK_RT_INVALID_HANDLE;
  const uint8_t *typeRecord = database->data + read64(record + 48);
  bool fourState =
      ((read32(typeRecord + 4) >> 8) & OBELISK_RT_DESIGN_TYPE_FOUR_STATE) != 0;
  uint64_t selectedOffset = stateOffset + bitOffset;

  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  const uint8_t *canonicalValuePlane = nullptr;
  const uint8_t *canonicalUnknownPlane = nullptr;
  const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
  if (plan && (plan->flags & OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE) != 0 &&
      !context->nativeScheduleDeoptimized &&
      plan->state_bit_count == context->execution->state_bit_count &&
      plan->state_value && plan->state_unknown) {
    bool dirty = context->nativeScheduleDirtyRootsPresent;
    if (dirty) {
      dirty = false;
      __int128 selectedEnd = static_cast<__int128>(selectedOffset) + bitWidth;
      for (const auto &[id, state] : context->nativeStaticStates)
        if (static_cast<__int128>(state.bitOffset) < selectedEnd &&
            static_cast<__int128>(selectedOffset) <
                static_cast<__int128>(state.bitOffset) + state.bitWidth &&
            (context->nativeScheduleTransientDirtyRoots.find(id) !=
                 context->nativeScheduleTransientDirtyRoots.end() ||
             context->nativeSchedulePersistentDirtyRoots.find(id) !=
                 context->nativeSchedulePersistentDirtyRoots.end())) {
          dirty = true;
          break;
        }
    }
    if (!dirty) {
      canonicalValuePlane = plan->state_value;
      canonicalUnknownPlane = plan->state_unknown;
    }
  }

  uint64_t limbs = (bitWidth - 1) / 64 + 1;
  for (uint64_t limb = 0; limb != limbs; ++limb) {
    uint64_t width = std::min<uint64_t>(64, bitWidth - limb * 64);
    uint64_t offset = selectedOffset + limb * 64;
    value[limb] = canonicalValuePlane
                      ? loadPackedBytes(canonicalValuePlane, offset, width)
                      : loadPackedState(context->stateValue, offset, width);
    if (unknown)
      unknown[limb] =
          fourState
              ? (canonicalUnknownPlane
                     ? loadPackedBytes(canonicalUnknownPlane, offset, width)
                     : loadPackedState(context->stateUnknown, offset, width))
              : 0;
  }
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_design_read(obelisk_rt_context *context,
                          obelisk_rt_design_cursor_v1 cursor, uint64_t *value,
                          uint64_t *unknown, uint64_t bitWidth) {
  return accessState(context, cursor, value, unknown, bitWidth, false);
}

extern "C" obelisk_rt_status obelisk_rt_v1_design_write(
    obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    const uint64_t *value, const uint64_t *unknown, uint64_t bitWidth) {
  return accessState(context, cursor, const_cast<uint64_t *>(value),
                     const_cast<uint64_t *>(unknown), bitWidth, true);
}

extern "C" obelisk_rt_status obelisk_rt_v1_design_force(
    obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    const uint64_t *value, const uint64_t *unknown, uint64_t bitWidth) {
  if (!context || !value || bitWidth == 0 || !context->execution)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  const uint8_t *record;
  uint32_t kind;
  if (!database || !getRecord(*database, cursor.offset, record, kind) ||
      (kind != OBELISK_RT_DESIGN_RECORD_STORAGE &&
       kind != OBELISK_RT_DESIGN_RECORD_NET) ||
      (read32(record + 4) & OBELISK_RT_DESIGN_CAP_WRITE) == 0 ||
      read64(record + 56) != bitWidth)
    return OBELISK_RT_PERMISSION_DENIED;
  uint64_t stateOffset = read64(record + 80);
  if (stateOffset > context->execution->state_bit_count ||
      bitWidth > context->execution->state_bit_count - stateOffset)
    return OBELISK_RT_INVALID_HANDLE;
  if (kind == OBELISK_RT_DESIGN_RECORD_NET)
    return obelisk_rt_force_design_nets(
        context, stateOffset, bitWidth,
        reinterpret_cast<const uint8_t *>(value),
        reinterpret_cast<const uint8_t *>(unknown));
  OBELISK_RT_TRY {
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      if (context->forceMask.empty())
        context->forceMask.assign(context->stateValue.size(), 0);
      for (uint64_t bit = 0; bit != bitWidth; ++bit) {
        uint64_t absolute = stateOffset + bit;
        context->forceMask[absolute / 64] |= uint64_t{1} << (absolute % 64);
      }
    }
    obelisk_rt_status status =
        accessState(context, cursor, const_cast<uint64_t *>(value),
                    const_cast<uint64_t *>(unknown), bitWidth, true, true);
    if (status == OBELISK_RT_OK) {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      obelisk_rt_aot_external_write_range_unlocked(context, stateOffset,
                                                   bitWidth, true);
    }
    return status;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_DESIGN; }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_design_release(obelisk_rt_context *context,
                             obelisk_rt_design_cursor_v1 cursor) {
  if (!context || !context->execution)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  const uint8_t *record;
  uint32_t kind;
  if (!database || !getRecord(*database, cursor.offset, record, kind) ||
      (kind != OBELISK_RT_DESIGN_RECORD_STORAGE &&
       kind != OBELISK_RT_DESIGN_RECORD_NET) ||
      (read32(record + 4) & OBELISK_RT_DESIGN_CAP_WRITE) == 0)
    return OBELISK_RT_INVALID_HANDLE;
  uint64_t stateOffset = read64(record + 80);
  uint64_t bitWidth = read64(record + 56);
  if (stateOffset > context->execution->state_bit_count ||
      bitWidth > context->execution->state_bit_count - stateOffset)
    return OBELISK_RT_INVALID_HANDLE;
  if (kind == OBELISK_RT_DESIGN_RECORD_NET)
    return obelisk_rt_release_design_nets(context, stateOffset, bitWidth);
  if (bitWidth > UINT64_MAX - 7)
    return OBELISK_RT_OUT_OF_RESOURCES;
  uint64_t byteCount = (bitWidth + 7) / 8;
  if (byteCount > std::numeric_limits<size_t>::max())
    return OBELISK_RT_OUT_OF_RESOURCES;
  std::vector<uint8_t> oldValue, oldUnknown, newValue, newUnknown;
  OBELISK_RT_TRY {
    oldValue.assign(static_cast<size_t>(byteCount), 0);
    oldUnknown.assign(static_cast<size_t>(byteCount), 0);
    newValue.assign(static_cast<size_t>(byteCount), 0);
    newUnknown.assign(static_cast<size_t>(byteCount), 0);
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  uint64_t signalBase = UINT64_MAX;
  bool stateChanged = false;
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    signalBase = obelisk_rt_canonical_state_handle_unlocked(
        context, stateOffset, bitWidth);
    if (signalBase == UINT64_MAX)
      return OBELISK_RT_INVALID_HANDLE;
    for (uint64_t bit = 0; bit != bitWidth; ++bit) {
      uint64_t absolute = stateOffset + bit;
      uint64_t limb = absolute / 64;
      uint64_t mask = uint64_t{1} << (absolute % 64);
      if (limb < context->forceMask.size())
        context->forceMask[limb] &= ~mask;
      bool assigned = limb < context->assignMask.size() &&
                      (context->assignMask[limb] & mask) != 0;
      bool retained = limb < context->continuousMask.size() &&
                      (context->continuousMask[limb] & mask) != 0;
      bool oldValueBit = (context->stateValue[limb] & mask) != 0;
      bool oldUnknownBit = (context->stateUnknown[limb] & mask) != 0;
      bool newValueBit = oldValueBit;
      bool newUnknownBit = oldUnknownBit;
      if (kind == OBELISK_RT_DESIGN_RECORD_STORAGE && (assigned || retained)) {
        newValueBit = assigned ? (context->assignValue[limb] & mask) != 0
                               : (context->continuousValue[limb] & mask) != 0;
        newUnknownBit = assigned
                            ? (context->assignUnknown[limb] & mask) != 0
                            : (context->continuousUnknown[limb] & mask) != 0;
        context->stateValue[limb] = newValueBit
                                        ? context->stateValue[limb] | mask
                                        : context->stateValue[limb] & ~mask;
        context->stateUnknown[limb] = newUnknownBit
                                          ? context->stateUnknown[limb] | mask
                                          : context->stateUnknown[limb] & ~mask;
        uint32_t edges =
            transitionEdges(oldValueBit, oldUnknownBit, newValueBit,
                            newUnknownBit);
        stateChanged |= edges != 0;
      }
      setPackedByte(oldValue.data(), bit, oldValueBit);
      setPackedByte(oldUnknown.data(), bit, oldUnknownBit);
      setPackedByte(newValue.data(), bit, newValueBit);
      setPackedByte(newUnknown.data(), bit, newUnknownBit);
    }
    if (stateChanged)
      obelisk_rt_aot_external_write_unlocked(context);
    // Generic native fragments may resume direct addressing as soon as the
    // release guard is refreshed. Materialize the released continuous/assign
    // value in their bound planes before that boundary or any observer runs.
    obelisk_rt_sync_native_state_range_unlocked(context, stateOffset, bitWidth);
    obelisk_rt_aot_release_range_unlocked(context, stateOffset, bitWidth);
  }
  // A procedural variable retains the forced value. Continuously driven
  // storage and nets immediately reveal their retained driver state.
  if (stateChanged) {
    publishOverrideTransition(
        context, signalBase, bitWidth, oldValue.data(), oldUnknown.data(),
        newValue.data(), newUnknown.data());
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (context->schedulerStatus != OBELISK_RT_OK)
      return context->schedulerStatus;
  }
  return OBELISK_RT_OK;
}
