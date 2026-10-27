//===- ClassBitstream.cpp - Class bit-stream execution ------------------===//

#include "ContainerStorageInternal.h"
#include "RuntimeInternal.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using talos::runtime_detail::AssocSlot;
using talos::runtime_detail::assocSlotStride;
using talos::runtime_detail::assocValueOffset;
using talos::runtime_detail::BufferHeader;
using talos::runtime_detail::ContainerHeader;
using talos::runtime_detail::ensureAssocOrderedWithoutSafepoint;

constexpr uint64_t kHeaderSize = sizeof(talos_rt_class_bitstream_header_v1);
constexpr uint64_t kSiteSize = sizeof(talos_rt_class_bitstream_site_v1);
constexpr uint64_t kGroupSize = sizeof(talos_rt_class_bitstream_group_v1);
constexpr uint64_t kMemberSize = sizeof(uint64_t);
constexpr uint64_t kSchemaSize = sizeof(talos_rt_class_bitstream_schema_v1);
constexpr uint64_t kFieldSize = sizeof(talos_rt_class_bitstream_field_v1);
constexpr uint64_t kPlanHeaderSize = 32;
constexpr uint64_t kPlanRecordSize = 48;
constexpr uint64_t kResourceLimit = UINT64_C(1) << 62;

TALOS_RT_FEATURE_HELPER uint32_t read32(const uint8_t *bytes) {
  uint32_t value = 0;
  for (unsigned index = 0; index != 4; ++index)
    value |= static_cast<uint32_t>(bytes[index]) << (index * 8);
  return value;
}

TALOS_RT_FEATURE_HELPER uint64_t read64(const uint8_t *bytes) {
  uint64_t value = 0;
  for (unsigned index = 0; index != 8; ++index)
    value |= static_cast<uint64_t>(bytes[index]) << (index * 8);
  return value;
}

TALOS_RT_FEATURE_HELPER bool checkedAdd(uint64_t left, uint64_t right,
                                          uint64_t &result) {
  if (right > UINT64_MAX - left)
    return false;
  result = left + right;
  return true;
}

TALOS_RT_FEATURE_HELPER bool checkedTable(uint64_t begin, uint64_t count,
                                            uint64_t stride, uint64_t &end) {
  return count <= (UINT64_MAX - begin) / stride &&
         checkedAdd(begin, count * stride, end);
}

TALOS_RT_FEATURE_HELPER bool checkedPointerRange(const void *pointer,
                                                   uint64_t size,
                                                   uintptr_t &begin,
                                                   uintptr_t &end) {
  if (!pointer || size > UINTPTR_MAX)
    return false;
  begin = reinterpret_cast<uintptr_t>(pointer);
  if (size > UINTPTR_MAX - begin)
    return false;
  end = begin + static_cast<uintptr_t>(size);
  return true;
}

TALOS_RT_FEATURE_HELPER bool overlaps(uintptr_t leftBegin, uintptr_t leftEnd,
                                        uintptr_t rightBegin,
                                        uintptr_t rightEnd) {
  return leftBegin < rightEnd && rightBegin < leftEnd;
}

struct PlanRecord {
  uint32_t opcode = 0;
  uint32_t bodyRecords = 0;
  uint64_t sourceOffset = 0;
  uint64_t extent = 0;
  uint64_t stride = 0;
  uint64_t sourceSpan = 0;
  uint64_t outputWidth = 0;
};

TALOS_RT_FEATURE_HELPER PlanRecord readRecord(const uint8_t *records,
                                                uint64_t index) {
  const uint8_t *record = records + index * kPlanRecordSize;
  uint64_t operation = read64(record);
  return {static_cast<uint32_t>(operation),
          static_cast<uint32_t>(operation >> 32),
          read64(record + 8),
          read64(record + 16),
          read64(record + 24),
          read64(record + 32),
          read64(record + 40)};
}

struct Plan {
  const uint8_t *bytes = nullptr;
  uint64_t size = 0;
  const uint8_t *records = nullptr;
  uint64_t recordCount = 0;
  uint64_t rootSpan = 0;
};

struct Site {
  uint64_t id = 0;
  uint32_t function = TALOS_RT_CLASS_BITSTREAM_NO_BYTECODE;
  uint32_t bytecodeSite = TALOS_RT_CLASS_BITSTREAM_NO_BYTECODE;
  Plan plan;
};

struct Group {
  uint64_t id = 0;
  uint64_t staticClassID = 0;
  const talos_rt_class_descriptor_v1 *staticClass = nullptr;
  std::vector<uint64_t> members;
  std::vector<size_t> memberSchemas;
};

struct Field {
  uint64_t offset = 0;
  uint64_t planeSize = 0;
  uint64_t rootSpan = 0;
  uint32_t flags = 0;
  uint32_t alignment = 0;
  size_t planIndex = SIZE_MAX;
  Plan plan;
};

struct Schema {
  uint64_t classID = 0;
  uint64_t instanceSize = 0;
  uint32_t instanceAlignment = 0;
  const talos_rt_class_descriptor_v1 *descriptor = nullptr;
  std::vector<Field> fields;
};

struct ClassBitstreamStateImpl final : ClassBitstreamState {
  struct WatchRange {
    uint64_t begin = 0;
    uint64_t end = 0;
    uint64_t token = 0;
  };
  const uint8_t *blob = nullptr;
  uint64_t blobSize = 0;
  std::vector<Site> sites;
  std::vector<Group> groups;
  std::vector<Schema> schemas;
  std::unordered_map<uint64_t, std::vector<uint64_t>> watchGroups;
  std::unordered_map<uint64_t, uint64_t> bytecodeSites;
  std::unordered_map<uint64_t, std::vector<WatchRange>> watchRanges;
  uint64_t nextWatchGroup = 1;

  ClassBitstreamStateImpl() {
    destroy = destroyState;
    notifyRange = notifyRangeState;
  }
  TALOS_RT_FEATURE_HELPER static void
  destroyState(ClassBitstreamState *state) noexcept {
    delete static_cast<ClassBitstreamStateImpl *>(state);
  }
  TALOS_RT_FEATURE_HELPER static void
  notifyRangeState(ClassBitstreamState *base, talos_rt_context *context,
                   uint64_t identity, uint64_t offset, uint64_t size) noexcept {
    auto &state = *static_cast<ClassBitstreamStateImpl *>(base);
    if (offset > UINT64_MAX - size)
      return;
    auto found = state.watchRanges.find(identity);
    if (found == state.watchRanges.end())
      return;
    uint64_t end = offset + size;
    for (const WatchRange &range : found->second) {
      // The legacy exact-selector path immediately following this callback
      // owns this token. Notify only additional overlapping top-level fields.
      if (range.begin == offset || offset >= range.end || range.begin >= end)
        continue;
      if ((!talos_rt_notify_managed_waiters_unlocked(context, range.token) ||
           !talos_rt_notify_observer_managed_unlocked(context,
                                                        range.token)) &&
          context->schedulerStatus == TALOS_RT_OK)
        context->schedulerStatus = TALOS_RT_INVALID_ARGUMENT;
    }
  }

  TALOS_RT_FEATURE_HELPER const Site *site(uint64_t id) const {
    if (id == 0 || id > sites.size())
      return nullptr;
    const Site &candidate = sites[static_cast<size_t>(id - 1)];
    return candidate.id == id ? &candidate : nullptr;
  }
  TALOS_RT_FEATURE_HELPER const Group *group(uint64_t id) const {
    auto found = std::lower_bound(
        groups.begin(), groups.end(), id,
        [](const Group &group, uint64_t value) { return group.id < value; });
    return found != groups.end() && found->id == id ? &*found : nullptr;
  }
  TALOS_RT_FEATURE_HELPER const Schema *schema(uint64_t id) const {
    auto found = std::lower_bound(schemas.begin(), schemas.end(), id,
                                  [](const Schema &schema, uint64_t value) {
                                    return schema.classID < value;
                                  });
    return found != schemas.end() && found->classID == id ? &*found : nullptr;
  }
};

TALOS_RT_FEATURE_HELPER bool powerOfTwo(uint64_t value) {
  return value != 0 && (value & (value - 1)) == 0;
}

TALOS_RT_FEATURE_HELPER bool
descriptorIsA(const talos_rt_class_descriptor_v1 *candidate,
              const talos_rt_class_descriptor_v1 *target) {
  if (!candidate || !target)
    return false;
  for (const talos_rt_class_descriptor_v1 *current = candidate; current;
       current = current->base) {
    if (current == target || current->class_id == target->class_id)
      return true;
    for (uint64_t index = 0; index != current->interface_count; ++index)
      if (current->interfaces[index].interface_id == target->class_id)
        return true;
  }
  return false;
}

TALOS_RT_FEATURE_HELPER bool rangeInPlanRegion(uint64_t offset, uint64_t size,
                                                 uint64_t planOffset,
                                                 uint64_t planSize) {
  return (offset & 7) == 0 && (size & 7) == 0 && offset >= planOffset &&
         offset <= planOffset + planSize &&
         size <= planOffset + planSize - offset;
}

