//===- ProcessObservers.cpp - Native computed observers -----------------===//

#include "ProcessObservers.h"
#include "ProcessShared.h"
#include "ProcessValidation.h"
#include "RuntimeInternal.h"
#include "SignalSemantics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <new>
#include <vector>

using namespace obelisk::process;
using namespace obelisk::runtime;

namespace {

constexpr uint32_t kMaximumObserverDepth = 256;

} // namespace

obelisk_rt_computed_wait_record_v1 *computedWait(ScheduledProcess &process) {
  if (process.suspendKind != OBELISK_RT_SUSPEND_OBSERVER || !process.instance ||
      !process.instance->frame ||
      process.waitSize < sizeof(obelisk_rt_computed_wait_record_v1) ||
      process.waitOffset > process.instance->frame_size ||
      process.waitSize > process.instance->frame_size - process.waitOffset)
    return nullptr;
  auto *wait = reinterpret_cast<obelisk_rt_computed_wait_record_v1 *>(
      static_cast<uint8_t *>(process.instance->frame) + process.waitOffset);
  return wait->version == OBELISK_RT_VERSION &&
                 wait->kind == OBELISK_RT_SUSPEND_OBSERVER &&
                 wait->total_size <= process.waitSize
             ? wait
             : nullptr;
}

namespace {

bool evaluateNativeBoundObserver(obelisk_rt_context *context,
                                 uint64_t processToken, uint64_t codeUnitID,
                                 const obelisk_rt_computed_capture_v1 *captures,
                                 uint32_t captureCount, uint64_t *value,
                                 uint64_t *unknown, uint32_t limbCapacity) {
  if (!context || (!captures && captureCount != 0) || !value || !unknown)
    return false;
  ScheduledProcess *process = findScheduledProcess(context, processToken);
  if (!process || !process->instance || !process->instance->descriptor)
    return false;
  const obelisk_rt_observer_descriptor_v1 *descriptor = findObserverDescriptor(
      process->instance->descriptor->execution, codeUnitID);
  if (!descriptor || descriptor->capture_count != captureCount)
    return false;
  if (context->observerDepth >= kMaximumObserverDepth) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
    return false;
  }
  uint32_t limbs =
      static_cast<uint32_t>((uint64_t{descriptor->result_width} + 63) / 64);
  if (limbs == 0 || limbs > limbCapacity)
    return false;
  std::fill(value, value + limbs, 0);
  std::fill(unknown, unknown + limbs, 0);
  constexpr uint32_t kInlineCaptures = 4;
  std::array<obelisk_rt_computed_capture_v1, kInlineCaptures> inlineCaptures{};
  std::vector<obelisk_rt_computed_capture_v1> spilledCaptures;
  obelisk_rt_computed_capture_v1 *copiedCaptures = inlineCaptures.data();
  if (captureCount > kInlineCaptures) {
    spilledCaptures.resize(captureCount);
    copiedCaptures = spilledCaptures.data();
  }
  std::copy(captures, captures + captureCount, copiedCaptures);
  std::array<uint64_t, kInlineCaptures> inlineRetainedCaptures{};
  std::vector<uint64_t> spilledRetainedCaptures;
  uint64_t *retainedCaptures = inlineRetainedCaptures.data();
  if (captureCount > kInlineCaptures) {
    spilledRetainedCaptures.resize(captureCount);
    retainedCaptures = spilledRetainedCaptures.data();
  }
  uint32_t retainedCaptureCount = 0;
  obelisk_rt_process_instance_v1 *waiter = process->instance;
  if (waiter->observer_pin_count == UINT32_MAX) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
    return false;
  }
  ++waiter->observer_pin_count;
  obelisk_rt_status status = OBELISK_RT_OK;
  for (uint32_t index = 0; index != captureCount; ++index) {
    uint32_t kind = descriptor->capture_abi[index].kind;
    uint64_t retained = 0;
    if (kind == OBELISK_RT_OBSERVER_CAPTURE_STORAGE)
      retained = copiedCaptures[index].stable_id;
    else if (kind == OBELISK_RT_OBSERVER_CAPTURE_ARGUMENT_REF &&
             copiedCaptures[index].payload1 == 0)
      retained = copiedCaptures[index].payload0;
    else
      continue;
    status = obelisk_rt_v1_native_state_retain(context, retained);
    if (status != OBELISK_RT_OK)
      break;
    retainedCaptures[retainedCaptureCount++] = retained;
  }
  if (status != OBELISK_RT_OK) {
    while (retainedCaptureCount != 0)
      (void)obelisk_rt_v1_native_state_release(
          context, retainedCaptures[--retainedCaptureCount], 0);
    --waiter->observer_pin_count;
    context->schedulerStatus = status;
    return false;
  }

  obelisk_rt_process_instance_v1 *producer = context->activeNativeProcess;
  uint64_t producerToken = context->activeLogicalProcessToken;
  uint64_t producerDesignTask = context->activeDesignTaskID;
  bool producerWaitOrderFailed = context->activeWaitOrderFailed;
  bool producerDesignExecuting = context->designTaskExecuting;
  std::vector<uint64_t> producerControls = std::move(context->activeControls);
  std::vector<uint64_t> waiterControls = process->controls;
  context->activeNativeProcess = waiter;
  context->activeLogicalProcessToken =
      OBELISK_RT_NATIVE_LOGICAL_PROCESS_TAG | processToken;
  context->activeDesignTaskID = 0;
  context->activeWaitOrderFailed = false;
  context->designTaskExecuting = false;
  context->activeControls = std::move(waiterControls);
  ++context->observerDepth;
  if (context->nativeStateSpecializationFast)
    *context->nativeStateSpecializationFast = 0;
  {
    ContextCallbackUnlock unlock(context);
    OBELISK_RT_TRY {
      if ((waiter->tier == OBELISK_RT_TIER_BYTECODE ||
           !descriptor->native_evaluator) &&
          descriptor->bytecode_function != OBELISK_RT_OBSERVER_NO_BYTECODE) {
        status = obelisk_rt_execute_design_observer(
            *waiter->descriptor->execution, context,
            descriptor->bytecode_function, copiedCaptures, captureCount, value,
            unknown, limbs);
      } else if (descriptor->native_evaluator) {
        status = descriptor->native_evaluator(
            context, reinterpret_cast<const uint64_t *>(copiedCaptures),
            captureCount, value, unknown, limbs);
      } else {
        status = OBELISK_RT_TIER_UNAVAILABLE;
      }
    }
    OBELISK_RT_CATCH(const std::bad_alloc &) {
      status = OBELISK_RT_OUT_OF_MEMORY;
    }
    OBELISK_RT_CATCH_ALL { status = OBELISK_RT_INVALID_ARGUMENT; }
  }
  --context->observerDepth;
  if (context->nativeStateSpecializationFast)
    refreshNativeStaticSpecializationFastUnlocked(context);
  waiterControls = std::move(context->activeControls);
  if (ScheduledProcess *updated = findScheduledProcess(context, processToken);
      updated && updated->instance == waiter)
    updated->controls = std::move(waiterControls);
  context->activeControls = std::move(producerControls);
  context->activeNativeProcess = producer;
  context->activeLogicalProcessToken = producerToken;
  context->activeDesignTaskID = producerDesignTask;
  context->activeWaitOrderFailed = producerWaitOrderFailed;
  context->designTaskExecuting = producerDesignExecuting;
  while (retainedCaptureCount != 0) {
    obelisk_rt_status releaseStatus = obelisk_rt_v1_native_state_release(
        context, retainedCaptures[--retainedCaptureCount], 0);
    if (status == OBELISK_RT_OK && releaseStatus != OBELISK_RT_OK)
      status = releaseStatus;
  }
  --waiter->observer_pin_count;
  if (waiter->observer_pin_count == 0 &&
      waiter->observer_destroy_pending != 0) {
    obelisk_rt_status destroyStatus =
        obelisk_rt_v1_process_instance_destroy(waiter);
    if (status == OBELISK_RT_OK && destroyStatus != OBELISK_RT_OK)
      status = destroyStatus;
  }
  if (status != OBELISK_RT_OK) {
    context->schedulerStatus = status;
    return false;
  }
  if (descriptor->result_width % 64 != 0) {
    uint64_t mask = (uint64_t{1} << (descriptor->result_width % 64)) - 1;
    value[limbs - 1] &= mask;
    unknown[limbs - 1] &= mask;
  }
  if ((descriptor->flags & OBELISK_RT_OBSERVER_FOUR_STATE) == 0)
    std::fill(unknown, unknown + limbs, 0);
  return true;
}

