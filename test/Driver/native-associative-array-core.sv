// RUN: talos -O3 %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: FileCheck %s < %t.native.out
// RUN: talos -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | diff -u %t.native.out -
// RUN: talos --std=1800-2017 -O3 %s -o %t.2017
// RUN: %t.2017 | diff -u %t.native.out -

module native_associative_array_core;
  typedef logic [3:0] map_t[int];
  typedef logic [3:0] logic_key_map_t[logic [31:0]];
  map_t a, b;
  map_t empty_a, empty_b;
  logic_key_map_t empty_logic;
  int text[string];

  function automatic map_t changed(input map_t source);
    source[8] = 4'h8;
    return source;
  endfunction

  function automatic int change_element(ref logic [3:0] target);
    target = 4'h6;
    return target;
  endfunction

  task automatic change_ref(ref map_t target);
    target[-4] = 4'hc;
  endtask

  initial begin
    int key;
    int empty_visits;
    bit empty_first;
    map_t defaults_a, defaults_b;
    empty_visits = 0;
    foreach (empty_a[key])
      empty_visits++;
    empty_first = empty_a.first(key);
    $display("empty eq=%b case=%0d low=%h high=%h visits=%0d first=%0d",
             empty_a == empty_b, empty_a === empty_b, $low(empty_logic),
             $high(empty_logic), empty_visits, empty_first);
    a = '{3: 4'b1x01, -1: 4'h3, default: 4'h7};
    b = a;
    b[3] = 4'h9;
    text["z"] = 2;
    text["a"] = 1;
    $display("copy a=%p b=%p missing=%h exists=%0d size=%0d",
             a, b, a[99], a.exists(99), a.num());
    b = changed(a);
    change_ref(b);
    $display("calls a=%p b=%p element=%0d", a, b, change_element(b[8]));
    $write("order");
    foreach (b[key])
      $write(" %0d:%h", key, b[key]);
    $display("");
    $display("keys low=%0d high=%0d left=%0d right=%0d inc=%0d text=%p",
             $low(b), $high(b), $left(b), $right(b), $increment(b), text);
    $display("eq %b %b %b %b", a == a, a != b, a === a, a !== b);
    defaults_a = '{default: 4'h1};
    defaults_b = '{default: 4'h2};
    $display("default-eq=%0d reads=%0h/%0h",
             defaults_a === defaults_b, defaults_a[10], defaults_b[10]);
    b.delete(3);
    $display("key-delete=%p missing=%h", b, b[123]);
    b.delete();
    $display("whole-delete=%p missing=%h", b, b[123]);
    b <= a;
    #1 $display("nba=%p caseeq=%0d", b, b === a);
  end

  // CHECK: empty eq=1 case=1 low=xxxxxxxx high=xxxxxxxx visits=0 first=0
  // CHECK-NEXT: copy a='{-1:
  // CHECK-SAME: 3:
  // CHECK-SAME: missing=7
  // CHECK-SAME: exists=0 size=2
  // CHECK-NEXT: calls a='{-1:
  // CHECK-SAME: b='{-4:
  // CHECK-SAME: element=6
  // CHECK-NEXT: order -4:c -1:3 3:X 8:6
  // CHECK-NEXT: keys low=-4 high=8 left=0 right=-1 inc=-1 text='{"a":
  // CHECK-NEXT: eq x 1 1 1
  // CHECK-NEXT: default-eq=1 reads=1/2
  // CHECK-NEXT: key-delete='{-4:
  // CHECK-SAME: missing=7
  // IEEE 1800-2023 7.9.2, 7.9.11: delete() removes the elements, not the
  // default.
  // CHECK-NEXT: whole-delete='{} missing=7
  // CHECK-NEXT: nba='{-1:
  // CHECK-SAME: caseeq=1
  // SLANG: slang.expression.structured_assignment_pattern
  // SLANG-SAME: has_default_setter = true
  // SLANG-SAME: index_setter_count = 2
  // SLANG-SAME: member_setter_count = 0
  // SLANG-SAME: type_setter_count = 0
  // TALOS: talos.sv.expression.structured_assignment_pattern
  // TALOS-SAME: has_default_setter = true
  // TALOS-SAME: index_setter_count = 2
  // TALOS-SAME: member_setter_count = 0
  // TALOS-SAME: type_setter_count = 0
endmodule
