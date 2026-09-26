// RUN: %llvm_dist/bin/clang --target=x86_64-unknown-linux-gnu -fPIC -c \
// RUN:   %S/Inputs/dpi_disable_return_invalid_impl.c \
// RUN:   -I%resource_dir/include -o %t.o
// RUN: obelisk %s %t.o -o %t.native
// RUN: /bin/sh -c '"%t.native" > "%t.native.out"; test $? -eq 19'
// RUN: test ! -s %t.native.out
// RUN: obelisk --execution-tier=bytecode %s %t.o -o %t.bytecode
// RUN: /bin/sh -c '"%t.bytecode" > "%t.bytecode.out"; test $? -eq 19'
// RUN: test ! -s %t.bytecode.out

module dpi_disable_return_invalid;
  import "DPI-C" context task drive_invalid_disable(output int result);

  task automatic invalid_disable(output int result);
    #10 result = 42;
  endtask
  export "DPI-C" c_invalid_disable = task invalid_disable;

  initial begin : caller
    int result;
    drive_invalid_disable(result);
    $display("continued %0d", result);
  end

  initial begin
    #1 disable dpi_disable_return_invalid.caller;
  end
endmodule
