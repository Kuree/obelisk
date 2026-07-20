//===- ObeliskTypes.td - Obelisk simulation types ----------*- tablegen -*-===//

#ifndef OBELISK_DIALECT_SIM_OBELISKTYPES_TD
#define OBELISK_DIALECT_SIM_OBELISKTYPES_TD

include "obelisk/Dialect/Sim/ObeliskDialect.td"
include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/OpBase.td"

class ObeliskType<string name> : TypeDef<ObeliskDialect, name> {}

// Exact four-state packed data.  The canonical lowering is two W-bit planes:
// `unknown=0` selects ordinary 0/1 from `value`; `unknown=1` selects X/Z from
// `value`.  This represents all four symbols without a third plane.
def LogicType : ObeliskType<"Logic"> {
  let mnemonic = "logic";
  let summary = "fixed-width exact four-state packed value";
  let parameters = (ins "unsigned":$width);
  let assemblyFormat = "`<` $width `>`";
  let genVerifyDecl = 1;
}

def Logic1Type : ConfinedType<LogicType, [
  CPred<"::llvm::cast<::obelisk::ir::LogicType>($_self).getWidth() == 1">
], "one-bit exact four-state logic", "::obelisk::ir::LogicType">;

// Simulation time is unsigned and independent of host time_t.  The eventual
// runtime ABI uses a fixed-width representation selected by the lowering.
def TimeType : ObeliskType<"Time"> {
  let mnemonic = "time";
  let summary = "scaled simulation time";
}

// References have identity and aliasing.  They are deliberately distinct from
// MLIR's memref, which cannot express all SystemVerilog storage behavior.
def RefType : ObeliskType<"Ref"> {
  let mnemonic = "ref";
  let summary = "mutable variable or object-field reference";
  let parameters = (ins "::mlir::Type":$elementType);
  let assemblyFormat = "`<` $elementType `>`";
}

// Nets are resolved, potentially multi-driver resources rather than variables.
def NetType : ObeliskType<"Net"> {
  let mnemonic = "net";
  let summary = "resolved SystemVerilog net handle";
  let parameters = (ins "::mlir::Type":$elementType);
  let assemblyFormat = "`<` $elementType `>`";
}

def ProcessType : ObeliskType<"Process"> {
  let mnemonic = "process";
  let summary = "scheduler-owned process handle";
}

def EventType : ObeliskType<"Event"> {
  let mnemonic = "event";
  let summary = "named event handle";
}

def ObjectType : ObeliskType<"Object"> {
  let mnemonic = "object";
  let summary = "nullable dynamically-dispatched class object handle";
}

def BoxType : ObeliskType<"Box"> {
  let mnemonic = "any";
  let summary = "boxed runtime value for untyped containers and reflection";
}

def DynArrayType : ObeliskType<"DynArray"> {
  let mnemonic = "dynarray";
  let summary = "dynamic array with reference semantics";
  let parameters = (ins "::mlir::Type":$elementType);
  let assemblyFormat = "`<` $elementType `>`";
}

def AssocArrayType : ObeliskType<"AssocArray"> {
  let mnemonic = "assoc";
  let summary = "associative array with reference semantics";
  let parameters = (ins
    "::mlir::Type":$keyType,
    "::mlir::Type":$elementType
  );
  let assemblyFormat = "`<` $keyType `,` $elementType `>`";
}

def MailboxType : ObeliskType<"Mailbox"> {
  let mnemonic = "mailbox";
  let summary = "typed or boxed blocking mailbox";
  let parameters = (ins "::mlir::Type":$elementType);
  let assemblyFormat = "`<` $elementType `>`";
}

def SemaphoreType : ObeliskType<"Semaphore"> {
  let mnemonic = "semaphore";
  let summary = "SystemVerilog semaphore handle";
}

def RandomStreamType : ObeliskType<"RandomStream"> {
  let mnemonic = "random_stream";
  let summary = "deterministic hierarchical pseudorandom stream";
}

def RandomStateType : ObeliskType<"RandomState"> {
  let mnemonic = "random_state";
  let summary = "saved random stream state";
}

def RandCStateType : ObeliskType<"RandCState"> {
  let mnemonic = "randc_state";
  let summary = "state for a random-cyclic variable";
  let parameters = (ins "::mlir::Type":$elementType);
  let assemblyFormat = "`<` $elementType `>`";
}

def ConstraintSetType : ObeliskType<"ConstraintSet"> {
  let mnemonic = "constraint_set";
  let summary = "compiled random constraint graph handle";
}

def ChandleType : ObeliskType<"Chandle"> {
  let mnemonic = "chandle";
  let summary = "opaque DPI C pointer";
}

def VPIHandleType : ObeliskType<"VPIHandle"> {
  let mnemonic = "vpi_handle";
  let summary = "opaque VPI object handle";
}

#endif // OBELISK_DIALECT_SIM_OBELISKTYPES_TD
