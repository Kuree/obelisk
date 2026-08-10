//===- DesignBytecodeObservers.cpp - Computed observer scheduling --------===//

#include "DesignBytecodeNets.h"
#include "ProcessValidation.h"
#include "RuntimeInternal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <vector>

namespace {

using namespace obelisk::designbytecode;
using namespace obelisk::process;

constexpr uint32_t kMaximumObserverDepth = 256;

ScheduledDesignTask *findDesignTask(obelisk_rt_context *context, uint64_t id) {
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

obelisk_rt_computed_wait_record_v1 *
currentComputedWait(ScheduledDesignTask &task) {
  if (task.terminated || !task.started ||
      task.suspendKind != OBELISK_RT_SUSPEND_OBSERVER ||
      task.waitSize < sizeof(obelisk_rt_computed_wait_record_v1) ||
      task.waitOffset > task.scratchOffset ||
      task.waitSize > task.scratchOffset - task.waitOffset)
    return nullptr;
  auto *wait = reinterpret_cast<obelisk_rt_computed_wait_record_v1 *>(
      task.frame.data() + task.waitOffset);
  return wait->version == OBELISK_RT_VERSION &&
                 wait->kind == OBELISK_RT_SUSPEND_OBSERVER &&
                 wait->total_size <= task.waitSize
             ? wait
             : nullptr;
}

bool evaluateObserver(obelisk_rt_context *context, uint64_t taskID,
                      uint32_t observerIndex, std::vector<uint64_t> &value,
                      std::vector<uint64_t> &unknown) {
  ScheduledDesignTask *task = findDesignTask(context, taskID);
  obelisk_rt_computed_wait_record_v1 *wait =
      task ? currentComputedWait(*task) : nullptr;
  if (!task || !wait || observerIndex >= wait->observer_count)
    return false;
  auto *observers = computedWaitSpan<obelisk_rt_computed_observer_v1>(
      wait, wait->observers_offset, wait->observer_count);
  auto *captures = computedWaitSpan<obelisk_rt_computed_capture_v1>(
      wait, wait->captures_offset, wait->capture_count);
  if (!observers || !captures)
    return false;
  obelisk_rt_computed_observer_v1 binding = observers[observerIndex];
  const obelisk_rt_observer_descriptor_v1 *descriptor =
      findObserverDescriptor(context->execution, binding.code_unit_id);
  if (!descriptor || descriptor->capture_count != binding.capture_count ||
      descriptor->bytecode_function == OBELISK_RT_OBSERVER_NO_BYTECODE)
    return false;
  if (context->observerDepth >= kMaximumObserverDepth) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
    return false;
  }
  std::vector<obelisk_rt_computed_capture_v1> copiedCaptures(
      captures + binding.capture_begin,
      captures + binding.capture_begin + binding.capture_count);
  uint32_t limbs =
      static_cast<uint32_t>((uint64_t{descriptor->result_width} + 63) / 64);
  value.assign(limbs, 0);
  unknown.assign(limbs, 0);

  std::vector<uint64_t> retainedCaptures;
  retainedCaptures.reserve(binding.capture_count);
  obelisk_rt_status status = OBELISK_RT_OK;
  for (uint32_t index = 0; index != binding.capture_count; ++index) {
    if (descriptor->capture_abi[index].kind !=
        OBELISK_RT_OBSERVER_CAPTURE_STORAGE)
      continue;
    status = obelisk_rt_v1_native_state_retain(context,
                                               copiedCaptures[index].stable_id);
    if (status != OBELISK_RT_OK)
      break;
    retainedCaptures.push_back(copiedCaptures[index].stable_id);
  }
  if (status != OBELISK_RT_OK) {
    for (auto capture = retainedCaptures.rbegin();
         capture != retainedCaptures.rend(); ++capture)
      (void)obelisk_rt_v1_native_state_release(context, *capture, 0);
    context->schedulerStatus = status;
    return false;
  }

  obelisk_rt_process_instance_v1 *producer = context->activeNativeProcess;
  uint64_t producerToken = context->activeLogicalProcessToken;
  uint64_t producerTask = context->activeDesignTaskID;
  bool producerExecuting = context->designTaskExecuting;
  std::vector<uint64_t> producerControls = std::move(context->activeControls);
  std::vector<uint64_t> waiterControls = task->controls;
  context->activeNativeProcess = nullptr;
  context->activeLogicalProcessToken = taskID;
  context->activeDesignTaskID = taskID;
  context->designTaskExecuting = true;
  context->activeControls = std::move(waiterControls);
  ++context->observerDepth;
  {
    ContextCallbackUnlock unlock(context);
    status = obelisk_rt_execute_design_observer(
        *context->execution, context, descriptor->bytecode_function,
        copiedCaptures.data(), binding.capture_count, value.data(),
        unknown.data(), limbs);
  }
  --context->observerDepth;
  waiterControls = std::move(context->activeControls);
  if (ScheduledDesignTask *updated = findDesignTask(context, taskID);
      updated && !updated->terminated)
    updated->controls = std::move(waiterControls);
  context->activeControls = std::move(producerControls);
  context->activeNativeProcess = producer;
  context->activeLogicalProcessToken = producerToken;
  context->activeDesignTaskID = producerTask;
  context->designTaskExecuting = producerExecuting;
  for (auto capture = retainedCaptures.rbegin();
       capture != retainedCaptures.rend(); ++capture) {
    obelisk_rt_status releaseStatus =
        obelisk_rt_v1_native_state_release(context, *capture, 0);
    if (status == OBELISK_RT_OK && releaseStatus != OBELISK_RT_OK)
      status = releaseStatus;
  }
  if (status != OBELISK_RT_OK) {
    context->schedulerStatus = status;
    return false;
  }
  if (descriptor->result_width % 64 != 0) {
    uint64_t mask = (uint64_t{1} << (descriptor->result_width % 64)) - 1;
    value.back() &= mask;
    unknown.back() &= mask;
  }
  if ((descriptor->flags & OBELISK_RT_OBSERVER_FOUR_STATE) == 0)
    std::fill(unknown.begin(), unknown.end(), 0);
  return true;
}

} // namespace

