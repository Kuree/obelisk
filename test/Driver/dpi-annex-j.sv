// REQUIRES: native-elf
// RUN: rm -rf %t.dir
// RUN: mkdir -p %t.dir/root
// RUN: %target_clang -fPIC -shared %S/Inputs/dpi_annex_j_bootstrap.c -Wl,-soname,bootstrap.so -o %t.dir/root/bootstrap.so
// RUN: %target_clang -fPIC -shared %S/Inputs/dpi_annex_j_direct.c -Wl,-soname,direct.so -o %t.dir/root/direct.so
// RUN: cp %S/Inputs/dpi_annex_j.libs %t.dir/root/libraries.list
// RUN: obelisk %s -sv_root %t.dir/root -sv_liblist libraries.list -sv_lib direct -o %t
// RUN: %t | FileCheck %s
// RUN: %llvm_dist/bin/llvm-readelf -d %t | FileCheck %s --check-prefix=ELF

module dpi_annex_j;
  import "DPI-C" function int annex_j_bootstrap();
  import "DPI-C" function int annex_j_direct();

  initial begin
    $display("ANNEX J %0d %0d", annex_j_bootstrap(), annex_j_direct());
  end
endmodule

// CHECK: ANNEX J 11 22
// ELF: Shared library: [bootstrap.so]
// ELF-NEXT: Shared library: [direct.so]
