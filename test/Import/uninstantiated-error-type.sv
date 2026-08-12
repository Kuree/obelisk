// RUN: obelisk -emit-slang --top=top %s | FileCheck %s

// A definition with a required parameter is semantically checked by slang in
// an uninstantiated context. Parameter-dependent member types are deliberately
// unavailable there and use ErrorType recovery nodes; they are not errors in
// the selected design.
interface parameterized #(parameter int WIDTH);
  function automatic bit [WIDTH-1:0] pass(bit [WIDTH-1:0] value);
    return value;
  endfunction
endinterface

module top;
endmodule

// CHECK: slang.symbol.instance attributes {hierarchical_name = "$unit"
// CHECK-SAME: is_uninstantiated = true
// CHECK: slang.symbol.subroutine attributes
// CHECK-SAME: name = "pass"
// CHECK-SAME: semantic_type = !slang.subroutine<(!slang.error<false>) -> !slang.error<false>, false>
// CHECK: slang.symbol.formal_argument attributes
// CHECK-SAME: name = "value"
// CHECK-SAME: semantic_type = !slang.error<false>
