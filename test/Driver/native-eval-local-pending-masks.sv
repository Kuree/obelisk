// RUN: obelisk --clocked-kernels=immediate -O3 --vpi=off --native-scheduler=eval --mlir-timing %s -o %t.o3 2> %t.timing
// RUN: FileCheck %s --check-prefix=PLAN < %t.timing
// RUN: %t.o3 > %t.o3.out
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.o3.out
// RUN: %t.o3 --execution-tier=bytecode | diff -u %t.o3.out -

// LRM 4.9.4/10.4.2: differently masked captures must both be retained.
// The current local form requires one fixed mask per root. Mixed remains on
// the canonical accumulator path, including its intervening blocking write.
module native_eval_local_pending_masks;
  bit clk = 0;
  int count = 0;
  logic [15:0] mixed = 16'hxxzz;
  always #5 clk = ~clk;
  always @(posedge clk) begin
    mixed[15:8] = mixed[15:8] + 1;
    count <= count + 1;
    mixed[7:0] <= count;
    mixed[15:8] <= count + 8'h10;
    if (count == 1) mixed[11:4] <= 'z;
  end
  initial begin
    repeat (3) begin
      @(posedge clk);
      #1;
      $display("%0d %h", count, mixed);
    end
    $finish;
  end
endmodule

// PLAN: obelisk eval transaction source roots: 2/2
// PLAN: obelisk eval local pending roots: 1 cores={{[1-9][0-9]*}} fixed-candidates=1
// OUTPUT: 1 1000
// OUTPUT-NEXT: 2 1zz1
// OUTPUT-NEXT: 3 1202
