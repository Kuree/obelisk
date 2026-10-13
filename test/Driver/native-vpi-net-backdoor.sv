// RUN: mkdir -p %t.dir/lib %t.dir/bin
// RUN: %target_clang -fPIC -shared -nostdlib %S/Inputs/vpi_net_probe.c -I%resource_dir/include -Wl,-soname,libobelisk_vpi_net_probe.so -o %t.dir/lib/libobelisk_vpi_net_probe.so
// RUN: cd %t.dir && obelisk -O2 --vpi=read --native-scheduler=auto %s lib/libobelisk_vpi_net_probe.so -o bin/read-auto
// RUN: cd %t.dir && obelisk -O2 --vpi=full --native-scheduler=auto %s lib/libobelisk_vpi_net_probe.so -o bin/full-auto
// RUN: %t.dir/bin/read-auto > %t.read-auto.out
// RUN: FileCheck %s < %t.read-auto.out
// RUN: %t.dir/bin/full-auto | diff -u %t.read-auto.out -

// The backdoor reads a net and a variable inside a submodule while the design
// is running. Nets have no directly addressable handle once observability is
// requested, so these reads exercise the runtime plane accessor rather than a
// stamped offset, and must agree across execution tiers and schedulers.

module capture(input bit clk, input logic [7:0] source,
               output logic [7:0] captured);
  wire [7:0] masked;
  assign masked = source ^ 8'h0f;
  always @(posedge clk)
    captured <= masked;
endmodule

module vpi_nets;
  import "DPI-C" function int vpi_probe_net();
  import "DPI-C" function int vpi_probe_reg();
  import "DPI-C" function int vpi_probe_size();

  bit clock;
  logic [7:0] source;
  logic [7:0] observed;

  capture u(.clk(clock), .source(source), .captured(observed));

  always #2 clock = ~clock;

  initial begin
    source = 8'h5a;
    #21;
    $display("net=%0d reg=%0d size=%0d", vpi_probe_net(), vpi_probe_reg(),
             vpi_probe_size());
    source = 8'h00;
    #4;
    $display("after net=%0d reg=%0d", vpi_probe_net(), vpi_probe_reg());
    $finish;
  end
endmodule

// CHECK: probe startup
// The continuous assignment gives masked = 0x5a ^ 0x0f = 0x55 = 85, and the
// clocked capture has already sampled it.
// CHECK: net=85 reg=85 size=8
// Driving source to zero leaves masked = 0x0f = 15 and the register follows.
// CHECK: after net=15 reg=15
