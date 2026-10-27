// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph{vpi=off},talos-sim-specialize-static-state-nba),convert-talos-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=OFF
// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph{vpi=full},talos-sim-specialize-static-state-nba),convert-talos-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=FULL

// A joined handle may use direct plane loads only if every possible leaf is
// certified. Keep arbitrary handles, invalid selections and guarded roots on
// the validating runtime path; knowing one incoming root is insufficient.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
} {
  simulation.design @joined_load_guards {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i16 design
    simulation.net.decl 0 in 0 : i8 design
    simulation.net.decl 1 in 0 : i8 design
    simulation.net.decl 2 in 0 : !simulation.logic<8> design
    simulation.net.decl 3 in 0 : !simulation.logic<8> design
    simulation.code_unit.decl 4 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 1 in 0 function hierarchy "unknown_join"
    simulation.code_unit.decl 2 in 0 function hierarchy "invalid_join"
    simulation.code_unit.decl 3 in 0 function hierarchy "known_join"
    simulation.code_unit.decl 5 in 0 function hierarchy "four_state_join"
    simulation.code_unit.decl 6 in 0 function hierarchy "loop_join"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 4 : i64} {
      simulation.return
    }
    // OFF-LABEL: llvm.func @unknown_join(
    // OFF: llvm.call @talos_rt_v1_native_state_load_plane
    // OFF: llvm.return
    simulation.func @unknown_join(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %external: !simulation.net<i8> {simulation.capture_kind = 1 : i32}, %choose: i1 {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %known = simulation.context.net %ctx[0] : !simulation.net<i8>
      cf.cond_br %choose, ^join(%known : !simulation.net<i8>), ^join(%external : !simulation.net<i8>)
    ^join(%net: !simulation.net<i8>):
      %value = simulation.net.read %net : !simulation.net<i8> -> i8
      simulation.return %value : i8
    }
    // OFF-LABEL: llvm.func @invalid_join(
    // OFF: llvm.call @talos_rt_v1_native_state_load_plane
    // OFF: llvm.return
    simulation.func @invalid_join(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %choose: i1 {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<i16>
      %known = simulation.ref.extract %root from 0 : !simulation.ref<i16> -> !simulation.ref<i8>
      %past_end = arith.constant 16 : i64
      %invalid = simulation.ref.dyn_extract %root from %past_end : (!simulation.ref<i16>, i64) -> !simulation.ref<i8>
      cf.cond_br %choose, ^join(%known : !simulation.ref<i8>), ^join(%invalid : !simulation.ref<i8>)
    ^join(%ref: !simulation.ref<i8>):
      %value = simulation.ref.load %ref : !simulation.ref<i8> -> i8
      simulation.return %value : i8
    }
    // The same finite join is direct without runtime overlays, but retains
    // the generic path when writable VPI can force either resolved net.
    // OFF-LABEL: llvm.func @known_join(
    // OFF-NOT: llvm.call @talos_rt_v1_native_state_load_plane
    // OFF: llvm.select {{.*}} : i1, i8
    // OFF-NOT: llvm.call @talos_rt_v1_native_state_load_plane
    // OFF: llvm.return
    // FULL-LABEL: llvm.func @known_join(
    // FULL: llvm.call @talos_rt_v1_native_state_load_plane
    // FULL: llvm.return
    simulation.func @known_join(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %choose: i1 {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %a = simulation.context.net %ctx[0] : !simulation.net<i8>
      %b = simulation.context.net %ctx[1] : !simulation.net<i8>
      cf.cond_br %choose, ^join(%a : !simulation.net<i8>), ^join(%b : !simulation.net<i8>)
    ^join(%net: !simulation.net<i8>):
      %value = simulation.net.read %net : !simulation.net<i8> -> i8
      simulation.return %value : i8
    }
    // Ordinary four-state reads must select both planes with the same handle.
    // The value-plane optimization must not discard X/Z information.
    // OFF-LABEL: llvm.func @four_state_join(
    // OFF-NOT: llvm.call @talos_rt_v1_native_state_load_plane
    // OFF: llvm.mlir.addressof @__talos_state_value
    // OFF: llvm.select {{.*}} : i1, i8
    // OFF: llvm.mlir.addressof @__talos_state_unknown
    // OFF: llvm.select {{.*}} : i1, i8
    // OFF-NOT: llvm.call @talos_rt_v1_native_state_load_plane
    // OFF: llvm.return
    // FULL-LABEL: llvm.func @four_state_join(
    // FULL-COUNT-2: llvm.call @talos_rt_v1_native_state_load_plane
    // FULL: llvm.return
    simulation.func @four_state_join(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %choose: i1 {simulation.capture_kind = 1 : i32}) -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %a = simulation.context.net %ctx[2] : !simulation.net<!simulation.logic<8>>
      %b = simulation.context.net %ctx[3] : !simulation.net<!simulation.logic<8>>
      cf.cond_br %choose, ^join(%a : !simulation.net<!simulation.logic<8>>), ^join(%b : !simulation.net<!simulation.logic<8>>)
    ^join(%net: !simulation.net<!simulation.logic<8>>):
      %value = simulation.net.read %net : !simulation.net<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.return %value : !simulation.logic<8>
    }
    // The second iteration retains the prior handle or selects another known
    // net. Following the backedge must terminate and certify both leaves.
    // OFF-LABEL: llvm.func @loop_join(
    // OFF-NOT: llvm.call @talos_rt_v1_native_state_load_plane
    // OFF: llvm.select {{.*}} : i1, i8
    // OFF-NOT: llvm.call @talos_rt_v1_native_state_load_plane
    // OFF: llvm.return
    // FULL-LABEL: llvm.func @loop_join(
    // FULL: llvm.call @talos_rt_v1_native_state_load_plane
    // FULL: llvm.return
    simulation.func @loop_join(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %keep: i1 {simulation.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %a = simulation.context.net %ctx[0] : !simulation.net<i8>
      %b = simulation.context.net %ctx[1] : !simulation.net<i8>
      %false = arith.constant false
      %true = arith.constant true
      cf.br ^loop(%a, %false : !simulation.net<i8>, i1)
    ^loop(%net: !simulation.net<i8>, %last: i1):
      %value = simulation.net.read %net : !simulation.net<i8> -> i8
      cf.cond_br %last, ^done(%value : i8), ^advance
    ^advance:
      cf.cond_br %keep, ^loop(%net, %true : !simulation.net<i8>, i1), ^loop(%b, %true : !simulation.net<i8>, i1)
    ^done(%result: i8):
      simulation.return %result : i8
    }
  }
}
