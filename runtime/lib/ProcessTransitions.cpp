//===- ProcessTransitions.cpp - Signal occurrence publication ------------===//
//
// Publication of signal occurrences and value transitions: subscription
// matching and wakeup, the indexed and static AOT fanout fast paths, and the
// scheduler entry points generated code calls to report signal, static, real,
// and event transitions.  Split out of Process.cpp.
//
//===----------------------------------------------------------------------===//

#include "ProcessAllocation.h"
#include "ProcessContext.h"
#include "ProcessObservers.h"
#include "ProcessPacking.h"
#include "ProcessShared.h"
#include "ProcessSignals.h"
#include "ProcessValidation.h"
#include "RuntimeInternal.h"
#include "SignalSemantics.h"
#include "obelisk/Runtime/StableHandle.h"
#include "obelisk/Runtime/StableHash.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <tuple>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

using namespace obelisk::process;
using namespace obelisk::runtime;

bool obelisk_rt_read_signal_bit_unlocked(obelisk_rt_context *context,
                                         uint64_t stableID, uint64_t bit,
                                         bool &value, bool &unknown,
                                         bool useSnapshot) {
  value = false;
  unknown = false;
  if (!context || stableID == UINT64_MAX)
    return false;
  obelisk_rt_stable_handle_v1 decoded;
  if (!obelisk_rt_stable_handle_decode(stableID, &decoded) ||
      decoded.offset < 0 || bit > uint64_t{INT64_MAX})
    return false;
  uint64_t relative = static_cast<uint64_t>(decoded.offset) + bit;
  if (relative < static_cast<uint64_t>(decoded.offset))
    return false;
  uint64_t indexed =
      obelisk_rt_stable_handle_offset(stableID, static_cast<int64_t>(bit));
  auto snapshot = !useSnapshot || indexed == UINT64_MAX
                      ? context->signalValueSnapshots.end()
                      : context->signalValueSnapshots.find(indexed);
  if (snapshot != context->signalValueSnapshots.end()) {
    value = snapshot->second.value;
    unknown = snapshot->second.unknown;
    return true;
  }
  if (decoded.kind == OBELISK_RT_STABLE_HANDLE_AUTOMATIC) {
    auto found = context->nativeAutomaticStates.find(decoded.id);
    if (found == context->nativeAutomaticStates.end() ||
        relative >= found->second.bitWidth)
      return false;
    value = !found->second.value.empty() &&
            ((found->second.value[relative / 8] >> (relative % 8)) & 1) != 0;
    unknown =
        !found->second.unknown.empty() &&
        ((found->second.unknown[relative / 8] >> (relative % 8)) & 1) != 0;
    return true;
  }
  uint64_t absolute = relative;
  if (decoded.kind == OBELISK_RT_STABLE_HANDLE_STATIC) {
    const NativeStaticState *storage =
        findNativeStaticState(context, decoded.id);
    if (!storage || relative >= storage->bitWidth)
      return false;
    absolute = storage->bitOffset + relative;
  } else if (decoded.kind != OBELISK_RT_STABLE_HANDLE_GLOBAL) {
    return false;
  }
  if (absolute >= context->stateValue.size() * uint64_t{64} ||
      absolute >= context->stateUnknown.size() * uint64_t{64})
    return false;
  uint64_t mask = uint64_t{1} << (absolute % 64);
  value = (context->stateValue[absolute / 64] & mask) != 0;
  unknown = (context->stateUnknown[absolute / 64] & mask) != 0;
  return true;
}

namespace {

void finalizeClockOccurrenceWave(ClockOccurrenceWaitState &state) {
  if (!state.hasCurrentKey)
    return;
  state.finalizedCohorts.insert(state.finalizedCohorts.end(),
                                state.currentCohorts.begin(),
                                state.currentCohorts.end());
  state.currentCohorts.clear();
  state.occurrenceCounts.fill(0);
  state.hasCurrentKey = false;
}

constexpr size_t kMaximumNoChangePendingEntries = size_t{1} << 20;

bool addNoChangeCount(obelisk_rt_context *context, uint64_t &target,
                      uint64_t increment) {
  if (increment > std::numeric_limits<uint64_t>::max() - target) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
    return false;
  }
  target += increment;
  return true;
}

bool noChangeContains(const NoChangeClosedWindow &window, uint64_t time) {
  __int128 wide = static_cast<__int128>(time);
  return window.begin < wide && wide < window.end;
}

void pruneNoChangeData(NoChangeCheckState &state, uint64_t now) {
  __int128 futureBegin =
      static_cast<__int128>(now) - std::max<int64_t>(state.startOffset, 0);
  __int128 oldestBegin =
      state.open ? std::min(state.openBegin, futureBegin) : futureBegin;
  while (state.dataBegin != state.data.size() &&
         static_cast<__int128>(state.data[state.dataBegin].time) <= oldestBegin)
    ++state.dataBegin;
  if (state.open)
    state.openDataBegin = std::max(state.openDataBegin, state.dataBegin);
  if (state.dataBegin >= 1024 && state.dataBegin * 2 >= state.data.size()) {
    size_t erased = state.dataBegin;
    state.data.erase(state.data.begin(), state.data.begin() + state.dataBegin);
    state.dataBegin = 0;
    if (state.open)
      state.openDataBegin -= std::min(state.openDataBegin, erased);
  }
}

bool recordNoChangeData(obelisk_rt_context *context, NoChangeCheckState &state,
                        uint64_t now) {
  for (const NoChangeClosedWindow &window : state.windows)
    if (noChangeContains(window, now) &&
        !addNoChangeCount(context, state.pendingReports, window.multiplicity))
      return false;
  if (!state.data.empty() && state.data.back().time == now)
    return addNoChangeCount(context, state.data.back().count, 1);
  if (state.data.size() - state.dataBegin >= kMaximumNoChangePendingEntries) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
    return false;
  }
  state.data.push_back({now, 1});
  return true;
}

bool closeNoChangeWindow(obelisk_rt_context *context, NoChangeCheckState &state,
                         uint64_t now) {
  if (!state.open)
    return true;
  NoChangeClosedWindow closed{state.openBegin,
                              static_cast<__int128>(now) + state.endOffset, 1};
  state.open = false;
  if (closed.begin >= closed.end)
    return true;
  for (size_t index = std::max(state.dataBegin, state.openDataBegin);
       index != state.data.size(); ++index)
    if (noChangeContains(closed, state.data[index].time) &&
        !addNoChangeCount(context, state.pendingReports,
                          state.data[index].count))
      return false;
  if (closed.end <= static_cast<__int128>(now))
    return true;
  for (NoChangeClosedWindow &window : state.windows)
    if (window.begin == closed.begin && window.end == closed.end)
      return addNoChangeCount(context, window.multiplicity, 1);
  if (state.windows.size() >= kMaximumNoChangePendingEntries) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
    return false;
  }
  state.windows.push_back(closed);
  return true;
}

bool finalizeNoChangeOpenWindow(obelisk_rt_context *context,
                                NoChangeCheckState &state) {
  if (!state.open || state.endOffset < 0)
    return true;
  // IEEE 1800-2017 31.4.6 makes data after the open beginning certain as soon
  // as a complete numeric slot has no trailing edge: a future trailing edge
  // plus a nonnegative end offset must lie later. Clause 31.6 therefore
  // publishes these reports now, including positive-start history discovered
  // by the leading edge, instead of delaying the notifier until the trailing
  // edge. Only a negative end offset remains genuinely uncertain.
  for (size_t index = std::max(state.dataBegin, state.openDataBegin);
       index != state.data.size(); ++index) {
    const NoChangeDataOccurrence &data = state.data[index];
    if (state.openBegin < static_cast<__int128>(data.time) &&
        !addNoChangeCount(context, state.pendingReports, data.count))
      return false;
  }
  state.openDataBegin = state.data.size();
  return true;
}

bool locateClockPublicationBit(const ClockConditionPublicationView *sample,
                               uint64_t stableID, uint64_t &planeBit) {
  if (!sample || sample->bitWidth == 0)
    return false;
  obelisk_rt_stable_handle_v1 published;
  obelisk_rt_stable_handle_v1 conditioned;
  if (!obelisk_rt_stable_handle_decode(sample->stableID, &published) ||
      !obelisk_rt_stable_handle_decode(stableID, &conditioned) ||
      published.offset < 0 || conditioned.offset < 0 ||
      published.kind != conditioned.kind ||
      (published.kind != OBELISK_RT_STABLE_HANDLE_GLOBAL &&
       published.id != conditioned.id))
    return false;
  __int128 bit = static_cast<__int128>(conditioned.offset) - published.offset;
  if (bit < 0 || bit >= sample->bitWidth ||
      bit > UINT64_MAX - sample->planeBitOffset)
    return false;
  planeBit = sample->planeBitOffset + static_cast<uint64_t>(bit);
  return true;
}

class ClockConditionPublicationOverlay {
public:
  ClockConditionPublicationOverlay(obelisk_rt_context *context,
                                   const ClockConditionPublicationView *sample)
      : context(context),
        previousCanonicalPlane(context->observerForcesCanonicalPlane),
        previousPublication(context->conditionPublication) {
    context->observerForcesCanonicalPlane = true;
    if (sample) {
      publication = *sample;
      publication.previous = context->conditionPublication;
      context->conditionPublication = &publication;
    }
  }

  ClockConditionPublicationOverlay(const ClockConditionPublicationOverlay &) =
      delete;
  ClockConditionPublicationOverlay &
  operator=(const ClockConditionPublicationOverlay &) = delete;

  ~ClockConditionPublicationOverlay() {
    context->conditionPublication = previousPublication;
    context->observerForcesCanonicalPlane = previousCanonicalPlane;
  }

private:
  obelisk_rt_context *context = nullptr;
  bool previousCanonicalPlane = false;
  const ClockConditionPublicationView *previousPublication = nullptr;
  ClockConditionPublicationView publication;
};

bool readClockOccurrenceCondition(obelisk_rt_context *context,
                                  const ClockOccurrenceCondition &condition,
                                  const ClockConditionPublicationView *sample,
                                  uint64_t waiterToken, bool native) {
  if (!context)
    return false;
  if (!condition.isObserver() &&
      (condition.stableID == UINT64_MAX || condition.width == 0))
    return condition.stableID == UINT64_MAX;
  uint32_t predicate = condition.predicate;
  uint64_t value = 0;
  uint64_t unknown = 0;
  // IEEE 1800-2017 31.7 uses only the LSB when the conditioning net or
  // expression is multibit. Sampling one bit also keeps this event hot path
  // independent of the declared vector width.
  // IEEE 1800-2017 31.7 samples the condition in the controlled event. A
  // generated AOT plane may already contain later source stores, while its
  // canonical mirror can still precede this publication. Use only this
  // publication's transient post-transition plane when the condition aliases
  // it; otherwise retain the ordinary live read. This state is pay-for-play
  // and never enters an ABI record or a persistent lookup table.
  if (condition.isObserver()) {
    // IEEE 1800-2017 31.7 samples the complete expression in the controlled
    // event. Native publication intentionally trails generated stores, so the
    // compiled observer gets a scoped post-transition canonical overlay. It
    // exists only on this computed-condition path and is restored by RAII even
    // when an evaluator reports or throws; ordinary signal loads and direct
    // timing conditions retain their allocation-free ABI and hot path.
    ClockConditionPublicationOverlay overlay(context, sample);
    bool evaluated =
        native ? obelisk_rt_evaluate_native_clock_condition_unlocked(
                     context, waiterToken, condition.observerCodeUnitID,
                     condition.observerCaptures.data(),
                     condition.observerCaptures.size(), value, unknown)
               : obelisk_rt_evaluate_design_clock_condition_unlocked(
                     context, waiterToken, condition.observerCodeUnitID,
                     condition.observerCaptures.data(),
                     condition.observerCaptures.size(), value, unknown);
    if (!evaluated)
      return false;
  } else {
    uint64_t publicationBit = 0;
    bool aliasesPublication =
        locateClockPublicationBit(sample, condition.stableID, publicationBit);
    bool bitValue = false;
    bool bitUnknown = false;
    if (aliasesPublication && sample->newValue) {
      bitValue = byteBit(sample->newValue, publicationBit);
      bitUnknown =
          sample->newUnknown && byteBit(sample->newUnknown, publicationBit);
    } else if (!obelisk_rt_read_signal_bit_unlocked(
                   context, condition.stableID, 0, bitValue, bitUnknown,
                   /*useSnapshot=*/!aliasesPublication)) {
      return false;
    }
    value = bitValue;
    unknown = bitUnknown;
  }
  // IEEE 1800-2017 31.7 explicitly distinguishes nondeterministic ==/!=
  // from deterministic forms: any X enables the former and disables the
  // latter. The direct packed handle is sampled in this publication, never
  // reconstructed later from an Observed-region live value.
  switch (predicate) {
  case OBELISK_RT_WAIT_CONDITION_KNOWN_ONE:
    return !unknown && value;
  case OBELISK_RT_WAIT_CONDITION_KNOWN_ZERO:
    return !unknown && !value;
  case OBELISK_RT_WAIT_CONDITION_LOGICAL_EQ_ZERO:
    return unknown || !value;
  case OBELISK_RT_WAIT_CONDITION_LOGICAL_EQ_ONE:
    return unknown || value;
  case OBELISK_RT_WAIT_CONDITION_LOGICAL_NE_ZERO:
    return unknown || value;
  case OBELISK_RT_WAIT_CONDITION_LOGICAL_NE_ONE:
    return unknown || !value;
  case OBELISK_RT_WAIT_CONDITION_CASE_EQ_ZERO:
    return !unknown && !value;
  case OBELISK_RT_WAIT_CONDITION_CASE_EQ_ONE:
    return !unknown && value;
  case OBELISK_RT_WAIT_CONDITION_CASE_NE_ZERO:
    return !unknown && value;
  case OBELISK_RT_WAIT_CONDITION_CASE_NE_ONE:
    return !unknown && !value;
  default:
    return false;
  }
}

