// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

module {
  obelisk_sim.design @zero_schema {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{covergroup schema type ID must be positive}}
    obelisk_sim.covergroup.decl @cg schema 0
  }
}

// -----

module {
  obelisk_sim.design @duplicate_schema {
    obelisk_sim.scope.decl 0
    obelisk_sim.covergroup.decl @first schema 1
    // expected-error @+1 {{duplicate covergroup schema type ID 1}}
    obelisk_sim.covergroup.decl @second schema 1
  }
}

// -----

module {
  obelisk_sim.design @unknown_create {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      // expected-error @+1 {{references an unknown covergroup declaration}}
      %handle = obelisk_sim.covergroup.create %ctx from @missing
        payloads [] argument_count 0 formal_ids [] expression_ids []
        : () ->
          !obelisk_sim.covergroup_handle<@missing>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_create_type {
    obelisk_sim.scope.decl 0
    obelisk_sim.covergroup.decl @selected schema 1
    obelisk_sim.covergroup.decl @wrong schema 2
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      // expected-error @+1 {{result type must name the selected declaration}}
      %handle = obelisk_sim.covergroup.create %ctx from @selected
        payloads [] argument_count 0 formal_ids [] expression_ids []
        : () ->
          !obelisk_sim.covergroup_handle<@wrong>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @unknown_type_query {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      // expected-error @+2 {{references an unknown covergroup declaration}}
      %percentage, %covered, %total =
        obelisk_sim.covergroup.type_query %ctx from @missing item 0
        : !obelisk_sim.context -> (f64, i32, i32)
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @unknown_sample {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "unknown_sample.bad"
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@missing>
          {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 1 : i8
      // expected-error @+1 {{handle type references an unknown covergroup declaration}}
      obelisk_sim.covergroup.sample %ctx, %handle values [%value] ids [1]
        : (!obelisk_sim.context,
           !obelisk_sim.covergroup_handle<@missing>, i8) -> ()
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @sample_count {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "sample_count.bad"
    obelisk_sim.covergroup.decl @cg schema 1
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
          {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 1 : i8
      // expected-error @+1 {{requires one FunctionalExpression ID for each sample value; got 2 IDs and 1 values}}
      obelisk_sim.covergroup.sample %ctx, %handle values [%value] ids [1, 2]
        : (!obelisk_sim.context,
           !obelisk_sim.covergroup_handle<@cg>, i8) -> ()
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @sample_zero_id {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "sample_zero_id.bad"
    obelisk_sim.covergroup.decl @cg schema 1
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
          {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 1 : i8
      // expected-error @+1 {{FunctionalExpression ID 0 must be a positive signed 64-bit value}}
      obelisk_sim.covergroup.sample %ctx, %handle values [%value] ids [0]
        : (!obelisk_sim.context,
           !obelisk_sim.covergroup_handle<@cg>, i8) -> ()
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @sample_duplicate_id {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "sample_duplicate_id.bad"
    obelisk_sim.covergroup.decl @cg schema 1
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
          {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %first = arith.constant 1 : i8
      %second = arith.constant 2 : i8
      // expected-error @+1 {{FunctionalExpression IDs must be unique; ID 7 is repeated}}
      obelisk_sim.covergroup.sample %ctx, %handle
          values [%first, %second] ids [7, 7]
        : (!obelisk_sim.context,
           !obelisk_sim.covergroup_handle<@cg>, i8, i8) -> ()
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @sample_bad_type {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "sample_bad_type.bad"
    obelisk_sim.covergroup.decl @cg schema 1
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
          {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %raw = arith.constant 1 : i64
      %value = arith.index_cast %raw : i64 to index
      // expected-error @+1 {{operand #2 must be variadic of signless integer or fixed-width exact four-state packed value or nullable immutable managed SystemVerilog string or 64-bit float, but got 'index'}}
      obelisk_sim.covergroup.sample %ctx, %handle values [%value] ids [9]
        : (!obelisk_sim.context,
           !obelisk_sim.covergroup_handle<@cg>, index) -> ()
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @zero_control_instance {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %control = arith.constant 3 : i32
      %metric = arith.constant 23 : i32
      %scope = arith.constant 11 : i32
      // expected-error @+1 {{coverage scope ID must be nonzero}}
      %status = obelisk_sim.coverage.control_instance %ctx
        control %control metric %metric scope %scope instance 0
        : !obelisk_sim.context
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @zero_query_instance {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %metric = arith.constant 22 : i32
      %scope = arith.constant 10 : i32
      // expected-error @+1 {{coverage scope ID must be nonzero}}
      %value = obelisk_sim.coverage.query_instance %ctx
        metric %metric scope %scope instance 0 maximum false
        : !obelisk_sim.context
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @unknown_control_instance {
    obelisk_sim.scope.decl 0 hierarchy "$root" coverage_id 42
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %control = arith.constant 3 : i32
      %metric = arith.constant 23 : i32
      %scope = arith.constant 11 : i32
      // expected-error @+1 {{references unknown coverage scope ID 99}}
      %status = obelisk_sim.coverage.control_instance %ctx
        control %control metric %metric scope %scope instance 99
        : !obelisk_sim.context
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @unknown_covergroup_base {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{references an unknown base covergroup declaration}}
    obelisk_sim.covergroup.decl @derived schema 1 base @missing
  }
}

// -----

module {
  obelisk_sim.design @unrelated_covergroup_cast {
    obelisk_sim.scope.decl 0
    obelisk_sim.covergroup.decl @left schema 1
    obelisk_sim.covergroup.decl @right schema 2
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %null = obelisk_sim.covergroup.null
        : !obelisk_sim.covergroup_handle<@left>
      // expected-error @+1 {{cannot cast between unrelated covergroup types}}
      %cast = obelisk_sim.covergroup.cast %null
        : !obelisk_sim.covergroup_handle<@left> to
          !obelisk_sim.covergroup_handle<@right>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @integer_comment_option {
    obelisk_sim.scope.decl 0
    obelisk_sim.covergroup.decl @cg schema 1
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %handle = obelisk_sim.covergroup.null
        : !obelisk_sim.covergroup_handle<@cg>
      %value = arith.constant 1 : i64
      // expected-error @+1 {{requires an integral option kind}}
      obelisk_sim.covergroup.set_integer_option %ctx, %handle
        item 0 option comment value %value
        : !obelisk_sim.context, !obelisk_sim.covergroup_handle<@cg>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @instance_merge_option {
    obelisk_sim.scope.decl 0
    obelisk_sim.covergroup.decl @cg schema 1
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %handle = obelisk_sim.covergroup.null
        : !obelisk_sim.covergroup_handle<@cg>
      %value = arith.constant 1 : i64
      // expected-error @+1 {{requires an integral option kind}}
      obelisk_sim.covergroup.set_integer_option %ctx, %handle
        item 0 option merge_instances value %value
        : !obelisk_sim.context, !obelisk_sim.covergroup_handle<@cg>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @string_goal_option {
    obelisk_sim.scope.decl 0
    obelisk_sim.covergroup.decl @cg schema 1
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %handle = obelisk_sim.covergroup.null
        : !obelisk_sim.covergroup_handle<@cg>
      %value = obelisk_sim.string.literal "bad"
      // expected-error @+1 {{requires the comment option kind}}
      obelisk_sim.covergroup.set_string_option %ctx, %handle
        item 0 option goal value %value
        : !obelisk_sim.context, !obelisk_sim.covergroup_handle<@cg>
      obelisk_sim.return
    }
  }
}
