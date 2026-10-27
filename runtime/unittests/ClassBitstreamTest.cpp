//===- ClassBitstreamTest.cpp - Class bit-stream runtime tests ----------===//

#include "../lib/RuntimeInternal.h"
#include "talos/Runtime/Runtime.h"

#include "gtest/gtest.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr uint64_t kPlanHeaderSize = 32;

struct Blob {
  std::vector<uint64_t> words;

  uint8_t *data() { return reinterpret_cast<uint8_t *>(words.data()); }
  const uint8_t *data() const {
    return reinterpret_cast<const uint8_t *>(words.data());
  }
  uint64_t size() const { return words.size() * sizeof(uint64_t); }
};

template <typename T> void put(Blob &blob, uint64_t offset, const T &value) {
  ASSERT_LE(offset + sizeof(value), blob.size());
  std::memcpy(blob.data() + offset, &value, sizeof(value));
}

void put64(Blob &blob, uint64_t offset, uint64_t value) {
  put(blob, offset, value);
}

void putPlan(Blob &blob, uint64_t offset, uint32_t version, uint64_t rootSpan,
             uint32_t opcode, uint64_t extent, uint64_t outputWidth) {
  put64(blob, offset,
        uint64_t{TALOS_RT_RECURSIVE_BITSTREAM_PLAN_MAGIC} |
            (uint64_t{version} << 32));
  put64(blob, offset + 8, 1);
  put64(blob, offset + 16, rootSpan);
  put64(blob, offset + 24, 0);
  put64(blob, offset + kPlanHeaderSize, opcode);
  put64(blob, offset + kPlanHeaderSize + 8, 0);
  put64(blob, offset + kPlanHeaderSize + 16, extent);
  put64(blob, offset + kPlanHeaderSize + 24, 0);
  put64(blob, offset + kPlanHeaderSize + 32,
        opcode == TALOS_RT_RECURSIVE_BITSTREAM_OBJECT
            ? sizeof(talos_rt_managed_word_v1) * 8
            : 0);
  put64(blob, offset + kPlanHeaderSize + 40, outputWidth);
}

Blob makeBlob(uint64_t staticClassID, uint64_t memberClassID,
              uint64_t instanceSize, uint32_t instanceAlignment,
              uint64_t fieldOffset, uint64_t fieldPlaneSize,
              uint64_t fieldRootSpan, uint32_t fieldFlags, bool fieldIsObject) {
  constexpr uint64_t siteOffset = 128;
  constexpr uint64_t groupOffset = siteOffset + 40;
  constexpr uint64_t memberOffset = groupOffset + 40;
  constexpr uint64_t schemaOffset = memberOffset + 8;
  constexpr uint64_t fieldOffsetTable = schemaOffset + 48;
  constexpr uint64_t planOffset = fieldOffsetTable + 48;
  constexpr uint64_t fieldPlanOffset = planOffset;
  constexpr uint64_t sitePlanOffset = fieldPlanOffset + 80;
  constexpr uint64_t blobSize = sitePlanOffset + 80;
  Blob blob{std::vector<uint64_t>(blobSize / 8)};
  talos_rt_class_bitstream_header_v1 header{
      uint64_t{TALOS_RT_CLASS_BITSTREAM_BLOB_MAGIC} |
          (uint64_t{TALOS_RT_CLASS_BITSTREAM_BLOB_VERSION} << 32),
      blobSize,
      siteOffset,
      1,
      groupOffset,
      1,
      memberOffset,
      1,
      schemaOffset,
      1,
      fieldOffsetTable,
      1,
      planOffset,
      blobSize - planOffset,
      0,
      0};
  put(blob, 0, header);
  put(blob, siteOffset,
      talos_rt_class_bitstream_site_v1{
          1, TALOS_RT_CLASS_BITSTREAM_NO_BYTECODE,
          TALOS_RT_CLASS_BITSTREAM_NO_BYTECODE, sitePlanOffset, 80, 0});
  put(blob, groupOffset,
      talos_rt_class_bitstream_group_v1{1, staticClassID, 0, 1, 0, 0});
  put64(blob, memberOffset, memberClassID);
  put(blob, schemaOffset,
      talos_rt_class_bitstream_schema_v1{memberClassID, instanceSize, 0, 1,
                                           instanceAlignment, 0, 0});
  put(blob, fieldOffsetTable,
      talos_rt_class_bitstream_field_v1{fieldOffset, fieldPlaneSize,
                                          fieldRootSpan, fieldPlanOffset, 80,
                                          fieldFlags, 1});
  putPlan(blob, fieldPlanOffset,
          fieldIsObject ? TALOS_RT_RECURSIVE_BITSTREAM_PLAN_OBJECT_VERSION
                        : TALOS_RT_RECURSIVE_BITSTREAM_PLAN_VERSION,
          fieldRootSpan,
          fieldIsObject ? TALOS_RT_RECURSIVE_BITSTREAM_OBJECT
                        : TALOS_RT_RECURSIVE_BITSTREAM_COPY,
          fieldIsObject ? 1 : fieldRootSpan, fieldIsObject ? 0 : fieldRootSpan);
  putPlan(blob, sitePlanOffset,
          TALOS_RT_RECURSIVE_BITSTREAM_PLAN_OBJECT_VERSION, 64,
          TALOS_RT_RECURSIVE_BITSTREAM_OBJECT, 1, 0);
  return blob;
}

