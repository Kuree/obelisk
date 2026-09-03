//===- VPI.cpp - Single-context IEEE VPI compatibility shim ---------------===//

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "RuntimeInternal.h"

#include "VPIInternal.h"
#include "obelisk/Reflection/VPIObjectModel.h"

// VPI startup loads a caller-provided shared object and reads the ELF symbol
// size of its vlog_startup_routines table. WebAssembly has neither, and a
// wasm build links its design statically, so that path is compiled out there
// rather than emulated. Everything else in this file is portable.
#if !defined(__EMSCRIPTEN__)
#define OBELISK_RT_VPI_DYNAMIC_STARTUP 1
#include <dlfcn.h>
#include <elf.h>
#endif

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define OBELISK_VPI_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define OBELISK_VPI_EXPORT __attribute__((visibility("default")))
#else
#define OBELISK_VPI_EXPORT
#endif

namespace {

struct VPIState;

enum class VPIHandleKind : uint8_t {
  Object,
  Iterator,
  Callback,
  ScheduledEvent,
  SystemTf
};

} // namespace

struct __vpiHandle {
  VPIState *owner = nullptr;
  bool alive = true;
  VPIHandleKind kind = VPIHandleKind::Object;
  obelisk_rt_design_cursor_v1 cursor{};
  uint32_t exactVpiType = 0;
  bool statement = false;
  bool hasInfo = false;
  obelisk_rt_design_info_v1 info{};
  // Callback iteration snapshots registrations because callbacks may remove
  // peers during dispatch. Immutable design iterators use cursors or relation
  // indices directly and never populate this vector.
  std::vector<obelisk_rt_design_cursor_v1> items;
  bool callbackIterator = false;
  bool designIterator = false;
  bool relationIterator = false;
  obelisk::reflection::VPIObjectSetID requestedTargets{};
  VPIRelationRange relationRange{};
  size_t next = 0;
  std::string scratch;
  std::vector<s_vpi_vecval> vectorScratch;
  std::vector<uint64_t> valueScratch;
  std::vector<uint64_t> unknownScratch;
  void *userData = nullptr;
};

