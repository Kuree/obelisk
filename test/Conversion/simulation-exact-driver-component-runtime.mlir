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
  obelisk_sim.design @exact_driver_component {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<2> design
        hierarchy "top.source"
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<2> design
        hierarchy "top.alias"
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<2> design
        {driven_low = 0 : i64, driven_width = 1 : i64}
    obelisk_sim.net.pass.decl 0 in 0 0[0] to 1[1] width 1 reversed = false
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "top.member"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %driver = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<2>>
      %source = obelisk_sim.context.net %ctx[0] :
          !obelisk_sim.net<!obelisk_sim.logic<2>>
      %alias = obelisk_sim.context.net %ctx[1] :
          !obelisk_sim.net<!obelisk_sim.logic<2>>
      %process = obelisk_sim.spawn @cohort_member(
          %ctx, %driver, %source, %alias) :
          !obelisk_sim.context, !obelisk_sim.driver<!obelisk_sim.logic<2>>,
          !obelisk_sim.net<!obelisk_sim.logic<2>>,
          !obelisk_sim.net<!obelisk_sim.logic<2>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @cohort_member(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<2>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %source: !obelisk_sim.net<!obelisk_sim.logic<2>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %alias: !obelisk_sim.net<!obelisk_sim.logic<2>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %bit = obelisk_sim.driver.extract %driver from 0 :
          !obelisk_sim.driver<!obelisk_sim.logic<2>> ->
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %one = obelisk_sim.logic.constant true, false :
          !obelisk_sim.logic<1>
      %changed = obelisk_sim.driver.drive_changed %bit = %one {
        schedule.exact_driver_id = 0 : i64,
        schedule.exact_driver_low = 0 : i64
      } : !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      %source_value = obelisk_sim.net.read %source :
          !obelisk_sim.net<!obelisk_sim.logic<2>> -> !obelisk_sim.logic<2>
      %alias_value = obelisk_sim.net.read %alias :
          !obelisk_sim.net<!obelisk_sim.logic<2>> -> !obelisk_sim.logic<2>
      %expected_source = obelisk_sim.logic.constant 3 : i2, 2 : i2 :
          !obelisk_sim.logic<2>
      %expected_alias = obelisk_sim.logic.constant 3 : i2, 1 : i2 :
          !obelisk_sim.logic<2>
      %source_ok = obelisk_sim.logic.compare case_eq
          %source_value, %expected_source :
          (!obelisk_sim.logic<2>, !obelisk_sim.logic<2>) -> i1
      %alias_ok = obelisk_sim.logic.compare case_eq
          %alias_value, %expected_alias :
          (!obelisk_sim.logic<2>, !obelisk_sim.logic<2>) -> i1
      %ok = arith.andi %source_ok, %alias_ok : i1
      cf.cond_br %ok, ^pass, ^fail
    ^pass:
      %message = obelisk_sim.bytes.constant "EXACT COMPONENT PASS"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    ^fail:
      %fail_message = obelisk_sim.bytes.constant "EXACT COMPONENT FAIL"
      %fail_stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %fail_stdout(%fail_message)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }
  }
}
