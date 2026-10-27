//===- DPIExport.cpp - Cold zero-time DPI export dispatch ----------------===//

#include "RuntimeInternal.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#if defined(__clang__) || defined(__GNUC__)
__attribute__((weak))
#endif
talos_rt_status
talos_rt_execute_dpi_export_bytecode(
    const talos_rt_execution_descriptor_v1 &execution,
    const talos_rt_export_descriptor_v1 &descriptor,
    talos_rt_context *context, const talos_rt_import_input_v1 *inputs,
    uint32_t inputCount, talos_rt_import_output_v1 *outputs,
    uint32_t outputCount) noexcept;

#if defined(__APPLE__)
// Mach-O extracts archive members even for weak references. Define the
// absent-tier result here; DPIExportBytecode.cpp's strong definition replaces
// it when a design references that feature's link anchor.
talos_rt_status talos_rt_execute_dpi_export_bytecode(
    const talos_rt_execution_descriptor_v1 &,
    const talos_rt_export_descriptor_v1 &, talos_rt_context *,
    const talos_rt_import_input_v1 *, uint32_t, talos_rt_import_output_v1 *,
    uint32_t) noexcept {
  return TALOS_RT_TIER_UNAVAILABLE;
}
#endif

namespace {

thread_local std::vector<std::vector<std::string>> exportStringFrames;
thread_local size_t exportDepth = 0;

uint64_t limbCount(uint32_t width) { return (uint64_t{width} + 63) / 64; }

bool validKind(talos_rt_design_register_kind kind) {
  return kind == TALOS_RT_DBREG_BITS || kind == TALOS_RT_DBREG_LOGIC ||
         kind == TALOS_RT_DBREG_STATUS || kind == TALOS_RT_DBREG_STRING ||
         kind == TALOS_RT_DBREG_REAL32 || kind == TALOS_RT_DBREG_REAL64 ||
         kind == TALOS_RT_DBREG_AGGREGATE;
}

bool validReal(talos_rt_design_register_kind kind, uint8_t flags,
               uint32_t width, const uint64_t *unknown) {
  if (kind == TALOS_RT_DBREG_REAL32)
    return flags == 0 && width == 32 && unknown == nullptr;
  if (kind == TALOS_RT_DBREG_REAL64)
    return flags == 0 && width == 64 && unknown == nullptr;
  return false;
}

bool validInput(const talos_rt_import_input_v1 &input) {
  if (!validKind(input.kind) ||
      (input.flags & ~uint8_t{TALOS_RT_DBREG_SIGNED}) != 0 ||
      input.reserved != 0 || !input.value)
    return false;
  uint32_t width = input.kind == TALOS_RT_DBREG_STATUS ? 32 : input.bit_width;
  if (width == 0 || input.bit_width != width ||
      input.limb_count != limbCount(width))
    return false;
  if (input.kind == TALOS_RT_DBREG_STRING)
    return input.flags == 0 && input.bit_width == 64 &&
           input.unknown == nullptr && input.limb_count == 1;
  if (input.kind == TALOS_RT_DBREG_REAL32 ||
      input.kind == TALOS_RT_DBREG_REAL64)
    return validReal(input.kind, input.flags, input.bit_width, input.unknown);
  if (input.kind == TALOS_RT_DBREG_AGGREGATE)
    return true;
  return input.kind == TALOS_RT_DBREG_LOGIC ? input.unknown != nullptr
                                              : input.unknown == nullptr;
}

bool validOutput(const talos_rt_import_output_v1 &output) {
  if (!validKind(output.kind) ||
      (output.flags & ~uint8_t{TALOS_RT_DBREG_SIGNED}) != 0 ||
      output.reserved != 0 || !output.value)
    return false;
  uint32_t width =
      output.kind == TALOS_RT_DBREG_STATUS ? 32 : output.bit_width;
  if (width == 0 || output.bit_width != width ||
      output.limb_count != limbCount(width))
    return false;
  if (output.kind == TALOS_RT_DBREG_STRING)
    return output.flags == 0 && output.bit_width == 64 &&
           output.unknown == nullptr;
  if (output.kind == TALOS_RT_DBREG_REAL32 ||
      output.kind == TALOS_RT_DBREG_REAL64)
    return validReal(output.kind, output.flags, output.bit_width,
                     output.unknown);
  if (output.kind == TALOS_RT_DBREG_AGGREGATE)
    return true;
  return output.kind == TALOS_RT_DBREG_LOGIC ? output.unknown != nullptr
                                               : output.unknown == nullptr;
}

void zeroOutput(talos_rt_import_output_v1 &output) {
  if (output.kind == TALOS_RT_DBREG_REAL32 ||
      output.kind == TALOS_RT_DBREG_REAL64)
    std::memset(output.value, 0, output.bit_width / 8);
  else
    std::fill_n(output.value, output.limb_count, uint64_t{0});
  if (output.unknown)
    std::fill_n(output.unknown, output.limb_count, uint64_t{0});
}

void normalize(talos_rt_import_output_v1 &output) {
  if (output.kind == TALOS_RT_DBREG_STRING ||
      output.kind == TALOS_RT_DBREG_REAL32 ||
      output.kind == TALOS_RT_DBREG_REAL64)
    return;
  unsigned tail = output.bit_width % 64;
  if (tail == 0)
    return;
  uint64_t mask = (uint64_t{1} << tail) - 1;
  output.value[output.limb_count - 1] &= mask;
  if (output.unknown)
    output.unknown[output.limb_count - 1] &= mask;
}

const talos_rt_execution_extension_v1 *
exportExtension(const talos_rt_execution_descriptor_v1 &execution) {
  if ((execution.flags & TALOS_RT_EXECUTION_DPI_EXPORTS) == 0 ||
      execution.reserved < sizeof(execution))
    return nullptr;
  uintptr_t base = reinterpret_cast<uintptr_t>(&execution);
  if (execution.reserved > std::numeric_limits<uintptr_t>::max() - base)
    return nullptr;
  uintptr_t address = base + static_cast<uintptr_t>(execution.reserved);
  if (address % alignof(talos_rt_execution_extension_v1) != 0)
    return nullptr;
  auto *extension =
      reinterpret_cast<const talos_rt_execution_extension_v1 *>(address);
  return extension->version == TALOS_RT_EXECUTION_EXTENSION_VERSION &&
                 extension->size == sizeof(*extension)
             ? extension
             : nullptr;
}

const talos_rt_export_descriptor_v1 *
findExport(const talos_rt_execution_extension_v1 &extension,
           uint32_t exportID, uint64_t scopeID) {
  uint64_t low = 0, high = extension.export_count;
  while (low < high) {
    uint64_t middle = low + (high - low) / 2;
    const talos_rt_export_descriptor_v1 &candidate =
        extension.exports[middle];
    if (candidate.export_id < exportID ||
        (candidate.export_id == exportID && candidate.scope_id < scopeID))
      low = middle + 1;
    else
      high = middle;
  }
  if (low == extension.export_count)
    return nullptr;
  const talos_rt_export_descriptor_v1 &candidate = extension.exports[low];
  return candidate.export_id == exportID && candidate.scope_id == scopeID
             ? &candidate
             : nullptr;
}

void latchFailure(ActiveDpiCall &call, talos_rt_status status) {
  if (status != TALOS_RT_OK && call.exportStatus == TALOS_RT_OK)
    call.exportStatus = status;
}

class ScopedManagedRoots {
public:
  ScopedManagedRoots(talos_rt_context *context,
                     std::vector<talos_rt_managed_word_v1> &words)
      : lane(words.empty() ? nullptr : talos_rt_v1_gc_current_lane(context)) {
    if (words.empty())
      return;
    if (!lane)
      status = TALOS_RT_INVALID_LIFECYCLE;
    else
      status = talos_rt_v1_gc_managed_root_range_push(
          lane, &range, words.data(), words.size());
  }
  ~ScopedManagedRoots() {
    if (status == TALOS_RT_OK && range.cookie != 0)
      (void)talos_rt_v1_gc_managed_root_range_pop(lane, &range);
  }
  talos_rt_status getStatus() const { return status; }
  talos_rt_gc_lane_v1 *getLane() const { return lane; }

private:
  talos_rt_gc_lane_v1 *lane = nullptr;
  talos_rt_gc_managed_root_range_v1 range{};
  talos_rt_status status = TALOS_RT_OK;
};

class ScopedExportDepth {
public:
  ScopedExportDepth() { ++exportDepth; }
  ScopedExportDepth(const ScopedExportDepth &) = delete;
  ScopedExportDepth &operator=(const ScopedExportDepth &) = delete;
  ~ScopedExportDepth() { --exportDepth; }
};

} // namespace

