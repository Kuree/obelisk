// RUN: obelisk %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s < %t.native.out

module native_real_arithmetic;
  typedef struct {
    bit flag;
    real wide;
    shortreal narrow;
  } real_pair_t;

  real a;
  real b;
  shortreal s;
  longint unsigned rounding_input;
  shortreal rounded_once;
  real values[0:1];
  real_pair_t pair;
  real watched;
  real zero;
  integer change_count;

  always @(watched)
    change_count++;

  initial begin
    a = 1.5;
    b = -2.0;
    s = 0.25;
    $display("sum=%0.3f", a + b);
    $display("product=%0.3f", a * b);
    $display("quotient=%0.3f", b / a);
    $display("power=%0.3f", a ** 2.0);
    $display("short=%0.3f", s + 0.5);
    a++;
    --s;
    $display("updates=%0.3f,%0.3f", a, s);
    $display("compare=%0d,%0d,%0d", a > b, a == b, !0.0);
    values[0] = a;
    values[1] = s;
    pair.flag = 1;
    pair.wide = values[0] + values[1];
    pair.narrow = s * 2.0;
    $display("aggregate=%0d,%0.3f,%0.3f", pair.flag, pair.wide, pair.narrow);
    rounding_input = 64'd9007199791611905;
    rounded_once = rounding_input;
    $display("short-round=%.0f", rounded_once);
  end

  initial begin
    change_count = 0;
    zero = 0.0;
    #1 watched = -0.0;
    #1 watched = zero / zero;
    #1 watched = watched;
    #1 $display("real-changes=%0d", change_count);
  end
endmodule

// CHECK: sum=-0.500
// CHECK-NEXT: product=-3.000
// CHECK-NEXT: quotient=-1.333
// CHECK-NEXT: power=2.250
// CHECK-NEXT: short=0.750
// CHECK-NEXT: updates=2.500,-0.750
// CHECK-NEXT: compare=1,0,1
// CHECK-NEXT: aggregate=1,1.750,-1.500
// CHECK-NEXT: short-round=9007200328482816
// CHECK-NEXT: real-changes=2
