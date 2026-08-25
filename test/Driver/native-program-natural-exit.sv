// RUN: obelisk -fno-lto -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: FileCheck %s < %t.o3.native.out

module top;
  natural_program p();

  initial begin
    #10;
    $display("module-must-not-run");
  end

  final $display("natural-final");
endmodule

program natural_program;
  initial begin
    #1;
    $display("first-root-%0t", $time);
  end

  initial begin
    fork
      begin
        #4;
        $display("detached-descendant-%0t", $time);
      end
    join_none
  end
endprogram

// CHECK: first-root-1
// CHECK-NEXT: detached-descendant-4
// CHECK-NEXT: natural-final
// CHECK-NOT: module-must-not-run
