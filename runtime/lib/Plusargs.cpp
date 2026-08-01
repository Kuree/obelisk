//===- Plusargs.cpp - Command-line plusarg queries (IEEE 1800 21.6) -------===//
//
// The generated executable hands its argv to the context at startup, which
// keeps every '+'-introduced argument with that prefix stripped. Both queries
// below match a caller-supplied prefix against those entries in the order they
// were given, which is what $test$plusargs and $value$plusargs are specified
// to do.
//
//===---------------------------------------------------------------------===//

#include "RuntimeInternal.h"

#include <string_view>

namespace {

// The matched argument, or nullptr when no plusarg starts with `prefix`.
const std::string *findPlusarg(obelisk_rt_context *context,
                               std::string_view prefix) {
  for (const std::string &argument : context->plusargs)
    if (std::string_view(argument).substr(0, prefix.size()) == prefix)
      return &argument;
  return nullptr;
}

} // namespace

extern "C" obelisk_rt_status
obelisk_rt_v1_plusarg_test(obelisk_rt_context *context,
                           obelisk_rt_string_v1 name, uint32_t *outFound) {
  if (!context || !outFound)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outFound = 0;
  char scratch[8] = {};
  const char *bytes = nullptr;
  uint64_t size = 0;
  obelisk_rt_status status =
      obelisk_rt_v1_string_view(name, scratch, &bytes, &size);
  if (status != OBELISK_RT_OK)
    return status;
  return guarded(context, [&] {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    *outFound =
        findPlusarg(context, std::string_view(bytes, size)) ? 1u : 0u;
    return OBELISK_RT_OK;
  });
}

extern "C" obelisk_rt_status obelisk_rt_v1_plusarg_value(
    obelisk_rt_context *context, obelisk_rt_gc_lane_v1 *lane,
    obelisk_rt_string_v1 prefix, obelisk_rt_string_v1 *outTail,
    uint32_t *outFound) {
  if (!context || !outTail || !outFound)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outTail = 0;
  *outFound = 0;
  char scratch[8] = {};
  const char *bytes = nullptr;
  uint64_t size = 0;
  obelisk_rt_status status =
      obelisk_rt_v1_string_view(prefix, scratch, &bytes, &size);
  if (status != OBELISK_RT_OK)
    return status;
  std::string tail;
  status = guarded(context, [&] {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    const std::string *match =
        findPlusarg(context, std::string_view(bytes, size));
    if (!match)
      return OBELISK_RT_OK;
    tail = match->substr(static_cast<size_t>(size));
    *outFound = 1;
    return OBELISK_RT_OK;
  });
  if (status != OBELISK_RT_OK || !*outFound)
    return status;
  return obelisk_rt_v1_string_create(lane, tail.data(), tail.size(), outTail);
}
