// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// IEEE 1800-2017 10.3.3: a vector continuous assignment that changes from
// the initial nonzero (Z) contribution to zero uses the falling delay. The
// falling delay is three ticks, so the value must have matured by tick four
// rather than waiting for the five-tick rising delay.
// CHECK: initial-fall 0000

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @vector_continuous_delay_runtime {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<4> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<4> design
    obelisk_sim.code_unit.decl 9937000 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.code_unit.decl 9937001 in 0 initial hierarchy "top.test"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9937000 : i64,
                    obelisk_sim.lowered} {
      %driver = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<4>>
      %output = obelisk_sim.context.net %ctx[0] :
          !obelisk_sim.net<!obelisk_sim.logic<4>>
      %test = obelisk_sim.spawn @test(%ctx, %driver, %output) :
          !obelisk_sim.context,
          !obelisk_sim.driver<!obelisk_sim.logic<4>>,
          !obelisk_sim.net<!obelisk_sim.logic<4>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @test(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<4>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %output: !obelisk_sim.net<!obelisk_sim.logic<4>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9937001 : i64,
                    obelisk_sim.lowered} {
      %zero = obelisk_sim.logic.constant 0 : i4, 0 : i4 :
          !obelisk_sim.logic<4>
      %rise = obelisk_sim.time.constant 5
      %fall = obelisk_sim.time.constant 3
      obelisk_sim.driver.drive_inertial %driver = %zero
          after[%rise, %fall, %rise] site 9937001 : 0 vector = true :
          !obelisk_sim.driver<!obelisk_sim.logic<4>>,
          !obelisk_sim.logic<4>
      %four = obelisk_sim.time.constant 4
      obelisk_sim.suspend.delay %four to ^check

    ^check:
      %value = obelisk_sim.net.read %output :
          !obelisk_sim.net<!obelisk_sim.logic<4>> -> !obelisk_sim.logic<4>
      %format = obelisk_sim.bytes.constant "initial-fall %b"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%format, %value)
          newline = true radix = 10 flags = [0, 0] :
          !obelisk_sim.bytes, !obelisk_sim.logic<4>
      obelisk_sim.return
    }
  }
}
