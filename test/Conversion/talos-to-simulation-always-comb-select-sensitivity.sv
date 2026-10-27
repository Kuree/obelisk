// RUN: talos -emit-slang %s | talos-opt --convert-slang-to-talos -o %t.talos.mlir
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 9.2.2.2.1(b) excludes from an always_comb's implicit
// sensitivity any expression the block also writes, compared by longest static
// prefix (11.5.3). Writing one bit of a vector excludes that bit, not the
// vector: each block below still watches the bits it reads but never writes.
module always_comb_select_sensitivity;
  logic [3:0] w;
  logic [3:0] v;
  logic a, b, y;
  logic t, z;
  // Reads w[0], writes w[1].
  always_comb w[1] = !w[0];
  // Reads w[3] and b, writes w[2].
  always_comb w[2] = w[3] & b;
  // Reads all of v, writes v[0]: watches v[3:1].
  always_comb begin
    v[0] = a;
    y = ^v;
  end
  // Writes all of t before reading it: t is excluded.
  always_comb begin
    t = a;
    z = t;
  end
endmodule

// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: %[[W0:[0-9]+]] = simulation.ref.extract %arg1 from 0 : {{.*}} -> !simulation.ref<!simulation.logic<1>>
// CHECK-NEXT: simulation.suspend.change %[[W0]] to

// CHECK-LABEL: simulation.func private @unit_1(
// CHECK: %[[W3:[0-9]+]] = simulation.ref.extract %arg1 from 3 : {{.*}} -> !simulation.ref<!simulation.logic<1>>
// CHECK-NEXT: simulation.suspend.any %arg2, %[[W3]] edges [0, 0] to

// CHECK-LABEL: simulation.func private @unit_2(
// CHECK: %[[HIGH:[0-9]+]] = simulation.ref.extract %arg1 from 1 : {{.*}} -> !simulation.ref<!simulation.logic<3>>
// CHECK-NEXT: simulation.suspend.any %arg2, %[[HIGH]] edges [0, 0] to

// CHECK-LABEL: simulation.func private @unit_3(
// CHECK-NOT: simulation.ref.extract
// CHECK: simulation.suspend.change %arg1 to
