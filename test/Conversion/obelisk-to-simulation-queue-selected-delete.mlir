// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 7.10 makes `$` the current last queue index. A mutating
// method on an element selected by `$-1` must resolve that bound before it
// captures the selected dynamic-array lvalue.
// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK: %[[PARENT:.*]] = obelisk_sim.ref.load
// CHECK: %[[SIZE:.*]] = obelisk_sim.container.size %[[PARENT]]
// CHECK: %[[LAST:.*]] = arith.subi %[[SIZE]],
// CHECK: %[[INDEX:.*]] = arith.subi {{.*}},
// CHECK: %[[CHILD:.*]] = obelisk_sim.container.read %[[PARENT]], {{.*}}
// CHECK: obelisk_sim.container.clone %[[CHILD]]
// CHECK: obelisk_sim.container.delete %[[MUTABLE:[^ ]+]]
// CHECK: obelisk_sim.container.write {{.*}}, {{.*}}, %[[MUTABLE]]

module {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32, hierarchical_name = "queue_selected_delete",
    name = "queue_selected_delete", node_id = 0 : i64,
    sym_name = "s0.queue_selected_delete"
  } {
  }
  obelisk.sv.symbol.root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64,
    sym_name = "s1.$root"
  } {
    obelisk.sv.symbol.compilation_unit attributes {
      hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"
    } {
    }
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "queue_selected_delete", is_uninstantiated = false,
      name = "queue_selected_delete", node_id = 3 : i64,
      referenced_path = "queue_selected_delete",
      referenced_symbol = @s0.queue_selected_delete,
      sym_name = "s3.queue_selected_delete"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "queue_selected_delete",
        name = "queue_selected_delete", node_id = 4 : i64,
        sym_name = "s4.queue_selected_delete", time_precision_fs = 1000000 : i64,
        time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "queue_selected_delete.queue", lifetime = 1 : i32,
          name = "queue", node_id = 5 : i64,
          semantic_type = !obelisk.queue<!obelisk.dynarray<!obelisk.integral<32, true, true, 31 : 0, integer>>, 0>,
          sym_name = "s5.queue"
        } {
        }
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "queue_selected_delete", node_id = 6 : i64,
          procedure_kind = 0 : i32, sym_name = "s6",
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {node_id = 7 : i64} {
            obelisk.sv.expression.call attributes {
              argument_count = 1 : i64, callee_name = "delete",
              constraint_restrictions = [], defaulted_arguments = array<i64: 0>,
              has_inline_constraints = false, has_iterator_expression = false,
              has_output_arguments = false, has_this_class = false,
              is_signed = false, is_super_class = false, is_system_call = true,
              node_id = 8 : i64, semantic_type = !obelisk.void,
              subroutine_kind = 0 : i32
            } {
              obelisk.sv.expression.element_select attributes {
                is_signed = false, node_id = 9 : i64,
                semantic_type = !obelisk.dynarray<!obelisk.integral<32, true, true, 31 : 0, integer>>
              } {
                obelisk.sv.expression.named_value attributes {
                  is_signed = false, node_id = 10 : i64,
                  referenced_path = "queue_selected_delete.queue",
                  referenced_symbol = @s1.$root::@s3.queue_selected_delete::@s4.queue_selected_delete::@s5.queue,
                  semantic_type = !obelisk.queue<!obelisk.dynarray<!obelisk.integral<32, true, true, 31 : 0, integer>>, 0>
                } {
                }
                obelisk.sv.expression.binary_op attributes {
                  is_signed = true, node_id = 11 : i64, operator_kind = 1 : i32,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {
                  obelisk.sv.expression.conversion attributes {
                    is_implicit = true, is_signed = true, node_id = 12 : i64,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                  } {
                    obelisk.sv.expression.unbounded_literal attributes {
                      is_signed = false, node_id = 13 : i64,
                      semantic_type = !obelisk.unbounded
                    } {
                    }
                  }
                  obelisk.sv.expression.integer_literal attributes {
                    constant_value = "1", is_declared_unsized = true,
                    is_signed = true, node_id = 14 : i64,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                  } {
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
