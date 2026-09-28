// RUN: obelisk -O3 --vpi=full --bytecode-scope=required %s -o %t.required
// RUN: %t.required
// RUN: obelisk -O3 --vpi=full --bytecode-scope=all %s -o %t.all
// RUN: %t.all
// RUN: obelisk -O3 --vpi=full --execution-tier=bytecode --bytecode-scope=required %s -o %t.bytecode
// RUN: %t.bytecode
// RUN: not obelisk --bytecode-scope=invalid %s -o %t.invalid 2>&1 | FileCheck %s

// IEEE 1800-2023 4.5: native actors still run to quiescence with state-only
// bytecode metadata, including full VPI reflection and writable state.
module bytecode_scope;
  logic a = 0;
  wire b = a;
  logic c;
  always_comb c = b;
  initial #1 a = 1;
endmodule

// CHECK: unsupported bytecode scope 'invalid'; expected required or all
