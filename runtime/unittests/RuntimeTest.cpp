//===- RuntimeTest.cpp - Tests for the Obelisk native runtime -------------===//

#include "obelisk/Runtime/Runtime.h"
#include "svdpi.h"

#include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

extern "C" int obelisk_runtime_c_api_smoke(void);

namespace {

class RuntimeBuffer {
public:
  RuntimeBuffer() = default;
  RuntimeBuffer(const RuntimeBuffer &) = delete;
  RuntimeBuffer &operator=(const RuntimeBuffer &) = delete;
  ~RuntimeBuffer() { obelisk_rt_v1_buffer_release(&buffer); }

  obelisk_rt_buffer_v1 *out() { return &buffer; }
  std::string str() const {
    if (buffer.size == 0)
      return {};
    return std::string(reinterpret_cast<const char *>(buffer.data),
                       static_cast<size_t>(buffer.size));
  }

private:
  obelisk_rt_buffer_v1 buffer{};
};

class LogicValue {
public:
  explicit LogicValue(std::string_view symbols, bool isSigned = false)
      : width(symbols.size()), value((width + 63) / 64),
        unknown((width + 63) / 64), isSigned(isSigned) {
    for (size_t index = 0; index < symbols.size(); ++index) {
      char symbol = symbols[symbols.size() - index - 1];
      if (symbol == '1' || symbol == 'z' || symbol == 'Z')
        value[index / 64] |= uint64_t{1} << (index % 64);
      if (symbol == 'x' || symbol == 'X' || symbol == 'z' || symbol == 'Z')
        unknown[index / 64] |= uint64_t{1} << (index % 64);
    }
  }

  LogicValue(uint64_t width, std::vector<uint64_t> value,
             std::vector<uint64_t> unknown = {}, bool isSigned = false)
      : width(width), value(std::move(value)), unknown(std::move(unknown)),
        isSigned(isSigned) {}

  obelisk_rt_arg_v1 arg(uint32_t extraFlags = 0) const {
    return {OBELISK_RT_ARG_LOGIC,
            extraFlags |
                (isSigned ? static_cast<uint32_t>(OBELISK_RT_ARG_SIGNED) : 0u),
            width, value.data(), unknown.empty() ? nullptr : unknown.data()};
  }

private:
  uint64_t width;
  std::vector<uint64_t> value;
  std::vector<uint64_t> unknown;
  bool isSigned;
};

obelisk_rt_arg_v1 stringArg(std::string_view value, uint32_t flags = 0) {
  return {OBELISK_RT_ARG_STRING, flags, value.size(), value.data(), nullptr};
}

obelisk_rt_arg_v1 realArg(const double &value) {
  return {OBELISK_RT_ARG_REAL, 0, 0, &value, nullptr};
}

obelisk_rt_arg_v1 timeArg(const uint64_t &value) {
  return {OBELISK_RT_ARG_TIME, 0, 64, &value, nullptr};
}

void appendInstruction(std::vector<uint8_t> &code, uint8_t opcode,
                       uint8_t type = OBELISK_RT_BC_TYPE_NONE,
                       uint16_t destination = 0, uint16_t source0 = 0,
                       uint16_t source1 = 0, uint64_t immediate = 0) {
  size_t offset = code.size();
  code.resize(offset + OBELISK_RT_BYTECODE_INSTRUCTION_SIZE, 0);
  code[offset] = opcode;
  code[offset + 1] = type;
  auto write16 = [&](size_t byte, uint16_t value) {
    code[offset + byte] = static_cast<uint8_t>(value);
    code[offset + byte + 1] = static_cast<uint8_t>(value >> 8);
  };
  write16(2, destination);
  write16(4, source0);
  write16(6, source1);
  for (unsigned byte = 0; byte != 8; ++byte)
    code[offset + 8 + byte] = static_cast<uint8_t>(immediate >> (byte * 8));
}

void appendCheckedService(std::vector<uint8_t> &code, uint64_t site,
                          uint16_t statusRegister = 0) {
  uint64_t first = code.size() / OBELISK_RT_BYTECODE_INSTRUCTION_SIZE;
  appendInstruction(code, OBELISK_RT_BC_CALL_SERVICE, OBELISK_RT_BC_TYPE_STATUS,
                    statusRegister, 0, 0, site);
  appendInstruction(code, OBELISK_RT_BC_BRANCH_ZERO, OBELISK_RT_BC_TYPE_STATUS,
                    0, statusRegister, 0, first + 3);
  appendInstruction(code, OBELISK_RT_BC_FAIL, OBELISK_RT_BC_TYPE_STATUS, 0,
                    statusRegister);
}

obelisk_rt_fragment_descriptor_v1
bytecodeDescriptor(const std::vector<uint8_t> &code, uint32_t registers) {
  static constexpr obelisk_rt_bytecode_entry_v1 defaultEntry{0, 0};
  obelisk_rt_fragment_descriptor_v1 descriptor{};
  descriptor.handle = {OBELISK_RT_DESCRIPTOR_FRAGMENT, 0, 7};
  descriptor.code_kind = OBELISK_RT_FRAGMENT_BYTECODE;
  descriptor.code.bytecode = {code.data(), code.size(), &defaultEntry, 1,
                              registers,   0,           nullptr};
  return descriptor;
}

enum class ObserverEvaluatorMode : uint32_t {
  ConstantZero,
  ConstantOne,
  Fail,
  ReadFromJoinedThread,
  DestroyContext,
  RecurseOnceAndLatch,
  Recurse
};

std::atomic<ObserverEvaluatorMode> observerEvaluatorMode{
    ObserverEvaluatorMode::ConstantZero};
std::atomic<uint32_t> observerEvaluatorCalls{0};
std::atomic<bool> observerJoinedReadCompleted{false};

obelisk_rt_status observerEvaluator(obelisk_rt_context *context,
                                    const uint64_t *, uint32_t, uint64_t *value,
                                    uint64_t *unknown, uint32_t limbCount) {
  if (!value || !unknown || limbCount == 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  std::fill(value, value + limbCount, 0);
  std::fill(unknown, unknown + limbCount, 0);
  uint32_t call = observerEvaluatorCalls.fetch_add(1) + 1;
  switch (observerEvaluatorMode.load()) {
  case ObserverEvaluatorMode::ConstantZero:
    break;
  case ObserverEvaluatorMode::ConstantOne:
    value[0] = 1;
    break;
  case ObserverEvaluatorMode::Fail:
    return OBELISK_RT_INVALID_ARGUMENT;
  case ObserverEvaluatorMode::ReadFromJoinedThread: {
    std::thread reader([&] {
      (void)obelisk_rt_v1_scheduler_event_triggered(context, 99);
      observerJoinedReadCompleted = true;
    });
    reader.join();
    value[0] = 1;
    break;
  }
  case ObserverEvaluatorMode::DestroyContext:
    value[0] = 1;
    obelisk_rt_v1_context_destroy(context);
    break;
  case ObserverEvaluatorMode::RecurseOnceAndLatch:
    if (call == 1) {
      const uint8_t one = 1;
      const uint8_t zero = 0;
      obelisk_rt_v1_scheduler_signal_transition(
          context, obelisk_rt_v1_native_state_static_handle(1), 1, &one,
          nullptr, &zero, nullptr);
    }
    value[0] = 1;
    break;
  case ObserverEvaluatorMode::Recurse:
    if (call < 300) {
      uint8_t oldValue = static_cast<uint8_t>(call & 1);
      uint8_t newValue = static_cast<uint8_t>(oldValue ^ 1);
      obelisk_rt_v1_scheduler_signal_transition(
          context, obelisk_rt_v1_native_state_static_handle(1), 1, &oldValue,
          nullptr, &newValue, nullptr);
    }
    value[0] = call & 1;
    break;
  }
  return OBELISK_RT_OK;
}

obelisk_rt_status observerWaitRequirements(uint64_t *size,
                                           uint64_t *alignment) {
  if (!size || !alignment)
    return OBELISK_RT_INVALID_ARGUMENT;
  *size = 0;
  *alignment = 1;
  return OBELISK_RT_OK;
}

obelisk_rt_status overAlignedRequirements(uint64_t *size, uint64_t *alignment) {
  if (!size || !alignment)
    return OBELISK_RT_INVALID_ARGUMENT;
  *size = 0;
  *alignment = 32;
  return OBELISK_RT_OK;
}

obelisk_rt_status
observerWaitExecute(obelisk_rt_process_instance_v1 *instance) {
  if (!instance || !instance->action)
    return OBELISK_RT_INVALID_ARGUMENT;
  *instance->action = {OBELISK_RT_FRAGMENT_SUSPEND,
                       OBELISK_RT_SUSPEND_OBSERVER,
                       1,
                       OBELISK_RT_ACTION_FRAME_WAIT_RECORD,
                       0,
                       176};
  return OBELISK_RT_OK;
}

void observerWaitDestroy(obelisk_rt_process_instance_v1 *) {}

uint32_t observerSecondClauseCalls = 0;

obelisk_rt_status observerSecondEvaluator(obelisk_rt_context *,
                                          const uint64_t *, uint32_t,
                                          uint64_t *value, uint64_t *unknown,
                                          uint32_t limbCount) {
  if (!value || !unknown || limbCount != 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  ++observerSecondClauseCalls;
  value[0] = 1;
  unknown[0] = 0;
  return OBELISK_RT_OK;
}

obelisk_rt_status
observerTwoClauseExecute(obelisk_rt_process_instance_v1 *instance) {
  if (!instance || !instance->action)
    return OBELISK_RT_INVALID_ARGUMENT;
  *instance->action = {OBELISK_RT_FRAGMENT_SUSPEND,
                       OBELISK_RT_SUSPEND_OBSERVER,
                       1,
                       OBELISK_RT_ACTION_FRAME_WAIT_RECORD,
                       0,
                       256};
  return OBELISK_RT_OK;
}

uint64_t observerWaitLayoutChecksum(const obelisk_rt_frame_layout_v1 &layout) {
  uint64_t hash = UINT64_C(14695981039346656037);
  auto append = [&](const void *data, size_t size) {
    const auto *bytes = static_cast<const uint8_t *>(data);
    for (size_t index = 0; index != size; ++index) {
      hash ^= bytes[index];
      hash *= UINT64_C(1099511628211);
    }
  };
  append(&layout.version, sizeof(layout.version));
  append(&layout.flags, sizeof(layout.flags));
  append(&layout.frame_size, sizeof(layout.frame_size));
  append(&layout.frame_alignment, sizeof(layout.frame_alignment));
  append(&layout.field_count, sizeof(layout.field_count));
  append(&layout.continuation_count, sizeof(layout.continuation_count));
  for (uint32_t index = 0; index != layout.field_count; ++index)
    append(&layout.fields[index], sizeof(layout.fields[index]));
  for (uint32_t index = 0; index != layout.continuation_count; ++index)
    append(&layout.continuations[index], sizeof(layout.continuations[index]));
  return hash;
}

struct ObserverWaitTestDescriptor {
  obelisk_rt_observer_descriptor_v1 observer{
      7, nullptr, 0, 1, 0, OBELISK_RT_OBSERVER_NO_BYTECODE, observerEvaluator,
      0};
  obelisk_rt_execution_descriptor_v1 execution{};
  obelisk_rt_frame_field_v1 field{
      OBELISK_RT_FRAME_WAIT, OBELISK_RT_FRAME_FIELD_FLAGS_NONE, 0, 176, 8, 0};
  std::array<uint32_t, 2> continuations{0, 1};
  obelisk_rt_frame_layout_v1 layout{};
  obelisk_rt_process_descriptor_v1 process{};

  ObserverWaitTestDescriptor() {
    execution.version = OBELISK_RT_VERSION;
    execution.observers = &observer;
    execution.observer_count = 1;
    layout = {OBELISK_RT_VERSION,
              0,
              176,
              8,
              &field,
              1,
              static_cast<uint32_t>(continuations.size()),
              continuations.data(),
              0};
    layout.checksum = observerWaitLayoutChecksum(layout);
    process = {{OBELISK_RT_DESCRIPTOR_PROCESS, 0, 17},
               OBELISK_RT_VERSION,
               0,
               OBELISK_RT_TIER_MASK_NATIVE,
               0,
               &layout,
               observerWaitRequirements,
               observerWaitExecute,
               observerWaitDestroy,
               nullptr,
               &execution,
               nullptr};
  }

  static void populate(void *frame) {
    std::memset(frame, 0, 176);
    auto *wait = static_cast<obelisk_rt_computed_wait_record_v1 *>(frame);
    *wait = {OBELISK_RT_VERSION,
             OBELISK_RT_SUSPEND_OBSERVER,
             OBELISK_RT_COMPUTED_WAIT_INTERLEAVED,
             1,
             1,
             0,
             1,
             1,
             96,
             128,
             128,
             144,
             160,
             0,
             176,
             0};
    auto *binding = reinterpret_cast<obelisk_rt_computed_observer_v1 *>(
        static_cast<uint8_t *>(frame) + wait->observers_offset);
    *binding = {7, 0, 0, 0, 1, 160, 0};
    auto *dependency = reinterpret_cast<obelisk_rt_computed_dependency_v1 *>(
        static_cast<uint8_t *>(frame) + wait->dependencies_offset);
    *dependency = {obelisk_rt_v1_native_state_static_handle(1),
                   OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL, 1};
    auto *clause = reinterpret_cast<obelisk_rt_computed_clause_v1 *>(
        static_cast<uint8_t *>(frame) + wait->clauses_offset);
    *clause = {0, OBELISK_RT_OBSERVER_CONDITION_NONE,
               OBELISK_RT_WAIT_EDGE_CHANGE, 0};
  }
};

obelisk_rt_status startObserverWait(ObserverWaitTestDescriptor &descriptor,
                                    obelisk_rt_context **outContext) {
  if (!outContext)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outContext = nullptr;
  obelisk_rt_context *context = nullptr;
  obelisk_rt_status status =
      obelisk_rt_v1_context_create_for_design(&descriptor.execution, &context);
  if (status != OBELISK_RT_OK)
    return status;
  status = obelisk_rt_v1_native_state_register_static(context, 1, 0, 1);
  if (status != OBELISK_RT_OK) {
    obelisk_rt_v1_context_destroy(context);
    return status;
  }
  obelisk_rt_process_instance_v1 *instance = nullptr;
  status =
      obelisk_rt_v1_process_instance_create(&descriptor.process, &instance);
  if (status != OBELISK_RT_OK) {
    obelisk_rt_v1_context_destroy(context);
    return status;
  }
  void *frame = nullptr;
  uint64_t frameSize = 0;
  status = obelisk_rt_v1_process_instance_frame(instance, &frame, &frameSize);
  if (status != OBELISK_RT_OK || frameSize != 176) {
    (void)obelisk_rt_v1_process_instance_destroy(instance);
    obelisk_rt_v1_context_destroy(context);
    return status == OBELISK_RT_OK ? OBELISK_RT_INVALID_FRAME : status;
  }
  ObserverWaitTestDescriptor::populate(frame);
  status = obelisk_rt_v1_scheduler_add(context, instance, 0);
  if (status != OBELISK_RT_OK) {
    (void)obelisk_rt_v1_process_instance_destroy(instance);
    obelisk_rt_v1_context_destroy(context);
    return status;
  }
  status = obelisk_rt_v1_scheduler_run(context);
  if (status != OBELISK_RT_OK) {
    obelisk_rt_v1_context_destroy(context);
    return status;
  }
  *outContext = context;
  return OBELISK_RT_OK;
}

obelisk_rt_status
executeBytecode(const obelisk_rt_fragment_descriptor_v1 &input, void *frame,
                uint64_t frameSize, uint32_t continuation,
                obelisk_rt_fragment_action_v1 *action,
                uint64_t instructionLimit = 0,
                obelisk_rt_context *context = nullptr) {
  obelisk_rt_fragment_descriptor_v1 descriptor = input;
  descriptor.code.bytecode.register_offset = frameSize;
  uint64_t scratchSize =
      static_cast<uint64_t>(descriptor.code.bytecode.register_count) *
      OBELISK_RT_BYTECODE_REGISTER_SIZE;
  std::vector<uint8_t> storage(frameSize + scratchSize);
  if (frameSize != 0)
    std::memcpy(storage.data(), frame, frameSize);
  obelisk_rt_status status =
      instructionLimit == 0
          ? obelisk_rt_v1_fragment_execute(&descriptor, context,
                                           storage.empty() ? nullptr
                                                           : storage.data(),
                                           storage.size(), continuation, action)
          : obelisk_rt_v1_bytecode_execute_bounded(
                &descriptor, context,
                storage.empty() ? nullptr : storage.data(), storage.size(),
                continuation, instructionLimit, action);
  if (frameSize != 0)
    std::memcpy(frame, storage.data(), frameSize);
  return status;
}

std::string readHostFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

class TempDirectory {
public:
  TempDirectory() {
    static std::atomic<uint64_t> sequence{0};
    uint64_t stamp = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    path = std::filesystem::temp_directory_path() /
           ("obelisk-runtime-" + std::to_string(stamp) + "-" +
            std::to_string(sequence.fetch_add(1)));
    std::filesystem::create_directory(path);
  }
  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }

  std::filesystem::path file(std::string_view name) const {
    return path / std::string(name);
  }

private:
  std::filesystem::path path;
};

class RuntimeTest : public testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
    ASSERT_NE(context, nullptr);
  }

  void TearDown() override { obelisk_rt_v1_context_destroy(context); }

  std::pair<obelisk_rt_status, std::string>
  format(std::string_view formatString,
         const std::vector<obelisk_rt_arg_v1> &arguments,
         const obelisk_rt_format_env_v1 *environment = nullptr) {
    RuntimeBuffer output;
    obelisk_rt_status status = obelisk_rt_v1_format(
        context, formatString.data(), formatString.size(), arguments.data(),
        arguments.size(), environment, output.out());
    return {status, output.str()};
  }

  uint32_t open(const std::filesystem::path &path, std::string_view mode) {
    std::string pathString = path.string();
    uint32_t descriptor = 0;
    EXPECT_EQ(obelisk_rt_v1_file_open(context, pathString.data(),
                                      pathString.size(), mode.data(),
                                      mode.size(), &descriptor),
              OBELISK_RT_OK);
    EXPECT_NE(descriptor, 0u);
    return descriptor;
  }

  obelisk_rt_context *context = nullptr;
};

TEST(RuntimeABI, StableScalarLayout) {
  EXPECT_EQ(sizeof(obelisk_rt_status), 4u);
  EXPECT_EQ(sizeof(obelisk_rt_arg_kind), 4u);
  EXPECT_EQ(sizeof(obelisk_rt_arg_flags), 4u);
  EXPECT_EQ(offsetof(obelisk_rt_arg_v1, kind), 0u);
  EXPECT_EQ(offsetof(obelisk_rt_arg_v1, flags), 4u);
  EXPECT_EQ(offsetof(obelisk_rt_arg_v1, size), 8u);
  EXPECT_EQ(sizeof(obelisk_rt_activation_descriptor_v1), 24u);
  EXPECT_EQ(offsetof(obelisk_rt_activation_descriptor_v1, native_entry), 8u);
  EXPECT_EQ(offsetof(obelisk_rt_activation_descriptor_v1, bytecode_function),
            16u);
  EXPECT_EQ(sizeof(obelisk_rt_observer_capture_abi_v1), 8u);
  EXPECT_EQ(sizeof(obelisk_rt_observer_descriptor_v1), 48u);
  EXPECT_EQ(sizeof(obelisk_rt_execution_descriptor_v1), 120u);
  EXPECT_EQ(offsetof(obelisk_rt_execution_descriptor_v1, version), 0u);
  EXPECT_EQ(offsetof(obelisk_rt_execution_descriptor_v1, flags), 4u);
  EXPECT_EQ(offsetof(obelisk_rt_execution_descriptor_v1, reserved), 8u);
  EXPECT_EQ(offsetof(obelisk_rt_execution_descriptor_v1, activations), 88u);
  EXPECT_EQ(offsetof(obelisk_rt_execution_descriptor_v1, observers), 104u);
  EXPECT_EQ(sizeof(obelisk_rt_computed_wait_record_v1), 96u);
  EXPECT_EQ(sizeof(obelisk_rt_computed_observer_v1), 32u);
  EXPECT_EQ(sizeof(obelisk_rt_computed_capture_v1), 32u);
  EXPECT_EQ(sizeof(obelisk_rt_computed_dependency_v1), 16u);
  EXPECT_EQ(sizeof(obelisk_rt_computed_clause_v1), 16u);
  EXPECT_EQ(OBELISK_RT_VERSION, 1u);
  EXPECT_STREQ(obelisk_rt_v1_status_string(OBELISK_RT_FORMAT_ERROR),
               "format error");
}

TEST(RuntimeABI, CConsumerCompilesLinksAndRuns) {
  EXPECT_EQ(obelisk_runtime_c_api_smoke(), 0);
}

TEST(RuntimeABI, RejectsMalformedActivationInventory) {
  const obelisk_rt_activation_descriptor_v1 activation{
      1, nullptr, OBELISK_RT_ACTIVATION_NO_BYTECODE,
      OBELISK_RT_ACTIVATION_HAS_NATIVE};
  const obelisk_rt_execution_descriptor_v1 execution{
      OBELISK_RT_VERSION, 0, 0, nullptr, 0, nullptr, 0, 0, 0, nullptr, 0, 0, 0,
      &activation,        1,
  };
  obelisk_rt_context *context = nullptr;
  EXPECT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(context, nullptr);
}

TEST(RuntimeABI, ValidatesObserverInventoryAndDescriptorVersion) {
  obelisk_rt_observer_capture_abi_v1 capture{
      OBELISK_RT_OBSERVER_CAPTURE_STORAGE, 8};
  obelisk_rt_observer_descriptor_v1 observer{
      7, &capture, 1, 1, 0, OBELISK_RT_OBSERVER_NO_BYTECODE, observerEvaluator,
      0};
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.observers = &observer;
  execution.observer_count = 1;

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);

  execution.version = OBELISK_RT_VERSION - 1;
  EXPECT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(context, nullptr);
  execution.version = OBELISK_RT_VERSION;

  capture.kind = 0;
  EXPECT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(context, nullptr);
  capture.kind = OBELISK_RT_OBSERVER_CAPTURE_STORAGE;
  observer.native_evaluator = nullptr;
  EXPECT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(context, nullptr);
}

TEST(RuntimeABI, AlignsProcessFramesForNativeState) {
  const uint32_t continuation = 0;
  obelisk_rt_frame_layout_v1 layout{OBELISK_RT_VERSION, 0, 8, 8, nullptr, 0, 1,
                                    &continuation,      0};
  layout.checksum = observerWaitLayoutChecksum(layout);
  obelisk_rt_process_descriptor_v1 process{
      {OBELISK_RT_DESCRIPTOR_PROCESS, 0, 19},
      OBELISK_RT_VERSION,
      0,
      OBELISK_RT_TIER_MASK_NATIVE,
      0,
      &layout,
      overAlignedRequirements,
      observerWaitExecute,
      observerWaitDestroy,
      nullptr,
      nullptr,
      nullptr};
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(obelisk_rt_v1_process_instance_create(&process, &instance),
            OBELISK_RT_OK);
  void *frame = nullptr;
  uint64_t frameSize = 0;
  ASSERT_EQ(obelisk_rt_v1_process_instance_frame(instance, &frame, &frameSize),
            OBELISK_RT_OK);
  EXPECT_EQ(frameSize, 8u);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(frame) % 32, 0u);
  EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
}

TEST(RuntimeABI, RejectsMalformedComputedWaitRecords) {
  ObserverWaitTestDescriptor descriptor;
  auto execute = [&](auto mutate, obelisk_rt_status expected) {
    obelisk_rt_process_instance_v1 *instance = nullptr;
    ASSERT_EQ(
        obelisk_rt_v1_process_instance_create(&descriptor.process, &instance),
        OBELISK_RT_OK);
    void *frame = nullptr;
    uint64_t frameSize = 0;
    ASSERT_EQ(
        obelisk_rt_v1_process_instance_frame(instance, &frame, &frameSize),
        OBELISK_RT_OK);
    ASSERT_EQ(frameSize, 176u);
    ObserverWaitTestDescriptor::populate(frame);
    mutate(*static_cast<obelisk_rt_computed_wait_record_v1 *>(frame), frame);
    obelisk_rt_fragment_action_v1 action{};
    EXPECT_EQ(obelisk_rt_v1_process_instance_execute(
                  instance, nullptr, OBELISK_RT_TIER_NATIVE, &action),
              expected);
    EXPECT_EQ(obelisk_rt_v1_process_instance_destroy(instance), OBELISK_RT_OK);
  };

  execute([](auto &, void *) {}, OBELISK_RT_OK);
  execute([](auto &wait, void *) { wait.version = OBELISK_RT_VERSION + 1; },
          OBELISK_RT_INVALID_FRAME);
  execute([](auto &wait, void *) { wait.observers_offset = UINT64_MAX; },
          OBELISK_RT_INVALID_FRAME);
  execute(
      [](auto &wait, void *frame) {
        auto *binding = reinterpret_cast<obelisk_rt_computed_observer_v1 *>(
            static_cast<uint8_t *>(frame) + wait.observers_offset);
        binding->code_unit_id = 8;
      },
      OBELISK_RT_INVALID_FRAME);
  execute(
      [](auto &wait, void *frame) {
        auto *clause = reinterpret_cast<obelisk_rt_computed_clause_v1 *>(
            static_cast<uint8_t *>(frame) + wait.clauses_offset);
        clause->condition_observer = 0;
      },
      OBELISK_RT_INVALID_FRAME);
  execute(
      [](auto &wait, void *frame) {
        auto *binding = reinterpret_cast<obelisk_rt_computed_observer_v1 *>(
            static_cast<uint8_t *>(frame) + wait.observers_offset);
        binding->previous_offset += sizeof(uint64_t);
      },
      OBELISK_RT_INVALID_FRAME);
  execute(
      [](auto &wait, void *frame) {
        auto *dependency =
            reinterpret_cast<obelisk_rt_computed_dependency_v1 *>(
                static_cast<uint8_t *>(frame) + wait.dependencies_offset);
        dependency->stable_id = UINT64_MAX;
      },
      OBELISK_RT_INVALID_FRAME);
  execute(
      [](auto &wait, void *frame) {
        auto *dependency =
            reinterpret_cast<obelisk_rt_computed_dependency_v1 *>(
                static_cast<uint8_t *>(frame) + wait.dependencies_offset);
        dependency->width = 0;
      },
      OBELISK_RT_INVALID_FRAME);
  execute(
      [](auto &wait, void *frame) {
        auto *clause = reinterpret_cast<obelisk_rt_computed_clause_v1 *>(
            static_cast<uint8_t *>(frame) + wait.clauses_offset);
        clause->flags = UINT32_MAX;
      },
      OBELISK_RT_INVALID_FRAME);
  execute([](auto &wait, void *) { wait.previous_limb_count = UINT32_MAX; },
          OBELISK_RT_INVALID_FRAME);
}

