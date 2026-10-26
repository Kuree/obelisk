// RUN: obelisk-opt %s -split-input-file -verify-diagnostics -o /dev/null

module {
  simulation.design @zero {
    // expected-error @+1 {{box type ID must be positive}}
    simulation.vpi_box_type.decl @bad id 0 : i32 {vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>}
  }
}

// -----

module {
  // expected-error @+1 {{must be directly contained in a simulation design}}
  simulation.vpi_box_type.decl @bad id 1 : i32 {vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>}
}

// -----

module {
  simulation.design @width {
    // expected-error @+1 {{does not normalize to executable type 'i64'}}
    simulation.vpi_box_type.decl @bad id 1 : i64 {vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>}
  }
}

// -----

module {
  simulation.design @duplicate {
    simulation.vpi_box_type.decl @first id 1 : i32 {vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>}
    // expected-error @+1 {{duplicates a box type ID}}
    simulation.vpi_box_type.decl @second id 1 : i32 {vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>}
  }
}

// -----

module {
  simulation.design @missing {
    simulation.func @f(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %array: !simulation.dynamic_array<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32} {
      // expected-error @+1 {{descriptor must name a VPI box type declaration}}
      simulation.box.bind_type %array to @type : !simulation.dynamic_array<i32>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @wrong {
    simulation.vpi_box_type.decl @type id 1 : i32 {vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>}
    simulation.func @f(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %array: !simulation.dynamic_array<i64> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32} {
      // expected-error @+1 {{input element type must exactly match the descriptor}}
      simulation.box.bind_type %array to @type : !simulation.dynamic_array<i64>
      simulation.return
    }
  }
}
