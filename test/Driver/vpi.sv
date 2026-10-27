// REQUIRES: native-elf
// RUN: mkdir -p %t.dir/lib %t.dir/bin
// RUN: %target_clang -fPIC -shared -nostdlib %S/Inputs/vpi_startup.c -I%resource_dir/include -Wl,-soname,libtalos_vpi_test.so -o %t.dir/lib/libtalos_vpi_test.so
// RUN: %target_clang -fPIC -shared -nostdlib %S/Inputs/vpi_startup_second.c -I%resource_dir/include -Wl,-soname,libtalos_vpi_second.so -o %t.dir/lib/libtalos_vpi_second.so
// RUN: not talos --vpi=off %s %t.dir/lib/libtalos_vpi_test.so -o %t.dir/off 2>&1 | FileCheck %s --check-prefix=OFF
// RUN: cd %t.dir && talos --vpi=full %s lib/libtalos_vpi_test.so lib/libtalos_vpi_test.so lib/libtalos_vpi_second.so -o bin/simulator
// RUN: %t.dir/bin/simulator | FileCheck %s --check-prefix=OUTPUT
// RUN: llvm-readelf -d %t.dir/bin/simulator | FileCheck %s --check-prefix=DYNAMIC
// RUN: llvm-readelf --dyn-syms %t.dir/bin/simulator | FileCheck %s --check-prefix=EXPORTS

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

  final $display("final marker");
endmodule

// OFF: exports vlog_startup_routines but --vpi=off
// Packed dimensions on bit/logic variables retain the vpiBitVar/vpiLogicVar
// kind, so both logic declarations remain visible through vpiReg.
// OUTPUT: traverse module=vpi_top scope=vpi_top regs=2 same=1
// OUTPUT-NEXT: startup vpi_top.value size=64 aval=ffffffff bval=ffffffff
// OUTPUT-NEXT: binary underscore=9
// OUTPUT-NEXT: startup second
// OUTPUT-NEXT: start simulation
// OUTPUT-NEXT: run value=7
// OUTPUT-NEXT: forced net=1
// OUTPUT-NEXT: released value=7
// OUTPUT-NEXT: released net=0
// OUTPUT-NEXT: after value=9
// OUTPUT-NEXT: vpi assign restore=1311
// OUTPUT-NEXT: final marker
// OUTPUT-NEXT: end simulation
// DYNAMIC: (RUNPATH)      Library runpath: [$ORIGIN/../lib]
// DYNAMIC: (NEEDED)       Shared library: [libtalos_vpi_test.so]
// DYNAMIC: (NEEDED)       Shared library: [libtalos_vpi_second.so]
// EXPORTS-DAG: vpi_handle_by_name
// EXPORTS-DAG: vpi_get_value
// EXPORTS-DAG: vpi_put_value
