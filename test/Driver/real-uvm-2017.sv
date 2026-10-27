// REQUIRES: real-uvm
// RUN: talos -emit-slang --std=1800-2017 --single-unit -I%uvm \
// RUN:   %uvm/uvm_pkg.sv %s > %t.slang.mlir
// RUN: talos-opt --convert-slang-to-talos %t.slang.mlir \
// RUN:   | FileCheck %s

// Keep one full, unmodified UVM package elaboration for this language mode.
// Converting the emitted text also checks that the Slang dialect's printed
// form of the whole package parses back.
// The user code concentrates on the randomizable class and constraint family;
// smaller property-level cases belong in the mock-UVM tests.
class talos_uvm_item extends uvm_pkg::uvm_object;
  rand bit [7:0] payload;

  constraint legal_payload {
    payload inside {[8'h01:8'hff]};
  }

  function new(string name = "talos_uvm_item");
    super.new(name);
  endfunction
endclass

module talos_real_uvm_2017;
  initial begin
    talos_uvm_item item = new;
    assert (item.randomize());
  end
endmodule

// CHECK: talos.sv.symbol.package
// CHECK: talos.sv.type.class_type
// CHECK: hierarchical_name = "talos_uvm_item"
// CHECK: talos.sv.symbol.constraint_block
// CHECK: hierarchical_name = "talos_uvm_item::legal_payload"
// CHECK: talos.sv.symbol.instance
// CHECK: talos.sv.expression.call attributes {{.*}}callee_name = "randomize"
// CHECK-NOT: slang.
// CHECK-NOT: !slang.
