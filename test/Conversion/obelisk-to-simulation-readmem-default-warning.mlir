// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 21.4 requires a word-count warning when a file without
// address records does not fill the range implied by omitted bounds.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.memory", lifetime = 1 : i32, name = "memory", node_id = 5 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 3 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>>, sym_name = "s5.memory"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 6 : i64, procedure_kind = 0 : i32, sym_name = "s6", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 7 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 8 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$readmemh", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 9 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "memory.hex", is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 11 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 3 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "top.memory", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.memory, semantic_type = !obelisk.ranged_unpacked_array<0 : 3 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>>} {
                  }
                  obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 13 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 3 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>>} {
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK-LABEL: simulation.func private @unit_0
// CHECK: ^[[LOOP:bb[0-9]+]](%{{.*}}: i64, %[[SAW_ADDRESS:.*]]: i1, %[[COUNT:.*]]: i64,
// CHECK: simulation.file.readmem_token
// CHECK: cf.cond_br {{.*}}, ^[[COUNT_CHECK:bb[0-9]+]],
// CHECK: ^[[COUNT_CHECK]]:
// CHECK: %[[COUNT_MATCHES:.*]] = arith.cmpi eq, %[[COUNT]],
// CHECK: %[[SUPPRESS:.*]] = arith.ori %[[SAW_ADDRESS]], %[[COUNT_MATCHES]]
// CHECK: cf.cond_br %[[SUPPRESS]], ^[[EXIT:bb[0-9]+]], ^[[WARN:bb[0-9]+]]
// CHECK: ^[[WARN]]:
// CHECK: simulation.bytes.constant "WARNING: $readmemh: data word count does not match address range"
// CHECK: simulation.display
// CHECK: cf.br ^[[EXIT]]
