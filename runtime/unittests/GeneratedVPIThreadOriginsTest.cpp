//===- GeneratedVPIThreadOriginsTest.cpp - Exact thread source identity
//----===//

#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Reflection/DesignReflection.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <cstdlib>
#include <vector>

namespace {
using namespace obelisk::reflection;

template <typename Bytes> uint64_t imageChecksum(const Bytes &bytes) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (size_t index = 0; index != bytes.size(); ++index) {
    hash ^= index >= 32 && index < 40 ? uint8_t{0} : bytes[index];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

TEST(GeneratedDesignDatabase, ThreadOrigins) {
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
  HeaderView header(encoded->designDatabase.data());
  SemanticDirectoryView directory(encoded->designDatabase.data() +
                                  header.getReserved());
  ASSERT_EQ(directory.getThreadOriginBindingCount(), 2u);
  ThreadOriginBindingView first(encoded->designDatabase.data() +
                                directory.getThreadOriginBindingOffset());
  ThreadOriginBindingView second(encoded->designDatabase.data() +
                                 directory.getThreadOriginBindingOffset() +
                                 ThreadOriginBindingLayout.size);
  EXPECT_EQ(first.getCodeUnitID(), 10u);
  EXPECT_EQ(second.getCodeUnitID(), 99u);
  obelisk_rt_design_cursor_v1 initial{}, branch{};
  ASSERT_EQ(obelisk_rt_cached_vpi_thread_origin(runtime, 10, &initial),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_cached_vpi_thread_origin(runtime, 99, &branch),
            OBELISK_RT_OK);
  EXPECT_NE(initial.offset, branch.offset);
  EXPECT_EQ(initial.offset,
            header.getStatementOffset() +
                uint64_t{first.getStatementIndex()} * StatementLayout.size);
  EXPECT_EQ(branch.offset,
            header.getStatementOffset() +
                uint64_t{second.getStatementIndex()} * StatementLayout.size);
  StatementView root(encoded->designDatabase.data() + initial.offset);
  StatementView fork(encoded->designDatabase.data() + branch.offset);
  EXPECT_EQ(root.getID(), 100u);
  EXPECT_EQ(fork.getID(), 200u);
  EXPECT_EQ(root.getOwnerObjectIndex(), fork.getOwnerObjectIndex());
  EXPECT_EQ(fork.getParentIndex(), first.getStatementIndex());
  obelisk_rt_design_cursor_v1 absent = branch;
  EXPECT_EQ(obelisk_rt_cached_vpi_thread_origin(runtime, 20, &absent),
            OBELISK_RT_EOF);
  EXPECT_EQ(absent.offset, 0u);
  EXPECT_EQ(obelisk_rt_cached_vpi_thread_origin(runtime, 0, &absent),
            OBELISK_RT_EOF);
  EXPECT_EQ(obelisk_rt_cached_vpi_thread_origin(runtime, 10, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_cached_vpi_thread_origin(nullptr, 10, &absent),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(absent.offset, 0u);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(runtime, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(runtime), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(runtime), OBELISK_RT_OK);
  auto entryFunction = std::find_if(
      encoded->functions.begin(), encoded->functions.end(),
      [](const auto &function) { return function.symbol == "initial"; });
  ASSERT_NE(entryFunction, encoded->functions.end());
  obelisk_rt_design_bytecode_entry_v1 entry{&execution, entryFunction->index,
                                            0};
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
  auto initialHandle =
      vpi_handle_by_name(const_cast<char *>("top.initial"), nullptr);
  auto event = vpi_handle_by_name(const_cast<char *>("top.ready"), nullptr);
  ASSERT_NE(initialHandle, nullptr);
  ASSERT_NE(event, nullptr);
  auto body = vpi_handle(vpiStmt, initialHandle);
  ASSERT_NE(body, nullptr);
  auto statements = vpi_iterate(vpiStmt, body);
  ASSERT_NE(statements, nullptr);
  auto firstStatement = vpi_scan(statements);
  auto branchStatement = vpi_scan(statements);
  ASSERT_NE(firstStatement, nullptr);
  ASSERT_NE(branchStatement, nullptr);
  EXPECT_EQ(vpi_scan(statements), nullptr);
  auto waiters = vpi_iterate(vpiWaitingProcesses, event);
  ASSERT_NE(waiters, nullptr);
  std::vector<vpiHandle> threads;
  vpiHandle origin = nullptr;
  size_t missing = 0;
  while (auto thread = vpi_scan(waiters)) {
    threads.push_back(thread);
    auto source = vpi_handle(vpiOrigin, thread);
    if (source) {
      ASSERT_EQ(origin, nullptr);
      origin = source;
      EXPECT_EQ(vpi_compare_objects(origin, branchStatement), 1);
      auto lexicalScope = vpi_handle(vpiScope, origin);
      ASSERT_NE(lexicalScope, nullptr);
      EXPECT_EQ(vpi_compare_objects(lexicalScope, body), 1);
      EXPECT_EQ(vpi_release_handle(lexicalScope), 1);
    } else {
      ++missing;
      EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
    }
  }
  EXPECT_EQ(threads.size(), 2u);
  EXPECT_EQ(missing, 1u);
  ASSERT_NE(origin, nullptr);
  obelisk_rt_v1_scheduler_event(runtime, 7, 0);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(runtime), OBELISK_RT_OK);
  for (auto thread : threads) {
    EXPECT_EQ(vpi_get(vpiType, thread), vpiUndefined);
    EXPECT_NE(vpi_chk_error(nullptr), 0);
  }
  // The thread expires; its original source statement remains immutable.
  EXPECT_EQ(vpi_get(vpiType, origin), vpiNullStmt);
  EXPECT_EQ(vpi_compare_objects(origin, branchStatement), 1);
  for (auto handle :
       {origin, firstStatement, branchStatement, body, event, initialHandle})
    EXPECT_EQ(vpi_release_handle(handle), 1);
  obelisk_rt_v1_context_destroy(runtime);

  auto reject = [&](auto mutate) {
    auto image = encoded->designDatabase;
    mutate(image);
    HeaderWriter(image.data()).setChecksum(imageChecksum(image));
    auto invalid = execution;
    invalid.design_database = image.data();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&invalid),
              OBELISK_RT_INVALID_DESIGN);
  };
  const uint64_t offset = directory.getThreadOriginBindingOffset();
  reject([&](auto &image) {
    ThreadOriginBindingWriter(image.data() + offset).setCodeUnitID(0);
  });
  reject([&](auto &image) {
    ThreadOriginBindingWriter(image.data() + offset).setCodeUnitID(UINT64_MAX);
  });
  reject([&](auto &image) {
    ThreadOriginBindingWriter(image.data() + offset).setCodeUnitID(9);
  });
  reject([&](auto &image) {
    ThreadOriginBindingWriter(image.data() + offset).setReserved(1);
  });
  reject([&](auto &image) {
    ThreadOriginBindingWriter(image.data() + offset)
        .setStatementIndex(static_cast<uint32_t>(header.getStatementCount()));
  });
  reject([&](auto &image) {
    ThreadOriginBindingWriter(image.data() + offset +
                              ThreadOriginBindingLayout.size)
        .setCodeUnitID(10);
  });
  // Public initial20 cannot claim an origin owned by initial10, even in the
  // same scope.
  reject([&](auto &image) {
    ThreadOriginBindingWriter(image.data() + offset).setCodeUnitID(20);
  });
  uint32_t foreign = UINT32_MAX, scopeOwned = UINT32_MAX;
  for (uint32_t i = 0; i < header.getStatementCount(); ++i) {
    StatementView statement(encoded->designDatabase.data() +
                            header.getStatementOffset() +
                            uint64_t{i} * StatementLayout.size);
    if (statement.getID() == 300)
      foreign = i;
    if (statement.getID() == 400)
      scopeOwned = i;
  }
  ASSERT_NE(foreign, UINT32_MAX);
  ASSERT_NE(scopeOwned, UINT32_MAX);
  reject([&](auto &image) {
    ThreadOriginBindingWriter(image.data() + offset +
                              ThreadOriginBindingLayout.size)
        .setStatementIndex(foreign);
  });
  reject([&](auto &image) {
    ThreadOriginBindingWriter(image.data() + offset)
        .setStatementIndex(scopeOwned);
  });
  uint64_t functionOffset = 0;
  for (uint64_t i = 0; i < header.getObjectCount(); ++i) {
    const uint64_t record = header.getObjectOffset() + i * ObjectLayout.size;
    if (ObjectView(encoded->designDatabase.data() + record).getID() == 60)
      functionOffset = record;
  }
  ASSERT_NE(functionOffset, 0u);
  // A process and function sharing an ID cannot ambiguously resolve a binding.
  reject([&](auto &image) {
    ObjectWriter(image.data() + functionOffset).setID(10);
  });
}
} // namespace
