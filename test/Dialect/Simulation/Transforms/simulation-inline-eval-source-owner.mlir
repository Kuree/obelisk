// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-inline{opt-level=3}))' | FileCheck %s

// The late inliner must carry the fused call site's logical-process identity
// into helper operations. Materialization uses this identity to suppress an
// active process's implicit self-trigger even when a Tier-1 coordinator is the
// physical executor. This test intentionally runs only the inliner pass.
module {
  simulation.design @eval_source_owner {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "owner.helper"
    simulation.code_unit.decl 2 in 0 function hierarchy "owner.caller"

    simulation.func private @helper(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %one = arith.constant 1 : i32
      %condition = arith.cmpi eq, %value, %one : i32
      %result = scf.if %condition -> i32 {
        %incremented = arith.addi %value, %one : i32
        scf.yield %incremented : i32
      } else {
        scf.yield %value : i32
      }
      simulation.return %result : i32
    }

    simulation.func @caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %result = simulation.call @helper(%ctx, %value)
          {schedule.eval.source_owner = #schedule.source_owner<codeUnit = 9 : i64, continuation = 7 : i32>}
          : (!simulation.context, i32) -> i32
      simulation.return %result : i32
    }
  }
}

// CHECK-LABEL: simulation.func @caller
// CHECK-NOT: simulation.call
// CHECK: arith.constant {schedule.eval.source_owner = #schedule.source_owner<codeUnit = 9 : i64, continuation = 7 : i32>} 1 : i32
// CHECK: scf.if
// CHECK: arith.addi {{.*}} {schedule.eval.source_owner = #schedule.source_owner<codeUnit = 9 : i64, continuation = 7 : i32>} : i32
// CHECK: } {schedule.eval.source_owner = #schedule.source_owner<codeUnit = 9 : i64, continuation = 7 : i32>}
