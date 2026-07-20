//===- ObeliskEnums.td - Obelisk semantic enumerations -----*- tablegen -*-===//

#ifndef OBELISK_DIALECT_SIM_OBELISKENUMS_TD
#define OBELISK_DIALECT_SIM_OBELISKENUMS_TD

include "mlir/IR/EnumAttr.td"

// IEEE 1800-2023 event regions, in scheduling order.  We keep every region
// distinct even when an optimization later proves that adjacent queues may be
// coalesced.
def ObeliskRegion_Preponed     : I32EnumAttrCase<"Preponed",     0,  "preponed">;
def ObeliskRegion_PreActive    : I32EnumAttrCase<"PreActive",    1,  "pre_active">;
def ObeliskRegion_Active       : I32EnumAttrCase<"Active",       2,  "active">;
def ObeliskRegion_Inactive     : I32EnumAttrCase<"Inactive",     3,  "inactive">;
def ObeliskRegion_PreNBA       : I32EnumAttrCase<"PreNBA",       4,  "pre_nba">;
def ObeliskRegion_NBA          : I32EnumAttrCase<"NBA",          5,  "nba">;
def ObeliskRegion_PostNBA      : I32EnumAttrCase<"PostNBA",      6,  "post_nba">;
def ObeliskRegion_PreObserved  : I32EnumAttrCase<"PreObserved",  7,  "pre_observed">;
def ObeliskRegion_Observed     : I32EnumAttrCase<"Observed",     8,  "observed">;
def ObeliskRegion_PostObserved : I32EnumAttrCase<"PostObserved", 9,  "post_observed">;
def ObeliskRegion_Reactive     : I32EnumAttrCase<"Reactive",     10, "reactive">;
def ObeliskRegion_ReInactive   : I32EnumAttrCase<"ReInactive",   11, "re_inactive">;
def ObeliskRegion_PreReNBA     : I32EnumAttrCase<"PreReNBA",     12, "pre_re_nba">;
def ObeliskRegion_ReNBA        : I32EnumAttrCase<"ReNBA",        13, "re_nba">;
def ObeliskRegion_PostReNBA    : I32EnumAttrCase<"PostReNBA",    14, "post_re_nba">;
def ObeliskRegion_PrePostponed : I32EnumAttrCase<"PrePostponed", 15, "pre_postponed">;
def ObeliskRegion_Postponed    : I32EnumAttrCase<"Postponed",    16, "postponed">;

