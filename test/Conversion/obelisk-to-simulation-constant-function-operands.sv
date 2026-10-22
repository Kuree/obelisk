// RUN: obelisk -emit-slang %s | obelisk-opt --convert-slang-to-obelisk '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 11.5.1, 11.4.12.1 and A.6.7.1 make part-select bounds,
// replication counts and array pattern keys constant expressions, and 13.4.3 evaluates a constant
// function call in one at elaboration. Withdrawing executable folds around a
// user call must keep these operands constant, while an executable use of the
// same call still runs it.
module constant_function_operands(
    input logic [7:0] a, output logic [3:0] y, output logic [1:0] z,
    output logic [1:0] p [2], output logic [1:0] r [2], output int q);
  function int w(int n);
    return n - 8;
  endfunction
  assign y = a[w(12) - 1:0];
  assign z = {w(10) + 0{a[0]}};
  assign p = '{w(10) + 0{a[1:0]}};
  assign r = '{(w(9) + 0): a[1:0], default: a[3:2]};
  assign q = w(12) - 1;
endmodule

// CHECK-LABEL: simulation.func private @unit_1(
// CHECK: simulation.logic.extract %{{.*}} from 0 : !simulation.logic<8> -> !simulation.logic<4>
// CHECK-LABEL: simulation.func private @unit_2(
// CHECK: simulation.logic.replicate %{{.*}} times 2 : !simulation.logic<1> -> !simulation.logic<2>
// CHECK-LABEL: simulation.func private @unit_3(
// CHECK: simulation.aggregate.splat %{{.*}} -> !simulation.unpacked_array<0 : 1 x
// CHECK-LABEL: simulation.func private @unit_4(
// CHECK: %[[A:.*]] = simulation.logic.extract %{{.*}} from 0 : !simulation.logic<8> -> !simulation.logic<2>
// CHECK: %[[KEYED:.*]] = simulation.packed.unflatten %[[A]]
// CHECK: %[[B:.*]] = simulation.logic.extract %{{.*}} from 2 : !simulation.logic<8> -> !simulation.logic<2>
// CHECK: %[[DEFAULT:.*]] = simulation.packed.unflatten %[[B]]
// CHECK: simulation.aggregate.construct %[[DEFAULT]], %[[KEYED]] :
// CHECK-LABEL: simulation.func private @unit_5(
// CHECK: simulation.call @unit_0(%arg0) : (!simulation.context) -> i32
