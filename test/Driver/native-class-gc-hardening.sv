// RUN: obelisk -fno-lto --std=1800-2023 -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk -fno-lto --std=1800-2023 -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -fno-lto --std=1800-2023 -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: FileCheck %s < %t.o0.native.out
// RUN: obelisk --std=1800-2023 -O0 -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM

class node;
  int value;
endclass

class pressure;
  bit [262143:0] payload;
endclass

class worker;
  int value;
  logic [7:0] four_state;

  function void touch(ref int target);
    target = 11;
  endfunction

  function int update(ref int target);
    target = 19;
    return value;
  endfunction

  function logic [7:0] update_logic(ref logic [7:0] target);
    target = 8'ha5;
    return four_state;
  endfunction

  function int mutate(output int target);
    pressure garbage;
    repeat (1100)
      garbage = new;
    target = 42;
    return 0;
  endfunction
endclass

typedef struct {
  node item;
  int tag;
} holder;

class envelope;
  holder contents;
endclass

module top;
  holder retained;
  std::weak_reference #(node) retained_weak;

  function automatic holder preserve_aggregate(holder value);
    pressure garbage;
    repeat (1100)
      garbage = new;
    return value;
  endfunction

  initial begin
    automatic worker executor = new;
    automatic node object = new;
    automatic std::weak_reference #(node) weak_object = new(object);
    automatic node observed;
    automatic pressure garbage;
    automatic int ignored;
    automatic logic [7:0] logic_observed;
    automatic envelope box = new;
    automatic holder staged;
    automatic std::weak_reference #(node) box_weak;
    automatic holder suspended;
    automatic std::weak_reference #(node) suspended_weak;
    automatic node indexed[2];
    automatic int index;

    executor.touch(object.value);
    $display("void %0d", object.value);
    ignored = executor.update(executor.value);
    $display("ref %0d %0d", ignored, executor.value);
    logic_observed = executor.update_logic(executor.four_state);
    $display("logic-ref %0h %0h", logic_observed, executor.four_state);

    ignored = executor.mutate(object.value);
    observed = weak_object.get();
    if (observed == null)
      $display("managed dead");
    else
      $display("managed live %0d", observed.value);

    retained.item = new;
    retained.item.value = 23;
    retained.tag = 5;
    retained_weak = new(retained.item);
    repeat (1100)
      garbage = new;
    $display("aggregate %0d %0d %0d", retained_weak.get() != null,
             retained.item.value, retained.tag);

    staged.item = new;
    staged.item.value = 29;
    staged.tag = 4;
    box_weak = new(staged.item);
    staged = preserve_aggregate(staged);
    $display("transient-aggregate %0d %0d %0d", box_weak.get() != null,
             staged.item.value, staged.tag);

    staged.item = new;
    staged.item.value = 31;
    staged.tag = 6;
    box.contents = staged;
    box_weak = new(staged.item);
    staged.item = null;
    repeat (1100)
      garbage = new;
    $display("object-aggregate %0d %0d %0d", box_weak.get() != null,
             box.contents.item.value, box.contents.tag);

    staged.item = new;
    staged.item.value = 41;
    staged.tag = 9;
    box_weak = new(staged.item);
    box.contents <= staged;
    staged.item = null;
    repeat (1100)
      garbage = new;
    #1;
    $display("object-aggregate-nba %0d %0d %0d", box_weak.get() != null,
             box.contents.item.value, box.contents.tag);

    suspended.item = new;
    suspended.item.value = 37;
    suspended.tag = 8;
    suspended_weak = new(suspended.item);
    #1;
    repeat (1100)
      garbage = new;
    $display("automatic-aggregate %0d %0d %0d",
             suspended_weak.get() != null, suspended.item.value,
             suspended.tag);

    indexed[1] = new;
    indexed[1].value = 47;
    index = executor.value - 18;
    $display("dynamic-class-array %0d", indexed[index].value);
    index = executor.value - 16;
    observed = indexed[index];
    $display("dynamic-class-array-oob %0d", observed == null);

    object = new;
    object.value = 7;
    weak_object = new(object);
    #1;
    $display("before %0d", object.value);
    object = null;
    repeat (1100)
      garbage = new;
    $display("continued %0d", weak_object.get() == null);
  end
endmodule

// CHECK: void 11
// CHECK-NEXT: ref 19 19
// CHECK-NEXT: logic-ref a5 a5
// CHECK-NEXT: managed live 42
// CHECK-NEXT: aggregate 1 23 5
// CHECK-NEXT: transient-aggregate 1 29 4
// CHECK-NEXT: object-aggregate 1 31 6
// CHECK-NEXT: object-aggregate-nba 1 41 9
// CHECK-NEXT: automatic-aggregate 1 37 8
// CHECK-NEXT: dynamic-class-array 47
// CHECK-NEXT: dynamic-class-array-oob 1
// CHECK-NEXT: before 7
// CHECK-NEXT: continued 1

// LLVM: @__obelisk_current_context = internal thread_local global ptr null
