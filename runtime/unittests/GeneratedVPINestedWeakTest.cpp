//===- GeneratedVPINestedWeakTest.cpp - Copied weak child lifetime -----===//

#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "talos/Conversion/SimulationToBytecode.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace {
TEST(GeneratedDesignDatabase, MailboxWeakChildren) {
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
  const talos_rt_execution_descriptor_v1 execution{
      TALOS_RT_VERSION,
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
  talos_rt_context *raw = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &raw),
            TALOS_RT_OK);
  std::unique_ptr<talos_rt_context, decltype(&talos_rt_v1_context_destroy)>
      runtime(raw, talos_rt_v1_context_destroy);
  ASSERT_EQ(talos_rt_v1_vpi_startup(raw, nullptr, 0), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_end_compile(raw), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_start_simulation(raw), TALOS_RT_OK);
  talos_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_lane_create(raw, &lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_enter(lane), TALOS_RT_OK);
  std::array<talos_rt_object_v1 *, 3> objects{};
  talos_rt_gc_root_range_v1 roots{};
  ASSERT_EQ(talos_rt_v1_gc_root_range_push(lane, &roots, objects.data(),
                                             objects.size()),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_semaphore_create(lane, 1, &objects[2]),
            TALOS_RT_OK);
  const uint64_t identity = talos_rt_v1_object_id(objects[2]);
  const uint64_t reference = talos_rt_managed_word_from_object(objects[2]);
  std::array<talos_rt_design_cursor_v1, 2> storages{};
  std::array<vpiHandle, 2> containers{}, pending{}, references{},
      classObjects{};
  const std::array<const char *, 2> names{"top.records", "top.unions"};
  const std::array<const char *, 2> symbols{"records", "unions"};
  std::vector<vpiHandle> retained;
  auto keep = [&](vpiHandle handle) {
    EXPECT_NE(handle, nullptr);
    if (handle)
      retained.push_back(handle);
    return handle;
  };
  auto field = [&](talos_rt_design_cursor_v1 type, unsigned ordinal) {
    talos_rt_design_cursor_v1 cursor{};
    talos_rt_design_type_info_v1 result{};
    EXPECT_EQ(talos_rt_cached_design_type_child(raw, type, ordinal, &cursor),
              TALOS_RT_OK);
    EXPECT_EQ(talos_rt_cached_design_type_info(raw, cursor, &result),
              TALOS_RT_OK);
    return result;
  };
  auto putBits = [](std::vector<uint8_t> &value, uint64_t offset,
                    unsigned width, uint64_t bits) {
    ASSERT_LE(offset + width, value.size() * 8);
    for (unsigned bit = 0; bit != width; ++bit)
      if ((bits >> bit) & 1)
        value[(offset + bit) / 8] |= uint8_t{1} << ((offset + bit) % 8);
  };
  for (size_t index = 0; index != names.size(); ++index) {
    auto declaration = mlir::cast<talos::sim::SimVPITypespecDeclOp>(
        mlir::SymbolTable::lookupSymbolIn(design, symbols[index]));
    auto descriptor = talos::sim::getElementDescriptor(
        declaration.getMessageTypeAttr().getValue());
    ASSERT_TRUE(descriptor);
    std::vector<talos_rt_element_trace_slot_v1> slots;
    for (size_t slot = 0; slot != descriptor->traceOffsets.size(); ++slot)
      slots.push_back({static_cast<uint64_t>(descriptor->traceOffsets[slot]),
                       static_cast<uint32_t>(descriptor->traceKinds[slot]), 0});
    ASSERT_EQ(
        talos_rt_v1_mailbox_create_typed(
            lane, descriptor->typeID, static_cast<uint32_t>(descriptor->kind),
            static_cast<uint32_t>(descriptor->flags), descriptor->valueSize,
            descriptor->alignment, descriptor->bitWidth, slots.data(),
            slots.size(), 0, &objects[index]),
        TALOS_RT_OK);
    ASSERT_EQ(talos_rt_cached_design_lookup(
                  raw, reinterpret_cast<const uint8_t *>(names[index]),
                  std::strlen(names[index]), &storages[index]),
              TALOS_RT_OK);
    talos_rt_design_cursor_v1 semantic{}, owner{}, physical{};
    ASSERT_EQ(
        talos_rt_cached_design_semantic_root(raw, storages[index], &semantic),
        TALOS_RT_OK);
    ASSERT_EQ(
        talos_rt_cached_vpi_semantic_class_typespec(raw, semantic, &owner),
        TALOS_RT_OK);
    talos_rt_design_semantic_type_edge_v1 edge{};
    ASSERT_EQ(
        talos_rt_cached_design_semantic_type_edge(raw, semantic, 0, &edge),
        TALOS_RT_OK);
    ASSERT_EQ(talos_rt_cached_vpi_mailbox_element_type(raw, owner, edge.child,
                                                         &physical),
              TALOS_RT_OK);
    std::vector<uint8_t> value(descriptor->valueSize, 0);
    uint64_t offset = 0;
    if (index) {
      talos_rt_design_type_info_v1 type{};
      ASSERT_EQ(talos_rt_cached_design_type_info(raw, physical, &type),
                TALOS_RT_OK);
      putBits(value, type.bit_width - type.tag_bits, type.tag_bits, 1);
      auto record = field(physical, 0);
      physical = record.element_type;
      offset = record.packed_offset;
    }
    putBits(value, offset + field(physical, 0).packed_offset, 64, reference);
    putBits(value, offset + field(physical, 1).packed_offset, 32, 73);
    uint32_t success = 0;
    ASSERT_EQ(talos_rt_v1_mailbox_try_put(lane, objects[index], value.data(),
                                            nullptr, &success),
              TALOS_RT_OK);
    ASSERT_EQ(success, 1u);
    const uint64_t word = talos_rt_managed_word_from_object(objects[index]);
    ASSERT_EQ(
        talos_rt_v1_design_write(raw, storages[index], &word, nullptr, 64),
        TALOS_RT_OK);
    auto variable =
        keep(vpi_handle_by_name(const_cast<char *>(names[index]), nullptr));
    auto mailbox = vpi_handle(vpiClassObj, variable);
    ASSERT_NE(mailbox, nullptr);
    auto messages = vpi_iterate(vpiMessages, mailbox);
    ASSERT_NE(messages, nullptr);
    auto message = keep(vpi_scan(messages));
    EXPECT_EQ(vpi_scan(messages), nullptr);
    containers[index] = message;
    if (index) {
      auto members = vpi_iterate(vpiMember, message);
      ASSERT_NE(members, nullptr);
      containers[index] = keep(vpi_scan(members));
      EXPECT_EQ(vpi_scan(members), nullptr);
    }
    const int selector = index ? vpiMember : vpiOperand;
    pending[index] = vpi_iterate(selector, containers[index]);
    ASSERT_NE(pending[index], nullptr);
    auto current = vpi_iterate(selector, containers[index]);
    ASSERT_NE(current, nullptr);
    references[index] = keep(vpi_scan(current));
    keep(vpi_scan(current));
    EXPECT_EQ(vpi_scan(current), nullptr);
    EXPECT_EQ(vpi_get64(vpiObjId, references[index]), identity);
    classObjects[index] = vpi_handle(vpiClassObj, references[index]);
    ASSERT_NE(classObjects[index], nullptr);
    EXPECT_EQ(vpi_release_handle(mailbox), 1);
  }
  const uint64_t zero = 0;
  for (auto storage : storages)
    ASSERT_EQ(talos_rt_v1_design_write(raw, storage, &zero, nullptr, 64),
              TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_root_range_pop(lane, &roots), TALOS_RT_OK);
  objects.fill(nullptr);
  ASSERT_EQ(talos_rt_v1_gc_collect(lane), TALOS_RT_OK);
  talos_rt_gc_statistics_v1 statistics{};
  ASSERT_EQ(talos_rt_v1_gc_statistics(raw, &statistics), TALOS_RT_OK);
  EXPECT_EQ(statistics.live_objects, 0u);
  for (size_t index = 0; index != containers.size(); ++index) {
    EXPECT_EQ(vpi_get(vpiType, classObjects[index]), vpiUndefined);
    EXPECT_EQ(vpi_get(vpiType, references[index]), vpiClassVar);
    auto referenceNode = keep(vpi_scan(pending[index]));
    ASSERT_NE(referenceNode, nullptr);
    EXPECT_EQ(vpi_compare_objects(referenceNode, references[index]), 1);
    EXPECT_EQ(vpi_get64(vpiObjId, referenceNode), vpiUndefined);
    EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
    EXPECT_EQ(vpi_handle(vpiClassObj, referenceNode), nullptr);
    EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
    auto type = keep(vpi_handle(vpiTypespec, referenceNode));
    EXPECT_EQ(vpi_get(vpiType, type), vpiClassTypespec);
    if (index) {
      EXPECT_STREQ(vpi_get_str(vpiName, referenceNode), "reference");
      auto parent = keep(vpi_handle(vpiParent, referenceNode));
      EXPECT_EQ(vpi_compare_objects(parent, containers[index]), 1);
    }
    auto sibling = keep(vpi_scan(pending[index]));
    ASSERT_NE(sibling, nullptr);
    s_vpi_value value{};
    value.format = vpiIntVal;
    vpi_get_value(sibling, &value);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(value.value.integer, 73);
    EXPECT_EQ(vpi_scan(pending[index]), nullptr);
  }
  for (auto handle : retained)
    EXPECT_EQ(vpi_release_handle(handle), 1);
  ASSERT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
}
} // namespace
