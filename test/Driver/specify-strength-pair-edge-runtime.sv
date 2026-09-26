// RUN: obelisk -O0 %s -o %t.native-o0
// RUN: %t.native-o0 | FileCheck %s
// RUN: obelisk -O3 %s -o %t.native-o3
// RUN: %t.native-o3 | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.bytecode-o0
// RUN: %t.bytecode-o0 | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode-o3
// RUN: %t.bytecode-o3 | FileCheck %s

`timescale 1ns / 1ns

module strength_edge_cell(input wire data, input wire enable,
                          output wire destination);
  bufif1 (strong1, pull0) gate(destination, data, enable);
  specify
    (posedge data => (destination +: data)) =
        (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
  endspecify
endmodule

module specify_strength_pair_edge_runtime;
  logic data = 0;
  logic enable = 1'bx;
  wire destination;
  strength_edge_cell dut(data, enable, destination);

  initial begin
    #13;
    $display("initial=%v", destination);
    data = 1;
    #0;
    $display("strength-change=%v", destination);
    enable = 0;
    #0;
    $display("release-now=%v", destination);
    #15;
    $display("release-later=%v", destination);
    $finish;
  end
endmodule

// IEEE 1800-2017 28.12.2 and 30.4.3: the X-valued L-to-H range change
// consumes the posedge qualification. The later control-only release must not
// reuse that stale edge and acquire a path delay.
// CHECK: initial=PuL
// CHECK-NEXT: strength-change=StH
// CHECK-NEXT: release-now=HiZ
// CHECK-NEXT: release-later=HiZ
