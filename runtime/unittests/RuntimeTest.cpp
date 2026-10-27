//===- RuntimeTest.cpp - Tests for the Talos native runtime -------------===//

#include "talos/Runtime/DescriptorMap.h"
#include "talos/Runtime/Runtime.h"
#include "svdpi.h"

#include "gtest/gtest.h"

#include "../lib/RuntimeInternal.h"
#include "../lib/StrengthFormat.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
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
#include <unordered_set>
#include <utility>
#include <vector>

extern "C" int talos_runtime_c_api_smoke(void);

namespace {

class RuntimeBuffer {
public:
  RuntimeBuffer() = default;
  RuntimeBuffer(const RuntimeBuffer &) = delete;
  RuntimeBuffer &operator=(const RuntimeBuffer &) = delete;
  ~RuntimeBuffer() { talos_rt_v1_buffer_release(&buffer); }

  talos_rt_buffer_v1 *out() { return &buffer; }
  std::string str() const {
    if (buffer.size == 0)
      return {};
    return std::string(reinterpret_cast<const char *>(buffer.data),
                       static_cast<size_t>(buffer.size));
  }

private:
  talos_rt_buffer_v1 buffer{};
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

  talos_rt_arg_v1 arg(uint32_t extraFlags = 0) const {
    return {TALOS_RT_ARG_LOGIC,
            extraFlags |
                (isSigned ? static_cast<uint32_t>(TALOS_RT_ARG_SIGNED) : 0u),
            width, value.data(), unknown.empty() ? nullptr : unknown.data()};
  }

private:
  uint64_t width;
  std::vector<uint64_t> value;
  std::vector<uint64_t> unknown;
  bool isSigned;
};

talos_rt_arg_v1 stringArg(std::string_view value, uint32_t flags = 0) {
  return {TALOS_RT_ARG_STRING, flags, value.size(), value.data(), nullptr};
}

talos_rt_arg_v1 realArg(const double &value) {
  return {TALOS_RT_ARG_REAL, 0, 0, &value, nullptr};
}

talos_rt_arg_v1 timeArg(const uint64_t &value) {
  return {TALOS_RT_ARG_TIME, 0, 64, &value, nullptr};
}

void appendInstruction(std::vector<uint8_t> &code, uint8_t opcode,
                       uint8_t type = TALOS_RT_BC_TYPE_NONE,
                       uint16_t destination = 0, uint16_t source0 = 0,
                       uint16_t source1 = 0, uint64_t immediate = 0) {
  size_t offset = code.size();
  code.resize(offset + TALOS_RT_BYTECODE_INSTRUCTION_SIZE, 0);
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
  uint64_t first = code.size() / TALOS_RT_BYTECODE_INSTRUCTION_SIZE;
  appendInstruction(code, TALOS_RT_BC_CALL_SERVICE, TALOS_RT_BC_TYPE_STATUS,
                    statusRegister, 0, 0, site);
  appendInstruction(code, TALOS_RT_BC_BRANCH_ZERO, TALOS_RT_BC_TYPE_STATUS,
                    0, statusRegister, 0, first + 3);
  appendInstruction(code, TALOS_RT_BC_FAIL, TALOS_RT_BC_TYPE_STATUS, 0,
                    statusRegister);
}

talos_rt_fragment_descriptor_v1
bytecodeDescriptor(const std::vector<uint8_t> &code, uint32_t registers) {
  static constexpr talos_rt_bytecode_entry_v1 defaultEntry{0, 0};
  talos_rt_fragment_descriptor_v1 descriptor{};
  descriptor.handle = {TALOS_RT_DESCRIPTOR_FRAGMENT, 0, 7};
  descriptor.code_kind = TALOS_RT_FRAGMENT_BYTECODE;
  descriptor.code.bytecode = {
      code.data(), code.size(), &defaultEntry, 1, registers, 0,       nullptr,
      nullptr,     0,           nullptr,       0, 0,         nullptr, 0};
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

talos_rt_status observerEvaluator(talos_rt_context *context,
                                    const uint64_t *, uint32_t, uint64_t *value,
                                    uint64_t *unknown, uint32_t limbCount) {
  if (!value || !unknown || limbCount == 0)
    return TALOS_RT_INVALID_ARGUMENT;
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
    return TALOS_RT_INVALID_ARGUMENT;
  case ObserverEvaluatorMode::ReadFromJoinedThread: {
    std::thread reader([&] {
      (void)talos_rt_v1_scheduler_event_triggered(context, 99);
      observerJoinedReadCompleted = true;
    });
    reader.join();
    value[0] = 1;
    break;
  }
  case ObserverEvaluatorMode::DestroyContext:
    value[0] = 1;
    talos_rt_v1_context_destroy(context);
    break;
  case ObserverEvaluatorMode::RecurseOnceAndLatch:
    if (call == 1) {
      const uint8_t one = 1;
      const uint8_t zero = 0;
      talos_rt_v1_scheduler_signal_transition(
          context, talos_rt_v1_native_state_static_handle(1), 1, &one,
          nullptr, &zero, nullptr);
    }
    value[0] = 1;
    break;
  case ObserverEvaluatorMode::Recurse:
    if (call < 300) {
      uint8_t oldValue = static_cast<uint8_t>(call & 1);
      uint8_t newValue = static_cast<uint8_t>(oldValue ^ 1);
      talos_rt_v1_scheduler_signal_transition(
          context, talos_rt_v1_native_state_static_handle(1), 1, &oldValue,
          nullptr, &newValue, nullptr);
    }
    value[0] = call & 1;
    break;
  }
  return TALOS_RT_OK;
}

talos_rt_status observerWaitRequirements(uint64_t *size,
                                           uint64_t *alignment) {
  if (!size || !alignment)
    return TALOS_RT_INVALID_ARGUMENT;
  *size = 0;
  *alignment = 1;
  return TALOS_RT_OK;
}

talos_rt_status overAlignedRequirements(uint64_t *size, uint64_t *alignment) {
  if (!size || !alignment)
    return TALOS_RT_INVALID_ARGUMENT;
  *size = 0;
  *alignment = 32;
  return TALOS_RT_OK;
}

talos_rt_status
observerWaitExecute(talos_rt_process_instance_v1 *instance) {
  if (!instance || !instance->action)
    return TALOS_RT_INVALID_ARGUMENT;
  *instance->action = {TALOS_RT_FRAGMENT_SUSPEND,
                       TALOS_RT_SUSPEND_OBSERVER,
                       1,
                       TALOS_RT_ACTION_FRAME_WAIT_RECORD,
                       0,
                       176};
  return TALOS_RT_OK;
}

void observerWaitDestroy(talos_rt_process_instance_v1 *) {}

uint32_t observerSecondClauseCalls = 0;

talos_rt_status observerSecondEvaluator(talos_rt_context *,
                                          const uint64_t *, uint32_t,
                                          uint64_t *value, uint64_t *unknown,
                                          uint32_t limbCount) {
  if (!value || !unknown || limbCount != 1)
    return TALOS_RT_INVALID_ARGUMENT;
  ++observerSecondClauseCalls;
  value[0] = 1;
  unknown[0] = 0;
  return TALOS_RT_OK;
}

talos_rt_status
observerTwoClauseExecute(talos_rt_process_instance_v1 *instance) {
  if (!instance || !instance->action)
    return TALOS_RT_INVALID_ARGUMENT;
  *instance->action = {TALOS_RT_FRAGMENT_SUSPEND,
                       TALOS_RT_SUSPEND_OBSERVER,
                       1,
                       TALOS_RT_ACTION_FRAME_WAIT_RECORD,
                       0,
                       256};
  return TALOS_RT_OK;
}

uint64_t observerWaitLayoutChecksum(const talos_rt_frame_layout_v1 &layout) {
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
  talos_rt_observer_descriptor_v1 observer{
      7, nullptr, 0, 1, 0, TALOS_RT_OBSERVER_NO_BYTECODE, observerEvaluator,
      0};
  talos_rt_execution_descriptor_v1 execution{};
  talos_rt_frame_field_v1 field{
      TALOS_RT_FRAME_WAIT, TALOS_RT_FRAME_FIELD_FLAGS_NONE, 0, 176, 8, 0};
  std::array<uint32_t, 2> continuations{0, 1};
  talos_rt_frame_layout_v1 layout{};
  talos_rt_process_descriptor_v1 process{};

  ObserverWaitTestDescriptor() {
    execution.version = TALOS_RT_VERSION;
    execution.observers = &observer;
    execution.observer_count = 1;
    layout = {TALOS_RT_VERSION,
              0,
              176,
              8,
              &field,
              1,
              static_cast<uint32_t>(continuations.size()),
              continuations.data(),
              0};
    layout.checksum = observerWaitLayoutChecksum(layout);
    process = {{TALOS_RT_DESCRIPTOR_PROCESS, 0, 17},
               TALOS_RT_VERSION,
               0,
               TALOS_RT_TIER_MASK_NATIVE,
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
    auto *wait = static_cast<talos_rt_computed_wait_record_v1 *>(frame);
    *wait = {TALOS_RT_VERSION,
             TALOS_RT_SUSPEND_OBSERVER,
             TALOS_RT_COMPUTED_WAIT_INTERLEAVED,
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
    auto *binding = reinterpret_cast<talos_rt_computed_observer_v1 *>(
        static_cast<uint8_t *>(frame) + wait->observers_offset);
    *binding = {7, 0, 0, 0, 1, 160, 0};
    auto *dependency = reinterpret_cast<talos_rt_computed_dependency_v1 *>(
        static_cast<uint8_t *>(frame) + wait->dependencies_offset);
    *dependency = {talos_rt_v1_native_state_static_handle(1),
                   TALOS_RT_OBSERVER_DEPENDENCY_SIGNAL, 1};
    auto *clause = reinterpret_cast<talos_rt_computed_clause_v1 *>(
        static_cast<uint8_t *>(frame) + wait->clauses_offset);
    *clause = {0, TALOS_RT_OBSERVER_CONDITION_NONE,
               TALOS_RT_WAIT_EDGE_CHANGE, 0};
  }
};

talos_rt_status startObserverWait(ObserverWaitTestDescriptor &descriptor,
                                    talos_rt_context **outContext) {
  if (!outContext)
    return TALOS_RT_INVALID_ARGUMENT;
  *outContext = nullptr;
  talos_rt_context *context = nullptr;
  talos_rt_status status =
      talos_rt_v1_context_create_for_design(&descriptor.execution, &context);
  if (status != TALOS_RT_OK)
    return status;
  status = talos_rt_v1_native_state_register_static(context, 1, 0, 1);
  if (status != TALOS_RT_OK) {
    talos_rt_v1_context_destroy(context);
    return status;
  }
  talos_rt_process_instance_v1 *instance = nullptr;
  status =
      talos_rt_v1_process_instance_create(&descriptor.process, &instance);
  if (status != TALOS_RT_OK) {
    talos_rt_v1_context_destroy(context);
    return status;
  }
  void *frame = nullptr;
  uint64_t frameSize = 0;
  status = talos_rt_v1_process_instance_frame(instance, &frame, &frameSize);
  if (status != TALOS_RT_OK || frameSize != 176) {
    (void)talos_rt_v1_process_instance_destroy(instance);
    talos_rt_v1_context_destroy(context);
    return status == TALOS_RT_OK ? TALOS_RT_INVALID_FRAME : status;
  }
  ObserverWaitTestDescriptor::populate(frame);
  status = talos_rt_v1_scheduler_add(context, instance, 0);
  if (status != TALOS_RT_OK) {
    (void)talos_rt_v1_process_instance_destroy(instance);
    talos_rt_v1_context_destroy(context);
    return status;
  }
  status = talos_rt_v1_scheduler_run(context);
  if (status != TALOS_RT_OK) {
    talos_rt_v1_context_destroy(context);
    return status;
  }
  *outContext = context;
  return TALOS_RT_OK;
}

talos_rt_status
executeBytecode(const talos_rt_fragment_descriptor_v1 &input, void *frame,
                uint64_t frameSize, uint32_t continuation,
                talos_rt_fragment_action_v1 *action,
                uint64_t instructionLimit = 0,
                talos_rt_context *context = nullptr) {
  talos_rt_fragment_descriptor_v1 descriptor = input;
  descriptor.code.bytecode.register_offset = frameSize;
  uint64_t scratchSize =
      static_cast<uint64_t>(descriptor.code.bytecode.register_count) *
      TALOS_RT_BYTECODE_REGISTER_SIZE;
  std::vector<uint8_t> storage(frameSize + scratchSize);
  if (frameSize != 0)
    std::memcpy(storage.data(), frame, frameSize);
  talos_rt_status status =
      instructionLimit == 0
          ? talos_rt_v1_fragment_execute(&descriptor, context,
                                           storage.empty() ? nullptr
                                                           : storage.data(),
                                           storage.size(), continuation, action)
          : talos_rt_v1_bytecode_execute_bounded(
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
           ("talos-runtime-" + std::to_string(stamp) + "-" +
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
    ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
    ASSERT_NE(context, nullptr);
  }

  void TearDown() override { talos_rt_v1_context_destroy(context); }

  std::pair<talos_rt_status, std::string>
  format(std::string_view formatString,
         const std::vector<talos_rt_arg_v1> &arguments,
         const talos_rt_format_env_v1 *environment = nullptr) {
    RuntimeBuffer output;
    talos_rt_status status = talos_rt_v1_format(
        context, formatString.data(), formatString.size(), arguments.data(),
        arguments.size(), environment, output.out());
    return {status, output.str()};
  }

  uint32_t open(const std::filesystem::path &path, std::string_view mode) {
    std::string pathString = path.string();
    uint32_t descriptor = 0;
    EXPECT_EQ(talos_rt_v1_file_open(context, pathString.data(),
                                      pathString.size(), mode.data(),
                                      mode.size(), &descriptor),
              TALOS_RT_OK);
    EXPECT_NE(descriptor, 0u);
    return descriptor;
  }

  talos_rt_context *context = nullptr;
};

// Layout expectations follow the pointer width, exactly as the tables in
// runtime/lib/ABI.cpp do.
static constexpr size_t abiPtr(size_t wide, size_t narrow) {
  return sizeof(void *) == 8 ? wide : narrow;
}

TEST(RuntimeABI, StableScalarLayout) {
  EXPECT_EQ(sizeof(talos_rt_status), 4u);
  EXPECT_EQ(sizeof(talos_rt_arg_kind), 4u);
  EXPECT_EQ(sizeof(talos_rt_arg_flags), 4u);
  EXPECT_EQ(offsetof(talos_rt_arg_v1, kind), 0u);
  EXPECT_EQ(offsetof(talos_rt_arg_v1, flags), 4u);
  EXPECT_EQ(offsetof(talos_rt_arg_v1, size), 8u);
  EXPECT_EQ(sizeof(talos_rt_enum_arg_v1), abiPtr(40u, 32u));
  EXPECT_EQ(offsetof(talos_rt_enum_arg_v1, value), 16u);
  EXPECT_EQ(offsetof(talos_rt_enum_arg_v1, name), abiPtr(32u, 24u));
  EXPECT_EQ(sizeof(talos_rt_net_arg_v1), abiPtr(40u, 32u));
  EXPECT_EQ(offsetof(talos_rt_net_arg_v1, native_state), 12u);
  EXPECT_EQ(offsetof(talos_rt_net_arg_v1, value), 16u);
  EXPECT_EQ(offsetof(talos_rt_net_arg_v1, handle), abiPtr(32u, 24u));
  EXPECT_EQ(sizeof(talos_rt_raw_aggregate_arg_v1), 24u);
  EXPECT_EQ(offsetof(talos_rt_raw_aggregate_arg_v1, four_state), 16u);
  EXPECT_EQ(sizeof(talos_rt_activation_descriptor_v1), 24u);
  EXPECT_EQ(offsetof(talos_rt_activation_descriptor_v1, native_entry), 8u);
  EXPECT_EQ(offsetof(talos_rt_activation_descriptor_v1, bytecode_function),
            abiPtr(16u, 12u));
  EXPECT_EQ(sizeof(talos_rt_observer_capture_abi_v1), 8u);
  EXPECT_EQ(sizeof(talos_rt_observer_descriptor_v1), abiPtr(48u, 40u));
  EXPECT_EQ(sizeof(talos_rt_execution_descriptor_v1), 120u);
  EXPECT_EQ(offsetof(talos_rt_execution_descriptor_v1, version), 0u);
  EXPECT_EQ(offsetof(talos_rt_execution_descriptor_v1, flags), 4u);
  EXPECT_EQ(offsetof(talos_rt_execution_descriptor_v1, reserved), 8u);
  EXPECT_EQ(offsetof(talos_rt_execution_descriptor_v1, activations), 88u);
  EXPECT_EQ(offsetof(talos_rt_execution_descriptor_v1, observers), 104u);
  EXPECT_EQ(sizeof(talos_rt_computed_wait_record_v1), 96u);
  EXPECT_EQ(sizeof(talos_rt_computed_observer_v1), 32u);
  EXPECT_EQ(sizeof(talos_rt_computed_capture_v1), 32u);
  EXPECT_EQ(sizeof(talos_rt_computed_dependency_v1), 16u);
  EXPECT_EQ(sizeof(talos_rt_computed_clause_v1), 16u);
  EXPECT_EQ(TALOS_RT_VERSION, 1u);
  EXPECT_STREQ(talos_rt_v1_status_string(TALOS_RT_FORMAT_ERROR),
               "format error");
}

TEST(RuntimeABI, CConsumerCompilesLinksAndRuns) {
  EXPECT_EQ(talos_runtime_c_api_smoke(), 0);
}

TEST(RuntimeABI, RejectsMalformedActivationInventory) {
  const talos_rt_activation_descriptor_v1 activation{
      1, nullptr, TALOS_RT_ACTIVATION_NO_BYTECODE,
      TALOS_RT_ACTIVATION_HAS_NATIVE};
  const talos_rt_execution_descriptor_v1 execution{TALOS_RT_VERSION,
                                                     0,
                                                     0,
                                                     nullptr,
                                                     0,
                                                     nullptr,
                                                     0,
                                                     0,
                                                     0,
                                                     nullptr,
                                                     0,
                                                     0,
                                                     0,
                                                     &activation,
                                                     1,
                                                     nullptr,
                                                     0};
  talos_rt_context *context = nullptr;
  EXPECT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_INVALID_DESIGN);
  EXPECT_EQ(context, nullptr);
}

TEST(RuntimeABI, ValidatesObserverInventoryAndDescriptorVersion) {
  talos_rt_observer_capture_abi_v1 capture{
      TALOS_RT_OBSERVER_CAPTURE_STORAGE, 8};
  talos_rt_observer_descriptor_v1 observer{
      7, &capture, 1, 1, 0, TALOS_RT_OBSERVER_NO_BYTECODE, observerEvaluator,
      0};
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.observers = &observer;
  execution.observer_count = 1;

  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);

  execution.version = TALOS_RT_VERSION - 1;
  EXPECT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_INVALID_DESIGN);
  EXPECT_EQ(context, nullptr);
  execution.version = TALOS_RT_VERSION;

  capture.kind = 0;
  EXPECT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_INVALID_DESIGN);
  EXPECT_EQ(context, nullptr);
  capture.kind = TALOS_RT_OBSERVER_CAPTURE_STORAGE;
  observer.native_evaluator = nullptr;
  EXPECT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_INVALID_DESIGN);
  EXPECT_EQ(context, nullptr);
}

TEST(RuntimeABI, AlignsProcessFramesForNativeState) {
  const uint32_t continuation = 0;
  talos_rt_frame_layout_v1 layout{TALOS_RT_VERSION, 0, 8, 8, nullptr, 0, 1,
                                    &continuation,      0};
  layout.checksum = observerWaitLayoutChecksum(layout);
  talos_rt_process_descriptor_v1 process{
      {TALOS_RT_DESCRIPTOR_PROCESS, 0, 19},
      TALOS_RT_VERSION,
      0,
      TALOS_RT_TIER_MASK_NATIVE,
      0,
      &layout,
      overAlignedRequirements,
      observerWaitExecute,
      observerWaitDestroy,
      nullptr,
      nullptr,
      nullptr};
  talos_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(talos_rt_v1_process_instance_create(&process, &instance),
            TALOS_RT_OK);
  void *frame = nullptr;
  uint64_t frameSize = 0;
  ASSERT_EQ(talos_rt_v1_process_instance_frame(instance, &frame, &frameSize),
            TALOS_RT_OK);
  EXPECT_EQ(frameSize, 8u);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(frame) % 32, 0u);
  EXPECT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
}

TEST(RuntimeABI, RejectsMalformedComputedWaitRecords) {
  ObserverWaitTestDescriptor descriptor;
  auto execute = [&](auto mutate, talos_rt_status expected) {
    talos_rt_process_instance_v1 *instance = nullptr;
    ASSERT_EQ(
        talos_rt_v1_process_instance_create(&descriptor.process, &instance),
        TALOS_RT_OK);
    void *frame = nullptr;
    uint64_t frameSize = 0;
    ASSERT_EQ(
        talos_rt_v1_process_instance_frame(instance, &frame, &frameSize),
        TALOS_RT_OK);
    ASSERT_EQ(frameSize, 176u);
    ObserverWaitTestDescriptor::populate(frame);
    mutate(*static_cast<talos_rt_computed_wait_record_v1 *>(frame), frame);
    talos_rt_fragment_action_v1 action{};
    EXPECT_EQ(talos_rt_v1_process_instance_execute(
                  instance, nullptr, TALOS_RT_TIER_NATIVE, &action),
              expected);
    EXPECT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
  };

  execute([](auto &, void *) {}, TALOS_RT_OK);
  execute([](auto &wait, void *) { wait.version = TALOS_RT_VERSION + 1; },
          TALOS_RT_INVALID_FRAME);
  execute([](auto &wait, void *) { wait.observers_offset = UINT64_MAX; },
          TALOS_RT_INVALID_FRAME);
  execute(
      [](auto &wait, void *frame) {
        auto *binding = reinterpret_cast<talos_rt_computed_observer_v1 *>(
            static_cast<uint8_t *>(frame) + wait.observers_offset);
        binding->code_unit_id = 8;
      },
      TALOS_RT_INVALID_FRAME);
  execute(
      [](auto &wait, void *frame) {
        auto *clause = reinterpret_cast<talos_rt_computed_clause_v1 *>(
            static_cast<uint8_t *>(frame) + wait.clauses_offset);
        clause->condition_observer = 0;
      },
      TALOS_RT_INVALID_FRAME);
  execute(
      [](auto &wait, void *frame) {
        auto *binding = reinterpret_cast<talos_rt_computed_observer_v1 *>(
            static_cast<uint8_t *>(frame) + wait.observers_offset);
        binding->previous_offset += sizeof(uint64_t);
      },
      TALOS_RT_INVALID_FRAME);
  execute(
      [](auto &wait, void *frame) {
        auto *dependency =
            reinterpret_cast<talos_rt_computed_dependency_v1 *>(
                static_cast<uint8_t *>(frame) + wait.dependencies_offset);
        dependency->stable_id = UINT64_MAX;
      },
      TALOS_RT_INVALID_FRAME);
  execute(
      [](auto &wait, void *frame) {
        auto *dependency =
            reinterpret_cast<talos_rt_computed_dependency_v1 *>(
                static_cast<uint8_t *>(frame) + wait.dependencies_offset);
        dependency->width = 0;
      },
      TALOS_RT_INVALID_FRAME);
  execute(
      [](auto &wait, void *frame) {
        auto *clause = reinterpret_cast<talos_rt_computed_clause_v1 *>(
            static_cast<uint8_t *>(frame) + wait.clauses_offset);
        clause->flags = UINT32_MAX;
      },
      TALOS_RT_INVALID_FRAME);
  execute([](auto &wait, void *) { wait.previous_limb_count = UINT32_MAX; },
          TALOS_RT_INVALID_FRAME);
}

TEST(RuntimeABI, ObserverEvaluatorRunsWithoutTheContextMutexHeld) {
  ObserverWaitTestDescriptor descriptor;
  observerEvaluatorMode = ObserverEvaluatorMode::ReadFromJoinedThread;
  observerEvaluatorCalls = 0;
  observerJoinedReadCompleted = false;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(startObserverWait(descriptor, &context), TALOS_RT_OK);

  const uint8_t zero = 0;
  const uint8_t one = 1;
  talos_rt_v1_scheduler_signal_transition(
      context, talos_rt_v1_native_state_static_handle(1), 1, &zero, nullptr,
      &one, nullptr);
  EXPECT_TRUE(observerJoinedReadCompleted.load());
  EXPECT_EQ(observerEvaluatorCalls.load(), 1u);
  EXPECT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);
  observerEvaluatorMode = ObserverEvaluatorMode::ConstantZero;
}

TEST(RuntimeABI, ObserverEvaluatorFailurePropagatesThroughScheduler) {
  ObserverWaitTestDescriptor descriptor;
  observerEvaluatorMode = ObserverEvaluatorMode::Fail;
  observerEvaluatorCalls = 0;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(startObserverWait(descriptor, &context), TALOS_RT_OK);

  const uint8_t zero = 0;
  const uint8_t one = 1;
  talos_rt_v1_scheduler_signal_transition(
      context, talos_rt_v1_native_state_static_handle(1), 1, &zero, nullptr,
      &one, nullptr);
  EXPECT_EQ(observerEvaluatorCalls.load(), 1u);
  EXPECT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_INVALID_ARGUMENT);
  talos_rt_v1_context_destroy(context);
  observerEvaluatorMode = ObserverEvaluatorMode::ConstantZero;
}

TEST(RuntimeABI, ObserverEvaluatorDefersContextDestructionUntilReturn) {
  ObserverWaitTestDescriptor descriptor;
  observerEvaluatorMode = ObserverEvaluatorMode::DestroyContext;
  observerEvaluatorCalls = 0;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(startObserverWait(descriptor, &context), TALOS_RT_OK);

  const uint8_t zero = 0;
  const uint8_t one = 1;
  // Destruction occurs from inside the callback. The transaction pins all
  // state needed to restore the producer and release the waiting activation,
  // then performs final cleanup as this publication returns.
  talos_rt_v1_scheduler_signal_transition(
      context, talos_rt_v1_native_state_static_handle(1), 1, &zero, nullptr,
      &one, nullptr);
  EXPECT_EQ(observerEvaluatorCalls.load(), 1u);
  observerEvaluatorMode = ObserverEvaluatorMode::ConstantZero;
}

TEST(RuntimeABI, NestedObserverLatchStopsLaterSourceClauses) {
  std::array<talos_rt_observer_descriptor_v1, 2> observers{{
      {7, nullptr, 0, 1, 0, TALOS_RT_OBSERVER_NO_BYTECODE, observerEvaluator,
       0},
      {8, nullptr, 0, 1, 0, TALOS_RT_OBSERVER_NO_BYTECODE,
       observerSecondEvaluator, 0},
  }};
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.observers = observers.data();
  execution.observer_count = observers.size();
  talos_rt_frame_field_v1 field{
      TALOS_RT_FRAME_WAIT, TALOS_RT_FRAME_FIELD_FLAGS_NONE, 0, 256, 8, 0};
  std::array<uint32_t, 2> continuations{{0, 1}};
  talos_rt_frame_layout_v1 layout{TALOS_RT_VERSION,
                                    0,
                                    256,
                                    8,
                                    &field,
                                    1,
                                    static_cast<uint32_t>(continuations.size()),
                                    continuations.data(),
                                    0};
  layout.checksum = observerWaitLayoutChecksum(layout);
  talos_rt_process_descriptor_v1 process{
      {TALOS_RT_DESCRIPTOR_PROCESS, 0, 18},
      TALOS_RT_VERSION,
      0,
      TALOS_RT_TIER_MASK_NATIVE,
      0,
      &layout,
      observerWaitRequirements,
      observerTwoClauseExecute,
      observerWaitDestroy,
      nullptr,
      &execution,
      nullptr};

  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 1),
            TALOS_RT_OK);
  talos_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(talos_rt_v1_process_instance_create(&process, &instance),
            TALOS_RT_OK);
  void *frame = nullptr;
  uint64_t frameSize = 0;
  ASSERT_EQ(talos_rt_v1_process_instance_frame(instance, &frame, &frameSize),
            TALOS_RT_OK);
  ASSERT_EQ(frameSize, 256u);
  std::memset(frame, 0, frameSize);
  auto *wait = static_cast<talos_rt_computed_wait_record_v1 *>(frame);
  *wait = {TALOS_RT_VERSION,
           TALOS_RT_SUSPEND_OBSERVER,
           TALOS_RT_COMPUTED_WAIT_INTERLEAVED,
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
  auto *bindings = reinterpret_cast<talos_rt_computed_observer_v1 *>(
      static_cast<uint8_t *>(frame) + wait->observers_offset);
  bindings[0] = {7, 0, 0, 0, 1, 224, 0};
  bindings[1] = {8, 0, 0, 1, 1, 240, 0};
  auto *dependencies = reinterpret_cast<talos_rt_computed_dependency_v1 *>(
      static_cast<uint8_t *>(frame) + wait->dependencies_offset);
  dependencies[0] = {talos_rt_v1_native_state_static_handle(1),
                     TALOS_RT_OBSERVER_DEPENDENCY_SIGNAL, 1};
  dependencies[1] = dependencies[0];
  auto *clauses = reinterpret_cast<talos_rt_computed_clause_v1 *>(
      static_cast<uint8_t *>(frame) + wait->clauses_offset);
  clauses[0] = {0, TALOS_RT_OBSERVER_CONDITION_NONE,
                TALOS_RT_WAIT_EDGE_CHANGE, 0};
  clauses[1] = {1, TALOS_RT_OBSERVER_CONDITION_NONE,
                TALOS_RT_WAIT_EDGE_CHANGE, 0};
  ASSERT_EQ(talos_rt_v1_scheduler_add(context, instance, 0), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);

  observerEvaluatorMode = ObserverEvaluatorMode::RecurseOnceAndLatch;
  observerEvaluatorCalls = 0;
  observerSecondClauseCalls = 0;
  const uint8_t zero = 0;
  const uint8_t one = 1;
  talos_rt_v1_scheduler_signal_transition(
      context, talos_rt_v1_native_state_static_handle(1), 1, &zero, nullptr,
      &one, nullptr);
  EXPECT_EQ(observerEvaluatorCalls.load(), 2u);
  EXPECT_EQ(observerSecondClauseCalls, 0u);
  talos_rt_v1_context_destroy(context);
  observerEvaluatorMode = ObserverEvaluatorMode::ConstantZero;
}

TEST(RuntimeABI, RecursiveObserverEvaluationHasADeterministicDepthLimit) {
  ObserverWaitTestDescriptor descriptor;
  observerEvaluatorMode = ObserverEvaluatorMode::Recurse;
  observerEvaluatorCalls = 0;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(startObserverWait(descriptor, &context), TALOS_RT_OK);

  const uint8_t zero = 0;
  const uint8_t one = 1;
  talos_rt_v1_scheduler_signal_transition(
      context, talos_rt_v1_native_state_static_handle(1), 1, &zero, nullptr,
      &one, nullptr);
  EXPECT_EQ(observerEvaluatorCalls.load(), 256u);
  EXPECT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OUT_OF_RESOURCES);
  talos_rt_v1_context_destroy(context);
  observerEvaluatorMode = ObserverEvaluatorMode::ConstantZero;
}

TEST(RuntimeABI, ReportsEveryStatusAndReleasesBuffersIdempotently) {
  static constexpr std::pair<talos_rt_status, const char *> statuses[] = {
      {TALOS_RT_OK, "ok"},
      {TALOS_RT_EOF, "end of file"},
      {TALOS_RT_INVALID_ARGUMENT, "invalid argument"},
      {TALOS_RT_INVALID_HANDLE, "invalid handle"},
      {TALOS_RT_IO_ERROR, "I/O error"},
      {TALOS_RT_OUT_OF_MEMORY, "out of memory"},
      {TALOS_RT_OUT_OF_RESOURCES, "out of resources"},
      {TALOS_RT_FORMAT_ERROR, "format error"},
      {TALOS_RT_ARGUMENT_MISMATCH, "format argument mismatch"},
      {TALOS_RT_INVALID_BYTECODE, "invalid bytecode"},
      {TALOS_RT_STEP_LIMIT, "fragment step limit exceeded"},
      {TALOS_RT_LAYOUT_MISMATCH, "process frame layout mismatch"},
      {TALOS_RT_INVALID_CONTINUATION, "invalid process continuation"},
      {TALOS_RT_TIER_UNAVAILABLE, "requested process tier unavailable"},
      {TALOS_RT_AOT_CHECKPOINT,
       "native scheduler synchronization checkpoint"},
      {TALOS_RT_AOT_TIMED_CHECKPOINT,
       "native scheduler timed synchronization checkpoint"},
      {TALOS_RT_AOT_GENERATED_CHECKPOINT,
       "generated native scheduler branch checkpoint"},
      {TALOS_RT_INVALID_LIFECYCLE, "invalid process lifecycle transition"},
      {TALOS_RT_INVALID_FRAME, "invalid process frame record"},
      {TALOS_RT_INVALID_DESIGN, "invalid design metadata"},
      {TALOS_RT_PERMISSION_DENIED, "permission denied"},
      {TALOS_RT_DPI_DISABLE_UNSUPPORTED, "DPI task disable is unsupported"},
      {TALOS_RT_FATAL, "fatal SystemVerilog diagnostic"},
  };
  for (const auto &[status, message] : statuses)
    EXPECT_STREQ(talos_rt_v1_status_string(status), message);
  EXPECT_STREQ(talos_rt_v1_status_string(-1), "unknown runtime status");

  talos_rt_buffer_v1 buffer{};
  talos_rt_v1_buffer_release(nullptr);
  talos_rt_v1_buffer_release(&buffer);
  talos_rt_v1_buffer_release(&buffer);
  EXPECT_EQ(buffer.data, nullptr);
  EXPECT_EQ(buffer.size, 0u);
}

TEST(RuntimeABI, FinishStopAndFatalHaveDistinctEntryPoints) {
  EXPECT_EQ(talos_rt_v1_scheduler_finish(nullptr, 0),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_scheduler_stop(nullptr, 0),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_scheduler_fatal(nullptr, 0),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_scheduler_termination_requested(nullptr), 0u);
  EXPECT_EQ(talos_rt_v1_scheduler_time(nullptr), 0u);

  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_NE(context, nullptr);
  EXPECT_EQ(talos_rt_v1_scheduler_termination_requested(context), 0u);
  EXPECT_EQ(talos_rt_v1_scheduler_time(context), 0u);
  EXPECT_EQ(talos_rt_v1_scheduler_finish(context, 2), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_scheduler_termination_requested(context), 1u);
  EXPECT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);

  context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_NE(context, nullptr);
  EXPECT_EQ(talos_rt_v1_scheduler_termination_requested(context), 0u);
  EXPECT_EQ(talos_rt_v1_scheduler_stop(context, 1), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_scheduler_termination_requested(context), 1u);
  EXPECT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);

  context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_NE(context, nullptr);
  EXPECT_EQ(talos_rt_v1_scheduler_termination_requested(context), 0u);
  EXPECT_EQ(talos_rt_v1_scheduler_fatal(context, 0), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_scheduler_termination_requested(context), 1u);
  EXPECT_EQ(talos_rt_v1_scheduler_run(context), TALOS_RT_FATAL);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeABI, TerminationPollingObservesConcurrentRequests) {
  using Request = talos_rt_status (*)(talos_rt_context *, uint32_t);
  for (Request request :
       {talos_rt_v1_scheduler_finish, talos_rt_v1_scheduler_stop,
        talos_rt_v1_scheduler_fatal}) {
    talos_rt_context *context = nullptr;
    ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
    ASSERT_NE(context, nullptr);
    std::atomic<bool> polling{false};
    bool observed = false;
    std::thread reader([&] {
      // Read before announcing readiness, then overlap further queries with
      // the request. The deadline makes a missed publication fail finitely.
      observed = talos_rt_v1_scheduler_termination_requested(context) != 0;
      polling.store(true, std::memory_order_release);
      auto deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(5);
      while (!observed && std::chrono::steady_clock::now() < deadline) {
        observed = talos_rt_v1_scheduler_termination_requested(context) != 0;
        std::this_thread::yield();
      }
    });
    while (!polling.load(std::memory_order_acquire))
      std::this_thread::yield();
    EXPECT_EQ(request(context, 0), TALOS_RT_OK);
    reader.join();
    EXPECT_TRUE(observed);
    // The latch is monotonic, including after the request's transaction ends.
    EXPECT_EQ(talos_rt_v1_scheduler_termination_requested(context), 1u);
    talos_rt_v1_context_destroy(context);
  }
}

struct DpiObservation {
  uint32_t calls = 0;
  uint32_t nestedID = 0;
  talos_rt_import_site_v1 nestedSite{};
  bool invokeNested = false;
  int userKey = 0;
};

TEST(RuntimeDPI, ImplementsCanonicalPackedVectorUtilities) {
  EXPECT_STREQ(svDpiVersion(), "1800-2005");
  EXPECT_EQ(SV_PACKED_DATA_NELEMS(65), 3);
  EXPECT_EQ(SV_GET_UNSIGNED_BITS(UINT32_C(0x1234abcd), 12), UINT32_C(0xbcd));

  svBitVecVal bits[3]{UINT32_C(0x80000001), UINT32_C(0x00000003), 0};
  EXPECT_EQ(svGetBitselBit(bits, 0), sv_1);
  EXPECT_EQ(svGetBitselBit(bits, 31), sv_1);
  EXPECT_EQ(svGetBitselBit(bits, 33), sv_1);
  svPutBitselBit(bits, 32, sv_1);
  EXPECT_EQ(bits[1], UINT32_C(3));
  svPutBitselBit(bits, 33, sv_0);
  EXPECT_EQ(bits[1], UINT32_C(1));

  svBitVecVal bitPart = 0;
  svGetPartselBit(&bitPart, bits, 28, 8);
  EXPECT_EQ(bitPart, UINT32_C(0x18));
  svPutPartselBit(bits, UINT32_C(0xa5), 30, 8);
  svGetPartselBit(&bitPart, bits, 30, 8);
  EXPECT_EQ(bitPart, UINT32_C(0xa5));

  svLogicVecVal logic[2]{{0, 0}, {0, 0}};
  svPutBitselLogic(logic, 0, sv_1);
  svPutBitselLogic(logic, 31, sv_z);
  svPutBitselLogic(logic, 32, sv_x);
  svPutBitselLogic(logic, 33, sv_0);
  EXPECT_EQ(svGetBitselLogic(logic, 0), sv_1);
  EXPECT_EQ(svGetBitselLogic(logic, 31), sv_z);
  EXPECT_EQ(svGetBitselLogic(logic, 32), sv_x);
  EXPECT_EQ(svGetBitselLogic(logic, 33), sv_0);

  svLogicVecVal logicPart{};
  svGetPartselLogic(&logicPart, logic, 30, 4);
  EXPECT_EQ(logicPart.aval, UINT32_C(4));
  EXPECT_EQ(logicPart.bval, UINT32_C(6));
  svLogicVecVal replacement{UINT32_C(0xa), UINT32_C(0xc)};
  svPutPartselLogic(logic, replacement, 29, 4);
  svGetPartselLogic(&logicPart, logic, 29, 4);
  EXPECT_EQ(logicPart.aval, replacement.aval);
  EXPECT_EQ(logicPart.bval, replacement.bval);
}

talos_rt_status observeDpiCall(talos_rt_context *context, uint32_t importID,
                                 const talos_rt_import_input_v1 *inputs,
                                 uint32_t inputCount,
                                 talos_rt_import_output_v1 *outputs,
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
  const char *file = nullptr;
  int line = 0;
  EXPECT_EQ(svGetCallerInfo(&file, &line), 1);
  EXPECT_STREQ(file, "dpi_test.sv");
  EXPECT_EQ(line, 41);
  EXPECT_EQ(svPutUserData(initial, &observation.userKey, &observation), 0);
  EXPECT_EQ(svGetUserData(initial, &observation.userKey), &observation);

  if (observation.invokeNested && importID != observation.nestedID) {
    uint64_t nestedValue[2]{};
    talos_rt_import_output_v1 nestedOutput{
        TALOS_RT_DBREG_BITS, 0, 0, 65, nestedValue, nullptr, 2};
    EXPECT_EQ(talos_rt_v1_import_call(context, &observation.nestedSite,
                                        inputs, inputCount, &nestedOutput, 1),
              TALOS_RT_OK);
    EXPECT_EQ(svGetScope(), initial);
    EXPECT_STREQ(svGetNameFromScope(initial), "top");
  }
  outputs[0].value[0] = inputs[0].value[0] ^ UINT64_C(0xffff);
  outputs[0].value[1] = UINT64_MAX;
  return TALOS_RT_OK;
}

TEST(RuntimeState, TwoStateUsesValueOnlyBacking) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.flags = TALOS_RT_EXECUTION_TWO_STATE;
  execution.state_bit_count = 129;
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_NE(context->stateValue.data(), nullptr);
  EXPECT_EQ(context->stateUnknown.data(), nullptr);
  EXPECT_EQ(context->stateUnknown.size(), 3u);
  context->stateUnknown[1] = UINT64_MAX;
  EXPECT_EQ(uint64_t(context->stateUnknown[1]), 0u);
  CanonicalPlane copied = context->stateUnknown;
  EXPECT_EQ(copied.data(), nullptr);
  EXPECT_EQ(copied, context->stateUnknown);

  alignas(8) uint8_t values[32]{};
  const talos_rt_native_state_fill_v1 fills[] = {{0, 65, 1, 1},
                                                   {65, 1, 1, 0}};
  ASSERT_EQ(talos_rt_v1_native_state_initialize(context, values, nullptr, 129,
                                                  fills, 2),
            TALOS_RT_OK);
  EXPECT_EQ(values[0], 0u);
  EXPECT_EQ(values[8], 2u);
  ASSERT_EQ(
      talos_rt_v1_native_state_bind_shared(context, values, nullptr, 129),
      TALOS_RT_OK);
  EXPECT_EQ(context->stateUnknown.data(), nullptr);
  uint8_t *retained = nullptr, *unknown = nullptr, *mask = nullptr;
  ASSERT_EQ(talos_rt_v1_native_state_bind_continuous(context, &retained,
                                                       &unknown, &mask),
            TALOS_RT_OK);
  EXPECT_NE(retained, nullptr);
  EXPECT_NE(mask, nullptr);
  EXPECT_EQ(unknown, nullptr);
  EXPECT_EQ(context->continuousUnknown.data(), nullptr);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeDPI, ValidatesDispatchContextAndNestedRestoration) {
  static constexpr char topName[] = "top";
  static constexpr char childName[] = "top.child";
  const talos_rt_dpi_scope_v1 scopes[] = {
      {0, UINT64_MAX, topName, sizeof(topName) - 1, -9, -12, 0},
      {1, 0, childName, sizeof(childName) - 1, -9, -12, 0},
  };
  talos_rt_execution_descriptor_v1 execution{
      TALOS_RT_VERSION, 0,   0, nullptr, 0, nullptr, 0, 0, 0, scopes,
      std::size(scopes),  -12, 0, nullptr, 0, nullptr, 0};
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  DpiObservation observation;
  constexpr std::string_view outerName = "outer";
  constexpr std::string_view innerName = "inner";
  uint32_t outerID = talos_rt_v1_import_id(
      reinterpret_cast<const uint8_t *>(outerName.data()), outerName.size());
  observation.nestedID = talos_rt_v1_import_id(
      reinterpret_cast<const uint8_t *>(innerName.data()), innerName.size());
  static constexpr char caller[] = "dpi_test.sv";
  observation.nestedSite = {TALOS_RT_VERSION,
                            TALOS_RT_IMPORT_CONTEXT,
                            observation.nestedID,
                            0,
                            1,
                            caller,
                            sizeof(caller) - 1,
                            41,
                            7,
                            0};
  observation.invokeNested = true;
  ASSERT_EQ(talos_rt_v1_context_register_import(context, outerID,
                                                  observeDpiCall, &observation),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_context_register_import(context, observation.nestedID,
                                                  observeDpiCall, &observation),
            TALOS_RT_OK);

  uint64_t inputValue[2]{UINT64_C(0x123456789abcdef0), 1};
  uint64_t outputValue[2]{UINT64_MAX, UINT64_MAX};
  talos_rt_import_input_v1 input{
      TALOS_RT_DBREG_BITS, 0, 0, 65, inputValue, nullptr, 2};
  talos_rt_import_output_v1 output{TALOS_RT_DBREG_BITS, 0,       0, 65,
                                     outputValue,           nullptr, 2};
  talos_rt_import_site_v1 site{TALOS_RT_VERSION,
                                 TALOS_RT_IMPORT_CONTEXT,
                                 outerID,
                                 0,
                                 0,
                                 caller,
                                 sizeof(caller) - 1,
                                 41,
                                 3,
                                 0};
  EXPECT_EQ(talos_rt_v1_import_call(context, &site, &input, 1, &output, 1),
            TALOS_RT_OK);
  EXPECT_EQ(observation.calls, 2u);
  EXPECT_EQ(outputValue[0], inputValue[0] ^ UINT64_C(0xffff));
  EXPECT_EQ(outputValue[1], 1u);
  EXPECT_EQ(svGetScope(), nullptr);

  site.reserved = 1;
  EXPECT_EQ(talos_rt_v1_import_call(context, &site, &input, 1, &output, 1),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(observation.calls, 2u);
  site.reserved = 0;
  site.import_id ^= UINT32_C(0x40000000);
  EXPECT_EQ(talos_rt_v1_import_call(context, &site, &input, 1, &output, 1),
            TALOS_RT_TIER_UNAVAILABLE);
  EXPECT_EQ(observation.calls, 2u);
  site.import_id = outerID;
  observation.invokeNested = false;
  ASSERT_EQ(
      talos_rt_v1_context_register_import_signature(
          context, outerID, UINT64_C(0x1234), observeDpiCall, &observation),
      TALOS_RT_OK);
  site.abi_signature = UINT64_C(0x5678);
  EXPECT_EQ(talos_rt_v1_import_call(context, &site, &input, 1, &output, 1),
            TALOS_RT_ARGUMENT_MISMATCH);
  EXPECT_EQ(observation.calls, 2u);
  site.abi_signature = UINT64_C(0x1234);
  EXPECT_EQ(talos_rt_v1_import_call(context, &site, &input, 1, &output, 1),
            TALOS_RT_OK);
  EXPECT_EQ(observation.calls, 3u);
  talos_rt_v1_context_destroy(context);
}

talos_rt_status observeNoncontextDpi(talos_rt_context *, uint32_t,
                                       const talos_rt_import_input_v1 *,
                                       uint32_t, talos_rt_import_output_v1 *,
                                       uint32_t, void *userData) {
  auto &calls = *static_cast<uint32_t *>(userData);
  ++calls;
  EXPECT_EQ(talos_rt_v1_dpi_current_context(), nullptr);
  EXPECT_EQ(svGetScope(), nullptr);
  const char *file = nullptr;
  int line = 0;
  EXPECT_EQ(svGetCallerInfo(&file, &line), 0);
  return TALOS_RT_OK;
}

TEST(RuntimeDPI, NoncontextBoundaryHasNoActiveCallState) {
  talos_rt_execution_descriptor_v1 execution{TALOS_RT_VERSION,
                                               0,
                                               0,
                                               nullptr,
                                               0,
                                               nullptr,
                                               0,
                                               0,
                                               0,
                                               nullptr,
                                               0,
                                               0,
                                               0,
                                               nullptr,
                                               0,
                                               nullptr,
                                               0};
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  constexpr std::string_view name = "noncontext";
  uint32_t importID = talos_rt_v1_import_id(
      reinterpret_cast<const uint8_t *>(name.data()), name.size());
  uint32_t calls = 0;
  ASSERT_EQ(talos_rt_v1_context_register_import(context, importID,
                                                  observeNoncontextDpi, &calls),
            TALOS_RT_OK);
  talos_rt_import_site_v1 site{
      TALOS_RT_VERSION, 0, importID, 0, UINT64_MAX, nullptr, 0, 0, 0, 0};
  EXPECT_EQ(talos_rt_v1_import_call_noncontext(context, &site, nullptr, 0,
                                                 nullptr, 0),
            TALOS_RT_OK);
  EXPECT_EQ(calls, 1u);
  site.flags = TALOS_RT_IMPORT_CONTEXT;
  EXPECT_EQ(talos_rt_v1_import_call_noncontext(context, &site, nullptr, 0,
                                                 nullptr, 0),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(calls, 1u);
  talos_rt_v1_context_destroy(context);
}

talos_rt_status observeDpiTime(talos_rt_context *, uint32_t,
                                 const talos_rt_import_input_v1 *, uint32_t,
                                 talos_rt_import_output_v1 *, uint32_t,
                                 void *userData) {
  ++*static_cast<uint32_t *>(userData);
  // IEEE 1800-2023 H.13: a scope reports its own time unit and precision; a
  // NULL scope reports simulation time, whose unit is the global precision.
  svScope top = svGetScope();
  svScope child = svGetScopeFromName("top.child");
  int32_t unit = 0;
  int32_t precision = 0;
  EXPECT_EQ(svGetTimeUnit(top, &unit), 0);
  EXPECT_EQ(unit, -9);
  EXPECT_EQ(svGetTimePrecision(top, &precision), 0);
  EXPECT_EQ(precision, -12);
  EXPECT_EQ(svGetTimeUnit(child, &unit), 0);
  EXPECT_EQ(unit, -6);
  EXPECT_EQ(svGetTimePrecision(child, &precision), 0);
  EXPECT_EQ(precision, -9);
  EXPECT_EQ(svGetTimeUnit(nullptr, &unit), 0);
  EXPECT_EQ(unit, -12);
  EXPECT_EQ(svGetTimePrecision(nullptr, &precision), 0);
  EXPECT_EQ(precision, -12);

  // Simulation time counts global-precision ticks; scaled real time is in
  // the scope's time unit, like vpi_get_time().
  svTimeVal time{sv_sim_time, 0, 0, 0.0};
  EXPECT_EQ(svGetTime(child, &time), 0);
  EXPECT_EQ(time.high, 0u);
  EXPECT_EQ(time.low, 1500000u);
  time.type = sv_scaled_real_time;
  EXPECT_EQ(svGetTime(top, &time), 0);
  EXPECT_DOUBLE_EQ(time.real, 1500.0);
  EXPECT_EQ(svGetTime(child, &time), 0);
  EXPECT_DOUBLE_EQ(time.real, 1.5);
  EXPECT_EQ(svGetTime(nullptr, &time), 0);
  EXPECT_DOUBLE_EQ(time.real, 1500000.0);

  EXPECT_EQ(svGetTimeUnit(&unit, &unit), -1);
  EXPECT_EQ(svGetTimeUnit(top, nullptr), -1);
  time.type = vpiSuppressTime;
  EXPECT_EQ(svGetTime(top, &time), -1);
  return TALOS_RT_OK;
}

TEST(RuntimeDPI, ReportsScopeTimeUnitsAndTime) {
  static constexpr char topName[] = "top";
  static constexpr char childName[] = "top.child";
  const talos_rt_dpi_scope_v1 scopes[] = {
      {0, UINT64_MAX, topName, sizeof(topName) - 1, -9, -12, 0},
      {1, 0, childName, sizeof(childName) - 1, -6, -9, 0},
  };
  talos_rt_execution_descriptor_v1 execution{
      TALOS_RT_VERSION, 0,   0, nullptr, 0, nullptr, 0, 0, 0, scopes,
      std::size(scopes),  -12, 0, nullptr, 0, nullptr, 0};
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  constexpr std::string_view name = "time";
  uint32_t importID = talos_rt_v1_import_id(
      reinterpret_cast<const uint8_t *>(name.data()), name.size());
  uint32_t calls = 0;
  ASSERT_EQ(talos_rt_v1_context_register_import(context, importID,
                                                  observeDpiTime, &calls),
            TALOS_RT_OK);
  context->schedulerTime = 1500000;
  talos_rt_import_site_v1 site{TALOS_RT_VERSION,
                                 TALOS_RT_IMPORT_CONTEXT,
                                 importID,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 0,
                                 0,
                                 0};
  EXPECT_EQ(talos_rt_v1_import_call(context, &site, nullptr, 0, nullptr, 0),
            TALOS_RT_OK);
  EXPECT_EQ(calls, 1u);
  // Outside a context call there is no scope or simulation time to report.
  int32_t unit = 0;
  EXPECT_EQ(svGetTimeUnit(nullptr, &unit), -1);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeDPI, AggregatePlanSupportsDeepLegalNesting) {
  constexpr uint64_t depth = 65;
  std::vector<int64_t> plan;
  plan.reserve((depth + 1) * 8);
  for (uint64_t level = 0; level != depth; ++level)
    plan.insert(plan.end(),
                {1, 0, 0, 1, 1, 1, 0, static_cast<int64_t>(depth - level)});
  plan.insert(plan.end(), {0, 0, 0, 2, 8, 0, 0, 0});
  uint8_t value = 0x5a;
  uint8_t result = 0;
  ASSERT_EQ(talos_rt_v1_dpi_aggregate_pack(&value, nullptr, 1, 8, 0, &result,
                                             1, 1, plan.data(), plan.size()),
            TALOS_RT_OK);
  EXPECT_EQ(result, value);
}

struct DpiDisableObservation {
  bool acknowledge = false;
  bool tryExportAfterDisable = false;
  talos_rt_status result = TALOS_RT_OK;
};

talos_rt_status observeDpiDisable(talos_rt_context *, uint32_t,
                                    const talos_rt_import_input_v1 *,
                                    uint32_t, talos_rt_import_output_v1 *,
                                    uint32_t, void *userData) {
  auto &observation = *static_cast<DpiDisableObservation *>(userData);
  EXPECT_EQ(svIsDisabledState(), 0);
  EXPECT_NE(activeDpiCall, nullptr);
  if (!activeDpiCall)
    return TALOS_RT_FATAL;
  activeDpiCall->disabledState = true;
  EXPECT_EQ(svIsDisabledState(), 1);
  if (observation.tryExportAfterDisable) {
    EXPECT_EQ(talos_rt_v1_export_call(1, 1, nullptr, 0, nullptr, 0),
              TALOS_RT_FATAL);
  }
  if (observation.acknowledge) {
    svAckDisabledState();
    EXPECT_EQ(svIsDisabledState(), 0);
  }
  return observation.result;
}

TEST(RuntimeDPI, EnforcesFunctionAndTaskDisableProtocol) {
  static constexpr char scopeName[] = "top";
  const talos_rt_dpi_scope_v1 scopes[] = {
      {0, UINT64_MAX, scopeName, sizeof(scopeName) - 1, -9, -12, 0},
  };
  talos_rt_execution_descriptor_v1 execution{
      TALOS_RT_VERSION, 0,   0, nullptr, 0, nullptr, 0, 0, 0, scopes,
      std::size(scopes),  -12, 0, nullptr, 0, nullptr, 0};
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  constexpr uint32_t importID = UINT32_C(0x12345678);
  DpiDisableObservation observation;
  ASSERT_EQ(talos_rt_v1_context_register_import(
                context, importID, observeDpiDisable, &observation),
            TALOS_RT_OK);
  talos_rt_import_site_v1 site{TALOS_RT_VERSION,
                                 TALOS_RT_IMPORT_CONTEXT,
                                 importID,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 0,
                                 0,
                                 0};

  observation.acknowledge = true;
  observation.tryExportAfterDisable = true;
  EXPECT_EQ(talos_rt_v1_import_call(context, &site, nullptr, 0, nullptr, 0),
            TALOS_RT_DPI_DISABLE_UNSUPPORTED);

  observation.acknowledge = false;
  observation.tryExportAfterDisable = false;
  EXPECT_EQ(talos_rt_v1_import_call(context, &site, nullptr, 0, nullptr, 0),
            TALOS_RT_FATAL);

  site.flags |= TALOS_RT_IMPORT_TASK;
  observation.result = TALOS_RT_DPI_DISABLE_UNSUPPORTED;
  EXPECT_EQ(talos_rt_v1_import_call(context, &site, nullptr, 0, nullptr, 0),
            TALOS_RT_DPI_DISABLE_UNSUPPORTED);

  observation.result = TALOS_RT_OK;
  EXPECT_EQ(talos_rt_v1_import_call(context, &site, nullptr, 0, nullptr, 0),
            TALOS_RT_FATAL);
  talos_rt_v1_context_destroy(context);
}

struct DpiRealObservation {
  uint32_t calls = 0;
};

talos_rt_status observeDpiRealCall(talos_rt_context *, uint32_t,
                                     const talos_rt_import_input_v1 *inputs,
                                     uint32_t inputCount,
                                     talos_rt_import_output_v1 *outputs,
                                     uint32_t outputCount, void *userData) {
  auto &observation = *static_cast<DpiRealObservation *>(userData);
  ++observation.calls;
  EXPECT_EQ(inputCount, 2u);
  EXPECT_EQ(outputCount, 2u);
  EXPECT_EQ(inputs[0].kind, TALOS_RT_DBREG_REAL32);
  EXPECT_EQ(inputs[1].kind, TALOS_RT_DBREG_REAL64);
  EXPECT_EQ(outputs[0].kind, TALOS_RT_DBREG_REAL32);
  EXPECT_EQ(outputs[1].kind, TALOS_RT_DBREG_REAL64);

  float narrow = 0.0f;
  double wide = 0.0;
  float zeroNarrow = 1.0f;
  double zeroWide = 1.0;
  std::memcpy(&narrow, inputs[0].value, sizeof(narrow));
  std::memcpy(&wide, inputs[1].value, sizeof(wide));
  std::memcpy(&zeroNarrow, outputs[0].value, sizeof(zeroNarrow));
  std::memcpy(&zeroWide, outputs[1].value, sizeof(zeroWide));
  EXPECT_FLOAT_EQ(narrow, 1.25f);
  EXPECT_DOUBLE_EQ(wide, 2.5);
  EXPECT_FLOAT_EQ(zeroNarrow, 0.0f);
  EXPECT_DOUBLE_EQ(zeroWide, 0.0);

  narrow += 0.5f;
  wide += 0.25;
  std::memcpy(outputs[0].value, &narrow, sizeof(narrow));
  std::memcpy(outputs[1].value, &wide, sizeof(wide));
  return TALOS_RT_OK;
}

TEST(RuntimeDPI, MarshalsBinary32AndBinary64WithoutOverwritingNeighbors) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  DpiRealObservation observation;
  constexpr std::string_view name = "dpi_reals";
  uint32_t importID = talos_rt_v1_import_id(
      reinterpret_cast<const uint8_t *>(name.data()), name.size());
  ASSERT_EQ(talos_rt_v1_context_register_import(
                context, importID, observeDpiRealCall, &observation),
            TALOS_RT_OK);

  alignas(8) float narrowInput = 1.25f;
  double wideInput = 2.5;
  const talos_rt_import_input_v1 inputs[] = {
      {TALOS_RT_DBREG_REAL32, 0, 0, 32,
       reinterpret_cast<const uint64_t *>(&narrowInput), nullptr, 1},
      {TALOS_RT_DBREG_REAL64, 0, 0, 64,
       reinterpret_cast<const uint64_t *>(&wideInput), nullptr, 1},
  };
  struct alignas(8) NarrowOutput {
    float value;
    uint32_t canary;
  } narrowOutput{99.0f, UINT32_C(0xdeadbeef)};
  double wideOutput = 99.0;
  talos_rt_import_output_v1 outputs[] = {
      {TALOS_RT_DBREG_REAL32, 0, 0, 32,
       reinterpret_cast<uint64_t *>(&narrowOutput.value), nullptr, 1},
      {TALOS_RT_DBREG_REAL64, 0, 0, 64,
       reinterpret_cast<uint64_t *>(&wideOutput), nullptr, 1},
  };
  talos_rt_import_site_v1 site{
      TALOS_RT_VERSION, 0, importID, 0, UINT64_MAX, nullptr, 0, 0, 0, 0};
  EXPECT_EQ(talos_rt_v1_import_call(context, &site, inputs, 2, outputs, 2),
            TALOS_RT_OK);
  EXPECT_EQ(observation.calls, 1u);
  EXPECT_FLOAT_EQ(narrowOutput.value, 1.75f);
  EXPECT_EQ(narrowOutput.canary, UINT32_C(0xdeadbeef));
  EXPECT_DOUBLE_EQ(wideOutput, 2.75);

  talos_rt_import_input_v1 invalid = inputs[0];
  invalid.flags = TALOS_RT_DBREG_SIGNED;
  EXPECT_EQ(talos_rt_v1_import_call(context, &site, &invalid, 1, outputs, 2),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(observation.calls, 1u);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeDPI, RejectsMalformedScopeMetadata) {
  static constexpr char name[] = "top";
  talos_rt_dpi_scope_v1 scope{1,  UINT64_MAX, name, sizeof(name) - 1,
                                -9, -12,        0};
  talos_rt_execution_descriptor_v1 execution{TALOS_RT_VERSION,
                                               0,
                                               0,
                                               nullptr,
                                               0,
                                               nullptr,
                                               0,
                                               0,
                                               0,
                                               &scope,
                                               1,
                                               -12,
                                               0,
                                               nullptr,
                                               0,
                                               nullptr,
                                               0};
  talos_rt_context *context = nullptr;
  EXPECT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_INVALID_DESIGN);
  EXPECT_EQ(context, nullptr);

  scope.id = 0;
  scope.time_precision = -15;
  EXPECT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_INVALID_DESIGN);
  EXPECT_EQ(context, nullptr);
}

TEST(RuntimeDPI, AcceptsPositiveLegalTimeUnitExponents) {
  static constexpr char name[] = "top";
  const talos_rt_dpi_scope_v1 scope{0, UINT64_MAX, name, sizeof(name) - 1,
                                      2, -15,        0};
  const talos_rt_execution_descriptor_v1 execution{TALOS_RT_VERSION,
                                                     0,
                                                     0,
                                                     nullptr,
                                                     0,
                                                     nullptr,
                                                     0,
                                                     0,
                                                     0,
                                                     &scope,
                                                     1,
                                                     -15,
                                                     0,
                                                     nullptr,
                                                     0,
                                                     nullptr,
                                                     0};
  talos_rt_context *context = nullptr;
  EXPECT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  EXPECT_NE(context, nullptr);
  talos_rt_v1_context_destroy(context);
}

TEST(RuntimeABI, RejectsNullPublicArguments) {
  EXPECT_EQ(talos_rt_v1_context_create(nullptr), TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_import_id(nullptr, 0), 0u);
  EXPECT_EQ(talos_rt_v1_import_id(nullptr, 1), 0u);
  EXPECT_EQ(talos_rt_v1_context_register_import(nullptr, 1, nullptr, nullptr),
            TALOS_RT_INVALID_ARGUMENT);
  talos_rt_v1_context_destroy(nullptr);
  EXPECT_EQ(talos_rt_v1_last_error(nullptr, nullptr),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(
      talos_rt_v1_format(nullptr, nullptr, 0, nullptr, 0, nullptr, nullptr),
      TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_display(nullptr, 0, 0, TALOS_RT_RADIX_DECIMAL,
                                  nullptr, 0, nullptr),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_file_open_mcd(nullptr, nullptr, 0, nullptr),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_file_open(nullptr, nullptr, 0, nullptr, 0, nullptr),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_file_close(nullptr, 0), TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_file_flush(nullptr, 0), TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_file_write(nullptr, 0, nullptr, 0, nullptr),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_file_read(nullptr, 0, nullptr, 0, nullptr),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_file_getc(nullptr, 0, nullptr),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_file_ungetc(nullptr, 0, 0),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_file_getline(nullptr, 0, 0, nullptr),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_file_eof(nullptr, 0, nullptr),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_file_error(nullptr, 0, nullptr, nullptr),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_file_seek(nullptr, 0, 0, TALOS_RT_SEEK_SET),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_file_tell(nullptr, 0, nullptr),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_file_rewind(nullptr, 0), TALOS_RT_INVALID_ARGUMENT);
}

TEST_F(RuntimeTest, FormatsExactFourStateRadices) {
  LogicValue value("10xz01xz");
  auto [status, output] =
      format("%b %o %h", {value.arg(), value.arg(), value.arg()});
  EXPECT_EQ(status, TALOS_RT_OK);
  EXPECT_EQ(output, "10xz01xz 2XX XX");

  LogicValue leading("0000xxxx");
  auto [leadingStatus, leadingOutput] =
      format("%h %0h %4h %-4h",
             {leading.arg(), leading.arg(), leading.arg(), leading.arg()});
  EXPECT_EQ(leadingStatus, TALOS_RT_OK);
  EXPECT_EQ(leadingOutput, "0x 0x 000x 0x00");
}

TEST_F(RuntimeTest, FormatsRadixGroupsAcrossWordAndPartialLimbBoundaries) {
  LogicValue padded(65, {UINT64_MAX, UINT64_MAX}, {0, UINT64_MAX});
  EXPECT_EQ(format("%b", {padded.arg()}).second, "z" + std::string(64, '1'));
  EXPECT_EQ(format("%o", {padded.arg()}).second, "Z" + std::string(21, '7'));
  EXPECT_EQ(format("%h", {padded.arg()}).second, "z" + std::string(16, 'f'));
  LogicValue one(1, {UINT64_MAX}, {UINT64_MAX ^ 1});
  EXPECT_EQ(format("%b %o %h", {one.arg(), one.arg(), one.arg()}).second,
            "1 1 1");
  for (unsigned width = 1; width <= 193; ++width) {
    for (std::string_view pattern :
         {"0", "1", "x", "z", "001101", "01xz", "00z11", "xx001"}) {
      std::string bits;
      for (unsigned bit = 0; bit != width; ++bit)
        bits.push_back(pattern[bit % pattern.size()]);
      LogicValue value(bits);
      for (unsigned groupBits : {1u, 3u, 4u}) {
        std::string expected;
        for (unsigned begin = 0; begin != width;) {
          unsigned size = begin == 0 && width % groupBits != 0
                              ? width % groupBits
                              : groupBits;
          std::string group = bits.substr(begin, size);
          begin += size;
          if (group == std::string(size, 'x'))
            expected += 'x';
          else if (group == std::string(size, 'z'))
            expected += 'z';
          else if (group.find('x') != std::string::npos)
            expected += 'X';
          else if (group.find('z') != std::string::npos)
            expected += 'Z';
          else {
            unsigned digit = 0;
            for (char bit : group)
              digit = digit * 2 + (bit == '1');
            expected += "0123456789abcdef"[digit];
          }
        }
        std::string specifier = groupBits == 1   ? "%b"
                                : groupBits == 3 ? "%o"
                                                 : "%h";
        auto [status, output] = format(specifier, {value.arg()});
        ASSERT_EQ(status, TALOS_RT_OK);
        ASSERT_EQ(output, expected)
            << "width=" << width << " bits=" << bits << " format=" << specifier;
        if (bits.find_first_of("xz") == std::string::npos) {
          auto known = value.arg();
          known.unknown = nullptr;
          ASSERT_EQ(format(specifier, {known}).second, expected);
        }
      }
    }
  }
}

TEST_F(RuntimeTest, FormatsUnknownAndSignedDecimal) {
  LogicValue allX("xxxxxxxx");
  LogicValue allZ("zzzzzzzz");
  LogicValue mixedX("000x0001");
  LogicValue mixedZ("000z0001");
  auto [unknownStatus, unknownOutput] = format(
      "%0d %0d %0d %0d", {allX.arg(), allZ.arg(), mixedX.arg(), mixedZ.arg()});
  EXPECT_EQ(unknownStatus, TALOS_RT_OK);
  EXPECT_EQ(unknownOutput, "x z X Z");

  LogicValue negativeFiveBits("11101", true);
  LogicValue unsignedFiveBits("00011");
  auto [smallStatus, smallOutput] =
      format("%d|%0d|%d", {negativeFiveBits.arg(), negativeFiveBits.arg(),
                           unsignedFiveBits.arg()});
  EXPECT_EQ(smallStatus, TALOS_RT_OK);
  EXPECT_EQ(smallOutput, " -3|-3| 3");

  // IEEE 1800-2017 21.2.1.3: the default %d field is wide enough for the
  // largest value the operand can hold, which for a signed operand is one
  // magnitude bit narrower than the operand itself plus room for the sign.
  LogicValue fourteenBits("00111111111111", true);
  LogicValue sixteenBits("0000001111111111", true);
  auto [signedWidthStatus, signedWidthOutput] =
      format("%d|%d", {fourteenBits.arg(), sixteenBits.arg()});
  EXPECT_EQ(signedWidthStatus, TALOS_RT_OK);
  EXPECT_EQ(signedWidthOutput, " 4095|  1023");

  // A real operand of an integer format converts by rounding, which leaves
  // infinities and NaN with no numeric rendering. Report them rather than
  // failing the whole format, which would abort the display.
  double infinity = std::numeric_limits<double>::infinity();
  double notANumber = std::numeric_limits<double>::quiet_NaN();
  double negativeInfinity = -infinity;
  auto [nonFiniteStatus, nonFiniteOutput] =
      format("%d|%0d|%h", {realArg(infinity), realArg(negativeInfinity),
                           realArg(notANumber)});
  EXPECT_EQ(nonFiniteStatus, TALOS_RT_OK);
  EXPECT_EQ(nonFiniteOutput, "inf|-inf|nan");

  // IEEE 1800-2017 6.12.2: converting an integral value to a real treats its
  // x and z bits as zero, so a real format still renders an unknown operand.
  LogicValue unknownReal("0000000000001x1z");
  LogicValue allUnknownReal("xxxxxxxx");
  auto [realStatus, realOutput] =
      format("%f|%0.1f", {unknownReal.arg(), allUnknownReal.arg()});
  EXPECT_EQ(realStatus, TALOS_RT_OK);
  EXPECT_EQ(realOutput, "10.000000|0.0");

  LogicValue sixtyFiveBits(65, {0, 1});
  auto [wideStatus, wideOutput] = format("%0d", {sixtyFiveBits.arg()});
  EXPECT_EQ(wideStatus, TALOS_RT_OK);
  EXPECT_EQ(wideOutput, "18446744073709551616");
}

TEST_F(RuntimeTest, FormatsArbitraryNonPowerOfTwoWidths) {
  LogicValue oneBit("z");
  EXPECT_EQ(format("%b", {oneBit.arg()}).second, "z");

  std::string symbols = "1" + std::string(126, '0') + "xz0";
  ASSERT_EQ(symbols.size(), 130u);
  LogicValue wide(symbols);
  auto [status, output] = format("%b", {wide.arg()});
  EXPECT_EQ(status, TALOS_RT_OK);
  EXPECT_EQ(output, symbols);

  LogicValue negativeSixtyFiveBits(65, {~uint64_t{2}, 1}, {},
                                   /*isSigned=*/true);
  LogicValue ignoredPaddingBits(65, {3, ~uint64_t{0}}, {0, ~uint64_t{1}});
  auto [decimalStatus, decimalOutput] = format(
      "%0d %0d", {negativeSixtyFiveBits.arg(), ignoredPaddingBits.arg()});
  EXPECT_EQ(decimalStatus, TALOS_RT_OK);
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
  talos_rt_format_env_v1 environment{scope.data(),
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
  EXPECT_EQ(status, TALOS_RT_OK);
  EXPECT_EQ(output,
            "[000a][a000][a][  ok][ok  ] 3.25 top.worker work.top 1000ns%");

  uint64_t largestTime = std::numeric_limits<uint64_t>::max();
  auto [largeStatus, largeOutput] =
      format("%0t", {timeArg(largestTime)}, &environment);
  EXPECT_EQ(largeStatus, TALOS_RT_OK);
  EXPECT_EQ(largeOutput, "1844674407370955161500ns");

  auto [realTimeStatus, realTimeOutput] =
      format("%0t", {realArg(real)}, &environment);
  EXPECT_EQ(realTimeStatus, TALOS_RT_OK);
  EXPECT_EQ(realTimeOutput, "325ns");
}

TEST_F(RuntimeTest, FormatsRemainingScalarFormsAndEmptyStrings) {
  LogicValue letter("01000001");
  double real = 12.5;
  std::string pattern = "pattern";
  talos_rt_arg_v1 emptyString{TALOS_RT_ARG_STRING, 0, 0, nullptr, nullptr};

  auto [status, output] =
      format("%c|%.1e|%.1E|%.3g|%p|%p|[%s]",
             {letter.arg(), realArg(real), realArg(real), realArg(real),
              realArg(real), stringArg(pattern), emptyString});
  EXPECT_EQ(status, TALOS_RT_OK);
  EXPECT_EQ(output, "A|1.2e+01|1.2E+01|12.5|12.5|pattern|[]");

  double padded = 3.25;
  auto [paddingStatus, paddingOutput] =
      format("[%08.2f][%-8.2f]", {realArg(padded), realArg(padded)});
  EXPECT_EQ(paddingStatus, TALOS_RT_OK);
  EXPECT_EQ(paddingOutput, "[00003.25][3.25    ]");

  double positive = 3.7;
  double negative = -3.7;
  auto [integerStatus, integerOutput] =
      format("%0d %0d", {realArg(positive), realArg(negative)});
  EXPECT_EQ(integerStatus, TALOS_RT_OK);
  EXPECT_EQ(integerOutput, "4 -4");

  uint64_t time = 10;
  auto [timeStatus, timeOutput] = format("[%t]", {timeArg(time)});
  EXPECT_EQ(timeStatus, TALOS_RT_OK);
  EXPECT_EQ(timeOutput, "[" + std::string(18, ' ') + "10]");
}

TEST_F(RuntimeTest, ScalesTimeInputThroughCurrentTimeFormat) {
  // Default input units are design precision (100 ps here), rounded to zero
  // fractional digits, then converted to the caller's 1 ns unit.
  EXPECT_DOUBLE_EQ(talos_rt_v1_time_scan_scale(context, 10.5, 10, -10), 1.1);

  ASSERT_EQ(talos_rt_v1_time_format(context, -9, 1, nullptr, 0, 0),
            TALOS_RT_OK);
  EXPECT_DOUBLE_EQ(talos_rt_v1_time_scan_scale(context, 1.25, 10, -10), 1.3);
  EXPECT_DOUBLE_EQ(talos_rt_v1_time_scan_scale(context, -1.25, 10, -10),
                   -1.3);

  // Changing the design-global state takes effect at the next scan. Picosecond
  // input is finer than the same caller's 1 ns time unit.
  ASSERT_EQ(talos_rt_v1_time_format(context, -12, 0, nullptr, 0, 0),
            TALOS_RT_OK);
  EXPECT_DOUBLE_EQ(talos_rt_v1_time_scan_scale(context, 1250.0, 10, -10),
                   1.25);
  EXPECT_TRUE(std::isinf(talos_rt_v1_time_scan_scale(
      context, std::numeric_limits<double>::infinity(), 10, -10)));
}

TEST_F(RuntimeTest, FormatsDefaultScalarStrengths) {
  LogicValue zero("0");
  LogicValue one("1");
  LogicValue unknown("x");
  LogicValue highz("z");
  auto [status, output] = format(
      "%v|%v|%v|%v", {zero.arg(), one.arg(), unknown.arg(), highz.arg()});
  EXPECT_EQ(status, TALOS_RT_OK);
  EXPECT_EQ(output, "St0|St1|StX|HiZ");
}

TEST_F(RuntimeTest, StrengthInputAcceptsEveryCanonicalOutputField) {
  std::unordered_set<std::string> emittedFields;
  for (uint32_t strengths = 1; strengths != (uint32_t{1} << 15); ++strengths) {
    std::string field =
        talos_rt_format_strength_range(static_cast<uint16_t>(strengths));
    ASSERT_EQ(field.size(), 3u);
    char logic = 0;
    ASSERT_TRUE(
        talos_rt_parse_strength_field(field.data(), field.size(), logic))
        << field;
    char expected = field[2] == 'L' ? '0' : field[2] == 'H' ? '1' : field[2];
    EXPECT_EQ(logic, expected) << field;
    emittedFields.insert(field);
  }

  // Conversely, every alphanumeric three-byte spelling accepted by the
  // scanner must be one the canonical formatter can emit. This rejects case
  // variants, equal strength pairs, ranges through level zero, and impossible
  // combinations such as Hi0 or StZ.
  constexpr std::string_view alphabet =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  constexpr std::string_view components = "01XZLH";
  for (char first : alphabet)
    for (char second : alphabet)
      for (char component : components) {
        std::string field{first, second, component};
        char logic = 0;
        bool accepted =
            talos_rt_parse_strength_field(field.data(), field.size(), logic);
        EXPECT_EQ(accepted, emittedFields.count(field) != 0) << field;
      }
}

// IEEE 1800-2017 21.2.1.7: a singular value that is not a packed structure,
// an enumeration, or a string "shall print its value as they would
// unformatted", and 21.2.1 makes the unformatted rendering of $display the
// default decimal format. An unknown bit makes the whole field unknown, just
// as %d does.
TEST_F(RuntimeTest, FormatsPackedStringsAndScalarPatterns) {
  LogicValue packed("010000010000000001000010"); // "A", NUL, "B"
  LogicValue partlyUnknown("10xz", true);
  LogicValue whollyUnknown("xxxx", true);
  LogicValue negative("1101", true);
  LogicValue positive("0101");
  auto [status, output] = format(
      "%s %p %p %p %p", {packed.arg(), partlyUnknown.arg(), whollyUnknown.arg(),
                         negative.arg(), positive.arg()});
  EXPECT_EQ(status, TALOS_RT_OK);
  EXPECT_EQ(output, "A B X x -3 5");
}

TEST_F(RuntimeTest, EmitsRawTwoAndFourStateChunks) {
  LogicValue value("x00000000000000000000000000000001");
  auto [twoStatus, rawTwo] = format("%u", {value.arg()});
  ASSERT_EQ(twoStatus, TALOS_RT_OK);
  ASSERT_EQ(rawTwo.size(), 8u);
  uint32_t twoWords[2];
  std::memcpy(twoWords, rawTwo.data(), sizeof(twoWords));
  EXPECT_EQ(twoWords[0], 1u);
  EXPECT_EQ(twoWords[1], 0u);

  auto [fourStatus, rawFour] = format("%z", {value.arg()});
  ASSERT_EQ(fourStatus, TALOS_RT_OK);
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
  EXPECT_EQ(format("%q", {value.arg()}).first, TALOS_RT_FORMAT_ERROR);
  EXPECT_EQ(format("%", {}).first, TALOS_RT_FORMAT_ERROR);
  EXPECT_EQ(format("%42949672960d", {value.arg()}).first,
            TALOS_RT_FORMAT_ERROR);
  EXPECT_EQ(format("%.42949672960f", {value.arg()}).first,
            TALOS_RT_FORMAT_ERROR);
  EXPECT_EQ(format("%.2d", {value.arg()}).first, TALOS_RT_FORMAT_ERROR);
  EXPECT_EQ(format("%2c", {value.arg()}).first, TALOS_RT_FORMAT_ERROR);
  EXPECT_EQ(format("%d", {}).first, TALOS_RT_ARGUMENT_MISMATCH);
  EXPECT_EQ(format("", {value.arg()}).first, TALOS_RT_ARGUMENT_MISMATCH);
  EXPECT_EQ(format("%s", {value.arg()}).first, TALOS_RT_OK);
  EXPECT_EQ(format("%f", {value.arg()}).first, TALOS_RT_OK);

  talos_rt_format_env_v1 invalidEnvironment{};
  invalidEnvironment.scope_size = 1;
  EXPECT_EQ(format("%m", {}, &invalidEnvironment).first,
            TALOS_RT_INVALID_ARGUMENT);
  invalidEnvironment = {};
  invalidEnvironment.time_suffix_size = 1;
  EXPECT_EQ(format("%t", {value.arg()}, &invalidEnvironment).first,
            TALOS_RT_INVALID_ARGUMENT);

  talos_rt_arg_v1 zeroWidth{TALOS_RT_ARG_LOGIC, 0, 0, nullptr, nullptr};
  EXPECT_EQ(format("%d", {zeroWidth}).first, TALOS_RT_ARGUMENT_MISMATCH);

  RuntimeBuffer message;
  ASSERT_EQ(talos_rt_v1_last_error(context, message.out()), TALOS_RT_OK);
  EXPECT_FALSE(message.str().empty());
}

TEST_F(RuntimeTest, FormatsPaddedWidthsAndRealIntegerDefaults) {
  // IEEE 1800-2017 21.2.1.3: a field width is a plain decimal constant, so the
  // zero of `%04d` is part of it rather than a pad character. Decimal fields
  // pad with spaces; hexadecimal, octal, and binary fields pad with zeros.
  LogicValue value("0000000001100101");
  auto [padStatus, padOutput] =
      format("%04d|%08h|%0d|%6d",
             {value.arg(), value.arg(), value.arg(), value.arg()});
  EXPECT_EQ(padStatus, TALOS_RT_OK);
  EXPECT_EQ(padOutput, " 101|00000065|101|   101");

  // The default field width of an integer format comes from the size of its
  // operand, which a real does not have. The standard leaves the case
  // undefined; rendering at the operand's own length keeps the field from
  // reporting the width of the integer the value was staged in.
  double real = 1.4;
  double wide = 20.0;
  auto [realStatus, realOutput] =
      format("R: %d|%d|%5d", {realArg(real), realArg(wide), realArg(wide)});
  EXPECT_EQ(realStatus, TALOS_RT_OK);
  EXPECT_EQ(realOutput, "R: 1|20|   20");
}

TEST_F(RuntimeTest, DisplayHandlesFormatItemsDefaultsAndNewline) {
  TempDirectory temporary;
  uint32_t descriptor = open(temporary.file("display.bin"), "w+b");
  LogicValue value("00001010");
  std::string formatString = "v=%0h ";
  std::string text = "tail";
  std::vector<talos_rt_arg_v1> items = {
      stringArg(formatString, TALOS_RT_ARG_FORMAT_STRING), value.arg(),
      stringArg(text), value.arg()};
  ASSERT_EQ(talos_rt_v1_display(context, descriptor, 1,
                                  TALOS_RT_RADIX_BINARY, items.data(),
                                  items.size(), nullptr),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_file_flush(context, descriptor), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_file_rewind(context, descriptor), TALOS_RT_OK);

  char bytes[64]{};
  uint64_t read = 0;
  ASSERT_EQ(
      talos_rt_v1_file_read(context, descriptor, bytes, sizeof(bytes), &read),
      TALOS_RT_OK);
  EXPECT_EQ(std::string(bytes, static_cast<size_t>(read)),
            "v=a tail00001010\n");
  EXPECT_EQ(talos_rt_v1_file_close(context, descriptor), TALOS_RT_OK);
}

TEST_F(RuntimeTest, DisplayValidatesItemsAndHandlesEmptyValues) {
  TempDirectory temporary;
  uint32_t descriptor = open(temporary.file("display-empty.bin"), "w+b");
  double real = 2.5;
  talos_rt_arg_v1 empty{TALOS_RT_ARG_EMPTY, 0, 0, nullptr, nullptr};
  talos_rt_arg_v1 emptyString{TALOS_RT_ARG_STRING, 0, 0, nullptr, nullptr};
  talos_rt_arg_v1 emptyFormat{
      TALOS_RT_ARG_STRING, TALOS_RT_ARG_FORMAT_STRING, 0, nullptr, nullptr};
  talos_rt_arg_v1 invalidFormat{
      TALOS_RT_ARG_LOGIC, TALOS_RT_ARG_FORMAT_STRING, 1, nullptr, nullptr};
  talos_rt_arg_v1 invalidKind{99, 0, 0, nullptr, nullptr};
  talos_rt_arg_v1 invalidString{TALOS_RT_ARG_STRING, 0, 1, nullptr,
                                  nullptr};
  std::string malformed = "%q";
  talos_rt_arg_v1 malformedFormat =
      stringArg(malformed, TALOS_RT_ARG_FORMAT_STRING);

  EXPECT_EQ(
      talos_rt_v1_display(context, descriptor, 0, 3, nullptr, 0, nullptr),
      TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_display(context, descriptor, 0,
                                  TALOS_RT_RADIX_DECIMAL, &invalidFormat, 1,
                                  nullptr),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_display(context, descriptor, 0,
                                  TALOS_RT_RADIX_DECIMAL, &invalidKind, 1,
                                  nullptr),
            TALOS_RT_ARGUMENT_MISMATCH);
  EXPECT_EQ(talos_rt_v1_display(context, descriptor, 0,
                                  TALOS_RT_RADIX_DECIMAL, &invalidString, 1,
                                  nullptr),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_display(context, descriptor, 0,
                                  TALOS_RT_RADIX_DECIMAL, &malformedFormat, 1,
                                  nullptr),
            TALOS_RT_FORMAT_ERROR);

  std::vector<talos_rt_arg_v1> items = {empty, realArg(real), emptyString,
                                          emptyFormat};
  ASSERT_EQ(talos_rt_v1_display(context, descriptor, 0,
                                  TALOS_RT_RADIX_DECIMAL, items.data(),
                                  items.size(), nullptr),
            TALOS_RT_OK);
  LogicValue value("00001010");
  talos_rt_arg_v1 logic = value.arg();
  ASSERT_EQ(talos_rt_v1_display(context, descriptor, 0,
                                  TALOS_RT_RADIX_OCTAL, &logic, 1, nullptr),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_display(context, descriptor, 0, TALOS_RT_RADIX_HEX,
                                  &logic, 1, nullptr),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_file_flush(context, descriptor), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_file_rewind(context, descriptor), TALOS_RT_OK);

  char bytes[32]{};
  uint64_t read = 0;
  ASSERT_EQ(
      talos_rt_v1_file_read(context, descriptor, bytes, sizeof(bytes), &read),
      TALOS_RT_OK);
  EXPECT_EQ(std::string(bytes, static_cast<size_t>(read)), " 2.5000000120a");
  EXPECT_EQ(talos_rt_v1_file_close(context, descriptor), TALOS_RT_OK);
}

TEST_F(RuntimeTest, EvalDisplayUsesSnapshotsWithoutMonitorOrSchedulerEffects) {
  LogicValue value("10xz");
  std::string formatString = "bits=%b";
  std::vector<talos_rt_arg_v1> items = {
      stringArg(formatString, TALOS_RT_ARG_FORMAT_STRING), value.arg()};
  context->activeLogicalProcessToken = 17;
  context->monitorLogicalProcessToken = 17;
  context->monitorEnabled = false;
  context->monitorReported = true;
  context->monitorReport = "unchanged";
  testing::internal::CaptureStdout();
  EXPECT_EQ(talos_rt_v1_eval_display(context, 1, 1, TALOS_RT_RADIX_DECIMAL,
                                       items.data(), items.size(), nullptr),
            TALOS_RT_OK);
  EXPECT_EQ(testing::internal::GetCapturedStdout(), "bits=10xz\n");
  EXPECT_EQ(context->activeLogicalProcessToken, 17u);
  EXPECT_EQ(context->monitorLogicalProcessToken, 17u);
  EXPECT_EQ(context->monitorReport, "unchanged");
  EXPECT_FALSE(context->monitorEnabled);
  EXPECT_FALSE(context->schedulerFinishRequested);
}

TEST_F(RuntimeTest, EvalDisplayWritesDiagnosticSnapshotsToStandardError) {
  std::string diagnostic = "unique case warning";
  talos_rt_arg_v1 item = stringArg(diagnostic, TALOS_RT_ARG_FORMAT_STRING);
  context->monitorEnabled = false;
  context->monitorReport = "unchanged";
  testing::internal::CaptureStderr();
  EXPECT_EQ(talos_rt_v1_eval_display(context, 0x80000002u, 1,
                                       TALOS_RT_RADIX_DECIMAL, &item, 1,
                                       nullptr),
            TALOS_RT_OK);
  EXPECT_EQ(testing::internal::GetCapturedStderr(), diagnostic + "\n");
  EXPECT_EQ(context->monitorReport, "unchanged");
  EXPECT_FALSE(context->schedulerFinishRequested);
}

TEST_F(RuntimeTest, EvalDisplayRejectsRuntimeOwnedArgumentsAndChannels) {
  EXPECT_EQ(talos_rt_v1_eval_display(context, 2, 0, TALOS_RT_RADIX_DECIMAL,
                                       nullptr, 0, nullptr),
            TALOS_RT_INVALID_ARGUMENT);
  for (uint32_t kind :
       {TALOS_RT_ARG_NET, TALOS_RT_ARG_MANAGED_STRING,
        TALOS_RT_ARG_MANAGED_CONTAINER, TALOS_RT_ARG_MANAGED_OBJECT,
        TALOS_RT_ARG_ENUM, TALOS_RT_ARG_PROCESS}) {
    talos_rt_arg_v1 item{kind, 0, 0, nullptr, nullptr};
    EXPECT_EQ(talos_rt_v1_eval_display(
                  context, 1, 0, TALOS_RT_RADIX_DECIMAL, &item, 1, nullptr),
              TALOS_RT_INVALID_ARGUMENT);
  }
}

TEST_F(RuntimeTest, ReadsWritesAndPositionsBinaryFiles) {
  TempDirectory temporary;
  uint32_t descriptor = open(temporary.file("roundtrip.bin"), "w+b");
  const std::string bytes("a\0bc", 4);
  uint64_t written = 0;
  ASSERT_EQ(talos_rt_v1_file_write(context, descriptor, nullptr, 0, &written),
            TALOS_RT_OK);
  EXPECT_EQ(written, 0u);
  ASSERT_EQ(talos_rt_v1_file_write(context, descriptor, bytes.data(),
                                     bytes.size(), &written),
            TALOS_RT_OK);
  EXPECT_EQ(written, bytes.size());

  int64_t offset = -1;
  ASSERT_EQ(talos_rt_v1_file_tell(context, descriptor, &offset),
            TALOS_RT_OK);
  EXPECT_EQ(offset, 4);
  ASSERT_EQ(
      talos_rt_v1_file_seek(context, descriptor, -2, TALOS_RT_SEEK_CUR),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_file_tell(context, descriptor, &offset),
            TALOS_RT_OK);
  EXPECT_EQ(offset, 2);
  ASSERT_EQ(talos_rt_v1_file_rewind(context, descriptor), TALOS_RT_OK);

  char result[4]{};
  uint64_t read = 0;
  ASSERT_EQ(talos_rt_v1_file_read(context, descriptor, nullptr, 0, &read),
            TALOS_RT_OK);
  EXPECT_EQ(read, 0u);
  ASSERT_EQ(talos_rt_v1_file_read(context, descriptor, result, sizeof(result),
                                    &read),
            TALOS_RT_OK);
  EXPECT_EQ(read, 4u);
  EXPECT_EQ(std::string(result, sizeof(result)), bytes);
  EXPECT_EQ(talos_rt_v1_file_close(context, descriptor), TALOS_RT_OK);
}

TEST_F(RuntimeTest, HierarchyScanMatchesPrefixWithoutConsumingAField) {
  TempDirectory temporary;
  std::filesystem::path path = temporary.file("hierarchy-scan.bin");
  { std::ofstream(path, std::ios::binary) << "tag=Q"; }
  uint32_t descriptor = open(path, "rb");

  talos_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_lane_create(context, &lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_enter(lane), TALOS_RT_OK);
  talos_rt_string_v1 field = 1;
  uint32_t ok = 0;
  uint32_t scanEOF = 1;
  ASSERT_EQ(talos_rt_v1_file_scan_field(context, lane, descriptor, 1, "tag=",
                                          4, 'M', 0, &field, &ok, &scanEOF),
            TALOS_RT_OK);
  EXPECT_EQ(field, 0u);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(scanEOF, 0u);

  int64_t offset = -1;
  ASSERT_EQ(talos_rt_v1_file_tell(context, descriptor, &offset),
            TALOS_RT_OK);
  EXPECT_EQ(offset, 4);
  uint8_t byte = 0;
  ASSERT_EQ(talos_rt_v1_file_getc(context, descriptor, &byte), TALOS_RT_OK);
  EXPECT_EQ(byte, 'Q');
  EXPECT_EQ(talos_rt_v1_file_close(context, descriptor), TALOS_RT_OK);

  std::filesystem::path emptyPath = temporary.file("empty-hierarchy-scan.bin");
  { std::ofstream(emptyPath, std::ios::binary); }
  descriptor = open(emptyPath, "rb");
  field = 1;
  ok = 0;
  scanEOF = 1;
  ASSERT_EQ(talos_rt_v1_file_scan_field(context, lane, descriptor, 1, nullptr,
                                          0, 'm', 0, &field, &ok, &scanEOF),
            TALOS_RT_OK);
  EXPECT_EQ(field, 0u);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(scanEOF, 0u);
  ASSERT_EQ(talos_rt_v1_file_tell(context, descriptor, &offset),
            TALOS_RT_OK);
  EXPECT_EQ(offset, 0);
  EXPECT_EQ(talos_rt_v1_file_getc(context, descriptor, &byte),
            TALOS_RT_EOF);
  EXPECT_EQ(talos_rt_v1_file_close(context, descriptor), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
}

TEST_F(RuntimeTest, FileScanAcceptsFourStateNumericFieldsExactly) {
  TempDirectory temporary;
  std::filesystem::path path = temporary.file("four-state-scan.txt");
  { std::ofstream(path, std::ios::binary) << "1x?zQ ?R +7"; }
  uint32_t descriptor = open(path, "rb");
  talos_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_lane_create(context, &lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_enter(lane), TALOS_RT_OK);

  auto expectField = [&](uint32_t specifier, std::string_view prefix,
                         std::string_view expected) {
    talos_rt_string_v1 field = 0;
    uint32_t ok = 0;
    uint32_t scanEOF = 1;
    ASSERT_EQ(talos_rt_v1_file_scan_field(
                  context, lane, descriptor, 1, prefix.data(), prefix.size(),
                  specifier, 0, &field, &ok, &scanEOF),
              TALOS_RT_OK);
    EXPECT_EQ(ok, 1u);
    EXPECT_EQ(scanEOF, 0u);
    char scratch[8]{};
    const char *bytes = nullptr;
    uint64_t size = 0;
    ASSERT_EQ(talos_rt_v1_string_view(field, scratch, &bytes, &size),
              TALOS_RT_OK);
    EXPECT_EQ(std::string_view(bytes, size), expected);
  };

  expectField('h', "", "1x?z");
  uint8_t byte = 0;
  ASSERT_EQ(talos_rt_v1_file_getc(context, descriptor, &byte), TALOS_RT_OK);
  EXPECT_EQ(byte, 'Q');
  expectField('d', " ", "?");
  ASSERT_EQ(talos_rt_v1_file_getc(context, descriptor, &byte), TALOS_RT_OK);
  EXPECT_EQ(byte, 'R');
  // A sign belongs to decimal, but Table 21-8 does not make it part of a
  // power-of-two field. The signed decimal field is consumed in full.
  expectField('d', " ", "+7");
  int64_t offset = -1;
  ASSERT_EQ(talos_rt_v1_file_tell(context, descriptor, &offset),
            TALOS_RT_OK);
  EXPECT_EQ(offset, 11);

  EXPECT_EQ(talos_rt_v1_file_close(context, descriptor), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
}

TEST_F(RuntimeTest, DynamicScanPlansCacheAndInterpretSuppressionExactly) {
  talos_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_lane_create(context, &lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_enter(lane), TALOS_RT_OK);
  auto string = [&](std::string_view value) {
    talos_rt_string_v1 result = 0;
    EXPECT_EQ(
        talos_rt_v1_string_create(lane, value.data(), value.size(), &result),
        TALOS_RT_OK);
    return result;
  };
  auto mask = [](std::string_view letters) {
    uint64_t result = 0;
    for (char letter : letters)
      result |= UINT64_C(1) << static_cast<unsigned>(letter - 'a');
    return result;
  };

  talos_rt_string_v1 input = string("tag=ab SKIP 7f!");
  talos_rt_string_v1 format = string("tag=%2s %*4s %2h!");
  EXPECT_EQ(context->dynamicScanState, nullptr);
  uint32_t cursor = 0;
  uint32_t planCursor = 0;
  uint32_t kind = 0;
  uint32_t ok = 0;
  talos_rt_string_v1 field = 0;
  ASSERT_EQ(talos_rt_v1_string_scan_dynamic(
                context, lane, input, cursor, format, planCursor, 1, 0,
                mask("scm"), 0, 0, &field, &cursor, &planCursor, &kind, &ok),
            TALOS_RT_OK);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(cursor, 6u);
  EXPECT_EQ(planCursor, 1u);
  EXPECT_EQ(kind, TALOS_RT_SCAN_DYNAMIC_TEXT);
  ASSERT_NE(context->dynamicScanState, nullptr);
  EXPECT_EQ(context->dynamicScanState->parseCount, 1u);
  EXPECT_EQ(context->dynamicScanState->contentCompareBytes, 0u);

  ASSERT_EQ(talos_rt_v1_string_scan_dynamic(
                context, lane, input, cursor, format, planCursor, 1, 0,
                mask("h"), 0, 0, &field, &cursor, &planCursor, &kind, &ok),
            TALOS_RT_OK);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(cursor, 14u);
  EXPECT_EQ(planCursor, 3u);
  EXPECT_EQ(kind, TALOS_RT_SCAN_DYNAMIC_LOGIC16);
  char scratch[8]{};
  const char *bytes = nullptr;
  uint64_t size = 0;
  ASSERT_EQ(talos_rt_v1_string_view(field, scratch, &bytes, &size),
            TALOS_RT_OK);
  EXPECT_EQ(std::string_view(bytes, size), "7f");
  ASSERT_EQ(talos_rt_v1_string_scan_dynamic(
                context, lane, input, cursor, format, planCursor, 1, 1, 0, 0, 0,
                &field, &cursor, &planCursor, &kind, &ok),
            TALOS_RT_OK);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(cursor, 15u);
  EXPECT_EQ(context->dynamicScanState->parseCount, 1u);

  // Equal bytes in a newly allocated immutable string reuse the same plan.
  talos_rt_string_v1 equalFormat = string("tag=%2s %*4s %2h!");
  std::shared_ptr<const DynamicScanPlan> plan;
  ASSERT_EQ(talos_rt_dynamic_scan_plan(context, equalFormat, plan),
            TALOS_RT_OK);
  EXPECT_EQ(context->dynamicScanState->parseCount, 1u);
  EXPECT_EQ(context->dynamicScanState->contentCompareBytes,
            std::string_view("tag=%2s %*4s %2h!").size());

  // Heap strings are cached by immutable contents, not by handle. Mutating a
  // character produces a new string and therefore a distinct plan even when
  // both strings are longer than the inline representation.
  talos_rt_string_v1 decimalFormat = string("prefix=%d");
  ASSERT_EQ(talos_rt_dynamic_scan_plan(context, decimalFormat, plan),
            TALOS_RT_OK);
  EXPECT_EQ(plan->conversions.front().specifier, 'd');
  talos_rt_string_v1 hexFormat = 0;
  ASSERT_EQ(talos_rt_v1_string_putc(lane, decimalFormat, 8, 'h', &hexFormat),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_dynamic_scan_plan(context, hexFormat, plan),
            TALOS_RT_OK);
  EXPECT_EQ(plan->conversions.front().specifier, 'h');

  // The LRU stays bounded even when a runtime variable cycles through formats.
  for (unsigned index = 0; index != 9; ++index) {
    std::string spelling = "prefix" + std::to_string(index) + "=%d";
    ASSERT_EQ(talos_rt_dynamic_scan_plan(context, string(spelling), plan),
              TALOS_RT_OK);
  }
  EXPECT_EQ(context->dynamicScanState->plans.size(), 8u);
  EXPECT_EQ(context->dynamicScanState->parseCount, 12u);

  // Concurrent callers may race cache hits and evictions, but the cache stays
  // coherent and within its fixed resident bound.
  std::vector<talos_rt_string_v1> concurrentFormats;
  for (unsigned index = 0; index != 12; ++index)
    concurrentFormats.push_back(
        string("concurrent" + std::to_string(index) + "=%d"));
  std::atomic<unsigned> failures{0};
  std::vector<std::thread> threads;
  for (unsigned thread = 0; thread != 4; ++thread) {
    threads.emplace_back([&, thread] {
      for (unsigned iteration = 0; iteration != 100; ++iteration) {
        std::shared_ptr<const DynamicScanPlan> concurrentPlan;
        talos_rt_status status = talos_rt_dynamic_scan_plan(
            context, concurrentFormats[(iteration + thread) % 12],
            concurrentPlan);
        if (status != TALOS_RT_OK || !concurrentPlan ||
            concurrentPlan->conversions.size() != 1 ||
            concurrentPlan->conversions.front().specifier != 'd')
          failures.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }
  for (std::thread &thread : threads)
    thread.join();
  EXPECT_EQ(failures.load(std::memory_order_relaxed), 0u);
  EXPECT_LE(context->dynamicScanState->plans.size(), 8u);
  ASSERT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
}

TEST_F(RuntimeTest, DynamicScanIdentityHitsDoNotWalkFormatBytes) {
  talos_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_lane_create(context, &lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_enter(lane), TALOS_RT_OK);
  std::string spelling;
  for (unsigned index = 0; index != 512; ++index)
    spelling += "%d ";
  talos_rt_string_v1 format = 0;
  ASSERT_EQ(talos_rt_v1_string_create(lane, spelling.data(), spelling.size(),
                                        &format),
            TALOS_RT_OK);
  std::shared_ptr<const DynamicScanPlan> plan;
  ASSERT_EQ(talos_rt_dynamic_scan_plan(context, format, plan), TALOS_RT_OK);
  ASSERT_TRUE(plan);
  ASSERT_EQ(plan->conversions.size(), 512u);
  ASSERT_NE(context->dynamicScanState, nullptr);
  EXPECT_EQ(context->dynamicScanState->parseCount, 1u);
  EXPECT_EQ(context->dynamicScanState->contentCompareBytes, 0u);
  for (size_t ordinal = 0; ordinal != plan->conversions.size(); ++ordinal) {
    std::shared_ptr<const DynamicScanPlan> cached;
    ASSERT_EQ(talos_rt_dynamic_scan_plan(context, format, cached),
              TALOS_RT_OK);
    ASSERT_EQ(cached.get(), plan.get());
  }
  EXPECT_EQ(context->dynamicScanState->parseCount, 1u);
  EXPECT_EQ(context->dynamicScanState->contentCompareBytes, 0u);
  ASSERT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
}

TEST_F(RuntimeTest, DynamicRawScanUsesDestinationSpecificTransferSizes) {
  talos_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_lane_create(context, &lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_enter(lane), TALOS_RT_OK);
  auto string = [&](std::string_view value) {
    talos_rt_string_v1 result = 0;
    EXPECT_EQ(
        talos_rt_v1_string_create(lane, value.data(), value.size(), &result),
        TALOS_RT_OK);
    return result;
  };
  auto view = [&](talos_rt_string_v1 value) {
    char scratch[8]{};
    const char *bytes = nullptr;
    uint64_t size = 0;
    EXPECT_EQ(talos_rt_v1_string_view(value, scratch, &bytes, &size),
              TALOS_RT_OK);
    return std::string(bytes, static_cast<size_t>(size));
  };

  std::string raw("ABCD", 4);
  raw.append("\x01\x00\x00\x80\x00\x00\x00\x80", 8);
  talos_rt_string_v1 input = string(raw);
  talos_rt_string_v1 format = string("%u%z");
  uint64_t allowed =
      (UINT64_C(1) << ('u' - 'a')) | (UINT64_C(1) << ('z' - 'a'));
  talos_rt_string_v1 field = 0;
  uint32_t cursor = 0, planCursor = 0, kind = 0, ok = 0;
  ASSERT_EQ(talos_rt_v1_string_scan_dynamic(
                context, lane, input, cursor, format, planCursor, 1, 0, allowed,
                4, 8, &field, &cursor, &planCursor, &kind, &ok),
            TALOS_RT_OK);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(cursor, 4u);
  EXPECT_EQ(planCursor, 1u);
  EXPECT_EQ(kind, TALOS_RT_SCAN_DYNAMIC_RAW2);
  EXPECT_EQ(view(field), raw.substr(0, 4));

  ASSERT_EQ(talos_rt_v1_string_scan_dynamic(
                context, lane, input, cursor, format, planCursor, 1, 0, allowed,
                4, 8, &field, &cursor, &planCursor, &kind, &ok),
            TALOS_RT_OK);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(cursor, 12u);
  EXPECT_EQ(planCursor, 2u);
  EXPECT_EQ(kind, TALOS_RT_SCAN_DYNAMIC_RAW4);
  EXPECT_EQ(view(field), raw.substr(4));

  format = string("%3u");
  cursor = 0;
  planCursor = 0;
  ASSERT_EQ(talos_rt_v1_string_scan_dynamic(
                context, lane, input, cursor, format, planCursor, 1, 0, allowed,
                4, 8, &field, &cursor, &planCursor, &kind, &ok),
            TALOS_RT_OK);
  EXPECT_EQ(ok, 0u);
  EXPECT_EQ(cursor, 0u);
  EXPECT_EQ(planCursor, 0u);

  TempDirectory temporary;
  std::filesystem::path path = temporary.file("dynamic-raw.bin");
  {
    std::ofstream output(path, std::ios::binary);
    output.write(raw.data() + 4, 8);
  }
  uint32_t descriptor = open(path, "rb");
  talos_rt_string_v1 incompatible = string("%d %u");
  uint32_t validationCursor = 0;
  ASSERT_EQ(talos_rt_v1_scan_dynamic_validate(
                context, incompatible, validationCursor, 1, 0,
                UINT64_C(1) << ('d' - 'a'), &validationCursor),
            TALOS_RT_OK);
  EXPECT_EQ(validationCursor, 1u);
  EXPECT_EQ(talos_rt_v1_scan_dynamic_validate(
                context, incompatible, validationCursor, 1, 0,
                UINT64_C(1) << ('s' - 'a'), &validationCursor),
            TALOS_RT_INVALID_ARGUMENT);
  int64_t position = -1;
  EXPECT_EQ(talos_rt_v1_file_tell(context, descriptor, &position),
            TALOS_RT_OK);
  EXPECT_EQ(position, 0);

  format = string("%z");
  planCursor = 0;
  uint32_t eof = 0;
  ASSERT_EQ(talos_rt_v1_file_scan_dynamic(
                context, lane, descriptor, format, planCursor, 1, 0, allowed, 4,
                8, &field, &planCursor, &kind, &ok, &eof),
            TALOS_RT_OK);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(eof, 0u);
  EXPECT_EQ(planCursor, 1u);
  EXPECT_EQ(kind, TALOS_RT_SCAN_DYNAMIC_RAW4);
  EXPECT_EQ(view(field), raw.substr(4));
  EXPECT_EQ(talos_rt_v1_file_tell(context, descriptor, &position),
            TALOS_RT_OK);
  EXPECT_EQ(position, 8);
  EXPECT_EQ(talos_rt_v1_file_close(context, descriptor), TALOS_RT_OK);

  ASSERT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
}

TEST_F(RuntimeTest, DynamicScanRejectsCrossContextLanesAndHeapStrings) {
  talos_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_lane_create(context, &lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_enter(lane), TALOS_RT_OK);
  auto create = [](talos_rt_gc_lane_v1 *owner, std::string_view value) {
    talos_rt_string_v1 result = 0;
    EXPECT_EQ(
        talos_rt_v1_string_create(owner, value.data(), value.size(), &result),
        TALOS_RT_OK);
    return result;
  };
  talos_rt_string_v1 input = create(lane, "input-source");
  talos_rt_string_v1 format = create(lane, "value=%d");
  ASSERT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);

  talos_rt_context *otherContext = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&otherContext), TALOS_RT_OK);
  talos_rt_gc_lane_v1 *otherLane = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_lane_create(otherContext, &otherLane),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_enter(otherLane), TALOS_RT_OK);
  talos_rt_string_v1 otherInput = create(otherLane, "other-input");
  talos_rt_string_v1 otherFormat = create(otherLane, "other=%d");
  ASSERT_EQ(talos_rt_v1_gc_lane_leave(otherLane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_enter(lane), TALOS_RT_OK);

  talos_rt_string_v1 field = 0;
  uint32_t cursor = 0, planCursor = 0, kind = 0, ok = 0, eof = 0;
  uint64_t decimal = UINT64_C(1) << ('d' - 'a');
  EXPECT_EQ(talos_rt_v1_string_scan_dynamic(
                context, otherLane, input, 0, format, 0, 1, 0, decimal, 0, 0,
                &field, &cursor, &planCursor, &kind, &ok),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_string_scan_dynamic(
                context, lane, otherInput, 0, format, 0, 1, 0, decimal, 0, 0,
                &field, &cursor, &planCursor, &kind, &ok),
            TALOS_RT_INVALID_HANDLE);
  EXPECT_EQ(talos_rt_v1_string_scan_dynamic(
                context, lane, input, 0, otherFormat, 0, 1, 0, decimal, 0, 0,
                &field, &cursor, &planCursor, &kind, &ok),
            TALOS_RT_INVALID_HANDLE);
  EXPECT_EQ(talos_rt_v1_file_scan_dynamic(context, otherLane, 0, format, 0, 1,
                                            0, decimal, 0, 0, &field,
                                            &planCursor, &kind, &ok, &eof),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_file_scan_dynamic(context, lane, 0, otherFormat, 0, 1,
                                            0, decimal, 0, 0, &field,
                                            &planCursor, &kind, &ok, &eof),
            TALOS_RT_INVALID_HANDLE);
  EXPECT_EQ(talos_rt_v1_scan_dynamic_validate(context, otherFormat, 0, 0, 0,
                                                decimal, &planCursor),
            TALOS_RT_INVALID_HANDLE);

  ASSERT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_destroy(otherLane), TALOS_RT_OK);
  talos_rt_v1_context_destroy(otherContext);
}

TEST_F(RuntimeTest, DynamicFileScanPreservesPositionAndEOF) {
  TempDirectory temporary;
  std::filesystem::path path = temporary.file("dynamic-scan.txt");
  { std::ofstream(path, std::ios::binary) << "A=12 SKIP 7f!"; }
  uint32_t descriptor = open(path, "rb");
  talos_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_lane_create(context, &lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_enter(lane), TALOS_RT_OK);
  auto string = [&](std::string_view value) {
    talos_rt_string_v1 result = 0;
    EXPECT_EQ(
        talos_rt_v1_string_create(lane, value.data(), value.size(), &result),
        TALOS_RT_OK);
    return result;
  };
  talos_rt_string_v1 format = string("A=%2d %*4s %2h!");
  talos_rt_string_v1 field = 0;
  uint32_t planCursor = 0;
  uint32_t kind = 0;
  uint32_t ok = 0;
  uint32_t scanEOF = 0;
  uint64_t numeric =
      (UINT64_C(1) << ('d' - 'a')) | (UINT64_C(1) << ('h' - 'a'));
  ASSERT_EQ(talos_rt_v1_file_scan_dynamic(
                context, lane, descriptor, format, planCursor, 1, 0, numeric, 0,
                0, &field, &planCursor, &kind, &ok, &scanEOF),
            TALOS_RT_OK);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(scanEOF, 0u);
  EXPECT_EQ(planCursor, 1u);
  EXPECT_EQ(kind, TALOS_RT_SCAN_DYNAMIC_LOGIC10);
  int64_t position = -1;
  ASSERT_EQ(talos_rt_v1_file_tell(context, descriptor, &position),
            TALOS_RT_OK);
  EXPECT_EQ(position, 4);

  ASSERT_EQ(talos_rt_v1_file_scan_dynamic(
                context, lane, descriptor, format, planCursor, 1, 0, numeric, 0,
                0, &field, &planCursor, &kind, &ok, &scanEOF),
            TALOS_RT_OK);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(planCursor, 3u);
  EXPECT_EQ(kind, TALOS_RT_SCAN_DYNAMIC_LOGIC16);
  ASSERT_EQ(talos_rt_v1_file_tell(context, descriptor, &position),
            TALOS_RT_OK);
  EXPECT_EQ(position, 12);
  ASSERT_EQ(talos_rt_v1_file_scan_dynamic(context, lane, descriptor, format,
                                            planCursor, 1, 1, 0, 0, 0, &field,
                                            &planCursor, &kind, &ok, &scanEOF),
            TALOS_RT_OK);
  EXPECT_EQ(ok, 1u);
  ASSERT_EQ(talos_rt_v1_file_tell(context, descriptor, &position),
            TALOS_RT_OK);
  EXPECT_EQ(position, 13);
  EXPECT_EQ(talos_rt_v1_file_close(context, descriptor), TALOS_RT_OK);

  std::filesystem::path empty = temporary.file("dynamic-empty.txt");
  { std::ofstream(empty, std::ios::binary); }
  descriptor = open(empty, "rb");
  format = string("%d");
  planCursor = 0;
  ASSERT_EQ(talos_rt_v1_file_scan_dynamic(
                context, lane, descriptor, format, planCursor, 1, 0, numeric, 0,
                0, &field, &planCursor, &kind, &ok, &scanEOF),
            TALOS_RT_OK);
  EXPECT_EQ(ok, 0u);
  EXPECT_EQ(scanEOF, 1u);
  EXPECT_EQ(talos_rt_v1_file_close(context, descriptor), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
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
  ASSERT_EQ(talos_rt_v1_file_readmem_token(
                context, descriptor, 16, 13, value.data(), value.size(),
                unknown.data(), unknown.size(), &kind, &address),
            TALOS_RT_OK);
  EXPECT_EQ(kind, TALOS_RT_READMEM_DATA);
  EXPECT_EQ(value, (std::array<uint8_t, 2>{0xaf, 0x00}));
  EXPECT_EQ(unknown, (std::array<uint8_t, 2>{0x0f, 0x00}));
  ASSERT_EQ(talos_rt_v1_file_readmem_token(
                context, descriptor, 16, 13, value.data(), value.size(),
                unknown.data(), unknown.size(), &kind, &address),
            TALOS_RT_OK);
  EXPECT_EQ(kind, TALOS_RT_READMEM_ADDRESS);
  EXPECT_EQ(address, 0x1fu);
  ASSERT_EQ(talos_rt_v1_file_readmem_token(
                context, descriptor, 16, 13, value.data(), value.size(),
                unknown.data(), unknown.size(), &kind, &address),
            TALOS_RT_OK);
  EXPECT_EQ(kind, TALOS_RT_READMEM_DATA);
  EXPECT_EQ(value, (std::array<uint8_t, 2>{0x01, 0x10}));
  EXPECT_EQ(unknown, (std::array<uint8_t, 2>{0xf0, 0x00}));
  ASSERT_EQ(talos_rt_v1_file_readmem_token(
                context, descriptor, 16, 13, value.data(), value.size(),
                unknown.data(), unknown.size(), &kind, &address),
            TALOS_RT_OK);
  EXPECT_EQ(kind, TALOS_RT_READMEM_EOF);
  EXPECT_EQ(talos_rt_v1_file_close(context, descriptor), TALOS_RT_OK);
}

TEST_F(RuntimeTest, ReadMemTokenizerTruncatesLongWordsAndMasksPartialBytes) {
  // LRM 21.4: underscores do not consume bits, X/Z retain their planes, and
  // oversized words retain the destination's least significant bits.
  TempDirectory temporary;
  std::filesystem::path path = temporary.file("long-memory.hex");
  {
    std::ofstream output(path);
    std::string word = std::string(80, 'f') + "x_Z";
    output << word << '\n' << word << '\n' << word << "\n10xz_1";
  }
  uint32_t descriptor = open(path, "r");
  uint32_t kind = 0;
  uint64_t address = 0;
  for (uint64_t width : {5u, 64u, 65u}) {
    std::vector<uint8_t> value((width + 7) / 8, 0xff);
    std::vector<uint8_t> unknown(value.size(), 0xff);
    ASSERT_EQ(talos_rt_v1_file_readmem_token(
                  context, descriptor, 16, width, value.data(), value.size(),
                  unknown.data(), unknown.size(), &kind, &address),
              TALOS_RT_OK);
    EXPECT_EQ(kind, TALOS_RT_READMEM_DATA);
    EXPECT_EQ(value[0], 0x0f);
    EXPECT_EQ(unknown[0], width == 5 ? 0x1f : 0xff);
    for (size_t byte = 1; byte != value.size(); ++byte) {
      EXPECT_EQ(value[byte], byte == 8 ? 0x01 : 0xff);
      EXPECT_EQ(unknown[byte], 0x00);
    }
  }
  uint8_t value = 0xff, unknown = 0xff;
  ASSERT_EQ(talos_rt_v1_file_readmem_token(context, descriptor, 2, 3, &value,
                                             1, &unknown, 1, &kind, &address),
            TALOS_RT_OK);
  EXPECT_EQ(kind, TALOS_RT_READMEM_DATA);
  EXPECT_EQ(value, 0x03);
  EXPECT_EQ(unknown, 0x06);
  EXPECT_EQ(talos_rt_v1_file_close(context, descriptor), TALOS_RT_OK);
}

TEST_F(RuntimeTest, ReadMemTokenizerRejectsMalformedInput) {
  TempDirectory temporary;
  std::filesystem::path path = temporary.file("bad.hex");
  { std::ofstream(path) << "@"; }
  uint32_t descriptor = open(path, "r");
  uint8_t value = 0, unknown = 0;
  uint32_t kind = 0;
  uint64_t address = 0;
  EXPECT_EQ(talos_rt_v1_file_readmem_token(context, descriptor, 16, 8, &value,
                                             1, &unknown, 1, &kind, &address),
            TALOS_RT_FORMAT_ERROR);
  EXPECT_EQ(talos_rt_v1_file_close(context, descriptor), TALOS_RT_OK);
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
  ASSERT_EQ(talos_rt_v1_file_getc(context, descriptor, &byte), TALOS_RT_OK);
  EXPECT_EQ(byte, 'a');
  ASSERT_EQ(talos_rt_v1_file_ungetc(context, descriptor, byte),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_file_getc(context, descriptor, &byte), TALOS_RT_OK);
  EXPECT_EQ(byte, 'a');

  RuntimeBuffer firstLine;
  ASSERT_EQ(
      talos_rt_v1_file_getline(context, descriptor, 64, firstLine.out()),
      TALOS_RT_OK);
  EXPECT_EQ(firstLine.str(), std::string("\0b\n", 3));
  RuntimeBuffer secondLine;
  ASSERT_EQ(
      talos_rt_v1_file_getline(context, descriptor, 64, secondLine.out()),
      TALOS_RT_OK);
  EXPECT_EQ(secondLine.str(), "last");
  RuntimeBuffer eofLine;
  EXPECT_EQ(talos_rt_v1_file_getline(context, descriptor, 64, eofLine.out()),
            TALOS_RT_EOF);
  uint32_t isEOF = 0;
  ASSERT_EQ(talos_rt_v1_file_eof(context, descriptor, &isEOF), TALOS_RT_OK);
  EXPECT_EQ(isEOF, 1u);
  EXPECT_EQ(talos_rt_v1_file_close(context, descriptor), TALOS_RT_OK);
}

TEST_F(RuntimeTest, ReadsWithoutReadAccessReportEndOfFileInsteadOfIOError) {
  TempDirectory temporary;
  std::filesystem::path path = temporary.file("write-only.txt");
  uint32_t descriptor = open(path, "w");

  // The host refuses these reads with EBADF. Reporting that as an I/O error
  // would fail the whole simulation instead of the individual system call.
  uint8_t byte = 0;
  EXPECT_EQ(talos_rt_v1_file_getc(context, descriptor, &byte),
            TALOS_RT_EOF);
  char bytes[4] = {};
  uint64_t read = 1;
  ASSERT_EQ(
      talos_rt_v1_file_read(context, descriptor, bytes, sizeof(bytes), &read),
      TALOS_RT_OK);
  EXPECT_EQ(read, 0u);
  RuntimeBuffer line;
  EXPECT_EQ(talos_rt_v1_file_getline(context, descriptor, 64, line.out()),
            TALOS_RT_EOF);
  uint32_t isEOF = 0;
  ASSERT_EQ(talos_rt_v1_file_eof(context, descriptor, &isEOF), TALOS_RT_OK);
  EXPECT_EQ(isEOF, 1u);

  talos_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_lane_create(context, &lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_enter(lane), TALOS_RT_OK);
  talos_rt_string_v1 field = 1;
  uint32_t ok = 1;
  uint32_t scanEOF = 0;
  EXPECT_EQ(talos_rt_v1_file_scan_field(context, lane, descriptor, 1, nullptr,
                                          0, 'd', 0, &field, &ok, &scanEOF),
            TALOS_RT_OK);
  EXPECT_EQ(field, 0u);
  EXPECT_EQ(ok, 0u);
  EXPECT_EQ(scanEOF, 1u);
  EXPECT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);

  // The descriptor keeps the access it was opened with.
  uint64_t written = 0;
  ASSERT_EQ(talos_rt_v1_file_write(context, descriptor, "kept", 4, &written),
            TALOS_RT_OK);
  EXPECT_EQ(written, 4u);
  EXPECT_EQ(talos_rt_v1_file_close(context, descriptor), TALOS_RT_OK);

  EXPECT_EQ(readHostFile(path), "kept");
}

// IEEE 1800-2017 21.3.4.3: $fscanf returns EOF (-1) when the input ends
// before the first conversion. A descriptor that was never opened -- $fopen
// returning 0 on a missing file is the ordinary way to get one -- has no
// input at all, so it reads as end of file. Returning an error status instead
// would end the simulation rather than the one system call, which is what
// every other read on an unusable descriptor already avoids.
TEST_F(RuntimeTest, ScansOnAnInvalidDescriptorReportEndOfFile) {
  talos_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_lane_create(context, &lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_enter(lane), TALOS_RT_OK);

  talos_rt_string_v1 field = 1;
  uint32_t ok = 1;
  uint32_t scanEOF = 0;
  EXPECT_EQ(talos_rt_v1_file_scan_field(context, lane, 0, 1, nullptr, 0, 'd',
                                          0, &field, &ok, &scanEOF),
            TALOS_RT_OK);
  EXPECT_EQ(field, 0u);
  EXPECT_EQ(ok, 0u);
  EXPECT_EQ(scanEOF, 1u);

  // A disabled conversion still leaves the stream alone and reports nothing:
  // lowering stops issuing them once an earlier one failed.
  scanEOF = 1;
  EXPECT_EQ(talos_rt_v1_file_scan_field(context, lane, 0, 0, nullptr, 0, 'd',
                                          0, &field, &ok, &scanEOF),
            TALOS_RT_OK);
  EXPECT_EQ(scanEOF, 0u);

  EXPECT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
}

TEST_F(RuntimeTest, FormattedScanReadErrorsAreInputFailures) {
#if defined(_WIN32)
  GTEST_SKIP() << "The Windows CRT rejects directory streams at fopen; this "
                  "test needs a directory stream that fails only when read";
#endif
  TempDirectory temporary;
  std::filesystem::path directory = temporary.file("scan-error");
  ASSERT_TRUE(std::filesystem::create_directory(directory));
  talos_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_lane_create(context, &lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_enter(lane), TALOS_RT_OK);

  auto expectErrorState = [&](uint32_t descriptor) {
    int64_t position = -1;
    EXPECT_EQ(talos_rt_v1_file_tell(context, descriptor, &position),
              TALOS_RT_OK);
    EXPECT_EQ(position, 0);
    uint32_t eof = 1;
    EXPECT_EQ(talos_rt_v1_file_eof(context, descriptor, &eof), TALOS_RT_OK);
    EXPECT_EQ(eof, 0u);
    int32_t code = 0;
    RuntimeBuffer message;
    EXPECT_EQ(
        talos_rt_v1_file_error(context, descriptor, &code, message.out()),
        TALOS_RT_OK);
    EXPECT_NE(code, 0);
    EXPECT_FALSE(message.str().empty());
    EXPECT_EQ(talos_rt_v1_file_close(context, descriptor), TALOS_RT_OK);
  };

  talos_rt_string_v1 field = 1;
  uint32_t ok = 1;
  uint32_t inputFailure = 0;
  uint32_t descriptor = open(directory, "r");
  ASSERT_EQ(talos_rt_v1_file_scan_field(context, lane, descriptor, 1, nullptr,
                                          0, 'd', 0, &field, &ok,
                                          &inputFailure),
            TALOS_RT_OK);
  EXPECT_EQ(field, 0u);
  EXPECT_EQ(ok, 0u);
  EXPECT_EQ(inputFailure, 1u);
  expectErrorState(descriptor);

  descriptor = open(directory, "r");
  uint32_t value = UINT32_MAX;
  uint32_t unknown = UINT32_MAX;
  ASSERT_EQ(talos_rt_v1_file_scan_raw(
                context, descriptor, 1, nullptr, 0, 4, 32, 0, 0, &value,
                sizeof(value), &unknown, sizeof(unknown), &ok, &inputFailure),
            TALOS_RT_OK);
  EXPECT_EQ(value, 0u);
  EXPECT_EQ(unknown, 0u);
  EXPECT_EQ(ok, 0u);
  EXPECT_EQ(inputFailure, 1u);
  expectErrorState(descriptor);

  talos_rt_string_v1 format = 0;
  ASSERT_EQ(talos_rt_v1_string_create(lane, "%d", 2, &format), TALOS_RT_OK);
  descriptor = open(directory, "r");
  uint32_t planCursor = 0;
  uint32_t kind = 0;
  ASSERT_EQ(talos_rt_v1_file_scan_dynamic(context, lane, descriptor, format,
                                            0, 1, 0, UINT64_C(1) << ('d' - 'a'),
                                            0, 0, &field, &planCursor, &kind,
                                            &ok, &inputFailure),
            TALOS_RT_OK);
  EXPECT_EQ(field, 0u);
  EXPECT_EQ(planCursor, 0u);
  EXPECT_EQ(kind, 0u);
  EXPECT_EQ(ok, 0u);
  EXPECT_EQ(inputFailure, 1u);
  expectErrorState(descriptor);

  ASSERT_EQ(talos_rt_v1_string_create(lane, "tail", 4, &format),
            TALOS_RT_OK);
  descriptor = open(directory, "r");
  ASSERT_EQ(talos_rt_v1_file_scan_dynamic(
                context, lane, descriptor, format, 0, 1, 1, 0, 0, 0, &field,
                &planCursor, &kind, &ok, &inputFailure),
            TALOS_RT_OK);
  EXPECT_EQ(field, 0u);
  EXPECT_EQ(planCursor, 0u);
  EXPECT_EQ(kind, 0u);
  EXPECT_EQ(ok, 0u);
  EXPECT_EQ(inputFailure, 1u);
  expectErrorState(descriptor);

  EXPECT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
}

TEST_F(RuntimeTest, HoldsOnePushedBackByteWithoutReadAccess) {
  TempDirectory temporary;
  std::filesystem::path path = temporary.file("pushback.txt");
  uint32_t descriptor = open(path, "w");

  // The runtime holds the byte itself: glibc accepts ungetc() on a write-only
  // stream and then crashes on the next write through it.
  ASSERT_EQ(talos_rt_v1_file_ungetc(context, descriptor, 'z'), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_file_ungetc(context, descriptor, 'y'),
            TALOS_RT_EOF);
  uint32_t isEOF = 1;
  ASSERT_EQ(talos_rt_v1_file_eof(context, descriptor, &isEOF), TALOS_RT_OK);
  EXPECT_EQ(isEOF, 0u);

  uint8_t byte = 0;
  ASSERT_EQ(talos_rt_v1_file_getc(context, descriptor, &byte), TALOS_RT_OK);
  EXPECT_EQ(byte, 'z');
  EXPECT_EQ(talos_rt_v1_file_getc(context, descriptor, &byte),
            TALOS_RT_EOF);
  ASSERT_EQ(talos_rt_v1_file_eof(context, descriptor, &isEOF), TALOS_RT_OK);
  EXPECT_EQ(isEOF, 1u);

  uint64_t written = 0;
  ASSERT_EQ(talos_rt_v1_file_write(context, descriptor, "after", 5, &written),
            TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_file_close(context, descriptor), TALOS_RT_OK);

  EXPECT_EQ(readHostFile(path), "after");
}

TEST_F(RuntimeTest, FormattedScansConsumeSyntheticPushback) {
  TempDirectory temporary;
  uint32_t descriptor = open(temporary.file("pushback-scan.bin"), "w");
  talos_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_lane_create(context, &lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_enter(lane), TALOS_RT_OK);

  auto expectField = [&](talos_rt_string_v1 field,
                         std::string_view expected) {
    char scratch[8]{};
    const char *bytes = nullptr;
    uint64_t size = 0;
    ASSERT_EQ(talos_rt_v1_string_view(field, scratch, &bytes, &size),
              TALOS_RT_OK);
    EXPECT_EQ(std::string_view(bytes, size), expected);
  };
  auto expectPosition = [&] {
    int64_t offset = -1;
    ASSERT_EQ(talos_rt_v1_file_tell(context, descriptor, &offset),
              TALOS_RT_OK);
    EXPECT_EQ(offset, 0);
  };
  auto expectEOF = [&](uint32_t expected) {
    uint32_t isEOF = 2;
    ASSERT_EQ(talos_rt_v1_file_eof(context, descriptor, &isEOF),
              TALOS_RT_OK);
    EXPECT_EQ(isEOF, expected);
  };

  talos_rt_string_v1 field = 0;
  uint32_t ok = 0;
  uint32_t scanEOF = 0;
  ASSERT_EQ(talos_rt_v1_file_ungetc(context, descriptor, 'Q'), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_file_scan_field(context, lane, descriptor, 1, nullptr,
                                          0, 'c', 0, &field, &ok, &scanEOF),
            TALOS_RT_OK);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(scanEOF, 0u);
  expectField(field, "Q");
  expectEOF(1);
  expectPosition();

  ASSERT_EQ(talos_rt_v1_file_ungetc(context, descriptor, 'S'), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_file_scan_field(context, lane, descriptor, 1, nullptr,
                                          0, 's', 0, &field, &ok, &scanEOF),
            TALOS_RT_OK);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(scanEOF, 0u);
  expectField(field, "S");
  expectEOF(1);
  expectPosition();

  // A failed numeric conversion must restore the synthetic byte just as
  // ungetc() restores a byte on an ordinary readable stream.
  ASSERT_EQ(talos_rt_v1_file_ungetc(context, descriptor, 'Q'), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_file_scan_field(context, lane, descriptor, 1, nullptr,
                                          0, 'd', 0, &field, &ok, &scanEOF),
            TALOS_RT_OK);
  EXPECT_EQ(ok, 0u);
  EXPECT_EQ(scanEOF, 0u);
  expectEOF(0);
  uint8_t byte = 0;
  ASSERT_EQ(talos_rt_v1_file_getc(context, descriptor, &byte), TALOS_RT_OK);
  EXPECT_EQ(byte, 'Q');
  expectEOF(1);
  expectPosition();

  // Prefix mismatch also puts the byte back; a matching prefix followed by
  // zero-byte %m consumes it and can still succeed at synthetic EOF.
  ASSERT_EQ(talos_rt_v1_file_ungetc(context, descriptor, 'Q'), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_file_scan_field(context, lane, descriptor, 1, "X", 1,
                                          'm', 0, &field, &ok, &scanEOF),
            TALOS_RT_OK);
  EXPECT_EQ(ok, 0u);
  expectEOF(0);
  ASSERT_EQ(talos_rt_v1_file_scan_field(context, lane, descriptor, 1, "Q", 1,
                                          'm', 0, &field, &ok, &scanEOF),
            TALOS_RT_OK);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(scanEOF, 0u);
  expectEOF(1);
  expectPosition();

  // A suppressed raw field supplies its explicit byte count. A typed raw
  // conversion needs a full native word; consuming the one available byte
  // and then reaching EOF is a partial conversion, not a mismatch.
  ASSERT_EQ(talos_rt_v1_file_ungetc(context, descriptor, 'Q'), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_file_scan_raw(context, descriptor, 1, nullptr, 0, 1,
                                        0, 0, 1, nullptr, 0, nullptr, 0, &ok,
                                        &scanEOF),
            TALOS_RT_OK);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(scanEOF, 0u);
  expectEOF(1);
  expectPosition();

  uint32_t value = UINT32_MAX;
  uint32_t unknown = UINT32_MAX;
  ASSERT_EQ(talos_rt_v1_file_ungetc(context, descriptor, 'Q'), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_file_scan_raw(
                context, descriptor, 1, nullptr, 0, 4, 32, 0, 0, &value,
                sizeof(value), &unknown, sizeof(unknown), &ok, &scanEOF),
            TALOS_RT_OK);
  EXPECT_EQ(ok, 0u);
  EXPECT_EQ(scanEOF, 1u);
  EXPECT_EQ(value, 0u);
  EXPECT_EQ(unknown, 0u);
  expectEOF(1);
  expectPosition();

  EXPECT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_file_close(context, descriptor), TALOS_RT_OK);
}

TEST_F(RuntimeTest, BoundsPackedLineReadsWithoutDiscardingRemainingBytes) {
  TempDirectory temporary;
  uint32_t descriptor = open(temporary.file("bounded-line.txt"), "w+");
  uint64_t written = 0;
  ASSERT_EQ(
      talos_rt_v1_file_write(context, descriptor, "abcdef\n", 7, &written),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_file_rewind(context, descriptor), TALOS_RT_OK);

  RuntimeBuffer prefix;
  ASSERT_EQ(talos_rt_v1_file_getline(context, descriptor, 3, prefix.out()),
            TALOS_RT_OK);
  EXPECT_EQ(prefix.str(), "abc");

  RuntimeBuffer remainder;
  ASSERT_EQ(
      talos_rt_v1_file_getline(context, descriptor, 64, remainder.out()),
      TALOS_RT_OK);
  EXPECT_EQ(remainder.str(), "def\n");
  EXPECT_EQ(talos_rt_v1_file_close(context, descriptor), TALOS_RT_OK);
}

TEST_F(RuntimeTest, ReservesPredefinedFileDescriptorValues) {
  TempDirectory temporary;
  uint32_t descriptor = open(temporary.file("descriptor.txt"), "w");
  EXPECT_EQ(descriptor, 0x80000003u);
  EXPECT_EQ(talos_rt_v1_file_close(context, descriptor), TALOS_RT_OK);

  uint32_t reused = open(temporary.file("descriptor-reused.txt"), "w");
  EXPECT_EQ(reused, 0x80000003u);
  EXPECT_EQ(talos_rt_v1_file_close(context, reused), TALOS_RT_OK);
}

TEST_F(RuntimeTest, FlushesAllStreamsAndReportsByteEOF) {
  TempDirectory temporary;
  std::string mcdPath = temporary.file("flush-mcd.txt").string();
  uint32_t mcd = 0;
  ASSERT_EQ(talos_rt_v1_file_open_mcd(context, mcdPath.data(), mcdPath.size(),
                                        &mcd),
            TALOS_RT_OK);
  uint32_t descriptor = open(temporary.file("flush-file.txt"), "w");
  uint64_t written = 0;
  ASSERT_EQ(talos_rt_v1_file_write(context, mcd, "mcd", 3, &written),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_file_write(context, descriptor, "file", 4, &written),
            TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_file_flush(context, 0), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_file_flush(context, 1), TALOS_RT_OK);
  EXPECT_EQ(readHostFile(mcdPath), "mcd");
  EXPECT_EQ(readHostFile(temporary.file("flush-file.txt")), "file");
  EXPECT_EQ(talos_rt_v1_file_close(context, mcd), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_file_close(context, descriptor), TALOS_RT_OK);

  uint32_t empty = open(temporary.file("empty.txt"), "w+b");
  uint8_t byte = 0xff;
  EXPECT_EQ(talos_rt_v1_file_getc(context, empty, &byte), TALOS_RT_EOF);
  EXPECT_EQ(talos_rt_v1_file_close(context, empty), TALOS_RT_OK);
}

TEST_F(RuntimeTest, RejectsInvalidFileArgumentsAndOpenFailures) {
  TempDirectory temporary;
  std::string missing = temporary.file("missing/child.txt").string();
  uint32_t descriptor = 123;
  EXPECT_EQ(talos_rt_v1_file_open(context, missing.data(), missing.size(),
                                    "r", 1, &descriptor),
            TALOS_RT_IO_ERROR);
  EXPECT_EQ(descriptor, 0u);
  descriptor = 123;
  EXPECT_EQ(talos_rt_v1_file_open_mcd(context, missing.data(), missing.size(),
                                        &descriptor),
            TALOS_RT_IO_ERROR);
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
      talos_rt_v1_file_write(context, invalidDescriptor, &byte, 1, &count),
      TALOS_RT_INVALID_HANDLE);
  EXPECT_EQ(
      talos_rt_v1_file_read(context, invalidDescriptor, &byte, 1, &count),
      TALOS_RT_INVALID_HANDLE);
  EXPECT_EQ(talos_rt_v1_file_getc(context, invalidDescriptor, &unsignedByte),
            TALOS_RT_INVALID_HANDLE);
  EXPECT_EQ(talos_rt_v1_file_ungetc(context, invalidDescriptor, 'x'),
            TALOS_RT_INVALID_HANDLE);
  EXPECT_EQ(
      talos_rt_v1_file_getline(context, invalidDescriptor, 64, line.out()),
      TALOS_RT_INVALID_HANDLE);
  EXPECT_EQ(talos_rt_v1_file_eof(context, invalidDescriptor, &eof),
            TALOS_RT_INVALID_HANDLE);
  EXPECT_EQ(talos_rt_v1_file_error(context, invalidDescriptor, &errorCode,
                                     error.out()),
            TALOS_RT_INVALID_HANDLE);
  EXPECT_EQ(talos_rt_v1_file_seek(context, invalidDescriptor, 0,
                                    TALOS_RT_SEEK_SET),
            TALOS_RT_INVALID_HANDLE);
  EXPECT_EQ(talos_rt_v1_file_tell(context, invalidDescriptor, &offset),
            TALOS_RT_INVALID_HANDLE);
  EXPECT_EQ(talos_rt_v1_file_flush(context, invalidDescriptor),
            TALOS_RT_INVALID_HANDLE);
  EXPECT_EQ(talos_rt_v1_file_close(context, invalidDescriptor),
            TALOS_RT_INVALID_HANDLE);

  uint32_t valid = open(temporary.file("seek.txt"), "w+");
  EXPECT_EQ(talos_rt_v1_file_seek(context, valid, 0, 99),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_file_write(context, valid, nullptr, 1, &count),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_file_read(context, valid, nullptr, 1, &count),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_file_close(context, valid), TALOS_RT_OK);
}

TEST_F(RuntimeTest, SupportsAppendAndUpdateModes) {
  TempDirectory temporary;
  std::filesystem::path path = temporary.file("append.txt");
  uint32_t first = open(path, "w");
  uint64_t count = 0;
  ASSERT_EQ(talos_rt_v1_file_write(context, first, "one", 3, &count),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_file_close(context, first), TALOS_RT_OK);

  uint32_t append = open(path, "a+b");
  ASSERT_EQ(talos_rt_v1_file_write(context, append, "two", 3, &count),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_file_rewind(context, append), TALOS_RT_OK);
  char bytes[6]{};
  uint64_t read = 0;
  ASSERT_EQ(
      talos_rt_v1_file_read(context, append, bytes, sizeof(bytes), &read),
      TALOS_RT_OK);
  EXPECT_EQ(std::string(bytes, static_cast<size_t>(read)), "onetwo");
  EXPECT_EQ(talos_rt_v1_file_close(context, append), TALOS_RT_OK);
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
      EXPECT_EQ(talos_rt_v1_file_write(context, descriptor, "x", 1, &written),
                TALOS_RT_OK);
      EXPECT_EQ(written, 1u);
    }
    if (readable) {
      ASSERT_EQ(talos_rt_v1_file_rewind(context, descriptor), TALOS_RT_OK);
      char byte = 0;
      uint64_t read = 0;
      EXPECT_EQ(talos_rt_v1_file_read(context, descriptor, &byte, 1, &read),
                TALOS_RT_OK);
      EXPECT_EQ(read, 1u);
      EXPECT_EQ(byte, writable ? 'x' : 's');
    }
    EXPECT_EQ(talos_rt_v1_file_close(context, descriptor), TALOS_RT_OK);
  }
}

TEST_F(RuntimeTest, FansOutMultichannelDescriptors) {
  TempDirectory temporary;
  std::string firstPath = temporary.file("first.txt").string();
  std::string secondPath = temporary.file("second.txt").string();
  uint32_t first = 0;
  uint32_t second = 0;
  ASSERT_EQ(talos_rt_v1_file_open_mcd(context, firstPath.data(),
                                        firstPath.size(), &first),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_file_open_mcd(context, secondPath.data(),
                                        secondPath.size(), &second),
            TALOS_RT_OK);
  EXPECT_EQ(first & second, 0u);
  uint64_t written = 0;
  ASSERT_EQ(
      talos_rt_v1_file_write(context, first | second, "fanout", 6, &written),
      TALOS_RT_OK);
  EXPECT_EQ(written, 6u);
  ASSERT_EQ(talos_rt_v1_file_close(context, first | second), TALOS_RT_OK);
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
    ASSERT_EQ(talos_rt_v1_file_open_mcd(context, path.data(), path.size(),
                                          &descriptor),
              TALOS_RT_OK);
    EXPECT_EQ(combined & descriptor, 0u);
    combined |= descriptor;
    if (descriptor == (uint32_t{1} << 30))
      highestPath = path;
  }
  EXPECT_EQ(combined, 0x7ffffffeu);
  ASSERT_FALSE(highestPath.empty());
  talos_rt_arg_v1 item = stringArg("highest");
  ASSERT_EQ(talos_rt_v1_display(context, uint32_t{1} << 30, 0,
                                  TALOS_RT_RADIX_DECIMAL, &item, 1, nullptr),
            TALOS_RT_OK);
  std::string overflowPath = temporary.file("overflow").string();
  uint32_t overflow = 123;
  EXPECT_EQ(talos_rt_v1_file_open_mcd(context, overflowPath.data(),
                                        overflowPath.size(), &overflow),
            TALOS_RT_OUT_OF_RESOURCES);
  EXPECT_EQ(overflow, 0u);
  ASSERT_EQ(talos_rt_v1_file_close(context, combined), TALOS_RT_OK);
  EXPECT_EQ(readHostFile(highestPath), "highest");

  uint32_t reused = 0;
  ASSERT_EQ(talos_rt_v1_file_open_mcd(context, overflowPath.data(),
                                        overflowPath.size(), &reused),
            TALOS_RT_OK);
  EXPECT_NE(reused, 0u);
  EXPECT_EQ(talos_rt_v1_file_close(context, reused), TALOS_RT_OK);
}

TEST_F(RuntimeTest, ValidatesModesHandlesAndFileErrors) {
  TempDirectory temporary;
  std::string path = temporary.file("errors.txt").string();
  uint32_t descriptor = 99;
  EXPECT_EQ(talos_rt_v1_file_open(context, path.data(), path.size(), "bad", 3,
                                    &descriptor),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(descriptor, 0u);

  const char pathWithNul[] = {'b', 'a', 'd', '\0', 'p', 'a', 't', 'h'};
  EXPECT_EQ(talos_rt_v1_file_open(context, pathWithNul, sizeof(pathWithNul),
                                    "w", 1, &descriptor),
            TALOS_RT_INVALID_ARGUMENT);

  uint32_t readOnly = open(temporary.file("missing.txt"), "w+");
  EXPECT_EQ(talos_rt_v1_file_close(context, readOnly), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_file_close(context, readOnly),
            TALOS_RT_INVALID_HANDLE);

  readOnly = open(temporary.file("missing.txt"), "r");
  uint64_t written = 0;
  EXPECT_EQ(talos_rt_v1_file_write(context, readOnly, "x", 1, &written),
            TALOS_RT_IO_ERROR);
  int32_t errorCode = 0;
  RuntimeBuffer errorMessage;
  ASSERT_EQ(talos_rt_v1_file_error(context, readOnly, &errorCode,
                                     errorMessage.out()),
            TALOS_RT_OK);
  EXPECT_NE(errorCode, 0);
  EXPECT_FALSE(errorMessage.str().empty());
  EXPECT_EQ(talos_rt_v1_file_close(context, readOnly), TALOS_RT_OK);
}

TEST_F(RuntimeTest, ReusesDescriptorsAndClosesOwnedFilesOnDestroy) {
  TempDirectory temporary;
  std::filesystem::path firstPath = temporary.file("first.txt");
  uint32_t first = open(firstPath, "w");
  ASSERT_EQ(talos_rt_v1_file_close(context, first), TALOS_RT_OK);
  uint32_t reused = open(temporary.file("second.txt"), "w");
  EXPECT_EQ(first, reused);

  uint64_t written = 0;
  ASSERT_EQ(talos_rt_v1_file_write(context, reused, "saved", 5, &written),
            TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);
  context = nullptr;
  EXPECT_EQ(readHostFile(temporary.file("second.txt")), "saved");
}

TEST_F(RuntimeTest, KeepsLastErrorsIsolatedPerThread) {
  static constexpr std::array<std::string_view, 4> formats = {"%q", "x%q",
                                                              "xx%q", "xxx%q"};
  std::array<talos_rt_status, formats.size()> formatStatuses{};
  std::array<talos_rt_status, formats.size()> errorStatuses{};
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
      errorStatuses[index] = talos_rt_v1_last_error(context, message.out());
      messages[index] = message.str();
    });
  }
  for (std::thread &thread : threads)
    thread.join();

  for (size_t index = 0; index < formats.size(); ++index) {
    EXPECT_EQ(formatStatuses[index], TALOS_RT_FORMAT_ERROR);
    EXPECT_EQ(errorStatuses[index], TALOS_RT_OK);
    EXPECT_EQ(messages[index],
              "unknown format specifier at byte " + std::to_string(index));
  }
}

TEST_F(RuntimeTest, DoesNotReuseExitedThreadsLastError) {
  std::thread::id errorThread;
  std::thread producer([&] {
    errorThread = std::this_thread::get_id();
    EXPECT_EQ(format("%q", {}).first, TALOS_RT_FORMAT_ERROR);
  });
  producer.join();

  for (unsigned attempt = 0; attempt != 64; ++attempt) {
    std::thread::id readerThread;
    talos_rt_status status = TALOS_RT_IO_ERROR;
    std::string message;
    std::thread reader([&] {
      readerThread = std::this_thread::get_id();
      RuntimeBuffer buffer;
      status = talos_rt_v1_last_error(context, buffer.out());
      message = buffer.str();
    });
    reader.join();
    if (readerThread == errorThread) {
      EXPECT_EQ(status, TALOS_RT_OK);
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
        EXPECT_EQ(talos_rt_v1_file_write(context, descriptor, line.data(),
                                           line.size(), &count),
                  TALOS_RT_OK);
        EXPECT_EQ(count, line.size());
      }
    });
  }
  for (std::thread &thread : threads)
    thread.join();
  ASSERT_EQ(talos_rt_v1_file_flush(context, descriptor), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_file_rewind(context, descriptor), TALOS_RT_OK);

  std::string contents(threadCount * writesPerThread * 9, '\0');
  uint64_t read = 0;
  ASSERT_EQ(talos_rt_v1_file_read(context, descriptor, contents.data(),
                                    contents.size(), &read),
            TALOS_RT_OK);
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
  EXPECT_EQ(talos_rt_v1_file_close(context, descriptor), TALOS_RT_OK);
}

TEST(RuntimeFragmentTest, ExecutesTypedBytecodeThroughSharedABI) {
  std::vector<uint8_t> code;
  appendInstruction(code, TALOS_RT_BC_CONST, TALOS_RT_BC_TYPE_U64, 0, 0, 0,
                    19);
  appendInstruction(code, TALOS_RT_BC_CONST, TALOS_RT_BC_TYPE_U64, 1, 0, 0,
                    23);
  appendInstruction(code, TALOS_RT_BC_ADD, TALOS_RT_BC_TYPE_U64, 2, 0, 1);
  appendInstruction(code, TALOS_RT_BC_STORE_FRAME, TALOS_RT_BC_TYPE_U64, 0,
                    2, 0, 8);
  appendInstruction(code, TALOS_RT_BC_CONST, TALOS_RT_BC_TYPE_U64, 3, 0, 0,
                    1234);
  appendInstruction(code, TALOS_RT_BC_SUSPEND, TALOS_RT_BC_TYPE_NONE, 0,
                    TALOS_RT_SUSPEND_DELAY, 3, 0x89abcdefu);
  auto descriptor = bytecodeDescriptor(code, 4);
  std::array<uint64_t, 2> frame{};
  talos_rt_fragment_action_v1 action{};

  EXPECT_EQ(
      executeBytecode(descriptor, frame.data(), sizeof(frame), 0, &action),
      TALOS_RT_OK);
  EXPECT_EQ(frame[1], 42u);
  EXPECT_EQ(action.kind, TALOS_RT_FRAGMENT_SUSPEND);
  EXPECT_EQ(action.suspend_kind, TALOS_RT_SUSPEND_DELAY);
  EXPECT_EQ(action.continuation, 0x89abcdefu);
  EXPECT_EQ(action.payload, 1234u);
}

TEST(RuntimeFragmentTest, ExecutesArithmeticComparisonAndControlOpcodes) {
  std::vector<uint8_t> code;
  appendInstruction(code, TALOS_RT_BC_CONST, TALOS_RT_BC_TYPE_U64, 0, 0, 0,
                    7);
  appendInstruction(code, TALOS_RT_BC_MOVE, TALOS_RT_BC_TYPE_U64, 1, 0);
  appendInstruction(code, TALOS_RT_BC_CONST, TALOS_RT_BC_TYPE_U64, 2, 0, 0,
                    3);
  appendInstruction(code, TALOS_RT_BC_SUB, TALOS_RT_BC_TYPE_U64, 3, 1, 2);
  appendInstruction(code, TALOS_RT_BC_MUL, TALOS_RT_BC_TYPE_U64, 4, 3, 2);
  appendInstruction(code, TALOS_RT_BC_AND, TALOS_RT_BC_TYPE_U64, 5, 4, 1);
  appendInstruction(code, TALOS_RT_BC_OR, TALOS_RT_BC_TYPE_U64, 6, 5, 2);
  appendInstruction(code, TALOS_RT_BC_XOR, TALOS_RT_BC_TYPE_U64, 7, 6, 2);
  appendInstruction(code, TALOS_RT_BC_NOT, TALOS_RT_BC_TYPE_U64, 8, 7);
  appendInstruction(code, TALOS_RT_BC_EQ, TALOS_RT_BC_TYPE_U64, 9, 7, 3);
  appendInstruction(code, TALOS_RT_BC_ULT, TALOS_RT_BC_TYPE_U64, 10, 2, 0);
  appendInstruction(code, TALOS_RT_BC_CONST, TALOS_RT_BC_TYPE_I64, 11, 0, 0,
                    UINT64_MAX);
  appendInstruction(code, TALOS_RT_BC_CONST, TALOS_RT_BC_TYPE_I64, 12, 0, 0,
                    0);
  appendInstruction(code, TALOS_RT_BC_SLT, TALOS_RT_BC_TYPE_I64, 13, 11,
                    12);
  appendInstruction(code, TALOS_RT_BC_CONST, TALOS_RT_BC_TYPE_BOOL, 14, 0,
                    0, 0);
  appendInstruction(code, TALOS_RT_BC_BRANCH_ZERO, TALOS_RT_BC_TYPE_BOOL, 0,
                    14, 0, 17);
  appendInstruction(code, TALOS_RT_BC_TERMINATE);
  appendInstruction(code, TALOS_RT_BC_JUMP, TALOS_RT_BC_TYPE_NONE, 0, 0, 0,
                    19);
  appendInstruction(code, TALOS_RT_BC_TERMINATE);
  appendInstruction(code, TALOS_RT_BC_NOP);
  appendInstruction(code, TALOS_RT_BC_STORE_FRAME, TALOS_RT_BC_TYPE_U64, 0,
                    3, 0, 0);
  appendInstruction(code, TALOS_RT_BC_STORE_FRAME, TALOS_RT_BC_TYPE_U64, 0,
                    8, 0, 8);
  appendInstruction(code, TALOS_RT_BC_STORE_FRAME, TALOS_RT_BC_TYPE_BOOL, 0,
                    9, 0, 16);
  appendInstruction(code, TALOS_RT_BC_STORE_FRAME, TALOS_RT_BC_TYPE_BOOL, 0,
                    10, 0, 24);
  appendInstruction(code, TALOS_RT_BC_STORE_FRAME, TALOS_RT_BC_TYPE_BOOL, 0,
                    13, 0, 32);
  appendInstruction(code, TALOS_RT_BC_TERMINATE, TALOS_RT_BC_TYPE_NONE, 0,
                    0, 0, 99);
  auto descriptor = bytecodeDescriptor(code, 15);
  std::array<uint64_t, 5> frame{};
  talos_rt_fragment_action_v1 action{};

  ASSERT_EQ(
      executeBytecode(descriptor, frame.data(), sizeof(frame), 0, &action),
      TALOS_RT_OK);
  EXPECT_EQ(frame[0], 4u);
  EXPECT_EQ(frame[1], ~uint64_t{4});
  EXPECT_EQ(frame[2], 1u);
  EXPECT_EQ(frame[3], 1u);
  EXPECT_EQ(frame[4], 1u);
  EXPECT_EQ(action.kind, TALOS_RT_FRAGMENT_TERMINATE);
  EXPECT_EQ(action.payload, 99u);
}

TEST(RuntimeFragmentTest, BoundsRunawayBytecodeOnlyWhenAskedTo) {
  // A backward jump to itself is well-formed bytecode, so validation cannot
  // reject it. Only an explicit budget turns it into a reported failure.
  std::vector<uint8_t> code;
  appendInstruction(code, TALOS_RT_BC_JUMP, TALOS_RT_BC_TYPE_NONE, 0, 0, 0,
                    0);
  auto descriptor = bytecodeDescriptor(code, 0);
  talos_rt_fragment_action_v1 action{};
  EXPECT_EQ(executeBytecode(descriptor, nullptr, 0, 0, &action, 1000),
            TALOS_RT_STEP_LIMIT);

  // A terminating fragment is unaffected by a budget it never reaches.
  std::vector<uint8_t> terminating;
  appendInstruction(terminating, TALOS_RT_BC_TERMINATE);
  auto bounded = bytecodeDescriptor(terminating, 0);
  EXPECT_EQ(executeBytecode(bounded, nullptr, 0, 0, &action, 1), TALOS_RT_OK);
  EXPECT_EQ(action.kind, TALOS_RT_FRAGMENT_TERMINATE);
}

TEST(RuntimeFragmentTest, ContinuationSelectsBytecodeEntryInstruction) {
  std::vector<uint8_t> code;
  appendInstruction(code, TALOS_RT_BC_TERMINATE, TALOS_RT_BC_TYPE_NONE, 0,
                    0, 0, 1);
  appendInstruction(code, TALOS_RT_BC_CONTINUE, TALOS_RT_BC_TYPE_NONE, 0, 0,
                    0, 17);
  auto descriptor = bytecodeDescriptor(code, 0);
  constexpr talos_rt_bytecode_entry_v1 entries[] = {{3, 0}, {11, 1}};
  descriptor.code.bytecode.entries = entries;
  descriptor.code.bytecode.entry_count = std::size(entries);
  talos_rt_bytecode_validation_v1 validation{};
  descriptor.code.bytecode.validation = &validation;
  talos_rt_fragment_action_v1 action{};
  ASSERT_EQ(executeBytecode(descriptor, nullptr, 0, 11, &action),
            TALOS_RT_OK);
  EXPECT_EQ(action.kind, TALOS_RT_FRAGMENT_CONTINUE);
  EXPECT_EQ(action.continuation, 17u);
  EXPECT_NE(validation.state, 0u);
}

TEST(RuntimeFragmentTest, ValidationRecordSupportsConcurrentDispatch) {
  std::vector<uint8_t> code;
  appendInstruction(code, TALOS_RT_BC_TERMINATE);
  auto descriptor = bytecodeDescriptor(code, 0);
  talos_rt_bytecode_validation_v1 validation{};
  descriptor.code.bytecode.validation = &validation;
  std::atomic<unsigned> failures{0};
  std::vector<std::thread> threads;
  for (unsigned index = 0; index != 16; ++index)
    threads.emplace_back([&] {
      talos_rt_fragment_action_v1 action{};
      if (executeBytecode(descriptor, nullptr, 0, 0, &action) !=
              TALOS_RT_OK ||
          action.kind != TALOS_RT_FRAGMENT_TERMINATE)
        ++failures;
    });
  for (std::thread &thread : threads)
    thread.join();
  EXPECT_EQ(failures.load(), 0u);
  EXPECT_EQ(validation.state, TALOS_RT_BC_VALIDATION_VALID);
}

TEST(RuntimeFragmentTest, RejectsMalformedBytecodeAndFrameAccess) {
  auto rejects = [](const std::vector<uint8_t> &code, uint32_t registers) {
    auto descriptor = bytecodeDescriptor(code, registers);
    talos_rt_fragment_action_v1 action{};
    return executeBytecode(descriptor, nullptr, 0, 0, &action);
  };
  std::vector<uint8_t> empty;
  EXPECT_EQ(rejects(empty, 0), TALOS_RT_INVALID_BYTECODE);
  std::vector<uint8_t> truncated(3, 0);
  auto malformed = bytecodeDescriptor(truncated, 1);
  talos_rt_fragment_action_v1 action{};
  EXPECT_EQ(executeBytecode(malformed, nullptr, 0, 0, &action),
            TALOS_RT_INVALID_BYTECODE);

  std::vector<uint8_t> code;
  appendInstruction(code, TALOS_RT_BC_LOAD_FRAME, TALOS_RT_BC_TYPE_U64, 0,
                    0, 0, 8);
  appendInstruction(code, TALOS_RT_BC_TERMINATE);
  auto badFrame = bytecodeDescriptor(code, 1);
  uint64_t frame = 0;
  EXPECT_EQ(executeBytecode(badFrame, &frame, sizeof(frame), 0, &action),
            TALOS_RT_INVALID_BYTECODE);

  std::vector<uint8_t> invalidBool;
  appendInstruction(invalidBool, TALOS_RT_BC_CONST, TALOS_RT_BC_TYPE_BOOL,
                    0, 0, 0, 2);
  appendInstruction(invalidBool, TALOS_RT_BC_TERMINATE);
  auto badBool = bytecodeDescriptor(invalidBool, 1);
  EXPECT_EQ(executeBytecode(badBool, nullptr, 0, 0, &action),
            TALOS_RT_INVALID_BYTECODE);
  EXPECT_STREQ(talos_rt_v1_status_string(TALOS_RT_INVALID_BYTECODE),
               "invalid bytecode");

  std::vector<uint8_t> noneStore;
  appendInstruction(noneStore, TALOS_RT_BC_STORE_FRAME,
                    TALOS_RT_BC_TYPE_NONE, 0, 0);
  auto badStore = bytecodeDescriptor(noneStore, 1);
  EXPECT_EQ(executeBytecode(badStore, &frame, sizeof(frame), 0, &action),
            TALOS_RT_INVALID_BYTECODE);

  std::vector<uint8_t> noneBranch;
  appendInstruction(noneBranch, TALOS_RT_BC_BRANCH_ZERO,
                    TALOS_RT_BC_TYPE_NONE, 0, 0, 0, 0);
  auto badBranch = bytecodeDescriptor(noneBranch, 1);
  EXPECT_EQ(executeBytecode(badBranch, nullptr, 0, 0, &action),
            TALOS_RT_INVALID_BYTECODE);

  std::vector<uint8_t> invalidOpcode;
  appendInstruction(invalidOpcode, 0xff);
  EXPECT_EQ(rejects(invalidOpcode, 0), TALOS_RT_INVALID_BYTECODE);
  std::vector<uint8_t> invalidType;
  appendInstruction(invalidType, TALOS_RT_BC_CONST, 0xff, 0);
  EXPECT_EQ(rejects(invalidType, 1), TALOS_RT_INVALID_BYTECODE);
  std::vector<uint8_t> uninitializedMove;
  appendInstruction(uninitializedMove, TALOS_RT_BC_MOVE,
                    TALOS_RT_BC_TYPE_U64, 0, 0);
  EXPECT_EQ(rejects(uninitializedMove, 1), TALOS_RT_INVALID_BYTECODE);
  std::vector<uint8_t> invalidJump;
  appendInstruction(invalidJump, TALOS_RT_BC_JUMP, TALOS_RT_BC_TYPE_NONE, 0,
                    0, 0, 1);
  EXPECT_EQ(rejects(invalidJump, 0), TALOS_RT_INVALID_BYTECODE);
  std::vector<uint8_t> unterminated;
  appendInstruction(unterminated, TALOS_RT_BC_NOP);
  EXPECT_EQ(rejects(unterminated, 0), TALOS_RT_INVALID_BYTECODE);

  std::vector<uint8_t> terminate;
  appendInstruction(terminate, TALOS_RT_BC_TERMINATE);
  auto badEntries = bytecodeDescriptor(terminate, 0);
  constexpr talos_rt_bytecode_entry_v1 duplicateEntries[] = {{0, 0}, {0, 0}};
  badEntries.code.bytecode.entries = duplicateEntries;
  badEntries.code.bytecode.entry_count = std::size(duplicateEntries);
  EXPECT_EQ(executeBytecode(badEntries, nullptr, 0, 0, &action),
            TALOS_RT_INVALID_BYTECODE);
  constexpr talos_rt_bytecode_entry_v1 outOfRangeEntry{0, 1};
  badEntries.code.bytecode.entries = &outOfRangeEntry;
  badEntries.code.bytecode.entry_count = 1;
  EXPECT_EQ(executeBytecode(badEntries, nullptr, 0, 0, &action),
            TALOS_RT_INVALID_BYTECODE);
  auto missingEntry = bytecodeDescriptor(terminate, 0);
  EXPECT_EQ(executeBytecode(missingEntry, nullptr, 0, 7, &action),
            TALOS_RT_INVALID_BYTECODE);

  std::vector<uint8_t> fourInstructions;
  for (unsigned index = 0; index != 4; ++index)
    appendInstruction(fourInstructions, TALOS_RT_BC_TERMINATE);
  auto unsorted = bytecodeDescriptor(fourInstructions, 0);
  constexpr talos_rt_bytecode_entry_v1 unsortedEntries[] = {
      {0, 0}, {100, 1}, {50, 2}, {200, 3}};
  unsorted.code.bytecode.entries = unsortedEntries;
  unsorted.code.bytecode.entry_count = std::size(unsortedEntries);
  EXPECT_EQ(executeBytecode(unsorted, nullptr, 0, 0, &action),
            TALOS_RT_INVALID_BYTECODE);

  auto remoteBadInstruction = bytecodeDescriptor(terminate, 0);
  constexpr talos_rt_bytecode_entry_v1 remoteBadEntries[] = {{0, 0}, {1, 1}};
  remoteBadInstruction.code.bytecode.entries = remoteBadEntries;
  remoteBadInstruction.code.bytecode.entry_count = std::size(remoteBadEntries);
  EXPECT_EQ(executeBytecode(remoteBadInstruction, nullptr, 0, 0, &action),
            TALOS_RT_INVALID_BYTECODE);
}

talos_rt_status nativeFragment(talos_rt_context *, void *frame,
                                 uint64_t frameSize, uint32_t continuation,
                                 talos_rt_fragment_action_v1 *action) {
  if (!frame || frameSize != sizeof(uint64_t))
    return TALOS_RT_INVALID_ARGUMENT;
  ++*static_cast<uint64_t *>(frame);
  *action = {TALOS_RT_FRAGMENT_CONTINUE,
             TALOS_RT_SUSPEND_NONE,
             continuation + 1,
             0,
             0,
             0};
  return TALOS_RT_OK;
}

talos_rt_status invalidNativeAction(talos_rt_context *, void *, uint64_t,
                                      uint32_t,
                                      talos_rt_fragment_action_v1 *action) {
  *action = {TALOS_RT_FRAGMENT_CONTINUE, TALOS_RT_SUSPEND_NONE, 1, 0, 1, 0};
  return TALOS_RT_OK;
}

TEST(RuntimeFragmentTest, ValidatesNativeDescriptorsAndActions) {
  talos_rt_fragment_descriptor_v1 descriptor{};
  descriptor.handle = {TALOS_RT_DESCRIPTOR_FRAGMENT, 0, 3};
  descriptor.code_kind = TALOS_RT_FRAGMENT_NATIVE;
  descriptor.code.native_entry = invalidNativeAction;
  talos_rt_fragment_action_v1 action{};
  EXPECT_EQ(talos_rt_v1_fragment_execute(&descriptor, nullptr, nullptr, 0, 0,
                                           &action),
            TALOS_RT_INVALID_ARGUMENT);

  descriptor.flags = 1;
  EXPECT_EQ(talos_rt_v1_fragment_execute(&descriptor, nullptr, nullptr, 0, 0,
                                           &action),
            TALOS_RT_INVALID_ARGUMENT);
  descriptor.flags = 0;
  descriptor.code.native_entry = nullptr;
  EXPECT_EQ(talos_rt_v1_fragment_execute(&descriptor, nullptr, nullptr, 0, 0,
                                           &action),
            TALOS_RT_INVALID_ARGUMENT);
  descriptor.handle.kind = TALOS_RT_DESCRIPTOR_PROCESS;
  EXPECT_EQ(talos_rt_v1_fragment_execute(&descriptor, nullptr, nullptr, 0, 0,
                                           &action),
            TALOS_RT_INVALID_ARGUMENT);
}

TEST(RuntimeFragmentTest, NativeAndBytecodeUseOneDispatchContract) {
  talos_rt_fragment_descriptor_v1 native{};
  native.handle = {TALOS_RT_DESCRIPTOR_FRAGMENT, 0, 3};
  native.code_kind = TALOS_RT_FRAGMENT_NATIVE;
  native.code.native_entry = nativeFragment;
  uint64_t nativeFrame = 4;
  talos_rt_fragment_action_v1 nativeAction{};
  EXPECT_EQ(talos_rt_v1_fragment_execute(&native, nullptr, &nativeFrame,
                                           sizeof(nativeFrame), 11,
                                           &nativeAction),
            TALOS_RT_OK);
  EXPECT_EQ(nativeFrame, 5u);

  std::vector<uint8_t> code;
  appendInstruction(code, TALOS_RT_BC_LOAD_FRAME, TALOS_RT_BC_TYPE_U64, 0,
                    0, 0, 0);
  appendInstruction(code, TALOS_RT_BC_CONST, TALOS_RT_BC_TYPE_U64, 1, 0, 0,
                    1);
  appendInstruction(code, TALOS_RT_BC_ADD, TALOS_RT_BC_TYPE_U64, 2, 0, 1);
  appendInstruction(code, TALOS_RT_BC_STORE_FRAME, TALOS_RT_BC_TYPE_U64, 0,
                    2, 0, 0);
  appendInstruction(code, TALOS_RT_BC_CONTINUE, TALOS_RT_BC_TYPE_NONE, 0, 0,
                    0, 12);
  auto bytecode = bytecodeDescriptor(code, 3);
  constexpr talos_rt_bytecode_entry_v1 entry{11, 0};
  bytecode.code.bytecode.entries = &entry;
  bytecode.code.bytecode.entry_count = 1;
  uint64_t bytecodeFrame = 4;
  talos_rt_fragment_action_v1 bytecodeAction{};
  EXPECT_EQ(executeBytecode(bytecode, &bytecodeFrame, sizeof(bytecodeFrame), 11,
                            &bytecodeAction),
            TALOS_RT_OK);
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

  const talos_rt_bytecode_operand_v1 operands[] = {
      // format(format, arguments, environment, out buffer)
      {TALOS_RT_BC_OPERAND_CONSTANT, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_BYTES, 0, 0, 0, formatText.size(), 0},
      {TALOS_RT_BC_OPERAND_IMMEDIATE, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_ARGUMENT_ARRAY, 0, 0, 8, 1, 0},
      {TALOS_RT_BC_OPERAND_IMMEDIATE, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_FORMAT_ENVIRONMENT, 0, 0, 9, 5, 0},
      {TALOS_RT_BC_OPERAND_REGISTER, TALOS_RT_BC_OPERAND_OUTPUT,
       TALOS_RT_BC_VALUE_BUFFER, 0, 0, 1, 0, 0},
      // file_write(descriptor, resource bytes, out written)
      {TALOS_RT_BC_OPERAND_FRAME, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_U32, 0, 0, 0, 4, 0},
      {TALOS_RT_BC_OPERAND_RESOURCE, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_BYTES, 0, 0, 1, 0, 0},
      {TALOS_RT_BC_OPERAND_FRAME, TALOS_RT_BC_OPERAND_OUTPUT,
       TALOS_RT_BC_VALUE_U64, 0, 0, 8, 8, 0},
      // buffer_release(resource)
      {TALOS_RT_BC_OPERAND_RESOURCE, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_BUFFER, 0, 0, 1, 0, 0},
      // One time formatting argument followed by the environment's children.
      {TALOS_RT_BC_OPERAND_CONSTANT, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_ARGUMENT_TIME, 0, 0, 16, 8, 0},
      {TALOS_RT_BC_OPERAND_CONSTANT, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_BYTES, 0, 0, 24, scopeText.size(), 0},
      {TALOS_RT_BC_OPERAND_CONSTANT, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_BYTES, 0, 0, 40, libraryCellText.size(), 0},
      {TALOS_RT_BC_OPERAND_IMMEDIATE, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_U32, 0, 0, 0, 0, 0},
      {TALOS_RT_BC_OPERAND_CONSTANT, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_BYTES, 0, 0, 48, suffixText.size(), 0},
      {TALOS_RT_BC_OPERAND_IMMEDIATE, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_U64, 0, 0, 100, 0, 0},
  };
  const talos_rt_bytecode_service_site_v1 sites[] = {
      {TALOS_RT_BC_SERVICE_FORMAT, 0, 4, 0, 0},
      {TALOS_RT_BC_SERVICE_FILE_WRITE, 4, 3, 0, 0},
      {TALOS_RT_BC_SERVICE_BUFFER_RELEASE, 7, 1, 0, 0},
  };

  std::vector<uint8_t> code;
  appendInstruction(code, TALOS_RT_BC_CALL_SERVICE, TALOS_RT_BC_TYPE_STATUS,
                    0, 0, 0, 0);
  appendInstruction(code, TALOS_RT_BC_BRANCH_ZERO, TALOS_RT_BC_TYPE_STATUS,
                    0, 0, 0, 3);
  appendInstruction(code, TALOS_RT_BC_FAIL, TALOS_RT_BC_TYPE_STATUS, 0, 0);
  appendInstruction(code, TALOS_RT_BC_CALL_SERVICE, TALOS_RT_BC_TYPE_STATUS,
                    0, 0, 0, 1);
  appendInstruction(code, TALOS_RT_BC_BRANCH_ZERO, TALOS_RT_BC_TYPE_STATUS,
                    0, 0, 0, 6);
  appendInstruction(code, TALOS_RT_BC_FAIL, TALOS_RT_BC_TYPE_STATUS, 0, 0);
  appendInstruction(code, TALOS_RT_BC_CALL_SERVICE, TALOS_RT_BC_TYPE_STATUS,
                    0, 0, 0, 2);
  appendInstruction(code, TALOS_RT_BC_BRANCH_ZERO, TALOS_RT_BC_TYPE_STATUS,
                    0, 0, 0, 9);
  appendInstruction(code, TALOS_RT_BC_FAIL, TALOS_RT_BC_TYPE_STATUS, 0, 0);
  appendInstruction(code, TALOS_RT_BC_TERMINATE);

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
  talos_rt_fragment_action_v1 action{};
  ASSERT_EQ(executeBytecode(descriptor, &frame, sizeof(frame), 0, &action, 0,
                            context),
            TALOS_RT_OK);
  EXPECT_EQ(frame.written, 26u);
  EXPECT_EQ(action.kind, TALOS_RT_FRAGMENT_TERMINATE);
  ASSERT_EQ(talos_rt_v1_file_flush(context, descriptorValue), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_file_rewind(context, descriptorValue), TALOS_RT_OK);
  char output[64]{};
  uint64_t read = 0;
  ASSERT_EQ(talos_rt_v1_file_read(context, descriptorValue, output,
                                    sizeof(output), &read),
            TALOS_RT_OK);
  EXPECT_EQ(std::string(output, read), "top.worker work.top 4200ns");
  EXPECT_EQ(talos_rt_v1_file_close(context, descriptorValue), TALOS_RT_OK);
}

TEST_F(RuntimeTest, BytecodeServicesAcceptEmptyConstantPoolSlices) {
  const talos_rt_bytecode_operand_v1 operands[] = {
      {TALOS_RT_BC_OPERAND_CONSTANT, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_BYTES, 0, 0, 0, 0, 0},
      {TALOS_RT_BC_OPERAND_IMMEDIATE, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_ARGUMENT_ARRAY, 0, 0, 0, 0, 0},
      {TALOS_RT_BC_OPERAND_IMMEDIATE, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_FORMAT_ENVIRONMENT, 0, 0, 0, 0, 0},
      {TALOS_RT_BC_OPERAND_REGISTER, TALOS_RT_BC_OPERAND_OUTPUT,
       TALOS_RT_BC_VALUE_BUFFER, 0, 0, 1, 0, 0},
      {TALOS_RT_BC_OPERAND_RESOURCE, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_BUFFER, 0, 0, 1, 0, 0},
  };
  const talos_rt_bytecode_service_site_v1 sites[] = {
      {TALOS_RT_BC_SERVICE_FORMAT, 0, 4, 0, 0},
      {TALOS_RT_BC_SERVICE_BUFFER_RELEASE, 4, 1, 0, 0},
  };
  std::vector<uint8_t> code;
  appendCheckedService(code, 0);
  appendCheckedService(code, 1);
  appendInstruction(code, TALOS_RT_BC_TERMINATE);
  auto descriptor = bytecodeDescriptor(code, 2);
  descriptor.code.bytecode.service_sites = sites;
  descriptor.code.bytecode.service_site_count = std::size(sites);
  descriptor.code.bytecode.operands = operands;
  descriptor.code.bytecode.operand_count = std::size(operands);
  talos_rt_fragment_action_v1 action{};
  EXPECT_EQ(executeBytecode(descriptor, nullptr, 0, 0, &action, 0, context),
            TALOS_RT_OK);
  EXPECT_EQ(action.kind, TALOS_RT_FRAGMENT_TERMINATE);
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

  auto operand = [](talos_rt_bytecode_operand_kind kind,
                    talos_rt_bytecode_operand_direction direction,
                    talos_rt_bytecode_value_kind valueKind, uint64_t value,
                    uint64_t size = 0, uint64_t auxiliary = 0,
                    uint8_t flags = 0) {
    return talos_rt_bytecode_operand_v1{kind,  direction, valueKind, flags, 0,
                                          value, size,      auxiliary};
  };
  auto constantBytes = [&](ConstantSpan span) {
    return operand(TALOS_RT_BC_OPERAND_CONSTANT, TALOS_RT_BC_OPERAND_INPUT,
                   TALOS_RT_BC_VALUE_BYTES, span.offset, span.size);
  };
  auto inputFrame = [&](talos_rt_bytecode_value_kind kind, uint64_t offset,
                        uint64_t size) {
    return operand(TALOS_RT_BC_OPERAND_FRAME, TALOS_RT_BC_OPERAND_INPUT,
                   kind, offset, size);
  };
  auto outputFrame = [&](talos_rt_bytecode_value_kind kind, uint64_t offset,
                         uint64_t size) {
    return operand(TALOS_RT_BC_OPERAND_FRAME, TALOS_RT_BC_OPERAND_OUTPUT,
                   kind, offset, size);
  };
  auto immediate = [&](talos_rt_bytecode_value_kind kind, uint64_t value) {
    return operand(TALOS_RT_BC_OPERAND_IMMEDIATE, TALOS_RT_BC_OPERAND_INPUT,
                   kind, value);
  };
  auto outputBuffer = [&](uint16_t reg) {
    return operand(TALOS_RT_BC_OPERAND_REGISTER, TALOS_RT_BC_OPERAND_OUTPUT,
                   TALOS_RT_BC_VALUE_BUFFER, reg);
  };
  auto inputBuffer = [&](uint16_t reg) {
    return operand(TALOS_RT_BC_OPERAND_RESOURCE, TALOS_RT_BC_OPERAND_INPUT,
                   TALOS_RT_BC_VALUE_BUFFER, reg);
  };

  std::vector<talos_rt_bytecode_operand_v1> operands;
  std::vector<talos_rt_bytecode_service_site_v1> sites;
  std::vector<uint8_t> code;
  auto call =
      [&](talos_rt_bytecode_service service,
          std::initializer_list<talos_rt_bytecode_operand_v1> siteOperands) {
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
    return inputFrame(TALOS_RT_BC_VALUE_U32, descriptorOffset,
                      sizeof(frame.descriptor));
  };
  auto mcd = [&] {
    return inputFrame(TALOS_RT_BC_VALUE_U32, mcdOffset, sizeof(frame.mcd));
  };
  auto noEnvironment = [&] {
    return operand(TALOS_RT_BC_OPERAND_IMMEDIATE, TALOS_RT_BC_OPERAND_INPUT,
                   TALOS_RT_BC_VALUE_FORMAT_ENVIRONMENT, 0, 0);
  };

  call(TALOS_RT_BC_SERVICE_FILE_OPEN,
       {constantBytes(path), constantBytes(mode),
        outputFrame(TALOS_RT_BC_VALUE_U32, descriptorOffset,
                    sizeof(frame.descriptor))});

  uint64_t displayArgument = operands.size();
  operands.push_back(operand(TALOS_RT_BC_OPERAND_CONSTANT,
                             TALOS_RT_BC_OPERAND_INPUT,
                             TALOS_RT_BC_VALUE_ARGUMENT_STRING,
                             displayText.offset, displayText.size));
  call(TALOS_RT_BC_SERVICE_DISPLAY,
       {descriptor(), immediate(TALOS_RT_BC_VALUE_U32, 1),
        immediate(TALOS_RT_BC_VALUE_U32, TALOS_RT_RADIX_DECIMAL),
        operand(TALOS_RT_BC_OPERAND_IMMEDIATE, TALOS_RT_BC_OPERAND_INPUT,
                TALOS_RT_BC_VALUE_ARGUMENT_ARRAY, displayArgument, 1),
        noEnvironment()});
  call(TALOS_RT_BC_SERVICE_FILE_WRITE,
       {descriptor(), constantBytes(writtenText),
        outputFrame(TALOS_RT_BC_VALUE_U64, offsetof(Frame, written),
                    sizeof(frame.written))});
  call(TALOS_RT_BC_SERVICE_FILE_FLUSH, {descriptor()});
  call(TALOS_RT_BC_SERVICE_FILE_TELL,
       {descriptor(),
        outputFrame(TALOS_RT_BC_VALUE_I64, offsetof(Frame, firstTell),
                    sizeof(frame.firstTell))});
  call(TALOS_RT_BC_SERVICE_FILE_REWIND, {descriptor()});
  call(TALOS_RT_BC_SERVICE_FILE_GETLINE,
       {descriptor(), immediate(TALOS_RT_BC_VALUE_U64, 3), outputBuffer(1)});
  call(TALOS_RT_BC_SERVICE_BUFFER_RELEASE, {inputBuffer(1)});
  call(TALOS_RT_BC_SERVICE_FILE_TELL,
       {descriptor(),
        outputFrame(TALOS_RT_BC_VALUE_I64, offsetof(Frame, boundedTell),
                    sizeof(frame.boundedTell))});
  call(TALOS_RT_BC_SERVICE_FILE_REWIND, {descriptor()});
  call(TALOS_RT_BC_SERVICE_FILE_GETLINE,
       {descriptor(), immediate(TALOS_RT_BC_VALUE_U64, 64), outputBuffer(1)});
  call(TALOS_RT_BC_SERVICE_BUFFER_RELEASE, {inputBuffer(1)});
  call(TALOS_RT_BC_SERVICE_FILE_GETC,
       {descriptor(), outputFrame(TALOS_RT_BC_VALUE_U8, offsetof(Frame, byte),
                                  sizeof(frame.byte))});
  call(TALOS_RT_BC_SERVICE_FILE_UNGETC,
       {descriptor(), immediate(TALOS_RT_BC_VALUE_U8, 'a')});
  call(TALOS_RT_BC_SERVICE_FILE_READ,
       {descriptor(),
        operand(TALOS_RT_BC_OPERAND_FRAME, TALOS_RT_BC_OPERAND_INOUT,
                TALOS_RT_BC_VALUE_MUTABLE_BYTES, offsetof(Frame, data),
                frame.data.size()),
        outputFrame(TALOS_RT_BC_VALUE_U64, offsetof(Frame, read),
                    sizeof(frame.read))});
  call(TALOS_RT_BC_SERVICE_FILE_EOF,
       {descriptor(), outputFrame(TALOS_RT_BC_VALUE_U32, offsetof(Frame, eof),
                                  sizeof(frame.eof))});
  call(TALOS_RT_BC_SERVICE_FILE_ERROR,
       {descriptor(),
        outputFrame(TALOS_RT_BC_VALUE_I32, offsetof(Frame, error),
                    sizeof(frame.error)),
        outputBuffer(1)});
  call(TALOS_RT_BC_SERVICE_BUFFER_RELEASE, {inputBuffer(1)});
  call(TALOS_RT_BC_SERVICE_FILE_SEEK,
       {descriptor(), immediate(TALOS_RT_BC_VALUE_I64, 1),
        immediate(TALOS_RT_BC_VALUE_U32, TALOS_RT_SEEK_SET)});
  call(TALOS_RT_BC_SERVICE_FILE_TELL,
       {descriptor(),
        outputFrame(TALOS_RT_BC_VALUE_I64, offsetof(Frame, finalTell),
                    sizeof(frame.finalTell))});
  call(TALOS_RT_BC_SERVICE_FILE_REWIND, {descriptor()});
  call(TALOS_RT_BC_SERVICE_FILE_CLOSE, {descriptor()});

  call(TALOS_RT_BC_SERVICE_FILE_OPEN_MCD,
       {constantBytes(mcdPath),
        outputFrame(TALOS_RT_BC_VALUE_U32, mcdOffset, sizeof(frame.mcd))});
  call(TALOS_RT_BC_SERVICE_FILE_WRITE,
       {mcd(), constantBytes(mcdText),
        outputFrame(TALOS_RT_BC_VALUE_U64, offsetof(Frame, mcdWritten),
                    sizeof(frame.mcdWritten))});
  call(TALOS_RT_BC_SERVICE_FILE_FLUSH, {mcd()});
  call(TALOS_RT_BC_SERVICE_FILE_CLOSE, {mcd()});
  appendInstruction(code, TALOS_RT_BC_TERMINATE);

  auto program = bytecodeDescriptor(code, 2);
  program.code.bytecode.constants = constants.data();
  program.code.bytecode.constant_size = constants.size();
  program.code.bytecode.service_sites = sites.data();
  program.code.bytecode.service_site_count = sites.size();
  program.code.bytecode.operands = operands.data();
  program.code.bytecode.operand_count = operands.size();
  talos_rt_fragment_action_v1 action{};
  ASSERT_EQ(
      executeBytecode(program, &frame, sizeof(frame), 0, &action, 0, context),
      TALOS_RT_OK);
  EXPECT_EQ(action.kind, TALOS_RT_FRAGMENT_TERMINATE);
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
  appendInstruction(code, TALOS_RT_BC_CALL_SERVICE, TALOS_RT_BC_TYPE_STATUS,
                    0, 0, 0, 0);
  appendInstruction(code, TALOS_RT_BC_TERMINATE);
  auto descriptor = bytecodeDescriptor(code, 1);
  talos_rt_fragment_action_v1 action{};

  talos_rt_bytecode_service_site_v1 badSite{999, 0, 0, 0, 0};
  descriptor.code.bytecode.service_sites = &badSite;
  descriptor.code.bytecode.service_site_count = 1;
  EXPECT_EQ(executeBytecode(descriptor, nullptr, 0, 0, &action),
            TALOS_RT_INVALID_BYTECODE);

  talos_rt_bytecode_operand_v1 forged{TALOS_RT_BC_OPERAND_RESOURCE,
                                        TALOS_RT_BC_OPERAND_INPUT,
                                        TALOS_RT_BC_VALUE_BUFFER,
                                        0,
                                        0,
                                        0,
                                        0,
                                        0};
  talos_rt_bytecode_service_site_v1 release{
      TALOS_RT_BC_SERVICE_BUFFER_RELEASE, 0, 1, 0, 0};
  descriptor.code.bytecode.service_sites = &release;
  descriptor.code.bytecode.operands = &forged;
  descriptor.code.bytecode.operand_count = 1;
  EXPECT_EQ(executeBytecode(descriptor, nullptr, 0, 0, &action),
            TALOS_RT_INVALID_BYTECODE);

  // A caller-provided validation record is an observation of validation, not
  // authority to bypass it.
  talos_rt_bytecode_validation_v1 forgedValidation{2, 0};
  descriptor.code.bytecode.validation = &forgedValidation;
  descriptor.code.bytecode.service_sites = &badSite;
  descriptor.code.bytecode.operands = nullptr;
  descriptor.code.bytecode.operand_count = 0;
  EXPECT_EQ(executeBytecode(descriptor, nullptr, 0, 0, &action),
            TALOS_RT_INVALID_BYTECODE);
  EXPECT_EQ(forgedValidation.state, 3u);
  descriptor.code.bytecode.validation = nullptr;

  // CALL_SERVICE must not store its status over one of the service results.
  const talos_rt_bytecode_operand_v1 eofOperands[] = {
      {TALOS_RT_BC_OPERAND_IMMEDIATE, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_U32, 0, 0, 0, 0, 0},
      {TALOS_RT_BC_OPERAND_REGISTER, TALOS_RT_BC_OPERAND_OUTPUT,
       TALOS_RT_BC_VALUE_U32, 0, 0, 0, 0, 0},
  };
  const talos_rt_bytecode_service_site_v1 eofSite{
      TALOS_RT_BC_SERVICE_FILE_EOF, 0, 2, 0, 0};
  descriptor.code.bytecode.service_sites = &eofSite;
  descriptor.code.bytecode.operands = eofOperands;
  descriptor.code.bytecode.operand_count = std::size(eofOperands);
  EXPECT_EQ(executeBytecode(descriptor, nullptr, 0, 0, &action),
            TALOS_RT_INVALID_BYTECODE);

  // Multi-result services also require distinct result registers.
  const talos_rt_bytecode_operand_v1 errorOperands[] = {
      {TALOS_RT_BC_OPERAND_IMMEDIATE, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_U32, 0, 0, 0, 0, 0},
      {TALOS_RT_BC_OPERAND_REGISTER, TALOS_RT_BC_OPERAND_OUTPUT,
       TALOS_RT_BC_VALUE_I32, 0, 0, 1, 0, 0},
      {TALOS_RT_BC_OPERAND_REGISTER, TALOS_RT_BC_OPERAND_OUTPUT,
       TALOS_RT_BC_VALUE_BUFFER, 0, 0, 1, 0, 0},
  };
  const talos_rt_bytecode_service_site_v1 errorSite{
      TALOS_RT_BC_SERVICE_FILE_ERROR, 0, 3, 0, 0};
  descriptor = bytecodeDescriptor(code, 2);
  descriptor.code.bytecode.service_sites = &errorSite;
  descriptor.code.bytecode.service_site_count = 1;
  descriptor.code.bytecode.operands = errorOperands;
  descriptor.code.bytecode.operand_count = std::size(errorOperands);
  EXPECT_EQ(executeBytecode(descriptor, nullptr, 0, 0, &action),
            TALOS_RT_INVALID_BYTECODE);
}

TEST_F(RuntimeTest, BytecodeResourcesRejectLeaksAndDoubleRelease) {
  TempDirectory temporary;
  uint32_t descriptorValue = open(temporary.file("line.txt"), "w+");
  uint64_t written = 0;
  ASSERT_EQ(
      talos_rt_v1_file_write(context, descriptorValue, "line\n", 5, &written),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_file_rewind(context, descriptorValue), TALOS_RT_OK);

  const talos_rt_bytecode_operand_v1 operands[] = {
      {TALOS_RT_BC_OPERAND_IMMEDIATE, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_U32, 0, 0, descriptorValue, 0, 0},
      {TALOS_RT_BC_OPERAND_IMMEDIATE, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_U64, 0, 0, 5, 0, 0},
      {TALOS_RT_BC_OPERAND_REGISTER, TALOS_RT_BC_OPERAND_OUTPUT,
       TALOS_RT_BC_VALUE_BUFFER, 0, 0, 1, 0, 0},
      {TALOS_RT_BC_OPERAND_RESOURCE, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_BUFFER, 0, 0, 1, 0, 0},
      {TALOS_RT_BC_OPERAND_IMMEDIATE, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_U32, 0, 0, descriptorValue, 0, 0},
      {TALOS_RT_BC_OPERAND_REGISTER, TALOS_RT_BC_OPERAND_OUTPUT,
       TALOS_RT_BC_VALUE_U32, 0, 0, 1, 0, 0},
  };
  const talos_rt_bytecode_service_site_v1 sites[] = {
      {TALOS_RT_BC_SERVICE_FILE_GETLINE, 0, 3, 0, 0},
      {TALOS_RT_BC_SERVICE_BUFFER_RELEASE, 3, 1, 0, 0},
      {TALOS_RT_BC_SERVICE_FILE_EOF, 4, 2, 0, 0},
  };

  std::vector<uint8_t> leakCode;
  appendCheckedService(leakCode, 0);
  appendInstruction(leakCode, TALOS_RT_BC_TERMINATE);
  auto leak = bytecodeDescriptor(leakCode, 2);
  leak.code.bytecode.service_sites = sites;
  leak.code.bytecode.service_site_count = std::size(sites);
  leak.code.bytecode.operands = operands;
  leak.code.bytecode.operand_count = std::size(operands);
  talos_rt_fragment_action_v1 action{};
  EXPECT_EQ(executeBytecode(leak, nullptr, 0, 0, &action, 0, context),
            TALOS_RT_INVALID_BYTECODE);

  ASSERT_EQ(talos_rt_v1_file_rewind(context, descriptorValue), TALOS_RT_OK);
  std::vector<uint8_t> overwriteResourceCode;
  appendCheckedService(overwriteResourceCode, 0);
  appendCheckedService(overwriteResourceCode, 2);
  appendInstruction(overwriteResourceCode, TALOS_RT_BC_TERMINATE);
  auto overwriteResource = bytecodeDescriptor(overwriteResourceCode, 2);
  overwriteResource.code.bytecode.service_sites = sites;
  overwriteResource.code.bytecode.service_site_count = std::size(sites);
  overwriteResource.code.bytecode.operands = operands;
  overwriteResource.code.bytecode.operand_count = std::size(operands);
  EXPECT_EQ(
      executeBytecode(overwriteResource, nullptr, 0, 0, &action, 0, context),
      TALOS_RT_INVALID_BYTECODE);

  ASSERT_EQ(talos_rt_v1_file_rewind(context, descriptorValue), TALOS_RT_OK);
  std::vector<uint8_t> doubleReleaseCode;
  appendCheckedService(doubleReleaseCode, 0);
  appendCheckedService(doubleReleaseCode, 1);
  appendCheckedService(doubleReleaseCode, 1);
  appendInstruction(doubleReleaseCode, TALOS_RT_BC_TERMINATE);
  auto doubleRelease = bytecodeDescriptor(doubleReleaseCode, 2);
  doubleRelease.code.bytecode.service_sites = sites;
  doubleRelease.code.bytecode.service_site_count = std::size(sites);
  doubleRelease.code.bytecode.operands = operands;
  doubleRelease.code.bytecode.operand_count = std::size(operands);
  EXPECT_EQ(executeBytecode(doubleRelease, nullptr, 0, 0, &action, 0, context),
            TALOS_RT_INVALID_BYTECODE);
  EXPECT_EQ(talos_rt_v1_file_close(context, descriptorValue), TALOS_RT_OK);
}

TEST(RuntimeFragmentTest, ValidatesEveryServiceOperandBoundary) {
  std::vector<uint8_t> code;
  appendCheckedService(code, 0);
  appendInstruction(code, TALOS_RT_BC_TERMINATE);
  talos_rt_fragment_action_v1 action{};

  auto check = [&](const talos_rt_bytecode_service_site_v1 &site,
                   const talos_rt_bytecode_operand_v1 *operands,
                   uint64_t operandCount, uint32_t registers = 1,
                   void *frame = nullptr, uint64_t frameSize = 0) {
    auto descriptor = bytecodeDescriptor(code, registers);
    descriptor.code.bytecode.service_sites = &site;
    descriptor.code.bytecode.service_site_count = 1;
    descriptor.code.bytecode.operands = operands;
    descriptor.code.bytecode.operand_count = operandCount;
    return executeBytecode(descriptor, frame, frameSize, 0, &action);
  };

  talos_rt_bytecode_service_site_v1 noArity{TALOS_RT_BC_SERVICE_FILE_CLOSE,
                                              0, 0, 0, 0};
  EXPECT_EQ(check(noArity, nullptr, 0), TALOS_RT_INVALID_BYTECODE);

  talos_rt_bytecode_operand_v1 wrongType{TALOS_RT_BC_OPERAND_IMMEDIATE,
                                           TALOS_RT_BC_OPERAND_INPUT,
                                           TALOS_RT_BC_VALUE_I64,
                                           0,
                                           0,
                                           0,
                                           0,
                                           0};
  talos_rt_bytecode_service_site_v1 close{TALOS_RT_BC_SERVICE_FILE_CLOSE, 0,
                                            1, 0, 0};
  EXPECT_EQ(check(close, &wrongType, 1), TALOS_RT_INVALID_BYTECODE);

  talos_rt_bytecode_operand_v1 badFrame{TALOS_RT_BC_OPERAND_FRAME,
                                          TALOS_RT_BC_OPERAND_INPUT,
                                          TALOS_RT_BC_VALUE_U32,
                                          0,
                                          0,
                                          8,
                                          4,
                                          0};
  uint32_t frame = 0;
  EXPECT_EQ(check(close, &badFrame, 1, 1, &frame, sizeof(frame)),
            TALOS_RT_INVALID_BYTECODE);

  talos_rt_bytecode_operand_v1 badRegister{TALOS_RT_BC_OPERAND_REGISTER,
                                             TALOS_RT_BC_OPERAND_INPUT,
                                             TALOS_RT_BC_VALUE_U32,
                                             0,
                                             0,
                                             1,
                                             0,
                                             0};
  EXPECT_EQ(check(close, &badRegister, 1), TALOS_RT_INVALID_BYTECODE);

  talos_rt_bytecode_operand_v1 badConstant{TALOS_RT_BC_OPERAND_CONSTANT,
                                             TALOS_RT_BC_OPERAND_INPUT,
                                             TALOS_RT_BC_VALUE_BYTES,
                                             0,
                                             0,
                                             1,
                                             8,
                                             0};
  talos_rt_bytecode_operand_v1 output{TALOS_RT_BC_OPERAND_REGISTER,
                                        TALOS_RT_BC_OPERAND_OUTPUT,
                                        TALOS_RT_BC_VALUE_U32,
                                        0,
                                        0,
                                        0,
                                        0,
                                        0};
  const talos_rt_bytecode_operand_v1 openOperands[] = {badConstant, output};
  talos_rt_bytecode_service_site_v1 openSite{
      TALOS_RT_BC_SERVICE_FILE_OPEN_MCD, 0, 2, 0, 0};
  auto descriptor = bytecodeDescriptor(code, 1);
  const uint8_t constant = 0;
  descriptor.code.bytecode.constants = &constant;
  descriptor.code.bytecode.constant_size = 1;
  descriptor.code.bytecode.service_sites = &openSite;
  descriptor.code.bytecode.service_site_count = 1;
  descriptor.code.bytecode.operands = openOperands;
  descriptor.code.bytecode.operand_count = std::size(openOperands);
  EXPECT_EQ(executeBytecode(descriptor, nullptr, 0, 0, &action),
            TALOS_RT_INVALID_BYTECODE);

  const talos_rt_bytecode_operand_v1 overlappingReadOperands[] = {
      {TALOS_RT_BC_OPERAND_IMMEDIATE, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_U32, 0, 0, 0, 0, 0},
      {TALOS_RT_BC_OPERAND_FRAME, TALOS_RT_BC_OPERAND_INOUT,
       TALOS_RT_BC_VALUE_MUTABLE_BYTES, 0, 0, 0, 8, 0},
      {TALOS_RT_BC_OPERAND_FRAME, TALOS_RT_BC_OPERAND_OUTPUT,
       TALOS_RT_BC_VALUE_U64, 0, 0, 4, 8, 0},
  };
  const talos_rt_bytecode_service_site_v1 overlappingReadSite{
      TALOS_RT_BC_SERVICE_FILE_READ, 0, 3, 0, 0};
  std::array<uint8_t, 16> overlappingFrame{};
  EXPECT_EQ(check(overlappingReadSite, overlappingReadOperands,
                  std::size(overlappingReadOperands), 1,
                  overlappingFrame.data(), overlappingFrame.size()),
            TALOS_RT_INVALID_BYTECODE);

  const talos_rt_bytecode_operand_v1 overlappingWriteOperands[] = {
      {TALOS_RT_BC_OPERAND_IMMEDIATE, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_U32, 0, 0, 0, 0, 0},
      {TALOS_RT_BC_OPERAND_FRAME, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_BYTES, 0, 0, 0, 8, 0},
      {TALOS_RT_BC_OPERAND_FRAME, TALOS_RT_BC_OPERAND_OUTPUT,
       TALOS_RT_BC_VALUE_U64, 0, 0, 0, 8, 0},
  };
  const talos_rt_bytecode_service_site_v1 overlappingWriteSite{
      TALOS_RT_BC_SERVICE_FILE_WRITE, 0, 3, 0, 0};
  EXPECT_EQ(check(overlappingWriteSite, overlappingWriteOperands,
                  std::size(overlappingWriteOperands), 1,
                  overlappingFrame.data(), overlappingFrame.size()),
            TALOS_RT_INVALID_BYTECODE);
}

TEST_F(RuntimeTest, BytecodeFailureOutputsMatchNativeZeroInitialization) {
  const talos_rt_bytecode_operand_v1 operands[] = {
      {TALOS_RT_BC_OPERAND_IMMEDIATE, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_U32, 0, 0, 0, 0, 0},
      {TALOS_RT_BC_OPERAND_FRAME, TALOS_RT_BC_OPERAND_OUTPUT,
       TALOS_RT_BC_VALUE_U32, 0, 0, 0, 4, 0},
  };
  const talos_rt_bytecode_service_site_v1 site{TALOS_RT_BC_SERVICE_FILE_EOF,
                                                 0, 2, 0, 0};
  std::vector<uint8_t> code;
  appendCheckedService(code, 0);
  appendInstruction(code, TALOS_RT_BC_TERMINATE);
  auto descriptor = bytecodeDescriptor(code, 1);
  descriptor.code.bytecode.service_sites = &site;
  descriptor.code.bytecode.service_site_count = 1;
  descriptor.code.bytecode.operands = operands;
  descriptor.code.bytecode.operand_count = std::size(operands);
  uint32_t eof = UINT32_C(0xdeadbeef);
  talos_rt_fragment_action_v1 action{};
  EXPECT_EQ(
      executeBytecode(descriptor, &eof, sizeof(eof), 0, &action, 0, context),
      TALOS_RT_INVALID_HANDLE);
  EXPECT_EQ(eof, 0u);
}

TEST(RuntimeFragmentTest, FailedBufferServiceStillProducesReleasableResource) {
  const talos_rt_bytecode_operand_v1 operands[] = {
      {TALOS_RT_BC_OPERAND_CONSTANT, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_BYTES, 0, 0, 0, 0, 0},
      {TALOS_RT_BC_OPERAND_IMMEDIATE, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_ARGUMENT_ARRAY, 0, 0, 0, 0, 0},
      {TALOS_RT_BC_OPERAND_IMMEDIATE, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_FORMAT_ENVIRONMENT, 0, 0, 0, 0, 0},
      {TALOS_RT_BC_OPERAND_REGISTER, TALOS_RT_BC_OPERAND_OUTPUT,
       TALOS_RT_BC_VALUE_BUFFER, 0, 0, 1, 0, 0},
      {TALOS_RT_BC_OPERAND_RESOURCE, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_BUFFER, 0, 0, 1, 0, 0},
  };
  const talos_rt_bytecode_service_site_v1 sites[] = {
      {TALOS_RT_BC_SERVICE_FORMAT, 0, 4, 0, 0},
      {TALOS_RT_BC_SERVICE_BUFFER_RELEASE, 4, 1, 0, 0},
  };
  std::vector<uint8_t> code;
  appendInstruction(code, TALOS_RT_BC_CALL_SERVICE, TALOS_RT_BC_TYPE_STATUS,
                    0, 0, 0, 0);
  appendInstruction(code, TALOS_RT_BC_CALL_SERVICE, TALOS_RT_BC_TYPE_STATUS,
                    2, 0, 0, 1);
  appendInstruction(code, TALOS_RT_BC_FAIL, TALOS_RT_BC_TYPE_STATUS, 0, 0);
  auto descriptor = bytecodeDescriptor(code, 3);
  descriptor.code.bytecode.service_sites = sites;
  descriptor.code.bytecode.service_site_count = std::size(sites);
  descriptor.code.bytecode.operands = operands;
  descriptor.code.bytecode.operand_count = std::size(operands);
  talos_rt_fragment_action_v1 action{};
  EXPECT_EQ(executeBytecode(descriptor, nullptr, 0, 0, &action),
            TALOS_RT_INVALID_ARGUMENT);
}

TEST_F(RuntimeTest, MalformedInstructionCannotRunEarlierFileService) {
  TempDirectory temporary;
  std::string path = temporary.file("must-not-exist.txt").string();
  std::vector<uint8_t> constants(path.begin(), path.end());
  uint64_t modeOffset = constants.size();
  constants.push_back('w');
  const talos_rt_bytecode_operand_v1 operands[] = {
      {TALOS_RT_BC_OPERAND_CONSTANT, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_BYTES, 0, 0, 0, path.size(), 0},
      {TALOS_RT_BC_OPERAND_CONSTANT, TALOS_RT_BC_OPERAND_INPUT,
       TALOS_RT_BC_VALUE_BYTES, 0, 0, modeOffset, 1, 0},
      {TALOS_RT_BC_OPERAND_FRAME, TALOS_RT_BC_OPERAND_OUTPUT,
       TALOS_RT_BC_VALUE_U32, 0, 0, 0, 4, 0},
  };
  const talos_rt_bytecode_service_site_v1 site{
      TALOS_RT_BC_SERVICE_FILE_OPEN, 0, 3, 0, 0};
  std::vector<uint8_t> code;
  appendInstruction(code, TALOS_RT_BC_CALL_SERVICE, TALOS_RT_BC_TYPE_STATUS,
                    0, 0, 0, 0);
  appendInstruction(code, 255);
  talos_rt_bytecode_validation_v1 validation{};
  auto descriptor = bytecodeDescriptor(code, 1);
  descriptor.code.bytecode.validation = &validation;
  descriptor.code.bytecode.constants = constants.data();
  descriptor.code.bytecode.constant_size = constants.size();
  descriptor.code.bytecode.service_sites = &site;
  descriptor.code.bytecode.service_site_count = 1;
  descriptor.code.bytecode.operands = operands;
  descriptor.code.bytecode.operand_count = std::size(operands);
  uint32_t opened = 0;
  talos_rt_fragment_action_v1 action{};
  EXPECT_EQ(executeBytecode(descriptor, &opened, sizeof(opened), 0, &action, 0,
                            context),
            TALOS_RT_INVALID_BYTECODE);
  EXPECT_EQ(validation.state, TALOS_RT_BC_VALIDATION_INVALID);
  EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(RuntimeFragmentTest, BytecodeServiceStatusPropagatesMissingContext) {
  const talos_rt_bytecode_operand_v1 operand{TALOS_RT_BC_OPERAND_IMMEDIATE,
                                               TALOS_RT_BC_OPERAND_INPUT,
                                               TALOS_RT_BC_VALUE_U32,
                                               0,
                                               0,
                                               0,
                                               0,
                                               0};
  const talos_rt_bytecode_service_site_v1 site{
      TALOS_RT_BC_SERVICE_FILE_FLUSH, 0, 1, 0, 0};
  std::vector<uint8_t> code;
  appendCheckedService(code, 0);
  appendInstruction(code, TALOS_RT_BC_TERMINATE);
  auto descriptor = bytecodeDescriptor(code, 1);
  descriptor.code.bytecode.service_sites = &site;
  descriptor.code.bytecode.service_site_count = 1;
  descriptor.code.bytecode.operands = &operand;
  descriptor.code.bytecode.operand_count = 1;
  talos_rt_fragment_action_v1 action{};
  EXPECT_EQ(executeBytecode(descriptor, nullptr, 0, 0, &action),
            TALOS_RT_INVALID_ARGUMENT);
}

// These objects interleave pointer-sized handle slots with 64-bit payload
// fields, so the slot stride is the wider of the two. Spelling the offsets as
// multiples of sizeof(void *) kept the payloads 8-byte aligned only at 64 bits;
// at 32 they landed misaligned and ran off the end of the object.
constexpr uint64_t kSlot = sizeof(void *) > 8 ? sizeof(void *) : 8;
constexpr uint64_t kSlotAlign = alignof(uint64_t) > alignof(void *)
                                    ? alignof(uint64_t)
                                    : alignof(void *);

constexpr uint64_t kNodeLinkOffset = kSlot;
constexpr uint64_t kNodeValueOffset = kSlot * 2;
constexpr uint64_t kDerivedExtraOffset = kSlot * 3;

const talos_rt_trace_entry_v1 nodeTraceEntry{kNodeLinkOffset,
                                               0,
                                               1,
                                               TALOS_RT_TRACE_STRONG,
                                               TALOS_RT_MANAGED_SLOT_CLASS,
                                               nullptr};
const talos_rt_trace_layout_v1 nodeTraceLayout{
    TALOS_RT_VERSION, 0, kSlot * 3, kSlotAlign, &nodeTraceEntry, 1};
const talos_rt_trace_entry_v1 derivedTraceEntry{
    0,
    0,
    1,
    TALOS_RT_TRACE_EMBEDDED,
    TALOS_RT_MANAGED_SLOT_INVALID,
    &nodeTraceLayout};
const talos_rt_trace_layout_v1 derivedTraceLayout{
    TALOS_RT_VERSION, 0, kSlot * 4, kSlotAlign, &derivedTraceEntry, 1};

talos_rt_status nodeValueMethod(talos_rt_context *context,
                                  talos_rt_gc_lane_v1 *,
                                  talos_rt_object_v1 *receiver,
                                  const talos_rt_method_argument_v1 *,
                                  uint32_t argumentCount, void *result,
                                  uint64_t resultSize) {
  if (!context || argumentCount != 0 || !result ||
      resultSize != sizeof(uint64_t))
    return TALOS_RT_INVALID_ARGUMENT;
  return talos_rt_v1_object_read(receiver, kNodeValueOffset, result,
                                   resultSize);
}

talos_rt_status derivedValueMethod(talos_rt_context *context,
                                     talos_rt_gc_lane_v1 *,
                                     talos_rt_object_v1 *receiver,
                                     const talos_rt_method_argument_v1 *,
                                     uint32_t argumentCount, void *result,
                                     uint64_t resultSize) {
  uint64_t value = 0;
  if (!context || argumentCount != 0 || !result ||
      resultSize != sizeof(value) ||
      talos_rt_v1_object_read(receiver, kNodeValueOffset, &value,
                                sizeof(value)) != TALOS_RT_OK)
    return TALOS_RT_INVALID_ARGUMENT;
  value += 100;
  std::memcpy(result, &value, sizeof(value));
  return TALOS_RT_OK;
}

const talos_rt_method_descriptor_v1 nodeMethods[]{
    {42, 0, TALOS_RT_METHOD_NO_BYTECODE, nodeValueMethod, nullptr, 0}};
const talos_rt_method_descriptor_v1 derivedMethods[]{
    {42, 0, TALOS_RT_METHOD_NO_BYTECODE, derivedValueMethod, nullptr, 0}};
const char nodeName[] = "node";
const char derivedName[] = "derived_node";
const talos_rt_class_descriptor_v1 nodeDescriptor{TALOS_RT_VERSION,
                                                    0,
                                                    1,
                                                    kSlot * 3,
                                                    kSlotAlign,
                                                    nullptr,
                                                    nullptr,
                                                    0,
                                                    &nodeTraceLayout,
                                                    nodeMethods,
                                                    std::size(nodeMethods),
                                                    nodeName,
                                                    sizeof(nodeName) - 1,
                                                    nullptr,
                                                    nullptr,
                                                    0,
                                                    0,
                                                    0,
                                                    0};
const talos_rt_random_edge_v1 randomNodeEdge{kNodeLinkOffset,
                                               kNodeValueOffset, UINT64_C(2)};
constexpr uint64_t kRandomNodeValueOffset = kSlot * 3;
const talos_rt_random_variable_v1 randomNodeVariable{
    kRandomNodeValueOffset,
    kNodeValueOffset,
    UINT64_C(1),
    UINT64_MAX,
    UINT64_MAX,
    64,
    TALOS_RT_RANDOM_VARIABLE_SIGNED};
const talos_rt_random_layout_v1 randomNodeLayout{
    TALOS_RT_VERSION, 0, &randomNodeEdge, 1, &randomNodeVariable, 1};
const talos_rt_trace_layout_v1 randomNodeTraceLayout{
    TALOS_RT_VERSION, 0, kSlot * 4, kSlotAlign, &nodeTraceEntry, 1};
const char randomNodeName[] = "random_node";
const talos_rt_class_descriptor_v1 randomNodeDescriptor{
    TALOS_RT_VERSION,
    0,
    6,
    kSlot * 4,
    kSlotAlign,
    nullptr,
    nullptr,
    0,
    &randomNodeTraceLayout,
    nodeMethods,
    std::size(nodeMethods),
    randomNodeName,
    sizeof(randomNodeName) - 1,
    &randomNodeLayout,
    nullptr,
    0,
    0,
    0,
    0};
const talos_rt_trace_entry_v1 randomDerivedTraceEntries[]{
    {0, 0, 1, TALOS_RT_TRACE_EMBEDDED, TALOS_RT_MANAGED_SLOT_INVALID,
     &randomNodeTraceLayout},
    {kSlot * 4, 0, 1, TALOS_RT_TRACE_STRONG, TALOS_RT_MANAGED_SLOT_CLASS,
     nullptr}};
const talos_rt_trace_layout_v1 randomDerivedTraceLayout{
    TALOS_RT_VERSION,
    0,
    kSlot * 6,
    kSlotAlign,
    randomDerivedTraceEntries,
    std::size(randomDerivedTraceEntries)};
const talos_rt_random_edge_v1 randomDerivedEdge{kSlot * 4, kNodeValueOffset,
                                                  UINT64_C(4)};
const talos_rt_random_variable_v1 randomDerivedVariable{
    kSlot * 5, kNodeValueOffset, UINT64_C(8), UINT64_MAX, UINT64_MAX, 32, 0};
const talos_rt_random_layout_v1 randomDerivedLayout{
    TALOS_RT_VERSION, 0, &randomDerivedEdge, 1, &randomDerivedVariable, 1};
const char randomDerivedName[] = "random_derived_node";
const talos_rt_class_descriptor_v1 randomDerivedDescriptor{
    TALOS_RT_VERSION,
    TALOS_RT_CLASS_FINAL,
    7,
    kSlot * 6,
    kSlotAlign,
    &randomNodeDescriptor,
    nullptr,
    0,
    &randomDerivedTraceLayout,
    nodeMethods,
    std::size(nodeMethods),
    randomDerivedName,
    sizeof(randomDerivedName) - 1,
    &randomDerivedLayout,
    nullptr,
    0,
    0,
    0,
    0};
const talos_rt_class_descriptor_v1 derivedDescriptor{
    TALOS_RT_VERSION,
    TALOS_RT_CLASS_FINAL,
    2,
    kSlot * 4,
    kSlotAlign,
    &nodeDescriptor,
    nullptr,
    0,
    &derivedTraceLayout,
    derivedMethods,
    std::size(derivedMethods),
    derivedName,
    sizeof(derivedName) - 1,
    nullptr,
    nullptr,
    0,
    0,
    0,
    0};
// One handle slot followed by a 128-bit packed payload. Spelling the payload
// as two more pointers happened to be right at 64 bits and leaves the object
// too small at 32.
const talos_rt_trace_layout_v1 planeTraceLayout{
    TALOS_RT_VERSION, 0, sizeof(void *) + 16, alignof(void *), nullptr, 0};
const char planeName[] = "plane_object";
const talos_rt_class_descriptor_v1 planeDescriptor{TALOS_RT_VERSION,
                                                     TALOS_RT_CLASS_FINAL,
                                                     3,
                                                     sizeof(void *) + 16,
                                                     alignof(void *),
                                                     nullptr,
                                                     nullptr,
                                                     0,
                                                     &planeTraceLayout,
                                                     nullptr,
                                                     0,
                                                     planeName,
                                                     sizeof(planeName) - 1,
                                                     nullptr,
                                                     nullptr,
                                                     0,
                                                     0,
                                                     0,
                                                     0};
const talos_rt_trace_entry_v1 weakTraceEntry{
    sizeof(talos_rt_managed_word_v1), 0,      1, TALOS_RT_TRACE_WEAK,
    TALOS_RT_MANAGED_SLOT_CLASS,      nullptr};
const talos_rt_trace_layout_v1 weakTraceLayout{
    TALOS_RT_VERSION,
    0,
    sizeof(talos_rt_managed_word_v1) * 2,
    alignof(talos_rt_managed_word_v1),
    &weakTraceEntry,
    1};
const char weakName[] = "weak_reference";
const talos_rt_class_descriptor_v1 weakDescriptor{
    TALOS_RT_VERSION,
    TALOS_RT_CLASS_FINAL | TALOS_RT_CLASS_WEAK_WRAPPER,
    4,
    sizeof(talos_rt_managed_word_v1) * 2,
    alignof(talos_rt_managed_word_v1),
    nullptr,
    nullptr,
    0,
    &weakTraceLayout,
    nullptr,
    0,
    weakName,
    sizeof(weakName) - 1,
    nullptr,
    nullptr,
    0,
    0,
    0,
    0};

class ManagedHeapTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
    ASSERT_NE(context, nullptr);
    ASSERT_EQ(talos_rt_v1_gc_lane_create(context, &lane), TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_gc_lane_enter(lane), TALOS_RT_OK);
  }

  void TearDown() override {
    if (lane) {
      EXPECT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);
      EXPECT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
    }
    talos_rt_v1_context_destroy(context);
  }

  talos_rt_context *context = nullptr;
  talos_rt_gc_lane_v1 *lane = nullptr;
};

TEST_F(ManagedHeapTest, PlusargsPreservePrefixOrderAndReplaceTheirIndex) {
  const char *arguments[] = {"sim", "+ABfirst", "+Asecond",
                             "+",   "+ABthird", "+case"};
  ASSERT_EQ(talos_rt_v1_context_configure_argv(
                context, static_cast<int>(std::size(arguments)), arguments),
            TALOS_RT_OK);
  EXPECT_TRUE(context->vpiArguments.empty());

  auto query = [&](std::string_view prefix, std::string &tail,
                   uint32_t &found) {
    talos_rt_string_v1 prefixString = 0;
    EXPECT_EQ(talos_rt_v1_string_create(lane, prefix.data(), prefix.size(),
                                          &prefixString),
              TALOS_RT_OK);
    talos_rt_string_v1 result = 0;
    EXPECT_EQ(talos_rt_v1_plusarg_value(context, lane, prefixString, &result,
                                          &found),
              TALOS_RT_OK);
    char scratch[8] = {};
    const char *bytes = nullptr;
    uint64_t size = 0;
    EXPECT_EQ(talos_rt_v1_string_view(result, scratch, &bytes, &size),
              TALOS_RT_OK);
    tail.assign(bytes ? bytes : "", static_cast<size_t>(size));
  };

  std::string tail;
  uint32_t found = 0;
  query("A", tail, found);
  EXPECT_EQ(found, 1u);
  EXPECT_EQ(tail, "Bfirst");
  query("AB", tail, found);
  EXPECT_EQ(found, 1u);
  EXPECT_EQ(tail, "first");
  query("", tail, found);
  EXPECT_EQ(found, 1u);
  EXPECT_EQ(tail, "ABfirst");
  query("CASE", tail, found);
  EXPECT_EQ(found, 0u);
  EXPECT_TRUE(tail.empty());

  const char *replacement[] = {
      "sim", "+NEW=value", "--coverage-output=kept.obcov",
      "--coverage-test=kept", "--coverage-tag=suite=kept"};
  ASSERT_EQ(talos_rt_v1_context_configure_argv(
                context, static_cast<int>(std::size(replacement)), replacement),
            TALOS_RT_OK);
  EXPECT_TRUE(context->vpiArguments.empty());
  query("A", tail, found);
  EXPECT_EQ(found, 0u);
  query("NEW=", tail, found);
  EXPECT_EQ(found, 1u);
  EXPECT_EQ(tail, "value");

  ASSERT_NE(context->coverage, nullptr);
  EXPECT_EQ(context->coverage->outputPath, "kept.obcov");
  EXPECT_EQ(context->coverage->testName, "kept");
  ASSERT_EQ(context->coverage->tags.size(), 1u);
  EXPECT_EQ(context->coverage->tags.front(),
            (std::pair<std::string, std::string>{"suite", "kept"}));

  talos_rt_random_state_v1 randomBefore{};
  ASSERT_EQ(talos_rt_v1_random_get_state(context, &randomBefore),
            TALOS_RT_OK);
  const char *failedCoverageLoad[] = {"sim", "+BROKEN", "--seed=19",
                                      "--coverage-load=missing.obcov"};
  EXPECT_EQ(talos_rt_v1_context_configure_argv(
                context, static_cast<int>(std::size(failedCoverageLoad)),
                failedCoverageLoad),
            TALOS_RT_IO_ERROR);
  talos_rt_random_state_v1 randomAfter{};
  ASSERT_EQ(talos_rt_v1_random_get_state(context, &randomAfter),
            TALOS_RT_OK);
  EXPECT_EQ(randomAfter.state, randomBefore.state);
  EXPECT_EQ(randomAfter.increment, randomBefore.increment);
  query("NEW=", tail, found);
  EXPECT_EQ(found, 1u);
  EXPECT_EQ(tail, "value");

  // Parsing and merging all requested inputs is transactional as a group:
  // the first valid database must not leak into live state if a later static
  // schema is incompatible.
  TempDirectory temporary;
  talos::coverage::Database validDatabase;
  talos::coverage::Run validRun;
  validRun.uuid.back() = 1;
  validRun.name = "staged";
  validDatabase.runs.push_back(validRun);
  std::filesystem::path validPath = temporary.file("valid.obcov");
  ASSERT_EQ(
      talos::coverage::writeFileAtomically(validPath.string(), validDatabase),
      talos::coverage::Status::Ok);
  talos::coverage::Database mismatchedDatabase;
  mismatchedDatabase.sourceFiles.push_back({1, "other.sv", {}});
  mismatchedDatabase.scopes.push_back({2, 0, "top", 0, "top"});
  mismatchedDatabase.linePoints.push_back({3, 1, 1, 2, "", 1, 1, 1, 2, 0, 0});
  std::filesystem::path mismatchedPath = temporary.file("mismatch.obcov");
  ASSERT_EQ(talos::coverage::writeFileAtomically(mismatchedPath.string(),
                                                   mismatchedDatabase),
            talos::coverage::Status::Ok);
  std::vector<std::string> stagedStorage{
      "sim",
      "+BROKEN",
      "--seed=19",
      "--coverage-output=broken.obcov",
      "--coverage-test=broken",
      "--coverage-tag=suite=broken",
      "--coverage-load=" + validPath.string(),
      "--coverage-load=" + mismatchedPath.string()};
  std::vector<const char *> stagedArguments;
  for (const std::string &argument : stagedStorage)
    stagedArguments.push_back(argument.c_str());
  EXPECT_EQ(talos_rt_v1_context_configure_argv(
                context, static_cast<int>(stagedArguments.size()),
                stagedArguments.data()),
            TALOS_RT_INVALID_DESIGN);
  EXPECT_EQ(context->coverage->schema, nullptr);
  EXPECT_EQ(context->coverage->outputPath, "kept.obcov");
  EXPECT_EQ(context->coverage->testName, "kept");
  ASSERT_EQ(context->coverage->tags.size(), 1u);
  EXPECT_EQ(context->coverage->tags.front(),
            (std::pair<std::string, std::string>{"suite", "kept"}));
  talos_rt_random_state_v1 randomAfterStagedMerge{};
  ASSERT_EQ(talos_rt_v1_random_get_state(context, &randomAfterStagedMerge),
            TALOS_RT_OK);
  EXPECT_EQ(randomAfterStagedMerge.state, randomBefore.state);
  EXPECT_EQ(randomAfterStagedMerge.increment, randomBefore.increment);
  query("NEW=", tail, found);
  EXPECT_EQ(found, 1u);
  EXPECT_EQ(tail, "value");

  const char *invalid[] = {"sim", "+BROKEN", "--seed=not-a-number"};
  EXPECT_EQ(talos_rt_v1_context_configure_argv(
                context, static_cast<int>(std::size(invalid)), invalid),
            TALOS_RT_INVALID_ARGUMENT);
  query("NEW=", tail, found);
  EXPECT_EQ(found, 1u);
  EXPECT_EQ(tail, "value");
}

TEST_F(ManagedHeapTest, PlusargIndexIsLazyAndEmptyWithoutArguments) {
  std::vector<std::string> storage;
  storage.reserve(4097);
  storage.emplace_back("sim");
  for (unsigned index = 0; index != 4096; ++index)
    storage.push_back("+ARG" + std::to_string(index) + "=value");
  std::vector<const char *> arguments;
  arguments.reserve(storage.size());
  for (const std::string &argument : storage)
    arguments.push_back(argument.c_str());
  ASSERT_EQ(talos_rt_v1_context_configure_argv(
                context, static_cast<int>(arguments.size()), arguments.data()),
            TALOS_RT_OK);
  EXPECT_FALSE(context->plusargIndexBuilt);
  EXPECT_TRUE(context->plusargIndexNodes.empty());
  EXPECT_TRUE(context->plusargIndexEdges.empty());

  talos_rt_string_v1 prefix = 0;
  ASSERT_EQ(talos_rt_v1_string_create(lane, "ARG4095=", 8, &prefix),
            TALOS_RT_OK);
  uint32_t found = 0;
  ASSERT_EQ(talos_rt_v1_plusarg_test(context, prefix, &found), TALOS_RT_OK);
  EXPECT_EQ(found, 1u);
  EXPECT_TRUE(context->plusargIndexBuilt);
  EXPECT_FALSE(context->plusargIndexNodes.empty());

  const char *empty[] = {"sim"};
  ASSERT_EQ(talos_rt_v1_context_configure_argv(context, 1, empty),
            TALOS_RT_OK);
  EXPECT_FALSE(context->plusargIndexBuilt);
  EXPECT_TRUE(context->plusargIndexNodes.empty());
  ASSERT_EQ(talos_rt_v1_plusarg_test(context, prefix, &found), TALOS_RT_OK);
  EXPECT_EQ(found, 0u);
  EXPECT_TRUE(context->plusargIndexBuilt);
  EXPECT_TRUE(context->plusargIndexNodes.empty());
  EXPECT_TRUE(context->plusargIndexEdges.empty());
}

TEST_F(ManagedHeapTest, PlusargConversionsAreStrictWideAndFourState) {
  auto parse = [&](std::string_view spelling, uint32_t radix, uint64_t width,
                   std::vector<uint8_t> &value, std::vector<uint8_t> &unknown) {
    talos_rt_string_v1 string = 0;
    EXPECT_EQ(talos_rt_v1_string_create(lane, spelling.data(),
                                          spelling.size(), &string),
              TALOS_RT_OK);
    uint64_t bytes = (width + 7) / 8;
    value.assign(static_cast<size_t>(bytes), 0xcc);
    unknown.assign(static_cast<size_t>(bytes), 0xcc);
    EXPECT_EQ(talos_rt_v1_plusarg_parse_logic(string, radix, width,
                                                value.data(), value.size(),
                                                unknown.data(), unknown.size()),
              TALOS_RT_OK);
  };

  std::vector<uint8_t> value, unknown;
  parse("123456789abcdef0123456789abcdef0", 16, 129, value, unknown);
  const std::array<uint8_t, 17> wideExpected{0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56,
                                             0x34, 0x12, 0xf0, 0xde, 0xbc, 0x9a,
                                             0x78, 0x56, 0x34, 0x12, 0x00};
  EXPECT_TRUE(std::equal(value.begin(), value.end(), wideExpected.begin()));
  EXPECT_TRUE(std::all_of(unknown.begin(), unknown.end(),
                          [](uint8_t byte) { return byte == 0; }));

  // Boundary widths are written directly, without a fixed 64-bit staging
  // value that would truncate bit 64 or sign-extend bit 63.
  parse("7fffffffffffffff", 16, 63, value, unknown);
  EXPECT_EQ(value, (std::vector<uint8_t>{0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                         0xff, 0x7f}));
  EXPECT_TRUE(std::all_of(unknown.begin(), unknown.end(),
                          [](uint8_t byte) { return byte == 0; }));
  parse("ffffffffffffffff", 16, 64, value, unknown);
  EXPECT_EQ(value, (std::vector<uint8_t>(8, 0xff)));
  parse("1ffffffffffffffff", 16, 65, value, unknown);
  EXPECT_EQ(value, (std::vector<uint8_t>{0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                         0xff, 0xff, 0x01}));
  parse("ffffffffffffffffffffffffffffffff", 16, 128, value, unknown);
  EXPECT_EQ(value, (std::vector<uint8_t>(16, 0xff)));

  parse("1x?z", 16, 65, value, unknown);
  EXPECT_EQ(value, (std::vector<uint8_t>{0xff, 0x10, 0, 0, 0, 0, 0, 0, 0}));
  EXPECT_EQ(unknown, (std::vector<uint8_t>{0xff, 0x0f, 0, 0, 0, 0, 0, 0, 0}));
  parse("x", 10, 65, value, unknown);
  EXPECT_EQ(value, (std::vector<uint8_t>(9, 0)));
  EXPECT_EQ(unknown, (std::vector<uint8_t>{0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                           0xff, 0xff, 0x01}));
  parse("?", 10, 65, value, unknown);
  EXPECT_EQ(value, (std::vector<uint8_t>{0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                         0xff, 0xff, 0x01}));
  EXPECT_EQ(unknown, value);

  // An underscore is a separator wherever it appears, so it does not make the
  // lone decimal x or z stop being the whole value.
  parse("z_", 10, 8, value, unknown);
  EXPECT_EQ(value, (std::vector<uint8_t>{0xff}));
  EXPECT_EQ(unknown, (std::vector<uint8_t>{0xff}));
  parse("_x_", 10, 8, value, unknown);
  EXPECT_EQ(value, (std::vector<uint8_t>{0x00}));
  EXPECT_EQ(unknown, (std::vector<uint8_t>{0xff}));
  // A decimal x or z still stands for the whole value, so it may not share the
  // conversion with a digit on either side.
  parse("z5", 10, 8, value, unknown);
  EXPECT_EQ(unknown, (std::vector<uint8_t>{0xff}));
  EXPECT_EQ(value, (std::vector<uint8_t>{0x00}));
  parse("5z", 10, 8, value, unknown);
  EXPECT_EQ(unknown, (std::vector<uint8_t>{0xff}));
  EXPECT_EQ(value, (std::vector<uint8_t>{0x00}));

  parse("10xz", 2, 8, value, unknown);
  EXPECT_EQ(value, (std::vector<uint8_t>{0x09}));
  EXPECT_EQ(unknown, (std::vector<uint8_t>{0x03}));
  parse(" \t+12", 10, 8, value, unknown);
  EXPECT_EQ(value, (std::vector<uint8_t>{12}));
  EXPECT_EQ(unknown, (std::vector<uint8_t>{0}));
  parse("12 ", 10, 8, value, unknown);
  EXPECT_EQ(value, (std::vector<uint8_t>{0}));
  EXPECT_EQ(unknown, (std::vector<uint8_t>{0xff}));
  // The conversion already supplies the radix. Verilog-style base prefixes
  // are not stripped: x remains a legal unknown hexadecimal digit, while b
  // is illegal in a binary field and poisons the complete conversion.
  parse("0x12", 16, 16, value, unknown);
  EXPECT_EQ(value, (std::vector<uint8_t>{0x12, 0x00}));
  EXPECT_EQ(unknown, (std::vector<uint8_t>{0x00, 0x0f}));
  parse("0b10", 2, 8, value, unknown);
  EXPECT_EQ(value, (std::vector<uint8_t>{0}));
  EXPECT_EQ(unknown, (std::vector<uint8_t>{0xff}));
  parse("12junk", 10, 37, value, unknown);
  EXPECT_EQ(value, (std::vector<uint8_t>(5, 0)));
  EXPECT_EQ(unknown, (std::vector<uint8_t>{0xff, 0xff, 0xff, 0xff, 0x1f}));
  parse("-1", 10, 129, value, unknown);
  EXPECT_EQ(value, (std::vector<uint8_t>{0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                         0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                         0xff, 0xff, 0xff, 0xff, 0x01}));
  EXPECT_TRUE(std::all_of(unknown.begin(), unknown.end(),
                          [](uint8_t byte) { return byte == 0; }));

  std::string scale(1024, 'a');
  parse(scale, 16, 4096, value, unknown);
  EXPECT_EQ(value.size(), 512u);
  EXPECT_TRUE(std::all_of(value.begin(), value.end(),
                          [](uint8_t byte) { return byte == 0xaa; }));
  EXPECT_TRUE(std::all_of(unknown.begin(), unknown.end(),
                          [](uint8_t byte) { return byte == 0; }));

  talos_rt_string_v1 realString = 0;
  ASSERT_EQ(talos_rt_v1_string_create(lane, "1.25junk", 8, &realString),
            TALOS_RT_OK);
  double real = 7.0;
  EXPECT_EQ(talos_rt_v1_plusarg_parse_real(realString, &real), TALOS_RT_OK);
  EXPECT_EQ(real, 0.0);
  ASSERT_EQ(talos_rt_v1_string_create(lane, " \t+1.25e+2", 10, &realString),
            TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_plusarg_parse_real(realString, &real), TALOS_RT_OK);
  EXPECT_EQ(real, 125.0);
  ASSERT_EQ(talos_rt_v1_string_create(lane, "1.25e", 5, &realString),
            TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_plusarg_parse_real(realString, &real), TALOS_RT_OK);
  EXPECT_EQ(real, 0.0);
}

TEST_F(ManagedHeapTest, NarrowBitInsertPreservesPackedStorageAndHandles) {
  talos_rt_object_v1 *object = nullptr;
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &planeDescriptor, &object),
            TALOS_RT_OK);

  std::array<uint8_t, 16> bytes{};
  ASSERT_EQ(talos_rt_v1_object_bits_insert(object, sizeof(void *), 128, 4, 1,
                                             UINT64_C(0xa5), 8),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_read(object, sizeof(void *), bytes.data(),
                                      bytes.size()),
            TALOS_RT_OK);
  EXPECT_EQ(bytes[0], 0x50);
  EXPECT_EQ(bytes[1], 0x0a);

  bytes.fill(0);
  ASSERT_EQ(talos_rt_v1_object_write(object, sizeof(void *), bytes.data(),
                                       bytes.size()),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_bits_insert(object, sizeof(void *), 128, -3, 1,
                                             UINT64_MAX, 8),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_read(object, sizeof(void *), bytes.data(),
                                      bytes.size()),
            TALOS_RT_OK);
  EXPECT_EQ(bytes[0], 0x1f);

  std::array<uint8_t, 16> before = bytes;
  EXPECT_EQ(talos_rt_v1_object_bits_insert(object, sizeof(void *), 128,
                                             INT64_MAX, 0, 0, 8),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_read(object, sizeof(void *), bytes.data(),
                                      bytes.size()),
            TALOS_RT_OK);
  EXPECT_EQ(bytes, before);

  talos_rt_object_v1 *node = nullptr;
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &nodeDescriptor, &node),
            TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_object_bits_insert(node, kNodeLinkOffset, 64, 0, 1,
                                             UINT64_C(1), 1),
            TALOS_RT_INVALID_ARGUMENT);
}

// IEEE 1800-2017 10.6: force has priority over procedural assign, and release
// exposes the still-active assign value. Managed shadow storage must therefore
// remain a precise GC root while it is hidden beneath force.
TEST_F(ManagedHeapTest, PropertyOverrideKeepsHiddenManagedValueRooted) {
  talos_rt_object_v1 *holder = nullptr;
  talos_rt_object_v1 *assigned = nullptr;
  talos_rt_object_v1 *forced = nullptr;
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &nodeDescriptor, &holder),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &nodeDescriptor, &assigned),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &nodeDescriptor, &forced),
            TALOS_RT_OK);

  talos_rt_gc_root_v1 holderRoot{};
  ASSERT_EQ(talos_rt_v1_gc_root_push(lane, &holderRoot, &holder),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_override(holder, kNodeLinkOffset,
                                          sizeof(assigned), 0, 1, 0, 0, 0,
                                          &assigned, nullptr),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_override(holder, kNodeLinkOffset,
                                          sizeof(forced), 0, 0, 0, 0, 0,
                                          &forced, nullptr),
            TALOS_RT_OK);

  // Ordinary writes are masked while either layer is active.
  ASSERT_EQ(talos_rt_v1_object_field_store(holder, kNodeLinkOffset, nullptr),
            TALOS_RT_OK);
  talos_rt_object_v1 *visible = nullptr;
  ASSERT_EQ(talos_rt_v1_object_field_load(holder, kNodeLinkOffset, &visible),
            TALOS_RT_OK);
  EXPECT_EQ(visible, forced);

  assigned = nullptr;
  forced = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_collect(lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_release_override(holder, kNodeLinkOffset,
                                                  sizeof(visible), 0, 0),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_field_load(holder, kNodeLinkOffset, &visible),
            TALOS_RT_OK);
  EXPECT_TRUE(talos_rt_v1_object_is_instance(visible, &nodeDescriptor));

  ASSERT_EQ(talos_rt_v1_object_release_override(holder, kNodeLinkOffset,
                                                  sizeof(visible), 0, 1),
            TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_gc_root_pop(lane, &holderRoot), TALOS_RT_OK);
}

TEST(ManagedHeap, StaticOverrideKeepsHiddenManagedValueRooted) {
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.state_bit_count = 64;
  talos_rt_context *context = nullptr;
  talos_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &context),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_create(context, &lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_enter(lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_register_static(context, 1, 0, 64),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_design_root_register(context, 0), TALOS_RT_OK);
  uint64_t target = talos_rt_v1_native_state_static_handle(1);
  ASSERT_NE(target, UINT64_MAX);

  talos_rt_object_v1 *assigned = nullptr;
  talos_rt_object_v1 *forced = nullptr;
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &nodeDescriptor, &assigned),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &nodeDescriptor, &forced),
            TALOS_RT_OK);
  uint64_t globalValue = 0;
  uint64_t globalUnknown = 0;
  ASSERT_EQ(talos_rt_v1_native_override(
                context, reinterpret_cast<uint8_t *>(&globalValue),
                reinterpret_cast<uint8_t *>(&globalUnknown), 64, target, 64,
                TALOS_RT_DESCRIPTOR_STORAGE, 1,
                reinterpret_cast<const uint8_t *>(&assigned), nullptr),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_override(
                context, reinterpret_cast<uint8_t *>(&globalValue),
                reinterpret_cast<uint8_t *>(&globalUnknown), 64, target, 64,
                TALOS_RT_DESCRIPTOR_STORAGE, 0,
                reinterpret_cast<const uint8_t *>(&forced), nullptr),
            TALOS_RT_OK);

  assigned = nullptr;
  forced = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_collect(lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_release_override(
                context, reinterpret_cast<uint8_t *>(&globalValue),
                reinterpret_cast<uint8_t *>(&globalUnknown), 64, target, 64,
                TALOS_RT_DESCRIPTOR_STORAGE, 0),
            TALOS_RT_OK);
  talos_rt_object_v1 *visible = nullptr;
  std::memcpy(&visible, &globalValue, sizeof(visible));
  EXPECT_TRUE(talos_rt_v1_object_is_instance(visible, &nodeDescriptor));

  EXPECT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);
}

TEST_F(RuntimeTest, SchedulerStatusReportNamesTheFailure) {
  // A standalone simulator returns the scheduler's status and exits, so a run
  // that ended on a runtime error has nowhere else to say what happened. A
  // successful run and a $fatal -- which printed its own message -- stay
  // quiet.
  testing::internal::CaptureStderr();
  talos_rt_v1_scheduler_report_status(context, TALOS_RT_OK);
  talos_rt_v1_scheduler_report_status(context, TALOS_RT_FATAL);
  EXPECT_EQ(testing::internal::GetCapturedStderr(), "");

  uint32_t closed = open(TempDirectory().file("reported.log"), "w");
  ASSERT_EQ(talos_rt_v1_file_close(context, closed), TALOS_RT_OK);
  uint64_t written = 0;
  char byte = 'x';
  ASSERT_EQ(talos_rt_v1_file_write(context, closed, &byte, 1, &written),
            TALOS_RT_INVALID_HANDLE);
  testing::internal::CaptureStderr();
  talos_rt_v1_scheduler_report_status(context, TALOS_RT_IO_ERROR);
  std::string reported = testing::internal::GetCapturedStderr();
  EXPECT_NE(reported.find("invalid output descriptor"), std::string::npos);
  EXPECT_NE(reported.find("status 4"), std::string::npos);
}

TEST_F(RuntimeTest, ClosedOutputDescriptorWarnsAndContinues) {
  // IEEE 1800-2017 21.3.4 leaves writing to a channel that is not open
  // undefined. Reporting it and carrying on tells the user what happened;
  // failing the display would end the simulation with nothing to read.
  TempDirectory temporary;
  uint32_t descriptor = open(temporary.file("closed-output.log"), "w");
  ASSERT_EQ(talos_rt_v1_file_close(context, descriptor), TALOS_RT_OK);
  testing::internal::CaptureStderr();
  talos_rt_arg_v1 item = stringArg("dropped");
  EXPECT_EQ(talos_rt_v1_display(context, descriptor, 1,
                                  TALOS_RT_RADIX_DECIMAL, &item, 1, nullptr),
            TALOS_RT_OK);
  std::string warnings = testing::internal::GetCapturedStderr();
  EXPECT_NE(warnings.find("output descriptor"), std::string::npos);
}

TEST_F(RuntimeTest, DesignatedFormatContinuesWithUnformattedArguments) {
  TempDirectory temporary;
  uint32_t descriptor = open(temporary.file("designated-format.bin"), "w+b");
  uint32_t designated =
      TALOS_RT_ARG_FORMAT_STRING | TALOS_RT_ARG_DESIGNATED_FORMAT;
  testing::internal::CaptureStderr();
  talos_rt_arg_v1 missing = stringArg("%s", designated);
  EXPECT_EQ(talos_rt_v1_display(context, descriptor, 0,
                                  TALOS_RT_RADIX_DECIMAL, &missing, 1,
                                  nullptr),
            TALOS_RT_OK);
  LogicValue surplus("10");
  std::vector<talos_rt_arg_v1> extra = {stringArg("%s", designated),
                                          stringArg("kept"), surplus.arg()};
  EXPECT_EQ(talos_rt_v1_display(context, descriptor, 0,
                                  TALOS_RT_RADIX_DECIMAL, extra.data(),
                                  extra.size(), nullptr),
            TALOS_RT_OK);
  std::string warnings = testing::internal::GetCapturedStderr();
  EXPECT_NE(warnings.find("not enough arguments"), std::string::npos);
  EXPECT_EQ(warnings.find("extra argument"), std::string::npos);

  ASSERT_EQ(talos_rt_v1_file_flush(context, descriptor), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_file_rewind(context, descriptor), TALOS_RT_OK);
  char bytes[32]{};
  uint64_t read = 0;
  ASSERT_EQ(
      talos_rt_v1_file_read(context, descriptor, bytes, sizeof(bytes), &read),
      TALOS_RT_OK);
  EXPECT_EQ(std::string(bytes, static_cast<size_t>(read)), "<%s>kept2");
  EXPECT_EQ(talos_rt_v1_file_close(context, descriptor), TALOS_RT_OK);
}

TEST_F(ManagedHeapTest, FormatsClassHandlesAsSingularPatterns) {
  talos_rt_object_v1 *object = nullptr;
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &nodeDescriptor, &object),
            TALOS_RT_OK);
  talos_rt_object_v1 *nullObject = nullptr;
  const talos_rt_arg_v1 arguments[] = {
      {TALOS_RT_ARG_MANAGED_OBJECT, 0, 0, &object, nullptr},
      {TALOS_RT_ARG_MANAGED_OBJECT, 0, 0, &nullObject, nullptr}};
  talos_rt_format_env_v1 environment{};
  environment.time_multiplier = 1;
  RuntimeBuffer output;
  ASSERT_EQ(talos_rt_v1_format(context, "%p|%0p", 6, arguments,
                                 std::size(arguments), &environment,
                                 output.out()),
            TALOS_RT_OK);
  EXPECT_EQ(output.str(), "class@" +
                              std::to_string(talos_rt_v1_object_id(object)) +
                              "|null");

  talos_rt_string_v1 string = 0;
  ASSERT_EQ(talos_rt_v1_string_create(lane, "not-a-class", 11, &string),
            TALOS_RT_OK);
  ASSERT_EQ(string & UINT64_C(3), UINT64_C(0));
  auto *notClass =
      reinterpret_cast<talos_rt_object_v1 *>(static_cast<uintptr_t>(string));
  const talos_rt_arg_v1 invalid[] = {
      {TALOS_RT_ARG_MANAGED_OBJECT, 0, 0, &notClass, nullptr}};
  EXPECT_EQ(talos_rt_v1_format(context, "%p", 2, invalid, std::size(invalid),
                                 &environment, output.out()),
            TALOS_RT_INVALID_HANDLE);
}

TEST_F(ManagedHeapTest, FormatsEnumsWithNamesAndPackedFallbacks) {
  talos_rt_string_v1 name = 0;
  ASSERT_EQ(talos_rt_v1_string_create(lane, "ELARGE", 6, &name),
            TALOS_RT_OK);
  uint64_t value = UINT64_C(0xf00d);
  talos_rt_enum_arg_v1 enumeration{
      32, TALOS_RT_ARG_SIGNED, 0, &value, nullptr, name};
  talos_rt_arg_v1 argument{TALOS_RT_ARG_ENUM, TALOS_RT_ARG_SIGNED, 0,
                             &enumeration, nullptr};
  const talos_rt_arg_v1 named[] = {argument, argument, argument};
  RuntimeBuffer output;
  ASSERT_EQ(talos_rt_v1_format(context, "%p|%0h|%s", 9, named,
                                 std::size(named), nullptr, output.out()),
            TALOS_RT_OK);
  EXPECT_EQ(output.str(), "ELARGE|f00d|ELARGE");

  enumeration.name = 0;
  value = 17;
  RuntimeBuffer fallbackOutput;
  ASSERT_EQ(talos_rt_v1_format(context, "%p", 2, &argument, 1, nullptr,
                                 fallbackOutput.out()),
            TALOS_RT_OK);
  EXPECT_EQ(fallbackOutput.str(), "17");

  enumeration.reserved = 1;
  RuntimeBuffer invalidOutput;
  EXPECT_EQ(talos_rt_v1_format(context, "%p", 2, &argument, 1, nullptr,
                                 invalidOutput.out()),
            TALOS_RT_INVALID_ARGUMENT);
}

TEST_F(ManagedHeapTest, FormatsRawAggregateRepresentationsWithoutTextLoss) {
  const std::string pattern("P\0Q", 3);
  const std::string twoState("A\0B", 3);
  const std::string fourState("C\0D", 3);
  talos_rt_raw_aggregate_arg_v1 aggregate{};
  ASSERT_EQ(talos_rt_v1_string_create(lane, pattern.data(), pattern.size(),
                                        &aggregate.pattern),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_string_create(lane, twoState.data(), twoState.size(),
                                        &aggregate.two_state),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_string_create(lane, fourState.data(),
                                        fourState.size(),
                                        &aggregate.four_state),
            TALOS_RT_OK);
  talos_rt_arg_v1 argument{TALOS_RT_ARG_RAW_AGGREGATE, 0, 0, &aggregate,
                             nullptr};
  const talos_rt_arg_v1 arguments[] = {argument, argument};
  RuntimeBuffer rawOutput;
  ASSERT_EQ(talos_rt_v1_format(context, "%u%z", 4, arguments,
                                 std::size(arguments), nullptr,
                                 rawOutput.out()),
            TALOS_RT_OK);
  EXPECT_EQ(rawOutput.str(), twoState + fourState);
  EXPECT_EQ(rawOutput.str().size(), 6u);

  RuntimeBuffer patternOutput;
  ASSERT_EQ(talos_rt_v1_format(context, "%s", 2, &argument, 1, nullptr,
                                 patternOutput.out()),
            TALOS_RT_OK);
  EXPECT_EQ(patternOutput.str(), "P Q");

  uint64_t packed = 1;
  talos_rt_arg_v1 packedArgument{TALOS_RT_ARG_LOGIC, 0, 1, &packed,
                                   nullptr};
  RuntimeBuffer packedWidth;
  RuntimeBuffer aggregateWidth;
  EXPECT_EQ(talos_rt_v1_format(context, "%1u", 3, &packedArgument, 1, nullptr,
                                 packedWidth.out()),
            TALOS_RT_FORMAT_ERROR);
  EXPECT_EQ(talos_rt_v1_format(context, "%1u", 3, &argument, 1, nullptr,
                                 aggregateWidth.out()),
            TALOS_RT_FORMAT_ERROR);

  RuntimeBuffer malformed;
  EXPECT_EQ(talos_rt_v1_format(context, "%", 1, &argument, 1, nullptr,
                                 malformed.out()),
            TALOS_RT_FORMAT_ERROR);
}

TEST_F(ManagedHeapTest, CollectsCyclesAndClearsWeakReferences) {
  talos_rt_object_v1 *first = nullptr;
  talos_rt_object_v1 *second = nullptr;
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &nodeDescriptor, &first),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &nodeDescriptor, &second),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_field_store(first, kNodeLinkOffset, second),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_field_store(second, kNodeLinkOffset, first),
            TALOS_RT_OK);

  talos_rt_gc_root_v1 firstRoot{};
  ASSERT_EQ(talos_rt_v1_gc_root_push(lane, &firstRoot, &first),
            TALOS_RT_OK);
  talos_rt_object_v1 *weak = nullptr;
  ASSERT_EQ(talos_rt_v1_weak_create(lane, &weakDescriptor, second, &weak),
            TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_object_is_instance(weak, &weakDescriptor), 1u);
  talos_rt_gc_root_v1 weakRoot{};
  ASSERT_EQ(talos_rt_v1_gc_root_push(lane, &weakRoot, &weak), TALOS_RT_OK);

  ASSERT_EQ(talos_rt_v1_gc_collect(lane), TALOS_RT_OK);
  talos_rt_gc_statistics_v1 statistics{};
  ASSERT_EQ(talos_rt_v1_gc_statistics(context, &statistics), TALOS_RT_OK);
  EXPECT_EQ(statistics.live_objects, 3u);

  first = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_collect(lane), TALOS_RT_OK);
  talos_rt_object_v1 *referent =
      reinterpret_cast<talos_rt_object_v1 *>(uintptr_t{1});
  ASSERT_EQ(talos_rt_v1_weak_get(weak, &referent), TALOS_RT_OK);
  EXPECT_EQ(referent, nullptr);
  ASSERT_EQ(talos_rt_v1_gc_statistics(context, &statistics), TALOS_RT_OK);
  EXPECT_EQ(statistics.live_objects, 1u);
  EXPECT_EQ(statistics.reclaimed_objects, 2u);

  ASSERT_EQ(talos_rt_v1_gc_root_pop(lane, &weakRoot), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_root_pop(lane, &firstRoot), TALOS_RT_OK);
}

TEST_F(ManagedHeapTest, DiscoversActiveRandomObjectGraphByIdentity) {
  talos_rt_object_v1 *root = nullptr;
  talos_rt_object_v1 *child = nullptr;
  talos_rt_object_v1 *derivedChild = nullptr;
  ASSERT_EQ(
      talos_rt_v1_object_allocate(lane, &randomDerivedDescriptor, &root),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &randomNodeDescriptor, &child),
            TALOS_RT_OK);
  ASSERT_EQ(
      talos_rt_v1_object_allocate(lane, &randomNodeDescriptor, &derivedChild),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_field_store(root, kNodeLinkOffset, child),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_field_store(root, kSlot * 4, derivedChild),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_field_store(child, kNodeLinkOffset, root),
            TALOS_RT_OK);

  talos_rt_random_graph_v1 *graph = nullptr;
  ASSERT_EQ(talos_rt_v1_random_graph_discover(lane, root, &graph),
            TALOS_RT_OK);
  ASSERT_NE(graph, nullptr);
  EXPECT_EQ(talos_rt_v1_random_graph_size(graph), 3u);
  EXPECT_EQ(talos_rt_v1_random_graph_object(graph, 0), root);
  EXPECT_EQ(talos_rt_v1_random_graph_object(graph, 1), child);
  EXPECT_EQ(talos_rt_v1_random_graph_object(graph, 2), derivedChild);
  EXPECT_EQ(talos_rt_v1_random_graph_object(graph, 3), nullptr);
  EXPECT_EQ(talos_rt_v1_random_graph_object_descriptor(graph, 0),
            &randomDerivedDescriptor);
  EXPECT_EQ(talos_rt_v1_random_graph_object_descriptor(graph, 1),
            &randomNodeDescriptor);
  EXPECT_EQ(talos_rt_v1_random_graph_object_descriptor(graph, 2),
            &randomNodeDescriptor);
  EXPECT_EQ(talos_rt_v1_random_graph_object_descriptor(graph, 3), nullptr);
  EXPECT_EQ(talos_rt_v1_random_graph_variable_count(graph), 4u);
  talos_rt_object_v1 *variableObject = nullptr;
  const talos_rt_random_variable_v1 *variable = nullptr;
  ASSERT_EQ(
      talos_rt_v1_random_graph_variable(graph, 0, &variableObject, &variable),
      TALOS_RT_OK);
  EXPECT_EQ(variableObject, root);
  EXPECT_EQ(variable, &randomNodeVariable);
  ASSERT_EQ(
      talos_rt_v1_random_graph_variable(graph, 1, &variableObject, &variable),
      TALOS_RT_OK);
  EXPECT_EQ(variableObject, root);
  EXPECT_EQ(variable, &randomDerivedVariable);
  ASSERT_EQ(
      talos_rt_v1_random_graph_variable(graph, 2, &variableObject, &variable),
      TALOS_RT_OK);
  EXPECT_EQ(variableObject, child);
  EXPECT_EQ(variable, &randomNodeVariable);
  ASSERT_EQ(
      talos_rt_v1_random_graph_variable(graph, 3, &variableObject, &variable),
      TALOS_RT_OK);
  EXPECT_EQ(variableObject, derivedChild);
  EXPECT_EQ(variable, &randomNodeVariable);
  EXPECT_EQ(
      talos_rt_v1_random_graph_variable(graph, 4, &variableObject, &variable),
      TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(variableObject, nullptr);
  EXPECT_EQ(variable, nullptr);

  talos_rt_object_v1 *referencedObject = nullptr;
  const talos_rt_random_variable_v1 *referencedVariable = nullptr;
  uint64_t graphVariableIndex = UINT64_MAX;
  const talos_rt_random_variable_reference_v1 rootValueReference{
      nullptr, 0, kRandomNodeValueOffset, 64,
      TALOS_RT_RANDOM_VARIABLE_SIGNED};
  ASSERT_EQ(talos_rt_v1_random_graph_resolve_variable(
                graph, 0, &rootValueReference, &referencedObject,
                &referencedVariable, &graphVariableIndex),
            TALOS_RT_OK);
  EXPECT_EQ(referencedObject, root);
  EXPECT_EQ(referencedVariable, &randomNodeVariable);
  EXPECT_EQ(graphVariableIndex, 0u);

  const uint64_t childPath[]{kNodeLinkOffset};
  const talos_rt_random_variable_reference_v1 childValueReference{
      childPath, std::size(childPath), kRandomNodeValueOffset, 64,
      TALOS_RT_RANDOM_VARIABLE_SIGNED};
  ASSERT_EQ(talos_rt_v1_random_graph_resolve_variable(
                graph, 0, &childValueReference, &referencedObject,
                &referencedVariable, &graphVariableIndex),
            TALOS_RT_OK);
  EXPECT_EQ(referencedObject, child);
  EXPECT_EQ(referencedVariable, &randomNodeVariable);
  EXPECT_EQ(graphVariableIndex, 2u);
  EXPECT_EQ(talos_rt_v1_random_graph_resolve_variable(
                graph, 2, &childValueReference, &referencedObject,
                &referencedVariable, &graphVariableIndex),
            TALOS_RT_INVALID_HANDLE);
  EXPECT_EQ(referencedObject, nullptr);
  EXPECT_EQ(referencedVariable, nullptr);
  EXPECT_EQ(graphVariableIndex, UINT64_MAX);

  const uint64_t aliasPath[]{kNodeLinkOffset, kNodeLinkOffset};
  const talos_rt_random_variable_reference_v1 aliasValueReference{
      aliasPath, std::size(aliasPath), kRandomNodeValueOffset, 64,
      TALOS_RT_RANDOM_VARIABLE_SIGNED};
  ASSERT_EQ(talos_rt_v1_random_graph_resolve_variable(
                graph, 0, &aliasValueReference, &referencedObject,
                &referencedVariable, &graphVariableIndex),
            TALOS_RT_OK);
  EXPECT_EQ(referencedObject, root);
  EXPECT_EQ(referencedVariable, &randomNodeVariable);
  EXPECT_EQ(graphVariableIndex, 0u);

  const uint64_t invalidPath[]{kRandomNodeValueOffset};
  const talos_rt_random_variable_reference_v1 invalidReference{
      invalidPath, std::size(invalidPath), kRandomNodeValueOffset, 64,
      TALOS_RT_RANDOM_VARIABLE_SIGNED};
  EXPECT_EQ(talos_rt_v1_random_graph_resolve_variable(
                graph, 0, &invalidReference, &referencedObject,
                &referencedVariable, &graphVariableIndex),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(referencedObject, nullptr);
  EXPECT_EQ(referencedVariable, nullptr);
  EXPECT_EQ(graphVariableIndex, UINT64_MAX);

  // The graph owns exact roots while callers compose a solver plan.
  ASSERT_EQ(talos_rt_v1_gc_collect(lane), TALOS_RT_OK);
  EXPECT_NE(talos_rt_v1_object_id(root), 0u);
  EXPECT_NE(talos_rt_v1_object_id(child), 0u);
  EXPECT_NE(talos_rt_v1_object_id(derivedChild), 0u);
  talos_rt_v1_random_graph_destroy(graph);

  uint64_t disabled = 7;
  ASSERT_EQ(talos_rt_v1_object_write(root, kNodeValueOffset, &disabled,
                                       sizeof(disabled)),
            TALOS_RT_OK);
  graph = nullptr;
  ASSERT_EQ(talos_rt_v1_random_graph_discover(lane, root, &graph),
            TALOS_RT_OK);
  ASSERT_NE(graph, nullptr);
  EXPECT_EQ(talos_rt_v1_random_graph_size(graph), 1u);
  EXPECT_EQ(talos_rt_v1_random_graph_object(graph, 0), root);
  EXPECT_EQ(talos_rt_v1_random_graph_object_descriptor(graph, 0),
            &randomDerivedDescriptor);
  EXPECT_EQ(talos_rt_v1_random_graph_variable_count(graph), 1u);
  ASSERT_EQ(
      talos_rt_v1_random_graph_variable(graph, 0, &variableObject, &variable),
      TALOS_RT_OK);
  EXPECT_EQ(variableObject, root);
  EXPECT_EQ(variable, &randomDerivedVariable);
  ASSERT_EQ(talos_rt_v1_random_graph_resolve_variable(
                graph, 0, &rootValueReference, &referencedObject,
                &referencedVariable, &graphVariableIndex),
            TALOS_RT_OK);
  EXPECT_EQ(referencedObject, root);
  EXPECT_EQ(referencedVariable, &randomNodeVariable);
  EXPECT_EQ(graphVariableIndex, UINT64_MAX);
  EXPECT_EQ(talos_rt_v1_random_graph_resolve_variable(
                graph, 0, &childValueReference, &referencedObject,
                &referencedVariable, &graphVariableIndex),
            TALOS_RT_INVALID_HANDLE);
  EXPECT_EQ(referencedObject, nullptr);
  EXPECT_EQ(referencedVariable, nullptr);
  EXPECT_EQ(graphVariableIndex, UINT64_MAX);
  talos_rt_v1_random_graph_destroy(graph);
}

TEST_F(ManagedHeapTest,
       ClassAssociativeKeysPreserveIdentityNullAndDerivedObjects) {
  const talos_rt_element_type_v1 wordElement{
      TALOS_RT_VERSION, TALOS_RT_ELEMENT_BITS, 91, 0,       0,
      sizeof(uint64_t),   alignof(uint64_t),       64, nullptr, nullptr};
  talos_rt_object_v1 *objects[2] = {};
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &nodeDescriptor, &objects[0]),
            TALOS_RT_OK);
  ASSERT_EQ(
      talos_rt_v1_object_allocate(lane, &derivedDescriptor, &objects[1]),
      TALOS_RT_OK);
  talos_rt_gc_root_range_v1 objectRoots{};
  ASSERT_EQ(talos_rt_v1_gc_root_range_push(lane, &objectRoots, objects, 2),
            TALOS_RT_OK);

  talos_rt_object_v1 *array = nullptr;
  ASSERT_EQ(talos_rt_v1_assoc_create(lane, &wordElement,
                                       TALOS_RT_ASSOC_KEY_CLASS, 0, &array),
            TALOS_RT_OK);
  talos_rt_gc_root_v1 arrayRoot{};
  ASSERT_EQ(talos_rt_v1_gc_root_push(lane, &arrayRoot, &array),
            TALOS_RT_OK);

  talos_rt_object_v1 *expectedKeys[] = {nullptr, objects[0], objects[1]};
  for (uint64_t index = 0; index != std::size(expectedKeys); ++index) {
    talos_rt_assoc_key_v1 key{TALOS_RT_ASSOC_KEY_CLASS, 0, 0, {}, {}, 0};
    key.object = expectedKeys[index];
    uint64_t value = index + 10;
    ASSERT_EQ(talos_rt_v1_assoc_write(lane, array, &key, &value, nullptr),
              TALOS_RT_OK);
  }

  ASSERT_EQ(talos_rt_v1_gc_root_range_pop(lane, &objectRoots), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_collect(lane), TALOS_RT_OK);
  for (uint64_t index = 0; index != std::size(expectedKeys); ++index) {
    talos_rt_assoc_key_v1 key{TALOS_RT_ASSOC_KEY_CLASS, 0, 0, {}, {}, 0};
    key.object = expectedKeys[index];
    uint64_t value = 0;
    uint32_t present = 0;
    ASSERT_EQ(talos_rt_v1_assoc_read(array, &key, &value, nullptr, &present),
              TALOS_RT_OK);
    EXPECT_EQ(present, 1u);
    EXPECT_EQ(value, index + 10);
  }

  ASSERT_EQ(talos_rt_v1_gc_set_threshold(context, 1), TALOS_RT_OK);
  talos_rt_object_v1 *cursorOnly = nullptr;
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &nodeDescriptor, &cursorOnly),
            TALOS_RT_OK);
  talos_rt_gc_root_v1 cursorRoot{};
  ASSERT_EQ(talos_rt_v1_gc_root_push(lane, &cursorRoot, &cursorOnly),
            TALOS_RT_OK);
  talos_rt_assoc_key_v1 absentCursor{
      TALOS_RT_ASSOC_KEY_CLASS, 0, 0, {}, {}, 0};
  absentCursor.object = cursorOnly;
  ASSERT_EQ(talos_rt_v1_gc_root_pop(lane, &cursorRoot), TALOS_RT_OK);
  uint32_t cursorSuccess = 1;
  ASSERT_EQ(
      talos_rt_v1_assoc_next(lane, array, &absentCursor, &cursorSuccess),
      TALOS_RT_OK);
  EXPECT_EQ(cursorSuccess, 0u);

  talos_rt_object_v1 *copy = nullptr;
  ASSERT_EQ(talos_rt_v1_container_clone(lane, array, &copy), TALOS_RT_OK);
  talos_rt_gc_root_v1 copyRoot{};
  ASSERT_EQ(talos_rt_v1_gc_root_push(lane, &copyRoot, &copy), TALOS_RT_OK);
  talos_rt_assoc_key_v1 derivedKey{
      TALOS_RT_ASSOC_KEY_CLASS, 0, 0, {}, {}, 0};
  derivedKey.object = expectedKeys[2];
  uint64_t value = 0;
  uint32_t present = 0;
  ASSERT_EQ(
      talos_rt_v1_assoc_read(copy, &derivedKey, &value, nullptr, &present),
      TALOS_RT_OK);
  EXPECT_EQ(present, 1u);
  EXPECT_EQ(value, 12u);
  EXPECT_EQ(talos_rt_v1_gc_root_pop(lane, &copyRoot), TALOS_RT_OK);

  talos_rt_object_v1 *path = nullptr;
  ASSERT_EQ(talos_rt_v1_reference_path_assoc_create(lane, array, &derivedKey,
                                                      nullptr, 0, 0, nullptr,
                                                      nullptr, 0, &path),
            TALOS_RT_OK);
  talos_rt_gc_root_v1 pathRoot{};
  ASSERT_EQ(talos_rt_v1_gc_root_push(lane, &pathRoot, &path), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_assoc_delete(array, &derivedKey), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_collect(lane), TALOS_RT_OK);
  uint64_t replacement = 42;
  ASSERT_EQ(
      talos_rt_v1_reference_path_store(lane, path, &replacement, nullptr),
      TALOS_RT_OK);
  value = 0;
  present = 0;
  ASSERT_EQ(talos_rt_v1_reference_path_load(path, &value, nullptr, &present),
            TALOS_RT_OK);
  EXPECT_EQ(present, 1u);
  EXPECT_EQ(value, replacement);
  value = 0;
  present = 1;
  ASSERT_EQ(
      talos_rt_v1_assoc_read(array, &derivedKey, &value, nullptr, &present),
      TALOS_RT_OK);
  EXPECT_EQ(present, 0u);
  EXPECT_EQ(talos_rt_v1_gc_root_pop(lane, &pathRoot), TALOS_RT_OK);

  talos_rt_assoc_key_v1 cursor{};
  uint32_t success = 0;
  for (size_t index = 0; index + 1 < std::size(expectedKeys); ++index) {
    talos_rt_object_v1 *expected = expectedKeys[index];
    ASSERT_EQ(talos_rt_v1_assoc_first(lane, array, &cursor, &success),
              TALOS_RT_OK);
    ASSERT_EQ(success, 1u);
    EXPECT_EQ(cursor.kind, TALOS_RT_ASSOC_KEY_CLASS);
    EXPECT_EQ(cursor.object, expected);
    ASSERT_EQ(talos_rt_v1_assoc_delete(array, &cursor), TALOS_RT_OK);
  }
  EXPECT_EQ(talos_rt_v1_container_size(array), 0u);
  EXPECT_EQ(talos_rt_v1_gc_root_pop(lane, &arrayRoot), TALOS_RT_OK);
}

TEST_F(ManagedHeapTest, TracesContiguousActivationRootRanges) {
  talos_rt_object_v1 *slots[2] = {};
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &nodeDescriptor, &slots[0]),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &nodeDescriptor, &slots[1]),
            TALOS_RT_OK);
  talos_rt_gc_root_range_v1 range{};
  ASSERT_EQ(talos_rt_v1_gc_root_range_push(lane, &range, slots, 2),
            TALOS_RT_OK);

  ASSERT_EQ(talos_rt_v1_gc_collect(lane), TALOS_RT_OK);
  talos_rt_gc_statistics_v1 statistics{};
  ASSERT_EQ(talos_rt_v1_gc_statistics(context, &statistics), TALOS_RT_OK);
  EXPECT_EQ(statistics.live_objects, 2u);

  slots[0] = nullptr;
  slots[1] = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_collect(lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_statistics(context, &statistics), TALOS_RT_OK);
  EXPECT_EQ(statistics.live_objects, 0u);
  EXPECT_EQ(talos_rt_v1_gc_root_range_pop(lane, &range), TALOS_RT_OK);
}

TEST_F(ManagedHeapTest, TracesManagedValuesInAutomaticAggregateState) {
  talos_rt_object_v1 *objects[2] = {};
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &nodeDescriptor, &objects[0]),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &nodeDescriptor, &objects[1]),
            TALOS_RT_OK);
  talos_rt_object_v1 *weak[2] = {};
  ASSERT_EQ(
      talos_rt_v1_weak_create(lane, &weakDescriptor, objects[0], &weak[0]),
      TALOS_RT_OK);
  ASSERT_EQ(
      talos_rt_v1_weak_create(lane, &weakDescriptor, objects[1], &weak[1]),
      TALOS_RT_OK);
  talos_rt_gc_root_range_v1 weakRoots{};
  ASSERT_EQ(talos_rt_v1_gc_root_range_push(lane, &weakRoots, weak, 2),
            TALOS_RT_OK);

  // Managed roots in native state are tagged 64-bit words, not raw pointers,
  // so the backing bytes are eight per root at any pointer width. Copying the
  // pointer array directly only produced the right layout at 64 bits.
  talos_rt_managed_word_v1 initialWords[std::size(objects)] = {};
  for (size_t index = 0; index != std::size(objects); ++index)
    initialWords[index] = static_cast<talos_rt_managed_word_v1>(
        reinterpret_cast<uintptr_t>(objects[index]));
  uint8_t initial[sizeof(initialWords)] = {};
  std::memcpy(initial, initialWords, sizeof(initialWords));
  const uint64_t invalidRootOffset = 1;
  uint64_t rolledBackHandle = 0;
  EXPECT_EQ(talos_rt_v1_native_state_alloc_with_roots(
                context, sizeof(initial) * 8, initial, nullptr,
                &invalidRootOffset, 1, &rolledBackHandle),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(rolledBackHandle, UINT64_MAX);
  rolledBackHandle = 0;
  EXPECT_EQ(talos_rt_v1_native_state_alloc_with_roots(
                context, sizeof(initial) * 8, initial, nullptr, nullptr, 1,
                &rolledBackHandle),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(rolledBackHandle, UINT64_MAX);
  uint64_t handle = UINT64_MAX;
  ASSERT_EQ(talos_rt_v1_native_state_alloc(context, sizeof(initial) * 8,
                                             initial, nullptr, &handle),
            TALOS_RT_OK);
  const uint64_t rootOffsets[] = {0, 64};
  ASSERT_EQ(talos_rt_v1_native_state_register_managed_roots(
                context, handle, rootOffsets, std::size(rootOffsets)),
            TALOS_RT_OK);
  objects[0] = nullptr;
  objects[1] = nullptr;

  ASSERT_EQ(talos_rt_v1_gc_collect(lane), TALOS_RT_OK);
  talos_rt_object_v1 *referent = nullptr;
  EXPECT_EQ(talos_rt_v1_weak_get(weak[0], &referent), TALOS_RT_OK);
  EXPECT_NE(referent, nullptr);
  EXPECT_EQ(talos_rt_v1_weak_get(weak[1], &referent), TALOS_RT_OK);
  EXPECT_NE(referent, nullptr);

  ASSERT_EQ(talos_rt_v1_native_state_release(context, handle, 0),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_collect(lane), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_weak_get(weak[0], &referent), TALOS_RT_OK);
  EXPECT_EQ(referent, nullptr);
  EXPECT_EQ(talos_rt_v1_weak_get(weak[1], &referent), TALOS_RT_OK);
  EXPECT_EQ(referent, nullptr);
  EXPECT_EQ(talos_rt_v1_gc_root_range_pop(lane, &weakRoots), TALOS_RT_OK);
}

TEST_F(ManagedHeapTest, AutomaticStringStatePreservesSSOAndHeapRoots) {
  talos_rt_string_v1 small = 0;
  ASSERT_EQ(talos_rt_v1_string_create(lane, "seven!!", 7, &small),
            TALOS_RT_OK);
  ASSERT_EQ(small & UINT64_C(3), UINT64_C(1));
  const uint64_t rootOffset = 0;
  uint64_t smallHandle = UINT64_MAX;
  ASSERT_EQ(talos_rt_v1_native_state_alloc_with_roots(
                context, 64, reinterpret_cast<const uint8_t *>(&small), nullptr,
                &rootOffset, 1, &smallHandle),
            TALOS_RT_OK);
  talos_rt_string_v1 loaded = 0;
  uint8_t dummy[8]{};
  ASSERT_EQ(talos_rt_v1_argument_ref_load(
                context, dummy, dummy, 64, nullptr, smallHandle, 0, 64, 8, 0,
                TALOS_RT_ARGUMENT_VALUE_STRING, &loaded, nullptr),
            TALOS_RT_OK);
  EXPECT_EQ(loaded, small);
  ASSERT_EQ(talos_rt_v1_native_state_release(context, smallHandle, 0),
            TALOS_RT_OK);

  talos_rt_string_v1 heap = 0;
  ASSERT_EQ(talos_rt_v1_string_create(lane, "heap-backed", 11, &heap),
            TALOS_RT_OK);
  ASSERT_EQ(heap & UINT64_C(3), UINT64_C(0));
  uint64_t heapHandle = UINT64_MAX;
  ASSERT_EQ(talos_rt_v1_native_state_alloc_with_roots(
                context, 64, reinterpret_cast<const uint8_t *>(&heap), nullptr,
                &rootOffset, 1, &heapHandle),
            TALOS_RT_OK);
  heap = 0;
  ASSERT_EQ(talos_rt_v1_gc_collect(lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_argument_ref_load(
                context, dummy, dummy, 64, nullptr, heapHandle, 0, 64, 8, 0,
                TALOS_RT_ARGUMENT_VALUE_STRING, &loaded, nullptr),
            TALOS_RT_OK);
  char scratch[8]{};
  const char *bytes = nullptr;
  uint64_t size = 0;
  ASSERT_EQ(talos_rt_v1_string_view(loaded, scratch, &bytes, &size),
            TALOS_RT_OK);
  EXPECT_EQ(std::string_view(bytes, size), "heap-backed");

  talos_rt_string_v1 equal = 0;
  ASSERT_EQ(talos_rt_v1_string_create(lane, "heap-backed", 11, &equal),
            TALOS_RT_OK);
  ASSERT_NE(equal, loaded);
  ASSERT_EQ(talos_rt_v1_argument_ref_store(
                context, dummy, dummy, 64, nullptr, heapHandle, 0, 64, 8, 0,
                TALOS_RT_ARGUMENT_VALUE_STRING, &equal, nullptr),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_native_state_release(context, heapHandle, 0),
            TALOS_RT_OK);
}

TEST_F(ManagedHeapTest, AccessesFourStatePlanesAtomicallyAcrossThreads) {
  talos_rt_object_v1 *object = nullptr;
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &planeDescriptor, &object),
            TALOS_RT_OK);
  talos_rt_gc_root_v1 root{};
  ASSERT_EQ(talos_rt_v1_gc_root_push(lane, &root, &object), TALOS_RT_OK);
  constexpr uint64_t first = UINT64_C(0xaaaaaaaaaaaaaaaa);
  constexpr uint64_t second = UINT64_C(0x5555555555555555);
  uint64_t initialUnknown = ~first;
  ASSERT_EQ(talos_rt_v1_object_write_planes(object, sizeof(void *), &first,
                                              &initialUnknown, sizeof(first)),
            TALOS_RT_OK);

  std::atomic<bool> inconsistent{false};
  std::thread writer([&] {
    for (unsigned iteration = 0; iteration != 10000; ++iteration) {
      uint64_t value = (iteration & 1) ? first : second;
      uint64_t unknown = ~value;
      if (talos_rt_v1_object_write_planes(object, sizeof(void *), &value,
                                            &unknown,
                                            sizeof(value)) != TALOS_RT_OK) {
        inconsistent.store(true, std::memory_order_relaxed);
        return;
      }
    }
  });
  std::thread reader([&] {
    for (unsigned iteration = 0; iteration != 10000; ++iteration) {
      uint64_t value = 0;
      uint64_t unknown = 0;
      if (talos_rt_v1_object_read_planes(object, sizeof(void *), &value,
                                           &unknown,
                                           sizeof(value)) != TALOS_RT_OK ||
          unknown != ~value) {
        inconsistent.store(true, std::memory_order_relaxed);
        return;
      }
    }
  });
  writer.join();
  reader.join();
  EXPECT_FALSE(inconsistent.load(std::memory_order_relaxed));
  EXPECT_EQ(talos_rt_v1_gc_root_pop(lane, &root), TALOS_RT_OK);
}

TEST_F(ManagedHeapTest, DispatchesOverridesAndShallowCopiesDynamicType) {
  talos_rt_object_v1 *object = nullptr;
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &derivedDescriptor, &object),
            TALOS_RT_OK);
  const uint64_t value = 23;
  const uint64_t extra = 99;
  ASSERT_EQ(talos_rt_v1_object_write(object, kNodeValueOffset, &value,
                                       sizeof(value)),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_write(object, kDerivedExtraOffset, &extra,
                                       sizeof(extra)),
            TALOS_RT_OK);
  EXPECT_TRUE(talos_rt_v1_object_is_instance(object, &derivedDescriptor));
  EXPECT_TRUE(talos_rt_v1_object_is_instance(object, &nodeDescriptor));

  uint64_t result = 0;
  ASSERT_EQ(talos_rt_v1_method_invoke(lane, object, 0, 42, nullptr, 0,
                                        &result, sizeof(result)),
            TALOS_RT_OK);
  EXPECT_EQ(result, 123u);
  EXPECT_EQ(talos_rt_v1_method_invoke(lane, object, 0, 43, nullptr, 0,
                                        &result, sizeof(result)),
            TALOS_RT_LAYOUT_MISMATCH);

  talos_rt_object_v1 *copy = nullptr;
  ASSERT_EQ(
      talos_rt_v1_object_shallow_copy(lane, &nodeDescriptor, object, &copy),
      TALOS_RT_OK);
  EXPECT_TRUE(talos_rt_v1_object_is_instance(copy, &nodeDescriptor));
  EXPECT_TRUE(talos_rt_v1_object_is_instance(copy, &derivedDescriptor));
  ASSERT_EQ(talos_rt_v1_method_invoke(lane, copy, 0, 42, nullptr, 0, &result,
                                        sizeof(result)),
            TALOS_RT_OK);
  EXPECT_EQ(result, 123u);
  result = 0;
  ASSERT_EQ(talos_rt_v1_object_read(copy, kDerivedExtraOffset, &result,
                                      sizeof(result)),
            TALOS_RT_OK);
  EXPECT_EQ(result, extra);
  EXPECT_NE(talos_rt_v1_object_id(object), talos_rt_v1_object_id(copy));

  talos_rt_object_v1 *castResult = nullptr;
  ASSERT_EQ(talos_rt_v1_object_cast(object, &nodeDescriptor, &castResult),
            TALOS_RT_OK);
  EXPECT_EQ(castResult, object);
  castResult = object;
  ASSERT_EQ(talos_rt_v1_object_cast(copy, &derivedDescriptor, &castResult),
            TALOS_RT_OK);
  EXPECT_EQ(castResult, copy);
  castResult = object;
  ASSERT_EQ(talos_rt_v1_object_cast(nullptr, &derivedDescriptor, &castResult),
            TALOS_RT_OK);
  EXPECT_EQ(castResult, nullptr);
}

TEST_F(ManagedHeapTest, ShallowCopiesContainerPropertiesAsValues) {
  // IEEE 1800-2023 8.12: a shallow copy shares object handles but copies
  // every property variable, so a container property gets its own value.
  const talos_rt_trace_entry_v1 entries[] = {
      {kSlot, 0, 1, TALOS_RT_TRACE_STRONG, TALOS_RT_MANAGED_SLOT_CONTAINER,
       nullptr},
      {kSlot * 2, 0, 1, TALOS_RT_TRACE_STRONG, TALOS_RT_MANAGED_SLOT_CLASS,
       nullptr}};
  const talos_rt_trace_layout_v1 layout{
      TALOS_RT_VERSION, 0, kSlot * 3, kSlotAlign, entries, 2};
  talos_rt_class_descriptor_v1 descriptor = nodeDescriptor;
  descriptor.class_id = 91;
  descriptor.layout = &layout;
  const talos_rt_element_type_v1 word{
      TALOS_RT_VERSION, TALOS_RT_ELEMENT_BITS, 93, 0,       0,
      sizeof(uint64_t),   alignof(uint64_t),       64, nullptr, nullptr};

  talos_rt_object_v1 *object = nullptr, *child = nullptr, *array = nullptr,
                       *copy = nullptr;
  talos_rt_gc_root_v1 objectRoot{}, copyRoot{};
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &descriptor, &object),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_root_push(lane, &objectRoot, &object),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &nodeDescriptor, &child),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_field_store(object, kSlot * 2, child),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_dynamic_array_create(lane, &word, 1, &array),
            TALOS_RT_OK);
  auto arrayWord = static_cast<talos_rt_managed_word_v1>(
      reinterpret_cast<uintptr_t>(array));
  ASSERT_EQ(talos_rt_v1_object_write(object, kSlot, &arrayWord,
                                       sizeof(arrayWord)),
            TALOS_RT_OK);
  const uint64_t value = 7;
  ASSERT_EQ(talos_rt_v1_container_write(lane, array, 0, &value, nullptr),
            TALOS_RT_OK);

  ASSERT_EQ(talos_rt_v1_object_shallow_copy(lane, &descriptor, object, &copy),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_root_push(lane, &copyRoot, &copy), TALOS_RT_OK);
  talos_rt_managed_word_v1 copiedWord = 0;
  talos_rt_object_v1 *copiedChild = nullptr;
  ASSERT_EQ(talos_rt_v1_object_read(copy, kSlot, &copiedWord,
                                      sizeof(copiedWord)),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_field_load(copy, kSlot * 2, &copiedChild),
            TALOS_RT_OK);
  auto *copiedArray = reinterpret_cast<talos_rt_object_v1 *>(
      static_cast<uintptr_t>(copiedWord));
  EXPECT_EQ(copiedChild, child);
  ASSERT_NE(copiedArray, nullptr);
  EXPECT_NE(copiedArray, array);
  uint64_t copied = 0;
  ASSERT_EQ(talos_rt_v1_container_read(copiedArray, 0, &copied, nullptr),
            TALOS_RT_OK);
  EXPECT_EQ(copied, value);
  EXPECT_EQ(talos_rt_v1_gc_root_pop(lane, &copyRoot), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_gc_root_pop(lane, &objectRoot), TALOS_RT_OK);
}

TEST_F(ManagedHeapTest, ShallowCopiesClearCopyResetRanges) {
  // IEEE 1800-2023 8.12: an embedded covergroup handle is null in a shallow
  // copy. Each descriptor lists its own ranges; the copy clears the whole
  // chain of the source's dynamic class.
  const talos_rt_byte_range_v1 baseResets[] = {{kNodeValueOffset, 8}};
  talos_rt_class_descriptor_v1 base = nodeDescriptor;
  base.class_id = 94;
  base.copy_resets = baseResets;
  base.copy_reset_count = 1;
  const talos_rt_byte_range_v1 derivedResets[] = {{kDerivedExtraOffset, 8}};
  talos_rt_class_descriptor_v1 derived = derivedDescriptor;
  derived.class_id = 95;
  derived.base = &base;
  derived.copy_resets = derivedResets;
  derived.copy_reset_count = 1;
  ASSERT_EQ(talos_rt_v1_class_validate(&derived), TALOS_RT_OK);

  talos_rt_object_v1 *object = nullptr, *copy = nullptr;
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &derived, &object),
            TALOS_RT_OK);
  talos_rt_gc_root_v1 root{};
  ASSERT_EQ(talos_rt_v1_gc_root_push(lane, &root, &object), TALOS_RT_OK);
  const uint64_t value = 23;
  for (uint64_t offset : {kNodeValueOffset, kDerivedExtraOffset})
    ASSERT_EQ(talos_rt_v1_object_write(object, offset, &value, sizeof(value)),
              TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_shallow_copy(lane, &base, object, &copy),
            TALOS_RT_OK);
  for (uint64_t offset : {kNodeValueOffset, kDerivedExtraOffset}) {
    uint64_t original = 0, copied = 1;
    ASSERT_EQ(talos_rt_v1_object_read(object, offset, &original,
                                        sizeof(original)),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_object_read(copy, offset, &copied, sizeof(copied)),
              TALOS_RT_OK);
    EXPECT_EQ(original, value) << offset;
    EXPECT_EQ(copied, 0u) << offset;
  }
  EXPECT_EQ(talos_rt_v1_gc_root_pop(lane, &root), TALOS_RT_OK);

  // Ranges must be nonempty, follow the descriptor word and fit the instance.
  for (talos_rt_byte_range_v1 invalid :
       {talos_rt_byte_range_v1{kNodeValueOffset, 0},
        talos_rt_byte_range_v1{0, 8},
        talos_rt_byte_range_v1{kSlot * 3, 8}}) {
    base.copy_resets = &invalid;
    EXPECT_EQ(talos_rt_v1_class_validate(&base), TALOS_RT_INVALID_DESIGN);
  }
  base.copy_resets = nullptr;
  EXPECT_EQ(talos_rt_v1_class_validate(&base), TALOS_RT_INVALID_DESIGN);
}

TEST_F(ManagedHeapTest, UsesChunkAllocationForSmallObjectChurn) {
  ASSERT_EQ(talos_rt_v1_gc_set_threshold(context, UINT64_MAX), TALOS_RT_OK);
  constexpr uint64_t objectCount = 200'000;
  for (uint64_t index = 0; index != objectCount; ++index) {
    talos_rt_object_v1 *object = nullptr;
    ASSERT_EQ(talos_rt_v1_object_allocate(lane, &nodeDescriptor, &object),
              TALOS_RT_OK);
  }
  talos_rt_gc_statistics_v1 statistics{};
  ASSERT_EQ(talos_rt_v1_gc_statistics(context, &statistics), TALOS_RT_OK);
  EXPECT_EQ(statistics.allocated_objects, objectCount);
  EXPECT_EQ(statistics.large_allocation_count, 0u);
  EXPECT_LE(statistics.chunk_allocation_count, 7u);

  ASSERT_EQ(talos_rt_v1_gc_collect(lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_statistics(context, &statistics), TALOS_RT_OK);
  EXPECT_EQ(statistics.live_objects, 0u);
  EXPECT_EQ(statistics.reclaimed_objects, objectCount);
  EXPECT_LE(statistics.cached_empty_chunks, 2u);
}

TEST_F(ManagedHeapTest, PinsAndStaticSlotsArePreciseRoots) {
  talos_rt_object_v1 *staticObject = nullptr;
  talos_rt_object_v1 *pinnedObject = nullptr;
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &nodeDescriptor, &staticObject),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &nodeDescriptor, &pinnedObject),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_static_root_register(context, &staticObject),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_pin(context, pinnedObject), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_collect(lane), TALOS_RT_OK);
  talos_rt_gc_statistics_v1 statistics{};
  ASSERT_EQ(talos_rt_v1_gc_statistics(context, &statistics), TALOS_RT_OK);
  EXPECT_EQ(statistics.live_objects, 2u);

  ASSERT_EQ(talos_rt_v1_gc_static_root_unregister(context, &staticObject),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_unpin(context, pinnedObject), TALOS_RT_OK);
  staticObject = nullptr;
  pinnedObject = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_collect(lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_statistics(context, &statistics), TALOS_RT_OK);
  EXPECT_EQ(statistics.live_objects, 0u);
}

TEST_F(ManagedHeapTest, AutomaticCollectionsClearWeakReferencesDuringChurn) {
  ASSERT_EQ(talos_rt_v1_gc_set_threshold(context, 1024), TALOS_RT_OK);
  talos_rt_object_v1 *referent = nullptr;
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &nodeDescriptor, &referent),
            TALOS_RT_OK);
  talos_rt_object_v1 *weak = nullptr;
  ASSERT_EQ(talos_rt_v1_weak_create(lane, &weakDescriptor, referent, &weak),
            TALOS_RT_OK);
  talos_rt_gc_root_v1 weakRoot{};
  ASSERT_EQ(talos_rt_v1_gc_root_push(lane, &weakRoot, &weak), TALOS_RT_OK);
  referent = nullptr;

  for (size_t index = 0; index != 1000; ++index) {
    talos_rt_object_v1 *garbage = nullptr;
    ASSERT_EQ(talos_rt_v1_object_allocate(lane, &nodeDescriptor, &garbage),
              TALOS_RT_OK);
  }
  ASSERT_EQ(talos_rt_v1_weak_get(weak, &referent), TALOS_RT_OK);
  EXPECT_EQ(referent, nullptr);
  talos_rt_gc_statistics_v1 statistics{};
  ASSERT_EQ(talos_rt_v1_gc_statistics(context, &statistics), TALOS_RT_OK);
  EXPECT_GT(statistics.collection_count, 0u);
  EXPECT_EQ(talos_rt_v1_gc_root_pop(lane, &weakRoot), TALOS_RT_OK);
}

TEST(ManagedHeap, CoordinatesConcurrentLaneSafepoints) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  constexpr size_t laneCount = 4;
  std::array<talos_rt_gc_lane_v1 *, laneCount> lanes{};
  for (auto &lane : lanes)
    ASSERT_EQ(talos_rt_v1_gc_lane_create(context, &lane), TALOS_RT_OK);

  std::atomic<size_t> ready{0};
  std::atomic<bool> start{false};
  std::atomic<bool> collectionDone{false};
  std::array<talos_rt_status, laneCount> statuses{};
  std::vector<std::thread> workers;
  for (size_t index = 0; index != laneCount; ++index) {
    workers.emplace_back([&, index] {
      talos_rt_gc_lane_v1 *lane = lanes[index];
      statuses[index] = talos_rt_v1_gc_lane_enter(lane);
      talos_rt_object_v1 *rooted = nullptr;
      talos_rt_gc_root_v1 root{};
      if (statuses[index] == TALOS_RT_OK)
        statuses[index] =
            talos_rt_v1_object_allocate(lane, &nodeDescriptor, &rooted);
      if (statuses[index] == TALOS_RT_OK)
        statuses[index] = talos_rt_v1_gc_root_push(lane, &root, &rooted);
      ready.fetch_add(1);
      while (!start.load())
        std::this_thread::yield();
      if (statuses[index] == TALOS_RT_OK) {
        if (index == 0) {
          statuses[index] = talos_rt_v1_gc_collect(lane);
          collectionDone.store(true);
        } else {
          while (!collectionDone.load() && statuses[index] == TALOS_RT_OK)
            statuses[index] = talos_rt_v1_gc_safepoint(lane);
        }
      }
      if (root.cookie) {
        EXPECT_EQ(talos_rt_v1_gc_root_pop(lane, &root), TALOS_RT_OK);
      }
      if (statuses[index] == TALOS_RT_OK)
        statuses[index] = talos_rt_v1_gc_lane_leave(lane);
    });
  }
  while (ready.load() != laneCount)
    std::this_thread::yield();
  start.store(true);
  for (std::thread &worker : workers)
    worker.join();
  for (talos_rt_status status : statuses)
    EXPECT_EQ(status, TALOS_RT_OK);
  for (auto *lane : lanes)
    EXPECT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);
}

TEST(ManagedHeap, ConcurrentSmallObjectAllocationUsesChunkedTLSCaches) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_set_threshold(context, UINT64_MAX), TALOS_RT_OK);
  constexpr size_t laneCount = 4;
  constexpr size_t objectsPerLane = 50'000;
  std::array<talos_rt_gc_lane_v1 *, laneCount> lanes{};
  for (auto &lane : lanes)
    ASSERT_EQ(talos_rt_v1_gc_lane_create(context, &lane), TALOS_RT_OK);

  std::array<talos_rt_status, laneCount> statuses{};
  std::array<std::vector<uint64_t>, laneCount> identities;
  std::vector<std::thread> workers;
  for (size_t index = 0; index != laneCount; ++index) {
    workers.emplace_back([&, index] {
      talos_rt_gc_lane_v1 *lane = lanes[index];
      statuses[index] = talos_rt_v1_gc_lane_enter(lane);
      identities[index].reserve(objectsPerLane);
      for (size_t objectIndex = 0;
           objectIndex != objectsPerLane && statuses[index] == TALOS_RT_OK;
           ++objectIndex) {
        talos_rt_object_v1 *object = nullptr;
        statuses[index] =
            talos_rt_v1_object_allocate(lane, &nodeDescriptor, &object);
        if (statuses[index] == TALOS_RT_OK)
          identities[index].push_back(talos_rt_v1_object_id(object));
      }
      if (statuses[index] == TALOS_RT_OK)
        statuses[index] = talos_rt_v1_gc_lane_leave(lane);
    });
  }
  for (std::thread &worker : workers)
    worker.join();
  for (talos_rt_status status : statuses)
    EXPECT_EQ(status, TALOS_RT_OK);

  std::vector<uint64_t> allIdentities;
  allIdentities.reserve(laneCount * objectsPerLane);
  for (const auto &laneIdentities : identities)
    allIdentities.insert(allIdentities.end(), laneIdentities.begin(),
                         laneIdentities.end());
  std::sort(allIdentities.begin(), allIdentities.end());
  EXPECT_EQ(allIdentities.size(), laneCount * objectsPerLane);
  EXPECT_EQ(std::adjacent_find(allIdentities.begin(), allIdentities.end()),
            allIdentities.end());

  talos_rt_gc_statistics_v1 statistics{};
  ASSERT_EQ(talos_rt_v1_gc_statistics(context, &statistics), TALOS_RT_OK);
  EXPECT_EQ(statistics.allocated_objects, laneCount * objectsPerLane);
  EXPECT_EQ(statistics.large_allocation_count, 0u);
  EXPECT_LE(statistics.chunk_allocation_count, 7u);
  for (auto *lane : lanes)
    EXPECT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);
}

TEST(ManagedHeap, DesignatedCollectionReclaimsOnlyAtQuiescentPoints) {
  // IEEE 1800-2023 8.29 leaves the timing of reclamation to the
  // implementation. Once a schedule designates its collection points,
  // allocation only records pressure; a collection point reclaims it outside
  // every managed activation, where static and frame roots hold all live
  // objects. Pressure far beyond the threshold still collects at allocation.
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_set_threshold(context, UINT64_MAX), TALOS_RT_OK);
  auto statistics = [&] {
    talos_rt_gc_statistics_v1 result{};
    EXPECT_EQ(talos_rt_v1_gc_statistics(context, &result), TALOS_RT_OK);
    return result;
  };
  auto allocate = [&](talos_rt_gc_lane_v1 *lane) {
    talos_rt_object_v1 *object = nullptr;
    EXPECT_EQ(talos_rt_v1_object_allocate(lane, &nodeDescriptor, &object),
              TALOS_RT_OK);
    return object;
  };

  talos_rt_gc_lane_v1 *lane = nullptr;
  bool entered = false;
  ASSERT_EQ(talos_rt_managed_execution_enter(context, &lane, &entered),
            TALOS_RT_OK);
  ASSERT_TRUE(entered);
  talos_rt_object_v1 *kept = allocate(lane);
  uint64_t objectBytes = statistics().live_bytes;
  ASSERT_GT(objectBytes, 0u);
  uint64_t keptID = talos_rt_v1_object_id(kept);
  ASSERT_EQ(talos_rt_v1_gc_static_root_register(context, &kept),
            TALOS_RT_OK);

  // Exceed the threshold, staying below the emergency multiple.
  ASSERT_EQ(talos_rt_v1_gc_set_threshold(context, objectBytes * 16),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_designate(context), TALOS_RT_OK);
  for (unsigned index = 0; index != 32; ++index)
    allocate(lane);
  EXPECT_EQ(statistics().collection_count, 0u);
  // A collection point inside a managed activation defers.
  {
    talos_rt_gc_lane_v1 *nested = nullptr;
    bool nestedEntered = true;
    ASSERT_EQ(talos_rt_managed_execution_enter(context, &nested,
                                                 &nestedEntered),
              TALOS_RT_OK);
    EXPECT_FALSE(nestedEntered);
    EXPECT_EQ(talos_rt_v1_gc_collect_pending(context), TALOS_RT_OK);
    talos_rt_managed_execution_leave(nested, nestedEntered);
  }
  EXPECT_EQ(statistics().collection_count, 0u);
  talos_rt_managed_execution_leave(lane, entered);

  // Outside every activation it reclaims the garbage and keeps static roots.
  EXPECT_EQ(talos_rt_v1_gc_collect_pending(context), TALOS_RT_OK);
  talos_rt_gc_statistics_v1 collected = statistics();
  EXPECT_EQ(collected.collection_count, 1u);
  EXPECT_EQ(collected.reclaimed_objects, 32u);
  EXPECT_EQ(talos_rt_v1_object_id(kept), keptID);
  // Nothing is pending afterwards.
  EXPECT_EQ(talos_rt_v1_gc_collect_pending(context), TALOS_RT_OK);
  EXPECT_EQ(statistics().collection_count, 1u);

  // An activation that allocates far past the threshold without reaching a
  // collection point still collects at allocation.
  ASSERT_EQ(talos_rt_managed_execution_enter(context, &lane, &entered),
            TALOS_RT_OK);
  for (unsigned index = 0; index != 80; ++index)
    allocate(lane);
  EXPECT_GE(statistics().collection_count, 2u);
  talos_rt_managed_execution_leave(lane, entered);
  ASSERT_EQ(talos_rt_v1_gc_static_root_unregister(context, &kept),
            TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);
}

TEST(ManagedHeap, PolledCollectionReclaimsOnlyAtPollsAndBetweenActivations) {
  // Under TALOS_RT_EXECUTION_POLLED_COLLECTION compiled code registers its
  // managed values only at polls, so allocation must neither collect nor
  // park. A poll reclaims pending pressure, deferring to designated points
  // until pressure reaches the emergency multiple; so does a point between
  // activations.
  auto run = [](bool designate) {
    talos_rt_context *context = nullptr;
    ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
    talos_rt_managed_heap_poll_collections(context->managedHeap);
    ASSERT_EQ(talos_rt_v1_gc_set_threshold(context, UINT64_MAX),
              TALOS_RT_OK);
    auto statistics = [&] {
      talos_rt_gc_statistics_v1 result{};
      EXPECT_EQ(talos_rt_v1_gc_statistics(context, &result), TALOS_RT_OK);
      return result;
    };
    auto allocate = [&](talos_rt_gc_lane_v1 *lane, unsigned count) {
      for (unsigned index = 0; index != count; ++index) {
        talos_rt_object_v1 *object = nullptr;
        EXPECT_EQ(talos_rt_v1_object_allocate(lane, &nodeDescriptor, &object),
                  TALOS_RT_OK);
      }
    };

    talos_rt_gc_lane_v1 *lane = nullptr;
    bool entered = false;
    ASSERT_EQ(talos_rt_managed_execution_enter(context, &lane, &entered),
              TALOS_RT_OK);
    ASSERT_TRUE(entered);
    talos_rt_object_v1 *kept = nullptr;
    ASSERT_EQ(talos_rt_v1_object_allocate(lane, &nodeDescriptor, &kept),
              TALOS_RT_OK);
    uint64_t objectBytes = statistics().live_bytes;
    ASSERT_GT(objectBytes, 0u);
    uint64_t keptID = talos_rt_v1_object_id(kept);
    ASSERT_EQ(talos_rt_v1_gc_static_root_register(context, &kept),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_gc_set_threshold(context, objectBytes * 16),
              TALOS_RT_OK);
    if (designate)
      ASSERT_EQ(talos_rt_v1_gc_designate(context), TALOS_RT_OK);

    // Allocation never collects, even far past the emergency multiple.
    allocate(lane, 32);
    EXPECT_EQ(statistics().collection_count, 0u);
    // A poll reclaims what is due: everything without designated points,
    // only an emergency with them.
    EXPECT_EQ(talos_rt_v1_gc_safepoint(lane), TALOS_RT_OK);
    EXPECT_EQ(statistics().collection_count, designate ? 0u : 1u);
    allocate(lane, 80);
    EXPECT_EQ(statistics().collection_count, designate ? 0u : 1u);
    EXPECT_EQ(talos_rt_v1_gc_safepoint(lane), TALOS_RT_OK);
    EXPECT_EQ(statistics().collection_count, designate ? 1u : 2u);
    EXPECT_EQ(statistics().reclaimed_objects, 112u);

    // A point between activations does nothing while one runs.
    allocate(lane, 32);
    EXPECT_EQ(talos_rt_managed_heap_collect_between_activations(context),
              TALOS_RT_OK);
    EXPECT_EQ(statistics().collection_count, designate ? 1u : 2u);
    talos_rt_managed_execution_leave(lane, entered);
    // Between activations it reclaims what is due; a designated point
    // reclaims whatever is pending.
    EXPECT_EQ(talos_rt_managed_heap_collect_between_activations(context),
              TALOS_RT_OK);
    EXPECT_EQ(statistics().collection_count, designate ? 1u : 3u);
    EXPECT_EQ(talos_rt_v1_gc_collect_pending(context), TALOS_RT_OK);
    talos_rt_gc_statistics_v1 collected = statistics();
    EXPECT_EQ(collected.collection_count, designate ? 2u : 3u);
    EXPECT_EQ(collected.reclaimed_objects, 144u);
    EXPECT_EQ(talos_rt_v1_object_id(kept), keptID);
    ASSERT_EQ(talos_rt_v1_gc_static_root_unregister(context, &kept),
              TALOS_RT_OK);
    talos_rt_v1_context_destroy(context);
  };
  run(/*designate=*/false);
  run(/*designate=*/true);
}

TEST(ManagedHeap, PolledAllocationLeavesCollectorsWaitingForAPoll) {
  // Another thread requests a collection while this thread's activation
  // allocates. Under polled collection the activation registers its managed
  // values only at polls, so its allocations must not park for the collector:
  // the collector waits until the activation polls with its live values
  // registered. It then reclaims every unreachable object and keeps every
  // reachable one. Afterwards no object and no root registration outlives its
  // owner. Eager rooting registers the activation's values on entry and parks
  // at its first allocation instead.
  auto run = [](bool polled) {
    SCOPED_TRACE(polled ? "polled" : "eager");
    talos_rt_context *context = nullptr;
    ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
    if (polled)
      talos_rt_managed_heap_poll_collections(context->managedHeap);
    // Only the explicit request below collects.
    ASSERT_EQ(talos_rt_v1_gc_set_threshold(context, UINT64_MAX),
              TALOS_RT_OK);
    auto statistics = [&] {
      talos_rt_gc_statistics_v1 result{};
      EXPECT_EQ(talos_rt_v1_gc_statistics(context, &result), TALOS_RT_OK);
      return result;
    };
    talos_rt_gc_lane_v1 *activation = nullptr;
    talos_rt_gc_lane_v1 *collector = nullptr;
    ASSERT_EQ(talos_rt_v1_gc_lane_create(context, &activation),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_gc_lane_create(context, &collector), TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_gc_lane_enter(activation), TALOS_RT_OK);
    auto allocate = [&] {
      talos_rt_object_v1 *object = nullptr;
      EXPECT_EQ(
          talos_rt_v1_object_allocate(activation, &nodeDescriptor, &object),
          TALOS_RT_OK);
      return object;
    };
    auto link = [](talos_rt_object_v1 *from, talos_rt_object_v1 *to) {
      EXPECT_EQ(talos_rt_v1_object_field_store(from, kNodeLinkOffset, to),
                TALOS_RT_OK);
    };

    // Reachable: a static root, and a chain held only by the activation.
    talos_rt_object_v1 *kept = allocate();
    ASSERT_EQ(talos_rt_v1_gc_static_root_register(context, &kept),
              TALOS_RT_OK);
    talos_rt_object_v1 *head = allocate();
    talos_rt_object_v1 *middle = allocate();
    talos_rt_object_v1 *tail = allocate();
    link(head, middle);
    link(middle, tail);
    uint64_t tailID = talos_rt_v1_object_id(tail);
    // Unreachable once the activation drops it.
    link(allocate(), allocate());
    talos_rt_managed_word_v1 slots[]{
        talos_rt_managed_word_from_object(head)};
    talos_rt_gc_managed_root_range_v1 range{};
    auto registerChain = [&] {
      EXPECT_EQ(talos_rt_v1_gc_managed_root_range_push(activation, &range,
                                                          slots, 1),
                TALOS_RT_OK);
    };
    if (!polled)
      registerChain();

    std::atomic<talos_rt_status> collected{TALOS_RT_INVALID_LIFECYCLE};
    std::thread worker([&] {
      talos_rt_status status = talos_rt_v1_gc_lane_enter(collector);
      if (status == TALOS_RT_OK)
        status = talos_rt_v1_gc_collect(collector);
      if (status == TALOS_RT_OK)
        status = talos_rt_v1_gc_lane_leave(collector);
      collected.store(status);
    });
    while (!talos_rt_managed_heap_collection_requested(context->managedHeap))
      std::this_thread::yield();

    constexpr unsigned garbage = 64;
    if (polled) {
      // The collector keeps waiting through every allocation.
      for (unsigned index = 0; index != garbage; ++index)
        allocate();
      EXPECT_TRUE(
          talos_rt_managed_heap_collection_requested(context->managedHeap));
      EXPECT_EQ(statistics().collection_count, 0u);
      // Compiled code registers its live values at the poll.
      registerChain();
    } else {
      // Eager rooting parks at the allocation; the collector runs meanwhile.
      allocate();
      EXPECT_EQ(statistics().collection_count, 1u);
      EXPECT_EQ(statistics().reclaimed_objects, 2u);
      for (unsigned index = 1; index != garbage; ++index)
        allocate();
    }
    // A poll lets a waiting collector run.
    EXPECT_EQ(talos_rt_v1_gc_safepoint(activation), TALOS_RT_OK);
    worker.join();
    EXPECT_EQ(collected.load(), TALOS_RT_OK);
    talos_rt_gc_statistics_v1 afterPoll = statistics();
    EXPECT_EQ(afterPoll.collection_count, 1u);
    EXPECT_EQ(afterPoll.allocated_objects, 6u + garbage);
    EXPECT_EQ(afterPoll.reclaimed_objects, polled ? 2u + garbage : 2u);
    EXPECT_EQ(afterPoll.live_objects, polled ? 4u : 4u + garbage);
    // The registered chain survived intact.
    talos_rt_object_v1 *loaded = nullptr;
    ASSERT_EQ(talos_rt_v1_object_field_load(head, kNodeLinkOffset, &loaded),
              TALOS_RT_OK);
    EXPECT_EQ(loaded, middle);
    ASSERT_EQ(talos_rt_v1_object_field_load(middle, kNodeLinkOffset, &loaded),
              TALOS_RT_OK);
    EXPECT_EQ(talos_rt_v1_object_id(loaded), tailID);
    ASSERT_EQ(talos_rt_v1_gc_managed_root_range_pop(activation, &range),
              TALOS_RT_OK);

    // Once nothing holds them, every object is reclaimed: none leaks, and no
    // root registration outlives the activation that owned it.
    ASSERT_EQ(talos_rt_v1_gc_static_root_unregister(context, &kept),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_gc_collect(activation), TALOS_RT_OK);
    talos_rt_gc_statistics_v1 final = statistics();
    EXPECT_EQ(final.live_objects, 0u);
    EXPECT_EQ(final.live_bytes, 0u);
    EXPECT_EQ(final.reclaimed_objects, final.allocated_objects);
    EXPECT_EQ(talos_rt_v1_gc_lane_leave(activation), TALOS_RT_OK);
    EXPECT_EQ(talos_rt_v1_gc_lane_destroy(activation), TALOS_RT_OK);
    EXPECT_EQ(talos_rt_v1_gc_lane_destroy(collector), TALOS_RT_OK);
    talos_rt_v1_context_destroy(context);
  };
  run(/*polled=*/true);
  run(/*polled=*/false);
}

TEST(ManagedHeap, SerializesConcurrentCollectionRequestsWithoutDeadlock) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  constexpr size_t laneCount = 4;
  std::array<talos_rt_gc_lane_v1 *, laneCount> lanes{};
  for (auto &lane : lanes)
    ASSERT_EQ(talos_rt_v1_gc_lane_create(context, &lane), TALOS_RT_OK);

  std::atomic<size_t> ready{0};
  std::atomic<bool> start{false};
  std::array<talos_rt_status, laneCount> statuses{};
  std::vector<std::thread> workers;
  for (size_t index = 0; index != laneCount; ++index) {
    workers.emplace_back([&, index] {
      talos_rt_gc_lane_v1 *lane = lanes[index];
      statuses[index] = talos_rt_v1_gc_lane_enter(lane);
      ready.fetch_add(1);
      while (!start.load())
        std::this_thread::yield();
      if (statuses[index] == TALOS_RT_OK)
        statuses[index] = talos_rt_v1_gc_collect(lane);
      if (statuses[index] == TALOS_RT_OK)
        statuses[index] = talos_rt_v1_gc_lane_leave(lane);
    });
  }
  while (ready.load() != laneCount)
    std::this_thread::yield();
  start.store(true);
  for (std::thread &worker : workers)
    worker.join();

  for (talos_rt_status status : statuses)
    EXPECT_EQ(status, TALOS_RT_OK);
  talos_rt_gc_statistics_v1 statistics{};
  ASSERT_EQ(talos_rt_v1_gc_statistics(context, &statistics), TALOS_RT_OK);
  EXPECT_EQ(statistics.collection_count, laneCount);
  for (auto *lane : lanes)
    EXPECT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);
}

TEST(ManagedHeap, RejectsMalformedClassLayouts) {
  talos_rt_method_descriptor_v1 sparseMethods[]{nodeMethods[0],
                                                  nodeMethods[0]};
  sparseMethods[1].slot = UINT64_C(1) << 20;
  talos_rt_class_descriptor_v1 sparse = nodeDescriptor;
  sparse.methods = sparseMethods;
  sparse.method_count = std::size(sparseMethods);
  EXPECT_EQ(talos_rt_v1_class_validate(&sparse), TALOS_RT_OK);

  sparseMethods[0].flags = TALOS_RT_METHOD_PURE;
  EXPECT_EQ(talos_rt_v1_class_validate(&sparse), TALOS_RT_INVALID_DESIGN);

  talos_rt_class_descriptor_v1 malformed = nodeDescriptor;
  malformed.class_id = 0;
  EXPECT_EQ(talos_rt_v1_class_validate(&malformed),
            TALOS_RT_INVALID_DESIGN);
  malformed = nodeDescriptor;
  talos_rt_trace_entry_v1 badEntry = nodeTraceEntry;
  badEntry.offset = 1;
  talos_rt_trace_layout_v1 badLayout = nodeTraceLayout;
  badLayout.entries = &badEntry;
  malformed.layout = &badLayout;
  EXPECT_EQ(talos_rt_v1_class_validate(&malformed),
            TALOS_RT_INVALID_DESIGN);

  talos_rt_random_edge_v1 badRandomEdge = randomNodeEdge;
  badRandomEdge.handle_offset = kNodeValueOffset;
  talos_rt_random_layout_v1 badRandomLayout = randomNodeLayout;
  badRandomLayout.edges = &badRandomEdge;
  malformed = randomNodeDescriptor;
  malformed.random_layout = &badRandomLayout;
  EXPECT_EQ(talos_rt_v1_class_validate(&malformed),
            TALOS_RT_INVALID_DESIGN);
  badRandomEdge = randomNodeEdge;
  badRandomEdge.mode_mask = 3;
  EXPECT_EQ(talos_rt_v1_class_validate(&malformed),
            TALOS_RT_INVALID_DESIGN);
  talos_rt_random_variable_v1 badRandomVariable = randomNodeVariable;
  badRandomLayout = randomNodeLayout;
  badRandomLayout.variables = &badRandomVariable;
  malformed = randomNodeDescriptor;
  malformed.random_layout = &badRandomLayout;
  badRandomVariable.value_offset = kNodeLinkOffset;
  EXPECT_EQ(talos_rt_v1_class_validate(&malformed),
            TALOS_RT_INVALID_DESIGN);
  badRandomVariable = randomNodeVariable;
  badRandomVariable.mode_mask = randomNodeEdge.mode_mask;
  EXPECT_EQ(talos_rt_v1_class_validate(&malformed),
            TALOS_RT_INVALID_DESIGN);
  badRandomVariable = randomNodeVariable;
  badRandomVariable.randc_key_offset = kRandomNodeValueOffset;
  EXPECT_EQ(talos_rt_v1_class_validate(&malformed),
            TALOS_RT_INVALID_DESIGN);
  malformed = nodeDescriptor;
  malformed.flags = TALOS_RT_CLASS_ABSTRACT;
  EXPECT_EQ(talos_rt_v1_class_validate(&malformed), TALOS_RT_OK);
  malformed = nodeDescriptor;
  malformed.flags = TALOS_RT_CLASS_WEAK_WRAPPER;
  EXPECT_EQ(talos_rt_v1_class_validate(&malformed),
            TALOS_RT_INVALID_DESIGN);
  talos_rt_trace_entry_v1 ambiguousWeakEntries[] = {
      {8, 1, 8, TALOS_RT_TRACE_WEAK, TALOS_RT_MANAGED_SLOT_CLASS, nullptr},
      {8, 1, 8, TALOS_RT_TRACE_STRONG, TALOS_RT_MANAGED_SLOT_CLASS,
       nullptr}};
  talos_rt_trace_layout_v1 ambiguousWeakLayout{TALOS_RT_VERSION,   0, 16, 8,
                                                 ambiguousWeakEntries, 2};
  malformed.layout = &ambiguousWeakLayout;
  EXPECT_EQ(talos_rt_v1_class_validate(&malformed),
            TALOS_RT_INVALID_DESIGN);

  talos_rt_trace_entry_v1 duplicateEntries[] = {nodeTraceEntry,
                                                  nodeTraceEntry};
  talos_rt_trace_layout_v1 duplicateLayout{
      TALOS_RT_VERSION, 0,
      sizeof(void *) * 3, alignof(void *),
      duplicateEntries,   std::size(duplicateEntries)};
  malformed = nodeDescriptor;
  malformed.layout = &duplicateLayout;
  EXPECT_EQ(talos_rt_v1_class_validate(&malformed),
            TALOS_RT_INVALID_DESIGN);

  talos_rt_class_descriptor_v1 finalBase = nodeDescriptor;
  finalBase.flags = TALOS_RT_CLASS_FINAL;
  malformed = derivedDescriptor;
  malformed.base = &finalBase;
  EXPECT_EQ(talos_rt_v1_class_validate(&malformed),
            TALOS_RT_INVALID_DESIGN);

  malformed = derivedDescriptor;
  malformed.layout = nullptr;
  EXPECT_EQ(talos_rt_v1_class_validate(&malformed),
            TALOS_RT_INVALID_DESIGN);

  auto scalarBase = nodeDescriptor;
  scalarBase.layout = nullptr;
  auto intrudingLayout = derivedTraceLayout;
  intrudingLayout.entries = &nodeTraceEntry;
  malformed = derivedDescriptor;
  malformed.base = &scalarBase;
  malformed.layout = &intrudingLayout;
  EXPECT_EQ(talos_rt_v1_class_validate(&malformed), TALOS_RT_INVALID_DESIGN);

  talos_rt_method_descriptor_v1 pureMethod = nodeMethods[0];
  pureMethod.flags = TALOS_RT_METHOD_PURE;
  malformed = nodeDescriptor;
  malformed.methods = &pureMethod;
  EXPECT_EQ(talos_rt_v1_class_validate(&malformed),
            TALOS_RT_INVALID_DESIGN);
}

TEST(ManagedHeap, ValidatesDeepClassChainsAndRejectsAliasedIDs) {
  std::array<talos_rt_class_descriptor_v1, 32> classes{};
  for (size_t index = 0; index != classes.size(); ++index) {
    auto &descriptor = classes[index];
    descriptor.version = TALOS_RT_VERSION;
    descriptor.class_id = (UINT64_C(1) << 48) + (classes.size() - index) * 257;
    descriptor.instance_size = sizeof(void *);
    descriptor.instance_alignment = alignof(void *);
    descriptor.base = index ? &classes[index - 1] : nullptr;
  }
  EXPECT_EQ(talos_rt_v1_class_validate(&classes.back()), TALOS_RT_OK);
  uint64_t savedID = classes.back().class_id;
  classes.back().class_id = classes.front().class_id;
  EXPECT_EQ(talos_rt_v1_class_validate(&classes.back()),
            TALOS_RT_INVALID_DESIGN);
  classes.back().class_id = savedID;
  classes.front().base = &classes.back();
  EXPECT_EQ(talos_rt_v1_class_validate(&classes.back()),
            TALOS_RT_INVALID_DESIGN);
  classes.front().base = nullptr;

  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  // Registering the leaf first validates unregistered ancestors as well.
  EXPECT_EQ(talos_rt_v1_class_register(context, &classes.back()),
            TALOS_RT_OK);
  for (const auto &descriptor : classes) {
    EXPECT_EQ(talos_rt_v1_class_register(context, &descriptor),
              TALOS_RT_OK);
    EXPECT_EQ(talos_rt_v1_class_register(context, &descriptor),
              TALOS_RT_OK);
  }
  auto duplicate = classes.front();
  EXPECT_EQ(talos_rt_v1_class_register(context, &duplicate),
            TALOS_RT_INVALID_DESIGN);
  auto child = classes.back();
  child.class_id += 1;
  child.base = &duplicate;
  EXPECT_EQ(talos_rt_v1_class_register(context, &child),
            TALOS_RT_INVALID_ARGUMENT);
  talos_rt_v1_context_destroy(context);
}

TEST(ManagedHeap, ValidatesSharedNestedTraceLayoutsAndRejectsCycles) {
  talos_rt_trace_entry_v1 fields[] = {
      {8, 0, 1, TALOS_RT_TRACE_STRONG, TALOS_RT_MANAGED_SLOT_CLASS,
       nullptr},
      {0, 0, 1, TALOS_RT_TRACE_WEAK, TALOS_RT_MANAGED_SLOT_REFERENCE_PATH,
       nullptr}};
  talos_rt_trace_layout_v1 element{TALOS_RT_VERSION, 0, 16, 8, fields, 2};
  talos_rt_trace_entry_v1 entries[] = {
      {8, 16, 8, TALOS_RT_TRACE_EMBEDDED, TALOS_RT_MANAGED_SLOT_INVALID,
       &element},
      {16, 0, 1, TALOS_RT_TRACE_STRONG, TALOS_RT_MANAGED_SLOT_CLASS,
       nullptr}};
  talos_rt_trace_layout_v1 layout{TALOS_RT_VERSION, 0, 136, 8, entries, 1};
  auto descriptor = nodeDescriptor;
  descriptor.instance_size = layout.size;
  descriptor.layout = &layout;
  EXPECT_EQ(talos_rt_v1_class_validate(&descriptor), TALOS_RT_OK);
  // A direct slot aliases a slot reached through the repeated child layout.
  layout.entry_count = 2;
  EXPECT_EQ(talos_rt_v1_class_validate(&descriptor),
            TALOS_RT_INVALID_DESIGN);
  layout.entry_count = 1;
  element.alignment = 0;
  EXPECT_EQ(talos_rt_v1_class_validate(&descriptor),
            TALOS_RT_INVALID_DESIGN);
  element.alignment = 8;
  entries[0].stride = 8;
  EXPECT_EQ(talos_rt_v1_class_validate(&descriptor),
            TALOS_RT_INVALID_DESIGN);

  talos_rt_trace_entry_v1 cycleEntry{
      0,       0, 1, TALOS_RT_TRACE_EMBEDDED, TALOS_RT_MANAGED_SLOT_INVALID,
      &element};
  element.entries = &cycleEntry;
  element.entry_count = 1;
  entries[0].stride = 16;
  EXPECT_EQ(talos_rt_v1_class_validate(&descriptor),
            TALOS_RT_INVALID_DESIGN);
}

TEST(ManagedHeap, ValidatesDeepTraceLayoutsWithoutRecursiveValidation) {
  std::array<talos_rt_trace_layout_v1, 32> children{};
  std::array<talos_rt_trace_entry_v1, 32> entries{};
  for (size_t index = 0; index != children.size(); ++index) {
    entries[index] =
        index ? talos_rt_trace_entry_v1{8,
                                          0,
                                          1,
                                          TALOS_RT_TRACE_EMBEDDED,
                                          TALOS_RT_MANAGED_SLOT_INVALID,
                                          &children[index - 1]}
              : talos_rt_trace_entry_v1{0,
                                          8,
                                          8,
                                          TALOS_RT_TRACE_STRONG,
                                          TALOS_RT_MANAGED_SLOT_CLASS,
                                          nullptr};
    children[index] = {TALOS_RT_VERSION, 0, 64 + index * 8, 8,
                       &entries[index],    1};
  }
  talos_rt_trace_entry_v1 rootEntry{8,
                                      0,
                                      1,
                                      TALOS_RT_TRACE_EMBEDDED,
                                      TALOS_RT_MANAGED_SLOT_INVALID,
                                      &children.back()};
  talos_rt_trace_layout_v1 root{
      TALOS_RT_VERSION, 0, children.back().size + 8, 8, &rootEntry, 1};
  auto descriptor = nodeDescriptor;
  descriptor.instance_size = root.size;
  descriptor.layout = &root;
  EXPECT_EQ(talos_rt_v1_class_validate(&descriptor), TALOS_RT_OK);
  for (size_t index = 1; index != children.size(); ++index) {
    entries[index].offset = 0;
    children[index].size = 64;
  }
  root.size = 72;
  descriptor.instance_size = root.size;
  EXPECT_EQ(talos_rt_v1_class_validate(&descriptor), TALOS_RT_OK);
  entries.front() = {0,
                     0,
                     1,
                     TALOS_RT_TRACE_EMBEDDED,
                     TALOS_RT_MANAGED_SLOT_INVALID,
                     &children.back()};
  EXPECT_EQ(talos_rt_v1_class_validate(&descriptor),
            TALOS_RT_INVALID_DESIGN);
}

TEST(ManagedHeap, ValidatesUnorderedRandomStorageAndSharedModeWords) {
  constexpr size_t count = 16;
  constexpr uint64_t modeOffset = 8 + count * 8;
  constexpr uint64_t valueOffset = modeOffset + 8;
  std::array<talos_rt_random_edge_v1, count> edges{};
  std::array<talos_rt_random_variable_v1, count> variables{};
  for (size_t index = 0; index != count; ++index) {
    edges[index] = {8 + (count - index - 1) * 8, modeOffset,
                    UINT64_C(1) << index};
    auto &variable = variables[index];
    variable.value_offset = valueOffset + (count - index - 1) * 32;
    variable.mode_offset = modeOffset;
    variable.mode_mask = UINT64_C(1) << (count + index);
    variable.bit_width = 32;
    variable.flags = index % 2 ? 0 : TALOS_RT_RANDOM_VARIABLE_FOUR_STATE;
    variable.randc_key_offset = UINT64_MAX;
    variable.randc_position_offset = UINT64_MAX;
    if (index % 3 == 0) {
      variable.flags |= TALOS_RT_RANDOM_VARIABLE_RANDC;
      variable.randc_key_offset = variable.value_offset + 8;
      variable.randc_position_offset = variable.value_offset + 16;
    }
  }
  talos_rt_trace_entry_v1 entry{
      8,      8, count, TALOS_RT_TRACE_STRONG, TALOS_RT_MANAGED_SLOT_CLASS,
      nullptr};
  talos_rt_trace_layout_v1 trace{
      TALOS_RT_VERSION, 0, valueOffset + count * 32, 8, &entry, 1};
  talos_rt_random_layout_v1 random{
      TALOS_RT_VERSION, 0, edges.data(), count, variables.data(), count};
  auto descriptor = nodeDescriptor;
  descriptor.instance_size = trace.size;
  descriptor.layout = &trace;
  descriptor.random_layout = &random;
  EXPECT_EQ(talos_rt_v1_class_validate(&descriptor), TALOS_RT_OK);

  auto savedEdge = edges.back();
  edges.back().handle_offset = edges.front().handle_offset;
  EXPECT_EQ(talos_rt_v1_class_validate(&descriptor),
            TALOS_RT_INVALID_DESIGN);
  edges.back() = savedEdge;
  edges.back().mode_mask = variables.front().mode_mask;
  EXPECT_EQ(talos_rt_v1_class_validate(&descriptor),
            TALOS_RT_INVALID_DESIGN);
  edges.back() = savedEdge;
  auto savedVariable = variables.back();
  variables.back().value_offset = variables.front().value_offset + 4;
  // The value aliases the unknown plane of another four-state variable.
  EXPECT_EQ(talos_rt_v1_class_validate(&descriptor),
            TALOS_RT_INVALID_DESIGN);
  variables.back() = savedVariable;
  variables.back().value_offset = 10;
  EXPECT_EQ(talos_rt_v1_class_validate(&descriptor),
            TALOS_RT_INVALID_DESIGN);
  variables.back() = savedVariable;
  variables.back().randc_key_offset = variables.front().value_offset;
  EXPECT_EQ(talos_rt_v1_class_validate(&descriptor),
            TALOS_RT_INVALID_DESIGN);
  variables.back() = savedVariable;
  variables.back().mode_mask = variables.front().mode_mask;
  EXPECT_EQ(talos_rt_v1_class_validate(&descriptor),
            TALOS_RT_INVALID_DESIGN);
  variables.back() = savedVariable;
  variables.back().value_offset = UINT64_MAX - 2;
  EXPECT_EQ(talos_rt_v1_class_validate(&descriptor),
            TALOS_RT_INVALID_DESIGN);
  variables.back() = savedVariable;
  EXPECT_EQ(talos_rt_v1_class_validate(&descriptor), TALOS_RT_OK);
}

TEST(ManagedHeap, ChecksCachedBaseLayoutsAndRollsBackFailedRegistration) {
  talos_rt_context *context = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create(&context), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_class_register(context, &nodeDescriptor),
            TALOS_RT_OK);
  auto child = derivedDescriptor;
  auto entry = nodeTraceEntry;
  auto layout = derivedTraceLayout;
  layout.entries = &entry;
  child.layout = &layout;
  entry.offset = kDerivedExtraOffset;
  EXPECT_EQ(talos_rt_v1_class_register(context, &child),
            TALOS_RT_INVALID_ARGUMENT);
  entry = derivedTraceEntry;
  // Unregistered metadata may be repaired. A failed attempt must leave no
  // stale layout index in the cache or skip the base/derived boundary checks.
  EXPECT_EQ(talos_rt_v1_class_register(context, &child), TALOS_RT_OK);
  EXPECT_EQ(talos_rt_v1_class_register(context, &child), TALOS_RT_OK);
  talos_rt_v1_context_destroy(context);
}

TEST_F(ManagedHeapTest,
       ResolvesSparseInheritedDescriptorsAndInterfaceDiamonds) {
  constexpr size_t count = 32;
  const uint64_t signatures[]{42};
  const talos_rt_interface_descriptor_v1 root{501, signatures, 1, nullptr, 0};
  const talos_rt_interface_descriptor_v1 *rootBases[]{&root};
  const talos_rt_interface_descriptor_v1 left{502, nullptr, 0, rootBases, 1};
  const talos_rt_interface_descriptor_v1 right{503, nullptr, 0, rootBases, 1};
  const talos_rt_interface_descriptor_v1 *joinBases[]{&left, &right};
  const talos_rt_interface_descriptor_v1 join{504, nullptr, 0, joinBases, 2};
  std::array<talos_rt_class_descriptor_v1, count> classes{};
  std::array<talos_rt_trace_layout_v1, count> layouts{};
  std::array<std::array<talos_rt_trace_entry_v1, 2>, count> entries{};
  std::array<std::array<talos_rt_method_descriptor_v1, 2>, count> methods{};
  auto destroy = [&](talos_rt_context *) {
    TearDown();
    lane = nullptr;
    context = nullptr;
  };
  // Registered immutable metadata must outlive its context, including failures.
  std::unique_ptr<talos_rt_context, decltype(destroy)> cleanup(context,
                                                                 destroy);
  for (size_t index = 0; index != count; ++index) {
    methods[index][0] = nodeMethods[0];
    if (index) {
      methods[index][0].slot = (UINT64_C(1) << 20) + index;
      methods[index][0].signature_id = 100 + index;
      entries[index][0] = {0,
                           0,
                           1,
                           TALOS_RT_TRACE_EMBEDDED,
                           TALOS_RT_MANAGED_SLOT_INVALID,
                           &layouts[index - 1]};
      entries[index][1] = {
          kSlot * (index + 2),           0,      1, TALOS_RT_TRACE_STRONG,
          TALOS_RT_MANAGED_SLOT_CLASS, nullptr};
    } else {
      entries[index][0] = nodeTraceEntry;
    }
    layouts[index] = {TALOS_RT_VERSION,    0,
                      kSlot * (index + 3),   kSlotAlign,
                      entries[index].data(), index ? 2u : 1u};
    classes[index] = nodeDescriptor;
    classes[index].class_id = 1000 + index;
    classes[index].instance_size = layouts[index].size;
    classes[index].layout = &layouts[index];
    classes[index].base = index ? &classes[index - 1] : nullptr;
    classes[index].methods = methods[index].data();
    classes[index].interfaces = index ? nullptr : &join;
    classes[index].interface_count = index ? 0 : 1;
  }
  methods.back()[1] = methods.back()[0];
  methods.back()[0] = derivedMethods[0];
  classes.back().method_count = 2;
  EXPECT_EQ(talos_rt_v1_class_validate(&classes.back()), TALOS_RT_OK);
  methods.back()[0].signature_id = 43;
  EXPECT_EQ(talos_rt_v1_class_validate(&classes.back()),
            TALOS_RT_INVALID_DESIGN);
  methods.back()[0] = derivedMethods[0];
  methods.back()[0].flags = TALOS_RT_METHOD_TASK;
  EXPECT_EQ(talos_rt_v1_class_validate(&classes.back()),
            TALOS_RT_INVALID_DESIGN);
  methods.back()[0] = derivedMethods[0];

  ASSERT_EQ(talos_rt_v1_class_register(context, &classes.back()),
            TALOS_RT_OK);
  talos_rt_object_v1 *object = nullptr;
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &classes.back(), &object),
            TALOS_RT_OK);
  const talos_rt_method_descriptor_v1 *resolved = nullptr;
  ASSERT_EQ(talos_rt_v1_method_resolve(object, 0, 42, &resolved),
            TALOS_RT_OK);
  EXPECT_EQ(resolved, &methods.back()[0]);
  ASSERT_EQ(talos_rt_v1_method_resolve(object, (UINT64_C(1) << 20) + 1, 101,
                                         &resolved),
            TALOS_RT_OK);
  EXPECT_EQ(resolved, &methods[1][0]);
  ASSERT_EQ(
      talos_rt_v1_interface_method_resolve(object, 501, 0, 42, &resolved),
      TALOS_RT_OK);
  EXPECT_EQ(resolved, &methods.back()[0]);
  EXPECT_EQ(
      talos_rt_v1_interface_method_resolve(object, 501, 0, 43, &resolved),
      TALOS_RT_LAYOUT_MISMATCH);
  EXPECT_EQ(
      talos_rt_v1_method_resolve(object, UINT64_C(1) << 48, 42, &resolved),
      TALOS_RT_INVALID_HANDLE);
  for (const auto &ancestor : classes)
    EXPECT_EQ(talos_rt_v1_object_is_instance(object, &ancestor), 1u);
  auto interfaceType = classes.front();
  interfaceType.class_id = 501;
  interfaceType.flags = TALOS_RT_CLASS_ABSTRACT | TALOS_RT_CLASS_INTERFACE;
  EXPECT_EQ(talos_rt_v1_object_is_instance(object, &interfaceType), 1u);
}

TEST_F(ManagedHeapTest,
       CompletesInheritedInterfaceRequirementsAndRollsBackGraphs) {
  const uint64_t signatures[]{42};
  talos_rt_interface_descriptor_v1 interface{701, signatures, 1, nullptr, 0};
  auto base = nodeDescriptor;
  base.class_id = 702;
  base.flags = TALOS_RT_CLASS_ABSTRACT;
  base.interfaces = &interface;
  base.interface_count = 1;
  base.methods = nullptr;
  base.method_count = 0;
  auto child = derivedDescriptor;
  child.class_id = 703;
  child.base = &base;
  auto method = derivedMethods[0];
  auto destroy = [&](talos_rt_context *) {
    TearDown();
    lane = nullptr;
    context = nullptr;
  };
  std::unique_ptr<talos_rt_context, decltype(destroy)> cleanup(context,
                                                                 destroy);
  child.methods = &method;
  method.signature_id = 43;
  EXPECT_EQ(talos_rt_v1_class_register(context, &child),
            TALOS_RT_INVALID_ARGUMENT);
  method.signature_id = 42;
  const talos_rt_interface_descriptor_v1 *cycle[]{&interface};
  interface.bases = cycle;
  interface.base_count = 1;
  EXPECT_EQ(talos_rt_v1_class_register(context, &child),
            TALOS_RT_INVALID_ARGUMENT);
  interface.bases = nullptr;
  interface.base_count = 0;
  ASSERT_EQ(talos_rt_v1_class_register(context, &child), TALOS_RT_OK);
  talos_rt_object_v1 *object = nullptr;
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &child, &object),
            TALOS_RT_OK);
  const talos_rt_method_descriptor_v1 *resolved = nullptr;
  EXPECT_EQ(
      talos_rt_v1_interface_method_resolve(object, 701, 0, 42, &resolved),
      TALOS_RT_OK);
  EXPECT_EQ(resolved, &method);
}

TEST(ManagedHeap, ReusesInterfaceChecksAcrossSiblingMethodOverrides) {
  using Map = talos::runtime::DescriptorMap<uint64_t>;
  constexpr size_t count = 32;
  Map requirements, inherited;
  for (size_t index = 0; index != count; ++index) {
    uint64_t key = (UINT64_C(1) << 48) + index * 257;
    requirements = requirements.set(key, key);
    inherited = inherited.set(key, 1);
  }
  std::vector<Map> siblings;
  siblings.reserve(count);
  for (size_t index = 0; index != count; ++index)
    siblings.push_back(inherited.set((UINT64_C(1) << 48) + index * 257, 0));
  talos::runtime::DescriptorMatchCache cache;
  size_t comparisons = 0;
  auto implements = [&](uint64_t, uint64_t available) {
    ++comparisons;
    return available != 0;
  };
  EXPECT_EQ(requirements.countMatches(inherited, cache, implements), count);
  for (const Map &sibling : siblings)
    EXPECT_EQ(requirements.countMatches(sibling, cache, implements), count - 1);
  // Each sibling changes exactly one implementation. Unchanged inherited
  // methods are compared once across all classes, rather than once per class.
  EXPECT_EQ(comparisons, count * 2);
  cache.rollback();
  comparisons = 0;
  EXPECT_EQ(requirements.countMatches(inherited, cache, implements), count);
  EXPECT_EQ(comparisons, count);
}

TEST(ManagedHeap, SharesMergesAcrossGrowingInterfaceParents) {
  struct CountedValue {
    uint64_t value = 0;
    size_t *copies = nullptr;
    CountedValue() = default;
    CountedValue(uint64_t value, size_t *copies)
        : value(value), copies(copies) {}
    CountedValue(const CountedValue &other)
        : value(other.value), copies(other.copies) {
      if (copies)
        ++*copies;
    }
    CountedValue(CountedValue &&) = default;
    CountedValue &operator=(CountedValue &&) = default;
    bool operator==(const CountedValue &other) const {
      return value == other.value;
    }
  };
  using Map = talos::runtime::DescriptorMap<CountedValue>;
  for (size_t count : {32, 64}) {
    size_t copies = 0;
    Map left, right;
    Map::MergeCache cache;
    std::vector<Map> inventories;
    inventories.reserve(count);
    constexpr uint64_t first = UINT64_C(1) << 48;
    for (size_t index = 0; index != count; ++index) {
      left = left.set(first + index * 2, {index * 2 + 1, &copies});
      right = right.set(first + index * 2 + 1, {index * 2 + 2, &copies});
      inventories.push_back(left.merge(right, cache));
      EXPECT_EQ(inventories.back().size(), (index + 1) * 2);
    }
    // Two evolving parents previously took count^2 merge visits. Only the
    // changed bounded-width Patricia paths introduce new subproblems now.
    EXPECT_LE(cache.size(), count * 8);
    EXPECT_LE(copies, count * 4);
    for (size_t index = 0; index != count; ++index) {
      const auto *last = inventories[index].find(first + index * 2 + 1);
      ASSERT_NE(last, nullptr);
      EXPECT_EQ(last->value, index * 2 + 2);
      EXPECT_EQ(inventories[index].find(first + (index + 1) * 2), nullptr);
    }
    size_t entries = cache.size();
    size_t previousCopies = copies;
    EXPECT_EQ(left.merge(right, cache).size(), count * 2);
    EXPECT_EQ(cache.size(), entries);
    EXPECT_EQ(copies, previousCopies);
  }
}

TEST(ManagedHeap, RollsBackMergedInventoriesAndPreservesParentPrecedence) {
  using Map = talos::runtime::DescriptorMap<uint64_t>;
  Map left, right;
  left = left.set(1, 7).set(2, 9);
  right = right.set(1, 11).set(3, 13);
  Map::MergeCache cache;
  Map inventory = left.merge(right, cache);
  ASSERT_NE(inventory.find(1), nullptr);
  EXPECT_EQ(*inventory.find(1), 7u);
  EXPECT_EQ(inventory.size(), 3u);
  cache.commit();
  size_t committed = cache.size();
  Map reversed = right.merge(left, cache);
  ASSERT_NE(reversed.find(1), nullptr);
  EXPECT_EQ(*reversed.find(1), 11u);
  EXPECT_GT(cache.size(), committed);
  cache.rollback();
  EXPECT_EQ(cache.size(), committed);

  // A repaired unpublished contract must use its new value after rollback.
  right = right.set(1, 17);
  inventory = right.merge(left, cache);
  ASSERT_NE(inventory.find(1), nullptr);
  EXPECT_EQ(*inventory.find(1), 17u);
  cache.rollback();
  EXPECT_EQ(cache.size(), committed);
  inventory = left.merge(right, cache);
  EXPECT_EQ(*inventory.find(1), 7u);
}

TEST_F(ManagedHeapTest, RegistersGrowingInterfaceParentsAndRepairsFailedMerge) {
  constexpr size_t count = 8;
  using Interface = talos_rt_interface_descriptor_v1;
  std::array<Interface, count> left{}, right{}, joins{};
  std::array<std::array<const Interface *, 1>, count> leftBases{}, rightBases{};
  std::array<std::array<const Interface *, 2>, count> joinBases{};
  std::array<std::array<uint64_t, 2>, count> signatures{};
  std::array<std::array<talos_rt_method_descriptor_v1, 2>, count> methods{};
  std::array<talos_rt_class_descriptor_v1, count> classes{};
  auto destroy = [&](talos_rt_context *) {
    TearDown();
    lane = nullptr;
    context = nullptr;
  };
  std::unique_ptr<talos_rt_context, decltype(destroy)> cleanup(context,
                                                                 destroy);
  for (size_t index = 0; index != count; ++index) {
    signatures[index] = {2000 + index * 2, 2001 + index * 2};
    if (index) {
      leftBases[index][0] = &left[index - 1];
      rightBases[index][0] = &right[index - 1];
    }
    left[index] = {500 + index * 2, &signatures[index][0], 1,
                   index ? leftBases[index].data() : nullptr, index ? 1u : 0u};
    right[index] = {501 + index * 2, &signatures[index][1], 1,
                    index ? rightBases[index].data() : nullptr,
                    index ? 1u : 0u};
    joinBases[index] = {&left[index], &right[index]};
    joins[index] = {600 + index, nullptr, 0, joinBases[index].data(), 2};
    for (size_t side = 0; side != 2; ++side) {
      methods[index][side] = nodeMethods[0];
      methods[index][side].signature_id = signatures[index][side];
      methods[index][side].slot = index * 2 + side;
    }
    classes[index] = nodeDescriptor;
    classes[index].class_id = 700 + index;
    classes[index].base = index ? &classes[index - 1] : nullptr;
    classes[index].methods = methods[index].data();
    classes[index].method_count = 2;
    classes[index].interfaces = &joins[index];
    classes[index].interface_count = 1;
    if (index + 1 == count) {
      // Roll back both requirement and interface-binding merges, then repair
      // the still-unpublished method and retry against committed ancestors.
      methods[index][1].signature_id = 9000;
      EXPECT_EQ(talos_rt_v1_class_register(context, &classes[index]),
                TALOS_RT_INVALID_ARGUMENT);
      methods[index][1].signature_id = signatures[index][1];
    }
    ASSERT_EQ(talos_rt_v1_class_register(context, &classes[index]),
              TALOS_RT_OK);
  }
  talos_rt_object_v1 *object = nullptr;
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &classes.back(), &object),
            TALOS_RT_OK);
  const talos_rt_method_descriptor_v1 *resolved = nullptr;
  ASSERT_EQ(talos_rt_v1_interface_method_resolve(
                object, left.front().interface_id, 0, signatures.front()[0],
                &resolved),
            TALOS_RT_OK);
  EXPECT_EQ(resolved, &methods.front()[0]);
  ASSERT_EQ(talos_rt_v1_interface_method_resolve(
                object, right.back().interface_id, 0, signatures.back()[1],
                &resolved),
            TALOS_RT_OK);
  EXPECT_EQ(resolved, &methods.back()[1]);
  auto target = nodeDescriptor;
  target.flags = TALOS_RT_CLASS_ABSTRACT | TALOS_RT_CLASS_INTERFACE;
  for (const Interface &interface : joins) {
    target.class_id = interface.interface_id;
    EXPECT_EQ(talos_rt_v1_object_is_instance(object, &target), 1u);
  }
  target.class_id = 999;
  EXPECT_EQ(talos_rt_v1_object_is_instance(object, &target), 0u);
}

} // namespace
