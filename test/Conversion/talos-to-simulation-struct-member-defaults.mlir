// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s --check-prefix=SIM
// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' \
// RUN:   --convert-talos-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' \
// RUN:   '--encode-talos-sim-to-bytecode=vpi=off' -o /dev/null

//   typedef struct {int v = 7; logic [3:0] w = 4'ha;} S;
//   S q[$];
//   initial q.push_back(q[0]);
//
// IEEE 1800-2023 7.2.2: member initializers are part of the unpacked struct
// type's default, so the simulation type carries them. Table 7-1 reads a
// nonexistent entry as that default, and 7.5.1 creates new elements with it,
// so the executable declares each container element default to the runtime
// before any process runs, in both execution tiers.

module attributes {
  coverage.language_version = 2023 : i32,
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  talos.sv.symbol.definition @s0.member_defaults attributes {definition_kind = 0 : i32, hierarchical_name = "member_defaults", name = "member_defaults", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    talos.sv.symbol.instance @s3.member_defaults attributes {hierarchical_name = "member_defaults", is_uninstantiated = false, name = "member_defaults", node_id = 3 : i64, referenced_path = "member_defaults", referenced_symbol = @s0.member_defaults} {
      talos.sv.symbol.instance_body @s4.member_defaults attributes {hierarchical_name = "member_defaults", name = "member_defaults", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "member_defaults", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.type.type_alias @s5.S attributes {hierarchical_name = "member_defaults.S", name = "S", node_id = 5 : i64, semantic_type = !talos.source_aggregate<"member_defaults", false, false, false, false, false, false, 0, 36, 36, 0, [{initializer = "7", name = "v", ordinal = 0 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !talos.integral<32, true, false, 31 : 0, int>}, {initializer = "4'b1010", name = "w", ordinal = 1 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>}]>, vpi_source_type_identity = 0 : i64, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.member_defaults::@s5.S], path = array<i64>}]} {
        }
        talos.sv.symbol.variable @s6.q attributes {hierarchical_name = "member_defaults.q", lifetime = 1 : i32, name = "q", node_id = 6 : i64, semantic_type = !talos.queue<!talos.source_aggregate<"member_defaults", false, false, false, false, false, false, 0, 36, 36, 0, [{initializer = "7", name = "v", ordinal = 0 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !talos.integral<32, true, false, 31 : 0, int>}, {initializer = "4'b1010", name = "w", ordinal = 1 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>}]>, 0>, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.member_defaults::@s5.S], path = array<i64: 0>}]} {
        }
        talos.sv.symbol.procedural_block @s7 attributes {hierarchical_name = "member_defaults", node_id = 7 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 8 : i64} {
            talos.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "push_back", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 9 : i64, semantic_type = !talos.void, subroutine_kind = 0 : i32, system_library_cell = "work.member_defaults", system_scope_path = "member_defaults", system_scope_symbol = @s1.$root::@s3.member_defaults::@s4.member_defaults} {
              talos.sv.expression.named_value attributes {is_signed = false, node_id = 10 : i64, referenced_path = "member_defaults.q", referenced_symbol = @s1.$root::@s3.member_defaults::@s4.member_defaults::@s6.q, semantic_type = !talos.queue<!talos.source_aggregate<"member_defaults", false, false, false, false, false, false, 0, 36, 36, 0, [{initializer = "7", name = "v", ordinal = 0 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !talos.integral<32, true, false, 31 : 0, int>}, {initializer = "4'b1010", name = "w", ordinal = 1 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>}]>, 0>, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.member_defaults::@s5.S], path = array<i64: 0>}]} {
              }
              talos.sv.expression.element_select attributes {is_signed = false, node_id = 11 : i64, semantic_type = !talos.source_aggregate<"member_defaults", false, false, false, false, false, false, 0, 36, 36, 0, [{initializer = "7", name = "v", ordinal = 0 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !talos.integral<32, true, false, 31 : 0, int>}, {initializer = "4'b1010", name = "w", ordinal = 1 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>}]>, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.member_defaults::@s5.S], path = array<i64>}]} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "member_defaults.q", referenced_symbol = @s1.$root::@s3.member_defaults::@s4.member_defaults::@s6.q, semantic_type = !talos.queue<!talos.source_aggregate<"member_defaults", false, false, false, false, false, false, 0, 36, 36, 0, [{initializer = "7", name = "v", ordinal = 0 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !talos.integral<32, true, false, 31 : 0, int>}, {initializer = "4'b1010", name = "w", ordinal = 1 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>}]>, 0>, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.member_defaults::@s5.S], path = array<i64: 0>}]} {
                }
                talos.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 13 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
      }
    }
  }
}


// SIM: !simulation.queue<!simulation.unpacked_struct<[#simulation.field<name = "v", type = i32, ordinal = 0, packedOffset = 0, initializer = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32>
// SIM-SAME: #simulation.field<name = "w", {{.*}}initializer = #simulation.frozen_constant<value = [-6 : i4, 0 : i4], isSigned = false>

// NATIVE: native.element_defaults = [#native.element_default<typeId = [[TYPE:[0-9]+]], image = [7, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]>]
// NATIVE-LABEL: llvm.func @main(
// NATIVE: %[[IMAGE:.*]] = llvm.mlir.addressof @__talos_element_default_[[TYPE]]
// NATIVE-NEXT: %[[SIZE:.*]] = llvm.mlir.constant(16 : i64) : i64
// NATIVE-NEXT: llvm.call @talos_rt_v1_element_type_define_default(%{{.*}}, %{{.*}}, %[[IMAGE]], %[[SIZE]])
// NATIVE: llvm.mlir.global internal constant @__talos_element_default_[[TYPE]]("\07\00\00\00\0A\00\00\00\00\00\00\00\00\00\00\00")
