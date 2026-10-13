// RUN: obelisk -O3 %s -o %t.native
// RUN: %t.native > %t.native.out 2>&1
// RUN: FileCheck %s < %t.native.out
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode 2>&1 | diff -u %t.native.out -

module native_deferred_assertion_flush;
  logic comb_value = 0;
  event wake;

  always_comb begin
    assert #0 (comb_value) $display("comb-current"); else
      $display("BAD-comb-stale");
  end

  initial begin
    comb_value <= 1;
  end

  initial begin : delay_resume
    assert #0 (1'b0) $display("BAD-delay-pass"); else
      $display("BAD-delay-stale");
    #0;
    assert #0 (1'b1) $display("delay-current"); else
      $display("BAD-delay-fail");
  end

  initial begin : event_resume
    assert #0 (1'b0) $display("BAD-event-pass"); else
      $display("BAD-event-stale");
    @wake;
    assert #0 (1'b1) $display("event-current"); else
      $display("BAD-event-fail");
  end

  initial begin
    #0;
    -> wake;
  end

  initial begin : outer_victim
    assert #0 (1'b0) $display("BAD-outer-pass"); else
      $display("BAD-outer-stale");
    #1;
  end

  initial begin
    #0;
    disable outer_victim;
  end

  initial begin : nested_owner
    begin : nested_scope
      assert #0 (1'b0) $display("BAD-nested-pass"); else
        $display("nested-kept");
      disable nested_scope;
    end
  end

  initial begin
    #2;
    $finish;
  end
endmodule

// CHECK-DAG: comb-current
// CHECK-DAG: delay-current
// CHECK-DAG: event-current
// CHECK-DAG: nested-kept
// CHECK-NOT: BAD-