Blob makeEmptyGroupBlob(uint64_t staticClassID) {
  constexpr uint64_t siteOffset = 128;
  constexpr uint64_t groupOffset = siteOffset + 40;
  constexpr uint64_t planOffset = groupOffset + 40;
  constexpr uint64_t blobSize = planOffset + 80;
  Blob blob{std::vector<uint64_t>(blobSize / 8)};
  put(blob, 0,
      talos_rt_class_bitstream_header_v1{
          uint64_t{TALOS_RT_CLASS_BITSTREAM_BLOB_MAGIC} |
              (uint64_t{TALOS_RT_CLASS_BITSTREAM_BLOB_VERSION} << 32),
          blobSize, siteOffset, 1, groupOffset, 1, planOffset, 0, planOffset, 0,
          planOffset, 0, planOffset, 80, 0, 0});
  put(blob, siteOffset,
      talos_rt_class_bitstream_site_v1{
          1, TALOS_RT_CLASS_BITSTREAM_NO_BYTECODE,
          TALOS_RT_CLASS_BITSTREAM_NO_BYTECODE, planOffset, 80, 0});
  put(blob, groupOffset,
      talos_rt_class_bitstream_group_v1{1, staticClassID, 0, 0, 0, 0});
  putPlan(blob, planOffset, TALOS_RT_RECURSIVE_BITSTREAM_PLAN_OBJECT_VERSION,
          64, TALOS_RT_RECURSIVE_BITSTREAM_OBJECT, 1, 0);
  return blob;
}

Blob makeWideSharedCycleBlob(uint64_t classCount) {
  const uint64_t siteOffset = 128;
  const uint64_t groupOffset = siteOffset + 40;
  const uint64_t memberOffset = groupOffset + 40;
  const uint64_t schemaOffset = memberOffset + classCount * 8;
  const uint64_t fieldOffset = schemaOffset + classCount * 48;
  const uint64_t planOffset = fieldOffset + classCount * 48;
  const uint64_t blobSize = planOffset + 80;
  Blob blob{std::vector<uint64_t>(blobSize / 8)};
  put(blob, 0,
      talos_rt_class_bitstream_header_v1{
          uint64_t{TALOS_RT_CLASS_BITSTREAM_BLOB_MAGIC} |
              (uint64_t{TALOS_RT_CLASS_BITSTREAM_BLOB_VERSION} << 32),
          blobSize, siteOffset, 1, groupOffset, 1, memberOffset, classCount,
          schemaOffset, classCount, fieldOffset, classCount, planOffset, 80, 0,
          0});
  put(blob, siteOffset,
      talos_rt_class_bitstream_site_v1{
          1, TALOS_RT_CLASS_BITSTREAM_NO_BYTECODE,
          TALOS_RT_CLASS_BITSTREAM_NO_BYTECODE, planOffset, 80, 0});
  put(blob, groupOffset,
      talos_rt_class_bitstream_group_v1{1, 1, 0, classCount, 0, 0});
  for (uint64_t index = 0; index != classCount; ++index) {
    put64(blob, memberOffset + index * 8, index + 1);
    put(blob, schemaOffset + index * 48,
        talos_rt_class_bitstream_schema_v1{index + 1, 16, index, 1,
                                             alignof(void *), 0, 0});
    put(blob, fieldOffset + index * 48,
        talos_rt_class_bitstream_field_v1{sizeof(void *), sizeof(void *), 64,
                                            planOffset, 80, 0,
                                            alignof(void *)});
  }
  putPlan(blob, planOffset, TALOS_RT_RECURSIVE_BITSTREAM_PLAN_OBJECT_VERSION,
          64, TALOS_RT_RECURSIVE_BITSTREAM_OBJECT, 1, 0);
  return blob;
}

