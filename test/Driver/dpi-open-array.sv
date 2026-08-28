// RUN: %llvm_dist/bin/clang --target=x86_64-unknown-linux-gnu -fPIC -c %S/Inputs/dpi_open_array_impl.c -I%resource_dir/include -o %t.o
// RUN: %obelisk -fno-lto --target=native -o %t.native %s %t.o
// RUN: %t.native | FileCheck %s
// RUN: %obelisk -fno-lto --execution-tier=bytecode -o %t.bytecode %s %t.o
// RUN: %t.bytecode | FileCheck %s
// RUN: %obelisk --emit-dpi-header %s -o %t.h
// RUN: FileCheck %s --check-prefix=HEADER < %t.h

module dpi_open_array;
  import "DPI-C" context task inspect_arrays(
      input int ints[],
      inout logic [4:0] logic_values[],
      output byte bytes[]);
  import "DPI-C" context task inspect_dynamic(
      inout int values[], input int empty[]);

  int ints[3:1] = '{11, 22, 33};
  logic [4:0] logic_values[2:0] = '{5'bx10z1, 5'b0, 5'b1};
  byte bytes[1:0] = '{-1, -1};
  int dynamic_values[];
  int empty_values[];

  initial begin
    inspect_arrays(ints, logic_values, bytes);
    $display("after=%b bytes=%h,%h", logic_values[2], bytes[1], bytes[0]);
    dynamic_values = new[3];
    dynamic_values[0] = 7;
    dynamic_values[1] = 8;
    dynamic_values[2] = 9;
    inspect_dynamic(dynamic_values, empty_values);
    $display("dynamic-after=%0d", dynamic_values[1]);
  end
endmodule

// CHECK: dims=1 int-range=3:1 packed=31:0/32 raw=33,22,11
// CHECK: logic=19/12
// CHECK: after=1zx10 bytes=41,42
// CHECK: dynamic=3 range=0:2 raw=7,8,9 empty=0 range=0:-1 low=0 high=-1 inc=-1 ptr=null
// CHECK: dynamic-after=42
// HEADER: int inspect_arrays(const svOpenArrayHandle arg0, const svOpenArrayHandle arg1, const svOpenArrayHandle arg2);
// HEADER: int inspect_dynamic(const svOpenArrayHandle arg0, const svOpenArrayHandle arg1);