namespace {

enum class VPIPhase : uint8_t {
  StartupRestricted,
  BeforeEndCompile,
  EndCompile,
  BeforeStartSimulation,
  StartSimulation,
  Running,
  EndSimulation,
  Ended
};

struct VPICallback {
  uint64_t id = 0;
  PLI_INT32 reason = 0;
  PLI_INT32 (*routine)(p_cb_data) = nullptr;
  PLI_BYTE8 *userData = nullptr;
};

struct VPIState {
  obelisk_rt_context *context = nullptr;
  std::unordered_map<__vpiHandle *, std::unique_ptr<__vpiHandle>> handles;
  std::string errorMessage;
  std::string errorCode;
  int errorLevel = 0;
  bool unsupportedStartup = false;
  VPIPhase phase = VPIPhase::StartupRestricted;
  uint64_t nextCallbackId = 1;
  std::unordered_map<uint64_t, VPICallback> callbacks;
  std::vector<uint64_t> callbackOrder;
  // IEEE temporary results are invalidated by the next routine call of the
  // same family, irrespective of which object handle was used.
  std::string propertyStringScratch;
  std::string valueStringScratch;
  std::vector<s_vpi_vecval> vectorScratch;
};

// VPI is callable only from the simulation thread (startup, callbacks, and
// reentrant DPI on that thread).  A process-global binding would accidentally
// authorize debugger transport/signal threads and race callback scratch state.
thread_local VPIState *activeState = nullptr;

void setError(VPIState *state, const char *message, int level = vpiError,
              const char *code = "OBELISK_VPI") {
  if (!state)
    return;
  OBELISK_RT_TRY {
    state->errorMessage = message ? message : "VPI error";
    state->errorCode = code;
    state->errorLevel = level;
  }
  OBELISK_RT_CATCH_ALL {}
}

VPIState *requireState() {
  if (!activeState)
    return nullptr;
  if (activeState->phase == VPIPhase::StartupRestricted ||
      activeState->phase == VPIPhase::BeforeEndCompile) {
    setError(activeState,
             "only callback and system-task registration is allowed during "
             "VPI startup");
    return nullptr;
  }
  return activeState;
}

__vpiHandle *findHandle(vpiHandle opaque) {
  VPIState *state = requireState();
  auto *handle = reinterpret_cast<__vpiHandle *>(opaque);
  if (!state || !handle ||
      state->handles.find(handle) == state->handles.end() ||
      handle->owner != state || !handle->alive) {
    setError(state, "invalid or released VPI handle");
    return nullptr;
  }
  return handle;
}

__vpiHandle *validate(vpiHandle opaque,
                      VPIHandleKind kind = VPIHandleKind::Object) {
  __vpiHandle *handle = findHandle(opaque);
  if (!handle || handle->kind != kind) {
    setError(requireState(), "wrong-kind VPI handle");
    return nullptr;
  }
  return handle;
}

vpiHandle keepHandle(VPIState *state, std::unique_ptr<__vpiHandle> handle) {
  __vpiHandle *result = handle.get();
  state->handles.try_emplace(result, std::move(handle));
  return reinterpret_cast<vpiHandle>(result);
}

vpiHandle makeHandle(VPIState *state, obelisk_rt_design_cursor_v1 cursor,
                     uint32_t exactVpiType = 0, bool statement = false) {
  OBELISK_RT_TRY {
    auto handle = std::make_unique<__vpiHandle>();
    handle->owner = state;
    handle->kind = VPIHandleKind::Object;
    handle->cursor = cursor;
    handle->exactVpiType = exactVpiType;
    handle->statement = statement;
    return keepHandle(state, std::move(handle));
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    setError(state, "VPI handle arena is out of memory", vpiSystem);
    return nullptr;
  }
  OBELISK_RT_CATCH_ALL {
    setError(state, "could not allocate VPI handle", vpiInternal);
    return nullptr;
  }
}

vpiHandle makeCallbackHandle(VPIState *state, uint64_t callbackId) {
  OBELISK_RT_TRY {
    auto handle = std::make_unique<__vpiHandle>();
    handle->owner = state;
    handle->kind = VPIHandleKind::Callback;
    handle->cursor.offset = callbackId;
    return keepHandle(state, std::move(handle));
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    setError(state, "VPI callback handle arena is out of memory", vpiSystem);
    return nullptr;
  }
  OBELISK_RT_CATCH_ALL {
    setError(state, "could not allocate VPI callback handle", vpiInternal);
    return nullptr;
  }
}

VPICallback *findCallback(__vpiHandle *handle) {
  if (!handle || handle->kind != VPIHandleKind::Callback)
    return nullptr;
  auto found = handle->owner->callbacks.find(handle->cursor.offset);
  if (found == handle->owner->callbacks.end()) {
    setError(handle->owner, "VPI callback registration no longer exists");
    return nullptr;
  }
  return &found->second;
}

bool isLifecycleReason(PLI_INT32 reason) {
  return reason == cbEndOfCompile || reason == cbStartOfSimulation ||
         reason == cbEndOfSimulation;
}

obelisk_rt_status dispatchLifecycle(VPIState *state, PLI_INT32 reason) {
  // A fixed frontier makes registration from a callback eligible only for a
  // later event. Each record is looked up again immediately before invocation
  // so removing a callback that has not run yet suppresses it safely.
  std::vector<uint64_t> scheduled;
  OBELISK_RT_TRY { scheduled = state->callbackOrder; }
  OBELISK_RT_CATCH_ALL {
    setError(state, "could not snapshot VPI callbacks", vpiSystem);
    return OBELISK_RT_OUT_OF_MEMORY;
  }
  obelisk_rt_status status = OBELISK_RT_OK;
  for (uint64_t id : scheduled) {
    auto found = state->callbacks.find(id);
    if (found == state->callbacks.end() || found->second.reason != reason)
      continue;
    auto routine = found->second.routine;
    auto *userData = found->second.userData;
    s_cb_data invocation{};
    invocation.reason = reason;
    invocation.cb_rtn = routine;
    invocation.user_data = userData;
    OBELISK_RT_TRY { (void)routine(&invocation); }
    OBELISK_RT_CATCH_ALL {
      setError(state, "VPI callback raised an exception", vpiInternal);
      status = OBELISK_RT_FATAL;
    }
  }
  return status;
}

PLI_INT32 removeCallbackHandle(VPIState *state, __vpiHandle *handle) {
  if (!state || !handle || handle->owner != state || !handle->alive ||
      handle->kind != VPIHandleKind::Callback) {
    setError(state, "invalid VPI callback handle");
    return 0;
  }
  auto found = state->callbacks.find(handle->cursor.offset);
  if (found == state->callbacks.end()) {
    setError(state, "VPI callback was already removed");
    return 0;
  }
  const uint64_t id = handle->cursor.offset;
  state->callbacks.erase(found);
  state->callbackOrder.erase(
      std::remove(state->callbackOrder.begin(), state->callbackOrder.end(), id),
      state->callbackOrder.end());
  // The callback object no longer exists, so invalidate every equivalent
  // handle, including handles returned by callback iteration.
  for (auto &entry : state->handles)
    if (entry.second->kind == VPIHandleKind::Callback &&
        entry.second->cursor.offset == id)
      entry.second->alive = false;
  return 1;
}

bool infoFor(__vpiHandle *handle, obelisk_rt_design_info_v1 &info) {
  if (!handle->hasInfo) {
    obelisk_rt_status status = obelisk_rt_cached_design_info(
        handle->owner->context, handle->cursor, &handle->info);
    if (status != OBELISK_RT_OK) {
      setError(handle->owner, "design metadata lookup failed");
      return false;
    }
    handle->hasInfo = true;
  }
  info = handle->info;
  return true;
}

int vpiTypeFor(uint32_t kind) {
  switch (kind) {
  case OBELISK_RT_DESIGN_RECORD_SCOPE:
    return vpiModule;
  case OBELISK_RT_DESIGN_RECORD_STORAGE:
    return vpiReg;
  case OBELISK_RT_DESIGN_RECORD_NET:
    return vpiNet;
  case OBELISK_RT_DESIGN_RECORD_FUNCTION:
    return vpiFunction;
  case OBELISK_RT_DESIGN_RECORD_PORT:
    return vpiPort;
  default:
    return vpiUndefined;
  }
}

PLI_INT32 vpiTypeForHandle(VPIHandleKind kind) {
  switch (kind) {
  case VPIHandleKind::Iterator:
    return vpiIterator;
  case VPIHandleKind::Callback:
    return vpiCallback;
  case VPIHandleKind::ScheduledEvent:
    return vpiSchedEvent;
  case VPIHandleKind::SystemTf:
    return vpiUserSystf;
  case VPIHandleKind::Object:
    return vpiUndefined;
  }
  return vpiUndefined;
}

int exactTypeFor(VPIState *state, obelisk_rt_design_cursor_v1 cursor,
                 uint32_t fallbackKind) {
  uint32_t exact = 0;
  if (obelisk_rt_cached_vpi_type(state->context, cursor, &exact) ==
      OBELISK_RT_OK)
    return static_cast<int>(exact);
  return vpiTypeFor(fallbackKind);
}

bool nameFor(__vpiHandle *handle, std::string &name) {
  const uint8_t *data = nullptr;
  uint64_t size = 0;
  obelisk_rt_status status = obelisk_rt_cached_design_name(
      handle->owner->context, handle->cursor, &data, &size);
  if (status != OBELISK_RT_OK) {
    setError(handle->owner, "design name lookup failed");
    return false;
  }
  OBELISK_RT_TRY {
    if (size == 0)
      name.clear();
    else
      name.assign(reinterpret_cast<const char *>(data),
                  static_cast<size_t>(size));
    return true;
  }
  OBELISK_RT_CATCH_ALL {
    setError(handle->owner, "could not materialize design name", vpiSystem);
    return false;
  }
}

bool fullNameForStatement(__vpiHandle *handle, std::string &name) {
  OBELISK_RT_TRY {
    std::vector<std::string> components;
    obelisk_rt_design_cursor_v1 cursor = handle->cursor;
    while (true) {
      const uint8_t *data = nullptr;
      uint64_t size = 0;
      if (obelisk_rt_cached_design_name(handle->owner->context, cursor, &data,
                                        &size) != OBELISK_RT_OK)
        return false;
      if (size != 0)
        components.emplace_back(reinterpret_cast<const char *>(data),
                                static_cast<size_t>(size));
      obelisk_rt_design_cursor_v1 parent{};
      obelisk_rt_status status = obelisk_rt_cached_vpi_statement_parent(
          handle->owner->context, cursor, &parent);
      if (status == OBELISK_RT_EOF)
        break;
      if (status != OBELISK_RT_OK)
        return false;
      cursor = parent;
    }
    obelisk_rt_design_cursor_v1 base{};
    uint32_t baseType = 0;
    bool useCodeUnit = false;
    obelisk_rt_design_cursor_v1 owner{};
    obelisk_rt_status ownerStatus = obelisk_rt_cached_vpi_statement_owner(
        handle->owner->context, handle->cursor, &owner);
    if (ownerStatus == OBELISK_RT_OK &&
        obelisk_rt_cached_vpi_type(handle->owner->context, owner, &baseType) ==
            OBELISK_RT_OK) {
      using VPIKind = obelisk::reflection::VPIObjectKind;
      useCodeUnit = baseType == static_cast<uint16_t>(VPIKind::Task) ||
                    baseType == static_cast<uint16_t>(VPIKind::Function);
      if (useCodeUnit)
        base = owner;
    } else if (ownerStatus != OBELISK_RT_EOF) {
      return false;
    }
    if (!useCodeUnit) {
      if (obelisk_rt_cached_vpi_statement_scope(
              handle->owner->context, handle->cursor, &base) != OBELISK_RT_OK ||
          obelisk_rt_cached_vpi_type(handle->owner->context, base, &baseType) !=
              OBELISK_RT_OK)
        return false;
    }
    const uint8_t *baseName = nullptr;
    uint64_t baseNameSize = 0;
    if (obelisk_rt_cached_design_name(handle->owner->context, base, &baseName,
                                      &baseNameSize) != OBELISK_RT_OK ||
        baseNameSize == 0)
      return false;
    name.assign(reinterpret_cast<const char *>(baseName),
                static_cast<size_t>(baseNameSize));
    using VPIKind = obelisk::reflection::VPIObjectKind;
    bool namespaceBase =
        !useCodeUnit && (baseType == static_cast<uint16_t>(VPIKind::Package) ||
                         baseType == static_cast<uint16_t>(VPIKind::ClassDefn));
    bool first = true;
    for (auto component = components.rbegin(); component != components.rend();
         ++component) {
      name.append(first && namespaceBase ? "::" : ".");
      name.append(*component);
      first = false;
    }
    return true;
  }
  OBELISK_RT_CATCH_ALL {
    setError(handle->owner, "could not materialize statement full name",
             vpiSystem);
    return false;
  }
}

obelisk_rt_status
findMatchingDesignObject(VPIState *state, obelisk_rt_design_cursor_v1 cursor,
                         obelisk::reflection::VPIObjectSetID targets,
                         obelisk_rt_design_cursor_v1 *outCursor) {
  while (cursor.offset != 0) {
    obelisk_rt_design_info_v1 info{};
    obelisk_rt_status status =
        obelisk_rt_cached_design_info(state->context, cursor, &info);
    if (status != OBELISK_RT_OK)
      return status;
    uint32_t exact =
        static_cast<uint32_t>(exactTypeFor(state, cursor, info.kind));
    if (obelisk::reflection::vpiObjectSetContains(targets, exact)) {
      *outCursor = cursor;
      return OBELISK_RT_OK;
    }
    status = obelisk_rt_cached_design_sibling(state->context, cursor, &cursor);
    if (status != OBELISK_RT_OK)
      return status;
  }
  return OBELISK_RT_EOF;
}

bool lookup(VPIState *state, const std::string &name,
            obelisk_rt_design_cursor_v1 &cursor) {
  return obelisk_rt_cached_design_lookup(
             state->context, reinterpret_cast<const uint8_t *>(name.data()),
             name.size(), &cursor) == OBELISK_RT_OK;
}

bool readValue(__vpiHandle *handle, obelisk_rt_design_info_v1 &info) {
  if (!infoFor(handle, info) || info.bit_width == 0 ||
      info.kind == OBELISK_RT_DESIGN_RECORD_DRIVER) {
    setError(handle->owner,
             "VPI value access requires readable storage or net");
    return false;
  }
  OBELISK_RT_TRY {
    size_t limbs = static_cast<size_t>((info.bit_width + 63) / 64);
    handle->valueScratch.assign(limbs, 0);
    handle->unknownScratch.assign(limbs, 0);
  }
  OBELISK_RT_CATCH_ALL {
    setError(handle->owner, "VPI value buffer is out of memory", vpiSystem);
    return false;
  }
  obelisk_rt_status status = obelisk_rt_v1_design_read(
      handle->owner->context, handle->cursor, handle->valueScratch.data(),
      handle->unknownScratch.data(), info.bit_width);
  if (status != OBELISK_RT_OK) {
    setError(handle->owner, "VPI design read failed");
    return false;
  }
  return true;
}

bool decodeValue(__vpiHandle *handle, const s_vpi_value *source, uint64_t width,
                 std::vector<uint64_t> &value, std::vector<uint64_t> &unknown) {
  if (!source) {
    setError(handle->owner, "VPI write value is null");
    return false;
  }
  OBELISK_RT_TRY {
    size_t limbs = static_cast<size_t>((width + 63) / 64);
    value.assign(limbs, 0);
    unknown.assign(limbs, 0);
    switch (source->format) {
    case vpiVectorVal:
      if (!source->value.vector)
        return false;
      for (size_t bit = 0; bit < width; ++bit) {
        size_t word = bit / 32;
        uint32_t mask = uint32_t{1} << (bit % 32);
        bool b = (source->value.vector[word].bval & mask) != 0;
        bool a = (source->value.vector[word].aval & mask) != 0;
        if (a != b)
          value[bit / 64] |= uint64_t{1} << (bit % 64);
        if (b)
          unknown[bit / 64] |= uint64_t{1} << (bit % 64);
      }
      break;
    case vpiIntVal:
      if (source->value.integer < 0)
        std::fill(value.begin(), value.end(), UINT64_MAX);
      value[0] = (value[0] & ~UINT64_C(0xffffffff)) |
                 static_cast<uint32_t>(source->value.integer);
      break;
    case vpiScalarVal:
      if (source->value.scalar == vpi1)
        value[0] = 1;
      else if (source->value.scalar == vpiX) {
        unknown[0] = 1;
      } else if (source->value.scalar == vpiZ) {
        value[0] = 1;
        unknown[0] = 1;
      } else if (source->value.scalar != vpi0) {
        setError(handle->owner, "invalid VPI scalar value");
        return false;
      }
      break;
    case vpiBinStrVal: {
      if (!source->value.str)
        return false;
      std::string text(source->value.str);
      size_t bit = 0;
      for (auto iterator = text.rbegin(); iterator != text.rend(); ++iterator) {
        char digit = *iterator;
        if (digit == '_')
          continue;
        if (digit != '0' && digit != '1' && digit != 'x' && digit != 'X' &&
            digit != 'z' && digit != 'Z' && digit != '?') {
          setError(handle->owner, "invalid binary digit in VPI write");
          return false;
        }
        if (bit < width) {
          uint64_t mask = uint64_t{1} << (bit % 64);
          if (digit == '1')
            value[bit / 64] |= mask;
          else if (digit == 'x' || digit == 'X')
            unknown[bit / 64] |= mask;
          else if (digit == 'z' || digit == 'Z' || digit == '?') {
            value[bit / 64] |= mask;
            unknown[bit / 64] |= mask;
          }
        }
        ++bit;
      }
      break;
    }
    default:
      setError(handle->owner, "unsupported VPI value format");
      return false;
    }
    if (width % 64 != 0) {
      uint64_t mask = (uint64_t{1} << (width % 64)) - 1;
      value.back() &= mask;
      unknown.back() &= mask;
    }
    return true;
  }
  OBELISK_RT_CATCH_ALL {
    setError(handle->owner, "could not decode VPI value", vpiSystem);
    return false;
  }
}

void unsupportedStartup(const char *feature) {
  VPIState *state = activeState;
  if (state)
    state->unsupportedStartup = true;
  setError(state, feature, vpiError, "OBELISK_VPI_UNSUPPORTED_STARTUP");
}

} // namespace