struct Execution {
  talos_rt_execution_descriptor_v1 descriptor{};
  talos_rt_execution_extension_v1 extension{};

  Execution() = default;
  explicit Execution(const uint8_t *blob, uint64_t size) {
    descriptor.version = TALOS_RT_VERSION;
    descriptor.flags = TALOS_RT_EXECUTION_CLASS_BITSTREAM;
    descriptor.reserved = sizeof(descriptor);
    extension.version = TALOS_RT_EXECUTION_EXTENSION_VERSION;
    extension.size = sizeof(extension);
    extension.class_bitstream = blob;
    extension.class_bitstream_size = size;
  }
};
static_assert(offsetof(Execution, extension) ==
              sizeof(talos_rt_execution_descriptor_v1));

struct ClassInfo {
  talos_rt_trace_layout_v1 layout{};
  talos_rt_trace_entry_v1 entry{};
  talos_rt_class_descriptor_v1 descriptor{};

  ClassInfo(uint64_t id, uint32_t flags, uint64_t size,
            const talos_rt_class_descriptor_v1 *base = nullptr,
            bool objectField = false) {
    if (objectField)
      entry = {sizeof(void *),
               0,
               1,
               TALOS_RT_TRACE_STRONG,
               TALOS_RT_MANAGED_SLOT_CLASS,
               nullptr};
    if (base && base->layout && base->layout->entry_count) {
      entry = {0,
               0,
               1,
               TALOS_RT_TRACE_EMBEDDED,
               TALOS_RT_MANAGED_SLOT_INVALID,
               base->layout};
      objectField = true;
    }
    layout = {TALOS_RT_VERSION,
              0,
              size,
              alignof(void *),
              objectField ? &entry : nullptr,
              objectField ? 1u : 0u};
    descriptor.version = TALOS_RT_VERSION;
    descriptor.flags = flags;
    descriptor.class_id = id;
    descriptor.instance_size = size;
    descriptor.instance_alignment = alignof(void *);
    descriptor.base = base;
    descriptor.layout = &layout;
  }
};

talos_rt_status finalize(const Blob &blob,
                           const std::vector<ClassInfo *> &classes,
                           talos_rt_context **outContext = nullptr,
                           Execution *retainedExecution = nullptr) {
  Execution localExecution(blob.data(), blob.size());
  if (retainedExecution)
    *retainedExecution = localExecution;
  Execution &execution =
      retainedExecution ? *retainedExecution : localExecution;
  talos_rt_context *context = nullptr;
  talos_rt_status status =
      talos_rt_v1_context_create_for_design(&execution.descriptor, &context);
  if (status == TALOS_RT_OK)
    for (ClassInfo *info : classes) {
      status = talos_rt_v1_class_register(context, &info->descriptor);
      if (status != TALOS_RT_OK)
        break;
    }
  if (status == TALOS_RT_OK)
    status = talos_rt_v1_class_bitstream_finalize(context);
  if (outContext && status == TALOS_RT_OK)
    *outContext = context;
  else
    talos_rt_v1_context_destroy(context);
  return status;
}

