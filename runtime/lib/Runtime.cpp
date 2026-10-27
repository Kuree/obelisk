//===- Runtime.cpp - Talos native runtime context and buffers
//------------===//

#include "RuntimeInternal.h"
#include "talos/Runtime/StableHash.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <utility>

#if defined(__clang__) || defined(__GNUC__)
__attribute__((weak))
#endif
bool talos_rt_validate_dpi_exports(
    const talos_rt_execution_descriptor_v1 &execution,
    const talos_rt_execution_extension_v1 &extension) noexcept;

#if defined(__APPLE__)
// Mach-O extracts archive members even for weak references. DPIExport.cpp's
// strong definition replaces this one whenever a design links that object.
bool talos_rt_validate_dpi_exports(
    const talos_rt_execution_descriptor_v1 &,
    const talos_rt_execution_extension_v1 &) noexcept {
  return false;
}
#endif

namespace {

thread_local talos_rt_context *threadTransactionContext = nullptr;
thread_local uint32_t threadTransactionDepth = 0;

std::recursive_mutex hostErrorMutex;

struct ThreadError {
  std::weak_ptr<const uint8_t> contextLifetime;
  std::string message;
};

thread_local std::unordered_map<const talos_rt_context *, ThreadError>
    threadErrors;

bool validActivationInventory(
    const talos_rt_execution_descriptor_v1 &execution) {
  if ((execution.activation_count == 0) != (execution.activations == nullptr))
    return false;
  constexpr uint32_t validFlags =
      TALOS_RT_ACTIVATION_HAS_NATIVE | TALOS_RT_ACTIVATION_HAS_BYTECODE;
  uint64_t previousID = 0;
  for (uint64_t index = 0; index != execution.activation_count; ++index) {
    const talos_rt_activation_descriptor_v1 &activation =
        execution.activations[index];
    if (activation.code_unit_id == 0 ||
        (index != 0 && activation.code_unit_id <= previousID) ||
        activation.flags == 0 || (activation.flags & ~validFlags) != 0)
      return false;
    previousID = activation.code_unit_id;
    bool hasNative = (activation.flags & TALOS_RT_ACTIVATION_HAS_NATIVE) != 0;
    if (hasNative != (activation.native_entry != nullptr))
      return false;
    if (hasNative &&
        (activation.native_entry->handle.kind !=
             TALOS_RT_DESCRIPTOR_PROCESS ||
         activation.native_entry->handle.id != activation.code_unit_id ||
         activation.native_entry->version != TALOS_RT_VERSION ||
         activation.native_entry->execution != &execution))
      return false;
    bool hasBytecode =
        (activation.flags & TALOS_RT_ACTIVATION_HAS_BYTECODE) != 0;
    if (hasBytecode) {
      if ((execution.flags & TALOS_RT_EXECUTION_HAS_BYTECODE) == 0 ||
          activation.bytecode_function == TALOS_RT_ACTIVATION_NO_BYTECODE)
        return false;
    } else if (activation.bytecode_function !=
               TALOS_RT_ACTIVATION_NO_BYTECODE) {
      return false;
    }
  }
  return true;
}

bool validObserverInventory(
    const talos_rt_execution_descriptor_v1 &execution) {
  if ((execution.observer_count == 0) != (execution.observers == nullptr))
    return false;
  uint64_t previousID = 0;
  for (uint64_t index = 0; index != execution.observer_count; ++index) {
    const talos_rt_observer_descriptor_v1 &observer =
        execution.observers[index];
    if (observer.code_unit_id == 0 ||
        (index != 0 && observer.code_unit_id <= previousID) ||
        observer.result_width == 0 ||
        (observer.flags &
         ~(TALOS_RT_OBSERVER_FOUR_STATE | TALOS_RT_OBSERVER_REAL32 |
           TALOS_RT_OBSERVER_REAL64)) != 0 ||
        ((observer.flags & TALOS_RT_OBSERVER_REAL32) != 0 &&
         (observer.result_width != 32 ||
          (observer.flags & (TALOS_RT_OBSERVER_FOUR_STATE |
                             TALOS_RT_OBSERVER_REAL64)) != 0)) ||
        ((observer.flags & TALOS_RT_OBSERVER_REAL64) != 0 &&
         (observer.result_width != 64 ||
          (observer.flags & (TALOS_RT_OBSERVER_FOUR_STATE |
                             TALOS_RT_OBSERVER_REAL32)) != 0)) ||
        observer.reserved != 0 ||
        (observer.capture_count == 0) != (observer.capture_abi == nullptr))
      return false;
    previousID = observer.code_unit_id;
    for (uint32_t capture = 0; capture != observer.capture_count; ++capture) {
      const talos_rt_observer_capture_abi_v1 &abi =
          observer.capture_abi[capture];
      if (abi.kind < TALOS_RT_OBSERVER_CAPTURE_STORAGE ||
          abi.kind > TALOS_RT_OBSERVER_CAPTURE_ARGUMENT_REF ||
          abi.width == 0 ||
          (abi.kind == TALOS_RT_OBSERVER_CAPTURE_EVENT && abi.width != 1) ||
          ((abi.kind == TALOS_RT_OBSERVER_CAPTURE_MANAGED ||
            abi.kind == TALOS_RT_OBSERVER_CAPTURE_COVERGROUP) &&
           abi.width != 64))
        return false;
    }
    bool hasBytecode =
        observer.bytecode_function != TALOS_RT_OBSERVER_NO_BYTECODE;
    if (hasBytecode &&
        (execution.flags & TALOS_RT_EXECUTION_HAS_BYTECODE) == 0)
      return false;
    if (!observer.native_evaluator && !hasBytecode)
      return false;
  }
  return true;
}

const talos_rt_execution_extension_v1 *
executionExtension(const talos_rt_execution_descriptor_v1 &execution) {
  uintptr_t base = reinterpret_cast<uintptr_t>(&execution);
  if (execution.reserved < sizeof(execution) ||
      execution.reserved > std::numeric_limits<uintptr_t>::max() - base)
    return nullptr;
  uintptr_t address = base + static_cast<uintptr_t>(execution.reserved);
  if (address % alignof(talos_rt_execution_extension_v1) != 0)
    return nullptr;
  return reinterpret_cast<const talos_rt_execution_extension_v1 *>(address);
}

bool validExecutionExtension(
    const talos_rt_execution_descriptor_v1 &execution) {
  bool sampled =
      (execution.flags & TALOS_RT_EXECUTION_PREPONED_SNAPSHOT) != 0;
  bool exported = (execution.flags & TALOS_RT_EXECUTION_DPI_EXPORTS) != 0;
  bool classBitstream =
      (execution.flags & TALOS_RT_EXECUTION_CLASS_BITSTREAM) != 0;
  bool coverage = (execution.flags & TALOS_RT_EXECUTION_COVERAGE_SCHEMA) != 0;
  if (!sampled && !exported && !classBitstream && !coverage)
    return execution.reserved == 0;
  const talos_rt_execution_extension_v1 *extension =
      executionExtension(execution);
  if (!extension)
    return false;
  if (extension->version != TALOS_RT_EXECUTION_EXTENSION_VERSION ||
      extension->size != sizeof(*extension))
    return false;
  if (coverage) {
    if (!extension->coverage_schema ||
        extension->coverage_schema_size < talos::coverage::HeaderSize +
                                              talos::coverage::ChecksumSize ||
        extension->coverage_schema_size > SIZE_MAX)
      return false;
    if (std::memcmp(extension->coverage_schema, talos::coverage::Magic,
                    sizeof(talos::coverage::Magic)) != 0)
      return false;
  }
  if (coverage != (extension->coverage_schema != nullptr) ||
      coverage != (extension->coverage_schema_size != 0) ||
      classBitstream != (extension->class_bitstream != nullptr) ||
      classBitstream != (extension->class_bitstream_size != 0) ||
      exported != (extension->exports != nullptr) ||
      exported != (extension->export_count != 0))
    return false;
  if (sampled != (extension->sampled_ranges != nullptr) ||
      sampled != (extension->sampled_range_count != 0))
    return false;
  uint64_t previousEnd = 0;
  uint64_t snapshotBytes = 0;
  for (uint64_t index = 0; index != extension->sampled_range_count; ++index) {
    const talos_rt_sampled_range_v1 &range = extension->sampled_ranges[index];
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
  if (snapshotBytes > std::numeric_limits<size_t>::max() ||
      snapshotBytes > UINT64_MAX / 8)
    return false;
  return !exported || (talos_rt_validate_dpi_exports &&
                       talos_rt_validate_dpi_exports(execution, *extension));
}

void seedContextUnlocked(talos_rt_context *context, uint64_t seed) {
  // Allocate the complete work list before changing any random state.  This
  // makes reseeding transactional even when allocation fails.
  struct RootStream {
    uint64_t sequence;
    uint64_t initializationScope;
    talos_rt_random_state_v1 *state;
  };
  std::vector<RootStream> roots;
  roots.reserve(context->scheduledProcesses.size() +
                context->scheduledDesignTasks.size());
  for (ScheduledProcess &process : context->scheduledProcesses)
    if (process.parent == 0 && !process.started)
      roots.push_back({process.insertionSequence,
                       process.initializationRandomScope, &process.random});
  for (ScheduledDesignTask &task : context->scheduledDesignTasks)
    if (task.parent == 0 && !task.started)
      roots.push_back({task.insertionSequence, task.initializationRandomScope,
                       &task.random});
  std::sort(roots.begin(), roots.end(),
            [](const RootStream &left, const RootStream &right) {
              return left.sequence < right.sequence;
            });

  talos_rt_random_seed_context_unlocked(context, seed);
  context->configuredSeed = seed;
  if (context->initializationRandomScopes)
    for (auto &[scope, state] : context->initializationRandomScopes->streams)
      talos_rt_v1_random_state_seed(&state, seed,
                                      TALOS_RT_RANDOM_DEFAULT_INCREMENT >> 1);
  for (RootStream root : roots)
    talos_rt_random_split_initialization_unlocked(
        context, root.initializationScope, *root.state);
}

talos_rt_status mapCoverageStatus(talos::coverage::Status status) {
  switch (status) {
  case talos::coverage::Status::Ok:
    return TALOS_RT_OK;
  case talos::coverage::Status::OutOfMemory:
    return TALOS_RT_OUT_OF_MEMORY;
  case talos::coverage::Status::IoError:
    return TALOS_RT_IO_ERROR;
  default:
    return TALOS_RT_INVALID_DESIGN;
  }
}

bool coverageSchemaCounts(const talos::coverage::Database &schema,
                          uint64_t &lineCount, uint64_t &toggleBitCount) {
  lineCount = schema.linePoints.size();
  toggleBitCount = 0;
  for (const talos::coverage::ToggleObject &object : schema.toggleObjects) {
    if (object.bitWidth > UINT64_MAX - toggleBitCount)
      return false;
    toggleBitCount += object.bitWidth;
  }
  return true;
}

} // namespace

talos_rt_context::talos_rt_context() {
  if (const char *diagnostics = std::getenv("TALOS_RT_SIGNAL_DIAGNOSTICS")) {
    signalDiagnosticsEnabled =
        diagnostics[0] != '\0' && std::strcmp(diagnostics, "0") != 0;
    signalDiagnosticsReport = signalDiagnosticsEnabled;
  }
  errorLifetime = std::make_shared<const uint8_t>(0);
  managedHeap = talos_rt_managed_heap_create(this);
  // A small threshold makes the collector run often, which tests use to
  // exercise every collection point.
  if (const char *threshold = std::getenv("TALOS_RT_GC_THRESHOLD"))
    if (uint64_t bytes = std::strtoull(threshold, nullptr, 0))
      talos_rt_v1_gc_set_threshold(this, bytes);
  mcd[0].stream = stdout;
  mcd[0].writable = true;
  mcd[0].name = "stdout";
  files.resize(3);
  files[0] = {stdin, 0, false};
  files[1] = {stdout, 0, true};
  files[2] = {stderr, 0, true};
  files[0].name = "stdin";
  files[1].name = "stdout";
  files[2].name = "stderr";
  for (uint32_t bit = 30; bit >= 1; --bit)
    freeMCDs.push_back(bit);
  talos_rt_random_seed_context_unlocked(this, 1);
}

void talos_rt_report_signal_diagnostics_unlocked(
    talos_rt_context *context) {
  if (!context || !context->signalDiagnosticsReport)
    return;
  std::fprintf(
      stderr,
      "talos-signal-diagnostics publications=%llu "
      "subscriptions_current=%llu subscriptions_high_water=%llu "
      "subscribers_examined=%llu readiness_calls=%llu "
      "candidate_scans=%llu scheduler_iterations=%llu "
      "fallback_rescans=%llu aot_node_executions=%llu "
      "aot_region_passes=%llu aot_fanout_entries=%llu "
      "aot_nba_stages=%llu aot_nba_commits=%llu "
      "aot_nba_batches=%llu aot_nba_private_commits=%llu "
      "aot_state_fast_paths=%llu aot_state_slow_paths=%llu "
      "aot_deadline_high_water=%llu aot_fallbacks=%llu "
      "aot_checkpoints=%llu aot_terminal_checkpoints=%llu "
      "candidate_inventory_visits=%llu eval_dispatches=%llu "
      "periodic_preparations=%llu periodic_clocks_high_water=%llu "
      "periodic_slot_drains=%llu\n",
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
      static_cast<unsigned long long>(context->signalDiagnostics.aotNBABatches),
      static_cast<unsigned long long>(
          context->signalDiagnostics.aotNBAPrivateCommits),
      static_cast<unsigned long long>(
          context->signalDiagnostics.aotStateFastPaths),
      static_cast<unsigned long long>(
          context->signalDiagnostics.aotStateSlowPaths),
      static_cast<unsigned long long>(
          context->signalDiagnostics.aotDeadlineHighWater),
      static_cast<unsigned long long>(context->signalDiagnostics.aotFallbacks),
      static_cast<unsigned long long>(
          context->signalDiagnostics.aotCheckpoints),
      static_cast<unsigned long long>(
          context->signalDiagnostics.aotTerminalCheckpoints),
      static_cast<unsigned long long>(
          context->signalDiagnostics.candidateInventoryVisits),
      static_cast<unsigned long long>(
          context->signalDiagnostics.evalDispatches),
      static_cast<unsigned long long>(
          context->signalDiagnostics.periodicPreparations),
      static_cast<unsigned long long>(
          context->signalDiagnostics.periodicClocksHighWater),
      static_cast<unsigned long long>(
          context->signalDiagnostics.periodicSlotDrains));
  if (const NativeUnknownSummary *summary =
          context->nativeUnknownSummary.get()) {
    uint64_t dirty = 0;
    for (uint64_t root = 0; root != summary->roots.size(); ++root)
      dirty += (summary->generated[root / 64] >> (root % 64)) & 1;
    std::fprintf(
        stderr,
        "talos-unknown-summary roots=%llu dirty=%llu refreshes=%llu "
        "rescans=%llu cleared=%llu marked_all=%llu marked_roots=%llu\n",
        static_cast<unsigned long long>(summary->roots.size()),
        static_cast<unsigned long long>(dirty),
        static_cast<unsigned long long>(summary->refreshes),
        static_cast<unsigned long long>(summary->rescans),
        static_cast<unsigned long long>(summary->cleared),
        static_cast<unsigned long long>(summary->markedAll),
        static_cast<unsigned long long>(summary->markedRoots));
  }
  for (unsigned slot = 0; slot != 64; ++slot)
    if (context->signalDiagnostics.aotActorExecutions[slot] != 0)
      std::fprintf(stderr, "talos-aot-actor slot=%u executions=%llu\n", slot,
                   static_cast<unsigned long long>(
                       context->signalDiagnostics.aotActorExecutions[slot]));
}

talos_rt_context::~talos_rt_context() {
  if (designDatabaseRegistered)
    talos_rt_unregister_design_database(execution);
  talos_rt_report_signal_diagnostics_unlocked(this);
  talos_rt_release_native_schedule_plan(this);
  if (dynamicScanState && dynamicScanState->destroy)
    dynamicScanState->destroy(dynamicScanState);
  if (classBitstreamState && classBitstreamState->destroy)
    classBitstreamState->destroy(classBitstreamState);
  if (recursiveWatchGroups && recursiveWatchGroups->destroy)
    recursiveWatchGroups->destroy(recursiveWatchGroups);
  threadErrors.erase(this);
  if (const char *diagnostics = std::getenv("TALOS_RT_GC_DIAGNOSTICS");
      diagnostics && diagnostics[0] != '\0' &&
      std::strcmp(diagnostics, "0") != 0) {
    talos_rt_gc_statistics_v1 statistics{};
    talos_rt_v1_gc_statistics(this, &statistics);
    std::fprintf(
        stderr,
        "talos-gc-diagnostics collections=%llu designated=%llu "
        "allocated_objects=%llu reclaimed_objects=%llu polled=%d\n",
        static_cast<unsigned long long>(statistics.collection_count),
        static_cast<unsigned long long>(
            talos_rt_managed_heap_designated_collections(managedHeap)),
        static_cast<unsigned long long>(statistics.allocated_objects),
        static_cast<unsigned long long>(statistics.reclaimed_objects),
        talos_rt_managed_heap_collections_polled(managedHeap) ? 1 : 0);
  }
  talos_rt_managed_heap_destroy(managedHeap);
}

namespace {

void destroyContextNow(talos_rt_context *context) noexcept {
  talos_rt_retire_dispatch_memos();
  std::vector<ScheduledProcess> processes;
  TALOS_RT_TRY {
    // Settle the final time slot before the state planes go away.
    talos_rt_dump_destroy(context);
    if (context->vpiState)
      talos_rt_v1_vpi_shutdown(context);
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      for (ScheduledProcess &scheduled : context->scheduledProcesses)
        talos_rt_unregister_signal_wait_unlocked(
            context, scheduled.signalSubscriptions);
      for (ScheduledDesignTask &task : context->scheduledDesignTasks)
        talos_rt_unregister_signal_wait_unlocked(context,
                                                   task.signalSubscriptions);
      processes.swap(context->scheduledProcesses);
      context->scheduledProcessTokens.clear();
    }
    for (ScheduledProcess &scheduled : processes) {
      if (scheduled.instance)
        (void)talos_rt_v1_process_instance_destroy(scheduled.instance);
      for (talos_rt_process_instance_v1 *caller : scheduled.callers)
        (void)talos_rt_v1_process_instance_destroy(caller);
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
  }
  TALOS_RT_CATCH_ALL {}
  if (context->nativeStateSpecializationFast)
    *context->nativeStateSpecializationFast = 0;
  delete context;
}

} // namespace

ContextTransaction::ContextTransaction(talos_rt_context *context)
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
  TALOS_RT_TRY {
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
  }
  TALOS_RT_CATCH_ALL {
    // Transaction teardown must not replace the operation's status.
  }
}

