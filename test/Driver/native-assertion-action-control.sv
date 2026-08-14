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

module native_assertion_action_control;
  int pass_evaluations = 0;
  int fail_evaluations = 0;
  int cover_evaluations = 0;
  int locked_evaluations = 0;
  int unlocked_evaluations = 0;

  initial begin
    $assertpassoff(0, pass_suppressed);
    pass_suppressed: assert (++pass_evaluations)
      $display("BAD-pass-suppressed");
    $display("pass-evaluations=%0d", pass_evaluations);

    $assertpasson(0, pass_enabled);
    pass_enabled: assert (1'b1) $display("pass-enabled");

    $assertfailoff(0, fail_suppressed);
    fail_suppressed: assert (++fail_evaluations == 0) else
      $display("BAD-fail-suppressed");
    $display("fail-evaluations=%0d", fail_evaluations);

    $assertfailon(0, fail_enabled);
    fail_enabled: assert (1'b0) else $display("fail-enabled");

    $assertfailoff(0, default_fail_suppressed);
    default_fail_suppressed: assert (1'b0);
    $assertfailon(0, default_fail_enabled);
    default_fail_enabled: assert (1'b0);

    // Pass controls suppress only the cover success action. The cover
    // expression still evaluates and succeeds independently.
    $assertcontrol(7, 2, 2, 0, cover_suppressed);
    cover_suppressed: cover (++cover_evaluations)
      $display("BAD-cover-suppressed");
    $display("cover-evaluations=%0d", cover_evaluations);
    $assertcontrol(6, 2, 2, 0, cover_enabled);
    cover_enabled: cover (1'b1) $display("cover-enabled");

    $assertpassoff(0, nonvacuous_enabled);
    $assertnonvacuouson(0, nonvacuous_enabled);
    nonvacuous_enabled: assert (1'b1) $display("nonvacuous-enabled");

    // Immediate assertion successes are nonvacuous, so VacuousOff does not
    // suppress their pass actions.
    $assertvacuousoff(0, vacuous_irrelevant);
    vacuous_irrelevant: assert (1'b1) $display("vacuous-irrelevant");

    $assertcontrol(1, 2, 1, 0, locked_target);
    $assertoff(0, locked_target);
    $assertpassoff(0, locked_target);
    locked_target: assert (++locked_evaluations)
      $display("locked-controls-ignored");

    $assertcontrol(1, 2, 1, 0, unlocked_target);
    $assertcontrol(2, 2, 1, 0, unlocked_target);
    $assertoff(0, unlocked_target);
    unlocked_target: assert (++unlocked_evaluations)
      $display("BAD-unlocked-attempt");
    $display("lock-evaluations=%0d/%0d", locked_evaluations,
             unlocked_evaluations);

    // Deferred attempts snapshot action state at encounter. Later control
    // changes cannot alter the queued action, while ticket replacement and
    // Kill cancellation retain their existing behavior.
    $assertpasson(0, deferred_pass_preserved);
    deferred_pass_preserved: assert #0 (1'b1)
      $display("deferred-pass-preserved");
    $assertpassoff(0, deferred_pass_preserved);

    $assertpassoff(0, deferred_pass_suppressed);
    deferred_pass_suppressed: assert #0 (1'b1)
      $display("BAD-deferred-pass-suppressed");
    $assertpasson(0, deferred_pass_suppressed);

    $assertfailon(0, deferred_fail_preserved);
    deferred_fail_preserved: assert #0 (1'b0) else
      $display("deferred-fail-preserved");
    $assertfailoff(0, deferred_fail_preserved);

    $assertfailoff(0, deferred_fail_suppressed);
    deferred_fail_suppressed: assert #0 (1'b0) else
      $display("BAD-deferred-fail-suppressed");
    $assertfailon(0, deferred_fail_suppressed);

    $assertpasson(0, final_pass_preserved);
    final_pass_preserved: assert final (1'b1)
      $display("final-pass-preserved");
    $assertpassoff(0, final_pass_preserved);

    $assertfailoff(0, final_fail_suppressed);
    final_fail_suppressed: assert final (1'b0) else
      $display("BAD-final-fail-suppressed");
    $assertfailon(0, final_fail_suppressed);

    #1;
    $finish;
  end
endmodule

// CHECK-COUNT-1: immediate assertion failed.
// CHECK-DAG: pass-evaluations=1
// CHECK-DAG: pass-enabled
// CHECK-DAG: fail-evaluations=1
// CHECK-DAG: fail-enabled
// CHECK-DAG: cover-evaluations=1
// CHECK-DAG: cover-enabled
// CHECK-DAG: nonvacuous-enabled
// CHECK-DAG: vacuous-irrelevant
// CHECK-DAG: locked-controls-ignored
// CHECK-DAG: lock-evaluations=1/0
// CHECK-DAG: deferred-pass-preserved
// CHECK-DAG: deferred-fail-preserved
// CHECK-DAG: final-pass-preserved
// CHECK-NOT: BAD-

// IR: obelisk_sim.assert.control
// IR: obelisk_sim.assert.action_state