bool evaluateNativeObserver(obelisk_rt_context *context, uint64_t processToken,
                            obelisk_rt_computed_wait_record_v1 *wait,
                            uint32_t observerIndex,
                            std::vector<uint64_t> &value,
                            std::vector<uint64_t> &unknown) {
  if (!context || !wait || observerIndex >= wait->observer_count)
    return false;
  auto *observers = computedWaitSpan<obelisk_rt_computed_observer_v1>(
      wait, wait->observers_offset, wait->observer_count);
  auto *captures = computedWaitSpan<obelisk_rt_computed_capture_v1>(
      wait, wait->captures_offset, wait->capture_count);
  if (!observers || !captures)
    return false;
  const obelisk_rt_computed_observer_v1 &binding = observers[observerIndex];
  if (binding.capture_begin > wait->capture_count ||
      binding.capture_count > wait->capture_count - binding.capture_begin)
    return false;
  ScheduledProcess *process = findScheduledProcess(context, processToken);
  const obelisk_rt_observer_descriptor_v1 *descriptor =
      process && process->instance && process->instance->descriptor
          ? findObserverDescriptor(process->instance->descriptor->execution,
                                   binding.code_unit_id)
          : nullptr;
  if (!descriptor)
    return false;
  uint32_t limbs =
      static_cast<uint32_t>((uint64_t{descriptor->result_width} + 63) / 64);
  value.assign(limbs, 0);
  unknown.assign(limbs, 0);
  return evaluateNativeBoundObserver(
      context, processToken, binding.code_unit_id,
      captures + binding.capture_begin, binding.capture_count, value.data(),
      unknown.data(), limbs);
}

