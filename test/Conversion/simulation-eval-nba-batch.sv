// RUN: talos -emit-slang %s | talos-opt --test-talos-native-backend='scheduler=eval timing=true' -o %t.mlir 2> %t.trace
// RUN: FileCheck %s --check-prefix=TRACE < %t.trace
// RUN: FileCheck %s < %t.mlir

// IEEE 1800-2023 4.5, 4.6, 4.9.4, 9.4.2 and 10.4.2: private NBA state may be
// committed as one batch at the NBA barrier, but a root whose edge another
// process observes keeps every update in order.
// `pulse` (NBA root 3) has two update sites and a `posedge` observer: its
// 0->1->0 pulse must stay two ordered commits. The other four roots, including
// the round trip that only a change waiter sees, form the private batch.
// TRACE: talos NBA root keeps ordered commits: root=3 width=1 semantic-sites=2
// TRACE: talos native NBA batch: candidates=4 roots=5 private-cone=4 unread=0 wide=0 slots=0
// CHECK-LABEL: llvm.func @__talos_aot_static_nba_commit_v1(
// CHECK: llvm.call @talos_rt_v1_static_nba_commit_ordered(
// CHECK: llvm.call @talos_rt_v1_static_nba_batch_guard(
// CHECK: llvm.call @talos_rt_v1_static_nba_batch_finish(
// CHECK: llvm.call @talos_rt_v1_static_nba_batch_knownness(
// CHECK: llvm.call @talos_rt_v1_static_nba_commit_roots(

module native_nba_batch;
  bit clk = 0;
  always #5 clk = ~clk;
  int tick = 0;
  always @(posedge clk) tick <= tick + 1;
  wire [15:0] result[2];
  for (genvar i = 0; i < 2; i++) begin : lane
    logic [15:0] q = i;
    always @(posedge clk) begin
      q[7:0] <= tick[7:0] + i;
      q[15:8] <= (tick % 31 == 0) ? 8'hxz : tick[15:8];
    end
    assign result[i] = q ^ (16'h1357 + i);
  end
  bit pulse = 0;
  int observed = 0;
  always @(posedge clk) begin
    pulse <= 1;
    pulse <= 0;
  end
  always @(posedge pulse) observed++;
  bit round_trip = 0;
  int changed = 0;
  always @(posedge clk) begin
    round_trip <= 1;
    round_trip <= 0;
  end
  always @(round_trip) changed++;
  initial begin
    automatic string label = "NBA batch";
    for (int cycle = 0; cycle < 8; cycle++) begin
      @(posedge clk);
      #1;
      $display("%0d %h %h %0d %0d", tick, result[0], result[1], observed, changed);
    end
    $display("%s complete %0d %0d", label, tick, observed);
    $finish;
  end
endmodule
