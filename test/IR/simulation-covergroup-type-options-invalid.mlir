// RUN: talos-opt %s -split-input-file -verify-diagnostics

module {
  func.func @item_distribution(%ctx: !simulation.context, %value: i64) {
    // expected-error @+1 {{requires a valid integral type-option owner and kind}}
    simulation.covergroup.set_type_integer_option %ctx type 1 item 2 option distribute_first value %value : !simulation.context
    return
  }
}

// -----

module {
  simulation.covergroup.decl @cg schema 1
  func.func @instance_distribution(%ctx: !simulation.context, %handle: !simulation.covergroup_handle<@cg>, %value: i64) {
    // expected-error @+1 {{requires an integral option kind}}
    simulation.covergroup.set_integer_option %ctx, %handle item 0 option distribute_first value %value : !simulation.context, !simulation.covergroup_handle<@cg>
    return
  }
}

// -----

module {
  simulation.covergroup.decl @cg schema 1
  func.func @instance_read(%ctx: !simulation.context, %handle: !simulation.covergroup_handle<@cg>) {
    // expected-error @+1 {{requires an integral instance-option kind}}
    %value = simulation.covergroup.get_integer_option %ctx, %handle item 0 option distribute_first : !simulation.context, !simulation.covergroup_handle<@cg>
    return
  }
}

// -----

module {
  func.func @missing_type(%ctx: !simulation.context, %value: i64) {
    // expected-error @+1 {{requires a valid integral type-option owner and kind}}
    simulation.covergroup.set_type_integer_option %ctx type 0 item 0 option distribute_first value %value : !simulation.context
    return
  }
}
