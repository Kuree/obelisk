// RUN: rm -rf %t.dir
// RUN: mkdir -p %t.dir/root
// RUN: %llvm_dist/bin/clang --target=x86_64-unknown-linux-gnu -fPIC -shared \
// RUN:   %S/Inputs/dpi_annex_j_bootstrap.c -Wl,-soname,bootstrap.so \
// RUN:   -o %t.dir/root/bootstrap.so
// RUN: %llvm_dist/bin/clang --target=x86_64-unknown-linux-gnu -fPIC -shared \
// RUN:   %S/Inputs/dpi_annex_j_direct.c -Wl,-soname,direct.so \
// RUN:   -o %t.dir/root/direct.so
// RUN: cp %S/Inputs/dpi_annex_j.libs %t.dir/root/libraries.list
// RUN: obelisk %s -sv_root %t.dir/root \
// RUN:   -sv_liblist libraries.list -sv_lib direct -o %t
// RUN: %t | FileCheck %s
// RUN: %llvm_dist/bin/llvm-readelf -d %t | FileCheck %s --check-prefix=ELF
// RUN: not obelisk %s \
// RUN:   -sv_liblist %S/Inputs/dpi_annex_j_bootstrap.c -o %t.bad 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BAD-BOOTSTRAP
// RUN: not obelisk --target=wasm32 %s -sv_lib direct -o %t.wasm 2>&1 \
// RUN:   | FileCheck %s --check-prefix=WASM
// RUN: not obelisk --target=wasm32 %s -sv_liblist %t.missing -o %t.wasm 2>&1 \
// RUN:   | FileCheck %s --check-prefix=WASM

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
// BAD-BOOTSTRAP: must begin with #!SV_LIBRARIES
// WASM: Annex J DPI libraries require --target=native
