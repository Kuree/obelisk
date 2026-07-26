//===- VPI.cpp - Single-context IEEE VPI compatibility shim ---------------===//

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "RuntimeInternal.h"

#include "VPIInternal.h"

#include <dlfcn.h>
#include <elf.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#define OBELISK_VPI_EXPORT __attribute__((visibility("default")))
#else
#define OBELISK_VPI_EXPORT
#endif

namespace {

struct VPIState;

} // namespace

struct __vpiHandle {
  VPIState *owner = nullptr;
  bool alive = true;
  bool iterator = false;
  obelisk_rt_design_cursor_v1 cursor{};
  std::vector<obelisk_rt_design_cursor_v1> items;
  size_t next = 0;
  std::string scratch;
  std::vector<s_vpi_vecval> vectorScratch;
  void *userData = nullptr;
};

namespace {

struct VPIState {
  obelisk_rt_context *context = nullptr;
  std::vector<std::unique_ptr<__vpiHandle>> handles;
  std::unordered_set<__vpiHandle *> arena;
  std::string errorMessage;
  std::string errorCode;
  int errorLevel = 0;
  bool unsupportedStartup = false;
};

VPIState *activeState = nullptr;

void setError(VPIState *state, const char *message, int level = vpiError,
              const char *code = "OBELISK_VPI") {
  if (!state)
    return;
  try {
    state->errorMessage = message ? message : "VPI error";
    state->errorCode = code;
    state->errorLevel = level;
  } catch (...) {
  }
}

VPIState *requireState() {
  if (!activeState)
    return nullptr;
  return activeState;
}

__vpiHandle *validate(vpiHandle opaque, bool iterator = false) {
  VPIState *state = requireState();
  auto *handle = reinterpret_cast<__vpiHandle *>(opaque);
  if (!state || !handle || state->arena.find(handle) == state->arena.end() ||
      handle->owner != state || !handle->alive ||
      handle->iterator != iterator) {
    setError(state, "invalid, released, or wrong-kind VPI handle");
    return nullptr;
  }
  return handle;
}

vpiHandle makeHandle(VPIState *state, obelisk_rt_design_cursor_v1 cursor) {
  try {
    auto handle = std::make_unique<__vpiHandle>();
    handle->owner = state;
    handle->cursor = cursor;
    __vpiHandle *result = handle.get();
    state->handles.push_back(std::move(handle));
    try {
      state->arena.insert(result);
    } catch (...) {
      state->handles.pop_back();
      throw;
    }
    return reinterpret_cast<vpiHandle>(result);
  } catch (const std::bad_alloc &) {
    setError(state, "VPI handle arena is out of memory", vpiSystem);
    return nullptr;
  } catch (...) {
    setError(state, "could not allocate VPI handle", vpiInternal);
    return nullptr;
  }
}

bool infoFor(__vpiHandle *handle, obelisk_rt_design_info_v1 &info) {
  obelisk_rt_status status = obelisk_rt_v1_design_info(
      handle->owner->context->execution, handle->cursor, &info);
  if (status != OBELISK_RT_OK) {
    setError(handle->owner, "design metadata lookup failed");
    return false;
  }
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
  case OBELISK_RT_DESIGN_RECORD_DRIVER:
    return vpiDriver;
  default:
    return vpiUndefined;
  }
}

bool matchesType(uint32_t kind, int requested) {
  int actual = vpiTypeFor(kind);
  return requested == actual || (requested == vpiInternalScope &&
                                 kind == OBELISK_RT_DESIGN_RECORD_SCOPE);
}

bool nameFor(__vpiHandle *handle, std::string &name) {
  const uint8_t *data = nullptr;
  uint64_t size = 0;
  obelisk_rt_status status = obelisk_rt_v1_design_name(
      handle->owner->context->execution, handle->cursor, &data, &size);
  if (status != OBELISK_RT_OK) {
    setError(handle->owner, "design name lookup failed");
    return false;
  }
  try {
    name.assign(reinterpret_cast<const char *>(data),
                static_cast<size_t>(size));
    return true;
  } catch (...) {
    setError(handle->owner, "could not materialize design name", vpiSystem);
    return false;
  }
}

bool lookup(VPIState *state, const std::string &name,
            obelisk_rt_design_cursor_v1 &cursor) {
  return obelisk_rt_v1_design_lookup(
             state->context->execution,
             reinterpret_cast<const uint8_t *>(name.data()), name.size(),
             &cursor) == OBELISK_RT_OK;
}