extern "C" TALOS_RT_FEATURE_TEXT void
talos_rt_v1_dpi_export_unpack_vector(const void *source, void *value,
                                       void *unknown, uint32_t width,
                                       uint32_t fourState) {
  if (!source || !value || width == 0)
    return;
  const auto *sourceBytes = static_cast<const uint8_t *>(source);
  auto *valueBytes = static_cast<uint8_t *>(value);
  auto *unknownBytes = static_cast<uint8_t *>(unknown);
  uint64_t words = (uint64_t{width} + 31) / 32;
  uint32_t stride = fourState ? 8 : 4;
  for (uint64_t word = 0; word != words; ++word) {
    uint32_t aval = 0, bval = 0;
    std::memcpy(&aval, sourceBytes + word * stride, sizeof(aval));
    if (fourState)
      std::memcpy(&bval, sourceBytes + word * stride + 4, sizeof(bval));
    if (word + 1 == words && width % 32 != 0) {
      uint32_t mask = (uint32_t{1} << (width % 32)) - 1;
      aval &= mask;
      bval &= mask;
    }
    // The C representation is independent of the simulator storage domain.
    // A null unknown plane requests projection into two-state data.
    uint32_t internal = unknown ? aval ^ bval : aval & ~bval;
    std::memcpy(valueBytes + word * 4, &internal, sizeof(internal));
    if (fourState && unknown)
      std::memcpy(unknownBytes + word * 4, &bval, sizeof(bval));
  }
}

