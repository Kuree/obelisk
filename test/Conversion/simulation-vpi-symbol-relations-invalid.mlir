// RUN: obelisk-opt --verify-diagnostics %s \
// RUN:   --encode-obelisk-sim-to-bytecode='vpi=read'

// A semantic alias already supplies vpiTypedefAlias. Explicit relations cannot
// overwrite that slice even when their target is the same physical object.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @synthesized_alias {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.vpi_typespec.decl @base id 0 in 1 owner @top
        hierarchy "top.base" debug "base" {
      target_type = #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    }
    simulation.vpi_typespec.decl @alias id 1 in 1 owner @top
        hierarchy "top.alias" debug "alias" {
      target_type = #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = [],
          typedefAliases = [@alias, @base]>
    }
    // expected-error @below {{relation source, selector, and access mode are already supplied by design inventory}}
    simulation.vpi_relation.decl <kind = symbol, symbol = @alias>
        selector 701 handle ordinal 0 to <kind = symbol, symbol = @base>
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
