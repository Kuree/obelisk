//===- RuntimeCAPISmoke.c - Compile and exercise the runtime C ABI --------===//

#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHandle.h"

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(obelisk_rt_status) == 4, "status ABI changed");
_Static_assert(sizeof(obelisk_rt_arg_kind) == 4, "argument kind ABI changed");
_Static_assert(sizeof(obelisk_rt_arg_flags) == 4, "argument flags ABI changed");
_Static_assert(offsetof(obelisk_rt_arg_v1, kind) == 0,
               "argument kind offset changed");
_Static_assert(offsetof(obelisk_rt_arg_v1, flags) == 4,
               "argument flags offset changed");
_Static_assert(offsetof(obelisk_rt_arg_v1, size) == 8,
               "argument size offset changed");
_Static_assert(offsetof(obelisk_rt_arg_v1, data) == 16,
               "argument data offset changed");
_Static_assert(offsetof(obelisk_rt_arg_v1, unknown) == 16 + sizeof(void *),
               "argument unknown offset changed");
_Static_assert(offsetof(obelisk_rt_buffer_v1, data) == 0,
               "buffer data offset changed");
_Static_assert(offsetof(obelisk_rt_format_env_v1, scope) == 0,
               "environment scope offset changed");
_Static_assert(sizeof(obelisk_rt_format_env_v1) == 64,
               "format environment size changed");
_Static_assert(offsetof(obelisk_rt_format_env_v1, library_cell) == 16,
               "environment library.cell offset changed");
_Static_assert(offsetof(obelisk_rt_format_env_v1, time_multiplier) == 56,
               "environment time multiplier offset changed");
_Static_assert(sizeof(obelisk_rt_handle_v1) == 16, "stable handle ABI changed");
_Static_assert(offsetof(obelisk_rt_handle_v1, kind) == 0,
               "stable handle kind offset changed");
_Static_assert(offsetof(obelisk_rt_handle_v1, generation) == 4,
               "stable handle generation offset changed");
_Static_assert(offsetof(obelisk_rt_handle_v1, id) == 8,
               "stable handle ID offset changed");
_Static_assert(sizeof(obelisk_rt_fragment_action_v1) == 32,
               "fragment action size changed");
_Static_assert(offsetof(obelisk_rt_fragment_action_v1, kind) == 0,
               "fragment action kind offset changed");
_Static_assert(offsetof(obelisk_rt_fragment_action_v1, suspend_kind) == 4,
               "fragment suspend kind offset changed");
_Static_assert(offsetof(obelisk_rt_fragment_action_v1, continuation) == 8,
               "fragment continuation offset changed");
_Static_assert(offsetof(obelisk_rt_fragment_action_v1, flags) == 12,
               "fragment action flags offset changed");
_Static_assert(offsetof(obelisk_rt_fragment_action_v1, payload) == 16,
               "fragment action ABI changed");
_Static_assert(offsetof(obelisk_rt_fragment_action_v1, auxiliary) == 24,
               "fragment auxiliary offset changed");
_Static_assert(sizeof(obelisk_rt_frame_field_v1) == 32,
               "process frame field size changed");
_Static_assert(sizeof(obelisk_rt_frame_layout_v1) == 56,
               "process frame layout size changed");
_Static_assert(offsetof(obelisk_rt_frame_layout_v1, fields) == 24,
               "process frame fields offset changed");
_Static_assert(offsetof(obelisk_rt_frame_layout_v1, continuations) == 40,
               "process frame continuations offset changed");
_Static_assert(offsetof(obelisk_rt_frame_layout_v1, checksum) == 48,
               "process frame checksum offset changed");
_Static_assert(sizeof(obelisk_rt_wait_record_v1) == 32,
               "process wait record size changed");
_Static_assert(sizeof(obelisk_rt_wait_entry_v1) == 16,
               "process wait entry size changed");
_Static_assert(offsetof(obelisk_rt_wait_entry_v1, edge) == 8,
               "process wait entry edge offset changed");
_Static_assert(sizeof(obelisk_rt_process_descriptor_v1) == 88,
               "process descriptor size changed");
_Static_assert(offsetof(obelisk_rt_process_descriptor_v1, frame_layout) == 32,
               "process frame layout pointer offset changed");
_Static_assert(offsetof(obelisk_rt_process_descriptor_v1, bytecode) == 64,
               "process bytecode pointer offset changed");
_Static_assert(offsetof(obelisk_rt_process_descriptor_v1, execution) == 72,
               "process execution descriptor offset changed");
