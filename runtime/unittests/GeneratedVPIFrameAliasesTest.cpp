//===- GeneratedVPIFrameAliasesTest.cpp - Live reference aliases
//-----===//
#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Reflection/DesignReflection.h"
#include "obelisk/Runtime/StableHandle.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
namespace {
TEST(GeneratedDesignDatabase, FrameReferenceAliases) {
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
  uint64_t root = 0;
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_enter(
                raw, 44, OBELISK_RT_VPI_FRAME_CODE_UNIT_SCOPE, 44, &root),
            OBELISK_RT_OK);
  auto read = [&](vpiHandle handle) {
    s_vpi_value value{};
    value.format = vpiIntVal;
    vpi_get_value(handle, &value);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    return value.value.integer;
  };
  auto unavailable = [&](vpiHandle handle) {
    EXPECT_EQ(vpi_get(vpiHasActual, handle), 0);
    s_vpi_value value{};
    value.format = vpiIntVal;
    vpi_get_value(handle, &value);
    EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  };
  auto *stateValue = reinterpret_cast<uint8_t *>(raw->stateValue.data());
  auto *stateUnknown = reinterpret_cast<uint8_t *>(raw->stateUnknown.data());
  ASSERT_EQ(execution.state_bit_count, 64u);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(raw, 1, 3, 40),
            OBELISK_RT_OK);
  const obelisk_rt_trace_layout_v1 layout{
      OBELISK_RT_VERSION, 0, 24, 8, nullptr, 0};
  obelisk_rt_class_descriptor_v1 descriptor{};
  descriptor.version = OBELISK_RT_VERSION;
  descriptor.class_id = 100;
  descriptor.instance_size = 24;
  descriptor.instance_alignment = 8;
  descriptor.layout = &layout;
  const obelisk_rt_element_type_v1 element{OBELISK_RT_VERSION,
                                           OBELISK_RT_ELEMENT_BITS,
                                           123,
                                           0,
                                           0,
                                           4,
                                           4,
                                           32,
                                           nullptr,
                                           nullptr};
  for (unsigned kind = 0; kind != 5; ++kind) {
    SCOPED_TRACE(kind);
    uint64_t activation = 0;
    ASSERT_EQ(obelisk_rt_v1_vpi_frame_enter(raw, 45, OBELISK_RT_VPI_FRAME_CALL,
                                            20, &activation),
              OBELISK_RT_OK);
    auto frame = vpi_handle(vpiFrame, nullptr);
    ASSERT_NE(frame, nullptr);
    auto value = vpi_handle_by_name(const_cast<char *>("value"), frame);
    auto flags = vpi_handle_by_name(const_cast<char *>("flags"), frame);
    ASSERT_NE(value, nullptr);
    ASSERT_NE(flags, nullptr);
    unavailable(value);
    uint64_t payload = 0;
    obelisk_rt_object_v1 *owner = nullptr;
    uint32_t managed = 0;
    uint32_t number = 17;
    uint8_t changed = 0;
    ManagedExecutionScope lane(raw);
    ASSERT_EQ(lane.getStatus(), OBELISK_RT_OK);
    obelisk_rt_gc_root_v1 ownerRoot{}, queueRoot{};
    obelisk_rt_object_v1 *queue = nullptr;
    if (kind < 2) {
      payload = kind ? obelisk_rt_stable_handle_encode(
                           OBELISK_RT_STABLE_HANDLE_STATIC, 1, 2)
                     : 0;
      ASSERT_EQ(obelisk_rt_v1_native_state_store_plane(
                    raw, stateValue, 64, payload, 32, 0,
                    reinterpret_cast<uint8_t *>(&number), &changed),
                OBELISK_RT_OK);
      uint32_t known = 0;
      ASSERT_EQ(obelisk_rt_v1_native_state_store_plane(
                    raw, stateUnknown, 64, payload, 32, 1,
                    reinterpret_cast<uint8_t *>(&known), &changed),
                OBELISK_RT_OK);
    } else if (kind == 2) {
      ASSERT_EQ(
          obelisk_rt_v1_native_state_alloc(
              raw, 32, reinterpret_cast<uint8_t *>(&number), nullptr, &payload),
          OBELISK_RT_OK);
    } else if (kind == 3) {
      managed = 1;
      payload = 8;
      ASSERT_EQ(
          obelisk_rt_v1_object_allocate(lane.getLane(), &descriptor, &owner),
          OBELISK_RT_OK);
      ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane.getLane(), &ownerRoot, &owner),
                OBELISK_RT_OK);
      ASSERT_EQ(
          obelisk_rt_v1_object_write(owner, payload, &number, sizeof(number)),
          OBELISK_RT_OK);
    } else {
      managed = 2;
      ASSERT_EQ(obelisk_rt_v1_queue_create(lane.getLane(), &element, UINT64_MAX,
                                           &queue),
                OBELISK_RT_OK);
      ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane.getLane(), &queueRoot, &queue),
                OBELISK_RT_OK);
      ASSERT_EQ(
          obelisk_rt_v1_queue_push(lane.getLane(), queue, 0, &number, nullptr),
          OBELISK_RT_OK);
      ASSERT_EQ(obelisk_rt_v1_reference_path_index_create(
                    lane.getLane(), queue, 0, nullptr, 0, 0, nullptr, nullptr,
                    0, &owner),
                OBELISK_RT_OK);
      ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane.getLane(), &ownerRoot, &owner),
                OBELISK_RT_OK);
    }
    EXPECT_EQ(obelisk_rt_v1_vpi_frame_bind_alias(raw, 1, owner, payload, 3),
              OBELISK_RT_INVALID_ARGUMENT);
    EXPECT_EQ(
        obelisk_rt_v1_vpi_frame_bind_alias(raw, 999, owner, payload, managed),
        OBELISK_RT_INVALID_ARGUMENT);
    ASSERT_EQ(
        obelisk_rt_v1_vpi_frame_bind_alias(raw, 1, owner, payload, managed),
        OBELISK_RT_OK);
    EXPECT_EQ(
        obelisk_rt_v1_vpi_frame_bind_alias(raw, 1, owner, payload, managed),
        OBELISK_RT_OK);
    EXPECT_EQ(vpi_get(vpiHasActual, value), 1);
    EXPECT_EQ(read(value), 17);
    number = 23;
    ASSERT_EQ(obelisk_rt_v1_argument_ref_store(
                  raw, stateValue, stateUnknown, 64, owner, payload, managed,
                  32, 4, 0, OBELISK_RT_ARGUMENT_VALUE_BITS, &number, nullptr),
              OBELISK_RT_OK);
    EXPECT_EQ(read(value), 23);
    // Packed selection reads share the exact live alias, not a copied value.
    auto bit = vpi_handle_by_index(value, 0);
    ASSERT_NE(bit, nullptr);
    EXPECT_EQ(read(bit), 1);
    EXPECT_EQ(vpi_release_handle(bit), 1);
    if (kind < 2) {
      auto bad = kind ? obelisk_rt_stable_handle_encode(
                            OBELISK_RT_STABLE_HANDLE_STATIC, 1, 39)
                      : 63;
      EXPECT_EQ(obelisk_rt_v1_vpi_frame_bind_alias(raw, 2, nullptr, bad, 0),
                OBELISK_RT_INVALID_ARGUMENT);
      uint8_t v = 0xff, u = 0xff;
      EXPECT_EQ(obelisk_rt_vpi_state_read(raw, bad, 0, 8, &v, &u),
                OBELISK_RT_INVALID_ARGUMENT);
      EXPECT_EQ(v, 0u);
      EXPECT_EQ(u, 0u);
      EXPECT_EQ(raw->schedulerStatus, OBELISK_RT_OK);
    } else if (kind == 2) {
      ASSERT_EQ(obelisk_rt_v1_native_state_release(raw, payload, 0),
                OBELISK_RT_OK);
      unavailable(value);
    } else if (kind == 3) {
      uint16_t v = 0x101, u = 0x80;
      ASSERT_EQ(obelisk_rt_v1_object_write_planes(owner, 16, &v, &u, 2),
                OBELISK_RT_OK);
      ASSERT_EQ(obelisk_rt_v1_vpi_frame_bind_alias(raw, 2, owner, 16, 1),
                OBELISK_RT_OK);
      s_vpi_value logic{};
      logic.format = vpiVectorVal;
      vpi_get_value(flags, &logic);
      ASSERT_EQ(vpi_chk_error(nullptr), 0);
      ASSERT_NE(logic.value.vector, nullptr);
      EXPECT_EQ(logic.value.vector[0].aval, 0x181u);
      EXPECT_EQ(logic.value.vector[0].bval, 0x80u);
    } else {
      uint32_t popped = 0, present = 0;
      ASSERT_EQ(obelisk_rt_v1_queue_pop(queue, 1, &popped, nullptr, &present),
                OBELISK_RT_OK);
      EXPECT_EQ(present, 1u);
      EXPECT_EQ(popped, 23u);
      number = 61;
      ASSERT_EQ(
          obelisk_rt_v1_queue_push(lane.getLane(), queue, 0, &number, nullptr),
          OBELISK_RT_OK);
      // A ref to a removed queue element retains that detached element.
      EXPECT_EQ(read(value), 23);
      number = 57;
      ASSERT_EQ(obelisk_rt_v1_reference_path_store(lane.getLane(), owner,
                                                   &number, nullptr),
                OBELISK_RT_OK);
      EXPECT_EQ(read(value), 57);
    }
    if (managed) {
      ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane.getLane(), &ownerRoot),
                OBELISK_RT_OK);
      if (queue)
        ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane.getLane(), &queueRoot),
                  OBELISK_RT_OK);
      ASSERT_EQ(obelisk_rt_v1_gc_collect(lane.getLane()), OBELISK_RT_OK);
      unavailable(value);
      if (kind == 3)
        unavailable(flags);
    }
    auto type = vpi_handle(vpiTypespec, value);
    ASSERT_NE(type, nullptr);
    ASSERT_EQ(obelisk_rt_v1_vpi_frame_leave(raw, activation), OBELISK_RT_OK);
    EXPECT_EQ(vpi_get(vpiType, value), vpiUndefined);
    EXPECT_NE(vpi_chk_error(nullptr), 0);
    EXPECT_NE(vpi_get(vpiType, type), vpiUndefined);
    EXPECT_EQ(vpi_release_handle(type), 1);
  }
  // A managed word in an actual automatic variable remains weak as well.
  uint64_t activation = 0;
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_enter(raw, 45, OBELISK_RT_VPI_FRAME_CALL,
                                          20, &activation),
            OBELISK_RT_OK);
  auto frame = vpi_handle(vpiFrame, nullptr);
  ASSERT_NE(frame, nullptr);
  auto lock = vpi_handle_by_name(const_cast<char *>("lock"), frame);
  ASSERT_NE(lock, nullptr);
  {
    ManagedExecutionScope lane(raw);
    ASSERT_EQ(lane.getStatus(), OBELISK_RT_OK);
    obelisk_rt_object_v1 *semaphore = nullptr;
    ASSERT_EQ(obelisk_rt_v1_semaphore_create(lane.getLane(), 0, &semaphore),
              OBELISK_RT_OK);
    uint64_t reference = 0;
    ASSERT_EQ(obelisk_rt_native_state_alloc_managed(raw, semaphore, &reference),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_frame_bind_alias(raw, 3, nullptr, reference, 0),
              OBELISK_RT_OK);
    auto object = vpi_handle(vpiClassObj, lock);
    ASSERT_NE(object, nullptr);
    ASSERT_EQ(obelisk_rt_v1_native_state_release(raw, reference, 0),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_gc_collect(lane.getLane()), OBELISK_RT_OK);
    EXPECT_EQ(vpi_get(vpiHasActual, lock), 0);
    EXPECT_EQ(vpi_get(vpiType, object), vpiUndefined);
    EXPECT_NE(vpi_chk_error(nullptr), 0);
    // The tagged aggregate has a ten-bit physical span and a sixteen-bit
    // descriptor copy span. Binding and availability must accept that exact
    // descriptor instead of requiring equal physical and copy widths.
    auto declaration = mlir::cast<obelisk::sim::SimVPILocalDeclOp>(
        mlir::SymbolTable::lookupSymbolIn(design, "tagged"));
    auto element =
        obelisk::sim::getElementDescriptor(declaration.getValueType());
    ASSERT_TRUE(element);
    EXPECT_EQ(element->valueSize, 2u);
    EXPECT_EQ(element->bitWidth, 16u);
    obelisk_rt_object_v1 *queue = nullptr, *path = nullptr;
    ASSERT_EQ(obelisk_rt_v1_container_create_typed(
                  lane.getLane(), OBELISK_RT_CONTAINER_QUEUE, element->typeID,
                  static_cast<uint32_t>(element->kind),
                  static_cast<uint32_t>(element->flags), element->valueSize,
                  element->alignment, element->bitWidth, nullptr, 0, 0,
                  UINT64_MAX, &queue),
              OBELISK_RT_OK);
    obelisk_rt_gc_root_v1 queueRoot{}, pathRoot{};
    ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane.getLane(), &queueRoot, &queue),
              OBELISK_RT_OK);
    uint8_t bytes[2] = {5, 1};
    ASSERT_EQ(
        obelisk_rt_v1_queue_push(lane.getLane(), queue, 0, bytes, nullptr),
        OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_reference_path_index_create(
                  lane.getLane(), queue, 0, nullptr, 0, 0, nullptr, nullptr, 0,
                  &path),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane.getLane(), &pathRoot, &path),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_vpi_frame_bind_alias(raw, 4, path, 0, 2),
              OBELISK_RT_OK);
    auto tagged = vpi_handle_by_name(const_cast<char *>("tagged"), frame);
    ASSERT_NE(tagged, nullptr);
    EXPECT_EQ(vpi_get(vpiHasActual, tagged), 1);
    bytes[0] = 9;
    ASSERT_EQ(obelisk_rt_v1_reference_path_store(lane.getLane(), path, bytes,
                                                 nullptr),
              OBELISK_RT_OK);
    EXPECT_EQ(vpi_get(vpiHasActual, tagged), 1);
    ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane.getLane(), &pathRoot),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane.getLane(), &queueRoot),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_gc_collect(lane.getLane()), OBELISK_RT_OK);
    EXPECT_EQ(vpi_get(vpiHasActual, tagged), 0);
  }
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_leave(raw, activation), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_leave(raw, root), OBELISK_RT_OK);
  runtime.reset();
}
} // namespace
