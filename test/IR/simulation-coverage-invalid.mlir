// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

module {
  simulation.design @zero_schema {
    simulation.scope.decl 0
    // expected-error @+1 {{covergroup schema type ID must be positive}}
    simulation.covergroup.decl @cg schema 0
  }
}

// -----

module {
  simulation.design @duplicate_schema {
    simulation.scope.decl 0
    simulation.covergroup.decl @first schema 1
    // expected-error @+1 {{duplicate covergroup schema type ID 1}}
    simulation.covergroup.decl @second schema 1
  }
}

// -----

module {
  simulation.design @unknown_create {
    simulation.scope.decl 0
    simulation.func @bad(
        %ctx: !simulation.context
          {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      // expected-error @+1 {{references an unknown covergroup declaration}}
      %handle = simulation.covergroup.create %ctx from @missing
        payloads [] argument_count 0 formal_ids [] expression_ids []
        : () ->
          !simulation.covergroup_handle<@missing>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_create_type {
    simulation.scope.decl 0
    simulation.covergroup.decl @selected schema 1
    simulation.covergroup.decl @wrong schema 2
    simulation.func @bad(
        %ctx: !simulation.context
          {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      // expected-error @+1 {{result type must name the selected declaration}}
      %handle = simulation.covergroup.create %ctx from @selected
        payloads [] argument_count 0 formal_ids [] expression_ids []
        : () ->
          !simulation.covergroup_handle<@wrong>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @unknown_type_query {
    simulation.scope.decl 0
    simulation.func @bad(
        %ctx: !simulation.context
          {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      // expected-error @+2 {{references an unknown covergroup declaration}}
      %percentage, %covered, %total =
        simulation.covergroup.type_query %ctx from @missing item 0
        : !simulation.context -> (f64, i32, i32)
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @unknown_sample {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "unknown_sample.bad"
    simulation.func @bad(
        %ctx: !simulation.context
          {simulation.capture_kind = 0 : i32},
        %handle: !simulation.covergroup_handle<@missing>
          {simulation.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 1 : i8
      // expected-error @+1 {{handle type references an unknown covergroup declaration}}
      simulation.covergroup.sample %ctx, %handle values [%value] ids [1]
        : (!simulation.context,
           !simulation.covergroup_handle<@missing>, i8) -> ()
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @sample_count {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "sample_count.bad"
    simulation.covergroup.decl @cg schema 1
    simulation.func @bad(
        %ctx: !simulation.context
          {simulation.capture_kind = 0 : i32},
        %handle: !simulation.covergroup_handle<@cg>
          {simulation.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 1 : i8
      // expected-error @+1 {{requires one FunctionalExpression ID for each sample value; got 2 IDs and 1 values}}
      simulation.covergroup.sample %ctx, %handle values [%value] ids [1, 2]
        : (!simulation.context,
           !simulation.covergroup_handle<@cg>, i8) -> ()
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @sample_zero_id {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "sample_zero_id.bad"
    simulation.covergroup.decl @cg schema 1
    simulation.func @bad(
        %ctx: !simulation.context
          {simulation.capture_kind = 0 : i32},
        %handle: !simulation.covergroup_handle<@cg>
          {simulation.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 1 : i8
      // expected-error @+1 {{FunctionalExpression ID 0 must be a positive signed 64-bit value}}
      simulation.covergroup.sample %ctx, %handle values [%value] ids [0]
        : (!simulation.context,
           !simulation.covergroup_handle<@cg>, i8) -> ()
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @sample_duplicate_id {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "sample_duplicate_id.bad"
    simulation.covergroup.decl @cg schema 1
    simulation.func @bad(
        %ctx: !simulation.context
          {simulation.capture_kind = 0 : i32},
        %handle: !simulation.covergroup_handle<@cg>
          {simulation.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %first = arith.constant 1 : i8
      %second = arith.constant 2 : i8
      // expected-error @+1 {{FunctionalExpression IDs must be unique; ID 7 is repeated}}
      simulation.covergroup.sample %ctx, %handle
          values [%first, %second] ids [7, 7]
        : (!simulation.context,
           !simulation.covergroup_handle<@cg>, i8, i8) -> ()
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @sample_bad_type {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "sample_bad_type.bad"
    simulation.covergroup.decl @cg schema 1
    simulation.func @bad(
        %ctx: !simulation.context
          {simulation.capture_kind = 0 : i32},
        %handle: !simulation.covergroup_handle<@cg>
          {simulation.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %raw = arith.constant 1 : i64
      %value = arith.index_cast %raw : i64 to index
      // expected-error @+1 {{operand #2 must be variadic of signless integer or fixed-width exact four-state packed value or nullable immutable managed SystemVerilog string or 64-bit float, but got 'index'}}
      simulation.covergroup.sample %ctx, %handle values [%value] ids [9]
        : (!simulation.context,
           !simulation.covergroup_handle<@cg>, index) -> ()
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @zero_control_instance {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.func @bad(
        %ctx: !simulation.context
          {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %control = arith.constant 3 : i32
      %metric = arith.constant 23 : i32
      %scope = arith.constant 11 : i32
      // expected-error @+1 {{coverage scope ID must be nonzero}}
      %status = simulation.coverage.control_instance %ctx
        control %control metric %metric scope %scope instance 0
        : !simulation.context
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @zero_query_instance {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.func @bad(
        %ctx: !simulation.context
          {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %metric = arith.constant 22 : i32
      %scope = arith.constant 10 : i32
      // expected-error @+1 {{coverage scope ID must be nonzero}}
      %value = simulation.coverage.query_instance %ctx
        metric %metric scope %scope instance 0 maximum false
        : !simulation.context
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @unknown_control_instance {
    simulation.scope.decl 0 hierarchy "$root" coverage_id 42
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.func @bad(
        %ctx: !simulation.context
          {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %control = arith.constant 3 : i32
      %metric = arith.constant 23 : i32
      %scope = arith.constant 11 : i32
      // expected-error @+1 {{references unknown coverage scope ID 99}}
      %status = simulation.coverage.control_instance %ctx
        control %control metric %metric scope %scope instance 99
        : !simulation.context
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @unknown_covergroup_base {
    simulation.scope.decl 0
    // expected-error @+1 {{references an unknown base covergroup declaration}}
    simulation.covergroup.decl @derived schema 1 base @missing
  }
}

// -----

module {
  simulation.design @unrelated_covergroup_cast {
    simulation.scope.decl 0
    simulation.covergroup.decl @left schema 1
    simulation.covergroup.decl @right schema 2
    simulation.func @bad(
        %ctx: !simulation.context
          {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %null = simulation.covergroup.null
        : !simulation.covergroup_handle<@left>
      // expected-error @+1 {{cannot cast between unrelated covergroup types}}
      %cast = simulation.covergroup.cast %null
        : !simulation.covergroup_handle<@left> to
          !simulation.covergroup_handle<@right>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @integer_comment_option {
    simulation.scope.decl 0
    simulation.covergroup.decl @cg schema 1
    simulation.func @bad(
        %ctx: !simulation.context
          {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %handle = simulation.covergroup.null
        : !simulation.covergroup_handle<@cg>
      %value = arith.constant 1 : i64
      // expected-error @+1 {{requires an integral option kind}}
      simulation.covergroup.set_integer_option %ctx, %handle
        item 0 option comment value %value
        : !simulation.context, !simulation.covergroup_handle<@cg>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @instance_merge_option {
    simulation.scope.decl 0
    simulation.covergroup.decl @cg schema 1
    simulation.func @bad(
        %ctx: !simulation.context
          {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %handle = simulation.covergroup.null
        : !simulation.covergroup_handle<@cg>
      %value = arith.constant 1 : i64
      // expected-error @+1 {{requires an integral option kind}}
      simulation.covergroup.set_integer_option %ctx, %handle
        item 0 option merge_instances value %value
        : !simulation.context, !simulation.covergroup_handle<@cg>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @string_goal_option {
    simulation.scope.decl 0
    simulation.covergroup.decl @cg schema 1
    simulation.func @bad(
        %ctx: !simulation.context
          {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %handle = simulation.covergroup.null
        : !simulation.covergroup_handle<@cg>
      %value = simulation.string.literal "bad"
      // expected-error @+1 {{requires the comment option kind}}
      simulation.covergroup.set_string_option %ctx, %handle
        item 0 option goal value %value
        : !simulation.context, !simulation.covergroup_handle<@cg>
      simulation.return
    }
  }
}
