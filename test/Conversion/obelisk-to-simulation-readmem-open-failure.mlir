// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 21.4 says nothing about a memory file that cannot be opened.
// Reporting it and leaving the memory alone is what the surrounding clause's
// other recoverable problems do, and it keeps the failure attributable to the
// file rather than surfacing as the runtime's generic descriptor complaint.

module {
  obelisk.sv.symbol.definition @s0.readmem_open_failure attributes {definition_kind = 0 : i32, hierarchical_name = "readmem_open_failure", name = "readmem_open_failure", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.readmem_open_failure attributes {hierarchical_name = "readmem_open_failure", is_uninstantiated = false, name = "readmem_open_failure", node_id = 3 : i64, referenced_path = "readmem_open_failure", referenced_symbol = @s0.readmem_open_failure} {
      obelisk.sv.symbol.instance_body @s4.readmem_open_failure attributes {hierarchical_name = "readmem_open_failure", name = "readmem_open_failure", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s5.rom attributes {hierarchical_name = "readmem_open_failure.rom", lifetime = 1 : i32, name = "rom", node_id = 5 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 3 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>>} {
        }
        obelisk.sv.symbol.procedural_block @s6 attributes {hierarchical_name = "readmem_open_failure", node_id = 6 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 7 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 8 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$readmemh", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 9 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.readmem_open_failure", system_scope_path = "readmem_open_failure", system_scope_symbol = @s1.$root::@s3.readmem_open_failure::@s4.readmem_open_failure} {
                obelisk.sv.expression.string_literal attributes {constant_value = "missing.mem", is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<87 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 11 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 3 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "readmem_open_failure.rom", referenced_symbol = @s1.$root::@s3.readmem_open_failure::@s4.readmem_open_failure::@s5.rom, semantic_type = !obelisk.ranged_unpacked_array<0 : 3 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>>} {
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
// CHECK: %[[FD:.*]] = simulation.file.open
// A zero descriptor means the open failed, so the read is branched around.
// CHECK: %[[OPENED:.*]] = arith.cmpi ne, %[[FD]], %{{.*}}
// CHECK: cf.cond_br %[[OPENED]], ^[[READ:.*]], ^[[FAILED:.*]]

// The report names the file, records an unsuccessful run, and joins the same
// exit the completed read uses -- without closing a descriptor it never got.
// CHECK: ^[[FAILED]]:
// CHECK: simulation.bytes.constant "ERROR: $readmemh: cannot open the memory file missing.mem"
// CHECK: simulation.display
// CHECK: simulation.error
// CHECK-NOT: simulation.file.close
// CHECK: cf.br ^[[DONE:.*]]
