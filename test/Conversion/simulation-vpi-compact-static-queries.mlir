// RUN: env OBELISK_TEST_INPUT=%s %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.CompactStaticQueries:GeneratedDesignDatabase.RejectsMalformedFrozenParameterImage

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @compact_static_queries {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" {vpi_kind = 32 : i32}

    // The first compact record deliberately uses stable ID zero.
    obelisk_sim.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 1
        hierarchy "pkg" debug "pkg" loc("compact_static.sv":3:4)
    obelisk_sim.vpi_object.anchor @top id 1 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #obelisk_sim.vpi_backing<kind = scope, id = 1 : i64>
    }
    obelisk_sim.vpi_object.anchor @cb id 2 type 650 in 1 parent @top
        ordinal 0 hierarchy "top.cb" debug "cb"
    obelisk_sim.vpi_object.anchor @seq id 3 type 661 in 1 parent @cb
        ordinal 0 hierarchy "top.cb.seq" debug "seq"
    obelisk_sim.vpi_object.anchor @prop id 4 type 655 in 1 parent @cb
        ordinal 1 hierarchy "top.cb.prop" debug "prop"
    obelisk_sim.vpi_object.anchor @generated id 5 type 134 in 1 parent @top
        ordinal 1 hierarchy "top.g" debug "g" {is_protected}
    // Scalar primitives retain their Object-only input-count payload.
    obelisk_sim.vpi_object.anchor @gate id 6 type 21 in 1 parent @top
        ordinal 2 hierarchy "top.gate" debug "gate" {
      primitive_input_count = 2 : i64
    }
    obelisk_sim.vpi_object.anchor @max_width id 7 type 41 in 0 parent @pkg
        ordinal 0 hierarchy "pkg::UVM_HDL_MAX_WIDTH"
        debug "UVM_HDL_MAX_WIDTH" {
      immutable_value = #obelisk_sim.frozen_constant<
          value = [1536 : i32, 0 : i32], isSigned = true> : i32,
      vpi_type = #obelisk_sim.vpi_type<kind = int, isSigned = true,
          isFourState = false, range = [31, 0], children = [], childNames = []>
    } loc("compact_static.sv":8:9)
    // An equal value must share one immutable pool row while retaining an
    // independent declaration identity and package traversal edge.
    obelisk_sim.vpi_object.anchor @same_width id 8 type 41 in 0 parent @pkg
        ordinal 1 hierarchy "pkg::SAME_WIDTH" debug "SAME_WIDTH" {
      immutable_value = #obelisk_sim.frozen_constant<
          value = [1536 : i32, 0 : i32], isSigned = true> : i32,
      vpi_type = #obelisk_sim.vpi_type<kind = int, isSigned = true,
          isFourState = false, range = [31, 0], children = [], childNames = []>
    } loc("compact_static.sv":9:9)

    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "__root"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context
        {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      obelisk_sim.return
    }
  }
}
