// REQUIRES: compiled-sampler
// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s

// IEEE 1800-2023 18.4: only members of a rand unpacked structure that carry
// rand or randc are solved, together with the structure's constraints; the
// remaining members (strings, four-state symbols, nested state) keep their
// values. Each randc member has its own cycle, and a static structure shares
// both its cycle and its modes across instances. A packed structure member is
// one integral value, so its enum member is not restricted to enumerators.
// CHECK: qualified leaves 1 1 1 1 1 1 1 1
// CHECK-NEXT: independent member cycles 1 1
// CHECK-NEXT: wide member views 1 1
// CHECK-NEXT: shared static member cycle 1 1

// IEEE 1800-2023 18.8: rand_mode on a structure, a member, a nested
// structure or an array element changes only that scope, and a member call
// re-enables a single leaf.
// CHECK-NEXT: whole structure modes 1 1 1 1 1
// CHECK-NEXT: member reenable 1 1 1 1
// CHECK-NEXT: array and nested modes 1 1 1 1

// IEEE 1800-2023 18.6.1 and 18.11: a randomize call with inactive members
// solves only the active ones. A failed solve leaves every member unchanged,
// and an explicit variable list temporarily activates the whole structure.
// CHECK-NEXT: partial structure solve 1 1 1 1 1
// CHECK-NEXT: frozen member failure 1 1 1 1 1 1
// CHECK-NEXT: explicit structure selection 1 1 1 1 1 1 1

// IEEE 1800-2023 18.8: a class rand_mode call covers structure leaves, a
// static structure shares its modes with other instances, and rand_mode(1) on
// the object restores all of them.
// CHECK-NEXT: static member sharing 1 1
// CHECK-NEXT: static member reenable 1 1 1
// CHECK-NEXT: class disable select 1 1 1 1 1
// CHECK-NEXT: class reset 1 1 1 1 1 1
// CHECK-NEXT: reset structure solve 1 1 1 1 1 1 1 1

typedef enum bit [1:0] {E0 = 0, E3 = 3} e_t;
typedef struct {
  rand bit x;
  bit [7:0] state;
} n_t;
typedef struct packed {
  e_t e;
} p_t;
typedef struct {
  randc bit [1:0] c;
  rand bit [3:0] a;
  bit [3:0] limit;
  logic [3:0] crc;
  string text;
  rand n_t nested;
  rand logic [3:0] data[1:0];
  rand bit [128:0] wide;
  rand e_t en;
  rand p_t pk;
} h_t;

class C;
  rand h_t h;
  static rand h_t sh;
  rand bit q;
  constraint k {
    h.a == h.limit;
    h.nested.x == 1;
    h.data[1] <= 7;
    h.data[0] <= 7;
    sh.a == sh.limit;
    sh.nested.x == 1;
    sh.data[1] <= 7;
    sh.data[0] <= 7;
    h.wide == 129'h1_0000_0000_0000_0000_0000_0000_0000_0011;
    int'(h.pk) == 1;
    sh.wide == 129'h1_0000_0000_0000_0000_0000_0000_0000_0011;
    int'(sh.pk) == 1;
    q == 1;
  }
endclass

