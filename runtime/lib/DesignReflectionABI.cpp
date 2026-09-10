//===- DesignReflectionABI.cpp - generated schema ABI checks -------------===//

#include "obelisk/Reflection/DesignReflection.h"
#include "obelisk/Runtime/Runtime.h"

#include <cstddef>
#include <cstdint>

using namespace obelisk::reflection;

static_assert(HeaderLayout.size ==
              sizeof(obelisk_rt_design_database_header_v1));
static_assert(HeaderLayout.size == OBELISK_RT_DESIGN_DATABASE_HEADER_SIZE);
static_assert(field::HeaderMagic ==
              offsetof(obelisk_rt_design_database_header_v1, magic));
static_assert(field::HeaderVersion ==
              offsetof(obelisk_rt_design_database_header_v1, version));
static_assert(field::HeaderReserved ==
              offsetof(obelisk_rt_design_database_header_v1, reserved));
static_assert(field::HeaderProfile ==
              offsetof(obelisk_rt_design_database_header_v1, profile));
static_assert(field::HeaderHeaderSize ==
              offsetof(obelisk_rt_design_database_header_v1, header_size));
static_assert(field::HeaderImageSize ==
              offsetof(obelisk_rt_design_database_header_v1, image_size));
static_assert(field::HeaderChecksum ==
              offsetof(obelisk_rt_design_database_header_v1, checksum));
static_assert(field::HeaderRoot ==
              offsetof(obelisk_rt_design_database_header_v1, root_offset));
static_assert(field::HeaderScopeOffset ==
              offsetof(obelisk_rt_design_database_header_v1, scope_offset));
static_assert(field::HeaderScopeCount ==
              offsetof(obelisk_rt_design_database_header_v1, scope_count));
static_assert(field::HeaderObjectOffset ==
              offsetof(obelisk_rt_design_database_header_v1, object_offset));
static_assert(field::HeaderObjectCount ==
              offsetof(obelisk_rt_design_database_header_v1, object_count));
static_assert(field::HeaderTypeOffset ==
              offsetof(obelisk_rt_design_database_header_v1, type_offset));
static_assert(field::HeaderTypeCount ==
              offsetof(obelisk_rt_design_database_header_v1, type_count));
static_assert(field::HeaderStringOffset ==
              offsetof(obelisk_rt_design_database_header_v1, string_offset));
static_assert(field::HeaderStringSize ==
              offsetof(obelisk_rt_design_database_header_v1, string_size));
static_assert(field::HeaderIndexOffset ==
              offsetof(obelisk_rt_design_database_header_v1, index_offset));
static_assert(field::HeaderIndexCount ==
              offsetof(obelisk_rt_design_database_header_v1, index_count));
static_assert(field::HeaderStatementOffset ==
              offsetof(obelisk_rt_design_database_header_v1, statement_offset));
static_assert(field::HeaderStatementCount ==
              offsetof(obelisk_rt_design_database_header_v1, statement_count));
static_assert(field::HeaderStatementSiteOffset ==
              offsetof(obelisk_rt_design_database_header_v1,
                       statement_site_offset));
static_assert(field::HeaderStatementSiteCount ==
              offsetof(obelisk_rt_design_database_header_v1,
                       statement_site_count));
static_assert(field::HeaderRelationOffset ==
              offsetof(obelisk_rt_design_database_header_v1, relation_offset));
static_assert(field::HeaderRelationCount ==
              offsetof(obelisk_rt_design_database_header_v1, relation_count));

static_assert(ScopeLayout.size == 64);
static_assert(ObjectLayout.size == 96);
static_assert(TypeLayout.size == 80);
static_assert(IndexLayout.size == 24);
static_assert(StatementLayout.size == 40);
static_assert(StatementSiteLayout.size == 16);
static_assert(RelationLayout.size == 16);
static_assert(StaticObjectLayout.size == 32);
static_assert(field::StaticObjectID == 0);
static_assert(field::StaticObjectScopeIndex == 8);
static_assert(field::StaticObjectSourceFile == 12);
static_assert(field::StaticObjectName == 16);
static_assert(field::StaticObjectSourceLine == 20);
static_assert(field::StaticObjectSourceColumn == 24);
static_assert(field::StaticObjectVPIKind == 28);
static_assert(field::StaticObjectFlags == 30);
static_assert(SemanticDirectoryLayout.size == 304);
static_assert(field::SemanticDirectoryStaticObjectOffset == 160);
static_assert(field::SemanticDirectoryStaticObjectCount == 168);
static_assert(field::SemanticDirectoryDefinitionOffset == 176);
static_assert(field::SemanticDirectoryDefinitionCount == 184);
static_assert(field::SemanticDirectoryDefinitionBindingOffset == 192);
static_assert(field::SemanticDirectoryDefinitionBindingCount == 200);
static_assert(field::SemanticDirectoryDefinitionMemberOffset == 208);
static_assert(field::SemanticDirectoryDefinitionMemberCount == 216);
static_assert(field::SemanticDirectoryDefinitionMemberRelationOffset == 224);
static_assert(field::SemanticDirectoryDefinitionMemberRelationCount == 232);
static_assert(field::SemanticDirectoryDefinitionMemberRelationTargetOffset ==
              240);
static_assert(field::SemanticDirectoryDefinitionMemberRelationTargetCount ==
              248);
static_assert(field::SemanticDirectoryDefinitionSpecializationOffset == 256);
static_assert(field::SemanticDirectoryDefinitionSpecializationCount == 264);
static_assert(field::SemanticDirectoryDefinitionSpecializationBindingOffset ==
              272);
