//===- RuntimeInternal.h - Shared native runtime internals -------*- C++
//-*-===//

#ifndef OBELISK_RUNTIME_LIB_RUNTIMEINTERNAL_H
#define OBELISK_RUNTIME_LIB_RUNTIMEINTERNAL_H

#include "obelisk/Runtime/Runtime.h"

#include <array>
#include <cstdio>
#include <mutex>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

struct FileEntry {
  FILE *stream = nullptr;
  int lastError = 0;
  bool writable = false;
};

struct ScheduledProcess {
  obelisk_rt_process_instance_v1 *instance = nullptr;
  std::vector<obelisk_rt_process_instance_v1 *> callers;
  std::vector<uint64_t> controls;
  uint64_t token = 0;
  uint64_t parent = 0;
  uint64_t observedEpoch = 0;
  uint64_t observedSignalSequence = 0;
  uint64_t wakeTime = 0;
  uint64_t waitOffset = 0;
  uint64_t waitSize = 0;
  std::vector<uint64_t> waitGenerations;
  std::vector<std::pair<uint32_t, uint32_t>> continuationRanks;
  uint32_t suspendKind = OBELISK_RT_SUSPEND_NONE;
  uint32_t phase = 0;
  uint32_t scheduleRank = UINT32_MAX;
  uint64_t insertionSequence = 0;
  // 0 is the process home/active queue; 1 is the corresponding inactive
  // queue used only by a zero delay.
  uint32_t queuedRegion = 0;
  bool started = false;
  bool urgent = false;
  bool signalTriggered = false;
};

struct ScheduledSignalEvent {
  uint64_t sequence = 0;
  uint64_t bitOffset = 0;
  uint64_t bitWidth = 0;
  uint32_t edges = 0;
};

struct SignalValueSnapshot {
  uint64_t sequence = 0;
  bool value = false;
  bool unknown = false;
};

struct NativeAutomaticState {
  uint64_t bitWidth = 0;
  obelisk_rt_process_instance_v1 *owner = nullptr;
  uint64_t designOwner = 0;
  uint64_t referenceCount = 1;
  std::vector<uint8_t> value;
  std::vector<uint8_t> unknown;
};

struct NativeStaticState {
  uint64_t bitOffset = 0;
  uint64_t bitWidth = 0;
};

struct ScheduledNBA {
  uint64_t sequence = 0;
  uint64_t dueTime = 0;
  uint32_t retainedAutomaticID = 0;
  uint8_t *valuePlane = nullptr;
  uint8_t *unknownPlane = nullptr;
  uint64_t planeBitCount = 0;
  uint64_t bitOffset = 0;
  uint64_t bitWidth = 0;
  std::vector<uint8_t> value;
  std::vector<uint8_t> unknown;
};

struct ScheduledDesignNBA {
  uint64_t sequence = 0;
  uint64_t dueTime = 0;
  uint32_t handleKind = 0;
  int64_t begin = 0;
  int64_t start = 0;
  int64_t end = 0;
  uint64_t bitWidth = 0;
  std::vector<uint64_t> value;
  std::vector<uint64_t> unknown;
};

struct ScheduledDesignEvent {
  uint64_t sequence = 0;
  uint64_t dueTime = 0;
  uint64_t stableID = 0;
};

struct DesignActivation {
  uint32_t function = 0;
  uint32_t continuation = 0;
  std::vector<uint8_t> frame;
  uint64_t scratchOffset = 0;
  uint64_t scratchSize = 0;
  uint32_t scheduleRank = UINT32_MAX;
};

struct ScheduledDesignTask {
  uint64_t id = 0;
  uint64_t parent = 0;
  uint32_t function = 0;
  uint32_t continuation = 0;
  std::vector<DesignActivation> callers;
  std::vector<uint64_t> controls;
  std::vector<uint8_t> frame;
  uint64_t scratchOffset = 0;
  uint64_t scratchSize = 0;
  uint64_t observedEpoch = 0;
  uint64_t observedSignalSequence = 0;
  uint64_t wakeTime = 0;
  uint64_t waitOffset = 0;
  uint64_t waitSize = 0;
  std::vector<uint64_t> waitGenerations;
  uint32_t suspendKind = OBELISK_RT_SUSPEND_NONE;
  uint32_t scheduleRank = UINT32_MAX;
  uint32_t queuedRegion = 0;
  uint64_t insertionSequence = 0;
  bool started = false;
  bool urgent = false;
  bool terminated = false;
  bool signalTriggered = false;
};

struct ImportBinding {
  obelisk_rt_import_callback_v1 callback = nullptr;
  void *userData = nullptr;
  uint64_t abiSignature = 0;
};

struct ControlActivation {
  uint64_t target = 0;
  uint64_t memberships = 0;
};

struct DpiScopeHandle {
  obelisk_rt_context *context = nullptr;
  uint64_t id = 0;
  uint64_t parentID = UINT64_MAX;
  std::string name;
  int32_t timeUnit = 0;
  int32_t timePrecision = 0;
  std::unordered_map<void *, void *> userData;
};