def EventRegion : I32EnumAttr<"EventRegion", "IEEE 1800 event region", [
  ObeliskRegion_Preponed, ObeliskRegion_PreActive, ObeliskRegion_Active,
  ObeliskRegion_Inactive, ObeliskRegion_PreNBA, ObeliskRegion_NBA,
  ObeliskRegion_PostNBA, ObeliskRegion_PreObserved, ObeliskRegion_Observed,
  ObeliskRegion_PostObserved, ObeliskRegion_Reactive,
  ObeliskRegion_ReInactive, ObeliskRegion_PreReNBA, ObeliskRegion_ReNBA,
  ObeliskRegion_PostReNBA, ObeliskRegion_PrePostponed,
  ObeliskRegion_Postponed
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskEdge_Change  : I32EnumAttrCase<"Change",  0, "change">;
def ObeliskEdge_Posedge : I32EnumAttrCase<"Posedge", 1, "posedge">;
def ObeliskEdge_Negedge : I32EnumAttrCase<"Negedge", 2, "negedge">;
def EdgeKind : I32EnumAttr<"EdgeKind", "SystemVerilog event edge", [
  ObeliskEdge_Change, ObeliskEdge_Posedge, ObeliskEdge_Negedge
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskProcess_Initial     : I32EnumAttrCase<"Initial",     0, "initial">;
def ObeliskProcess_Always      : I32EnumAttrCase<"Always",      1, "always">;
def ObeliskProcess_AlwaysComb  : I32EnumAttrCase<"AlwaysComb",  2, "always_comb">;
def ObeliskProcess_AlwaysFF    : I32EnumAttrCase<"AlwaysFF",    3, "always_ff">;
def ObeliskProcess_AlwaysLatch : I32EnumAttrCase<"AlwaysLatch", 4, "always_latch">;
def ObeliskProcess_Final       : I32EnumAttrCase<"Final",       5, "final">;
def ObeliskProcess_Task        : I32EnumAttrCase<"Task",        6, "task">;
def ObeliskProcess_Fork        : I32EnumAttrCase<"Fork",        7, "fork">;
def ProcessKind : I32EnumAttr<"ProcessKind", "simulation process kind", [
  ObeliskProcess_Initial, ObeliskProcess_Always, ObeliskProcess_AlwaysComb,
  ObeliskProcess_AlwaysFF, ObeliskProcess_AlwaysLatch, ObeliskProcess_Final,
  ObeliskProcess_Task, ObeliskProcess_Fork
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskJoin_All  : I32EnumAttrCase<"All",  0, "all">;
def ObeliskJoin_Any  : I32EnumAttrCase<"Any",  1, "any">;
def ObeliskJoin_None : I32EnumAttrCase<"None", 2, "none">;
def JoinKind : I32EnumAttr<"JoinKind", "fork/join completion policy", [
  ObeliskJoin_All, ObeliskJoin_Any, ObeliskJoin_None
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskUnary_Plus       : I32EnumAttrCase<"Plus",       0, "plus">;
def ObeliskUnary_Negate     : I32EnumAttrCase<"Negate",     1, "negate">;
def ObeliskUnary_BitNot     : I32EnumAttrCase<"BitNot",     2, "bit_not">;
def ObeliskUnary_LogicalNot : I32EnumAttrCase<"LogicalNot", 3, "logical_not">;
def LogicUnaryKind : I32EnumAttr<"LogicUnaryKind",
    "four-state unary operator", [
  ObeliskUnary_Plus, ObeliskUnary_Negate, ObeliskUnary_BitNot,
  ObeliskUnary_LogicalNot
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskBinary_Add        : I32EnumAttrCase<"Add",        0,  "add">;
def ObeliskBinary_Sub        : I32EnumAttrCase<"Sub",        1,  "sub">;
def ObeliskBinary_Mul        : I32EnumAttrCase<"Mul",        2,  "mul">;
def ObeliskBinary_Div        : I32EnumAttrCase<"Div",        3,  "div">;
def ObeliskBinary_Mod        : I32EnumAttrCase<"Mod",        4,  "mod">;
def ObeliskBinary_Pow        : I32EnumAttrCase<"Pow",        5,  "pow">;
def ObeliskBinary_BitAnd     : I32EnumAttrCase<"BitAnd",     6,  "bit_and">;
def ObeliskBinary_BitOr      : I32EnumAttrCase<"BitOr",      7,  "bit_or">;
def ObeliskBinary_BitXor     : I32EnumAttrCase<"BitXor",     8,  "bit_xor">;
def ObeliskBinary_BitXnor    : I32EnumAttrCase<"BitXnor",    9,  "bit_xnor">;
def ObeliskBinary_LogicalAnd : I32EnumAttrCase<"LogicalAnd", 10, "logical_and">;
def ObeliskBinary_LogicalOr  : I32EnumAttrCase<"LogicalOr",  11, "logical_or">;
def ObeliskBinary_ShiftLeft  : I32EnumAttrCase<"ShiftLeft",  12, "shift_left">;
def ObeliskBinary_ShiftRight : I32EnumAttrCase<"ShiftRight", 13, "shift_right">;
def ObeliskBinary_AShiftRight
    : I32EnumAttrCase<"AShiftRight", 14, "ashift_right">;
def LogicBinaryKind : I32EnumAttr<"LogicBinaryKind",
    "four-state binary operator", [
  ObeliskBinary_Add, ObeliskBinary_Sub, ObeliskBinary_Mul,
  ObeliskBinary_Div, ObeliskBinary_Mod, ObeliskBinary_Pow,
  ObeliskBinary_BitAnd, ObeliskBinary_BitOr, ObeliskBinary_BitXor,
  ObeliskBinary_BitXnor, ObeliskBinary_LogicalAnd, ObeliskBinary_LogicalOr,
  ObeliskBinary_ShiftLeft, ObeliskBinary_ShiftRight,
  ObeliskBinary_AShiftRight
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskCompare_LogicalEq : I32EnumAttrCase<"LogicalEq", 0, "logical_eq">;
def ObeliskCompare_LogicalNe : I32EnumAttrCase<"LogicalNe", 1, "logical_ne">;
def ObeliskCompare_CaseEq    : I32EnumAttrCase<"CaseEq",    2, "case_eq">;
def ObeliskCompare_CaseNe    : I32EnumAttrCase<"CaseNe",    3, "case_ne">;
def ObeliskCompare_WildEq    : I32EnumAttrCase<"WildEq",    4, "wild_eq">;
def ObeliskCompare_WildNe    : I32EnumAttrCase<"WildNe",    5, "wild_ne">;
def ObeliskCompare_Lt        : I32EnumAttrCase<"Lt",        6, "lt">;
def ObeliskCompare_Le        : I32EnumAttrCase<"Le",        7, "le">;
def ObeliskCompare_Gt        : I32EnumAttrCase<"Gt",        8, "gt">;
def ObeliskCompare_Ge        : I32EnumAttrCase<"Ge",        9, "ge">;
def LogicCompareKind : I32EnumAttr<"LogicCompareKind",
    "four-state comparison operator", [
  ObeliskCompare_LogicalEq, ObeliskCompare_LogicalNe,
  ObeliskCompare_CaseEq, ObeliskCompare_CaseNe, ObeliskCompare_WildEq,
  ObeliskCompare_WildNe, ObeliskCompare_Lt, ObeliskCompare_Le,
  ObeliskCompare_Gt, ObeliskCompare_Ge
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskReduce_And  : I32EnumAttrCase<"And",  0, "and">;
def ObeliskReduce_Or   : I32EnumAttrCase<"Or",   1, "or">;
def ObeliskReduce_Xor  : I32EnumAttrCase<"Xor",  2, "xor">;
def ObeliskReduce_Nand : I32EnumAttrCase<"Nand", 3, "nand">;
def ObeliskReduce_Nor  : I32EnumAttrCase<"Nor",  4, "nor">;
def ObeliskReduce_Xnor : I32EnumAttrCase<"Xnor", 5, "xnor">;
def LogicReduceKind : I32EnumAttr<"LogicReduceKind",
    "four-state reduction operator", [
  ObeliskReduce_And, ObeliskReduce_Or, ObeliskReduce_Xor,
  ObeliskReduce_Nand, ObeliskReduce_Nor, ObeliskReduce_Xnor
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskNet_Wire   : I32EnumAttrCase<"Wire",   0, "wire">;
def ObeliskNet_Tri    : I32EnumAttrCase<"Tri",    1, "tri">;
def ObeliskNet_Wand   : I32EnumAttrCase<"Wand",   2, "wand">;
def ObeliskNet_Wor    : I32EnumAttrCase<"Wor",    3, "wor">;
def ObeliskNet_TriAnd : I32EnumAttrCase<"TriAnd", 4, "triand">;
def ObeliskNet_TriOr  : I32EnumAttrCase<"TriOr",  5, "trior">;
def ObeliskNet_Tri0   : I32EnumAttrCase<"Tri0",   6, "tri0">;
def ObeliskNet_Tri1   : I32EnumAttrCase<"Tri1",   7, "tri1">;
def ObeliskNet_Supply0
    : I32EnumAttrCase<"Supply0", 8, "supply0">;
def ObeliskNet_Supply1
    : I32EnumAttrCase<"Supply1", 9, "supply1">;
def NetKind : I32EnumAttr<"NetKind", "net resolution function", [
  ObeliskNet_Wire, ObeliskNet_Tri, ObeliskNet_Wand, ObeliskNet_Wor,
  ObeliskNet_TriAnd, ObeliskNet_TriOr, ObeliskNet_Tri0, ObeliskNet_Tri1,
  ObeliskNet_Supply0, ObeliskNet_Supply1
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskSeverity_Info    : I32EnumAttrCase<"Info",    0, "info">;
def ObeliskSeverity_Warning : I32EnumAttrCase<"Warning", 1, "warning">;
def ObeliskSeverity_Error   : I32EnumAttrCase<"Error",   2, "error">;
def ObeliskSeverity_Fatal   : I32EnumAttrCase<"Fatal",   3, "fatal">;
def Severity : I32EnumAttr<"Severity", "diagnostic severity", [
  ObeliskSeverity_Info, ObeliskSeverity_Warning, ObeliskSeverity_Error,
  ObeliskSeverity_Fatal
]> {
  let cppNamespace = "::obelisk::ir";
}

#endif // OBELISK_DIALECT_SIM_OBELISKENUMS_TD
