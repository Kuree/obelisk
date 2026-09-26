// RUN: %llvm_dist/bin/clang --target=x86_64-unknown-linux-gnu -fPIC -c \
// RUN:   %S/Inputs/dpi_open_array_ragged_impl.c -I%resource_dir/include -o %t.o
// RUN: %obelisk --target=native -o %t.native %s %t.o
// RUN: not %t.native 2>&1 | FileCheck %s
// RUN: %obelisk --execution-tier=bytecode -o %t.bytecode %s %t.o
// RUN: not %t.bytecode 2>&1 | FileCheck %s

module dpi_open_array_ragged;
  import "DPI-C" context task inspect_ragged(input int values[][]);
  int values[][];

  initial begin
    values = new[2];
    values[0] = new[1];
    values[1] = new[2];
    inspect_ragged(values);
  end
endmodule

// CHECK: simulation ended: format argument mismatch (status 8)
