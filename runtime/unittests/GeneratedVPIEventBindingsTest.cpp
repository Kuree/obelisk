//===- GeneratedVPIEventBindingsTest.cpp - Named event identities --------===//

#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Analysis/ManagedClassLayoutAnalysis.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Reflection/DesignReflection.h"
#include "obelisk/Runtime/Runtime.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "llvm/IR/DataLayout.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {
using namespace obelisk::reflection;

uint64_t imageChecksum(const std::vector<uint8_t> &bytes) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (size_t index = 0; index != bytes.size(); ++index) {
    hash ^= index >= 32 && index < 40 ? uint8_t{0} : bytes[index];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

TEST(GeneratedDesignDatabase, EventBindings) {
  const char *inputPath = std::getenv("OBELISK_TEST_INPUT");
  ASSERT_NE(inputPath, nullptr);
  mlir::DialectRegistry registry;
  registry.insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                  obelisk::sim::ObeliskSimulationDialect>();
  mlir::MLIRContext context(registry);
  auto module = mlir::parseSourceFile<mlir::ModuleOp>(inputPath, &context);
  ASSERT_TRUE(module);
  auto design = *module->getOps<obelisk::sim::SimDesignOp>().begin();
  obelisk::SimulationBytecodeOptions options;
  options.vpi = "full";
  auto encoded = obelisk::encodeSimulationDesign(design, options);
  ASSERT_TRUE(mlir::succeeded(encoded));
  uint64_t checksum = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    checksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  const obelisk_rt_execution_descriptor_v1 execution{
      OBELISK_RT_VERSION,
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
  auto event = [&](const char *name) {
    obelisk_rt_design_cursor_v1 cursor{};
    EXPECT_EQ(obelisk_rt_cached_design_lookup(
                  runtime, reinterpret_cast<const uint8_t *>(name),
                  std::strlen(name), &cursor),
              OBELISK_RT_OK);
    return cursor;
  };
  uint64_t identity = UINT64_MAX;
  EXPECT_EQ(obelisk_rt_cached_vpi_event_binding(runtime, event("top.first"),
                                                &identity),
            OBELISK_RT_OK);
  EXPECT_EQ(identity, 0u);
  EXPECT_EQ(obelisk_rt_cached_vpi_event_binding(runtime, event("top.second"),
                                                &identity),
            OBELISK_RT_OK);
  EXPECT_EQ(identity, 7u);
  EXPECT_EQ(obelisk_rt_cached_vpi_event_binding(runtime, event("top.unbound"),
                                                &identity),
            OBELISK_RT_EOF);
  EXPECT_EQ(identity, 0u);
  EXPECT_EQ(obelisk_rt_cached_vpi_event_binding(runtime, {}, &identity),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(
      obelisk_rt_cached_vpi_event_binding(runtime, event("top.first"), nullptr),
      OBELISK_RT_INVALID_ARGUMENT);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(runtime, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(runtime), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(runtime), OBELISK_RT_OK);
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(runtime, &lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);
  auto layouts = obelisk::analysis::ManagedClassLayoutAnalysis::compute(
      design,
      llvm::DataLayout((*module)
                           ->getAttrOfType<mlir::StringAttr>("llvm.data_layout")
                           .getValue()));
  ASSERT_TRUE(mlir::succeeded(layouts));
  const auto *layout = layouts->lookup(uint64_t{1});
  ASSERT_NE(layout, nullptr);
  std::map<std::string, uint64_t> offsets;
  for (const auto &field : layout->fields) {
    auto declaration = field.declaration;
    offsets.emplace(declaration.getSymName().str(), field.offset);
  }
  obelisk_rt_class_descriptor_v1 descriptor{};
  descriptor.version = OBELISK_RT_VERSION;
  descriptor.class_id = 1;
  descriptor.instance_size = layout->size;
  descriptor.instance_alignment = layout->alignment;
  ASSERT_EQ(obelisk_rt_v1_class_register(runtime, &descriptor), OBELISK_RT_OK);
  obelisk_rt_object_v1 *object = nullptr;
  obelisk_rt_gc_root_v1 root{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &root, &object), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &descriptor, &object),
            OBELISK_RT_OK);
  const std::array<uint64_t, 2> eventIDs{0, 7};
  ASSERT_EQ(obelisk_rt_v1_object_write(object, offsets.at("ready"),
                                       eventIDs.data(), sizeof(uint64_t)),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_write(object, offsets.at("events"),
                                       eventIDs.data(), sizeof(eventIDs)),
            OBELISK_RT_OK);
  auto objectStorage = event("top.obj");
  auto aliasStorage = event("top.alias");
  uint64_t word = obelisk_rt_managed_word_from_object(object);
  ASSERT_EQ(
      obelisk_rt_v1_design_write(runtime, objectStorage, &word, nullptr, 64),
      OBELISK_RT_OK);
  word = 0;
  ASSERT_EQ(
      obelisk_rt_v1_design_write(runtime, aliasStorage, &word, nullptr, 64),
      OBELISK_RT_OK);

  // Run the authored initializer normally: it creates three real design
  // threads with event/event-order waits. No scheduler wait records are forged.
  auto initial = std::find_if(
      encoded->functions.begin(), encoded->functions.end(),
      [](const auto &function) { return function.symbol == "initial"; });
  ASSERT_NE(initial, encoded->functions.end());
  obelisk_rt_design_bytecode_entry_v1 entry{&execution, initial->index, 0};
  uint64_t scratchSize = 0, scratchAlignment = 0;
  ASSERT_EQ(obelisk_rt_validate_design_bytecode(entry, runtime, &scratchSize,
                                                &scratchAlignment),
            OBELISK_RT_OK);
  ASSERT_LE(scratchAlignment, alignof(uint64_t));
  std::vector<uint64_t> frame((scratchSize + 7) / 8);
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_execute_design_bytecode(entry, runtime, frame.data(),
                                               frame.size() * 8, 0, scratchSize,
                                               0, 0, &action),
            OBELISK_RT_OK);
  ASSERT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(runtime), OBELISK_RT_OK);
  ASSERT_EQ(runtime->scheduledDesignTasks.size(), 3u);

  std::vector<vpiHandle> handles;
  std::vector<vpiHandle> threads;
  auto keep = [&](vpiHandle handle) {
    EXPECT_NE(handle, nullptr);
    if (handle)
      handles.push_back(handle);
    return handle;
  };
  auto named = [&](const char *name, vpiHandle scope = nullptr) {
    return keep(vpi_handle_by_name(const_cast<char *>(name), scope));
  };
  auto firstEvent = named("top.first");
  auto secondEvent = named("top.second");
  auto unboundEvent = named("top.unbound");
  auto alias = named("top.alias");
  auto moduleScope = named("top");
  auto aliasScope = keep(vpi_handle(vpiScope, alias));
  EXPECT_EQ(vpi_compare_objects(aliasScope, moduleScope), 1);
  auto eventInventory = vpi_iterate(vpiNamedEvent, moduleScope);
  ASSERT_NE(eventInventory, nullptr);
  unsigned aliasOccurrences = 0;
  while (auto child = vpi_scan(eventInventory)) {
    keep(child);
    aliasOccurrences += vpi_compare_objects(child, alias) == 1;
  }
  EXPECT_EQ(aliasOccurrences, 1u);
  uint64_t fixedEventID = 0;
  EXPECT_EQ(
      obelisk_rt_cached_vpi_event_binding(runtime, aliasStorage, &fixedEventID),
      OBELISK_RT_EOF);

  auto variable = named("top.obj");
  auto receiver = keep(vpi_handle(vpiClassObj, variable));
  auto canonical = keep(vpi_handle(vpiClassTypespec, variable));
  auto definition = named("top.Node");
  auto ready = named("ready", receiver);
  auto events = named("events", receiver);
  auto firstElement = keep(vpi_handle_by_index(events, 0));
  auto secondElement = keep(vpi_handle_by_index(events, 1));
  auto shared = named("shared", receiver);
  auto unboundReady = named("ready", canonical);
  auto lexicalShared = named("shared", definition);
  std::vector<vpiHandle> eventTypes;
  for (auto source :
       {firstEvent, secondEvent, ready, firstElement, shared, alias}) {
    auto type = keep(vpi_handle(vpiTypespec, source));
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(vpi_get(vpiType, type), vpiEventTypespec);
    auto repeated = keep(vpi_handle(vpiTypespec, source));
    EXPECT_EQ(vpi_compare_objects(type, repeated), 1);
    eventTypes.push_back(type);
  }
  auto arrayType = keep(vpi_handle(vpiTypespec, events));
  EXPECT_EQ(vpi_get(vpiType, arrayType), vpiArrayTypespec);
  auto elementType = keep(vpi_handle(vpiElemTypespec, arrayType));
  EXPECT_EQ(vpi_get(vpiType, elementType), vpiEventTypespec);
  auto countWaiters = [&](vpiHandle source, size_t expected) {
    auto iterator = vpi_iterate(vpiWaitingProcesses, source);
    if (expected == 0) {
      EXPECT_EQ(iterator, nullptr);
      EXPECT_EQ(vpi_chk_error(nullptr), 0);
      return;
    }
    ASSERT_NE(iterator, nullptr);
    auto use = keep(vpi_handle(vpiUse, iterator));
    EXPECT_EQ(vpi_compare_objects(use, source), 1);
    size_t count = 0;
    while (auto thread = vpi_scan(iterator)) {
      keep(thread);
      threads.push_back(thread);
      EXPECT_EQ(vpi_get(vpiType, thread), vpiThread);
      EXPECT_EQ(vpi_get(vpiActive, thread), 0);
      ++count;
    }
    EXPECT_EQ(count, expected);
  };
  for (auto source : {firstEvent, secondEvent, ready, firstElement,
                      secondElement, shared, alias})
    countWaiters(source, 2);
  for (auto source : {unboundEvent, unboundReady, lexicalShared}) {
    EXPECT_EQ(vpi_iterate(vpiWaitingProcesses, source), nullptr);
    EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  }
  word = UINT64_MAX;
  ASSERT_EQ(
      obelisk_rt_v1_design_write(runtime, aliasStorage, &word, nullptr, 64),
      OBELISK_RT_OK);
  countWaiters(alias, 0);
  word = 7;
  ASSERT_EQ(
      obelisk_rt_v1_design_write(runtime, aliasStorage, &word, nullptr, 64),
      OBELISK_RT_OK);
  countWaiters(alias, 2);
  auto pending = keep(vpi_iterate(vpiWaitingProcesses, firstElement));
  // Waiting-process queries retain only a weak receiver recipe. The scalar
  // static event declaration and ordinary event aliases remain independent.
  word = 0;
  ASSERT_EQ(
      obelisk_rt_v1_design_write(runtime, objectStorage, &word, nullptr, 64),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane, &root), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  EXPECT_EQ(vpi_get(vpiType, pending), vpiUndefined);
  EXPECT_EQ(vpi_get(vpiType, ready), vpiUndefined);
  for (auto type : eventTypes)
    EXPECT_EQ(vpi_get(vpiType, type), vpiEventTypespec);
  EXPECT_EQ(vpi_get(vpiType, arrayType), vpiArrayTypespec);
  EXPECT_EQ(vpi_get(vpiType, elementType), vpiEventTypespec);
  countWaiters(shared, 2);

  obelisk_rt_v1_scheduler_event(runtime, 0, 0);
  countWaiters(firstEvent, 0); // Ready ordinary wait and consumed order prefix.
  countWaiters(secondEvent, 2);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(runtime), OBELISK_RT_OK);
  countWaiters(firstEvent, 0);
  countWaiters(secondEvent, 2);
  obelisk_rt_v1_scheduler_event(runtime, 7, 0);
  countWaiters(secondEvent, 0); // Both are ready before dispatch.
  countWaiters(alias, 0);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(runtime), OBELISK_RT_OK);
  EXPECT_TRUE(runtime->scheduledDesignTasks.empty());
  for (auto thread : threads)
    EXPECT_EQ(vpi_get(vpiType, thread), vpiUndefined);
  // Expired receivers/iterators/threads have already been reclaimed by VPI;
  // release whichever immutable declarations remain in the handle arena.
  for (auto handle : handles)
    (void)vpi_release_handle(handle);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(runtime);

  const auto &image = encoded->designDatabase;
  HeaderView header(image.data());
  SemanticDirectoryView directory(image.data() + header.getReserved());
  ASSERT_EQ(directory.getEventBindingCount(), 2u);
  const uint64_t offset = directory.getEventBindingOffset();
  const uint32_t first =
      EventBindingView(image.data() + offset).getEventIndexAndTable();
  auto rejects = [&](auto corrupt) {
    std::vector<uint8_t> bytes(image.begin(), image.end());
    corrupt(bytes);
    HeaderWriter(bytes.data()).setChecksum(imageChecksum(bytes));
    auto invalid = execution;
    invalid.design_database = bytes.data();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&invalid),
              OBELISK_RT_INVALID_DESIGN);
  };
  rejects([&](auto &bytes) {
    EventBindingWriter(bytes.data() + offset).setReserved(1);
  });
  rejects([&](auto &bytes) {
    EventBindingWriter(bytes.data() + offset).setEventID(UINT64_MAX);
  });
  rejects([&](auto &bytes) {
    EventBindingWriter(bytes.data() + offset).setEventID(UINT64_C(1) << 63);
  });
  rejects([&](auto &bytes) {
    EventBindingWriter(bytes.data() + offset + EventBindingLayout.size)
        .setEventIndexAndTable(first);
  });
  rejects([&](auto &bytes) {
    uint32_t root = 0;
    ASSERT_TRUE(tryPackTableIndex(TableKind::Scope, 0, root));
    EventBindingWriter(bytes.data() + offset).setEventIndexAndTable(root);
  });
  rejects([&](auto &bytes) {
    SemanticDirectoryWriter(bytes.data() + header.getReserved())
        .setEventBindingCount(UINT64_MAX);
  });
}
} // namespace