TEST(RuntimeABI, ObserverEvaluatorRunsWithoutTheContextMutexHeld) {
  ObserverWaitTestDescriptor descriptor;
  observerEvaluatorMode = ObserverEvaluatorMode::ReadFromJoinedThread;
  observerEvaluatorCalls = 0;
  observerJoinedReadCompleted = false;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(startObserverWait(descriptor, &context), OBELISK_RT_OK);

  const uint8_t zero = 0;
  const uint8_t one = 1;
  obelisk_rt_v1_scheduler_signal_transition(
      context, obelisk_rt_v1_native_state_static_handle(1), 1, &zero, nullptr,
      &one, nullptr);
  EXPECT_TRUE(observerJoinedReadCompleted.load());
  EXPECT_EQ(observerEvaluatorCalls.load(), 1u);
  EXPECT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
  observerEvaluatorMode = ObserverEvaluatorMode::ConstantZero;
}

TEST(RuntimeABI, ObserverEvaluatorFailurePropagatesThroughScheduler) {
  ObserverWaitTestDescriptor descriptor;
  observerEvaluatorMode = ObserverEvaluatorMode::Fail;
  observerEvaluatorCalls = 0;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(startObserverWait(descriptor, &context), OBELISK_RT_OK);

  const uint8_t zero = 0;
  const uint8_t one = 1;
  obelisk_rt_v1_scheduler_signal_transition(
      context, obelisk_rt_v1_native_state_static_handle(1), 1, &zero, nullptr,
      &one, nullptr);
  EXPECT_EQ(observerEvaluatorCalls.load(), 1u);
  EXPECT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_INVALID_ARGUMENT);
  obelisk_rt_v1_context_destroy(context);
  observerEvaluatorMode = ObserverEvaluatorMode::ConstantZero;
}

TEST(RuntimeABI, ObserverEvaluatorDefersContextDestructionUntilReturn) {
  ObserverWaitTestDescriptor descriptor;
  observerEvaluatorMode = ObserverEvaluatorMode::DestroyContext;
  observerEvaluatorCalls = 0;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(startObserverWait(descriptor, &context), OBELISK_RT_OK);

  const uint8_t zero = 0;
  const uint8_t one = 1;
  // Destruction occurs from inside the callback. The transaction pins all
  // state needed to restore the producer and release the waiting activation,
  // then performs final cleanup as this publication returns.
  obelisk_rt_v1_scheduler_signal_transition(
      context, obelisk_rt_v1_native_state_static_handle(1), 1, &zero, nullptr,
      &one, nullptr);
  EXPECT_EQ(observerEvaluatorCalls.load(), 1u);
  observerEvaluatorMode = ObserverEvaluatorMode::ConstantZero;
}

TEST(RuntimeABI, NestedObserverLatchStopsLaterSourceClauses) {
  std::array<obelisk_rt_observer_descriptor_v1, 2> observers{{
      {7, nullptr, 0, 1, 0, OBELISK_RT_OBSERVER_NO_BYTECODE, observerEvaluator,
       0},
      {8, nullptr, 0, 1, 0, OBELISK_RT_OBSERVER_NO_BYTECODE,
       observerSecondEvaluator, 0},
  }};
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.observers = observers.data();
  execution.observer_count = observers.size();
  obelisk_rt_frame_field_v1 field{
      OBELISK_RT_FRAME_WAIT, OBELISK_RT_FRAME_FIELD_FLAGS_NONE, 0, 256, 8, 0};
  std::array<uint32_t, 2> continuations{{0, 1}};
  obelisk_rt_frame_layout_v1 layout{OBELISK_RT_VERSION,
                                    0,
                                    256,
                                    8,
                                    &field,
                                    1,
                                    static_cast<uint32_t>(continuations.size()),
                                    continuations.data(),
                                    0};
  layout.checksum = observerWaitLayoutChecksum(layout);
  obelisk_rt_process_descriptor_v1 process{
      {OBELISK_RT_DESCRIPTOR_PROCESS, 0, 18},
      OBELISK_RT_VERSION,
      0,
      OBELISK_RT_TIER_MASK_NATIVE,
      0,
      &layout,
      observerWaitRequirements,
      observerTwoClauseExecute,
      observerWaitDestroy,
      nullptr,
      &execution,
      nullptr};

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 1),
            OBELISK_RT_OK);
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(obelisk_rt_v1_process_instance_create(&process, &instance),
            OBELISK_RT_OK);
  void *frame = nullptr;
  uint64_t frameSize = 0;
  ASSERT_EQ(obelisk_rt_v1_process_instance_frame(instance, &frame, &frameSize),
            OBELISK_RT_OK);
  ASSERT_EQ(frameSize, 256u);
  std::memset(frame, 0, frameSize);
  auto *wait = static_cast<obelisk_rt_computed_wait_record_v1 *>(frame);
  *wait = {OBELISK_RT_VERSION,
           OBELISK_RT_SUSPEND_OBSERVER,
           OBELISK_RT_COMPUTED_WAIT_INTERLEAVED,
           2,
           2,
           0,
           2,
           2,
           96,
           160,
           160,
           192,
           224,
           0,
           256,
           0};
  auto *bindings = reinterpret_cast<obelisk_rt_computed_observer_v1 *>(
      static_cast<uint8_t *>(frame) + wait->observers_offset);
  bindings[0] = {7, 0, 0, 0, 1, 224, 0};
  bindings[1] = {8, 0, 0, 1, 1, 240, 0};
  auto *dependencies = reinterpret_cast<obelisk_rt_computed_dependency_v1 *>(
      static_cast<uint8_t *>(frame) + wait->dependencies_offset);
  dependencies[0] = {obelisk_rt_v1_native_state_static_handle(1),
                     OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL, 1};
  dependencies[1] = dependencies[0];
  auto *clauses = reinterpret_cast<obelisk_rt_computed_clause_v1 *>(
      static_cast<uint8_t *>(frame) + wait->clauses_offset);
  clauses[0] = {0, OBELISK_RT_OBSERVER_CONDITION_NONE,
                OBELISK_RT_WAIT_EDGE_CHANGE, 0};
  clauses[1] = {1, OBELISK_RT_OBSERVER_CONDITION_NONE,
                OBELISK_RT_WAIT_EDGE_CHANGE, 0};
  ASSERT_EQ(obelisk_rt_v1_scheduler_add(context, instance, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);

  observerEvaluatorMode = ObserverEvaluatorMode::RecurseOnceAndLatch;
  observerEvaluatorCalls = 0;
  observerSecondClauseCalls = 0;
  const uint8_t zero = 0;
  const uint8_t one = 1;
  obelisk_rt_v1_scheduler_signal_transition(
      context, obelisk_rt_v1_native_state_static_handle(1), 1, &zero, nullptr,
      &one, nullptr);
  EXPECT_EQ(observerEvaluatorCalls.load(), 2u);
  EXPECT_EQ(observerSecondClauseCalls, 0u);
  obelisk_rt_v1_context_destroy(context);
  observerEvaluatorMode = ObserverEvaluatorMode::ConstantZero;
}

TEST(RuntimeABI, RecursiveObserverEvaluationHasADeterministicDepthLimit) {
  ObserverWaitTestDescriptor descriptor;
  observerEvaluatorMode = ObserverEvaluatorMode::Recurse;
  observerEvaluatorCalls = 0;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(startObserverWait(descriptor, &context), OBELISK_RT_OK);

  const uint8_t zero = 0;
  const uint8_t one = 1;
  obelisk_rt_v1_scheduler_signal_transition(
      context, obelisk_rt_v1_native_state_static_handle(1), 1, &zero, nullptr,
      &one, nullptr);
  EXPECT_EQ(observerEvaluatorCalls.load(), 256u);
  EXPECT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OUT_OF_RESOURCES);
  obelisk_rt_v1_context_destroy(context);
  observerEvaluatorMode = ObserverEvaluatorMode::ConstantZero;
}

TEST(RuntimeABI, ReportsEveryStatusAndReleasesBuffersIdempotently) {
  static constexpr std::pair<obelisk_rt_status, const char *> statuses[] = {
      {OBELISK_RT_OK, "ok"},
      {OBELISK_RT_EOF, "end of file"},
      {OBELISK_RT_INVALID_ARGUMENT, "invalid argument"},
      {OBELISK_RT_INVALID_HANDLE, "invalid handle"},
      {OBELISK_RT_IO_ERROR, "I/O error"},
      {OBELISK_RT_OUT_OF_MEMORY, "out of memory"},
      {OBELISK_RT_OUT_OF_RESOURCES, "out of resources"},
      {OBELISK_RT_FORMAT_ERROR, "format error"},
      {OBELISK_RT_ARGUMENT_MISMATCH, "format argument mismatch"},
      {OBELISK_RT_INVALID_BYTECODE, "invalid bytecode"},
      {OBELISK_RT_STEP_LIMIT, "fragment step limit exceeded"},
      {OBELISK_RT_LAYOUT_MISMATCH, "process frame layout mismatch"},
      {OBELISK_RT_INVALID_CONTINUATION, "invalid process continuation"},
      {OBELISK_RT_TIER_UNAVAILABLE, "requested process tier unavailable"},
      {OBELISK_RT_AOT_CHECKPOINT,
       "native scheduler synchronization checkpoint"},
      {OBELISK_RT_AOT_TIMED_CHECKPOINT,
       "native scheduler timed synchronization checkpoint"},
      {OBELISK_RT_AOT_GENERATED_CHECKPOINT,
       "generated native scheduler branch checkpoint"},
      {OBELISK_RT_INVALID_LIFECYCLE, "invalid process lifecycle transition"},
      {OBELISK_RT_INVALID_FRAME, "invalid process frame record"},
      {OBELISK_RT_INVALID_DESIGN, "invalid design metadata"},
      {OBELISK_RT_PERMISSION_DENIED, "permission denied"},
      {OBELISK_RT_DPI_DISABLE_UNSUPPORTED, "DPI task disable is unsupported"},
      {OBELISK_RT_FATAL, "fatal SystemVerilog diagnostic"},
  };
  for (const auto &[status, message] : statuses)
    EXPECT_STREQ(obelisk_rt_v1_status_string(status), message);
  EXPECT_STREQ(obelisk_rt_v1_status_string(-1), "unknown runtime status");

  obelisk_rt_buffer_v1 buffer{};
  obelisk_rt_v1_buffer_release(nullptr);
  obelisk_rt_v1_buffer_release(&buffer);
  obelisk_rt_v1_buffer_release(&buffer);
  EXPECT_EQ(buffer.data, nullptr);
  EXPECT_EQ(buffer.size, 0u);
}

TEST(RuntimeABI, FinishStopAndFatalHaveDistinctEntryPoints) {
  EXPECT_EQ(obelisk_rt_v1_scheduler_finish(nullptr, 0),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_scheduler_stop(nullptr, 0),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_scheduler_fatal(nullptr, 0),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_scheduler_termination_requested(nullptr), 0u);
  EXPECT_EQ(obelisk_rt_v1_scheduler_time(nullptr), 0u);

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_NE(context, nullptr);
  EXPECT_EQ(obelisk_rt_v1_scheduler_termination_requested(context), 0u);
  EXPECT_EQ(obelisk_rt_v1_scheduler_time(context), 0u);
  EXPECT_EQ(obelisk_rt_v1_scheduler_finish(context, 2), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_scheduler_termination_requested(context), 1u);
  EXPECT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);

  context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_NE(context, nullptr);
  EXPECT_EQ(obelisk_rt_v1_scheduler_termination_requested(context), 0u);
  EXPECT_EQ(obelisk_rt_v1_scheduler_stop(context, 1), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_scheduler_termination_requested(context), 1u);
  EXPECT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);

  context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_NE(context, nullptr);
  EXPECT_EQ(obelisk_rt_v1_scheduler_termination_requested(context), 0u);
  EXPECT_EQ(obelisk_rt_v1_scheduler_fatal(context, 0), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_scheduler_termination_requested(context), 1u);
  EXPECT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_FATAL);
  obelisk_rt_v1_context_destroy(context);
}

struct DpiObservation {
  uint32_t calls = 0;
  uint32_t nestedID = 0;
  obelisk_rt_import_site_v1 nestedSite{};
  bool invokeNested = false;
  int userKey = 0;
};

obelisk_rt_status observeDpiCall(obelisk_rt_context *context, uint32_t importID,
                                 const obelisk_rt_import_input_v1 *inputs,
                                 uint32_t inputCount,
                                 obelisk_rt_import_output_v1 *outputs,
                                 uint32_t outputCount, void *userData) {
  auto &observation = *static_cast<DpiObservation *>(userData);
  ++observation.calls;
  EXPECT_NE(importID, 0u);
  EXPECT_EQ(inputCount, 1u);
  EXPECT_EQ(outputCount, 1u);
  EXPECT_EQ(inputs[0].bit_width, 65u);
  EXPECT_EQ(outputs[0].value[0], 0u);
  EXPECT_EQ(outputs[0].value[1], 0u);

  svScope initial = svGetScope();
  EXPECT_NE(initial, nullptr);
  const char *expectedScope =
      importID == observation.nestedID ? "top.child" : "top";
  EXPECT_STREQ(svGetNameFromScope(initial), expectedScope);
  EXPECT_EQ(svGetScopeFromName(expectedScope), initial);
  int32_t unit = 0, precision = 0;
  EXPECT_EQ(svGetTimeUnit(initial, &unit), 0);
  EXPECT_EQ(svGetTimePrecision(initial, &precision), 0);
  EXPECT_EQ(unit, -9);
  EXPECT_EQ(precision, -12);
  const char *file = nullptr;
  int line = 0;
  EXPECT_EQ(svGetCallerInfo(&file, &line), 1);
  EXPECT_STREQ(file, "dpi_test.sv");
  EXPECT_EQ(line, 41);
  EXPECT_EQ(svPutUserData(initial, &observation.userKey, &observation), 0);
  EXPECT_EQ(svGetUserData(initial, &observation.userKey), &observation);
  svTimeVal time{};
  EXPECT_EQ(svGetTime(initial, &time), 0);
  EXPECT_EQ(time.type, sv_sim_time);
  EXPECT_EQ(time.high, 0u);
  EXPECT_EQ(time.low, 0u);

  if (observation.invokeNested && importID != observation.nestedID) {
    uint64_t nestedValue[2]{};
    obelisk_rt_import_output_v1 nestedOutput{
        OBELISK_RT_DBREG_BITS, 0, 0, 65, nestedValue, nullptr, 2};
    EXPECT_EQ(obelisk_rt_v1_import_call(context, &observation.nestedSite,
                                        inputs, inputCount, &nestedOutput, 1),
              OBELISK_RT_OK);
    EXPECT_EQ(svGetScope(), initial);
    EXPECT_STREQ(svGetNameFromScope(initial), "top");
  }
  outputs[0].value[0] = inputs[0].value[0] ^ UINT64_C(0xffff);
  outputs[0].value[1] = UINT64_MAX;
  return OBELISK_RT_OK;
}

