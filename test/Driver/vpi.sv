// RUN: mkdir -p %t.dir/lib %t.dir/bin
// RUN: %llvm_dist/bin/clang --target=x86_64-unknown-linux-gnu -fPIC \
// RUN:   -shared -nostdlib %S/Inputs/vpi_startup.c \
// RUN:   -I$(obelisk --print-resource-dir)/include \
// RUN:   -Wl,-soname,libobelisk_vpi_test.so -o %t.dir/lib/libobelisk_vpi_test.so
// RUN: %llvm_dist/bin/clang --target=x86_64-unknown-linux-gnu -fPIC \
// RUN:   -shared -nostdlib %S/Inputs/vpi_startup_second.c \
// RUN:   -I$(obelisk --print-resource-dir)/include \
// RUN:   -Wl,-soname,libobelisk_vpi_second.so \
// RUN:   -o %t.dir/lib/libobelisk_vpi_second.so
// RUN: not obelisk --vpi=off %s %t.dir/lib/libobelisk_vpi_test.so \
// RUN:   -o %t.dir/off 2>&1 | FileCheck %s --check-prefix=OFF
// RUN: cd %t.dir && obelisk --vpi=full %s lib/libobelisk_vpi_test.so \
// RUN:   lib/libobelisk_vpi_test.so lib/libobelisk_vpi_second.so -o bin/simulator
// RUN: %t.dir/bin/simulator | FileCheck %s --check-prefix=OUTPUT
// RUN: cd %t.dir && obelisk --execution-tier=bytecode --vpi=full %s \
// RUN:   lib/libobelisk_vpi_test.so lib/libobelisk_vpi_second.so \
// RUN:   -o bin/simulator-bytecode
// RUN: %t.dir/bin/simulator-bytecode | FileCheck %s --check-prefix=OUTPUT
// RUN: llvm-readelf -d %t.dir/bin/simulator \
// RUN:   | FileCheck %s --check-prefix=DYNAMIC
// RUN: llvm-readelf --dyn-syms %t.dir/bin/simulator \
// RUN:   | FileCheck %s --check-prefix=EXPORTS
// RUN: test ! -e %t.dir/bin/libobelisk_vpi_test.so
// RUN: test -f "$(obelisk --print-resource-dir)/include/vpi_user.h"
// RUN: test -f "$(obelisk --print-resource-dir)/include/sv_vpi_user.h"
// RUN: test -f "$(obelisk --print-resource-dir)/include/vpi_compatibility.h"
// RUN: mkdir -p %t.dir/relocated/bin %t.dir/relocated/lib
// RUN: cp %t.dir/bin/simulator %t.dir/relocated/bin/simulator
// RUN: cp %t.dir/lib/libobelisk_vpi_test.so \
// RUN:   %t.dir/lib/libobelisk_vpi_second.so %t.dir/relocated/lib
// RUN: %t.dir/relocated/bin/simulator | FileCheck %s --check-prefix=OUTPUT

module vpi_top;
  import "DPI-C" function int vpi_release_value();
  import "DPI-C" function int vpi_release_net();
  import "DPI-C" function int vpi_force_release_value();
  logic [63:0] value;
  logic driver;
  wire net_value;
  assign net_value = driver;
  initial begin
    value = 64'hffff;
    driver = 0;
    #1;
    $display("run value=%0d", value);
    $display("forced net=%0d", net_value);
    $display("released value=%0d", vpi_release_value());
    $display("released net=%0d", vpi_release_net());
    value = 9;
    $display("after value=%0d", value);
    assign value = 11;
    $display("vpi assign restore=%0d", vpi_force_release_value());
    deassign value;
  end
endmodule

// OFF: exports vlog_startup_routines but --vpi=off
// OUTPUT: traverse module=vpi_top scope=vpi_top regs=2 same=1
// OUTPUT-NEXT: startup vpi_top.value size=64 aval=ffffffff bval=ffffffff
// OUTPUT-NEXT: binary underscore=9
// OUTPUT-NEXT: startup second
// OUTPUT-NEXT: run value=7
// OUTPUT-NEXT: forced net=1
// OUTPUT-NEXT: released value=7
// OUTPUT-NEXT: released net=0
// OUTPUT-NEXT: after value=9
// OUTPUT-NEXT: vpi assign restore=1311
// DYNAMIC: (RUNPATH)      Library runpath: [$ORIGIN/../lib]
// DYNAMIC: (NEEDED)       Shared library: [libobelisk_vpi_test.so]
// DYNAMIC: (NEEDED)       Shared library: [libobelisk_vpi_second.so]
// EXPORTS-DAG: vpi_handle_by_name
// EXPORTS-DAG: vpi_get_value
// EXPORTS-DAG: vpi_put_value
