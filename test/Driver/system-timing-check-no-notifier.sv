// RUN: obelisk -fno-lto -O0 %s -o %t.native
// RUN: %t.native 2>&1 | FileCheck %s
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode 2>&1 | FileCheck %s

`timescale 1ns / 1ps

module check_without_notifier(input wire data, reference);
  specify
    $setup(posedge data, posedge reference, 3);
  endspecify
endmodule

module system_timing_check_no_notifier;
  logic data = 0, reference = 0;
  check_without_notifier dut(data, reference);

  initial begin
    #1 data = 1;
    #2 reference = 1;
    #0.001 $display("simulation-survived");
    $finish;
  end
endmodule

// IEEE 1800-2017 31.3.1 still reports the violation when the Clause 31.6
// optional notifier is omitted; reporting is nonfatal here.
// CHECK: warning: system timing check violation
// CHECK: simulation-survived
