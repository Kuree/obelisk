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
obelisk_rt_status
obelisk_rt_execute_dpi_export_bytecode(
    const obelisk_rt_execution_descriptor_v1 &execution,
    const obelisk_rt_export_descriptor_v1 &descriptor,
    obelisk_rt_context *context, const obelisk_rt_import_input_v1 *inputs,
    uint32_t inputCount, obelisk_rt_import_output_v1 *outputs,
    uint32_t outputCount) noexcept;

namespace {

thread_local std::vector<std::vector<std::string>> exportStringFrames;
thread_local size_t exportDepth = 0;

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
           input.unknown == nullptr && input.limb_count == 1;
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

void zeroOutput(obelisk_rt_import_output_v1 &output) {
  if (output.kind == OBELISK_RT_DBREG_REAL32 ||
      output.kind == OBELISK_RT_DBREG_REAL64)
    std::memset(output.value, 0, output.bit_width / 8);
  else
    std::fill_n(output.value, output.limb_count, uint64_t{0});
  if (output.unknown)
    std::fill_n(output.unknown, output.limb_count, uint64_t{0});
}

void normalize(obelisk_rt_import_output_v1 &output) {
  if (output.kind == OBELISK_RT_DBREG_STRING ||
      output.kind == OBELISK_RT_DBREG_REAL32 ||
      output.kind == OBELISK_RT_DBREG_REAL64)
    return;
  unsigned tail = output.bit_width % 64;
  if (tail == 0)
    return;
  uint64_t mask = (uint64_t{1} << tail) - 1;
  output.value[output.limb_count - 1] &= mask;
  if (output.unknown)
    output.unknown[output.limb_count - 1] &= mask;
}

const obelisk_rt_execution_extension_v2 *
exportExtension(const obelisk_rt_execution_descriptor_v1 &execution) {
  if ((execution.flags & OBELISK_RT_EXECUTION_DPI_EXPORTS) == 0 ||
      execution.reserved < sizeof(execution))
    return nullptr;
  uintptr_t base = reinterpret_cast<uintptr_t>(&execution);
  if (execution.reserved > std::numeric_limits<uintptr_t>::max() - base)
    return nullptr;
  uintptr_t address = base + static_cast<uintptr_t>(execution.reserved);
  if (address % alignof(obelisk_rt_execution_extension_v2) != 0)
    return nullptr;
  auto *extension =
      reinterpret_cast<const obelisk_rt_execution_extension_v2 *>(address);
  return extension->version == OBELISK_RT_EXECUTION_EXTENSION_V2_VERSION &&
                 extension->size == sizeof(*extension)
             ? extension
             : nullptr;
}

const obelisk_rt_export_descriptor_v1 *
findExport(const obelisk_rt_execution_extension_v2 &extension,
           uint32_t exportID, uint64_t scopeID) {
  uint64_t low = 0, high = extension.export_count;
  while (low < high) {
    uint64_t middle = low + (high - low) / 2;
    const obelisk_rt_export_descriptor_v1 &candidate =
        extension.exports[middle];
    if (candidate.export_id < exportID ||
        (candidate.export_id == exportID && candidate.scope_id < scopeID))
      low = middle + 1;
    else
      high = middle;
  }
  if (low == extension.export_count)
    return nullptr;
  const obelisk_rt_export_descriptor_v1 &candidate = extension.exports[low];
  return candidate.export_id == exportID && candidate.scope_id == scopeID
             ? &candidate
             : nullptr;
}

void latchFailure(ActiveDpiCall &call, obelisk_rt_status status) {
  if (status != OBELISK_RT_OK && call.exportStatus == OBELISK_RT_OK)
    call.exportStatus = status;
}

class ScopedManagedRoots {
public:
  ScopedManagedRoots(obelisk_rt_context *context,
                     std::vector<obelisk_rt_managed_word_v1> &words)
      : lane(words.empty() ? nullptr : obelisk_rt_v1_gc_current_lane(context)) {
    if (words.empty())
      return;
    if (!lane)
      status = OBELISK_RT_INVALID_LIFECYCLE;
    else
      status = obelisk_rt_v1_gc_managed_root_range_push(
          lane, &range, words.data(), words.size());
  }
  ~ScopedManagedRoots() {
    if (status == OBELISK_RT_OK && range.cookie != 0)
      (void)obelisk_rt_v1_gc_managed_root_range_pop(lane, &range);
  }
  obelisk_rt_status getStatus() const { return status; }
  obelisk_rt_gc_lane_v1 *getLane() const { return lane; }

private:
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  obelisk_rt_gc_managed_root_range_v1 range{};
  obelisk_rt_status status = OBELISK_RT_OK;
};

class ScopedExportDepth {
public:
  ScopedExportDepth() { ++exportDepth; }
  ScopedExportDepth(const ScopedExportDepth &) = delete;
  ScopedExportDepth &operator=(const ScopedExportDepth &) = delete;
  ~ScopedExportDepth() { --exportDepth; }
};

} // namespace

