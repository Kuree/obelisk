// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

module {
  obelisk_sim.design @duplicate {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design
    // expected-error @+1 {{duplicate storage ID 0}}
    obelisk_sim.storage.decl 0 in 0 : i8 design
  }
}

// -----

module {
  // Only time-controlled statements are illegal in a SystemVerilog function.
  obelisk_sim.design @delay_function {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32} {
      %delay = obelisk_sim.time.constant 1
      // expected-error @+1 {{is not permitted in a zero-time function entry}}
      obelisk_sim.suspend.delay %delay to ^done
    ^done:
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_summary {
    obelisk_sim.scope.decl 0
    // expected-error @below {{attribute 'effect_summary' failed to satisfy constraint: compute effect array}}
    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, effect_summary = [0 : i32]} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  // expected-error @+1 {{scope IDs must be dense from zero; missing 1}}
  obelisk_sim.design @sparse {
    obelisk_sim.scope.decl 0
    obelisk_sim.scope.decl 2 parent 0
  }
}

// -----

module {
  obelisk_sim.design @cyclic_scopes {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{parent scope ID must precede the child scope ID}}
    obelisk_sim.scope.decl 1 parent 2
    obelisk_sim.scope.decl 2 parent 1
  }
}

// -----

module {
  // expected-error @+1 {{time precision must be a positive femtosecond value}}
  obelisk_sim.design @bad_time_precision attributes {time_precision_fs = 0 : i64} {
    obelisk_sim.scope.decl 0
  }
}

// -----

