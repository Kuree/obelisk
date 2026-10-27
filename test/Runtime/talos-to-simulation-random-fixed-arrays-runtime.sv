// RUN: %talos --std=1800-2023 -emit-talos %s | talos-opt '--lower-talos-to-sim=opt-level=0' | FileCheck %s --check-prefix=PLAN
// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: z3

// IEEE 1800-2023 18.4 and 18.5.7.1: fixed unpacked integral arrays (class
// properties and std::randomize arguments) are random variables whose elements
// form one exact bitstream. A selected element names a solve-before operand
// (18.5.9). Foreach over their dimensions expands in declaration order with
// the declared iterator type, so every solve site (two for the class, one for
// the scope call) lowers to a Z3-proven structural sampler (SAMP section).
// PLAN-COUNT-3: simulation.random.solve {{.*}}SAMP

// Unpacked matrices with descending, ascending and negative bounds, a skipped
// middle dimension, fixed unpacked state captures, soft priority (the last
// iteration wins), and an omitted-iterator loop (empty conjunction). Bitstream
// casts check element order in the committed arrays.
// RUNTIME: static foreach 1 1 1 1 1 1
// RUNTIME-NEXT: scope foreach 1 1

typedef bit [15:0] w16_t;
typedef bit [7:0] w8_t;

class C;
  rand bit [3:0] descending [1:0][2:1];
  rand bit [3:0] ascending [-1:0][3:2];
  rand bit cube [1:0][1:0][1:0];
  rand bit [3:0] pick, nonzero;
  bit [3:0] limits [3:-1];
  constraint constraints {
    foreach (descending[id, jd]) descending[id][jd] == id + jd;
    foreach (ascending[ia, ja]) ascending[ia][ja] == ia + ja;
    foreach (cube[ic, , kc]) cube[ic][0][kc] == (ic ^ kc);
    foreach (descending[pr]) soft pick == pr;
    foreach (limits[fixed]) nonzero != limits[fixed];
    solve nonzero before cube[0][0][0];
    foreach (cube[,,]) 1'b0;
  }
endclass

module top;
  C object = new;
  int status, scope_status;
  bit [3:0] packed_value [1:0];
  initial begin
    status = object.randomize();
    $display("static foreach %0d %0d %0d %0d %0d %0d", status,
             w16_t'(object.descending) == 16'h3221,
             w16_t'(object.ascending) == 16'h2132,
             (w8_t'(object.cube) & 8'h33) == 8'h12, object.pick == 0,
             object.nonzero > 0);
    scope_status = std::randomize(packed_value) with {
      foreach (packed_value[si]) packed_value[si] == si;
    };
    $display("scope foreach %0d %0d", scope_status, w8_t'(packed_value) == 8'h10);
  end
endmodule
