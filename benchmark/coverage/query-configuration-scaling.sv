// Standalone bounded type-query probe; deliberately excluded from tests.
module top;
  covergroup average(input int key) with function sample(input int value);
    option.per_instance = 1;
    cp: coverpoint value { bins selected = {key}; }
  endgroup
  covergroup merged(input int key) with function sample(input int value);
    option.per_instance = 1;
    type_option.merge_instances = 1;
    cp: coverpoint value { bins selected = {key}; }
  endgroup
  average averages[];
  merged unions[];
  int count = 16;
  int queries = 1;
  int covered, total;
  real score;
  initial begin
    void'($value$plusargs("N=%d", count));
    void'($value$plusargs("Q=%d", queries));
    if (count < 2 || count > 128 || queries < 1 || queries > 16)
      $fatal(1, "bounded probe limits");
    if (!$test$plusargs("load_only")) begin
      averages = new[count];
      unions = new[count];
      for (int i = 0; i < count; ++i) begin
        averages[i] = new(i);
        unions[i] = new(i);
        if (i % 2 == 0) begin
          averages[i].sample(i);
          unions[i].sample(i);
        end
      end
    end
    for (int q = 0; q < queries; ++q) begin
      score = average::get_coverage(covered, total);
      if (q == 0) $display("AVERAGE %.0f %0d/%0d", score, covered, total);
      score = merged::get_coverage(covered, total);
      if (q == 0) $display("MERGED %.0f %0d/%0d", score, covered, total);
    end
    $display("GLOBAL %.0f", $get_coverage());
    $finish;
  end
endmodule