bool evaluateNativeComputedWaiters(obelisk_rt_context *context,
                                   uint32_t dependencyKind,
                                   uint64_t publishedHandle,
                                   uint64_t publishedWidth) {
  if (!context)
    return false;
  if (context->schedulerStatus != OBELISK_RT_OK)
    return true;
  std::vector<uint64_t> tokens;
  if (dependencyKind == OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL)
    tokens.swap(context->pendingNativeComputedWaiters);
  else {
    tokens.reserve(context->scheduledProcesses.size());
    for (const ScheduledProcess &process : context->scheduledProcesses)
      if (process.instance && process.started &&
          process.suspendKind == OBELISK_RT_SUSPEND_OBSERVER &&
          !process.signalTriggered)
        tokens.push_back(process.token);
  }

  for (uint64_t token : tokens) {
    ScheduledProcess *process = findScheduledProcess(context, token);
    if (!process || process->signalTriggered)
      continue;
    obelisk_rt_computed_wait_record_v1 *wait = computedWait(*process);
    if (!wait)
      continue;
    auto *observers = computedWaitSpan<obelisk_rt_computed_observer_v1>(
        wait, wait->observers_offset, wait->observer_count);
    auto *dependencies = computedWaitSpan<obelisk_rt_computed_dependency_v1>(
        wait, wait->dependencies_offset, wait->dependency_count);
    auto *clauses = computedWaitSpan<obelisk_rt_computed_clause_v1>(
        wait, wait->clauses_offset, wait->clause_count);
    if (!observers || !dependencies || !clauses)
      return false;
    auto primaryAffected = [&](const obelisk_rt_computed_observer_v1 &primary) {
      for (uint32_t dependencyIndex = 0;
           dependencyIndex != primary.dependency_count; ++dependencyIndex) {
        const obelisk_rt_computed_dependency_v1 &dependency =
            dependencies[primary.dependency_begin + dependencyIndex];
        if (dependency.kind != dependencyKind)
          continue;
        if (dependencyKind == OBELISK_RT_OBSERVER_DEPENDENCY_EVENT ||
                    dependencyKind == OBELISK_RT_OBSERVER_DEPENDENCY_MANAGED
                ? dependency.stable_id == publishedHandle
                : rangesOverlap(dependency.stable_id, dependency.width,
                                publishedHandle, publishedWidth))
          return true;
      }
      return false;
    };
    if (dependencyKind == OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL) {
      bool affected = false;
      for (uint32_t clauseIndex = 0; clauseIndex != wait->clause_count;
           ++clauseIndex) {
        if (primaryAffected(observers[clauses[clauseIndex].primary_observer])) {
          affected = true;
          break;
        }
      }
      if (!affected) {
        OBELISK_RT_TRY {
          context->pendingNativeComputedWaiters.push_back(token);
        }
        OBELISK_RT_CATCH(const std::bad_alloc &) {
          context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
          return false;
        }
        continue;
      }
      if (process->signalLatch)
        process->signalLatch->affected = false;
    }
    for (uint32_t clauseIndex = 0; clauseIndex != wait->clause_count;
         ++clauseIndex) {
      obelisk_rt_computed_clause_v1 clause = clauses[clauseIndex];
      obelisk_rt_computed_observer_v1 primary =
          observers[clause.primary_observer];
      if (!primaryAffected(primary))
        continue;
      std::vector<uint64_t> value;
      std::vector<uint64_t> unknown;
      if (!evaluateNativeObserver(context, token, wait, clause.primary_observer,
                                  value, unknown))
        return false;
      process = findScheduledProcess(context, token);
      wait = process ? computedWait(*process) : nullptr;
      if (!process || !process->instance || process->signalTriggered || !wait)
        break;
      const obelisk_rt_observer_descriptor_v1 *descriptor =
          findObserverDescriptor(process->instance->descriptor->execution,
                                 primary.code_unit_id);
      if (!descriptor)
        return false;
      uint32_t limbs =
          static_cast<uint32_t>((uint64_t{descriptor->result_width} + 63) / 64);
      auto *previousValue = reinterpret_cast<uint64_t *>(
          reinterpret_cast<uint8_t *>(wait) + primary.previous_offset);
      auto *previousUnknown = previousValue + limbs;
      bool changed = false;
      if ((descriptor->flags & OBELISK_RT_OBSERVER_REAL32) != 0) {
        float previous = 0.0f;
        float current = 0.0f;
        std::memcpy(&previous, previousValue, sizeof(previous));
        std::memcpy(&current, value.data(), sizeof(current));
        changed = previous != current || std::isnan(current);
      } else if ((descriptor->flags & OBELISK_RT_OBSERVER_REAL64) != 0) {
        double previous = 0.0;
        double current = 0.0;
        std::memcpy(&previous, previousValue, sizeof(previous));
        std::memcpy(&current, value.data(), sizeof(current));
        changed = previous != current || std::isnan(current);
      } else {
        for (uint32_t limb = 0; limb != limbs; ++limb)
          changed |= previousValue[limb] != value[limb] ||
                     previousUnknown[limb] != unknown[limb];
      }
      uint32_t observedEdges = transitionEdges(
          (previousValue[0] & 1) != 0, (previousUnknown[0] & 1) != 0,
          (value[0] & 1) != 0, (unknown[0] & 1) != 0);
      for (uint32_t limb = 0; limb != limbs; ++limb) {
        previousValue[limb] = value[limb];
        previousUnknown[limb] = unknown[limb];
      }
      bool levelTrue =
          (clause.flags & OBELISK_RT_COMPUTED_CLAUSE_LEVEL_TRUE) != 0 &&
          !value.empty() && (value[0] & 1) != 0 &&
          (unknown.empty() || (unknown[0] & 1) == 0);
      bool occurrence =
          (dependencyKind == OBELISK_RT_OBSERVER_DEPENDENCY_EVENT &&
           (clause.flags & OBELISK_RT_COMPUTED_CLAUSE_EVENT_PRIMARY) != 0) ||
          levelTrue ||
          (clause.edge == OBELISK_RT_WAIT_EDGE_CHANGE
               ? changed
               : signalEdgeMatches(clause.edge, observedEdges));
      if (!occurrence)
        continue;
      if (clause.condition_observer != OBELISK_RT_OBSERVER_CONDITION_NONE) {
        if (!evaluateNativeObserver(context, token, wait,
                                    clause.condition_observer, value, unknown))
          return false;
        process = findScheduledProcess(context, token);
        wait = process ? computedWait(*process) : nullptr;
        if (!process || !process->instance || process->signalTriggered || !wait)
          break;
        occurrence = !value.empty() && (value[0] & 1) != 0 &&
                     (unknown.empty() || (unknown[0] & 1) == 0);
      }
      if (occurrence) {
        if (ScheduledProcess *updated = findScheduledProcess(context, token);
            updated && updated->instance) {
          updated->signalTriggered = true;
          context->prioritySignalPending |= updated->prioritySignal;
          OBELISK_RT_TRY { context->nativePollCandidates.insert(token); }
          OBELISK_RT_CATCH(const std::bad_alloc &) {
            context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
            return false;
          }
          if (++context->schedulerSelectionGeneration == 0)
            context->schedulerSelectionGeneration = 1;
        }
        break;
      }
    }
  }
  return context->schedulerStatus == OBELISK_RT_OK;
}

} // namespace

