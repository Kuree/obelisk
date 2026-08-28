// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir > %t.ll
// RUN: %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' %t.ll \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// Minimal MLIR-level exercise of the packed twelve-transition pulse policy.
// A three-tick scheduled-output pulse lies between reject=2 and error=4, so
// on-detect changes the path destination to X immediately and restores zero
// at the unchanged trailing-edge time.
// CHECK: filtered x
// CHECK-NEXT: restored 0

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @pulse_path_runtime {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design hierarchy "top.output"
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.code_unit.decl 9931100 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.code_unit.decl 9931101 in 0 initial hierarchy "top.test"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9931100 : i64,
                    obelisk_sim.lowered} {
      %driver = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %output = obelisk_sim.context.net %ctx[0] :
          !obelisk_sim.net<!obelisk_sim.logic<1>>
      %test = obelisk_sim.spawn @test(%ctx, %driver, %output) :
          !obelisk_sim.context,
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @test(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %output: !obelisk_sim.net<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9931101 : i64,
                    obelisk_sim.lowered} {
      %zero = obelisk_sim.logic.constant 0 : i1, 0 : i1 : !obelisk_sim.logic<1>
      %active = arith.constant 1 : i1
      %all_transitions = arith.constant 4095 : i12
      %delay = obelisk_sim.time.constant 5
      obelisk_sim.driver.drive_inertial_path %driver = %zero active %active
          masks[%active, %active, %active] after[%delay, %delay, %delay]
          site 9931101 : 0 group 0 of 1
          pulse_transitions %all_transitions : i12
          {pulse_error = 4 : i64, pulse_on_detect = true,
           pulse_reject = 2 : i64} :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, i1
      %six = obelisk_sim.time.constant 6
      obelisk_sim.suspend.delay %six to ^rise

    ^rise:
      %rise_one = obelisk_sim.logic.constant 1 : i1, 0 : i1 : !obelisk_sim.logic<1>
      %rise_active = arith.constant 1 : i1
      %rise_transitions = arith.constant 4095 : i12
      %rise_delay = obelisk_sim.time.constant 5
      obelisk_sim.driver.drive_inertial_path %driver = %rise_one active %rise_active
          masks[%rise_active, %rise_active, %rise_active]
          after[%rise_delay, %rise_delay, %rise_delay]
          site 9931101 : 0 group 0 of 1
          pulse_transitions %rise_transitions : i12
          {pulse_error = 4 : i64, pulse_on_detect = true,
           pulse_reject = 2 : i64} :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, i1
      %three = obelisk_sim.time.constant 3
      obelisk_sim.suspend.delay %three to ^fall

    ^fall:
      %fall_zero = obelisk_sim.logic.constant 0 : i1, 0 : i1 : !obelisk_sim.logic<1>
      %fall_active = arith.constant 1 : i1
      %fall_transitions = arith.constant 4095 : i12
      %fall_delay = obelisk_sim.time.constant 5
      obelisk_sim.driver.drive_inertial_path %driver = %fall_zero active %fall_active
          masks[%fall_active, %fall_active, %fall_active]
          after[%fall_delay, %fall_delay, %fall_delay]
          site 9931101 : 0 group 0 of 1
          pulse_transitions %fall_transitions : i12
          {pulse_error = 4 : i64, pulse_on_detect = true,
           pulse_reject = 2 : i64} :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, i1
      %tick = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %tick to ^filtered

    ^filtered:
      %filtered_value = obelisk_sim.net.read %output :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %filtered_format = obelisk_sim.bytes.constant "filtered %b"
      %stdout0 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout0(%filtered_format, %filtered_value)
          newline = true radix = 10 flags = [0, 0] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>
      %four = obelisk_sim.time.constant 4
      obelisk_sim.suspend.delay %four to ^restored

    ^restored:
      %restored_value = obelisk_sim.net.read %output :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %restored_format = obelisk_sim.bytes.constant "restored %b"
      %stdout1 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout1(%restored_format, %restored_value)
          newline = true radix = 10 flags = [0, 0] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>
      %status = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %status
      obelisk_sim.return
    }
  }
}