bool evaluateCovergroupClockEventsNowUnlocked(
    obelisk_rt_context *context, uint64_t sequence, uint32_t dependencyKind,
    uint32_t kind, uint32_t objectID, int64_t firstPage, int64_t lastPage,
    uint64_t stableID, uint64_t bitWidth,
    const ClockConditionPublicationView *sample) {
  CovergroupClockEventFeatureState *feature =
      context->covergroupClockEvents.get();
  if (!feature)
    return true;
  struct Candidate {
    uint64_t ownerLogicalToken = 0;
    uint32_t clauseIndex = 0;
  };
  std::vector<Candidate> candidates;
  std::vector<uint64_t> sampleOwners;
  auto considerClause = [&](CovergroupClockEventClause *clause) {
    if (!clause || clause->lastExaminedSequence == sequence)
      return;
    clause->lastExaminedSequence = sequence;
    if (context->signalDiagnosticsEnabled)
      ++context->signalDiagnostics.subscribersExamined;
    if (!clause->registration)
      return;
    auto *plan = reinterpret_cast<obelisk_rt_computed_wait_record_v1 *>(
        clause->registration->eventPlan.data());
    auto *observers = computedWaitSpan<obelisk_rt_computed_observer_v1>(
        plan, plan->observers_offset, plan->observer_count);
    auto *dependencies = computedWaitSpan<obelisk_rt_computed_dependency_v1>(
        plan, plan->dependencies_offset, plan->dependency_count);
    if (!observers || !dependencies ||
        clause->clauseIndex >= plan->clause_count)
      return;
    const auto &primary = observers[clause->clauseIndex];
    bool affected = false;
    for (uint32_t dependencyIndex = 0;
         dependencyIndex != primary.dependency_count; ++dependencyIndex) {
      const auto &dependency =
          dependencies[primary.dependency_begin + dependencyIndex];
      if (dependency.kind != dependencyKind)
        continue;
      affected |= dependencyKind == OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL
                      ? rangesOverlap(dependency.stable_id, dependency.width,
                                      stableID, bitWidth)
                      : dependency.stable_id == stableID;
    }
    if (affected)
      candidates.push_back(
          {clause->registration->ownerLogicalToken, clause->clauseIndex});
  };
  auto visitBucket = [&](int64_t page) {
    SignalSubscriptionBucketKey key{kind, objectID, page};
    auto bucket = feature->subscriptionBuckets.find(key);
    if (bucket == feature->subscriptionBuckets.end())
      return true;
    for (const CovergroupClockEventBucketEntry &entry : bucket->second) {
      considerClause(entry.clause);
    }
    return true;
  };
  OBELISK_RT_TRY {
    if (dependencyKind == OBELISK_RT_OBSERVER_DEPENDENCY_MANAGED) {
      for (const auto &[owner, registration] : feature->registrations) {
        (void)owner;
        if (!registration)
          continue;
        for (const auto &clause : registration->clauses)
          considerClause(clause.get());
      }
    } else {
      __int128 pageCount = static_cast<__int128>(lastPage) - firstPage + 1;
      if (pageCount <= kMaximumIndexedSignalPages) {
        if (!visitBucket(kWideSignalSubscriptionPage))
          return false;
        for (int64_t page = firstPage;; ++page) {
          if (!visitBucket(page))
            return false;
          if (page == lastPage)
            break;
        }
      } else {
        std::vector<int64_t> pages;
        for (const auto &[key, bucket] : feature->subscriptionBuckets) {
          (void)bucket;
          if (key.kind == kind && key.id == objectID)
            pages.push_back(key.page);
        }
        for (int64_t page : pages)
          if (!visitBucket(page))
            return false;
      }
    }
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
    return false;
  }

  auto evaluateObserver =
      [&](CovergroupClockEventRegistration &registration,
          uint32_t observerIndex, std::vector<uint64_t> &value,
          std::vector<uint64_t> &unknown, uint32_t &resultWidth) -> bool {
    auto *plan = reinterpret_cast<obelisk_rt_computed_wait_record_v1 *>(
        registration.eventPlan.data());
    auto *observers = computedWaitSpan<obelisk_rt_computed_observer_v1>(
        plan, plan->observers_offset, plan->observer_count);
    auto *captures = computedWaitSpan<obelisk_rt_computed_capture_v1>(
        plan, plan->captures_offset, plan->capture_count);
    if (!observers || !captures || observerIndex >= plan->observer_count)
      return false;
    const obelisk_rt_computed_observer_v1 binding = observers[observerIndex];
    const obelisk_rt_execution_descriptor_v1 *execution = context->execution;
    uint64_t token = registration.ownerLogicalToken;
    if (registration.native) {
      token &= ~kNativeLogicalProcessTag;
      ScheduledProcess *process = findScheduledProcess(context, token);
      execution = process && process->instance && process->instance->descriptor
                      ? process->instance->descriptor->execution
                      : nullptr;
    }
    const obelisk_rt_observer_descriptor_v1 *descriptor =
        findObserverDescriptor(execution, binding.code_unit_id);
    if (!descriptor || binding.capture_begin > plan->capture_count ||
        binding.capture_count > plan->capture_count - binding.capture_begin)
      return false;
    resultWidth = descriptor->result_width;
    uint32_t limbs = static_cast<uint32_t>((uint64_t{resultWidth} + 63) / 64);
    if (limbs == 0)
      return false;
    std::vector<obelisk_rt_computed_capture_v1> copiedCaptures;
    OBELISK_RT_TRY {
      copiedCaptures.assign(captures + binding.capture_begin,
                            captures + binding.capture_begin +
                                binding.capture_count);
      value.assign(limbs, 0);
      unknown.assign(limbs, 0);
    }
    OBELISK_RT_CATCH(const std::bad_alloc &) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
      return false;
    }
    OBELISK_RT_CATCH(const std::length_error &) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return false;
    }
    return registration.native
               ? obelisk_rt_evaluate_native_bound_observer_unlocked(
                     context, token, binding.code_unit_id,
                     copiedCaptures.data(), copiedCaptures.size(), value.data(),
                     unknown.data(), limbs)
               : obelisk_rt_evaluate_design_bound_observer_unlocked(
                     context, token, binding.code_unit_id,
                     copiedCaptures.data(), copiedCaptures.size(), value.data(),
                     unknown.data(), limbs);
  };

  for (const Candidate &candidate : candidates) {
    // Evaluating a compiled observer can cross a callback boundary that
    // temporarily releases the context mutex. Reentrant code may terminate an
    // owner and erase its registration, so re-resolve by stable owner token
    // and copy every field needed by the evaluator before calling it.
    CovergroupClockEventFeatureState *currentFeature =
        context->covergroupClockEvents.get();
    if (!currentFeature)
      break;
    auto found =
        currentFeature->registrations.find(candidate.ownerLogicalToken);
    if (found == currentFeature->registrations.end() || !found->second)
      continue;
    CovergroupClockEventRegistration &registrationBeforeEvaluation =
        *found->second;
    auto *plan = reinterpret_cast<obelisk_rt_computed_wait_record_v1 *>(
        registrationBeforeEvaluation.eventPlan.data());
    auto *clauses = computedWaitSpan<obelisk_rt_computed_clause_v1>(
        plan, plan->clauses_offset, plan->clause_count);
    auto *observers = computedWaitSpan<obelisk_rt_computed_observer_v1>(
        plan, plan->observers_offset, plan->observer_count);
    if (!clauses || !observers || candidate.clauseIndex >= plan->clause_count)
      return false;
    obelisk_rt_computed_clause_v1 clause = clauses[candidate.clauseIndex];
    uint32_t resultWidth = 0;
    std::vector<uint64_t> value;
    std::vector<uint64_t> unknown;
    ClockConditionPublicationOverlay overlay(context, sample);
    if (!evaluateObserver(registrationBeforeEvaluation, clause.primary_observer,
                          value, unknown, resultWidth))
      return false;

    // Observer evaluation can release the context lock. Re-resolve the
    // registration before reading or updating its retained previous result.
    currentFeature = context->covergroupClockEvents.get();
    if (!currentFeature)
      break;
    found = currentFeature->registrations.find(candidate.ownerLogicalToken);
    if (found == currentFeature->registrations.end() || !found->second)
      continue;
    CovergroupClockEventRegistration &current = *found->second;
    plan = reinterpret_cast<obelisk_rt_computed_wait_record_v1 *>(
        current.eventPlan.data());
    clauses = computedWaitSpan<obelisk_rt_computed_clause_v1>(
        plan, plan->clauses_offset, plan->clause_count);
    observers = computedWaitSpan<obelisk_rt_computed_observer_v1>(
        plan, plan->observers_offset, plan->observer_count);
    if (!clauses || !observers || candidate.clauseIndex >= plan->clause_count)
      return false;
    clause = clauses[candidate.clauseIndex];
    const auto &primary = observers[clause.primary_observer];
    uint32_t limbs = static_cast<uint32_t>((uint64_t{resultWidth} + 63) / 64);
    auto *previousValue = reinterpret_cast<uint64_t *>(
        reinterpret_cast<uint8_t *>(plan) + primary.previous_offset);
    auto *previousUnknown = previousValue + limbs;
    bool changed = false;
    for (uint32_t limb = 0; limb != limbs; ++limb)
      changed |= previousValue[limb] != value[limb] ||
                 previousUnknown[limb] != unknown[limb];
    uint32_t observedEdges = transitionEdges(
        (previousValue[0] & 1) != 0, (previousUnknown[0] & 1) != 0,
        (value[0] & 1) != 0, (unknown[0] & 1) != 0);
    for (uint32_t limb = 0; limb != limbs; ++limb) {
      previousValue[limb] = value[limb];
      previousUnknown[limb] = unknown[limb];
    }
    bool occurrence = clause.edge == OBELISK_RT_WAIT_EDGE_CHANGE
                          ? changed
                          : signalEdgeMatches(clause.edge, observedEdges);
    if (!occurrence)
      continue;
    if (clause.condition_observer != OBELISK_RT_OBSERVER_CONDITION_NONE) {
      // IEEE 1800-2023 9.4.2.3 evaluates iff only at the primary event
      // instant. The publication overlay makes the just-published value
      // visible even before its canonical storage commit.
      if (!evaluateObserver(current, clause.condition_observer, value, unknown,
                            resultWidth))
        return false;
      currentFeature = context->covergroupClockEvents.get();
      if (!currentFeature)
        break;
      found = currentFeature->registrations.find(candidate.ownerLogicalToken);
      if (found == currentFeature->registrations.end() || !found->second)
        continue;
      if (value.empty() || unknown.empty() || (unknown[0] & 1) != 0 ||
          (value[0] & 1) == 0)
        continue;
    }
    CovergroupClockEventRegistration &registration = *found->second;
    if (registration.lastSampledSequence == sequence)
      continue;
    registration.lastSampledSequence = sequence;
    if (registration.strobe) {
      // IEEE 1800-2023 19.3 retains only one automatic sample per time slot
      // when type_option.strobe is set. The event iff was evaluated above at
      // the event instant; sample expressions and enabled state are observed
      // later, when the scheduler enters the Postponed region.
      if (registration.lastStrobeTimeValid &&
          registration.lastStrobeTime == context->schedulerTime)
        continue;
      registration.lastStrobeTime = context->schedulerTime;
      registration.lastStrobeTimeValid = true;
      if (currentFeature->pendingStrobeCount == UINT64_MAX) {
        context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
        return false;
      }
      registration.strobePending = true;
      ++currentFeature->pendingStrobeCount;
      continue;
    }
    OBELISK_RT_TRY { sampleOwners.push_back(registration.ownerLogicalToken); }
    OBELISK_RT_CATCH(const std::bad_alloc &) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
      return false;
    }
    OBELISK_RT_CATCH(const std::length_error &) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return false;
    }
  }

  // Complete the event-instant phase for every affected clause before any
  // user sample evaluator runs. This ensures an `or` event commits every
  // primary's retained result even when the first matching clause coalesces
  // the registration's sample. Sampler-triggered publications may now execute
  // recursively because all history for this publication is stable.
  context->covergroupClockEventEvaluationDepth = 0;
  for (uint64_t ownerLogicalToken : sampleOwners) {
    CovergroupClockEventFeatureState *currentFeature =
        context->covergroupClockEvents.get();
    if (!currentFeature)
      break;
    auto found = currentFeature->registrations.find(ownerLogicalToken);
    if (found == currentFeature->registrations.end() || !found->second)
      continue;
    CovergroupClockEventRegistration &registration = *found->second;
    uint32_t enabled = 0;
    obelisk_rt_status status = obelisk_rt_v1_covergroup_sample_enabled(
        context, registration.covergroupHandle, &enabled);
    if (status != OBELISK_RT_OK) {
      context->schedulerStatus = status;
      return false;
    }
    if (!enabled)
      continue;
    const bool native = registration.native;
    const uint64_t sampleOwnerToken = registration.ownerLogicalToken;
    const uint64_t observerCodeUnitID = registration.observerCodeUnitID;
    std::vector<obelisk_rt_computed_capture_v1> captures;
    OBELISK_RT_TRY { captures = registration.captures; }
    OBELISK_RT_CATCH(const std::bad_alloc &) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
      return false;
    }
    OBELISK_RT_CATCH(const std::length_error &) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return false;
    }
    uint64_t sampleValue = 0;
    uint64_t sampleUnknown = 0;
    uint64_t token = native ? sampleOwnerToken & ~kNativeLogicalProcessTag
                            : sampleOwnerToken;
    ClockConditionPublicationOverlay overlay(context, sample);
    bool evaluated =
        native ? obelisk_rt_evaluate_native_clock_condition_unlocked(
                     context, token, observerCodeUnitID, captures.data(),
                     captures.size(), sampleValue, sampleUnknown)
               : obelisk_rt_evaluate_design_clock_condition_unlocked(
                     context, token, observerCodeUnitID, captures.data(),
                     captures.size(), sampleValue, sampleUnknown);
    if (!evaluated)
      return false;
    if (sampleUnknown != 0 || sampleValue != 1) {
      context->schedulerStatus = OBELISK_RT_INVALID_DESIGN;
      return false;
    }
  }
  return true;
}

bool evaluateCovergroupClockEventsUnlocked(
    obelisk_rt_context *context, uint64_t sequence, uint32_t dependencyKind,
    uint32_t kind, uint32_t objectID, int64_t firstPage, int64_t lastPage,
    uint64_t stableID, uint64_t bitWidth,
    const ClockConditionPublicationView *sample) {
  if (!context)
    return false;
  if (context->covergroupClockEventEvaluationDepth != 0) {
    // Compiler-generated primary and iff observers are required to be
    // read-only, so they cannot publish recursively. Reject a malformed or
    // foreign descriptor that violates that contract instead of deferring its
    // iff evaluation past the IEEE 1800 event instant.
    context->schedulerStatus = OBELISK_RT_INVALID_DESIGN;
    return false;
  }

  struct EvaluationGuard {
    obelisk_rt_context *context;
    ~EvaluationGuard() { context->covergroupClockEventEvaluationDepth = 0; }
  } guard{context};
  context->covergroupClockEventEvaluationDepth = 1;
  return evaluateCovergroupClockEventsNowUnlocked(
      context, sequence, dependencyKind, kind, objectID, firstPage, lastPage,
      stableID, bitWidth, sample);
}