TEST(RuntimeDPI, ValidatesDispatchContextAndNestedRestoration) {
  static constexpr char topName[] = "top";
  static constexpr char childName[] = "top.child";
  const obelisk_rt_dpi_scope_v1 scopes[] = {
      {0, UINT64_MAX, topName, sizeof(topName) - 1, -9, -12, 0},
      {1, 0, childName, sizeof(childName) - 1, -9, -12, 0},
  };
  obelisk_rt_execution_descriptor_v1 execution{
      OBELISK_RT_VERSION, 0,   0, nullptr, 0, nullptr, 0, 0, 0, scopes,
      std::size(scopes),  -12, 0,
  };
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  DpiObservation observation;
  constexpr std::string_view outerName = "outer";
  constexpr std::string_view innerName = "inner";
  uint32_t outerID = obelisk_rt_v1_import_id(
      reinterpret_cast<const uint8_t *>(outerName.data()), outerName.size());
  observation.nestedID = obelisk_rt_v1_import_id(
      reinterpret_cast<const uint8_t *>(innerName.data()), innerName.size());
  static constexpr char caller[] = "dpi_test.sv";
  observation.nestedSite = {OBELISK_RT_VERSION,
                            OBELISK_RT_IMPORT_CONTEXT,
                            observation.nestedID,
                            0,
                            1,
                            caller,
                            sizeof(caller) - 1,
                            41,
                            7};
  observation.invokeNested = true;
  ASSERT_EQ(obelisk_rt_v1_context_register_import(context, outerID,
                                                  observeDpiCall, &observation),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_context_register_import(context, observation.nestedID,
                                                  observeDpiCall, &observation),
            OBELISK_RT_OK);

  uint64_t inputValue[2]{UINT64_C(0x123456789abcdef0), 1};
  uint64_t outputValue[2]{UINT64_MAX, UINT64_MAX};
  obelisk_rt_import_input_v1 input{
      OBELISK_RT_DBREG_BITS, 0, 0, 65, inputValue, nullptr, 2};
  obelisk_rt_import_output_v1 output{OBELISK_RT_DBREG_BITS, 0,       0, 65,
                                     outputValue,           nullptr, 2};
  obelisk_rt_import_site_v1 site{
      OBELISK_RT_VERSION,
      OBELISK_RT_IMPORT_CONTEXT,
      outerID,
      0,
      0,
      caller,
      sizeof(caller) - 1,
      41,
      3,
  };
  EXPECT_EQ(obelisk_rt_v1_import_call(context, &site, &input, 1, &output, 1),
            OBELISK_RT_OK);
  EXPECT_EQ(observation.calls, 2u);
  EXPECT_EQ(outputValue[0], inputValue[0] ^ UINT64_C(0xffff));
  EXPECT_EQ(outputValue[1], 1u);
  EXPECT_EQ(svGetScope(), nullptr);

  site.reserved = 1;
  EXPECT_EQ(obelisk_rt_v1_import_call(context, &site, &input, 1, &output, 1),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(observation.calls, 2u);
  site.reserved = 0;
  site.import_id ^= UINT32_C(0x40000000);
  EXPECT_EQ(obelisk_rt_v1_import_call(context, &site, &input, 1, &output, 1),
            OBELISK_RT_TIER_UNAVAILABLE);
  EXPECT_EQ(observation.calls, 2u);
  site.import_id = outerID;
  observation.invokeNested = false;
  ASSERT_EQ(
      obelisk_rt_v1_context_register_import_signature(
          context, outerID, UINT64_C(0x1234), observeDpiCall, &observation),
      OBELISK_RT_OK);
  site.abi_signature = UINT64_C(0x5678);
  EXPECT_EQ(obelisk_rt_v1_import_call(context, &site, &input, 1, &output, 1),
            OBELISK_RT_ARGUMENT_MISMATCH);
  EXPECT_EQ(observation.calls, 2u);
  site.abi_signature = UINT64_C(0x1234);
  EXPECT_EQ(obelisk_rt_v1_import_call(context, &site, &input, 1, &output, 1),
            OBELISK_RT_OK);
  EXPECT_EQ(observation.calls, 3u);
  obelisk_rt_v1_context_destroy(context);
}

TEST(RuntimeDPI, RejectsMalformedScopeMetadata) {
  static constexpr char name[] = "top";
  obelisk_rt_dpi_scope_v1 scope{1,  UINT64_MAX, name, sizeof(name) - 1,
                                -9, -12,        0};
  obelisk_rt_execution_descriptor_v1 execution{
      OBELISK_RT_VERSION, 0, 0, nullptr, 0, nullptr, 0, 0, 0, &scope, 1, -12, 0,
  };
  obelisk_rt_context *context = nullptr;
  EXPECT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(context, nullptr);

  scope.id = 0;
  scope.time_precision = -15;
  EXPECT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(context, nullptr);
}

TEST(RuntimeABI, RejectsNullPublicArguments) {
  EXPECT_EQ(obelisk_rt_v1_context_create(nullptr), OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_import_id(nullptr, 0), 0u);
  EXPECT_EQ(obelisk_rt_v1_import_id(nullptr, 1), 0u);
  EXPECT_EQ(obelisk_rt_v1_context_register_import(nullptr, 1, nullptr, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  obelisk_rt_v1_context_destroy(nullptr);
  EXPECT_EQ(obelisk_rt_v1_last_error(nullptr, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(
      obelisk_rt_v1_format(nullptr, nullptr, 0, nullptr, 0, nullptr, nullptr),
      OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_display(nullptr, 0, 0, OBELISK_RT_RADIX_DECIMAL,
                                  nullptr, 0, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_open_mcd(nullptr, nullptr, 0, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_open(nullptr, nullptr, 0, nullptr, 0, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_close(nullptr, 0), OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_flush(nullptr, 0), OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_write(nullptr, 0, nullptr, 0, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_read(nullptr, 0, nullptr, 0, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_getc(nullptr, 0, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_ungetc(nullptr, 0, 0),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_getline(nullptr, 0, 0, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_eof(nullptr, 0, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_error(nullptr, 0, nullptr, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_seek(nullptr, 0, 0, OBELISK_RT_SEEK_SET),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_tell(nullptr, 0, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_rewind(nullptr, 0), OBELISK_RT_INVALID_ARGUMENT);
}

TEST_F(RuntimeTest, FormatsExactFourStateRadices) {
  LogicValue value("10xz01xz");
  auto [status, output] =
      format("%b %o %h", {value.arg(), value.arg(), value.arg()});
  EXPECT_EQ(status, OBELISK_RT_OK);
  EXPECT_EQ(output, "10xz01xz 2XX XX");

  LogicValue leading("0000xxxx");
  auto [leadingStatus, leadingOutput] =
      format("%h %0h %4h %-4h",
             {leading.arg(), leading.arg(), leading.arg(), leading.arg()});
  EXPECT_EQ(leadingStatus, OBELISK_RT_OK);
  EXPECT_EQ(leadingOutput, "0x 0x 000x 0x00");
}

TEST_F(RuntimeTest, FormatsUnknownAndSignedDecimal) {
  LogicValue allX("xxxxxxxx");
  LogicValue allZ("zzzzzzzz");
  LogicValue mixedX("000x0001");
  LogicValue mixedZ("000z0001");
  auto [unknownStatus, unknownOutput] = format(
      "%0d %0d %0d %0d", {allX.arg(), allZ.arg(), mixedX.arg(), mixedZ.arg()});
  EXPECT_EQ(unknownStatus, OBELISK_RT_OK);
  EXPECT_EQ(unknownOutput, "x z X Z");

  LogicValue negativeFiveBits("11101", true);
  LogicValue unsignedFiveBits("00011");
  auto [smallStatus, smallOutput] =
      format("%d|%0d|%d", {negativeFiveBits.arg(), negativeFiveBits.arg(),
                           unsignedFiveBits.arg()});
  EXPECT_EQ(smallStatus, OBELISK_RT_OK);
  EXPECT_EQ(smallOutput, " -3|-3| 3");

  LogicValue sixtyFiveBits(65, {0, 1});
  auto [wideStatus, wideOutput] = format("%0d", {sixtyFiveBits.arg()});
  EXPECT_EQ(wideStatus, OBELISK_RT_OK);
  EXPECT_EQ(wideOutput, "18446744073709551616");
}

TEST_F(RuntimeTest, FormatsArbitraryNonPowerOfTwoWidths) {
  LogicValue oneBit("z");
  EXPECT_EQ(format("%b", {oneBit.arg()}).second, "z");

  std::string symbols = "1" + std::string(126, '0') + "xz0";
  ASSERT_EQ(symbols.size(), 130u);
  LogicValue wide(symbols);
  auto [status, output] = format("%b", {wide.arg()});
  EXPECT_EQ(status, OBELISK_RT_OK);
  EXPECT_EQ(output, symbols);

  LogicValue negativeSixtyFiveBits(65, {~uint64_t{2}, 1}, {},
                                   /*isSigned=*/true);
  LogicValue ignoredPaddingBits(65, {3, ~uint64_t{0}}, {0, ~uint64_t{1}});
  auto [decimalStatus, decimalOutput] = format(
      "%0d %0d", {negativeSixtyFiveBits.arg(), ignoredPaddingBits.arg()});
  EXPECT_EQ(decimalStatus, OBELISK_RT_OK);
  EXPECT_EQ(decimalOutput, "-3 18446744073709551619");
}

TEST_F(RuntimeTest, FormatsStringsRealsTimeAndEnvironment) {
  LogicValue hexValue("00001010");
  double real = 3.25;
  uint64_t time = 10;
  std::string text = "ok";
  std::string scope = "top.worker";
  std::string libraryCell = "work.top";
  std::string suffix = "ns";
  obelisk_rt_format_env_v1 environment{scope.data(),
                                       scope.size(),
                                       libraryCell.data(),
                                       libraryCell.size(),
                                       20,
                                       0,
                                       suffix.data(),
                                       suffix.size(),
                                       100};

  auto [status, output] =
      format("[%4h][%-4h][%0h][%4s][%-4s] %.2f %m %l %0t%%",
             {hexValue.arg(), hexValue.arg(), hexValue.arg(), stringArg(text),
              stringArg(text), realArg(real), timeArg(time)},
             &environment);
  EXPECT_EQ(status, OBELISK_RT_OK);
  EXPECT_EQ(output,
            "[000a][a000][a][  ok][ok  ] 3.25 top.worker work.top 1000ns%");

  uint64_t largestTime = std::numeric_limits<uint64_t>::max();
  auto [largeStatus, largeOutput] =
      format("%0t", {timeArg(largestTime)}, &environment);
  EXPECT_EQ(largeStatus, OBELISK_RT_OK);
  EXPECT_EQ(largeOutput, "1844674407370955161500ns");

  auto [realTimeStatus, realTimeOutput] =
      format("%0t", {realArg(real)}, &environment);
  EXPECT_EQ(realTimeStatus, OBELISK_RT_OK);
  EXPECT_EQ(realTimeOutput, "325ns");
}

TEST_F(RuntimeTest, FormatsRemainingScalarFormsAndEmptyStrings) {
  LogicValue letter("01000001");
  double real = 12.5;
  std::string pattern = "pattern";
  obelisk_rt_arg_v1 emptyString{OBELISK_RT_ARG_STRING, 0, 0, nullptr, nullptr};

  auto [status, output] =
      format("%c|%.1e|%.1E|%.3g|%p|%p|[%s]",
             {letter.arg(), realArg(real), realArg(real), realArg(real),
              realArg(real), stringArg(pattern), emptyString});
  EXPECT_EQ(status, OBELISK_RT_OK);
  EXPECT_EQ(output, "A|1.2e+01|1.2E+01|12.5|12.5|pattern|[]");

  double padded = 3.25;
  auto [paddingStatus, paddingOutput] =
      format("[%08.2f][%-8.2f]", {realArg(padded), realArg(padded)});
  EXPECT_EQ(paddingStatus, OBELISK_RT_OK);
  EXPECT_EQ(paddingOutput, "[00003.25][3.25    ]");

  double positive = 3.7;
  double negative = -3.7;
  auto [integerStatus, integerOutput] =
      format("%0d %0d", {realArg(positive), realArg(negative)});
  EXPECT_EQ(integerStatus, OBELISK_RT_OK);
  EXPECT_EQ(integerOutput, "4 -4");

  uint64_t time = 10;
  auto [timeStatus, timeOutput] = format("[%t]", {timeArg(time)});
  EXPECT_EQ(timeStatus, OBELISK_RT_OK);
  EXPECT_EQ(timeOutput, "[" + std::string(18, ' ') + "10]");
}

TEST_F(RuntimeTest, FormatsPackedStringsAndScalarPatterns) {
  LogicValue packed("010000010000000001000010"); // "A", NUL, "B"
  LogicValue pattern("10xz", true);
  auto [status, output] = format("%s %p", {packed.arg(), pattern.arg()});
  EXPECT_EQ(status, OBELISK_RT_OK);
  EXPECT_EQ(output, "AB 4'sb10xz");
}

TEST_F(RuntimeTest, EmitsRawTwoAndFourStateChunks) {
  LogicValue value("x00000000000000000000000000000001");
  auto [twoStatus, rawTwo] = format("%u", {value.arg()});
  ASSERT_EQ(twoStatus, OBELISK_RT_OK);
  ASSERT_EQ(rawTwo.size(), 8u);
  uint32_t twoWords[2];
  std::memcpy(twoWords, rawTwo.data(), sizeof(twoWords));
  EXPECT_EQ(twoWords[0], 1u);
  EXPECT_EQ(twoWords[1], 0u);

  auto [fourStatus, rawFour] = format("%z", {value.arg()});
  ASSERT_EQ(fourStatus, OBELISK_RT_OK);
  ASSERT_EQ(rawFour.size(), 16u);
  uint32_t fourWords[4];
  std::memcpy(fourWords, rawFour.data(), sizeof(fourWords));
  EXPECT_EQ(fourWords[0], 1u);
  EXPECT_EQ(fourWords[1], 0u);
  EXPECT_EQ(fourWords[2], 1u);
  EXPECT_EQ(fourWords[3], 1u);
}

TEST_F(RuntimeTest, RejectsMalformedFormatsAndArgumentMismatch) {
  LogicValue value("1");
  EXPECT_EQ(format("%q", {value.arg()}).first, OBELISK_RT_FORMAT_ERROR);
  EXPECT_EQ(format("%", {}).first, OBELISK_RT_FORMAT_ERROR);
  EXPECT_EQ(format("%42949672960d", {value.arg()}).first,
            OBELISK_RT_FORMAT_ERROR);
  EXPECT_EQ(format("%.42949672960f", {value.arg()}).first,
            OBELISK_RT_FORMAT_ERROR);
  EXPECT_EQ(format("%.2d", {value.arg()}).first, OBELISK_RT_FORMAT_ERROR);
  EXPECT_EQ(format("%2c", {value.arg()}).first, OBELISK_RT_FORMAT_ERROR);
  EXPECT_EQ(format("%d", {}).first, OBELISK_RT_ARGUMENT_MISMATCH);
  EXPECT_EQ(format("", {value.arg()}).first, OBELISK_RT_ARGUMENT_MISMATCH);
  EXPECT_EQ(format("%s", {value.arg()}).first, OBELISK_RT_OK);
  EXPECT_EQ(format("%f", {value.arg()}).first, OBELISK_RT_OK);

  obelisk_rt_format_env_v1 invalidEnvironment{};
  invalidEnvironment.scope_size = 1;
  EXPECT_EQ(format("%m", {}, &invalidEnvironment).first,
            OBELISK_RT_INVALID_ARGUMENT);
  invalidEnvironment = {};
  invalidEnvironment.time_suffix_size = 1;
  EXPECT_EQ(format("%t", {value.arg()}, &invalidEnvironment).first,
            OBELISK_RT_INVALID_ARGUMENT);

  obelisk_rt_arg_v1 zeroWidth{OBELISK_RT_ARG_LOGIC, 0, 0, nullptr, nullptr};
  EXPECT_EQ(format("%d", {zeroWidth}).first, OBELISK_RT_ARGUMENT_MISMATCH);

  RuntimeBuffer message;
  ASSERT_EQ(obelisk_rt_v1_last_error(context, message.out()), OBELISK_RT_OK);
  EXPECT_FALSE(message.str().empty());
}

TEST_F(RuntimeTest, DisplayHandlesFormatItemsDefaultsAndNewline) {
  TempDirectory temporary;
  uint32_t descriptor = open(temporary.file("display.bin"), "w+b");
  LogicValue value("00001010");
  std::string formatString = "v=%0h ";
  std::string text = "tail";
  std::vector<obelisk_rt_arg_v1> items = {
      stringArg(formatString, OBELISK_RT_ARG_FORMAT_STRING), value.arg(),
      stringArg(text), value.arg()};
  ASSERT_EQ(obelisk_rt_v1_display(context, descriptor, 1,
                                  OBELISK_RT_RADIX_BINARY, items.data(),
                                  items.size(), nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_flush(context, descriptor), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_rewind(context, descriptor), OBELISK_RT_OK);

  char bytes[64]{};
  uint64_t read = 0;
  ASSERT_EQ(
      obelisk_rt_v1_file_read(context, descriptor, bytes, sizeof(bytes), &read),
      OBELISK_RT_OK);
  EXPECT_EQ(std::string(bytes, static_cast<size_t>(read)),
            "v=a tail00001010\n");
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, DisplayValidatesItemsAndHandlesEmptyValues) {
  TempDirectory temporary;
  uint32_t descriptor = open(temporary.file("display-empty.bin"), "w+b");
  double real = 2.5;
  obelisk_rt_arg_v1 empty{OBELISK_RT_ARG_EMPTY, 0, 0, nullptr, nullptr};
  obelisk_rt_arg_v1 emptyString{OBELISK_RT_ARG_STRING, 0, 0, nullptr, nullptr};
  obelisk_rt_arg_v1 emptyFormat{
      OBELISK_RT_ARG_STRING, OBELISK_RT_ARG_FORMAT_STRING, 0, nullptr, nullptr};
  obelisk_rt_arg_v1 invalidFormat{
      OBELISK_RT_ARG_LOGIC, OBELISK_RT_ARG_FORMAT_STRING, 1, nullptr, nullptr};
  obelisk_rt_arg_v1 invalidKind{99, 0, 0, nullptr, nullptr};
  obelisk_rt_arg_v1 invalidString{OBELISK_RT_ARG_STRING, 0, 1, nullptr,
                                  nullptr};
  std::string malformed = "%q";
  obelisk_rt_arg_v1 malformedFormat =
      stringArg(malformed, OBELISK_RT_ARG_FORMAT_STRING);

  EXPECT_EQ(
      obelisk_rt_v1_display(context, descriptor, 0, 3, nullptr, 0, nullptr),
      OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_display(context, descriptor, 0,
                                  OBELISK_RT_RADIX_DECIMAL, &invalidFormat, 1,
                                  nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_display(context, descriptor, 0,
                                  OBELISK_RT_RADIX_DECIMAL, &invalidKind, 1,
                                  nullptr),
            OBELISK_RT_ARGUMENT_MISMATCH);
  EXPECT_EQ(obelisk_rt_v1_display(context, descriptor, 0,
                                  OBELISK_RT_RADIX_DECIMAL, &invalidString, 1,
                                  nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_display(context, descriptor, 0,
                                  OBELISK_RT_RADIX_DECIMAL, &malformedFormat, 1,
                                  nullptr),
            OBELISK_RT_FORMAT_ERROR);

  std::vector<obelisk_rt_arg_v1> items = {empty, realArg(real), emptyString,
                                          emptyFormat};
  ASSERT_EQ(obelisk_rt_v1_display(context, descriptor, 0,
                                  OBELISK_RT_RADIX_DECIMAL, items.data(),
                                  items.size(), nullptr),
            OBELISK_RT_OK);
  LogicValue value("00001010");
  obelisk_rt_arg_v1 logic = value.arg();
  ASSERT_EQ(obelisk_rt_v1_display(context, descriptor, 0,
                                  OBELISK_RT_RADIX_OCTAL, &logic, 1, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_display(context, descriptor, 0, OBELISK_RT_RADIX_HEX,
                                  &logic, 1, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_flush(context, descriptor), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_rewind(context, descriptor), OBELISK_RT_OK);

  char bytes[32]{};
  uint64_t read = 0;
  ASSERT_EQ(
      obelisk_rt_v1_file_read(context, descriptor, bytes, sizeof(bytes), &read),
      OBELISK_RT_OK);
  EXPECT_EQ(std::string(bytes, static_cast<size_t>(read)), " 2.5000000120a");
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, ReadsWritesAndPositionsBinaryFiles) {
  TempDirectory temporary;
  uint32_t descriptor = open(temporary.file("roundtrip.bin"), "w+b");
  const std::string bytes("a\0bc", 4);
  uint64_t written = 0;
  ASSERT_EQ(obelisk_rt_v1_file_write(context, descriptor, nullptr, 0, &written),
            OBELISK_RT_OK);
  EXPECT_EQ(written, 0u);
  ASSERT_EQ(obelisk_rt_v1_file_write(context, descriptor, bytes.data(),
                                     bytes.size(), &written),
            OBELISK_RT_OK);
  EXPECT_EQ(written, bytes.size());

  int64_t offset = -1;
  ASSERT_EQ(obelisk_rt_v1_file_tell(context, descriptor, &offset),
            OBELISK_RT_OK);
  EXPECT_EQ(offset, 4);
  ASSERT_EQ(
      obelisk_rt_v1_file_seek(context, descriptor, -2, OBELISK_RT_SEEK_CUR),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_tell(context, descriptor, &offset),
            OBELISK_RT_OK);
  EXPECT_EQ(offset, 2);
  ASSERT_EQ(obelisk_rt_v1_file_rewind(context, descriptor), OBELISK_RT_OK);

  char result[4]{};
  uint64_t read = 0;
  ASSERT_EQ(obelisk_rt_v1_file_read(context, descriptor, nullptr, 0, &read),
            OBELISK_RT_OK);
  EXPECT_EQ(read, 0u);
  ASSERT_EQ(obelisk_rt_v1_file_read(context, descriptor, result, sizeof(result),
                                    &read),
            OBELISK_RT_OK);
  EXPECT_EQ(read, 4u);
  EXPECT_EQ(std::string(result, sizeof(result)), bytes);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, ReadMemTokenizerPreservesFourStateWordsAndAddresses) {
  TempDirectory temporary;
  std::filesystem::path path = temporary.file("memory.hex");
  {
    std::ofstream output(path);
    output << "aZ /* gap */ @1f // reposition\n10x_1";
  }
  uint32_t descriptor = open(path, "r");
  std::array<uint8_t, 2> value{}, unknown{};
  uint32_t kind = 0;
  uint64_t address = 0;
  ASSERT_EQ(obelisk_rt_v1_file_readmem_token(
                context, descriptor, 16, 13, value.data(), value.size(),
                unknown.data(), unknown.size(), &kind, &address),
            OBELISK_RT_OK);
  EXPECT_EQ(kind, OBELISK_RT_READMEM_DATA);
  EXPECT_EQ(value, (std::array<uint8_t, 2>{0xaf, 0x00}));
  EXPECT_EQ(unknown, (std::array<uint8_t, 2>{0x0f, 0x00}));
  ASSERT_EQ(obelisk_rt_v1_file_readmem_token(
                context, descriptor, 16, 13, value.data(), value.size(),
                unknown.data(), unknown.size(), &kind, &address),
            OBELISK_RT_OK);
  EXPECT_EQ(kind, OBELISK_RT_READMEM_ADDRESS);
  EXPECT_EQ(address, 0x1fu);
  ASSERT_EQ(obelisk_rt_v1_file_readmem_token(
                context, descriptor, 16, 13, value.data(), value.size(),
                unknown.data(), unknown.size(), &kind, &address),
            OBELISK_RT_OK);
  EXPECT_EQ(kind, OBELISK_RT_READMEM_DATA);
  EXPECT_EQ(value, (std::array<uint8_t, 2>{0x01, 0x10}));
  EXPECT_EQ(unknown, (std::array<uint8_t, 2>{0xf0, 0x00}));
  ASSERT_EQ(obelisk_rt_v1_file_readmem_token(
                context, descriptor, 16, 13, value.data(), value.size(),
                unknown.data(), unknown.size(), &kind, &address),
            OBELISK_RT_OK);
  EXPECT_EQ(kind, OBELISK_RT_READMEM_EOF);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, ReadMemTokenizerRejectsMalformedInput) {
  TempDirectory temporary;
  std::filesystem::path path = temporary.file("bad.hex");
  { std::ofstream(path) << "@"; }
  uint32_t descriptor = open(path, "r");
  uint8_t value = 0, unknown = 0;
  uint32_t kind = 0;
  uint64_t address = 0;
  EXPECT_EQ(obelisk_rt_v1_file_readmem_token(
                context, descriptor, 16, 8, &value, 1, &unknown, 1, &kind,
                &address),
            OBELISK_RT_FORMAT_ERROR);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, ReadsBytesLinesAndReportsEOF) {
  TempDirectory temporary;
  std::filesystem::path path = temporary.file("lines.bin");
  {
    std::ofstream output(path, std::ios::binary);
    output.write("a\0b\nlast", 8);
  }
  uint32_t descriptor = open(path, "rb");

  uint8_t byte = 0;
  ASSERT_EQ(obelisk_rt_v1_file_getc(context, descriptor, &byte), OBELISK_RT_OK);
  EXPECT_EQ(byte, 'a');
  ASSERT_EQ(obelisk_rt_v1_file_ungetc(context, descriptor, byte),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_getc(context, descriptor, &byte), OBELISK_RT_OK);
  EXPECT_EQ(byte, 'a');

  RuntimeBuffer firstLine;
  ASSERT_EQ(
      obelisk_rt_v1_file_getline(context, descriptor, 64, firstLine.out()),
      OBELISK_RT_OK);
  EXPECT_EQ(firstLine.str(), std::string("\0b\n", 3));
  RuntimeBuffer secondLine;
  ASSERT_EQ(
      obelisk_rt_v1_file_getline(context, descriptor, 64, secondLine.out()),
      OBELISK_RT_OK);
  EXPECT_EQ(secondLine.str(), "last");
  RuntimeBuffer eofLine;
  EXPECT_EQ(obelisk_rt_v1_file_getline(context, descriptor, 64, eofLine.out()),
            OBELISK_RT_EOF);
  uint32_t isEOF = 0;
  ASSERT_EQ(obelisk_rt_v1_file_eof(context, descriptor, &isEOF), OBELISK_RT_OK);
  EXPECT_EQ(isEOF, 1u);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, ReadsWithoutReadAccessReportEndOfFileInsteadOfIOError) {
  TempDirectory temporary;
  std::filesystem::path path = temporary.file("write-only.txt");
  uint32_t descriptor = open(path, "w");

  // The host refuses these reads with EBADF. Reporting that as an I/O error
  // would fail the whole simulation instead of the individual system call.
  uint8_t byte = 0;
  EXPECT_EQ(obelisk_rt_v1_file_getc(context, descriptor, &byte),
            OBELISK_RT_EOF);
  char bytes[4] = {};
  uint64_t read = 1;
  ASSERT_EQ(obelisk_rt_v1_file_read(context, descriptor, bytes, sizeof(bytes),
                                    &read),
            OBELISK_RT_OK);
  EXPECT_EQ(read, 0u);
  RuntimeBuffer line;
  EXPECT_EQ(obelisk_rt_v1_file_getline(context, descriptor, 64, line.out()),
            OBELISK_RT_EOF);
  uint32_t isEOF = 0;
  ASSERT_EQ(obelisk_rt_v1_file_eof(context, descriptor, &isEOF),
            OBELISK_RT_OK);
  EXPECT_EQ(isEOF, 1u);

  obelisk_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(context, &lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);
  obelisk_rt_string_v1 field = 1;
  uint32_t ok = 1;
  uint32_t scanEOF = 0;
  EXPECT_EQ(obelisk_rt_v1_file_scan_field(context, lane, descriptor, 1, nullptr,
                                          0, 'd', &field, &ok, &scanEOF),
            OBELISK_RT_OK);
  EXPECT_EQ(field, 0u);
  EXPECT_EQ(ok, 0u);
  EXPECT_EQ(scanEOF, 1u);
  EXPECT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);

  // The descriptor keeps the access it was opened with.
  uint64_t written = 0;
  ASSERT_EQ(obelisk_rt_v1_file_write(context, descriptor, "kept", 4, &written),
            OBELISK_RT_OK);
  EXPECT_EQ(written, 4u);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);

  EXPECT_EQ(readHostFile(path), "kept");
}

TEST_F(RuntimeTest, HoldsOnePushedBackByteWithoutReadAccess) {
  TempDirectory temporary;
  std::filesystem::path path = temporary.file("pushback.txt");
  uint32_t descriptor = open(path, "w");

  // The runtime holds the byte itself: glibc accepts ungetc() on a write-only
  // stream and then crashes on the next write through it.
  ASSERT_EQ(obelisk_rt_v1_file_ungetc(context, descriptor, 'z'),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_file_ungetc(context, descriptor, 'y'),
            OBELISK_RT_EOF);
  uint32_t isEOF = 1;
  ASSERT_EQ(obelisk_rt_v1_file_eof(context, descriptor, &isEOF),
            OBELISK_RT_OK);
  EXPECT_EQ(isEOF, 0u);

  uint8_t byte = 0;
  ASSERT_EQ(obelisk_rt_v1_file_getc(context, descriptor, &byte),
            OBELISK_RT_OK);
  EXPECT_EQ(byte, 'z');
  EXPECT_EQ(obelisk_rt_v1_file_getc(context, descriptor, &byte),
            OBELISK_RT_EOF);
  ASSERT_EQ(obelisk_rt_v1_file_eof(context, descriptor, &isEOF),
            OBELISK_RT_OK);
  EXPECT_EQ(isEOF, 1u);

  uint64_t written = 0;
  ASSERT_EQ(obelisk_rt_v1_file_write(context, descriptor, "after", 5, &written),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);

  EXPECT_EQ(readHostFile(path), "after");
}

TEST_F(RuntimeTest, BoundsPackedLineReadsWithoutDiscardingRemainingBytes) {
  TempDirectory temporary;
  uint32_t descriptor = open(temporary.file("bounded-line.txt"), "w+");
  uint64_t written = 0;
  ASSERT_EQ(
      obelisk_rt_v1_file_write(context, descriptor, "abcdef\n", 7, &written),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_rewind(context, descriptor), OBELISK_RT_OK);

  RuntimeBuffer prefix;
  ASSERT_EQ(obelisk_rt_v1_file_getline(context, descriptor, 3, prefix.out()),
            OBELISK_RT_OK);
  EXPECT_EQ(prefix.str(), "abc");

  RuntimeBuffer remainder;
  ASSERT_EQ(
      obelisk_rt_v1_file_getline(context, descriptor, 64, remainder.out()),
      OBELISK_RT_OK);
  EXPECT_EQ(remainder.str(), "def\n");
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, ReservesPredefinedFileDescriptorValues) {
  TempDirectory temporary;
  uint32_t descriptor = open(temporary.file("descriptor.txt"), "w");
  EXPECT_EQ(descriptor, 0x80000003u);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);

  uint32_t reused = open(temporary.file("descriptor-reused.txt"), "w");
  EXPECT_EQ(reused, 0x80000003u);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, reused), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, FlushesAllStreamsAndReportsByteEOF) {
  TempDirectory temporary;
  std::string mcdPath = temporary.file("flush-mcd.txt").string();
  uint32_t mcd = 0;
  ASSERT_EQ(obelisk_rt_v1_file_open_mcd(context, mcdPath.data(), mcdPath.size(),
                                        &mcd),
            OBELISK_RT_OK);
  uint32_t descriptor = open(temporary.file("flush-file.txt"), "w");
  uint64_t written = 0;
  ASSERT_EQ(obelisk_rt_v1_file_write(context, mcd, "mcd", 3, &written),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_write(context, descriptor, "file", 4, &written),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_file_flush(context, 0), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_file_flush(context, 1), OBELISK_RT_OK);
  EXPECT_EQ(readHostFile(mcdPath), "mcd");
  EXPECT_EQ(readHostFile(temporary.file("flush-file.txt")), "file");
  EXPECT_EQ(obelisk_rt_v1_file_close(context, mcd), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);

  uint32_t empty = open(temporary.file("empty.txt"), "w+b");
  uint8_t byte = 0xff;
  EXPECT_EQ(obelisk_rt_v1_file_getc(context, empty, &byte), OBELISK_RT_EOF);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, empty), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, RejectsInvalidFileArgumentsAndOpenFailures) {
  TempDirectory temporary;
  std::string missing = temporary.file("missing/child.txt").string();
  uint32_t descriptor = 123;
  EXPECT_EQ(obelisk_rt_v1_file_open(context, missing.data(), missing.size(),
                                    "r", 1, &descriptor),
            OBELISK_RT_IO_ERROR);
  EXPECT_EQ(descriptor, 0u);
  descriptor = 123;
  EXPECT_EQ(obelisk_rt_v1_file_open_mcd(context, missing.data(), missing.size(),
                                        &descriptor),
            OBELISK_RT_IO_ERROR);
  EXPECT_EQ(descriptor, 0u);

  constexpr uint32_t invalidDescriptor = 0x8000ffffu;
  char byte = 0;
  uint8_t unsignedByte = 0;
  uint64_t count = 0;
  uint32_t eof = 0;
  int32_t errorCode = 0;
  int64_t offset = 0;
  RuntimeBuffer line;
  RuntimeBuffer error;
  EXPECT_EQ(
      obelisk_rt_v1_file_write(context, invalidDescriptor, &byte, 1, &count),
      OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(
      obelisk_rt_v1_file_read(context, invalidDescriptor, &byte, 1, &count),
      OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_file_getc(context, invalidDescriptor, &unsignedByte),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_file_ungetc(context, invalidDescriptor, 'x'),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(
      obelisk_rt_v1_file_getline(context, invalidDescriptor, 64, line.out()),
      OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_file_eof(context, invalidDescriptor, &eof),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_file_error(context, invalidDescriptor, &errorCode,
                                     error.out()),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_file_seek(context, invalidDescriptor, 0,
                                    OBELISK_RT_SEEK_SET),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_file_tell(context, invalidDescriptor, &offset),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_file_flush(context, invalidDescriptor),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, invalidDescriptor),
            OBELISK_RT_INVALID_HANDLE);

  uint32_t valid = open(temporary.file("seek.txt"), "w+");
  EXPECT_EQ(obelisk_rt_v1_file_seek(context, valid, 0, 99),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_write(context, valid, nullptr, 1, &count),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_read(context, valid, nullptr, 1, &count),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, valid), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, SupportsAppendAndUpdateModes) {
  TempDirectory temporary;
  std::filesystem::path path = temporary.file("append.txt");
  uint32_t first = open(path, "w");
  uint64_t count = 0;
  ASSERT_EQ(obelisk_rt_v1_file_write(context, first, "one", 3, &count),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_close(context, first), OBELISK_RT_OK);

  uint32_t append = open(path, "a+b");
  ASSERT_EQ(obelisk_rt_v1_file_write(context, append, "two", 3, &count),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_rewind(context, append), OBELISK_RT_OK);
  char bytes[6]{};
  uint64_t read = 0;
  ASSERT_EQ(
      obelisk_rt_v1_file_read(context, append, bytes, sizeof(bytes), &read),
      OBELISK_RT_OK);
  EXPECT_EQ(std::string(bytes, static_cast<size_t>(read)), "onetwo");
  EXPECT_EQ(obelisk_rt_v1_file_close(context, append), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, AcceptsEveryStandardFileMode) {
  static constexpr std::string_view modes[] = {
      "r",  "w",   "a",   "r+",  "w+",  "a+",  "rb", "wb",
      "ab", "r+b", "w+b", "a+b", "rb+", "wb+", "ab+"};
  TempDirectory temporary;
  for (size_t index = 0; index < std::size(modes); ++index) {
    std::filesystem::path path =
        temporary.file("mode-" + std::to_string(index));
    if (modes[index].front() == 'r') {
      std::ofstream seed(path, std::ios::binary);
      seed << "seed";
    }
    SCOPED_TRACE(modes[index]);
    uint32_t descriptor = open(path, modes[index]);
    bool readable = modes[index].front() == 'r' ||
                    modes[index].find('+') != std::string_view::npos;
    bool writable = modes[index].front() != 'r' ||
                    modes[index].find('+') != std::string_view::npos;
    if (writable) {
      uint64_t written = 0;
      EXPECT_EQ(obelisk_rt_v1_file_write(context, descriptor, "x", 1, &written),
                OBELISK_RT_OK);
      EXPECT_EQ(written, 1u);
    }
    if (readable) {
      ASSERT_EQ(obelisk_rt_v1_file_rewind(context, descriptor), OBELISK_RT_OK);
      char byte = 0;
      uint64_t read = 0;
      EXPECT_EQ(obelisk_rt_v1_file_read(context, descriptor, &byte, 1, &read),
                OBELISK_RT_OK);
      EXPECT_EQ(read, 1u);
      EXPECT_EQ(byte, writable ? 'x' : 's');
    }
    EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);
  }
}

TEST_F(RuntimeTest, FansOutMultichannelDescriptors) {
  TempDirectory temporary;
  std::string firstPath = temporary.file("first.txt").string();
  std::string secondPath = temporary.file("second.txt").string();
  uint32_t first = 0;
  uint32_t second = 0;
  ASSERT_EQ(obelisk_rt_v1_file_open_mcd(context, firstPath.data(),
                                        firstPath.size(), &first),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_open_mcd(context, secondPath.data(),
                                        secondPath.size(), &second),
            OBELISK_RT_OK);
  EXPECT_EQ(first & second, 0u);
  uint64_t written = 0;
  ASSERT_EQ(
      obelisk_rt_v1_file_write(context, first | second, "fanout", 6, &written),
      OBELISK_RT_OK);
  EXPECT_EQ(written, 6u);
  ASSERT_EQ(obelisk_rt_v1_file_close(context, first | second), OBELISK_RT_OK);
  EXPECT_EQ(readHostFile(firstPath), "fanout");
  EXPECT_EQ(readHostFile(secondPath), "fanout");
}

TEST_F(RuntimeTest, ReportsMCDExhaustionAndReusesChannels) {
  TempDirectory temporary;
  uint32_t combined = 0;
  std::string highestPath;
  for (uint32_t index = 0; index < 30; ++index) {
    std::string path = temporary.file("mcd-" + std::to_string(index)).string();
    uint32_t descriptor = 0;
    ASSERT_EQ(obelisk_rt_v1_file_open_mcd(context, path.data(), path.size(),
                                          &descriptor),
              OBELISK_RT_OK);
    EXPECT_EQ(combined & descriptor, 0u);
    combined |= descriptor;
    if (descriptor == (uint32_t{1} << 30))
      highestPath = path;
  }
  EXPECT_EQ(combined, 0x7ffffffeu);
  ASSERT_FALSE(highestPath.empty());
  obelisk_rt_arg_v1 item = stringArg("highest");
  ASSERT_EQ(obelisk_rt_v1_display(context, uint32_t{1} << 30, 0,
                                  OBELISK_RT_RADIX_DECIMAL, &item, 1, nullptr),
            OBELISK_RT_OK);
  std::string overflowPath = temporary.file("overflow").string();
  uint32_t overflow = 123;
  EXPECT_EQ(obelisk_rt_v1_file_open_mcd(context, overflowPath.data(),
                                        overflowPath.size(), &overflow),
            OBELISK_RT_OUT_OF_RESOURCES);
  EXPECT_EQ(overflow, 0u);
  ASSERT_EQ(obelisk_rt_v1_file_close(context, combined), OBELISK_RT_OK);
  EXPECT_EQ(readHostFile(highestPath), "highest");

  uint32_t reused = 0;
  ASSERT_EQ(obelisk_rt_v1_file_open_mcd(context, overflowPath.data(),
                                        overflowPath.size(), &reused),
            OBELISK_RT_OK);
  EXPECT_NE(reused, 0u);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, reused), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, ValidatesModesHandlesAndFileErrors) {
  TempDirectory temporary;
  std::string path = temporary.file("errors.txt").string();
  uint32_t descriptor = 99;
  EXPECT_EQ(obelisk_rt_v1_file_open(context, path.data(), path.size(), "bad", 3,
                                    &descriptor),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(descriptor, 0u);

  const char pathWithNul[] = {'b', 'a', 'd', '\0', 'p', 'a', 't', 'h'};
  EXPECT_EQ(obelisk_rt_v1_file_open(context, pathWithNul, sizeof(pathWithNul),
                                    "w", 1, &descriptor),
            OBELISK_RT_INVALID_ARGUMENT);

  uint32_t readOnly = open(temporary.file("missing.txt"), "w+");
  EXPECT_EQ(obelisk_rt_v1_file_close(context, readOnly), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, readOnly),
            OBELISK_RT_INVALID_HANDLE);

  readOnly = open(temporary.file("missing.txt"), "r");
  uint64_t written = 0;
  EXPECT_EQ(obelisk_rt_v1_file_write(context, readOnly, "x", 1, &written),
            OBELISK_RT_IO_ERROR);
  int32_t errorCode = 0;
  RuntimeBuffer errorMessage;
  ASSERT_EQ(obelisk_rt_v1_file_error(context, readOnly, &errorCode,
                                     errorMessage.out()),
            OBELISK_RT_OK);
  EXPECT_NE(errorCode, 0);
  EXPECT_FALSE(errorMessage.str().empty());
  EXPECT_EQ(obelisk_rt_v1_file_close(context, readOnly), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, ReusesDescriptorsAndClosesOwnedFilesOnDestroy) {
  TempDirectory temporary;
  std::filesystem::path firstPath = temporary.file("first.txt");
  uint32_t first = open(firstPath, "w");
  ASSERT_EQ(obelisk_rt_v1_file_close(context, first), OBELISK_RT_OK);
  uint32_t reused = open(temporary.file("second.txt"), "w");
  EXPECT_EQ(first, reused);

  uint64_t written = 0;
  ASSERT_EQ(obelisk_rt_v1_file_write(context, reused, "saved", 5, &written),
            OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
  context = nullptr;
  EXPECT_EQ(readHostFile(temporary.file("second.txt")), "saved");
}

TEST_F(RuntimeTest, KeepsLastErrorsIsolatedPerThread) {
  static constexpr std::array<std::string_view, 4> formats = {"%q", "x%q",
                                                              "xx%q", "xxx%q"};
  std::array<obelisk_rt_status, formats.size()> formatStatuses{};
  std::array<obelisk_rt_status, formats.size()> errorStatuses{};
  std::array<std::string, formats.size()> messages;
  std::atomic<size_t> ready{0};
  std::vector<std::thread> threads;

  for (size_t index = 0; index < formats.size(); ++index) {
    threads.emplace_back([&, index] {
      formatStatuses[index] = format(formats[index], {}).first;
      ready.fetch_add(1);
      while (ready.load() != formats.size())
        std::this_thread::yield();
      RuntimeBuffer message;
      errorStatuses[index] = obelisk_rt_v1_last_error(context, message.out());
      messages[index] = message.str();
    });
  }
  for (std::thread &thread : threads)
    thread.join();

  for (size_t index = 0; index < formats.size(); ++index) {
    EXPECT_EQ(formatStatuses[index], OBELISK_RT_FORMAT_ERROR);
    EXPECT_EQ(errorStatuses[index], OBELISK_RT_OK);
    EXPECT_EQ(messages[index],
              "unknown format specifier at byte " + std::to_string(index));
  }
}

TEST_F(RuntimeTest, DoesNotReuseExitedThreadsLastError) {
  std::thread::id errorThread;
  std::thread producer([&] {
    errorThread = std::this_thread::get_id();
    EXPECT_EQ(format("%q", {}).first, OBELISK_RT_FORMAT_ERROR);
  });
  producer.join();

  for (unsigned attempt = 0; attempt != 64; ++attempt) {
    std::thread::id readerThread;
    obelisk_rt_status status = OBELISK_RT_IO_ERROR;
    std::string message;
    std::thread reader([&] {
      readerThread = std::this_thread::get_id();
      RuntimeBuffer buffer;
      status = obelisk_rt_v1_last_error(context, buffer.out());
      message = buffer.str();
    });
    reader.join();
    if (readerThread == errorThread) {
      EXPECT_EQ(status, OBELISK_RT_OK);
      EXPECT_TRUE(message.empty());
      return;
    }
  }
  GTEST_SKIP() << "host did not reuse a thread ID";
}

TEST_F(RuntimeTest, SerializesConcurrentWholeMessageWrites) {
  TempDirectory temporary;
  uint32_t descriptor = open(temporary.file("threads.txt"), "w+");
  constexpr int threadCount = 4;
  constexpr int writesPerThread = 50;
  std::vector<std::thread> threads;
  for (int thread = 0; thread < threadCount; ++thread) {
    threads.emplace_back([&, thread] {
      std::string line = "thread-" + std::to_string(thread) + "\n";
      for (int write = 0; write < writesPerThread; ++write) {
        uint64_t count = 0;
        EXPECT_EQ(obelisk_rt_v1_file_write(context, descriptor, line.data(),
                                           line.size(), &count),
                  OBELISK_RT_OK);
        EXPECT_EQ(count, line.size());
      }
    });
  }
  for (std::thread &thread : threads)
    thread.join();
  ASSERT_EQ(obelisk_rt_v1_file_flush(context, descriptor), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_rewind(context, descriptor), OBELISK_RT_OK);

  std::string contents(threadCount * writesPerThread * 9, '\0');
  uint64_t read = 0;
  ASSERT_EQ(obelisk_rt_v1_file_read(context, descriptor, contents.data(),
                                    contents.size(), &read),
            OBELISK_RT_OK);
  contents.resize(static_cast<size_t>(read));
  std::array<int, threadCount> counts{};
  size_t position = 0;
  while (position < contents.size()) {
    size_t newline = contents.find('\n', position);
    ASSERT_NE(newline, std::string::npos);
    std::string_view line(contents.data() + position, newline - position);
    ASSERT_EQ(line.size(), std::string_view("thread-0").size());
    ASSERT_TRUE(line.substr(0, 7) == "thread-");
    ASSERT_GE(line.back(), '0');
    ASSERT_LT(line.back(), static_cast<char>('0' + threadCount));
    ++counts[static_cast<size_t>(line.back() - '0')];
    position = newline + 1;
  }
  EXPECT_EQ(position, contents.size());
  for (int count : counts)
    EXPECT_EQ(count, writesPerThread);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);
}

TEST(RuntimeFragmentTest, ExecutesTypedBytecodeThroughSharedABI) {
  std::vector<uint8_t> code;
  appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_U64, 0, 0, 0,
                    19);
  appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_U64, 1, 0, 0,
                    23);
  appendInstruction(code, OBELISK_RT_BC_ADD, OBELISK_RT_BC_TYPE_U64, 2, 0, 1);
  appendInstruction(code, OBELISK_RT_BC_STORE_FRAME, OBELISK_RT_BC_TYPE_U64, 0,
                    2, 0, 8);
  appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_U64, 3, 0, 0,
                    1234);
  appendInstruction(code, OBELISK_RT_BC_SUSPEND, OBELISK_RT_BC_TYPE_NONE, 0,
                    OBELISK_RT_SUSPEND_DELAY, 3, 0x89abcdefu);
  auto descriptor = bytecodeDescriptor(code, 4);
  std::array<uint64_t, 2> frame{};
  obelisk_rt_fragment_action_v1 action{};

  EXPECT_EQ(
      executeBytecode(descriptor, frame.data(), sizeof(frame), 0, &action),
      OBELISK_RT_OK);
  EXPECT_EQ(frame[1], 42u);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_SUSPEND);
  EXPECT_EQ(action.suspend_kind, OBELISK_RT_SUSPEND_DELAY);
  EXPECT_EQ(action.continuation, 0x89abcdefu);
  EXPECT_EQ(action.payload, 1234u);
}

TEST(RuntimeFragmentTest, ExecutesArithmeticComparisonAndControlOpcodes) {
  std::vector<uint8_t> code;
  appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_U64, 0, 0, 0,
                    7);
  appendInstruction(code, OBELISK_RT_BC_MOVE, OBELISK_RT_BC_TYPE_U64, 1, 0);
  appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_U64, 2, 0, 0,
                    3);
  appendInstruction(code, OBELISK_RT_BC_SUB, OBELISK_RT_BC_TYPE_U64, 3, 1, 2);
  appendInstruction(code, OBELISK_RT_BC_MUL, OBELISK_RT_BC_TYPE_U64, 4, 3, 2);
  appendInstruction(code, OBELISK_RT_BC_AND, OBELISK_RT_BC_TYPE_U64, 5, 4, 1);
  appendInstruction(code, OBELISK_RT_BC_OR, OBELISK_RT_BC_TYPE_U64, 6, 5, 2);
  appendInstruction(code, OBELISK_RT_BC_XOR, OBELISK_RT_BC_TYPE_U64, 7, 6, 2);
  appendInstruction(code, OBELISK_RT_BC_NOT, OBELISK_RT_BC_TYPE_U64, 8, 7);
  appendInstruction(code, OBELISK_RT_BC_EQ, OBELISK_RT_BC_TYPE_U64, 9, 7, 3);
  appendInstruction(code, OBELISK_RT_BC_ULT, OBELISK_RT_BC_TYPE_U64, 10, 2, 0);
  appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_I64, 11, 0, 0,
                    UINT64_MAX);
  appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_I64, 12, 0, 0,
                    0);
  appendInstruction(code, OBELISK_RT_BC_SLT, OBELISK_RT_BC_TYPE_I64, 13, 11,
                    12);
  appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_BOOL, 14, 0,
                    0, 0);
  appendInstruction(code, OBELISK_RT_BC_BRANCH_ZERO, OBELISK_RT_BC_TYPE_BOOL, 0,
                    14, 0, 17);
  appendInstruction(code, OBELISK_RT_BC_TERMINATE);
  appendInstruction(code, OBELISK_RT_BC_JUMP, OBELISK_RT_BC_TYPE_NONE, 0, 0, 0,
                    19);
  appendInstruction(code, OBELISK_RT_BC_TERMINATE);
  appendInstruction(code, OBELISK_RT_BC_NOP);
  appendInstruction(code, OBELISK_RT_BC_STORE_FRAME, OBELISK_RT_BC_TYPE_U64, 0,
                    3, 0, 0);
  appendInstruction(code, OBELISK_RT_BC_STORE_FRAME, OBELISK_RT_BC_TYPE_U64, 0,
                    8, 0, 8);
  appendInstruction(code, OBELISK_RT_BC_STORE_FRAME, OBELISK_RT_BC_TYPE_BOOL, 0,
                    9, 0, 16);
  appendInstruction(code, OBELISK_RT_BC_STORE_FRAME, OBELISK_RT_BC_TYPE_BOOL, 0,
                    10, 0, 24);
  appendInstruction(code, OBELISK_RT_BC_STORE_FRAME, OBELISK_RT_BC_TYPE_BOOL, 0,
                    13, 0, 32);
  appendInstruction(code, OBELISK_RT_BC_TERMINATE, OBELISK_RT_BC_TYPE_NONE, 0,
                    0, 0, 99);
  auto descriptor = bytecodeDescriptor(code, 15);
  std::array<uint64_t, 5> frame{};
  obelisk_rt_fragment_action_v1 action{};

  ASSERT_EQ(
      executeBytecode(descriptor, frame.data(), sizeof(frame), 0, &action),
      OBELISK_RT_OK);
  EXPECT_EQ(frame[0], 4u);
  EXPECT_EQ(frame[1], ~uint64_t{4});
  EXPECT_EQ(frame[2], 1u);
  EXPECT_EQ(frame[3], 1u);
  EXPECT_EQ(frame[4], 1u);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  EXPECT_EQ(action.payload, 99u);
}

TEST(RuntimeFragmentTest, BoundsRunawayBytecodeOnlyWhenAskedTo) {
  // A backward jump to itself is well-formed bytecode, so validation cannot
  // reject it. Only an explicit budget turns it into a reported failure.
  std::vector<uint8_t> code;
  appendInstruction(code, OBELISK_RT_BC_JUMP, OBELISK_RT_BC_TYPE_NONE, 0, 0, 0,
                    0);
  auto descriptor = bytecodeDescriptor(code, 0);
  obelisk_rt_fragment_action_v1 action{};
  EXPECT_EQ(executeBytecode(descriptor, nullptr, 0, 0, &action, 1000),
            OBELISK_RT_STEP_LIMIT);

  // A terminating fragment is unaffected by a budget it never reaches.
  std::vector<uint8_t> terminating;
  appendInstruction(terminating, OBELISK_RT_BC_TERMINATE);
  auto bounded = bytecodeDescriptor(terminating, 0);
  EXPECT_EQ(executeBytecode(bounded, nullptr, 0, 0, &action, 1), OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
}

TEST(RuntimeFragmentTest, ContinuationSelectsBytecodeEntryInstruction) {
  std::vector<uint8_t> code;
  appendInstruction(code, OBELISK_RT_BC_TERMINATE, OBELISK_RT_BC_TYPE_NONE, 0,
                    0, 0, 1);
  appendInstruction(code, OBELISK_RT_BC_CONTINUE, OBELISK_RT_BC_TYPE_NONE, 0, 0,
                    0, 17);
  auto descriptor = bytecodeDescriptor(code, 0);
  constexpr obelisk_rt_bytecode_entry_v1 entries[] = {{3, 0}, {11, 1}};
  descriptor.code.bytecode.entries = entries;
  descriptor.code.bytecode.entry_count = std::size(entries);
  obelisk_rt_bytecode_validation_v1 validation{};
  descriptor.code.bytecode.validation = &validation;
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(executeBytecode(descriptor, nullptr, 0, 11, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_CONTINUE);
  EXPECT_EQ(action.continuation, 17u);
  EXPECT_NE(validation.state, 0u);
}

TEST(RuntimeFragmentTest, ValidationRecordSupportsConcurrentDispatch) {
  std::vector<uint8_t> code;
  appendInstruction(code, OBELISK_RT_BC_TERMINATE);
  auto descriptor = bytecodeDescriptor(code, 0);
  obelisk_rt_bytecode_validation_v1 validation{};
  descriptor.code.bytecode.validation = &validation;
  std::atomic<unsigned> failures{0};
  std::vector<std::thread> threads;
  for (unsigned index = 0; index != 16; ++index)
    threads.emplace_back([&] {
      obelisk_rt_fragment_action_v1 action{};
      if (executeBytecode(descriptor, nullptr, 0, 0, &action) !=
              OBELISK_RT_OK ||
          action.kind != OBELISK_RT_FRAGMENT_TERMINATE)
        ++failures;
    });
  for (std::thread &thread : threads)
    thread.join();
  EXPECT_EQ(failures.load(), 0u);
  EXPECT_EQ(validation.state, OBELISK_RT_BC_VALIDATION_VALID);
}

TEST(RuntimeFragmentTest, RejectsMalformedBytecodeAndFrameAccess) {
  auto rejects = [](const std::vector<uint8_t> &code, uint32_t registers) {
    auto descriptor = bytecodeDescriptor(code, registers);
    obelisk_rt_fragment_action_v1 action{};
    return executeBytecode(descriptor, nullptr, 0, 0, &action);
  };
  std::vector<uint8_t> empty;
  EXPECT_EQ(rejects(empty, 0), OBELISK_RT_INVALID_BYTECODE);
  std::vector<uint8_t> truncated(3, 0);
  auto malformed = bytecodeDescriptor(truncated, 1);
  obelisk_rt_fragment_action_v1 action{};
  EXPECT_EQ(executeBytecode(malformed, nullptr, 0, 0, &action),
            OBELISK_RT_INVALID_BYTECODE);

  std::vector<uint8_t> code;
  appendInstruction(code, OBELISK_RT_BC_LOAD_FRAME, OBELISK_RT_BC_TYPE_U64, 0,
                    0, 0, 8);
  appendInstruction(code, OBELISK_RT_BC_TERMINATE);
  auto badFrame = bytecodeDescriptor(code, 1);
  uint64_t frame = 0;
  EXPECT_EQ(executeBytecode(badFrame, &frame, sizeof(frame), 0, &action),
            OBELISK_RT_INVALID_BYTECODE);

  std::vector<uint8_t> invalidBool;
  appendInstruction(invalidBool, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_BOOL,
                    0, 0, 0, 2);
  appendInstruction(invalidBool, OBELISK_RT_BC_TERMINATE);
  auto badBool = bytecodeDescriptor(invalidBool, 1);
  EXPECT_EQ(executeBytecode(badBool, nullptr, 0, 0, &action),
            OBELISK_RT_INVALID_BYTECODE);
  EXPECT_STREQ(obelisk_rt_v1_status_string(OBELISK_RT_INVALID_BYTECODE),
               "invalid bytecode");

  std::vector<uint8_t> noneStore;
  appendInstruction(noneStore, OBELISK_RT_BC_STORE_FRAME,
                    OBELISK_RT_BC_TYPE_NONE, 0, 0);
  auto badStore = bytecodeDescriptor(noneStore, 1);
  EXPECT_EQ(executeBytecode(badStore, &frame, sizeof(frame), 0, &action),
            OBELISK_RT_INVALID_BYTECODE);

  std::vector<uint8_t> noneBranch;
  appendInstruction(noneBranch, OBELISK_RT_BC_BRANCH_ZERO,
                    OBELISK_RT_BC_TYPE_NONE, 0, 0, 0, 0);
  auto badBranch = bytecodeDescriptor(noneBranch, 1);
  EXPECT_EQ(executeBytecode(badBranch, nullptr, 0, 0, &action),
            OBELISK_RT_INVALID_BYTECODE);

  std::vector<uint8_t> invalidOpcode;
  appendInstruction(invalidOpcode, 0xff);
  EXPECT_EQ(rejects(invalidOpcode, 0), OBELISK_RT_INVALID_BYTECODE);
  std::vector<uint8_t> invalidType;
  appendInstruction(invalidType, OBELISK_RT_BC_CONST, 0xff, 0);
  EXPECT_EQ(rejects(invalidType, 1), OBELISK_RT_INVALID_BYTECODE);
  std::vector<uint8_t> uninitializedMove;
  appendInstruction(uninitializedMove, OBELISK_RT_BC_MOVE,
                    OBELISK_RT_BC_TYPE_U64, 0, 0);
  EXPECT_EQ(rejects(uninitializedMove, 1), OBELISK_RT_INVALID_BYTECODE);
  std::vector<uint8_t> invalidJump;
  appendInstruction(invalidJump, OBELISK_RT_BC_JUMP, OBELISK_RT_BC_TYPE_NONE, 0,
                    0, 0, 1);
  EXPECT_EQ(rejects(invalidJump, 0), OBELISK_RT_INVALID_BYTECODE);
  std::vector<uint8_t> unterminated;
  appendInstruction(unterminated, OBELISK_RT_BC_NOP);
  EXPECT_EQ(rejects(unterminated, 0), OBELISK_RT_INVALID_BYTECODE);

  std::vector<uint8_t> terminate;
  appendInstruction(terminate, OBELISK_RT_BC_TERMINATE);
  auto badEntries = bytecodeDescriptor(terminate, 0);
  constexpr obelisk_rt_bytecode_entry_v1 duplicateEntries[] = {{0, 0}, {0, 0}};
  badEntries.code.bytecode.entries = duplicateEntries;
  badEntries.code.bytecode.entry_count = std::size(duplicateEntries);
  EXPECT_EQ(executeBytecode(badEntries, nullptr, 0, 0, &action),
            OBELISK_RT_INVALID_BYTECODE);
  constexpr obelisk_rt_bytecode_entry_v1 outOfRangeEntry{0, 1};
  badEntries.code.bytecode.entries = &outOfRangeEntry;
  badEntries.code.bytecode.entry_count = 1;
  EXPECT_EQ(executeBytecode(badEntries, nullptr, 0, 0, &action),
            OBELISK_RT_INVALID_BYTECODE);
  auto missingEntry = bytecodeDescriptor(terminate, 0);
  EXPECT_EQ(executeBytecode(missingEntry, nullptr, 0, 7, &action),
            OBELISK_RT_INVALID_BYTECODE);

  std::vector<uint8_t> fourInstructions;
  for (unsigned index = 0; index != 4; ++index)
    appendInstruction(fourInstructions, OBELISK_RT_BC_TERMINATE);
  auto unsorted = bytecodeDescriptor(fourInstructions, 0);
  constexpr obelisk_rt_bytecode_entry_v1 unsortedEntries[] = {
      {0, 0}, {100, 1}, {50, 2}, {200, 3}};
  unsorted.code.bytecode.entries = unsortedEntries;
  unsorted.code.bytecode.entry_count = std::size(unsortedEntries);
  EXPECT_EQ(executeBytecode(unsorted, nullptr, 0, 0, &action),
            OBELISK_RT_INVALID_BYTECODE);

  auto remoteBadInstruction = bytecodeDescriptor(terminate, 0);
  constexpr obelisk_rt_bytecode_entry_v1 remoteBadEntries[] = {{0, 0}, {1, 1}};
  remoteBadInstruction.code.bytecode.entries = remoteBadEntries;
  remoteBadInstruction.code.bytecode.entry_count = std::size(remoteBadEntries);
  EXPECT_EQ(executeBytecode(remoteBadInstruction, nullptr, 0, 0, &action),
            OBELISK_RT_INVALID_BYTECODE);
}

obelisk_rt_status nativeFragment(obelisk_rt_context *, void *frame,
                                 uint64_t frameSize, uint32_t continuation,
                                 obelisk_rt_fragment_action_v1 *action) {
  if (!frame || frameSize != sizeof(uint64_t))
    return OBELISK_RT_INVALID_ARGUMENT;
  ++*static_cast<uint64_t *>(frame);
  *action = {OBELISK_RT_FRAGMENT_CONTINUE,
             OBELISK_RT_SUSPEND_NONE,
             continuation + 1,
             0,
             0,
             0};
  return OBELISK_RT_OK;
}

obelisk_rt_status invalidNativeAction(obelisk_rt_context *, void *, uint64_t,
                                      uint32_t,
                                      obelisk_rt_fragment_action_v1 *action) {
  *action = {OBELISK_RT_FRAGMENT_CONTINUE, OBELISK_RT_SUSPEND_NONE, 1, 0, 1, 0};
  return OBELISK_RT_OK;
}

obelisk_rt_status throwingNativeFragment(obelisk_rt_context *, void *, uint64_t,
                                         uint32_t,
                                         obelisk_rt_fragment_action_v1 *) {
  throw std::bad_alloc();
}

TEST(RuntimeFragmentTest, ValidatesNativeDescriptorsAndActions) {
  obelisk_rt_fragment_descriptor_v1 descriptor{};
  descriptor.handle = {OBELISK_RT_DESCRIPTOR_FRAGMENT, 0, 3};
  descriptor.code_kind = OBELISK_RT_FRAGMENT_NATIVE;
  descriptor.code.native_entry = invalidNativeAction;
  obelisk_rt_fragment_action_v1 action{};
  EXPECT_EQ(obelisk_rt_v1_fragment_execute(&descriptor, nullptr, nullptr, 0, 0,
                                           &action),
            OBELISK_RT_INVALID_ARGUMENT);

  descriptor.code.native_entry = throwingNativeFragment;
  EXPECT_EQ(obelisk_rt_v1_fragment_execute(&descriptor, nullptr, nullptr, 0, 0,
                                           &action),
            OBELISK_RT_OUT_OF_MEMORY);

  descriptor.flags = 1;
  EXPECT_EQ(obelisk_rt_v1_fragment_execute(&descriptor, nullptr, nullptr, 0, 0,
                                           &action),
            OBELISK_RT_INVALID_ARGUMENT);
  descriptor.flags = 0;
  descriptor.code.native_entry = nullptr;
  EXPECT_EQ(obelisk_rt_v1_fragment_execute(&descriptor, nullptr, nullptr, 0, 0,
                                           &action),
            OBELISK_RT_INVALID_ARGUMENT);
  descriptor.handle.kind = OBELISK_RT_DESCRIPTOR_PROCESS;
  EXPECT_EQ(obelisk_rt_v1_fragment_execute(&descriptor, nullptr, nullptr, 0, 0,
                                           &action),
            OBELISK_RT_INVALID_ARGUMENT);
}

TEST(RuntimeFragmentTest, NativeAndBytecodeUseOneDispatchContract) {
  obelisk_rt_fragment_descriptor_v1 native{};
  native.handle = {OBELISK_RT_DESCRIPTOR_FRAGMENT, 0, 3};
  native.code_kind = OBELISK_RT_FRAGMENT_NATIVE;
  native.code.native_entry = nativeFragment;
  uint64_t nativeFrame = 4;
  obelisk_rt_fragment_action_v1 nativeAction{};
  EXPECT_EQ(obelisk_rt_v1_fragment_execute(&native, nullptr, &nativeFrame,
                                           sizeof(nativeFrame), 11,
                                           &nativeAction),
            OBELISK_RT_OK);
  EXPECT_EQ(nativeFrame, 5u);

  std::vector<uint8_t> code;
  appendInstruction(code, OBELISK_RT_BC_LOAD_FRAME, OBELISK_RT_BC_TYPE_U64, 0,
                    0, 0, 0);
  appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_U64, 1, 0, 0,
                    1);
  appendInstruction(code, OBELISK_RT_BC_ADD, OBELISK_RT_BC_TYPE_U64, 2, 0, 1);
  appendInstruction(code, OBELISK_RT_BC_STORE_FRAME, OBELISK_RT_BC_TYPE_U64, 0,
                    2, 0, 0);
  appendInstruction(code, OBELISK_RT_BC_CONTINUE, OBELISK_RT_BC_TYPE_NONE, 0, 0,
                    0, 12);
  auto bytecode = bytecodeDescriptor(code, 3);
  constexpr obelisk_rt_bytecode_entry_v1 entry{11, 0};
  bytecode.code.bytecode.entries = &entry;
  bytecode.code.bytecode.entry_count = 1;
  uint64_t bytecodeFrame = 4;
  obelisk_rt_fragment_action_v1 bytecodeAction{};
  EXPECT_EQ(executeBytecode(bytecode, &bytecodeFrame, sizeof(bytecodeFrame), 11,
                            &bytecodeAction),
            OBELISK_RT_OK);
  EXPECT_EQ(bytecodeFrame, nativeFrame);
  EXPECT_EQ(bytecodeAction.kind, nativeAction.kind);
  EXPECT_EQ(bytecodeAction.suspend_kind, nativeAction.suspend_kind);
  EXPECT_EQ(bytecodeAction.continuation, nativeAction.continuation);
  EXPECT_EQ(bytecodeAction.flags, nativeAction.flags);
  EXPECT_EQ(bytecodeAction.payload, nativeAction.payload);
  EXPECT_EQ(bytecodeAction.auxiliary, nativeAction.auxiliary);
}

TEST_F(RuntimeTest, BytecodeServicesFormatWriteAndReleaseOwnedBuffers) {
  TempDirectory temporary;
  uint32_t descriptorValue = open(temporary.file("service.txt"), "w+");

  std::vector<uint8_t> constants(64, 0);
  constexpr std::string_view formatText = "%m %l %0t";
  constexpr std::string_view scopeText = "top.worker";
  constexpr std::string_view libraryCellText = "work.top";
  constexpr std::string_view suffixText = "ns";
  std::copy(formatText.begin(), formatText.end(), constants.begin());
  uint64_t time = 42;
  std::memcpy(constants.data() + 16, &time, sizeof(time));
  std::copy(scopeText.begin(), scopeText.end(), constants.begin() + 24);
  std::copy(libraryCellText.begin(), libraryCellText.end(),
            constants.begin() + 40);
  std::copy(suffixText.begin(), suffixText.end(), constants.begin() + 48);

  const obelisk_rt_bytecode_operand_v1 operands[] = {
      // format(format, arguments, environment, out buffer)
      {OBELISK_RT_BC_OPERAND_CONSTANT, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_BYTES, 0, 0, 0, formatText.size(), 0},
      {OBELISK_RT_BC_OPERAND_IMMEDIATE, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_ARGUMENT_ARRAY, 0, 0, 8, 1, 0},
      {OBELISK_RT_BC_OPERAND_IMMEDIATE, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_FORMAT_ENVIRONMENT, 0, 0, 9, 5, 0},
      {OBELISK_RT_BC_OPERAND_REGISTER, OBELISK_RT_BC_OPERAND_OUTPUT,
       OBELISK_RT_BC_VALUE_BUFFER, 0, 0, 1, 0, 0},
      // file_write(descriptor, resource bytes, out written)
      {OBELISK_RT_BC_OPERAND_FRAME, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_U32, 0, 0, 0, 4, 0},
      {OBELISK_RT_BC_OPERAND_RESOURCE, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_BYTES, 0, 0, 1, 0, 0},
      {OBELISK_RT_BC_OPERAND_FRAME, OBELISK_RT_BC_OPERAND_OUTPUT,
       OBELISK_RT_BC_VALUE_U64, 0, 0, 8, 8, 0},
      // buffer_release(resource)
      {OBELISK_RT_BC_OPERAND_RESOURCE, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_BUFFER, 0, 0, 1, 0, 0},
      // One time formatting argument followed by the environment's children.
      {OBELISK_RT_BC_OPERAND_CONSTANT, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_ARGUMENT_TIME, 0, 0, 16, 8, 0},
      {OBELISK_RT_BC_OPERAND_CONSTANT, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_BYTES, 0, 0, 24, scopeText.size(), 0},
      {OBELISK_RT_BC_OPERAND_CONSTANT, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_BYTES, 0, 0, 40, libraryCellText.size(), 0},
      {OBELISK_RT_BC_OPERAND_IMMEDIATE, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_U32, 0, 0, 0, 0, 0},
      {OBELISK_RT_BC_OPERAND_CONSTANT, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_BYTES, 0, 0, 48, suffixText.size(), 0},
      {OBELISK_RT_BC_OPERAND_IMMEDIATE, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_U64, 0, 0, 100, 0, 0},
  };
  const obelisk_rt_bytecode_service_site_v1 sites[] = {
      {OBELISK_RT_BC_SERVICE_FORMAT, 0, 4, 0, 0},
      {OBELISK_RT_BC_SERVICE_FILE_WRITE, 4, 3, 0, 0},
      {OBELISK_RT_BC_SERVICE_BUFFER_RELEASE, 7, 1, 0, 0},
  };

  std::vector<uint8_t> code;
  appendInstruction(code, OBELISK_RT_BC_CALL_SERVICE, OBELISK_RT_BC_TYPE_STATUS,
                    0, 0, 0, 0);
  appendInstruction(code, OBELISK_RT_BC_BRANCH_ZERO, OBELISK_RT_BC_TYPE_STATUS,
                    0, 0, 0, 3);
  appendInstruction(code, OBELISK_RT_BC_FAIL, OBELISK_RT_BC_TYPE_STATUS, 0, 0);
  appendInstruction(code, OBELISK_RT_BC_CALL_SERVICE, OBELISK_RT_BC_TYPE_STATUS,
                    0, 0, 0, 1);
  appendInstruction(code, OBELISK_RT_BC_BRANCH_ZERO, OBELISK_RT_BC_TYPE_STATUS,
                    0, 0, 0, 6);
  appendInstruction(code, OBELISK_RT_BC_FAIL, OBELISK_RT_BC_TYPE_STATUS, 0, 0);
  appendInstruction(code, OBELISK_RT_BC_CALL_SERVICE, OBELISK_RT_BC_TYPE_STATUS,
                    0, 0, 0, 2);
  appendInstruction(code, OBELISK_RT_BC_BRANCH_ZERO, OBELISK_RT_BC_TYPE_STATUS,
                    0, 0, 0, 9);
  appendInstruction(code, OBELISK_RT_BC_FAIL, OBELISK_RT_BC_TYPE_STATUS, 0, 0);
  appendInstruction(code, OBELISK_RT_BC_TERMINATE);

  auto descriptor = bytecodeDescriptor(code, 2);
  descriptor.code.bytecode.constants = constants.data();
  descriptor.code.bytecode.constant_size = constants.size();
  descriptor.code.bytecode.service_sites = sites;
  descriptor.code.bytecode.service_site_count = std::size(sites);
  descriptor.code.bytecode.operands = operands;
  descriptor.code.bytecode.operand_count = std::size(operands);
  struct Frame {
    uint32_t descriptor;
    uint32_t padding;
    uint64_t written;
  } frame{descriptorValue, 0, 0};
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(executeBytecode(descriptor, &frame, sizeof(frame), 0, &action, 0,
                            context),
            OBELISK_RT_OK);
  EXPECT_EQ(frame.written, 26u);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  ASSERT_EQ(obelisk_rt_v1_file_flush(context, descriptorValue), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_rewind(context, descriptorValue), OBELISK_RT_OK);
  char output[64]{};
  uint64_t read = 0;
  ASSERT_EQ(obelisk_rt_v1_file_read(context, descriptorValue, output,
                                    sizeof(output), &read),
            OBELISK_RT_OK);
  EXPECT_EQ(std::string(output, read), "top.worker work.top 4200ns");
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptorValue), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, BytecodeServicesAcceptEmptyConstantPoolSlices) {
  const obelisk_rt_bytecode_operand_v1 operands[] = {
      {OBELISK_RT_BC_OPERAND_CONSTANT, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_BYTES, 0, 0, 0, 0, 0},
      {OBELISK_RT_BC_OPERAND_IMMEDIATE, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_ARGUMENT_ARRAY, 0, 0, 0, 0, 0},
      {OBELISK_RT_BC_OPERAND_IMMEDIATE, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_FORMAT_ENVIRONMENT, 0, 0, 0, 0, 0},
      {OBELISK_RT_BC_OPERAND_REGISTER, OBELISK_RT_BC_OPERAND_OUTPUT,
       OBELISK_RT_BC_VALUE_BUFFER, 0, 0, 1, 0, 0},
      {OBELISK_RT_BC_OPERAND_RESOURCE, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_BUFFER, 0, 0, 1, 0, 0},
  };
  const obelisk_rt_bytecode_service_site_v1 sites[] = {
      {OBELISK_RT_BC_SERVICE_FORMAT, 0, 4, 0, 0},
      {OBELISK_RT_BC_SERVICE_BUFFER_RELEASE, 4, 1, 0, 0},
  };
  std::vector<uint8_t> code;
  appendCheckedService(code, 0);
  appendCheckedService(code, 1);
  appendInstruction(code, OBELISK_RT_BC_TERMINATE);
  auto descriptor = bytecodeDescriptor(code, 2);
  descriptor.code.bytecode.service_sites = sites;
  descriptor.code.bytecode.service_site_count = std::size(sites);
  descriptor.code.bytecode.operands = operands;
  descriptor.code.bytecode.operand_count = std::size(operands);
  obelisk_rt_fragment_action_v1 action{};
  EXPECT_EQ(executeBytecode(descriptor, nullptr, 0, 0, &action, 0, context),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
}

TEST_F(RuntimeTest, BytecodeServicesExerciseEveryFileAndDisplayCall) {
  TempDirectory temporary;
  std::vector<uint8_t> constants;
  struct ConstantSpan {
    uint64_t offset;
    uint64_t size;
  };
  auto addConstant = [&](std::string_view value) {
    ConstantSpan span{constants.size(), value.size()};
    constants.insert(constants.end(), value.begin(), value.end());
    return span;
  };
  ConstantSpan path = addConstant(temporary.file("services.bin").string());
  ConstantSpan mcdPath = addConstant(temporary.file("services.mcd").string());
  ConstantSpan mode = addConstant("w+");
  ConstantSpan displayText = addConstant("head");
  ConstantSpan writtenText = addConstant("abc\n");
  ConstantSpan mcdText = addConstant("mcd");

  struct Frame {
    uint32_t descriptor = 0;
    uint32_t mcd = 0;
    uint64_t written = 0;
    uint64_t mcdWritten = 0;
    int64_t firstTell = 0;
    int64_t boundedTell = 0;
    int64_t finalTell = 0;
    uint64_t read = 0;
    uint8_t byte = 0;
    uint8_t padding[3]{};
    uint32_t eof = 0;
    int32_t error = 0;
    std::array<uint8_t, 16> data{};
  } frame;

  auto operand = [](obelisk_rt_bytecode_operand_kind kind,
                    obelisk_rt_bytecode_operand_direction direction,
                    obelisk_rt_bytecode_value_kind valueKind, uint64_t value,
                    uint64_t size = 0, uint64_t auxiliary = 0,
                    uint8_t flags = 0) {
    return obelisk_rt_bytecode_operand_v1{kind,  direction, valueKind, flags, 0,
                                          value, size,      auxiliary};
  };
  auto constantBytes = [&](ConstantSpan span) {
    return operand(OBELISK_RT_BC_OPERAND_CONSTANT, OBELISK_RT_BC_OPERAND_INPUT,
                   OBELISK_RT_BC_VALUE_BYTES, span.offset, span.size);
  };
  auto inputFrame = [&](obelisk_rt_bytecode_value_kind kind, uint64_t offset,
                        uint64_t size) {
    return operand(OBELISK_RT_BC_OPERAND_FRAME, OBELISK_RT_BC_OPERAND_INPUT,
                   kind, offset, size);
  };
  auto outputFrame = [&](obelisk_rt_bytecode_value_kind kind, uint64_t offset,
                         uint64_t size) {
    return operand(OBELISK_RT_BC_OPERAND_FRAME, OBELISK_RT_BC_OPERAND_OUTPUT,
                   kind, offset, size);
  };
  auto immediate = [&](obelisk_rt_bytecode_value_kind kind, uint64_t value) {
    return operand(OBELISK_RT_BC_OPERAND_IMMEDIATE, OBELISK_RT_BC_OPERAND_INPUT,
                   kind, value);
  };
  auto outputBuffer = [&](uint16_t reg) {
    return operand(OBELISK_RT_BC_OPERAND_REGISTER, OBELISK_RT_BC_OPERAND_OUTPUT,
                   OBELISK_RT_BC_VALUE_BUFFER, reg);
  };
  auto inputBuffer = [&](uint16_t reg) {
    return operand(OBELISK_RT_BC_OPERAND_RESOURCE, OBELISK_RT_BC_OPERAND_INPUT,
                   OBELISK_RT_BC_VALUE_BUFFER, reg);
  };

  std::vector<obelisk_rt_bytecode_operand_v1> operands;
  std::vector<obelisk_rt_bytecode_service_site_v1> sites;
  std::vector<uint8_t> code;
  auto call =
      [&](obelisk_rt_bytecode_service service,
          std::initializer_list<obelisk_rt_bytecode_operand_v1> siteOperands) {
        ASSERT_LE(operands.size(), UINT32_MAX);
        ASSERT_LE(siteOperands.size(), UINT16_MAX);
        uint32_t first = static_cast<uint32_t>(operands.size());
        operands.insert(operands.end(), siteOperands.begin(),
                        siteOperands.end());
        sites.push_back(
            {service, first, static_cast<uint16_t>(siteOperands.size()), 0, 0});
        appendCheckedService(code, sites.size() - 1);
      };

  constexpr uint64_t descriptorOffset = offsetof(Frame, descriptor);
  constexpr uint64_t mcdOffset = offsetof(Frame, mcd);
  auto descriptor = [&] {
    return inputFrame(OBELISK_RT_BC_VALUE_U32, descriptorOffset,
                      sizeof(frame.descriptor));
  };
  auto mcd = [&] {
    return inputFrame(OBELISK_RT_BC_VALUE_U32, mcdOffset, sizeof(frame.mcd));
  };
  auto noEnvironment = [&] {
    return operand(OBELISK_RT_BC_OPERAND_IMMEDIATE, OBELISK_RT_BC_OPERAND_INPUT,
                   OBELISK_RT_BC_VALUE_FORMAT_ENVIRONMENT, 0, 0);
  };

  call(OBELISK_RT_BC_SERVICE_FILE_OPEN,
       {constantBytes(path), constantBytes(mode),
        outputFrame(OBELISK_RT_BC_VALUE_U32, descriptorOffset,
                    sizeof(frame.descriptor))});

  uint64_t displayArgument = operands.size();
  operands.push_back(operand(OBELISK_RT_BC_OPERAND_CONSTANT,
                             OBELISK_RT_BC_OPERAND_INPUT,
                             OBELISK_RT_BC_VALUE_ARGUMENT_STRING,
                             displayText.offset, displayText.size));
  call(OBELISK_RT_BC_SERVICE_DISPLAY,
       {descriptor(), immediate(OBELISK_RT_BC_VALUE_U32, 1),
        immediate(OBELISK_RT_BC_VALUE_U32, OBELISK_RT_RADIX_DECIMAL),
        operand(OBELISK_RT_BC_OPERAND_IMMEDIATE, OBELISK_RT_BC_OPERAND_INPUT,
                OBELISK_RT_BC_VALUE_ARGUMENT_ARRAY, displayArgument, 1),
        noEnvironment()});
  call(OBELISK_RT_BC_SERVICE_FILE_WRITE,
       {descriptor(), constantBytes(writtenText),
        outputFrame(OBELISK_RT_BC_VALUE_U64, offsetof(Frame, written),
                    sizeof(frame.written))});
  call(OBELISK_RT_BC_SERVICE_FILE_FLUSH, {descriptor()});
  call(OBELISK_RT_BC_SERVICE_FILE_TELL,
       {descriptor(),
        outputFrame(OBELISK_RT_BC_VALUE_I64, offsetof(Frame, firstTell),
                    sizeof(frame.firstTell))});
  call(OBELISK_RT_BC_SERVICE_FILE_REWIND, {descriptor()});
  call(OBELISK_RT_BC_SERVICE_FILE_GETLINE,
       {descriptor(), immediate(OBELISK_RT_BC_VALUE_U64, 3), outputBuffer(1)});
  call(OBELISK_RT_BC_SERVICE_BUFFER_RELEASE, {inputBuffer(1)});
  call(OBELISK_RT_BC_SERVICE_FILE_TELL,
       {descriptor(),
        outputFrame(OBELISK_RT_BC_VALUE_I64, offsetof(Frame, boundedTell),
                    sizeof(frame.boundedTell))});
  call(OBELISK_RT_BC_SERVICE_FILE_REWIND, {descriptor()});
  call(OBELISK_RT_BC_SERVICE_FILE_GETLINE,
       {descriptor(), immediate(OBELISK_RT_BC_VALUE_U64, 64), outputBuffer(1)});
  call(OBELISK_RT_BC_SERVICE_BUFFER_RELEASE, {inputBuffer(1)});
  call(OBELISK_RT_BC_SERVICE_FILE_GETC,
       {descriptor(), outputFrame(OBELISK_RT_BC_VALUE_U8, offsetof(Frame, byte),
                                  sizeof(frame.byte))});
  call(OBELISK_RT_BC_SERVICE_FILE_UNGETC,
       {descriptor(), immediate(OBELISK_RT_BC_VALUE_U8, 'a')});
  call(OBELISK_RT_BC_SERVICE_FILE_READ,
       {descriptor(),
        operand(OBELISK_RT_BC_OPERAND_FRAME, OBELISK_RT_BC_OPERAND_INOUT,
                OBELISK_RT_BC_VALUE_MUTABLE_BYTES, offsetof(Frame, data),
                frame.data.size()),
        outputFrame(OBELISK_RT_BC_VALUE_U64, offsetof(Frame, read),
                    sizeof(frame.read))});
  call(OBELISK_RT_BC_SERVICE_FILE_EOF,
       {descriptor(), outputFrame(OBELISK_RT_BC_VALUE_U32, offsetof(Frame, eof),
                                  sizeof(frame.eof))});
  call(OBELISK_RT_BC_SERVICE_FILE_ERROR,
       {descriptor(),
        outputFrame(OBELISK_RT_BC_VALUE_I32, offsetof(Frame, error),
                    sizeof(frame.error)),
        outputBuffer(1)});
  call(OBELISK_RT_BC_SERVICE_BUFFER_RELEASE, {inputBuffer(1)});
  call(OBELISK_RT_BC_SERVICE_FILE_SEEK,
       {descriptor(), immediate(OBELISK_RT_BC_VALUE_I64, 1),
        immediate(OBELISK_RT_BC_VALUE_U32, OBELISK_RT_SEEK_SET)});
  call(OBELISK_RT_BC_SERVICE_FILE_TELL,
       {descriptor(),
        outputFrame(OBELISK_RT_BC_VALUE_I64, offsetof(Frame, finalTell),
                    sizeof(frame.finalTell))});
  call(OBELISK_RT_BC_SERVICE_FILE_REWIND, {descriptor()});
  call(OBELISK_RT_BC_SERVICE_FILE_CLOSE, {descriptor()});

  call(OBELISK_RT_BC_SERVICE_FILE_OPEN_MCD,
       {constantBytes(mcdPath),
        outputFrame(OBELISK_RT_BC_VALUE_U32, mcdOffset, sizeof(frame.mcd))});
  call(OBELISK_RT_BC_SERVICE_FILE_WRITE,
       {mcd(), constantBytes(mcdText),
        outputFrame(OBELISK_RT_BC_VALUE_U64, offsetof(Frame, mcdWritten),
                    sizeof(frame.mcdWritten))});
  call(OBELISK_RT_BC_SERVICE_FILE_FLUSH, {mcd()});
  call(OBELISK_RT_BC_SERVICE_FILE_CLOSE, {mcd()});
  appendInstruction(code, OBELISK_RT_BC_TERMINATE);

  auto program = bytecodeDescriptor(code, 2);
  program.code.bytecode.constants = constants.data();
  program.code.bytecode.constant_size = constants.size();
  program.code.bytecode.service_sites = sites.data();
  program.code.bytecode.service_site_count = sites.size();
  program.code.bytecode.operands = operands.data();
  program.code.bytecode.operand_count = operands.size();
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(
      executeBytecode(program, &frame, sizeof(frame), 0, &action, 0, context),
      OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  EXPECT_EQ(frame.written, writtenText.size);
  EXPECT_EQ(frame.mcdWritten, mcdText.size);
  EXPECT_EQ(frame.firstTell, 9);
  EXPECT_EQ(frame.boundedTell, 3);
  EXPECT_EQ(frame.finalTell, 1);
  EXPECT_EQ(frame.byte, 'a');
  EXPECT_EQ(frame.read, 4u);
  EXPECT_EQ(std::string(reinterpret_cast<const char *>(frame.data.data()),
                        frame.read),
            "abc\n");
  EXPECT_EQ(frame.eof, 1u);
  EXPECT_EQ(frame.error, 0);
  EXPECT_EQ(readHostFile(temporary.file("services.bin")), "head\nabc\n");
  EXPECT_EQ(readHostFile(temporary.file("services.mcd")), "mcd");
}

TEST(RuntimeFragmentTest, RejectsMalformedServiceMetadataAndResources) {
  std::vector<uint8_t> code;
  appendInstruction(code, OBELISK_RT_BC_CALL_SERVICE, OBELISK_RT_BC_TYPE_STATUS,
                    0, 0, 0, 0);
  appendInstruction(code, OBELISK_RT_BC_TERMINATE);
  auto descriptor = bytecodeDescriptor(code, 1);
  obelisk_rt_fragment_action_v1 action{};

  obelisk_rt_bytecode_service_site_v1 badSite{999, 0, 0, 0, 0};
  descriptor.code.bytecode.service_sites = &badSite;
  descriptor.code.bytecode.service_site_count = 1;
  EXPECT_EQ(executeBytecode(descriptor, nullptr, 0, 0, &action),
            OBELISK_RT_INVALID_BYTECODE);

  obelisk_rt_bytecode_operand_v1 forged{OBELISK_RT_BC_OPERAND_RESOURCE,
                                        OBELISK_RT_BC_OPERAND_INPUT,
                                        OBELISK_RT_BC_VALUE_BUFFER,
                                        0,
                                        0,
                                        0,
                                        0,
                                        0};
  obelisk_rt_bytecode_service_site_v1 release{
      OBELISK_RT_BC_SERVICE_BUFFER_RELEASE, 0, 1, 0, 0};
  descriptor.code.bytecode.service_sites = &release;
  descriptor.code.bytecode.operands = &forged;
  descriptor.code.bytecode.operand_count = 1;
  EXPECT_EQ(executeBytecode(descriptor, nullptr, 0, 0, &action),
            OBELISK_RT_INVALID_BYTECODE);

  // A caller-provided validation record is an observation of validation, not
  // authority to bypass it.
  obelisk_rt_bytecode_validation_v1 forgedValidation{2, 0};
  descriptor.code.bytecode.validation = &forgedValidation;
  descriptor.code.bytecode.service_sites = &badSite;
  descriptor.code.bytecode.operands = nullptr;
  descriptor.code.bytecode.operand_count = 0;
  EXPECT_EQ(executeBytecode(descriptor, nullptr, 0, 0, &action),
            OBELISK_RT_INVALID_BYTECODE);
  EXPECT_EQ(forgedValidation.state, 3u);
  descriptor.code.bytecode.validation = nullptr;

  // CALL_SERVICE must not store its status over one of the service results.
  const obelisk_rt_bytecode_operand_v1 eofOperands[] = {
      {OBELISK_RT_BC_OPERAND_IMMEDIATE, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_U32, 0, 0, 0, 0, 0},
      {OBELISK_RT_BC_OPERAND_REGISTER, OBELISK_RT_BC_OPERAND_OUTPUT,
       OBELISK_RT_BC_VALUE_U32, 0, 0, 0, 0, 0},
  };
  const obelisk_rt_bytecode_service_site_v1 eofSite{
      OBELISK_RT_BC_SERVICE_FILE_EOF, 0, 2, 0, 0};
  descriptor.code.bytecode.service_sites = &eofSite;
  descriptor.code.bytecode.operands = eofOperands;
  descriptor.code.bytecode.operand_count = std::size(eofOperands);
  EXPECT_EQ(executeBytecode(descriptor, nullptr, 0, 0, &action),
            OBELISK_RT_INVALID_BYTECODE);

  // Multi-result services also require distinct result registers.
  const obelisk_rt_bytecode_operand_v1 errorOperands[] = {
      {OBELISK_RT_BC_OPERAND_IMMEDIATE, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_U32, 0, 0, 0, 0, 0},
      {OBELISK_RT_BC_OPERAND_REGISTER, OBELISK_RT_BC_OPERAND_OUTPUT,
       OBELISK_RT_BC_VALUE_I32, 0, 0, 1, 0, 0},
      {OBELISK_RT_BC_OPERAND_REGISTER, OBELISK_RT_BC_OPERAND_OUTPUT,
       OBELISK_RT_BC_VALUE_BUFFER, 0, 0, 1, 0, 0},
  };
  const obelisk_rt_bytecode_service_site_v1 errorSite{
      OBELISK_RT_BC_SERVICE_FILE_ERROR, 0, 3, 0, 0};
  descriptor = bytecodeDescriptor(code, 2);
  descriptor.code.bytecode.service_sites = &errorSite;
  descriptor.code.bytecode.service_site_count = 1;
  descriptor.code.bytecode.operands = errorOperands;
  descriptor.code.bytecode.operand_count = std::size(errorOperands);
  EXPECT_EQ(executeBytecode(descriptor, nullptr, 0, 0, &action),
            OBELISK_RT_INVALID_BYTECODE);
}

TEST_F(RuntimeTest, BytecodeResourcesRejectLeaksAndDoubleRelease) {
  TempDirectory temporary;
  uint32_t descriptorValue = open(temporary.file("line.txt"), "w+");
  uint64_t written = 0;
  ASSERT_EQ(
      obelisk_rt_v1_file_write(context, descriptorValue, "line\n", 5, &written),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_rewind(context, descriptorValue), OBELISK_RT_OK);

  const obelisk_rt_bytecode_operand_v1 operands[] = {
      {OBELISK_RT_BC_OPERAND_IMMEDIATE, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_U32, 0, 0, descriptorValue, 0, 0},
      {OBELISK_RT_BC_OPERAND_IMMEDIATE, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_U64, 0, 0, 5, 0, 0},
      {OBELISK_RT_BC_OPERAND_REGISTER, OBELISK_RT_BC_OPERAND_OUTPUT,
       OBELISK_RT_BC_VALUE_BUFFER, 0, 0, 1, 0, 0},
      {OBELISK_RT_BC_OPERAND_RESOURCE, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_BUFFER, 0, 0, 1, 0, 0},
      {OBELISK_RT_BC_OPERAND_IMMEDIATE, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_U32, 0, 0, descriptorValue, 0, 0},
      {OBELISK_RT_BC_OPERAND_REGISTER, OBELISK_RT_BC_OPERAND_OUTPUT,
       OBELISK_RT_BC_VALUE_U32, 0, 0, 1, 0, 0},
  };
  const obelisk_rt_bytecode_service_site_v1 sites[] = {
      {OBELISK_RT_BC_SERVICE_FILE_GETLINE, 0, 3, 0, 0},
      {OBELISK_RT_BC_SERVICE_BUFFER_RELEASE, 3, 1, 0, 0},
      {OBELISK_RT_BC_SERVICE_FILE_EOF, 4, 2, 0, 0},
  };

  std::vector<uint8_t> leakCode;
  appendCheckedService(leakCode, 0);
  appendInstruction(leakCode, OBELISK_RT_BC_TERMINATE);
  auto leak = bytecodeDescriptor(leakCode, 2);
  leak.code.bytecode.service_sites = sites;
  leak.code.bytecode.service_site_count = std::size(sites);
  leak.code.bytecode.operands = operands;
  leak.code.bytecode.operand_count = std::size(operands);
  obelisk_rt_fragment_action_v1 action{};
  EXPECT_EQ(executeBytecode(leak, nullptr, 0, 0, &action, 0, context),
            OBELISK_RT_INVALID_BYTECODE);

  ASSERT_EQ(obelisk_rt_v1_file_rewind(context, descriptorValue), OBELISK_RT_OK);
  std::vector<uint8_t> overwriteResourceCode;
  appendCheckedService(overwriteResourceCode, 0);
  appendCheckedService(overwriteResourceCode, 2);
  appendInstruction(overwriteResourceCode, OBELISK_RT_BC_TERMINATE);
  auto overwriteResource = bytecodeDescriptor(overwriteResourceCode, 2);
  overwriteResource.code.bytecode.service_sites = sites;
  overwriteResource.code.bytecode.service_site_count = std::size(sites);
  overwriteResource.code.bytecode.operands = operands;
  overwriteResource.code.bytecode.operand_count = std::size(operands);
  EXPECT_EQ(
      executeBytecode(overwriteResource, nullptr, 0, 0, &action, 0, context),
      OBELISK_RT_INVALID_BYTECODE);

  ASSERT_EQ(obelisk_rt_v1_file_rewind(context, descriptorValue), OBELISK_RT_OK);
  std::vector<uint8_t> doubleReleaseCode;
  appendCheckedService(doubleReleaseCode, 0);
  appendCheckedService(doubleReleaseCode, 1);
  appendCheckedService(doubleReleaseCode, 1);
  appendInstruction(doubleReleaseCode, OBELISK_RT_BC_TERMINATE);
  auto doubleRelease = bytecodeDescriptor(doubleReleaseCode, 2);
  doubleRelease.code.bytecode.service_sites = sites;
  doubleRelease.code.bytecode.service_site_count = std::size(sites);
  doubleRelease.code.bytecode.operands = operands;
  doubleRelease.code.bytecode.operand_count = std::size(operands);
  EXPECT_EQ(executeBytecode(doubleRelease, nullptr, 0, 0, &action, 0, context),
            OBELISK_RT_INVALID_BYTECODE);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptorValue), OBELISK_RT_OK);
}

TEST(RuntimeFragmentTest, ValidatesEveryServiceOperandBoundary) {
  std::vector<uint8_t> code;
  appendCheckedService(code, 0);
  appendInstruction(code, OBELISK_RT_BC_TERMINATE);
  obelisk_rt_fragment_action_v1 action{};

  auto check = [&](const obelisk_rt_bytecode_service_site_v1 &site,
                   const obelisk_rt_bytecode_operand_v1 *operands,
                   uint64_t operandCount, uint32_t registers = 1,
                   void *frame = nullptr, uint64_t frameSize = 0) {
    auto descriptor = bytecodeDescriptor(code, registers);
    descriptor.code.bytecode.service_sites = &site;
    descriptor.code.bytecode.service_site_count = 1;
    descriptor.code.bytecode.operands = operands;
    descriptor.code.bytecode.operand_count = operandCount;
    return executeBytecode(descriptor, frame, frameSize, 0, &action);
  };

  obelisk_rt_bytecode_service_site_v1 noArity{OBELISK_RT_BC_SERVICE_FILE_CLOSE,
                                              0, 0, 0, 0};
  EXPECT_EQ(check(noArity, nullptr, 0), OBELISK_RT_INVALID_BYTECODE);

  obelisk_rt_bytecode_operand_v1 wrongType{OBELISK_RT_BC_OPERAND_IMMEDIATE,
                                           OBELISK_RT_BC_OPERAND_INPUT,
                                           OBELISK_RT_BC_VALUE_I64,
                                           0,
                                           0,
                                           0,
                                           0,
                                           0};
  obelisk_rt_bytecode_service_site_v1 close{OBELISK_RT_BC_SERVICE_FILE_CLOSE, 0,
                                            1, 0, 0};
  EXPECT_EQ(check(close, &wrongType, 1), OBELISK_RT_INVALID_BYTECODE);

  obelisk_rt_bytecode_operand_v1 badFrame{OBELISK_RT_BC_OPERAND_FRAME,
                                          OBELISK_RT_BC_OPERAND_INPUT,
                                          OBELISK_RT_BC_VALUE_U32,
                                          0,
                                          0,
                                          8,
                                          4,
                                          0};
  uint32_t frame = 0;
  EXPECT_EQ(check(close, &badFrame, 1, 1, &frame, sizeof(frame)),
            OBELISK_RT_INVALID_BYTECODE);

  obelisk_rt_bytecode_operand_v1 badRegister{OBELISK_RT_BC_OPERAND_REGISTER,
                                             OBELISK_RT_BC_OPERAND_INPUT,
                                             OBELISK_RT_BC_VALUE_U32,
                                             0,
                                             0,
                                             1,
                                             0,
                                             0};
  EXPECT_EQ(check(close, &badRegister, 1), OBELISK_RT_INVALID_BYTECODE);

  obelisk_rt_bytecode_operand_v1 badConstant{OBELISK_RT_BC_OPERAND_CONSTANT,
                                             OBELISK_RT_BC_OPERAND_INPUT,
                                             OBELISK_RT_BC_VALUE_BYTES,
                                             0,
                                             0,
                                             1,
                                             8,
                                             0};
  obelisk_rt_bytecode_operand_v1 output{OBELISK_RT_BC_OPERAND_REGISTER,
                                        OBELISK_RT_BC_OPERAND_OUTPUT,
                                        OBELISK_RT_BC_VALUE_U32,
                                        0,
                                        0,
                                        0,
                                        0,
                                        0};
  const obelisk_rt_bytecode_operand_v1 openOperands[] = {badConstant, output};
  obelisk_rt_bytecode_service_site_v1 openSite{
      OBELISK_RT_BC_SERVICE_FILE_OPEN_MCD, 0, 2, 0, 0};
  auto descriptor = bytecodeDescriptor(code, 1);
  const uint8_t constant = 0;
  descriptor.code.bytecode.constants = &constant;
  descriptor.code.bytecode.constant_size = 1;
  descriptor.code.bytecode.service_sites = &openSite;
  descriptor.code.bytecode.service_site_count = 1;
  descriptor.code.bytecode.operands = openOperands;
  descriptor.code.bytecode.operand_count = std::size(openOperands);
  EXPECT_EQ(executeBytecode(descriptor, nullptr, 0, 0, &action),
            OBELISK_RT_INVALID_BYTECODE);

  const obelisk_rt_bytecode_operand_v1 overlappingReadOperands[] = {
      {OBELISK_RT_BC_OPERAND_IMMEDIATE, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_U32, 0, 0, 0, 0, 0},
      {OBELISK_RT_BC_OPERAND_FRAME, OBELISK_RT_BC_OPERAND_INOUT,
       OBELISK_RT_BC_VALUE_MUTABLE_BYTES, 0, 0, 0, 8, 0},
      {OBELISK_RT_BC_OPERAND_FRAME, OBELISK_RT_BC_OPERAND_OUTPUT,
       OBELISK_RT_BC_VALUE_U64, 0, 0, 4, 8, 0},
  };
  const obelisk_rt_bytecode_service_site_v1 overlappingReadSite{
      OBELISK_RT_BC_SERVICE_FILE_READ, 0, 3, 0, 0};
  std::array<uint8_t, 16> overlappingFrame{};
  EXPECT_EQ(check(overlappingReadSite, overlappingReadOperands,
                  std::size(overlappingReadOperands), 1,
                  overlappingFrame.data(), overlappingFrame.size()),
            OBELISK_RT_INVALID_BYTECODE);

  const obelisk_rt_bytecode_operand_v1 overlappingWriteOperands[] = {
      {OBELISK_RT_BC_OPERAND_IMMEDIATE, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_U32, 0, 0, 0, 0, 0},
      {OBELISK_RT_BC_OPERAND_FRAME, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_BYTES, 0, 0, 0, 8, 0},
      {OBELISK_RT_BC_OPERAND_FRAME, OBELISK_RT_BC_OPERAND_OUTPUT,
       OBELISK_RT_BC_VALUE_U64, 0, 0, 0, 8, 0},
  };
  const obelisk_rt_bytecode_service_site_v1 overlappingWriteSite{
      OBELISK_RT_BC_SERVICE_FILE_WRITE, 0, 3, 0, 0};
  EXPECT_EQ(check(overlappingWriteSite, overlappingWriteOperands,
                  std::size(overlappingWriteOperands), 1,
                  overlappingFrame.data(), overlappingFrame.size()),
            OBELISK_RT_INVALID_BYTECODE);
}

TEST_F(RuntimeTest, BytecodeFailureOutputsMatchNativeZeroInitialization) {
  const obelisk_rt_bytecode_operand_v1 operands[] = {
      {OBELISK_RT_BC_OPERAND_IMMEDIATE, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_U32, 0, 0, 0, 0, 0},
      {OBELISK_RT_BC_OPERAND_FRAME, OBELISK_RT_BC_OPERAND_OUTPUT,
       OBELISK_RT_BC_VALUE_U32, 0, 0, 0, 4, 0},
  };
  const obelisk_rt_bytecode_service_site_v1 site{OBELISK_RT_BC_SERVICE_FILE_EOF,
                                                 0, 2, 0, 0};
  std::vector<uint8_t> code;
  appendCheckedService(code, 0);
  appendInstruction(code, OBELISK_RT_BC_TERMINATE);
  auto descriptor = bytecodeDescriptor(code, 1);
  descriptor.code.bytecode.service_sites = &site;
  descriptor.code.bytecode.service_site_count = 1;
  descriptor.code.bytecode.operands = operands;
  descriptor.code.bytecode.operand_count = std::size(operands);
  uint32_t eof = UINT32_C(0xdeadbeef);
  obelisk_rt_fragment_action_v1 action{};
  EXPECT_EQ(
      executeBytecode(descriptor, &eof, sizeof(eof), 0, &action, 0, context),
      OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(eof, 0u);
}

TEST(RuntimeFragmentTest, FailedBufferServiceStillProducesReleasableResource) {
  const obelisk_rt_bytecode_operand_v1 operands[] = {
      {OBELISK_RT_BC_OPERAND_CONSTANT, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_BYTES, 0, 0, 0, 0, 0},
      {OBELISK_RT_BC_OPERAND_IMMEDIATE, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_ARGUMENT_ARRAY, 0, 0, 0, 0, 0},
      {OBELISK_RT_BC_OPERAND_IMMEDIATE, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_FORMAT_ENVIRONMENT, 0, 0, 0, 0, 0},
      {OBELISK_RT_BC_OPERAND_REGISTER, OBELISK_RT_BC_OPERAND_OUTPUT,
       OBELISK_RT_BC_VALUE_BUFFER, 0, 0, 1, 0, 0},
      {OBELISK_RT_BC_OPERAND_RESOURCE, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_BUFFER, 0, 0, 1, 0, 0},
  };
  const obelisk_rt_bytecode_service_site_v1 sites[] = {
      {OBELISK_RT_BC_SERVICE_FORMAT, 0, 4, 0, 0},
      {OBELISK_RT_BC_SERVICE_BUFFER_RELEASE, 4, 1, 0, 0},
  };
  std::vector<uint8_t> code;
  appendInstruction(code, OBELISK_RT_BC_CALL_SERVICE, OBELISK_RT_BC_TYPE_STATUS,
                    0, 0, 0, 0);
  appendInstruction(code, OBELISK_RT_BC_CALL_SERVICE, OBELISK_RT_BC_TYPE_STATUS,
                    2, 0, 0, 1);
  appendInstruction(code, OBELISK_RT_BC_FAIL, OBELISK_RT_BC_TYPE_STATUS, 0, 0);
  auto descriptor = bytecodeDescriptor(code, 3);
  descriptor.code.bytecode.service_sites = sites;
  descriptor.code.bytecode.service_site_count = std::size(sites);
  descriptor.code.bytecode.operands = operands;
  descriptor.code.bytecode.operand_count = std::size(operands);
  obelisk_rt_fragment_action_v1 action{};
  EXPECT_EQ(executeBytecode(descriptor, nullptr, 0, 0, &action),
            OBELISK_RT_INVALID_ARGUMENT);
}

TEST_F(RuntimeTest, MalformedInstructionCannotRunEarlierFileService) {
  TempDirectory temporary;
  std::string path = temporary.file("must-not-exist.txt").string();
  std::vector<uint8_t> constants(path.begin(), path.end());
  uint64_t modeOffset = constants.size();
  constants.push_back('w');
  const obelisk_rt_bytecode_operand_v1 operands[] = {
      {OBELISK_RT_BC_OPERAND_CONSTANT, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_BYTES, 0, 0, 0, path.size(), 0},
      {OBELISK_RT_BC_OPERAND_CONSTANT, OBELISK_RT_BC_OPERAND_INPUT,
       OBELISK_RT_BC_VALUE_BYTES, 0, 0, modeOffset, 1, 0},
      {OBELISK_RT_BC_OPERAND_FRAME, OBELISK_RT_BC_OPERAND_OUTPUT,
       OBELISK_RT_BC_VALUE_U32, 0, 0, 0, 4, 0},
  };
  const obelisk_rt_bytecode_service_site_v1 site{
      OBELISK_RT_BC_SERVICE_FILE_OPEN, 0, 3, 0, 0};
  std::vector<uint8_t> code;
  appendInstruction(code, OBELISK_RT_BC_CALL_SERVICE, OBELISK_RT_BC_TYPE_STATUS,
                    0, 0, 0, 0);
  appendInstruction(code, 255);
  obelisk_rt_bytecode_validation_v1 validation{};
  auto descriptor = bytecodeDescriptor(code, 1);
  descriptor.code.bytecode.validation = &validation;
  descriptor.code.bytecode.constants = constants.data();
  descriptor.code.bytecode.constant_size = constants.size();
  descriptor.code.bytecode.service_sites = &site;
  descriptor.code.bytecode.service_site_count = 1;
  descriptor.code.bytecode.operands = operands;
  descriptor.code.bytecode.operand_count = std::size(operands);
  uint32_t opened = 0;
  obelisk_rt_fragment_action_v1 action{};
  EXPECT_EQ(executeBytecode(descriptor, &opened, sizeof(opened), 0, &action, 0,
                            context),
            OBELISK_RT_INVALID_BYTECODE);
  EXPECT_EQ(validation.state, OBELISK_RT_BC_VALIDATION_INVALID);
  EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(RuntimeFragmentTest, BytecodeServiceStatusPropagatesMissingContext) {
  const obelisk_rt_bytecode_operand_v1 operand{OBELISK_RT_BC_OPERAND_IMMEDIATE,
                                               OBELISK_RT_BC_OPERAND_INPUT,
                                               OBELISK_RT_BC_VALUE_U32,
                                               0,
                                               0,
                                               0,
                                               0,
                                               0};
  const obelisk_rt_bytecode_service_site_v1 site{
      OBELISK_RT_BC_SERVICE_FILE_FLUSH, 0, 1, 0, 0};
  std::vector<uint8_t> code;
  appendCheckedService(code, 0);
  appendInstruction(code, OBELISK_RT_BC_TERMINATE);
  auto descriptor = bytecodeDescriptor(code, 1);
  descriptor.code.bytecode.service_sites = &site;
  descriptor.code.bytecode.service_site_count = 1;
  descriptor.code.bytecode.operands = &operand;
  descriptor.code.bytecode.operand_count = 1;
  obelisk_rt_fragment_action_v1 action{};
  EXPECT_EQ(executeBytecode(descriptor, nullptr, 0, 0, &action),
            OBELISK_RT_INVALID_ARGUMENT);
}

constexpr uint64_t kNodeLinkOffset = sizeof(void *);
constexpr uint64_t kNodeValueOffset = sizeof(void *) * 2;
constexpr uint64_t kDerivedExtraOffset = sizeof(void *) * 3;

const obelisk_rt_trace_entry_v1 nodeTraceEntry{
    kNodeLinkOffset, 0, 1, OBELISK_RT_TRACE_STRONG,
    OBELISK_RT_MANAGED_SLOT_CLASS, nullptr};
const obelisk_rt_trace_layout_v1 nodeTraceLayout{
    OBELISK_RT_VERSION, 0, sizeof(void *) * 3, alignof(void *),
    &nodeTraceEntry,    1};
const obelisk_rt_trace_layout_v1 derivedTraceLayout{
    OBELISK_RT_VERSION, 0, sizeof(void *) * 4, alignof(void *),
    &nodeTraceEntry,    1};

obelisk_rt_status nodeValueMethod(obelisk_rt_context *context,
                                  obelisk_rt_gc_lane_v1 *,
                                  obelisk_rt_object_v1 *receiver,
                                  const obelisk_rt_method_argument_v1 *,
                                  uint32_t argumentCount, void *result,
                                  uint64_t resultSize) {
  if (!context || argumentCount != 0 || !result ||
      resultSize != sizeof(uint64_t))
    return OBELISK_RT_INVALID_ARGUMENT;
  return obelisk_rt_v1_object_read(receiver, kNodeValueOffset, result,
                                   resultSize);
}

obelisk_rt_status derivedValueMethod(obelisk_rt_context *context,
                                     obelisk_rt_gc_lane_v1 *,
                                     obelisk_rt_object_v1 *receiver,
                                     const obelisk_rt_method_argument_v1 *,
                                     uint32_t argumentCount, void *result,
                                     uint64_t resultSize) {
  uint64_t value = 0;
  if (!context || argumentCount != 0 || !result ||
      resultSize != sizeof(value) ||
      obelisk_rt_v1_object_read(receiver, kNodeValueOffset, &value,
                                sizeof(value)) != OBELISK_RT_OK)
    return OBELISK_RT_INVALID_ARGUMENT;
  value += 100;
  std::memcpy(result, &value, sizeof(value));
  return OBELISK_RT_OK;
}

obelisk_rt_status throwingValueMethod(
    obelisk_rt_context *, obelisk_rt_gc_lane_v1 *, obelisk_rt_object_v1 *,
    const obelisk_rt_method_argument_v1 *, uint32_t, void *, uint64_t) {
  throw std::bad_alloc();
}

const obelisk_rt_method_descriptor_v1 nodeMethods[]{
    {42, 0, OBELISK_RT_METHOD_NO_BYTECODE, nodeValueMethod, nullptr}};
const obelisk_rt_method_descriptor_v1 derivedMethods[]{
    {42, 0, OBELISK_RT_METHOD_NO_BYTECODE, derivedValueMethod, nullptr}};
const obelisk_rt_method_descriptor_v1 throwingMethods[]{
    {42, 0, OBELISK_RT_METHOD_NO_BYTECODE, throwingValueMethod, nullptr}};
const char nodeName[] = "node";
const char derivedName[] = "derived_node";
const char throwingName[] = "throwing_node";
const obelisk_rt_class_descriptor_v1 nodeDescriptor{OBELISK_RT_VERSION,
                                                    0,
                                                    1,
                                                    sizeof(void *) * 3,
                                                    alignof(void *),
                                                    nullptr,
                                                    nullptr,
                                                    0,
                                                    &nodeTraceLayout,
                                                    nodeMethods,
                                                    std::size(nodeMethods),
                                                    nodeName,
                                                    sizeof(nodeName) - 1,
                                                    nullptr};
const obelisk_rt_random_edge_v1 randomNodeEdge{kNodeLinkOffset,
                                               kNodeValueOffset, UINT64_C(2)};
constexpr uint64_t kRandomNodeValueOffset = sizeof(void *) * 3;
const obelisk_rt_random_variable_v1 randomNodeVariable{
    kRandomNodeValueOffset,
    kNodeValueOffset,
    UINT64_C(1),
    UINT64_MAX,
    UINT64_MAX,
    64,
    OBELISK_RT_RANDOM_VARIABLE_SIGNED};
const obelisk_rt_random_layout_v1 randomNodeLayout{OBELISK_RT_VERSION, 0,
                                                   &randomNodeEdge, 1,
                                                   &randomNodeVariable, 1};
const obelisk_rt_trace_layout_v1 randomNodeTraceLayout{
    OBELISK_RT_VERSION, 0, sizeof(void *) * 4, alignof(void *),
    &nodeTraceEntry,    1};
const char randomNodeName[] = "random_node";
const obelisk_rt_class_descriptor_v1 randomNodeDescriptor{
    OBELISK_RT_VERSION,
    0,
    6,
    sizeof(void *) * 4,
    alignof(void *),
    nullptr,
    nullptr,
    0,
    &randomNodeTraceLayout,
    nodeMethods,
    std::size(nodeMethods),
    randomNodeName,
    sizeof(randomNodeName) - 1,
    &randomNodeLayout};
const obelisk_rt_trace_entry_v1 randomDerivedTraceEntries[]{
    nodeTraceEntry,
    {sizeof(void *) * 4, 0, 1, OBELISK_RT_TRACE_STRONG,
     OBELISK_RT_MANAGED_SLOT_CLASS, nullptr}};
const obelisk_rt_trace_layout_v1 randomDerivedTraceLayout{
    OBELISK_RT_VERSION,        0,
    sizeof(void *) * 6,        alignof(void *),
    randomDerivedTraceEntries, std::size(randomDerivedTraceEntries)};
const obelisk_rt_random_edge_v1 randomDerivedEdge{
    sizeof(void *) * 4, kNodeValueOffset, UINT64_C(4)};
const obelisk_rt_random_variable_v1 randomDerivedVariable{
    sizeof(void *) * 5,
    kNodeValueOffset,
    UINT64_C(8),
    UINT64_MAX,
    UINT64_MAX,
    32,
    0};
const obelisk_rt_random_layout_v1 randomDerivedLayout{OBELISK_RT_VERSION, 0,
                                                      &randomDerivedEdge, 1,
                                                      &randomDerivedVariable,
                                                      1};
const char randomDerivedName[] = "random_derived_node";
const obelisk_rt_class_descriptor_v1 randomDerivedDescriptor{
    OBELISK_RT_VERSION,
    OBELISK_RT_CLASS_FINAL,
    7,
    sizeof(void *) * 6,
    alignof(void *),
    &randomNodeDescriptor,
    nullptr,
    0,
    &randomDerivedTraceLayout,
    nodeMethods,
    std::size(nodeMethods),
    randomDerivedName,
    sizeof(randomDerivedName) - 1,
    &randomDerivedLayout};
const obelisk_rt_class_descriptor_v1 derivedDescriptor{
    OBELISK_RT_VERSION,
    OBELISK_RT_CLASS_FINAL,
    2,
    sizeof(void *) * 4,
    alignof(void *),
    &nodeDescriptor,
    nullptr,
    0,
    &derivedTraceLayout,
    derivedMethods,
    std::size(derivedMethods),
    derivedName,
    sizeof(derivedName) - 1,
    nullptr};
const obelisk_rt_class_descriptor_v1 throwingDescriptor{
    OBELISK_RT_VERSION,
    OBELISK_RT_CLASS_FINAL,
    5,
    sizeof(void *) * 3,
    alignof(void *),
    &nodeDescriptor,
    nullptr,
    0,
    &nodeTraceLayout,
    throwingMethods,
    std::size(throwingMethods),
    throwingName,
    sizeof(throwingName) - 1,
    nullptr};
const obelisk_rt_trace_layout_v1 planeTraceLayout{
    OBELISK_RT_VERSION, 0, sizeof(void *) * 3, alignof(void *), nullptr, 0};
const char planeName[] = "plane_object";
const obelisk_rt_class_descriptor_v1 planeDescriptor{OBELISK_RT_VERSION,
                                                     OBELISK_RT_CLASS_FINAL,
                                                     3,
                                                     sizeof(void *) * 3,
                                                     alignof(void *),
                                                     nullptr,
                                                     nullptr,
                                                     0,
                                                     &planeTraceLayout,
                                                     nullptr,
                                                     0,
                                                     planeName,
                                                     sizeof(planeName) - 1,
                                                     nullptr};
const obelisk_rt_trace_entry_v1 weakTraceEntry{
    sizeof(void *), 0, 1, OBELISK_RT_TRACE_WEAK,
    OBELISK_RT_MANAGED_SLOT_CLASS, nullptr};
const obelisk_rt_trace_layout_v1 weakTraceLayout{
    OBELISK_RT_VERSION, 0, sizeof(void *) * 2, alignof(void *),
    &weakTraceEntry,    1};
const char weakName[] = "weak_reference";
const obelisk_rt_class_descriptor_v1 weakDescriptor{
    OBELISK_RT_VERSION,
    OBELISK_RT_CLASS_FINAL | OBELISK_RT_CLASS_WEAK_WRAPPER,
    4,
    sizeof(void *) * 2,
    alignof(void *),
    nullptr,
    nullptr,
    0,
    &weakTraceLayout,
    nullptr,
    0,
    weakName,
    sizeof(weakName) - 1,
    nullptr};

class ManagedHeapTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
    ASSERT_NE(context, nullptr);
    ASSERT_EQ(obelisk_rt_v1_gc_lane_create(context, &lane), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);
  }

  void TearDown() override {
    if (lane) {
      EXPECT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
      EXPECT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
    }
    obelisk_rt_v1_context_destroy(context);
  }

  obelisk_rt_context *context = nullptr;
  obelisk_rt_gc_lane_v1 *lane = nullptr;
};

TEST_F(ManagedHeapTest, NarrowBitInsertPreservesPackedStorageAndHandles) {
  obelisk_rt_object_v1 *object = nullptr;
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &planeDescriptor, &object),
            OBELISK_RT_OK);

  std::array<uint8_t, 16> bytes{};
  ASSERT_EQ(obelisk_rt_v1_object_bits_insert(
                object, sizeof(void *), 128, 4, 1, UINT64_C(0xa5), 8),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_read(object, sizeof(void *), bytes.data(),
                                      bytes.size()),
            OBELISK_RT_OK);
  EXPECT_EQ(bytes[0], 0x50);
  EXPECT_EQ(bytes[1], 0x0a);

  bytes.fill(0);
  ASSERT_EQ(obelisk_rt_v1_object_write(object, sizeof(void *), bytes.data(),
                                       bytes.size()),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_bits_insert(
                object, sizeof(void *), 128, -3, 1, UINT64_MAX, 8),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_read(object, sizeof(void *), bytes.data(),
                                      bytes.size()),
            OBELISK_RT_OK);
  EXPECT_EQ(bytes[0], 0x1f);

  std::array<uint8_t, 16> before = bytes;
  EXPECT_EQ(obelisk_rt_v1_object_bits_insert(
                object, sizeof(void *), 128, INT64_MAX, 0, 0, 8),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_read(object, sizeof(void *), bytes.data(),
                                      bytes.size()),
            OBELISK_RT_OK);
  EXPECT_EQ(bytes, before);

  obelisk_rt_object_v1 *node = nullptr;
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &nodeDescriptor, &node),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_object_bits_insert(
                node, kNodeLinkOffset, 64, 0, 1, UINT64_C(1), 1),
            OBELISK_RT_INVALID_ARGUMENT);
}

TEST_F(RuntimeTest, DesignatedFormatWarnsAndContinuesOnArgumentMismatch) {
  TempDirectory temporary;
  uint32_t descriptor = open(temporary.file("designated-format.bin"), "w+b");
  uint32_t designated =
      OBELISK_RT_ARG_FORMAT_STRING | OBELISK_RT_ARG_DESIGNATED_FORMAT;
  testing::internal::CaptureStderr();
  obelisk_rt_arg_v1 missing = stringArg("%s", designated);
  EXPECT_EQ(obelisk_rt_v1_display(context, descriptor, 0,
                                  OBELISK_RT_RADIX_DECIMAL, &missing, 1,
                                  nullptr),
            OBELISK_RT_OK);
  LogicValue surplus("10");
  std::vector<obelisk_rt_arg_v1> extra = {stringArg("%s", designated),
                                          stringArg("kept"), surplus.arg()};
  EXPECT_EQ(obelisk_rt_v1_display(context, descriptor, 0,
                                  OBELISK_RT_RADIX_DECIMAL, extra.data(),
                                  extra.size(), nullptr),
            OBELISK_RT_OK);
  std::string warnings = testing::internal::GetCapturedStderr();
  EXPECT_NE(warnings.find("not enough arguments"), std::string::npos);
  EXPECT_NE(warnings.find("1 extra argument"), std::string::npos);

  ASSERT_EQ(obelisk_rt_v1_file_flush(context, descriptor), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_rewind(context, descriptor), OBELISK_RT_OK);
  char bytes[32]{};
  uint64_t read = 0;
  ASSERT_EQ(
      obelisk_rt_v1_file_read(context, descriptor, bytes, sizeof(bytes), &read),
      OBELISK_RT_OK);
  EXPECT_EQ(std::string(bytes, static_cast<size_t>(read)), "<%s>kept");
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);
}

TEST_F(ManagedHeapTest, FormatsClassHandlesAsSingularPatterns) {
  obelisk_rt_object_v1 *object = nullptr;
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &nodeDescriptor, &object),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *nullObject = nullptr;
  const obelisk_rt_arg_v1 arguments[] = {
      {OBELISK_RT_ARG_MANAGED_OBJECT, 0, 0, &object, nullptr},
      {OBELISK_RT_ARG_MANAGED_OBJECT, 0, 0, &nullObject, nullptr}};
  obelisk_rt_format_env_v1 environment{};
  environment.time_multiplier = 1;
  RuntimeBuffer output;
  ASSERT_EQ(obelisk_rt_v1_format(context, "%p|%0p", 6, arguments,
                                 std::size(arguments), &environment,
                                 output.out()),
            OBELISK_RT_OK);
  EXPECT_EQ(output.str(),
            "class@" + std::to_string(obelisk_rt_v1_object_id(object)) +
                "|null");

  obelisk_rt_string_v1 string = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "not-a-class", 11, &string),
            OBELISK_RT_OK);
  ASSERT_EQ(string & UINT64_C(3), UINT64_C(0));
  auto *notClass = reinterpret_cast<obelisk_rt_object_v1 *>(
      static_cast<uintptr_t>(string));
  const obelisk_rt_arg_v1 invalid[] = {
      {OBELISK_RT_ARG_MANAGED_OBJECT, 0, 0, &notClass, nullptr}};
  EXPECT_EQ(obelisk_rt_v1_format(context, "%p", 2, invalid,
                                 std::size(invalid), &environment,
                                 output.out()),
            OBELISK_RT_INVALID_HANDLE);
}

