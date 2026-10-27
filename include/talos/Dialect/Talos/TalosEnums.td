//===- TalosEnums.td - Talos semantic enumerations -----*- tablegen -*-===//

#ifndef TALOS_DIALECT_TALOS_TALOSENUMS_TD
#define TALOS_DIALECT_TALOS_TALOSENUMS_TD

include "mlir/IR/EnumAttr.td"
include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/BuiltinAttributes.td"

// IEEE 1800-2023 event regions, in scheduling order.  We keep every region
// distinct even when an optimization later proves that adjacent queues may be
// coalesced.
def TalosRegion_Preponed     : I32EnumAttrCase<"Preponed",     0,  "preponed">;
def TalosRegion_PreActive    : I32EnumAttrCase<"PreActive",    1,  "pre_active">;
def TalosRegion_Active       : I32EnumAttrCase<"Active",       2,  "active">;
def TalosRegion_Inactive     : I32EnumAttrCase<"Inactive",     3,  "inactive">;
def TalosRegion_PreNBA       : I32EnumAttrCase<"PreNBA",       4,  "pre_nba">;
def TalosRegion_NBA          : I32EnumAttrCase<"NBA",          5,  "nba">;
def TalosRegion_PostNBA      : I32EnumAttrCase<"PostNBA",      6,  "post_nba">;
def TalosRegion_PreObserved  : I32EnumAttrCase<"PreObserved",  7,  "pre_observed">;
def TalosRegion_Observed     : I32EnumAttrCase<"Observed",     8,  "observed">;
def TalosRegion_PostObserved : I32EnumAttrCase<"PostObserved", 9,  "post_observed">;
def TalosRegion_Reactive     : I32EnumAttrCase<"Reactive",     10, "reactive">;
def TalosRegion_ReInactive   : I32EnumAttrCase<"ReInactive",   11, "re_inactive">;
def TalosRegion_PreReNBA     : I32EnumAttrCase<"PreReNBA",     12, "pre_re_nba">;
def TalosRegion_ReNBA        : I32EnumAttrCase<"ReNBA",        13, "re_nba">;
def TalosRegion_PostReNBA    : I32EnumAttrCase<"PostReNBA",    14, "post_re_nba">;
def TalosRegion_PrePostponed : I32EnumAttrCase<"PrePostponed", 15, "pre_postponed">;
def TalosRegion_Postponed    : I32EnumAttrCase<"Postponed",    16, "postponed">;

def EventRegion : I32EnumAttr<"EventRegion", "IEEE 1800 event region", [
  TalosRegion_Preponed, TalosRegion_PreActive, TalosRegion_Active,
  TalosRegion_Inactive, TalosRegion_PreNBA, TalosRegion_NBA,
  TalosRegion_PostNBA, TalosRegion_PreObserved, TalosRegion_Observed,
  TalosRegion_PostObserved, TalosRegion_Reactive,
  TalosRegion_ReInactive, TalosRegion_PreReNBA, TalosRegion_ReNBA,
  TalosRegion_PostReNBA, TalosRegion_PrePostponed,
  TalosRegion_Postponed
]> {
  let cppNamespace = "::talos::ir";
}

def TalosEdge_Change  : I32EnumAttrCase<"Change",  0, "change">;
def TalosEdge_Posedge : I32EnumAttrCase<"Posedge", 1, "posedge">;
def TalosEdge_Negedge : I32EnumAttrCase<"Negedge", 2, "negedge">;
def TalosEdge_Both     : I32EnumAttrCase<"Both",    3, "both">;
def EdgeKind : I32EnumAttr<"EdgeKind", "SystemVerilog event edge", [
  TalosEdge_Change, TalosEdge_Posedge, TalosEdge_Negedge,
  TalosEdge_Both
]> {
  let cppNamespace = "::talos::ir";
}

def TalosProcess_Initial     : I32EnumAttrCase<"Initial",     0, "initial">;
def TalosProcess_Always      : I32EnumAttrCase<"Always",      1, "always">;
def TalosProcess_AlwaysComb  : I32EnumAttrCase<"AlwaysComb",  2, "always_comb">;
def TalosProcess_AlwaysFF    : I32EnumAttrCase<"AlwaysFF",    3, "always_ff">;
def TalosProcess_AlwaysLatch : I32EnumAttrCase<"AlwaysLatch", 4, "always_latch">;
def TalosProcess_Final       : I32EnumAttrCase<"Final",       5, "final">;
def TalosProcess_Task        : I32EnumAttrCase<"Task",        6, "task">;
def TalosProcess_Fork        : I32EnumAttrCase<"Fork",        7, "fork">;
def ProcessKind : I32EnumAttr<"ProcessKind", "simulation process kind", [
  TalosProcess_Initial, TalosProcess_Always, TalosProcess_AlwaysComb,
  TalosProcess_AlwaysFF, TalosProcess_AlwaysLatch, TalosProcess_Final,
  TalosProcess_Task, TalosProcess_Fork
]> {
  let cppNamespace = "::talos::ir";
}

def TalosJoin_All  : I32EnumAttrCase<"All",  0, "all">;
def TalosJoin_Any  : I32EnumAttrCase<"Any",  1, "any">;
def TalosJoin_None : I32EnumAttrCase<"None", 2, "none">;
def JoinKind : I32EnumAttr<"JoinKind", "fork/join completion policy", [
  TalosJoin_All, TalosJoin_Any, TalosJoin_None
]> {
  let cppNamespace = "::talos::ir";
}

def TalosUnary_Plus       : I32EnumAttrCase<"Plus",       0, "plus">;
def TalosUnary_Negate     : I32EnumAttrCase<"Negate",     1, "negate">;
def TalosUnary_BitNot     : I32EnumAttrCase<"BitNot",     2, "bit_not">;
def TalosUnary_LogicalNot : I32EnumAttrCase<"LogicalNot", 3, "logical_not">;
def LogicUnaryKind : I32EnumAttr<"LogicUnaryKind",
    "four-state unary operator", [
  TalosUnary_Plus, TalosUnary_Negate, TalosUnary_BitNot,
  TalosUnary_LogicalNot
]> {
  let cppNamespace = "::talos::ir";
}

def TalosBinary_Add        : I32EnumAttrCase<"Add",        0,  "add">;
def TalosBinary_Sub        : I32EnumAttrCase<"Sub",        1,  "sub">;
def TalosBinary_Mul        : I32EnumAttrCase<"Mul",        2,  "mul">;
def TalosBinary_Div        : I32EnumAttrCase<"Div",        3,  "div">;
def TalosBinary_Mod        : I32EnumAttrCase<"Mod",        4,  "mod">;
def TalosBinary_Pow        : I32EnumAttrCase<"Pow",        5,  "pow">;
def TalosBinary_BitAnd     : I32EnumAttrCase<"BitAnd",     6,  "bit_and">;
def TalosBinary_BitOr      : I32EnumAttrCase<"BitOr",      7,  "bit_or">;
def TalosBinary_BitXor     : I32EnumAttrCase<"BitXor",     8,  "bit_xor">;
def TalosBinary_BitXnor    : I32EnumAttrCase<"BitXnor",    9,  "bit_xnor">;
def TalosBinary_LogicalAnd : I32EnumAttrCase<"LogicalAnd", 10, "logical_and">;
def TalosBinary_LogicalOr  : I32EnumAttrCase<"LogicalOr",  11, "logical_or">;
def TalosBinary_ShiftLeft  : I32EnumAttrCase<"ShiftLeft",  12, "shift_left">;
def TalosBinary_ShiftRight : I32EnumAttrCase<"ShiftRight", 13, "shift_right">;
def TalosBinary_AShiftRight
    : I32EnumAttrCase<"AShiftRight", 14, "ashift_right">;
