// RUN: obelisk -fno-lto -O0 --std=1800-2023 %s -o %t.o0.native
// RUN: %t.o0.native --seed=1 > %t.o0.native.out
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode --std=1800-2023 %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode --seed=1 > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -fno-lto -O3 --std=1800-2023 %s -o %t.o3.native
// RUN: %t.o3.native --seed=1 > %t.o3.native.out
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode --std=1800-2023 %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode --seed=1 > %t.o3.bytecode.out
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: %t.o0.native --seed=2 > %t.seed2.out
// RUN: not /usr/bin/cmp -s %t.o0.native.out %t.seed2.out
// RUN: FileCheck %s < %t.o0.native.out

module native_dynamic_array_methods;
  int a[] = '{3, 1, 2, 1};
  int q[$];
  int mapped[$];
  int key_evaluations;

  function int key(input int value);
    key_evaluations++;
    return value;
  endfunction

  task showq(string tag, int value[$]);
    $write("%s:", tag);
    foreach (value[i])
      $write(" %0d", value[i]);
    $display("");
  endtask

  task showa(string tag);
    $write("%s:", tag);
    foreach (a[i])
      $write(" %0d", a[i]);
    $display("");
  endtask

  initial begin
    $display("red %0d %0d %0d %0d %0d %0d",
             a.sum(), a.sum() with (item * 2), a.product(),
             a.and(), a.or(), a.xor());
    q = a.find(x) with (x > 1); showq("find", q);
    q = a.find_index(x) with (x > 1); showq("findi", q);
    q = a.find_first(x) with (x > 1); showq("first", q);
    q = a.find_first_index(x) with (x > 1); showq("firsti", q);
    q = a.find_last(x) with (x > 1); showq("last", q);
    q = a.find_last_index(x) with (x > 1); showq("lasti", q);
    q = a.find_index(x) with (x.index() == 2); showq("index", q);
    q = a.min(); showq("min", q);
    q = a.max(); showq("max", q);
    q = a.min(x) with (-x); showq("minkey", q);
    q = a.max(x) with (-x); showq("maxkey", q);
    q = a.unique(); showq("unique", q);
    q = a.unique_index(); showq("uniquei", q);
    q = a.unique(x) with (x & 1); showq("uniquekey", q);
    q = a.unique_index(x) with (x & 1); showq("uniqueikey", q);
    a.sort(x) with (key(x)); showa("sort");
    $display("keyeval: %0d", key_evaluations);
    a.rsort(x) with (x); showa("rsort");
    a.reverse(); showa("reverse");
    a.shuffle(); showa("shuffle");
    mapped = a.map(x) with (x + 10); showq("map", mapped);
  end

  // CHECK: red 7 14 6 0 3 1
  // CHECK-NEXT: find: 3 2
  // CHECK-NEXT: findi: 0 2
  // CHECK-NEXT: first: 3
  // CHECK-NEXT: firsti: 0
  // CHECK-NEXT: last: 2
  // CHECK-NEXT: lasti: 2
  // CHECK-NEXT: index: 2
  // CHECK-NEXT: min: 1
  // CHECK-NEXT: max: 3
  // CHECK-NEXT: minkey: 3
  // CHECK-NEXT: maxkey: 1
  // CHECK-NEXT: unique: 3 1 2
  // CHECK-NEXT: uniquei: 0 1 2
  // CHECK-NEXT: uniquekey: 3 2
  // CHECK-NEXT: uniqueikey: 0 2
  // CHECK-NEXT: sort: 1 1 2 3
  // CHECK-NEXT: keyeval: 4
  // CHECK-NEXT: rsort: 3 2 1 1
  // CHECK-NEXT: reverse: 1 1 2 3
  // CHECK-NEXT: shuffle: 2 1 1 3
  // CHECK-NEXT: map: 12 11 11 13
endmodule
