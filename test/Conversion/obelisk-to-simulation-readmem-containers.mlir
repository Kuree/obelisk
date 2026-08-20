// RUN: obelisk-opt %s --lower-obelisk-to-sim | FileCheck %s

// Dynamic arrays and queues retain their call-time size: lowering clones the
// value-semantics container once, snapshots its size, and only overwrites
// existing indices.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.dynamic_memory", lifetime = 1 : i32, name = "dynamic_memory", node_id = 5 : i64, semantic_type = !obelisk.dynarray<!obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>, sym_name = "s5.dynamic_memory"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.queue_memory", lifetime = 1 : i32, name = "queue_memory", node_id = 6 : i64, semantic_type = !obelisk.queue<!obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, 0>, sym_name = "s6.queue_memory"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 8 : i64} {
            obelisk.sv.statement.list attributes {node_id = 9 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "$readmemb", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 11 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "dynamic.mem", is_signed = false, node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<87 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 13 : i64, semantic_type = !obelisk.dynarray<!obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "top.dynamic_memory", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.dynamic_memory, semantic_type = !obelisk.dynarray<!obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                    }
                    obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 15 : i64, semantic_type = !obelisk.dynarray<!obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                    }
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 16 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "3", is_declared_unsized = true, is_signed = true, node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 18 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$readmemh", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 19 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "queue.mem", is_signed = false, node_id = 20 : i64, semantic_type = !obelisk.ranged_packed_array<71 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 21 : i64, semantic_type = !obelisk.queue<!obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, 0>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 22 : i64, referenced_path = "top.queue_memory", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.queue_memory, semantic_type = !obelisk.queue<!obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, 0>} {
                    }
                    obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 23 : i64, semantic_type = !obelisk.queue<!obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, 0>} {
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
}

// CHECK: %[[DYNAMIC:.*]] = obelisk_sim.ref.load
// CHECK: %[[DYNAMIC_COPY:.*]] = obelisk_sim.container.clone %[[DYNAMIC]]
// CHECK: obelisk_sim.ref.store %[[DYNAMIC_COPY]]
// CHECK: %[[DYNAMIC_SIZE:.*]] = obelisk_sim.container.size %[[DYNAMIC_COPY]]
// CHECK: obelisk_sim.file.readmem_token {{.*}} {radix = 2 : i32}
// CHECK: obelisk_sim.container.write %[[DYNAMIC_COPY]],
// CHECK: %[[QUEUE:.*]] = obelisk_sim.ref.load
// CHECK: %[[QUEUE_COPY:.*]] = obelisk_sim.container.clone %[[QUEUE]]
// CHECK: obelisk_sim.ref.store %[[QUEUE_COPY]]
// CHECK: %[[QUEUE_SIZE:.*]] = obelisk_sim.container.size %[[QUEUE_COPY]]
// CHECK: obelisk_sim.file.readmem_token {{.*}} {radix = 16 : i32}
// CHECK: obelisk_sim.container.write %[[QUEUE_COPY]],
// CHECK-NOT: obelisk_sim.container.create
