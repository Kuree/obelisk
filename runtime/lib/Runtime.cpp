//===- Runtime.cpp - Obelisk native runtime context and buffers
//------------===//

#include "RuntimeInternal.h"
#include "obelisk/Runtime/StableHash.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace {

thread_local obelisk_rt_context *threadTransactionContext = nullptr;
thread_local uint32_t threadTransactionDepth = 0;

std::recursive_mutex hostErrorMutex;

struct ThreadError {
  std::weak_ptr<const uint8_t> contextLifetime;
  std::string message;
};

thread_local std::unordered_map<const obelisk_rt_context *, ThreadError>
    threadErrors;

bool validActivationInventory(
    const obelisk_rt_execution_descriptor_v1 &execution) {
  if ((execution.activation_count == 0) != (execution.activations == nullptr))
    return false;
  constexpr uint32_t validFlags =
      OBELISK_RT_ACTIVATION_HAS_NATIVE | OBELISK_RT_ACTIVATION_HAS_BYTECODE;
  uint64_t previousID = 0;
  for (uint64_t index = 0; index != execution.activation_count; ++index) {
    const obelisk_rt_activation_descriptor_v1 &activation =
        execution.activations[index];
    if (activation.code_unit_id == 0 ||
        (index != 0 && activation.code_unit_id <= previousID) ||
        activation.flags == 0 || (activation.flags & ~validFlags) != 0)
      return false;
    previousID = activation.code_unit_id;
    bool hasNative = (activation.flags & OBELISK_RT_ACTIVATION_HAS_NATIVE) != 0;
    if (hasNative != (activation.native_entry != nullptr))
      return false;
    if (hasNative &&
        (activation.native_entry->handle.kind !=
             OBELISK_RT_DESCRIPTOR_PROCESS ||
         activation.native_entry->handle.id != activation.code_unit_id ||
         activation.native_entry->version != OBELISK_RT_VERSION ||
         activation.native_entry->execution != &execution))
      return false;
    bool hasBytecode =
        (activation.flags & OBELISK_RT_ACTIVATION_HAS_BYTECODE) != 0;
    if (hasBytecode) {
      if ((execution.flags & OBELISK_RT_EXECUTION_HAS_BYTECODE) == 0 ||
          activation.bytecode_function == OBELISK_RT_ACTIVATION_NO_BYTECODE)
        return false;
    } else if (activation.bytecode_function !=
               OBELISK_RT_ACTIVATION_NO_BYTECODE) {
      return false;
    }
  }
  return true;
}

bool validObserverInventory(
    const obelisk_rt_execution_descriptor_v1 &execution) {
  if ((execution.observer_count == 0) != (execution.observers == nullptr))
    return false;
  uint64_t previousID = 0;
  for (uint64_t index = 0; index != execution.observer_count; ++index) {
    const obelisk_rt_observer_descriptor_v1 &observer =
        execution.observers[index];
    if (observer.code_unit_id == 0 ||
        (index != 0 && observer.code_unit_id <= previousID) ||
        observer.result_width == 0 ||
        (observer.flags &
         ~(OBELISK_RT_OBSERVER_FOUR_STATE | OBELISK_RT_OBSERVER_REAL32 |
           OBELISK_RT_OBSERVER_REAL64)) != 0 ||
        ((observer.flags & OBELISK_RT_OBSERVER_REAL32) != 0 &&
         (observer.result_width != 32 ||
          (observer.flags & (OBELISK_RT_OBSERVER_FOUR_STATE |
                             OBELISK_RT_OBSERVER_REAL64)) != 0)) ||
        ((observer.flags & OBELISK_RT_OBSERVER_REAL64) != 0 &&
         (observer.result_width != 64 ||
          (observer.flags & (OBELISK_RT_OBSERVER_FOUR_STATE |
                             OBELISK_RT_OBSERVER_REAL32)) != 0)) ||
        observer.reserved != 0 ||
        (observer.capture_count == 0) != (observer.capture_abi == nullptr))
      return false;
    previousID = observer.code_unit_id;
    for (uint32_t capture = 0; capture != observer.capture_count; ++capture) {
      const obelisk_rt_observer_capture_abi_v1 &abi =
          observer.capture_abi[capture];
      if (abi.kind < OBELISK_RT_OBSERVER_CAPTURE_STORAGE ||
          abi.kind > OBELISK_RT_OBSERVER_CAPTURE_DRIVER || abi.width == 0 ||
          (abi.kind == OBELISK_RT_OBSERVER_CAPTURE_EVENT && abi.width != 1))
        return false;
    }
    bool hasBytecode =
        observer.bytecode_function != OBELISK_RT_OBSERVER_NO_BYTECODE;
    if (hasBytecode &&
        (execution.flags & OBELISK_RT_EXECUTION_HAS_BYTECODE) == 0)
      return false;
    if (!observer.native_evaluator && !hasBytecode)
      return false;
  }
  return true;
}

