//===- CoverageBlockEvents.cpp - Covergroup block-event sampling --------===//

#include "ProcessContext.h"
#include "ProcessShared.h"
#include "ProcessValidation.h"
#include "RuntimeInternal.h"
#include "talos/Runtime/StableHandle.h"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace talos::process;

namespace {

constexpr uint32_t kMaximumObserverDepth = 256;

bool validEventKind(talos_rt_covergroup_block_event_kind_v1 kind) {
  return kind == TALOS_RT_COVERGROUP_BLOCK_EVENT_BEGIN ||
         kind == TALOS_RT_COVERGROUP_BLOCK_EVENT_END;
}

bool validManagedObject(talos_rt_context *context,
                        talos_rt_object_v1 *object,
                        talos_rt_managed_kind_v1 expected) {
  return object && talos_rt_managed_object_context(object) == context &&
         talos_rt_managed_object_kind(object) == expected;
}

bool validPackedHandle(talos_rt_context *context, uint64_t handle,
                       uint32_t width) {
  if (!context || width == 0)
    return false;
  talos_rt_stable_handle_v1 decoded;
  if (!talos_rt_stable_handle_decode(handle, &decoded) || decoded.offset < 0)
    return false;
  uint64_t offset = static_cast<uint64_t>(decoded.offset);
  if (decoded.kind == TALOS_RT_STABLE_HANDLE_AUTOMATIC) {
    auto found = context->nativeAutomaticStates.find(decoded.id);
    return found != context->nativeAutomaticStates.end() &&
           offset <= found->second.bitWidth &&
           width <= found->second.bitWidth - offset;
  }
  if (decoded.kind == TALOS_RT_STABLE_HANDLE_STATIC) {
    auto found = context->nativeStaticStates.find(decoded.id);
    return found != context->nativeStaticStates.end() &&
           offset <= found->second.bitWidth &&
           width <= found->second.bitWidth - offset;
  }
  return decoded.kind == TALOS_RT_STABLE_HANDLE_GLOBAL &&
         context->execution && offset <= context->execution->state_bit_count &&
         width <= context->execution->state_bit_count - offset;
}

bool validArgumentReference(
    talos_rt_context *context,
    const talos_rt_observer_capture_abi_v1 &captureABI,
    const talos_rt_computed_capture_v1 &capture) {
  if (captureABI.width == 0 || capture.payload1 > 2 || capture.payload2 != 0 ||
      (capture.payload1 == 0 && capture.stable_id != 0) ||
      (capture.payload1 != 0 && capture.stable_id == 0))
    return false;
  if (capture.payload1 == 0)
    return validPackedHandle(context, capture.payload0, captureABI.width);

  talos_rt_object_v1 *owner =
      talos_rt_object_from_managed_word(capture.stable_id);
  talos_rt_managed_kind_v1 expected = capture.payload1 == 1
                                            ? TALOS_RT_MANAGED_CLASS
                                            : TALOS_RT_MANAGED_REFERENCE_PATH;
  if (!validManagedObject(context, owner, expected) ||
      talos_rt_managed_word_from_object(owner) != capture.stable_id)
    return false;
  if (expected == TALOS_RT_MANAGED_REFERENCE_PATH)
    return true;

  const talos_rt_class_descriptor_v1 *descriptor =
      talos_rt_managed_object_class_descriptor(owner);
  uint64_t byteWidth = (uint64_t{captureABI.width} + 7) / 8;
  return descriptor && capture.payload0 >= sizeof(void *) &&
         capture.payload0 <= descriptor->instance_size &&
         byteWidth <= descriptor->instance_size - capture.payload0;
}

bool validSamplerCapture(talos_rt_context *context,
                         talos_rt_covergroup_v1 handle, uint32_t index,
                         const talos_rt_observer_capture_abi_v1 &captureABI,
                         const talos_rt_computed_capture_v1 &capture) {
  if (captureABI.kind == TALOS_RT_OBSERVER_CAPTURE_ARGUMENT_REF)
    return validArgumentReference(context, captureABI, capture);
  if (capture.payload0 != 0 || capture.payload1 != 0 || capture.payload2 != 0)
    return false;
  switch (captureABI.kind) {
  case TALOS_RT_OBSERVER_CAPTURE_COVERGROUP:
    return index == 0 && captureABI.width == 64 && capture.stable_id == handle;
  case TALOS_RT_OBSERVER_CAPTURE_MANAGED: {
    talos_rt_object_v1 *object =
        talos_rt_object_from_managed_word(capture.stable_id);
    return captureABI.width == 64 && object &&
           talos_rt_managed_word_from_object(object) == capture.stable_id &&
           talos_rt_managed_object_context(object) == context;
  }
  case TALOS_RT_OBSERVER_CAPTURE_EVENT:
    return talos_rt_stable_handle_is_dynamic_event(capture.stable_id) ||
           validPackedHandle(context, capture.stable_id, captureABI.width);
  case TALOS_RT_OBSERVER_CAPTURE_STORAGE:
  case TALOS_RT_OBSERVER_CAPTURE_NET:
  case TALOS_RT_OBSERVER_CAPTURE_DRIVER:
    return validPackedHandle(context, capture.stable_id, captureABI.width);
  default:
    return false;
  }
}

uint64_t retainedHandle(const talos_rt_observer_capture_abi_v1 &captureABI,
                        const talos_rt_computed_capture_v1 &capture) {
  if (captureABI.kind == TALOS_RT_OBSERVER_CAPTURE_ARGUMENT_REF &&
      capture.payload1 == 0)
    return capture.payload0;
  if (captureABI.kind == TALOS_RT_OBSERVER_CAPTURE_STORAGE ||
      captureABI.kind == TALOS_RT_OBSERVER_CAPTURE_NET ||
      captureABI.kind == TALOS_RT_OBSERVER_CAPTURE_DRIVER)
    return capture.stable_id;
  return UINT64_MAX;
}

void releaseRetainedCaptures(
    talos_rt_context *context,
    const std::vector<uint64_t> &retainedAutomaticCaptures) {
  for (auto handle = retainedAutomaticCaptures.rbegin();
       handle != retainedAutomaticCaptures.rend(); ++handle)
    (void)talos_rt_v1_native_state_release(context, *handle, 0);
}

talos_rt_status
evaluateSamplerUnlocked(talos_rt_context *context,
                        const CovergroupBlockEventRegistration &registration) {
  const talos_rt_observer_descriptor_v1 *descriptor = findObserverDescriptor(
      registration.execution, registration.observerCodeUnitID);
  if (!descriptor ||
      descriptor->capture_count != registration.captures.size() ||
      descriptor->result_width != 1 || descriptor->flags != 0)
    return TALOS_RT_INVALID_DESIGN;
  if (context->observerDepth >= kMaximumObserverDepth)
    return TALOS_RT_OUT_OF_RESOURCES;

  uint64_t value = 0;
  uint64_t unknown = 0;
  talos_rt_status status = TALOS_RT_OK;
  bool previousCanonicalPlane = context->observerForcesCanonicalPlane;
  context->observerForcesCanonicalPlane = true;
  ++context->observerDepth;
  if (context->nativeStateSpecializationFast)
    *context->nativeStateSpecializationFast = 0;
  {
    ContextCallbackUnlock unlock(context);
    TALOS_RT_TRY {
      if (registration.native) {
        if (!descriptor->native_evaluator) {
          status = TALOS_RT_TIER_UNAVAILABLE;
        } else {
          status = descriptor->native_evaluator(
              context,
              reinterpret_cast<const uint64_t *>(registration.captures.data()),
              static_cast<uint32_t>(registration.captures.size()), &value,
              &unknown, 1);
        }
      } else if (descriptor->bytecode_function !=
                 TALOS_RT_OBSERVER_NO_BYTECODE) {
        status = talos_rt_execute_design_observer(
            *registration.execution, context, descriptor->bytecode_function,
            registration.captures.data(),
            static_cast<uint32_t>(registration.captures.size()), &value,
            &unknown, 1);
      } else {
        status = TALOS_RT_TIER_UNAVAILABLE;
      }
    }
    TALOS_RT_CATCH(const std::bad_alloc &) {
      status = TALOS_RT_OUT_OF_MEMORY;
    }
    TALOS_RT_CATCH_ALL { status = TALOS_RT_INVALID_ARGUMENT; }
  }
  --context->observerDepth;
  context->observerForcesCanonicalPlane = previousCanonicalPlane;
  if (context->nativeStateSpecializationFast)
    refreshNativeStaticSpecializationFastUnlocked(context);
  if (status != TALOS_RT_OK)
    return status;
  // Match the ordinary bound-observer contract: a one-bit two-state result
  // observes only its low value bit and discards an evaluator's unknown plane.
  return (value & 1) != 0 ? TALOS_RT_OK : TALOS_RT_INVALID_DESIGN;
}

void eraseEmptyBuckets(CovergroupBlockEventFeatureState &feature) {
  for (auto bucket = feature.buckets.begin();
       bucket != feature.buckets.end();) {
    if (bucket->second.empty())
      bucket = feature.buckets.erase(bucket);
    else
      ++bucket;
  }
}

} // namespace