_Static_assert(offsetof(obelisk_rt_process_descriptor_v1, design_bytecode) ==
                   80,
               "process design bytecode offset changed");
_Static_assert(sizeof(obelisk_rt_dpi_scope_v1) == 48,
               "DPI scope descriptor size changed");
_Static_assert(offsetof(obelisk_rt_dpi_scope_v1, name) == 16,
               "DPI scope name offset changed");
_Static_assert(sizeof(obelisk_rt_activation_descriptor_v1) == 24,
               "activation descriptor size changed");
_Static_assert(offsetof(obelisk_rt_activation_descriptor_v1, native_entry) == 8,
               "activation native entry offset changed");
_Static_assert(
    offsetof(obelisk_rt_activation_descriptor_v1, bytecode_function) == 16,
    "activation bytecode entry offset changed");
_Static_assert(sizeof(obelisk_rt_observer_capture_abi_v1) == 8,
               "observer capture ABI size changed");
_Static_assert(sizeof(obelisk_rt_observer_descriptor_v1) == 48,
               "observer descriptor size changed");
_Static_assert(offsetof(obelisk_rt_observer_descriptor_v1,
                        native_evaluator) == 32,
               "observer native evaluator offset changed");
_Static_assert(sizeof(obelisk_rt_execution_descriptor_v1) == 120,
               "execution descriptor size changed");
_Static_assert(offsetof(obelisk_rt_execution_descriptor_v1, version) == 0,
               "execution version offset changed");
_Static_assert(offsetof(obelisk_rt_execution_descriptor_v1, flags) == 4,
               "execution flags offset changed");
_Static_assert(offsetof(obelisk_rt_execution_descriptor_v1, reserved) == 8,
               "execution reserved offset changed");
_Static_assert(offsetof(obelisk_rt_execution_descriptor_v1, dpi_scopes) == 64,
               "execution DPI scope offset changed");
_Static_assert(offsetof(obelisk_rt_execution_descriptor_v1, activations) == 88,
               "execution activation table offset changed");
_Static_assert(offsetof(obelisk_rt_execution_descriptor_v1, observers) == 104,
               "execution observer table offset changed");
_Static_assert(sizeof(obelisk_rt_computed_wait_record_v1) == 96,
               "computed wait record size changed");
_Static_assert(sizeof(obelisk_rt_computed_observer_v1) == 32,
               "computed observer binding size changed");
_Static_assert(sizeof(obelisk_rt_computed_capture_v1) == 32,
               "computed observer capture size changed");
_Static_assert(sizeof(obelisk_rt_computed_dependency_v1) == 16,
               "computed observer dependency size changed");
_Static_assert(sizeof(obelisk_rt_computed_clause_v1) == 16,
               "computed observer clause size changed");
_Static_assert(sizeof(obelisk_rt_import_site_v1) == 56,
               "DPI import site size changed");
_Static_assert(sizeof(obelisk_rt_design_bytecode_entry_v1) == 16,
               "design bytecode entry size changed");
_Static_assert(sizeof(obelisk_rt_design_info_v1) == 56,
               "design info size changed");
_Static_assert(sizeof(obelisk_rt_process_instance_v1) == 104,
               "process instance size changed");
_Static_assert(offsetof(obelisk_rt_process_instance_v1, allocation) == 8,
               "process allocation offset changed");
_Static_assert(offsetof(obelisk_rt_process_instance_v1, frame) == 16,
               "process instance frame offset changed");
_Static_assert(offsetof(obelisk_rt_process_instance_v1, scratch_offset) == 32,
               "process scratch offset changed");
_Static_assert(offsetof(obelisk_rt_process_instance_v1, native_handle) == 48,
               "process native handle offset changed");
_Static_assert(offsetof(obelisk_rt_process_instance_v1, continuation) == 56,
               "process continuation offset changed");
_Static_assert(offsetof(obelisk_rt_process_instance_v1, status) == 68,
               "process status offset changed");
_Static_assert(offsetof(obelisk_rt_process_instance_v1, context) == 72,
               "process transient context offset changed");
_Static_assert(offsetof(obelisk_rt_process_instance_v1, action) == 80,
               "process transient action offset changed");
_Static_assert(offsetof(obelisk_rt_process_instance_v1, ownership_context) ==
                   88,
               "process ownership context offset changed");
_Static_assert(offsetof(obelisk_rt_process_instance_v1, observer_pin_count) ==
                   96,
               "process observer pin offset changed");
_Static_assert(
    offsetof(obelisk_rt_process_instance_v1, observer_destroy_pending) == 100,
    "process observer pending-destroy offset changed");
