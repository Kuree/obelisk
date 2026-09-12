// RUN: obelisk-opt %s --split-input-file --verify-diagnostics -o /dev/null

module {
  obelisk_sim.design @fixed_and_sparse {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{VPI array cannot be both fixed and sparse}}
    obelisk_sim.vpi_object.anchor @array id 1 type 112 in 0 parent @owner
        ordinal 0 hierarchy "pkg.a" debug "a" {
      index_ranges = array<i64: 3, 0>, sparse_indices = array<i64>
    }
  }
}

// -----

module {
  obelisk_sim.design @scalar_with_fixed_geometry {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{index_ranges is only valid on a fixed array anchor}}
    obelisk_sim.vpi_object.anchor @event id 0 type 34 in 0 ordinal 0
        hierarchy "event" debug "event" {index_ranges = array<i64>}
  }
}

// -----

module {
  obelisk_sim.design @fixed_without_geometry {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{fixed VPI array requires nonempty index_ranges}}
    obelisk_sim.vpi_object.anchor @array id 1 type 129 in 0 parent @owner
        ordinal 0 hierarchy "pkg.events" debug "events"
  }
}

// -----

module {
  obelisk_sim.design @fixed_with_empty_geometry {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{fixed VPI array requires nonempty index_ranges}}
    obelisk_sim.vpi_object.anchor @array id 1 type 129 in 0 parent @owner
        ordinal 0 hierarchy "pkg.events" debug "events" {
      index_ranges = array<i64>
    }
  }
}

// -----

module {
  obelisk_sim.design @generate_without_geometry {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{generate-scope array requires sparse_indices}}
    obelisk_sim.vpi_object.anchor @array id 1 type 133 in 0 parent @owner
        ordinal 0 hierarchy "pkg.g" debug "g"
  }
}

// -----

module {
  obelisk_sim.design @empty_generate_array {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    obelisk_sim.vpi_object.anchor @array id 1 type 133 in 0 parent @owner
        ordinal 0 hierarchy "pkg.g" debug "g" {
      sparse_indices = array<i64>
    }
  }
}

// -----

module {
  obelisk_sim.design @empty_member_on_scalar_parent {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{member_indices requires a fixed or generate array parent}}
    obelisk_sim.vpi_object.anchor @event id 1 type 34 in 0 parent @owner
        ordinal 0 hierarchy "pkg.event" debug "event" {
      member_indices = array<i64>
    }
  }
}

// -----

module {
  obelisk_sim.design @member_on_root {
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32
    // expected-error @+1 {{member_indices requires a fixed or generate array parent}}
    obelisk_sim.vpi_object.anchor @top id 0 type 32 in 0 ordinal 0
        hierarchy "top" debug "top" {
      backing = #obelisk_sim.vpi_backing<kind = scope, id = 0 : i64>,
      member_indices = array<i64: 0>
    }
  }
}

// -----

module {
  obelisk_sim.design @fixed_child_wrong_rank {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    obelisk_sim.vpi_object.anchor @array id 1 type 129 in 0 parent @owner
        ordinal 0 hierarchy "pkg.events" debug "events" {
      index_ranges = array<i64: 1, 0, 2, 3>
    }
    // expected-error @+1 {{fixed-array child member_indices rank does not match parent}}
    obelisk_sim.vpi_object.anchor @event id 2 type 34 in 0 parent @array
        ordinal 0 hierarchy "pkg.events[1][2]" debug "events[1][2]" {
      member_indices = array<i64: 1>
    }
  }
}

// -----

module {
  obelisk_sim.design @fixed_child_out_of_range {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    obelisk_sim.vpi_object.anchor @array id 1 type 129 in 0 parent @owner
        ordinal 0 hierarchy "pkg.events" debug "events" {
      index_ranges = array<i64: 1, 0>
    }
    // expected-error @+1 {{fixed-array child member index is outside parent range}}
    obelisk_sim.vpi_object.anchor @event id 2 type 34 in 0 parent @array
        ordinal 0 hierarchy "pkg.events[2]" debug "events[2]" {
      member_indices = array<i64: 2>
    }
  }
}

