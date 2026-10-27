// RUN: %target_clang %target_pic -c \
// RUN:   %S/Inputs/dpi_impl.c \
// RUN:   -I%resource_dir/include -o %t.o
// RUN: talos %s %t.o -o %t.native
// RUN: not --status=19 %t.native > %t.native.out
// RUN: test ! -s %t.native.out
// RUN: talos --execution-tier=bytecode %s %t.o -o %t.bytecode
// RUN: not --status=19 %t.bytecode > %t.bytecode.out
// RUN: test ! -s %t.bytecode.out

module dpi_failure;
  import "DPI-C" task dpi_fail(output int value);

  int value = 7;
  initial begin
    dpi_fail(value);
    $display("continued %0d", value);
  end
endmodule