extern "C" TALOS_RT_FEATURE_TEXT void
talos_rt_v1_dpi_export_pack_vector(void *destination, const void *value,
                                     const void *unknown, uint32_t width,
                                     uint32_t fourState) {
  if (!destination || !value || width == 0)
    return;
  auto *destinationBytes = static_cast<uint8_t *>(destination);
  const auto *valueBytes = static_cast<const uint8_t *>(value);
  const auto *unknownBytes = static_cast<const uint8_t *>(unknown);
  uint64_t words = (uint64_t{width} + 31) / 32;
  uint32_t stride = fourState ? 8 : 4;
  for (uint64_t word = 0; word != words; ++word) {
    uint32_t internal = 0, bval = 0;
    std::memcpy(&internal, valueBytes + word * 4, sizeof(internal));
    if (fourState && unknown)
      std::memcpy(&bval, unknownBytes + word * 4, sizeof(bval));
    uint32_t aval = internal ^ bval;
    if (word + 1 == words && width % 32 != 0) {
      uint32_t mask = (uint32_t{1} << (width % 32)) - 1;
      aval &= mask;
      bval &= mask;
    }
    std::memcpy(destinationBytes + word * stride, &aval, sizeof(aval));
    if (fourState)
      std::memcpy(destinationBytes + word * stride + 4, &bval, sizeof(bval));
  }
}

