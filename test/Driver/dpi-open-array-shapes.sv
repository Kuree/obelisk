// RUN: %llvm_dist/bin/clang --target=x86_64-unknown-linux-gnu -fPIC -c \
// RUN:   %S/Inputs/dpi_open_array_shapes_impl.c -I%resource_dir/include -o %t.o
// RUN: %obelisk --target=native -o %t.native %s %t.o
// RUN: %t.native | FileCheck %s
// RUN: %obelisk --execution-tier=bytecode -o %t.bytecode %s %t.o
// RUN: %t.bytecode | FileCheck %s

module dpi_open_array_shapes;
  typedef struct {
    int id;
    logic state;
    string label;
  } payload_t;

  import "DPI-C" context task inspect_shapes(
      inout int nested[][],
      input int mixed[][0:1],
      input logic logic_nested[][],
      input payload_t payloads[][0:1],
      inout string nested_strings[][],
      inout chandle nested_tokens[][]);
  import "DPI-C" context task inspect_sized(inout bit [] values[0:2]);
  import "DPI-C" function int token_is_5678(input chandle token);

  int nested[][];
  int mixed[][2:1];
  logic logic_nested[][];
  payload_t payloads[][1:0];
  string nested_strings[][];
  chandle nested_tokens[][];
  bit [7:0] sized_values[3:1] = '{8'h31, 8'h22, 8'h13};

  initial begin
    nested = new[2];
    nested[0] = new[2];
    nested[1] = new[2];
    nested[0][0] = 1;
    nested[0][1] = 2;
    nested[1][0] = 3;
    nested[1][1] = 4;

    mixed = new[2];
    mixed[0] = '{10, 11};
    mixed[1] = '{12, 13};

    logic_nested = new[2];
    logic_nested[0] = new[2];
    logic_nested[1] = new[2];
    logic_nested[0][0] = 1'bx;
    logic_nested[0][1] = 1'bz;
    logic_nested[1][0] = 1'b1;
    logic_nested[1][1] = 1'b0;

    payloads = new[2];
    payloads[0] = '{'{100, 1'b0, "p00"}, '{101, 1'b1, "p01"}};
    payloads[1] = '{'{110, 1'bx, "p10"}, '{111, 1'bz, "p11"}};

    nested_strings = new[2];
    nested_tokens = new[2];
    foreach (nested_strings[i]) begin
      nested_strings[i] = new[2];
      nested_tokens[i] = new[2];
    end
    nested_strings[0][0] = "source-00-heap";
    nested_strings[0][1] = "source-01-heap";
    nested_strings[1][0] = "source-10-heap";
    nested_strings[1][1] = "source-11-heap";

    inspect_shapes(nested, mixed, logic_nested, payloads, nested_strings,
                   nested_tokens);
    $display("nested-after=%0d strings=%s,%s token=%0d", nested[0][1],
             nested_strings[0][0], nested_strings[1][1],
             token_is_5678(nested_tokens[1][0]));
    inspect_sized(sized_values);
    $display("sized-after=%h,%h,%h", sized_values[3], sized_values[2],
             sized_values[1]);
  end
endmodule

// CHECK: nested=2x2 data=1,2,3,4
// CHECK: mixed=2x2 ranges=0:1,0:1 data=11,10,13,12
// CHECK: logic=3,2 payload=101/p01/1
// CHECK: nested-after=42 strings=copy-00-heap,copy-11-heap token=1
// CHECK: sized-ranges=0:2 packed=7:0 data=13,22,31
// CHECK: sized-after=31,22,5a
