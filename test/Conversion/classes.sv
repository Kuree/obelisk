// RUN: obelisk-translate %s | obelisk-opt --convert-moore-to-obelisk \
// RUN:   | obelisk-opt | FileCheck %s

class payload;
  int value;

  function int get();
    return value;
  endfunction
endclass

module class_scope;
  payload object;

  initial begin
    object = new;
  end
endmodule

// CHECK: obelisk.semantic.symbol_table @payload class.classdecl
// CHECK: obelisk.semantic.symbol @value class.propertydecl
// CHECK: func.func private @"payload::get"
// CHECK: class.property_ref
// CHECK: obelisk.semantic.graph_symbol @class_scope module
// CHECK: !obelisk.class_handle<@payload>
// CHECK: class.new
// CHECK-NOT: moore.
