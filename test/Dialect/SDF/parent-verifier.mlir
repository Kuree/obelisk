// RUN: talos-opt %s --split-input-file --verify-diagnostics -o /dev/null

module {
  func.func @nested_sdf_root() {
    // expected-error@+1 {{'talos_sdf.delay_file' op must be directly nested under builtin.module}}
    talos_sdf.delay_file "nested.sdf" version "4.0" {}
    func.return
  }
}

// -----

module {
  // expected-error@+1 {{'talos_sdf.cell' op expects parent op 'talos_sdf.delay_file'}}
  talos_sdf.cell "dut" {}
}

// -----

module {
  // expected-error@+1 {{'talos_sdf.path_delay' op expects parent op 'talos_sdf.cell'}}
  talos_sdf.path_delay absolute iopath #talos_sdf.port<name = "a", hasIndex = false, index = 0, edge = none> to #talos_sdf.port<name = "z", hasIndex = false, index = 0, edge = none> delays [#talos_sdf.delay_value<form = scalar, typ = #talos_sdf.decimal<"1">>]

}

// -----

module {
  // expected-error@+1 {{'talos_sdf.timing_check' op expects parent op 'talos_sdf.cell'}}
  talos_sdf.timing_check width [#talos_sdf.timing_event<port = #talos_sdf.port<name = "clk", hasIndex = false, index = 0, edge = posedge>>] limits [#talos_sdf.delay_value<form = scalar, typ = #talos_sdf.decimal<"1">>]

}

// -----

module {
  // expected-error@+1 {{'talos_sdf.label' op expects parent op 'talos_sdf.cell'}}
  talos_sdf.label absolute "L" = #talos_sdf.delay_value<form = scalar, typ = #talos_sdf.decimal<"1">>

}

// -----

module {
  // expected-error@+1 {{'talos_sdf.interconnect_delay' op expects parent op 'talos_sdf.cell'}}
  talos_sdf.interconnect_delay absolute #talos_sdf.port<name = "a", hasIndex = false, index = 0, edge = none> to #talos_sdf.port<name = "z", hasIndex = false, index = 0, edge = none> delays [#talos_sdf.delay_value<form = scalar, typ = #talos_sdf.decimal<"1">>]

}

// -----

module {
  // expected-error@+1 {{'talos_sdf.terminal_delay' op expects parent op 'talos_sdf.cell'}}
  talos_sdf.terminal_delay absolute port #talos_sdf.port<name = "z", hasIndex = false, index = 0, edge = none> delays [#talos_sdf.delay_value<form = scalar, typ = #talos_sdf.decimal<"1">>]

}

// -----

module {
  // expected-error@+1 {{'talos_sdf.pulse' op expects parent op 'talos_sdf.cell'}}
  talos_sdf.pulse pathpulse reject #talos_sdf.delay_value<form = scalar, typ = #talos_sdf.decimal<"1">> error #talos_sdf.delay_value<form = scalar, typ = #talos_sdf.decimal<"2">>

}
