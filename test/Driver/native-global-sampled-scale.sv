// RUN: obelisk -fno-lto --std=1800-2023 -O0 --native-scheduler=generic -D SIZE=32 -D CYCLES=20 %s -o %t.native
// RUN: obelisk -fno-lto --std=1800-2023 -O0 --execution-tier=bytecode -D SIZE=32 -D CYCLES=20 %s -o %t.bytecode
// RUN: %t.native > %t.native.out
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s < %t.native.out

`ifndef SIZE
`define SIZE 64
`endif
`ifndef CYCLES
`define CYCLES 1000
`endif

module l18_global_sampled_scale;
  localparam int SIZE = `SIZE;
  logic gclk = 0, endpoint = 0;
  logic [SIZE-1:0] value = '0;
  global clocking gcb @(posedge gclk); endclocking

  for (genvar i = 0; i < SIZE; i++) begin : checks
    assert property (@(posedge endpoint) $future_gclk(value[i]));
  end

  initial begin
    for (int cycle = 0; cycle < `CYCLES; cycle++) begin
      #1 endpoint = 1;
      #1 begin endpoint = 0; value = '1; end
      #1 gclk = 1;
      #1 begin gclk = 0; value = '0; end
    end
    $display("DONE %0d %0d", SIZE, `CYCLES);
    $finish;
  end
endmodule

// CHECK: DONE 32 20
// CHECK-NOT: assertion failed
