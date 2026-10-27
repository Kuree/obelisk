// REQUIRES: native-macho
// RUN: mkdir -p %t.dir
// RUN: %target_clang -dynamiclib -Wl,-undefined,dynamic_lookup -Wl,-install_name,@rpath/libdpi.dylib %S/Inputs/dpi_impl.c -I%resource_dir/include -o %t.dir/libdpi.dylib
// RUN: %talos -O1 -flto --compile-threads=2 --mlir-timing %s %t.dir/libdpi.dylib -o %t.dir/simulator 2> %t.log
// RUN: FileCheck %s --check-prefix=PARTITIONS < %t.log
// RUN: %t.dir/simulator | FileCheck %s
// RUN: test -d %t.dir/simulator.thinlto-cache

// PARTITIONS: talos backend timing: partition 0
// PARTITIONS: talos backend timing: partition 1
// CHECK: sum=656

module macos_dpi_leaf #(parameter int ID = 0)(output int value);
  import "DPI-C" pure function int dpi_add(input int value);
  initial begin
    value = 0;
    #1;
    value = dpi_add(ID);
  end
endmodule

module macos_dpi_thinlto;
  wire [31:0] values[32];
  for (genvar i = 0; i < 32; i++) begin : leaves
    macos_dpi_leaf #(i) leaf(values[i]);
  end
  initial begin
    int sum;
    #2;
    sum = 0;
    foreach (values[i])
      sum += values[i];
    $display("sum=%0d", sum);
  end
endmodule
