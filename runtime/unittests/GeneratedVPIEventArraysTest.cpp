//===- GeneratedVPIEventArraysTest.cpp - Event array storage relations
//-----===//

#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectRegistry.h"
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
#include <vector>

namespace {
using namespace talos::reflection;

uint64_t imageChecksum(const std::vector<uint8_t> &bytes) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (size_t index = 0; index != bytes.size(); ++index) {
    hash ^= index >= 32 && index < 40 ? uint8_t{0} : bytes[index];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

TEST(GeneratedDesignDatabase, EventArrays) {
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
  ASSERT_EQ(talos_rt_v1_design_validate(&execution), TALOS_RT_OK);
  talos_rt_context *runtime = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &runtime),
            TALOS_RT_OK);
  auto lookup = [&](const char *name) {
    talos_rt_design_cursor_v1 cursor{};
    EXPECT_EQ(talos_rt_cached_design_lookup(
                  runtime, reinterpret_cast<const uint8_t *>(name),
                  std::strlen(name), &cursor),
              TALOS_RT_OK);
    return cursor;
  };
  const std::array<const char *, 4> names{
      "top.events[2][-1]", "top.events[2][0]", "top.events[1][-1]",
      "top.events[1][0]"};
  std::array<talos_rt_design_cursor_v1, 4> cursors{};
  std::array<VPIEventCellBinding, 4> cells{};
  for (size_t index = 0; index != names.size(); ++index) {
    cursors[index] = lookup(names[index]);
    ASSERT_EQ(talos_rt_cached_vpi_event_array_cell(runtime, cursors[index],
                                                     &cells[index]),
              TALOS_RT_OK);
    EXPECT_EQ(cells[index].storage.offset, cells[0].storage.offset);
    EXPECT_EQ(cells[index].bitOffset, index * 64);
  }
  auto arrayCursor = lookup("top.events");
  auto scalarCursor = lookup("top.scalar");
  EXPECT_NE(arrayCursor.offset, cells[0].storage.offset);
  VPIEventCellBinding absent = cells[0];
  EXPECT_EQ(
      talos_rt_cached_vpi_event_array_cell(runtime, scalarCursor, &absent),
      TALOS_RT_EOF);
  EXPECT_EQ(absent.storage.offset, 0u);
  EXPECT_EQ(absent.bitOffset, 0u);
  EXPECT_EQ(talos_rt_cached_vpi_event_array_cell(runtime, {}, &absent),
            TALOS_RT_INVALID_HANDLE);
  EXPECT_EQ(
      talos_rt_cached_vpi_event_array_cell(runtime, cursors[0], nullptr),
      TALOS_RT_INVALID_ARGUMENT);
  VPIEventCellBinding other{};
  ASSERT_EQ(talos_rt_cached_vpi_event_array_cell(
                runtime, lookup("top.other[4]"), &other),
            TALOS_RT_OK);
  std::array<uint64_t, 4> words{0, 7, UINT64_MAX, 0};
  ASSERT_EQ(talos_rt_v1_design_write(runtime, cells[0].storage, words.data(),
                                       nullptr, 256),
            TALOS_RT_OK);
  const std::array<uint64_t, 2> otherWords{7, UINT64_MAX};
  ASSERT_EQ(talos_rt_v1_design_write(runtime, other.storage,
                                       otherWords.data(), nullptr, 128),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_startup(runtime, nullptr, 0), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_end_compile(runtime), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_start_simulation(runtime), TALOS_RT_OK);
  auto initial =
      std::find_if(encoded->functions.begin(), encoded->functions.end(),
                   [](const auto &f) { return f.symbol == "initial"; });
  ASSERT_NE(initial, encoded->functions.end());
  talos_rt_design_bytecode_entry_v1 entry{&execution, initial->index, 0};
  uint64_t scratchSize = 0, scratchAlignment = 0;
  ASSERT_EQ(talos_rt_validate_design_bytecode(entry, runtime, &scratchSize,
                                                &scratchAlignment),
            TALOS_RT_OK);
  ASSERT_LE(scratchAlignment, alignof(uint64_t));
  std::vector<uint64_t> frame((scratchSize + 7) / 8);
  talos_rt_fragment_action_v1 action{};
  ASSERT_EQ(talos_rt_execute_design_bytecode(entry, runtime, frame.data(),
                                               frame.size() * 8, 0, scratchSize,
                                               0, 0, &action),
            TALOS_RT_OK);
  ASSERT_EQ(action.kind, TALOS_RT_FRAGMENT_TERMINATE);
  ASSERT_EQ(talos_rt_v1_scheduler_run(runtime), TALOS_RT_OK);
  ASSERT_EQ(runtime->scheduledDesignTasks.size(), 2u);
  std::vector<vpiHandle> retained;
  auto keep = [&](vpiHandle handle) {
    EXPECT_NE(handle, nullptr);
    if (handle)
      retained.push_back(handle);
    return handle;
  };
  auto named = [&](const char *name) {
    return keep(vpi_handle_by_name(const_cast<char *>(name), nullptr));
  };
  auto top = named("top");
  auto events = named("top.events");
  auto eventAlias = named("top.EventAlias");
  auto gridAlias = named("top.EventGrid");
  auto arrayType = keep(vpi_handle(vpiTypespec, events));
  EXPECT_EQ(vpi_compare_objects(arrayType, gridAlias), 1);
  std::array<vpiHandle, 4> leaves{};
  auto iterator = vpi_iterate(vpiNamedEvent, events);
  ASSERT_NE(iterator, nullptr);
  for (size_t index = 0; index != leaves.size(); ++index) {
    leaves[index] = keep(vpi_scan(iterator));
    auto exact = named(names[index]);
    EXPECT_EQ(vpi_compare_objects(leaves[index], exact), 1);
    auto type = keep(vpi_handle(vpiTypespec, leaves[index]));
    EXPECT_EQ(vpi_compare_objects(type, eventAlias), 1);
    auto parent = keep(vpi_handle(vpiParent, leaves[index]));
    EXPECT_EQ(vpi_compare_objects(parent, events), 1);
    auto scope = keep(vpi_handle(vpiScope, leaves[index]));
    EXPECT_EQ(vpi_compare_objects(scope, top), 1);
  }
  EXPECT_EQ(vpi_scan(iterator), nullptr);
  auto partial = keep(vpi_handle_by_index(events, 1));
  auto selected = keep(vpi_handle_by_index(partial, 0));
  EXPECT_EQ(vpi_compare_objects(selected, leaves[3]), 1);
  PLI_INT32 indices[]{2, 0};
  auto multi = keep(vpi_handle_by_multi_index(events, 2, indices));
  EXPECT_EQ(vpi_compare_objects(multi, leaves[1]), 1);
  iterator = vpi_iterate(vpiNamedEventArray, top);
  ASSERT_NE(iterator, nullptr);
  size_t arrayCount = 0;
  while (auto array = vpi_scan(iterator)) {
    keep(array);
    ++arrayCount;
  }
  EXPECT_EQ(arrayCount,
            2u); // Private executable storage has no second identity.
  auto waiters = [&](vpiHandle event, size_t expected) {
    auto waiting = vpi_iterate(vpiWaitingProcesses, event);
    if (!expected) {
      EXPECT_EQ(waiting, nullptr);
      EXPECT_EQ(vpi_chk_error(nullptr), 0);
      return;
    }
    ASSERT_NE(waiting, nullptr);
    auto use = keep(vpi_handle(vpiUse, waiting));
    EXPECT_EQ(vpi_compare_objects(use, event), 1);
    size_t count = 0;
    while (auto thread = vpi_scan(waiting)) {
      keep(thread);
      EXPECT_EQ(vpi_get(vpiType, thread), vpiThread);
      ++count;
    }
    EXPECT_EQ(count, expected);
  };
  for (size_t index = 0; index != leaves.size(); ++index)
    waiters(leaves[index], index == 2 ? 0 : 1);
  waiters(named("top.other[4]"), 1);
  words[0] = 7;
  ASSERT_EQ(talos_rt_v1_design_write(runtime, cells[0].storage, words.data(),
                                       nullptr, 256),
            TALOS_RT_OK);
  talos_rt_v1_scheduler_event(runtime, 0, 0);
  waiters(leaves[0],
          1); // Reads the retargeted cell, not its ordinal or anchor ID.
  waiters(leaves[3], 0);
  ASSERT_EQ(talos_rt_v1_scheduler_run(runtime), TALOS_RT_OK);
  talos_rt_v1_scheduler_event(runtime, 7, 0);
  for (auto leaf : leaves)
    waiters(leaf, 0);
  ASSERT_EQ(talos_rt_v1_scheduler_run(runtime), TALOS_RT_OK);
  EXPECT_TRUE(runtime->scheduledDesignTasks.empty());
  for (auto handle : retained)
    (void)vpi_release_handle(handle);
  talos_rt_v1_context_destroy(runtime);

