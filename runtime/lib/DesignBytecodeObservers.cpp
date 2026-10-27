//===- DesignBytecodeObservers.cpp - Computed observer scheduling --------===//

#include "DesignBytecodeNets.h"
#include "ProcessValidation.h"
#include "RuntimeInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <new>
#include <vector>

namespace {

using namespace talos::designbytecode;
using namespace talos::process;

constexpr uint32_t kMaximumObserverDepth = 256;

ScheduledDesignTask *findDesignTask(talos_rt_context *context, uint64_t id) {
  if (auto indexed = context->scheduledDesignTaskIndices.find(id);
      indexed != context->scheduledDesignTaskIndices.end() &&
      indexed->second < context->scheduledDesignTasks.size()) {
    ScheduledDesignTask &task = context->scheduledDesignTasks[indexed->second];
    if (task.id == id)
      return &task;
  }
  for (ScheduledDesignTask &task : context->scheduledDesignTasks)
    if (task.id == id)
      return &task;
  return nullptr;
}

talos_rt_computed_wait_record_v1 *
currentComputedWait(ScheduledDesignTask &task) {
  if (task.terminated || !task.started ||
      task.suspendKind != TALOS_RT_SUSPEND_OBSERVER ||
      task.waitSize < sizeof(talos_rt_computed_wait_record_v1) ||
      task.waitOffset > task.scratchOffset ||
      task.waitSize > task.scratchOffset - task.waitOffset)
    return nullptr;
  auto *wait = reinterpret_cast<talos_rt_computed_wait_record_v1 *>(
      task.frame.data() + task.waitOffset);
  return wait->version == TALOS_RT_VERSION &&
                 wait->kind == TALOS_RT_SUSPEND_OBSERVER &&
                 wait->total_size <= task.waitSize
             ? wait
             : nullptr;
}

bool evaluateBoundObserver(talos_rt_context *context, uint64_t taskID,
                           uint64_t codeUnitID,
                           const talos_rt_computed_capture_v1 *captures,
                           uint32_t captureCount, uint64_t *value,
                           uint64_t *unknown, uint32_t limbCapacity) {
  ScheduledDesignTask *task = findDesignTask(context, taskID);
  if (!task || (!captures && captureCount != 0) || !value || !unknown)
    return false;
  const talos_rt_observer_descriptor_v1 *descriptor =
      findObserverDescriptor(context->execution, codeUnitID);
  if (!descriptor || descriptor->capture_count != captureCount ||
      descriptor->bytecode_function == TALOS_RT_OBSERVER_NO_BYTECODE)
    return false;
  if (context->observerDepth >= kMaximumObserverDepth) {
    context->schedulerStatus = TALOS_RT_OUT_OF_RESOURCES;
    return false;
  }
  uint32_t limbs =
      static_cast<uint32_t>((uint64_t{descriptor->result_width} + 63) / 64);
  if (limbs == 0 || limbs > limbCapacity)
    return false;
  std::fill(value, value + limbs, 0);
  std::fill(unknown, unknown + limbs, 0);

  constexpr uint32_t kInlineCaptures = 4;
  std::array<talos_rt_computed_capture_v1, kInlineCaptures> inlineCaptures{};
  std::vector<talos_rt_computed_capture_v1> spilledCaptures;
  talos_rt_computed_capture_v1 *copiedCaptures = inlineCaptures.data();
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
  talos_rt_status status = TALOS_RT_OK;
  for (uint32_t index = 0; index != captureCount; ++index) {
    uint32_t kind = descriptor->capture_abi[index].kind;
    uint64_t retained = 0;
    if (kind == TALOS_RT_OBSERVER_CAPTURE_STORAGE)
      retained = copiedCaptures[index].stable_id;
    else if (kind == TALOS_RT_OBSERVER_CAPTURE_ARGUMENT_REF &&
             copiedCaptures[index].payload1 == 0)
      retained = copiedCaptures[index].payload0;
    else
      continue;
    status = talos_rt_v1_native_state_retain(context, retained);
    if (status != TALOS_RT_OK)
      break;
    retainedCaptures[retainedCaptureCount++] = retained;
  }
  if (status != TALOS_RT_OK) {
    while (retainedCaptureCount != 0)
      (void)talos_rt_v1_native_state_release(
          context, retainedCaptures[--retainedCaptureCount], 0);
    context->schedulerStatus = status;
    return false;
  }

  talos_rt_process_instance_v1 *producer = context->activeNativeProcess;
  uint64_t producerToken = context->activeLogicalProcessToken;
  uint64_t producerParent = context->activeLogicalProcessParent;
  uint64_t producerTask = context->activeDesignTaskID;
  bool producerWaitOrderFailed = context->activeWaitOrderFailed;
  bool producerExecuting = context->designTaskExecuting;
  std::vector<uint64_t> producerControls = std::move(context->activeControls);
  std::vector<uint64_t> waiterControls = task->controls;
  context->activeNativeProcess = nullptr;
  context->activeLogicalProcessToken = taskID;
  context->activeLogicalProcessParent = task->parent;
  context->activeDesignTaskID = taskID;
  context->activeWaitOrderFailed = false;
  context->designTaskExecuting = true;
  context->activeControls = std::move(waiterControls);
  ++context->observerDepth;
  {
    ContextCallbackUnlock unlock(context);
    status = talos_rt_execute_design_observer(
        *context->execution, context, descriptor->bytecode_function,
        copiedCaptures, captureCount, value, unknown, limbs);
  }
  --context->observerDepth;
  waiterControls = std::move(context->activeControls);
  if (ScheduledDesignTask *updated = findDesignTask(context, taskID);
      updated && !updated->terminated)
    updated->controls = std::move(waiterControls);
  context->activeControls = std::move(producerControls);
  context->activeNativeProcess = producer;
  context->activeLogicalProcessToken = producerToken;
  context->activeLogicalProcessParent = producerParent;
  context->activeDesignTaskID = producerTask;
  context->activeWaitOrderFailed = producerWaitOrderFailed;
  context->designTaskExecuting = producerExecuting;
  while (retainedCaptureCount != 0) {
    talos_rt_status releaseStatus = talos_rt_v1_native_state_release(
        context, retainedCaptures[--retainedCaptureCount], 0);
    if (status == TALOS_RT_OK && releaseStatus != TALOS_RT_OK)
      status = releaseStatus;
  }
  if (status != TALOS_RT_OK) {
    context->schedulerStatus = status;
    return false;
  }
  if (descriptor->result_width % 64 != 0) {
    uint64_t mask = (uint64_t{1} << (descriptor->result_width % 64)) - 1;
    value[limbs - 1] &= mask;
    unknown[limbs - 1] &= mask;
  }
  if ((descriptor->flags & TALOS_RT_OBSERVER_FOUR_STATE) == 0)
    std::fill(unknown, unknown + limbs, 0);
  return true;
}

bool evaluateObserver(talos_rt_context *context, uint64_t taskID,
                      uint32_t observerIndex, std::vector<uint64_t> &value,
                      std::vector<uint64_t> &unknown) {
  ScheduledDesignTask *task = findDesignTask(context, taskID);
  talos_rt_computed_wait_record_v1 *wait =
      task ? currentComputedWait(*task) : nullptr;
  if (!task || !wait || observerIndex >= wait->observer_count)
    return false;
  auto *observers = computedWaitSpan<talos_rt_computed_observer_v1>(
      wait, wait->observers_offset, wait->observer_count);
  auto *captures = computedWaitSpan<talos_rt_computed_capture_v1>(
      wait, wait->captures_offset, wait->capture_count);
  if (!observers || !captures)
    return false;
  talos_rt_computed_observer_v1 binding = observers[observerIndex];
  if (binding.capture_begin > wait->capture_count ||
      binding.capture_count > wait->capture_count - binding.capture_begin)
    return false;
  const talos_rt_observer_descriptor_v1 *descriptor =
      findObserverDescriptor(context->execution, binding.code_unit_id);
  if (!descriptor)
    return false;
  uint32_t limbs =
      static_cast<uint32_t>((uint64_t{descriptor->result_width} + 63) / 64);
  value.assign(limbs, 0);
  unknown.assign(limbs, 0);
  return evaluateBoundObserver(
      context, taskID, binding.code_unit_id, captures + binding.capture_begin,
      binding.capture_count, value.data(), unknown.data(), limbs);
}

} // namespace