TEST_F(ManagedHeapTest, CollectsCyclesAndClearsWeakReferences) {
  obelisk_rt_object_v1 *first = nullptr;
  obelisk_rt_object_v1 *second = nullptr;
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &nodeDescriptor, &first),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &nodeDescriptor, &second),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_field_store(first, kNodeLinkOffset, second),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_field_store(second, kNodeLinkOffset, first),
            OBELISK_RT_OK);

  obelisk_rt_gc_root_v1 firstRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &firstRoot, &first),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *weak = nullptr;
  ASSERT_EQ(obelisk_rt_v1_weak_create(lane, &weakDescriptor, second, &weak),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_object_is_instance(weak, &weakDescriptor), 1u);
  obelisk_rt_gc_root_v1 weakRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &weakRoot, &weak), OBELISK_RT_OK);

  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  obelisk_rt_gc_statistics_v1 statistics{};
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(context, &statistics), OBELISK_RT_OK);
  EXPECT_EQ(statistics.live_objects, 3u);

  first = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  obelisk_rt_object_v1 *referent =
      reinterpret_cast<obelisk_rt_object_v1 *>(uintptr_t{1});
  ASSERT_EQ(obelisk_rt_v1_weak_get(weak, &referent), OBELISK_RT_OK);
  EXPECT_EQ(referent, nullptr);
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(context, &statistics), OBELISK_RT_OK);
  EXPECT_EQ(statistics.live_objects, 1u);
  EXPECT_EQ(statistics.reclaimed_objects, 2u);

  ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane, &weakRoot), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane, &firstRoot), OBELISK_RT_OK);
}