_Static_assert(sizeof(obelisk_rt_bytecode_entry_v1) == 8,
               "bytecode entry size changed");
_Static_assert(offsetof(obelisk_rt_bytecode_entry_v1, continuation) == 0,
               "bytecode continuation offset changed");
_Static_assert(offsetof(obelisk_rt_bytecode_entry_v1, instruction) == 4,
               "bytecode instruction offset changed");
_Static_assert(sizeof(obelisk_rt_bytecode_v1) == 96,
               "bytecode descriptor size changed");
_Static_assert(offsetof(obelisk_rt_bytecode_v1, code) == 0,
               "bytecode code offset changed");
_Static_assert(offsetof(obelisk_rt_bytecode_v1, code_size) == sizeof(void *),
               "bytecode size offset changed");
_Static_assert(offsetof(obelisk_rt_bytecode_v1, entries) ==
                   sizeof(void *) + sizeof(uint64_t),
               "bytecode entries offset changed");
_Static_assert(offsetof(obelisk_rt_bytecode_v1, entry_count) == 24,
               "bytecode entry count offset changed");
_Static_assert(offsetof(obelisk_rt_bytecode_v1, register_count) == 28,
               "bytecode register count offset changed");
_Static_assert(offsetof(obelisk_rt_bytecode_v1, register_offset) == 32,
               "bytecode register offset changed");
_Static_assert(offsetof(obelisk_rt_bytecode_v1, validation) == 40,
               "bytecode validation offset changed");
_Static_assert(offsetof(obelisk_rt_bytecode_v1, constants) == 48,
               "bytecode constants offset changed");
_Static_assert(offsetof(obelisk_rt_bytecode_v1, constant_size) == 56,
               "bytecode constant size offset changed");
_Static_assert(offsetof(obelisk_rt_bytecode_v1, service_sites) == 64,
               "bytecode service sites offset changed");
_Static_assert(offsetof(obelisk_rt_bytecode_v1, service_site_count) == 72,
               "bytecode service count offset changed");
_Static_assert(offsetof(obelisk_rt_bytecode_v1, reserved) == 76,
               "bytecode reserved offset changed");
_Static_assert(offsetof(obelisk_rt_bytecode_v1, operands) == 80,
               "bytecode operands offset changed");
_Static_assert(offsetof(obelisk_rt_bytecode_v1, operand_count) == 88,
               "bytecode operand count offset changed");
_Static_assert(sizeof(obelisk_rt_bytecode_validation_v1) == 8,
               "bytecode validation record size changed");
_Static_assert(offsetof(obelisk_rt_bytecode_validation_v1, state) == 0,
               "bytecode validation state offset changed");
_Static_assert(offsetof(obelisk_rt_bytecode_validation_v1, reserved) == 4,
               "bytecode validation reserved offset changed");
_Static_assert(sizeof(obelisk_rt_bytecode_operand_v1) == 32,
               "bytecode operand size changed");
_Static_assert(offsetof(obelisk_rt_bytecode_operand_v1, kind) == 0,
               "bytecode operand kind offset changed");
_Static_assert(offsetof(obelisk_rt_bytecode_operand_v1, direction) == 1,
               "bytecode operand direction offset changed");
_Static_assert(offsetof(obelisk_rt_bytecode_operand_v1, value_kind) == 2,
               "bytecode operand value kind offset changed");
_Static_assert(offsetof(obelisk_rt_bytecode_operand_v1, flags) == 3,
               "bytecode operand flags offset changed");
_Static_assert(offsetof(obelisk_rt_bytecode_operand_v1, reserved) == 4,
               "bytecode operand reserved offset changed");
_Static_assert(offsetof(obelisk_rt_bytecode_operand_v1, value) == 8,
               "bytecode operand value offset changed");
_Static_assert(offsetof(obelisk_rt_bytecode_operand_v1, size) == 16,
               "bytecode operand size offset changed");
_Static_assert(offsetof(obelisk_rt_bytecode_operand_v1, auxiliary) == 24,
               "bytecode operand auxiliary offset changed");
_Static_assert(sizeof(obelisk_rt_bytecode_service_site_v1) == 16,
               "bytecode service site size changed");
_Static_assert(offsetof(obelisk_rt_bytecode_service_site_v1, service) == 0,
               "bytecode service offset changed");
_Static_assert(offsetof(obelisk_rt_bytecode_service_site_v1, first_operand) ==
                   4,
               "bytecode first operand offset changed");
_Static_assert(offsetof(obelisk_rt_bytecode_service_site_v1, operand_count) ==
                   8,
               "bytecode operand count offset changed");
