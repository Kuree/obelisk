//===- GeneratedArithmeticTest.cpp - Execute generated integer bytecode ---===//

#include "talos/Runtime/Runtime.h"

#include "../lib/RuntimeInternal.h"

#include "GeneratedSymbol.h"
#include "gtest/gtest.h"

extern "C" const talos_rt_process_descriptor_v1 arithmeticDescriptor asm(
    TALOS_TEST_SYMBOL("arithmetic_process.__talos_process_descriptor"));

TEST(GeneratedArithmetic, MultiWidthBytecodeMatchesNativeExecution) {
  ASSERT_EQ(arithmeticDescriptor.available_tiers,
            TALOS_RT_TIER_MASK_NATIVE | TALOS_RT_TIER_MASK_BYTECODE);
  ASSERT_NE(arithmeticDescriptor.execution, nullptr);
  ASSERT_NE(arithmeticDescriptor.design_bytecode, nullptr);
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(
                arithmeticDescriptor.execution, &context),
            TALOS_RT_OK);
  talos_rt_process_instance_v1 *native = nullptr;
  talos_rt_process_instance_v1 *bytecode = nullptr;
  ASSERT_EQ(
      talos_rt_v1_process_instance_create(&arithmeticDescriptor, &native),
      TALOS_RT_OK);
  ASSERT_EQ(
      talos_rt_v1_process_instance_create(&arithmeticDescriptor, &bytecode),
      TALOS_RT_OK);

  for (unsigned step = 0; step != 2; ++step) {
    talos_rt_fragment_action_v1 nativeAction{}, bytecodeAction{};
    ASSERT_EQ(talos_rt_v1_process_instance_execute(
                  native, context, TALOS_RT_TIER_NATIVE, &nativeAction),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_process_instance_execute(
                  bytecode, context, TALOS_RT_TIER_BYTECODE, &bytecodeAction),
              TALOS_RT_OK);
    EXPECT_EQ(bytecodeAction.kind, nativeAction.kind);
    EXPECT_EQ(bytecodeAction.suspend_kind, nativeAction.suspend_kind);
    EXPECT_EQ(bytecodeAction.continuation, nativeAction.continuation);
    if (step == 0) {
      EXPECT_EQ(nativeAction.kind, TALOS_RT_FRAGMENT_SUSPEND);
      EXPECT_EQ(nativeAction.suspend_kind, TALOS_RT_SUSPEND_DELAY);
      EXPECT_EQ(nativeAction.continuation, 1u);
    } else {
      EXPECT_EQ(nativeAction.kind, TALOS_RT_FRAGMENT_TERMINATE);
    }
  }

  EXPECT_EQ(talos_rt_v1_process_instance_destroy(native), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_process_instance_destroy(bytecode), TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);
}

TEST(GeneratedArithmetic, BytecodeDirectCallsRecycleScratchFrames) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(
                arithmeticDescriptor.execution, &context),
            TALOS_RT_OK);
  for (unsigned iteration = 0; iteration != 1000; ++iteration) {
    talos_rt_process_instance_v1 *instance = nullptr;
    ASSERT_EQ(
        talos_rt_v1_process_instance_create(&arithmeticDescriptor, &instance),
        TALOS_RT_OK);
    for (unsigned step = 0; step != 2; ++step) {
      talos_rt_fragment_action_v1 action{};
      ASSERT_EQ(talos_rt_v1_process_instance_execute(
                    instance, context, TALOS_RT_TIER_BYTECODE, &action),
                TALOS_RT_OK);
    }
    ASSERT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
  }

  EXPECT_EQ(context->designTaskFrames.size(), 1u);
  talos_rt_v1_context_destroy(context);
}