TEST_F(ManagedHeapTest, DiscoversActiveRandomObjectGraphByIdentity) {
  obelisk_rt_object_v1 *root = nullptr;
  obelisk_rt_object_v1 *child = nullptr;
  obelisk_rt_object_v1 *derivedChild = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_object_allocate(lane, &randomDerivedDescriptor, &root),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &randomNodeDescriptor, &child),
            OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_object_allocate(lane, &randomNodeDescriptor, &derivedChild),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_field_store(root, kNodeLinkOffset, child),
            OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_object_field_store(root, sizeof(void *) * 4, derivedChild),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_field_store(child, kNodeLinkOffset, root),
            OBELISK_RT_OK);

  obelisk_rt_random_graph_v1 *graph = nullptr;
  ASSERT_EQ(obelisk_rt_v1_random_graph_discover(lane, root, &graph),
            OBELISK_RT_OK);
  ASSERT_NE(graph, nullptr);
  EXPECT_EQ(obelisk_rt_v1_random_graph_size(graph), 3u);
  EXPECT_EQ(obelisk_rt_v1_random_graph_object(graph, 0), root);
  EXPECT_EQ(obelisk_rt_v1_random_graph_object(graph, 1), child);
  EXPECT_EQ(obelisk_rt_v1_random_graph_object(graph, 2), derivedChild);
  EXPECT_EQ(obelisk_rt_v1_random_graph_object(graph, 3), nullptr);
  EXPECT_EQ(obelisk_rt_v1_random_graph_object_descriptor(graph, 0),
            &randomDerivedDescriptor);
  EXPECT_EQ(obelisk_rt_v1_random_graph_object_descriptor(graph, 1),
            &randomNodeDescriptor);
  EXPECT_EQ(obelisk_rt_v1_random_graph_object_descriptor(graph, 2),
            &randomNodeDescriptor);
  EXPECT_EQ(obelisk_rt_v1_random_graph_object_descriptor(graph, 3), nullptr);
  EXPECT_EQ(obelisk_rt_v1_random_graph_variable_count(graph), 4u);
  obelisk_rt_object_v1 *variableObject = nullptr;
  const obelisk_rt_random_variable_v1 *variable = nullptr;
  ASSERT_EQ(obelisk_rt_v1_random_graph_variable(
                graph, 0, &variableObject, &variable),
            OBELISK_RT_OK);
  EXPECT_EQ(variableObject, root);
  EXPECT_EQ(variable, &randomNodeVariable);
  ASSERT_EQ(obelisk_rt_v1_random_graph_variable(
                graph, 1, &variableObject, &variable),
            OBELISK_RT_OK);
  EXPECT_EQ(variableObject, root);
  EXPECT_EQ(variable, &randomDerivedVariable);
  ASSERT_EQ(obelisk_rt_v1_random_graph_variable(
                graph, 2, &variableObject, &variable),
            OBELISK_RT_OK);
  EXPECT_EQ(variableObject, child);
  EXPECT_EQ(variable, &randomNodeVariable);
  ASSERT_EQ(obelisk_rt_v1_random_graph_variable(
                graph, 3, &variableObject, &variable),
            OBELISK_RT_OK);
  EXPECT_EQ(variableObject, derivedChild);
  EXPECT_EQ(variable, &randomNodeVariable);
  EXPECT_EQ(obelisk_rt_v1_random_graph_variable(
                graph, 4, &variableObject, &variable),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(variableObject, nullptr);
  EXPECT_EQ(variable, nullptr);

  obelisk_rt_object_v1 *referencedObject = nullptr;
  const obelisk_rt_random_variable_v1 *referencedVariable = nullptr;
  uint64_t graphVariableIndex = UINT64_MAX;
  const obelisk_rt_random_variable_reference_v1 rootValueReference{
      nullptr,
      0,
      kRandomNodeValueOffset,
      64,
      OBELISK_RT_RANDOM_VARIABLE_SIGNED};
  ASSERT_EQ(obelisk_rt_v1_random_graph_resolve_variable(
                graph, 0, &rootValueReference, &referencedObject,
                &referencedVariable, &graphVariableIndex),
            OBELISK_RT_OK);
  EXPECT_EQ(referencedObject, root);
  EXPECT_EQ(referencedVariable, &randomNodeVariable);
  EXPECT_EQ(graphVariableIndex, 0u);

  const uint64_t childPath[]{kNodeLinkOffset};
  const obelisk_rt_random_variable_reference_v1 childValueReference{
      childPath,
      std::size(childPath),
      kRandomNodeValueOffset,
      64,
      OBELISK_RT_RANDOM_VARIABLE_SIGNED};
  ASSERT_EQ(obelisk_rt_v1_random_graph_resolve_variable(
                graph, 0, &childValueReference, &referencedObject,
                &referencedVariable, &graphVariableIndex),
            OBELISK_RT_OK);
  EXPECT_EQ(referencedObject, child);
  EXPECT_EQ(referencedVariable, &randomNodeVariable);
  EXPECT_EQ(graphVariableIndex, 2u);
  EXPECT_EQ(obelisk_rt_v1_random_graph_resolve_variable(
                graph, 2, &childValueReference, &referencedObject,
                &referencedVariable, &graphVariableIndex),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(referencedObject, nullptr);
  EXPECT_EQ(referencedVariable, nullptr);
  EXPECT_EQ(graphVariableIndex, UINT64_MAX);

  const uint64_t aliasPath[]{kNodeLinkOffset, kNodeLinkOffset};
  const obelisk_rt_random_variable_reference_v1 aliasValueReference{
      aliasPath,
      std::size(aliasPath),
      kRandomNodeValueOffset,
      64,
      OBELISK_RT_RANDOM_VARIABLE_SIGNED};
  ASSERT_EQ(obelisk_rt_v1_random_graph_resolve_variable(
                graph, 0, &aliasValueReference, &referencedObject,
                &referencedVariable, &graphVariableIndex),
            OBELISK_RT_OK);
  EXPECT_EQ(referencedObject, root);
  EXPECT_EQ(referencedVariable, &randomNodeVariable);
  EXPECT_EQ(graphVariableIndex, 0u);

  const uint64_t invalidPath[]{kRandomNodeValueOffset};
  const obelisk_rt_random_variable_reference_v1 invalidReference{
      invalidPath,
      std::size(invalidPath),
      kRandomNodeValueOffset,
      64,
      OBELISK_RT_RANDOM_VARIABLE_SIGNED};
  EXPECT_EQ(obelisk_rt_v1_random_graph_resolve_variable(
                graph, 0, &invalidReference, &referencedObject,
                &referencedVariable, &graphVariableIndex),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(referencedObject, nullptr);
  EXPECT_EQ(referencedVariable, nullptr);
  EXPECT_EQ(graphVariableIndex, UINT64_MAX);

  // The graph owns exact roots while callers compose a solver plan.
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  EXPECT_NE(obelisk_rt_v1_object_id(root), 0u);
  EXPECT_NE(obelisk_rt_v1_object_id(child), 0u);
  EXPECT_NE(obelisk_rt_v1_object_id(derivedChild), 0u);
  obelisk_rt_v1_random_graph_destroy(graph);

  uint64_t disabled = 7;
  ASSERT_EQ(obelisk_rt_v1_object_write(root, kNodeValueOffset, &disabled,
                                       sizeof(disabled)),
            OBELISK_RT_OK);
  graph = nullptr;
  ASSERT_EQ(obelisk_rt_v1_random_graph_discover(lane, root, &graph),
            OBELISK_RT_OK);
  ASSERT_NE(graph, nullptr);
  EXPECT_EQ(obelisk_rt_v1_random_graph_size(graph), 1u);
  EXPECT_EQ(obelisk_rt_v1_random_graph_object(graph, 0), root);
  EXPECT_EQ(obelisk_rt_v1_random_graph_object_descriptor(graph, 0),
            &randomDerivedDescriptor);
  EXPECT_EQ(obelisk_rt_v1_random_graph_variable_count(graph), 1u);
  ASSERT_EQ(obelisk_rt_v1_random_graph_variable(
                graph, 0, &variableObject, &variable),
            OBELISK_RT_OK);
  EXPECT_EQ(variableObject, root);
  EXPECT_EQ(variable, &randomDerivedVariable);
  ASSERT_EQ(obelisk_rt_v1_random_graph_resolve_variable(
                graph, 0, &rootValueReference, &referencedObject,
                &referencedVariable, &graphVariableIndex),
            OBELISK_RT_OK);
  EXPECT_EQ(referencedObject, root);
  EXPECT_EQ(referencedVariable, &randomNodeVariable);
  EXPECT_EQ(graphVariableIndex, UINT64_MAX);
  EXPECT_EQ(obelisk_rt_v1_random_graph_resolve_variable(
                graph, 0, &childValueReference, &referencedObject,
                &referencedVariable, &graphVariableIndex),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(referencedObject, nullptr);
  EXPECT_EQ(referencedVariable, nullptr);
  EXPECT_EQ(graphVariableIndex, UINT64_MAX);
  obelisk_rt_v1_random_graph_destroy(graph);
}

TEST_F(ManagedHeapTest,
       ClassAssociativeKeysPreserveIdentityNullAndDerivedObjects) {
  const obelisk_rt_element_type_v1 wordElement{
      OBELISK_RT_VERSION, OBELISK_RT_ELEMENT_BITS, 91, 0,      0,
      sizeof(uint64_t),   alignof(uint64_t),       64, nullptr};
  obelisk_rt_object_v1 *objects[2] = {};
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &nodeDescriptor, &objects[0]),
            OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_object_allocate(lane, &derivedDescriptor, &objects[1]),
      OBELISK_RT_OK);
  obelisk_rt_gc_root_range_v1 objectRoots{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_range_push(lane, &objectRoots, objects, 2),
            OBELISK_RT_OK);

  obelisk_rt_object_v1 *array = nullptr;
  ASSERT_EQ(obelisk_rt_v1_assoc_create(lane, &wordElement,
                                       OBELISK_RT_ASSOC_KEY_CLASS, 0, &array),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 arrayRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &arrayRoot, &array),
            OBELISK_RT_OK);

  obelisk_rt_object_v1 *expectedKeys[] = {nullptr, objects[0], objects[1]};
  for (uint64_t index = 0; index != std::size(expectedKeys); ++index) {
    obelisk_rt_assoc_key_v1 key{OBELISK_RT_ASSOC_KEY_CLASS, 0, 0};
    key.object = expectedKeys[index];
    uint64_t value = index + 10;
    ASSERT_EQ(obelisk_rt_v1_assoc_write(lane, array, &key, &value, nullptr),
              OBELISK_RT_OK);
  }

  ASSERT_EQ(obelisk_rt_v1_gc_root_range_pop(lane, &objectRoots),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  for (uint64_t index = 0; index != std::size(expectedKeys); ++index) {
    obelisk_rt_assoc_key_v1 key{OBELISK_RT_ASSOC_KEY_CLASS, 0, 0};
    key.object = expectedKeys[index];
    uint64_t value = 0;
    uint32_t present = 0;
    ASSERT_EQ(obelisk_rt_v1_assoc_read(array, &key, &value, nullptr, &present),
              OBELISK_RT_OK);
    EXPECT_EQ(present, 1u);
    EXPECT_EQ(value, index + 10);
  }

  ASSERT_EQ(obelisk_rt_v1_gc_set_threshold(context, 1), OBELISK_RT_OK);
  obelisk_rt_object_v1 *cursorOnly = nullptr;
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &nodeDescriptor, &cursorOnly),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 cursorRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &cursorRoot, &cursorOnly),
            OBELISK_RT_OK);
  obelisk_rt_assoc_key_v1 absentCursor{OBELISK_RT_ASSOC_KEY_CLASS, 0, 0};
  absentCursor.object = cursorOnly;
  ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane, &cursorRoot), OBELISK_RT_OK);
  uint32_t cursorSuccess = 1;
  ASSERT_EQ(obelisk_rt_v1_assoc_next(lane, array, &absentCursor,
                                     &cursorSuccess),
            OBELISK_RT_OK);
  EXPECT_EQ(cursorSuccess, 0u);

  obelisk_rt_object_v1 *copy = nullptr;
  ASSERT_EQ(obelisk_rt_v1_container_clone(lane, array, &copy), OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 copyRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &copyRoot, &copy), OBELISK_RT_OK);
  obelisk_rt_assoc_key_v1 derivedKey{OBELISK_RT_ASSOC_KEY_CLASS, 0, 0};
  derivedKey.object = expectedKeys[2];
  uint64_t value = 0;
  uint32_t present = 0;
  ASSERT_EQ(
      obelisk_rt_v1_assoc_read(copy, &derivedKey, &value, nullptr, &present),
      OBELISK_RT_OK);
  EXPECT_EQ(present, 1u);
  EXPECT_EQ(value, 12u);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &copyRoot), OBELISK_RT_OK);

  obelisk_rt_object_v1 *path = nullptr;
  ASSERT_EQ(obelisk_rt_v1_reference_path_assoc_create(
                lane, array, &derivedKey, nullptr, 0, 0, &path),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 pathRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &pathRoot, &path), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_assoc_delete(array, &derivedKey), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  uint64_t replacement = 42;
  ASSERT_EQ(
      obelisk_rt_v1_reference_path_store(lane, path, &replacement, nullptr),
      OBELISK_RT_OK);
  value = 0;
  present = 0;
  ASSERT_EQ(obelisk_rt_v1_reference_path_load(path, &value, nullptr, &present),
            OBELISK_RT_OK);
  EXPECT_EQ(present, 1u);
  EXPECT_EQ(value, replacement);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &pathRoot), OBELISK_RT_OK);

  obelisk_rt_assoc_key_v1 cursor{};
  uint32_t success = 0;
  for (obelisk_rt_object_v1 *expected : expectedKeys) {
    ASSERT_EQ(obelisk_rt_v1_assoc_first(lane, array, &cursor, &success),
              OBELISK_RT_OK);
    ASSERT_EQ(success, 1u);
    EXPECT_EQ(cursor.kind, OBELISK_RT_ASSOC_KEY_CLASS);
    EXPECT_EQ(cursor.object, expected);
    ASSERT_EQ(obelisk_rt_v1_assoc_delete(array, &cursor), OBELISK_RT_OK);
  }
  EXPECT_EQ(obelisk_rt_v1_container_size(array), 0u);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &arrayRoot), OBELISK_RT_OK);
}

