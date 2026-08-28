// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
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
  obelisk_sim.design @pulse_path_four_state_runtime {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design hierarchy "top.x_value"
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<1> design hierarchy "top.z_value"
    obelisk_sim.code_unit.decl 9931200 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.code_unit.decl 9931201 in 0 initial hierarchy "top.test"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9931200 : i64,
                    obelisk_sim.lowered} {
      %x_ref = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %z_ref = obelisk_sim.context.storage %ctx[1] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %z = obelisk_sim.logic.constant 1 : i1, 1 : i1 :
          !obelisk_sim.logic<1>
      obelisk_sim.ref.store %z to %z_ref :
          !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %test = obelisk_sim.spawn @test(%ctx, %x_ref, %z_ref) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @test(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %x_ref: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %z_ref: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9931201 : i64,
                    obelisk_sim.lowered} {
      %zero = obelisk_sim.logic.constant 0 : i1, 0 : i1 :
          !obelisk_sim.logic<1>
      %mask = arith.constant 1 : i1
      %x_to_zero = arith.constant 512 : i12
      %z_to_zero = arith.constant 32 : i12
      %two = obelisk_sim.time.constant 2
      %seven = obelisk_sim.time.constant 7

      obelisk_sim.ref.store_inertial_path %zero to %x_ref write %mask
          active %mask masks[%mask, %mask, %mask] after[%two, %two, %two]
          site 9931201 : 0 group 0 of 2 nonblocking = false
          pulse_transitions %x_to_zero : i12
          {pulse_error = 2 : i64, pulse_reject = 2 : i64} :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, i1
      obelisk_sim.ref.store_inertial_path %zero to %x_ref write %mask
          active %mask masks[%mask, %mask, %mask]
          after[%seven, %seven, %seven] site 9931201 : 0 group 1 of 2
          nonblocking = false pulse_transitions %z_to_zero : i12
          {pulse_error = 7 : i64, pulse_reject = 7 : i64} :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, i1

      obelisk_sim.ref.store_inertial_path %zero to %z_ref write %mask
          active %mask masks[%mask, %mask, %mask] after[%two, %two, %two]
          site 9931202 : 0 group 0 of 2 nonblocking = false
          pulse_transitions %x_to_zero : i12
          {pulse_error = 2 : i64, pulse_reject = 2 : i64} :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, i1
      obelisk_sim.ref.store_inertial_path %zero to %z_ref write %mask
          active %mask masks[%mask, %mask, %mask]
          after[%seven, %seven, %seven] site 9931202 : 0 group 1 of 2
          nonblocking = false pulse_transitions %z_to_zero : i12
          {pulse_error = 7 : i64, pulse_reject = 7 : i64} :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, i1

      %three = obelisk_sim.time.constant 3
      obelisk_sim.suspend.delay %three to ^at_three
    ^at_three:
      %x3 = obelisk_sim.ref.load %x_ref :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %z3 = obelisk_sim.ref.load %z_ref :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %format3 = obelisk_sim.bytes.constant "at3 %b %b"
      %stdout3 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout3(%format3, %x3, %z3)
          newline = true radix = 10 flags = [0, 0, 0] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>
      %five = obelisk_sim.time.constant 5
      obelisk_sim.suspend.delay %five to ^at_eight
    ^at_eight:
      %x8 = obelisk_sim.ref.load %x_ref :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %z8 = obelisk_sim.ref.load %z_ref :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %format8 = obelisk_sim.bytes.constant "at8 %b %b"
      %stdout8 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout8(%format8, %x8, %z8)
          newline = true radix = 10 flags = [0, 0, 0] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>
      %status = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %status
      obelisk_sim.return
    }
  }
}
