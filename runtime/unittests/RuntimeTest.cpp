//===- RuntimeTest.cpp - Tests for the Obelisk native runtime -------------===//

#include "obelisk/Runtime/Runtime.h"
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

// Layout expectations follow the pointer width, exactly as the tables in
// runtime/lib/ABI.cpp do.
static constexpr size_t abiPtr(size_t wide, size_t narrow) {
  return sizeof(void *) == 8 ? wide : narrow;
}

TEST(RuntimeABI, StableScalarLayout) {
  EXPECT_EQ(sizeof(obelisk_rt_status), 4u);
  EXPECT_EQ(sizeof(obelisk_rt_arg_kind), 4u);
  EXPECT_EQ(sizeof(obelisk_rt_arg_flags), 4u);
  EXPECT_EQ(offsetof(obelisk_rt_arg_v1, kind), 0u);
  EXPECT_EQ(offsetof(obelisk_rt_arg_v1, flags), 4u);
  EXPECT_EQ(offsetof(obelisk_rt_arg_v1, size), 8u);
  EXPECT_EQ(sizeof(obelisk_rt_enum_arg_v1), abiPtr(40u, 32u));
  EXPECT_EQ(offsetof(obelisk_rt_enum_arg_v1, value), 16u);
  EXPECT_EQ(offsetof(obelisk_rt_enum_arg_v1, name), abiPtr(32u, 24u));
  EXPECT_EQ(sizeof(obelisk_rt_net_arg_v1), abiPtr(40u, 32u));
  EXPECT_EQ(offsetof(obelisk_rt_net_arg_v1, native_state), 12u);
  EXPECT_EQ(offsetof(obelisk_rt_net_arg_v1, value), 16u);
  EXPECT_EQ(offsetof(obelisk_rt_net_arg_v1, handle), abiPtr(32u, 24u));
  EXPECT_EQ(sizeof(obelisk_rt_raw_aggregate_arg_v1), 24u);
  EXPECT_EQ(offsetof(obelisk_rt_raw_aggregate_arg_v1, four_state), 16u);
  EXPECT_EQ(sizeof(obelisk_rt_activation_descriptor_v1), 24u);
  EXPECT_EQ(offsetof(obelisk_rt_activation_descriptor_v1, native_entry), 8u);
  EXPECT_EQ(offsetof(obelisk_rt_activation_descriptor_v1, bytecode_function),
            abiPtr(16u, 12u));
  EXPECT_EQ(sizeof(obelisk_rt_observer_capture_abi_v1), 8u);
  EXPECT_EQ(sizeof(obelisk_rt_observer_descriptor_v1), abiPtr(48u, 40u));
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
  const char *file = nullptr;
  int line = 0;
  EXPECT_EQ(svGetCallerInfo(&file, &line), 1);
  EXPECT_STREQ(file, "dpi_test.sv");
  EXPECT_EQ(line, 41);
  EXPECT_EQ(svPutUserData(initial, &observation.userKey, &observation), 0);
  EXPECT_EQ(svGetUserData(initial, &observation.userKey), &observation);

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

obelisk_rt_status observeNoncontextDpi(obelisk_rt_context *, uint32_t,
                                       const obelisk_rt_import_input_v1 *,
                                       uint32_t, obelisk_rt_import_output_v1 *,
                                       uint32_t, void *userData) {
  auto &calls = *static_cast<uint32_t *>(userData);
  ++calls;
  EXPECT_EQ(obelisk_rt_v1_dpi_current_context(), nullptr);
  EXPECT_EQ(svGetScope(), nullptr);
  const char *file = nullptr;
  int line = 0;
  EXPECT_EQ(svGetCallerInfo(&file, &line), 0);
  return OBELISK_RT_OK;
}

TEST(RuntimeDPI, NoncontextBoundaryHasNoActiveCallState) {
  obelisk_rt_execution_descriptor_v1 execution{OBELISK_RT_VERSION};
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  constexpr std::string_view name = "noncontext";
  uint32_t importID = obelisk_rt_v1_import_id(
      reinterpret_cast<const uint8_t *>(name.data()), name.size());
  uint32_t calls = 0;
  ASSERT_EQ(obelisk_rt_v1_context_register_import(context, importID,
                                                  observeNoncontextDpi, &calls),
            OBELISK_RT_OK);
  obelisk_rt_import_site_v1 site{
      OBELISK_RT_VERSION, 0, importID, 0, UINT64_MAX, nullptr, 0, 0, 0, 0};
  EXPECT_EQ(obelisk_rt_v1_import_call_noncontext(context, &site, nullptr, 0,
                                                 nullptr, 0),
            OBELISK_RT_OK);
  EXPECT_EQ(calls, 1u);
  site.flags = OBELISK_RT_IMPORT_CONTEXT;
  EXPECT_EQ(obelisk_rt_v1_import_call_noncontext(context, &site, nullptr, 0,
                                                 nullptr, 0),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(calls, 1u);
  obelisk_rt_v1_context_destroy(context);
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
  ASSERT_EQ(obelisk_rt_v1_dpi_aggregate_pack(&value, nullptr, 1, 8, 0, &result,
                                             1, 1, plan.data(), plan.size()),
            OBELISK_RT_OK);
  EXPECT_EQ(result, value);
}

struct DpiDisableObservation {
  bool acknowledge = false;
  bool tryExportAfterDisable = false;
  obelisk_rt_status result = OBELISK_RT_OK;
};

obelisk_rt_status observeDpiDisable(obelisk_rt_context *, uint32_t,
                                    const obelisk_rt_import_input_v1 *,
                                    uint32_t, obelisk_rt_import_output_v1 *,
                                    uint32_t, void *userData) {
  auto &observation = *static_cast<DpiDisableObservation *>(userData);
  EXPECT_EQ(svIsDisabledState(), 0);
  EXPECT_NE(activeDpiCall, nullptr);
  if (!activeDpiCall)
    return OBELISK_RT_FATAL;
  activeDpiCall->disabledState = true;
  EXPECT_EQ(svIsDisabledState(), 1);
  if (observation.tryExportAfterDisable) {
    EXPECT_EQ(obelisk_rt_v1_export_call(1, 1, nullptr, 0, nullptr, 0),
              OBELISK_RT_FATAL);
  }
  if (observation.acknowledge) {
    svAckDisabledState();
    EXPECT_EQ(svIsDisabledState(), 0);
  }
  return observation.result;
}

TEST(RuntimeDPI, EnforcesFunctionAndTaskDisableProtocol) {
  static constexpr char scopeName[] = "top";
  const obelisk_rt_dpi_scope_v1 scopes[] = {
      {0, UINT64_MAX, scopeName, sizeof(scopeName) - 1, -9, -12, 0},
  };
  obelisk_rt_execution_descriptor_v1 execution{
      OBELISK_RT_VERSION, 0,   0, nullptr, 0, nullptr, 0, 0, 0, scopes,
      std::size(scopes),  -12, 0,
  };
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  constexpr uint32_t importID = UINT32_C(0x12345678);
  DpiDisableObservation observation;
  ASSERT_EQ(obelisk_rt_v1_context_register_import(
                context, importID, observeDpiDisable, &observation),
            OBELISK_RT_OK);
  obelisk_rt_import_site_v1 site{OBELISK_RT_VERSION,
                                 OBELISK_RT_IMPORT_CONTEXT,
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
  EXPECT_EQ(obelisk_rt_v1_import_call(context, &site, nullptr, 0, nullptr, 0),
            OBELISK_RT_DPI_DISABLE_UNSUPPORTED);

  observation.acknowledge = false;
  observation.tryExportAfterDisable = false;
  EXPECT_EQ(obelisk_rt_v1_import_call(context, &site, nullptr, 0, nullptr, 0),
            OBELISK_RT_FATAL);

  site.flags |= OBELISK_RT_IMPORT_TASK;
  observation.result = OBELISK_RT_DPI_DISABLE_UNSUPPORTED;
  EXPECT_EQ(obelisk_rt_v1_import_call(context, &site, nullptr, 0, nullptr, 0),
            OBELISK_RT_DPI_DISABLE_UNSUPPORTED);

  observation.result = OBELISK_RT_OK;
  EXPECT_EQ(obelisk_rt_v1_import_call(context, &site, nullptr, 0, nullptr, 0),
            OBELISK_RT_FATAL);
  obelisk_rt_v1_context_destroy(context);
}

struct DpiRealObservation {
  uint32_t calls = 0;
};

obelisk_rt_status observeDpiRealCall(obelisk_rt_context *, uint32_t,
                                     const obelisk_rt_import_input_v1 *inputs,
                                     uint32_t inputCount,
                                     obelisk_rt_import_output_v1 *outputs,
                                     uint32_t outputCount, void *userData) {
  auto &observation = *static_cast<DpiRealObservation *>(userData);
  ++observation.calls;
  EXPECT_EQ(inputCount, 2u);
  EXPECT_EQ(outputCount, 2u);
  EXPECT_EQ(inputs[0].kind, OBELISK_RT_DBREG_REAL32);
  EXPECT_EQ(inputs[1].kind, OBELISK_RT_DBREG_REAL64);
  EXPECT_EQ(outputs[0].kind, OBELISK_RT_DBREG_REAL32);
  EXPECT_EQ(outputs[1].kind, OBELISK_RT_DBREG_REAL64);

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
  return OBELISK_RT_OK;
}

TEST(RuntimeDPI, MarshalsBinary32AndBinary64WithoutOverwritingNeighbors) {
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  DpiRealObservation observation;
  constexpr std::string_view name = "dpi_reals";
  uint32_t importID = obelisk_rt_v1_import_id(
      reinterpret_cast<const uint8_t *>(name.data()), name.size());
  ASSERT_EQ(obelisk_rt_v1_context_register_import(
                context, importID, observeDpiRealCall, &observation),
            OBELISK_RT_OK);

  alignas(8) float narrowInput = 1.25f;
  double wideInput = 2.5;
  const obelisk_rt_import_input_v1 inputs[] = {
      {OBELISK_RT_DBREG_REAL32, 0, 0, 32,
       reinterpret_cast<const uint64_t *>(&narrowInput), nullptr, 1},
      {OBELISK_RT_DBREG_REAL64, 0, 0, 64,
       reinterpret_cast<const uint64_t *>(&wideInput), nullptr, 1},
  };
  struct alignas(8) NarrowOutput {
    float value;
    uint32_t canary;
  } narrowOutput{99.0f, UINT32_C(0xdeadbeef)};
  double wideOutput = 99.0;
  obelisk_rt_import_output_v1 outputs[] = {
      {OBELISK_RT_DBREG_REAL32, 0, 0, 32,
       reinterpret_cast<uint64_t *>(&narrowOutput.value), nullptr, 1},
      {OBELISK_RT_DBREG_REAL64, 0, 0, 64,
       reinterpret_cast<uint64_t *>(&wideOutput), nullptr, 1},
  };
  obelisk_rt_import_site_v1 site{
      OBELISK_RT_VERSION, 0, importID, 0, UINT64_MAX, nullptr, 0, 0, 0, 0};
  EXPECT_EQ(obelisk_rt_v1_import_call(context, &site, inputs, 2, outputs, 2),
            OBELISK_RT_OK);
  EXPECT_EQ(observation.calls, 1u);
  EXPECT_FLOAT_EQ(narrowOutput.value, 1.75f);
  EXPECT_EQ(narrowOutput.canary, UINT32_C(0xdeadbeef));
  EXPECT_DOUBLE_EQ(wideOutput, 2.75);

  obelisk_rt_import_input_v1 invalid = inputs[0];
  invalid.flags = OBELISK_RT_DBREG_SIGNED;
  EXPECT_EQ(obelisk_rt_v1_import_call(context, &site, &invalid, 1, outputs, 2),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(observation.calls, 1u);
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

TEST(RuntimeDPI, AcceptsPositiveLegalTimeUnitExponents) {
  static constexpr char name[] = "top";
  const obelisk_rt_dpi_scope_v1 scope{0, UINT64_MAX, name, sizeof(name) - 1,
                                      2, -15,        0};
  const obelisk_rt_execution_descriptor_v1 execution{
      OBELISK_RT_VERSION, 0, 0, nullptr, 0, nullptr, 0, 0, 0, &scope, 1, -15, 0,
  };
  obelisk_rt_context *context = nullptr;
  EXPECT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  EXPECT_NE(context, nullptr);
  obelisk_rt_v1_context_destroy(context);
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
        ASSERT_EQ(status, OBELISK_RT_OK);
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
  EXPECT_EQ(unknownStatus, OBELISK_RT_OK);
  EXPECT_EQ(unknownOutput, "x z X Z");

  LogicValue negativeFiveBits("11101", true);
  LogicValue unsignedFiveBits("00011");
  auto [smallStatus, smallOutput] =
      format("%d|%0d|%d", {negativeFiveBits.arg(), negativeFiveBits.arg(),
                           unsignedFiveBits.arg()});
  EXPECT_EQ(smallStatus, OBELISK_RT_OK);
  EXPECT_EQ(smallOutput, " -3|-3| 3");

  // IEEE 1800-2017 21.2.1.3: the default %d field is wide enough for the
  // largest value the operand can hold, which for a signed operand is one
  // magnitude bit narrower than the operand itself plus room for the sign.
  LogicValue fourteenBits("00111111111111", true);
  LogicValue sixteenBits("0000001111111111", true);
  auto [signedWidthStatus, signedWidthOutput] =
      format("%d|%d", {fourteenBits.arg(), sixteenBits.arg()});
  EXPECT_EQ(signedWidthStatus, OBELISK_RT_OK);
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
  EXPECT_EQ(nonFiniteStatus, OBELISK_RT_OK);
  EXPECT_EQ(nonFiniteOutput, "inf|-inf|nan");

  // IEEE 1800-2017 6.12.2: converting an integral value to a real treats its
  // x and z bits as zero, so a real format still renders an unknown operand.
  LogicValue unknownReal("0000000000001x1z");
  LogicValue allUnknownReal("xxxxxxxx");
  auto [realStatus, realOutput] =
      format("%f|%0.1f", {unknownReal.arg(), allUnknownReal.arg()});
  EXPECT_EQ(realStatus, OBELISK_RT_OK);
  EXPECT_EQ(realOutput, "10.000000|0.0");

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

TEST_F(RuntimeTest, ScalesTimeInputThroughCurrentTimeFormat) {
  // Default input units are design precision (100 ps here), rounded to zero
  // fractional digits, then converted to the caller's 1 ns unit.
  EXPECT_DOUBLE_EQ(obelisk_rt_v1_time_scan_scale(context, 10.5, 10, -10), 1.1);

  ASSERT_EQ(obelisk_rt_v1_time_format(context, -9, 1, nullptr, 0, 0),
            OBELISK_RT_OK);
  EXPECT_DOUBLE_EQ(obelisk_rt_v1_time_scan_scale(context, 1.25, 10, -10), 1.3);
  EXPECT_DOUBLE_EQ(obelisk_rt_v1_time_scan_scale(context, -1.25, 10, -10),
                   -1.3);

  // Changing the design-global state takes effect at the next scan. Picosecond
  // input is finer than the same caller's 1 ns time unit.
  ASSERT_EQ(obelisk_rt_v1_time_format(context, -12, 0, nullptr, 0, 0),
            OBELISK_RT_OK);
  EXPECT_DOUBLE_EQ(obelisk_rt_v1_time_scan_scale(context, 1250.0, 10, -10),
                   1.25);
  EXPECT_TRUE(std::isinf(obelisk_rt_v1_time_scan_scale(
      context, std::numeric_limits<double>::infinity(), 10, -10)));
}

TEST_F(RuntimeTest, FormatsDefaultScalarStrengths) {
  LogicValue zero("0");
  LogicValue one("1");
  LogicValue unknown("x");
  LogicValue highz("z");
  auto [status, output] = format(
      "%v|%v|%v|%v", {zero.arg(), one.arg(), unknown.arg(), highz.arg()});
  EXPECT_EQ(status, OBELISK_RT_OK);
  EXPECT_EQ(output, "St0|St1|StX|HiZ");
}

TEST_F(RuntimeTest, StrengthInputAcceptsEveryCanonicalOutputField) {
  std::unordered_set<std::string> emittedFields;
  for (uint32_t strengths = 1; strengths != (uint32_t{1} << 15); ++strengths) {
    std::string field =
        obelisk_rt_format_strength_range(static_cast<uint16_t>(strengths));
    ASSERT_EQ(field.size(), 3u);
    char logic = 0;
    ASSERT_TRUE(
        obelisk_rt_parse_strength_field(field.data(), field.size(), logic))
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
            obelisk_rt_parse_strength_field(field.data(), field.size(), logic);
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
  EXPECT_EQ(status, OBELISK_RT_OK);
  EXPECT_EQ(output, "A B X x -3 5");
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

TEST_F(RuntimeTest, FormatsPaddedWidthsAndRealIntegerDefaults) {
  // IEEE 1800-2017 21.2.1.3: a field width is a plain decimal constant, so the
  // zero of `%04d` is part of it rather than a pad character. Decimal fields
  // pad with spaces; hexadecimal, octal, and binary fields pad with zeros.
  LogicValue value("0000000001100101");
  auto [padStatus, padOutput] =
      format("%04d|%08h|%0d|%6d",
             {value.arg(), value.arg(), value.arg(), value.arg()});
  EXPECT_EQ(padStatus, OBELISK_RT_OK);
  EXPECT_EQ(padOutput, " 101|00000065|101|   101");

  // The default field width of an integer format comes from the size of its
  // operand, which a real does not have. The standard leaves the case
  // undefined; rendering at the operand's own length keeps the field from
  // reporting the width of the integer the value was staged in.
  double real = 1.4;
  double wide = 20.0;
  auto [realStatus, realOutput] =
      format("R: %d|%d|%5d", {realArg(real), realArg(wide), realArg(wide)});
  EXPECT_EQ(realStatus, OBELISK_RT_OK);
  EXPECT_EQ(realOutput, "R: 1|20|   20");
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

TEST_F(RuntimeTest, EvalDisplayUsesSnapshotsWithoutMonitorOrSchedulerEffects) {
  LogicValue value("10xz");
  std::string formatString = "bits=%b";
  std::vector<obelisk_rt_arg_v1> items = {
      stringArg(formatString, OBELISK_RT_ARG_FORMAT_STRING), value.arg()};
  context->activeLogicalProcessToken = 17;
  context->monitorLogicalProcessToken = 17;
  context->monitorEnabled = false;
  context->monitorReported = true;
  context->monitorReport = "unchanged";
  testing::internal::CaptureStdout();
  EXPECT_EQ(obelisk_rt_v1_eval_display(context, 1, 1, OBELISK_RT_RADIX_DECIMAL,
                                       items.data(), items.size(), nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(testing::internal::GetCapturedStdout(), "bits=10xz\n");
  EXPECT_EQ(context->activeLogicalProcessToken, 17u);
  EXPECT_EQ(context->monitorLogicalProcessToken, 17u);
  EXPECT_EQ(context->monitorReport, "unchanged");
  EXPECT_FALSE(context->monitorEnabled);
  EXPECT_FALSE(context->schedulerFinishRequested);
}

TEST_F(RuntimeTest, EvalDisplayWritesDiagnosticSnapshotsToStandardError) {
  std::string diagnostic = "unique case warning";
  obelisk_rt_arg_v1 item = stringArg(diagnostic, OBELISK_RT_ARG_FORMAT_STRING);
  context->monitorEnabled = false;
  context->monitorReport = "unchanged";
  testing::internal::CaptureStderr();
  EXPECT_EQ(obelisk_rt_v1_eval_display(context, 0x80000002u, 1,
                                       OBELISK_RT_RADIX_DECIMAL, &item, 1,
                                       nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(testing::internal::GetCapturedStderr(), diagnostic + "\n");
  EXPECT_EQ(context->monitorReport, "unchanged");
  EXPECT_FALSE(context->schedulerFinishRequested);
}

TEST_F(RuntimeTest, EvalDisplayRejectsRuntimeOwnedArgumentsAndChannels) {
  EXPECT_EQ(obelisk_rt_v1_eval_display(context, 2, 0, OBELISK_RT_RADIX_DECIMAL,
                                       nullptr, 0, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  for (uint32_t kind :
       {OBELISK_RT_ARG_NET, OBELISK_RT_ARG_MANAGED_STRING,
        OBELISK_RT_ARG_MANAGED_CONTAINER, OBELISK_RT_ARG_MANAGED_OBJECT,
        OBELISK_RT_ARG_ENUM, OBELISK_RT_ARG_PROCESS}) {
    obelisk_rt_arg_v1 item{kind, 0, 0, nullptr, nullptr};
    EXPECT_EQ(obelisk_rt_v1_eval_display(
                  context, 1, 0, OBELISK_RT_RADIX_DECIMAL, &item, 1, nullptr),
              OBELISK_RT_INVALID_ARGUMENT);
  }
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

TEST_F(RuntimeTest, HierarchyScanMatchesPrefixWithoutConsumingAField) {
  TempDirectory temporary;
  std::filesystem::path path = temporary.file("hierarchy-scan.bin");
  { std::ofstream(path, std::ios::binary) << "tag=Q"; }
  uint32_t descriptor = open(path, "rb");

  obelisk_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(context, &lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);
  obelisk_rt_string_v1 field = 1;
  uint32_t ok = 0;
  uint32_t scanEOF = 1;
  ASSERT_EQ(obelisk_rt_v1_file_scan_field(context, lane, descriptor, 1, "tag=",
                                          4, 'M', 0, &field, &ok, &scanEOF),
            OBELISK_RT_OK);
  EXPECT_EQ(field, 0u);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(scanEOF, 0u);

  int64_t offset = -1;
  ASSERT_EQ(obelisk_rt_v1_file_tell(context, descriptor, &offset),
            OBELISK_RT_OK);
  EXPECT_EQ(offset, 4);
  uint8_t byte = 0;
  ASSERT_EQ(obelisk_rt_v1_file_getc(context, descriptor, &byte), OBELISK_RT_OK);
  EXPECT_EQ(byte, 'Q');
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);

  std::filesystem::path emptyPath = temporary.file("empty-hierarchy-scan.bin");
  { std::ofstream(emptyPath, std::ios::binary); }
  descriptor = open(emptyPath, "rb");
  field = 1;
  ok = 0;
  scanEOF = 1;
  ASSERT_EQ(obelisk_rt_v1_file_scan_field(context, lane, descriptor, 1, nullptr,
                                          0, 'm', 0, &field, &ok, &scanEOF),
            OBELISK_RT_OK);
  EXPECT_EQ(field, 0u);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(scanEOF, 0u);
  ASSERT_EQ(obelisk_rt_v1_file_tell(context, descriptor, &offset),
            OBELISK_RT_OK);
  EXPECT_EQ(offset, 0);
  EXPECT_EQ(obelisk_rt_v1_file_getc(context, descriptor, &byte),
            OBELISK_RT_EOF);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, FileScanAcceptsFourStateNumericFieldsExactly) {
  TempDirectory temporary;
  std::filesystem::path path = temporary.file("four-state-scan.txt");
  { std::ofstream(path, std::ios::binary) << "1x?zQ ?R +7"; }
  uint32_t descriptor = open(path, "rb");
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(context, &lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);

  auto expectField = [&](uint32_t specifier, std::string_view prefix,
                         std::string_view expected) {
    obelisk_rt_string_v1 field = 0;
    uint32_t ok = 0;
    uint32_t scanEOF = 1;
    ASSERT_EQ(obelisk_rt_v1_file_scan_field(
                  context, lane, descriptor, 1, prefix.data(), prefix.size(),
                  specifier, 0, &field, &ok, &scanEOF),
              OBELISK_RT_OK);
    EXPECT_EQ(ok, 1u);
    EXPECT_EQ(scanEOF, 0u);
    char scratch[8]{};
    const char *bytes = nullptr;
    uint64_t size = 0;
    ASSERT_EQ(obelisk_rt_v1_string_view(field, scratch, &bytes, &size),
              OBELISK_RT_OK);
    EXPECT_EQ(std::string_view(bytes, size), expected);
  };

  expectField('h', "", "1x?z");
  uint8_t byte = 0;
  ASSERT_EQ(obelisk_rt_v1_file_getc(context, descriptor, &byte), OBELISK_RT_OK);
  EXPECT_EQ(byte, 'Q');
  expectField('d', " ", "?");
  ASSERT_EQ(obelisk_rt_v1_file_getc(context, descriptor, &byte), OBELISK_RT_OK);
  EXPECT_EQ(byte, 'R');
  // A sign belongs to decimal, but Table 21-8 does not make it part of a
  // power-of-two field. The signed decimal field is consumed in full.
  expectField('d', " ", "+7");
  int64_t offset = -1;
  ASSERT_EQ(obelisk_rt_v1_file_tell(context, descriptor, &offset),
            OBELISK_RT_OK);
  EXPECT_EQ(offset, 11);

  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, DynamicScanPlansCacheAndInterpretSuppressionExactly) {
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(context, &lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);
  auto string = [&](std::string_view value) {
    obelisk_rt_string_v1 result = 0;
    EXPECT_EQ(
        obelisk_rt_v1_string_create(lane, value.data(), value.size(), &result),
        OBELISK_RT_OK);
    return result;
  };
  auto mask = [](std::string_view letters) {
    uint64_t result = 0;
    for (char letter : letters)
      result |= UINT64_C(1) << static_cast<unsigned>(letter - 'a');
    return result;
  };

  obelisk_rt_string_v1 input = string("tag=ab SKIP 7f!");
  obelisk_rt_string_v1 format = string("tag=%2s %*4s %2h!");
  EXPECT_EQ(context->dynamicScanState, nullptr);
  uint32_t cursor = 0;
  uint32_t planCursor = 0;
  uint32_t kind = 0;
  uint32_t ok = 0;
  obelisk_rt_string_v1 field = 0;
  ASSERT_EQ(obelisk_rt_v1_string_scan_dynamic(
                context, lane, input, cursor, format, planCursor, 1, 0,
                mask("scm"), 0, 0, &field, &cursor, &planCursor, &kind, &ok),
            OBELISK_RT_OK);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(cursor, 6u);
  EXPECT_EQ(planCursor, 1u);
  EXPECT_EQ(kind, OBELISK_RT_SCAN_DYNAMIC_TEXT);
  ASSERT_NE(context->dynamicScanState, nullptr);
  EXPECT_EQ(context->dynamicScanState->parseCount, 1u);
  EXPECT_EQ(context->dynamicScanState->contentCompareBytes, 0u);

  ASSERT_EQ(obelisk_rt_v1_string_scan_dynamic(
                context, lane, input, cursor, format, planCursor, 1, 0,
                mask("h"), 0, 0, &field, &cursor, &planCursor, &kind, &ok),
            OBELISK_RT_OK);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(cursor, 14u);
  EXPECT_EQ(planCursor, 3u);
  EXPECT_EQ(kind, OBELISK_RT_SCAN_DYNAMIC_LOGIC16);
  char scratch[8]{};
  const char *bytes = nullptr;
  uint64_t size = 0;
  ASSERT_EQ(obelisk_rt_v1_string_view(field, scratch, &bytes, &size),
            OBELISK_RT_OK);
  EXPECT_EQ(std::string_view(bytes, size), "7f");
  ASSERT_EQ(obelisk_rt_v1_string_scan_dynamic(
                context, lane, input, cursor, format, planCursor, 1, 1, 0, 0, 0,
                &field, &cursor, &planCursor, &kind, &ok),
            OBELISK_RT_OK);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(cursor, 15u);
  EXPECT_EQ(context->dynamicScanState->parseCount, 1u);

  // Equal bytes in a newly allocated immutable string reuse the same plan.
  obelisk_rt_string_v1 equalFormat = string("tag=%2s %*4s %2h!");
  std::shared_ptr<const DynamicScanPlan> plan;
  ASSERT_EQ(obelisk_rt_dynamic_scan_plan(context, equalFormat, plan),
            OBELISK_RT_OK);
  EXPECT_EQ(context->dynamicScanState->parseCount, 1u);
  EXPECT_EQ(context->dynamicScanState->contentCompareBytes,
            std::string_view("tag=%2s %*4s %2h!").size());

  // Heap strings are cached by immutable contents, not by handle. Mutating a
  // character produces a new string and therefore a distinct plan even when
  // both strings are longer than the inline representation.
  obelisk_rt_string_v1 decimalFormat = string("prefix=%d");
  ASSERT_EQ(obelisk_rt_dynamic_scan_plan(context, decimalFormat, plan),
            OBELISK_RT_OK);
  EXPECT_EQ(plan->conversions.front().specifier, 'd');
  obelisk_rt_string_v1 hexFormat = 0;
  ASSERT_EQ(obelisk_rt_v1_string_putc(lane, decimalFormat, 8, 'h', &hexFormat),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_dynamic_scan_plan(context, hexFormat, plan),
            OBELISK_RT_OK);
  EXPECT_EQ(plan->conversions.front().specifier, 'h');

  // The LRU stays bounded even when a runtime variable cycles through formats.
  for (unsigned index = 0; index != 9; ++index) {
    std::string spelling = "prefix" + std::to_string(index) + "=%d";
    ASSERT_EQ(obelisk_rt_dynamic_scan_plan(context, string(spelling), plan),
              OBELISK_RT_OK);
  }
  EXPECT_EQ(context->dynamicScanState->plans.size(), 8u);
  EXPECT_EQ(context->dynamicScanState->parseCount, 12u);

  // Concurrent callers may race cache hits and evictions, but the cache stays
  // coherent and within its fixed resident bound.
  std::vector<obelisk_rt_string_v1> concurrentFormats;
  for (unsigned index = 0; index != 12; ++index)
    concurrentFormats.push_back(
        string("concurrent" + std::to_string(index) + "=%d"));
  std::atomic<unsigned> failures{0};
  std::vector<std::thread> threads;
  for (unsigned thread = 0; thread != 4; ++thread) {
    threads.emplace_back([&, thread] {
      for (unsigned iteration = 0; iteration != 100; ++iteration) {
        std::shared_ptr<const DynamicScanPlan> concurrentPlan;
        obelisk_rt_status status = obelisk_rt_dynamic_scan_plan(
            context, concurrentFormats[(iteration + thread) % 12],
            concurrentPlan);
        if (status != OBELISK_RT_OK || !concurrentPlan ||
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
  ASSERT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, DynamicScanIdentityHitsDoNotWalkFormatBytes) {
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(context, &lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);
  std::string spelling;
  for (unsigned index = 0; index != 512; ++index)
    spelling += "%d ";
  obelisk_rt_string_v1 format = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, spelling.data(), spelling.size(),
                                        &format),
            OBELISK_RT_OK);
  std::shared_ptr<const DynamicScanPlan> plan;
  ASSERT_EQ(obelisk_rt_dynamic_scan_plan(context, format, plan), OBELISK_RT_OK);
  ASSERT_TRUE(plan);
  ASSERT_EQ(plan->conversions.size(), 512u);
  ASSERT_NE(context->dynamicScanState, nullptr);
  EXPECT_EQ(context->dynamicScanState->parseCount, 1u);
  EXPECT_EQ(context->dynamicScanState->contentCompareBytes, 0u);
  for (size_t ordinal = 0; ordinal != plan->conversions.size(); ++ordinal) {
    std::shared_ptr<const DynamicScanPlan> cached;
    ASSERT_EQ(obelisk_rt_dynamic_scan_plan(context, format, cached),
              OBELISK_RT_OK);
    ASSERT_EQ(cached.get(), plan.get());
  }
  EXPECT_EQ(context->dynamicScanState->parseCount, 1u);
  EXPECT_EQ(context->dynamicScanState->contentCompareBytes, 0u);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, DynamicRawScanUsesDestinationSpecificTransferSizes) {
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(context, &lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);
  auto string = [&](std::string_view value) {
    obelisk_rt_string_v1 result = 0;
    EXPECT_EQ(
        obelisk_rt_v1_string_create(lane, value.data(), value.size(), &result),
        OBELISK_RT_OK);
    return result;
  };
  auto view = [&](obelisk_rt_string_v1 value) {
    char scratch[8]{};
    const char *bytes = nullptr;
    uint64_t size = 0;
    EXPECT_EQ(obelisk_rt_v1_string_view(value, scratch, &bytes, &size),
              OBELISK_RT_OK);
    return std::string(bytes, static_cast<size_t>(size));
  };

  std::string raw("ABCD", 4);
  raw.append("\x01\x00\x00\x80\x00\x00\x00\x80", 8);
  obelisk_rt_string_v1 input = string(raw);
  obelisk_rt_string_v1 format = string("%u%z");
  uint64_t allowed =
      (UINT64_C(1) << ('u' - 'a')) | (UINT64_C(1) << ('z' - 'a'));
  obelisk_rt_string_v1 field = 0;
  uint32_t cursor = 0, planCursor = 0, kind = 0, ok = 0;
  ASSERT_EQ(obelisk_rt_v1_string_scan_dynamic(
                context, lane, input, cursor, format, planCursor, 1, 0, allowed,
                4, 8, &field, &cursor, &planCursor, &kind, &ok),
            OBELISK_RT_OK);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(cursor, 4u);
  EXPECT_EQ(planCursor, 1u);
  EXPECT_EQ(kind, OBELISK_RT_SCAN_DYNAMIC_RAW2);
  EXPECT_EQ(view(field), raw.substr(0, 4));

  ASSERT_EQ(obelisk_rt_v1_string_scan_dynamic(
                context, lane, input, cursor, format, planCursor, 1, 0, allowed,
                4, 8, &field, &cursor, &planCursor, &kind, &ok),
            OBELISK_RT_OK);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(cursor, 12u);
  EXPECT_EQ(planCursor, 2u);
  EXPECT_EQ(kind, OBELISK_RT_SCAN_DYNAMIC_RAW4);
  EXPECT_EQ(view(field), raw.substr(4));

  format = string("%3u");
  cursor = 0;
  planCursor = 0;
  ASSERT_EQ(obelisk_rt_v1_string_scan_dynamic(
                context, lane, input, cursor, format, planCursor, 1, 0, allowed,
                4, 8, &field, &cursor, &planCursor, &kind, &ok),
            OBELISK_RT_OK);
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
  obelisk_rt_string_v1 incompatible = string("%d %u");
  uint32_t validationCursor = 0;
  ASSERT_EQ(obelisk_rt_v1_scan_dynamic_validate(
                context, incompatible, validationCursor, 1, 0,
                UINT64_C(1) << ('d' - 'a'), &validationCursor),
            OBELISK_RT_OK);
  EXPECT_EQ(validationCursor, 1u);
  EXPECT_EQ(obelisk_rt_v1_scan_dynamic_validate(
                context, incompatible, validationCursor, 1, 0,
                UINT64_C(1) << ('s' - 'a'), &validationCursor),
            OBELISK_RT_INVALID_ARGUMENT);
  int64_t position = -1;
  EXPECT_EQ(obelisk_rt_v1_file_tell(context, descriptor, &position),
            OBELISK_RT_OK);
  EXPECT_EQ(position, 0);

  format = string("%z");
  planCursor = 0;
  uint32_t eof = 0;
  ASSERT_EQ(obelisk_rt_v1_file_scan_dynamic(
                context, lane, descriptor, format, planCursor, 1, 0, allowed, 4,
                8, &field, &planCursor, &kind, &ok, &eof),
            OBELISK_RT_OK);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(eof, 0u);
  EXPECT_EQ(planCursor, 1u);
  EXPECT_EQ(kind, OBELISK_RT_SCAN_DYNAMIC_RAW4);
  EXPECT_EQ(view(field), raw.substr(4));
  EXPECT_EQ(obelisk_rt_v1_file_tell(context, descriptor, &position),
            OBELISK_RT_OK);
  EXPECT_EQ(position, 8);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);

  ASSERT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, DynamicScanRejectsCrossContextLanesAndHeapStrings) {
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(context, &lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);
  auto create = [](obelisk_rt_gc_lane_v1 *owner, std::string_view value) {
    obelisk_rt_string_v1 result = 0;
    EXPECT_EQ(
        obelisk_rt_v1_string_create(owner, value.data(), value.size(), &result),
        OBELISK_RT_OK);
    return result;
  };
  obelisk_rt_string_v1 input = create(lane, "input-source");
  obelisk_rt_string_v1 format = create(lane, "value=%d");
  ASSERT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);

  obelisk_rt_context *otherContext = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&otherContext), OBELISK_RT_OK);
  obelisk_rt_gc_lane_v1 *otherLane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(otherContext, &otherLane),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(otherLane), OBELISK_RT_OK);
  obelisk_rt_string_v1 otherInput = create(otherLane, "other-input");
  obelisk_rt_string_v1 otherFormat = create(otherLane, "other=%d");
  ASSERT_EQ(obelisk_rt_v1_gc_lane_leave(otherLane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);

  obelisk_rt_string_v1 field = 0;
  uint32_t cursor = 0, planCursor = 0, kind = 0, ok = 0, eof = 0;
  uint64_t decimal = UINT64_C(1) << ('d' - 'a');
  EXPECT_EQ(obelisk_rt_v1_string_scan_dynamic(
                context, otherLane, input, 0, format, 0, 1, 0, decimal, 0, 0,
                &field, &cursor, &planCursor, &kind, &ok),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_string_scan_dynamic(
                context, lane, otherInput, 0, format, 0, 1, 0, decimal, 0, 0,
                &field, &cursor, &planCursor, &kind, &ok),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_string_scan_dynamic(
                context, lane, input, 0, otherFormat, 0, 1, 0, decimal, 0, 0,
                &field, &cursor, &planCursor, &kind, &ok),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_file_scan_dynamic(context, otherLane, 0, format, 0, 1,
                                            0, decimal, 0, 0, &field,
                                            &planCursor, &kind, &ok, &eof),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_scan_dynamic(context, lane, 0, otherFormat, 0, 1,
                                            0, decimal, 0, 0, &field,
                                            &planCursor, &kind, &ok, &eof),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_scan_dynamic_validate(context, otherFormat, 0, 0, 0,
                                                decimal, &planCursor),
            OBELISK_RT_INVALID_HANDLE);

  ASSERT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_destroy(otherLane), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(otherContext);
}

TEST_F(RuntimeTest, DynamicFileScanPreservesPositionAndEOF) {
  TempDirectory temporary;
  std::filesystem::path path = temporary.file("dynamic-scan.txt");
  { std::ofstream(path, std::ios::binary) << "A=12 SKIP 7f!"; }
  uint32_t descriptor = open(path, "rb");
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(context, &lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);
  auto string = [&](std::string_view value) {
    obelisk_rt_string_v1 result = 0;
    EXPECT_EQ(
        obelisk_rt_v1_string_create(lane, value.data(), value.size(), &result),
        OBELISK_RT_OK);
    return result;
  };
  obelisk_rt_string_v1 format = string("A=%2d %*4s %2h!");
  obelisk_rt_string_v1 field = 0;
  uint32_t planCursor = 0;
  uint32_t kind = 0;
  uint32_t ok = 0;
  uint32_t scanEOF = 0;
  uint64_t numeric =
      (UINT64_C(1) << ('d' - 'a')) | (UINT64_C(1) << ('h' - 'a'));
  ASSERT_EQ(obelisk_rt_v1_file_scan_dynamic(
                context, lane, descriptor, format, planCursor, 1, 0, numeric, 0,
                0, &field, &planCursor, &kind, &ok, &scanEOF),
            OBELISK_RT_OK);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(scanEOF, 0u);
  EXPECT_EQ(planCursor, 1u);
  EXPECT_EQ(kind, OBELISK_RT_SCAN_DYNAMIC_LOGIC10);
  int64_t position = -1;
  ASSERT_EQ(obelisk_rt_v1_file_tell(context, descriptor, &position),
            OBELISK_RT_OK);
  EXPECT_EQ(position, 4);

  ASSERT_EQ(obelisk_rt_v1_file_scan_dynamic(
                context, lane, descriptor, format, planCursor, 1, 0, numeric, 0,
                0, &field, &planCursor, &kind, &ok, &scanEOF),
            OBELISK_RT_OK);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(planCursor, 3u);
  EXPECT_EQ(kind, OBELISK_RT_SCAN_DYNAMIC_LOGIC16);
  ASSERT_EQ(obelisk_rt_v1_file_tell(context, descriptor, &position),
            OBELISK_RT_OK);
  EXPECT_EQ(position, 12);
  ASSERT_EQ(obelisk_rt_v1_file_scan_dynamic(context, lane, descriptor, format,
                                            planCursor, 1, 1, 0, 0, 0, &field,
                                            &planCursor, &kind, &ok, &scanEOF),
            OBELISK_RT_OK);
  EXPECT_EQ(ok, 1u);
  ASSERT_EQ(obelisk_rt_v1_file_tell(context, descriptor, &position),
            OBELISK_RT_OK);
  EXPECT_EQ(position, 13);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);

  std::filesystem::path empty = temporary.file("dynamic-empty.txt");
  { std::ofstream(empty, std::ios::binary); }
  descriptor = open(empty, "rb");
  format = string("%d");
  planCursor = 0;
  ASSERT_EQ(obelisk_rt_v1_file_scan_dynamic(
                context, lane, descriptor, format, planCursor, 1, 0, numeric, 0,
                0, &field, &planCursor, &kind, &ok, &scanEOF),
            OBELISK_RT_OK);
  EXPECT_EQ(ok, 0u);
  EXPECT_EQ(scanEOF, 1u);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
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
    ASSERT_EQ(obelisk_rt_v1_file_readmem_token(
                  context, descriptor, 16, width, value.data(), value.size(),
                  unknown.data(), unknown.size(), &kind, &address),
              OBELISK_RT_OK);
    EXPECT_EQ(kind, OBELISK_RT_READMEM_DATA);
    EXPECT_EQ(value[0], 0x0f);
    EXPECT_EQ(unknown[0], width == 5 ? 0x1f : 0xff);
    for (size_t byte = 1; byte != value.size(); ++byte) {
      EXPECT_EQ(value[byte], byte == 8 ? 0x01 : 0xff);
      EXPECT_EQ(unknown[byte], 0x00);
    }
  }
  uint8_t value = 0xff, unknown = 0xff;
  ASSERT_EQ(obelisk_rt_v1_file_readmem_token(context, descriptor, 2, 3, &value,
                                             1, &unknown, 1, &kind, &address),
            OBELISK_RT_OK);
  EXPECT_EQ(kind, OBELISK_RT_READMEM_DATA);
  EXPECT_EQ(value, 0x03);
  EXPECT_EQ(unknown, 0x06);
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
  EXPECT_EQ(obelisk_rt_v1_file_readmem_token(context, descriptor, 16, 8, &value,
                                             1, &unknown, 1, &kind, &address),
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
  ASSERT_EQ(
      obelisk_rt_v1_file_read(context, descriptor, bytes, sizeof(bytes), &read),
      OBELISK_RT_OK);
  EXPECT_EQ(read, 0u);
  RuntimeBuffer line;
  EXPECT_EQ(obelisk_rt_v1_file_getline(context, descriptor, 64, line.out()),
            OBELISK_RT_EOF);
  uint32_t isEOF = 0;
  ASSERT_EQ(obelisk_rt_v1_file_eof(context, descriptor, &isEOF), OBELISK_RT_OK);
  EXPECT_EQ(isEOF, 1u);

  obelisk_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(context, &lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);
  obelisk_rt_string_v1 field = 1;
  uint32_t ok = 1;
  uint32_t scanEOF = 0;
  EXPECT_EQ(obelisk_rt_v1_file_scan_field(context, lane, descriptor, 1, nullptr,
                                          0, 'd', 0, &field, &ok, &scanEOF),
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

// IEEE 1800-2017 21.3.4.3: $fscanf returns EOF (-1) when the input ends
// before the first conversion. A descriptor that was never opened -- $fopen
// returning 0 on a missing file is the ordinary way to get one -- has no
// input at all, so it reads as end of file. Returning an error status instead
// would end the simulation rather than the one system call, which is what
// every other read on an unusable descriptor already avoids.
TEST_F(RuntimeTest, ScansOnAnInvalidDescriptorReportEndOfFile) {
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(context, &lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);

  obelisk_rt_string_v1 field = 1;
  uint32_t ok = 1;
  uint32_t scanEOF = 0;
  EXPECT_EQ(obelisk_rt_v1_file_scan_field(context, lane, 0, 1, nullptr, 0, 'd',
                                          0, &field, &ok, &scanEOF),
            OBELISK_RT_OK);
  EXPECT_EQ(field, 0u);
  EXPECT_EQ(ok, 0u);
  EXPECT_EQ(scanEOF, 1u);

  // A disabled conversion still leaves the stream alone and reports nothing:
  // lowering stops issuing them once an earlier one failed.
  scanEOF = 1;
  EXPECT_EQ(obelisk_rt_v1_file_scan_field(context, lane, 0, 0, nullptr, 0, 'd',
                                          0, &field, &ok, &scanEOF),
            OBELISK_RT_OK);
  EXPECT_EQ(scanEOF, 0u);

  EXPECT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, FormattedScanReadErrorsAreInputFailures) {
  TempDirectory temporary;
  std::filesystem::path directory = temporary.file("scan-error");
  ASSERT_TRUE(std::filesystem::create_directory(directory));
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(context, &lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);

  auto expectErrorState = [&](uint32_t descriptor) {
    int64_t position = -1;
    EXPECT_EQ(obelisk_rt_v1_file_tell(context, descriptor, &position),
              OBELISK_RT_OK);
    EXPECT_EQ(position, 0);
    uint32_t eof = 1;
    EXPECT_EQ(obelisk_rt_v1_file_eof(context, descriptor, &eof), OBELISK_RT_OK);
    EXPECT_EQ(eof, 0u);
    int32_t code = 0;
    RuntimeBuffer message;
    EXPECT_EQ(
        obelisk_rt_v1_file_error(context, descriptor, &code, message.out()),
        OBELISK_RT_OK);
    EXPECT_NE(code, 0);
    EXPECT_FALSE(message.str().empty());
    EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);
  };

  obelisk_rt_string_v1 field = 1;
  uint32_t ok = 1;
  uint32_t inputFailure = 0;
  uint32_t descriptor = open(directory, "r");
  ASSERT_EQ(obelisk_rt_v1_file_scan_field(context, lane, descriptor, 1, nullptr,
                                          0, 'd', 0, &field, &ok,
                                          &inputFailure),
            OBELISK_RT_OK);
  EXPECT_EQ(field, 0u);
  EXPECT_EQ(ok, 0u);
  EXPECT_EQ(inputFailure, 1u);
  expectErrorState(descriptor);

  descriptor = open(directory, "r");
  uint32_t value = UINT32_MAX;
  uint32_t unknown = UINT32_MAX;
  ASSERT_EQ(obelisk_rt_v1_file_scan_raw(
                context, descriptor, 1, nullptr, 0, 4, 32, 0, 0, &value,
                sizeof(value), &unknown, sizeof(unknown), &ok, &inputFailure),
            OBELISK_RT_OK);
  EXPECT_EQ(value, 0u);
  EXPECT_EQ(unknown, 0u);
  EXPECT_EQ(ok, 0u);
  EXPECT_EQ(inputFailure, 1u);
  expectErrorState(descriptor);

  obelisk_rt_string_v1 format = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "%d", 2, &format), OBELISK_RT_OK);
  descriptor = open(directory, "r");
  uint32_t planCursor = 0;
  uint32_t kind = 0;
  ASSERT_EQ(obelisk_rt_v1_file_scan_dynamic(context, lane, descriptor, format,
                                            0, 1, 0, UINT64_C(1) << ('d' - 'a'),
                                            0, 0, &field, &planCursor, &kind,
                                            &ok, &inputFailure),
            OBELISK_RT_OK);
  EXPECT_EQ(field, 0u);
  EXPECT_EQ(planCursor, 0u);
  EXPECT_EQ(kind, 0u);
  EXPECT_EQ(ok, 0u);
  EXPECT_EQ(inputFailure, 1u);
  expectErrorState(descriptor);

  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "tail", 4, &format),
            OBELISK_RT_OK);
  descriptor = open(directory, "r");
  ASSERT_EQ(obelisk_rt_v1_file_scan_dynamic(
                context, lane, descriptor, format, 0, 1, 1, 0, 0, 0, &field,
                &planCursor, &kind, &ok, &inputFailure),
            OBELISK_RT_OK);
  EXPECT_EQ(field, 0u);
  EXPECT_EQ(planCursor, 0u);
  EXPECT_EQ(kind, 0u);
  EXPECT_EQ(ok, 0u);
  EXPECT_EQ(inputFailure, 1u);
  expectErrorState(descriptor);

  EXPECT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, HoldsOnePushedBackByteWithoutReadAccess) {
  TempDirectory temporary;
  std::filesystem::path path = temporary.file("pushback.txt");
  uint32_t descriptor = open(path, "w");

  // The runtime holds the byte itself: glibc accepts ungetc() on a write-only
  // stream and then crashes on the next write through it.
  ASSERT_EQ(obelisk_rt_v1_file_ungetc(context, descriptor, 'z'), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_file_ungetc(context, descriptor, 'y'),
            OBELISK_RT_EOF);
  uint32_t isEOF = 1;
  ASSERT_EQ(obelisk_rt_v1_file_eof(context, descriptor, &isEOF), OBELISK_RT_OK);
  EXPECT_EQ(isEOF, 0u);

  uint8_t byte = 0;
  ASSERT_EQ(obelisk_rt_v1_file_getc(context, descriptor, &byte), OBELISK_RT_OK);
  EXPECT_EQ(byte, 'z');
  EXPECT_EQ(obelisk_rt_v1_file_getc(context, descriptor, &byte),
            OBELISK_RT_EOF);
  ASSERT_EQ(obelisk_rt_v1_file_eof(context, descriptor, &isEOF), OBELISK_RT_OK);
  EXPECT_EQ(isEOF, 1u);

  uint64_t written = 0;
  ASSERT_EQ(obelisk_rt_v1_file_write(context, descriptor, "after", 5, &written),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);

  EXPECT_EQ(readHostFile(path), "after");
}

TEST_F(RuntimeTest, FormattedScansConsumeSyntheticPushback) {
  TempDirectory temporary;
  uint32_t descriptor = open(temporary.file("pushback-scan.bin"), "w");
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(context, &lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);

  auto expectField = [&](obelisk_rt_string_v1 field,
                         std::string_view expected) {
    char scratch[8]{};
    const char *bytes = nullptr;
    uint64_t size = 0;
    ASSERT_EQ(obelisk_rt_v1_string_view(field, scratch, &bytes, &size),
              OBELISK_RT_OK);
    EXPECT_EQ(std::string_view(bytes, size), expected);
  };
  auto expectPosition = [&] {
    int64_t offset = -1;
    ASSERT_EQ(obelisk_rt_v1_file_tell(context, descriptor, &offset),
              OBELISK_RT_OK);
    EXPECT_EQ(offset, 0);
  };
  auto expectEOF = [&](uint32_t expected) {
    uint32_t isEOF = 2;
    ASSERT_EQ(obelisk_rt_v1_file_eof(context, descriptor, &isEOF),
              OBELISK_RT_OK);
    EXPECT_EQ(isEOF, expected);
  };

  obelisk_rt_string_v1 field = 0;
  uint32_t ok = 0;
  uint32_t scanEOF = 0;
  ASSERT_EQ(obelisk_rt_v1_file_ungetc(context, descriptor, 'Q'), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_scan_field(context, lane, descriptor, 1, nullptr,
                                          0, 'c', 0, &field, &ok, &scanEOF),
            OBELISK_RT_OK);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(scanEOF, 0u);
  expectField(field, "Q");
  expectEOF(1);
  expectPosition();

  ASSERT_EQ(obelisk_rt_v1_file_ungetc(context, descriptor, 'S'), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_scan_field(context, lane, descriptor, 1, nullptr,
                                          0, 's', 0, &field, &ok, &scanEOF),
            OBELISK_RT_OK);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(scanEOF, 0u);
  expectField(field, "S");
  expectEOF(1);
  expectPosition();

  // A failed numeric conversion must restore the synthetic byte just as
  // ungetc() restores a byte on an ordinary readable stream.
  ASSERT_EQ(obelisk_rt_v1_file_ungetc(context, descriptor, 'Q'), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_scan_field(context, lane, descriptor, 1, nullptr,
                                          0, 'd', 0, &field, &ok, &scanEOF),
            OBELISK_RT_OK);
  EXPECT_EQ(ok, 0u);
  EXPECT_EQ(scanEOF, 0u);
  expectEOF(0);
  uint8_t byte = 0;
  ASSERT_EQ(obelisk_rt_v1_file_getc(context, descriptor, &byte), OBELISK_RT_OK);
  EXPECT_EQ(byte, 'Q');
  expectEOF(1);
  expectPosition();

  // Prefix mismatch also puts the byte back; a matching prefix followed by
  // zero-byte %m consumes it and can still succeed at synthetic EOF.
  ASSERT_EQ(obelisk_rt_v1_file_ungetc(context, descriptor, 'Q'), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_scan_field(context, lane, descriptor, 1, "X", 1,
                                          'm', 0, &field, &ok, &scanEOF),
            OBELISK_RT_OK);
  EXPECT_EQ(ok, 0u);
  expectEOF(0);
  ASSERT_EQ(obelisk_rt_v1_file_scan_field(context, lane, descriptor, 1, "Q", 1,
                                          'm', 0, &field, &ok, &scanEOF),
            OBELISK_RT_OK);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(scanEOF, 0u);
  expectEOF(1);
  expectPosition();

  // A suppressed raw field supplies its explicit byte count. A typed raw
  // conversion needs a full native word; consuming the one available byte
  // and then reaching EOF is a partial conversion, not a mismatch.
  ASSERT_EQ(obelisk_rt_v1_file_ungetc(context, descriptor, 'Q'), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_scan_raw(context, descriptor, 1, nullptr, 0, 1,
                                        0, 0, 1, nullptr, 0, nullptr, 0, &ok,
                                        &scanEOF),
            OBELISK_RT_OK);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(scanEOF, 0u);
  expectEOF(1);
  expectPosition();

  uint32_t value = UINT32_MAX;
  uint32_t unknown = UINT32_MAX;
  ASSERT_EQ(obelisk_rt_v1_file_ungetc(context, descriptor, 'Q'), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_scan_raw(
                context, descriptor, 1, nullptr, 0, 4, 32, 0, 0, &value,
                sizeof(value), &unknown, sizeof(unknown), &ok, &scanEOF),
            OBELISK_RT_OK);
  EXPECT_EQ(ok, 0u);
  EXPECT_EQ(scanEOF, 1u);
  EXPECT_EQ(value, 0u);
  EXPECT_EQ(unknown, 0u);
  expectEOF(1);
  expectPosition();

  EXPECT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);
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

TEST(RuntimeFragmentTest, ValidatesNativeDescriptorsAndActions) {
  obelisk_rt_fragment_descriptor_v1 descriptor{};
  descriptor.handle = {OBELISK_RT_DESCRIPTOR_FRAGMENT, 0, 3};
  descriptor.code_kind = OBELISK_RT_FRAGMENT_NATIVE;
  descriptor.code.native_entry = invalidNativeAction;
  obelisk_rt_fragment_action_v1 action{};
  EXPECT_EQ(obelisk_rt_v1_fragment_execute(&descriptor, nullptr, nullptr, 0, 0,
                                           &action),
            OBELISK_RT_INVALID_ARGUMENT);

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

const obelisk_rt_trace_entry_v1 nodeTraceEntry{kNodeLinkOffset,
                                               0,
                                               1,
                                               OBELISK_RT_TRACE_STRONG,
                                               OBELISK_RT_MANAGED_SLOT_CLASS,
                                               nullptr};
const obelisk_rt_trace_layout_v1 nodeTraceLayout{
    OBELISK_RT_VERSION, 0, kSlot * 3, kSlotAlign, &nodeTraceEntry, 1};
const obelisk_rt_trace_layout_v1 derivedTraceLayout{
    OBELISK_RT_VERSION, 0, kSlot * 4, kSlotAlign, &nodeTraceEntry, 1};

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

const obelisk_rt_method_descriptor_v1 nodeMethods[]{
    {42, 0, OBELISK_RT_METHOD_NO_BYTECODE, nodeValueMethod, nullptr}};
const obelisk_rt_method_descriptor_v1 derivedMethods[]{
    {42, 0, OBELISK_RT_METHOD_NO_BYTECODE, derivedValueMethod, nullptr}};
const char nodeName[] = "node";
const char derivedName[] = "derived_node";
const char throwingName[] = "throwing_node";
const obelisk_rt_class_descriptor_v1 nodeDescriptor{OBELISK_RT_VERSION,
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
                                                    nullptr};
const obelisk_rt_random_edge_v1 randomNodeEdge{kNodeLinkOffset,
                                               kNodeValueOffset, UINT64_C(2)};
constexpr uint64_t kRandomNodeValueOffset = kSlot * 3;
const obelisk_rt_random_variable_v1 randomNodeVariable{
    kRandomNodeValueOffset,
    kNodeValueOffset,
    UINT64_C(1),
    UINT64_MAX,
    UINT64_MAX,
    64,
    OBELISK_RT_RANDOM_VARIABLE_SIGNED};
const obelisk_rt_random_layout_v1 randomNodeLayout{
    OBELISK_RT_VERSION, 0, &randomNodeEdge, 1, &randomNodeVariable, 1};
const obelisk_rt_trace_layout_v1 randomNodeTraceLayout{
    OBELISK_RT_VERSION, 0, kSlot * 4, kSlotAlign, &nodeTraceEntry, 1};
const char randomNodeName[] = "random_node";
const obelisk_rt_class_descriptor_v1 randomNodeDescriptor{
    OBELISK_RT_VERSION,
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
    &randomNodeLayout};
const obelisk_rt_trace_entry_v1 randomDerivedTraceEntries[]{
    nodeTraceEntry,
    {kSlot * 4, 0, 1, OBELISK_RT_TRACE_STRONG, OBELISK_RT_MANAGED_SLOT_CLASS,
     nullptr}};
const obelisk_rt_trace_layout_v1 randomDerivedTraceLayout{
    OBELISK_RT_VERSION,
    0,
    kSlot * 6,
    kSlotAlign,
    randomDerivedTraceEntries,
    std::size(randomDerivedTraceEntries)};
const obelisk_rt_random_edge_v1 randomDerivedEdge{kSlot * 4, kNodeValueOffset,
                                                  UINT64_C(4)};
const obelisk_rt_random_variable_v1 randomDerivedVariable{
    kSlot * 5, kNodeValueOffset, UINT64_C(8), UINT64_MAX, UINT64_MAX, 32, 0};
const obelisk_rt_random_layout_v1 randomDerivedLayout{
    OBELISK_RT_VERSION, 0, &randomDerivedEdge, 1, &randomDerivedVariable, 1};
const char randomDerivedName[] = "random_derived_node";
const obelisk_rt_class_descriptor_v1 randomDerivedDescriptor{
    OBELISK_RT_VERSION,
    OBELISK_RT_CLASS_FINAL,
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
    &randomDerivedLayout};
const obelisk_rt_class_descriptor_v1 derivedDescriptor{
    OBELISK_RT_VERSION,
    OBELISK_RT_CLASS_FINAL,
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
    nullptr};
const obelisk_rt_class_descriptor_v1 throwingDescriptor{OBELISK_RT_VERSION,
                                                        OBELISK_RT_CLASS_FINAL,
                                                        5,
                                                        kSlot * 3,
                                                        kSlotAlign,
                                                        &nodeDescriptor,
                                                        nullptr,
                                                        0,
                                                        &nodeTraceLayout,
                                                        nodeMethods,
                                                        std::size(nodeMethods),
                                                        throwingName,
                                                        sizeof(throwingName) -
                                                            1,
                                                        nullptr};
// One handle slot followed by a 128-bit packed payload. Spelling the payload
// as two more pointers happened to be right at 64 bits and leaves the object
// too small at 32.
const obelisk_rt_trace_layout_v1 planeTraceLayout{
    OBELISK_RT_VERSION, 0, sizeof(void *) + 16, alignof(void *), nullptr, 0};
const char planeName[] = "plane_object";
const obelisk_rt_class_descriptor_v1 planeDescriptor{OBELISK_RT_VERSION,
                                                     OBELISK_RT_CLASS_FINAL,
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
                                                     nullptr};
const obelisk_rt_trace_entry_v1 weakTraceEntry{
    sizeof(obelisk_rt_managed_word_v1), 0,      1, OBELISK_RT_TRACE_WEAK,
    OBELISK_RT_MANAGED_SLOT_CLASS,      nullptr};
const obelisk_rt_trace_layout_v1 weakTraceLayout{
    OBELISK_RT_VERSION,
    0,
    sizeof(obelisk_rt_managed_word_v1) * 2,
    alignof(obelisk_rt_managed_word_v1),
    &weakTraceEntry,
    1};
const char weakName[] = "weak_reference";
const obelisk_rt_class_descriptor_v1 weakDescriptor{
    OBELISK_RT_VERSION,
    OBELISK_RT_CLASS_FINAL | OBELISK_RT_CLASS_WEAK_WRAPPER,
    4,
    sizeof(obelisk_rt_managed_word_v1) * 2,
    alignof(obelisk_rt_managed_word_v1),
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

TEST_F(ManagedHeapTest, PlusargsPreservePrefixOrderAndReplaceTheirIndex) {
  const char *arguments[] = {"sim", "+ABfirst", "+Asecond",
                             "+",   "+ABthird", "+case"};
  ASSERT_EQ(obelisk_rt_v1_context_configure_argv(
                context, static_cast<int>(std::size(arguments)), arguments),
            OBELISK_RT_OK);
  EXPECT_TRUE(context->vpiArguments.empty());

  auto query = [&](std::string_view prefix, std::string &tail,
                   uint32_t &found) {
    obelisk_rt_string_v1 prefixString = 0;
    EXPECT_EQ(obelisk_rt_v1_string_create(lane, prefix.data(), prefix.size(),
                                          &prefixString),
              OBELISK_RT_OK);
    obelisk_rt_string_v1 result = 0;
    EXPECT_EQ(obelisk_rt_v1_plusarg_value(context, lane, prefixString, &result,
                                          &found),
              OBELISK_RT_OK);
    char scratch[8] = {};
    const char *bytes = nullptr;
    uint64_t size = 0;
    EXPECT_EQ(obelisk_rt_v1_string_view(result, scratch, &bytes, &size),
              OBELISK_RT_OK);
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

  const char *replacement[] = {"sim", "+NEW=value",
                               "--coverage-output=kept.obcov",
                               "--coverage-test=kept",
                               "--coverage-tag=suite=kept"};
  ASSERT_EQ(obelisk_rt_v1_context_configure_argv(
                context, static_cast<int>(std::size(replacement)), replacement),
            OBELISK_RT_OK);
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

  obelisk_rt_random_state_v1 randomBefore{};
  ASSERT_EQ(obelisk_rt_v1_random_get_state(context, &randomBefore),
            OBELISK_RT_OK);
  const char *failedCoverageLoad[] = {
      "sim", "+BROKEN", "--seed=19", "--coverage-load=missing.obcov"};
  EXPECT_EQ(obelisk_rt_v1_context_configure_argv(
                context, static_cast<int>(std::size(failedCoverageLoad)),
                failedCoverageLoad),
            OBELISK_RT_IO_ERROR);
  obelisk_rt_random_state_v1 randomAfter{};
  ASSERT_EQ(obelisk_rt_v1_random_get_state(context, &randomAfter),
            OBELISK_RT_OK);
  EXPECT_EQ(randomAfter.state, randomBefore.state);
  EXPECT_EQ(randomAfter.increment, randomBefore.increment);
  query("NEW=", tail, found);
  EXPECT_EQ(found, 1u);
  EXPECT_EQ(tail, "value");

  // Parsing and merging all requested inputs is transactional as a group:
  // the first valid database must not leak into live state if a later static
  // schema is incompatible.
  TempDirectory temporary;
  obelisk::coverage::Database validDatabase;
  obelisk::coverage::Run validRun;
  validRun.uuid.back() = 1;
  validRun.name = "staged";
  validDatabase.runs.push_back(validRun);
  std::filesystem::path validPath = temporary.file("valid.obcov");
  ASSERT_EQ(obelisk::coverage::writeFileAtomically(validPath.string(),
                                                   validDatabase),
            obelisk::coverage::Status::Ok);
  obelisk::coverage::Database mismatchedDatabase;
  mismatchedDatabase.sourceFiles.push_back({1, "other.sv", {}});
  mismatchedDatabase.scopes.push_back({2, 0, "top", 0, "top"});
  mismatchedDatabase.linePoints.push_back(
      {3, 1, 1, 2, "", 1, 1, 1, 2, 0, 0});
  std::filesystem::path mismatchedPath = temporary.file("mismatch.obcov");
  ASSERT_EQ(obelisk::coverage::writeFileAtomically(mismatchedPath.string(),
                                                   mismatchedDatabase),
            obelisk::coverage::Status::Ok);
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
  EXPECT_EQ(obelisk_rt_v1_context_configure_argv(
                context, static_cast<int>(stagedArguments.size()),
                stagedArguments.data()),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(context->coverage->schema, nullptr);
  EXPECT_EQ(context->coverage->outputPath, "kept.obcov");
  EXPECT_EQ(context->coverage->testName, "kept");
  ASSERT_EQ(context->coverage->tags.size(), 1u);
  EXPECT_EQ(context->coverage->tags.front(),
            (std::pair<std::string, std::string>{"suite", "kept"}));
  obelisk_rt_random_state_v1 randomAfterStagedMerge{};
  ASSERT_EQ(obelisk_rt_v1_random_get_state(context, &randomAfterStagedMerge),
            OBELISK_RT_OK);
  EXPECT_EQ(randomAfterStagedMerge.state, randomBefore.state);
  EXPECT_EQ(randomAfterStagedMerge.increment, randomBefore.increment);
  query("NEW=", tail, found);
  EXPECT_EQ(found, 1u);
  EXPECT_EQ(tail, "value");

  const char *invalid[] = {"sim", "+BROKEN", "--seed=not-a-number"};
  EXPECT_EQ(obelisk_rt_v1_context_configure_argv(
                context, static_cast<int>(std::size(invalid)), invalid),
            OBELISK_RT_INVALID_ARGUMENT);
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
  ASSERT_EQ(obelisk_rt_v1_context_configure_argv(
                context, static_cast<int>(arguments.size()), arguments.data()),
            OBELISK_RT_OK);
  EXPECT_FALSE(context->plusargIndexBuilt);
  EXPECT_TRUE(context->plusargIndexNodes.empty());
  EXPECT_TRUE(context->plusargIndexEdges.empty());

  obelisk_rt_string_v1 prefix = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "ARG4095=", 8, &prefix),
            OBELISK_RT_OK);
  uint32_t found = 0;
  ASSERT_EQ(obelisk_rt_v1_plusarg_test(context, prefix, &found), OBELISK_RT_OK);
  EXPECT_EQ(found, 1u);
  EXPECT_TRUE(context->plusargIndexBuilt);
  EXPECT_FALSE(context->plusargIndexNodes.empty());

  const char *empty[] = {"sim"};
  ASSERT_EQ(obelisk_rt_v1_context_configure_argv(context, 1, empty),
            OBELISK_RT_OK);
  EXPECT_FALSE(context->plusargIndexBuilt);
  EXPECT_TRUE(context->plusargIndexNodes.empty());
  ASSERT_EQ(obelisk_rt_v1_plusarg_test(context, prefix, &found), OBELISK_RT_OK);
  EXPECT_EQ(found, 0u);
  EXPECT_TRUE(context->plusargIndexBuilt);
  EXPECT_TRUE(context->plusargIndexNodes.empty());
  EXPECT_TRUE(context->plusargIndexEdges.empty());
}

TEST_F(ManagedHeapTest, PlusargConversionsAreStrictWideAndFourState) {
  auto parse = [&](std::string_view spelling, uint32_t radix, uint64_t width,
                   std::vector<uint8_t> &value, std::vector<uint8_t> &unknown) {
    obelisk_rt_string_v1 string = 0;
    EXPECT_EQ(obelisk_rt_v1_string_create(lane, spelling.data(),
                                          spelling.size(), &string),
              OBELISK_RT_OK);
    uint64_t bytes = (width + 7) / 8;
    value.assign(static_cast<size_t>(bytes), 0xcc);
    unknown.assign(static_cast<size_t>(bytes), 0xcc);
    EXPECT_EQ(obelisk_rt_v1_plusarg_parse_logic(string, radix, width,
                                                value.data(), value.size(),
                                                unknown.data(), unknown.size()),
              OBELISK_RT_OK);
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

  obelisk_rt_string_v1 realString = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "1.25junk", 8, &realString),
            OBELISK_RT_OK);
  double real = 7.0;
  EXPECT_EQ(obelisk_rt_v1_plusarg_parse_real(realString, &real), OBELISK_RT_OK);
  EXPECT_EQ(real, 0.0);
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, " \t+1.25e+2", 10, &realString),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_plusarg_parse_real(realString, &real), OBELISK_RT_OK);
  EXPECT_EQ(real, 125.0);
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "1.25e", 5, &realString),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_plusarg_parse_real(realString, &real), OBELISK_RT_OK);
  EXPECT_EQ(real, 0.0);
}

TEST_F(ManagedHeapTest, NarrowBitInsertPreservesPackedStorageAndHandles) {
  obelisk_rt_object_v1 *object = nullptr;
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &planeDescriptor, &object),
            OBELISK_RT_OK);

  std::array<uint8_t, 16> bytes{};
  ASSERT_EQ(obelisk_rt_v1_object_bits_insert(object, sizeof(void *), 128, 4, 1,
                                             UINT64_C(0xa5), 8),
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
  ASSERT_EQ(obelisk_rt_v1_object_bits_insert(object, sizeof(void *), 128, -3, 1,
                                             UINT64_MAX, 8),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_read(object, sizeof(void *), bytes.data(),
                                      bytes.size()),
            OBELISK_RT_OK);
  EXPECT_EQ(bytes[0], 0x1f);

  std::array<uint8_t, 16> before = bytes;
  EXPECT_EQ(obelisk_rt_v1_object_bits_insert(object, sizeof(void *), 128,
                                             INT64_MAX, 0, 0, 8),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_read(object, sizeof(void *), bytes.data(),
                                      bytes.size()),
            OBELISK_RT_OK);
  EXPECT_EQ(bytes, before);

  obelisk_rt_object_v1 *node = nullptr;
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &nodeDescriptor, &node),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_object_bits_insert(node, kNodeLinkOffset, 64, 0, 1,
                                             UINT64_C(1), 1),
            OBELISK_RT_INVALID_ARGUMENT);
}

// IEEE 1800-2017 10.6: force has priority over procedural assign, and release
// exposes the still-active assign value. Managed shadow storage must therefore
// remain a precise GC root while it is hidden beneath force.
TEST_F(ManagedHeapTest, PropertyOverrideKeepsHiddenManagedValueRooted) {
  obelisk_rt_object_v1 *holder = nullptr;
  obelisk_rt_object_v1 *assigned = nullptr;
  obelisk_rt_object_v1 *forced = nullptr;
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &nodeDescriptor, &holder),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &nodeDescriptor, &assigned),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &nodeDescriptor, &forced),
            OBELISK_RT_OK);

  obelisk_rt_gc_root_v1 holderRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &holderRoot, &holder),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_override(holder, kNodeLinkOffset,
                                          sizeof(assigned), 0, 1, 0, 0, 0,
                                          &assigned, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_override(holder, kNodeLinkOffset,
                                          sizeof(forced), 0, 0, 0, 0, 0,
                                          &forced, nullptr),
            OBELISK_RT_OK);

  // Ordinary writes are masked while either layer is active.
  ASSERT_EQ(obelisk_rt_v1_object_field_store(holder, kNodeLinkOffset, nullptr),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *visible = nullptr;
  ASSERT_EQ(obelisk_rt_v1_object_field_load(holder, kNodeLinkOffset, &visible),
            OBELISK_RT_OK);
  EXPECT_EQ(visible, forced);

  assigned = nullptr;
  forced = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_release_override(holder, kNodeLinkOffset,
                                                  sizeof(visible), 0, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_field_load(holder, kNodeLinkOffset, &visible),
            OBELISK_RT_OK);
  EXPECT_TRUE(obelisk_rt_v1_object_is_instance(visible, &nodeDescriptor));

  ASSERT_EQ(obelisk_rt_v1_object_release_override(holder, kNodeLinkOffset,
                                                  sizeof(visible), 0, 1),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &holderRoot), OBELISK_RT_OK);
}

