// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: FileCheck %s < %t.o0.native.out

module native_dynamic_array_core;
  typedef int int_array[];
  int a[], b[], c[], d[];
  int q[$];
  int nested[][];

  function automatic int_array changed(input int_array source);
    source[0] = source[0] + 10;
    return source;
  endfunction

  function automatic int change_element(ref int target);
    target = 88;
    return target;
  endfunction

  task automatic change_inout(inout int_array target);
    target[1] = 42;
  endtask

  task automatic change_ref(ref int_array target);
    target[0] = 77;
  endtask

  initial begin
    int element_result;
    a = '{3, 1};
    b = a;
    b[0] = 9;
    $display("copy a=%p b=%p", a, b);
    c = new[4](a);
    $display("new c=%p size=%0d right=%0d", c, $size(c), $right(c));
    d = changed(a);
    change_inout(d);
    change_ref(d);
    element_result = change_element(d[1]);
    $display("element_ref=%0d", element_result);
    d[-1] = 99;
    d[99] = 99;
    $display("calls a=%p d=%p bad=%0d/%0d", a, d, d[-1], d[99]);
    q = d.find(x) with (x > 40);
    $display("find=%p unique=%p", q, d.unique());
    nested = new[2];
    nested[0] = '{10, 11};
    nested[1] = '{20, 21, 22};
    foreach (nested[i, j]) begin
      if (nested[i][j] == 21)
        continue;
      $write(" %0d:%0d=%0d", i, j, nested[i][j]);
    end
    $display("");
    b <= a;
    #1 $display("nba b=%p eq=%0d", b, b == a);
  end

  // CHECK: copy a='{
  // CHECK-SAME: b='{
  // CHECK-NEXT: new c='{
  // CHECK-SAME: size=4 right=3
  // CHECK-NEXT: element_ref=88
  // CHECK-NEXT: calls a='{
  // CHECK-SAME: bad=0/0
  // CHECK-NEXT: find='{
  // CHECK-SAME: unique='{
  // CHECK-NEXT:  0:0=10 0:1=11 1:0=20 1:2=22
  // CHECK-NEXT: nba b='{
  // CHECK-SAME: eq=1
endmodule
