// RUN: %obelisk --std=1800-2023 -emit-obelisk %s -o %t.source
// RUN: obelisk-opt %t.source '--lower-obelisk-to-sim=vpi=full opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-p:64:64-i64:64-i32:32-i16:16-i8:8\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t
// RUN: env OBELISK_TEST_INPUT=%t %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SourceIntegralOperators
// RUN: obelisk-opt %t.source '--lower-obelisk-to-sim=vpi=full opt-level=3' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-p:64:64-i64:64-i32:32-i16:16-i8:8\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.opt
// RUN: env OBELISK_TEST_INPUT=%t.opt %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SourceIntegralOperators

module top;
  bit [7:0] a, b;
  bit signed [7:0] negative;
  logic [7:0] condition;
  bit [7:0] sum, inverted, unsupported_cast;
  bit [15:0] widened, unsigned_context;
  bit less;
  logic [7:0] choice, quotient;

  initial sum = a + b;
  initial widened = (a + b) << 8'd1;
  initial unsigned_context = -negative + 16'd1;
  initial less = a < b;
  initial choice = condition ? a : b;
  initial inverted = ~a;
  initial quotient = a / b;
  initial unsupported_cast = 8'(a);
endmodule
