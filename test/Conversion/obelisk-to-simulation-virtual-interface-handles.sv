// RUN: obelisk -emit-slang %s | obelisk-opt --convert-slang-to-obelisk '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk -emit-slang -DINPUT_SKEW=#0 %s | obelisk-opt --convert-slang-to-obelisk '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=INPUT-ZERO
// RUN: obelisk -emit-slang -DINPUT_SKEW=negedge %s | obelisk-opt --convert-slang-to-obelisk '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=INPUT-EDGE
// RUN: obelisk -emit-slang -DINPUT_SKEW=#2 %s | obelisk-opt --convert-slang-to-obelisk '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=INPUT-SKEW
// RUN: obelisk -emit-slang -DOUTPUT_SKEW=negedge %s | obelisk-opt --convert-slang-to-obelisk '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=OUTPUT-EDGE
// RUN: obelisk -emit-slang -DPROGRAM %s | obelisk-opt --convert-slang-to-obelisk '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=PROGRAM
// RUN: obelisk -emit-slang -DNET_OUTPUT %s | obelisk-opt --convert-slang-to-obelisk '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=NET-OUTPUT
// RUN: obelisk -emit-slang -DCLOCK_IFF %s | obelisk-opt --convert-slang-to-obelisk '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=IFF

// Two instances of one interface share the handle type that both virtual
// interfaces (one through the phy modport) are declared with. Each instance
// keeps its own storage, so a handle access selects between them by scope.
// CHECK: simulation.scope.decl [[BUS:[0-9]+]] parent 1 hierarchy "top.bus" debug "bus_if" interface "[[IFACE:[^"]+]]"
// CHECK: simulation.scope.decl [[OTHER:[0-9]+]] parent 1 hierarchy "top.other" debug "bus_if" interface "[[IFACE]]"
// CHECK: simulation.vpi_typespec.decl @{{__obelisk_vpi_interface_typespec_[0-9A-F]+_modport_}} id {{[0-9]+}} in [[BUS]]
// CHECK: simulation.vpi_typespec.decl @{{__obelisk_vpi_interface_typespec_[0-9A-F]+_modport_[0-9A-F]+}} id {{[0-9]+}} in [[BUS]]
// CHECK-DAG: simulation.storage.decl [[SIGNAL:[0-9]+]] in [[BUS]] : !simulation.logic<1> design hierarchy "top.bus.signal"
// CHECK-DAG: simulation.net.decl {{[0-9]+}} in [[BUS]] : !simulation.logic<1> design hierarchy "top.bus.ready"
// CHECK-DAG: simulation.storage.decl {{[0-9]+}} in [[BUS]] : !simulation.logic<1> design hierarchy "top.bus.clk"
// CHECK-DAG: simulation.storage.decl [[OTHER_SIGNAL:[0-9]+]] in [[OTHER]] : !simulation.logic<1> design hierarchy "top.other.signal"
// CHECK-DAG: simulation.net.decl {{[0-9]+}} in [[OTHER]] : !simulation.logic<1> design hierarchy "top.other.ready"
// CHECK-DAG: simulation.storage.decl {{[0-9]+}} in [[OTHER]] : !simulation.logic<1> design hierarchy "top.other.clk"
// CHECK: simulation.storage.decl {{[0-9]+}} in {{[0-9]+}} : !simulation.virtual_interface<"[[IFACE]]", ""> design hierarchy "top.d.vif"
// CHECK: simulation.storage.decl {{[0-9]+}} in {{[0-9]+}} : !simulation.virtual_interface<"[[IFACE]]", "phy"> design hierarchy "top.d.vif_phy"
// IEEE 1800-2023 14.13 and 14.16: a clocking drive issued before the clocking
// event is outlined so it does not block its caller; the child waits for the
// event and publishes NBA.
// CHECK-LABEL: simulation.func private @unit_0.$clocking_output.{{[0-9]+}}(
// CHECK: simulation.suspend.edge posedge
// CHECK: simulation.nba.enqueue
// Each instance's clocking input is sampled in the Observed region (8) at the
// clocking event; the default #1step skew reads the preponed value.
// CHECK-LABEL: simulation.func private @unit_0.$clocking_input.{{[0-9]+}}(
// CHECK: simulation.suspend.edge posedge {{.*}}resume_region = 8 : i32
// CHECK: simulation.assert.sampled_read
// CHECK: simulation.assert.clocked_sample_update
// CHECK-LABEL: simulation.func private @unit_0.$clocking_input.{{[0-9]+}}(
// CHECK: simulation.suspend.edge posedge {{.*}}resume_region = 8 : i32
// CHECK: simulation.assert.sampled_read
// CHECK: simulation.assert.clocked_sample_update
// A drive issued after @(vif.cb) belongs to the current clocking event and does
// not wait for another one.
// CHECK-LABEL: simulation.func private @unit_0.$clocking_output.{{[0-9]+}}(
// CHECK-SAME: home_region = 10 : i32
// CHECK-NOT: simulation.suspend
// CHECK: simulation.nba.enqueue
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-DAG: simulation.context.storage %arg0{{\[}}[[SIGNAL]]{{\]}}
// CHECK-DAG: simulation.context.storage %arg0{{\[}}[[OTHER_SIGNAL]]{{\]}}
// CHECK: simulation.string.output_format {{.*}} flags = [32, 256] {{.*}} !simulation.virtual_interface<"[[IFACE]]", "">
// CHECK: simulation.virtual_interface.scope
// CHECK: simulation.spawn @unit_0.$clocking_output.
// Reads of vif.cb.ready select the retained sample of the handle's instance
// instead of resampling at each use.
// CHECK: simulation.assert.clocked_sample_read
// CHECK: simulation.assert.clocked_sample_read
// CHECK: simulation.virtual_interface.scope
// CHECK: simulation.suspend.edge posedge {{.*}}resume_region = 9 : i32
// CHECK: simulation.spawn @unit_0.$clocking_output.
// CHECK-NOT: obelisk.sv.