bool recordClockOccurrenceUnlocked(
    obelisk_rt_context *context, ClockOccurrenceSubscription &subscription,
    const ClockConditionPublicationView *sample) {
  if (subscription.waiterToken == 0) {
    subscription.clockingOutputSeen = true;
    subscription.lastClockingOutputTime = context->schedulerTime;
    return true;
  }
  bool native = subscription.native;
  uint64_t logicalToken =
      native ? kNativeLogicalProcessTag | subscription.waiterToken
             : subscription.waiterToken;
  if (!context->clockOccurrences) {
    context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
    return false;
  }
  auto found = context->clockOccurrences->waits.find(logicalToken);
  if (found == context->clockOccurrences->waits.end() ||
      subscription.occurrenceBit >= found->second.conditions.size()) {
    context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
    return false;
  }
  ClockOccurrenceWaitState &state = found->second;
  if (!readClockOccurrenceCondition(
          context, state.conditions[subscription.occurrenceBit], sample,
          subscription.waiterToken, native))
    return true;
  ClockOccurrenceWaveKey key{context->schedulerTime,
                             context->schedulerSlotProgress,
                             context->activeExecRegion};
  if (state.hasCurrentKey && !(state.currentKey == key))
    finalizeClockOccurrenceWave(state);
  if (!state.hasCurrentKey) {
    state.currentKey = key;
    state.hasCurrentKey = true;
  }
  uint64_t ordinal = state.occurrenceCounts[subscription.occurrenceBit]++;
  if (ordinal >= std::numeric_limits<size_t>::max()) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
    return false;
  }
  if (state.currentCohorts.size() <= ordinal)
    state.currentCohorts.resize(static_cast<size_t>(ordinal) + 1, 0);
  state.currentCohorts[ordinal] |= uint64_t{1} << subscription.occurrenceBit;

  bool wasTriggered = false;
  if (native) {
    ScheduledProcess *process =
        findScheduledProcess(context, subscription.waiterToken);
    if (!process || !process->instance || !process->started) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return false;
    }
    wasTriggered = process->signalTriggered;
    process->signalTriggered = true;
    if (!wasTriggered)
      context->nativePollCandidates.insert(subscription.waiterToken);
  } else {
    auto indexed =
        context->scheduledDesignTaskIndices.find(subscription.waiterToken);
    if (indexed == context->scheduledDesignTaskIndices.end() ||
        indexed->second >= context->scheduledDesignTasks.size()) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return false;
    }
    ScheduledDesignTask &task = context->scheduledDesignTasks[indexed->second];
    if (task.id != subscription.waiterToken || task.terminated ||
        !task.started) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return false;
    }
    wasTriggered = task.signalTriggered;
    task.signalTriggered = true;
    if (!wasTriggered)
      context->designPollCandidates.insert(subscription.waiterToken);
  }
  if (!wasTriggered && ++context->schedulerSelectionGeneration == 0)
    context->schedulerSelectionGeneration = 1;
  return true;
}

} // namespace

bool obelisk_rt_evaluate_covergroup_managed_clock_events_unlocked(
    obelisk_rt_context *context, uint64_t token) {
  if (!context || !context->covergroupClockEvents)
    return context != nullptr;
  if (token == 0 || context->nextSchedulerSequence == 0) {
    context->schedulerStatus =
        token == 0 ? OBELISK_RT_INVALID_HANDLE : OBELISK_RT_OUT_OF_RESOURCES;
    return false;
  }
  uint64_t sequence = context->nextSchedulerSequence++;
  if (context->signalDiagnosticsEnabled)
    ++context->signalDiagnostics.publications;
  return evaluateCovergroupClockEventsUnlocked(
      context, sequence, OBELISK_RT_OBSERVER_DEPENDENCY_MANAGED, 0, 0, 0, 0,
      token, 1, nullptr);
}

bool obelisk_rt_covergroup_strobes_pending_unlocked(
    const obelisk_rt_context *context) {
  if (!context || !context->covergroupClockEvents)
    return false;
  return context->covergroupClockEvents->pendingStrobeCount != 0;
}

obelisk_rt_status
obelisk_rt_drain_covergroup_strobes_unlocked(obelisk_rt_context *context) {
  if (!context || !context->covergroupClockEvents)
    return context ? OBELISK_RT_OK : OBELISK_RT_INVALID_ARGUMENT;

  std::vector<uint64_t> owners;
  OBELISK_RT_TRY {
    owners.reserve(context->covergroupClockEvents->registrations.size());
    for (const auto &[owner, registration] :
         context->covergroupClockEvents->registrations)
      if (registration && registration->strobePending)
        owners.push_back(owner);
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
    return context->schedulerStatus;
  }
  OBELISK_RT_CATCH(const std::length_error &) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
    return context->schedulerStatus;
  }

  struct PostponedRegionGuard {
    obelisk_rt_context *context;
    uint32_t home;
    uint32_t exec;

    explicit PostponedRegionGuard(obelisk_rt_context *context)
        : context(context), home(context->activeHomeRegion),
          exec(context->activeExecRegion) {
      context->activeHomeRegion = OBELISK_RT_REGION_POSTPONED;
      context->activeExecRegion = OBELISK_RT_REGION_POSTPONED;
    }
    ~PostponedRegionGuard() {
      context->activeHomeRegion = home;
      context->activeExecRegion = exec;
    }
  } postponedRegion(context);

  for (uint64_t owner : owners) {
    // The observer evaluator releases the recursive context mutex around a
    // native callback. Re-resolve every candidate by stable owner token and
    // copy its evaluator state before crossing that boundary, just as the
    // event-instant path does.
    CovergroupClockEventFeatureState *feature =
        context->covergroupClockEvents.get();
    if (!feature)
      break;
    auto found = feature->registrations.find(owner);
    if (found == feature->registrations.end() || !found->second ||
        !found->second->strobePending)
      continue;
    CovergroupClockEventRegistration &registration = *found->second;
    registration.strobePending = false;
    if (feature->pendingStrobeCount == 0) {
      context->schedulerStatus = OBELISK_RT_INVALID_DESIGN;
      return context->schedulerStatus;
    }
    --feature->pendingStrobeCount;

    uint32_t enabled = 0;
    obelisk_rt_status status = obelisk_rt_v1_covergroup_sample_enabled(
        context, registration.covergroupHandle, &enabled);
    if (status != OBELISK_RT_OK) {
      context->schedulerStatus = status;
      return status;
    }
    if (!enabled)
      continue;

    const bool native = registration.native;
    const uint64_t observerCodeUnitID = registration.observerCodeUnitID;
    const uint64_t token = native ? owner & ~kNativeLogicalProcessTag : owner;
    std::vector<obelisk_rt_computed_capture_v1> captures;
    OBELISK_RT_TRY { captures = registration.captures; }
    OBELISK_RT_CATCH(const std::bad_alloc &) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
      return context->schedulerStatus;
    }
    OBELISK_RT_CATCH(const std::length_error &) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return context->schedulerStatus;
    }
    uint64_t value = 0;
    uint64_t unknown = 0;
    bool evaluated =
        native ? obelisk_rt_evaluate_native_clock_condition_unlocked(
                     context, token, observerCodeUnitID, captures.data(),
                     captures.size(), value, unknown)
               : obelisk_rt_evaluate_design_clock_condition_unlocked(
                     context, token, observerCodeUnitID, captures.data(),
                     captures.size(), value, unknown);
    if (!evaluated) {
      if (context->schedulerStatus == OBELISK_RT_OK)
        context->schedulerStatus = OBELISK_RT_INVALID_DESIGN;
      return context->schedulerStatus;
    }
    if (unknown != 0 || value != 1) {
      context->schedulerStatus = OBELISK_RT_INVALID_DESIGN;
      return context->schedulerStatus;
    }
  }
  return OBELISK_RT_OK;
}

bool obelisk_rt_read_clock_condition_publication_bit_unlocked(
    const obelisk_rt_context *context, uint64_t stableID, uint64_t bit,
    bool &value, bool &unknown) {
  if (!context || !context->observerForcesCanonicalPlane ||
      bit > uint64_t{INT64_MAX})
    return false;
  obelisk_rt_stable_handle_v1 loaded;
  // A partially out-of-range view may start below zero and still overlap a
  // valid publication. Match the selected bit, not just the view's base.
  if (!obelisk_rt_stable_handle_decode(stableID, &loaded))
    return false;
  for (const ClockConditionPublicationView *publication =
           context->conditionPublication;
       publication; publication = publication->previous) {
    if (!publication->newValue || publication->bitWidth == 0)
      continue;
    obelisk_rt_stable_handle_v1 published;
    if (!obelisk_rt_stable_handle_decode(publication->stableID, &published) ||
        published.offset < 0 ||
        (published.kind != OBELISK_RT_STABLE_HANDLE_GLOBAL &&
         published.kind != OBELISK_RT_STABLE_HANDLE_STATIC) ||
        published.kind != loaded.kind ||
        (published.kind == OBELISK_RT_STABLE_HANDLE_STATIC &&
         published.id != loaded.id))
      continue;
    __int128 relative = static_cast<__int128>(loaded.offset) + bit -
                        static_cast<__int128>(published.offset);
    if (relative < 0 || relative >= publication->bitWidth ||
        static_cast<uint64_t>(relative) >
            UINT64_MAX - publication->planeBitOffset)
      continue;
    uint64_t source =
        publication->planeBitOffset + static_cast<uint64_t>(relative);
    value = byteBit(publication->newValue, source);
    unknown =
        publication->newUnknown && byteBit(publication->newUnknown, source);
    return true;
  }
  return false;
}

void wakeMonitorProcessUnlocked(obelisk_rt_context *context,
                                uint64_t logicalToken) {
  if (!logicalToken)
    return;
  if ((logicalToken & kNativeLogicalProcessTag) != 0) {
    uint64_t token = logicalToken & ~kNativeLogicalProcessTag;
    if (ScheduledProcess *process = findScheduledProcess(context, token);
        process && process->instance) {
      obelisk_rt_unregister_signal_wait_unlocked(
          context, process->signalSubscriptions, process->token, false);
      process->suspendKind = OBELISK_RT_SUSPEND_NONE;
      process->signalTriggered = false;
      process->urgent = false;
      process->queuedRegion = OBELISK_RT_REGION_POSTPONED;
      context->nativePollCandidates.insert(token);
      if (++context->schedulerSelectionGeneration == 0)
        context->schedulerSelectionGeneration = 1;
    }
    return;
  }
  for (ScheduledDesignTask &task : context->scheduledDesignTasks) {
    if (task.id != logicalToken || task.terminated)
      continue;
    obelisk_rt_unregister_signal_wait_unlocked(
        context, task.signalSubscriptions, task.id, true);
    task.suspendKind = OBELISK_RT_SUSPEND_NONE;
    task.signalTriggered = false;
    task.urgent = false;
    task.queuedRegion = OBELISK_RT_REGION_POSTPONED;
    context->designPollCandidates.insert(logicalToken);
    if (++context->schedulerSelectionGeneration == 0)
      context->schedulerSelectionGeneration = 1;
    return;
  }
}