const obelisk_rt_execution_extension_v1 *
executionExtension(const obelisk_rt_execution_descriptor_v1 &execution) {
  if (execution.reserved == 0 ||
      execution.reserved > std::numeric_limits<uintptr_t>::max() ||
      execution.reserved % alignof(obelisk_rt_execution_extension_v1) != 0)
    return nullptr;
  return reinterpret_cast<const obelisk_rt_execution_extension_v1 *>(
      static_cast<uintptr_t>(execution.reserved));
}

bool validSampledRanges(const obelisk_rt_execution_descriptor_v1 &execution) {
  bool enabled =
      (execution.flags & OBELISK_RT_EXECUTION_PREPONED_SNAPSHOT) != 0;
  if (!enabled)
    return execution.reserved == 0;
  const obelisk_rt_execution_extension_v1 *extension =
      executionExtension(execution);
  if (!extension ||
      extension->version != OBELISK_RT_EXECUTION_EXTENSION_VERSION ||
      extension->size != sizeof(*extension) || !extension->sampled_ranges ||
      extension->sampled_range_count == 0)
    return false;
  uint64_t previousEnd = 0;
  uint64_t snapshotBytes = 0;
  for (uint64_t index = 0; index != extension->sampled_range_count; ++index) {
    const obelisk_rt_sampled_range_v1 &range = extension->sampled_ranges[index];
    if (range.bit_width == 0 || range.source_bit_offset < previousEnd ||
        range.source_bit_offset > execution.state_bit_count ||
        range.bit_width > execution.state_bit_count - range.source_bit_offset ||
        range.snapshot_byte_offset != snapshotBytes ||
        range.bit_width > UINT64_MAX - 7 ||
        snapshotBytes > UINT64_MAX - (range.bit_width + 7) / 8)
      return false;
    previousEnd = range.source_bit_offset + range.bit_width;
    snapshotBytes += (range.bit_width + 7) / 8;
  }
  return snapshotBytes <= std::numeric_limits<size_t>::max() &&
         snapshotBytes <= UINT64_MAX / 8;
}

} // namespace

obelisk_rt_context::obelisk_rt_context() {
  if (const char *diagnostics = std::getenv("OBELISK_RT_SIGNAL_DIAGNOSTICS")) {
    signalDiagnosticsEnabled =
        diagnostics[0] != '\0' && std::strcmp(diagnostics, "0") != 0;
    signalDiagnosticsReport = signalDiagnosticsEnabled;
  }
  errorLifetime = std::make_shared<const uint8_t>(0);
  managedHeap = obelisk_rt_managed_heap_create(this);
  mcd[0].stream = stdout;
  mcd[0].writable = true;
  files.resize(3);
  files[0] = {stdin, 0, false};
  files[1] = {stdout, 0, true};
  files[2] = {stderr, 0, true};
  for (uint32_t bit = 30; bit >= 1; --bit)
    freeMCDs.push_back(bit);
  obelisk_rt_random_seed_context_unlocked(this, 1);
}

void obelisk_rt_report_signal_diagnostics_unlocked(
    obelisk_rt_context *context) {
  if (!context || !context->signalDiagnosticsReport)
    return;
  std::fprintf(
      stderr,
      "obelisk-signal-diagnostics publications=%llu "
      "subscriptions_current=%llu subscriptions_high_water=%llu "
      "subscribers_examined=%llu readiness_calls=%llu "
      "candidate_scans=%llu scheduler_iterations=%llu "
      "fallback_rescans=%llu aot_node_executions=%llu "
      "aot_region_passes=%llu aot_fanout_entries=%llu "
      "aot_nba_stages=%llu aot_nba_commits=%llu "
      "aot_state_fast_paths=%llu aot_state_slow_paths=%llu "
      "aot_deadline_high_water=%llu aot_fallbacks=%llu\n",
      static_cast<unsigned long long>(context->signalDiagnostics.publications),
      static_cast<unsigned long long>(
          context->signalDiagnostics.subscriptionsCurrent),
      static_cast<unsigned long long>(
          context->signalDiagnostics.subscriptionsHighWater),
      static_cast<unsigned long long>(
          context->signalDiagnostics.subscribersExamined),
      static_cast<unsigned long long>(
          context->signalDiagnostics.readinessCalls),
      static_cast<unsigned long long>(
          context->signalDiagnostics.candidateScans),
      static_cast<unsigned long long>(
          context->signalDiagnostics.schedulerIterations),
      static_cast<unsigned long long>(
          context->signalDiagnostics.fallbackRescans),
      static_cast<unsigned long long>(
          context->signalDiagnostics.aotNodeExecutions),
      static_cast<unsigned long long>(
          context->signalDiagnostics.aotRegionPasses),
      static_cast<unsigned long long>(
          context->signalDiagnostics.aotFanoutEntries),
      static_cast<unsigned long long>(context->signalDiagnostics.aotNBAStages),
      static_cast<unsigned long long>(context->signalDiagnostics.aotNBACommits),
      static_cast<unsigned long long>(
          context->signalDiagnostics.aotStateFastPaths),
      static_cast<unsigned long long>(
          context->signalDiagnostics.aotStateSlowPaths),
      static_cast<unsigned long long>(
          context->signalDiagnostics.aotDeadlineHighWater),
      static_cast<unsigned long long>(context->signalDiagnostics.aotFallbacks));
  for (unsigned slot = 0; slot != 64; ++slot)
    if (context->signalDiagnostics.aotActorExecutions[slot] != 0)
      std::fprintf(stderr, "obelisk-aot-actor slot=%u executions=%llu\n", slot,
                   static_cast<unsigned long long>(
                       context->signalDiagnostics.aotActorExecutions[slot]));
}