def TalosBinary_UDiv       : I32EnumAttrCase<"UDiv",       15, "udiv">;
def TalosBinary_SDiv       : I32EnumAttrCase<"SDiv",       16, "sdiv">;
def TalosBinary_UMod       : I32EnumAttrCase<"UMod",       17, "umod">;
def TalosBinary_SMod       : I32EnumAttrCase<"SMod",       18, "smod">;
def TalosBinary_UPow       : I32EnumAttrCase<"UPow",       19, "upow">;
def TalosBinary_SPow       : I32EnumAttrCase<"SPow",       20, "spow">;
def LogicBinaryKind : I32EnumAttr<"LogicBinaryKind",
    "four-state binary operator", [
  TalosBinary_Add, TalosBinary_Sub, TalosBinary_Mul,
  TalosBinary_Div, TalosBinary_Mod, TalosBinary_Pow,
  TalosBinary_BitAnd, TalosBinary_BitOr, TalosBinary_BitXor,
  TalosBinary_BitXnor, TalosBinary_LogicalAnd, TalosBinary_LogicalOr,
  TalosBinary_ShiftLeft, TalosBinary_ShiftRight,
  TalosBinary_AShiftRight, TalosBinary_UDiv, TalosBinary_SDiv,
  TalosBinary_UMod, TalosBinary_SMod, TalosBinary_UPow,
  TalosBinary_SPow
]> {
  let cppNamespace = "::talos::ir";
}

def TalosCompare_LogicalEq : I32EnumAttrCase<"LogicalEq", 0, "logical_eq">;
def TalosCompare_LogicalNe : I32EnumAttrCase<"LogicalNe", 1, "logical_ne">;
def TalosCompare_CaseEq    : I32EnumAttrCase<"CaseEq",    2, "case_eq">;
def TalosCompare_CaseNe    : I32EnumAttrCase<"CaseNe",    3, "case_ne">;
def TalosCompare_WildEq    : I32EnumAttrCase<"WildEq",    4, "wild_eq">;
def TalosCompare_WildNe    : I32EnumAttrCase<"WildNe",    5, "wild_ne">;
def TalosCompare_Lt        : I32EnumAttrCase<"Lt",        6, "lt">;
def TalosCompare_Le        : I32EnumAttrCase<"Le",        7, "le">;
def TalosCompare_Gt        : I32EnumAttrCase<"Gt",        8, "gt">;
def TalosCompare_Ge        : I32EnumAttrCase<"Ge",        9, "ge">;
def TalosCompare_ULT       : I32EnumAttrCase<"ULT",       10, "ult">;
def TalosCompare_ULE       : I32EnumAttrCase<"ULE",       11, "ule">;
def TalosCompare_UGT       : I32EnumAttrCase<"UGT",       12, "ugt">;
def TalosCompare_UGE       : I32EnumAttrCase<"UGE",       13, "uge">;
def TalosCompare_SLT       : I32EnumAttrCase<"SLT",       14, "slt">;
def TalosCompare_SLE       : I32EnumAttrCase<"SLE",       15, "sle">;
def TalosCompare_SGT       : I32EnumAttrCase<"SGT",       16, "sgt">;
def TalosCompare_SGE       : I32EnumAttrCase<"SGE",       17, "sge">;
def TalosCompare_CaseZEq   : I32EnumAttrCase<"CaseZEq",   18, "casez_eq">;
def TalosCompare_CaseXZEq  : I32EnumAttrCase<"CaseXZEq",  19, "casexz_eq">;
def LogicCompareKind : I32EnumAttr<"LogicCompareKind",
    "four-state comparison operator", [
  TalosCompare_LogicalEq, TalosCompare_LogicalNe,
  TalosCompare_CaseEq, TalosCompare_CaseNe, TalosCompare_WildEq,
  TalosCompare_WildNe, TalosCompare_Lt, TalosCompare_Le,
  TalosCompare_Gt, TalosCompare_Ge, TalosCompare_ULT,
  TalosCompare_ULE, TalosCompare_UGT, TalosCompare_UGE,
  TalosCompare_SLT, TalosCompare_SLE, TalosCompare_SGT,
  TalosCompare_SGE, TalosCompare_CaseZEq, TalosCompare_CaseXZEq
]> {
  let cppNamespace = "::talos::ir";
}

def TalosReduce_And  : I32EnumAttrCase<"And",  0, "and">;
def TalosReduce_Or   : I32EnumAttrCase<"Or",   1, "or">;
def TalosReduce_Xor  : I32EnumAttrCase<"Xor",  2, "xor">;
def TalosReduce_Nand : I32EnumAttrCase<"Nand", 3, "nand">;
def TalosReduce_Nor  : I32EnumAttrCase<"Nor",  4, "nor">;
def TalosReduce_Xnor : I32EnumAttrCase<"Xnor", 5, "xnor">;
def LogicReduceKind : I32EnumAttr<"LogicReduceKind",
    "four-state reduction operator", [
  TalosReduce_And, TalosReduce_Or, TalosReduce_Xor,
  TalosReduce_Nand, TalosReduce_Nor, TalosReduce_Xnor
]> {
  let cppNamespace = "::talos::ir";
}

def TalosNet_Wire   : I32EnumAttrCase<"Wire",   0, "wire">;
def TalosNet_Tri    : I32EnumAttrCase<"Tri",    1, "tri">;
def TalosNet_Wand   : I32EnumAttrCase<"Wand",   2, "wand">;
def TalosNet_Wor    : I32EnumAttrCase<"Wor",    3, "wor">;
def TalosNet_TriAnd : I32EnumAttrCase<"TriAnd", 4, "triand">;
def TalosNet_TriOr  : I32EnumAttrCase<"TriOr",  5, "trior">;
def TalosNet_Tri0   : I32EnumAttrCase<"Tri0",   6, "tri0">;
def TalosNet_Tri1   : I32EnumAttrCase<"Tri1",   7, "tri1">;
def TalosNet_Supply0
    : I32EnumAttrCase<"Supply0", 8, "supply0">;
def TalosNet_Supply1
    : I32EnumAttrCase<"Supply1", 9, "supply1">;
def TalosNet_TriReg
    : I32EnumAttrCase<"TriReg", 10, "trireg">;
def TalosNet_UWire
    : I32EnumAttrCase<"UWire", 11, "uwire">;
def TalosNet_Interconnect
    : I32EnumAttrCase<"Interconnect", 12, "interconnect">;
def TalosNet_UserDefined
    : I32EnumAttrCase<"UserDefined", 13, "user_defined">;
def TalosNet_Unknown
    : I32EnumAttrCase<"Unknown", 14, "unknown">;