extern "C" OBELISK_VPI_EXPORT obelisk_rt_status
obelisk_rt_v1_vpi_startup(obelisk_rt_context *context,
                          const char *const *modules, uint64_t moduleCount) {
  if (!context || (moduleCount != 0 && !modules) || activeState)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (!context->execution ||
      (context->execution->flags & OBELISK_RT_EXECUTION_VPI_READ) == 0)
    return OBELISK_RT_PERMISSION_DENIED;
  std::unique_ptr<VPIState> state;
  OBELISK_RT_TRY { state = std::make_unique<VPIState>(); }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_OUT_OF_MEMORY; }
  state->context = context;
  activeState = state.get();
  for (uint64_t moduleIndex = 0; moduleIndex != moduleCount; ++moduleIndex) {
    const char *name = modules[moduleIndex];
    if (!name || !*name) {
      activeState = nullptr;
      return OBELISK_RT_INVALID_ARGUMENT;
    }
#if !defined(OBELISK_RT_VPI_DYNAMIC_STARTUP)
    // Same status as a failed dlopen, because that is what happened from the
    // caller's side; the message carries the reason it can never succeed here.
    setError(state.get(),
             "loading VPI startup modules requires a dynamic loader, which "
             "this target does not provide",
             vpiSystem);
    activeState = nullptr;
    return OBELISK_RT_IO_ERROR;
#else
    void *module = dlopen(name, RTLD_LAZY | RTLD_NOLOAD);
    if (!module) {
      setError(state.get(), dlerror(), vpiSystem);
      activeState = nullptr;
      return OBELISK_RT_IO_ERROR;
    }
    dlerror();
    void *symbol = dlsym(module, "vlog_startup_routines");
    const char *symbolError = dlerror();
    if (!symbol || symbolError) {
      setError(state.get(),
               symbolError ? symbolError : "VPI startup table is missing",
               vpiSystem);
      dlclose(module);
      activeState = nullptr;
      return OBELISK_RT_INVALID_DESIGN;
    }
    size_t entries = 0;
    Dl_info addressInfo{};
    void *extra = nullptr;
    if (dladdr1(symbol, &addressInfo, &extra, RTLD_DL_SYMENT) != 0 && extra) {
      const auto *elfSymbol = static_cast<const Elf64_Sym *>(extra);
      entries = static_cast<size_t>(elfSymbol->st_size / sizeof(void *));
    }
    if (entries == 0 || entries > 65536) {
      setError(state.get(), "VPI startup table has no bounded ELF symbol size");
      dlclose(module);
      activeState = nullptr;
      return OBELISK_RT_INVALID_DESIGN;
    }
    auto *routines = static_cast<void (**)(void)>(symbol);
    bool terminated = false;
    for (size_t index = 0; index != entries; ++index) {
      if (!routines[index]) {
        terminated = true;
        break;
      }
      routines[index]();
      if (state->unsupportedStartup)
        break;
    }
    dlclose(module);
    if (!terminated || state->unsupportedStartup) {
      if (!terminated)
        setError(state.get(), "VPI startup table is not null terminated");
      activeState = nullptr;
      return OBELISK_RT_INVALID_DESIGN;
    }
#endif
  }
  state->phase = VPIPhase::BeforeEndCompile;
  // Ownership moves only after all startup modules have succeeded.
  context->vpiState = state.release();
  return OBELISK_RT_OK;
}