void setLastErrorUnlocked(talos_rt_context *context, std::string message) {
  for (auto error = threadErrors.begin(); error != threadErrors.end();) {
    if (error->second.contextLifetime.expired())
      error = threadErrors.erase(error);
    else
      ++error;
  }
  threadErrors[context] = {context->errorLifetime, std::move(message)};
}

void setLastError(talos_rt_context *context, std::string message) {
  if (!context)
    return;
  TALOS_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    setLastErrorUnlocked(context, std::move(message));
  }
  TALOS_RT_CATCH_ALL {}
}

void talos_rt_retain_controls_unlocked(
    talos_rt_context *context, const std::vector<uint64_t> &controls) {
  for (uint64_t token : controls)
    if (auto found = context->controlActivations.find(token);
        found != context->controlActivations.end())
      ++found->second.memberships;
}

void talos_rt_release_controls_unlocked(
    talos_rt_context *context, const std::vector<uint64_t> &controls) {
  for (uint64_t token : controls)
    talos_rt_release_control_unlocked(context, token);
}

void talos_rt_release_control_unlocked(talos_rt_context *context,
                                         uint64_t token) {
  auto found = context->controlActivations.find(token);
  if (found == context->controlActivations.end())
    return;
  if (found->second.memberships != 0)
    --found->second.memberships;
  if (found->second.memberships == 0)
    context->controlActivations.erase(found);
}

