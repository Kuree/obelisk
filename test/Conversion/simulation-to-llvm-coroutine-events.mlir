// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @events {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "events.process"

    simulation.func @process(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %event = simulation.context.event %ctx[7] : !simulation.event
      %delay = simulation.time.constant 3
      simulation.event.trigger %event nonblocking = false
      simulation.event.trigger %event after %delay nonblocking = true
      // Compiler-private Clause 31 timers schedule/replace with a delay and
      // cancel without one; ordinary event triggers keep the existing ABI.
      simulation.event.trigger %event after %delay nonblocking = true {replaceable}
      simulation.event.trigger %event nonblocking = true {replaceable}
      %triggered = simulation.event.triggered %event
      %equal = simulation.event.equal %event, %event
      simulation.return
    }
  }
}

// CHECK-DAG: llvm.func @obelisk_rt_v1_scheduler_event_after
// CHECK-DAG: llvm.func @obelisk_rt_v1_scheduler_event_replace_after
// CHECK-DAG: llvm.func @obelisk_rt_v1_scheduler_event_triggered
// CHECK-LABEL: llvm.func @process
// CHECK: llvm.call @obelisk_rt_v1_scheduler_event_after
// CHECK: llvm.call @obelisk_rt_v1_scheduler_event_after
// CHECK: llvm.call @obelisk_rt_v1_scheduler_event_replace_after
// CHECK: llvm.call @obelisk_rt_v1_scheduler_event_replace_after
// CHECK: llvm.call @obelisk_rt_v1_scheduler_event_triggered
// CHECK: llvm.icmp "eq"
// CHECK-NOT: simulation.event
// Ordinary blocking/nonblocking event triggers retain IDs and flags exactly;
// the cold timer service has its own intrinsic and adds no ordinary hot-case
// branch.
// BYTECODE: intrinsic {{[0-9]+}}: id=0x00010202 inputs=1 outputs=0 flags=0
// BYTECODE: intrinsic {{[0-9]+}}: id=0x00010202 inputs=2 outputs=0 flags=1
// BYTECODE: intrinsic {{[0-9]+}}: id=0x0001024b inputs=2 outputs=0 flags=0
// BYTECODE: intrinsic {{[0-9]+}}: id=0x0001024b inputs=1 outputs=0 flags=0