extern "C" OBELISK_VPI_EXPORT obelisk_rt_status
obelisk_rt_v1_vpi_end_compile(obelisk_rt_context *context) {
  if (!context || context->vpiState != activeState)
    return OBELISK_RT_INVALID_ARGUMENT;
  auto *state = static_cast<VPIState *>(context->vpiState);
  if (state->phase != VPIPhase::BeforeEndCompile)
    return OBELISK_RT_INVALID_ARGUMENT;
  state->phase = VPIPhase::EndCompile;
  obelisk_rt_status status = dispatchLifecycle(state, cbEndOfCompile);
  state->phase = VPIPhase::BeforeStartSimulation;
  return status;
}

extern "C" OBELISK_VPI_EXPORT obelisk_rt_status
obelisk_rt_v1_vpi_start_simulation(obelisk_rt_context *context) {
  if (!context || context->vpiState != activeState)
    return OBELISK_RT_INVALID_ARGUMENT;
  auto *state = static_cast<VPIState *>(context->vpiState);
  if (state->phase != VPIPhase::BeforeStartSimulation)
    return OBELISK_RT_INVALID_ARGUMENT;
  state->phase = VPIPhase::StartSimulation;
  obelisk_rt_status status = dispatchLifecycle(state, cbStartOfSimulation);
  state->phase = VPIPhase::Running;
  return status;
}

extern "C" OBELISK_VPI_EXPORT obelisk_rt_status
obelisk_rt_v1_vpi_end_simulation(obelisk_rt_context *context) {
  if (!context || context->vpiState != activeState)
    return OBELISK_RT_INVALID_ARGUMENT;
  auto *state = static_cast<VPIState *>(context->vpiState);
  if (state->phase != VPIPhase::Running)
    return OBELISK_RT_INVALID_ARGUMENT;
  state->phase = VPIPhase::EndSimulation;
  obelisk_rt_status status = dispatchLifecycle(state, cbEndOfSimulation);
  state->phase = VPIPhase::Ended;
  return status;
}

