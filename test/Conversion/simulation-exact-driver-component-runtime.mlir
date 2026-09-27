// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=LLVM

// Runtime behavior is checked in ../Runtime/simulation-exact-driver-component-runtime.test.

// A selected stateful-cohort publication may bound the raw driver interval,
// but it must still resolve and notify every net bit in the selected bit's
// collapsed pass-connected component.
// LLVM-LABEL: llvm.func @cohort_member
// LLVM-COUNT-1: llvm.call @obelisk_rt_v1_scheduler_resolve_drivers

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @exact_driver_component {
    simulation.scope.decl 0 hierarchy "top"
    simulation.net.decl 0 in 0 : !simulation.logic<2> design
        hierarchy "top.source"
    simulation.net.decl 1 in 0 : !simulation.logic<2> design
        hierarchy "top.alias"
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<2> design
        {driven_low = 0 : i64, driven_width = 1 : i64}
    simulation.net.pass.decl 0 in 0 0[0] to 1[1] width 1 reversed = false
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "top.member"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %driver = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<2>>
      %source = simulation.context.net %ctx[0] :
          !simulation.net<!simulation.logic<2>>
      %alias = simulation.context.net %ctx[1] :
          !simulation.net<!simulation.logic<2>>
      %process = simulation.spawn @cohort_member(
          %ctx, %driver, %source, %alias) :
          !simulation.context, !simulation.driver<!simulation.logic<2>>,
          !simulation.net<!simulation.logic<2>>,
          !simulation.net<!simulation.logic<2>> -> !simulation.process
      simulation.return
    }

    simulation.func private @cohort_member(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %driver: !simulation.driver<!simulation.logic<2>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 0 : i64},
        %source: !simulation.net<!simulation.logic<2>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 0 : i64},
        %alias: !simulation.net<!simulation.logic<2>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %bit = simulation.driver.extract %driver from 0 :
          !simulation.driver<!simulation.logic<2>> ->
          !simulation.driver<!simulation.logic<1>>
      %one = simulation.logic.constant true, false :
          !simulation.logic<1>
      %changed = simulation.driver.drive_changed %bit = %one {
        schedule.exact_driver_id = 0 : i64,
        schedule.exact_driver_low = 0 : i64
      } : !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>
      %source_value = simulation.net.read %source :
          !simulation.net<!simulation.logic<2>> -> !simulation.logic<2>
      %alias_value = simulation.net.read %alias :
          !simulation.net<!simulation.logic<2>> -> !simulation.logic<2>
      %expected_source = simulation.logic.constant 3 : i2, 2 : i2 :
          !simulation.logic<2>
      %expected_alias = simulation.logic.constant 3 : i2, 1 : i2 :
          !simulation.logic<2>
      %source_ok = simulation.logic.compare case_eq
          %source_value, %expected_source :
          (!simulation.logic<2>, !simulation.logic<2>) -> i1
      %alias_ok = simulation.logic.compare case_eq
          %alias_value, %expected_alias :
          (!simulation.logic<2>, !simulation.logic<2>) -> i1
      %ok = arith.andi %source_ok, %alias_ok : i1
      cf.cond_br %ok, ^pass, ^fail
    ^pass:
      %message = simulation.bytes.constant "EXACT COMPONENT PASS"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    ^fail:
      %fail_message = simulation.bytes.constant "EXACT COMPONENT FAIL"
      %fail_stdout = arith.constant 1 : i32
      simulation.display %ctx to %fail_stdout(%fail_message)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    }
  }
}
