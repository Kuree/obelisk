module top;
  logic clk = 0;
  logic a = 1;
  logic b = 1;
  logic reset_active = 1;
  controlled: assert property (@(posedge clk) a ##1 b);
  disabled: cover property (@(posedge clk) disable iff (reset_active) a);
  initial begin
    expect (@(posedge clk) a ##1 b);
  end
  task pulse;
    #1 clk = 1;
    #1 clk = 0;
  endtask
  initial begin
    $assertoff(0, controlled);
    pulse(); pulse();
    $asserton(0, controlled);
    pulse(); pulse();
    $assertkill(0, controlled);
    pulse();
    $asserton(0, controlled);
    pulse();
    #1 $finish;
  end
endmodule

// Clause 20.12 says Off and Kill start no new assertion attempts. The labeled
// assertion therefore starts three attempts: two after the first On and one
// after the second. LRM 16.12 and 16.14.3 still count a disable-iff evaluation
// as an attempt, so the always-disabled cover starts six attempts. Clause 16.17
// defines one expect execution as one evaluation thread, so its two clock ages
// still contribute one statement hit.
// RUN: obelisk --std=1800-2023 -O0 --coverage=line %s -o %t.native
// RUN: %t.native --coverage-output=%t.native.obcov
// RUN: obelisk-cov report --format=lcov %t.native.obcov -o %t.native.lcov
// RUN: FileCheck %s --check-prefix=COVERAGE < %t.native.lcov
// RUN: obelisk --std=1800-2023 -O0 --execution-tier=bytecode --coverage=line %s -o %t.bytecode
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov
// RUN: obelisk-cov report --format=lcov %t.bytecode.obcov -o %t.bytecode.lcov
// RUN: diff -u %t.native.lcov %t.bytecode.lcov

// COVERAGE: DA:6,3
// COVERAGE: DA:7,6
// COVERAGE: DA:9,1