extern "C" OBELISK_VPI_EXPORT void
obelisk_rt_v1_vpi_shutdown(obelisk_rt_context *context) {
  if (!context || context->vpiState != activeState)
    return;
  auto *state = static_cast<VPIState *>(context->vpiState);
  context->vpiState = nullptr;
  activeState = nullptr;
  delete state;
}

extern "C" OBELISK_VPI_EXPORT vpiHandle vpi_handle_by_name(PLI_BYTE8 *name,
                                                           vpiHandle scope) {
  VPIState *state = requireState();
  if (!state || !name)
    return nullptr;
  std::string requested(name);
  if (requested == "$root" || requested == "\\$root ") {
    obelisk_rt_design_cursor_v1 root{};
    return obelisk_rt_cached_design_root(state->context, &root) == OBELISK_RT_OK
               ? makeHandle(state, root)
               : nullptr;
  }
  bool absolute = requested.rfind("$root.", 0) == 0;
  if (absolute)
    requested.erase(0, 6);
  if (scope && !absolute) {
    __vpiHandle *base = validate(scope);
    if (!base)
      return nullptr;
    std::string prefix;
    if (!nameFor(base, prefix))
      return nullptr;
    if (prefix != "$root" && prefix != "\\$root ")
      requested = prefix + "." + requested;
  }
  obelisk_rt_design_cursor_v1 cursor{};
  if (!lookup(state, requested, cursor)) {
    setError(state, "hierarchical VPI name was not found", vpiNotice);
    return nullptr;
  }
  obelisk_rt_design_info_v1 info{};
  uint32_t exactType = 0;
  if (obelisk_rt_cached_design_info(state->context, cursor, &info) !=
          OBELISK_RT_OK ||
      obelisk_rt_cached_vpi_type(state->context, cursor, &exactType) !=
          OBELISK_RT_OK ||
      exactType == 0 ||
      (info.capabilities & OBELISK_RT_DESIGN_CAP_INTERNAL) != 0) {
    setError(state, "hierarchical name does not identify a VPI object",
             vpiNotice);
    return nullptr;
  }
  return makeHandle(state, cursor);
}

extern "C" OBELISK_VPI_EXPORT vpiHandle vpi_handle(PLI_INT32 type,
                                                   vpiHandle reference) {
  __vpiHandle *handle = validate(reference);
  if (!handle)
    return nullptr;
  VPIRelationRange range{};
  obelisk_rt_status relationStatus = obelisk_rt_cached_vpi_relation_range(
      handle->owner->context, handle->cursor, static_cast<uint32_t>(type),
      false, &range);
  if (relationStatus == OBELISK_RT_OK) {
    obelisk_rt_design_cursor_v1 target{};
    uint32_t targetType = 0;
    bool targetIsStatement = false;
    if (obelisk_rt_cached_vpi_relation_target(
            handle->owner->context, range.first, &target, &targetType,
            &targetIsStatement) != OBELISK_RT_OK)
      return nullptr;
    return makeHandle(handle->owner, target, targetType, targetIsStatement);
  }
  if (type != vpiScope)
    return nullptr;
  if (handle->statement) {
    obelisk_rt_design_cursor_v1 cursor = handle->cursor;
    while (true) {
      obelisk_rt_design_cursor_v1 parent{};
      obelisk_rt_status status = obelisk_rt_cached_vpi_statement_parent(
          handle->owner->context, cursor, &parent);
      if (status == OBELISK_RT_EOF)
        break;
      if (status != OBELISK_RT_OK)
        return nullptr;
      bool parentIsScope = false;
      if (obelisk_rt_cached_vpi_statement_is_scope(
              handle->owner->context, parent, &parentIsScope) != OBELISK_RT_OK)
        return nullptr;
      if (parentIsScope) {
        uint32_t parentType = 0;
        if (obelisk_rt_cached_vpi_type(handle->owner->context, parent,
                                       &parentType) != OBELISK_RT_OK)
          return nullptr;
        return makeHandle(handle->owner, parent, parentType, true);
      }
      cursor = parent;
    }
    obelisk_rt_design_cursor_v1 owner{};
    obelisk_rt_status ownerStatus = obelisk_rt_cached_vpi_statement_owner(
        handle->owner->context, handle->cursor, &owner);
    if (ownerStatus == OBELISK_RT_OK) {
      uint32_t ownerType = 0;
      if (obelisk_rt_cached_vpi_type(handle->owner->context, owner,
                                     &ownerType) != OBELISK_RT_OK)
        return nullptr;
      const auto *kind = obelisk::reflection::findVPIObjectKind(ownerType);
      if (kind && (kind->families &
                   obelisk::reflection::vpiFamilyMask(
                       obelisk::reflection::VPIObjectFamily::Scope)) != 0)
        return makeHandle(handle->owner, owner, ownerType);
    } else if (ownerStatus != OBELISK_RT_EOF) {
      return nullptr;
    }
    obelisk_rt_design_cursor_v1 scope{};
    return obelisk_rt_cached_vpi_statement_scope(
               handle->owner->context, handle->cursor, &scope) == OBELISK_RT_OK
               ? makeHandle(handle->owner, scope)
               : nullptr;
  }
  std::string fullName;
  if (!nameFor(handle, fullName))
    return nullptr;
  size_t separator = fullName.rfind('.');
  if (separator == std::string::npos) {
    obelisk_rt_design_cursor_v1 root{};
    if (obelisk_rt_cached_design_root(handle->owner->context, &root) !=
        OBELISK_RT_OK)
      return nullptr;
    return makeHandle(handle->owner, root);
  }
  fullName.resize(separator);
  obelisk_rt_design_cursor_v1 parent{};
  return lookup(handle->owner, fullName, parent)
             ? makeHandle(handle->owner, parent)
             : nullptr;
}

