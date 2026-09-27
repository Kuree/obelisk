// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
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
  simulation.design @pulse_path_runtime {
    simulation.scope.decl 0 hierarchy "top"
    simulation.net.decl 0 in 0 : !simulation.logic<1> design hierarchy "top.output"
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    simulation.code_unit.decl 9931100 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 9931101 in 0 initial hierarchy "top.test"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9931100 : i64,
                    simulation.lowered} {
      %driver = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<1>>
      %output = simulation.context.net %ctx[0] :
          !simulation.net<!simulation.logic<1>>
      %test = simulation.spawn @test(%ctx, %driver, %output) :
          !simulation.context,
          !simulation.driver<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }

    simulation.func private @test(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %driver: !simulation.driver<!simulation.logic<1>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 0 : i64},
        %output: !simulation.net<!simulation.logic<1>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9931101 : i64,
                    simulation.lowered} {
      %zero = simulation.logic.constant 0 : i1, 0 : i1 : !simulation.logic<1>
      %active = arith.constant 1 : i1
      %all_transitions = arith.constant 4095 : i12
      %delay = simulation.time.constant 5
      simulation.driver.drive_inertial_path %driver = %zero active %active
          masks[%active, %active, %active] after[%delay, %delay, %delay]
          site 9931101 : 0 group 0 of 1
          pulse_transitions %all_transitions : i12
          {pulse_error = 4 : i64, pulse_on_detect = true,
           pulse_reject = 2 : i64} :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>, i1
      %six = simulation.time.constant 6
      simulation.suspend.delay %six to ^rise

    ^rise:
      %rise_one = simulation.logic.constant 1 : i1, 0 : i1 : !simulation.logic<1>
      %rise_active = arith.constant 1 : i1
      %rise_transitions = arith.constant 4095 : i12
      %rise_delay = simulation.time.constant 5
      simulation.driver.drive_inertial_path %driver = %rise_one active %rise_active
          masks[%rise_active, %rise_active, %rise_active]
          after[%rise_delay, %rise_delay, %rise_delay]
          site 9931101 : 0 group 0 of 1
          pulse_transitions %rise_transitions : i12
          {pulse_error = 4 : i64, pulse_on_detect = true,
           pulse_reject = 2 : i64} :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>, i1
      %three = simulation.time.constant 3
      simulation.suspend.delay %three to ^fall

    ^fall:
      %fall_zero = simulation.logic.constant 0 : i1, 0 : i1 : !simulation.logic<1>
      %fall_active = arith.constant 1 : i1
      %fall_transitions = arith.constant 4095 : i12
      %fall_delay = simulation.time.constant 5
      simulation.driver.drive_inertial_path %driver = %fall_zero active %fall_active
          masks[%fall_active, %fall_active, %fall_active]
          after[%fall_delay, %fall_delay, %fall_delay]
          site 9931101 : 0 group 0 of 1
          pulse_transitions %fall_transitions : i12
          {pulse_error = 4 : i64, pulse_on_detect = true,
           pulse_reject = 2 : i64} :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>, i1
      %tick = simulation.time.constant 1
      simulation.suspend.delay %tick to ^filtered

    ^filtered:
      %filtered_value = simulation.net.read %output :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %filtered_format = simulation.bytes.constant "filtered %b"
      %stdout0 = arith.constant 1 : i32
      simulation.display %ctx to %stdout0(%filtered_format, %filtered_value)
          newline = true radix = <decimal> flags = [0, 0] :
          !simulation.bytes, !simulation.logic<1>
      %four = simulation.time.constant 4
      simulation.suspend.delay %four to ^restored

    ^restored:
      %restored_value = simulation.net.read %output :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %restored_format = simulation.bytes.constant "restored %b"
      %stdout1 = arith.constant 1 : i32
      simulation.display %ctx to %stdout1(%restored_format, %restored_value)
          newline = true radix = <decimal> flags = [0, 0] :
          !simulation.bytes, !simulation.logic<1>
      %status = arith.constant 0 : i32
      simulation.finish %ctx, %status
      simulation.return
    }
  }
}