// A #0 input skew samples the current value instead of the preponed one.
// INPUT-ZERO-LABEL: simulation.func private @unit_0.$clocking_input.{{[0-9]+}}(
// INPUT-ZERO: simulation.suspend.edge posedge {{.*}}resume_region = 8 : i32
// INPUT-ZERO-NOT: simulation.assert.sampled_read
// INPUT-ZERO: simulation.net.read
// INPUT-ZERO: simulation.assert.clocked_sample_update

// An edge input skew samples at that edge of the clock.
// INPUT-EDGE-LABEL: simulation.func private @unit_0.$clocking_input.{{[0-9]+}}(
// INPUT-EDGE: simulation.suspend.edge negedge {{.*}}resume_region = 8 : i32
// INPUT-EDGE: simulation.net.read
// INPUT-EDGE: simulation.assert.clocked_sample_update

// A #2 input skew tracks the input and commits each value two time units
// later (region 16) so the clocking event reads the value from #2 before.
// INPUT-SKEW-LABEL: simulation.func private @unit_0.$clocking_input_delay.{{[0-9]+}}.commit(
// INPUT-SKEW: simulation.time.constant 2{{$|[^0-9]}}
// INPUT-SKEW: simulation.suspend.delay {{.*}}resume_region = 16 : i32
// INPUT-SKEW: simulation.assert.clocked_sample_update
// INPUT-SKEW-LABEL: simulation.func private @unit_0.$clocking_input_delay.{{[0-9]+}}(
// INPUT-SKEW: simulation.assert.sampled_read
// INPUT-SKEW: simulation.suspend.change
// INPUT-SKEW: simulation.net.read
// INPUT-SKEW: simulation.spawn @unit_0.$clocking_input_delay.{{[0-9]+}}.commit
// INPUT-SKEW-NOT: obelisk.sv.

// An edge output skew drives at that clock edge after the clocking event.
// OUTPUT-EDGE-LABEL: simulation.func private @unit_0.$clocking_output.{{[0-9]+}}(
// OUTPUT-EDGE: simulation.suspend.edge posedge
// OUTPUT-EDGE: simulation.suspend.edge negedge
// OUTPUT-EDGE: simulation.nba.enqueue

// A program's clocking drives run in the reactive domain.
// PROGRAM-LABEL: simulation.func private @unit_0.$clocking_output.{{[0-9]+}}(
// PROGRAM-SAME: domain = 1 : i32
// PROGRAM-LABEL: simulation.func private @unit_0.$clocking_output.{{[0-9]+}}(
// PROGRAM-SAME: domain = 1 : i32
// PROGRAM-SAME: home_region = 10 : i32
// PROGRAM: simulation.nba.enqueue

// A clockvar named apart from the net it drives publishes through the net's
// driver.
// NET-OUTPUT-LABEL: simulation.func private @unit_0.$clocking_output.{{[0-9]+}}(
// NET-OUTPUT-SAME: !simulation.driver<!simulation.logic<1>>
// NET-OUTPUT: simulation.nba.enqueue {{.*}} !simulation.driver<!simulation.logic<1>>
// NET-OUTPUT-LABEL: simulation.func private @unit_0(
// NET-OUTPUT: simulation.spawn @unit_0.$clocking_output.{{[0-9]+}}({{.*}}!simulation.driver<!simulation.logic<1>>

// A clocking event with iff is monitored by the interface instance; samplers
// and drives wait for that monitor's event.
// IFF-LABEL: simulation.func private @unit_{{[0-9]+}}.$clocking_input.{{[0-9]+}}(
// IFF: simulation.suspend.event {{.*}}resume_region = 8 : i32
// IFF: simulation.assert.sampled_read
// IFF: simulation.assert.clocked_sample_update
// IFF-NOT: simulation.suspend.edge
// IFF: simulation.nba.enqueue

`ifndef INPUT_SKEW
`define INPUT_SKEW
`endif
`ifndef OUTPUT_SKEW
`define OUTPUT_SKEW
`endif
`ifdef CLOCK_IFF
`define CLOCK_EVENT @(posedge clk iff ready)
`else
`define CLOCK_EVENT @(posedge clk)
`endif
`ifdef PROGRAM
`define UNIT program
`define END_UNIT endprogram
`else
`define UNIT module
`define END_UNIT endmodule
`endif
`ifdef NET_OUTPUT
`define OUTPUT drive = ready
`define DRIVEN drive
`else
`define OUTPUT signal
`define DRIVEN signal
`endif

interface bus_if;
  logic signal;
  wire ready;
  logic clk;
  clocking cb `CLOCK_EVENT;
    input `INPUT_SKEW ready;
    output `OUTPUT_SKEW `OUTPUT;
  endclocking
  modport phy(clocking cb);
endinterface

`UNIT driver(bus_if bus, bus_if other);
  virtual bus_if vif;
  virtual bus_if.phy vif_phy;
  string text;
  logic sampled;
  logic probed;
  initial begin
    vif = bus;
    text = $sformatf("%p", vif);
    vif.signal = 1'b1;
    sampled = vif.signal;
    vif.cb.`DRIVEN <= 1'b1;
    sampled = vif.cb.ready;
    @(vif.cb);
    vif.cb.`DRIVEN <= 1'b0;
    @(vif.signal);
  end
`ifndef PROGRAM
  // A combinational read of a virtual-interface member.
  always_comb probed = vif.ready;
`endif
`END_UNIT

module top;
  bus_if bus();
  bus_if other();
  driver d(bus, other);
endmodule
