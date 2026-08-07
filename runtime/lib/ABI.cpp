//===- ABI.cpp - Compile-time verification of the public runtime ABI -----===//

#include "obelisk/Runtime/Runtime.h"

#include <climits>
#include <cstddef>
#include <cstdint>
#include <type_traits>

static_assert(CHAR_BIT == 8, "the runtime ABI requires 8-bit bytes");
static_assert(sizeof(void *) == 8,
              "the initial runtime ABI supports only 64-bit targets");
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
              "the initial runtime ABI supports only little-endian targets");
#endif

#define ABI_SIZE_ALIGN(Type, Size, Align)                                      \
  static_assert(sizeof(Type) == Size, #Type " size changed");                  \
  static_assert(alignof(Type) == Align, #Type " alignment changed")
#define ABI_OFFSET(Type, Field, Offset)                                        \
  static_assert(offsetof(Type, Field) == Offset,                               \
                #Type "." #Field " offset changed")

ABI_SIZE_ALIGN(obelisk_rt_status, 4, 4);
ABI_SIZE_ALIGN(obelisk_rt_buffer_v1, 16, 8);
ABI_OFFSET(obelisk_rt_buffer_v1, data, 0);
ABI_OFFSET(obelisk_rt_buffer_v1, size, 8);

ABI_SIZE_ALIGN(obelisk_rt_trace_entry_v1, 40, 8);
ABI_OFFSET(obelisk_rt_trace_entry_v1, offset, 0);
ABI_OFFSET(obelisk_rt_trace_entry_v1, stride, 8);
ABI_OFFSET(obelisk_rt_trace_entry_v1, count, 16);
ABI_OFFSET(obelisk_rt_trace_entry_v1, kind, 24);
ABI_OFFSET(obelisk_rt_trace_entry_v1, slot_kind, 28);
ABI_OFFSET(obelisk_rt_trace_entry_v1, child_layout, 32);
ABI_SIZE_ALIGN(obelisk_rt_trace_layout_v1, 40, 8);
ABI_OFFSET(obelisk_rt_trace_layout_v1, version, 0);
ABI_OFFSET(obelisk_rt_trace_layout_v1, reserved, 4);
ABI_OFFSET(obelisk_rt_trace_layout_v1, size, 8);
ABI_OFFSET(obelisk_rt_trace_layout_v1, alignment, 16);
ABI_OFFSET(obelisk_rt_trace_layout_v1, entries, 24);
ABI_OFFSET(obelisk_rt_trace_layout_v1, entry_count, 32);
ABI_SIZE_ALIGN(obelisk_rt_method_argument_v1, 16, 8);
ABI_OFFSET(obelisk_rt_method_argument_v1, data, 0);
ABI_OFFSET(obelisk_rt_method_argument_v1, size, 8);
ABI_SIZE_ALIGN(obelisk_rt_method_descriptor_v1, 32, 8);
ABI_OFFSET(obelisk_rt_method_descriptor_v1, signature_id, 0);
ABI_OFFSET(obelisk_rt_method_descriptor_v1, flags, 8);
ABI_OFFSET(obelisk_rt_method_descriptor_v1, bytecode_function, 12);
ABI_OFFSET(obelisk_rt_method_descriptor_v1, native_entry, 16);
ABI_OFFSET(obelisk_rt_method_descriptor_v1, environment, 24);
ABI_SIZE_ALIGN(obelisk_rt_class_descriptor_v1, 96, 8);
ABI_OFFSET(obelisk_rt_class_descriptor_v1, version, 0);
ABI_OFFSET(obelisk_rt_class_descriptor_v1, flags, 4);
ABI_OFFSET(obelisk_rt_class_descriptor_v1, class_id, 8);
ABI_OFFSET(obelisk_rt_class_descriptor_v1, instance_size, 16);
ABI_OFFSET(obelisk_rt_class_descriptor_v1, instance_alignment, 24);
ABI_OFFSET(obelisk_rt_class_descriptor_v1, base, 32);
ABI_OFFSET(obelisk_rt_class_descriptor_v1, interface_ids, 40);
ABI_OFFSET(obelisk_rt_class_descriptor_v1, interface_count, 48);
ABI_OFFSET(obelisk_rt_class_descriptor_v1, layout, 56);
ABI_OFFSET(obelisk_rt_class_descriptor_v1, methods, 64);
ABI_OFFSET(obelisk_rt_class_descriptor_v1, method_count, 72);
ABI_OFFSET(obelisk_rt_class_descriptor_v1, debug_name, 80);
ABI_OFFSET(obelisk_rt_class_descriptor_v1, debug_name_size, 88);
ABI_SIZE_ALIGN(obelisk_rt_element_type_v1, 56, 8);
ABI_OFFSET(obelisk_rt_element_type_v1, version, 0);
ABI_OFFSET(obelisk_rt_element_type_v1, kind, 4);
ABI_OFFSET(obelisk_rt_element_type_v1, type_id, 8);
ABI_OFFSET(obelisk_rt_element_type_v1, flags, 16);
ABI_OFFSET(obelisk_rt_element_type_v1, reserved, 20);
ABI_OFFSET(obelisk_rt_element_type_v1, value_size, 24);
ABI_OFFSET(obelisk_rt_element_type_v1, alignment, 32);
ABI_OFFSET(obelisk_rt_element_type_v1, bit_width, 40);
ABI_OFFSET(obelisk_rt_element_type_v1, trace, 48);
ABI_SIZE_ALIGN(obelisk_rt_assoc_key_v1, 40, 8);
ABI_OFFSET(obelisk_rt_assoc_key_v1, kind, 0);
ABI_OFFSET(obelisk_rt_assoc_key_v1, reserved, 4);
ABI_OFFSET(obelisk_rt_assoc_key_v1, width, 8);
ABI_OFFSET(obelisk_rt_assoc_key_v1, value, 16);
ABI_OFFSET(obelisk_rt_assoc_key_v1, unknown, 24);
ABI_OFFSET(obelisk_rt_assoc_key_v1, string, 32);
ABI_SIZE_ALIGN(obelisk_rt_gc_root_v1, 24, 8);
ABI_SIZE_ALIGN(obelisk_rt_gc_root_range_v1, 32, 8);
ABI_OFFSET(obelisk_rt_gc_root_range_v1, slots, 0);
ABI_OFFSET(obelisk_rt_gc_root_range_v1, count, 8);
ABI_OFFSET(obelisk_rt_gc_root_range_v1, previous, 16);
ABI_OFFSET(obelisk_rt_gc_root_range_v1, cookie, 24);
ABI_SIZE_ALIGN(obelisk_rt_gc_managed_root_v1, 24, 8);
ABI_OFFSET(obelisk_rt_gc_managed_root_v1, slot, 0);
ABI_OFFSET(obelisk_rt_gc_managed_root_v1, previous, 8);
ABI_OFFSET(obelisk_rt_gc_managed_root_v1, cookie, 16);
ABI_SIZE_ALIGN(obelisk_rt_gc_managed_root_range_v1, 32, 8);
ABI_OFFSET(obelisk_rt_gc_managed_root_range_v1, slots, 0);
ABI_OFFSET(obelisk_rt_gc_managed_root_range_v1, count, 8);
ABI_OFFSET(obelisk_rt_gc_managed_root_range_v1, previous, 16);
ABI_OFFSET(obelisk_rt_gc_managed_root_range_v1, cookie, 24);
ABI_SIZE_ALIGN(obelisk_rt_string_span_v1, 8, 8);
ABI_OFFSET(obelisk_rt_string_span_v1, string, 0);
ABI_OFFSET(obelisk_rt_gc_root_v1, slot, 0);
ABI_OFFSET(obelisk_rt_gc_root_v1, previous, 8);
ABI_OFFSET(obelisk_rt_gc_root_v1, cookie, 16);
ABI_SIZE_ALIGN(obelisk_rt_gc_statistics_v1, 72, 8);

ABI_SIZE_ALIGN(obelisk_rt_handle_v1, 16, 8);
ABI_OFFSET(obelisk_rt_handle_v1, kind, 0);
ABI_OFFSET(obelisk_rt_handle_v1, generation, 4);
ABI_OFFSET(obelisk_rt_handle_v1, id, 8);

ABI_SIZE_ALIGN(obelisk_rt_dpi_scope_v1, 48, 8);
ABI_OFFSET(obelisk_rt_dpi_scope_v1, id, 0);
ABI_OFFSET(obelisk_rt_dpi_scope_v1, parent_id, 8);
ABI_OFFSET(obelisk_rt_dpi_scope_v1, name, 16);
ABI_OFFSET(obelisk_rt_dpi_scope_v1, name_size, 24);
ABI_OFFSET(obelisk_rt_dpi_scope_v1, time_unit, 32);
ABI_OFFSET(obelisk_rt_dpi_scope_v1, time_precision, 36);
ABI_OFFSET(obelisk_rt_dpi_scope_v1, reserved, 40);
ABI_SIZE_ALIGN(obelisk_rt_activation_descriptor_v1, 24, 8);
ABI_OFFSET(obelisk_rt_activation_descriptor_v1, code_unit_id, 0);
ABI_OFFSET(obelisk_rt_activation_descriptor_v1, native_entry, 8);
ABI_OFFSET(obelisk_rt_activation_descriptor_v1, bytecode_function, 16);
ABI_OFFSET(obelisk_rt_activation_descriptor_v1, flags, 20);
ABI_SIZE_ALIGN(obelisk_rt_observer_capture_abi_v1, 8, 4);
ABI_SIZE_ALIGN(obelisk_rt_observer_descriptor_v1, 48, 8);
ABI_OFFSET(obelisk_rt_observer_descriptor_v1, code_unit_id, 0);
ABI_OFFSET(obelisk_rt_observer_descriptor_v1, capture_abi, 8);
ABI_OFFSET(obelisk_rt_observer_descriptor_v1, capture_count, 16);
ABI_OFFSET(obelisk_rt_observer_descriptor_v1, result_width, 20);
ABI_OFFSET(obelisk_rt_observer_descriptor_v1, flags, 24);
ABI_OFFSET(obelisk_rt_observer_descriptor_v1, bytecode_function, 28);
ABI_OFFSET(obelisk_rt_observer_descriptor_v1, native_evaluator, 32);
ABI_OFFSET(obelisk_rt_observer_descriptor_v1, reserved, 40);
ABI_SIZE_ALIGN(obelisk_rt_execution_descriptor_v1, 120, 8);
ABI_OFFSET(obelisk_rt_execution_descriptor_v1, version, 0);
ABI_OFFSET(obelisk_rt_execution_descriptor_v1, flags, 4);
ABI_OFFSET(obelisk_rt_execution_descriptor_v1, reserved, 8);
ABI_OFFSET(obelisk_rt_execution_descriptor_v1, bytecode, 16);
ABI_OFFSET(obelisk_rt_execution_descriptor_v1, bytecode_size, 24);
ABI_OFFSET(obelisk_rt_execution_descriptor_v1, design_database, 32);
ABI_OFFSET(obelisk_rt_execution_descriptor_v1, design_database_size, 40);
ABI_OFFSET(obelisk_rt_execution_descriptor_v1, state_bit_count, 48);
ABI_OFFSET(obelisk_rt_execution_descriptor_v1, checksum, 56);
ABI_OFFSET(obelisk_rt_execution_descriptor_v1, dpi_scopes, 64);
ABI_OFFSET(obelisk_rt_execution_descriptor_v1, dpi_scope_count, 72);
ABI_OFFSET(obelisk_rt_execution_descriptor_v1, dpi_time_precision, 80);
ABI_OFFSET(obelisk_rt_execution_descriptor_v1, dpi_reserved, 84);
ABI_OFFSET(obelisk_rt_execution_descriptor_v1, activations, 88);
ABI_OFFSET(obelisk_rt_execution_descriptor_v1, activation_count, 96);
ABI_OFFSET(obelisk_rt_execution_descriptor_v1, observers, 104);
ABI_OFFSET(obelisk_rt_execution_descriptor_v1, observer_count, 112);
ABI_SIZE_ALIGN(obelisk_rt_design_bytecode_entry_v1, 16, 8);
ABI_OFFSET(obelisk_rt_design_bytecode_entry_v1, execution, 0);
ABI_OFFSET(obelisk_rt_design_bytecode_entry_v1, function, 8);
ABI_OFFSET(obelisk_rt_design_bytecode_entry_v1, reserved, 12);
ABI_SIZE_ALIGN(obelisk_rt_design_bytecode_header_v1, 208, 8);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, magic, 0);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, version, 8);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, reserved, 12);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, header_size, 16);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, flags, 20);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, image_size, 24);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, checksum, 32);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, function_offset, 40);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, function_count, 48);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, layout_offset, 56);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, layout_count, 64);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, code_offset, 72);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, instruction_count, 80);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, operand_offset, 88);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, operand_count, 96);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, constant_offset, 104);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, constant_size, 112);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, continuation_offset, 120);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, continuation_count, 128);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, intrinsic_offset, 136);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, intrinsic_count, 144);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, site_offset, 152);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, site_count, 160);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, state_offset, 168);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, state_count, 176);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, connectivity_offset, 184);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, connectivity_count, 192);
ABI_OFFSET(obelisk_rt_design_bytecode_header_v1, tail_reserved, 200);
ABI_SIZE_ALIGN(obelisk_rt_design_database_header_v1, 128, 8);
ABI_OFFSET(obelisk_rt_design_database_header_v1, magic, 0);
ABI_OFFSET(obelisk_rt_design_database_header_v1, version, 8);
ABI_OFFSET(obelisk_rt_design_database_header_v1, reserved, 12);
ABI_OFFSET(obelisk_rt_design_database_header_v1, profile, 16);
ABI_OFFSET(obelisk_rt_design_database_header_v1, header_size, 20);
ABI_OFFSET(obelisk_rt_design_database_header_v1, image_size, 24);
ABI_OFFSET(obelisk_rt_design_database_header_v1, checksum, 32);
ABI_OFFSET(obelisk_rt_design_database_header_v1, root_offset, 40);
ABI_OFFSET(obelisk_rt_design_database_header_v1, scope_offset, 48);
ABI_OFFSET(obelisk_rt_design_database_header_v1, scope_count, 56);
ABI_OFFSET(obelisk_rt_design_database_header_v1, object_offset, 64);
ABI_OFFSET(obelisk_rt_design_database_header_v1, object_count, 72);
ABI_OFFSET(obelisk_rt_design_database_header_v1, type_offset, 80);
ABI_OFFSET(obelisk_rt_design_database_header_v1, type_count, 88);
ABI_OFFSET(obelisk_rt_design_database_header_v1, string_offset, 96);
ABI_OFFSET(obelisk_rt_design_database_header_v1, string_size, 104);
ABI_OFFSET(obelisk_rt_design_database_header_v1, index_offset, 112);
ABI_OFFSET(obelisk_rt_design_database_header_v1, index_count, 120);
ABI_SIZE_ALIGN(obelisk_rt_design_cursor_v1, 8, 8);
ABI_SIZE_ALIGN(obelisk_rt_design_info_v1, 56, 8);
ABI_SIZE_ALIGN(obelisk_rt_import_input_v1, 32, 8);
ABI_OFFSET(obelisk_rt_import_input_v1, kind, 0);
ABI_OFFSET(obelisk_rt_import_input_v1, flags, 1);
ABI_OFFSET(obelisk_rt_import_input_v1, reserved, 2);
ABI_OFFSET(obelisk_rt_import_input_v1, bit_width, 4);
ABI_OFFSET(obelisk_rt_import_input_v1, value, 8);
ABI_OFFSET(obelisk_rt_import_input_v1, unknown, 16);
ABI_OFFSET(obelisk_rt_import_input_v1, limb_count, 24);
ABI_SIZE_ALIGN(obelisk_rt_import_output_v1, 32, 8);
ABI_OFFSET(obelisk_rt_import_output_v1, kind, 0);
ABI_OFFSET(obelisk_rt_import_output_v1, flags, 1);
ABI_OFFSET(obelisk_rt_import_output_v1, reserved, 2);
ABI_OFFSET(obelisk_rt_import_output_v1, bit_width, 4);
ABI_OFFSET(obelisk_rt_import_output_v1, value, 8);
ABI_OFFSET(obelisk_rt_import_output_v1, unknown, 16);
ABI_OFFSET(obelisk_rt_import_output_v1, limb_count, 24);
ABI_SIZE_ALIGN(obelisk_rt_import_site_v1, 56, 8);
ABI_OFFSET(obelisk_rt_import_site_v1, version, 0);
ABI_OFFSET(obelisk_rt_import_site_v1, flags, 4);
ABI_OFFSET(obelisk_rt_import_site_v1, import_id, 8);
ABI_OFFSET(obelisk_rt_import_site_v1, reserved, 12);
ABI_OFFSET(obelisk_rt_import_site_v1, scope_id, 16);
ABI_OFFSET(obelisk_rt_import_site_v1, source_file, 24);
ABI_OFFSET(obelisk_rt_import_site_v1, source_file_size, 32);
ABI_OFFSET(obelisk_rt_import_site_v1, source_line, 40);
ABI_OFFSET(obelisk_rt_import_site_v1, source_column, 44);
ABI_OFFSET(obelisk_rt_import_site_v1, abi_signature, 48);

