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

static_assert(ScopeLayout.size == 64);
static_assert(ObjectLayout.size == 96);
static_assert(TypeLayout.size == 80);
static_assert(IndexLayout.size == 24);

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
