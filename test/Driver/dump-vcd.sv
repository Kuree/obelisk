// Waveform collection is a once-per-time-slot difference over the canonical
// state planes, so every execution tier must produce the same file.
//
// RUN: %split-file %s %t
// RUN: cd %t && talos design.sv -o %t/native.sim
// RUN: cd %t && %t/native.sim
// RUN: FileCheck %s --check-prefix=VCD --implicit-check-not='$root' < %t/waves.vcd
// RUN: cd %t && talos --native-scheduler=eval design.sv -o %t/eval.sim
// RUN: cd %t && %t/eval.sim
// RUN: FileCheck %s --check-prefix=VCD --implicit-check-not='$root' < %t/waves.vcd

// Forcing the periodic plan, an active dump must hand off to native fragments
// and canonical NBA publication instead of spinning at the first clock edge.
// Other tiers and designs are in dump-vcd-{bytecode,scopes,computed,names}.test.

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
// VCD-DAG: $var reg 4 [[COUNTER:[^ ]+]] counter [3:0] $end
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
// VCD: b1 [[COUNTER]]
// VCD: #15
// VCD: b10 [[COUNTER]]
// VCD: #22
// VCD-NEXT: $dumpoff
// VCD: #32
// VCD-NEXT: $dumpon
// VCD: b11 [[COUNTER]]
// VCD: #35
// VCD: b100 [[COUNTER]]

//--- positive-timescale.sv
module positive_timescale;
  timeunit 100s/100s;
  bit value = 0;
  initial begin
    $dumpfile("positive.vcd");
    $dumpvars(0, positive_timescale);
    $dumpports(positive_timescale, "positive-ports.vcd");
    #1 value = 1;
    #1 $finish;
  end
endmodule

// POSITIVE-TIMESCALE: $timescale
// POSITIVE-TIMESCALE-NEXT: 100s
// POSITIVE-TIMESCALE: #1
// POSITIVE-PORTS: $timescale
// POSITIVE-PORTS-NEXT: 100s

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
