// RUN: obelisk -O0 -emit-sim %s | FileCheck %s --implicit-check-not=obelisk.sv.

module assertion_lowering(input logic a);
  logic b;

  initial begin
    assert (a) b = 1; else b = 0;
    assume (a) b = 1; else b = 0;
    cover (a) b = 1;
    assert #0 (a) $display("pass"); else $display("fail");
    assert #0 (a);
    assert final (a);
  end
endmodule

program assertion_program(input logic a);
  initial assert #0 (a);
endprogram

// Ordinary immediate assertions are explicit control flow.
// CHECK-DAG: cf.cond_br
// CHECK-DAG: obelisk_sim.ref.store
// CHECK-DAG: "ERROR: {{.*}}obelisk-to-simulation-assertions.sv:{{[0-9]+}}: immediate assertion failed."

// Deferred sites are coalesced before evaluator processes are spawned.
// CHECK-DAG: obelisk_sim.assert.deferred_once
// CHECK-DAG: home_region = 8 : i32
// CHECK-DAG: home_region = 10 : i32
// CHECK-DAG: home_region = 16 : i32

// Both Observed/Postponed evaluators and the Reactive default action use the
// design domain, including when their enclosing procedural domain differs.
// CHECK-DAG: domain = 0 : i32
