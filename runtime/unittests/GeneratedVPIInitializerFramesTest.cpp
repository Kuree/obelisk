//===- GeneratedVPIInitializerFramesTest.cpp - Initializer call frames
//-----===//
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
#include <cstdlib>
#include <memory>
#include <vector>
namespace {
TEST(GeneratedDesignDatabase, InitializerFrames) {
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
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.flags = encoded->executionFlags;
  execution.bytecode = encoded->bytecode.data();
  execution.bytecode_size = encoded->bytecode.size();
  execution.design_database = encoded->designDatabase.data();
  execution.design_database_size = encoded->designDatabase.size();
  execution.state_bit_count = encoded->stateBitCount;
  for (unsigned byte = 0; byte != 8; ++byte)
    execution.checksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);
  obelisk_rt_context *raw = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &raw),
            OBELISK_RT_OK);
  std::unique_ptr<obelisk_rt_context, decltype(&obelisk_rt_v1_context_destroy)>
      runtime(raw, obelisk_rt_v1_context_destroy);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(raw, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(raw), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(raw), OBELISK_RT_OK);
  uint64_t ticket = 0, activation = 0;
  EXPECT_EQ(obelisk_rt_v1_vpi_call_prepare(raw, 30, &ticket),
            OBELISK_RT_INVALID_FRAME);
  EXPECT_EQ(obelisk_rt_v1_vpi_frame_enter(
                raw, 41, OBELISK_RT_VPI_FRAME_CODE_UNIT_SCOPE, 41, &activation),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(vpi_handle_by_name(const_cast<char *>("top.x.$static_initializer"),
                               nullptr),
            nullptr);
  for (bool withParent : {false, true}) {
    uint64_t parentID = 0;
    if (withParent)
      ASSERT_EQ(
          obelisk_rt_v1_vpi_frame_enter(
              raw, 44, OBELISK_RT_VPI_FRAME_CODE_UNIT_SCOPE, 44, &parentID),
          OBELISK_RT_OK);
    auto saved = vpi_handle(vpiFrame, nullptr);
    for (uint64_t call : {20u, 21u}) {
      ASSERT_EQ(obelisk_rt_v1_vpi_call_prepare(raw, call, &ticket),
                OBELISK_RT_OK);
      EXPECT_EQ(obelisk_rt_v1_vpi_call_finish(raw, ticket),
                OBELISK_RT_INVALID_LIFECYCLE);
      EXPECT_EQ(obelisk_rt_v1_vpi_frame_enter_prepared(raw, 61, 0, &activation),
                OBELISK_RT_INVALID_ARGUMENT);
      ASSERT_EQ(obelisk_rt_v1_vpi_frame_enter_prepared(raw, 60, 0, &activation),
                OBELISK_RT_OK);
      auto frame = vpi_handle(vpiFrame, nullptr);
      ASSERT_NE(frame, nullptr);
      auto parent = vpi_handle(vpiParent, frame);
      if (withParent) {
        EXPECT_EQ(vpi_compare_objects(parent, saved), 1);
        EXPECT_EQ(vpi_release_handle(parent), 1);
      } else {
        EXPECT_EQ(parent, nullptr);
      }
      auto origin = vpi_handle(vpiOrigin, frame);
      ASSERT_NE(origin, nullptr);
      EXPECT_EQ(vpi_get(vpiType, origin), vpiFuncCall);
      auto scope = vpi_handle(vpiScope, origin);
      ASSERT_NE(scope, nullptr);
      EXPECT_EQ(vpi_get(vpiType, scope),
                call == 20 ? vpiModule : vpiClassTypespec);
      if (call == 20) {
        EXPECT_STREQ(vpi_get_str(vpiFullName, scope), "top");
      } else {
        auto definition = vpi_handle(vpiClassDefn, scope);
        auto expected =
            vpi_handle_by_name(const_cast<char *>("top.Node"), nullptr);
        ASSERT_NE(definition, nullptr);
        ASSERT_NE(expected, nullptr);
        EXPECT_EQ(vpi_compare_objects(definition, expected), 1);
        EXPECT_EQ(vpi_release_handle(definition), 1);
        EXPECT_EQ(vpi_release_handle(expected), 1);
      }
      ASSERT_EQ(obelisk_rt_v1_vpi_frame_leave(raw, activation), OBELISK_RT_OK);
      ASSERT_EQ(obelisk_rt_v1_vpi_call_finish(raw, ticket), OBELISK_RT_OK);
      EXPECT_EQ(vpi_get(vpiType, frame), vpiUndefined);
      EXPECT_NE(vpi_chk_error(nullptr), 0);
      EXPECT_EQ(vpi_get(vpiType, origin), vpiFuncCall);
      EXPECT_EQ(vpi_release_handle(origin), 1);
      EXPECT_EQ(vpi_release_handle(scope), 1);
    }
    if (withParent) {
      auto current = vpi_handle(vpiFrame, nullptr);
      EXPECT_EQ(vpi_compare_objects(current, saved), 1);
      EXPECT_EQ(vpi_release_handle(current), 1);
      EXPECT_EQ(vpi_release_handle(saved), 1);
      ASSERT_EQ(obelisk_rt_v1_vpi_frame_leave(raw, parentID), OBELISK_RT_OK);
    } else {
      EXPECT_EQ(vpi_handle(vpiFrame, nullptr), nullptr);
    }
  }
  runtime.reset();
  using namespace obelisk::reflection;
  const auto &image = encoded->designDatabase;
  HeaderView header(image.data());
  SemanticDirectoryView directory(image.data() + header.getReserved());
  ASSERT_EQ(directory.getInitializerBindingCount(), 2u);
  const uint64_t first = directory.getInitializerBindingOffset();
  const uint64_t second = first + InitializerBindingLayout.size;
  uint32_t storage = UINT32_MAX, function = UINT32_MAX;
  for (uint32_t index = 0; index != header.getObjectCount(); ++index) {
    ObjectView object(image.data() + header.getObjectOffset() +
                      uint64_t{index} * ObjectLayout.size);
    if (unpackRecordKind(object.getKindAndVPIKind()) == RecordKind::Storage &&
        object.getID() == 1)
      ASSERT_TRUE(tryPackTableIndex(TableKind::Object, index, storage));
    if (unpackRecordKind(object.getKindAndVPIKind()) == RecordKind::Function &&
        object.getID() == 60)
      ASSERT_TRUE(tryPackTableIndex(TableKind::Object, index, function));
  }
  ASSERT_NE(storage, UINT32_MAX);
  ASSERT_NE(function, UINT32_MAX);
  auto rejects = [&](auto corrupt) {
    std::vector<uint8_t> bytes(image.begin(), image.end());
    corrupt(bytes);
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t index = 0; index != bytes.size(); ++index) {
      hash ^=
          index >= field::HeaderChecksum && index < field::HeaderChecksum + 8
              ? 0
              : bytes[index];
      hash *= UINT64_C(1099511628211);
    }
    HeaderWriter(bytes.data()).setChecksum(hash);
    auto invalid = execution;
    invalid.design_database = bytes.data();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&invalid),
              OBELISK_RT_INVALID_DESIGN);
  };
  rejects([&](auto &bytes) {
    InitializerBindingWriter(bytes.data() + first).setReserved(1);
  });
  rejects([&](auto &bytes) {
    InitializerBindingWriter(bytes.data() + first).setCodeUnitID(0);
  });
  rejects([&](auto &bytes) {
    InitializerBindingWriter(bytes.data() + first).setCodeUnitID(60);
  });
  rejects([&](auto &bytes) {
    InitializerBindingWriter(bytes.data() + second).setCodeUnitID(41);
  });
  rejects([&](auto &bytes) {
    InitializerBindingWriter(bytes.data() + first)
        .setSourceIndexAndTable(function);
  });
  rejects([&](auto &bytes) {
    InitializerBindingWriter(bytes.data() + second)
        .setSourceIndexAndTable(storage);
  });
  rejects([&](auto &bytes) {
    InitializerBindingWriter(bytes.data() + second)
        .setSourceIndexAndTable(InitializerBindingView(image.data() + first)
                                    .getSourceIndexAndTable());
  });
}
} // namespace