static_assert(field::SemanticDirectoryDefinitionSpecializationBindingCount ==
              280);
static_assert(field::SemanticDirectoryDefinitionMemberEndpointOffset == 288);
static_assert(field::SemanticDirectoryDefinitionMemberEndpointCount == 296);
static_assert(DefinitionLayout.size == 32);
static_assert(field::DefinitionVPIKind == 0);
static_assert(field::DefinitionFlags == 2);
static_assert(field::DefinitionName == 4);
static_assert(field::DefinitionFile == 8);
static_assert(field::DefinitionLine == 12);
static_assert(DefinitionBindingLayout.size == 16);
static_assert(field::DefinitionBindingSourceIndexAndTable == 0);
static_assert(field::DefinitionBindingDefinition == 4);
static_assert(field::DefinitionBindingSpecialization == 8);
static_assert(field::DefinitionBindingFirstMemberEndpoint == 12);
static_assert(DefinitionMemberLayout.size == 20);
static_assert(DefinitionMemberRelationLayout.size == 12);
static_assert(DefinitionMemberRelationTargetLayout.size == 4);
static_assert(DefinitionSpecializationLayout.size == 12);
static_assert(DefinitionSpecializationBindingLayout.size == 8);
static_assert(DefinitionMemberEndpointLayout.size == 4);
static_assert(FixedPropertyLayout.size == 16);
static_assert(field::FixedPropertySourceIndexAndTable == 0);
static_assert(field::FixedPropertySelector == 4);
static_assert(field::FixedPropertyKindAndFlags == 6);
static_assert(field::FixedPropertyPayload == 8);
static_assert(ResolvedNetRunLayout.size == 24);
static_assert(field::ResolvedNetRunObjectIndex == 0);
static_assert(field::ResolvedNetRunNetType == 4);
static_assert(field::ResolvedNetRunFirstBit == 8);
static_assert(field::ResolvedNetRunBitCount == 16);
static_assert(NetDelayRunLayout.size == 48);
static_assert(field::NetDelayRunObjectIndex == 0);
static_assert(field::NetDelayRunReserved == 4);
static_assert(field::NetDelayRunFirstBit == 8);
static_assert(field::NetDelayRunBitCount == 16);
static_assert(field::NetDelayRunRise == 24);
static_assert(field::NetDelayRunFall == 32);
static_assert(field::NetDelayRunThird == 40);
static_assert(RelationIndexLayout.size == 20);
static_assert(field::RelationIndexObjectIndex == 0);
static_assert(field::RelationIndexFirstDimension == 4);
static_assert(field::RelationIndexDimensionCount == 8);
static_assert(field::RelationIndexFlags == 10);
static_assert(field::RelationIndexFirstKey == 12);
static_assert(field::RelationIndexFirstOrdinalKey == 16);
static_assert(RelationIndexDimensionLayout.size == 16);
static_assert(field::RelationIndexDimensionLeft == 0);
static_assert(field::RelationIndexDimensionRight == 8);
static_assert(RelationIndexKeyLayout.size == 12);
static_assert(field::RelationIndexKeyIndex == 0);
static_assert(field::RelationIndexKeyOrdinal == 8);
static_assert(RelationIndexMemberLayout.size == 12);
static_assert(field::RelationIndexMemberTargetIndexAndTable == 0);
static_assert(field::RelationIndexMemberRelationIndex == 4);
static_assert(field::RelationIndexMemberOrdinal == 8);
static_assert(tableIndexPackedShift == 30);
static_assert(unpackTableIndexKind((uint32_t{2} << 30) | 17) ==
              TableKind::Statement);
static_assert(unpackTableIndex((uint32_t{2} << 30) | 17) == 17);

static_assert(uint32_t(RecordKind::Scope) == OBELISK_RT_DESIGN_RECORD_SCOPE);
static_assert(uint32_t(RecordKind::Storage) ==
              OBELISK_RT_DESIGN_RECORD_STORAGE);
static_assert(uint32_t(RecordKind::Net) == OBELISK_RT_DESIGN_RECORD_NET);
static_assert(uint32_t(RecordKind::Driver) == OBELISK_RT_DESIGN_RECORD_DRIVER);
static_assert(uint32_t(RecordKind::Process) ==
              OBELISK_RT_DESIGN_RECORD_PROCESS);
static_assert(uint32_t(RecordKind::Type) == OBELISK_RT_DESIGN_RECORD_TYPE);
static_assert(uint32_t(RecordKind::Function) ==
              OBELISK_RT_DESIGN_RECORD_FUNCTION);
static_assert(uint32_t(RecordKind::Port) == OBELISK_RT_DESIGN_RECORD_PORT);
static_assert(uint32_t(RecordKind::StaticObject) ==
              OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT);

constexpr bool recordKindPackingIsStable() {
  uint32_t packed = 0;
  return recordKindPackedWidth == 16 && recordKindMask == UINT32_C(0xffff) &&
         recordKindPayloadMask == UINT32_C(0xffff) &&
         tryPackRecordKindPayload(RecordKind::Scope, 601, packed) &&
         unpackRecordKind(packed) == RecordKind::Scope &&
         unpackRecordKindPayload(packed) == 601 &&
         !tryPackRecordKindPayload(static_cast<RecordKind>(0), 0, packed) &&
         !tryPackRecordKindPayload(RecordKind::Scope, UINT32_C(0x10000),
                                   packed);
}
static_assert(recordKindPackingIsStable());
