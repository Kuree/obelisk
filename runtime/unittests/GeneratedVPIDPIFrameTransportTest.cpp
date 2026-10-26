//===- GeneratedVPIDPIFrameTransportTest.cpp - Exact DPI frame transport
//----===//
#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <cstdlib>
#include <memory>

namespace {
struct TransportProbe {
  uint64_t call, callee, wrongCallee;
  const char *sourceName;
  bool ran = false;
  vpiHandle source = nullptr;
};
void inspectTransport(obelisk_rt_context *context, TransportProbe &probe) {
  probe.ran = true;
  ASSERT_NE(obelisk_rt_v1_process_current(context), 0u);
  uint64_t root = 0, ticket = 0, activation = 0;
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_enter(
                context, 44, OBELISK_RT_VPI_FRAME_CODE_UNIT_SCOPE, 44, &root),
            OBELISK_RT_OK);
  bool physicalTask = true;
  ASSERT_EQ(obelisk_rt_cached_vpi_code_unit_transport(context, probe.callee,
                                                      &physicalTask),
            OBELISK_RT_OK);
  EXPECT_FALSE(physicalTask);
  ASSERT_EQ(obelisk_rt_v1_vpi_call_prepare(context, probe.call, &ticket),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_vpi_frame_enter_prepared(context, probe.callee, 1,
                                                   &activation),
            OBELISK_RT_INVALID_LIFECYCLE);
  EXPECT_EQ(activation, 0u);
  EXPECT_NE(obelisk_rt_v1_vpi_frame_enter_prepared(context, probe.wrongCallee,
                                                   0, &activation),
            OBELISK_RT_OK);
  EXPECT_EQ(activation, 0u);
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_enter_prepared(context, probe.callee, 0,
                                                   &activation),
            OBELISK_RT_OK);
  vpiHandle frame = vpi_handle(vpiFrame, nullptr);
  ASSERT_NE(frame, nullptr);
  auto origin = vpi_handle(vpiOrigin, frame);
  ASSERT_NE(origin, nullptr);
  EXPECT_EQ(vpi_get(vpiType, origin), vpiTaskCall);
  probe.source =
      vpi_handle_by_name(const_cast<char *>(probe.sourceName), nullptr);
  ASSERT_NE(probe.source, nullptr);
  EXPECT_EQ(vpi_get(vpiType, probe.source), vpiTask);
  obelisk_rt_design_cursor_v1 source{};
  uint64_t lexical = 0;
  ASSERT_EQ(obelisk_rt_cached_vpi_code_unit_scope(context, probe.callee,
                                                  &source, &lexical),
            OBELISK_RT_OK);
  EXPECT_EQ(lexical, probe.callee);
  uint32_t sourceKind = 0;
  ASSERT_EQ(obelisk_rt_cached_vpi_type(context, source, &sourceKind),
            OBELISK_RT_OK);
  EXPECT_EQ(sourceKind, vpiTask);
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_leave(context, activation), OBELISK_RT_OK);
  EXPECT_EQ(vpi_get(vpiType, frame), vpiUndefined);
  EXPECT_NE(vpi_chk_error(nullptr), 0);
  ASSERT_EQ(obelisk_rt_v1_vpi_call_finish(context, ticket), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_leave(context, root), OBELISK_RT_OK);
  EXPECT_EQ(vpi_get(vpiType, origin), vpiTaskCall);
  EXPECT_EQ(vpi_release_handle(origin), 1);
}
obelisk_rt_status transportEntry(obelisk_rt_process_instance_v1 *instance) {
  instance->native_handle = instance;
  auto *probe = *static_cast<TransportProbe **>(instance->frame);
  inspectTransport(instance->context, *probe);
  *instance->action = {
      OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
  return ::testing::Test::HasFatalFailure() ? OBELISK_RT_FATAL : OBELISK_RT_OK;
}
void testTransport(uint64_t call, uint64_t callee, uint64_t wrongCallee,
                   const char *sourceName) {
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
  obelisk_rt_context *rawContext = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &rawContext),
            OBELISK_RT_OK);
  std::unique_ptr<obelisk_rt_context, decltype(&obelisk_rt_v1_context_destroy)>
      runtime(rawContext, obelisk_rt_v1_context_destroy);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(runtime.get(), nullptr, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(runtime.get()), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(runtime.get()), OBELISK_RT_OK);
  TransportProbe probe{call, callee, wrongCallee, sourceName};
  const obelisk_rt_frame_field_v1 field{
      OBELISK_RT_FRAME_CAPTURE,  0, 0, sizeof(TransportProbe *),
      alignof(TransportProbe *), 0};
  const uint32_t continuation = 0;
  obelisk_rt_frame_layout_v1 layout{OBELISK_RT_VERSION,
                                    0,
                                    sizeof(TransportProbe *),
                                    alignof(TransportProbe *),
                                    &field,
                                    1,
                                    1,
                                    &continuation,
                                    0};
  uint64_t hash = UINT64_C(14695981039346656037);
  auto append = [&](const auto &value) {
    const auto *bytes = reinterpret_cast<const uint8_t *>(&value);
    for (size_t i = 0; i != sizeof(value); ++i) {
      hash ^= bytes[i];
      hash *= UINT64_C(1099511628211);
    }
  };
  append(layout.version);
  append(layout.flags);
  append(layout.frame_size);
  append(layout.frame_alignment);
  append(layout.field_count);
  append(layout.continuation_count);
  append(field);
  append(continuation);
  layout.checksum = hash;
  const obelisk_rt_process_descriptor_v1 descriptor{
      {OBELISK_RT_DESCRIPTOR_PROCESS, 0, 44},
      OBELISK_RT_VERSION,
      0,
      OBELISK_RT_TIER_MASK_NATIVE,
      0,
      &layout,
      [](uint64_t *size, uint64_t *alignment) {
        *size = 0;
        *alignment = 1;
        return OBELISK_RT_OK;
      },
      transportEntry,
      [](obelisk_rt_process_instance_v1 *p) { p->native_handle = nullptr; },
      nullptr,
      &execution,
      nullptr};
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(obelisk_rt_v1_process_instance_create(&descriptor, &instance),
            OBELISK_RT_OK);
  *static_cast<TransportProbe **>(instance->frame) = &probe;
  ASSERT_EQ(obelisk_rt_v1_scheduler_add(runtime.get(), instance, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(runtime.get()), OBELISK_RT_OK);
  EXPECT_TRUE(probe.ran);
  ASSERT_NE(probe.source, nullptr);
  EXPECT_EQ(vpi_get(vpiType, probe.source), vpiTask);
  EXPECT_EQ(vpi_release_handle(probe.source), 1);
}
TEST(GeneratedDesignDatabase, DirectDPITaskFrameTransport) {
  testTransport(20, 46, 45, "top.worker");
}
TEST(GeneratedDesignDatabase, InterfaceDPITaskFrameTransport) {
  testTransport(19, 101, 100, "second.dpi.dpi_t");
}
} // namespace