ABI_SIZE_ALIGN(obelisk_rt_fragment_action_v1, 32, 8);
ABI_OFFSET(obelisk_rt_fragment_action_v1, kind, 0);
ABI_OFFSET(obelisk_rt_fragment_action_v1, suspend_kind, 4);
ABI_OFFSET(obelisk_rt_fragment_action_v1, continuation, 8);
ABI_OFFSET(obelisk_rt_fragment_action_v1, flags, 12);
ABI_OFFSET(obelisk_rt_fragment_action_v1, payload, 16);
ABI_OFFSET(obelisk_rt_fragment_action_v1, auxiliary, 24);

ABI_SIZE_ALIGN(obelisk_rt_bytecode_entry_v1, 8, 4);
ABI_OFFSET(obelisk_rt_bytecode_entry_v1, continuation, 0);
ABI_OFFSET(obelisk_rt_bytecode_entry_v1, instruction, 4);
ABI_SIZE_ALIGN(obelisk_rt_bytecode_validation_v1, 8, 4);
ABI_OFFSET(obelisk_rt_bytecode_validation_v1, state, 0);
ABI_OFFSET(obelisk_rt_bytecode_validation_v1, reserved, 4);

ABI_SIZE_ALIGN(obelisk_rt_bytecode_operand_v1, 32, 8);
ABI_OFFSET(obelisk_rt_bytecode_operand_v1, kind, 0);
ABI_OFFSET(obelisk_rt_bytecode_operand_v1, direction, 1);
ABI_OFFSET(obelisk_rt_bytecode_operand_v1, value_kind, 2);
ABI_OFFSET(obelisk_rt_bytecode_operand_v1, flags, 3);
ABI_OFFSET(obelisk_rt_bytecode_operand_v1, reserved, 4);
ABI_OFFSET(obelisk_rt_bytecode_operand_v1, value, 8);
ABI_OFFSET(obelisk_rt_bytecode_operand_v1, size, 16);
ABI_OFFSET(obelisk_rt_bytecode_operand_v1, auxiliary, 24);

ABI_SIZE_ALIGN(obelisk_rt_bytecode_service_site_v1, 16, 4);
ABI_OFFSET(obelisk_rt_bytecode_service_site_v1, service, 0);
ABI_OFFSET(obelisk_rt_bytecode_service_site_v1, first_operand, 4);
ABI_OFFSET(obelisk_rt_bytecode_service_site_v1, operand_count, 8);
ABI_OFFSET(obelisk_rt_bytecode_service_site_v1, flags, 10);
ABI_OFFSET(obelisk_rt_bytecode_service_site_v1, reserved, 12);

ABI_SIZE_ALIGN(obelisk_rt_bytecode_v1, 96, 8);
ABI_OFFSET(obelisk_rt_bytecode_v1, code, 0);
ABI_OFFSET(obelisk_rt_bytecode_v1, code_size, 8);
ABI_OFFSET(obelisk_rt_bytecode_v1, entries, 16);
ABI_OFFSET(obelisk_rt_bytecode_v1, entry_count, 24);
ABI_OFFSET(obelisk_rt_bytecode_v1, register_count, 28);
ABI_OFFSET(obelisk_rt_bytecode_v1, register_offset, 32);
ABI_OFFSET(obelisk_rt_bytecode_v1, validation, 40);
ABI_OFFSET(obelisk_rt_bytecode_v1, constants, 48);
ABI_OFFSET(obelisk_rt_bytecode_v1, constant_size, 56);
ABI_OFFSET(obelisk_rt_bytecode_v1, service_sites, 64);
ABI_OFFSET(obelisk_rt_bytecode_v1, service_site_count, 72);
ABI_OFFSET(obelisk_rt_bytecode_v1, reserved, 76);
ABI_OFFSET(obelisk_rt_bytecode_v1, operands, 80);
ABI_OFFSET(obelisk_rt_bytecode_v1, operand_count, 88);

ABI_SIZE_ALIGN(obelisk_rt_arg_v1, 32, 8);
ABI_OFFSET(obelisk_rt_arg_v1, kind, 0);
ABI_OFFSET(obelisk_rt_arg_v1, flags, 4);
ABI_OFFSET(obelisk_rt_arg_v1, size, 8);
ABI_OFFSET(obelisk_rt_arg_v1, data, 16);
ABI_OFFSET(obelisk_rt_arg_v1, unknown, 24);

ABI_SIZE_ALIGN(obelisk_rt_format_env_v1, 64, 8);
ABI_OFFSET(obelisk_rt_format_env_v1, scope, 0);
ABI_OFFSET(obelisk_rt_format_env_v1, scope_size, 8);
ABI_OFFSET(obelisk_rt_format_env_v1, library_cell, 16);
ABI_OFFSET(obelisk_rt_format_env_v1, library_cell_size, 24);
ABI_OFFSET(obelisk_rt_format_env_v1, time_width, 32);
ABI_OFFSET(obelisk_rt_format_env_v1, time_precision, 36);
ABI_OFFSET(obelisk_rt_format_env_v1, time_suffix, 40);
ABI_OFFSET(obelisk_rt_format_env_v1, time_suffix_size, 48);
ABI_OFFSET(obelisk_rt_format_env_v1, time_multiplier, 56);

ABI_SIZE_ALIGN(obelisk_rt_fragment_descriptor_v1, 120, 8);
ABI_OFFSET(obelisk_rt_fragment_descriptor_v1, handle, 0);
ABI_OFFSET(obelisk_rt_fragment_descriptor_v1, code_kind, 16);
ABI_OFFSET(obelisk_rt_fragment_descriptor_v1, flags, 20);
ABI_OFFSET(obelisk_rt_fragment_descriptor_v1, code, 24);

