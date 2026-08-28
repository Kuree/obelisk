// RUN: %llvm_dist/bin/clang --target=x86_64-unknown-linux-gnu -fPIC -c \
// RUN:   %S/Inputs/dpi_impl.c \
// RUN:   -I%resource_dir/include -o %t.o
// RUN: not obelisk -fno-lto --dpi-link=%t.o %s -o %t.removed 2>&1 \
// RUN:   | FileCheck %s --check-prefix=REMOVED
// RUN: not obelisk -fno-lto %t.o -o %t.native-only 2>&1 \
// RUN:   | FileCheck %s --check-prefix=NATIVE-ONLY
// RUN: obelisk -fno-lto %s %t.o -o %t.native
// RUN: %t.native | FileCheck %s --check-prefix=OUTPUT
// RUN: %obelisk -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=NONCONTEXT-IR
// RUN: %obelisk -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=CONTEXT-IR
// RUN: llvm-readelf --dyn-syms %t.native \
// RUN:   | FileCheck %s --check-prefix=EXPORTS \
// RUN:     --implicit-check-not=obelisk_rt_v1_
// RUN: obelisk -fno-lto --execution-tier=bytecode %s %t.o -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s --check-prefix=OUTPUT
// RUN: %llvm_dist/bin/clang --target=x86_64-unknown-linux-gnu -fPIC \
// RUN:   -flto=full -funified-lto -c %S/Inputs/dpi_impl.c \
// RUN:   -I%resource_dir/include -o %t.bc
// RUN: obelisk %s %t.bc -o %t.bitcode
// RUN: %t.bitcode | FileCheck %s --check-prefix=OUTPUT
// RUN: %llvm_dist/bin/clang --target=x86_64-unknown-linux-gnu -fPIC \
// RUN:   -flto=full -c %S/Inputs/dpi_impl.c \
// RUN:   -I%resource_dir/include -o %t.incompatible.bc
// RUN: not obelisk %s %t.incompatible.bc \
// RUN:   -o %t.incompatible 2>&1 \
// RUN:   | FileCheck %s --check-prefix=INCOMPATIBLE
// RUN: %llvm_dist/bin/llvm-ar rcs %t.a %t.o
// RUN: obelisk -fno-lto %s %t.a -o %t.archive
// RUN: %t.archive | FileCheck %s --check-prefix=OUTPUT
// RUN: %llvm_dist/bin/clang --target=x86_64-unknown-linux-gnu -nostdlib \
// RUN:   -shared %t.o -o %t.so
// RUN: not obelisk --target=wasm32 %s %t.so -o %t.wasm 2>&1 \
// RUN:   | FileCheck %s --check-prefix=WASM-NATIVE
// RUN: obelisk -fno-lto %s %t.so -o %t.shared
// RUN: %t.shared | FileCheck %s --check-prefix=OUTPUT
// RUN: ln -f %t.so %t.hardlink.so
// RUN: obelisk -fno-lto %s %t.so %t.hardlink.so -o %t.hardlink
// RUN: %t.hardlink | FileCheck %s --check-prefix=OUTPUT
// RUN: llvm-readelf -d %t.shared \
// RUN:   | FileCheck %s --check-prefix=NO-SONAME \
// RUN:     --implicit-check-not='Shared library: [/'
// RUN: mkdir -p %t.collision/first %t.collision/second
// RUN: cp %t.so %t.collision/first/plugin.so
// RUN: cp %t.so %t.collision/second/plugin.so
// RUN: not obelisk -fno-lto %s %t.collision/first/plugin.so \
// RUN:   %t.collision/second/plugin.so -o %t.collision.exe 2>&1 \
// RUN:   | FileCheck %s --check-prefix=COLLISION
// RUN: %llvm_dist/bin/clang --target=x86_64-unknown-linux-gnu -fPIC \
// RUN:   -shared -nostdlib %S/Inputs/native_probe_failure.c \
// RUN:   -o %t.probe-failure.so
// RUN: not obelisk -fno-lto %s %t.probe-failure.so -o %t.probe-failure 2>&1 \
// RUN:   | FileCheck %s --check-prefix=PROBE-FAILURE
// RUN: %llvm_dist/bin/clang --target=x86_64-unknown-linux-gnu -nostdlib \
// RUN:   -shared %t.o -Wl,-soname,bad/name.so -o %t.bad-soname.so
// RUN: not obelisk -fno-lto %s %t.bad-soname.so -o %t.bad-soname 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BAD-SONAME
// RUN: not obelisk -fno-lto %s -o %t.missing 2>&1 \
// RUN:   | FileCheck %s --check-prefix=MISSING

