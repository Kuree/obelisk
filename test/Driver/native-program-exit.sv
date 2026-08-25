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
  exiting_program exiting();
  survivor_program survivor();

  initial begin
    #20;
    $display("module-must-not-run");
  end

  final $display("final-ran");
endmodule

program exiting_program;
  task automatic exit_from_descendant;
    $display("exit-at-%0t", $time);
    $exit;
  endtask

  // This root and its descendant are both terminated by the descendant's
  // explicit $exit.
  initial begin
    fork
      begin
        #3;
        exit_from_descendant();
      end
    join_none
    forever #1;
  end

  // Two disjoint initial roots prove that $exit owns the
  // complete program instance, not only the calling process tree.
  initial begin
    #10;
    $display("exiting-initial-must-not-run");
  end

  initial begin
    forever begin
      #11;
      $display("exiting-forever-must-not-run");
    end
  end
endprogram

program survivor_program;
  initial begin
    #5;
    $display("survivor-first-%0t", $time);
  end

  initial begin
    #7;
    $display("survivor-last-%0t", $time);
  end
endprogram

// CHECK: exit-at-3
// CHECK-NEXT: survivor-first-5
// CHECK-NEXT: survivor-last-7
// CHECK-NEXT: final-ran
// CHECK-NOT: must-not-run
