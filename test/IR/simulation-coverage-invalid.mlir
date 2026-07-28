// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

module {
  obelisk_sim.design @empty {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{requires at least one coverpoint}}
    obelisk_sim.covergroup.decl @cg id 1 bins []
  }
}

// -----

module {
  obelisk_sim.design @zero_id {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{covergroup ID must be positive}}
    obelisk_sim.covergroup.decl @cg id 0 bins [1]
  }
}

// -----

module {
  obelisk_sim.design @zero_bins {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{every coverpoint requires a positive 32-bit named-bin count}}
    obelisk_sim.covergroup.decl @cg id 1 bins [2, 0]
  }
}

// -----

module {
  obelisk_sim.design @duplicate_ids {
    obelisk_sim.scope.decl 0
    obelisk_sim.covergroup.decl @first id 1 bins [1]
    // expected-error @+1 {{duplicate covergroup ID 1}}
    obelisk_sim.covergroup.decl @second id 1 bins [2]
  }
}

// -----

module {
  obelisk_sim.design @bad_create {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function
      hierarchy "bad_create.bad"
    obelisk_sim.covergroup.decl @selected id 1 bins [1]
    obelisk_sim.covergroup.decl @wrong id 2 bins [1]
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      // expected-error @+1 {{result type must name the selected declaration}}
      %handle = obelisk_sim.covergroup.create %ctx from @selected
        : !obelisk_sim.context ->
          !obelisk_sim.covergroup_handle<@wrong>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_nested_create_type {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function
      hierarchy "bad_nested_create_type.bad"
    obelisk_sim.covergroup.decl @selected id 1 bins [1]
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      // expected-error @+1 {{result type must name the selected declaration}}
      %handle = obelisk_sim.covergroup.create %ctx from @selected
        : !obelisk_sim.context ->
          !obelisk_sim.covergroup_handle<@selected::@bogus>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_sample_inventory {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function
      hierarchy "bad_sample_inventory.bad"
    obelisk_sim.covergroup.decl @cg id 1 bins [2, 1]
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
          {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %hit = arith.constant true
      // expected-error @+1 {{requires exactly 3 flattened bin-hit operands}}
      obelisk_sim.covergroup.sample %ctx, %handle[%hit, %hit]
        : !obelisk_sim.context, !obelisk_sim.covergroup_handle<@cg>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_coverpoint {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function
      hierarchy "bad_coverpoint.bad"
    obelisk_sim.covergroup.decl @cg id 1 bins [2]
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
          {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      // expected-error @+1 {{coverpoint index is outside the declaration}}
      obelisk_sim.covergroup.bin_hit %ctx, %handle[1, 0]
        : !obelisk_sim.context, !obelisk_sim.covergroup_handle<@cg>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_bin {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function
      hierarchy "bad_bin.bad"
    obelisk_sim.covergroup.decl @cg id 1 bins [2]
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
          {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      // expected-error @+1 {{bin index is outside the selected coverpoint}}
      obelisk_sim.covergroup.bin_hit %ctx, %handle[0, 2]
        : !obelisk_sim.context, !obelisk_sim.covergroup_handle<@cg>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_type_query {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function
      hierarchy "bad_type_query.bad"
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      // expected-error @+2 {{references an unknown covergroup declaration}}
      %percentage, %covered, %total =
        obelisk_sim.covergroup.type_query %ctx from @missing
        : !obelisk_sim.context -> (f64, i32, i32)
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @unknown_null_declaration {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function
      hierarchy "unknown_null_declaration.bad"
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      // expected-error @+1 {{handle type references an unknown covergroup declaration}}
      %null = obelisk_sim.covergroup.null
        : !obelisk_sim.covergroup_handle<@missing>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @unknown_sample_declaration {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function
      hierarchy "unknown_sample_declaration.bad"
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@missing>
          {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      // expected-error @+1 {{handle type references an unknown covergroup declaration}}
      %enabled = obelisk_sim.covergroup.sample_enabled %ctx, %handle
        : (!obelisk_sim.context,
           !obelisk_sim.covergroup_handle<@missing>) -> i1
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @unknown_stop_declaration {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function
      hierarchy "unknown_stop_declaration.bad"
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@missing>
          {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      // expected-error @+1 {{handle type references an unknown covergroup declaration}}
      obelisk_sim.covergroup.stop %ctx, %handle
        : !obelisk_sim.context, !obelisk_sim.covergroup_handle<@missing>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @unknown_start_declaration {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function
      hierarchy "unknown_start_declaration.bad"
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@missing>
          {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      // expected-error @+1 {{handle type references an unknown covergroup declaration}}
      obelisk_sim.covergroup.start %ctx, %handle
        : !obelisk_sim.context, !obelisk_sim.covergroup_handle<@missing>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @unknown_instance_query_declaration {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function
      hierarchy "unknown_instance_query_declaration.bad"
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@missing>
          {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      // expected-error @+2 {{handle type references an unknown covergroup declaration}}
      %percentage, %covered, %total =
        obelisk_sim.covergroup.instance_query %ctx, %handle
        : (!obelisk_sim.context,
           !obelisk_sim.covergroup_handle<@missing>) -> (f64, i32, i32)
      obelisk_sim.return
    }
  }
}
