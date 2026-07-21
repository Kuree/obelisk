// RUN: obelisk -emit-slang %s | obelisk-opt | FileCheck %s --check-prefix=SOURCE
// RUN: obelisk -emit-obelisk %s | obelisk-opt | FileCheck %s --check-prefix=TARGET

module requires_parameter #(parameter int WIDTH);
  logic marker;
  initial marker = 1'b1;
endmodule

module uninstantiated_test_top;
endmodule

// The standalone definition retains identity, and slang's semantic checking
// instance carries the elaborated body even though no design instance exists.
// SOURCE: slang.symbol.definition attributes {{.*}}name = "requires_parameter"
// SOURCE: slang.symbol.instance attributes {{.*}}is_uninstantiated = true
// SOURCE-SAME: referenced_path = "requires_parameter"
// SOURCE: slang.symbol.instance_body
// SOURCE: slang.symbol.variable attributes {{.*}}name = "marker"

// TARGET: obelisk.sv.symbol.definition attributes {{.*}}name = "requires_parameter"
// TARGET: obelisk.sv.symbol.instance attributes {{.*}}is_uninstantiated = true
// TARGET-SAME: referenced_path = "requires_parameter"
// TARGET: obelisk.sv.symbol.instance_body
// TARGET: obelisk.sv.symbol.variable attributes {{.*}}name = "marker"
// TARGET-NOT: slang.