// -----

module {
  obelisk_sim.design @generate_child_missing_index {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    obelisk_sim.vpi_object.anchor @array id 1 type 133 in 0 parent @owner
        ordinal 0 hierarchy "pkg.g" debug "g" {
      sparse_indices = array<i64: -1, 4>
    }
    // expected-error @+1 {{generate-array child member index is absent from parent}}
    obelisk_sim.vpi_object.anchor @scope id 2 type 134 in 0 parent @array
        ordinal 0 hierarchy "pkg.g[0]" debug "g[0]" {
      member_indices = array<i64: 0>
    }
  }
}

// -----

module {
  obelisk_sim.design @duplicate_generate_index {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{sparse_indices contains a duplicate index}}
    obelisk_sim.vpi_object.anchor @array id 1 type 133 in 0 parent @owner
        ordinal 0 hierarchy "pkg.g" debug "g" {
      sparse_indices = array<i64: 2, -1, 2>
    }
  }
}

// -----

module {
  obelisk_sim.design @fixed_extent_overflow {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{fixed VPI array shape exceeds relation encoding}}
    obelisk_sim.vpi_object.anchor @array id 1 type 129 in 0 parent @owner
        ordinal 0 hierarchy "pkg.events" debug "events" {
      index_ranges = array<i64: -9223372036854775808, 9223372036854775807>
    }
  }
}

// -----

module {
  obelisk_sim.design @generate_with_ranges {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{VPI array cannot be both fixed and sparse}}
    obelisk_sim.vpi_object.anchor @array id 1 type 133 in 0 parent @owner
        ordinal 0 hierarchy "pkg.g" debug "g" {
      index_ranges = array<i64>, sparse_indices = array<i64>
    }
  }
}

// -----

module {
  obelisk_sim.design @scalar_with_sparse_geometry {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{sparse_indices is only valid on a generate-scope array anchor}}
    obelisk_sim.vpi_object.anchor @event id 0 type 34 in 0 ordinal 0
        hierarchy "event" debug "event" {sparse_indices = array<i64>}
  }
}

// -----

module {
  obelisk_sim.design @multidimensional_gate_array {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{primitive VPI array must be one-dimensional}}
    obelisk_sim.vpi_object.anchor @array id 1 type 111 in 0 parent @owner
        ordinal 0 hierarchy "pkg.g" debug "g" {
      index_ranges = array<i64: 1, 0, -2, 2>
    }
  }
}

// -----

module {
  obelisk_sim.design @array_with_member_geometry {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{array aggregates cannot carry member_indices}}
    obelisk_sim.vpi_object.anchor @array id 1 type 112 in 0 parent @owner
        ordinal 0 hierarchy "pkg.a" debug "a" {
      index_ranges = array<i64: 0, 0>, member_indices = array<i64>
    }
  }
}

// -----

module {
  obelisk_sim.design @invalid_delegated_vpi_identity {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{delegated VPI identity must name its owning object anchor}}
    obelisk_sim.storage.decl 0 in 0 : i8 design hierarchy "value" {
      obelisk_sim.vpi_identity_delegated
    }
  }
}

// -----

module {
  obelisk_sim.design @fixed_child_missing_indices {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    obelisk_sim.vpi_object.anchor @array id 1 type 129 in 0 parent @owner
        ordinal 0 hierarchy "pkg.events" debug "events" {
      index_ranges = array<i64: 0, 0>
    }
    // expected-error @+1 {{fixed-array child member_indices rank does not match parent}}
    obelisk_sim.vpi_object.anchor @event id 2 type 34 in 0 parent @array
        ordinal 0 hierarchy "pkg.events[0]" debug "events[0]"
  }
}

// -----