ABI_SIZE_ALIGN(obelisk_rt_frame_field_v1, 32, 8);
ABI_OFFSET(obelisk_rt_frame_field_v1, kind, 0);
ABI_OFFSET(obelisk_rt_frame_field_v1, flags, 4);
ABI_OFFSET(obelisk_rt_frame_field_v1, offset, 8);
ABI_OFFSET(obelisk_rt_frame_field_v1, size, 16);
ABI_OFFSET(obelisk_rt_frame_field_v1, alignment, 24);
ABI_OFFSET(obelisk_rt_frame_field_v1, reserved, 28);
ABI_SIZE_ALIGN(obelisk_rt_frame_layout_v1, 56, 8);
ABI_OFFSET(obelisk_rt_frame_layout_v1, version, 0);
ABI_OFFSET(obelisk_rt_frame_layout_v1, flags, 4);
ABI_OFFSET(obelisk_rt_frame_layout_v1, frame_size, 8);
ABI_OFFSET(obelisk_rt_frame_layout_v1, frame_alignment, 16);
ABI_OFFSET(obelisk_rt_frame_layout_v1, fields, 24);
ABI_OFFSET(obelisk_rt_frame_layout_v1, field_count, 32);
ABI_OFFSET(obelisk_rt_frame_layout_v1, continuation_count, 36);
ABI_OFFSET(obelisk_rt_frame_layout_v1, continuations, 40);
ABI_OFFSET(obelisk_rt_frame_layout_v1, checksum, 48);
ABI_SIZE_ALIGN(obelisk_rt_wait_record_v1, 32, 8);
ABI_OFFSET(obelisk_rt_wait_record_v1, version, 0);
ABI_OFFSET(obelisk_rt_wait_record_v1, kind, 4);
ABI_OFFSET(obelisk_rt_wait_record_v1, flags, 8);
ABI_OFFSET(obelisk_rt_wait_record_v1, count, 12);
ABI_OFFSET(obelisk_rt_wait_record_v1, payload, 16);
ABI_OFFSET(obelisk_rt_wait_record_v1, auxiliary, 24);
ABI_SIZE_ALIGN(obelisk_rt_wait_entry_v1, 16, 8);
ABI_OFFSET(obelisk_rt_wait_entry_v1, stable_id, 0);
ABI_OFFSET(obelisk_rt_wait_entry_v1, edge, 8);
ABI_OFFSET(obelisk_rt_wait_entry_v1, reserved, 12);
ABI_SIZE_ALIGN(obelisk_rt_computed_wait_record_v1, 96, 8);
ABI_OFFSET(obelisk_rt_computed_wait_record_v1, version, 0);
ABI_OFFSET(obelisk_rt_computed_wait_record_v1, kind, 4);
ABI_OFFSET(obelisk_rt_computed_wait_record_v1, flags, 8);
ABI_OFFSET(obelisk_rt_computed_wait_record_v1, clause_count, 12);
ABI_OFFSET(obelisk_rt_computed_wait_record_v1, observer_count, 16);
ABI_OFFSET(obelisk_rt_computed_wait_record_v1, capture_count, 20);
ABI_OFFSET(obelisk_rt_computed_wait_record_v1, dependency_count, 24);
ABI_OFFSET(obelisk_rt_computed_wait_record_v1, previous_limb_count, 28);
ABI_OFFSET(obelisk_rt_computed_wait_record_v1, observers_offset, 32);
ABI_OFFSET(obelisk_rt_computed_wait_record_v1, captures_offset, 40);
ABI_OFFSET(obelisk_rt_computed_wait_record_v1, dependencies_offset, 48);
ABI_OFFSET(obelisk_rt_computed_wait_record_v1, clauses_offset, 56);
ABI_OFFSET(obelisk_rt_computed_wait_record_v1, previous_value_offset, 64);
ABI_OFFSET(obelisk_rt_computed_wait_record_v1, previous_unknown_offset, 72);
ABI_OFFSET(obelisk_rt_computed_wait_record_v1, total_size, 80);
ABI_OFFSET(obelisk_rt_computed_wait_record_v1, reserved, 88);
ABI_SIZE_ALIGN(obelisk_rt_computed_observer_v1, 32, 8);
ABI_SIZE_ALIGN(obelisk_rt_computed_capture_v1, 32, 8);
ABI_SIZE_ALIGN(obelisk_rt_computed_dependency_v1, 16, 8);
ABI_SIZE_ALIGN(obelisk_rt_computed_clause_v1, 16, 4);
ABI_SIZE_ALIGN(obelisk_rt_process_descriptor_v1, 88, 8);
ABI_OFFSET(obelisk_rt_process_descriptor_v1, handle, 0);
ABI_OFFSET(obelisk_rt_process_descriptor_v1, version, 16);
ABI_OFFSET(obelisk_rt_process_descriptor_v1, flags, 20);
ABI_OFFSET(obelisk_rt_process_descriptor_v1, available_tiers, 24);
ABI_OFFSET(obelisk_rt_process_descriptor_v1, reserved, 28);
ABI_OFFSET(obelisk_rt_process_descriptor_v1, frame_layout, 32);
ABI_OFFSET(obelisk_rt_process_descriptor_v1, native_requirements, 40);
ABI_OFFSET(obelisk_rt_process_descriptor_v1, native_execute, 48);
ABI_OFFSET(obelisk_rt_process_descriptor_v1, native_destroy, 56);
ABI_OFFSET(obelisk_rt_process_descriptor_v1, bytecode, 64);
ABI_OFFSET(obelisk_rt_process_descriptor_v1, execution, 72);
ABI_OFFSET(obelisk_rt_process_descriptor_v1, design_bytecode, 80);
ABI_SIZE_ALIGN(obelisk_rt_process_instance_v1, 104, 8);
ABI_OFFSET(obelisk_rt_process_instance_v1, descriptor, 0);
ABI_OFFSET(obelisk_rt_process_instance_v1, allocation, 8);
ABI_OFFSET(obelisk_rt_process_instance_v1, frame, 16);
ABI_OFFSET(obelisk_rt_process_instance_v1, frame_size, 24);
ABI_OFFSET(obelisk_rt_process_instance_v1, scratch_offset, 32);
ABI_OFFSET(obelisk_rt_process_instance_v1, scratch_size, 40);
ABI_OFFSET(obelisk_rt_process_instance_v1, native_handle, 48);
ABI_OFFSET(obelisk_rt_process_instance_v1, continuation, 56);
ABI_OFFSET(obelisk_rt_process_instance_v1, tier, 60);
ABI_OFFSET(obelisk_rt_process_instance_v1, lifecycle, 64);
ABI_OFFSET(obelisk_rt_process_instance_v1, status, 68);
ABI_OFFSET(obelisk_rt_process_instance_v1, context, 72);
ABI_OFFSET(obelisk_rt_process_instance_v1, action, 80);
ABI_OFFSET(obelisk_rt_process_instance_v1, ownership_context, 88);
ABI_OFFSET(obelisk_rt_process_instance_v1, observer_pin_count, 96);
ABI_OFFSET(obelisk_rt_process_instance_v1, observer_destroy_pending, 100);

