// RUN: obelisk-opt %s --split-input-file \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

// A statically selected slice which exactly equals the declaration's driven
// range resolves only that logical bit. The resolver still expands a
// pass-connected component from that bit; it does not truncate the component.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @exact {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "exact.drive"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<4> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<4> design
        {driven_low = 2 : i64, driven_width = 1 : i64}
    obelisk_sim.func @exact_slice(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %driver = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<4>>
      %bit = obelisk_sim.driver.extract %driver from 2 :
          !obelisk_sim.driver<!obelisk_sim.logic<4>> ->
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %one = obelisk_sim.logic.constant true, false :
          !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %bit = %one :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: llvm.func @exact_slice
// CHECK-COUNT-1: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK-NOT: llvm.call @obelisk_rt_v1_strength_resolve_kind

// -----

// A statically selected value narrower than the driver's declared range is
// not exact: resolving only the selected bit would miss other raw
// contributions owned by the same drive operation.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @mismatched {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "mismatched.drive"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<4> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<4> design
    obelisk_sim.func @mismatched_slice(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %driver = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<4>>
      %bit = obelisk_sim.driver.extract %driver from 2 :
          !obelisk_sim.driver<!obelisk_sim.logic<4>> ->
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %one = obelisk_sim.logic.constant true, false :
          !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %bit = %one :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: llvm.func @mismatched_slice
// CHECK-COUNT-4: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK-NOT: llvm.call @obelisk_rt_v1_strength_resolve_kind

// -----

// An internal CFG argument is not a function formal even when its argument
// number happens to match a descriptor-bearing formal. Different incoming
// drivers make this join ambiguous, so it must retain full-design resolution.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @ambiguous {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "ambiguous.drive"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<4> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<4> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<4> design
    obelisk_sim.driver.decl 1 in 0 drives 1 : !obelisk_sim.logic<4> design
    obelisk_sim.func @ambiguous_join(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %driver0: !obelisk_sim.driver<!obelisk_sim.logic<4>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %driver1: !obelisk_sim.driver<!obelisk_sim.logic<4>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 1 : i64},
        %condition: i1 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %false = arith.constant false
      cf.cond_br %condition, ^left, ^right
    ^left:
      cf.br ^join(%false, %driver0 : i1, !obelisk_sim.driver<!obelisk_sim.logic<4>>)
    ^right:
      cf.br ^join(%false, %driver1 : i1, !obelisk_sim.driver<!obelisk_sim.logic<4>>)
    ^join(%dummy: i1, %selected: !obelisk_sim.driver<!obelisk_sim.logic<4>>):
      %one = obelisk_sim.logic.constant 15 : i4, 0 : i4 :
          !obelisk_sim.logic<4>
      obelisk_sim.driver.drive %selected = %one :
          !obelisk_sim.driver<!obelisk_sim.logic<4>>,
          !obelisk_sim.logic<4>
      obelisk_sim.return
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
  obelisk_sim.design @identical_join {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 4 in 0 function hierarchy "identical.drive"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<4> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<4> design
        {driven_low = 2 : i64, driven_width = 1 : i64}
    obelisk_sim.func @identical_join(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<4>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %condition: i1 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %bit = obelisk_sim.driver.extract %driver from 2 :
          !obelisk_sim.driver<!obelisk_sim.logic<4>> ->
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %false = arith.constant false
      cf.cond_br %condition, ^left, ^right
    ^left:
      cf.br ^join(%false, %bit : i1, !obelisk_sim.driver<!obelisk_sim.logic<1>>)
    ^right:
      cf.br ^join(%false, %bit : i1, !obelisk_sim.driver<!obelisk_sim.logic<1>>)
    ^join(%dummy: i1, %selected: !obelisk_sim.driver<!obelisk_sim.logic<1>>):
      %one = obelisk_sim.logic.constant true, false :
          !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %selected = %one :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: llvm.func @identical_join
// CHECK-COUNT-1: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK-NOT: llvm.call @obelisk_rt_v1_strength_resolve_kind
