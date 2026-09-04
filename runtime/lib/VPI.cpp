//===- VPI.cpp - Single-context IEEE VPI compatibility shim ---------------===//

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "RuntimeInternal.h"

#include "ProcessContext.h"
#include "VPIHandleToken.h"
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
#include <atomic>
#include <cmath>
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

struct VPIActiveSlot {
  std::atomic<VPIState *> state{nullptr};
};

enum class VPIHandleKind : uint8_t {
  Object,
  Iterator,
  Callback,
  ScheduledEvent,
  SystemTf,
  TimeQueue
};

enum class VPIObjectForm : uint8_t {
  Design,
  Indexed,
  Typespec,
  TypespecMember,
  Range,
  IntegralConstant
};

enum class VPISemanticIteratorKind : uint8_t { None, Ranges, Members, Indices };

struct VPISelectionStep {
  obelisk_rt_design_cursor_v1 physicalType{};
  obelisk_rt_design_cursor_v1 semanticType{};
  uint64_t bitOffset = 0;
  uint64_t bitWidth = 0;
  int64_t index = 0;
  uint32_t selectionOrdinal = 0;
  uint32_t exactVpiType = 0;
  bool packed = false;
  bool arrayDimension = false;
  bool aggregateBoundary = false;
  bool suppressSemanticDimension = false;
};

} // namespace

struct __vpiHandle {
  VPIState *owner = nullptr;
  uintptr_t token = 0;
  VPIHandleKind kind = VPIHandleKind::Object;
  VPIObjectForm form = VPIObjectForm::Design;
  obelisk_rt_design_cursor_v1 cursor{};
  obelisk_rt_design_cursor_v1 semanticCursor{};
  uint32_t semanticEdge = 0;
  int64_t integralValue = 0;
  uint32_t exactVpiType = 0;
  bool statement = false;
  bool suppressSemanticAlias = false;
  bool suppressSemanticDimension = false;
  // IEEE 1800-2017 37.29 detail 2 restricts value access based on the
  // traversal that produced a handle, not on the declaration's physical
  // owner.  Keep that provenance on the opaque handle itself so a direct
  // hierarchical lookup of a static class member remains usable.
  bool classDefinitionOrigin = false;
  uint32_t selectionRootType = 0;
  obelisk::reflection::VPIIndexedAccessKind selectionAccessKind =
      obelisk::reflection::VPIIndexedAccessKind::VariableElement;
  uint64_t selectionBitOffset = 0;
  std::vector<VPISelectionStep> selectionSteps;
  bool hasInfo = false;
  obelisk_rt_design_info_v1 info{};
  // Callback iteration snapshots registrations because callbacks may remove
  // peers during dispatch. Immutable design iterators use cursors or relation
  // indices directly and never populate this vector.
  std::vector<obelisk_rt_design_cursor_v1> items;
  std::vector<uint64_t> timeQueueItems;
  std::vector<VPISelectionStep> indexItems;
  bool callbackIterator = false;
  bool timeQueueIterator = false;
  bool designIterator = false;
  bool relationIterator = false;
  VPISemanticIteratorKind semanticIterator = VPISemanticIteratorKind::None;
  bool hasUse = false;
  bool useStatement = false;
  bool useSuppressSemanticAlias = false;
  bool useSuppressSemanticDimension = false;
  bool useClassDefinitionOrigin = false;
  uint32_t useType = 0;
  obelisk_rt_design_cursor_v1 useCursor{};
  VPIObjectForm useForm = VPIObjectForm::Design;
  obelisk_rt_design_cursor_v1 useSemanticCursor{};
  uint32_t useSemanticEdge = 0;
  int64_t useIntegralValue = 0;
  uint32_t useSelectionRootType = 0;
  obelisk::reflection::VPIIndexedAccessKind useSelectionAccessKind =
      obelisk::reflection::VPIIndexedAccessKind::VariableElement;
  uint64_t useSelectionBitOffset = 0;
  std::vector<VPISelectionStep> useSelectionSteps;
  obelisk::reflection::VPIObjectSetID requestedTargets{};
  VPIRelationRange relationRange{};
  size_t next = 0;
  std::string scratch;
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
  // Keep the simulation thread's binding slot alive even if that thread exits
  // before another thread destroys the context.
  std::shared_ptr<VPIActiveSlot> ownerSlot;
  std::unordered_map<uintptr_t, std::unique_ptr<__vpiHandle>> handles;
  std::string errorMessage;
  std::string errorCode;
  int errorLevel = 0;
  bool unsupportedStartup = false;
  VPIPhase phase = VPIPhase::StartupRestricted;
  uint64_t nextCallbackId = 1;
  uint32_t callbackDepth = 0;
  std::unordered_map<uint64_t, VPICallback> callbacks;
  std::vector<uint64_t> callbackOrder;
  // IEEE temporary results are invalidated by the next routine call of the
  // same family, irrespective of which object handle was used.
  std::string propertyStringScratch;
  std::string valueStringScratch;
  std::vector<s_vpi_vecval> vectorScratch;
  // Private design-read planes are reusable across ordinary snapshot queries.
  // Unlike returned value buffers these never escape the active VPI call.
  std::vector<uint64_t> readValueScratch;
  std::vector<uint64_t> readUnknownScratch;
};

// VPI is callable only from the simulation thread (startup, callbacks, and
// reentrant DPI on that thread). A process-global binding would accidentally
// authorize debugger transport/signal threads and race callback scratch state.
// The indirection lets context destruction on another thread atomically revoke
// the simulation thread's binding before freeing the context-owned state.
thread_local std::shared_ptr<VPIActiveSlot> activeSlot;
std::atomic<uintptr_t> nextHandleToken{1};

VPIState *currentState() {
  return activeSlot ? activeSlot->state.load(std::memory_order_acquire)
                    : nullptr;
}

void clearActiveState(VPIState *state) {
  if (!state || !state->ownerSlot)
    return;
  VPIState *expected = state;
  state->ownerSlot->state.compare_exchange_strong(
      expected, nullptr, std::memory_order_acq_rel, std::memory_order_acquire);
}

class ActiveStateGuard {
public:
  explicit ActiveStateGuard(VPIState *state) : state(state) {}
  ~ActiveStateGuard() { clearActiveState(state); }
  void release() { state = nullptr; }

private:
  VPIState *state;
};

#if defined(OBELISK_RT_VPI_DYNAMIC_STARTUP)
struct DynamicModuleCloser {
  void operator()(void *module) const noexcept { (void)dlclose(module); }
};
#endif

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

void beginVPICall() {
  if (VPIState *state = currentState())
    state->errorLevel = 0;
}

VPIState *requireState() {
  VPIState *state = currentState();
  if (!state)
    return nullptr;
  if (state->phase == VPIPhase::StartupRestricted ||
      state->phase == VPIPhase::BeforeEndCompile) {
    setError(state,
             "only callback and system-task registration is allowed during "
             "VPI startup");
    return nullptr;
  }
  return state;
}

__vpiHandle *findHandle(vpiHandle opaque) {
  VPIState *state = requireState();
  const uintptr_t token = reinterpret_cast<uintptr_t>(opaque);
  if (!state || !token) {
    setError(state, "invalid or released VPI handle");
    return nullptr;
  }
  auto found = state->handles.find(token);
  if (found == state->handles.end() || found->second->owner != state) {
    setError(state, "invalid or released VPI handle");
    return nullptr;
  }
  return found->second.get();
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
  uintptr_t token = 0;
  if (!obelisk::runtime::allocateVPIHandleToken(nextHandleToken, token)) {
    setError(state, "VPI handle identifier space is exhausted", vpiSystem);
    return nullptr;
  }
  handle->token = token;
  auto inserted = state->handles.try_emplace(token, std::move(handle));
  if (!inserted.second) {
    setError(state, "VPI handle identifier collision", vpiInternal);
    return nullptr;
  }
  return reinterpret_cast<vpiHandle>(token);
}