extern "C" talos_rt_status
talos_rt_v1_control_enter(talos_rt_context *context, uint64_t targetID,
                            uint64_t *outActivation) {
  if (!context || targetID == 0 || !outActivation)
    return TALOS_RT_INVALID_ARGUMENT;
  *outActivation = 0;
  return guarded(context, [&] {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (context->activeLogicalProcessToken == 0 ||
        context->nextControlActivation == 0)
      return TALOS_RT_INVALID_LIFECYCLE;
    uint64_t frameDepth = 0;
    if (context->vpiState) {
      auto status = talos_rt_vpi_frame_depth_process(
          context, context->activeLogicalProcessToken, &frameDepth);
      if (status != TALOS_RT_OK)
        return status;
    }
    uint64_t token = context->nextControlActivation;
    context->activeControls.reserve(context->activeControls.size() + 1);
    auto [_, inserted] = context->controlActivations.emplace(
        token,
        ControlActivation{targetID, 1, context->activeLogicalProcessToken, 0,
                          frameDepth});
    if (!inserted)
      return TALOS_RT_INVALID_LIFECYCLE;
    context->activeControls.push_back(token);
    ++context->nextControlActivation;
    *outActivation = token;
    return TALOS_RT_OK;
  });
}

extern "C" talos_rt_status
talos_rt_v1_control_boundary(talos_rt_context *context, uint64_t activation,
                               uint32_t continuation) {
  if (!context || activation == 0 || continuation == 0)
    return TALOS_RT_INVALID_ARGUMENT;
  return guarded(context, [&] {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (context->activeLogicalProcessToken == 0)
      return TALOS_RT_INVALID_LIFECYCLE;
    auto found = context->controlActivations.find(activation);
    if (found == context->controlActivations.end() ||
        found->second.owner != context->activeLogicalProcessToken ||
        found->second.continuation != 0 || context->activeControls.empty() ||
        context->activeControls.back() != activation)
      return TALOS_RT_INVALID_LIFECYCLE;
    found->second.continuation = continuation;
    return TALOS_RT_OK;
  });
}

