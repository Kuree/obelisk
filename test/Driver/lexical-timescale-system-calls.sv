// RUN: obelisk -fno-lto -O0 --top=lexical_timescale_system_calls %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode --top=lexical_timescale_system_calls %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -fno-lto -O3 --top=lexical_timescale_system_calls %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode --top=lexical_timescale_system_calls %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: FileCheck %s < %t.o3.native.out

// Preserve the lexical time scope on system calls in subroutines. Such calls
// are lowered once and remain constant metadata in both execution tiers.
timeunit 100ps/10ps;

task automatic unit_report;
  $display("unit-query=%0d,%0d", $timeunit, $timeprecision);
  $printtimescale;
endtask

package lexical_timescale_pkg;
  task automatic package_report;
    $display("package-query=%0d,%0d", $timeunit, $timeprecision);
    $printtimescale;
  endtask
endpackage

module lexical_timescale_system_calls;
  timeunit 1ns/1ps;

  task automatic module_report;
    $display("module-query=%0d,%0d", $timeunit, $timeprecision);
    $printtimescale;
  endtask

  initial begin
    unit_report();
    lexical_timescale_pkg::package_report();
    module_report();
  end
endmodule

// CHECK: unit-query=-10,-11
// CHECK-NEXT: Time scale of ($unit::) is 100ps / 10ps
// CHECK-NEXT: package-query=-10,-11
// CHECK-NEXT: Time scale of (lexical_timescale_pkg::) is 100ps / 10ps
// CHECK-NEXT: module-query=-9,-12
// CHECK-NEXT: Time scale of (lexical_timescale_system_calls) is 1ns / 1ps
