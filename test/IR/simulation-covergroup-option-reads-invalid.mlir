// RUN: obelisk-opt %s -split-input-file -verify-diagnostics

module {
  simulation.covergroup.decl @cg schema 1
  func.func @numeric_string(%ctx: !simulation.context, %handle: !simulation.covergroup_handle<@cg>) {
    // expected-error @+1 {{requires a comment or group-name option kind}}
    %value = simulation.covergroup.get_string_option %ctx, %handle item 0 option auto_bin_max : !simulation.context, !simulation.covergroup_handle<@cg>
    return
  }
}

// -----

module {
  simulation.covergroup.decl @cg schema 1
  func.func @item_name(%ctx: !simulation.context, %handle: !simulation.covergroup_handle<@cg>) {
    // expected-error @+1 {{requires a comment or group-name option kind}}
    %value = simulation.covergroup.get_string_option %ctx, %handle item 2 option name : !simulation.context, !simulation.covergroup_handle<@cg>
    return
  }
}

// -----

module {
  simulation.covergroup.decl @cg schema 1
  func.func @item_per_instance(%ctx: !simulation.context, %handle: !simulation.covergroup_handle<@cg>) {
    // expected-error @+1 {{requires an integral instance-option kind}}
    %value = simulation.covergroup.get_integer_option %ctx, %handle item 2 option per_instance : !simulation.context, !simulation.covergroup_handle<@cg>
    return
  }
}

// -----

module {
  simulation.covergroup.decl @cg schema 1
  func.func @item_get_inst_coverage(%ctx: !simulation.context, %handle: !simulation.covergroup_handle<@cg>) {
    // expected-error @+1 {{requires an integral instance-option kind}}
    %value = simulation.covergroup.get_integer_option %ctx, %handle item 2 option get_inst_coverage : !simulation.context, !simulation.covergroup_handle<@cg>
    return
  }
}

// -----

module {
  simulation.covergroup.decl @cg schema 1
  func.func @readonly_write(%ctx: !simulation.context, %handle: !simulation.covergroup_handle<@cg>, %value: i64) {
    // expected-error @+1 {{requires an integral option kind}}
    simulation.covergroup.set_integer_option %ctx, %handle item 0 option detect_overlap value %value : !simulation.context, !simulation.covergroup_handle<@cg>
    return
  }
}

// -----

module {
  func.func @readonly_type_write(%ctx: !simulation.context, %value: i64) {
    // expected-error @+1 {{requires a valid integral type-option owner and kind}}
    simulation.covergroup.set_type_integer_option %ctx type 1 item 0 option cross_retain_auto_bins value %value : !simulation.context
    return
  }
}

// -----

module {
  simulation.covergroup.decl @cg schema 1
  func.func @string_integer(%ctx: !simulation.context, %handle: !simulation.covergroup_handle<@cg>) {
    // expected-error @+1 {{requires an integral instance-option kind}}
    %value = simulation.covergroup.get_integer_option %ctx, %handle item 0 option name : !simulation.context, !simulation.covergroup_handle<@cg>
    return
  }
}
