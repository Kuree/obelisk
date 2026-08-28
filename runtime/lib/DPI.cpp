//===- DPI.cpp - Shared native and bytecode DPI-C boundary ---------------===//

#include "RuntimeInternal.h"
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__CYGWIN__)
#define DPI_DLLISPEC __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define DPI_EXTERN __attribute__((visibility("default")))
#endif
#include "svdpi.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>

thread_local ActiveDpiCall *activeDpiCall = nullptr;

extern "C" obelisk_rt_context *obelisk_rt_v1_dpi_current_context(void) {
  return activeDpiCall ? activeDpiCall->context : nullptr;
}

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
         kind == OBELISK_RT_DBREG_REAL32 || kind == OBELISK_RT_DBREG_REAL64 ||
         kind == OBELISK_RT_DBREG_OPEN_ARRAY ||
         kind == OBELISK_RT_DBREG_AGGREGATE;
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
  if (input.kind == OBELISK_RT_DBREG_OPEN_ARRAY ||
      input.kind == OBELISK_RT_DBREG_AGGREGATE)
    return input.flags == 0 && input.bit_width == 64 && input.limb_count == 1 &&
           input.unknown == nullptr;
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
  if (output.kind == OBELISK_RT_DBREG_OPEN_ARRAY ||
      output.kind == OBELISK_RT_DBREG_AGGREGATE)
    return output.flags == 0 && output.bit_width == 64 &&
           output.limb_count == 1 && output.unknown == nullptr;
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
      output.kind == OBELISK_RT_DBREG_REAL64 ||
      output.kind == OBELISK_RT_DBREG_OPEN_ARRAY ||
      output.kind == OBELISK_RT_DBREG_AGGREGATE)
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

static obelisk_rt_status importCallImpl(
    obelisk_rt_context *context, const obelisk_rt_import_site_v1 *site,
    const obelisk_rt_import_input_v1 *inputs, uint32_t inputCount,
    obelisk_rt_import_output_v1 *outputs, uint32_t outputCount,
    bool exposeContext) {
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
      exposeContext !=
          ((site->flags & OBELISK_RT_IMPORT_CONTEXT) != 0) ||
      !validBytes(site->source_file, site->source_file_size))
    return OBELISK_RT_INVALID_ARGUMENT;
  for (uint32_t index = 0; index != inputCount; ++index)
    if (!validInput(inputs[index]))
      return OBELISK_RT_INVALID_ARGUMENT;
  for (uint32_t index = 0; index != outputCount; ++index)
    if (!validOutput(outputs[index]))
      return OBELISK_RT_INVALID_ARGUMENT;

  DpiScopeHandle *scope = nullptr;
  ImportBinding binding;
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (exposeContext && site->scope_id != UINT64_MAX) {
      scope = obelisk_rt_find_dpi_scope(context, site->scope_id);
      if (!scope)
        return OBELISK_RT_INVALID_ARGUMENT;
    }
    auto found = context->imports.find(site->import_id);
    if (found == context->imports.end())
      return OBELISK_RT_TIER_UNAVAILABLE;
    binding = found->second;
  }
  if (binding.abiSignature != 0 && site->abi_signature != 0 &&
      binding.abiSignature != site->abi_signature)
    return OBELISK_RT_ARGUMENT_MISMATCH;

  for (uint32_t index = 0; index != outputCount; ++index) {
    if (outputs[index].kind == OBELISK_RT_DBREG_OPEN_ARRAY ||
        outputs[index].kind == OBELISK_RT_DBREG_AGGREGATE)
      continue;
    if (outputs[index].kind == OBELISK_RT_DBREG_REAL32 ||
        outputs[index].kind == OBELISK_RT_DBREG_REAL64)
      std::memset(outputs[index].value, 0, outputs[index].bit_width / 8);
    else
      std::fill_n(outputs[index].value, outputs[index].limb_count, uint64_t{0});
    if (outputs[index].unknown)
      std::fill_n(outputs[index].unknown, outputs[index].limb_count,
                  uint64_t{0});
  }

  auto finishOutputs = [&](obelisk_rt_status status) {
    if (status != OBELISK_RT_OK)
      return status;
    for (uint32_t index = 0; index != outputCount; ++index) {
      if (outputs[index].kind == OBELISK_RT_DBREG_STRING &&
          !validStringWord(outputs[index].value))
        return OBELISK_RT_INVALID_HANDLE;
      normalize(outputs[index]);
    }
    return OBELISK_RT_OK;
  };

  // A non-context import cannot call an exported SystemVerilog subroutine or
  // use the svScope / caller-info API. Keep that common native boundary free
  // of ActiveDpiCall construction, source-string allocation, and TLS writes.
  if (!exposeContext)
    return guarded(context, [&] {
      obelisk_rt_status status =
          binding.callback(context, site->import_id, inputs, inputCount,
                           outputs, outputCount, binding.userData);
      if (status == OBELISK_RT_DPI_DISABLE_UNSUPPORTED)
        return OBELISK_RT_FATAL;
      return finishOutputs(status);
    });

  ContextTransaction transaction(context);
  return guarded(context, [&] {
    ActiveDpiCall call;
    call.context = context;
    call.scope = scope;
    call.importFlags = site->flags;
    if (site->source_file_size != 0)
      call.callerFile.assign(site->source_file,
                             static_cast<size_t>(site->source_file_size));
    call.callerLine = site->source_line;
    ActiveCallGuard active(call);
    obelisk_rt_status status =
        binding.callback(context, site->import_id, inputs, inputCount, outputs,
                         outputCount, binding.userData);
    bool task = (site->flags & OBELISK_RT_IMPORT_TASK) != 0;
    bool returnedDisabled = status == OBELISK_RT_DPI_DISABLE_UNSUPPORTED;
    if (call.disabledState) {
      if (task) {
        if (!returnedDisabled)
          return OBELISK_RT_FATAL;
      } else {
        if (status != OBELISK_RT_OK || !call.disableAcknowledged)
          return OBELISK_RT_FATAL;
        return OBELISK_RT_DPI_DISABLE_UNSUPPORTED;
      }
    } else if (returnedDisabled) {
      return OBELISK_RT_FATAL;
    }
    if (status != OBELISK_RT_OK)
      return status;
    if (call.exportStatus != OBELISK_RT_OK)
      return call.exportStatus;
    return finishOutputs(OBELISK_RT_OK);
  });
}