template <typename Matches, typename ClockMatches>
static bool
publishSignalOccurrenceUnlocked(obelisk_rt_context *context, uint64_t stableID,
                                uint64_t bitWidth, Matches &&matches,
                                ClockMatches &&clockMatches,
                                const ClockConditionPublicationView *sample,
                                uint64_t *outSequence = nullptr) {
  if (context->nextSchedulerSequence == 0) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
    return false;
  }
  uint64_t sequence = context->nextSchedulerSequence++;
  if (outSequence)
    *outSequence = sequence;
  if (context->signalDiagnosticsEnabled)
    ++context->signalDiagnostics.publications;
  // A direct native wait is polled by the generated schedule only while that
  // schedule owns execution.  During a transient fine-scheduler handoff the
  // same subscription must seed the runtime candidate set instead; otherwise
  // a force/assign that survives the current slot can strand every subsequent
  // edge-triggered actor while the fine scheduler advances time.
  bool fullyStaticAOT = !context->nativeScheduleStopAtCleanBoundary &&
                        context->nativeSchedulePlan &&
                        !context->nativeScheduleDeoptimized &&
                        (context->nativeSchedulePlan->flags &
                         OBELISK_RT_NATIVE_SCHEDULE_FULLY_STATIC) != 0;

  uint32_t kind = 0;
  uint32_t objectID = 0;
  int64_t firstPage = 0;
  int64_t lastPage = 0;
  if (!signalSubscriptionBucketRange(stableID, bitWidth, kind, objectID,
                                     firstPage, lastPage)) {
    context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
    return false;
  }
  auto visitBucket = [&](int64_t page) {
    SignalSubscriptionBucketKey key{kind, objectID, page};
    auto bucket = context->signalSubscriptionBuckets.find(key);
    if (bucket != context->signalSubscriptionBuckets.end()) {
      for (const SignalSubscriptionBucketEntry &entry : bucket->second) {
        SignalSubscription *subscription = entry.subscription;
        if (!subscription || subscription->lastExaminedSequence == sequence)
          continue;
        subscription->lastExaminedSequence = sequence;
        if (context->signalDiagnosticsEnabled)
          ++context->signalDiagnostics.subscribersExamined;
        bool overlaps = false;
        if (fullyStaticAOT &&
            subscription->target == SignalSubscription::NativeDirectWait) {
          int64_t publishedOffset = static_cast<int32_t>(stableID);
          int64_t subscribedOffset =
              static_cast<int32_t>(subscription->stableID);
          overlaps =
              static_cast<__int128>(publishedOffset) + bitWidth >
                  subscribedOffset &&
              static_cast<__int128>(subscribedOffset) + subscription->bitWidth >
                  publishedOffset;
        } else {
          overlaps = rangesOverlap(subscription->stableID,
                                   subscription->bitWidth, stableID, bitWidth);
        }
        if (!overlaps)
          continue;
        if (subscription->suppressActiveSelf &&
            subscription->waiterToken != 0) {
          uint64_t logicalToken =
              subscription->target == SignalSubscription::NativeDirectWait
                  ? kNativeLogicalProcessTag | subscription->waiterToken
                  : subscription->waiterToken;
          if (context->activeLogicalProcessToken == logicalToken)
            continue;
        }
        if (!matches(*subscription))
          continue;
        if (subscription->target == SignalSubscription::NativeDirectWait ||
            subscription->target == SignalSubscription::DesignDirectWait) {
          if (!subscription->latch || subscription->latch->triggered)
            continue;
          subscription->latch->triggered = true;
          bool staticallyPolledNative =
              fullyStaticAOT &&
              subscription->target == SignalSubscription::NativeDirectWait;
          if (staticallyPolledNative) {
            ScheduledProcess *scheduled =
                findScheduledProcess(context, subscription->waiterToken);
            if (!scheduled || scheduled->aotActorSlot == UINT32_MAX ||
                !markNativeAOTActorReadyUnlocked(context,
                                                 scheduled->aotActorSlot)) {
              context->schedulerStatus = OBELISK_RT_INVALID_CONTINUATION;
              return false;
            }
          }
          if (!staticallyPolledNative && subscription->waiterToken != 0) {
            auto &candidates =
                subscription->target == SignalSubscription::NativeDirectWait
                    ? context->nativePollCandidates
                    : context->designPollCandidates;
            OBELISK_RT_TRY { candidates.insert(subscription->waiterToken); }
            OBELISK_RT_CATCH(const std::bad_alloc &) {
              context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
              return false;
            }
          }
          if (!staticallyPolledNative &&
              ++context->schedulerSelectionGeneration == 0)
            context->schedulerSelectionGeneration = 1;
          continue;
        }
        if (!subscription->latch || subscription->latch->affected)
          continue;
        OBELISK_RT_TRY {
          auto &pending =
              subscription->target == SignalSubscription::NativeComputedWait
                  ? context->pendingNativeComputedWaiters
                  : context->pendingDesignComputedWaiters;
          pending.push_back(subscription->waiterToken);
          subscription->latch->affected = true;
        }
        OBELISK_RT_CATCH(const std::bad_alloc &) {
          context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
          return false;
        }
      }
    }
    return true;
  };
  __int128 pageCount = static_cast<__int128>(lastPage) - firstPage + 1;
  if (pageCount <= kMaximumIndexedSignalPages) {
    if (!visitBucket(kWideSignalSubscriptionPage))
      return false;
    for (int64_t page = firstPage;; ++page) {
      if (!visitBucket(page))
        return false;
      if (page == lastPage)
        break;
    }
  } else {
    std::vector<int64_t> pages;
    OBELISK_RT_TRY {
      for (const auto &[key, bucket] : context->signalSubscriptionBuckets) {
        (void)bucket;
        if (key.kind == kind && key.id == objectID)
          pages.push_back(key.page);
      }
    }
    OBELISK_RT_CATCH(const std::bad_alloc &) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
      return false;
    }
    for (int64_t page : pages)
      if (!visitBucket(page))
        return false;
  }

  // Exact multi-clock coordination owns a separate lazy subscription index.
  // Ordinary publication performs only this cold null-pointer gate and keeps
  // its long-standing subscription representation and matching path intact.
  if (!evaluateCovergroupClockEventsUnlocked(
          context, sequence, OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL, kind,
          objectID, firstPage, lastPage, stableID, bitWidth, sample))
    return false;

  ClockOccurrenceFeatureState *feature = context->clockOccurrences.get();
  if (!feature)
    return true;
  auto visitClockBucket = [&](int64_t page) {
    SignalSubscriptionBucketKey key{kind, objectID, page};
    auto bucket = feature->subscriptionBuckets.find(key);
    if (bucket == feature->subscriptionBuckets.end())
      return true;
    for (const ClockOccurrenceBucketEntry &entry : bucket->second) {
      ClockOccurrenceSubscription *subscription = entry.subscription;
      if (!subscription || subscription->lastExaminedSequence == sequence)
        continue;
      subscription->lastExaminedSequence = sequence;
      if (context->signalDiagnosticsEnabled)
        ++context->signalDiagnostics.subscribersExamined;
      if (!rangesOverlap(subscription->stableID, subscription->bitWidth,
                         stableID, bitWidth) ||
          !clockMatches(*subscription))
        continue;
      if (!recordClockOccurrenceUnlocked(context, *subscription, sample))
        return false;
    }
    return true;
  };
  if (pageCount <= kMaximumIndexedSignalPages) {
    if (!visitClockBucket(kWideSignalSubscriptionPage))
      return false;
    for (int64_t page = firstPage;; ++page) {
      if (!visitClockBucket(page))
        return false;
      if (page == lastPage)
        break;
    }
  } else {
    std::vector<int64_t> pages;
    OBELISK_RT_TRY {
      for (const auto &[key, bucket] : feature->subscriptionBuckets) {
        (void)bucket;
        if (key.kind == kind && key.id == objectID)
          pages.push_back(key.page);
      }
    }
    OBELISK_RT_CATCH(const std::bad_alloc &) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
      return false;
    }
    for (int64_t page : pages)
      if (!visitClockBucket(page))
        return false;
  }
  return true;
}

bool isCustomClockEdge(uint32_t edge) {
  return (edge & ~OBELISK_RT_WAIT_EDGE_TRANSITION_CLASSES) ==
             OBELISK_RT_WAIT_EDGE_TRANSITION_MASK &&
         (edge & OBELISK_RT_WAIT_EDGE_TRANSITION_CLASSES) != 0;
}

uint32_t clockTransitionClass(bool oldValue, bool oldUnknown, bool newValue,
                              bool newUnknown) {
  if (oldUnknown)
    return newUnknown ? 0 : newValue ? uint32_t{1} << 5 : uint32_t{1} << 4;
  if (!oldValue)
    return newUnknown ? uint32_t{1} << 1 : newValue ? uint32_t{1} << 0 : 0;
  return newUnknown ? uint32_t{1} << 3 : !newValue ? uint32_t{1} << 2 : 0;
}

bool customClockTransitionMatches(
    obelisk_rt_context *context, ClockOccurrenceSubscription &subscription,
    uint64_t stableID, uint64_t bitWidth, const uint8_t *changed,
    uint64_t edgeBitOffset, const uint8_t *publishedOldValue,
    const uint8_t *publishedOldUnknown, const uint8_t *publishedNewValue,
    const uint8_t *publishedNewUnknown) {
  obelisk_rt_stable_handle_v1 published;
  obelisk_rt_stable_handle_v1 subscribed;
  if (!obelisk_rt_stable_handle_decode(stableID, &published) ||
      !obelisk_rt_stable_handle_decode(subscription.stableID, &subscribed)) {
    context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
    return false;
  }
  __int128 overlapBegin =
      std::max<__int128>(published.offset, subscribed.offset);
  __int128 overlapEnd = std::min(
      static_cast<__int128>(published.offset) + bitWidth,
      static_cast<__int128>(subscribed.offset) + subscription.bitWidth);
  bool matched = false;
  for (__int128 coordinate = overlapBegin; coordinate < overlapEnd;
       ++coordinate) {
    uint64_t publishedBit =
        static_cast<uint64_t>(coordinate - published.offset);
    if (changed && !byteBit(changed, edgeBitOffset + publishedBit))
      continue;
    uint64_t subscribedBit =
        static_cast<uint64_t>(coordinate - subscribed.offset);
    bool initialized =
        byteBit(subscription.previousInitialized.data(), subscribedBit);
    bool oldValue = false;
    bool oldUnknown = false;
    if (initialized) {
      oldValue = byteBit(subscription.previousValue.data(), subscribedBit);
      oldUnknown = byteBit(subscription.previousUnknown.data(), subscribedBit);
    } else if (publishedOldValue) {
      oldValue = byteBit(publishedOldValue, publishedBit);
      oldUnknown =
          publishedOldUnknown && byteBit(publishedOldUnknown, publishedBit);
    }
    bool newValue = false;
    bool newUnknown = false;
    if (publishedNewValue) {
      newValue = byteBit(publishedNewValue, publishedBit);
      newUnknown =
          publishedNewUnknown && byteBit(publishedNewUnknown, publishedBit);
    } else if (!obelisk_rt_read_signal_bit_unlocked(
                   context, subscription.stableID, subscribedBit, newValue,
                   newUnknown, /*useSnapshot=*/false)) {
      context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
      return false;
    }
    setByteBit(subscription.previousValue.data(), subscribedBit, newValue);
    setByteBit(subscription.previousUnknown.data(), subscribedBit, newUnknown);
    setByteBit(subscription.previousInitialized.data(), subscribedBit, true);
    // If no prior plane was observable, importing this first published value
    // establishes the snapshot but cannot manufacture an IEEE 31.5 edge.
    if (!initialized && !publishedOldValue)
      continue;
    uint32_t transition =
        clockTransitionClass(oldValue, oldUnknown, newValue, newUnknown);
    matched |= (transition & (subscription.edge &
                              OBELISK_RT_WAIT_EDGE_TRANSITION_CLASSES)) != 0;
  }
  // IEEE 1800-2017 31.8 makes all matching bits in one packed publication one
  // vector occurrence. The existing occurrence-bucket visit already coalesces
  // multi-page overlap and repeated publications remain separate cohorts.
  return matched;
}

template <typename Subscription>
static bool signalTransitionBatchMatches(const Subscription &subscription,
                                         uint64_t stableID, uint64_t bitWidth,
                                         const uint8_t *changed,
                                         const uint8_t *posedge,
                                         const uint8_t *negedge,
                                         uint64_t edgeBitOffset, bool direct) {
  int64_t publishedOffset = 0;
  int64_t subscribedOffset = 0;
  if ((stableID & OBELISK_RT_STABLE_HANDLE_TAG_MASK) ==
          OBELISK_RT_STABLE_HANDLE_STATIC_TAG &&
      (subscription.stableID & OBELISK_RT_STABLE_HANDLE_TAG_MASK) ==
          OBELISK_RT_STABLE_HANDLE_STATIC_TAG) {
    if ((stableID >> 32) != (subscription.stableID >> 32))
      return false;
    publishedOffset = static_cast<int32_t>(stableID);
    subscribedOffset = static_cast<int32_t>(subscription.stableID);
  } else {
    obelisk_rt_stable_handle_v1 published;
    obelisk_rt_stable_handle_v1 subscribed;
    if (!obelisk_rt_stable_handle_decode(stableID, &published) ||
        !obelisk_rt_stable_handle_decode(subscription.stableID, &subscribed) ||
        published.kind != subscribed.kind ||
        (published.kind != OBELISK_RT_STABLE_HANDLE_GLOBAL &&
         published.id != subscribed.id))
      return false;
    publishedOffset = published.offset;
    subscribedOffset = subscribed.offset;
  }
  __int128 overlapBegin = std::max<__int128>(publishedOffset, subscribedOffset);
  __int128 overlapEnd =
      std::min(static_cast<__int128>(publishedOffset) + bitWidth,
               static_cast<__int128>(subscribedOffset) + subscription.bitWidth);
  if (overlapBegin >= overlapEnd)
    return false;
  uint64_t index =
      edgeBitOffset + static_cast<uint64_t>(overlapBegin - publishedOffset);
  uint64_t width = static_cast<uint64_t>(overlapEnd - overlapBegin);
  if (!direct)
    return anyPackedBits(changed, index, width) ||
           anyPackedBits(posedge, index, width) ||
           anyPackedBits(negedge, index, width);
  switch (subscription.edge) {
  case OBELISK_RT_WAIT_EDGE_CHANGE:
    return anyPackedBits(changed, index, width);
  case OBELISK_RT_WAIT_EDGE_POSEDGE:
    return anyPackedBits(posedge, index, width);
  case OBELISK_RT_WAIT_EDGE_NEGEDGE:
    return anyPackedBits(negedge, index, width);
  case OBELISK_RT_WAIT_EDGE_BOTH:
    return anyPackedBits(posedge, index, width) ||
           anyPackedBits(negedge, index, width);
  default:
    return false;
  }
}

static bool publishCovergroupClockTransitionUnlocked(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    const uint8_t *changed, const uint8_t *posedge, const uint8_t *negedge,
    const uint8_t *newValue, const uint8_t *newUnknown) {
  (void)changed;
  (void)posedge;
  (void)negedge;
  if (!context->covergroupClockEvents)
    return true;
  if (context->nextSchedulerSequence == 0) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
    return false;
  }
  uint32_t kind = 0;
  uint32_t objectID = 0;
  int64_t firstPage = 0;
  int64_t lastPage = 0;
  if (!signalSubscriptionBucketRange(stableID, bitWidth, kind, objectID,
                                     firstPage, lastPage)) {
    context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
    return false;
  }
  uint64_t sequence = context->nextSchedulerSequence++;
  if (context->signalDiagnosticsEnabled)
    ++context->signalDiagnostics.publications;
  ClockConditionPublicationView sample{stableID, bitWidth, 0, newValue,
                                       newUnknown};
  return evaluateCovergroupClockEventsUnlocked(
      context, sequence, OBELISK_RT_OBSERVER_DEPENDENCY_SIGNAL, kind, objectID,
      firstPage, lastPage, stableID, bitWidth, &sample);
}

bool recordStaticClockingOutputOccurrencesUnlocked(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    const uint8_t *changed, const uint8_t *posedge, const uint8_t *negedge,
    uint64_t edgeBitOffset, uint64_t &sequence) {
  ClockOccurrenceFeatureState *feature = context->clockOccurrences.get();
  if (!feature || feature->clockingOutputs.empty())
    return true;
  uint32_t kind = 0;
  uint32_t objectID = 0;
  int64_t firstPage = 0;
  int64_t lastPage = 0;
  if (!signalSubscriptionBucketRange(stableID, bitWidth, kind, objectID,
                                     firstPage, lastPage)) {
    context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
    return false;
  }
  auto visitBucket = [&](int64_t page) {
    SignalSubscriptionBucketKey key{kind, objectID, page};
    auto bucket = feature->subscriptionBuckets.find(key);
    if (bucket == feature->subscriptionBuckets.end())
      return true;
    for (const ClockOccurrenceBucketEntry &entry : bucket->second) {
      ClockOccurrenceSubscription *subscription = entry.subscription;
      if (!subscription || subscription->waiterToken != 0 ||
          !rangesOverlap(subscription->stableID, subscription->bitWidth,
                         stableID, bitWidth) ||
          !signalTransitionBatchMatches(*subscription, stableID, bitWidth,
                                        changed, posedge, negedge,
                                        edgeBitOffset, true))
        continue;
      if (sequence == 0) {
        if (context->nextSchedulerSequence == 0) {
          context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
          return false;
        }
        sequence = context->nextSchedulerSequence++;
      }
      if (subscription->lastExaminedSequence == sequence)
        continue;
      subscription->lastExaminedSequence = sequence;
      subscription->clockingOutputSeen = true;
      subscription->lastClockingOutputTime = context->schedulerTime;
    }
    return true;
  };
  __int128 pageCount = static_cast<__int128>(lastPage) - firstPage + 1;
  if (pageCount <= kMaximumIndexedSignalPages) {
    if (!visitBucket(kWideSignalSubscriptionPage))
      return false;
    for (int64_t page = firstPage;; ++page) {
      if (!visitBucket(page))
        return false;
      if (page == lastPage)
        break;
    }
    return true;
  }
  for (const auto &[key, bucket] : feature->subscriptionBuckets) {
    (void)bucket;
    if (key.kind == kind && key.id == objectID && !visitBucket(key.page))
      return false;
  }
  return true;
}

