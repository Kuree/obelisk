// RUN: obelisk-opt %s --lower-obelisk-to-sim | FileCheck %s

// MLIR-level coverage for the semantic lowering of a descending, bounded
// $readmemh load. Address records and data words take distinct validation
// paths before converging on the token loop.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.memory", lifetime = 1 : i32, name = "memory", node_id = 5 : i64, semantic_type = !obelisk.ranged_unpacked_array<3 : 6 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>, sym_name = "s5.memory"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 6 : i64, procedure_kind = 0 : i32, sym_name = "s6", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 7 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 8 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "$readmemh", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 9 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "memory.hex", is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 11 : i64, semantic_type = !obelisk.ranged_unpacked_array<3 : 6 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "top.memory", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.memory, semantic_type = !obelisk.ranged_unpacked_array<3 : 6 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                  }
                  obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 13 : i64, semantic_type = !obelisk.ranged_unpacked_array<3 : 6 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                  }
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "5", is_declared_unsized = true, is_signed = true, node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "3", is_declared_unsized = true, is_signed = true, node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK-DAG: obelisk_sim.bytes.constant "WARNING: $readmemh: data word count does not match address range"
// CHECK-DAG: obelisk_sim.bytes.constant "ERROR: $readmemh: address is outside the selected memory range"
// CHECK: %[[FD:.*]] = obelisk_sim.file.open
// A descriptor of zero means the open failed, so the read is entered only
// once that is ruled out. The address-range check folds into the same branch
// here because this memory's bounds are static.
// CHECK: %[[OPENED:.*]] = arith.cmpi ne, %[[FD]], %{{.*}}
// CHECK: cf.cond_br %[[OPENED]], ^[[LOOP:bb[0-9]+]]({{.*}} : i64, i1, i64, i1), ^[[OPEN_ERROR:bb[0-9]+]]
// CHECK: ^[[LOOP]](%[[CURSOR:.*]]: i64, %[[SAW_ADDRESS:.*]]: i1, %[[COUNT:.*]]: i64, %[[EXHAUSTED:.*]]: i1):
// CHECK: %[[DATA:.*]], %[[KIND:.*]], %[[ADDRESS:.*]] = obelisk_sim.file.readmem_token {{.*}} {radix = 16 : i32}
// CHECK: cf.cond_br {{.*}}, ^[[COUNT_CHECK:bb[0-9]+]], ^[[CLASSIFY:bb[0-9]+]]
// CHECK: ^[[CLASSIFY]]:
// CHECK: cf.cond_br {{.*}}, ^[[SET_ADDRESS:bb[0-9]+]], ^[[STORE_CHECK:bb[0-9]+]]
// CHECK: ^[[SET_ADDRESS]]:
// CHECK: arith.cmpi sle, %[[ADDRESS]],
// CHECK: cf.cond_br {{.*}}, ^[[LOOP]](%[[ADDRESS]], {{.*}}, %[[COUNT]], {{.*}} : i64, i1, i64, i1), ^[[ADDRESS_ERROR:bb[0-9]+]]
// CHECK: ^[[STORE_CHECK]]:
// CHECK: arith.cmpi sle, %[[CURSOR]],
// CHECK: cf.cond_br {{.*}}, ^[[STORE:bb[0-9]+]], ^[[COUNT_CHECK]]
// CHECK: ^[[STORE]]:
// CHECK: %[[ELEMENT:.*]] = obelisk_sim.packed.unflatten %[[DATA]]
// CHECK: %[[REF:.*]] = obelisk_sim.ref.array_element {{.*}}[%[[CURSOR]]]
// CHECK: obelisk_sim.ref.store %[[ELEMENT]] to %[[REF]]
// CHECK: cf.br ^[[LOOP]]
// CHECK: ^[[COUNT_CHECK]]:
// CHECK: arith.ori %[[SAW_ADDRESS]],
// CHECK: cf.cond_br {{.*}}, ^[[EXIT:bb[0-9]+]],
// CHECK: obelisk_sim.display
// CHECK: ^[[ADDRESS_ERROR]]:
// CHECK: obelisk_sim.display
// The failed-open report names the file and records an unsuccessful run. It
// shares nothing with the read but its exit; in particular it must not close a
// descriptor the open never handed out.
// CHECK: ^[[OPEN_ERROR]]:
// CHECK: obelisk_sim.bytes.constant "ERROR: $readmemh: cannot open the memory file
// CHECK: obelisk_sim.display
// CHECK-NEXT: obelisk_sim.error
// CHECK-NOT: obelisk_sim.file.close
// CHECK: ^[[EXIT]]:
// CHECK: obelisk_sim.file.close {{.*}}, %[[FD]]
