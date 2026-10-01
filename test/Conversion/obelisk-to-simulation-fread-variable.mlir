// RUN: obelisk-opt %s --lower-obelisk-to-sim | FileCheck %s

// IEEE 1800-2017 21.3.4.3: $fread accepts variable-size unpacked memories.
// Lowering snapshots the existing extent, clones the value-semantics
// container once, and overwrites only successfully read elements.
module {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s5.dynamic_memory attributes {hierarchical_name = "top.dynamic_memory", lifetime = 1 : i32, name = "dynamic_memory", node_id = 5 : i64, semantic_type = !obelisk.dynarray<!obelisk.integral<8, true, false, 7 : 0, byte>>} {
        }
        obelisk.sv.symbol.variable @s6.queue_memory attributes {hierarchical_name = "top.queue_memory", lifetime = 1 : i32, name = "queue_memory", node_id = 6 : i64, semantic_type = !obelisk.queue<!obelisk.ranged_packed_array<11 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, 0>} {
        }
        obelisk.sv.symbol.variable @s7.descriptor attributes {hierarchical_name = "top.descriptor", lifetime = 1 : i32, name = "descriptor", node_id = 7 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
        }
        obelisk.sv.symbol.procedural_block @s8 attributes {hierarchical_name = "top", node_id = 8 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 9 : i64} {
            obelisk.sv.statement.list attributes {node_id = 10 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "$fread", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 13 : i64, semantic_type = !obelisk.dynarray<!obelisk.integral<8, true, false, 7 : 0, byte>>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "top.dynamic_memory", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.dynamic_memory, semantic_type = !obelisk.dynarray<!obelisk.integral<8, true, false, 7 : 0, byte>>} {
                    }
                    obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 15 : i64, semantic_type = !obelisk.dynarray<!obelisk.integral<8, true, false, 7 : 0, byte>>} {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 16 : i64, referenced_path = "top.descriptor", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.descriptor, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 19 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$fread", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 21 : i64, semantic_type = !obelisk.queue<!obelisk.ranged_packed_array<11 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, 0>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 22 : i64, referenced_path = "top.queue_memory", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.queue_memory, semantic_type = !obelisk.queue<!obelisk.ranged_packed_array<11 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, 0>} {
                    }
                    obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 23 : i64, semantic_type = !obelisk.queue<!obelisk.ranged_packed_array<11 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, 0>} {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 24 : i64, referenced_path = "top.descriptor", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.descriptor, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
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

// CHECK: %[[DYNAMIC:.*]] = simulation.ref.load {{.*}} : !simulation.ref<!simulation.dynamic_array<i8>>
// CHECK: %[[DYNAMIC_COPY:.*]] = simulation.container.clone %[[DYNAMIC]]
// CHECK: simulation.ref.store %[[DYNAMIC_COPY]]
// CHECK: %[[DYNAMIC_SIZE:.*]] = simulation.container.size %[[DYNAMIC_COPY]]
// CHECK: simulation.file.read_packed {{.*}} -> (i8, i32)
// CHECK: simulation.container.write %[[DYNAMIC_COPY]],
// CHECK: %[[QUEUE:.*]] = simulation.ref.load {{.*}} : !simulation.ref<!simulation.queue
// CHECK: %[[QUEUE_COPY:.*]] = simulation.container.clone %[[QUEUE]]
// CHECK: simulation.ref.store %[[QUEUE_COPY]]
// CHECK: %[[QUEUE_SIZE:.*]] = simulation.container.size %[[QUEUE_COPY]]
// CHECK: simulation.file.read_packed {{.*}} -> (i12, i32)
// CHECK: simulation.container.write %[[QUEUE_COPY]],
// CHECK-NOT: simulation.container.create