  const auto &image = encoded->designDatabase;
  HeaderView header(image.data());
  SemanticDirectoryView directory(image.data() + header.getReserved());
  ASSERT_EQ(directory.getEventArrayStorageBindingCount(), 2u);
  const uint64_t bindingOffset = directory.getEventArrayStorageBindingOffset();
  EventArrayStorageBindingView first(image.data() + bindingOffset);
  const uint64_t storageOffset = cells[0].storage.offset;
  ASSERT_EQ(header.getObjectOffset() +
                first.getStorageIndex() * ObjectLayout.size,
            storageOffset);
  ObjectView storage(image.data() + storageOffset);
  const uint64_t rootType = storage.getType();
  const uint64_t rowType = TypeView(image.data() + rootType).getElement();
  auto rejects = [&](auto corrupt) {
    std::vector<uint8_t> bytes(image.begin(), image.end());
    corrupt(bytes);
    HeaderWriter(bytes.data()).setChecksum(imageChecksum(bytes));
    auto invalid = execution;
    invalid.design_database = bytes.data();
    EXPECT_EQ(talos_rt_v1_design_validate(&invalid),
              TALOS_RT_INVALID_DESIGN);
  };
  rejects([&](auto &bytes) {
    SemanticDirectoryWriter(bytes.data() + header.getReserved())
        .setEventArrayStorageBindingCount(0);
  });
  rejects([&](auto &bytes) {
    EventArrayStorageBindingWriter(bytes.data() + bindingOffset)
        .setStorageIndex(UINT32_MAX);
  });
  rejects([&](auto &bytes) {
    EventArrayStorageBindingWriter(bytes.data() + bindingOffset +
                                   EventArrayStorageBindingLayout.size)
        .setArrayIndexAndTable(first.getArrayIndexAndTable());
  });
  rejects([&](auto &bytes) {
    EventArrayStorageBindingWriter(bytes.data() + bindingOffset +
                                   EventArrayStorageBindingLayout.size)
        .setStorageIndex(first.getStorageIndex());
  });
  rejects([&](auto &bytes) {
    ObjectWriter(bytes.data() + storageOffset)
        .setCaps(storage.getCaps() & ~TALOS_RT_DESIGN_CAP_LEXICAL_ANCHOR);
  });
  rejects([&](auto &bytes) {
    ObjectWriter(bytes.data() + scalarCursor.offset)
        .setCaps(ObjectView(bytes.data() + scalarCursor.offset).getCaps() |
                 TALOS_RT_DESIGN_CAP_LEXICAL_ANCHOR);
  });
  rejects([&](auto &bytes) {
    ObjectWriter(bytes.data() + storageOffset)
        .setOwner(header.getScopeOffset());
  });
  rejects([&](auto &bytes) {
    TypeWriter type(bytes.data() + rootType);
    type.setLeft(7);
    type.setRight(6);
  });
  rejects([&](auto &bytes) {
    uint32_t root = 0;
    ASSERT_TRUE(tryPackTableIndex(TableKind::Scope, 0, root));
    EventArrayStorageBindingWriter(bytes.data() + bindingOffset)
        .setArrayIndexAndTable(root);
  });