bool talos_rt_evaluate_design_clock_condition_unlocked(
    talos_rt_context *context, uint64_t taskID, uint64_t codeUnitID,
    const talos_rt_computed_capture_v1 *captures, uint32_t captureCount,
    uint64_t &value, uint64_t &unknown) {
  uint64_t values = 0;
  uint64_t unknowns = 0;
  if (!evaluateBoundObserver(context, taskID, codeUnitID, captures,
                             captureCount, &values, &unknowns, 1))
    return false;
  value = values & 1;
  unknown = unknowns & 1;
  return true;
}

bool talos_rt_evaluate_design_bound_observer_unlocked(
    talos_rt_context *context, uint64_t taskID, uint64_t codeUnitID,
    const talos_rt_computed_capture_v1 *captures, uint32_t captureCount,
    uint64_t *value, uint64_t *unknown, uint32_t limbCapacity) {
  return evaluateBoundObserver(context, taskID, codeUnitID, captures,
                               captureCount, value, unknown, limbCapacity);
}

bool talos_rt_evaluate_design_observers_unlocked(talos_rt_context *context,
                                                   uint32_t dependencyKind,
                                                   uint64_t publishedHandle,
                                                   uint64_t publishedWidth) {
  if (!context || context->schedulerStatus != TALOS_RT_OK)
    return context != nullptr;
  if (!context->execution)
    return true;

  std::vector<uint64_t> taskIDs;
  if (dependencyKind == TALOS_RT_OBSERVER_DEPENDENCY_SIGNAL)
    taskIDs.swap(context->pendingDesignComputedWaiters);
  else {
    taskIDs.reserve(context->scheduledDesignTasks.size());
    for (const ScheduledDesignTask &task : context->scheduledDesignTasks)
      if (!task.terminated && task.started &&
          task.suspendKind == TALOS_RT_SUSPEND_OBSERVER &&
          !task.signalTriggered)
        taskIDs.push_back(task.id);
  }
  for (uint64_t taskID : taskIDs) {
    ScheduledDesignTask *task = findDesignTask(context, taskID);
    talos_rt_computed_wait_record_v1 *wait =
        task ? currentComputedWait(*task) : nullptr;
    if (!task || task->signalTriggered || !wait)
      continue;
    auto *observers = computedWaitSpan<talos_rt_computed_observer_v1>(
        wait, wait->observers_offset, wait->observer_count);
    auto *dependencies = computedWaitSpan<talos_rt_computed_dependency_v1>(
        wait, wait->dependencies_offset, wait->dependency_count);
    auto *clauses = computedWaitSpan<talos_rt_computed_clause_v1>(
        wait, wait->clauses_offset, wait->clause_count);
    if (!observers || !dependencies || !clauses)
      return false;
    auto primaryAffected = [&](const talos_rt_computed_observer_v1 &primary) {
      for (uint32_t dependencyIndex = 0;
           dependencyIndex != primary.dependency_count; ++dependencyIndex) {
        const talos_rt_computed_dependency_v1 &dependency =
            dependencies[primary.dependency_begin + dependencyIndex];
        if (dependency.kind != dependencyKind)
          continue;
        if (dependencyKind == TALOS_RT_OBSERVER_DEPENDENCY_EVENT ||
                    dependencyKind == TALOS_RT_OBSERVER_DEPENDENCY_MANAGED
                ? dependency.stable_id == publishedHandle
                : rangesOverlap(dependency.stable_id, dependency.width,
                                publishedHandle, publishedWidth))
          return true;
      }
      return false;
    };
    if (dependencyKind == TALOS_RT_OBSERVER_DEPENDENCY_SIGNAL) {
      bool affected = false;
      for (uint32_t clauseIndex = 0; clauseIndex != wait->clause_count;
           ++clauseIndex) {
        if (primaryAffected(observers[clauses[clauseIndex].primary_observer])) {
          affected = true;
          break;
        }
      }
      if (!affected) {
        TALOS_RT_TRY {
          context->pendingDesignComputedWaiters.push_back(taskID);
        }
        TALOS_RT_CATCH(const std::bad_alloc &) {
          context->schedulerStatus = TALOS_RT_OUT_OF_MEMORY;
          return false;
        }
        continue;
      }
      if (task->signalLatch)
        task->signalLatch->affected = false;
    }
    for (uint32_t clauseIndex = 0; clauseIndex != wait->clause_count;
         ++clauseIndex) {
      talos_rt_computed_clause_v1 clause = clauses[clauseIndex];
      talos_rt_computed_observer_v1 primary =
          observers[clause.primary_observer];
      if (!primaryAffected(primary))
        continue;

      std::vector<uint64_t> value;
      std::vector<uint64_t> unknown;
      if (!evaluateObserver(context, taskID, clause.primary_observer, value,
                            unknown))
        return false;
      task = findDesignTask(context, taskID);
      wait = task ? currentComputedWait(*task) : nullptr;
      if (!task || task->terminated || task->signalTriggered || !wait)
        break;
      const talos_rt_observer_descriptor_v1 *descriptor =
          findObserverDescriptor(context->execution, primary.code_unit_id);
      if (!descriptor)
        return false;
      uint32_t limbs =
          static_cast<uint32_t>((uint64_t{descriptor->result_width} + 63) / 64);
      auto *previousValue = reinterpret_cast<uint64_t *>(
          reinterpret_cast<uint8_t *>(wait) + primary.previous_offset);
      auto *previousUnknown = previousValue + limbs;
      bool changed = false;
      if ((descriptor->flags & TALOS_RT_OBSERVER_REAL32) != 0) {
        float previous = 0.0f;
        float current = 0.0f;
        std::memcpy(&previous, previousValue, sizeof(previous));
        std::memcpy(&current, value.data(), sizeof(current));
        changed = previous != current || std::isnan(current);
      } else if ((descriptor->flags & TALOS_RT_OBSERVER_REAL64) != 0) {
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
          (clause.flags & TALOS_RT_COMPUTED_CLAUSE_LEVEL_TRUE) != 0 &&
          !value.empty() && (value[0] & 1) != 0 &&
          (unknown.empty() || (unknown[0] & 1) == 0);
      bool occurrence =
          (dependencyKind == TALOS_RT_OBSERVER_DEPENDENCY_EVENT &&
           (clause.flags & TALOS_RT_COMPUTED_CLAUSE_EVENT_PRIMARY) != 0) ||
          levelTrue ||
          (clause.edge == TALOS_RT_WAIT_EDGE_CHANGE
               ? changed
               : signalEdgeMatches(clause.edge, observedEdges));
      if (!occurrence)
        continue;
      if (clause.condition_observer != TALOS_RT_OBSERVER_CONDITION_NONE) {
        if (!evaluateObserver(context, taskID, clause.condition_observer, value,
                              unknown))
          return false;
        task = findDesignTask(context, taskID);
        wait = task ? currentComputedWait(*task) : nullptr;
        if (!task || task->terminated || task->signalTriggered || !wait)
          break;
        occurrence = !value.empty() && (value[0] & 1) != 0 &&
                     (unknown.empty() || (unknown[0] & 1) == 0);
      }
      if (occurrence) {
        if (ScheduledDesignTask *updated = findDesignTask(context, taskID);
            updated && !updated->terminated) {
          updated->signalTriggered = true;
          context->prioritySignalPending |= updated->prioritySignal;
          TALOS_RT_TRY { context->designPollCandidates.insert(taskID); }
          TALOS_RT_CATCH(const std::bad_alloc &) {
            context->schedulerStatus = TALOS_RT_OUT_OF_MEMORY;
            return false;
          }
          if (++context->schedulerSelectionGeneration == 0)
            context->schedulerSelectionGeneration = 1;
        }
        break;
      }
    }
  }
  return context->schedulerStatus == TALOS_RT_OK;
}