obelisk_rt_context::~obelisk_rt_context() {
  if (designDatabaseRegistered)
    obelisk_rt_unregister_design_database(execution);
  obelisk_rt_report_signal_diagnostics_unlocked(this);
  obelisk_rt_release_native_schedule_plan(this);
  threadErrors.erase(this);
  obelisk_rt_managed_heap_destroy(managedHeap);
}

namespace {

void destroyContextNow(obelisk_rt_context *context) noexcept {
  std::vector<ScheduledProcess> processes;
  try {
    // Settle the final time slot before the state planes go away.
    obelisk_rt_dump_destroy(context);
    if (context->vpiState)
      obelisk_rt_v1_vpi_shutdown(context);
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      for (ScheduledProcess &scheduled : context->scheduledProcesses)
        obelisk_rt_unregister_signal_wait_unlocked(
            context, scheduled.signalSubscriptions);
      for (ScheduledDesignTask &task : context->scheduledDesignTasks)
        obelisk_rt_unregister_signal_wait_unlocked(context,
                                                   task.signalSubscriptions);
      processes.swap(context->scheduledProcesses);
    }
    for (ScheduledProcess &scheduled : processes) {
      if (scheduled.instance)
        (void)obelisk_rt_v1_process_instance_destroy(scheduled.instance);
      for (obelisk_rt_process_instance_v1 *caller : scheduled.callers)
        (void)obelisk_rt_v1_process_instance_destroy(caller);
    }
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    for (uint32_t bit = 1; bit < context->mcd.size(); ++bit) {
      if (context->mcd[bit].stream)
        std::fclose(context->mcd[bit].stream);
    }
    for (size_t index = 3; index < context->files.size(); ++index) {
      if (context->files[index].stream)
        std::fclose(context->files[index].stream);
    }
    std::fflush(stdout);
  } catch (...) {
  }
  delete context;
}

} // namespace

ContextTransaction::ContextTransaction(obelisk_rt_context *context)
    : context(context) {
  if (!context)
    return;
  if (threadTransactionContext == context) {
    ++threadTransactionDepth;
    nested = true;
    return;
  }
  previousThreadContext = threadTransactionContext;
  previousThreadDepth = threadTransactionDepth;
  transactionLock =
      std::unique_lock<std::recursive_mutex>(context->transactionMutex);
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  if (context->transactionDepth++ == 0)
    context->transactionOwner = std::this_thread::get_id();
  threadTransactionContext = context;
  threadTransactionDepth = 1;
}

ContextTransaction::~ContextTransaction() noexcept {
  if (!context)
    return;
  if (nested) {
    if (threadTransactionContext == context && threadTransactionDepth != 0)
      --threadTransactionDepth;
    return;
  }
  threadTransactionContext = previousThreadContext;
  threadTransactionDepth = previousThreadDepth;
  bool destroy = false;
  try {
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      if (context->transactionDepth != 0 && --context->transactionDepth == 0) {
        context->transactionOwner = {};
        destroy = context->destroyPending;
      }
    }
    transactionLock.unlock();
    if (destroy)
      destroyContextNow(context);
  } catch (...) {
    // Transaction teardown must not replace the operation's status.
  }
}

void setLastErrorUnlocked(obelisk_rt_context *context, std::string message) {
  for (auto error = threadErrors.begin(); error != threadErrors.end();) {
    if (error->second.contextLifetime.expired())
      error = threadErrors.erase(error);
    else
      ++error;
  }
  threadErrors[context] = {context->errorLifetime, std::move(message)};
}

void setLastError(obelisk_rt_context *context, std::string message) {
  if (!context)
    return;
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    setLastErrorUnlocked(context, std::move(message));
  } catch (...) {
  }
}

void obelisk_rt_retain_controls_unlocked(
    obelisk_rt_context *context, const std::vector<uint64_t> &controls) {
  for (uint64_t token : controls)
    if (auto found = context->controlActivations.find(token);
        found != context->controlActivations.end())
      ++found->second.memberships;
}

void obelisk_rt_release_controls_unlocked(
    obelisk_rt_context *context, const std::vector<uint64_t> &controls) {
  for (uint64_t token : controls)
    obelisk_rt_release_control_unlocked(context, token);
}

