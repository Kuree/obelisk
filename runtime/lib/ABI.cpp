//===- ABI.cpp - Compile-time verification of the public runtime ABI -----===//

#include "talos/Runtime/Runtime.h"

#include <climits>
#include <cstddef>
#include <cstdint>
#include <type_traits>

static_assert(CHAR_BIT == 8, "the runtime ABI requires 8-bit bytes");
static_assert(sizeof(void *) == 8 || sizeof(void *) == 4,
              "the runtime ABI supports only 32-bit and 64-bit targets");
static_assert(sizeof(void *) == sizeof(uintptr_t),
              "UINTPTR_MAX selects the layout tables below");
static_assert(sizeof(talos_rt_managed_word_v1) == 8,
              "managed words are fixed-width ABI storage");
static_assert(alignof(talos_rt_managed_word_v1) == 8,
              "managed words require 64-bit ABI alignment");
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
              "the initial runtime ABI supports only little-endian targets");
#endif

// Structures that embed a pointer change size, alignment and field offsets
// with the pointer width, so the assertions below carry both tables. They stay
// literal rather than derived: the compiler computes the same numbers
// independently from llvm::DataLayout, and these assertions exist precisely to
// catch a disagreement between the two.
#if UINTPTR_MAX == UINT64_MAX
#define ABI_PTR(Wide, Narrow) Wide
#else
#define ABI_PTR(Wide, Narrow) Narrow
#endif
#define ABI_SIZE_ALIGN(Type, Size, Align)                                      \
  static_assert(sizeof(Type) == Size, #Type " size changed");                  \
  static_assert(alignof(Type) == Align, #Type " alignment changed")
#define ABI_OFFSET(Type, Field, Offset)                                        \
  static_assert(offsetof(Type, Field) == Offset,                               \
                #Type "." #Field " offset changed")

ABI_SIZE_ALIGN(talos_rt_status, 4, 4);
ABI_SIZE_ALIGN(talos_rt_buffer_v1, 16, 8);
ABI_OFFSET(talos_rt_buffer_v1, data, 0);
ABI_OFFSET(talos_rt_buffer_v1, size, 8);

ABI_SIZE_ALIGN(talos_rt_vpi_object_model_v1, ABI_PTR(24, 24), 8);
ABI_OFFSET(talos_rt_vpi_object_model_v1, data, 0);
ABI_OFFSET(talos_rt_vpi_object_model_v1, size, 8);
ABI_OFFSET(talos_rt_vpi_object_model_v1, fingerprint, 16);

ABI_SIZE_ALIGN(talos_rt_trace_entry_v1, 40, 8);
ABI_OFFSET(talos_rt_trace_entry_v1, offset, 0);
ABI_OFFSET(talos_rt_trace_entry_v1, stride, 8);
ABI_OFFSET(talos_rt_trace_entry_v1, count, 16);
ABI_OFFSET(talos_rt_trace_entry_v1, kind, 24);
ABI_OFFSET(talos_rt_trace_entry_v1, slot_kind, 28);
ABI_OFFSET(talos_rt_trace_entry_v1, child_layout, 32);
ABI_SIZE_ALIGN(talos_rt_trace_layout_v1, 40, 8);
ABI_OFFSET(talos_rt_trace_layout_v1, version, 0);
ABI_OFFSET(talos_rt_trace_layout_v1, reserved, 4);
ABI_OFFSET(talos_rt_trace_layout_v1, size, 8);
ABI_OFFSET(talos_rt_trace_layout_v1, alignment, 16);
ABI_OFFSET(talos_rt_trace_layout_v1, entries, 24);
ABI_OFFSET(talos_rt_trace_layout_v1, entry_count, 32);
ABI_SIZE_ALIGN(talos_rt_method_argument_v1, 16, 8);
ABI_OFFSET(talos_rt_method_argument_v1, data, 0);
ABI_OFFSET(talos_rt_method_argument_v1, size, 8);
ABI_SIZE_ALIGN(talos_rt_method_descriptor_v1, ABI_PTR(40, 32), 8);
ABI_OFFSET(talos_rt_method_descriptor_v1, signature_id, 0);
ABI_OFFSET(talos_rt_method_descriptor_v1, flags, 8);
ABI_OFFSET(talos_rt_method_descriptor_v1, bytecode_function, 12);
ABI_OFFSET(talos_rt_method_descriptor_v1, native_entry, 16);
ABI_OFFSET(talos_rt_method_descriptor_v1, environment, ABI_PTR(24, 20));
ABI_OFFSET(talos_rt_method_descriptor_v1, slot, ABI_PTR(32, 24));
ABI_SIZE_ALIGN(talos_rt_interface_descriptor_v1, 40, 8);
ABI_OFFSET(talos_rt_interface_descriptor_v1, interface_id, 0);
ABI_OFFSET(talos_rt_interface_descriptor_v1, method_signatures, 8);
ABI_OFFSET(talos_rt_interface_descriptor_v1, method_count, 16);
ABI_OFFSET(talos_rt_interface_descriptor_v1, bases, 24);
ABI_OFFSET(talos_rt_interface_descriptor_v1, base_count, 32);
ABI_SIZE_ALIGN(talos_rt_random_edge_v1, 24, 8);
ABI_OFFSET(talos_rt_random_edge_v1, handle_offset, 0);
ABI_OFFSET(talos_rt_random_edge_v1, mode_offset, 8);
ABI_OFFSET(talos_rt_random_edge_v1, mode_mask, 16);
ABI_SIZE_ALIGN(talos_rt_random_variable_v1, 48, 8);
ABI_OFFSET(talos_rt_random_variable_v1, value_offset, 0);
ABI_OFFSET(talos_rt_random_variable_v1, mode_offset, 8);
ABI_OFFSET(talos_rt_random_variable_v1, mode_mask, 16);
ABI_OFFSET(talos_rt_random_variable_v1, randc_key_offset, 24);
ABI_OFFSET(talos_rt_random_variable_v1, randc_position_offset, 32);
ABI_OFFSET(talos_rt_random_variable_v1, bit_width, 40);
ABI_OFFSET(talos_rt_random_variable_v1, flags, 44);
ABI_SIZE_ALIGN(talos_rt_random_variable_reference_v1, 32, 8);
ABI_OFFSET(talos_rt_random_variable_reference_v1, handle_offsets, 0);
ABI_OFFSET(talos_rt_random_variable_reference_v1, handle_count, 8);
ABI_OFFSET(talos_rt_random_variable_reference_v1, value_offset, 16);
ABI_OFFSET(talos_rt_random_variable_reference_v1, bit_width, 24);
ABI_OFFSET(talos_rt_random_variable_reference_v1, flags, 28);
ABI_SIZE_ALIGN(talos_rt_random_layout_v1, 40, 8);
ABI_OFFSET(talos_rt_random_layout_v1, version, 0);
ABI_OFFSET(talos_rt_random_layout_v1, reserved, 4);
ABI_OFFSET(talos_rt_random_layout_v1, edges, 8);
ABI_OFFSET(talos_rt_random_layout_v1, edge_count, 16);
ABI_OFFSET(talos_rt_random_layout_v1, variables, 24);
ABI_OFFSET(talos_rt_random_layout_v1, variable_count, 32);
ABI_SIZE_ALIGN(talos_rt_byte_range_v1, 16, 8);
ABI_OFFSET(talos_rt_byte_range_v1, offset, 0);
ABI_OFFSET(talos_rt_byte_range_v1, size, 8);
ABI_SIZE_ALIGN(talos_rt_class_descriptor_v1, ABI_PTR(136, 112), 8);
ABI_OFFSET(talos_rt_class_descriptor_v1, version, 0);
ABI_OFFSET(talos_rt_class_descriptor_v1, flags, 4);
ABI_OFFSET(talos_rt_class_descriptor_v1, class_id, 8);
ABI_OFFSET(talos_rt_class_descriptor_v1, instance_size, 16);
ABI_OFFSET(talos_rt_class_descriptor_v1, instance_alignment, 24);
ABI_OFFSET(talos_rt_class_descriptor_v1, base, 32);
ABI_OFFSET(talos_rt_class_descriptor_v1, interfaces, ABI_PTR(40, 36));
ABI_OFFSET(talos_rt_class_descriptor_v1, interface_count, ABI_PTR(48, 40));
ABI_OFFSET(talos_rt_class_descriptor_v1, layout, ABI_PTR(56, 48));
ABI_OFFSET(talos_rt_class_descriptor_v1, methods, ABI_PTR(64, 52));
ABI_OFFSET(talos_rt_class_descriptor_v1, method_count, ABI_PTR(72, 56));
ABI_OFFSET(talos_rt_class_descriptor_v1, debug_name, ABI_PTR(80, 64));
ABI_OFFSET(talos_rt_class_descriptor_v1, debug_name_size, ABI_PTR(88, 72));
ABI_OFFSET(talos_rt_class_descriptor_v1, random_layout, ABI_PTR(96, 80));
ABI_OFFSET(talos_rt_class_descriptor_v1, copy_resets, ABI_PTR(104, 84));
ABI_OFFSET(talos_rt_class_descriptor_v1, copy_reset_count, ABI_PTR(112, 88));
ABI_OFFSET(talos_rt_class_descriptor_v1, builtin_base_kind, ABI_PTR(120, 96));
ABI_OFFSET(talos_rt_class_descriptor_v1, builtin_base_reserved,
           ABI_PTR(124, 100));
ABI_OFFSET(talos_rt_class_descriptor_v1, builtin_base_offset,
           ABI_PTR(128, 104));
ABI_SIZE_ALIGN(talos_rt_element_type_v1, ABI_PTR(64, 56), 8);
ABI_OFFSET(talos_rt_element_type_v1, version, 0);
ABI_OFFSET(talos_rt_element_type_v1, kind, 4);
ABI_OFFSET(talos_rt_element_type_v1, type_id, 8);
ABI_OFFSET(talos_rt_element_type_v1, flags, 16);
ABI_OFFSET(talos_rt_element_type_v1, reserved, 20);
ABI_OFFSET(talos_rt_element_type_v1, value_size, 24);
ABI_OFFSET(talos_rt_element_type_v1, alignment, 32);
ABI_OFFSET(talos_rt_element_type_v1, bit_width, 40);
ABI_OFFSET(talos_rt_element_type_v1, trace, 48);
ABI_OFFSET(talos_rt_element_type_v1, default_image, ABI_PTR(56, 52));
ABI_SIZE_ALIGN(talos_rt_assoc_key_v1, 40, 8);
ABI_OFFSET(talos_rt_assoc_key_v1, kind, 0);
ABI_OFFSET(talos_rt_assoc_key_v1, reserved, 4);
ABI_OFFSET(talos_rt_assoc_key_v1, width, 8);
ABI_OFFSET(talos_rt_assoc_key_v1, value, 16);
ABI_OFFSET(talos_rt_assoc_key_v1, unknown, 24);
ABI_OFFSET(talos_rt_assoc_key_v1, string, 32);
ABI_SIZE_ALIGN(talos_rt_gc_root_v1, ABI_PTR(24, 16), 8);
ABI_SIZE_ALIGN(talos_rt_gc_root_range_v1, 32, 8);
ABI_OFFSET(talos_rt_gc_root_range_v1, slots, 0);
ABI_OFFSET(talos_rt_gc_root_range_v1, count, 8);
ABI_OFFSET(talos_rt_gc_root_range_v1, previous, 16);
ABI_OFFSET(talos_rt_gc_root_range_v1, cookie, 24);
ABI_SIZE_ALIGN(talos_rt_gc_managed_root_v1, ABI_PTR(24, 16), 8);
ABI_OFFSET(talos_rt_gc_managed_root_v1, slot, 0);
ABI_OFFSET(talos_rt_gc_managed_root_v1, previous, ABI_PTR(8, 4));
ABI_OFFSET(talos_rt_gc_managed_root_v1, cookie, ABI_PTR(16, 8));
ABI_SIZE_ALIGN(talos_rt_gc_managed_root_range_v1, 32, 8);
ABI_OFFSET(talos_rt_gc_managed_root_range_v1, slots, 0);
ABI_OFFSET(talos_rt_gc_managed_root_range_v1, count, 8);
ABI_OFFSET(talos_rt_gc_managed_root_range_v1, previous, 16);
ABI_OFFSET(talos_rt_gc_managed_root_range_v1, cookie, 24);
ABI_SIZE_ALIGN(talos_rt_string_span_v1, 8, 8);
ABI_OFFSET(talos_rt_string_span_v1, string, 0);
ABI_OFFSET(talos_rt_gc_root_v1, slot, 0);
ABI_OFFSET(talos_rt_gc_root_v1, previous, ABI_PTR(8, 4));
ABI_OFFSET(talos_rt_gc_root_v1, cookie, ABI_PTR(16, 8));
ABI_SIZE_ALIGN(talos_rt_gc_statistics_v1, 72, 8);

ABI_SIZE_ALIGN(talos_rt_handle_v1, 16, 8);
ABI_OFFSET(talos_rt_handle_v1, kind, 0);
ABI_OFFSET(talos_rt_handle_v1, generation, 4);
ABI_OFFSET(talos_rt_handle_v1, id, 8);

ABI_SIZE_ALIGN(talos_rt_dpi_scope_v1, 48, 8);
ABI_OFFSET(talos_rt_dpi_scope_v1, id, 0);
ABI_OFFSET(talos_rt_dpi_scope_v1, parent_id, 8);
ABI_OFFSET(talos_rt_dpi_scope_v1, name, 16);
ABI_OFFSET(talos_rt_dpi_scope_v1, name_size, 24);
ABI_OFFSET(talos_rt_dpi_scope_v1, time_unit, 32);
ABI_OFFSET(talos_rt_dpi_scope_v1, time_precision, 36);
ABI_OFFSET(talos_rt_dpi_scope_v1, reserved, 40);
ABI_SIZE_ALIGN(talos_rt_dpi_dimension_v1, 24, 8);
ABI_OFFSET(talos_rt_dpi_dimension_v1, left, 0);
ABI_OFFSET(talos_rt_dpi_dimension_v1, right, 4);
ABI_OFFSET(talos_rt_dpi_dimension_v1, byte_stride, 8);
ABI_OFFSET(talos_rt_dpi_dimension_v1, flags, 16);
ABI_OFFSET(talos_rt_dpi_dimension_v1, reserved, 20);
ABI_SIZE_ALIGN(talos_rt_dpi_open_array_v1, 64, 8);
ABI_OFFSET(talos_rt_dpi_open_array_v1, magic, 0);
ABI_OFFSET(talos_rt_dpi_open_array_v1, flags, 4);
ABI_OFFSET(talos_rt_dpi_open_array_v1, dimensions, 8);
ABI_OFFSET(talos_rt_dpi_open_array_v1, element_bit_width, 12);
ABI_OFFSET(talos_rt_dpi_open_array_v1, packed_left, 16);
ABI_OFFSET(talos_rt_dpi_open_array_v1, packed_right, 20);
ABI_OFFSET(talos_rt_dpi_open_array_v1, element_size, 24);
ABI_OFFSET(talos_rt_dpi_open_array_v1, data, 32);
ABI_OFFSET(talos_rt_dpi_open_array_v1, data_size, 40);
ABI_OFFSET(talos_rt_dpi_open_array_v1, ranges, 48);
ABI_OFFSET(talos_rt_dpi_open_array_v1, reserved, 56);
ABI_SIZE_ALIGN(talos_rt_activation_descriptor_v1, 24, 8);
ABI_OFFSET(talos_rt_activation_descriptor_v1, code_unit_id, 0);
ABI_OFFSET(talos_rt_activation_descriptor_v1, native_entry, 8);
ABI_OFFSET(talos_rt_activation_descriptor_v1, bytecode_function,
           ABI_PTR(16, 12));
ABI_OFFSET(talos_rt_activation_descriptor_v1, flags, ABI_PTR(20, 16));
ABI_SIZE_ALIGN(talos_rt_observer_capture_abi_v1, 8, 4);
ABI_SIZE_ALIGN(talos_rt_observer_descriptor_v1, ABI_PTR(48, 40), 8);
ABI_OFFSET(talos_rt_observer_descriptor_v1, code_unit_id, 0);
ABI_OFFSET(talos_rt_observer_descriptor_v1, capture_abi, 8);
ABI_OFFSET(talos_rt_observer_descriptor_v1, capture_count, ABI_PTR(16, 12));
ABI_OFFSET(talos_rt_observer_descriptor_v1, result_width, ABI_PTR(20, 16));
ABI_OFFSET(talos_rt_observer_descriptor_v1, flags, ABI_PTR(24, 20));
ABI_OFFSET(talos_rt_observer_descriptor_v1, bytecode_function,
           ABI_PTR(28, 24));
ABI_OFFSET(talos_rt_observer_descriptor_v1, native_evaluator,
           ABI_PTR(32, 28));
ABI_OFFSET(talos_rt_observer_descriptor_v1, reserved, ABI_PTR(40, 32));
ABI_SIZE_ALIGN(talos_rt_sampled_range_v1, 24, 8);
ABI_OFFSET(talos_rt_sampled_range_v1, source_bit_offset, 0);
ABI_OFFSET(talos_rt_sampled_range_v1, snapshot_byte_offset, 8);
ABI_OFFSET(talos_rt_sampled_range_v1, bit_width, 16);
ABI_SIZE_ALIGN(talos_rt_export_descriptor_v1, 64, 8);
ABI_OFFSET(talos_rt_export_descriptor_v1, export_id, 0);
ABI_OFFSET(talos_rt_export_descriptor_v1, flags, 4);
ABI_OFFSET(talos_rt_export_descriptor_v1, scope_id, 8);
ABI_OFFSET(talos_rt_export_descriptor_v1, code_unit_id, 16);
ABI_OFFSET(talos_rt_export_descriptor_v1, abi_signature, 24);
ABI_OFFSET(talos_rt_export_descriptor_v1, input_count, 32);
ABI_OFFSET(talos_rt_export_descriptor_v1, output_count, 36);
ABI_OFFSET(talos_rt_export_descriptor_v1, bytecode_function, 40);
ABI_OFFSET(talos_rt_export_descriptor_v1, reserved, 44);
ABI_OFFSET(talos_rt_export_descriptor_v1, native_entry, 48);
ABI_OFFSET(talos_rt_export_descriptor_v1, reserved_tail, ABI_PTR(56, 56));
ABI_SIZE_ALIGN(talos_rt_execution_extension_v1, 72, 8);
ABI_OFFSET(talos_rt_execution_extension_v1, version, 0);
ABI_OFFSET(talos_rt_execution_extension_v1, size, 4);
ABI_OFFSET(talos_rt_execution_extension_v1, sampled_ranges, 8);
ABI_OFFSET(talos_rt_execution_extension_v1, sampled_range_count, 16);
ABI_OFFSET(talos_rt_execution_extension_v1, exports, 24);
ABI_OFFSET(talos_rt_execution_extension_v1, export_count, 32);
ABI_OFFSET(talos_rt_execution_extension_v1, class_bitstream, 40);
ABI_OFFSET(talos_rt_execution_extension_v1, class_bitstream_size, 48);
ABI_OFFSET(talos_rt_execution_extension_v1, coverage_schema, 56);
ABI_OFFSET(talos_rt_execution_extension_v1, coverage_schema_size, 64);
ABI_SIZE_ALIGN(talos_rt_class_bitstream_header_v1, 128, 8);
ABI_OFFSET(talos_rt_class_bitstream_header_v1, identity, 0);
ABI_OFFSET(talos_rt_class_bitstream_header_v1, size, 8);
ABI_OFFSET(talos_rt_class_bitstream_header_v1, site_offset, 16);
ABI_OFFSET(talos_rt_class_bitstream_header_v1, site_count, 24);
ABI_OFFSET(talos_rt_class_bitstream_header_v1, group_offset, 32);
ABI_OFFSET(talos_rt_class_bitstream_header_v1, group_count, 40);
ABI_OFFSET(talos_rt_class_bitstream_header_v1, member_offset, 48);
ABI_OFFSET(talos_rt_class_bitstream_header_v1, member_count, 56);
ABI_OFFSET(talos_rt_class_bitstream_header_v1, schema_offset, 64);
ABI_OFFSET(talos_rt_class_bitstream_header_v1, schema_count, 72);
ABI_OFFSET(talos_rt_class_bitstream_header_v1, field_offset, 80);
ABI_OFFSET(talos_rt_class_bitstream_header_v1, field_count, 88);
ABI_OFFSET(talos_rt_class_bitstream_header_v1, plan_offset, 96);
ABI_OFFSET(talos_rt_class_bitstream_header_v1, plan_size, 104);
ABI_OFFSET(talos_rt_class_bitstream_header_v1, reserved0, 112);
ABI_OFFSET(talos_rt_class_bitstream_header_v1, reserved1, 120);
ABI_SIZE_ALIGN(talos_rt_class_bitstream_site_v1, 40, 8);
ABI_OFFSET(talos_rt_class_bitstream_site_v1, site_id, 0);
ABI_OFFSET(talos_rt_class_bitstream_site_v1, bytecode_function, 8);
ABI_OFFSET(talos_rt_class_bitstream_site_v1, bytecode_site, 12);
ABI_OFFSET(talos_rt_class_bitstream_site_v1, plan_offset, 16);
ABI_OFFSET(talos_rt_class_bitstream_site_v1, plan_size, 24);
ABI_OFFSET(talos_rt_class_bitstream_site_v1, reserved, 32);
ABI_SIZE_ALIGN(talos_rt_class_bitstream_group_v1, 40, 8);
ABI_OFFSET(talos_rt_class_bitstream_group_v1, group_id, 0);
ABI_OFFSET(talos_rt_class_bitstream_group_v1, static_class_id, 8);
ABI_OFFSET(talos_rt_class_bitstream_group_v1, first_member, 16);
ABI_OFFSET(talos_rt_class_bitstream_group_v1, member_count, 24);
ABI_OFFSET(talos_rt_class_bitstream_group_v1, allow_hidden_root, 32);
ABI_OFFSET(talos_rt_class_bitstream_group_v1, reserved, 36);
ABI_SIZE_ALIGN(talos_rt_class_bitstream_schema_v1, 48, 8);
ABI_OFFSET(talos_rt_class_bitstream_schema_v1, class_id, 0);
ABI_OFFSET(talos_rt_class_bitstream_schema_v1, instance_size, 8);
ABI_OFFSET(talos_rt_class_bitstream_schema_v1, first_field, 16);
ABI_OFFSET(talos_rt_class_bitstream_schema_v1, field_count, 24);
ABI_OFFSET(talos_rt_class_bitstream_schema_v1, instance_alignment, 32);
ABI_OFFSET(talos_rt_class_bitstream_schema_v1, reserved, 36);
ABI_OFFSET(talos_rt_class_bitstream_schema_v1, reserved_tail, 40);
ABI_SIZE_ALIGN(talos_rt_class_bitstream_field_v1, 48, 8);
ABI_OFFSET(talos_rt_class_bitstream_field_v1, offset, 0);
ABI_OFFSET(talos_rt_class_bitstream_field_v1, plane_size, 8);
ABI_OFFSET(talos_rt_class_bitstream_field_v1, root_span, 16);
ABI_OFFSET(talos_rt_class_bitstream_field_v1, plan_offset, 24);
ABI_OFFSET(talos_rt_class_bitstream_field_v1, plan_size, 32);
ABI_OFFSET(talos_rt_class_bitstream_field_v1, flags, 40);
ABI_OFFSET(talos_rt_class_bitstream_field_v1, value_alignment, 44);
ABI_SIZE_ALIGN(talos_rt_execution_descriptor_v1, 120, 8);
ABI_OFFSET(talos_rt_execution_descriptor_v1, version, 0);
ABI_OFFSET(talos_rt_execution_descriptor_v1, flags, 4);
ABI_OFFSET(talos_rt_execution_descriptor_v1, reserved, 8);
ABI_OFFSET(talos_rt_execution_descriptor_v1, bytecode, 16);
ABI_OFFSET(talos_rt_execution_descriptor_v1, bytecode_size, 24);
ABI_OFFSET(talos_rt_execution_descriptor_v1, design_database, 32);
ABI_OFFSET(talos_rt_execution_descriptor_v1, design_database_size, 40);
ABI_OFFSET(talos_rt_execution_descriptor_v1, state_bit_count, 48);
ABI_OFFSET(talos_rt_execution_descriptor_v1, checksum, 56);
ABI_OFFSET(talos_rt_execution_descriptor_v1, dpi_scopes, 64);
ABI_OFFSET(talos_rt_execution_descriptor_v1, dpi_scope_count, 72);
ABI_OFFSET(talos_rt_execution_descriptor_v1, dpi_time_precision, 80);
ABI_OFFSET(talos_rt_execution_descriptor_v1, dpi_reserved, 84);
ABI_OFFSET(talos_rt_execution_descriptor_v1, activations, 88);
ABI_OFFSET(talos_rt_execution_descriptor_v1, activation_count, 96);
ABI_OFFSET(talos_rt_execution_descriptor_v1, observers, 104);
ABI_OFFSET(talos_rt_execution_descriptor_v1, observer_count, 112);
ABI_SIZE_ALIGN(talos_rt_design_bytecode_entry_v1, ABI_PTR(16, 12),
               ABI_PTR(8, 4));
ABI_OFFSET(talos_rt_design_bytecode_entry_v1, execution, 0);
ABI_OFFSET(talos_rt_design_bytecode_entry_v1, function, ABI_PTR(8, 4));
ABI_OFFSET(talos_rt_design_bytecode_entry_v1, reserved, ABI_PTR(12, 8));
ABI_SIZE_ALIGN(talos_rt_design_bytecode_header_v1, 208, 8);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, magic, 0);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, version, 8);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, reserved, 12);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, header_size, 16);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, flags, 20);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, image_size, 24);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, checksum, 32);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, function_offset, 40);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, function_count, 48);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, layout_offset, 56);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, layout_count, 64);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, code_offset, 72);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, instruction_count, 80);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, operand_offset, 88);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, operand_count, 96);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, constant_offset, 104);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, constant_size, 112);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, continuation_offset, 120);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, continuation_count, 128);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, intrinsic_offset, 136);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, intrinsic_count, 144);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, site_offset, 152);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, site_count, 160);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, state_offset, 168);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, state_count, 176);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, connectivity_offset, 184);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, connectivity_count, 192);
ABI_OFFSET(talos_rt_design_bytecode_header_v1, tail_reserved, 200);
ABI_SIZE_ALIGN(talos_rt_design_database_header_v1, 176, 8);
ABI_OFFSET(talos_rt_design_database_header_v1, magic, 0);
ABI_OFFSET(talos_rt_design_database_header_v1, version, 8);
ABI_OFFSET(talos_rt_design_database_header_v1, reserved, 12);
ABI_OFFSET(talos_rt_design_database_header_v1, profile, 16);
ABI_OFFSET(talos_rt_design_database_header_v1, header_size, 20);
ABI_OFFSET(talos_rt_design_database_header_v1, image_size, 24);
ABI_OFFSET(talos_rt_design_database_header_v1, checksum, 32);
ABI_OFFSET(talos_rt_design_database_header_v1, root_offset, 40);
ABI_OFFSET(talos_rt_design_database_header_v1, scope_offset, 48);
ABI_OFFSET(talos_rt_design_database_header_v1, scope_count, 56);
ABI_OFFSET(talos_rt_design_database_header_v1, object_offset, 64);
ABI_OFFSET(talos_rt_design_database_header_v1, object_count, 72);
ABI_OFFSET(talos_rt_design_database_header_v1, type_offset, 80);
ABI_OFFSET(talos_rt_design_database_header_v1, type_count, 88);
ABI_OFFSET(talos_rt_design_database_header_v1, string_offset, 96);
ABI_OFFSET(talos_rt_design_database_header_v1, string_size, 104);
ABI_OFFSET(talos_rt_design_database_header_v1, index_offset, 112);
ABI_OFFSET(talos_rt_design_database_header_v1, index_count, 120);
ABI_OFFSET(talos_rt_design_database_header_v1, statement_offset, 128);
ABI_OFFSET(talos_rt_design_database_header_v1, statement_count, 136);
ABI_OFFSET(talos_rt_design_database_header_v1, statement_site_offset, 144);
ABI_OFFSET(talos_rt_design_database_header_v1, statement_site_count, 152);
ABI_OFFSET(talos_rt_design_database_header_v1, relation_offset, 160);
ABI_OFFSET(talos_rt_design_database_header_v1, relation_count, 168);
ABI_SIZE_ALIGN(talos_rt_design_cursor_v1, 8, 8);
ABI_SIZE_ALIGN(talos_rt_design_info_v1, 56, 8);
ABI_SIZE_ALIGN(talos_rt_import_input_v1, ABI_PTR(32, 24), 8);
ABI_OFFSET(talos_rt_import_input_v1, kind, 0);
ABI_OFFSET(talos_rt_import_input_v1, flags, 1);
ABI_OFFSET(talos_rt_import_input_v1, reserved, 2);
ABI_OFFSET(talos_rt_import_input_v1, bit_width, 4);
ABI_OFFSET(talos_rt_import_input_v1, value, 8);
ABI_OFFSET(talos_rt_import_input_v1, unknown, ABI_PTR(16, 12));
ABI_OFFSET(talos_rt_import_input_v1, limb_count, ABI_PTR(24, 16));
ABI_SIZE_ALIGN(talos_rt_import_output_v1, ABI_PTR(32, 24), 8);
ABI_OFFSET(talos_rt_import_output_v1, kind, 0);
ABI_OFFSET(talos_rt_import_output_v1, flags, 1);
ABI_OFFSET(talos_rt_import_output_v1, reserved, 2);
ABI_OFFSET(talos_rt_import_output_v1, bit_width, 4);
ABI_OFFSET(talos_rt_import_output_v1, value, 8);
ABI_OFFSET(talos_rt_import_output_v1, unknown, ABI_PTR(16, 12));
ABI_OFFSET(talos_rt_import_output_v1, limb_count, ABI_PTR(24, 16));
ABI_SIZE_ALIGN(talos_rt_import_site_v1, 56, 8);
ABI_OFFSET(talos_rt_import_site_v1, version, 0);
ABI_OFFSET(talos_rt_import_site_v1, flags, 4);
ABI_OFFSET(talos_rt_import_site_v1, import_id, 8);
ABI_OFFSET(talos_rt_import_site_v1, reserved, 12);
ABI_OFFSET(talos_rt_import_site_v1, scope_id, 16);
ABI_OFFSET(talos_rt_import_site_v1, source_file, 24);
ABI_OFFSET(talos_rt_import_site_v1, source_file_size, 32);
ABI_OFFSET(talos_rt_import_site_v1, source_line, 40);
ABI_OFFSET(talos_rt_import_site_v1, source_column, 44);
ABI_OFFSET(talos_rt_import_site_v1, abi_signature, 48);

