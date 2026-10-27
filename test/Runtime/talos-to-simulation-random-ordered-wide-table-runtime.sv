// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler
// RUN: %if z3 %{ %talos --std=1800-2023 -emit-talos %s | talos-opt '--lower-talos-to-sim=opt-level=0' | FileCheck %s --check-prefix=TABLE %}

// IEEE 1800-2023 18.5.9: a 135-bit sparse relation has three legal rows.
// Complete compiler Z3 tables must project APInt keys across word boundaries.
// Selecting the first field uniformly gives its high bit probability 1/2.
// Verify that compiler enumeration materializes the wide table's conditional
// branches, independently of the compiled residual sampling path checked at runtime.
// TABLE: %[[DRAW:.*]] = arith.trunci {{.*}} : i135 to i64
// TABLE: %[[INDEX:.*]] = arith.andi %[[DRAW]], {{.*}} : i64
// TABLE: %[[CHOICE:.*]] = arith.cmpi eq, %[[INDEX]], {{.*}} : i64
// TABLE: %[[HIGH:.*]] = arith.constant {{.*}} 18446744073709551616 : i135
// TABLE-NEXT: cf.cond_br %[[CHOICE]], ^{{.*}}, ^{{.*}}({{.*}}, %[[HIGH]] : i64, i135)
// RUNTIME: wide table valid 1
// RUNTIME-NEXT: wide table ordered 1

class C;
  rand bit [64:0] a;
  rand bit [69:0] b;
  constraint rules {
    a == 0 || a == 65'h1_0000_0000_0000_0000;
    b == 0 || b == 70'h20_0000_0000_0000_0000;
    a != 0 -> b == 0;
    solve a before b;
  }
endclass

module top;
  C object = new;
  bit valid = 1;
  bit [31:0] ones = 0;
  initial begin
    repeat (192) begin
      valid = valid && object.randomize() == 1;
      valid = valid && ((object.a != 0) -> (object.b == 0));
      ones += 32'(object.a != 0);
    end
    $display("wide table valid %0d", valid);
    $display("wide table ordered %0d", ones > 48 && ones < 144);
  end
endmodule
