// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=PLAN
// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: z3

// IEEE 1800-2023 18.5.7.1 and 18.5.13.1 (soft priorities): fixed foreach dimensions expand in
// declaration order with the declared iterator type, before Z3 planning. The
// class randomize (two solve sites) and the std::randomize scope call each
// lower to one Z3-proven structural sampler (SAMP section), not a runtime loop.
// PLAN-COUNT-3: simulation.random.solve {{.*}}SAMP

// Packed matrices, ascending and negative bounds, a skipped middle dimension,
// fixed unpacked state captures, solve-before, later-iteration soft priority
// (the last iteration wins), and an omitted-iterator loop (empty conjunction).
// RUNTIME: static foreach 1 1 1 1 1 1
// RUNTIME-NEXT: scope foreach 1 1

class C;
  rand bit [1:0][2:1][3:0] descending;
  rand bit [-1:0][3:2][3:0] ascending;
  rand bit [1:0][1:0][1:0] cube;
  rand bit [3:0] pick, nonzero;
  bit [3:0] limits [3:-1];
  constraint constraints {
    foreach (descending[id, jd]) descending[id][jd] == id + jd;
    foreach (ascending[ia, ja]) ascending[ia][ja] == ia + ja;
    foreach (cube[ic, , kc]) cube[ic][0][kc] == (ic ^ kc);
    foreach (descending[pr]) soft pick == pr;
    foreach (limits[fixed]) nonzero != limits[fixed];
    solve nonzero before cube;
    foreach (cube[,,]) 1'b0;
  }
endclass

module top;
  C object = new;
  int status, scope_status;
  bit [1:0][3:0] packed_value;
  initial begin
    status = object.randomize();
    $display("static foreach %0d %0d %0d %0d %0d %0d", status,
             object.descending == 16'h3221, object.ascending == 16'h2132,
             (object.cube & 8'h33) == 8'h12, object.pick == 0,
             object.nonzero > 0);
    scope_status = std::randomize(packed_value) with {
      foreach (packed_value[si]) packed_value[si] == si;
    };
    $display("scope foreach %0d %0d", scope_status, packed_value == 8'h10);
  end
endmodule