ABI_SIZE_ALIGN(talos_rt_fragment_action_v1, 32, 8);
ABI_OFFSET(talos_rt_fragment_action_v1, kind, 0);
ABI_OFFSET(talos_rt_fragment_action_v1, suspend_kind, 4);
ABI_OFFSET(talos_rt_fragment_action_v1, continuation, 8);
ABI_OFFSET(talos_rt_fragment_action_v1, flags, 12);
ABI_OFFSET(talos_rt_fragment_action_v1, payload, 16);
ABI_OFFSET(talos_rt_fragment_action_v1, auxiliary, 24);

ABI_SIZE_ALIGN(talos_rt_bytecode_entry_v1, 8, 4);
ABI_OFFSET(talos_rt_bytecode_entry_v1, continuation, 0);
ABI_OFFSET(talos_rt_bytecode_entry_v1, instruction, 4);
ABI_SIZE_ALIGN(talos_rt_bytecode_validation_v1, 8, 4);
ABI_OFFSET(talos_rt_bytecode_validation_v1, state, 0);
ABI_OFFSET(talos_rt_bytecode_validation_v1, reserved, 4);

ABI_SIZE_ALIGN(talos_rt_bytecode_operand_v1, 32, 8);
ABI_OFFSET(talos_rt_bytecode_operand_v1, kind, 0);
ABI_OFFSET(talos_rt_bytecode_operand_v1, direction, 1);
ABI_OFFSET(talos_rt_bytecode_operand_v1, value_kind, 2);
ABI_OFFSET(talos_rt_bytecode_operand_v1, flags, 3);
ABI_OFFSET(talos_rt_bytecode_operand_v1, reserved, 4);
ABI_OFFSET(talos_rt_bytecode_operand_v1, value, 8);
ABI_OFFSET(talos_rt_bytecode_operand_v1, size, 16);
ABI_OFFSET(talos_rt_bytecode_operand_v1, auxiliary, 24);

ABI_SIZE_ALIGN(talos_rt_bytecode_service_site_v1, 16, 4);
ABI_OFFSET(talos_rt_bytecode_service_site_v1, service, 0);
ABI_OFFSET(talos_rt_bytecode_service_site_v1, first_operand, 4);
ABI_OFFSET(talos_rt_bytecode_service_site_v1, operand_count, 8);
ABI_OFFSET(talos_rt_bytecode_service_site_v1, flags, 10);
ABI_OFFSET(talos_rt_bytecode_service_site_v1, reserved, 12);

ABI_SIZE_ALIGN(talos_rt_bytecode_v1, ABI_PTR(96, 80), 8);
ABI_OFFSET(talos_rt_bytecode_v1, code, 0);
ABI_OFFSET(talos_rt_bytecode_v1, code_size, 8);
ABI_OFFSET(talos_rt_bytecode_v1, entries, 16);
ABI_OFFSET(talos_rt_bytecode_v1, entry_count, ABI_PTR(24, 20));
ABI_OFFSET(talos_rt_bytecode_v1, register_count, ABI_PTR(28, 24));
ABI_OFFSET(talos_rt_bytecode_v1, register_offset, 32);
ABI_OFFSET(talos_rt_bytecode_v1, validation, 40);
ABI_OFFSET(talos_rt_bytecode_v1, constants, ABI_PTR(48, 44));
ABI_OFFSET(talos_rt_bytecode_v1, constant_size, ABI_PTR(56, 48));
ABI_OFFSET(talos_rt_bytecode_v1, service_sites, ABI_PTR(64, 56));
ABI_OFFSET(talos_rt_bytecode_v1, service_site_count, ABI_PTR(72, 60));
ABI_OFFSET(talos_rt_bytecode_v1, reserved, ABI_PTR(76, 64));
ABI_OFFSET(talos_rt_bytecode_v1, operands, ABI_PTR(80, 68));
ABI_OFFSET(talos_rt_bytecode_v1, operand_count, ABI_PTR(88, 72));

ABI_SIZE_ALIGN(talos_rt_arg_v1, ABI_PTR(32, 24), 8);
ABI_OFFSET(talos_rt_arg_v1, kind, 0);
ABI_OFFSET(talos_rt_arg_v1, flags, 4);
ABI_OFFSET(talos_rt_arg_v1, size, 8);
ABI_OFFSET(talos_rt_arg_v1, data, 16);
ABI_OFFSET(talos_rt_arg_v1, unknown, ABI_PTR(24, 20));

ABI_SIZE_ALIGN(talos_rt_enum_arg_v1, ABI_PTR(40, 32), 8);
ABI_OFFSET(talos_rt_enum_arg_v1, width, 0);
ABI_OFFSET(talos_rt_enum_arg_v1, flags, 8);
ABI_OFFSET(talos_rt_enum_arg_v1, reserved, 12);
ABI_OFFSET(talos_rt_enum_arg_v1, value, 16);
ABI_OFFSET(talos_rt_enum_arg_v1, unknown, ABI_PTR(24, 20));
ABI_OFFSET(talos_rt_enum_arg_v1, name, ABI_PTR(32, 24));

ABI_SIZE_ALIGN(talos_rt_raw_aggregate_arg_v1, 24, 8);
ABI_OFFSET(talos_rt_raw_aggregate_arg_v1, pattern, 0);
ABI_OFFSET(talos_rt_raw_aggregate_arg_v1, two_state, 8);
ABI_OFFSET(talos_rt_raw_aggregate_arg_v1, four_state, 16);

ABI_SIZE_ALIGN(talos_rt_format_env_v1, 64, 8);
ABI_OFFSET(talos_rt_format_env_v1, scope, 0);
ABI_OFFSET(talos_rt_format_env_v1, scope_size, 8);
ABI_OFFSET(talos_rt_format_env_v1, library_cell, 16);
ABI_OFFSET(talos_rt_format_env_v1, library_cell_size, 24);
ABI_OFFSET(talos_rt_format_env_v1, time_width, 32);
ABI_OFFSET(talos_rt_format_env_v1, time_precision, 36);
ABI_OFFSET(talos_rt_format_env_v1, time_suffix, 40);
ABI_OFFSET(talos_rt_format_env_v1, time_suffix_size, 48);
ABI_OFFSET(talos_rt_format_env_v1, time_multiplier, 56);

ABI_SIZE_ALIGN(talos_rt_fragment_descriptor_v1, ABI_PTR(120, 104), 8);
ABI_OFFSET(talos_rt_fragment_descriptor_v1, handle, 0);
ABI_OFFSET(talos_rt_fragment_descriptor_v1, code_kind, 16);
ABI_OFFSET(talos_rt_fragment_descriptor_v1, flags, 20);
ABI_OFFSET(talos_rt_fragment_descriptor_v1, code, 24);

ABI_SIZE_ALIGN(talos_rt_frame_field_v1, 32, 8);
ABI_OFFSET(talos_rt_frame_field_v1, kind, 0);
ABI_OFFSET(talos_rt_frame_field_v1, flags, 4);
ABI_OFFSET(talos_rt_frame_field_v1, offset, 8);
ABI_OFFSET(talos_rt_frame_field_v1, size, 16);
ABI_OFFSET(talos_rt_frame_field_v1, alignment, 24);
ABI_OFFSET(talos_rt_frame_field_v1, reserved, 28);
ABI_SIZE_ALIGN(talos_rt_frame_layout_v1, ABI_PTR(56, 48), 8);
ABI_OFFSET(talos_rt_frame_layout_v1, version, 0);
ABI_OFFSET(talos_rt_frame_layout_v1, flags, 4);
ABI_OFFSET(talos_rt_frame_layout_v1, frame_size, 8);
ABI_OFFSET(talos_rt_frame_layout_v1, frame_alignment, 16);
ABI_OFFSET(talos_rt_frame_layout_v1, fields, 24);
ABI_OFFSET(talos_rt_frame_layout_v1, field_count, ABI_PTR(32, 28));
ABI_OFFSET(talos_rt_frame_layout_v1, continuation_count, ABI_PTR(36, 32));
ABI_OFFSET(talos_rt_frame_layout_v1, continuations, ABI_PTR(40, 36));
ABI_OFFSET(talos_rt_frame_layout_v1, checksum, ABI_PTR(48, 40));
ABI_SIZE_ALIGN(talos_rt_wait_record_v1, 32, 8);
ABI_OFFSET(talos_rt_wait_record_v1, version, 0);
ABI_OFFSET(talos_rt_wait_record_v1, kind, 4);
ABI_OFFSET(talos_rt_wait_record_v1, flags, 8);
ABI_OFFSET(talos_rt_wait_record_v1, count, 12);
ABI_OFFSET(talos_rt_wait_record_v1, payload, 16);
ABI_OFFSET(talos_rt_wait_record_v1, auxiliary, 24);
ABI_SIZE_ALIGN(talos_rt_wait_entry_v1, 16, 8);
ABI_OFFSET(talos_rt_wait_entry_v1, stable_id, 0);
ABI_OFFSET(talos_rt_wait_entry_v1, edge, 8);
ABI_OFFSET(talos_rt_wait_entry_v1, reserved, 12);
ABI_SIZE_ALIGN(talos_rt_computed_wait_record_v1, 96, 8);
ABI_OFFSET(talos_rt_computed_wait_record_v1, version, 0);
ABI_OFFSET(talos_rt_computed_wait_record_v1, kind, 4);
ABI_OFFSET(talos_rt_computed_wait_record_v1, flags, 8);
ABI_OFFSET(talos_rt_computed_wait_record_v1, clause_count, 12);
ABI_OFFSET(talos_rt_computed_wait_record_v1, observer_count, 16);
ABI_OFFSET(talos_rt_computed_wait_record_v1, capture_count, 20);
ABI_OFFSET(talos_rt_computed_wait_record_v1, dependency_count, 24);
ABI_OFFSET(talos_rt_computed_wait_record_v1, previous_limb_count, 28);
ABI_OFFSET(talos_rt_computed_wait_record_v1, observers_offset, 32);
ABI_OFFSET(talos_rt_computed_wait_record_v1, captures_offset, 40);
ABI_OFFSET(talos_rt_computed_wait_record_v1, dependencies_offset, 48);
ABI_OFFSET(talos_rt_computed_wait_record_v1, clauses_offset, 56);
ABI_OFFSET(talos_rt_computed_wait_record_v1, previous_value_offset, 64);
ABI_OFFSET(talos_rt_computed_wait_record_v1, previous_unknown_offset, 72);
ABI_OFFSET(talos_rt_computed_wait_record_v1, total_size, 80);
ABI_OFFSET(talos_rt_computed_wait_record_v1, reserved, 88);
ABI_SIZE_ALIGN(talos_rt_computed_observer_v1, 32, 8);
ABI_SIZE_ALIGN(talos_rt_computed_capture_v1, 32, 8);
ABI_SIZE_ALIGN(talos_rt_computed_dependency_v1, 16, 8);
ABI_SIZE_ALIGN(talos_rt_computed_clause_v1, 16, 4);
ABI_SIZE_ALIGN(talos_rt_process_descriptor_v1, ABI_PTR(88, 64), 8);
ABI_OFFSET(talos_rt_process_descriptor_v1, handle, 0);
ABI_OFFSET(talos_rt_process_descriptor_v1, version, 16);
ABI_OFFSET(talos_rt_process_descriptor_v1, flags, 20);
ABI_OFFSET(talos_rt_process_descriptor_v1, available_tiers, 24);
ABI_OFFSET(talos_rt_process_descriptor_v1, reserved, 28);
ABI_OFFSET(talos_rt_process_descriptor_v1, frame_layout, 32);
ABI_OFFSET(talos_rt_process_descriptor_v1, native_requirements,
           ABI_PTR(40, 36));
