// RUN: %target_clang -fPIC -shared -nostdlib %S/Inputs/vpi_control.c -I%resource_dir/include -o %t.probe.so
// RUN: obelisk -O0 --vpi=full --coverage=toggle %s %t.probe.so -o %t.native
// RUN: %t.native | FileCheck %s
// RUN: obelisk -O3 --vpi=full --coverage=toggle --execution-tier=bytecode %s %t.probe.so -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s

// IEEE 1800-2023 40.5.3: vpi_control starts and stops coverage of an instance
// as $coverage_control does. 38.4: vpiFinish behaves as $finish.
module vpi_control;
  timeunit 1ns; timeprecision 1ns;
  bit q;
  import "DPI-C" context function int control_toggle(int operation);
  import "DPI-C" context function int control_unsupported();
  import "DPI-C" context function int control_finish();
  initial begin
    $display("stop=%0d", control_toggle(751));
    #1 q = 1; #1 q = 0; #1;
    $display("covered after stop=%0d", $coverage_get(23, 10, vpi_control));
    $display("start=%0d", control_toggle(750));
    #1 q = 1; #1 q = 0; #1;
    $display("covered after start=%0d", $coverage_get(23, 10, vpi_control));
    void'(control_unsupported());
    void'(control_finish());
    $display("after finish");
  end
  final $display("final");
endmodule

// CHECK: stop=1
// CHECK-NEXT: covered after stop=0
// CHECK-NEXT: start=1
// CHECK-NEXT: covered after start=2
// CHECK-NEXT: unsupported result=0 error=3
// CHECK-NOT: after finish
// CHECK: final