module {
  obelisk_sim.design @duplicate_fixed_coordinate {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    obelisk_sim.vpi_object.anchor @array id 1 type 129 in 0 parent @owner
        ordinal 0 hierarchy "pkg.events" debug "events" {
      index_ranges = array<i64: 0, 0>
    }
    obelisk_sim.vpi_object.anchor @event0 id 2 type 34 in 0 parent @array
        ordinal 0 hierarchy "pkg.events[0]" debug "events[0]" {
      member_indices = array<i64: 0>
    }
    // expected-error @+1 {{duplicates a source coordinate in its VPI array parent}}
    obelisk_sim.vpi_object.anchor @event1 id 3 type 34 in 0 parent @array
        ordinal 1 hierarchy "pkg.events[0]" debug "events[0]" {
      member_indices = array<i64: 0>
    }
  }
}

// -----

module {
  obelisk_sim.design @incomplete_fixed_coordinates {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{does not have exactly one child for every source coordinate}}
    obelisk_sim.vpi_object.anchor @array id 1 type 129 in 0 parent @owner
        ordinal 0 hierarchy "pkg.events" debug "events" {
      index_ranges = array<i64: 1, 0>
    }
    obelisk_sim.vpi_object.anchor @event id 2 type 34 in 0 parent @array
        ordinal 0 hierarchy "pkg.events[1]" debug "events[1]" {
      member_indices = array<i64: 1>
    }
  }
}

// -----

module {
  obelisk_sim.design @delegated_unknown_anchor {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{delegated VPI identity must reference a named-event-array anchor}}
    obelisk_sim.storage.decl 0 in 0 :
        !obelisk_sim.unpacked_array<1 : 0 x !obelisk_sim.event> design
        hierarchy "pkg.events" {
      obelisk_sim.vpi_identity_delegated = @missing,
      vpi_type = #obelisk_sim.vpi_type<kind = unpacked_array,
          isSigned = false, isFourState = false, range = [1, 0], children = [
            #obelisk_sim.vpi_type<kind = event, isSigned = false,
                isFourState = false, range = [], children = [],
                childNames = []>
          ], childNames = []>
    }
  }
}

// -----

module {
  obelisk_sim.design @delegated_wrong_anchor_kind {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{delegated VPI identity must reference a named-event-array anchor}}
    obelisk_sim.storage.decl 0 in 0 :
        !obelisk_sim.unpacked_array<1 : 0 x !obelisk_sim.event> design
        hierarchy "pkg.events" {
      obelisk_sim.vpi_identity_delegated = @owner,
      vpi_type = #obelisk_sim.vpi_type<kind = unpacked_array,
          isSigned = false, isFourState = false, range = [1, 0], children = [
            #obelisk_sim.vpi_type<kind = event, isSigned = false,
                isFourState = false, range = [], children = [],
                childNames = []>
          ], childNames = []>
    }
  }
}

// -----

module {
  obelisk_sim.design @delegated_name_mismatch {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    obelisk_sim.vpi_object.anchor @events id 1 type 129 in 0 parent @owner
        ordinal 0 hierarchy "pkg.events" debug "events" {
      index_ranges = array<i64: 0, 0>
    }
    obelisk_sim.vpi_object.anchor @event id 2 type 34 in 0 parent @events
        ordinal 0 hierarchy "pkg.events[0]" debug "events[0]" {
      member_indices = array<i64: 0>
    }
    // expected-error @+1 {{delegated VPI identity disagrees with its anchor scope or name}}
    obelisk_sim.storage.decl 0 in 0 :
        !obelisk_sim.unpacked_array<0 : 0 x !obelisk_sim.event> design
        hierarchy "pkg.other" {
      obelisk_sim.vpi_identity_delegated = @events,
      vpi_type = #obelisk_sim.vpi_type<kind = unpacked_array,
          isSigned = false, isFourState = false, range = [0, 0], children = [
            #obelisk_sim.vpi_type<kind = event, isSigned = false,
                isFourState = false, range = [], children = [],
                childNames = []>
          ], childNames = []>
    }
  }
}

// -----