enum class PlanValidation { Invalid, Valid, NeedsDepth };
struct ValidationFrame {
  uint64_t index = 0;
  uint64_t end = 0;
  uint64_t sourceSpan = 0;
  uint64_t sourceEnd = 0;
};

TALOS_RT_FEATURE_HELPER PlanValidation
validateBody(const uint8_t *records, uint64_t recordCount, uint64_t rootSpan,
             const ClassBitstreamStateImpl &state, ValidationFrame *stack,
             uint64_t capacity, bool &sawObject) {
  uint64_t depth = 0;
  stack[0] = {0, recordCount, rootSpan, 0};
  while (true) {
    ValidationFrame &frame = stack[depth];
    if (frame.index == frame.end) {
      if (depth == 0)
        return PlanValidation::Valid;
      --depth;
      continue;
    }
    if (frame.index > frame.end)
      return PlanValidation::Invalid;
    PlanRecord record = readRecord(records, frame.index++);
    if (record.sourceOffset < frame.sourceEnd)
      return PlanValidation::Invalid;
    if (record.opcode == TALOS_RT_RECURSIVE_BITSTREAM_COPY) {
      if (record.bodyRecords != 0 || record.extent == 0 || record.stride != 0 ||
          record.sourceSpan != 0 || record.outputWidth != record.extent ||
          record.sourceOffset > frame.sourceSpan ||
          record.extent > frame.sourceSpan - record.sourceOffset)
        return PlanValidation::Invalid;
      frame.sourceEnd = record.sourceOffset + record.extent;
      continue;
    }
    if (record.opcode == TALOS_RT_RECURSIVE_BITSTREAM_STRING ||
        record.opcode == TALOS_RT_RECURSIVE_BITSTREAM_OBJECT) {
      bool object = record.opcode == TALOS_RT_RECURSIVE_BITSTREAM_OBJECT;
      if (record.bodyRecords != 0 || record.stride != 0 ||
          record.sourceSpan != sizeof(talos_rt_managed_word_v1) * 8 ||
          record.outputWidth != 0 || (record.sourceOffset & 7) != 0 ||
          record.sourceOffset > frame.sourceSpan ||
          sizeof(talos_rt_managed_word_v1) * 8 >
              frame.sourceSpan - record.sourceOffset ||
          (object ? (record.extent == 0 || !state.group(record.extent))
                  : record.extent != 0))
        return PlanValidation::Invalid;
      sawObject |= object;
      frame.sourceEnd =
          record.sourceOffset + sizeof(talos_rt_managed_word_v1) * 8;
      continue;
    }
    if (record.opcode != TALOS_RT_RECURSIVE_BITSTREAM_REPEAT &&
        record.opcode != TALOS_RT_RECURSIVE_BITSTREAM_CONTAINER)
      return PlanValidation::Invalid;
    if (record.bodyRecords == 0 ||
        record.bodyRecords > frame.end - frame.index ||
        record.sourceSpan == 0 || record.outputWidth != 0)
      return PlanValidation::Invalid;
    uint64_t bodyBegin = frame.index;
    uint64_t bodyEnd = bodyBegin + record.bodyRecords;
    if (record.opcode == TALOS_RT_RECURSIVE_BITSTREAM_REPEAT) {
      if (record.extent == 0 || record.stride < record.sourceSpan ||
          record.sourceOffset > frame.sourceSpan ||
          record.extent - 1 >
              (UINT64_MAX - record.sourceOffset) / record.stride)
        return PlanValidation::Invalid;
      uint64_t last = record.sourceOffset + (record.extent - 1) * record.stride;
      if (last > frame.sourceSpan ||
          record.sourceSpan > frame.sourceSpan - last)
        return PlanValidation::Invalid;
      frame.sourceEnd = last + record.sourceSpan;
    } else {
      if ((record.extent != TALOS_RT_CONTAINER_DYNAMIC_ARRAY &&
           record.extent != TALOS_RT_CONTAINER_QUEUE &&
           record.extent != TALOS_RT_CONTAINER_ASSOCIATIVE_ARRAY) ||
          record.stride != 0 || (record.sourceOffset & 7) != 0 ||
          record.sourceOffset > frame.sourceSpan ||
          sizeof(talos_rt_managed_word_v1) * 8 >
              frame.sourceSpan - record.sourceOffset)
        return PlanValidation::Invalid;
      frame.sourceEnd =
          record.sourceOffset + sizeof(talos_rt_managed_word_v1) * 8;
    }
    if (depth + 1 >= capacity)
      return PlanValidation::NeedsDepth;
    frame.index = bodyEnd;
    stack[++depth] = {bodyBegin, bodyEnd, record.sourceSpan, 0};
  }
}

TALOS_RT_FEATURE_HELPER bool
validatePlan(Plan &plan, const uint8_t *blob, uint64_t offset, uint64_t size,
             const ClassBitstreamStateImpl &state, bool &sawObject) {
  if (size < kPlanHeaderSize || size > SIZE_MAX ||
      size != kPlanHeaderSize + ((size - kPlanHeaderSize) / kPlanRecordSize) *
                                    kPlanRecordSize)
    return false;
  const uint8_t *bytes = blob + offset;
  uint64_t identity = read64(bytes);
  uint64_t recordCount = read64(bytes + 8);
  uint64_t rootSpan = read64(bytes + 16);
  if (static_cast<uint32_t>(identity) !=
          TALOS_RT_RECURSIVE_BITSTREAM_PLAN_MAGIC ||
      recordCount == 0 ||
      recordCount > TALOS_RT_RECURSIVE_BITSTREAM_PLAN_MAX_RECORDS ||
      size != kPlanHeaderSize + recordCount * kPlanRecordSize ||
      read64(bytes + 24) != 0)
    return false;
  std::array<ValidationFrame, 16> local;
  sawObject = false;
  PlanValidation validation =
      validateBody(bytes + kPlanHeaderSize, recordCount, rootSpan, state,
                   local.data(), local.size(), sawObject);
  std::unique_ptr<ValidationFrame[]> deep;
  if (validation == PlanValidation::NeedsDepth) {
    deep = std::make_unique<ValidationFrame[]>(recordCount + 1);
    sawObject = false;
    validation = validateBody(bytes + kPlanHeaderSize, recordCount, rootSpan,
                              state, deep.get(), recordCount + 1, sawObject);
  }
  uint32_t version = static_cast<uint32_t>(identity >> 32);
  uint32_t expectedVersion =
      sawObject ? TALOS_RT_RECURSIVE_BITSTREAM_PLAN_OBJECT_VERSION
                : TALOS_RT_RECURSIVE_BITSTREAM_PLAN_VERSION;
  if (validation != PlanValidation::Valid || version != expectedVersion)
    return false;
  plan = {bytes, size, bytes + kPlanHeaderSize, recordCount, rootSpan};
  return true;
}

TALOS_RT_FEATURE_HELPER const talos_rt_execution_extension_v1 *
classExtension(const talos_rt_execution_descriptor_v1 *execution) {
  if (!execution ||
      (execution->flags & TALOS_RT_EXECUTION_CLASS_BITSTREAM) == 0 ||
      execution->reserved < sizeof(*execution))
    return nullptr;
  uintptr_t base = reinterpret_cast<uintptr_t>(execution);
  if (execution->reserved > UINTPTR_MAX - base)
    return nullptr;
  uintptr_t address = base + static_cast<uintptr_t>(execution->reserved);
  if (address % alignof(talos_rt_execution_extension_v1) != 0)
    return nullptr;
  auto *extension =
      reinterpret_cast<const talos_rt_execution_extension_v1 *>(address);
  bool valid = extension->version == TALOS_RT_EXECUTION_EXTENSION_VERSION &&
               extension->size == sizeof(*extension);
  return valid && extension->class_bitstream &&
                 extension->class_bitstream_size != 0
             ? extension
             : nullptr;
}

} // namespace

extern "C" TALOS_RT_FEATURE_TEXT void
talos_rt_v1_class_bitstream_link_anchor() {}

