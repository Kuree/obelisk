//===- DPI.cpp - Shared native and bytecode DPI-C boundary ---------------===//

#include "RuntimeInternal.h"
#include "svdpi.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>

namespace {

struct ActiveDpiCall {
  obelisk_rt_context *context = nullptr;
  DpiScopeHandle *scope = nullptr;
  std::string callerFile;
  uint32_t callerLine = 0;
  ActiveDpiCall *previous = nullptr;
};

thread_local ActiveDpiCall *activeDpiCall = nullptr;

struct ActiveCallGuard {
  explicit ActiveCallGuard(ActiveDpiCall &call) : call(call) {
    call.previous = activeDpiCall;
    activeDpiCall = &call;
  }
  ~ActiveCallGuard() { activeDpiCall = call.previous; }
  ActiveDpiCall &call;
};

bool validTimeExponent(int32_t value) {
  return value >= -15 && value <= 0;
}

uint64_t limbCount(uint32_t width) {
  return (uint64_t{width} + 63) / 64;
}

bool validKind(obelisk_rt_design_register_kind kind) {
  return kind == OBELISK_RT_DBREG_BITS ||
         kind == OBELISK_RT_DBREG_LOGIC ||
         kind == OBELISK_RT_DBREG_STATUS;
}

bool validInput(const obelisk_rt_import_input_v1 &input) {
  if (!validKind(input.kind) ||
      (input.flags & ~uint8_t{OBELISK_RT_DBREG_SIGNED}) != 0 ||
      input.reserved != 0 || !input.value)
    return false;
  uint32_t width =
      input.kind == OBELISK_RT_DBREG_STATUS ? 32 : input.bit_width;
  if (width == 0 || input.bit_width != width ||
      input.limb_count != limbCount(width))
    return false;
  return input.kind == OBELISK_RT_DBREG_LOGIC ? input.unknown != nullptr
                                               : input.unknown == nullptr;
}

bool validOutput(const obelisk_rt_import_output_v1 &output) {
  if (!validKind(output.kind) ||
      (output.flags & ~uint8_t{OBELISK_RT_DBREG_SIGNED}) != 0 ||
      output.reserved != 0 || !output.value)
    return false;
  uint32_t width =
      output.kind == OBELISK_RT_DBREG_STATUS ? 32 : output.bit_width;
  if (width == 0 || output.bit_width != width ||
      output.limb_count != limbCount(width))
    return false;
  return output.kind == OBELISK_RT_DBREG_LOGIC ? output.unknown != nullptr
                                                : output.unknown == nullptr;
}

void normalize(obelisk_rt_import_output_v1 &output) {
  uint32_t width =
      output.kind == OBELISK_RT_DBREG_STATUS ? 32 : output.bit_width;
  unsigned tail = width % 64;
  if (tail != 0) {
    uint64_t mask = (uint64_t{1} << tail) - 1;
    output.value[output.limb_count - 1] &= mask;
    if (output.unknown)
      output.unknown[output.limb_count - 1] &= mask;
  }
}

DpiScopeHandle *scopeFromOpaque(ActiveDpiCall *call, const svScope scope) {
  if (!call || !scope)
    return nullptr;
  for (const std::unique_ptr<DpiScopeHandle> &candidate :
       call->context->dpiScopes)
    if (candidate.get() == scope)
      return candidate.get();
  return nullptr;
}

bool powerOfTen(int32_t exponent, uint64_t &value) {
  if (!validTimeExponent(exponent))
    return false;
  value = 1;
  for (int32_t index = exponent; index < 0; ++index)
    value *= 10;
  return true;
}

} // namespace

DpiScopeHandle *obelisk_rt_find_dpi_scope(obelisk_rt_context *context,
                                          uint64_t id) {
  if (!context || id >= context->dpiScopes.size())
    return nullptr;
  return context->dpiScopes[static_cast<size_t>(id)].get();
}