extern "C" OBELISK_RT_FEATURE_TEXT void
obelisk_rt_v1_dpi_export_unpack_vector(const void *source, void *value,
                                       void *unknown, uint32_t width,
                                       uint32_t fourState) {
  if (!source || !value || width == 0 || (fourState != 0 && !unknown))
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
    uint32_t internal = aval ^ bval;
    std::memcpy(valueBytes + word * 4, &internal, sizeof(internal));
    if (fourState)
      std::memcpy(unknownBytes + word * 4, &bval, sizeof(bval));
  }
}

extern "C" OBELISK_RT_FEATURE_TEXT void
obelisk_rt_v1_dpi_export_pack_vector(void *destination, const void *value,
                                     const void *unknown, uint32_t width,
                                     uint32_t fourState) {
  if (!destination || !value || width == 0 || (fourState != 0 && !unknown))
    return;
  auto *destinationBytes = static_cast<uint8_t *>(destination);
  const auto *valueBytes = static_cast<const uint8_t *>(value);
  const auto *unknownBytes = static_cast<const uint8_t *>(unknown);
  uint64_t words = (uint64_t{width} + 31) / 32;
  uint32_t stride = fourState ? 8 : 4;
  for (uint64_t word = 0; word != words; ++word) {
    uint32_t internal = 0, bval = 0;
    std::memcpy(&internal, valueBytes + word * 4, sizeof(internal));
    if (fourState)
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

OBELISK_RT_FEATURE_TEXT bool obelisk_rt_validate_dpi_exports(
    const obelisk_rt_execution_descriptor_v1 &execution,
    const obelisk_rt_execution_extension_v2 &extension) noexcept {
  constexpr uint32_t validFlags =
      OBELISK_RT_EXPORT_HAS_NATIVE | OBELISK_RT_EXPORT_HAS_BYTECODE;
  uint32_t previousExportID = 0;
  uint64_t previousScopeID = 0;
  for (uint64_t index = 0; index != extension.export_count; ++index) {
    const obelisk_rt_export_descriptor_v1 &descriptor =
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
    bool hasNative = (descriptor.flags & OBELISK_RT_EXPORT_HAS_NATIVE) != 0;
    if (hasNative != (descriptor.native_entry != nullptr))
      return false;
    bool hasBytecode = (descriptor.flags & OBELISK_RT_EXPORT_HAS_BYTECODE) != 0;
    if (hasBytecode) {
      if ((execution.flags & OBELISK_RT_EXECUTION_HAS_BYTECODE) == 0 ||
          descriptor.bytecode_function == OBELISK_RT_EXPORT_NO_BYTECODE)
        return false;
    } else if (descriptor.bytecode_function != OBELISK_RT_EXPORT_NO_BYTECODE) {
      return false;
    }
    if ((execution.flags & OBELISK_RT_EXECUTION_REQUIRE_BYTECODE) != 0 &&
        (hasNative || !hasBytecode))
      return false;
  }
  return true;
}

extern "C" OBELISK_RT_FEATURE_TEXT obelisk_rt_status obelisk_rt_v1_export_call(
    uint32_t exportID, uint64_t abiSignature,
    const obelisk_rt_import_input_v1 *inputs, uint32_t inputCount,
    obelisk_rt_import_output_v1 *outputs, uint32_t outputCount) {
  ActiveDpiCall *call = activeDpiCall;
  auto fail = [&](obelisk_rt_status status) {
    if (call)
      latchFailure(*call, status);
    return status;
  };
  if (outputCount != 0 && !outputs)
    return fail(OBELISK_RT_INVALID_ARGUMENT);
  for (uint32_t index = 0; index != outputCount; ++index)
    if (!validOutput(outputs[index]))
      return fail(OBELISK_RT_INVALID_ARGUMENT);
  for (uint32_t index = 0; index != outputCount; ++index)
    zeroOutput(outputs[index]);
  if (inputCount != 0 && !inputs)
    return fail(OBELISK_RT_INVALID_ARGUMENT);
  if (!call || !call->context || !call->scope)
    return fail(OBELISK_RT_INVALID_LIFECYCLE);
  if (call->exportStatus != OBELISK_RT_OK)
    return call->exportStatus;
  if (exportID == 0 || abiSignature == 0)
    return fail(OBELISK_RT_INVALID_ARGUMENT);
  for (uint32_t index = 0; index != inputCount; ++index)
    if (!validInput(inputs[index]))
      return fail(OBELISK_RT_INVALID_ARGUMENT);
  obelisk_rt_context *context = call->context;
  const obelisk_rt_execution_descriptor_v1 *execution = context->execution;
  const obelisk_rt_execution_extension_v2 *extension =
      execution ? exportExtension(*execution) : nullptr;
  const obelisk_rt_export_descriptor_v1 *descriptor =
      extension ? findExport(*extension, exportID, call->scope->id) : nullptr;
  if (!descriptor)
    return fail(OBELISK_RT_INVALID_HANDLE);
  if (descriptor->abi_signature != abiSignature ||
      descriptor->input_count != inputCount ||
      descriptor->output_count != outputCount)
    return fail(OBELISK_RT_ARGUMENT_MISMATCH);

  ContextTransaction transaction(context);
  obelisk_rt_status result =
      obelisk_rt_feature_guarded(context, [&]() OBELISK_RT_FEATURE_HELPER {
        if (exportDepth == std::numeric_limits<size_t>::max())
          return fail(OBELISK_RT_OUT_OF_RESOURCES);
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
          stringCount += inputs[index].kind == OBELISK_RT_DBREG_STRING;
        for (uint32_t index = 0; index != outputCount; ++index)
          stringCount += outputs[index].kind == OBELISK_RT_DBREG_STRING;
        if (stringCount > std::numeric_limits<size_t>::max())
          return fail(OBELISK_RT_OUT_OF_RESOURCES);
        std::vector<obelisk_rt_managed_word_v1> roots(
            static_cast<size_t>(stringCount), 0);
        ScopedManagedRoots rooted(context, roots);
        if (rooted.getStatus() != OBELISK_RT_OK)
          return fail(rooted.getStatus());

        std::vector<obelisk_rt_import_input_v1> convertedInputs;
        std::vector<obelisk_rt_import_output_v1> convertedOutputs;
        if (inputCount != 0)
          convertedInputs.assign(inputs, inputs + inputCount);
        if (outputCount != 0)
          convertedOutputs.assign(outputs, outputs + outputCount);
        size_t root = 0;
        obelisk_rt_gc_lane_v1 *lane = rooted.getLane();
        for (uint32_t index = 0; index != inputCount; ++index) {
          if (inputs[index].kind != OBELISK_RT_DBREG_STRING)
            continue;
          convertedInputs[index].value = &roots[root];
          uint64_t pointerWord = *inputs[index].value;
          // Generated wrappers use a null raw pointer only for output formals,
          // whose incoming value is intentionally absent. Input and inout
          // strings always carry their initialized C address.
          if (pointerWord != 0) {
            if (pointerWord > std::numeric_limits<uintptr_t>::max())
              return fail(OBELISK_RT_INVALID_ARGUMENT);
            const char *text = reinterpret_cast<const char *>(
                static_cast<uintptr_t>(pointerWord));
            obelisk_rt_status status = obelisk_rt_v1_string_create(
                lane, text, std::strlen(text), &roots[root]);
            if (status != OBELISK_RT_OK)
              return fail(status);
          }
          ++root;
        }
        for (uint32_t index = 0; index != outputCount; ++index) {
          if (outputs[index].kind != OBELISK_RT_DBREG_STRING)
            continue;
          convertedOutputs[index].value = &roots[root++];
        }

        ScopedExportDepth depth;
        obelisk_rt_status status;
        bool requireBytecode =
            (execution->flags & OBELISK_RT_EXECUTION_REQUIRE_BYTECODE) != 0;
        if (!requireBytecode && descriptor->native_entry) {
          status = descriptor->native_entry(context, convertedInputs.data(),
                                            inputCount, convertedOutputs.data(),
                                            outputCount);
        } else if (obelisk_rt_execute_dpi_export_bytecode) {
          status = obelisk_rt_execute_dpi_export_bytecode(
              *execution, *descriptor, context, convertedInputs.data(),
              inputCount, convertedOutputs.data(), outputCount);
        } else {
          status = OBELISK_RT_TIER_UNAVAILABLE;
        }
        if (status != OBELISK_RT_OK)
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
          if (outputs[index].kind != OBELISK_RT_DBREG_STRING)
            continue;
          const char *bytes = nullptr;
          uint64_t size = 0;
          status = obelisk_rt_v1_string_view(*convertedOutputs[index].value,
                                             scratch, &bytes, &size);
          if (status != OBELISK_RT_OK)
            return fail(status);
          if (size > std::numeric_limits<size_t>::max())
            return fail(OBELISK_RT_OUT_OF_RESOURCES);
          if (size == 0)
            exportStrings[index].clear();
          else
            exportStrings[index].assign(bytes, static_cast<size_t>(size));
        }
        return OBELISK_RT_OK;
      });
  if (result != OBELISK_RT_OK)
    latchFailure(*call, result);
  return result;
}

extern "C" OBELISK_RT_FEATURE_TEXT const char *
obelisk_rt_v1_export_string(uint32_t outputIndex) {
  return exportDepth < exportStringFrames.size() &&
                 outputIndex < exportStringFrames[exportDepth].size()
             ? exportStringFrames[exportDepth][outputIndex].c_str()
             : nullptr;
}