bool obelisk_rt_evaluate_design_observers_unlocked(obelisk_rt_context *context,
                                                   uint32_t dependencyKind,
                                                   uint64_t publishedHandle,
                                                   uint64_t publishedWidth) {
  if (!context || context->schedulerStatus != OBELISK_RT_OK)
    return context != nullptr;
  if (!context->execution)
    return true;

  std::vector<uint64_t> taskIDs;
  if (dependencyKind == OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL)
    taskIDs.swap(context->pendingDesignComputedWaiters);
  else {
    taskIDs.reserve(context->scheduledDesignTasks.size());
    for (const ScheduledDesignTask &task : context->scheduledDesignTasks)
      if (!task.terminated && task.started &&
          task.suspendKind == OBELISK_RT_SUSPEND_OBSERVER &&
          !task.signalTriggered)
        taskIDs.push_back(task.id);
  }

  for (uint64_t taskID : taskIDs) {
    ScheduledDesignTask *task = findDesignTask(context, taskID);
    obelisk_rt_computed_wait_record_v1 *wait =
        task ? currentComputedWait(*task) : nullptr;
    if (!task || task->signalTriggered || !wait)
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
        if (dependencyKind == OBELISK_RT_OBSERVER_DEPENDENCY_EVENT
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
        try {
          context->pendingDesignComputedWaiters.push_back(taskID);
        } catch (const std::bad_alloc &) {
          context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
          return false;
        }
        continue;
      }
      if (task->signalLatch)
        task->signalLatch->affected = false;
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
      if (!evaluateObserver(context, taskID, clause.primary_observer, value,
                            unknown))
        return false;
      task = findDesignTask(context, taskID);
      wait = task ? currentComputedWait(*task) : nullptr;
      if (!task || task->terminated || task->signalTriggered || !wait)
        break;
      const obelisk_rt_observer_descriptor_v1 *descriptor =
          findObserverDescriptor(context->execution, primary.code_unit_id);
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
          try {
            context->designPollCandidates.insert(taskID);
          } catch (const std::bad_alloc &) {
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