ABI_SIZE_ALIGN(obelisk_rt_aot_deopt_actor, 88, 8);
ABI_OFFSET(obelisk_rt_aot_deopt_actor, slot, 0);
ABI_OFFSET(obelisk_rt_aot_deopt_actor, flags, 4);
ABI_OFFSET(obelisk_rt_aot_deopt_actor, schedule_rank, 8);
ABI_OFFSET(obelisk_rt_aot_deopt_actor, queued_region, 12);
ABI_OFFSET(obelisk_rt_aot_deopt_actor, insertion_sequence, 16);
ABI_OFFSET(obelisk_rt_aot_deopt_actor, wake_time, 24);
ABI_OFFSET(obelisk_rt_aot_deopt_actor, wait_offset, 32);
ABI_OFFSET(obelisk_rt_aot_deopt_actor, wait_size, 40);
ABI_OFFSET(obelisk_rt_aot_deopt_actor, action, 48);
ABI_OFFSET(obelisk_rt_aot_deopt_actor, started, 80);
ABI_OFFSET(obelisk_rt_aot_deopt_actor, ready, 84);
ABI_SIZE_ALIGN(obelisk_rt_aot_deopt_nba, 24, 8);
ABI_OFFSET(obelisk_rt_aot_deopt_nba, slot, 0);
ABI_OFFSET(obelisk_rt_aot_deopt_nba, exec_region, 4);
ABI_OFFSET(obelisk_rt_aot_deopt_nba, sequence, 8);
ABI_OFFSET(obelisk_rt_aot_deopt_nba, due_time, 16);
ABI_SIZE_ALIGN(obelisk_rt_aot_deopt_snapshot, 56, 8);
ABI_OFFSET(obelisk_rt_aot_deopt_snapshot, size, 0);
ABI_OFFSET(obelisk_rt_aot_deopt_snapshot, current_time, 8);
ABI_OFFSET(obelisk_rt_aot_deopt_snapshot, actors, 16);
ABI_OFFSET(obelisk_rt_aot_deopt_snapshot, actor_count, 24);
ABI_OFFSET(obelisk_rt_aot_deopt_snapshot, ready_count, 28);
ABI_OFFSET(obelisk_rt_aot_deopt_snapshot, nbas, 32);
ABI_OFFSET(obelisk_rt_aot_deopt_snapshot, nba_count, 40);
ABI_OFFSET(obelisk_rt_aot_deopt_snapshot, reserved, 44);
ABI_OFFSET(obelisk_rt_aot_deopt_snapshot, next_sequence, 48);
ABI_SIZE_ALIGN(obelisk_rt_native_schedule_node, 12, 4);
ABI_OFFSET(obelisk_rt_native_schedule_node, actor_slot, 0);
ABI_OFFSET(obelisk_rt_native_schedule_node, continuation, 4);
ABI_OFFSET(obelisk_rt_native_schedule_node, fusion_group, 8);
ABI_SIZE_ALIGN(obelisk_rt_static_nba_storage, 4, 4);
ABI_SIZE_ALIGN(obelisk_rt_generated_nba_accumulator_256, 104, 8);
ABI_OFFSET(obelisk_rt_generated_nba_accumulator_256, value, 0);
ABI_OFFSET(obelisk_rt_generated_nba_accumulator_256, unknown, 32);
ABI_OFFSET(obelisk_rt_generated_nba_accumulator_256, write_mask, 64);
ABI_OFFSET(obelisk_rt_generated_nba_accumulator_256, valid, 96);
ABI_OFFSET(obelisk_rt_generated_nba_accumulator_256, exec_region, 100);
ABI_SIZE_ALIGN(obelisk_rt_static_nba_root, 24, 8);
ABI_OFFSET(obelisk_rt_static_nba_root, commit_node, 0);
ABI_OFFSET(obelisk_rt_static_nba_root, static_state, 4);
ABI_OFFSET(obelisk_rt_static_nba_root, bit_width, 8);
ABI_SIZE_ALIGN(obelisk_rt_static_nba_site, 16, 8);
ABI_OFFSET(obelisk_rt_static_nba_site, site, 0);
ABI_OFFSET(obelisk_rt_static_nba_site, root, 8);
ABI_OFFSET(obelisk_rt_static_nba_site, storage, 12);
ABI_SIZE_ALIGN(obelisk_rt_static_fanout_entry, 48, 8);
ABI_OFFSET(obelisk_rt_static_fanout_entry, static_state, 0);
ABI_OFFSET(obelisk_rt_static_fanout_entry, actor_slot, 4);
ABI_OFFSET(obelisk_rt_static_fanout_entry, continuation, 8);
ABI_OFFSET(obelisk_rt_static_fanout_entry, edge, 12);
ABI_OFFSET(obelisk_rt_static_fanout_entry, compute_node, 16);
ABI_OFFSET(obelisk_rt_static_fanout_entry, reserved, 20);
ABI_OFFSET(obelisk_rt_static_fanout_entry, low_bit, 24);
ABI_OFFSET(obelisk_rt_static_fanout_entry, bit_width, 32);
ABI_OFFSET(obelisk_rt_static_fanout_entry, kernel, 40);
ABI_OFFSET(obelisk_rt_static_fanout_entry, merged_bit, 44);
ABI_SIZE_ALIGN(obelisk_rt_native_clock_kernel, 48, 8);
ABI_OFFSET(obelisk_rt_native_clock_kernel, static_state, 0);
ABI_OFFSET(obelisk_rt_native_clock_kernel, edge, 4);
ABI_OFFSET(obelisk_rt_native_clock_kernel, low_bit, 8);
ABI_OFFSET(obelisk_rt_native_clock_kernel, bit_width, 16);
ABI_OFFSET(obelisk_rt_native_clock_kernel, ingress_mask, 24);
ABI_OFFSET(obelisk_rt_native_clock_kernel, ingress_word_count, 32);
ABI_OFFSET(obelisk_rt_native_clock_kernel, reserved, 36);
ABI_OFFSET(obelisk_rt_native_clock_kernel, active_mask, 40);
ABI_SIZE_ALIGN(obelisk_rt_native_merged_fragment, 32, 8);
ABI_OFFSET(obelisk_rt_native_merged_fragment, actor_slot, 0);
ABI_OFFSET(obelisk_rt_native_merged_fragment, continuation, 4);
ABI_OFFSET(obelisk_rt_native_merged_fragment, kernel, 8);
ABI_OFFSET(obelisk_rt_native_merged_fragment, bit, 12);
ABI_OFFSET(obelisk_rt_native_merged_fragment, compute_node, 16);
ABI_OFFSET(obelisk_rt_native_merged_fragment, flags, 20);
ABI_OFFSET(obelisk_rt_native_merged_fragment, execute, 24);
ABI_SIZE_ALIGN(obelisk_rt_static_actor_root, 16, 4);
ABI_OFFSET(obelisk_rt_static_actor_root, actor_slot, 0);
ABI_OFFSET(obelisk_rt_static_actor_root, static_state, 4);
ABI_OFFSET(obelisk_rt_static_actor_root, flags, 8);
ABI_OFFSET(obelisk_rt_static_actor_root, reserved, 12);
ABI_SIZE_ALIGN(obelisk_rt_native_schedule_plan, 256, 8);
ABI_OFFSET(obelisk_rt_native_schedule_plan, size, 0);
ABI_OFFSET(obelisk_rt_native_schedule_plan, graph_layout_checksum, 8);
ABI_OFFSET(obelisk_rt_native_schedule_plan, mutable_state, 16);
ABI_OFFSET(obelisk_rt_native_schedule_plan, mutable_state_size, 24);
ABI_OFFSET(obelisk_rt_native_schedule_plan, actor_capacity, 32);
ABI_OFFSET(obelisk_rt_native_schedule_plan, flags, 36);
ABI_OFFSET(obelisk_rt_native_schedule_plan, state_value, 40);
ABI_OFFSET(obelisk_rt_native_schedule_plan, state_unknown, 48);
ABI_OFFSET(obelisk_rt_native_schedule_plan, state_bit_count, 56);
ABI_OFFSET(obelisk_rt_native_schedule_plan, bind, 64);
ABI_OFFSET(obelisk_rt_native_schedule_plan, run, 72);
ABI_OFFSET(obelisk_rt_native_schedule_plan, fallback_snapshot, 80);
ABI_OFFSET(obelisk_rt_native_schedule_plan, nba_roots, 88);
ABI_OFFSET(obelisk_rt_native_schedule_plan, nba_root_count, 96);
ABI_OFFSET(obelisk_rt_native_schedule_plan, nba_reserved, 100);
ABI_OFFSET(obelisk_rt_native_schedule_plan, nba_sites, 104);
ABI_OFFSET(obelisk_rt_native_schedule_plan, nba_site_count, 112);
ABI_OFFSET(obelisk_rt_native_schedule_plan, fanout_entries, 120);
ABI_OFFSET(obelisk_rt_native_schedule_plan, fanout_entry_count, 128);
ABI_OFFSET(obelisk_rt_native_schedule_plan, actor_roots, 136);
ABI_OFFSET(obelisk_rt_native_schedule_plan, actor_root_count, 144);
ABI_OFFSET(obelisk_rt_native_schedule_plan, nba_commit, 152);
ABI_OFFSET(obelisk_rt_native_schedule_plan, specialization_fast, 160);
ABI_OFFSET(obelisk_rt_native_schedule_plan, nba_dirty_roots, 168);
ABI_OFFSET(obelisk_rt_native_schedule_plan, nba_dirty_word_count, 176);
ABI_OFFSET(obelisk_rt_native_schedule_plan, nba_dirty_reserved, 180);
ABI_OFFSET(obelisk_rt_native_schedule_plan, nba_dirty_summary, 184);
ABI_OFFSET(obelisk_rt_native_schedule_plan, nba_dirty_summary_word_count, 192);
ABI_OFFSET(obelisk_rt_native_schedule_plan, nba_dirty_summary_reserved, 196);
ABI_OFFSET(obelisk_rt_native_schedule_plan, clock_kernels, 200);
ABI_OFFSET(obelisk_rt_native_schedule_plan, clock_kernel_count, 208);
ABI_OFFSET(obelisk_rt_native_schedule_plan, clock_kernel_reserved, 212);
ABI_OFFSET(obelisk_rt_native_schedule_plan, merged_fragments, 216);
ABI_OFFSET(obelisk_rt_native_schedule_plan, merged_fragment_count, 224);
ABI_OFFSET(obelisk_rt_native_schedule_plan, timeslot_coordinator, 232);
ABI_OFFSET(obelisk_rt_native_schedule_plan, promotion_invalidate, 240);
ABI_OFFSET(obelisk_rt_native_schedule_plan, promotion_ready, 248);

#undef ABI_OFFSET
#undef ABI_SIZE_ALIGN

