//===- ScanFormat.cpp - Cached dynamic scanf format plans ----------------===//

#include "RuntimeInternal.h"

#include <cctype>

namespace {

constexpr size_t maxCachedDynamicScanPlans = 8;

OBELISK_RT_FEATURE_HELPER std::shared_ptr<const DynamicScanPlan>
parsePlan(std::string format) {
  auto plan = std::make_shared<DynamicScanPlan>();
  plan->format = std::move(format);
  std::string pending;
  for (size_t index = 0; index < plan->format.size(); ++index) {
    if (plan->format[index] != '%') {
      pending.push_back(plan->format[index]);
      continue;
    }
    size_t start = index;
    if (++index == plan->format.size()) {
      plan->error = "%";
      return plan;
    }
    char specifier = plan->format[index];
    if (specifier == '%') {
      pending.push_back('%');
      continue;
    }
    bool suppressed = specifier == '*';
    if (suppressed) {
      if (++index == plan->format.size()) {
        plan->error = plan->format.substr(start);
        return plan;
      }
      specifier = plan->format[index];
    }
    uint64_t width = 0;
    while (specifier >= '0' && specifier <= '9') {
      uint64_t digit = static_cast<uint64_t>(specifier - '0');
      if (width > (UINT64_MAX - digit) / 10)
        width = UINT64_MAX;
      else
        width = width * 10 + digit;
      if (++index == plan->format.size()) {
        plan->error = plan->format.substr(start);
        return plan;
      }
      specifier = plan->format[index];
    }
    if (std::string_view("bBoOdDhHxXeEfFgGsScCmMtTvVuUzZ").find(specifier) ==
        std::string_view::npos) {
      plan->error = plan->format.substr(start, index - start + 1);
      return plan;
    }
    if (plan->conversions.size() == UINT32_MAX) {
      plan->error = "too many conversions";
      return plan;
    }
    plan->conversions.push_back(
        {std::move(pending), width,
         static_cast<uint32_t>(static_cast<unsigned char>(specifier)),
         suppressed});
    pending.clear();
  }
  plan->suffix = std::move(pending);
  return plan;
}

} // namespace

