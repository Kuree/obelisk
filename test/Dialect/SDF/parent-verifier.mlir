// RUN: obelisk-opt %s --split-input-file --verify-diagnostics -o /dev/null

module {
  func.func @nested_sdf_root() {
    // expected-error@+1 {{'obelisk_sdf.delay_file' op must be directly nested under builtin.module}}
    obelisk_sdf.delay_file "nested.sdf" version "4.0" {}
    func.return
  }
}

// -----

module {
  // expected-error@+1 {{'obelisk_sdf.cell' op expects parent op 'obelisk_sdf.delay_file'}}
  obelisk_sdf.cell "dut" {}
}

// -----

module {
  // expected-error@+1 {{'obelisk_sdf.path_delay' op expects parent op 'obelisk_sdf.cell'}}
  obelisk_sdf.path_delay absolute iopath #obelisk_sdf.port<name = "a", hasIndex = false, index = 0, edge = none> to #obelisk_sdf.port<name = "z", hasIndex = false, index = 0, edge = none> delays [#obelisk_sdf.delay_value<form = scalar, typ = #obelisk_sdf.decimal<"1">>]

}

// -----

module {
  // expected-error@+1 {{'obelisk_sdf.timing_check' op expects parent op 'obelisk_sdf.cell'}}
  obelisk_sdf.timing_check width [#obelisk_sdf.timing_event<port = #obelisk_sdf.port<name = "clk", hasIndex = false, index = 0, edge = posedge>>] limits [#obelisk_sdf.delay_value<form = scalar, typ = #obelisk_sdf.decimal<"1">>]

}

// -----

module {
  // expected-error@+1 {{'obelisk_sdf.label' op expects parent op 'obelisk_sdf.cell'}}
  obelisk_sdf.label absolute "L" = #obelisk_sdf.delay_value<form = scalar, typ = #obelisk_sdf.decimal<"1">>

}

// -----

module {
  // expected-error@+1 {{'obelisk_sdf.interconnect_delay' op expects parent op 'obelisk_sdf.cell'}}
  obelisk_sdf.interconnect_delay absolute #obelisk_sdf.port<name = "a", hasIndex = false, index = 0, edge = none> to #obelisk_sdf.port<name = "z", hasIndex = false, index = 0, edge = none> delays [#obelisk_sdf.delay_value<form = scalar, typ = #obelisk_sdf.decimal<"1">>]

}

// -----

module {
  // expected-error@+1 {{'obelisk_sdf.terminal_delay' op expects parent op 'obelisk_sdf.cell'}}
  obelisk_sdf.terminal_delay absolute port #obelisk_sdf.port<name = "z", hasIndex = false, index = 0, edge = none> delays [#obelisk_sdf.delay_value<form = scalar, typ = #obelisk_sdf.decimal<"1">>]

}

// -----

module {
  // expected-error@+1 {{'obelisk_sdf.pulse' op expects parent op 'obelisk_sdf.cell'}}
  obelisk_sdf.pulse pathpulse reject #obelisk_sdf.delay_value<form = scalar, typ = #obelisk_sdf.decimal<"1">> error #obelisk_sdf.delay_value<form = scalar, typ = #obelisk_sdf.decimal<"2">>

}