obelisk_rt_status obelisk_rt_initialize_dpi_scopes(
    obelisk_rt_context *context,
    const obelisk_rt_execution_descriptor_v1 *execution) {
  if (!context || !execution)
    return OBELISK_RT_INVALID_ARGUMENT;
  if ((execution->dpi_scopes == nullptr) !=
          (execution->dpi_scope_count == 0) ||
      execution->dpi_reserved != 0)
    return OBELISK_RT_INVALID_DESIGN;
  if (execution->dpi_scope_count == 0)
    return execution->dpi_time_precision == 0 ? OBELISK_RT_OK
                                               : OBELISK_RT_INVALID_DESIGN;
  if (!validTimeExponent(execution->dpi_time_precision) ||
      execution->dpi_scope_count > std::numeric_limits<size_t>::max())
    return OBELISK_RT_INVALID_DESIGN;

  context->dpiScopes.reserve(
      static_cast<size_t>(execution->dpi_scope_count));
  bool sawRoot = false;
  for (uint64_t index = 0; index != execution->dpi_scope_count; ++index) {
    const obelisk_rt_dpi_scope_v1 &record = execution->dpi_scopes[index];
    if (record.id != index || record.reserved != 0 ||
        !validBytes(record.name, record.name_size) || record.name_size == 0 ||
        !validTimeExponent(record.time_unit) ||
        !validTimeExponent(record.time_precision) ||
        record.time_unit < record.time_precision ||
        record.time_precision < execution->dpi_time_precision)
      return OBELISK_RT_INVALID_DESIGN;
    if (record.parent_id == UINT64_MAX) {
      if (sawRoot)
        return OBELISK_RT_INVALID_DESIGN;
      sawRoot = true;
    } else if (record.parent_id >= record.id) {
      return OBELISK_RT_INVALID_DESIGN;
    }
    auto scope = std::make_unique<DpiScopeHandle>();
    scope->context = context;
    scope->id = record.id;
    scope->parentID = record.parent_id;
    scope->name.assign(record.name,
                       static_cast<size_t>(record.name_size));
    scope->timeUnit = record.time_unit;
    scope->timePrecision = record.time_precision;
    if (scope->name.find('\0') != std::string::npos ||
        !context->dpiScopesByName.emplace(scope->name, scope.get()).second)
      return OBELISK_RT_INVALID_DESIGN;
    context->dpiScopes.push_back(std::move(scope));
  }
  return sawRoot ? OBELISK_RT_OK : OBELISK_RT_INVALID_DESIGN;
}

extern "C" obelisk_rt_status obelisk_rt_v1_import_call(
    obelisk_rt_context *context, const obelisk_rt_import_site_v1 *site,
    const obelisk_rt_import_input_v1 *inputs, uint32_t inputCount,
    obelisk_rt_import_output_v1 *outputs, uint32_t outputCount) {
  if (!context || !site ||
      (inputs == nullptr && inputCount != 0) ||
      (outputs == nullptr && outputCount != 0))
    return OBELISK_RT_INVALID_ARGUMENT;
  constexpr uint32_t validFlags = OBELISK_RT_IMPORT_PURE |
                                  OBELISK_RT_IMPORT_CONTEXT |
                                  OBELISK_RT_IMPORT_TASK;
  if (site->version != OBELISK_RT_IMPORT_SITE_VERSION ||
      site->import_id == 0 || site->reserved != 0 ||
      (site->flags & ~validFlags) != 0 ||
      ((site->flags & OBELISK_RT_IMPORT_PURE) != 0 &&
       (site->flags & (OBELISK_RT_IMPORT_CONTEXT |
                       OBELISK_RT_IMPORT_TASK)) != 0) ||
      !validBytes(site->source_file, site->source_file_size))
    return OBELISK_RT_INVALID_ARGUMENT;
  for (uint32_t index = 0; index != inputCount; ++index)
    if (!validInput(inputs[index]))
      return OBELISK_RT_INVALID_ARGUMENT;
  for (uint32_t index = 0; index != outputCount; ++index)
    if (!validOutput(outputs[index]))
      return OBELISK_RT_INVALID_ARGUMENT;

  ContextTransaction transaction(context);
  DpiScopeHandle *scope = nullptr;
  ImportBinding binding;
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (site->scope_id != UINT64_MAX) {
      scope = obelisk_rt_find_dpi_scope(context, site->scope_id);
      if (!scope)
        return OBELISK_RT_INVALID_ARGUMENT;
    }
    auto found = context->imports.find(site->import_id);
    if (found == context->imports.end())
      return OBELISK_RT_TIER_UNAVAILABLE;
    binding = found->second;
  }
  if (binding.abiSignature != 0 &&
      binding.abiSignature != site->abi_signature)
    return OBELISK_RT_ARGUMENT_MISMATCH;

  for (uint32_t index = 0; index != outputCount; ++index) {
    std::fill_n(outputs[index].value, outputs[index].limb_count, uint64_t{0});
    if (outputs[index].unknown)
      std::fill_n(outputs[index].unknown, outputs[index].limb_count,
                  uint64_t{0});
  }

  return guarded(context, [&] {
    ActiveDpiCall call;
    call.context = context;
    call.scope = scope;
    if (site->source_file_size != 0)
      call.callerFile.assign(
          site->source_file, static_cast<size_t>(site->source_file_size));
    call.callerLine = site->source_line;
    ActiveCallGuard active(call);
    obelisk_rt_status status = binding.callback(
        context, site->import_id, inputs, inputCount, outputs, outputCount,
        binding.userData);
    if (status != OBELISK_RT_OK)
      return status;
    for (uint32_t index = 0; index != outputCount; ++index)
      normalize(outputs[index]);
    return OBELISK_RT_OK;
  });
}

extern "C" const char *svDpiVersion(void) { return "1800-2023"; }