TEST_F(ManagedHeapTest, TracesContiguousActivationRootRanges) {
  obelisk_rt_object_v1 *slots[2] = {};
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &nodeDescriptor, &slots[0]),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &nodeDescriptor, &slots[1]),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_range_v1 range{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_range_push(lane, &range, slots, 2),
            OBELISK_RT_OK);

  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  obelisk_rt_gc_statistics_v1 statistics{};
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(context, &statistics), OBELISK_RT_OK);
  EXPECT_EQ(statistics.live_objects, 2u);

  slots[0] = nullptr;
  slots[1] = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(context, &statistics), OBELISK_RT_OK);
  EXPECT_EQ(statistics.live_objects, 0u);
  EXPECT_EQ(obelisk_rt_v1_gc_root_range_pop(lane, &range), OBELISK_RT_OK);
}

TEST_F(ManagedHeapTest, TracesManagedValuesInAutomaticAggregateState) {
  obelisk_rt_object_v1 *objects[2] = {};
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &nodeDescriptor, &objects[0]),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &nodeDescriptor, &objects[1]),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *weak[2] = {};
  ASSERT_EQ(
      obelisk_rt_v1_weak_create(lane, &weakDescriptor, objects[0], &weak[0]),
      OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_weak_create(lane, &weakDescriptor, objects[1], &weak[1]),
      OBELISK_RT_OK);
  obelisk_rt_gc_root_range_v1 weakRoots{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_range_push(lane, &weakRoots, weak, 2),
            OBELISK_RT_OK);

  uint8_t initial[sizeof(objects)] = {};
  std::memcpy(initial, objects, sizeof(objects));
  const uint64_t invalidRootOffset = 1;
  uint64_t rolledBackHandle = 0;
  EXPECT_EQ(obelisk_rt_v1_native_state_alloc_with_roots(
                context, sizeof(initial) * 8, initial, nullptr,
                &invalidRootOffset, 1, &rolledBackHandle),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(rolledBackHandle, UINT64_MAX);
  rolledBackHandle = 0;
  EXPECT_EQ(obelisk_rt_v1_native_state_alloc_with_roots(
                context, sizeof(initial) * 8, initial, nullptr, nullptr, 1,
                &rolledBackHandle),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(rolledBackHandle, UINT64_MAX);
  uint64_t handle = UINT64_MAX;
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc(context, sizeof(initial) * 8,
                                             initial, nullptr, &handle),
            OBELISK_RT_OK);
  const uint64_t rootOffsets[] = {0, 64};
  ASSERT_EQ(obelisk_rt_v1_native_state_register_managed_roots(
                context, handle, rootOffsets, std::size(rootOffsets)),
            OBELISK_RT_OK);
  objects[0] = nullptr;
  objects[1] = nullptr;

  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  obelisk_rt_object_v1 *referent = nullptr;
  EXPECT_EQ(obelisk_rt_v1_weak_get(weak[0], &referent), OBELISK_RT_OK);
  EXPECT_NE(referent, nullptr);
  EXPECT_EQ(obelisk_rt_v1_weak_get(weak[1], &referent), OBELISK_RT_OK);
  EXPECT_NE(referent, nullptr);

  ASSERT_EQ(obelisk_rt_v1_native_state_release(context, handle, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_weak_get(weak[0], &referent), OBELISK_RT_OK);
  EXPECT_EQ(referent, nullptr);
  EXPECT_EQ(obelisk_rt_v1_weak_get(weak[1], &referent), OBELISK_RT_OK);
  EXPECT_EQ(referent, nullptr);
  EXPECT_EQ(obelisk_rt_v1_gc_root_range_pop(lane, &weakRoots), OBELISK_RT_OK);
}

TEST_F(ManagedHeapTest, AutomaticStringStatePreservesSSOAndHeapRoots) {
  obelisk_rt_string_v1 small = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "seven!!", 7, &small),
            OBELISK_RT_OK);
  ASSERT_EQ(small & UINT64_C(3), UINT64_C(1));
  const uint64_t rootOffset = 0;
  uint64_t smallHandle = UINT64_MAX;
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc_with_roots(
                context, 64, reinterpret_cast<const uint8_t *>(&small),
                nullptr, &rootOffset, 1, &smallHandle),
            OBELISK_RT_OK);
  obelisk_rt_string_v1 loaded = 0;
  uint8_t dummy[8]{};
  ASSERT_EQ(obelisk_rt_v1_argument_ref_load(
                context, dummy, dummy, 64, nullptr, smallHandle, 0, 64, 8, 0,
                OBELISK_RT_ARGUMENT_VALUE_STRING, &loaded, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(loaded, small);
  ASSERT_EQ(obelisk_rt_v1_native_state_release(context, smallHandle, 0),
            OBELISK_RT_OK);

  obelisk_rt_string_v1 heap = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "heap-backed", 11, &heap),
            OBELISK_RT_OK);
  ASSERT_EQ(heap & UINT64_C(3), UINT64_C(0));
  uint64_t heapHandle = UINT64_MAX;
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc_with_roots(
                context, 64, reinterpret_cast<const uint8_t *>(&heap),
                nullptr, &rootOffset, 1, &heapHandle),
            OBELISK_RT_OK);
  heap = 0;
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_argument_ref_load(
                context, dummy, dummy, 64, nullptr, heapHandle, 0, 64, 8, 0,
                OBELISK_RT_ARGUMENT_VALUE_STRING, &loaded, nullptr),
            OBELISK_RT_OK);
  char scratch[8]{};
  const char *bytes = nullptr;
  uint64_t size = 0;
  ASSERT_EQ(obelisk_rt_v1_string_view(loaded, scratch, &bytes, &size),
            OBELISK_RT_OK);
  EXPECT_EQ(std::string_view(bytes, size), "heap-backed");

  obelisk_rt_string_v1 equal = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "heap-backed", 11, &equal),
            OBELISK_RT_OK);
  ASSERT_NE(equal, loaded);
  ASSERT_EQ(obelisk_rt_v1_argument_ref_store(
                context, dummy, dummy, 64, nullptr, heapHandle, 0, 64, 8, 0,
                OBELISK_RT_ARGUMENT_VALUE_STRING, &equal, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_release(context, heapHandle, 0),
            OBELISK_RT_OK);
}