static bool staticAOTFanoutRangeHasConsumer(const obelisk_rt_context *context,
                                            uint32_t staticID,
                                            uint64_t staticOffset,
                                            uint64_t bitWidth) {
  if (context->nativeScheduleFanoutEntryCount == 0)
    return false;
  const obelisk_rt_static_fanout_entry *begin =
      context->nativeScheduleFanoutEntries;
  const obelisk_rt_static_fanout_entry *end =
      begin + context->nativeScheduleFanoutEntryCount;
  auto first = end;
  auto last = end;
  if (staticID < context->nativeScheduleFanoutRanges.size()) {
    auto [firstIndex, lastIndex] =
        context->nativeScheduleFanoutRanges[staticID];
    if (firstIndex <= lastIndex &&
        lastIndex <= context->nativeScheduleFanoutEntryCount) {
      first = begin + firstIndex;
      last = begin + lastIndex;
    }
  } else {
    first =
        std::lower_bound(begin, end, staticID,
                         [](const obelisk_rt_static_fanout_entry &entry,
                            uint32_t id) { return entry.static_state < id; });
    last = first;
    while (last != end && last->static_state == staticID)
      ++last;
  }
  return std::any_of(
      first, last, [&](const obelisk_rt_static_fanout_entry &entry) {
        return static_cast<__int128>(staticOffset) <
                   static_cast<__int128>(entry.low_bit) + entry.bit_width &&
               static_cast<__int128>(entry.low_bit) <
                   static_cast<__int128>(staticOffset) + bitWidth;
      });
}

template <bool UseClockIngress>
static bool publishStaticAOTSignalTransitionUnlockedImpl(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    const uint8_t *changed, const uint8_t *posedge, const uint8_t *negedge,
    uint64_t *outSequence, bool indexedExternalDeposit) {
  if (outSequence)
    *outSequence = 0;
  if (!(indexedExternalDeposit ? canUseIndexedExternalAOTFanout(context)
                               : canUseStaticAOTFanout(context)))
    return false;
  uint32_t staticID = 0;
  int64_t staticOffset = 0;
  if (!decodeNativeStatic(stableID, staticID, staticOffset) || staticOffset < 0)
    return false;
  uint64_t publishedLow = static_cast<uint64_t>(staticOffset);
  // Exact static fanout proves that no observer, conditional waiter, design
  // task, or unlisted direct wait can consume this transition.  Keep the
  // canonical state update but do not manufacture a scheduler publication.
  if (!staticAOTFanoutRangeHasConsumer(context, staticID, publishedLow,
                                       bitWidth))
    return true;
  // Metadata-only clock plans predate executable generated fragments.  Their
  // coordinator is nevertheless the owner of external-deposit ingress, so
  // retain that routing while using the ownership marker for hybrid plans.
  // This scan is confined to the asynchronous publication path and therefore
  // does not add work to the generated periodic loop.
  bool metadataOnlyClockCoordinator = false;
  if constexpr (UseClockIngress) {
    const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
    metadataOnlyClockCoordinator =
        plan->merged_fragment_count != 0 &&
        std::none_of(plan->merged_fragments,
                     plan->merged_fragments + plan->merged_fragment_count,
                     [](const obelisk_rt_native_merged_fragment &fragment) {
                       return fragment.execute != nullptr;
                     });
  }
  bool published = false;
  const obelisk_rt_static_fanout_entry *begin =
      context->nativeScheduleFanoutEntries;
  const obelisk_rt_static_fanout_entry *end =
      begin + context->nativeScheduleFanoutEntryCount;
  auto first = end;
  auto last = end;
  if (staticID < context->nativeScheduleFanoutRanges.size()) {
    auto [firstIndex, lastIndex] =
        context->nativeScheduleFanoutRanges[staticID];
    if (firstIndex <= lastIndex &&
        lastIndex <= context->nativeScheduleFanoutEntryCount) {
      first = begin + firstIndex;
      last = begin + lastIndex;
    }
  } else {
    first =
        std::lower_bound(begin, end, staticID,
                         [](const obelisk_rt_static_fanout_entry &entry,
                            uint32_t id) { return entry.static_state < id; });
    last = first;
    while (last != end && last->static_state == staticID)
      ++last;
  }
  for (auto entry = first; entry != last; ++entry) {
    ++context->signalDiagnostics.aotFanoutEntries;
    uint32_t slot = entry->actor_slot;
    if (slot >= context->nativeScheduleActors.size()) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return true;
    }
    obelisk_rt_process_instance_v1 *actor = context->nativeScheduleActors[slot];
    if (!actor)
      continue;
    size_t index = context->nativeScheduleActorIndices[slot];
    if (index >= context->scheduledProcesses.size()) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return true;
    }
    ScheduledProcess &scheduled = context->scheduledProcesses[index];
    bool activeSelf = context->activeLogicalProcessToken ==
                      (kNativeLogicalProcessTag | scheduled.token);
    if (activeSelf &&
        (entry->reserved & OBELISK_RT_FANOUT_SUPPRESS_ACTIVE_SELF) != 0)
      continue;
    if ((actor->continuation != entry->continuation && !activeSelf) ||
        scheduled.instance != actor || !scheduled.started ||
        (scheduled.signalTriggered && !activeSelf) ||
        (scheduled.suspendKind != OBELISK_RT_SUSPEND_CHANGE &&
         scheduled.suspendKind != OBELISK_RT_SUSPEND_EDGE))
      continue;
    uint64_t overlapLow = std::max(publishedLow, entry->low_bit);
    uint64_t overlapHigh =
        std::min(publishedLow + bitWidth, entry->low_bit + entry->bit_width);
    if (overlapLow >= overlapHigh)
      continue;
    uint64_t offset = overlapLow - publishedLow;
    uint64_t width = overlapHigh - overlapLow;
    bool matched = entry->edge == OBELISK_RT_WAIT_EDGE_CHANGE
                       ? anyPackedBits(changed, offset, width)
                   : entry->edge == OBELISK_RT_WAIT_EDGE_POSEDGE
                       ? anyPackedBits(posedge, offset, width)
                   : entry->edge == OBELISK_RT_WAIT_EDGE_NEGEDGE
                       ? anyPackedBits(negedge, offset, width)
                       : anyPackedBits(posedge, offset, width) ||
                             anyPackedBits(negedge, offset, width);
    if (!matched)
      continue;
    if (!published) {
      if (context->nextSchedulerSequence == 0) {
        context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
        return true;
      }
      uint64_t sequence = context->nextSchedulerSequence++;
      if (outSequence)
        *outSequence = sequence;
      if (context->signalDiagnosticsEnabled)
        ++context->signalDiagnostics.publications;
      published = true;
    }
    if constexpr (UseClockIngress) {
      if ((entry->reserved & OBELISK_RT_FANOUT_ROUTE_MASK) ==
              OBELISK_RT_FANOUT_DIRECT ||
          metadataOnlyClockCoordinator) {
        if (entry->kernel >= context->nativeSchedulePlan->clock_kernel_count ||
            entry->merged_bit / 64 >=
                context->nativeSchedulePlan->clock_kernels[entry->kernel]
                    .ingress_word_count) {
          context->schedulerStatus = OBELISK_RT_INVALID_CONTINUATION;
          return true;
        }
        obelisk_rt_native_clock_kernel kernel =
            context->nativeSchedulePlan->clock_kernels[entry->kernel];
        kernel.ingress_mask[entry->merged_bit / 64] |=
            uint64_t{1} << (entry->merged_bit % 64);
        context->nativeScheduleClockIngressPending = true;
        continue;
      }
    }
    scheduled.signalTriggered = true;
    uint32_t node = entry->compute_node;
    if (node >= context->nativeScheduleNodes.size() ||
        node / 64 >= context->nativeScheduleReadyNodes.size() ||
        context->nativeScheduleNodes[node].actor_slot != slot ||
        context->nativeScheduleNodes[node].continuation !=
            entry->continuation) {
      context->schedulerStatus = OBELISK_RT_INVALID_CONTINUATION;
      return true;
    }
    context->nativeScheduleReadyNodes[node / 64] |= uint64_t{1} << (node % 64);
    context->nativeScheduleMinimumActivatedNode =
        std::min(context->nativeScheduleMinimumActivatedNode, node);
  }
  return true;
}

bool publishStaticAOTSignalTransitionUnlocked(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    const uint8_t *changed, const uint8_t *posedge, const uint8_t *negedge,
    uint64_t *outSequence, bool indexedExternalDeposit) {
  bool useClockIngress = context && context->nativeSchedulePlan &&
                         context->nativeSchedulePlan->clock_kernel_count != 0;
  return useClockIngress ? publishStaticAOTSignalTransitionUnlockedImpl<true>(
                               context, stableID, bitWidth, changed, posedge,
                               negedge, outSequence, indexedExternalDeposit)
                         : publishStaticAOTSignalTransitionUnlockedImpl<false>(
                               context, stableID, bitWidth, changed, posedge,
                               negedge, outSequence, indexedExternalDeposit);
}

static bool publishSignalTransitionBatchImpl(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    const uint8_t *changed, const uint8_t *posedge, const uint8_t *negedge,
    uint64_t edgeBitOffset, uint64_t *outSequence, const uint8_t *oldValue,
    const uint8_t *oldUnknown, const uint8_t *newValue,
    const uint8_t *newUnknown) {
  if (!context || bitWidth == 0 || !changed || !posedge || !negedge)
    return context != nullptr;
  bool anyChanged = false;
  for (uint64_t bit = 0; bit != bitWidth; ++bit)
    anyChanged |= byteBit(changed, edgeBitOffset + bit);
  if (!anyChanged)
    return true;
  uint64_t sequence = 0;
  ClockConditionPublicationView sample{stableID, bitWidth, edgeBitOffset,
                                       newValue, newUnknown};
  return publishSignalOccurrenceUnlocked(
      context, stableID, bitWidth,
      [&](const SignalSubscription &subscription) {
        bool direct =
            subscription.target == SignalSubscription::NativeDirectWait ||
            subscription.target == SignalSubscription::DesignDirectWait;
        return signalTransitionBatchMatches(subscription, stableID, bitWidth,
                                            changed, posedge, negedge,
                                            edgeBitOffset, direct);
      },
      [&](ClockOccurrenceSubscription &subscription) {
        if (isCustomClockEdge(subscription.edge))
          return customClockTransitionMatches(
              context, subscription, stableID, bitWidth, changed, edgeBitOffset,
              oldValue, oldUnknown, newValue, newUnknown);
        return signalTransitionBatchMatches(subscription, stableID, bitWidth,
                                            changed, posedge, negedge,
                                            edgeBitOffset, true);
      },
      &sample, outSequence ? outSequence : &sequence);
}

bool obelisk_rt_publish_signal_transition_batch_unlocked(
    obelisk_rt_context *context, uint64_t stableID, uint64_t bitWidth,
    const uint8_t *changed, const uint8_t *posedge, const uint8_t *negedge,
    uint64_t edgeBitOffset, uint64_t *outSequence, const uint8_t *oldValue,
    const uint8_t *oldUnknown, const uint8_t *newValue,
    const uint8_t *newUnknown) {
  obelisk_rt_coverage_record_transition_unlocked(context, stableID, bitWidth,
                                                 changed, newValue, newUnknown);
  ClockOccurrenceFeatureState *feature = context->clockOccurrences.get();
  if ((!feature || feature->waits.empty()) && !context->covergroupClockEvents &&
      edgeBitOffset == 0) {
    uint64_t sequence = 0;
    if (publishStaticAOTSignalTransitionUnlocked(context, stableID, bitWidth,
                                                 changed, posedge, negedge,
                                                 &sequence, false)) {
      if (!recordStaticClockingOutputOccurrencesUnlocked(
              context, stableID, bitWidth, changed, posedge, negedge,
              edgeBitOffset, sequence))
        return false;
      if (outSequence)
        *outSequence = sequence;
      return context->schedulerStatus == OBELISK_RT_OK;
    }
  }
  return publishSignalTransitionBatchImpl(
      context, stableID, bitWidth, changed, posedge, negedge, edgeBitOffset,
      outSequence, oldValue, oldUnknown, newValue, newUnknown);
}

bool obelisk_rt_publish_signal_occurrence_unlocked(obelisk_rt_context *context,
                                                   uint64_t stableID,
                                                   uint64_t bitWidth,
                                                   uint32_t edges,
                                                   uint64_t *outSequence) {
  if (!context || bitWidth == 0 || edges == 0)
    return context != nullptr;
  uint64_t sequence = 0;
  ClockConditionPublicationView sample{stableID, bitWidth, 0, nullptr, nullptr};
  return publishSignalOccurrenceUnlocked(
      context, stableID, bitWidth,
      [&](const SignalSubscription &subscription) {
        bool direct =
            subscription.target == SignalSubscription::NativeDirectWait ||
            subscription.target == SignalSubscription::DesignDirectWait;
        return !direct || signalEdgeMatches(subscription.edge, edges);
      },
      [&](ClockOccurrenceSubscription &subscription) {
        if (isCustomClockEdge(subscription.edge))
          return customClockTransitionMatches(context, subscription, stableID,
                                              bitWidth, nullptr, 0, nullptr,
                                              nullptr, nullptr, nullptr);
        return signalEdgeMatches(subscription.edge, edges);
      },
      &sample, outSequence ? outSequence : &sequence);
}