struct obelisk_rt_context {
  // Mutable state is guarded separately from logical execution. Evaluator
  // callbacks release `mutex` while arbitrary user code runs, but retain the
  // recursive transaction lock so another thread cannot interleave a state or
  // scheduler mutation. Nested calls on the evaluator thread re-enter both.
  std::recursive_mutex mutex;
  std::recursive_mutex transactionMutex;
  std::thread::id transactionOwner;
  uint32_t transactionDepth = 0;
  bool destroyPending = false;
  std::array<FileEntry, 31> mcd;
  // 0x80000000, 0x80000001, and 0x80000002 are the IEEE predefined stdin,
  // stdout, and stderr descriptors. Dynamic descriptors begin at index 3.
  std::vector<FileEntry> files;
  std::vector<uint32_t> freeFiles;
  std::vector<uint32_t> freeMCDs;
  std::unordered_map<std::thread::id, std::string> lastErrors;
  std::vector<ScheduledProcess> scheduledProcesses;
  std::vector<ScheduledSignalEvent> scheduledSignalEvents;
  std::unordered_map<uint64_t, SignalValueSnapshot> signalValueSnapshots;
  std::vector<ScheduledNBA> scheduledNBAs;
  std::vector<ScheduledDesignNBA> scheduledDesignNBAs;
  std::vector<ScheduledDesignEvent> scheduledDesignEvents;
  std::vector<ScheduledDesignTask> scheduledDesignTasks;
  uint64_t nextSchedulerSequence = 1;
  uint64_t nextNativeProcessToken = 1;
  uint32_t nextNativeAutomaticID = 1;
  uint64_t nextDesignTaskID = 1;
  uint64_t nextProcessInsertionSequence = 1;
  uint64_t activeDesignTaskID = 0;
  uint64_t activeLogicalProcessToken = 0;
  std::vector<uint64_t> activeControls;
  obelisk_rt_process_instance_v1 *activeNativeProcess = nullptr;
  bool designTaskExecuting = false;
  std::unordered_set<uint64_t> terminatedDesignTasks;
  std::unordered_set<uint64_t> terminatedNativeProcesses;
  uint64_t nextControlActivation = 1;
  std::unordered_map<uint64_t, ControlActivation> controlActivations;
  std::unordered_set<uint64_t> initializedStaticSites;
  std::unordered_map<uint32_t, NativeStaticState> nativeStaticStates;
  std::unordered_map<uint32_t, NativeAutomaticState> nativeAutomaticStates;
  std::unordered_map<uint64_t, uint64_t> eventGenerations;
  std::unordered_map<uint64_t, uint64_t> eventLastTriggeredTimes;
  std::unordered_map<uint32_t, ImportBinding> imports;
  std::vector<std::unique_ptr<DpiScopeHandle>> dpiScopes;
  std::unordered_map<std::string, DpiScopeHandle *> dpiScopesByName;
  size_t schedulerCursor = 0;
  uint64_t schedulerEpoch = 1;
  uint64_t schedulerTime = 0;
  bool schedulerRunningFinals = false;
  obelisk_rt_status schedulerStatus = OBELISK_RT_OK;
  uint32_t observerDepth = 0;
  const obelisk_rt_execution_descriptor_v1 *execution = nullptr;
  // Live simulation state is owned by the context.  The planes use the same
  // little-endian limb representation as bytecode values and are never stored
  // in the immutable reflection image.
  std::vector<uint64_t> stateValue;
  std::vector<uint64_t> stateUnknown;

  obelisk_rt_context();
};

// Serialize a complete external mutation or scheduler fragment across any
// recursively invoked observer callbacks. If a callback destroys its active
// context, final cleanup is deferred until the outermost transaction returns.
class ContextTransaction {
public:
  explicit ContextTransaction(obelisk_rt_context *context);
  ContextTransaction(const ContextTransaction &) = delete;
  ContextTransaction &operator=(const ContextTransaction &) = delete;
  ~ContextTransaction() noexcept;

private:
  obelisk_rt_context *context = nullptr;
  std::unique_lock<std::recursive_mutex> transactionLock;
};

class ContextCallbackUnlock {
public:
  explicit ContextCallbackUnlock(obelisk_rt_context *context)
      : context(context) {
    context->mutex.unlock();
  }
  ContextCallbackUnlock(const ContextCallbackUnlock &) = delete;
  ContextCallbackUnlock &operator=(const ContextCallbackUnlock &) = delete;
  ~ContextCallbackUnlock() { context->mutex.lock(); }

private:
  obelisk_rt_context *context;
};

void setLastErrorUnlocked(obelisk_rt_context *context, std::string message);
void setLastError(obelisk_rt_context *context, std::string message);

void obelisk_rt_retain_controls_unlocked(obelisk_rt_context *context,
                                         const std::vector<uint64_t> &controls);
void obelisk_rt_release_control_unlocked(obelisk_rt_context *context,
                                         uint64_t control);