TEST(ManagedHeap, StaticOverrideKeepsHiddenManagedValueRooted) {
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 64;
  obelisk_rt_context *context = nullptr;
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(context, &lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 64),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_design_root_register(context, 0), OBELISK_RT_OK);
  uint64_t target = obelisk_rt_v1_native_state_static_handle(1);
  ASSERT_NE(target, UINT64_MAX);

  obelisk_rt_object_v1 *assigned = nullptr;
  obelisk_rt_object_v1 *forced = nullptr;
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &nodeDescriptor, &assigned),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &nodeDescriptor, &forced),
            OBELISK_RT_OK);
  uint64_t globalValue = 0;
  uint64_t globalUnknown = 0;
  ASSERT_EQ(obelisk_rt_v1_native_override(
                context, reinterpret_cast<uint8_t *>(&globalValue),
                reinterpret_cast<uint8_t *>(&globalUnknown), 64, target, 64,
                OBELISK_RT_DESCRIPTOR_STORAGE, 1,
                reinterpret_cast<const uint8_t *>(&assigned), nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_override(
                context, reinterpret_cast<uint8_t *>(&globalValue),
                reinterpret_cast<uint8_t *>(&globalUnknown), 64, target, 64,
                OBELISK_RT_DESCRIPTOR_STORAGE, 0,
                reinterpret_cast<const uint8_t *>(&forced), nullptr),
            OBELISK_RT_OK);

  assigned = nullptr;
  forced = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_release_override(
                context, reinterpret_cast<uint8_t *>(&globalValue),
                reinterpret_cast<uint8_t *>(&globalUnknown), 64, target, 64,
                OBELISK_RT_DESCRIPTOR_STORAGE, 0),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *visible = nullptr;
  std::memcpy(&visible, &globalValue, sizeof(visible));
  EXPECT_TRUE(obelisk_rt_v1_object_is_instance(visible, &nodeDescriptor));

  EXPECT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(RuntimeTest, SchedulerStatusReportNamesTheFailure) {
  // A standalone simulator returns the scheduler's status and exits, so a run
  // that ended on a runtime error has nowhere else to say what happened. A
  // successful run and a $fatal -- which printed its own message -- stay
  // quiet.
  testing::internal::CaptureStderr();
  obelisk_rt_v1_scheduler_report_status(context, OBELISK_RT_OK);
  obelisk_rt_v1_scheduler_report_status(context, OBELISK_RT_FATAL);
  EXPECT_EQ(testing::internal::GetCapturedStderr(), "");

  uint32_t closed = open(TempDirectory().file("reported.log"), "w");
  ASSERT_EQ(obelisk_rt_v1_file_close(context, closed), OBELISK_RT_OK);
  uint64_t written = 0;
  char byte = 'x';
  ASSERT_EQ(obelisk_rt_v1_file_write(context, closed, &byte, 1, &written),
            OBELISK_RT_INVALID_HANDLE);
  testing::internal::CaptureStderr();
  obelisk_rt_v1_scheduler_report_status(context, OBELISK_RT_IO_ERROR);
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
  ASSERT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);
  testing::internal::CaptureStderr();
  obelisk_rt_arg_v1 item = stringArg("dropped");
  EXPECT_EQ(obelisk_rt_v1_display(context, descriptor, 1,
                                  OBELISK_RT_RADIX_DECIMAL, &item, 1, nullptr),
            OBELISK_RT_OK);
  std::string warnings = testing::internal::GetCapturedStderr();
  EXPECT_NE(warnings.find("output descriptor"), std::string::npos);
}

