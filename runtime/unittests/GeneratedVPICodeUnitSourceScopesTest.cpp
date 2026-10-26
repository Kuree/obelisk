//===- GeneratedVPICodeUnitSourceScopesTest.cpp - Exact source scopes
//---------===//

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

TEST(GeneratedDesignDatabase, CodeUnitSourceScopes) {
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
                                               checksum};
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);
  obelisk_rt_context *runtime = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &runtime),
            OBELISK_RT_OK);
  HeaderView header(encoded->designDatabase.data());
  SemanticDirectoryView directory(encoded->designDatabase.data() +
                                  header.getReserved());
  ASSERT_EQ(directory.getCodeUnitScopeBindingCount(), 2u);
  const uint64_t first = directory.getCodeUnitScopeBindingOffset();
  CodeUnitScopeBindingView functionBinding(encoded->designDatabase.data() +
                                           first);
  CodeUnitScopeBindingView taskBinding(encoded->designDatabase.data() + first +
                                       CodeUnitScopeBindingLayout.size);
  EXPECT_EQ(functionBinding.getCodeUnitID(), 45u);
  EXPECT_EQ(functionBinding.getFlags(), 0u);
  EXPECT_EQ(taskBinding.getCodeUnitID(), 46u);
  EXPECT_EQ(taskBinding.getFlags(), 1u);
  obelisk_rt_design_cursor_v1 scopes[2]{};
  for (unsigned index = 0; index != 2; ++index) {
    uint64_t owner = 0;
    ASSERT_EQ(obelisk_rt_cached_vpi_code_unit_scope(runtime, 45 + index,
                                                    &scopes[index], &owner),
              OBELISK_RT_OK);
    EXPECT_EQ(owner, 45u + index);
    uint32_t kind = 0;
    ASSERT_EQ(obelisk_rt_cached_vpi_type(runtime, scopes[index], &kind),
              OBELISK_RT_OK);
    EXPECT_EQ(kind, index ? vpiTask : vpiFunction);
    obelisk_rt_design_cursor_v1 origin{};
    bool statement = true;
    ASSERT_EQ(obelisk_rt_cached_vpi_frame_origin(runtime, 0, 45 + index,
                                                 &origin, &statement),
              OBELISK_RT_OK);
    EXPECT_FALSE(statement);
    EXPECT_EQ(origin.offset, scopes[index].offset);
  }
  EXPECT_EQ(obelisk_rt_cached_vpi_call_target(runtime, 20, 46), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_cached_vpi_call_target(runtime, 21, 45), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_cached_vpi_call_target(runtime, 20, 45), OBELISK_RT_EOF);
  EXPECT_EQ(obelisk_rt_cached_vpi_call_target(runtime, 21, 47), OBELISK_RT_EOF);
  EXPECT_EQ(obelisk_rt_cached_vpi_call_target(runtime, 21, 48), OBELISK_RT_EOF);
  obelisk_rt_design_cursor_v1 ordinary{};
  uint64_t owner = 0;
  ASSERT_EQ(
      obelisk_rt_cached_vpi_code_unit_scope(runtime, 47, &ordinary, &owner),
      OBELISK_RT_OK);
  EXPECT_EQ(owner, 47u);
  EXPECT_NE(ordinary.offset, scopes[0].offset);
  EXPECT_EQ(
      obelisk_rt_cached_vpi_code_unit_scope(runtime, 48, &ordinary, &owner),
      OBELISK_RT_EOF);
  EXPECT_EQ(ordinary.offset, 0u);
  EXPECT_EQ(owner, 0u);
  EXPECT_EQ(obelisk_rt_cached_vpi_code_unit_scope(runtime, 45, nullptr, &owner),
            OBELISK_RT_INVALID_ARGUMENT);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(runtime, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(runtime), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(runtime), OBELISK_RT_OK);
  auto top = vpi_handle_by_name(const_cast<char *>("top"), nullptr);
  auto task = vpi_handle_by_name(const_cast<char *>("top.worker"), nullptr);
  auto function =
      vpi_handle_by_name(const_cast<char *>("top.f.with.dot"), nullptr);
  auto initial = vpi_handle_by_name(const_cast<char *>("top.initial"), nullptr);
  ASSERT_NE(top, nullptr);
  ASSERT_NE(task, nullptr);
  ASSERT_NE(function, nullptr);
  ASSERT_NE(initial, nullptr);
  EXPECT_STREQ(vpi_get_str(vpiName, function), "f.with.dot");
  auto call = vpi_handle(vpiStmt, initial);
  ASSERT_NE(call, nullptr);
  auto target = vpi_handle(vpiTask, call);
  ASSERT_NE(target, nullptr);
  EXPECT_EQ(vpi_compare_objects(target, task), 1);
  auto scope = vpi_handle(vpiScope, function);
  ASSERT_NE(scope, nullptr);
  EXPECT_EQ(vpi_compare_objects(scope, top), 1);
  auto declarations = vpi_iterate(vpiInternalScope, top);
  ASSERT_NE(declarations, nullptr);
  unsigned taskCount = 0, functionCount = 0;
  while (auto declaration = vpi_scan(declarations)) {
    taskCount += vpi_compare_objects(declaration, task) == 1;
    functionCount += vpi_compare_objects(declaration, function) == 1;
    EXPECT_EQ(vpi_release_handle(declaration), 1);
  }
  EXPECT_EQ(taskCount, 1u);
  EXPECT_EQ(functionCount, 1u);
  for (vpiHandle handle : {top, task, function, initial, call, target, scope}) {
    EXPECT_EQ(vpi_release_handle(handle), 1);
  }
  obelisk_rt_v1_context_destroy(runtime);

  auto unitOffset = [&](uint64_t id) {
    for (uint64_t index = 0; index != header.getObjectCount(); ++index) {
      uint64_t offset = header.getObjectOffset() + index * ObjectLayout.size;
      ObjectView object(encoded->designDatabase.data() + offset);
      auto kind = unpackRecordKind(object.getKindAndVPIKind());
      if ((kind == RecordKind::Process || kind == RecordKind::Function) &&
          object.getID() == id)
        return offset;
    }
    return uint64_t{0};
  };
  uint64_t hiddenFunction = unitOffset(45);
  uint64_t unmappedFunction = unitOffset(48);
  ASSERT_NE(hiddenFunction, 0u);
  ASSERT_NE(unmappedFunction, 0u);
  auto invalid = [&](auto mutate) {
    auto bytes = encoded->designDatabase;
    mutate(bytes);
    HeaderWriter(bytes.data()).setChecksum(imageChecksum(bytes));
    auto corrupt = execution;
    corrupt.design_database = bytes.data();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&corrupt),
              OBELISK_RT_INVALID_DESIGN);
  };
  for (uint64_t id :
       {uint64_t{0}, UINT64_C(1) << 63, uint64_t{44}, uint64_t{999}}) {
    SCOPED_TRACE(id);
    invalid([&](auto &bytes) {
      CodeUnitScopeBindingWriter(bytes.data() + first).setCodeUnitID(id);
    });
  }
  invalid([&](auto &bytes) {
    CodeUnitScopeBindingWriter(bytes.data() + first +
                               CodeUnitScopeBindingLayout.size)
        .setCodeUnitID(45);
  });
  invalid([&](auto &bytes) {
    CodeUnitScopeBindingWriter(bytes.data() + first)
        .setSourceIndexAndTable(taskBinding.getSourceIndexAndTable());
  });
  invalid([&](auto &bytes) {
    CodeUnitScopeBindingWriter(bytes.data() + first)
        .setSourceIndexAndTable(UINT32_MAX);
  });
  invalid([&](auto &bytes) {
    CodeUnitScopeBindingWriter(bytes.data() + first).setFlags(2);
  });
  invalid([&](auto &bytes) {
    CodeUnitScopeBindingWriter(bytes.data() + first +
                               CodeUnitScopeBindingLayout.size)
        .setFlags(0);
  });
  invalid([&](auto &bytes) {
    ObjectWriter(bytes.data() + hiddenFunction)
        .setOwner(header.getScopeOffset() + 2 * ScopeLayout.size);
  });
  invalid([&](auto &bytes) {
    ObjectWriter(bytes.data() + hiddenFunction)
        .setName(ObjectView(bytes.data() + unmappedFunction).getName());
  });
}
} // namespace
