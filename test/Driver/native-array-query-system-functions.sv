// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out 2> %t.o0.native.err
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out 2> %t.o0.bytecode.err
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.err %t.o0.bytecode.err
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out 2> %t.o3.native.err
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out 2> %t.o3.bytecode.err
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.err %t.o3.native.err
// RUN: diff -u %t.o0.native.err %t.o3.bytecode.err
// RUN: FileCheck %s < %t.o3.native.out

module native_array_query_system_functions;
  typedef struct packed {
    logic [2:0] first;
    bit [1:0] second;
  } packed_pair_t;
  typedef enum logic [4:2] {
    enum_zero
  } ranged_enum_t;

  logic [3:1][2:4] matrix [7:5][-2:0];
  integer dimension;
  logic [1:0] narrow_unsigned_dimension;
  logic signed [1:0] narrow_signed_dimension;
  int side_effects;

  function automatic int queried_object();
    side_effects++;
    return 7;
  endfunction

  function automatic int queried_dimension();
    side_effects++;
    return 3;
  endfunction

  initial begin
    $display("dimensions=%0d,%0d,%0d,%0d,%0d,%0d,%0d",
             $dimensions(matrix), $unpacked_dimensions(matrix),
             $dimensions(logic [0:0]), $dimensions(logic),
             $dimensions(int), $dimensions(packed_pair_t),
             $unpacked_dimensions(packed_pair_t));

    $display("types=%0d,%0d,%0d,%0d,%0d,%0d",
             $left(logic [2:5]), $right(logic [2:5]),
             $low(logic [2:5]), $high(logic [2:5]),
             $increment(logic [2:5]), $size(logic [2:5]));
    $display("aggregate=%0d,%0d,%0d",
             $left(packed_pair_t), $right(packed_pair_t),
             $size(packed_pair_t));
    $display("integral-types=%0d,%0d,%0d,%0d,%0d,%0d",
             $dimensions(ranged_enum_t), $left(ranged_enum_t),
             $right(ranged_enum_t), $size(ranged_enum_t),
             $left(time), $size(time));

    dimension = 1;
    $display("dim1=%0d,%0d,%0d,%0d,%0d,%0d",
             $left(matrix, dimension), $right(matrix, dimension),
             $low(matrix, dimension), $high(matrix, dimension),
             $increment(matrix, dimension), $size(matrix, dimension));
    dimension = 2;
    $display("dim2=%0d,%0d,%0d,%0d,%0d,%0d",
             $left(matrix, dimension), $right(matrix, dimension),
             $low(matrix, dimension), $high(matrix, dimension),
             $increment(matrix, dimension), $size(matrix, dimension));
    dimension = 3;
    $display("dim3=%0d,%0d,%0d,%0d,%0d,%0d",
             $left(matrix, dimension), $right(matrix, dimension),
             $low(matrix, dimension), $high(matrix, dimension),
             $increment(matrix, dimension), $size(matrix, dimension));
    dimension = 4;
    $display("dim4=%0d,%0d,%0d,%0d,%0d,%0d",
             $left(matrix, dimension), $right(matrix, dimension),
             $low(matrix, dimension), $high(matrix, dimension),
             $increment(matrix, dimension), $size(matrix, dimension));

    dimension = 0;
    $display("invalid=%h", $left(matrix, dimension));
    dimension = 5;
    $display("invalid=%h", $right(matrix, dimension));
    dimension = 'x;
    $display("invalid=%h", $size(matrix, dimension));
    narrow_unsigned_dimension = 3;
    narrow_signed_dimension = 2'b11;
    $display("narrow=%0d,%h",
             $left(matrix, narrow_unsigned_dimension),
             $left(matrix, narrow_signed_dimension));

    side_effects = 0;
    $display("unevaluated=%0d,%0d,%0d,%0d side=%0d",
             $dimensions(queried_object()), $left(queried_object()),
             $size(matrix[queried_object()]),
             $unpacked_dimensions(matrix[queried_object()]), side_effects);
    $display("selector=%0d side=%0d",
             $left(matrix, queried_dimension()), side_effects);

    $display("string-literal=%0d,%0d,%0d,%0d,%0d,%0d,%0d",
             $dimensions("abc"), $left("abc"), $right("abc"),
             $low("abc"), $high("abc"), $increment("abc"), $size("abc"));
    $display("string-object=%0d,%0d,%0d,%0d,%0d,%0d,%0d",
             $dimensions(string'("abc")), $left(string'("abc")),
             $right(string'("abc")), $low(string'("abc")),
             $high(string'("abc")), $increment(string'("abc")),
             $size(string'("abc")));
    $display("empty-literal=%0d,%0d,%0d,%0d",
             $left(""), $right(""), $low(""), $high(""));
    $display("empty-string-object=%0d,%0d,%0d,%0d",
             $left(string'("")), $right(string'("")),
             $low(string'("")), $high(string'("")));
  end
endmodule

// CHECK: dimensions=4,2,1,0,1,1,0
// CHECK-NEXT: types=2,5,2,5,-1,4
// CHECK-NEXT: aggregate=4,0,5
// CHECK-NEXT: integral-types=1,2,0,3,63,64
// CHECK-NEXT: dim1=7,5,5,7,1,3
// CHECK-NEXT: dim2=-2,0,-2,0,-1,3
// CHECK-NEXT: dim3=3,1,1,3,1,3
// CHECK-NEXT: dim4=2,4,2,4,-1,3
// CHECK-NEXT: invalid=xxxxxxxx
// CHECK-NEXT: invalid=xxxxxxxx
// CHECK-NEXT: invalid=xxxxxxxx
// CHECK-NEXT: narrow=3,xxxxxxxx
// CHECK-NEXT: unevaluated=1,31,3,1 side=0
// CHECK-NEXT: selector=3 side=1
// String literals are packed byte arrays, not objects of the dynamic `string`
// type. Even the empty literal has the language-defined minimum byte width.
// CHECK-NEXT: string-literal=1,23,0,0,23,1,24
// CHECK-NEXT: string-object=1,0,2,0,2,-1,3
// CHECK-NEXT: empty-literal=7,0,0,7
// CHECK-NEXT: empty-string-object=0,-1,0,-1
