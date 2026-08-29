// RUN: obelisk-opt %s --split-input-file --verify-diagnostics -o /dev/null

module {
  func.func @nested_sdf_root() {
    // expected-error@+1 {{'obelisk_sdf.delay_file' op must be directly nested under builtin.module}}
    obelisk_sdf.delay_file attributes {
        source = "nested.sdf", sdf_version = "4.0"} {}
    func.return
  }
}

// -----

module {
  // expected-error@+1 {{'obelisk_sdf.cell' op expects parent op 'obelisk_sdf.delay_file'}}
  obelisk_sdf.cell attributes {cell_type = "dut"} {}
}

// -----

module {
  // expected-error@+1 {{'obelisk_sdf.path_delay' op expects parent op 'obelisk_sdf.cell'}}
  obelisk_sdf.path_delay {
    mode = 0 : i32, kind = 0 : i32,
    input = #obelisk_sdf.port<name = "a", hasIndex = false, index = 0, edge = none>,
    output = #obelisk_sdf.port<name = "z", hasIndex = false, index = 0, edge = none>,
    delays = [#obelisk_sdf.delay_value<form = scalar, typ = #obelisk_sdf.decimal<"1">>]
  }
}

// -----

module {
  // expected-error@+1 {{'obelisk_sdf.timing_check' op expects parent op 'obelisk_sdf.cell'}}
  obelisk_sdf.timing_check {
    kind = 9 : i32,
    events = [#obelisk_sdf.timing_event<port = #obelisk_sdf.port<name = "clk", hasIndex = false, index = 0, edge = posedge>>],
    limits = [#obelisk_sdf.delay_value<form = scalar, typ = #obelisk_sdf.decimal<"1">>]
  }
}

// -----

module {
  // expected-error@+1 {{'obelisk_sdf.label' op expects parent op 'obelisk_sdf.cell'}}
  obelisk_sdf.label {
    name = "L", value = #obelisk_sdf.delay_value<form = scalar, typ = #obelisk_sdf.decimal<"1">>
  }
}

// -----

module {
  // expected-error@+1 {{'obelisk_sdf.interconnect_delay' op expects parent op 'obelisk_sdf.cell'}}
  obelisk_sdf.interconnect_delay {
    mode = 0 : i32,
    source_port = #obelisk_sdf.port<name = "a", hasIndex = false, index = 0, edge = none>,
    destination_port = #obelisk_sdf.port<name = "z", hasIndex = false, index = 0, edge = none>,
    delays = [#obelisk_sdf.delay_value<form = scalar, typ = #obelisk_sdf.decimal<"1">>]
  }
}

// -----

module {
  // expected-error@+1 {{'obelisk_sdf.terminal_delay' op expects parent op 'obelisk_sdf.cell'}}
  obelisk_sdf.terminal_delay {
    mode = 0 : i32, kind = 0 : i32,
    terminal = #obelisk_sdf.port<name = "z", hasIndex = false, index = 0, edge = none>,
    delays = [#obelisk_sdf.delay_value<form = scalar, typ = #obelisk_sdf.decimal<"1">>]
  }
}

// -----

module {
  // expected-error@+1 {{'obelisk_sdf.pulse' op expects parent op 'obelisk_sdf.cell'}}
  obelisk_sdf.pulse {
    kind = 0 : i32,
    reject = #obelisk_sdf.delay_value<form = scalar, typ = #obelisk_sdf.decimal<"1">>,
    error = #obelisk_sdf.delay_value<form = scalar, typ = #obelisk_sdf.decimal<"2">>
  }
}