extern "C" void obelisk_rt_v1_scheduler_signal(obelisk_rt_context *context,
                                               uint64_t bitOffset,
                                               uint64_t bitWidth,
                                               uint32_t edges) {
  if (!context || bitOffset == UINT64_MAX || bitWidth == 0 || edges == 0 ||
      (edges & ~(OBELISK_RT_SIGNAL_CHANGE | OBELISK_RT_SIGNAL_POSEDGE |
                 OBELISK_RT_SIGNAL_NEGEDGE)) != 0)
    return;
  ContextTransaction transaction(context);
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    if (context->schedulerStatus != OBELISK_RT_OK)
      return;
    if (context->activeExecRegion == OBELISK_RT_REGION_POSTPONED) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return;
    }
    uint32_t objectID = 0;
    int64_t offset = 0;
    uint64_t objectWidth = 0;
    if (decodeNativeAutomatic(bitOffset, objectID, offset)) {
      auto found = context->nativeAutomaticStates.find(objectID);
      if (found == context->nativeAutomaticStates.end())
        return;
      objectWidth = found->second.bitWidth;
    } else if (decodeNativeStatic(bitOffset, objectID, offset)) {
      auto found = context->nativeStaticStates.find(objectID);
      if (found == context->nativeStaticStates.end())
        return;
      objectWidth = found->second.bitWidth;
    } else if (!decodeNativeGlobal(bitOffset, offset)) {
      return;
    }
    if (objectWidth != 0 && (offset >= static_cast<__int128>(objectWidth) ||
                             static_cast<__int128>(offset) + bitWidth <= 0))
      return;
    if (!obelisk_rt_publish_signal_occurrence_unlocked(context, bitOffset,
                                                       bitWidth, edges))
      return;
    if (!obelisk_rt_latch_conditional_signal_range_unlocked(context, bitOffset,
                                                            bitWidth, edges))
      return;
    if (++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    ContextMutexLock lock(context);
    context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
  }
  OBELISK_RT_CATCH_ALL {
    ContextMutexLock lock(context);
    context->schedulerStatus = OBELISK_RT_INVALID_ARGUMENT;
  }
}

bool publishNativeSignalTransitionUnlocked(
    obelisk_rt_context *context, uint64_t bitOffset, uint64_t bitWidth,
    const uint8_t *changed, const uint8_t *posedge, const uint8_t *negedge,
    const uint8_t *oldValue, const uint8_t *oldUnknown, const uint8_t *newValue,
    const uint8_t *newUnknown, bool establishesOverride) {
  // Generated native code may have committed later source stores to its
  // private plane before publishing this transition. Advance the canonical
  // plane one publication at a time so observer evaluators see source-order
  // state: prior publications plus this transition, never future stores.
  uint32_t publishedStaticID = 0;
  int64_t publishedOffset = 0;
  bool publishedStatic =
      decodeNativeStatic(bitOffset, publishedStaticID, publishedOffset);
  bool publishedGlobal =
      !publishedStatic && decodeNativeGlobal(bitOffset, publishedOffset);
  const NativeStaticState *publishedState =
      publishedStatic ? findNativeStaticState(context, publishedStaticID)
                      : nullptr;
  auto canonicalBit = [&](uint64_t bit, uint64_t &absolute) {
    int64_t local = 0;
    if (bit > static_cast<uint64_t>(INT64_MAX) ||
        !addHandleOffset(publishedOffset, bit, local) || local < 0)
      return false;
    absolute = static_cast<uint64_t>(local);
    if (publishedStatic) {
      if (!publishedState || absolute >= publishedState->bitWidth)
        return false;
      absolute += publishedState->bitOffset;
    } else if (!publishedGlobal) {
      return false;
    }
    return absolute < context->stateValue.size() * uint64_t{64} &&
           absolute < context->stateUnknown.size() * uint64_t{64};
  };

  // IEEE 1800-2017 10.6.2: a force overrides every driver of its target until
  // the target is released, so a driver that changes behind an active override
  // is not a value change. Generated code computes this transition from its own
  // plane, which does not model the override, so drop the overridden bits here.
  // They must not wake a waiter, and above all they must not reach the
  // canonical plane: release resolves the target from its drivers and compares
  // the result against that plane to decide whether the net changed.
  // The publication that installs an override is exempt: it carries the very
  // value the mask was just set to describe, and IEEE 1800-2017 9.4.2 makes
  // that a value change like any other, so it has to reach waiters.
  std::vector<uint8_t> overriddenChanged;
  std::vector<uint8_t> overriddenPosedge;
  std::vector<uint8_t> overriddenNegedge;
  if (!establishesOverride &&
      (!context->forceMask.empty() || !context->assignMask.empty())) {
    size_t byteCount = static_cast<size_t>((bitWidth + 7) / 8);
    for (uint64_t bit = 0; bit != bitWidth; ++bit) {
      uint64_t absolute = 0;
      if (!byteBit(changed, bit) || !canonicalBit(bit, absolute))
        continue;
      uint64_t limb = absolute / 64;
      uint64_t mask = uint64_t{1} << (absolute % 64);
      if ((limb >= context->forceMask.size() ||
           (context->forceMask[limb] & mask) == 0) &&
          (limb >= context->assignMask.size() ||
           (context->assignMask[limb] & mask) == 0))
        continue;
      if (overriddenChanged.empty()) {
        overriddenChanged.assign(changed, changed + byteCount);
        if (posedge)
          overriddenPosedge.assign(posedge, posedge + byteCount);
        if (negedge)
          overriddenNegedge.assign(negedge, negedge + byteCount);
      }
      setByteBit(overriddenChanged.data(), bit, false);
      if (posedge)
        setByteBit(overriddenPosedge.data(), bit, false);
      if (negedge)
        setByteBit(overriddenNegedge.data(), bit, false);
    }
    if (!overriddenChanged.empty()) {
      changed = overriddenChanged.data();
      if (posedge)
        posedge = overriddenPosedge.data();
      if (negedge)
        negedge = overriddenNegedge.data();
    }
  }

  uint64_t sequence = 0;
  if (!obelisk_rt_publish_signal_transition_batch_unlocked(
          context, bitOffset, bitWidth, changed, posedge, negedge, 0, &sequence,
          oldValue, oldUnknown, newValue, newUnknown))
    return false;
  uint64_t firstCanonicalBit = 0;
  uint64_t lastCanonicalBit = 0;
  bool packedCanonical = bitWidth <= 64 && bitWidth != 0 &&
                         canonicalBit(0, firstCanonicalBit) &&
                         canonicalBit(bitWidth - 1, lastCanonicalBit);
  if (packedCanonical) {
    // Preserve the publication-before-canonical ordering above, but merge a
    // bounded payload with word operations instead of revisiting every bit.
    uint64_t mask = loadPackedBytes(changed, 0, bitWidth);
    uint64_t value = loadPackedBytes(newValue, 0, bitWidth);
    uint64_t unknown =
        newUnknown ? loadPackedBytes(newUnknown, 0, bitWidth) : 0;
    storePackedBits(
        context->stateValue, firstCanonicalBit, bitWidth,
        (loadPackedBits(context->stateValue, firstCanonicalBit, bitWidth) &
         ~mask) |
            (value & mask));
    storePackedBits(
        context->stateUnknown, firstCanonicalBit, bitWidth,
        (loadPackedBits(context->stateUnknown, firstCanonicalBit, bitWidth) &
         ~mask) |
            (unknown & mask));
  } else
    for (uint64_t bit = 0; bit != bitWidth; ++bit) {
      uint64_t absolute = 0;
      if (!byteBit(changed, bit) || !canonicalBit(bit, absolute))
        continue;
      uint64_t mask = uint64_t{1} << (absolute % 64);
      uint64_t &valueLimb = context->stateValue[absolute / 64];
      uint64_t &unknownLimb = context->stateUnknown[absolute / 64];
      valueLimb = byteBit(newValue, bit) ? valueLimb | mask : valueLimb & ~mask;
      unknownLimb = newUnknown && byteBit(newUnknown, bit)
                        ? unknownLimb | mask
                        : unknownLimb & ~mask;
    }
  obelisk_rt_invalidate_signal_snapshots_unlocked(context, bitOffset, bitWidth);
  if (obelisk_rt_has_conditional_signal_waiters(context)) {
    for (uint64_t bit = 0; bit != bitWidth; ++bit) {
      if (!byteBit(changed, bit) || bit > static_cast<uint64_t>(INT64_MAX))
        continue;
      uint64_t eventHandle =
          nativeHandleOffset(bitOffset, static_cast<int64_t>(bit));
      if (eventHandle == UINT64_MAX)
        continue;
      context->signalValueSnapshots[eventHandle] = {
          sequence, byteBit(newValue, bit),
          newUnknown && byteBit(newUnknown, bit)};
    }
    for (uint64_t bit = 0; bit != bitWidth; ++bit) {
      if (!byteBit(changed, bit) || bit > static_cast<uint64_t>(INT64_MAX))
        continue;
      uint64_t eventHandle =
          nativeHandleOffset(bitOffset, static_cast<int64_t>(bit));
      if (eventHandle == UINT64_MAX)
        continue;
      uint32_t edges = OBELISK_RT_SIGNAL_CHANGE;
      if (byteBit(posedge, bit))
        edges |= OBELISK_RT_SIGNAL_POSEDGE;
      if (byteBit(negedge, bit))
        edges |= OBELISK_RT_SIGNAL_NEGEDGE;
      if (!obelisk_rt_latch_conditional_signal_waiters_unlocked(
              context, eventHandle, edges))
        return false;
    }
  }
  bool previousCanonicalPlane = context->observerForcesCanonicalPlane;
  context->observerForcesCanonicalPlane = true;
  bool observerStatus =
      obelisk_rt_notify_observer_signal_unlocked(context, bitOffset, bitWidth);
  context->observerForcesCanonicalPlane = previousCanonicalPlane;
  if (!observerStatus)
    return false;
  if (++context->schedulerEpoch == 0)
    context->schedulerEpoch = 1;
  return true;
}

namespace {

void schedulerSignalTransition(obelisk_rt_context *context, uint64_t bitOffset,
                               uint64_t bitWidth, const uint8_t *oldValue,
                               const uint8_t *oldUnknown,
                               const uint8_t *newValue,
                               const uint8_t *newUnknown,
                               bool establishesOverride) {
  if (!context || bitOffset == UINT64_MAX || bitWidth == 0 ||
      bitWidth > UINT64_MAX - 7 || !oldValue || !newValue)
    return;
  ContextTransaction transaction(context);
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    if (context->schedulerStatus != OBELISK_RT_OK)
      return;
    bool packedRange = bitWidth <= 64;
    uint32_t staticID = 0;
    int64_t staticOffset = 0;
    if (packedRange && decodeNativeStatic(bitOffset, staticID, staticOffset)) {
      const NativeStaticState *state = findNativeStaticState(context, staticID);
      packedRange =
          state && staticOffset >= 0 &&
          static_cast<uint64_t>(staticOffset) <= state->bitWidth &&
          bitWidth <= state->bitWidth - static_cast<uint64_t>(staticOffset);
    } else {
      packedRange = false;
    }
    if (packedRange) {
      uint64_t widthMask = packedWidthMask(bitWidth);
      uint64_t oldValueBits =
          loadPackedBytes(oldValue, 0, bitWidth) & widthMask;
      uint64_t oldUnknownBits =
          oldUnknown ? loadPackedBytes(oldUnknown, 0, bitWidth) & widthMask : 0;
      uint64_t newValueBits =
          loadPackedBytes(newValue, 0, bitWidth) & widthMask;
      uint64_t newUnknownBits =
          newUnknown ? loadPackedBytes(newUnknown, 0, bitWidth) & widthMask : 0;
      uint64_t changedBits =
          (oldValueBits ^ newValueBits) | (oldUnknownBits ^ newUnknownBits);
      if (changedBits == 0)
        return;
      uint64_t oldZero = ~oldUnknownBits & ~oldValueBits & widthMask;
      uint64_t oldOne = ~oldUnknownBits & oldValueBits & widthMask;
      uint64_t newZero = ~newUnknownBits & ~newValueBits & widthMask;
      uint64_t newOne = ~newUnknownBits & newValueBits & widthMask;
      uint64_t posedgeBits =
          ((oldZero & ~newZero) | (oldUnknownBits & newOne)) & widthMask;
      uint64_t negedgeBits =
          ((oldOne & ~newOne) | (oldUnknownBits & newZero)) & widthMask;
      (void)publishNativeSignalTransitionUnlocked(
          context, bitOffset, bitWidth,
          reinterpret_cast<const uint8_t *>(&changedBits),
          reinterpret_cast<const uint8_t *>(&posedgeBits),
          reinterpret_cast<const uint8_t *>(&negedgeBits), oldValue, oldUnknown,
          newValue, newUnknown, establishesOverride);
      return;
    }
    PackedSignalTransitionBuffer transitions(bitWidth);
    bool changed = false;
    for (uint64_t bit = 0; bit != bitWidth; ++bit) {
      bool oldValueBit = byteBit(oldValue, bit);
      bool oldUnknownBit = oldUnknown && byteBit(oldUnknown, bit);
      bool newValueBit = byteBit(newValue, bit);
      bool newUnknownBit = newUnknown && byteBit(newUnknown, bit);
      uint32_t edges = transitionEdges(oldValueBit, oldUnknownBit, newValueBit,
                                       newUnknownBit);
      if (edges == 0)
        continue;
      if (bit > static_cast<uint64_t>(INT64_MAX))
        continue;
      uint64_t eventHandle =
          nativeHandleOffset(bitOffset, static_cast<int64_t>(bit));
      uint32_t automaticID = 0;
      uint32_t staticID = 0;
      int64_t eventOffset = 0;
      bool automatic =
          decodeNativeAutomatic(eventHandle, automaticID, eventOffset);
      bool boundedStatic =
          !automatic && decodeNativeStatic(eventHandle, staticID, eventOffset);
      bool inRange = eventOffset >= 0;
      if (automatic) {
        auto found = context->nativeAutomaticStates.find(automaticID);
        inRange &= found != context->nativeAutomaticStates.end() &&
                   static_cast<uint64_t>(eventOffset) < found->second.bitWidth;
      } else if (boundedStatic) {
        auto found = context->nativeStaticStates.find(staticID);
        inRange &= found != context->nativeStaticStates.end() &&
                   static_cast<uint64_t>(eventOffset) < found->second.bitWidth;
      } else {
        inRange &= decodeNativeGlobal(eventHandle, eventOffset);
      }
      if (!inRange)
        continue;
      transitions.record(bit, edges);
      changed = true;
    }
    if (changed)
      (void)publishNativeSignalTransitionUnlocked(
          context, bitOffset, bitWidth, transitions.changed(),
          transitions.posedge(), transitions.negedge(), oldValue, oldUnknown,
          newValue, newUnknown, establishesOverride);
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_OUT_OF_MEMORY);
  }
  OBELISK_RT_CATCH_ALL {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
  }
}

} // namespace

