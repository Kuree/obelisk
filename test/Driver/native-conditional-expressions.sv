// RUN: obelisk -fno-lto -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out 2> %t.o0.native.err
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out 2> %t.o0.bytecode.err
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.err %t.o0.bytecode.err
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out 2> %t.o3.native.err
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out 2> %t.o3.bytecode.err
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.err %t.o3.native.err
// RUN: diff -u %t.o0.native.err %t.o3.bytecode.err
// RUN: obelisk -fno-lto --std=1800-2017 -O0 %s -o %t.2017.native
// RUN: %t.2017.native > %t.2017.native.out 2> %t.2017.native.err
// RUN: diff -u %t.o0.native.out %t.2017.native.out
// RUN: diff -u %t.o0.native.err %t.2017.native.err
// RUN: FileCheck %s < %t.o3.native.out

class conditional_item;
  int value;

  function new(int value);
    this.value = value;
  endfunction
endclass

module native_conditional_expressions;
  typedef struct packed {
    logic [3:0] left;
    logic [3:0] right;
  } pair_t;
  typedef union packed {
    logic [7:0] bits;
    logic [7:0] alias_bits;
  } packed_union_t;
  typedef enum logic [2:0] {
    enum_left = 3'b001,
    enum_right = 3'b110
  } enum_t;
  typedef union tagged {
    int IntValue;
    byte ByteValue;
  } tagged_union_t;

  int calls;
  int order;
  pair_t pair;
  event ready;
  int array_left [0:2];
  int array_right [0:2];
  int array_result [0:2];
  logic [3:0] logic_left [0:1];
  logic [3:0] logic_right [0:1];
  logic [3:0] logic_result [0:1];

  function automatic logic [3:0] packed_arm(input int tag,
                                             input logic [3:0] value);
    calls++;
    order = order * 10 + tag;
    return value;
  endfunction

  function automatic string string_arm(input int tag, input string value);
    calls++;
    order = order * 10 + tag;
    return value;
  endfunction

  initial begin
    logic predicate;
    logic [3:0] packed_result;
    logic signed [7:0] mixed_result;
    logic signed [3:0] narrow_signed;
    logic [7:0] wide_unsigned;
    string string_result;
    real real_result;
    pair_t pair_result;
    packed_union_t packed_union_result;
    enum_t enum_result;
    conditional_item object;
    conditional_item object_result;

    calls = 0;
    order = 0;
    predicate = 1'b1;
    packed_result = predicate ? packed_arm(1, 4'b0011)
                              : packed_arm(2, 4'b1100);
    $display("known-true value=%b calls=%0d order=%0d",
             packed_result, calls, order);

    calls = 0;
    order = 0;
    predicate = 1'b0;
    packed_result = predicate ? packed_arm(1, 4'b0011)
                              : packed_arm(2, 4'b1100);
    $display("known-false value=%b calls=%0d order=%0d",
             packed_result, calls, order);

    calls = 0;
    order = 0;
    predicate = 1'bx;
    packed_result = predicate ? packed_arm(1, 4'b101z)
                              : packed_arm(2, 4'b100z);
    $display("ambiguous value=%b calls=%0d order=%0d",
             packed_result, calls, order);

    packed_result = 4'bx001 ? 4'b0101 : 4'b1010;
    $display("vector-true value=%b", packed_result);

    packed_result =
        predicate ? (1'b0 ? packed_arm(8, 4'b0000) : 4'b1010) : 4'b1000;
    $display("nested value=%b", packed_result);

    narrow_signed = -1;
    wide_unsigned = 8'h81;
    mixed_result = predicate ? narrow_signed : wide_unsigned;
    $display("mixed-width value=%b", mixed_result);

    enum_result = predicate ? enum_left : enum_right;
    $display("enum value=%b", enum_result);

    pair_result = predicate ? {4'b10xz, 4'b0011}
                            : {4'b10xz, 4'b0111};
    $display("packed-struct value=%b,%b",
             pair_result.left, pair_result.right);

    packed_union_result.bits =
        predicate ? 8'b10xz0011 : 8'b10xz0111;
    $display("packed-union value=%b", packed_union_result.bits);

    calls = 0;
    order = 0;
    packed_result = 1'bx &&& packed_arm(7, 4'b0000)
                        ? packed_arm(1, 4'b0011)
                        : packed_arm(2, 4'b1100);
    $display("unknown-and-false value=%b calls=%0d order=%0d",
             packed_result, calls, order);

    calls = 0;
    order = 0;
    packed_result = 1'bx &&& packed_arm(7, 4'b0001)
                        ? packed_arm(1, 4'b0011)
                        : packed_arm(2, 4'b1100);
    $display("unknown-and-true value=%b calls=%0d order=%0d",
             packed_result, calls, order);

    calls = 0;
    order = 0;
    string_result = predicate ? string_arm(1, "same")
                              : string_arm(2, "same");
    $display("ambiguous-string value='%s' len=%0d calls=%0d order=%0d",
             string_result, string_result.len(), calls, order);

    real_result = predicate ? 1.5 : 1.5;
    $display("ambiguous-real value=%0.1f", real_result);

    object = new(7);
    object_result = predicate ? object : object;
    $display("ambiguous-class null=%0d", object_result == null);

    $display("ambiguous-event distinct=%0d",
             (predicate ? ready : ready) != ready);

    pair = {4'd1, 4'd2};
    packed_result =
        pair matches '{left: .capture, right: 4'd2} &&&
        capture == 4'd1 ? 4'b0110 : 4'b1001;
    $display("pattern-capture value=%b", packed_result);

    calls = 0;
    pair = {4'd1, 4'd3};
    packed_result =
        pair matches '{left: .*, right: 4'd2} &&&
        packed_arm(9, 4'b0001) ? 4'b0110 : 4'b1001;
    $display("pattern-short-circuit value=%b calls=%0d",
             packed_result, calls);

    array_left = '{1, 2, 3};
    array_right = '{1, 9, 3};
    array_result = predicate ? array_left : array_right;
    $display("ambiguous-array value=%0d,%0d,%0d",
             array_result[0], array_result[1], array_result[2]);

    logic_left[0] = 4'b0011;
    logic_right[0] = 4'b0011;
    logic_left[1] = 4'bzzzz;
    logic_right[1] = 4'bzzzz;
    logic_result = predicate ? logic_left : logic_right;
    $display("ambiguous-logic-array value=%b,%b",
             logic_result[0], logic_result[1]);

  end
endmodule

// CHECK: known-true value=0011 calls=1 order=1
// CHECK-NEXT: known-false value=1100 calls=1 order=2
// CHECK-NEXT: ambiguous value=10xz calls=2 order=12
// CHECK-NEXT: vector-true value=0101
// CHECK-NEXT: nested value=10x0
// CHECK-NEXT: mixed-width value=1xxxxxx1
// CHECK-NEXT: enum value=xxx
// CHECK-NEXT: packed-struct value=10xz,0x11
// CHECK-NEXT: packed-union value=10xz0x11
// CHECK-NEXT: unknown-and-false value=1100 calls=2 order=72
// CHECK-NEXT: unknown-and-true value=xxxx calls=3 order=712
// CHECK-NEXT: ambiguous-string value='' len=0 calls=2 order=12
// CHECK-NEXT: ambiguous-real value=0.0
// CHECK-NEXT: ambiguous-class null=1
// CHECK-NEXT: ambiguous-event distinct=1
// CHECK-NEXT: pattern-capture value=0110
// CHECK-NEXT: pattern-short-circuit value=1001 calls=0
// CHECK-NEXT: ambiguous-array value=1,0,3
// CHECK-NEXT: ambiguous-logic-array value=0011,xxxx
