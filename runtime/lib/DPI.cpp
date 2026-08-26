//===- DPI.cpp - Shared native and bytecode DPI-C boundary ---------------===//

#include "RuntimeInternal.h"
#include "svdpi.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>

thread_local ActiveDpiCall *activeDpiCall = nullptr;

namespace {

struct ActiveCallGuard {
  explicit ActiveCallGuard(ActiveDpiCall &call) : call(call) {
    call.previous = activeDpiCall;
    activeDpiCall = &call;
  }
  ~ActiveCallGuard() { activeDpiCall = call.previous; }
  ActiveDpiCall &call;
};

bool validTimeExponent(int32_t value) { return value >= -15 && value <= 2; }

uint64_t limbCount(uint32_t width) { return (uint64_t{width} + 63) / 64; }

bool validKind(obelisk_rt_design_register_kind kind) {
  return kind == OBELISK_RT_DBREG_BITS || kind == OBELISK_RT_DBREG_LOGIC ||
         kind == OBELISK_RT_DBREG_STATUS || kind == OBELISK_RT_DBREG_STRING ||
         kind == OBELISK_RT_DBREG_REAL32 || kind == OBELISK_RT_DBREG_REAL64;
}

bool validReal(obelisk_rt_design_register_kind kind, uint8_t flags,
               uint32_t width, const uint64_t *unknown) {
  if (kind == OBELISK_RT_DBREG_REAL32)
    return flags == 0 && width == 32 && unknown == nullptr;
  if (kind == OBELISK_RT_DBREG_REAL64)
    return flags == 0 && width == 64 && unknown == nullptr;
  return false;
}

bool validStringWord(const uint64_t *value) {
  char scratch[8];
  const char *bytes = nullptr;
  uint64_t size = 0;
  return value && obelisk_rt_v1_string_view(*value, scratch, &bytes, &size) ==
                      OBELISK_RT_OK;
}

bool validInput(const obelisk_rt_import_input_v1 &input) {
  if (!validKind(input.kind) ||
      (input.flags & ~uint8_t{OBELISK_RT_DBREG_SIGNED}) != 0 ||
      input.reserved != 0 || !input.value)
    return false;
  uint32_t width = input.kind == OBELISK_RT_DBREG_STATUS ? 32 : input.bit_width;
  if (width == 0 || input.bit_width != width ||
      input.limb_count != limbCount(width))
    return false;
  if (input.kind == OBELISK_RT_DBREG_STRING)
    return input.flags == 0 && input.bit_width == 64 &&
           input.unknown == nullptr && validStringWord(input.value);
  if (input.kind == OBELISK_RT_DBREG_REAL32 ||
      input.kind == OBELISK_RT_DBREG_REAL64)
    return validReal(input.kind, input.flags, input.bit_width, input.unknown);
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
  if (output.kind == OBELISK_RT_DBREG_STRING)
    return output.flags == 0 && output.bit_width == 64 &&
           output.unknown == nullptr;
  if (output.kind == OBELISK_RT_DBREG_REAL32 ||
      output.kind == OBELISK_RT_DBREG_REAL64)
    return validReal(output.kind, output.flags, output.bit_width,
                     output.unknown);
  return output.kind == OBELISK_RT_DBREG_LOGIC ? output.unknown != nullptr
                                               : output.unknown == nullptr;
}

void normalize(obelisk_rt_import_output_v1 &output) {
  if (output.kind == OBELISK_RT_DBREG_STRING ||
      output.kind == OBELISK_RT_DBREG_REAL32 ||
      output.kind == OBELISK_RT_DBREG_REAL64)
    return;
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

bool timeScaleRatio(int32_t unit, int32_t precision, uint64_t &value) {
  if (!validTimeExponent(unit) || !validTimeExponent(precision) ||
      unit < precision)
    return false;
  value = 1;
  for (int32_t index = precision; index < unit; ++index)
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
  if ((execution->dpi_scopes == nullptr) != (execution->dpi_scope_count == 0) ||
      execution->dpi_reserved != 0)
    return OBELISK_RT_INVALID_DESIGN;
  if (execution->dpi_scope_count == 0)
    return execution->dpi_time_precision == 0 ? OBELISK_RT_OK
                                              : OBELISK_RT_INVALID_DESIGN;
  if (!validTimeExponent(execution->dpi_time_precision) ||
      execution->dpi_scope_count > std::numeric_limits<size_t>::max())
    return OBELISK_RT_INVALID_DESIGN;

  context->dpiScopes.reserve(static_cast<size_t>(execution->dpi_scope_count));
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
    scope->name.assign(record.name, static_cast<size_t>(record.name_size));
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
  if (!context || !site || (inputs == nullptr && inputCount != 0) ||
      (outputs == nullptr && outputCount != 0))
    return OBELISK_RT_INVALID_ARGUMENT;
  constexpr uint32_t validFlags = OBELISK_RT_IMPORT_PURE |
                                  OBELISK_RT_IMPORT_CONTEXT |
                                  OBELISK_RT_IMPORT_TASK;
  if (site->version != OBELISK_RT_VERSION || site->import_id == 0 ||
      site->reserved != 0 || (site->flags & ~validFlags) != 0 ||
      ((site->flags & OBELISK_RT_IMPORT_PURE) != 0 &&
       (site->flags & (OBELISK_RT_IMPORT_CONTEXT | OBELISK_RT_IMPORT_TASK)) !=
           0) ||
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
  if (binding.abiSignature != 0 && binding.abiSignature != site->abi_signature)
    return OBELISK_RT_ARGUMENT_MISMATCH;

  for (uint32_t index = 0; index != outputCount; ++index) {
    if (outputs[index].kind == OBELISK_RT_DBREG_REAL32 ||
        outputs[index].kind == OBELISK_RT_DBREG_REAL64)
      std::memset(outputs[index].value, 0, outputs[index].bit_width / 8);
    else
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
      call.callerFile.assign(site->source_file,
                             static_cast<size_t>(site->source_file_size));
    call.callerLine = site->source_line;
    ActiveCallGuard active(call);
    obelisk_rt_status status =
        binding.callback(context, site->import_id, inputs, inputCount, outputs,
                         outputCount, binding.userData);
    if (status != OBELISK_RT_OK)
      return status;
    if (call.exportStatus != OBELISK_RT_OK)
      return call.exportStatus;
    for (uint32_t index = 0; index != outputCount; ++index) {
      if (outputs[index].kind == OBELISK_RT_DBREG_STRING &&
          !validStringWord(outputs[index].value))
        return OBELISK_RT_INVALID_HANDLE;
      normalize(outputs[index]);
    }
    return OBELISK_RT_OK;
  });
}

extern "C" obelisk_rt_status
obelisk_rt_v1_dpi_string_copy(obelisk_rt_context *context, const char *string,
                              obelisk_rt_string_v1 *outString) {
  if (!context || !string || !outString)
    return OBELISK_RT_INVALID_ARGUMENT;
  obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
  if (!lane)
    return OBELISK_RT_INVALID_LIFECYCLE;
  return obelisk_rt_v1_string_create(lane, string, std::strlen(string),
                                     outString);
}

extern "C" const char *svDpiVersion(void) { return "1800-2005"; }

extern "C" svBit svGetBitselBit(const svBitVecVal *source, int index) {
  if (!source || index < 0)
    return sv_0;
  return static_cast<svBit>((source[static_cast<unsigned>(index) / 32] >>
                             (static_cast<unsigned>(index) % 32)) &
                            1U);
}

extern "C" svLogic svGetBitselLogic(const svLogicVecVal *source, int index) {
  if (!source || index < 0)
    return sv_0;
  unsigned word = static_cast<unsigned>(index) / 32;
  unsigned bit = static_cast<unsigned>(index) % 32;
  return static_cast<svLogic>(((source[word].aval >> bit) & 1U) |
                              (((source[word].bval >> bit) & 1U) << 1));
}

extern "C" void svPutBitselBit(svBitVecVal *destination, int index,
                                 svBit source) {
  if (!destination || index < 0)
    return;
  unsigned word = static_cast<unsigned>(index) / 32;
  unsigned bit = static_cast<unsigned>(index) % 32;
  uint32_t mask = uint32_t{1} << bit;
  destination[word] = (destination[word] & ~mask) |
                      (static_cast<uint32_t>(source & 1U) << bit);
}

extern "C" void svPutBitselLogic(svLogicVecVal *destination, int index,
                                   svLogic source) {
  if (!destination || index < 0)
    return;
  unsigned word = static_cast<unsigned>(index) / 32;
  unsigned bit = static_cast<unsigned>(index) % 32;
  uint32_t mask = uint32_t{1} << bit;
  destination[word].aval =
      (destination[word].aval & ~mask) |
      (static_cast<uint32_t>(source & 1U) << bit);
  destination[word].bval =
      (destination[word].bval & ~mask) |
      (static_cast<uint32_t>((source >> 1) & 1U) << bit);
}

extern "C" void svGetPartselBit(svBitVecVal *destination,
                                  const svBitVecVal *source, int index,
                                  int width) {
  if (!destination)
    return;
  *destination = 0;
  if (!source || index < 0 || width <= 0 || width > 32)
    return;
  for (int bit = 0; bit != width; ++bit)
    *destination |= static_cast<uint32_t>(svGetBitselBit(source, index + bit))
                    << bit;
}

extern "C" void svGetPartselLogic(svLogicVecVal *destination,
                                    const svLogicVecVal *source, int index,
                                    int width) {
  if (!destination)
    return;
  destination->aval = 0;
  destination->bval = 0;
  if (!source || index < 0 || width <= 0 || width > 32)
    return;
  for (int bit = 0; bit != width; ++bit) {
    svLogic value = svGetBitselLogic(source, index + bit);
    destination->aval |= static_cast<uint32_t>(value & 1U) << bit;
    destination->bval |= static_cast<uint32_t>((value >> 1) & 1U) << bit;
  }
}

extern "C" void svPutPartselBit(svBitVecVal *destination,
                                  const svBitVecVal source, int index,
                                  int width) {
  if (!destination || index < 0 || width <= 0 || width > 32)
    return;
  for (int bit = 0; bit != width; ++bit)
    svPutBitselBit(destination, index + bit,
                   static_cast<svBit>((source >> bit) & 1U));
}

extern "C" void svPutPartselLogic(svLogicVecVal *destination,
                                    const svLogicVecVal source, int index,
                                    int width) {
  if (!destination || index < 0 || width <= 0 || width > 32)
    return;
  for (int bit = 0; bit != width; ++bit) {
    svLogic value = static_cast<svLogic>(((source.aval >> bit) & 1U) |
                                         (((source.bval >> bit) & 1U) << 1));
    svPutBitselLogic(destination, index + bit, value);
  }
}

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
  return found == activeDpiCall->context->dpiScopesByName.end() ? nullptr
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
  uint64_t precisionTicksPerUnit = 0;
  if (!timeScaleRatio(unit, execution->dpi_time_precision,
                      precisionTicksPerUnit))
    return -1;
  uint64_t ticks = 0;
  {
    std::lock_guard<std::recursive_mutex> lock(activeDpiCall->context->mutex);
    ticks = activeDpiCall->context->schedulerTime;
  }
  uint64_t scaled = ticks / precisionTicksPerUnit;
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

extern "C" int svGetTimePrecision(const svScope scope, int32_t *timePrecision) {
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
