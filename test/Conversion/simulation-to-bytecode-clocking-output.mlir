// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=NATIVE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @clocking_output {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 9000000 in 0 root_initializer
        hierarchy "__obelisk_root"
    simulation.code_unit.decl 9000001 in 0 initial
        hierarchy "top.clocking_output"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
        hierarchy "top.clk"

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 9000000 : i64, entry_kind = 0 : i32} {
      %clock = simulation.context.storage %ctx[0]
          : !simulation.ref<!simulation.logic<1>>
      simulation.clocking_output.track posedge %clock width 1
          : !simulation.ref<!simulation.logic<1>>
      simulation.return
    }

    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 9000001 : i64, entry_kind = 1 : i32} {
      %clock = simulation.context.storage %ctx[0]
          : !simulation.ref<!simulation.logic<1>>
      %current = simulation.clocking_output.current posedge %clock width 1
          descriptor 0
          : !simulation.ref<!simulation.logic<1>>
      simulation.return
    }
  }
}

// The two operations remain executable in the closed bytecode legality set.
// CHECK: intrinsic {{.*}} id=0x0001024d inputs=2 outputs=0 flags=1
// CHECK: intrinsic {{.*}} id=0x0001024e inputs=2 outputs=1 flags=1

// NATIVE: llvm.call @obelisk_rt_v1_clocking_output_track
// NATIVE: llvm.call @obelisk_rt_v1_clocking_output_current
