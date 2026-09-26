// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-inline{opt-level=3}))' | FileCheck %s

// The late inliner must carry the fused call site's logical-process identity
// into helper operations. Materialization uses this identity to suppress an
// active process's implicit self-trigger even when a Tier-1 coordinator is the
// physical executor. This test intentionally runs only the inliner pass.
module {
  obelisk_sim.design @eval_source_owner {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "owner.helper"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "owner.caller"

    obelisk_sim.func private @helper(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %one = arith.constant 1 : i32
      %condition = arith.cmpi eq, %value, %one : i32
      %result = scf.if %condition -> i32 {
        %incremented = arith.addi %value, %one : i32
        scf.yield %incremented : i32
      } else {
        scf.yield %value : i32
      }
      obelisk_sim.return %result : i32
    }

    obelisk_sim.func @caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %result = obelisk_sim.call @helper(%ctx, %value)
          {obelisk.eval.source_owner = {code_unit = 9 : i64,
                                        continuation = 7 : i32}}
          : (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %result : i32
    }
  }
}

// CHECK-LABEL: obelisk_sim.func @caller
// CHECK-NOT: obelisk_sim.call
// CHECK: arith.constant {obelisk.eval.source_owner = {code_unit = 9 : i64, continuation = 7 : i32}} 1 : i32
// CHECK: scf.if
// CHECK: arith.addi {{.*}} {obelisk.eval.source_owner = {code_unit = 9 : i64, continuation = 7 : i32}} : i32
// CHECK: } {obelisk.eval.source_owner = {code_unit = 9 : i64, continuation = 7 : i32}}
