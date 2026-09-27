// RUN: obelisk-opt %s --split-input-file --verify-diagnostics -o /dev/null

module {
  simulation.design @fixed_and_sparse {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{VPI array cannot be both fixed and sparse}}
    simulation.vpi_object.anchor @array id 1 type 112 in 0 parent @owner
        ordinal 0 hierarchy "pkg.a" debug "a" {
      index_ranges = array<i64: 3, 0>, sparse_indices = array<i64>
    }
  }
}

// -----

module {
  simulation.design @scalar_with_fixed_geometry {
    simulation.scope.decl 0
    // expected-error @+1 {{index_ranges is only valid on a fixed array anchor}}
    simulation.vpi_object.anchor @event id 0 type 34 in 0 ordinal 0
        hierarchy "event" debug "event" {index_ranges = array<i64>}
  }
}

// -----

module {
  simulation.design @fixed_without_geometry {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{fixed VPI array requires nonempty index_ranges}}
    simulation.vpi_object.anchor @array id 1 type 129 in 0 parent @owner
        ordinal 0 hierarchy "pkg.events" debug "events"
  }
}

// -----

module {
  simulation.design @fixed_with_empty_geometry {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{fixed VPI array requires nonempty index_ranges}}
    simulation.vpi_object.anchor @array id 1 type 129 in 0 parent @owner
        ordinal 0 hierarchy "pkg.events" debug "events" {
      index_ranges = array<i64>
    }
  }
}

// -----

module {
  simulation.design @generate_without_geometry {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{generate-scope array requires sparse_indices}}
    simulation.vpi_object.anchor @array id 1 type 133 in 0 parent @owner
        ordinal 0 hierarchy "pkg.g" debug "g"
  }
}

// -----

module {
  simulation.design @empty_generate_array {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @array id 1 type 133 in 0 parent @owner
        ordinal 0 hierarchy "pkg.g" debug "g" {
      sparse_indices = array<i64>
    }
  }
}

// -----

module {
  simulation.design @empty_member_on_scalar_parent {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{member_indices requires a fixed or generate array parent}}
    simulation.vpi_object.anchor @event id 1 type 34 in 0 parent @owner
        ordinal 0 hierarchy "pkg.event" debug "event" {
      member_indices = array<i64>
    }
  }
}

// -----

module {
  simulation.design @member_on_root {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    // expected-error @+1 {{member_indices requires a fixed or generate array parent}}
    simulation.vpi_object.anchor @top id 0 type 32 in 0 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 0 : i64>,
      member_indices = array<i64: 0>
    }
  }
}

// -----

module {
  simulation.design @fixed_child_wrong_rank {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @array id 1 type 129 in 0 parent @owner
        ordinal 0 hierarchy "pkg.events" debug "events" {
      index_ranges = array<i64: 1, 0, 2, 3>
    }
    // expected-error @+1 {{fixed-array child member_indices rank does not match parent}}
    simulation.vpi_object.anchor @event id 2 type 34 in 0 parent @array
        ordinal 0 hierarchy "pkg.events[1][2]" debug "events[1][2]" {
      member_indices = array<i64: 1>
    }
  }
}

// -----

module {
  simulation.design @fixed_child_out_of_range {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @array id 1 type 129 in 0 parent @owner
        ordinal 0 hierarchy "pkg.events" debug "events" {
      index_ranges = array<i64: 1, 0>
    }
    // expected-error @+1 {{fixed-array child member index is outside parent range}}
    simulation.vpi_object.anchor @event id 2 type 34 in 0 parent @array
        ordinal 0 hierarchy "pkg.events[2]" debug "events[2]" {
      member_indices = array<i64: 2>
    }
  }
}

// -----

module {
  simulation.design @generate_child_missing_index {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @array id 1 type 133 in 0 parent @owner
        ordinal 0 hierarchy "pkg.g" debug "g" {
      sparse_indices = array<i64: -1, 4>
    }
    // expected-error @+1 {{generate-array child member index is absent from parent}}
    simulation.vpi_object.anchor @scope id 2 type 134 in 0 parent @array
        ordinal 0 hierarchy "pkg.g[0]" debug "g[0]" {
      member_indices = array<i64: 0>
    }
  }
}

// -----

