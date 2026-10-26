//===- GeneratedVPIUntypedMessagesTest.cpp - Boxed message snapshots ---===//

#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Analysis/ManagedClassLayoutAnalysis.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Reflection/DesignReflection.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "llvm/IR/DataLayout.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
using namespace obelisk::reflection;

TEST(GeneratedDesignDatabase, UntypedMailboxMessages) {
  const char *input = std::getenv("OBELISK_TEST_INPUT");
  ASSERT_NE(input, nullptr);
  mlir::DialectRegistry registry;
  registry.insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                  obelisk::sim::ObeliskSimulationDialect>();
  mlir::MLIRContext context(registry);
  auto module = mlir::parseSourceFile<mlir::ModuleOp>(input, &context);
  ASSERT_TRUE(module);
  auto design = *module->getOps<obelisk::sim::SimDesignOp>().begin();
  auto layouts = obelisk::analysis::ManagedClassLayoutAnalysis::compute(
      design,
      llvm::DataLayout((*module)
                           ->getAttrOfType<mlir::StringAttr>("llvm.data_layout")
                           .getValue()));
  ASSERT_TRUE(mlir::succeeded(layouts));
  obelisk::SimulationBytecodeOptions options;
  options.vpi = "full";
  auto encoded = obelisk::encodeSimulationDesign(design, options);
  ASSERT_TRUE(mlir::succeeded(encoded));
  uint64_t checksum = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    checksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  obelisk_rt_execution_descriptor_v1 execution{OBELISK_RT_VERSION,
                                               encoded->executionFlags,
                                               0,
                                               encoded->bytecode.data(),
                                               encoded->bytecode.size(),
                                               encoded->designDatabase.data(),
                                               encoded->designDatabase.size(),
                                               encoded->stateBitCount,
                                               checksum};
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);
  obelisk_rt_context *runtime = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &runtime),
            OBELISK_RT_OK);

  obelisk_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(runtime, &lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(runtime, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(runtime), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(runtime), OBELISK_RT_OK);
  // The first three boxes share one physical i32 descriptor. Their source
  // types remain distinct: signed int, unsigned bit vector, and a typedef.
  constexpr size_t messageCount = 8;
  const std::array<const char *, messageCount> names{
      "signed_word", "unsigned_word", "aliased_word", "wide",
      "text",        "record",        "sparse",       "object"};
  std::array<obelisk_rt_object_v1 *, 13> objects{};
  obelisk_rt_gc_root_range_v1 roots{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_range_push(lane, &roots, objects.data(),
                                             objects.size()),
            OBELISK_RT_OK);
  auto slotsFor = [](const obelisk::sim::ElementDescriptor &descriptor) {
    std::vector<obelisk_rt_element_trace_slot_v1> slots;
    for (size_t i = 0; i != descriptor.traceOffsets.size(); ++i)
      slots.push_back({static_cast<uint64_t>(descriptor.traceOffsets[i]),
                       static_cast<uint32_t>(descriptor.traceKinds[i]), 0});
    return slots;
  };
  auto boxElement =
      obelisk::sim::getElementDescriptor(obelisk::sim::BoxType::get(&context));
  ASSERT_TRUE(boxElement);
  auto boxSlots = slotsFor(*boxElement);
  ASSERT_EQ(obelisk_rt_v1_mailbox_create_typed(
                lane, boxElement->typeID,
                static_cast<uint32_t>(boxElement->kind),
                static_cast<uint32_t>(boxElement->flags), boxElement->valueSize,
                boxElement->alignment, boxElement->bitWidth, boxSlots.data(),
                boxSlots.size(), 0, &objects[0]),
            OBELISK_RT_OK);
  std::array<obelisk::sim::ElementDescriptor, messageCount> descriptors{};
  for (size_t i = 0; i != messageCount; ++i) {
    auto declaration = mlir::cast<obelisk::sim::SimVPIBoxTypeDeclOp>(
        mlir::SymbolTable::lookupSymbolIn(design, names[i]));
    auto descriptor =
        obelisk::sim::getElementDescriptor(declaration.getValueType());
    ASSERT_TRUE(descriptor);
    descriptors[i] = *descriptor;
    auto slots = slotsFor(*descriptor);
    ASSERT_EQ(obelisk_rt_v1_container_create_typed(
                  lane, OBELISK_RT_CONTAINER_DYNAMIC_ARRAY, descriptor->typeID,
                  static_cast<uint32_t>(descriptor->kind),
                  static_cast<uint32_t>(descriptor->flags),
                  descriptor->valueSize, descriptor->alignment,
                  descriptor->bitWidth, slots.data(), slots.size(), 1,
                  UINT64_MAX, &objects[i + 1]),
              OBELISK_RT_OK);
    if (i == 0) {
      ASSERT_EQ(obelisk_rt_v1_dynamic_array_resize(lane, objects[1], 2),
                OBELISK_RT_OK);
      EXPECT_EQ(obelisk_rt_v1_box_bind_type(runtime, objects[1], 1),
                OBELISK_RT_INVALID_ARGUMENT);
      ASSERT_EQ(obelisk_rt_v1_dynamic_array_resize(lane, objects[1], 1),
                OBELISK_RT_OK);
    } else if (i == 3) {
      EXPECT_EQ(obelisk_rt_v1_box_bind_type(runtime, objects[i + 1], 1),
                OBELISK_RT_INVALID_ARGUMENT); // Wide four-state vs i32.
    } else if (i == 4) {
      EXPECT_EQ(obelisk_rt_v1_box_bind_type(runtime, objects[i + 1], 8),
                OBELISK_RT_INVALID_ARGUMENT); // String vs class managed slot.
    }
    ASSERT_EQ(obelisk_rt_v1_box_bind_type(runtime, objects[i + 1], i + 1),
              OBELISK_RT_OK);
    EXPECT_EQ(obelisk_rt_v1_box_bind_type(runtime, objects[i + 1], i + 1),
              OBELISK_RT_OK); // Same binding is idempotent.
  }
  // Source identity is immutable even when both bindings have identical
  // physical storage. Context ownership is checked before inspecting bytes.
  EXPECT_EQ(obelisk_rt_v1_box_bind_type(runtime, objects[1], 2),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_box_bind_type(runtime, objects[1], UINT64_MAX),
            OBELISK_RT_INVALID_ARGUMENT);
  obelisk_rt_context *foreign = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &foreign),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_box_bind_type(foreign, objects[1], 1),
            OBELISK_RT_INVALID_HANDLE);
  obelisk_rt_v1_context_destroy(foreign);
  ASSERT_EQ(descriptors[0].typeID, descriptors[1].typeID);
  ASSERT_EQ(descriptors[0].typeID, descriptors[2].typeID);
  ASSERT_EQ(descriptors[5].valueSize, 32u);
  const auto &word = descriptors[0];
  ASSERT_EQ(obelisk_rt_v1_container_create_typed(
                lane, OBELISK_RT_CONTAINER_QUEUE, word.typeID,
                static_cast<uint32_t>(word.kind),
                static_cast<uint32_t>(word.flags), word.valueSize,
                word.alignment, word.bitWidth, nullptr, 0, 0, UINT64_MAX,
                &objects[9]),
            OBELISK_RT_OK);
  const int32_t number = -17, second = 29;
  for (int32_t value : {number, second})
    ASSERT_EQ(obelisk_rt_v1_queue_push(lane, objects[9], 0, &value, nullptr),
              OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_assoc_create_typed(
                lane, word.typeID, static_cast<uint32_t>(word.kind),
                static_cast<uint32_t>(word.flags), word.valueSize,
                word.alignment, word.bitWidth, nullptr, 0,
                OBELISK_RT_ASSOC_KEY_SIGNED, 32, &objects[10]),
            OBELISK_RT_OK);
  obelisk_rt_assoc_key_v1 key{};
  key.kind = OBELISK_RT_ASSOC_KEY_SIGNED;
  key.width = 32;
  key.value = static_cast<uint32_t>(-4);
  ASSERT_EQ(
      obelisk_rt_v1_assoc_write(lane, objects[10], &key, &second, nullptr),
      OBELISK_RT_OK);
  const auto *classLayout = layouts->lookup(1);
  ASSERT_NE(classLayout, nullptr);
  const obelisk_rt_trace_layout_v1 trace{
      OBELISK_RT_VERSION,     0,       classLayout->size,
      classLayout->alignment, nullptr, 0};
  obelisk_rt_class_descriptor_v1 classDescriptor{};
  classDescriptor.version = OBELISK_RT_VERSION;
  classDescriptor.class_id = 1;
  classDescriptor.instance_size = classLayout->size;
  classDescriptor.instance_alignment = classLayout->alignment;
  classDescriptor.layout = &trace;
  ASSERT_EQ(obelisk_rt_v1_class_register(runtime, &classDescriptor),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &classDescriptor, &objects[11]),
            OBELISK_RT_OK);
  obelisk_rt_string_v1 string = 0;
  obelisk_rt_gc_managed_root_v1 stringRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_push(lane, &stringRoot, &string),
            OBELISK_RT_OK);
  const std::string text = "untyped source identity survives collection";
  ASSERT_EQ(
      obelisk_rt_v1_string_create(lane, text.data(), text.size(), &string),
      OBELISK_RT_OK);
  std::array<uint8_t, 17> wide{}, unknown{};
  wide[0] = 0x12;
  unknown[0] = 0x18;
  wide[16] = 2;
  std::array<uint8_t, 32> record{};
  const uint64_t queueWord = obelisk_rt_managed_word_from_object(objects[9]);
  const uint64_t sparseWord = obelisk_rt_managed_word_from_object(objects[10]);
  const uint64_t classWord = obelisk_rt_managed_word_from_object(objects[11]);
  std::memcpy(record.data(), &number, 4);
  std::memcpy(record.data() + 8, &string, 8);
  std::memcpy(record.data() + 16, &queueWord, 8);
  std::memcpy(record.data() + 24, &classWord, 8);
  const std::array<const void *, messageCount> values{
      &number, &number,       &number,     wide.data(),
      &string, record.data(), &sparseWord, &classWord};
  for (size_t i = 0; i != messageCount; ++i) {
    ASSERT_EQ(obelisk_rt_v1_container_write(lane, objects[i + 1], 0, values[i],
                                            i == 3 ? unknown.data() : nullptr),
              OBELISK_RT_OK);
    // Mailbox puts copy value containers; cloning each outer box explicitly
    // also verifies that its source descriptor follows ordinary value copies.
    obelisk_rt_object_v1 *clone = nullptr;
    ASSERT_EQ(obelisk_rt_v1_container_clone(lane, objects[i + 1], &clone),
              OBELISK_RT_OK);
    ASSERT_NE(clone, nullptr);
    objects[i + 1] = clone;
    uint64_t copiedID = 0;
    const obelisk_rt_element_type_v1 *copiedElement = nullptr;
    std::vector<uint8_t> copiedPlanes;
    ASSERT_EQ(
        obelisk_rt_box_snapshot(clone, &copiedID, &copiedElement, copiedPlanes),
        OBELISK_RT_OK);
    EXPECT_EQ(copiedID, i + 1);
    ASSERT_NE(copiedElement, nullptr);
    const bool four =
        (copiedElement->flags & OBELISK_RT_ELEMENT_FOUR_STATE) != 0;
    EXPECT_EQ(copiedPlanes.size(), copiedElement->value_size * (four ? 2 : 1));
    uint32_t success = 0;
    const uint64_t boxed = obelisk_rt_managed_word_from_object(objects[i + 1]);
    ASSERT_EQ(obelisk_rt_v1_mailbox_try_put(lane, objects[0], &boxed, nullptr,
                                            &success),
              OBELISK_RT_OK);
    ASSERT_EQ(success, 1u);
  }
  obelisk_rt_design_cursor_v1 storage{};
  constexpr char variableName[] = "top.messages";
  ASSERT_EQ(obelisk_rt_cached_design_lookup(
                runtime, reinterpret_cast<const uint8_t *>(variableName),
                sizeof(variableName) - 1, &storage),
            OBELISK_RT_OK);
  const uint64_t mailboxWord = obelisk_rt_managed_word_from_object(objects[0]);
  ASSERT_EQ(
      obelisk_rt_v1_design_write(runtime, storage, &mailboxWord, nullptr, 64),
      OBELISK_RT_OK);
  std::vector<vpiHandle> handles;
  auto keep = [&](vpiHandle handle) {
    EXPECT_NE(handle, nullptr);
    if (handle)
      handles.push_back(handle);
    return handle;
  };
  auto variable =
      keep(vpi_handle_by_name(const_cast<char *>(variableName), nullptr));
  auto mailbox = vpi_handle(vpiClassObj, variable);
  ASSERT_NE(mailbox, nullptr);
  auto iterator = vpi_iterate(vpiMessages, mailbox);
  ASSERT_NE(iterator, nullptr);
  auto use = vpi_handle(vpiUse, iterator);
  ASSERT_NE(use, nullptr);
  EXPECT_EQ(vpi_compare_objects(use, mailbox), 1);
  EXPECT_EQ(vpi_release_handle(use), 1);
  std::array<vpiHandle, messageCount> messages{};
  for (size_t i = 0; i != messageCount; ++i) {
    messages[i] = vpi_scan(iterator);
    ASSERT_NE(messages[i], nullptr);
    if (i != 7)
      handles.push_back(messages[i]);
  }
  EXPECT_EQ(vpi_scan(iterator), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  auto integer = [&](vpiHandle handle, int32_t expected) {
    s_vpi_value value{};
    value.format = vpiIntVal;
    vpi_get_value(handle, &value);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(value.value.integer, expected);
  };
  auto stringValue = [&](vpiHandle handle) {
    s_vpi_value value{};
    value.format = vpiStringVal;
    vpi_get_value(handle, &value);
    ASSERT_EQ(vpi_chk_error(nullptr), 0);
    ASSERT_NE(value.value.str, nullptr);
    EXPECT_EQ(value.value.str, text);
  };
  const std::array<PLI_INT32, 5> kinds{vpiIntTypespec, vpiBitTypespec,
                                       vpiIntTypespec, vpiLogicTypespec,
                                       vpiStringTypespec};
  for (size_t i = 0; i != kinds.size(); ++i) {
    auto type = keep(vpi_handle(vpiTypespec, messages[i]));
    EXPECT_EQ(vpi_get(vpiType, type), kinds[i]);
    if (i == 2) {
      ASSERT_NE(vpi_get_str(vpiName, type), nullptr);
      EXPECT_STREQ(vpi_get_str(vpiName, type), "Word");
      auto raw = keep(vpi_handle(vpiTypedefAlias, type));
      EXPECT_EQ(vpi_get(vpiType, raw), vpiIntTypespec);
    }
  }
  EXPECT_EQ(vpi_get(vpiSigned, messages[0]), 1);
  EXPECT_EQ(vpi_get(vpiSigned, messages[1]), 0);
  EXPECT_EQ(vpi_get(vpiSigned, messages[2]), 1);
  auto recordItems = vpi_iterate(vpiOperand, messages[5]);
  ASSERT_NE(recordItems, nullptr);
  std::array<vpiHandle, 4> fields{};
  for (auto &field : fields)
    field = keep(vpi_scan(recordItems));
  EXPECT_EQ(vpi_scan(recordItems), nullptr);
  auto queueItems = vpi_iterate(vpiOperand, fields[2]);
  ASSERT_NE(queueItems, nullptr);
  auto queueFirst = keep(vpi_scan(queueItems));
  // Retain this iterator across collection before scanning its second value.
  auto sparse = keep(vpi_handle_by_index(messages[6], -4));
  auto sparseIndices = vpi_iterate(vpiIndex, sparse);
  ASSERT_NE(sparseIndices, nullptr);
  auto sparseIndex = keep(vpi_scan(sparseIndices));
  EXPECT_EQ(vpi_scan(sparseIndices), nullptr);
  integer(sparseIndex, -4);
  auto classObject = vpi_handle(vpiClassObj, messages[7]);
  auto nestedObject = vpi_handle(vpiClassObj, fields[3]);
  ASSERT_NE(classObject, nullptr);
  ASSERT_NE(nestedObject, nullptr);
  EXPECT_EQ(vpi_compare_objects(classObject, nestedObject), 1);
  // An old box with no source descriptor cannot be reclassified from its
  // physical i32 storage. Report unavailable metadata without consuming it.
  ASSERT_EQ(obelisk_rt_v1_container_create_typed(
                lane, OBELISK_RT_CONTAINER_DYNAMIC_ARRAY, word.typeID,
                static_cast<uint32_t>(word.kind),
                static_cast<uint32_t>(word.flags), word.valueSize,
                word.alignment, word.bitWidth, nullptr, 0, 1, UINT64_MAX,
                &objects[12]),
            OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_container_write(lane, objects[12], 0, &number, nullptr),
      OBELISK_RT_OK);
  const uint64_t unbound = obelisk_rt_managed_word_from_object(objects[12]);
  uint32_t inserted = 0;
  ASSERT_EQ(obelisk_rt_v1_mailbox_try_put(lane, objects[0], &unbound, nullptr,
                                          &inserted),
            OBELISK_RT_OK);
  ASSERT_EQ(inserted, 1u);
  EXPECT_EQ(vpi_iterate(vpiMessages, mailbox), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  uint32_t count = 0;
  ASSERT_EQ(obelisk_rt_v1_mailbox_num(objects[0], &count), OBELISK_RT_OK);
  EXPECT_EQ(count, messageCount + 1);
  // Neither snapshots nor their pending iterators keep managed values alive.
  const uint64_t nullWord = 0;
  ASSERT_EQ(
      obelisk_rt_v1_design_write(runtime, storage, &nullWord, nullptr, 64),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &stringRoot),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_range_pop(lane, &roots), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  obelisk_rt_gc_statistics_v1 statistics{};
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(runtime, &statistics), OBELISK_RT_OK);
  EXPECT_EQ(statistics.live_objects, 0u);
  for (size_t i = 0; i != 3; ++i)
    integer(messages[i], number);
  stringValue(messages[4]);
  integer(fields[0], number);
  stringValue(fields[1]);
  integer(queueFirst, number);
  auto queueSecond = keep(vpi_scan(queueItems));
  integer(queueSecond, second);
  EXPECT_EQ(vpi_scan(queueItems), nullptr);
  integer(sparse, second);
  integer(sparseIndex, -4);
  s_vpi_value wideValue{};
  wideValue.format = vpiVectorVal;
  vpi_get_value(messages[3], &wideValue);
  ASSERT_EQ(vpi_chk_error(nullptr), 0);
  ASSERT_NE(wideValue.value.vector, nullptr);
  EXPECT_EQ(wideValue.value.vector[0].aval, 0x0a);
  EXPECT_EQ(wideValue.value.vector[0].bval, 0x18);
  EXPECT_EQ(wideValue.value.vector[4].aval, 2);
  EXPECT_EQ(vpi_get(vpiSize, messages[3]), 130);
  EXPECT_EQ(vpi_get(vpiType, fields[3]), vpiClassVar);
  EXPECT_EQ(vpi_handle(vpiClassObj, fields[3]), nullptr);
  EXPECT_NE(vpi_chk_error(nullptr), 0);
  for (auto handle : {mailbox, messages[7], classObject, nestedObject}) {
    EXPECT_EQ(vpi_get(vpiType, handle), vpiUndefined);
    EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  }
  for (auto handle : handles)
    EXPECT_EQ(vpi_release_handle(handle), 1);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(runtime);
}
} // namespace
