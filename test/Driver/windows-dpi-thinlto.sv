// REQUIRES: windows
// RUN: %split-file %S/Inputs/windows-pi.defs %t.dir
// RUN: %llvm_dist/bin/llvm-dlltool -m %dlltool_machine -D simulator.exe -d %native_support/obelisk-pi.def -l %t.dir/host.lib
// RUN: %target_clang -fuse-ld=lld -shared %S/Inputs/dpi_impl.c -I%resource_dir/include %t.dir/host.lib -Xlinker /def:%t.dir/dpi.def -o %t.dir/dpi.dll
// RUN: obelisk -O1 -flto --compile-threads=4 --mlir-timing %s %t.dir/dpi.dll -o %t.dir/simulator.exe 2> %t.log
// RUN: FileCheck %s --check-prefix=PARTITIONS < %t.log
// RUN: %t.dir/simulator.exe | FileCheck %s
// RUN: test -d %t.dir/simulator.exe.thinlto-cache
//
// Enough live coroutine bodies to select partitioned ThinLTO rather than Full LTO.
// PARTITIONS: obelisk backend timing: partition 0
// PARTITIONS: obelisk backend timing: partition 1
// CHECK: sum=656

module windows_dpi_leaf #(parameter int ID = 0)(output int value);
  import "DPI-C" pure function int dpi_add(input int value);
  initial begin
    value = 0;
    #1;
    value = dpi_add(ID);
  end
endmodule

module windows_dpi_thinlto;
  wire [31:0] values[32];
  for (genvar i = 0; i < 32; i++) begin : leaves
    windows_dpi_leaf #(i) leaf(values[i]);
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