OBELISK_RT_FEATURE_TEXT obelisk_rt_status obelisk_rt_dynamic_scan_plan(
    obelisk_rt_context *context, obelisk_rt_string_v1 format,
    std::shared_ptr<const DynamicScanPlan> &plan) noexcept {
  plan.reset();
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  obelisk_rt_status ownership = obelisk_rt_validate_string(context, format);
  if (ownership != OBELISK_RT_OK)
    return ownership;
  OBELISK_RT_TRY {
    uint64_t identity = 0;
    obelisk_rt_string_v1 inlineValue = 0;
    if ((format & UINT64_C(3)) == 0 && format != 0)
      identity = obelisk_rt_v1_object_id(
          reinterpret_cast<const obelisk_rt_object_v1 *>(
              static_cast<uintptr_t>(format)));
    else
      inlineValue = format;

    DynamicScanState *state = nullptr;
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      if (!context->dynamicScanState) {
        context->dynamicScanState = new (std::nothrow) DynamicScanState();
        if (!context->dynamicScanState)
          return OBELISK_RT_OUT_OF_MEMORY;
        context->dynamicScanState->destroy =
            [](DynamicScanState *state)
                { delete state; };
      }
      state = context->dynamicScanState;
    }
    std::lock_guard<std::mutex> lock(state->mutex);
    auto use = [&](size_t index) {
      plan = state->plans[index].plan;
      if (index != 0) {
        DynamicScanCacheEntry hit = std::move(state->plans[index]);
        state->plans.erase(state->plans.begin() + index);
        state->plans.insert(state->plans.begin(), std::move(hit));
      }
    };
    // Managed strings are immutable, and their object identities never
    // repeat within one context. Inline strings carry all bytes in the value.
    // These overwhelmingly common hits therefore require no content walk.
    for (size_t index = 0; index != state->plans.size(); ++index) {
      DynamicScanCacheEntry &entry = state->plans[index];
      bool sameIdentity = identity != 0 && entry.identity == identity;
      bool sameInline = identity == 0 && entry.identity == 0 &&
                        entry.inlineValue == inlineValue;
      if (!sameIdentity && !sameInline)
        continue;
      use(index);
      return OBELISK_RT_OK;
    }

    char scratch[8]{};
    const char *bytes = nullptr;
    uint64_t size = 0;
    obelisk_rt_status status =
        obelisk_rt_v1_string_view(format, scratch, &bytes, &size);
    if (status != OBELISK_RT_OK)
      return status;
    if (size > std::string{}.max_size())
      return OBELISK_RT_OUT_OF_MEMORY;
    uint64_t hash = obelisk_rt_v1_string_hash(format);
    for (size_t index = 0; index != state->plans.size(); ++index) {
      DynamicScanCacheEntry &entry = state->plans[index];
      bool sameValue = entry.hash == hash && entry.size == size && entry.plan &&
                       entry.plan->format.size() == size;
      if (!sameValue)
        continue;
      state->contentCompareBytes += size;
      if (std::memcmp(entry.plan->format.data(), bytes,
                      static_cast<size_t>(size)) != 0)
        continue;
      use(index);
      return OBELISK_RT_OK;
    }

    plan = parsePlan(std::string(bytes, static_cast<size_t>(size)));
    ++state->parseCount;
    state->plans.insert(state->plans.begin(),
                        {hash, size, identity, inlineValue, plan});
    if (state->plans.size() > maxCachedDynamicScanPlans)
      state->plans.pop_back();
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    plan.reset();
    return OBELISK_RT_OUT_OF_MEMORY;
  }
  OBELISK_RT_CATCH_ALL {
    plan.reset();
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" OBELISK_RT_FEATURE_TEXT obelisk_rt_status
obelisk_rt_v1_scan_dynamic_validate(obelisk_rt_context *context,
                                    obelisk_rt_string_v1 format,
                                    uint32_t planCursor, uint32_t file,
                                    uint32_t finalize,
                                    uint64_t allowedSpecifiers,
                                    uint32_t *outPlanCursor) {
  if (!context || !outPlanCursor || file > 1 || finalize > 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outPlanCursor = planCursor;
  std::shared_ptr<const DynamicScanPlan> plan;
  obelisk_rt_status status =
      obelisk_rt_dynamic_scan_plan(context, format, plan);
  if (status != OBELISK_RT_OK)
    return status;
  const char *task = file ? "$fscanf" : "$sscanf";
  if (!plan->error.empty()) {
    std::fprintf(stderr, "obelisk: malformed dynamic %s conversion '%s'\n",
                 task, plan->error.c_str());
    return OBELISK_RT_INVALID_ARGUMENT;
  }
  if (planCursor > plan->conversions.size()) {
    std::fprintf(stderr, "obelisk: invalid dynamic %s format cursor\n", task);
    return OBELISK_RT_INVALID_ARGUMENT;
  }
  auto letter = [](uint32_t specifier) {
    return static_cast<char>(
        std::tolower(static_cast<unsigned char>(static_cast<char>(specifier))));
  };
  auto allowed = [&](uint32_t specifier) {
    char normalized = letter(specifier);
    return normalized >= 'a' && normalized <= 'z' &&
           (allowedSpecifiers &
            (UINT64_C(1) << static_cast<unsigned>(normalized - 'a'))) != 0;
  };
  for (size_t ordinal = planCursor; ordinal != plan->conversions.size();
       ++ordinal) {
    const DynamicScanConversion &conversion = plan->conversions[ordinal];
    char normalized = letter(conversion.specifier);
    if (conversion.suppressed) {
      if ((normalized == 'u' || normalized == 'z') && conversion.width == 0) {
        std::fprintf(stderr,
                     "obelisk: dynamic %s assignment suppression for raw "
                     "%%%c requires an explicit byte count\n",
                     task, static_cast<char>(conversion.specifier));
        return OBELISK_RT_INVALID_ARGUMENT;
      }
      continue;
    }
    if (finalize) {
      std::fprintf(stderr,
                   "obelisk: dynamic %s format has more conversions than "
                   "destinations\n",
                   task);
      return OBELISK_RT_INVALID_ARGUMENT;
    }
    if (!allowed(conversion.specifier)) {
      std::fprintf(stderr,
                   "obelisk: dynamic %s %%%c is incompatible with its "
                   "destination\n",
                   task, static_cast<char>(conversion.specifier));
      return OBELISK_RT_INVALID_ARGUMENT;
    }
    *outPlanCursor = static_cast<uint32_t>(ordinal + 1);
    return OBELISK_RT_OK;
  }
  if (!finalize) {
    std::fprintf(stderr,
                 "obelisk: dynamic %s format has fewer conversions than "
                 "destinations\n",
                 task);
    return OBELISK_RT_INVALID_ARGUMENT;
  }
  *outPlanCursor = static_cast<uint32_t>(plan->conversions.size());
  return OBELISK_RT_OK;
}