bool readValue(__vpiHandle *handle, obelisk_rt_design_info_v1 &info,
               std::vector<uint64_t> &value, std::vector<uint64_t> &unknown) {
  if (!infoFor(handle, info) || info.bit_width == 0 ||
      info.kind == OBELISK_RT_DESIGN_RECORD_DRIVER) {
    setError(handle->owner,
             "VPI value access requires readable storage or net");
    return false;
  }
  try {
    size_t limbs = static_cast<size_t>((info.bit_width + 63) / 64);
    value.assign(limbs, 0);
    unknown.assign(limbs, 0);
  } catch (...) {
    setError(handle->owner, "VPI value buffer is out of memory", vpiSystem);
    return false;
  }
  obelisk_rt_status status =
      obelisk_rt_v1_design_read(handle->owner->context, handle->cursor,
                                value.data(), unknown.data(), info.bit_width);
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
  try {
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
      value[0] = static_cast<uint32_t>(source->value.integer);
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
  } catch (...) {
    setError(handle->owner, "could not decode VPI value", vpiSystem);
    return false;
  }
}

void unsupportedStartup(const char *feature) {
  VPIState *state = requireState();
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
  try {
    state = std::make_unique<VPIState>();
  } catch (...) {
    return OBELISK_RT_OUT_OF_MEMORY;
  }
  state->context = context;
  activeState = state.get();
  for (uint64_t moduleIndex = 0; moduleIndex != moduleCount; ++moduleIndex) {
    const char *name = modules[moduleIndex];
    if (!name || !*name) {
      activeState = nullptr;
      return OBELISK_RT_INVALID_ARGUMENT;
    }
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
  }
  // Ownership moves only after all startup modules have succeeded.
  context->vpiState = state.release();
  return OBELISK_RT_OK;
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
    return obelisk_rt_v1_design_root(state->context->execution, &root) ==
                   OBELISK_RT_OK
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
  return makeHandle(state, cursor);
}

