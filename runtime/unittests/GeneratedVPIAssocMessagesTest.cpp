//===- GeneratedVPIAssocMessagesTest.cpp - Sparse associative snapshots
//--------===//

#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "talos/Conversion/SimulationToBytecode.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
#include "talos/Reflection/DesignReflection.h"
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
using namespace talos::reflection;

TEST(GeneratedDesignDatabase, MailboxAssociativeMessages) {
  const char *input = std::getenv("TALOS_TEST_INPUT");
  ASSERT_NE(input, nullptr);
  mlir::DialectRegistry registry;
  registry.insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                  talos::sim::TalosSimulationDialect>();
  mlir::MLIRContext context(registry);
  auto module = mlir::parseSourceFile<mlir::ModuleOp>(input, &context);
  ASSERT_TRUE(module);
  auto design = *module->getOps<talos::sim::SimDesignOp>().begin();
  talos::SimulationBytecodeOptions options;
  options.vpi = "full";
  auto encoded = talos::encodeSimulationDesign(design, options);
  ASSERT_TRUE(mlir::succeeded(encoded));
  uint64_t checksum = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    checksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  talos_rt_execution_descriptor_v1 execution{TALOS_RT_VERSION,
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
  ASSERT_EQ(talos_rt_v1_design_validate(&execution), TALOS_RT_OK);
  talos_rt_context *runtime = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &runtime),
            TALOS_RT_OK);

  talos_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_lane_create(runtime, &lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_enter(lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_startup(runtime, nullptr, 0), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_end_compile(runtime), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_start_simulation(runtime), TALOS_RT_OK);
  const std::array<const char *, 5> names{"wide", "strings", "records",
                                          "wildcard", "nested"};
  std::array<talos_rt_object_v1 *, 12> objects{};
  talos_rt_gc_root_range_v1 roots{};
  ASSERT_EQ(talos_rt_v1_gc_root_range_push(lane, &roots, objects.data(),
                                             objects.size()),
            TALOS_RT_OK);
  auto slotsFor = [](const talos::sim::ElementDescriptor &descriptor) {
    std::vector<talos_rt_element_trace_slot_v1> slots;
    for (size_t i = 0; i != descriptor.traceOffsets.size(); ++i)
      slots.push_back({static_cast<uint64_t>(descriptor.traceOffsets[i]),
                       static_cast<uint32_t>(descriptor.traceKinds[i]), 0});
    return slots;
  };
  for (size_t i = 0; i != names.size(); ++i) {
    auto declaration = mlir::cast<talos::sim::SimVPITypespecDeclOp>(
        mlir::SymbolTable::lookupSymbolIn(design, names[i]));
    auto type = mlir::cast<talos::sim::AssocArrayType>(
        declaration.getMessageTypeAttr().getValue());
    auto message = talos::sim::getElementDescriptor(type);
    auto element = talos::sim::getElementDescriptor(type.getElementType());
    auto key = talos::sim::getElementDescriptor(type.getKeyType());
    auto keyLayout = talos::sim::getAssocKeyLayout(type);
    ASSERT_TRUE(message && element && key && keyLayout);
    auto messageSlots = slotsFor(*message), elementSlots = slotsFor(*element),
         keySlots = slotsFor(*key);
    ASSERT_EQ(talos_rt_v1_mailbox_create_typed(
                  lane, message->typeID, static_cast<uint32_t>(message->kind),
                  static_cast<uint32_t>(message->flags), message->valueSize,
                  message->alignment, message->bitWidth, messageSlots.data(),
                  messageSlots.size(), 0, &objects[i]),
              TALOS_RT_OK);
    if (keyLayout->kind == talos::sim::AssocKeyKind::Element) {
      auto plan = talos::sim::getAssocKeyPlan(type.getKeyType());
      ASSERT_TRUE(plan);
      std::vector<uint8_t> bytes(plan->size() * 8);
      for (size_t word = 0; word != plan->size(); ++word)
        for (unsigned byte = 0; byte != 8; ++byte)
          bytes[word * 8 + byte] = (*plan)[word] >> (byte * 8);
      ASSERT_EQ(talos_rt_v1_assoc_create_element_keyed(
                    lane, element->typeID, static_cast<uint32_t>(element->kind),
                    static_cast<uint32_t>(element->flags), element->valueSize,
                    element->alignment, element->bitWidth, elementSlots.data(),
                    elementSlots.size(), key->typeID,
                    static_cast<uint32_t>(key->kind),
                    static_cast<uint32_t>(key->flags), key->valueSize,
                    key->alignment, key->bitWidth, keySlots.data(),
                    keySlots.size(), bytes.data(), bytes.size(),
                    &objects[5 + i]),
                TALOS_RT_OK);
    } else {
      ASSERT_EQ(talos_rt_v1_assoc_create_typed(
                    lane, element->typeID, static_cast<uint32_t>(element->kind),
                    static_cast<uint32_t>(element->flags), element->valueSize,
                    element->alignment, element->bitWidth, elementSlots.data(),
                    elementSlots.size(),
                    static_cast<talos_rt_assoc_key_kind_v1>(keyLayout->kind),
                    keyLayout->width, &objects[5 + i]),
                TALOS_RT_OK);
    }
  }
  talos_rt_string_v1 text = 0;
  talos_rt_gc_managed_root_v1 textRoot{};
  ASSERT_EQ(talos_rt_v1_gc_managed_root_push(lane, &textRoot, &text),
            TALOS_RT_OK);
  const std::string longText = "z key survives all live storage";
  ASSERT_EQ(talos_rt_v1_string_create(lane, longText.data(), longText.size(),
                                        &text),
            TALOS_RT_OK);
  const talos_rt_element_type_v1 boxedWord{TALOS_RT_VERSION,
                                             TALOS_RT_ELEMENT_BITS,
                                             8123,
                                             0,
                                             0,
                                             8,
                                             8,
                                             64,
                                             nullptr,
                                             nullptr};
  for (size_t entry = 0; entry != 3; ++entry) {
    std::array<uint8_t, 17> wide{};
    if (entry == 0)
      wide[12] = 16;
    else if (entry == 1) {
      wide.fill(255);
      wide.back() = 3;
    } else
      wide[0] = 1;
    talos_rt_assoc_key_v1 key{};
    key.kind = TALOS_RT_ASSOC_KEY_SIGNED;
    key.width = 130;
    key.value_data = wide.data();
    const int32_t value = entry == 0 ? 30 : entry == 1 ? 10 : 20;
    ASSERT_EQ(
        talos_rt_v1_assoc_write(lane, objects[5], &key, &value, nullptr),
        TALOS_RT_OK);
  }
  for (size_t entry = 0; entry != 2; ++entry) {
    const int32_t value = entry ? 11 : 22;
    talos_rt_assoc_key_v1 key{};
    key.kind = TALOS_RT_ASSOC_KEY_STRING;
    key.string = text;
    if (entry)
      ASSERT_EQ(talos_rt_v1_string_create(lane, "aaa", 3, &key.string),
                TALOS_RT_OK);
    ASSERT_EQ(
        talos_rt_v1_assoc_write(lane, objects[6], &key, &value, nullptr),
        TALOS_RT_OK);
    std::array<uint8_t, 16> record{};
    const int32_t number = entry ? 1 : 2;
    std::memcpy(record.data(), &number, sizeof(number));
    std::memcpy(record.data() + 8, &text, sizeof(text));
    key = {};
    key.kind = TALOS_RT_ASSOC_KEY_ELEMENT;
    key.width = record.size() * 8;
    key.value_data = record.data();
    ASSERT_EQ(
        talos_rt_v1_assoc_write(lane, objects[7], &key, &value, nullptr),
        TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_container_create_typed(
                  lane, TALOS_RT_CONTAINER_DYNAMIC_ARRAY, boxedWord.type_id,
                  boxedWord.kind, boxedWord.flags, boxedWord.value_size,
                  boxedWord.alignment, boxedWord.bit_width, nullptr, 0, 1,
                  UINT64_MAX, &objects[10 + entry]),
              TALOS_RT_OK);
    const uint64_t wildcard = entry ? 1 : 256;
    ASSERT_EQ(talos_rt_v1_container_write(lane, objects[10 + entry], 0,
                                            &wildcard, nullptr),
              TALOS_RT_OK);
    key = {};
    key.kind = TALOS_RT_ASSOC_KEY_WILDCARD;
    key.object = objects[10 + entry];
    ASSERT_EQ(
        talos_rt_v1_assoc_write(lane, objects[8], &key, &value, nullptr),
        TALOS_RT_OK);
    key = {};
    key.kind = TALOS_RT_ASSOC_KEY_SIGNED;
    key.width = 32;
    key.value = static_cast<uint32_t>(entry ? -3 : 7);
    const std::array<int32_t, 2> pair =
        entry ? std::array<int32_t, 2>{10, 11} : std::array<int32_t, 2>{20, 21};
    ASSERT_EQ(
        talos_rt_v1_assoc_write(lane, objects[9], &key, pair.data(), nullptr),
        TALOS_RT_OK);
  }
  const int32_t defaultValue = 99;
  ASSERT_EQ(
      talos_rt_v1_assoc_set_default(lane, objects[5], &defaultValue, nullptr),
      TALOS_RT_OK);
  talos_rt_gc_statistics_v1 before{}, after{};
  ASSERT_EQ(talos_rt_v1_gc_statistics(runtime, &before), TALOS_RT_OK);
  VPIAssocSnapshot copied;
  ASSERT_EQ(talos_rt_assoc_snapshot(objects[5], &copied), TALOS_RT_OK);
  ASSERT_EQ(copied.entries.size(), 3u);
  for (size_t i = 0; i != 3; ++i) {
    int32_t value = 0;
    std::memcpy(&value, copied.entries[i].value.data(), sizeof(value));
    EXPECT_EQ(value, 10 * (i + 1));
    EXPECT_EQ(copied.entries[i].keyWidth, 130u);
  }
  ASSERT_EQ(talos_rt_v1_gc_statistics(runtime, &after), TALOS_RT_OK);
  EXPECT_EQ(before.live_objects, after.live_objects);
  EXPECT_EQ(before.allocated_objects, after.allocated_objects);
  EXPECT_EQ(talos_rt_assoc_snapshot(objects[0], &copied),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_TRUE(copied.entries.empty());
  EXPECT_EQ(copied.element, nullptr);
  EXPECT_EQ(talos_rt_assoc_snapshot(objects[5], nullptr),
            TALOS_RT_INVALID_ARGUMENT);
  std::array<talos_rt_design_cursor_v1, 5> storage{};
  std::array<vpiHandle, 5> arrays{}, iterators{}, pending{};
  std::vector<vpiHandle> retained;
  auto keep = [&](vpiHandle handle) {
    EXPECT_NE(handle, nullptr);
    if (handle)
      retained.push_back(handle);
    return handle;
  };
  auto integer = [&](vpiHandle handle) {
    s_vpi_value value{};
    value.format = vpiIntVal;
    vpi_get_value(handle, &value);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    return value.value.integer;
  };
  auto children = [&](vpiHandle parent, PLI_INT32 selector) {
    std::vector<vpiHandle> values;
    auto iterator = vpi_iterate(selector, parent);
    EXPECT_NE(iterator, nullptr);
    if (!iterator)
      return values;
    auto use = keep(vpi_handle(vpiUse, iterator));
    EXPECT_EQ(vpi_compare_objects(use, parent), 1);
    while (auto child = vpi_scan(iterator))
      values.push_back(keep(child));
    return values;
  };
  for (size_t i = 0; i != 5; ++i) {
    const uint64_t arrayWord =
        talos_rt_managed_word_from_object(objects[5 + i]);
    uint32_t success = 0;
    ASSERT_EQ(talos_rt_v1_mailbox_try_put(lane, objects[i], &arrayWord,
                                            nullptr, &success),
              TALOS_RT_OK);
    ASSERT_EQ(success, 1u);
    const uint64_t emptyArray = 0;
    ASSERT_EQ(talos_rt_v1_mailbox_try_put(lane, objects[i], &emptyArray,
                                            nullptr, &success),
              TALOS_RT_OK);
    ASSERT_EQ(success, 1u);
    const std::string name = std::string("top.") + names[i];
    ASSERT_EQ(talos_rt_cached_design_lookup(
                  runtime, reinterpret_cast<const uint8_t *>(name.data()),
                  name.size(), &storage[i]),
              TALOS_RT_OK);
    const uint64_t mailboxWord =
        talos_rt_managed_word_from_object(objects[i]);
    ASSERT_EQ(talos_rt_v1_design_write(runtime, storage[i], &mailboxWord,
                                         nullptr, 64),
              TALOS_RT_OK);
    auto variable =
        keep(vpi_handle_by_name(const_cast<char *>(name.c_str()), nullptr));
    auto mailbox = vpi_handle(vpiClassObj, variable);
    ASSERT_NE(mailbox, nullptr);
    pending[i] = vpi_iterate(vpiMessages, mailbox);
    ASSERT_NE(pending[i], nullptr);
    auto messages = vpi_iterate(vpiMessages, mailbox);
    ASSERT_NE(messages, nullptr);
    arrays[i] = keep(vpi_scan(messages));
    ASSERT_NE(arrays[i], nullptr);
    auto empty = keep(vpi_scan(messages));
    ASSERT_NE(empty, nullptr);
    EXPECT_EQ(vpi_get(vpiType, empty), vpiRegArray);
    EXPECT_EQ(vpi_get(vpiSize, empty), 0);
    EXPECT_EQ(vpi_iterate(vpiReg, empty), nullptr);
    EXPECT_EQ(vpi_handle(vpiLeftRange, empty), nullptr);
    EXPECT_EQ(vpi_scan(messages), nullptr);
    EXPECT_EQ(vpi_get(vpiType, arrays[i]), vpiRegArray);
    EXPECT_EQ(vpi_get(vpiArrayType, arrays[i]), vpiAssocArray);
    EXPECT_EQ(vpi_get_str(vpiDecompile, arrays[i]), nullptr);
    EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
    EXPECT_EQ(vpi_get(vpiSize, arrays[i]), i == 0 ? 3 : i == 4 ? 4 : 2);
    EXPECT_EQ(vpi_handle(vpiLeftRange, arrays[i]), nullptr);
    EXPECT_EQ(vpi_handle(vpiRightRange, arrays[i]), nullptr);
    auto ranges = children(arrays[i], vpiRange);
    ASSERT_EQ(ranges.size(), i == 4 ? 2u : 1u);
    EXPECT_EQ(vpi_get(vpiSize, ranges[0]), 0);
    EXPECT_EQ(vpi_handle(vpiLeftRange, ranges[0]), nullptr);
    auto type = keep(vpi_handle(vpiTypespec, arrays[i]));
    EXPECT_EQ(vpi_get(vpiType, type), vpiArrayTypespec);
    auto keyType = vpi_handle(vpiIndexTypespec, type);
    if (i == 3)
      EXPECT_EQ(keyType, nullptr);
    else
      keep(keyType);
    iterators[i] = vpi_iterate(vpiReg, arrays[i]);
    ASSERT_NE(iterators[i], nullptr);
    EXPECT_EQ(vpi_release_handle(mailbox), 1);
  }
  const uint64_t zero = 0;
  for (auto cursor : storage)
    ASSERT_EQ(talos_rt_v1_design_write(runtime, cursor, &zero, nullptr, 64),
              TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_managed_root_pop(lane, &textRoot), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_root_range_pop(lane, &roots), TALOS_RT_OK);
  objects.fill(nullptr);
  ASSERT_EQ(talos_rt_v1_gc_collect(lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_statistics(runtime, &after), TALOS_RT_OK);
  EXPECT_EQ(after.live_objects, 0u);
  for (size_t i = 0; i != 5; ++i) {
    EXPECT_EQ(vpi_scan(pending[i]), nullptr);
    EXPECT_NE(vpi_chk_error(nullptr), 0);
    auto use = keep(vpi_handle(vpiUse, iterators[i]));
    EXPECT_EQ(vpi_compare_objects(use, arrays[i]), 1);
    const unsigned count = i == 0 ? 3 : i == 4 ? 4 : 2;
    for (unsigned n = 0; n != count; ++n) {
      auto entry = keep(vpi_scan(iterators[i]));
      ASSERT_NE(entry, nullptr);
      EXPECT_EQ(vpi_get(vpiType, entry), vpiIntVar);
      EXPECT_EQ(vpi_get(vpiArrayMember, entry), 1);
      EXPECT_EQ(integer(entry), i == 0   ? 10 * (n + 1)
                                : i == 4 ? (n < 2 ? 10 + n : 18 + n)
                                         : (n ? 22 : 11));
      auto indices = children(entry, vpiIndex);
      ASSERT_EQ(indices.size(), i == 4 ? 2u : 1u);
      auto parent = keep(vpi_handle(vpiParent, entry));
      EXPECT_EQ(vpi_compare_objects(parent, arrays[i]), 1);
      if (i == 0) {
        EXPECT_EQ(vpi_get(vpiSize, indices[0]), 130);
        EXPECT_EQ(vpi_get(vpiSigned, indices[0]), 1);
        s_vpi_value bits{};
        bits.format = vpiVectorVal;
        vpi_get_value(indices[0], &bits);
        ASSERT_NE(bits.value.vector, nullptr);
        EXPECT_EQ(bits.value.vector[0].aval, n == 0   ? UINT32_MAX
                                             : n == 1 ? 1u
                                                      : 0u);
        EXPECT_EQ(bits.value.vector[0].bval, 0u);
        EXPECT_EQ(bits.value.vector[3].aval, n == 0   ? UINT32_MAX
                                             : n == 2 ? 16u
                                                      : 0u);
        if (n < 2)
          EXPECT_EQ(
              vpi_compare_objects(
                  keep(vpi_handle_by_index(arrays[i], n ? 1 : -1)), entry),
              1);
      } else if (i == 1) {
        s_vpi_value value{};
        value.format = vpiStringVal;
        vpi_get_value(indices[0], &value);
        ASSERT_NE(value.value.str, nullptr);
        EXPECT_EQ(std::string(value.value.str), n ? longText : "aaa");
      } else if (i == 2) {
        EXPECT_EQ(vpi_get(vpiType, indices[0]), vpiOperation);
        EXPECT_EQ(vpi_get(vpiOpType, indices[0]), vpiAssignmentPatternOp);
        auto fields = children(indices[0], vpiOperand);
        ASSERT_EQ(fields.size(), 2u);
        EXPECT_EQ(integer(fields[0]), n + 1);
        s_vpi_value value{};
        value.format = vpiStringVal;
        vpi_get_value(fields[1], &value);
        ASSERT_NE(value.value.str, nullptr);
        EXPECT_EQ(std::string(value.value.str), longText);
      } else if (i == 3) {
        EXPECT_EQ(integer(indices[0]), n ? 256 : 1);
        EXPECT_EQ(vpi_get(vpiSize, indices[0]), n ? 9 : 1);
        EXPECT_EQ(vpi_get(vpiSigned, indices[0]), 0);
        EXPECT_EQ(vpi_handle(vpiTypespec, indices[0]), nullptr);
        EXPECT_EQ(vpi_compare_objects(
                      keep(vpi_handle_by_index(arrays[i], n ? 256 : 1)), entry),
                  1);
      } else {
        EXPECT_EQ(integer(indices[0]), n % 2 ? 1 : 2);
        EXPECT_EQ(integer(indices[1]), n < 2 ? -3 : 7);
        PLI_INT32 path[] = {n < 2 ? -3 : 7, n % 2 ? 1 : 2};
        EXPECT_EQ(
            vpi_compare_objects(
                keep(vpi_handle_by_multi_index(arrays[i], 2, path)), entry),
            1);
      }
      s_vpi_value denied{};
      denied.format = vpiIntVal;
      EXPECT_EQ(vpi_put_value(entry, &denied, nullptr, vpiNoDelay), nullptr);
      EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
    }
    EXPECT_EQ(vpi_scan(iterators[i]), nullptr);
    EXPECT_EQ(vpi_handle_by_index(arrays[i], 99), nullptr);
  }
  for (auto handle : retained)
    EXPECT_EQ(vpi_release_handle(handle), 1);
  ASSERT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
  talos_rt_v1_context_destroy(runtime);
}
} // namespace
