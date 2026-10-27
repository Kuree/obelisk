// RUN: talos-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,talos-sim-verify-compute-graph),encode-talos-sim-to-bytecode{vpi=off require-bytecode=true},convert-talos-sim-processes-to-llvm-coroutines)' \
// RUN:   | talos-translate --mlir-to-llvmir \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %target_cxx %t.o %runtime_link_flags -o %t.exe
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s
// RUN: %t.exe | FileCheck %s

// math.ctpop is the population count of an integer at its own width. The
// bytecode POPCOUNT instruction counts every limb of a register, so a value
// wider than one 64-bit limb counts both limbs. Values come from simulation
// time so the counts cannot be folded at compile time.
// CHECK: popcount 3 2 5

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @popcount {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 9920000 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 9920001 in 0 initial hierarchy "top.count"

    simulation.func @__talos_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9920000 : i64} {
      %count = simulation.spawn @count(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func private @count(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9920001 : i64} {
      %delay = simulation.time.constant 11
      simulation.suspend.delay %delay to ^counted

    ^counted:
      // 11 = 0b1011 has three ones, its low two bits 0b11 have two, and the
      // 80-bit value 11 | 1 << 70 | 1 << 79 has five.
      %now = simulation.time.now %ctx
      %small = arith.trunci %now : i64 to i8
      %ones = math.ctpop %small : i8
      %narrow = arith.trunci %now : i64 to i2
      %low = math.ctpop %narrow : i2
      %wide = arith.extui %now : i64 to i80
      %high = arith.constant 605643501428031998656512 : i80
      %joined = arith.ori %wide, %high : i80
      %many = math.ctpop %joined : i80
      %format = simulation.bytes.constant "popcount %0d %0d %0d"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%format, %ones, %low, %many)
          newline = true radix = <decimal> flags = [0, 0, 0, 0]
          : !simulation.bytes, i8, i2, i80
      simulation.finish %ctx, %stdout
      simulation.return
    }
  }
}
