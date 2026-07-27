// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s

module unsupported_assertion(input logic enable);
  property named_property;
    @(posedge enable) enable;
  endproperty
  assert property (named_property);
endmodule

// CHECK: concurrent assertions require typed Preponed sampling and a verified temporal monitor
