//===- InitializationRandomTest.cpp - IEEE 18.14.1 scope streams --------===//

#include "../lib/ProcessValidation.h"
#include "../lib/RuntimeInternal.h"
#include "obelisk/Runtime/Runtime.h"
#include "gtest/gtest.h"

namespace {

obelisk_rt_status requirements(uint64_t *size, uint64_t *alignment) {
  *size = 0;
  *alignment = 8;
  return OBELISK_RT_OK;
}

obelisk_rt_status execute(obelisk_rt_process_instance_v1 *instance) {
  *instance->action = {OBELISK_RT_FRAGMENT_TERMINATE, 0, 0, 0, 0, 0};
  return OBELISK_RT_OK;
}

void destroy(obelisk_rt_process_instance_v1 *) {}

class InitializationRandomTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_context_seed(context, 9876), OBELISK_RT_OK);
    layout = {OBELISK_RT_VERSION, 0, 0, 8, nullptr, 0, 1, &continuation, 0};
    layout.checksum = obelisk::process::layoutChecksum(layout);
    descriptor = {{OBELISK_RT_DESCRIPTOR_PROCESS, 0, 17},
                  OBELISK_RT_VERSION,
                  0,
                  OBELISK_RT_TIER_MASK_NATIVE,
                  0,
                  &layout,
                  requirements,
                  execute,
                  destroy,
                  nullptr};
  }
  void TearDown() override { obelisk_rt_v1_context_destroy(context); }

  uint64_t enter(uint64_t scope) {
    uint64_t activation = 0;
    EXPECT_EQ(
        obelisk_rt_v1_random_initialization_enter(context, scope, &activation),
        OBELISK_RT_OK);
    return activation;
  }
  void leave(uint64_t activation) {
    EXPECT_EQ(obelisk_rt_v1_random_initialization_leave(context, activation),
              OBELISK_RT_OK);
  }
  uint64_t next() {
    uint64_t value = 0;
    EXPECT_EQ(obelisk_rt_v1_random_next(context, &value), OBELISK_RT_OK);
    return value;
  }
  size_t add() {
    obelisk_rt_process_instance_v1 *instance = nullptr;
    EXPECT_EQ(obelisk_rt_v1_process_instance_create(&descriptor, &instance),
              OBELISK_RT_OK);
    EXPECT_EQ(obelisk_rt_v1_scheduler_add_planned(context, instance, 0, 0,
                                                  nullptr, nullptr, 0),
              OBELISK_RT_OK);
    return context->scheduledProcesses.size() - 1;
  }
  static void same(const obelisk_rt_random_state_v1 &left,
                   const obelisk_rt_random_state_v1 &right) {
    EXPECT_EQ(left.state, right.state);
    EXPECT_EQ(left.increment, right.increment);
  }

  obelisk_rt_context *context = nullptr;
  uint32_t continuation = 0;
  obelisk_rt_frame_layout_v1 layout{};
  obelisk_rt_process_descriptor_v1 descriptor{};
};

TEST_F(InitializationRandomTest,
       ScopesStartWithDefaultSeedAndResumeIndependently) {
  obelisk_rt_random_state_v1 reference{};
  obelisk_rt_v1_random_state_seed(&reference, 9876,
                                  OBELISK_RT_RANDOM_DEFAULT_INCREMENT >> 1);
  uint64_t expectedFirst = obelisk_rt_v1_random_state_next64(&reference);
  uint64_t a = enter(100);
  EXPECT_EQ(next(), expectedFirst);
  leave(a);
  a = enter(100);
  EXPECT_EQ(next(), obelisk_rt_v1_random_state_next64(&reference));
  for (unsigned i = 0; i != 40; ++i)
    (void)next();
  leave(a);
  uint64_t b = enter(200);
  EXPECT_EQ(next(), expectedFirst);
  leave(b);
  // Creating and drawing from scopes did not consume the context stream.
  EXPECT_EQ(next(), expectedFirst);
}

TEST_F(InitializationRandomTest,
       NestedBindingsRestoreStartupProcessAndStreams) {
  obelisk_rt_random_state_v1 startup = context->random;
  context->activeRandom = &startup;
  context->activeLogicalProcessToken = 42;
  uint64_t a = enter(10);
  EXPECT_EQ(context->activeLogicalProcessToken, 0u);
  (void)next();
  auto *aStream = context->activeRandom;
  auto aState = *aStream;
  uint64_t b = enter(20);
  (void)next();
  EXPECT_EQ(obelisk_rt_v1_random_initialization_leave(context, a),
            OBELISK_RT_INVALID_LIFECYCLE);
  leave(b);
  EXPECT_EQ(context->activeRandom, aStream);
  same(*aStream, aState);
  leave(a);
  EXPECT_EQ(context->activeLogicalProcessToken, 42u);
  EXPECT_EQ(context->activeRandom, &startup);
  same(startup, context->random);
  context->activeRandom = nullptr;
  context->activeLogicalProcessToken = 0;
}