extern "C" talos_rt_status talos_rt_v1_covergroup_block_event_register(
    talos_rt_context *context, talos_rt_covergroup_v1 handle,
    talos_rt_object_v1 *receiver, uint64_t observerCodeUnit,
    const talos_rt_computed_capture_v1 *captures, uint32_t captureCount,
    const uint64_t *targetIDs,
    const talos_rt_covergroup_block_event_kind_v1 *eventKinds,
    uint32_t eventCount) {
  if (!context || handle == 0 || observerCodeUnit == 0 || captureCount == 0 ||
      !captures || eventCount == 0 || !targetIDs || !eventKinds)
    return TALOS_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  TALOS_RT_TRY {
    ContextMutexLock lock(context);
    if (context->schedulerStatus != TALOS_RT_OK)
      return context->schedulerStatus;
    if (receiver &&
        !validManagedObject(context, receiver, TALOS_RT_MANAGED_CLASS))
      return TALOS_RT_INVALID_HANDLE;

    const talos_rt_execution_descriptor_v1 *execution = nullptr;
    bool native = false;
    if (context->activeNativeProcess) {
      if ((context->activeLogicalProcessToken &
           TALOS_RT_LOGICAL_PROCESS_NATIVE_TAG) == 0 ||
          !context->activeNativeProcess->descriptor)
        return TALOS_RT_INVALID_LIFECYCLE;
      execution = context->activeNativeProcess->descriptor->execution;
      native = context->activeNativeProcess->tier != TALOS_RT_TIER_BYTECODE;
    } else if (context->designTaskExecuting && context->activeDesignTask &&
               context->activeDesignTaskID != 0 &&
               context->activeLogicalProcessToken ==
                   context->activeDesignTaskID) {
      execution = context->execution;
    } else {
      return TALOS_RT_INVALID_LIFECYCLE;
    }

    const talos_rt_observer_descriptor_v1 *descriptor =
        findObserverDescriptor(execution, observerCodeUnit);
    if (!descriptor || descriptor->capture_count != captureCount ||
        descriptor->result_width != 1 || descriptor->flags != 0 ||
        descriptor->capture_abi[0].kind !=
            TALOS_RT_OBSERVER_CAPTURE_COVERGROUP)
      return TALOS_RT_INVALID_DESIGN;
    if (native && !descriptor->native_evaluator) {
      if (descriptor->bytecode_function == TALOS_RT_OBSERVER_NO_BYTECODE)
        return TALOS_RT_TIER_UNAVAILABLE;
      native = false;
    }
    if (!native &&
        descriptor->bytecode_function == TALOS_RT_OBSERVER_NO_BYTECODE)
      return TALOS_RT_TIER_UNAVAILABLE;

    uint32_t enabled = 0;
    talos_rt_status status =
        talos_rt_v1_covergroup_sample_enabled(context, handle, &enabled);
    if (status != TALOS_RT_OK)
      return status;
    (void)enabled;

    auto registration = std::make_unique<CovergroupBlockEventRegistration>();
    registration->covergroupHandle = handle;
    registration->receiver = receiver;
    registration->execution = execution;
    registration->observerCodeUnitID = observerCodeUnit;
    registration->native = native;
    registration->captures.assign(captures, captures + captureCount);
    registration->retainedAutomaticCaptures.reserve(captureCount);
    for (uint32_t index = 0; index != captureCount; ++index)
      if (!validSamplerCapture(context, handle, index,
                               descriptor->capture_abi[index],
                               registration->captures[index]))
        return TALOS_RT_INVALID_DESIGN;

    std::vector<std::pair<uint64_t, uint32_t>> keys;
    keys.reserve(eventCount);
    for (uint32_t index = 0; index != eventCount; ++index) {
      if (targetIDs[index] == 0 || !validEventKind(eventKinds[index]))
        return TALOS_RT_INVALID_DESIGN;
      keys.emplace_back(targetIDs[index], eventKinds[index]);
    }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

    CoverageState *coverage = context->coverage.get();
    if (!coverage)
      return TALOS_RT_INVALID_HANDLE;
    if (!coverage->blockEvents)
      coverage->blockEvents =
          std::make_unique<CovergroupBlockEventFeatureState>();
    CovergroupBlockEventFeatureState &feature = *coverage->blockEvents;
    if (feature.nextRegistration == 0)
      return TALOS_RT_OUT_OF_RESOURCES;
    uint64_t registrationID = feature.nextRegistration;
    registration->id = registrationID;

    // Reserve every forward edge before publishing the registration. This
    // gives allocation failures a strong guarantee without copying the whole
    // context-owned index.
    for (const auto &key : keys) {
      auto [bucket, inserted] = feature.buckets.try_emplace(key);
      (void)inserted;
      bucket->second.reserve(bucket->second.size() + 1);
    }
    auto [stored, inserted] =
        feature.registrations.emplace(registrationID, std::move(registration));
    if (!inserted || !stored->second)
      return TALOS_RT_OUT_OF_RESOURCES;

    for (uint32_t index = 0; index != captureCount; ++index) {
      uint64_t retained =
          retainedHandle(descriptor->capture_abi[index], captures[index]);
      if (retained == UINT64_MAX)
        continue;
      status = talos_rt_v1_native_state_retain(context, retained);
      if (status != TALOS_RT_OK) {
        releaseRetainedCaptures(context,
                                stored->second->retainedAutomaticCaptures);
        feature.registrations.erase(stored);
        eraseEmptyBuckets(feature);
        return status;
      }
      stored->second->retainedAutomaticCaptures.push_back(retained);
    }
    for (const auto &key : keys)
      feature.buckets.find(key)->second.push_back(registrationID);
    feature.nextRegistration = registrationID + 1;
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) {
    if (context->coverage && context->coverage->blockEvents)
      eraseEmptyBuckets(*context->coverage->blockEvents);
    return TALOS_RT_OUT_OF_MEMORY;
  }
  TALOS_RT_CATCH(const std::length_error &) {
    if (context->coverage && context->coverage->blockEvents)
      eraseEmptyBuckets(*context->coverage->blockEvents);
    return TALOS_RT_OUT_OF_RESOURCES;
  }
  TALOS_RT_CATCH_ALL { return TALOS_RT_INVALID_DESIGN; }
}