extern "C" void obelisk_rt_v1_scheduler_signal_transition(
    obelisk_rt_context *context, uint64_t bitOffset, uint64_t bitWidth,
    const uint8_t *oldValue, const uint8_t *oldUnknown, const uint8_t *newValue,
    const uint8_t *newUnknown) {
  schedulerSignalTransition(context, bitOffset, bitWidth, oldValue, oldUnknown,
                            newValue, newUnknown, false);
}

// IEEE 1800-2017 10.6.2: installing a force/assign and releasing a force back
// to an active assign are value changes of the overridden target, not driver
// changes behind an override. Their masks already describe the post-transition
// ownership, so publish them exempt from ordinary override suppression.
void publishOverrideTransition(obelisk_rt_context *context, uint64_t bitOffset,
                               uint64_t bitWidth, const uint8_t *oldValue,
                               const uint8_t *oldUnknown,
                               const uint8_t *newValue,
                               const uint8_t *newUnknown) {
  schedulerSignalTransition(context, bitOffset, bitWidth, oldValue, oldUnknown,
                            newValue, newUnknown, true);
}

extern "C" void obelisk_rt_v1_scheduler_static_transition(
    obelisk_rt_context *context, uint32_t staticState, uint64_t lowBit,
    uint64_t bitWidth, uint64_t oldValue, uint64_t oldUnknown,
    uint64_t newValue, uint64_t newUnknown) {
  if (!context || staticState == 0 || bitWidth == 0 || bitWidth > 64)
    return;
  uint64_t widthMask = packedWidthMask(bitWidth);
  oldValue &= widthMask;
  oldUnknown &= widthMask;
  newValue &= widthMask;
  newUnknown &= widthMask;
  uint64_t changed = (oldValue ^ newValue) | (oldUnknown ^ newUnknown);
  if (changed == 0)
    return;

  // The generated leaf is valid only while the installed exact-fanout plan is
  // the active clean AOT kernel. A mid-slot handover remains correct by
  // entering the ordinary transition path with the same scalar planes.
  if (activeNativeAOTContext != context || !canUseStaticAOTFanout(context)) {
    uint64_t handle = obelisk_rt_stable_handle_encode(
        OBELISK_RT_STABLE_HANDLE_STATIC, staticState,
        static_cast<int64_t>(lowBit));
    obelisk_rt_v1_scheduler_signal_transition(
        context, handle, bitWidth, reinterpret_cast<const uint8_t *>(&oldValue),
        reinterpret_cast<const uint8_t *>(&oldUnknown),
        reinterpret_cast<const uint8_t *>(&newValue),
        reinterpret_cast<const uint8_t *>(&newUnknown));
    return;
  }
  const NativeStaticState *state = findNativeStaticState(context, staticState);
  if (!state || lowBit > state->bitWidth ||
      bitWidth > state->bitWidth - lowBit) {
    context->schedulerStatus = OBELISK_RT_LAYOUT_MISMATCH;
    return;
  }
  uint64_t handle = obelisk_rt_stable_handle_encode(
      OBELISK_RT_STABLE_HANDLE_STATIC, staticState,
      static_cast<int64_t>(lowBit));
  obelisk_rt_coverage_record_transition_unlocked(
      context, handle, bitWidth, reinterpret_cast<const uint8_t *>(&changed),
      reinterpret_cast<const uint8_t *>(&newValue),
      reinterpret_cast<const uint8_t *>(&newUnknown));
  uint8_t edgeKinds = 0;
  if (staticState < context->nativeScheduleStaticStateFanoutEdges.size()) {
    edgeKinds = context->nativeScheduleStaticStateFanoutEdges[staticState];
  }
  uint8_t posedgeKinds = (uint8_t{1} << OBELISK_RT_WAIT_EDGE_POSEDGE) |
                         (uint8_t{1} << OBELISK_RT_WAIT_EDGE_BOTH);
  uint8_t negedgeKinds = (uint8_t{1} << OBELISK_RT_WAIT_EDGE_NEGEDGE) |
                         (uint8_t{1} << OBELISK_RT_WAIT_EDGE_BOTH);
  uint64_t posedge = 0;
  uint64_t negedge = 0;
  if (context->covergroupClockEvents ||
      (edgeKinds & (posedgeKinds | negedgeKinds)) != 0) {
    uint64_t oldZero = ~oldUnknown & ~oldValue & widthMask;
    uint64_t oldOne = ~oldUnknown & oldValue & widthMask;
    uint64_t newZero = ~newUnknown & ~newValue & widthMask;
    uint64_t newOne = ~newUnknown & newValue & widthMask;
    if (context->covergroupClockEvents || (edgeKinds & posedgeKinds) != 0)
      posedge = ((oldZero & ~newZero) | (oldUnknown & newOne)) & widthMask;
    if (context->covergroupClockEvents || (edgeKinds & negedgeKinds) != 0)
      negedge = ((oldOne & ~newOne) | (oldUnknown & newZero)) & widthMask;
  }
  if (!publishCovergroupClockTransitionUnlocked(
          context, handle, bitWidth,
          reinterpret_cast<const uint8_t *>(&changed),
          reinterpret_cast<const uint8_t *>(&posedge),
          reinterpret_cast<const uint8_t *>(&negedge),
          reinterpret_cast<const uint8_t *>(&newValue),
          reinterpret_cast<const uint8_t *>(&newUnknown)))
    return;
  // Generated Eval bodies publish their statically indexed actor ingress
  // locally. The retained v1 callback exists only for runtime-owned observers
  // such as covergroup clock events; do not enqueue the same native fanout a
  // second time through the runtime plan.
  if ((context->nativeSchedulePlan->flags &
       OBELISK_RT_NATIVE_SCHEDULE_STATIC_EVAL_ISLAND) != 0) {
    if (++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
    return;
  }
  if (edgeKinds == 0) {
    if (++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
    return;
  }
  uint64_t observedEdges = 0;
  if ((edgeKinds & (uint8_t{1} << OBELISK_RT_WAIT_EDGE_CHANGE)) != 0)
    observedEdges |= changed;
  if ((edgeKinds & posedgeKinds) != 0)
    observedEdges |= posedge;
  if ((edgeKinds & negedgeKinds) != 0)
    observedEdges |= negedge;
  if (observedEdges == 0) {
    if (++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
    return;
  }

  const obelisk_rt_static_fanout_entry *begin =
      context->nativeScheduleFanoutEntries;
  const obelisk_rt_static_fanout_entry *end =
      begin + context->nativeScheduleFanoutEntryCount;
  auto first = end;
  auto last = end;
  if (staticState < context->nativeScheduleFanoutRanges.size()) {
    auto [firstIndex, lastIndex] =
        context->nativeScheduleFanoutRanges[staticState];
    if (firstIndex <= lastIndex &&
        lastIndex <= context->nativeScheduleFanoutEntryCount) {
      first = begin + firstIndex;
      last = begin + lastIndex;
    }
  } else {
    first =
        std::lower_bound(begin, end, staticState,
                         [](const obelisk_rt_static_fanout_entry &entry,
                            uint32_t id) { return entry.static_state < id; });
    last = first;
    while (last != end && last->static_state == staticState)
      ++last;
  }
  bool published = false;
  uint64_t publishedEnd = lowBit + bitWidth;
  for (auto entry = first; entry != last; ++entry) {
    ++context->signalDiagnostics.aotFanoutEntries;
    uint64_t overlapLow = std::max(lowBit, entry->low_bit);
    uint64_t overlapHigh =
        std::min(publishedEnd, entry->low_bit + entry->bit_width);
    if (overlapLow >= overlapHigh)
      continue;
    uint64_t localLow = overlapLow - lowBit;
    uint64_t localWidth = overlapHigh - overlapLow;
    uint64_t overlapMask = packedWidthMask(localWidth)
                           << static_cast<unsigned>(localLow);
    uint64_t observed = changed;
    switch (entry->edge) {
    case OBELISK_RT_WAIT_EDGE_POSEDGE:
      observed = posedge;
      break;
    case OBELISK_RT_WAIT_EDGE_NEGEDGE:
      observed = negedge;
      break;
    case OBELISK_RT_WAIT_EDGE_BOTH:
      observed = posedge | negedge;
      break;
    default:
      break;
    }
    if ((observed & overlapMask) == 0)
      continue;

    uint32_t slot = entry->actor_slot;
    if (slot >= context->nativeScheduleActors.size()) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return;
    }
    obelisk_rt_process_instance_v1 *actor = context->nativeScheduleActors[slot];
    if (!actor)
      continue;
    size_t index = context->nativeScheduleActorIndices[slot];
    if (index >= context->scheduledProcesses.size()) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return;
    }
    ScheduledProcess &scheduled = context->scheduledProcesses[index];
    bool activeSelf = context->activeLogicalProcessToken ==
                      (kNativeLogicalProcessTag | scheduled.token);
    if (activeSelf &&
        (entry->reserved & OBELISK_RT_FANOUT_SUPPRESS_ACTIVE_SELF) != 0)
      continue;
    if ((actor->continuation != entry->continuation && !activeSelf) ||
        scheduled.instance != actor || !scheduled.started ||
        (scheduled.signalTriggered && !activeSelf) ||
        (scheduled.suspendKind != OBELISK_RT_SUSPEND_CHANGE &&
         scheduled.suspendKind != OBELISK_RT_SUSPEND_EDGE))
      continue;
    if (!published) {
      if (context->nextSchedulerSequence == 0) {
        context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
        return;
      }
      ++context->nextSchedulerSequence;
      if (context->signalDiagnosticsEnabled)
        ++context->signalDiagnostics.publications;
      published = true;
    }
    scheduled.signalTriggered = true;
    uint32_t node = entry->compute_node;
    if (node >= context->nativeScheduleNodes.size() ||
        node / 64 >= context->nativeScheduleReadyNodes.size()) {
      context->schedulerStatus = OBELISK_RT_INVALID_CONTINUATION;
      return;
    }
    context->nativeScheduleReadyNodes[node / 64] |= uint64_t{1} << (node % 64);
    context->nativeScheduleMinimumActivatedNode =
        std::min(context->nativeScheduleMinimumActivatedNode, node);
  }
  if (++context->schedulerEpoch == 0)
    context->schedulerEpoch = 1;
}

extern "C" void
obelisk_rt_v1_scheduler_activate_static_nodes(obelisk_rt_context *context,
                                              const uint64_t *nodeWords,
                                              uint32_t wordCount) {
  if (!context || !nodeWords)
    return;
  if (!context->nativeSchedulePlan ||
      wordCount != context->nativeScheduleReadyNodes.size()) {
    context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
    return;
  }
  bool hasNodes = false;
  for (uint32_t word = 0; word != wordCount; ++word)
    hasNodes |= nodeWords[word] != 0;
  if (!hasNodes)
    return;
  if ((activeNativeAOTContext != context &&
       !canUseIndexedExternalAOTFanout(context)) ||
      (!canUseStaticAOTFanout(context) &&
       !canUseIndexedExternalAOTFanout(context)) ||
      (context->nativeSchedulePlan->flags &
       (OBELISK_RT_NATIVE_SCHEDULE_CLEAN_SUPERSTEP |
        OBELISK_RT_NATIVE_SCHEDULE_STATIC_EVAL_ISLAND)) == 0 ||
      context->nativeScheduleReadyNodes.empty()) {
    context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
    return;
  }

  bool published = false;
  for (uint32_t word = 0; word != wordCount; ++word) {
    uint64_t nodes = nodeWords[word];
    while (nodes != 0) {
      uint32_t bit = static_cast<uint32_t>(__builtin_ctzll(nodes));
      uint32_t node = word * 64 + bit;
      nodes &= nodes - 1;
      if (node >= context->nativeScheduleNodes.size()) {
        context->schedulerStatus = OBELISK_RT_INVALID_CONTINUATION;
        return;
      }
      const obelisk_rt_native_schedule_node &entry =
          context->nativeScheduleNodes[node];
      if (entry.actor_slot >= context->nativeScheduleActors.size()) {
        context->schedulerStatus = OBELISK_RT_INVALID_CONTINUATION;
        return;
      }
      obelisk_rt_process_instance_v1 *actor =
          context->nativeScheduleActors[entry.actor_slot];
      if (!actor)
        continue;
      size_t index = context->nativeScheduleActorIndices[entry.actor_slot];
      if (index >= context->scheduledProcesses.size()) {
        context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
        return;
      }
      ScheduledProcess &scheduled = context->scheduledProcesses[index];
      bool activeSelf = context->activeLogicalProcessToken ==
                        (kNativeLogicalProcessTag | scheduled.token);
      if ((actor->continuation != entry.continuation && !activeSelf) ||
          scheduled.instance != actor || !scheduled.started ||
          (scheduled.signalTriggered && !activeSelf) ||
          (scheduled.suspendKind != OBELISK_RT_SUSPEND_CHANGE &&
           scheduled.suspendKind != OBELISK_RT_SUSPEND_EDGE))
        continue;
      scheduled.signalTriggered = true;
      context->nativeScheduleReadyNodes[word] |= uint64_t{1} << bit;
      context->nativeScheduleMinimumActivatedNode =
          std::min(context->nativeScheduleMinimumActivatedNode, node);
      ++context->signalDiagnostics.aotFanoutEntries;
      published = true;
    }
  }
  if (!published)
    return;
  if (context->nextSchedulerSequence == 0) {
    context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
    return;
  }
  ++context->nextSchedulerSequence;
  if (context->signalDiagnosticsEnabled)
    ++context->signalDiagnostics.publications;
  if (++context->schedulerEpoch == 0)
    context->schedulerEpoch = 1;
}

extern "C" void obelisk_rt_v1_scheduler_real_transition(
    obelisk_rt_context *context, uint64_t bitOffset, uint32_t bitWidth,
    const void *oldValue, const void *newValue) {
  if (!context || bitOffset == UINT64_MAX ||
      (bitWidth != 32 && bitWidth != 64) || !oldValue || !newValue)
    return;
  bool changed = false;
  if (bitWidth == 32) {
    float oldReal = 0.0f;
    float newReal = 0.0f;
    std::memcpy(&oldReal, oldValue, sizeof(oldReal));
    std::memcpy(&newReal, newValue, sizeof(newReal));
    changed = oldReal != newReal || std::isnan(newReal);
  } else {
    double oldReal = 0.0;
    double newReal = 0.0;
    std::memcpy(&oldReal, oldValue, sizeof(oldReal));
    std::memcpy(&newReal, newValue, sizeof(newReal));
    changed = oldReal != newReal || std::isnan(newReal);
  }
  if (!changed)
    return;
  ContextTransaction transaction(context);
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    if (context->schedulerStatus != OBELISK_RT_OK)
      return;
    if (!obelisk_rt_publish_signal_occurrence_unlocked(
            context, bitOffset, bitWidth, OBELISK_RT_SIGNAL_CHANGE))
      return;
    obelisk_rt_invalidate_signal_snapshots_unlocked(context, bitOffset,
                                                    bitWidth);
    if (!obelisk_rt_latch_conditional_signal_range_unlocked(
            context, bitOffset, bitWidth, OBELISK_RT_SIGNAL_CHANGE))
      return;
    if (!obelisk_rt_notify_observer_signal_unlocked(context, bitOffset,
                                                    bitWidth))
      return;
    if (++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
  }
  OBELISK_RT_CATCH_ALL {
    obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
  }
}

extern "C" void obelisk_rt_v1_scheduler_event(obelisk_rt_context *context,
                                              uint64_t stableID,
                                              uint32_t nonblocking) {
  obelisk_rt_v1_scheduler_event_after(context, stableID, nonblocking, 0);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_scheduler_event_create(obelisk_rt_context *context,
                                     uint64_t *outStableID) {
  if (!context || !outStableID)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outStableID = UINT64_MAX;
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    if (context->schedulerStatus != OBELISK_RT_OK)
      return context->schedulerStatus;
    if (context->nextDynamicEventID == 0 ||
        context->nextDynamicEventID >=
            OBELISK_RT_STABLE_HANDLE_DYNAMIC_EVENT_TAG)
      return OBELISK_RT_OUT_OF_RESOURCES;
    *outStableID = OBELISK_RT_STABLE_HANDLE_DYNAMIC_EVENT_TAG |
                   context->nextDynamicEventID++;
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}

extern "C" void obelisk_rt_v1_scheduler_event_after(obelisk_rt_context *context,
                                                    uint64_t stableID,
                                                    uint32_t nonblocking,
                                                    uint64_t delay) {
  if (!context || nonblocking > 1 || (!nonblocking && delay != 0)) {
    if (context)
      obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return;
  }
  // IEEE 1800-2017 15.5.5.2: triggering a null event has no effect. The
  // all-ones stable ID is the canonical null event in native execution; do
  // not create its queue or enqueue a delayed/nonblocking occurrence.
  if (stableID == UINT64_MAX)
    return;
  ContextTransaction transaction(context);
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    if (context->schedulerStatus != OBELISK_RT_OK)
      return;
    if (context->activeExecRegion == OBELISK_RT_REGION_POSTPONED) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return;
    }
    uint32_t retainedAutomaticID = 0;
    int64_t automaticOffset = 0;
    if (decodeNativeAutomatic(stableID, retainedAutomaticID, automaticOffset)) {
      auto found = context->nativeAutomaticStates.find(retainedAutomaticID);
      if (found == context->nativeAutomaticStates.end()) {
        context->schedulerStatus = OBELISK_RT_INVALID_HANDLE;
        return;
      }
      if (nonblocking && found->second.referenceCount == UINT64_MAX) {
        context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
        return;
      }
    }
    if (nonblocking) {
      if (context->nextSchedulerSequence == 0) {
        context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
        return;
      }
      uint64_t dueTime = delay > UINT64_MAX - context->schedulerTime
                             ? UINT64_MAX
                             : context->schedulerTime + delay;
      uint32_t execRegion = obelisk_rt_commit_region(
          context->activeHomeRegion == UINT32_MAX
              ? static_cast<uint32_t>(OBELISK_RT_REGION_ACTIVE)
              : context->activeHomeRegion);
      if (execRegion == UINT32_MAX) {
        context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
        return;
      }
      context->scheduledDesignEvents.push_back({context->nextSchedulerSequence,
                                                dueTime, execRegion, stableID,
                                                retainedAutomaticID});
      if (retainedAutomaticID != 0)
        ++context->nativeAutomaticStates.find(retainedAutomaticID)
              ->second.referenceCount;
      ++context->nextSchedulerSequence;
      return;
    }
    EventState &event = context->events[stableID];
    if (++event.generation == 0)
      event.generation = 1;
    event.lastTriggeredTime = context->schedulerTime;
    if (!obelisk_rt_notify_event_order_waiters_unlocked(context, stableID))
      return;
    if (!obelisk_rt_notify_observer_event_unlocked(context, stableID))
      return;
    if (++context->schedulerEpoch == 0)
      context->schedulerEpoch = 1;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    ContextMutexLock lock(context);
    context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
  }
  OBELISK_RT_CATCH_ALL {
    ContextMutexLock lock(context);
    context->schedulerStatus = OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" void
obelisk_rt_v1_scheduler_event_replace_after(obelisk_rt_context *context,
                                            uint64_t stableID, uint32_t active,
                                            uint64_t delay) {
  if (!context || active > 1) {
    if (context)
      obelisk_rt_v1_scheduler_fail(context, OBELISK_RT_INVALID_ARGUMENT);
    return;
  }
  // A null named event is inert for cancellation as well as triggering
  // (IEEE 1800-2017 15.5.5.2).
  if (stableID == UINT64_MAX)
    return;
  ContextTransaction transaction(context);
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    if (context->schedulerStatus != OBELISK_RT_OK)
      return;
    if (!context->clockOccurrences) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return;
    }
    ClockOccurrenceFeatureState &clock = *context->clockOccurrences;
    uint64_t ownerToken = context->activeLogicalProcessToken;
    if (ownerToken == 0 || clock.waits.find(ownerToken) == clock.waits.end()) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return;
    }
    if (!clock.replaceableEvents) {
      if (!active)
        return;
      clock.replaceableEvents =
          std::make_unique<ReplaceableEventFeatureState>();
    }
    ReplaceableEventFeatureState &feature = *clock.replaceableEvents;
    auto foundPending = feature.pending.find(stableID);
    if (foundPending != feature.pending.end() &&
        foundPending->second.ownerToken != ownerToken) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return;
    }
    if (foundPending == feature.pending.end()) {
      auto [owned, insertedOwner] = feature.ownedTimers.try_emplace(ownerToken);
      size_t priorOwnedSize = owned->second.size();
      OBELISK_RT_TRY {
        owned->second.push_back(stableID);
        auto [inserted, success] = feature.pending.try_emplace(stableID);
        if (!success) {
          owned->second.pop_back();
          if (insertedOwner && owned->second.empty())
            feature.ownedTimers.erase(owned);
          context->schedulerStatus = OBELISK_RT_INVALID_DESIGN;
          return;
        }
        inserted->second.ownerToken = ownerToken;
        foundPending = inserted;
      }
      OBELISK_RT_CATCH_ALL {
        if (owned->second.size() != priorOwnedSize)
          owned->second.resize(priorOwnedSize);
        if (insertedOwner && owned->second.empty())
          feature.ownedTimers.erase(owned);
        OBELISK_RT_RETHROW;
      }
    }
    ReplaceableEventPending &pending = foundPending->second;
    // The generation is a permanent guard, not a wrapping stale-event tag.
    // Once exhausted, no later replacement may accidentally make an ancient
    // maturity current again.
    if (pending.generation == UINT64_MAX ||
        (active && context->nextSchedulerSequence == 0)) {
      context->schedulerStatus = OBELISK_RT_OUT_OF_RESOURCES;
      return;
    }
    ++pending.generation;
    if (pending.scheduled) {
      feature.calendar.erase(pending.event);
      pending.scheduled = false;
    }
    if (!active)
      return;

    // Match ordinary delayed named-event saturation exactly. UINT64_MAX is a
    // permanent deadline/sentinel and must never wrap into an early expiry.
    uint64_t dueTime = delay > UINT64_MAX - context->schedulerTime
                           ? UINT64_MAX
                           : context->schedulerTime + delay;
    uint64_t sequence = context->nextSchedulerSequence++;
    auto [event, inserted] = feature.calendar.emplace(
        std::pair{dueTime, sequence},
        ScheduledDesignEvent{sequence, dueTime, OBELISK_RT_REGION_RE_NBA,
                             stableID, 0});
    if (!inserted) {
      context->schedulerStatus = OBELISK_RT_INVALID_DESIGN;
      return;
    }
    pending.event = event;
    pending.scheduled = true;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    ContextMutexLock lock(context);
    context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
  }
  OBELISK_RT_CATCH_ALL {
    ContextMutexLock lock(context);
    context->schedulerStatus = OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" uint32_t
obelisk_rt_v1_scheduler_event_triggered(obelisk_rt_context *context,
                                        uint64_t stableID) {
  if (!context)
    return 0;
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    auto found = context->events.find(stableID);
    return found != context->events.end() && found->second.generation != 0 &&
           found->second.lastTriggeredTime == context->schedulerTime;
  }
  OBELISK_RT_CATCH_ALL { return 0; }
}

