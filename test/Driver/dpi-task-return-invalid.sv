// RUN: %target_clang -fPIC -c \
// RUN:   %S/Inputs/dpi_impl.c -I%resource_dir/include -o %t.o
// RUN: obelisk %s %t.o -o %t.native
// RUN: not --status=19 %t.native > %t.native.out
// RUN: test ! -s %t.native.out
// RUN: obelisk --execution-tier=bytecode %s %t.o -o %t.bytecode
// RUN: not --status=19 %t.bytecode > %t.bytecode.out
// RUN: test ! -s %t.bytecode.out

module dpi_task_return_invalid;
  import "DPI-C" context task dpi_fail_invalid(output int value);

  int value = 7;
  initial begin
    dpi_fail_invalid(value);
    $display("continued %0d", value);
  end
endmodule
