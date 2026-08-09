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
// IR-COUNT-3: obelisk_sim.assert.deferred_enqueue {{[0-9]+}} {obelisk_sim.assertion_control_target_id = {{[1-9][0-9]*}} : i64}
// IR-NOT: obelisk_sim.control.enter
