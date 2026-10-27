module blk #(parameter bit FAST = 0) (input logic clk, output int y);
  always_ff @(posedge clk)
    if (FAST) y <= 3;
    else      y <= 1;
endmodule

module top;
  logic clk = 0;
  int a, b;
  blk #(.FAST(1)) u0 (.clk(clk), .y(a));
  blk #(.FAST(0)) u1 (.clk(clk), .y(b));
  initial begin
    repeat (2) #5 clk = ~clk;
    $finish;
  end
endmodule

// Two instances of one module share its source lines. Each instance's page
// counts its own statements, while the enclosing scope merges them per line,
// so a line that one instance missed is partial, as in the line metric.
// Sources are named relative to the directory they were compiled from.
// RUN: cd %S && %talos --std=1800-2023 -O0 --target=native --coverage=line \
// RUN:   -o %t.sim html-report.sv
// RUN: %t.sim --coverage-output=%t.obcov --coverage-test=instances
// RUN: talos-cov report --format=text %t.obcov | FileCheck %s --check-prefix=TEXT
// TEXT: line: 60.00% (3/5), partial 2
// RUN: talos-cov report --format=html --source-root=%S -o %t.html %t.obcov
// RUN: %if node %{ %node %S/../Support/CheckCoverageHtml.js %t.html \
// RUN:   'row:top|top|60.0% 3/5|—|—' 'row:u0|blk|66.7% 2/3|—' 'row:u1|blk|66.7% 2/3|—' \
// RUN:   'row:html-report.sv:3:15|if (FAST) y <= 3;|1|covered|instances' \
// RUN:   'row:html-report.sv:4|else y <= 1;|0|uncovered|' \
// RUN:   '3 / 5 lines, 2 partial' %}