TALOS_RT_FEATURE_HELPER talos_rt_status
finalizeClassBitstream(talos_rt_context *context) {
  if (!context || !context->execution || context->classBitstreamState)
    return TALOS_RT_INVALID_ARGUMENT;
  const talos_rt_execution_extension_v1 *extension =
      classExtension(context->execution);
  if (!extension || extension->class_bitstream_size < kHeaderSize ||
      extension->class_bitstream_size > SIZE_MAX ||
      extension->class_bitstream_size >= kResourceLimit)
    return TALOS_RT_INVALID_DESIGN;
  const uint8_t *blob = extension->class_bitstream;
  uint64_t blobSize = extension->class_bitstream_size;
  uintptr_t blobBegin = 0, blobEnd = 0;
  if ((reinterpret_cast<uintptr_t>(blob) & 7) != 0 || blobSize > PTRDIFF_MAX ||
      !checkedPointerRange(blob, blobSize, blobBegin, blobEnd))
    return TALOS_RT_INVALID_DESIGN;
  uint64_t identity = read64(blob);
  if (static_cast<uint32_t>(identity) !=
          TALOS_RT_CLASS_BITSTREAM_BLOB_MAGIC ||
      static_cast<uint32_t>(identity >> 32) !=
          TALOS_RT_CLASS_BITSTREAM_BLOB_VERSION ||
      read64(blob + 8) != blobSize || read64(blob + 112) != 0 ||
      read64(blob + 120) != 0)
    return TALOS_RT_INVALID_DESIGN;

  uint64_t siteOffset = read64(blob + 16), siteCount = read64(blob + 24);
  uint64_t groupOffset = read64(blob + 32), groupCount = read64(blob + 40);
  uint64_t memberOffset = read64(blob + 48), memberCount = read64(blob + 56);
  uint64_t schemaOffset = read64(blob + 64), schemaCount = read64(blob + 72);
  uint64_t fieldOffset = read64(blob + 80), fieldCount = read64(blob + 88);
  uint64_t planOffset = read64(blob + 96), planSize = read64(blob + 104);
  uint64_t end = 0;
  if (siteCount == 0 || groupCount == 0 ||
      siteCount > SIZE_MAX / sizeof(Site) ||
      groupCount > SIZE_MAX / sizeof(Group) ||
      schemaCount > SIZE_MAX / sizeof(Schema) ||
      memberCount > SIZE_MAX / sizeof(uint64_t) ||
      fieldCount > SIZE_MAX / sizeof(Field) || siteOffset != kHeaderSize ||
      !checkedTable(siteOffset, siteCount, kSiteSize, end) ||
      end != groupOffset ||
      !checkedTable(groupOffset, groupCount, kGroupSize, end) ||
      end != memberOffset ||
      !checkedTable(memberOffset, memberCount, kMemberSize, end) ||
      end != schemaOffset ||
      !checkedTable(schemaOffset, schemaCount, kSchemaSize, end) ||
      end != fieldOffset ||
      !checkedTable(fieldOffset, fieldCount, kFieldSize, end) ||
      end != planOffset || !checkedAdd(planOffset, planSize, end) ||
      end != blobSize || planSize == 0 || planOffset > blobSize)
    return TALOS_RT_INVALID_DESIGN;

  std::unique_ptr<ClassBitstreamStateImpl> state;
  TALOS_RT_TRY {
    state = std::make_unique<ClassBitstreamStateImpl>();
    state->blob = blob;
    state->blobSize = blobSize;
    state->groups.reserve(static_cast<size_t>(groupCount));
    state->schemas.reserve(static_cast<size_t>(schemaCount));
    state->sites.reserve(static_cast<size_t>(siteCount));
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }

  uint64_t memberCursor = 0;
  TALOS_RT_TRY {
    for (uint64_t index = 0; index != groupCount; ++index) {
      const uint8_t *record = blob + groupOffset + index * kGroupSize;
      Group group;
      group.id = read64(record);
      group.staticClassID = read64(record + 8);
      uint64_t firstMember = read64(record + 16);
      uint64_t count = read64(record + 24);
      uint32_t hidden = read32(record + 32);
      if (group.id == 0 || group.staticClassID == 0 || hidden > 1 ||
          read32(record + 36) != 0 || firstMember != memberCursor ||
          count > memberCount - memberCursor ||
          (index != 0 && state->groups.back().id >= group.id))
        return TALOS_RT_INVALID_DESIGN;
      group.staticClass =
          talos_rt_managed_class_lookup(context, group.staticClassID);
      if (!group.staticClass)
        return TALOS_RT_INVALID_DESIGN;
      group.members.reserve(static_cast<size_t>(count));
      for (uint64_t ordinal = 0; ordinal != count; ++ordinal) {
        uint64_t member =
            read64(blob + memberOffset + (memberCursor + ordinal) * 8);
        if (member == 0 || (ordinal != 0 && group.members.back() >= member))
          return TALOS_RT_INVALID_DESIGN;
        group.members.push_back(member);
      }
      memberCursor += count;
      state->groups.push_back(std::move(group));
    }
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  if (memberCursor != memberCount)
    return TALOS_RT_INVALID_DESIGN;

  std::vector<std::pair<uint64_t, uint64_t>> planRanges;
  uint64_t fieldCursor = 0;
  TALOS_RT_TRY {
    for (uint64_t index = 0; index != schemaCount; ++index) {
      const uint8_t *record = blob + schemaOffset + index * kSchemaSize;
      Schema schema;
      schema.classID = read64(record);
      schema.instanceSize = read64(record + 8);
      uint64_t firstField = read64(record + 16);
      uint64_t count = read64(record + 24);
      schema.instanceAlignment = read32(record + 32);
      if (schema.classID == 0 || !powerOfTwo(schema.instanceAlignment) ||
          schema.instanceSize == 0 ||
          schema.instanceSize % schema.instanceAlignment != 0 ||
          firstField != fieldCursor || count > fieldCount - fieldCursor ||
          read32(record + 36) != 0 || read64(record + 40) != 0 ||
          (index != 0 && state->schemas.back().classID >= schema.classID))
        return TALOS_RT_INVALID_DESIGN;
      schema.descriptor =
          talos_rt_managed_class_lookup(context, schema.classID);
      if (!schema.descriptor ||
          schema.descriptor->instance_size != schema.instanceSize ||
          schema.descriptor->instance_alignment != schema.instanceAlignment)
        return TALOS_RT_INVALID_DESIGN;
      schema.fields.reserve(static_cast<size_t>(count));
      uint64_t previousFieldEnd = 0;
      for (uint64_t ordinal = 0; ordinal != count; ++ordinal) {
        const uint8_t *fieldRecord =
            blob + fieldOffset + (fieldCursor + ordinal) * kFieldSize;
        Field field;
        field.offset = read64(fieldRecord);
        field.planeSize = read64(fieldRecord + 8);
        field.rootSpan = read64(fieldRecord + 16);
        uint64_t fieldPlanOffset = read64(fieldRecord + 24);
        uint64_t fieldPlanSize = read64(fieldRecord + 32);
        field.flags = read32(fieldRecord + 40);
        field.alignment = read32(fieldRecord + 44);
        bool fourState =
            (field.flags & TALOS_RT_CLASS_BITSTREAM_FIELD_FOUR_STATE) != 0;
        uint64_t storageSize = field.planeSize;
        if (fourState && field.planeSize <= UINT64_MAX / 2)
          storageSize *= 2;
        else if (fourState)
          return TALOS_RT_INVALID_DESIGN;
        if (field.offset < sizeof(void *) || field.planeSize == 0 ||
            field.planeSize > UINT64_MAX / 8 || field.rootSpan == 0 ||
            field.rootSpan > field.planeSize * 8 ||
            (field.flags & ~TALOS_RT_CLASS_BITSTREAM_FIELD_FOUR_STATE) != 0 ||
            !powerOfTwo(field.alignment) ||
            field.alignment > schema.instanceAlignment ||
            field.offset % field.alignment != 0 ||
            field.offset < previousFieldEnd ||
            field.offset > schema.instanceSize ||
            storageSize > schema.instanceSize - field.offset ||
            !rangeInPlanRegion(fieldPlanOffset, fieldPlanSize, planOffset,
                               planSize))
          return TALOS_RT_INVALID_DESIGN;
        planRanges.emplace_back(fieldPlanOffset,
                                fieldPlanOffset + fieldPlanSize);
        previousFieldEnd = field.offset + storageSize;
        schema.fields.push_back(std::move(field));
        Field &stored = schema.fields.back();
        // Plans are validated after every group has been indexed.
        stored.plan = {blob + fieldPlanOffset, fieldPlanSize, nullptr, 0,
                       field.rootSpan};
      }
      fieldCursor += count;
      state->schemas.push_back(std::move(schema));
    }
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  if (fieldCursor != fieldCount)
    return TALOS_RT_INVALID_DESIGN;

  // Group membership is closed-world dispatch authority. Empty groups are
  // valid for abstract/interface static handles, but every listed concrete
  // member must have an exact schema and derive from the registered root.
  for (Group &group : state->groups)
    for (uint64_t member : group.members) {
      const Schema *schema = state->schema(member);
      if (!schema ||
          (schema->descriptor->flags &
           (TALOS_RT_CLASS_ABSTRACT | TALOS_RT_CLASS_INTERFACE)) != 0 ||
          !descriptorIsA(schema->descriptor, group.staticClass))
        return TALOS_RT_INVALID_DESIGN;
      group.memberSchemas.push_back(
          static_cast<size_t>(schema - state->schemas.data()));
    }

  struct ValidatedPlan {
    Plan plan;
    bool hasObject = false;
    size_t index = SIZE_MAX;
  };
  std::map<std::pair<uint64_t, uint64_t>, ValidatedPlan> validatedPlans;
  std::vector<std::vector<size_t>> planGroups;
  auto resolvePlan = [&](uint64_t offset, uint64_t size, uint64_t rootSpan,
                         bool requireObject, Plan &out,
                         size_t *outIndex = nullptr) {
    auto key = std::make_pair(offset, size);
    auto found = validatedPlans.find(key);
    if (found == validatedPlans.end()) {
      ValidatedPlan validated;
      if (!validatePlan(validated.plan, blob, offset, size, *state,
                        validated.hasObject))
        return false;
      validated.index = planGroups.size();
      std::vector<size_t> groups;
      for (uint64_t recordIndex = 0; recordIndex != validated.plan.recordCount;
           ++recordIndex) {
        PlanRecord record = readRecord(validated.plan.records, recordIndex);
        if (record.opcode != TALOS_RT_RECURSIVE_BITSTREAM_OBJECT)
          continue;
        const Group *group = state->group(record.extent);
        if (!group)
          return false;
        groups.push_back(static_cast<size_t>(group - state->groups.data()));
      }
      std::sort(groups.begin(), groups.end());
      groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
      planGroups.push_back(std::move(groups));
      found = validatedPlans.emplace(key, std::move(validated)).first;
    }
    if (found->second.plan.rootSpan != rootSpan ||
        (requireObject && !found->second.hasObject))
      return false;
    out = found->second.plan;
    if (outIndex)
      *outIndex = found->second.index;
    return true;
  };

  for (Schema &schema : state->schemas)
    for (Field &field : schema.fields) {
      uint64_t offset = static_cast<uint64_t>(field.plan.bytes - blob);
      if (!resolvePlan(offset, field.plan.size, field.rootSpan, false,
                       field.plan, &field.planIndex))
        return TALOS_RT_INVALID_DESIGN;
    }

  // Reject recursive static class layouts before any runtime value is
  // traversed. Dynamic object identity cycles remain guarded by the walker,
  // but a pointer-free image may not use them to smuggle an invalid schema
  // graph past compiler analysis.
  const size_t schemaNodes = state->schemas.size();
  const size_t planNodes = planGroups.size();
  const size_t groupNodes = state->groups.size();
  if (schemaNodes > SIZE_MAX - planNodes ||
      schemaNodes + planNodes > SIZE_MAX - groupNodes)
    return TALOS_RT_OUT_OF_MEMORY;
  const size_t planBase = schemaNodes;
  const size_t groupBase = planBase + planNodes;
  const size_t nodeCount = groupBase + groupNodes;
  std::vector<uint8_t> nodeColor(nodeCount, 0);
  struct GraphFrame {
    size_t node = 0;
    size_t edge = 0;
  };
  auto edgeCount = [&](size_t node) {
    if (node < planBase)
      return state->schemas[node].fields.size();
    if (node < groupBase)
      return planGroups[node - planBase].size();
    return state->groups[node - groupBase].memberSchemas.size();
  };
  auto edgeTarget = [&](size_t node, size_t edge) {
    if (node < planBase)
      return planBase + state->schemas[node].fields[edge].planIndex;
    if (node < groupBase)
      return groupBase + planGroups[node - planBase][edge];
    return state->groups[node - groupBase].memberSchemas[edge];
  };
  std::vector<GraphFrame> graphStack;
  for (size_t root = 0; root != nodeCount; ++root) {
    if (nodeColor[root] != 0)
      continue;
    nodeColor[root] = 1;
    graphStack.push_back({root, 0});
    while (!graphStack.empty()) {
      GraphFrame &frame = graphStack.back();
      if (frame.edge == edgeCount(frame.node)) {
        nodeColor[frame.node] = 2;
        graphStack.pop_back();
        continue;
      }
      size_t target = edgeTarget(frame.node, frame.edge++);
      if (nodeColor[target] == 1)
        return TALOS_RT_INVALID_DESIGN;
      if (nodeColor[target] == 0) {
        nodeColor[target] = 1;
        graphStack.push_back({target, 0});
      }
    }
  }

  bool hasBytecodeBinding = false;
  for (uint64_t index = 0; index != siteCount; ++index) {
    const uint8_t *record = blob + siteOffset + index * kSiteSize;
    if (read32(record + 8) != TALOS_RT_CLASS_BITSTREAM_NO_BYTECODE ||
        read32(record + 12) != TALOS_RT_CLASS_BITSTREAM_NO_BYTECODE) {
      hasBytecodeBinding = true;
      break;
    }
  }
  std::vector<uint32_t> bytecodeOwners;
  std::vector<uint8_t> bytecodeReferences;
  TALOS_RT_TRY {
    const auto &image = context->designBytecodeImage;
    if (hasBytecodeBinding &&
        (context->execution->flags & TALOS_RT_EXECUTION_HAS_BYTECODE) != 0) {
      bytecodeOwners.assign(static_cast<size_t>(image.siteCount), UINT32_MAX);
      bytecodeReferences.assign(static_cast<size_t>(image.siteCount), 0);
      for (uint32_t functionIndex = 0; functionIndex != image.functionCount;
           ++functionIndex) {
        auto function =
            talos::designbytecode::functionAt(image, functionIndex);
        for (uint64_t ordinal = 0; ordinal != function.instructionCount;
             ++ordinal) {
          auto instruction = talos::designbytecode::instructionAt(
              image, function.firstInstruction + ordinal);
          if (instruction.opcode != TALOS_RT_DB_INTRINSIC ||
              instruction.immediate >= image.siteCount)
            continue;
          uint32_t siteIndex = instruction.immediate;
          uint8_t &references = bytecodeReferences[siteIndex];
          if (references == 0)
            bytecodeOwners[siteIndex] = functionIndex;
          if (references != UINT8_MAX)
            ++references;
        }
      }
    }
    for (uint64_t index = 0; index != siteCount; ++index) {
      const uint8_t *record = blob + siteOffset + index * kSiteSize;
      Site site;
      site.id = read64(record);
      site.function = read32(record + 8);
      site.bytecodeSite = read32(record + 12);
      uint64_t sitePlanOffset = read64(record + 16);
      uint64_t sitePlanSize = read64(record + 24);
      bool noFunction = site.function == TALOS_RT_CLASS_BITSTREAM_NO_BYTECODE;
      bool noSite = site.bytecodeSite == TALOS_RT_CLASS_BITSTREAM_NO_BYTECODE;
      if (site.id != index + 1 || noFunction != noSite ||
          (((context->execution->flags &
             TALOS_RT_EXECUTION_REQUIRE_BYTECODE) != 0) &&
           noFunction) ||
          read64(record + 32) != 0 ||
          !rangeInPlanRegion(sitePlanOffset, sitePlanSize, planOffset,
                             planSize) ||
          sitePlanSize < kPlanHeaderSize ||
          !resolvePlan(sitePlanOffset, sitePlanSize,
                       read64(blob + sitePlanOffset + 16), true, site.plan))
        return TALOS_RT_INVALID_DESIGN;
      planRanges.emplace_back(sitePlanOffset, sitePlanOffset + sitePlanSize);
      if (!noFunction) {
        uint64_t binding = uint64_t{site.function} << 32 | site.bytecodeSite;
        const auto &image = context->designBytecodeImage;
        if ((context->execution->flags & TALOS_RT_EXECUTION_HAS_BYTECODE) ==
                0 ||
            site.function >= image.functionCount ||
            site.bytecodeSite >= image.siteCount)
          return TALOS_RT_INVALID_DESIGN;
        auto function =
            talos::designbytecode::functionAt(image, site.function);
        auto intrinsicSite =
            talos::designbytecode::siteAt(image, site.bytecodeSite);
        auto signature = talos::designbytecode::intrinsicAt(
            image, intrinsicSite.intrinsic);
        if (signature.id !=
                TALOS_RT_INTRINSIC_V1_CONTAINER_EXPORT_BITSTREAM ||
            (signature.flags != 3 && signature.flags != 4) ||
            signature.inputCount != 1 || signature.outputCount != 3 ||
            intrinsicSite.inputCount != 1 || intrinsicSite.outputCount != 3)
          return TALOS_RT_INVALID_DESIGN;
        if (bytecodeReferences[site.bytecodeSite] != 1 ||
            bytecodeOwners[site.bytecodeSite] != site.function)
          return TALOS_RT_INVALID_DESIGN;
        for (uint32_t operand = 0;
             operand != intrinsicSite.inputCount + intrinsicSite.outputCount;
             ++operand) {
          auto bindingOperand = talos::designbytecode::operandAt(
              image, intrinsicSite.firstOperand + operand);
          uint32_t reg = operand < intrinsicSite.inputCount
                             ? bindingOperand.second
                             : bindingOperand.first;
          if (!talos::designbytecode::validRegister(function, reg))
            return TALOS_RT_INVALID_DESIGN;
        }
        if (!state->bytecodeSites.emplace(binding, site.id).second)
          return TALOS_RT_INVALID_DESIGN;
      }
      state->sites.push_back(std::move(site));
    }
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }

  std::sort(planRanges.begin(), planRanges.end());
  uint64_t planCursor = planOffset;
  std::pair<uint64_t, uint64_t> previous{UINT64_MAX, UINT64_MAX};
  for (const auto &range : planRanges) {
    if (range == previous)
      continue;
    if (range.first != planCursor || range.second <= range.first)
      return TALOS_RT_INVALID_DESIGN;
    planCursor = range.second;
    previous = range;
  }
  if (planCursor != blobSize)
    return TALOS_RT_INVALID_DESIGN;

  context->classBitstreamState = state.release();
  return TALOS_RT_OK;
}

extern "C" TALOS_RT_FEATURE_TEXT talos_rt_status
talos_rt_v1_class_bitstream_finalize(talos_rt_context *context) {
  TALOS_RT_TRY { return finalizeClassBitstream(context); }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_DESIGN; }
}

TALOS_RT_FEATURE_TEXT uint64_t talos_rt_managed_watch_range(
    talos_rt_object_v1 *object, uint64_t offset, uint64_t size) noexcept {
  TALOS_RT_TRY {
    uint64_t extent = talos_rt_managed_object_extent(object);
    if (!object || size == 0 || offset > UINT64_MAX - size || size > extent ||
        offset > extent - size)
      return 0;
    talos_rt_context *context = talos_rt_managed_object_context(object);
    auto *state = static_cast<ClassBitstreamStateImpl *>(
        context ? context->classBitstreamState : nullptr);
    if (!state)
      return 0;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    uint64_t identity = talos_rt_v1_object_id(object);
    if (identity == 0)
      return 0;
    uint64_t token = talos_rt_v1_managed_watch(
        object, TALOS_RT_MANAGED_WATCH_FIELD, offset);
    if (token == 0)
      return 0;
    auto &ranges = state->watchRanges[identity];
    uint64_t end = offset + size;
    auto duplicate =
        std::find_if(ranges.begin(), ranges.end(), [&](const auto &range) {
          return range.begin == offset && range.end == end &&
                 range.token == token;
        });
    if (duplicate == ranges.end())
      ranges.push_back({offset, end, token});
    return token;
  }
  TALOS_RT_CATCH_ALL { return 0; }
}

namespace {

TALOS_RT_FEATURE_HELPER void
copyBits(uint8_t *destination, uint64_t destinationOffset,
         const uint8_t *source, const uint8_t *sourceUnknown,
         uint64_t sourceOffset, uint64_t width, bool outputFourState,
         uint8_t *outputUnknown) {
  if (((destinationOffset | sourceOffset | width) & 7) == 0) {
    uint8_t *output = destination + destinationOffset / 8;
    source += sourceOffset / 8;
    if (sourceUnknown)
      sourceUnknown += sourceOffset / 8;
    size_t bytes = static_cast<size_t>(width / 8);
    if (!sourceUnknown || outputFourState)
      std::memcpy(output, source, bytes);
    else
      for (size_t byte = 0; byte != bytes; ++byte)
        output[byte] =
            static_cast<uint8_t>(source[byte] & ~sourceUnknown[byte]);
    if (outputFourState && sourceUnknown)
      std::memcpy(outputUnknown + destinationOffset / 8, sourceUnknown, bytes);
    return;
  }
  for (uint64_t bit = 0; bit != width; ++bit) {
    uint64_t sourceBit = sourceOffset + bit;
    uint8_t sourceMask = static_cast<uint8_t>(1u << (sourceBit & 7));
    uint64_t destinationBit = destinationOffset + bit;
    uint8_t destinationMask = static_cast<uint8_t>(1u << (destinationBit & 7));
    bool unknown = sourceUnknown && (sourceUnknown[sourceBit / 8] & sourceMask);
    uint8_t &output = destination[destinationBit / 8];
    if ((source[sourceBit / 8] & sourceMask) && (!unknown || outputFourState))
      output |= destinationMask;
    else
      output &= static_cast<uint8_t>(~destinationMask);
    if (outputFourState && unknown)
      outputUnknown[destinationBit / 8] |= destinationMask;
  }
}

struct View {
  const uint8_t *value = nullptr;
  const uint8_t *unknown = nullptr;
  uint64_t planeSize = 0;
  uint64_t span = 0;
  uint64_t base = 0;
};

TALOS_RT_FEATURE_HELPER bool
readManagedWord(const View &view, uint64_t bitOffset,
                talos_rt_managed_word_v1 &word) {
  if (bitOffset > view.span || view.base > UINT64_MAX - bitOffset)
    return false;
  uint64_t absolute = view.base + bitOffset;
  if ((absolute & 7) != 0 || sizeof(word) * 8 > view.span - bitOffset ||
      absolute / 8 > view.planeSize ||
      sizeof(word) > view.planeSize - absolute / 8)
    return false;
  if (view.unknown) {
    talos_rt_managed_word_v1 unknown = 0;
    std::memcpy(&unknown, view.unknown + absolute / 8, sizeof(unknown));
    if (unknown != 0)
      return false;
  }
  std::memcpy(&word, view.value + absolute / 8, sizeof(word));
  return true;
}

TALOS_RT_FEATURE_HELPER std::optional<uint64_t>
elementSpan(const talos_rt_element_type_v1 *element) {
  if (talos_rt_v1_element_type_validate(element) != TALOS_RT_OK)
    return std::nullopt;
  bool fourState = (element->flags & TALOS_RT_ELEMENT_FOUR_STATE) != 0;
  switch (element->kind) {
  case TALOS_RT_ELEMENT_BITS:
    return fourState ? std::nullopt
                     : std::optional<uint64_t>(element->bit_width);
  case TALOS_RT_ELEMENT_LOGIC:
    return fourState ? std::optional<uint64_t>(element->bit_width)
                     : std::nullopt;
  case TALOS_RT_ELEMENT_AGGREGATE:
    return element->value_size <= UINT64_MAX / 8
               ? std::optional<uint64_t>(element->value_size * 8)
               : std::nullopt;
  case TALOS_RT_ELEMENT_STRING:
  case TALOS_RT_ELEMENT_CONTAINER_HANDLE:
  case TALOS_RT_ELEMENT_CLASS_HANDLE:
    return sizeof(talos_rt_managed_word_v1) * 8;
  default:
    return std::nullopt;
  }
}

struct ContainerLease {
  // Reverse destruction releases buffer, order, then header.
  ManagedObjectLease header;
  ManagedObjectLease order;
  ManagedObjectLease buffer;
  talos_rt_object_v1 *object = nullptr;
  PlanRecord record{};
  uint8_t *data = nullptr;
  const uint64_t *indices = nullptr;
  uint64_t count = 0;
  uint64_t capacity = 0;
  uint64_t head = 0;
  uint64_t stride = 0;
  uint64_t valueOffset = 0;
  uint64_t planeSize = 0;
  bool fourState = false;
  bool physicalAssociative = false;

  ContainerLease() = default;
  ContainerLease(const ContainerLease &) = delete;
  ContainerLease &operator=(const ContainerLease &) = delete;
  ContainerLease(ContainerLease &&) noexcept = default;
  ContainerLease &operator=(ContainerLease &&) noexcept = default;
};

struct Frame {
  enum Kind : uint8_t { Body, Repeat, Container, Object } kind = Body;
  const Plan *plan = nullptr;
  uint64_t index = 0;
  uint64_t end = 0;
  View view{};
  PlanRecord record{};
  uint64_t ordinal = 0;
  uint64_t visited = 0;
  ContainerLease container;
  ManagedObjectLease objectLease;
  talos_rt_object_v1 *object = nullptr;
  const Schema *schema = nullptr;

  Frame() = default;
  Frame(const Frame &) = delete;
  Frame &operator=(const Frame &) = delete;
  Frame(Frame &&) noexcept = default;
  Frame &operator=(Frame &&) noexcept = default;
};

struct Request {
  talos_rt_context *context = nullptr;
  talos_rt_gc_lane_v1 *lane = nullptr;
  const ClassBitstreamStateImpl *state = nullptr;
  uint8_t *value = nullptr;
  uint8_t *unknown = nullptr;
  uint64_t cursor = 0;
  bool fourState = false;
  bool writeOutput = false;
  bool collectAssociative = false;
  bool collectWatches = false;
  bool overflow = false;
  bool needsOrdering = false;
  std::unordered_set<talos_rt_object_v1 *> active{};
  std::unordered_set<talos_rt_object_v1 *> associative{};
  std::unordered_set<uint64_t> watchTokens{};
};

TALOS_RT_FEATURE_HELPER talos_rt_status
acquireContainer(Request &request, talos_rt_object_v1 *container,
                 const PlanRecord &record, ContainerLease &lease) {
  lease.record = record;
  lease.object = container;
  if (!container)
    return TALOS_RT_OK;
  if (request.collectWatches) {
    uint64_t token = talos_rt_v1_managed_watch(
        container, TALOS_RT_MANAGED_WATCH_CONTAINER_SIZE, 0);
    if (token == 0)
      return TALOS_RT_INVALID_HANDLE;
    TALOS_RT_TRY { request.watchTokens.insert(token); }
    TALOS_RT_CATCH(const std::bad_alloc &) {
      return TALOS_RT_OUT_OF_MEMORY;
    }
  }
  talos_rt_status status = talos_rt_managed_object_acquire(
      request.lane, container, TALOS_RT_MANAGED_CONTAINER, &lease.header);
  if (status != TALOS_RT_OK)
    return status;
  if (lease.header.extent != sizeof(ContainerHeader))
    return TALOS_RT_INVALID_HANDLE;
  auto *header = reinterpret_cast<ContainerHeader *>(lease.header.object);
  std::optional<uint64_t> span = elementSpan(header->element);
  bool sequential = header->kind == TALOS_RT_CONTAINER_DYNAMIC_ARRAY ||
                    header->kind == TALOS_RT_CONTAINER_QUEUE;
  bool associative = header->kind == TALOS_RT_CONTAINER_ASSOCIATIVE_ARRAY;
  if ((!sequential && !associative) || header->kind != record.extent || !span ||
      *span != record.sourceSpan || header->element->value_size == 0 ||
      header->size > header->capacity ||
      (header->size != 0 && !header->buffer) ||
      (header->kind == TALOS_RT_CONTAINER_QUEUE && header->capacity != 0 &&
       ((header->capacity & (header->capacity - 1)) != 0 ||
        header->head >= header->capacity)) ||
      (associative && header->capacity != 0 &&
       (header->capacity & (header->capacity - 1)) != 0))
    return TALOS_RT_ARGUMENT_MISMATCH;
  lease.count = header->size;
  lease.capacity = header->capacity;
  lease.head = header->head;
  lease.planeSize = header->element->value_size;
  lease.fourState =
      (header->element->flags & TALOS_RT_ELEMENT_FOUR_STATE) != 0;
  lease.physicalAssociative =
      associative && request.collectAssociative && !header->ordered;
  if (lease.physicalAssociative) {
    request.needsOrdering = true;
    TALOS_RT_TRY { request.associative.insert(container); }
    TALOS_RT_CATCH(const std::bad_alloc &) {
      return TALOS_RT_OUT_OF_MEMORY;
    }
  }
  if (header->size == 0)
    return TALOS_RT_OK;
  if (associative && !lease.physicalAssociative) {
    if (!header->ordered)
      return TALOS_RT_INVALID_HANDLE;
    status = talos_rt_managed_object_acquire(
        request.lane, header->ordered, TALOS_RT_MANAGED_BUFFER, &lease.order);
    if (status != TALOS_RT_OK)
      return status;
    if (lease.order.extent < sizeof(BufferHeader) ||
        reinterpret_cast<BufferHeader *>(lease.order.object)->reserved != 0 ||
        header->size >
            (lease.order.extent - sizeof(BufferHeader)) / sizeof(uint64_t))
      return TALOS_RT_INVALID_HANDLE;
    lease.indices = reinterpret_cast<const uint64_t *>(lease.order.object +
                                                       sizeof(BufferHeader));
  }
  status = talos_rt_managed_object_acquire(
      request.lane, header->buffer, TALOS_RT_MANAGED_BUFFER, &lease.buffer);
  if (status != TALOS_RT_OK)
    return status;
  if (lease.buffer.extent < sizeof(BufferHeader) ||
      reinterpret_cast<BufferHeader *>(lease.buffer.object)->reserved != 0)
    return TALOS_RT_INVALID_HANDLE;
  lease.stride = associative
                     ? assocSlotStride(header->element)
                     : talos::runtime_detail::elementStride(header->element);
  lease.valueOffset = associative ? assocValueOffset(header->element) : 0;
  if (lease.stride == 0 ||
      header->capacity >
          (lease.buffer.extent - sizeof(BufferHeader)) / lease.stride)
    return TALOS_RT_INVALID_HANDLE;
  lease.data = lease.buffer.object + sizeof(BufferHeader);
  return TALOS_RT_OK;
}

TALOS_RT_FEATURE_HELPER void popFrame(Request &request,
                                        std::vector<Frame> &stack) {
  if (stack.back().kind == Frame::Container && stack.back().container.object)
    request.active.erase(stack.back().container.object);
  else if (stack.back().kind == Frame::Object && stack.back().object)
    request.active.erase(stack.back().object);
  stack.pop_back();
}

TALOS_RT_FEATURE_HELPER talos_rt_status
pushActive(Request &request, std::vector<Frame> &stack, Frame frame,
           talos_rt_object_v1 *object) {
  if (object) {
    TALOS_RT_TRY {
      if (!request.active.insert(object).second)
        return TALOS_RT_INVALID_HANDLE;
    }
    TALOS_RT_CATCH(const std::bad_alloc &) {
      return TALOS_RT_OUT_OF_MEMORY;
    }
  }
  TALOS_RT_TRY { stack.push_back(std::move(frame)); }
  TALOS_RT_CATCH(const std::bad_alloc &) {
    request.active.erase(object);
    return TALOS_RT_OUT_OF_MEMORY;
  }
  return TALOS_RT_OK;
}

TALOS_RT_FEATURE_HELPER talos_rt_status
pushObject(Request &request, std::vector<Frame> &stack,
           talos_rt_object_v1 *object, const Group &group) {
  if (!object)
    return TALOS_RT_OK;
  Frame frame;
  frame.kind = Frame::Object;
  frame.object = object;
  talos_rt_status status = talos_rt_managed_object_acquire(
      request.lane, object, TALOS_RT_MANAGED_CLASS, &frame.objectLease);
  if (status != TALOS_RT_OK)
    return status;
  const talos_rt_class_descriptor_v1 *descriptor =
      talos_rt_managed_object_class_descriptor(object);
  if (!descriptor ||
      !std::binary_search(group.members.begin(), group.members.end(),
                          descriptor->class_id))
    return TALOS_RT_INVALID_HANDLE;
  const Schema *schema = request.state->schema(descriptor->class_id);
  if (!schema || schema->descriptor != descriptor)
    return TALOS_RT_INVALID_HANDLE;
  frame.schema = schema;
  if (frame.objectLease.extent != schema->instanceSize)
    return TALOS_RT_INVALID_HANDLE;
  return pushActive(request, stack, std::move(frame), object);
}

TALOS_RT_FEATURE_HELPER talos_rt_status walk(Request &request,
                                                 const Plan &plan,
                                                 const View &root) {
  std::vector<Frame> stack;
  TALOS_RT_TRY { stack.reserve(16); }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  Frame initial;
  initial.kind = Frame::Body;
  initial.plan = &plan;
  initial.end = plan.recordCount;
  initial.view = root;
  stack.push_back(std::move(initial));
  while (!stack.empty()) {
    Frame &frame = stack.back();
    if (frame.kind == Frame::Repeat) {
      if (frame.ordinal == frame.record.extent) {
        popFrame(request, stack);
        continue;
      }
      uint64_t base =
          frame.record.sourceOffset + frame.ordinal++ * frame.record.stride;
      Frame body;
      body.kind = Frame::Body;
      body.plan = frame.plan;
      body.index = frame.index;
      body.end = frame.end;
      body.view = {frame.view.value, frame.view.unknown, frame.view.planeSize,
                   frame.record.sourceSpan, frame.view.base + base};
      TALOS_RT_TRY { stack.push_back(std::move(body)); }
      TALOS_RT_CATCH(const std::bad_alloc &) {
        return TALOS_RT_OUT_OF_MEMORY;
      }
      continue;
    }
    if (frame.kind == Frame::Object) {
      if (frame.ordinal == frame.schema->fields.size()) {
        popFrame(request, stack);
        continue;
      }
      const Field &field = frame.schema->fields[frame.ordinal++];
      bool fieldFourState =
          (field.flags & TALOS_RT_CLASS_BITSTREAM_FIELD_FOUR_STATE) != 0;
      if (request.collectWatches) {
        uint64_t storageSize = field.planeSize * (fieldFourState ? 2 : 1);
        uint64_t token = talos_rt_managed_watch_range(
            frame.object, field.offset, storageSize);
        if (token == 0)
          return TALOS_RT_INVALID_HANDLE;
        TALOS_RT_TRY { request.watchTokens.insert(token); }
        TALOS_RT_CATCH(const std::bad_alloc &) {
          return TALOS_RT_OUT_OF_MEMORY;
        }
      }
      const uint8_t *value = frame.objectLease.object + field.offset;
      Frame body;
      body.kind = Frame::Body;
      body.plan = &field.plan;
      body.end = field.plan.recordCount;
      body.view = {value, fieldFourState ? value + field.planeSize : nullptr,
                   field.planeSize, field.rootSpan, 0};
      TALOS_RT_TRY { stack.push_back(std::move(body)); }
      TALOS_RT_CATCH(const std::bad_alloc &) {
        return TALOS_RT_OUT_OF_MEMORY;
      }
      continue;
    }
    if (frame.kind == Frame::Container) {
      ContainerLease &container = frame.container;
      uint64_t physical = frame.ordinal;
      if (container.physicalAssociative) {
        while (physical != container.capacity &&
               reinterpret_cast<const AssocSlot *>(container.data +
                                                   physical * container.stride)
                       ->hash == 0)
          ++physical;
        frame.ordinal = physical;
      }
      uint64_t limit =
          container.physicalAssociative ? container.capacity : container.count;
      if (frame.ordinal == limit) {
        if (frame.visited != container.count)
          return TALOS_RT_INVALID_HANDLE;
        popFrame(request, stack);
        continue;
      }
      uint64_t ordinal = frame.ordinal++;
      if (container.record.extent == TALOS_RT_CONTAINER_QUEUE)
        physical = (container.head + ordinal) & (container.capacity - 1);
      else if (container.record.extent ==
                   TALOS_RT_CONTAINER_ASSOCIATIVE_ARRAY &&
               !container.physicalAssociative) {
        physical = container.indices[ordinal];
        if (physical >= container.capacity ||
            reinterpret_cast<const AssocSlot *>(container.data +
                                                physical * container.stride)
                    ->hash == 0)
          return TALOS_RT_INVALID_HANDLE;
      } else
        physical = ordinal;
      ++frame.visited;
      uint8_t *element =
          container.data + physical * container.stride + container.valueOffset;
      Frame body;
      body.kind = Frame::Body;
      body.plan = frame.plan;
      body.index = frame.index;
      body.end = frame.end;
      body.view = {element,
                   container.fourState ? element + container.planeSize
                                       : nullptr,
                   container.planeSize, container.record.sourceSpan, 0};
      TALOS_RT_TRY { stack.push_back(std::move(body)); }
      TALOS_RT_CATCH(const std::bad_alloc &) {
        return TALOS_RT_OUT_OF_MEMORY;
      }
      continue;
    }
    if (frame.index == frame.end) {
      popFrame(request, stack);
      continue;
    }
    PlanRecord record = readRecord(frame.plan->records, frame.index++);
    if (record.opcode == TALOS_RT_RECURSIVE_BITSTREAM_COPY) {
      if (record.extent > request.cursor) {
        request.overflow = true;
        return TALOS_RT_OK;
      }
      request.cursor -= record.extent;
      if (request.writeOutput)
        copyBits(request.value, request.cursor, frame.view.value,
                 frame.view.unknown, frame.view.base + record.sourceOffset,
                 record.extent, request.fourState, request.unknown);
      continue;
    }
    if (record.opcode == TALOS_RT_RECURSIVE_BITSTREAM_STRING) {
      talos_rt_managed_word_v1 string = 0;
      if (!readManagedWord(frame.view, record.sourceOffset, string))
        return TALOS_RT_INVALID_HANDLE;
      ManagedObjectLease stringLease;
      talos_rt_object_v1 *stringObject =
          talos_rt_object_from_managed_word(string);
      if (string != 0 && stringObject) {
        talos_rt_status acquire = talos_rt_managed_object_acquire(
            request.lane, stringObject, TALOS_RT_MANAGED_STRING,
            &stringLease);
        if (acquire != TALOS_RT_OK)
          return acquire;
      } else if (talos_rt_validate_string(request.context, string) !=
                 TALOS_RT_OK) {
        return TALOS_RT_INVALID_HANDLE;
      }
      char scratch[8]{};
      const char *bytes = nullptr;
      uint64_t size = 0;
      talos_rt_status status =
          talos_rt_v1_string_view(string, scratch, &bytes, &size);
      if (status != TALOS_RT_OK)
        return status;
      if (size > request.cursor / 8) {
        request.overflow = true;
        return TALOS_RT_OK;
      }
      if (!request.writeOutput) {
        request.cursor -= size * 8;
        continue;
      }
      for (uint64_t ordinal = 0; ordinal != size; ++ordinal) {
        request.cursor -= 8;
        copyBits(request.value, request.cursor,
                 reinterpret_cast<const uint8_t *>(bytes + ordinal), nullptr, 0,
                 8, request.fourState, request.unknown);
      }
      continue;
    }
    if (record.opcode == TALOS_RT_RECURSIVE_BITSTREAM_OBJECT) {
      talos_rt_managed_word_v1 word = 0;
      if (!readManagedWord(frame.view, record.sourceOffset, word))
        return TALOS_RT_INVALID_HANDLE;
      talos_rt_object_v1 *object = talos_rt_object_from_managed_word(word);
      if (word != talos_rt_managed_word_from_object(object))
        return TALOS_RT_INVALID_HANDLE;
      const Group *group = request.state->group(record.extent);
      if (!group)
        return TALOS_RT_INVALID_DESIGN;
      talos_rt_status status = pushObject(request, stack, object, *group);
      if (status != TALOS_RT_OK)
        return status;
      continue;
    }
    uint64_t bodyBegin = frame.index;
    uint64_t bodyEnd = bodyBegin + record.bodyRecords;
    frame.index = bodyEnd;
    if (record.opcode == TALOS_RT_RECURSIVE_BITSTREAM_REPEAT) {
      Frame repeat;
      repeat.kind = Frame::Repeat;
      repeat.plan = frame.plan;
      repeat.index = bodyBegin;
      repeat.end = bodyEnd;
      repeat.view = frame.view;
      repeat.record = record;
      TALOS_RT_TRY { stack.push_back(std::move(repeat)); }
      TALOS_RT_CATCH(const std::bad_alloc &) {
        return TALOS_RT_OUT_OF_MEMORY;
      }
      continue;
    }
    if (record.opcode != TALOS_RT_RECURSIVE_BITSTREAM_CONTAINER)
      return TALOS_RT_INVALID_DESIGN;
    talos_rt_managed_word_v1 word = 0;
    if (!readManagedWord(frame.view, record.sourceOffset, word))
      return TALOS_RT_INVALID_HANDLE;
    talos_rt_object_v1 *container = talos_rt_object_from_managed_word(word);
    if (word != talos_rt_managed_word_from_object(container))
      return TALOS_RT_INVALID_HANDLE;
    Frame elements;
    elements.kind = Frame::Container;
    elements.plan = frame.plan;
    elements.index = bodyBegin;
    elements.end = bodyEnd;
    elements.container.object = container;
    talos_rt_status status =
        pushActive(request, stack, std::move(elements), container);
    if (status != TALOS_RT_OK)
      return status;
    status =
        acquireContainer(request, container, record, stack.back().container);
    if (status != TALOS_RT_OK)
      popFrame(request, stack);
    if (status != TALOS_RT_OK)
      return status;
  }
  return TALOS_RT_OK;
}

TALOS_RT_FEATURE_HELPER talos_rt_status createWatchGroup(
    talos_rt_context *context, ClassBitstreamStateImpl &state,
    const std::unordered_set<uint64_t> &tokens, uint64_t &outWatch) {
  outWatch = 0;
  if (tokens.empty())
    return TALOS_RT_OK;
  std::vector<uint64_t> members;
  TALOS_RT_TRY { members.assign(tokens.begin(), tokens.end()); }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  std::sort(members.begin(), members.end());
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  if (state.nextWatchGroup == 0 || state.nextWatchGroup >= kClassWatchGroupBit)
    return TALOS_RT_OUT_OF_RESOURCES;
  uint64_t token = kClassWatchGroupBit | state.nextWatchGroup++;
  TALOS_RT_TRY {
    if (!state.watchGroups.emplace(token, std::move(members)).second)
      return TALOS_RT_INVALID_LIFECYCLE;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  outWatch = token;
  return TALOS_RT_OK;
}

TALOS_RT_FEATURE_HELPER talos_rt_status exportAtSite(
    talos_rt_context *context, const Site &site, const void *inputValue,
    const void *inputUnknown, uint64_t inputPlaneSize, uint32_t inputFourState,
    void *outValue, void *outUnknown, uint64_t outputPlaneSize,
    uint64_t outputBitWidth, uint32_t outputFourState, uint32_t observe,
    uint32_t *outMatched, uint64_t *outWatch) {
  auto *state = static_cast<ClassBitstreamStateImpl *>(
      context ? context->classBitstreamState : nullptr);
  if (!state)
    return TALOS_RT_INVALID_LIFECYCLE;
  ContextTransaction transaction(context);
  ManagedExecutionScope managed(context);
  if (managed.getStatus() != TALOS_RT_OK)
    return managed.getStatus();
  talos_rt_gc_lane_v1 *lane = managed.getLane();
  if (!lane || talos_rt_managed_lane_context(lane) != context)
    return TALOS_RT_INVALID_LIFECYCLE;
  View rootView{static_cast<const uint8_t *>(inputValue),
                inputFourState ? static_cast<const uint8_t *>(inputUnknown)
                               : nullptr,
                inputPlaneSize, site.plan.rootSpan, 0};
  Request request{context,
                  lane,
                  state,
                  nullptr,
                  nullptr,
                  outputBitWidth,
                  outputFourState != 0};
  request.collectAssociative = true;
  talos_rt_status status = walk(request, site.plan, rootView);
  if (status != TALOS_RT_OK)
    return status;
  if (request.overflow || request.cursor != 0) {
    std::memset(outValue, 0, static_cast<size_t>(outputPlaneSize));
    if (outputFourState)
      std::memset(outUnknown, 0, static_cast<size_t>(outputPlaneSize));
    *outMatched = 0;
    *outWatch = 0;
    return TALOS_RT_OK;
  }
  for (talos_rt_object_v1 *array : request.associative) {
    status = ensureAssocOrderedWithoutSafepoint(lane, array);
    if (status != TALOS_RT_OK)
      return status;
  }
  std::vector<uint8_t> value;
  std::vector<uint8_t> unknown;
  TALOS_RT_TRY {
    value.resize(static_cast<size_t>(outputPlaneSize));
    if (outputFourState)
      unknown.resize(static_cast<size_t>(outputPlaneSize));
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  request.value = value.data();
  request.unknown = outputFourState ? unknown.data() : nullptr;
  std::fill(value.begin(), value.end(), uint8_t{0});
  std::fill(unknown.begin(), unknown.end(), uint8_t{0});
  request.cursor = outputBitWidth;
  request.overflow = false;
  request.collectAssociative = false;
  request.collectWatches = observe != 0;
  request.writeOutput = true;
  request.needsOrdering = false;
  request.associative.clear();
  request.watchTokens.clear();
  status = walk(request, site.plan, rootView);
  if (status != TALOS_RT_OK || request.overflow || request.cursor != 0)
    return status != TALOS_RT_OK ? status : TALOS_RT_INVALID_HANDLE;
  uint64_t preparedWatch = 0;
  if (observe) {
    status =
        createWatchGroup(context, *state, request.watchTokens, preparedWatch);
    if (status != TALOS_RT_OK)
      return status;
  }
  std::memcpy(outValue, value.data(), static_cast<size_t>(outputPlaneSize));
  if (outputFourState)
    std::memcpy(outUnknown, unknown.data(),
                static_cast<size_t>(outputPlaneSize));
  *outMatched = 1;
  *outWatch = preparedWatch;
  return TALOS_RT_OK;
}

} // namespace

TALOS_RT_FEATURE_HELPER talos_rt_status exportRecursiveV2(
    talos_rt_context *context, uint64_t siteID, const void *inputValue,
    const void *inputUnknown, uint64_t inputPlaneSize, uint64_t inputBitWidth,
    uint32_t inputFourState, void *outValue, void *outUnknown,
    uint64_t outputPlaneSize, uint64_t outputBitWidth, uint32_t outputFourState,
    uint32_t observe, uint32_t *outMatched, uint64_t *outWatch) {
  auto *state = static_cast<ClassBitstreamStateImpl *>(
      context ? context->classBitstreamState : nullptr);
  const Site *site = state ? state->site(siteID) : nullptr;
  if (!context || !site || !inputValue || inputPlaneSize == 0 ||
      inputPlaneSize > SIZE_MAX || inputPlaneSize >= kResourceLimit ||
      inputBitWidth != site->plan.rootSpan || inputFourState > 1 ||
      (inputFourState && !inputUnknown) || !outValue || outputPlaneSize == 0 ||
      outputPlaneSize > SIZE_MAX || outputPlaneSize >= kResourceLimit ||
      outputBitWidth == 0 || outputFourState > 1 || observe > 1 ||
      outputPlaneSize != outputBitWidth / 8 + ((outputBitWidth & 7) != 0) ||
      (outputFourState && !outUnknown) || !outMatched || !outWatch)
    return TALOS_RT_INVALID_ARGUMENT;
  if (inputPlaneSize < inputBitWidth / 8 + ((inputBitWidth & 7) != 0))
    return TALOS_RT_INVALID_ARGUMENT;
  struct Range {
    uintptr_t begin = 0;
    uintptr_t end = 0;
  } ranges[6];
  size_t rangeCount = 0;
  auto appendRange = [&](const void *pointer, uint64_t size) {
    Range range;
    if (!checkedPointerRange(pointer, size, range.begin, range.end))
      return false;
    for (size_t index = 0; index != rangeCount; ++index)
      if (overlaps(range.begin, range.end, ranges[index].begin,
                   ranges[index].end))
        return false;
    ranges[rangeCount++] = range;
    return true;
  };
  if (!appendRange(inputValue, inputPlaneSize) ||
      (inputFourState && !appendRange(inputUnknown, inputPlaneSize)) ||
      !appendRange(outValue, outputPlaneSize) ||
      (outputFourState && !appendRange(outUnknown, outputPlaneSize)) ||
      !appendRange(outMatched, sizeof(*outMatched)) ||
      !appendRange(outWatch, sizeof(*outWatch)))
    return TALOS_RT_INVALID_ARGUMENT;
  uintptr_t blobBegin = 0, blobEnd = 0;
  if (!checkedPointerRange(state->blob, state->blobSize, blobBegin, blobEnd))
    return TALOS_RT_INVALID_LIFECYCLE;
  size_t firstWritable = 1 + static_cast<size_t>(inputFourState != 0);
  for (size_t index = firstWritable; index != rangeCount; ++index)
    if (overlaps(ranges[index].begin, ranges[index].end, blobBegin, blobEnd))
      return TALOS_RT_INVALID_ARGUMENT;
  return exportAtSite(context, *site, inputValue, inputUnknown, inputPlaneSize,
                      inputFourState, outValue, outUnknown, outputPlaneSize,
                      outputBitWidth, outputFourState, observe, outMatched,
                      outWatch);
}

extern "C" TALOS_RT_FEATURE_TEXT talos_rt_status
talos_rt_v2_recursive_export_bitstream(
    talos_rt_context *context, uint64_t siteID, const void *inputValue,
    const void *inputUnknown, uint64_t inputPlaneSize, uint64_t inputBitWidth,
    uint32_t inputFourState, void *outValue, void *outUnknown,
    uint64_t outputPlaneSize, uint64_t outputBitWidth, uint32_t outputFourState,
    uint32_t observe, uint32_t *outMatched, uint64_t *outWatch) {
  TALOS_RT_TRY {
    return exportRecursiveV2(
        context, siteID, inputValue, inputUnknown, inputPlaneSize,
        inputBitWidth, inputFourState, outValue, outUnknown, outputPlaneSize,
        outputBitWidth, outputFourState, observe, outMatched, outWatch);
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_ARGUMENT; }
}

TALOS_RT_FEATURE_TEXT talos_rt_status
talos_rt_class_bitstream_export_bytecode(
    talos_rt_context *context, uint32_t function, uint32_t bytecodeSite,
    const void *inputValue, const void *inputUnknown, uint64_t inputPlaneSize,
    uint64_t inputBitWidth, uint32_t inputFourState, void *outValue,
    void *outUnknown, uint64_t outputPlaneSize, uint64_t outputBitWidth,
    uint32_t outputFourState, uint32_t observe, uint32_t *outMatched,
    uint64_t *outWatch) {
  auto *state = static_cast<ClassBitstreamStateImpl *>(
      context ? context->classBitstreamState : nullptr);
  if (!state)
    return TALOS_RT_INVALID_LIFECYCLE;
  uint64_t key = uint64_t{function} << 32 | bytecodeSite;
  auto found = state->bytecodeSites.find(key);
  const Site *binding = found == state->bytecodeSites.end()
                            ? nullptr
                            : state->site(found->second);
  if (!binding)
    return TALOS_RT_INVALID_BYTECODE;
  return talos_rt_v2_recursive_export_bitstream(
      context, binding->id, inputValue, inputUnknown, inputPlaneSize,
      inputBitWidth, inputFourState, outValue, outUnknown, outputPlaneSize,
      outputBitWidth, outputFourState, observe, outMatched, outWatch);
}

TALOS_RT_FEATURE_TEXT bool
talos_rt_expand_class_watch_group(talos_rt_context *context, uint64_t token,
                                    RecursiveWatchGroupVisit visit,
                                    void *environment) {
  if (!context || (token & kClassWatchGroupBit) == 0 ||
      (token & kRecursiveWatchGroupBit) != 0 || !visit)
    return false;
  auto *state =
      static_cast<ClassBitstreamStateImpl *>(context->classBitstreamState);
  if (!state)
    return false;
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  auto found = state->watchGroups.find(token);
  if (found == state->watchGroups.end())
    return false;
  std::vector<uint64_t> members = std::move(found->second);
  state->watchGroups.erase(found);
  for (uint64_t member : members)
    if (!visit(environment, member))
      return false;
  return true;
}