module dpi_driver;
  import "DPI-C" pure dpi_add = function int add(input int value);
  import "DPI-C" function longint dpi_scalars(
      input byte byte_value, input shortint short_value,
      input int int_value, input longint long_value,
      input bit bit_value, input logic logic_value,
      output int output_value, inout int inout_value);
  import "DPI-C" context task dpi_update(input logic [64:0] source,
                                         output bit [32:0] destination);
  import "DPI-C" function int dpi_unused(input int value);
  import "DPI-C" task dpi_logic_inout(inout logic [64:0] value);
  import "DPI-C" function void dpi_void(input int value,
                                         output int doubled);
  import "DPI-C" function real dpi_reals(
      input real source_value, input shortreal scale,
      output shortreal rounded, inout realtime accumulated);
  int result;
  longint scalar_result;
  int output_value;
  int inout_value;
  logic [64:0] source;
  bit [32:0] destination;
  logic [64:0] vector_value;
  int void_output;
  real real_result;
  shortreal rounded;
  realtime accumulated;

  initial begin
    result = add(7);
    inout_value = 5;
    scalar_result = dpi_scalars(1, 2, 3, 4, 1'b1, 1'bx,
                                output_value, inout_value);
    dpi_update(source, destination);
    vector_value = {1'b1, 60'b0, 4'b0zx1};
    dpi_logic_inout(vector_value);
    dpi_void(21, void_output);
    accumulated = 4.0;
    real_result = dpi_reals(2.5, 1.5, rounded, accumulated);
    if (vector_value === {1'b0, 60'b0, 4'b1xz0})
      $display("vector-ok");
    $display("%0d %0d %0d %0d %h", result, scalar_result,
             output_value, inout_value, destination);
    $display("void=%0d", void_output);
    $display("real=%0.2f rounded=%0.2f accumulated=%0.2f",
             real_result, rounded, accumulated);
  end
endmodule

// OUTPUT: vector-ok
// OUTPUT: 12 10 40 7 100000000
// OUTPUT: void=42
// OUTPUT: real=3.75 rounded=4.00 accumulated=4.25
// NONCONTEXT-IR-COUNT-5: call i32 @obelisk_rt_v1_import_call_noncontext_guarded(
// CONTEXT-IR: call i32 @obelisk_rt_v1_import_call_guarded(
// EXPORTS-DAG: svGetScope
// EXPORTS-DAG: svGetNameFromScope
// NO-SONAME: Library runpath: [/
// NO-SONAME: Shared library: [dpi.sv.tmp.so]
// COLLISION: duplicate runtime loader identity 'plugin.so'
// PROBE-FAILURE: could not load shared library
// PROBE-FAILURE-SAME: undefined symbol: obelisk_missing_probe_symbol
// BAD-SONAME: has a DT_SONAME containing '/'
// REMOVED: unknown argument '--dpi-link=
// NATIVE-ONLY: at least one SystemVerilog input or command file is required
// WASM-NATIVE: native input '{{.*}}.so' is only valid when linking a final executable
// INCOMPATIBLE: ld.lld: error: unified LTO compilation must use compatible bitcode modules
// MISSING-DAG: undefined symbol: dpi_add
// MISSING-DAG: undefined symbol: dpi_scalars
// MISSING-DAG: undefined symbol: dpi_update
// MISSING-DAG: undefined symbol: dpi_unused
// MISSING-DAG: undefined symbol: dpi_logic_inout
// MISSING-DAG: undefined symbol: dpi_void
// MISSING-DAG: undefined symbol: dpi_reals