module {
  simulation.design @duplicate_generate_index {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{sparse_indices contains a duplicate index}}
    simulation.vpi_object.anchor @array id 1 type 133 in 0 parent @owner
        ordinal 0 hierarchy "pkg.g" debug "g" {
      sparse_indices = array<i64: 2, -1, 2>
    }
  }
}

// -----

module {
  simulation.design @fixed_extent_overflow {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{fixed VPI array shape exceeds relation encoding}}
    simulation.vpi_object.anchor @array id 1 type 129 in 0 parent @owner
        ordinal 0 hierarchy "pkg.events" debug "events" {
      index_ranges = array<i64: -9223372036854775808, 9223372036854775807>
    }
  }
}

// -----

module {
  simulation.design @generate_with_ranges {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{VPI array cannot be both fixed and sparse}}
    simulation.vpi_object.anchor @array id 1 type 133 in 0 parent @owner
        ordinal 0 hierarchy "pkg.g" debug "g" {
      index_ranges = array<i64>, sparse_indices = array<i64>
    }
  }
}

// -----

module {
  simulation.design @scalar_with_sparse_geometry {
    simulation.scope.decl 0
    // expected-error @+1 {{sparse_indices is only valid on a generate-scope array anchor}}
    simulation.vpi_object.anchor @event id 0 type 34 in 0 ordinal 0
        hierarchy "event" debug "event" {sparse_indices = array<i64>}
  }
}

// -----

module {
  simulation.design @multidimensional_gate_array {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{primitive VPI array must be one-dimensional}}
    simulation.vpi_object.anchor @array id 1 type 111 in 0 parent @owner
        ordinal 0 hierarchy "pkg.g" debug "g" {
      index_ranges = array<i64: 1, 0, -2, 2>
    }
  }
}

// -----

module {
  simulation.design @array_with_member_geometry {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{array aggregates cannot carry member_indices}}
    simulation.vpi_object.anchor @array id 1 type 112 in 0 parent @owner
        ordinal 0 hierarchy "pkg.a" debug "a" {
      index_ranges = array<i64: 0, 0>, member_indices = array<i64>
    }
  }
}

// -----

module {
  simulation.design @invalid_delegated_vpi_identity {
    simulation.scope.decl 0
    // expected-error @+1 {{delegated VPI identity must name its owning object anchor}}
    simulation.storage.decl 0 in 0 : i8 design hierarchy "value" {
      simulation.vpi_identity_delegated
    }
  }
}

// -----

module {
  simulation.design @fixed_child_missing_indices {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @array id 1 type 129 in 0 parent @owner
        ordinal 0 hierarchy "pkg.events" debug "events" {
      index_ranges = array<i64: 0, 0>
    }
    // expected-error @+1 {{fixed-array child member_indices rank does not match parent}}
    simulation.vpi_object.anchor @event id 2 type 34 in 0 parent @array
        ordinal 0 hierarchy "pkg.events[0]" debug "events[0]"
  }
}

// -----

module {
  simulation.design @duplicate_fixed_coordinate {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @array id 1 type 129 in 0 parent @owner
        ordinal 0 hierarchy "pkg.events" debug "events" {
      index_ranges = array<i64: 0, 0>
    }
    simulation.vpi_object.anchor @event0 id 2 type 34 in 0 parent @array
        ordinal 0 hierarchy "pkg.events[0]" debug "events[0]" {
      member_indices = array<i64: 0>
    }
    // expected-error @+1 {{duplicates a source coordinate in its VPI array parent}}
    simulation.vpi_object.anchor @event1 id 3 type 34 in 0 parent @array
        ordinal 1 hierarchy "pkg.events[0]" debug "events[0]" {
      member_indices = array<i64: 0>
    }
  }
}

// -----

module {
  simulation.design @incomplete_fixed_coordinates {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{does not have exactly one child for every source coordinate}}
    simulation.vpi_object.anchor @array id 1 type 129 in 0 parent @owner
        ordinal 0 hierarchy "pkg.events" debug "events" {
      index_ranges = array<i64: 1, 0>
    }
    simulation.vpi_object.anchor @event id 2 type 34 in 0 parent @array
        ordinal 0 hierarchy "pkg.events[1]" debug "events[1]" {
      member_indices = array<i64: 1>
    }
  }
}

// -----