vpiHandle makeHandle(VPIState *state, obelisk_rt_design_cursor_v1 cursor,
                     uint32_t exactVpiType = 0, bool statement = false,
                     bool classDefinitionOrigin = false) {
  OBELISK_RT_TRY {
    auto handle = std::make_unique<__vpiHandle>();
    handle->owner = state;
    handle->kind = VPIHandleKind::Object;
    handle->cursor = cursor;
    handle->exactVpiType = exactVpiType;
    handle->statement = statement;
    handle->classDefinitionOrigin = classDefinitionOrigin;
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

uint32_t semanticTypespecKind(VPIState *state,
                              obelisk_rt_design_cursor_v1 semanticCursor) {
  obelisk_rt_design_semantic_type_info_v1 info{};
  if (!state || obelisk_rt_cached_design_semantic_type_info(
                    state->context, semanticCursor, &info) != OBELISK_RT_OK)
    return 0;
  return info.public_vpi_kind;
}

vpiHandle makeTypespecHandle(VPIState *state, obelisk_rt_design_cursor_v1 base,
                             obelisk_rt_design_cursor_v1 semanticCursor,
                             bool honorAlias = true,
                             bool suppressDimension = false) {
  obelisk_rt_design_semantic_type_info_v1 info{};
  if (!state || obelisk_rt_cached_design_semantic_type_info(
                    state->context, semanticCursor, &info) != OBELISK_RT_OK)
    return nullptr;
  if (honorAlias && info.alias_object.offset != 0) {
    uint32_t exactType = 0;
    if (obelisk_rt_cached_vpi_type(state->context, info.alias_object,
                                   &exactType) != OBELISK_RT_OK)
      return nullptr;
    return makeHandle(state, info.alias_object, exactType);
  }
  if (info.kind == OBELISK_RT_DESIGN_SEMANTIC_VIRTUAL_INTERFACE &&
      info.identity_target.offset != 0) {
    uint32_t exactType = 0;
    if (obelisk_rt_cached_vpi_type(state->context, info.identity_target,
                                   &exactType) != OBELISK_RT_OK ||
        exactType != vpiInterfaceTypespec)
      return nullptr;
    return makeHandle(state, info.identity_target, exactType);
  }
  uint32_t exactType = semanticTypespecKind(state, semanticCursor);
  if (exactType == 0)
    return nullptr;
  OBELISK_RT_TRY {
    auto handle = std::make_unique<__vpiHandle>();
    handle->owner = state;
    handle->kind = VPIHandleKind::Object;
    handle->form = VPIObjectForm::Typespec;
    handle->cursor = base;
    handle->semanticCursor = semanticCursor;
    handle->exactVpiType = exactType;
    handle->suppressSemanticAlias = !honorAlias;
    handle->suppressSemanticDimension = suppressDimension;
    return keepHandle(state, std::move(handle));
  }
  OBELISK_RT_CATCH_ALL {
    setError(state, "could not allocate VPI typespec handle", vpiSystem);
    return nullptr;
  }
}

vpiHandle makeSemanticObjectHandle(VPIState *state, VPIObjectForm form,
                                   obelisk_rt_design_cursor_v1 base,
                                   obelisk_rt_design_cursor_v1 semanticCursor,
                                   uint32_t semanticEdge, uint32_t exactType,
                                   int64_t integralValue = 0) {
  OBELISK_RT_TRY {
    auto handle = std::make_unique<__vpiHandle>();
    handle->owner = state;
    handle->kind = VPIHandleKind::Object;
    handle->form = form;
    handle->cursor = base;
    handle->semanticCursor = semanticCursor;
    handle->semanticEdge = semanticEdge;
    handle->exactVpiType = exactType;
    handle->integralValue = integralValue;
    return keepHandle(state, std::move(handle));
  }
  OBELISK_RT_CATCH_ALL {
    setError(state, "could not allocate derived VPI handle", vpiSystem);
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

vpiHandle makeTimeQueueHandle(VPIState *state, uint64_t scheduledTime) {
  OBELISK_RT_TRY {
    auto handle = std::make_unique<__vpiHandle>();
    handle->owner = state;
    handle->kind = VPIHandleKind::TimeQueue;
    handle->cursor.offset = scheduledTime;
    return keepHandle(state, std::move(handle));
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    setError(state, "VPI time-queue handle arena is out of memory", vpiSystem);
    return nullptr;
  }
  OBELISK_RT_CATCH_ALL {
    setError(state, "could not allocate VPI time-queue handle", vpiInternal);
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
  ++state->callbackDepth;
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
  --state->callbackDepth;
  return status;
}

PLI_INT32 removeCallbackHandle(VPIState *state, __vpiHandle *handle) {
  if (!state || !handle || handle->owner != state ||
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
  // The callback object no longer exists, so free every equivalent handle,
  // including handles returned by callback iteration.
  for (auto entry = state->handles.begin(); entry != state->handles.end();) {
    if (entry->second->kind == VPIHandleKind::Callback &&
        entry->second->cursor.offset == id)
      entry = state->handles.erase(entry);
    else
      ++entry;
  }
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

enum class VPIValueShape { Neither, Scalar, Vector };

PLI_INT32 vpiTypeForHandle(__vpiHandle *handle);

bool valueShapeFor(__vpiHandle *handle,
                   const obelisk_rt_design_info_v1 &objectInfo,
                   VPIValueShape &shape) {
  obelisk_rt_design_cursor_v1 cursor{objectInfo.type_offset};
  for (;;) {
    obelisk_rt_design_type_info_v1 type{};
    if (cursor.offset == 0 ||
        obelisk_rt_cached_design_type_info(handle->owner->context, cursor,
                                           &type) != OBELISK_RT_OK) {
      setError(handle->owner, "design type metadata lookup failed",
               vpiInternal);
      return false;
    }

    const bool packed = (type.flags & OBELISK_RT_DESIGN_TYPE_PACKED) != 0;
    if (type.kind == OBELISK_RT_DESIGN_TYPE_ARRAY && !packed) {
      cursor = type.element_type;
      continue;
    }
    if (type.kind == OBELISK_RT_DESIGN_TYPE_SCALAR) {
      // Non-packed scalar metadata represents real-valued objects. They are
      // neither scalar nor vector in the VPI net/variable diagrams.
      shape = !packed               ? VPIValueShape::Neither
              : type.bit_width == 1 ? VPIValueShape::Scalar
                                    : VPIValueShape::Vector;
      return true;
    }
    if (packed && (type.kind == OBELISK_RT_DESIGN_TYPE_ARRAY ||
                   type.kind == OBELISK_RT_DESIGN_TYPE_STRUCT ||
                   type.kind == OBELISK_RT_DESIGN_TYPE_UNION)) {
      // Packed aggregates are vectors even when their flattened width is one.
      shape = VPIValueShape::Vector;
      return true;
    }
    shape = VPIValueShape::Neither;
    return true;
  }
}

bool hasValueRequirement(
    const obelisk::reflection::VPIValuePolicyDescriptor &policy,
    obelisk::reflection::VPIValueRequirement requirement) {
  return (policy.requirements & static_cast<uint8_t>(requirement)) != 0;
}

const obelisk::reflection::VPIValuePolicyDescriptor *
valuePolicyFor(__vpiHandle *handle) {
  PLI_INT32 type = vpiTypeForHandle(handle);
  const auto *policy = type == vpiUndefined
                           ? nullptr
                           : obelisk::reflection::findVPIValuePolicy(
                                 static_cast<uint32_t>(type));
  if (!policy)
    setError(handle->owner, "vpi_get_value is not defined for this VPI object",
             vpiNotice);
  return policy;
}

bool valueRequirementsSatisfied(
    __vpiHandle *handle, const obelisk_rt_design_info_v1 &objectInfo,
    const obelisk::reflection::VPIValuePolicyDescriptor &policy) {
  using Requirement = obelisk::reflection::VPIValueRequirement;
  if (handle->form == VPIObjectForm::IntegralConstant)
    return true;
  obelisk_rt_design_type_info_v1 type{};
  if (objectInfo.type_offset == 0 ||
      obelisk_rt_cached_design_type_info(handle->owner->context,
                                         {objectInfo.type_offset},
                                         &type) != OBELISK_RT_OK) {
    setError(handle->owner, "design type metadata lookup failed", vpiInternal);
    return false;
  }
  // Inspect the physical root shape independently of its exact VPI kind.
  // Compiler-emitted databases predating semantic-kind preservation label
  // storage/net records generically as vpiReg/vpiNet; they must not thereby
  // make a whole unpacked aggregate readable.
  if ((type.flags & OBELISK_RT_DESIGN_TYPE_PACKED) == 0 &&
      (type.kind == OBELISK_RT_DESIGN_TYPE_ARRAY ||
       type.kind == OBELISK_RT_DESIGN_TYPE_STRUCT ||
       type.kind == OBELISK_RT_DESIGN_TYPE_UNION)) {
    setError(handle->owner,
             "vpi_get_value is not defined for a whole unpacked aggregate",
             vpiNotice);
    return false;
  }
  if (hasValueRequirement(policy, Requirement::RejectClassDefinitionOrigin)) {
    if (handle->classDefinitionOrigin) {
      setError(handle->owner,
               "vpi_get_value is not defined for a variable or event handle "
               "obtained from a class definition",
               vpiNotice);
      return false;
    }
  }
  // The current immutable database contains runtime design storage only: it
  // has no physical record kind for variables reached from a non-static class
  // typespec, nor for constants. Consequently the remaining two generated
  // origin/string requirements are structurally unreachable until those
  // object records are introduced; their serializer must add provenance at
  // the same time.
  return true;
}

void resolveObjectTypeValueFormat(
    const obelisk::reflection::VPIValuePolicyDescriptor &policy,
    PLI_INT32 &format) {
  using Default = obelisk::reflection::VPIValueDefaultFormat;
  if (policy.defaultFormat == Default::Real)
    format = vpiRealVal;
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
  case VPIHandleKind::TimeQueue:
    return vpiTimeQueue;
  case VPIHandleKind::Object:
    return vpiUndefined;
  }
  return vpiUndefined;
}

PLI_INT32 vpiTypeForHandle(__vpiHandle *handle) {
  if (!handle)
    return vpiUndefined;
  if (handle->kind != VPIHandleKind::Object)
    return vpiTypeForHandle(handle->kind);
  if (handle->exactVpiType != 0)
    return static_cast<PLI_INT32>(handle->exactVpiType);
  uint32_t exact = 0;
  if (obelisk_rt_cached_vpi_type(handle->owner->context, handle->cursor,
                                 &exact) == OBELISK_RT_OK &&
      exact != 0) {
    handle->exactVpiType = exact;
    return static_cast<PLI_INT32>(exact);
  }
  obelisk_rt_design_info_v1 info{};
  return infoFor(handle, info) ? vpiTypeFor(info.kind) : vpiUndefined;
}

const obelisk::reflection::VPIPropertyDescriptor *
propertyFor(__vpiHandle *handle, PLI_INT32 property) {
  PLI_INT32 type = vpiTypeForHandle(handle);
  if (type == vpiUndefined)
    return nullptr;
  const auto *descriptor = obelisk::reflection::findVPIProperty(
      static_cast<uint32_t>(type), static_cast<uint32_t>(property));
  if (!descriptor)
    setError(handle->owner, "property is not defined for this VPI object",
             vpiNotice);
  return descriptor;
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
    if (handle->form == VPIObjectForm::Indexed)
      for (const VPISelectionStep &step : handle->selectionSteps) {
        name.push_back('[');
        name += std::to_string(step.index);
        name.push_back(']');
      }
    return true;
  }
  OBELISK_RT_CATCH_ALL {
    setError(handle->owner, "could not materialize design name", vpiSystem);
    return false;
  }
}

DpiScopeHandle *timeScopeFor(__vpiHandle *handle) {
  if (!handle || handle->kind != VPIHandleKind::Object)
    return nullptr;
  obelisk_rt_design_cursor_v1 cursor = handle->cursor;
  bool statement = handle->statement;
  for (;;) {
    obelisk_rt_design_cursor_v1 parent{};
    bool parentIsStatement = false;
    obelisk_rt_status status = OBELISK_RT_OK;
    if (statement) {
      status = obelisk_rt_cached_vpi_statement_enclosing_scope(
          handle->owner->context, cursor, &parent, &parentIsStatement);
    } else {
      obelisk_rt_design_info_v1 info{};
      status =
          obelisk_rt_cached_design_info(handle->owner->context, cursor, &info);
      if (status == OBELISK_RT_OK &&
          info.kind == OBELISK_RT_DESIGN_RECORD_SCOPE)
        return obelisk_rt_find_dpi_scope(handle->owner->context,
                                         info.handle.id);
      if (status == OBELISK_RT_OK)
        status = obelisk_rt_cached_design_parent(handle->owner->context, cursor,
                                                 &parent);
    }
    if (status != OBELISK_RT_OK || parent.offset == cursor.offset)
      return nullptr;
    cursor = parent;
    statement = parentIsStatement;
  }
}

bool globalTimeExponent(VPIState *state, int32_t &exponent) {
  if (!state || !state->context || !state->context->execution) {
    setError(state, "simulation time metadata is unavailable", vpiNotice);
    return false;
  }
  exponent = state->context->execution->dpi_time_precision;
  return true;
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
    // Static reflection records and lexically anchored code units have a
    // physical scope owner only so the pointer-free database can validate and
    // reach every record. Their VPI ownership is carried exclusively by
    // generated relation records.
    if (info.kind != OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT &&
        (info.capabilities & OBELISK_RT_DESIGN_CAP_LEXICAL_ANCHOR) == 0 &&
        obelisk::reflection::vpiObjectSetContains(targets, exact)) {
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

bool isTypespecVPIKind(uint32_t type) {
  const auto *descriptor = obelisk::reflection::findVPIObjectKind(type);
  return descriptor &&
         (descriptor->families &
          obelisk::reflection::vpiFamilyMask(
              obelisk::reflection::VPIObjectFamily::Typespec)) != 0;
}

bool isSemanticObjectForm(VPIObjectForm form) {
  return form == VPIObjectForm::Typespec ||
         form == VPIObjectForm::TypespecMember ||
         form == VPIObjectForm::Range ||
         form == VPIObjectForm::IntegralConstant;
}

bool semanticCursorFor(__vpiHandle *handle,
                       obelisk_rt_design_cursor_v1 &cursor) {
  if (handle->form != VPIObjectForm::Design) {
    cursor = handle->semanticCursor;
    return cursor.offset != 0;
  }
  return obelisk_rt_cached_design_semantic_root(
             handle->owner->context, handle->cursor, &cursor) == OBELISK_RT_OK;
}

bool semanticTypeInfo(__vpiHandle *handle, obelisk_rt_design_cursor_v1 cursor,
                      obelisk_rt_design_semantic_type_info_v1 &info) {
  if (obelisk_rt_cached_design_semantic_type_info(
          handle->owner->context, cursor, &info) == OBELISK_RT_OK)
    return true;
  setError(handle->owner, "VPI semantic type metadata lookup failed",
           vpiInternal);
  return false;
}

bool semanticEdge(__vpiHandle *handle, obelisk_rt_design_cursor_v1 cursor,
                  uint32_t ordinal,
                  obelisk_rt_design_semantic_type_edge_v1 &edge) {
  if (obelisk_rt_cached_design_semantic_type_edge(
          handle->owner->context, cursor, ordinal, &edge) == OBELISK_RT_OK)
    return true;
  setError(handle->owner, "VPI semantic type edge lookup failed", vpiInternal);
  return false;
}

bool semanticElement(__vpiHandle *handle, obelisk_rt_design_cursor_v1 cursor,
                     obelisk_rt_design_cursor_v1 &element) {
  obelisk_rt_design_semantic_type_info_v1 info{};
  if (!semanticTypeInfo(handle, cursor, info))
    return false;
  for (uint32_t ordinal = 0; ordinal != info.edge_count; ++ordinal) {
    obelisk_rt_design_semantic_type_edge_v1 edge{};
    if (!semanticEdge(handle, cursor, ordinal, edge))
      return false;
    if (edge.role == OBELISK_RT_DESIGN_SEMANTIC_EDGE_ELEMENT) {
      element = edge.child;
      return true;
    }
  }
  return false;
}

bool sourceIndexOrdinal(int64_t left, int64_t right, int64_t index,
                        uint64_t &ordinal, uint64_t &extent) {
  if (left >= right) {
    if (index > left || index < right)
      return false;
    ordinal = static_cast<uint64_t>(left) - static_cast<uint64_t>(index);
    extent = static_cast<uint64_t>(left) - static_cast<uint64_t>(right) + 1;
  } else {
    if (index < left || index > right)
      return false;
    ordinal = static_cast<uint64_t>(index) - static_cast<uint64_t>(left);
    extent = static_cast<uint64_t>(right) - static_cast<uint64_t>(left) + 1;
  }
  return extent != 0 && ordinal < extent;
}

bool unpackedArrayElementCount(__vpiHandle *handle,
                               obelisk_rt_design_cursor_v1 cursor,
                               uint64_t &count) {
  count = 1;
  for (;;) {
    obelisk_rt_design_type_info_v1 type{};
    if (cursor.offset == 0 ||
        obelisk_rt_cached_design_type_info(handle->owner->context, cursor,
                                           &type) != OBELISK_RT_OK)
      return false;
    if (type.kind != OBELISK_RT_DESIGN_TYPE_ARRAY ||
        (type.flags & OBELISK_RT_DESIGN_TYPE_PACKED) != 0)
      return true;
    uint64_t ordinal = 0, extent = 0;
    if (!sourceIndexOrdinal(type.range_left, type.range_right, type.range_left,
                            ordinal, extent) ||
        extent > UINT64_MAX / count)
      return false;
    count *= extent;
    cursor = type.element_type;
  }
}

bool supportsPackedBitSelect(__vpiHandle *handle,
                             obelisk_rt_design_cursor_v1 semantic,
                             uint32_t exactType) {
  if (semantic.offset == 0) {
    switch (exactType) {
    case vpiByteVar:
    case vpiShortIntVar:
    case vpiIntVar:
    case vpiLongIntVar:
    case vpiIntegerVar:
    case vpiTimeVar:
    case vpiBitVar:
    case vpiReg:
    case vpiStructVar:
    case vpiUnionVar:
    case vpiEnumVar:
    case vpiPackedArrayVar:
    case vpiNet:
    case vpiByteNet:
    case vpiShortIntNet:
    case vpiIntNet:
    case vpiLongIntNet:
    case vpiIntegerNet:
    case vpiTimeNet:
    case vpiBitNet:
    case vpiStructNet:
    case vpiUnionNet:
    case vpiEnumNet:
    case vpiPackedArrayNet:
    case vpiInterconnectNet:
    case vpiPort:
      return true;
    default:
      return false;
    }
  }
  obelisk_rt_design_semantic_type_info_v1 info{};
  if (!semanticTypeInfo(handle, semantic, info))
    return false;
  switch (info.kind) {
  case OBELISK_RT_DESIGN_SEMANTIC_GENERIC_INTEGRAL:
  case OBELISK_RT_DESIGN_SEMANTIC_BIT:
  case OBELISK_RT_DESIGN_SEMANTIC_LOGIC:
  case OBELISK_RT_DESIGN_SEMANTIC_REG:
  case OBELISK_RT_DESIGN_SEMANTIC_BYTE:
  case OBELISK_RT_DESIGN_SEMANTIC_SHORT_INT:
  case OBELISK_RT_DESIGN_SEMANTIC_INT:
  case OBELISK_RT_DESIGN_SEMANTIC_LONG_INT:
  case OBELISK_RT_DESIGN_SEMANTIC_INTEGER:
  case OBELISK_RT_DESIGN_SEMANTIC_ENUM:
  case OBELISK_RT_DESIGN_SEMANTIC_TIME:
  case OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY:
  case OBELISK_RT_DESIGN_SEMANTIC_PACKED_STRUCT:
  case OBELISK_RT_DESIGN_SEMANTIC_PACKED_UNION:
  case OBELISK_RT_DESIGN_SEMANTIC_PACKED_OPEN_ARRAY:
    return true;
  default:
    return false;
  }
}

bool isIndexedArrayMember(const __vpiHandle *handle) {
  return handle->form == VPIObjectForm::Indexed &&
         !handle->selectionSteps.empty() &&
         handle->selectionSteps.back().arrayDimension &&
         !handle->selectionSteps.back().packed;
}

bool isIndexedPackedArrayMember(const __vpiHandle *handle,
                                uint32_t objectType) {
  if (handle->form != VPIObjectForm::Indexed ||
      handle->selectionSteps.empty() ||
      !handle->selectionSteps.back().arrayDimension ||
      !handle->selectionSteps.back().packed)
    return false;
  switch (objectType) {
  case vpiEnumNet:
  case vpiStructNet:
  case vpiPackedArrayNet:
  case vpiStructVar:
  case vpiUnionVar:
  case vpiEnumVar:
  case vpiPackedArrayVar:
    return true;
  default:
    return false;
  }
}

bool supportsIndexQuery(const __vpiHandle *handle, uint32_t objectType) {
  return objectType == vpiNetBit || objectType == vpiRegBit ||
         isIndexedArrayMember(handle) ||
         isIndexedPackedArrayMember(handle, objectType);
}

uint32_t
indexedResultType(__vpiHandle *source,
                  const obelisk::reflection::VPIIndexedAccessDescriptor &access,
                  obelisk_rt_design_cursor_v1 semanticType,
                  bool selectedPhysicalUnpackedArray, bool terminalBit) {
  if (terminalBit)
    return access.terminalResult;
  if (selectedPhysicalUnpackedArray)
    return access.unpackedFallback;
  if (access.mapSemanticType && semanticType.offset != 0) {
    uint32_t publicTypespec = semanticTypespecKind(source->owner, semanticType);
    if (const auto *mapping = obelisk::reflection::findVPIIndexedTypeResult(
            access.accessKind, publicTypespec))
      return mapping->resultType;
  }
  return access.packedFallback;
}

bool indexedInfoForSteps(__vpiHandle *source,
                         const std::vector<VPISelectionStep> &steps,
                         obelisk_rt_design_info_v1 &info) {
  if (steps.empty() ||
      obelisk_rt_cached_design_info(source->owner->context, source->cursor,
                                    &info) != OBELISK_RT_OK)
    return false;
  const VPISelectionStep &last = steps.back();
  obelisk_rt_design_type_info_v1 type{};
  if (last.physicalType.offset == 0 ||
      obelisk_rt_cached_design_type_info(
          source->owner->context, last.physicalType, &type) != OBELISK_RT_OK)
    return false;
  info.type_offset = last.physicalType.offset;
  info.bit_width = last.bitWidth;
  if (last.suppressSemanticDimension) {
    info.range_left = 0;
    info.range_right = 0;
  } else {
    info.range_left = type.range_left;
    info.range_right = type.range_right;
  }
  return true;
}

vpiHandle
makeIndexedHandle(__vpiHandle *source, uint32_t rootType,
                  obelisk::reflection::VPIIndexedAccessKind accessKind,
                  std::vector<VPISelectionStep> steps) {
  if (!source || steps.empty())
    return nullptr;
  obelisk_rt_design_info_v1 info{};
  if (!indexedInfoForSteps(source, steps, info)) {
    setError(source->owner, "indexed VPI metadata lookup failed", vpiInternal);
    return nullptr;
  }
  OBELISK_RT_TRY {
    auto handle = std::make_unique<__vpiHandle>();
    handle->owner = source->owner;
    handle->kind = VPIHandleKind::Object;
    handle->form = VPIObjectForm::Indexed;
    handle->cursor = source->cursor;
    handle->semanticCursor = steps.back().semanticType;
    handle->exactVpiType = steps.back().exactVpiType;
    handle->statement = source->statement;
    handle->classDefinitionOrigin = source->classDefinitionOrigin;
    handle->suppressSemanticDimension = steps.back().suppressSemanticDimension;
    handle->selectionRootType = rootType;
    handle->selectionAccessKind = accessKind;
    handle->selectionBitOffset = steps.back().bitOffset;
    handle->selectionSteps = std::move(steps);
    handle->hasInfo = true;
    handle->info = info;
    return keepHandle(source->owner, std::move(handle));
  }
  OBELISK_RT_CATCH_ALL {
    setError(source->owner, "could not allocate indexed VPI handle", vpiSystem);
    return nullptr;
  }
}

vpiHandle makeIndexedPrefix(__vpiHandle *source, size_t count) {
  if (!source || source->form != VPIObjectForm::Indexed ||
      count > source->selectionSteps.size())
    return nullptr;
  if (count == 0)
    return makeHandle(source->owner, source->cursor, source->selectionRootType,
                      source->statement, source->classDefinitionOrigin);
  OBELISK_RT_TRY {
    std::vector<VPISelectionStep> prefix(
        source->selectionSteps.begin(), source->selectionSteps.begin() + count);
    return makeIndexedHandle(source, source->selectionRootType,
                             source->selectionAccessKind, std::move(prefix));
  }
  OBELISK_RT_CATCH_ALL {
    setError(source->owner, "could not allocate indexed VPI prefix", vpiSystem);
    return nullptr;
  }
}

bool appendIndexedSelection(
    __vpiHandle *source, obelisk::reflection::VPIIndexedAccessKind accessKind,
    int64_t index, std::vector<VPISelectionStep> &steps) {
  obelisk_rt_design_info_v1 object{};
  if (obelisk_rt_cached_design_info(source->owner->context, source->cursor,
                                    &object) != OBELISK_RT_OK ||
      object.type_offset == 0) {
    setError(source->owner, "indexed VPI object has no physical type",
             vpiNotice);
    return false;
  }
  obelisk_rt_design_cursor_v1 physical{
      steps.empty() ? object.type_offset : steps.back().physicalType.offset};
  obelisk_rt_design_cursor_v1 semantic{};
  if (!steps.empty())
    semantic = steps.back().semanticType;
  else
    (void)obelisk_rt_cached_design_semantic_root(source->owner->context,
                                                 source->cursor, &semantic);
  if (!steps.empty() && steps.back().suppressSemanticDimension)
    return false;

  uint32_t currentType = steps.empty()
                             ? static_cast<uint32_t>(vpiTypeForHandle(source))
                             : steps.back().exactVpiType;
  const auto *access = obelisk::reflection::findVPIIndexedAccess(currentType);
  if (!access || access->accessKind != accessKind)
    return false;

  obelisk_rt_design_type_info_v1 type{};
  if (obelisk_rt_cached_design_type_info(source->owner->context, physical,
                                         &type) != OBELISK_RT_OK) {
    setError(source->owner, "indexed VPI type metadata lookup failed",
             vpiInternal);
    return false;
  }
  const bool isArray = type.kind == OBELISK_RT_DESIGN_TYPE_ARRAY;
  const bool packed = (type.flags & OBELISK_RT_DESIGN_TYPE_PACKED) != 0;
  if ((!isArray && (!packed || type.bit_width <= 1)) ||
      (isArray && type.element_type.offset == 0))
    return false;
  if (!isArray && !supportsPackedBitSelect(source, semantic, currentType))
    return false;

  uint64_t ordinal = 0, extent = 0;
  if (!sourceIndexOrdinal(type.range_left, type.range_right, index, ordinal,
                          extent))
    return false;

  obelisk_rt_design_cursor_v1 selectedPhysical = physical;
  obelisk_rt_design_type_info_v1 selectedType = type;
  uint64_t selectedWidth = 1;
  if (isArray) {
    selectedPhysical = type.element_type;
    if (obelisk_rt_cached_design_type_info(source->owner->context,
                                           selectedPhysical,
                                           &selectedType) != OBELISK_RT_OK) {
      setError(source->owner, "indexed VPI element type lookup failed",
               vpiInternal);
      return false;
    }
    selectedWidth = selectedType.bit_width;
  }
  uint64_t storageOrdinal = packed ? extent - 1 - ordinal : ordinal;
  if (selectedWidth == 0 ||
      storageOrdinal > (std::numeric_limits<uint64_t>::max() /
                        static_cast<uint64_t>(selectedWidth))) {
    setError(source->owner, "indexed VPI bit offset overflow", vpiInternal);
    return false;
  }
  uint64_t delta = storageOrdinal * selectedWidth;
  uint64_t previousOffset = steps.empty() ? 0 : steps.back().bitOffset;
  if (delta > std::numeric_limits<uint64_t>::max() - previousOffset) {
    setError(source->owner, "indexed VPI cumulative offset overflow",
             vpiInternal);
    return false;
  }

  obelisk_rt_design_cursor_v1 selectedSemantic = semantic;
  if (isArray && semantic.offset != 0) {
    obelisk_rt_design_cursor_v1 element{};
    if (!semanticElement(source, semantic, element))
      return false;
    selectedSemantic = element;
  }
  bool terminalBit = !isArray;
  if (isArray && packed && selectedWidth == 1 &&
      selectedType.kind == OBELISK_RT_DESIGN_TYPE_SCALAR) {
    uint32_t publicTypespec =
        selectedSemantic.offset == 0
            ? 0
            : semanticTypespecKind(source->owner, selectedSemantic);
    terminalBit = publicTypespec == 0 || publicTypespec == vpiBitTypespec ||
                  publicTypespec == vpiLogicTypespec;
  }
  bool selectedPhysicalUnpackedArray =
      selectedType.kind == OBELISK_RT_DESIGN_TYPE_ARRAY &&
      (selectedType.flags & OBELISK_RT_DESIGN_TYPE_PACKED) == 0;
  uint32_t resultType =
      indexedResultType(source, *access, selectedSemantic,
                        selectedPhysicalUnpackedArray, terminalBit);
  if (resultType == 0)
    return false;
  bool aggregateBoundary =
      !isArray && (type.kind == OBELISK_RT_DESIGN_TYPE_STRUCT ||
                   type.kind == OBELISK_RT_DESIGN_TYPE_UNION);
  if (steps.size() > UINT32_MAX) {
    setError(source->owner, "indexed VPI selection depth exceeds ABI",
             vpiInternal);
    return false;
  }
  steps.push_back({selectedPhysical, selectedSemantic, previousOffset + delta,
                   selectedWidth, index, static_cast<uint32_t>(steps.size()),
                   resultType, packed, isArray, aggregateBoundary,
                   terminalBit});
  return true;
}

vpiHandle handleByIndices(vpiHandle opaque, PLI_INT32 count,
                          const PLI_INT32 *indices) {
  __vpiHandle *source = validate(opaque, VPIHandleKind::Object);
  if (!source)
    return nullptr;
  if (count <= 0 || !indices) {
    setError(source->owner, "VPI indexed access requires at least one index");
    return nullptr;
  }
  if (source->form != VPIObjectForm::Design &&
      source->form != VPIObjectForm::Indexed)
    return nullptr;

  const uint32_t sourceType = static_cast<uint32_t>(vpiTypeForHandle(source));
  const auto *access = obelisk::reflection::findVPIIndexedAccess(sourceType);
  if (!access)
    return nullptr;

  uint32_t rootType = sourceType;
  auto accessKind = access->accessKind;
  std::vector<VPISelectionStep> steps;
  OBELISK_RT_TRY {
    if (source->form == VPIObjectForm::Indexed) {
      rootType = source->selectionRootType;
      accessKind = source->selectionAccessKind;
      if (access->accessKind != accessKind)
        return nullptr;
      steps = source->selectionSteps;
    }
    for (PLI_INT32 position = 0; position != count; ++position)
      if (!appendIndexedSelection(source, accessKind, indices[position], steps))
        return nullptr;

    const auto *rootAccess =
        obelisk::reflection::findVPIIndexedAccess(rootType);
    if (!rootAccess || rootAccess->accessKind != accessKind || steps.empty() ||
        !obelisk::reflection::indexedVPIResultAllowed(
            *rootAccess, steps.back().exactVpiType))
      return nullptr;
    return makeIndexedHandle(source, rootType, accessKind, std::move(steps));
  }
  OBELISK_RT_CATCH_ALL {
    setError(source->owner, "could not allocate indexed VPI selection",
             vpiSystem);
    return nullptr;
  }
}

size_t indexedParentPrefixCount(const __vpiHandle *handle) {
  if (!handle || handle->selectionSteps.empty() ||
      handle->exactVpiType == vpiPortBit)
    return 0;
  if (handle->selectionSteps.back().aggregateBoundary)
    return handle->selectionSteps.size() - 1;
  const bool packed = handle->selectionSteps.back().packed;
  size_t firstInGroup = handle->selectionSteps.size() - 1;
  while (firstInGroup != 0 &&
         handle->selectionSteps[firstInGroup - 1].packed == packed)
    --firstInGroup;
  return firstInGroup;
}

bool isUnpackedDimension(uint32_t kind) {
  return kind == OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY ||
         kind == OBELISK_RT_DESIGN_SEMANTIC_DYNAMIC_ARRAY ||
         kind == OBELISK_RT_DESIGN_SEMANTIC_QUEUE ||
         kind == OBELISK_RT_DESIGN_SEMANTIC_ASSOC_ARRAY ||
         kind == OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_OPEN_ARRAY;
}

bool isEmptyDimension(uint32_t kind) {
  return kind == OBELISK_RT_DESIGN_SEMANTIC_DYNAMIC_ARRAY ||
         kind == OBELISK_RT_DESIGN_SEMANTIC_QUEUE ||
         kind == OBELISK_RT_DESIGN_SEMANTIC_ASSOC_ARRAY ||
         kind == OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_OPEN_ARRAY;
}

bool isDimensionForTypespec(uint32_t publicType, uint32_t semanticKind,
                            uint32_t semanticFlags,
                            bool suppressDimension = false) {
  if (suppressDimension)
    return false;
  if (publicType == vpiArrayTypespec)
    return isUnpackedDimension(semanticKind);
  if (publicType == vpiBitTypespec || publicType == vpiLogicTypespec ||
      publicType == vpiPackedArrayTypespec) {
    if (semanticKind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY)
      return true;
    return (semanticFlags & OBELISK_RT_DESIGN_SEMANTIC_HAS_RANGE) != 0 &&
           (semanticKind == OBELISK_RT_DESIGN_SEMANTIC_BIT ||
            semanticKind == OBELISK_RT_DESIGN_SEMANTIC_LOGIC ||
            semanticKind == OBELISK_RT_DESIGN_SEMANTIC_REG);
  }
  return false;
}

vpiHandle makeSemanticRelation(__vpiHandle *handle, PLI_INT32 selector) {
  obelisk_rt_design_cursor_v1 semantic{};
  if (!semanticCursorFor(handle, semantic))
    return nullptr;
  obelisk_rt_design_semantic_type_info_v1 info{};
  if (!semanticTypeInfo(handle, semantic, info))
    return nullptr;
  uint32_t sourceType = static_cast<uint32_t>(vpiTypeForHandle(handle));

  if ((handle->form == VPIObjectForm::Design ||
       handle->form == VPIObjectForm::Indexed) &&
      selector == vpiTypespec && !isTypespecVPIKind(sourceType))
    return makeTypespecHandle(handle->owner, handle->cursor, semantic, true,
                              handle->suppressSemanticDimension);

  if (handle->form == VPIObjectForm::Typespec ||
      (handle->form == VPIObjectForm::Design &&
       isTypespecVPIKind(sourceType))) {
    if (!obelisk::reflection::findVPITraversal(
            sourceType, static_cast<uint32_t>(selector),
            obelisk::reflection::VPITraversalMode::Handle))
      return nullptr;
    if (selector == vpiInstance) {
      obelisk_rt_design_cursor_v1 instance{};
      uint32_t instanceType = 0;
      if (obelisk_rt_cached_design_parent(handle->owner->context,
                                          handle->cursor,
                                          &instance) != OBELISK_RT_OK ||
          obelisk_rt_cached_vpi_type(handle->owner->context, instance,
                                     &instanceType) != OBELISK_RT_OK)
        return nullptr;
      const auto *relation = obelisk::reflection::findVPITraversal(
          sourceType, vpiInstance,
          obelisk::reflection::VPITraversalMode::Handle);
      if (!relation || !obelisk::reflection::vpiObjectSetContains(
                           relation->targets, instanceType))
        return nullptr;
      return makeHandle(handle->owner, instance, instanceType);
    }
    if (selector == vpiClassDefn &&
        info.kind == OBELISK_RT_DESIGN_SEMANTIC_CLASS &&
        info.identity_target.offset != 0) {
      uint32_t targetType = 0;
      if (obelisk_rt_cached_vpi_type(handle->owner->context,
                                     info.identity_target,
                                     &targetType) != OBELISK_RT_OK)
        return nullptr;
      const auto *relation = obelisk::reflection::findVPITraversal(
          sourceType, vpiClassDefn,
          obelisk::reflection::VPITraversalMode::Handle);
      if (!relation || !obelisk::reflection::vpiObjectSetContains(
                           relation->targets, targetType))
        return nullptr;
      return makeHandle(handle->owner, info.identity_target, targetType);
    }
    if (selector == vpiTypedefAlias && !handle->suppressSemanticAlias &&
        info.alias_object.offset != 0) {
      uint32_t targetType = 0;
      if (obelisk_rt_cached_vpi_type(handle->owner->context, info.alias_object,
                                     &targetType) != OBELISK_RT_OK)
        return nullptr;
      return makeHandle(handle->owner, info.alias_object, targetType);
    }
    if (selector == vpiTypedefAlias && !handle->suppressSemanticAlias &&
        handle->form == VPIObjectForm::Design &&
        info.kind == OBELISK_RT_DESIGN_SEMANTIC_VIRTUAL_INTERFACE &&
        info.identity_target.offset != 0 &&
        info.identity_target.offset != handle->cursor.offset)
      return makeHandle(handle->owner, info.identity_target,
                        vpiInterfaceTypespec);
    if (selector == vpiTypedefAlias && !handle->suppressSemanticAlias &&
        handle->form == VPIObjectForm::Design && info.name_size == 0)
      return makeTypespecHandle(handle->owner, handle->cursor, semantic, false);
    uint32_t wantedRole = 0;
    if (selector == vpiElemTypespec &&
        (info.kind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY ||
         info.kind == OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY ||
         info.kind == OBELISK_RT_DESIGN_SEMANTIC_DYNAMIC_ARRAY ||
         info.kind == OBELISK_RT_DESIGN_SEMANTIC_QUEUE ||
         info.kind == OBELISK_RT_DESIGN_SEMANTIC_ASSOC_ARRAY ||
         info.kind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_OPEN_ARRAY ||
         info.kind == OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_OPEN_ARRAY))
      wantedRole = OBELISK_RT_DESIGN_SEMANTIC_EDGE_ELEMENT;
    else if (selector == vpiBaseTypespec &&
             info.kind == OBELISK_RT_DESIGN_SEMANTIC_ENUM)
      wantedRole = OBELISK_RT_DESIGN_SEMANTIC_EDGE_ENUM_BASE;
    else if (selector == vpiIndexTypespec &&
             info.kind == OBELISK_RT_DESIGN_SEMANTIC_ASSOC_ARRAY &&
             (info.flags & OBELISK_RT_DESIGN_SEMANTIC_WILDCARD_INDEX) == 0)
      wantedRole = OBELISK_RT_DESIGN_SEMANTIC_EDGE_ASSOC_INDEX;
    if (wantedRole != 0) {
      for (uint32_t ordinal = 0; ordinal != info.edge_count; ++ordinal) {
        obelisk_rt_design_semantic_type_edge_v1 edge{};
        if (!semanticEdge(handle, semantic, ordinal, edge))
          return nullptr;
        if (edge.role == wantedRole)
          return makeTypespecHandle(handle->owner, handle->cursor, edge.child);
      }
      return nullptr;
    }
    if (selector == vpiElemTypespec &&
        isDimensionForTypespec(sourceType, info.kind, info.flags,
                               handle->suppressSemanticDimension) &&
        (info.kind == OBELISK_RT_DESIGN_SEMANTIC_BIT ||
         info.kind == OBELISK_RT_DESIGN_SEMANTIC_LOGIC ||
         info.kind == OBELISK_RT_DESIGN_SEMANTIC_REG))
      return makeTypespecHandle(handle->owner, handle->cursor, semantic, false,
                                true);
    if ((selector == vpiLeftRange || selector == vpiRightRange) &&
        isDimensionForTypespec(sourceType, info.kind, info.flags,
                               handle->suppressSemanticDimension) &&
        !isEmptyDimension(info.kind)) {
      int64_t value =
          selector == vpiLeftRange ? info.range_left : info.range_right;
      return makeSemanticObjectHandle(
          handle->owner, VPIObjectForm::IntegralConstant, handle->cursor,
          semantic, selector == vpiLeftRange ? 0 : 1, vpiConstant, value);
    }
  }

  if (handle->form == VPIObjectForm::Range &&
      (selector == vpiLeftRange || selector == vpiRightRange) &&
      !isEmptyDimension(info.kind)) {
    int64_t value =
        selector == vpiLeftRange ? info.range_left : info.range_right;
    return makeSemanticObjectHandle(
        handle->owner, VPIObjectForm::IntegralConstant, handle->cursor,
        semantic, selector == vpiLeftRange ? 0 : 1, vpiConstant, value);
  }

  if (handle->form == VPIObjectForm::TypespecMember &&
      selector == vpiTypespec) {
    obelisk_rt_design_semantic_type_edge_v1 edge{};
    if (!semanticEdge(handle, semantic, handle->semanticEdge, edge) ||
        edge.role != OBELISK_RT_DESIGN_SEMANTIC_EDGE_MEMBER)
      return nullptr;
    return makeTypespecHandle(handle->owner, handle->cursor, edge.child);
  }
  return nullptr;
}

void copyUseRecipe(__vpiHandle &iterator, const __vpiHandle &source) {
  iterator.hasUse = true;
  iterator.useCursor = source.cursor;
  iterator.useType = static_cast<uint32_t>(
      vpiTypeForHandle(const_cast<__vpiHandle *>(&source)));
  iterator.useStatement = source.statement;
  iterator.useSuppressSemanticAlias = source.suppressSemanticAlias;
  iterator.useSuppressSemanticDimension = source.suppressSemanticDimension;
  iterator.useClassDefinitionOrigin = source.classDefinitionOrigin;
  iterator.useForm = source.form;
  iterator.useSemanticCursor = source.semanticCursor;
  iterator.useSemanticEdge = source.semanticEdge;
  iterator.useIntegralValue = source.integralValue;
  iterator.useSelectionRootType = source.selectionRootType;
  iterator.useSelectionAccessKind = source.selectionAccessKind;
  iterator.useSelectionBitOffset = source.selectionBitOffset;
  iterator.useSelectionSteps = source.selectionSteps;
}

vpiHandle makeUseHandle(__vpiHandle *iterator) {
  if (!iterator->hasUse)
    return nullptr;
  if (iterator->useForm == VPIObjectForm::Design)
    return makeHandle(iterator->owner, iterator->useCursor, iterator->useType,
                      iterator->useStatement,
                      iterator->useClassDefinitionOrigin);
  if (iterator->useForm == VPIObjectForm::Indexed) {
    OBELISK_RT_TRY {
      __vpiHandle source;
      source.owner = iterator->owner;
      source.cursor = iterator->useCursor;
      source.statement = iterator->useStatement;
      source.classDefinitionOrigin = iterator->useClassDefinitionOrigin;
      source.selectionRootType = iterator->useSelectionRootType;
      source.selectionAccessKind = iterator->useSelectionAccessKind;
      source.selectionBitOffset = iterator->useSelectionBitOffset;
      return makeIndexedHandle(&source, iterator->useSelectionRootType,
                               iterator->useSelectionAccessKind,
                               iterator->useSelectionSteps);
    }
    OBELISK_RT_CATCH_ALL {
      setError(iterator->owner, "could not copy indexed iterator use",
               vpiSystem);
      return nullptr;
    }
  }
  vpiHandle result = makeSemanticObjectHandle(
      iterator->owner, iterator->useForm, iterator->useCursor,
      iterator->useSemanticCursor, iterator->useSemanticEdge, iterator->useType,
      iterator->useIntegralValue);
  if (result) {
    __vpiHandle *handle = findHandle(result);
    if (handle)
      handle->suppressSemanticAlias = iterator->useSuppressSemanticAlias;
    if (handle)
      handle->suppressSemanticDimension =
          iterator->useSuppressSemanticDimension;
  }
  return result;
}

vpiHandle makeSemanticIterator(__vpiHandle *source, PLI_INT32 selector) {
  obelisk_rt_design_cursor_v1 semantic{};
  if (!semanticCursorFor(source, semantic))
    return nullptr;
  obelisk_rt_design_semantic_type_info_v1 info{};
  if (!semanticTypeInfo(source, semantic, info))
    return nullptr;
  VPISemanticIteratorKind kind = VPISemanticIteratorKind::None;
  uint32_t sourceType = static_cast<uint32_t>(vpiTypeForHandle(source));
  if (!obelisk::reflection::findVPITraversal(
          sourceType, static_cast<uint32_t>(selector),
          obelisk::reflection::VPITraversalMode::Iterate))
    return nullptr;
  if (selector == vpiRange &&
      isDimensionForTypespec(sourceType, info.kind, info.flags,
                             source->suppressSemanticDimension))
    kind = VPISemanticIteratorKind::Ranges;
  else if (selector == vpiTypespecMember &&
           (info.kind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_STRUCT ||
            info.kind == OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_STRUCT ||
            info.kind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_UNION ||
            info.kind == OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_UNION))
    kind = VPISemanticIteratorKind::Members;
  if (kind == VPISemanticIteratorKind::None)
    return nullptr;
  OBELISK_RT_TRY {
    auto iterator = std::make_unique<__vpiHandle>();
    iterator->owner = source->owner;
    iterator->kind = VPIHandleKind::Iterator;
    iterator->semanticIterator = kind;
    iterator->cursor = source->cursor;
    iterator->semanticCursor = semantic;
    iterator->exactVpiType = sourceType;
    iterator->suppressSemanticDimension = source->suppressSemanticDimension;
    copyUseRecipe(*iterator, *source);
    return keepHandle(source->owner, std::move(iterator));
  }
  OBELISK_RT_CATCH_ALL {
    setError(source->owner, "could not allocate semantic VPI iterator",
             vpiSystem);
    return nullptr;
  }
}

bool checkedWordCount(uint64_t width, uint64_t bitsPerWord, size_t &count) {
  if (width == 0 || bitsPerWord == 0)
    return false;
  uint64_t words = (width - 1) / bitsPerWord + 1;
  if (words > std::numeric_limits<size_t>::max())
    return false;
  count = static_cast<size_t>(words);
  return true;
}

bool isShortRealVPIType(PLI_INT32 type) {
  return type == vpiShortRealVar || type == vpiShortRealNet;
}

bool decodeRealBits(__vpiHandle *handle, PLI_INT32 type, uint64_t width,
                    uint64_t bits, double &result) {
  if (isShortRealVPIType(type)) {
    if (width != 32) {
      setError(handle->owner, "shortreal VPI object does not have 32 bits",
               vpiInternal);
      return false;
    }
    uint32_t shortBits = static_cast<uint32_t>(bits);
    float value = 0;
    std::memcpy(&value, &shortBits, sizeof(value));
    result = value;
    return true;
  }
  if (type == vpiRealVar || type == vpiRealNet) {
    if (width != 64) {
      setError(handle->owner, "real VPI object does not have 64 bits",
               vpiInternal);
      return false;
    }
    std::memcpy(&result, &bits, sizeof(result));
    return true;
  }
  setError(handle->owner,
           "vpiRealVal conversion is not implemented for non-real objects",
           vpiNotice);
  return false;
}

bool encodeRealBits(__vpiHandle *handle, PLI_INT32 type, uint64_t width,
                    double source, uint64_t &result) {
  if (isShortRealVPIType(type)) {
    if (width != 32) {
      setError(handle->owner, "shortreal VPI object does not have 32 bits",
               vpiInternal);
      return false;
    }
    float value = static_cast<float>(source);
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    result = bits;
    return true;
  }
  if (type == vpiRealVar || type == vpiRealNet) {
    if (width != 64) {
      setError(handle->owner, "real VPI object does not have 64 bits",
               vpiInternal);
      return false;
    }
    std::memcpy(&result, &source, sizeof(result));
    return true;
  }
  setError(handle->owner,
           "vpiRealVal conversion is not implemented for non-real objects",
           vpiNotice);
  return false;
}

bool readValue(__vpiHandle *handle, const obelisk_rt_design_info_v1 &info,
               std::vector<uint64_t> &value, std::vector<uint64_t> &unknown) {
  if (info.bit_width == 0 || info.kind == OBELISK_RT_DESIGN_RECORD_DRIVER) {
    setError(handle->owner,
             "VPI value access requires readable storage or net");
    return false;
  }
  size_t limbs = 0;
  if (!checkedWordCount(info.bit_width, 64, limbs)) {
    setError(handle->owner, "VPI value width exceeds host capacity", vpiSystem);
    return false;
  }
  OBELISK_RT_TRY {
    value.assign(limbs, 0);
    unknown.assign(limbs, 0);
  }
  OBELISK_RT_CATCH_ALL {
    setError(handle->owner, "VPI value buffer is out of memory", vpiSystem);
    return false;
  }
  if (handle->form == VPIObjectForm::IntegralConstant) {
    value[0] = static_cast<uint64_t>(handle->integralValue);
    return true;
  }
  if (handle->form == VPIObjectForm::Indexed) {
    if (obelisk_rt_read_design_slice(
            handle->owner->context, handle->cursor, handle->selectionBitOffset,
            info.bit_width, value.data(), unknown.data()) != OBELISK_RT_OK) {
      setError(handle->owner, "VPI indexed design read failed");
      return false;
    }
    return true;
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
                 PLI_INT32 exactType, std::vector<uint64_t> &value,
                 std::vector<uint64_t> &unknown) {
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
    case vpiRealVal:
      if (!encodeRealBits(handle, exactType, width, source->value.real,
                          value[0]))
        return false;
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
  VPIState *state = currentState();
  if (state)
    state->unsupportedStartup = true;
  setError(state, feature, vpiError, "OBELISK_VPI_UNSUPPORTED_STARTUP");
}

} // namespace

extern "C" OBELISK_VPI_EXPORT obelisk_rt_status
obelisk_rt_v1_vpi_startup(obelisk_rt_context *context,
                          const char *const *modules, uint64_t moduleCount) {
  if (!context || (moduleCount != 0 && !modules) || context->vpiState ||
      currentState())
    return OBELISK_RT_INVALID_ARGUMENT;
  if (!activeSlot) {
    OBELISK_RT_TRY { activeSlot = std::make_shared<VPIActiveSlot>(); }
    OBELISK_RT_CATCH_ALL { return OBELISK_RT_OUT_OF_MEMORY; }
  }
  if (!context->execution ||
      (context->execution->flags & OBELISK_RT_EXECUTION_VPI_READ) == 0)
    return OBELISK_RT_PERMISSION_DENIED;
  std::unique_ptr<VPIState> state;
  OBELISK_RT_TRY { state = std::make_unique<VPIState>(); }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_OUT_OF_MEMORY; }
  state->context = context;
  state->ownerSlot = activeSlot;
  activeSlot->state.store(state.get(), std::memory_order_release);
  ActiveStateGuard bindingGuard(state.get());
  for (uint64_t moduleIndex = 0; moduleIndex != moduleCount; ++moduleIndex) {
    const char *name = modules[moduleIndex];
    if (!name || !*name) {
      return OBELISK_RT_INVALID_ARGUMENT;
    }
#if !defined(OBELISK_RT_VPI_DYNAMIC_STARTUP)
    // Same status as a failed dlopen, because that is what happened from the
    // caller's side; the message carries the reason it can never succeed here.
    setError(state.get(),
             "loading VPI startup modules requires a dynamic loader, which "
             "this target does not provide",
             vpiSystem);
    return OBELISK_RT_IO_ERROR;
#else
    void *module = dlopen(name, RTLD_LAZY | RTLD_NOLOAD);
    if (!module) {
      setError(state.get(), dlerror(), vpiSystem);
      return OBELISK_RT_IO_ERROR;
    }
    std::unique_ptr<void, DynamicModuleCloser> moduleGuard(module);
    dlerror();
    void *symbol = dlsym(moduleGuard.get(), "vlog_startup_routines");
    const char *symbolError = dlerror();
    if (!symbol || symbolError) {
      setError(state.get(),
               symbolError ? symbolError : "VPI startup table is missing",
               vpiSystem);
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
      return OBELISK_RT_INVALID_DESIGN;
    }
    auto *routines = static_cast<void (**)(void)>(symbol);
    bool terminated = false;
    bool routineFailed = false;
    for (size_t index = 0; index != entries; ++index) {
      if (!routines[index]) {
        terminated = true;
        break;
      }
      OBELISK_RT_TRY { routines[index](); }
      OBELISK_RT_CATCH_ALL {
        setError(state.get(), "VPI startup routine raised an exception",
                 vpiInternal);
        routineFailed = true;
        break;
      }
      if (state->unsupportedStartup)
        break;
    }
    if (!terminated || routineFailed || state->unsupportedStartup) {
      if (!terminated && !routineFailed)
        setError(state.get(), "VPI startup table is not null terminated");
      return OBELISK_RT_INVALID_DESIGN;
    }
#endif
  }
  state->phase = VPIPhase::BeforeEndCompile;
  // Ownership moves only after all startup modules have succeeded.
  context->vpiState = state.release();
  bindingGuard.release();
  return OBELISK_RT_OK;
}

extern "C" OBELISK_VPI_EXPORT obelisk_rt_status
obelisk_rt_v1_vpi_end_compile(obelisk_rt_context *context) {
  if (!context || context->vpiState != currentState())
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
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
  if (!context || context->vpiState != currentState())
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
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
  if (!context || context->vpiState != currentState())
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
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
  if (!context || !context->vpiState)
    return;
  auto *state = static_cast<VPIState *>(context->vpiState);
  if (state->callbackDepth != 0) {
    setError(state, "VPI shutdown is not allowed from a VPI callback");
    return;
  }
  context->vpiState = nullptr;
  clearActiveState(state);
  delete state;
}

extern "C" OBELISK_VPI_EXPORT vpiHandle vpi_handle_by_name(PLI_BYTE8 *name,
                                                           vpiHandle scope) {
  beginVPICall();
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
  uint32_t sourceType = 0;
  bool sourceClassDefinitionOrigin = false;
  std::string compatibilityName;
  if (scope && !absolute) {
    __vpiHandle *base = validate(scope);
    if (!base)
      return nullptr;
    sourceType = static_cast<uint32_t>(vpiTypeForHandle(base));
    sourceClassDefinitionOrigin = base->classDefinitionOrigin;
    std::string prefix;
    if (!nameFor(base, prefix))
      return nullptr;
    if (prefix != "$root" && prefix != "\\$root ") {
      if (sourceType == vpiPackage) {
        if (prefix.size() < 2 ||
            prefix.compare(prefix.size() - 2, 2, "::") != 0)
          prefix.append("::");
        requested = prefix + requested;
      } else if (sourceType == vpiClassDefn) {
        compatibilityName = prefix + "." + requested;
        requested = prefix + "::" + requested;
      } else {
        requested = prefix + "." + requested;
      }
    }
  }
  obelisk_rt_design_cursor_v1 cursor{};
  bool found = lookup(state, requested, cursor);
  if (!found && !compatibilityName.empty())
    found = lookup(state, compatibilityName, cursor);
  if (!found && (!scope || absolute) &&
      (requested.size() < 2 ||
       requested.compare(requested.size() - 2, 2, "::") != 0))
    found = lookup(state, requested + "::", cursor);
  if (!found) {
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
  return makeHandle(state, cursor, exactType, false,
                    obelisk::runtime::hasClassDefinitionValueOrigin(
                        sourceType, sourceClassDefinitionOrigin, exactType));
}

extern "C" OBELISK_VPI_EXPORT vpiHandle vpi_handle(PLI_INT32 type,
                                                   vpiHandle reference) {
  beginVPICall();
  __vpiHandle *handle = findHandle(reference);
  if (!handle)
    return nullptr;
  if (handle->kind == VPIHandleKind::Iterator) {
    if (type != vpiUse)
      return nullptr;
    return makeUseHandle(handle);
  }
  if (handle->kind == VPIHandleKind::TimeQueue)
    return nullptr;
  if (handle->kind != VPIHandleKind::Object) {
    setError(handle->owner, "wrong-kind VPI handle");
    return nullptr;
  }
  uint32_t sourceType = handle->exactVpiType;
  if (sourceType == 0 &&
      obelisk_rt_cached_vpi_type(handle->owner->context, handle->cursor,
                                 &sourceType) != OBELISK_RT_OK)
    return nullptr;
  if (handle->form == VPIObjectForm::Indexed && type == vpiIndex) {
    const auto *edge = obelisk::reflection::findVPITraversal(
        sourceType, vpiIndex, obelisk::reflection::VPITraversalMode::Handle);
    if (!edge || !supportsIndexQuery(handle, sourceType))
      return nullptr;
    const VPISelectionStep &step = handle->selectionSteps.back();
    return makeSemanticObjectHandle(
        handle->owner, VPIObjectForm::IntegralConstant, handle->cursor,
        step.semanticType, step.selectionOrdinal, vpiConstant, step.index);
  }
  if (handle->form == VPIObjectForm::Indexed && type == vpiParent) {
    const auto *edge = obelisk::reflection::findVPITraversal(
        sourceType, vpiParent, obelisk::reflection::VPITraversalMode::Handle);
    if (!edge || handle->selectionSteps.empty())
      return nullptr;
    if (handle->exactVpiType == vpiPortBit)
      return makeIndexedPrefix(handle, 0);
    return makeIndexedPrefix(handle, indexedParentPrefixCount(handle));
  }
  VPIRelationRange range{};
  obelisk_rt_status relationStatus =
      handle->form == VPIObjectForm::Design
          ? obelisk_rt_cached_vpi_relation_range(
                handle->owner->context, handle->cursor,
                static_cast<uint32_t>(type), false, &range)
          : OBELISK_RT_EOF;
  if (relationStatus == OBELISK_RT_OK) {
    obelisk_rt_design_cursor_v1 target{};
    uint32_t targetType = 0;
    bool targetIsStatement = false;
    if (obelisk_rt_cached_vpi_relation_target(
            handle->owner->context, range.first, &target, &targetType,
            &targetIsStatement) != OBELISK_RT_OK)
      return nullptr;
    return makeHandle(
        handle->owner, target, targetType, targetIsStatement,
        obelisk::runtime::hasClassDefinitionValueOrigin(
            sourceType, handle->classDefinitionOrigin, targetType));
  }
  if (relationStatus != OBELISK_RT_EOF)
    return nullptr;
  if ((type == vpiLeftRange || type == vpiRightRange) &&
      (handle->form == VPIObjectForm::Design ||
       handle->form == VPIObjectForm::Indexed)) {
    const auto *edge = obelisk::reflection::findVPITraversal(
        sourceType, static_cast<uint32_t>(type),
        obelisk::reflection::VPITraversalMode::Handle);
    obelisk_rt_design_info_v1 info{};
    if (edge && infoFor(handle, info)) {
      int64_t value = type == vpiLeftRange ? info.range_left : info.range_right;
      return makeSemanticObjectHandle(
          handle->owner, VPIObjectForm::IntegralConstant, handle->cursor,
          handle->semanticCursor, type == vpiLeftRange ? 0 : 1, vpiConstant,
          value);
    }
  }
  if (vpiHandle semantic = makeSemanticRelation(handle, type))
    return semantic;
  const auto *edge = obelisk::reflection::findVPITraversal(
      sourceType, static_cast<uint32_t>(type),
      obelisk::reflection::VPITraversalMode::Handle);
  if (edge && edge->automaticRelation ==
                  obelisk::reflection::VPIAutomaticRelation::ParentScope) {
    obelisk_rt_design_cursor_v1 parent{};
    uint32_t parentType = 0;
    bool parentIsStatement = false;
    obelisk_rt_status parentStatus =
        handle->statement
            ? obelisk_rt_cached_vpi_statement_enclosing_scope(
                  handle->owner->context, handle->cursor, &parent,
                  &parentIsStatement)
            : obelisk_rt_cached_design_parent(handle->owner->context,
                                              handle->cursor, &parent);
    if (parentStatus != OBELISK_RT_OK ||
        obelisk_rt_cached_vpi_type(handle->owner->context, parent,
                                   &parentType) != OBELISK_RT_OK ||
        !obelisk::reflection::vpiObjectSetContains(edge->targets, parentType))
      return nullptr;
    return makeHandle(
        handle->owner, parent, parentType, parentIsStatement,
        obelisk::runtime::hasClassDefinitionValueOrigin(
            sourceType, handle->classDefinitionOrigin, parentType));
  }
  // Generated traversal legality is authoritative for statement objects.
  // Helper/container statement kinds deliberately have no vpiScope edge and
  // must not fall through to the name-based hierarchy compatibility path.
  if (handle->statement)
    return nullptr;
  if (type != vpiScope)
    return nullptr;
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
  beginVPICall();
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
  if (type == vpiTimeQueue && !reference) {
    OBELISK_RT_TRY {
      std::vector<uint64_t> times;
      {
        ContextMutexLock lock(state->context);
        obelisk_rt_status status =
            obelisk_rt_snapshot_future_time_queues_unlocked(state->context,
                                                            times);
        if (status != OBELISK_RT_OK) {
          setError(state,
                   status == OBELISK_RT_OUT_OF_MEMORY
                       ? "could not allocate VPI time-queue snapshot"
                       : "could not inspect scheduler time queues",
                   status == OBELISK_RT_OUT_OF_MEMORY ? vpiSystem
                                                      : vpiInternal);
          return nullptr;
        }
        if ((state->phase == VPIPhase::StartSimulation ||
             state->phase == VPIPhase::Running) &&
            obelisk_rt_current_time_queue_pending_unlocked(state->context))
          times.insert(times.begin(), state->context->schedulerTime);
      }
      if (times.empty())
        return nullptr;
      auto iterator = std::make_unique<__vpiHandle>();
      iterator->owner = state;
      iterator->kind = VPIHandleKind::Iterator;
      iterator->timeQueueItems = std::move(times);
      iterator->timeQueueIterator = true;
      return keepHandle(state, std::move(iterator));
    }
    OBELISK_RT_CATCH(const std::bad_alloc &) {
      setError(state, "could not allocate VPI time-queue iterator", vpiSystem);
      return nullptr;
    }
    OBELISK_RT_CATCH_ALL {
      setError(state, "could not create VPI time-queue iterator", vpiInternal);
      return nullptr;
    }
  }
  obelisk_rt_design_cursor_v1 parent{};
  uint32_t sourceType = 0;
  bool sourceStatement = false;
  bool sourceClassDefinitionOrigin = false;
  bool sourceDesign = true;
  if (reference) {
    __vpiHandle *handle = validate(reference);
    if (!handle)
      return nullptr;
    parent = handle->cursor;
    sourceStatement = handle->statement;
    sourceClassDefinitionOrigin = handle->classDefinitionOrigin;
    sourceDesign = handle->form == VPIObjectForm::Design;
    sourceType = handle->exactVpiType;
    if (sourceType == 0 &&
        obelisk_rt_cached_vpi_type(state->context, parent, &sourceType) !=
            OBELISK_RT_OK)
      return nullptr;
    if (type == vpiIndex && handle->form == VPIObjectForm::Indexed) {
      const auto *edge = obelisk::reflection::findVPITraversal(
          sourceType, vpiIndex, obelisk::reflection::VPITraversalMode::Iterate);
      if (!edge || !supportsIndexQuery(handle, sourceType))
        return nullptr;
      OBELISK_RT_TRY {
        auto iterator = std::make_unique<__vpiHandle>();
        iterator->owner = state;
        iterator->kind = VPIHandleKind::Iterator;
        iterator->semanticIterator = VPISemanticIteratorKind::Indices;
        iterator->cursor = handle->cursor;
        iterator->semanticCursor = handle->semanticCursor;
        iterator->exactVpiType = sourceType;
        size_t first = sourceType == vpiNetBit || sourceType == vpiRegBit
                           ? 0
                           : indexedParentPrefixCount(handle);
        iterator->indexItems.reserve(handle->selectionSteps.size() - first);
        for (size_t index = handle->selectionSteps.size(); index != first;)
          iterator->indexItems.push_back(handle->selectionSteps[--index]);
        copyUseRecipe(*iterator, *handle);
        return keepHandle(state, std::move(iterator));
      }
      OBELISK_RT_CATCH_ALL {
        setError(state, "could not allocate VPI index iterator", vpiSystem);
        return nullptr;
      }
    }
  } else if (obelisk_rt_cached_design_root(state->context, &parent) !=
             OBELISK_RT_OK) {
    return nullptr;
  }
  VPIRelationRange range{};
  obelisk_rt_status relationStatus =
      !reference || sourceDesign
          ? obelisk_rt_cached_vpi_relation_range(state->context, parent,
                                                 static_cast<uint32_t>(type),
                                                 true, &range)
          : OBELISK_RT_EOF;
  if (relationStatus == OBELISK_RT_OK) {
    OBELISK_RT_TRY {
      auto iterator = std::make_unique<__vpiHandle>();
      iterator->owner = state;
      iterator->kind = VPIHandleKind::Iterator;
      iterator->relationIterator = true;
      iterator->relationRange = range;
      if (reference) {
        iterator->hasUse = true;
        iterator->useCursor = parent;
        iterator->useType = sourceType;
        iterator->useStatement = sourceStatement;
        iterator->useClassDefinitionOrigin = sourceClassDefinitionOrigin;
        iterator->classDefinitionOrigin = sourceClassDefinitionOrigin;
      }
      return keepHandle(state, std::move(iterator));
    }
    OBELISK_RT_CATCH_ALL {
      setError(state, "could not allocate VPI relation iterator", vpiSystem);
      return nullptr;
    }
  }
  if (relationStatus != OBELISK_RT_EOF)
    return nullptr;
  if (reference) {
    __vpiHandle *source = validate(reference);
    if (!source)
      return nullptr;
    if (vpiHandle semantic = makeSemanticIterator(source, type))
      return semantic;
  }
  if (reference && sourceType == 0)
    return nullptr;
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
    if (reference) {
      iterator->hasUse = true;
      iterator->useCursor = parent;
      iterator->useType = sourceType;
      iterator->useStatement = sourceStatement;
      iterator->useClassDefinitionOrigin = sourceClassDefinitionOrigin;
      iterator->classDefinitionOrigin = sourceClassDefinitionOrigin;
    }
    return keepHandle(state, std::move(iterator));
  }
  OBELISK_RT_CATCH_ALL {
    setError(state, "could not allocate VPI iterator", vpiSystem);
    return nullptr;
  }
}

extern "C" OBELISK_VPI_EXPORT vpiHandle vpi_scan(vpiHandle opaque) {
  beginVPICall();
  __vpiHandle *iterator = validate(opaque, VPIHandleKind::Iterator);
  if (!iterator)
    return nullptr;
  if (iterator->callbackIterator) {
    while (iterator->next != iterator->items.size()) {
      uint64_t id = iterator->items[iterator->next++].offset;
      if (iterator->owner->callbacks.find(id) !=
          iterator->owner->callbacks.end()) {
        VPIState *state = iterator->owner;
        const uintptr_t iteratorToken = iterator->token;
        vpiHandle result = makeCallbackHandle(state, id);
        if (!result)
          state->handles.erase(iteratorToken);
        return result;
      }
    }
    iterator->owner->handles.erase(iterator->token);
    return nullptr;
  }
  if (iterator->timeQueueIterator) {
    if (iterator->next == iterator->timeQueueItems.size()) {
      iterator->owner->handles.erase(iterator->token);
      return nullptr;
    }
    VPIState *state = iterator->owner;
    const uintptr_t iteratorToken = iterator->token;
    vpiHandle result =
        makeTimeQueueHandle(state, iterator->timeQueueItems[iterator->next++]);
    if (!result)
      state->handles.erase(iteratorToken);
    return result;
  }
  if (iterator->semanticIterator != VPISemanticIteratorKind::None) {
    VPIState *state = iterator->owner;
    const uintptr_t iteratorToken = iterator->token;
    if (iterator->semanticIterator == VPISemanticIteratorKind::Indices) {
      if (iterator->next == iterator->indexItems.size()) {
        state->handles.erase(iteratorToken);
        return nullptr;
      }
      const size_t reverseOrdinal = iterator->next++;
      const VPISelectionStep &step = iterator->indexItems[reverseOrdinal];
      vpiHandle result = makeSemanticObjectHandle(
          state, VPIObjectForm::IntegralConstant, iterator->cursor,
          step.semanticType, step.selectionOrdinal, vpiConstant, step.index);
      if (!result)
        state->handles.erase(iteratorToken);
      return result;
    }
    if (iterator->semanticIterator == VPISemanticIteratorKind::Ranges) {
      if (iterator->semanticCursor.offset == 0) {
        state->handles.erase(iteratorToken);
        return nullptr;
      }
      obelisk_rt_design_semantic_type_info_v1 info{};
      if (!semanticTypeInfo(iterator, iterator->semanticCursor, info) ||
          !isDimensionForTypespec(iterator->exactVpiType, info.kind, info.flags,
                                  iterator->suppressSemanticDimension)) {
        state->handles.erase(iteratorToken);
        return nullptr;
      }
      obelisk_rt_design_cursor_v1 current = iterator->semanticCursor;
      obelisk_rt_design_cursor_v1 element{};
      if (semanticElement(iterator, current, element)) {
        obelisk_rt_design_semantic_type_info_v1 elementInfo{};
        if (obelisk_rt_cached_design_semantic_type_info(
                state->context, element, &elementInfo) == OBELISK_RT_OK &&
            isDimensionForTypespec(iterator->exactVpiType, elementInfo.kind,
                                   elementInfo.flags))
          iterator->semanticCursor = element;
        else
          iterator->semanticCursor = {};
      } else {
        iterator->semanticCursor = {};
      }
      vpiHandle result = makeSemanticObjectHandle(
          state, VPIObjectForm::Range, iterator->cursor, current, 0, vpiRange);
      if (!result)
        state->handles.erase(iteratorToken);
      return result;
    }
    obelisk_rt_design_semantic_type_info_v1 info{};
    if (!semanticTypeInfo(iterator, iterator->semanticCursor, info) ||
        iterator->next >= info.edge_count) {
      state->handles.erase(iteratorToken);
      return nullptr;
    }
    obelisk_rt_design_semantic_type_edge_v1 edge{};
    uint32_t edgeIndex = static_cast<uint32_t>(iterator->next++);
    if (!semanticEdge(iterator, iterator->semanticCursor, edgeIndex, edge) ||
        edge.role != OBELISK_RT_DESIGN_SEMANTIC_EDGE_MEMBER) {
      state->handles.erase(iteratorToken);
      return nullptr;
    }
    vpiHandle result = makeSemanticObjectHandle(
        state, VPIObjectForm::TypespecMember, iterator->cursor,
        iterator->semanticCursor, edgeIndex, vpiTypespecMember);
    if (!result)
      state->handles.erase(iteratorToken);
    return result;
  }
  if (iterator->relationIterator) {
    if (iterator->next == iterator->relationRange.count) {
      iterator->owner->handles.erase(iterator->token);
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
      VPIState *state = iterator->owner;
      state->handles.erase(iterator->token);
      setError(state, "VPI relation target lookup failed", vpiInternal);
      return nullptr;
    }
    ++iterator->next;
    VPIState *state = iterator->owner;
    const uintptr_t iteratorToken = iterator->token;
    vpiHandle result = makeHandle(
        state, target, targetType, targetIsStatement,
        obelisk::runtime::hasClassDefinitionValueOrigin(
            iterator->useType, iterator->classDefinitionOrigin, targetType));
    if (!result)
      state->handles.erase(iteratorToken);
    return result;
  }
  if (iterator->designIterator) {
    if (iterator->cursor.offset == 0) {
      iterator->owner->handles.erase(iterator->token);
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
    VPIState *state = iterator->owner;
    const uintptr_t iteratorToken = iterator->token;
    if (status != OBELISK_RT_OK && status != OBELISK_RT_EOF) {
      state->handles.erase(iteratorToken);
      setError(state, "VPI design iterator lookup failed", vpiInternal);
      return nullptr;
    }
    uint32_t targetType = 0;
    if (obelisk_rt_cached_vpi_type(state->context, current, &targetType) !=
        OBELISK_RT_OK) {
      state->handles.erase(iteratorToken);
      setError(state, "VPI design iterator target type lookup failed",
               vpiInternal);
      return nullptr;
    }
    vpiHandle result = makeHandle(
        state, current, targetType, false,
        obelisk::runtime::hasClassDefinitionValueOrigin(
            iterator->useType, iterator->classDefinitionOrigin, targetType));
    if (!result)
      state->handles.erase(iteratorToken);
    return result;
  }
  if (iterator->next == iterator->items.size()) {
    iterator->owner->handles.erase(iterator->token);
    return nullptr;
  }
  VPIState *state = iterator->owner;
  const uintptr_t iteratorToken = iterator->token;
  vpiHandle result = makeHandle(state, iterator->items[iterator->next++]);
  if (!result)
    state->handles.erase(iteratorToken);
  return result;
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32 vpi_get(PLI_INT32 property,
                                                vpiHandle opaque) {
  beginVPICall();
  if (!opaque && (property == vpiTimeUnit || property == vpiTimePrecision)) {
    VPIState *state = requireState();
    int32_t exponent = 0;
    return globalTimeExponent(state, exponent) ? exponent : vpiUndefined;
  }
  __vpiHandle *handle = findHandle(opaque);
  if (!handle)
    return vpiUndefined;
  if (property == vpiType)
    return vpiTypeForHandle(handle);
  uint32_t objectType = static_cast<uint32_t>(vpiTypeForHandle(handle));
  if (handle->kind == VPIHandleKind::Object &&
      handle->form == VPIObjectForm::IntegralConstant) {
    if (!propertyFor(handle, property))
      return vpiUndefined;
    if (property == vpiSize)
      return 64;
    if (property == vpiConstType)
      return vpiIntConst;
    setError(handle->owner, "unsupported integral-constant VPI property",
             vpiNotice);
    return vpiUndefined;
  }
  if (handle->kind == VPIHandleKind::Object &&
      (property == vpiArrayMember || property == vpiPackedArrayMember ||
       property == vpiConstantSelect || property == vpiSigned)) {
    if (!propertyFor(handle, property))
      return vpiUndefined;
    if (property == vpiArrayMember)
      return isIndexedArrayMember(handle);
    if (property == vpiPackedArrayMember)
      return isIndexedPackedArrayMember(handle, objectType);
    if (property == vpiConstantSelect)
      return handle->form == VPIObjectForm::Design ||
             handle->form == VPIObjectForm::Indexed;
    if (objectType == vpiNetBit || objectType == vpiRegBit)
      return 0;
    if (handle->semanticCursor.offset != 0) {
      obelisk_rt_design_semantic_type_info_v1 semantic{};
      if (obelisk_rt_cached_design_semantic_type_info(
              handle->owner->context, handle->semanticCursor, &semantic) ==
          OBELISK_RT_OK)
        return (semantic.flags & OBELISK_RT_DESIGN_SEMANTIC_SIGNED) != 0;
    }
    obelisk_rt_design_info_v1 info{};
    if (!infoFor(handle, info) || info.type_offset == 0)
      return vpiUndefined;
    obelisk_rt_design_type_info_v1 type{};
    if (obelisk_rt_cached_design_type_info(
            handle->owner->context, {info.type_offset}, &type) != OBELISK_RT_OK)
      return vpiUndefined;
    return (type.flags & OBELISK_RT_DESIGN_TYPE_SIGNED) != 0;
  }
  if (handle->kind == VPIHandleKind::Object &&
      (isSemanticObjectForm(handle->form) || isTypespecVPIKind(objectType)) &&
      property != vpiLineNo) {
    if (!propertyFor(handle, property))
      return vpiUndefined;
    obelisk_rt_design_cursor_v1 semantic{};
    if (!semanticCursorFor(handle, semantic))
      return vpiUndefined;
    obelisk_rt_design_semantic_type_info_v1 info{};
    if (!semanticTypeInfo(handle, semantic, info))
      return vpiUndefined;
    auto extent = [&]() -> uint64_t {
      if (isEmptyDimension(info.kind))
        return 0;
      uint64_t distance = info.range_left >= info.range_right
                              ? static_cast<uint64_t>(info.range_left) -
                                    static_cast<uint64_t>(info.range_right)
                              : static_cast<uint64_t>(info.range_right) -
                                    static_cast<uint64_t>(info.range_left);
      return distance == UINT64_MAX ? UINT64_MAX : distance + 1;
    };
    switch (property) {
    case vpiSize: {
      uint64_t size = 0;
      if (handle->form == VPIObjectForm::IntegralConstant)
        size = 64;
      else if (handle->form == VPIObjectForm::Range)
        size = extent();
      else if (info.bit_width != 0)
        size = info.bit_width;
      else if ((info.flags & OBELISK_RT_DESIGN_SEMANTIC_HAS_RANGE) != 0)
        size = extent();
      return static_cast<PLI_INT32>(std::min<uint64_t>(size, INT32_MAX));
    }
    case vpiPacked:
      return info.kind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_STRUCT ||
             info.kind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_UNION;
    case vpiTagged:
      return (info.flags & OBELISK_RT_DESIGN_SEMANTIC_TAGGED) != 0;
    case vpiVector:
      return isDimensionForTypespec(objectType, info.kind, info.flags,
                                    handle->suppressSemanticDimension);
    case vpiArrayType:
      if (info.kind == OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY)
        return vpiStaticArray;
      if (info.kind == OBELISK_RT_DESIGN_SEMANTIC_DYNAMIC_ARRAY)
        return vpiDynamicArray;
      if (info.kind == OBELISK_RT_DESIGN_SEMANTIC_ASSOC_ARRAY)
        return vpiAssocArray;
      if (info.kind == OBELISK_RT_DESIGN_SEMANTIC_QUEUE)
        return vpiQueueArray;
      return vpiUndefined;
    case vpiRandType:
      if (handle->form == VPIObjectForm::TypespecMember) {
        obelisk_rt_design_semantic_type_edge_v1 edge{};
        if (!semanticEdge(handle, handle->semanticCursor, handle->semanticEdge,
                          edge))
          return vpiUndefined;
        if (edge.flags >= OBELISK_RT_DESIGN_SEMANTIC_EDGE_NOT_RANDOM &&
            edge.flags <= OBELISK_RT_DESIGN_SEMANTIC_EDGE_RANDOM_CYCLIC)
          return static_cast<PLI_INT32>(edge.flags);
        setError(handle->owner,
                 "typespec member has invalid randomization metadata",
                 vpiInternal);
        return vpiUndefined;
      }
      return vpiUndefined;
    case vpiConstType:
      return vpiIntConst;
    case vpiIsProtected:
      return 0;
    default:
      setError(handle->owner, "unsupported semantic VPI property", vpiNotice);
      return vpiUndefined;
    }
  }
  if (property == vpiIsProtected)
    return propertyFor(handle, property) ? 0 : vpiUndefined;
  if (handle->kind != VPIHandleKind::Object) {
    setError(handle->owner, "unsupported property for VPI handle kind",
             vpiNotice);
    return vpiUndefined;
  }
  if (property == vpiSize) {
    if (handle->statement)
      return 0;
    obelisk_rt_design_info_v1 info{};
    if (!infoFor(handle, info))
      return vpiUndefined;
    uint32_t exactType = static_cast<uint32_t>(vpiTypeForHandle(handle));
    if ((exactType == vpiRegArray || exactType == vpiNetArray ||
         exactType == vpiInterconnectArray) &&
        info.type_offset != 0) {
      obelisk_rt_design_type_info_v1 type{};
      if (obelisk_rt_cached_design_type_info(handle->owner->context,
                                             {info.type_offset},
                                             &type) != OBELISK_RT_OK)
        return vpiUndefined;
      if (type.kind == OBELISK_RT_DESIGN_TYPE_ARRAY &&
          (type.flags & OBELISK_RT_DESIGN_TYPE_PACKED) == 0) {
        uint64_t count = 0;
        if (!unpackedArrayElementCount(handle, {info.type_offset}, count)) {
          setError(handle->owner, "invalid unpacked-array VPI size metadata",
                   vpiInternal);
          return vpiUndefined;
        }
        return static_cast<PLI_INT32>(std::min<uint64_t>(count, INT32_MAX));
      }
    }
    return info.kind == OBELISK_RT_DESIGN_RECORD_SCOPE
               ? 0
               : static_cast<PLI_INT32>(
                     std::min<uint64_t>(info.bit_width, INT32_MAX));
  }
  if (property == vpiTimeUnit || property == vpiTimePrecision) {
    if (!propertyFor(handle, property))
      return vpiUndefined;
    DpiScopeHandle *scope = timeScopeFor(handle);
    if (!scope) {
      setError(handle->owner, "VPI object timescale metadata is unavailable",
               vpiNotice);
      return vpiUndefined;
    }
    return property == vpiTimeUnit ? scope->timeUnit : scope->timePrecision;
  }
  if (property == vpiDirection || property == vpiPortIndex ||
      property == vpiPortType || property == vpiScalar ||
      property == vpiVector) {
    uint32_t exactType = handle->exactVpiType;
    if (exactType == 0 &&
        obelisk_rt_cached_vpi_type(handle->owner->context, handle->cursor,
                                   &exactType) == OBELISK_RT_OK)
      handle->exactVpiType = exactType;
    if (!obelisk::reflection::findVPIProperty(exactType, property)) {
      setError(handle->owner, "property is not defined for this VPI object",
               vpiNotice);
      return vpiUndefined;
    }
    obelisk_rt_design_info_v1 info{};
    if (!infoFor(handle, info))
      return vpiUndefined;
    const bool isPort = info.kind == OBELISK_RT_DESIGN_RECORD_PORT;
    if (property == vpiDirection) {
      if (!isPort) {
        setError(handle->owner, "port property requested for non-port object",
                 vpiNotice);
        return vpiUndefined;
      }
      bool input = (info.capabilities & OBELISK_RT_DESIGN_CAP_PORT_INPUT) != 0;
      bool output =
          (info.capabilities & OBELISK_RT_DESIGN_CAP_PORT_OUTPUT) != 0;
      if (input && output)
        return vpiInout;
      return input ? vpiInput : vpiOutput;
    }
    if (property == vpiPortIndex) {
      if (!isPort) {
        setError(handle->owner, "port property requested for non-port object",
                 vpiNotice);
        return vpiUndefined;
      }
      return static_cast<PLI_INT32>(
          (info.capabilities & OBELISK_RT_DESIGN_CAP_PORT_ORDINAL_MASK) >>
          OBELISK_RT_DESIGN_CAP_PORT_ORDINAL_SHIFT);
    }
    if (property == vpiPortType) {
      if (!isPort) {
        setError(handle->owner, "port property requested for non-port object",
                 vpiNotice);
        return vpiUndefined;
      }
      return vpiPort;
    }
    if (isPort) {
      bool scalar = info.bit_width == 1;
      return property == vpiScalar ? static_cast<PLI_INT32>(scalar)
                                   : static_cast<PLI_INT32>(!scalar);
    }
    if (exactType == vpiNetBit || exactType == vpiRegBit)
      return property == vpiScalar;
    VPIValueShape shape = VPIValueShape::Neither;
    if (!valueShapeFor(handle, info, shape))
      return vpiUndefined;
    return property == vpiScalar
               ? static_cast<PLI_INT32>(shape == VPIValueShape::Scalar)
               : static_cast<PLI_INT32>(shape == VPIValueShape::Vector);
  }
  if (property == vpiLineNo || property == vpiDefLineNo) {
    if (property == vpiLineNo && !propertyFor(handle, property))
      return vpiUndefined;
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
                                                  vpiHandle opaque) {
  beginVPICall();
  __vpiHandle *handle = findHandle(opaque);
  if (!handle)
    return vpiUndefined;
  if (property != vpiObjId) {
    setError(handle->owner, "property is not a 64-bit integer VPI property",
             vpiNotice);
    return vpiUndefined;
  }
  if (!propertyFor(handle, property))
    return vpiUndefined;
  setError(handle->owner, "unsupported 64-bit integer VPI property", vpiNotice);
  return vpiUndefined;
}

extern "C" OBELISK_VPI_EXPORT PLI_BYTE8 *vpi_get_str(PLI_INT32 property,
                                                     vpiHandle opaque) {
  beginVPICall();
  __vpiHandle *handle = findHandle(opaque);
  if (!handle)
    return nullptr;
  std::string &scratch = handle->owner->propertyStringScratch;
  if (property == vpiType) {
    PLI_INT32 type = vpiTypeForHandle(handle);
    const auto *descriptor = type == vpiUndefined
                                 ? nullptr
                                 : obelisk::reflection::findVPIObjectKind(
                                       static_cast<uint32_t>(type));
    if (!descriptor) {
      setError(handle->owner, "VPI object type has no symbolic spelling",
               vpiNotice);
      return nullptr;
    }
    OBELISK_RT_TRY {
      scratch = descriptor->apiName;
      return scratch.data();
    }
    OBELISK_RT_CATCH_ALL {
      setError(handle->owner, "VPI type-name buffer is out of memory",
               vpiSystem);
      return nullptr;
    }
  }
  if (handle->kind != VPIHandleKind::Object) {
    setError(handle->owner, "unsupported string property for VPI handle kind",
             vpiError);
    return nullptr;
  }
  uint32_t objectType = static_cast<uint32_t>(vpiTypeForHandle(handle));
  if (handle->form == VPIObjectForm::Indexed && objectType == vpiPortBit &&
      property == vpiName)
    return nullptr;
  if (property == vpiFullName &&
      (isSemanticObjectForm(handle->form) || isTypespecVPIKind(objectType))) {
    propertyFor(handle, property);
    return nullptr;
  }
  if (isSemanticObjectForm(handle->form) && property == vpiName) {
    if (handle->form == VPIObjectForm::TypespecMember) {
      obelisk_rt_design_semantic_type_edge_v1 edge{};
      if (!semanticEdge(handle, handle->semanticCursor, handle->semanticEdge,
                        edge) ||
          edge.role != OBELISK_RT_DESIGN_SEMANTIC_EDGE_MEMBER ||
          edge.name_size == 0)
        return nullptr;
      OBELISK_RT_TRY {
        scratch.assign(reinterpret_cast<const char *>(edge.name),
                       static_cast<size_t>(edge.name_size));
        return scratch.data();
      }
      OBELISK_RT_CATCH_ALL {
        setError(handle->owner, "could not materialize member name", vpiSystem);
        return nullptr;
      }
    }
    if (handle->form != VPIObjectForm::Typespec)
      return nullptr;
    obelisk_rt_design_semantic_type_info_v1 info{};
    if (!semanticTypeInfo(handle, handle->semanticCursor, info) ||
        (info.kind != OBELISK_RT_DESIGN_SEMANTIC_CLASS &&
         info.kind != OBELISK_RT_DESIGN_SEMANTIC_VIRTUAL_INTERFACE))
      return nullptr;
    OBELISK_RT_TRY {
      if (info.name_size != 0) {
        scratch.assign(reinterpret_cast<const char *>(info.name),
                       static_cast<size_t>(info.name_size));
      } else if (info.kind == OBELISK_RT_DESIGN_SEMANTIC_CLASS &&
                 info.identity_target.offset != 0) {
        const uint8_t *name = nullptr;
        uint64_t size = 0;
        if (obelisk_rt_cached_design_name(handle->owner->context,
                                          info.identity_target, &name,
                                          &size) != OBELISK_RT_OK ||
            size == 0)
          return nullptr;
        scratch.assign(reinterpret_cast<const char *>(name),
                       static_cast<size_t>(size));
        size_t dot = scratch.rfind('.');
        size_t colon = scratch.rfind("::");
        if (colon != std::string::npos &&
            (dot == std::string::npos || colon > dot))
          scratch.erase(0, colon + 2);
        else if (dot != std::string::npos)
          scratch.erase(0, dot + 1);
      } else {
        return nullptr;
      }
      return scratch.data();
    }
    OBELISK_RT_CATCH_ALL {
      setError(handle->owner, "could not materialize typespec name", vpiSystem);
      return nullptr;
    }
  }
  if (property == vpiName || property == vpiFullName) {
    if (property == vpiName && !handle->statement) {
      obelisk_rt_design_info_v1 info{};
      if (obelisk_rt_cached_design_info(handle->owner->context, handle->cursor,
                                        &info) != OBELISK_RT_OK)
        return nullptr;
      const auto *kind =
          obelisk::reflection::findVPIObjectKind(handle->exactVpiType);
      bool typespec =
          kind && (kind->families &
                   obelisk::reflection::vpiFamilyMask(
                       obelisk::reflection::VPIObjectFamily::Typespec));
      if (info.kind == OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT && typespec &&
          handle->exactVpiType != vpiClassTypespec &&
          (info.capabilities & OBELISK_RT_DESIGN_CAP_NAMED_TYPESPEC) == 0)
        return nullptr;
    }
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
      if (handle->exactVpiType == vpiPackage && scratch.size() >= 2 &&
          scratch.compare(scratch.size() - 2, 2, "::") == 0)
        scratch.resize(scratch.size() - 2);
      size_t separator = scratch.rfind('.');
      size_t namespaceSeparator = scratch.rfind("::");
      if (namespaceSeparator != std::string::npos &&
          (separator == std::string::npos || namespaceSeparator > separator))
        scratch.erase(0, namespaceSeparator + 2);
      else if (separator != std::string::npos)
        scratch.erase(0, separator + 1);
    }
    return scratch.data();
  }
  if (property == vpiFile || property == vpiDefFile) {
    if (property == vpiFile && !propertyFor(handle, property))
      return nullptr;
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
  beginVPICall();
  __vpiHandle *handle = validate(opaque);
  if (!handle)
    return;
  if (!destination) {
    setError(handle->owner, "VPI value destination is null");
    return;
  }
  const auto *policy = valuePolicyFor(handle);
  if (!policy)
    return;
  obelisk_rt_design_info_v1 info{};
  if (handle->form == VPIObjectForm::IntegralConstant) {
    info.kind = OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT;
    info.bit_width = 64;
  } else if (!infoFor(handle, info)) {
    return;
  }
  if (destination->format == vpiObjTypeVal &&
      handle->form == VPIObjectForm::IntegralConstant)
    destination->format = vpiIntVal;
  else if (destination->format == vpiObjTypeVal)
    resolveObjectTypeValueFormat(*policy, destination->format);
  if (!obelisk::reflection::acceptsVPIValueFormat(
          *policy, static_cast<uint32_t>(destination->format))) {
    setError(handle->owner, "value format is not valid for this VPI object",
             vpiNotice);
    return;
  }
  if (!valueRequirementsSatisfied(handle, info, *policy))
    return;
  std::vector<uint64_t> &value = handle->owner->readValueScratch;
  std::vector<uint64_t> &unknown = handle->owner->readUnknownScratch;
  if (!readValue(handle, info, value, unknown))
    return;
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
  case vpiRealVal:
    if (!decodeRealBits(handle, vpiTypeForHandle(handle), info.bit_width,
                        value[0], destination->value.real))
      return;
    break;
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

extern "C" OBELISK_VPI_EXPORT void vpi_get_time(vpiHandle opaque,
                                                p_vpi_time destination) {
  beginVPICall();
  VPIState *state = requireState();
  if (!state)
    return;
  if (!destination) {
    setError(state, "VPI time destination is null");
    return;
  }

  int32_t unit = 0;
  int32_t precision = 0;
  if (!globalTimeExponent(state, precision))
    return;
  unit = precision;
  uint64_t ticks = state->context->schedulerTime;
  if (opaque) {
    __vpiHandle *handle = findHandle(opaque);
    if (!handle)
      return;
    if (handle->kind == VPIHandleKind::TimeQueue) {
      ticks = handle->cursor.offset;
    } else if (handle->kind == VPIHandleKind::Object) {
      DpiScopeHandle *scope = timeScopeFor(handle);
      if (!scope) {
        setError(state, "VPI object timescale metadata is unavailable",
                 vpiNotice);
        return;
      }
      unit = scope->timeUnit;
    } else {
      setError(state, "VPI handle does not have simulation time", vpiNotice);
      return;
    }
  }

  switch (destination->type) {
  case vpiSimTime:
    destination->high = static_cast<PLI_UINT32>(ticks >> 32);
    destination->low = static_cast<PLI_UINT32>(ticks);
    destination->real = 0.0;
    return;
  case vpiScaledRealTime:
    destination->high = 0;
    destination->low = 0;
    destination->real = static_cast<double>(
        static_cast<long double>(ticks) *
        std::pow(10.0L, static_cast<long double>(precision - unit)));
    return;
  case vpiSuppressTime:
    destination->high = 0;
    destination->low = 0;
    destination->real = 0.0;
    return;
  default:
    setError(state, "unsupported VPI time format");
    return;
  }
}

extern "C" OBELISK_VPI_EXPORT vpiHandle vpi_put_value(vpiHandle opaque,
                                                      p_vpi_value source,
                                                      p_vpi_time,
                                                      PLI_INT32 flags) {
  beginVPICall();
  __vpiHandle *handle = validate(opaque);
  if (!handle)
    return nullptr;
  obelisk_rt_context *context = handle->owner->context;
  if (!context->execution ||
      (context->execution->flags & OBELISK_RT_EXECUTION_VPI_WRITE) == 0) {
    setError(handle->owner, "VPI mutation requires --vpi=full");
    return nullptr;
  }
  if (handle->form == VPIObjectForm::Indexed) {
    setError(handle->owner,
             "indexed VPI writes require selected-state scheduling support");
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
  std::vector<uint64_t> value;
  std::vector<uint64_t> unknown;
  if (!decodeValue(handle, source, info.bit_width, vpiTypeForHandle(handle),
                   value, unknown))
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
  beginVPICall();
  __vpiHandle *left = findHandle(first);
  __vpiHandle *right = findHandle(second);
  if (!left || !right || left->kind != right->kind)
    return 0;
  if (left->kind == VPIHandleKind::Callback ||
      left->kind == VPIHandleKind::TimeQueue)
    return left->cursor.offset == right->cursor.offset;
  if (left->kind != VPIHandleKind::Object) {
    setError(left->owner, "VPI handle kind does not denote an object");
    return 0;
  }
  bool sameSelection =
      left->selectionRootType == right->selectionRootType &&
      left->selectionAccessKind == right->selectionAccessKind &&
      left->selectionBitOffset == right->selectionBitOffset &&
      left->selectionSteps.size() == right->selectionSteps.size() &&
      std::equal(left->selectionSteps.begin(), left->selectionSteps.end(),
                 right->selectionSteps.begin(),
                 [](const VPISelectionStep &a, const VPISelectionStep &b) {
                   return a.physicalType.offset == b.physicalType.offset &&
                          a.semanticType.offset == b.semanticType.offset &&
                          a.bitOffset == b.bitOffset &&
                          a.bitWidth == b.bitWidth && a.index == b.index &&
                          a.selectionOrdinal == b.selectionOrdinal &&
                          a.exactVpiType == b.exactVpiType &&
                          a.packed == b.packed &&
                          a.arrayDimension == b.arrayDimension &&
                          a.aggregateBoundary == b.aggregateBoundary &&
                          a.suppressSemanticDimension ==
                              b.suppressSemanticDimension;
                 });
  return left->form == right->form &&
         left->cursor.offset == right->cursor.offset &&
         left->semanticCursor.offset == right->semanticCursor.offset &&
         left->semanticEdge == right->semanticEdge &&
         left->suppressSemanticAlias == right->suppressSemanticAlias &&
         left->suppressSemanticDimension == right->suppressSemanticDimension &&
         left->integralValue == right->integralValue && sameSelection;
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32 vpi_release_handle(vpiHandle opaque) {
  beginVPICall();
  VPIState *state = requireState();
  const uintptr_t token = reinterpret_cast<uintptr_t>(opaque);
  if (!state || !token) {
    setError(state, "VPI handle was already released or is invalid");
    return 0;
  }
  auto found = state->handles.find(token);
  if (found == state->handles.end() || found->second->owner != state) {
    setError(state, "VPI handle was already released or is invalid");
    return 0;
  }
  state->handles.erase(found);
  return 1;
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32 vpi_free_object(vpiHandle object) {
  return vpi_release_handle(object);
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32
vpi_chk_error(p_vpi_error_info destination) {
  VPIState *state = currentState();
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
  return level;
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32 vpi_vprintf(PLI_BYTE8 *format,
                                                    va_list arguments) {
  beginVPICall();
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
  beginVPICall();
  return requireState() ? std::fflush(nullptr) : -1;
}

extern "C" OBELISK_VPI_EXPORT void *vpi_get_userdata(vpiHandle opaque) {
  beginVPICall();
  __vpiHandle *handle = validate(opaque);
  return handle ? handle->userData : nullptr;
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32 vpi_put_userdata(vpiHandle opaque,
                                                         void *data) {
  beginVPICall();
  __vpiHandle *handle = validate(opaque);
  if (!handle)
    return 0;
  handle->userData = data;
  return 1;
}

extern "C" OBELISK_VPI_EXPORT vpiHandle
vpi_register_cb(p_cb_data callbackData) {
  beginVPICall();
  VPIState *state = currentState();
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
  beginVPICall();
  VPIState *state = requireState();
  if (!state)
    return 0;
  const uintptr_t token = reinterpret_cast<uintptr_t>(opaque);
  auto found = token ? state->handles.find(token) : state->handles.end();
  if (!token || found == state->handles.end()) {
    setError(state, "invalid VPI callback handle");
    return 0;
  }
  return removeCallbackHandle(state, found->second.get());
}

extern "C" OBELISK_VPI_EXPORT void vpi_get_cb_info(vpiHandle opaque,
                                                   p_cb_data destination) {
  beginVPICall();
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
  beginVPICall();
  unsupportedStartup(
      "VPI system task/function registration is not supported during startup");
  return nullptr;
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32
vpi_get_vlog_info(p_vpi_vlog_info info) {
  beginVPICall();
  static char product[] = "Obelisk";
  static char version[] = "0.1";
  if (!requireState() || !info)
    return 0;
  *info = {};
  info->product = product;
  info->version = version;
  return 1;
}

extern "C" OBELISK_VPI_EXPORT vpiHandle vpi_handle_by_index(vpiHandle object,
                                                            PLI_INT32 index) {
  beginVPICall();
  return handleByIndices(object, 1, &index);
}

extern "C" OBELISK_VPI_EXPORT vpiHandle vpi_handle_by_multi_index(
    vpiHandle object, PLI_INT32 count, PLI_INT32 *indices) {
  beginVPICall();
  return handleByIndices(object, count, indices);
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32 vpi_control(PLI_INT32, ...) {
  beginVPICall();
  VPIState *state = requireState();
  setError(state, "VPI control operations are not supported");
  return 0;
}