bool obelisk_rt_evaluate_native_clock_condition_unlocked(
    obelisk_rt_context *context, uint64_t processToken, uint64_t codeUnitID,
    const obelisk_rt_computed_capture_v1 *captures, uint32_t captureCount,
    uint64_t &value, uint64_t &unknown) {
  uint64_t values = 0;
  uint64_t unknowns = 0;
  if (!evaluateNativeBoundObserver(context, processToken, codeUnitID, captures,
                                   captureCount, &values, &unknowns, 1))
    return false;
  value = values & 1;
  unknown = unknowns & 1;
  return true;
}

bool obelisk_rt_evaluate_native_bound_observer_unlocked(
    obelisk_rt_context *context, uint64_t processToken, uint64_t codeUnitID,
    const obelisk_rt_computed_capture_v1 *captures, uint32_t captureCount,
    uint64_t *value, uint64_t *unknown, uint32_t limbCapacity) {
  return evaluateNativeBoundObserver(context, processToken, codeUnitID,
                                     captures, captureCount, value, unknown,
                                     limbCapacity);
}

ScheduledProcess *findScheduledProcess(obelisk_rt_context *context,
                                       uint64_t token) {
  if (auto indexed = context->scheduledProcessIndices.find(token);
      indexed != context->scheduledProcessIndices.end() &&
      indexed->second < context->scheduledProcesses.size()) {
    ScheduledProcess &process = context->scheduledProcesses[indexed->second];
    if (process.token == token)
      return &process;
  }
  for (ScheduledProcess &process : context->scheduledProcesses)
    if (process.token == token)
      return &process;
  return nullptr;
}

