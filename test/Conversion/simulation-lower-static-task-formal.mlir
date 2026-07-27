// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

// A static task formal has both a value argument for each invocation and a
// descriptor-backed shared location.  After copy-in, semantic reads and writes
// must consistently use the descriptor rather than the temporary local.

!int = !obelisk.integral<32, true, false, 31 : 0, int>

module {
  obelisk_sim.design @static_task_formal {
    obelisk_sim.code_unit.decl 9400001 in 0 task
        hierarchy "top.mutate"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i32 static
        hierarchy "top.mutate.value"

    // CHECK-LABEL: obelisk_sim.func @mutate
    // CHECK: %[[LOCAL:.*]] = obelisk_sim.ref.alloc %arg1
    // CHECK: %[[COPY_IN:.*]] = obelisk_sim.ref.load %[[LOCAL]]
    // CHECK: obelisk_sim.ref.store %[[COPY_IN]] to %arg2
    // CHECK: %[[NINE:.*]] = arith.constant 9 : i32
    // CHECK: obelisk_sim.ref.store %[[NINE]] to %arg2
    // CHECK-NOT: obelisk_sim.ref.store %[[NINE]] to %[[LOCAL]]
    // CHECK: obelisk_sim.return
    obelisk_sim.func @mutate(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %input: i32
            {obelisk_sim.capture_kind = 1 : i32},
        %shared: !obelisk_sim.ref<i32>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {
          entry_kind = 12 : i32,
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "top.mutate.value",
                argument = 1, kind = formal_local, copyOut = false>,
            #obelisk_sim.argument_binding<path = "top.mutate.value",
                argument = 2, kind = direct, copyOut = false>
          ],
          code_unit_id = 9400001 : i64
        } {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        obelisk.sv.expression.assignment attributes {
            node_id = 2 : i64, assignment_kind = 0 : i32,
            semantic_type = !int} {
          obelisk.sv.expression.named_value attributes {
              node_id = 3 : i64, referenced_path = "top.mutate.value",
              referenced_symbol = @value, semantic_type = !int} {
          }
          obelisk.sv.expression.integer_literal attributes {
              node_id = 4 : i64, constant_value = "9",
              semantic_type = !int} {
          }
        }
      }
      obelisk_sim.return
    }
  }
}