extern "C" talos_rt_status talos_rt_v1_covergroup_block_event_fire(
    talos_rt_context *context, uint64_t targetID,
    talos_rt_covergroup_block_event_kind_v1 eventKind,
    talos_rt_object_v1 *receiver) {
  if (!context || targetID == 0 || !validEventKind(eventKind))
    return TALOS_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  TALOS_RT_TRY {
    ContextMutexLock lock(context);
    if (context->schedulerStatus != TALOS_RT_OK)
      return context->schedulerStatus;
    if (receiver &&
        !validManagedObject(context, receiver, TALOS_RT_MANAGED_CLASS))
      return TALOS_RT_INVALID_HANDLE;
    CoverageState *coverage = context->coverage.get();
    if (!coverage || !coverage->blockEvents)
      return TALOS_RT_OK;
    CovergroupBlockEventFeatureState &feature = *coverage->blockEvents;
    auto bucket = feature.buckets.find({targetID, eventKind});
    if (bucket == feature.buckets.end())
      return TALOS_RT_OK;

    // Evaluators may reenter and register another covergroup. Snapshot only
    // stable IDs so the new registration starts with the next block event and
    // no unordered-map rehash can invalidate this dispatch.
    std::vector<uint64_t> candidates = bucket->second;
    std::unordered_set<uint64_t> sampledCovergroups;
    sampledCovergroups.reserve(candidates.size());
    for (uint64_t registrationID : candidates) {
      auto found = feature.registrations.find(registrationID);
      if (found == feature.registrations.end() || !found->second ||
          (receiver && found->second->receiver != receiver))
        continue;
      const uint64_t handle = found->second->covergroupHandle;
      if (!sampledCovergroups.insert(handle).second)
        continue;
      uint32_t enabled = 0;
      talos_rt_status status =
          talos_rt_v1_covergroup_sample_enabled(context, handle, &enabled);
      if (status != TALOS_RT_OK) {
        context->schedulerStatus = status;
        return status;
      }
      if (!enabled)
        continue;
      status = evaluateSamplerUnlocked(context, *found->second);
      if (status != TALOS_RT_OK) {
        context->schedulerStatus = status;
        return status;
      }
    }
    return TALOS_RT_OK;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) {
    context->schedulerStatus = TALOS_RT_OUT_OF_MEMORY;
    return TALOS_RT_OUT_OF_MEMORY;
  }
  TALOS_RT_CATCH(const std::length_error &) {
    context->schedulerStatus = TALOS_RT_OUT_OF_RESOURCES;
    return TALOS_RT_OUT_OF_RESOURCES;
  }
  TALOS_RT_CATCH_ALL {
    context->schedulerStatus = TALOS_RT_INVALID_DESIGN;
    return TALOS_RT_INVALID_DESIGN;
  }
}