TEST_F(ManagedHeapTest, AccessesFourStatePlanesAtomicallyAcrossThreads) {
  obelisk_rt_object_v1 *object = nullptr;
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &planeDescriptor, &object),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 root{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &root, &object), OBELISK_RT_OK);
  constexpr uint64_t first = UINT64_C(0xaaaaaaaaaaaaaaaa);
  constexpr uint64_t second = UINT64_C(0x5555555555555555);
  uint64_t initialUnknown = ~first;
  ASSERT_EQ(obelisk_rt_v1_object_write_planes(object, sizeof(void *), &first,
                                              &initialUnknown, sizeof(first)),
            OBELISK_RT_OK);

  std::atomic<bool> inconsistent{false};
  std::thread writer([&] {
    for (unsigned iteration = 0; iteration != 10000; ++iteration) {
      uint64_t value = (iteration & 1) ? first : second;
      uint64_t unknown = ~value;
      if (obelisk_rt_v1_object_write_planes(object, sizeof(void *), &value,
                                            &unknown,
                                            sizeof(value)) != OBELISK_RT_OK) {
        inconsistent.store(true, std::memory_order_relaxed);
        return;
      }
    }
  });
  std::thread reader([&] {
    for (unsigned iteration = 0; iteration != 10000; ++iteration) {
      uint64_t value = 0;
      uint64_t unknown = 0;
      if (obelisk_rt_v1_object_read_planes(object, sizeof(void *), &value,
                                           &unknown,
                                           sizeof(value)) != OBELISK_RT_OK ||
          unknown != ~value) {
        inconsistent.store(true, std::memory_order_relaxed);
        return;
      }
    }
  });
  writer.join();
  reader.join();
  EXPECT_FALSE(inconsistent.load(std::memory_order_relaxed));
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &root), OBELISK_RT_OK);
}

