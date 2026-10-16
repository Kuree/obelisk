// RUN: obelisk-opt %s -split-input-file -verify-diagnostics -o /dev/null

#void = #simulation.vpi_type<kind = void, isSigned = false, isFourState = false,
    range = [], children = [], childNames = []>
#mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#void], childNames = []>
module {
  simulation.design @standalone_void {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @std id 0 type 600 in 0 ordinal 0 hierarchy "std" debug "std"
    // expected-error @+1 {{mailbox void storage requires a tagged union member}}
    simulation.vpi_typespec.decl @type id 0 in 0 owner @std hierarchy "std::mailbox" debug "mailbox" {
      origin = 4 : i32, message_type = i1, target_type = #mailbox}
  }
}

// -----

#void = #simulation.vpi_type<kind = void, isSigned = false, isFourState = false,
    range = [], children = [], childNames = []>
#union = #simulation.vpi_type<kind = unpacked_union, isSigned = false, isFourState = false,
    range = [], children = [#void], childNames = ["none"], isTagged = true,
    isSoft = false, bitWidth = 0 : i64, selectableWidth = 1 : i64,
    bitstreamWidth = 1 : i64, tagBits = 1 : i64,
    childOrdinals = [0], childPackedOffsets = [0]>
#mailbox = #simulation.vpi_type<kind = mailbox, isSigned = false, isFourState = false,
    range = [], children = [#union], childNames = []>
!wrong = !simulation.unpacked_union<fields = [
    #simulation.field<name = "none", type = i8, ordinal = 0, packedOffset = 0>], isTagged = true>
module {
  simulation.design @wrong_void_placeholder {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @std id 0 type 600 in 0 ordinal 0 hierarchy "std" debug "std"
    // expected-error @+1 {{does not normalize to executable type}}
    simulation.vpi_typespec.decl @type id 0 in 0 owner @std hierarchy "std::mailbox" debug "mailbox" {
      origin = 4 : i32, message_type = !wrong, target_type = #mailbox}
  }
}