void obelisk_rt_release_control_unlocked(obelisk_rt_context *context,
                                         uint64_t token) {
  auto found = context->controlActivations.find(token);
  if (found == context->controlActivations.end())
    return;
  if (found->second.memberships != 0)
    --found->second.memberships;
  if (found->second.memberships == 0)
    context->controlActivations.erase(found);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_control_enter(obelisk_rt_context *context, uint64_t targetID,
                            uint64_t *outActivation) {
  if (!context || targetID == 0 || !outActivation)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outActivation = 0;
  return guarded(context, [&] {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (context->activeLogicalProcessToken == 0 ||
        context->nextControlActivation == 0)
      return OBELISK_RT_INVALID_LIFECYCLE;
    uint64_t token = context->nextControlActivation;
    context->activeControls.reserve(context->activeControls.size() + 1);
    auto [_, inserted] = context->controlActivations.emplace(
        token, ControlActivation{targetID, 1});
    if (!inserted)
      return OBELISK_RT_INVALID_LIFECYCLE;
    context->activeControls.push_back(token);
    ++context->nextControlActivation;
    *outActivation = token;
    return OBELISK_RT_OK;
  });
}

extern "C" obelisk_rt_status
obelisk_rt_v1_control_leave(obelisk_rt_context *context, uint64_t activation) {
  if (!context || activation == 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  return guarded(context, [&] {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (context->activeLogicalProcessToken == 0 ||
        context->activeControls.empty() ||
        context->activeControls.back() != activation)
      return OBELISK_RT_INVALID_LIFECYCLE;
    context->activeControls.pop_back();
    obelisk_rt_release_control_unlocked(context, activation);
    return OBELISK_RT_OK;
  });
}

extern "C" uint32_t obelisk_rt_v1_static_once(obelisk_rt_context *context,
                                              uint64_t siteID) {
  if (!context || siteID == 0)
    return 0;
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    return context->initializedStaticSites.insert(siteID).second ? 1u : 0u;
  } catch (...) {
    if (context)
      context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
    return 0;
  }
}

extern "C" uint32_t obelisk_rt_v1_deferred_once(obelisk_rt_context *context,
                                                uint64_t siteID) {
  if (!context || siteID == 0)
    return 0;
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (context->activeLogicalProcessToken == 0)
      return 0;
    if (context->deferredImmediateTime != context->schedulerTime) {
      context->deferredImmediateSites.clear();
      context->deferredImmediateTime = context->schedulerTime;
    }
    return context->deferredImmediateSites[context->activeLogicalProcessToken]
                   .insert(siteID)
                   .second
               ? 1u
               : 0u;
  } catch (...) {
    if (context)
      context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
    return 0;
  }
}

static void resetDeferredImmediateReportsForTime(obelisk_rt_context *context) {
  if (context->deferredImmediateTime == context->schedulerTime)
    return;
  context->deferredImmediateSites.clear();
  context->deferredImmediateReports.clear();
  context->latestDeferredImmediateReports.clear();
  context->deferredImmediateAssertionReports.clear();
  context->deferredImmediateTime = context->schedulerTime;
}

static void eraseDeferredImmediateReportUnlocked(obelisk_rt_context *context,
                                                 uint64_t ticket) {
  auto report = context->deferredImmediateReports.find(ticket);
  if (report == context->deferredImmediateReports.end())
    return;
  if (report->second.assertion != 0) {
    auto assertion = context->deferredImmediateAssertionReports.find(
        report->second.assertion);
    if (assertion != context->deferredImmediateAssertionReports.end()) {
      assertion->second.erase(ticket);
      if (assertion->second.empty())
        context->deferredImmediateAssertionReports.erase(assertion);
    }
  }
  context->deferredImmediateReports.erase(report);
}

void obelisk_rt_flush_deferred_immediate_reports_unlocked(
    obelisk_rt_context *context, uint64_t logicalProcess) {
  if (!context || logicalProcess == 0)
    return;
  resetDeferredImmediateReportsForTime(context);
  auto process = context->latestDeferredImmediateReports.find(logicalProcess);
  if (process == context->latestDeferredImmediateReports.end())
    return;
  for (const auto &[site, ticket] : process->second) {
    (void)site;
    eraseDeferredImmediateReportUnlocked(context, ticket);
  }
  context->latestDeferredImmediateReports.erase(process);
}