module {
  obelisk_sim.design @bad_capture {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{requires one argument metadata dictionary per argument}}
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context) attributes {entry_kind = 1 : i32} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_call {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @callee(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %value: i8 {obelisk_sim.capture_kind = 1 : i32}) -> i8 attributes {entry_kind = 8 : i32} {
      obelisk_sim.return %value : i8
    }
    obelisk_sim.func @caller(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      // expected-error @+1 {{operand and result types must match callee signature}}
      obelisk_sim.call @callee(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_width {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      // expected-error @+1 {{value and unknown planes must match result width}}
      %bad = obelisk_sim.logic.constant 0 : i8, 0 : i4 : !obelisk_sim.logic<8>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_shift_amount {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %value = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %amount = obelisk_sim.time.constant 1
      // expected-error @+1 {{shift amount must be an integer or four-state logic}}
      %shifted = obelisk_sim.logic.shift left %value by %amount : (!obelisk_sim.logic<8>, !obelisk_sim.time) -> !obelisk_sim.logic<8>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_dynamic_index {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %value = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %index = obelisk_sim.time.constant 1
      // expected-error @+1 {{index must be a signless builtin integer or four-state logic}}
      %bad = obelisk_sim.logic.dyn_extract %value from %index : (!obelisk_sim.logic<8>, !obelisk_sim.time) -> !obelisk_sim.logic<1>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_signed_dynamic_index {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      obelisk_sim.return
    ^invalid(%index: si32):
      %value = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      // expected-error @+1 {{builtin integer index must be signless}}
      %bad = obelisk_sim.logic.dyn_extract %value from %index : (!obelisk_sim.logic<8>, si32) -> !obelisk_sim.logic<1>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_conversion_domain {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %value = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      // expected-error @+1 {{result must be a signless builtin integer}}
      %bad = obelisk_sim.logic.to_bits %value : !obelisk_sim.logic<8> -> si8
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_dynamic_width {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %value = arith.constant 0 : i4
      %index = arith.constant 0 : i32
      // expected-error @+1 {{result width exceeds input width}}
      %bad = obelisk_sim.bits.dyn_extract %value from %index : (i4, i32) -> i8
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_dynamic_domain {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %value = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %ref = obelisk_sim.ref.alloc %value : !obelisk_sim.logic<8> -> !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %index = arith.constant 0 : i32
      // expected-error @+1 {{input and result element types must use the same state domain}}
      %bad = obelisk_sim.ref.dyn_extract %ref from %index : (!obelisk_sim.ref<!obelisk_sim.logic<8>>, i32) -> !obelisk_sim.ref<i4>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_logical_not_width {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %value = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      // expected-error @+1 {{logical negation must produce !obelisk_sim.logic<1>}}
      %bad = obelisk_sim.logic.unary logical_not %value : (!obelisk_sim.logic<8>) -> !obelisk_sim.logic<8>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_selection_width {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %value = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      // expected-error @+1 {{constant selection is outside the input width}}
      %part = obelisk_sim.logic.extract %value from 6 : !obelisk_sim.logic<8> -> !obelisk_sim.logic<4>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_continuation {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i8>
      %value = arith.constant 0 : i8
      // expected-error @+1 {{type mismatch for bb argument #0 of successor #0}}
      obelisk_sim.suspend.change %ref to ^next(%value : i8) : !obelisk_sim.ref<i8>
    ^next(%wrong: i16):
      obelisk_sim.return
    }
  }
}

// -----

module {
  // expected-error @+1 {{Operations with a 'SymbolTable' must have exactly one block}}
  "obelisk_sim.design"() ({
  }) {sym_name = "empty"} : () -> ()
}

// -----

module {
  obelisk_sim.design @unknown_lookup {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      // expected-error @+1 {{references an unknown or incompatible storage descriptor}}
      %ref = obelisk_sim.context.storage %ctx[7] : !obelisk_sim.ref<i8>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @capture_descriptor_mismatch {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design
    // expected-error @+1 {{argument #1 has an incompatible capture descriptor}}
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %capture: !obelisk_sim.ref<i16> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @blocking_function {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i8 attributes {entry_kind = 8 : i32} {
      %delay = obelisk_sim.time.constant 1
      // expected-error @+1 {{is not permitted in a zero-time function entry}}
      obelisk_sim.suspend.delay %delay to ^next
    ^next:
      %zero = arith.constant 0 : i8
      obelisk_sim.return %zero : i8
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_any {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i8>
      // expected-error @+1 {{edge inventory exceeds the operand inventory}}
      obelisk_sim.suspend.any %ref edges [0, 1] to ^next : !obelisk_sim.ref<i8>
    ^next:
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @cross_isolation {
    obelisk_sim.scope.decl 0
    %outside = arith.constant 0 : i8
    // expected-note @+1 {{required by region isolation constraints}}
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i8 attributes {entry_kind = 8 : i32} {
      // expected-error @+1 {{using value defined outside the region}}
      obelisk_sim.return %outside : i8
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_driver {
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : i8 design
    // expected-error @+1 {{references an incompatible scope or net descriptor}}
    obelisk_sim.driver.decl 0 in 0 drives 0 : i16 design
  }
}

// -----

module {
  // expected-error @+1 {{design must contain a root scope descriptor}}
  obelisk_sim.design @no_root {
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @two_roots {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{design must contain exactly one root scope}}
    obelisk_sim.scope.decl 1
  }
}

// -----

module {
  obelisk_sim.design @self_parent {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{scope cannot be its own parent}}
    obelisk_sim.scope.decl 1 parent 1
  }
}

// -----

module {
  obelisk_sim.design @unknown_storage_scope {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{references an unknown scope ID}}
    obelisk_sim.storage.decl 0 in 4 : i8 design
  }
}

// -----

module {
  obelisk_sim.design @unknown_net_scope {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{references an unknown scope ID}}
    obelisk_sim.net.decl 0 in 4 : i8 design
  }
}

// -----

module {
  obelisk_sim.design @unknown_callee {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @caller(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      // expected-error @+1 {{callee must name a sibling function entry}}
      obelisk_sim.call @missing(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @call_targets_process {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @process(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      obelisk_sim.return
    }
    obelisk_sim.func @caller(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      // expected-error @+1 {{callee must name a sibling function entry}}
      obelisk_sim.call @process(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @spawn_targets_function {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @callee(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i8 attributes {entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i8
      obelisk_sim.return %zero : i8
    }
    obelisk_sim.func @caller(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      // expected-error @+1 {{callee must name a sibling process entry}}
      %process = obelisk_sim.spawn @callee(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @spawn_signature {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @process(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %value: i8 {obelisk_sim.capture_kind = 2 : i32}) attributes {entry_kind = 1 : i32} {
      obelisk_sim.return
    }
    obelisk_sim.func @caller(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      // expected-error @+1 {{operands must match the void callee signature}}
      %process = obelisk_sim.spawn @process(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @missing_context {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{first argument must be !obelisk_sim.context}}
    obelisk_sim.func @bad(%value: i8 {obelisk_sim.capture_kind = 2 : i32}) attributes {entry_kind = 1 : i32} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @process_returns_value {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{process and root entries must not return values}}
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i8 attributes {entry_kind = 1 : i32} {
      %zero = arith.constant 0 : i8
      obelisk_sim.return %zero : i8
    }
  }
}

// -----

module {
  obelisk_sim.design @root_takes_captures {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design
    // expected-error @+1 {{root initializer accepts only the context argument}}
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 0 : i32} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @context_capture_metadata {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{argument #1 cannot have context capture metadata}}
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %value: i8 {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @spurious_descriptor {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{argument #1 must not have descriptor metadata}}
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %value: i8 {obelisk_sim.capture_kind = 2 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @missing_descriptor {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design
    // expected-error @+1 {{argument #1 requires obelisk_sim.descriptor_id metadata}}
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 3 : i32}) attributes {entry_kind = 1 : i32} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @return_type_mismatch {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i8 attributes {entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i16
      // expected-error @+1 {{operand types must match the enclosing function results}}
      obelisk_sim.return %zero : i16
    }
  }
}

// -----

module {
  obelisk_sim.design @watched_is_value {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %value = arith.constant 0 : i8
      // expected-error @+1 {{watched value must be a ref or net handle}}
      obelisk_sim.suspend.change %value to ^next : i8
    ^next:
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @any_needs_edges {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i8>
      // expected-error @+1 {{requires at least one watched handle}}
      obelisk_sim.suspend.any %ref edges [] to ^next : !obelisk_sim.ref<i8>
    ^next(%resumed: !obelisk_sim.ref<i8>):
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @any_bad_edge {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i8>
      // expected-error @+1 {{contains an invalid edge kind}}
      obelisk_sim.suspend.any %ref edges [9] to ^next : !obelisk_sim.ref<i8>
    ^next:
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @alloc_mismatch {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %value = arith.constant 0 : i8
      // expected-error @+1 {{initial value must match allocated element type}}
      %local = obelisk_sim.ref.alloc %value : i8 -> !obelisk_sim.ref<i16>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @concat_width {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %value = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      // expected-error @+1 {{result width must equal the sum of input widths}}
      %bad = obelisk_sim.logic.concat %value, %value : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<8>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @replicate_width {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %value = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      // expected-error @+1 {{result width must equal input width times count}}
      %bad = obelisk_sim.logic.replicate %value times 3 : !obelisk_sim.logic<8> -> !obelisk_sim.logic<16>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @replicate_width_overflow {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %value = obelisk_sim.logic.constant 0 : i4, 0 : i4 : !obelisk_sim.logic<4>
      // expected-error @+1 {{replication width overflows uint64_t}}
      %bad = obelisk_sim.logic.replicate %value times 4611686018427387905 : !obelisk_sim.logic<4> -> !obelisk_sim.logic<4>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @insert_range {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %value = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %part = obelisk_sim.logic.constant 0 : i4, 0 : i4 : !obelisk_sim.logic<4>
      // expected-error @+1 {{replacement is outside the input width}}
      %bad = obelisk_sim.logic.insert %part into %value at 6 : (!obelisk_sim.logic<8>, !obelisk_sim.logic<4>) -> !obelisk_sim.logic<8>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @case_compare_result {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %value = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      // expected-error @+1 {{case comparisons must produce i1}}
      %bad = obelisk_sim.logic.compare case_eq %value, %value : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<1>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @negative_time {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      // expected-error @+1 {{simulation time must be nonnegative}}
      %bad = obelisk_sim.time.constant -1
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @ref_extract_range {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i8>
      // expected-error @+1 {{constant selection is outside the input element width}}
      %bad = obelisk_sim.ref.extract %ref from 6 : !obelisk_sim.ref<i8> -> !obelisk_sim.ref<i4>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_time_scale {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %value = arith.constant 1 : i8
      // expected-error @+1 {{tick scale must be positive}}
      %bad = obelisk_sim.time.scale %value by 0 signed = false : i8
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_join {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @child(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      obelisk_sim.return
    }
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %process = obelisk_sim.spawn @child(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      // expected-error @+1 {{process count exceeds the operand inventory}}
      obelisk_sim.suspend.join all %process processes 2 to ^next : !obelisk_sim.process
    ^next:
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_display_radix {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %fd = arith.constant 1 : i32
      %value = arith.constant 0 : i8
      // expected-error @+1 {{default radix must be 2, 8, 10, or 16}}
      obelisk_sim.display %ctx to %fd(%value) newline = true radix = 3 flags = [0] : i8
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_display_flags {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %fd = arith.constant 1 : i32
      %value = arith.constant 0 : i8
      // expected-error @+1 {{requires one flag entry per display item}}
      obelisk_sim.display %ctx to %fd(%value) newline = false radix = 10 flags = [] : i8
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @unknown_display_flag {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %fd = arith.constant 1 : i32
      %value = arith.constant 0 : i8
      // expected-error @+1 {{display item flags contain an unknown bit}}
      obelisk_sim.display %ctx to %fd(%value) newline = false radix = 10 flags = [4] : i8
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @signed_literal_display {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %fd = arith.constant 1 : i32
      %text = obelisk_sim.bytes.constant "text"
      // expected-error @+1 {{literal byte items cannot be signed}}
      obelisk_sim.display %ctx to %fd(%text) newline = false radix = 10 flags = [1] : !obelisk_sim.bytes
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @unsupported_display_item {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %fd = arith.constant 1 : i32
      %value = arith.constant 0.0 : f32
      // expected-error @+1 {{items must be literal bytes or packed integers}}
      obelisk_sim.display %ctx to %fd(%value) newline = false radix = 10 flags = [0] : f32
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_display_time_multiplier {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32} {
      %fd = arith.constant 1 : i32
      %value = arith.constant 0 : i8
      // expected-error @+1 {{time multiplier must be positive}}
      obelisk_sim.display %ctx to %fd(%value) newline = false radix = 10 flags = [0] {time_multiplier = 0 : i64} : i8
      obelisk_sim.return
    }
  }
}
