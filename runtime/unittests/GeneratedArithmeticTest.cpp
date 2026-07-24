//===- GeneratedArithmeticTest.cpp - Execute generated integer bytecode ---===//

#include "obelisk/Runtime/Runtime.h"

#include "../lib/RuntimeInternal.h"

#include "gtest/gtest.h"

extern "C" const obelisk_rt_process_descriptor_v1
    arithmeticDescriptor asm("arithmetic_process.__obelisk_process_descriptor");

TEST(GeneratedArithmetic, MultiWidthBytecodeMatchesNativeExecution) {
  ASSERT_EQ(arithmeticDescriptor.available_tiers,
            OBELISK_RT_TIER_MASK_NATIVE | OBELISK_RT_TIER_MASK_BYTECODE);
  ASSERT_NE(arithmeticDescriptor.execution, nullptr);
  ASSERT_NE(arithmeticDescriptor.design_bytecode, nullptr);
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(
                arithmeticDescriptor.execution, &context),
            OBELISK_RT_OK);
  obelisk_rt_process_instance_v1 *native = nullptr;
  obelisk_rt_process_instance_v1 *bytecode = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&arithmeticDescriptor, &native),
      OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_process_instance_create(&arithmeticDescriptor, &bytecode),
      OBELISK_RT_OK);

  for (unsigned step = 0; step != 2; ++step) {
    obelisk_rt_fragment_action_v1 nativeAction{}, bytecodeAction{};
    ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                  native, context, OBELISK_RT_TIER_NATIVE, &nativeAction),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                  bytecode, context, OBELISK_RT_TIER_BYTECODE, &bytecodeAction),
              OBELISK_RT_OK);
    EXPECT_EQ(bytecodeAction.kind, nativeAction.kind);
    EXPECT_EQ(bytecodeAction.suspend_kind, nativeAction.suspend_kind);
    EXPECT_EQ(bytecodeAction.continuation, nativeAction.continuation);
    if (step == 0) {
      EXPECT_EQ(nativeAction.kind, OBELISK_RT_FRAGMENT_SUSPEND);
      EXPECT_EQ(nativeAction.suspend_kind, OBELISK_RT_SUSPEND_DELAY);
      EXPECT_EQ(nativeAction.continuation, 1u);
    } else {
      EXPECT_EQ(nativeAction.kind, OBELISK_RT_FRAGMENT_TERMINATE);
    }
  }

  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(native), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(bytecode), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(GeneratedArithmetic, BytecodeDirectCallsRecycleScratchFrames) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(
                arithmeticDescriptor.execution, &context),
            OBELISK_RT_OK);
  for (unsigned iteration = 0; iteration != 1000; ++iteration) {
    obelisk_rt_process_instance_v1 *instance = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_process_instance_create(&arithmeticDescriptor, &instance),
        OBELISK_RT_OK);
    for (unsigned step = 0; step != 2; ++step) {
      obelisk_rt_fragment_action_v1 action{};
      ASSERT_EQ(obelisk_rt_v1_process_instance_execute(
                    instance, context, OBELISK_RT_TIER_BYTECODE, &action),
                OBELISK_RT_OK);
    }
    ASSERT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  }

  EXPECT_EQ(context->designTaskFrames.size(), 1u);
  obelisk_rt_v1_context_destroy(context);
}
