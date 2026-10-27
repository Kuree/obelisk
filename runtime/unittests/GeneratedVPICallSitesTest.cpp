//===- GeneratedVPICallSitesTest.cpp - Exact source call identity ---------===//

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
#include <cstdlib>
#include <string>

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

TEST(GeneratedDesignDatabase, CallSites) {
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
  HeaderView header(encoded->designDatabase.data());
  SemanticDirectoryView directory(encoded->designDatabase.data() +
                                  header.getReserved());
  ASSERT_EQ(directory.getCallSiteBindingCount(), 4u);
  const uint64_t first = directory.getCallSiteBindingOffset();
  obelisk_rt_design_cursor_v1 calls[4]{};
  for (uint64_t index = 0; index != 4; ++index) {
    uint64_t owner = 0;
    bool statement = false;
    ASSERT_EQ(obelisk_rt_cached_vpi_call(runtime, 20 + index, &calls[index],
                                         &owner, &statement),
              OBELISK_RT_OK);
    EXPECT_EQ(owner, index == 0 ? 44u : 45u);
    EXPECT_EQ(statement, index == 0);
    CallSiteBindingView row(encoded->designDatabase.data() + first +
                            index * CallSiteBindingLayout.size);
    EXPECT_EQ(row.getID(), 20u + index);
    EXPECT_EQ(row.getFlags(), index == 3 ? 1u : 0u);
  }
  uint64_t owner = 0;
  bool statement = false;
  obelisk_rt_design_cursor_v1 selected{};
  ASSERT_EQ(obelisk_rt_cached_vpi_statement(runtime, 11, &selected, &owner),
            OBELISK_RT_OK);
  EXPECT_EQ(selected.offset, calls[0].offset);
  EXPECT_EQ(owner, 44u);
  EXPECT_EQ(obelisk_rt_cached_vpi_call_target(runtime, 20, 45), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_cached_vpi_call_target(runtime, 20, 46), OBELISK_RT_EOF);
  EXPECT_EQ(obelisk_rt_cached_vpi_call_target(runtime, 21, 46), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_cached_vpi_call_target(runtime, 23, 46), OBELISK_RT_EOF);
  EXPECT_EQ(obelisk_rt_cached_vpi_call_target(runtime, 22, 46), OBELISK_RT_EOF);
  EXPECT_EQ(obelisk_rt_cached_vpi_call_target(runtime, 999, 46),
            OBELISK_RT_EOF);
  EXPECT_EQ(
      obelisk_rt_cached_vpi_frame_origin(runtime, 1, 21, &selected, &statement),
      OBELISK_RT_OK);
  EXPECT_EQ(selected.offset, calls[1].offset);
  EXPECT_FALSE(statement);
  EXPECT_EQ(
      obelisk_rt_cached_vpi_frame_origin(runtime, 1, 22, &selected, &statement),
      OBELISK_RT_EOF);
  EXPECT_EQ(selected.offset, 0u);
  EXPECT_EQ(
      obelisk_rt_cached_vpi_frame_origin(runtime, 2, 13, &selected, &statement),
      OBELISK_RT_EOF);
  ASSERT_EQ(
      obelisk_rt_cached_vpi_frame_origin(runtime, 2, 12, &selected, &statement),
      OBELISK_RT_OK);
  EXPECT_TRUE(statement);
  auto namedBlock = selected;
  ASSERT_EQ(
      obelisk_rt_cached_vpi_code_unit_scope(runtime, 47, &selected, &owner),
      OBELISK_RT_OK);
  EXPECT_EQ(selected.offset, namedBlock.offset);
  EXPECT_EQ(owner, 45u);
  ASSERT_EQ(
      obelisk_rt_cached_vpi_frame_origin(runtime, 0, 47, &selected, &statement),
      OBELISK_RT_OK);
  EXPECT_TRUE(statement);
  EXPECT_EQ(selected.offset, namedBlock.offset);
  VPIRelationRange range{};
  ASSERT_EQ(obelisk_rt_cached_vpi_relation_range(runtime, calls[1], vpiScope,
                                                 false, &range),
            OBELISK_RT_OK);
  ASSERT_EQ(range.count, 1u);
  uint32_t kind = 0;
  ASSERT_EQ(obelisk_rt_cached_vpi_relation_target(runtime, range.first,
                                                  &selected, &kind, &statement),
            OBELISK_RT_OK);
  EXPECT_EQ(selected.offset, namedBlock.offset);
  EXPECT_TRUE(statement);
  VPIFixedPropertyValue name{};
  ASSERT_EQ(
      obelisk_rt_cached_vpi_fixed_property(runtime, calls[0], vpiName, &name),
      OBELISK_RT_OK);
  EXPECT_EQ(std::string(reinterpret_cast<const char *>(name.stringData),
                        name.stringSize),
            "worker.with.dot");
  ASSERT_EQ(
      obelisk_rt_cached_vpi_fixed_property(runtime, calls[2], vpiName, &name),
      OBELISK_RT_OK);
  EXPECT_EQ(std::string(reinterpret_cast<const char *>(name.stringData),
                        name.stringSize),
            "$system.with.dot");
  EXPECT_EQ(
      obelisk_rt_cached_vpi_fixed_property(runtime, calls[1], vpiName, &name),
      OBELISK_RT_EOF);
  EXPECT_EQ(
      obelisk_rt_cached_vpi_call(runtime, 999, &selected, &owner, &statement),
      OBELISK_RT_EOF);
  EXPECT_EQ(selected.offset, 0u);
  EXPECT_EQ(owner, 0u);
  EXPECT_EQ(
      obelisk_rt_cached_vpi_call(runtime, 20, nullptr, &owner, &statement),
      OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(
      obelisk_rt_cached_vpi_call(nullptr, 20, &selected, &owner, &statement),
      OBELISK_RT_INVALID_HANDLE);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(runtime, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(runtime), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(runtime), OBELISK_RT_OK);
  auto initial = vpi_handle_by_name(const_cast<char *>("top.initial"), nullptr);
  ASSERT_NE(initial, nullptr);
  auto taskCall = vpi_handle(vpiStmt, initial);
  ASSERT_NE(taskCall, nullptr);
  EXPECT_EQ(vpi_get(vpiType, taskCall), vpiTaskCall);
  EXPECT_STREQ(vpi_get_str(vpiName, taskCall), "worker.with.dot");
  auto target = vpi_handle(vpiTask, taskCall);
  ASSERT_NE(target, nullptr);
  auto declared = vpi_handle_by_name(const_cast<char *>("top.worker"), nullptr);
  ASSERT_NE(declared, nullptr);
  EXPECT_EQ(vpi_compare_objects(target, declared), 1);
  EXPECT_EQ(vpi_release_handle(declared), 1);
  EXPECT_EQ(vpi_release_handle(target), 1);
  EXPECT_EQ(vpi_release_handle(taskCall), 1);
  EXPECT_EQ(vpi_release_handle(initial), 1);
  obelisk_rt_v1_context_destroy(runtime);

  auto invalid = [&](auto mutate) {
    auto bytes = encoded->designDatabase;
    mutate(bytes);
    HeaderWriter(bytes.data()).setChecksum(imageChecksum(bytes));
    auto corrupt = execution;
    corrupt.design_database = bytes.data();
    corrupt.design_database_size = bytes.size();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&corrupt),
              OBELISK_RT_INVALID_DESIGN);
  };
  invalid([&](auto &bytes) {
    CallSiteBindingWriter(bytes.data() + first).setID(0);
  });
  invalid([&](auto &bytes) {
    CallSiteBindingWriter(bytes.data() + first).setID(UINT64_C(1) << 63);
  });
  invalid([&](auto &bytes) {
    CallSiteBindingWriter(bytes.data() + first)
        .setOwnerCodeUnitIndex(UINT32_MAX);
  });
  invalid([&](auto &bytes) {
    CallSiteBindingWriter(bytes.data() + first)
        .setObjectIndexAndTable(UINT32_MAX);
  });
  invalid([&](auto &bytes) {
    CallSiteBindingWriter(bytes.data() + first).setFlags(2);
  });
  invalid([&](auto &bytes) {
    CallSiteBindingWriter(bytes.data() + first).setReserved(1);
  });
  invalid([&](auto &bytes) {
    CallSiteBindingView firstRow(bytes.data() + first);
    CallSiteBindingWriter(bytes.data() + first + CallSiteBindingLayout.size)
        .setObjectIndexAndTable(firstRow.getObjectIndexAndTable());
  });
  invalid([&](auto &bytes) {
    CallSiteBindingView nested(bytes.data() + first +
                               CallSiteBindingLayout.size);
    CallSiteBindingWriter(bytes.data() + first)
        .setOwnerCodeUnitIndex(nested.getOwnerCodeUnitIndex());
  });
  // Physical scope alone cannot justify a different lexical call owner.
  invalid([&](auto &bytes) {
    CallSiteBindingView initialOwner(bytes.data() + first);
    CallSiteBindingWriter(bytes.data() + first + CallSiteBindingLayout.size)
        .setOwnerCodeUnitIndex(initialOwner.getOwnerCodeUnitIndex());
  });
  invalid([&](auto &bytes) {
    SemanticDirectoryWriter(bytes.data() + header.getReserved())
        .setCallSiteBindingCount(3);
  });
  invalid([&](auto &bytes) {
    CallSiteBindingWriter(bytes.data() + first + 2 * CallSiteBindingLayout.size)
        .setFlags(1);
  });
}
} // namespace
