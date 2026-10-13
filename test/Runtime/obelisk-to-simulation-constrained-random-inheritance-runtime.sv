// RUN: obelisk -emit-slang %s | obelisk-opt --convert-slang-to-obelisk '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.obelisk.mlir
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=0' '--encode-obelisk-sim-to-bytecode=vpi=off' --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --seed=1 | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode --seed=1 | FileCheck %s

// IEEE 1800-2017 18.5.2 makes differently named derived constraints
// additional to inherited constraints. Section 18.6.3 permits randomize() to
// fail only when the active constraints are infeasible. Fresh domain samples
// must therefore find x > 0 && x < y instead of keeping x and y correlated.
// CHECK: inheritance 1 1

class B;
  rand int x;
  constraint positive { x > 0; }
endclass
class C extends B;
  rand int y;
endclass
class D extends C;
  constraint ordered { x < y; }
endclass
module top;
  initial begin
    D d = new;
    int ok = 1;
    int valid = 1;
    repeat (128) begin
      ok &= d.randomize();
      valid &= d.x > 0 && d.x < d.y;
    end
    $display("inheritance %0d %0d", ok, valid);
  end
endmodule
