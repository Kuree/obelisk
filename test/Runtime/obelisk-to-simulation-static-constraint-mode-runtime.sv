// RUN: obelisk -emit-slang %s | obelisk-opt --convert-slang-to-obelisk '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.obelisk.mlir
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=0' '--encode-obelisk-sim-to-bytecode=vpi=off' --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --seed=1 | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode --seed=1 | FileCheck %s

// IEEE 1800-2017 18.5.11 requires one constraint_mode() state shared by
// every instance of a static constraint. Section 18.9 requires constraints
// to start active and to be ignored while disabled. Exercise named and
// class-wide mode changes, then prove randomization observes both states.
// CHECK: static-mode 1 1 1 1 1

class C;
  rand bit [7:0] value;
  static constraint range_c { value inside {[10:15]}; }
  constraint positive_c { value > 5; }
endclass
module top;
  initial begin
    static C a = new;
    static C b = new;
    static int initial_on, named_shared, class_shared, disabled_ok, enabled_rejects;
    initial_on = a.range_c.constraint_mode() && b.range_c.constraint_mode();
    a.range_c.constraint_mode(0);
    named_shared = !a.range_c.constraint_mode() && !b.range_c.constraint_mode();
    b.range_c.constraint_mode(1);
    a.constraint_mode(0);
    class_shared = !a.range_c.constraint_mode() && !b.range_c.constraint_mode();
    disabled_ok = b.randomize() with { value == 20; };
    a.constraint_mode(1);
    enabled_rejects = !(b.randomize() with { value == 20; });
    $display("static-mode %0d %0d %0d %0d %0d", initial_on, named_shared,
             class_shared, disabled_ok, enabled_rejects);
  end
endmodule
