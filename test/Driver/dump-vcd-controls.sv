// RUN: %split-file %s %t
// RUN: cd %t && obelisk -fno-lto -O0 --vpi=off controls.sv -o o0.native
// RUN: cd %t && ./o0.native
// RUN: FileCheck %s --check-prefix=VCD < %t/controls.vcd
// RUN: cd %t && obelisk -fno-lto -O0 --vpi=off \
// RUN:   --execution-tier=bytecode controls.sv -o o0.bytecode
// RUN: cd %t && ./o0.bytecode
// RUN: FileCheck %s --check-prefix=VCD < %t/controls.vcd
// RUN: cd %t && obelisk -fno-lto -O3 --vpi=off controls.sv -o o3.native
// RUN: cd %t && ./o3.native
// RUN: FileCheck %s --check-prefix=VCD < %t/controls.vcd
// RUN: cd %t && obelisk -fno-lto -O3 --vpi=off \
// RUN:   --execution-tier=bytecode controls.sv -o o3.bytecode
// RUN: cd %t && ./o3.bytecode
// RUN: FileCheck %s --check-prefix=VCD < %t/controls.vcd

//--- controls.sv
module controls;
  logic [3:0] value = 4'h1;
  initial begin
    $dumpfile("controls.vcd");
    $dumpvars(0, controls);
    #1 value = 4'h2;
    $dumpall;
    $dumpflush;
    $dumpoff;
    value = 4'h3;
    // An explicit checkpoint remains observable while ordinary changes are
    // suspended. Resumption then republishes the same current value.
    $dumpall;
    $dumpon;
    $dumplimit(1 << 20);
    #1 $finish;
  end
endmodule

// VCD: #0
// VCD-NEXT: $dumpvars
// VCD: #1
// VCD-NEXT: $dumpall
// VCD: b10 {{.*}}
// VCD: $end
// VCD-NEXT: $dumpoff
// VCD: bx {{.*}}
// VCD: $end
// VCD-NEXT: $dumpall
// VCD: b11 {{.*}}
// VCD: $end
// VCD-NEXT: $dumpon
// VCD: b11 {{.*}}
// VCD: $end
