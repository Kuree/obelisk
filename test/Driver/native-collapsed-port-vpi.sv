// RUN: mkdir -p %t.dir
// RUN: obelisk -O0 --vpi=full -emit-sim %s -o %t.dir/sim.mlir
// RUN: FileCheck %s --check-prefix=COLLAPSE < %t.dir/sim.mlir
// RUN: %target_clang -fPIC -shared -nostdlib %S/Inputs/vpi_collapsed_ports.c -I%resource_dir/include -o %t.dir/probe.so
// RUN: obelisk -O0 --vpi=full %s %t.dir/probe.so -o %t.dir/o0
// RUN: %t.dir/o0 +WRITE | FileCheck %s
// RUN: obelisk -O3 --vpi=full --mlir-timing %s %t.dir/probe.so -o %t.dir/o3 2> %t.dir/timing
// RUN: FileCheck %s --check-prefix=ADMISSION < %t.dir/timing
// RUN: %t.dir/o3 +WRITE | FileCheck %s
// RUN: obelisk -O3 --vpi=full --native-scheduler=generic %s %t.dir/probe.so -o %t.dir/generic
// RUN: %t.dir/generic +WRITE | FileCheck %s
// RUN: obelisk -O3 --vpi=full --execution-tier=bytecode %s %t.dir/probe.so -o %t.dir/bytecode
// RUN: %t.dir/bytecode +WRITE | FileCheck %s
// RUN: obelisk -O3 --vpi=read %s %t.dir/probe.so -o %t.dir/read
// RUN: %t.dir/read | FileCheck %s
// RUN: obelisk -O3 --vpi=read --execution-tier=bytecode %s %t.dir/probe.so -o %t.dir/read-bytecode
// RUN: %t.dir/read-bytecode | FileCheck %s
// CHECK-NOT: FAIL:
// ADMISSION: native eligibility: eligible=1 fully_eligible=0 cost_effective=1

// COLLAPSE: simulation.net.decl [[BUS:[0-9]+]] {{.*}} hierarchy "collapsed_ports.bus"
// COLLAPSE: simulation.net.decl [[RESULT:[0-9]+]] {{.*}} hierarchy "collapsed_ports.result"
// COLLAPSE: simulation.vpi_net_identity.decl {{[0-9]+}} backed_by [[BUS]] {{.*}} hierarchy "collapsed_ports.u.pin"
// COLLAPSE: simulation.vpi_net_identity.decl {{[0-9]+}} backed_by [[RESULT]] {{.*}} hierarchy "collapsed_ports.u.pout"
// COLLAPSE: simulation.vpi_net_identity.decl {{[0-9]+}} backed_by [[BUS]] {{.*}} hierarchy "collapsed_ports.u.leaf.pin"
// COLLAPSE: simulation.vpi_net_identity.decl {{[0-9]+}} backed_by [[RESULT]] {{.*}} hierarchy "collapsed_ports.u.leaf.pout"
// CHECK: collapsed VPI passed
// CHECK-NOT: FAIL:

module collapsed_leaf(input wire [7:0] pin, output wire [7:0] pout);
  assign pout = pin;
endmodule
module collapsed_middle(input wire [7:0] pin, output wire [7:0] pout);
  collapsed_leaf leaf(pin, pout);
endmodule
module collapsed_sink(input wire [7:0] pin);
endmodule
module collapsed_ports;
  import "DPI-C" function int collapse_setup();
  import "DPI-C" function int collapse_subscribe(int enable);
  import "DPI-C" function int collapse_check(int a, int b);
  import "DPI-C" function int collapse_put(int index, int a, int b, int mode, int writable);
  logic [7:0] drive = 8'h12;
  wire [7:0] bus, result;
  assign bus = drive;
  collapsed_middle u(bus, result);
  collapsed_sink left(right.pin), right(left.pin);
  bit writable;
  bit clock;
  int ticks[128];
  string message;
  always #1 clock = ~clock;
  for (genvar i = 0; i < 128; ++i) begin
    always @(posedge clock) ticks[i] <= ticks[i] + 1;
  end
  initial begin
    message = "collapsed VPI passed";
    writable = $test$plusargs("WRITE");
    #1;
    if (collapse_setup() || collapse_subscribe(1)) $fatal(1, "setup");
    if (writable) begin
      if (collapse_put(2, 'h34, 0, 0, 1)) $fatal(1, "deposit");
      #1;
      if (collapse_check('h12, 0) || result !== 'h12) $fatal(1, "rejected deposit changed state");
      drive = 'h12;
      #1;
      if (collapse_check('h12, 0)) $fatal(1, "unchanged driver");
      drive = 'h56;
      #1;
      if (collapse_check('h56, 0)) $fatal(1, "changed driver");
      if (collapse_put(1, 'hff, 'hff, 1, 1)) $fatal(1, "force X");
      #1;
      drive = 'h78;
      #1;
      if (collapse_check('hff, 'hff) || result !== 'x) $fatal(1, "forced");
      if (collapse_put(0, 'h78, 0, 2, 1)) $fatal(1, "release");
      #1;
      if (collapse_check('h78, 0) || result !== 'h78) $fatal(1, "released");
      if (collapse_subscribe(0)) $fatal(1, "unsubscribe");
      if (collapse_put(1, 0, 'hff, 1, 1)) $fatal(1, "force Z without subscribers");
      #1;
      if (collapse_check(0, 'hff) || result !== 'z) $fatal(1, "unsubscribed write");
    end else begin
      if (collapse_put(0, 'h34, 0, 0, 0) ||
          collapse_put(1, 'hff, 'hff, 1, 0) ||
          collapse_put(2, 0, 0, 2, 0)) $fatal(1, "read-only permissions");
      #1;
      if (collapse_check('h12, 0)) $fatal(1, "read-only changed state");
      drive = 'x;
      #1;
      if (collapse_check('hff, 'hff) || result !== 'x) $fatal(1, "read X");
      drive = 'z;
      #1;
      if (collapse_check(0, 'hff) || result !== 'z) $fatal(1, "read Z");
      if (collapse_subscribe(0)) $fatal(1, "unsubscribe");
    end
    if (ticks[127] == 0) $fatal(1, "partial native island did not run");
    $display("%s", message);
    $finish;
  end
endmodule
