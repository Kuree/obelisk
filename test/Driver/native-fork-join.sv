// RUN: obelisk -O3 %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: FileCheck %s < %t.native.out
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | diff -u %t.native.out -

`timescale 1ns/1ps
module native_fork_join;
  initial begin
    $display("start");
    fork
      begin #2; $display("join-late"); end
      begin #1; $display("join-first"); end
    join
    $display("join-done");

    fork
    join
    $display("empty-done");

    fork
      begin #1; $display("any-first"); end
      begin #3; $display("any-late"); end
    join_any
    $display("any-done");

    fork
      begin #1; $display("none-first"); end
      begin #2; $display("none-late"); end
    join_none
    $display("none-done");
    wait fork;
    $display("wait-done");

    for (int i = 0; i < 3; i++)
      fork
        automatic int captured = i;
        $display("capture=%0d", captured);
      join_none
    wait fork;
  end
endmodule

// CHECK: start
// CHECK-NEXT: join-first
// CHECK-NEXT: join-late
// CHECK-NEXT: join-done
// CHECK-NEXT: empty-done
// CHECK-NEXT: any-first
// CHECK-NEXT: any-done
// CHECK-NEXT: none-done
// CHECK-NEXT: none-first
// CHECK-NEXT: any-late
// CHECK-NEXT: none-late
// CHECK-NEXT: wait-done
// CHECK-NEXT: capture=0
// CHECK-NEXT: capture=1
// CHECK-NEXT: capture=2