extern "C" OBELISK_VPI_EXPORT vpiHandle vpi_iterate(PLI_INT32 type,
                                                    vpiHandle reference) {
  VPIState *state = requireState();
  if (!state)
    return nullptr;
  if (type == vpiCallback && !reference) {
    std::vector<obelisk_rt_design_cursor_v1> callbacks;
    OBELISK_RT_TRY {
      callbacks.reserve(state->callbacks.size());
      for (uint64_t id : state->callbackOrder)
        if (state->callbacks.find(id) != state->callbacks.end())
          callbacks.push_back({id});
      if (callbacks.empty())
        return nullptr;
      auto iterator = std::make_unique<__vpiHandle>();
      iterator->owner = state;
      iterator->kind = VPIHandleKind::Iterator;
      iterator->items = std::move(callbacks);
      iterator->callbackIterator = true;
      return keepHandle(state, std::move(iterator));
    }
    OBELISK_RT_CATCH_ALL {
      setError(state, "could not allocate VPI callback iterator", vpiSystem);
      return nullptr;
    }
  }
  obelisk_rt_design_cursor_v1 parent{};
  uint32_t sourceType = 0;
  if (reference) {
    __vpiHandle *handle = validate(reference);
    if (!handle)
      return nullptr;
    parent = handle->cursor;
    sourceType = handle->exactVpiType;
    if (sourceType == 0 &&
        obelisk_rt_cached_vpi_type(state->context, parent, &sourceType) !=
            OBELISK_RT_OK)
      return nullptr;
  } else if (obelisk_rt_cached_design_root(state->context, &parent) !=
             OBELISK_RT_OK) {
    return nullptr;
  }
  if (reference) {
    VPIRelationRange range{};
    obelisk_rt_status relationStatus = obelisk_rt_cached_vpi_relation_range(
        state->context, parent, static_cast<uint32_t>(type), true, &range);
    if (relationStatus == OBELISK_RT_OK) {
      OBELISK_RT_TRY {
        auto iterator = std::make_unique<__vpiHandle>();
        iterator->owner = state;
        iterator->kind = VPIHandleKind::Iterator;
        iterator->relationIterator = true;
        iterator->relationRange = range;
        return keepHandle(state, std::move(iterator));
      }
      OBELISK_RT_CATCH_ALL {
        setError(state, "could not allocate VPI relation iterator", vpiSystem);
        return nullptr;
      }
    }
    if (sourceType == 0)
      return nullptr;
  }
  const auto *edge = obelisk::reflection::findVPITraversal(
      sourceType, static_cast<uint32_t>(type),
      obelisk::reflection::VPITraversalMode::Iterate);
  if (!edge || edge->statementContainment)
    return nullptr;
  obelisk_rt_design_cursor_v1 cursor{};
  obelisk_rt_status status =
      obelisk_rt_cached_design_child(state->context, parent, &cursor);
  if (status == OBELISK_RT_OK)
    status = findMatchingDesignObject(state, cursor, edge->targets, &cursor);
  if (status != OBELISK_RT_OK)
    return nullptr;
  OBELISK_RT_TRY {
    auto iterator = std::make_unique<__vpiHandle>();
    iterator->owner = state;
    iterator->kind = VPIHandleKind::Iterator;
    iterator->designIterator = true;
    iterator->requestedTargets = edge->targets;
    iterator->cursor = cursor;
    return keepHandle(state, std::move(iterator));
  }
  OBELISK_RT_CATCH_ALL {
    setError(state, "could not allocate VPI iterator", vpiSystem);
    return nullptr;
  }
}

