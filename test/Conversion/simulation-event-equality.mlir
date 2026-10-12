// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' --test-obelisk-print-bytecode -o /dev/null 2>&1 | FileCheck %s

// IEEE 15.5.5.3: event equality compares synchronization identities, including
// null. Canonical frame and reference loads may reconstruct different bounds.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @event_equality {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.exercise"
    simulation.func @exercise(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %first = simulation.event.null
      %second = simulation.event.null
      %same = simulation.event.equal %first, %second
      simulation.return
    }
  }
}

// CHECK: opcode=39 flags=0 dst=[[LEFT:[0-9]+]] src0=1
// CHECK-NEXT: {{[0-9]+}}: opcode=39 flags=0 dst=[[RIGHT:[0-9]+]] src0=2
// CHECK-NEXT: {{[0-9]+}}: opcode=17 flags=0 dst=3 src0=[[LEFT]] src1=[[RIGHT]]