TALOS_RT_FEATURE_TEXT bool talos_rt_validate_dpi_exports(
    const talos_rt_execution_descriptor_v1 &execution,
    const talos_rt_execution_extension_v1 &extension) noexcept {
  constexpr uint32_t validFlags = TALOS_RT_EXPORT_HAS_NATIVE |
                                  TALOS_RT_EXPORT_HAS_BYTECODE |
                                  TALOS_RT_EXPORT_TASK;
  uint32_t previousExportID = 0;
  uint64_t previousScopeID = 0;
  for (uint64_t index = 0; index != extension.export_count; ++index) {
    const talos_rt_export_descriptor_v1 &descriptor =
        extension.exports[index];
    if (descriptor.export_id == 0 || descriptor.code_unit_id == 0 ||
        descriptor.abi_signature == 0 || descriptor.flags == 0 ||
        (descriptor.flags & ~validFlags) != 0 || descriptor.reserved != 0 ||
        descriptor.reserved_tail != 0 ||
        descriptor.scope_id >= execution.dpi_scope_count ||
        (index != 0 && (descriptor.export_id < previousExportID ||
                        (descriptor.export_id == previousExportID &&
                         descriptor.scope_id <= previousScopeID))))
      return false;
    previousExportID = descriptor.export_id;
    previousScopeID = descriptor.scope_id;
    bool hasNative = (descriptor.flags & TALOS_RT_EXPORT_HAS_NATIVE) != 0;
    bool task = (descriptor.flags & TALOS_RT_EXPORT_TASK) != 0;
    if ((!task && hasNative != (descriptor.native_entry != nullptr)) ||
        (task && (!descriptor.native_entry || hasNative)))
      return false;
    bool hasBytecode = (descriptor.flags & TALOS_RT_EXPORT_HAS_BYTECODE) != 0;
    if (hasBytecode) {
      if ((execution.flags & TALOS_RT_EXECUTION_HAS_BYTECODE) == 0 ||
          descriptor.bytecode_function == TALOS_RT_EXPORT_NO_BYTECODE)
        return false;
    } else if (descriptor.bytecode_function != TALOS_RT_EXPORT_NO_BYTECODE) {
      return false;
    }
    if ((execution.flags & TALOS_RT_EXECUTION_REQUIRE_BYTECODE) != 0 &&
        (hasNative || !hasBytecode))
      return false;
  }
  return true;
}

