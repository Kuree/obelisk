// REQUIRES: real-uvm
// RUN: obelisk -emit-slang --std=1800-2017 --single-unit -I%uvm \
// RUN:   %uvm/uvm_pkg.sv %s > %t.slang.mlir
// RUN: obelisk-opt %t.slang.mlir -o %t.roundtrip.mlir
// RUN: obelisk-opt --convert-slang-to-obelisk %t.roundtrip.mlir \
// RUN:   | FileCheck %s

// Keep one full, unmodified UVM package elaboration for this language mode.
// The user code concentrates on the randomizable class and constraint family;
// smaller property-level cases belong in the mock-UVM tests.
class obelisk_uvm_item extends uvm_pkg::uvm_object;
  rand bit [7:0] payload;

  constraint legal_payload {
    payload inside {[8'h01:8'hff]};
  }

  function new(string name = "obelisk_uvm_item");
    super.new(name);
  endfunction
endclass

module obelisk_real_uvm_2017;
  initial begin
    obelisk_uvm_item item = new;
    assert (item.randomize());
  end
endmodule

// CHECK: obelisk.sv.symbol.package
// CHECK: obelisk.sv.type.class_type
// CHECK: hierarchical_name = "obelisk_uvm_item"
// CHECK: obelisk.sv.symbol.constraint_block
// CHECK: hierarchical_name = "obelisk_uvm_item::legal_payload"
// CHECK: obelisk.sv.symbol.instance
// CHECK: obelisk.sv.expression.call attributes {{.*}}callee_name = "randomize"
// CHECK-NOT: slang.
// CHECK-NOT: !slang.
