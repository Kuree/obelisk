// RUN: obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: obelisk-opt %t.mlir --canonicalize --cse | FileCheck %s
// RUN: obelisk-opt %t.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: obelisk-opt %t.mlir --encode-obelisk-sim-to-bytecode='vpi=off' -o /dev/null
// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t | FileCheck %s --check-prefix=RUNTIME

// Constructor-time bins and options keep typed phase/ordinal order: every bin
// expression in coverpoint order, then the option expressions. The 260
// coverpoints cross an ordinal byte boundary (255 to 256) as well as the
// bin/option phase boundary; the constructor itself checks the whole order.
// CHECK: simulation.call @[[MARK:unit_[0-9]+]](%arg0, %c0_i32{{(_[0-9]+)?}}, %c0_i32{{(_[0-9]+)?}})
// CHECK: simulation.call @[[MARK]](%arg0, %c1_i32{{(_[0-9]+)?}}, %c1_i32{{(_[0-9]+)?}})
// CHECK: simulation.call @[[MARK]](%arg0, %c2_i32{{(_[0-9]+)?}}, %c2_i32{{(_[0-9]+)?}})
// CHECK: simulation.call @[[MARK]](%arg0, %c254_i32{{(_[0-9]+)?}}, %c254_i32{{(_[0-9]+)?}})
// CHECK: simulation.call @[[MARK]](%arg0, %c255_i32{{(_[0-9]+)?}}, %c255_i32{{(_[0-9]+)?}})
// CHECK: simulation.call @[[MARK]](%arg0, %c256_i32{{(_[0-9]+)?}}, %c256_i32{{(_[0-9]+)?}})
// CHECK: simulation.call @[[MARK]](%arg0, %c257_i32{{(_[0-9]+)?}}, %c257_i32{{(_[0-9]+)?}})
// CHECK: simulation.call @[[MARK]](%arg0, %c259_i32{{(_[0-9]+)?}}, %c259_i32{{(_[0-9]+)?}})
// CHECK: simulation.call @[[MARK]](%arg0, %c2000_i32{{(_[0-9]+)?}}, %c100_i32{{(_[0-9]+)?}})
// CHECK: simulation.call @[[MARK]](%arg0, %c2001_i32{{(_[0-9]+)?}}, %c1_i32{{(_[0-9]+)?}})
// RUNTIME: ordered=1 calls=262

`define CP(n) cp``n: coverpoint sampled { bins selected = {mark(n, n)}; }
`define CP10(p) `CP(p``0) `CP(p``1) `CP(p``2) `CP(p``3) `CP(p``4) `CP(p``5) `CP(p``6) `CP(p``7) `CP(p``8) `CP(p``9)
`define CP100(p) `CP10(p``0) `CP10(p``1) `CP10(p``2) `CP10(p``3) `CP10(p``4) `CP10(p``5) `CP10(p``6) `CP10(p``7) `CP10(p``8) `CP10(p``9)
module top;
  int calls, trace[262], sampled;
  function automatic int mark(input int id, input int value);
    trace[calls] = id;
    calls++;
    return value;
  endfunction
  covergroup cg;
    option.goal = mark(2000, 100);
    option.weight = mark(2001, 1);
    `CP100(0) `CP100(1) `CP10(20) `CP10(21) `CP10(22) `CP10(23) `CP10(24) `CP10(25)
  endgroup
  cg cov;
  initial begin
    bit ordered;
    cov = new;
    ordered = calls == 262 && trace[260] == 2000 && trace[261] == 2001;
    for (int i = 0; i < 260; ++i)
      ordered &= trace[i] == i;
    $display("ordered=%0d calls=%0d", ordered, calls);
  end
endmodule