extern "C" TALOS_RT_FEATURE_TEXT talos_rt_status talos_rt_v1_export_call(
    uint32_t exportID, uint64_t abiSignature,
    const talos_rt_import_input_v1 *inputs, uint32_t inputCount,
    talos_rt_import_output_v1 *outputs, uint32_t outputCount) {
  ActiveDpiCall *call = activeDpiCall;
  auto fail = [&](talos_rt_status status) {
    if (call)
      latchFailure(*call, status);
    return status;
  };
  if (outputCount != 0 && !outputs)
    return fail(TALOS_RT_INVALID_ARGUMENT);
  for (uint32_t index = 0; index != outputCount; ++index)
    if (!validOutput(outputs[index]))
      return fail(TALOS_RT_INVALID_ARGUMENT);
  for (uint32_t index = 0; index != outputCount; ++index)
    zeroOutput(outputs[index]);
  if (inputCount != 0 && !inputs)
    return fail(TALOS_RT_INVALID_ARGUMENT);
  if (!call || !call->context || !call->scope)
    return fail(TALOS_RT_INVALID_LIFECYCLE);
  if (call->disabledState)
    return fail(TALOS_RT_FATAL);
  if (call->exportStatus != TALOS_RT_OK)
    return call->exportStatus;
  if (exportID == 0 || abiSignature == 0)
    return fail(TALOS_RT_INVALID_ARGUMENT);
  for (uint32_t index = 0; index != inputCount; ++index)
    if (!validInput(inputs[index]))
      return fail(TALOS_RT_INVALID_ARGUMENT);
  talos_rt_context *context = call->context;
  const talos_rt_execution_descriptor_v1 *execution = context->execution;
  const talos_rt_execution_extension_v1 *extension =
      execution ? exportExtension(*execution) : nullptr;
  const talos_rt_export_descriptor_v1 *descriptor =
      extension ? findExport(*extension, exportID, call->scope->id) : nullptr;
  if (!descriptor)
    return fail(TALOS_RT_INVALID_HANDLE);
  if (descriptor->abi_signature != abiSignature ||
      descriptor->input_count != inputCount ||
      descriptor->output_count != outputCount)
    return fail(TALOS_RT_ARGUMENT_MISMATCH);

  ContextTransaction transaction(context);
  talos_rt_status result =
      talos_rt_feature_guarded(context, [&]() {
        if (exportDepth == std::numeric_limits<size_t>::max())
          return fail(TALOS_RT_OUT_OF_RESOURCES);
        size_t frameIndex = exportDepth;
        if (exportStringFrames.size() <= frameIndex)
          exportStringFrames.resize(frameIndex + 1);
        // A caller may pass a pointer returned by export_string straight back
        // into the next same-depth export. Preserve the prior frame until all
        // raw C string inputs have been copied into managed storage.
        std::vector<std::string> priorStrings;
        priorStrings.swap(exportStringFrames[frameIndex]);

        uint64_t stringCount = 0;
        for (uint32_t index = 0; index != inputCount; ++index)
          stringCount += inputs[index].kind == TALOS_RT_DBREG_STRING;
        for (uint32_t index = 0; index != outputCount; ++index)
          stringCount += outputs[index].kind == TALOS_RT_DBREG_STRING;
        if (stringCount > std::numeric_limits<size_t>::max())
          return fail(TALOS_RT_OUT_OF_RESOURCES);
        std::vector<talos_rt_managed_word_v1> roots(
            static_cast<size_t>(stringCount), 0);
        ScopedManagedRoots rooted(context, roots);
        if (rooted.getStatus() != TALOS_RT_OK)
          return fail(rooted.getStatus());

        std::vector<talos_rt_import_input_v1> convertedInputs;
        std::vector<talos_rt_import_output_v1> convertedOutputs;
        if (inputCount != 0)
          convertedInputs.assign(inputs, inputs + inputCount);
        if (outputCount != 0)
          convertedOutputs.assign(outputs, outputs + outputCount);
        size_t root = 0;
        talos_rt_gc_lane_v1 *lane = rooted.getLane();
        for (uint32_t index = 0; index != inputCount; ++index) {
          if (inputs[index].kind != TALOS_RT_DBREG_STRING)
            continue;
          convertedInputs[index].value = &roots[root];
          uint64_t pointerWord = *inputs[index].value;
          // Generated wrappers use a null raw pointer only for output formals,
          // whose incoming value is intentionally absent. Input and inout
          // strings always carry their initialized C address.
          if (pointerWord != 0) {
            if (pointerWord > std::numeric_limits<uintptr_t>::max())
              return fail(TALOS_RT_INVALID_ARGUMENT);
            const char *text = reinterpret_cast<const char *>(
                static_cast<uintptr_t>(pointerWord));
            talos_rt_status status = talos_rt_v1_string_create(
                lane, text, std::strlen(text), &roots[root]);
            if (status != TALOS_RT_OK)
              return fail(status);
          }
          ++root;
        }
        for (uint32_t index = 0; index != outputCount; ++index) {
          if (outputs[index].kind != TALOS_RT_DBREG_STRING)
            continue;
          convertedOutputs[index].value = &roots[root++];
        }

        ScopedExportDepth depth;
        talos_rt_status status;
        bool requireBytecode =
            (execution->flags & TALOS_RT_EXECUTION_REQUIRE_BYTECODE) != 0;
        bool task = (descriptor->flags & TALOS_RT_EXPORT_TASK) != 0;
        if (task && (call->importFlags &
                     (TALOS_RT_IMPORT_CONTEXT | TALOS_RT_IMPORT_TASK)) !=
                        (TALOS_RT_IMPORT_CONTEXT | TALOS_RT_IMPORT_TASK))
          return fail(TALOS_RT_FATAL);
        uint64_t vpiExport = 0;
        status = talos_rt_vpi_export_begin(context, descriptor, &vpiExport);
        if (status != TALOS_RT_OK)
          return fail(status);
        struct ExportFrameGuard {
          talos_rt_context *context;
          uint64_t token;
          bool active = true;
          ~ExportFrameGuard() {
            if (active)
              (void)talos_rt_vpi_export_end(context, token);
          }
        } exportFrame{context, vpiExport};
        if (task || (!requireBytecode && descriptor->native_entry)) {
          status = descriptor->native_entry(context, convertedInputs.data(),
                                            inputCount, convertedOutputs.data(),
                                            outputCount);
        } else if (talos_rt_execute_dpi_export_bytecode) {
          status = talos_rt_execute_dpi_export_bytecode(
              *execution, *descriptor, context, convertedInputs.data(),
              inputCount, convertedOutputs.data(), outputCount);
        } else {
          status = TALOS_RT_TIER_UNAVAILABLE;
        }
        auto frameStatus = talos_rt_vpi_export_end(context, vpiExport);
        exportFrame.active = false;
        if (status == TALOS_RT_OK)
          status = frameStatus;
        if (status == TALOS_RT_DPI_DISABLE_UNSUPPORTED) {
          call->disabledState = true;
          return status;
        }
        if (status != TALOS_RT_OK)
          return fail(status);

        // Nested dispatch may grow and relocate exportStringFrames. Reacquire
        // this depth's frame after it returns instead of retaining a dangling
        // reference across the callback.
        std::vector<std::string> &exportStrings =
            exportStringFrames[frameIndex];
        exportStrings.resize(outputCount);
        char scratch[8];
        for (uint32_t index = 0; index != outputCount; ++index) {
          normalize(convertedOutputs[index]);
          if (outputs[index].kind != TALOS_RT_DBREG_STRING)
            continue;
          const char *bytes = nullptr;
          uint64_t size = 0;
          status = talos_rt_v1_string_view(*convertedOutputs[index].value,
                                             scratch, &bytes, &size);
          if (status != TALOS_RT_OK)
            return fail(status);
          if (size > std::numeric_limits<size_t>::max())
            return fail(TALOS_RT_OUT_OF_RESOURCES);
          if (size == 0)
            exportStrings[index].clear();
          else
            exportStrings[index].assign(bytes, static_cast<size_t>(size));
        }
        return TALOS_RT_OK;
      });
  if (result != TALOS_RT_OK && result != TALOS_RT_DPI_DISABLE_UNSUPPORTED)
    latchFailure(*call, result);
  return result;
}

extern "C" talos_rt_status
talos_rt_v1_dpi_export_status(talos_rt_status status) {
  if (status != TALOS_RT_OK && activeDpiCall)
    latchFailure(*activeDpiCall, status);
  return status;
}

extern "C" talos_rt_status talos_rt_v1_export_call_guarded(
    talos_rt_status priorStatus, uint32_t exportID, uint64_t abiSignature,
    const talos_rt_import_input_v1 *inputs, uint32_t inputCount,
    talos_rt_import_output_v1 *outputs, uint32_t outputCount) {
  if (priorStatus != TALOS_RT_OK)
    return talos_rt_v1_dpi_export_status(priorStatus);
  return talos_rt_v1_export_call(exportID, abiSignature, inputs, inputCount,
                                   outputs, outputCount);
}

extern "C" TALOS_RT_FEATURE_TEXT const char *
talos_rt_v1_export_string(uint32_t outputIndex) {
  return exportDepth < exportStringFrames.size() &&
                 outputIndex < exportStringFrames[exportDepth].size()
             ? exportStringFrames[exportDepth][outputIndex].c_str()
             : nullptr;
}
