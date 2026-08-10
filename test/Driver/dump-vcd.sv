// Waveform collection is a once-per-time-slot difference over the canonical
// state planes, so every execution tier must produce the same file.
//
// RUN: %split-file %s %t
// RUN: cd %t && obelisk --vpi=read design.sv -o %t/native.sim
// RUN: cd %t && %t/native.sim
// RUN: FileCheck %s --check-prefix=VCD < %t/waves.vcd
//
// RUN: cd %t && obelisk --vpi=read --execution-tier=bytecode design.sv \
// RUN:   -o %t/bytecode.sim
// RUN: cd %t && %t/bytecode.sim
// RUN: FileCheck %s --check-prefix=VCD < %t/waves.vcd
//
// A computed file name is refused rather than silently ignored.
// RUN: not obelisk --vpi=read %t/computed.sv -o %t/computed.sim 2>&1 \
// RUN:   | FileCheck %s --check-prefix=COMPUTED

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

//--- computed.sv
module top;
  string name = "w.vcd";
  initial begin
    $dumpfile(name);
    $dumpvars(0, top);
  end
endmodule

// COMPUTED: $dumpfile requires a string literal file name
