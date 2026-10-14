// LRM 4.5, 4.6, 4.9.4, 9.4.2, 10.4.2: private state may be batched at
// the NBA barrier. An observable 0->1->0 pulse must retain both updates.
// The persistent initial process keeps the shared runtime calendar engaged.
module native_nba_batch;
  bit clk = 0;
  always #5 clk = ~clk;
  int tick = 0;
  always @(posedge clk) tick <= tick + 1;
  wire [15:0] result[16];
  for (genvar i = 0; i < 16; i++) begin : lane
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
    for (int cycle = 0; cycle < 5000; cycle++) begin
      @(posedge clk);
      if (tick != cycle) $fatal(1, "early NBA");
      #1;
      if (tick != cycle + 1) $fatal(1, "late NBA");
      if (observed != cycle + 1) $fatal(1, "lost pulse");
      if (changed != cycle + 1) $fatal(1, "lost transient change");
      if (cycle % 1000 == 0) $display("%0d %h %h", tick, result[0], result[15]);
    end
    $display("%s complete %0d %0d", label, tick, observed);
    $finish;
  end
endmodule
