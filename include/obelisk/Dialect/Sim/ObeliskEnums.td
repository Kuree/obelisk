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
def ObeliskEdge_Both     : I32EnumAttrCase<"Both",    3, "both">;
def EdgeKind : I32EnumAttr<"EdgeKind", "SystemVerilog event edge", [
  ObeliskEdge_Change, ObeliskEdge_Posedge, ObeliskEdge_Negedge,
  ObeliskEdge_Both
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
def ObeliskBinary_UDiv       : I32EnumAttrCase<"UDiv",       15, "udiv">;
def ObeliskBinary_SDiv       : I32EnumAttrCase<"SDiv",       16, "sdiv">;
def ObeliskBinary_UMod       : I32EnumAttrCase<"UMod",       17, "umod">;
def ObeliskBinary_SMod       : I32EnumAttrCase<"SMod",       18, "smod">;
def ObeliskBinary_UPow       : I32EnumAttrCase<"UPow",       19, "upow">;
def ObeliskBinary_SPow       : I32EnumAttrCase<"SPow",       20, "spow">;
def LogicBinaryKind : I32EnumAttr<"LogicBinaryKind",
    "four-state binary operator", [
  ObeliskBinary_Add, ObeliskBinary_Sub, ObeliskBinary_Mul,
  ObeliskBinary_Div, ObeliskBinary_Mod, ObeliskBinary_Pow,
  ObeliskBinary_BitAnd, ObeliskBinary_BitOr, ObeliskBinary_BitXor,
  ObeliskBinary_BitXnor, ObeliskBinary_LogicalAnd, ObeliskBinary_LogicalOr,
  ObeliskBinary_ShiftLeft, ObeliskBinary_ShiftRight,
  ObeliskBinary_AShiftRight, ObeliskBinary_UDiv, ObeliskBinary_SDiv,
  ObeliskBinary_UMod, ObeliskBinary_SMod, ObeliskBinary_UPow,
  ObeliskBinary_SPow
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
def ObeliskCompare_ULT       : I32EnumAttrCase<"ULT",       10, "ult">;
def ObeliskCompare_ULE       : I32EnumAttrCase<"ULE",       11, "ule">;
def ObeliskCompare_UGT       : I32EnumAttrCase<"UGT",       12, "ugt">;
def ObeliskCompare_UGE       : I32EnumAttrCase<"UGE",       13, "uge">;
def ObeliskCompare_SLT       : I32EnumAttrCase<"SLT",       14, "slt">;
def ObeliskCompare_SLE       : I32EnumAttrCase<"SLE",       15, "sle">;
def ObeliskCompare_SGT       : I32EnumAttrCase<"SGT",       16, "sgt">;
def ObeliskCompare_SGE       : I32EnumAttrCase<"SGE",       17, "sge">;
def ObeliskCompare_CaseZEq   : I32EnumAttrCase<"CaseZEq",   18, "casez_eq">;
def ObeliskCompare_CaseXZEq  : I32EnumAttrCase<"CaseXZEq",  19, "casexz_eq">;
def LogicCompareKind : I32EnumAttr<"LogicCompareKind",
    "four-state comparison operator", [
  ObeliskCompare_LogicalEq, ObeliskCompare_LogicalNe,
  ObeliskCompare_CaseEq, ObeliskCompare_CaseNe, ObeliskCompare_WildEq,
  ObeliskCompare_WildNe, ObeliskCompare_Lt, ObeliskCompare_Le,
  ObeliskCompare_Gt, ObeliskCompare_Ge, ObeliskCompare_ULT,
  ObeliskCompare_ULE, ObeliskCompare_UGT, ObeliskCompare_UGE,
  ObeliskCompare_SLT, ObeliskCompare_SLE, ObeliskCompare_SGT,
  ObeliskCompare_SGE, ObeliskCompare_CaseZEq, ObeliskCompare_CaseXZEq
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
def ObeliskNet_TriReg
    : I32EnumAttrCase<"TriReg", 10, "trireg">;
def ObeliskNet_UWire
    : I32EnumAttrCase<"UWire", 11, "uwire">;
def ObeliskNet_Interconnect
    : I32EnumAttrCase<"Interconnect", 12, "interconnect">;
def ObeliskNet_UserDefined
    : I32EnumAttrCase<"UserDefined", 13, "user_defined">;
def ObeliskNet_Unknown
    : I32EnumAttrCase<"Unknown", 14, "unknown">;
def NetKind : I32EnumAttr<"NetKind", "net resolution function", [
  ObeliskNet_Wire, ObeliskNet_Tri, ObeliskNet_Wand, ObeliskNet_Wor,
  ObeliskNet_TriAnd, ObeliskNet_TriOr, ObeliskNet_Tri0, ObeliskNet_Tri1,
  ObeliskNet_Supply0, ObeliskNet_Supply1, ObeliskNet_TriReg,
  ObeliskNet_UWire, ObeliskNet_Interconnect, ObeliskNet_UserDefined,
  ObeliskNet_Unknown
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

def ObeliskDPI_In     : I32EnumAttrCase<"In",     0, "in">;
def ObeliskDPI_Out    : I32EnumAttrCase<"Out",    1, "out">;
def ObeliskDPI_InOut  : I32EnumAttrCase<"InOut",  2, "inout">;
def ObeliskDPI_Return : I32EnumAttrCase<"Return", 3, "return">;
def DPIArgDirection : I32EnumAttr<"DPIArgDirection",
    "DPI argument direction", [
  ObeliskDPI_In, ObeliskDPI_Out, ObeliskDPI_InOut, ObeliskDPI_Return
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskDefer_Immediate : I32EnumAttrCase<"Immediate", 0, "immediate">;
def ObeliskDefer_Observed  : I32EnumAttrCase<"Observed",  1, "observed">;
def ObeliskDefer_Final     : I32EnumAttrCase<"Final",     2, "final">;
def DeferAssert : I32EnumAttr<"DeferAssert", "assertion deferral", [
  ObeliskDefer_Immediate, ObeliskDefer_Observed, ObeliskDefer_Final
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskFile_Read         : I32EnumAttrCase<"Read",         0, "read">;
def ObeliskFile_Write        : I32EnumAttrCase<"Write",        1, "write">;
def ObeliskFile_Append       : I32EnumAttrCase<"Append",       2, "append">;
def ObeliskFile_ReadUpdate   : I32EnumAttrCase<"ReadUpdate",   3, "read_update">;
def ObeliskFile_WriteUpdate  : I32EnumAttrCase<"WriteUpdate",  4, "write_update">;
def ObeliskFile_AppendUpdate : I32EnumAttrCase<"AppendUpdate", 5, "append_update">;
def FileOpenMode : I32EnumAttr<"FileOpenMode", "SV file open mode", [
  ObeliskFile_Read, ObeliskFile_Write, ObeliskFile_Append,
  ObeliskFile_ReadUpdate, ObeliskFile_WriteUpdate,
  ObeliskFile_AppendUpdate
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskAlign_Right : I32EnumAttrCase<"Right", 0, "right">;
def ObeliskAlign_Left  : I32EnumAttrCase<"Left",  1, "left">;
def IntegerAlignment : I32EnumAttr<"IntegerAlignment",
    "formatted integer alignment", [
  ObeliskAlign_Right, ObeliskAlign_Left
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskFormat_Decimal  : I32EnumAttrCase<"Decimal",  0, "decimal">;
def ObeliskFormat_Binary   : I32EnumAttrCase<"Binary",   1, "binary">;
def ObeliskFormat_Octal    : I32EnumAttrCase<"Octal",    2, "octal">;
def ObeliskFormat_HexLower : I32EnumAttrCase<"HexLower", 3, "hex_lower">;
def ObeliskFormat_HexUpper : I32EnumAttrCase<"HexUpper", 4, "hex_upper">;
def IntegerFormat : I32EnumAttr<"IntegerFormat", "integer format radix", [
  ObeliskFormat_Decimal, ObeliskFormat_Binary, ObeliskFormat_Octal,
  ObeliskFormat_HexLower, ObeliskFormat_HexUpper
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskPadding_Space : I32EnumAttrCase<"Space", 0, "space">;
def ObeliskPadding_Zero  : I32EnumAttrCase<"Zero",  1, "zero">;
def IntegerPadding : I32EnumAttr<"IntegerPadding",
    "formatted integer padding", [
  ObeliskPadding_Space, ObeliskPadding_Zero
]> {
  let cppNamespace = "::obelisk::ir";
}

// Complete source-level operator inventories. These are intentionally distinct
// from the normalized Logic* enums above, which are only valid after semantic
// typing and lvalue side effects have been lowered.
def ObeliskSVUnary_Plus          : I32EnumAttrCase<"Plus",          0, "plus">;
def ObeliskSVUnary_Minus         : I32EnumAttrCase<"Minus",         1, "minus">;
def ObeliskSVUnary_BitwiseNot    : I32EnumAttrCase<"BitwiseNot",    2, "bitwise_not">;
def ObeliskSVUnary_BitwiseAnd    : I32EnumAttrCase<"BitwiseAnd",    3, "bitwise_and">;
def ObeliskSVUnary_BitwiseOr     : I32EnumAttrCase<"BitwiseOr",     4, "bitwise_or">;
def ObeliskSVUnary_BitwiseXor    : I32EnumAttrCase<"BitwiseXor",    5, "bitwise_xor">;
def ObeliskSVUnary_BitwiseNand   : I32EnumAttrCase<"BitwiseNand",   6, "bitwise_nand">;
def ObeliskSVUnary_BitwiseNor    : I32EnumAttrCase<"BitwiseNor",    7, "bitwise_nor">;
def ObeliskSVUnary_BitwiseXnor   : I32EnumAttrCase<"BitwiseXnor",   8, "bitwise_xnor">;
def ObeliskSVUnary_LogicalNot    : I32EnumAttrCase<"LogicalNot",    9, "logical_not">;
def ObeliskSVUnary_Preincrement  : I32EnumAttrCase<"Preincrement", 10, "preincrement">;
def ObeliskSVUnary_Predecrement  : I32EnumAttrCase<"Predecrement", 11, "predecrement">;
def ObeliskSVUnary_Postincrement : I32EnumAttrCase<"Postincrement", 12, "postincrement">;
def ObeliskSVUnary_Postdecrement : I32EnumAttrCase<"Postdecrement", 13, "postdecrement">;
def ObeliskSVUnaryOperator : I32EnumAttr<"SVUnaryOperator",
    "complete source-level SystemVerilog unary operator", [
  ObeliskSVUnary_Plus, ObeliskSVUnary_Minus, ObeliskSVUnary_BitwiseNot,
  ObeliskSVUnary_BitwiseAnd, ObeliskSVUnary_BitwiseOr,
  ObeliskSVUnary_BitwiseXor, ObeliskSVUnary_BitwiseNand,
  ObeliskSVUnary_BitwiseNor, ObeliskSVUnary_BitwiseXnor,
  ObeliskSVUnary_LogicalNot, ObeliskSVUnary_Preincrement,
  ObeliskSVUnary_Predecrement, ObeliskSVUnary_Postincrement,
  ObeliskSVUnary_Postdecrement
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskSVBinary_Add                : I32EnumAttrCase<"Add",                 0, "add">;
def ObeliskSVBinary_Subtract           : I32EnumAttrCase<"Subtract",            1, "subtract">;
def ObeliskSVBinary_Multiply           : I32EnumAttrCase<"Multiply",            2, "multiply">;
def ObeliskSVBinary_Divide             : I32EnumAttrCase<"Divide",              3, "divide">;
def ObeliskSVBinary_Mod                : I32EnumAttrCase<"Mod",                 4, "mod">;
def ObeliskSVBinary_BinaryAnd          : I32EnumAttrCase<"BinaryAnd",           5, "binary_and">;
def ObeliskSVBinary_BinaryOr           : I32EnumAttrCase<"BinaryOr",            6, "binary_or">;
def ObeliskSVBinary_BinaryXor          : I32EnumAttrCase<"BinaryXor",           7, "binary_xor">;
def ObeliskSVBinary_BinaryXnor         : I32EnumAttrCase<"BinaryXnor",          8, "binary_xnor">;
def ObeliskSVBinary_Equality           : I32EnumAttrCase<"Equality",            9, "equality">;
def ObeliskSVBinary_Inequality         : I32EnumAttrCase<"Inequality",         10, "inequality">;
def ObeliskSVBinary_CaseEquality       : I32EnumAttrCase<"CaseEquality",       11, "case_equality">;
def ObeliskSVBinary_CaseInequality     : I32EnumAttrCase<"CaseInequality",     12, "case_inequality">;
def ObeliskSVBinary_GreaterThanEqual   : I32EnumAttrCase<"GreaterThanEqual",   13, "greater_than_equal">;
def ObeliskSVBinary_GreaterThan        : I32EnumAttrCase<"GreaterThan",        14, "greater_than">;
def ObeliskSVBinary_LessThanEqual      : I32EnumAttrCase<"LessThanEqual",      15, "less_than_equal">;
def ObeliskSVBinary_LessThan           : I32EnumAttrCase<"LessThan",           16, "less_than">;
def ObeliskSVBinary_WildcardEquality   : I32EnumAttrCase<"WildcardEquality",   17, "wildcard_equality">;
def ObeliskSVBinary_WildcardInequality : I32EnumAttrCase<"WildcardInequality", 18, "wildcard_inequality">;
def ObeliskSVBinary_LogicalAnd         : I32EnumAttrCase<"LogicalAnd",         19, "logical_and">;
def ObeliskSVBinary_LogicalOr          : I32EnumAttrCase<"LogicalOr",          20, "logical_or">;
def ObeliskSVBinary_LogicalImplication : I32EnumAttrCase<"LogicalImplication", 21, "logical_implication">;
def ObeliskSVBinary_LogicalEquivalence : I32EnumAttrCase<"LogicalEquivalence", 22, "logical_equivalence">;
def ObeliskSVBinary_LogicalShiftLeft   : I32EnumAttrCase<"LogicalShiftLeft",   23, "logical_shift_left">;
def ObeliskSVBinary_LogicalShiftRight  : I32EnumAttrCase<"LogicalShiftRight",  24, "logical_shift_right">;
def ObeliskSVBinary_ArithmeticShiftLeft
    : I32EnumAttrCase<"ArithmeticShiftLeft", 25, "arithmetic_shift_left">;
def ObeliskSVBinary_ArithmeticShiftRight
    : I32EnumAttrCase<"ArithmeticShiftRight", 26, "arithmetic_shift_right">;
def ObeliskSVBinary_Power : I32EnumAttrCase<"Power", 27, "power">;
def ObeliskSVBinaryOperator : I32EnumAttr<"SVBinaryOperator",
    "complete source-level SystemVerilog binary operator", [
  ObeliskSVBinary_Add, ObeliskSVBinary_Subtract, ObeliskSVBinary_Multiply,
  ObeliskSVBinary_Divide, ObeliskSVBinary_Mod, ObeliskSVBinary_BinaryAnd,
  ObeliskSVBinary_BinaryOr, ObeliskSVBinary_BinaryXor,
  ObeliskSVBinary_BinaryXnor, ObeliskSVBinary_Equality,
  ObeliskSVBinary_Inequality, ObeliskSVBinary_CaseEquality,
  ObeliskSVBinary_CaseInequality, ObeliskSVBinary_GreaterThanEqual,
  ObeliskSVBinary_GreaterThan, ObeliskSVBinary_LessThanEqual,
  ObeliskSVBinary_LessThan, ObeliskSVBinary_WildcardEquality,
  ObeliskSVBinary_WildcardInequality, ObeliskSVBinary_LogicalAnd,
  ObeliskSVBinary_LogicalOr, ObeliskSVBinary_LogicalImplication,
  ObeliskSVBinary_LogicalEquivalence, ObeliskSVBinary_LogicalShiftLeft,
  ObeliskSVBinary_LogicalShiftRight, ObeliskSVBinary_ArithmeticShiftLeft,
  ObeliskSVBinary_ArithmeticShiftRight, ObeliskSVBinary_Power
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskSVCheck_None     : I32EnumAttrCase<"None",     0, "none">;
def ObeliskSVCheck_Unique   : I32EnumAttrCase<"Unique",   1, "unique">;
def ObeliskSVCheck_Unique0  : I32EnumAttrCase<"Unique0",  2, "unique0">;
def ObeliskSVCheck_Priority : I32EnumAttrCase<"Priority", 3, "priority">;
def ObeliskSVUniquePriorityCheck : I32EnumAttr<"SVUniquePriorityCheck",
    "unique or priority runtime check", [
  ObeliskSVCheck_None, ObeliskSVCheck_Unique, ObeliskSVCheck_Unique0,
  ObeliskSVCheck_Priority
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskSVCase_Normal : I32EnumAttrCase<"Normal", 0, "normal">;
def ObeliskSVCase_WildcardXOrZ
    : I32EnumAttrCase<"WildcardXOrZ", 1, "wildcard_x_or_z">;
def ObeliskSVCase_WildcardJustZ
    : I32EnumAttrCase<"WildcardJustZ", 2, "wildcard_just_z">;
def ObeliskSVCase_Inside : I32EnumAttrCase<"Inside", 3, "inside">;
def ObeliskSVCaseCondition : I32EnumAttr<"SVCaseCondition",
    "SystemVerilog case matching mode", [
  ObeliskSVCase_Normal, ObeliskSVCase_WildcardXOrZ,
  ObeliskSVCase_WildcardJustZ, ObeliskSVCase_Inside
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskSVAssertion_Assert
    : I32EnumAttrCase<"Assert", 0, "assert">;
def ObeliskSVAssertion_Assume
    : I32EnumAttrCase<"Assume", 1, "assume">;
def ObeliskSVAssertion_CoverProperty
    : I32EnumAttrCase<"CoverProperty", 2, "cover_property">;
def ObeliskSVAssertion_CoverSequence
    : I32EnumAttrCase<"CoverSequence", 3, "cover_sequence">;
def ObeliskSVAssertion_Restrict
    : I32EnumAttrCase<"Restrict", 4, "restrict">;
def ObeliskSVAssertion_Expect
    : I32EnumAttrCase<"Expect", 5, "expect">;
def ObeliskSVAssertionKind : I32EnumAttr<"SVAssertionKind",
    "immediate or concurrent assertion directive kind", [
  ObeliskSVAssertion_Assert, ObeliskSVAssertion_Assume,
  ObeliskSVAssertion_CoverProperty, ObeliskSVAssertion_CoverSequence,
  ObeliskSVAssertion_Restrict, ObeliskSVAssertion_Expect
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskSVCoverageBin_Bins
    : I32EnumAttrCase<"Bins", 0, "bins">;
def ObeliskSVCoverageBin_IllegalBins
    : I32EnumAttrCase<"IllegalBins", 1, "illegal_bins">;
def ObeliskSVCoverageBin_IgnoreBins
    : I32EnumAttrCase<"IgnoreBins", 2, "ignore_bins">;
def ObeliskSVCoverageBinKind : I32EnumAttr<"SVCoverageBinKind",
    "coverage bin classification", [
  ObeliskSVCoverageBin_Bins, ObeliskSVCoverageBin_IllegalBins,
  ObeliskSVCoverageBin_IgnoreBins
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskReal_Float       : I32EnumAttrCase<"Float",       0, "float">;
def ObeliskReal_Exponential : I32EnumAttrCase<"Exponential", 1, "exponential">;
def ObeliskReal_General     : I32EnumAttrCase<"General",     2, "general">;
def RealFormat : I32EnumAttr<"RealFormat", "real number format", [
  ObeliskReal_Float, ObeliskReal_Exponential, ObeliskReal_General
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskStringCmp_Eq : I32EnumAttrCase<"Eq", 0, "eq">;
def ObeliskStringCmp_Ne : I32EnumAttrCase<"Ne", 1, "ne">;
def ObeliskStringCmp_Lt : I32EnumAttrCase<"Lt", 2, "lt">;
def ObeliskStringCmp_Le : I32EnumAttrCase<"Le", 3, "le">;
def ObeliskStringCmp_Gt : I32EnumAttrCase<"Gt", 4, "gt">;
def ObeliskStringCmp_Ge : I32EnumAttrCase<"Ge", 5, "ge">;
def StringCmpPredicate : I32EnumAttr<"StringCmpPredicate",
    "dynamic string comparison predicate", [
  ObeliskStringCmp_Eq, ObeliskStringCmp_Ne, ObeliskStringCmp_Lt,
  ObeliskStringCmp_Le, ObeliskStringCmp_Gt, ObeliskStringCmp_Ge
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskArrayCmp_Eq : I32EnumAttrCase<"Eq", 0, "eq">;
def ObeliskArrayCmp_Ne : I32EnumAttrCase<"Ne", 1, "ne">;
def ArrayCmpPredicate : I32EnumAttr<"ArrayCmpPredicate",
    "unpacked array comparison predicate", [
  ObeliskArrayCmp_Eq, ObeliskArrayCmp_Ne
]> {
  let cppNamespace = "::obelisk::ir";
}

// Source-semantic enum attributes used by the concrete obelisk.sv operation
// family. Their numeric encodings intentionally match the Slang dialect enums
// so dialect conversion can preserve the underlying builtin IntegerAttr.
def ObeliskSVAssignment_Blocking
    : I32EnumAttrCase<"Blocking", 0, "blocking">;
def ObeliskSVAssignment_Nonblocking
    : I32EnumAttrCase<"Nonblocking", 1, "nonblocking">;
def ObeliskSVAssignmentKind : I32EnumAttr<"SVAssignmentKind",
    "SystemVerilog assignment scheduling kind", [
  ObeliskSVAssignment_Blocking, ObeliskSVAssignment_Nonblocking
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskSVDirection_In
    : I32EnumAttrCase<"In", 0, "in">;
def ObeliskSVDirection_Out
    : I32EnumAttrCase<"Out", 1, "out">;
def ObeliskSVDirection_InOut
    : I32EnumAttrCase<"InOut", 2, "inout">;
def ObeliskSVDirection_Ref
    : I32EnumAttrCase<"Ref", 3, "ref">;
def ObeliskSVArgumentDirection : I32EnumAttr<"SVArgumentDirection",
    "SystemVerilog port or argument direction", [
  ObeliskSVDirection_In, ObeliskSVDirection_Out,
  ObeliskSVDirection_InOut, ObeliskSVDirection_Ref
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskSVDefinition_Module
    : I32EnumAttrCase<"Module", 0, "module">;
def ObeliskSVDefinition_Interface
    : I32EnumAttrCase<"Interface", 1, "interface">;
def ObeliskSVDefinition_Program
    : I32EnumAttrCase<"Program", 2, "program">;
def ObeliskSVDefinitionKind : I32EnumAttr<"SVDefinitionKind",
    "SystemVerilog design-unit definition kind", [
  ObeliskSVDefinition_Module, ObeliskSVDefinition_Interface,
  ObeliskSVDefinition_Program
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskSVProcedure_Initial
    : I32EnumAttrCase<"Initial", 0, "initial">;
def ObeliskSVProcedure_Final
    : I32EnumAttrCase<"Final", 1, "final">;
def ObeliskSVProcedure_Always
    : I32EnumAttrCase<"Always", 2, "always">;
def ObeliskSVProcedure_AlwaysComb
    : I32EnumAttrCase<"AlwaysComb", 3, "always_comb">;
def ObeliskSVProcedure_AlwaysLatch
    : I32EnumAttrCase<"AlwaysLatch", 4, "always_latch">;
def ObeliskSVProcedure_AlwaysFF
    : I32EnumAttrCase<"AlwaysFF", 5, "always_ff">;
def ObeliskSVProceduralBlockKind : I32EnumAttr<"SVProceduralBlockKind",
    "SystemVerilog procedural block kind", [
  ObeliskSVProcedure_Initial, ObeliskSVProcedure_Final,
  ObeliskSVProcedure_Always, ObeliskSVProcedure_AlwaysComb,
  ObeliskSVProcedure_AlwaysLatch, ObeliskSVProcedure_AlwaysFF
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskSVBlock_Sequential
    : I32EnumAttrCase<"Sequential", 0, "sequential">;
def ObeliskSVBlock_JoinAll
    : I32EnumAttrCase<"JoinAll", 1, "join_all">;
def ObeliskSVBlock_JoinAny
    : I32EnumAttrCase<"JoinAny", 2, "join_any">;
def ObeliskSVBlock_JoinNone
    : I32EnumAttrCase<"JoinNone", 3, "join_none">;
def ObeliskSVStatementBlockKind : I32EnumAttr<"SVStatementBlockKind",
    "SystemVerilog sequential or parallel statement block kind", [
  ObeliskSVBlock_Sequential, ObeliskSVBlock_JoinAll,
  ObeliskSVBlock_JoinAny, ObeliskSVBlock_JoinNone
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskSVSubroutine_Function
    : I32EnumAttrCase<"Function", 0, "function">;
def ObeliskSVSubroutine_Task
    : I32EnumAttrCase<"Task", 1, "task">;
def ObeliskSVSubroutineKind : I32EnumAttr<"SVSubroutineKind",
    "SystemVerilog function or task kind", [
  ObeliskSVSubroutine_Function, ObeliskSVSubroutine_Task
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskSVLifetime_Automatic
    : I32EnumAttrCase<"Automatic", 0, "automatic">;
def ObeliskSVLifetime_Static
    : I32EnumAttrCase<"Static", 1, "static">;
def ObeliskSVVariableLifetime : I32EnumAttr<"SVVariableLifetime",
    "SystemVerilog variable storage lifetime", [
  ObeliskSVLifetime_Automatic, ObeliskSVLifetime_Static
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskSVVisibility_Public
    : I32EnumAttrCase<"Public", 0, "public">;
def ObeliskSVVisibility_Protected
    : I32EnumAttrCase<"Protected", 1, "protected">;
def ObeliskSVVisibility_Local
    : I32EnumAttrCase<"Local", 2, "local">;
def ObeliskSVVisibility : I32EnumAttr<"SVVisibility",
    "SystemVerilog class member visibility", [
  ObeliskSVVisibility_Public, ObeliskSVVisibility_Protected,
  ObeliskSVVisibility_Local
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskSVRand_None  : I32EnumAttrCase<"None",  0, "none">;
def ObeliskSVRand_Rand  : I32EnumAttrCase<"Rand",  1, "rand">;
def ObeliskSVRand_RandC : I32EnumAttrCase<"RandC", 2, "randc">;
def ObeliskSVRandMode : I32EnumAttr<"SVRandMode",
    "SystemVerilog randomization mode", [
  ObeliskSVRand_None, ObeliskSVRand_Rand, ObeliskSVRand_RandC
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskSVIntegral_Generic
    : I32EnumAttrCase<"Generic", 0, "generic">;
def ObeliskSVIntegral_Bit : I32EnumAttrCase<"Bit", 1, "bit">;
def ObeliskSVIntegral_Logic : I32EnumAttrCase<"Logic", 2, "logic">;
def ObeliskSVIntegral_Reg : I32EnumAttrCase<"Reg", 3, "reg">;
def ObeliskSVIntegral_ShortInt
    : I32EnumAttrCase<"ShortInt", 4, "shortint">;
def ObeliskSVIntegral_Int : I32EnumAttrCase<"Int", 5, "int">;
def ObeliskSVIntegral_LongInt
    : I32EnumAttrCase<"LongInt", 6, "longint">;
def ObeliskSVIntegral_Byte : I32EnumAttrCase<"Byte", 7, "byte">;
def ObeliskSVIntegral_Integer
    : I32EnumAttrCase<"Integer", 8, "integer">;
def ObeliskSVIntegralFlavor : I32EnumAttr<"SVIntegralFlavor",
    "declared SystemVerilog integral spelling", [
  ObeliskSVIntegral_Generic, ObeliskSVIntegral_Bit,
  ObeliskSVIntegral_Logic, ObeliskSVIntegral_Reg,
  ObeliskSVIntegral_ShortInt, ObeliskSVIntegral_Int,
  ObeliskSVIntegral_LongInt, ObeliskSVIntegral_Byte,
  ObeliskSVIntegral_Integer
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskSVNet_Unknown : I32EnumAttrCase<"Unknown", 0, "unknown">;
def ObeliskSVNet_Wire : I32EnumAttrCase<"Wire", 1, "wire">;
def ObeliskSVNet_WAnd : I32EnumAttrCase<"WAnd", 2, "wand">;
def ObeliskSVNet_WOr : I32EnumAttrCase<"WOr", 3, "wor">;
def ObeliskSVNet_Tri : I32EnumAttrCase<"Tri", 4, "tri">;
def ObeliskSVNet_TriAnd : I32EnumAttrCase<"TriAnd", 5, "triand">;
def ObeliskSVNet_TriOr : I32EnumAttrCase<"TriOr", 6, "trior">;
def ObeliskSVNet_Tri0 : I32EnumAttrCase<"Tri0", 7, "tri0">;
def ObeliskSVNet_Tri1 : I32EnumAttrCase<"Tri1", 8, "tri1">;
def ObeliskSVNet_TriReg : I32EnumAttrCase<"TriReg", 9, "trireg">;
def ObeliskSVNet_Supply0 : I32EnumAttrCase<"Supply0", 10, "supply0">;
def ObeliskSVNet_Supply1 : I32EnumAttrCase<"Supply1", 11, "supply1">;
def ObeliskSVNet_UWire : I32EnumAttrCase<"UWire", 12, "uwire">;
def ObeliskSVNet_Interconnect
    : I32EnumAttrCase<"Interconnect", 13, "interconnect">;
def ObeliskSVNet_UserDefined
    : I32EnumAttrCase<"UserDefined", 14, "user_defined">;
def ObeliskSVNetKind : I32EnumAttr<"SVNetKind",
    "SystemVerilog net resolution kind", [
  ObeliskSVNet_Unknown, ObeliskSVNet_Wire, ObeliskSVNet_WAnd,
  ObeliskSVNet_WOr, ObeliskSVNet_Tri, ObeliskSVNet_TriAnd,
  ObeliskSVNet_TriOr, ObeliskSVNet_Tri0, ObeliskSVNet_Tri1,
  ObeliskSVNet_TriReg, ObeliskSVNet_Supply0, ObeliskSVNet_Supply1,
  ObeliskSVNet_UWire, ObeliskSVNet_Interconnect,
  ObeliskSVNet_UserDefined
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskSVAssertUnary_Not : I32EnumAttrCase<"Not", 0, "not">;
def ObeliskSVAssertUnary_NextTime : I32EnumAttrCase<"NextTime", 1, "nexttime">;
def ObeliskSVAssertUnary_SNextTime : I32EnumAttrCase<"SNextTime", 2, "s_nexttime">;
def ObeliskSVAssertUnary_Always : I32EnumAttrCase<"Always", 3, "always">;
def ObeliskSVAssertUnary_SAlways : I32EnumAttrCase<"SAlways", 4, "s_always">;
def ObeliskSVAssertUnary_Eventually : I32EnumAttrCase<"Eventually", 5, "eventually">;
def ObeliskSVAssertUnary_SEventually
    : I32EnumAttrCase<"SEventually", 6, "s_eventually">;
def ObeliskSVAssertionUnaryOperator : I32EnumAttr<"SVAssertionUnaryOperator",
    "SystemVerilog unary sequence or property operator", [
  ObeliskSVAssertUnary_Not, ObeliskSVAssertUnary_NextTime,
  ObeliskSVAssertUnary_SNextTime, ObeliskSVAssertUnary_Always,
  ObeliskSVAssertUnary_SAlways, ObeliskSVAssertUnary_Eventually,
  ObeliskSVAssertUnary_SEventually
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskSVAssertBinary_And : I32EnumAttrCase<"And", 0, "and">;
def ObeliskSVAssertBinary_Or : I32EnumAttrCase<"Or", 1, "or">;
def ObeliskSVAssertBinary_Intersect : I32EnumAttrCase<"Intersect", 2, "intersect">;
def ObeliskSVAssertBinary_Throughout : I32EnumAttrCase<"Throughout", 3, "throughout">;
def ObeliskSVAssertBinary_Within : I32EnumAttrCase<"Within", 4, "within">;
def ObeliskSVAssertBinary_Iff : I32EnumAttrCase<"Iff", 5, "iff">;
def ObeliskSVAssertBinary_Until : I32EnumAttrCase<"Until", 6, "until">;
def ObeliskSVAssertBinary_SUntil : I32EnumAttrCase<"SUntil", 7, "s_until">;
def ObeliskSVAssertBinary_UntilWith : I32EnumAttrCase<"UntilWith", 8, "until_with">;
def ObeliskSVAssertBinary_SUntilWith : I32EnumAttrCase<"SUntilWith", 9, "s_until_with">;
def ObeliskSVAssertBinary_Implies : I32EnumAttrCase<"Implies", 10, "implies">;
def ObeliskSVAssertBinary_OverlappedImplication
    : I32EnumAttrCase<"OverlappedImplication", 11, "overlapped_implication">;
def ObeliskSVAssertBinary_NonOverlappedImplication
    : I32EnumAttrCase<"NonOverlappedImplication", 12,
                      "nonoverlapped_implication">;
def ObeliskSVAssertBinary_OverlappedFollowedBy
    : I32EnumAttrCase<"OverlappedFollowedBy", 13, "overlapped_followed_by">;
def ObeliskSVAssertBinary_NonOverlappedFollowedBy
    : I32EnumAttrCase<"NonOverlappedFollowedBy", 14,
                      "nonoverlapped_followed_by">;
def ObeliskSVAssertionBinaryOperator : I32EnumAttr<"SVAssertionBinaryOperator",
    "SystemVerilog binary sequence or property operator", [
  ObeliskSVAssertBinary_And, ObeliskSVAssertBinary_Or,
  ObeliskSVAssertBinary_Intersect, ObeliskSVAssertBinary_Throughout,
  ObeliskSVAssertBinary_Within, ObeliskSVAssertBinary_Iff,
  ObeliskSVAssertBinary_Until, ObeliskSVAssertBinary_SUntil,
  ObeliskSVAssertBinary_UntilWith, ObeliskSVAssertBinary_SUntilWith,
  ObeliskSVAssertBinary_Implies,
  ObeliskSVAssertBinary_OverlappedImplication,
  ObeliskSVAssertBinary_NonOverlappedImplication,
  ObeliskSVAssertBinary_OverlappedFollowedBy,
  ObeliskSVAssertBinary_NonOverlappedFollowedBy
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskSVRepetition_Consecutive
    : I32EnumAttrCase<"Consecutive", 0, "consecutive">;
def ObeliskSVRepetition_Nonconsecutive
    : I32EnumAttrCase<"Nonconsecutive", 1, "nonconsecutive">;
def ObeliskSVRepetition_GoTo
    : I32EnumAttrCase<"GoTo", 2, "goto">;
def ObeliskSVSequenceRepetitionKind
    : I32EnumAttr<"SVSequenceRepetitionKind",
                  "SystemVerilog sequence repetition kind", [
  ObeliskSVRepetition_Consecutive, ObeliskSVRepetition_Nonconsecutive,
  ObeliskSVRepetition_GoTo
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskSVStrength_Strong : I32EnumAttrCase<"Strong", 0, "strong">;
def ObeliskSVStrength_Weak : I32EnumAttrCase<"Weak", 1, "weak">;
def ObeliskSVAssertionStrength : I32EnumAttr<"SVAssertionStrength",
    "strong or weak property qualification", [
  ObeliskSVStrength_Strong, ObeliskSVStrength_Weak
]> {
  let cppNamespace = "::obelisk::ir";
}

def ObeliskSVAbort_Accept : I32EnumAttrCase<"Accept", 0, "accept">;
def ObeliskSVAbort_Reject : I32EnumAttrCase<"Reject", 1, "reject">;
def ObeliskSVAssertionAbortAction : I32EnumAttr<"SVAssertionAbortAction",
    "accept or reject property abort action", [
  ObeliskSVAbort_Accept, ObeliskSVAbort_Reject
]> {
  let cppNamespace = "::obelisk::ir";
}

#endif // OBELISK_DIALECT_SIM_OBELISKENUMS_TD