  // The immutable ABI permits padding at each physical array level. Exercise
  // actual per-dimension strides, including a 64-bit event crossing word
  // bounds.
  std::vector<uint8_t> padded(image.begin(), image.end());
  TypeWriter(padded.data() + rootType).setWidth(512);
  TypeWriter(padded.data() + rowType).setWidth(192);
  ObjectWriter privateStorage(padded.data() + storageOffset);
  privateStorage.setWidth(512);
  privateStorage.setStateOffset(execution.state_bit_count);
  HeaderWriter(padded.data()).setChecksum(imageChecksum(padded));
  auto paddedExecution = execution;
  paddedExecution.design_database = padded.data();
  paddedExecution.state_bit_count += 512;
  ASSERT_EQ(talos_rt_v1_design_validate(&paddedExecution), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&paddedExecution, &runtime),
            TALOS_RT_OK);
  const std::array<uint64_t, 4> offsets{0, 96, 256, 352};
  std::array<uint64_t, 8> paddedWords{};
  for (size_t index = 0; index != offsets.size(); ++index) {
    const uint64_t value = UINT64_C(0x123456789abc0000) + index;
    const unsigned shift = offsets[index] % 64;
    paddedWords[offsets[index] / 64] |= value << shift;
    if (shift)
      paddedWords[offsets[index] / 64 + 1] |= value >> (64 - shift);
  }
  ASSERT_EQ(talos_rt_v1_design_write(runtime, cells[0].storage,
                                       paddedWords.data(), nullptr, 512),
            TALOS_RT_OK);
  for (size_t index = 0; index != offsets.size(); ++index) {
    VPIEventCellBinding cell{};
    ASSERT_EQ(
        talos_rt_cached_vpi_event_array_cell(runtime, cursors[index], &cell),
        TALOS_RT_OK);
    EXPECT_EQ(cell.bitOffset, offsets[index]);
    uint64_t value = 0, unknown = 0;
    ASSERT_EQ(talos_rt_read_design_slice(
                  runtime, cell.storage, cell.bitOffset, 64, &value, &unknown),
              TALOS_RT_OK);
    EXPECT_EQ(value, UINT64_C(0x123456789abc0000) + index);
    EXPECT_EQ(unknown, 0u);
  }
  talos_rt_v1_context_destroy(runtime);
}
} // namespace
