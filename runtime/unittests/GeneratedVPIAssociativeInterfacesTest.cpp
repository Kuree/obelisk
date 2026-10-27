//===- GeneratedVPIAssociativeInterfacesTest.cpp - Sparse VIF keys ----===//
#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Analysis/ManagedClassLayoutAnalysis.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "llvm/IR/DataLayout.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
TEST(GeneratedDesignDatabase, AssociativeVirtualInterfaceArrays) {
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
  std::vector<vpiHandle> handles;
  auto keep = [&](vpiHandle handle) {
    if (handle)
      handles.push_back(handle);
    return handle;
  };
  auto named = [&](std::string name) {
    auto handle = keep(vpi_handle_by_name(name.data(), nullptr));
    EXPECT_NE(handle, nullptr) << name;
    return handle;
  };
  auto cursor = [&](std::string name) {
    obelisk_rt_design_cursor_v1 result{};
    EXPECT_EQ(obelisk_rt_cached_design_lookup(
                  runtime, reinterpret_cast<const uint8_t *>(name.data()),
                  name.size(), &result),
              OBELISK_RT_OK)
        << name;
    return result;
  };
  auto assign = [&](std::string name, const uint64_t *words, uint64_t width) {
    std::vector<uint64_t> unknown((width + 63) / 64, 0);
    auto target = cursor(name);
    VPIClassMemberBinding member{};
    if (obelisk_rt_cached_vpi_class_member(runtime, target, &member) ==
            OBELISK_RT_OK &&
        member.isStatic)
      target = member.backingStorage;
    EXPECT_EQ(obelisk_rt_v1_design_write(runtime, target, words, unknown.data(),
                                         width),
              OBELISK_RT_OK)
        << name;
  };
  auto integer = [&](vpiHandle handle) {
    s_vpi_value value{};
    value.format = vpiIntVal;
    vpi_get_value(handle, &value);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    return value.value.integer;
  };
  auto children = [&](vpiHandle owner, PLI_INT32 selector) {
    std::vector<vpiHandle> result;
    vpiHandle iterator = vpi_iterate(selector, owner);
    if (!iterator)
      return result;
    EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiUse, iterator)), owner),
              1);
    while (auto child = vpi_scan(iterator))
      result.push_back(keep(child));
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    return result;
  };
  const std::array<const char *, 7> names{"wide",     "strings", "records",
                                          "wildcard", "nested",  "classes",
                                          "processes"};
  auto type = [&](uint64_t id) {
    for (auto storage :
         design.getBody().front().getOps<obelisk::sim::SimStorageDeclOp>())
      if (storage.getId() == id)
        return storage.getType();
    return mlir::Type{};
  };
  auto slotsFor = [](const obelisk::sim::ElementDescriptor &descriptor) {
    std::vector<obelisk_rt_element_trace_slot_v1> slots;
    for (size_t i = 0; i != descriptor.traceOffsets.size(); ++i)
      slots.push_back({static_cast<uint64_t>(descriptor.traceOffsets[i]),
                       static_cast<uint32_t>(descriptor.traceKinds[i]), 0});
    return slots;
  };
  std::array<obelisk_rt_object_v1 *, 12> objects{};
  obelisk_rt_gc_root_range_v1 roots{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_range_push(lane, &roots, objects.data(),
                                             objects.size()),
            OBELISK_RT_OK);
  auto create = [&](size_t slot, size_t typeID) {
    auto array = mlir::cast<obelisk::sim::AssocArrayType>(type(typeID));
    auto element = obelisk::sim::getElementDescriptor(array.getElementType());
    auto key = obelisk::sim::getElementDescriptor(array.getKeyType());
    auto keyLayout = obelisk::sim::getAssocKeyLayout(array);
    ASSERT_TRUE(element && key && keyLayout);
    auto slots = slotsFor(*element), keySlots = slotsFor(*key);
    if (keyLayout->kind == obelisk::sim::AssocKeyKind::Element) {
      auto plan = obelisk::sim::getAssocKeyPlan(array.getKeyType());
      ASSERT_TRUE(plan);
      std::vector<uint8_t> bytes(plan->size() * 8);
      for (size_t word = 0; word != plan->size(); ++word)
        for (unsigned byte = 0; byte != 8; ++byte)
          bytes[word * 8 + byte] = (*plan)[word] >> (byte * 8);
      ASSERT_EQ(obelisk_rt_v1_assoc_create_element_keyed(
                    lane, element->typeID, static_cast<uint32_t>(element->kind),
                    static_cast<uint32_t>(element->flags), element->valueSize,
                    element->alignment, element->bitWidth, slots.data(),
                    slots.size(), key->typeID, static_cast<uint32_t>(key->kind),
                    static_cast<uint32_t>(key->flags), key->valueSize,
                    key->alignment, key->bitWidth, keySlots.data(),
                    keySlots.size(), bytes.data(), bytes.size(),
                    &objects[slot]),
                OBELISK_RT_OK);
    } else {
      ASSERT_EQ(obelisk_rt_v1_assoc_create_typed(
                    lane, element->typeID, static_cast<uint32_t>(element->kind),
                    static_cast<uint32_t>(element->flags), element->valueSize,
                    element->alignment, element->bitWidth, slots.data(),
                    slots.size(),
                    static_cast<obelisk_rt_assoc_key_kind_v1>(keyLayout->kind),
                    keyLayout->width, &objects[slot]),
                OBELISK_RT_OK);
    }
  };
  for (size_t slot = 0; slot != names.size(); ++slot)
    create(slot, slot);
  create(11, 1);
  auto dynamicType = mlir::cast<obelisk::sim::DynamicArrayType>(type(7));
  auto element =
      obelisk::sim::getElementDescriptor(dynamicType.getElementType());
  ASSERT_TRUE(element);
  auto slots = slotsFor(*element);
  ASSERT_EQ(obelisk_rt_v1_container_create_typed(
                lane, OBELISK_RT_CONTAINER_DYNAMIC_ARRAY, element->typeID,
                static_cast<uint32_t>(element->kind),
                static_cast<uint32_t>(element->flags), element->valueSize,
                element->alignment, element->bitWidth, slots.data(),
                slots.size(), 1, UINT64_MAX, &objects[7]),
            OBELISK_RT_OK);
  uint64_t stringArray = obelisk_rt_managed_word_from_object(objects[1]);
  obelisk_rt_class_descriptor_v1 descriptor{};
  descriptor.version = OBELISK_RT_VERSION;
  descriptor.class_id = 1;
  descriptor.instance_size = sizeof(void *);
  descriptor.instance_alignment = alignof(void *);
  ASSERT_EQ(obelisk_rt_v1_class_register(runtime, &descriptor), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &descriptor, &objects[10]),
            OBELISK_RT_OK);
  obelisk_rt_string_v1 text = 0;
  obelisk_rt_gc_managed_root_v1 textRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_push(lane, &textRoot, &text),
            OBELISK_RT_OK);
  const std::string longText = "z key survives deletion and GC";
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, longText.data(), longText.size(),
                                        &text),
            OBELISK_RT_OK);
  const uint64_t firstID = 2, secondID = 3;
  std::array<uint8_t, 17> negative{};
  negative.fill(255);
  negative.back() = 3;
  obelisk_rt_assoc_key_v1 negativeKey{};
  negativeKey.kind = OBELISK_RT_ASSOC_KEY_SIGNED;
  negativeKey.width = 130;
  negativeKey.value_data = negative.data();
  ASSERT_EQ(obelisk_rt_v1_assoc_write(lane, objects[0], &negativeKey, &firstID,
                                      nullptr),
            OBELISK_RT_OK);
  std::array<uint8_t, 17> positive{};
  positive[0] = 1;
  obelisk_rt_assoc_key_v1 positiveKey = negativeKey;
  positiveKey.value_data = positive.data();
  ASSERT_EQ(obelisk_rt_v1_assoc_write(lane, objects[0], &positiveKey, &secondID,
                                      nullptr),
            OBELISK_RT_OK);
  std::array<uint8_t, 17> wide{};
  wide[12] = 16;
  auto wideKey = negativeKey;
  wideKey.value_data = wide.data();
  ASSERT_EQ(
      obelisk_rt_v1_assoc_write(lane, objects[0], &wideKey, &firstID, nullptr),
      OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_assoc_set_default(lane, objects[0], &secondID, nullptr),
      OBELISK_RT_OK);
  for (size_t entry = 0; entry != 2; ++entry) {
    const uint64_t actual = entry ? secondID : firstID;
    obelisk_rt_assoc_key_v1 key{};
    key.kind = OBELISK_RT_ASSOC_KEY_STRING;
    key.string = text;
    if (!entry)
      ASSERT_EQ(obelisk_rt_v1_string_create(lane, "aaa", 3, &key.string),
                OBELISK_RT_OK);
    ASSERT_EQ(
        obelisk_rt_v1_assoc_write(lane, objects[1], &key, &actual, nullptr),
        OBELISK_RT_OK);
    std::array<uint8_t, 16> record{};
    const int32_t number = entry + 1;
    std::memcpy(record.data(), &number, 4);
    std::memcpy(record.data() + 8, &text, 8);
    key = {};
    key.kind = OBELISK_RT_ASSOC_KEY_ELEMENT;
    key.width = record.size() * 8;
    key.value_data = record.data();
    ASSERT_EQ(
        obelisk_rt_v1_assoc_write(lane, objects[2], &key, &actual, nullptr),
        OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_container_create_typed(
                  lane, OBELISK_RT_CONTAINER_DYNAMIC_ARRAY, 8123,
                  OBELISK_RT_ELEMENT_BITS, 0, 8, 8, 64, nullptr, 0, 1,
                  UINT64_MAX, &objects[8 + entry]),
              OBELISK_RT_OK);
    const uint64_t wildcard = entry ? 256 : 1;
    ASSERT_EQ(obelisk_rt_v1_container_write(lane, objects[8 + entry], 0,
                                            &wildcard, nullptr),
              OBELISK_RT_OK);
    key = {};
    key.kind = OBELISK_RT_ASSOC_KEY_WILDCARD;
    key.object = objects[8 + entry];
    ASSERT_EQ(
        obelisk_rt_v1_assoc_write(lane, objects[3], &key, &actual, nullptr),
        OBELISK_RT_OK);
    key = {};
    key.kind = OBELISK_RT_ASSOC_KEY_SIGNED;
    key.width = 32;
    key.value = static_cast<uint32_t>(entry ? 7 : -3);
    const uint64_t pair[] = {actual, entry ? uint64_t{0} : secondID};
    ASSERT_EQ(obelisk_rt_v1_assoc_write(lane, objects[4], &key, pair, nullptr),
              OBELISK_RT_OK);
    key = {};
    key.kind = OBELISK_RT_ASSOC_KEY_CLASS;
    key.object = entry ? objects[10] : nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_assoc_write(lane, objects[5], &key, &actual, nullptr),
        OBELISK_RT_OK);
  }
  obelisk_rt_assoc_key_v1 processKey{};
  processKey.kind = OBELISK_RT_ASSOC_KEY_PROCESS;
  ASSERT_EQ(obelisk_rt_v1_assoc_write(lane, objects[6], &processKey, &secondID,
                                      nullptr),
            OBELISK_RT_OK);
  // Container assignment copies its associative value after entries exist.
  ASSERT_EQ(
      obelisk_rt_v1_container_write(lane, objects[7], 0, &stringArray, nullptr),
      OBELISK_RT_OK);
  for (size_t slot = 0; slot != names.size(); ++slot) {
    uint64_t word = obelisk_rt_managed_word_from_object(objects[slot]);
    assign(std::string("top.") + names[slot], &word, 64);
  }
  uint64_t dynamicWord = obelisk_rt_managed_word_from_object(objects[7]);
  assign("top.dynamic", &dynamicWord, 64);
  auto top = named("top");
  auto first = named("top.first.\\m.dot "),
       second = named("top.second.\\m.dot ");
  std::array<vpiHandle, 7> arrays{};
  std::array<std::vector<vpiHandle>, 7> elements;
  auto actual = [&](vpiHandle selected, vpiHandle expected) {
    ASSERT_NE(selected, nullptr);
    EXPECT_EQ(vpi_get(vpiType, selected), vpiVirtualInterfaceVar);
    EXPECT_EQ(vpi_get(vpiArrayMember, selected), 1);
    auto result = keep(vpi_handle(vpiActual, selected));
    if (expected)
      EXPECT_EQ(vpi_compare_objects(result, expected), 1);
    else
      EXPECT_EQ(result, nullptr);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
  };
  for (size_t i = 0; i != names.size(); ++i) {
    arrays[i] = named(std::string("top.") + names[i]);
    EXPECT_EQ(vpi_get(vpiArrayType, arrays[i]), vpiAssocArray);
    EXPECT_EQ(vpi_handle(vpiLeftRange, arrays[i]), nullptr);
    EXPECT_EQ(vpi_handle(vpiRightRange, arrays[i]), nullptr);
    auto ranges = children(arrays[i], vpiRange);
    ASSERT_FALSE(ranges.empty());
    EXPECT_EQ(vpi_get(vpiSize, ranges[0]), 0);
    elements[i] = children(arrays[i], vpiReg);
  }
  ASSERT_EQ(elements[0].size(), 3u);
  ASSERT_EQ(elements[1].size(), 2u);
  ASSERT_EQ(elements[2].size(), 2u);
  ASSERT_EQ(elements[3].size(), 2u);
  ASSERT_EQ(elements[4].size(), 4u);
  ASSERT_EQ(elements[5].size(), 2u);
  ASSERT_EQ(elements[6].size(), 1u);
  actual(elements[0][0], first);
  actual(elements[0][1], second);
  actual(elements[0][2], first);
  auto indexOf = [&](vpiHandle selected) {
    auto indices = children(selected, vpiIndex);
    EXPECT_EQ(indices.size(), 1u);
    return indices.empty() ? nullptr : indices.front();
  };
  auto negativeIndex = indexOf(elements[0][0]);
  EXPECT_EQ(integer(negativeIndex), -1);
  EXPECT_EQ(vpi_get(vpiSize, negativeIndex), 130);
  auto negativeSelected = keep(vpi_handle_by_index(arrays[0], -1));
  EXPECT_EQ(vpi_compare_objects(negativeSelected, elements[0][0]), 1);
  EXPECT_EQ(vpi_handle_by_index(arrays[0], 99), nullptr);
  EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiParent, negativeSelected)),
                                arrays[0]),
            1);
  auto stringIndex = indexOf(elements[1][1]);
  s_vpi_value stringValue{};
  stringValue.format = vpiStringVal;
  vpi_get_value(stringIndex, &stringValue);
  ASSERT_NE(stringValue.value.str, nullptr);
  EXPECT_EQ(std::string(stringValue.value.str), longText);
  EXPECT_EQ(std::string(vpi_get_str(vpiFullName, elements[1][1])),
            "top.strings[\"" + longText + "\"]");
  auto recordIndex = indexOf(elements[2][0]);
  EXPECT_EQ(vpi_get(vpiType, recordIndex), vpiOperation);
  auto operands = children(recordIndex, vpiOperand);
  ASSERT_EQ(operands.size(), 2u);
  EXPECT_EQ(integer(operands[0]), 1);
  // Canonical key equality ignores native structure padding.
  std::array<uint8_t, 16> sameRecord{};
  const int32_t sameNumber = 1;
  std::memcpy(sameRecord.data(), &sameNumber, 4);
  std::memcpy(sameRecord.data() + 8, &text, 8);
  sameRecord[4] = 0x7f;
  obelisk_rt_assoc_key_v1 sameRecordKey{};
  sameRecordKey.kind = OBELISK_RT_ASSOC_KEY_ELEMENT;
  sameRecordKey.width = sameRecord.size() * 8;
  sameRecordKey.value_data = sameRecord.data();
  ASSERT_EQ(obelisk_rt_v1_assoc_write(lane, objects[2], &sameRecordKey,
                                      &secondID, nullptr),
            OBELISK_RT_OK);
  actual(elements[2][0], second);
  auto recordElements = children(arrays[2], vpiReg);
  ASSERT_EQ(recordElements.size(), 2u);
  EXPECT_EQ(vpi_compare_objects(recordElements[0], elements[2][0]), 1);
  EXPECT_EQ(integer(indexOf(elements[3][0])), 1);
  EXPECT_EQ(integer(indexOf(elements[3][1])), 256);
  EXPECT_EQ(vpi_compare_objects(keep(vpi_handle_by_index(arrays[3], 1)),
                                elements[3][0]),
            1);
  PLI_INT32 indices[] = {-3, 1};
  auto nested = keep(vpi_handle_by_multi_index(arrays[4], 2, indices));
  actual(nested, second);
  auto nestedIndices = children(nested, vpiIndex);
  ASSERT_EQ(nestedIndices.size(), 2u);
  EXPECT_EQ(integer(nestedIndices[0]), 1);
  EXPECT_EQ(integer(nestedIndices[1]), -3);
  auto classIndex = indexOf(elements[5][1]);
  EXPECT_EQ(vpi_get(vpiType, classIndex), vpiClassVar);
  auto classObject = keep(vpi_handle(vpiClassObj, classIndex));
  ASSERT_NE(classObject, nullptr);
  EXPECT_EQ(vpi_get64(vpiObjId, classObject),
            obelisk_rt_v1_object_id(objects[10]));
  auto processIndex = indexOf(elements[6][0]);
  EXPECT_EQ(vpi_get(vpiType, processIndex), vpiClassVar);
  EXPECT_EQ(vpi_handle(vpiClassObj, processIndex), nullptr);
  auto dynamic = named("top.dynamic");
  EXPECT_EQ(vpi_get(vpiSize, dynamic), 2);
  auto selectedAssoc = keep(vpi_handle_by_index(dynamic, 0));
  ASSERT_NE(selectedAssoc, nullptr);
  EXPECT_EQ(vpi_get(vpiType, selectedAssoc), vpiRegArray);
  EXPECT_EQ(vpi_get(vpiSize, selectedAssoc), 2);
  auto dynamicElements = children(dynamic, vpiReg);
  ASSERT_EQ(dynamicElements.size(), 2u);
  actual(dynamicElements[1], second);
  EXPECT_EQ(children(top, vpiVirtualInterfaceVar).size(), 18u);

  // Key snapshots survive entry deletion; live values resolve the same key,
  // independently of hash-table ordinal or a container's replacement.
  auto pending = vpi_iterate(vpiReg, arrays[0]);
  ASSERT_NE(pending, nullptr);
  ASSERT_EQ(obelisk_rt_v1_assoc_delete(objects[0], &negativeKey),
            OBELISK_RT_OK);
  auto deleted = keep(vpi_scan(pending));
  ASSERT_NE(deleted, nullptr);
  EXPECT_EQ(vpi_handle(vpiActual, deleted), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  actual(keep(vpi_scan(pending)), second);
  actual(keep(vpi_scan(pending)), first);
  EXPECT_EQ(vpi_scan(pending), nullptr);
  ASSERT_EQ(obelisk_rt_v1_assoc_write(lane, objects[0], &negativeKey, &secondID,
                                      nullptr),
            OBELISK_RT_OK);
  actual(negativeSelected, second);
  uint64_t emptyStrings = obelisk_rt_managed_word_from_object(objects[11]);
  assign("top.strings", &emptyStrings, 64);
  EXPECT_EQ(vpi_handle(vpiActual, elements[1][1]), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  actual(dynamicElements[1], second);
  obelisk_rt_string_v1 copiedText = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, longText.data(), longText.size(),
                                        &copiedText),
            OBELISK_RT_OK);
  obelisk_rt_assoc_key_v1 copiedKey{};
  copiedKey.kind = OBELISK_RT_ASSOC_KEY_STRING;
  copiedKey.string = copiedText;
  ASSERT_EQ(obelisk_rt_v1_assoc_write(lane, objects[11], &copiedKey, &firstID,
                                      nullptr),
            OBELISK_RT_OK);
  actual(elements[1][1], first);
  EXPECT_EQ(
      vpi_compare_objects(children(arrays[1], vpiReg).at(0), elements[1][1]),
      1);
  uint64_t zero = 0;
  for (auto name : names)
    assign(std::string("top.") + name, &zero, 64);
  assign("top.dynamic", &zero, 64);
  text = 0;
  objects.fill(nullptr);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  obelisk_rt_gc_statistics_v1 collected{};
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(runtime, &collected), OBELISK_RT_OK);
  EXPECT_EQ(collected.live_objects, 0u);
  EXPECT_EQ(vpi_get(vpiType, classObject), vpiUndefined);
  EXPECT_EQ(vpi_get(vpiType, classIndex), vpiUndefined);
  EXPECT_EQ(vpi_get(vpiType, elements[5][1]), vpiVirtualInterfaceVar);
  EXPECT_EQ(vpi_handle(vpiActual, elements[5][1]), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  stringValue.format = vpiStringVal;
  vpi_get_value(stringIndex, &stringValue);
  EXPECT_EQ(std::string(stringValue.value.str), longText);
  EXPECT_EQ(integer(negativeIndex), -1);
  for (auto handle : handles)
    if (vpi_get(vpiType, handle) != vpiUndefined)
      vpi_release_handle(handle);
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &textRoot), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_range_pop(lane, &roots), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(runtime);
}
} // namespace