TEST_F(InitializationRandomTest, StaticRegistrationUsesContainingScope) {
  uint64_t a = enter(10);
  size_t first = add();
  auto firstState = context->scheduledProcesses[first].random;
  EXPECT_EQ(context->scheduledProcesses[first].initializationRandomScope, 10u);
  EXPECT_EQ(context->scheduledProcesses[first].parent, 0u);
  (void)add();
  (void)next();
  leave(a);
  uint64_t b = enter(20);
  size_t other = add();
  same(context->scheduledProcesses[other].random, firstState);
  leave(b);
}

TEST_F(InitializationRandomTest,
       DynamicChildrenBorrowTheirRunningParentStream) {
  uint64_t a = enter(10);
  size_t parent = add();
  auto parentState = context->scheduledProcesses[parent].random;
  context->activeLogicalProcessToken =
      context->scheduledProcesses[parent].token |
      OBELISK_RT_LOGICAL_PROCESS_NATIVE_TAG;
  context->activeRandom = nullptr;
  size_t child = add();
  EXPECT_EQ(context->scheduledProcesses[child].initializationRandomScope, 0u);
  EXPECT_EQ(context->scheduledProcesses[child].parent,
            context->activeLogicalProcessToken);
  EXPECT_NE(context->scheduledProcesses[parent].random.state,
            parentState.state);
  EXPECT_EQ(obelisk_rt_v1_random_seed(context, 123), OBELISK_RT_OK);
  // Restore the scheduler binding that the primed startup actor borrowed.
  context->activeLogicalProcessToken = 0;
  context->activeRandom = &context->initializationRandomScopes->streams.at(10);
  leave(a);
}

TEST_F(InitializationRandomTest,
       ReseedingDormantNativeAndBytecodeRootsUsesScopes) {
  uint64_t a = enter(10);
  size_t first = add();
  size_t second = add();
  leave(a);
  uint64_t b = enter(20);
  size_t other = add();
  leave(b);
  for (uint64_t scope : {uint64_t{10}, uint64_t{20}}) {
    context->scheduledDesignTasks.emplace_back();
    auto &task = context->scheduledDesignTasks.back();
    task.id = context->scheduledDesignTasks.size();
    task.initializationRandomScope = scope;
    task.insertionSequence = context->nextProcessInsertionSequence++;
  }
  context->scheduledDesignTasks.emplace_back();
  auto &running = context->scheduledDesignTasks.back();
  running.id = 3;
  running.started = true;
  running.random = {11, 13};
  ASSERT_EQ(obelisk_rt_v1_context_seed(context, 4567), OBELISK_RT_OK);
  same(context->scheduledProcesses[first].random,
       context->scheduledProcesses[other].random);
  same(context->scheduledProcesses[second].random,
       context->scheduledDesignTasks[1].random);
  auto &scope = context->initializationRandomScopes->streams.at(10);
  auto scopeState = scope;
  obelisk_rt_random_state_v1 nextChild{};
  obelisk_rt_random_split_initialization_unlocked(context, 10, nextChild);
  EXPECT_NE(nextChild.state, context->scheduledDesignTasks[0].random.state);
  same(context->scheduledDesignTasks[2].random, running.random);
  // The next child is the fourth scope-10 split after the two native and one
  // bytecode roots, and entering the scope resumes that advanced stream.
  scope = scopeState;
  a = enter(10);
  EXPECT_EQ(next(), obelisk_rt_v1_random_state_next64(&scopeState));
  leave(a);
}

TEST_F(InitializationRandomTest,
       ExplicitUrRandomSeedWorksInStaticInitialization) {
  uint64_t a = enter(10);
  auto initial = *context->activeRandom;
  ASSERT_EQ(obelisk_rt_v1_random_seed(context, 99), OBELISK_RT_OK);
  uint64_t seeded = next();
  ASSERT_EQ(obelisk_rt_v1_random_seed(context, 99), OBELISK_RT_OK);
  EXPECT_EQ(next(), seeded);
  leave(a);
  uint64_t b = enter(20);
  same(*context->activeRandom, initial);
  leave(b);
}

TEST_F(InitializationRandomTest, MalformedBindingsDoNotChangeTheActiveStream) {
  uint64_t activation = 999;
  auto before = context->random;
  EXPECT_EQ(obelisk_rt_v1_random_initialization_enter(nullptr, 1, &activation),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_random_initialization_enter(context, 0, &activation),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_random_initialization_enter(context, 1, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_random_initialization_leave(context, 9),
            OBELISK_RT_INVALID_LIFECYCLE);
  EXPECT_EQ(activation, 999u);
  same(context->random, before);
  uint64_t a = enter(1);
  leave(a);
  EXPECT_EQ(obelisk_rt_v1_random_initialization_leave(context, a),
            OBELISK_RT_INVALID_LIFECYCLE);
  uint64_t b = enter(1);
  EXPECT_NE(a, b);
  leave(b);
}

} // namespace
