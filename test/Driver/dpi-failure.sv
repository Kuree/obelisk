// RUN: %llvm_dist/bin/clang --target=x86_64-unknown-linux-gnu -fPIC -c \
// RUN:   %S/Inputs/dpi_impl.c \
// RUN:   -I$(obelisk --print-resource-dir)/include -o %t.o
// RUN: obelisk --dpi-link=%t.o %s -o %t.native
// RUN: /bin/sh -c '"%t.native" > "%t.native.out"; test $? -eq 18'
// RUN: test ! -s %t.native.out
// RUN: obelisk --execution-tier=bytecode --dpi-link=%t.o %s -o %t.bytecode
// RUN: /bin/sh -c '"%t.bytecode" > "%t.bytecode.out"; test $? -eq 18'
// RUN: test ! -s %t.bytecode.out

module dpi_failure;
  import "DPI-C" task dpi_fail(output int value);

  int value = 7;
  initial begin
    dpi_fail(value);
    $display("continued %0d", value);
  end
endmodule
