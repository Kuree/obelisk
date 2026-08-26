//===- DPIExportBytecode.cpp - Bytecode DPI export entry ----------------===//

#include "RuntimeInternal.h"

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