bool obelisk_rt_cancel_deferred_immediate_assertion_unlocked(
    obelisk_rt_context *context, uint64_t assertionID,
    uint64_t logicalProcess) {
  if (!context || assertionID == 0)
    return false;
  resetDeferredImmediateReportsForTime(context);
  auto assertion =
      context->deferredImmediateAssertionReports.find(assertionID);
  if (assertion == context->deferredImmediateAssertionReports.end())
    return false;
  bool canceled = false;
  for (auto iterator = assertion->second.begin();
       iterator != assertion->second.end();) {
    uint64_t ticket = *iterator;
    auto report = context->deferredImmediateReports.find(ticket);
    if (report == context->deferredImmediateReports.end()) {
      iterator = assertion->second.erase(iterator);
      continue;
    }
    if (logicalProcess != 0 &&
        report->second.logicalProcess != logicalProcess) {
      ++iterator;
      continue;
    }
    auto process = context->latestDeferredImmediateReports.find(
        report->second.logicalProcess);
    if (process != context->latestDeferredImmediateReports.end()) {
      auto site = process->second.find(report->second.site);
      if (site != process->second.end() && site->second == ticket)
        process->second.erase(site);
      if (process->second.empty())
        context->latestDeferredImmediateReports.erase(process);
    }
    context->deferredImmediateReports.erase(report);
    iterator = assertion->second.erase(iterator);
    canceled = true;
  }
  if (assertion->second.empty())
    context->deferredImmediateAssertionReports.erase(assertion);
  return canceled;
}

namespace {
constexpr uint8_t kAssertionDisabled = UINT8_C(1) << 0;
constexpr uint8_t kAssertionLocked = UINT8_C(1) << 1;
constexpr uint8_t kAssertionNonvacuousPassDisabled = UINT8_C(1) << 2;
constexpr uint8_t kAssertionVacuousPassDisabled = UINT8_C(1) << 3;
constexpr uint8_t kAssertionFailDisabled = UINT8_C(1) << 4;
} // namespace

extern "C" obelisk_rt_status
obelisk_rt_v1_assertion_control(obelisk_rt_context *context, uint32_t action,
                                uint64_t assertionID) {
  if (!context || assertionID == 0 || action < 1 || action > 11)
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    auto found = context->assertionControlStates.find(assertionID);
    uint8_t state = found == context->assertionControlStates.end()
                        ? 0
                        : found->second;
    if (action == 2) {
      state &= ~kAssertionLocked;
    } else if ((state & kAssertionLocked) != 0) {
      return OBELISK_RT_OK;
    } else {
      switch (action) {
      case 1:
        state |= kAssertionLocked;
        break;
      case 3:
        state &= ~kAssertionDisabled;
        break;
      case 4:
      case 5:
        state |= kAssertionDisabled;
        break;
      case 6:
        state &= ~(kAssertionNonvacuousPassDisabled |
                   kAssertionVacuousPassDisabled);
        break;
      case 7:
        state |= kAssertionNonvacuousPassDisabled |
                 kAssertionVacuousPassDisabled;
        break;
      case 8:
        state &= ~kAssertionFailDisabled;
        break;
      case 9:
        state |= kAssertionFailDisabled;
        break;
      case 10:
        state &= ~kAssertionNonvacuousPassDisabled;
        break;
      case 11:
        state |= kAssertionVacuousPassDisabled;
        break;
      default:
        break;
      }
    }
    if (state == 0)
      context->assertionControlStates.erase(assertionID);
    else
      context->assertionControlStates[assertionID] = state;
    if (action == 5)
      obelisk_rt_cancel_deferred_immediate_assertion_unlocked(context,
                                                              assertionID);
    return OBELISK_RT_OK;
  } catch (...) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
    return OBELISK_RT_OUT_OF_MEMORY;
  }
}

extern "C" uint32_t obelisk_rt_v1_assertion_enabled(obelisk_rt_context *context,
                                                    uint64_t assertionID) {
  if (!context || assertionID == 0)
    return 0;
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    auto found = context->assertionControlStates.find(assertionID);
    return found == context->assertionControlStates.end() ||
                   (found->second & kAssertionDisabled) == 0
               ? 1u
               : 0u;
  } catch (...) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
    return 0;
  }
}

extern "C" uint32_t
obelisk_rt_v1_assertion_action_state(obelisk_rt_context *context,
                                     uint64_t assertionID) {
  if (!context || assertionID == 0)
    return 0;
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    auto found = context->assertionControlStates.find(assertionID);
    uint8_t state = found == context->assertionControlStates.end()
                        ? 0
                        : found->second;
    uint32_t enabled = 7;
    if ((state & kAssertionNonvacuousPassDisabled) != 0)
      enabled &= ~UINT32_C(1);
    if ((state & kAssertionVacuousPassDisabled) != 0)
      enabled &= ~UINT32_C(2);
    if ((state & kAssertionFailDisabled) != 0)
      enabled &= ~UINT32_C(4);
    return enabled;
  } catch (...) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
    return 0;
  }
}

