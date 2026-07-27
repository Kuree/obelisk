// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out 2>&1
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out 2>&1
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out 2>&1
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out 2>&1
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: FileCheck %s < %t.o3.native.out

module native_assertions;
  logic value = 0;
  logic unknown_value = 1'bx;

  initial begin
    assert (1'b1) $display("assert-pass");
    assert (1'b0) $display("wrong"); else $display("assert-fail");
    assume (1'b1) $display("assume-pass"); else $display("wrong");
    assume (1'b0) $display("wrong"); else $display("assume-fail");
    cover (1'b1) $display("cover-match");
    cover (1'b0) $display("wrong");
    assert (1'bz) $display("wrong"); else $display("z-fail");
    assert (unknown_value);

    repeat (2)
      assert #0 (value) $display("deferred-pass"); else
        $display("deferred-fail");
    repeat (2)
      assert #0 (1'b0);
    value = 1;
    repeat (2)
      assert final (value) $display("final-pass"); else
        $display("wrong");
    #1 $finish;
  end
endmodule

// CHECK-COUNT-2: ERROR: {{.*}}native-assertions.sv:{{[0-9]+}}: immediate assertion failed.
// CHECK-COUNT-1: assert-pass
// CHECK-COUNT-1: assert-fail
// CHECK-COUNT-1: assume-pass
// CHECK-COUNT-1: assume-fail
// CHECK-COUNT-1: cover-match
// CHECK-COUNT-1: z-fail
// CHECK-COUNT-1: deferred-pass
// CHECK-COUNT-1: final-pass
// CHECK-NOT: wrong
