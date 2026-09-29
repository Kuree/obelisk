// RUN: %target_clang -fPIC -c \
// RUN:   %S/Inputs/dpi_open_array_composite_impl.c -I%resource_dir/include -o %t.o
// RUN: %obelisk --target=native -o %t.native %s %t.o
// RUN: %t.native | FileCheck %s
// RUN: %obelisk --execution-tier=bytecode -o %t.bytecode %s %t.o
// RUN: %t.bytecode | FileCheck %s
// RUN: %target_clang -fPIC \
// RUN:   -flto=full -funified-lto -c %S/Inputs/dpi_open_array_composite_impl.c \
// RUN:   -I%resource_dir/include -o %t.bc
// RUN: %obelisk -flto -o %t.lto %s %t.bc
// RUN: %t.lto | FileCheck %s
// RUN: %obelisk --emit-dpi-header %s -o %t.h
// RUN: FileCheck %s --check-prefix=HEADER < %t.h

module dpi_open_array_composite;
  typedef struct {
    int id;
    logic [4:0] state;
    string label;
  } payload_t;

  import "DPI-C" context task inspect_composite(
      inout payload_t matrix[][],
      input payload_t dynamic_values[],
      inout string labels[],
      inout chandle tokens[]);
  import "DPI-C" context task inspect_open_packed(
      input bit [] packed_only,
      inout logic [] packed_elements[]);
  import "DPI-C" function int token_is_1234(input chandle token);

  payload_t matrix[2:1][-1:0] =
      '{'{'{11, 5'b00001, "m11"}, '{12, 5'b00010, "m12"}},
        '{'{21, 5'b00011, "m21"}, '{22, 5'b00100, "m22"}}};
  payload_t dynamic_values[];
  string labels[1:0] = '{"left", "right"};
  chandle tokens[1:0] = '{null, null};
  bit [7:0] packed_only = 8'ha5;
  logic [5:0] packed_elements[1:0] = '{6'b10xz01, 6'b001100};

  initial begin
    dynamic_values = new[2];
    dynamic_values[0] = '{31, 5'b00101, "d0"};
    dynamic_values[1] = '{32, 5'b00110, "d1"};
    inspect_composite(matrix, dynamic_values, labels, tokens);
    inspect_open_packed(packed_only, packed_elements);
    $display("after=%0d/%s dyn=%0d/%s labels=%s,%s token=%0d packed=%b",
             matrix[1][-1].id, matrix[1][-1].label,
             dynamic_values[1].id, dynamic_values[1].label,
             labels[1], labels[0], token_is_1234(tokens[0]),
             packed_elements[1]);
  end
endmodule

// CHECK: matrix-dims=2 ranges=2:1,-1:0 first=21/m21/3
// CHECK: dynamic=2 first=31/d0 labels=right,left tokens=0,0
// CHECK: packed-dims=0 range=7:0 raw=a5 elements=2 elem-range=5:0 value=29/c
// CHECK: after=101/c-matrix dyn=32/d1 labels=c-left,c-right token=1 packed=10xz11
// HEADER-DAG: int inspect_composite(const svOpenArrayHandle arg0, const svOpenArrayHandle arg1, const svOpenArrayHandle arg2, const svOpenArrayHandle arg3);
// HEADER-DAG: int inspect_open_packed(const svOpenArrayHandle arg0, const svOpenArrayHandle arg1);
// HEADER-DAG: int32_t token_is_1234(void * arg0);