extern "C" talos_rt_status
talos_rt_v1_control_leave(talos_rt_context *context, uint64_t activation) {
  if (!context || activation == 0)
    return TALOS_RT_INVALID_ARGUMENT;
  return guarded(context, [&] {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (context->activeLogicalProcessToken == 0 ||
        context->activeControls.empty() ||
        context->activeControls.back() != activation)
      return TALOS_RT_INVALID_LIFECYCLE;
    context->activeControls.pop_back();
    talos_rt_release_control_unlocked(context, activation);
    return TALOS_RT_OK;
  });
}

extern "C" uint32_t
talos_rt_v1_control_escape_pending(talos_rt_context *context) {
  if (!context)
    return 0;
  TALOS_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (context->activeLogicalProcessToken == 0)
      return 0;
    bool pending = context->controlEscapePending;
    context->controlEscapePending = false;
    return pending ? 1u : 0u;
  }
  TALOS_RT_CATCH_ALL { return 0; }
}

extern "C" uint32_t talos_rt_v1_static_once(talos_rt_context *context,
                                              uint64_t siteID) {
  if (!context || siteID == 0)
    return 0;
  TALOS_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    return context->initializedStaticSites.insert(siteID).second ? 1u : 0u;
  }
  TALOS_RT_CATCH_ALL {
    if (context)
      context->schedulerStatus = TALOS_RT_OUT_OF_MEMORY;
    return 0;
  }
}

extern "C" uint32_t talos_rt_v1_deferred_once(talos_rt_context *context,
                                                uint64_t siteID) {
  if (!context || siteID == 0)
    return 0;
  TALOS_RT_TRY {
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
  }
  TALOS_RT_CATCH_ALL {
    if (context)
      context->schedulerStatus = TALOS_RT_OUT_OF_MEMORY;
    return 0;
  }
}

static void resetDeferredImmediateReportsForTime(talos_rt_context *context) {
  if (context->deferredImmediateTime == context->schedulerTime)
    return;
  context->deferredImmediateSites.clear();
  context->deferredImmediateReports.clear();
  context->deferredImmediateProcessReports.clear();
  context->deferredImmediateAssertionReports.clear();
  context->generatedDeferredReports = 0;
  context->deferredImmediateTime = context->schedulerTime;
}