ABI_OFFSET(talos_rt_process_descriptor_v1, native_execute, ABI_PTR(48, 40));
ABI_OFFSET(talos_rt_process_descriptor_v1, native_destroy, ABI_PTR(56, 44));
ABI_OFFSET(talos_rt_process_descriptor_v1, bytecode, ABI_PTR(64, 48));
ABI_OFFSET(talos_rt_process_descriptor_v1, execution, ABI_PTR(72, 52));
ABI_OFFSET(talos_rt_process_descriptor_v1, design_bytecode, ABI_PTR(80, 56));
ABI_SIZE_ALIGN(talos_rt_process_spawn_plan_v1, ABI_PTR(72, 64), 8);
ABI_OFFSET(talos_rt_process_spawn_plan_v1, descriptor, 0);
ABI_OFFSET(talos_rt_process_spawn_plan_v1, capture_size, 8);
ABI_OFFSET(talos_rt_process_spawn_plan_v1, continuations, 16);
ABI_OFFSET(talos_rt_process_spawn_plan_v1, ranks, ABI_PTR(24, 20));
ABI_OFFSET(talos_rt_process_spawn_plan_v1, bytecode_continuations,
           ABI_PTR(32, 24));
ABI_OFFSET(talos_rt_process_spawn_plan_v1, program_owner, ABI_PTR(40, 32));
ABI_OFFSET(talos_rt_process_spawn_plan_v1, schedule_flags, ABI_PTR(48, 40));
ABI_OFFSET(talos_rt_process_spawn_plan_v1, actor_slot, ABI_PTR(52, 44));
ABI_OFFSET(talos_rt_process_spawn_plan_v1, initial_rank, ABI_PTR(56, 48));
ABI_OFFSET(talos_rt_process_spawn_plan_v1, continuation_count,
           ABI_PTR(60, 52));
ABI_OFFSET(talos_rt_process_spawn_plan_v1, bytecode_continuation_count,
           ABI_PTR(64, 56));
ABI_OFFSET(talos_rt_process_spawn_plan_v1, options, ABI_PTR(68, 60));
ABI_SIZE_ALIGN(talos_rt_process_spawn_entry_v1, 24, 8);
ABI_OFFSET(talos_rt_process_spawn_entry_v1, plan, 0);
ABI_OFFSET(talos_rt_process_spawn_entry_v1, capture_offset, 8);
ABI_OFFSET(talos_rt_process_spawn_entry_v1, initialization_scope, 16);
ABI_SIZE_ALIGN(talos_rt_process_instance_v1, ABI_PTR(104, 80), 8);
ABI_OFFSET(talos_rt_process_instance_v1, descriptor, 0);
ABI_OFFSET(talos_rt_process_instance_v1, allocation, ABI_PTR(8, 4));
ABI_OFFSET(talos_rt_process_instance_v1, frame, ABI_PTR(16, 8));
ABI_OFFSET(talos_rt_process_instance_v1, frame_size, ABI_PTR(24, 16));
ABI_OFFSET(talos_rt_process_instance_v1, scratch_offset, ABI_PTR(32, 24));
ABI_OFFSET(talos_rt_process_instance_v1, scratch_size, ABI_PTR(40, 32));
ABI_OFFSET(talos_rt_process_instance_v1, native_handle, ABI_PTR(48, 40));
ABI_OFFSET(talos_rt_process_instance_v1, continuation, ABI_PTR(56, 44));
ABI_OFFSET(talos_rt_process_instance_v1, tier, ABI_PTR(60, 48));
ABI_OFFSET(talos_rt_process_instance_v1, lifecycle, ABI_PTR(64, 52));
ABI_OFFSET(talos_rt_process_instance_v1, status, ABI_PTR(68, 56));
ABI_OFFSET(talos_rt_process_instance_v1, context, ABI_PTR(72, 60));
ABI_OFFSET(talos_rt_process_instance_v1, action, ABI_PTR(80, 64));
ABI_OFFSET(talos_rt_process_instance_v1, ownership_context, ABI_PTR(88, 68));
ABI_OFFSET(talos_rt_process_instance_v1, observer_pin_count, ABI_PTR(96, 72));
ABI_OFFSET(talos_rt_process_instance_v1, observer_destroy_pending,
           ABI_PTR(100, 76));

ABI_SIZE_ALIGN(talos_rt_aot_deopt_actor, 88, 8);
ABI_OFFSET(talos_rt_aot_deopt_actor, slot, 0);
ABI_OFFSET(talos_rt_aot_deopt_actor, flags, 4);
ABI_OFFSET(talos_rt_aot_deopt_actor, schedule_rank, 8);
ABI_OFFSET(talos_rt_aot_deopt_actor, queued_region, 12);
ABI_OFFSET(talos_rt_aot_deopt_actor, insertion_sequence, 16);
ABI_OFFSET(talos_rt_aot_deopt_actor, wake_time, 24);
ABI_OFFSET(talos_rt_aot_deopt_actor, wait_offset, 32);
ABI_OFFSET(talos_rt_aot_deopt_actor, wait_size, 40);
ABI_OFFSET(talos_rt_aot_deopt_actor, action, 48);
ABI_OFFSET(talos_rt_aot_deopt_actor, started, 80);
ABI_OFFSET(talos_rt_aot_deopt_actor, ready, 84);
ABI_SIZE_ALIGN(talos_rt_aot_deopt_nba, 24, 8);
ABI_OFFSET(talos_rt_aot_deopt_nba, slot, 0);
ABI_OFFSET(talos_rt_aot_deopt_nba, exec_region, 4);
ABI_OFFSET(talos_rt_aot_deopt_nba, sequence, 8);
ABI_OFFSET(talos_rt_aot_deopt_nba, due_time, 16);
ABI_SIZE_ALIGN(talos_rt_aot_deopt_snapshot, ABI_PTR(56, 48), 8);
ABI_OFFSET(talos_rt_aot_deopt_snapshot, size, 0);
ABI_OFFSET(talos_rt_aot_deopt_snapshot, current_time, 8);
ABI_OFFSET(talos_rt_aot_deopt_snapshot, actors, 16);
ABI_OFFSET(talos_rt_aot_deopt_snapshot, actor_count, ABI_PTR(24, 20));
ABI_OFFSET(talos_rt_aot_deopt_snapshot, ready_count, ABI_PTR(28, 24));
ABI_OFFSET(talos_rt_aot_deopt_snapshot, nbas, ABI_PTR(32, 28));
ABI_OFFSET(talos_rt_aot_deopt_snapshot, nba_count, ABI_PTR(40, 32));
ABI_OFFSET(talos_rt_aot_deopt_snapshot, reserved, ABI_PTR(44, 36));
ABI_OFFSET(talos_rt_aot_deopt_snapshot, next_sequence, ABI_PTR(48, 40));
ABI_SIZE_ALIGN(talos_rt_native_schedule_node, 12, 4);
ABI_OFFSET(talos_rt_native_schedule_node, actor_slot, 0);
ABI_OFFSET(talos_rt_native_schedule_node, continuation, 4);
ABI_OFFSET(talos_rt_native_schedule_node, fusion_group, 8);
ABI_SIZE_ALIGN(talos_rt_native_periodic_clock_v1, 48, 8);
ABI_OFFSET(talos_rt_native_periodic_clock_v1, actor_slot, 0);
ABI_OFFSET(talos_rt_native_periodic_clock_v1, continuation, 4);
ABI_OFFSET(talos_rt_native_periodic_clock_v1, static_state, 8);
ABI_OFFSET(talos_rt_native_periodic_clock_v1, reserved, 12);
ABI_OFFSET(talos_rt_native_periodic_clock_v1, bit_offset, 16);
ABI_OFFSET(talos_rt_native_periodic_clock_v1, half_period, 24);
ABI_OFFSET(talos_rt_native_periodic_clock_v1, coverage_points, 32);
ABI_OFFSET(talos_rt_native_periodic_clock_v1, coverage_point_count, 40);
ABI_SIZE_ALIGN(talos_rt_static_nba_storage, 4, 4);
ABI_SIZE_ALIGN(talos_rt_generated_nba_accumulator_256, 136, 8);
ABI_OFFSET(talos_rt_generated_nba_accumulator_256, value, 0);
ABI_OFFSET(talos_rt_generated_nba_accumulator_256, unknown, 32);
ABI_OFFSET(talos_rt_generated_nba_accumulator_256, write_mask, 64);
ABI_OFFSET(talos_rt_generated_nba_accumulator_256, valid, 96);
ABI_OFFSET(talos_rt_generated_nba_accumulator_256, exec_region, 100);
ABI_OFFSET(talos_rt_generated_nba_accumulator_256, transient, 104);
ABI_SIZE_ALIGN(talos_rt_generated_nba_slot, ABI_PTR(48, 32), 8);
ABI_OFFSET(talos_rt_generated_nba_slot, offset, 0);
ABI_OFFSET(talos_rt_generated_nba_slot, value, ABI_PTR(8, 4));
ABI_OFFSET(talos_rt_generated_nba_slot, unknown, ABI_PTR(16, 8));
ABI_OFFSET(talos_rt_generated_nba_slot, valid, ABI_PTR(24, 12));
ABI_OFFSET(talos_rt_generated_nba_slot, transition, ABI_PTR(32, 16));
ABI_OFFSET(talos_rt_generated_nba_slot, bit_width, ABI_PTR(40, 24));
ABI_SIZE_ALIGN(talos_rt_static_nba_root, ABI_PTR(40, 32), 8);
ABI_OFFSET(talos_rt_static_nba_root, commit_node, 0);
ABI_OFFSET(talos_rt_static_nba_root, static_state, 4);
ABI_OFFSET(talos_rt_static_nba_root, bit_width, 8);
ABI_OFFSET(talos_rt_static_nba_root, generated_accumulator, 16);
ABI_OFFSET(talos_rt_static_nba_root, generated_slots, ABI_PTR(24, 20));
ABI_OFFSET(talos_rt_static_nba_root, generated_slot_count, ABI_PTR(32, 24));
ABI_SIZE_ALIGN(talos_rt_static_nba_site, 16, 8);
ABI_OFFSET(talos_rt_static_nba_site, site, 0);
ABI_OFFSET(talos_rt_static_nba_site, root, 8);
ABI_OFFSET(talos_rt_static_nba_site, storage, 12);
ABI_SIZE_ALIGN(talos_rt_static_fanout_entry, 48, 8);
ABI_OFFSET(talos_rt_static_fanout_entry, static_state, 0);
ABI_OFFSET(talos_rt_static_fanout_entry, actor_slot, 4);
ABI_OFFSET(talos_rt_static_fanout_entry, continuation, 8);
ABI_OFFSET(talos_rt_static_fanout_entry, edge, 12);
ABI_OFFSET(talos_rt_static_fanout_entry, compute_node, 16);
ABI_OFFSET(talos_rt_static_fanout_entry, reserved, 20);
ABI_OFFSET(talos_rt_static_fanout_entry, low_bit, 24);
ABI_OFFSET(talos_rt_static_fanout_entry, bit_width, 32);
ABI_OFFSET(talos_rt_static_fanout_entry, kernel, 40);
ABI_OFFSET(talos_rt_static_fanout_entry, merged_bit, 44);
ABI_SIZE_ALIGN(talos_rt_native_clock_kernel, ABI_PTR(48, 40), 8);
ABI_OFFSET(talos_rt_native_clock_kernel, static_state, 0);
ABI_OFFSET(talos_rt_native_clock_kernel, edge, 4);
ABI_OFFSET(talos_rt_native_clock_kernel, low_bit, 8);
ABI_OFFSET(talos_rt_native_clock_kernel, bit_width, 16);
ABI_OFFSET(talos_rt_native_clock_kernel, ingress_mask, 24);
ABI_OFFSET(talos_rt_native_clock_kernel, ingress_word_count, ABI_PTR(32, 28));
ABI_OFFSET(talos_rt_native_clock_kernel, reserved, ABI_PTR(36, 32));
ABI_OFFSET(talos_rt_native_clock_kernel, active_mask, ABI_PTR(40, 36));
ABI_SIZE_ALIGN(talos_rt_native_merged_fragment, ABI_PTR(32, 28),
               ABI_PTR(8, 4));
ABI_OFFSET(talos_rt_native_merged_fragment, actor_slot, 0);
ABI_OFFSET(talos_rt_native_merged_fragment, continuation, 4);
ABI_OFFSET(talos_rt_native_merged_fragment, kernel, 8);
ABI_OFFSET(talos_rt_native_merged_fragment, bit, 12);
ABI_OFFSET(talos_rt_native_merged_fragment, compute_node, 16);
ABI_OFFSET(talos_rt_native_merged_fragment, flags, 20);
ABI_OFFSET(talos_rt_native_merged_fragment, execute, 24);
ABI_SIZE_ALIGN(talos_rt_static_actor_root, 16, 4);
ABI_OFFSET(talos_rt_static_actor_root, actor_slot, 0);
ABI_OFFSET(talos_rt_static_actor_root, static_state, 4);
ABI_OFFSET(talos_rt_static_actor_root, flags, 8);
ABI_OFFSET(talos_rt_static_actor_root, reserved, 12);
ABI_SIZE_ALIGN(talos_rt_native_promotion_dependency, 32, 8);
ABI_OFFSET(talos_rt_native_promotion_dependency, begin, 0);
ABI_OFFSET(talos_rt_native_promotion_dependency, end, 8);
ABI_OFFSET(talos_rt_native_promotion_dependency, subtree_end, 16);
ABI_OFFSET(talos_rt_native_promotion_dependency, certificate, 24);
ABI_SIZE_ALIGN(talos_rt_native_promotion_certificate, ABI_PTR(40, 24), 8);
ABI_OFFSET(talos_rt_native_promotion_certificate, latch, 0);
ABI_OFFSET(talos_rt_native_promotion_certificate, word, ABI_PTR(8, 4));
ABI_OFFSET(talos_rt_native_promotion_certificate, mask, ABI_PTR(16, 8));
ABI_OFFSET(talos_rt_native_promotion_certificate, route_slot,
           ABI_PTR(24, 16));
ABI_OFFSET(talos_rt_native_promotion_certificate, fallback, ABI_PTR(32, 20));
ABI_SIZE_ALIGN(talos_rt_native_ordered_nba_bridge, ABI_PTR(16, 8),
               ABI_PTR(8, 4));
ABI_OFFSET(talos_rt_native_ordered_nba_bridge, queue, 0);
ABI_OFFSET(talos_rt_native_ordered_nba_bridge, roots, ABI_PTR(8, 4));
ABI_SIZE_ALIGN(talos_rt_native_schedule_plan, ABI_PTR(288, 216), 8);
ABI_OFFSET(talos_rt_native_schedule_plan, size, 0);
ABI_OFFSET(talos_rt_native_schedule_plan, graph_layout_checksum, 8);
ABI_OFFSET(talos_rt_native_schedule_plan, mutable_state, 16);
ABI_OFFSET(talos_rt_native_schedule_plan, mutable_state_size, 24);
ABI_OFFSET(talos_rt_native_schedule_plan, actor_capacity, 32);
ABI_OFFSET(talos_rt_native_schedule_plan, flags, 36);
ABI_OFFSET(talos_rt_native_schedule_plan, state_value, 40);
ABI_OFFSET(talos_rt_native_schedule_plan, state_unknown, ABI_PTR(48, 44));
ABI_OFFSET(talos_rt_native_schedule_plan, state_bit_count, ABI_PTR(56, 48));
ABI_OFFSET(talos_rt_native_schedule_plan, bind, ABI_PTR(64, 56));
ABI_OFFSET(talos_rt_native_schedule_plan, run, ABI_PTR(72, 60));
ABI_OFFSET(talos_rt_native_schedule_plan, fallback_snapshot, ABI_PTR(80, 64));
ABI_OFFSET(talos_rt_native_schedule_plan, nba_roots, ABI_PTR(88, 68));
ABI_OFFSET(talos_rt_native_schedule_plan, nba_root_count, ABI_PTR(96, 72));
ABI_OFFSET(talos_rt_native_schedule_plan, nba_reserved, ABI_PTR(100, 76));
ABI_OFFSET(talos_rt_native_schedule_plan, nba_sites, ABI_PTR(104, 80));
ABI_OFFSET(talos_rt_native_schedule_plan, nba_site_count, ABI_PTR(112, 88));
ABI_OFFSET(talos_rt_native_schedule_plan, fanout_entries, ABI_PTR(120, 96));
ABI_OFFSET(talos_rt_native_schedule_plan, fanout_entry_count,
           ABI_PTR(128, 104));
ABI_OFFSET(talos_rt_native_schedule_plan, actor_roots, ABI_PTR(136, 112));
ABI_OFFSET(talos_rt_native_schedule_plan, actor_root_count,
           ABI_PTR(144, 120));
ABI_OFFSET(talos_rt_native_schedule_plan, nba_commit, ABI_PTR(152, 128));
ABI_OFFSET(talos_rt_native_schedule_plan, specialization_fast,
           ABI_PTR(160, 132));
ABI_OFFSET(talos_rt_native_schedule_plan, nba_dirty_roots, ABI_PTR(168, 136));
ABI_OFFSET(talos_rt_native_schedule_plan, nba_dirty_word_count,
           ABI_PTR(176, 140));
ABI_OFFSET(talos_rt_native_schedule_plan, nba_dirty_reserved,
           ABI_PTR(180, 144));
ABI_OFFSET(talos_rt_native_schedule_plan, nba_dirty_summary,
           ABI_PTR(184, 148));
ABI_OFFSET(talos_rt_native_schedule_plan, nba_dirty_summary_word_count,
           ABI_PTR(192, 152));
ABI_OFFSET(talos_rt_native_schedule_plan, nba_dirty_summary_reserved,
           ABI_PTR(196, 156));
ABI_OFFSET(talos_rt_native_schedule_plan, clock_kernels, ABI_PTR(200, 160));
ABI_OFFSET(talos_rt_native_schedule_plan, clock_kernel_count,
           ABI_PTR(208, 164));
ABI_OFFSET(talos_rt_native_schedule_plan, clock_kernel_reserved,
           ABI_PTR(212, 168));
ABI_OFFSET(talos_rt_native_schedule_plan, merged_fragments,
           ABI_PTR(216, 172));
ABI_OFFSET(talos_rt_native_schedule_plan, merged_fragment_count,
           ABI_PTR(224, 176));
ABI_OFFSET(talos_rt_native_schedule_plan, timeslot_coordinator,
           ABI_PTR(232, 184));
