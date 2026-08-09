// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

// Source queue mutations are covered at the unit-lowering boundary. Runtime
// tests separately exercise ring ordering and the declared maximum-index
// bound, while backend tests cover native and bytecode op lowering.

!int = !obelisk.integral<32, true, false, 31 : 0, int>
!queue = !obelisk.queue<!int, 1>

module {
  obelisk_sim.design @queue_mutations {
    obelisk_sim.code_unit.decl 9700001 in 0 function
        hierarchy "top.queue_mutations"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.queue<i32, 1>
        design hierarchy "top.queue"

    // CHECK-LABEL: obelisk_sim.func @mutate
    // push_back appends at the current size.
    // CHECK: %[[BACK_SIZE:.*]] = obelisk_sim.container.size
    // CHECK: obelisk_sim.container.write {{.*}}, %[[BACK_SIZE]], {{.*}}
    // Bounded push_front drops the last element when the queue is full before
    // inserting at zero. This differs from an explicit insert on a full queue,
    // which remains ignored by the runtime.
    // CHECK: %[[FRONT_VALUE:.*]] = arith.constant 5 : i32
    // CHECK-NEXT: %[[FRONT_SIZE:.*]] = obelisk_sim.container.size
    // CHECK-NEXT: %[[CAPACITY:.*]] = arith.constant 2 : i64
    // CHECK-NEXT: %[[FULL:.*]] = arith.cmpi uge, %[[FRONT_SIZE]], %[[CAPACITY]]
    // CHECK-NEXT: cf.cond_br %[[FULL]]
    // CHECK: %[[ONE:.*]] = arith.constant 1 : i64
    // CHECK-NEXT: %[[LAST:.*]] = arith.subi %[[FRONT_SIZE]], %[[ONE]]
    // CHECK-NEXT: obelisk_sim.queue.delete {{.*}}[%[[LAST]]]
    // CHECK-NEXT: cf.br
    // CHECK: %[[FRONT_ZERO:.*]] = arith.constant 0 : i64
    // CHECK-NEXT: obelisk_sim.queue.insert %[[FRONT_VALUE]] into {{.*}}[%[[FRONT_ZERO]]]
    // insert and indexed delete preserve the explicit source index.
    // CHECK: %[[INSERT_INDEX:.*]] = arith.constant 1 : i32
    // CHECK: %[[INSERT_INDEX64:.*]] = arith.extsi %[[INSERT_INDEX]] : i32 to i64
    // CHECK: obelisk_sim.queue.insert {{.*}} into {{.*}}[%[[INSERT_INDEX64]]]
    // CHECK: %[[DELETE_INDEX:.*]] = arith.constant 1 : i32
    // CHECK: %[[DELETE_INDEX64:.*]] = arith.extsi %[[DELETE_INDEX]] : i32 to i64
    // CHECK: obelisk_sim.queue.delete {{.*}}[%[[DELETE_INDEX64]]]
    // A no-argument delete clears the whole queue.
    // CHECK: obelisk_sim.container.delete
    obelisk_sim.func @mutate(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %queue: !obelisk_sim.ref<!obelisk_sim.queue<i32, 1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {
          entry_kind = 8 : i32,
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "top.queue", argument = 1,
                kind = direct, copyOut = false>
          ],
          code_unit_id = 9700001 : i64,
          obelisk_sim.void_function
        } {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        obelisk.sv.expression.call attributes {
            argument_count = 2 : i64, callee_name = "push_back",
            constraint_restrictions = [], has_inline_constraints = false,
            has_iterator_expression = false, has_output_arguments = false,
            has_this_class = false, is_super_class = false,
            is_system_call = true, node_id = 2 : i64,
            semantic_type = !obelisk.void, subroutine_kind = 0 : i32,
            system_library_cell = "work.top",
            system_scope_path = "top"} {
          obelisk.sv.expression.named_value attributes {
              node_id = 3 : i64, referenced_path = "top.queue",
              referenced_symbol = @queue, semantic_type = !queue} {
          }
          obelisk.sv.expression.integer_literal attributes {
              node_id = 4 : i64, constant_value = "4",
              semantic_type = !int} {
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 5 : i64} {
        obelisk.sv.expression.call attributes {
            argument_count = 2 : i64, callee_name = "push_front",
            constraint_restrictions = [], has_inline_constraints = false,
            has_iterator_expression = false, has_output_arguments = false,
            has_this_class = false, is_super_class = false,
            is_system_call = true, node_id = 6 : i64,
            semantic_type = !obelisk.void, subroutine_kind = 0 : i32,
            system_library_cell = "work.top",
            system_scope_path = "top"} {
          obelisk.sv.expression.named_value attributes {
              node_id = 7 : i64, referenced_path = "top.queue",
              referenced_symbol = @queue, semantic_type = !queue} {
          }
          obelisk.sv.expression.integer_literal attributes {
              node_id = 8 : i64, constant_value = "5",
              semantic_type = !int} {
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
        obelisk.sv.expression.call attributes {
            argument_count = 3 : i64, callee_name = "insert",
            constraint_restrictions = [], has_inline_constraints = false,
            has_iterator_expression = false, has_output_arguments = false,
            has_this_class = false, is_super_class = false,
            is_system_call = true, node_id = 10 : i64,
            semantic_type = !obelisk.void, subroutine_kind = 0 : i32,
            system_library_cell = "work.top",
            system_scope_path = "top"} {
          obelisk.sv.expression.named_value attributes {
              node_id = 11 : i64, referenced_path = "top.queue",
              referenced_symbol = @queue, semantic_type = !queue} {
          }
          obelisk.sv.expression.integer_literal attributes {
              node_id = 12 : i64, constant_value = "1",
              semantic_type = !int} {
          }
          obelisk.sv.expression.integer_literal attributes {
              node_id = 13 : i64, constant_value = "6",
              semantic_type = !int} {
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {
          node_id = 14 : i64} {
        obelisk.sv.expression.call attributes {
            argument_count = 2 : i64, callee_name = "delete",
            constraint_restrictions = [], has_inline_constraints = false,
            has_iterator_expression = false, has_output_arguments = false,
            has_this_class = false, is_super_class = false,
            is_system_call = true, node_id = 15 : i64,
            semantic_type = !obelisk.void, subroutine_kind = 0 : i32,
            system_library_cell = "work.top",
            system_scope_path = "top"} {
          obelisk.sv.expression.named_value attributes {
              node_id = 16 : i64, referenced_path = "top.queue",
              referenced_symbol = @queue, semantic_type = !queue} {
          }
          obelisk.sv.expression.integer_literal attributes {
              node_id = 17 : i64, constant_value = "1",
              semantic_type = !int} {
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {
          node_id = 18 : i64} {
        obelisk.sv.expression.call attributes {
            argument_count = 1 : i64, callee_name = "delete",
            constraint_restrictions = [], has_inline_constraints = false,
            has_iterator_expression = false, has_output_arguments = false,
            has_this_class = false, is_super_class = false,
            is_system_call = true, node_id = 19 : i64,
            semantic_type = !obelisk.void, subroutine_kind = 0 : i32,
            system_library_cell = "work.top",
            system_scope_path = "top"} {
          obelisk.sv.expression.named_value attributes {
              node_id = 20 : i64, referenced_path = "top.queue",
              referenced_symbol = @queue, semantic_type = !queue} {
          }
        }
      }
      obelisk_sim.return
    }
  }
}
