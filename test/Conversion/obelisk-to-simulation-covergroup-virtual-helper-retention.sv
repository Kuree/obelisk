// RUN: obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: obelisk-opt %t.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | obelisk-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: obelisk-opt %t.mlir --encode-obelisk-sim-to-bytecode='vpi=off' -o /dev/null

// IEEE 1800-2023 8.20 and 19.4: an embedded coverpoint's implicit
// this.read_current() dispatches virtually on the owning object. The sample
// helper is materialized from the covergroup template, outside the design
// body, yet early DCE must keep every override it can reach.
// CHECK: simulation.class.virtual_call %{{[0-9]+}}[@[[SLOT:__obelisk_class_[A-Za-z0-9_]*Box_method_0]]] slot 0
// CHECK: simulation.covergroup.sample
// CHECK-DAG: simulation.class.method @[[SLOT]] of @{{.*}}Box slot 0 {{.*}}implemented_by @{{unit_[0-9]+}}
// CHECK-DAG: simulation.class.method @{{.*}}Derived_method_0 of @{{.*}}Derived slot 0 {{.*}}implemented_by @{{unit_[0-9]+}}

class Box;
  int backing = 1;
  virtual function int read_current();
    return backing;
  endfunction
  covergroup cg;
    cp: coverpoint read_current() { bins values[] = {1, 2}; }
  endgroup
  function new();
    cg = new;
  endfunction
endclass
class Derived extends Box;
  function int read_current();
    return backing + 1;
  endfunction
endclass
module top;
  Box base_view, base_object;
  Derived derived;
  initial begin
    base_object = new;
    derived = new;
    base_view = base_object;
    base_view.cg.sample();
    base_view = derived;
    base_view.cg.sample();
  end
endmodule
