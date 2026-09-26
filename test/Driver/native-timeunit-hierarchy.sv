// RUN: obelisk -O0 --top=native_timeunit_hierarchy %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk -O0 --execution-tier=bytecode --top=native_timeunit_hierarchy %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -O3 --top=native_timeunit_hierarchy %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -O3 --execution-tier=bytecode --top=native_timeunit_hierarchy %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: FileCheck %s < %t.o3.native.out

// IEEE 1800-2017 3.14.2-3.14.3: compilation-unit declarations supply the
// package and module defaults, local declarations override them, and the full
// legal 1fs through 100s range must survive runtime scope metadata.

timeunit 10ns/1ns;

package timing_pkg;
  task automatic wait_one();
    #1;
  endtask
endpackage

module inherited_delay(output bit done = 0);
  initial #1 done = 1;
endmodule

module local_delay(output bit done = 0);
  timeunit 1us/100ns;
  initial #1 done = 1;
endmodule

module positive_scale;
  timeunit 100s/10s;
endmodule

module native_timeunit_hierarchy;
  timeunit 1ns/1ns;
  bit inherited_done;
  bit local_done;
  bit package_done = 0;
  inherited_delay inherited(inherited_done);
  local_delay local_i(local_done);
  positive_scale positive();

  initial begin
    timing_pkg::wait_one();
    package_done = 1;
  end

  initial begin
    $printtimescale(inherited);
    $printtimescale(local_i);
    $printtimescale(positive);
    #9 $display("at9 inherited=%b package=%b local=%b",
                inherited_done, package_done, local_done);
    #2 $display("at11 inherited=%b package=%b local=%b",
                inherited_done, package_done, local_done);
    #989 $display("at1000 inherited=%b package=%b local=%b",
                  inherited_done, package_done, local_done);
  end
endmodule

// CHECK: Time scale of (native_timeunit_hierarchy.inherited) is 10ns / 1ns
// CHECK-NEXT: Time scale of (native_timeunit_hierarchy.local_i) is 1us / 100ns
// CHECK-NEXT: Time scale of (native_timeunit_hierarchy.positive) is 100s / 10s
// CHECK-NEXT: at9 inherited=0 package=0 local=0
// CHECK-NEXT: at11 inherited=1 package=1 local=0
// CHECK-NEXT: at1000 inherited=1 package=1 local=1
