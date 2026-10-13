// RUN: %split-file %s %t
// RUN: obelisk -emit-slang %t/success.sv | obelisk-opt --convert-slang-to-obelisk '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=SUCCESS
// RUN: obelisk -emit-slang %t/cycle.sv | not obelisk-opt --convert-slang-to-obelisk '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=CYCLE
// RUN: obelisk -emit-slang %t/output-formal.sv | obelisk-opt --convert-slang-to-obelisk '--test-obelisk-edit-attributes=op=obelisk.sv.symbol.formal_argument match={name = "value"} set={direction = 1 : i32}' -o %t/output-formal.mlir
// RUN: not obelisk-opt %t/output-formal.mlir '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=OUTPUT
// RUN: obelisk -emit-slang %t/success.sv | obelisk-opt --convert-slang-to-obelisk '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' '--lower-obelisk-to-sim=opt-level=0' --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: obelisk -emit-slang %t/success.sv | obelisk-opt --convert-slang-to-obelisk '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' '--lower-obelisk-to-sim=opt-level=0' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// The function body is expanded into both the generated checker and residual
// program. Its rand `bias` read remains a pre-solve capture, while the actual
// argument `y` remains a candidate variable. The implicit y-before-x edge sets
// the solve-before program flag (2) without a source solve-before constraint.
// SUCCESS-LABEL: simulation.func private @unit_1
// SUCCESS: simulation.managed.load
// SUCCESS: arith.addi
// SUCCESS: simulation.random.solve
// SUCCESS-SAME: captures(%{{.*}})
// SUCCESS-SAME: "ODR1\01\00\18\00\0A\00\00\00
// SUCCESS-SAME: \02\00\00\00
// The two path masks for s.b -> s.a are preserved instead of collapsing to
// the containing property: s starts at bit 6, so the masks are 0xc0 and 0x300.
// SUCCESS-SAME: \C0\00\00\00\00\00\00\00\00\03\00\00\00\00\00\00
// CYCLE: error: solve before ordering contains a cycle
// OUTPUT: error: constraint functions cannot have output, inout, or non-const ref arguments

//--- success.sv
typedef struct packed {
  bit [1:0] a;
  bit [1:0] b;
} Pair;
class C;
  rand bit [1:0] x, y, bias;
  rand Pair s;
  function bit [1:0] map(bit [1:0] value);
    map = value + bias;
  endfunction
  constraint rules {
    x == map(y);
    s.a[1:0] == map(s.b[1:0]);
    s[0] == 1'b0;
  }
endclass
module top;
  C object;
  initial void'(object.randomize());
endmodule

//--- cycle.sv
class C;
  rand bit x, y;
  function bit id(bit value);
    return value;
  endfunction
  constraint rules {
    solve x before y;
    x == id(y);
  }
endclass
module top;
  C object;
  initial void'(object.randomize());
endmodule

//--- output-formal.sv
// The frontend rejects an output formal in a constraint function, so the RUN
// line turns this input formal into an output one; there is no explicit solve
// edge, so the purity contract is the first lowering failure.
class C;
  rand bit x;
  function bit bad(bit value);
    return value;
  endfunction
  constraint rules { bad(x); }
endclass
module top;
  C object;
  initial void'(object.randomize());
endmodule
