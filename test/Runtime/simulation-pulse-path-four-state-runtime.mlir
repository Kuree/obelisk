// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir > %t.ll
// RUN: %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o %t.ll
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// IEEE 1800-2017 30.5.1 gives X->0 index 9 and Z->0 index 5 in the
// twelve-transition delay tuple. Distinct static policies make a row swap
// visible in both the native and bytecode storage schedulers.
// CHECK: at3 0 z
// CHECK-NEXT: at8 0 0

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @pulse_path_four_state_runtime {
    simulation.scope.decl 0 hierarchy "top"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design hierarchy "top.x_value"
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design hierarchy "top.z_value"
    simulation.code_unit.decl 9931200 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 9931201 in 0 initial hierarchy "top.test"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9931200 : i64,
                    simulation.lowered} {
      %x_ref = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %z_ref = simulation.context.storage %ctx[1] :
          !simulation.ref<!simulation.logic<1>>
      %z = simulation.logic.constant 1 : i1, 1 : i1 :
          !simulation.logic<1>
      simulation.ref.store %z to %z_ref :
          !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %test = simulation.spawn @test(%ctx, %x_ref, %z_ref) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>,
          !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }

    simulation.func private @test(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %x_ref: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %z_ref: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9931201 : i64,
                    simulation.lowered} {
      %zero = simulation.logic.constant 0 : i1, 0 : i1 :
          !simulation.logic<1>
      %mask = arith.constant 1 : i1
      %x_to_zero = arith.constant 512 : i12
      %z_to_zero = arith.constant 32 : i12
      %two = simulation.time.constant 2
      %seven = simulation.time.constant 7

      simulation.ref.store_inertial_path %zero to %x_ref write %mask
          active %mask masks[%mask, %mask, %mask] after[%two, %two, %two]
          site 9931201 : 0 group 0 of 2 nonblocking = false
          pulse_transitions %x_to_zero : i12
          {pulse_error = 2 : i64, pulse_reject = 2 : i64} :
          !simulation.ref<!simulation.logic<1>>,
          !simulation.logic<1>, i1
      simulation.ref.store_inertial_path %zero to %x_ref write %mask
          active %mask masks[%mask, %mask, %mask]
          after[%seven, %seven, %seven] site 9931201 : 0 group 1 of 2
          nonblocking = false pulse_transitions %z_to_zero : i12
          {pulse_error = 7 : i64, pulse_reject = 7 : i64} :
          !simulation.ref<!simulation.logic<1>>,
          !simulation.logic<1>, i1

      simulation.ref.store_inertial_path %zero to %z_ref write %mask
          active %mask masks[%mask, %mask, %mask] after[%two, %two, %two]
          site 9931202 : 0 group 0 of 2 nonblocking = false
          pulse_transitions %x_to_zero : i12
          {pulse_error = 2 : i64, pulse_reject = 2 : i64} :
          !simulation.ref<!simulation.logic<1>>,
          !simulation.logic<1>, i1
      simulation.ref.store_inertial_path %zero to %z_ref write %mask
          active %mask masks[%mask, %mask, %mask]
          after[%seven, %seven, %seven] site 9931202 : 0 group 1 of 2
          nonblocking = false pulse_transitions %z_to_zero : i12
          {pulse_error = 7 : i64, pulse_reject = 7 : i64} :
          !simulation.ref<!simulation.logic<1>>,
          !simulation.logic<1>, i1

      %three = simulation.time.constant 3
      simulation.suspend.delay %three to ^at_three
    ^at_three:
      %x3 = simulation.ref.load %x_ref :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %z3 = simulation.ref.load %z_ref :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %format3 = simulation.bytes.constant "at3 %b %b"
      %stdout3 = arith.constant 1 : i32
      simulation.display %ctx to %stdout3(%format3, %x3, %z3)
          newline = true radix = <decimal> flags = [0, 0, 0] :
          !simulation.bytes, !simulation.logic<1>, !simulation.logic<1>
      %five = simulation.time.constant 5
      simulation.suspend.delay %five to ^at_eight
    ^at_eight:
      %x8 = simulation.ref.load %x_ref :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %z8 = simulation.ref.load %z_ref :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %format8 = simulation.bytes.constant "at8 %b %b"
      %stdout8 = arith.constant 1 : i32
      simulation.display %ctx to %stdout8(%format8, %x8, %z8)
          newline = true radix = <decimal> flags = [0, 0, 0] :
          !simulation.bytes, !simulation.logic<1>, !simulation.logic<1>
      %status = arith.constant 0 : i32
      simulation.finish %ctx, %status
      simulation.return
    }
  }
}