_Static_assert(offsetof(obelisk_rt_bytecode_service_site_v1, flags) == 10,
               "bytecode service flags offset changed");
_Static_assert(offsetof(obelisk_rt_bytecode_service_site_v1, reserved) == 12,
               "bytecode service reserved offset changed");
_Static_assert(sizeof(obelisk_rt_fragment_descriptor_v1) == 120,
               "fragment descriptor size changed");
_Static_assert(offsetof(obelisk_rt_fragment_descriptor_v1, handle) == 0,
               "fragment handle offset changed");
_Static_assert(offsetof(obelisk_rt_fragment_descriptor_v1, code_kind) == 16,
               "fragment code kind offset changed");
_Static_assert(offsetof(obelisk_rt_fragment_descriptor_v1, flags) == 20,
               "fragment flags offset changed");
_Static_assert(offsetof(obelisk_rt_fragment_descriptor_v1, code) == 24,
               "fragment code offset changed");
_Static_assert(OBELISK_RT_BYTECODE_INSTRUCTION_SIZE == 16u,
               "bytecode instruction size changed");

static obelisk_rt_status
c_native_fragment(obelisk_rt_context *context, void *frame, uint64_t frame_size,
                  uint32_t continuation,
                  obelisk_rt_fragment_action_v1 *out_action) {
  uint64_t *counter = (uint64_t *)frame;
  (void)context;
  if (frame_size != sizeof(*counter))
    return OBELISK_RT_INVALID_ARGUMENT;
  ++*counter;
  *out_action = (obelisk_rt_fragment_action_v1){OBELISK_RT_FRAGMENT_CONTINUE,
                                                OBELISK_RT_SUSPEND_NONE,
                                                continuation + 1,
                                                OBELISK_RT_FRAGMENT_FLAGS_NONE,
                                                0,
                                                0};
  return OBELISK_RT_OK;
}

int obelisk_runtime_c_api_smoke(void) {
  obelisk_rt_context *context = NULL;
  obelisk_rt_buffer_v1 output = {NULL, 0};
  obelisk_rt_arg_v1 empty_string = {OBELISK_RT_ARG_STRING, 0, 0, NULL, NULL};
  obelisk_rt_fragment_descriptor_v1 fragment = {0};
  obelisk_rt_fragment_action_v1 action = {0};
  uint64_t frame = 41;
  obelisk_rt_stable_handle_v1 decoded = {0};
  uint64_t stable = obelisk_rt_stable_handle_encode(
      OBELISK_RT_STABLE_HANDLE_STATIC, 7, -3);

  if (OBELISK_RT_VERSION != 1u)
    return 1;
  if (!obelisk_rt_stable_handle_decode(stable, &decoded) ||
      decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC || decoded.id != 7 ||
      decoded.offset != -3 ||
      obelisk_rt_stable_handle_offset(stable, 5) == UINT64_MAX ||
      !obelisk_rt_stable_handle_same_object(
          stable, obelisk_rt_stable_handle_offset(stable, 5)))
    return 20;
  if (obelisk_rt_v1_context_create(&context) != OBELISK_RT_OK || !context)
    return 2;
  if (obelisk_rt_v1_format(context, "%s", 2, &empty_string, 1, NULL, &output) !=
      OBELISK_RT_OK) {
    obelisk_rt_v1_context_destroy(context);
    return 3;
  }
  if (output.data != NULL || output.size != 0) {
    obelisk_rt_v1_buffer_release(&output);
    obelisk_rt_v1_context_destroy(context);
    return 4;
  }
  if (strcmp(obelisk_rt_v1_status_string(OBELISK_RT_OK), "ok") != 0) {
    obelisk_rt_v1_context_destroy(context);
    return 5;
  }
  fragment.handle =
      (obelisk_rt_handle_v1){OBELISK_RT_DESCRIPTOR_FRAGMENT, 0, 9};
  fragment.code_kind = OBELISK_RT_FRAGMENT_NATIVE;
  fragment.code.native_entry = c_native_fragment;
  if (obelisk_rt_v1_fragment_execute(&fragment, context, &frame, sizeof(frame),
                                     17, &action) != OBELISK_RT_OK ||
      frame != 42 || action.kind != OBELISK_RT_FRAGMENT_CONTINUE ||
      action.continuation != 18) {
    obelisk_rt_v1_context_destroy(context);
    return 6;
  }

  obelisk_rt_v1_buffer_release(&output);
  obelisk_rt_v1_context_destroy(context);
  return 0;
}
