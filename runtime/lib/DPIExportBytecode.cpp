//===- DPIExportBytecode.cpp - Bytecode DPI export entry ----------------===//

#include "ProcessContext.h"
#include "ProcessShared.h"
#include "RuntimeInternal.h"

#include <limits>

namespace {
// An exported function runs in the interpreter, which writes shared planes in
// place without reporting its footprint to the generated unknown summary.
talos_rt_status reportExportWrites(talos_rt_context *context,
                                     talos_rt_status status) {
  if (context) {
    ContextMutexLock lock(context);
    markNativeUnknownAllUnlocked(context);
  }
  return status;
}
} // namespace

extern "C" TALOS_RT_FEATURE_TEXT void
talos_rt_v1_dpi_export_bytecode_link_anchor() {}

TALOS_RT_FEATURE_TEXT talos_rt_status
talos_rt_execute_dpi_export_bytecode(
    const talos_rt_execution_descriptor_v1 &execution,
    const talos_rt_export_descriptor_v1 &descriptor,
    talos_rt_context *context, const talos_rt_import_input_v1 *inputs,
    uint32_t inputCount, talos_rt_import_output_v1 *outputs,
    uint32_t outputCount) noexcept {
  return reportExportWrites(context, talos_rt_execute_design_export(
                                         execution, descriptor, context, inputs,
                                         inputCount, outputs, outputCount));
}

extern "C" TALOS_RT_FEATURE_TEXT talos_rt_status
talos_rt_v1_dpi_export_task_bytecode_run(
    talos_rt_context *context, uint32_t exportID,
    const talos_rt_import_input_v1 *inputs, uint32_t inputCount,
    talos_rt_import_output_v1 *outputs, uint32_t outputCount,
    const uint8_t *directions, const int64_t *const *aggregatePlans,
    const uint64_t *aggregatePlanWords) {
  if (!context || !context->execution || !activeDpiCall ||
      activeDpiCall->context != context || !activeDpiCall->scope ||
      exportID == 0 ||
      (inputCount != 0 &&
       (!inputs || !directions || !aggregatePlans || !aggregatePlanWords)) ||
      (outputCount != 0 && !outputs))
    return TALOS_RT_INVALID_ARGUMENT;
  const talos_rt_execution_descriptor_v1 &execution = *context->execution;
  if ((execution.flags & TALOS_RT_EXECUTION_DPI_EXPORTS) == 0 ||
      execution.reserved < sizeof(execution))
    return TALOS_RT_INVALID_DESIGN;
  uintptr_t base = reinterpret_cast<uintptr_t>(&execution);
  if (execution.reserved > std::numeric_limits<uintptr_t>::max() - base)
    return TALOS_RT_INVALID_DESIGN;
  auto *extension = reinterpret_cast<const talos_rt_execution_extension_v1 *>(
      base + static_cast<uintptr_t>(execution.reserved));
  if (extension->version != TALOS_RT_EXECUTION_EXTENSION_VERSION ||
      extension->size != sizeof(*extension) || !extension->exports)
    return TALOS_RT_INVALID_DESIGN;
  const talos_rt_export_descriptor_v1 *descriptor = nullptr;
  for (uint64_t index = 0; index != extension->export_count; ++index) {
    const talos_rt_export_descriptor_v1 &candidate =
        extension->exports[index];
    if (candidate.export_id == exportID &&
        candidate.scope_id == activeDpiCall->scope->id) {
      descriptor = &candidate;
      break;
    }
  }
  if (!descriptor ||
      (descriptor->flags &
       (TALOS_RT_EXPORT_TASK | TALOS_RT_EXPORT_HAS_BYTECODE)) !=
          (TALOS_RT_EXPORT_TASK | TALOS_RT_EXPORT_HAS_BYTECODE))
    return TALOS_RT_INVALID_HANDLE;
  return reportExportWrites(context,
                            talos_rt_execute_design_export_task(
                                execution, *descriptor, context, inputs,
                                inputCount, outputs, outputCount, directions,
                                aggregatePlans, aggregatePlanWords));
}
