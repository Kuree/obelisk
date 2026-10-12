// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.4, 18.5.12 and 18.8: a state variable used as an index
// selects a fixed array element of the random problem, and the element keeps
// its own rand_mode. Out-of-range and X/Z indices are evaluation errors that
// fail randomize() atomically; only a state guard can discard them. Covers
// signed negative bounds, nested unpacked arrays with a packed tail, frozen
// X/Z elements, a 96-bit index and constraint_mode of the guarded block.

typedef bit [3:0] B4;
typedef bit [1:0] B2;

class C;
  rand logic [3:0] a [-1:1];
  rand bit [3:0] out;
  logic signed [31:0] index;
  bit guard;
  int bad = 2;
  constraint sample {
    out == B4'(a[index]);
    a[0] == 9;
    a[1] == 6;
  }
  constraint bounds {
    if (guard) a[bad] == 0;
  }
endclass

class G;
  rand bit [3:0] a [2];
  rand bit [3:0] y;
  int index;
  int bad = 2;
  constraint guards {
    a[index] == 0 || a[bad] == 0;
    y == 5;
  }
endclass

class N;
  rand logic [5:4][2:1] p [-2:-1][4:3];
  rand bit [1:0] out;
  int i, j, k;
  constraint selected {
    out == B2'(p[i][j][k]);
    p[-1][3] == 4'b110;
  }
endclass

class W;
  rand bit [3:0] a [2];
  rand bit [3:0] out;
  bit [95:0] index;
  constraint selected {
    a[0] == 3;
    a[1] == 12;
    out == a[index];
  }
endclass

module top;
  C c;
  G g;
  N n;
  W w;
  bit [3:0] saved;
  int good;

  initial begin
    c = new;
    c.a[-1] = 4'b10xz;
    c.a[-1].rand_mode(0);

    // a[-1] is a frozen element selected by the state index; the two-state
    // cast of 10xz is 8. Elements 0 and 1 are active and constrained.
    good = 0;
    for (int iteration = 0; iteration < 12; iteration++) begin
      c.index = -1;
      good += c.randomize() && c.out == 8 && c.a[-1] === 4'b10xz &&
              c.a[0] == 9 && c.a[1] == 6;
      c.index = 0;
      good += c.randomize() && c.out == 9 && c.a[-1] === 4'b10xz;
      c.index = 1;
      good += c.randomize() && c.out == 6;
    end
    $display("selected good=%0d", good);
    // CHECK: selected good=36

    // An index past the upper bound is an evaluation error: randomize()
    // fails and the committed state is unchanged.
    saved = c.out;
    c.index = 2;
    $display("high ok=%0d same=%0d keep=%0d", c.randomize(), c.out === saved,
             c.a[-1] === 4'b10xz);
    // CHECK-NEXT: high ok=0 same=1 keep=1

    c.index = 'x;
    $display("unknown ok=%0d same=%0d", c.randomize(), c.out === saved);
    // CHECK-NEXT: unknown ok=0 same=1

    // The guard is a state predicate, so bad = 2 raises an error exactly when
    // the guarded block is enabled and its guard is true.
    c.index = -1;
    c.guard = 1;
    $display("guard-on ok=%0d same=%0d", c.randomize(), c.out === saved);
    // CHECK-NEXT: guard-on ok=0 same=1

    c.bounds.constraint_mode(0);
    $display("block-off ok=%0d out=%0d", c.randomize(), c.out);
    // CHECK-NEXT: block-off ok=1 out=8

    c.bounds.constraint_mode(1);
    c.guard = 0;
    $display("guard-off ok=%0d out=%0d", c.randomize(), c.out);
    // CHECK-NEXT: guard-off ok=1 out=8

    // a[index] with index 0 inactive: TRUE || ERROR is TRUE (18.5.12), but
    // FALSE || ERROR is an error, and a random element is not a guard.
    g = new;
    g.index = 0;
    g.a[0] = 0;
    g.a[0].rand_mode(0);
    $display("true-guard ok=%0d y=%0d a0=%0d", g.randomize(), g.y, g.a[0]);
    // CHECK-NEXT: true-guard ok=1 y=5 a0=0

    g.a[0] = 1;
    $display("false-guard ok=%0d y=%0d a0=%0d", g.randomize(), g.y, g.a[0]);
    // CHECK-NEXT: false-guard ok=0 y=5 a0=1

    g.a[0].rand_mode(1);
    $display("random-guard ok=%0d y=%0d a0=%0d", g.randomize(), g.y, g.a[0]);
    // CHECK-NEXT: random-guard ok=0 y=5 a0=1

    g.bad = 1;
    good = g.randomize() && (g.a[0] == 0 || g.a[1] == 0);
    $display("valid-disjunction good=%0d", good);
    // CHECK-NEXT: valid-disjunction good=1

    // Multidimensional selection: unpacked [-2:-1][4:3] and packed [5:4].
    n = new;
    n.p.rand_mode(0);
    n.p[-2][4] = 4'b10xz;
    n.p[-2][3] = 4'bzzzz;
    n.p[-1][4] = 4'bxxxx;
    n.p[-1][3].rand_mode(1);
    n.i = -2;
    n.j = 4;
    n.k = 5;
    $display("md-high ok=%0d out=%0d p=%b,%b", n.randomize(), n.out,
             n.p[-2][4], n.p[-2][3]);
    // CHECK-NEXT: md-high ok=1 out=2 p=10xz,zzzz

    n.k = 4;
    $display("md-unknown-cast ok=%0d out=%0d p=%b", n.randomize(), n.out,
             n.p[-1][4]);
    // CHECK-NEXT: md-unknown-cast ok=1 out=0 p=xxxx

    n.i = -1;
    n.j = 3;
    n.k = 5;
    $display("md-active-high ok=%0d out=%0d p=%0d", n.randomize(), n.out,
             n.p[-1][3]);
    // CHECK-NEXT: md-active-high ok=1 out=1 p=6

    n.k = 4;
    $display("md-active-low ok=%0d out=%0d", n.randomize(), n.out);
    // CHECK-NEXT: md-active-low ok=1 out=2

    n.k = 3;
    $display("md-bounds ok=%0d out=%0d p=%0d", n.randomize(), n.out,
             n.p[-1][3]);
    // CHECK-NEXT: md-bounds ok=0 out=2 p=6

    // A 96-bit index must not be truncated to 32 bits: bit 65 set is out of
    // range even though the low bits select element 1.
    w = new;
    w.index = 0;
    $display("wide0 ok=%0d out=%0d", w.randomize(), w.out);
    // CHECK-NEXT: wide0 ok=1 out=3
    w.index = 1;
    $display("wide1 ok=%0d out=%0d", w.randomize(), w.out);
    // CHECK-NEXT: wide1 ok=1 out=12
    w.index = (96'h1 << 65) | 1;
    $display("wide-bad ok=%0d out=%0d a=%0d,%0d", w.randomize(), w.out,
             w.a[0], w.a[1]);
    // CHECK-NEXT: wide-bad ok=0 out=12 a=3,12
  end
endmodule
