// RUN: talos-opt %s --verify-roundtrip -o %t
// RUN: FileCheck %s --implicit-check-not='"sdf.' < %t
// RUN: talos-opt %t -o %t.again
// RUN: diff %t %t.again

#a = #sdf.port<name = "a", hasIndex = false, index = 0, edge = none>
#z = #sdf.port<name = "z", hasIndex = true, index = 2, edge = posedge>
#delay = #sdf.delay_value<form = scalar, typ = <"1.25">>
#empty = #sdf.delay_value<form = empty>
#condition = #sdf.condition<tokens = [
  #sdf.condition_token<opcode = port, port = <name = "a", hasIndex = false, index = 0, edge = none>>
], inverted = false>

module {
  // CHECK: sdf.delay_file "timing.sdf" version "4.0" attributes {design = "dut", test.note = "retained"}
  sdf.delay_file "timing.sdf" version "4.0" attributes {
    design = "dut", test.note = "retained"
  } {
    // CHECK: sdf.cell "dut" instance "top/u"
    sdf.cell "dut" instance "top/u" {
      // CHECK: sdf.path_delay absolute iopath
      // CHECK-SAME: to #sdf.port
      // CHECK-SAME: delays [#sdf.delay_value<form = scalar, typ = <"1.25">>]
      sdf.path_delay absolute iopath #a to #z delays [#delay]
      // CHECK: sdf.path_delay increment cond
      // CHECK-SAME: when #sdf.condition
      // CHECK-SAME: delays [#sdf.delay_value<form = empty>]
      sdf.path_delay increment cond #a to #z when #condition delays [#empty]
      // CHECK: sdf.path_delay absolute condelse
      sdf.path_delay absolute condelse #a to #z delays [#delay]

      // CHECK: sdf.timing_check setuphold
      // CHECK-SAME: limits [#sdf.delay_value
      sdf.timing_check setuphold [
        #sdf.timing_event<port = <name = "a", hasIndex = false, index = 0, edge = none>>,
        #sdf.timing_event<port = <name = "z", hasIndex = true, index = 2, edge = posedge>, condition = #condition>
      ] limits [#delay, #delay]
      // CHECK: sdf.label absolute "LATENCY" = #sdf.delay_value
      sdf.label absolute "LATENCY" = #delay
      // CHECK: sdf.interconnect_delay increment
      // CHECK-SAME: to #sdf.port
      // CHECK-SAME: delays [#sdf.delay_value
      sdf.interconnect_delay increment #a to #z delays [#delay]
      // CHECK: sdf.terminal_delay absolute port #sdf.port
      sdf.terminal_delay absolute port #z delays [#delay]
      // CHECK: sdf.terminal_delay increment device delays
      sdf.terminal_delay increment device delays [#delay]
      // CHECK: sdf.pulse pathpulse from #sdf.port
      // CHECK-SAME: to #sdf.port
      // CHECK-SAME: reject #sdf.delay_value
      // CHECK-SAME: error #sdf.delay_value
      sdf.pulse pathpulse from #a to #z reject #delay error #delay
      // CHECK: sdf.pulse pathpulsepercent reject
      sdf.pulse pathpulsepercent reject #delay error #delay
    }
    // CHECK: sdf.cell "dut" wildcard
    sdf.cell "dut" wildcard {}
    // CHECK: sdf.cell "dut" {
    sdf.cell "dut" {}
  }
}
