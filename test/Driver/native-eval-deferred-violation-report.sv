// RUN: talos -O3 --mlir-timing %s -o %t.runtime-calendar 2> %t.timing
// RUN: FileCheck %s --check-prefix=TIER < %t.timing
// RUN: talos -O3 -DGENERATED_CALENDAR %s -o %t.generated-calendar
// RUN: talos -O0 --native-scheduler=generic %s -o %t.reference
// RUN: %t.reference > %t.reference.out 2>&1
// RUN: %t.runtime-calendar > %t.runtime-calendar.out 2>&1 && diff -u %t.reference.out %t.runtime-calendar.out
// RUN: %t.generated-calendar > %t.generated-calendar.out 2>&1 && diff -u %t.reference.out %t.generated-calendar.out
// RUN: FileCheck %s --implicit-check-not=warning: < %t.reference.out

// IEEE 1800-2023 12.4.2.1: a unique, unique0 or priority violation is
// detected when the statement executes, but reported only when it matures in
// the Observed region; a resumption of the process before then discards it.
// `u1` is the clause's example, with `not_a` lagging through an Inactive
// region event (4.4.2.3) so `a1` always first sees it stale and violates;
// `a1` resumes once it settles, flushing the report, so nothing is printed.
// The case statements violate in settled states, in distinct slots, and
// report once per slot.
// The processes stay in the generated loop in both calendar modes.
module native_eval_deferred_violation_report;
`ifdef GENERATED_CALENDAR
  export "DPI-C" function host_period;
  function int host_period();
    return 10;
  endfunction
`endif
  logic clk = 0;
  logic a = 0, b = 1, c = 0;
  logic not_a = 1, z;
  logic [2:0] s = 0;
  logic [3:0] y;
  always #5 clk = ~clk;
  always_ff @(posedge clk) a <= ~a;
  always_ff @(posedge clk) s <= s + 1;
  always @(a) #0 not_a = !a;
  always_comb begin : a1
    u1: unique if (a)
      z = a | b;
    else if (not_a)
      z = a | c;
  end
  always_comb begin
    y = 0;
    unique case (s)
      3'd1, 3'd2: y = 1;
      3'd2, 3'd3: y = 2;
      3'd4: y = 3;
    endcase
    priority case (s)
      3'd0, 3'd2, 3'd3, 3'd4, 3'd5, 3'd6, 3'd7: y[3] = 1;
    endcase
  end
  initial begin
    #80 $display("done y=%0d z=%b", y, z);
    $finish;
  end
endmodule

// TIER: talos eval executor inventory: {{.*}} runtime_checkpoints=0
// CHECK: :43:5: warning: unique case violation: no match
// CHECK-NEXT: :48:5: warning: priority case violation: no match
// CHECK-NEXT: :43:5: warning: unique case violation: multiple matches
// CHECK-NEXT: :43:5: warning: unique case violation: no match
// CHECK-NEXT: :43:5: warning: unique case violation: no match
// CHECK-NEXT: :43:5: warning: unique case violation: no match
// CHECK-NEXT: :43:5: warning: unique case violation: no match
// CHECK: done y=8 z=0
