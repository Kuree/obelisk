// RUN: %talos --std=1800-2023 -emit-talos %s | talos-opt --lower-talos-to-sim=opt-level=0 | FileCheck %s --check-prefix=IR
// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s

// IEEE 1800-2023 18.6.1: inside a class method, randomize() without a
// receiver is the object's randomize method, the same as this.randomize(),
// not std::randomize() of no scope variables. Both methods solve the class
// constraints.
// IR: simulation.func private @{{[^(]+}}({{.*}}, %[[THIS:[^:]+]]: !simulation.class_handle<@[[CLASS:[^>]+]]>{{.*}}-> i32
// IR: simulation.class.direct_call @[[PLAN:__talos_randomize_plan_[^ ]+]] %[[THIS]]()
// IR: simulation.func private @{{[^(]+}}({{.*}}, %[[THIS2:[^:]+]]: !simulation.class_handle<@[[CLASS]]>{{.*}}-> i32
// IR: simulation.class.direct_call @[[PLAN]] %[[THIS2]]()
// IR: simulation.func private @[[PLAN]](
// IR: simulation.random.solve
// CHECK: implicit 1 5
// CHECK: this 1 5

class C;
  rand int x;
  constraint c { x == 5; }
  function int implicit_call();
    return randomize();
  endfunction
  function int this_call();
    return this.randomize();
  endfunction
endclass

module top;
  initial begin
    C c = new;
    int ok;
    ok = c.implicit_call();
    $display("implicit %0d %0d", ok, c.x);
    c.x = 0;
    ok = c.this_call();
    $display("this %0d %0d", ok, c.x);
  end
endmodule