static void eraseDeferredImmediateReportUnlocked(talos_rt_context *context,
                                                 uint64_t ticket) {
  auto report = context->deferredImmediateReports.find(ticket);
  if (report == context->deferredImmediateReports.end())
    return;
  if (report->second.logicalProcess == 0 &&
      context->generatedDeferredReports != 0)
    --context->generatedDeferredReports;
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

void talos_rt_flush_deferred_immediate_reports_unlocked(
    talos_rt_context *context, uint64_t logicalProcess) {
  if (!context || logicalProcess == 0)
    return;
  resetDeferredImmediateReportsForTime(context);
  auto process = context->deferredImmediateProcessReports.find(logicalProcess);
  if (process == context->deferredImmediateProcessReports.end())
    return;
  for (uint64_t ticket : process->second)
    eraseDeferredImmediateReportUnlocked(context, ticket);
  context->deferredImmediateProcessReports.erase(process);
}

bool talos_rt_cancel_deferred_immediate_assertion_unlocked(
    talos_rt_context *context, uint64_t assertionID,
    uint64_t logicalProcess) {
  if (!context || assertionID == 0)
    return false;
  resetDeferredImmediateReportsForTime(context);
  auto assertion = context->deferredImmediateAssertionReports.find(assertionID);
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
    auto process = context->deferredImmediateProcessReports.find(
        report->second.logicalProcess);
    if (process != context->deferredImmediateProcessReports.end()) {
      process->second.erase(ticket);
      if (process->second.empty())
        context->deferredImmediateProcessReports.erase(process);
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

extern "C" talos_rt_status
talos_rt_v1_assertion_control(talos_rt_context *context, uint32_t action,
                                uint64_t assertionID) {
  if (!context || assertionID == 0 || action < 1 || action > 11)
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    auto found = context->assertionControlStates.find(assertionID);
    uint8_t state =
        found == context->assertionControlStates.end() ? 0 : found->second;
    bool killApplied = false;
    if (action == 2) {
      state &= ~kAssertionLocked;
    } else if ((state & kAssertionLocked) != 0) {
      return TALOS_RT_OK;
    } else {
      switch (action) {
      case 1:
        state |= kAssertionLocked;
        break;
      case 3:
        state &= ~kAssertionDisabled;
        break;
      case 4:
        state |= kAssertionDisabled;
        break;
      case 5:
        state |= kAssertionDisabled;
        killApplied = true;
        break;
      case 6:
        state &=
            ~(kAssertionNonvacuousPassDisabled | kAssertionVacuousPassDisabled);
        break;
      case 7:
        state |=
            kAssertionNonvacuousPassDisabled | kAssertionVacuousPassDisabled;
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
    if (killApplied) {
      ++context->assertionKillEpochs[assertionID];
      talos_rt_cancel_deferred_immediate_assertion_unlocked(context,
                                                              assertionID);
    }
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH_ALL {
    context->schedulerStatus = TALOS_RT_OUT_OF_MEMORY;
    return TALOS_RT_OUT_OF_MEMORY;
  }
}

extern "C" uint32_t talos_rt_v1_assertion_enabled(talos_rt_context *context,
                                                    uint64_t assertionID) {
  if (!context || assertionID == 0)
    return 0;
  TALOS_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    auto found = context->assertionControlStates.find(assertionID);
    return found == context->assertionControlStates.end() ||
                   (found->second & kAssertionDisabled) == 0
               ? 1u
               : 0u;
  }
  TALOS_RT_CATCH_ALL {
    context->schedulerStatus = TALOS_RT_OUT_OF_MEMORY;
    return 0;
  }
}

extern "C" uint32_t
talos_rt_v1_assertion_action_state(talos_rt_context *context,
                                     uint64_t assertionID) {
  if (!context || assertionID == 0)
    return 0;
  TALOS_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    auto found = context->assertionControlStates.find(assertionID);
    uint8_t state =
        found == context->assertionControlStates.end() ? 0 : found->second;
    uint32_t enabled = 7;
    if ((state & kAssertionNonvacuousPassDisabled) != 0)
      enabled &= ~UINT32_C(1);
    if ((state & kAssertionVacuousPassDisabled) != 0)
      enabled &= ~UINT32_C(2);
    if ((state & kAssertionFailDisabled) != 0)
      enabled &= ~UINT32_C(4);
    return enabled;
  }
  TALOS_RT_CATCH_ALL {
    context->schedulerStatus = TALOS_RT_OUT_OF_MEMORY;
    return 0;
  }
}

extern "C" uint64_t
talos_rt_v1_assertion_kill_epoch(talos_rt_context *context,
                                   uint64_t assertionID) {
  if (!context || assertionID == 0)
    return 0;
  TALOS_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    auto found = context->assertionKillEpochs.find(assertionID);
    return found == context->assertionKillEpochs.end() ? 0 : found->second;
  }
  TALOS_RT_CATCH_ALL {
    context->schedulerStatus = TALOS_RT_OUT_OF_MEMORY;
    return 0;
  }
}

extern "C" uint64_t talos_rt_v1_deferred_enqueue_for_assertion(
    talos_rt_context *context, uint64_t siteID, uint64_t assertionID) {
  if (!context || siteID == 0)
    return 0;
  TALOS_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    // IEEE 1800-2023 16.4.1: the report belongs to the executing process. A
    // generated activation runs outside any runtime process; its report
    // carries a compiler-maintained flush epoch instead (16.4.2), so it is
    // queued under no process and only a kill or a new time step cancels it.
    resetDeferredImmediateReportsForTime(context);
    uint64_t ticket = context->nextDeferredImmediateTicket++;
    if (ticket == 0)
      ticket = context->nextDeferredImmediateTicket++;
    uint64_t logicalProcess = context->activeLogicalProcessToken;
    if (logicalProcess == 0)
      ++context->generatedDeferredReports;
    context->deferredImmediateReports.emplace(
        ticket, talos_rt_context::DeferredImmediateReport{logicalProcess,
                                                            assertionID});
    context->deferredImmediateProcessReports[logicalProcess].insert(ticket);
    if (assertionID != 0)
      context->deferredImmediateAssertionReports[assertionID].insert(ticket);
    return ticket;
  }
  TALOS_RT_CATCH_ALL {
    if (context)
      context->schedulerStatus = TALOS_RT_OUT_OF_MEMORY;
    return 0;
  }
}

extern "C" uint64_t talos_rt_v1_deferred_enqueue(talos_rt_context *context,
                                                   uint64_t siteID) {
  return talos_rt_v1_deferred_enqueue_for_assertion(context, siteID, 0);
}

extern "C" uint32_t talos_rt_v1_deferred_mature(talos_rt_context *context,
                                                  uint64_t ticket) {
  if (!context || ticket == 0)
    return 0;
  TALOS_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    resetDeferredImmediateReportsForTime(context);
    auto report = context->deferredImmediateReports.find(ticket);
    if (report == context->deferredImmediateReports.end())
      return 0;
    auto process = context->deferredImmediateProcessReports.find(
        report->second.logicalProcess);
    bool pending = false;
    if (process != context->deferredImmediateProcessReports.end()) {
      pending = process->second.erase(ticket) != 0;
      if (process->second.empty())
        context->deferredImmediateProcessReports.erase(process);
    }
    eraseDeferredImmediateReportUnlocked(context, ticket);
    return pending ? 1u : 0u;
  }
  TALOS_RT_CATCH_ALL {
    if (context)
      context->schedulerStatus = TALOS_RT_OUT_OF_MEMORY;
    return 0;
  }
}

talos_rt_status makeBuffer(std::string_view source,
                             talos_rt_buffer_v1 *output) {
  if (!output)
    return TALOS_RT_INVALID_ARGUMENT;
  output->data = nullptr;
  output->size = 0;
  if (source.empty())
    return TALOS_RT_OK;
  void *memory = std::malloc(source.size());
  if (!memory)
    return TALOS_RT_OUT_OF_MEMORY;
  std::memcpy(memory, source.data(), source.size());
  output->data = static_cast<uint8_t *>(memory);
  output->size = source.size();
  return TALOS_RT_OK;
}

bool validBytes(const void *data, uint64_t size) {
  return size == 0 || data != nullptr;
}

std::string hostErrorMessage(int error) {
  std::lock_guard<std::recursive_mutex> lock(hostErrorMutex);
  const char *message = std::strerror(error);
  return message ? message : "unknown host error";
}

extern "C" talos_rt_status
talos_rt_v1_context_create(talos_rt_context **outContext) {
  return talos_rt_v1_context_create_for_design(nullptr, outContext);
}

