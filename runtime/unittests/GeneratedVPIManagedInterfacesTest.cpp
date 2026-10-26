//===- GeneratedVPIManagedInterfacesTest.cpp - Live managed VIF arrays ----===//
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
TEST(GeneratedDesignDatabase, ManagedVirtualInterfaceArrays) {
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
  auto type = [&](uint64_t id) {
    for (auto storage :
         design.getBody().front().getOps<obelisk::sim::SimStorageDeclOp>())
      if (storage.getId() == id)
        return storage.getType();
    return mlir::Type{};
  };
  std::array<obelisk_rt_object_v1 *, 8> objects{};
  obelisk_rt_gc_root_range_v1 roots{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_range_push(lane, &roots, objects.data(),
                                             objects.size()),
            OBELISK_RT_OK);
  auto create = [&](size_t slot, mlir::Type elementType, uint32_t kind,
                    uint64_t size) {
    auto element = obelisk::sim::getElementDescriptor(elementType);
    ASSERT_TRUE(element);
    std::vector<obelisk_rt_element_trace_slot_v1> trace;
    for (size_t i = 0; i != element->traceOffsets.size(); ++i)
      trace.push_back({static_cast<uint64_t>(element->traceOffsets[i]),
                       static_cast<uint32_t>(element->traceKinds[i]), 0});
    ASSERT_EQ(obelisk_rt_v1_container_create_typed(
                  lane, kind, element->typeID,
                  static_cast<uint32_t>(element->kind),
                  static_cast<uint32_t>(element->flags), element->valueSize,
                  element->alignment, element->bitWidth, trace.data(),
                  trace.size(), size, UINT64_MAX, &objects[slot]),
              OBELISK_RT_OK);
  };
  auto interfaceType =
      mlir::cast<obelisk::sim::DynamicArrayType>(type(0)).getElementType();
  create(0, interfaceType, OBELISK_RT_CONTAINER_DYNAMIC_ARRAY, 2);
  create(1, interfaceType, OBELISK_RT_CONTAINER_QUEUE, 0);
  create(2, interfaceType, OBELISK_RT_CONTAINER_DYNAMIC_ARRAY, 1);
  create(3,
         mlir::cast<obelisk::sim::DynamicArrayType>(type(3)).getElementType(),
         OBELISK_RT_CONTAINER_DYNAMIC_ARRAY, 2);
  create(4,
         mlir::cast<obelisk::sim::DynamicArrayType>(type(4)).getElementType(),
         OBELISK_RT_CONTAINER_DYNAMIC_ARRAY, 1);
  create(5, interfaceType, OBELISK_RT_CONTAINER_DYNAMIC_ARRAY, 0);
  uint64_t firstID = 2, secondID = 3, nullID = 0;
  ASSERT_EQ(
      obelisk_rt_v1_container_write(lane, objects[0], 0, &firstID, nullptr),
      OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_container_write(lane, objects[0], 1, &secondID, nullptr),
      OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_container_write(lane, objects[2], 0, &secondID, nullptr),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_queue_push(lane, objects[1], 0, &secondID, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_queue_push(lane, objects[1], 0, &firstID, nullptr),
            OBELISK_RT_OK);
  uint64_t fixed0[] = {firstID, secondID}, fixed1[] = {secondID, nullID};
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, objects[3], 0, fixed0, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, objects[3], 1, fixed1, nullptr),
            OBELISK_RT_OK);
  uint64_t queueWord = reinterpret_cast<uintptr_t>(objects[1]);
  ASSERT_EQ(
      obelisk_rt_v1_container_write(lane, objects[4], 0, &queueWord, nullptr),
      OBELISK_RT_OK);
  uint64_t words[] = {reinterpret_cast<uintptr_t>(objects[0]),
                      reinterpret_cast<uintptr_t>(objects[2])};
  assign("top.dynamic", words, 64);
  assign("top.queue", &queueWord, 64);
  assign("top.fixed_dynamic", words, 128);
  uint64_t dynamicFixed = reinterpret_cast<uintptr_t>(objects[3]);
  assign("top.dynamic_fixed", &dynamicFixed, 64);
  uint64_t dynamicQueue = reinterpret_cast<uintptr_t>(objects[4]);
  assign("top.dynamic_queue", &dynamicQueue, 64);
  uint64_t sharedWord = reinterpret_cast<uintptr_t>(objects[2]);
  assign("top.Node.shared", &sharedWord, 64);
  auto *layout = layouts->lookup(uint64_t{1});
  ASSERT_NE(layout, nullptr);
  uint64_t portsOffset = UINT64_MAX;
  for (const auto &field : layout->fields) {
    auto declaration = field.declaration;
    if (declaration.getSymName() == "ports")
      portsOffset = field.offset;
  }
  ASSERT_NE(portsOffset, UINT64_MAX);
  obelisk_rt_trace_entry_v1 trace{portsOffset,
                                  0,
                                  1,
                                  OBELISK_RT_TRACE_STRONG,
                                  OBELISK_RT_MANAGED_SLOT_CONTAINER,
                                  nullptr};
  obelisk_rt_trace_layout_v1 traceLayout{
      OBELISK_RT_VERSION, 0, layout->size, layout->alignment, &trace, 1};
  obelisk_rt_class_descriptor_v1 descriptor{};
  descriptor.version = OBELISK_RT_VERSION;
  descriptor.class_id = 1;
  descriptor.instance_size = layout->size;
  descriptor.instance_alignment = layout->alignment;
  descriptor.layout = &traceLayout;
  ASSERT_EQ(obelisk_rt_v1_class_register(runtime, &descriptor), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &descriptor, &objects[6]),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_write(objects[6], portsOffset, &queueWord, 8),
            OBELISK_RT_OK);
  uint64_t objectWord = reinterpret_cast<uintptr_t>(objects[6]);
  assign("top.obj", &objectWord, 64);

  auto top = named("top");
  auto first = named("top.first.\\m.dot ");
  auto second = named("top.second.\\m.dot ");
  auto dynamic = named("top.dynamic");
  auto queue = named("top.queue");
  auto fixedDynamic = named("top.fixed_dynamic");
  auto dynamicFixedHandle = named("top.dynamic_fixed");
  auto dynamicQueueHandle = named("top.dynamic_queue");
  auto actual = [&](vpiHandle selected, vpiHandle expected) {
    ASSERT_NE(selected, nullptr);
    EXPECT_EQ(vpi_get(vpiType, selected), vpiVirtualInterfaceVar);
    EXPECT_EQ(vpi_get(vpiArrayMember, selected), 1);
    auto reference = keep(vpi_handle(vpiActual, selected));
    if (expected)
      EXPECT_EQ(vpi_compare_objects(reference, expected), 1);
    else
      EXPECT_EQ(reference, nullptr);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
  };
  auto d0 = keep(vpi_handle_by_index(dynamic, 0));
  auto d1 = keep(vpi_handle_by_index(dynamic, 1));
  actual(d0, first);
  actual(d1, second);
  EXPECT_EQ(vpi_compare_objects(keep(vpi_handle_by_index(dynamic, 0)), d0), 1);
  EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiParent, d0)), dynamic), 1);
  EXPECT_EQ(integer(children(d1, vpiIndex).at(0)), 1);
  auto declared = keep(vpi_handle(vpiTypespec, d0));
  EXPECT_EQ(vpi_get(vpiType, declared), vpiInterfaceTypespec);
  EXPECT_EQ(vpi_get(vpiIsModPort, declared), 1);
  EXPECT_EQ(vpi_get(vpiSize, dynamic), 2);
  EXPECT_EQ(vpi_get(vpiArrayType, dynamic), vpiDynamicArray);
  EXPECT_EQ(vpi_get(vpiArrayType, queue), vpiQueueArray);
  EXPECT_EQ(vpi_get(vpiArrayType, fixedDynamic), vpiStaticArray);
  EXPECT_EQ(vpi_get(vpiIsMemory, dynamic), 1);
  EXPECT_EQ(vpi_get(vpiIsMemory, dynamicQueueHandle), 0);
  EXPECT_EQ(integer(keep(vpi_handle(vpiLeftRange, dynamic))), 0);
  EXPECT_EQ(integer(keep(vpi_handle(vpiRightRange, dynamic))), 1);
  auto dynamicRanges = children(dynamic, vpiRange);
  ASSERT_EQ(dynamicRanges.size(), 1u);
  EXPECT_EQ(integer(keep(vpi_handle(vpiRightRange, dynamicRanges[0]))), 1);
  EXPECT_EQ(children(dynamic, vpiReg).size(), 2u);
  EXPECT_EQ(vpi_handle_by_index(dynamic, -1), nullptr);
  EXPECT_EQ(vpi_handle_by_index(dynamic, 2), nullptr);

  auto selected = [&](vpiHandle array, PLI_INT32 outer, PLI_INT32 inner,
                      vpiHandle expected) {
    PLI_INT32 indices[] = {outer, inner};
    auto element = keep(vpi_handle_by_multi_index(array, 2, indices));
    actual(element, expected);
    auto index = children(element, vpiIndex);
    ASSERT_EQ(index.size(), 2u);
    EXPECT_EQ(integer(index[0]), inner);
    EXPECT_EQ(integer(index[1]), outer);
    EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiParent, element)), array),
              1);
  };
  selected(fixedDynamic, 2, 1, second);
  selected(fixedDynamic, 1, 0, second);
  selected(dynamicFixedHandle, 0, 1, first);
  selected(dynamicFixedHandle, 0, 0, second);
  selected(dynamicFixedHandle, 1, 0, nullptr);
  selected(dynamicQueueHandle, 0, 1, first);
  EXPECT_EQ(vpi_get(vpiSize, fixedDynamic), 3);
  EXPECT_EQ(vpi_get(vpiSize, dynamicFixedHandle), 4);
  EXPECT_EQ(vpi_get(vpiSize, dynamicQueueHandle), 2);
  auto nestedRanges = children(dynamicQueueHandle, vpiRange);
  ASSERT_EQ(nestedRanges.size(), 2u);
  EXPECT_EQ(integer(keep(vpi_handle(vpiRightRange, nestedRanges[0]))), 0);
  EXPECT_EQ(vpi_handle(vpiLeftRange, nestedRanges[1]), nullptr);
  EXPECT_EQ(vpi_handle(vpiRightRange, nestedRanges[1]), nullptr);
  EXPECT_EQ(children(top, vpiVirtualInterfaceVar).size(), 13u);

  auto classObject = keep(vpi_handle(vpiClassObj, named("top.obj")));
  ASSERT_NE(classObject, nullptr);
  std::unordered_map<std::string, vpiHandle> members;
  for (auto member : children(classObject, vpiVariables))
    members.emplace(vpi_get_str(vpiName, member), member);
  ASSERT_EQ(members.size(), 2u);
  actual(keep(vpi_handle_by_index(members.at("ports"), 0)), second);
  actual(keep(vpi_handle_by_index(members.at("shared"), 0)), second);
  EXPECT_EQ(children(classObject, vpiVirtualInterfaceVar).size(), 3u);
  auto definition = named("top.Node");
  auto lexicalMembers = children(definition, vpiVariables);
  ASSERT_EQ(lexicalMembers.size(), 1u);
  EXPECT_EQ(vpi_handle_by_index(lexicalMembers[0], 0), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);

  // Index handles follow reassignment; they do not keep the old container
  // alive.
  assign("top.dynamic", &sharedWord, 64);
  actual(d0, second);
  EXPECT_EQ(vpi_handle(vpiActual, d1), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  EXPECT_EQ(vpi_get(vpiSize, dynamic), 1);
  EXPECT_EQ(integer(keep(vpi_handle(vpiRightRange, dynamicRanges[0]))), 0);
  auto pending = vpi_iterate(vpiReg, queue);
  ASSERT_NE(pending, nullptr);
  auto q0 = keep(vpi_handle_by_index(queue, 0));
  uint64_t popped = 0;
  uint32_t present = 0;
  ASSERT_EQ(obelisk_rt_v1_queue_pop(objects[1], 1, &popped, nullptr, &present),
            OBELISK_RT_OK);
  ASSERT_EQ(present, 1u);
  actual(q0, first);
  actual(keep(vpi_scan(pending)), first);
  auto removed = keep(vpi_scan(pending));
  ASSERT_NE(removed, nullptr);
  EXPECT_EQ(vpi_handle(vpiActual, removed), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  EXPECT_EQ(vpi_scan(pending), nullptr);
  ASSERT_EQ(obelisk_rt_v1_dynamic_array_resize(lane, objects[2], 0),
            OBELISK_RT_OK);
  EXPECT_EQ(vpi_get(vpiSize, dynamic), 0);
  EXPECT_EQ(vpi_get(vpiSize, dynamicRanges[0]), 0);
  EXPECT_EQ(vpi_handle(vpiLeftRange, dynamic), nullptr);
  EXPECT_EQ(vpi_handle(vpiRightRange, dynamicRanges[0]), nullptr);
  EXPECT_EQ(vpi_iterate(vpiReg, dynamic), nullptr);
  ASSERT_EQ(obelisk_rt_v1_dynamic_array_resize(lane, objects[2], 1),
            OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_container_write(lane, objects[2], 0, &firstID, nullptr),
      OBELISK_RT_OK);
  actual(d0, first);

  // Class receivers govern bound-view lifetime; ordinary root/index metadata
  // remains usable after all managed arrays have been reclaimed.
  uint64_t zeroes[] = {0, 0};
  for (const char *name :
       {"dynamic", "queue", "dynamic_fixed", "dynamic_queue", "obj"})
    assign(std::string("top.") + name, zeroes, 64);
  assign("top.fixed_dynamic", zeroes, 128);
  assign("top.Node.shared", zeroes, 64);
  std::fill(objects.begin(), objects.end(), nullptr);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  EXPECT_EQ(vpi_get(vpiType, classObject), vpiUndefined);
  EXPECT_EQ(vpi_get(vpiValid, d0), 1);
  EXPECT_EQ(vpi_get(vpiSize, fixedDynamic), 0);
  EXPECT_EQ(vpi_handle(vpiLeftRange, fixedDynamic), nullptr);
  auto fixedRanges = children(fixedDynamic, vpiRange);
  ASSERT_EQ(fixedRanges.size(), 2u);
  EXPECT_EQ(integer(keep(vpi_handle(vpiLeftRange, fixedRanges[0]))), 2);
  EXPECT_EQ(vpi_get(vpiSize, fixedRanges[1]), 0);
  EXPECT_EQ(vpi_handle(vpiActual, d0), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  EXPECT_EQ(vpi_get(vpiType, declared), vpiInterfaceTypespec);
  for (auto handle : handles)
    if (vpi_get(vpiType, handle) != vpiUndefined)
      vpi_release_handle(handle);
  ASSERT_EQ(obelisk_rt_v1_gc_root_range_pop(lane, &roots), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(runtime);
}
} // namespace
