// Waveform collection is a once-per-time-slot difference over the canonical
// state planes, so every execution tier must produce the same file.
//
// RUN: %split-file %s %t
// RUN: cd %t && obelisk -fno-lto design.sv -o %t/native.sim
// RUN: cd %t && %t/native.sim
// RUN: FileCheck %s --check-prefix=VCD --implicit-check-not='$root' \
// RUN:   < %t/waves.vcd
//
// RUN: cd %t && obelisk -fno-lto --execution-tier=bytecode design.sv \
// RUN:   -o %t/bytecode.sim
// RUN: cd %t && %t/bytecode.sim
// RUN: FileCheck %s --check-prefix=VCD --implicit-check-not='$root' \
// RUN:   < %t/waves.vcd
//
// A VCD may have multiple root scopes. Flattening the synthetic design root
// must retain all independently elaborated top-level modules.
// RUN: cd %t && obelisk -fno-lto multiple-tops.sv -o %t/multiple-tops.sim
// RUN: cd %t && %t/multiple-tops.sim
// RUN: FileCheck %s --check-prefix=MULTIPLE --implicit-check-not='$root' \
// RUN:   < %t/multiple.vcd
//
// Explicit generic scheduling still needs the waveform database even with VPI
// disabled, and an individual variable is a valid $dumpvars selection.
// RUN: cd %t && obelisk -fno-lto --native-scheduler=generic selected.sv \
// RUN:   -o %t/selected.sim
// RUN: cd %t && %t/selected.sim
// RUN: FileCheck %s --check-prefix=SELECTED < %t/selected.vcd
//
// The LRM permits a string expression and an omitted argument as file names.
// RUN: cd %t && obelisk -fno-lto computed.sv -o %t/computed.sim
// RUN: cd %t && %t/computed.sim
// RUN: FileCheck %s --check-prefix=COMPUTED < %t/computed.vcd
// RUN: cd %t && obelisk -fno-lto --execution-tier=bytecode computed.sv \
// RUN:   -o %t/computed-bytecode.sim
// RUN: cd %t && %t/computed-bytecode.sim
// RUN: FileCheck %s --check-prefix=COMPUTED < %t/computed.vcd
// RUN: cd %t && obelisk -fno-lto default-name.sv -o %t/default-name.sim
// RUN: cd %t && %t/default-name.sim
// RUN: FileCheck %s --check-prefix=DEFAULT-NAME < %t/dump.vcd
// RUN: cd %t && obelisk -fno-lto integral-name.sv -o %t/integral-name.sim
// RUN: cd %t && %t/integral-name.sim
// RUN: FileCheck %s --check-prefix=INTEGRAL-NAME < %t/integral.vcd

//--- design.sv
module sub(input logic clk, output logic [3:0] tick);
  logic [3:0] counter = 4'd0;
  // An unpacked array is not a value: each element is its own signal.
  logic [1:0] slot [0:1];
  always @(posedge clk) begin
    counter <= counter + 4'd1;
    slot[1] <= slot[1] + 2'd1;
  end
  assign tick = counter;
endmodule

module top;
  logic clk = 0;
  logic [3:0] tick;
  sub u(.clk(clk), .tick(tick));
  always #5 clk = ~clk;
  initial begin
    $dumpfile("waves.vcd");
    $dumpvars(0, top);
    #22 $dumpoff;
    #10 $dumpon;
    #10 $finish;
  end
endmodule

// VCD: $timescale
// VCD: 1ns
// VCD: $scope module top $end

// The declaration order follows the design hierarchy, and every declared
// object carries its own name.
// VCD-DAG: $var {{.*}} clk $end
// VCD-DAG: $var {{.*}} counter [3:0] $end
// Unpacked arrays expand per element, and each element's declared range
// describes its own bits rather than the element bounds.
// VCD-DAG: $var reg 2 {{.*}} slot[0] [1:0] $end
// VCD-DAG: $var reg 2 {{.*}} slot[1] [1:0] $end

// VCD: $enddefinitions $end
// The initial section is emitted at the end of the slot that selected it, so
// it lands at time zero even though $dumpvars ran mid-slot.
// VCD: #0
// VCD-NEXT: $dumpvars
// VCD: $end
// Only changed variables appear afterwards.
// VCD: #5
// VCD: #22
// VCD-NEXT: $dumpoff
// VCD: #32
// VCD-NEXT: $dumpon

//--- multiple-tops.sv
module first_top;
  logic first = 0;
  initial begin
    $dumpfile("multiple.vcd");
    $dumpvars;
    #1 first = 1;
    #1 $finish;
  end
endmodule

module second_top;
  logic second = 0;
  initial #1 second = 1;
endmodule

// MULTIPLE: $scope module first_top $end
// MULTIPLE: $var reg 1 {{.*}} first $end
// MULTIPLE: $upscope $end
// MULTIPLE: $scope module second_top $end
// MULTIPLE: $var reg 1 {{.*}} second $end
// MULTIPLE: $upscope $end
// MULTIPLE: $enddefinitions $end

//--- computed.sv
module top;
  string name = "computed.vcd";
  logic traced = 0;
  initial begin
    $dumpfile(name);
    $dumpvars(0, top);
    #1 traced = 1;
    #1 $finish;
  end
endmodule

// COMPUTED: $scope module top $end
// COMPUTED: $var reg 1 {{.*}} traced $end
// COMPUTED-NOT: name
// COMPUTED: $enddefinitions $end

//--- default-name.sv
module default_name;
  logic value = 0;
  initial begin
    $dumpfile();
    $dumpvars(0, default_name);
    #1 $finish;
  end
endmodule

// DEFAULT-NAME: $var reg 1 {{.*}} value $end
// DEFAULT-NAME: $enddefinitions $end

//--- integral-name.sv
module integral_name;
  logic [8*12-1:0] name = "integral.vcd";
  logic value = 0;
  initial begin
    $dumpfile(name);
    $dumpvars(0, integral_name);
    #1 $finish;
  end
endmodule

// INTEGRAL-NAME: $var reg 1 {{.*}} value $end
// INTEGRAL-NAME: $enddefinitions $end

//--- selected.sv
module selected;
  logic traced = 0;
  logic omitted = 0;
  initial begin
    $dumpfile("selected.vcd");
    $dumpvars(0, selected.traced);
    #1 traced = 1;
    #1 $finish;
  end
endmodule

// SELECTED: $scope module selected $end
// SELECTED: $var reg 1 {{.*}} traced $end
// SELECTED-NOT: omitted
// SELECTED: $enddefinitions $end
