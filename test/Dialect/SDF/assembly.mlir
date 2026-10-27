// RUN: talos-opt %s --verify-roundtrip -o %t
// RUN: FileCheck %s --implicit-check-not='"talos_sdf.' < %t
// RUN: talos-opt %t -o %t.again
// RUN: diff %t %t.again

#a = #talos_sdf.port<name = "a", hasIndex = false, index = 0, edge = none>
#z = #talos_sdf.port<name = "z", hasIndex = true, index = 2, edge = posedge>
#delay = #talos_sdf.delay_value<form = scalar, typ = <"1.25">>
#empty = #talos_sdf.delay_value<form = empty>
#condition = #talos_sdf.condition<tokens = [
  #talos_sdf.condition_token<opcode = port, port = <name = "a", hasIndex = false, index = 0, edge = none>>
], inverted = false>

module {
  // CHECK: talos_sdf.delay_file "timing.sdf" version "4.0" attributes {design = "dut", test.note = "retained"}
  talos_sdf.delay_file "timing.sdf" version "4.0" attributes {
    design = "dut", test.note = "retained"
  } {
    // CHECK: talos_sdf.cell "dut" instance "top/u"
    talos_sdf.cell "dut" instance "top/u" {
      // CHECK: talos_sdf.path_delay absolute iopath
      // CHECK-SAME: to #talos_sdf.port
      // CHECK-SAME: delays [#talos_sdf.delay_value<form = scalar, typ = <"1.25">>]
      talos_sdf.path_delay absolute iopath #a to #z delays [#delay]
      // CHECK: talos_sdf.path_delay increment cond
      // CHECK-SAME: when #talos_sdf.condition
      // CHECK-SAME: delays [#talos_sdf.delay_value<form = empty>]
      talos_sdf.path_delay increment cond #a to #z when #condition delays [#empty]
      // CHECK: talos_sdf.path_delay absolute condelse
      talos_sdf.path_delay absolute condelse #a to #z delays [#delay]

      // CHECK: talos_sdf.timing_check setuphold
      // CHECK-SAME: limits [#talos_sdf.delay_value
      talos_sdf.timing_check setuphold [
        #talos_sdf.timing_event<port = <name = "a", hasIndex = false, index = 0, edge = none>>,
        #talos_sdf.timing_event<port = <name = "z", hasIndex = true, index = 2, edge = posedge>, condition = #condition>
      ] limits [#delay, #delay]
      // CHECK: talos_sdf.label absolute "LATENCY" = #talos_sdf.delay_value
      talos_sdf.label absolute "LATENCY" = #delay
      // CHECK: talos_sdf.interconnect_delay increment
      // CHECK-SAME: to #talos_sdf.port
      // CHECK-SAME: delays [#talos_sdf.delay_value
      talos_sdf.interconnect_delay increment #a to #z delays [#delay]
      // CHECK: talos_sdf.terminal_delay absolute port #talos_sdf.port
      talos_sdf.terminal_delay absolute port #z delays [#delay]
      // CHECK: talos_sdf.terminal_delay increment device delays
      talos_sdf.terminal_delay increment device delays [#delay]
      // CHECK: talos_sdf.pulse pathpulse from #talos_sdf.port
      // CHECK-SAME: to #talos_sdf.port
      // CHECK-SAME: reject #talos_sdf.delay_value
      // CHECK-SAME: error #talos_sdf.delay_value
      talos_sdf.pulse pathpulse from #a to #z reject #delay error #delay
      // CHECK: talos_sdf.pulse pathpulsepercent reject
      talos_sdf.pulse pathpulsepercent reject #delay error #delay
    }
    // CHECK: talos_sdf.cell "dut" wildcard
    talos_sdf.cell "dut" wildcard {}
    // CHECK: talos_sdf.cell "dut" {
    talos_sdf.cell "dut" {}
  }
}
