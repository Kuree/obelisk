// RUN: obelisk-opt %s --verify-roundtrip -o %t
// RUN: FileCheck %s --implicit-check-not='"obelisk_sdf.' < %t
// RUN: obelisk-opt %t -o %t.again
// RUN: diff %t %t.again

#a = #obelisk_sdf.port<name = "a", hasIndex = false, index = 0, edge = none>
#z = #obelisk_sdf.port<name = "z", hasIndex = true, index = 2, edge = posedge>
#delay = #obelisk_sdf.delay_value<form = scalar, typ = <"1.25">>
#empty = #obelisk_sdf.delay_value<form = empty>
#condition = #obelisk_sdf.condition<tokens = [
  #obelisk_sdf.condition_token<opcode = port, port = <name = "a", hasIndex = false, index = 0, edge = none>>
], inverted = false>

module {
  // CHECK: obelisk_sdf.delay_file "timing.sdf" version "4.0" attributes {design = "dut", test.note = "retained"}
  obelisk_sdf.delay_file "timing.sdf" version "4.0" attributes {
    design = "dut", test.note = "retained"
  } {
    // CHECK: obelisk_sdf.cell "dut" instance "top/u"
    obelisk_sdf.cell "dut" instance "top/u" {
      // CHECK: obelisk_sdf.path_delay absolute iopath
      // CHECK-SAME: to #obelisk_sdf.port
      // CHECK-SAME: delays [#obelisk_sdf.delay_value<form = scalar, typ = <"1.25">>]
      obelisk_sdf.path_delay absolute iopath #a to #z delays [#delay]
      // CHECK: obelisk_sdf.path_delay increment cond
      // CHECK-SAME: when #obelisk_sdf.condition
      // CHECK-SAME: delays [#obelisk_sdf.delay_value<form = empty>]
      obelisk_sdf.path_delay increment cond #a to #z when #condition delays [#empty]
      // CHECK: obelisk_sdf.path_delay absolute condelse
      obelisk_sdf.path_delay absolute condelse #a to #z delays [#delay]

      // CHECK: obelisk_sdf.timing_check setuphold
      // CHECK-SAME: limits [#obelisk_sdf.delay_value
      obelisk_sdf.timing_check setuphold [
        #obelisk_sdf.timing_event<port = <name = "a", hasIndex = false, index = 0, edge = none>>,
        #obelisk_sdf.timing_event<port = <name = "z", hasIndex = true, index = 2, edge = posedge>, condition = #condition>
      ] limits [#delay, #delay]
      // CHECK: obelisk_sdf.label absolute "LATENCY" = #obelisk_sdf.delay_value
      obelisk_sdf.label absolute "LATENCY" = #delay
      // CHECK: obelisk_sdf.interconnect_delay increment
      // CHECK-SAME: to #obelisk_sdf.port
      // CHECK-SAME: delays [#obelisk_sdf.delay_value
      obelisk_sdf.interconnect_delay increment #a to #z delays [#delay]
      // CHECK: obelisk_sdf.terminal_delay absolute port #obelisk_sdf.port
      obelisk_sdf.terminal_delay absolute port #z delays [#delay]
      // CHECK: obelisk_sdf.terminal_delay increment device delays
      obelisk_sdf.terminal_delay increment device delays [#delay]
      // CHECK: obelisk_sdf.pulse pathpulse from #obelisk_sdf.port
      // CHECK-SAME: to #obelisk_sdf.port
      // CHECK-SAME: reject #obelisk_sdf.delay_value
      // CHECK-SAME: error #obelisk_sdf.delay_value
      obelisk_sdf.pulse pathpulse from #a to #z reject #delay error #delay
      // CHECK: obelisk_sdf.pulse pathpulsepercent reject
      obelisk_sdf.pulse pathpulsepercent reject #delay error #delay
    }
    // CHECK: obelisk_sdf.cell "dut" wildcard
    obelisk_sdf.cell "dut" wildcard {}
    // CHECK: obelisk_sdf.cell "dut" {
    obelisk_sdf.cell "dut" {}
  }
}
