//===- SDFEnums.td - Normalized SDF enumerations ---------*- tablegen -*-===//

#ifndef OBELISK_DIALECT_SDF_SDFENUMS_TD
#define OBELISK_DIALECT_SDF_SDFENUMS_TD

include "mlir/IR/EnumAttr.td"

def SDFModeAbsolute : I32EnumAttrCase<"Absolute", 0, "absolute">;
def SDFModeIncrement : I32EnumAttrCase<"Increment", 1, "increment">;
def SDFDelayMode : I32EnumAttr<"DelayMode", "SDF delay annotation mode", [
  SDFModeAbsolute, SDFModeIncrement
]> { let cppNamespace = "::obelisk::sdf"; }

def SDFPathIOPath : I32EnumAttrCase<"IOPath", 0, "iopath">;
def SDFPathCond : I32EnumAttrCase<"Cond", 1, "cond">;
def SDFPathCondElse : I32EnumAttrCase<"CondElse", 2, "condelse">;
def SDFPathKind : I32EnumAttr<"PathKind", "SDF module-path record kind", [
  SDFPathIOPath, SDFPathCond, SDFPathCondElse
]> { let cppNamespace = "::obelisk::sdf"; }

def SDFTerminalPort : I32EnumAttrCase<"Port", 0, "port">;
def SDFTerminalNetDelay : I32EnumAttrCase<"NetDelay", 1, "netdelay">;
def SDFTerminalDevice : I32EnumAttrCase<"Device", 2, "device">;
def SDFTerminalDelayKind
    : I32EnumAttr<"TerminalDelayKind", "terminal delay record kind", [
        SDFTerminalPort, SDFTerminalNetDelay, SDFTerminalDevice
      ]> { let cppNamespace = "::obelisk::sdf"; }

def SDFPulseAbsolute : I32EnumAttrCase<"Absolute", 0, "pathpulse">;
def SDFPulsePercent : I32EnumAttrCase<"Percent", 1, "pathpulsepercent">;
def SDFPulseKind : I32EnumAttr<"PulseKind", "path pulse limit kind", [
  SDFPulseAbsolute, SDFPulsePercent
]> { let cppNamespace = "::obelisk::sdf"; }

def SDFTCSetup : I32EnumAttrCase<"Setup", 0, "setup">;
def SDFTCHold : I32EnumAttrCase<"Hold", 1, "hold">;
def SDFTCSetupHold : I32EnumAttrCase<"SetupHold", 2, "setuphold">;
def SDFTCRecovery : I32EnumAttrCase<"Recovery", 3, "recovery">;
def SDFTCRemoval : I32EnumAttrCase<"Removal", 4, "removal">;
def SDFTCRecrem : I32EnumAttrCase<"Recrem", 5, "recrem">;
def SDFTCSkew : I32EnumAttrCase<"Skew", 6, "skew">;
def SDFTCBidirectSkew
    : I32EnumAttrCase<"BidirectSkew", 7, "bidirectskew">;
def SDFTCPeriod : I32EnumAttrCase<"Period", 8, "period">;
def SDFTCWidth : I32EnumAttrCase<"Width", 9, "width">;
def SDFTCNoChange : I32EnumAttrCase<"NoChange", 10, "nochange">;
def SDFTimingCheckKind
    : I32EnumAttr<"TimingCheckKind", "SDF timing-check record kind", [
        SDFTCSetup, SDFTCHold, SDFTCSetupHold, SDFTCRecovery, SDFTCRemoval,
        SDFTCRecrem, SDFTCSkew, SDFTCBidirectSkew, SDFTCPeriod, SDFTCWidth,
        SDFTCNoChange
      ]> { let cppNamespace = "::obelisk::sdf"; }

def SDFEdgeNone : I32EnumAttrCase<"None", 0, "none">;
def SDFEdgePosedge : I32EnumAttrCase<"Posedge", 1, "posedge">;
def SDFEdgeNegedge : I32EnumAttrCase<"Negedge", 2, "negedge">;
def SDFEdge01 : I32EnumAttrCase<"ZeroOne", 3, "01">;
def SDFEdge10 : I32EnumAttrCase<"OneZero", 4, "10">;
def SDFEdge0Z : I32EnumAttrCase<"ZeroZ", 5, "0z">;
def SDFEdgeZ1 : I32EnumAttrCase<"ZOne", 6, "z1">;
def SDFEdge1Z : I32EnumAttrCase<"OneZ", 7, "1z">;
def SDFEdgeZ0 : I32EnumAttrCase<"ZZero", 8, "z0">;
def SDFEdge : I32EnumAttr<"Edge", "SDF port edge identifier", [
  SDFEdgeNone, SDFEdgePosedge, SDFEdgeNegedge, SDFEdge01, SDFEdge10,
  SDFEdge0Z, SDFEdgeZ1, SDFEdge1Z, SDFEdgeZ0
]> { let cppNamespace = "::obelisk::sdf"; }

def SDFUnitS : I32EnumAttrCase<"Seconds", 0, "s">;
def SDFUnitMS : I32EnumAttrCase<"Milliseconds", 1, "ms">;
def SDFUnitUS : I32EnumAttrCase<"Microseconds", 2, "us">;
def SDFUnitNS : I32EnumAttrCase<"Nanoseconds", 3, "ns">;
def SDFUnitPS : I32EnumAttrCase<"Picoseconds", 4, "ps">;
def SDFUnitFS : I32EnumAttrCase<"Femtoseconds", 5, "fs">;
def SDFTimeUnit : I32EnumAttr<"TimeUnit", "SDF TIMESCALE unit", [
  SDFUnitS, SDFUnitMS, SDFUnitUS, SDFUnitNS, SDFUnitPS, SDFUnitFS
]> { let cppNamespace = "::obelisk::sdf"; }

def SDFCondPort : I32EnumAttrCase<"Port", 0, "port">;
def SDFCondConstant : I32EnumAttrCase<"Constant", 1, "constant">;
def SDFCondNot : I32EnumAttrCase<"Not", 2, "not">;
def SDFCondAnd : I32EnumAttrCase<"And", 3, "and">;
def SDFCondOr : I32EnumAttrCase<"Or", 4, "or">;
def SDFCondXor : I32EnumAttrCase<"Xor", 5, "xor">;
def SDFCondEq : I32EnumAttrCase<"Eq", 6, "eq">;
def SDFCondNe : I32EnumAttrCase<"Ne", 7, "ne">;
def SDFCondCaseEq : I32EnumAttrCase<"CaseEq", 8, "caseeq">;
def SDFCondCaseNe : I32EnumAttrCase<"CaseNe", 9, "casene">;
def SDFConditionOpcode
    : I32EnumAttr<"ConditionOpcode", "postfix SDF condition opcode", [
        SDFCondPort, SDFCondConstant, SDFCondNot, SDFCondAnd, SDFCondOr,
        SDFCondXor, SDFCondEq, SDFCondNe, SDFCondCaseEq, SDFCondCaseNe
      ]> { let cppNamespace = "::obelisk::sdf"; }

def SDFDelayEmpty : I32EnumAttrCase<"Empty", 0, "empty">;
def SDFDelayScalar : I32EnumAttrCase<"Scalar", 1, "scalar">;
def SDFDelayTriple : I32EnumAttrCase<"Triple", 2, "triple">;
def SDFDelayValueForm
    : I32EnumAttr<"DelayValueForm", "delay value surface form", [
        SDFDelayEmpty, SDFDelayScalar, SDFDelayTriple
      ]> { let cppNamespace = "::obelisk::sdf"; }

#endif // OBELISK_DIALECT_SDF_SDFENUMS_TD