TEST_F(ManagedHeapTest, DispatchesOverridesAndShallowCopiesDynamicType) {
  obelisk_rt_object_v1 *object = nullptr;
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &derivedDescriptor, &object),
            OBELISK_RT_OK);
  const uint64_t value = 23;
  const uint64_t extra = 99;
  ASSERT_EQ(obelisk_rt_v1_object_write(object, kNodeValueOffset, &value,
                                       sizeof(value)),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_write(object, kDerivedExtraOffset, &extra,
                                       sizeof(extra)),
            OBELISK_RT_OK);
  EXPECT_TRUE(obelisk_rt_v1_object_is_instance(object, &derivedDescriptor));
  EXPECT_TRUE(obelisk_rt_v1_object_is_instance(object, &nodeDescriptor));

  uint64_t result = 0;
  ASSERT_EQ(obelisk_rt_v1_method_invoke(lane, object, 0, 42, nullptr, 0,
                                        &result, sizeof(result)),
            OBELISK_RT_OK);
  EXPECT_EQ(result, 123u);
  EXPECT_EQ(obelisk_rt_v1_method_invoke(lane, object, 0, 43, nullptr, 0,
                                        &result, sizeof(result)),
            OBELISK_RT_LAYOUT_MISMATCH);

  obelisk_rt_object_v1 *copy = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_object_shallow_copy(lane, &nodeDescriptor, object, &copy),
      OBELISK_RT_OK);
  EXPECT_TRUE(obelisk_rt_v1_object_is_instance(copy, &nodeDescriptor));
  EXPECT_TRUE(obelisk_rt_v1_object_is_instance(copy, &derivedDescriptor));
  ASSERT_EQ(obelisk_rt_v1_method_invoke(lane, copy, 0, 42, nullptr, 0, &result,
                                        sizeof(result)),
            OBELISK_RT_OK);
  EXPECT_EQ(result, 123u);
  result = 0;
  ASSERT_EQ(obelisk_rt_v1_object_read(copy, kDerivedExtraOffset, &result,
                                      sizeof(result)),
            OBELISK_RT_OK);
  EXPECT_EQ(result, extra);
  EXPECT_NE(obelisk_rt_v1_object_id(object), obelisk_rt_v1_object_id(copy));

  obelisk_rt_object_v1 *castResult = nullptr;
  ASSERT_EQ(obelisk_rt_v1_object_cast(object, &nodeDescriptor, &castResult),
            OBELISK_RT_OK);
  EXPECT_EQ(castResult, object);
  castResult = object;
  ASSERT_EQ(obelisk_rt_v1_object_cast(copy, &derivedDescriptor, &castResult),
            OBELISK_RT_OK);
  EXPECT_EQ(castResult, copy);
  castResult = object;
  ASSERT_EQ(obelisk_rt_v1_object_cast(nullptr, &derivedDescriptor, &castResult),
            OBELISK_RT_OK);
  EXPECT_EQ(castResult, nullptr);
}

TEST_F(ManagedHeapTest, ContainsExceptionsFromNativeMethodCallbacks) {
  obelisk_rt_object_v1 *object = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_object_allocate(lane, &throwingDescriptor, &object),
      OBELISK_RT_OK);
  uint64_t result = 0;
  EXPECT_EQ(obelisk_rt_v1_method_invoke(lane, object, 0, 42, nullptr, 0,
                                        &result, sizeof(result)),
            OBELISK_RT_OUT_OF_MEMORY);

  // The receiver root must be removed even when a foreign callback throws.
  obelisk_rt_gc_root_v1 root{};
  EXPECT_EQ(obelisk_rt_v1_gc_root_push(lane, &root, &object), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &root), OBELISK_RT_OK);
}

TEST_F(ManagedHeapTest, UsesChunkAllocationForSmallObjectChurn) {
  ASSERT_EQ(obelisk_rt_v1_gc_set_threshold(context, UINT64_MAX), OBELISK_RT_OK);
  constexpr uint64_t objectCount = 200'000;
  for (uint64_t index = 0; index != objectCount; ++index) {
    obelisk_rt_object_v1 *object = nullptr;
    ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &nodeDescriptor, &object),
              OBELISK_RT_OK);
  }
  obelisk_rt_gc_statistics_v1 statistics{};
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(context, &statistics), OBELISK_RT_OK);
  EXPECT_EQ(statistics.allocated_objects, objectCount);
  EXPECT_EQ(statistics.large_allocation_count, 0u);
  EXPECT_LE(statistics.chunk_allocation_count, 7u);

  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(context, &statistics), OBELISK_RT_OK);
  EXPECT_EQ(statistics.live_objects, 0u);
  EXPECT_EQ(statistics.reclaimed_objects, objectCount);
  EXPECT_LE(statistics.cached_empty_chunks, 2u);
}

TEST_F(ManagedHeapTest, PinsAndStaticSlotsArePreciseRoots) {
  obelisk_rt_object_v1 *staticObject = nullptr;
  obelisk_rt_object_v1 *pinnedObject = nullptr;
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &nodeDescriptor, &staticObject),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &nodeDescriptor, &pinnedObject),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_static_root_register(context, &staticObject),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_pin(context, pinnedObject), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  obelisk_rt_gc_statistics_v1 statistics{};
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(context, &statistics), OBELISK_RT_OK);
  EXPECT_EQ(statistics.live_objects, 2u);

  ASSERT_EQ(obelisk_rt_v1_gc_static_root_unregister(context, &staticObject),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_unpin(context, pinnedObject), OBELISK_RT_OK);
  staticObject = nullptr;
  pinnedObject = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(context, &statistics), OBELISK_RT_OK);
  EXPECT_EQ(statistics.live_objects, 0u);
}

TEST_F(ManagedHeapTest, AutomaticCollectionsClearWeakReferencesDuringChurn) {
  ASSERT_EQ(obelisk_rt_v1_gc_set_threshold(context, 1024), OBELISK_RT_OK);
  obelisk_rt_object_v1 *referent = nullptr;
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &nodeDescriptor, &referent),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *weak = nullptr;
  ASSERT_EQ(obelisk_rt_v1_weak_create(lane, &weakDescriptor, referent, &weak),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 weakRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &weakRoot, &weak), OBELISK_RT_OK);
  referent = nullptr;

  for (size_t index = 0; index != 1000; ++index) {
    obelisk_rt_object_v1 *garbage = nullptr;
    ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &nodeDescriptor, &garbage),
              OBELISK_RT_OK);
  }
  ASSERT_EQ(obelisk_rt_v1_weak_get(weak, &referent), OBELISK_RT_OK);
  EXPECT_EQ(referent, nullptr);
  obelisk_rt_gc_statistics_v1 statistics{};
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(context, &statistics), OBELISK_RT_OK);
  EXPECT_GT(statistics.collection_count, 0u);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &weakRoot), OBELISK_RT_OK);
}

TEST(ManagedHeap, CoordinatesConcurrentLaneSafepoints) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  constexpr size_t laneCount = 4;
  std::array<obelisk_rt_gc_lane_v1 *, laneCount> lanes{};
  for (auto &lane : lanes)
    ASSERT_EQ(obelisk_rt_v1_gc_lane_create(context, &lane), OBELISK_RT_OK);

  std::atomic<size_t> ready{0};
  std::atomic<bool> start{false};
  std::atomic<bool> collectionDone{false};
  std::array<obelisk_rt_status, laneCount> statuses{};
  std::vector<std::thread> workers;
  for (size_t index = 0; index != laneCount; ++index) {
    workers.emplace_back([&, index] {
      obelisk_rt_gc_lane_v1 *lane = lanes[index];
      statuses[index] = obelisk_rt_v1_gc_lane_enter(lane);
      obelisk_rt_object_v1 *rooted = nullptr;
      obelisk_rt_gc_root_v1 root{};
      if (statuses[index] == OBELISK_RT_OK)
        statuses[index] =
            obelisk_rt_v1_object_allocate(lane, &nodeDescriptor, &rooted);
      if (statuses[index] == OBELISK_RT_OK)
        statuses[index] = obelisk_rt_v1_gc_root_push(lane, &root, &rooted);
      ready.fetch_add(1);
      while (!start.load())
        std::this_thread::yield();
      if (statuses[index] == OBELISK_RT_OK) {
        if (index == 0) {
          statuses[index] = obelisk_rt_v1_gc_collect(lane);
          collectionDone.store(true);
        } else {
          while (!collectionDone.load() && statuses[index] == OBELISK_RT_OK)
            statuses[index] = obelisk_rt_v1_gc_safepoint(lane);
        }
      }
      if (root.cookie) {
        EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &root), OBELISK_RT_OK);
      }
      if (statuses[index] == OBELISK_RT_OK)
        statuses[index] = obelisk_rt_v1_gc_lane_leave(lane);
    });
  }
  while (ready.load() != laneCount)
    std::this_thread::yield();
  start.store(true);
  for (std::thread &worker : workers)
    worker.join();
  for (obelisk_rt_status status : statuses)
    EXPECT_EQ(status, OBELISK_RT_OK);
  for (auto *lane : lanes)
    EXPECT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(ManagedHeap, ConcurrentSmallObjectAllocationUsesChunkedTLSCaches) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_set_threshold(context, UINT64_MAX), OBELISK_RT_OK);
  constexpr size_t laneCount = 4;
  constexpr size_t objectsPerLane = 50'000;
  std::array<obelisk_rt_gc_lane_v1 *, laneCount> lanes{};
  for (auto &lane : lanes)
    ASSERT_EQ(obelisk_rt_v1_gc_lane_create(context, &lane), OBELISK_RT_OK);

  std::array<obelisk_rt_status, laneCount> statuses{};
  std::array<std::vector<uint64_t>, laneCount> identities;
  std::vector<std::thread> workers;
  for (size_t index = 0; index != laneCount; ++index) {
    workers.emplace_back([&, index] {
      obelisk_rt_gc_lane_v1 *lane = lanes[index];
      statuses[index] = obelisk_rt_v1_gc_lane_enter(lane);
      identities[index].reserve(objectsPerLane);
      for (size_t objectIndex = 0;
           objectIndex != objectsPerLane && statuses[index] == OBELISK_RT_OK;
           ++objectIndex) {
        obelisk_rt_object_v1 *object = nullptr;
        statuses[index] =
            obelisk_rt_v1_object_allocate(lane, &nodeDescriptor, &object);
        if (statuses[index] == OBELISK_RT_OK)
          identities[index].push_back(obelisk_rt_v1_object_id(object));
      }
      if (statuses[index] == OBELISK_RT_OK)
        statuses[index] = obelisk_rt_v1_gc_lane_leave(lane);
    });
  }
  for (std::thread &worker : workers)
    worker.join();
  for (obelisk_rt_status status : statuses)
    EXPECT_EQ(status, OBELISK_RT_OK);

  std::vector<uint64_t> allIdentities;
  allIdentities.reserve(laneCount * objectsPerLane);
  for (const auto &laneIdentities : identities)
    allIdentities.insert(allIdentities.end(), laneIdentities.begin(),
                         laneIdentities.end());
  std::sort(allIdentities.begin(), allIdentities.end());
  EXPECT_EQ(allIdentities.size(), laneCount * objectsPerLane);
  EXPECT_EQ(std::adjacent_find(allIdentities.begin(), allIdentities.end()),
            allIdentities.end());

  obelisk_rt_gc_statistics_v1 statistics{};
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(context, &statistics), OBELISK_RT_OK);
  EXPECT_EQ(statistics.allocated_objects, laneCount * objectsPerLane);
  EXPECT_EQ(statistics.large_allocation_count, 0u);
  EXPECT_LE(statistics.chunk_allocation_count, 7u);
  for (auto *lane : lanes)
    EXPECT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(ManagedHeap, SerializesConcurrentCollectionRequestsWithoutDeadlock) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  constexpr size_t laneCount = 4;
  std::array<obelisk_rt_gc_lane_v1 *, laneCount> lanes{};
  for (auto &lane : lanes)
    ASSERT_EQ(obelisk_rt_v1_gc_lane_create(context, &lane), OBELISK_RT_OK);

  std::atomic<size_t> ready{0};
  std::atomic<bool> start{false};
  std::array<obelisk_rt_status, laneCount> statuses{};
  std::vector<std::thread> workers;
  for (size_t index = 0; index != laneCount; ++index) {
    workers.emplace_back([&, index] {
      obelisk_rt_gc_lane_v1 *lane = lanes[index];
      statuses[index] = obelisk_rt_v1_gc_lane_enter(lane);
      ready.fetch_add(1);
      while (!start.load())
        std::this_thread::yield();
      if (statuses[index] == OBELISK_RT_OK)
        statuses[index] = obelisk_rt_v1_gc_collect(lane);
      if (statuses[index] == OBELISK_RT_OK)
        statuses[index] = obelisk_rt_v1_gc_lane_leave(lane);
    });
  }
  while (ready.load() != laneCount)
    std::this_thread::yield();
  start.store(true);
  for (std::thread &worker : workers)
    worker.join();

  for (obelisk_rt_status status : statuses)
    EXPECT_EQ(status, OBELISK_RT_OK);
  obelisk_rt_gc_statistics_v1 statistics{};
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(context, &statistics), OBELISK_RT_OK);
  EXPECT_EQ(statistics.collection_count, laneCount);
  for (auto *lane : lanes)
    EXPECT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST(ManagedHeap, RejectsMalformedClassLayouts) {
  obelisk_rt_class_descriptor_v1 malformed = nodeDescriptor;
  malformed.class_id = 0;
  EXPECT_EQ(obelisk_rt_v1_class_validate(&malformed),
            OBELISK_RT_INVALID_DESIGN);
  malformed = nodeDescriptor;
  obelisk_rt_trace_entry_v1 badEntry = nodeTraceEntry;
  badEntry.offset = 1;
  obelisk_rt_trace_layout_v1 badLayout = nodeTraceLayout;
  badLayout.entries = &badEntry;
  malformed.layout = &badLayout;
  EXPECT_EQ(obelisk_rt_v1_class_validate(&malformed),
            OBELISK_RT_INVALID_DESIGN);

  obelisk_rt_random_edge_v1 badRandomEdge = randomNodeEdge;
  badRandomEdge.handle_offset = kNodeValueOffset;
  obelisk_rt_random_layout_v1 badRandomLayout = randomNodeLayout;
  badRandomLayout.edges = &badRandomEdge;
  malformed = randomNodeDescriptor;
  malformed.random_layout = &badRandomLayout;
  EXPECT_EQ(obelisk_rt_v1_class_validate(&malformed),
            OBELISK_RT_INVALID_DESIGN);
  badRandomEdge = randomNodeEdge;
  badRandomEdge.mode_mask = 3;
  EXPECT_EQ(obelisk_rt_v1_class_validate(&malformed),
            OBELISK_RT_INVALID_DESIGN);
  obelisk_rt_random_variable_v1 badRandomVariable = randomNodeVariable;
  badRandomLayout = randomNodeLayout;
  badRandomLayout.variables = &badRandomVariable;
  malformed = randomNodeDescriptor;
  malformed.random_layout = &badRandomLayout;
  badRandomVariable.value_offset = kNodeLinkOffset;
  EXPECT_EQ(obelisk_rt_v1_class_validate(&malformed),
            OBELISK_RT_INVALID_DESIGN);
  badRandomVariable = randomNodeVariable;
  badRandomVariable.mode_mask = randomNodeEdge.mode_mask;
  EXPECT_EQ(obelisk_rt_v1_class_validate(&malformed),
            OBELISK_RT_INVALID_DESIGN);
  badRandomVariable = randomNodeVariable;
  badRandomVariable.randc_key_offset = kRandomNodeValueOffset;
  EXPECT_EQ(obelisk_rt_v1_class_validate(&malformed),
            OBELISK_RT_INVALID_DESIGN);
  malformed = nodeDescriptor;
  malformed.flags = OBELISK_RT_CLASS_ABSTRACT;
  EXPECT_EQ(obelisk_rt_v1_class_validate(&malformed), OBELISK_RT_OK);
  malformed = nodeDescriptor;
  malformed.flags = OBELISK_RT_CLASS_WEAK_WRAPPER;
  EXPECT_EQ(obelisk_rt_v1_class_validate(&malformed),
            OBELISK_RT_INVALID_DESIGN);
  obelisk_rt_trace_entry_v1 ambiguousWeakEntries[] = {
      {8, 1, 8, OBELISK_RT_TRACE_WEAK, OBELISK_RT_MANAGED_SLOT_CLASS,
       nullptr},
      {8, 1, 8, OBELISK_RT_TRACE_STRONG, OBELISK_RT_MANAGED_SLOT_CLASS,
       nullptr}};
  obelisk_rt_trace_layout_v1 ambiguousWeakLayout{OBELISK_RT_VERSION,   0, 16, 8,
                                                 ambiguousWeakEntries, 2};
  malformed.layout = &ambiguousWeakLayout;
  EXPECT_EQ(obelisk_rt_v1_class_validate(&malformed),
            OBELISK_RT_INVALID_DESIGN);

  obelisk_rt_trace_entry_v1 duplicateEntries[] = {nodeTraceEntry,
                                                  nodeTraceEntry};
  obelisk_rt_trace_layout_v1 duplicateLayout{
      OBELISK_RT_VERSION, 0,
      sizeof(void *) * 3, alignof(void *),
      duplicateEntries,   std::size(duplicateEntries)};
  malformed = nodeDescriptor;
  malformed.layout = &duplicateLayout;
  EXPECT_EQ(obelisk_rt_v1_class_validate(&malformed),
            OBELISK_RT_INVALID_DESIGN);

  obelisk_rt_class_descriptor_v1 finalBase = nodeDescriptor;
  finalBase.flags = OBELISK_RT_CLASS_FINAL;
  malformed = derivedDescriptor;
  malformed.base = &finalBase;
  EXPECT_EQ(obelisk_rt_v1_class_validate(&malformed),
            OBELISK_RT_INVALID_DESIGN);

  obelisk_rt_method_descriptor_v1 pureMethod = nodeMethods[0];
  pureMethod.flags = OBELISK_RT_METHOD_PURE;
  malformed = nodeDescriptor;
  malformed.methods = &pureMethod;
  EXPECT_EQ(obelisk_rt_v1_class_validate(&malformed),
            OBELISK_RT_INVALID_DESIGN);
}

} // namespace