extern "C" uint64_t obelisk_rt_v1_deferred_enqueue_for_assertion(
    obelisk_rt_context *context, uint64_t siteID, uint64_t assertionID) {
  if (!context || siteID == 0)
    return 0;
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (context->activeLogicalProcessToken == 0)
      return 0;
    resetDeferredImmediateReportsForTime(context);
    uint64_t ticket = context->nextDeferredImmediateTicket++;
    if (ticket == 0)
      ticket = context->nextDeferredImmediateTicket++;
    auto &sites =
        context->latestDeferredImmediateReports
            [context->activeLogicalProcessToken];
    auto previous = sites.find(siteID);
    if (previous != sites.end())
      eraseDeferredImmediateReportUnlocked(context, previous->second);
    context->deferredImmediateReports.emplace(
        ticket, obelisk_rt_context::DeferredImmediateReport{
                    context->activeLogicalProcessToken, siteID, assertionID});
    sites[siteID] = ticket;
    if (assertionID != 0)
      context->deferredImmediateAssertionReports[assertionID].insert(ticket);
    return ticket;
  } catch (...) {
    if (context)
      context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
    return 0;
  }
}

extern "C" uint64_t obelisk_rt_v1_deferred_enqueue(obelisk_rt_context *context,
                                                   uint64_t siteID) {
  return obelisk_rt_v1_deferred_enqueue_for_assertion(context, siteID, 0);
}

extern "C" uint32_t obelisk_rt_v1_deferred_mature(obelisk_rt_context *context,
                                                  uint64_t ticket) {
  if (!context || ticket == 0)
    return 0;
  try {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    resetDeferredImmediateReportsForTime(context);
    auto report = context->deferredImmediateReports.find(ticket);
    if (report == context->deferredImmediateReports.end())
      return 0;
    auto process = context->latestDeferredImmediateReports.find(
        report->second.logicalProcess);
    bool current = false;
    if (process != context->latestDeferredImmediateReports.end()) {
      auto site = process->second.find(report->second.site);
      current = site != process->second.end() && site->second == ticket;
      if (current) {
        process->second.erase(site);
        if (process->second.empty())
          context->latestDeferredImmediateReports.erase(process);
      }
    }
    eraseDeferredImmediateReportUnlocked(context, ticket);
    return current ? 1u : 0u;
  } catch (...) {
    if (context)
      context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
    return 0;
  }
}

obelisk_rt_status makeBuffer(std::string_view source,
                             obelisk_rt_buffer_v1 *output) {
  if (!output)
    return OBELISK_RT_INVALID_ARGUMENT;
  output->data = nullptr;
  output->size = 0;
  if (source.empty())
    return OBELISK_RT_OK;
  void *memory = std::malloc(source.size());
  if (!memory)
    return OBELISK_RT_OUT_OF_MEMORY;
  std::memcpy(memory, source.data(), source.size());
  output->data = static_cast<uint8_t *>(memory);
  output->size = source.size();
  return OBELISK_RT_OK;
}

bool validBytes(const void *data, uint64_t size) {
  return size == 0 || data != nullptr;
}

std::string hostErrorMessage(int error) {
  std::lock_guard<std::recursive_mutex> lock(hostErrorMutex);
  const char *message = std::strerror(error);
  return message ? message : "unknown host error";
}

extern "C" obelisk_rt_status
obelisk_rt_v1_context_create(obelisk_rt_context **outContext) {
  return obelisk_rt_v1_context_create_for_design(nullptr, outContext);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_context_seed(obelisk_rt_context *context, uint64_t seed) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  return guarded(context, [&] {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    obelisk_rt_random_seed_context_unlocked(context, seed);
    // Generated executables configure argv after elaboration has registered
    // root processes. Re-split those dormant roots in their common lexical
    // insertion order so --seed governs their streams and native/bytecode
    // registration details cannot perturb the result.
    struct RootStream {
      uint64_t sequence;
      obelisk_rt_random_state_v1 *state;
    };
    std::vector<RootStream> roots;
    roots.reserve(context->scheduledProcesses.size() +
                  context->scheduledDesignTasks.size());
    for (ScheduledProcess &process : context->scheduledProcesses)
      if (process.parent == 0 && !process.started)
        roots.push_back({process.insertionSequence, &process.random});
    for (ScheduledDesignTask &task : context->scheduledDesignTasks)
      if (task.parent == 0 && !task.started)
        roots.push_back({task.insertionSequence, &task.random});
    std::sort(roots.begin(), roots.end(),
              [](const RootStream &left, const RootStream &right) {
                return left.sequence < right.sequence;
              });
    for (RootStream root : roots)
      obelisk_rt_random_split_unlocked(context, *root.state);
    return OBELISK_RT_OK;
  });
}

