//===- SimulationEnums.td - Executable simulation enumerations -*- td -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONENUMS_TD
#define OBELISK_DIALECT_SIMULATION_SIMULATIONENUMS_TD

include "mlir/IR/EnumAttr.td"

def SimEntryRootInitializer
    : I32EnumAttrCase<"RootInitializer", 0, "root_initializer">;
def SimEntryInitial : I32EnumAttrCase<"Initial", 1, "initial">;
def SimEntryFinal : I32EnumAttrCase<"Final", 2, "final">;
def SimEntryAlways : I32EnumAttrCase<"Always", 3, "always">;
def SimEntryAlwaysComb : I32EnumAttrCase<"AlwaysComb", 4, "always_comb">;
def SimEntryAlwaysFF : I32EnumAttrCase<"AlwaysFF", 5, "always_ff">;
def SimEntryAlwaysLatch : I32EnumAttrCase<"AlwaysLatch", 6, "always_latch">;
def SimEntryContinuous : I32EnumAttrCase<"Continuous", 7, "continuous">;
def SimEntryFunction : I32EnumAttrCase<"Function", 8, "function">;
def SimEntryKind : I32EnumAttr<"EntryKind", "simulation code-unit kind", [
  SimEntryRootInitializer, SimEntryInitial, SimEntryFinal, SimEntryAlways,
  SimEntryAlwaysComb, SimEntryAlwaysFF, SimEntryAlwaysLatch, SimEntryContinuous,
  SimEntryFunction
]> {
  let cppNamespace = "::obelisk::sim";
}

def SimCaptureContext : I32EnumAttrCase<"Context", 0, "context">;
def SimCaptureFormal : I32EnumAttrCase<"Formal", 1, "formal">;
def SimCaptureValue : I32EnumAttrCase<"Value", 2, "value">;
def SimCaptureStorage : I32EnumAttrCase<"Storage", 3, "storage">;
def SimCaptureNet : I32EnumAttrCase<"Net", 4, "net">;
def SimCaptureDriver : I32EnumAttrCase<"Driver", 5, "driver">;
def SimCaptureEvent : I32EnumAttrCase<"Event", 6, "event">;
def SimCaptureKind : I32EnumAttr<"CaptureKind", "entry argument binding kind", [
  SimCaptureContext, SimCaptureFormal, SimCaptureValue, SimCaptureStorage,
  SimCaptureNet, SimCaptureDriver, SimCaptureEvent
]> {
  let cppNamespace = "::obelisk::sim";
}

def SimLifetimeAutomatic : I32EnumAttrCase<"Automatic", 0, "automatic">;
def SimLifetimeStatic : I32EnumAttrCase<"Static", 1, "static">;
def SimLifetimeDesign : I32EnumAttrCase<"Design", 2, "design">;
def SimLifetime : I32EnumAttr<"Lifetime", "simulation object lifetime", [
  SimLifetimeAutomatic, SimLifetimeStatic, SimLifetimeDesign
]> {
  let cppNamespace = "::obelisk::sim";
}

def SimEdgeChange : I32EnumAttrCase<"Change", 0, "change">;
def SimEdgePosedge : I32EnumAttrCase<"Posedge", 1, "posedge">;
def SimEdgeNegedge : I32EnumAttrCase<"Negedge", 2, "negedge">;
def SimEdgeBoth : I32EnumAttrCase<"Both", 3, "both">;
def SimEdgeKind : I32EnumAttr<"EdgeKind", "suspension edge kind", [
  SimEdgeChange, SimEdgePosedge, SimEdgeNegedge, SimEdgeBoth
]> {
  let cppNamespace = "::obelisk::sim";
}

def SimUnaryPlus : I32EnumAttrCase<"Plus", 0, "plus">;
def SimUnaryNegate : I32EnumAttrCase<"Negate", 1, "negate">;
def SimUnaryBitNot : I32EnumAttrCase<"BitNot", 2, "bit_not">;
def SimUnaryLogicalNot : I32EnumAttrCase<"LogicalNot", 3, "logical_not">;
def SimUnaryKind
    : I32EnumAttr<"UnaryKind", "normalized four-state unary operation", [
      SimUnaryPlus, SimUnaryNegate, SimUnaryBitNot, SimUnaryLogicalNot
    ]> {
  let cppNamespace = "::obelisk::sim";
}

