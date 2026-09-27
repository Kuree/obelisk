// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=LLVM

// Runtime behavior is checked in ../Runtime/simulation-native-unbound-dynamic-state-runtime.test.

// This deliberately omits bytecode encoding and native-state synchronization.
// A formal driver handle must therefore read and compare against the generated
// native plane, whose declaration initializer is Z.  The canonical context
// plane starts as X and is not authoritative in this configuration.
//
// LLVM-LABEL: llvm.func @read_and_rewrite_z
// LLVM-COUNT-2: llvm.call @obelisk_rt_v1_native_state_load_plane
// LLVM-COUNT-2: llvm.call @obelisk_rt_v1_native_state_store_plane

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @native_dynamic_initial_state {
    simulation.scope.decl 0 hierarchy "top"
    simulation.net.decl 0 in 0 :
        !simulation.packed_array<1 : 0 x !simulation.logic<1>> design
    simulation.driver.decl 0 in 0 drives 0 :
        !simulation.packed_array<1 : 0 x !simulation.logic<1>> design
        {driven_low = 0 : i64, driven_width = 1 : i64}
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "top.check"
    simulation.code_unit.decl 3 in 0 function hierarchy "top.read_and_rewrite_z"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %driver = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.packed_array<1 : 0 x !simulation.logic<1>>>
      %process = simulation.spawn @check(%ctx, %driver) :
          !simulation.context,
          !simulation.driver<!simulation.packed_array<1 : 0 x !simulation.logic<1>>> ->
          !simulation.process
      simulation.return
    }

    simulation.func private @check(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %driver: !simulation.driver<!simulation.packed_array<1 : 0 x !simulation.logic<1>>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %bit = simulation.driver.subelement %driver[[1]] :
          !simulation.driver<!simulation.packed_array<1 : 0 x !simulation.logic<1>>> ->
          !simulation.driver<!simulation.logic<1>>
      %raw, %changed = simulation.call @read_and_rewrite_z(%ctx, %bit) :
          (!simulation.context, !simulation.driver<!simulation.logic<1>>) ->
          (!simulation.logic<1>, i1)
      %z = simulation.logic.constant true, true : !simulation.logic<1>
      %is_z = simulation.logic.compare case_eq %raw, %z :
          (!simulation.logic<1>, !simulation.logic<1>) -> i1
      %false = arith.constant false
      %unchanged = arith.cmpi eq, %changed, %false : i1
      %ok = arith.andi %is_z, %unchanged : i1
      cf.cond_br %ok, ^pass, ^fail
    ^pass:
      %pass_message = simulation.bytes.constant "NATIVE DYNAMIC INITIAL PASS"
      %pass_stdout = arith.constant 1 : i32
      simulation.display %ctx to %pass_stdout(%pass_message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    ^fail:
      %fail_message = simulation.bytes.constant "NATIVE DYNAMIC INITIAL FAIL"
      %fail_stdout = arith.constant 1 : i32
      simulation.display %ctx to %fail_stdout(%fail_message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    }

    simulation.func private @read_and_rewrite_z(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %driver: !simulation.driver<!simulation.logic<1>>
            {simulation.capture_kind = 1 : i32})
        -> (!simulation.logic<1>, i1)
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64,
                    passthrough = ["noinline"]} {
      %raw = simulation.driver.read %driver :
          !simulation.driver<!simulation.logic<1>> ->
          !simulation.logic<1>
      %z = simulation.logic.constant true, true : !simulation.logic<1>
      %changed = simulation.driver.drive_changed %driver = %z {
        schedule.defer_net_resolution
      } : !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>
      simulation.return %raw, %changed : !simulation.logic<1>, i1
    }
  }
}