TEST(ClassBitstreamTest, RejectsHostileWireImages) {
  ClassInfo leaf(2, TALOS_RT_CLASS_FINAL, 16);
  Blob valid =
      makeBlob(2, 2, 16, alignof(void *), sizeof(void *), 1, 8, 0, false);
  EXPECT_EQ(finalize(valid, {&leaf}), TALOS_RT_OK);

  Blob reserved = valid;
  put64(reserved, 112, 1);
  EXPECT_EQ(finalize(reserved, {&leaf}), TALOS_RT_INVALID_DESIGN);

  Blob headerLeak = valid;
  auto *header = reinterpret_cast<talos_rt_class_bitstream_header_v1 *>(
      headerLeak.data());
  auto *field = reinterpret_cast<talos_rt_class_bitstream_field_v1 *>(
      headerLeak.data() + header->field_offset);
  field->offset = 0;
  EXPECT_EQ(finalize(headerLeak, {&leaf}), TALOS_RT_INVALID_DESIGN);

  Blob tail = valid;
  tail.words.push_back(0);
  auto *tailHeader =
      reinterpret_cast<talos_rt_class_bitstream_header_v1 *>(tail.data());
  tailHeader->size = tail.size();
  tailHeader->plan_size += 8;
  EXPECT_EQ(finalize(tail, {&leaf}), TALOS_RT_INVALID_DESIGN);

  std::vector<uint8_t> unaligned(valid.size() + 8);
  const uintptr_t base = reinterpret_cast<uintptr_t>(unaligned.data());
  const size_t alignedOffset = (8 - (base & 7)) & 7;
  const size_t unalignedOffset = alignedOffset == 7 ? 1 : alignedOffset + 1;
  ASSERT_NE((base + unalignedOffset) & 7, 0u);
  std::memcpy(unaligned.data() + unalignedOffset, valid.data(), valid.size());
  Execution execution(unaligned.data() + unalignedOffset, valid.size());
  talos_rt_context *context = nullptr;
  ASSERT_EQ(
      talos_rt_v1_context_create_for_design(&execution.descriptor, &context),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_class_register(context, &leaf.descriptor),
            TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_class_bitstream_finalize(context),
            TALOS_RT_INVALID_DESIGN);
  talos_rt_v1_context_destroy(context);
}

TEST(ClassBitstreamTest, EmptyAbstractGroupHandlesNullAndRejectsObject) {
  ClassInfo abstractClass(10, TALOS_RT_CLASS_ABSTRACT, sizeof(void *));
  ClassInfo derived(11, TALOS_RT_CLASS_FINAL, sizeof(void *),
                    &abstractClass.descriptor);
  Blob blob = makeEmptyGroupBlob(10);
  talos_rt_context *context = nullptr;
  Execution execution;
  ASSERT_EQ(finalize(blob, {&abstractClass, &derived}, &context, &execution),
            TALOS_RT_OK);
  talos_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_lane_create(context, &lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_enter(lane), TALOS_RT_OK);

  talos_rt_managed_word_v1 input = 0;
  uint8_t output = 0xff;
  uint32_t matched = 1;
  uint64_t watch = 7;
  EXPECT_EQ(talos_rt_v2_recursive_export_bitstream(
                context, 1, &input, nullptr, sizeof(input), 64, 0, &output,
                nullptr, 1, 8, 0, 0, &matched, &watch),
            TALOS_RT_OK);
  EXPECT_EQ(output, 0);
  EXPECT_EQ(matched, 0u);
  EXPECT_EQ(watch, 0u);

  talos_rt_object_v1 *object = nullptr;
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &derived.descriptor, &object),
            TALOS_RT_OK);
  input = static_cast<talos_rt_managed_word_v1>(
      reinterpret_cast<uintptr_t>(object));
  output = 0x5a;
  matched = 9;
  watch = 9;
  EXPECT_EQ(talos_rt_v2_recursive_export_bitstream(
                context, 1, &input, nullptr, sizeof(input), 64, 0, &output,
                nullptr, 1, 8, 0, 0, &matched, &watch),
            TALOS_RT_INVALID_HANDLE);
  EXPECT_EQ(output, 0x5a);
  EXPECT_EQ(matched, 9u);
  EXPECT_EQ(watch, 9u);

  EXPECT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);
}