static_assert(OBELISK_RT_VERSION == 1);
static_assert(OBELISK_RT_BYTECODE_INSTRUCTION_SIZE == 16);
static_assert(OBELISK_RT_BYTECODE_REGISTER_SIZE == 16);
static_assert(OBELISK_RT_OK == 0);
static_assert(OBELISK_RT_EOF == 1);
static_assert(OBELISK_RT_INVALID_ARGUMENT == 2);
static_assert(OBELISK_RT_INVALID_HANDLE == 3);
static_assert(OBELISK_RT_IO_ERROR == 4);
static_assert(OBELISK_RT_OUT_OF_MEMORY == 5);
static_assert(OBELISK_RT_OUT_OF_RESOURCES == 6);
static_assert(OBELISK_RT_FORMAT_ERROR == 7);
static_assert(OBELISK_RT_ARGUMENT_MISMATCH == 8);
static_assert(OBELISK_RT_INVALID_BYTECODE == 9);
static_assert(OBELISK_RT_STEP_LIMIT == 10);
static_assert(OBELISK_RT_LAYOUT_MISMATCH == 11);
static_assert(OBELISK_RT_INVALID_CONTINUATION == 12);
static_assert(OBELISK_RT_TIER_UNAVAILABLE == 13);
static_assert(OBELISK_RT_AOT_CHECKPOINT == 20);
static_assert(OBELISK_RT_AOT_TIMED_CHECKPOINT == 21);
static_assert(OBELISK_RT_AOT_GENERATED_CHECKPOINT == 22);
static_assert(OBELISK_RT_INVALID_LIFECYCLE == 14);
static_assert(OBELISK_RT_INVALID_FRAME == 15);
static_assert(OBELISK_RT_INVALID_DESIGN == 16);
static_assert(OBELISK_RT_PERMISSION_DENIED == 17);
static_assert(OBELISK_RT_DPI_DISABLE_UNSUPPORTED == 18);
static_assert(OBELISK_RT_FATAL == 19);
static_assert(OBELISK_RT_DESCRIPTOR_INVALID == 0);
static_assert(OBELISK_RT_DESCRIPTOR_SCOPE == 1);
static_assert(OBELISK_RT_DESCRIPTOR_STORAGE == 2);
static_assert(OBELISK_RT_DESCRIPTOR_NET == 3);
static_assert(OBELISK_RT_DESCRIPTOR_DRIVER == 4);
static_assert(OBELISK_RT_DESCRIPTOR_EVENT == 5);
static_assert(OBELISK_RT_DESCRIPTOR_PROCESS == 6);
static_assert(OBELISK_RT_DESCRIPTOR_FRAGMENT == 7);
static_assert(OBELISK_RT_DESCRIPTOR_FUNCTION == 8);
static_assert(OBELISK_RT_DESCRIPTOR_OBSERVER == 9);
static_assert(OBELISK_RT_FRAGMENT_CONTINUE == 0);
static_assert(OBELISK_RT_FRAGMENT_SUSPEND == 1);
static_assert(OBELISK_RT_FRAGMENT_TERMINATE == 2);
static_assert(OBELISK_RT_FRAGMENT_TASK_CALL == 3);
static_assert(OBELISK_RT_SUSPEND_NONE == 0);
static_assert(OBELISK_RT_SUSPEND_DELAY == 1);
static_assert(OBELISK_RT_SUSPEND_CHANGE == 2);
static_assert(OBELISK_RT_SUSPEND_EDGE == 3);
static_assert(OBELISK_RT_SUSPEND_EVENT == 4);
static_assert(OBELISK_RT_SUSPEND_AWAIT == 5);
static_assert(OBELISK_RT_SUSPEND_JOIN == 6);
static_assert(OBELISK_RT_SUSPEND_FOREVER == 7);
static_assert(OBELISK_RT_SUSPEND_FRONTIER == 8);
static_assert(OBELISK_RT_SUSPEND_CHILDREN == 9);
static_assert(OBELISK_RT_SUSPEND_OBSERVER == 10);
static_assert(OBELISK_RT_ACTION_FRAME_WAIT_RECORD == 1);
static_assert(OBELISK_RT_FRAME_CAPTURE == 1);
static_assert(OBELISK_RT_FRAME_CONTINUATION == 2);
static_assert(OBELISK_RT_FRAME_LIVE == 3);
static_assert(OBELISK_RT_FRAME_WAIT == 4);
static_assert(OBELISK_RT_FRAME_FIELD_FLAGS_NONE == 0);
static_assert(OBELISK_RT_FRAME_FOUR_STATE_VALUE == 1);
static_assert(OBELISK_RT_FRAME_FOUR_STATE_UNKNOWN == 2);
static_assert(OBELISK_RT_WAIT_EDGE_CHANGE == 0);
static_assert(OBELISK_RT_WAIT_EDGE_POSEDGE == 1);
static_assert(OBELISK_RT_WAIT_EDGE_NEGEDGE == 2);
static_assert(OBELISK_RT_WAIT_EDGE_BOTH == 3);
static_assert(OBELISK_RT_WAIT_EDGE_NONE == UINT32_MAX);
static_assert(OBELISK_RT_TIER_NATIVE == 1);
static_assert(OBELISK_RT_TIER_BYTECODE == 2);
static_assert(OBELISK_RT_TIER_MASK_NATIVE == 1);
static_assert(OBELISK_RT_TIER_MASK_BYTECODE == 2);
static_assert(OBELISK_RT_PROCESS_READY == 0);
static_assert(OBELISK_RT_PROCESS_EXECUTING == 1);
static_assert(OBELISK_RT_PROCESS_SUSPENDED == 2);
static_assert(OBELISK_RT_PROCESS_TERMINATED == 3);
static_assert(OBELISK_RT_FRAGMENT_NATIVE == 0);
static_assert(OBELISK_RT_FRAGMENT_BYTECODE == 1);
static_assert(OBELISK_RT_BC_TYPE_NONE == 0);
static_assert(OBELISK_RT_BC_TYPE_U64 == 1);
static_assert(OBELISK_RT_BC_TYPE_I64 == 2);
static_assert(OBELISK_RT_BC_TYPE_BOOL == 3);
static_assert(OBELISK_RT_BC_TYPE_STATUS == 4);
static_assert(OBELISK_RT_BC_TYPE_RESOURCE == 5);
static_assert(OBELISK_RT_BC_VALIDATION_UNVALIDATED == 0);
static_assert(OBELISK_RT_BC_VALIDATION_VALID == 2);
static_assert(OBELISK_RT_BC_VALIDATION_INVALID == 3);
static_assert(OBELISK_RT_BC_NOP == 0);
static_assert(OBELISK_RT_BC_CONST == 1);
static_assert(OBELISK_RT_BC_MOVE == 2);
static_assert(OBELISK_RT_BC_ADD == 3);
static_assert(OBELISK_RT_BC_SUB == 4);
static_assert(OBELISK_RT_BC_MUL == 5);
static_assert(OBELISK_RT_BC_AND == 6);
static_assert(OBELISK_RT_BC_OR == 7);
static_assert(OBELISK_RT_BC_XOR == 8);
static_assert(OBELISK_RT_BC_NOT == 9);
static_assert(OBELISK_RT_BC_EQ == 10);
static_assert(OBELISK_RT_BC_ULT == 11);
static_assert(OBELISK_RT_BC_SLT == 12);
static_assert(OBELISK_RT_BC_LOAD_FRAME == 13);
static_assert(OBELISK_RT_BC_STORE_FRAME == 14);
static_assert(OBELISK_RT_BC_JUMP == 15);
static_assert(OBELISK_RT_BC_BRANCH_ZERO == 16);
static_assert(OBELISK_RT_BC_CONTINUE == 17);
static_assert(OBELISK_RT_BC_SUSPEND == 18);
static_assert(OBELISK_RT_BC_TERMINATE == 19);
static_assert(OBELISK_RT_BC_CALL_SERVICE == 20);
static_assert(OBELISK_RT_BC_FAIL == 21);
static_assert(OBELISK_RT_BC_OPERAND_IMMEDIATE == 0);
static_assert(OBELISK_RT_BC_OPERAND_REGISTER == 1);
static_assert(OBELISK_RT_BC_OPERAND_FRAME == 2);
static_assert(OBELISK_RT_BC_OPERAND_CONSTANT == 3);
static_assert(OBELISK_RT_BC_OPERAND_RESOURCE == 4);
static_assert(OBELISK_RT_BC_OPERAND_INPUT == 0);
static_assert(OBELISK_RT_BC_OPERAND_OUTPUT == 1);
static_assert(OBELISK_RT_BC_OPERAND_INOUT == 2);
static_assert(OBELISK_RT_BC_VALUE_NONE == 0);
static_assert(OBELISK_RT_BC_VALUE_U8 == 1);
static_assert(OBELISK_RT_BC_VALUE_U32 == 2);
static_assert(OBELISK_RT_BC_VALUE_I32 == 3);
static_assert(OBELISK_RT_BC_VALUE_U64 == 4);
static_assert(OBELISK_RT_BC_VALUE_I64 == 5);
static_assert(OBELISK_RT_BC_VALUE_BYTES == 6);
static_assert(OBELISK_RT_BC_VALUE_MUTABLE_BYTES == 7);
static_assert(OBELISK_RT_BC_VALUE_ARGUMENT_ARRAY == 8);
static_assert(OBELISK_RT_BC_VALUE_FORMAT_ENVIRONMENT == 9);
static_assert(OBELISK_RT_BC_VALUE_BUFFER == 10);
static_assert(OBELISK_RT_BC_VALUE_ARGUMENT_EMPTY == 11);
static_assert(OBELISK_RT_BC_VALUE_ARGUMENT_LOGIC == 12);
static_assert(OBELISK_RT_BC_VALUE_ARGUMENT_STRING == 13);
static_assert(OBELISK_RT_BC_VALUE_ARGUMENT_REAL == 14);
static_assert(OBELISK_RT_BC_VALUE_ARGUMENT_TIME == 15);
static_assert(OBELISK_RT_BC_SERVICE_FORMAT == 1);
static_assert(OBELISK_RT_BC_SERVICE_DISPLAY == 2);
static_assert(OBELISK_RT_BC_SERVICE_BUFFER_RELEASE == 3);
static_assert(OBELISK_RT_BC_SERVICE_FILE_OPEN_MCD == 10);
static_assert(OBELISK_RT_BC_SERVICE_FILE_OPEN == 11);
static_assert(OBELISK_RT_BC_SERVICE_FILE_CLOSE == 12);
static_assert(OBELISK_RT_BC_SERVICE_FILE_FLUSH == 13);
static_assert(OBELISK_RT_BC_SERVICE_FILE_WRITE == 14);
static_assert(OBELISK_RT_BC_SERVICE_FILE_READ == 15);
static_assert(OBELISK_RT_BC_SERVICE_FILE_GETC == 16);
static_assert(OBELISK_RT_BC_SERVICE_FILE_UNGETC == 17);
static_assert(OBELISK_RT_BC_SERVICE_FILE_GETLINE == 18);
static_assert(OBELISK_RT_BC_SERVICE_FILE_EOF == 19);
static_assert(OBELISK_RT_BC_SERVICE_FILE_ERROR == 20);
static_assert(OBELISK_RT_BC_SERVICE_FILE_SEEK == 21);
static_assert(OBELISK_RT_BC_SERVICE_FILE_TELL == 22);
static_assert(OBELISK_RT_BC_SERVICE_FILE_REWIND == 23);
static_assert(OBELISK_RT_ARG_EMPTY == 0);
static_assert(OBELISK_RT_ARG_LOGIC == 1);
static_assert(OBELISK_RT_ARG_STRING == 2);
static_assert(OBELISK_RT_ARG_REAL == 3);
static_assert(OBELISK_RT_ARG_TIME == 4);
static_assert(OBELISK_RT_ARG_MANAGED_STRING == 5);
static_assert(OBELISK_RT_ARG_MANAGED_CONTAINER == 6);
static_assert(OBELISK_RT_ARG_SIGNED == 1);
static_assert(OBELISK_RT_ARG_FORMAT_STRING == 2);
static_assert(OBELISK_RT_ARGUMENT_VALUE_BITS == 0);
static_assert(OBELISK_RT_ARGUMENT_VALUE_CLASS == 1);
static_assert(OBELISK_RT_ARGUMENT_VALUE_STRING == 2);
static_assert(OBELISK_RT_RADIX_BINARY == 2);
static_assert(OBELISK_RT_RADIX_OCTAL == 8);
static_assert(OBELISK_RT_RADIX_DECIMAL == 10);
static_assert(OBELISK_RT_RADIX_HEX == 16);
static_assert(OBELISK_RT_SEEK_SET == 0);
static_assert(OBELISK_RT_SEEK_CUR == 1);
static_assert(OBELISK_RT_SEEK_END == 2);

using NativeFragment = obelisk_rt_status (*)(obelisk_rt_context *, void *,
                                             uint64_t, uint32_t,
                                             obelisk_rt_fragment_action_v1 *);
static_assert(std::is_same_v<obelisk_rt_native_fragment_v1, NativeFragment>);

#define ABI_FUNCTION(Name, Type)                                               \
  static_assert(std::is_same_v<decltype(&Name), Type>,                         \
                #Name " signature changed")

ABI_FUNCTION(obelisk_rt_v1_context_create,
             obelisk_rt_status (*)(obelisk_rt_context **));
ABI_FUNCTION(obelisk_rt_v1_context_configure_argv,
             obelisk_rt_status (*)(obelisk_rt_context *, int,
                                   const char *const *));
ABI_FUNCTION(obelisk_rt_v1_context_seed,
             obelisk_rt_status (*)(obelisk_rt_context *, uint64_t));
ABI_FUNCTION(obelisk_rt_v1_random_next,
             obelisk_rt_status (*)(obelisk_rt_context *, uint64_t *));
ABI_FUNCTION(obelisk_rt_v1_random_seed,
             obelisk_rt_status (*)(obelisk_rt_context *, uint64_t));
ABI_FUNCTION(obelisk_rt_v1_random_bounded,
             obelisk_rt_status (*)(obelisk_rt_context *, uint64_t, uint64_t *));
ABI_FUNCTION(obelisk_rt_v1_random_solve,
             obelisk_rt_status (*)(obelisk_rt_context *, const uint8_t *,
                                   uint64_t, uint64_t, uint64_t,
                                   const uint64_t *, uint64_t, uint64_t *,
                                   uint32_t *));
ABI_FUNCTION(obelisk_rt_v1_random_get_state,
             obelisk_rt_status (*)(obelisk_rt_context *,
                                   obelisk_rt_random_state_v1 *));
ABI_FUNCTION(obelisk_rt_v1_random_set_state,
             obelisk_rt_status (*)(obelisk_rt_context *,
                                   const obelisk_rt_random_state_v1 *));
ABI_FUNCTION(obelisk_rt_v1_random_state_seed,
             void (*)(obelisk_rt_random_state_v1 *, uint64_t, uint64_t));
ABI_FUNCTION(obelisk_rt_v1_random_state_next32,
             uint32_t (*)(obelisk_rt_random_state_v1 *));
ABI_FUNCTION(obelisk_rt_v1_random_state_next64,
             uint64_t (*)(obelisk_rt_random_state_v1 *));
ABI_FUNCTION(obelisk_rt_v1_random_state_bounded,
             obelisk_rt_status (*)(obelisk_rt_random_state_v1 *, uint64_t,
                                   uint64_t *));
ABI_FUNCTION(obelisk_rt_v1_context_create_for_design,
             obelisk_rt_status (*)(const obelisk_rt_execution_descriptor_v1 *,
                                   obelisk_rt_context **));
ABI_FUNCTION(obelisk_rt_v1_import_id, uint32_t (*)(const uint8_t *, uint64_t));
ABI_FUNCTION(obelisk_rt_v1_context_register_import,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t,
                                   obelisk_rt_import_callback_v1, void *));
ABI_FUNCTION(obelisk_rt_v1_context_register_import_signature,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t, uint64_t,
                                   obelisk_rt_import_callback_v1, void *));
ABI_FUNCTION(obelisk_rt_v1_import_call,
             obelisk_rt_status (*)(obelisk_rt_context *,
                                   const obelisk_rt_import_site_v1 *,
                                   const obelisk_rt_import_input_v1 *, uint32_t,
                                   obelisk_rt_import_output_v1 *, uint32_t));
ABI_FUNCTION(obelisk_rt_v1_context_destroy, void (*)(obelisk_rt_context *));
ABI_FUNCTION(obelisk_rt_v1_status_string, const char *(*)(obelisk_rt_status));
ABI_FUNCTION(obelisk_rt_v1_buffer_release, void (*)(obelisk_rt_buffer_v1 *));
ABI_FUNCTION(obelisk_rt_v1_last_error,
             obelisk_rt_status (*)(obelisk_rt_context *,
                                   obelisk_rt_buffer_v1 *));