extern "C" svScope svGetScope(void) {
  return activeDpiCall ? activeDpiCall->scope : nullptr;
}

extern "C" svScope svSetScope(const svScope scope) {
  if (!activeDpiCall)
    return nullptr;
  DpiScopeHandle *next = scopeFromOpaque(activeDpiCall, scope);
  if (!next)
    return nullptr;
  DpiScopeHandle *previous = activeDpiCall->scope;
  activeDpiCall->scope = next;
  return previous;
}

extern "C" const char *svGetNameFromScope(const svScope scope) {
  DpiScopeHandle *handle = scopeFromOpaque(activeDpiCall, scope);
  return handle ? handle->name.c_str() : nullptr;
}

extern "C" svScope svGetScopeFromName(const char *scopeName) {
  if (!activeDpiCall || !scopeName)
    return nullptr;
  auto found = activeDpiCall->context->dpiScopesByName.find(scopeName);
  return found == activeDpiCall->context->dpiScopesByName.end()
             ? nullptr
             : found->second;
}

extern "C" int svPutUserData(const svScope scope, void *userKey,
                              void *userData) {
  DpiScopeHandle *handle = scopeFromOpaque(activeDpiCall, scope);
  if (!handle || !userKey || !userData)
    return -1;
  std::lock_guard<std::recursive_mutex> lock(handle->context->mutex);
  handle->userData[userKey] = userData;
  return 0;
}

extern "C" void *svGetUserData(const svScope scope, void *userKey) {
  DpiScopeHandle *handle = scopeFromOpaque(activeDpiCall, scope);
  if (!handle || !userKey)
    return nullptr;
  std::lock_guard<std::recursive_mutex> lock(handle->context->mutex);
  auto found = handle->userData.find(userKey);
  return found == handle->userData.end() ? nullptr : found->second;
}

extern "C" int svGetCallerInfo(const char **fileName, int *lineNumber) {
  if (!activeDpiCall || activeDpiCall->callerFile.empty() ||
      activeDpiCall->callerLine == 0 || !fileName || !lineNumber)
    return 0;
  *fileName = activeDpiCall->callerFile.c_str();
  *lineNumber = static_cast<int>(activeDpiCall->callerLine);
  return 1;
}

extern "C" int svIsDisabledState(void) { return 0; }
extern "C" void svAckDisabledState(void) {}

extern "C" int svGetTime(const svScope scope, svTimeVal *time) {
  if (!activeDpiCall || !time)
    return -1;
  int32_t unit = 0;
  if (scope) {
    DpiScopeHandle *handle = scopeFromOpaque(activeDpiCall, scope);
    if (!handle)
      return -1;
    unit = handle->timeUnit;
  } else {
    const obelisk_rt_execution_descriptor_v1 *execution =
        activeDpiCall->context->execution;
    if (!execution)
      return -1;
    unit = execution->dpi_time_precision;
  }
  const obelisk_rt_execution_descriptor_v1 *execution =
      activeDpiCall->context->execution;
  if (!execution)
    return -1;
  uint64_t unitScale = 0, precisionScale = 0;
  if (!powerOfTen(unit, unitScale) ||
      !powerOfTen(execution->dpi_time_precision, precisionScale) ||
      precisionScale < unitScale)
    return -1;
  uint64_t ticks = 0;
  {
    std::lock_guard<std::recursive_mutex> lock(activeDpiCall->context->mutex);
    ticks = activeDpiCall->context->schedulerTime;
  }
  uint64_t scaled = ticks / (precisionScale / unitScale);
  time->type = sv_sim_time;
  time->high = static_cast<uint32_t>(scaled >> 32);
  time->low = static_cast<uint32_t>(scaled);
  time->real = 0.0;
  return 0;
}

extern "C" int svGetTimeUnit(const svScope scope, int32_t *timeUnit) {
  if (!activeDpiCall || !timeUnit)
    return -1;
  if (!scope) {
    const obelisk_rt_execution_descriptor_v1 *execution =
        activeDpiCall->context->execution;
    if (!execution)
      return -1;
    *timeUnit = execution->dpi_time_precision;
    return 0;
  }
  DpiScopeHandle *handle = scopeFromOpaque(activeDpiCall, scope);
  if (!handle)
    return -1;
  *timeUnit = handle->timeUnit;
  return 0;
}

extern "C" int svGetTimePrecision(const svScope scope,
                                   int32_t *timePrecision) {
  if (!activeDpiCall || !timePrecision)
    return -1;
  if (!scope) {
    const obelisk_rt_execution_descriptor_v1 *execution =
        activeDpiCall->context->execution;
    if (!execution)
      return -1;
    *timePrecision = execution->dpi_time_precision;
    return 0;
  }
  DpiScopeHandle *handle = scopeFromOpaque(activeDpiCall, scope);
  if (!handle)
    return -1;
  *timePrecision = handle->timePrecision;
  return 0;
}
