!record = !simulation.packed_struct<[
  #simulation.field<name = "a", type = !simulation.logic<3>, ordinal = 0,
      packedOffset = 1>,
  #simulation.field<name = "b", type = !simulation.logic<1>, ordinal = 1,
      packedOffset = 0>
]>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @vpi_interconnects {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top.d" {vpi_kind = 32 : i32}
    simulation.vpi_object.anchor @top_d id 0 type 32 in 1 ordinal 0
        hierarchy "top.d" debug "d" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.vpi_object.anchor @bus id 1 type 534 in 1 parent @top_d
        ordinal 0 hierarchy "top.d.bus" debug "bus" {
      index_dimension_flags = array<i64: 0, 1>,
      index_ranges = array<i64: 1, 0, -1, 0>,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 16 : i32>
      ]>
    }
    simulation.net.decl 0 in 1 : !simulation.logic<2> design
        hierarchy "top.d.bus[1][-1]" debug "bus" {
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 16 : i32>
      ]>,
      vpi_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [1, 0], children = [], childNames = []>
    }
    simulation.vpi_object.anchor @bus_1_n1 id 2 type 533 in 1 parent @bus
        ordinal 0 hierarchy "top.d.bus[1][-1]" debug "bus" {
      backing = #simulation.vpi_backing<kind = net, id = 0 : i64>,
      member_indices = array<i64: 1, -1>
    }
    simulation.net.decl 1 in 1 : f64 design
        hierarchy "top.d.bus[1][0]" debug "bus" {
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 16 : i32>
      ]>,
      vpi_type = #simulation.vpi_type<kind = real, isSigned = false,
          isFourState = false, range = [], children = [], childNames = []>
    }
    simulation.vpi_object.anchor @bus_1_0 id 3 type 533 in 1 parent @bus
        ordinal 1 hierarchy "top.d.bus[1][0]" debug "bus" {
      backing = #simulation.vpi_backing<kind = net, id = 1 : i64>,
      member_indices = array<i64: 1, 0>
    }
    simulation.net.decl 2 in 1 : f64 design
        hierarchy "top.d.bus[0][-1]" debug "bus" {
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 16 : i32>
      ]>,
      vpi_type = #simulation.vpi_type<kind = real, isSigned = false,
          isFourState = false, range = [], children = [], childNames = []>
    }
    simulation.vpi_object.anchor @bus_0_n1 id 4 type 533 in 1 parent @bus
        ordinal 2 hierarchy "top.d.bus[0][-1]" debug "bus" {
      backing = #simulation.vpi_backing<kind = net, id = 2 : i64>,
      member_indices = array<i64: 0, -1>
    }
    simulation.net.decl 3 in 1 : !record design
        hierarchy "top.d.bus[0][0]" debug "bus" {
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 16 : i32>
      ]>,
      vpi_type = #simulation.vpi_type<kind = packed_struct, isSigned = false,
          isFourState = true, name = "record_t", range = [], children = [
            #simulation.vpi_type<kind = logic, isSigned = false,
                isFourState = true, range = [2, 0], children = [],
                childNames = []>,
            #simulation.vpi_type<kind = logic, isSigned = false,
                isFourState = true, range = [0, 0], children = [],
                childNames = []>
          ], childNames = ["a", "b"], isTagged = false, isSoft = false,
          bitWidth = 4 : i64, selectableWidth = 4 : i64,
          bitstreamWidth = 4 : i64, tagBits = 0 : i64,
          childOrdinals = [0, 1], childPackedOffsets = [1, 0]>
    }
    simulation.vpi_object.anchor @bus_0_0 id 5 type 533 in 1 parent @bus
        ordinal 3 hierarchy "top.d.bus[0][0]" debug "bus" {
      backing = #simulation.vpi_backing<kind = net, id = 3 : i64>,
      member_indices = array<i64: 0, 0>
    }
    simulation.net.decl 4 in 1 : !simulation.logic<4> design
        hierarchy "top.d.scalar" debug "scalar" {
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 16 : i32>
      ]>,
      vpi_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [3, 0], children = [], childNames = []>
    }
    simulation.vpi_object.anchor @scalar id 6 type 533 in 1 parent @top_d
        ordinal 1 hierarchy "top.d.scalar" debug "scalar" {
      backing = #simulation.vpi_backing<kind = net, id = 4 : i64>,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 22 : i32, value = 16 : i32>
      ]>
    }
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