module {
  obelisk_sim.design @delegated_shape_mismatch {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    obelisk_sim.vpi_object.anchor @events id 1 type 129 in 0 parent @owner
        ordinal 0 hierarchy "pkg.events" debug "events" {
      index_ranges = array<i64: 0, 0>
    }
    obelisk_sim.vpi_object.anchor @event id 2 type 34 in 0 parent @events
        ordinal 0 hierarchy "pkg.events[0]" debug "events[0]" {
      member_indices = array<i64: 0>
    }
    // expected-error @+1 {{delegated VPI identity disagrees with its anchor array shape}}
    obelisk_sim.storage.decl 0 in 0 :
        !obelisk_sim.unpacked_array<2 : 0 x !obelisk_sim.event> design
        hierarchy "pkg.events" {
      obelisk_sim.vpi_identity_delegated = @events,
      vpi_type = #obelisk_sim.vpi_type<kind = unpacked_array,
          isSigned = false, isFourState = false, range = [2, 0], children = [
            #obelisk_sim.vpi_type<kind = event, isSigned = false,
                isFourState = false, range = [], children = [],
                childNames = []>
          ], childNames = []>
    }
  }
}

// -----

module {
  obelisk_sim.design @duplicate_delegated_identity {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    obelisk_sim.vpi_object.anchor @events id 1 type 129 in 0 parent @owner
        ordinal 0 hierarchy "pkg.events" debug "events" {
      index_ranges = array<i64: 0, 0>
    }
    obelisk_sim.vpi_object.anchor @event id 2 type 34 in 0 parent @events
        ordinal 0 hierarchy "pkg.events[0]" debug "events[0]" {
      member_indices = array<i64: 0>
    }
    obelisk_sim.storage.decl 0 in 0 :
        !obelisk_sim.unpacked_array<0 : 0 x !obelisk_sim.event> design
        hierarchy "pkg.events" {
      obelisk_sim.vpi_identity_delegated = @events,
      vpi_type = #obelisk_sim.vpi_type<kind = unpacked_array,
          isSigned = false, isFourState = false, range = [0, 0], children = [
            #obelisk_sim.vpi_type<kind = event, isSigned = false,
                isFourState = false, range = [], children = [],
                childNames = []>
          ], childNames = []>
    }
    // expected-error @+1 {{multiple storages delegate the same public VPI identity}}
    obelisk_sim.storage.decl 1 in 0 :
        !obelisk_sim.unpacked_array<0 : 0 x !obelisk_sim.event> design
        hierarchy "pkg.events" {
      obelisk_sim.vpi_identity_delegated = @events,
      vpi_type = #obelisk_sim.vpi_type<kind = unpacked_array,
          isSigned = false, isFourState = false, range = [0, 0], children = [
            #obelisk_sim.vpi_type<kind = event, isSigned = false,
                isFourState = false, range = [], children = [],
                childNames = []>
          ], childNames = []>
    }
  }
}

// -----

module {
  obelisk_sim.design @interconnect_missing_dimension_flags {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{interconnect-array requires index dimension flags}}
    obelisk_sim.vpi_object.anchor @array id 1 type 534 in 0 parent @owner
        ordinal 0 hierarchy "pkg.bus" debug "bus" {
      index_ranges = array<i64: 1, 0, -1, 0>,
      vpi_properties = #obelisk_sim.vpi_properties<[
        #obelisk_sim.vpi_property<selector = 22 : i32, value = 16 : i32>
      ]>
    }
  }
}

// -----

module {
  obelisk_sim.design @dimension_flags_on_module_array {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{index_dimension_flags is only valid on an interconnect-array anchor}}
    obelisk_sim.vpi_object.anchor @array id 1 type 112 in 0 parent @owner
        ordinal 0 hierarchy "pkg.instances" debug "instances" {
      index_dimension_flags = array<i64: 0>, index_ranges = array<i64: 0, 0>
    }
  }
}

// -----