extern "C" talos_rt_status
talos_rt_v1_context_seed(talos_rt_context *context, uint64_t seed) {
  if (!context)
    return TALOS_RT_INVALID_ARGUMENT;
  return guarded(context, [&] {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    // Generated executables configure argv after elaboration has registered
    // root processes. Re-split those dormant roots in their common lexical
    // insertion order so --seed governs their streams and native/bytecode
    // registration details cannot perturb the result.
    seedContextUnlocked(context, seed);
    return TALOS_RT_OK;
  });
}

extern "C" talos_rt_status
talos_rt_v1_context_configure_argv(talos_rt_context *context, int argc,
                                     const char *const *argv) {
  if (!context || argc < 0 || (argc != 0 && !argv))
    return TALOS_RT_INVALID_ARGUMENT;
  for (int index = 1; index < argc; ++index)
    if (!argv[index])
      return TALOS_RT_INVALID_ARGUMENT;

  return guarded(context, [&] {
    std::vector<std::string> plusargs;
    std::vector<std::string> vpiArguments;
    std::vector<PlusargIndexNode> nodes;
    std::vector<PlusargIndexEdge> edges;
    size_t plusargCount = 0;
    bool haveSeed = false;
    uint64_t configuredSeed = 0;
    bool haveCoverageOptions = false;
    std::string coverageOutput = "coverage.obcov";
    std::string coverageTest;
    std::vector<std::pair<std::string, std::string>> coverageTags;
    std::vector<std::string> coverageLoads;
    bool suppressCoverageDump = false;
    std::unordered_set<std::string> coverageTagKeys;

    // Validate and size the replacement before touching the live context.
    for (int index = 1; index < argc; ++index) {
      std::string_view argument(argv[index]);
      if (!argument.empty() && argument.front() == '+') {
        ++plusargCount;
        continue;
      }
      constexpr std::string_view prefix = "--seed=";
      if (argument.substr(0, prefix.size()) == prefix) {
        std::string_view digits = argument.substr(prefix.size());
        if (digits.empty())
          return TALOS_RT_INVALID_ARGUMENT;
        uint64_t seed = 0;
        for (char digit : digits) {
          if (digit < '0' || digit > '9')
            return TALOS_RT_INVALID_ARGUMENT;
          uint64_t value = static_cast<uint64_t>(digit - '0');
          if (seed > (UINT64_MAX - value) / 10)
            return TALOS_RT_INVALID_ARGUMENT;
          seed = seed * 10 + value;
        }
        configuredSeed = seed;
        haveSeed = true;
        continue;
      }
      constexpr std::string_view outputPrefix = "--coverage-output=";
      constexpr std::string_view testPrefix = "--coverage-test=";
      constexpr std::string_view tagPrefix = "--coverage-tag=";
      constexpr std::string_view loadPrefix = "--coverage-load=";
      if (argument.substr(0, outputPrefix.size()) == outputPrefix) {
        std::string_view value = argument.substr(outputPrefix.size());
        if (value.empty())
          return TALOS_RT_INVALID_ARGUMENT;
        coverageOutput.assign(value);
        haveCoverageOptions = true;
      } else if (argument.substr(0, testPrefix.size()) == testPrefix) {
        std::string_view value = argument.substr(testPrefix.size());
        if (value.empty())
          return TALOS_RT_INVALID_ARGUMENT;
        coverageTest.assign(value);
        haveCoverageOptions = true;
      } else if (argument.substr(0, tagPrefix.size()) == tagPrefix) {
        std::string_view value = argument.substr(tagPrefix.size());
        size_t equal = value.find('=');
        if (equal == std::string_view::npos || equal == 0 ||
            !coverageTagKeys.insert(std::string(value.substr(0, equal))).second)
          return TALOS_RT_INVALID_ARGUMENT;
        coverageTags.emplace_back(value.substr(0, equal),
                                  value.substr(equal + 1));
        haveCoverageOptions = true;
      } else if (argument.substr(0, loadPrefix.size()) == loadPrefix) {
        std::string_view value = argument.substr(loadPrefix.size());
        if (value.empty())
          return TALOS_RT_INVALID_ARGUMENT;
        coverageLoads.emplace_back(value);
        haveCoverageOptions = true;
      } else if (argument == "--no-coverage-dump") {
        suppressCoverageDump = true;
        haveCoverageOptions = true;
      }
    }
    plusargs.reserve(plusargCount);

    const bool retainVPIArguments =
        context->execution &&
        (context->execution->flags & TALOS_RT_EXECUTION_VPI_READ) != 0;
    if (retainVPIArguments) {
      vpiArguments.reserve(static_cast<size_t>(argc));
      for (int index = 0; index < argc; ++index)
        vpiArguments.emplace_back(argv[index] ? argv[index] : "talos");
    }

    for (int index = 1; index < argc; ++index) {
      std::string_view argument(argv[index]);
      if (argument.empty() || argument.front() != '+')
        continue;
      plusargs.emplace_back(argument.substr(1));
    }

    // Parse every requested database before touching the context.  A bad
    // later --coverage-load must not leave an earlier load, seed, plusarg, or
    // output setting installed.
    std::vector<talos::coverage::Database> loadedDatabases;
    loadedDatabases.reserve(coverageLoads.size());
    for (const std::string &path : coverageLoads) {
      talos::coverage::Database incoming;
      talos::coverage::Diagnostic diagnostic;
      talos::coverage::Status status =
          talos::coverage::readFile(path, incoming, {}, &diagnostic);
      if (status != talos::coverage::Status::Ok)
        return mapCoverageStatus(status);
      loadedDatabases.push_back(std::move(incoming));
    }

    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *liveCoverage = context->coverage.get();
    std::unique_ptr<talos::coverage::Database> stagedSchema;
    if (!loadedDatabases.empty()) {
      if (liveCoverage && liveCoverage->schema)
        stagedSchema = std::make_unique<talos::coverage::Database>(
            *liveCoverage->schema);
      for (talos::coverage::Database &incoming : loadedDatabases) {
        if (liveCoverage && liveCoverage->finalized) {
          uint64_t incomingLines = 0;
          uint64_t incomingToggleBits = 0;
          if (!coverageSchemaCounts(incoming, incomingLines,
                                    incomingToggleBits) ||
              incomingLines != liveCoverage->lineCount ||
              incomingToggleBits != liveCoverage->toggleBitCount)
            return TALOS_RT_INVALID_DESIGN;
        }
        if (!stagedSchema) {
          stagedSchema = std::make_unique<talos::coverage::Database>(
              std::move(incoming));
          continue;
        }
        talos::coverage::Diagnostic diagnostic;
        talos::coverage::Status status = talos::coverage::merge(
            *stagedSchema, incoming, false, &diagnostic);
        if (status != talos::coverage::Status::Ok) {
          std::fprintf(stderr, "error: coverage load failed%s%s%s%s\n",
                       diagnostic.field ? " in " : "",
                       diagnostic.field ? diagnostic.field : "",
                       diagnostic.detail.empty() ? "" : ": ",
                       diagnostic.detail.empty() ? ""
                                                 : diagnostic.detail.c_str());
          return mapCoverageStatus(status);
        }
      }
    }

    // Allocate optional service state before reseeding.  Everything following
    // seedContextUnlocked is a no-throw move/swap commit.
    std::unique_ptr<CoverageState> stagedCoverage;
    if (haveCoverageOptions && !liveCoverage)
      stagedCoverage = std::make_unique<CoverageState>();
    if (haveSeed)
      seedContextUnlocked(context, configuredSeed);
    context->plusargs.swap(plusargs);
    context->vpiArguments.swap(vpiArguments);
    context->plusargIndexNodes.swap(nodes);
    context->plusargIndexEdges.swap(edges);
    context->plusargIndexBuilt = false;
    if (haveCoverageOptions) {
      if (stagedCoverage)
        context->coverage = std::move(stagedCoverage);
      CoverageState &coverage = *context->coverage;
      if (stagedSchema)
        coverage.schema = std::move(stagedSchema);
      coverage.outputPath = std::move(coverageOutput);
      coverage.outputExplicit = coverage.outputPath != "coverage.obcov";
      coverage.testName = std::move(coverageTest);
      coverage.tags = std::move(coverageTags);
      coverage.loadPaths.clear();
      coverage.dumpSuppressed = suppressCoverageDump;
    }
    return TALOS_RT_OK;
  });
}

