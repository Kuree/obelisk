// RUN: talos --coverage=functional -emit-sim %s -o %t.mlir
// RUN: talos-opt %t.mlir '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' '--encode-talos-sim-to-bytecode=vpi=off' '--test-talos-print-bytecode=metadata=true' -o /dev/null 2>&1 | FileCheck %s

// IEEE 1800-2023 19.3 and 13.5.1 require an input constructor formal to
// preserve its assigned typed value. A later formal read uses that declared
// type, so a known four-state argument still needs the two-plane ABI. The
// unrelated known logic option expression remains compact one-plane bits.
// CHECK: layout function=[[F:[0-9]+]] register=3 kind=1 flags=0 width=32
// CHECK-SAME: size=8
// CHECK: layout function=[[F]] register=4 kind=2 flags=0 width=32
// CHECK-SAME: size=16

module top;
  bit [7:0] sampled;
  covergroup cg(input integer frozen);
    point: coverpoint sampled { bins zero = {0}; }
  endgroup
  cg cov;
  initial begin
    cov = new(5);
    cov.sample();
    $finish;
  end
endmodule