ScheduledProcess *
findScheduledProcessByInstance(obelisk_rt_context *context,
                               obelisk_rt_process_instance_v1 *instance) {
  ScheduledProcess *first = nullptr;
  auto [begin, end] = context->scheduledProcessTokens.equal_range(instance);
  for (auto it = begin; it != end; ++it)
    if (auto *process = findScheduledProcess(context, it->second);
        process && process->instance == instance &&
        (!first || process->token < first->token))
      first = process;
  if (first)
    return first;
  // Internal fixtures and recovery may install records before rebuilding.
  for (auto &process : context->scheduledProcesses)
    if (process.instance == instance)
      return &process;
  return nullptr;
}

void updateScheduledProcessInstance(obelisk_rt_context *context,
                                    ScheduledProcess &process,
                                    obelisk_rt_process_instance_v1 *instance) {
  auto [begin, end] =
      context->scheduledProcessTokens.equal_range(process.instance);
  for (auto it = begin; it != end; ++it) {
    if (it->second != process.token)
      continue;
    auto node = context->scheduledProcessTokens.extract(it);
    if (instance) {
      // Reuse the ownership record; task calls and returns allocate no index
      // nodes and cannot leave a key pointing at a destroyed callee.
      node.key() = instance;
      context->scheduledProcessTokens.insert(std::move(node));
    }
    break;
  }
  process.instance = instance;
}

bool obelisk_rt_notify_observer_event_unlocked(obelisk_rt_context *context,
                                               uint64_t stableID) {
  return evaluateNativeComputedWaiters(
             context, OBELISK_RT_OBSERVER_DEPENDENCY_EVENT, stableID, 1) &&
         obelisk_rt_evaluate_design_observers_unlocked(
             context, OBELISK_RT_OBSERVER_DEPENDENCY_EVENT, stableID, 1);
}

bool obelisk_rt_notify_observer_signal_unlocked(obelisk_rt_context *context,
                                                uint64_t stableID,
                                                uint64_t width) {
  return evaluateNativeComputedWaiters(
             context, OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL, stableID, width) &&
         obelisk_rt_evaluate_design_observers_unlocked(
             context, OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL, stableID, width);
}

bool obelisk_rt_notify_observer_managed_unlocked(obelisk_rt_context *context,
                                                 uint64_t token) {
  return evaluateNativeComputedWaiters(
             context, OBELISK_RT_OBSERVER_DEPENDENCY_MANAGED, token, 1) &&
         obelisk_rt_evaluate_design_observers_unlocked(
             context, OBELISK_RT_OBSERVER_DEPENDENCY_MANAGED, token, 1) &&
         obelisk_rt_evaluate_covergroup_managed_clock_events_unlocked(context,
                                                                      token);
}
