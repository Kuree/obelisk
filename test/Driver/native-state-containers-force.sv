// RUN: talos -O0 %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: talos -O3 %s -o %t.o3
// RUN: %t.o3 | FileCheck %s
// RUN: talos -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s
// CHECK: container force passed

// IEEE 1800-2023 10.6.2: a released procedural variable retains its value;
// a released net takes its current driver value. Exercise odd-width roots.
module native_state_containers_force;
  logic [20:0] guarded;
  logic sentinel;
  logic [36:0] wide;
  wire [36:0] driven = wide;
  initial begin
    sentinel = 1;
    wide = 'x;
    #1;
    force guarded = 21'h12345;
    guarded = '0;
    force driven = 37'h123456789;
    wide = '1;
    #1;
    if (guarded !== 21'h12345 || driven !== 37'h123456789)
      $fatal(1, "force override");
    release guarded;
    release driven;
    #1;
    if (guarded !== 21'h12345 || driven !== '1)
      $fatal(1, "release value");
    guarded = 'z;
    #1;
    if (guarded !== 'z || sentinel !== 1) $fatal(1, "post-release store");
    $display("container force passed");
    $finish;
  end
endmodule
