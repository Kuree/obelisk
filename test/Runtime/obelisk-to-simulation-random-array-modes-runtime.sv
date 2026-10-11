// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=PLAN
// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.8: for unpacked arrays rand_mode() addresses individual
// elements, and an omitted index affects every element. Each fixed array
// field carries a persistent per-leaf mode field and its leaf width.
// PLAN: simulation.class.field {{.*}}debug_name = "m"
// PLAN-SAME: simulation.random_array_element_width = 4 : i32
// PLAN-SAME: simulation.random_array_mode_field = @[[M_MODES:[A-Za-z0-9_.$]+]]
// PLAN: simulation.class.field @[[M_MODES]]

// Individual leaves have persistent modes: ascending negative bounds,
// multidimensional subarray setters, more than 64 elements, static sharing
// (18.8: a static variable's mode is shared by all instances), and base-handle
// class-wide dispatch. Frozen four-state elements retain Z exactly; active
// leaves become two-state. Wide, unknown, and out-of-range indices cannot
// alias a valid mode bit. Constraints still check frozen values; an explicit
// randomize list temporarily activates its selected property without changing
// the persistent modes.
// RUNTIME: initial modes 1 1 1 1 1 1
// RUNTIME-NEXT: selected modes 1 1 1 1 1 1 1
// RUNTIME-NEXT: frozen elements 1 1
// RUNTIME-NEXT: static whole array modes 1 1
// RUNTIME-NEXT: static whole array reset 1 1
// RUNTIME-NEXT: subarray modes 1 1 1 1
// RUNTIME-NEXT: whole array reset 1 1
// RUNTIME-NEXT: class disable then select 1 1 1 1 1 1
// RUNTIME-NEXT: dynamic class reset 1 1 1 1 1 1
// RUNTIME-NEXT: invalid indices 1 1
// RUNTIME-NEXT: all elements inactive 1 1 1
// RUNTIME-NEXT: disabled constraints still check 1 1
// RUNTIME-NEXT: explicit selection 1 1 1

typedef bit [15:0] m_bits_t;

class C;
  rand bit [3:0] m [-1:0][3:2];
  rand logic [3:0] p [2:1];
  rand bit w [64:0];
  static rand bit [3:0] a [1:0];
  rand bit [2:0] odd [2:0];
  constraint constraints {
    foreach (m[i, j]) m[i][j] <= 7;
    foreach (a[k]) a[k] <= 7;
  }
endclass

class D extends C;
  rand bit q [1:0];
endclass

module top;
  D object = new;
  D other = new;
  C base = object;
  int status;
  string before_state, after_state;
  bit valid = 1;
  int changed = 0;
  bit [128:0] wide_index = 129'h1_0000_0000_0000_0000;
  initial begin
    $display("initial modes %0d %0d %0d %0d %0d %0d",
             object.m[-1][3].rand_mode() == 1, object.p[2].rand_mode() == 1,
             object.w[64].rand_mode() == 1, object.a[1].rand_mode() == 1,
             object.odd[1].rand_mode() == 1, object.q[1].rand_mode() == 1);
    object.m = '{'{3, 3}, '{3, 3}};
    object.p = '{4'hz, 4'hx};
    object.w = '{default: 0};
    object.w[64] = 1;
    object.a = '{4, 4};
    object.odd = '{5, 5, 5};
    object.odd[1].rand_mode(0);
    object.m[-1][3].rand_mode(0);
    object.p[2].rand_mode(0);
    object.w[64].rand_mode(0);
    object.a[1].rand_mode(0);
    $display("selected modes %0d %0d %0d %0d %0d %0d %0d",
             object.m[-1][3].rand_mode() == 0, object.m[-1][2].rand_mode() == 1,
             other.m[-1][3].rand_mode() == 1, object.p[2].rand_mode() == 0,
             object.w[64].rand_mode() == 0, other.a[1].rand_mode() == 0,
             object.odd[1].rand_mode() == 0);
    repeat (16) begin
      status = object.randomize();
      valid = valid && status == 1 && object.m[-1][3] == 3 &&
              object.p[2] === 4'hz && object.p[1] <= 15 && object.w[64] == 1 &&
              object.a[1] == 4 && object.odd[1] == 5;
      changed += object.m[-1][2] != 3;
    end
    $display("frozen elements %0d %0d", valid, changed > 0);

    object.a.rand_mode(0);
    object.a[0].rand_mode(1);
    $display("static whole array modes %0d %0d",
             other.a[1].rand_mode() == 0, other.a[0].rand_mode() == 1);
    object.a.rand_mode(1);
    $display("static whole array reset %0d %0d",
             other.a[1].rand_mode() == 1, other.a[0].rand_mode() == 1);

    object.m.rand_mode(0);
    object.m[0].rand_mode(1);
    $display("subarray modes %0d %0d %0d %0d",
             object.m[-1][3].rand_mode() == 0, object.m[-1][2].rand_mode() == 0,
             object.m[0][3].rand_mode() == 1, object.m[0][2].rand_mode() == 1);
    object.m.rand_mode(1);
    $display("whole array reset %0d %0d", object.m[-1][3].rand_mode() == 1,
             object.m[0][2].rand_mode() == 1);

    base.rand_mode(0);
    object.m[-1][2].rand_mode(1);
    object.a[0].rand_mode(1);
    object.q[0].rand_mode(1);
    $display("class disable then select %0d %0d %0d %0d %0d %0d",
             object.m[-1][3].rand_mode() == 0, object.m[-1][2].rand_mode() == 1,
             other.a[1].rand_mode() == 0, other.a[0].rand_mode() == 1,
             object.q[1].rand_mode() == 0, object.q[0].rand_mode() == 1);
    base.rand_mode(1);
    $display("dynamic class reset %0d %0d %0d %0d %0d %0d",
             object.m[-1][3].rand_mode() == 1, object.p[2].rand_mode() == 1,
             object.w[64].rand_mode() == 1, other.a[1].rand_mode() == 1,
             object.odd[1].rand_mode() == 1, object.q[1].rand_mode() == 1);

    object.m[wide_index][3].rand_mode(0);
    object.m[32'bx][3].rand_mode(0);
    object.m[-2][3].rand_mode(0);
    $display("invalid indices %0d %0d", object.m[-1][3].rand_mode() == 1,
             object.m[wide_index][3].rand_mode() == 0);

    object.rand_mode(0);
    object.m.rand_mode(1);
    object.m[-1][3].rand_mode(0);
    object.m[-1][2].rand_mode(0);
    object.m[0][3].rand_mode(0);
    object.m[0][2].rand_mode(0);
    object.m = '{'{3, 3}, '{3, 3}};
    before_state = object.get_randstate();
    status = object.randomize();
    after_state = object.get_randstate();
    $display("all elements inactive %0d %0d %0d", status == 1,
             before_state == after_state, m_bits_t'(object.m) == 16'h3333);

    object.m = '{'{15, 15}, '{15, 15}};
    status = object.randomize();
    $display("disabled constraints still check %0d %0d", status == 0,
             m_bits_t'(object.m) == 16'hFFFF);

    status = base.randomize(m);
    $display("explicit selection %0d %0d %0d", status == 1,
             object.m[-1][3].rand_mode() == 0, object.m[-1][3] <= 7);
  end
endmodule