def SimReductionAnd : I32EnumAttrCase<"And", 0, "and">;
def SimReductionOr : I32EnumAttrCase<"Or", 1, "or">;
def SimReductionXor : I32EnumAttrCase<"Xor", 2, "xor">;
def SimReductionNand : I32EnumAttrCase<"Nand", 3, "nand">;
def SimReductionNor : I32EnumAttrCase<"Nor", 4, "nor">;
def SimReductionXnor : I32EnumAttrCase<"Xnor", 5, "xnor">;
def SimReductionKind
    : I32EnumAttr<"ReductionKind", "normalized four-state reduction", [
      SimReductionAnd, SimReductionOr, SimReductionXor, SimReductionNand,
      SimReductionNor, SimReductionXnor
    ]> {
  let cppNamespace = "::obelisk::sim";
}

def SimJoinAll : I32EnumAttrCase<"All", 0, "all">;
def SimJoinAny : I32EnumAttrCase<"Any", 1, "any">;
def SimJoinKind : I32EnumAttr<"JoinKind", "process join mode", [
  SimJoinAll, SimJoinAny
]> {
  let cppNamespace = "::obelisk::sim";
}

def SimBinaryAdd : I32EnumAttrCase<"Add", 0, "add">;
def SimBinarySub : I32EnumAttrCase<"Sub", 1, "sub">;
def SimBinaryMul : I32EnumAttrCase<"Mul", 2, "mul">;
def SimBinaryUDiv : I32EnumAttrCase<"UDiv", 3, "udiv">;
def SimBinarySDiv : I32EnumAttrCase<"SDiv", 4, "sdiv">;
def SimBinaryUMod : I32EnumAttrCase<"UMod", 5, "umod">;
def SimBinarySMod : I32EnumAttrCase<"SMod", 6, "smod">;
def SimBinaryAnd : I32EnumAttrCase<"And", 7, "and">;
def SimBinaryOr : I32EnumAttrCase<"Or", 8, "or">;
def SimBinaryXor : I32EnumAttrCase<"Xor", 9, "xor">;
def SimBinaryXnor : I32EnumAttrCase<"Xnor", 10, "xnor">;
def SimBinaryKind
    : I32EnumAttr<"BinaryKind", "normalized four-state binary operation", [
      SimBinaryAdd, SimBinarySub, SimBinaryMul, SimBinaryUDiv, SimBinarySDiv,
      SimBinaryUMod, SimBinarySMod, SimBinaryAnd, SimBinaryOr, SimBinaryXor,
      SimBinaryXnor
    ]> {
  let cppNamespace = "::obelisk::sim";
}

def SimLogicalAnd : I32EnumAttrCase<"And", 0, "and">;
def SimLogicalOr : I32EnumAttrCase<"Or", 1, "or">;
def SimLogicalKind : I32EnumAttr<"LogicalKind", "four-state logical operation",
                                 [SimLogicalAnd, SimLogicalOr]> {
  let cppNamespace = "::obelisk::sim";
}

def SimShiftLeft : I32EnumAttrCase<"Left", 0, "left">;
def SimShiftRight : I32EnumAttrCase<"Right", 1, "right">;
def SimShiftRightArith : I32EnumAttrCase<"RightArith", 2, "right_arith">;
def SimShiftKind
    : I32EnumAttr<"ShiftKind", "four-state shift operation",
                  [SimShiftLeft, SimShiftRight, SimShiftRightArith]> {
  let cppNamespace = "::obelisk::sim";
}

def SimCmpEq : I32EnumAttrCase<"Eq", 0, "eq">;
def SimCmpNe : I32EnumAttrCase<"Ne", 1, "ne">;
def SimCmpCaseEq : I32EnumAttrCase<"CaseEq", 2, "case_eq">;
def SimCmpCaseNe : I32EnumAttrCase<"CaseNe", 3, "case_ne">;
def SimCmpULT : I32EnumAttrCase<"ULT", 4, "ult">;
def SimCmpULE : I32EnumAttrCase<"ULE", 5, "ule">;
def SimCmpUGT : I32EnumAttrCase<"UGT", 6, "ugt">;
def SimCmpUGE : I32EnumAttrCase<"UGE", 7, "uge">;
def SimCmpSLT : I32EnumAttrCase<"SLT", 8, "slt">;
def SimCmpSLE : I32EnumAttrCase<"SLE", 9, "sle">;
def SimCmpSGT : I32EnumAttrCase<"SGT", 10, "sgt">;
def SimCmpSGE : I32EnumAttrCase<"SGE", 11, "sge">;
def SimCompareKind
    : I32EnumAttr<"CompareKind", "normalized comparison operation", [
      SimCmpEq, SimCmpNe, SimCmpCaseEq, SimCmpCaseNe, SimCmpULT, SimCmpULE,
      SimCmpUGT, SimCmpUGE, SimCmpSLT, SimCmpSLE, SimCmpSGT, SimCmpSGE
    ]> {
  let cppNamespace = "::obelisk::sim";
}

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONENUMS_TD