extern "C" obelisk_rt_status
obelisk_rt_v1_context_configure_argv(obelisk_rt_context *context, int argc,
                                     const char *const *argv) {
  if (!context || argc < 0 || (argc != 0 && !argv))
    return OBELISK_RT_INVALID_ARGUMENT;
  for (int index = 1; index < argc; ++index) {
    if (!argv[index])
      return OBELISK_RT_INVALID_ARGUMENT;
    std::string_view argument(argv[index]);
    if (!argument.empty() && argument.front() == '+') {
      context->plusargs.emplace_back(argument.substr(1));
      continue;
    }
    constexpr std::string_view prefix = "--seed=";
    if (argument.substr(0, prefix.size()) != prefix)
      continue;
    std::string_view digits = argument.substr(prefix.size());
    if (digits.empty())
      return OBELISK_RT_INVALID_ARGUMENT;
    uint64_t seed = 0;
    for (char digit : digits) {
      if (digit < '0' || digit > '9')
        return OBELISK_RT_INVALID_ARGUMENT;
      uint64_t value = static_cast<uint64_t>(digit - '0');
      if (seed > (UINT64_MAX - value) / 10)
        return OBELISK_RT_INVALID_ARGUMENT;
      seed = seed * 10 + value;
    }
    obelisk_rt_status status = obelisk_rt_v1_context_seed(context, seed);
    if (status != OBELISK_RT_OK)
      return status;
  }
  return OBELISK_RT_OK;
}

extern "C" uint32_t obelisk_rt_v1_import_id(const uint8_t *symbol,
                                            uint64_t symbolSize) {
  if (!validBytes(symbol, symbolSize) || symbolSize == 0)
    return 0;
  uint64_t hash = obelisk_stable_hash(symbol, symbolSize);
  uint32_t result = static_cast<uint32_t>(hash ^ (hash >> 32));
  return result == 0 ? 1 : result;
}

extern "C" obelisk_rt_status obelisk_rt_v1_context_register_import(
    obelisk_rt_context *context, uint32_t importID,
    obelisk_rt_import_callback_v1 callback, void *userData) {
  return obelisk_rt_v1_context_register_import_signature(context, importID, 0,
                                                         callback, userData);
}

extern "C" obelisk_rt_status obelisk_rt_v1_context_register_import_signature(
    obelisk_rt_context *context, uint32_t importID, uint64_t abiSignature,
    obelisk_rt_import_callback_v1 callback, void *userData) {
  if (!context || importID == 0 || !callback)
    return OBELISK_RT_INVALID_ARGUMENT;
  return guarded(context, [&] {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    context->imports[importID] = {callback, userData, abiSignature};
    return OBELISK_RT_OK;
  });
}

extern "C" obelisk_rt_status obelisk_rt_v1_context_create_for_design(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_context **outContext) {
  if (!outContext)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outContext = nullptr;
  try {
    DesignDatabaseCache designDatabase;
    obelisk::designbytecode::Image designBytecodeImage;
    if (execution) {
      constexpr uint32_t validFlags = OBELISK_RT_EXECUTION_HAS_BYTECODE |
                                      OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE |
                                      OBELISK_RT_EXECUTION_VPI_READ |
                                      OBELISK_RT_EXECUTION_VPI_WRITE |
                                      OBELISK_RT_EXECUTION_REQUIRE_BYTECODE |
                                      OBELISK_RT_EXECUTION_PREPONED_SNAPSHOT |
                                      OBELISK_RT_EXECUTION_WAVEFORM_METADATA;
      if (execution->version != OBELISK_RT_VERSION ||
          execution->dpi_reserved != 0 ||
          (execution->flags & ~validFlags) != 0 ||
          ((execution->flags & OBELISK_RT_EXECUTION_VPI_WRITE) != 0 &&
           (execution->flags & OBELISK_RT_EXECUTION_VPI_READ) == 0) ||
          ((execution->flags & OBELISK_RT_EXECUTION_REQUIRE_BYTECODE) != 0 &&
           (execution->flags & OBELISK_RT_EXECUTION_HAS_BYTECODE) == 0) ||
          ((execution->flags & OBELISK_RT_EXECUTION_WAVEFORM_METADATA) != 0 &&
           (execution->flags & OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE) ==
               0) ||
          ((execution->flags & OBELISK_RT_EXECUTION_HAS_BYTECODE) != 0
               ? (!execution->bytecode || execution->bytecode_size == 0 ||
                  execution->checksum == 0)
               : (execution->bytecode || execution->bytecode_size != 0 ||
                  execution->checksum != 0)) ||
          !validActivationInventory(*execution) ||
          !validObserverInventory(*execution) ||
          !validSampledRanges(*execution))
        return OBELISK_RT_INVALID_DESIGN;
      if ((execution->flags & OBELISK_RT_EXECUTION_HAS_BYTECODE) != 0) {
        obelisk_rt_status status = obelisk_rt_initialize_design_bytecode_image(
            *execution, designBytecodeImage);
        if (status != OBELISK_RT_OK)
          return status;
      }
      if ((execution->flags & OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE) == 0 &&
          (execution->design_database || execution->design_database_size != 0))
        return OBELISK_RT_INVALID_DESIGN;
      if ((execution->flags & OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE) != 0) {
        obelisk_rt_status status =
            obelisk_rt_initialize_design_database(execution, designDatabase);
        if (status != OBELISK_RT_OK)
          return status;
      }
    }
    auto *context = new obelisk_rt_context();
    context->execution = execution;
    context->designDatabase = designDatabase;
    context->designBytecodeImage = designBytecodeImage;
    context->designBytecodeImageValidated = designBytecodeImage.data != nullptr;
    if (execution) {
      obelisk_rt_status status =
          obelisk_rt_initialize_dpi_scopes(context, execution);
      if (status != OBELISK_RT_OK) {
        delete context;
        return status;
      }
    }
    if (execution && execution->state_bit_count != 0) {
      if (execution->state_bit_count > std::numeric_limits<size_t>::max() - 63)
        throw std::bad_alloc();
      size_t limbs =
          static_cast<size_t>((execution->state_bit_count + 63) / 64);
      context->stateValue.assign(limbs, 0);
      context->stateUnknown.assign(limbs, UINT64_MAX);
      unsigned tail = static_cast<unsigned>(execution->state_bit_count % 64);
      if (tail != 0)
        context->stateUnknown.back() &= (uint64_t{1} << tail) - 1;
    }
    if (execution &&
        (execution->flags & OBELISK_RT_EXECUTION_HAS_BYTECODE) != 0) {
      obelisk_rt_status status = obelisk_rt_initialize_design_state(context);
      if (status != OBELISK_RT_OK) {
        delete context;
        return status;
      }
    }
    if (context->designDatabase.validated) {
      obelisk_rt_status status = obelisk_rt_register_design_database(
          context->execution, context->designDatabase);
      if (status != OBELISK_RT_OK) {
        delete context;
        return status;
      }
      context->designDatabaseRegistered = true;
    }
    *outContext = context;
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_IO_ERROR;
  }
}

