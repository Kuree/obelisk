// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s

// IEEE 1800-2023 18.13.1: an explicit seed determines the same sequence in
// every thread. Four static threads start with different inherited PCG
// selectors (0..3 prior draws). Seeding through $urandom(seed),
// process::self().srandom, and srandom on a saved process handle must agree
// across threads, for positive and negative seeds, and re-seeding must
// restart the sequence.
module seeds;
  int a_0, a_1, a_r, b_0, b_1, b_r, c_0, c_1, c_r, d_0, d_1, d_r;
  int an_0, an_1, an_r, bn_0, bn_1, bn_r, cn_0, cn_1, cn_r, dn_0, dn_1, dn_r;

  initial begin
    a_0 = $urandom(123); a_1 = $urandom(); a_r = $urandom(123);
    an_0 = $urandom(-123); an_1 = $urandom(); an_r = $urandom(-123);
  end
  initial begin
    void'($urandom()); void'($urandom()); void'($urandom());
    b_0 = $urandom(123); b_1 = $urandom(); b_r = $urandom(123);
    bn_0 = $urandom(-123); bn_1 = $urandom(); bn_r = $urandom(-123);
  end
  initial begin
    void'($urandom());
    process::self().srandom(123);
    c_0 = $urandom(); c_1 = $urandom();
    process::self().srandom(123); c_r = $urandom();
    process::self().srandom(-123);
    cn_0 = $urandom(); cn_1 = $urandom();
    process::self().srandom(-123); cn_r = $urandom();
  end
  initial begin
    process p;
    p = process::self();
    void'($urandom()); void'($urandom());
    p.srandom(123);
    d_0 = $urandom(); d_1 = $urandom();
    p.srandom(123); d_r = $urandom();
    p.srandom(-123);
    dn_0 = $urandom(); dn_1 = $urandom();
    p.srandom(-123); dn_r = $urandom();
  end

  final begin
    $display("positive 0 %0d %0d %0d", a_0 == b_0, a_0 == c_0, a_0 == d_0);
    $display("positive 1 %0d %0d %0d %0d", a_1 == b_1, a_1 == c_1, a_1 == d_1, a_0 != a_1);
    $display("positive restart %0d %0d %0d", a_r == b_r, a_r == c_r, a_r == d_r);
    $display("positive repeat %0d %0d %0d %0d", a_0 == a_r, b_0 == b_r, c_0 == c_r, d_0 == d_r);
    $display("negative 0 %0d %0d %0d", an_0 == bn_0, an_0 == cn_0, an_0 == dn_0);
    $display("negative 1 %0d %0d %0d %0d", an_1 == bn_1, an_1 == cn_1, an_1 == dn_1, an_0 != an_1);
    $display("negative restart %0d %0d %0d", an_r == bn_r, an_r == cn_r, an_r == dn_r);
    $display("negative repeat %0d %0d %0d %0d", an_0 == an_r, bn_0 == bn_r, cn_0 == cn_r, dn_0 == dn_r);
    $display("sign %0d", a_0 != an_0);
  end
endmodule

// CHECK: positive 0 1 1 1
// CHECK-NEXT: positive 1 1 1 1 1
// CHECK-NEXT: positive restart 1 1 1
// CHECK-NEXT: positive repeat 1 1 1 1
// CHECK-NEXT: negative 0 1 1 1
// CHECK-NEXT: negative 1 1 1 1 1
// CHECK-NEXT: negative restart 1 1 1
// CHECK-NEXT: negative repeat 1 1 1 1
// CHECK-NEXT: sign 1