ABI_FUNCTION(obelisk_rt_v1_format,
             obelisk_rt_status (*)(obelisk_rt_context *, const char *, uint64_t,
                                   const obelisk_rt_arg_v1 *, uint64_t,
                                   const obelisk_rt_format_env_v1 *,
                                   obelisk_rt_buffer_v1 *));
ABI_FUNCTION(obelisk_rt_v1_display,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t, uint32_t,
                                   obelisk_rt_radix, const obelisk_rt_arg_v1 *,
                                   uint64_t, const obelisk_rt_format_env_v1 *));
ABI_FUNCTION(obelisk_rt_v1_monitor_register,
             obelisk_rt_status (*)(obelisk_rt_context *, uint64_t, uint32_t));
ABI_FUNCTION(obelisk_rt_v1_monitor_control,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t));
ABI_FUNCTION(obelisk_rt_v1_monitor_current, uint32_t (*)(obelisk_rt_context *));
ABI_FUNCTION(obelisk_rt_v1_scheduler_finish,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t));
ABI_FUNCTION(obelisk_rt_v1_scheduler_fatal,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t));
ABI_FUNCTION(obelisk_rt_v1_scheduler_termination_requested,
             uint32_t (*)(obelisk_rt_context *));
ABI_FUNCTION(obelisk_rt_v1_scheduler_time, uint64_t (*)(obelisk_rt_context *));
ABI_FUNCTION(obelisk_rt_v1_file_open_mcd,
             obelisk_rt_status (*)(obelisk_rt_context *, const char *, uint64_t,
                                   uint32_t *));
ABI_FUNCTION(obelisk_rt_v1_file_open,
             obelisk_rt_status (*)(obelisk_rt_context *, const char *, uint64_t,
                                   const char *, uint64_t, uint32_t *));
ABI_FUNCTION(obelisk_rt_v1_file_open_string_mcd,
             obelisk_rt_status (*)(obelisk_rt_context *, obelisk_rt_string_v1,
                                   uint32_t *));
ABI_FUNCTION(obelisk_rt_v1_file_open_string,
             obelisk_rt_status (*)(obelisk_rt_context *, obelisk_rt_string_v1,
                                   obelisk_rt_string_v1, uint32_t *));
ABI_FUNCTION(obelisk_rt_v1_file_close,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t));
ABI_FUNCTION(obelisk_rt_v1_file_flush,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t));
ABI_FUNCTION(obelisk_rt_v1_file_write,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t, const void *,
                                   uint64_t, uint64_t *));
ABI_FUNCTION(obelisk_rt_v1_file_read,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t, void *,
                                   uint64_t, uint64_t *));
ABI_FUNCTION(obelisk_rt_v1_file_getc,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t, uint8_t *));
ABI_FUNCTION(obelisk_rt_v1_file_ungetc,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t, uint8_t));
ABI_FUNCTION(obelisk_rt_v1_file_getline,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t, uint64_t,
                                   obelisk_rt_buffer_v1 *));
ABI_FUNCTION(obelisk_rt_v1_file_getline_string,
             obelisk_rt_status (*)(obelisk_rt_context *,
                                   obelisk_rt_gc_lane_v1 *, uint32_t,
                                   obelisk_rt_string_v1 *, uint32_t *));
ABI_FUNCTION(obelisk_rt_v1_file_eof,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t, uint32_t *));
ABI_FUNCTION(obelisk_rt_v1_file_error,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t, int32_t *,
                                   obelisk_rt_buffer_v1 *));
ABI_FUNCTION(obelisk_rt_v1_file_seek,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t, int64_t,
                                   obelisk_rt_seek_origin));
ABI_FUNCTION(obelisk_rt_v1_file_tell,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t, int64_t *));
ABI_FUNCTION(obelisk_rt_v1_file_rewind,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t));
ABI_FUNCTION(obelisk_rt_v1_fragment_execute,
             obelisk_rt_status (*)(const obelisk_rt_fragment_descriptor_v1 *,
                                   obelisk_rt_context *, void *, uint64_t,
                                   uint32_t, obelisk_rt_fragment_action_v1 *));
ABI_FUNCTION(obelisk_rt_v1_bytecode_execute_bounded,
             obelisk_rt_status (*)(const obelisk_rt_fragment_descriptor_v1 *,
                                   obelisk_rt_context *, void *, uint64_t,
                                   uint32_t, uint64_t,
                                   obelisk_rt_fragment_action_v1 *));
ABI_FUNCTION(obelisk_rt_v1_process_instance_create,
             obelisk_rt_status (*)(const obelisk_rt_process_descriptor_v1 *,
                                   obelisk_rt_process_instance_v1 **));
ABI_FUNCTION(obelisk_rt_v1_process_instance_frame,
             obelisk_rt_status (*)(obelisk_rt_process_instance_v1 *, void **,
                                   uint64_t *));
ABI_FUNCTION(obelisk_rt_v1_process_instance_execute,
             obelisk_rt_status (*)(obelisk_rt_process_instance_v1 *,
                                   obelisk_rt_context *,
                                   obelisk_rt_execution_tier,
                                   obelisk_rt_fragment_action_v1 *));
ABI_FUNCTION(obelisk_rt_v1_process_instance_destroy,
             obelisk_rt_status (*)(obelisk_rt_process_instance_v1 *));
ABI_FUNCTION(obelisk_rt_v1_design_validate,
             obelisk_rt_status (*)(const obelisk_rt_execution_descriptor_v1 *));
ABI_FUNCTION(obelisk_rt_v1_design_root,
             obelisk_rt_status (*)(const obelisk_rt_execution_descriptor_v1 *,
                                   obelisk_rt_design_cursor_v1 *));
ABI_FUNCTION(obelisk_rt_v1_design_child,
             obelisk_rt_status (*)(const obelisk_rt_execution_descriptor_v1 *,
                                   obelisk_rt_design_cursor_v1,
                                   obelisk_rt_design_cursor_v1 *));
ABI_FUNCTION(obelisk_rt_v1_design_sibling,
             obelisk_rt_status (*)(const obelisk_rt_execution_descriptor_v1 *,
                                   obelisk_rt_design_cursor_v1,
                                   obelisk_rt_design_cursor_v1 *));
ABI_FUNCTION(obelisk_rt_v1_design_lookup,
             obelisk_rt_status (*)(const obelisk_rt_execution_descriptor_v1 *,
                                   const uint8_t *, uint64_t,
                                   obelisk_rt_design_cursor_v1 *));
ABI_FUNCTION(obelisk_rt_v1_design_info,
             obelisk_rt_status (*)(const obelisk_rt_execution_descriptor_v1 *,
                                   obelisk_rt_design_cursor_v1,
                                   obelisk_rt_design_info_v1 *));
ABI_FUNCTION(obelisk_rt_v1_design_name,
             obelisk_rt_status (*)(const obelisk_rt_execution_descriptor_v1 *,
                                   obelisk_rt_design_cursor_v1,
                                   const uint8_t **, uint64_t *));
ABI_FUNCTION(obelisk_rt_v1_design_read,
             obelisk_rt_status (*)(obelisk_rt_context *,
                                   obelisk_rt_design_cursor_v1, uint64_t *,
                                   uint64_t *, uint64_t));
ABI_FUNCTION(obelisk_rt_v1_design_write,
             obelisk_rt_status (*)(obelisk_rt_context *,
                                   obelisk_rt_design_cursor_v1,
                                   const uint64_t *, const uint64_t *,
                                   uint64_t));
ABI_FUNCTION(obelisk_rt_v1_scheduler_add,
             obelisk_rt_status (*)(obelisk_rt_context *,
                                   obelisk_rt_process_instance_v1 *, uint32_t));
ABI_FUNCTION(obelisk_rt_v1_scheduler_add_ranked,
             obelisk_rt_status (*)(obelisk_rt_context *,
                                   obelisk_rt_process_instance_v1 *, uint32_t,
                                   uint32_t));
ABI_FUNCTION(obelisk_rt_v1_scheduler_add_planned,
             obelisk_rt_status (*)(obelisk_rt_context *,
                                   obelisk_rt_process_instance_v1 *, uint32_t,
                                   uint32_t, const uint32_t *, const uint32_t *,
                                   uint32_t));
ABI_FUNCTION(obelisk_rt_v1_scheduler_install_aot,
             obelisk_rt_status (*)(obelisk_rt_context *,
                                   const obelisk_rt_native_schedule_plan *));
ABI_FUNCTION(obelisk_rt_v1_scheduler_activate_clock_kernel,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t,
                                   uint32_t));
ABI_FUNCTION(obelisk_rt_v1_scheduler_run_clock_coordinator,
             obelisk_rt_status (*)(obelisk_rt_context *));
ABI_FUNCTION(obelisk_rt_v1_scheduler_direct_fragment_enter,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t, uint32_t,
                                   obelisk_rt_process_instance_v1 **));
ABI_FUNCTION(obelisk_rt_v1_scheduler_direct_fragment_leave,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t));
ABI_FUNCTION(obelisk_rt_v1_scheduler_execute_aot_actor,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t));
ABI_FUNCTION(obelisk_rt_v1_scheduler_queue_aot_checkpoint,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t,
                                   uint32_t,
                                   obelisk_rt_native_checkpoint_callback));
ABI_FUNCTION(obelisk_rt_v1_scheduler_add_aot,
             obelisk_rt_status (*)(obelisk_rt_context *,
                                   obelisk_rt_process_instance_v1 *, uint32_t,
                                   uint32_t, uint32_t, const uint32_t *,
                                   const uint32_t *, uint32_t, const uint32_t *,
                                   uint32_t));
ABI_FUNCTION(obelisk_rt_v1_scheduler_run_aot_nodes,
             obelisk_rt_status (*)(obelisk_rt_context *,
                                   const obelisk_rt_native_schedule_node *,
                                   uint32_t));
ABI_FUNCTION(
    obelisk_rt_v1_scheduler_prepare_periodic_aot,
    obelisk_rt_status (*)(obelisk_rt_context *,
                          const obelisk_rt_native_schedule_node *, uint32_t,
                          const obelisk_rt_native_periodic_clock_v1 *, uint32_t,
                          const obelisk_rt_native_periodic_alias_v1 *, uint32_t,
                          uint64_t *, obelisk_rt_native_periodic_control_v1 *));
ABI_FUNCTION(obelisk_rt_v1_scheduler_handoff_periodic_aot,
             obelisk_rt_status (*)(
                 obelisk_rt_context *,
                 const obelisk_rt_native_periodic_clock_v1 *, uint32_t,
                 const uint64_t *));
ABI_FUNCTION(obelisk_rt_v1_scheduler_snapshot_aot,
             obelisk_rt_status (*)(obelisk_rt_context *,
                                   obelisk_rt_aot_deopt_snapshot *));
ABI_FUNCTION(obelisk_rt_v1_scheduler_process_token,
             uint64_t (*)(obelisk_rt_context *,
                          obelisk_rt_process_instance_v1 *));
ABI_FUNCTION(obelisk_rt_v1_scheduler_nba,
             obelisk_rt_status (*)(obelisk_rt_context *, uint8_t *, uint8_t *,
                                   uint64_t, uint64_t, uint64_t, uint64_t,
                                   const uint8_t *, const uint8_t *));
ABI_FUNCTION(obelisk_rt_v1_scheduler_static_nba,
             obelisk_rt_status (*)(obelisk_rt_context *, uint64_t, uint8_t *,
                                   uint8_t *, uint64_t, uint64_t, uint64_t,
                                   const uint8_t *, const uint8_t *));
ABI_FUNCTION(obelisk_rt_v1_scheduler_static_nba_packed,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t, uint8_t *,
                                   uint8_t *, uint64_t, uint64_t, uint64_t,
                                   uint64_t, uint64_t));