module {
  obelisk_sim.design @wrong_child_under_interconnect_array {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    obelisk_sim.vpi_object.anchor @array id 1 type 534 in 0 parent @owner
        ordinal 0 hierarchy "pkg.bus" debug "bus" {
      index_dimension_flags = array<i64: 0>, index_ranges = array<i64: 0, 0>,
      vpi_properties = #obelisk_sim.vpi_properties<[
        #obelisk_sim.vpi_property<selector = 22 : i32, value = 16 : i32>
      ]>
    }
    // expected-error @+1 {{has an illegal lexical parent kind}}
    obelisk_sim.vpi_object.anchor @child id 2 type 21 in 0 parent @array
        ordinal 0 hierarchy "pkg.bus[0]" debug "child" {
      member_indices = array<i64: 0>, primitive_input_count = 0 : i64
    }
  }
}

// -----

module {
  obelisk_sim.design @interconnect_dimension_flag_rank {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{index_dimension_flags must have one entry per index range}}
    obelisk_sim.vpi_object.anchor @array id 1 type 534 in 0 parent @owner
        ordinal 0 hierarchy "pkg.bus" debug "bus" {
      index_dimension_flags = array<i64>, index_ranges = array<i64: 0, 0>,
      vpi_properties = #obelisk_sim.vpi_properties<[
        #obelisk_sim.vpi_property<selector = 22 : i32, value = 16 : i32>
      ]>
    }
  }
}

// -----

module {
  obelisk_sim.design @interconnect_dimension_flag_value {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{index dimension flags must be zero or one}}
    obelisk_sim.vpi_object.anchor @array id 1 type 534 in 0 parent @owner
        ordinal 0 hierarchy "pkg.bus" debug "bus" {
      index_dimension_flags = array<i64: 2>, index_ranges = array<i64: 0, 0>,
      vpi_properties = #obelisk_sim.vpi_properties<[
        #obelisk_sim.vpi_property<selector = 22 : i32, value = 16 : i32>
      ]>
    }
  }
}

// -----

module {
  obelisk_sim.design @interconnect_leaf_unknown_net {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    obelisk_sim.vpi_object.anchor @array id 1 type 534 in 0 parent @owner
        ordinal 0 hierarchy "pkg.bus" debug "bus" {
      index_dimension_flags = array<i64: 0>, index_ranges = array<i64: 0, 0>,
      vpi_properties = #obelisk_sim.vpi_properties<[
        #obelisk_sim.vpi_property<selector = 22 : i32, value = 16 : i32>
      ]>
    }
    // expected-error @+1 {{references an unknown backing net ID}}
    obelisk_sim.vpi_object.anchor @leaf id 2 type 533 in 0 parent @array
        ordinal 0 hierarchy "pkg.bus[0]" debug "bus" {
      backing = #obelisk_sim.vpi_backing<kind = net, id = 99 : i64>,
      member_indices = array<i64: 0>
    }
  }
}

// -----

module {
  obelisk_sim.design @interconnect_array_missing_subtype {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{interconnect-array requires vpiInterconnect subtype}}
    obelisk_sim.vpi_object.anchor @array id 1 type 534 in 0 parent @owner
        ordinal 0 hierarchy "pkg.bus" debug "bus" {
      index_dimension_flags = array<i64: 0>, index_ranges = array<i64: 0, 0>
    }
  }
}

// -----

module {
  obelisk_sim.design @interconnect_array_wrong_subtype {
    obelisk_sim.scope.decl 0
    obelisk_sim.vpi_object.anchor @owner id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{interconnect-array requires vpiInterconnect subtype}}
    obelisk_sim.vpi_object.anchor @array id 1 type 534 in 0 parent @owner
        ordinal 0 hierarchy "pkg.bus" debug "bus" {
      index_dimension_flags = array<i64: 0>, index_ranges = array<i64: 0, 0>,
      vpi_properties = #obelisk_sim.vpi_properties<[
        #obelisk_sim.vpi_property<selector = 22 : i32, value = 1 : i32>
      ]>
    }
  }
}