TEST_F(RuntimeTest, DesignatedFormatContinuesWithUnformattedArguments) {
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
  EXPECT_EQ(warnings.find("extra argument"), std::string::npos);

  ASSERT_EQ(obelisk_rt_v1_file_flush(context, descriptor), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_rewind(context, descriptor), OBELISK_RT_OK);
  char bytes[32]{};
  uint64_t read = 0;
  ASSERT_EQ(
      obelisk_rt_v1_file_read(context, descriptor, bytes, sizeof(bytes), &read),
      OBELISK_RT_OK);
  EXPECT_EQ(std::string(bytes, static_cast<size_t>(read)), "<%s>kept2");
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
  EXPECT_EQ(output.str(), "class@" +
                              std::to_string(obelisk_rt_v1_object_id(object)) +
                              "|null");

  obelisk_rt_string_v1 string = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "not-a-class", 11, &string),
            OBELISK_RT_OK);
  ASSERT_EQ(string & UINT64_C(3), UINT64_C(0));
  auto *notClass =
      reinterpret_cast<obelisk_rt_object_v1 *>(static_cast<uintptr_t>(string));
  const obelisk_rt_arg_v1 invalid[] = {
      {OBELISK_RT_ARG_MANAGED_OBJECT, 0, 0, &notClass, nullptr}};
  EXPECT_EQ(obelisk_rt_v1_format(context, "%p", 2, invalid, std::size(invalid),
                                 &environment, output.out()),
            OBELISK_RT_INVALID_HANDLE);
}