ABI_FUNCTION(obelisk_rt_v1_static_nba_stage_wide,
             void (*)(obelisk_rt_context *, uint32_t, uint64_t, uint64_t,
                      uint64_t, uint64_t, uint32_t));
ABI_FUNCTION(obelisk_rt_v1_static_nba_claim,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t, uint8_t *,
                                   uint8_t *, uint64_t, uint64_t, uint64_t,
                                   uint64_t, uint64_t));
ABI_FUNCTION(obelisk_rt_v1_static_nba_commit_root,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t, uint32_t,
                                   uint32_t *));
ABI_FUNCTION(obelisk_rt_v1_static_nba_commit_roots,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t, uint32_t,
                                   uint32_t *));
ABI_FUNCTION(obelisk_rt_v1_scheduler_string_nba,
             obelisk_rt_status (*)(obelisk_rt_context *, uint8_t *, uint64_t,
                                   uint64_t, uint64_t, obelisk_rt_string_v1));
ABI_FUNCTION(obelisk_rt_v1_scheduler_managed_nba,
             obelisk_rt_status (*)(obelisk_rt_context *, obelisk_rt_object_v1 *,
                                   uint64_t, const void *, const void *,
                                   uint64_t, uint64_t));
ABI_FUNCTION(obelisk_rt_v1_scheduler_signal,
             void (*)(obelisk_rt_context *, uint64_t, uint64_t, uint32_t));
ABI_FUNCTION(obelisk_rt_v1_scheduler_signal_transition,
             void (*)(obelisk_rt_context *, uint64_t, uint64_t, const uint8_t *,
                      const uint8_t *, const uint8_t *, const uint8_t *));
ABI_FUNCTION(obelisk_rt_v1_scheduler_static_transition,
             void (*)(obelisk_rt_context *, uint32_t, uint64_t, uint64_t,
                      uint64_t, uint64_t, uint64_t, uint64_t));
ABI_FUNCTION(obelisk_rt_v1_scheduler_activate_static_nodes,
             void (*)(obelisk_rt_context *, const uint64_t *, uint32_t));
ABI_FUNCTION(obelisk_rt_v1_scheduler_real_transition,
             void (*)(obelisk_rt_context *, uint64_t, uint32_t, const void *,
                      const void *));
ABI_FUNCTION(obelisk_rt_v1_scheduler_event,
             void (*)(obelisk_rt_context *, uint64_t, uint32_t));
ABI_FUNCTION(obelisk_rt_v1_scheduler_event_after,
             void (*)(obelisk_rt_context *, uint64_t, uint32_t, uint64_t));
ABI_FUNCTION(obelisk_rt_v1_scheduler_event_triggered,
             uint32_t (*)(obelisk_rt_context *, uint64_t));
ABI_FUNCTION(obelisk_rt_v1_scheduler_fail,
             void (*)(obelisk_rt_context *, obelisk_rt_status));
ABI_FUNCTION(obelisk_rt_v1_native_state_register_static,
             obelisk_rt_status (*)(obelisk_rt_context *, uint32_t, uint64_t,
                                   uint64_t));
ABI_FUNCTION(obelisk_rt_v1_native_state_static_handle, uint64_t (*)(uint32_t));
ABI_FUNCTION(obelisk_rt_v1_native_handle_offset,
             uint64_t (*)(uint64_t, int64_t));
ABI_FUNCTION(obelisk_rt_v1_native_state_alloc,
             obelisk_rt_status (*)(obelisk_rt_context *, uint64_t,
                                   const uint8_t *, const uint8_t *,
                                   uint64_t *));
ABI_FUNCTION(obelisk_rt_v1_native_state_alloc_with_roots,
             obelisk_rt_status (*)(obelisk_rt_context *, uint64_t,
                                   const uint8_t *, const uint8_t *,
                                   const uint64_t *, uint64_t, uint64_t *));
ABI_FUNCTION(obelisk_rt_v1_native_state_retain,
             obelisk_rt_status (*)(obelisk_rt_context *, uint64_t));
ABI_FUNCTION(obelisk_rt_v1_native_state_register_managed_roots,
             obelisk_rt_status (*)(obelisk_rt_context *, uint64_t,
                                   const uint64_t *, uint64_t));
ABI_FUNCTION(obelisk_rt_v1_native_state_release,
             obelisk_rt_status (*)(obelisk_rt_context *, uint64_t, uint32_t));
ABI_FUNCTION(obelisk_rt_v1_native_state_load_plane,
             obelisk_rt_status (*)(obelisk_rt_context *, const uint8_t *,
                                   uint64_t, uint64_t, uint64_t, uint32_t,
                                   uint32_t, uint8_t *));
ABI_FUNCTION(obelisk_rt_v1_native_state_store_plane,
             obelisk_rt_status (*)(obelisk_rt_context *, uint8_t *, uint64_t,
                                   uint64_t, uint64_t, uint32_t,
                                   const uint8_t *, uint8_t *));
ABI_FUNCTION(obelisk_rt_v1_scheduler_notify, void (*)(obelisk_rt_context *));
ABI_FUNCTION(obelisk_rt_v1_scheduler_run,
             obelisk_rt_status (*)(obelisk_rt_context *));
ABI_FUNCTION(obelisk_rt_v1_scheduler_run_aot,
             obelisk_rt_status (*)(obelisk_rt_context *));
ABI_FUNCTION(obelisk_rt_v1_gc_lane_create,
             obelisk_rt_status (*)(obelisk_rt_context *,
                                   obelisk_rt_gc_lane_v1 **));
ABI_FUNCTION(obelisk_rt_v1_gc_lane_destroy,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *));
ABI_FUNCTION(obelisk_rt_v1_gc_lane_enter,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *));
ABI_FUNCTION(obelisk_rt_v1_gc_lane_leave,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *));
ABI_FUNCTION(obelisk_rt_v1_gc_safepoint,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *));
ABI_FUNCTION(obelisk_rt_v1_gc_current_lane,
             obelisk_rt_gc_lane_v1 *(*)(obelisk_rt_context *));
ABI_FUNCTION(obelisk_rt_v1_gc_root_push,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_gc_root_v1 *,
                                   obelisk_rt_object_v1 **));
ABI_FUNCTION(obelisk_rt_v1_gc_root_pop,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_gc_root_v1 *));
ABI_FUNCTION(obelisk_rt_v1_gc_root_range_push,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_gc_root_range_v1 *,
                                   obelisk_rt_object_v1 **, uint64_t));
ABI_FUNCTION(obelisk_rt_v1_gc_root_range_pop,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_gc_root_range_v1 *));
ABI_FUNCTION(obelisk_rt_v1_gc_managed_root_push,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_gc_managed_root_v1 *,
                                   obelisk_rt_managed_word_v1 *));
ABI_FUNCTION(obelisk_rt_v1_gc_managed_root_pop,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_gc_managed_root_v1 *));
ABI_FUNCTION(obelisk_rt_v1_gc_managed_root_range_push,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_gc_managed_root_range_v1 *,
                                   obelisk_rt_managed_word_v1 *, uint64_t));
ABI_FUNCTION(obelisk_rt_v1_gc_managed_root_range_pop,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_gc_managed_root_range_v1 *));
ABI_FUNCTION(obelisk_rt_v1_argument_ref_load,
             obelisk_rt_status (*)(obelisk_rt_context *, const uint8_t *,
                                   const uint8_t *, uint64_t,
                                   obelisk_rt_object_v1 *, uint64_t, uint32_t,
                                   uint64_t, uint64_t, uint32_t, uint32_t,
                                   void *, void *));
ABI_FUNCTION(obelisk_rt_v1_argument_ref_store,
             obelisk_rt_status (*)(obelisk_rt_context *, uint8_t *, uint8_t *,
                                   uint64_t, obelisk_rt_object_v1 *, uint64_t,
                                   uint32_t, uint64_t, uint64_t, uint32_t,
                                   uint32_t, const void *, const void *));
ABI_FUNCTION(obelisk_rt_v1_gc_static_root_register,
             obelisk_rt_status (*)(obelisk_rt_context *,
                                   obelisk_rt_object_v1 **));
ABI_FUNCTION(obelisk_rt_v1_gc_static_root_unregister,
             obelisk_rt_status (*)(obelisk_rt_context *,
                                   obelisk_rt_object_v1 **));
ABI_FUNCTION(obelisk_rt_v1_gc_pin,
             obelisk_rt_status (*)(obelisk_rt_context *,
                                   obelisk_rt_object_v1 *));
ABI_FUNCTION(obelisk_rt_v1_gc_unpin,
             obelisk_rt_status (*)(obelisk_rt_context *,
                                   obelisk_rt_object_v1 *));
ABI_FUNCTION(obelisk_rt_v1_gc_collect,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *));
ABI_FUNCTION(obelisk_rt_v1_gc_set_threshold,
             obelisk_rt_status (*)(obelisk_rt_context *, uint64_t));
ABI_FUNCTION(obelisk_rt_v1_gc_statistics,
             obelisk_rt_status (*)(obelisk_rt_context *,
                                   obelisk_rt_gc_statistics_v1 *));
ABI_FUNCTION(obelisk_rt_v1_class_validate,
             obelisk_rt_status (*)(const obelisk_rt_class_descriptor_v1 *));
ABI_FUNCTION(obelisk_rt_v1_class_register,
             obelisk_rt_status (*)(obelisk_rt_context *,
                                   const obelisk_rt_class_descriptor_v1 *));
ABI_FUNCTION(obelisk_rt_v1_element_type_validate,
             obelisk_rt_status (*)(const obelisk_rt_element_type_v1 *));
ABI_FUNCTION(obelisk_rt_v1_element_type_register,
             obelisk_rt_status (*)(obelisk_rt_context *,
                                   const obelisk_rt_element_type_v1 *));
ABI_FUNCTION(obelisk_rt_v1_gc_design_root_register,
             obelisk_rt_status (*)(obelisk_rt_context *, uint64_t));
ABI_FUNCTION(obelisk_rt_v1_object_allocate,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   const obelisk_rt_class_descriptor_v1 *,
                                   obelisk_rt_object_v1 **));
ABI_FUNCTION(obelisk_rt_v1_object_shallow_copy,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   const obelisk_rt_class_descriptor_v1 *,
                                   obelisk_rt_object_v1 *,
                                   obelisk_rt_object_v1 **));
ABI_FUNCTION(obelisk_rt_v1_object_read,
             obelisk_rt_status (*)(obelisk_rt_object_v1 *, uint64_t, void *,
                                   uint64_t));
ABI_FUNCTION(obelisk_rt_v1_object_write,
             obelisk_rt_status (*)(obelisk_rt_object_v1 *, uint64_t,
                                   const void *, uint64_t));
ABI_FUNCTION(obelisk_rt_v1_object_read_planes,
             obelisk_rt_status (*)(obelisk_rt_object_v1 *, uint64_t, void *,
                                   void *, uint64_t));
ABI_FUNCTION(obelisk_rt_v1_object_write_planes,
             obelisk_rt_status (*)(obelisk_rt_object_v1 *, uint64_t,
                                   const void *, const void *, uint64_t));
ABI_FUNCTION(obelisk_rt_v1_object_field_load,
             obelisk_rt_status (*)(obelisk_rt_object_v1 *, uint64_t,
                                   obelisk_rt_object_v1 **));
ABI_FUNCTION(obelisk_rt_v1_object_field_store,
             obelisk_rt_status (*)(obelisk_rt_object_v1 *, uint64_t,
                                   obelisk_rt_object_v1 *));