extern "C" uint32_t talos_rt_v1_import_id(const uint8_t *symbol,
                                            uint64_t symbolSize) {
  if (!validBytes(symbol, symbolSize) || symbolSize == 0)
    return 0;
  uint64_t hash = talos_stable_hash(symbol, symbolSize);
  uint32_t result = static_cast<uint32_t>(hash ^ (hash >> 32));
  return result == 0 ? 1 : result;
}

extern "C" talos_rt_status talos_rt_v1_context_register_import(
    talos_rt_context *context, uint32_t importID,
    talos_rt_import_callback_v1 callback, void *userData) {
  return talos_rt_v1_context_register_import_signature(context, importID, 0,
                                                         callback, userData);
}

extern "C" talos_rt_status talos_rt_v1_context_register_import_signature(
    talos_rt_context *context, uint32_t importID, uint64_t abiSignature,
    talos_rt_import_callback_v1 callback, void *userData) {
  if (!context || importID == 0 || !callback)
    return TALOS_RT_INVALID_ARGUMENT;
  return guarded(context, [&] {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    context->imports[importID] = {callback, userData, abiSignature};
    return TALOS_RT_OK;
  });
}

extern "C" talos_rt_status talos_rt_v1_context_create_for_design(
    const talos_rt_execution_descriptor_v1 *execution,
    talos_rt_context **outContext) {
  if (!outContext)
    return TALOS_RT_INVALID_ARGUMENT;
  *outContext = nullptr;
  TALOS_RT_TRY {
    DesignDatabaseCache designDatabase;
    talos::designbytecode::Image designBytecodeImage;
    talos::coverage::Database coverageSchema;
    bool hasCoverageSchema = false;
    if (execution) {
      constexpr uint32_t validFlags = TALOS_RT_EXECUTION_HAS_BYTECODE |
                                      TALOS_RT_EXECUTION_HAS_DESIGN_DATABASE |
                                      TALOS_RT_EXECUTION_VPI_READ |
                                      TALOS_RT_EXECUTION_VPI_WRITE |
                                      TALOS_RT_EXECUTION_REQUIRE_BYTECODE |
                                      TALOS_RT_EXECUTION_PREPONED_SNAPSHOT |
                                      TALOS_RT_EXECUTION_WAVEFORM_METADATA |
                                      TALOS_RT_EXECUTION_DPI_EXPORTS |
                                      TALOS_RT_EXECUTION_CLASS_BITSTREAM |
                                      TALOS_RT_EXECUTION_COVERAGE_SCHEMA |
                                      TALOS_RT_EXECUTION_POLLED_COLLECTION |
                                      TALOS_RT_EXECUTION_TWO_STATE;
      if (execution->version != TALOS_RT_VERSION ||
          execution->dpi_reserved != 0 ||
          (execution->flags & ~validFlags) != 0 ||
          ((execution->flags & TALOS_RT_EXECUTION_VPI_WRITE) != 0 &&
           (execution->flags & TALOS_RT_EXECUTION_VPI_READ) == 0) ||
          ((execution->flags & TALOS_RT_EXECUTION_REQUIRE_BYTECODE) != 0 &&
           (execution->flags & TALOS_RT_EXECUTION_HAS_BYTECODE) == 0) ||
          ((execution->flags & TALOS_RT_EXECUTION_WAVEFORM_METADATA) != 0 &&
           (execution->flags & TALOS_RT_EXECUTION_HAS_DESIGN_DATABASE) ==
               0) ||
          ((execution->flags & TALOS_RT_EXECUTION_HAS_BYTECODE) != 0
               ? (!execution->bytecode || execution->bytecode_size == 0 ||
                  execution->checksum == 0)
               : (execution->bytecode || execution->bytecode_size != 0 ||
                  execution->checksum != 0)) ||
          !validActivationInventory(*execution) ||
          !validObserverInventory(*execution) ||
          !validExecutionExtension(*execution))
        return TALOS_RT_INVALID_DESIGN;
      if ((execution->flags & TALOS_RT_EXECUTION_HAS_BYTECODE) != 0) {
        talos_rt_status status = talos_rt_initialize_design_bytecode_image(
            *execution, designBytecodeImage);
        if (status != TALOS_RT_OK)
          return status;
      }
      if ((execution->flags & TALOS_RT_EXECUTION_HAS_DESIGN_DATABASE) == 0 &&
          (execution->design_database || execution->design_database_size != 0))
        return TALOS_RT_INVALID_DESIGN;
      if ((execution->flags & TALOS_RT_EXECUTION_HAS_DESIGN_DATABASE) != 0) {
        talos_rt_status status =
            talos_rt_initialize_design_database(execution, designDatabase);
        if (status != TALOS_RT_OK)
          return status;
      }
      if ((execution->flags & TALOS_RT_EXECUTION_COVERAGE_SCHEMA) != 0) {
        const talos_rt_execution_extension_v1 *extension =
            executionExtension(*execution);
        talos::coverage::Diagnostic diagnostic;
        talos::coverage::Status status = talos::coverage::parse(
            extension->coverage_schema,
            static_cast<size_t>(extension->coverage_schema_size),
            coverageSchema, {}, &diagnostic);
        if (status == talos::coverage::Status::OutOfMemory)
          return TALOS_RT_OUT_OF_MEMORY;
        if (status != talos::coverage::Status::Ok ||
            !talos::coverage::isSchemaOnly(coverageSchema))
          return TALOS_RT_INVALID_DESIGN;
        hasCoverageSchema = true;
      }
    }
    auto *context = new talos_rt_context();
    context->execution = execution;
    context->designDatabase = designDatabase;
    context->designBytecodeImage = designBytecodeImage;
    context->designBytecodeImageValidated = designBytecodeImage.data != nullptr;
    if (hasCoverageSchema) {
      context->coverage = std::make_unique<CoverageState>();
      context->coverage->schema = std::make_unique<talos::coverage::Database>(
          std::move(coverageSchema));
    }
    if (execution &&
        (execution->flags & TALOS_RT_EXECUTION_POLLED_COLLECTION) != 0)
      talos_rt_managed_heap_poll_collections(context->managedHeap);
    if (execution) {
      talos_rt_status status =
          talos_rt_initialize_dpi_scopes(context, execution);
      if (status != TALOS_RT_OK) {
        delete context;
        return status;
      }
    }
    if (execution && execution->state_bit_count != 0) {
      if (execution->state_bit_count > std::numeric_limits<size_t>::max() - 63)
        talos_rt_out_of_memory();
      size_t limbs =
          static_cast<size_t>((execution->state_bit_count + 63) / 64);
      context->stateValue.assign(limbs, 0);
      if (context->twoState())
        context->stateUnknown.assignZero(limbs);
      else
        context->stateUnknown.assign(limbs, UINT64_MAX);
      unsigned tail = static_cast<unsigned>(execution->state_bit_count % 64);
      if (tail != 0)
        context->stateUnknown.back() &= (uint64_t{1} << tail) - 1;
    }
    if (execution &&
        (execution->flags & TALOS_RT_EXECUTION_HAS_BYTECODE) != 0) {
      talos_rt_status status = talos_rt_initialize_design_state(context);
      if (status != TALOS_RT_OK) {
        delete context;
        return status;
      }
    }
    if (context->designDatabase.validated) {
      talos_rt_status status = talos_rt_register_design_database(
          context->execution, context->designDatabase);
      if (status != TALOS_RT_OK) {
        delete context;
        return status;
      }
      context->designDatabaseRegistered = true;
    }
    *outContext = context;
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_IO_ERROR; }
}