void obelisk_rt_release_controls_unlocked(
    obelisk_rt_context *context, const std::vector<uint64_t> &controls);

bool obelisk_rt_validate_activation_bytecode_inventory(
    const obelisk_rt_execution_descriptor_v1 &execution) noexcept;

DpiScopeHandle *obelisk_rt_find_dpi_scope(obelisk_rt_context *context,
                                          uint64_t id);
obelisk_rt_status obelisk_rt_initialize_dpi_scopes(
    obelisk_rt_context *context,
    const obelisk_rt_execution_descriptor_v1 *execution);

template <typename Callable>
obelisk_rt_status guarded(obelisk_rt_context *context,
                          Callable &&callable) noexcept {
  try {
    return callable();
  } catch (const std::bad_alloc &) {
    setLastError(context, "runtime allocation failed");
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    setLastError(context, "unexpected runtime exception");
    return OBELISK_RT_IO_ERROR;
  }
}

obelisk_rt_status makeBuffer(std::string_view source,
                             obelisk_rt_buffer_v1 *output);
bool validBytes(const void *data, uint64_t size);
std::string hostErrorMessage(int error);

obelisk_rt_status writeUnlocked(obelisk_rt_context *context,
                                uint32_t descriptor, const void *data,
                                uint64_t size, uint64_t *outWritten);

// Fully validate immutable bytecode metadata without executing or mutating a
// process frame. Missing continuations are tier-unavailable; malformed
// programs are invalid bytecode.
obelisk_rt_status
obelisk_rt_validate_bytecode_program(const obelisk_rt_bytecode_v1 &program,
                                     uint32_t continuation) noexcept;

// Design-wide bytecode helpers shared by process construction/dispatch.  They
// perform full image validation before returning layout information.
obelisk_rt_status obelisk_rt_validate_design_bytecode(
    const obelisk_rt_design_bytecode_entry_v1 &entry,
    uint64_t *outScratchSize, uint64_t *outScratchAlignment) noexcept;
obelisk_rt_status obelisk_rt_execute_design_bytecode(
    const obelisk_rt_design_bytecode_entry_v1 &entry,
    obelisk_rt_context *context, void *frame, uint64_t frameSize,
    uint64_t scratchOffset, uint64_t scratchSize, uint32_t continuation,
    uint64_t instructionLimit,
    obelisk_rt_fragment_action_v1 *outAction) noexcept;
obelisk_rt_status obelisk_rt_execute_design_observer(
    const obelisk_rt_execution_descriptor_v1 &execution,
    obelisk_rt_context *context, uint32_t function,
    const obelisk_rt_computed_capture_v1 *captures, uint32_t captureCount,
    uint64_t *value, uint64_t *unknown, uint32_t limbCount) noexcept;
obelisk_rt_status
obelisk_rt_initialize_design_state(obelisk_rt_context *context) noexcept;
obelisk_rt_status obelisk_rt_resolve_design_drivers(
    obelisk_rt_context *context, uint64_t begin, uint64_t end) noexcept;
obelisk_rt_status obelisk_rt_design_net_is_connected(
    obelisk_rt_context *context, uint64_t begin, uint64_t end,
    bool *outConnected) noexcept;
obelisk_rt_status obelisk_rt_run_one_design_task(
    obelisk_rt_context *context, uint32_t maximumRegion,
    uint32_t maximumRank, uint64_t maximumInsertionSequence,
    bool *outProgress) noexcept;

// Append one already-committed scalar transition while the context mutex is
// held, and latch level/iff observers against the state at this exact
// occurrence. Both native stores and design bytecode use this path.
bool obelisk_rt_append_signal_event_unlocked(
    obelisk_rt_context *context, uint64_t bitOffset, bool oldValue,
    bool oldUnknown, bool newValue, bool newUnknown);
bool obelisk_rt_append_signal_event_unlocked(
    obelisk_rt_context *context, uint64_t bitOffset, bool oldValue,
    bool oldUnknown, bool newValue, bool newUnknown,
    bool evaluateComputedObservers);
bool obelisk_rt_notify_observer_event_unlocked(obelisk_rt_context *context,
                                               uint64_t stableID);
bool obelisk_rt_notify_observer_signal_unlocked(obelisk_rt_context *context,
                                                uint64_t stableID,
                                                uint64_t width);
bool obelisk_rt_evaluate_design_observers_unlocked(
    obelisk_rt_context *context, uint32_t dependencyKind,
    uint64_t publishedHandle, uint64_t publishedWidth);
void obelisk_rt_erase_automatic_signal_snapshots_unlocked(
    obelisk_rt_context *context, uint32_t automaticID);
void obelisk_rt_invalidate_signal_snapshots_unlocked(
    obelisk_rt_context *context, uint64_t bitOffset, uint64_t bitWidth);

bool obelisk_rt_checked_design_record(
    const obelisk_rt_execution_descriptor_v1 *execution, uint64_t offset,
    const uint8_t *&record, uint32_t &kind) noexcept;

#endif // OBELISK_RUNTIME_LIB_RUNTIMEINTERNAL_H