ABI_FUNCTION(obelisk_rt_v1_object_is_instance,
             uint32_t (*)(const obelisk_rt_object_v1 *,
                          const obelisk_rt_class_descriptor_v1 *));
ABI_FUNCTION(obelisk_rt_v1_object_cast,
             obelisk_rt_status (*)(obelisk_rt_object_v1 *,
                                   const obelisk_rt_class_descriptor_v1 *,
                                   obelisk_rt_object_v1 **));
ABI_FUNCTION(obelisk_rt_v1_object_id,
             uint64_t (*)(const obelisk_rt_object_v1 *));
ABI_FUNCTION(obelisk_rt_v1_string_create,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *, const char *,
                                   uint64_t, obelisk_rt_string_v1 *));
ABI_FUNCTION(obelisk_rt_v1_string_concat,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_string_v1, obelisk_rt_string_v1,
                                   obelisk_rt_string_v1 *));
ABI_FUNCTION(obelisk_rt_v1_string_concat_many,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   const obelisk_rt_string_span_v1 *, uint64_t,
                                   obelisk_rt_string_v1 *));
ABI_FUNCTION(obelisk_rt_v1_string_repeat,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_string_v1, uint64_t,
                                   obelisk_rt_string_v1 *));
ABI_FUNCTION(obelisk_rt_v1_string_from_packed,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *, const void *,
                                   const void *, uint64_t,
                                   obelisk_rt_string_v1 *));
ABI_FUNCTION(obelisk_rt_v1_string_to_packed,
             obelisk_rt_status (*)(obelisk_rt_string_v1, void *, void *,
                                   uint64_t));
ABI_FUNCTION(obelisk_rt_v1_string_view,
             obelisk_rt_status (*)(obelisk_rt_string_v1, char *, const char **,
                                   uint64_t *));
ABI_FUNCTION(obelisk_rt_v1_string_length, uint64_t (*)(obelisk_rt_string_v1));
ABI_FUNCTION(obelisk_rt_v1_string_hash, uint64_t (*)(obelisk_rt_string_v1));
ABI_FUNCTION(obelisk_rt_v1_string_getc,
             uint32_t (*)(obelisk_rt_string_v1, int64_t));
ABI_FUNCTION(obelisk_rt_v1_string_putc,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_string_v1, int64_t, uint32_t,
                                   obelisk_rt_string_v1 *));
ABI_FUNCTION(obelisk_rt_v1_string_substr,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_string_v1, int64_t, int64_t,
                                   obelisk_rt_string_v1 *));
ABI_FUNCTION(obelisk_rt_v1_string_compare,
             int32_t (*)(obelisk_rt_string_v1, obelisk_rt_string_v1));
ABI_FUNCTION(obelisk_rt_v1_string_compare_insensitive,
             int32_t (*)(obelisk_rt_string_v1, obelisk_rt_string_v1));
ABI_FUNCTION(obelisk_rt_v1_string_case_convert,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_string_v1, uint32_t,
                                   obelisk_rt_string_v1 *));
ABI_FUNCTION(obelisk_rt_v1_string_parse_integer,
             obelisk_rt_status (*)(obelisk_rt_string_v1, uint32_t, uint64_t *));
ABI_FUNCTION(obelisk_rt_v1_string_parse_real,
             obelisk_rt_status (*)(obelisk_rt_string_v1, double *));
ABI_FUNCTION(obelisk_rt_v1_string_format_integer,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *, uint64_t, uint32_t,
                                   uint32_t, obelisk_rt_string_v1 *));
ABI_FUNCTION(obelisk_rt_v1_string_format_real,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *, double,
                                   obelisk_rt_string_v1 *));
ABI_FUNCTION(obelisk_rt_v1_dynamic_array_create,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   const obelisk_rt_element_type_v1 *, uint64_t,
                                   obelisk_rt_object_v1 **));
ABI_FUNCTION(obelisk_rt_v1_dynamic_array_resize,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_object_v1 *, uint64_t));
ABI_FUNCTION(obelisk_rt_v1_queue_create,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   const obelisk_rt_element_type_v1 *, uint64_t,
                                   obelisk_rt_object_v1 **));
ABI_FUNCTION(obelisk_rt_v1_container_create_like,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_object_v1 *,
                                   obelisk_rt_object_v1 *, uint64_t,
                                   obelisk_rt_object_v1 **));
ABI_FUNCTION(obelisk_rt_v1_container_create_typed,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *, uint32_t, uint64_t,
                                   uint32_t, uint32_t, uint64_t, uint64_t,
                                   uint64_t,
                                   const obelisk_rt_element_trace_slot_v1 *,
                                   uint64_t, uint64_t, uint64_t,
                                   obelisk_rt_object_v1 **));
ABI_FUNCTION(obelisk_rt_v1_container_size,
             uint64_t (*)(obelisk_rt_object_v1 *));
ABI_FUNCTION(obelisk_rt_v1_container_read,
             obelisk_rt_status (*)(obelisk_rt_object_v1 *, int64_t, void *,
                                   void *));
ABI_FUNCTION(obelisk_rt_v1_container_read_checked,
             obelisk_rt_status (*)(obelisk_rt_object_v1 *, int64_t, void *,
                                   uint64_t, void *, uint64_t));
ABI_FUNCTION(obelisk_rt_v1_container_write,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_object_v1 *, int64_t,
                                   const void *, const void *));
ABI_FUNCTION(obelisk_rt_v1_container_write_checked,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_object_v1 *, int64_t,
                                   const void *, uint64_t, const void *,
                                   uint64_t));
ABI_FUNCTION(obelisk_rt_v1_container_clone,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_object_v1 *,
                                   obelisk_rt_object_v1 **));
ABI_FUNCTION(obelisk_rt_v1_container_delete,
             obelisk_rt_status (*)(obelisk_rt_object_v1 *));
ABI_FUNCTION(obelisk_rt_v1_queue_push,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_object_v1 *, uint32_t,
                                   const void *, const void *));
ABI_FUNCTION(obelisk_rt_v1_queue_pop,
             obelisk_rt_status (*)(obelisk_rt_object_v1 *, uint32_t, void *,
                                   void *, uint32_t *));
ABI_FUNCTION(obelisk_rt_v1_queue_insert,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_object_v1 *, int64_t,
                                   const void *, const void *));
ABI_FUNCTION(obelisk_rt_v1_queue_delete_index,
             obelisk_rt_status (*)(obelisk_rt_object_v1 *, int64_t));
ABI_FUNCTION(obelisk_rt_v1_assoc_create,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   const obelisk_rt_element_type_v1 *,
                                   obelisk_rt_assoc_key_kind_v1, uint64_t,
                                   obelisk_rt_object_v1 **));
ABI_FUNCTION(obelisk_rt_v1_assoc_create_typed,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *, uint64_t, uint32_t,
                                   uint32_t, uint64_t, uint64_t, uint64_t,
                                   const obelisk_rt_element_trace_slot_v1 *,
                                   uint64_t, obelisk_rt_assoc_key_kind_v1,
                                   uint64_t, obelisk_rt_object_v1 **));
ABI_FUNCTION(obelisk_rt_v1_assoc_exists,
             obelisk_rt_status (*)(obelisk_rt_object_v1 *,
                                   const obelisk_rt_assoc_key_v1 *,
                                   uint32_t *));
ABI_FUNCTION(obelisk_rt_v1_assoc_key_info,
             obelisk_rt_status (*)(obelisk_rt_object_v1 *,
                                   obelisk_rt_assoc_key_kind_v1 *, uint64_t *));
ABI_FUNCTION(obelisk_rt_v1_assoc_read,
             obelisk_rt_status (*)(obelisk_rt_object_v1 *,
                                   const obelisk_rt_assoc_key_v1 *, void *,
                                   void *, uint32_t *));
ABI_FUNCTION(obelisk_rt_v1_assoc_read_checked,
             obelisk_rt_status (*)(obelisk_rt_object_v1 *,
                                   const obelisk_rt_assoc_key_v1 *, void *,
                                   uint64_t, void *, uint64_t, uint32_t *));
ABI_FUNCTION(obelisk_rt_v1_assoc_write,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_object_v1 *,
                                   const obelisk_rt_assoc_key_v1 *,
                                   const void *, const void *));
ABI_FUNCTION(obelisk_rt_v1_assoc_write_checked,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_object_v1 *,
                                   const obelisk_rt_assoc_key_v1 *,
                                   const void *, uint64_t, const void *,
                                   uint64_t));
ABI_FUNCTION(obelisk_rt_v1_assoc_set_default,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_object_v1 *, const void *,
                                   const void *));
ABI_FUNCTION(obelisk_rt_v1_assoc_set_default_checked,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_object_v1 *, const void *,
                                   uint64_t, const void *, uint64_t));
ABI_FUNCTION(obelisk_rt_v1_assoc_delete,
             obelisk_rt_status (*)(obelisk_rt_object_v1 *,
                                   const obelisk_rt_assoc_key_v1 *));
ABI_FUNCTION(obelisk_rt_v1_assoc_first,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_object_v1 *,
                                   obelisk_rt_assoc_key_v1 *, uint32_t *));
ABI_FUNCTION(obelisk_rt_v1_assoc_last,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_object_v1 *,
                                   obelisk_rt_assoc_key_v1 *, uint32_t *));
ABI_FUNCTION(obelisk_rt_v1_assoc_next,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_object_v1 *,
                                   obelisk_rt_assoc_key_v1 *, uint32_t *));
ABI_FUNCTION(obelisk_rt_v1_assoc_prev,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_object_v1 *,
                                   obelisk_rt_assoc_key_v1 *, uint32_t *));
ABI_FUNCTION(obelisk_rt_v1_reference_path_index_create,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_object_v1 *, int64_t,
                                   obelisk_rt_object_v1 *, uint64_t, uint32_t,
                                   obelisk_rt_object_v1 **));
ABI_FUNCTION(obelisk_rt_v1_reference_path_assoc_create,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_object_v1 *,
                                   const obelisk_rt_assoc_key_v1 *,
                                   obelisk_rt_object_v1 *, uint64_t, uint32_t,
                                   obelisk_rt_object_v1 **));
ABI_FUNCTION(obelisk_rt_v1_reference_path_load,
             obelisk_rt_status (*)(obelisk_rt_object_v1 *, void *, void *,
                                   uint32_t *));
ABI_FUNCTION(obelisk_rt_v1_reference_path_store,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_object_v1 *, const void *,
                                   const void *));
ABI_FUNCTION(obelisk_rt_v1_method_resolve,
             obelisk_rt_status (*)(obelisk_rt_object_v1 *, uint64_t, uint64_t,
                                   const obelisk_rt_method_descriptor_v1 **));
ABI_FUNCTION(obelisk_rt_v1_method_invoke,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   obelisk_rt_object_v1 *, uint64_t, uint64_t,
                                   const obelisk_rt_method_argument_v1 *,
                                   uint32_t, void *, uint64_t));
ABI_FUNCTION(obelisk_rt_v1_weak_create,
             obelisk_rt_status (*)(obelisk_rt_gc_lane_v1 *,
                                   const obelisk_rt_class_descriptor_v1 *,
                                   obelisk_rt_object_v1 *,
                                   obelisk_rt_object_v1 **));
ABI_FUNCTION(obelisk_rt_v1_weak_get,
             obelisk_rt_status (*)(obelisk_rt_object_v1 *,
                                   obelisk_rt_object_v1 **));
ABI_FUNCTION(obelisk_rt_v1_weak_clear,
             obelisk_rt_status (*)(obelisk_rt_object_v1 *));

#undef ABI_FUNCTION
