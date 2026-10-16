//===- DesignDatabase.cpp - Checked DWARF-like design reflection ----------===//

#include "ProcessPacking.h"
#include "ProcessShared.h"
#include "RuntimeInternal.h"
#include "VPIExpressionValue.h"
#include "obelisk/Reflection/DesignReflection.h"
#include "obelisk/Reflection/VPIObjectModel.h"
#include "obelisk/Runtime/DescriptorMap.h"
#include "obelisk/Runtime/StableHash.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <map>
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
constexpr uint64_t kClassTypeBindingSize =
    obelisk::reflection::ClassTypeBindingLayout.size;
constexpr uint64_t kSemanticClassBindingSize =
    obelisk::reflection::SemanticClassBindingLayout.size;
constexpr uint64_t kClassMemberBindingSize =
    obelisk::reflection::ClassMemberBindingLayout.size;
constexpr uint64_t kThreadOriginBindingSize =
    obelisk::reflection::ThreadOriginBindingLayout.size;
constexpr uint64_t kCallSiteBindingSize =
    obelisk::reflection::CallSiteBindingLayout.size;
constexpr uint64_t kFrameLocalBindingSize =
    obelisk::reflection::FrameLocalBindingLayout.size;
constexpr uint64_t kCallDispatchBindingSize =
    obelisk::reflection::CallDispatchBindingLayout.size;
constexpr uint64_t kClassDispatchMethodBindingSize =
    obelisk::reflection::ClassDispatchMethodBindingLayout.size;
constexpr uint64_t kClassDispatchParentBindingSize =
    obelisk::reflection::ClassDispatchParentBindingLayout.size;
constexpr uint64_t kInterfaceCallableSize =
    obelisk::reflection::InterfaceCallableLayout.size;
constexpr uint64_t kInterfaceInstanceSize =
    obelisk::reflection::InterfaceInstanceLayout.size;
constexpr uint64_t kInterfaceCallableBindingSize =
    obelisk::reflection::InterfaceCallableBindingLayout.size;
constexpr uint64_t kInterfaceCallBindingSize =
    obelisk::reflection::InterfaceCallBindingLayout.size;
constexpr uint64_t kCallableSignatureBindingSize =
    obelisk::reflection::CallableSignatureBindingLayout.size;
constexpr uint64_t kCodeUnitScopeBindingSize =
    obelisk::reflection::CodeUnitScopeBindingLayout.size;
constexpr uint64_t kInitializerBindingSize =
    obelisk::reflection::InitializerBindingLayout.size;
constexpr uint64_t kExpressionRootBindingSize =
    obelisk::reflection::ExpressionRootBindingLayout.size;
constexpr uint64_t kExpressionTemplateSize =
    obelisk::reflection::ExpressionTemplateLayout.size;
constexpr uint64_t kExpressionNodeSize =
    obelisk::reflection::ExpressionNodeLayout.size;
constexpr uint64_t kExpressionOperandSize =
    obelisk::reflection::ExpressionOperandLayout.size;
constexpr uint64_t kExpressionLeafSize =
    obelisk::reflection::ExpressionLeafLayout.size;
constexpr uint64_t kExpressionBoundMemberSize =
    obelisk::reflection::ExpressionBoundMemberLayout.size;
constexpr uint64_t kExpressionLeafShapeSize =
    obelisk::reflection::ExpressionLeafShapeLayout.size;
constexpr uint64_t kTerminalDriverBindingSize =
    obelisk::reflection::TerminalDriverBindingLayout.size;

constexpr uint64_t kEventBindingSize =
    obelisk::reflection::EventBindingLayout.size;
constexpr uint64_t kEventArrayStorageBindingSize =
    obelisk::reflection::EventArrayStorageBindingLayout.size;
constexpr uint64_t kModportBindingSize =
    obelisk::reflection::ModportBindingLayout.size;
constexpr uint64_t kBoxTypeBindingSize =
    obelisk::reflection::BoxTypeBindingLayout.size;
constexpr uint64_t kValueElementBindingSize =
    obelisk::reflection::ValueElementBindingLayout.size;
constexpr uint64_t kMailboxElementBindingSize =
    obelisk::reflection::MailboxElementBindingLayout.size;
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
constexpr uint64_t kDefinitionParameterValueSize =
    obelisk::reflection::DefinitionParameterValueLayout.size;
constexpr uint16_t kDefinitionParameterLocal = UINT16_C(1) << 8;
constexpr uint16_t kDefinitionParameterRange = UINT16_C(1) << 9;
constexpr uint16_t kDefinitionParameterDefaultUnavailable = UINT16_C(1) << 10;
constexpr uint16_t kDefinitionParameterOriginalDefault = UINT16_C(1) << 11;

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
               obelisk::reflection::VPIObjectSetID::StaticImageObjects, vpiKind) ||
           obelisk::reflection::vpiObjectSetContains(
               obelisk::reflection::VPIObjectSetID::DataVariableObjects, vpiKind);
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
  uint64_t classTypeBindingOffset = 0, classTypeBindingCount = 0;
  uint64_t semanticClassBindingOffset = 0, semanticClassBindingCount = 0;
  uint64_t classMemberBindingOffset = 0, classMemberBindingCount = 0;
  uint64_t modportBindingOffset = 0, modportBindingCount = 0;
  uint64_t mailboxElementBindingOffset = 0, mailboxElementBindingCount = 0;
  uint64_t boxTypeBindingOffset = 0, boxTypeBindingCount = 0;
  uint64_t valueElementBindingOffset = 0, valueElementBindingCount = 0;
  uint64_t eventBindingOffset = 0, eventBindingCount = 0;
  uint64_t eventArrayStorageBindingOffset = 0,
           eventArrayStorageBindingCount = 0;
  uint64_t threadOriginBindingOffset = 0, threadOriginBindingCount = 0;
  uint64_t callSiteBindingOffset = 0, callSiteBindingCount = 0;
  uint64_t frameLocalBindingOffset = 0, frameLocalBindingCount = 0;
  uint64_t callDispatchBindingOffset = 0, callDispatchBindingCount = 0;
  uint64_t classDispatchMethodBindingOffset = 0,
           classDispatchMethodBindingCount = 0;
  uint64_t classDispatchParentBindingOffset = 0,
           classDispatchParentBindingCount = 0;
  uint64_t interfaceCallableOffset = 0, interfaceCallableCount = 0;
  uint64_t interfaceInstanceOffset = 0, interfaceInstanceCount = 0;
  uint64_t interfaceCallableBindingOffset = 0,
           interfaceCallableBindingCount = 0;
  uint64_t interfaceCallBindingOffset = 0, interfaceCallBindingCount = 0;
  uint64_t callableSignatureBindingOffset = 0,
           callableSignatureBindingCount = 0;
  uint64_t codeUnitScopeBindingOffset = 0, codeUnitScopeBindingCount = 0;
  uint64_t initializerBindingOffset = 0, initializerBindingCount = 0;
  uint64_t expressionRootBindingOffset = 0, expressionRootBindingCount = 0;
  uint64_t expressionTemplateOffset = 0, expressionTemplateCount = 0;
  uint64_t expressionNodeOffset = 0, expressionNodeCount = 0;
  uint64_t expressionOperandOffset = 0, expressionOperandCount = 0;
  uint64_t expressionLeafOffset = 0, expressionLeafCount = 0;
  uint64_t expressionBoundMemberOffset = 0, expressionBoundMemberCount = 0;
  uint64_t expressionLeafShapeOffset = 0, expressionLeafShapeCount = 0;
  uint64_t terminalDriverBindingOffset = 0, terminalDriverBindingCount = 0;

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
  uint64_t definitionParameterValueOffset = 0;
  uint64_t definitionParameterValueCount = 0;
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
    definitionParameterValueOffset = read64(directory + 768);
    definitionParameterValueCount = read64(directory + 776);
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
    obelisk::reflection::SemanticDirectoryView reader(directory);
    classTypeBindingOffset = reader.getClassTypeBindingOffset();
    classTypeBindingCount = reader.getClassTypeBindingCount();
    semanticClassBindingOffset = reader.getSemanticClassBindingOffset();
    semanticClassBindingCount = reader.getSemanticClassBindingCount();
    classMemberBindingOffset = reader.getClassMemberBindingOffset();
    classMemberBindingCount = reader.getClassMemberBindingCount();
    modportBindingOffset = reader.getModportBindingOffset();
    modportBindingCount = reader.getModportBindingCount();
    mailboxElementBindingOffset = reader.getMailboxElementBindingOffset();
    mailboxElementBindingCount = reader.getMailboxElementBindingCount();
    boxTypeBindingOffset = reader.getBoxTypeBindingOffset();
    boxTypeBindingCount = reader.getBoxTypeBindingCount();
    valueElementBindingOffset = reader.getValueElementBindingOffset();
    valueElementBindingCount = reader.getValueElementBindingCount();
    eventBindingOffset = reader.getEventBindingOffset();
    eventBindingCount = reader.getEventBindingCount();
    eventArrayStorageBindingOffset = reader.getEventArrayStorageBindingOffset();
    eventArrayStorageBindingCount = reader.getEventArrayStorageBindingCount();
    threadOriginBindingOffset = reader.getThreadOriginBindingOffset();
    threadOriginBindingCount = reader.getThreadOriginBindingCount();
    callSiteBindingOffset = reader.getCallSiteBindingOffset();
    callSiteBindingCount = reader.getCallSiteBindingCount();
    frameLocalBindingOffset = reader.getFrameLocalBindingOffset();
    frameLocalBindingCount = reader.getFrameLocalBindingCount();
    callDispatchBindingOffset = reader.getCallDispatchBindingOffset();
    callDispatchBindingCount = reader.getCallDispatchBindingCount();
    classDispatchMethodBindingOffset =
        reader.getClassDispatchMethodBindingOffset();
    classDispatchMethodBindingCount =
        reader.getClassDispatchMethodBindingCount();
    classDispatchParentBindingOffset =
        reader.getClassDispatchParentBindingOffset();
    classDispatchParentBindingCount =
        reader.getClassDispatchParentBindingCount();
    interfaceCallableOffset = reader.getInterfaceCallableOffset();
    interfaceCallableCount = reader.getInterfaceCallableCount();
    interfaceInstanceOffset = reader.getInterfaceInstanceOffset();
    interfaceInstanceCount = reader.getInterfaceInstanceCount();
    interfaceCallableBindingOffset = reader.getInterfaceCallableBindingOffset();
    interfaceCallableBindingCount = reader.getInterfaceCallableBindingCount();
    interfaceCallBindingOffset = reader.getInterfaceCallBindingOffset();
    interfaceCallBindingCount = reader.getInterfaceCallBindingCount();
    callableSignatureBindingOffset = reader.getCallableSignatureBindingOffset();
    codeUnitScopeBindingOffset = reader.getCodeUnitScopeBindingOffset();
    callableSignatureBindingCount = reader.getCallableSignatureBindingCount();
    codeUnitScopeBindingCount = reader.getCodeUnitScopeBindingCount();
    initializerBindingOffset = reader.getInitializerBindingOffset();
    initializerBindingCount = reader.getInitializerBindingCount();
    expressionRootBindingOffset = reader.getExpressionRootBindingOffset();
    expressionRootBindingCount = reader.getExpressionRootBindingCount();
    expressionTemplateOffset = reader.getExpressionTemplateOffset();
    expressionTemplateCount = reader.getExpressionTemplateCount();
    expressionNodeOffset = reader.getExpressionNodeOffset();
    expressionNodeCount = reader.getExpressionNodeCount();
    expressionOperandOffset = reader.getExpressionOperandOffset();
    expressionOperandCount = reader.getExpressionOperandCount();
    expressionLeafOffset = reader.getExpressionLeafOffset();
    expressionLeafCount = reader.getExpressionLeafCount();
    expressionBoundMemberOffset = reader.getExpressionBoundMemberOffset();
    expressionBoundMemberCount = reader.getExpressionBoundMemberCount();
    expressionLeafShapeOffset = reader.getExpressionLeafShapeOffset();
    expressionLeafShapeCount = reader.getExpressionLeafShapeCount();
    terminalDriverBindingOffset = reader.getTerminalDriverBindingOffset();
    terminalDriverBindingCount = reader.getTerminalDriverBindingCount();
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
              classTypeBindingOffset,
              classTypeBindingCount,
              semanticClassBindingOffset,
              semanticClassBindingCount,
              classMemberBindingOffset,
              classMemberBindingCount,
              modportBindingOffset,
              modportBindingCount,
              mailboxElementBindingOffset,
              mailboxElementBindingCount,
              boxTypeBindingOffset,
              boxTypeBindingCount,
              valueElementBindingOffset,
              valueElementBindingCount,
              eventBindingOffset,
              eventBindingCount,
              eventArrayStorageBindingOffset,
              eventArrayStorageBindingCount,
              threadOriginBindingOffset,
              threadOriginBindingCount,
              callSiteBindingOffset,
              callSiteBindingCount,
              frameLocalBindingOffset,
              frameLocalBindingCount,
              callDispatchBindingOffset,
              callDispatchBindingCount,
              classDispatchMethodBindingOffset,
              classDispatchMethodBindingCount,
              classDispatchParentBindingOffset,
              classDispatchParentBindingCount,
              interfaceCallableOffset,
              interfaceCallableCount,
              interfaceInstanceOffset,
              interfaceInstanceCount,
              interfaceCallableBindingOffset,
              interfaceCallableBindingCount,
              interfaceCallBindingOffset,
              interfaceCallBindingCount,
              callableSignatureBindingOffset,
              callableSignatureBindingCount,
              codeUnitScopeBindingOffset,
              codeUnitScopeBindingCount,
              initializerBindingOffset,
              initializerBindingCount,
              expressionRootBindingOffset,
              expressionRootBindingCount,
              expressionTemplateOffset,
              expressionTemplateCount,
              expressionNodeOffset,
              expressionNodeCount,
              expressionOperandOffset,
              expressionOperandCount,
              expressionLeafOffset,
              expressionLeafCount,
              expressionBoundMemberOffset,
              expressionBoundMemberCount,
              expressionLeafShapeOffset,
              expressionLeafShapeCount,
              terminalDriverBindingOffset,
              terminalDriverBindingCount,

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
              definitionParameterValueOffset,
              definitionParameterValueCount,
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
        {database.classTypeBindings, database.classTypeBindingCount,
         kClassTypeBindingSize},
        {database.semanticClassBindings, database.semanticClassBindingCount,
         kSemanticClassBindingSize},
        {database.classMemberBindings, database.classMemberBindingCount,
         kClassMemberBindingSize},
        {database.modportBindings, database.modportBindingCount,
         kModportBindingSize},
        {database.mailboxElementBindings, database.mailboxElementBindingCount,
         kMailboxElementBindingSize},
        {database.boxTypeBindings, database.boxTypeBindingCount,
         kBoxTypeBindingSize},
        {database.valueElementBindings, database.valueElementBindingCount,
         kValueElementBindingSize},
        {database.eventBindings, database.eventBindingCount, kEventBindingSize},
        {database.eventArrayStorageBindings,
         database.eventArrayStorageBindingCount, kEventArrayStorageBindingSize},
        {database.threadOriginBindings, database.threadOriginBindingCount,
         kThreadOriginBindingSize},
        {database.callSiteBindings, database.callSiteBindingCount,
         kCallSiteBindingSize},
        {database.frameLocalBindings, database.frameLocalBindingCount,
         kFrameLocalBindingSize},
        {database.callDispatchBindings, database.callDispatchBindingCount,
         kCallDispatchBindingSize},
        {database.classDispatchMethodBindings,
         database.classDispatchMethodBindingCount,
         kClassDispatchMethodBindingSize},
        {database.classDispatchParentBindings,
         database.classDispatchParentBindingCount,
         kClassDispatchParentBindingSize},
        {database.interfaceCallables, database.interfaceCallableCount,
         kInterfaceCallableSize},
        {database.interfaceInstances, database.interfaceInstanceCount,
         kInterfaceInstanceSize},
        {database.interfaceCallableBindings,
         database.interfaceCallableBindingCount, kInterfaceCallableBindingSize},
        {database.interfaceCallBindings, database.interfaceCallBindingCount,
         kInterfaceCallBindingSize},
        {database.callableSignatureBindings,
         database.callableSignatureBindingCount, kCallableSignatureBindingSize},
        {database.codeUnitScopeBindings, database.codeUnitScopeBindingCount,
         kCodeUnitScopeBindingSize},
        {database.initializerBindings, database.initializerBindingCount,
         kInitializerBindingSize},
        {database.expressionRootBindings, database.expressionRootBindingCount,
         kExpressionRootBindingSize},
        {database.expressionTemplates, database.expressionTemplateCount,
         kExpressionTemplateSize},
        {database.expressionNodes, database.expressionNodeCount,
         kExpressionNodeSize},
        {database.expressionOperands, database.expressionOperandCount,
         kExpressionOperandSize},
        {database.expressionLeaves, database.expressionLeafCount,
         kExpressionLeafSize},
        {database.expressionBoundMembers, database.expressionBoundMemberCount,
         kExpressionBoundMemberSize},
        {database.expressionLeafShapes, database.expressionLeafShapeCount,
         kExpressionLeafShapeSize},
        {database.terminalDriverBindings, database.terminalDriverBindingCount,
         kTerminalDriverBindingSize},

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
        {database.definitionParameterValues,
         database.definitionParameterValueCount, kDefinitionParameterValueSize},
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
        database.classTypeBindingCount != 0 ||
        database.semanticClassBindingCount != 0 ||
        database.classMemberBindingCount != 0 ||
        database.modportBindingCount != 0 ||
        database.mailboxElementBindingCount != 0 ||
        database.boxTypeBindingCount != 0 ||
        database.valueElementBindingCount != 0 ||
        database.eventBindingCount != 0 ||
        database.eventArrayStorageBindingCount != 0 ||
        database.threadOriginBindingCount != 0 ||
        database.callSiteBindingCount != 0 ||
        database.frameLocalBindingCount != 0 ||
        database.callDispatchBindingCount != 0 ||
        database.classDispatchMethodBindingCount != 0 ||
        database.classDispatchParentBindingCount != 0 ||
        database.interfaceCallableCount != 0 ||
        database.interfaceInstanceCount != 0 ||
        database.interfaceCallableBindingCount != 0 ||
        database.interfaceCallBindingCount != 0 ||
        database.callableSignatureBindingCount != 0 ||
        database.codeUnitScopeBindingCount != 0 ||
        database.initializerBindingCount != 0 ||
        database.expressionRootBindingCount != 0 ||
        database.expressionTemplateCount != 0 ||
        database.expressionNodeCount != 0 ||
        database.expressionOperandCount != 0 ||
        database.expressionLeafCount != 0 ||
        database.expressionBoundMemberCount != 0 ||
        database.expressionLeafShapeCount != 0 ||
        database.terminalDriverBindingCount != 0 ||

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
        database.definitionParameterValueCount != 0 ||
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
      !validRange(database.classTypeBindings, database.classTypeBindingCount,
                  kClassTypeBindingSize, database.size) ||
      !validRange(database.semanticClassBindings,
                  database.semanticClassBindingCount, kSemanticClassBindingSize,
                  database.size) ||
      !validRange(database.classMemberBindings,
                  database.classMemberBindingCount, kClassMemberBindingSize,
                  database.size) ||
      !validRange(database.modportBindings, database.modportBindingCount,
                  kModportBindingSize, database.size) ||
      !validRange(database.boxTypeBindings, database.boxTypeBindingCount,
                  kBoxTypeBindingSize, database.size) ||
      !validRange(database.valueElementBindings,
                  database.valueElementBindingCount, kValueElementBindingSize,
                  database.size) ||
      !validRange(database.mailboxElementBindings,
                  database.mailboxElementBindingCount,
                  kMailboxElementBindingSize, database.size) ||
      !validRange(database.eventBindings, database.eventBindingCount,
                  kEventBindingSize, database.size) ||
      !validRange(database.eventArrayStorageBindings,
                  database.eventArrayStorageBindingCount,
                  kEventArrayStorageBindingSize, database.size) ||
      !validRange(database.threadOriginBindings,
                  database.threadOriginBindingCount, kThreadOriginBindingSize,
                  database.size) ||
      !validRange(database.callSiteBindings, database.callSiteBindingCount,
                  kCallSiteBindingSize, database.size) ||
      !validRange(database.frameLocalBindings, database.frameLocalBindingCount,
                  kFrameLocalBindingSize, database.size) ||
      !validRange(database.callDispatchBindings,
                  database.callDispatchBindingCount, kCallDispatchBindingSize,
                  database.size) ||
      !validRange(database.classDispatchMethodBindings,
                  database.classDispatchMethodBindingCount,
                  kClassDispatchMethodBindingSize, database.size) ||
      !validRange(database.classDispatchParentBindings,
                  database.classDispatchParentBindingCount,
                  kClassDispatchParentBindingSize, database.size) ||
      !validRange(database.interfaceCallables, database.interfaceCallableCount,
                  kInterfaceCallableSize, database.size) ||
      !validRange(database.interfaceInstances, database.interfaceInstanceCount,
                  kInterfaceInstanceSize, database.size) ||
      !validRange(database.interfaceCallableBindings,
                  database.interfaceCallableBindingCount,
                  kInterfaceCallableBindingSize, database.size) ||
      !validRange(database.interfaceCallBindings,
                  database.interfaceCallBindingCount, kInterfaceCallBindingSize,
                  database.size) ||
      !validRange(database.callableSignatureBindings,
                  database.callableSignatureBindingCount,
                  kCallableSignatureBindingSize, database.size) ||
      !validRange(database.codeUnitScopeBindings,
                  database.codeUnitScopeBindingCount, kCodeUnitScopeBindingSize,
                  database.size) ||
      !validRange(database.initializerBindings,
                  database.initializerBindingCount, kInitializerBindingSize,
                  database.size) ||
      !validRange(database.expressionRootBindings,
                  database.expressionRootBindingCount,
                  kExpressionRootBindingSize, database.size) ||
      !validRange(database.expressionTemplates,
                  database.expressionTemplateCount, kExpressionTemplateSize,
                  database.size) ||
      !validRange(database.expressionNodes, database.expressionNodeCount,
                  kExpressionNodeSize, database.size) ||
      !validRange(database.expressionOperands, database.expressionOperandCount,
                  kExpressionOperandSize, database.size) ||
      !validRange(database.expressionLeaves, database.expressionLeafCount,
                  kExpressionLeafSize, database.size) ||
      !validRange(database.expressionBoundMembers,
                  database.expressionBoundMemberCount,
                  kExpressionBoundMemberSize, database.size) ||
      !validRange(database.expressionLeafShapes,
                  database.expressionLeafShapeCount, kExpressionLeafShapeSize,
                  database.size) ||
      !validRange(database.terminalDriverBindings,
                  database.terminalDriverBindingCount,
                  kTerminalDriverBindingSize, database.size) ||

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
      !validRange(database.definitionParameterValues,
                  database.definitionParameterValueCount,
                  kDefinitionParameterValueSize, database.size) ||
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
      (database.classTypeBindingCount != 0 &&
       database.classTypeBindings < kHeaderSize) ||
      (database.semanticClassBindingCount != 0 &&
       database.semanticClassBindings < kHeaderSize) ||
      (database.classMemberBindingCount != 0 &&
       database.classMemberBindings < kHeaderSize) ||
      (database.modportBindingCount != 0 &&
       database.modportBindings < kHeaderSize) ||
      (database.boxTypeBindingCount != 0 &&
       database.boxTypeBindings < kHeaderSize) ||
      (database.valueElementBindingCount != 0 &&
       database.valueElementBindings < kHeaderSize) ||
      (database.mailboxElementBindingCount != 0 &&
       database.mailboxElementBindings < kHeaderSize) ||
      (database.eventBindingCount != 0 &&
       database.eventBindings < kHeaderSize) ||
      (database.eventArrayStorageBindingCount != 0 &&
       database.eventArrayStorageBindings < kHeaderSize) ||
      (database.threadOriginBindingCount != 0 &&
       database.threadOriginBindings < kHeaderSize) ||
      (database.callSiteBindingCount != 0 &&
       database.callSiteBindings < kHeaderSize) ||
      (database.frameLocalBindingCount != 0 &&
       database.frameLocalBindings < kHeaderSize) ||
      (database.callDispatchBindingCount != 0 &&
       database.callDispatchBindings < kHeaderSize) ||
      (database.classDispatchMethodBindingCount != 0 &&
       database.classDispatchMethodBindings < kHeaderSize) ||
      (database.classDispatchParentBindingCount != 0 &&
       database.classDispatchParentBindings < kHeaderSize) ||
      (database.interfaceCallableCount != 0 &&
       database.interfaceCallables < kHeaderSize) ||
      (database.interfaceInstanceCount != 0 &&
       database.interfaceInstances < kHeaderSize) ||
      (database.interfaceCallableBindingCount != 0 &&
       database.interfaceCallableBindings < kHeaderSize) ||
      (database.interfaceCallBindingCount != 0 &&
       database.interfaceCallBindings < kHeaderSize) ||
      (database.callableSignatureBindingCount != 0 &&
       database.callableSignatureBindings < kHeaderSize) ||
      (database.codeUnitScopeBindingCount != 0 &&
       database.codeUnitScopeBindings < kHeaderSize) ||
      (database.initializerBindingCount != 0 &&
       database.initializerBindings < kHeaderSize) ||
      (database.expressionRootBindingCount != 0 &&
       database.expressionRootBindings < kHeaderSize) ||
      (database.expressionTemplateCount != 0 &&
       database.expressionTemplates < kHeaderSize) ||
      (database.expressionNodeCount != 0 &&
       database.expressionNodes < kHeaderSize) ||
      (database.expressionOperandCount != 0 &&
       database.expressionOperands < kHeaderSize) ||
      (database.expressionLeafCount != 0 &&
       database.expressionLeaves < kHeaderSize) ||
      (database.expressionBoundMemberCount != 0 &&
       database.expressionBoundMembers < kHeaderSize) ||
      (database.expressionLeafShapeCount != 0 &&
       database.expressionLeafShapes < kHeaderSize) ||
      (database.terminalDriverBindingCount != 0 &&
       database.terminalDriverBindings < kHeaderSize) ||

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
      ((database.definitionParameterValueCount != 0 ||
        database.definitionParameterValues != 0) &&
       database.definitionParameterValues < kHeaderSize) ||
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
      database.classTypeBindingCount > UINT32_MAX ||
      database.semanticClassBindingCount > UINT32_MAX ||
      database.classMemberBindingCount > UINT32_MAX ||
      database.modportBindingCount > UINT32_MAX ||
      database.mailboxElementBindingCount > UINT32_MAX ||
      database.boxTypeBindingCount > UINT32_MAX ||
      database.valueElementBindingCount > UINT32_MAX ||
      database.eventBindingCount > UINT32_MAX ||
      database.eventArrayStorageBindingCount > UINT32_MAX ||
      database.threadOriginBindingCount > UINT32_MAX ||
      database.callSiteBindingCount > UINT32_MAX ||
      database.frameLocalBindingCount > UINT32_MAX ||
      database.callDispatchBindingCount > UINT32_MAX ||
      database.classDispatchMethodBindingCount > UINT32_MAX ||
      database.classDispatchParentBindingCount > UINT32_MAX ||
      database.interfaceCallableCount > UINT32_MAX ||
      database.interfaceInstanceCount > UINT32_MAX ||
      database.interfaceCallableBindingCount > UINT32_MAX ||
      database.interfaceCallBindingCount > UINT32_MAX ||
      database.callableSignatureBindingCount > UINT32_MAX ||
      database.codeUnitScopeBindingCount > UINT32_MAX ||
      database.initializerBindingCount > UINT32_MAX ||
      database.expressionRootBindingCount >= (UINT32_C(1) << 30) ||
      database.expressionTemplateCount >= UINT32_MAX ||
      database.expressionNodeCount >= (UINT32_C(1) << 30) ||
      database.expressionOperandCount >= (UINT32_C(1) << 30) ||
      database.expressionLeafCount >= UINT32_MAX ||
      database.expressionBoundMemberCount >= (UINT32_C(1) << 30) ||
      database.expressionLeafShapeCount >= UINT32_MAX ||
      database.terminalDriverBindingCount > UINT32_MAX ||

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
      database.definitionParameterValueCount > UINT32_MAX ||
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
      {database.classTypeBindings, database.classTypeBindingCount,
       kClassTypeBindingSize},
      {database.semanticClassBindings, database.semanticClassBindingCount,
       kSemanticClassBindingSize},
      {database.classMemberBindings, database.classMemberBindingCount,
       kClassMemberBindingSize},
      {database.modportBindings, database.modportBindingCount,
       kModportBindingSize},
      {database.mailboxElementBindings, database.mailboxElementBindingCount,
       kMailboxElementBindingSize},
      {database.boxTypeBindings, database.boxTypeBindingCount,
       kBoxTypeBindingSize},
      {database.valueElementBindings, database.valueElementBindingCount,
       kValueElementBindingSize},
      {database.eventBindings, database.eventBindingCount, kEventBindingSize},
      {database.eventArrayStorageBindings,
       database.eventArrayStorageBindingCount, kEventArrayStorageBindingSize},
      {database.threadOriginBindings, database.threadOriginBindingCount,
       kThreadOriginBindingSize},
      {database.callSiteBindings, database.callSiteBindingCount,
       kCallSiteBindingSize},
      {database.frameLocalBindings, database.frameLocalBindingCount,
       kFrameLocalBindingSize},
      {database.callDispatchBindings, database.callDispatchBindingCount,
       kCallDispatchBindingSize},
      {database.classDispatchMethodBindings,
       database.classDispatchMethodBindingCount,
       kClassDispatchMethodBindingSize},
      {database.classDispatchParentBindings,
       database.classDispatchParentBindingCount,
       kClassDispatchParentBindingSize},
      {database.interfaceCallables, database.interfaceCallableCount,
       kInterfaceCallableSize},
      {database.interfaceInstances, database.interfaceInstanceCount,
       kInterfaceInstanceSize},
      {database.interfaceCallableBindings,
       database.interfaceCallableBindingCount, kInterfaceCallableBindingSize},
      {database.interfaceCallBindings, database.interfaceCallBindingCount,
       kInterfaceCallBindingSize},
      {database.callableSignatureBindings,
       database.callableSignatureBindingCount, kCallableSignatureBindingSize},
      {database.codeUnitScopeBindings, database.codeUnitScopeBindingCount,
       kCodeUnitScopeBindingSize},
      {database.initializerBindings, database.initializerBindingCount,
       kInitializerBindingSize},
      {database.expressionRootBindings, database.expressionRootBindingCount,
       kExpressionRootBindingSize},
      {database.expressionTemplates, database.expressionTemplateCount,
       kExpressionTemplateSize},
      {database.expressionNodes, database.expressionNodeCount,
       kExpressionNodeSize},
      {database.expressionOperands, database.expressionOperandCount,
       kExpressionOperandSize},
      {database.expressionLeaves, database.expressionLeafCount,
       kExpressionLeafSize},
      {database.expressionBoundMembers, database.expressionBoundMemberCount,
       kExpressionBoundMemberSize},
      {database.expressionLeafShapes, database.expressionLeafShapeCount,
       kExpressionLeafShapeSize},
      {database.terminalDriverBindings, database.terminalDriverBindingCount,
       kTerminalDriverBindingSize},

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
      {database.definitionParameterValues,
       database.definitionParameterValueCount, kDefinitionParameterValueSize},
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

bool isDefinitionMemberInventory(
    const obelisk::reflection::VPITraversalDescriptor &edge) {
  using namespace obelisk::reflection;
  return edge.automaticRelation == VPIAutomaticRelation::DefinitionMember ||
         (edge.mode == VPITraversalMode::Iterate &&
          edge.automaticRelation == VPIAutomaticRelation::DirectChild &&
          (edge.selector == static_cast<uint32_t>(VPIObjectKind::Parameter) ||
           edge.selector ==
               static_cast<uint32_t>(VPIObjectKind::TypeParameter)));
}

uint32_t definitionIOCount(const Database &database, const uint8_t *definition,
                           uint32_t limit = UINT32_MAX) {
  uint32_t first = read32(definition + 16);
  uint32_t count = std::min(read32(definition + 20), limit);
  uint32_t result = 0;
  for (uint32_t index = 0; index != count; ++index)
    result += read16(database.data + database.definitionMembers +
                     uint64_t{first + index} * kDefinitionMemberSize + 16) ==
              static_cast<uint16_t>(obelisk::reflection::VPIObjectKind::IODecl);
  return result;
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
                                      uint32_t &semanticType,
                                      uint32_t *outBindingIndex = nullptr) {
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
  if (outBindingIndex)
    *outBindingIndex = low;
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
      (kind == OBELISK_RT_DESIGN_SEMANTIC_GENERIC_INTEGRAL ||
       kind == OBELISK_RT_DESIGN_SEMANTIC_BIT ||
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

obelisk_rt_status semanticPackedBitWidth(const Database &database,
                                         uint32_t index, uint64_t *outWidth) {
  uint64_t multiplier = 1;
  // The image validator has already established an acyclic semantic graph.
  // Packed dimensions and enum bases have one child, so no traversal stack or
  // allocation is needed; aggregate widths use their required layout metadata.
  for (;;) {
    const uint8_t *record = database.data + database.semanticTypes +
                            uint64_t{index} * kSemanticTypeSize;
    const uint32_t kind = read32(record) & UINT32_C(0xff);
    // An unpacked dimension's range is its element count, not a packed width.
    if (kind == OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY)
      return OBELISK_RT_EOF;
    uint64_t width = 0;
    if (!semanticBitWidth(database, index, width))
      return OBELISK_RT_INVALID_DESIGN;
    if (kind != OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY &&
        kind != OBELISK_RT_DESIGN_SEMANTIC_ENUM) {
      if (width == 0)
        return OBELISK_RT_EOF;
      if (multiplier > UINT64_MAX / width)
        return OBELISK_RT_OUT_OF_RESOURCES;
      *outWidth = multiplier * width;
      return OBELISK_RT_OK;
    }
    if (kind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY) {
      if (width == 0 || multiplier > UINT64_MAX / width)
        return OBELISK_RT_OUT_OF_RESOURCES;
      multiplier *= width;
    }
    const uint8_t *edge = database.data + database.semanticTypeEdges +
                          uint64_t{read32(record + 4)} * kSemanticTypeEdgeSize;
    index = read32(edge);
  }
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

// Frame-local lexical owners are either a subroutine object or a retained
// procedural scope statement; they are not restricted to object tables.
bool packedFrameLocalScopeOffset(const Database &database, uint32_t packed,
                                 uint64_t &offset) {
  using obelisk::reflection::TableKind;
  const uint32_t index = obelisk::reflection::unpackTableIndex(packed);
  switch (obelisk::reflection::unpackTableIndexKind(packed)) {
  case TableKind::Object:
    return packedObjectReferenceOffset(database, packed, offset);
  case TableKind::Statement:
    if (index >= database.statementCount)
      return false;
    offset = database.statements + uint64_t{index} * kStatementSize;
    return true;
  default:
    return false;
  }
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

bool frozenValueAt(const Database &database, uint32_t valueIndex,
                   VPIFrozenValue &value) {
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

bool findDefinitionParameterValue(const Database &database,
                                  uint32_t specialization, uint32_t member,
                                  VPIFrozenValue &value) {
  uint32_t semantic = 0, wanted = 0;
  if (!findDefinitionMemberSemanticRoot(database, specialization, member,
                                        semantic, &wanted))
    return false;
  uint32_t low = 0,
           high = static_cast<uint32_t>(database.definitionParameterValueCount);
  while (low != high) {
    uint32_t middle = low + (high - low) / 2;
    const uint8_t *row = database.data + database.definitionParameterValues +
                         uint64_t{middle} * kDefinitionParameterValueSize;
    if (read32(row) < wanted)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == database.definitionParameterValueCount)
    return false;
  const uint8_t *row = database.data + database.definitionParameterValues +
                       uint64_t{low} * kDefinitionParameterValueSize;
  return read32(row) == wanted &&
         frozenValueAt(database, read32(row + 4), value);
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
  return frozenValueAt(database, read32(binding + 4), value);
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

// Recompute class-call compatibility from direct method and inheritance facts.
// Persistent maps share inherited tables, matching ClassDispatchAnalysis and
// the managed runtime without expanding a transitive-closure matrix.
bool validateClassCallDispatch(const Database &database) {
  using namespace obelisk::reflection;
  using MethodMap = obelisk::runtime::DescriptorMap<uint32_t>;
  struct Class {
    uint64_t id;
    uint32_t flags, base = UINT32_MAX;
    size_t pureMethods = 0;
    std::vector<uint32_t> parents, children, methods;
    MethodMap effective, signatures;
  };
  std::vector<Class> classes;
  std::unordered_map<uint64_t, uint32_t> classIndices;
  std::tuple<uint64_t, uint32_t, uint64_t> previousParent{};
  for (uint64_t i = 0; i != database.classDispatchParentBindingCount; ++i) {
    ClassDispatchParentBindingView row(database.data +
                                       database.classDispatchParentBindings +
                                       i * kClassDispatchParentBindingSize);
    auto key =
        std::tuple(row.getClassID(), row.getKind(), row.getParentClassID());
    if (!row.getClassID() || row.getClassID() > INT64_MAX ||
        (i && key <= previousParent) || row.getKind() > 2)
      return false;
    previousParent = key;
    if (row.getKind() == 0) {
      if (row.getParentClassID() || row.getFlags() > 3 ||
          ((row.getFlags() & 1) && !(row.getFlags() & 2)) ||
          !classIndices.emplace(row.getClassID(), classes.size()).second)
        return false;
      classes.push_back({row.getClassID(), row.getFlags()});
    } else if (row.getFlags() || !classIndices.count(row.getClassID())) {
      return false;
    }
  }
  for (uint64_t i = 0; i != database.classDispatchParentBindingCount; ++i) {
    ClassDispatchParentBindingView row(database.data +
                                       database.classDispatchParentBindings +
                                       i * kClassDispatchParentBindingSize);
    if (!row.getKind())
      continue;
    auto parent = classIndices.find(row.getParentClassID());
    uint32_t child = classIndices.at(row.getClassID());
    if (parent == classIndices.end() || child == parent->second)
      return false;
    auto &entry = classes[child];
    bool parentInterface = (classes[parent->second].flags & 1) != 0;
    if (row.getKind() == 1) {
      if (entry.base != UINT32_MAX || (entry.flags & 1) || parentInterface)
        return false;
      entry.base = parent->second;
    } else if (!parentInterface) {
      return false;
    }
    entry.parents.push_back(parent->second);
    classes[parent->second].children.push_back(child);
  }

  auto methodAt = [&](uint32_t index) {
    return ClassDispatchMethodBindingView(
        database.data + database.classDispatchMethodBindings +
        uint64_t{index} * kClassDispatchMethodBindingSize);
  };
  std::unordered_map<uint32_t, uint64_t> reflectedMethodOwners;
  for (uint64_t i = 0; i != database.classMemberBindingCount; ++i) {
    ClassMemberBindingView member(database.data + database.classMemberBindings +
                                  i * kClassMemberBindingSize);
    if (!(member.getFlags() & 2) ||
        unpackTableIndexKind(member.getMemberIndexAndTable()) !=
            TableKind::Object)
      continue;
    uint32_t index = unpackTableIndex(member.getMemberIndexAndTable());
    auto [found, inserted] =
        reflectedMethodOwners.emplace(index, member.getClassID());
    if (!inserted && found->second != member.getClassID())
      return false;
  }
  std::tuple<uint64_t, uint64_t, uint64_t> previousMethod{};
  for (uint32_t i = 0; i != database.classDispatchMethodBindingCount; ++i) {
    auto row = methodAt(i);
    auto key =
        std::tuple(row.getClassID(), row.getSlot(), row.getSignatureID());
    auto owner = classIndices.find(row.getClassID());
    if (owner == classIndices.end() || (i && key <= previousMethod) ||
        row.getSlot() > UINT32_MAX || !row.getSignatureID() ||
        row.getFlags() > 7 || ((row.getFlags() & 2) && !(row.getFlags() & 4)))
      return false;
    bool isInterface = (classes[owner->second].flags & 1) != 0;
    if (isInterface != (row.getSlot() == UINT32_MAX))
      return false;
    if (row.getFlags() & 4) {
      if (row.getCodeUnitIndex() != UINT32_MAX)
        return false;
    } else {
      if (row.getCodeUnitIndex() >= database.objectCount)
        return false;
      const uint8_t *unit = database.data + database.objects +
                            uint64_t{row.getCodeUnitIndex()} * kObjectSize;
      bool task = (row.getFlags() & 1) != 0;
      if (recordKind(unit) != (task ? OBELISK_RT_DESIGN_RECORD_PROCESS
                                    : OBELISK_RT_DESIGN_RECORD_FUNCTION) ||
          recordVPIKind(unit) !=
              static_cast<uint32_t>(task ? VPIObjectKind::Task
                                         : VPIObjectKind::Function))
        return false;
      auto reflected = reflectedMethodOwners.find(row.getCodeUnitIndex());
      if (reflected == reflectedMethodOwners.end() ||
          reflected->second != row.getClassID())
        return false;
    }
    auto &methods = classes[owner->second].methods;
    if (!isInterface && !methods.empty() &&
        methodAt(methods.back()).getSlot() == row.getSlot())
      return false;
    methods.push_back(i);
    previousMethod = key;
  }

  // All inheritance edges participate in cycle detection. Only the ordinary
  // base supplies a vtable; interfaces constrain membership and signatures.
  std::vector<uint32_t> remaining(classes.size()), ready;
  for (uint32_t i = 0; i != classes.size(); ++i) {
    remaining[i] = classes[i].parents.size();
    if (!remaining[i])
      ready.push_back(i);
  }
  for (size_t next = 0; next != ready.size(); ++next) {
    auto &entry = classes[ready[next]];
    if (entry.base != UINT32_MAX) {
      entry.effective = classes[entry.base].effective;
      entry.signatures = classes[entry.base].signatures;
      entry.pureMethods = classes[entry.base].pureMethods;
    }
    if (!(entry.flags & 1)) {
      for (uint32_t index : entry.methods) {
        auto method = methodAt(index);
        if (auto previous = entry.effective.find(method.getSlot())) {
          auto inherited = methodAt(*previous);
          if (inherited.getSignatureID() != method.getSignatureID() ||
              ((inherited.getFlags() ^ method.getFlags()) & 1))
            return false;
          entry.pureMethods -= bool(inherited.getFlags() & 2);
        }
        entry.pureMethods += bool(method.getFlags() & 2);
        entry.effective = entry.effective.set(method.getSlot(), index);
        auto previous = entry.signatures.find(method.getSignatureID());
        if (!previous || methodAt(*previous).getSlot() >= method.getSlot())
          entry.signatures =
              entry.signatures.set(method.getSignatureID(), index);
      }
      if (!(entry.flags & 2) && entry.pureMethods)
        return false;
    }
    for (uint32_t child : entry.children)
      if (--remaining[child] == 0)
        ready.push_back(child);
  }
  if (ready.size() != classes.size())
    return false;

  std::unordered_map<uint64_t, bool> contracts;
  for (uint64_t i = 0; i != database.callSiteBindingCount; ++i) {
    CallSiteBindingView call(database.data + database.callSiteBindings +
                             i * kCallSiteBindingSize);
    if (!(call.getFlags() & 2))
      continue;
    uint32_t kind = 0;
    uint32_t packed = call.getObjectIndexAndTable();
    if (unpackTableIndexKind(packed) == TableKind::Statement) {
      uint32_t index = unpackTableIndex(packed);
      if (index >= database.statementCount)
        return false;
      kind = read16(database.data + database.statements +
                    uint64_t{index} * kStatementSize + 36);
    } else {
      uint64_t offset = 0;
      if (!packedObjectReferenceOffset(database, packed, offset))
        return false;
      kind = objectReferenceVPIKind(database, offset);
    }
    contracts.emplace(
        call.getID(),
        kind == static_cast<uint32_t>(VPIObjectKind::TaskCall) ||
            kind == static_cast<uint32_t>(VPIObjectKind::MethodTaskCall));
  }
  if (contracts.empty())
    return classes.empty() && !database.classDispatchMethodBindingCount &&
           !database.callDispatchBindingCount;
  uint64_t previousCall = 0;
  for (uint64_t i = 0; i != database.callDispatchBindingCount;) {
    CallDispatchBindingView header(database.data +
                                   database.callDispatchBindings +
                                   i * kCallDispatchBindingSize);
    auto contract = contracts.find(header.getCallID());
    auto receiver = classIndices.find(header.getReceiverClassID());
    if (header.getCallID() <= previousCall || header.getCodeUnitID() != 0 ||
        contract == contracts.end() || receiver == classIndices.end() ||
        header.getSlot() > UINT32_MAX || !header.getSignatureID())
      return false;
    bool hasDeclaration = false;
    for (uint32_t methodIndex : classes[receiver->second].methods) {
      auto method = methodAt(methodIndex);
      if (method.getSlot() == header.getSlot() &&
          method.getSignatureID() == header.getSignatureID() &&
          bool(method.getFlags() & 1) == contract->second)
        hasDeclaration = true;
    }
    if (!hasDeclaration)
      return false;
    std::vector<uint64_t> expected;
    std::vector<uint32_t> pending{receiver->second};
    std::unordered_set<uint32_t> visited;
    while (!pending.empty()) {
      uint32_t index = pending.back();
      pending.pop_back();
      if (!visited.insert(index).second)
        continue;
      const auto &candidate = classes[index];
      pending.insert(pending.end(), candidate.children.begin(),
                     candidate.children.end());
      if (candidate.flags & 3)
        continue;
      const uint32_t *selected =
          header.getSlot() == UINT32_MAX
              ? candidate.signatures.find(header.getSignatureID())
              : candidate.effective.find(header.getSlot());
      if (!selected)
        continue;
      auto method = methodAt(*selected);
      if ((method.getFlags() & 6) ||
          bool(method.getFlags() & 1) != contract->second ||
          method.getSignatureID() != header.getSignatureID())
        continue;
      const uint8_t *unit = database.data + database.objects +
                            uint64_t{method.getCodeUnitIndex()} * kObjectSize;
      expected.push_back(read64(unit + 8));
    }
    std::sort(expected.begin(), expected.end());
    expected.erase(std::unique(expected.begin(), expected.end()),
                   expected.end());
    ++i;
    for (uint64_t target : expected) {
      if (i == database.callDispatchBindingCount)
        return false;
      CallDispatchBindingView row(database.data +
                                  database.callDispatchBindings +
                                  i++ * kCallDispatchBindingSize);
      if (row.getCallID() != header.getCallID() ||
          row.getCodeUnitID() != target ||
          row.getReceiverClassID() != header.getReceiverClassID() ||
          row.getSlot() != header.getSlot() ||
          row.getSignatureID() != header.getSignatureID())
        return false;
    }
    previousCall = header.getCallID();
    contracts.erase(contract);
  }
  return contracts.empty();
}

bool findCallableSource(const Database &database, uint32_t codeUnitObjectIndex,
                        uint32_t &packedSource, uint64_t &sourceOffset) {
  using namespace obelisk::reflection;
  if (codeUnitObjectIndex >= database.objectCount)
    return false;
  const uint64_t offset =
      database.objects + uint64_t{codeUnitObjectIndex} * kObjectSize;
  const uint8_t *unit = database.data + offset;
  if (recordKind(unit) != OBELISK_RT_DESIGN_RECORD_PROCESS &&
      recordKind(unit) != OBELISK_RT_DESIGN_RECORD_FUNCTION)
    return false;
  const uint32_t kind = recordVPIKind(unit);
  if (kind == static_cast<uint32_t>(VPIObjectKind::Task) ||
      kind == static_cast<uint32_t>(VPIObjectKind::Function)) {
    sourceOffset = offset;
    return tryPackTableIndex(TableKind::Object, codeUnitObjectIndex,
                             packedSource);
  }
  const uint64_t id = read64(unit + 8);
  uint64_t low = 0, high = database.codeUnitScopeBindingCount;
  while (low < high) {
    uint64_t middle = low + (high - low) / 2;
    CodeUnitScopeBindingView row(database.data +
                                 database.codeUnitScopeBindings +
                                 middle * kCodeUnitScopeBindingSize);
    if (row.getCodeUnitID() < id)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == database.codeUnitScopeBindingCount)
    return false;
  CodeUnitScopeBindingView row(database.data + database.codeUnitScopeBindings +
                               low * kCodeUnitScopeBindingSize);
  if (row.getCodeUnitID() != id)
    return false;
  packedSource = row.getSourceIndexAndTable();
  return packedObjectReferenceOffset(database, packedSource, sourceOffset);
}

bool findInitializerSource(const Database &database, uint64_t codeUnitID,
                           uint32_t &source) {
  using namespace obelisk::reflection;
  uint64_t low = 0, high = database.initializerBindingCount;
  while (low < high) {
    uint64_t middle = low + (high - low) / 2;
    InitializerBindingView row(database.data + database.initializerBindings +
                               middle * kInitializerBindingSize);
    if (row.getCodeUnitID() < codeUnitID)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == database.initializerBindingCount)
    return false;
  InitializerBindingView row(database.data + database.initializerBindings +
                             low * kInitializerBindingSize);
  if (row.getCodeUnitID() != codeUnitID)
    return false;
  source = row.getSourceIndexAndTable();
  return true;
}

uint32_t initializerSourceScope(const Database &database, uint32_t source) {
  using namespace obelisk::reflection;
  auto table = unpackTableIndexKind(source);
  auto index = unpackTableIndex(source);
  auto selector = static_cast<uint32_t>(VPIRelationKind::ScopeRel);
  uint64_t position =
      lowerBoundRelation(database, table, index, selector, false);
  if (position == database.relationCount)
    return UINT32_MAX;
  auto relation = relationAt(database, position);
  return relationMatches(relation, table, index, selector, false)
             ? relation.getTargetIndexAndTable()
             : UINT32_MAX;
}

bool validateInitializers(const Database &database) {
  if (!database.initializerBindingCount)
    return true;
  using namespace obelisk::reflection;
  std::unordered_map<uint64_t, uint32_t> units;
  for (uint32_t index = 0; index != database.objectCount; ++index) {
    const uint8_t *object =
        database.data + database.objects + uint64_t{index} * kObjectSize;
    if (recordKind(object) == OBELISK_RT_DESIGN_RECORD_FUNCTION ||
        recordKind(object) == OBELISK_RT_DESIGN_RECORD_PROCESS)
      if (!units.emplace(read64(object + 8), index).second)
        return false;
  }
  // Public class members and event arrays replace their physical backing.
  std::unordered_map<uint32_t, uint32_t> canonicalStorage;
  std::unordered_set<uint32_t> replacedStorage;
  for (uint64_t index = 0; index != database.classMemberBindingCount; ++index) {
    ClassMemberBindingView row(database.data + database.classMemberBindings +
                               index * kClassMemberBindingSize);
    if (!(row.getFlags() & 1) || (row.getFlags() & 2) ||
        row.getStorageIndexAndTable() == UINT32_MAX)
      continue;
    uint64_t owner = 0;
    if (!packedObjectReferenceOffset(database, row.getOwnerIndexAndTable(),
                                     owner))
      return false;
    if (objectReferenceVPIKind(database, owner) ==
        static_cast<uint32_t>(VPIObjectKind::ClassTypespec))
      canonicalStorage.emplace(row.getMemberIndexAndTable(),
                               row.getStorageIndexAndTable());
    replacedStorage.insert(row.getStorageIndexAndTable());
  }
  for (uint64_t index = 0; index != database.eventArrayStorageBindingCount;
       ++index) {
    EventArrayStorageBindingView row(database.data +
                                     database.eventArrayStorageBindings +
                                     index * kEventArrayStorageBindingSize);
    uint32_t storage = 0;
    if (!tryPackTableIndex(TableKind::Object, row.getStorageIndex(), storage))
      return false;
    canonicalStorage.emplace(row.getArrayIndexAndTable(), storage);
    replacedStorage.insert(storage);
  }
  uint64_t previous = 0;
  std::unordered_set<uint32_t> sources;
  for (uint64_t index = 0; index != database.initializerBindingCount; ++index) {
    InitializerBindingView row(database.data + database.initializerBindings +
                               index * kInitializerBindingSize);
    auto unitIndex = units.find(row.getCodeUnitID());
    uint32_t source = row.getSourceIndexAndTable();
    uint64_t offset = 0;
    if (!row.getCodeUnitID() || row.getCodeUnitID() > INT64_MAX ||
        (index && row.getCodeUnitID() <= previous) || row.getReserved() ||
        unitIndex == units.end() || !sources.insert(source).second ||
        replacedStorage.count(source) ||
        !packedObjectReferenceOffset(database, source, offset) ||
        initializerSourceScope(database, source) == UINT32_MAX)
      return false;
    uint32_t callableSource = 0;
    uint64_t callableOffset = 0;
    if (findCallableSource(database, unitIndex->second, callableSource,
                           callableOffset))
      return false;
    const uint8_t *unit = database.data + database.objects +
                          uint64_t{unitIndex->second} * kObjectSize;
    if (recordKind(unit) != OBELISK_RT_DESIGN_RECORD_FUNCTION ||
        recordVPIKind(unit) ||
        !(read32(unit + 4) & OBELISK_RT_DESIGN_CAP_INTERNAL))
      return false;
    if (auto canonical = canonicalStorage.find(source);
        canonical != canonicalStorage.end())
      if (!packedObjectReferenceOffset(database, canonical->second, offset))
        return false;
    if (isStaticObjectOffset(database, offset) ||
        recordKind(database.data + offset) !=
            OBELISK_RT_DESIGN_RECORD_STORAGE ||
        read64(database.data + offset + 16) != read64(unit + 16))
      return false;
    previous = row.getCodeUnitID();
  }
  return true;
}

bool validateCodeUnitScopes(
    const Database &database,
    const std::unordered_map<uint32_t, std::string_view> &fixedNames) {
  using namespace obelisk::reflection;
  std::unordered_map<uint64_t, uint32_t> units;
  for (uint32_t index = 0; index != database.objectCount; ++index) {
    const uint8_t *unit =
        database.data + database.objects + uint64_t{index} * kObjectSize;
    if (recordKind(unit) == OBELISK_RT_DESIGN_RECORD_PROCESS ||
        recordKind(unit) == OBELISK_RT_DESIGN_RECORD_FUNCTION)
      if (!units.emplace(read64(unit + 8), index).second)
        return false;
  }
  uint64_t previous = 0;
  std::unordered_set<uint32_t> sources;
  for (uint64_t index = 0; index != database.codeUnitScopeBindingCount;
       ++index) {
    CodeUnitScopeBindingView row(database.data +
                                 database.codeUnitScopeBindings +
                                 index * kCodeUnitScopeBindingSize);
    auto found = units.find(row.getCodeUnitID());
    uint64_t sourceOffset = 0;
    const uint32_t source = row.getSourceIndexAndTable();
    if (!row.getCodeUnitID() || row.getCodeUnitID() > INT64_MAX ||
        (index && row.getCodeUnitID() <= previous) || row.getFlags() > 1 ||
        found == units.end() || !sources.insert(source).second ||
        !packedObjectReferenceOffset(database, source, sourceOffset))
      return false;
    const uint8_t *unit = database.data + database.objects +
                          uint64_t{found->second} * kObjectSize;
    const uint8_t *target = database.data + sourceOffset;
    bool compact = isStaticObjectOffset(database, sourceOffset);
    if (recordVPIKind(unit) ||
        !(read32(unit + 4) & OBELISK_RT_DESIGN_CAP_INTERNAL) ||
        (!compact &&
         recordKind(target) != OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT))
      return false;
    const uint32_t kind = objectReferenceVPIKind(database, sourceOffset);
    const bool functionABI =
        recordKind(unit) == OBELISK_RT_DESIGN_RECORD_FUNCTION;
    if (row.getFlags() && !functionABI)
      return false;
    if (kind != static_cast<uint32_t>(functionABI && !row.getFlags()
                                          ? VPIObjectKind::Function
                                          : VPIObjectKind::Task))
      return false;
    uint64_t sourceScope =
        compact ? database.scopes + uint64_t{read32(target + 8)} * kScopeSize
                : read64(target + 16);
    uint64_t sourceName =
        compact ? database.strings + read32(target + 16) : read64(target + 40);
    std::string_view unitHierarchy, sourceHierarchy;
    auto name = fixedNames.find(source);
    if (sourceScope != read64(unit + 16) ||
        !getString(database, read64(unit + 40), unitHierarchy) ||
        !getString(database, sourceName, sourceHierarchy) ||
        unitHierarchy != sourceHierarchy || name == fixedNames.end() ||
        name->second.empty())
      return false;
    previous = row.getCodeUnitID();
  }
  return true;
}

// Interface targets come from declaration-owned nominal membership, not a
// caller-authored candidate list. Original source signatures are retained as
// independent callable facts and checked for every declaration/provider pair.
bool validateInterfaceCallDispatch(
    const Database &database,
    const std::unordered_map<uint32_t, std::string_view> &fixedNames) {
  using namespace obelisk::reflection;
  auto memberAt = [&](uint32_t index) {
    return InterfaceCallableView(database.data + database.interfaceCallables +
                                 uint64_t{index} * kInterfaceCallableSize);
  };
  auto isNominal = [&](uint32_t packed) {
    uint64_t offset = 0;
    uint32_t root = UINT32_MAX;
    if (!packedObjectReferenceOffset(database, packed, offset) ||
        objectReferenceVPIKind(database, offset) !=
            static_cast<uint32_t>(VPIObjectKind::InterfaceTypespec) ||
        !findSemanticRootBinding(database, packed, root) ||
        root >= database.semanticTypeCount)
      return false;
    const uint8_t *semantic = database.data + database.semanticTypes +
                              uint64_t{root} * kSemanticTypeSize;
    std::string_view modport;
    return (read32(semantic) & UINT32_C(0xff)) ==
               OBELISK_RT_DESIGN_SEMANTIC_VIRTUAL_INTERFACE &&
           read32(semantic + 12) == UINT32_MAX &&
           read32(semantic + 16) == packed &&
           getString(database, database.strings + read32(semantic + 24),
                     modport) &&
           modport.empty();
  };
  std::vector<std::string_view> names;
  std::unordered_map<uint32_t, uint32_t> nominalInstanceCounts;
  std::pair<uint32_t, std::string_view> previousMember{};
  for (uint32_t i = 0; i != database.interfaceCallableCount; ++i) {
    auto member = memberAt(i);
    std::string_view name;
    if (!getString(database, database.strings + member.getNameOffset(), name) ||
        name.empty() || name.size() != member.getNameSize() ||
        (member.getNameOffset() &&
         database.data[database.strings + member.getNameOffset() - 1]) ||
        member.getFlags() > 1 || !member.getSignatureID() ||
        member.getReserved() ||
        (member.getKind() != static_cast<uint16_t>(VPIObjectKind::Task) &&
         member.getKind() != static_cast<uint16_t>(VPIObjectKind::Function)) ||
        !isNominal(member.getTypeIndexAndTable()))
      return false;
    auto key = std::pair(member.getTypeIndexAndTable(), name);
    if (i && key <= previousMember)
      return false;
    previousMember = key;
    names.push_back(name);
    nominalInstanceCounts.try_emplace(member.getTypeIndexAndTable(), 0);
  }
  std::unordered_map<uint32_t, uint32_t> instanceTypes;
  uint32_t previousScope = 0;
  for (uint32_t i = 0; i != database.interfaceInstanceCount; ++i) {
    InterfaceInstanceView instance(database.data + database.interfaceInstances +
                                   uint64_t{i} * kInterfaceInstanceSize);
    auto nominal = nominalInstanceCounts.find(instance.getTypeIndexAndTable());
    if (instance.getScopeIndex() >= database.scopeCount ||
        (i && instance.getScopeIndex() <= previousScope) ||
        nominal == nominalInstanceCounts.end() ||
        recordVPIKind(database.data + database.scopes +
                      uint64_t{instance.getScopeIndex()} * kScopeSize) !=
            static_cast<uint32_t>(VPIObjectKind::Interface))
      return false;
    instanceTypes.emplace(instance.getScopeIndex(),
                          instance.getTypeIndexAndTable());
    ++nominal->second;
    previousScope = instance.getScopeIndex();
  }
  std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> signatures;
  uint32_t previousUnit = 0;
  for (uint32_t i = 0; i != database.callableSignatureBindingCount; ++i) {
    CallableSignatureBindingView binding(
        database.data + database.callableSignatureBindings +
        uint64_t{i} * kCallableSignatureBindingSize);
    uint32_t packed = 0;
    uint64_t source = 0;
    if ((i && binding.getCodeUnitIndex() <= previousUnit) ||
        !binding.getSignatureID() || (binding.getFlags() & ~UINT32_C(3)) ||
        !findCallableSource(database, binding.getCodeUnitIndex(), packed,
                            source) ||
        ((binding.getFlags() & 2) &&
         (!(binding.getFlags() & 1) ||
          objectReferenceVPIKind(database, source) !=
              static_cast<uint32_t>(VPIObjectKind::Task))))
      return false;
    signatures.emplace(binding.getCodeUnitIndex(),
                       std::pair(binding.getSignatureID(), binding.getFlags()));
    previousUnit = binding.getCodeUnitIndex();
  }
  std::unordered_set<uint32_t> usedSignatures;
  auto unitMatches = [&](uint32_t index, uint32_t kind, uint32_t signature) {
    auto found = signatures.find(index);
    uint32_t packed = 0;
    uint64_t source = 0;
    if (found == signatures.end() || found->second.first != signature ||
        !findCallableSource(database, index, packed, source) ||
        objectReferenceVPIKind(database, source) != kind)
      return false;
    usedSignatures.insert(index);
    return true;
  };
  std::vector<uint32_t> memberCounts(database.interfaceCallableCount, 0);
  std::pair<uint32_t, uint32_t> previousBinding{};
  for (uint32_t i = 0; i != database.interfaceCallableBindingCount; ++i) {
    InterfaceCallableBindingView binding(
        database.data + database.interfaceCallableBindings +
        uint64_t{i} * kInterfaceCallableBindingSize);
    auto key = std::pair(binding.getMemberIndex(), binding.getScopeIndex());
    auto instance = instanceTypes.find(binding.getScopeIndex());
    if ((i && key <= previousBinding) ||
        binding.getMemberIndex() >= database.interfaceCallableCount ||
        instance == instanceTypes.end())
      return false;
    auto member = memberAt(binding.getMemberIndex());
    if (instance->second != member.getTypeIndexAndTable() ||
        !unitMatches(binding.getDeclarationIndex(), member.getKind(),
                     member.getSignatureID()) ||
        !unitMatches(binding.getImplementationIndex(), member.getKind(),
                     member.getSignatureID()) ||
        (!member.getFlags() &&
         binding.getDeclarationIndex() != binding.getImplementationIndex()))
      return false;
    uint32_t declarationFlags =
        signatures.at(binding.getDeclarationIndex()).second;
    bool external = declarationFlags & 1;
    bool forkJoin = declarationFlags & 2;
    bool self =
        binding.getDeclarationIndex() == binding.getImplementationIndex();
    if (external != static_cast<bool>(member.getFlags()) ||
        (external && self != forkJoin) ||
        (!self && signatures.at(binding.getImplementationIndex()).second))
      return false;
    uint32_t packed = 0;
    uint64_t declaration = 0;
    if (!findCallableSource(database, binding.getDeclarationIndex(), packed,
                            declaration))
      return false;
    uint64_t scope =
        isStaticObjectOffset(database, declaration)
            ? database.scopes +
                  uint64_t{read32(database.data + declaration + 8)} * kScopeSize
            : read64(database.data + declaration + 16);
    if (scope !=
        database.scopes + uint64_t{binding.getScopeIndex()} * kScopeSize)
      return false;
    auto name = fixedNames.find(packed);
    if (name == fixedNames.end() ||
        name->second != names[binding.getMemberIndex()])
      return false;
    ++memberCounts[binding.getMemberIndex()];
    previousBinding = key;
  }
  if (usedSignatures.size() != signatures.size())
    return false;
  for (uint32_t i = 0; i != database.interfaceCallableCount; ++i)
    if (memberCounts[i] !=
        nominalInstanceCounts.at(memberAt(i).getTypeIndexAndTable()))
      return false;

  std::unordered_map<uint64_t, uint32_t> contracts;
  for (uint64_t i = 0; i != database.callSiteBindingCount; ++i) {
    CallSiteBindingView call(database.data + database.callSiteBindings +
                             i * kCallSiteBindingSize);
    if (call.getFlags() & 4)
      contracts.emplace(call.getID(), call.getObjectIndexAndTable());
  }
  std::vector<bool> usedMembers(database.interfaceCallableCount, false);
  uint64_t previousCall = 0;
  for (uint64_t i = 0; i != database.interfaceCallBindingCount; ++i) {
    InterfaceCallBindingView binding(database.data +
                                     database.interfaceCallBindings +
                                     i * kInterfaceCallBindingSize);
    auto call = contracts.find(binding.getCallID());
    if (!binding.getCallID() || binding.getCallID() <= previousCall ||
        call == contracts.end() || binding.getAccess() > 2 ||
        binding.getMemberIndex() >= database.interfaceCallableCount)
      return false;
    auto member = memberAt(binding.getMemberIndex());
    uint32_t packed = call->second;
    uint32_t kind = 0;
    if (unpackTableIndexKind(packed) == TableKind::Statement) {
      kind = read16(database.data + database.statements +
                    uint64_t{unpackTableIndex(packed)} * kStatementSize + 36);
    } else {
      const uint8_t *object = database.data + database.objects +
                              uint64_t{unpackTableIndex(packed)} * kObjectSize;
      kind = recordVPIKind(object);
      std::string_view name;
      if (!getString(database, read64(object + 40), name) ||
          name != names[binding.getMemberIndex()])
        return false;
    }
    bool task = kind == static_cast<uint32_t>(VPIObjectKind::TaskCall) ||
                kind == static_cast<uint32_t>(VPIObjectKind::MethodTaskCall);
    bool function =
        kind == static_cast<uint32_t>(VPIObjectKind::FuncCall) ||
        kind == static_cast<uint32_t>(VPIObjectKind::MethodFuncCall);
    if ((!task && !function) ||
        member.getKind() !=
            static_cast<uint16_t>(task ? VPIObjectKind::Task
                                       : VPIObjectKind::Function) ||
        (binding.getAccess() == 2 && !member.getFlags()))
      return false;
    auto name = fixedNames.find(packed);
    if (name != fixedNames.end() &&
        name->second != names[binding.getMemberIndex()])
      return false;
    usedMembers[binding.getMemberIndex()] = true;
    previousCall = binding.getCallID();
    contracts.erase(call);
  }
  return contracts.empty() &&
         std::all_of(usedMembers.begin(), usedMembers.end(),
                     [](bool used) { return used; });
}

constexpr uint32_t lexicalInheritanceFlags =
    OBELISK_RT_DESIGN_CAP_LEXICAL_CLASS_TYPESPEC |
    OBELISK_RT_DESIGN_CAP_UNAVAILABLE_CLASS_BASE |
    OBELISK_RT_DESIGN_CAP_UNAVAILABLE_CONSTRUCTOR_ARGUMENTS |
    OBELISK_RT_DESIGN_CAP_UNAVAILABLE_CLASS_PARAMETERS |
    OBELISK_RT_DESIGN_CAP_DEFAULT_CLASS_PARAMETER_ASSIGNMENT |
    OBELISK_RT_DESIGN_CAP_EXPLICIT_CLASS_PARAMETER_ASSIGNMENT |
    OBELISK_RT_DESIGN_CAP_UNAVAILABLE_CLASS_PARAMETER_RHS;

bool validateLexicalInheritance(
    const Database &database,
    const std::unordered_map<uint32_t, std::string_view> &fixedNames) {
  using namespace obelisk::reflection;
  using Kind = VPIObjectKind;
  using Rel = VPIRelationKind;
  constexpr uint32_t missing = UINT32_MAX;
  auto relationEndpoint = [](TableKind table, uint32_t index) {
    return (static_cast<uint32_t>(table) << 30) | index;
  };
  auto offsetFor = [&](uint32_t packed) {
    uint64_t offset = 0;
    return packedObjectReferenceOffset(database, packed, offset) ? offset : 0;
  };
  auto kindFor = [&](uint32_t packed) {
    uint64_t offset = offsetFor(packed);
    return offset ? objectReferenceVPIKind(database, offset) : 0u;
  };
  auto capsFor = [&](uint32_t packed) {
    uint64_t offset = offsetFor(packed);
    if (!offset)
      return 0u;
    return isStaticObjectOffset(database, offset)
               ? uint32_t{read16(database.data + offset + 30)}
           : recordKind(database.data + offset) ==
                   OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT
               ? read32(database.data + offset + 4)
               : 0u;
  };
  auto scopeFor = [&](uint32_t packed) {
    uint64_t offset = offsetFor(packed);
    if (!offset)
      return uint64_t{0};
    return isStaticObjectOffset(database, offset)
               ? database.scopes +
                     uint64_t{read32(database.data + offset + 8)} * kScopeSize
               : read64(database.data + offset + 16);
  };
  auto isUnnamed = [&](uint32_t packed) {
    uint64_t offset = offsetFor(packed);
    if (!offset)
      return false;
    uint64_t name = isStaticObjectOffset(database, offset)
                        ? database.strings + read32(database.data + offset + 16)
                        : read64(database.data + offset + 40);
    std::string_view value;
    return getString(database, name, value) && value.empty();
  };
  auto handle = [&](uint32_t packed, uint32_t selector) {
    auto table = unpackTableIndexKind(packed);
    auto index = unpackTableIndex(packed);
    auto position = lowerBoundRelation(database, table, index, selector, false);
    if (position == database.relationCount)
      return missing;
    auto relation = relationAt(database, position);
    return relationMatches(relation, table, index, selector, false)
               ? relation.getTargetIndexAndTable()
               : missing;
  };
  auto iterate = [&](uint32_t packed, uint32_t selector) {
    std::vector<uint32_t> result;
    auto table = unpackTableIndexKind(packed);
    auto index = unpackTableIndex(packed);
    auto position = lowerBoundRelation(database, table, index, selector, true);
    while (position < database.relationCount) {
      auto relation = relationAt(database, position++);
      if (!relationMatches(relation, table, index, selector, true))
        break;
      result.push_back(relation.getTargetIndexAndTable());
    }
    return result;
  };
  auto isLocalParameter = [&](uint32_t packed) {
    for (uint64_t index = 0; index != database.fixedPropertyCount; ++index) {
      const uint8_t *property =
          database.data + database.fixedProperties + index * kFixedPropertySize;
      if (read32(property) == packed && read16(property + 4) == 70)
        return read64(property + 8) != 0;
    }
    return false;
  };
  std::unordered_set<uint32_t> types, assignments, clauses;
  std::unordered_map<uint32_t, uint32_t> assignmentOwners, typeOccurrences,
      clauseOwners;
  for (uint32_t index = 0; index != database.objectCount; ++index) {
    const uint8_t *object =
        database.data + database.objects + uint64_t{index} * kObjectSize;
    if (recordKind(object) != OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT)
      continue;
    uint32_t packed = relationEndpoint(TableKind::Object, index);
    uint32_t caps = read32(object + 4), kind = recordVPIKind(object);
    if (kind == static_cast<uint32_t>(Kind::Extends))
      clauses.insert(packed);
    if (!(caps & lexicalInheritanceFlags) &&
        kind != static_cast<uint32_t>(Kind::Extends))
      continue;
    if (read64(object + 48) != 0 || read64(object + 56) != 0 ||
        read64(object + 64) != 0 || read64(object + 72) != 0 ||
        read64(object + 80) != 0)
      return false;
    uint32_t semantic = 0;
    if (findSemanticRootBinding(database, packed, semantic))
      return false;
    if (caps & OBELISK_RT_DESIGN_CAP_LEXICAL_CLASS_TYPESPEC) {
      if (kind != static_cast<uint32_t>(Kind::ClassTypespec) ||
          (caps & ~(OBELISK_RT_DESIGN_CAP_LEXICAL_CLASS_TYPESPEC |
                    OBELISK_RT_DESIGN_CAP_UNAVAILABLE_CLASS_PARAMETERS)) != 0)
        return false;
      auto name = fixedNames.find(packed);
      std::string_view recordName;
      if (name == fixedNames.end() || name->second.empty() ||
          !getString(database, read64(object + 40), recordName) ||
          name->second != recordName)
        return false;
      types.insert(packed);
    } else if (caps &
               (OBELISK_RT_DESIGN_CAP_DEFAULT_CLASS_PARAMETER_ASSIGNMENT |
                OBELISK_RT_DESIGN_CAP_EXPLICIT_CLASS_PARAMETER_ASSIGNMENT)) {
      bool originalDefault =
          caps & OBELISK_RT_DESIGN_CAP_DEFAULT_CLASS_PARAMETER_ASSIGNMENT;
      uint32_t allowed =
          originalDefault
              ? OBELISK_RT_DESIGN_CAP_DEFAULT_CLASS_PARAMETER_ASSIGNMENT
              : OBELISK_RT_DESIGN_CAP_EXPLICIT_CLASS_PARAMETER_ASSIGNMENT |
                    OBELISK_RT_DESIGN_CAP_UNAVAILABLE_CLASS_PARAMETER_RHS;
      if (kind != static_cast<uint32_t>(Kind::ParamAssign) || (caps & ~allowed))
        return false;
      assignments.insert(packed);
    } else if (kind != static_cast<uint32_t>(Kind::Extends) ||
               (caps &
                ~(OBELISK_RT_DESIGN_CAP_UNAVAILABLE_CLASS_BASE |
                  OBELISK_RT_DESIGN_CAP_UNAVAILABLE_CONSTRUCTOR_ARGUMENTS)) !=
                   0) {
      return false;
    }
  }
  for (uint64_t index = 0; index != database.fixedPropertyCount; ++index) {
    const uint8_t *property =
        database.data + database.fixedProperties + index * kFixedPropertySize;
    uint32_t source = read32(property);
    if ((types.count(source) && read16(property + 4) != 2) ||
        (assignments.count(source) && read16(property + 4) != 21) ||
        (capsFor(source) &
         (OBELISK_RT_DESIGN_CAP_UNAVAILABLE_CLASS_BASE |
          OBELISK_RT_DESIGN_CAP_UNAVAILABLE_CONSTRUCTOR_ARGUMENTS)))
      return false;
  }
  for (uint64_t index = 0; index != database.relationCount; ++index) {
    auto relation = relationAt(database, index);
    uint32_t packedSource = relation.getSourceKindAndTable();
    auto table = unpackRelationSourceTable(packedSource);
    uint32_t from = relationEndpoint(table, relation.getSourceIndex());
    uint32_t to = relation.getTargetIndexAndTable();
    uint32_t selector = relation.getSelector();
    bool many = relationSourceIsIterate(packedSource);
    if (types.count(from)) {
      bool allowed =
          (!many && (selector == static_cast<uint32_t>(Kind::ClassDefn) ||
                     selector == static_cast<uint32_t>(Rel::ScopeRel) ||
                     selector == static_cast<uint32_t>(Rel::ExtendsRel))) ||
          (many && (selector == static_cast<uint32_t>(Kind::ParamAssign) ||
                    selector == static_cast<uint32_t>(Kind::Parameter)));
      if (!allowed)
        return false;
      if (many && selector == static_cast<uint32_t>(Kind::ParamAssign) &&
          (!assignments.count(to) ||
           !assignmentOwners.emplace(to, from).second))
        return false;
    }
    if (types.count(to)) {
      if (many)
        return false;
      if (selector == static_cast<uint32_t>(Kind::ClassTypespec) &&
          clauses.count(from)) {
        if (!typeOccurrences.emplace(to, from).second)
          return false;
      } else if (!(selector == static_cast<uint32_t>(Rel::ExtendsRel) &&
                   types.count(from)) &&
                 !(selector == static_cast<uint32_t>(Rel::ScopeRel) &&
                   assignments.count(from))) {
        return false;
      }
    }
    if (assignments.count(to) &&
        (!many || selector != static_cast<uint32_t>(Kind::ParamAssign) ||
         !types.count(from)))
      return false;
    if (assignments.count(from) &&
        (many || (selector != static_cast<uint32_t>(Rel::LhsRel) &&
                  selector != static_cast<uint32_t>(Rel::RhsRel) &&
                  selector != static_cast<uint32_t>(Rel::ScopeRel))))
      return false;
    if (clauses.count(to) && !many &&
        selector == static_cast<uint32_t>(Rel::ExtendsRel)) {
      if (kindFor(from) != static_cast<uint32_t>(Kind::ClassDefn) ||
          !clauseOwners.emplace(to, from).second)
        return false;
    }
  }
  for (uint32_t clause : clauses) {
    uint32_t owner = handle(clause, static_cast<uint32_t>(Kind::ClassDefn));
    auto incoming = clauseOwners.find(clause);
    if (incoming == clauseOwners.end() || incoming->second != owner ||
        kindFor(owner) != static_cast<uint32_t>(Kind::ClassDefn) ||
        scopeFor(clause) != scopeFor(owner))
      return false;
    uint32_t base = handle(clause, static_cast<uint32_t>(Kind::ClassTypespec));
    bool unavailable =
        capsFor(clause) & OBELISK_RT_DESIGN_CAP_UNAVAILABLE_CLASS_BASE;
    if (unavailable ? base != missing : base == missing)
      return false;
    if ((capsFor(clause) &
         OBELISK_RT_DESIGN_CAP_UNAVAILABLE_CONSTRUCTOR_ARGUMENTS) &&
        !iterate(clause, static_cast<uint32_t>(Rel::ArgumentRel)).empty())
      return false;
  }
  std::unordered_map<uint32_t, uint32_t> actualTypeOwners;
  for (uint32_t type : types) {
    uint32_t owner = handle(type, static_cast<uint32_t>(Rel::ScopeRel));
    uint32_t definition = handle(type, static_cast<uint32_t>(Kind::ClassDefn));
    auto occurrence = typeOccurrences.find(type);
    if (kindFor(owner) != static_cast<uint32_t>(Kind::ClassDefn) ||
        kindFor(definition) != static_cast<uint32_t>(Kind::ClassDefn) ||
        owner == definition || scopeFor(type) != scopeFor(owner) ||
        occurrence == typeOccurrences.end() ||
        handle(occurrence->second, static_cast<uint32_t>(Kind::ClassDefn)) !=
            owner)
      return false;
    auto definitionName = fixedNames.find(definition);
    if (definitionName == fixedNames.end() ||
        definitionName->second != fixedNames.at(type))
      return false;
    auto actuals = iterate(type, static_cast<uint32_t>(Kind::ParamAssign));
    auto formals = iterate(definition, static_cast<uint32_t>(Kind::Parameter));
    if (iterate(type, static_cast<uint32_t>(Kind::Parameter)) != formals)
      return false;
    formals.erase(
        std::remove_if(formals.begin(), formals.end(), isLocalParameter),
        formals.end());
    if (capsFor(type) & OBELISK_RT_DESIGN_CAP_UNAVAILABLE_CLASS_PARAMETERS) {
      if (!actuals.empty())
        return false;
    } else {
      if (actuals.size() != formals.size())
        return false;
      for (size_t index = 0; index != actuals.size(); ++index) {
        if (scopeFor(actuals[index]) != scopeFor(type) ||
            handle(actuals[index], static_cast<uint32_t>(Rel::ScopeRel)) !=
                type ||
            handle(actuals[index], static_cast<uint32_t>(Rel::LhsRel)) !=
                formals[index])
          return false;
        uint32_t actual = actuals[index];
        uint32_t rhs = handle(actual, static_cast<uint32_t>(Rel::RhsRel));
        uint32_t caps = capsFor(actual);
        if (caps & OBELISK_RT_DESIGN_CAP_DEFAULT_CLASS_PARAMETER_ASSIGNMENT) {
          if (rhs !=
              handle(formals[index], static_cast<uint32_t>(Rel::ExprRel)))
            return false;
          continue;
        }
        if (caps & OBELISK_RT_DESIGN_CAP_UNAVAILABLE_CLASS_PARAMETER_RHS) {
          if (rhs != missing)
            return false;
          continue;
        }
        if (rhs == missing)
          return false;
        bool typeActual = kindFor(formals[index]) ==
                          static_cast<uint32_t>(Kind::TypeParameter);
        uint32_t rhsKind = kindFor(rhs);
        if (rhsKind == static_cast<uint32_t>(typeActual ? Kind::TypeParameter
                                                        : Kind::Parameter)) {
          auto derivedParameters =
              iterate(owner, static_cast<uint32_t>(Kind::Parameter));
          if (std::find(derivedParameters.begin(), derivedParameters.end(),
                        rhs) == derivedParameters.end() ||
              isLocalParameter(rhs))
            return false;
        } else if (typeActual) {
          if (!vpiObjectSetContains(VPIObjectSetID::ConcreteTypespecObjects,
                                    rhsKind) ||
              (capsFor(rhs) & OBELISK_RT_DESIGN_CAP_NAMED_TYPESPEC) ||
              !isUnnamed(rhs) || scopeFor(rhs) != scopeFor(actual) ||
              !actualTypeOwners.emplace(rhs, actual).second)
            return false;
        } else if ((rhsKind != static_cast<uint32_t>(Kind::Constant) &&
                    rhsKind != static_cast<uint32_t>(Kind::Operation)) ||
                   scopeFor(rhs) != scopeFor(actual)) {
          return false;
        }
      }
    }
    uint32_t inheritedClause =
        handle(definition, static_cast<uint32_t>(Rel::ExtendsRel));
    uint32_t expectedBase =
        inheritedClause == missing
            ? missing
            : handle(inheritedClause,
                     static_cast<uint32_t>(Kind::ClassTypespec));
    if (handle(type, static_cast<uint32_t>(Rel::ExtendsRel)) != expectedBase)
      return false;
    std::unordered_set<uint32_t> seen;
    uint32_t cursor = type;
    while (types.count(cursor)) {
      if (!seen.insert(cursor).second)
        return false;
      cursor = handle(cursor, static_cast<uint32_t>(Rel::ExtendsRel));
    }
  }
  for (uint64_t index = 0; index != database.relationCount; ++index) {
    auto relation = relationAt(database, index);
    auto owner = actualTypeOwners.find(relation.getTargetIndexAndTable());
    if (owner == actualTypeOwners.end())
      continue;
    uint32_t source = relationEndpoint(
        unpackRelationSourceTable(relation.getSourceKindAndTable()),
        relation.getSourceIndex());
    bool reverseEnum =
        !relationSourceIsIterate(relation.getSourceKindAndTable()) &&
        relation.getSelector() == static_cast<uint32_t>(Kind::EnumTypespec) &&
        kindFor(source) == static_cast<uint32_t>(Kind::EnumConst);
    if (reverseEnum) {
      auto members =
          iterate(owner->first, static_cast<uint32_t>(Kind::EnumConst));
      if (std::find(members.begin(), members.end(), source) == members.end())
        return false;
    } else if (source != owner->second ||
               relationSourceIsIterate(relation.getSourceKindAndTable()) ||
               relation.getSelector() != static_cast<uint32_t>(Rel::RhsRel)) {
      return false;
    }
  }
  return assignmentOwners.size() == assignments.size();
}

constexpr uint32_t kExpressionCursorFlag = UINT32_C(1) << 30;
constexpr uint32_t kExpressionOperandLeafFlag = UINT32_C(1) << 31;
constexpr uint32_t kExpressionInstanceRelation = UINT32_MAX - 1;
constexpr uint32_t kExpressionModuleRelation = UINT32_MAX - 2;
constexpr uint32_t kExpressionUseOwnerRelation = UINT32_MAX - 3;
constexpr uint32_t kDefaultInstanceRelation = UINT32_MAX - 4;
constexpr uint32_t kDefaultModuleRelation = UINT32_MAX - 5;
// A root group iterates root indices in the low payload bits. This domain is
// disjoint from operand indices (<2^30) and the high reserved sentinels.
constexpr uint32_t kExpressionRootGroupFlag = UINT32_C(1) << 31;
constexpr uint32_t kExpressionPayloadDomainMask = UINT32_C(3) << 30;

const uint8_t *expressionRoot(const Database &database, uint32_t index) {
  return index < database.expressionRootBindingCount
             ? database.data + database.expressionRootBindings +
                   uint64_t{index} * kExpressionRootBindingSize
             : nullptr;
}
bool isConcreteTypeDefault(const uint8_t *root) {
  return (read16(root + 14) &
          obelisk::reflection::expressionRootConcreteTypeDefault) != 0;
}
const uint8_t *expressionTemplate(const Database &database,
                                  const uint8_t *root) {
  if (isConcreteTypeDefault(root))
    return nullptr;
  uint32_t index = read32(root + 4);
  return index < database.expressionTemplateCount
             ? database.data + database.expressionTemplates +
                   uint64_t{index} * kExpressionTemplateSize
             : nullptr;
}
const uint8_t *expressionNode(const Database &database, const uint8_t *recipe,
                              uint32_t ordinal) {
  uint32_t first = read32(recipe + 4), count = read32(recipe + 8);
  return ordinal < count &&
                 uint64_t{first} + ordinal < database.expressionNodeCount
             ? database.data + database.expressionNodes +
                   (uint64_t{first} + ordinal) * kExpressionNodeSize
             : nullptr;
}
bool isDefinitionDefault(const uint8_t *root) {
  return (read16(root + 14) &
          obelisk::reflection::expressionRootDefinitionMember) != 0;
}
// The node pools have globally unique source occurrences. A default cursor
// combines that occurrence with the querying instance binding, without an
// image row per instance. Ordinary expression cursors retain their root index.
bool definitionDefaultForNode(const Database &database, uint32_t globalNode,
                              uint32_t &rootIndex, uint32_t &ordinal,
                              const uint8_t *&root, const uint8_t *&recipe) {
  for (uint32_t index = 0; index != database.expressionRootBindingCount;
       ++index) {
    const uint8_t *candidate = expressionRoot(database, index);
    const uint8_t *type = expressionTemplate(database, candidate);
    if (!isDefinitionDefault(candidate) || !type)
      continue;
    uint32_t first = read32(type + 4);
    if (globalNode >= first &&
        uint64_t{globalNode} - first < read32(type + 8)) {
      rootIndex = index;
      ordinal = globalNode - first;
      root = candidate;
      recipe = type;
      return true;
    }
  }
  return false;
}
bool expressionForCursor(const Database &database, uint64_t cursor,
                         uint32_t &rootIndex, uint32_t &ordinal,
                         const uint8_t *&root, const uint8_t *&recipe,
                         const uint8_t *&node,
                         uint32_t *outDefaultBinding = nullptr) {
  if (outDefaultBinding)
    *outDefaultBinding = UINT32_MAX;
  uint32_t payload = 0, context = 0;
  if (!decodeVirtualToken(cursor, kVirtualMemberTag, context, payload))
    return false;
  uint32_t domain = payload & kExpressionPayloadDomainMask;
  if (domain == kExpressionPayloadDomainMask) {
    if (!definitionDefaultForNode(database,
                                  payload & ~kExpressionPayloadDomainMask,
                                  rootIndex, ordinal, root, recipe))
      return false;
    uint32_t scope = 0, memberIndex = 0;
    const uint8_t *member = nullptr;
    if (!virtualMemberForCursor(
            database, virtualToken(kVirtualMemberTag, context, read32(root)),
            scope, memberIndex, member))
      return false;
    if (outDefaultBinding)
      *outDefaultBinding = context;
  } else {
    rootIndex = context;
    if (domain != kExpressionCursorFlag ||
        !(root = expressionRoot(database, rootIndex)) ||
        isDefinitionDefault(root) ||
        !(recipe = expressionTemplate(database, root)))
      return false;
    ordinal = payload & ~kExpressionCursorFlag;
  }
  node = expressionNode(database, recipe, ordinal);
  return node != nullptr;
}
bool expressionContext(const Database &database, const uint8_t *root,
                       uint32_t &scope, uint32_t *outKind = nullptr,
                       uint64_t *outOwner = nullptr,
                       uint32_t defaultBinding = UINT32_MAX) {
  using obelisk::reflection::TableKind;
  if (isDefinitionDefault(root)) {
    uint32_t memberIndex = 0;
    const uint8_t *member = nullptr;
    uint64_t cursor =
        virtualToken(kVirtualMemberTag, defaultBinding, read32(root));
    if (defaultBinding == UINT32_MAX ||
        !virtualMemberForCursor(database, cursor, scope, memberIndex, member))
      return false;
    if (outKind)
      *outKind = read16(member + 16);
    if (outOwner)
      *outOwner = cursor;
    return true;
  }
  uint32_t packed = read32(root),
           index = obelisk::reflection::unpackTableIndex(packed);
  uint64_t offset = 0;
  uint32_t kind = 0;
  switch (obelisk::reflection::unpackTableIndexKind(packed)) {
  case TableKind::Statement:
    if (index >= database.statementCount)
      return false;
    offset = database.statements + uint64_t{index} * kStatementSize;
    scope = read32(database.data + offset + 12);
    kind = read16(database.data + offset + 36);
    break;
  case TableKind::Object: {
    if (index >= database.objectCount)
      return false;
    offset = database.objects + uint64_t{index} * kObjectSize;
    uint64_t parent = read64(database.data + offset + 16);
    if (!isScopeOffset(database, parent))
      return false;
    scope = (parent - database.scopes) / kScopeSize;
    kind = recordVPIKind(database.data + offset);
    break;
  }
  case TableKind::StaticObject:
    if (index >= database.staticObjectCount)
      return false;
    offset = database.staticObjects + uint64_t{index} * kStaticObjectSize;
    scope = read32(database.data + offset + 8);
    kind = read16(database.data + offset + 28);
    break;
  default:
    return false;
  }
  if (scope >= database.scopeCount)
    return false;
  if (kind == uint32_t(obelisk::reflection::VPIObjectKind::Port) &&
      read16(root + 12) ==
          uint32_t(obelisk::reflection::VPIRelationKind::HighConnRel)) {
    uint64_t parent = read64(database.data + database.scopes +
                             uint64_t{scope} * kScopeSize + 16);
    if (!isScopeOffset(database, parent))
      return false;
    scope = (parent - database.scopes) / kScopeSize;
  }
  if (outKind)
    *outKind = kind;
  if (outOwner)
    *outOwner = offset;
  return true;
}

// Case items group labels; the LRM simple-expression Use target is the
// enclosing case statement, using the existing containment identity.
bool expressionUseOwner(const Database &database, const uint8_t *root,
                        uint32_t &kind, uint64_t &offset) {
  uint32_t scope = 0;
  if (!expressionContext(database, root, scope, &kind, &offset))
    return false;
  using Kind = obelisk::reflection::VPIObjectKind;
  if (kind != uint32_t(Kind::CaseItem))
    return true;
  if (!isStatementOffset(database, offset))
    return false;
  uint32_t parent = read32(database.data + offset + 16);
  if (parent >= database.statementCount)
    return false;
  offset = database.statements + uint64_t{parent} * kStatementSize;
  kind = read16(database.data + offset + 36);
  return kind == uint32_t(Kind::Case);
}

struct ExpressionLeafInfo {
  uint64_t cursor = 0;
  uint32_t kind = 0, semantic = 0;
};
bool expressionInstance(const Database &database, const uint8_t *root,
                        bool requireModule, uint64_t &offset, uint32_t &kind,
                        uint32_t defaultBinding = UINT32_MAX) {
  uint32_t scope = 0;
  if (!expressionContext(database, root, scope, nullptr, nullptr,
                         defaultBinding))
    return false;
  offset = database.scopes + uint64_t{scope} * kScopeSize;
  using Kind = obelisk::reflection::VPIObjectKind;
  for (;;) {
    kind = recordVPIKind(database.data + offset);
    if (kind == uint32_t(Kind::Module) || kind == uint32_t(Kind::Interface) ||
        kind == uint32_t(Kind::Program) || kind == uint32_t(Kind::Package))
      return !requireModule || kind == uint32_t(Kind::Module);
    offset = read64(database.data + offset + 16);
    if (!isScopeOffset(database, offset))
      return false;
  }
}
bool expressionLeaf(const Database &database, const uint8_t *root,
                    uint32_t ordinal, ExpressionLeafInfo &out,
                    uint32_t defaultBinding = UINT32_MAX) {
  uint64_t index = uint64_t{read32(root + 8)} + ordinal;
  if (index >= database.expressionLeafCount)
    return false;
  uint32_t encoded = read32(database.data + database.expressionLeaves +
                            index * kExpressionLeafSize);
  uint32_t tag = encoded >> 30, payload = encoded & ((UINT32_C(1) << 30) - 1);
  if (tag == 1 || tag == 3) {
    uint32_t packed = 0;
    if (!obelisk::reflection::tryPackTableIndex(
            tag == 1 ? obelisk::reflection::TableKind::Object
                     : obelisk::reflection::TableKind::StaticObject,
            payload, packed) ||
        !packedObjectReferenceOffset(database, packed, out.cursor) ||
        !findSemanticRootBinding(database, packed, out.semantic))
      return false;
    out.kind = objectReferenceVPIKind(database, out.cursor);
    return true;
  }
  uint32_t binding = 0, memberIndex = payload;
  if (tag == 0) {
    uint32_t scope = 0, definition = 0, specialization = 0;
    if (!expressionContext(database, root, scope, nullptr, nullptr,
                           defaultBinding) ||
        !findDefinitionBinding(database, scope, definition, specialization,
                               &binding))
      return false;
  } else {
    if (payload >= database.expressionBoundMemberCount)
      return false;
    const uint8_t *pair = database.data + database.expressionBoundMembers +
                          uint64_t{payload} * kExpressionBoundMemberSize;
    binding = read32(pair);
    memberIndex = read32(pair + 4);
  }
  out.cursor = virtualToken(kVirtualMemberTag, binding, memberIndex);
  uint32_t scope = 0, checkedMember = 0, specialization = 0;
  const uint8_t *member = nullptr;
  if (!virtualMemberForCursor(database, out.cursor, scope, checkedMember,
                              member, nullptr, &specialization) ||
      !findDefinitionMemberSemanticRoot(database, specialization, checkedMember,
                                        out.semantic))
    return false;
  out.kind = read16(member + 16);
  return true;
}
bool expressionIntegral(const Database &database, uint32_t semantic,
                        uint64_t &width, uint32_t &flags) {
  if (semantic >= database.semanticTypeCount ||
      semanticPackedBitWidth(database, semantic, &width) != OBELISK_RT_OK ||
      !width)
    return false;
  flags = read32(database.data + database.semanticTypes +
                 uint64_t{semantic} * kSemanticTypeSize);
  uint32_t kind = flags & UINT32_C(0xff);
  while (kind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY) {
    const uint8_t *record = database.data + database.semanticTypes +
                            uint64_t{semantic} * kSemanticTypeSize;
    const uint8_t *edge = database.data + database.semanticTypeEdges +
                          uint64_t{read32(record + 4)} * kSemanticTypeEdgeSize;
    semantic = read32(edge);
    kind = read32(database.data + database.semanticTypes +
                  uint64_t{semantic} * kSemanticTypeSize) &
           UINT32_C(0xff);
  }
  switch (kind) {
  case OBELISK_RT_DESIGN_SEMANTIC_GENERIC_INTEGRAL:
  case OBELISK_RT_DESIGN_SEMANTIC_BIT:
  case OBELISK_RT_DESIGN_SEMANTIC_LOGIC:
  case OBELISK_RT_DESIGN_SEMANTIC_REG:
  case OBELISK_RT_DESIGN_SEMANTIC_BYTE:
  case OBELISK_RT_DESIGN_SEMANTIC_SHORT_INT:
  case OBELISK_RT_DESIGN_SEMANTIC_INT:
  case OBELISK_RT_DESIGN_SEMANTIC_LONG_INT:
  case OBELISK_RT_DESIGN_SEMANTIC_INTEGER:
  case OBELISK_RT_DESIGN_SEMANTIC_TIME:
    return true;
  default:
    return false;
  }
}
bool expressionFrozen(const Database &database, uint32_t index,
                      VPIFrozenValue &out) {
  if (index >= database.frozenValueCount)
    return false;
  const uint8_t *record = database.data + database.frozenValues +
                          uint64_t{index} * kFrozenValueSize;
  out.kindAndFlags = read32(record);
  out.bitWidth = read64(record + 8);
  out.payload =
      database.data + database.frozenValuePayload + read64(record + 16);
  out.payloadSize = read64(record + 24);
  return true;
}
// Elaboration constancy is a syntactic property. Unknown/out-of-range constant
// indices remain constant even when they do not currently denote an object.
bool expressionOperandIsConstant(const Database &database, const uint8_t *root,
                                 const uint8_t *recipe, uint32_t operand) {
  OBELISK_RT_TRY {
    std::vector<uint32_t> pending{operand};
    while (!pending.empty()) {
      uint32_t current = pending.back();
      pending.pop_back();
      if (current & kExpressionOperandLeafFlag) {
        ExpressionLeafInfo leaf;
        if (!expressionLeaf(database, root,
                            current & ~kExpressionOperandLeafFlag, leaf) ||
            (leaf.kind !=
                 uint32_t(obelisk::reflection::VPIObjectKind::Parameter) &&
             leaf.kind !=
                 uint32_t(obelisk::reflection::VPIObjectKind::SpecParam) &&
             leaf.kind !=
                 uint32_t(obelisk::reflection::VPIObjectKind::Constant) &&
             leaf.kind !=
                 uint32_t(obelisk::reflection::VPIObjectKind::EnumConst)))
          return false;
        continue;
      }
      const uint8_t *expression = expressionNode(database, recipe, current);
      if (!expression)
        return false;
      auto kind =
          static_cast<obelisk::reflection::VPIObjectKind>(read16(expression));
      if (kind == obelisk::reflection::VPIObjectKind::Constant)
        continue;
      if ((kind != obelisk::reflection::VPIObjectKind::Operation &&
           kind != obelisk::reflection::VPIObjectKind::PartSelect &&
           kind != obelisk::reflection::VPIObjectKind::IndexedPartSelect &&
           !obelisk::reflection::isVPIBitSelection(uint32_t(kind))) ||
          read32(expression + 4) == UINT32_MAX)
        return false;
      uint64_t first = uint64_t{read32(recipe + 12)} + read32(expression + 8);
      for (uint32_t child = 0; child != read32(expression + 12); ++child)
        pending.push_back(read32(database.data + database.expressionOperands +
                                 (first + child) * kExpressionOperandSize));
    }
    return true;
  }
  OBELISK_RT_CATCH_ALL { return false; }
}

bool expressionKnownInteger(const Database &database, const uint8_t *root,
                            const uint8_t *recipe, uint32_t operand,
                            int64_t &result) {
  using Kind = obelisk::reflection::VPIObjectKind;
  using Shape = obelisk::reflection::VPIIntegralExpressionShape;
  using obelisk::designbytecode::Logic;
  struct Constant {
    Logic value;
    Shape shape;
  };
  // Template structure is proved before root-specific width checks. Still
  // prove effective operand shapes before arithmetic: later nodes have not
  // yet undergone their root-specific type check. No storage or calls enter
  // this proof, and all temporary values are discarded after validation.
  std::unordered_map<uint32_t, Constant> values;
  std::vector<std::pair<uint32_t, bool>> pending{{operand, false}};
  while (!pending.empty()) {
    auto [current, ready] = pending.back();
    pending.pop_back();
    if (values.count(current))
      continue;
    VPIFrozenValue frozen{};
    uint32_t semantic = UINT32_MAX, code = 0;
    const uint8_t *node = nullptr;
    if (current & kExpressionOperandLeafFlag) {
      ExpressionLeafInfo leaf;
      uint32_t ordinal = current & ~kExpressionOperandLeafFlag;
      if (ordinal >= (recipe ? read32(recipe + 20) : 1) ||
          !expressionLeaf(database, root, ordinal, leaf) ||
          (leaf.kind != uint32_t(Kind::Parameter) &&
           leaf.kind != uint32_t(Kind::SpecParam) &&
           leaf.kind != uint32_t(Kind::EnumConst)))
        return false;
      semantic = leaf.semantic;
      uint32_t scope = 0, memberIndex = 0, specialization = 0, packed = 0;
      const uint8_t *member = nullptr;
      if (virtualMemberForCursor(database, leaf.cursor, scope, memberIndex,
                                 member, nullptr, &specialization)) {
        if (!findDefinitionParameterValue(database, specialization, memberIndex,
                                          frozen))
          return false;
      } else if (!packedObjectReferenceForCursor(database, leaf.cursor,
                                                 packed) ||
                 !findFrozenValue(database, packed, frozen))
        return false;
    } else {
      node = recipe ? expressionNode(database, recipe, current) : nullptr;
      if (!node)
        return false;
      semantic = read32(node + 4);
      if (read16(node) == uint16_t(Kind::Constant)) {
        if (!expressionFrozen(database, read32(node + 28), frozen))
          return false;
      } else if (read16(node) == uint16_t(Kind::Operation)) {
        code = read16(node + 2);
        auto descriptor = obelisk::reflection::findVPISourceOperator(code);
        if (!descriptor || read32(node + 12) != descriptor->arity ||
            obelisk::reflection::integralOperatorRule(code) ==
                obelisk::reflection::VPIIntegralOperatorRule::Unsupported)
          return false;
      } else
        return false;
    }
    uint64_t width = 0;
    uint32_t flags = 0;
    if (!expressionIntegral(database, semantic, width, flags) ||
        width > UINT32_MAX)
      return false;
    Shape shape{width, bool(flags & OBELISK_RT_DESIGN_SEMANTIC_SIGNED),
                bool(flags & OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE), code};
    if (!code) {
      uint64_t bytes = (width + 7) / 8;
      if (frozen.bitWidth != width || frozen.payloadSize != bytes * 2 ||
          bool(frozen.kindAndFlags & obelisk::reflection::frozenValueSigned) !=
              shape.isSigned ||
          bool(frozen.kindAndFlags &
               obelisk::reflection::frozenValueFourState) != shape.fourState)
        return false;
      Logic value{static_cast<uint32_t>(width), shape.fourState, {}, {}};
      value.value.assign(obelisk::designbytecode::limbCount(width), 0);
      value.unknown.assign(obelisk::designbytecode::limbCount(width), 0);
      for (uint64_t byte = 0; byte != bytes; ++byte) {
        value.value[byte / 8] |= uint64_t{frozen.payload[byte]}
                                 << (byte % 8 * 8);
        value.unknown[byte / 8] |= uint64_t{frozen.payload[bytes + byte]}
                                   << (byte % 8 * 8);
      }
      values.emplace(current, Constant{std::move(value), shape});
      continue;
    }
    auto operandAt = [&](uint32_t child) {
      return read32(database.data + database.expressionOperands +
                    (uint64_t{read32(recipe + 12)} + read32(node + 8) + child) *
                        kExpressionOperandSize);
    };
    if (!ready) {
      pending.push_back({current, true});
      for (uint32_t child = 0; child != read32(node + 12); ++child) {
        uint32_t next = operandAt(child);
        // The on-image parent-before-child order also bounds this worklist.
        if (!(next & kExpressionOperandLeafFlag) && next <= current)
          return false;
        pending.push_back({next, false});
      }
      continue;
    }
    std::array<Logic, 3> inputs;
    std::array<Shape, 3> shapes;
    uint32_t count = read32(node + 12);
    if (count > inputs.size())
      return false;
    for (uint32_t child = 0; child != count; ++child) {
      auto found = values.find(operandAt(child));
      if (found == values.end())
        return false;
      inputs[child] = found->second.value;
      shapes[child] = found->second.shape;
    }
    if (!obelisk::reflection::validVPIIntegralOperator(code, shape,
                                                       shapes.data(), count))
      return false;
    values.emplace(current, Constant{obelisk::vpiexpression::evaluate(
                                         code, shape, inputs, shapes),
                                     shape});
  }
  auto found = values.find(operand);
  if (found == values.end() ||
      obelisk::designbytecode::anyUnknown(found->second.value))
    return false;
  const auto &value = found->second.value;
  bool negative = found->second.shape.isSigned &&
                  obelisk::designbytecode::bit(value.value, value.width - 1);
  for (uint64_t bit = 64; bit < value.width; ++bit)
    if (obelisk::designbytecode::bit(value.value, bit) != negative)
      return false;
  uint64_t raw = value.value[0];
  if (value.width < 64 && negative)
    raw |= UINT64_MAX << value.width;
  if ((raw >> 63) != uint64_t(negative))
    return false;
  result = static_cast<int64_t>(raw);
  return true;
}

bool expressionOperandTarget(const Database &database, uint32_t rootIndex,
                             uint32_t operand, uint64_t &cursor, uint32_t &kind,
                             uint32_t defaultBinding = UINT32_MAX) {
  const uint8_t *root = expressionRoot(database, rootIndex);
  const uint8_t *recipe = root ? expressionTemplate(database, root) : nullptr;
  if (!root)
    return false;
  if (operand & kExpressionOperandLeafFlag) {
    ExpressionLeafInfo leaf;
    uint32_t ordinal = operand & ~kExpressionOperandLeafFlag;
    if (ordinal >= (recipe ? read32(recipe + 20) : 1) ||
        !expressionLeaf(database, root, ordinal, leaf, defaultBinding))
      return false;
    cursor = leaf.cursor;
    kind = leaf.kind;
    return true;
  }
  const uint8_t *node =
      recipe ? expressionNode(database, recipe, operand) : nullptr;
  if (!node)
    return false;
  if (isDefinitionDefault(root)) {
    uint32_t scope = 0;
    if (!expressionContext(database, root, scope, nullptr, nullptr,
                           defaultBinding))
      return false;
    cursor = virtualToken(kVirtualMemberTag, defaultBinding,
                          kExpressionPayloadDomainMask |
                              (read32(recipe + 4) + operand));
  } else
    cursor = virtualToken(kVirtualMemberTag, rootIndex,
                          kExpressionCursorFlag | operand);
  kind = read16(node);
  return true;
}

bool originalBuiltinTypeDefault(const Database &database, uint32_t index,
                                uint32_t &publicKind) {
  if (index >= database.semanticTypeCount)
    return false;
  const uint8_t *type = database.data + database.semanticTypes +
                        uint64_t{index} * kSemanticTypeSize;
  uint32_t encoded = read32(type);
  if (read32(type + 8) || read32(type + 12) != UINT32_MAX ||
      read32(type + 16) != UINT32_MAX || read32(type + 20) || read32(type + 24))
    return false;
  uint32_t kind = encoded & UINT32_C(0xff);
  if ((kind == OBELISK_RT_DESIGN_SEMANTIC_BIT ||
       kind == OBELISK_RT_DESIGN_SEMANTIC_LOGIC ||
       kind == OBELISK_RT_DESIGN_SEMANTIC_REG) &&
      (readI64(type + 32) != 0 || readI64(type + 40) != 0))
    return false;
  switch (kind) {
  case OBELISK_RT_DESIGN_SEMANTIC_BIT:
  case OBELISK_RT_DESIGN_SEMANTIC_LOGIC:
  case OBELISK_RT_DESIGN_SEMANTIC_REG:
  case OBELISK_RT_DESIGN_SEMANTIC_BYTE:
  case OBELISK_RT_DESIGN_SEMANTIC_SHORT_INT:
  case OBELISK_RT_DESIGN_SEMANTIC_INT:
  case OBELISK_RT_DESIGN_SEMANTIC_LONG_INT:
  case OBELISK_RT_DESIGN_SEMANTIC_INTEGER:
  case OBELISK_RT_DESIGN_SEMANTIC_TIME:
  case OBELISK_RT_DESIGN_SEMANTIC_SHORT_REAL:
  case OBELISK_RT_DESIGN_SEMANTIC_REAL:
  case OBELISK_RT_DESIGN_SEMANTIC_REALTIME:
  case OBELISK_RT_DESIGN_SEMANTIC_STRING:
  case OBELISK_RT_DESIGN_SEMANTIC_CHANDLE:
  case OBELISK_RT_DESIGN_SEMANTIC_VOID:
    publicKind = (encoded & OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_MASK) >>
                 OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_SHIFT;
    return true;
  default:
    return false;
  }
}

bool validateExpressions(const Database &database,
                         std::vector<uint8_t> &frozenReferenced) {
  using Kind = obelisk::reflection::VPIObjectKind;
  using Rel = obelisk::reflection::VPIRelationKind;
  using Mode = obelisk::reflection::VPITraversalMode;
  std::vector<uint8_t> templatesUsed(database.expressionTemplateCount, 0);
  std::vector<uint8_t> leavesUsed(database.expressionLeafCount, 0);
  std::vector<uint8_t> boundUsed(database.expressionBoundMemberCount, 0);
  std::vector<uint32_t> defaultTemplateOwner(database.expressionTemplateCount,
                                             UINT32_MAX);
  std::vector<uint8_t> defaultRoots(database.definitionMemberCount, 0);
  std::vector<uint32_t> memberDefinitions(database.definitionMemberCount,
                                          UINT32_MAX);
  for (uint32_t index = 0; index != database.definitionCount; ++index) {
    const uint8_t *definition = database.data + database.definitions +
                                uint64_t{index} * kDefinitionSize;
    uint32_t first = read32(definition + 16), count = read32(definition + 20);
    if (uint64_t{first} + count > database.definitionMemberCount)
      return false;
    for (uint32_t member = first; member != first + count; ++member)
      memberDefinitions[member] = index;
  }
  for (uint32_t index = 0; index != database.expressionRootBindingCount;
       ++index) {
    const uint8_t *root = expressionRoot(database, index);
    if (!isDefinitionDefault(root))
      continue;
    // Default relation tokens reserve zero for occurrence-mode root groups and
    // encode the instance binding as index + 1 below the two virtual tag bits.
    if (database.definitionBindingCount > kVirtualScopeMask)
      return false;
    uint32_t owner = read32(root), recipe = read32(root + 4);
    if (owner >= database.definitionMemberCount || defaultRoots[owner]++)
      return false;
    if (!isConcreteTypeDefault(root) && recipe != UINT32_MAX) {
      if (recipe >= database.expressionTemplateCount ||
          defaultTemplateOwner[recipe] != UINT32_MAX)
        return false;
      defaultTemplateOwner[recipe] = owner;
    }
  }
  for (uint32_t index = 0; index != database.definitionMemberCount; ++index) {
    const uint8_t *member = database.data + database.definitionMembers +
                            uint64_t{index} * kDefinitionMemberSize;
    if (bool(read16(member + 18) & kDefinitionParameterOriginalDefault) !=
        bool(defaultRoots[index]))
      return false;
  }
  uint64_t nextNode = 0, nextOperand = 0, nextShape = 0;
  std::unordered_set<std::string> templateKeys;
  for (uint32_t index = 0; index != database.expressionTemplateCount; ++index) {
    const uint8_t *recipe = database.data + database.expressionTemplates +
                            uint64_t{index} * kExpressionTemplateSize;
    uint32_t definition = read32(recipe), first = read32(recipe + 4),
             count = read32(recipe + 8);
    uint32_t operands = read32(recipe + 16), leaves = read32(recipe + 20);
    if ((definition != UINT32_MAX && definition >= database.definitionCount) ||
        !count || count >= kExpressionCursorFlag || first != nextNode ||
        read32(recipe + 12) != nextOperand ||
        read32(recipe + 24) != nextShape ||
        uint64_t{first} + count > database.expressionNodeCount ||
        nextOperand + operands > database.expressionOperandCount ||
        nextShape + leaves > database.expressionLeafShapeCount)
      return false;
    bool definitionDefault = defaultTemplateOwner[index] != UINT32_MAX;
    if (definitionDefault &&
        definition != memberDefinitions[defaultTemplateOwner[index]])
      return false;
    std::vector<uint8_t> owned(count, 0), leafUsed(leaves, 0);
    owned[0] = 1;
    uint64_t nextLocalOperand = 0;
    for (uint32_t ordinal = 0; ordinal != count; ++ordinal) {
      const uint8_t *node = expressionNode(database, recipe, ordinal);
      uint32_t start = read32(node + 8), size = read32(node + 12);
      uint64_t width = 0;
      uint32_t flags = 0;
      bool sourceOnly = read32(node + 4) == UINT32_MAX;
      if (start != nextLocalOperand || uint64_t{start} + size > operands ||
          (!sourceOnly &&
           !expressionIntegral(database, read32(node + 4), width, flags)))
        return false;
      nextLocalOperand += size;
      uint32_t file = read32(node + 16);
      std::string_view text;
      if ((file == UINT32_MAX && (read32(node + 20) || read32(node + 24))) ||
          (file != UINT32_MAX &&
           (!getString(database, database.strings + file, text) ||
            text.empty())))
        return false;
      Kind kind = static_cast<Kind>(read16(node));
      uint16_t code = read16(node + 2);
      if (definitionDefault && kind != Kind::Constant &&
          (kind != Kind::Operation || !sourceOnly ||
           (!obelisk::reflection::findVPISourceOperator(code) && code != 33 &&
            code != 34)))
        return false;
      if (sourceOnly) {
        if (kind != Kind::Operation || read32(node + 28) != UINT32_MAX ||
            !obelisk::reflection::validVPISourceOnlyArity(code, size))
          return false;
      } else if (kind == Kind::Constant) {
        VPIFrozenValue frozen{};
        uint32_t value = read32(node + 28);
        if (size || code != 7 || !expressionFrozen(database, value, frozen) ||
            frozen.bitWidth != width ||
            bool(flags & OBELISK_RT_DESIGN_SEMANTIC_SIGNED) !=
                bool(frozen.kindAndFlags &
                     obelisk::reflection::frozenValueSigned) ||
            bool(flags & OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE) !=
                bool(frozen.kindAndFlags &
                     obelisk::reflection::frozenValueFourState))
          return false;
        frozenReferenced[value] = 1;
      } else {
        if (read32(node + 28) != UINT32_MAX)
          return false;
        if (kind == Kind::Operation) {
          if (code == 33 || code == 34) {
            if (size < (code == 34 ? 2u : 1u) ||
                (flags & OBELISK_RT_DESIGN_SEMANTIC_SIGNED))
              return false;
          } else {
            auto descriptor = obelisk::reflection::findVPISourceOperator(code);
            if (!descriptor || size != descriptor->arity ||
                obelisk::reflection::integralOperatorRule(code) ==
                    obelisk::reflection::VPIIntegralOperatorRule::Unsupported)
              return false;
          }
        } else if (flags & OBELISK_RT_DESIGN_SEMANTIC_SIGNED)
          return false;
        else if (obelisk::reflection::isVPIBitSelection(uint32_t(kind))) {
          if (code || size != 2 || width != 1)
            return false;
        } else if (kind == Kind::PartSelect) {
          if (code || size != 3)
            return false;
        } else if (kind == Kind::IndexedPartSelect) {
          if ((code != 1 && code != 2) || size != 3)
            return false;
        } else
          return false;
      }
      for (uint32_t child = 0; child != size; ++child) {
        uint32_t operand =
            read32(database.data + database.expressionOperands +
                   (nextOperand + start + child) * kExpressionOperandSize);
        uint32_t childKind = 0;
        if (operand & kExpressionOperandLeafFlag) {
          uint32_t leafIndex = operand & ~kExpressionOperandLeafFlag;
          if (leafIndex >= leaves || leafUsed[leafIndex]++)
            return false;
          const uint8_t *shape =
              database.data + database.expressionLeafShapes +
              (nextShape + leafIndex) * kExpressionLeafShapeSize;
          uint64_t childWidth = 0;
          uint32_t childFlags = 0;
          bool unboundLeaf = read32(shape) == UINT32_MAX;
          if (read16(shape + 6) ||
              (unboundLeaf ? !definitionDefault || !sourceOnly ||
                                 read16(shape + 4) != uint16_t(Kind::Parameter)
                           : definitionDefault ||
                                 !expressionIntegral(database, read32(shape),
                                                     childWidth, childFlags)))
            return false;
          uint32_t name = read32(shape + 8);
          if (name != UINT32_MAX) {
            std::string_view identifier;
            if (!getString(database, database.strings + name, identifier) ||
                identifier.empty() ||
                std::any_of(identifier.begin(), identifier.end(),
                            [](unsigned char byte) {
                              return byte <= 32 || byte == 127;
                            }))
              return false;
          }
          childKind = read16(shape + 4);
        } else {
          if (operand <= ordinal || operand >= count || owned[operand]++)
            return false;
          const uint8_t *childNode = expressionNode(database, recipe, operand);
          if (!sourceOnly && read32(childNode + 4) == UINT32_MAX)
            return false;
          childKind = read16(childNode);
        }
        uint32_t selector =
            kind == Kind::Operation ? uint32_t(Rel::OperandRel)
            : child == 0            ? uint32_t(Rel::ParentRel)
            : obelisk::reflection::isVPIBitSelection(uint32_t(kind))
                ? uint32_t(Rel::IndexRel)
            : child == 1
                ? uint32_t(kind == Kind::PartSelect ? Rel::LeftRangeRel
                                                    : Rel::BaseExprRel)
                : uint32_t(kind == Kind::PartSelect ? Rel::RightRangeRel
                                                    : Rel::WidthExprRel);
        const auto *edge = obelisk::reflection::findVPITraversal(
            uint32_t(kind), selector,
            kind == Kind::Operation ? Mode::Iterate : Mode::Handle);
        if (!edge || !obelisk::reflection::vpiObjectSetContains(edge->targets,
                                                                childKind))
          return false;
      }
    }
    if (nextLocalOperand != operands ||
        std::find(owned.begin(), owned.end(), 0) != owned.end() ||
        std::find(leafUsed.begin(), leafUsed.end(), 0) != leafUsed.end())
      return false;
    std::string key(reinterpret_cast<const char *>(recipe), 4);
    key.append(reinterpret_cast<const char *>(recipe + 8), 4);
    key.append(reinterpret_cast<const char *>(recipe + 16), 8);
    key.append(reinterpret_cast<const char *>(database.data +
                                              database.expressionNodes +
                                              nextNode * kExpressionNodeSize),
               size_t(count) * kExpressionNodeSize);
    key.append(reinterpret_cast<const char *>(
                   database.data + database.expressionOperands +
                   nextOperand * kExpressionOperandSize),
               size_t(operands) * kExpressionOperandSize);
    key.append(reinterpret_cast<const char *>(
                   database.data + database.expressionLeafShapes +
                   nextShape * kExpressionLeafShapeSize),
               size_t(leaves) * kExpressionLeafShapeSize);
    if (!definitionDefault && !templateKeys.insert(std::move(key)).second)
      return false;
    nextNode += count;
    nextOperand += operands;
    nextShape += leaves;
  }
  if (nextNode != database.expressionNodeCount ||
      nextOperand != database.expressionOperandCount ||
      nextShape != database.expressionLeafShapeCount)
    return false;
  std::tuple<uint32_t, uint16_t, uint16_t, uint32_t> previous{};
  for (uint32_t index = 0; index != database.expressionRootBindingCount;
       ++index) {
    const uint8_t *root = expressionRoot(database, index),
                  *recipe = expressionTemplate(database, root);
    auto key = std::make_tuple(read32(root), read16(root + 12),
                               read16(root + 14), read32(root + 16));
    uint32_t scope = 0, ownerKind = 0;
    uint64_t owner = 0;
    bool definitionDefault = isDefinitionDefault(root);
    bool concreteType = isConcreteTypeDefault(root);
    uint16_t mode = read16(root + 14) &
                    ~(obelisk::reflection::expressionRootDefinitionMember |
                      obelisk::reflection::expressionRootConcreteTypeDefault);
    bool iterate = mode == uint16_t(Mode::Iterate);
    uint32_t ownerDefinition = UINT32_MAX;
    if (definitionDefault) {
      uint32_t memberIndex = read32(root);
      if (memberIndex >= database.definitionMemberCount)
        return false;
      const uint8_t *member = database.data + database.definitionMembers +
                              uint64_t{memberIndex} * kDefinitionMemberSize;
      ownerKind = read16(member + 16);
      ownerDefinition = memberDefinitions[memberIndex];
      if ((ownerKind != uint32_t(Kind::Parameter) &&
           ownerKind != uint32_t(Kind::TypeParameter)) ||
          !(read16(member + 18) & kDefinitionParameterOriginalDefault) ||
          (read16(member + 18) & kDefinitionParameterDefaultUnavailable) ||
          mode != uint16_t(Mode::Handle) ||
          read16(root + 12) != uint16_t(Rel::ExprRel))
        return false;
    }
    bool sameGroup = index && std::get<0>(key) == std::get<0>(previous) &&
                     std::get<1>(key) == std::get<1>(previous) &&
                     std::get<2>(key) == std::get<2>(previous);
    uint64_t expectedOrdinal =
        sameGroup ? uint64_t(std::get<3>(previous)) + 1 : 0;
    if ((index && key <= previous) || mode > uint16_t(Mode::Iterate) ||
        (!concreteType && read32(root + 16) != expectedOrdinal) ||
        (!iterate && sameGroup) ||
        (!definitionDefault &&
         !expressionContext(database, root, scope, &ownerKind, &owner)))
      return false;
    bool callArgument = (ownerKind == uint32_t(Kind::FuncCall) ||
                         ownerKind == uint32_t(Kind::TaskCall)) &&
                        read16(root + 12) == uint16_t(Rel::ArgumentRel);
    if (iterate && !callArgument &&
        (ownerKind != uint32_t(Kind::CaseItem) ||
         read16(root + 12) != uint16_t(Rel::ExprRel)))
      return false;
    previous = key;
    const auto *edge = obelisk::reflection::findVPITraversal(
        ownerKind, read16(root + 12), iterate ? Mode::Iterate : Mode::Handle);
    if (!edge || edge->statementContainment ||
        edge->automaticRelation !=
            obelisk::reflection::VPIAutomaticRelation::None)
      return false;
    if (concreteType) {
      uint32_t publicKind = 0;
      uint32_t file = read32(root + 8), line = read32(root + 16);
      std::string_view filename;
      if (!definitionDefault || ownerKind != uint32_t(Kind::TypeParameter) ||
          !originalBuiltinTypeDefault(database, read32(root + 4), publicKind) ||
          !obelisk::reflection::vpiObjectSetContains(edge->targets,
                                                     publicKind) ||
          (file == UINT32_MAX
               ? line != 0
               : !line || line > INT32_MAX ||
                     !getString(database, database.strings + file, filename) ||
                     filename.empty()))
        return false;
      continue;
    }
    if (ownerKind == uint32_t(Kind::TypeParameter) && recipe)
      return false;
    auto table = obelisk::reflection::unpackTableIndexKind(read32(root));
    if (!definitionDefault &&
        table != obelisk::reflection::TableKind::Statement &&
        ownerKind != uint32_t(Kind::Port) &&
        ownerKind != uint32_t(Kind::PrimTerm) && !callArgument)
      return false;
    uint32_t ownerIndex = obelisk::reflection::unpackTableIndex(read32(root));
    uint64_t physical = lowerBoundRelation(database, table, ownerIndex,
                                           read16(root + 12), iterate);
    if (!definitionDefault && physical < database.relationCount &&
        relationMatches(relationAt(database, physical), table, ownerIndex,
                        read16(root + 12), iterate))
      return false;
    uint32_t count = recipe ? read32(recipe + 20) : 1;
    uint32_t first = read32(root + 8);
    if (uint64_t{first} + count > database.expressionLeafCount)
      return false;
    if (recipe) {
      templatesUsed[read32(root + 4)] = 1;
      uint32_t definition = UINT32_MAX, specialization = 0;
      if (definitionDefault)
        definition = ownerDefinition;
      else
        findDefinitionBinding(database, scope, definition, specialization);
      if (!definitionDefault &&
          defaultTemplateOwner[read32(root + 4)] != UINT32_MAX)
        return false;
      if (read32(recipe) != definition)
        return false;
    } else if (read32(root + 4) != UINT32_MAX)
      return false;
    for (uint32_t leafIndex = 0; leafIndex != count; ++leafIndex) {
      ExpressionLeafInfo leaf;
      if (leavesUsed[first + leafIndex]++)
        return false;
      if (definitionDefault) {
        uint32_t target =
            read32(database.data + database.expressionLeaves +
                   uint64_t{first + leafIndex} * kExpressionLeafSize);
        if (target >= read32(root) ||
            target >= database.definitionMemberCount ||
            memberDefinitions[target] != ownerDefinition)
          return false;
        const uint8_t *member = database.data + database.definitionMembers +
                                uint64_t{target} * kDefinitionMemberSize;
        leaf.kind = read16(member + 16);
        leaf.semantic = UINT32_MAX;
        if (leaf.kind != ownerKind)
          return false;
        if (recipe) {
          const uint8_t *shape = database.data + database.expressionLeafShapes +
                                 (uint64_t{read32(recipe + 24)} + leafIndex) *
                                     kExpressionLeafShapeSize;
          if (read32(shape + 8) != read32(member))
            return false;
        }
      } else if (!expressionLeaf(database, root, leafIndex, leaf))
        return false;
      uint32_t encoded =
          read32(database.data + database.expressionLeaves +
                 uint64_t{first + leafIndex} * kExpressionLeafSize);
      if (encoded >> 30 == 2)
        boundUsed[encoded & ((UINT32_C(1) << 30) - 1)] = 1;
      if (recipe) {
        const uint8_t *shape = database.data + database.expressionLeafShapes +
                               (uint64_t{read32(recipe + 24)} + leafIndex) *
                                   kExpressionLeafShapeSize;
        if (leaf.semantic != read32(shape) || leaf.kind != read16(shape + 4))
          return false;
      } else if (!obelisk::reflection::vpiObjectSetContains(edge->targets,
                                                            leaf.kind))
        return false;
    }
    if (!recipe)
      continue;
    if (!obelisk::reflection::vpiObjectSetContains(
            edge->targets, read16(expressionNode(database, recipe, 0))))
      return false;
    for (uint32_t ordinal = 0; ordinal != read32(recipe + 8); ++ordinal) {
      const uint8_t *node = expressionNode(database, recipe, ordinal);
      Kind kind = static_cast<Kind>(read16(node));
      if (kind == Kind::Constant || read32(node + 4) == UINT32_MAX)
        continue;
      uint64_t width = 0;
      uint32_t flags = 0;
      if (!expressionIntegral(database, read32(node + 4), width, flags))
        return false;
      auto operandAt = [&](uint32_t child) {
        return read32(
            database.data + database.expressionOperands +
            (uint64_t{read32(recipe + 12)} + read32(node + 8) + child) *
                kExpressionOperandSize);
      };
      auto typeAt = [&](uint32_t child, uint64_t &bits, uint32_t &typeFlags,
                        uint32_t *outSemantic = nullptr) {
        uint32_t operand = operandAt(child), semantic = 0;
        if (operand & kExpressionOperandLeafFlag) {
          const uint8_t *shape = database.data + database.expressionLeafShapes +
                                 (uint64_t{read32(recipe + 24)} +
                                  (operand & ~kExpressionOperandLeafFlag)) *
                                     kExpressionLeafShapeSize;
          semantic = read32(shape);
        } else
          semantic = read32(expressionNode(database, recipe, operand) + 4);
        if (outSemantic)
          *outSemantic = semantic;
        return expressionIntegral(database, semantic, bits, typeFlags);
      };
      bool fourState = false;
      if (kind == Kind::Operation) {
        uint32_t code = read16(node + 2);
        if (code != 33 && code != 34) {
          std::array<obelisk::reflection::VPIIntegralExpressionShape, 3>
              shapes{};
          uint32_t count = read32(node + 12);
          if (count > shapes.size())
            return false;
          for (uint32_t child = 0; child != count; ++child) {
            uint64_t bits = 0;
            uint32_t typeFlags = 0;
            if (!typeAt(child, bits, typeFlags))
              return false;
            uint32_t operand = operandAt(child), childCode = 0;
            if (!(operand & kExpressionOperandLeafFlag)) {
              const uint8_t *childNode =
                  expressionNode(database, recipe, operand);
              if (read16(childNode) == uint32_t(Kind::Operation))
                childCode = read16(childNode + 2);
            }
            shapes[child] = {
                bits, bool(typeFlags & OBELISK_RT_DESIGN_SEMANTIC_SIGNED),
                bool(typeFlags & OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE),
                childCode};
          }
          if (!obelisk::reflection::validVPIIntegralOperator(
                  code,
                  {width, bool(flags & OBELISK_RT_DESIGN_SEMANTIC_SIGNED),
                   bool(flags & OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE)},
                  shapes.data(), count))
            return false;
          continue;
        }
        bool repeated = read16(node + 2) == 34;
        int64_t multiplier = 1;
        if (repeated && (!expressionKnownInteger(database, root, recipe,
                                                 operandAt(0), multiplier) ||
                         multiplier <= 0))
          return false;
        uint64_t sum = 0;
        for (uint32_t child = repeated ? 1 : 0; child != read32(node + 12);
             ++child) {
          uint64_t bits = 0;
          uint32_t typeFlags = 0;
          if (!typeAt(child, bits, typeFlags) || bits > UINT64_MAX - sum)
            return false;
          sum += bits;
          fourState |= (typeFlags & OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE) != 0;
        }
        if (sum > UINT64_MAX / uint64_t(multiplier) ||
            sum * uint64_t(multiplier) != width)
          return false;
      } else {
        uint64_t bits = 0;
        uint32_t typeFlags = 0, semantic = 0;
        if (!typeAt(0, bits, typeFlags, &semantic))
          return false;
        if ((typeFlags & UINT32_C(0xff)) ==
            OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY) {
          const uint8_t *parent = database.data + database.semanticTypes +
                                  uint64_t{semantic} * kSemanticTypeSize;
          const uint8_t *edge =
              database.data + database.semanticTypeEdges +
              uint64_t{read32(parent + 4)} * kSemanticTypeEdgeSize;
          uint64_t stride = 0;
          if (semanticPackedBitWidth(database, read32(edge), &stride) !=
                  OBELISK_RT_OK ||
              stride != 1)
            return false;
        }
        fourState = (typeFlags & OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE) != 0;
        if (obelisk::reflection::isVPIBitSelection(uint32_t(kind))) {
          uint32_t parent = operandAt(0), parentKind = 0;
          if (parent & kExpressionOperandLeafFlag) {
            const uint8_t *shape = database.data +
                                   database.expressionLeafShapes +
                                   (uint64_t{read32(recipe + 24)} +
                                    (parent & ~kExpressionOperandLeafFlag)) *
                                       kExpressionLeafShapeSize;
            parentKind = read16(shape + 4);
          } else
            parentKind = read16(expressionNode(database, recipe, parent));
          if (width != 1 ||
              obelisk::reflection::vpiScalarBitSelectionKind(parentKind) !=
                  uint32_t(kind) ||
              fourState != bool(flags & OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE))
            return false;
          continue;
        }
        int64_t last = 0;
        if (!expressionKnownInteger(database, root, recipe, operandAt(2), last))
          return false;
        if (kind == Kind::IndexedPartSelect) {
          if (last <= 0 || uint64_t(last) != width)
            return false;
        } else {
          int64_t firstBound = 0;
          if (!expressionKnownInteger(database, root, recipe, operandAt(1),
                                      firstBound))
            return false;
          uint64_t distance = firstBound >= last
                                  ? uint64_t(firstBound) - uint64_t(last)
                                  : uint64_t(last) - uint64_t(firstBound);
          if (distance == UINT64_MAX || distance + 1 != width)
            return false;
          const uint8_t *type = database.data + database.semanticTypes +
                                uint64_t{semantic} * kSemanticTypeSize;
          int64_t left = static_cast<int64_t>(read64(type + 32)),
                  right = static_cast<int64_t>(read64(type + 40));
          // Canonical scalar integral types have an implicit descending
          // [width-1:0] range when the source semantic has no explicit range.
          bool descending = left >= right;
          if (firstBound != last && (left != right || bits > 1) &&
              (firstBound > last) != descending)
            return false;
        }
      }
      if (fourState != bool(flags & OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE))
        return false;
    }
  }
  if (std::find(templatesUsed.begin(), templatesUsed.end(), 0) !=
          templatesUsed.end() ||
      std::find(leavesUsed.begin(), leavesUsed.end(), 0) != leavesUsed.end() ||
      std::find(boundUsed.begin(), boundUsed.end(), 0) != boundUsed.end())
    return false;
  std::unordered_set<uint64_t> pairs;
  for (uint32_t index = 0; index != database.expressionBoundMemberCount;
       ++index) {
    const uint8_t *pair = database.data + database.expressionBoundMembers +
                          uint64_t{index} * kExpressionBoundMemberSize;
    if (!pairs.insert(uint64_t{read32(pair)} << 32 | read32(pair + 4)).second)
      return false;
  }
  return true;
}

bool validateDatabaseImpl(const Database &database) {
  if (database.scopeCount == 0 || !isScopeOffset(database, database.root))
    return false;
  std::array<std::unordered_set<uint64_t>,
             OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT + 1>
      stableIDs;
  constexpr uint32_t parameterSourceFlags =
      OBELISK_RT_DESIGN_CAP_UNRESOLVED_TYPE_PARAMETER |
      OBELISK_RT_DESIGN_CAP_UNRESOLVED_VALUE_PARAMETER |
      OBELISK_RT_DESIGN_CAP_UNAVAILABLE_PARAMETER_DEFAULT;
  auto validParameterSourceFlags = [](uint32_t kind, uint32_t caps) {
    using Kind = obelisk::reflection::VPIObjectKind;
    if ((caps & OBELISK_RT_DESIGN_CAP_UNRESOLVED_TYPE_PARAMETER) != 0 &&
        (kind != static_cast<uint32_t>(Kind::TypeParameter) ||
         (caps & OBELISK_RT_DESIGN_CAP_NAMED_TYPESPEC) != 0))
      return false;
    if ((caps & OBELISK_RT_DESIGN_CAP_UNRESOLVED_VALUE_PARAMETER) != 0 &&
        (kind != static_cast<uint32_t>(Kind::Parameter) ||
         (caps & OBELISK_RT_DESIGN_CAP_PARAMETER_EXPLICIT_RANGE) != 0))
      return false;
    return (caps & OBELISK_RT_DESIGN_CAP_UNAVAILABLE_PARAMETER_DEFAULT) == 0 ||
           kind == static_cast<uint32_t>(Kind::Parameter) ||
           kind == static_cast<uint32_t>(Kind::TypeParameter);
  };
  auto isSourceExpressionKind = [](uint32_t kind) {
    using Kind = obelisk::reflection::VPIObjectKind;
    return kind == static_cast<uint32_t>(Kind::Constant) ||
           kind == static_cast<uint32_t>(Kind::Operation);
  };
  std::unordered_set<uint32_t> sourceClassProperties;
  std::unordered_set<uint32_t> frameLocals, automaticFrameLocals;
  std::unordered_map<uint32_t, uint32_t> frameLocalOwners;
  std::unordered_set<uint32_t> frameLocalForward, frameLocalReverse;
  std::unordered_map<uint32_t, std::string_view> fixedNames;
  std::unordered_map<uint32_t, uint32_t> sourceClassPropertyOwners;
  std::unordered_map<uint32_t, uint32_t> sourceClassPropertyScopes;
  std::unordered_set<uint32_t> unresolvedTypeParameters;
  std::unordered_set<uint32_t> unresolvedTypeAliases, namedUnresolvedAliases;
  std::unordered_map<uint32_t, uint32_t> unresolvedAliasOwners,
      unresolvedAliasScopes, unresolvedAliasTargets;
  std::unordered_set<uint32_t> unresolvedValueParameters;
  std::unordered_set<uint32_t> primitiveTerminals;
  std::unordered_map<uint32_t, uint32_t> terminalDirections, primitiveTypes;
  std::unordered_map<uint32_t, uint32_t> terminalOwners, terminalPrimitives;
  std::unordered_set<uint32_t> foreachNulls, foreachNullProperties;
  std::unordered_set<uint32_t> sourceExpressionObjects;
  std::unordered_map<uint32_t, uint16_t> sourceExpressionOperators;
  std::unordered_set<uint32_t> sourceExpressionConstants;
  std::unordered_set<uint32_t> namedParameters;
  std::unordered_set<uint32_t> unavailableParameterDefaults;
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
         kind == OBELISK_RT_DESIGN_RECORD_STORAGE ||
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
    bool unnamedTypespec =
        kind == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT &&
        (caps & OBELISK_RT_DESIGN_CAP_NAMED_TYPESPEC) == 0 &&
        obelisk::reflection::vpiObjectSetContains(
            obelisk::reflection::VPIObjectSetID::ConcreteTypespecObjects,
            intrinsicKind);
    bool sourceExpression = kind == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT &&
                            isSourceExpressionKind(intrinsicKind);
    bool unnamedInheritance =
        kind == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT &&
        (intrinsicKind == static_cast<uint32_t>(VPIKind::Extends) ||
         (intrinsicKind == static_cast<uint32_t>(VPIKind::ParamAssign) &&
          (caps &
           (OBELISK_RT_DESIGN_CAP_DEFAULT_CLASS_PARAMETER_ASSIGNMENT |
            OBELISK_RT_DESIGN_CAP_EXPLICIT_CLASS_PARAMETER_ASSIGNMENT))));
    bool primitiveTerminal =
        kind == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT &&
        intrinsicKind == static_cast<uint32_t>(VPIKind::PrimTerm);
    if (!getString(database, nameOffset, name) ||
        (sourceExpression || primitiveTerminal
             ? !name.empty()
             : name.empty() && !unnamedTypespec && !unnamedInheritance))
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
            (caps & (OBELISK_RT_DESIGN_CAP_SOURCE_CLASS_PROPERTY |
                     OBELISK_RT_DESIGN_CAP_FRAME_LOCAL)) == 0 &&
            indexed &&
            indexed->accessKind ==
                obelisk::reflection::VPIIndexedAccessKind::RelationElement;
        using VPIKind = obelisk::reflection::VPIObjectKind;
        bool scalarPrimitive =
            intrinsicKind == static_cast<uint32_t>(VPIKind::Gate) ||
            intrinsicKind == static_cast<uint32_t>(VPIKind::Switch) ||
            intrinsicKind == static_cast<uint32_t>(VPIKind::Udp);
        if ((caps & ~(OBELISK_RT_DESIGN_CAP_NAMED_TYPESPEC |
                      OBELISK_RT_DESIGN_CAP_SOURCE_CLASS_PROPERTY |
                      OBELISK_RT_DESIGN_CAP_UNRESOLVED_TYPE_ALIAS |
                      OBELISK_RT_DESIGN_CAP_FRAME_LOCAL |
                      lexicalInheritanceFlags | parameterSourceFlags)) != 0 ||
            !validParameterSourceFlags(intrinsicKind, caps) ||
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
            (refPort && portCaps != (OBELISK_RT_DESIGN_CAP_PORT_INPUT |
                                     OBELISK_RT_DESIGN_CAP_PORT_OUTPUT)) ||
            caps != (OBELISK_RT_DESIGN_CAP_READ | portCaps | ordinalCaps |
                     (caps & OBELISK_RT_DESIGN_CAP_PORT_REF) |
                     (caps & OBELISK_RT_DESIGN_CAP_PORT_WHOLE_SOURCE)))
          return false;
      } else if (portCaps != 0 && kind != OBELISK_RT_DESIGN_RECORD_STORAGE &&
                 kind != OBELISK_RT_DESIGN_RECORD_NET) {
        return false;
      } else if (portCaps == 0 &&
                 (ordinalCaps &
                  ~(kind == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT
                        ? parameterSourceFlags |
                              OBELISK_RT_DESIGN_CAP_SOURCE_CLASS_PROPERTY |
                              OBELISK_RT_DESIGN_CAP_UNRESOLVED_TYPE_ALIAS |
                              OBELISK_RT_DESIGN_CAP_FRAME_LOCAL |
                              lexicalInheritanceFlags
                        : 0u)) != 0) {
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
  std::unordered_set<uint32_t> voidFunctions;
  std::unordered_map<uint32_t, uint32_t> functionReturns;
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
      if (recordKind(database.data + database.objects +
                     uint64_t{sourceIndex} * kObjectSize) ==
              OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT &&
          (read32(database.data + database.objects +
                  uint64_t{sourceIndex} * kObjectSize + 4) &
           OBELISK_RT_DESIGN_CAP_UNRESOLVED_TYPE_ALIAS) != 0) {
        // This typespec inherits the parameter's public kind, but is not a
        // parameter declaration. Only its exact source name is a fixed fact.
        if (selector != 2)
          return false;
        std::string_view name;
        if (!getString(database, payload, name) || name.empty())
          return false;
        namedUnresolvedAliases.insert(packedSource);
      }
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
             obelisk::reflection::VPIPropertyRealization::DefinitionImage &&
         descriptor->realization !=
             obelisk::reflection::VPIPropertyRealization::OptionalImage) ||
        kindAndFlags != static_cast<uint16_t>(descriptor->valueKind))
      return false;
    if (table == obelisk::reflection::TableKind::Object && selector == 50 &&
        (read32(database.data + database.objects +
                uint64_t{sourceIndex} * kObjectSize + 4) &
         OBELISK_RT_DESIGN_CAP_FRAME_LOCAL)) {
      if (payload != 1)
        return false;
      automaticFrameLocals.insert(packedSource);
    }
    using ValueKind = obelisk::reflection::VPIPropertyValueKind;
    switch (descriptor->valueKind) {
    case ValueKind::Boolean:
      // Optional source facts retain explicit false; ordinary FixedImage
      // Booleans keep their canonical false-as-absence encoding.
      if (payload > 1 ||
          (payload == 0 &&
           descriptor->realization !=
               obelisk::reflection::VPIPropertyRealization::OptionalImage))
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
      if (selector == 2 /* vpiName */)
        fixedNames.emplace(packedSource, value);
      break;
    }
    }
    if (exactKind == uint32_t(obelisk::reflection::VPIObjectKind::Function) &&
        selector == 4) {
      if (payload != 0)
        return false;
      voidFunctions.insert(packedSource);
    }
    if (selector == 33)
      primitiveTypes.emplace(packedSource, static_cast<uint32_t>(payload));
    if (exactKind == static_cast<uint32_t>(
                         obelisk::reflection::VPIObjectKind::PrimTerm) &&
        selector == 20) {
      if (payload < 1 || payload > 3)
        return false;
      terminalDirections.emplace(packedSource, static_cast<uint32_t>(payload));
    }
    if (table == obelisk::reflection::TableKind::StaticObject &&
        exactKind == static_cast<uint32_t>(
                         obelisk::reflection::VPIObjectKind::Operation) &&
        selector == 39) {
      if (payload != 36)
        return false;
      foreachNullProperties.insert(packedSource);
    }
    if (table == obelisk::reflection::TableKind::Object &&
        recordKind(database.data + database.objects +
                   uint64_t{sourceIndex} * kObjectSize) ==
            OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT) {
      using Kind = obelisk::reflection::VPIObjectKind;
      if (exactKind == static_cast<uint32_t>(Kind::Operation) &&
          selector == 39) {
        if (!obelisk::reflection::findVPISourceOperator(
                static_cast<uint32_t>(payload)))
          return false;
        sourceExpressionOperators.emplace(packedSource,
                                          static_cast<uint16_t>(payload));
      }
      if (exactKind == static_cast<uint32_t>(Kind::Constant) &&
          selector == 40) {
        if (payload != 7)
          return false;
        sourceExpressionConstants.insert(packedSource);
      }
    }
    if (exactKind == static_cast<uint32_t>(
                         obelisk::reflection::VPIObjectKind::Parameter) &&
        selector == 2) {
      std::string_view name;
      if (!getString(database, payload, name) || name.empty())
        return false;
      namedParameters.insert(packedSource);
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
            extent == 0 || width % extent != 0 ||
            elementWidth > width / extent ||
            ((flags & OBELISK_RT_DESIGN_TYPE_PACKED) != 0 &&
             elementWidth != width / extent) ||
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
    const bool disjointTaggedUnion =
        kind == OBELISK_RT_DESIGN_TYPE_UNION &&
        (flags & OBELISK_RT_DESIGN_TYPE_PACKED) == 0 &&
        (flags & OBELISK_RT_DESIGN_TYPE_TAGGED) != 0;
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
      if (kind == OBELISK_RT_DESIGN_TYPE_STRUCT || disjointTaggedUnion ||
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
      } else if (packedOffset != 0) {
        return false;
      }
    }
    if (((flags & OBELISK_RT_DESIGN_TYPE_FOUR_STATE) != 0) != fourState)
      return false;
    if (kind == OBELISK_RT_DESIGN_TYPE_STRUCT || disjointTaggedUnion) {
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
      if (maximum > UINT64_MAX - tagBits || maximum + tagBits > width ||
          ((flags & OBELISK_RT_DESIGN_TYPE_PACKED) != 0 &&
           maximum + tagBits != width))
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
  for (uint64_t index = 0; index != database.mailboxElementBindingCount;
       ++index) {
    obelisk::reflection::MailboxElementBindingView binding(
        database.data + database.mailboxElementBindings +
        index * kMailboxElementBindingSize);
    if (binding.getPhysicalTypeIndex() >= database.typeCount ||
        !visitType(database.types +
                   uint64_t{binding.getPhysicalTypeIndex()} * kTypeSize))
      return false;
  }
  for (uint64_t index = 0; index != database.boxTypeBindingCount; ++index) {
    obelisk::reflection::BoxTypeBindingView binding(
        database.data + database.boxTypeBindings + index * kBoxTypeBindingSize);
    if (binding.getPhysicalTypeIndex() >= database.typeCount ||
        !visitType(database.types +
                   uint64_t{binding.getPhysicalTypeIndex()} * kTypeSize))
      return false;
  }
  for (uint64_t index = 0; index != database.valueElementBindingCount;
       ++index) {
    obelisk::reflection::ValueElementBindingView binding(
        database.data + database.valueElementBindings +
        index * kValueElementBindingSize);
    if (binding.getPhysicalTypeIndex() >= database.typeCount ||
        !visitType(database.types +
                   uint64_t{binding.getPhysicalTypeIndex()} * kTypeSize))
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
    uint32_t caps = recordKind(object) == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT
                        ? read32(object + 4)
                        : 0;
    bool unresolved =
        (caps & OBELISK_RT_DESIGN_CAP_UNRESOLVED_TYPE_PARAMETER) != 0;
    bool unresolvedAlias =
        (caps & OBELISK_RT_DESIGN_CAP_UNRESOLVED_TYPE_ALIAS) != 0;
    bool lexicalClass = caps & OBELISK_RT_DESIGN_CAP_LEXICAL_CLASS_TYPESPEC;
    if (typespec && ((unresolved || unresolvedAlias || lexicalClass)
                         ? objectSemanticRoots[index]
                         : !objectSemanticRoots[index]))
      return false;
    uint32_t packed =
        (static_cast<uint32_t>(obelisk::reflection::TableKind::Object) << 30) |
        index;
    if (unresolvedAlias) {
      if (recordVPIKind(object) !=
              static_cast<uint32_t>(
                  obelisk::reflection::VPIObjectKind::TypeParameter) ||
          caps != (OBELISK_RT_DESIGN_CAP_UNRESOLVED_TYPE_ALIAS |
                   OBELISK_RT_DESIGN_CAP_NAMED_TYPESPEC) ||
          read64(object + 48) != 0 || objectSemanticRoots[index] ||
          !namedUnresolvedAliases.count(packed))
        return false;
      unresolvedTypeAliases.insert(packed);
    }
    if ((caps & OBELISK_RT_DESIGN_CAP_SOURCE_CLASS_PROPERTY) != 0) {
      using Kind = obelisk::reflection::VPIObjectKind;
      if (caps != OBELISK_RT_DESIGN_CAP_SOURCE_CLASS_PROPERTY ||
          !objectSemanticRoots[index] || read64(object + 48) != 0 ||
          (!obelisk::reflection::vpiObjectSetContains(
               obelisk::reflection::VPIObjectSetID::DataVariableObjects,
               recordVPIKind(object)) &&
           recordVPIKind(object) != static_cast<uint32_t>(Kind::NamedEvent) &&
           recordVPIKind(object) !=
               static_cast<uint32_t>(Kind::NamedEventArray)))
        return false;
      sourceClassProperties.insert(packed);
    }
    if ((caps & OBELISK_RT_DESIGN_CAP_FRAME_LOCAL) != 0) {
      const auto *automatics = obelisk::reflection::findVPITraversal(
          static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::Frame),
          static_cast<uint32_t>(
              obelisk::reflection::VPIRelationKind::AutomaticsRel),
          obelisk::reflection::VPITraversalMode::Iterate);
      if (caps != OBELISK_RT_DESIGN_CAP_FRAME_LOCAL ||
          !objectSemanticRoots[index] ||
          !isTypeOffset(database, read64(object + 48)) || !automatics ||
          !obelisk::reflection::vpiObjectSetContains(automatics->targets,
                                                     recordVPIKind(object)))
        return false;
      if (obelisk::reflection::findVPIProperty(recordVPIKind(object), 50) &&
          !automaticFrameLocals.count(packed))
        return false;
      frameLocals.insert(packed);
    }
    if (unresolved)
      unresolvedTypeParameters.insert(packed);
    if ((caps & OBELISK_RT_DESIGN_CAP_UNRESOLVED_VALUE_PARAMETER) != 0) {
      if (objectSemanticRoots[index])
        return false;
      unresolvedValueParameters.insert(packed);
    }
    if (recordVPIKind(object) ==
        static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::PrimTerm)) {
      if (recordKind(object) != OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT ||
          caps || objectSemanticRoots[index] || read64(object + 48) ||
          read64(object + 56) || read64(object + 64) || read64(object + 72) ||
          read64(object + 80) || !terminalDirections.count(packed))
        return false;
      primitiveTerminals.insert(packed);
    }
    if (recordKind(object) == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT &&
        isSourceExpressionKind(recordVPIKind(object))) {
      bool constant =
          recordVPIKind(object) ==
          static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::Constant);
      if (constant != objectSemanticRoots[index] || read32(object + 4) != 0 ||
          (constant && sourceExpressionConstants.count(packed) == 0) ||
          (recordVPIKind(object) ==
               static_cast<uint32_t>(
                   obelisk::reflection::VPIObjectKind::Operation) &&
           sourceExpressionOperators.count(packed) == 0))
        return false;
      sourceExpressionObjects.insert(packed);
    }
    if ((caps & OBELISK_RT_DESIGN_CAP_UNAVAILABLE_PARAMETER_DEFAULT) != 0)
      unavailableParameterDefaults.insert(packed);
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
    uint32_t caps = read16(object + 30);
    bool unresolved =
        (caps & OBELISK_RT_DESIGN_CAP_UNRESOLVED_TYPE_PARAMETER) != 0;
    if ((caps &
         (OBELISK_RT_DESIGN_CAP_SOURCE_CLASS_PROPERTY |
          OBELISK_RT_DESIGN_CAP_UNRESOLVED_TYPE_ALIAS |
          OBELISK_RT_DESIGN_CAP_FRAME_LOCAL | lexicalInheritanceFlags)) != 0 ||
        !validParameterSourceFlags(read16(object + 28), caps) ||
        (typespec && (unresolved ? staticObjectSemanticRoots[index]
                                 : !staticObjectSemanticRoots[index])))
      return false;
    uint32_t packed =
        (static_cast<uint32_t>(obelisk::reflection::TableKind::StaticObject)
         << 30) |
        index;
    if (unresolved)
      unresolvedTypeParameters.insert(packed);
    if ((caps & OBELISK_RT_DESIGN_CAP_UNRESOLVED_VALUE_PARAMETER) != 0) {
      if (staticObjectSemanticRoots[index])
        return false;
      unresolvedValueParameters.insert(packed);
    }
    if (read16(object + 28) ==
        static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::PrimTerm)) {
      if (caps || staticObjectSemanticRoots[index] || read32(object + 16) ||
          !terminalDirections.count(packed))
        return false;
      primitiveTerminals.insert(packed);
    }
    // Compact operation records are only skipped foreach dimensions. Their
    // unique source owner and absence of outgoing operands are proved below.
    bool foreachNull =
        read16(object + 28) ==
        static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::Operation);
    if (foreachNull) {
      if (caps || staticObjectSemanticRoots[index] || read32(object + 16) ||
          !foreachNullProperties.count(packed))
        return false;
      foreachNulls.insert(packed);
    }
    if ((!foreachNull && isSourceExpressionKind(read16(object + 28))) ||
        obelisk::reflection::vpiObjectSetContains(
            obelisk::reflection::VPIObjectSetID::TaskFunctionCallObjects,
            read16(object + 28)))
      return false;
    if ((caps & OBELISK_RT_DESIGN_CAP_UNAVAILABLE_PARAMETER_DEFAULT) != 0)
      unavailableParameterDefaults.insert(packed);
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
    if (sourceClassProperties.count(read32(binding)) != 0 ||
        frameLocals.count(read32(binding)) != 0) {
      // Reuse the generated typespec-to-variable mapping. Events are separate
      // declaration kinds, including an unpacked event-array declaration.
      const uint8_t *semantic = database.data + database.semanticTypes +
                                uint64_t{root} * kSemanticTypeSize;
      uint32_t rootKind = (read32(semantic) &
                           OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_MASK) >>
                          OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_SHIFT;
      uint32_t expected = 0;
      if (const auto *mapping = obelisk::reflection::findVPIIndexedTypeResult(
              obelisk::reflection::VPIIndexedAccessKind::VariableElement,
              rootKind))
        expected = mapping->resultType;
      bool eventArray = false;
      while ((read32(semantic) & UINT32_C(0xff)) ==
             OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY) {
        eventArray = true;
        const uint8_t *edge =
            database.data + database.semanticTypeEdges +
            uint64_t{read32(semantic + 4)} * kSemanticTypeEdgeSize;
        semantic = database.data + database.semanticTypes +
                   uint64_t{read32(edge)} * kSemanticTypeSize;
      }
      if ((read32(semantic) & UINT32_C(0xff)) ==
          OBELISK_RT_DESIGN_SEMANTIC_EVENT)
        expected = static_cast<uint32_t>(
            eventArray ? obelisk::reflection::VPIObjectKind::NamedEventArray
                       : obelisk::reflection::VPIObjectKind::NamedEvent);
      if (!expected ||
          expected != objectReferenceVPIKind(database, objectOffset))
        return false;
    }
    // A type parameter is itself a typespec while unresolved (37.25), but
    // its elaborated semantic root describes its effective type (37.28).
    // Only concrete typespec declarations share their root's exact kind.
    if (!obelisk::reflection::vpiObjectSetContains(
            obelisk::reflection::VPIObjectSetID::ConcreteTypespecObjects,
            objectReferenceVPIKind(database, objectOffset)))
      continue;
    uint32_t rootKind = (read32(database.data + database.semanticTypes +
                                uint64_t{root} * kSemanticTypeSize) &
                         OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_MASK) >>
                        OBELISK_RT_DESIGN_SEMANTIC_PUBLIC_VPI_KIND_SHIFT;
    if (rootKind != objectReferenceVPIKind(database, objectOffset))
      return false;
  }

  // Runtime classes and builtins have canonical typespec identities. Only
  // user classes need a runtime class-ID binding; builtins bind their exact
  // semantic specialization directly. Aliases retain their lexical spelling.
  std::unordered_map<uint32_t, uint32_t> canonicalClassRoots;
  std::unordered_map<uint64_t, uint32_t> canonicalClassTypes;
  uint64_t previousClassID = 0;
  for (uint64_t index = 0; index != database.classTypeBindingCount; ++index) {
    obelisk::reflection::ClassTypeBindingView binding(
        database.data + database.classTypeBindings +
        index * kClassTypeBindingSize);
    uint64_t classID = binding.getClassID();
    uint32_t packed = binding.getTypespecIndexAndTable();
    uint64_t targetOffset = 0;
    uint32_t root = UINT32_MAX;
    if (classID == 0 || (index && classID <= previousClassID) ||
        !packedObjectReferenceOffset(database, packed, targetOffset) ||
        objectReferenceVPIKind(database, targetOffset) !=
            static_cast<uint32_t>(
                obelisk::reflection::VPIObjectKind::ClassTypespec) ||
        !findSemanticRootBinding(database, packed, root) ||
        root >= database.semanticTypeCount ||
        !canonicalClassRoots.emplace(packed, root).second)
      return false;
    const uint8_t *type = database.data + database.semanticTypes +
                          uint64_t{root} * kSemanticTypeSize;
    if ((read32(type) & UINT32_C(0xff)) != OBELISK_RT_DESIGN_SEMANTIC_CLASS ||
        read32(type + 12) != UINT32_MAX)
      return false;
    previousClassID = classID;
    canonicalClassTypes.emplace(classID, packed);
  }
  std::unordered_set<uint32_t> canonicalClassRootBindings;
  std::unordered_map<uint32_t, uint32_t> semanticClassTargets;
  auto semanticRecord = [&](uint32_t index) {
    return database.data + database.semanticTypes +
           uint64_t{index} * kSemanticTypeSize;
  };
  auto isBuiltinClassKind = [](uint32_t kind) {
    return kind == OBELISK_RT_DESIGN_SEMANTIC_MAILBOX ||
           kind == OBELISK_RT_DESIGN_SEMANTIC_SEMAPHORE ||
           kind == OBELISK_RT_DESIGN_SEMANTIC_PROCESS;
  };
  uint32_t previousClassSemantic = 0;
  for (uint64_t index = 0; index != database.semanticClassBindingCount;
       ++index) {
    obelisk::reflection::SemanticClassBindingView binding(
        database.data + database.semanticClassBindings +
        index * kSemanticClassBindingSize);
    uint32_t semantic = binding.getSemanticType();
    uint32_t packedTarget = binding.getTypespecIndexAndTable();
    if (semantic >= database.semanticTypeCount ||
        (index && semantic <= previousClassSemantic))
      return false;
    auto target = canonicalClassRoots.find(packedTarget);
    if (target == canonicalClassRoots.end()) {
      uint64_t targetOffset = 0;
      uint32_t root = UINT32_MAX;
      if (!packedObjectReferenceOffset(database, packedTarget, targetOffset) ||
          objectReferenceVPIKind(database, targetOffset) !=
              static_cast<uint32_t>(
                  obelisk::reflection::VPIObjectKind::ClassTypespec) ||
          !findSemanticRootBinding(database, packedTarget, root) ||
          root >= database.semanticTypeCount ||
          !isBuiltinClassKind(read32(semanticRecord(root)) & UINT32_C(0xff)) ||
          read32(semanticRecord(root) + 12) != UINT32_MAX)
        return false;
      target = canonicalClassRoots.emplace(packedTarget, root).first;
    }
    const uint8_t *type = semanticRecord(semantic);
    const uint8_t *canonical = semanticRecord(target->second);
    uint32_t kind = read32(type) & UINT32_C(0xff);
    if (kind != (read32(canonical) & UINT32_C(0xff)) ||
        (!isBuiltinClassKind(kind) &&
         (kind != OBELISK_RT_DESIGN_SEMANTIC_CLASS ||
          read32(type + 16) != read32(canonical + 16))))
      return false;
    if (semantic == target->second)
      canonicalClassRootBindings.insert(packedTarget);
    semanticClassTargets.emplace(semantic, packedTarget);
    previousClassSemantic = semantic;
  }

  // Normalize the already validated DAG structurally, ignoring typedef
  // spelling. A class child uses its canonical specialization, since generic
  // siblings can share a lexical ClassDefn. Interned keys make repeated
  // subgraphs and duplicate canonical builtin types exact, without recursion.
  std::vector<uint32_t> normalizedTypes;
  std::map<std::vector<uint64_t>, uint32_t> normalizedTypeKeys;
  std::unordered_map<std::string_view, uint64_t> normalizedNames;
  auto normalizeName = [&](uint32_t relative) -> uint64_t {
    if (relative == 0)
      return 0;
    std::string_view name;
    if (!getRelativeString(relative, name))
      return UINT64_MAX;
    return normalizedNames.emplace(name, normalizedNames.size() + 1)
        .first->second;
  };
  auto normalizeType = [&](uint32_t root) {
    if (normalizedTypes.empty())
      normalizedTypes.resize(database.semanticTypeCount, 0);
    std::vector<SemanticWorkItem> work{{root, false}};
    while (!work.empty()) {
      auto [index, finish] = work.back();
      work.pop_back();
      if (normalizedTypes[index])
        continue;
      const uint8_t *type = semanticRecord(index);
      uint32_t firstEdge = read32(type + 4), count = read32(type + 8);
      if (!finish) {
        work.push_back({index, true});
        for (uint32_t ordinal = count; ordinal != 0; --ordinal) {
          const uint8_t *edge =
              database.data + database.semanticTypeEdges +
              uint64_t{firstEdge + ordinal - 1} * kSemanticTypeEdgeSize;
          work.push_back({read32(edge), false});
        }
        continue;
      }
      uint32_t classTarget = UINT32_MAX;
      if ((read32(type) & UINT32_C(0xff)) == OBELISK_RT_DESIGN_SEMANTIC_CLASS) {
        auto binding = semanticClassTargets.find(index);
        if (binding == semanticClassTargets.end())
          return false;
        classTarget = binding->second;
      }
      uint64_t name = normalizeName(read32(type + 20));
      uint64_t modport = normalizeName(read32(type + 24));
      if (name == UINT64_MAX || modport == UINT64_MAX)
        return false;
      std::vector<uint64_t> key{read32(type),
                                read32(type + 16),
                                classTarget,
                                name,
                                modport,
                                read32(type + 28),
                                read64(type + 32),
                                read64(type + 40),
                                read64(type + 48),
                                read64(type + 56),
                                count};
      for (uint32_t ordinal = 0; ordinal != count; ++ordinal) {
        const uint8_t *edge =
            database.data + database.semanticTypeEdges +
            uint64_t{firstEdge + ordinal} * kSemanticTypeEdgeSize;
        uint64_t edgeName = normalizeName(read32(edge + 12));
        if (edgeName == UINT64_MAX)
          return false;
        key.insert(key.end(), {normalizedTypes[read32(edge)], read32(edge + 4),
                               read32(edge + 8), edgeName, read64(edge + 16)});
      }
      normalizedTypes[index] =
          normalizedTypeKeys
              .emplace(std::move(key), normalizedTypeKeys.size() + 1)
              .first->second;
    }
    return true;
  };
  std::unordered_map<uint32_t, uint32_t> canonicalBuiltinTypes;
  std::unordered_set<uint32_t> canonicalBuiltinObjects;
  std::unordered_set<uint32_t> canonicalBuiltinChildren;
  for (const auto &[target, root] : canonicalClassRoots) {
    if (!isBuiltinClassKind(read32(semanticRecord(root)) & UINT32_C(0xff)))
      continue;
    canonicalBuiltinObjects.insert(target);
    std::vector<uint32_t> work{root};
    while (!work.empty()) {
      uint32_t index = work.back();
      work.pop_back();
      if (!canonicalBuiltinChildren.insert(index).second)
        continue;
      const uint8_t *type = semanticRecord(index);
      if (read32(type + 12) != UINT32_MAX)
        return false;
      for (uint32_t ordinal = 0; ordinal != read32(type + 8); ++ordinal) {
        const uint8_t *edge =
            database.data + database.semanticTypeEdges +
            uint64_t{read32(type + 4) + ordinal} * kSemanticTypeEdgeSize;
        work.push_back(read32(edge));
      }
    }
    if (!normalizeType(root) ||
        !canonicalBuiltinTypes.emplace(normalizedTypes[root], target).second)
      return false;
  }
  for (const auto &[semantic, target] : semanticClassTargets) {
    if (isBuiltinClassKind(read32(semanticRecord(semantic)) & UINT32_C(0xff)) &&
        (!normalizeType(semantic) ||
         normalizedTypes[semantic] !=
             normalizedTypes[canonicalClassRoots.at(target)]))
      return false;
  }
  for (const auto &[semantic, target] : semanticClassTargets) {
    const uint8_t *type = database.data + database.semanticTypes +
                          uint64_t{semantic} * kSemanticTypeSize;
    uint32_t alias = read32(type + 12);
    if (alias == UINT32_MAX)
      continue;
    uint32_t aliasRoot = UINT32_MAX;
    if (!findSemanticRootBinding(database, alias, aliasRoot))
      return false;
    auto binding = semanticClassTargets.find(aliasRoot);
    if (binding == semanticClassTargets.end() || binding->second != target)
      return false;
  }
  if (canonicalClassRootBindings.size() != canonicalClassRoots.size())
    return false;

  // Automatic local declarations have immutable source and storage shape,
  // but no allocation until an exact live frame binds an automatic reference.
  std::unordered_map<uint64_t, uint32_t> localCodeUnits;
  if (database.frameLocalBindingCount) {
    for (uint32_t index = 0; index != database.objectCount; ++index) {
      const uint8_t *object =
          database.data + database.objects + uint64_t{index} * kObjectSize;
      if (recordKind(object) != OBELISK_RT_DESIGN_RECORD_PROCESS &&
          recordKind(object) != OBELISK_RT_DESIGN_RECORD_FUNCTION)
        continue;
      auto [entry, inserted] =
          localCodeUnits.emplace(read64(object + 8), index);
      if (!inserted)
        entry->second = UINT32_MAX;
    }
  }
  std::unordered_map<uint32_t, std::unordered_set<uint32_t>> localOrdinals;
  std::unordered_map<uint32_t, std::unordered_set<std::string_view>> localNames;
  uint64_t previousLocalID = 0;
  for (uint64_t index = 0; index != database.frameLocalBindingCount; ++index) {
    obelisk::reflection::FrameLocalBindingView binding(
        database.data + database.frameLocalBindings +
        index * kFrameLocalBindingSize);
    const uint32_t objectIndex = binding.getObjectIndex();
    const uint32_t codeUnitIndex = binding.getOwnerCodeUnitIndex();
    const uint32_t scope = binding.getScopeIndexAndTable();
    if (!binding.getID() || binding.getID() > INT64_MAX ||
        binding.getID() <= previousLocalID ||
        objectIndex >= database.objectCount ||
        codeUnitIndex >= database.objectCount)
      return false;
    previousLocalID = binding.getID();
    const uint8_t *object =
        database.data + database.objects + uint64_t{objectIndex} * kObjectSize;
    const uint8_t *codeUnit = database.data + database.objects +
                              uint64_t{codeUnitIndex} * kObjectSize;
    uint32_t packed = 0;
    if (!obelisk::reflection::tryPackTableIndex(
            obelisk::reflection::TableKind::Object, objectIndex, packed) ||
        !frameLocals.count(packed) ||
        !frameLocalOwners.emplace(packed, scope).second ||
        read64(object + 16) != read64(codeUnit + 16))
      return false;
    auto uniqueUnit = localCodeUnits.find(read64(codeUnit + 8));
    if (uniqueUnit == localCodeUnits.end() ||
        uniqueUnit->second != codeUnitIndex)
      return false;
    using obelisk::reflection::TableKind;
    const uint32_t scopeIndex = obelisk::reflection::unpackTableIndex(scope);
    switch (obelisk::reflection::unpackTableIndexKind(scope)) {
    case TableKind::Object:
      if (scopeIndex != codeUnitIndex ||
          (recordVPIKind(codeUnit) !=
               static_cast<uint32_t>(
                   obelisk::reflection::VPIObjectKind::Task) &&
           recordVPIKind(codeUnit) !=
               static_cast<uint32_t>(
                   obelisk::reflection::VPIObjectKind::Function)))
        return false;
      break;
    case TableKind::Statement: {
      if (scopeIndex >= database.statementCount)
        return false;
      const uint8_t *statement = database.data + database.statements +
                                 uint64_t{scopeIndex} * kStatementSize;
      if (read32(statement + 8) != codeUnitIndex ||
          !(read16(statement + 38) & OBELISK_RT_DESIGN_STATEMENT_SCOPE))
        return false;
      break;
    }
    default:
      return false;
    }
    auto name = fixedNames.find(packed);
    if (name == fixedNames.end() || name->second.empty() ||
        !localNames[scope].insert(name->second).second ||
        !localOrdinals[scope].insert(binding.getOrdinal()).second)
      return false;
  }
  if (frameLocalOwners.size() != frameLocals.size())
    return false;
  for (const auto &[scope, ordinals] : localOrdinals)
    for (uint32_t ordinal = 0; ordinal != ordinals.size(); ++ordinal)
      if (!ordinals.count(ordinal))
        return false;

  // Each value layout owns a complete pairing between one semantic value
  // graph and exact normalized storage. Legacy mailbox rows share the proof,
  // but never overlap a common-owner group.
  using LayoutOwnerKind = obelisk::reflection::ValueLayoutOwnerKind;
  struct ValueLayout {
    uint32_t root = UINT32_MAX;
    uint32_t physicalRoot = UINT32_MAX;
    bool legacy = false;
    std::unordered_map<uint32_t, uint32_t> pairs;
  };
  std::unordered_map<uint64_t, ValueLayout> valueLayouts;
  auto ownerKey = [](LayoutOwnerKind kind, uint32_t index) {
    return (uint64_t{static_cast<uint32_t>(kind)} << 32) | index;
  };
  auto mailboxRoot = [&](uint32_t owner, uint32_t &root) {
    auto canonical = canonicalClassRoots.find(owner);
    if (canonical == canonicalClassRoots.end())
      return false;
    const uint8_t *mailbox = semanticRecord(canonical->second);
    if ((read32(mailbox) & UINT32_C(0xff)) !=
        OBELISK_RT_DESIGN_SEMANTIC_MAILBOX)
      return false;
    root = read32(database.data + database.semanticTypeEdges +
                  uint64_t{read32(mailbox + 4)} * kSemanticTypeEdgeSize);
    return true;
  };
  std::unordered_set<uint32_t> classFieldLayoutOwners;
  for (uint64_t index = 0; index != database.classMemberBindingCount; ++index) {
    obelisk::reflection::ClassMemberBindingView binding(
        database.data + database.classMemberBindings +
        index * kClassMemberBindingSize);
    if ((binding.getFlags() & 2) == 0)
      classFieldLayoutOwners.insert(binding.getMemberIndexAndTable());
  }
  auto reflectedRoot = [&](uint32_t owner, ValueLayout &layout) {
    if (mailboxRoot(owner, layout.root))
      return true;
    uint64_t offset = 0;
    if (!packedObjectReferenceOffset(database, owner, offset) ||
        !isObjectOffset(database, offset) ||
        !findSemanticRootBinding(database, owner, layout.root))
      return false;
    const uint8_t *record = database.data + offset;
    if (recordKind(record) != OBELISK_RT_DESIGN_RECORD_STORAGE &&
        !(recordKind(record) == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT &&
          (classFieldLayoutOwners.count(owner) || frameLocals.count(owner))))
      return false;
    const uint64_t physical = read64(record + 48);
    if (!isTypeOffset(database, physical))
      return false;
    layout.physicalRoot =
        static_cast<uint32_t>((physical - database.types) / kTypeSize);
    return true;
  };
  uint64_t previousBoxID = 0;
  for (uint64_t index = 0; index != database.boxTypeBindingCount; ++index) {
    obelisk::reflection::BoxTypeBindingView binding(
        database.data + database.boxTypeBindings + index * kBoxTypeBindingSize);
    if (binding.getID() == 0 || binding.getID() > INT64_MAX ||
        binding.getID() <= previousBoxID ||
        binding.getSemanticTypeIndex() >= database.semanticTypeCount ||
        binding.getPhysicalTypeIndex() >= database.typeCount)
      return false;
    auto &layout =
        valueLayouts[ownerKey(LayoutOwnerKind::BoxDescriptor, index)];
    layout.root = binding.getSemanticTypeIndex();
    layout.physicalRoot = binding.getPhysicalTypeIndex();
    previousBoxID = binding.getID();
  }
  uint64_t previousMailboxElement = 0;
  for (uint64_t index = 0; index != database.mailboxElementBindingCount;
       ++index) {
    obelisk::reflection::MailboxElementBindingView binding(
        database.data + database.mailboxElementBindings +
        index * kMailboxElementBindingSize);
    const uint32_t owner = binding.getTypespecIndexAndTable();
    const uint32_t semantic = binding.getSemanticTypeIndex();
    const uint32_t physical = binding.getPhysicalTypeIndex();
    const uint64_t key = (uint64_t{owner} << 32) | semantic;
    auto &layout =
        valueLayouts[ownerKey(LayoutOwnerKind::ReflectedObject, owner)];
    if ((index && key <= previousMailboxElement) ||
        semantic >= database.semanticTypeCount ||
        physical >= database.typeCount || !mailboxRoot(owner, layout.root))
      return false;
    layout.legacy = true;
    layout.pairs.emplace(semantic, physical);
    previousMailboxElement = key;
  }
  std::tuple<uint32_t, uint32_t, uint32_t> previousValueElement{};
  for (uint64_t index = 0; index != database.valueElementBindingCount;
       ++index) {
    obelisk::reflection::ValueElementBindingView binding(
        database.data + database.valueElementBindings +
        index * kValueElementBindingSize);
    const uint32_t encodedKind = binding.getOwnerKind();
    if (!obelisk::reflection::isValidValueLayoutOwnerKind(
            static_cast<LayoutOwnerKind>(encodedKind)))
      return false;
    const auto kind = static_cast<LayoutOwnerKind>(encodedKind);
    const uint32_t owner = binding.getOwnerIndex();
    const uint32_t semantic = binding.getSemanticTypeIndex();
    const uint32_t physical = binding.getPhysicalTypeIndex();
    const auto key = std::make_tuple(encodedKind, owner, semantic);
    if ((index && key <= previousValueElement) ||
        semantic >= database.semanticTypeCount ||
        physical >= database.typeCount ||
        (kind == LayoutOwnerKind::BoxDescriptor &&
         owner >= database.boxTypeBindingCount))
      return false;
    auto &layout = valueLayouts[ownerKey(kind, owner)];
    if (layout.legacy || (layout.root == UINT32_MAX &&
                          (kind != LayoutOwnerKind::ReflectedObject ||
                           !reflectedRoot(owner, layout))))
      return false;
    layout.pairs.emplace(semantic, physical);
    previousValueElement = key;
  }
  for (const auto &entry : valueLayouts) {
    const auto &layout = entry.second;
    const auto &layouts = layout.pairs;
    auto root = layouts.find(layout.root);
    if (!layout.legacy &&
        (root == layouts.end() || (layout.physicalRoot != UINT32_MAX &&
                                   root->second != layout.physicalRoot)))
      return false;
    std::vector<uint32_t> work{layout.root};
    std::unordered_set<uint32_t> visited;
    std::unordered_set<uint32_t> voidMembers;
    while (!work.empty()) {
      const uint32_t semanticIndex = work.back();
      work.pop_back();
      // Enumeration constants use their total packed width. Executable enum
      // storage can scalarize a composite source base; it has no layout view.
      if (layout.legacy && (read32(semanticRecord(semanticIndex)) &
                            UINT32_C(0xff)) == OBELISK_RT_DESIGN_SEMANTIC_ENUM)
        continue;
      if (!visited.insert(semanticIndex).second)
        continue;
      auto binding = layouts.find(semanticIndex);
      if (binding == layouts.end())
        return false;
      const uint8_t *semantic = semanticRecord(semanticIndex);
      const uint8_t *physical = database.data + database.types +
                                uint64_t{binding->second} * kTypeSize;
      const uint32_t semanticKind = read32(semantic) & UINT32_C(0xff);
      const uint32_t physicalKind = read32(physical + 4) & UINT32_C(0xff);
      const uint32_t physicalFlags = read32(physical + 4) >> 8;
      const bool fourState =
          (read32(semantic) & OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE) != 0;
      const bool physicalFourState =
          (physicalFlags & OBELISK_RT_DESIGN_TYPE_FOUR_STATE) != 0;
      const bool physicalPacked =
          (physicalFlags & OBELISK_RT_DESIGN_TYPE_PACKED) != 0;
      const uint32_t first = read32(semantic + 4);
      const uint32_t count = read32(semantic + 8);
      auto childEdge = [&](uint32_t ordinal) {
        return database.data + database.semanticTypeEdges +
               uint64_t{first + ordinal} * kSemanticTypeEdgeSize;
      };
      auto matchesChildPhysical = [&](uint32_t ordinal, uint64_t offset) {
        const uint32_t childIndex = read32(childEdge(ordinal));
        const uint8_t *childSemantic = semanticRecord(childIndex);
        if (layout.legacy && (read32(childSemantic) & UINT32_C(0xff)) ==
                                 OBELISK_RT_DESIGN_SEMANTIC_ENUM) {
          uint64_t width = 0;
          const uint8_t *childPhysical = database.data + offset;
          const uint32_t flags = read32(childPhysical + 4) >> 8;
          return semanticPackedBitWidth(database, childIndex, &width) ==
                     OBELISK_RT_OK &&
                 read64(childPhysical + 8) == width &&
                 (flags & OBELISK_RT_DESIGN_TYPE_PACKED) != 0 &&
                 ((flags & OBELISK_RT_DESIGN_TYPE_FOUR_STATE) != 0) ==
                     ((read32(childSemantic) &
                       OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE) != 0);
        }
        auto child = layouts.find(childIndex);
        return child != layouts.end() &&
               offset == database.types + uint64_t{child->second} * kTypeSize;
      };
      for (uint32_t ordinal = 0;
           semanticKind != OBELISK_RT_DESIGN_SEMANTIC_ENUM && ordinal != count;
           ++ordinal) {
        // Integral associative keys similarly normalize to the runtime lookup
        // domain. Their copied values use descriptor width, never field layout.
        const uint32_t childKind =
            read32(semanticRecord(read32(childEdge(ordinal)))) & UINT32_C(0xff);
        const bool integral =
            (childKind >= OBELISK_RT_DESIGN_SEMANTIC_GENERIC_INTEGRAL &&
             childKind <= OBELISK_RT_DESIGN_SEMANTIC_TIME) ||
            childKind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY ||
            childKind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_STRUCT ||
            childKind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_UNION;
        if (childKind == OBELISK_RT_DESIGN_SEMANTIC_VOID) {
          if ((semanticKind != OBELISK_RT_DESIGN_SEMANTIC_PACKED_UNION &&
               semanticKind != OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_UNION) ||
              (read32(semantic) & OBELISK_RT_DESIGN_SEMANTIC_TAGGED) == 0)
            return false;
          voidMembers.insert(read32(childEdge(ordinal)));
        }
        uint64_t keyWidth = 0;
        if (semanticKind == OBELISK_RT_DESIGN_SEMANTIC_ASSOC_ARRAY &&
            ordinal == 0 && integral &&
            semanticPackedBitWidth(database, read32(childEdge(ordinal)),
                                   &keyWidth) == OBELISK_RT_OK)
          continue;
        work.push_back(read32(childEdge(ordinal)));
      }
      switch (semanticKind) {
      case OBELISK_RT_DESIGN_SEMANTIC_ENUM: {
        uint64_t width = 0;
        if (!physicalPacked || physicalFourState != fourState ||
            semanticPackedBitWidth(database, semanticIndex, &width) !=
                OBELISK_RT_OK ||
            width == 0 || read64(physical + 8) != width)
          return false;
        break;
      }
      case OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY:
      case OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY:
        if (physicalKind != OBELISK_RT_DESIGN_TYPE_ARRAY ||
            physicalPacked !=
                (semanticKind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY) ||
            physicalFourState != fourState ||
            readI64(physical + 16) != readI64(semantic + 32) ||
            readI64(physical + 24) != readI64(semantic + 40) ||
            !matchesChildPhysical(0, read64(physical + 32)))
          return false;
        break;
      case OBELISK_RT_DESIGN_SEMANTIC_PACKED_STRUCT:
      case OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_STRUCT:
      case OBELISK_RT_DESIGN_SEMANTIC_PACKED_UNION:
      case OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_UNION: {
        const bool packed =
            semanticKind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_STRUCT ||
            semanticKind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_UNION;
        const bool isUnion =
            semanticKind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_UNION ||
            semanticKind == OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_UNION;
        const bool tagged =
            (read32(semantic) & OBELISK_RT_DESIGN_SEMANTIC_TAGGED) != 0;
        if (physicalKind != (isUnion ? OBELISK_RT_DESIGN_TYPE_UNION
                                     : OBELISK_RT_DESIGN_TYPE_STRUCT) ||
            physicalPacked != packed || physicalFourState != fourState ||
            ((physicalFlags & OBELISK_RT_DESIGN_TYPE_TAGGED) != 0) != tagged ||
            read64(physical + 56) != read64(semantic + 56) ||
            read64(physical + 48) != count ||
            (packed && read64(physical + 8) != read64(semantic + 48)))
          return false;
        for (uint32_t ordinal = 0; ordinal != count; ++ordinal) {
          const uint8_t *field = database.data + read64(physical + 40) +
                                 uint64_t{ordinal} * kTypeSize;
          const uint8_t *edge = childEdge(ordinal);
          std::string_view physicalName, semanticName;
          if (!matchesChildPhysical(ordinal, read64(field + 32)) ||
              !getString(database, read64(field + 72), physicalName) ||
              !getRelativeString(read32(edge + 12), semanticName) ||
              physicalName != semanticName ||
              (packed && read64(field + 64) != read64(edge + 16)))
            return false;
        }
        break;
      }
      case OBELISK_RT_DESIGN_SEMANTIC_GENERIC_INTEGRAL:
      case OBELISK_RT_DESIGN_SEMANTIC_BIT:
      case OBELISK_RT_DESIGN_SEMANTIC_LOGIC:
      case OBELISK_RT_DESIGN_SEMANTIC_REG:
      case OBELISK_RT_DESIGN_SEMANTIC_BYTE:
      case OBELISK_RT_DESIGN_SEMANTIC_SHORT_INT:
      case OBELISK_RT_DESIGN_SEMANTIC_INT:
      case OBELISK_RT_DESIGN_SEMANTIC_LONG_INT:
      case OBELISK_RT_DESIGN_SEMANTIC_INTEGER:
      case OBELISK_RT_DESIGN_SEMANTIC_TIME:
      case OBELISK_RT_DESIGN_SEMANTIC_SHORT_REAL:
      case OBELISK_RT_DESIGN_SEMANTIC_REAL:
      case OBELISK_RT_DESIGN_SEMANTIC_REALTIME: {
        const bool real =
            semanticKind == OBELISK_RT_DESIGN_SEMANTIC_SHORT_REAL ||
            semanticKind == OBELISK_RT_DESIGN_SEMANTIC_REAL ||
            semanticKind == OBELISK_RT_DESIGN_SEMANTIC_REALTIME;
        uint64_t width = 0;
        if (physicalKind != OBELISK_RT_DESIGN_TYPE_SCALAR ||
            physicalPacked == real || physicalFourState != fourState ||
            !semanticBitWidth(database, semanticIndex, width) || width == 0 ||
            read64(physical + 8) != width)
          return false;
        break;
      }
      case OBELISK_RT_DESIGN_SEMANTIC_VOID:
        if (voidMembers.count(semanticIndex) == 0 ||
            physicalKind != OBELISK_RT_DESIGN_TYPE_SCALAR || !physicalPacked ||
            physicalFourState || read64(physical + 8) != 1)
          return false;
        break;
      case OBELISK_RT_DESIGN_SEMANTIC_STRING:
      case OBELISK_RT_DESIGN_SEMANTIC_CHANDLE:
      case OBELISK_RT_DESIGN_SEMANTIC_DYNAMIC_ARRAY:
      case OBELISK_RT_DESIGN_SEMANTIC_QUEUE:
      case OBELISK_RT_DESIGN_SEMANTIC_ASSOC_ARRAY:
      case OBELISK_RT_DESIGN_SEMANTIC_CLASS:
      case OBELISK_RT_DESIGN_SEMANTIC_VIRTUAL_INTERFACE:
      case OBELISK_RT_DESIGN_SEMANTIC_EVENT:
      case OBELISK_RT_DESIGN_SEMANTIC_PROCESS:
      case OBELISK_RT_DESIGN_SEMANTIC_COVERGROUP:
      case OBELISK_RT_DESIGN_SEMANTIC_MAILBOX:
      case OBELISK_RT_DESIGN_SEMANTIC_SEMAPHORE:
      case OBELISK_RT_DESIGN_SEMANTIC_UNTYPED:
        if (physicalKind != OBELISK_RT_DESIGN_TYPE_SCALAR || physicalPacked ||
            physicalFourState || read64(physical + 8) != 64)
          return false;
        break;
      default:
        return false;
      }
    }
    // A reachable graph can share nodes, but cannot hide unrelated bindings.
    if (visited.size() != layouts.size())
      return false;
  }

  for (uint32_t local : frameLocals)
    if (!valueLayouts.count(ownerKey(LayoutOwnerKind::ReflectedObject, local)))
      return false;

  std::unordered_set<uint32_t> boundEvents;
  uint32_t previousEvent = 0;
  for (uint64_t index = 0; index != database.eventBindingCount; ++index) {
    obelisk::reflection::EventBindingView binding(
        database.data + database.eventBindings + index * kEventBindingSize);
    uint32_t event = binding.getEventIndexAndTable();
    uint64_t offset = 0;
    if ((index && event <= previousEvent) || binding.getReserved() != 0 ||
        binding.getEventID() > INT64_MAX ||
        !packedObjectReferenceOffset(database, event, offset) ||
        objectReferenceVPIKind(database, offset) !=
            static_cast<uint32_t>(
                obelisk::reflection::VPIObjectKind::NamedEvent))
      return false;
    // Static anchors have no state storage, class receiver, or mutable cell.
    if (!isStaticObjectOffset(database, offset) &&
        (!isObjectOffset(database, offset) ||
         recordKind(database.data + offset) !=
             OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT))
      return false;
    boundEvents.insert(event);
    previousEvent = event;
  }

  std::unordered_set<uint32_t> boundModports;
  std::unordered_set<uint64_t> modportParents;
  std::unordered_map<uint32_t, std::unordered_set<std::string_view>>
      modportNames;
  uint32_t previousModport = 0;
  for (uint64_t index = 0; index != database.modportBindingCount; ++index) {
    obelisk::reflection::ModportBindingView binding(
        database.data + database.modportBindings + index * kModportBindingSize);
    uint32_t modport = binding.getModportIndexAndTable();
    uint32_t scope = binding.getScopeIndex();
    uint64_t offset = 0;
    std::string_view name;
    if ((index && modport <= previousModport) || binding.getReserved() != 0 ||
        scope >= database.scopeCount ||
        !packedObjectReferenceOffset(database, modport, offset) ||
        objectReferenceVPIKind(database, offset) !=
            static_cast<uint32_t>(
                obelisk::reflection::VPIObjectKind::Modport) ||
        !getRelativeString(binding.getName(), name) || name.empty() ||
        database.data[database.strings + binding.getName() - 1] != 0 ||
        !modportNames[scope].insert(name).second)
      return false;
    uint64_t scopeOffset = database.scopes + uint64_t{scope} * kScopeSize;
    if (recordVPIKind(database.data + scopeOffset) !=
        static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::Interface))
      return false;
    if (isStaticObjectOffset(database, offset)) {
      if (read32(database.data + offset + 8) != scope)
        return false;
    } else if (!isObjectOffset(database, offset) ||
               recordKind(database.data + offset) !=
                   OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT ||
               read64(database.data + offset + 16) != scopeOffset) {
      return false;
    }
    previousModport = modport;
    boundModports.insert(modport);
    modportParents.insert((uint64_t{scope} << 32) | modport);
  }
  auto modportHasBinding = [&](obelisk::reflection::TableKind table,
                               uint32_t index, uint32_t kind) {
    uint32_t packed = 0;
    return kind != static_cast<uint32_t>(
                       obelisk::reflection::VPIObjectKind::Modport) ||
           (obelisk::reflection::tryPackTableIndex(table, index, packed) &&
            boundModports.count(packed) != 0);
  };
  for (uint32_t index = 0; index != database.objectCount; ++index)
    if (!modportHasBinding(obelisk::reflection::TableKind::Object, index,
                           recordVPIKind(database.data + database.objects +
                                         uint64_t{index} * kObjectSize)))
      return false;
  for (uint32_t index = 0; index != database.staticObjectCount; ++index)
    if (!modportHasBinding(obelisk::reflection::TableKind::StaticObject, index,
                           read16(database.data + database.staticObjects +
                                  uint64_t{index} * kStaticObjectSize + 28)))
      return false;

  // The lexical array owns the public identity; a checked binding keeps its
  // executable cells private. Physical and semantic dimensions are checked
  // against the relation-index shape below, after that inventory is validated.
  std::unordered_map<uint32_t, uint64_t> eventArrayStorage;
  std::unordered_set<uint64_t> eventBackingStorage;
  uint32_t previousEventArray = 0;
  for (uint64_t index = 0; index != database.eventArrayStorageBindingCount;
       ++index) {
    obelisk::reflection::EventArrayStorageBindingView binding(
        database.data + database.eventArrayStorageBindings +
        index * kEventArrayStorageBindingSize);
    const uint32_t array = binding.getArrayIndexAndTable();
    uint64_t arrayOffset = 0;
    if ((index && array <= previousEventArray) ||
        obelisk::reflection::unpackTableIndexKind(array) !=
            obelisk::reflection::TableKind::Object ||
        !packedObjectReferenceOffset(database, array, arrayOffset) ||
        recordKind(database.data + arrayOffset) !=
            OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT ||
        objectReferenceVPIKind(database, arrayOffset) !=
            static_cast<uint32_t>(
                obelisk::reflection::VPIObjectKind::NamedEventArray) ||
        read64(database.data + arrayOffset + 48) != 0 ||
        binding.getStorageIndex() >= database.objectCount)
      return false;
    const uint64_t storageOffset =
        database.objects + uint64_t{binding.getStorageIndex()} * kObjectSize;
    const uint8_t *storage = database.data + storageOffset;
    // The private backing retains its physical array classification. The
    // public declaration above carries the named-event-array identity.
    if (recordKind(storage) != OBELISK_RT_DESIGN_RECORD_STORAGE ||
        recordVPIKind(storage) !=
            static_cast<uint32_t>(
                obelisk::reflection::VPIObjectKind::ArrayVar) ||
        read64(storage + 16) != read64(database.data + arrayOffset + 16) ||
        !isTypeOffset(database, read64(storage + 48)) ||
        !eventBackingStorage.insert(storageOffset).second)
      return false;
    eventArrayStorage.emplace(array, storageOffset);
    previousEventArray = array;
  }

  std::unordered_set<uint32_t> boundClassFields;
  std::unordered_set<uint64_t> classBackingStorage;
  std::unordered_set<uint64_t> classMemberParents;
  uint32_t previousMember = 0;
  for (uint64_t index = 0; index != database.classMemberBindingCount; ++index) {
    obelisk::reflection::ClassMemberBindingView binding(
        database.data + database.classMemberBindings +
        index * kClassMemberBindingSize);
    uint32_t member = binding.getMemberIndexAndTable();
    uint32_t owner = binding.getOwnerIndexAndTable();
    auto canonical = canonicalClassTypes.find(binding.getClassID());
    uint64_t memberOffset = 0, ownerOffset = 0;
    std::string_view memberName;
    if ((index && member <= previousMember) || binding.getFlags() > 3 ||
        boundEvents.count(member) != 0 ||
        eventArrayStorage.count(member) != 0 ||
        canonical == canonicalClassTypes.end() ||
        !packedObjectReferenceOffset(database, member, memberOffset) ||
        !packedObjectReferenceOffset(database, owner, ownerOffset) ||
        !getRelativeString(binding.getName(), memberName) || memberName.empty())
      return false;
    previousMember = member;
    uint32_t canonicalRoot = canonicalClassRoots.at(canonical->second);
    const uint8_t *classType = database.data + database.semanticTypes +
                              uint64_t{canonicalRoot} * kSemanticTypeSize;
    if (owner != canonical->second && owner != read32(classType + 16))
      return false;
    uint32_t kind = objectReferenceVPIKind(database, memberOffset);
    const auto *scopeEdge = obelisk::reflection::findVPITraversal(
        kind, static_cast<uint32_t>(obelisk::reflection::VPIRelationKind::ScopeRel),
        obelisk::reflection::VPITraversalMode::Handle);
    if (scopeEdge && obelisk::reflection::vpiObjectSetContains(
                         scopeEdge->targets,
                         objectReferenceVPIKind(database, ownerOffset)))
      classMemberParents.insert((uint64_t{owner} << 32) | member);
    bool isStatic = (binding.getFlags() & 1) != 0;
    bool isMethod = (binding.getFlags() & 2) != 0;
    bool hasStorage = binding.getStorageIndexAndTable() != UINT32_MAX;
    bool hasEvent = binding.getEventID() != UINT64_MAX;
    if (isMethod) {
      if ((kind != static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::Task) &&
           kind != static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::Function)) ||
          hasStorage || hasEvent || binding.getByteOffset() != UINT64_MAX)
        return false;
      continue;
    }
    uint32_t semanticRoot = UINT32_MAX;
    bool dataVariable = obelisk::reflection::vpiObjectSetContains(
        obelisk::reflection::VPIObjectSetID::DataVariableObjects, kind);
    bool namedEvent = kind == static_cast<uint32_t>(
                                 obelisk::reflection::VPIObjectKind::NamedEvent);
    bool namedEventArray = kind == static_cast<uint32_t>(
                                      obelisk::reflection::VPIObjectKind::NamedEventArray);
    if ((!dataVariable && !namedEvent && !namedEventArray) ||
        !isObjectOffset(database, memberOffset) ||
        recordKind(database.data + memberOffset) != OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT ||
        read64(database.data + memberOffset + 48) == 0 ||
        !findSemanticRootBinding(database, member, semanticRoot) ||
        (isStatic ? binding.getByteOffset() != UINT64_MAX || hasStorage == hasEvent
                  : binding.getByteOffset() < sizeof(void *) ||
                        binding.getByteOffset() == UINT64_MAX || hasStorage || hasEvent) ||
        (hasEvent && !namedEvent))
      return false;
    if (hasStorage) {
      uint64_t storageOffset = 0;
      if (!packedObjectReferenceOffset(database, binding.getStorageIndexAndTable(),
                                       storageOffset) ||
          !isObjectOffset(database, storageOffset) ||
          recordKind(database.data + storageOffset) != OBELISK_RT_DESIGN_RECORD_STORAGE ||
          read64(database.data + storageOffset + 48) !=
              read64(database.data + memberOffset + 48))
        return false;
      if (eventBackingStorage.count(storageOffset) != 0)
        return false;
      classBackingStorage.insert(storageOffset);
    }
    boundClassFields.insert(member);
  }
  // Private delegated storage has a separate public identity. Ordinary
  // procedural static variables retain their storage identity, but their
  // exact lexical scope must be proved by the relations below.
  std::unordered_set<uint32_t> proceduralStaticStorage;
  std::unordered_map<uint32_t, uint32_t> proceduralStaticOwners,
      proceduralStaticScopes;
  for (uint32_t index = 0; index != database.objectCount; ++index) {
    uint64_t offset = database.objects + uint64_t{index} * kObjectSize;
    const uint8_t *object = database.data + offset;
    if (recordKind(object) != OBELISK_RT_DESIGN_RECORD_STORAGE)
      continue;
    bool marked =
        (read32(object + 4) & OBELISK_RT_DESIGN_CAP_LEXICAL_ANCHOR) != 0;
    bool delegated =
        classBackingStorage.count(offset) || eventBackingStorage.count(offset);
    if (delegated && !marked)
      return false;
    if (marked && !delegated) {
      uint32_t packed = 0;
      if ((read32(object + 4) & OBELISK_RT_DESIGN_CAP_INTERNAL) ||
          !obelisk::reflection::vpiObjectSetContains(
              obelisk::reflection::VPIObjectSetID::DataVariableObjects,
              recordVPIKind(object)) ||
          !obelisk::reflection::tryPackTableIndex(
              obelisk::reflection::TableKind::Object, index, packed))
        return false;
      proceduralStaticStorage.insert(packed);
    }
  }
  // Variable kinds newly admitted to the immutable table are class
  // declarations, never synthetic executable storage. Existing source-only
  // kinds (including named events and their arrays) retain their lexical
  // inventory representation without requiring a class-member binding.
  for (uint32_t index = 0; index != database.objectCount; ++index) {
    const uint8_t *object = database.data + database.objects +
                            uint64_t{index} * kObjectSize;
    if (recordKind(object) != OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT ||
        !obelisk::reflection::vpiObjectSetContains(
            obelisk::reflection::VPIObjectSetID::DataVariableObjects,
            recordVPIKind(object)) ||
        obelisk::reflection::vpiObjectSetContains(
            obelisk::reflection::VPIObjectSetID::StaticImageObjects,
            recordVPIKind(object)))
      continue;
    uint32_t packed = 0;
    if (!obelisk::reflection::tryPackTableIndex(
            obelisk::reflection::TableKind::Object, index, packed) ||
        (!boundClassFields.count(packed) &&
         !sourceClassProperties.count(packed) && !frameLocals.count(packed)))
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
  std::unordered_set<uint32_t> frozenEnumConstants;
  for (uint64_t index = 0; index != database.frozenValueBindingCount; ++index) {
    const uint8_t *binding = database.data + database.frozenValueBindings +
                             index * kFrozenValueBindingSize;
    uint32_t packedSource = read32(binding);
    uint32_t valueIndex = read32(binding + 4);
    uint64_t objectOffset = 0;
    if (packedSource == UINT32_MAX ||
        (index != 0 && packedSource <= previousFrozenSource) ||
        !packedObjectReferenceOffset(database, packedSource, objectOffset) ||
        (objectReferenceVPIKind(database, objectOffset) !=
             static_cast<uint32_t>(
                 obelisk::reflection::VPIObjectKind::Parameter) &&
         objectReferenceVPIKind(database, objectOffset) !=
             static_cast<uint32_t>(
                 obelisk::reflection::VPIObjectKind::EnumConst) &&
         sourceExpressionConstants.count(packedSource) == 0) ||
        unresolvedValueParameters.count(packedSource) != 0 ||
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
    bool isEnum =
        objectReferenceVPIKind(database, objectOffset) ==
        static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::EnumConst);
    if (isEnum) {
      if ((semanticFlags & UINT32_C(0xff)) != OBELISK_RT_DESIGN_SEMANTIC_ENUM)
        return false;
      frozenEnumConstants.insert(packedSource);
    }
    if (sourceExpressionConstants.count(packedSource) != 0) {
      if (read32(semantic + 8) != 0 || read32(semantic + 12) != UINT32_MAX)
        return false;
      switch (semanticFlags & UINT32_C(0xff)) {
      case OBELISK_RT_DESIGN_SEMANTIC_GENERIC_INTEGRAL:
      case OBELISK_RT_DESIGN_SEMANTIC_BIT:
      case OBELISK_RT_DESIGN_SEMANTIC_LOGIC:
      case OBELISK_RT_DESIGN_SEMANTIC_REG:
      case OBELISK_RT_DESIGN_SEMANTIC_BYTE:
      case OBELISK_RT_DESIGN_SEMANTIC_SHORT_INT:
      case OBELISK_RT_DESIGN_SEMANTIC_INT:
      case OBELISK_RT_DESIGN_SEMANTIC_LONG_INT:
      case OBELISK_RT_DESIGN_SEMANTIC_INTEGER:
      case OBELISK_RT_DESIGN_SEMANTIC_TIME:
        break;
      default:
        return false;
      }
    }
    uint64_t semanticWidth = 0;
    if ((isEnum ? semanticPackedBitWidth(database, semanticRoot,
                                         &semanticWidth) != OBELISK_RT_OK
                : !semanticBitWidth(database, semanticRoot, semanticWidth)) ||
        semanticWidth != read64(record + 8) ||
        ((semanticFlags & OBELISK_RT_DESIGN_SEMANTIC_SIGNED) != 0) !=
            ((kindAndFlags & obelisk::reflection::frozenValueSigned) != 0) ||
        ((semanticFlags & OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE) != 0) !=
            ((kindAndFlags & obelisk::reflection::frozenValueFourState) != 0))
      return false;
  }
  for (uint32_t index = 0; index != database.staticObjectCount; ++index) {
    const uint8_t *object = database.data + database.staticObjects +
                            uint64_t{index} * kStaticObjectSize;
    if (read16(object + 28) ==
            static_cast<uint32_t>(
                obelisk::reflection::VPIObjectKind::PrimTerm) ||
        read16(object + 28) ==
            static_cast<uint32_t>(
                obelisk::reflection::VPIObjectKind::Operation) ||
        !obelisk::reflection::findVPIValuePolicy(read16(object + 28)) ||
        (read16(object + 30) &
         OBELISK_RT_DESIGN_CAP_UNRESOLVED_VALUE_PARAMETER) != 0)
      continue;
    uint32_t packedSource = 0;
    VPIFrozenValue value{};
    if (!obelisk::reflection::tryPackTableIndex(
            obelisk::reflection::TableKind::StaticObject, index,
            packedSource) ||
        !findFrozenValue(database, packedSource, value))
      return false;
  }
  for (uint32_t index = 0; index != database.objectCount; ++index) {
    const uint8_t *object =
        database.data + database.objects + uint64_t{index} * kObjectSize;
    if (recordKind(object) != OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT ||
        recordVPIKind(object) !=
            static_cast<uint32_t>(
                obelisk::reflection::VPIObjectKind::Parameter) ||
        (read32(object + 4) &
         OBELISK_RT_DESIGN_CAP_UNRESOLVED_VALUE_PARAMETER) != 0)
      continue;
    uint32_t packed = 0;
    VPIFrozenValue value{};
    if (!obelisk::reflection::tryPackTableIndex(
            obelisk::reflection::TableKind::Object, index, packed) ||
        !findFrozenValue(database, packed, value))
      return false;
  }
  for (uint32_t constant : sourceExpressionConstants) {
    VPIFrozenValue value{};
    if (!findFrozenValue(database, constant, value))
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
    bool loopScope =
        isScope &&
        (vpiKind ==
             static_cast<uint16_t>(obelisk::reflection::VPIObjectKind::For) ||
         vpiKind == static_cast<uint16_t>(
                        obelisk::reflection::VPIObjectKind::ForeachStmt));
    if ((named && name == 0) ||
        (name != 0 && ((!named && !loopScope) || text.empty())))
      return false;
  }
  std::unordered_set<uint32_t> callObjects;
  std::unordered_map<uint32_t, uint32_t> callOwners;
  std::unordered_set<uint32_t> callScopes;
  if (database.threadOriginBindingCount || database.callSiteBindingCount) {
    // IDs belong to a shared executable code-unit namespace. Legacy images
    // without origin bindings retain their existing per-record-kind checks.
    std::unordered_map<uint64_t, uint64_t> codeUnits;
    for (uint64_t index = 0; index != database.objectCount; ++index) {
      const uint64_t offset = database.objects + index * kObjectSize;
      const uint8_t *record = database.data + offset;
      if (recordKind(record) != OBELISK_RT_DESIGN_RECORD_PROCESS &&
          recordKind(record) != OBELISK_RT_DESIGN_RECORD_FUNCTION)
        continue;
      auto [entry, inserted] = codeUnits.emplace(read64(record + 8), offset);
      if (!inserted)
        entry->second = 0;
    }
    const auto *originEdge = obelisk::reflection::findVPITraversal(
        static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::Thread),
        static_cast<uint32_t>(obelisk::reflection::VPIRelationKind::OriginRel),
        obelisk::reflection::VPITraversalMode::Handle);
    uint64_t previous = 0;
    for (uint64_t index = 0; index != database.threadOriginBindingCount;
         ++index) {
      obelisk::reflection::ThreadOriginBindingView binding(
          database.data + database.threadOriginBindings +
          index * kThreadOriginBindingSize);
      const uint64_t codeUnitID = binding.getCodeUnitID();
      auto codeUnit = codeUnits.find(codeUnitID);
      if (!codeUnitID || codeUnitID > INT64_MAX ||
          (index && codeUnitID <= previous) || binding.getReserved() ||
          binding.getStatementIndex() >= database.statementCount ||
          codeUnit == codeUnits.end() || !codeUnit->second)
        return false;
      const uint8_t *record = database.data + codeUnit->second;
      const uint8_t *statement =
          database.data + database.statements +
          uint64_t{binding.getStatementIndex()} * kStatementSize;
      const uint32_t owner = read32(statement + 8);
      if (owner == UINT32_MAX ||
          read64(record + 16) !=
              database.scopes + uint64_t{read32(statement + 12)} * kScopeSize ||
          (codeUnit->second !=
               database.objects + uint64_t{owner} * kObjectSize &&
           !(read32(record + 4) & OBELISK_RT_DESIGN_CAP_INTERNAL)) ||
          !originEdge ||
          !obelisk::reflection::vpiObjectSetContains(originEdge->targets,
                                                     read16(statement + 36)))
        return false;
      previous = codeUnitID;
    }
    previous = 0;
    for (uint64_t index = 0; index != database.callSiteBindingCount; ++index) {
      obelisk::reflection::CallSiteBindingView binding(
          database.data + database.callSiteBindings +
          index * kCallSiteBindingSize);
      uint64_t id = binding.getID();
      uint32_t packed = binding.getObjectIndexAndTable();
      uint32_t owner = binding.getOwnerCodeUnitIndex();
      if (!id || id > INT64_MAX || (index && id <= previous) ||
          owner >= database.objectCount || !callObjects.insert(packed).second ||
          (binding.getFlags() & ~UINT32_C(7)) ||
          ((binding.getFlags() & 6) && !(binding.getFlags() & 1)) ||
          ((binding.getFlags() & 6) == 6) || binding.getReserved())
        return false;
      const uint64_t ownerOffset =
          database.objects + uint64_t{owner} * kObjectSize;
      const uint8_t *codeUnit = database.data + ownerOffset;
      auto found = codeUnits.find(read64(codeUnit + 8));
      if (found == codeUnits.end() || found->second != ownerOffset)
        return false;
      using obelisk::reflection::TableKind;
      uint32_t target = obelisk::reflection::unpackTableIndex(packed);
      uint32_t kind = 0;
      switch (obelisk::reflection::unpackTableIndexKind(packed)) {
      case TableKind::Statement: {
        if (target >= database.statementCount)
          return false;
        const uint8_t *statement = database.data + database.statements +
                                   uint64_t{target} * kStatementSize;
        if (read32(statement + 8) != owner)
          return false;
        kind = read16(statement + 36);
        break;
      }
      case TableKind::Object: {
        if (target >= database.objectCount)
          return false;
        const uint8_t *object =
            database.data + database.objects + uint64_t{target} * kObjectSize;
        if (recordKind(object) != OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT ||
            read64(object + 16) != read64(codeUnit + 16) ||
            read64(object + 48) || read64(object + 56) || read64(object + 80))
          return false;
        kind = recordVPIKind(object);
        break;
      }
      default:
        return false;
      }
      using Kind = obelisk::reflection::VPIObjectKind;
      switch (static_cast<Kind>(kind)) {
      case Kind::TaskCall:
      case Kind::FuncCall:
      case Kind::MethodTaskCall:
      case Kind::MethodFuncCall:
      case Kind::SysTaskCall:
      case Kind::SysFuncCall:
        break;
      default:
        return false;
      }
      if ((binding.getFlags() & 1) &&
          kind != static_cast<uint32_t>(Kind::MethodFuncCall) &&
          kind != static_cast<uint32_t>(Kind::MethodTaskCall) &&
          kind != static_cast<uint32_t>(Kind::FuncCall) &&
          kind != static_cast<uint32_t>(Kind::TaskCall))
        return false;
      uint32_t initializer = 0;
      if (findInitializerSource(database, read64(codeUnit + 8), initializer) &&
          obelisk::reflection::unpackTableIndexKind(packed) ==
              TableKind::Statement)
        return false;
      callOwners.emplace(packed, owner);
      previous = id;
    }
  }
  // Every full static call is owned by an explicit occurrence binding. The
  // general static-kind allowance must not admit forged unbound call records.
  for (uint32_t index = 0; index != database.objectCount; ++index) {
    const uint8_t *object =
        database.data + database.objects + uint64_t{index} * kObjectSize;
    if (recordKind(object) != OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT ||
        !obelisk::reflection::vpiObjectSetContains(
            obelisk::reflection::VPIObjectSetID::TaskFunctionCallObjects,
            recordVPIKind(object)))
      continue;
    uint32_t packed = 0;
    if (!obelisk::reflection::tryPackTableIndex(
            obelisk::reflection::TableKind::Object, index, packed))
      return false;
    if (callObjects.count(packed) == 0)
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
    uint16_t supportedFlags = parameterSourceFlags;
    if (vpiKind ==
        static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::Parameter))
      supportedFlags |= OBELISK_RT_DESIGN_CAP_PARAMETER_EXPLICIT_RANGE;
    if ((index != 0 && id <= previousStaticObjectID) ||
        (scopeIndex != UINT32_MAX && scopeIndex >= database.scopeCount) ||
        (flags & ~supportedFlags) != 0 ||
        !validParameterSourceFlags(vpiKind, flags) ||
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
  std::unordered_set<uint64_t> sharedMemberInventories;
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
    bool primitive = kind == static_cast<uint16_t>(Kind::Gate) ||
                     kind == static_cast<uint16_t>(Kind::Switch);
    const auto *primitiveType =
        primitive ? obelisk::reflection::findVPIBuiltinPrimitive(flags)
                  : nullptr;
    if ((primitive ? !primitiveType || primitiveType->objectKind != kind ||
                         file != 0 || line != 0 || memberCount != 0 ||
                         relationCount != 0
                   : (kind != static_cast<uint16_t>(Kind::Module) &&
                      kind != static_cast<uint16_t>(Kind::Interface) &&
                      kind != static_cast<uint16_t>(Kind::Program)) ||
                         flags != 0) ||
        name == 0 || line > INT32_MAX || (file == 0) != (line == 0) ||
        (memberCount != 0 && firstMember != nextDefinitionMember) ||
        firstMember > database.definitionMemberCount ||
        memberCount > database.definitionMemberCount - firstMember ||
        firstRelation != nextDefinitionMemberRelation ||
        firstRelation > database.definitionMemberRelationCount ||
        relationCount > database.definitionMemberRelationCount - firstRelation)
      return false;
    std::string_view text;
    if (!getRelativeString(name, text) || text.empty() ||
        (primitive && text != primitiveType->sourceName) ||
        (file != 0 && !getRelativeString(file, text)))
      return false;

    std::unordered_set<std::string_view> memberNames;
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
          (memberKind == static_cast<uint16_t>(Kind::IODecl) ? memberFlags > 6
           : memberKind == static_cast<uint16_t>(Kind::Parameter)
               ? (memberFlags &
                  ~(kDefinitionParameterLocal | kDefinitionParameterRange |
                    kDefinitionParameterDefaultUnavailable |
                    kDefinitionParameterOriginalDefault)) != 0
           : memberKind == static_cast<uint16_t>(Kind::TypeParameter)
               ? (memberFlags & ~(kDefinitionParameterLocal |
                                  kDefinitionParameterDefaultUnavailable |
                                  kDefinitionParameterOriginalDefault)) != 0
               : true) ||
          ((memberFlags & kDefinitionParameterDefaultUnavailable) &&
           (memberFlags & kDefinitionParameterOriginalDefault)) ||
          !getRelativeString(memberName, text) || text.empty() ||
          !memberNames.insert(text).second ||
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
          !edge || !isDefinitionMemberInventory(*edge))
        return false;
      previousSelector = selector;
      uint32_t expectedCount = 0;
      for (uint32_t memberOrdinal = 0; memberOrdinal != memberCount;
           ++memberOrdinal) {
        const uint8_t *member =
            database.data + database.definitionMembers +
            uint64_t{firstMember + memberOrdinal} * kDefinitionMemberSize;
        expectedCount += obelisk::reflection::vpiObjectSetContains(
            edge->targets, read16(member + 16));
      }
      if (targetCount != expectedCount)
        return false;
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
    for (const auto &edge : obelisk::reflection::vpiTraversals) {
      if (edge.sourceType != kind || !isDefinitionMemberInventory(edge))
        continue;
      bool expected = false, found = false;
      for (uint32_t ordinal = 0; ordinal != memberCount; ++ordinal)
        expected |= obelisk::reflection::vpiObjectSetContains(
            edge.targets,
            read16(database.data + database.definitionMembers +
                   uint64_t{firstMember + ordinal} * kDefinitionMemberSize +
                   16));
      for (uint32_t ordinal = 0; ordinal != relationCount; ++ordinal)
        found |= read16(database.data + database.definitionMemberRelations +
                        uint64_t{firstRelation + ordinal} *
                            kDefinitionMemberRelationSize) == edge.selector;
      if (expected != found)
        return false;
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
    // Built-in primitive definitions have no effective-type specialization.
    if (read16(definition + 2) != 0 || bindingCount != memberCount)
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

  uint32_t nextParameterValue = 0;
  for (uint32_t index = 0;
       index != database.definitionSpecializationBindingCount; ++index) {
    const uint8_t *binding =
        database.data + database.definitionSpecializationBindings +
        uint64_t{index} * kDefinitionSpecializationBindingSize;
    const uint8_t *member = database.data + database.definitionMembers +
                            uint64_t{read32(binding)} * kDefinitionMemberSize;
    if (read16(member + 16) !=
        static_cast<uint16_t>(obelisk::reflection::VPIObjectKind::Parameter))
      continue;
    if (nextParameterValue == database.definitionParameterValueCount)
      return false;
    const uint8_t *valueBinding =
        database.data + database.definitionParameterValues +
        uint64_t{nextParameterValue++} * kDefinitionParameterValueSize;
    uint32_t valueIndex = read32(valueBinding + 4);
    if (read32(valueBinding) != index ||
        valueIndex >= database.frozenValueCount)
      return false;
    uint32_t semanticIndex = read32(binding + 4);
    const uint8_t *semantic = database.data + database.semanticTypes +
                              uint64_t{semanticIndex} * kSemanticTypeSize;
    uint32_t semanticFlags = read32(semantic);
    if ((read16(member + 18) & (UINT16_C(1) << 9)) &&
        ((semanticFlags & UINT32_C(0xff)) !=
             OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY ||
         !(semanticFlags & OBELISK_RT_DESIGN_SEMANTIC_HAS_RANGE)))
      return false;
    switch (semanticFlags & UINT32_C(0xff)) {
    case OBELISK_RT_DESIGN_SEMANTIC_GENERIC_INTEGRAL:
    case OBELISK_RT_DESIGN_SEMANTIC_BIT:
    case OBELISK_RT_DESIGN_SEMANTIC_LOGIC:
    case OBELISK_RT_DESIGN_SEMANTIC_REG:
    case OBELISK_RT_DESIGN_SEMANTIC_BYTE:
    case OBELISK_RT_DESIGN_SEMANTIC_SHORT_INT:
    case OBELISK_RT_DESIGN_SEMANTIC_INT:
    case OBELISK_RT_DESIGN_SEMANTIC_LONG_INT:
    case OBELISK_RT_DESIGN_SEMANTIC_INTEGER:
    case OBELISK_RT_DESIGN_SEMANTIC_TIME:
    case OBELISK_RT_DESIGN_SEMANTIC_ENUM:
    case OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY:
    case OBELISK_RT_DESIGN_SEMANTIC_PACKED_STRUCT:
    case OBELISK_RT_DESIGN_SEMANTIC_PACKED_UNION:
      break;
    default:
      return false;
    }
    const uint8_t *value = database.data + database.frozenValues +
                           uint64_t{valueIndex} * kFrozenValueSize;
    uint64_t width = 0;
    if (semanticPackedBitWidth(database, semanticIndex, &width) !=
            OBELISK_RT_OK ||
        width != read64(value + 8) ||
        ((semanticFlags & OBELISK_RT_DESIGN_SEMANTIC_SIGNED) != 0) !=
            ((read32(value) & obelisk::reflection::frozenValueSigned) != 0) ||
        ((semanticFlags & OBELISK_RT_DESIGN_SEMANTIC_FOUR_STATE) != 0) !=
            ((read32(value) & obelisk::reflection::frozenValueFourState) != 0))
      return false;
    frozenValueReferenced[valueIndex] = 1;
  }
  if (!validateExpressions(database, frozenValueReferenced))
    return false;
  if (nextParameterValue != database.definitionParameterValueCount ||
      std::find(frozenValueReferenced.begin(), frozenValueReferenced.end(),
                uint8_t{0}) != frozenValueReferenced.end())
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
        (table == obelisk::reflection::TableKind::Scope
             ? sourceIndex >= database.scopeCount
             : table != obelisk::reflection::TableKind::Object ||
                   sourceIndex >= database.objectCount) ||
        definitionIndex >= database.definitionCount ||
        firstMemberEndpoint != nextDefinitionMemberEndpoint ||
        (specializationIndex != UINT32_MAX &&
         specializationIndex >= database.definitionSpecializationCount))
      return false;
    previousDefinitionSource = packedSource;
    bool primitive = table == obelisk::reflection::TableKind::Object;
    const uint8_t *source =
        database.data + (primitive ? database.objects : database.scopes) +
        uint64_t{sourceIndex} * (primitive ? kObjectSize : kScopeSize);
    const uint8_t *definition = database.data + database.definitions +
                                uint64_t{definitionIndex} * kDefinitionSize;
    if (primitive &&
        (recordKind(source) != OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT ||
         (recordVPIKind(source) !=
              static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::Gate) &&
          recordVPIKind(source) !=
              static_cast<uint32_t>(
                  obelisk::reflection::VPIObjectKind::Switch)) ||
         specializationIndex != UINT32_MAX))
      return false;
    uint32_t memberCount = read32(definition + 20);
    uint32_t endpointCount = definitionIOCount(database, definition);
    for (uint32_t ordinal = 0; ordinal != read32(definition + 28); ++ordinal) {
      const uint8_t *group = database.data +
                             database.definitionMemberRelations +
                             uint64_t{read32(definition + 24) + ordinal} *
                                 kDefinitionMemberRelationSize;
      sharedMemberInventories.insert((uint64_t{packedSource} << 16) |
                                     read16(group));
    }
    if (firstMemberEndpoint > database.definitionMemberEndpointCount ||
        endpointCount >
            database.definitionMemberEndpointCount - firstMemberEndpoint)
      return false;
    if (recordVPIKind(source) != read16(definition))
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
    uint32_t endpointOrdinal = 0;
    for (uint32_t ordinal = 0; ordinal != memberCount; ++ordinal) {
      const uint8_t *member =
          database.data + database.definitionMembers +
          uint64_t{read32(definition + 16) + ordinal} * kDefinitionMemberSize;
      if (read16(member + 16) !=
          static_cast<uint16_t>(obelisk::reflection::VPIObjectKind::IODecl))
        continue;
      const uint8_t *endpoint =
          database.data + database.definitionMemberEndpoints +
          uint64_t{firstMemberEndpoint + endpointOrdinal++} *
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
    nextDefinitionMemberEndpoint += endpointCount;
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
      if (selector == 9 || selector == 15 || selector == 16 ||
          (primitive && selector == 33))
        return false;
      ++fixedPropertyIndex;
    }
    if (primitive)
      primitiveTypes.emplace(packedSource, read16(definition + 2));
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
    uint32_t endpointIndex =
        read32(binding + 12) +
        definitionIOCount(database, definition, memberIndex - firstMember);
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
  std::unordered_set<uint64_t> canonicalMethodObjects;
  std::unordered_set<uint64_t> statementTypedefObjects;
  std::unordered_map<uint64_t, uint32_t> typedefOwners;
  std::unordered_set<uint64_t> multipleTypedefOwners;
  std::unordered_map<uint32_t, uint32_t> frozenEnumOwners;
  std::unordered_map<uint32_t, uint32_t> frozenEnumInventories;
  std::unordered_map<uint32_t, uint64_t> unresolvedParameterParents;
  std::unordered_map<uint32_t, uint32_t> foreachNullOwners;
  std::unordered_map<uint32_t, uint32_t> foreachCollections, foreachSlotCounts;
  std::unordered_set<uint32_t> foreachIndexLocals;
  std::unordered_map<uint32_t, uint32_t> expressionParents;
  std::unordered_map<uint32_t, uint32_t> expressionOperandCounts;
  std::unordered_set<uint32_t> explicitClassAssignments;
  std::unordered_map<uint32_t, uint32_t> actualScopes, lexicalClassScopes;
  std::vector<std::pair<uint32_t, uint32_t>> expressionReferences;
  std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>>
      parameterPositions;
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
    // Canonical builtin identity must not name a typedef declaration, whose
    // independent source identity is encoded by its intrinsic inventory edge.
    if (iterate &&
        selector == static_cast<uint32_t>(
                        obelisk::reflection::VPIRelationKind::TypedefRel)) {
      const uint64_t targetOffset =
          static_cast<uint64_t>(target - database.data);
      uint32_t packedOwner = 0;
      if (!obelisk::reflection::tryPackTableIndex(sourceTable, sourceIndex,
                                                  packedOwner))
        return false;
      auto [owner, inserted] = typedefOwners.emplace(targetOffset, packedOwner);
      if (!inserted && owner->second != packedOwner)
        multipleTypedefOwners.insert(targetOffset);
      if (sourceTable == obelisk::reflection::TableKind::Statement) {
        if (!(read16(sourceRecord + 38) & OBELISK_RT_DESIGN_STATEMENT_SCOPE) ||
            read32(sourceRecord + 8) == UINT32_MAX ||
            targetTable != obelisk::reflection::TableKind::Object ||
            recordKind(target) != OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT ||
            !(read32(target + 4) & OBELISK_RT_DESIGN_CAP_NAMED_TYPESPEC) ||
            read64(target + 16) !=
                database.scopes +
                    uint64_t{read32(sourceRecord + 12)} * kScopeSize)
          return false;
        statementTypedefObjects.insert(
            static_cast<uint64_t>(target - database.data));
      }
      uint32_t packedTarget = 0;
      if (obelisk::reflection::tryPackTableIndex(targetTable, targetIndex,
                                                 packedTarget) &&
          canonicalBuiltinObjects.count(packedTarget))
        return false;
    }
    uint32_t sourceReference = 0;
    if (!obelisk::reflection::tryPackTableIndex(sourceTable, sourceIndex,
                                                sourceReference))
      return false;
    if (!iterate &&
        sourceKind == static_cast<uint32_t>(
                          obelisk::reflection::VPIObjectKind::EnumConst) &&
        selector == static_cast<uint32_t>(
                        obelisk::reflection::VPIObjectKind::EnumTypespec) &&
        frozenEnumConstants.count(sourceReference)) {
      uint32_t sourceRoot = 0, targetRoot = 0;
      if (!findSemanticRootBinding(database, sourceReference, sourceRoot) ||
          !findSemanticRootBinding(database, packedTarget, targetRoot) ||
          sourceRoot != targetRoot ||
          !frozenEnumOwners.emplace(sourceReference, packedTarget).second)
        return false;
    }
    if (iterate &&
        sourceKind == static_cast<uint32_t>(
                          obelisk::reflection::VPIObjectKind::EnumTypespec) &&
        selector == static_cast<uint32_t>(
                        obelisk::reflection::VPIObjectKind::EnumConst) &&
        frozenEnumConstants.count(packedTarget)) {
      if (!frozenEnumInventories.emplace(packedTarget, sourceReference).second)
        return false;
    }
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
    bool classParameterInventory =
        iterate &&
        sourceKind == static_cast<uint32_t>(
                          obelisk::reflection::VPIObjectKind::ClassTypespec) &&
        selector == static_cast<uint32_t>(
                        obelisk::reflection::VPIObjectKind::Parameter);
    bool lexicalParameterReference =
        classParameterInventory &&
        sourceTable == obelisk::reflection::TableKind::Object &&
        (read32(sourceRecord + 4) &
         OBELISK_RT_DESIGN_CAP_LEXICAL_CLASS_TYPESPEC) != 0;
    bool lexicalForward =
        !lexicalParameterReference &&
        (selector == targetKind || classParameterInventory ||
         edge->automaticRelation ==
             obelisk::reflection::VPIAutomaticRelation::DirectChild);
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

    uint32_t sourceEndpoint = relationEndpoint(sourceTable, sourceIndex);
    if (!iterate &&
        selector == uint32_t(obelisk::reflection::VPIRelationKind::ReturnRel)) {
      if (sourceTable != obelisk::reflection::TableKind::Object ||
          recordKind(sourceRecord) != OBELISK_RT_DESIGN_RECORD_FUNCTION ||
          (read32(sourceRecord + 4) & OBELISK_RT_DESIGN_CAP_INTERNAL) ||
          voidFunctions.count(sourceEndpoint) ||
          (!frameLocals.count(packedTarget) &&
           !proceduralStaticStorage.count(packedTarget)) ||
          fixedNames.find(sourceEndpoint) == fixedNames.end() ||
          fixedNames.find(packedTarget) == fixedNames.end() ||
          fixedNames.at(sourceEndpoint) != fixedNames.at(packedTarget) ||
          !functionReturns.emplace(packedTarget, sourceEndpoint).second)
        return false;
    }
    if (unresolvedTypeAliases.count(packedTarget)) {
      if (iterate &&
          selector == static_cast<uint32_t>(
                          obelisk::reflection::VPIObjectKind::Parameter))
        return false;
      if (iterate &&
          selector == static_cast<uint32_t>(
                          obelisk::reflection::VPIRelationKind::TypedefRel)) {
        if (sourceKind != static_cast<uint32_t>(
                              obelisk::reflection::VPIObjectKind::ClassDefn) ||
            !unresolvedAliasOwners.emplace(packedTarget, sourceEndpoint).second)
          return false;
      }
    }
    if (unresolvedTypeAliases.count(sourceEndpoint)) {
      if (iterate)
        return false;
      if (selector ==
          static_cast<uint32_t>(
              obelisk::reflection::VPIRelationKind::TypedefAliasRel)) {
        if ((!unresolvedTypeAliases.count(packedTarget) &&
             !unresolvedTypeParameters.count(packedTarget)) ||
            !unresolvedAliasTargets.emplace(sourceEndpoint, packedTarget)
                 .second)
          return false;
      } else if (selector ==
                 static_cast<uint32_t>(
                     obelisk::reflection::VPIRelationKind::ScopeRel)) {
        uint64_t ownerScope =
            targetTable == obelisk::reflection::TableKind::StaticObject
                ? database.scopes + uint64_t{read32(target + 8)} * kScopeSize
                : read64(target + 16);
        if (targetKind != static_cast<uint32_t>(
                              obelisk::reflection::VPIObjectKind::ClassDefn) ||
            read64(sourceRecord + 16) != ownerScope ||
            !unresolvedAliasScopes.emplace(sourceEndpoint, packedTarget).second)
          return false;
      } else {
        return false;
      }
    }

    if (iterate &&
        (lexicalForward ||
         selector == static_cast<uint32_t>(
                         obelisk::reflection::VPIRelationKind::VariablesRel)) &&
        sourceClassProperties.count(packedTarget)) {
      if (sourceKind !=
          static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::ClassDefn))
        return false;
      auto [entry, inserted] =
          sourceClassPropertyOwners.emplace(packedTarget, sourceEndpoint);
      if (!inserted && entry->second != sourceEndpoint)
        return false;
    }
    if (iterate &&
        (lexicalForward ||
         selector == static_cast<uint32_t>(
                         obelisk::reflection::VPIRelationKind::VariablesRel)) &&
        proceduralStaticStorage.count(packedTarget)) {
      auto [owner, inserted] =
          proceduralStaticOwners.emplace(packedTarget, sourceEndpoint);
      if (!inserted && owner->second != sourceEndpoint)
        return false;
    }
    if (!iterate && proceduralStaticStorage.count(sourceEndpoint) &&
        selector == static_cast<uint32_t>(
                        obelisk::reflection::VPIRelationKind::ScopeRel)) {
      uint64_t physicalScope = 0;
      if (targetTable == obelisk::reflection::TableKind::Statement) {
        if (!(read16(target + 38) & OBELISK_RT_DESIGN_STATEMENT_SCOPE) ||
            read32(target + 8) >= database.objectCount)
          return false;
        const uint8_t *unit = database.data + database.objects +
                              uint64_t{read32(target + 8)} * kObjectSize;
        if ((recordKind(unit) != OBELISK_RT_DESIGN_RECORD_PROCESS &&
             recordKind(unit) != OBELISK_RT_DESIGN_RECORD_FUNCTION) ||
            (read32(unit + 4) & OBELISK_RT_DESIGN_CAP_INTERNAL))
          return false;
        physicalScope =
            database.scopes + uint64_t{read32(target + 12)} * kScopeSize;
      } else if (targetTable == obelisk::reflection::TableKind::Object &&
                 (recordKind(target) == OBELISK_RT_DESIGN_RECORD_PROCESS ||
                  recordKind(target) == OBELISK_RT_DESIGN_RECORD_FUNCTION) &&
                 (targetKind == static_cast<uint32_t>(
                                    obelisk::reflection::VPIObjectKind::Task) ||
                  targetKind ==
                      static_cast<uint32_t>(
                          obelisk::reflection::VPIObjectKind::Function))) {
        physicalScope = read64(target + 16);
      } else {
        return false;
      }
      auto name = fixedNames.find(sourceEndpoint);
      if ((targetTable == obelisk::reflection::TableKind::Object &&
           (read32(target + 4) & OBELISK_RT_DESIGN_CAP_INTERNAL)) ||
          read64(sourceRecord + 16) != physicalScope ||
          name == fixedNames.end() || name->second.empty() ||
          !localNames[packedTarget].insert(name->second).second ||
          !proceduralStaticScopes.emplace(sourceEndpoint, packedTarget).second)
        return false;
    }
    const bool foreachOwner =
        sourceTable == obelisk::reflection::TableKind::Statement &&
        sourceKind == static_cast<uint32_t>(
                          obelisk::reflection::VPIObjectKind::ForeachStmt);
    const bool foreachIndex =
        foreachOwner && iterate &&
        selector == static_cast<uint32_t>(
                        obelisk::reflection::VPIRelationKind::LoopVarsRel);
    if (foreachNulls.count(sourceEndpoint))
      return false;
    if (foreachNulls.count(packedTarget)) {
      if (!foreachIndex || read32(target + 8) != read32(sourceRecord + 12) ||
          !foreachNullOwners.emplace(packedTarget, sourceEndpoint).second)
        return false;
    }
    if (foreachOwner && iterate &&
        selector == static_cast<uint32_t>(
                        obelisk::reflection::VPIRelationKind::VariablesRel)) {
      if (ordinal ||
          !foreachCollections.emplace(sourceEndpoint, packedTarget).second)
        return false;
    }
    if (foreachIndex) {
      ++foreachSlotCounts[sourceEndpoint];
      if (!foreachNulls.count(packedTarget) &&
          (!frameLocals.count(packedTarget) ||
           targetKind != static_cast<uint32_t>(
                             obelisk::reflection::VPIObjectKind::IntVar) ||
           !foreachIndexLocals.insert(packedTarget).second))
        return false;
    }
    if (iterate &&
        (lexicalForward || foreachIndex ||
         (!foreachOwner &&
          selector ==
              static_cast<uint32_t>(
                  obelisk::reflection::VPIRelationKind::VariablesRel))) &&
        frameLocals.count(packedTarget)) {
      if (frameLocalOwners.at(packedTarget) != sourceEndpoint)
        return false;
      frameLocalForward.insert(packedTarget);
    }
    if (!iterate && frameLocals.count(sourceEndpoint) &&
        selector == static_cast<uint32_t>(
                        obelisk::reflection::VPIRelationKind::ScopeRel)) {
      if (frameLocalOwners.at(sourceEndpoint) != packedTarget)
        return false;
      frameLocalReverse.insert(sourceEndpoint);
    }
    if (!iterate && sourceClassProperties.count(sourceEndpoint) &&
        selector == static_cast<uint32_t>(
                        obelisk::reflection::VPIRelationKind::ScopeRel)) {
      if (targetKind !=
          static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::ClassDefn))
        return false;
      sourceClassPropertyScopes.emplace(sourceEndpoint, packedTarget);
    }
    if (!iterate &&
        selector == static_cast<uint32_t>(
                        obelisk::reflection::VPIRelationKind::ScopeRel)) {
      auto callOwner = callOwners.find(sourceEndpoint);
      if (callOwner != callOwners.end()) {
        const uint8_t *owner = database.data + database.objects +
                               uint64_t{callOwner->second} * kObjectSize;
        uint32_t initializer = 0;
        if (findInitializerSource(database, read64(owner + 8), initializer)) {
          if (initializerSourceScope(database, initializer) != packedTarget)
            return false;
        } else
          switch (targetTable) {
          case obelisk::reflection::TableKind::Statement:
            if (read32(target + 8) != callOwner->second ||
                !(read16(target + 38) & OBELISK_RT_DESIGN_STATEMENT_SCOPE))
              return false;
            break;
          case obelisk::reflection::TableKind::Object:
          case obelisk::reflection::TableKind::StaticObject: {
            uint32_t source = 0;
            uint64_t sourceOffset = 0;
            if (!findCallableSource(database, callOwner->second, source,
                                    sourceOffset) ||
                source != packedTarget)
              return false;
            break;
          }
          case obelisk::reflection::TableKind::Scope: {
            const auto *ownerKind =
                obelisk::reflection::findVPIObjectKind(recordVPIKind(owner));
            uint32_t source = 0;
            uint64_t sourceOffset = 0;
            if (findCallableSource(database, callOwner->second, source,
                                   sourceOffset) ||
                read64(owner + 16) != uint64_t(target - database.data) ||
                (ownerKind &&
                 (ownerKind->families &
                  obelisk::reflection::vpiFamilyMask(
                      obelisk::reflection::VPIObjectFamily::Scope))))
              return false;
            break;
          }
          }
        callScopes.insert(sourceEndpoint);
      }
    }
    if (!iterate &&
        selector == static_cast<uint32_t>(
                        obelisk::reflection::VPIRelationKind::ExprRel) &&
        unavailableParameterDefaults.count(sourceEndpoint) != 0)
      return false;
    if (iterate && !lexicalParameterReference &&
        selector == static_cast<uint32_t>(
                        obelisk::reflection::VPIObjectKind::Parameter) &&
        targetKind == static_cast<uint32_t>(
                          obelisk::reflection::VPIObjectKind::Parameter)) {
      if (!parameterPositions
               .emplace(packedTarget, std::make_pair(sourceEndpoint, ordinal))
               .second)
        return false;
    }
    auto primitiveKind = [](uint32_t kind) {
      using K = obelisk::reflection::VPIObjectKind;
      return kind == static_cast<uint32_t>(K::Gate) ||
             kind == static_cast<uint32_t>(K::Switch) ||
             kind == static_cast<uint32_t>(K::Udp);
    };
    if (iterate && primitiveKind(sourceKind) &&
        selector == static_cast<uint32_t>(
                        obelisk::reflection::VPIObjectKind::PrimTerm)) {
      if (!primitiveTerminals.count(packedTarget) ||
          sourceTable != obelisk::reflection::TableKind::Object ||
          recordKind(sourceRecord) != OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT ||
          read64(sourceRecord + 16) !=
              (targetTable == obelisk::reflection::TableKind::StaticObject
                   ? database.scopes + uint64_t{read32(target + 8)} * kScopeSize
                   : read64(target + 16)) ||
          ordinal > INT32_MAX ||
          !terminalOwners.emplace(packedTarget, sourceEndpoint).second)
        return false;
    }
    if (!iterate && primitiveTerminals.count(sourceEndpoint) &&
        selector == static_cast<uint32_t>(
                        obelisk::reflection::VPIRelationKind::PrimitiveRel)) {
      if (!primitiveKind(targetKind) ||
          targetTable != obelisk::reflection::TableKind::Object ||
          recordKind(target) != OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT ||
          (sourceTable == obelisk::reflection::TableKind::StaticObject
               ? database.scopes +
                     uint64_t{read32(sourceRecord + 8)} * kScopeSize
               : read64(sourceRecord + 16)) != read64(target + 16) ||
          !terminalPrimitives.emplace(sourceEndpoint, packedTarget).second)
        return false;
    }
    bool sourceExpression = sourceExpressionObjects.count(sourceEndpoint) != 0;
    bool targetExpression = sourceExpressionObjects.count(packedTarget) != 0;
    using Kind = obelisk::reflection::VPIObjectKind;
    using Relation = obelisk::reflection::VPIRelationKind;
    bool explicitClassAssignment =
        sourceKind == static_cast<uint32_t>(Kind::ParamAssign) &&
        sourceTable == obelisk::reflection::TableKind::Object &&
        recordKind(sourceRecord) == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT &&
        (read32(sourceRecord + 4) &
         OBELISK_RT_DESIGN_CAP_EXPLICIT_CLASS_PARAMETER_ASSIGNMENT);
    if (explicitClassAssignment) {
      explicitClassAssignments.insert(sourceEndpoint);
      if (!iterate && selector == static_cast<uint32_t>(Relation::ScopeRel))
        actualScopes.emplace(sourceEndpoint, packedTarget);
    }
    if (!iterate && selector == static_cast<uint32_t>(Relation::ScopeRel) &&
        sourceKind == static_cast<uint32_t>(Kind::ClassTypespec) &&
        sourceTable == obelisk::reflection::TableKind::Object &&
        (read32(sourceRecord + 4) &
         OBELISK_RT_DESIGN_CAP_LEXICAL_CLASS_TYPESPEC))
      lexicalClassScopes.emplace(sourceEndpoint, packedTarget);
    bool sharedClassDefault =
        !iterate && selector == static_cast<uint32_t>(Relation::RhsRel) &&
        sourceKind == static_cast<uint32_t>(Kind::ParamAssign) &&
        sourceTable == obelisk::reflection::TableKind::Object &&
        recordKind(sourceRecord) == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT &&
        (read32(sourceRecord + 4) &
         OBELISK_RT_DESIGN_CAP_DEFAULT_CLASS_PARAMETER_ASSIGNMENT);
    if (targetExpression && !sharedClassDefault) {
      bool defaultRoot = !iterate &&
                         selector == static_cast<uint32_t>(Relation::ExprRel) &&
                         sourceKind == static_cast<uint32_t>(Kind::Parameter);
      bool operand = iterate &&
                     selector == static_cast<uint32_t>(Relation::OperandRel) &&
                     sourceExpression &&
                     sourceKind == static_cast<uint32_t>(Kind::Operation);
      bool actualRoot = !iterate && explicitClassAssignment &&
                        selector == static_cast<uint32_t>(Relation::RhsRel);
      if ((!defaultRoot && !operand && !actualRoot) ||
          !expressionParents.emplace(packedTarget, sourceEndpoint).second)
        return false;
      uint64_t ownerScope =
          sourceTable == obelisk::reflection::TableKind::StaticObject
              ? database.scopes +
                    uint64_t{read32(sourceRecord + 8)} * kScopeSize
              : read64(sourceRecord + 16);
      if (read64(target + 16) != ownerScope)
        return false;
    }
    bool parameterLeaf = targetKind == static_cast<uint32_t>(Kind::Parameter);
    if (sourceExpression) {
      if (!iterate || selector != static_cast<uint32_t>(Relation::OperandRel) ||
          sourceKind != static_cast<uint32_t>(Kind::Operation) ||
          (!targetExpression && !parameterLeaf))
        return false;
      ++expressionOperandCounts[sourceEndpoint];
      if (parameterLeaf)
        expressionReferences.emplace_back(sourceEndpoint, packedTarget);
    } else if (!iterate &&
               selector == static_cast<uint32_t>(Relation::ExprRel) &&
               sourceKind == static_cast<uint32_t>(Kind::Parameter) &&
               parameterLeaf) {
      expressionReferences.emplace_back(sourceEndpoint, packedTarget);
    }
    if ((unresolvedTypeParameters.count(sourceEndpoint) != 0 ||
         unresolvedValueParameters.count(sourceEndpoint) != 0) &&
        !iterate) {
      if (selector == static_cast<uint32_t>(
                          obelisk::reflection::VPIObjectKind::Typespec) &&
          (unresolvedValueParameters.count(sourceEndpoint) != 0 ||
           packedTarget != sourceEndpoint))
        return false;
      if (selector == static_cast<uint32_t>(
                          obelisk::reflection::VPIRelationKind::ScopeRel)) {
        if (targetKind != static_cast<uint32_t>(
                              obelisk::reflection::VPIObjectKind::ClassDefn))
          return false;
        unresolvedParameterParents.emplace(
            sourceEndpoint, lexicalPair(packedTarget, sourceEndpoint));
      }
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
    if (sharedMemberInventories.count(
            (uint64_t{relationEndpoint(sourceTable, sourceIndex)} << 16) |
            selector))
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
        // Canonical methods share their display name with the lexical
        // declaration. The validated specialization owner keeps this
        // executable view reachable without a second name-index entry.
        if ((read32(sourceRecord + 4) &
             OBELISK_RT_DESIGN_CAP_LEXICAL_ANCHOR) != 0 &&
            (sourceKind == static_cast<uint32_t>(
                               obelisk::reflection::VPIObjectKind::Task) ||
             sourceKind == static_cast<uint32_t>(
                               obelisk::reflection::VPIObjectKind::Function)) &&
            canonicalClassRoots.count(packedTarget) != 0)
          canonicalMethodObjects.insert(sourceOffset);
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
  if (!automaticGroupComplete() || callScopes.size() != callOwners.size())
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
  if (!std::all_of(unresolvedTypeParameters.begin(),
                   unresolvedTypeParameters.end(), [&](uint32_t parameter) {
                     auto parent = unresolvedParameterParents.find(parameter);
                     return parent != unresolvedParameterParents.end() &&
                            staticLexicalChildren.count(parent->second) != 0 &&
                            staticLexicalParents.count(parent->second) != 0;
                   }))
    return false;
  if (!std::all_of(unresolvedValueParameters.begin(),
                   unresolvedValueParameters.end(), [&](uint32_t parameter) {
                     auto parent = unresolvedParameterParents.find(parameter);
                     return parent != unresolvedParameterParents.end() &&
                            staticLexicalChildren.count(parent->second) != 0 &&
                            staticLexicalParents.count(parent->second) != 0;
                   }))
    return false;
  for (uint32_t storage : proceduralStaticStorage) {
    auto owner = proceduralStaticOwners.find(storage);
    auto scope = proceduralStaticScopes.find(storage);
    if (owner == proceduralStaticOwners.end() ||
        scope == proceduralStaticScopes.end() || owner->second != scope->second)
      return false;
  }
  if (foreachNullOwners.size() != foreachNulls.size() ||
      foreachCollections.size() != foreachSlotCounts.size())
    return false;
  for (const auto &[variable, function] : functionReturns) {
    auto automatic = frameLocalOwners.find(variable);
    auto persistent = proceduralStaticScopes.find(variable);
    if ((automatic != frameLocalOwners.end() &&
         automatic->second != function) ||
        (automatic == frameLocalOwners.end() &&
         (persistent == proceduralStaticScopes.end() ||
          persistent->second != function)))
      return false;
  }
  for (const auto &[owner, count] : foreachSlotCounts) {
    auto collection = foreachCollections.find(owner);
    uint32_t semanticIndex = 0;
    if (collection == foreachCollections.end() ||
        !findSemanticRootBinding(database, collection->second, semanticIndex))
      return false;
    uint32_t dimensions = 0;
    while (true) {
      const uint8_t *semantic = database.data + database.semanticTypes +
                                uint64_t{semanticIndex} * kSemanticTypeSize;
      uint32_t kind = read32(semantic) & UINT32_C(0xff);
      if (kind != OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY &&
          kind != OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY)
        break;
      if (read32(semantic + 8) != 1 ||
          !(read32(semantic) & OBELISK_RT_DESIGN_SEMANTIC_HAS_RANGE))
        return false;
      ++dimensions;
      const uint8_t *child =
          database.data + database.semanticTypeEdges +
          uint64_t{read32(semantic + 4)} * kSemanticTypeEdgeSize;
      semanticIndex = read32(child);
    }
    if (!count || count > dimensions)
      return false;
  }
  for (uint32_t local : frameLocals) {
    if (!frameLocalForward.count(local) || boundClassFields.count(local) ||
        boundEvents.count(local) || eventArrayStorage.count(local))
      return false;
    uint64_t localOffset = 0, scopeOffset = 0;
    if (!packedObjectReferenceOffset(database, local, localOffset) ||
        !packedFrameLocalScopeOffset(database, frameLocalOwners.at(local),
                                     scopeOffset))
      return false;
    const auto *scopeEdge = obelisk::reflection::findVPITraversal(
        objectReferenceVPIKind(database, localOffset),
        static_cast<uint32_t>(obelisk::reflection::VPIRelationKind::ScopeRel),
        obelisk::reflection::VPITraversalMode::Handle);
    if (scopeEdge &&
        obelisk::reflection::vpiObjectSetContains(
            scopeEdge->targets,
            isStatementOffset(database, scopeOffset)
                ? read16(database.data + scopeOffset + 36)
                : objectReferenceVPIKind(database, scopeOffset)) &&
        !frameLocalReverse.count(local))
      return false;
  }
  for (uint32_t property : sourceClassProperties) {
    auto owner = sourceClassPropertyOwners.find(property);
    if (owner == sourceClassPropertyOwners.end() ||
        boundClassFields.count(property) || boundEvents.count(property) ||
        eventArrayStorage.count(property))
      return false;
    uint64_t offset = 0;
    if (!packedObjectReferenceOffset(database, property, offset))
      return false;
    const auto *scopeEdge = obelisk::reflection::findVPITraversal(
        objectReferenceVPIKind(database, offset),
        static_cast<uint32_t>(obelisk::reflection::VPIRelationKind::ScopeRel),
        obelisk::reflection::VPITraversalMode::Handle);
    if (scopeEdge && obelisk::reflection::vpiObjectSetContains(
                         scopeEdge->targets,
                         static_cast<uint32_t>(
                             obelisk::reflection::VPIObjectKind::ClassDefn))) {
      auto scope = sourceClassPropertyScopes.find(property);
      if (scope == sourceClassPropertyScopes.end() ||
          scope->second != owner->second)
        return false;
    }
  }
  std::unordered_map<uint32_t, uint64_t> terminalInputCounts;
  for (uint32_t terminal : primitiveTerminals) {
    auto owner = terminalOwners.find(terminal);
    auto reverse = terminalPrimitives.find(terminal);
    if (owner == terminalOwners.end() || reverse == terminalPrimitives.end() ||
        owner->second != reverse->second)
      return false;
    terminalInputCounts[owner->second] += terminalDirections.at(terminal) == 1;
  }
  for (auto [owner, count] : terminalInputCounts) {
    uint64_t offset = 0;
    if (!packedObjectReferenceOffset(database, owner, offset) ||
        read64(database.data + offset + 56) != count)
      return false;
  }
  // Bindings select the primitive's own immutable Driver records. Their
  // execution spans and complementary-bank pairing are checked again by the
  // bytecode inventory helper when a value is read.
  uint32_t previousTerminal = 0;
  for (uint64_t index = 0; index != database.terminalDriverBindingCount;
       ++index) {
    obelisk::reflection::TerminalDriverBindingView binding(
        database.data + database.terminalDriverBindings +
        index * kTerminalDriverBindingSize);
    uint32_t terminal = binding.getTerminalIndexAndTable();
    if ((index && terminal <= previousTerminal) ||
        !primitiveTerminals.count(terminal) ||
        terminalDirections.at(terminal) != 2)
      return false;
    previousTerminal = terminal;
    uint64_t primitiveOffset = 0;
    if (!packedObjectReferenceOffset(database, terminalOwners.at(terminal),
                                     primitiveOffset))
      return false;
    using Kind = obelisk::reflection::VPIObjectKind;
    uint32_t kind = objectReferenceVPIKind(database, primitiveOffset);
    if (kind != uint32_t(Kind::Gate) && kind != uint32_t(Kind::Udp))
      return false;
    uint32_t primitiveType = 0;
    if (kind == uint32_t(Kind::Gate)) {
      auto retained = primitiveTypes.find(terminalOwners.at(terminal));
      if (retained == primitiveTypes.end())
        return false;
      primitiveType = retained->second;
      const auto *descriptor =
          obelisk::reflection::findVPIBuiltinPrimitive(primitiveType);
      if (!descriptor || descriptor->objectKind != uint32_t(Kind::Gate))
        return false;
    }
    bool conditional = primitiveType >= 9 && primitiveType <= 12;
    if (conditional != (binding.getHighDriverIndex() != UINT32_MAX))
      return false;
    uint64_t scope = read64(database.data + primitiveOffset + 16);
    auto driver = [&](uint32_t object) -> const uint8_t * {
      if (object >= database.objectCount)
        return nullptr;
      const uint8_t *record =
          database.data + database.objects + uint64_t{object} * kObjectSize;
      uint64_t type = read64(record + 48);
      if (recordKind(record) != OBELISK_RT_DESIGN_RECORD_DRIVER ||
          read64(record + 16) != scope || !isTypeOffset(database, type) ||
          binding.getBitOffset() >= read64(record + 56))
        return nullptr;
      const uint8_t *physical = database.data + type;
      uint32_t encodedType = read32(physical + 4);
      if ((encodedType & UINT32_C(0xff)) != OBELISK_RT_DESIGN_TYPE_SCALAR &&
          !((encodedType >> 8) & OBELISK_RT_DESIGN_TYPE_PACKED))
        return nullptr;
      return record;
    };
    const uint8_t *low = driver(binding.getLowDriverIndex());
    if (!low)
      return false;
    uint32_t highIndex = binding.getHighDriverIndex();
    if (highIndex != UINT32_MAX) {
      const uint8_t *high = driver(highIndex);
      if (!high || highIndex == binding.getLowDriverIndex() ||
          read64(low + 48) != read64(high + 48) ||
          read64(low + 56) != read64(high + 56))
        return false;
    }
  }
  // Rootless typedefs retain a distinct identity and terminate at an unresolved
  // parameter of the same lexical class. Memoized iterative walks reject cycles
  // without recursively consuming the host stack on an untrusted image.
  std::unordered_set<uint32_t> provenUnresolvedAliases;
  for (uint32_t alias : unresolvedTypeAliases) {
    std::vector<uint32_t> path;
    std::unordered_set<uint32_t> visiting;
    uint32_t cursor = alias;
    auto owner = unresolvedAliasOwners.find(alias);
    if (owner == unresolvedAliasOwners.end())
      return false;
    while (unresolvedTypeAliases.count(cursor)) {
      auto aliasOwner = unresolvedAliasOwners.find(cursor);
      auto scope = unresolvedAliasScopes.find(cursor);
      auto target = unresolvedAliasTargets.find(cursor);
      if (aliasOwner == unresolvedAliasOwners.end() ||
          aliasOwner->second != owner->second ||
          scope == unresolvedAliasScopes.end() ||
          scope->second != owner->second ||
          target == unresolvedAliasTargets.end())
        return false;
      if (provenUnresolvedAliases.count(cursor))
        break;
      if (!visiting.insert(cursor).second)
        return false;
      path.push_back(cursor);
      cursor = target->second;
    }
    if (!unresolvedTypeAliases.count(cursor)) {
      auto parent = unresolvedParameterParents.find(cursor);
      if (!unresolvedTypeParameters.count(cursor) ||
          parent == unresolvedParameterParents.end() ||
          parent->second != lexicalPair(owner->second, cursor))
        return false;
    }
    provenUnresolvedAliases.insert(path.begin(), path.end());
  }
  // Each occurrence belongs to exactly one acyclic tree rooted in a precise
  // parameter default. Cache roots while following parent links so shared
  // suffixes are checked once, including maliciously deep images.
  std::unordered_map<uint32_t, uint32_t> expressionRoots;
  for (uint32_t expression : sourceExpressionObjects) {
    std::vector<uint32_t> path;
    std::unordered_set<uint32_t> visiting;
    uint32_t cursor = expression;
    uint32_t root = 0;
    while (true) {
      auto known = expressionRoots.find(cursor);
      if (known != expressionRoots.end()) {
        root = known->second;
        break;
      }
      if (!visiting.insert(cursor).second)
        return false;
      path.push_back(cursor);
      auto parent = expressionParents.find(cursor);
      if (parent == expressionParents.end())
        return false;
      cursor = parent->second;
      if (sourceExpressionObjects.count(cursor) == 0) {
        if (parameterPositions.count(cursor) == 0 &&
            explicitClassAssignments.count(cursor) == 0)
          return false;
        root = cursor;
        break;
      }
    }
    for (uint32_t node : path)
      expressionRoots.emplace(node, root);
  }
  for (const auto &[expression, operation] : sourceExpressionOperators) {
    const auto *descriptor =
        obelisk::reflection::findVPISourceOperator(operation);
    if (!descriptor || expressionOperandCounts[expression] != descriptor->arity)
      return false;
  }
  for (const auto &[source, reference] : expressionReferences) {
    auto known = expressionRoots.find(source);
    uint32_t root = known == expressionRoots.end() ? source : known->second;
    auto declaration = parameterPositions.find(root);
    auto target = parameterPositions.find(reference);
    if (explicitClassAssignments.count(root)) {
      auto actualScope = actualScopes.find(root);
      auto lexicalScope = actualScope == actualScopes.end()
                              ? lexicalClassScopes.end()
                              : lexicalClassScopes.find(actualScope->second);
      if (lexicalScope == lexicalClassScopes.end() ||
          target == parameterPositions.end() ||
          target->second.first != lexicalScope->second ||
          namedParameters.count(reference) == 0)
        return false;
      for (uint64_t index = 0; index != database.fixedPropertyCount; ++index) {
        const uint8_t *property = database.data + database.fixedProperties +
                                  index * kFixedPropertySize;
        if (read32(property) == reference && read16(property + 4) == 70 &&
            read64(property + 8))
          return false;
      }
      continue;
    }
    if (declaration == parameterPositions.end() ||
        target == parameterPositions.end() ||
        declaration->second.first != target->second.first ||
        target->second.second >= declaration->second.second ||
        namedParameters.count(reference) == 0)
      return false;
  }
  if (!std::all_of(classMemberParents.begin(), classMemberParents.end(),
                   [&](uint64_t member) {
                     return staticLexicalParents.count(member) != 0;
                   }))
    return false;
  if (!std::all_of(modportParents.begin(), modportParents.end(),
                   [&](uint64_t modport) {
                     return staticLexicalChildren.count(modport) != 0 &&
                            staticLexicalParents.count(modport) != 0;
                   }))
    return false;
  if (haveStatementContainment &&
      std::any_of(incomingRelations.begin(), incomingRelations.end(),
                  [](const IncomingRelation &incoming) {
                    return incoming.modeMask == 0;
                  }))
    return false;

  std::unordered_set<uint32_t> checkedEventArrays;
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
    uint32_t packedArray = 0;
    if (!obelisk::reflection::tryPackTableIndex(
            obelisk::reflection::TableKind::Object, objectIndex, packedArray))
      return false;
    auto eventStorage = eventArrayStorage.find(packedArray);
    uint64_t eventPhysical = 0;
    uint32_t eventSemantic = UINT32_MAX, storageSemantic = UINT32_MAX;
    if (eventStorage != eventArrayStorage.end()) {
      uint32_t packedStorage = 0;
      if (!packedObjectReferenceForCursor(database, eventStorage->second,
                                          packedStorage) ||
          !findSemanticRootBinding(database, packedArray, eventSemantic) ||
          !findSemanticRootBinding(database, packedStorage, storageSemantic))
        return false;
      eventPhysical = read64(database.data + eventStorage->second + 48);
      checkedEventArrays.insert(packedArray);
    }
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
      if (eventStorage != eventArrayStorage.end()) {
        const uint8_t *physical = database.data + eventPhysical;
        const uint8_t *semantic = semanticRecord(eventSemantic);
        const uint8_t *stored = semanticRecord(storageSemantic);
        if (read32(physical + 4) != OBELISK_RT_DESIGN_TYPE_ARRAY ||
            readI64(physical + 16) != left || readI64(physical + 24) != right ||
            (read32(semantic) & UINT32_C(0xff)) !=
                OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY ||
            (read32(stored) & UINT32_C(0xff)) !=
                OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY ||
            readI64(semantic + 32) != left || readI64(semantic + 40) != right ||
            readI64(stored + 32) != left || readI64(stored + 40) != right)
          return false;
        // The physical type validator proves each dimension has an integral
        // stride that contains its child; padding is preserved, not guessed.
        eventPhysical = read64(physical + 32);
        eventSemantic =
            read32(database.data + database.semanticTypeEdges +
                   uint64_t{read32(semantic + 4)} * kSemanticTypeEdgeSize);
        storageSemantic =
            read32(database.data + database.semanticTypeEdges +
                   uint64_t{read32(stored + 4)} * kSemanticTypeEdgeSize);
      }
      if (dimension == 0 &&
          (readI64(object + 64) != left || readI64(object + 72) != right))
        return false;
    }
    if (product != elementCount)
      return false;
    if (eventStorage != eventArrayStorage.end()) {
      const uint8_t *physical = database.data + eventPhysical;
      if (read32(physical + 4) != OBELISK_RT_DESIGN_TYPE_SCALAR ||
          read64(physical + 8) != 64 ||
          (read32(semanticRecord(eventSemantic)) & UINT32_C(0xff)) !=
              OBELISK_RT_DESIGN_SEMANTIC_EVENT ||
          (read32(semanticRecord(storageSemantic)) & UINT32_C(0xff)) !=
              OBELISK_RT_DESIGN_SEMANTIC_EVENT)
        return false;
    }
  }
  if (checkedEventArrays.size() != eventArrayStorage.size())
    return false;
  for (uint32_t index = 0; index != database.objectCount; ++index) {
    const uint8_t *object =
        database.data + database.objects + uint64_t{index} * kObjectSize;
    const auto *access =
        obelisk::reflection::findVPIIndexedAccess(recordVPIKind(object));
    // A typed class event array uses its field's physical array shape and
    // receiver binding. Lexical event arrays instead own relation-index
    // members; the two representations cannot describe the same object.
    bool classEventArray =
        recordVPIKind(object) == static_cast<uint32_t>(
                                    obelisk::reflection::VPIObjectKind::
                                        NamedEventArray) &&
        boundClassFields.count(relationEndpoint(
            obelisk::reflection::TableKind::Object, index)) != 0;
    bool requiresIndex =
        recordKind(object) == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT &&
        !classEventArray &&
        !sourceClassProperties.count(
            relationEndpoint(obelisk::reflection::TableKind::Object, index)) &&
        !frameLocals.count(
            relationEndpoint(obelisk::reflection::TableKind::Object, index)) &&
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
    if (eventArrayStorage.count(packedArray) != 0 &&
        boundEvents.count(packedTarget) != 0)
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
      if (kind == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT &&
          (read32(record + 4) & OBELISK_RT_DESIGN_CAP_FRAME_LOCAL))
        return false;
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
  for (uint64_t target : statementTypedefObjects)
    if (multipleTypedefOwners.count(target))
      return false;
  if (frozenEnumOwners.size() != frozenEnumConstants.size() ||
      frozenEnumInventories != frozenEnumOwners)
    return false;
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
    // typespecs and interface-type variants). A statically allocated class
    // field is indexed by its member declaration; its validated backing
    // storage must not create a second name-index identity. Statement-owned
    // typedefs likewise use their exact lexical scope; unnamed blocks may
    // share their enclosing function's display hierarchy.
    if ((caps & OBELISK_RT_DESIGN_CAP_INTERNAL) != 0 ||
        classBackingStorage.count(offset) != 0 ||
        eventBackingStorage.count(offset) != 0 ||
        canonicalMethodObjects.count(offset) != 0 ||
        statementTypedefObjects.count(offset) != 0 ||
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
  return validateClassCallDispatch(database) &&
         validateCodeUnitScopes(database, fixedNames) &&
         validateInitializers(database) &&
         validateInterfaceCallDispatch(database, fixedNames) &&
         validateLexicalInheritance(database, fixedNames);
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
         left.classTypeBindings == right.classTypeBindings &&
         left.classTypeBindingCount == right.classTypeBindingCount &&
         left.semanticClassBindings == right.semanticClassBindings &&
         left.semanticClassBindingCount == right.semanticClassBindingCount &&
         left.classMemberBindings == right.classMemberBindings &&
         left.classMemberBindingCount == right.classMemberBindingCount &&
         left.modportBindings == right.modportBindings &&
         left.modportBindingCount == right.modportBindingCount &&
         left.mailboxElementBindings == right.mailboxElementBindings &&
         left.mailboxElementBindingCount == right.mailboxElementBindingCount &&
         left.boxTypeBindings == right.boxTypeBindings &&
         left.boxTypeBindingCount == right.boxTypeBindingCount &&
         left.valueElementBindings == right.valueElementBindings &&
         left.valueElementBindingCount == right.valueElementBindingCount &&
         left.eventBindings == right.eventBindings &&
         left.eventBindingCount == right.eventBindingCount &&
         left.eventArrayStorageBindings == right.eventArrayStorageBindings &&
         left.eventArrayStorageBindingCount ==
             right.eventArrayStorageBindingCount &&
         left.threadOriginBindings == right.threadOriginBindings &&
         left.threadOriginBindingCount == right.threadOriginBindingCount &&
         left.callSiteBindings == right.callSiteBindings &&
         left.callSiteBindingCount == right.callSiteBindingCount &&
         left.frameLocalBindings == right.frameLocalBindings &&
         left.frameLocalBindingCount == right.frameLocalBindingCount &&
         left.callDispatchBindings == right.callDispatchBindings &&
         left.callDispatchBindingCount == right.callDispatchBindingCount &&
         left.classDispatchMethodBindings ==
             right.classDispatchMethodBindings &&
         left.classDispatchMethodBindingCount ==
             right.classDispatchMethodBindingCount &&
         left.classDispatchParentBindings ==
             right.classDispatchParentBindings &&
         left.classDispatchParentBindingCount ==
             right.classDispatchParentBindingCount &&
         left.interfaceCallables == right.interfaceCallables &&
         left.interfaceCallableCount == right.interfaceCallableCount &&
         left.interfaceInstances == right.interfaceInstances &&
         left.interfaceInstanceCount == right.interfaceInstanceCount &&
         left.interfaceCallableBindings == right.interfaceCallableBindings &&
         left.interfaceCallableBindingCount ==
             right.interfaceCallableBindingCount &&
         left.interfaceCallBindings == right.interfaceCallBindings &&
         left.interfaceCallBindingCount == right.interfaceCallBindingCount &&
         left.callableSignatureBindings == right.callableSignatureBindings &&
         left.codeUnitScopeBindings == right.codeUnitScopeBindings &&
         left.initializerBindings == right.initializerBindings &&
         left.callableSignatureBindingCount ==
             right.callableSignatureBindingCount &&
         left.codeUnitScopeBindingCount == right.codeUnitScopeBindingCount &&
         left.initializerBindingCount == right.initializerBindingCount &&
         left.expressionRootBindings == right.expressionRootBindings &&
         left.expressionRootBindingCount == right.expressionRootBindingCount &&
         left.expressionTemplates == right.expressionTemplates &&
         left.expressionTemplateCount == right.expressionTemplateCount &&
         left.expressionNodes == right.expressionNodes &&
         left.expressionNodeCount == right.expressionNodeCount &&
         left.expressionOperands == right.expressionOperands &&
         left.expressionOperandCount == right.expressionOperandCount &&
         left.expressionLeaves == right.expressionLeaves &&
         left.expressionLeafCount == right.expressionLeafCount &&
         left.expressionBoundMembers == right.expressionBoundMembers &&
         left.expressionBoundMemberCount == right.expressionBoundMemberCount &&
         left.expressionLeafShapes == right.expressionLeafShapes &&
         left.expressionLeafShapeCount == right.expressionLeafShapeCount &&
         left.terminalDriverBindings == right.terminalDriverBindings &&
         left.terminalDriverBindingCount == right.terminalDriverBindingCount &&

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
         left.definitionParameterValues == right.definitionParameterValues &&
         left.definitionParameterValueCount ==
             right.definitionParameterValueCount &&
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
  uint32_t expressionRootIndex = 0, expressionOrdinal = 0,
           defaultBinding = UINT32_MAX;
  const uint8_t *root = nullptr, *recipe = nullptr, *node = nullptr;
  if (expressionForCursor(database, cursor.offset, expressionRootIndex,
                          expressionOrdinal, root, recipe, node,
                          &defaultBinding)) {
    uint32_t scope = 0;
    if (!expressionContext(database, root, scope, nullptr, nullptr,
                           defaultBinding))
      return OBELISK_RT_INVALID_DESIGN;
    outCursor->offset = database.scopes + uint64_t{scope} * kScopeSize;
    return OBELISK_RT_OK;
  }

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
  // Shared declarations do not duplicate full hierarchical strings or index
  // rows per instance. Resolve their exact scope prefix and declaration name.
  for (uint32_t index = 0; index != database.definitionBindingCount; ++index) {
    const uint8_t *binding = database.data + database.definitionBindings +
                             uint64_t{index} * kDefinitionBindingSize;
    uint32_t packed = read32(binding);
    if (obelisk::reflection::unpackTableIndexKind(packed) !=
        obelisk::reflection::TableKind::Scope)
      continue;
    const uint8_t *scope =
        database.data + database.scopes +
        uint64_t{obelisk::reflection::unpackTableIndex(packed)} * kScopeSize;
    std::string_view prefix;
    if (!getString(database, read64(scope + 40), prefix) ||
        nameSize <= prefix.size() || name[prefix.size()] != '.' ||
        std::memcmp(name, prefix.data(), prefix.size()) != 0)
      continue;
    const uint8_t *definition = database.data + database.definitions +
                                uint64_t{read32(binding + 4)} * kDefinitionSize;
    for (uint32_t ordinal = 0; ordinal != read32(definition + 20); ++ordinal) {
      uint32_t memberIndex = read32(definition + 16) + ordinal;
      const uint8_t *member = database.data + database.definitionMembers +
                              uint64_t{memberIndex} * kDefinitionMemberSize;
      using Kind = obelisk::reflection::VPIObjectKind;
      if (read16(member + 16) != static_cast<uint16_t>(Kind::Parameter) &&
          read16(member + 16) != static_cast<uint16_t>(Kind::TypeParameter))
        continue;
      std::string_view local;
      if (!getString(database, database.strings + read32(member), local))
        return OBELISK_RT_INVALID_DESIGN;
      if (local.size() == nameSize - prefix.size() - 1 &&
          std::memcmp(local.data(), name + prefix.size() + 1, local.size()) ==
              0) {
        outCursor->offset = virtualToken(kVirtualMemberTag, index, memberIndex);
        return OBELISK_RT_OK;
      }
    }
  }
  outCursor->offset = 0;
  return OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status designInfo(const Database &database,
                             obelisk_rt_design_cursor_v1 cursor,
                             obelisk_rt_design_info_v1 *outInfo) {
  uint32_t expressionRootIndex = 0, expressionOrdinal = 0;
  const uint8_t *root = nullptr, *recipe = nullptr, *node = nullptr;
  if (expressionForCursor(database, cursor.offset, expressionRootIndex,
                          expressionOrdinal, root, recipe, node)) {
    *outInfo = {};
    outInfo->kind = OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT;
    if (read32(node + 4) != UINT32_MAX &&
        semanticPackedBitWidth(database, read32(node + 4),
                               &outInfo->bit_width) != OBELISK_RT_OK)
      return OBELISK_RT_INVALID_DESIGN;
    return OBELISK_RT_OK;
  }

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
    if (!refObject &&
        (exactKind == static_cast<uint16_t>(
                          obelisk::reflection::VPIObjectKind::Parameter) ||
         exactKind == static_cast<uint16_t>(
                          obelisk::reflection::VPIObjectKind::TypeParameter))) {
      uint16_t flags = read16(member + 18);
      if (flags & kDefinitionParameterRange)
        outInfo->capabilities |= OBELISK_RT_DESIGN_CAP_PARAMETER_EXPLICIT_RANGE;
      if (flags & kDefinitionParameterDefaultUnavailable)
        outInfo->capabilities |=
            OBELISK_RT_DESIGN_CAP_UNAVAILABLE_PARAMETER_DEFAULT;
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
  if (kind == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT) {
    // Class-owned parameters use full records to retain their distinct
    // lexical and specialization identities, with the same frozen value
    // metadata as compact parameters and no executable storage type.
    uint32_t packedSource = 0;
    VPIFrozenValue frozen{};
    if (packedObjectReferenceForCursor(database, cursor.offset, packedSource) &&
        findFrozenValue(database, packedSource, frozen))
      outInfo->bit_width = frozen.bitWidth;
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
  uint32_t expressionRootIndex = 0, expressionOrdinal = 0;
  const uint8_t *expressionRootRecord = nullptr, *recipe = nullptr,
                *node = nullptr;
  if (expressionForCursor(database, object.offset, expressionRootIndex,
                          expressionOrdinal, expressionRootRecord, recipe,
                          node)) {
    if (read32(node + 4) == UINT32_MAX) {
      *outCursor = {};
      return OBELISK_RT_EOF;
    }
    outCursor->offset = uint64_t{read32(node + 4)} + 1;
    return OBELISK_RT_OK;
  }

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
  uint32_t expressionRootIndex = 0, expressionOrdinal = 0;
  const uint8_t *root = nullptr, *recipe = nullptr, *node = nullptr;
  if (expressionForCursor(database, cursor.offset, expressionRootIndex,
                          expressionOrdinal, root, recipe, node)) {
    *outType = read16(node);
    return OBELISK_RT_OK;
  }

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
  uint32_t expressionRootIndex = 0, expressionOrdinal = 0;
  const uint8_t *root = nullptr, *recipe = nullptr, *node = nullptr;
  if (expressionForCursor(database, cursor.offset, expressionRootIndex,
                          expressionOrdinal, root, recipe, node)) {
    if (!obelisk::reflection::findVPIProperty(read16(node), selector))
      return OBELISK_RT_EOF;
    if ((selector == 39 &&
         read16(node) ==
             uint32_t(obelisk::reflection::VPIObjectKind::Operation)) ||
        (selector == 40 &&
         read16(node) ==
             uint32_t(obelisk::reflection::VPIObjectKind::Constant)) ||
        (selector == 72 &&
         read16(node) ==
             uint32_t(obelisk::reflection::VPIObjectKind::IndexedPartSelect))) {
      outValue->kind =
          uint8_t(obelisk::reflection::VPIPropertyValueKind::Integer);
      outValue->payload = read16(node + 2);
      return OBELISK_RT_OK;
    }
    if (selector == 6 && read32(node + 20)) {
      outValue->kind =
          uint8_t(obelisk::reflection::VPIPropertyValueKind::Integer);
      outValue->payload = read32(node + 20);
      return OBELISK_RT_OK;
    }
    if (selector == 5 && read32(node + 16) != UINT32_MAX) {
      std::string_view value;
      if (!getString(database, database.strings + read32(node + 16), value))
        return OBELISK_RT_INVALID_DESIGN;
      outValue->kind =
          uint8_t(obelisk::reflection::VPIPropertyValueKind::String);
      outValue->stringData = reinterpret_cast<const uint8_t *>(value.data());
      outValue->stringSize = value.size();
      outValue->payload = database.strings + read32(node + 16);
      return OBELISK_RT_OK;
    }
    return OBELISK_RT_EOF;
  }
  uint32_t virtualScope = 0;
  uint32_t virtualMember = 0;
  const uint8_t *member = nullptr;
  if (virtualMemberForCursor(database, cursor.offset, virtualScope,
                             virtualMember, member)) {
    using Kind = obelisk::reflection::VPIObjectKind;
    bool parameter =
        read16(member + 16) == static_cast<uint16_t>(Kind::Parameter) ||
        read16(member + 16) == static_cast<uint16_t>(Kind::TypeParameter);
    if (!parameter)
      return OBELISK_RT_EOF;
    if (selector == 2 /* vpiName */) {
      std::string_view name;
      if (!getString(database, database.strings + read32(member), name))
        return OBELISK_RT_INVALID_DESIGN;
      outValue->kind = static_cast<uint8_t>(
          obelisk::reflection::VPIPropertyValueKind::String);
      outValue->payload = database.strings + read32(member);
      outValue->stringData = reinterpret_cast<const uint8_t *>(name.data());
      outValue->stringSize = name.size();
      return OBELISK_RT_OK;
    }
    if (selector == 70 /* vpiLocalParam */) {
      outValue->kind = static_cast<uint8_t>(
          obelisk::reflection::VPIPropertyValueKind::Boolean);
      outValue->payload =
          (read16(member + 18) & kDefinitionParameterLocal) != 0;
      return OBELISK_RT_OK;
    }
    return OBELISK_RT_EOF;
  }
  obelisk::reflection::TableKind table;
  uint32_t sourceIndex = 0;
  if (!relationSourceForCursor(database, cursor.offset, table, sourceIndex))
    return OBELISK_RT_INVALID_HANDLE;
  uint32_t packedSource = 0;
  if (!obelisk::reflection::tryPackTableIndex(table, sourceIndex, packedSource))
    return OBELISK_RT_INVALID_HANDLE;

  // Definition-backed properties are the common path for elaborated module,
  // interface, program, and built-in primitive instances. Validated images
  // cannot also carry a fixed row for these selectors on a bound source, so
  // avoid a guaranteed miss in the larger general property table.
  if (selector == 9 || selector == 15 || selector == 16 || selector == 33) {
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
        if (selector == 33) {
          uint16_t primitiveType = read16(definition + 2);
          if (primitiveType == 0)
            return OBELISK_RT_EOF;
          outValue->kind = static_cast<uint8_t>(
              obelisk::reflection::VPIPropertyValueKind::Integer);
          outValue->payload = primitiveType;
          return OBELISK_RT_OK;
        }
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

  // Static driver defaults need no per-instance property rows. Explicit
  // overrides above take precedence; gate defaults reuse the shared subtype.
  if (selector == 31 || selector == 32) {
    uint32_t kind = 0;
    if (designVPIType(database, cursor, &kind) != OBELISK_RT_OK)
      return OBELISK_RT_INVALID_HANDLE;
    uint32_t strength = 0;
    using Kind = obelisk::reflection::VPIObjectKind;
    if (kind == static_cast<uint32_t>(Kind::ContAssign))
      strength = 64;
    else if (kind == static_cast<uint32_t>(Kind::Gate)) {
      VPIFixedPropertyValue primitive{};
      auto status = designVPIFixedProperty(database, cursor, 33, &primitive);
      if (status != OBELISK_RT_OK)
        return status;
      const auto *subtype = obelisk::reflection::findVPIBuiltinPrimitive(
          static_cast<uint32_t>(primitive.payload));
      if (subtype && subtype->objectKind == kind)
        strength = subtype->defaultDriveStrength;
    }
    if (strength) {
      outValue->kind = static_cast<uint8_t>(
          obelisk::reflection::VPIPropertyValueKind::Integer);
      outValue->payload = strength;
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
  uint32_t expressionRootIndex = 0, expressionOrdinal = 0,
           defaultBinding = UINT32_MAX;
  const uint8_t *root = nullptr, *recipe = nullptr, *node = nullptr;
  if (expressionForCursor(database, source.offset, expressionRootIndex,
                          expressionOrdinal, root, recipe, node,
                          &defaultBinding)) {
    using Kind = obelisk::reflection::VPIObjectKind;
    using Rel = obelisk::reflection::VPIRelationKind;
    Kind kind = static_cast<Kind>(read16(node));
    if (!iterate && (selector == uint32_t(Kind::Module) ||
                     selector == uint32_t(Rel::InstanceRel))) {
      uint64_t instance = 0;
      uint32_t instanceKind = 0;
      bool module = selector == uint32_t(Kind::Module);
      if (!expressionInstance(database, root, module, instance, instanceKind,
                              defaultBinding))
        return OBELISK_RT_EOF;
      outRange->first =
          defaultBinding == UINT32_MAX
              ? virtualToken(kVirtualMemberTag, expressionRootIndex,
                             module ? kExpressionModuleRelation
                                    : kExpressionInstanceRelation)
              : virtualToken(kVirtualMemberTag, defaultBinding,
                             module ? kDefaultModuleRelation
                                    : kDefaultInstanceRelation);
      outRange->count = 1;
      return OBELISK_RT_OK;
    }
    if (!iterate && selector == uint32_t(Rel::ScopeRel) &&
        obelisk::reflection::isVPIBitSelection(uint32_t(kind)) &&
        obelisk::reflection::findVPITraversal(uint32_t(kind), selector, mode)) {
      uint64_t first = uint64_t{read32(recipe + 12)} + read32(node + 8);
      uint32_t parent = read32(database.data + database.expressionOperands +
                               first * kExpressionOperandSize);
      obelisk_rt_design_cursor_v1 declaration{};
      uint32_t parentKind = 0;
      if (!expressionOperandTarget(database, expressionRootIndex, parent,
                                   declaration.offset, parentKind))
        return OBELISK_RT_INVALID_DESIGN;
      return designVPIRelationRange(database, declaration, selector, false,
                                    outRange);
    }
    uint32_t child = UINT32_MAX, count = 1;
    if (selector == uint32_t(Rel::OperandRel) && iterate &&
        kind == Kind::Operation) {
      child = 0;
      count = read32(node + 12);
    } else if (obelisk::reflection::isVPIBitSelection(uint32_t(kind))) {
      const auto *edge =
          obelisk::reflection::findVPITraversal(uint32_t(kind), selector, mode);
      if (edge && selector == uint32_t(Rel::ParentRel) && !iterate)
        child = 0;
      else if (edge && selector == uint32_t(Rel::IndexRel))
        child = 1;
    } else if (!iterate &&
               (kind == Kind::PartSelect || kind == Kind::IndexedPartSelect)) {
      if (selector == uint32_t(Rel::ParentRel))
        child = 0;
      if (selector == uint32_t(kind == Kind::PartSelect ? Rel::LeftRangeRel
                                                        : Rel::BaseExprRel))
        child = 1;
      if (selector == uint32_t(kind == Kind::PartSelect ? Rel::RightRangeRel
                                                        : Rel::WidthExprRel))
        child = 2;
    }
    if (child == UINT32_MAX)
      return OBELISK_RT_EOF;
    uint32_t operand = read32(recipe + 12) + read32(node + 8) + child;
    outRange->first =
        defaultBinding == UINT32_MAX
            ? virtualToken(kVirtualMemberTag, expressionRootIndex, operand)
            : virtualToken(kVirtualMemberTag, defaultBinding,
                           kExpressionCursorFlag | operand);
    outRange->count = count;
    return OBELISK_RT_OK;
  }
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
    if (!virtualRefObject && !iterate &&
        (sourceType == static_cast<uint32_t>(
                           obelisk::reflection::VPIObjectKind::Parameter) ||
         sourceType ==
             static_cast<uint32_t>(
                 obelisk::reflection::VPIObjectKind::TypeParameter)) &&
        (selector == static_cast<uint32_t>(
                         obelisk::reflection::VPIRelationKind::ScopeRel) ||
         selector == static_cast<uint32_t>(
                         obelisk::reflection::VPIObjectKind::Module) ||
         selector == static_cast<uint32_t>(
                         obelisk::reflection::VPIRelationKind::InstanceRel))) {
      uint32_t scopeKind = recordVPIKind(database.data + database.scopes +
                                         uint64_t{virtualScope} * kScopeSize);
      if (!obelisk::reflection::vpiObjectSetContains(edge->targets, scopeKind))
        return OBELISK_RT_EOF;
      outRange->first = virtualToken(kVirtualReverseRelationTag, virtualBinding,
                                     virtualMember);
      outRange->count = 1;
      return OBELISK_RT_OK;
    }
    if (!virtualRefObject && !iterate &&
        (sourceType ==
             uint32_t(obelisk::reflection::VPIObjectKind::Parameter) ||
         sourceType ==
             uint32_t(obelisk::reflection::VPIObjectKind::TypeParameter)) &&
        selector == uint32_t(obelisk::reflection::VPIRelationKind::ExprRel)) {
      uint32_t low = 0, high = database.expressionRootBindingCount;
      auto wanted = std::make_tuple(
          virtualMember, uint16_t(selector),
          obelisk::reflection::expressionRootDefinitionMember, uint32_t{0});
      while (low != high) {
        uint32_t middle = low + (high - low) / 2;
        const uint8_t *candidate = expressionRoot(database, middle);
        auto key =
            std::make_tuple(read32(candidate), read16(candidate + 12),
                            read16(candidate + 14), read32(candidate + 16));
        if (key < wanted)
          low = middle + 1;
        else
          high = middle;
      }
      const uint8_t *candidate = expressionRoot(database, low);
      if (!candidate || read32(candidate) != virtualMember ||
          read16(candidate + 12) != selector ||
          !isDefinitionDefault(candidate) || isConcreteTypeDefault(candidate))
        return OBELISK_RT_EOF;
      // Upper zero is reserved for CaseItem groups. Binding+1 keeps their
      // root-group tokens disjoint from contextual default root tokens.
      outRange->first = virtualToken(kVirtualMemberTag, virtualBinding + 1,
                                     kExpressionRootGroupFlag | low);
      outRange->count = 1;
      return OBELISK_RT_OK;
    }
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
        read32(binding + 12) +
        definitionIOCount(database, definition, virtualMember - firstMember);
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
    if (edge && isDefinitionMemberInventory(*edge)) {
      uint32_t definitionIndex = 0;
      uint32_t specializationIndex = 0;
      uint32_t bindingIndex = 0;
      if (!findDefinitionBinding(database, sourceIndex, definitionIndex,
                                 specializationIndex, &bindingIndex))
        goto physicalRelations;
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
        goto physicalRelations;
      const uint8_t *relation = database.data +
                                database.definitionMemberRelations +
                                uint64_t{low} * kDefinitionMemberRelationSize;
      if (read16(relation) != compactSelector)
        goto physicalRelations;
      outRange->first = virtualToken(kVirtualForwardRelationTag, bindingIndex,
                                     read32(relation + 4));
      outRange->count = read32(relation + 8);
      return OBELISK_RT_OK;
    }
  }

physicalRelations: {
  uint32_t packed = 0;
  if (obelisk::reflection::tryPackTableIndex(table, sourceIndex, packed)) {
    uint32_t low = 0, high = database.expressionRootBindingCount;
    auto wanted = std::make_tuple(packed, uint16_t(selector), uint16_t(mode));
    auto group = [&](const uint8_t *root) {
      return std::make_tuple(read32(root), read16(root + 12),
                             read16(root + 14));
    };
    while (low != high) {
      uint32_t middle = low + (high - low) / 2;
      if (group(expressionRoot(database, middle)) < wanted)
        low = middle + 1;
      else
        high = middle;
    }
    if (low < database.expressionRootBindingCount &&
        group(expressionRoot(database, low)) == wanted) {
      uint32_t end = low + 1;
      while (iterate && end < database.expressionRootBindingCount &&
             group(expressionRoot(database, end)) == wanted)
        ++end;
      outRange->first = iterate
                            ? virtualToken(kVirtualMemberTag, 0,
                                           kExpressionRootGroupFlag | low)
                            : virtualToken(kVirtualMemberTag, low, UINT32_MAX);
      outRange->count = end - low;
      return OBELISK_RT_OK;
    }
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
  if (decodeVirtualToken(relationIndex, kVirtualMemberTag, bindingIndex,
                         payload)) {
    uint32_t defaultBinding = UINT32_MAX;
    if (payload == kDefaultInstanceRelation ||
        payload == kDefaultModuleRelation) {
      if (bindingIndex >= database.definitionBindingCount)
        return OBELISK_RT_INVALID_HANDLE;
      const uint8_t *binding = database.data + database.definitionBindings +
                               uint64_t{bindingIndex} * kDefinitionBindingSize;
      uint32_t packed = read32(binding),
               scope = obelisk::reflection::unpackTableIndex(packed);
      if (obelisk::reflection::unpackTableIndexKind(packed) !=
              obelisk::reflection::TableKind::Scope ||
          scope >= database.scopeCount)
        return OBELISK_RT_INVALID_HANDLE;
      outCursor->offset = database.scopes + uint64_t{scope} * kScopeSize;
      *outType = recordVPIKind(database.data + outCursor->offset);
      *outStatement = false;
      return payload != kDefaultModuleRelation ||
                     *outType ==
                         uint32_t(obelisk::reflection::VPIObjectKind::Module)
                 ? OBELISK_RT_OK
                 : OBELISK_RT_INVALID_HANDLE;
    }
    if ((payload & kExpressionPayloadDomainMask) == kExpressionCursorFlag) {
      defaultBinding = bindingIndex;
      uint32_t position = payload & ~kExpressionPayloadDomainMask;
      bool found = false;
      for (uint32_t index = 0; index != database.expressionRootBindingCount;
           ++index) {
        const uint8_t *candidate = expressionRoot(database, index);
        const uint8_t *type = expressionTemplate(database, candidate);
        if (!isDefinitionDefault(candidate) || !type ||
            position < read32(type + 12) ||
            uint64_t{position} - read32(type + 12) >= read32(type + 16))
          continue;
        bindingIndex = index;
        found = true;
        break;
      }
      if (!found)
        return OBELISK_RT_INVALID_HANDLE;
      payload = position;
    }
    if ((payload & kExpressionPayloadDomainMask) == kExpressionRootGroupFlag) {
      if (bindingIndex)
        defaultBinding = bindingIndex - 1;
      bindingIndex = payload & ~kExpressionPayloadDomainMask;
      const uint8_t *groupRoot = expressionRoot(database, bindingIndex);
      if (!groupRoot ||
          (defaultBinding == UINT32_MAX ? read16(groupRoot + 14) != 1
                                        : !isDefinitionDefault(groupRoot)))
        return OBELISK_RT_INVALID_HANDLE;
      payload = UINT32_MAX;
    }
    const uint8_t *root = expressionRoot(database, bindingIndex);
    const uint8_t *recipe = root ? expressionTemplate(database, root) : nullptr;
    if (!root || (isDefinitionDefault(root) != (defaultBinding != UINT32_MAX)))
      return OBELISK_RT_INVALID_HANDLE;
    if (payload == kExpressionUseOwnerRelation) {
      if (!expressionUseOwner(database, root, *outType, outCursor->offset))
        return OBELISK_RT_INVALID_HANDLE;
      *outStatement = obelisk::reflection::unpackTableIndexKind(read32(root)) ==
                      obelisk::reflection::TableKind::Statement;
      return OBELISK_RT_OK;
    }
    if (payload == kExpressionInstanceRelation ||
        payload == kExpressionModuleRelation) {
      *outStatement = false;
      return expressionInstance(database, root,
                                payload == kExpressionModuleRelation,
                                outCursor->offset, *outType)
                 ? OBELISK_RT_OK
                 : OBELISK_RT_INVALID_HANDLE;
    }
    uint32_t operand = recipe ? 0 : kExpressionOperandLeafFlag;
    if (payload != UINT32_MAX) {
      if (!recipe || payload < read32(recipe + 12) ||
          uint64_t{payload} - read32(recipe + 12) >= read32(recipe + 16))
        return OBELISK_RT_INVALID_HANDLE;
      operand = read32(database.data + database.expressionOperands +
                       uint64_t{payload} * kExpressionOperandSize);
    }
    *outStatement = false;
    return expressionOperandTarget(database, bindingIndex, operand,
                                   outCursor->offset, *outType, defaultBinding)
               ? OBELISK_RT_OK
               : OBELISK_RT_INVALID_HANDLE;
  }
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

obelisk_rt_status
obelisk_rt_cached_vpi_scope(const obelisk_rt_context *context, uint64_t scopeID,
                            obelisk_rt_design_cursor_v1 *outCursor) noexcept {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outCursor = {};
  const Database *database = cachedDatabase(context);
  if (!database)
    return OBELISK_RT_INVALID_HANDLE;
  if (scopeID == 0)
    return OBELISK_RT_EOF;
  // Validation guarantees unique scope IDs, but deliberately permits any
  // physical table order. Do not binary-search compiler-specific ordering.
  for (uint64_t index = 0; index != database->scopeCount; ++index) {
    uint64_t offset = database->scopes + index * kScopeSize;
    obelisk::reflection::ScopeView scope(database->data + offset);
    if (scope.getID() == scopeID) {
      outCursor->offset = offset;
      return OBELISK_RT_OK;
    }
  }
  return OBELISK_RT_EOF;
}

obelisk_rt_status obelisk_rt_cached_vpi_event_array_cell(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 event,
    VPIEventCellBinding *outBinding) noexcept {
  if (!outBinding)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outBinding = {};
  const Database *database = cachedDatabase(context);
  if (!database)
    return OBELISK_RT_INVALID_HANDLE;
  VPIArrayMemberInfo member{};
  auto status = designVPIArrayMember(*database, event, &member);
  if (status != OBELISK_RT_OK)
    return status;
  if (member.arrayType !=
      static_cast<uint32_t>(
          obelisk::reflection::VPIObjectKind::NamedEventArray))
    return OBELISK_RT_EOF;
  uint32_t packedArray = 0;
  if (!packedObjectReferenceForCursor(*database, member.array.offset,
                                      packedArray))
    return OBELISK_RT_INVALID_DESIGN;
  uint64_t low = 0, high = database->eventArrayStorageBindingCount;
  while (low < high) {
    uint64_t middle = low + (high - low) / 2;
    obelisk::reflection::EventArrayStorageBindingView binding(
        database->data + database->eventArrayStorageBindings +
        middle * kEventArrayStorageBindingSize);
    if (binding.getArrayIndexAndTable() < packedArray)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == database->eventArrayStorageBindingCount)
    return OBELISK_RT_EOF;
  obelisk::reflection::EventArrayStorageBindingView binding(
      database->data + database->eventArrayStorageBindings +
      low * kEventArrayStorageBindingSize);
  if (binding.getArrayIndexAndTable() != packedArray)
    return OBELISK_RT_EOF;
  const uint64_t storage =
      database->objects + uint64_t{binding.getStorageIndex()} * kObjectSize;
  uint64_t physical = read64(database->data + storage + 48);
  uint64_t remaining = member.index.elementCount;
  uint64_t ordinal = member.ordinal, bitOffset = 0;
  for (uint32_t dimension = 0; dimension != member.index.dimensionCount;
       ++dimension) {
    const uint8_t *type = database->data + physical;
    uint64_t extent = 0;
    // Validation ties the physical ranges and dimension count to the public
    // relation-index inventory, and proves each stride contains its child.
    if (!rangeExtent(type, extent) || !extent || remaining % extent)
      return OBELISK_RT_INVALID_DESIGN;
    remaining /= extent;
    if (!remaining)
      return OBELISK_RT_INVALID_DESIGN;
    uint64_t coordinate = ordinal / remaining;
    ordinal %= remaining;
    bitOffset += coordinate * (read64(type + 8) / extent);
    physical = read64(type + 32);
  }
  outBinding->storage.offset = storage;
  outBinding->bitOffset = bitOffset;
  return OBELISK_RT_OK;
}

namespace {
void decodeFrameLocalBinding(const Database &database,
                             obelisk::reflection::FrameLocalBindingView binding,
                             VPIFrameLocalBinding &result) noexcept {
  result.id = binding.getID();
  result.declaration.offset =
      database.objects + uint64_t{binding.getObjectIndex()} * kObjectSize;
  result.lexicalCodeUnitID =
      read64(database.data + database.objects +
             uint64_t{binding.getOwnerCodeUnitIndex()} * kObjectSize + 8);
  (void)packedFrameLocalScopeOffset(database, binding.getScopeIndexAndTable(),
                                    result.scope.offset);
  result.ordinal = binding.getOrdinal();
}
} // namespace

obelisk_rt_status
obelisk_rt_cached_vpi_frame_local(const obelisk_rt_context *context,
                                  uint64_t id,
                                  VPIFrameLocalBinding *outBinding) noexcept {
  if (!outBinding)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outBinding = {};
  const Database *database = cachedDatabase(context);
  if (!database)
    return OBELISK_RT_INVALID_HANDLE;
  uint64_t low = 0, high = database->frameLocalBindingCount;
  while (low < high) {
    const uint64_t middle = low + (high - low) / 2;
    obelisk::reflection::FrameLocalBindingView binding(
        database->data + database->frameLocalBindings +
        middle * kFrameLocalBindingSize);
    if (binding.getID() < id)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == database->frameLocalBindingCount)
    return OBELISK_RT_EOF;
  obelisk::reflection::FrameLocalBindingView binding(
      database->data + database->frameLocalBindings +
      low * kFrameLocalBindingSize);
  if (binding.getID() != id)
    return OBELISK_RT_EOF;
  decodeFrameLocalBinding(*database, binding, *outBinding);
  return OBELISK_RT_OK;
}

obelisk_rt_status obelisk_rt_cached_vpi_frame_locals(
    const obelisk_rt_context *context, uint64_t lexicalCodeUnitID,
    obelisk_rt_design_cursor_v1 scope,
    std::vector<VPIFrameLocalBinding> &outBindings) noexcept {
  outBindings.clear();
  const Database *database = cachedDatabase(context);
  if (!database)
    return OBELISK_RT_INVALID_HANDLE;
  obelisk::reflection::TableKind table;
  uint32_t index = 0, packed = 0;
  if (!relationSourceForCursor(*database, scope.offset, table, index) ||
      !obelisk::reflection::tryPackTableIndex(table, index, packed))
    return OBELISK_RT_INVALID_HANDLE;
  OBELISK_RT_TRY {
    std::vector<VPIFrameLocalBinding> bindings;
    for (uint64_t index = 0; index != database->frameLocalBindingCount;
         ++index) {
      obelisk::reflection::FrameLocalBindingView binding(
          database->data + database->frameLocalBindings +
          index * kFrameLocalBindingSize);
      if (binding.getScopeIndexAndTable() != packed)
        continue;
      VPIFrameLocalBinding local;
      decodeFrameLocalBinding(*database, binding, local);
      if (local.lexicalCodeUnitID == lexicalCodeUnitID)
        bindings.push_back(local);
    }
    std::sort(bindings.begin(), bindings.end(),
              [](const auto &left, const auto &right) {
                return left.ordinal < right.ordinal;
              });
    outBindings = std::move(bindings);
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}

obelisk_rt_status
obelisk_rt_cached_vpi_statement(const obelisk_rt_context *context,
                                uint64_t statementID,
                                obelisk_rt_design_cursor_v1 *outCursor,
                                uint64_t *outOwnerCodeUnitID) noexcept {
  if (!outCursor || !outOwnerCodeUnitID)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outCursor = {};
  *outOwnerCodeUnitID = 0;
  const Database *database = cachedDatabase(context);
  if (!database)
    return OBELISK_RT_INVALID_HANDLE;
  uint64_t low = 0, high = database->statementCount;
  while (low < high) {
    uint64_t middle = low + (high - low) / 2;
    const uint8_t *statement =
        database->data + database->statements + middle * kStatementSize;
    if (read64(statement) < statementID)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == database->statementCount)
    return OBELISK_RT_EOF;
  const uint64_t offset = database->statements + low * kStatementSize;
  const uint8_t *statement = database->data + offset;
  if (read64(statement) != statementID)
    return OBELISK_RT_EOF;
  uint32_t owner = read32(statement + 8);
  if (owner != UINT32_MAX)
    *outOwnerCodeUnitID = read64(database->data + database->objects +
                                 uint64_t{owner} * kObjectSize + 8);
  outCursor->offset = offset;
  return OBELISK_RT_OK;
}

obelisk_rt_status obelisk_rt_cached_vpi_initializer(
    const obelisk_rt_context *context, uint64_t codeUnitID,
    obelisk_rt_design_cursor_v1 *outSource) noexcept {
  if (!outSource)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outSource = {};
  const Database *database = cachedDatabase(context);
  if (!database)
    return OBELISK_RT_INVALID_HANDLE;
  uint32_t source = 0;
  if (!findInitializerSource(*database, codeUnitID, source))
    return OBELISK_RT_EOF;
  return packedObjectReferenceOffset(*database, source, outSource->offset)
             ? OBELISK_RT_OK
             : OBELISK_RT_INVALID_DESIGN;
}

obelisk_rt_status
obelisk_rt_cached_vpi_call(const obelisk_rt_context *context, uint64_t callID,
                           obelisk_rt_design_cursor_v1 *outCursor,
                           uint64_t *outOwnerCodeUnitID,
                           bool *outIsStatement) noexcept {
  if (!outCursor || !outOwnerCodeUnitID || !outIsStatement)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outCursor = {};
  *outOwnerCodeUnitID = 0;
  *outIsStatement = false;
  const Database *database = cachedDatabase(context);
  if (!database)
    return OBELISK_RT_INVALID_HANDLE;
  uint64_t low = 0, high = database->callSiteBindingCount;
  while (low < high) {
    uint64_t middle = low + (high - low) / 2;
    obelisk::reflection::CallSiteBindingView binding(
        database->data + database->callSiteBindings +
        middle * kCallSiteBindingSize);
    if (binding.getID() < callID)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == database->callSiteBindingCount)
    return OBELISK_RT_EOF;
  obelisk::reflection::CallSiteBindingView binding(
      database->data + database->callSiteBindings + low * kCallSiteBindingSize);
  if (binding.getID() != callID)
    return OBELISK_RT_EOF;
  uint32_t packed = binding.getObjectIndexAndTable();
  uint32_t index = obelisk::reflection::unpackTableIndex(packed);
  *outIsStatement = obelisk::reflection::unpackTableIndexKind(packed) ==
                    obelisk::reflection::TableKind::Statement;
  outCursor->offset =
      *outIsStatement ? database->statements + uint64_t{index} * kStatementSize
                      : database->objects + uint64_t{index} * kObjectSize;
  *outOwnerCodeUnitID =
      read64(database->data + database->objects +
             uint64_t{binding.getOwnerCodeUnitIndex()} * kObjectSize + 8);
  return OBELISK_RT_OK;
}

obelisk_rt_status
obelisk_rt_cached_vpi_code_unit_transport(const obelisk_rt_context *context,
                                          uint64_t codeUnitID,
                                          bool *outIsTask) noexcept {
  if (!outIsTask)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outIsTask = false;
  const Database *database = cachedDatabase(context);
  if (!database)
    return OBELISK_RT_INVALID_HANDLE;
  for (uint64_t index = 0; index != database->objectCount; ++index) {
    const uint8_t *object =
        database->data + database->objects + index * kObjectSize;
    const uint32_t kind = recordKind(object);
    if ((kind != OBELISK_RT_DESIGN_RECORD_PROCESS &&
         kind != OBELISK_RT_DESIGN_RECORD_FUNCTION) ||
        read64(object + 8) != codeUnitID)
      continue;
    uint32_t packedSource = 0;
    uint64_t sourceOffset = 0;
    if (!findCallableSource(*database, static_cast<uint32_t>(index),
                            packedSource, sourceOffset))
      return OBELISK_RT_EOF;
    // The validated mapping proves TaskViaFunction explicitly. The physical
    // record decides scheduler handoff; source identity still controls the
    // independent call-target proof and the public Frame scope.
    *outIsTask = kind == OBELISK_RT_DESIGN_RECORD_PROCESS;
    return OBELISK_RT_OK;
  }
  return OBELISK_RT_EOF;
}

obelisk_rt_status
obelisk_rt_cached_vpi_code_unit_scope(const obelisk_rt_context *context,
                                      uint64_t codeUnitID,
                                      obelisk_rt_design_cursor_v1 *outCursor,
                                      uint64_t *outLexicalCodeUnitID) noexcept {
  if (!outCursor || !outLexicalCodeUnitID)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outCursor = {};
  *outLexicalCodeUnitID = 0;
  const Database *database = cachedDatabase(context);
  if (!database)
    return OBELISK_RT_INVALID_HANDLE;
  uint64_t found = 0;
  for (uint64_t index = 0; index != database->objectCount; ++index) {
    const uint64_t offset = database->objects + index * kObjectSize;
    const uint8_t *object = database->data + offset;
    if ((recordKind(object) != OBELISK_RT_DESIGN_RECORD_PROCESS &&
         recordKind(object) != OBELISK_RT_DESIGN_RECORD_FUNCTION) ||
        read64(object + 8) != codeUnitID)
      continue;
    if (found)
      return OBELISK_RT_INVALID_DESIGN;
    found = offset;
  }
  if (!found)
    return OBELISK_RT_EOF;
  const uint8_t *object = database->data + found;
  uint32_t packedSource = 0;
  uint64_t sourceOffset = 0;
  if (findCallableSource(
          *database,
          static_cast<uint32_t>((found - database->objects) / kObjectSize),
          packedSource, sourceOffset)) {
    outCursor->offset = sourceOffset;
    *outLexicalCodeUnitID = codeUnitID;
    return OBELISK_RT_OK;
  }
  if (read32(object + 4) & OBELISK_RT_DESIGN_CAP_INTERNAL) {
    obelisk_rt_design_cursor_v1 origin{};
    auto status =
        obelisk_rt_cached_vpi_thread_origin(context, codeUnitID, &origin);
    if (status != OBELISK_RT_OK)
      return status;
    const uint8_t *statement = database->data + origin.offset;
    uint32_t owner = read32(statement + 8);
    if (owner == UINT32_MAX)
      return OBELISK_RT_EOF;
    const uint8_t *lexicalOwner =
        database->data + database->objects + uint64_t{owner} * kObjectSize;
    bool isStatement = false;
    status = obelisk_rt_cached_vpi_statement_enclosing_scope(
        context, origin, outCursor, &isStatement);
    if (status == OBELISK_RT_OK)
      *outLexicalCodeUnitID = read64(lexicalOwner + 8);
    return status;
  }
  const auto *kind =
      obelisk::reflection::findVPIObjectKind(recordVPIKind(object));
  outCursor->offset = kind && (kind->families &
                               obelisk::reflection::vpiFamilyMask(
                                   obelisk::reflection::VPIObjectFamily::Scope))
                          ? found
                          : read64(object + 16);
  *outLexicalCodeUnitID = codeUnitID;
  return OBELISK_RT_OK;
}

obelisk_rt_status
obelisk_rt_cached_vpi_call_target(const obelisk_rt_context *context,
                                  uint64_t callID,
                                  uint64_t calleeCodeUnitID) noexcept {
  const Database *database = cachedDatabase(context);
  if (!database)
    return OBELISK_RT_INVALID_HANDLE;
  uint64_t low = 0, high = database->callSiteBindingCount;
  while (low < high) {
    uint64_t middle = low + (high - low) / 2;
    obelisk::reflection::CallSiteBindingView binding(
        database->data + database->callSiteBindings +
        middle * kCallSiteBindingSize);
    if (binding.getID() < callID)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == database->callSiteBindingCount)
    return OBELISK_RT_EOF;
  obelisk::reflection::CallSiteBindingView binding(
      database->data + database->callSiteBindings + low * kCallSiteBindingSize);
  if (binding.getID() != callID)
    return OBELISK_RT_EOF;
  if (binding.getFlags() & 2) {
    // A positive target row has already passed complete dispatch recomputation.
    // The zero row describes the source contract and is never an activation.
    if (!calleeCodeUnitID)
      return OBELISK_RT_EOF;
    uint64_t first = 0, last = database->callDispatchBindingCount;
    const auto key = std::pair(callID, calleeCodeUnitID);
    while (first < last) {
      uint64_t middle = first + (last - first) / 2;
      obelisk::reflection::CallDispatchBindingView row(
          database->data + database->callDispatchBindings +
          middle * kCallDispatchBindingSize);
      if (std::pair(row.getCallID(), row.getCodeUnitID()) < key)
        first = middle + 1;
      else
        last = middle;
    }
    if (first == database->callDispatchBindingCount)
      return OBELISK_RT_EOF;
    obelisk::reflection::CallDispatchBindingView row(
        database->data + database->callDispatchBindings +
        first * kCallDispatchBindingSize);
    return std::pair(row.getCallID(), row.getCodeUnitID()) == key
               ? OBELISK_RT_OK
               : OBELISK_RT_EOF;
  }
  if (binding.getFlags() & 4) {
    if (!calleeCodeUnitID)
      return OBELISK_RT_EOF;
    uint64_t first = 0, last = database->interfaceCallBindingCount;
    while (first < last) {
      uint64_t middle = first + (last - first) / 2;
      obelisk::reflection::InterfaceCallBindingView contract(
          database->data + database->interfaceCallBindings +
          middle * kInterfaceCallBindingSize);
      if (contract.getCallID() < callID)
        first = middle + 1;
      else
        last = middle;
    }
    if (first == database->interfaceCallBindingCount)
      return OBELISK_RT_EOF;
    obelisk::reflection::InterfaceCallBindingView contract(
        database->data + database->interfaceCallBindings +
        first * kInterfaceCallBindingSize);
    if (contract.getCallID() != callID)
      return OBELISK_RT_EOF;
    const uint32_t member = contract.getMemberIndex();
    first = 0;
    last = database->interfaceCallableBindingCount;
    while (first < last) {
      uint64_t middle = first + (last - first) / 2;
      obelisk::reflection::InterfaceCallableBindingView implementation(
          database->data + database->interfaceCallableBindings +
          middle * kInterfaceCallableBindingSize);
      if (implementation.getMemberIndex() < member)
        first = middle + 1;
      else
        last = middle;
    }
    // Rows have been proved complete for this exact nominal member. Provider
    // identities may be shared by multiple interface instances.
    for (; first != database->interfaceCallableBindingCount; ++first) {
      obelisk::reflection::InterfaceCallableBindingView implementation(
          database->data + database->interfaceCallableBindings +
          first * kInterfaceCallableBindingSize);
      if (implementation.getMemberIndex() != member)
        break;
      const uint8_t *unit =
          database->data + database->objects +
          uint64_t{implementation.getImplementationIndex()} * kObjectSize;
      if (read64(unit + 8) == calleeCodeUnitID)
        return OBELISK_RT_OK;
    }
    return OBELISK_RT_EOF;
  }
  if (binding.getFlags() & 1)
    return OBELISK_RT_EOF;
  obelisk_rt_design_cursor_v1 call{};
  uint64_t owner = 0;
  bool statement = false;
  auto status =
      obelisk_rt_cached_vpi_call(context, callID, &call, &owner, &statement);
  if (status != OBELISK_RT_OK)
    return status;
  uint32_t kind = 0;
  status = obelisk_rt_cached_vpi_type(context, call, &kind);
  if (status != OBELISK_RT_OK)
    return status;
  using Kind = obelisk::reflection::VPIObjectKind;
  uint32_t selector;
  if (kind == static_cast<uint32_t>(Kind::TaskCall) ||
      kind == static_cast<uint32_t>(Kind::MethodTaskCall))
    selector = static_cast<uint32_t>(Kind::Task);
  else if (kind == static_cast<uint32_t>(Kind::FuncCall) ||
           kind == static_cast<uint32_t>(Kind::MethodFuncCall))
    selector = static_cast<uint32_t>(Kind::Function);
  else
    return OBELISK_RT_EOF;
  VPIRelationRange range{};
  status = obelisk_rt_cached_vpi_relation_range(context, call, selector, false,
                                                &range);
  if (status != OBELISK_RT_OK || !range.count)
    return status == OBELISK_RT_OK ? OBELISK_RT_EOF : status;
  obelisk_rt_design_cursor_v1 target{};
  uint32_t targetKind = 0;
  status = obelisk_rt_cached_vpi_relation_target(context, range.first, &target,
                                                 &targetKind, &statement);
  if (status != OBELISK_RT_OK)
    return status;
  if (statement)
    return OBELISK_RT_EOF;
  for (uint32_t index = 0; index != database->objectCount; ++index) {
    const uint8_t *unit =
        database->data + database->objects + uint64_t{index} * kObjectSize;
    if ((recordKind(unit) != OBELISK_RT_DESIGN_RECORD_PROCESS &&
         recordKind(unit) != OBELISK_RT_DESIGN_RECORD_FUNCTION) ||
        read64(unit + 8) != calleeCodeUnitID)
      continue;
    uint32_t packed = 0;
    uint64_t source = 0;
    return findCallableSource(*database, index, packed, source) &&
                   source == target.offset
               ? OBELISK_RT_OK
               : OBELISK_RT_EOF;
  }
  return OBELISK_RT_EOF;
}

obelisk_rt_status obelisk_rt_cached_vpi_net_resolution(
    const obelisk_rt_context *context, uint64_t netID,
    obelisk_rt_design_cursor_v1 *outNet,
    obelisk_rt_design_cursor_v1 *outFunction) noexcept {
  if (!outNet || !outFunction)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outNet = {};
  *outFunction = {};
  const Database *database = cachedDatabase(context);
  if (!database)
    return OBELISK_RT_INVALID_HANDLE;
  obelisk_rt_design_cursor_v1 net{};
  for (uint64_t index = 0; index != database->objectCount; ++index) {
    uint64_t offset = database->objects + index * kObjectSize;
    const uint8_t *object = database->data + offset;
    if (recordKind(object) == OBELISK_RT_DESIGN_RECORD_NET &&
        read64(object + 8) == netID) {
      const auto *origin = obelisk::reflection::findVPITraversal(
          static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::Frame),
          static_cast<uint32_t>(
              obelisk::reflection::VPIRelationKind::OriginRel),
          obelisk::reflection::VPITraversalMode::Handle);
      if (!origin || !obelisk::reflection::vpiObjectSetContains(
                         origin->targets, recordVPIKind(object)))
        return OBELISK_RT_EOF;
      net.offset = offset;
      break;
    }
  }
  if (!net.offset)
    return OBELISK_RT_EOF;
  auto target = [&](obelisk_rt_design_cursor_v1 source, uint32_t selector,
                    uint32_t expected, obelisk_rt_design_cursor_v1 &result) {
    VPIRelationRange range{};
    auto status = obelisk_rt_cached_vpi_relation_range(context, source,
                                                       selector, false, &range);
    if (status != OBELISK_RT_OK)
      return status;
    if (range.count != 1)
      return OBELISK_RT_INVALID_DESIGN;
    uint32_t kind = 0;
    bool statement = false;
    status = obelisk_rt_cached_vpi_relation_target(context, range.first,
                                                   &result, &kind, &statement);
    if (status != OBELISK_RT_OK)
      return status;
    return !statement && kind == expected ? OBELISK_RT_OK : OBELISK_RT_EOF;
  };
  using Kind = obelisk::reflection::VPIObjectKind;
  obelisk_rt_design_cursor_v1 nettype{}, function{};
  auto status = target(net, static_cast<uint32_t>(Kind::Typespec),
                       static_cast<uint32_t>(Kind::NettypeDecl), nettype);
  if (status != OBELISK_RT_OK)
    return status;
  status = target(
      nettype,
      static_cast<uint32_t>(obelisk::reflection::VPIRelationKind::WithRel),
      static_cast<uint32_t>(Kind::Function), function);
  if (status != OBELISK_RT_OK)
    return status;
  *outNet = net;
  *outFunction = function;
  return OBELISK_RT_OK;
}

obelisk_rt_status
obelisk_rt_cached_vpi_resolution_target(const obelisk_rt_context *context,
                                        uint64_t netID,
                                        uint64_t calleeCodeUnitID) noexcept {
  obelisk_rt_design_cursor_v1 net{}, function{};
  auto status =
      obelisk_rt_cached_vpi_net_resolution(context, netID, &net, &function);
  if (status != OBELISK_RT_OK)
    return status;
  const Database *database = cachedDatabase(context);
  for (uint32_t index = 0; index != database->objectCount; ++index) {
    const uint8_t *unit =
        database->data + database->objects + uint64_t{index} * kObjectSize;
    if (recordKind(unit) != OBELISK_RT_DESIGN_RECORD_FUNCTION ||
        read64(unit + 8) != calleeCodeUnitID)
      continue;
    uint32_t packed = 0;
    uint64_t source = 0;
    return findCallableSource(*database, index, packed, source) &&
                   source == function.offset
               ? OBELISK_RT_OK
               : OBELISK_RT_EOF;
  }
  return OBELISK_RT_EOF;
}

obelisk_rt_status
obelisk_rt_cached_vpi_export_target(const obelisk_rt_context *context,
                                    uint64_t codeUnitID, uint64_t scopeID,
                                    bool isTask) noexcept {
  const Database *database = cachedDatabase(context);
  if (!database)
    return OBELISK_RT_INVALID_HANDLE;
  for (uint32_t index = 0; index != database->objectCount; ++index) {
    const uint8_t *unit =
        database->data + database->objects + uint64_t{index} * kObjectSize;
    if (recordKind(unit) != (isTask ? OBELISK_RT_DESIGN_RECORD_PROCESS
                                    : OBELISK_RT_DESIGN_RECORD_FUNCTION) ||
        read64(unit + 8) != codeUnitID)
      continue;
    uint32_t packed = 0;
    uint64_t source = 0;
    if (!findCallableSource(*database, index, packed, source) ||
        objectReferenceVPIKind(*database, source) !=
            static_cast<uint32_t>(
                isTask ? obelisk::reflection::VPIObjectKind::Task
                       : obelisk::reflection::VPIObjectKind::Function))
      return OBELISK_RT_EOF;
    uint64_t scope = read64(unit + 16);
    return isScopeOffset(*database, scope) &&
                   read64(database->data + scope + 8) == scopeID
               ? OBELISK_RT_OK
               : OBELISK_RT_EOF;
  }
  return OBELISK_RT_EOF;
}

obelisk_rt_status obelisk_rt_cached_vpi_frame_origin(
    const obelisk_rt_context *context, uint32_t originKind, uint64_t originID,
    obelisk_rt_design_cursor_v1 *outCursor, bool *outIsStatement) noexcept {
  if (!outCursor || !outIsStatement)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outCursor = {};
  *outIsStatement = false;
  switch (originKind) {
  case 4:
  case 0: {
    uint64_t lexicalOwner = 0;
    auto status = obelisk_rt_cached_vpi_code_unit_scope(
        context, originID, outCursor, &lexicalOwner);
    if (status != OBELISK_RT_OK)
      return status;
    uint32_t kind = 0;
    status = obelisk_rt_cached_vpi_type(context, *outCursor, &kind);
    const auto *edge = obelisk::reflection::findVPITraversal(
        static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::Frame),
        static_cast<uint32_t>(obelisk::reflection::VPIRelationKind::OriginRel),
        obelisk::reflection::VPITraversalMode::Handle);
    if (status != OBELISK_RT_OK || !edge ||
        !obelisk::reflection::vpiObjectSetContains(edge->targets, kind)) {
      *outCursor = {};
      return status == OBELISK_RT_OK ? OBELISK_RT_EOF : status;
    }
    *outIsStatement =
        isStatementOffset(*cachedDatabase(context), outCursor->offset);
    return OBELISK_RT_OK;
  }
  case 1: {
    uint64_t owner = 0;
    obelisk_rt_design_cursor_v1 call{};
    bool statement = false;
    auto status = obelisk_rt_cached_vpi_call(context, originID, &call, &owner,
                                             &statement);
    if (status != OBELISK_RT_OK)
      return status;
    uint32_t kind = 0;
    status = obelisk_rt_cached_vpi_type(context, call, &kind);
    if (status != OBELISK_RT_OK)
      return status;
    const auto *edge = obelisk::reflection::findVPITraversal(
        static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::Frame),
        static_cast<uint32_t>(obelisk::reflection::VPIRelationKind::OriginRel),
        obelisk::reflection::VPITraversalMode::Handle);
    if (!edge ||
        !obelisk::reflection::vpiObjectSetContains(edge->targets, kind))
      return OBELISK_RT_EOF;
    *outCursor = call;
    *outIsStatement = statement;
    return OBELISK_RT_OK;
  }
  case 2: {
    uint64_t owner = 0;
    obelisk_rt_design_cursor_v1 statement{};
    auto status =
        obelisk_rt_cached_vpi_statement(context, originID, &statement, &owner);
    if (status != OBELISK_RT_OK)
      return status;
    const Database *database = cachedDatabase(context);
    if (!(read16(database->data + statement.offset + 38) &
          OBELISK_RT_DESIGN_STATEMENT_SCOPE))
      return OBELISK_RT_EOF;
    *outCursor = statement;
    *outIsStatement = true;
    return OBELISK_RT_OK;
  }
  case 3: {
    obelisk_rt_design_cursor_v1 function{};
    return obelisk_rt_cached_vpi_net_resolution(context, originID, outCursor,
                                                &function);
  }
  default:
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

obelisk_rt_status obelisk_rt_cached_vpi_thread_origin(
    const obelisk_rt_context *context, uint64_t codeUnitID,
    obelisk_rt_design_cursor_v1 *outStatement) noexcept {
  if (!outStatement)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outStatement = {};
  const Database *database = cachedDatabase(context);
  if (!database)
    return OBELISK_RT_INVALID_HANDLE;
  uint64_t low = 0, high = database->threadOriginBindingCount;
  while (low < high) {
    const uint64_t middle = low + (high - low) / 2;
    obelisk::reflection::ThreadOriginBindingView binding(
        database->data + database->threadOriginBindings +
        middle * kThreadOriginBindingSize);
    if (binding.getCodeUnitID() < codeUnitID)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == database->threadOriginBindingCount)
    return OBELISK_RT_EOF;
  obelisk::reflection::ThreadOriginBindingView binding(
      database->data + database->threadOriginBindings +
      low * kThreadOriginBindingSize);
  if (binding.getCodeUnitID() != codeUnitID)
    return OBELISK_RT_EOF;
  outStatement->offset = database->statements +
                         uint64_t{binding.getStatementIndex()} * kStatementSize;
  return OBELISK_RT_OK;
}

obelisk_rt_status
obelisk_rt_cached_vpi_event_binding(const obelisk_rt_context *context,
                                    obelisk_rt_design_cursor_v1 event,
                                    uint64_t *outEventID) noexcept {
  if (!outEventID)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outEventID = 0;
  const Database *database = cachedDatabase(context);
  uint32_t packed = 0;
  if (!database ||
      !packedObjectReferenceForCursor(*database, event.offset, packed))
    return OBELISK_RT_INVALID_HANDLE;
  uint64_t low = 0, high = database->eventBindingCount;
  while (low < high) {
    uint64_t middle = low + (high - low) / 2;
    obelisk::reflection::EventBindingView binding(
        database->data + database->eventBindings + middle * kEventBindingSize);
    if (binding.getEventIndexAndTable() < packed)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == database->eventBindingCount)
    return OBELISK_RT_EOF;
  obelisk::reflection::EventBindingView binding(
      database->data + database->eventBindings + low * kEventBindingSize);
  if (binding.getEventIndexAndTable() != packed)
    return OBELISK_RT_EOF;
  *outEventID = binding.getEventID();
  return OBELISK_RT_OK;
}

obelisk_rt_status
obelisk_rt_cached_vpi_modport_binding(const obelisk_rt_context *context,
                                      obelisk_rt_design_cursor_v1 modport,
                                      VPIModportBinding *outBinding) noexcept {
  if (!outBinding)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outBinding = {};
  const Database *database = cachedDatabase(context);
  uint32_t packed = 0;
  if (!database ||
      !packedObjectReferenceForCursor(*database, modport.offset, packed))
    return OBELISK_RT_INVALID_HANDLE;
  uint64_t low = 0, high = database->modportBindingCount;
  while (low < high) {
    uint64_t middle = low + (high - low) / 2;
    obelisk::reflection::ModportBindingView binding(
        database->data + database->modportBindings +
        middle * kModportBindingSize);
    if (binding.getModportIndexAndTable() < packed)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == database->modportBindingCount)
    return OBELISK_RT_EOF;
  obelisk::reflection::ModportBindingView binding(
      database->data + database->modportBindings + low * kModportBindingSize);
  if (binding.getModportIndexAndTable() != packed)
    return OBELISK_RT_EOF;
  std::string_view name;
  if (!getString(*database, database->strings + binding.getName(), name))
    return OBELISK_RT_INVALID_DESIGN;
  outBinding->scope.offset =
      database->scopes + uint64_t{binding.getScopeIndex()} * kScopeSize;
  outBinding->name = reinterpret_cast<const uint8_t *>(name.data());
  outBinding->nameSize = name.size();
  return OBELISK_RT_OK;
}

obelisk_rt_status
obelisk_rt_cached_vpi_modport(const obelisk_rt_context *context,
                              obelisk_rt_design_cursor_v1 scope,
                              const uint8_t *name, uint64_t nameSize,
                              obelisk_rt_design_cursor_v1 *outCursor) noexcept {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outCursor = {};
  if (nameSize && !name)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  if (!database || !isScopeOffset(*database, scope.offset) ||
      recordVPIKind(database->data + scope.offset) !=
          static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::Interface))
    return OBELISK_RT_INVALID_HANDLE;
  if (!nameSize)
    return OBELISK_RT_EOF;
  VPIRelationRange range{};
  obelisk_rt_status status = obelisk_rt_cached_vpi_relation_range(
      context, scope,
      static_cast<uint32_t>(obelisk::reflection::VPIObjectKind::Modport), true,
      &range);
  if (status != OBELISK_RT_OK)
    return status;
  for (uint64_t index = 0; index != range.count; ++index) {
    obelisk_rt_design_cursor_v1 modport{};
    uint32_t type = 0;
    bool statement = false;
    status = obelisk_rt_cached_vpi_relation_target(context, range.first + index,
                                                   &modport, &type, &statement);
    if (status != OBELISK_RT_OK)
      return status;
    VPIModportBinding binding{};
    if (statement ||
        type != static_cast<uint32_t>(
                    obelisk::reflection::VPIObjectKind::Modport) ||
        obelisk_rt_cached_vpi_modport_binding(context, modport, &binding) !=
            OBELISK_RT_OK ||
        binding.scope.offset != scope.offset)
      return OBELISK_RT_INVALID_DESIGN;
    if (binding.nameSize == nameSize &&
        std::memcmp(binding.name, name, nameSize) == 0) {
      *outCursor = modport;
      return OBELISK_RT_OK;
    }
  }
  return OBELISK_RT_EOF;
}

obelisk_rt_status obelisk_rt_cached_vpi_class_typespec(
    const obelisk_rt_context *context, uint64_t classID,
    obelisk_rt_design_cursor_v1 *outCursor) noexcept {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outCursor = {};
  const Database *database = cachedDatabase(context);
  if (!database)
    return OBELISK_RT_INVALID_HANDLE;
  uint64_t low = 0, high = database->classTypeBindingCount;
  while (low < high) {
    uint64_t middle = low + (high - low) / 2;
    obelisk::reflection::ClassTypeBindingView binding(
        database->data + database->classTypeBindings +
        middle * kClassTypeBindingSize);
    if (binding.getClassID() < classID)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == database->classTypeBindingCount)
    return OBELISK_RT_EOF;
  obelisk::reflection::ClassTypeBindingView binding(
      database->data + database->classTypeBindings +
      low * kClassTypeBindingSize);
  if (binding.getClassID() != classID)
    return OBELISK_RT_EOF;
  return packedObjectReferenceOffset(
             *database, binding.getTypespecIndexAndTable(), outCursor->offset)
             ? OBELISK_RT_OK
             : OBELISK_RT_INVALID_DESIGN;
}

obelisk_rt_status obelisk_rt_cached_vpi_class_member(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 member,
    VPIClassMemberBinding *outBinding) noexcept {
  if (!outBinding)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outBinding = {};
  const Database *database = cachedDatabase(context);
  uint32_t packed = 0;
  if (!database ||
      !packedObjectReferenceForCursor(*database, member.offset, packed))
    return OBELISK_RT_INVALID_HANDLE;
  uint64_t low = 0, high = database->classMemberBindingCount;
  while (low < high) {
    uint64_t middle = low + (high - low) / 2;
    obelisk::reflection::ClassMemberBindingView binding(
        database->data + database->classMemberBindings +
        middle * kClassMemberBindingSize);
    if (binding.getMemberIndexAndTable() < packed)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == database->classMemberBindingCount)
    return OBELISK_RT_EOF;
  obelisk::reflection::ClassMemberBindingView binding(
      database->data + database->classMemberBindings + low * kClassMemberBindingSize);
  if (binding.getMemberIndexAndTable() != packed)
    return OBELISK_RT_EOF;
  outBinding->classID = binding.getClassID();
  outBinding->byteOffset = binding.getByteOffset();
  outBinding->staticEventID = binding.getEventID();
  outBinding->isStatic = (binding.getFlags() & 1) != 0;
  outBinding->isMethod = (binding.getFlags() & 2) != 0;
  std::string_view name;
  if (!getString(*database, database->strings + binding.getName(), name))
    return OBELISK_RT_INVALID_DESIGN;
  outBinding->name = reinterpret_cast<const uint8_t *>(name.data());
  outBinding->nameSize = name.size();
  if (!packedObjectReferenceOffset(*database, binding.getOwnerIndexAndTable(),
                                   outBinding->ownerScope.offset) ||
      obelisk_rt_cached_vpi_class_typespec(context, binding.getClassID(),
                                          &outBinding->ownerTypespec) != OBELISK_RT_OK ||
      (binding.getStorageIndexAndTable() != UINT32_MAX &&
       !packedObjectReferenceOffset(*database, binding.getStorageIndexAndTable(),
                                      outBinding->backingStorage.offset)))
    return OBELISK_RT_INVALID_DESIGN;
  return OBELISK_RT_OK;
}

static obelisk_rt_status
cachedLegacyMailboxElementType(const obelisk_rt_context *context,
                               obelisk_rt_design_cursor_v1 typespec,
                               obelisk_rt_design_cursor_v1 semantic,
                               obelisk_rt_design_cursor_v1 *outType) noexcept {
  if (!outType)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outType = {};
  const Database *database = cachedDatabase(context);
  uint32_t packed = 0;
  if (!database || semantic.offset == 0 ||
      semantic.offset > database->semanticTypeCount ||
      !packedObjectReferenceForCursor(*database, typespec.offset, packed))
    return OBELISK_RT_INVALID_HANDLE;
  const uint32_t semanticIndex = static_cast<uint32_t>(semantic.offset - 1);
  const uint64_t key = (uint64_t{packed} << 32) | semanticIndex;
  uint64_t low = 0, high = database->mailboxElementBindingCount;
  while (low != high) {
    const uint64_t middle = low + (high - low) / 2;
    obelisk::reflection::MailboxElementBindingView binding(
        database->data + database->mailboxElementBindings +
        middle * kMailboxElementBindingSize);
    const uint64_t candidate =
        (uint64_t{binding.getTypespecIndexAndTable()} << 32) |
        binding.getSemanticTypeIndex();
    if (candidate < key)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == database->mailboxElementBindingCount)
    return OBELISK_RT_EOF;
  obelisk::reflection::MailboxElementBindingView binding(
      database->data + database->mailboxElementBindings +
      low * kMailboxElementBindingSize);
  if (binding.getTypespecIndexAndTable() != packed ||
      binding.getSemanticTypeIndex() != semanticIndex)
    return OBELISK_RT_EOF;
  outType->offset =
      database->types + uint64_t{binding.getPhysicalTypeIndex()} * kTypeSize;
  return OBELISK_RT_OK;
}

obelisk_rt_status obelisk_rt_cached_vpi_value_layout_owner(
    const obelisk_rt_context *context,
    obelisk_rt_design_cursor_v1 reflectedOwner,
    VPIValueLayoutOwner *outOwner) noexcept {
  if (!outOwner)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outOwner = {};
  const Database *database = cachedDatabase(context);
  uint32_t packed = 0;
  if (!database ||
      !packedObjectReferenceForCursor(*database, reflectedOwner.offset, packed))
    return OBELISK_RT_INVALID_HANDLE;
  outOwner->kind = obelisk::reflection::ValueLayoutOwnerKind::ReflectedObject;
  outOwner->index = packed;
  return OBELISK_RT_OK;
}

obelisk_rt_status
obelisk_rt_cached_vpi_box_type(const obelisk_rt_context *context, uint64_t id,
                               VPIBoxTypeBinding *outType) noexcept {
  if (!outType)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outType = {};
  const Database *database = cachedDatabase(context);
  if (!database)
    return OBELISK_RT_INVALID_HANDLE;
  uint64_t low = 0, high = database->boxTypeBindingCount;
  while (low != high) {
    const uint64_t middle = low + (high - low) / 2;
    obelisk::reflection::BoxTypeBindingView binding(
        database->data + database->boxTypeBindings +
        middle * kBoxTypeBindingSize);
    if (binding.getID() < id)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == database->boxTypeBindingCount)
    return OBELISK_RT_EOF;
  obelisk::reflection::BoxTypeBindingView binding(
      database->data + database->boxTypeBindings + low * kBoxTypeBindingSize);
  if (binding.getID() != id)
    return OBELISK_RT_EOF;
  outType->layout = {obelisk::reflection::ValueLayoutOwnerKind::BoxDescriptor,
                     static_cast<uint32_t>(low)};
  outType->semantic.offset = uint64_t{binding.getSemanticTypeIndex()} + 1;
  outType->physical.offset =
      database->types + uint64_t{binding.getPhysicalTypeIndex()} * kTypeSize;
  return OBELISK_RT_OK;
}

obelisk_rt_status obelisk_rt_cached_vpi_value_element_type(
    const obelisk_rt_context *context, VPIValueLayoutOwner owner,
    obelisk_rt_design_cursor_v1 semantic,
    obelisk_rt_design_cursor_v1 *outType) noexcept {
  if (!outType)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outType = {};
  const Database *database = cachedDatabase(context);
  uint64_t reflectedOffset = 0;
  using OwnerKind = obelisk::reflection::ValueLayoutOwnerKind;
  if (!database || semantic.offset == 0 ||
      semantic.offset > database->semanticTypeCount ||
      !obelisk::reflection::isValidValueLayoutOwnerKind(owner.kind) ||
      (owner.kind == OwnerKind::BoxDescriptor &&
       owner.index >= database->boxTypeBindingCount) ||
      (owner.kind == OwnerKind::ReflectedObject &&
       !packedObjectReferenceOffset(*database, owner.index, reflectedOffset)))
    return OBELISK_RT_INVALID_HANDLE;
  const auto key =
      std::make_tuple(static_cast<uint32_t>(owner.kind), owner.index,
                      static_cast<uint32_t>(semantic.offset - 1));
  uint64_t low = 0, high = database->valueElementBindingCount;
  while (low != high) {
    const uint64_t middle = low + (high - low) / 2;
    obelisk::reflection::ValueElementBindingView binding(
        database->data + database->valueElementBindings +
        middle * kValueElementBindingSize);
    const auto candidate =
        std::make_tuple(binding.getOwnerKind(), binding.getOwnerIndex(),
                        binding.getSemanticTypeIndex());
    if (candidate < key)
      low = middle + 1;
    else
      high = middle;
  }
  if (low != database->valueElementBindingCount) {
    obelisk::reflection::ValueElementBindingView binding(
        database->data + database->valueElementBindings +
        low * kValueElementBindingSize);
    if (std::make_tuple(binding.getOwnerKind(), binding.getOwnerIndex(),
                        binding.getSemanticTypeIndex()) == key) {
      outType->offset = database->types +
                        uint64_t{binding.getPhysicalTypeIndex()} * kTypeSize;
      return OBELISK_RT_OK;
    }
  }
  return owner.kind == OwnerKind::ReflectedObject
             ? cachedLegacyMailboxElementType(context, {reflectedOffset},
                                              semantic, outType)
             : OBELISK_RT_EOF;
}

obelisk_rt_status obelisk_rt_cached_vpi_value_element_type(
    const obelisk_rt_context *context,
    obelisk_rt_design_cursor_v1 reflectedOwner,
    obelisk_rt_design_cursor_v1 semantic,
    obelisk_rt_design_cursor_v1 *outType) noexcept {
  VPIValueLayoutOwner owner;
  auto status =
      obelisk_rt_cached_vpi_value_layout_owner(context, reflectedOwner, &owner);
  if (status != OBELISK_RT_OK) {
    if (outType)
      *outType = {};
    return outType ? status : OBELISK_RT_INVALID_ARGUMENT;
  }
  return obelisk_rt_cached_vpi_value_element_type(context, owner, semantic,
                                                  outType);
}

obelisk_rt_status obelisk_rt_cached_vpi_mailbox_element_type(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 typespec,
    obelisk_rt_design_cursor_v1 semantic,
    obelisk_rt_design_cursor_v1 *outType) noexcept {
  return obelisk_rt_cached_vpi_value_element_type(context, typespec, semantic,
                                                  outType);
}

obelisk_rt_status obelisk_rt_cached_vpi_semantic_class_typespec(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 semantic,
    obelisk_rt_design_cursor_v1 *outCursor) noexcept {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outCursor = {};
  const Database *database = cachedDatabase(context);
  if (!database || semantic.offset == 0 ||
      semantic.offset > database->semanticTypeCount)
    return OBELISK_RT_INVALID_HANDLE;
  uint32_t semanticIndex = static_cast<uint32_t>(semantic.offset - 1);
  uint64_t low = 0, high = database->semanticClassBindingCount;
  while (low < high) {
    uint64_t middle = low + (high - low) / 2;
    obelisk::reflection::SemanticClassBindingView binding(
        database->data + database->semanticClassBindings +
        middle * kSemanticClassBindingSize);
    if (binding.getSemanticType() < semanticIndex)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == database->semanticClassBindingCount)
    return OBELISK_RT_EOF;
  obelisk::reflection::SemanticClassBindingView binding(
      database->data + database->semanticClassBindings +
      low * kSemanticClassBindingSize);
  if (binding.getSemanticType() != semanticIndex)
    return OBELISK_RT_EOF;
  return packedObjectReferenceOffset(
             *database, binding.getTypespecIndexAndTable(), outCursor->offset)
             ? OBELISK_RT_OK
             : OBELISK_RT_INVALID_DESIGN;
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

obelisk_rt_status
obelisk_rt_cached_design_semantic_bit_width(const obelisk_rt_context *context,
                                            obelisk_rt_design_cursor_v1 cursor,
                                            uint64_t *outWidth) noexcept {
  if (!outWidth)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outWidth = 0;
  const Database *database = cachedDatabase(context);
  if (!database || cursor.offset == 0 ||
      cursor.offset > database->semanticTypeCount)
    return OBELISK_RT_INVALID_HANDLE;
  return semanticPackedBitWidth(
      *database, static_cast<uint32_t>(cursor.offset - 1), outWidth);
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

obelisk_rt_status obelisk_rt_cached_vpi_definition_member_scope(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    obelisk_rt_design_cursor_v1 *outScope) noexcept {
  if (!outScope)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outScope = {};
  const Database *database = cachedDatabase(context);
  if (!database)
    return OBELISK_RT_INVALID_HANDLE;
  uint32_t scope = 0, memberIndex = 0;
  const uint8_t *member = nullptr;
  if (!virtualMemberForCursor(*database, cursor.offset, scope, memberIndex,
                              member))
    return OBELISK_RT_EOF;
  outScope->offset = database->scopes + uint64_t{scope} * kScopeSize;
  return OBELISK_RT_OK;
}

obelisk_rt_status
obelisk_rt_cached_vpi_frozen_value(const obelisk_rt_context *context,
                                   obelisk_rt_design_cursor_v1 cursor,
                                   VPIFrozenValue *outValue) noexcept {
  if (!outValue)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  uint32_t rootIndex = 0, ordinal = 0;
  const uint8_t *root = nullptr, *recipe = nullptr, *node = nullptr;
  if (database && expressionForCursor(*database, cursor.offset, rootIndex,
                                      ordinal, root, recipe, node))
    return expressionFrozen(*database, read32(node + 28), *outValue)
               ? OBELISK_RT_OK
               : OBELISK_RT_EOF;
  uint32_t scope = 0, memberIndex = 0, specialization = 0;
  const uint8_t *member = nullptr;
  if (database &&
      virtualMemberForCursor(*database, cursor.offset, scope, memberIndex,
                             member, nullptr, &specialization))
    return findDefinitionParameterValue(*database, specialization, memberIndex,
                                        *outValue)
               ? OBELISK_RT_OK
               : OBELISK_RT_INVALID_HANDLE;
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
uint64_t loadPackedState(const Plane &plane, uint64_t offset, uint64_t width) {
  size_t word = static_cast<size_t>(offset / 64);
  unsigned shift = static_cast<unsigned>(offset % 64);
  uint64_t result = plane[word] >> shift;
  if (shift != 0 && width > 64 - shift)
    result |= plane[word + 1] << (64 - shift);
  return result & packedMask(width);
}

template <typename Plane>
void storePackedState(Plane &plane, uint64_t offset, uint64_t width,
                      uint64_t value) {
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
    if (!write && plan && !context->vpiObservationDemand &&
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
      uint8_t *publishedOldUnknown = write ? publishedPlanes + publishedBytes
                                           : static_cast<uint8_t *>(nullptr);
      uint8_t *publishedValue = write ? publishedPlanes + publishedBytes * 2
                                      : static_cast<uint8_t *>(nullptr);
      uint8_t *publishedUnknown = write ? publishedPlanes + publishedBytes * 3
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
  if (plan && !context->vpiObservationDemand &&
      (plan->flags & OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE) != 0 &&
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
        uint32_t edges = transitionEdges(oldValueBit, oldUnknownBit,
                                         newValueBit, newUnknownBit);
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
    publishOverrideTransition(context, signalBase, bitWidth, oldValue.data(),
                              oldUnknown.data(), newValue.data(),
                              newUnknown.data());
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (context->schedulerStatus != OBELISK_RT_OK)
      return context->schedulerStatus;
  }
  return OBELISK_RT_OK;
}

obelisk_rt_status
obelisk_rt_cached_vpi_type_default(const obelisk_rt_context *context,
                                   obelisk_rt_design_cursor_v1 cursor,
                                   VPITypeDefaultInfo *outInfo) noexcept {
  if (!context || !outInfo)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outInfo = {};
  const auto *database = cachedDatabase(context);
  if (!database)
    return OBELISK_RT_INVALID_DESIGN;
  uint32_t scope = 0, memberIndex = 0;
  const uint8_t *member = nullptr;
  bool refObject = false;
  if (!virtualMemberForCursor(*database, cursor.offset, scope, memberIndex,
                              member, nullptr, nullptr, &refObject) ||
      refObject ||
      read16(member + 16) !=
          uint32_t(obelisk::reflection::VPIObjectKind::TypeParameter))
    return OBELISK_RT_EOF;
  constexpr uint16_t mode =
      obelisk::reflection::expressionRootDefinitionMember |
      obelisk::reflection::expressionRootConcreteTypeDefault;
  auto wanted = std::make_tuple(
      memberIndex, uint16_t(obelisk::reflection::VPIRelationKind::ExprRel),
      mode);
  uint32_t low = 0, high = database->expressionRootBindingCount;
  while (low != high) {
    uint32_t middle = low + (high - low) / 2;
    const uint8_t *candidate = expressionRoot(*database, middle);
    auto key = std::make_tuple(read32(candidate), read16(candidate + 12),
                               read16(candidate + 14));
    if (key < wanted)
      low = middle + 1;
    else
      high = middle;
  }
  const uint8_t *root = expressionRoot(*database, low);
  if (!root || std::make_tuple(read32(root), read16(root + 12),
                               read16(root + 14)) != wanted)
    return OBELISK_RT_EOF;
  outInfo->semantic.offset = uint64_t{read32(root + 4)} + 1;
  outInfo->line = read32(root + 16);
  if (read32(root + 8) != UINT32_MAX) {
    std::string_view file;
    if (!getString(*database, database->strings + read32(root + 8), file))
      return OBELISK_RT_INVALID_DESIGN;
    outInfo->file = reinterpret_cast<const uint8_t *>(file.data());
    outInfo->fileSize = file.size();
  }
  return OBELISK_RT_OK;
}

obelisk_rt_status
obelisk_rt_cached_vpi_expression(const obelisk_rt_context *context,
                                 obelisk_rt_design_cursor_v1 cursor,
                                 VPIExpressionInfo *outInfo) noexcept {
  if (!context || !outInfo)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outInfo = {};
  const auto *database = cachedDatabase(context);
  if (!database)
    return OBELISK_RT_INVALID_DESIGN;
  uint32_t rootIndex = 0, ordinal = 0;
  const uint8_t *root = nullptr, *recipe = nullptr, *node = nullptr;
  if (!expressionForCursor(*database, cursor.offset, rootIndex, ordinal, root,
                           recipe, node))
    return OBELISK_RT_EOF;
  outInfo->kind = read16(node);
  outInfo->code = read16(node + 2);
  outInfo->operandCount = read32(node + 12);
  outInfo->sourceOnly = read32(node + 4) == UINT32_MAX;
  if (outInfo->sourceOnly)
    return OBELISK_RT_OK;
  outInfo->semantic.offset = uint64_t{read32(node + 4)} + 1;
  if (semanticPackedBitWidth(*database, read32(node + 4), &outInfo->bitWidth) !=
      OBELISK_RT_OK)
    return OBELISK_RT_INVALID_DESIGN;
  outInfo->constantSelect = true;
  const uint8_t *selection = node;
  while (obelisk::reflection::isVPIBitSelection(read16(selection)) ||
         read16(selection) ==
             uint32_t(obelisk::reflection::VPIObjectKind::PartSelect) ||
         read16(selection) ==
             uint32_t(obelisk::reflection::VPIObjectKind::IndexedPartSelect)) {
    uint64_t first = uint64_t{read32(recipe + 12)} + read32(selection + 8);
    if (obelisk::reflection::isVPIBitSelection(read16(selection)) ||
        read16(selection) ==
            uint32_t(obelisk::reflection::VPIObjectKind::IndexedPartSelect)) {
      uint32_t base = read32(database->data + database->expressionOperands +
                             (first + 1) * kExpressionOperandSize);
      if (!expressionOperandIsConstant(*database, root, recipe, base))
        outInfo->constantSelect = false;
    }
    uint32_t parent = read32(database->data + database->expressionOperands +
                             first * kExpressionOperandSize);
    if (parent & kExpressionOperandLeafFlag)
      break;
    selection = expressionNode(*database, recipe, parent);
  }
  return OBELISK_RT_OK;
}
obelisk_rt_status obelisk_rt_cached_vpi_expression_operand(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    uint32_t index, obelisk_rt_design_cursor_v1 *outCursor,
    uint32_t *outKind) noexcept {
  if (!context || !outCursor || !outKind)
    return OBELISK_RT_INVALID_ARGUMENT;
  const auto *database = cachedDatabase(context);
  if (!database)
    return OBELISK_RT_INVALID_DESIGN;
  uint32_t rootIndex = 0, ordinal = 0, defaultBinding = UINT32_MAX;
  const uint8_t *root = nullptr, *recipe = nullptr, *node = nullptr;
  if (!expressionForCursor(*database, cursor.offset, rootIndex, ordinal, root,
                           recipe, node, &defaultBinding) ||
      index >= read32(node + 12))
    return OBELISK_RT_EOF;
  uint64_t position = uint64_t{read32(recipe + 12)} + read32(node + 8) + index;
  uint32_t operand = read32(database->data + database->expressionOperands +
                            position * kExpressionOperandSize);
  return expressionOperandTarget(*database, rootIndex, operand,
                                 outCursor->offset, *outKind, defaultBinding)
             ? OBELISK_RT_OK
             : OBELISK_RT_INVALID_DESIGN;
}

obelisk_rt_status obelisk_rt_cached_vpi_expression_use_ranges(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 source,
    std::vector<VPIRelationRange> &ranges) noexcept {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  ranges.clear();
  const auto *database = cachedDatabase(context);
  if (!database)
    return OBELISK_RT_INVALID_DESIGN;
  using Rel = obelisk::reflection::VPIRelationKind;
  uint32_t kind = 0;
  if (designVPIType(*database, source, &kind) != OBELISK_RT_OK)
    return OBELISK_RT_INVALID_HANDLE;
  const auto *edge = obelisk::reflection::findVPITraversal(
      kind, uint32_t(Rel::UseRel),
      obelisk::reflection::VPITraversalMode::Iterate);
  if (!edge || !database->expressionRootBindingCount)
    return OBELISK_RT_EOF;
  OBELISK_RT_TRY {
    std::unordered_set<uint64_t> seen;
    VPIRelationRange physical{};
    auto status = designVPIRelationRange(
        *database, source, uint32_t(Rel::UseRel), true, &physical);
    if (status != OBELISK_RT_OK && status != OBELISK_RT_EOF)
      return status;
    if (status == OBELISK_RT_OK) {
      for (uint64_t index = 0; index != physical.count; ++index) {
        obelisk_rt_design_cursor_v1 target{};
        uint32_t targetKind = 0;
        bool statement = false;
        if (designVPIRelationTarget(*database, physical.first + index, &target,
                                    &targetKind, &statement) != OBELISK_RT_OK)
          return OBELISK_RT_INVALID_DESIGN;
        seen.insert(target.offset);
      }
      ranges.push_back(physical);
    }
    bool found = false;
    for (uint32_t index = 0; index != database->expressionRootBindingCount;
         ++index) {
      const uint8_t *root = expressionRoot(*database, index);
      if (isDefinitionDefault(root))
        continue;
      const uint8_t *recipe = expressionTemplate(*database, root);
      uint32_t leafCount = recipe ? read32(recipe + 20) : 1;
      bool matches = false;
      for (uint32_t ordinal = 0; ordinal != leafCount; ++ordinal) {
        ExpressionLeafInfo leaf;
        if (!expressionLeaf(*database, root, ordinal, leaf))
          return OBELISK_RT_INVALID_DESIGN;
        if (leaf.cursor == source.offset) {
          matches = true;
          break;
        }
      }
      if (!matches)
        continue;
      found = true;
      uint32_t ownerKind = 0;
      uint64_t owner = 0;
      if (!expressionUseOwner(*database, root, ownerKind, owner))
        return OBELISK_RT_INVALID_DESIGN;
      if (!obelisk::reflection::vpiObjectSetContains(edge->targets,
                                                     ownerKind) ||
          !seen.insert(owner).second)
        continue;
      ranges.push_back(
          {virtualToken(kVirtualMemberTag, index, kExpressionUseOwnerRelation),
           1});
    }
    if (!found || ranges.empty()) {
      ranges.clear();
      return OBELISK_RT_EOF;
    }
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    ranges.clear();
    return OBELISK_RT_OUT_OF_MEMORY;
  }
  OBELISK_RT_CATCH_ALL {
    ranges.clear();
    return OBELISK_RT_INVALID_DESIGN;
  }
}

obelisk_rt_status obelisk_rt_cached_vpi_selected_use_candidates(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 source,
    uint32_t selectedKind,
    std::vector<VPISelectedUseCandidate> &candidates) noexcept {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  candidates.clear();
  const auto *database = cachedDatabase(context);
  if (!database)
    return OBELISK_RT_INVALID_DESIGN;
  using Kind = obelisk::reflection::VPIObjectKind;
  using Rel = obelisk::reflection::VPIRelationKind;
  const auto *edge = obelisk::reflection::findVPITraversal(
      selectedKind, uint32_t(Rel::UseRel),
      obelisk::reflection::VPITraversalMode::Iterate);
  if (!edge)
    return OBELISK_RT_EOF;
  OBELISK_RT_TRY {
    VPIRelationRange physical{};
    auto status = designVPIRelationRange(
        *database, source, uint32_t(Rel::UseRel), true, &physical);
    if (status != OBELISK_RT_OK && status != OBELISK_RT_EOF)
      return status;
    if (status == OBELISK_RT_OK)
      candidates.push_back({physical, {}});
    for (uint32_t index = 0; index != database->expressionRootBindingCount;
         ++index) {
      const uint8_t *root = expressionRoot(*database, index);
      if (isDefinitionDefault(root))
        continue;
      uint32_t ownerKind = 0;
      uint64_t owner = 0;
      if (!expressionUseOwner(*database, root, ownerKind, owner))
        return OBELISK_RT_INVALID_DESIGN;
      if (!obelisk::reflection::vpiObjectSetContains(edge->targets, ownerKind))
        continue;
      const uint8_t *recipe = expressionTemplate(*database, root);
      uint32_t leafCount = recipe ? read32(recipe + 20) : 1;
      for (uint32_t ordinal = 0; ordinal != leafCount; ++ordinal) {
        ExpressionLeafInfo leaf;
        if (!expressionLeaf(*database, root, ordinal, leaf))
          return OBELISK_RT_INVALID_DESIGN;
        if (leaf.cursor != source.offset)
          continue;
        uint64_t selection = 0;
        if (recipe) {
          uint32_t current = kExpressionOperandLeafFlag | ordinal;
          // Only Parent slots narrow a declaration use. Index/bound operands
          // and ordinary operator operands remain uses of the whole leaf.
          for (;;) {
            bool advanced = false;
            for (uint32_t nodeOrdinal = 0; nodeOrdinal != read32(recipe + 8);
                 ++nodeOrdinal) {
              const uint8_t *node =
                  expressionNode(*database, recipe, nodeOrdinal);
              uint32_t kind = read16(node);
              if (kind != uint32_t(Kind::PartSelect) &&
                  kind != uint32_t(Kind::IndexedPartSelect) &&
                  !obelisk::reflection::isVPIBitSelection(kind))
                continue;
              uint64_t first = uint64_t{read32(recipe + 12)} + read32(node + 8);
              if (read32(database->data + database->expressionOperands +
                         first * kExpressionOperandSize) != current)
                continue;
              current = nodeOrdinal;
              uint32_t targetKind = 0;
              if (!expressionOperandTarget(*database, index, current, selection,
                                           targetKind))
                return OBELISK_RT_INVALID_DESIGN;
              advanced = true;
              break;
            }
            if (!advanced)
              break;
          }
        }
        candidates.push_back({{virtualToken(kVirtualMemberTag, index,
                                            kExpressionUseOwnerRelation),
                               1},
                              {selection}});
      }
    }
    return candidates.empty() ? OBELISK_RT_EOF : OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_DESIGN; }
}

obelisk_rt_status obelisk_rt_cached_vpi_expression_operand_identifier(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    uint32_t index, const uint8_t **outData, uint64_t *outSize) noexcept {
  if (!context || !outData || !outSize)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outData = nullptr;
  *outSize = 0;
  const auto *database = cachedDatabase(context);
  if (!database)
    return OBELISK_RT_INVALID_DESIGN;
  uint32_t rootIndex = 0, ordinal = 0;
  const uint8_t *root = nullptr, *recipe = nullptr, *node = nullptr;
  if (!expressionForCursor(*database, cursor.offset, rootIndex, ordinal, root,
                           recipe, node) ||
      index >= read32(node + 12))
    return OBELISK_RT_EOF;
  uint64_t position = uint64_t{read32(recipe + 12)} + read32(node + 8) + index;
  uint32_t operand = read32(database->data + database->expressionOperands +
                            position * kExpressionOperandSize);
  if (!(operand & kExpressionOperandLeafFlag))
    return OBELISK_RT_EOF;
  const uint8_t *shape = database->data + database->expressionLeafShapes +
                         (uint64_t{read32(recipe + 24)} +
                          (operand & ~kExpressionOperandLeafFlag)) *
                             kExpressionLeafShapeSize;
  uint32_t name = read32(shape + 8);
  if (name == UINT32_MAX)
    return OBELISK_RT_EOF;
  std::string_view identifier;
  if (!getString(*database, database->strings + name, identifier))
    return OBELISK_RT_INVALID_DESIGN;
  *outData = reinterpret_cast<const uint8_t *>(identifier.data());
  *outSize = identifier.size();
  return OBELISK_RT_OK;
}

obelisk_rt_status obelisk_rt_cached_vpi_terminal_driver(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 terminal,
    VPITerminalDriverBinding *outBinding) noexcept {
  if (!context || !outBinding)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outBinding = {};
  const auto *database = cachedDatabase(context);
  if (!database)
    return OBELISK_RT_INVALID_DESIGN;
  uint32_t packed = 0;
  if (!packedObjectReferenceForCursor(*database, terminal.offset, packed))
    return OBELISK_RT_EOF;
  uint64_t low = 0, high = database->terminalDriverBindingCount;
  auto row = [&](uint64_t index) {
    return obelisk::reflection::TerminalDriverBindingView(
        database->data + database->terminalDriverBindings +
        index * kTerminalDriverBindingSize);
  };
  while (low != high) {
    uint64_t middle = low + (high - low) / 2;
    if (row(middle).getTerminalIndexAndTable() < packed)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == database->terminalDriverBindingCount ||
      row(low).getTerminalIndexAndTable() != packed)
    return OBELISK_RT_EOF;
  auto binding = row(low);
  auto selectedOffset = [&](uint32_t index, uint64_t &offset) {
    if (index >= database->objectCount || !context->execution)
      return false;
    const uint8_t *driver =
        database->data + database->objects + uint64_t{index} * kObjectSize;
    uint64_t base = read64(driver + 80);
    uint64_t width = read64(driver + 56);
    if (recordKind(driver) != OBELISK_RT_DESIGN_RECORD_DRIVER ||
        binding.getBitOffset() >= width ||
        base > context->execution->state_bit_count ||
        width > context->execution->state_bit_count - base)
      return false;
    offset = base + binding.getBitOffset();
    return true;
  };
  VPITerminalDriverBinding result{};
  if (!selectedOffset(binding.getLowDriverIndex(), result.lowStateOffset) ||
      (binding.getHighDriverIndex() != UINT32_MAX &&
       !selectedOffset(binding.getHighDriverIndex(), result.highStateOffset)))
    return OBELISK_RT_INVALID_DESIGN;
  *outBinding = result;
  return OBELISK_RT_OK;
}
