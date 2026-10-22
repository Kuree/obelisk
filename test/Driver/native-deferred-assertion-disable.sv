// RUN: obelisk -O3 %s -o %t.native
// RUN: %t.native > %t.native.out 2>&1
// RUN: FileCheck %s < %t.native.out
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode 2>&1 | diff -u %t.native.out -
// RUN: obelisk -O0 -emit-sim %s -o %t.mlir
// RUN: FileCheck %s --check-prefix=IR < %t.mlir

module native_deferred_assertion_disable;
  initial begin
    canceled: assert #0 (1'b0) $display("BAD-canceled-pass"); else
      $display("BAD-canceled-fail");
    preserved: assert #0 (1'b0) $display("BAD-preserved-pass"); else
      $display("unrelated-kept");
    default_canceled: assert #0 (1'b0);

    disable canceled;
    disable default_canceled;
    #1;
    $finish;
  end
endmodule

// CHECK-COUNT-1: unrelated-kept
// CHECK-NOT: BAD-
// CHECK-NOT: immediate assertion failed

// Assertion labels remain stable identities rather than dynamic named-block
// activations, and every deferred ticket carries its label's prepared ID.
// IR-COUNT-3: simulation.assert.deferred_enqueue {{[0-9]+}} {simulation.assertion_control_target_id = {{[1-9][0-9]*}} : i64
// IR-NOT: simulation.control.enter
