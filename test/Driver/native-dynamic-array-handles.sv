// RUN: obelisk -fno-lto -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: FileCheck %s < %t.o0.native.out

class dynamic_array_handle_item;
  int value;
  function new(int value);
    this.value = value;
  endfunction
endclass

module native_dynamic_array_handles;
  dynamic_array_handle_item first, second;
  dynamic_array_handle_item items[];
  dynamic_array_handle_item result[$];
  int indices[$];

  initial begin
    first = new(2);
    second = new(1);
    items = '{first, second, first, null};
    result = items.unique();
    indices = items.unique_index();
    $display("sizes=%0d/%0d same=%0d/%0d null=%0d", result.size(),
             indices.size(), result[0] == first, result[1] == second,
             result[2] == null);
    result = items.find(x) with (x == first);
    $display("find=%0d same=%0d", result.size(), result[1] == first);
    items = '{first, second, first};
    items.sort(x) with (x.value);
    $display("sort=%0d/%0d/%0d", items[0] == second, items[1] == first,
             items[2] == first);
  end

  // CHECK: sizes=3/3 same=1/1 null=1
  // CHECK-NEXT: find=2 same=1
  // CHECK-NEXT: sort=1/1/1
endmodule