TEST_F(ManagedHeapTest, FormatsEnumsWithNamesAndPackedFallbacks) {
  obelisk_rt_string_v1 name = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "ELARGE", 6, &name),
            OBELISK_RT_OK);
  uint64_t value = UINT64_C(0xf00d);
  obelisk_rt_enum_arg_v1 enumeration{
      32, OBELISK_RT_ARG_SIGNED, 0, &value, nullptr, name};
  obelisk_rt_arg_v1 argument{OBELISK_RT_ARG_ENUM, OBELISK_RT_ARG_SIGNED, 0,
                             &enumeration, nullptr};
  const obelisk_rt_arg_v1 named[] = {argument, argument, argument};
  RuntimeBuffer output;
  ASSERT_EQ(obelisk_rt_v1_format(context, "%p|%0h|%s", 9, named,
                                 std::size(named), nullptr, output.out()),
            OBELISK_RT_OK);
  EXPECT_EQ(output.str(), "ELARGE|f00d|ELARGE");

  enumeration.name = 0;
  value = 17;
  RuntimeBuffer fallbackOutput;
  ASSERT_EQ(obelisk_rt_v1_format(context, "%p", 2, &argument, 1, nullptr,
                                 fallbackOutput.out()),
            OBELISK_RT_OK);
  EXPECT_EQ(fallbackOutput.str(), "17");

  enumeration.reserved = 1;
  RuntimeBuffer invalidOutput;
  EXPECT_EQ(obelisk_rt_v1_format(context, "%p", 2, &argument, 1, nullptr,
                                 invalidOutput.out()),
            OBELISK_RT_INVALID_ARGUMENT);
}