extern "C" OBELISK_VPI_EXPORT vpiHandle vpi_handle(PLI_INT32 type,
                                                   vpiHandle reference) {
  __vpiHandle *handle = validate(reference);
  if (!handle || type != vpiScope)
    return nullptr;
  std::string fullName;
  if (!nameFor(handle, fullName))
    return nullptr;
  size_t separator = fullName.rfind('.');
  if (separator == std::string::npos) {
    obelisk_rt_design_cursor_v1 root{};
    if (obelisk_rt_v1_design_root(handle->owner->context->execution, &root) !=
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
  obelisk_rt_design_cursor_v1 parent{};
  if (reference) {
    __vpiHandle *handle = validate(reference);
    if (!handle)
      return nullptr;
    parent = handle->cursor;
  } else if (obelisk_rt_v1_design_root(state->context->execution, &parent) !=
             OBELISK_RT_OK) {
    return nullptr;
  }
  std::vector<obelisk_rt_design_cursor_v1> items;
  obelisk_rt_design_cursor_v1 cursor{};
  obelisk_rt_status status =
      obelisk_rt_v1_design_child(state->context->execution, parent, &cursor);
  while (status == OBELISK_RT_OK) {
    obelisk_rt_design_info_v1 info{};
    if (obelisk_rt_v1_design_info(state->context->execution, cursor, &info) !=
        OBELISK_RT_OK)
      return nullptr;
    if (matchesType(info.kind, type))
      items.push_back(cursor);
    status = obelisk_rt_v1_design_sibling(state->context->execution, cursor,
                                          &cursor);
  }
  if (status != OBELISK_RT_EOF || items.empty())
    return nullptr;
  try {
    auto iterator = std::make_unique<__vpiHandle>();
    iterator->owner = state;
    iterator->iterator = true;
    iterator->items = std::move(items);
    __vpiHandle *result = iterator.get();
    state->handles.push_back(std::move(iterator));
    try {
      state->arena.insert(result);
    } catch (...) {
      state->handles.pop_back();
      throw;
    }
    return reinterpret_cast<vpiHandle>(result);
  } catch (...) {
    setError(state, "could not allocate VPI iterator", vpiSystem);
    return nullptr;
  }
}

extern "C" OBELISK_VPI_EXPORT vpiHandle vpi_scan(vpiHandle opaque) {
  __vpiHandle *iterator = validate(opaque, true);
  if (!iterator)
    return nullptr;
  if (iterator->next == iterator->items.size()) {
    iterator->alive = false;
    return nullptr;
  }
  return makeHandle(iterator->owner, iterator->items[iterator->next++]);
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32 vpi_get(PLI_INT32 property,
                                                vpiHandle opaque) {
  __vpiHandle *handle = validate(opaque);
  if (!handle)
    return vpiUndefined;
  obelisk_rt_design_info_v1 info{};
  if (!infoFor(handle, info))
    return vpiUndefined;
  if (property == vpiType)
    return vpiTypeFor(info.kind);
  if (property == vpiSize)
    return info.kind == OBELISK_RT_DESIGN_RECORD_SCOPE
               ? 0
               : static_cast<PLI_INT32>(
                     std::min<uint64_t>(info.bit_width, INT32_MAX));
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
  if (!handle || (property != vpiName && property != vpiFullName))
    return nullptr;
  if (!nameFor(handle, handle->scratch))
    return nullptr;
  if (property == vpiName) {
    size_t separator = handle->scratch.rfind('.');
    if (separator != std::string::npos)
      handle->scratch.erase(0, separator + 1);
  }
  return handle->scratch.data();
}

extern "C" OBELISK_VPI_EXPORT void vpi_get_value(vpiHandle opaque,
                                                 p_vpi_value destination) {
  __vpiHandle *handle = validate(opaque);
  if (!handle || !destination)
    return;
  obelisk_rt_design_info_v1 info{};
  std::vector<uint64_t> value;
  std::vector<uint64_t> unknown;
  if (!readValue(handle, info, value, unknown))
    return;
  switch (destination->format) {
  case vpiVectorVal: {
    if (!destination->value.vector) {
      try {
        handle->vectorScratch.resize(
            static_cast<size_t>((info.bit_width + 31) / 32));
        destination->value.vector = handle->vectorScratch.data();
      } catch (...) {
        setError(handle->owner, "VPI vector buffer is out of memory",
                 vpiSystem);
        return;
      }
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
    destination->value.integer = static_cast<PLI_INT32>(value[0] ^ unknown[0]);
    break;
  case vpiScalarVal: {
    bool v = (value[0] & 1) != 0;
    bool u = (unknown[0] & 1) != 0;
    destination->value.scalar = !u ? (v ? vpi1 : vpi0) : (v ? vpiZ : vpiX);
    break;
  }
  case vpiBinStrVal:
    try {
      handle->scratch.assign(static_cast<size_t>(info.bit_width), '0');
      for (uint64_t bit = 0; bit != info.bit_width; ++bit) {
        uint64_t mask = uint64_t{1} << (bit % 64);
        bool v = (value[bit / 64] & mask) != 0;
        bool u = (unknown[bit / 64] & mask) != 0;
        handle->scratch[info.bit_width - 1 - bit] =
            !u ? (v ? '1' : '0') : (v ? 'z' : 'x');
      }
      destination->value.str = handle->scratch.data();
    } catch (...) {
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
  std::vector<uint64_t> value, unknown;
  if (!decodeValue(handle, source, info.bit_width, value, unknown))
    return nullptr;
  obelisk_rt_status status =
      flags == vpiForceFlag
          ? obelisk_rt_v1_design_force(context, handle->cursor, value.data(),
                                       unknown.data(), info.bit_width)
          : obelisk_rt_v1_design_write(context, handle->cursor, value.data(),
                                       unknown.data(), info.bit_width);
  if (status != OBELISK_RT_OK)
    setError(handle->owner, "VPI write failed");
  return nullptr;
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32 vpi_compare_objects(vpiHandle first,
                                                            vpiHandle second) {
  __vpiHandle *left = validate(first);
  __vpiHandle *right = validate(second);
  return left && right && left->cursor.offset == right->cursor.offset;
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32 vpi_release_handle(vpiHandle opaque) {
  VPIState *state = requireState();
  auto *handle = reinterpret_cast<__vpiHandle *>(opaque);
  if (!state || !handle || state->arena.find(handle) == state->arena.end() ||
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
  return format ? std::vprintf(format, arguments) : -1;
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32 vpi_printf(PLI_BYTE8 *format, ...) {
  va_list arguments;
  va_start(arguments, format);
  int result = vpi_vprintf(format, arguments);
  va_end(arguments);
  return result;
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32 vpi_flush(void) {
  return std::fflush(nullptr);
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

extern "C" OBELISK_VPI_EXPORT vpiHandle vpi_register_cb(p_cb_data) {
  unsupportedStartup("VPI callbacks are not supported during startup");
  return nullptr;
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
  if (!info)
    return 0;
  *info = {};
  info->product = product;
  info->version = version;
  return 1;
}

extern "C" OBELISK_VPI_EXPORT vpiHandle vpi_handle_by_index(vpiHandle,
                                                            PLI_INT32) {
  setError(requireState(), "indexed VPI handles are not supported");
  return nullptr;
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32 vpi_control(PLI_INT32, ...) {
  setError(requireState(), "VPI control operations are not supported");
  return 0;
}
