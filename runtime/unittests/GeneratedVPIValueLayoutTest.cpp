//===- GeneratedVPIValueLayoutTest.cpp - Exact element descriptor proof ---===//
#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "talos/Conversion/SimulationToBytecode.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
#include "gtest/gtest.h"
#include <cstdlib>
#include <vector>

namespace {
TEST(GeneratedDesignDatabase, ExactVPIElementDescriptors) {
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
  for (unsigned i = 0; i != 8; ++i)
    checksum |= uint64_t{encoded->bytecode[32 + i]} << (i * 8);
  talos_rt_execution_descriptor_v1 execution{TALOS_RT_VERSION,
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
  size_t checked = 0;
  for (auto declaration :
       design.getBody().front().getOps<talos::sim::SimVPIBoxTypeDeclOp>()) {
    SCOPED_TRACE(declaration.getSymName().str());
    auto native =
        talos::sim::getElementDescriptor(declaration.getValueType());
    ASSERT_TRUE(native);
    VPIBoxTypeBinding binding;
    ASSERT_EQ(
        talos_rt_cached_vpi_box_type(runtime, declaration.getId(), &binding),
        TALOS_RT_OK);
    std::vector<talos_rt_trace_entry_v1> entries;
    for (size_t i = 0; i != native->traceOffsets.size(); ++i)
      entries.push_back({static_cast<uint64_t>(native->traceOffsets[i]), 0, 1,
                         TALOS_RT_TRACE_STRONG,
                         static_cast<uint32_t>(native->traceKinds[i]),
                         nullptr});
    // The runtime interner supplies ordinary scalar managed-kind traces;
    // explicit compiler slots refine builtin handles and aggregate fields.
    if (entries.empty()) {
      uint32_t slot = 0;
      if (native->kind == talos::sim::ElementKind::String)
        slot = TALOS_RT_MANAGED_SLOT_STRING;
      if (native->kind == talos::sim::ElementKind::ClassHandle)
        slot = TALOS_RT_MANAGED_SLOT_CLASS;
      if (native->kind == talos::sim::ElementKind::ContainerHandle)
        slot = TALOS_RT_MANAGED_SLOT_CONTAINER;
      if (slot)
        entries.push_back({0, 0, 1, TALOS_RT_TRACE_STRONG, slot, nullptr});
    }
    talos_rt_trace_layout_v1 trace{TALOS_RT_VERSION,
                                     0,
                                     native->valueSize,
                                     native->alignment,
                                     entries.empty() ? nullptr : entries.data(),
                                     entries.size()};
    talos_rt_element_type_v1 element{TALOS_RT_VERSION,
                                       static_cast<uint32_t>(native->kind),
                                       native->typeID,
                                       static_cast<uint32_t>(native->flags),
                                       0,
                                       native->valueSize,
                                       native->alignment,
                                       native->bitWidth,
                                       entries.empty() ? nullptr : &trace,
                                       nullptr};
    auto matches = [&](const talos_rt_element_type_v1 &candidate) {
      return talos_rt_vpi_element_matches(runtime, binding.layout,
                                            binding.semantic, &candidate);
    };
    ASSERT_EQ(talos_rt_v1_element_type_validate(&element), TALOS_RT_OK);
    EXPECT_EQ(matches(element), TALOS_RT_OK);
    auto differentID = element;
    differentID.type_id ^= UINT64_C(0x723be234);
    EXPECT_EQ(matches(differentID), TALOS_RT_OK);
    auto signedStorage = element;
    signedStorage.flags ^= TALOS_RT_ELEMENT_SIGNED;
    EXPECT_EQ(matches(signedStorage), TALOS_RT_OK);
    auto wrong = element;
    wrong.kind = element.kind == TALOS_RT_ELEMENT_BITS
                     ? TALOS_RT_ELEMENT_LOGIC
                     : TALOS_RT_ELEMENT_BITS;
    EXPECT_EQ(matches(wrong), TALOS_RT_INVALID_ARGUMENT);
    wrong = element;
    ++wrong.bit_width;
    EXPECT_EQ(matches(wrong), TALOS_RT_INVALID_ARGUMENT);
    wrong = element;
    wrong.flags ^= TALOS_RT_ELEMENT_FOUR_STATE;
    EXPECT_EQ(matches(wrong), TALOS_RT_INVALID_ARGUMENT);
    if (!entries.empty()) {
      wrong = element;
      wrong.trace = nullptr;
      EXPECT_EQ(matches(wrong), TALOS_RT_INVALID_ARGUMENT);
      auto forgedEntries = entries;
      forgedEntries[0].slot_kind =
          entries[0].slot_kind == TALOS_RT_MANAGED_SLOT_STRING
              ? TALOS_RT_MANAGED_SLOT_CLASS
              : TALOS_RT_MANAGED_SLOT_STRING;
      auto forgedTrace = trace;
      forgedTrace.entries = forgedEntries.data();
      wrong = element;
      wrong.trace = &forgedTrace;
      ASSERT_EQ(talos_rt_v1_element_type_validate(&wrong), TALOS_RT_OK);
      EXPECT_EQ(matches(wrong), TALOS_RT_INVALID_ARGUMENT);
    }
    if (declaration.getSymName() == "record") {
      ASSERT_EQ(entries.size(), 1u);
      ASSERT_EQ(entries[0].offset, 8u);
      // An extra or shifted valid pointer slot must not reinterpret the
      // ordinary four-state field/padding as a managed reference.
      auto extra = entries;
      extra.push_back({0, 0, 1, TALOS_RT_TRACE_STRONG,
                       TALOS_RT_MANAGED_SLOT_STRING, nullptr});
      auto extraTrace = trace;
      extraTrace.entries = extra.data();
      extraTrace.entry_count = extra.size();
      wrong = element;
      wrong.trace = &extraTrace;
      ASSERT_EQ(talos_rt_v1_element_type_validate(&wrong), TALOS_RT_OK);
      EXPECT_EQ(matches(wrong), TALOS_RT_INVALID_ARGUMENT);
      extra.resize(1);
      extra[0].offset = 0;
      extraTrace.entry_count = 1;
      EXPECT_EQ(matches(wrong), TALOS_RT_INVALID_ARGUMENT);
    }
    if (declaration.getSymName() == "overlap") {
      ASSERT_EQ(entries.size(), 1u);
      EXPECT_EQ(entries[0].slot_kind,
                TALOS_RT_MANAGED_SLOT_CANDIDATE |
                    TALOS_RT_MANAGED_ROOT_KIND_STRING |
                    TALOS_RT_MANAGED_ROOT_KIND_CLASS |
                    TALOS_RT_MANAGED_ROOT_KIND_CONTAINER);
      auto narrowed = entries[0];
      narrowed.slot_kind = TALOS_RT_MANAGED_SLOT_CANDIDATE |
                           TALOS_RT_MANAGED_ROOT_KIND_STRING;
      auto narrowTrace = trace;
      narrowTrace.entries = &narrowed;
      wrong = element;
      wrong.trace = &narrowTrace;
      ASSERT_EQ(talos_rt_v1_element_type_validate(&wrong), TALOS_RT_OK);
      EXPECT_EQ(matches(wrong), TALOS_RT_INVALID_ARGUMENT);
    }
    if (declaration.getSymName() == "tagged") {
      ASSERT_EQ(entries.size(), 2u);
      EXPECT_EQ(entries[0].slot_kind, TALOS_RT_MANAGED_SLOT_STRING);
      EXPECT_EQ(entries[1].slot_kind, TALOS_RT_MANAGED_SLOT_SEMAPHORE);
      EXPECT_EQ(element.value_size, 17u); // 128 payload bits plus 2 tag bits.
      EXPECT_EQ(element.bit_width, 136u);
    }
    if (declaration.getSymName() == "array") {
      ASSERT_EQ(entries.size(), 2u);
      ASSERT_EQ(entries[0].offset, 8u);
      ASSERT_EQ(entries[1].offset, 24u);
      auto childEntry = entries[0];
      talos_rt_trace_layout_v1 child{TALOS_RT_VERSION, 0, 16, 1,
                                       &childEntry,        1};
      talos_rt_trace_entry_v1 embedded{0,
                                         16,
                                         2,
                                         TALOS_RT_TRACE_EMBEDDED,
                                         TALOS_RT_MANAGED_SLOT_INVALID,
                                         &child};
      auto embeddedTrace = trace;
      embeddedTrace.entries = &embedded;
      embeddedTrace.entry_count = 1;
      wrong = element;
      wrong.trace = &embeddedTrace;
      ASSERT_EQ(talos_rt_v1_element_type_validate(&wrong), TALOS_RT_OK);
      EXPECT_EQ(matches(wrong), TALOS_RT_OK);
    }
    if (declaration.getSymName() == "enum") {
      EXPECT_EQ(element.kind, TALOS_RT_ELEMENT_BITS);
      EXPECT_EQ(element.bit_width, 32u);
      EXPECT_EQ(element.value_size, 4u);
    }
    EXPECT_EQ(talos_rt_vpi_element_matches(runtime, binding.layout,
                                             binding.semantic, nullptr),
              TALOS_RT_INVALID_ARGUMENT);
    ++checked;
  }
  EXPECT_EQ(checked, 12u);
  talos_rt_v1_context_destroy(runtime);
}
} // namespace
