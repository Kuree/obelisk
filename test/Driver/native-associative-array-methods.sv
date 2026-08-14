// RUN: obelisk -fno-lto --std=1800-2023 -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk -fno-lto --std=1800-2023 -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -fno-lto --std=1800-2023 -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: FileCheck %s < %t.o0.native.out

module native_associative_array_methods;
  int a[int];
  int strings[string];
  int mapped[string];
  int empty[int];
  int q[$];
  int qi[$];
  string qs[$];

  initial begin
    int cursor;
    a[-2] = 5;
    a[4] = 3;
    a[9] = 5;
    strings["z"] = 2;
    strings["a"] = 7;
    $display("red %0d %0d %0d %0d %0d",
             a.sum(), a.product(), a.and(), a.or(), a.xor());
    $display("with %0d %0d", a.sum() with (item + item.index()),
             a.sum() with (item.index()));
    q = a.find() with (item == 5);
    qi = a.find_index() with (item == 5);
    $display("find %p %p", q, qi);
    q = a.find_first() with (item == 5);
    qi = a.find_last_index() with (item == 5);
    $display("ends %p %p", q, qi);
    qi = a.find_first_index() with (item == 5);
    q = a.find_last() with (item == 5);
    $display("other-ends %p %p", qi, q);
    $display("minmax %p %p", a.min(), a.max());
    $display("unique %p %p", a.unique(), a.unique_index());
    mapped = strings.map() with (item + item.index().len());
    qs = strings.find_index() with (item > 1);
    $display("string %p %p %p", mapped, qs, strings.unique_index());
    $write("walk");
    if (a.first(cursor)) begin
      do
        $write(" %0d", cursor);
      while (a.next(cursor));
    end
    $display("");
    $write("reverse");
    if (a.last(cursor)) begin
      do
        $write(" %0d", cursor);
      while (a.prev(cursor));
    end
    $display("");
    $display("empty %0d %0d %p %p %p",
             empty.sum(), empty.product(), empty.find() with (1),
             empty.min(), empty.unique());
  end

  // CHECK: red 13 75 1 7 3
  // CHECK-NEXT: with 24 11
  // CHECK-NEXT: find '{
  // CHECK-SAME: '{
  // CHECK-NEXT: ends '{
  // CHECK-SAME: '{
  // CHECK-NEXT: other-ends '{
  // CHECK-SAME: '{
  // CHECK-NEXT: minmax '{
  // CHECK-SAME: '{
  // CHECK-NEXT: unique '{
  // CHECK-SAME: '{
  // CHECK-NEXT: string '{"a":
  // CHECK-SAME: '{"a", "z"}
  // CHECK-NEXT: walk -2 4 9
  // CHECK-NEXT: reverse 9 4 -2
  // CHECK-NEXT: empty 0 1 '{} '{} '{
endmodule