extern "C" uint32_t
obelisk_rt_v1_scheduler_wait_order_failed(obelisk_rt_context *context) {
  if (!context)
    return 0;
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    return context->activeLogicalProcessToken != 0 &&
           context->activeWaitOrderFailed;
  }
  OBELISK_RT_CATCH_ALL { return 0; }
}

extern "C" uint64_t
obelisk_rt_v1_clock_occurrence_consume(obelisk_rt_context *context,
                                       uint64_t occurrenceSite) {
  if (!context || occurrenceSite == 0)
    return 0;
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    uint64_t token = context->activeLogicalProcessToken;
    if (token == 0 || !context->clockOccurrences)
      return 0;
    auto found = context->clockOccurrences->waits.find(token);
    if (found == context->clockOccurrences->waits.end() ||
        found->second.occurrenceSite != occurrenceSite)
      return 0;
    ClockOccurrenceWaitState &state = found->second;
    if (state.consumedCohorts == state.finalizedCohorts.size() &&
        state.hasCurrentKey) {
      ClockOccurrenceWaveKey active{context->schedulerTime,
                                    context->schedulerSlotProgress,
                                    context->activeExecRegion};
      // The producer fragment or barrier must return before its mask is
      // visible. Until then another publication can still join this exact
      // wave, so an early consume observes no partial cohort.
      if (!(state.currentKey == active))
        finalizeClockOccurrenceWave(state);
    }
    if (state.consumedCohorts == state.finalizedCohorts.size())
      return 0;
    uint64_t result = state.finalizedCohorts[state.consumedCohorts++];
    if (state.consumedCohorts == state.finalizedCohorts.size()) {
      state.finalizedCohorts.clear();
      state.consumedCohorts = 0;
    } else if (state.consumedCohorts >= 1024 &&
               state.consumedCohorts * 2 >= state.finalizedCohorts.size()) {
      state.finalizedCohorts.erase(state.finalizedCohorts.begin(),
                                   state.finalizedCohorts.begin() +
                                       state.consumedCohorts);
      state.consumedCohorts = 0;
    }
    return result;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    ContextMutexLock lock(context);
    context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
    return 0;
  }
  OBELISK_RT_CATCH_ALL {
    ContextMutexLock lock(context);
    context->schedulerStatus = OBELISK_RT_INVALID_ARGUMENT;
    return 0;
  }
}

extern "C" uint64_t obelisk_rt_v1_nochange_update(obelisk_rt_context *context,
                                                  uint64_t occurrenceSite,
                                                  uint64_t occurrenceMask,
                                                  int64_t startOffset,
                                                  int64_t endOffset) {
  if (!context || occurrenceSite == 0 || (occurrenceMask & ~uint64_t{7}) != 0)
    return 0;
  OBELISK_RT_TRY {
    ContextMutexLock lock(context);
    uint64_t token = context->activeLogicalProcessToken;
    if (token == 0 || !context->clockOccurrences) {
      context->schedulerStatus = OBELISK_RT_INVALID_LIFECYCLE;
      return 0;
    }
    if (!context->noChangeChecks)
      context->noChangeChecks = std::make_unique<NoChangeFeatureState>();
    NoChangeCheckState &state =
        context->noChangeChecks->checks[{token, occurrenceSite}];
    if (!state.initialized) {
      state.startOffset = startOffset;
      state.endOffset = endOffset;
      state.initialized = true;
    } else if (state.startOffset != startOffset ||
               state.endOffset != endOffset) {
      context->schedulerStatus = OBELISK_RT_INVALID_DESIGN;
      return 0;
    }

    if (occurrenceMask == 0) {
      if (!finalizeNoChangeOpenWindow(context, state))
        return 0;
      uint64_t reports = state.pendingReports;
      state.pendingReports = 0;
      pruneNoChangeData(state, context->schedulerTime);
      return reports;
    }

    uint64_t now = context->schedulerTime;
    state.windows.erase(
        std::remove_if(state.windows.begin(), state.windows.end(),
                       [&](const NoChangeClosedWindow &window) {
                         return window.end <= static_cast<__int128>(now);
                       }),
        state.windows.end());

    // IEEE 1800-2017 31.4.6 defines strict numeric-time endpoints. Ordered
    // clock cohorts are retained for repeated derived edges, but data at the
    // same time is independent of within-cohort processing order: history
    // makes a later leading/trailing occurrence see it retroactively.
    if ((occurrenceMask & 2) != 0 && !recordNoChangeData(context, state, now))
      return 0;
    if ((occurrenceMask & 1) != 0) {
      state.openBegin = static_cast<__int128>(now) - startOffset;
      state.openDataBegin = state.dataBegin;
      state.open = true;
    }
    if ((occurrenceMask & 4) != 0 && !closeNoChangeWindow(context, state, now))
      return 0;
    pruneNoChangeData(state, now);
    return 0;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    ContextMutexLock lock(context);
    context->schedulerStatus = OBELISK_RT_OUT_OF_MEMORY;
    return 0;
  }
  OBELISK_RT_CATCH_ALL {
    ContextMutexLock lock(context);
    context->schedulerStatus = OBELISK_RT_INVALID_ARGUMENT;
    return 0;
  }
}
