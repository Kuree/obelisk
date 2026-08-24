// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

`timescale 1ns / 1ns

// IEEE 1800-2017 20.4.2: every $timeformat argument is optional. An empty
// ordered argument selects that position's default just like omitting it.
module timeformat_empty_arguments;
  initial begin
    #10;
    $timeformat(-9,,,);
    $display("trailing=%0t", $time);
    $timeformat(, 3, "ns",);
    $display("mixed=%0t", $time);
    $timeformat(,,,);
    $display("all=%0t", $time);
  end
endmodule

// CHECK: trailing={{ *}}10
// CHECK-NEXT: mixed={{ *}}10.000ns
// CHECK-NEXT: all={{ *}}10
