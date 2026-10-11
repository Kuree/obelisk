// Standalone bounded timing probe; deliberately excluded from the test suite.
module top;
  covergroup cg(input int key) with function sample(input int value);
    option.per_instance = 1;
    cp: coverpoint value { bins selected = {key}; }
  endgroup
  cg groups[];
  int count = 16;
  initial begin
    void'($value$plusargs("N=%d", count));
    if (count < 1 || count > 128)
      $fatal(1, "N must be between 1 and 128 for this bounded probe");
    groups = new[count];
    for (int i = 0; i < count; ++i) begin
      groups[i] = new(i);
      groups[i].sample(i);
    end
    $display("CONFIGURATIONS %0d", count);
    $finish;
  end
endmodule
