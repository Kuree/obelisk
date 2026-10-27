// RUN: talos -O3 --vpi=off --mlir-timing %s -o %t.native 2> %t.timing
// RUN: %llvm_dist/bin/llvm-nm %t.native | FileCheck %s --check-prefix=SEGMENTS
// RUN: env TALOS_RT_VERIFY_UNKNOWN_SUMMARY=1 %t.native > %t.native.out
// RUN: FileCheck %s < %t.native.out
// RUN: %t.native --execution-tier=bytecode | diff -u %t.native.out -

// A local dispatch loop must reselect the global minimum after each owner:
// the reverse chain publishes into earlier segments, including after X/Z
// changes (IEEE 1800-2023 9.4.2). Counters straddle segment and ready-word
// boundaries. Inactive observers see pre-NBA values, and all captured updates
// must be visible after the slot settles (4.4.2, 4.6, 10.4.2).
// The conditional finishes keep these owners outside ranked fusion groups.
module native_eval_dispatch_segments;
  localparam N = 520;
  bit clk = 0;
  bit [15:0] grouped_a = 0, grouped_b = 0;
  logic [7:0] source = 0;
  wire [N * 16 - 1:0] counters;
  logic [7:0] chain [N + 1];
  always #5 clk = ~clk;
  // These pure owners form a ranked group among the direct owners. Local
  // dispatch must yield to it, including its captured NBA dependency.
  always_ff @(posedge clk) grouped_a <= grouped_a + 1;
  always_ff @(posedge clk) grouped_b <= grouped_a;
  assign chain[N] = source;
  for (genvar i = 0; i < N; ++i) begin : stages
    logic [15:0] count = 0;
    assign counters[16*i +: 16] = count;
    always @(posedge clk) begin
      count <= count + 1;
      if (count == 1000 + i) $finish;
    end
    always @(chain[i+1]) begin
      chain[i] = chain[i+1];
      if (chain[i] === 8'hff) $finish;
    end
  end
  initial begin
    for (int round = 0; round < 6; ++round) begin
      @(posedge clk);
      #0;
      if (grouped_a !== round) $fatal(1, "early grouped NBA");
      for (int i = 0; i < N; ++i)
        if (counters[16*i +: 16] !== round)
          $fatal(1, "NBA committed before Inactive: %0d %0d", round, i);
      case (round)
        1: source = 'x;
        3: source = 'z;
        default: source = round;
      endcase
      #1;
      if (grouped_a !== round + 1 || grouped_b !== round)
        $fatal(1, "group dispatch/NBA order");
      for (int i = 0; i < N; ++i)
        if (counters[16*i +: 16] !== round + 1)
          $fatal(1, "missing NBA: %0d %0d", round, i);
      if (chain[0] !== source)
        $fatal(1, "backward propagation: %0d %h %h", round, chain[0], source);
      $display("round %0d count %0d chain %h", round, counters[15:0], chain[0]);
    end
    $finish;
  end
endmodule

// Ranked groups may absorb a whole segment. Require two distinct surviving
// dispatch segments without depending on the removed segment's number.
// SEGMENTS-DAG: __talos_eval_dispatch_v1.segment_{{[0-9]+}}
// SEGMENTS-DAG: __talos_eval_dispatch_v1.segment_{{[0-9]+}}
// SEGMENTS-DAG: __talos_eval_ranked_group_
// CHECK: round 0 count 1 chain 00
// CHECK-NEXT: round 1 count 2 chain xx
// CHECK-NEXT: round 2 count 3 chain 02
// CHECK-NEXT: round 3 count 4 chain zz
// CHECK-NEXT: round 4 count 5 chain 04
// CHECK-NEXT: round 5 count 6 chain 05