extern "C" void talos_rt_v1_context_destroy(talos_rt_context *context) {
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

extern "C" const char *talos_rt_v1_status_string(talos_rt_status status) {
  switch (status) {
  case TALOS_RT_OK:
    return "ok";
  case TALOS_RT_EOF:
    return "end of file";
  case TALOS_RT_INVALID_ARGUMENT:
    return "invalid argument";
  case TALOS_RT_INVALID_HANDLE:
    return "invalid handle";
  case TALOS_RT_IO_ERROR:
    return "I/O error";
  case TALOS_RT_OUT_OF_MEMORY:
    return "out of memory";
  case TALOS_RT_OUT_OF_RESOURCES:
    return "out of resources";
  case TALOS_RT_FORMAT_ERROR:
    return "format error";
  case TALOS_RT_ARGUMENT_MISMATCH:
    return "format argument mismatch";
  case TALOS_RT_INVALID_BYTECODE:
    return "invalid bytecode";
  case TALOS_RT_STEP_LIMIT:
    return "fragment step limit exceeded";
  case TALOS_RT_LAYOUT_MISMATCH:
    return "process frame layout mismatch";
  case TALOS_RT_INVALID_CONTINUATION:
    return "invalid process continuation";
  case TALOS_RT_TIER_UNAVAILABLE:
    return "requested process tier unavailable";
  case TALOS_RT_AOT_CHECKPOINT:
    return "native scheduler synchronization checkpoint";
  case TALOS_RT_AOT_TIMED_CHECKPOINT:
    return "native scheduler timed synchronization checkpoint";
  case TALOS_RT_AOT_GENERATED_CHECKPOINT:
    return "generated native scheduler branch checkpoint";
  case TALOS_RT_INVALID_LIFECYCLE:
    return "invalid process lifecycle transition";
  case TALOS_RT_INVALID_FRAME:
    return "invalid process frame record";
  case TALOS_RT_INVALID_DESIGN:
    return "invalid design metadata";
  case TALOS_RT_PERMISSION_DENIED:
    return "permission denied";
  case TALOS_RT_DPI_DISABLE_UNSUPPORTED:
    return "DPI task disable is unsupported";
  case TALOS_RT_FATAL:
    return "fatal SystemVerilog diagnostic";
  default:
    return "unknown runtime status";
  }
}

extern "C" void talos_rt_v1_buffer_release(talos_rt_buffer_v1 *buffer) {
  if (!buffer)
    return;
  std::free(buffer->data);
  buffer->data = nullptr;
  buffer->size = 0;
}

extern "C" talos_rt_status
talos_rt_v1_last_error(talos_rt_context *context,
                         talos_rt_buffer_v1 *outMessage) {
  if (!context || !outMessage)
    return TALOS_RT_INVALID_ARGUMENT;
  return guarded(context, [&] {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    auto error = threadErrors.find(context);
    if (error == threadErrors.end() ||
        error->second.contextLifetime.lock() != context->errorLifetime)
      return makeBuffer({}, outMessage);
    return makeBuffer(error->second.message, outMessage);
  });
}

extern "C" void
talos_rt_v1_scheduler_report_status(talos_rt_context *context,
                                      talos_rt_status status) {
  // A standalone simulator returns this status from main and exits, so this is
  // the last chance to say what ended the run. A design-requested end is not a
  // failure to report: $finish is OK and $fatal already printed its message.
  if (!context || status == TALOS_RT_OK || status == TALOS_RT_FATAL)
    return;
  TALOS_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    auto error = threadErrors.find(context);
    bool described =
        error != threadErrors.end() &&
        error->second.contextLifetime.lock() == context->errorLifetime &&
        !error->second.message.empty();
    if (described)
      std::fprintf(stderr, "error: %s (status %d)\n",
                   error->second.message.c_str(), status);
    else
      std::fprintf(stderr, "error: simulation ended: %s (status %d)\n",
                   talos_rt_v1_status_string(status), status);
  }
  TALOS_RT_CATCH_ALL {}
}
