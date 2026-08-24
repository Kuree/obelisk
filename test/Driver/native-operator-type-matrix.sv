// RUN: obelisk -fno-lto -O0 --top=top %s -o %t.o0.native
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode --top=top %s -o %t.o0.bytecode
// RUN: obelisk -fno-lto -O3 --top=top %s -o %t.o3.native
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode --top=top %s -o %t.o3.bytecode
// RUN: %t.o0.native > %t.o0.native.out
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: %t.o3.native > %t.o3.native.out
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: FileCheck %s < %t.o0.native.out

interface matrix_if;
  logic value;
endinterface

module operator_type_matrix(matrix_if intf);
  class object_t;
  endclass
  typedef struct { int value; byte tag; } record_t;

  string s0, s1;
  int da0[], da1[];
  int q0[$], q1[$];
  int aa0[int], aa1[int];
  record_t r0, r1;
  object_t o0, o1;
  chandle c0, c1;
  virtual matrix_if v0, v1;
  process p0, p1;
  event e0, e1;
  bit [65535:0] w0, w1, wr;

  initial begin
    // IEEE 1800-2017 6.16.1 and 11.4.5: string ordering and equality.
    s0 = "alpha";
    s1 = "beta";
    assert (s0 < s1);
    assert (s0 !== s1);

    // IEEE 1800-2017 7.2.1, 7.4.2, 7.5, 7.9, 7.10, and 11.4.5:
    // sequential containers, associative arrays, and unpacked aggregates use
    // elementwise logical/case equality.
    da0 = '{1, 2, 3};
    da1 = da0;
    q0 = '{4, 5, 6};
    q1 = q0;
    aa0[7] = 11;
    aa0[3] = 19;
    aa1[3] = 19;
    aa1[7] = 11;
    r0 = '{23, 8'h5a};
    r1 = r0;
    assert (da0 == da1 && da0 === da1);
    assert (q0 == q1 && q0 === q1);
    assert (aa0 == aa1 && aa0 === aa1);
    assert (r0 == r1 && r0 === r1);

    // IEEE 1800-2017 6.13, 8.2, 9.7, 15.5, 25.9, and 11.4.6: wildcard
    // equality is legal for handle values and has ordinary identity semantics
    // because handles have no x/z representation.
    o0 = new;
    o1 = o0;
    assert (o0 ==? o1);
    assert (!(o0 !=? o1));
    assert (o0 !=? null && null !=? o0);
    assert (c0 ==? c1);
    assert (!(c0 !=? c1));
    assert (c0 ==? null && null ==? c0);
    v0 = intf;
    v1 = null;
    assert (v0 !=? v1);
    assert (v0 !=? null && null !=? v0);
    p0 = process::self();
    p1 = null;
    assert (p0 !=? p1);
    assert (p0 !=? null && null !=? p0);
    e1 = e0;
    assert (e0 ==? e1);
    assert (e0 !=? null && null !=? e0);

    // IEEE 1800-2017 11.4.8: two-state XNOR is defined at every packed
    // width. This width also guards bounded compiler behavior.
    w0 = '0;
    w1 = '1;
    wr = w0 ~^ w1;
    assert (wr == '0);
    w0[32768] = 1'b1;
    w1[32768] = 1'b1;
    wr = w0 ^~ w1;
    assert (wr[32768] && !wr[32767] && !wr[32769]);
    $display("operator type matrix passed");
  end
endmodule

module top;
  matrix_if intf();
  operator_type_matrix dut(intf);
endmodule

// CHECK: operator type matrix passed