module top;
  localparam bit [128:0] WIDE = 129'h1_0000_0000_0000_0000_0000_0000_0000_0011;
  C object = new;
  C other = new;
  int status;
  bit valid = 1;
  int h_seen = 0;
  int sh_seen = 0;
  int old_c = 0;

  initial begin
    object.h.limit = 5;
    object.h.crc = 4'bz;
    object.h.text = "keep";
    object.h.nested.state = 99;
    object.sh.limit = 5;
    object.sh.crc = 4'bz;
    object.sh.text = "keep";
    object.sh.nested.state = 99;
    repeat (4) begin
      status = object.randomize();
      valid &= status == 1 && object.h.a == 5 && object.sh.a == 5 &&
               object.h.nested.x == 1 && object.sh.nested.x == 1 &&
               (object.h.en == E0 || object.h.en == E3) &&
               (object.sh.en == E0 || object.sh.en == E3);
      h_seen |= 1 << object.h.c;
      sh_seen |= 1 << object.sh.c;
    end
    $display("qualified leaves %0d %0d %0d %0d %0d %0d %0d %0d", valid,
             object.q == 1, object.h.crc === 4'bz, object.h.text == "keep",
             object.h.nested.state == 99, object.sh.crc === 4'bz,
             object.sh.text == "keep", object.sh.nested.state == 99);
    $display("independent member cycles %0d %0d", h_seen == 15,
             sh_seen == 15);
    $display("wide member views %0d %0d", object.h.wide == WIDE,
             object.sh.wide == WIDE);

    // Two instances draw from one static cycle.
    sh_seen = 0;
    status = object.randomize();
    valid &= status == 1;
    sh_seen |= 1 << object.sh.c;
    status = other.randomize();
    valid &= status == 1;
    sh_seen |= 1 << object.sh.c;
    status = object.randomize();
    valid &= status == 1;
    sh_seen |= 1 << object.sh.c;
    status = other.randomize();
    valid &= status == 1;
    sh_seen |= 1 << object.sh.c;
    $display("shared static member cycle %0d %0d", valid, sh_seen == 15);

    object.h.rand_mode(0);
    $display("whole structure modes %0d %0d %0d %0d %0d",
             object.h.a.rand_mode() == 0, object.h.c.rand_mode() == 0,
             object.h.nested.x.rand_mode() == 0,
             object.h.data[0].rand_mode() == 0, object.q.rand_mode() == 1);
    object.h.a.rand_mode(1);
    $display("member reenable %0d %0d %0d %0d", object.h.a.rand_mode() == 1,
             object.h.c.rand_mode() == 0, object.h.nested.x.rand_mode() == 0,
             object.h.data[0].rand_mode() == 0);
    object.h.data[1].rand_mode(1);
    object.h.nested.rand_mode(1);
    $display("array and nested modes %0d %0d %0d %0d",
             object.h.data[1].rand_mode() == 1,
             object.h.data[0].rand_mode() == 0,
             object.h.nested.x.rand_mode() == 1,
             object.h.c.rand_mode() == 0);

    status = object.randomize();
    $display("partial structure solve %0d %0d %0d %0d %0d", status == 1,
             object.h.a == 5, object.h.crc === 4'bz,
             object.h.text == "keep", object.h.nested.state == 99);

    object.h.a.rand_mode(0);
    object.h.limit = 6;
    old_c = object.h.c;
    status = object.randomize();
    $display("frozen member failure %0d %0d %0d %0d %0d %0d", status == 0,
             object.h.a == 5, object.h.c == old_c, object.h.crc === 4'bz,
             object.h.text == "keep", object.h.nested.state == 99);

    status = object.randomize(h);
    $display("explicit structure selection %0d %0d %0d %0d %0d %0d %0d",
             status == 1, object.h.a == 6, object.h.a.rand_mode() == 0,
             object.h.c.rand_mode() == 0, object.h.crc === 4'bz,
             object.h.text == "keep", object.h.nested.state == 99);

    object.sh.rand_mode(0);
    $display("static member sharing %0d %0d",
             other.sh.a.rand_mode() == 0, other.sh.c.rand_mode() == 0);
    other.sh.a.rand_mode(1);
    $display("static member reenable %0d %0d %0d",
             object.sh.a.rand_mode() == 1, object.sh.c.rand_mode() == 0,
             object.sh.data[0].rand_mode() == 0);

    object.rand_mode(0);
    object.h.nested.x.rand_mode(1);
    $display("class disable select %0d %0d %0d %0d %0d",
             object.h.nested.x.rand_mode() == 1,
             object.h.a.rand_mode() == 0, object.h.c.rand_mode() == 0,
             object.q.rand_mode() == 0, other.sh.a.rand_mode() == 0);
    object.rand_mode(1);
    $display("class reset %0d %0d %0d %0d %0d %0d",
             object.h.c.rand_mode() == 1, object.h.data[0].rand_mode() == 1,
             object.h.data[1].rand_mode() == 1, other.sh.c.rand_mode() == 1,
             other.sh.data[0].rand_mode() == 1, object.q.rand_mode() == 1);

    status = object.randomize();
    $display("reset structure solve %0d %0d %0d %0d %0d %0d %0d %0d",
             status == 1, object.h.a == 6, object.h.crc === 4'bz,
             object.h.text == "keep", object.h.nested.state == 99,
             object.sh.crc === 4'bz, object.sh.text == "keep",
             object.sh.nested.state == 99);
  end
endmodule
