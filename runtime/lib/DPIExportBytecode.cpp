//===- DPIExportBytecode.cpp - Bytecode DPI export entry ----------------===//

#include "RuntimeInternal.h"

#include <limits>

extern "C" OBELISK_RT_FEATURE_TEXT void
obelisk_rt_v1_dpi_export_bytecode_link_anchor() {}

OBELISK_RT_FEATURE_TEXT obelisk_rt_status
obelisk_rt_execute_dpi_export_bytecode(
    const obelisk_rt_execution_descriptor_v1 &execution,
    const obelisk_rt_export_descriptor_v1 &descriptor,
    obelisk_rt_context *context, const obelisk_rt_import_input_v1 *inputs,
    uint32_t inputCount, obelisk_rt_import_output_v1 *outputs,
    uint32_t outputCount) noexcept {
  return obelisk_rt_execute_design_export(
      execution, descriptor, context, inputs, inputCount, outputs, outputCount);
}

extern "C" OBELISK_RT_FEATURE_TEXT obelisk_rt_status
obelisk_rt_v1_dpi_export_task_bytecode_run(
    obelisk_rt_context *context, uint32_t exportID,
    const obelisk_rt_import_input_v1 *inputs, uint32_t inputCount,
    obelisk_rt_import_output_v1 *outputs, uint32_t outputCount,
    const uint8_t *directions, const int64_t *const *aggregatePlans,
    const uint64_t *aggregatePlanWords) {
  if (!context || !context->execution || !activeDpiCall ||
      activeDpiCall->context != context || !activeDpiCall->scope ||
      exportID == 0 ||
      (inputCount != 0 &&
       (!inputs || !directions || !aggregatePlans || !aggregatePlanWords)) ||
      (outputCount != 0 && !outputs))
    return OBELISK_RT_INVALID_ARGUMENT;
  const obelisk_rt_execution_descriptor_v1 &execution = *context->execution;
  if ((execution.flags & OBELISK_RT_EXECUTION_DPI_EXPORTS) == 0 ||
      execution.reserved < sizeof(execution))
    return OBELISK_RT_INVALID_DESIGN;
  uintptr_t base = reinterpret_cast<uintptr_t>(&execution);
  if (execution.reserved > std::numeric_limits<uintptr_t>::max() - base)
    return OBELISK_RT_INVALID_DESIGN;
  auto *extension = reinterpret_cast<const obelisk_rt_execution_extension_v1 *>(
      base + static_cast<uintptr_t>(execution.reserved));
  if (extension->version != OBELISK_RT_EXECUTION_EXTENSION_VERSION ||
      extension->size != sizeof(*extension) || !extension->exports)
    return OBELISK_RT_INVALID_DESIGN;
  const obelisk_rt_export_descriptor_v1 *descriptor = nullptr;
  for (uint64_t index = 0; index != extension->export_count; ++index) {
    const obelisk_rt_export_descriptor_v1 &candidate =
        extension->exports[index];
    if (candidate.export_id == exportID &&
        candidate.scope_id == activeDpiCall->scope->id) {
      descriptor = &candidate;
      break;
    }
  }
  if (!descriptor ||
      (descriptor->flags &
       (OBELISK_RT_EXPORT_TASK | OBELISK_RT_EXPORT_HAS_BYTECODE)) !=
          (OBELISK_RT_EXPORT_TASK | OBELISK_RT_EXPORT_HAS_BYTECODE))
    return OBELISK_RT_INVALID_HANDLE;
  return obelisk_rt_execute_design_export_task(
      execution, *descriptor, context, inputs, inputCount, outputs, outputCount,
      directions, aggregatePlans, aggregatePlanWords);
}
