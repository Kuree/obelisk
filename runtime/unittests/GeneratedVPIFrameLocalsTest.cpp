//===- GeneratedVPIFrameLocalsTest.cpp - Live automatic local queries
//------===//

#include "../lib/DesignBytecodeImage.h"
#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/DPI/DPIDialect.h"
#include "obelisk/Dialect/Runtime/RuntimeDialect.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Reflection/DesignReflection.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" void obelisk_rt_v1_dpi_import_bytecode_link_anchor(void);

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
struct LocalProbe {
  uint64_t rootID = 0, frameID = 0;
  uint64_t valueRef = 0, arrayRef = 0, lockRef = 0;
  vpiHandle frame = nullptr, value = nullptr, array = nullptr,
            selected = nullptr;
  vpiHandle bit = nullptr, lock = nullptr, pending = nullptr;
  vpiHandle typespec = nullptr, scope = nullptr;
  vpiHandle expression = nullptr, statement = nullptr;
  std::vector<unsigned> phases;
};
int readInt(vpiHandle handle) {
  s_vpi_value value{};
  value.format = vpiIntVal;
  vpi_get_value(handle, &value);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  return value.value.integer;
}
void same(vpiHandle object, PLI_INT32 selector, vpiHandle expected) {
  auto handle = vpi_handle(selector, object);
  ASSERT_NE(handle, nullptr);
  EXPECT_EQ(vpi_compare_objects(handle, expected), 1);
  EXPECT_EQ(vpi_release_handle(handle), 1);
}
void inspectLocalABI(obelisk_rt_context *context, LocalProbe &probe,
                     unsigned phase) {
  probe.phases.push_back(phase);
  if (phase == 1) {
    EXPECT_EQ(readInt(probe.value), 23);
    ASSERT_EQ(obelisk_rt_v1_native_state_release(context, probe.valueRef, 0),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_native_state_release(context, probe.arrayRef, 0),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_frame_leave(context, probe.frameID),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_frame_leave(context, probe.rootID),
              OBELISK_RT_OK);
    return;
  }
  ASSERT_EQ(phase, 0u);
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_enter(context, 44,
                                          OBELISK_RT_VPI_FRAME_CODE_UNIT_SCOPE,
                                          44, &probe.rootID),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_enter(
                context, 45, OBELISK_RT_VPI_FRAME_CALL, 20, &probe.frameID),
            OBELISK_RT_OK);
  probe.frame = vpi_handle(vpiFrame, nullptr);
  ASSERT_NE(probe.frame, nullptr);
  EXPECT_EQ(vpi_get(vpiValid, probe.frame), 1);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  auto thread = vpi_handle(vpiThread, probe.frame);
  ASSERT_NE(thread, nullptr);
  EXPECT_EQ(vpi_get(vpiActive, thread), 1);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  EXPECT_EQ(vpi_release_handle(thread), 1);
  auto iterator = vpi_iterate(vpiAutomatics, probe.frame);
  ASSERT_NE(iterator, nullptr);
  same(iterator, vpiUse, probe.frame);
  probe.value = vpi_scan(iterator);
  probe.array = vpi_scan(iterator);
  probe.lock = vpi_scan(iterator);
  ASSERT_NE(probe.value, nullptr);
  ASSERT_NE(probe.array, nullptr);
  ASSERT_NE(probe.lock, nullptr);
  EXPECT_EQ(vpi_scan(iterator), nullptr);
  EXPECT_STREQ(vpi_get_str(vpiName, probe.value), "value");
  EXPECT_STREQ(vpi_get_str(vpiName, probe.array), "flags");
  EXPECT_STREQ(vpi_get_str(vpiName, probe.lock), "lock");
  EXPECT_EQ(vpi_get(vpiAutomatic, probe.value), 1);
  EXPECT_EQ(vpi_get(vpiHasActual, probe.value), 0);
  EXPECT_EQ(vpi_get(vpiAllocScheme, probe.value), vpiAutomaticScheme);
  auto byName = vpi_handle_by_name(const_cast<char *>("value"), probe.frame);
  ASSERT_NE(byName, nullptr);
  EXPECT_EQ(vpi_compare_objects(probe.value, byName), 1);
  EXPECT_EQ(vpi_release_handle(byName), 1);
  EXPECT_EQ(vpi_handle_by_name(const_cast<char *>("top.worker.value"), nullptr),
            nullptr);
  s_vpi_value unavailable{};
  unavailable.format = vpiIntVal;
  vpi_get_value(probe.value, &unavailable);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  probe.typespec = vpi_handle(vpiTypespec, probe.value);
  probe.scope = vpi_handle(vpiScope, probe.value);
  ASSERT_NE(probe.typespec, nullptr);
  ASSERT_NE(probe.scope, nullptr);
  uint32_t number = 17;
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc(
                context, 32, reinterpret_cast<uint8_t *>(&number), nullptr,
                &probe.valueRef),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_vpi_frame_bind(context, 4, probe.valueRef),
            OBELISK_RT_INVALID_FRAME);
  EXPECT_EQ(obelisk_rt_v1_vpi_frame_bind(context, 999, probe.valueRef),
            OBELISK_RT_INVALID_ARGUMENT);
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_bind(context, 1, probe.valueRef),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_vpi_frame_bind(context, 1, probe.valueRef),
            OBELISK_RT_OK);
  EXPECT_EQ(vpi_get(vpiHasActual, probe.value), 1);
  EXPECT_EQ(readInt(probe.value), 17);
  s_vpi_value attempted{};
  attempted.format = vpiIntVal;
  attempted.value.integer = 99;
  EXPECT_EQ(vpi_put_value(probe.value, &attempted, nullptr, vpiNoDelay),
            nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  EXPECT_EQ(readInt(probe.value), 17);
  uint64_t replacementRef = 0;
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc(
                context, 32, reinterpret_cast<uint8_t *>(&number), nullptr,
                &replacementRef),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_vpi_frame_bind(context, 1, replacementRef),
            OBELISK_RT_INVALID_FRAME);
  ASSERT_EQ(obelisk_rt_v1_native_state_release(context, replacementRef, 0),
            OBELISK_RT_OK);
  number = 23;
  uint8_t changed = 0;
  ASSERT_EQ(obelisk_rt_v1_native_state_store_plane(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                context->execution->state_bit_count, probe.valueRef, 32, 0,
                reinterpret_cast<uint8_t *>(&number), &changed),
            OBELISK_RT_OK);
  EXPECT_EQ(readInt(probe.value), 23);
  const uint8_t values[] = {0x5a, 0xa5}, unknowns[] = {0x0c, 0x30};
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc(context, 16, values, unknowns,
                                             &probe.arrayRef),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_vpi_frame_bind(context, 1, probe.arrayRef),
            OBELISK_RT_INVALID_ARGUMENT);
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_bind(context, 2, probe.arrayRef),
            OBELISK_RT_OK);
  probe.selected = vpi_handle_by_index(probe.array, 1);
  ASSERT_NE(probe.selected, nullptr);
  same(probe.selected, vpiParent, probe.array);
  byName = vpi_handle_by_name(const_cast<char *>("flags[1]"), probe.frame);
  ASSERT_NE(byName, nullptr);
  EXPECT_EQ(vpi_compare_objects(byName, probe.selected), 1);
  EXPECT_EQ(vpi_release_handle(byName), 1);
  s_vpi_value vector{};
  vector.format = vpiVectorVal;
  vpi_get_value(probe.selected, &vector);
  ASSERT_EQ(vpi_chk_error(nullptr), 0);
  ASSERT_NE(vector.value.vector, nullptr);
  EXPECT_EQ(vector.value.vector[0].aval, 0x95u);
  EXPECT_EQ(vector.value.vector[0].bval, 0x30u);
  probe.bit = vpi_handle_by_index(probe.selected, 7);
  ASSERT_NE(probe.bit, nullptr);
  EXPECT_EQ(vpi_get(vpiType, probe.bit), vpiRegBit);
  EXPECT_EQ(readInt(probe.bit), 1);
  same(probe.bit, vpiParent, probe.selected);
  uint64_t blockID = 0;
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_enter(context, 45,
                                          OBELISK_RT_VPI_FRAME_STATEMENT_SCOPE,
                                          12, &blockID),
            OBELISK_RT_OK);
  auto block = vpi_handle(vpiFrame, nullptr);
  auto shadow = vpi_handle_by_name(const_cast<char *>("value"), block);
  ASSERT_NE(shadow, nullptr);
  EXPECT_EQ(vpi_compare_objects(shadow, probe.value), 0);
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_checkpoint(context, 13), OBELISK_RT_OK);
  probe.statement = vpi_handle(vpiStmt, block);
  ASSERT_NE(probe.statement, nullptr);
  probe.expression = vpi_handle(vpiRhs, probe.statement);
  ASSERT_NE(probe.expression, nullptr);
  EXPECT_EQ(vpi_get(vpiType, probe.expression), vpiOperation);
  EXPECT_EQ(vpi_get(vpiOpType, probe.expression), vpiAddOp);
  vpi_get_value(probe.expression, &unavailable);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  number = 5;
  uint64_t shadowRef = 0;
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc(
                context, 32, reinterpret_cast<uint8_t *>(&number), nullptr,
                &shadowRef),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_bind(context, 4, shadowRef), OBELISK_RT_OK);
  EXPECT_EQ(readInt(shadow), 5);
  EXPECT_EQ(readInt(probe.expression), 28);
  number = 9;
  ASSERT_EQ(obelisk_rt_v1_native_state_store_plane(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                context->execution->state_bit_count, shadowRef, 32, 0,
                reinterpret_cast<uint8_t *>(&number), &changed),
            OBELISK_RT_OK);
  EXPECT_EQ(readInt(probe.expression), 32);
  number = 5;
  ASSERT_EQ(obelisk_rt_v1_native_state_store_plane(
                context,
                reinterpret_cast<uint8_t *>(context->stateValue.data()),
                context->execution->state_bit_count, shadowRef, 32, 0,
                reinterpret_cast<uint8_t *>(&number), &changed),
            OBELISK_RT_OK);
  auto operands = vpi_iterate(vpiOperand, probe.expression);
  ASSERT_NE(operands, nullptr);
  same(operands, vpiUse, probe.expression);
  auto first = vpi_scan(operands);
  auto second = vpi_scan(operands);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(vpi_scan(operands), nullptr);
  EXPECT_EQ(vpi_compare_objects(first, probe.value), 1);
  EXPECT_EQ(vpi_compare_objects(second, shadow), 1);
  EXPECT_EQ(readInt(first), 23);
  EXPECT_EQ(readInt(second), 5);
  EXPECT_EQ(vpi_release_handle(first), 1);
  EXPECT_EQ(vpi_release_handle(second), 1);

  // Re-enter the same code unit: an unbound callee variable must not read the
  // caller's live declaration, even though its immutable cursor is identical.
  uint64_t recursiveID = 0, recursiveBlockID = 0;
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_enter(
                context, 45, OBELISK_RT_VPI_FRAME_CALL, 22, &recursiveID),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_enter(context, 45,
                                          OBELISK_RT_VPI_FRAME_STATEMENT_SCOPE,
                                          12, &recursiveBlockID),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_bind(context, 4, shadowRef), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_checkpoint(context, 13), OBELISK_RT_OK);
  auto recursive = vpi_handle(vpiFrame, nullptr);
  auto recursiveStatement = vpi_handle(vpiStmt, recursive);
  auto recursiveExpression = vpi_handle(vpiRhs, recursiveStatement);
  ASSERT_NE(recursiveExpression, nullptr);
  EXPECT_EQ(vpi_compare_objects(recursiveExpression, probe.expression), 1);
  vpi_get_value(recursiveExpression, &unavailable);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  EXPECT_EQ(readInt(probe.expression), 28);
  EXPECT_EQ(vpi_release_handle(recursiveExpression), 1);
  EXPECT_EQ(vpi_release_handle(recursiveStatement), 1);
  EXPECT_EQ(vpi_release_handle(recursive), 1);
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_leave(context, recursiveBlockID),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_leave(context, recursiveID), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_release(context, shadowRef, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_leave(context, blockID), OBELISK_RT_OK);
  // Source objects outlive execution; only reading their lost actual fails.
  EXPECT_EQ(vpi_get(vpiType, probe.statement), vpiAssignment);
  EXPECT_EQ(vpi_get(vpiType, probe.expression), vpiOperation);
  vpi_get_value(probe.expression, &unavailable);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  EXPECT_EQ(vpi_get(vpiType, shadow), vpiUndefined);
  EXPECT_NE(vpi_chk_error(nullptr), 0);
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_enter(context, 45,
                                          OBELISK_RT_VPI_FRAME_STATEMENT_SCOPE,
                                          12, &blockID),
            OBELISK_RT_OK);
  block = vpi_handle(vpiFrame, nullptr);
  auto fresh = vpi_handle_by_name(const_cast<char *>("value"), block);
  ASSERT_NE(fresh, nullptr);
  unavailable.format = vpiIntVal;
  vpi_get_value(fresh, &unavailable);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_leave(context, blockID), OBELISK_RT_OK);
  // A frame binding retains only a stable reference. It never adds a root.
  ManagedExecutionScope managed(context);
  ASSERT_EQ(managed.getStatus(), OBELISK_RT_OK);
  obelisk_rt_object_v1 *semaphore = nullptr;
  ASSERT_EQ(obelisk_rt_v1_semaphore_create(managed.getLane(), 0, &semaphore),
            OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_native_state_alloc_managed(context, semaphore, &probe.lockRef),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_bind(context, 3, probe.lockRef),
            OBELISK_RT_OK);
  auto object = vpi_handle(vpiClassObj, probe.lock);
  ASSERT_NE(object, nullptr);
  ASSERT_EQ(obelisk_rt_v1_native_state_release(context, probe.lockRef, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(managed.getLane()), OBELISK_RT_OK);
  EXPECT_EQ(vpi_get(vpiType, object), vpiUndefined);
  EXPECT_NE(vpi_chk_error(nullptr), 0);
  EXPECT_EQ(vpi_get(vpiType, probe.lock), vpiClassVar);
  EXPECT_EQ(vpi_handle(vpiClassObj, probe.lock), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  probe.pending = vpi_iterate(vpiAutomatics, probe.frame);
  ASSERT_NE(probe.pending, nullptr);
}
obelisk_rt_status inspectLocals(obelisk_rt_context *context, uint32_t id,
                                const obelisk_rt_import_input_v1 *inputs,
                                uint32_t count, obelisk_rt_import_output_v1 *,
                                uint32_t outputs, void *data) {
  if (id != 123 || count != 1 || !inputs || !inputs[0].value || outputs)
    return OBELISK_RT_INVALID_ARGUMENT;
  inspectLocalABI(context, *static_cast<LocalProbe *>(data),
                  inputs[0].value[0]);
  return ::testing::Test::HasFatalFailure() ? OBELISK_RT_FATAL : OBELISK_RT_OK;
}

TEST(GeneratedDesignDatabase, FrameAutomaticVariables) {
  // Drive the public activation ABI from a real executing process. The authored
  // metadata distinguishes an atomic call statement from a nested expression.
  obelisk_rt_v1_dpi_import_bytecode_link_anchor();
  const char *input = std::getenv("OBELISK_TEST_INPUT");
  ASSERT_NE(input, nullptr);
  mlir::DialectRegistry registry;
  registry
      .insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
              obelisk::dpi::DPIDialect, obelisk::runtime::ObeliskRuntimeDialect,
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
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.flags = encoded->executionFlags;
  execution.bytecode = encoded->bytecode.data();
  execution.bytecode_size = encoded->bytecode.size();
  execution.design_database = encoded->designDatabase.data();
  execution.design_database_size = encoded->designDatabase.size();
  execution.state_bit_count = encoded->stateBitCount;
  execution.checksum = checksum;
  const obelisk_rt_dpi_scope_v1 scopes[] = {
      {0, UINT64_MAX, "$root", 5, -9, -12, 0}, {1, 0, "top", 3, -9, -12, 0}};
  execution.dpi_scopes = scopes;
  execution.dpi_scope_count = 2;
  execution.dpi_time_precision = -12;
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);
  obelisk_rt_context *runtime = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &runtime),
            OBELISK_RT_OK);
  HeaderView header(encoded->designDatabase.data());
  SemanticDirectoryView directory(encoded->designDatabase.data() +
                                  header.getReserved());
  ASSERT_EQ(directory.getFrameLocalBindingCount(), 4u);
  const uint64_t first = directory.getFrameLocalBindingOffset();
  VPIFrameLocalBinding locals[4]{};
  for (unsigned index = 0; index != 4; ++index) {
    ASSERT_EQ(
        obelisk_rt_cached_vpi_frame_local(runtime, index + 1, &locals[index]),
        OBELISK_RT_OK);
    EXPECT_EQ(locals[index].id, index + 1);
    EXPECT_EQ(locals[index].lexicalCodeUnitID, 45u);
    EXPECT_EQ(locals[index].ordinal, index == 3 ? 0u : index);
    obelisk_rt_design_info_v1 info{};
    ASSERT_EQ(
        obelisk_rt_v1_design_info(&execution, locals[index].declaration, &info),
        OBELISK_RT_OK);
    EXPECT_EQ(info.capabilities, OBELISK_RT_DESIGN_CAP_FRAME_LOCAL);
    EXPECT_EQ(info.bit_width, index == 1 ? 16u : index == 2 ? 64u : 32u);
  }
  EXPECT_NE(locals[0].scope.offset, locals[3].scope.offset);
  std::vector<VPIFrameLocalBinding> inventory;
  ASSERT_EQ(obelisk_rt_cached_vpi_frame_locals(runtime, 45, locals[0].scope,
                                               inventory),
            OBELISK_RT_OK);
  ASSERT_EQ(inventory.size(), 3u);
  for (unsigned index = 0; index != inventory.size(); ++index)
    EXPECT_EQ(inventory[index].id, index + 1);
  ASSERT_EQ(obelisk_rt_cached_vpi_frame_locals(runtime, 45, locals[3].scope,
                                               inventory),
            OBELISK_RT_OK);
  ASSERT_EQ(inventory.size(), 1u);
  EXPECT_EQ(inventory[0].id, 4u);
  ASSERT_EQ(obelisk_rt_cached_vpi_frame_locals(runtime, 46, locals[0].scope,
                                               inventory),
            OBELISK_RT_OK);
  EXPECT_TRUE(inventory.empty());
  VPIFrameLocalBinding missing = locals[0];
  EXPECT_EQ(obelisk_rt_cached_vpi_frame_local(runtime, 999, &missing),
            OBELISK_RT_EOF);
  EXPECT_EQ(missing.id, 0u);
  EXPECT_EQ(missing.declaration.offset, 0u);
  EXPECT_EQ(obelisk_rt_cached_vpi_frame_local(runtime, 1, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_cached_vpi_frame_local(nullptr, 1, &missing),
            OBELISK_RT_INVALID_HANDLE);
  LocalProbe probe;
  ASSERT_EQ(obelisk_rt_v1_context_register_import(runtime, 123, inspectLocals,
                                                  &probe),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(runtime, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(runtime), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(runtime), OBELISK_RT_OK);
  auto startup = std::find_if(
      encoded->functions.begin(), encoded->functions.end(),
      [](const auto &function) { return function.symbol == "startup"; });
  ASSERT_NE(startup, encoded->functions.end());
  obelisk_rt_design_bytecode_entry_v1 entry{&execution, startup->index, 0};
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
  ASSERT_EQ(probe.phases, (std::vector<unsigned>{0}));
  EXPECT_EQ(readInt(probe.value), 23);
  EXPECT_EQ(vpi_get(vpiType, probe.selected), vpiReg);
  EXPECT_EQ(vpi_get(vpiActive, probe.frame), 0);
  obelisk_rt_v1_scheduler_event(runtime, 7, 0);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(runtime), OBELISK_RT_OK);
  EXPECT_EQ(probe.phases, (std::vector<unsigned>{0, 1}));
  for (auto handle : {probe.frame, probe.value, probe.array, probe.selected,
                      probe.bit, probe.lock, probe.pending}) {
    EXPECT_EQ(vpi_get(vpiType, handle), vpiUndefined);
    EXPECT_NE(vpi_chk_error(nullptr), 0);
  }
  EXPECT_EQ(vpi_get(vpiType, probe.typespec), vpiIntTypespec);
  EXPECT_EQ(vpi_get(vpiType, probe.scope), vpiTask);
  EXPECT_EQ(vpi_release_handle(probe.typespec), 1);
  EXPECT_EQ(vpi_release_handle(probe.scope), 1);
  EXPECT_EQ(vpi_get(vpiType, probe.statement), vpiAssignment);
  EXPECT_EQ(vpi_get(vpiType, probe.expression), vpiOperation);
  EXPECT_EQ(vpi_release_handle(probe.statement), 1);
  EXPECT_EQ(vpi_release_handle(probe.expression), 1);
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
  FrameLocalBindingView firstRow(encoded->designDatabase.data() + first);
  const uint64_t object =
      header.getObjectOffset() +
      uint64_t{firstRow.getObjectIndex()} * ObjectLayout.size;
  invalid([&](auto &bytes) {
    FrameLocalBindingWriter(bytes.data() + first).setID(0);
  });
  invalid([&](auto &bytes) {
    FrameLocalBindingWriter(bytes.data() + first).setID(UINT64_C(1) << 63);
  });
  invalid([&](auto &bytes) {
    FrameLocalBindingWriter(bytes.data() + first + FrameLocalBindingLayout.size)
        .setID(1);
  });
  invalid([&](auto &bytes) {
    FrameLocalBindingWriter(bytes.data() + first + FrameLocalBindingLayout.size)
        .setObjectIndex(firstRow.getObjectIndex());
  });
  invalid([&](auto &bytes) {
    FrameLocalBindingWriter(bytes.data() + first)
        .setOwnerCodeUnitIndex(UINT32_MAX);
  });
  invalid([&](auto &bytes) {
    FrameLocalBindingWriter(bytes.data() + first)
        .setScopeIndexAndTable(UINT32_MAX);
  });
  invalid([&](auto &bytes) {
    FrameLocalBindingWriter(bytes.data() + first).setOrdinal(4);
  });
  invalid([&](auto &bytes) {
    SemanticDirectoryWriter(bytes.data() + header.getReserved())
        .setFrameLocalBindingCount(3);
  });
  invalid([&](auto &bytes) { ObjectWriter(bytes.data() + object).setCaps(0); });
  invalid([&](auto &bytes) {
    ObjectWriter(bytes.data() + object)
        .setCaps(OBELISK_RT_DESIGN_CAP_FRAME_LOCAL |
                 OBELISK_RT_DESIGN_CAP_WRITE);
  });
  invalid([&](auto &bytes) { ObjectWriter(bytes.data() + object).setType(0); });
  // A different valid scope in the same code unit does not own this
  // declaration.
  invalid([&](auto &bytes) {
    FrameLocalBindingView nested(bytes.data() + first +
                                 3 * FrameLocalBindingLayout.size);
    FrameLocalBindingWriter(bytes.data() + first)
        .setScopeIndexAndTable(nested.getScopeIndexAndTable());
  });
  uint64_t automaticProperty = UINT64_MAX;
  uint32_t packed = 0;
  ASSERT_TRUE(
      tryPackTableIndex(TableKind::Object, firstRow.getObjectIndex(), packed));
  for (uint64_t index = 0; index != directory.getFixedPropertyCount();
       ++index) {
    uint64_t offset =
        directory.getFixedPropertyOffset() + index * FixedPropertyLayout.size;
    FixedPropertyView property(encoded->designDatabase.data() + offset);
    if (property.getSourceIndexAndTable() == packed &&
        property.getSelector() == vpiAutomatic)
      automaticProperty = offset;
  }
  ASSERT_NE(automaticProperty, UINT64_MAX);
  invalid([&](auto &bytes) {
    FixedPropertyWriter(bytes.data() + automaticProperty).setPayload(0);
  });
  invalid([&](auto &bytes) {
    const uint64_t end =
        directory.getFixedPropertyOffset() +
        directory.getFixedPropertyCount() * FixedPropertyLayout.size;
    std::memmove(bytes.data() + automaticProperty,
                 bytes.data() + automaticProperty + FixedPropertyLayout.size,
                 end - automaticProperty - FixedPropertyLayout.size);
    SemanticDirectoryWriter(bytes.data() + header.getReserved())
        .setFixedPropertyCount(directory.getFixedPropertyCount() - 1);
  });
}
} // namespace
