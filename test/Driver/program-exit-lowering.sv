// RUN: obelisk -O0 -emit-sim %s | FileCheck %s

module top;
  lowering_program p();
endmodule

program lowering_program;
  initial $exit;
  initial forever #1;
endprogram

// The frontend owns classification of program procedural roots. Runtime
// ownership, descendant propagation, completion, and final behavior are
// covered by simulation-program-exit-runtime.mlir.
// CHECK-LABEL: simulation.func private @unit_0
// CHECK-SAME: schedule.program_owner_id = [[OWNER:[0-9]+]] : i64
// CHECK: simulation.program.exit
// CHECK-LABEL: simulation.func private @unit_1
// CHECK-SAME: schedule.program_owner_id = [[OWNER]] : i64