TEST(ClassBitstreamTest, ExportsFourStateFieldAndRejectsStaticCycles) {
  ClassInfo leaf(2, TALOS_RT_CLASS_FINAL, 16);
  Blob blob = makeBlob(2, 2, 16, alignof(void *), sizeof(void *), 2, 8,
                       TALOS_RT_CLASS_BITSTREAM_FIELD_FOUR_STATE, false);
  talos_rt_context *context = nullptr;
  Execution execution;
  ASSERT_EQ(finalize(blob, {&leaf}, &context, &execution), TALOS_RT_OK);
  talos_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_lane_create(context, &lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_enter(lane), TALOS_RT_OK);
  talos_rt_object_v1 *object = nullptr;
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &leaf.descriptor, &object),
            TALOS_RT_OK);
  const uint8_t value[2] = {0xa5, 0x11}, unknown[2] = {0x3c, 0x22};
  ASSERT_EQ(talos_rt_v1_object_write_planes(object, sizeof(void *), value,
                                              unknown, 2),
            TALOS_RT_OK);
  talos_rt_managed_word_v1 input = static_cast<talos_rt_managed_word_v1>(
      reinterpret_cast<uintptr_t>(object));
  uint8_t output = 0, outputUnknown = 0;
  uint32_t matched = 0;
  uint64_t watch = 0;
  ASSERT_EQ(talos_rt_v2_recursive_export_bitstream(
                context, 1, &input, nullptr, sizeof(input), 64, 0, &output,
                &outputUnknown, 1, 8, 1, 1, &matched, &watch),
            TALOS_RT_OK);
  EXPECT_EQ(output, value[0]);
  EXPECT_EQ(outputUnknown, unknown[0]);
  EXPECT_EQ(matched, 1u);
  EXPECT_NE(watch, 0u);

  struct {
    talos_rt_wait_record_v1 wait;
    talos_rt_wait_entry_v1 entry;
  } record{{TALOS_RT_VERSION, TALOS_RT_SUSPEND_CHANGE, 0, 1, 0, 0},
           {watch, TALOS_RT_WAIT_EDGE_CHANGE, TALOS_RT_WAIT_WIDTH_MANAGED}};
  std::vector<std::unique_ptr<SignalSubscription>> subscriptions;
  std::unique_ptr<SignalWaitLatch> latch;
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    ASSERT_TRUE(talos_rt_register_signal_wait_unlocked(context, &record.wait,
                                                         subscriptions, latch));
  }
  const uint8_t overlap = 0x44;
  ASSERT_EQ(talos_rt_v1_object_write(object, sizeof(void *) + 1, &overlap, 1),
            TALOS_RT_OK);
  EXPECT_TRUE(latch->triggered);
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    talos_rt_unregister_signal_wait_unlocked(context, subscriptions);
  }
  EXPECT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);

  ClassInfo node(3, TALOS_RT_CLASS_FINAL, 16, nullptr, true);
  Blob cycleBlob =
      makeBlob(3, 3, 16, alignof(void *), sizeof(void *), 8, 64, 0, true);
  EXPECT_EQ(finalize(cycleBlob, {&node}), TALOS_RT_INVALID_DESIGN);
}

TEST(ClassBitstreamTest, RejectsWideSharedPlanCycleWithoutCartesianExpansion) {
  constexpr uint64_t classCount = 2048;
  Blob blob = makeWideSharedCycleBlob(classCount);
  std::vector<ClassInfo> classes;
  classes.reserve(classCount);
  classes.emplace_back(1, 0, 16, nullptr, true);
  for (uint64_t id = 2; id <= classCount; ++id)
    classes.emplace_back(id, TALOS_RT_CLASS_FINAL, 16,
                         &classes.front().descriptor, true);
  std::vector<ClassInfo *> registrations;
  registrations.reserve(classCount);
  for (ClassInfo &info : classes)
    registrations.push_back(&info);
  EXPECT_EQ(finalize(blob, registrations), TALOS_RT_INVALID_DESIGN);
}

} // namespace