ABI_OFFSET(talos_rt_native_schedule_plan, promotion_invalidate,
           ABI_PTR(240, 188));
ABI_OFFSET(talos_rt_native_schedule_plan, promotion_ready, ABI_PTR(248, 192));
ABI_OFFSET(talos_rt_native_schedule_plan, promotion_invalidate_range,
           ABI_PTR(256, 196));
ABI_OFFSET(talos_rt_native_schedule_plan, promotion_recheck_range,
           ABI_PTR(264, 200));
ABI_OFFSET(talos_rt_native_schedule_plan, nba_ordered_count,
           ABI_PTR(272, 204));
ABI_OFFSET(talos_rt_native_schedule_plan, nba_ordered_bridge,
           ABI_PTR(280, 208));

static_assert(TALOS_RT_VERSION == 1);
static_assert(TALOS_RT_BYTECODE_INSTRUCTION_SIZE == 16);
static_assert(TALOS_RT_BYTECODE_REGISTER_SIZE == 16);
static_assert(TALOS_RT_OK == 0);
static_assert(TALOS_RT_EOF == 1);
static_assert(TALOS_RT_INVALID_ARGUMENT == 2);
static_assert(TALOS_RT_INVALID_HANDLE == 3);
static_assert(TALOS_RT_IO_ERROR == 4);
static_assert(TALOS_RT_OUT_OF_MEMORY == 5);
static_assert(TALOS_RT_OUT_OF_RESOURCES == 6);
static_assert(TALOS_RT_FORMAT_ERROR == 7);
static_assert(TALOS_RT_ARGUMENT_MISMATCH == 8);
static_assert(TALOS_RT_INVALID_BYTECODE == 9);
static_assert(TALOS_RT_STEP_LIMIT == 10);
static_assert(TALOS_RT_LAYOUT_MISMATCH == 11);
static_assert(TALOS_RT_INVALID_CONTINUATION == 12);
static_assert(TALOS_RT_TIER_UNAVAILABLE == 13);
static_assert(TALOS_RT_AOT_CHECKPOINT == 20);
static_assert(TALOS_RT_AOT_TIMED_CHECKPOINT == 21);
static_assert(TALOS_RT_AOT_GENERATED_CHECKPOINT == 22);
static_assert(TALOS_RT_INVALID_LIFECYCLE == 14);
static_assert(TALOS_RT_INVALID_FRAME == 15);
static_assert(TALOS_RT_INVALID_DESIGN == 16);
static_assert(TALOS_RT_PERMISSION_DENIED == 17);
static_assert(TALOS_RT_DPI_DISABLE_UNSUPPORTED == 18);
static_assert(TALOS_RT_FATAL == 19);
static_assert(TALOS_RT_DESCRIPTOR_INVALID == 0);
static_assert(TALOS_RT_DESCRIPTOR_SCOPE == 1);
static_assert(TALOS_RT_DESCRIPTOR_STORAGE == 2);
static_assert(TALOS_RT_DESCRIPTOR_NET == 3);
static_assert(TALOS_RT_DESCRIPTOR_DRIVER == 4);
static_assert(TALOS_RT_DESCRIPTOR_EVENT == 5);
static_assert(TALOS_RT_DESCRIPTOR_PROCESS == 6);
static_assert(TALOS_RT_DESCRIPTOR_FRAGMENT == 7);
static_assert(TALOS_RT_DESCRIPTOR_FUNCTION == 8);
static_assert(TALOS_RT_DESCRIPTOR_OBSERVER == 9);
static_assert(TALOS_RT_FRAGMENT_CONTINUE == 0);
static_assert(TALOS_RT_FRAGMENT_SUSPEND == 1);
static_assert(TALOS_RT_FRAGMENT_TERMINATE == 2);
static_assert(TALOS_RT_FRAGMENT_TASK_CALL == 3);
static_assert(TALOS_RT_FRAGMENT_PROCESS_SUSPEND == 4);
static_assert(TALOS_RT_SUSPEND_NONE == 0);
static_assert(TALOS_RT_SUSPEND_DELAY == 1);
static_assert(TALOS_RT_SUSPEND_CHANGE == 2);
static_assert(TALOS_RT_SUSPEND_EDGE == 3);
static_assert(TALOS_RT_SUSPEND_EVENT == 4);
static_assert(TALOS_RT_SUSPEND_EVENT_ORDER == 13);
static_assert(TALOS_RT_SUSPEND_AWAIT == 5);
static_assert(TALOS_RT_SUSPEND_JOIN == 6);
static_assert(TALOS_RT_SUSPEND_FOREVER == 7);
static_assert(TALOS_RT_SUSPEND_FRONTIER == 8);
static_assert(TALOS_RT_SUSPEND_CHILDREN == 9);
static_assert(TALOS_RT_SUSPEND_OBSERVER == 10);
static_assert(TALOS_RT_SUSPEND_MAILBOX == 11);
static_assert(TALOS_RT_SUSPEND_SEMAPHORE == 12);
static_assert(TALOS_RT_ACTION_FRAME_WAIT_RECORD == 1);
static_assert(TALOS_RT_FRAME_CAPTURE == 1);
static_assert(TALOS_RT_FRAME_CONTINUATION == 2);
static_assert(TALOS_RT_FRAME_LIVE == 3);
static_assert(TALOS_RT_FRAME_WAIT == 4);
static_assert(TALOS_RT_FRAME_FIELD_FLAGS_NONE == 0);
static_assert(TALOS_RT_FRAME_FOUR_STATE_VALUE == 1);
static_assert(TALOS_RT_FRAME_FOUR_STATE_UNKNOWN == 2);
static_assert(TALOS_RT_WAIT_EDGE_CHANGE == 0);
static_assert(TALOS_RT_WAIT_EDGE_POSEDGE == 1);
static_assert(TALOS_RT_WAIT_EDGE_NEGEDGE == 2);
static_assert(TALOS_RT_WAIT_EDGE_BOTH == 3);
static_assert(TALOS_RT_WAIT_EDGE_NONE == UINT32_MAX);
static_assert(TALOS_RT_TIER_NATIVE == 1);
static_assert(TALOS_RT_TIER_BYTECODE == 2);
static_assert(TALOS_RT_TIER_MASK_NATIVE == 1);
static_assert(TALOS_RT_TIER_MASK_BYTECODE == 2);
static_assert(TALOS_RT_PROCESS_READY == 0);
static_assert(TALOS_RT_PROCESS_EXECUTING == 1);
static_assert(TALOS_RT_PROCESS_SUSPENDED == 2);
static_assert(TALOS_RT_PROCESS_TERMINATED == 3);
static_assert(TALOS_RT_LOGICAL_PROCESS_NATIVE_TAG == UINT64_C(1) << 63);
static_assert(TALOS_RT_PROCESS_FINISHED == 0);
static_assert(TALOS_RT_PROCESS_RUNNING == 1);
static_assert(TALOS_RT_PROCESS_WAITING == 2);
static_assert(TALOS_RT_PROCESS_EXPLICITLY_SUSPENDED == 3);
static_assert(TALOS_RT_PROCESS_KILLED == 4);
static_assert(TALOS_RT_PROCESS_CONTROL_KILL == 0);
static_assert(TALOS_RT_PROCESS_CONTROL_SUSPEND == 1);
static_assert(TALOS_RT_PROCESS_CONTROL_RESUME == 2);
static_assert(TALOS_RT_PROCESS_CONTROL_CONTINUE == 0);
static_assert(TALOS_RT_PROCESS_CONTROL_SUSPEND_CURRENT == 1);
static_assert(TALOS_RT_PROCESS_CONTROL_KILL_CURRENT == 2);
static_assert(TALOS_RT_DB_PROCESS_CONTROL == 58);
static_assert(TALOS_RT_DB_BITCAST == 59);
static_assert(TALOS_RT_DB_POWER == 60);
static_assert(TALOS_RT_DB_REPLICATE == 61);
static_assert(TALOS_RT_DB_POPCOUNT == 62);
static_assert(TALOS_RT_FRAGMENT_NATIVE == 0);
static_assert(TALOS_RT_FRAGMENT_BYTECODE == 1);
static_assert(TALOS_RT_BC_TYPE_NONE == 0);
static_assert(TALOS_RT_BC_TYPE_U64 == 1);
static_assert(TALOS_RT_BC_TYPE_I64 == 2);
static_assert(TALOS_RT_BC_TYPE_BOOL == 3);
static_assert(TALOS_RT_BC_TYPE_STATUS == 4);
static_assert(TALOS_RT_BC_TYPE_RESOURCE == 5);
static_assert(TALOS_RT_BC_VALIDATION_UNVALIDATED == 0);
static_assert(TALOS_RT_BC_VALIDATION_VALID == 2);
static_assert(TALOS_RT_BC_VALIDATION_INVALID == 3);
static_assert(TALOS_RT_BC_NOP == 0);
static_assert(TALOS_RT_BC_CONST == 1);
static_assert(TALOS_RT_BC_MOVE == 2);
static_assert(TALOS_RT_BC_ADD == 3);
static_assert(TALOS_RT_BC_SUB == 4);
static_assert(TALOS_RT_BC_MUL == 5);
static_assert(TALOS_RT_BC_AND == 6);
static_assert(TALOS_RT_BC_OR == 7);
static_assert(TALOS_RT_BC_XOR == 8);
static_assert(TALOS_RT_BC_NOT == 9);
static_assert(TALOS_RT_BC_EQ == 10);
static_assert(TALOS_RT_BC_ULT == 11);
static_assert(TALOS_RT_BC_SLT == 12);
static_assert(TALOS_RT_BC_LOAD_FRAME == 13);
static_assert(TALOS_RT_BC_STORE_FRAME == 14);
static_assert(TALOS_RT_BC_JUMP == 15);
static_assert(TALOS_RT_BC_BRANCH_ZERO == 16);
static_assert(TALOS_RT_BC_CONTINUE == 17);
static_assert(TALOS_RT_BC_SUSPEND == 18);
static_assert(TALOS_RT_BC_TERMINATE == 19);
static_assert(TALOS_RT_BC_CALL_SERVICE == 20);
static_assert(TALOS_RT_BC_FAIL == 21);
static_assert(TALOS_RT_BC_OPERAND_IMMEDIATE == 0);
static_assert(TALOS_RT_BC_OPERAND_REGISTER == 1);
static_assert(TALOS_RT_BC_OPERAND_FRAME == 2);
static_assert(TALOS_RT_BC_OPERAND_CONSTANT == 3);
static_assert(TALOS_RT_BC_OPERAND_RESOURCE == 4);
static_assert(TALOS_RT_BC_OPERAND_INPUT == 0);
static_assert(TALOS_RT_BC_OPERAND_OUTPUT == 1);
static_assert(TALOS_RT_BC_OPERAND_INOUT == 2);
static_assert(TALOS_RT_BC_VALUE_NONE == 0);
static_assert(TALOS_RT_BC_VALUE_U8 == 1);
static_assert(TALOS_RT_BC_VALUE_U32 == 2);
static_assert(TALOS_RT_BC_VALUE_I32 == 3);
static_assert(TALOS_RT_BC_VALUE_U64 == 4);
static_assert(TALOS_RT_BC_VALUE_I64 == 5);
static_assert(TALOS_RT_BC_VALUE_BYTES == 6);
static_assert(TALOS_RT_BC_VALUE_MUTABLE_BYTES == 7);
static_assert(TALOS_RT_BC_VALUE_ARGUMENT_ARRAY == 8);
static_assert(TALOS_RT_BC_VALUE_FORMAT_ENVIRONMENT == 9);
static_assert(TALOS_RT_BC_VALUE_BUFFER == 10);
static_assert(TALOS_RT_BC_VALUE_ARGUMENT_EMPTY == 11);
static_assert(TALOS_RT_BC_VALUE_ARGUMENT_LOGIC == 12);
static_assert(TALOS_RT_BC_VALUE_ARGUMENT_STRING == 13);
static_assert(TALOS_RT_BC_VALUE_ARGUMENT_REAL == 14);
static_assert(TALOS_RT_BC_VALUE_ARGUMENT_TIME == 15);
static_assert(TALOS_RT_BC_SERVICE_FORMAT == 1);
static_assert(TALOS_RT_BC_SERVICE_DISPLAY == 2);
static_assert(TALOS_RT_BC_SERVICE_BUFFER_RELEASE == 3);
static_assert(TALOS_RT_BC_SERVICE_FILE_OPEN_MCD == 10);
static_assert(TALOS_RT_BC_SERVICE_FILE_OPEN == 11);
static_assert(TALOS_RT_BC_SERVICE_FILE_CLOSE == 12);
static_assert(TALOS_RT_BC_SERVICE_FILE_FLUSH == 13);
static_assert(TALOS_RT_BC_SERVICE_FILE_WRITE == 14);
static_assert(TALOS_RT_BC_SERVICE_FILE_READ == 15);
static_assert(TALOS_RT_BC_SERVICE_FILE_GETC == 16);
static_assert(TALOS_RT_BC_SERVICE_FILE_UNGETC == 17);
static_assert(TALOS_RT_BC_SERVICE_FILE_GETLINE == 18);
static_assert(TALOS_RT_BC_SERVICE_FILE_EOF == 19);
static_assert(TALOS_RT_BC_SERVICE_FILE_ERROR == 20);
static_assert(TALOS_RT_BC_SERVICE_FILE_SEEK == 21);
static_assert(TALOS_RT_BC_SERVICE_FILE_TELL == 22);
static_assert(TALOS_RT_BC_SERVICE_FILE_REWIND == 23);
static_assert(TALOS_RT_ARG_EMPTY == 0);
static_assert(TALOS_RT_ARG_LOGIC == 1);
static_assert(TALOS_RT_ARG_STRING == 2);
static_assert(TALOS_RT_ARG_REAL == 3);
static_assert(TALOS_RT_ARG_TIME == 4);
static_assert(TALOS_RT_ARG_MANAGED_STRING == 5);
static_assert(TALOS_RT_ARG_MANAGED_CONTAINER == 6);
static_assert(TALOS_RT_ARG_MANAGED_OBJECT == 7);
static_assert(TALOS_RT_ARG_VIRTUAL_INTERFACE == 8);
static_assert(TALOS_RT_ARG_PROCESS == 9);
static_assert(TALOS_RT_ARG_ENUM == 10);
static_assert(TALOS_RT_ARG_NET == 11);
static_assert(TALOS_RT_ARG_RAW_AGGREGATE == 12);
static_assert(TALOS_RT_ARG_SIGNED == 1);
static_assert(TALOS_RT_ARG_FORMAT_STRING == 2);
static_assert(TALOS_RT_ARG_DESIGNATED_FORMAT == 4);
static_assert(TALOS_RT_ARGUMENT_VALUE_BITS == 0);
static_assert(TALOS_RT_ARGUMENT_VALUE_CLASS == 1);
static_assert(TALOS_RT_ARGUMENT_VALUE_STRING == 2);
static_assert(TALOS_RT_RADIX_BINARY == 2);
static_assert(TALOS_RT_RADIX_OCTAL == 8);
static_assert(TALOS_RT_RADIX_DECIMAL == 10);
static_assert(TALOS_RT_RADIX_HEX == 16);
static_assert(TALOS_RT_SEEK_SET == 0);
static_assert(TALOS_RT_SEEK_CUR == 1);
static_assert(TALOS_RT_SEEK_END == 2);

using NativeFragment = talos_rt_status (*)(talos_rt_context *, void *,
                                             uint64_t, uint32_t,
                                             talos_rt_fragment_action_v1 *);
static_assert(std::is_same_v<talos_rt_native_fragment_v1, NativeFragment>);

#define ABI_FUNCTION(Name, Type)                                               \
  static_assert(std::is_same_v<decltype(&Name), Type>,                         \
                #Name " signature changed")

ABI_SIZE_ALIGN(talos_rt_native_state_fill_v1, 32, 8);
ABI_OFFSET(talos_rt_native_state_fill_v1, bit_offset, 0);
ABI_OFFSET(talos_rt_native_state_fill_v1, bit_width, 8);
ABI_OFFSET(talos_rt_native_state_fill_v1, value, 16);
ABI_OFFSET(talos_rt_native_state_fill_v1, unknown, 24);
ABI_FUNCTION(talos_rt_v1_native_state_initialize,
             talos_rt_status (*)(talos_rt_context *, uint8_t *, uint8_t *,
                                   uint64_t,
                                   const talos_rt_native_state_fill_v1 *,
                                   uint64_t));
ABI_SIZE_ALIGN(talos_rt_native_unknown_root_v1, 24, 8);
ABI_OFFSET(talos_rt_native_unknown_root_v1, bit_offset, 0);
ABI_OFFSET(talos_rt_native_unknown_root_v1, bit_width, 8);
ABI_OFFSET(talos_rt_native_unknown_root_v1, first_pattern, 16);
ABI_OFFSET(talos_rt_native_unknown_root_v1, pattern_count, 20);
ABI_FUNCTION(talos_rt_v1_native_state_bind_unknown_summary,
             talos_rt_status (*)(talos_rt_context *, uint8_t *, uint64_t,
                                   uint64_t *,
                                   const talos_rt_native_unknown_root_v1 *,
                                   uint64_t, const uint64_t *, uint64_t));
ABI_FUNCTION(talos_rt_v1_native_state_refresh_unknown_summary,
             uint32_t (*)(talos_rt_context *, const uint64_t *, uint64_t,
                          uint64_t *));
ABI_FUNCTION(talos_rt_v1_native_state_mark_unknown,
             void (*)(talos_rt_context *, uint64_t, uint64_t));

ABI_FUNCTION(talos_rt_v1_context_create,
             talos_rt_status (*)(talos_rt_context **));
ABI_FUNCTION(talos_rt_v1_context_configure_argv,
             talos_rt_status (*)(talos_rt_context *, int,
                                   const char *const *));
ABI_FUNCTION(talos_rt_v1_coverage_finalize,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint64_t,
                                   const uint8_t *, const uint8_t *, uint32_t));
ABI_FUNCTION(talos_rt_v1_coverage_point_hit,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint32_t));
ABI_FUNCTION(talos_rt_v1_coverage_toggle_transition,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint64_t,
                                   const uint8_t *, const uint8_t *));
ABI_FUNCTION(talos_rt_v1_coverage_fsm_bind,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint64_t,
                                   uint64_t, uint64_t));
ABI_FUNCTION(talos_rt_v1_coverage_toggle_bind,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint64_t,
                                   uint64_t));
ABI_FUNCTION(talos_rt_v1_coverage_toggle_seal,
             talos_rt_status (*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v1_coverage_reset,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_coverage_metric_v1,
                                   const uint8_t *, const uint8_t *));
ABI_FUNCTION(talos_rt_v1_coverage_query,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_coverage_metric_v1, uint64_t *,
                                   uint64_t *, double *));
ABI_FUNCTION(talos_rt_v1_coverage_control_definition,
             talos_rt_status (*)(talos_rt_context *, int32_t, int32_t,
                                   int32_t, talos_rt_string_v1, int32_t *));
ABI_FUNCTION(talos_rt_v1_coverage_control_instance,
             talos_rt_status (*)(talos_rt_context *, int32_t, int32_t,
                                   int32_t, uint64_t, int32_t *));
ABI_FUNCTION(talos_rt_v1_coverage_query_definition,
             talos_rt_status (*)(talos_rt_context *, int32_t, int32_t,
                                   talos_rt_string_v1, uint32_t, int32_t *));
ABI_FUNCTION(talos_rt_v1_coverage_query_instance,
             talos_rt_status (*)(talos_rt_context *, int32_t, int32_t,
                                   uint64_t, uint32_t, int32_t *));
ABI_FUNCTION(talos_rt_v1_coverage_database_save,
             talos_rt_status (*)(talos_rt_context *, int32_t,
                                   talos_rt_string_v1, int32_t *));
ABI_FUNCTION(talos_rt_v1_coverage_database_merge,
             talos_rt_status (*)(talos_rt_context *, int32_t,
                                   talos_rt_string_v1, int32_t *));
ABI_FUNCTION(talos_rt_v1_coverage_load,
             talos_rt_status (*)(talos_rt_context *, const char *,
                                   uint64_t));
ABI_FUNCTION(talos_rt_v1_coverage_save,
             talos_rt_status (*)(talos_rt_context *, const char *, uint64_t,
                                   uint32_t, uint64_t));
