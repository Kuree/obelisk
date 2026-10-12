// Standalone bounded queue probe; deliberately excluded from unit/lit tests.
module top;
  bit value;
  int count = 32;
  int ordinary;
  int step;
  covergroup cg;
    cp: coverpoint value { bins one = {1}; }
  endgroup
  cg cov;
  initial begin
    void'($value$plusargs("N=%d", count));
    if (count < 2 || count > 1024)
      $fatal(1, "bounded probe limits: 2 <= N <= 1024");
    cov = new;
    // A mixed immediate barrier plus distinct delayed deadlines. The latter
    // checks that pending future writes are not scanned at every time step.
    for (step = 0; step < count; ++step) begin
      ordinary <= step;
      cov.option.weight <= step;
      cov.option.weight[0] <= 0;
      cov.option.weight <= #(step + 1) step;
    end
    #(count + 2);
    if (ordinary != count - 1 || cov.option.weight != count - 1)
      $fatal(1, "ordered queue result mismatch");
    $display("N=%0d writes=%0d weight=%0d ordinary=%0d", count, 4 * count,
             cov.option.weight, ordinary);
    $finish;
  end
endmodule
