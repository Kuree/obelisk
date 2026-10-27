// RUN: talos-opt %s -split-input-file -verify-diagnostics

module {
  simulation.covergroup.decl @cg schema 1
  func.func @readonly(%ctx: !simulation.context, %cg: !simulation.covergroup_handle<@cg>, %zero: i64) {
    // expected-error @+1 {{requires a mutable instance option kind and owner}}
    %path = simulation.covergroup.option_reference %ctx, %cg item 0 option auto_bin_max mask %zero index %zero character false : !simulation.context, !simulation.covergroup_handle<@cg> -> !simulation.reference_path<i64>
    return
  }
}

// -----

module {
  simulation.covergroup.decl @cg schema 1
  func.func @item_name(%ctx: !simulation.context, %cg: !simulation.covergroup_handle<@cg>, %zero: i64) {
    // expected-error @+1 {{requires a mutable instance option kind and owner}}
    %path = simulation.covergroup.option_reference %ctx, %cg item 2 option name mask %zero index %zero character false : !simulation.context, !simulation.covergroup_handle<@cg> -> !simulation.reference_path<!simulation.string>
    return
  }
}

// -----

module {
  simulation.covergroup.decl @cg schema 1
  func.func @narrow_integer(%ctx: !simulation.context, %cg: !simulation.covergroup_handle<@cg>, %zero: i64) {
    // expected-error @+1 {{has an incompatible option path element type}}
    %path = simulation.covergroup.option_reference %ctx, %cg item 0 option weight mask %zero index %zero character false : !simulation.context, !simulation.covergroup_handle<@cg> -> !simulation.reference_path<i32>
    return
  }
}

// -----

module {
  simulation.covergroup.decl @cg schema 1
  func.func @numeric_character(%ctx: !simulation.context, %cg: !simulation.covergroup_handle<@cg>, %zero: i64) {
    // expected-error @+1 {{has an incompatible option path element type}}
    %path = simulation.covergroup.option_reference %ctx, %cg item 0 option weight mask %zero index %zero character true : !simulation.context, !simulation.covergroup_handle<@cg> -> !simulation.reference_path<i8>
    return
  }
}

// -----

module {
  func.func @static_storage_width(%ctx: !simulation.context, %ref: !simulation.argument_ref<i64>, %zero: i64) {
    // expected-error @+1 {{has an incompatible static option storage type}}
    %path = simulation.covergroup.type_option_reference %ctx, %ref type 1 item 0 option goal mask %zero index %zero character false : !simulation.context, !simulation.argument_ref<i64> -> !simulation.reference_path<i64>
    return
  }
}

// -----

module {
  func.func @static_owner(%ctx: !simulation.context, %ref: !simulation.argument_ref<i32>, %zero: i64) {
    // expected-error @+1 {{requires a mutable type option kind and owner}}
    %path = simulation.covergroup.type_option_reference %ctx, %ref type 0 item 0 option weight mask %zero index %zero character false : !simulation.context, !simulation.argument_ref<i32> -> !simulation.reference_path<i64>
    return
  }
}

// -----

module {
  func.func @static_scoped_merge(%ctx: !simulation.context, %ref: !simulation.argument_ref<i1>, %zero: i64) {
    // expected-error @+1 {{requires a mutable type option kind and owner}}
    %path = simulation.covergroup.type_option_reference %ctx, %ref type 1 item 2 option merge_instances mask %zero index %zero character false : !simulation.context, !simulation.argument_ref<i1> -> !simulation.reference_path<i64>
    return
  }
}