ABI_FUNCTION(talos_rt_v1_coverage_snapshot,
             talos_rt_status (*)(talos_rt_context *, uint32_t, uint64_t));
ABI_SIZE_ALIGN(talos_rt_functional_value_v1, ABI_PTR(64, 56), 8);
ABI_OFFSET(talos_rt_functional_value_v1, id, 0);
ABI_OFFSET(talos_rt_functional_value_v1, bit_width, 8);
ABI_OFFSET(talos_rt_functional_value_v1, value_size, 16);
ABI_OFFSET(talos_rt_functional_value_v1, value, 24);
ABI_OFFSET(talos_rt_functional_value_v1, unknown, ABI_PTR(32, 28));
ABI_OFFSET(talos_rt_functional_value_v1, owner, ABI_PTR(40, 32));
ABI_OFFSET(talos_rt_functional_value_v1, payload, ABI_PTR(48, 40));
ABI_OFFSET(talos_rt_functional_value_v1, kind, ABI_PTR(56, 48));
ABI_OFFSET(talos_rt_functional_value_v1, argument_ref_kind, ABI_PTR(60, 52));

#undef ABI_PTR
#undef ABI_OFFSET
#undef ABI_SIZE_ALIGN
ABI_FUNCTION(talos_rt_v1_covergroup_prepare,
             talos_rt_status (*)(talos_rt_context *, uint64_t,
                                   const talos_rt_functional_value_v1 *,
                                   uint64_t, uint64_t *));
ABI_FUNCTION(talos_rt_v1_covergroup_candidates,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint64_t,
                                   talos_rt_object_v1 *, uint32_t));
ABI_FUNCTION(talos_rt_v1_covergroup_bind_with,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint64_t,
                                   talos_rt_object_v1 *));
ABI_FUNCTION(talos_rt_v1_covergroup_release_preparation,
             talos_rt_status (*)(talos_rt_context *, uint64_t));
ABI_FUNCTION(talos_rt_v1_covergroup_create,
             talos_rt_status (*)(talos_rt_context *, uint64_t,
                                   const talos_rt_functional_value_v1 *,
                                   uint64_t,
                                   const talos_rt_functional_value_v1 *,
                                   uint64_t, talos_rt_covergroup_v1 *));
ABI_FUNCTION(talos_rt_v1_covergroup_set_enabled,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_covergroup_v1, uint64_t,
                                   uint32_t));
ABI_FUNCTION(talos_rt_v1_covergroup_set_name,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_covergroup_v1,
                                   talos_rt_string_v1));
ABI_FUNCTION(talos_rt_v1_covergroup_set_integer_option,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_covergroup_v1, uint64_t,
                                   talos_rt_covergroup_instance_option_v1,
                                   int64_t));
ABI_FUNCTION(talos_rt_v1_covergroup_get_integer_option,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_covergroup_v1, uint64_t,
                                   talos_rt_covergroup_instance_option_v1,
                                   int64_t *));
ABI_FUNCTION(talos_rt_v1_covergroup_set_string_option,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_covergroup_v1, uint64_t,
                                   talos_rt_covergroup_instance_option_v1,
                                   talos_rt_string_v1));
ABI_FUNCTION(talos_rt_v1_covergroup_get_string_option,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                    talos_rt_covergroup_v1, uint64_t,
                                    talos_rt_covergroup_instance_option_v1,
                                    talos_rt_string_v1 *));
ABI_FUNCTION(talos_rt_v1_covergroup_set_type_integer_option,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint64_t,
                                   talos_rt_covergroup_instance_option_v1,
                                   int64_t));
ABI_FUNCTION(talos_rt_v1_covergroup_set_type_string_option,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint64_t,
                                   talos_rt_covergroup_instance_option_v1,
                                   talos_rt_string_v1));
ABI_FUNCTION(talos_rt_v1_covergroup_sample_enabled,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_covergroup_v1, uint32_t *));
ABI_FUNCTION(talos_rt_v1_covergroup_clock_event_register,
             talos_rt_status (*)(talos_rt_context *,
                                   const talos_rt_computed_wait_record_v1 *,
                                   uint64_t, uint32_t, uint64_t,
                                   const talos_rt_computed_capture_v1 *,
                                   uint32_t));
ABI_FUNCTION(talos_rt_v1_covergroup_block_event_register,
             talos_rt_status (*)(
                 talos_rt_context *, talos_rt_covergroup_v1,
                 talos_rt_object_v1 *, uint64_t,
                 const talos_rt_computed_capture_v1 *, uint32_t,
                 const uint64_t *,
                 const talos_rt_covergroup_block_event_kind_v1 *, uint32_t));
ABI_FUNCTION(talos_rt_v1_covergroup_block_event_fire,
             talos_rt_status (*)(talos_rt_context *, uint64_t,
                                   talos_rt_covergroup_block_event_kind_v1,
                                   talos_rt_object_v1 *));
ABI_FUNCTION(talos_rt_v1_covergroup_sample,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_covergroup_v1,
                                   const talos_rt_functional_value_v1 *,
                                   uint64_t));
ABI_FUNCTION(talos_rt_v1_covergroup_formal_read,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_covergroup_v1, uint64_t,
                                   talos_rt_functional_value_kind_v1,
                                   uint64_t, uint64_t, const uint8_t *,
                                   const uint8_t *, uint64_t, void *, void *));
ABI_FUNCTION(talos_rt_v1_covergroup_instance_query,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_covergroup_v1, uint64_t, double *,
                                   int32_t *, int32_t *));
ABI_FUNCTION(talos_rt_v1_covergroup_type_query,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint64_t,
                                   double *, int32_t *, int32_t *));
ABI_FUNCTION(talos_rt_v1_context_seed,
             talos_rt_status (*)(talos_rt_context *, uint64_t));
ABI_FUNCTION(talos_rt_v1_random_next,
             talos_rt_status (*)(talos_rt_context *, uint64_t *));
ABI_FUNCTION(talos_rt_v1_random_initialization_enter,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint64_t *));
ABI_FUNCTION(talos_rt_v1_random_initialization_leave,
             talos_rt_status (*)(talos_rt_context *, uint64_t));
ABI_FUNCTION(talos_rt_v1_random_legacy,
             talos_rt_status (*)(talos_rt_context *, int32_t *));
ABI_FUNCTION(talos_rt_v1_random_seed,
             talos_rt_status (*)(talos_rt_context *, uint64_t));
ABI_FUNCTION(talos_rt_v1_random_bounded,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint64_t *));
ABI_FUNCTION(talos_rt_v1_random_solve,
             talos_rt_status (*)(talos_rt_context *, const uint8_t *,
                                   uint64_t, uint64_t, uint64_t,
                                   const uint64_t *, uint64_t, uint64_t *,
                                   uint32_t *));
ABI_FUNCTION(talos_rt_v1_random_solve_masked,
             talos_rt_status (*)(talos_rt_context *, const uint8_t *,
                                   uint64_t, uint64_t, uint64_t, uint64_t,
                                   const uint64_t *, uint64_t, uint64_t *,
                                   uint32_t *));
ABI_FUNCTION(talos_rt_v1_random_solve_modes,
             talos_rt_status (*)(talos_rt_context *, const uint8_t *,
                                   uint64_t, uint64_t, uint64_t, uint64_t,
                                   uint64_t, const uint64_t *, uint64_t,
                                   uint64_t *, uint32_t *));
ABI_FUNCTION(talos_rt_v1_random_solve_modes_state,
             talos_rt_status (*)(talos_rt_context *, const uint8_t *,
                                   uint64_t, uint64_t, uint64_t, uint64_t,
                                   uint64_t, uint64_t, uint64_t,
                                   const uint64_t *, uint64_t, uint64_t *,
                                   uint32_t *, uint64_t *));
ABI_FUNCTION(talos_rt_v1_random_solve_wide_modes_state,
             talos_rt_status (*)(talos_rt_context *, const uint8_t *,
                                   uint64_t, const uint64_t *, const uint64_t *,
                                   uint64_t, uint64_t, uint64_t, uint64_t,
                                   uint64_t, const uint64_t *, uint64_t,
                                   const uint32_t *, uint64_t, uint64_t *,
                                   uint32_t *, uint64_t *));
ABI_FUNCTION(talos_rt_v1_random_cycle_sample,
             talos_rt_status (*)(
                 talos_rt_context *, const uint8_t *, uint64_t,
                 const uint64_t *, const uint64_t *, uint64_t, uint64_t,
                 uint64_t, uint64_t, uint64_t, uint64_t, talos_rt_string_v1,
                 uint32_t, uint32_t, const uint64_t *, uint64_t,
                 const uint32_t *, uint64_t, uint64_t *, uint32_t *, uint64_t *,
                 uint64_t *, uint64_t *, talos_rt_string_v1 *));
ABI_FUNCTION(talos_rt_v1_random_get_state,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_random_state_v1 *));
ABI_FUNCTION(talos_rt_v1_random_set_state,
             talos_rt_status (*)(talos_rt_context *,
                                   const talos_rt_random_state_v1 *));
ABI_FUNCTION(talos_rt_v1_random_state_seed,
             void (*)(talos_rt_random_state_v1 *, uint64_t, uint64_t));
ABI_FUNCTION(talos_rt_v1_random_state_next32,
             uint32_t (*)(talos_rt_random_state_v1 *));
ABI_FUNCTION(talos_rt_v1_random_state_next64,
             uint64_t (*)(talos_rt_random_state_v1 *));
ABI_FUNCTION(talos_rt_v1_random_state_bounded,
             talos_rt_status (*)(talos_rt_random_state_v1 *, uint64_t,
                                   uint64_t *));
ABI_FUNCTION(talos_rt_v1_random_real64_to_rank,
             talos_rt_status (*)(double, uint64_t *, uint64_t));
ABI_FUNCTION(talos_rt_v1_random_real32_to_rank,
             talos_rt_status (*)(float, uint64_t *, uint64_t));
ABI_FUNCTION(talos_rt_v1_random_real64_from_rank,
             talos_rt_status (*)(const uint64_t *, uint64_t, double *));
ABI_FUNCTION(talos_rt_v1_random_real32_from_rank,
             talos_rt_status (*)(const uint64_t *, uint64_t, float *));
ABI_FUNCTION(talos_rt_v1_random_real64_interval,
             talos_rt_status (*)(talos_rt_random_state_v1 *, double, double,
                                   uint32_t, double *, uint32_t *));
ABI_FUNCTION(talos_rt_v1_random_real32_interval,
             talos_rt_status (*)(talos_rt_random_state_v1 *, float, float,
                                   uint32_t, float *, uint32_t *));
ABI_FUNCTION(talos_rt_v1_sampled_read,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint64_t,
                                   uint8_t *, uint8_t *));
ABI_FUNCTION(talos_rt_v1_sampled_history,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint64_t,
                                   uint64_t, uint32_t, uint32_t,
                                   const uint8_t *, const uint8_t *, uint8_t *,
                                   uint8_t *));
ABI_FUNCTION(talos_rt_v1_clocked_sample_update,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint64_t,
                                   uint64_t, uint32_t, uint32_t,
                                   const uint8_t *, const uint8_t *));
ABI_FUNCTION(talos_rt_v1_clocked_sample_read,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint64_t,
                                   uint64_t, uint64_t, uint32_t, uint8_t *,
                                   uint8_t *));
ABI_FUNCTION(talos_rt_v1_assertion_control,
             talos_rt_status (*)(talos_rt_context *, uint32_t, uint64_t));
ABI_FUNCTION(talos_rt_v1_assertion_enabled,
             uint32_t (*)(talos_rt_context *, uint64_t));
ABI_FUNCTION(talos_rt_v1_assertion_action_state,
             uint32_t (*)(talos_rt_context *, uint64_t));
ABI_FUNCTION(talos_rt_v1_assertion_kill_epoch,
             uint64_t (*)(talos_rt_context *, uint64_t));
ABI_FUNCTION(talos_rt_v1_context_create_for_design,
             talos_rt_status (*)(const talos_rt_execution_descriptor_v1 *,
                                   talos_rt_context **));
ABI_FUNCTION(talos_rt_v1_import_id, uint32_t (*)(const uint8_t *, uint64_t));
ABI_FUNCTION(talos_rt_v1_context_register_import,
             talos_rt_status (*)(talos_rt_context *, uint32_t,
                                   talos_rt_import_callback_v1, void *));
ABI_FUNCTION(talos_rt_v1_context_register_import_signature,
             talos_rt_status (*)(talos_rt_context *, uint32_t, uint64_t,
                                   talos_rt_import_callback_v1, void *));
ABI_FUNCTION(talos_rt_v1_import_call,
             talos_rt_status (*)(talos_rt_context *,
                                   const talos_rt_import_site_v1 *,
                                   const talos_rt_import_input_v1 *, uint32_t,
                                   talos_rt_import_output_v1 *, uint32_t));
ABI_FUNCTION(talos_rt_v1_import_call_noncontext,
             talos_rt_status (*)(talos_rt_context *,
                                   const talos_rt_import_site_v1 *,
                                   const talos_rt_import_input_v1 *, uint32_t,
                                   talos_rt_import_output_v1 *, uint32_t));
ABI_FUNCTION(talos_rt_v1_import_call_guarded,
             talos_rt_status (*)(talos_rt_status, talos_rt_context *,
                                   const talos_rt_import_site_v1 *,
                                   const talos_rt_import_input_v1 *, uint32_t,
                                   talos_rt_import_output_v1 *, uint32_t));
ABI_FUNCTION(talos_rt_v1_import_call_noncontext_guarded,
             talos_rt_status (*)(talos_rt_status, talos_rt_context *,
                                   const talos_rt_import_site_v1 *,
                                   const talos_rt_import_input_v1 *, uint32_t,
                                   talos_rt_import_output_v1 *, uint32_t));
ABI_FUNCTION(talos_rt_v1_export_call,
             talos_rt_status (*)(uint32_t, uint64_t,
                                   const talos_rt_import_input_v1 *, uint32_t,
                                   talos_rt_import_output_v1 *, uint32_t));
ABI_FUNCTION(talos_rt_v1_export_call_guarded,
             talos_rt_status (*)(talos_rt_status, uint32_t, uint64_t,
                                   const talos_rt_import_input_v1 *, uint32_t,
                                   talos_rt_import_output_v1 *, uint32_t));
ABI_FUNCTION(talos_rt_v1_dpi_export_status,
             talos_rt_status (*)(talos_rt_status));
ABI_FUNCTION(talos_rt_v1_export_string, const char *(*)(uint32_t));
ABI_FUNCTION(talos_rt_v1_dpi_export_unpack_vector,
             void (*)(const void *, void *, void *, uint32_t, uint32_t));
ABI_FUNCTION(talos_rt_v1_dpi_export_pack_vector,
             void (*)(void *, const void *, const void *, uint32_t, uint32_t));
ABI_FUNCTION(talos_rt_v1_dpi_string_copy,
             talos_rt_status (*)(talos_rt_context *, const char *,
                                   talos_rt_string_v1 *));
ABI_FUNCTION(talos_rt_v1_context_destroy, void (*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v1_status_string, const char *(*)(talos_rt_status));
ABI_FUNCTION(talos_rt_v1_buffer_release, void (*)(talos_rt_buffer_v1 *));
ABI_FUNCTION(talos_rt_v1_last_error,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_buffer_v1 *));
ABI_FUNCTION(talos_rt_v1_format,
             talos_rt_status (*)(talos_rt_context *, const char *, uint64_t,
                                   const talos_rt_arg_v1 *, uint64_t,
                                   const talos_rt_format_env_v1 *,
                                   talos_rt_buffer_v1 *));
ABI_FUNCTION(talos_rt_v1_string_output_format,
             talos_rt_status (*)(talos_rt_context *, talos_rt_radix,
                                   const talos_rt_arg_v1 *, uint64_t,
                                   const talos_rt_format_env_v1 *,
                                   talos_rt_string_v1 *));
ABI_FUNCTION(talos_rt_v1_display,
             talos_rt_status (*)(talos_rt_context *, uint32_t, uint32_t,
                                   talos_rt_radix, const talos_rt_arg_v1 *,
                                   uint64_t, const talos_rt_format_env_v1 *));
ABI_FUNCTION(talos_rt_v1_time_scan_scale,
             double (*)(talos_rt_context *, double, uint64_t, int32_t));
ABI_FUNCTION(talos_rt_v1_monitor_register,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint32_t));
ABI_FUNCTION(talos_rt_v1_monitor_register_logical,
             talos_rt_status (*)(talos_rt_context *, uint64_t));
ABI_FUNCTION(talos_rt_v1_monitor_control,
             talos_rt_status (*)(talos_rt_context *, uint32_t));
ABI_FUNCTION(talos_rt_v1_monitor_current, uint32_t (*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v1_scheduler_finish,
             talos_rt_status (*)(talos_rt_context *, uint32_t));
ABI_FUNCTION(talos_rt_v1_scheduler_program_exit,
             talos_rt_status (*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v1_scheduler_program_register,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint64_t));
ABI_FUNCTION(talos_rt_v1_scheduler_stop,
             talos_rt_status (*)(talos_rt_context *, uint32_t));
ABI_FUNCTION(talos_rt_v1_scheduler_fatal,
             talos_rt_status (*)(talos_rt_context *, uint32_t));
ABI_FUNCTION(talos_rt_v1_scheduler_error,
             talos_rt_status (*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v1_scheduler_report_status,
             void (*)(talos_rt_context *, talos_rt_status));
ABI_FUNCTION(talos_rt_v1_vpi_object_model,
             const talos_rt_vpi_object_model_v1 *(*)(void));
ABI_FUNCTION(talos_rt_v1_vpi_startup,
             talos_rt_status (*)(talos_rt_context *, const char *const *,
                                   uint64_t));
ABI_FUNCTION(talos_rt_v1_vpi_end_compile,
             talos_rt_status (*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v1_vpi_start_simulation,
             talos_rt_status (*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v1_vpi_end_simulation,
             talos_rt_status (*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v1_vpi_shutdown, void (*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v1_vpi_frame_enter,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint32_t,
                                   uint64_t, uint64_t *));
ABI_FUNCTION(talos_rt_v1_vpi_call_prepare,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint64_t *));
ABI_FUNCTION(talos_rt_v1_vpi_resolution_prepare,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint64_t *));
ABI_FUNCTION(talos_rt_v1_vpi_export_prepare,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint64_t *));
ABI_FUNCTION(talos_rt_v1_vpi_frame_enter_prepared,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint32_t,
                                   uint64_t *));
ABI_FUNCTION(talos_rt_v1_vpi_call_finish,
             talos_rt_status (*)(talos_rt_context *, uint64_t));
ABI_FUNCTION(talos_rt_v1_vpi_frame_bind,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint64_t));
ABI_FUNCTION(talos_rt_v1_vpi_frame_bind_alias,
             talos_rt_status (*)(talos_rt_context *, uint64_t,
                                   talos_rt_object_v1 *, uint64_t, uint32_t));
ABI_FUNCTION(talos_rt_v1_vpi_frame_checkpoint,
             talos_rt_status (*)(talos_rt_context *, uint64_t));
ABI_FUNCTION(talos_rt_v1_vpi_frame_leave,
             talos_rt_status (*)(talos_rt_context *, uint64_t));
ABI_FUNCTION(talos_rt_v1_vpi_frame_depth,
             talos_rt_status (*)(talos_rt_context *, uint64_t *));
ABI_FUNCTION(talos_rt_v1_vpi_frame_unwind,
             talos_rt_status (*)(talos_rt_context *, uint64_t));
