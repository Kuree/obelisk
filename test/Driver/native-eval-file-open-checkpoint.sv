// RUN: rm -rf %t && mkdir -p %t && cd %t
// RUN: obelisk -O3 --native-scheduler=auto --mlir-timing %s -o %t/auto 2> %t/timing
// RUN: FileCheck %s --check-prefix=TIER < %t/timing
// RUN: obelisk -O0 --native-scheduler=generic %s -o %t/reference
// RUN: %t/auto > %t/auto.out && %t/reference > %t/reference.out
// RUN: diff -u %t/reference.out %t/auto.out
// RUN: FileCheck %s < %t/auto.out

// $fopen (IEEE 1800-2023 21.3.1) works on the runtime's descriptor table, so
// the clocked body that calls it cannot join the runtime-free eval closure.
// Each of its activations runs through a runtime checkpoint instead, and the
// rest of the design stays in the generated loop.
module native_eval_file_open_checkpoint;
  logic clk = 0, reset_i = 1;
  integer fd;
  logic [7:0] a = 0;
  always #5 clk = ~clk;
  always_ff @(posedge clk) a <= a + 1;
  always_ff @(negedge reset_i) fd = $fopen("out.txt", "w");
  initial begin
    #23 reset_i = 0;
    #50 $display("a=%0d fd=%0d", a, fd != 0);
    $finish;
  end
endmodule

// TIER: checkpoint admission: {{.*}} reason=runtime-state-access operation=simulation.file.open
// TIER: obelisk eval executor inventory: {{.*}} runtime_checkpoints=1
// TIER-NOT: error
// CHECK: a=7 fd=1
