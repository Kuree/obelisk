//===- VPI.cpp - Single-context IEEE VPI compatibility shim ---------------===//

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "RuntimeInternal.h"

#include "DesignBytecodeNets.h"
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
#include <string_view>
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
  InterModPath,
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

enum class VPISemanticIteratorKind : uint8_t {
  None,
  Ranges,
  TypespecMembers,
  Elements,
  ObjectMembers,
  Indices
};

struct VPISelectionStep {
  obelisk_rt_design_cursor_v1 physicalType{};
  obelisk_rt_design_cursor_v1 semanticType{};
  uint64_t bitOffset = 0;
  uint64_t bitWidth = 0;
  int64_t index = 0;
  uint32_t selectionOrdinal = 0;
  uint32_t exactVpiType = 0;
  obelisk_rt_design_cursor_v1 memberSemanticParent{};
  bool packed = false;
  bool arrayDimension = false;
  bool memberSelection = false;
  bool aggregateBoundary = false;
  bool suppressSemanticDimension = false;
};

struct VPIInterModPathEndpoint {
  VPIObjectForm form = VPIObjectForm::Design;
  obelisk_rt_design_cursor_v1 cursor{};
  uint32_t exactVpiType = 0;
  uint32_t selectionRootType = 0;
  obelisk::reflection::VPIIndexedAccessKind selectionAccessKind =
      obelisk::reflection::VPIIndexedAccessKind::VariableElement;
  uint64_t selectionBitOffset = 0;
  PLI_INT32 allocationScheme = vpiOtherScheme;
  std::vector<VPISelectionStep> selectionSteps;
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
  bool protectedObject = false;
  bool suppressSemanticAlias = false;
  bool suppressSemanticDimension = false;
  // IEEE 1800-2023 37.29 detail 2 restricts value access based on the
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
  std::vector<VPIInterModPathEndpoint> interModPathEndpoints;
  std::vector<uint64_t> timeQueueItems;
  std::vector<VPISelectionStep> indexItems;
  // Preserve the selector passed to vpi_iterate(). Iterator storage may use
  // a normalized relation selector, but vpiIteratorType is the requested one.
  PLI_INT32 iteratorType = vpiUndefined;
  // Immutable design handles are persistent. Indexed handles inherit this
  // explicit provenance from their source, ready for future transient kinds.
  PLI_INT32 allocationScheme = vpiOtherScheme;
  bool callbackIterator = false;
  bool systemTfIterator = false;
  bool interModPathIterator = false;
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
  PLI_INT32 useAllocationScheme = vpiOtherScheme;
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

struct VPISystemTf {
  uint64_t id = 0;
  PLI_INT32 type = 0;
  PLI_INT32 sysfunctype = 0;
  std::string name;
  PLI_INT32 (*calltf)(PLI_BYTE8 *) = nullptr;
  PLI_INT32 (*compiletf)(PLI_BYTE8 *) = nullptr;
  PLI_INT32 (*sizetf)(PLI_BYTE8 *) = nullptr;
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
  uint64_t nextSystemTfId = 1;
  uint32_t callbackDepth = 0;
  uint64_t runtimeObserverCallbacks = 0;
  std::unordered_map<uint64_t, VPICallback> callbacks;
  std::vector<uint64_t> callbackOrder;
  std::unordered_map<uint64_t, VPISystemTf> systemTfs;
  std::vector<uint64_t> systemTfOrder;
  // IEEE temporary results are invalidated by the next routine call of the
  // same family, irrespective of which object handle was used.
  std::string propertyStringScratch;
  std::string valueStringScratch;
  std::string fileNameScratch;
  std::vector<s_vpi_vecval> vectorScratch;
  std::vector<s_vpi_strengthval> strengthScratch;
  s_vpi_time timeScratch{};
  std::vector<std::string> vlogArgumentScratch;
  std::vector<PLI_BYTE8 *> vlogArgumentPointers;
  std::vector<PLI_INT32> arrayIntegerScratch;
  std::vector<PLI_INT16> arrayShortIntScratch;
  std::vector<PLI_INT64> arrayLongIntScratch;
  std::vector<PLI_BYTE8> arrayRawScratch;
  std::vector<s_vpi_vecval> arrayVectorScratch;
  std::vector<s_vpi_time> arrayTimeScratch;
  std::vector<double> arrayRealScratch;
  std::vector<float> arrayShortRealScratch;
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

bool lookup(VPIState *state, const std::string &name,
            obelisk_rt_design_cursor_v1 &cursor);

bool fixedProtectionFor(VPIState *state, obelisk_rt_design_cursor_v1 cursor,
                        bool &isProtected) {
  isProtected = false;
  VPIFixedPropertyValue property{};
  if (!state)
    return false;
  obelisk_rt_status status = obelisk_rt_cached_vpi_fixed_property(
      state->context, cursor, vpiIsProtected, &property);
  if (status == OBELISK_RT_EOF) {
    bool protectedStatement = false;
    obelisk_rt_status statementStatus =
        obelisk_rt_cached_vpi_statement_is_protected(state->context, cursor,
                                                     &protectedStatement);
    if (statementStatus == OBELISK_RT_OK) {
      isProtected = protectedStatement;
      return true;
    }
    if (statementStatus != OBELISK_RT_INVALID_HANDLE) {
      setError(state, "VPI statement protection lookup failed", vpiInternal);
      return false;
    }
    return true;
  }
  if (status != OBELISK_RT_OK) {
    setError(state, "VPI protection image lookup failed", vpiInternal);
    return false;
  }
  isProtected = property.payload != 0;
  return true;
}

bool protectedNameIntermediate(VPIState *state, const std::string &name) {
  auto protectedPrefix = [&](size_t prefixSize) {
    if (prefixSize == 0 || prefixSize == name.size())
      return false;
    obelisk_rt_design_cursor_v1 intermediate{};
    if (!lookup(state, name.substr(0, prefixSize), intermediate))
      return false;
    bool isProtected = false;
    if (!fixedProtectionFor(state, intermediate, isProtected))
      return true;
    if (!isProtected)
      return false;
    setError(state, "hierarchical lookup crosses a protected VPI scope",
             vpiError);
    return true;
  };
  for (size_t separator = 0; separator < name.size(); ++separator) {
    if (name[separator] == '.' && protectedPrefix(separator))
      return true;
    if (name[separator] == ':' && separator + 1 < name.size() &&
        name[separator + 1] == ':') {
      // Packages conventionally retain the trailing separator in their
      // indexed name, while class scopes do not.  Probe both canonical forms.
      if (protectedPrefix(separator) || protectedPrefix(separator + 2))
        return true;
      ++separator;
    }
  }
  return false;
}

vpiHandle makeHandle(VPIState *state, obelisk_rt_design_cursor_v1 cursor,
                     uint32_t exactVpiType = 0, bool statement = false,
                     bool classDefinitionOrigin = false,
                     bool inheritedProtection = false) {
  bool isProtected = false;
  if (!fixedProtectionFor(state, cursor, isProtected))
    return nullptr;
  OBELISK_RT_TRY {
    auto handle = std::make_unique<__vpiHandle>();
    handle->owner = state;
    handle->kind = VPIHandleKind::Object;
    handle->cursor = cursor;
    handle->exactVpiType = exactVpiType;
    handle->statement = statement;
    handle->protectedObject = isProtected || inheritedProtection;
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
  bool isProtected = false;
  if (!fixedProtectionFor(state, base, isProtected))
    return nullptr;
  OBELISK_RT_TRY {
    auto handle = std::make_unique<__vpiHandle>();
    handle->owner = state;
    handle->kind = VPIHandleKind::Object;
    handle->form = VPIObjectForm::Typespec;
    handle->cursor = base;
    handle->semanticCursor = semanticCursor;
    handle->exactVpiType = exactType;
    handle->protectedObject = isProtected;
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
  bool isProtected = false;
  if (!fixedProtectionFor(state, base, isProtected))
    return nullptr;
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
    handle->protectedObject = isProtected;
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

vpiHandle makeSystemTfHandle(VPIState *state, uint64_t systemTfId) {
  OBELISK_RT_TRY {
    auto handle = std::make_unique<__vpiHandle>();
    handle->owner = state;
    handle->kind = VPIHandleKind::SystemTf;
    handle->cursor.offset = systemTfId;
    return keepHandle(state, std::move(handle));
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    setError(state, "VPI system task/function handle arena is out of memory",
             vpiSystem);
    return nullptr;
  }
  OBELISK_RT_CATCH_ALL {
    setError(state, "could not allocate VPI system task/function handle",
             vpiInternal);
    return nullptr;
  }
}

vpiHandle
makeInterModPathHandle(VPIState *state,
                       const std::vector<VPIInterModPathEndpoint> &endpoints) {
  OBELISK_RT_TRY {
    auto handle = std::make_unique<__vpiHandle>();
    handle->owner = state;
    handle->kind = VPIHandleKind::InterModPath;
    handle->interModPathEndpoints = endpoints;
    return keepHandle(state, std::move(handle));
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    setError(state, "VPI intermodule-path handle arena is out of memory",
             vpiSystem);
    return nullptr;
  }
  OBELISK_RT_CATCH_ALL {
    setError(state, "could not allocate VPI intermodule-path handle",
             vpiInternal);
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

VPISystemTf *findSystemTf(__vpiHandle *handle) {
  if (!handle || handle->kind != VPIHandleKind::SystemTf)
    return nullptr;
  auto found = handle->owner->systemTfs.find(handle->cursor.offset);
  return found == handle->owner->systemTfs.end() ? nullptr : &found->second;
}

bool isLifecycleReason(PLI_INT32 reason) {
  return reason == cbEndOfCompile || reason == cbStartOfSimulation ||
         reason == cbEndOfSimulation;
}

bool observesRunningState(PLI_INT32 reason) {
  // Keep the complete live-observation classification here so enabling
  // statement, value, or synchronization registration cannot accidentally
  // bypass the Tier-1 lease. Lifecycle callbacks run only at cold phase
  // boundaries and never observe an executing scheduler.
  switch (reason) {
  case cbValueChange:
  case cbStmt:
  case cbReadWriteSynch:
  case cbReadOnlySynch:
  case cbNextSimTime:
  case cbAfterDelay:
  case cbAtStartOfSimTime:
  case cbNBASynch:
  case cbAtEndOfSimTime:
    return true;
  default:
    return false;
  }
}

void acquireObservationDemand(VPIState *state, PLI_INT32 reason) {
  if (!state || !observesRunningState(reason))
    return;
  if (++state->runtimeObserverCallbacks != 1)
    return;
  ContextMutexLock lock(state->context);
  obelisk_rt_aot_observation_demand_changed_unlocked(state->context, true);
}

void releaseObservationDemand(VPIState *state, PLI_INT32 reason) {
  if (!state || !observesRunningState(reason) ||
      state->runtimeObserverCallbacks == 0)
    return;
  if (--state->runtimeObserverCallbacks != 0)
    return;
  ContextMutexLock lock(state->context);
  obelisk_rt_aot_observation_demand_changed_unlocked(state->context, false);
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
  const PLI_INT32 reason = found->second.reason;
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
  releaseObservationDemand(state, reason);
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
bool semanticCursorFor(__vpiHandle *handle,
                       obelisk_rt_design_cursor_v1 &cursor);
bool semanticTypeInfo(__vpiHandle *handle, obelisk_rt_design_cursor_v1 cursor,
                      obelisk_rt_design_semantic_type_info_v1 &info);
bool semanticEdge(__vpiHandle *handle, obelisk_rt_design_cursor_v1 cursor,
                  uint32_t ordinal,
                  obelisk_rt_design_semantic_type_edge_v1 &edge);

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

struct VPIRefActualValueSource {
  obelisk_rt_design_cursor_v1 cursor{};
  uint32_t exactType = 0;
  obelisk_rt_design_info_v1 info{};
};

bool refActualValueSource(__vpiHandle *handle,
                          VPIRefActualValueSource &source) {
  if (handle->form != VPIObjectForm::Design ||
      handle->exactVpiType != vpiRefObj) {
    setError(handle->owner, "invalid VPI RefObj actual source", vpiInternal);
    return false;
  }
  VPIRelationRange range{};
  bool statement = false;
  if (obelisk_rt_cached_vpi_relation_range(handle->owner->context,
                                           handle->cursor, vpiActual, false,
                                           &range) != OBELISK_RT_OK ||
      range.count != 1 ||
      obelisk_rt_cached_vpi_relation_target(handle->owner->context, range.first,
                                            &source.cursor, &source.exactType,
                                            &statement) != OBELISK_RT_OK ||
      statement ||
      obelisk_rt_cached_design_info(handle->owner->context, source.cursor,
                                    &source.info) != OBELISK_RT_OK) {
    setError(handle->owner, "VPI RefObj actual cannot be resolved",
             vpiInternal);
    return false;
  }
  return true;
}

struct VPIValueSource {
  VPIObjectForm form = VPIObjectForm::Design;
  obelisk_rt_design_cursor_v1 cursor{};
  obelisk_rt_design_cursor_v1 semanticCursor{};
  obelisk_rt_design_info_v1 info{};
  uint64_t selectionBitOffset = 0;
  uint32_t exactType = 0;
  bool hasSemanticCursor = false;
  const obelisk::reflection::VPIValuePolicyDescriptor *policy = nullptr;
};

bool valueSourceFor(
    __vpiHandle *handle,
    const obelisk::reflection::VPIValuePolicyDescriptor &queryPolicy,
    VPIValueSource &source) {
  source = {};
  source.form = handle->form;
  source.cursor = handle->cursor;
  source.selectionBitOffset = handle->selectionBitOffset;
  source.exactType = static_cast<uint32_t>(vpiTypeForHandle(handle));
  source.policy = &queryPolicy;
  if (handle->form == VPIObjectForm::IntegralConstant) {
    source.info.kind = OBELISK_RT_DESIGN_RECORD_STATIC_OBJECT;
    source.info.bit_width = 64;
  } else if (!infoFor(handle, source.info)) {
    return false;
  }

  if (queryPolicy.readSemantics ==
      obelisk::reflection::VPIValueReadSemantics::EvaluateActual) {
    VPIRefActualValueSource actual{};
    if (!refActualValueSource(handle, actual))
      return false;
    source.form = VPIObjectForm::Design;
    source.cursor = actual.cursor;
    source.info = actual.info;
    source.exactType = actual.exactType;
    source.selectionBitOffset = 0;
    source.policy = obelisk::reflection::findVPIValuePolicy(source.exactType);
    if (!source.policy) {
      setError(handle->owner,
               "vpi_get_value is not defined for the VPI RefObj actual",
               vpiNotice);
      return false;
    }
    source.hasSemanticCursor = obelisk_rt_cached_design_semantic_root(
                                   handle->owner->context, source.cursor,
                                   &source.semanticCursor) == OBELISK_RT_OK;
  } else {
    source.hasSemanticCursor = semanticCursorFor(handle, source.semanticCursor);
  }
  return true;
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

bool resolveObjectTypeValueFormat(__vpiHandle *handle,
                                  const VPIValueSource &source,
                                  PLI_INT32 &format) {
  using Default = obelisk::reflection::VPIValueDefaultFormat;
  switch (source.policy->defaultFormat) {
  case Default::Integer:
    format = vpiIntVal;
    return true;
  case Default::Real:
    format = vpiRealVal;
    return true;
  case Default::String:
    format = vpiStringVal;
    return true;
  case Default::Time:
    format = vpiTimeVal;
    return true;
  case Default::ScalarOrVector: {
    if (source.exactType == vpiNetBit || source.exactType == vpiRegBit) {
      format = vpiScalarVal;
      return true;
    }
    VPIValueShape shape = VPIValueShape::Neither;
    if (!valueShapeFor(handle, source.info, shape))
      return false;
    if (shape == VPIValueShape::Scalar)
      format = vpiScalarVal;
    else if (shape == VPIValueShape::Vector)
      format = vpiVectorVal;
    else {
      setError(handle->owner, "VPI object has no scalar or vector value shape",
               vpiInternal);
      return false;
    }
    return true;
  }
  case Default::Semantic: {
    if (!source.hasSemanticCursor) {
      // Integral constants synthesized by traversal do not carry a semantic
      // type record; their closest representation is an integer.
      if (handle->form == VPIObjectForm::IntegralConstant) {
        format = vpiIntVal;
        return true;
      }
      setError(handle->owner, "VPI semantic value format is unavailable",
               vpiInternal);
      return false;
    }
    obelisk_rt_design_semantic_type_info_v1 semantic{};
    if (!semanticTypeInfo(handle, source.semanticCursor, semantic))
      return false;
    switch (semantic.kind) {
    case OBELISK_RT_DESIGN_SEMANTIC_SHORT_REAL:
    case OBELISK_RT_DESIGN_SEMANTIC_REAL:
    case OBELISK_RT_DESIGN_SEMANTIC_REALTIME:
      format = vpiRealVal;
      return true;
    case OBELISK_RT_DESIGN_SEMANTIC_STRING:
      format = vpiStringVal;
      return true;
    case OBELISK_RT_DESIGN_SEMANTIC_TIME:
      format = vpiTimeVal;
      return true;
    case OBELISK_RT_DESIGN_SEMANTIC_BYTE:
    case OBELISK_RT_DESIGN_SEMANTIC_SHORT_INT:
    case OBELISK_RT_DESIGN_SEMANTIC_INT:
    case OBELISK_RT_DESIGN_SEMANTIC_LONG_INT:
    case OBELISK_RT_DESIGN_SEMANTIC_INTEGER:
      format = vpiIntVal;
      return true;
    default:
      break;
    }
    VPIValueShape shape = VPIValueShape::Neither;
    if (!valueShapeFor(handle, source.info, shape))
      return false;
    if (shape == VPIValueShape::Scalar) {
      format = vpiScalarVal;
      return true;
    }
    if (shape == VPIValueShape::Vector) {
      format = vpiVectorVal;
      return true;
    }
    setError(handle->owner, "VPI semantic object has no value representation",
             vpiInternal);
    return false;
  }
  }
  setError(handle->owner, "unknown generated VPI value default", vpiInternal);
  return false;
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
  case VPIHandleKind::InterModPath:
    return vpiInterModPath;
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
  if (!descriptor) {
    setError(handle->owner,
             handle->protectedObject
                 ? "property access is denied for a protected VPI object"
                 : "property is not defined for this VPI object",
             handle->protectedObject ? vpiError : vpiNotice);
  } else if (handle->protectedObject &&
             descriptor->protectedAccess ==
                 obelisk::reflection::VPIPropertyProtectedAccess::Denied) {
    setError(handle->owner,
             "property access is denied for a protected VPI object", vpiError);
    return nullptr;
  }
  return descriptor;
}

bool fixedPropertyFor(
    __vpiHandle *handle,
    const obelisk::reflection::VPIPropertyDescriptor &descriptor,
    VPIFixedPropertyValue &value) {
  obelisk_rt_status status = obelisk_rt_cached_vpi_fixed_property(
      handle->owner->context, handle->cursor, descriptor.property, &value);
  if (status == OBELISK_RT_OK) {
    // IEEE 1800-2023 37.16: the declaration is vpiNettypeNet, while every
    // selected part of that declaration is vpiNettypeNetSelect. The immutable
    // image stores the declaration fact once; selection provenance belongs to
    // this cold query handle and must not be pushed into simulation state.
    if (descriptor.property == vpiNetType &&
        handle->form == VPIObjectForm::Indexed &&
        value.payload == static_cast<uint64_t>(vpiNettypeNet))
      value.payload = static_cast<uint64_t>(vpiNettypeNetSelect);
    return true;
  }
  if (status == OBELISK_RT_EOF &&
      (descriptor.valueKind ==
           obelisk::reflection::VPIPropertyValueKind::Boolean ||
       descriptor.property == vpiDefFile ||
       descriptor.property == vpiDefLineNo)) {
    value = {};
    value.kind = static_cast<uint8_t>(descriptor.valueKind);
    if (descriptor.property == vpiIsProtected)
      value.payload = handle->protectedObject;
    return true;
  }
  setError(handle->owner,
           status == OBELISK_RT_EOF ? "fixed VPI property value is unavailable"
                                    : "fixed VPI property image lookup failed",
           status == OBELISK_RT_EOF ? vpiNotice : vpiInternal);
  return false;
}

bool indexedImagePropertyFor(
    __vpiHandle *handle,
    const obelisk::reflection::VPIPropertyDescriptor &descriptor,
    uint32_t &value) {
  if (descriptor.property != vpiResolvedNetType) {
    setError(handle->owner, "unsupported indexed-image VPI property",
             vpiInternal);
    return false;
  }
  obelisk_rt_design_info_v1 info{};
  if (!infoFor(handle, info))
    return false;
  uint64_t bitOffset =
      handle->form == VPIObjectForm::Indexed ? handle->selectionBitOffset : 0;
  obelisk_rt_status status = obelisk_rt_cached_vpi_resolved_net_type(
      handle->owner->context, handle->cursor, bitOffset, info.bit_width,
      &value);
  if (status == OBELISK_RT_OK)
    return true;
  setError(handle->owner,
           status == OBELISK_RT_EOF
               ? "indexed VPI property value is unavailable"
               : "indexed VPI property image lookup failed",
           status == OBELISK_RT_EOF ? vpiNotice : vpiInternal);
  return false;
}

bool allowProtectedSource(__vpiHandle *handle, const char *operation) {
  if (!handle || !handle->protectedObject)
    return true;
  (void)operation;
  setError(handle->owner, "operation is denied for a protected VPI object",
           vpiError);
  return false;
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
        if (step.memberSelection) {
          obelisk_rt_design_semantic_type_edge_v1 edge{};
          if (!semanticEdge(handle, step.memberSemanticParent,
                            step.selectionOrdinal, edge) ||
              edge.role != OBELISK_RT_DESIGN_SEMANTIC_EDGE_MEMBER ||
              edge.name_size == 0)
            return false;
          name.push_back('.');
          name.append(reinterpret_cast<const char *>(edge.name),
                      static_cast<size_t>(edge.name_size));
        } else {
          name.push_back('[');
          name += std::to_string(step.index);
          name.push_back(']');
        }
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

bool physicalTypeInfo(__vpiHandle *handle,
                      obelisk_rt_design_type_info_v1 &type) {
  obelisk_rt_design_info_v1 info{};
  if (!infoFor(handle, info))
    return false;
  if (info.type_offset == 0 ||
      obelisk_rt_cached_design_type_info(
          handle->owner->context, {info.type_offset}, &type) != OBELISK_RT_OK) {
    setError(handle->owner, "design type metadata lookup failed", vpiInternal);
    return false;
  }
  return true;
}

bool physicalUnpackedArrayDimensions(__vpiHandle *handle,
                                     uint32_t &dimensions) {
  obelisk_rt_design_info_v1 info{};
  if (!infoFor(handle, info))
    return false;
  dimensions = 0;
  obelisk_rt_design_cursor_v1 cursor{info.type_offset};
  for (;;) {
    obelisk_rt_design_type_info_v1 type{};
    if (cursor.offset == 0 ||
        obelisk_rt_cached_design_type_info(handle->owner->context, cursor,
                                           &type) != OBELISK_RT_OK) {
      setError(handle->owner, "design type metadata lookup failed",
               vpiInternal);
      return false;
    }
    if (type.kind != OBELISK_RT_DESIGN_TYPE_ARRAY ||
        (type.flags & OBELISK_RT_DESIGN_TYPE_PACKED) != 0)
      return true;
    if (dimensions == UINT32_MAX) {
      setError(handle->owner, "physical array dimension count exceeds ABI",
               vpiInternal);
      return false;
    }
    ++dimensions;
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
  if (handle->form == VPIObjectForm::Indexed)
    return !handle->selectionSteps.empty() &&
           handle->selectionSteps.back().arrayDimension &&
           !handle->selectionSteps.back().packed;
  if (handle->form != VPIObjectForm::Design)
    return false;
  VPIArrayMemberInfo member{};
  return obelisk_rt_cached_vpi_array_member(
             handle->owner->context, handle->cursor, &member) == OBELISK_RT_OK;
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

bool staticArrayMemberIndexSteps(__vpiHandle *handle,
                                 std::vector<VPISelectionStep> &steps) {
  if (!handle || handle->form != VPIObjectForm::Design)
    return false;
  OBELISK_RT_TRY {
    steps.clear();
    obelisk_rt_design_cursor_v1 cursor = handle->cursor;
    bool foundMember = false;
    while (true) {
      VPIArrayMemberInfo member{};
      obelisk_rt_status status = obelisk_rt_cached_vpi_array_member(
          handle->owner->context, cursor, &member);
      if (status == OBELISK_RT_EOF)
        return foundMember;
      if (status != OBELISK_RT_OK || member.array.offset == cursor.offset)
        return false;
      foundMember = true;
      std::vector<VPISelectionStep> dimensions(member.index.dimensionCount);
      if (member.index.sparse) {
        int64_t index = 0;
        if (dimensions.size() != 1 ||
            obelisk_rt_cached_vpi_relation_index_ordinal_key(
                handle->owner->context, member.index, member.ordinal, &index) !=
                OBELISK_RT_OK)
          return false;
        dimensions[0].index = index;
        dimensions[0].selectionOrdinal = 0;
        dimensions[0].arrayDimension = true;
      } else {
        uint64_t flattened = member.ordinal;
        for (size_t dimension = member.index.dimensionCount; dimension != 0;) {
          --dimension;
          int64_t left = 0, right = 0;
          if (obelisk_rt_cached_vpi_relation_index_dimension(
                  handle->owner->context, member.index,
                  static_cast<uint32_t>(dimension), &left,
                  &right) != OBELISK_RT_OK)
            return false;
          uint64_t ordinal = 0, extent = 0;
          if (!sourceIndexOrdinal(left, right, left, ordinal, extent) ||
              extent == 0)
            return false;
          uint64_t coordinate = flattened % extent;
          flattened /= extent;
          dimensions[dimension].index =
              left >= right ? left - static_cast<int64_t>(coordinate)
                            : left + static_cast<int64_t>(coordinate);
          dimensions[dimension].selectionOrdinal =
              static_cast<uint32_t>(dimension);
          dimensions[dimension].arrayDimension = true;
        }
        if (flattened != 0)
          return false;
      }
      steps.insert(steps.begin(), dimensions.begin(), dimensions.end());
      cursor = member.array;
    }
  }
  OBELISK_RT_CATCH_ALL {
    setError(handle->owner, "could not allocate VPI member indices", vpiSystem);
    return false;
  }
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
    handle->protectedObject = source->protectedObject;
    handle->allocationScheme = source->allocationScheme;
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

VPIInterModPathEndpoint interModPathEndpointFor(const __vpiHandle &port) {
  VPIInterModPathEndpoint endpoint;
  endpoint.form = port.form;
  endpoint.cursor = port.cursor;
  endpoint.exactVpiType =
      static_cast<uint32_t>(vpiTypeForHandle(const_cast<__vpiHandle *>(&port)));
  endpoint.selectionRootType = port.selectionRootType;
  endpoint.selectionAccessKind = port.selectionAccessKind;
  endpoint.selectionBitOffset = port.selectionBitOffset;
  endpoint.allocationScheme = port.allocationScheme;
  endpoint.selectionSteps = port.selectionSteps;
  return endpoint;
}

bool sameInterModPathEndpoint(const VPIInterModPathEndpoint &first,
                              const VPIInterModPathEndpoint &second) {
  if (first.form != second.form ||
      first.cursor.offset != second.cursor.offset ||
      first.exactVpiType != second.exactVpiType ||
      first.selectionRootType != second.selectionRootType ||
      first.selectionAccessKind != second.selectionAccessKind ||
      first.selectionBitOffset != second.selectionBitOffset ||
      first.selectionSteps.size() != second.selectionSteps.size())
    return false;
  return std::equal(
      first.selectionSteps.begin(), first.selectionSteps.end(),
      second.selectionSteps.begin(),
      [](const VPISelectionStep &a, const VPISelectionStep &b) {
        return a.physicalType.offset == b.physicalType.offset &&
               a.semanticType.offset == b.semanticType.offset &&
               a.bitOffset == b.bitOffset && a.bitWidth == b.bitWidth &&
               a.index == b.index && a.selectionOrdinal == b.selectionOrdinal &&
               a.exactVpiType == b.exactVpiType && a.packed == b.packed &&
               a.arrayDimension == b.arrayDimension &&
               a.memberSemanticParent.offset == b.memberSemanticParent.offset &&
               a.memberSelection == b.memberSelection &&
               a.aggregateBoundary == b.aggregateBoundary &&
               a.suppressSemanticDimension == b.suppressSemanticDimension;
      });
}

vpiHandle
makeInterModPathEndpointHandle(VPIState *state,
                               const VPIInterModPathEndpoint &endpoint) {
  if (endpoint.form == VPIObjectForm::Design)
    return makeHandle(state, endpoint.cursor, endpoint.exactVpiType);
  if (endpoint.form != VPIObjectForm::Indexed ||
      endpoint.selectionSteps.empty())
    return nullptr;
  OBELISK_RT_TRY {
    __vpiHandle source;
    source.owner = state;
    source.cursor = endpoint.cursor;
    source.form = endpoint.form;
    source.exactVpiType = endpoint.exactVpiType;
    source.selectionRootType = endpoint.selectionRootType;
    source.selectionAccessKind = endpoint.selectionAccessKind;
    source.selectionBitOffset = endpoint.selectionBitOffset;
    source.allocationScheme = endpoint.allocationScheme;
    return makeIndexedHandle(&source, endpoint.selectionRootType,
                             endpoint.selectionAccessKind,
                             endpoint.selectionSteps);
  }
  OBELISK_RT_CATCH_ALL {
    setError(state, "could not copy VPI intermodule-path endpoint", vpiSystem);
    return nullptr;
  }
}

vpiHandle makeRelationIndexedHandle(__vpiHandle *source, uint32_t rootType,
                                    std::vector<VPISelectionStep> steps,
                                    uint64_t remainingElements,
                                    int64_t nextLeft, int64_t nextRight);

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
    if (source->selectionAccessKind ==
        obelisk::reflection::VPIIndexedAccessKind::RelationElement) {
      VPIRelationIndexInfo index{};
      if (obelisk_rt_cached_vpi_relation_index(source->owner->context,
                                               source->cursor,
                                               &index) != OBELISK_RT_OK ||
          index.sparse || count >= index.dimensionCount)
        return nullptr;
      uint64_t remaining = 1;
      int64_t nextLeft = 0, nextRight = 0;
      for (uint32_t dimension = static_cast<uint32_t>(count);
           dimension != index.dimensionCount; ++dimension) {
        int64_t left = 0, right = 0;
        if (obelisk_rt_cached_vpi_relation_index_dimension(
                source->owner->context, index, dimension, &left, &right) !=
            OBELISK_RT_OK)
          return nullptr;
        uint64_t ordinal = 0, extent = 0;
        if (!sourceIndexOrdinal(left, right, left, ordinal, extent) ||
            remaining > UINT32_MAX / extent)
          return nullptr;
        if (dimension == count) {
          nextLeft = left;
          nextRight = right;
        }
        remaining *= extent;
      }
      return makeRelationIndexedHandle(source, source->selectionRootType,
                                       std::move(prefix), remaining, nextLeft,
                                       nextRight);
    }
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
  steps.push_back({selectedPhysical,
                   selectedSemantic,
                   previousOffset + delta,
                   selectedWidth,
                   index,
                   static_cast<uint32_t>(steps.size()),
                   resultType,
                   {},
                   packed,
                   isArray,
                   false,
                   aggregateBoundary,
                   terminalBit});
  return true;
}

bool appendAggregateMemberSelection(
    __vpiHandle *source, obelisk::reflection::VPIIndexedAccessKind accessKind,
    uint32_t edgeIndex, std::vector<VPISelectionStep> &steps) {
  obelisk_rt_design_info_v1 object{};
  if (obelisk_rt_cached_design_info(source->owner->context, source->cursor,
                                    &object) != OBELISK_RT_OK ||
      object.type_offset == 0)
    return false;
  obelisk_rt_design_cursor_v1 physical{
      steps.empty() ? object.type_offset : steps.back().physicalType.offset};
  obelisk_rt_design_cursor_v1 semantic{};
  if (!steps.empty())
    semantic = steps.back().semanticType;
  else if (obelisk_rt_cached_design_semantic_root(source->owner->context,
                                                  source->cursor,
                                                  &semantic) != OBELISK_RT_OK)
    return false;

  uint32_t currentType = steps.empty()
                             ? static_cast<uint32_t>(vpiTypeForHandle(source))
                             : steps.back().exactVpiType;
  const auto *access = obelisk::reflection::findVPIIndexedAccess(currentType);
  if (!access || access->accessKind != accessKind)
    return false;

  obelisk_rt_design_type_info_v1 aggregate{};
  obelisk_rt_design_semantic_type_info_v1 semanticInfo{};
  if (obelisk_rt_cached_design_type_info(source->owner->context, physical,
                                         &aggregate) != OBELISK_RT_OK ||
      (aggregate.kind != OBELISK_RT_DESIGN_TYPE_STRUCT &&
       aggregate.kind != OBELISK_RT_DESIGN_TYPE_UNION) ||
      !semanticTypeInfo(source, semantic, semanticInfo) ||
      (semanticInfo.kind != OBELISK_RT_DESIGN_SEMANTIC_PACKED_STRUCT &&
       semanticInfo.kind != OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_STRUCT &&
       semanticInfo.kind != OBELISK_RT_DESIGN_SEMANTIC_PACKED_UNION &&
       semanticInfo.kind != OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_UNION))
    return false;

  obelisk_rt_design_semantic_type_edge_v1 edge{};
  if (!semanticEdge(source, semantic, edgeIndex, edge) ||
      edge.role != OBELISK_RT_DESIGN_SEMANTIC_EDGE_MEMBER)
    return false;
  obelisk_rt_design_cursor_v1 field{};
  obelisk_rt_design_type_info_v1 fieldInfo{};
  obelisk_rt_design_type_info_v1 selectedType{};
  if (obelisk_rt_cached_design_type_child(source->owner->context, physical,
                                          edge.ordinal,
                                          &field) != OBELISK_RT_OK ||
      obelisk_rt_cached_design_type_info(source->owner->context, field,
                                         &fieldInfo) != OBELISK_RT_OK ||
      fieldInfo.kind != OBELISK_RT_DESIGN_TYPE_FIELD ||
      fieldInfo.element_type.offset == 0 ||
      obelisk_rt_cached_design_type_info(source->owner->context,
                                         fieldInfo.element_type,
                                         &selectedType) != OBELISK_RT_OK)
    return false;

  const bool unpackedArray =
      selectedType.kind == OBELISK_RT_DESIGN_TYPE_ARRAY &&
      (selectedType.flags & OBELISK_RT_DESIGN_TYPE_PACKED) == 0;
  uint32_t resultType =
      indexedResultType(source, *access, edge.child, unpackedArray, false);
  if (resultType == 0 || steps.size() > UINT32_MAX)
    return false;
  uint64_t previousOffset = steps.empty() ? 0 : steps.back().bitOffset;
  if (fieldInfo.packed_offset > UINT64_MAX - previousOffset)
    return false;
  steps.push_back({fieldInfo.element_type, edge.child,
                   previousOffset + fieldInfo.packed_offset,
                   fieldInfo.bit_width, static_cast<int64_t>(edge.ordinal),
                   edgeIndex, resultType, semantic,
                   (aggregate.flags & OBELISK_RT_DESIGN_TYPE_PACKED) != 0,
                   false, true, false, false});
  return true;
}

vpiHandle handleByAggregateMember(__vpiHandle *source, uint32_t edgeIndex) {
  if (!source || (source->form != VPIObjectForm::Design &&
                  source->form != VPIObjectForm::Indexed))
    return nullptr;
  const uint32_t sourceType = static_cast<uint32_t>(vpiTypeForHandle(source));
  const auto *access = obelisk::reflection::findVPIIndexedAccess(sourceType);
  if (!access || access->accessKind ==
                     obelisk::reflection::VPIIndexedAccessKind::RelationElement)
    return nullptr;
  OBELISK_RT_TRY {
    std::vector<VPISelectionStep> steps;
    uint32_t rootType = sourceType;
    auto accessKind = access->accessKind;
    if (source->form == VPIObjectForm::Indexed) {
      rootType = source->selectionRootType;
      accessKind = source->selectionAccessKind;
      steps = source->selectionSteps;
    }
    if (!appendAggregateMemberSelection(source, accessKind, edgeIndex, steps))
      return nullptr;
    const auto *rootAccess =
        obelisk::reflection::findVPIIndexedAccess(rootType);
    if (!rootAccess || rootAccess->accessKind != accessKind ||
        !obelisk::reflection::indexedVPIResultAllowed(
            *rootAccess, steps.back().exactVpiType))
      return nullptr;
    return makeIndexedHandle(source, rootType, accessKind, std::move(steps));
  }
  OBELISK_RT_CATCH_ALL {
    setError(source->owner, "could not allocate aggregate-member VPI handle",
             vpiSystem);
    return nullptr;
  }
}

vpiHandle makeRelationIndexedHandle(__vpiHandle *source, uint32_t rootType,
                                    std::vector<VPISelectionStep> steps,
                                    uint64_t remainingElements,
                                    int64_t nextLeft, int64_t nextRight) {
  if (!source || steps.empty())
    return nullptr;
  obelisk_rt_design_info_v1 info{};
  if (obelisk_rt_cached_design_info(source->owner->context, source->cursor,
                                    &info) != OBELISK_RT_OK) {
    setError(source->owner, "relation-indexed VPI metadata lookup failed",
             vpiInternal);
    return nullptr;
  }
  OBELISK_RT_TRY {
    auto handle = std::make_unique<__vpiHandle>();
    handle->owner = source->owner;
    handle->kind = VPIHandleKind::Object;
    handle->form = VPIObjectForm::Indexed;
    handle->cursor = source->cursor;
    handle->semanticCursor = steps.back().semanticType;
    handle->exactVpiType = rootType;
    handle->statement = false;
    handle->protectedObject = source->protectedObject;
    handle->allocationScheme = source->allocationScheme;
    handle->classDefinitionOrigin = source->classDefinitionOrigin;
    handle->selectionRootType = rootType;
    handle->selectionAccessKind =
        obelisk::reflection::VPIIndexedAccessKind::RelationElement;
    handle->selectionBitOffset = steps.back().bitOffset;
    handle->selectionSteps = std::move(steps);
    handle->hasInfo = true;
    handle->info = info;
    handle->info.type_offset = 0;
    handle->info.bit_width = remainingElements;
    handle->info.range_left = nextLeft;
    handle->info.range_right = nextRight;
    return keepHandle(source->owner, std::move(handle));
  }
  OBELISK_RT_CATCH_ALL {
    setError(source->owner, "could not allocate relation-indexed VPI handle",
             vpiSystem);
    return nullptr;
  }
}

vpiHandle handleRelationByIndices(
    __vpiHandle *source,
    const obelisk::reflection::VPIIndexedAccessDescriptor &access,
    PLI_INT32 count, const PLI_INT32 *indices) {
  VPIRelationIndexInfo indexInfo{};
  if (obelisk_rt_cached_vpi_relation_index(
          source->owner->context, source->cursor, &indexInfo) != OBELISK_RT_OK)
    return nullptr;
  size_t priorCount = source->form == VPIObjectForm::Indexed
                          ? source->selectionSteps.size()
                          : 0;
  if (priorCount > indexInfo.dimensionCount ||
      static_cast<uint64_t>(count) > indexInfo.dimensionCount - priorCount)
    return nullptr;

  OBELISK_RT_TRY {
    std::vector<int64_t> requested;
    requested.reserve(priorCount + static_cast<size_t>(count));
    if (source->form == VPIObjectForm::Indexed)
      for (const VPISelectionStep &step : source->selectionSteps)
        requested.push_back(step.index);
    for (PLI_INT32 position = 0; position != count; ++position)
      requested.push_back(indices[position]);

    std::vector<VPISelectionStep> steps;
    steps.reserve(requested.size());
    uint64_t flattened = 0;
    obelisk_rt_design_cursor_v1 semantic{};
    (void)obelisk_rt_cached_design_semantic_root(source->owner->context,
                                                 source->cursor, &semantic);
    for (size_t dimension = 0; dimension != requested.size(); ++dimension) {
      uint64_t ordinal = 0;
      uint64_t extent = 0;
      if (indexInfo.sparse) {
        uint32_t sparseOrdinal = 0;
        if (dimension != 0 ||
            obelisk_rt_cached_vpi_relation_index_key(
                source->owner->context, indexInfo, requested[dimension],
                &sparseOrdinal) != OBELISK_RT_OK)
          return nullptr;
        ordinal = sparseOrdinal;
        extent = indexInfo.elementCount;
      } else {
        int64_t left = 0, right = 0;
        if (obelisk_rt_cached_vpi_relation_index_dimension(
                source->owner->context, indexInfo,
                static_cast<uint32_t>(dimension), &left,
                &right) != OBELISK_RT_OK ||
            !sourceIndexOrdinal(left, right, requested[dimension], ordinal,
                                extent))
          return nullptr;
      }
      if (extent == 0 || ordinal > UINT32_MAX ||
          flattened > (UINT32_MAX - ordinal) / extent)
        return nullptr;
      flattened = flattened * extent + ordinal;
      bool packed = false;
      if (semantic.offset != 0) {
        obelisk_rt_design_semantic_type_info_v1 semanticInfo{};
        if (!semanticTypeInfo(source, semantic, semanticInfo))
          return nullptr;
        packed = semanticInfo.kind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY;
        obelisk_rt_design_cursor_v1 element{};
        if (!semanticElement(source, semantic, element))
          return nullptr;
        semantic = element;
      }
      steps.push_back({{},
                       semantic,
                       flattened,
                       0,
                       requested[dimension],
                       static_cast<uint32_t>(dimension),
                       access.unpackedFallback,
                       {},
                       packed,
                       true,
                       false,
                       false,
                       false});
    }

    if (requested.size() == indexInfo.dimensionCount) {
      VPIRelationRange range{};
      if (obelisk_rt_cached_vpi_relation_range(
              source->owner->context, source->cursor, access.relationSelector,
              true, &range) != OBELISK_RT_OK ||
          range.count != indexInfo.elementCount || flattened >= range.count)
        return nullptr;
      obelisk_rt_design_cursor_v1 target{};
      uint32_t targetType = 0;
      bool targetIsStatement = false;
      if (obelisk_rt_cached_vpi_relation_target(
              source->owner->context, range.first + flattened, &target,
              &targetType, &targetIsStatement) != OBELISK_RT_OK ||
          targetIsStatement ||
          !obelisk::reflection::indexedVPIResultAllowed(access, targetType))
        return nullptr;
      return makeHandle(source->owner, target, targetType, false,
                        obelisk::runtime::hasClassDefinitionValueOrigin(
                            static_cast<uint32_t>(vpiTypeForHandle(source)),
                            source->classDefinitionOrigin, targetType),
                        source->protectedObject);
    }

    uint64_t remaining = 1;
    int64_t nextLeft = 0, nextRight = 0;
    for (uint32_t dimension = static_cast<uint32_t>(requested.size());
         dimension != indexInfo.dimensionCount; ++dimension) {
      int64_t left = 0, right = 0;
      if (indexInfo.sparse || obelisk_rt_cached_vpi_relation_index_dimension(
                                  source->owner->context, indexInfo, dimension,
                                  &left, &right) != OBELISK_RT_OK)
        return nullptr;
      uint64_t ordinal = 0, extent = 0;
      if (!sourceIndexOrdinal(left, right, left, ordinal, extent) ||
          remaining > UINT32_MAX / extent)
        return nullptr;
      if (dimension == requested.size()) {
        nextLeft = left;
        nextRight = right;
      }
      remaining *= extent;
    }
    return makeRelationIndexedHandle(source, access.sourceType,
                                     std::move(steps), remaining, nextLeft,
                                     nextRight);
  }
  OBELISK_RT_CATCH_ALL {
    setError(source->owner, "could not perform relation-indexed VPI access",
             vpiSystem);
    return nullptr;
  }
}

vpiHandle handleByIndices(vpiHandle opaque, PLI_INT32 count,
                          const PLI_INT32 *indices,
                          bool directNameLookup = false) {
  __vpiHandle *source = validate(opaque, VPIHandleKind::Object);
  if (!source)
    return nullptr;
  // A direct hierarchical name may identify a protected object; the returned
  // handle retains that protection and gates subsequent access.  In contrast,
  // vpi_handle_by_index is an operation on an existing protected handle and is
  // denied by IEEE 1800's protected-object rules.
  if (!directNameLookup && !allowProtectedSource(source, "indexed access"))
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

  if (access->accessKind ==
      obelisk::reflection::VPIIndexedAccessKind::RelationElement) {
    __vpiHandle *current = source;
    vpiHandle result = nullptr;
    bool ownsCurrent = false;
    PLI_INT32 position = 0;
    while (position != count) {
      uint32_t currentType = static_cast<uint32_t>(vpiTypeForHandle(current));
      const auto *currentAccess =
          obelisk::reflection::findVPIIndexedAccess(currentType);
      VPIRelationIndexInfo indexInfo{};
      size_t priorCount = current->form == VPIObjectForm::Indexed
                              ? current->selectionSteps.size()
                              : 0;
      if (!currentAccess ||
          currentAccess->accessKind !=
              obelisk::reflection::VPIIndexedAccessKind::RelationElement ||
          obelisk_rt_cached_vpi_relation_index(current->owner->context,
                                               current->cursor,
                                               &indexInfo) != OBELISK_RT_OK ||
          priorCount >= indexInfo.dimensionCount) {
        if (ownsCurrent)
          current->owner->handles.erase(current->token);
        return nullptr;
      }
      size_t available = indexInfo.dimensionCount - priorCount;
      PLI_INT32 take = static_cast<PLI_INT32>(
          std::min<size_t>(available, static_cast<size_t>(count - position)));
      result = handleRelationByIndices(current, *currentAccess, take,
                                       indices + position);
      if (ownsCurrent)
        current->owner->handles.erase(current->token);
      if (!result)
        return nullptr;
      position += take;
      if (position == count)
        return result;
      current = validate(result, VPIHandleKind::Object);
      if (!current)
        return nullptr;
      ownsCurrent = true;
    }
    return result;
  }

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

bool parseHierarchicalIndex(const std::string &name, size_t begin, size_t end,
                            PLI_INT32 &result) {
  if (begin == end)
    return false;
  bool negative = name[begin] == '-';
  if (negative && ++begin == end)
    return false;
  uint64_t magnitude = 0;
  const uint64_t limit = negative ? uint64_t{INT32_MAX} + 1 : INT32_MAX;
  for (size_t position = begin; position != end; ++position) {
    char digit = name[position];
    if (digit < '0' || digit > '9')
      return false;
    unsigned value = static_cast<unsigned>(digit - '0');
    if (magnitude > (limit - value) / 10)
      return false;
    magnitude = magnitude * 10 + value;
  }
  result = negative ? magnitude == uint64_t{INT32_MAX} + 1
                          ? INT32_MIN
                          : -static_cast<PLI_INT32>(magnitude)
                    : static_cast<PLI_INT32>(magnitude);
  return true;
}

vpiHandle handleSyntheticSelectionByName(VPIState *state,
                                         const std::string &name,
                                         bool classDefinitionOrigin) {
  if (!state || name.empty())
    return nullptr;
  vpiHandle current = nullptr;
  OBELISK_RT_TRY {
    obelisk_rt_design_cursor_v1 cursor{};
    size_t baseEnd = name.size();
    for (; baseEnd != 0; --baseEnd) {
      if (baseEnd != name.size() && name[baseEnd] != '[' &&
          name[baseEnd] != '.')
        continue;
      if (lookup(state, name.substr(0, baseEnd), cursor))
        break;
    }
    if (baseEnd == 0 || baseEnd == name.size())
      return nullptr;
    obelisk_rt_design_info_v1 info{};
    uint32_t exactType = 0;
    if (obelisk_rt_cached_design_info(state->context, cursor, &info) !=
            OBELISK_RT_OK ||
        obelisk_rt_cached_vpi_type(state->context, cursor, &exactType) !=
            OBELISK_RT_OK ||
        exactType == 0 ||
        (info.capabilities & OBELISK_RT_DESIGN_CAP_INTERNAL) != 0)
      return nullptr;
    current =
        makeHandle(state, cursor, exactType, false, classDefinitionOrigin);
    if (!current)
      return nullptr;
    size_t position = baseEnd;
    while (position != name.size()) {
      __vpiHandle *source = findHandle(current);
      if (!source)
        return nullptr;
      vpiHandle next = nullptr;
      if (name[position] == '[') {
        size_t close = name.find(']', position + 1);
        PLI_INT32 index = 0;
        if (close == std::string::npos ||
            !parseHierarchicalIndex(name, position + 1, close, index)) {
          state->handles.erase(source->token);
          return nullptr;
        }
        next = handleByIndices(current, 1, &index,
                               /*directNameLookup=*/true);
        position = close + 1;
      } else if (name[position] == '.') {
        size_t end = name.find_first_of(".[", position + 1);
        if (end == std::string::npos)
          end = name.size();
        if (end == position + 1) {
          state->handles.erase(source->token);
          return nullptr;
        }
        uint32_t sourceType = static_cast<uint32_t>(vpiTypeForHandle(source));
        if (!obelisk::reflection::findVPITraversal(
                sourceType, vpiMember,
                obelisk::reflection::VPITraversalMode::Iterate)) {
          state->handles.erase(source->token);
          return nullptr;
        }
        obelisk_rt_design_cursor_v1 semantic{};
        obelisk_rt_design_semantic_type_info_v1 semanticInfo{};
        if (!semanticCursorFor(source, semantic) ||
            !semanticTypeInfo(source, semantic, semanticInfo)) {
          state->handles.erase(source->token);
          return nullptr;
        }
        for (uint32_t edgeIndex = 0; edgeIndex != semanticInfo.edge_count;
             ++edgeIndex) {
          obelisk_rt_design_semantic_type_edge_v1 edge{};
          if (!semanticEdge(source, semantic, edgeIndex, edge))
            break;
          if (edge.role == OBELISK_RT_DESIGN_SEMANTIC_EDGE_MEMBER &&
              edge.name_size == end - position - 1 &&
              std::memcmp(edge.name, name.data() + position + 1,
                          static_cast<size_t>(edge.name_size)) == 0) {
            next = handleByAggregateMember(source, edgeIndex);
            break;
          }
        }
        position = end;
      } else {
        state->handles.erase(source->token);
        return nullptr;
      }
      state->handles.erase(source->token);
      if (!next)
        return nullptr;
      current = next;
    }
    return current;
  }
  OBELISK_RT_CATCH_ALL {
    state->handles.erase(reinterpret_cast<uintptr_t>(current));
    setError(state, "could not resolve synthetic hierarchical VPI name",
             vpiSystem);
    return nullptr;
  }
}

size_t indexedParentPrefixCount(const __vpiHandle *handle) {
  if (!handle || handle->selectionSteps.empty() ||
      handle->exactVpiType == vpiPortBit)
    return 0;
  if (handle->selectionSteps.back().memberSelection)
    return handle->selectionSteps.size() - 1;
  if (handle->selectionSteps.back().aggregateBoundary)
    return handle->selectionSteps.size() - 1;
  const bool packed = handle->selectionSteps.back().packed;
  size_t firstInGroup = handle->selectionSteps.size() - 1;
  while (firstInGroup != 0 &&
         !handle->selectionSteps[firstInGroup - 1].memberSelection &&
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

bool isDimensionForVPIObject(uint32_t publicType, uint32_t semanticKind,
                             uint32_t semanticFlags,
                             bool suppressDimension = false) {
  if (publicType == vpiIODecl) {
    if (suppressDimension)
      return false;
    if (semanticKind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY ||
        isUnpackedDimension(semanticKind))
      return true;
    return (semanticFlags & OBELISK_RT_DESIGN_SEMANTIC_HAS_RANGE) != 0 &&
           (semanticKind == OBELISK_RT_DESIGN_SEMANTIC_BIT ||
            semanticKind == OBELISK_RT_DESIGN_SEMANTIC_LOGIC ||
            semanticKind == OBELISK_RT_DESIGN_SEMANTIC_REG);
  }
  if (isDimensionForTypespec(publicType, semanticKind, semanticFlags,
                             suppressDimension))
    return true;
  if (suppressDimension ||
      (semanticKind != OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY &&
       semanticKind != OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY))
    return false;
  const auto *access = obelisk::reflection::findVPIIndexedAccess(publicType);
  return access &&
         access->accessKind ==
             obelisk::reflection::VPIIndexedAccessKind::RelationElement;
}

bool relationElementSelectorForIteration(uint32_t sourceType,
                                         uint32_t requestedSelector,
                                         uint32_t &storageSelector) {
  const auto *access = obelisk::reflection::findVPIIndexedAccess(sourceType);
  if (!access || access->accessKind !=
                     obelisk::reflection::VPIIndexedAccessKind::RelationElement)
    return false;
  if (requestedSelector != access->relationSelector) {
    const auto *edge = obelisk::reflection::findVPITraversal(
        sourceType, requestedSelector,
        obelisk::reflection::VPITraversalMode::Iterate);
    if (!edge || edge->order != obelisk::reflection::VPITraversalOrder::Index ||
        obelisk::reflection::vpiObjectSets[static_cast<uint16_t>(edge->targets)]
                .kindCount != 1 ||
        !obelisk::reflection::vpiObjectSetContains(edge->targets,
                                                   access->terminalResult))
      return false;
  }
  storageSelector = access->relationSelector;
  return true;
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
      selector == vpiTypespec && !isTypespecVPIKind(sourceType)) {
    if (sourceType == vpiRefObj) {
      VPIRelationRange range{};
      obelisk_rt_design_cursor_v1 actual{};
      uint32_t actualType = 0;
      bool actualStatement = false;
      if (obelisk_rt_cached_vpi_relation_range(handle->owner->context,
                                               handle->cursor, vpiActual, false,
                                               &range) != OBELISK_RT_OK ||
          obelisk_rt_cached_vpi_relation_target(
              handle->owner->context, range.first, &actual, &actualType,
              &actualStatement) != OBELISK_RT_OK ||
          actualStatement ||
          !obelisk::runtime::hasRefObjectTypespecActual(actualType))
        return nullptr;
    }
    const auto *relation = obelisk::reflection::findVPITraversal(
        sourceType, vpiTypespec, obelisk::reflection::VPITraversalMode::Handle);
    if (!relation)
      return nullptr;
    uint32_t targetType = info.public_vpi_kind;
    if (info.alias_object.offset != 0 ||
        (info.kind == OBELISK_RT_DESIGN_SEMANTIC_VIRTUAL_INTERFACE &&
         info.identity_target.offset != 0)) {
      obelisk_rt_design_cursor_v1 target = info.alias_object.offset != 0
                                               ? info.alias_object
                                               : info.identity_target;
      if (obelisk_rt_cached_vpi_type(handle->owner->context, target,
                                     &targetType) != OBELISK_RT_OK)
        return nullptr;
    }
    if (!obelisk::reflection::vpiObjectSetContains(relation->targets,
                                                   targetType))
      return nullptr;
    return makeTypespecHandle(handle->owner, handle->cursor, semantic, true,
                              handle->suppressSemanticDimension);
  }

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
  iterator.useAllocationScheme = source.allocationScheme;
  iterator.useSelectionSteps = source.selectionSteps;
}

vpiHandle makeUseHandle(__vpiHandle *iterator) {
  if (iterator->interModPathIterator)
    return makeInterModPathHandle(iterator->owner,
                                  iterator->interModPathEndpoints);
  if (!iterator->hasUse)
    return nullptr;
  if (iterator->useForm == VPIObjectForm::Design) {
    vpiHandle result =
        makeHandle(iterator->owner, iterator->useCursor, iterator->useType,
                   iterator->useStatement, iterator->useClassDefinitionOrigin);
    if (result) {
      if (__vpiHandle *handle = findHandle(result))
        handle->allocationScheme = iterator->useAllocationScheme;
    }
    return result;
  }
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
      source.allocationScheme = iterator->useAllocationScheme;
      source.form = VPIObjectForm::Indexed;
      source.exactVpiType = iterator->useType;
      source.selectionSteps = iterator->useSelectionSteps;
      if (source.selectionAccessKind ==
          obelisk::reflection::VPIIndexedAccessKind::RelationElement)
        return makeIndexedPrefix(&source, source.selectionSteps.size());
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
    if (handle)
      handle->allocationScheme = iterator->useAllocationScheme;
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
      isDimensionForVPIObject(sourceType, info.kind, info.flags,
                              source->suppressSemanticDimension))
    kind = VPISemanticIteratorKind::Ranges;
  else if (selector == vpiTypespecMember &&
           (info.kind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_STRUCT ||
            info.kind == OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_STRUCT ||
            info.kind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_UNION ||
            info.kind == OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_UNION))
    kind = VPISemanticIteratorKind::TypespecMembers;
  else if ((selector == vpiElement || selector == vpiBit) &&
           (info.kind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY ||
            info.kind == OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY ||
            ((info.kind == OBELISK_RT_DESIGN_SEMANTIC_BIT ||
              info.kind == OBELISK_RT_DESIGN_SEMANTIC_LOGIC ||
              info.kind == OBELISK_RT_DESIGN_SEMANTIC_REG) &&
             (info.flags & OBELISK_RT_DESIGN_SEMANTIC_HAS_RANGE) != 0)))
    kind = VPISemanticIteratorKind::Elements;
  else if (selector == vpiMember &&
           (info.kind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_STRUCT ||
            info.kind == OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_STRUCT))
    kind = VPISemanticIteratorKind::ObjectMembers;
  if (kind == VPISemanticIteratorKind::None)
    return nullptr;
  OBELISK_RT_TRY {
    auto iterator = std::make_unique<__vpiHandle>();
    iterator->owner = source->owner;
    iterator->kind = VPIHandleKind::Iterator;
    iterator->iteratorType = selector;
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
  bool shortReal = isShortRealVPIType(type);
  bool real = type == vpiRealVar || type == vpiRealNet;
  if (!shortReal && !real) {
    obelisk_rt_design_cursor_v1 cursor{};
    obelisk_rt_design_semantic_type_info_v1 semantic{};
    if (semanticCursorFor(handle, cursor) &&
        obelisk_rt_cached_design_semantic_type_info(
            handle->owner->context, cursor, &semantic) == OBELISK_RT_OK) {
      shortReal = semantic.kind == OBELISK_RT_DESIGN_SEMANTIC_SHORT_REAL;
      real = semantic.kind == OBELISK_RT_DESIGN_SEMANTIC_REAL ||
             semantic.kind == OBELISK_RT_DESIGN_SEMANTIC_REALTIME;
    }
  }
  if (shortReal) {
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
  if (real) {
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

bool readValue(__vpiHandle *handle, const VPIValueSource &source,
               std::vector<uint64_t> &value, std::vector<uint64_t> &unknown) {
  const auto &info = source.info;
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
  if (source.form == VPIObjectForm::IntegralConstant) {
    value[0] = static_cast<uint64_t>(handle->integralValue);
    return true;
  }
  if (source.form == VPIObjectForm::Indexed) {
    if (obelisk_rt_read_design_slice(
            handle->owner->context, source.cursor, source.selectionBitOffset,
            info.bit_width, value.data(), unknown.data()) != OBELISK_RT_OK) {
      setError(handle->owner, "VPI indexed design read failed");
      return false;
    }
    return true;
  }
  obelisk_rt_status status =
      obelisk_rt_v1_design_read(handle->owner->context, source.cursor,
                                value.data(), unknown.data(), info.bit_width);
  if (status != OBELISK_RT_OK) {
    setError(handle->owner, "VPI design read failed");
    return false;
  }
  return true;
}

bool readManagedStateWord(__vpiHandle *handle,
                          const obelisk_rt_design_info_v1 &info,
                          VPIObjectForm form,
                          obelisk_rt_design_cursor_v1 cursor,
                          uint64_t selectionBitOffset, uint64_t &word) {
  obelisk_rt_design_type_info_v1 type{};
  if (info.type_offset == 0 ||
      obelisk_rt_cached_design_type_info(
          handle->owner->context, {info.type_offset}, &type) != OBELISK_RT_OK ||
      type.kind != OBELISK_RT_DESIGN_TYPE_SCALAR || type.bit_width != 64 ||
      info.bit_width != 64 ||
      (form != VPIObjectForm::Design && form != VPIObjectForm::Indexed)) {
    setError(handle->owner, "invalid managed VPI storage representation",
             vpiInternal);
    return false;
  }
  uint64_t unknown = 0;
  obelisk_rt_status status =
      form == VPIObjectForm::Indexed
          ? obelisk_rt_read_design_slice(handle->owner->context, cursor,
                                         selectionBitOffset, 64, &word,
                                         &unknown)
          : obelisk_rt_v1_design_read(handle->owner->context, cursor, &word,
                                      &unknown, 64);
  if (status != OBELISK_RT_OK) {
    setError(handle->owner, "managed VPI state read failed", vpiInternal);
    return false;
  }
  if (unknown != 0) {
    setError(handle->owner, "managed VPI state contains unknown bits",
             vpiInternal);
    return false;
  }
  if (type.flags != 0) {
    setError(handle->owner, "invalid managed VPI storage representation",
             vpiInternal);
    return false;
  }
  return true;
}

bool readManagedStateWord(__vpiHandle *handle, uint64_t &word) {
  obelisk_rt_design_info_v1 info{};
  if (!infoFor(handle, info))
    return false;
  return readManagedStateWord(handle, info, handle->form, handle->cursor,
                              handle->selectionBitOffset, word);
}

bool readManagedStateWord(__vpiHandle *handle, const VPIValueSource &source,
                          uint64_t &word) {
  return readManagedStateWord(handle, source.info, source.form, source.cursor,
                              source.selectionBitOffset, word);
}

class ScopedManagedWordRoot {
public:
  ScopedManagedWordRoot(obelisk_rt_gc_lane_v1 *lane,
                        obelisk_rt_managed_word_v1 *word)
      : lane(lane),
        status(obelisk_rt_v1_gc_managed_root_push(lane, &root, word)) {}
  ScopedManagedWordRoot(const ScopedManagedWordRoot &) = delete;
  ScopedManagedWordRoot &operator=(const ScopedManagedWordRoot &) = delete;
  ~ScopedManagedWordRoot() {
    if (status == OBELISK_RT_OK)
      (void)obelisk_rt_v1_gc_managed_root_pop(lane, &root);
  }
  obelisk_rt_status getStatus() const { return status; }

private:
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  obelisk_rt_gc_managed_root_v1 root{};
  obelisk_rt_status status = OBELISK_RT_INVALID_HANDLE;
};

bool managedStringLength(__vpiHandle *handle,
                         const obelisk_rt_design_info_v1 &info,
                         VPIObjectForm form, obelisk_rt_design_cursor_v1 cursor,
                         uint64_t selectionBitOffset, PLI_INT32 &length) {
  ContextTransaction transaction(handle->owner->context);
  ManagedExecutionScope managed(handle->owner->context);
  if (managed.getStatus() != OBELISK_RT_OK || !managed.getLane()) {
    setError(handle->owner, "managed string VPI query cannot enter GC scope",
             vpiInternal);
    return false;
  }
  uint64_t string = 0;
  if (!readManagedStateWord(handle, info, form, cursor, selectionBitOffset,
                            string))
    return false;
  if (obelisk_rt_v1_gc_candidate_root(handle->owner->context, string,
                                      OBELISK_RT_MANAGED_ROOT_KIND_STRING) !=
      string) {
    setError(handle->owner, "invalid managed string in VPI design state",
             vpiInternal);
    return false;
  }
  ScopedManagedWordRoot root(managed.getLane(), &string);
  if (root.getStatus() != OBELISK_RT_OK ||
      obelisk_rt_validate_string(handle->owner->context, string) !=
          OBELISK_RT_OK) {
    setError(handle->owner, "invalid managed string in VPI design state",
             vpiInternal);
    return false;
  }
  length = static_cast<PLI_INT32>(std::min<uint64_t>(
      obelisk_rt_v1_string_length(static_cast<obelisk_rt_string_v1>(string)),
      INT32_MAX));
  return true;
}

bool logicBit(const std::vector<uint64_t> &plane, uint64_t bit) {
  return (plane[static_cast<size_t>(bit / 64)] & (uint64_t{1} << (bit % 64))) !=
         0;
}

void setLogicBit(std::vector<uint64_t> &plane, uint64_t bit) {
  plane[static_cast<size_t>(bit / 64)] |= uint64_t{1} << (bit % 64);
}

bool valueSigned(__vpiHandle *handle,
                 const obelisk_rt_design_info_v1 &objectInfo, PLI_INT32 type,
                 bool hasSemanticCursor,
                 obelisk_rt_design_cursor_v1 semanticCursor) {
  if (type == vpiNetBit || type == vpiRegBit || type == vpiBitSelect ||
      type == vpiPartSelect || type == vpiIndexedPartSelect)
    return false;
  if (hasSemanticCursor) {
    obelisk_rt_design_semantic_type_info_v1 info{};
    if (obelisk_rt_cached_design_semantic_type_info(
            handle->owner->context, semanticCursor, &info) == OBELISK_RT_OK)
      return (info.flags & OBELISK_RT_DESIGN_SEMANTIC_SIGNED) != 0;
  }
  if (objectInfo.type_offset != 0) {
    obelisk_rt_design_type_info_v1 typeInfo{};
    if (obelisk_rt_cached_design_type_info(handle->owner->context,
                                           {objectInfo.type_offset},
                                           &typeInfo) == OBELISK_RT_OK)
      return (typeInfo.flags & OBELISK_RT_DESIGN_TYPE_SIGNED) != 0;
  }
  // Legacy immutable records may lack both semantic and physical type
  // metadata. The predefined integer kinds are signed unless explicitly
  // declared otherwise, which current records preserve in one of those two
  // metadata graphs.
  switch (type) {
  case vpiIntegerNet:
  case vpiByteNet:
  case vpiShortIntNet:
  case vpiIntNet:
  case vpiLongIntNet:
  case vpiIntegerVar:
  case vpiByteVar:
  case vpiShortIntVar:
  case vpiIntVar:
  case vpiLongIntVar:
    return true;
  default:
    return false;
  }
}

bool valueSigned(__vpiHandle *handle,
                 const obelisk_rt_design_info_v1 &objectInfo) {
  obelisk_rt_design_cursor_v1 semanticCursor{};
  const bool hasSemanticCursor = semanticCursorFor(handle, semanticCursor);
  return valueSigned(handle, objectInfo, vpiTypeForHandle(handle),
                     hasSemanticCursor, semanticCursor);
}

bool valueSigned(__vpiHandle *handle, const VPIValueSource &source) {
  return valueSigned(handle, source.info,
                     static_cast<PLI_INT32>(source.exactType),
                     source.hasSemanticCursor, source.semanticCursor);
}

bool hasUnknownBits(const std::vector<uint64_t> &unknown, uint64_t width) {
  const size_t fullWords = static_cast<size_t>(width / 64);
  for (size_t word = 0; word != fullWords; ++word)
    if (unknown[word] != 0)
      return true;
  const unsigned tail = static_cast<unsigned>(width % 64);
  return tail != 0 && (unknown[fullWords] & ((uint64_t{1} << tail) - 1)) != 0;
}

bool formatRadixValue(std::string &result, const std::vector<uint64_t> &value,
                      const std::vector<uint64_t> &unknown, uint64_t width,
                      unsigned digitBits) {
  static constexpr char digits[] = "0123456789abcdef";
  size_t count = 0;
  if (width != 0 && !checkedWordCount(width, digitBits, count))
    return false;
  result.assign(count, '0');
  for (size_t digitIndex = 0; digitIndex != count; ++digitIndex) {
    const uint64_t firstBit = uint64_t{digitIndex} * digitBits;
    const unsigned bits =
        static_cast<unsigned>(std::min<uint64_t>(digitBits, width - firstBit));
    unsigned digit = 0;
    bool anyUnknown = false;
    bool allUnknown = true;
    bool anyX = false;
    bool anyZ = false;
    for (unsigned bit = 0; bit != bits; ++bit) {
      const uint64_t absolute = firstBit + bit;
      const bool v = logicBit(value, absolute);
      const bool u = logicBit(unknown, absolute);
      if (!u) {
        allUnknown = false;
        digit |= static_cast<unsigned>(v) << bit;
      } else {
        anyUnknown = true;
        anyX |= !v;
        anyZ |= v;
      }
    }
    char rendered = digits[digit];
    if (anyUnknown) {
      if (allUnknown && anyX && !anyZ)
        rendered = 'x';
      else if (allUnknown && anyZ && !anyX)
        rendered = 'z';
      else if (anyX)
        rendered = 'X';
      else
        rendered = 'Z';
    }
    result[count - 1 - digitIndex] = rendered;
  }
  return true;
}

void maskValueWidth(std::vector<uint64_t> &value, uint64_t width) {
  const unsigned tail = static_cast<unsigned>(width % 64);
  if (tail != 0)
    value.back() &= (uint64_t{1} << tail) - 1;
}

void negateWidth(std::vector<uint64_t> &value, uint64_t width) {
  for (uint64_t &word : value)
    word = ~word;
  uint64_t carry = 1;
  for (uint64_t &word : value) {
    const uint64_t previous = word;
    word += carry;
    carry = carry && word < previous;
  }
  maskValueWidth(value, width);
}

void formatDecimalValue(std::string &result,
                        const std::vector<uint64_t> &source,
                        const std::vector<uint64_t> &unknown, uint64_t width,
                        bool isSigned) {
  if (hasUnknownBits(unknown, width)) {
    result = "x";
    return;
  }
  std::vector<uint64_t> magnitude = source;
  maskValueWidth(magnitude, width);
  const bool negative = isSigned && logicBit(magnitude, width - 1);
  if (negative)
    negateWidth(magnitude, width);
  size_t last = magnitude.size();
  while (last != 0 && magnitude[last - 1] == 0)
    --last;
  if (last == 0) {
    result = "0";
    return;
  }
  result.clear();
  while (last != 0) {
    uint64_t remainder = 0;
    for (size_t word = last; word-- != 0;) {
      const unsigned __int128 dividend =
          (static_cast<unsigned __int128>(remainder) << 64) | magnitude[word];
      magnitude[word] = static_cast<uint64_t>(dividend / 10);
      remainder = static_cast<uint64_t>(dividend % 10);
    }
    result.push_back(static_cast<char>('0' + remainder));
    while (last != 0 && magnitude[last - 1] == 0)
      --last;
  }
  if (negative)
    result.push_back('-');
  std::reverse(result.begin(), result.end());
}

long double logicToReal(const std::vector<uint64_t> &value,
                        const std::vector<uint64_t> &unknown, uint64_t width,
                        bool isSigned) {
  std::vector<uint64_t> magnitude = value;
  for (size_t word = 0; word != magnitude.size(); ++word)
    magnitude[word] &= ~unknown[word];
  const bool negative =
      isSigned && logicBit(value, width - 1) && !logicBit(unknown, width - 1);
  if (negative)
    negateWidth(magnitude, width);
  long double result = 0;
  for (uint64_t bit = width; bit-- != 0;)
    result = std::ldexp(result, 1) + (logicBit(magnitude, bit) ? 1 : 0);
  return negative ? -result : result;
}

bool packStringValue(std::string_view bytes, std::vector<uint64_t> &value,
                     std::vector<uint64_t> &unknown, uint64_t &width) {
  if (bytes.size() > UINT64_MAX / 8)
    return false;
  width = static_cast<uint64_t>(bytes.size()) * 8;
  size_t words = 0;
  if (width != 0 && !checkedWordCount(width, 64, words))
    return false;
  value.assign(words, 0);
  unknown.assign(words, 0);
  for (size_t byte = 0; byte != bytes.size(); ++byte) {
    const uint8_t character =
        static_cast<uint8_t>(bytes[bytes.size() - 1 - byte]);
    for (unsigned bit = 0; bit != 8; ++bit)
      if ((character & (uint8_t{1} << bit)) != 0)
        setLogicBit(value, static_cast<uint64_t>(byte) * 8 + bit);
  }
  return true;
}

PLI_INT32 strengthCode(unsigned distance) {
  static constexpr PLI_INT32 codes[] = {
      vpiHiZ,         vpiSmallCharge, vpiMediumCharge, vpiWeakDrive,
      vpiLargeCharge, vpiPullDrive,   vpiStrongDrive,  vpiSupplyDrive,
  };
  return codes[std::min<unsigned>(distance, 7)];
}

void decodeStrengthRange(uint16_t range, PLI_INT32 &strength0,
                         PLI_INT32 &strength1) {
  if (range == 0) {
    strength0 = vpiStrongDrive;
    strength1 = vpiStrongDrive;
    return;
  }
  unsigned low = 0;
  while (low != 15 && (range & (uint16_t{1} << low)) == 0)
    ++low;
  unsigned high = 15;
  while (high != low && (range & (uint16_t{1} << (high - 1))) == 0)
    --high;
  --high;
  strength0 = strengthCode(low <= 7 ? 7 - low : low - 7);
  strength1 = strengthCode(high <= 7 ? 7 - high : high - 7);
}

bool readNetStrength(__vpiHandle *handle, obelisk_rt_design_cursor_v1 cursor,
                     uint64_t bitOffset, uint16_t &range) {
  uint64_t stateOffset = 0;
  if (obelisk_rt_design_state_offset(handle->owner->context, cursor, bitOffset,
                                     &stateOffset) != OBELISK_RT_OK) {
    setError(handle->owner, "VPI net strength offset is unavailable",
             vpiInternal);
    return false;
  }
  const obelisk_rt_context *context = handle->owner->context;
  const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
  const bool useDirectState =
      plan && (plan->flags & OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE) != 0 &&
      !context->nativeScheduleDeoptimized && context->execution &&
      plan->state_bit_count == context->execution->state_bit_count &&
      plan->state_value && plan->state_unknown;
  if (obelisk_rt_design_net_strength(handle->owner->context, stateOffset,
                                     &range, useDirectState) == OBELISK_RT_OK)
    return true;
  setError(handle->owner, "could not retrieve VPI net strength", vpiInternal);
  return false;
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

} // namespace

extern "C" OBELISK_VPI_EXPORT const obelisk_rt_vpi_object_model_v1 *
obelisk_rt_v1_vpi_object_model(void) {
  static const obelisk_rt_vpi_object_model_v1 model{
      obelisk::reflection::vpiObjectModelImage,
      sizeof(obelisk::reflection::vpiObjectModelImage),
      obelisk::reflection::vpiObjectModelImageFingerprint};
  return &model;
}

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
  if (state->runtimeObserverCallbacks != 0) {
    state->runtimeObserverCallbacks = 0;
    ContextMutexLock lock(context);
    obelisk_rt_aot_observation_demand_changed_unlocked(context, false);
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
  __vpiHandle *base = nullptr;
  if (scope) {
    base = validate(scope);
    if (!base)
      return nullptr;
    if (!allowProtectedSource(base, "vpi_handle_by_name"))
      return nullptr;
  }
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
  std::string resolvedName = requested;
  bool found = lookup(state, requested, cursor);
  if (!found && !compatibilityName.empty()) {
    found = lookup(state, compatibilityName, cursor);
    if (found)
      resolvedName = compatibilityName;
  }
  if (!found && (!scope || absolute) &&
      (requested.size() < 2 ||
       requested.compare(requested.size() - 2, 2, "::") != 0)) {
    found = lookup(state, requested + "::", cursor);
    if (found)
      resolvedName = requested + "::";
  }
  if (!found) {
    if (protectedNameIntermediate(state, requested) ||
        (!compatibilityName.empty() &&
         protectedNameIntermediate(state, compatibilityName)))
      return nullptr;
    if (vpiHandle indexed = handleSyntheticSelectionByName(
            state, requested, sourceClassDefinitionOrigin))
      return indexed;
    if (!compatibilityName.empty())
      if (vpiHandle indexed = handleSyntheticSelectionByName(
              state, compatibilityName, sourceClassDefinitionOrigin))
        return indexed;
    setError(state, "hierarchical VPI name was not found", vpiNotice);
    return nullptr;
  }
  if (protectedNameIntermediate(state, resolvedName))
    return nullptr;
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
  // IEEE 1800-2023 37.43 detail 4 defines this NULL-root query as the
  // currently active frame. Live frame materialization is intentionally a
  // separate feature; outside procedural execution the correct result is a
  // quiet NULL, rather than treating the NULL reference as an invalid handle.
  if (!reference && type == vpiFrame) {
    if (!requireState())
      return nullptr;
    return nullptr;
  }
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
  if (handle->kind == VPIHandleKind::InterModPath)
    return nullptr;
  if (handle->kind != VPIHandleKind::Object) {
    setError(handle->owner, "wrong-kind VPI handle");
    return nullptr;
  }
  if (!allowProtectedSource(handle, "vpi_handle"))
    return nullptr;
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
  if (handle->form == VPIObjectForm::Design && type == vpiIndex) {
    const auto *edge = obelisk::reflection::findVPITraversal(
        sourceType, vpiIndex, obelisk::reflection::VPITraversalMode::Handle);
    std::vector<VPISelectionStep> indices;
    if (!edge || !staticArrayMemberIndexSteps(handle, indices) ||
        indices.empty())
      return nullptr;
    const VPISelectionStep &step = indices.back();
    return makeSemanticObjectHandle(
        handle->owner, VPIObjectForm::IntegralConstant, handle->cursor, {},
        step.selectionOrdinal, vpiConstant, step.index);
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
  if (handle->form == VPIObjectForm::Design &&
      sourceType == vpiInterconnectNet && type == vpiParent) {
    VPIArrayMemberInfo member{};
    std::vector<VPISelectionStep> indices;
    if (obelisk_rt_cached_vpi_array_member(
            handle->owner->context, handle->cursor, &member) != OBELISK_RT_OK ||
        !staticArrayMemberIndexSteps(handle, indices) || indices.empty())
      return nullptr;
    uint32_t rootType = 0;
    if (obelisk_rt_cached_vpi_type(handle->owner->context, member.array,
                                   &rootType) != OBELISK_RT_OK)
      return nullptr;
    if (indices.size() == 1)
      return makeHandle(handle->owner, member.array, rootType);
    const auto *access = obelisk::reflection::findVPIIndexedAccess(rootType);
    if (!access ||
        access->accessKind !=
            obelisk::reflection::VPIIndexedAccessKind::RelationElement)
      return nullptr;
    OBELISK_RT_TRY {
      std::vector<PLI_INT32> prefix;
      prefix.reserve(indices.size() - 1);
      for (size_t ordinal = 0; ordinal + 1 != indices.size(); ++ordinal) {
        const VPISelectionStep &step = indices[ordinal];
        if (step.index < INT32_MIN || step.index > INT32_MAX)
          return nullptr;
        prefix.push_back(static_cast<PLI_INT32>(step.index));
      }
      __vpiHandle root;
      root.owner = handle->owner;
      root.cursor = member.array;
      root.exactVpiType = rootType;
      root.selectionAccessKind = access->accessKind;
      root.protectedObject = handle->protectedObject;
      return handleRelationByIndices(
          &root, *access, static_cast<PLI_INT32>(prefix.size()), prefix.data());
    }
    OBELISK_RT_CATCH_ALL {
      setError(handle->owner,
               "could not allocate interconnect parent selection", vpiSystem);
      return nullptr;
    }
  }
  auto makeSimulatedNetHandle = [&](obelisk_rt_design_cursor_v1 target,
                                    uint32_t targetType) -> vpiHandle {
    if (handle->form == VPIObjectForm::Design)
      return makeHandle(
          handle->owner, target, targetType, false,
          obelisk::runtime::hasClassDefinitionValueOrigin(
              sourceType, handle->classDefinitionOrigin, targetType));
    if (handle->form != VPIObjectForm::Indexed ||
        handle->selectionSteps.empty())
      return nullptr;
    if (target.offset == handle->cursor.offset)
      return makeIndexedPrefix(handle, handle->selectionSteps.size());
    OBELISK_RT_TRY {
      __vpiHandle source;
      source.owner = handle->owner;
      source.form = VPIObjectForm::Indexed;
      source.cursor = target;
      source.exactVpiType = targetType;
      source.protectedObject = handle->protectedObject;
      source.allocationScheme = handle->allocationScheme;
      source.classDefinitionOrigin = handle->classDefinitionOrigin;
      source.selectionRootType = targetType;
      const auto *access =
          obelisk::reflection::findVPIIndexedAccess(targetType);
      if (!access)
        return nullptr;
      source.selectionAccessKind = access->accessKind;
      if (!fixedProtectionFor(handle->owner, target, source.protectedObject))
        return nullptr;

      obelisk_rt_design_info_v1 selectedInfo{};
      if (!infoFor(handle, selectedInfo) || selectedInfo.bit_width == 0)
        return nullptr;
      const uint64_t desiredOffset = handle->selectionBitOffset;
      const uint64_t desiredWidth = selectedInfo.bit_width;
      std::vector<VPISelectionStep> steps;
      steps.reserve(handle->selectionSteps.size());
      uint64_t containingOffset = 0;
      while (true) {
        obelisk_rt_design_info_v1 rootInfo{};
        if (obelisk_rt_cached_design_info(handle->owner->context, target,
                                          &rootInfo) != OBELISK_RT_OK ||
            rootInfo.type_offset == 0)
          return nullptr;
        obelisk_rt_design_cursor_v1 physical{
            steps.empty() ? rootInfo.type_offset
                          : steps.back().physicalType.offset};
        obelisk_rt_design_type_info_v1 typeInfo{};
        if (obelisk_rt_cached_design_type_info(handle->owner->context, physical,
                                               &typeInfo) != OBELISK_RT_OK)
          return nullptr;
        bool isArray = typeInfo.kind == OBELISK_RT_DESIGN_TYPE_ARRAY;
        bool packed = (typeInfo.flags & OBELISK_RT_DESIGN_TYPE_PACKED) != 0;
        uint64_t childWidth = 1;
        if (isArray) {
          obelisk_rt_design_type_info_v1 element{};
          if (typeInfo.element_type.offset == 0 ||
              obelisk_rt_cached_design_type_info(handle->owner->context,
                                                 typeInfo.element_type,
                                                 &element) != OBELISK_RT_OK ||
              element.bit_width == 0)
            return nullptr;
          childWidth = element.bit_width;
        } else if (!packed || typeInfo.bit_width <= 1) {
          return nullptr;
        }
        uint64_t distance =
            typeInfo.range_left >= typeInfo.range_right
                ? static_cast<uint64_t>(typeInfo.range_left) -
                      static_cast<uint64_t>(typeInfo.range_right)
                : static_cast<uint64_t>(typeInfo.range_right) -
                      static_cast<uint64_t>(typeInfo.range_left);
        if (distance == UINT64_MAX)
          return nullptr;
        uint64_t extent = distance + 1;
        if (desiredOffset < containingOffset ||
            desiredWidth > typeInfo.bit_width ||
            desiredOffset - containingOffset >
                typeInfo.bit_width - desiredWidth)
          return nullptr;
        uint64_t localOffset = desiredOffset - containingOffset;
        uint64_t storageOrdinal = localOffset / childWidth;
        if (storageOrdinal >= extent)
          return nullptr;
        uint64_t ordinal =
            packed ? extent - 1 - storageOrdinal : storageOrdinal;
        __int128 index =
            typeInfo.range_left <= typeInfo.range_right
                ? static_cast<__int128>(typeInfo.range_left) + ordinal
                : static_cast<__int128>(typeInfo.range_left) - ordinal;
        if (index < std::numeric_limits<int64_t>::min() ||
            index > std::numeric_limits<int64_t>::max() ||
            !appendIndexedSelection(&source, access->accessKind,
                                    static_cast<int64_t>(index), steps))
          return nullptr;
        containingOffset = steps.back().bitOffset;
        if (containingOffset == desiredOffset &&
            steps.back().bitWidth == desiredWidth)
          break;
        if (steps.back().bitWidth < desiredWidth ||
            desiredOffset < containingOffset ||
            desiredOffset - containingOffset >
                steps.back().bitWidth - desiredWidth)
          return nullptr;
      }
      source.selectionBitOffset = desiredOffset;
      return makeIndexedHandle(&source, targetType, access->accessKind,
                               std::move(steps));
    }
    OBELISK_RT_CATCH_ALL {
      setError(handle->owner, "could not copy simulated-net selection",
               vpiSystem);
      return nullptr;
    }
  };
  VPIRelationRange range{};
  bool indexedSimNet =
      handle->form == VPIObjectForm::Indexed && type == vpiSimNet;
  obelisk_rt_status relationStatus =
      (handle->form == VPIObjectForm::Design || indexedSimNet)
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
    if (indexedSimNet) {
      if (targetIsStatement)
        return nullptr;
      return makeSimulatedNetHandle(target, targetType);
    }
    return makeHandle(
        handle->owner, target, targetType, targetIsStatement,
        obelisk::runtime::hasClassDefinitionValueOrigin(
            sourceType, handle->classDefinitionOrigin, targetType));
  }
  if (relationStatus != OBELISK_RT_EOF)
    return nullptr;
  if (type == vpiSimNet &&
      (handle->form == VPIObjectForm::Design || indexedSimNet)) {
    uint32_t rootType = 0;
    const auto *edge = obelisk::reflection::findVPITraversal(
        sourceType, vpiSimNet, obelisk::reflection::VPITraversalMode::Handle);
    if (edge &&
        obelisk_rt_cached_vpi_type(handle->owner->context, handle->cursor,
                                   &rootType) == OBELISK_RT_OK &&
        obelisk::reflection::vpiObjectSetContains(edge->targets, rootType))
      return makeSimulatedNetHandle(handle->cursor, rootType);
  }
  if ((type == vpiLeftRange || type == vpiRightRange) &&
      (handle->form == VPIObjectForm::Design ||
       handle->form == VPIObjectForm::Indexed)) {
    const auto *edge = obelisk::reflection::findVPITraversal(
        sourceType, static_cast<uint32_t>(type),
        obelisk::reflection::VPITraversalMode::Handle);
    obelisk_rt_design_info_v1 info{};
    if (edge && infoFor(handle, info)) {
      if (handle->form == VPIObjectForm::Design && sourceType == vpiIODecl) {
        obelisk_rt_design_cursor_v1 semanticCursor{};
        obelisk_rt_design_semantic_type_info_v1 semanticInfo{};
        if (!semanticCursorFor(handle, semanticCursor) ||
            !semanticTypeInfo(handle, semanticCursor, semanticInfo) ||
            !isDimensionForVPIObject(sourceType, semanticInfo.kind,
                                     semanticInfo.flags,
                                     handle->suppressSemanticDimension))
          return nullptr;
      }
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
  if (reference) {
    __vpiHandle *path = findHandle(reference);
    if (!path)
      return nullptr;
    if (path->kind == VPIHandleKind::InterModPath) {
      if (type != vpiPorts)
        return nullptr;
      OBELISK_RT_TRY {
        auto iterator = std::make_unique<__vpiHandle>();
        iterator->owner = state;
        iterator->kind = VPIHandleKind::Iterator;
        iterator->iteratorType = type;
        iterator->interModPathEndpoints = path->interModPathEndpoints;
        iterator->interModPathIterator = true;
        return keepHandle(state, std::move(iterator));
      }
      OBELISK_RT_CATCH_ALL {
        setError(state, "could not allocate VPI intermodule-path iterator",
                 vpiSystem);
        return nullptr;
      }
    }
  }
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
      iterator->iteratorType = type;
      iterator->items = std::move(callbacks);
      iterator->callbackIterator = true;
      return keepHandle(state, std::move(iterator));
    }
    OBELISK_RT_CATCH_ALL {
      setError(state, "could not allocate VPI callback iterator", vpiSystem);
      return nullptr;
    }
  }
  if (type == vpiUserSystf && !reference) {
    std::vector<obelisk_rt_design_cursor_v1> systemTfs;
    OBELISK_RT_TRY {
      systemTfs.reserve(state->systemTfs.size());
      for (uint64_t id : state->systemTfOrder)
        if (state->systemTfs.find(id) != state->systemTfs.end())
          systemTfs.push_back({id});
      if (systemTfs.empty())
        return nullptr;
      auto iterator = std::make_unique<__vpiHandle>();
      iterator->owner = state;
      iterator->kind = VPIHandleKind::Iterator;
      iterator->iteratorType = type;
      iterator->items = std::move(systemTfs);
      iterator->systemTfIterator = true;
      return keepHandle(state, std::move(iterator));
    }
    OBELISK_RT_CATCH_ALL {
      setError(state, "could not allocate VPI system task/function iterator",
               vpiSystem);
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
      iterator->iteratorType = type;
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
  __vpiHandle *referenceHandle = nullptr;
  if (reference) {
    __vpiHandle *handle = validate(reference);
    if (!handle)
      return nullptr;
    if (!allowProtectedSource(handle, "vpi_iterate"))
      return nullptr;
    referenceHandle = handle;
    parent = handle->cursor;
    sourceStatement = handle->statement;
    sourceClassDefinitionOrigin = handle->classDefinitionOrigin;
    sourceDesign = handle->form == VPIObjectForm::Design;
    sourceType = handle->exactVpiType;
    if (sourceType == 0 &&
        obelisk_rt_cached_vpi_type(state->context, parent, &sourceType) !=
            OBELISK_RT_OK)
      return nullptr;
    if (type == vpiIndex && (handle->form == VPIObjectForm::Indexed ||
                             handle->form == VPIObjectForm::Design)) {
      const auto *edge = obelisk::reflection::findVPITraversal(
          sourceType, vpiIndex, obelisk::reflection::VPITraversalMode::Iterate);
      if (!edge || !supportsIndexQuery(handle, sourceType))
        return nullptr;
      OBELISK_RT_TRY {
        std::vector<VPISelectionStep> memberSteps;
        const std::vector<VPISelectionStep> *sourceSteps =
            &handle->selectionSteps;
        size_t first = 0;
        if (handle->form == VPIObjectForm::Design) {
          if (!staticArrayMemberIndexSteps(handle, memberSteps))
            return nullptr;
          sourceSteps = &memberSteps;
        } else {
          first = sourceType == vpiNetBit || sourceType == vpiRegBit
                      ? 0
                      : indexedParentPrefixCount(handle);
          for (size_t position = first; position != sourceSteps->size();
               ++position)
            if ((*sourceSteps)[position].memberSelection)
              first = position + 1;
        }
        auto iterator = std::make_unique<__vpiHandle>();
        iterator->owner = state;
        iterator->kind = VPIHandleKind::Iterator;
        iterator->iteratorType = type;
        iterator->semanticIterator = VPISemanticIteratorKind::Indices;
        iterator->cursor = handle->cursor;
        iterator->semanticCursor = handle->semanticCursor;
        iterator->exactVpiType = sourceType;
        iterator->indexItems.reserve(sourceSteps->size() - first);
        for (size_t index = sourceSteps->size(); index != first;)
          iterator->indexItems.push_back((*sourceSteps)[--index]);
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
  obelisk_rt_status relationStatus = OBELISK_RT_EOF;
  uint32_t storageSelector = static_cast<uint32_t>(type);
  bool relationElementSelector =
      reference &&
      relationElementSelectorForIteration(
          sourceType, static_cast<uint32_t>(type), storageSelector);
  if (reference && relationElementSelector) {
    __vpiHandle *source = referenceHandle;
    const auto *access = source ? obelisk::reflection::findVPIIndexedAccess(
                                      source->form == VPIObjectForm::Indexed
                                          ? source->selectionRootType
                                          : sourceType)
                                : nullptr;
    if (access &&
        access->accessKind ==
            obelisk::reflection::VPIIndexedAccessKind::RelationElement)
      if (vpiHandle semantic = makeSemanticIterator(source, type))
        return semantic;
  }
  if (!reference || sourceDesign) {
    relationStatus = obelisk_rt_cached_vpi_relation_range(
        state->context, parent,
        relationElementSelector ? storageSelector : static_cast<uint32_t>(type),
        true, &range);
  } else {
    __vpiHandle *source = referenceHandle;
    const auto *access = source ? obelisk::reflection::findVPIIndexedAccess(
                                      source->selectionRootType)
                                : nullptr;
    if (source && source->form == VPIObjectForm::Indexed && access &&
        access->accessKind ==
            obelisk::reflection::VPIIndexedAccessKind::RelationElement &&
        relationElementSelector) {
      relationStatus = obelisk_rt_cached_vpi_relation_range(
          state->context, parent, storageSelector, true, &range);
      uint64_t remaining = source->info.bit_width;
      uint64_t prefix = source->selectionBitOffset;
      if (relationStatus == OBELISK_RT_OK &&
          (remaining == 0 || prefix > range.count / remaining ||
           prefix * remaining > range.count ||
           remaining > range.count - prefix * remaining))
        relationStatus = OBELISK_RT_INVALID_DESIGN;
      if (relationStatus == OBELISK_RT_OK) {
        range.first += prefix * remaining;
        range.count = remaining;
      }
    }
  }
  if (relationStatus == OBELISK_RT_OK) {
    OBELISK_RT_TRY {
      auto iterator = std::make_unique<__vpiHandle>();
      iterator->owner = state;
      iterator->kind = VPIHandleKind::Iterator;
      iterator->iteratorType = type;
      iterator->relationIterator = true;
      iterator->relationRange = range;
      if (reference) {
        copyUseRecipe(*iterator, *referenceHandle);
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
    __vpiHandle *source = referenceHandle;
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
    iterator->iteratorType = type;
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
  if (iterator->systemTfIterator) {
    while (iterator->next != iterator->items.size()) {
      uint64_t id = iterator->items[iterator->next++].offset;
      if (iterator->owner->systemTfs.find(id) !=
          iterator->owner->systemTfs.end()) {
        VPIState *state = iterator->owner;
        const uintptr_t iteratorToken = iterator->token;
        vpiHandle result = makeSystemTfHandle(state, id);
        if (!result)
          state->handles.erase(iteratorToken);
        return result;
      }
    }
    iterator->owner->handles.erase(iterator->token);
    return nullptr;
  }
  if (iterator->interModPathIterator) {
    if (iterator->next == iterator->interModPathEndpoints.size()) {
      iterator->owner->handles.erase(iterator->token);
      return nullptr;
    }
    VPIState *state = iterator->owner;
    const uintptr_t iteratorToken = iterator->token;
    vpiHandle result = makeInterModPathEndpointHandle(
        state, iterator->interModPathEndpoints[iterator->next++]);
    if (!result)
      state->handles.erase(iteratorToken);
    return result;
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
          !isDimensionForVPIObject(iterator->exactVpiType, info.kind,
                                   info.flags,
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
            isDimensionForVPIObject(iterator->exactVpiType, elementInfo.kind,
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
    if (iterator->semanticIterator == VPISemanticIteratorKind::Elements) {
      obelisk_rt_design_semantic_type_info_v1 info{};
      if (!semanticTypeInfo(iterator, iterator->semanticCursor, info) ||
          (info.kind != OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY &&
           info.kind != OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY &&
           !((info.kind == OBELISK_RT_DESIGN_SEMANTIC_BIT ||
              info.kind == OBELISK_RT_DESIGN_SEMANTIC_LOGIC ||
              info.kind == OBELISK_RT_DESIGN_SEMANTIC_REG) &&
             (info.flags & OBELISK_RT_DESIGN_SEMANTIC_HAS_RANGE) != 0))) {
        state->handles.erase(iteratorToken);
        return nullptr;
      }
      uint64_t ordinal = 0, extent = 0;
      if (!sourceIndexOrdinal(info.range_left, info.range_right,
                              info.range_left, ordinal, extent) ||
          iterator->next >= extent) {
        state->handles.erase(iteratorToken);
        return nullptr;
      }
      const uint64_t position = iterator->next++;
      __int128 index = info.range_left >= info.range_right
                           ? static_cast<__int128>(info.range_left) - position
                           : static_cast<__int128>(info.range_left) + position;
      if (index < INT32_MIN || index > INT32_MAX) {
        state->handles.erase(iteratorToken);
        return nullptr;
      }
      vpiHandle use = makeUseHandle(iterator);
      __vpiHandle *source = findHandle(use);
      PLI_INT32 selected = static_cast<PLI_INT32>(index);
      vpiHandle result = source ? handleByIndices(use, 1, &selected) : nullptr;
      if (source)
        state->handles.erase(source->token);
      if (!result)
        state->handles.erase(iteratorToken);
      return result;
    }
    if (iterator->semanticIterator == VPISemanticIteratorKind::ObjectMembers) {
      obelisk_rt_design_semantic_type_info_v1 info{};
      if (!semanticTypeInfo(iterator, iterator->semanticCursor, info)) {
        state->handles.erase(iteratorToken);
        return nullptr;
      }
      uint32_t edgeIndex = 0;
      obelisk_rt_design_semantic_type_edge_v1 edge{};
      do {
        if (iterator->next >= info.edge_count) {
          state->handles.erase(iteratorToken);
          return nullptr;
        }
        edgeIndex = static_cast<uint32_t>(iterator->next++);
        if (!semanticEdge(iterator, iterator->semanticCursor, edgeIndex,
                          edge)) {
          state->handles.erase(iteratorToken);
          return nullptr;
        }
      } while (edge.role != OBELISK_RT_DESIGN_SEMANTIC_EDGE_MEMBER);
      vpiHandle use = makeUseHandle(iterator);
      __vpiHandle *source = findHandle(use);
      vpiHandle result =
          source ? handleByAggregateMember(source, edgeIndex) : nullptr;
      if (source)
        state->handles.erase(source->token);
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
  if (!opaque) {
    VPIState *state = requireState();
    if (!state)
      return vpiUndefined;
    if (property == vpiCompatibilityMode)
      return vpiMode1800v2009;
    if (property != vpiTimeUnit && property != vpiTimePrecision) {
      setError(state, "invalid NULL VPI handle for property query");
      return vpiUndefined;
    }
    int32_t exponent = 0;
    return globalTimeExponent(state, exponent) ? exponent : vpiUndefined;
  }
  __vpiHandle *handle = findHandle(opaque);
  if (!handle)
    return vpiUndefined;
  if (property == vpiType)
    return vpiTypeForHandle(handle);
  const auto *propertyDescriptor = propertyFor(handle, property);
  if (!propertyDescriptor)
    return vpiUndefined;
  using PropertyValueKind = obelisk::reflection::VPIPropertyValueKind;
  if (propertyDescriptor->valueKind != PropertyValueKind::Boolean &&
      propertyDescriptor->valueKind != PropertyValueKind::Integer) {
    setError(handle->owner, "property is not a 32-bit integer VPI property",
             vpiNotice);
    return vpiUndefined;
  }
  if (propertyDescriptor->realization ==
          obelisk::reflection::VPIPropertyRealization::FixedImage ||
      propertyDescriptor->realization ==
          obelisk::reflection::VPIPropertyRealization::DefinitionImage) {
    // Runtime-created handles have no immutable physical source and are never
    // protected.  All source-code objects use the validated sparse image.
    if (handle->kind != VPIHandleKind::Object && property == vpiIsProtected)
      return 0;
    VPIFixedPropertyValue value{};
    if (!fixedPropertyFor(handle, *propertyDescriptor, value))
      return vpiUndefined;
    if (value.kind != static_cast<uint8_t>(propertyDescriptor->valueKind)) {
      setError(handle->owner, "fixed VPI property value kind mismatch",
               vpiInternal);
      return vpiUndefined;
    }
    return static_cast<PLI_INT32>(value.payload);
  }
  if (propertyDescriptor->realization ==
      obelisk::reflection::VPIPropertyRealization::IndexedImage) {
    uint32_t value = 0;
    return indexedImagePropertyFor(handle, *propertyDescriptor, value)
               ? static_cast<PLI_INT32>(value)
               : vpiUndefined;
  }
  // Allocation provenance belongs to the handle rather than its exact kind:
  // an indexed variable has the lifetime of the object it selects.
  if (property == vpiAllocScheme)
    return handle->allocationScheme;
  if (property == vpiIteratorType && handle->kind == VPIHandleKind::Iterator)
    return handle->iteratorType;
  // Released and reclaimed handles fail findHandle() above. A live variable
  // or frame handle is therefore valid at the instant of this query.
  if (property == vpiValid)
    return 1;
  // The current runtime object representations cover the two non-transient
  // vpiHasActual cases in IEEE 1800-2023 37.61 detail 3. Objects reached in an
  // elaborated context have an actual; objects reached lexically from a class
  // definition do not. Indexed and relation-derived handles preserve that
  // provenance when they are created. Dynamic class-object, virtual-interface,
  // and frame representations will refine this branch when those handle forms
  // are introduced.
  if (property == vpiHasActual)
    return !handle->classDefinitionOrigin;
  // propertyFor() already rejected protected sources. The applicable legacy
  // scope property has the canonical false value in every other case.
  if (property == vpiProtected)
    return 0;
  uint32_t objectType = static_cast<uint32_t>(vpiTypeForHandle(handle));
  if (property == vpiSize &&
      (objectType == vpiStringVar || objectType == vpiRefObj)) {
    obelisk_rt_design_info_v1 info{};
    VPIObjectForm form = handle->form;
    obelisk_rt_design_cursor_v1 cursor = handle->cursor;
    uint64_t selectionBitOffset = handle->selectionBitOffset;
    uint32_t sourceType = objectType;
    if (objectType == vpiRefObj) {
      VPIRefActualValueSource actual{};
      if (!refActualValueSource(handle, actual))
        return vpiUndefined;
      info = actual.info;
      form = VPIObjectForm::Design;
      cursor = actual.cursor;
      selectionBitOffset = 0;
      sourceType = actual.exactType;
    } else if (!infoFor(handle, info)) {
      return vpiUndefined;
    }
    if (sourceType != vpiStringVar)
      return static_cast<PLI_INT32>(
          std::min<uint64_t>(info.bit_width, INT32_MAX));
    PLI_INT32 length = vpiUndefined;
    return managedStringLength(handle, info, form, cursor, selectionBitOffset,
                               length)
               ? length
               : vpiUndefined;
  }
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
    obelisk_rt_design_cursor_v1 semanticCursor{};
    bool usesSemanticSignedness =
        handle->form != VPIObjectForm::Design || objectType == vpiIODecl ||
        objectType == vpiRefObj || isTypespecVPIKind(objectType);
    if (usesSemanticSignedness && semanticCursorFor(handle, semanticCursor)) {
      obelisk_rt_design_semantic_type_info_v1 semantic{};
      if (obelisk_rt_cached_design_semantic_type_info(
              handle->owner->context, semanticCursor, &semantic) ==
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
  if (handle->kind != VPIHandleKind::Object) {
    setError(handle->owner, "unsupported property for VPI handle kind",
             vpiNotice);
    return vpiUndefined;
  }
  if (property == vpiStructUnionMember) {
    if (!propertyFor(handle, property))
      return vpiUndefined;
    return handle->form == VPIObjectForm::Indexed &&
           !handle->selectionSteps.empty() &&
           handle->selectionSteps.back().memberSelection;
  }
  if (property == vpiArray || property == vpiIsMemory ||
      property == vpiPacked || property == vpiArrayType ||
      property == vpiRandType || property == vpiIsRandomized) {
    // The current physical image has no class-instance variable records and
    // therefore no source rand/randc declaration or active-randomization bit.
    // Every physical variable represented here is exactly non-random until
    // those immutable/dynamic fields are added; semantic aggregate members
    // are handled above from their edge metadata.
    if (property == vpiRandType)
      return vpiNotRand;
    if (property == vpiIsRandomized)
      return 0;

    if (property == vpiArray)
      return isIndexedArrayMember(handle);

    if (property == vpiIsMemory) {
      // IEEE 1800-2023 37.20 generalizes the legacy memory object to a
      // one-dimensional vpiRegArray/vpiArrayVar; no leaf-type restriction
      // remains after that generalization.
      uint32_t dimensions = 0;
      if (!physicalUnpackedArrayDimensions(handle, dimensions))
        return vpiUndefined;
      return dimensions == 1;
    }

    if (property == vpiArrayType) {
      uint32_t dimensions = 0;
      if (!physicalUnpackedArrayDimensions(handle, dimensions))
        return vpiUndefined;
      if (dimensions != 0)
        return vpiStaticArray;
      setError(handle->owner,
               "vpiRegArray object has no physical unpacked-array type",
               vpiInternal);
      return vpiUndefined;
    }

    if (property == vpiPacked) {
      const auto *access = obelisk::reflection::findVPIIndexedAccess(
          static_cast<uint32_t>(vpiTypeForHandle(handle)));
      if (access &&
          access->accessKind ==
              obelisk::reflection::VPIIndexedAccessKind::RelationElement) {
        obelisk_rt_design_cursor_v1 semantic{};
        obelisk_rt_design_semantic_type_info_v1 semanticInfo{};
        if (!semanticCursorFor(handle, semantic) ||
            !semanticTypeInfo(handle, semantic, semanticInfo))
          return vpiUndefined;
        return semanticInfo.kind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY;
      }
    }

    obelisk_rt_design_type_info_v1 type{};
    if (!physicalTypeInfo(handle, type))
      return vpiUndefined;
    return (type.flags & OBELISK_RT_DESIGN_TYPE_PACKED) != 0;
  }
  if (property == vpiSize) {
    if (!propertyFor(handle, property))
      return vpiUndefined;
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
      if (exactType == vpiIODecl) {
        uint32_t direction =
            (info.capabilities & OBELISK_RT_DESIGN_CAP_IO_DIRECTION_MASK) >>
            OBELISK_RT_DESIGN_CAP_IO_DIRECTION_SHIFT;
        return direction == 0 ? vpiUndefined
                              : static_cast<PLI_INT32>(direction);
      }
      if (!isPort) {
        setError(handle->owner, "port property requested for non-port object",
                 vpiNotice);
        return vpiUndefined;
      }
      if ((info.capabilities & OBELISK_RT_DESIGN_CAP_PORT_REF) != 0)
        return vpiRef;
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
  if (property == vpiLineNo) {
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
  const auto *descriptor = propertyFor(handle, property);
  if (!descriptor)
    return vpiUndefined;
  if (descriptor->valueKind !=
      obelisk::reflection::VPIPropertyValueKind::Int64) {
    setError(handle->owner, "property is not a 64-bit integer VPI property",
             vpiNotice);
    return vpiUndefined;
  }
  if (descriptor->realization ==
          obelisk::reflection::VPIPropertyRealization::FixedImage ||
      descriptor->realization ==
          obelisk::reflection::VPIPropertyRealization::DefinitionImage) {
    VPIFixedPropertyValue value{};
    if (!fixedPropertyFor(handle, *descriptor, value) ||
        value.kind != static_cast<uint8_t>(descriptor->valueKind))
      return vpiUndefined;
    return static_cast<PLI_INT64>(value.payload);
  }
  if (property == vpiObjId && vpiTypeForHandle(handle) == vpiClassVar) {
    ContextTransaction transaction(handle->owner->context);
    ManagedExecutionScope managed(handle->owner->context);
    obelisk_rt_gc_lane_v1 *lane = managed.getLane();
    if (managed.getStatus() != OBELISK_RT_OK || !lane ||
        obelisk_rt_managed_lane_context(lane) != handle->owner->context) {
      setError(handle->owner, "class VPI query cannot enter GC scope",
               vpiInternal);
      return vpiUndefined;
    }
    uint64_t word = 0;
    if (!readManagedStateWord(handle, word))
      return vpiUndefined;
    if (word == 0)
      return 0;
    obelisk_rt_object_v1 *object = obelisk_rt_object_from_managed_word(word);
    ManagedObjectLease lease;
    if (!object ||
        obelisk_rt_managed_object_acquire(
            lane, object, OBELISK_RT_MANAGED_CLASS, &lease) != OBELISK_RT_OK ||
        obelisk_rt_managed_object_context(object) != handle->owner->context) {
      setError(handle->owner, "invalid class reference in VPI design state",
               vpiInternal);
      return vpiUndefined;
    }
    uint64_t identity = obelisk_rt_v1_object_id(object);
    if (identity == 0 || identity > INT64_MAX) {
      setError(handle->owner, "invalid class identity in VPI design state",
               vpiInternal);
      return vpiUndefined;
    }
    return static_cast<PLI_INT64>(identity);
  }
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
  // Preserve the long-standing extension that gives synthetic port bits and
  // process records useful names even though those names are not properties
  // in the IEEE object diagrams.  Protected objects still reject the access.
  const bool extendedName = property == vpiName || property == vpiFullName;
  const auto *propertyDescriptor =
      extendedName ? nullptr : propertyFor(handle, property);
  if (extendedName) {
    if (handle->protectedObject) {
      setError(handle->owner,
               "property access is denied for a protected VPI object",
               vpiError);
      return nullptr;
    }
  } else if (!propertyDescriptor) {
    return nullptr;
  }
  if (propertyDescriptor &&
      propertyDescriptor->valueKind !=
          obelisk::reflection::VPIPropertyValueKind::String &&
      !propertyDescriptor->symbolicString) {
    setError(handle->owner, "property is not a string VPI property", vpiNotice);
    return nullptr;
  }
  if (propertyDescriptor &&
      propertyDescriptor->realization ==
          obelisk::reflection::VPIPropertyRealization::IndexedImage) {
    uint32_t value = 0;
    if (!indexedImagePropertyFor(handle, *propertyDescriptor, value))
      return nullptr;
    const auto *symbolic = obelisk::reflection::findVPIIntegerPropertyValue(
        propertyDescriptor->property, value);
    if (!symbolic || symbolic->symbolicName[0] == '\0') {
      setError(handle->owner,
               "VPI indexed integer property has no symbolic spelling",
               vpiNotice);
      return nullptr;
    }
    OBELISK_RT_TRY {
      scratch = symbolic->symbolicName;
      return scratch.data();
    }
    OBELISK_RT_CATCH_ALL {
      setError(handle->owner, "VPI symbolic-property buffer is out of memory",
               vpiSystem);
      return nullptr;
    }
  }
  if (propertyDescriptor &&
      (propertyDescriptor->realization ==
           obelisk::reflection::VPIPropertyRealization::FixedImage ||
       propertyDescriptor->realization ==
           obelisk::reflection::VPIPropertyRealization::DefinitionImage)) {
    if (propertyDescriptor->symbolicString &&
        propertyDescriptor->valueKind ==
            obelisk::reflection::VPIPropertyValueKind::Integer) {
      VPIFixedPropertyValue value{};
      if (!fixedPropertyFor(handle, *propertyDescriptor, value) ||
          value.kind != static_cast<uint8_t>(propertyDescriptor->valueKind))
        return nullptr;
      const auto *symbolic = obelisk::reflection::findVPIIntegerPropertyValue(
          propertyDescriptor->property, static_cast<uint32_t>(value.payload));
      if (!symbolic || symbolic->symbolicName[0] == '\0') {
        setError(handle->owner,
                 "VPI integer property value has no symbolic spelling",
                 vpiNotice);
        return nullptr;
      }
      OBELISK_RT_TRY {
        scratch = symbolic->symbolicName;
        return scratch.data();
      }
      OBELISK_RT_CATCH_ALL {
        setError(handle->owner, "VPI symbolic-property buffer is out of memory",
                 vpiSystem);
        return nullptr;
      }
    }
    if (propertyDescriptor->valueKind !=
        obelisk::reflection::VPIPropertyValueKind::String) {
      setError(handle->owner, "property is not a string VPI property",
               vpiNotice);
      return nullptr;
    }
    VPIFixedPropertyValue value{};
    if (!fixedPropertyFor(handle, *propertyDescriptor, value) ||
        value.kind != static_cast<uint8_t>(propertyDescriptor->valueKind))
      return nullptr;
    if (property == vpiDefFile && value.stringSize == 0)
      return nullptr;
    OBELISK_RT_TRY {
      scratch.assign(reinterpret_cast<const char *>(value.stringData),
                     static_cast<size_t>(value.stringSize));
      return scratch.data();
    }
    OBELISK_RT_CATCH_ALL {
      setError(handle->owner, "VPI property string buffer is out of memory",
               vpiSystem);
      return nullptr;
    }
  }
  if (property == vpiDecompile) {
    if (!nameFor(handle, scratch) || scratch.empty())
      return nullptr;
    obelisk_rt_design_cursor_v1 parent{};
    const uint8_t *parentName = nullptr;
    uint64_t parentNameSize = 0;
    if (obelisk_rt_cached_design_parent(handle->owner->context, handle->cursor,
                                        &parent) == OBELISK_RT_OK &&
        obelisk_rt_cached_design_name(handle->owner->context, parent,
                                      &parentName,
                                      &parentNameSize) == OBELISK_RT_OK &&
        parentNameSize != 0 && scratch.size() > parentNameSize &&
        std::memcmp(scratch.data(), parentName,
                    static_cast<size_t>(parentNameSize)) == 0) {
      size_t separator = static_cast<size_t>(parentNameSize);
      if (scratch[separator] == '.')
        scratch.erase(0, separator + 1);
      else if (scratch.size() > separator + 1 && scratch[separator] == ':' &&
               scratch[separator + 1] == ':')
        scratch.erase(0, separator + 2);
    }
    return scratch.data();
  }
  uint32_t objectType = static_cast<uint32_t>(vpiTypeForHandle(handle));
  if (handle->form == VPIObjectForm::Indexed && objectType == vpiPortBit &&
      property == vpiName)
    return nullptr;
  if (property == vpiFullName &&
      (objectType == vpiIODecl || isSemanticObjectForm(handle->form) ||
       isTypespecVPIKind(objectType))) {
    propertyFor(handle, property);
    return nullptr;
  }
  if (property == vpiFullName && objectType == vpiRefObj &&
      handle->form == VPIObjectForm::Design) {
    const uint8_t *memberName = nullptr;
    uint64_t memberNameSize = 0;
    VPIRelationRange range{};
    obelisk_rt_design_cursor_v1 instance{};
    uint32_t instanceType = 0;
    bool statement = false;
    if (obelisk_rt_cached_design_name(handle->owner->context, handle->cursor,
                                      &memberName,
                                      &memberNameSize) != OBELISK_RT_OK ||
        memberNameSize == 0 ||
        obelisk_rt_cached_vpi_relation_range(handle->owner->context,
                                             handle->cursor, vpiInstance, false,
                                             &range) != OBELISK_RT_OK ||
        obelisk_rt_cached_vpi_relation_target(
            handle->owner->context, range.first, &instance, &instanceType,
            &statement) != OBELISK_RT_OK ||
        statement)
      return nullptr;
    const uint8_t *instanceName = nullptr;
    uint64_t instanceNameSize = 0;
    if (obelisk_rt_cached_design_name(handle->owner->context, instance,
                                      &instanceName,
                                      &instanceNameSize) != OBELISK_RT_OK ||
        instanceNameSize == 0)
      return nullptr;
    OBELISK_RT_TRY {
      scratch.assign(reinterpret_cast<const char *>(instanceName),
                     static_cast<size_t>(instanceNameSize));
      scratch.push_back('.');
      scratch.append(reinterpret_cast<const char *>(memberName),
                     static_cast<size_t>(memberNameSize));
      return scratch.data();
    }
    OBELISK_RT_CATCH_ALL {
      setError(handle->owner, "could not materialize RefObj full name",
               vpiSystem);
      return nullptr;
    }
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
  if (property == vpiFile) {
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
  if (!allowProtectedSource(handle, "vpi_get_value"))
    return;
  const auto *policy = valuePolicyFor(handle);
  if (!policy)
    return;
  VPIValueSource source{};
  if (!valueSourceFor(handle, *policy, source))
    return;
  if (destination->format == vpiObjTypeVal &&
      handle->form == VPIObjectForm::IntegralConstant)
    destination->format = vpiIntVal;
  else if (destination->format == vpiObjTypeVal)
    if (!resolveObjectTypeValueFormat(handle, source, destination->format))
      return;
  if (!obelisk::reflection::acceptsVPIValueFormat(
          *source.policy, static_cast<uint32_t>(destination->format))) {
    setError(handle->owner, "value format is not valid for this VPI object",
             vpiNotice);
    return;
  }
  if (!valueRequirementsSatisfied(handle, source.info, *policy) ||
      (source.policy != policy &&
       !valueRequirementsSatisfied(handle, source.info, *source.policy)))
    return;
  const PLI_INT32 exactType = static_cast<PLI_INT32>(source.exactType);
  const bool managedStringSource = exactType == vpiStringVar;
  std::optional<ContextTransaction> stringTransaction;
  std::optional<ManagedExecutionScope> stringManaged;
  if (managedStringSource) {
    stringTransaction.emplace(handle->owner->context);
    stringManaged.emplace(handle->owner->context);
    if (stringManaged->getStatus() != OBELISK_RT_OK ||
        !stringManaged->getLane()) {
      setError(handle->owner, "managed string VPI query cannot enter GC scope",
               vpiInternal);
      return;
    }
  }
  std::vector<uint64_t> &value = handle->owner->readValueScratch;
  std::vector<uint64_t> &unknown = handle->owner->readUnknownScratch;
  obelisk_rt_managed_word_v1 rootedString = 0;
  std::optional<ScopedManagedWordRoot> stringRoot;
  if (managedStringSource) {
    if (!readManagedStateWord(handle, source, rootedString))
      return;
    if (obelisk_rt_v1_gc_candidate_root(handle->owner->context, rootedString,
                                        OBELISK_RT_MANAGED_ROOT_KIND_STRING) !=
        rootedString) {
      setError(handle->owner, "invalid managed string in VPI design state",
               vpiInternal);
      return;
    }
    stringRoot.emplace(stringManaged->getLane(), &rootedString);
    if (stringRoot->getStatus() != OBELISK_RT_OK ||
        obelisk_rt_validate_string(handle->owner->context, rootedString) !=
            OBELISK_RT_OK) {
      setError(handle->owner, "invalid managed string in VPI design state",
               vpiInternal);
      return;
    }
  } else if (!readValue(handle, source, value, unknown)) {
    return;
  }
  uint64_t width = source.info.bit_width;
  uint32_t semanticKind = OBELISK_RT_DESIGN_SEMANTIC_UNKNOWN;
  if (source.hasSemanticCursor) {
    obelisk_rt_design_semantic_type_info_v1 semantic{};
    if (obelisk_rt_cached_design_semantic_type_info(handle->owner->context,
                                                    source.semanticCursor,
                                                    &semantic) == OBELISK_RT_OK)
      semanticKind = semantic.kind;
  }
  const bool realSource =
      semanticKind == OBELISK_RT_DESIGN_SEMANTIC_SHORT_REAL ||
      semanticKind == OBELISK_RT_DESIGN_SEMANTIC_REAL ||
      semanticKind == OBELISK_RT_DESIGN_SEMANTIC_REALTIME ||
      exactType == vpiShortRealVar || exactType == vpiShortRealNet ||
      exactType == vpiRealVar || exactType == vpiRealNet;
  const bool stringSource = semanticKind == OBELISK_RT_DESIGN_SEMANTIC_STRING ||
                            exactType == vpiStringVar;
  double sourceReal = 0;
  if (realSource) {
    if (!decodeRealBits(handle, exactType, width, value[0], sourceReal))
      return;
    if (destination->format == vpiRealVal) {
      destination->value.real = sourceReal;
      return;
    }
    if (destination->format == vpiStringVal) {
      char rendered[32]{};
      const int length =
          std::snprintf(rendered, sizeof(rendered), "%.16g", sourceReal);
      if (length < 0) {
        setError(handle->owner, "could not format real VPI value", vpiSystem);
        return;
      }
      OBELISK_RT_TRY {
        handle->owner->valueStringScratch.assign(rendered,
                                                 static_cast<size_t>(length));
        destination->value.str = handle->owner->valueStringScratch.data();
      }
      OBELISK_RT_CATCH_ALL {
        setError(handle->owner, "could not format real VPI value", vpiSystem);
      }
      return;
    }
    const long double rounded =
        std::round(static_cast<long double>(sourceReal));
    if (!std::isfinite(sourceReal) ||
        rounded <
            static_cast<long double>(std::numeric_limits<int64_t>::min()) ||
        rounded >
            static_cast<long double>(std::numeric_limits<int64_t>::max())) {
      setError(handle->owner, "real VPI value cannot be converted to integer",
               vpiNotice);
      return;
    }
    OBELISK_RT_TRY {
      value.assign(1, static_cast<uint64_t>(static_cast<int64_t>(rounded)));
      unknown.assign(1, 0);
    }
    OBELISK_RT_CATCH_ALL {
      setError(handle->owner, "could not convert real VPI value", vpiSystem);
      return;
    }
    width = 64;
  } else if (stringSource) {
    char scratch[8]{};
    const char *bytes = nullptr;
    uint64_t size = 0;
    obelisk_rt_string_v1 string =
        managedStringSource ? rootedString
                            : static_cast<obelisk_rt_string_v1>(value[0]);
    if (obelisk_rt_v1_string_view(string, scratch, &bytes, &size) !=
        OBELISK_RT_OK) {
      setError(handle->owner, "could not read SystemVerilog string value",
               vpiInternal);
      return;
    }
    if (destination->format == vpiStringVal) {
      if (size > std::numeric_limits<size_t>::max()) {
        setError(handle->owner,
                 "SystemVerilog string exceeds VPI host capacity", vpiSystem);
        return;
      }
      OBELISK_RT_TRY {
        handle->owner->valueStringScratch.assign(bytes ? bytes : "",
                                                 static_cast<size_t>(size));
        destination->value.str = handle->owner->valueStringScratch.data();
      }
      OBELISK_RT_CATCH_ALL {
        setError(handle->owner, "could not materialize VPI string value",
                 vpiSystem);
      }
      return;
    }
    OBELISK_RT_TRY {
      if (size > std::numeric_limits<size_t>::max() ||
          !packStringValue(
              std::string_view(bytes ? bytes : "", static_cast<size_t>(size)),
              value, unknown, width)) {
        setError(handle->owner,
                 "SystemVerilog string exceeds VPI host capacity", vpiSystem);
        return;
      }
    }
    OBELISK_RT_CATCH_ALL {
      setError(handle->owner, "could not convert VPI string value", vpiSystem);
      return;
    }
  }
  const bool isSigned = valueSigned(handle, source) || realSource;
  OBELISK_RT_TRY {
    switch (destination->format) {
    case vpiVectorVal: {
      size_t words = 0;
      if (width != 0 && !checkedWordCount(width, 32, words)) {
        setError(handle->owner, "VPI vector result exceeds host capacity",
                 vpiSystem);
        return;
      }
      handle->owner->vectorScratch.resize(words);
      destination->value.vector = handle->owner->vectorScratch.data();
      for (size_t word = 0; word != words; ++word) {
        uint32_t a = 0, b = 0;
        for (unsigned bit = 0; bit != 32; ++bit) {
          size_t absolute = word * 32 + bit;
          if (absolute >= width)
            break;
          bool v = logicBit(value, absolute);
          bool u = logicBit(unknown, absolute);
          a |= static_cast<uint32_t>(v != u) << bit;
          b |= static_cast<uint32_t>(u) << bit;
        }
        destination->value.vector[word] = {a, b};
      }
      break;
    }
    case vpiIntVal:
      destination->value.integer =
          width == 0 ? 0 : static_cast<PLI_INT32>(value[0] & ~unknown[0]);
      break;
    case vpiScalarVal: {
      bool v = width != 0 && logicBit(value, 0);
      bool u = width != 0 && logicBit(unknown, 0);
      destination->value.scalar = !u ? (v ? vpi1 : vpi0) : (v ? vpiZ : vpiX);
      if (width != 0 && source.info.kind == OBELISK_RT_DESIGN_RECORD_NET && u &&
          !v) {
        const uint64_t offset = source.form == VPIObjectForm::Indexed
                                    ? source.selectionBitOffset
                                    : 0;
        uint16_t range = 0;
        if (!readNetStrength(handle, source.cursor, offset, range))
          return;
        constexpr uint16_t lowMask = (uint16_t{1} << 7) - 1;
        constexpr uint16_t highZ = uint16_t{1} << 7;
        constexpr uint16_t highMask = static_cast<uint16_t>(
            ((uint16_t{1} << 15) - 1) & ~(lowMask | highZ));
        if ((range & highZ) != 0 && (range & lowMask) != 0 &&
            (range & highMask) == 0)
          destination->value.scalar = vpiL;
        else if ((range & highZ) != 0 && (range & highMask) != 0 &&
                 (range & lowMask) == 0)
          destination->value.scalar = vpiH;
      }
      break;
    }
    case vpiRealVal:
      destination->value.real = static_cast<double>(
          width == 0 ? 0 : logicToReal(value, unknown, width, isSigned));
      break;
    case vpiBinStrVal:
      if (!formatRadixValue(handle->owner->valueStringScratch, value, unknown,
                            width, 1)) {
        setError(handle->owner, "VPI binary result exceeds host capacity",
                 vpiSystem);
        return;
      }
      destination->value.str = handle->owner->valueStringScratch.data();
      break;
    case vpiOctStrVal:
      if (!formatRadixValue(handle->owner->valueStringScratch, value, unknown,
                            width, 3)) {
        setError(handle->owner, "VPI octal result exceeds host capacity",
                 vpiSystem);
        return;
      }
      destination->value.str = handle->owner->valueStringScratch.data();
      break;
    case vpiHexStrVal:
      if (!formatRadixValue(handle->owner->valueStringScratch, value, unknown,
                            width, 4)) {
        setError(handle->owner, "VPI hexadecimal result exceeds host capacity",
                 vpiSystem);
        return;
      }
      destination->value.str = handle->owner->valueStringScratch.data();
      break;
    case vpiDecStrVal:
      if (width == 0)
        handle->owner->valueStringScratch = "0";
      else
        formatDecimalValue(handle->owner->valueStringScratch, value, unknown,
                           width, isSigned);
      destination->value.str = handle->owner->valueStringScratch.data();
      break;
    case vpiStringVal: {
      size_t byteCount = 0;
      if (width != 0 && !checkedWordCount(width, 8, byteCount)) {
        setError(handle->owner, "VPI string result exceeds host capacity",
                 vpiSystem);
        return;
      }
      handle->owner->valueStringScratch.assign(byteCount, '\0');
      for (size_t byte = 0; byte != byteCount; ++byte) {
        uint8_t character = 0;
        for (unsigned bit = 0; bit != 8; ++bit) {
          const uint64_t absolute = static_cast<uint64_t>(byte) * 8 + bit;
          if (absolute < width && logicBit(value, absolute) &&
              !logicBit(unknown, absolute))
            character |= uint8_t{1} << bit;
        }
        handle->owner->valueStringScratch[byteCount - 1 - byte] =
            character == 0 ? ' ' : static_cast<char>(character);
      }
      destination->value.str = handle->owner->valueStringScratch.data();
      break;
    }
    case vpiTimeVal:
      handle->owner->timeScratch = {};
      handle->owner->timeScratch.type = vpiSimTime;
      if (width != 0) {
        const uint64_t integral = value[0] & ~unknown[0];
        handle->owner->timeScratch.high =
            static_cast<PLI_UINT32>(integral >> 32);
        handle->owner->timeScratch.low = static_cast<PLI_UINT32>(integral);
      }
      destination->value.time = &handle->owner->timeScratch;
      break;
    case vpiStrengthVal: {
      if (width > std::numeric_limits<size_t>::max()) {
        setError(handle->owner, "VPI strength result exceeds host capacity",
                 vpiSystem);
        return;
      }
      handle->owner->strengthScratch.resize(static_cast<size_t>(width));
      const bool net = source.info.kind == OBELISK_RT_DESIGN_RECORD_NET;
      for (uint64_t bit = 0; bit != width; ++bit) {
        const bool v = logicBit(value, bit);
        const bool u = logicBit(unknown, bit);
        s_vpi_strengthval &strength =
            handle->owner->strengthScratch[static_cast<size_t>(bit)];
        strength.logic = !u ? (v ? vpi1 : vpi0) : (v ? vpiZ : vpiX);
        strength.s0 = vpiStrongDrive;
        strength.s1 = vpiStrongDrive;
        if (net) {
          uint16_t range = 0;
          const uint64_t base = source.form == VPIObjectForm::Indexed
                                    ? source.selectionBitOffset
                                    : 0;
          if (bit > UINT64_MAX - base) {
            setError(handle->owner, "VPI net strength offset is out of range",
                     vpiInternal);
            return;
          }
          const uint64_t offset = base + bit;
          if (!readNetStrength(handle, source.cursor, offset, range))
            return;
          decodeStrengthRange(range, strength.s0, strength.s1);
        }
      }
      destination->value.strength = handle->owner->strengthScratch.data();
      break;
    }
    default:
      setError(handle->owner, "unsupported VPI read format", vpiInternal);
      break;
    }
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    setError(handle->owner, "VPI value result is out of memory", vpiSystem);
  }
  OBELISK_RT_CATCH_ALL {
    setError(handle->owner, "could not convert VPI value", vpiInternal);
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
      if (!allowProtectedSource(handle, "vpi_get_time"))
        return;
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
      left->kind == VPIHandleKind::SystemTf ||
      left->kind == VPIHandleKind::TimeQueue)
    return left->cursor.offset == right->cursor.offset;
  if (left->kind == VPIHandleKind::InterModPath)
    return left->interModPathEndpoints.size() ==
               right->interModPathEndpoints.size() &&
           std::equal(left->interModPathEndpoints.begin(),
                      left->interModPathEndpoints.end(),
                      right->interModPathEndpoints.begin(),
                      sameInterModPathEndpoint);
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
                          a.memberSemanticParent.offset ==
                              b.memberSemanticParent.offset &&
                          a.packed == b.packed &&
                          a.arrayDimension == b.arrayDimension &&
                          a.memberSelection == b.memberSelection &&
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
    } else
      acquireObservationDemand(state, callbackData->reason);
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

extern "C" OBELISK_VPI_EXPORT void
vpi_get_systf_info(vpiHandle opaque, p_vpi_systf_data destination) {
  beginVPICall();
  VPIState *state = requireState();
  if (destination)
    *destination = {};
  if (!state)
    return;
  if (!destination) {
    setError(state, "VPI system task/function info destination is null");
    return;
  }
  __vpiHandle *handle = validate(opaque, VPIHandleKind::SystemTf);
  VPISystemTf *systemTf = findSystemTf(handle);
  if (!systemTf) {
    if (handle)
      setError(state, "VPI system task/function registration is unavailable",
               vpiInternal);
    return;
  }
  destination->type = systemTf->type;
  destination->sysfunctype = systemTf->sysfunctype;
  destination->tfname = reinterpret_cast<PLI_BYTE8 *>(systemTf->name.data());
  destination->calltf = systemTf->calltf;
  destination->compiletf = systemTf->compiletf;
  destination->sizetf = systemTf->sizetf;
  destination->user_data = systemTf->userData;
}

extern "C" OBELISK_VPI_EXPORT vpiHandle
vpi_register_systf(p_vpi_systf_data registration) {
  beginVPICall();
  VPIState *state = currentState();
  if (!state || !registration) {
    setError(state, "VPI system task/function registration requires data");
    return nullptr;
  }
  if (state->phase != VPIPhase::StartupRestricted &&
      state->phase != VPIPhase::BeforeEndCompile) {
    setError(state, "VPI system task/function registration must occur before "
                    "elaboration");
    return nullptr;
  }
  if (registration->type != vpiSysTask && registration->type != vpiSysFunc) {
    setError(state,
             "VPI system task/function type must be vpiSysTask or vpiSysFunc");
    return nullptr;
  }
  if (registration->type == vpiSysFunc &&
      (registration->sysfunctype < vpiIntFunc ||
       registration->sysfunctype > vpiSizedSignedFunc)) {
    setError(state, "unsupported VPI system function return type");
    return nullptr;
  }
  if (!registration->tfname || registration->tfname[0] != '$' ||
      registration->tfname[1] == '\0') {
    setError(state, "VPI system task/function name must start with '$' and be "
                    "nonempty");
    return nullptr;
  }
  for (const unsigned char *character =
           reinterpret_cast<const unsigned char *>(registration->tfname + 1);
       *character; ++character) {
    if ((*character >= 'A' && *character <= 'Z') ||
        (*character >= 'a' && *character <= 'z') ||
        (*character >= '0' && *character <= '9') || *character == '_' ||
        *character == '$')
      continue;
    setError(state, "VPI system task/function name is not a simple identifier");
    return nullptr;
  }

  OBELISK_RT_TRY {
    if (state->nextSystemTfId == std::numeric_limits<uint64_t>::max()) {
      setError(state, "VPI system task/function identifier space is exhausted",
               vpiSystem);
      return nullptr;
    }
    const uint64_t id = state->nextSystemTfId++;
    VPISystemTf systemTf;
    systemTf.id = id;
    systemTf.type = registration->type;
    systemTf.sysfunctype =
        registration->type == vpiSysFunc ? registration->sysfunctype : 0;
    systemTf.name = registration->tfname;
    systemTf.calltf = registration->calltf;
    systemTf.compiletf = registration->compiletf;
    systemTf.sizetf =
        registration->type == vpiSysFunc ? registration->sizetf : nullptr;
    systemTf.userData = registration->user_data;
    auto inserted = state->systemTfs.try_emplace(id, std::move(systemTf));
    if (!inserted.second) {
      setError(state, "VPI system task/function identifier collision",
               vpiInternal);
      return nullptr;
    }
    OBELISK_RT_TRY { state->systemTfOrder.push_back(id); }
    OBELISK_RT_CATCH_ALL {
      state->systemTfs.erase(id);
      OBELISK_RT_RETHROW;
    }
    vpiHandle result = makeSystemTfHandle(state, id);
    if (!result) {
      state->systemTfs.erase(id);
      state->systemTfOrder.pop_back();
    }
    return result;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    setError(state, "VPI system task/function registry is out of memory",
             vpiSystem);
    return nullptr;
  }
  OBELISK_RT_CATCH_ALL {
    setError(state, "could not register VPI system task/function", vpiInternal);
    return nullptr;
  }
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32
vpi_get_vlog_info(p_vpi_vlog_info info) {
  beginVPICall();
  static char product[] = "Obelisk";
  static char version[] = "prototype";
  VPIState *state = requireState();
  if (!state || !info)
    return 0;
  *info = {};
  OBELISK_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(state->context->mutex);
    state->vlogArgumentScratch = state->context->vpiArguments;
    if (state->vlogArgumentScratch.empty())
      state->vlogArgumentScratch.emplace_back("obelisk");
    state->vlogArgumentPointers.clear();
    state->vlogArgumentPointers.reserve(state->vlogArgumentScratch.size());
    for (std::string &argument : state->vlogArgumentScratch)
      state->vlogArgumentPointers.push_back(argument.data());
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    setError(state, "VPI invocation argument result is out of memory",
             vpiSystem);
    return 0;
  }
  OBELISK_RT_CATCH_ALL {
    setError(state, "could not snapshot VPI invocation arguments", vpiInternal);
    return 0;
  }
  if (state->vlogArgumentPointers.size() > static_cast<size_t>(INT32_MAX)) {
    setError(state, "VPI invocation argument count exceeds ABI", vpiSystem);
    return 0;
  }
  info->argc = static_cast<PLI_INT32>(state->vlogArgumentPointers.size());
  info->argv = state->vlogArgumentPointers.empty()
                   ? nullptr
                   : state->vlogArgumentPointers.data();
  info->product = product;
  info->version = version;
  return 1;
}

extern "C" OBELISK_VPI_EXPORT PLI_INT32 vpi_get_data(PLI_INT32, PLI_BYTE8 *,
                                                     PLI_INT32) {
  beginVPICall();
  VPIState *state = requireState();
  if (!state)
    return 0;
  setError(state, "VPI save/restart data is unavailable", vpiNotice);
  return 0;
}

extern "C" OBELISK_VPI_EXPORT void vpi_get_delays(vpiHandle opaque,
                                                  p_vpi_delay destination) {
  beginVPICall();
  VPIState *state = requireState();
  if (!state)
    return;
  if (!destination) {
    setError(state, "VPI delay destination is null");
    return;
  }
  __vpiHandle *handle = findHandle(opaque);
  if (!handle)
    return;
  const bool interModPath = handle->kind == VPIHandleKind::InterModPath;
  if (!interModPath && !allowProtectedSource(handle, "vpi_get_delays"))
    return;
  if (!interModPath && handle->kind != VPIHandleKind::Object) {
    setError(state, "VPI handle does not have delay metadata", vpiNotice);
    return;
  }
  obelisk_rt_design_info_v1 info{};
  if (!interModPath) {
    if (!infoFor(handle, info))
      return;
    if (info.kind != OBELISK_RT_DESIGN_RECORD_NET) {
      setError(state, "VPI delay metadata is unavailable", vpiNotice);
      return;
    }
  }
  const PLI_INT32 minimumDelayCount = interModPath ? 2 : 1;
  if (destination->no_of_delays < minimumDelayCount ||
      destination->no_of_delays > 3) {
    setError(state,
             interModPath
                 ? "VPI intermodule-path delay count must be two or three"
                 : "VPI net delay count must be between one and three");
    return;
  }
  switch (destination->time_type) {
  case vpiScaledRealTime:
  case vpiSimTime:
  case vpiSuppressTime:
    break;
  default:
    setError(state, "unsupported VPI delay time format");
    return;
  }
  if (!destination->da) {
    setError(state, "VPI delay value array is null");
    return;
  }

  VPINetDelayValue delay{};
  if (!interModPath) {
    uint64_t bitOffset =
        handle->form == VPIObjectForm::Indexed ? handle->selectionBitOffset : 0;
    obelisk_rt_status status =
        obelisk_rt_cached_vpi_net_delay(handle->owner->context, handle->cursor,
                                        bitOffset, info.bit_width, &delay);
    if (status != OBELISK_RT_OK) {
      setError(state,
               status == OBELISK_RT_EOF ? "VPI delay metadata is unavailable"
                                        : "VPI net delay image lookup failed",
               status == OBELISK_RT_EOF ? vpiNotice : vpiInternal);
      return;
    }
  }

  int32_t precision = 0;
  int32_t unit = 0;
  long double timeScale = 1.0L;
  if (destination->time_type == vpiScaledRealTime) {
    if (!globalTimeExponent(state, precision))
      return;
    __vpiHandle pathPort;
    __vpiHandle *timeObject = handle;
    if (interModPath) {
      if (handle->interModPathEndpoints.size() < 2) {
        setError(state, "VPI intermodule-path endpoints are invalid",
                 vpiInternal);
        return;
      }
      pathPort.owner = state;
      pathPort.cursor = handle->interModPathEndpoints.back().cursor;
      pathPort.exactVpiType = handle->interModPathEndpoints.back().exactVpiType;
      timeObject = &pathPort;
    }
    DpiScopeHandle *scope = timeScopeFor(timeObject);
    if (!scope) {
      setError(state, "VPI object timescale metadata is unavailable",
               vpiNotice);
      return;
    }
    unit = scope->timeUnit;
    timeScale = std::pow(10.0L, static_cast<long double>(precision - unit));
  }
  const std::array<int64_t, 3> values{delay.rise, delay.fall, delay.third};
  const size_t mtmCount = destination->mtm_flag ? 3 : 1;
  const size_t pulseCount = destination->pulsere_flag ? 3 : 1;
  for (size_t delayIndex = 0;
       delayIndex != static_cast<size_t>(destination->no_of_delays);
       ++delayIndex) {
    for (size_t pulseIndex = 0; pulseIndex != pulseCount; ++pulseIndex) {
      for (size_t mtmIndex = 0; mtmIndex != mtmCount; ++mtmIndex) {
        const size_t resultIndex = delayIndex * pulseCount * mtmCount +
                                   pulseIndex * mtmCount + mtmIndex;
        s_vpi_time &result = destination->da[resultIndex];
        result = {};
        result.type = destination->time_type;
        int64_t ticks = values[delayIndex];
        switch (destination->time_type) {
        case vpiSimTime: {
          uint64_t encoded = static_cast<uint64_t>(ticks);
          result.high = static_cast<PLI_UINT32>(encoded >> 32);
          result.low = static_cast<PLI_UINT32>(encoded);
          break;
        }
        case vpiScaledRealTime:
          result.real = ticks == -1
                            ? -1.0
                            : static_cast<double>(
                                  static_cast<long double>(ticks) * timeScale);
          break;
        case vpiSuppressTime:
          break;
        }
      }
    }
  }
}

struct VPIArrayDimension {
  int64_t left = 0;
  int64_t right = 0;
  uint64_t extent = 0;
};

bool checkedArrayProduct(size_t left, size_t right, size_t &result) {
  if (right != 0 && left > std::numeric_limits<size_t>::max() / right)
    return false;
  result = left * right;
  return true;
}

uint64_t signExtendArrayElement(uint64_t value, uint64_t width) {
  if (width == 0 || width >= 64 || (value & (uint64_t{1} << (width - 1))) == 0)
    return value;
  return value | (~uint64_t{0} << width);
}

bool prepareArrayElement(__vpiHandle *source,
                         obelisk::reflection::VPIIndexedAccessKind accessKind,
                         uint32_t rootType,
                         const std::vector<VPISelectionStep> &baseSteps,
                         const std::vector<VPIArrayDimension> &dimensions,
                         uint64_t flattened, __vpiHandle &element,
                         obelisk_rt_design_info_v1 &info) {
  std::vector<int64_t> coordinates(dimensions.size());
  for (size_t dimension = dimensions.size(); dimension != 0;) {
    --dimension;
    const VPIArrayDimension &range = dimensions[dimension];
    const uint64_t ordinal = flattened % range.extent;
    flattened /= range.extent;
    coordinates[dimension] = range.left >= range.right
                                 ? range.left - static_cast<int64_t>(ordinal)
                                 : range.left + static_cast<int64_t>(ordinal);
  }
  if (flattened != 0)
    return false;
  std::vector<VPISelectionStep> selected = baseSteps;
  selected.reserve(baseSteps.size() + coordinates.size());
  for (int64_t index : coordinates)
    if (!appendIndexedSelection(source, accessKind, index, selected))
      return false;
  if (selected.empty() || !indexedInfoForSteps(source, selected, info))
    return false;

  element.owner = source->owner;
  element.kind = VPIHandleKind::Object;
  element.form = VPIObjectForm::Indexed;
  element.cursor = source->cursor;
  element.semanticCursor = selected.back().semanticType;
  element.exactVpiType = selected.back().exactVpiType;
  element.statement = source->statement;
  element.classDefinitionOrigin = source->classDefinitionOrigin;
  element.suppressSemanticDimension = selected.back().suppressSemanticDimension;
  element.selectionRootType = rootType;
  element.selectionAccessKind = accessKind;
  element.selectionBitOffset = selected.back().bitOffset;
  element.selectionSteps = std::move(selected);
  element.hasInfo = true;
  element.info = info;
  return true;
}

void *arrayValueCallerBuffer(p_vpi_arrayvalue value) {
  switch (value->format) {
  case vpiIntVal:
    return value->value.integers;
  case vpiShortIntVal:
    return value->value.shortints;
  case vpiLongIntVal:
    return value->value.longints;
  case vpiRawTwoStateVal:
  case vpiRawFourStateVal:
    return value->value.rawvals;
  case vpiVectorVal:
    return value->value.vectors;
  case vpiTimeVal:
    return value->value.times;
  case vpiRealVal:
    return value->value.reals;
  case vpiShortRealVal:
    return value->value.shortreals;
  default:
    return nullptr;
  }
}

void clearArrayValuePointer(p_vpi_arrayvalue value) {
  switch (value->format) {
  case vpiIntVal:
    value->value.integers = nullptr;
    return;
  case vpiShortIntVal:
    value->value.shortints = nullptr;
    return;
  case vpiLongIntVal:
    value->value.longints = nullptr;
    return;
  case vpiVectorVal:
    value->value.vectors = nullptr;
    return;
  case vpiTimeVal:
    value->value.times = nullptr;
    return;
  case vpiRealVal:
    value->value.reals = nullptr;
    return;
  case vpiShortRealVal:
    value->value.shortreals = nullptr;
    return;
  case vpiRawTwoStateVal:
  case vpiRawFourStateVal:
  default:
    value->value.rawvals = nullptr;
    return;
  }
}

void failArrayValueQuery(VPIState *state, p_vpi_arrayvalue destination,
                         const char *message, int level = vpiError) {
  if (destination)
    clearArrayValuePointer(destination);
  setError(state, message, level);
}

extern "C" OBELISK_VPI_EXPORT void
vpi_get_value_array(vpiHandle opaque, p_vpi_arrayvalue destination,
                    PLI_INT32 *indices, PLI_UINT32 num) {
  beginVPICall();
  VPIState *state = requireState();
  if (!state)
    return;
  if (!destination) {
    setError(state, "VPI array-value destination is null");
    return;
  }
  void *const callerBuffer = arrayValueCallerBuffer(destination);
  clearArrayValuePointer(destination);
  __vpiHandle *source = findHandle(opaque);
  if (!source)
    return;
  if (!allowProtectedSource(source, "vpi_get_value_array"))
    return;
  if (source->kind != VPIHandleKind::Object || source->classDefinitionOrigin ||
      (source->form != VPIObjectForm::Design &&
       source->form != VPIObjectForm::Indexed)) {
    failArrayValueQuery(state, destination,
                        "VPI array query requires static design storage");
    return;
  }
  const uint32_t sourceType = static_cast<uint32_t>(vpiTypeForHandle(source));
  if (sourceType != vpiRegArray && sourceType != vpiNetArray &&
      sourceType != vpiInterconnectArray) {
    failArrayValueQuery(state, destination,
                        "VPI array query requires an unpacked array object",
                        vpiNotice);
    return;
  }
  if (num == 0 || !indices) {
    failArrayValueQuery(state, destination,
                        "VPI array query requires a start index and values");
    return;
  }
  if ((destination->flags & ~vpiUserAllocFlag) != 0) {
    failArrayValueQuery(state, destination,
                        "VPI array query has invalid allocation flags");
    return;
  }
  const bool userAllocated = (destination->flags & vpiUserAllocFlag) != 0;

  const auto *access = obelisk::reflection::findVPIIndexedAccess(sourceType);
  const auto accessKind =
      source->form == VPIObjectForm::Indexed ? source->selectionAccessKind
      : access                               ? access->accessKind
               : obelisk::reflection::VPIIndexedAccessKind::RelationElement;
  if (!access ||
      accessKind ==
          obelisk::reflection::VPIIndexedAccessKind::RelationElement) {
    failArrayValueQuery(state, destination,
                        "VPI array query has no fixed storage geometry",
                        vpiNotice);
    return;
  }
  const uint32_t rootType = source->form == VPIObjectForm::Indexed
                                ? source->selectionRootType
                                : sourceType;
  std::vector<VPISelectionStep> baseSteps;
  std::vector<VPIArrayDimension> dimensions;
  OBELISK_RT_TRY {
    if (source->form == VPIObjectForm::Indexed)
      baseSteps = source->selectionSteps;
  }
  OBELISK_RT_CATCH_ALL {
    failArrayValueQuery(state, destination,
                        "could not allocate VPI array selection", vpiSystem);
    return;
  }

  obelisk_rt_design_info_v1 object{};
  if (!infoFor(source, object) || object.type_offset == 0)
    return;
  obelisk_rt_design_cursor_v1 physical{
      baseSteps.empty() ? object.type_offset
                        : baseSteps.back().physicalType.offset};
  obelisk_rt_design_type_info_v1 elementType{};
  uint64_t elementCount = 1;
  OBELISK_RT_TRY {
    for (;;) {
      if (obelisk_rt_cached_design_type_info(state->context, physical,
                                             &elementType) != OBELISK_RT_OK) {
        failArrayValueQuery(state, destination,
                            "VPI array type metadata is invalid", vpiInternal);
        return;
      }
      if (elementType.kind != OBELISK_RT_DESIGN_TYPE_ARRAY ||
          (elementType.flags & OBELISK_RT_DESIGN_TYPE_PACKED) != 0)
        break;
      uint64_t ignored = 0, extent = 0;
      if (!sourceIndexOrdinal(elementType.range_left, elementType.range_right,
                              elementType.range_left, ignored, extent) ||
          extent == 0 || elementCount > UINT64_MAX / extent) {
        failArrayValueQuery(state, destination,
                            "VPI array range metadata is invalid", vpiInternal);
        return;
      }
      dimensions.push_back(
          {elementType.range_left, elementType.range_right, extent});
      elementCount *= extent;
      physical = elementType.element_type;
    }
  }
  OBELISK_RT_CATCH_ALL {
    failArrayValueQuery(state, destination,
                        "could not allocate VPI array geometry", vpiSystem);
    return;
  }
  if (dimensions.empty() || elementType.bit_width == 0 ||
      ((elementType.kind == OBELISK_RT_DESIGN_TYPE_STRUCT ||
        elementType.kind == OBELISK_RT_DESIGN_TYPE_UNION) &&
       (elementType.flags & OBELISK_RT_DESIGN_TYPE_PACKED) == 0)) {
    failArrayValueQuery(state, destination,
                        "VPI array element is not a packed static value",
                        vpiNotice);
    return;
  }

  uint64_t first = 0;
  for (size_t dimension = 0; dimension != dimensions.size(); ++dimension) {
    uint64_t ordinal = 0, extent = 0;
    const VPIArrayDimension &range = dimensions[dimension];
    if (!sourceIndexOrdinal(range.left, range.right, indices[dimension],
                            ordinal, extent) ||
        first > (UINT64_MAX - ordinal) / extent) {
      failArrayValueQuery(state, destination,
                          "VPI array start index is out of range", vpiNotice);
      return;
    }
    first = first * extent + ordinal;
  }
  if (first >= elementCount ||
      static_cast<uint64_t>(num) > elementCount - first) {
    failArrayValueQuery(state, destination,
                        "VPI array section exceeds the declared range",
                        vpiNotice);
    return;
  }

  __vpiHandle firstElement;
  obelisk_rt_design_info_v1 firstInfo{};
  OBELISK_RT_TRY {
    if (!prepareArrayElement(source, accessKind, rootType, baseSteps,
                             dimensions, first, firstElement, firstInfo)) {
      failArrayValueQuery(state, destination,
                          "VPI array element metadata is unavailable",
                          vpiInternal);
      return;
    }
  }
  OBELISK_RT_CATCH_ALL {
    failArrayValueQuery(state, destination,
                        "could not allocate VPI array element", vpiSystem);
    return;
  }
  const uint32_t elementVPIType = firstElement.exactVpiType;
  if (!obelisk::reflection::acceptsVPIArrayValueFormat(destination->format,
                                                       elementVPIType)) {
    failArrayValueQuery(
        state, destination,
        "VPI array value format does not match its element type", vpiNotice);
    return;
  }
  if (userAllocated && !callerBuffer) {
    failArrayValueQuery(state, destination,
                        "VPI array query user buffer is null");
    return;
  }
  const bool signedElement = valueSigned(&firstElement, firstInfo);

  size_t groups = 1;
  if (destination->format == vpiVectorVal) {
    if (!checkedWordCount(firstInfo.bit_width, 32, groups)) {
      failArrayValueQuery(state, destination,
                          "VPI array vector width exceeds host capacity",
                          vpiSystem);
      return;
    }
  } else if (destination->format == vpiRawTwoStateVal ||
             destination->format == vpiRawFourStateVal) {
    if (!checkedWordCount(firstInfo.bit_width, 8, groups) ||
        (destination->format == vpiRawFourStateVal &&
         groups > std::numeric_limits<size_t>::max() / 2)) {
      failArrayValueQuery(state, destination,
                          "VPI array raw width exceeds host capacity",
                          vpiSystem);
      return;
    }
    if (destination->format == vpiRawFourStateVal)
      groups *= 2;
  }
  size_t outputCount = 0;
  if (!checkedArrayProduct(groups, static_cast<size_t>(num), outputCount)) {
    failArrayValueQuery(state, destination,
                        "VPI array result exceeds host capacity", vpiSystem);
    return;
  }

  OBELISK_RT_TRY {
    switch (destination->format) {
    case vpiIntVal:
      if (!userAllocated) {
        state->arrayIntegerScratch.resize(outputCount);
        destination->value.integers = state->arrayIntegerScratch.data();
      } else
        destination->value.integers = static_cast<PLI_INT32 *>(callerBuffer);
      break;
    case vpiShortIntVal:
      if (!userAllocated) {
        state->arrayShortIntScratch.resize(outputCount);
        destination->value.shortints = state->arrayShortIntScratch.data();
      } else
        destination->value.shortints = static_cast<PLI_INT16 *>(callerBuffer);
      break;
    case vpiLongIntVal:
      if (!userAllocated) {
        state->arrayLongIntScratch.resize(outputCount);
        destination->value.longints = state->arrayLongIntScratch.data();
      } else
        destination->value.longints = static_cast<PLI_INT64 *>(callerBuffer);
      break;
    case vpiRawTwoStateVal:
    case vpiRawFourStateVal:
      if (!userAllocated) {
        state->arrayRawScratch.resize(outputCount);
        destination->value.rawvals = state->arrayRawScratch.data();
      } else
        destination->value.rawvals = static_cast<PLI_BYTE8 *>(callerBuffer);
      break;
    case vpiVectorVal:
      if (!userAllocated) {
        state->arrayVectorScratch.resize(outputCount);
        destination->value.vectors = state->arrayVectorScratch.data();
      } else
        destination->value.vectors = static_cast<s_vpi_vecval *>(callerBuffer);
      break;
    case vpiTimeVal:
      if (!userAllocated) {
        state->arrayTimeScratch.resize(outputCount);
        destination->value.times = state->arrayTimeScratch.data();
      } else
        destination->value.times = static_cast<s_vpi_time *>(callerBuffer);
      break;
    case vpiRealVal:
      if (!userAllocated) {
        state->arrayRealScratch.resize(outputCount);
        destination->value.reals = state->arrayRealScratch.data();
      } else
        destination->value.reals = static_cast<double *>(callerBuffer);
      break;
    case vpiShortRealVal:
      if (!userAllocated) {
        state->arrayShortRealScratch.resize(outputCount);
        destination->value.shortreals = state->arrayShortRealScratch.data();
      } else
        destination->value.shortreals = static_cast<float *>(callerBuffer);
      break;
    default:
      failArrayValueQuery(state, destination,
                          "unsupported VPI array value format", vpiNotice);
      return;
    }
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    failArrayValueQuery(state, destination, "VPI array result is out of memory",
                        vpiSystem);
    return;
  }
  OBELISK_RT_CATCH_ALL {
    failArrayValueQuery(state, destination,
                        "could not allocate VPI array result", vpiInternal);
    return;
  }

  if (firstInfo.bit_width > UINT64_MAX / static_cast<uint64_t>(num)) {
    failArrayValueQuery(state, destination, "VPI array read width overflows",
                        vpiSystem);
    return;
  }
  const uint64_t readWidth = firstInfo.bit_width * static_cast<uint64_t>(num);
  if (firstElement.selectionBitOffset >
      UINT64_MAX - firstInfo.bit_width * static_cast<uint64_t>(num - 1)) {
    failArrayValueQuery(state, destination, "VPI array read offset overflows",
                        vpiSystem);
    return;
  }
  size_t readWords = 0;
  if (!checkedWordCount(readWidth, 64, readWords)) {
    failArrayValueQuery(state, destination,
                        "VPI array read exceeds host capacity", vpiSystem);
    return;
  }
  std::vector<uint64_t> &value = state->readValueScratch;
  std::vector<uint64_t> &unknown = state->readUnknownScratch;
  OBELISK_RT_TRY {
    value.assign(readWords, 0);
    unknown.assign(readWords, 0);
  }
  OBELISK_RT_CATCH_ALL {
    failArrayValueQuery(state, destination,
                        "VPI array read buffer is out of memory", vpiSystem);
    return;
  }
  if (obelisk_rt_read_design_slice(
          state->context, source->cursor, firstElement.selectionBitOffset,
          readWidth, value.data(), unknown.data()) != OBELISK_RT_OK) {
    failArrayValueQuery(state, destination, "VPI array design read failed");
    return;
  }

  const size_t bytesPerElement =
      destination->format == vpiRawFourStateVal ? groups / 2 : groups;
  for (size_t element = 0; element != static_cast<size_t>(num); ++element) {
    const uint64_t firstBit =
        static_cast<uint64_t>(element) * firstInfo.bit_width;
    const size_t output = element * groups;
    auto valueBit = [&](uint64_t bit) {
      return logicBit(value, firstBit + bit);
    };
    auto unknownBit = [&](uint64_t bit) {
      return logicBit(unknown, firstBit + bit);
    };
    uint64_t low = 0;
    const unsigned lowBits = static_cast<unsigned>(
        std::min<uint64_t>(firstInfo.bit_width, uint64_t{64}));
    for (unsigned bit = 0; bit != lowBits; ++bit)
      if (valueBit(bit) && !unknownBit(bit))
        low |= uint64_t{1} << bit;
    if (signedElement)
      low = signExtendArrayElement(low, firstInfo.bit_width);
    switch (destination->format) {
    case vpiIntVal:
      destination->value.integers[element] = static_cast<PLI_INT32>(low);
      break;
    case vpiShortIntVal:
      destination->value.shortints[element] = static_cast<PLI_INT16>(low);
      break;
    case vpiLongIntVal:
      destination->value.longints[element] = static_cast<PLI_INT64>(low);
      break;
    case vpiTimeVal:
      destination->value.times[element] = {vpiSimTime,
                                           static_cast<PLI_UINT32>(low >> 32),
                                           static_cast<PLI_UINT32>(low), 0.0};
      break;
    case vpiRealVal:
    case vpiShortRealVal: {
      double real = 0;
      if (!decodeRealBits(&firstElement, elementVPIType, firstInfo.bit_width,
                          low, real)) {
        destination->value.rawvals = nullptr;
        return;
      }
      if (destination->format == vpiRealVal)
        destination->value.reals[element] = real;
      else
        destination->value.shortreals[element] = static_cast<float>(real);
      break;
    }
    case vpiVectorVal:
      for (size_t word = 0; word != groups; ++word) {
        uint32_t a = 0, b = 0;
        for (unsigned bit = 0; bit != 32; ++bit) {
          const uint64_t absolute = uint64_t{word} * 32 + bit;
          if (absolute >= firstInfo.bit_width)
            break;
          const bool v = valueBit(absolute);
          const bool u = unknownBit(absolute);
          a |= static_cast<uint32_t>(v != u) << bit;
          b |= static_cast<uint32_t>(u) << bit;
        }
        destination->value.vectors[output + word] = {a, b};
      }
      break;
    case vpiRawTwoStateVal:
    case vpiRawFourStateVal:
      for (size_t byte = 0; byte != bytesPerElement; ++byte) {
        uint8_t a = 0, b = 0;
        for (unsigned bit = 0; bit != 8; ++bit) {
          const uint64_t absolute = uint64_t{byte} * 8 + bit;
          if (absolute >= firstInfo.bit_width)
            break;
          const bool v = valueBit(absolute);
          const bool u = unknownBit(absolute);
          a |= static_cast<uint8_t>(v != u) << bit;
          b |= static_cast<uint8_t>(u) << bit;
        }
        destination->value.rawvals[output + byte] = a;
        if (destination->format == vpiRawFourStateVal)
          destination->value.rawvals[output + bytesPerElement + byte] = b;
      }
      break;
    default:
      break;
    }
  }
}

extern "C" OBELISK_VPI_EXPORT vpiHandle vpi_handle_multi(PLI_INT32 type,
                                                         vpiHandle first,
                                                         vpiHandle second,
                                                         ...) {
  beginVPICall();
  VPIState *state = requireState();
  if (!state)
    return nullptr;
  __vpiHandle *firstHandle = findHandle(first);
  __vpiHandle *secondHandle = findHandle(second);
  if (type != vpiInterModPath || !firstHandle || !secondHandle)
    return nullptr;
  if (!allowProtectedSource(firstHandle, "vpi_handle_multi") ||
      !allowProtectedSource(secondHandle, "vpi_handle_multi"))
    return nullptr;
  if (firstHandle->kind != VPIHandleKind::Object ||
      secondHandle->kind != VPIHandleKind::Object ||
      (firstHandle->form != VPIObjectForm::Design &&
       firstHandle->form != VPIObjectForm::Indexed) ||
      (secondHandle->form != VPIObjectForm::Design &&
       secondHandle->form != VPIObjectForm::Indexed) ||
      (vpiTypeForHandle(firstHandle) != vpiPort &&
       vpiTypeForHandle(firstHandle) != vpiPortBit) ||
      (vpiTypeForHandle(secondHandle) != vpiPort &&
       vpiTypeForHandle(secondHandle) != vpiPortBit)) {
    setError(state, "VPI intermodule path requires port or port-bit handles");
    return nullptr;
  }
  OBELISK_RT_TRY {
    std::vector<VPIInterModPathEndpoint> ports{
        interModPathEndpointFor(*firstHandle),
        interModPathEndpointFor(*secondHandle)};
    if (sameInterModPathEndpoint(ports[0], ports[1])) {
      setError(state, "VPI intermodule path requires distinct endpoints");
      return nullptr;
    }
    obelisk_rt_design_info_v1 firstInfo{};
    obelisk_rt_design_info_v1 secondInfo{};
    if (!infoFor(firstHandle, firstInfo) || !infoFor(secondHandle, secondInfo))
      return nullptr;
    if (firstInfo.kind != OBELISK_RT_DESIGN_RECORD_PORT ||
        secondInfo.kind != OBELISK_RT_DESIGN_RECORD_PORT ||
        firstInfo.bit_width == 0 ||
        firstInfo.bit_width != secondInfo.bit_width ||
        (firstInfo.capabilities & OBELISK_RT_DESIGN_CAP_PORT_OUTPUT) == 0 ||
        (secondInfo.capabilities & OBELISK_RT_DESIGN_CAP_PORT_INPUT) == 0) {
      setError(
          state,
          "VPI intermodule path requires same-width output and input ports");
      return nullptr;
    }
    return makeInterModPathHandle(state, ports);
  }
  OBELISK_RT_CATCH_ALL {
    setError(state, "could not allocate VPI intermodule-path endpoints",
             vpiSystem);
    return nullptr;
  }
}

extern "C" OBELISK_VPI_EXPORT PLI_BYTE8 *vpi_mcd_name(PLI_UINT32 descriptor) {
  beginVPICall();
  VPIState *state = requireState();
  if (!state)
    return nullptr;
  OBELISK_RT_TRY {
    ContextMutexLock lock(state->context);
    std::string_view name;
    if (!obelisk_rt_file_name_unlocked(state->context, descriptor, name)) {
      setError(state, "invalid or non-single-channel VPI file descriptor");
      return nullptr;
    }
    state->fileNameScratch.assign(name);
    return state->fileNameScratch.data();
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    setError(state, "VPI file name result is out of memory", vpiSystem);
    return nullptr;
  }
  OBELISK_RT_CATCH_ALL {
    setError(state, "could not retrieve VPI file name", vpiInternal);
    return nullptr;
  }
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
