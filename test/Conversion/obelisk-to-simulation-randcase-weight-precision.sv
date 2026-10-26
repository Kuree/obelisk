// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 18.16: weights are unsigned, the total has the maximum
// operand precision and wraps modulo that width, and a zero total selects no
// branch and draws nothing from the process RNG. In the first randcase
// 255 + 1 wraps to zero in 8 bits, so the RNG draw sits behind total != 0.
// CHECK: %[[A:.*]] = simulation.packed.flatten {{.*}} -> i8
// CHECK: %[[B:.*]] = simulation.packed.flatten {{.*}} -> i8
// CHECK: %[[TOTAL:.*]] = arith.addi %[[A]], %[[B]] : i8
// CHECK: %[[ANY:.*]] = arith.cmpi ne, %[[TOTAL]], %{{.*}} : i8
// CHECK: cf.cond_br %[[ANY]], ^[[DRAW:bb[0-9]+]], ^[[SKIP:bb[0-9]+]]

// The zero-total path reaches the next randcase without a draw, and the weight
// function of that randcase is evaluated exactly once.
// CHECK: ^[[SKIP]]:
// CHECK-NEXT: simulation.call @{{[^(]*}}(%{{.*}}) {simulation.vpi_call_site = @{{[^}]+}}} : (!simulation.context) -> i8
// CHECK-NOT: simulation.call @{{[^(]*}}(%{{.*}}) {simulation.vpi_call_site = @{{[^}]+}}} : (!simulation.context) -> i8
// CHECK: ^[[DRAW]]:
// CHECK-NEXT: %[[RANGE:.*]] = arith.extui %[[TOTAL]] : i8 to i64
// CHECK-NEXT: %[[RAW:.*]] = simulation.random.bounded %{{.*}}, %[[RANGE]]
// CHECK-NEXT: %[[DRAW_VALUE:.*]] = arith.trunci %[[RAW]] : i64 to i8
// CHECK-NEXT: arith.cmpi ult, %[[DRAW_VALUE]], %[[A]] : i8

// A byte weight of 8'd128 is the unsigned pattern 128, not a negative count:
// the draw range is 128 and the bound -128 : i8 is compared unsigned.
// CHECK: simulation.random.bounded %{{.*}}, %{{.*}}128_i64{{.*}}
// CHECK: arith.cmpi ult, %{{.*}}, %{{.*}}-128_i8{{.*}} : i8

// A weight a + b is self-determined: it is added at 8 bits, then zero-extended
// into the 12-bit total shared with the 12-bit weight.
// CHECK: %[[SUM8:.*]] = arith.addi {{.*}} : i8
// CHECK-NEXT: %[[SUM12:.*]] = arith.extui %[[SUM8]] : i8 to i12
// CHECK: %[[TOTAL12:.*]] = arith.addi %[[SUM12]], {{.*}} : i12
// CHECK: arith.cmpi ne, %[[TOTAL12]], {{.*}} : i12
// CHECK: arith.extui %[[TOTAL12]] : i12 to i64
// CHECK: simulation.random.bounded
// CHECK: arith.cmpi ult, {{.*}}, %[[SUM12]] : i12

module randcase_weight_precision;
  int evaluations;
  int selected;
  function automatic byte weight();
    evaluations++;
    return 8'd128;
  endfunction
  initial begin
    static bit [7:0] a = 8'd255;
    static bit [7:0] b = 1;
    static bit [11:0] larger = 12'd2048;
    selected = 0;
    randcase
      a: selected = 1;
      b: selected = 2;
    endcase
    randcase
      weight(): selected = 3;
      8'd0: selected = 4;
    endcase
    randcase
      a + b: selected = 5;
      larger: selected = 6;
    endcase
  end
endmodule
