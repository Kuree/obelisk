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
  logic action_result = 1;
  logic unknown_action_result = 0;

  task automatic capture_action(ref logic destination,
                                input logic sampled);
    destination = sampled;
    $display("mutating-action=%0d", sampled);
  endtask

  task automatic capture_unknown_action(ref logic destination,
                                        input logic sampled);
    $display("captured-unknown=%b", sampled);
    capture_action(destination, sampled);
  endtask

  initial begin
    assert (1'b1) $display("assert-pass");
    assert (1'b0) $display("wrong"); else $display("assert-fail");
    assume (1'b1) $display("assume-pass"); else $display("wrong");
    assume (1'b0) $display("wrong"); else $display("assume-fail");
    assume (1'b0);
    cover (1'b1) $display("cover-match");
    cover (1'b0) $display("wrong");
    assert (1'bz) $display("wrong"); else $display("z-fail");
    assert (unknown_value);

    repeat (2)
      assert #0 (value) $display("deferred-pass"); else
        $display("deferred-fail");
    repeat (2)
      assert #0 (1'b0);
    repeat (2) begin
      assert #0 (value) $display("replacement-pass=%0d", value); else
        $display("wrong");
      value = !value;
    end
    assert #0 (value) $display("wrong"); else
      capture_action(action_result, value);
    assert #0 (1'b0) $display("wrong"); else
      capture_unknown_action(unknown_action_result, unknown_value);
    unknown_value = 0;
    value = 1;
    repeat (2)
      assert final (value) $display("final-pass"); else
        $display("wrong");
    #1 begin
      $display("action-result=%0d", action_result);
      $display("unknown-action-result=%b", unknown_action_result);
    end
    $finish;
  end
endmodule

// CHECK-COUNT-3: ERROR: {{.*}}native-assertions.sv:{{[0-9]+}}: immediate assertion failed.
// CHECK-COUNT-1: assert-pass
// CHECK-COUNT-1: assert-fail
// CHECK-COUNT-1: assume-pass
// CHECK-COUNT-1: assume-fail
// CHECK-COUNT-1: cover-match
// CHECK-COUNT-1: z-fail
// The expression is evaluated when encountered, so changing value later does
// not turn the first site's selected failure into a pass.
// CHECK-COUNT-1: deferred-fail
// Repeated reports from one site/process are replaced by the last report, and
// pass-by-value action inputs retain their encounter-time value.
// CHECK-COUNT-1: replacement-pass=1
// Output arguments remain live references while the input argument is the
// value captured when the report was queued.
// CHECK-COUNT-1: mutating-action=0
// CHECK-COUNT-1: captured-unknown=x
// CHECK-COUNT-1: mutating-action=x
// CHECK-COUNT-1: final-pass
// CHECK-COUNT-1: action-result=0
// CHECK-COUNT-1: unknown-action-result=x
// CHECK-NOT: wrong