TEST_F(ManagedHeapTest, FormatsRawAggregateRepresentationsWithoutTextLoss) {
  const std::string pattern("P\0Q", 3);
  const std::string twoState("A\0B", 3);
  const std::string fourState("C\0D", 3);
  obelisk_rt_raw_aggregate_arg_v1 aggregate{};
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, pattern.data(), pattern.size(),
                                        &aggregate.pattern),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, twoState.data(), twoState.size(),
                                        &aggregate.two_state),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, fourState.data(),
                                        fourState.size(),
                                        &aggregate.four_state),
            OBELISK_RT_OK);
  obelisk_rt_arg_v1 argument{OBELISK_RT_ARG_RAW_AGGREGATE, 0, 0, &aggregate,
                             nullptr};
  const obelisk_rt_arg_v1 arguments[] = {argument, argument};
  RuntimeBuffer rawOutput;
  ASSERT_EQ(obelisk_rt_v1_format(context, "%u%z", 4, arguments,
                                 std::size(arguments), nullptr,
                                 rawOutput.out()),
            OBELISK_RT_OK);
  EXPECT_EQ(rawOutput.str(), twoState + fourState);
  EXPECT_EQ(rawOutput.str().size(), 6u);

  RuntimeBuffer patternOutput;
  ASSERT_EQ(obelisk_rt_v1_format(context, "%s", 2, &argument, 1, nullptr,
                                 patternOutput.out()),
            OBELISK_RT_OK);
  EXPECT_EQ(patternOutput.str(), "P Q");

  uint64_t packed = 1;
  obelisk_rt_arg_v1 packedArgument{OBELISK_RT_ARG_LOGIC, 0, 1, &packed,
                                   nullptr};
  RuntimeBuffer packedWidth;
  RuntimeBuffer aggregateWidth;
  EXPECT_EQ(obelisk_rt_v1_format(context, "%1u", 3, &packedArgument, 1, nullptr,
                                 packedWidth.out()),
            OBELISK_RT_FORMAT_ERROR);
  EXPECT_EQ(obelisk_rt_v1_format(context, "%1u", 3, &argument, 1, nullptr,
                                 aggregateWidth.out()),
            OBELISK_RT_FORMAT_ERROR);

  RuntimeBuffer malformed;
  EXPECT_EQ(obelisk_rt_v1_format(context, "%", 1, &argument, 1, nullptr,
                                 malformed.out()),
            OBELISK_RT_FORMAT_ERROR);
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
  ASSERT_EQ(obelisk_rt_v1_object_field_store(root, kSlot * 4, derivedChild),
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
  ASSERT_EQ(
      obelisk_rt_v1_random_graph_variable(graph, 0, &variableObject, &variable),
      OBELISK_RT_OK);
  EXPECT_EQ(variableObject, root);
  EXPECT_EQ(variable, &randomNodeVariable);
  ASSERT_EQ(
      obelisk_rt_v1_random_graph_variable(graph, 1, &variableObject, &variable),
      OBELISK_RT_OK);
  EXPECT_EQ(variableObject, root);
  EXPECT_EQ(variable, &randomDerivedVariable);
  ASSERT_EQ(
      obelisk_rt_v1_random_graph_variable(graph, 2, &variableObject, &variable),
      OBELISK_RT_OK);
  EXPECT_EQ(variableObject, child);
  EXPECT_EQ(variable, &randomNodeVariable);
  ASSERT_EQ(
      obelisk_rt_v1_random_graph_variable(graph, 3, &variableObject, &variable),
      OBELISK_RT_OK);
  EXPECT_EQ(variableObject, derivedChild);
  EXPECT_EQ(variable, &randomNodeVariable);
  EXPECT_EQ(
      obelisk_rt_v1_random_graph_variable(graph, 4, &variableObject, &variable),
      OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(variableObject, nullptr);
  EXPECT_EQ(variable, nullptr);

  obelisk_rt_object_v1 *referencedObject = nullptr;
  const obelisk_rt_random_variable_v1 *referencedVariable = nullptr;
  uint64_t graphVariableIndex = UINT64_MAX;
  const obelisk_rt_random_variable_reference_v1 rootValueReference{
      nullptr, 0, kRandomNodeValueOffset, 64,
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
      childPath, std::size(childPath), kRandomNodeValueOffset, 64,
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
      aliasPath, std::size(aliasPath), kRandomNodeValueOffset, 64,
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
      invalidPath, std::size(invalidPath), kRandomNodeValueOffset, 64,
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
  ASSERT_EQ(
      obelisk_rt_v1_random_graph_variable(graph, 0, &variableObject, &variable),
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

  ASSERT_EQ(obelisk_rt_v1_gc_root_range_pop(lane, &objectRoots), OBELISK_RT_OK);
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
  ASSERT_EQ(
      obelisk_rt_v1_assoc_next(lane, array, &absentCursor, &cursorSuccess),
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
  ASSERT_EQ(obelisk_rt_v1_reference_path_assoc_create(lane, array, &derivedKey,
                                                      nullptr, 0, 0, nullptr,
                                                      nullptr, 0, &path),
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
  value = 0;
  present = 1;
  ASSERT_EQ(
      obelisk_rt_v1_assoc_read(array, &derivedKey, &value, nullptr, &present),
      OBELISK_RT_OK);
  EXPECT_EQ(present, 0u);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &pathRoot), OBELISK_RT_OK);

  obelisk_rt_assoc_key_v1 cursor{};
  uint32_t success = 0;
  for (size_t index = 0; index + 1 < std::size(expectedKeys); ++index) {
    obelisk_rt_object_v1 *expected = expectedKeys[index];
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

  // Managed roots in native state are tagged 64-bit words, not raw pointers,
  // so the backing bytes are eight per root at any pointer width. Copying the
  // pointer array directly only produced the right layout at 64 bits.
  obelisk_rt_managed_word_v1 initialWords[std::size(objects)] = {};
  for (size_t index = 0; index != std::size(objects); ++index)
    initialWords[index] = static_cast<obelisk_rt_managed_word_v1>(
        reinterpret_cast<uintptr_t>(objects[index]));
  uint8_t initial[sizeof(initialWords)] = {};
  std::memcpy(initial, initialWords, sizeof(initialWords));
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
                context, 64, reinterpret_cast<const uint8_t *>(&small), nullptr,
                &rootOffset, 1, &smallHandle),
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
                context, 64, reinterpret_cast<const uint8_t *>(&heap), nullptr,
                &rootOffset, 1, &heapHandle),
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
  obelisk_rt_method_descriptor_v1 sparseMethods[]{{}, nodeMethods[0]};
  obelisk_rt_class_descriptor_v1 sparse = nodeDescriptor;
  sparse.methods = sparseMethods;
  sparse.method_count = std::size(sparseMethods);
  EXPECT_EQ(obelisk_rt_v1_class_validate(&sparse), OBELISK_RT_OK);

  sparseMethods[0].flags = OBELISK_RT_METHOD_PURE;
  EXPECT_EQ(obelisk_rt_v1_class_validate(&sparse), OBELISK_RT_INVALID_DESIGN);

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
      {8, 1, 8, OBELISK_RT_TRACE_WEAK, OBELISK_RT_MANAGED_SLOT_CLASS, nullptr},
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
