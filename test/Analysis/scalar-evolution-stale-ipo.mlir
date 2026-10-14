// RUN: obelisk-opt %s --test-obelisk-scoped-integer-ranges 2>&1 | FileCheck %s

!array = !simulation.unpacked_array<0 : 15 x i8>
module {
  simulation.design @stale_ipo {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !array design
    // A previous IPO round constrained this private entry. Its callers have
    // since been removed; re-running IPO must discard the old certificate.
    simulation.code_unit.decl 1 in 0 function hierarchy "no_callers"
    simulation.func private @no_callers(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %index: i8 {simulation.capture_kind = 2 : i32, analysis.integer_range = {umin = 3 : i8, umax = 3 : i8, smin = 3 : i8, smax = 3 : i8}}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %a = simulation.ref.array_element %root[%index] {test.scoped = "stale-entry"} : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      simulation.return
    }
    // CHECK: stale-entry umin=0 umax=255
  }
}