def NetKind : I32EnumAttr<"NetKind", "net resolution function", [
  TalosNet_Wire, TalosNet_Tri, TalosNet_Wand, TalosNet_Wor,
  TalosNet_TriAnd, TalosNet_TriOr, TalosNet_Tri0, TalosNet_Tri1,
  TalosNet_Supply0, TalosNet_Supply1, TalosNet_TriReg,
  TalosNet_UWire, TalosNet_Interconnect, TalosNet_UserDefined,
  TalosNet_Unknown
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSeverity_Info    : I32EnumAttrCase<"Info",    0, "info">;
def TalosSeverity_Warning : I32EnumAttrCase<"Warning", 1, "warning">;
def TalosSeverity_Error   : I32EnumAttrCase<"Error",   2, "error">;
def TalosSeverity_Fatal   : I32EnumAttrCase<"Fatal",   3, "fatal">;
def Severity : I32EnumAttr<"Severity", "diagnostic severity", [
  TalosSeverity_Info, TalosSeverity_Warning, TalosSeverity_Error,
  TalosSeverity_Fatal
]> {
  let cppNamespace = "::talos::ir";
}

def TalosDPI_In     : I32EnumAttrCase<"In",     0, "in">;
def TalosDPI_Out    : I32EnumAttrCase<"Out",    1, "out">;
def TalosDPI_InOut  : I32EnumAttrCase<"InOut",  2, "inout">;
def TalosDPI_Return : I32EnumAttrCase<"Return", 3, "return">;
def DPIArgDirection : I32EnumAttr<"DPIArgDirection",
    "DPI argument direction", [
  TalosDPI_In, TalosDPI_Out, TalosDPI_InOut, TalosDPI_Return
]> {
  let cppNamespace = "::talos::ir";
}

def TalosDefer_Immediate : I32EnumAttrCase<"Immediate", 0, "immediate">;
def TalosDefer_Observed  : I32EnumAttrCase<"Observed",  1, "observed">;
def TalosDefer_Final     : I32EnumAttrCase<"Final",     2, "final">;
def DeferAssert : I32EnumAttr<"DeferAssert", "assertion deferral", [
  TalosDefer_Immediate, TalosDefer_Observed, TalosDefer_Final
]> {
  let cppNamespace = "::talos::ir";
}

def TalosFile_Read         : I32EnumAttrCase<"Read",         0, "read">;
def TalosFile_Write        : I32EnumAttrCase<"Write",        1, "write">;
def TalosFile_Append       : I32EnumAttrCase<"Append",       2, "append">;
def TalosFile_ReadUpdate   : I32EnumAttrCase<"ReadUpdate",   3, "read_update">;
def TalosFile_WriteUpdate  : I32EnumAttrCase<"WriteUpdate",  4, "write_update">;
def TalosFile_AppendUpdate : I32EnumAttrCase<"AppendUpdate", 5, "append_update">;
def FileOpenMode : I32EnumAttr<"FileOpenMode", "SV file open mode", [
  TalosFile_Read, TalosFile_Write, TalosFile_Append,
  TalosFile_ReadUpdate, TalosFile_WriteUpdate,
  TalosFile_AppendUpdate
]> {
  let cppNamespace = "::talos::ir";
}

def TalosAlign_Right : I32EnumAttrCase<"Right", 0, "right">;
def TalosAlign_Left  : I32EnumAttrCase<"Left",  1, "left">;
def IntegerAlignment : I32EnumAttr<"IntegerAlignment",
    "formatted integer alignment", [
  TalosAlign_Right, TalosAlign_Left
]> {
  let cppNamespace = "::talos::ir";
}

def TalosFormat_Decimal  : I32EnumAttrCase<"Decimal",  0, "decimal">;
def TalosFormat_Binary   : I32EnumAttrCase<"Binary",   1, "binary">;
def TalosFormat_Octal    : I32EnumAttrCase<"Octal",    2, "octal">;
def TalosFormat_HexLower : I32EnumAttrCase<"HexLower", 3, "hex_lower">;
def TalosFormat_HexUpper : I32EnumAttrCase<"HexUpper", 4, "hex_upper">;
def IntegerFormat : I32EnumAttr<"IntegerFormat", "integer format radix", [
  TalosFormat_Decimal, TalosFormat_Binary, TalosFormat_Octal,
  TalosFormat_HexLower, TalosFormat_HexUpper
]> {
  let cppNamespace = "::talos::ir";
}

def TalosPadding_Space : I32EnumAttrCase<"Space", 0, "space">;
def TalosPadding_Zero  : I32EnumAttrCase<"Zero",  1, "zero">;
def IntegerPadding : I32EnumAttr<"IntegerPadding",
    "formatted integer padding", [
  TalosPadding_Space, TalosPadding_Zero
]> {
  let cppNamespace = "::talos::ir";
}

// Complete source-level operator inventories. These are intentionally distinct
// from the normalized Logic* enums above, which are only valid after semantic
// typing and lvalue side effects have been lowered.
def TalosSVUnary_Plus          : I32EnumAttrCase<"Plus",          0, "plus">;
def TalosSVUnary_Minus         : I32EnumAttrCase<"Minus",         1, "minus">;
def TalosSVUnary_BitwiseNot    : I32EnumAttrCase<"BitwiseNot",    2, "bitwise_not">;
def TalosSVUnary_BitwiseAnd    : I32EnumAttrCase<"BitwiseAnd",    3, "bitwise_and">;
def TalosSVUnary_BitwiseOr     : I32EnumAttrCase<"BitwiseOr",     4, "bitwise_or">;
def TalosSVUnary_BitwiseXor    : I32EnumAttrCase<"BitwiseXor",    5, "bitwise_xor">;
def TalosSVUnary_BitwiseNand   : I32EnumAttrCase<"BitwiseNand",   6, "bitwise_nand">;
def TalosSVUnary_BitwiseNor    : I32EnumAttrCase<"BitwiseNor",    7, "bitwise_nor">;
def TalosSVUnary_BitwiseXnor   : I32EnumAttrCase<"BitwiseXnor",   8, "bitwise_xnor">;
def TalosSVUnary_LogicalNot    : I32EnumAttrCase<"LogicalNot",    9, "logical_not">;
def TalosSVUnary_Preincrement  : I32EnumAttrCase<"Preincrement", 10, "preincrement">;
def TalosSVUnary_Predecrement  : I32EnumAttrCase<"Predecrement", 11, "predecrement">;
def TalosSVUnary_Postincrement : I32EnumAttrCase<"Postincrement", 12, "postincrement">;
def TalosSVUnary_Postdecrement : I32EnumAttrCase<"Postdecrement", 13, "postdecrement">;
def TalosSVUnaryOperator : I32EnumAttr<"SVUnaryOperator",
    "complete source-level SystemVerilog unary operator", [
  TalosSVUnary_Plus, TalosSVUnary_Minus, TalosSVUnary_BitwiseNot,
  TalosSVUnary_BitwiseAnd, TalosSVUnary_BitwiseOr,
  TalosSVUnary_BitwiseXor, TalosSVUnary_BitwiseNand,
  TalosSVUnary_BitwiseNor, TalosSVUnary_BitwiseXnor,
  TalosSVUnary_LogicalNot, TalosSVUnary_Preincrement,
  TalosSVUnary_Predecrement, TalosSVUnary_Postincrement,
  TalosSVUnary_Postdecrement
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVBinary_Add                : I32EnumAttrCase<"Add",                 0, "add">;
def TalosSVBinary_Subtract           : I32EnumAttrCase<"Subtract",            1, "subtract">;
def TalosSVBinary_Multiply           : I32EnumAttrCase<"Multiply",            2, "multiply">;
def TalosSVBinary_Divide             : I32EnumAttrCase<"Divide",              3, "divide">;
def TalosSVBinary_Mod                : I32EnumAttrCase<"Mod",                 4, "mod">;
def TalosSVBinary_BinaryAnd          : I32EnumAttrCase<"BinaryAnd",           5, "binary_and">;
def TalosSVBinary_BinaryOr           : I32EnumAttrCase<"BinaryOr",            6, "binary_or">;
def TalosSVBinary_BinaryXor          : I32EnumAttrCase<"BinaryXor",           7, "binary_xor">;
def TalosSVBinary_BinaryXnor         : I32EnumAttrCase<"BinaryXnor",          8, "binary_xnor">;
def TalosSVBinary_Equality           : I32EnumAttrCase<"Equality",            9, "equality">;
def TalosSVBinary_Inequality         : I32EnumAttrCase<"Inequality",         10, "inequality">;
def TalosSVBinary_CaseEquality       : I32EnumAttrCase<"CaseEquality",       11, "case_equality">;
def TalosSVBinary_CaseInequality     : I32EnumAttrCase<"CaseInequality",     12, "case_inequality">;
def TalosSVBinary_GreaterThanEqual   : I32EnumAttrCase<"GreaterThanEqual",   13, "greater_than_equal">;
def TalosSVBinary_GreaterThan        : I32EnumAttrCase<"GreaterThan",        14, "greater_than">;
def TalosSVBinary_LessThanEqual      : I32EnumAttrCase<"LessThanEqual",      15, "less_than_equal">;
def TalosSVBinary_LessThan           : I32EnumAttrCase<"LessThan",           16, "less_than">;
def TalosSVBinary_WildcardEquality   : I32EnumAttrCase<"WildcardEquality",   17, "wildcard_equality">;
def TalosSVBinary_WildcardInequality : I32EnumAttrCase<"WildcardInequality", 18, "wildcard_inequality">;
def TalosSVBinary_LogicalAnd         : I32EnumAttrCase<"LogicalAnd",         19, "logical_and">;
def TalosSVBinary_LogicalOr          : I32EnumAttrCase<"LogicalOr",          20, "logical_or">;
def TalosSVBinary_LogicalImplication : I32EnumAttrCase<"LogicalImplication", 21, "logical_implication">;
def TalosSVBinary_LogicalEquivalence : I32EnumAttrCase<"LogicalEquivalence", 22, "logical_equivalence">;
def TalosSVBinary_LogicalShiftLeft   : I32EnumAttrCase<"LogicalShiftLeft",   23, "logical_shift_left">;
def TalosSVBinary_LogicalShiftRight  : I32EnumAttrCase<"LogicalShiftRight",  24, "logical_shift_right">;
def TalosSVBinary_ArithmeticShiftLeft
    : I32EnumAttrCase<"ArithmeticShiftLeft", 25, "arithmetic_shift_left">;
def TalosSVBinary_ArithmeticShiftRight
    : I32EnumAttrCase<"ArithmeticShiftRight", 26, "arithmetic_shift_right">;
def TalosSVBinary_Power : I32EnumAttrCase<"Power", 27, "power">;
def TalosSVBinaryOperator : I32EnumAttr<"SVBinaryOperator",
    "complete source-level SystemVerilog binary operator", [
  TalosSVBinary_Add, TalosSVBinary_Subtract, TalosSVBinary_Multiply,
  TalosSVBinary_Divide, TalosSVBinary_Mod, TalosSVBinary_BinaryAnd,
  TalosSVBinary_BinaryOr, TalosSVBinary_BinaryXor,
  TalosSVBinary_BinaryXnor, TalosSVBinary_Equality,
  TalosSVBinary_Inequality, TalosSVBinary_CaseEquality,
  TalosSVBinary_CaseInequality, TalosSVBinary_GreaterThanEqual,
  TalosSVBinary_GreaterThan, TalosSVBinary_LessThanEqual,
  TalosSVBinary_LessThan, TalosSVBinary_WildcardEquality,
  TalosSVBinary_WildcardInequality, TalosSVBinary_LogicalAnd,
  TalosSVBinary_LogicalOr, TalosSVBinary_LogicalImplication,
  TalosSVBinary_LogicalEquivalence, TalosSVBinary_LogicalShiftLeft,
  TalosSVBinary_LogicalShiftRight, TalosSVBinary_ArithmeticShiftLeft,
  TalosSVBinary_ArithmeticShiftRight, TalosSVBinary_Power
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVCheck_None     : I32EnumAttrCase<"None",     0, "none">;
def TalosSVCheck_Unique   : I32EnumAttrCase<"Unique",   1, "unique">;
def TalosSVCheck_Unique0  : I32EnumAttrCase<"Unique0",  2, "unique0">;
def TalosSVCheck_Priority : I32EnumAttrCase<"Priority", 3, "priority">;
def TalosSVUniquePriorityCheck : I32EnumAttr<"SVUniquePriorityCheck",
    "unique or priority runtime check", [
  TalosSVCheck_None, TalosSVCheck_Unique, TalosSVCheck_Unique0,
  TalosSVCheck_Priority
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVCase_Normal : I32EnumAttrCase<"Normal", 0, "normal">;
def TalosSVCase_WildcardXOrZ
    : I32EnumAttrCase<"WildcardXOrZ", 1, "wildcard_x_or_z">;
def TalosSVCase_WildcardJustZ
    : I32EnumAttrCase<"WildcardJustZ", 2, "wildcard_just_z">;
def TalosSVCase_Inside : I32EnumAttrCase<"Inside", 3, "inside">;
def TalosSVCaseCondition : I32EnumAttr<"SVCaseCondition",
    "SystemVerilog case matching mode", [
  TalosSVCase_Normal, TalosSVCase_WildcardXOrZ,
  TalosSVCase_WildcardJustZ, TalosSVCase_Inside
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVValueRange_Simple
    : I32EnumAttrCase<"Simple", 0, "simple">;
def TalosSVValueRange_AbsoluteTolerance
    : I32EnumAttrCase<"AbsoluteTolerance", 1, "absolute_tolerance">;
def TalosSVValueRange_RelativeTolerance
    : I32EnumAttrCase<"RelativeTolerance", 2, "relative_tolerance">;
def TalosSVValueRangeKind : I32EnumAttr<"SVValueRangeKind",
    "kind of membership value range", [
  TalosSVValueRange_Simple, TalosSVValueRange_AbsoluteTolerance,
  TalosSVValueRange_RelativeTolerance
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVAssertion_Assert
    : I32EnumAttrCase<"Assert", 0, "assert">;
def TalosSVAssertion_Assume
    : I32EnumAttrCase<"Assume", 1, "assume">;
def TalosSVAssertion_CoverProperty
    : I32EnumAttrCase<"CoverProperty", 2, "cover_property">;
def TalosSVAssertion_CoverSequence
    : I32EnumAttrCase<"CoverSequence", 3, "cover_sequence">;
def TalosSVAssertion_Restrict
    : I32EnumAttrCase<"Restrict", 4, "restrict">;
def TalosSVAssertion_Expect
    : I32EnumAttrCase<"Expect", 5, "expect">;
def TalosSVAssertionKind : I32EnumAttr<"SVAssertionKind",
    "immediate or concurrent assertion directive kind", [
  TalosSVAssertion_Assert, TalosSVAssertion_Assume,
  TalosSVAssertion_CoverProperty, TalosSVAssertion_CoverSequence,
  TalosSVAssertion_Restrict, TalosSVAssertion_Expect
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVCoverageBin_Bins
    : I32EnumAttrCase<"Bins", 0, "bins">;
def TalosSVCoverageBin_IllegalBins
    : I32EnumAttrCase<"IllegalBins", 1, "illegal_bins">;
def TalosSVCoverageBin_IgnoreBins
    : I32EnumAttrCase<"IgnoreBins", 2, "ignore_bins">;
def TalosSVCoverageBinKind : I32EnumAttr<"SVCoverageBinKind",
    "coverage bin classification", [
  TalosSVCoverageBin_Bins, TalosSVCoverageBin_IllegalBins,
  TalosSVCoverageBin_IgnoreBins
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVCoverageBinChild_Iff
    : I32EnumAttrCase<"Iff", 0, "iff">;
def TalosSVCoverageBinChild_NumberOfBins
    : I32EnumAttrCase<"NumberOfBins", 1, "number_of_bins">;
def TalosSVCoverageBinChild_SetCoverage
    : I32EnumAttrCase<"SetCoverage", 2, "set_coverage">;
def TalosSVCoverageBinChild_With
    : I32EnumAttrCase<"With", 3, "with">;
def TalosSVCoverageBinChild_CrossSelect
    : I32EnumAttrCase<"CrossSelect", 4, "cross_select">;
def TalosSVCoverageBinChild_Value
    : I32EnumAttrCase<"Value", 5, "value">;
def TalosSVCoverageBinChild_TransitionItem
    : I32EnumAttrCase<"TransitionItem", 6, "transition_item">;
def TalosSVCoverageBinChild_TransitionRepeatFrom
    : I32EnumAttrCase<"TransitionRepeatFrom", 7, "transition_repeat_from">;
def TalosSVCoverageBinChild_TransitionRepeatTo
    : I32EnumAttrCase<"TransitionRepeatTo", 8, "transition_repeat_to">;
def TalosSVCoverageBinChildRole : I32EnumAttr<"SVCoverageBinChildRole",
    "semantic role of a direct coverage-bin child", [
  TalosSVCoverageBinChild_Iff,
  TalosSVCoverageBinChild_NumberOfBins,
  TalosSVCoverageBinChild_SetCoverage, TalosSVCoverageBinChild_With,
  TalosSVCoverageBinChild_CrossSelect, TalosSVCoverageBinChild_Value,
  TalosSVCoverageBinChild_TransitionItem,
  TalosSVCoverageBinChild_TransitionRepeatFrom,
  TalosSVCoverageBinChild_TransitionRepeatTo
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVCoverageTransitionRepeat_None
    : I32EnumAttrCase<"None", 0, "none">;
def TalosSVCoverageTransitionRepeat_Consecutive
    : I32EnumAttrCase<"Consecutive", 1, "consecutive">;
def TalosSVCoverageTransitionRepeat_Nonconsecutive
    : I32EnumAttrCase<"Nonconsecutive", 2, "nonconsecutive">;
def TalosSVCoverageTransitionRepeat_GoTo
    : I32EnumAttrCase<"GoTo", 3, "goto">;
def TalosSVCoverageTransitionRepeatKind
    : I32EnumAttr<"SVCoverageTransitionRepeatKind",
        "coverage transition repetition kind", [
  TalosSVCoverageTransitionRepeat_None,
  TalosSVCoverageTransitionRepeat_Consecutive,
  TalosSVCoverageTransitionRepeat_Nonconsecutive,
  TalosSVCoverageTransitionRepeat_GoTo
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVCoverageEvent_None
    : I32EnumAttrCase<"None", 0, "none">;
def TalosSVCoverageEvent_Clocking
    : I32EnumAttrCase<"Clocking", 1, "clocking">;
def TalosSVCoverageEvent_CustomSample
    : I32EnumAttrCase<"CustomSample", 2, "custom_sample">;
def TalosSVCoverageEvent_Block
    : I32EnumAttrCase<"Block", 3, "block">;
def TalosSVCoverageEvent_Inherited
    : I32EnumAttrCase<"Inherited", 4, "inherited">;
def TalosSVCoverageEventKind : I32EnumAttr<"SVCoverageEventKind",
    "declared covergroup sampling form", [
  TalosSVCoverageEvent_None, TalosSVCoverageEvent_Clocking,
  TalosSVCoverageEvent_CustomSample, TalosSVCoverageEvent_Block,
  TalosSVCoverageEvent_Inherited
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVCoverageOwner_Covergroup
    : I32EnumAttrCase<"Covergroup", 0, "covergroup">;
def TalosSVCoverageOwner_Coverpoint
    : I32EnumAttrCase<"Coverpoint", 1, "coverpoint">;
def TalosSVCoverageOwner_Cross
    : I32EnumAttrCase<"Cross", 2, "cross">;
def TalosSVCoverageOptionOwnerKind
    : I32EnumAttr<"SVCoverageOptionOwnerKind",
                  "coverage construct that owns a definition-time option setter", [
  TalosSVCoverageOwner_Covergroup,
  TalosSVCoverageOwner_Coverpoint,
  TalosSVCoverageOwner_Cross
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVCoverageOption_Instance
    : I32EnumAttrCase<"Instance", 0, "instance">;
def TalosSVCoverageOption_Type
    : I32EnumAttrCase<"Type", 1, "type">;
def TalosSVCoverageOptionScopeKind
    : I32EnumAttr<"SVCoverageOptionScopeKind",
                  "instance option or covergroup type option", [
  TalosSVCoverageOption_Instance, TalosSVCoverageOption_Type
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVCoverageOption_Name
    : I32EnumAttrCase<"Name", 0, "name">;
def TalosSVCoverageOption_Weight
    : I32EnumAttrCase<"Weight", 1, "weight">;
def TalosSVCoverageOption_Goal
    : I32EnumAttrCase<"Goal", 2, "goal">;
def TalosSVCoverageOption_Comment
    : I32EnumAttrCase<"Comment", 3, "comment">;
def TalosSVCoverageOption_AtLeast
    : I32EnumAttrCase<"AtLeast", 4, "at_least">;
def TalosSVCoverageOption_AutoBinMax
    : I32EnumAttrCase<"AutoBinMax", 5, "auto_bin_max">;
def TalosSVCoverageOption_CrossNumPrintMissing
    : I32EnumAttrCase<"CrossNumPrintMissing", 6,
                      "cross_num_print_missing">;
def TalosSVCoverageOption_CrossRetainAutoBins
    : I32EnumAttrCase<"CrossRetainAutoBins", 7,
                      "cross_retain_auto_bins">;
def TalosSVCoverageOption_DetectOverlap
    : I32EnumAttrCase<"DetectOverlap", 8, "detect_overlap">;
def TalosSVCoverageOption_PerInstance
    : I32EnumAttrCase<"PerInstance", 9, "per_instance">;
def TalosSVCoverageOption_GetInstCoverage
    : I32EnumAttrCase<"GetInstCoverage", 10, "get_inst_coverage">;
def TalosSVCoverageOption_Strobe
    : I32EnumAttrCase<"Strobe", 11, "strobe">;
def TalosSVCoverageOption_MergeInstances
    : I32EnumAttrCase<"MergeInstances", 12, "merge_instances">;
def TalosSVCoverageOption_DistributeFirst
    : I32EnumAttrCase<"DistributeFirst", 13, "distribute_first">;
def TalosSVCoverageOption_RealInterval
    : I32EnumAttrCase<"RealInterval", 14, "real_interval">;
def TalosSVCoverageOptionKind : I32EnumAttr<"SVCoverageOptionKind",
    "standard coverage option identity", [
  TalosSVCoverageOption_Name, TalosSVCoverageOption_Weight,
  TalosSVCoverageOption_Goal, TalosSVCoverageOption_Comment,
  TalosSVCoverageOption_AtLeast, TalosSVCoverageOption_AutoBinMax,
  TalosSVCoverageOption_CrossNumPrintMissing,
  TalosSVCoverageOption_CrossRetainAutoBins,
  TalosSVCoverageOption_DetectOverlap,
  TalosSVCoverageOption_PerInstance,
  TalosSVCoverageOption_GetInstCoverage, TalosSVCoverageOption_Strobe,
  TalosSVCoverageOption_MergeInstances,
  TalosSVCoverageOption_DistributeFirst,
  TalosSVCoverageOption_RealInterval
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVCoverageExpression_Sample
    : I32EnumAttrCase<"Sample", 0, "sample">;
def TalosSVCoverageExpression_Iff
    : I32EnumAttrCase<"Iff", 1, "iff">;
def TalosSVCoverageExpressionRole
    : I32EnumAttr<"SVCoverageExpressionRole",
                  "semantic role of a coverpoint or cross expression child", [
  TalosSVCoverageExpression_Sample, TalosSVCoverageExpression_Iff
]> {
  let cppNamespace = "::talos::ir";
}
def TalosSVCoverageExpressionRoleArrayAttr
    : TypedArrayAttrBase<TalosSVCoverageExpressionRole,
                         "coverage expression role array">;

def TalosSVCoverageBlockEvent_Begin
    : I32EnumAttrCase<"Begin", 0, "begin">;
def TalosSVCoverageBlockEvent_End
    : I32EnumAttrCase<"End", 1, "end">;
def TalosSVCoverageBlockEventKind
    : I32EnumAttr<"SVCoverageBlockEventKind",
                  "begin or end block sampling event", [
  TalosSVCoverageBlockEvent_Begin, TalosSVCoverageBlockEvent_End
]> {
  let cppNamespace = "::talos::ir";
}
def TalosSVCoverageBlockEventKindArrayAttr
    : TypedArrayAttrBase<TalosSVCoverageBlockEventKind,
                         "coverage block-event kind array">;

def TalosSVCoverageSelectUnary_Negation
    : I32EnumAttrCase<"Negation", 0, "negation">;
def TalosSVCoverageSelectUnaryOperator
    : I32EnumAttr<"SVCoverageSelectUnaryOperator",
                  "unary coverage-bin selection operator", [
  TalosSVCoverageSelectUnary_Negation
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVCoverageSelectBinary_And
    : I32EnumAttrCase<"And", 0, "and">;
def TalosSVCoverageSelectBinary_Or
    : I32EnumAttrCase<"Or", 1, "or">;
def TalosSVCoverageSelectBinaryOperator
    : I32EnumAttr<"SVCoverageSelectBinaryOperator",
                  "binary coverage-bin selection operator", [
  TalosSVCoverageSelectBinary_And, TalosSVCoverageSelectBinary_Or
]> {
  let cppNamespace = "::talos::ir";
}

def TalosReal_Float       : I32EnumAttrCase<"Float",       0, "float">;
def TalosReal_Exponential : I32EnumAttrCase<"Exponential", 1, "exponential">;
def TalosReal_General     : I32EnumAttrCase<"General",     2, "general">;
def RealFormat : I32EnumAttr<"RealFormat", "real number format", [
  TalosReal_Float, TalosReal_Exponential, TalosReal_General
]> {
  let cppNamespace = "::talos::ir";
}

def TalosStringCmp_Eq : I32EnumAttrCase<"Eq", 0, "eq">;
def TalosStringCmp_Ne : I32EnumAttrCase<"Ne", 1, "ne">;
def TalosStringCmp_Lt : I32EnumAttrCase<"Lt", 2, "lt">;
def TalosStringCmp_Le : I32EnumAttrCase<"Le", 3, "le">;
def TalosStringCmp_Gt : I32EnumAttrCase<"Gt", 4, "gt">;
def TalosStringCmp_Ge : I32EnumAttrCase<"Ge", 5, "ge">;
def StringCmpPredicate : I32EnumAttr<"StringCmpPredicate",
    "dynamic string comparison predicate", [
  TalosStringCmp_Eq, TalosStringCmp_Ne, TalosStringCmp_Lt,
  TalosStringCmp_Le, TalosStringCmp_Gt, TalosStringCmp_Ge
]> {
  let cppNamespace = "::talos::ir";
}

def TalosArrayCmp_Eq : I32EnumAttrCase<"Eq", 0, "eq">;
def TalosArrayCmp_Ne : I32EnumAttrCase<"Ne", 1, "ne">;
def ArrayCmpPredicate : I32EnumAttr<"ArrayCmpPredicate",
    "unpacked array comparison predicate", [
  TalosArrayCmp_Eq, TalosArrayCmp_Ne
]> {
  let cppNamespace = "::talos::ir";
}

// Source-semantic enum attributes used by the concrete talos.sv operation
// family. Their numeric encodings intentionally match the Slang dialect enums
// so dialect conversion can preserve the underlying builtin IntegerAttr.
def TalosSVAssignment_Blocking
    : I32EnumAttrCase<"Blocking", 0, "blocking">;
def TalosSVAssignment_Nonblocking
    : I32EnumAttrCase<"Nonblocking", 1, "nonblocking">;
def TalosSVAssignmentKind : I32EnumAttr<"SVAssignmentKind",
    "SystemVerilog assignment scheduling kind", [
  TalosSVAssignment_Blocking, TalosSVAssignment_Nonblocking
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVDynamicCast_AlwaysSuccess
    : I32EnumAttrCase<"AlwaysSuccess", 0, "always_success">;
def TalosSVDynamicCast_AlwaysFail
    : I32EnumAttrCase<"AlwaysFail", 1, "always_fail">;
def TalosSVDynamicCast_EnumMembership
    : I32EnumAttrCase<"EnumMembership", 2, "enum_membership">;
def TalosSVDynamicCast_ClassRuntime
    : I32EnumAttrCase<"ClassRuntime", 3, "class_runtime">;
def TalosSVDynamicCastKind : I32EnumAttr<"SVDynamicCastKind",
    "Elaborated SystemVerilog dynamic-cast behavior", [
  TalosSVDynamicCast_AlwaysSuccess, TalosSVDynamicCast_AlwaysFail,
  TalosSVDynamicCast_EnumMembership, TalosSVDynamicCast_ClassRuntime
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVRange_Simple
    : I32EnumAttrCase<"Simple", 0, "simple">;
def TalosSVRange_IndexedUp
    : I32EnumAttrCase<"IndexedUp", 1, "indexed_up">;
def TalosSVRange_IndexedDown
    : I32EnumAttrCase<"IndexedDown", 2, "indexed_down">;
def TalosSVRangeSelectionKind : I32EnumAttr<"SVRangeSelectionKind",
    "SystemVerilog packed range selection kind", [
  TalosSVRange_Simple, TalosSVRange_IndexedUp,
  TalosSVRange_IndexedDown
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVDirection_In
    : I32EnumAttrCase<"In", 0, "in">;
def TalosSVDirection_Out
    : I32EnumAttrCase<"Out", 1, "out">;
def TalosSVDirection_InOut
    : I32EnumAttrCase<"InOut", 2, "inout">;
def TalosSVDirection_Ref
    : I32EnumAttrCase<"Ref", 3, "ref">;
def TalosSVArgumentDirection : I32EnumAttr<"SVArgumentDirection",
    "SystemVerilog port or argument direction", [
  TalosSVDirection_In, TalosSVDirection_Out,
  TalosSVDirection_InOut, TalosSVDirection_Ref
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVDefinition_Module
    : I32EnumAttrCase<"Module", 0, "module">;
def TalosSVDefinition_Interface
    : I32EnumAttrCase<"Interface", 1, "interface">;
def TalosSVDefinition_Program
    : I32EnumAttrCase<"Program", 2, "program">;
def TalosSVDefinitionKind : I32EnumAttr<"SVDefinitionKind",
    "SystemVerilog design-unit definition kind", [
  TalosSVDefinition_Module, TalosSVDefinition_Interface,
  TalosSVDefinition_Program
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVProcedure_Initial
    : I32EnumAttrCase<"Initial", 0, "initial">;
def TalosSVProcedure_Final
    : I32EnumAttrCase<"Final", 1, "final">;
def TalosSVProcedure_Always
    : I32EnumAttrCase<"Always", 2, "always">;
def TalosSVProcedure_AlwaysComb
    : I32EnumAttrCase<"AlwaysComb", 3, "always_comb">;
def TalosSVProcedure_AlwaysLatch
    : I32EnumAttrCase<"AlwaysLatch", 4, "always_latch">;
def TalosSVProcedure_AlwaysFF
    : I32EnumAttrCase<"AlwaysFF", 5, "always_ff">;
def TalosSVProceduralBlockKind : I32EnumAttr<"SVProceduralBlockKind",
    "SystemVerilog procedural block kind", [
  TalosSVProcedure_Initial, TalosSVProcedure_Final,
  TalosSVProcedure_Always, TalosSVProcedure_AlwaysComb,
  TalosSVProcedure_AlwaysLatch, TalosSVProcedure_AlwaysFF
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVBlock_Sequential
    : I32EnumAttrCase<"Sequential", 0, "sequential">;
def TalosSVBlock_JoinAll
    : I32EnumAttrCase<"JoinAll", 1, "join_all">;
def TalosSVBlock_JoinAny
    : I32EnumAttrCase<"JoinAny", 2, "join_any">;
def TalosSVBlock_JoinNone
    : I32EnumAttrCase<"JoinNone", 3, "join_none">;
def TalosSVStatementBlockKind : I32EnumAttr<"SVStatementBlockKind",
    "SystemVerilog sequential or parallel statement block kind", [
  TalosSVBlock_Sequential, TalosSVBlock_JoinAll,
  TalosSVBlock_JoinAny, TalosSVBlock_JoinNone
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVSubroutine_Function
    : I32EnumAttrCase<"Function", 0, "function">;
def TalosSVSubroutine_Task
    : I32EnumAttrCase<"Task", 1, "task">;
def TalosSVSubroutineKind : I32EnumAttr<"SVSubroutineKind",
    "SystemVerilog function or task kind", [
  TalosSVSubroutine_Function, TalosSVSubroutine_Task
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVLifetime_Automatic
    : I32EnumAttrCase<"Automatic", 0, "automatic">;
def TalosSVLifetime_Static
    : I32EnumAttrCase<"Static", 1, "static">;
def TalosSVVariableLifetime : I32EnumAttr<"SVVariableLifetime",
    "SystemVerilog variable storage lifetime", [
  TalosSVLifetime_Automatic, TalosSVLifetime_Static
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVVisibility_Public
    : I32EnumAttrCase<"Public", 0, "public">;
def TalosSVVisibility_Protected
    : I32EnumAttrCase<"Protected", 1, "protected">;
def TalosSVVisibility_Local
    : I32EnumAttrCase<"Local", 2, "local">;
def TalosSVVisibility : I32EnumAttr<"SVVisibility",
    "SystemVerilog class member visibility", [
  TalosSVVisibility_Public, TalosSVVisibility_Protected,
  TalosSVVisibility_Local
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVRand_None  : I32EnumAttrCase<"None",  0, "none">;
def TalosSVRand_Rand  : I32EnumAttrCase<"Rand",  1, "rand">;
def TalosSVRand_RandC : I32EnumAttrCase<"RandC", 2, "randc">;
def TalosSVRandMode : I32EnumAttr<"SVRandMode",
    "SystemVerilog randomization mode", [
  TalosSVRand_None, TalosSVRand_Rand, TalosSVRand_RandC
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVIntegral_Generic
    : I32EnumAttrCase<"Generic", 0, "generic">;
def TalosSVIntegral_Bit : I32EnumAttrCase<"Bit", 1, "bit">;
def TalosSVIntegral_Logic : I32EnumAttrCase<"Logic", 2, "logic">;
def TalosSVIntegral_Reg : I32EnumAttrCase<"Reg", 3, "reg">;
def TalosSVIntegral_ShortInt
    : I32EnumAttrCase<"ShortInt", 4, "shortint">;
def TalosSVIntegral_Int : I32EnumAttrCase<"Int", 5, "int">;
def TalosSVIntegral_LongInt
    : I32EnumAttrCase<"LongInt", 6, "longint">;
def TalosSVIntegral_Byte : I32EnumAttrCase<"Byte", 7, "byte">;
def TalosSVIntegral_Integer
    : I32EnumAttrCase<"Integer", 8, "integer">;
def TalosSVIntegral_Time : I32EnumAttrCase<"Time", 9, "time">;
def TalosSVIntegralFlavor : I32EnumAttr<"SVIntegralFlavor",
    "declared SystemVerilog integral spelling", [
  TalosSVIntegral_Generic, TalosSVIntegral_Bit,
  TalosSVIntegral_Logic, TalosSVIntegral_Reg,
  TalosSVIntegral_ShortInt, TalosSVIntegral_Int,
  TalosSVIntegral_LongInt, TalosSVIntegral_Byte,
  TalosSVIntegral_Integer, TalosSVIntegral_Time
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVNet_Unknown : I32EnumAttrCase<"Unknown", 0, "unknown">;
def TalosSVNet_Wire : I32EnumAttrCase<"Wire", 1, "wire">;
def TalosSVNet_WAnd : I32EnumAttrCase<"WAnd", 2, "wand">;
def TalosSVNet_WOr : I32EnumAttrCase<"WOr", 3, "wor">;
def TalosSVNet_Tri : I32EnumAttrCase<"Tri", 4, "tri">;
def TalosSVNet_TriAnd : I32EnumAttrCase<"TriAnd", 5, "triand">;
def TalosSVNet_TriOr : I32EnumAttrCase<"TriOr", 6, "trior">;
def TalosSVNet_Tri0 : I32EnumAttrCase<"Tri0", 7, "tri0">;
def TalosSVNet_Tri1 : I32EnumAttrCase<"Tri1", 8, "tri1">;
def TalosSVNet_TriReg : I32EnumAttrCase<"TriReg", 9, "trireg">;
def TalosSVNet_Supply0 : I32EnumAttrCase<"Supply0", 10, "supply0">;
def TalosSVNet_Supply1 : I32EnumAttrCase<"Supply1", 11, "supply1">;
def TalosSVNet_UWire : I32EnumAttrCase<"UWire", 12, "uwire">;
def TalosSVNet_Interconnect
    : I32EnumAttrCase<"Interconnect", 13, "interconnect">;
def TalosSVNet_UserDefined
    : I32EnumAttrCase<"UserDefined", 14, "user_defined">;
def TalosSVNetKind : I32EnumAttr<"SVNetKind",
    "SystemVerilog net resolution kind", [
  TalosSVNet_Unknown, TalosSVNet_Wire, TalosSVNet_WAnd,
  TalosSVNet_WOr, TalosSVNet_Tri, TalosSVNet_TriAnd,
  TalosSVNet_TriOr, TalosSVNet_Tri0, TalosSVNet_Tri1,
  TalosSVNet_TriReg, TalosSVNet_Supply0, TalosSVNet_Supply1,
  TalosSVNet_UWire, TalosSVNet_Interconnect,
  TalosSVNet_UserDefined
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVNetExpansion_None
    : I32EnumAttrCase<"None", 0, "none">;
def TalosSVNetExpansion_Vectored
    : I32EnumAttrCase<"Vectored", 1, "vectored">;
def TalosSVNetExpansion_Scalared
    : I32EnumAttrCase<"Scalared", 2, "scalared">;
def TalosSVNetExpansionHint : I32EnumAttr<"SVNetExpansionHint",
    "explicit SystemVerilog net vector expansion request", [
  TalosSVNetExpansion_None, TalosSVNetExpansion_Vectored,
  TalosSVNetExpansion_Scalared
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVDrive_Supply : I32EnumAttrCase<"Supply", 0, "supply">;
def TalosSVDrive_Strong : I32EnumAttrCase<"Strong", 1, "strong">;
def TalosSVDrive_Pull : I32EnumAttrCase<"Pull", 2, "pull">;
def TalosSVDrive_Weak : I32EnumAttrCase<"Weak", 3, "weak">;
def TalosSVDrive_HighZ : I32EnumAttrCase<"HighZ", 4, "highz">;
def TalosSVDriveStrength : I32EnumAttr<"SVDriveStrength",
    "SystemVerilog drive strength for one logic polarity", [
  TalosSVDrive_Supply, TalosSVDrive_Strong, TalosSVDrive_Pull,
  TalosSVDrive_Weak, TalosSVDrive_HighZ
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVCharge_Small : I32EnumAttrCase<"Small", 0, "small">;
def TalosSVCharge_Medium : I32EnumAttrCase<"Medium", 1, "medium">;
def TalosSVCharge_Large : I32EnumAttrCase<"Large", 2, "large">;
def TalosSVChargeStrength : I32EnumAttr<"SVChargeStrength",
    "SystemVerilog trireg charge strength", [
  TalosSVCharge_Small, TalosSVCharge_Medium, TalosSVCharge_Large
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVPortConnOrdered : I32EnumAttrCase<"Ordered", 0, "ordered">;
def TalosSVPortConnNamed : I32EnumAttrCase<"Named", 1, "named">;
def TalosSVPortConnImplicit : I32EnumAttrCase<"Implicit", 2, "implicit">;
def TalosSVPortConnWildcard : I32EnumAttrCase<"Wildcard", 3, "wildcard">;
def TalosSVPortConnOmitted : I32EnumAttrCase<"Omitted", 4, "omitted">;
def TalosSVPortConnExplicitOpen
    : I32EnumAttrCase<"ExplicitOpen", 5, "explicit_open">;
def TalosSVPortConnDefault : I32EnumAttrCase<"Default", 6, "default">;
def TalosSVPortConnectionKind : I32EnumAttr<"SVPortConnectionKind",
    "elaborated port association provenance", [
  TalosSVPortConnOrdered, TalosSVPortConnNamed,
  TalosSVPortConnImplicit, TalosSVPortConnWildcard,
  TalosSVPortConnOmitted, TalosSVPortConnExplicitOpen,
  TalosSVPortConnDefault
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVAssertUnary_Not : I32EnumAttrCase<"Not", 0, "not">;
def TalosSVAssertUnary_NextTime : I32EnumAttrCase<"NextTime", 1, "nexttime">;
def TalosSVAssertUnary_SNextTime : I32EnumAttrCase<"SNextTime", 2, "s_nexttime">;
def TalosSVAssertUnary_Always : I32EnumAttrCase<"Always", 3, "always">;
def TalosSVAssertUnary_SAlways : I32EnumAttrCase<"SAlways", 4, "s_always">;
def TalosSVAssertUnary_Eventually : I32EnumAttrCase<"Eventually", 5, "eventually">;
def TalosSVAssertUnary_SEventually
    : I32EnumAttrCase<"SEventually", 6, "s_eventually">;
def TalosSVAssertionUnaryOperator : I32EnumAttr<"SVAssertionUnaryOperator",
    "SystemVerilog unary sequence or property operator", [
  TalosSVAssertUnary_Not, TalosSVAssertUnary_NextTime,
  TalosSVAssertUnary_SNextTime, TalosSVAssertUnary_Always,
  TalosSVAssertUnary_SAlways, TalosSVAssertUnary_Eventually,
  TalosSVAssertUnary_SEventually
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVAssertBinary_And : I32EnumAttrCase<"And", 0, "and">;
def TalosSVAssertBinary_Or : I32EnumAttrCase<"Or", 1, "or">;
def TalosSVAssertBinary_Intersect : I32EnumAttrCase<"Intersect", 2, "intersect">;
def TalosSVAssertBinary_Throughout : I32EnumAttrCase<"Throughout", 3, "throughout">;
def TalosSVAssertBinary_Within : I32EnumAttrCase<"Within", 4, "within">;
def TalosSVAssertBinary_Iff : I32EnumAttrCase<"Iff", 5, "iff">;
def TalosSVAssertBinary_Until : I32EnumAttrCase<"Until", 6, "until">;
def TalosSVAssertBinary_SUntil : I32EnumAttrCase<"SUntil", 7, "s_until">;
def TalosSVAssertBinary_UntilWith : I32EnumAttrCase<"UntilWith", 8, "until_with">;
def TalosSVAssertBinary_SUntilWith : I32EnumAttrCase<"SUntilWith", 9, "s_until_with">;
def TalosSVAssertBinary_Implies : I32EnumAttrCase<"Implies", 10, "implies">;
def TalosSVAssertBinary_OverlappedImplication
    : I32EnumAttrCase<"OverlappedImplication", 11, "overlapped_implication">;
def TalosSVAssertBinary_NonOverlappedImplication
    : I32EnumAttrCase<"NonOverlappedImplication", 12,
                      "nonoverlapped_implication">;
def TalosSVAssertBinary_OverlappedFollowedBy
    : I32EnumAttrCase<"OverlappedFollowedBy", 13, "overlapped_followed_by">;
def TalosSVAssertBinary_NonOverlappedFollowedBy
    : I32EnumAttrCase<"NonOverlappedFollowedBy", 14,
                      "nonoverlapped_followed_by">;
def TalosSVAssertionBinaryOperator : I32EnumAttr<"SVAssertionBinaryOperator",
    "SystemVerilog binary sequence or property operator", [
  TalosSVAssertBinary_And, TalosSVAssertBinary_Or,
  TalosSVAssertBinary_Intersect, TalosSVAssertBinary_Throughout,
  TalosSVAssertBinary_Within, TalosSVAssertBinary_Iff,
  TalosSVAssertBinary_Until, TalosSVAssertBinary_SUntil,
  TalosSVAssertBinary_UntilWith, TalosSVAssertBinary_SUntilWith,
  TalosSVAssertBinary_Implies,
  TalosSVAssertBinary_OverlappedImplication,
  TalosSVAssertBinary_NonOverlappedImplication,
  TalosSVAssertBinary_OverlappedFollowedBy,
  TalosSVAssertBinary_NonOverlappedFollowedBy
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVRepetition_Consecutive
    : I32EnumAttrCase<"Consecutive", 0, "consecutive">;
def TalosSVRepetition_Nonconsecutive
    : I32EnumAttrCase<"Nonconsecutive", 1, "nonconsecutive">;
def TalosSVRepetition_GoTo
    : I32EnumAttrCase<"GoTo", 2, "goto">;
def TalosSVSequenceRepetitionKind
    : I32EnumAttr<"SVSequenceRepetitionKind",
                  "SystemVerilog sequence repetition kind", [
  TalosSVRepetition_Consecutive, TalosSVRepetition_Nonconsecutive,
  TalosSVRepetition_GoTo
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVStrength_Strong : I32EnumAttrCase<"Strong", 0, "strong">;
def TalosSVStrength_Weak : I32EnumAttrCase<"Weak", 1, "weak">;
def TalosSVAssertionStrength : I32EnumAttr<"SVAssertionStrength",
    "strong or weak property qualification", [
  TalosSVStrength_Strong, TalosSVStrength_Weak
]> {
  let cppNamespace = "::talos::ir";
}

def TalosSVAbort_Accept : I32EnumAttrCase<"Accept", 0, "accept">;
def TalosSVAbort_Reject : I32EnumAttrCase<"Reject", 1, "reject">;
def TalosSVAssertionAbortAction : I32EnumAttr<"SVAssertionAbortAction",
    "accept or reject property abort action", [
  TalosSVAbort_Accept, TalosSVAbort_Reject
]> {
  let cppNamespace = "::talos::ir";
}

#endif // TALOS_DIALECT_TALOS_TALOSENUMS_TD