extern "C" OBELISK_VPI_EXPORT vpiHandle vpi_scan(vpiHandle opaque) {
  __vpiHandle *iterator = validate(opaque, VPIHandleKind::Iterator);
  if (!iterator)
    return nullptr;
  if (iterator->callbackIterator) {
    while (iterator->next != iterator->items.size()) {
      uint64_t id = iterator->items[iterator->next++].offset;
      if (iterator->owner->callbacks.find(id) !=
          iterator->owner->callbacks.end())
        return makeCallbackHandle(iterator->owner, id);
    }
    iterator->alive = false;
    return nullptr;
  }
  if (iterator->relationIterator) {
    if (iterator->next == iterator->relationRange.count) {
      iterator->alive = false;
      return nullptr;
    }
    obelisk_rt_design_cursor_v1 target{};
    uint32_t targetType = 0;
    bool targetIsStatement = false;
    obelisk_rt_status status = obelisk_rt_cached_vpi_relation_target(
        iterator->owner->context,
        iterator->relationRange.first + iterator->next, &target, &targetType,
        &targetIsStatement);
    if (status != OBELISK_RT_OK) {
      iterator->alive = false;
      setError(iterator->owner, "VPI relation target lookup failed",
               vpiInternal);
      return nullptr;
    }
    ++iterator->next;
    return makeHandle(iterator->owner, target, targetType, targetIsStatement);
  }
  if (iterator->designIterator) {
    if (iterator->cursor.offset == 0) {
      iterator->alive = false;
      return nullptr;
    }
    obelisk_rt_design_cursor_v1 current = iterator->cursor;
    obelisk_rt_design_cursor_v1 candidate{};
    obelisk_rt_status status = obelisk_rt_cached_design_sibling(
        iterator->owner->context, current, &candidate);
    if (status == OBELISK_RT_OK)
      status = findMatchingDesignObject(iterator->owner, candidate,
                                        iterator->requestedTargets, &candidate);
    if (status == OBELISK_RT_OK)
      iterator->cursor = candidate;
    else
      iterator->cursor = {};
    if (status != OBELISK_RT_OK && status != OBELISK_RT_EOF)
      setError(iterator->owner, "VPI design iterator lookup failed",
               vpiInternal);
    return makeHandle(iterator->owner, current);
  }
  if (iterator->next == iterator->items.size()) {
    iterator->alive = false;
    return nullptr;
  }
  return makeHandle(iterator->owner, iterator->items[iterator->next++]);
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32 vpi_get(PLI_INT32 property,
                                                vpiHandle opaque) {
  __vpiHandle *handle = findHandle(opaque);
  if (!handle)
    return vpiUndefined;
  if (property == vpiType && handle->kind != VPIHandleKind::Object)
    return vpiTypeForHandle(handle->kind);
  if (handle->kind != VPIHandleKind::Object) {
    setError(handle->owner, "unsupported property for VPI handle kind",
             vpiNotice);
    return vpiUndefined;
  }
  if (property == vpiType) {
    if (handle->exactVpiType != 0)
      return static_cast<PLI_INT32>(handle->exactVpiType);
    uint32_t exact = 0;
    if (obelisk_rt_cached_vpi_type(handle->owner->context, handle->cursor,
                                   &exact) == OBELISK_RT_OK) {
      handle->exactVpiType = exact;
      return exact == 0 ? vpiUndefined : static_cast<PLI_INT32>(exact);
    }
    obelisk_rt_design_info_v1 info{};
    return infoFor(handle, info) ? vpiTypeFor(info.kind) : vpiUndefined;
  }
  if (property == vpiSize) {
    if (handle->statement)
      return 0;
    obelisk_rt_design_info_v1 info{};
    if (!infoFor(handle, info))
      return vpiUndefined;
    return info.kind == OBELISK_RT_DESIGN_RECORD_SCOPE
               ? 0
               : static_cast<PLI_INT32>(
                     std::min<uint64_t>(info.bit_width, INT32_MAX));
  }
  if (property == vpiLineNo || property == vpiDefLineNo) {
    const uint8_t *file = nullptr;
    uint64_t fileSize = 0;
    uint32_t line = 0, column = 0;
    if (obelisk_rt_cached_design_source(handle->owner->context, handle->cursor,
                                        &file, &fileSize, &line,
                                        &column) != OBELISK_RT_OK)
      return vpiUndefined;
    return static_cast<PLI_INT32>(std::min<uint32_t>(line, INT32_MAX));
  }
  setError(handle->owner, "unsupported integer VPI property", vpiNotice);
  return vpiUndefined;
}

extern "C" OBELISK_VPI_EXPORT PLI_INT64 vpi_get64(PLI_INT32 property,
                                                  vpiHandle object) {
  return vpi_get(property, object);
}

extern "C" OBELISK_VPI_EXPORT PLI_BYTE8 *vpi_get_str(PLI_INT32 property,
                                                     vpiHandle opaque) {
  __vpiHandle *handle = validate(opaque);
  if (!handle)
    return nullptr;
  std::string &scratch = handle->owner->propertyStringScratch;
  if (property == vpiName || property == vpiFullName) {
    if (!nameFor(handle, scratch))
      return nullptr;
    if (scratch.empty())
      return nullptr;
    if (property == vpiFullName && handle->statement) {
      if (!fullNameForStatement(handle, scratch))
        return nullptr;
      return scratch.data();
    }
    if (property == vpiName) {
      size_t separator = scratch.rfind('.');
      if (separator != std::string::npos)
        scratch.erase(0, separator + 1);
    }
    return scratch.data();
  }
  if (property == vpiFile || property == vpiDefFile) {
    const uint8_t *file = nullptr;
    uint64_t size = 0;
    uint32_t line = 0, column = 0;
    if (obelisk_rt_cached_design_source(handle->owner->context, handle->cursor,
                                        &file, &size, &line,
                                        &column) != OBELISK_RT_OK)
      return nullptr;
    if (size == 0) {
      scratch.clear();
      return nullptr;
    }
    scratch.assign(reinterpret_cast<const char *>(file),
                   static_cast<size_t>(size));
    return scratch.data();
  }
  setError(handle->owner, "unsupported string VPI property", vpiNotice);
  return nullptr;
}

extern "C" OBELISK_VPI_EXPORT void vpi_get_value(vpiHandle opaque,
                                                 p_vpi_value destination) {
  __vpiHandle *handle = validate(opaque);
  if (!handle || !destination)
    return;
  obelisk_rt_design_info_v1 info{};
  if (!readValue(handle, info))
    return;
  const std::vector<uint64_t> &value = handle->valueScratch;
  const std::vector<uint64_t> &unknown = handle->unknownScratch;
  switch (destination->format) {
  case vpiVectorVal: {
    OBELISK_RT_TRY {
      handle->owner->vectorScratch.resize(
          static_cast<size_t>((info.bit_width + 31) / 32));
      destination->value.vector = handle->owner->vectorScratch.data();
    }
    OBELISK_RT_CATCH_ALL {
      setError(handle->owner, "VPI vector buffer is out of memory", vpiSystem);
      return;
    }
    size_t words = static_cast<size_t>((info.bit_width + 31) / 32);
    for (size_t word = 0; word != words; ++word) {
      uint32_t a = 0, b = 0;
      for (unsigned bit = 0; bit != 32; ++bit) {
        size_t absolute = word * 32 + bit;
        if (absolute >= info.bit_width)
          break;
        uint64_t mask = uint64_t{1} << (absolute % 64);
        bool v = (value[absolute / 64] & mask) != 0;
        bool u = (unknown[absolute / 64] & mask) != 0;
        a |= static_cast<uint32_t>(v != u) << bit;
        b |= static_cast<uint32_t>(u) << bit;
      }
      destination->value.vector[word] = {a, b};
    }
    break;
  }
  case vpiIntVal:
    destination->value.integer = static_cast<PLI_INT32>(value[0] & ~unknown[0]);
    break;
  case vpiScalarVal: {
    bool v = (value[0] & 1) != 0;
    bool u = (unknown[0] & 1) != 0;
    destination->value.scalar = !u ? (v ? vpi1 : vpi0) : (v ? vpiZ : vpiX);
    break;
  }
  case vpiBinStrVal:
    OBELISK_RT_TRY {
      handle->owner->valueStringScratch.assign(
          static_cast<size_t>(info.bit_width), '0');
      for (uint64_t bit = 0; bit != info.bit_width; ++bit) {
        uint64_t mask = uint64_t{1} << (bit % 64);
        bool v = (value[bit / 64] & mask) != 0;
        bool u = (unknown[bit / 64] & mask) != 0;
        handle->owner->valueStringScratch[info.bit_width - 1 - bit] =
            !u ? (v ? '1' : '0') : (v ? 'z' : 'x');
      }
      destination->value.str = handle->owner->valueStringScratch.data();
    }
    OBELISK_RT_CATCH_ALL {
      setError(handle->owner, "could not format binary VPI value", vpiSystem);
    }
    break;
  default:
    setError(handle->owner, "unsupported VPI read format");
    break;
  }
}

extern "C" OBELISK_VPI_EXPORT vpiHandle vpi_put_value(vpiHandle opaque,
                                                      p_vpi_value source,
                                                      p_vpi_time,
                                                      PLI_INT32 flags) {
  __vpiHandle *handle = validate(opaque);
  if (!handle)
    return nullptr;
  obelisk_rt_context *context = handle->owner->context;
  if (!context->execution ||
      (context->execution->flags & OBELISK_RT_EXECUTION_VPI_WRITE) == 0) {
    setError(handle->owner, "VPI mutation requires --vpi=full");
    return nullptr;
  }
  obelisk_rt_design_info_v1 info{};
  if (!infoFor(handle, info) || info.kind == OBELISK_RT_DESIGN_RECORD_DRIVER) {
    setError(handle->owner, "VPI driver writes are not supported");
    return nullptr;
  }
  if (flags == vpiReleaseFlag) {
    if (obelisk_rt_v1_design_release(context, handle->cursor) != OBELISK_RT_OK)
      setError(handle->owner, "VPI release failed");
    return nullptr;
  }
  if (flags != vpiNoDelay && flags != vpiForceFlag) {
    setError(handle->owner, "delayed VPI writes are not supported");
    return nullptr;
  }
  if (!decodeValue(handle, source, info.bit_width, handle->valueScratch,
                   handle->unknownScratch))
    return nullptr;
  obelisk_rt_status status =
      flags == vpiForceFlag
          ? obelisk_rt_v1_design_force(
                context, handle->cursor, handle->valueScratch.data(),
                handle->unknownScratch.data(), info.bit_width)
          : obelisk_rt_v1_design_write(
                context, handle->cursor, handle->valueScratch.data(),
                handle->unknownScratch.data(), info.bit_width);
  if (status != OBELISK_RT_OK)
    setError(handle->owner, "VPI write failed");
  return nullptr;
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32 vpi_compare_objects(vpiHandle first,
                                                            vpiHandle second) {
  __vpiHandle *left = findHandle(first);
  __vpiHandle *right = findHandle(second);
  if (!left || !right || left->kind != right->kind)
    return 0;
  if (left->kind == VPIHandleKind::Callback)
    return left->cursor.offset == right->cursor.offset;
  if (left->kind != VPIHandleKind::Object) {
    setError(left->owner, "VPI handle kind does not denote an object");
    return 0;
  }
  return left->cursor.offset == right->cursor.offset;
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32 vpi_release_handle(vpiHandle opaque) {
  VPIState *state = requireState();
  auto *handle = reinterpret_cast<__vpiHandle *>(opaque);
  if (!state || !handle ||
      state->handles.find(handle) == state->handles.end() ||
      handle->owner != state || !handle->alive) {
    setError(state, "VPI handle was already released or is invalid");
    return 0;
  }
  handle->alive = false;
  return 1;
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32 vpi_free_object(vpiHandle object) {
  return vpi_release_handle(object);
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32
vpi_chk_error(p_vpi_error_info destination) {
  VPIState *state = requireState();
  if (!state || state->errorLevel == 0)
    return 0;
  int level = state->errorLevel;
  if (destination) {
    *destination = {};
    destination->state = vpiRun;
    destination->level = level;
    destination->message = state->errorMessage.data();
    destination->product = const_cast<char *>("Obelisk");
    destination->code = state->errorCode.data();
  }
  state->errorLevel = 0;
  return level;
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32 vpi_vprintf(PLI_BYTE8 *format,
                                                    va_list arguments) {
  return requireState() && format ? std::vprintf(format, arguments) : -1;
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32 vpi_printf(PLI_BYTE8 *format, ...) {
  va_list arguments;
  va_start(arguments, format);
  int result = vpi_vprintf(format, arguments);
  va_end(arguments);
  return result;
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32 vpi_flush(void) {
  return requireState() ? std::fflush(nullptr) : -1;
}

extern "C" OBELISK_VPI_EXPORT void *vpi_get_userdata(vpiHandle opaque) {
  __vpiHandle *handle = validate(opaque);
  return handle ? handle->userData : nullptr;
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32 vpi_put_userdata(vpiHandle opaque,
                                                         void *data) {
  __vpiHandle *handle = validate(opaque);
  if (!handle)
    return 0;
  handle->userData = data;
  return 1;
}

extern "C" OBELISK_VPI_EXPORT vpiHandle
vpi_register_cb(p_cb_data callbackData) {
  VPIState *state = activeState;
  if (!state || !callbackData || !callbackData->cb_rtn) {
    setError(state, "VPI callback registration requires data and a routine");
    return nullptr;
  }
  if (!isLifecycleReason(callbackData->reason)) {
    if (state->phase == VPIPhase::StartupRestricted)
      state->unsupportedStartup = true;
    setError(state, "VPI callback reason is not implemented");
    return nullptr;
  }
  // IEEE 1800-2017 38.36.3 only requires reason, cb_rtn, and optionally
  // user_data for action callbacks. The remaining caller fields are ignored.
  OBELISK_RT_TRY {
    if (state->nextCallbackId == std::numeric_limits<uint64_t>::max()) {
      setError(state, "VPI callback identifier space is exhausted", vpiSystem);
      return nullptr;
    }
    const uint64_t id = state->nextCallbackId++;
    VPICallback callback{id, callbackData->reason, callbackData->cb_rtn,
                         callbackData->user_data};
    auto inserted = state->callbacks.try_emplace(id, callback);
    if (!inserted.second) {
      setError(state, "VPI callback identifier collision", vpiInternal);
      return nullptr;
    }
    OBELISK_RT_TRY { state->callbackOrder.push_back(id); }
    OBELISK_RT_CATCH_ALL {
      state->callbacks.erase(id);
      OBELISK_RT_RETHROW;
    }
    vpiHandle result = makeCallbackHandle(state, id);
    if (!result) {
      state->callbacks.erase(id);
      state->callbackOrder.pop_back();
    }
    return result;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    setError(state, "VPI callback registry is out of memory", vpiSystem);
    return nullptr;
  }
  OBELISK_RT_CATCH_ALL {
    setError(state, "could not register VPI callback", vpiInternal);
    return nullptr;
  }
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32 vpi_remove_cb(vpiHandle opaque) {
  VPIState *state = requireState();
  if (!state)
    return 0;
  auto *handle = reinterpret_cast<__vpiHandle *>(opaque);
  if (!handle || state->handles.find(handle) == state->handles.end()) {
    setError(state, "invalid VPI callback handle");
    return 0;
  }
  return removeCallbackHandle(state, handle);
}

extern "C" OBELISK_VPI_EXPORT void vpi_get_cb_info(vpiHandle opaque,
                                                   p_cb_data destination) {
  if (!destination)
    return;
  *destination = {};
  __vpiHandle *handle = validate(opaque, VPIHandleKind::Callback);
  VPICallback *callback = findCallback(handle);
  if (!callback)
    return;
  destination->reason = callback->reason;
  destination->cb_rtn = callback->routine;
  destination->user_data = callback->userData;
}

extern "C" OBELISK_VPI_EXPORT vpiHandle vpi_register_systf(p_vpi_systf_data) {
  unsupportedStartup(
      "VPI system task/function registration is not supported during startup");
  return nullptr;
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32
vpi_get_vlog_info(p_vpi_vlog_info info) {
  static char product[] = "Obelisk";
  static char version[] = "0.1";
  if (!requireState() || !info)
    return 0;
  *info = {};
  info->product = product;
  info->version = version;
  return 1;
}

extern "C" OBELISK_VPI_EXPORT vpiHandle vpi_handle_by_index(vpiHandle,
                                                            PLI_INT32) {
  VPIState *state = requireState();
  setError(state, "indexed VPI handles are not supported");
  return nullptr;
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32 vpi_control(PLI_INT32, ...) {
  VPIState *state = requireState();
  setError(state, "VPI control operations are not supported");
  return 0;
}
