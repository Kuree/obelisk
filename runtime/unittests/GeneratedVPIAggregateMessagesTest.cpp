//===- GeneratedVPIAggregateMessagesTest.cpp - Aggregate snapshots --------===//

#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Reflection/DesignReflection.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
using namespace obelisk::reflection;

TEST(GeneratedDesignDatabase, MailboxAggregateMessages) {
  const char *input = std::getenv("OBELISK_TEST_INPUT");
  ASSERT_NE(input, nullptr);
  mlir::DialectRegistry registry;
  registry.insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                  obelisk::sim::ObeliskSimulationDialect>();
  mlir::MLIRContext context(registry);
  auto module = mlir::parseSourceFile<mlir::ModuleOp>(input, &context);
  ASSERT_TRUE(module);
  auto design = *module->getOps<obelisk::sim::SimDesignOp>().begin();
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
                                               checksum,
                                               nullptr,
                                               0,
                                               0,
                                               0,
                                               nullptr,
                                               0,
                                               nullptr,
                                               0};
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
  auto messageType = [&](const char *name) {
    auto op = mlir::dyn_cast_or_null<obelisk::sim::SimVPITypespecDeclOp>(
        mlir::SymbolTable::lookupSymbolIn(design, name));
    return op ? op.getMessageTypeAttr().getValue() : mlir::Type{};
  };
  const std::array<mlir::Type, 3> types{
      messageType("array"), messageType("queue"), messageType("dynamic")};
  ASSERT_TRUE(types[0]);
  auto recordType =
      mlir::cast<obelisk::sim::UnpackedArrayType>(types[0]).getElementType();
  auto record = obelisk::sim::getElementDescriptor(recordType);
  ASSERT_TRUE(record);
  ASSERT_EQ(record->valueSize, 24u);
  std::array<obelisk_rt_object_v1 *, 5> objects{};
  obelisk_rt_gc_root_range_v1 roots{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_range_push(lane, &roots, objects.data(),
                                             objects.size()),
            OBELISK_RT_OK);
  auto slotsFor = [](const obelisk::sim::ElementDescriptor &descriptor) {
    std::vector<obelisk_rt_element_trace_slot_v1> result;
    for (size_t index = 0; index != descriptor.traceOffsets.size(); ++index)
      result.push_back({static_cast<uint64_t>(descriptor.traceOffsets[index]),
                        static_cast<uint32_t>(descriptor.traceKinds[index]),
                        0});
    return result;
  };
  for (size_t index = 0; index != types.size(); ++index) {
    auto descriptor = obelisk::sim::getElementDescriptor(types[index]);
    ASSERT_TRUE(descriptor);
    auto slots = slotsFor(*descriptor);
    ASSERT_EQ(
        obelisk_rt_v1_mailbox_create_typed(
            lane, descriptor->typeID, static_cast<uint32_t>(descriptor->kind),
            static_cast<uint32_t>(descriptor->flags), descriptor->valueSize,
            descriptor->alignment, descriptor->bitWidth, slots.data(),
            slots.size(), 0, &objects[index]),
        OBELISK_RT_OK);
  }
  auto recordSlots = slotsFor(*record);
  for (size_t index = 3; index != 5; ++index)
    ASSERT_EQ(obelisk_rt_v1_container_create_typed(
                  lane,
                  index == 3 ? OBELISK_RT_CONTAINER_QUEUE
                             : OBELISK_RT_CONTAINER_DYNAMIC_ARRAY,
                  record->typeID, static_cast<uint32_t>(record->kind),
                  static_cast<uint32_t>(record->flags), record->valueSize,
                  record->alignment, record->bitWidth, recordSlots.data(),
                  recordSlots.size(), 2, UINT64_MAX, &objects[index]),
              OBELISK_RT_OK);
  obelisk_rt_string_v1 textWord = 0;
  obelisk_rt_gc_managed_root_v1 textRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_push(lane, &textRoot, &textWord),
            OBELISK_RT_OK);
  const std::string text = "nested string survives container collection";
  ASSERT_EQ(
      obelisk_rt_v1_string_create(lane, text.data(), text.size(), &textWord),
      OBELISK_RT_OK);
  std::array<uint8_t, 48> values{}, unknown{};
  for (size_t index = 0; index != 2; ++index) {
    int32_t number = index ? -19 : 23;
    std::memcpy(values.data() + index * 24, &number, sizeof(number));
    values[index * 24 + 4] = index ? 0xab : 0x12;
    std::memcpy(values.data() + index * 24 + 8, &textWord, sizeof(textWord));
    values[index * 24 + 16] = 5;
    unknown[index * 24 + 16] = 6;
    for (size_t container = 3; container != 5; ++container)
      ASSERT_EQ(obelisk_rt_v1_container_write(lane, objects[container], index,
                                              values.data() + index * 24,
                                              unknown.data() + index * 24),
                OBELISK_RT_OK);
  }
  uint32_t success = 0;
  ASSERT_EQ(obelisk_rt_v1_mailbox_try_put(lane, objects[0], values.data(),
                                          unknown.data(), &success),
            OBELISK_RT_OK);
  ASSERT_EQ(success, 1u);
  for (size_t index = 1; index != 3; ++index) {
    const uint64_t word =
        obelisk_rt_managed_word_from_object(objects[index + 2]);
    ASSERT_EQ(obelisk_rt_v1_mailbox_try_put(lane, objects[index], &word,
                                            nullptr, &success),
              OBELISK_RT_OK);
    ASSERT_EQ(success, 1u);
  }
  const std::array<const char *, 3> names{"top.array", "top.queue",
                                          "top.dynamic"};
  std::array<obelisk_rt_design_cursor_v1, 3> storage{};
  std::array<vpiHandle, 3> mailboxes{}, messages{}, pending{}, operands{};
  std::vector<vpiHandle> retained;
  auto keep = [&](vpiHandle handle) {
    EXPECT_NE(handle, nullptr);
    if (handle)
      retained.push_back(handle);
    return handle;
  };
  for (size_t index = 0; index != names.size(); ++index) {
    ASSERT_EQ(obelisk_rt_cached_design_lookup(
                  runtime, reinterpret_cast<const uint8_t *>(names[index]),
                  std::strlen(names[index]), &storage[index]),
              OBELISK_RT_OK);
    uint64_t word = obelisk_rt_managed_word_from_object(objects[index]);
    ASSERT_EQ(
        obelisk_rt_v1_design_write(runtime, storage[index], &word, nullptr, 64),
        OBELISK_RT_OK);
    auto variable =
        keep(vpi_handle_by_name(const_cast<char *>(names[index]), nullptr));
    mailboxes[index] = vpi_handle(vpiClassObj, variable);
    ASSERT_NE(mailboxes[index], nullptr);
    pending[index] = vpi_iterate(vpiMessages, mailboxes[index]);
    ASSERT_NE(pending[index], nullptr);
    auto iterator = vpi_iterate(vpiMessages, mailboxes[index]);
    messages[index] = keep(vpi_scan(iterator));
    ASSERT_NE(messages[index], nullptr);
    EXPECT_EQ(vpi_scan(iterator), nullptr);
    EXPECT_EQ(vpi_get(vpiType, messages[index]), vpiOperation);
    EXPECT_EQ(vpi_get(vpiOpType, messages[index]), vpiAssignmentPatternOp);
    EXPECT_EQ(vpi_get(vpiSize, messages[index]), 2 * (43 + text.size() * 8));
    operands[index] = vpi_iterate(vpiOperand, messages[index]);
    ASSERT_NE(operands[index], nullptr);
    auto use = keep(vpi_handle(vpiUse, operands[index]));
    EXPECT_EQ(vpi_compare_objects(use, messages[index]), 1);
    auto type = keep(vpi_handle(vpiTypespec, messages[index]));
    EXPECT_EQ(vpi_get(vpiType, type), vpiArrayTypespec);
    s_vpi_value unsupported{};
    unsupported.format = vpiIntVal;
    vpi_get_value(messages[index], &unsupported);
    EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
    EXPECT_EQ(vpi_put_value(messages[index], &unsupported, nullptr, vpiNoDelay),
              nullptr);
    EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  }
  // Discard the entire live graph before scanning nested operands. Immutable
  // snapshots keep copied data, not managed roots or raw string pointers.
  for (auto cursor : storage) {
    const uint64_t zero = 0;
    ASSERT_EQ(obelisk_rt_v1_design_write(runtime, cursor, &zero, nullptr, 64),
              OBELISK_RT_OK);
  }
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &textRoot), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_range_pop(lane, &roots), OBELISK_RT_OK);
  objects.fill(nullptr);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  obelisk_rt_gc_statistics_v1 stats{};
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(runtime, &stats), OBELISK_RT_OK);
  EXPECT_EQ(stats.live_objects, 0u);
  vpiHandle survivor = nullptr;
  for (size_t index = 0; index != names.size(); ++index) {
    EXPECT_EQ(vpi_get(vpiType, mailboxes[index]), vpiUndefined);
    EXPECT_EQ(vpi_get(vpiType, pending[index]), vpiUndefined);
    auto use = keep(vpi_handle(vpiUse, operands[index]));
    EXPECT_EQ(vpi_compare_objects(use, messages[index]), 1);
    for (size_t item = 0; item != 2; ++item) {
      auto recordValue = keep(vpi_scan(operands[index]));
      ASSERT_NE(recordValue, nullptr);
      EXPECT_EQ(vpi_get(vpiType, recordValue), vpiOperation);
      auto fields = vpi_iterate(vpiOperand, recordValue);
      ASSERT_NE(fields, nullptr);
      std::array<vpiHandle, 4> fieldValues{};
      for (auto &field : fieldValues)
        field = keep(vpi_scan(fields));
      EXPECT_EQ(vpi_scan(fields), nullptr);
      if (!survivor)
        survivor = fieldValues[0];
      s_vpi_value number{};
      number.format = vpiIntVal;
      vpi_get_value(fieldValues[0], &number);
      EXPECT_EQ(number.value.integer, item ? -19 : 23);
      vpi_get_value(fieldValues[1], &number);
      EXPECT_EQ(number.value.integer, item ? 0xab : 0x12);
      s_vpi_value string{};
      string.format = vpiStringVal;
      vpi_get_value(fieldValues[2], &string);
      ASSERT_NE(string.value.str, nullptr);
      EXPECT_EQ(std::string(string.value.str), text);
      s_vpi_value logic{};
      logic.format = vpiVectorVal;
      vpi_get_value(fieldValues[3], &logic);
      ASSERT_NE(logic.value.vector, nullptr);
      EXPECT_EQ(logic.value.vector[0].aval, 3);
      EXPECT_EQ(logic.value.vector[0].bval, 6);
      auto enumType = keep(vpi_handle(vpiTypespec, fieldValues[1]));
      EXPECT_EQ(vpi_get(vpiType, enumType), vpiEnumTypespec);
    }
    EXPECT_EQ(vpi_scan(operands[index]), nullptr);
    const char *rendered = vpi_get_str(vpiDecompile, messages[index]);
    ASSERT_NE(rendered, nullptr);
    EXPECT_EQ(std::string(rendered).substr(0, 4), "'{'{");
    EXPECT_NE(std::string(rendered).find(text), std::string::npos);
  }
  for (auto handle : retained)
    if (handle != survivor)
      EXPECT_EQ(vpi_release_handle(handle), 1);
  // A scanned leaf independently owns the copied snapshot after every parent
  // expression, iterator, and sibling handle has been released.
  s_vpi_value survivingNumber{};
  survivingNumber.format = vpiIntVal;
  vpi_get_value(survivor, &survivingNumber);
  EXPECT_EQ(survivingNumber.value.integer, 23);
  EXPECT_EQ(vpi_release_handle(survivor), 1);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(runtime);
}
} // namespace
