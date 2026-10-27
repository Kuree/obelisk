// RUN: talos-opt %s --split-input-file --verify-diagnostics -o /dev/null

module {
  func.func @nested_sdf_root() {
    // expected-error@+1 {{'sdf.delay_file' op must be directly nested under builtin.module}}
    sdf.delay_file "nested.sdf" version "4.0" {}
    func.return
  }
}

// -----

module {
  // expected-error@+1 {{'sdf.cell' op expects parent op 'sdf.delay_file'}}
  sdf.cell "dut" {}
}

// -----

module {
  // expected-error@+1 {{'sdf.path_delay' op expects parent op 'sdf.cell'}}
  sdf.path_delay absolute iopath #sdf.port<name = "a", hasIndex = false, index = 0, edge = none> to #sdf.port<name = "z", hasIndex = false, index = 0, edge = none> delays [#sdf.delay_value<form = scalar, typ = #sdf.decimal<"1">>]

}

// -----

module {
  // expected-error@+1 {{'sdf.timing_check' op expects parent op 'sdf.cell'}}
  sdf.timing_check width [#sdf.timing_event<port = #sdf.port<name = "clk", hasIndex = false, index = 0, edge = posedge>>] limits [#sdf.delay_value<form = scalar, typ = #sdf.decimal<"1">>]

}

// -----

module {
  // expected-error@+1 {{'sdf.label' op expects parent op 'sdf.cell'}}
  sdf.label absolute "L" = #sdf.delay_value<form = scalar, typ = #sdf.decimal<"1">>

}

// -----

module {
  // expected-error@+1 {{'sdf.interconnect_delay' op expects parent op 'sdf.cell'}}
  sdf.interconnect_delay absolute #sdf.port<name = "a", hasIndex = false, index = 0, edge = none> to #sdf.port<name = "z", hasIndex = false, index = 0, edge = none> delays [#sdf.delay_value<form = scalar, typ = #sdf.decimal<"1">>]

}

// -----

module {
  // expected-error@+1 {{'sdf.terminal_delay' op expects parent op 'sdf.cell'}}
  sdf.terminal_delay absolute port #sdf.port<name = "z", hasIndex = false, index = 0, edge = none> delays [#sdf.delay_value<form = scalar, typ = #sdf.decimal<"1">>]

}

// -----

module {
  // expected-error@+1 {{'sdf.pulse' op expects parent op 'sdf.cell'}}
  sdf.pulse pathpulse reject #sdf.delay_value<form = scalar, typ = #sdf.decimal<"1">> error #sdf.delay_value<form = scalar, typ = #sdf.decimal<"2">>

}