ABI_FUNCTION(talos_rt_v1_scheduler_termination_requested,
             uint32_t (*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v1_scheduler_program_owned,
             uint32_t (*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v1_scheduler_time, uint64_t (*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v1_dump_open_string,
             talos_rt_status (*)(talos_rt_context *, talos_rt_string_v1));
ABI_FUNCTION(talos_rt_v1_dump_ports,
             talos_rt_status (*)(talos_rt_context *, talos_rt_string_v1,
                                   talos_rt_string_v1, int32_t));
ABI_FUNCTION(talos_rt_v1_dump_ports_control,
             talos_rt_status (*)(talos_rt_context *, talos_rt_string_v1,
                                   uint32_t, uint64_t));
ABI_FUNCTION(talos_rt_v1_file_open_mcd,
             talos_rt_status (*)(talos_rt_context *, const char *, uint64_t,
                                   uint32_t *));
ABI_FUNCTION(talos_rt_v1_file_open,
             talos_rt_status (*)(talos_rt_context *, const char *, uint64_t,
                                   const char *, uint64_t, uint32_t *));
ABI_FUNCTION(talos_rt_v1_file_open_string_mcd,
             talos_rt_status (*)(talos_rt_context *, talos_rt_string_v1,
                                   uint32_t *));
ABI_FUNCTION(talos_rt_v1_file_open_string,
             talos_rt_status (*)(talos_rt_context *, talos_rt_string_v1,
                                   talos_rt_string_v1, uint32_t *));
ABI_FUNCTION(talos_rt_v1_file_close,
             talos_rt_status (*)(talos_rt_context *, uint32_t));
ABI_FUNCTION(talos_rt_v1_file_flush,
             talos_rt_status (*)(talos_rt_context *, uint32_t));
ABI_FUNCTION(talos_rt_v1_file_write,
             talos_rt_status (*)(talos_rt_context *, uint32_t, const void *,
                                   uint64_t, uint64_t *));
ABI_FUNCTION(talos_rt_v1_file_read,
             talos_rt_status (*)(talos_rt_context *, uint32_t, void *,
                                   uint64_t, uint64_t *));
ABI_FUNCTION(talos_rt_v1_file_readmem_token,
             talos_rt_status (*)(talos_rt_context *, uint32_t, uint32_t,
                                   uint64_t, void *, uint64_t, void *, uint64_t,
                                   uint32_t *, uint64_t *));
ABI_FUNCTION(talos_rt_v1_file_getc,
             talos_rt_status (*)(talos_rt_context *, uint32_t, uint8_t *));
ABI_FUNCTION(talos_rt_v1_file_ungetc,
             talos_rt_status (*)(talos_rt_context *, uint32_t, uint8_t));
ABI_FUNCTION(talos_rt_v1_file_getline,
             talos_rt_status (*)(talos_rt_context *, uint32_t, uint64_t,
                                   talos_rt_buffer_v1 *));
ABI_FUNCTION(talos_rt_v1_file_getline_string,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_gc_lane_v1 *, uint32_t,
                                   talos_rt_string_v1 *, uint32_t *));
ABI_FUNCTION(talos_rt_v1_file_scan_field,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_gc_lane_v1 *, uint32_t, uint32_t,
                                   const char *, uint64_t, uint32_t, uint64_t,
                                   talos_rt_string_v1 *, uint32_t *,
                                   uint32_t *));
ABI_FUNCTION(talos_rt_v1_file_scan_dynamic,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_gc_lane_v1 *, uint32_t,
                                   talos_rt_string_v1, uint32_t, uint32_t,
                                   uint32_t, uint64_t, uint64_t, uint64_t,
                                   talos_rt_string_v1 *, uint32_t *,
                                   uint32_t *, uint32_t *, uint32_t *));
ABI_FUNCTION(talos_rt_v1_file_scan_raw,
             talos_rt_status (*)(talos_rt_context *, uint32_t, uint32_t,
                                   const char *, uint64_t, uint64_t, uint64_t,
                                   uint32_t, uint64_t, void *, uint64_t, void *,
                                   uint64_t, uint32_t *, uint32_t *));
ABI_FUNCTION(talos_rt_v1_file_eof,
             talos_rt_status (*)(talos_rt_context *, uint32_t, uint32_t *));
ABI_FUNCTION(talos_rt_v1_file_error,
             talos_rt_status (*)(talos_rt_context *, uint32_t, int32_t *,
                                   talos_rt_buffer_v1 *));
ABI_FUNCTION(talos_rt_v1_file_seek,
             talos_rt_status (*)(talos_rt_context *, uint32_t, int64_t,
                                   talos_rt_seek_origin));
ABI_FUNCTION(talos_rt_v1_file_tell,
             talos_rt_status (*)(talos_rt_context *, uint32_t, int64_t *));
ABI_FUNCTION(talos_rt_v1_file_rewind,
             talos_rt_status (*)(talos_rt_context *, uint32_t));
ABI_FUNCTION(talos_rt_v1_fragment_execute,
             talos_rt_status (*)(const talos_rt_fragment_descriptor_v1 *,
                                   talos_rt_context *, void *, uint64_t,
                                   uint32_t, talos_rt_fragment_action_v1 *));
ABI_FUNCTION(talos_rt_v1_bytecode_execute_bounded,
             talos_rt_status (*)(const talos_rt_fragment_descriptor_v1 *,
                                   talos_rt_context *, void *, uint64_t,
                                   uint32_t, uint64_t,
                                   talos_rt_fragment_action_v1 *));
ABI_FUNCTION(talos_rt_v1_process_instance_create,
             talos_rt_status (*)(const talos_rt_process_descriptor_v1 *,
                                   talos_rt_process_instance_v1 **));
ABI_FUNCTION(talos_rt_v1_process_spawn,
             uint64_t (*)(talos_rt_context *,
                          const talos_rt_process_spawn_plan_v1 *,
                          const void *));
ABI_FUNCTION(talos_rt_v1_process_spawn_batch,
             talos_rt_status (*)(talos_rt_context *,
                                   const talos_rt_process_spawn_entry_v1 *,
                                   const void *, uint32_t));
ABI_FUNCTION(talos_rt_v1_process_instance_frame,
             talos_rt_status (*)(talos_rt_process_instance_v1 *, void **,
                                   uint64_t *));
ABI_FUNCTION(talos_rt_v1_process_instance_execute,
             talos_rt_status (*)(talos_rt_process_instance_v1 *,
                                   talos_rt_context *,
                                   talos_rt_execution_tier,
                                   talos_rt_fragment_action_v1 *));
ABI_FUNCTION(talos_rt_v1_process_instance_destroy,
             talos_rt_status (*)(talos_rt_process_instance_v1 *));
ABI_FUNCTION(talos_rt_v1_dpi_export_task_run,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_process_instance_v1 *));
ABI_FUNCTION(talos_rt_v1_dpi_export_task_bytecode_run,
             talos_rt_status (*)(talos_rt_context *, uint32_t,
                                   const talos_rt_import_input_v1 *, uint32_t,
                                   talos_rt_import_output_v1 *, uint32_t,
                                   const uint8_t *, const int64_t *const *,
                                   const uint64_t *));
ABI_FUNCTION(talos_rt_v1_dpi_open_array_pack,
             talos_rt_status (*)(const void *, const void *, uint64_t,
                                   uint64_t, uint32_t, uint32_t, uint32_t,
                                   uint64_t, uint32_t, void *, uint64_t));
ABI_FUNCTION(talos_rt_v1_dpi_open_array_unpack,
             talos_rt_status (*)(const void *, uint64_t, uint32_t, uint32_t,
                                   uint64_t, void *, void *, uint64_t, uint64_t,
                                   uint32_t, uint32_t));
ABI_FUNCTION(talos_rt_v1_dpi_open_array_aggregate_pack,
             talos_rt_status (*)(const void *, const void *, uint64_t,
                                   uint64_t, uint32_t, uint32_t, uint64_t,
                                   uint32_t, const int64_t *, uint32_t, void *,
                                   uint64_t, uint64_t, uint64_t, uint64_t,
                                   const int64_t *, uint64_t));
ABI_FUNCTION(talos_rt_v1_dpi_open_array_aggregate_unpack,
             talos_rt_status (*)(talos_rt_context *, const void *, uint64_t,
                                   uint64_t, const int64_t *, uint64_t,
                                   uint64_t, uint32_t, const int64_t *,
                                   uint32_t, void *, void *, uint64_t, uint64_t,
                                   uint32_t, uint32_t));
ABI_FUNCTION(talos_rt_v1_dpi_open_array_prepare_recursive,
             talos_rt_status (*)(const void *, const void *, uint64_t,
                                   uint64_t, uint32_t, uint32_t, uint32_t,
                                   uint32_t, uint32_t, int32_t, int32_t,
                                   uint64_t, uint32_t, uint64_t,
                                   const int64_t *, uint64_t, const int64_t *,
                                   uint32_t,
                                   talos_rt_dpi_open_array_storage_v1 *));
ABI_FUNCTION(talos_rt_v1_dpi_open_array_finish_recursive,
             talos_rt_status (*)(talos_rt_status, talos_rt_context *,
                                   const talos_rt_dpi_open_array_storage_v1 *,
                                   void *, void *, uint64_t, uint64_t, uint32_t,
                                   uint32_t, uint32_t, uint64_t,
                                   const int64_t *, uint64_t, const int64_t *,
                                   uint32_t));
ABI_FUNCTION(talos_rt_v1_dpi_open_array_release_recursive,
             void (*)(talos_rt_dpi_open_array_storage_v1 *));
ABI_FUNCTION(talos_rt_v1_dpi_aggregate_pack,
             talos_rt_status (*)(const void *, const void *, uint64_t,
                                   uint64_t, uint32_t, void *, uint64_t,
                                   uint64_t, const int64_t *, uint64_t));
ABI_FUNCTION(talos_rt_v1_dpi_aggregate_unpack,
             talos_rt_status (*)(talos_rt_context *, const void *, uint64_t,
                                   const int64_t *, uint64_t, void *, void *,
                                   uint64_t, uint64_t, uint32_t));
ABI_FUNCTION(talos_rt_v1_dpi_aggregate_roots_push,
             talos_rt_status (*)(talos_rt_context *, void *, uint64_t,
                                   uint64_t, const int64_t *, uint64_t,
                                   void **));
ABI_FUNCTION(talos_rt_v1_dpi_aggregate_roots_pop,
             talos_rt_status (*)(talos_rt_context *, void *));
ABI_FUNCTION(talos_rt_v1_dpi_aggregate_export_pack,
             talos_rt_status (*)(const void *, const void *, uint64_t,
                                   uint64_t, uint32_t, void *, uint64_t,
                                   const int64_t *, uint64_t, uint32_t));
ABI_FUNCTION(talos_rt_v1_dpi_aggregate_state_alloc,
             talos_rt_status (*)(talos_rt_context *, uint64_t,
                                   const uint8_t *, const uint8_t *,
                                   const int64_t *, uint64_t, uint64_t *));
ABI_FUNCTION(talos_rt_v1_dpi_current_context, talos_rt_context *(*)(void));
ABI_FUNCTION(talos_rt_v1_design_validate,
             talos_rt_status (*)(const talos_rt_execution_descriptor_v1 *));
ABI_FUNCTION(talos_rt_v1_design_root,
             talos_rt_status (*)(const talos_rt_execution_descriptor_v1 *,
                                   talos_rt_design_cursor_v1 *));
ABI_FUNCTION(talos_rt_v1_design_child,
             talos_rt_status (*)(const talos_rt_execution_descriptor_v1 *,
                                   talos_rt_design_cursor_v1,
                                   talos_rt_design_cursor_v1 *));
ABI_FUNCTION(talos_rt_v1_design_sibling,
             talos_rt_status (*)(const talos_rt_execution_descriptor_v1 *,
                                   talos_rt_design_cursor_v1,
                                   talos_rt_design_cursor_v1 *));
ABI_FUNCTION(talos_rt_v1_design_lookup,
             talos_rt_status (*)(const talos_rt_execution_descriptor_v1 *,
                                   const uint8_t *, uint64_t,
                                   talos_rt_design_cursor_v1 *));
ABI_FUNCTION(talos_rt_v1_design_info,
             talos_rt_status (*)(const talos_rt_execution_descriptor_v1 *,
                                   talos_rt_design_cursor_v1,
                                   talos_rt_design_info_v1 *));
ABI_FUNCTION(talos_rt_v1_design_name,
             talos_rt_status (*)(const talos_rt_execution_descriptor_v1 *,
                                   talos_rt_design_cursor_v1,
                                   const uint8_t **, uint64_t *));
ABI_FUNCTION(talos_rt_v1_design_read,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_design_cursor_v1, uint64_t *,
                                   uint64_t *, uint64_t));
ABI_FUNCTION(talos_rt_v1_design_write,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_design_cursor_v1,
                                   const uint64_t *, const uint64_t *,
                                   uint64_t));
ABI_FUNCTION(talos_rt_v1_scheduler_add,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_process_instance_v1 *, uint32_t));
ABI_FUNCTION(talos_rt_v1_scheduler_add_ranked,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_process_instance_v1 *, uint32_t,
                                   uint32_t));
ABI_FUNCTION(talos_rt_v1_scheduler_add_planned,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_process_instance_v1 *, uint32_t,
                                   uint32_t, const uint32_t *, const uint32_t *,
                                   uint32_t));
ABI_FUNCTION(talos_rt_v1_scheduler_prime,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_process_instance_v1 *));
ABI_FUNCTION(talos_rt_v1_native_promotion_invalidate_ranges,
             void (*)(const talos_rt_native_promotion_dependency *, uint64_t,
                      const talos_rt_native_promotion_certificate *,
                      talos_rt_native_promotion_invalidate, uint64_t,
                      uint64_t));
ABI_FUNCTION(talos_rt_v1_native_promotion_recheck_ranges,
             void (*)(const talos_rt_native_promotion_dependency *, uint64_t,
                      const talos_rt_native_promotion_certificate *,
                      talos_rt_native_promotion_invalidate, uint64_t,
                      uint64_t));
ABI_FUNCTION(talos_rt_v1_native_promotion_invalidate_buckets,
             void (*)(const uint32_t *, const uint64_t *, uint64_t,
                      const talos_rt_native_promotion_certificate *,
                      talos_rt_native_promotion_invalidate, uint64_t,
                      uint64_t));
ABI_FUNCTION(talos_rt_v1_native_promotion_recheck_buckets,
             void (*)(const uint32_t *, const uint64_t *, uint64_t,
                      const talos_rt_native_promotion_certificate *,
                      talos_rt_native_promotion_invalidate, uint64_t,
                      uint64_t));
ABI_FUNCTION(talos_rt_v1_scheduler_install_aot,
             talos_rt_status (*)(talos_rt_context *,
                                   const talos_rt_native_schedule_plan *));
ABI_FUNCTION(talos_rt_v1_scheduler_activate_clock_kernel,
             talos_rt_status (*)(talos_rt_context *, uint32_t, uint32_t));
ABI_FUNCTION(talos_rt_v1_scheduler_run_clock_coordinator,
             talos_rt_status (*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v1_scheduler_direct_fragment_enter,
             talos_rt_status (*)(talos_rt_context *, uint32_t, uint32_t,
                                   talos_rt_process_instance_v1 **));
ABI_FUNCTION(talos_rt_v1_scheduler_direct_fragment_leave,
             talos_rt_status (*)(talos_rt_context *, uint32_t));
ABI_FUNCTION(talos_rt_v1_scheduler_execute_aot_actor,
             talos_rt_status (*)(talos_rt_context *, uint32_t));
ABI_FUNCTION(talos_rt_v1_scheduler_priority_signal_pending,
             uint32_t (*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v1_scheduler_handoff_pending,
             uint32_t (*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v1_scheduler_queue_aot_checkpoint,
             talos_rt_status (*)(talos_rt_context *, uint32_t, uint32_t,
                                   talos_rt_native_checkpoint_callback));
ABI_FUNCTION(talos_rt_v1_scheduler_add_aot,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_process_instance_v1 *, uint32_t,
                                   uint32_t, uint32_t, const uint32_t *,
                                   const uint32_t *, uint32_t, const uint32_t *,
                                   uint32_t));
ABI_FUNCTION(talos_rt_v1_scheduler_run_aot_nodes,
             talos_rt_status (*)(talos_rt_context *,
                                   const talos_rt_native_schedule_node *,
                                   uint32_t));
ABI_FUNCTION(
    talos_rt_v1_scheduler_prepare_periodic_aot,
    talos_rt_status (*)(talos_rt_context *,
                          const talos_rt_native_schedule_node *, uint32_t,
                          const talos_rt_native_periodic_clock_v1 *, uint32_t,
                          const talos_rt_native_periodic_alias_v1 *, uint32_t,
                          uint64_t *, talos_rt_native_periodic_control_v1 *));
ABI_FUNCTION(talos_rt_v1_scheduler_handoff_periodic_aot,
             talos_rt_status (*)(talos_rt_context *,
                                   const talos_rt_native_periodic_clock_v1 *,
                                   uint32_t, const uint64_t *));
ABI_FUNCTION(talos_rt_v1_scheduler_drain_periodic_aot,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_native_periodic_control_v1 *));
ABI_FUNCTION(talos_rt_v1_scheduler_enter_periodic_aot,
             talos_rt_status (*)(talos_rt_context *, uint64_t));
ABI_FUNCTION(talos_rt_v1_scheduler_snapshot_aot,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_aot_deopt_snapshot *));
ABI_FUNCTION(talos_rt_v1_scheduler_process_token,
             uint64_t (*)(talos_rt_context *,
                          talos_rt_process_instance_v1 *));
ABI_FUNCTION(talos_rt_v1_process_current, uint64_t (*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v1_process_status,
             talos_rt_status (*)(talos_rt_context *, uint64_t,
                                   talos_rt_process_state *));
ABI_FUNCTION(talos_rt_v1_process_random_get,
             talos_rt_status (*)(talos_rt_context *, uint64_t,
                                   talos_rt_random_state_v1 *));
ABI_FUNCTION(talos_rt_v1_process_random_set,
             talos_rt_status (*)(talos_rt_context *, uint64_t,
                                   const talos_rt_random_state_v1 *));
ABI_FUNCTION(talos_rt_v1_control_disable_selected,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint64_t,
                                   uint32_t, uint64_t *));
ABI_FUNCTION(talos_rt_v1_process_control,
             talos_rt_status (*)(talos_rt_context *, uint64_t,
                                   talos_rt_process_control_kind,
                                   talos_rt_process_control_disposition *));
ABI_FUNCTION(talos_rt_v1_scheduler_nba,
             talos_rt_status (*)(talos_rt_context *, uint8_t *, uint8_t *,
                                   uint64_t, uint64_t, uint64_t, uint64_t,
                                   const uint8_t *, const uint8_t *));
ABI_FUNCTION(talos_rt_v1_scheduler_driver_nba,
             talos_rt_status (*)(talos_rt_context *, uint8_t *, uint8_t *,
                                   uint64_t, uint64_t, uint64_t, uint64_t,
                                   const uint8_t *, const uint8_t *));
ABI_FUNCTION(talos_rt_v1_scheduler_inertial_driver,
             talos_rt_status (*)(talos_rt_context *, uint8_t *, uint8_t *,
                                   uint64_t, uint64_t, uint64_t, uint64_t,
                                   uint32_t, uint32_t, uint64_t, uint64_t,
                                   uint64_t, const uint8_t *, const uint8_t *));
ABI_FUNCTION(talos_rt_v1_scheduler_inertial_path_driver,
             talos_rt_status (*)(talos_rt_context *, uint8_t *, uint8_t *,
                                   uint64_t, uint64_t, uint64_t, uint64_t,
                                   uint32_t, uint32_t, uint32_t, uint32_t,
                                   uint64_t, uint64_t, uint64_t,
                                   const uint8_t *, const uint8_t *,
                                   const uint8_t *, const uint8_t *,
                                   const uint8_t *, const uint8_t *));
ABI_FUNCTION(talos_rt_v1_scheduler_inertial_path_driver_pulse,
             talos_rt_status (*)(talos_rt_context *, uint8_t *, uint8_t *,
                                   uint64_t, uint64_t, uint64_t, uint64_t,
                                   uint32_t, uint32_t, uint32_t, uint32_t,
                                   uint64_t, uint64_t, uint64_t, uint64_t,
                                   uint64_t, const uint8_t *, const uint8_t *,
                                   const uint8_t *, const uint8_t *,
                                   const uint8_t *, const uint8_t *,
                                   const uint8_t *));
ABI_FUNCTION(talos_rt_v1_scheduler_inertial_path_storage,
             talos_rt_status (*)(
                 talos_rt_context *, uint8_t *, uint8_t *, uint64_t, uint64_t,
                 uint64_t, uint64_t, uint32_t, uint32_t, uint32_t, uint32_t,
                 uint64_t, uint64_t, uint64_t, const uint8_t *, const uint8_t *,
                 const uint8_t *, const uint8_t *, const uint8_t *,
                 const uint8_t *, const uint8_t *));
