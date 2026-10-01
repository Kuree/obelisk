// RUN: obelisk -O3 --native-scheduler=auto -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll
// RUN: obelisk -O3 --native-scheduler=auto %s -o %t.native
// RUN: obelisk -O0 --native-scheduler=generic %s -o %t.reference
// RUN: %t.native > %t.native.out
// RUN: %t.reference > %t.reference.out
// RUN: diff -u %t.reference.out %t.native.out
// RUN: FileCheck %s < %t.native.out

module publication_clock_source(output logic clk = 0, output integer cycles);
  initial begin
    clk <= 0;
    cycles = 0;
    forever begin
      #5 clk <= 0;
      #5 clk <= 1;
      cycles += 1;
    end
  end
endmodule

// LRM 4.5, 9.4.2, 10.4.2: the clock's generated NBA and port copy must
// deliver runtime testbench waits before advancing time. A blocking writer
// must also keep its own source event control inactive while notifying an
// independent runtime waiter of the same transition.
module native_tier1_runtime_publication;
  logic clk;
  integer cycles;
  publication_clock_source source(clk, cycles);
  logic [7:0] trips = 0;
  initial forever begin
    @(posedge clk or trips);
    if (clk) trips = trips + 1;
  end
  bit [7:0] q[128];
  for (genvar i = 0; i < 128; i++)
    always @(posedge clk) q[i] <= q[i] + 1;
  initial begin
    automatic string label = "observed";
    repeat (3) @(trips);
    $display("%s %0d", label, trips);
  end
  initial begin
    automatic string label = "final";
    repeat (4) @(negedge clk);
    $display("%s trips=%0d q=%0d cycles=%0d", label, trips, q[0], cycles);
    $finish;
  end
endmodule

// LLVM: call void @obelisk_rt_v1_scheduler_static_transition_owned
// CHECK: observed 3
// CHECK-NEXT: final trips=4 q=4 cycles=4
