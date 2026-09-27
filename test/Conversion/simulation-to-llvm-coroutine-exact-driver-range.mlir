// RUN: obelisk-opt %s --split-input-file \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

// A statically selected slice which exactly equals the declaration's driven
// range resolves only that logical bit. The resolver still expands a
// pass-connected component from that bit; it does not truncate the component.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @exact {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "exact.drive"
    simulation.net.decl 0 in 0 : !simulation.logic<4> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<4> design
        {driven_low = 2 : i64, driven_width = 1 : i64}
    simulation.func @exact_slice(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %driver = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<4>>
      %bit = simulation.driver.extract %driver from 2 :
          !simulation.driver<!simulation.logic<4>> ->
          !simulation.driver<!simulation.logic<1>>
      %one = simulation.logic.constant true, false :
          !simulation.logic<1>
      simulation.driver.drive %bit = %one {
          schedule.eval.source_owner = #schedule.source_owner<codeUnit = 17 : i64, continuation = 3 : i32>} :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @exact_slice
// CHECK-COUNT-1: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK: llvm.call @obelisk_rt_v1_scheduler_static_transition
// CHECK-SAME: schedule.eval.source_owner = #schedule.source_owner<codeUnit = 17 : i64, continuation = 3 : i32>
// CHECK-NOT: llvm.call @obelisk_rt_v1_strength_resolve_kind

// -----

// A statically selected value narrower than the driver's declared range does
// not qualify for the whole-declaration exact-range shortcut, but its static
// low offset still proves that only the written component needs resolution.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @mismatched {
    simulation.scope.decl 0
    simulation.code_unit.decl 2 in 0 function hierarchy "mismatched.drive"
    simulation.net.decl 0 in 0 : !simulation.logic<4> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<4> design
    simulation.func @mismatched_slice(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %driver = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<4>>
      %bit = simulation.driver.extract %driver from 2 :
          !simulation.driver<!simulation.logic<4>> ->
          !simulation.driver<!simulation.logic<1>>
      %one = simulation.logic.constant true, false :
          !simulation.logic<1>
      simulation.driver.drive %bit = %one :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @mismatched_slice
// CHECK-COUNT-1: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK-NOT: llvm.call @obelisk_rt_v1_strength_resolve_kind

// -----

// An internal CFG argument is not a function formal even when its argument
// number happens to match a descriptor-bearing formal. Different incoming
// drivers make this join ambiguous, so it must retain full-design resolution.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @ambiguous {
    simulation.scope.decl 0
    simulation.code_unit.decl 3 in 0 function hierarchy "ambiguous.drive"
    simulation.net.decl 0 in 0 : !simulation.logic<4> design
    simulation.net.decl 1 in 0 : !simulation.logic<4> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<4> design
    simulation.driver.decl 1 in 0 drives 1 : !simulation.logic<4> design
    simulation.func @ambiguous_join(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %driver0: !simulation.driver<!simulation.logic<4>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 0 : i64},
        %driver1: !simulation.driver<!simulation.logic<4>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 1 : i64},
        %condition: i1 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %false = arith.constant false
      cf.cond_br %condition, ^left, ^right
    ^left:
      cf.br ^join(%false, %driver0 : i1, !simulation.driver<!simulation.logic<4>>)
    ^right:
      cf.br ^join(%false, %driver1 : i1, !simulation.driver<!simulation.logic<4>>)
    ^join(%dummy: i1, %selected: !simulation.driver<!simulation.logic<4>>):
      %one = simulation.logic.constant 15 : i4, 0 : i4 :
          !simulation.logic<4>
      simulation.driver.drive %selected = %one :
          !simulation.driver<!simulation.logic<4>>,
          !simulation.logic<4>
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @ambiguous_join
// CHECK-COUNT-8: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK-NOT: llvm.call @obelisk_rt_v1_strength_resolve_kind

// -----

// Capture threading forwards a selected cohort driver through an internal
// join. Identical incoming slices retain exact provenance.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @identical_join {
    simulation.scope.decl 0
    simulation.code_unit.decl 4 in 0 function hierarchy "identical.drive"
    simulation.net.decl 0 in 0 : !simulation.logic<4> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<4> design
        {driven_low = 2 : i64, driven_width = 1 : i64}
    simulation.func @identical_join(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %driver: !simulation.driver<!simulation.logic<4>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 0 : i64},
        %condition: i1 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %bit = simulation.driver.extract %driver from 2 :
          !simulation.driver<!simulation.logic<4>> ->
          !simulation.driver<!simulation.logic<1>>
      %false = arith.constant false
      cf.cond_br %condition, ^left, ^right
    ^left:
      cf.br ^join(%false, %bit : i1, !simulation.driver<!simulation.logic<1>>)
    ^right:
      cf.br ^join(%false, %bit : i1, !simulation.driver<!simulation.logic<1>>)
    ^join(%dummy: i1, %selected: !simulation.driver<!simulation.logic<1>>):
      %one = simulation.logic.constant true, false :
          !simulation.logic<1>
      simulation.driver.drive %selected = %one :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @identical_join
// CHECK-COUNT-1: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK-NOT: llvm.call @obelisk_rt_v1_strength_resolve_kind