extern "C" void obelisk_rt_v1_context_destroy(obelisk_rt_context *context) {
  if (!context)
    return;
  std::unique_lock<std::recursive_mutex> transaction(context->transactionMutex);
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (context->transactionDepth != 0 &&
        context->transactionOwner == std::this_thread::get_id()) {
      context->destroyPending = true;
      return;
    }
  }
  transaction.unlock();
  destroyContextNow(context);
}

extern "C" const char *obelisk_rt_v1_status_string(obelisk_rt_status status) {
  switch (status) {
  case OBELISK_RT_OK:
    return "ok";
  case OBELISK_RT_EOF:
    return "end of file";
  case OBELISK_RT_INVALID_ARGUMENT:
    return "invalid argument";
  case OBELISK_RT_INVALID_HANDLE:
    return "invalid handle";
  case OBELISK_RT_IO_ERROR:
    return "I/O error";
  case OBELISK_RT_OUT_OF_MEMORY:
    return "out of memory";
  case OBELISK_RT_OUT_OF_RESOURCES:
    return "out of resources";
  case OBELISK_RT_FORMAT_ERROR:
    return "format error";
  case OBELISK_RT_ARGUMENT_MISMATCH:
    return "format argument mismatch";
  case OBELISK_RT_INVALID_BYTECODE:
    return "invalid bytecode";
  case OBELISK_RT_STEP_LIMIT:
    return "fragment step limit exceeded";
  case OBELISK_RT_LAYOUT_MISMATCH:
    return "process frame layout mismatch";
  case OBELISK_RT_INVALID_CONTINUATION:
    return "invalid process continuation";
  case OBELISK_RT_TIER_UNAVAILABLE:
    return "requested process tier unavailable";
  case OBELISK_RT_AOT_CHECKPOINT:
    return "native scheduler synchronization checkpoint";
  case OBELISK_RT_AOT_TIMED_CHECKPOINT:
    return "native scheduler timed synchronization checkpoint";
  case OBELISK_RT_AOT_GENERATED_CHECKPOINT:
    return "generated native scheduler branch checkpoint";
  case OBELISK_RT_INVALID_LIFECYCLE:
    return "invalid process lifecycle transition";
  case OBELISK_RT_INVALID_FRAME:
    return "invalid process frame record";
  case OBELISK_RT_INVALID_DESIGN:
    return "invalid design metadata";
  case OBELISK_RT_PERMISSION_DENIED:
    return "permission denied";
  case OBELISK_RT_DPI_DISABLE_UNSUPPORTED:
    return "DPI task disable is unsupported";
  case OBELISK_RT_FATAL:
    return "fatal SystemVerilog diagnostic";
  default:
    return "unknown runtime status";
  }
}

extern "C" void obelisk_rt_v1_buffer_release(obelisk_rt_buffer_v1 *buffer) {
  if (!buffer)
    return;
  std::free(buffer->data);
  buffer->data = nullptr;
  buffer->size = 0;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_last_error(obelisk_rt_context *context,
                         obelisk_rt_buffer_v1 *outMessage) {
  if (!context || !outMessage)
    return OBELISK_RT_INVALID_ARGUMENT;
  return guarded(context, [&] {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    auto error = threadErrors.find(context);
    if (error == threadErrors.end() ||
        error->second.contextLifetime.lock() != context->errorLifetime)
      return makeBuffer({}, outMessage);
    return makeBuffer(error->second.message, outMessage);
  });
}