module {
  simulation.design @delegated_unknown_anchor {
    simulation.scope.decl 0
    // expected-error @+1 {{delegated VPI identity must reference a named-event-array anchor}}
    simulation.storage.decl 0 in 0 :
        !simulation.unpacked_array<1 : 0 x !simulation.event> design
        hierarchy "pkg.events" {
      simulation.vpi_identity_delegated = @missing,
      vpi_type = #simulation.vpi_type<kind = unpacked_array,
          isSigned = false, isFourState = false, range = [1, 0], children = [
            #simulation.vpi_type<kind = event, isSigned = false,
                isFourState = false, range = [], children = [],
                childNames = []>
          ], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @delegated_wrong_anchor_kind {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{delegated VPI identity must reference a named-event-array anchor}}
    simulation.storage.decl 0 in 0 :
        !simulation.unpacked_array<1 : 0 x !simulation.event> design
        hierarchy "pkg.events" {
      simulation.vpi_identity_delegated = @owner,
      vpi_type = #simulation.vpi_type<kind = unpacked_array,
          isSigned = false, isFourState = false, range = [1, 0], children = [
            #simulation.vpi_type<kind = event, isSigned = false,
                isFourState = false, range = [], children = [],
                childNames = []>
          ], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @delegated_name_mismatch {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @events id 1 type 129 in 0 parent @owner
        ordinal 0 hierarchy "pkg.events" debug "events" {
      index_ranges = array<i64: 0, 0>
    }
    simulation.vpi_object.anchor @event id 2 type 34 in 0 parent @events
        ordinal 0 hierarchy "pkg.events[0]" debug "events[0]" {
      member_indices = array<i64: 0>
    }
    // expected-error @+1 {{delegated VPI identity disagrees with its anchor scope or name}}
    simulation.storage.decl 0 in 0 :
        !simulation.unpacked_array<0 : 0 x !simulation.event> design
        hierarchy "pkg.other" {
      simulation.vpi_identity_delegated = @events,
      vpi_type = #simulation.vpi_type<kind = unpacked_array,
          isSigned = false, isFourState = false, range = [0, 0], children = [
            #simulation.vpi_type<kind = event, isSigned = false,
                isFourState = false, range = [], children = [],
                childNames = []>
          ], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @delegated_shape_mismatch {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @events id 1 type 129 in 0 parent @owner
        ordinal 0 hierarchy "pkg.events" debug "events" {
      index_ranges = array<i64: 0, 0>
    }
    simulation.vpi_object.anchor @event id 2 type 34 in 0 parent @events
        ordinal 0 hierarchy "pkg.events[0]" debug "events[0]" {
      member_indices = array<i64: 0>
    }
    // expected-error @+1 {{delegated VPI identity disagrees with its anchor array shape}}
    simulation.storage.decl 0 in 0 :
        !simulation.unpacked_array<2 : 0 x !simulation.event> design
        hierarchy "pkg.events" {
      simulation.vpi_identity_delegated = @events,
      vpi_type = #simulation.vpi_type<kind = unpacked_array,
          isSigned = false, isFourState = false, range = [2, 0], children = [
            #simulation.vpi_type<kind = event, isSigned = false,
                isFourState = false, range = [], children = [],
                childNames = []>
          ], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @duplicate_delegated_identity {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @events id 1 type 129 in 0 parent @owner
        ordinal 0 hierarchy "pkg.events" debug "events" {
      index_ranges = array<i64: 0, 0>
    }
    simulation.vpi_object.anchor @event id 2 type 34 in 0 parent @events
        ordinal 0 hierarchy "pkg.events[0]" debug "events[0]" {
      member_indices = array<i64: 0>
    }
    simulation.storage.decl 0 in 0 :
        !simulation.unpacked_array<0 : 0 x !simulation.event> design
        hierarchy "pkg.events" {
      simulation.vpi_identity_delegated = @events,
      vpi_type = #simulation.vpi_type<kind = unpacked_array,
          isSigned = false, isFourState = false, range = [0, 0], children = [
            #simulation.vpi_type<kind = event, isSigned = false,
                isFourState = false, range = [], children = [],
                childNames = []>
          ], childNames = []>
    }
    // expected-error @+1 {{multiple storages delegate the same public VPI identity}}
    simulation.storage.decl 1 in 0 :
        !simulation.unpacked_array<0 : 0 x !simulation.event> design
        hierarchy "pkg.events" {
      simulation.vpi_identity_delegated = @events,
      vpi_type = #simulation.vpi_type<kind = unpacked_array,
          isSigned = false, isFourState = false, range = [0, 0], children = [
            #simulation.vpi_type<kind = event, isSigned = false,
                isFourState = false, range = [], children = [],
                childNames = []>
          ], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @interconnect_missing_dimension_flags {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{interconnect-array requires index dimension flags}}
    simulation.vpi_object.anchor @array id 1 type 534 in 0 parent @owner
        ordinal 0 hierarchy "pkg.bus" debug "bus" {
      index_ranges = array<i64: 1, 0, -1, 0>,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 16 : i32>
      ]>
    }
  }
}

// -----

module {
  simulation.design @dimension_flags_on_module_array {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{index_dimension_flags is only valid on an interconnect-array anchor}}
    simulation.vpi_object.anchor @array id 1 type 112 in 0 parent @owner
        ordinal 0 hierarchy "pkg.instances" debug "instances" {
      index_dimension_flags = array<i64: 0>, index_ranges = array<i64: 0, 0>
    }
  }
}

// -----

module {
  simulation.design @wrong_child_under_interconnect_array {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @array id 1 type 534 in 0 parent @owner
        ordinal 0 hierarchy "pkg.bus" debug "bus" {
      index_dimension_flags = array<i64: 0>, index_ranges = array<i64: 0, 0>,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 16 : i32>
      ]>
    }
    // expected-error @+1 {{has an illegal lexical parent kind}}
    simulation.vpi_object.anchor @child id 2 type 21 in 0 parent @array
        ordinal 0 hierarchy "pkg.bus[0]" debug "child" {
      member_indices = array<i64: 0>, primitive_input_count = 0 : i64
    }
  }
}

// -----

module {
  simulation.design @interconnect_dimension_flag_rank {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{index_dimension_flags must have one entry per index range}}
    simulation.vpi_object.anchor @array id 1 type 534 in 0 parent @owner
        ordinal 0 hierarchy "pkg.bus" debug "bus" {
      index_dimension_flags = array<i64>, index_ranges = array<i64: 0, 0>,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 16 : i32>
      ]>
    }
  }
}

// -----

module {
  simulation.design @interconnect_dimension_flag_value {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{index dimension flags must be zero or one}}
    simulation.vpi_object.anchor @array id 1 type 534 in 0 parent @owner
        ordinal 0 hierarchy "pkg.bus" debug "bus" {
      index_dimension_flags = array<i64: 2>, index_ranges = array<i64: 0, 0>,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 16 : i32>
      ]>
    }
  }
}

// -----

module {
  simulation.design @interconnect_leaf_unknown_net {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @array id 1 type 534 in 0 parent @owner
        ordinal 0 hierarchy "pkg.bus" debug "bus" {
      index_dimension_flags = array<i64: 0>, index_ranges = array<i64: 0, 0>,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 16 : i32>
      ]>
    }
    // expected-error @+1 {{references an unknown backing net ID}}
    simulation.vpi_object.anchor @leaf id 2 type 533 in 0 parent @array
        ordinal 0 hierarchy "pkg.bus[0]" debug "bus" {
      backing = #simulation.vpi_backing<kind = net, id = 99 : i64>,
      member_indices = array<i64: 0>
    }
  }
}

// -----

module {
  simulation.design @interconnect_array_missing_subtype {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{interconnect-array requires vpiInterconnect subtype}}
    simulation.vpi_object.anchor @array id 1 type 534 in 0 parent @owner
        ordinal 0 hierarchy "pkg.bus" debug "bus" {
      index_dimension_flags = array<i64: 0>, index_ranges = array<i64: 0, 0>
    }
  }
}

// -----

module {
  simulation.design @interconnect_array_wrong_subtype {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{interconnect-array requires vpiInterconnect subtype}}
    simulation.vpi_object.anchor @array id 1 type 534 in 0 parent @owner
        ordinal 0 hierarchy "pkg.bus" debug "bus" {
      index_dimension_flags = array<i64: 0>, index_ranges = array<i64: 0, 0>,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 1 : i32>
      ]>
    }
  }
}
