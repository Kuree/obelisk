// RUN: obelisk -fno-lto -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out 2>&1
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out 2>&1
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out 2>&1
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out 2>&1
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: FileCheck %s < %t.o3.native.out
// RUN: obelisk -O0 -emit-sim %s -o %t.mlir
// RUN: FileCheck %s --check-prefix=IR < %t.mlir

module assertion_control_child;
  initial begin
    #1;
    child_kept: assert (1'b0) else $display("child-kept");
  end
endmodule

module native_assertion_control;
  assertion_control_child child();
  int evaluations = 0;

  initial begin
    $assertoff(0, side_effect_masked);
    side_effect_masked: assert (++evaluations);
    $asserton(0, side_effect_masked);
    $display("disabled-evaluations=%0d", evaluations);

    off_preserves: assert #0 (1'b0) else $display("off-preserved");
    $assertoff(0, off_preserves);
    off_final_preserves: assert final (1'b0) else
      $display("off-final-preserved");
    $assertoff(0, off_final_preserves);

    kill_cancels: assert final (1'b0) else
      $display("BAD-kill-canceled");
    $assertkill(0, kill_cancels);
    kill_zero_cancels: assert #0 (1'b0) else
      $display("BAD-kill-zero-canceled");
    $assertkill(0, kill_zero_cancels);

    // levels=1 affects this instance, but not its child instance.
    $assertoff(1, native_assertion_control);
    #1;
    top_suppressed: assert (1'b0) else $display("BAD-top-suppressed");
    $asserton(1, native_assertion_control);
    top_reenabled: assert (1'b0) else $display("top-reenabled");

    $assertoff(1);
    levels_only_masked: assert (1'b0) else
      $display("BAD-levels-only-mask");
    $asserton(1);
    levels_only_reenabled: assert (1'b0) else
      $display("levels-only-reenabled");

    $assertcontrol(4, 2, 1, 1);
    full_levels_only_masked: assert (1'b0) else
      $display("BAD-full-levels-only-mask");
    $assertcontrol(3, 2, 1, 1);
    full_levels_only_reenabled: assert (1'b0) else
      $display("full-levels-only-reenabled");

    // A simple-immediate assert mask must not suppress observed-deferred
    // assertions, and an assert directive mask must not suppress cover.
    $assertcontrol(4, 2, 1, 0, native_assertion_control);
    simple_masked: assert (1'b0) else $display("BAD-simple-mask");
    deferred_kept: assert #0 (1'b0) else $display("deferred-kept");
    cover_kept: cover (1'b1) $display("cover-kept");
    assume_kept: assume (1'b0) else $display("assume-kept");
    $assertcontrol(3, 2, 1, 0, native_assertion_control);

    $assertcontrol(4, 2, 2, 0, native_assertion_control);
    cover_masked: cover (1'b1) $display("BAD-cover-mask");
    assert_kept: assert (1'b0) else $display("assert-kept");
    $assertcontrol(3, 2, 2, 0, native_assertion_control);

    $assertcontrol(4, 2, 4, 0, native_assertion_control);
    assume_masked: assume (1'b0) else $display("BAD-assume-mask");
    assert_after_assume_mask: assert (1'b0) else
      $display("assert-after-assume-mask");
    $assertcontrol(3, 2, 4, 0, native_assertion_control);

    $assertoff;
    global_masked: assert (1'b0) else $display("BAD-global-mask");
    $asserton;
    global_reenabled: assert (1'b0) else $display("global-reenabled");
    #1;
    $finish;
  end
endmodule

// CHECK-DAG: off-preserved
// CHECK-DAG: disabled-evaluations=0
// CHECK-DAG: off-final-preserved
// CHECK-DAG: child-kept
// CHECK-DAG: top-reenabled
// CHECK-DAG: levels-only-reenabled
// CHECK-DAG: full-levels-only-reenabled
// CHECK-DAG: deferred-kept
// CHECK-DAG: cover-kept
// CHECK-DAG: assume-kept
// CHECK-DAG: assert-kept
// CHECK-DAG: assert-after-assume-mask
// CHECK-DAG: global-reenabled
// CHECK-NOT: BAD-

// IR: obelisk_sim.assert.control
// IR: obelisk_sim.assert.enabled
