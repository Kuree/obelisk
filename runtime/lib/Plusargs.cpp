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

#include <string>
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

bool splitValueFormat(std::string_view format, std::string &prefix,
                      uint32_t &conversion) {
  size_t percent = format.rfind('%');
  if (percent == std::string_view::npos)
    return false;
  size_t specifier = percent + 1;
  while (specifier < format.size() && format[specifier] == '0')
    ++specifier;
  if (specifier + 1 != format.size())
    return false;
  switch (format[specifier]) {
  case 'b':
  case 'B':
    conversion = 2;
    break;
  case 'o':
  case 'O':
    conversion = 8;
    break;
  case 'd':
  case 'D':
    conversion = 10;
    break;
  case 'h':
  case 'H':
  case 'x':
  case 'X':
    conversion = 16;
    break;
  case 'e':
  case 'E':
  case 'f':
  case 'F':
  case 'g':
  case 'G':
    conversion = 1;
    break;
  case 's':
  case 'S':
    conversion = 0;
    break;
  default:
    return false;
  }

  prefix.clear();
  prefix.reserve(percent);
  for (size_t index = 0; index < percent; ++index) {
    if (format[index] == '%' && index + 1 < percent &&
        format[index + 1] == '%')
      ++index;
    prefix.push_back(format[index]);
  }
  return true;
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

extern "C" obelisk_rt_status obelisk_rt_v1_plusarg_scan(
    obelisk_rt_context *context, obelisk_rt_gc_lane_v1 *lane,
    obelisk_rt_string_v1 format, obelisk_rt_string_v1 *outTail,
    uint32_t *outConversion, uint32_t *outFound) {
  if (!context || !lane || !outTail || !outConversion || !outFound)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outTail = 0;
  *outConversion = 0;
  *outFound = 0;
  char scratch[8] = {};
  const char *bytes = nullptr;
  uint64_t size = 0;
  obelisk_rt_status status =
      obelisk_rt_v1_string_view(format, scratch, &bytes, &size);
  if (status != OBELISK_RT_OK)
    return status;
  std::string prefix;
  if (!splitValueFormat(std::string_view(bytes, size), prefix, *outConversion))
    return OBELISK_RT_OK;

  std::string tail;
  status = guarded(context, [&] {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    const std::string *match = findPlusarg(context, prefix);
    if (!match)
      return OBELISK_RT_OK;
    tail = match->substr(prefix.size());
    *outFound = 1;
    return OBELISK_RT_OK;
  });
  if (status != OBELISK_RT_OK || !*outFound)
    return status;
  return obelisk_rt_v1_string_create(lane, tail.data(), tail.size(), outTail);
}
