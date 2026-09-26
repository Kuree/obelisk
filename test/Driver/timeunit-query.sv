// RUN: obelisk -O0 --vpi=off %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -O0 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.bytecode.out %t.native.out
// RUN: FileCheck %s < %t.native.out

// IEEE 1800-2017 20.11: the time-unit inquiry functions return the decimal
// exponent, in seconds, of the selected scope's time unit or precision.
module queried_scope;
  timeunit 10ns;
  timeprecision 100ps;
endmodule

module timeunit_query;
  timeunit 1us;
  timeprecision 10ns;
  queried_scope selected();

  initial begin
    $display("implicit=%0d,%0d", $timeunit, $timeprecision());
    $display("explicit=%0d,%0d", $timeunit(selected),
             $timeprecision(selected));
  end
endmodule

// CHECK: implicit=-6,-8
// CHECK-NEXT: explicit=-8,-10