ABI_FUNCTION(talos_rt_v1_scheduler_inertial_driver_strength_pair,
             talos_rt_status (*)(talos_rt_context *, uint8_t *, uint8_t *,
                                   uint64_t, uint64_t, uint64_t, uint64_t,
                                   uint64_t, uint32_t, uint64_t, uint64_t,
                                   uint64_t, const uint8_t *, const uint8_t *,
                                   const uint8_t *, const uint8_t *,
                                   const uint8_t *, const uint8_t *));
ABI_FUNCTION(talos_rt_v1_scheduler_inertial_path_strength_pair,
             talos_rt_status (*)(
                 talos_rt_context *, uint8_t *, uint8_t *, uint64_t, uint64_t,
                 uint64_t, uint64_t, uint64_t, uint32_t, uint32_t, uint32_t,
                 uint64_t, uint64_t, uint64_t, const uint8_t *, const uint8_t *,
                 const uint8_t *, const uint8_t *, const uint8_t *,
                 const uint8_t *, const uint8_t *, const uint8_t *,
                 const uint8_t *, const uint8_t *));
ABI_FUNCTION(talos_rt_v1_scheduler_inertial_path_strength_pair_pulse,
             talos_rt_status (*)(
                 talos_rt_context *, uint8_t *, uint8_t *, uint64_t, uint64_t,
                 uint64_t, uint64_t, uint64_t, uint32_t, uint32_t, uint32_t,
                 uint32_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                 const uint8_t *, const uint8_t *, const uint8_t *,
                 const uint8_t *, const uint8_t *, const uint8_t *,
                 const uint8_t *, const uint8_t *, const uint8_t *,
                 const uint8_t *, const uint8_t *));
ABI_FUNCTION(talos_rt_v1_scheduler_resolve_drivers,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint64_t));
ABI_FUNCTION(talos_rt_v1_scheduler_resolve_initial_drivers,
             talos_rt_status (*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v1_pass_switch_control,
             talos_rt_status (*)(talos_rt_context *, uint32_t, uint32_t,
                                   uint32_t));
ABI_FUNCTION(talos_rt_v1_pass_switch_control_delayed,
             talos_rt_status (*)(talos_rt_context *, uint32_t, uint32_t,
                                   uint32_t, uint64_t, uint64_t, uint64_t));
ABI_FUNCTION(talos_rt_v1_mos_drive_delayed,
             talos_rt_status (*)(talos_rt_context *, uint32_t, uint32_t,
                                   uint32_t, uint64_t, uint64_t, uint64_t));
ABI_FUNCTION(talos_rt_v1_scheduler_clocking_nba,
             talos_rt_status (*)(talos_rt_context *, uint8_t *, uint8_t *,
                                   uint64_t, uint64_t, uint64_t, uint64_t,
                                   const uint8_t *, const uint8_t *, uint64_t));
ABI_FUNCTION(talos_rt_v1_scheduler_clocking_driver_nba,
             talos_rt_status (*)(talos_rt_context *, uint8_t *, uint8_t *,
                                   uint64_t, uint64_t, uint64_t, uint64_t,
                                   const uint8_t *, const uint8_t *, uint64_t));
ABI_FUNCTION(talos_rt_v1_scheduler_static_nba,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint8_t *,
                                   uint8_t *, uint64_t, uint64_t, uint64_t,
                                   const uint8_t *, const uint8_t *));
ABI_FUNCTION(talos_rt_v1_scheduler_static_nba_packed,
             talos_rt_status (*)(talos_rt_context *, uint32_t, uint8_t *,
                                   uint8_t *, uint64_t, uint64_t, uint64_t,
                                   uint64_t, uint64_t));
ABI_FUNCTION(talos_rt_v1_static_nba_stage_wide,
             void (*)(talos_rt_context *, uint32_t, uint64_t, uint64_t,
                      uint64_t, uint64_t, uint32_t));
ABI_FUNCTION(talos_rt_v1_static_nba_claim,
             talos_rt_status (*)(talos_rt_context *, uint32_t, uint8_t *,
                                   uint8_t *, uint64_t, uint64_t, uint64_t,
                                   uint64_t, uint64_t));
ABI_FUNCTION(talos_rt_v1_static_nba_commit_root,
             talos_rt_status (*)(talos_rt_context *, uint32_t, uint32_t,
                                   uint32_t *));
ABI_FUNCTION(talos_rt_v1_static_nba_commit_roots,
             talos_rt_status (*)(talos_rt_context *, uint32_t, uint32_t,
                                   uint32_t *));
ABI_FUNCTION(talos_rt_v1_static_nba_commit_batch,
             talos_rt_status (*)(talos_rt_context *, const uint32_t *,
                                   uint32_t, uint32_t, uint32_t *));
ABI_FUNCTION(talos_rt_v1_static_nba_batch_guard,
             uint32_t (*)(talos_rt_context *,
                          const talos_rt_native_schedule_plan *, uint32_t));
ABI_FUNCTION(talos_rt_v1_static_nba_batch_knownness,
             void (*)(talos_rt_context *, uint64_t, uint64_t, uint64_t,
                      uint64_t));
ABI_FUNCTION(talos_rt_v1_static_nba_batch_finish,
             void (*)(talos_rt_context *, uint64_t, uint64_t, uint32_t));
ABI_FUNCTION(talos_rt_v1_static_nba_commit_ordered,
             talos_rt_status (*)(talos_rt_context *, uint32_t, int64_t,
                                   uint64_t, uint64_t, uint64_t));
ABI_FUNCTION(talos_rt_v1_scheduler_string_nba,
             talos_rt_status (*)(talos_rt_context *, uint8_t *, uint64_t,
                                   uint64_t, uint64_t, talos_rt_string_v1));
ABI_FUNCTION(talos_rt_v1_scheduler_managed_nba,
             talos_rt_status (*)(talos_rt_context *, talos_rt_object_v1 *,
                                   uint64_t, const void *, const void *,
                                   uint64_t, uint64_t));
ABI_FUNCTION(talos_rt_v1_scheduler_managed_nba_masked,
             talos_rt_status (*)(talos_rt_context *, talos_rt_object_v1 *,
                                   uint64_t, const void *, const void *,
                                   const void *, uint64_t, uint64_t));
ABI_FUNCTION(talos_rt_v1_scheduler_signal,
             void (*)(talos_rt_context *, uint64_t, uint64_t, uint32_t));
ABI_FUNCTION(talos_rt_v1_scheduler_signal_transition,
             void (*)(talos_rt_context *, uint64_t, uint64_t, const uint8_t *,
                      const uint8_t *, const uint8_t *, const uint8_t *));
ABI_FUNCTION(talos_rt_v1_scheduler_inertial_path_storage_pulse,
             talos_rt_status (*)(
                 talos_rt_context *, uint8_t *, uint8_t *, uint64_t, uint64_t,
                 uint64_t, uint64_t, uint32_t, uint32_t, uint32_t, uint32_t,
                 uint32_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                 const uint8_t *, const uint8_t *, const uint8_t *,
                 const uint8_t *, const uint8_t *, const uint8_t *,
                 const uint8_t *, const uint8_t *));
ABI_FUNCTION(talos_rt_v1_scheduler_static_transition,
             void (*)(talos_rt_context *, uint32_t, uint64_t, uint64_t,
                      uint64_t, uint64_t, uint64_t, uint64_t));
ABI_FUNCTION(talos_rt_v1_scheduler_static_transition_owned,
             void (*)(talos_rt_context *, uint32_t, uint32_t, uint64_t,
                      uint64_t, uint64_t, uint64_t, uint64_t, uint64_t));
ABI_FUNCTION(talos_rt_v1_scheduler_publication_batch_begin,
             talos_rt_status (*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v1_scheduler_publication_batch_end,
             talos_rt_status (*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v1_scheduler_activate_static_nodes,
             void (*)(talos_rt_context *, const uint64_t *, uint32_t));
ABI_FUNCTION(talos_rt_v1_scheduler_real_transition,
             void (*)(talos_rt_context *, uint64_t, uint32_t, const void *,
                      const void *));
ABI_FUNCTION(talos_rt_v1_scheduler_event,
             void (*)(talos_rt_context *, uint64_t, uint32_t));
ABI_FUNCTION(talos_rt_v1_scheduler_event_create,
             talos_rt_status (*)(talos_rt_context *, uint64_t *));
ABI_FUNCTION(talos_rt_v1_scheduler_event_after,
             void (*)(talos_rt_context *, uint64_t, uint32_t, uint64_t));
ABI_FUNCTION(talos_rt_v1_scheduler_event_replace_after,
             void (*)(talos_rt_context *, uint64_t, uint32_t, uint64_t));
ABI_FUNCTION(talos_rt_v1_scheduler_event_triggered,
             uint32_t (*)(talos_rt_context *, uint64_t));
ABI_FUNCTION(talos_rt_v1_clocking_output_track,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint64_t,
                                   uint32_t));
ABI_FUNCTION(talos_rt_v1_clocking_output_current,
             uint32_t (*)(talos_rt_context *, uint64_t, uint64_t, uint32_t));
ABI_FUNCTION(talos_rt_v1_scheduler_wait_order_failed,
             uint32_t (*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v1_clock_occurrence_consume,
             uint64_t (*)(talos_rt_context *, uint64_t));
ABI_FUNCTION(talos_rt_v1_nochange_update,
             uint64_t (*)(talos_rt_context *, uint64_t, uint64_t, int64_t,
                          int64_t));
ABI_FUNCTION(talos_rt_v1_scheduler_fail,
             void (*)(talos_rt_context *, talos_rt_status));
ABI_FUNCTION(talos_rt_v1_native_state_register_static,
             talos_rt_status (*)(talos_rt_context *, uint32_t, uint64_t,
                                   uint64_t));
ABI_FUNCTION(talos_rt_v1_native_state_static_handle, uint64_t (*)(uint32_t));
ABI_FUNCTION(talos_rt_v1_native_handle_offset,
             uint64_t (*)(uint64_t, int64_t));
ABI_FUNCTION(talos_rt_v1_strength_resolve, uint16_t (*)(uint16_t, uint16_t));
ABI_FUNCTION(talos_rt_v1_strength_resolve_kind,
             uint16_t (*)(uint16_t, uint16_t, uint32_t));
ABI_FUNCTION(talos_rt_v1_native_state_alloc,
             talos_rt_status (*)(talos_rt_context *, uint64_t,
                                   const uint8_t *, const uint8_t *,
                                   uint64_t *));
ABI_FUNCTION(talos_rt_v1_native_state_alloc_with_roots,
             talos_rt_status (*)(talos_rt_context *, uint64_t,
                                   const uint8_t *, const uint8_t *,
                                   const uint64_t *, uint64_t, uint64_t *));
ABI_FUNCTION(talos_rt_v1_native_state_alloc_with_typed_roots,
             talos_rt_status (*)(talos_rt_context *, uint64_t,
                                   const uint8_t *, const uint8_t *,
                                   const talos_rt_managed_root_slot_v1 *,
                                   uint64_t, uint64_t *));
ABI_FUNCTION(talos_rt_v1_native_state_retain,
             talos_rt_status (*)(talos_rt_context *, uint64_t));
ABI_FUNCTION(talos_rt_v1_native_state_register_managed_roots,
             talos_rt_status (*)(talos_rt_context *, uint64_t,
                                   const uint64_t *, uint64_t));
ABI_FUNCTION(talos_rt_v1_native_state_release,
             talos_rt_status (*)(talos_rt_context *, uint64_t, uint32_t));
ABI_FUNCTION(talos_rt_v1_native_state_load_plane,
             talos_rt_status (*)(talos_rt_context *, const uint8_t *,
                                   uint64_t, uint64_t, uint64_t, uint32_t,
                                   uint32_t, uint8_t *));
ABI_FUNCTION(talos_rt_v1_native_state_store_plane,
             talos_rt_status (*)(talos_rt_context *, uint8_t *, uint64_t,
                                   uint64_t, uint64_t, uint32_t,
                                   const uint8_t *, uint8_t *));
ABI_FUNCTION(talos_rt_v1_native_state_store_continuous_plane,
             talos_rt_status (*)(talos_rt_context *, uint8_t *, uint64_t,
                                   uint64_t, uint64_t, uint32_t,
                                   const uint8_t *, uint8_t *));
ABI_FUNCTION(talos_rt_v1_scheduler_notify, void (*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v1_scheduler_run,
             talos_rt_status (*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v1_scheduler_run_aot,
             talos_rt_status (*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v1_gc_lane_create,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_gc_lane_v1 **));
ABI_FUNCTION(talos_rt_v1_gc_lane_destroy,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *));
ABI_FUNCTION(talos_rt_v1_gc_lane_enter,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *));
ABI_FUNCTION(talos_rt_v1_gc_lane_leave,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *));
ABI_FUNCTION(talos_rt_v1_gc_safepoint,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *));
ABI_FUNCTION(talos_rt_v1_gc_current_lane,
             talos_rt_gc_lane_v1 *(*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v1_gc_root_push,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_gc_root_v1 *,
                                   talos_rt_object_v1 **));
ABI_FUNCTION(talos_rt_v1_gc_root_pop,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_gc_root_v1 *));
ABI_FUNCTION(talos_rt_v1_gc_root_range_push,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_gc_root_range_v1 *,
                                   talos_rt_object_v1 **, uint64_t));
ABI_FUNCTION(talos_rt_v1_gc_root_range_pop,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_gc_root_range_v1 *));
ABI_FUNCTION(talos_rt_v1_gc_managed_root_push,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_gc_managed_root_v1 *,
                                   talos_rt_managed_word_v1 *));
ABI_FUNCTION(talos_rt_v1_gc_managed_root_pop,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_gc_managed_root_v1 *));
ABI_FUNCTION(talos_rt_v1_gc_managed_root_range_push,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_gc_managed_root_range_v1 *,
                                   talos_rt_managed_word_v1 *, uint64_t));
ABI_FUNCTION(talos_rt_v1_gc_managed_root_range_pop,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_gc_managed_root_range_v1 *));
ABI_FUNCTION(talos_rt_v1_gc_candidate_root,
             talos_rt_managed_word_v1 (*)(
                 talos_rt_context *, talos_rt_managed_word_v1,
                 talos_rt_managed_root_kind_mask_v1));
ABI_FUNCTION(talos_rt_v1_argument_ref_load,
             talos_rt_status (*)(talos_rt_context *, const uint8_t *,
                                   const uint8_t *, uint64_t,
                                   talos_rt_object_v1 *, uint64_t, uint32_t,
                                   uint64_t, uint64_t, uint32_t, uint32_t,
                                   void *, void *));
ABI_FUNCTION(talos_rt_v1_argument_ref_store,
             talos_rt_status (*)(talos_rt_context *, uint8_t *, uint8_t *,
                                   uint64_t, talos_rt_object_v1 *, uint64_t,
                                   uint32_t, uint64_t, uint64_t, uint32_t,
                                   uint32_t, const void *, const void *));
ABI_FUNCTION(talos_rt_v1_gc_static_root_register,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_object_v1 **));
ABI_FUNCTION(talos_rt_v1_gc_candidate_static_root_register,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_managed_word_v1 *,
                                   talos_rt_managed_root_kind_mask_v1));
ABI_FUNCTION(talos_rt_v1_gc_static_root_unregister,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_object_v1 **));
ABI_FUNCTION(talos_rt_v1_gc_pin,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_object_v1 *));
ABI_FUNCTION(talos_rt_v1_gc_unpin,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_object_v1 *));
ABI_FUNCTION(talos_rt_v1_gc_collect,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *));
ABI_FUNCTION(talos_rt_v1_gc_collect_pending,
             talos_rt_status (*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v1_gc_designate,
             talos_rt_status (*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v1_gc_set_threshold,
             talos_rt_status (*)(talos_rt_context *, uint64_t));
ABI_FUNCTION(talos_rt_v1_gc_statistics,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_gc_statistics_v1 *));
ABI_FUNCTION(talos_rt_v1_class_validate,
             talos_rt_status (*)(const talos_rt_class_descriptor_v1 *));
ABI_FUNCTION(talos_rt_v1_class_register,
             talos_rt_status (*)(talos_rt_context *,
                                   const talos_rt_class_descriptor_v1 *));
ABI_FUNCTION(talos_rt_v1_random_graph_discover,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *,
                                   talos_rt_random_graph_v1 **));
ABI_FUNCTION(talos_rt_v1_random_graph_destroy,
             void (*)(talos_rt_random_graph_v1 *));
ABI_FUNCTION(talos_rt_v1_random_graph_size,
             uint64_t (*)(const talos_rt_random_graph_v1 *));
ABI_FUNCTION(talos_rt_v1_random_graph_object,
             talos_rt_object_v1 *(*)(const talos_rt_random_graph_v1 *,
                                       uint64_t));
ABI_FUNCTION(talos_rt_v1_random_graph_object_descriptor,
             const talos_rt_class_descriptor_v1
                 *(*)(const talos_rt_random_graph_v1 *, uint64_t));
ABI_FUNCTION(talos_rt_v1_random_graph_variable_count,
             uint64_t (*)(const talos_rt_random_graph_v1 *));
ABI_FUNCTION(talos_rt_v1_random_graph_variable,
             talos_rt_status (*)(const talos_rt_random_graph_v1 *, uint64_t,
                                   talos_rt_object_v1 **,
                                   const talos_rt_random_variable_v1 **));
ABI_FUNCTION(
    talos_rt_v1_random_graph_resolve_variable,
    talos_rt_status (*)(const talos_rt_random_graph_v1 *, uint64_t,
                          const talos_rt_random_variable_reference_v1 *,
                          talos_rt_object_v1 **,
                          const talos_rt_random_variable_v1 **, uint64_t *));
ABI_FUNCTION(talos_rt_v1_element_type_validate,
             talos_rt_status (*)(const talos_rt_element_type_v1 *));
ABI_FUNCTION(talos_rt_v1_element_type_register,
             talos_rt_status (*)(talos_rt_context *,
                                   const talos_rt_element_type_v1 *));
ABI_FUNCTION(talos_rt_v1_element_type_define_default,
             talos_rt_status (*)(talos_rt_context *, uint64_t,
                                   const uint8_t *, uint64_t));
ABI_FUNCTION(talos_rt_v1_gc_design_root_register,
             talos_rt_status (*)(talos_rt_context *, uint64_t));
ABI_FUNCTION(talos_rt_v1_object_allocate,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   const talos_rt_class_descriptor_v1 *,
                                   talos_rt_object_v1 **));
ABI_FUNCTION(talos_rt_v1_object_shallow_copy,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   const talos_rt_class_descriptor_v1 *,
                                   talos_rt_object_v1 *,
                                   talos_rt_object_v1 **));
ABI_FUNCTION(talos_rt_v1_object_read,
             talos_rt_status (*)(talos_rt_object_v1 *, uint64_t, void *,
                                   uint64_t));
ABI_FUNCTION(talos_rt_v1_gc_design_candidate_root_register,
             talos_rt_status (*)(talos_rt_context *, uint64_t,
                                   talos_rt_managed_root_kind_mask_v1));
ABI_FUNCTION(talos_rt_v1_object_write,
             talos_rt_status (*)(talos_rt_object_v1 *, uint64_t,
                                   const void *, uint64_t));
ABI_FUNCTION(talos_rt_v1_object_bits_insert,
             talos_rt_status (*)(talos_rt_object_v1 *, uint64_t, uint64_t,
                                   int64_t, uint32_t, uint64_t, uint32_t));
ABI_FUNCTION(talos_rt_v1_object_read_planes,
             talos_rt_status (*)(talos_rt_object_v1 *, uint64_t, void *,
                                   void *, uint64_t));
ABI_FUNCTION(talos_rt_v1_object_write_planes,
             talos_rt_status (*)(talos_rt_object_v1 *, uint64_t,
                                   const void *, const void *, uint64_t));
ABI_FUNCTION(talos_rt_v1_object_field_load,
             talos_rt_status (*)(talos_rt_object_v1 *, uint64_t,
                                   talos_rt_object_v1 **));
ABI_FUNCTION(talos_rt_v1_object_field_store,
             talos_rt_status (*)(talos_rt_object_v1 *, uint64_t,
                                   talos_rt_object_v1 *));
ABI_FUNCTION(talos_rt_v1_object_override,
             talos_rt_status (*)(talos_rt_object_v1 *, uint64_t, uint64_t,
                                   uint32_t, uint32_t, uint32_t, uint64_t,
                                   uint32_t, const void *, const void *));