extern "C" obelisk_rt_status obelisk_rt_v1_import_call(
    obelisk_rt_context *context, const obelisk_rt_import_site_v1 *site,
    const obelisk_rt_import_input_v1 *inputs, uint32_t inputCount,
    obelisk_rt_import_output_v1 *outputs, uint32_t outputCount) {
  if (!site)
    return OBELISK_RT_INVALID_ARGUMENT;
  return importCallImpl(context, site, inputs, inputCount, outputs, outputCount,
                        (site->flags & OBELISK_RT_IMPORT_CONTEXT) != 0);
}

extern "C" obelisk_rt_status obelisk_rt_v1_import_call_noncontext(
    obelisk_rt_context *context, const obelisk_rt_import_site_v1 *site,
    const obelisk_rt_import_input_v1 *inputs, uint32_t inputCount,
    obelisk_rt_import_output_v1 *outputs, uint32_t outputCount) {
  return importCallImpl(context, site, inputs, inputCount, outputs, outputCount,
                        false);
}

extern "C" obelisk_rt_status obelisk_rt_v1_import_call_guarded(
    obelisk_rt_status priorStatus, obelisk_rt_context *context,
    const obelisk_rt_import_site_v1 *site,
    const obelisk_rt_import_input_v1 *inputs, uint32_t inputCount,
    obelisk_rt_import_output_v1 *outputs, uint32_t outputCount) {
  if (priorStatus != OBELISK_RT_OK)
    return priorStatus;
  return obelisk_rt_v1_import_call(context, site, inputs, inputCount, outputs,
                                   outputCount);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_import_call_noncontext_guarded(
    obelisk_rt_status priorStatus, obelisk_rt_context *context,
    const obelisk_rt_import_site_v1 *site,
    const obelisk_rt_import_input_v1 *inputs, uint32_t inputCount,
    obelisk_rt_import_output_v1 *outputs, uint32_t outputCount) {
  if (priorStatus != OBELISK_RT_OK)
    return priorStatus;
  return obelisk_rt_v1_import_call_noncontext(
      context, site, inputs, inputCount, outputs, outputCount);
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
  destination[word] =
      (destination[word] & ~mask) | (static_cast<uint32_t>(source & 1U) << bit);
}

extern "C" void svPutBitselLogic(svLogicVecVal *destination, int index,
                                 svLogic source) {
  if (!destination || index < 0)
    return;
  unsigned word = static_cast<unsigned>(index) / 32;
  unsigned bit = static_cast<unsigned>(index) % 32;
  uint32_t mask = uint32_t{1} << bit;
  destination[word].aval = (destination[word].aval & ~mask) |
                           (static_cast<uint32_t>(source & 1U) << bit);
  destination[word].bval = (destination[word].bval & ~mask) |
                           (static_cast<uint32_t>((source >> 1) & 1U) << bit);
}

extern "C" void svGetPartselBit(svBitVecVal *destination,
                                const svBitVecVal *source, int index,
                                int width) {
  if (!destination)
    return;
  if (!source || index < 0 || width <= 0 || width > 32)
    return;
  uint32_t selected = 0;
  for (int bit = 0; bit != width; ++bit)
    selected |= static_cast<uint32_t>(svGetBitselBit(source, index + bit))
                << bit;
  uint32_t mask = width == 32 ? UINT32_MAX : (uint32_t{1} << width) - 1;
  *destination = (*destination & ~mask) | selected;
}

extern "C" void svGetPartselLogic(svLogicVecVal *destination,
                                  const svLogicVecVal *source, int index,
                                  int width) {
  if (!destination)
    return;
  if (!source || index < 0 || width <= 0 || width > 32)
    return;
  uint32_t aval = 0;
  uint32_t bval = 0;
  for (int bit = 0; bit != width; ++bit) {
    svLogic value = svGetBitselLogic(source, index + bit);
    aval |= static_cast<uint32_t>(value & 1U) << bit;
    bval |= static_cast<uint32_t>((value >> 1) & 1U) << bit;
  }
  uint32_t mask = width == 32 ? UINT32_MAX : (uint32_t{1} << width) - 1;
  destination->aval = (destination->aval & ~mask) | aval;
  destination->bval = (destination->bval & ~mask) | bval;
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

extern "C" int svIsDisabledState(void) {
  return activeDpiCall && activeDpiCall->disabledState &&
                 !activeDpiCall->disableAcknowledged
             ? 1
             : 0;
}
extern "C" void svAckDisabledState(void) {
  if (activeDpiCall && activeDpiCall->disabledState)
    activeDpiCall->disableAcknowledged = true;
}