ABI_FUNCTION(talos_rt_v1_object_release_override,
             talos_rt_status (*)(talos_rt_object_v1 *, uint64_t, uint64_t,
                                   uint32_t, uint32_t));
ABI_FUNCTION(talos_rt_v1_object_is_instance,
             uint32_t (*)(const talos_rt_object_v1 *,
                          const talos_rt_class_descriptor_v1 *));
ABI_FUNCTION(talos_rt_v1_object_cast,
             talos_rt_status (*)(talos_rt_object_v1 *,
                                   const talos_rt_class_descriptor_v1 *,
                                   talos_rt_object_v1 **));
ABI_FUNCTION(talos_rt_v1_object_id,
             uint64_t (*)(const talos_rt_object_v1 *));
ABI_FUNCTION(talos_rt_v1_managed_watch,
             uint64_t (*)(talos_rt_object_v1 *, talos_rt_managed_watch_kind,
                          uint64_t));
ABI_FUNCTION(talos_rt_v1_string_create,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *, const char *,
                                   uint64_t, talos_rt_string_v1 *));
ABI_FUNCTION(talos_rt_v1_string_concat,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_string_v1, talos_rt_string_v1,
                                   talos_rt_string_v1 *));
ABI_FUNCTION(talos_rt_v1_string_concat_many,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   const talos_rt_string_span_v1 *, uint64_t,
                                   talos_rt_string_v1 *));
ABI_FUNCTION(talos_rt_v1_string_repeat,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_string_v1, uint64_t,
                                   talos_rt_string_v1 *));
ABI_FUNCTION(talos_rt_v1_string_from_packed,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *, const void *,
                                   const void *, uint64_t,
                                   talos_rt_string_v1 *));
ABI_FUNCTION(talos_rt_v1_string_to_packed,
             talos_rt_status (*)(talos_rt_string_v1, void *, void *,
                                   uint64_t));
ABI_FUNCTION(talos_rt_v1_string_view,
             talos_rt_status (*)(talos_rt_string_v1, char *, const char **,
                                   uint64_t *));
ABI_FUNCTION(talos_rt_v1_string_length, uint64_t (*)(talos_rt_string_v1));
ABI_FUNCTION(talos_rt_v1_string_hash, uint64_t (*)(talos_rt_string_v1));
ABI_FUNCTION(talos_rt_v1_string_getc,
             uint32_t (*)(talos_rt_string_v1, int64_t));
ABI_FUNCTION(talos_rt_v1_string_putc,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_string_v1, int64_t, uint32_t,
                                   talos_rt_string_v1 *));
ABI_FUNCTION(talos_rt_v1_string_substr,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_string_v1, int64_t, int64_t,
                                   talos_rt_string_v1 *));
ABI_FUNCTION(talos_rt_v1_string_compare,
             int32_t (*)(talos_rt_string_v1, talos_rt_string_v1));
ABI_FUNCTION(talos_rt_v1_string_compare_insensitive,
             int32_t (*)(talos_rt_string_v1, talos_rt_string_v1));
ABI_FUNCTION(talos_rt_v1_string_case_convert,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_string_v1, uint32_t,
                                   talos_rt_string_v1 *));
ABI_FUNCTION(talos_rt_v1_scan_dynamic_validate,
             talos_rt_status (*)(talos_rt_context *, talos_rt_string_v1,
                                   uint32_t, uint32_t, uint32_t, uint64_t,
                                   uint32_t *));
ABI_FUNCTION(talos_rt_v1_string_scan_dynamic,
             talos_rt_status (*)(talos_rt_context *,
                                   talos_rt_gc_lane_v1 *,
                                   talos_rt_string_v1, uint32_t,
                                   talos_rt_string_v1, uint32_t, uint32_t,
                                   uint32_t, uint64_t, uint64_t, uint64_t,
                                   talos_rt_string_v1 *, uint32_t *,
                                   uint32_t *, uint32_t *, uint32_t *));
ABI_FUNCTION(talos_rt_v1_string_scan_raw,
             talos_rt_status (*)(talos_rt_string_v1, uint32_t, const char *,
                                   uint64_t, uint64_t, uint64_t, uint32_t,
                                   uint64_t, void *, uint64_t, void *, uint64_t,
                                   uint32_t *, uint32_t *));
ABI_FUNCTION(talos_rt_v1_string_parse_integer,
             talos_rt_status (*)(talos_rt_string_v1, uint32_t, uint64_t *));
ABI_FUNCTION(talos_rt_v1_string_parse_logic,
             talos_rt_status (*)(talos_rt_string_v1, uint32_t, uint64_t *,
                                   uint64_t *));
ABI_FUNCTION(talos_rt_v1_string_parse_real,
             talos_rt_status (*)(talos_rt_string_v1, double *));
ABI_FUNCTION(talos_rt_v1_string_parse_real_constant,
             talos_rt_status (*)(talos_rt_string_v1, double *));
ABI_FUNCTION(talos_rt_v1_plusarg_parse_logic,
             talos_rt_status (*)(talos_rt_string_v1, uint32_t, uint64_t,
                                   void *, uint64_t, void *, uint64_t));
ABI_FUNCTION(talos_rt_v1_plusarg_parse_real,
             talos_rt_status (*)(talos_rt_string_v1, double *));
ABI_FUNCTION(talos_rt_v1_string_format_integer,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *, uint64_t, uint32_t,
                                   uint32_t, talos_rt_string_v1 *));
ABI_FUNCTION(talos_rt_v1_string_format_real,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *, double,
                                   talos_rt_string_v1 *));
ABI_FUNCTION(talos_rt_v1_dynamic_array_create,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   const talos_rt_element_type_v1 *, uint64_t,
                                   talos_rt_object_v1 **));
ABI_FUNCTION(talos_rt_v1_dynamic_array_resize,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *, uint64_t));
ABI_FUNCTION(talos_rt_v1_queue_create,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   const talos_rt_element_type_v1 *, uint64_t,
                                   talos_rt_object_v1 **));
ABI_FUNCTION(talos_rt_v1_container_create_like,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *,
                                   talos_rt_object_v1 *, uint64_t,
                                   talos_rt_object_v1 **));
ABI_FUNCTION(talos_rt_v1_container_create_typed,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *, uint32_t, uint64_t,
                                   uint32_t, uint32_t, uint64_t, uint64_t,
                                   uint64_t,
                                   const talos_rt_element_trace_slot_v1 *,
                                   uint64_t, uint64_t, uint64_t,
                                   talos_rt_object_v1 **));
ABI_FUNCTION(talos_rt_v1_container_create_typed_pattern,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *, uint32_t, uint64_t,
                                   uint32_t, uint32_t, uint64_t, uint64_t,
                                   uint64_t,
                                   const talos_rt_element_trace_slot_v1 *,
                                   uint64_t, const uint8_t *, uint64_t,
                                   uint64_t, uint64_t,
                                   talos_rt_object_v1 **));
ABI_FUNCTION(talos_rt_v1_mailbox_create_typed,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *, uint64_t, uint32_t,
                                   uint32_t, uint64_t, uint64_t, uint64_t,
                                   const talos_rt_element_trace_slot_v1 *,
                                   uint64_t, int64_t, talos_rt_object_v1 **));
ABI_FUNCTION(talos_rt_v1_semaphore_create,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *, int32_t,
                                   talos_rt_object_v1 **));
ABI_FUNCTION(talos_rt_v1_semaphore_put,
             talos_rt_status (*)(talos_rt_object_v1 *, int32_t));
ABI_FUNCTION(talos_rt_v1_semaphore_try_get,
             talos_rt_status (*)(talos_rt_object_v1 *, int32_t,
                                   uint32_t *));
ABI_FUNCTION(talos_rt_v1_container_size,
             uint64_t (*)(talos_rt_object_v1 *));
ABI_FUNCTION(talos_rt_v1_box_bind_type,
             talos_rt_status (*)(talos_rt_context *, talos_rt_object_v1 *,
                                   uint64_t));
ABI_FUNCTION(talos_rt_v1_box_is_type,
             uint32_t (*)(talos_rt_object_v1 *, uint64_t));
ABI_FUNCTION(talos_rt_v1_container_read,
             talos_rt_status (*)(talos_rt_object_v1 *, int64_t, void *,
                                   void *));
ABI_FUNCTION(talos_rt_v1_container_read_checked,
             talos_rt_status (*)(talos_rt_object_v1 *, int64_t, void *,
                                   uint64_t, void *, uint64_t));
ABI_FUNCTION(talos_rt_v1_container_write,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *, int64_t,
                                   const void *, const void *));
ABI_FUNCTION(talos_rt_v1_container_write_checked,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *, int64_t,
                                   const void *, uint64_t, const void *,
                                   uint64_t));
ABI_FUNCTION(talos_rt_v1_container_clone,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *,
                                   talos_rt_object_v1 **));
ABI_FUNCTION(talos_rt_v1_container_swap,
             talos_rt_status (*)(talos_rt_object_v1 *, int64_t, int64_t));
ABI_FUNCTION(talos_rt_v1_container_import_fixed,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *, const void *,
                                   const void *, uint64_t, uint64_t, uint32_t,
                                   uint64_t, uint64_t));
ABI_FUNCTION(talos_rt_v1_container_export_fixed,
             talos_rt_status (*)(talos_rt_object_v1 *, void *, void *,
                                   uint64_t, uint64_t, uint32_t, uint64_t,
                                   uint64_t));
ABI_FUNCTION(talos_rt_v1_container_export_bitstream,
             talos_rt_status (*)(talos_rt_object_v1 *, void *, void *,
                                   uint64_t, uint64_t, uint32_t, uint64_t,
                                   uint64_t, uint64_t, uint32_t));
ABI_FUNCTION(talos_rt_v1_aggregate_export_bitstream,
             talos_rt_status (*)(const void *, const void *, uint64_t,
                                   uint64_t, uint32_t, void *, void *, uint64_t,
                                   uint64_t, uint32_t, const void *, uint64_t));
ABI_FUNCTION(talos_rt_v1_aggregate_import_bitstream,
             talos_rt_status (*)(const void *, const void *, uint64_t,
                                   uint64_t, uint32_t, void *, void *, uint64_t,
                                   uint64_t, uint32_t, const void *, uint64_t));
ABI_FUNCTION(talos_rt_v1_recursive_export_bitstream,
             talos_rt_status (*)(talos_rt_context *, const void *,
                                   const void *, uint64_t, uint64_t, uint32_t,
                                   void *, void *, uint64_t, uint64_t, uint32_t,
                                   const void *, uint64_t, uint32_t, uint32_t *,
                                   uint64_t *));
ABI_FUNCTION(talos_rt_v1_class_bitstream_finalize,
             talos_rt_status (*)(talos_rt_context *));
ABI_FUNCTION(talos_rt_v2_recursive_export_bitstream,
             talos_rt_status (*)(talos_rt_context *, uint64_t, const void *,
                                   const void *, uint64_t, uint64_t, uint32_t,
                                   void *, void *, uint64_t, uint64_t, uint32_t,
                                   uint32_t, uint32_t *, uint64_t *));
ABI_FUNCTION(talos_rt_v1_container_delete,
             talos_rt_status (*)(talos_rt_object_v1 *));
ABI_FUNCTION(talos_rt_v1_queue_push,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *, uint32_t,
                                   const void *, const void *));
ABI_FUNCTION(talos_rt_v1_queue_pop,
             talos_rt_status (*)(talos_rt_object_v1 *, uint32_t, void *,
                                   void *, uint32_t *));
ABI_FUNCTION(talos_rt_v1_mailbox_try_put,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *, const void *,
                                   const void *, uint32_t *));
ABI_FUNCTION(talos_rt_v1_mailbox_try_put_checked,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *, const void *,
                                   uint64_t, const void *, uint64_t,
                                   uint32_t *));
ABI_FUNCTION(talos_rt_v1_mailbox_num,
             talos_rt_status (*)(talos_rt_object_v1 *, uint32_t *));
ABI_FUNCTION(talos_rt_v1_mailbox_try_peek,
             talos_rt_status (*)(talos_rt_object_v1 *, void *, void *,
                                   uint32_t *));
ABI_FUNCTION(talos_rt_v1_mailbox_try_peek_checked,
             talos_rt_status (*)(talos_rt_object_v1 *, void *, uint64_t,
                                   void *, uint64_t, uint32_t *));
ABI_FUNCTION(talos_rt_v1_mailbox_try_get,
             talos_rt_status (*)(talos_rt_object_v1 *, void *, void *,
                                   uint32_t *));
ABI_FUNCTION(talos_rt_v1_mailbox_try_get_checked,
             talos_rt_status (*)(talos_rt_object_v1 *, void *, uint64_t,
                                   void *, uint64_t, uint32_t *));
ABI_FUNCTION(talos_rt_v1_queue_insert,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *, int64_t,
                                   const void *, const void *));
ABI_FUNCTION(talos_rt_v1_queue_delete_index,
             talos_rt_status (*)(talos_rt_object_v1 *, int64_t));
ABI_FUNCTION(talos_rt_v1_assoc_create,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   const talos_rt_element_type_v1 *,
                                   talos_rt_assoc_key_kind_v1, uint64_t,
                                   talos_rt_object_v1 **));
ABI_FUNCTION(talos_rt_v1_assoc_create_typed,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *, uint64_t, uint32_t,
                                   uint32_t, uint64_t, uint64_t, uint64_t,
                                   const talos_rt_element_trace_slot_v1 *,
                                   uint64_t, talos_rt_assoc_key_kind_v1,
                                   uint64_t, talos_rt_object_v1 **));
ABI_FUNCTION(talos_rt_v1_assoc_create_element_keyed,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *, uint64_t, uint32_t,
                                   uint32_t, uint64_t, uint64_t, uint64_t,
                                   const talos_rt_element_trace_slot_v1 *,
                                   uint64_t, uint64_t, uint32_t, uint32_t,
                                   uint64_t, uint64_t, uint64_t,
                                   const talos_rt_element_trace_slot_v1 *,
                                   uint64_t, const uint8_t *, uint64_t,
                                   talos_rt_object_v1 **));
ABI_FUNCTION(talos_rt_v1_assoc_exists,
             talos_rt_status (*)(talos_rt_object_v1 *,
                                   const talos_rt_assoc_key_v1 *,
                                   uint32_t *));
ABI_FUNCTION(talos_rt_v1_assoc_key_info,
             talos_rt_status (*)(talos_rt_object_v1 *,
                                   talos_rt_assoc_key_kind_v1 *, uint64_t *));
ABI_FUNCTION(talos_rt_v1_assoc_read,
             talos_rt_status (*)(talos_rt_object_v1 *,
                                   const talos_rt_assoc_key_v1 *, void *,
                                   void *, uint32_t *));
ABI_FUNCTION(talos_rt_v1_assoc_read_checked,
             talos_rt_status (*)(talos_rt_object_v1 *,
                                   const talos_rt_assoc_key_v1 *, void *,
                                   uint64_t, void *, uint64_t, uint32_t *));
ABI_FUNCTION(talos_rt_v1_assoc_write,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *,
                                   const talos_rt_assoc_key_v1 *,
                                   const void *, const void *));
ABI_FUNCTION(talos_rt_v1_assoc_write_checked,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *,
                                   const talos_rt_assoc_key_v1 *,
                                   const void *, uint64_t, const void *,
                                   uint64_t));
ABI_FUNCTION(talos_rt_v1_assoc_set_default,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *, const void *,
                                   const void *));
ABI_FUNCTION(talos_rt_v1_assoc_set_default_checked,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *, const void *,
                                   uint64_t, const void *, uint64_t));
ABI_FUNCTION(talos_rt_v1_assoc_delete,
             talos_rt_status (*)(talos_rt_object_v1 *,
                                   const talos_rt_assoc_key_v1 *));
ABI_FUNCTION(talos_rt_v1_assoc_first,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *,
                                   talos_rt_assoc_key_v1 *, uint32_t *));
ABI_FUNCTION(talos_rt_v1_assoc_last,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *,
                                   talos_rt_assoc_key_v1 *, uint32_t *));
ABI_FUNCTION(talos_rt_v1_assoc_next,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *,
                                   talos_rt_assoc_key_v1 *, uint32_t *));
ABI_FUNCTION(talos_rt_v1_assoc_prev,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *,
                                   talos_rt_assoc_key_v1 *, uint32_t *));
ABI_FUNCTION(talos_rt_v1_reference_path_index_create,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *, int64_t,
                                   talos_rt_object_v1 *, uint64_t, uint32_t,
                                   uint8_t *, uint8_t *, uint64_t,
                                   talos_rt_object_v1 **));
ABI_FUNCTION(talos_rt_v1_reference_path_assoc_create,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *,
                                   const talos_rt_assoc_key_v1 *,
                                   talos_rt_object_v1 *, uint64_t, uint32_t,
                                   uint8_t *, uint8_t *, uint64_t,
                                   talos_rt_object_v1 **));
ABI_FUNCTION(talos_rt_v1_reference_path_covergroup_option_create,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *, uint64_t, uint64_t,
                                   uint64_t, uint32_t, uint64_t, int64_t,
                                   uint32_t, talos_rt_object_v1 *, uint64_t,
                                   uint32_t, uint8_t *, uint8_t *, uint64_t,
                                   talos_rt_object_v1 **));
ABI_FUNCTION(talos_rt_v1_reference_path_string_character_create,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_string_v1, int64_t,
                                   talos_rt_object_v1 *, uint64_t, uint32_t,
                                   uint8_t *, uint8_t *, uint64_t,
                                   talos_rt_object_v1 **));
ABI_FUNCTION(talos_rt_v1_reference_path_aggregate_element_create,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *, uint64_t, uint32_t,
                                   uint8_t *, uint8_t *, uint64_t, int64_t,
                                   int64_t, int64_t, uint64_t, uint64_t,
                                   uint64_t, uint32_t, uint64_t, uint32_t,
                                   uint32_t, uint64_t, uint64_t, uint64_t,
                                   const talos_rt_element_trace_slot_v1 *,
                                   uint64_t, talos_rt_object_v1 **));
ABI_FUNCTION(talos_rt_v1_reference_path_load,
             talos_rt_status (*)(talos_rt_object_v1 *, void *, void *,
                                   uint32_t *));
ABI_FUNCTION(talos_rt_v1_reference_path_store,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *, const void *,
                                   const void *));
ABI_FUNCTION(talos_rt_v1_method_resolve,
             talos_rt_status (*)(talos_rt_object_v1 *, uint64_t, uint64_t,
                                   const talos_rt_method_descriptor_v1 **));
ABI_FUNCTION(talos_rt_v1_interface_method_resolve,
             talos_rt_status (*)(talos_rt_object_v1 *, uint64_t, uint64_t,
                                   uint64_t,
                                   const talos_rt_method_descriptor_v1 **));
ABI_FUNCTION(talos_rt_v1_method_invoke,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *, uint64_t, uint64_t,
                                   const talos_rt_method_argument_v1 *,
                                   uint32_t, void *, uint64_t));
ABI_FUNCTION(talos_rt_v1_interface_method_invoke,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *, uint64_t, uint64_t,
                                   uint64_t,
                                   const talos_rt_method_argument_v1 *,
                                   uint32_t, void *, uint64_t));
ABI_FUNCTION(talos_rt_v1_method_task_activate,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *, uint64_t, uint64_t,
                                   const talos_rt_method_argument_v1 *,
                                   uint32_t, uint64_t *));
ABI_FUNCTION(talos_rt_v1_interface_method_task_activate,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   talos_rt_object_v1 *, uint64_t, uint64_t,
                                   uint64_t,
                                   const talos_rt_method_argument_v1 *,
                                   uint32_t, uint64_t *));
ABI_FUNCTION(talos_rt_v1_weak_create,
             talos_rt_status (*)(talos_rt_gc_lane_v1 *,
                                   const talos_rt_class_descriptor_v1 *,
                                   talos_rt_object_v1 *,
                                   talos_rt_object_v1 **));
ABI_FUNCTION(talos_rt_v1_weak_get,
             talos_rt_status (*)(talos_rt_object_v1 *,
                                   talos_rt_object_v1 **));
ABI_FUNCTION(talos_rt_v1_weak_clear,
             talos_rt_status (*)(talos_rt_object_v1 *));

#undef ABI_FUNCTION
