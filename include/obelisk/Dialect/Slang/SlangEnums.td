//===- SlangEnums.td - Elaborated semantic enumerations ----*- tablegen -*-===//

#ifndef OBELISK_DIALECT_SLANG_SLANGENUMS_TD
#define OBELISK_DIALECT_SLANG_SLANGENUMS_TD

include "mlir/IR/EnumAttr.td"

def SlangAssignment_Blocking
    : I32EnumAttrCase<"Blocking", 0, "blocking">;
def SlangAssignment_Nonblocking
    : I32EnumAttrCase<"Nonblocking", 1, "nonblocking">;
def SlangAssignmentKind : I32EnumAttr<"AssignmentKind",
    "SystemVerilog assignment scheduling kind", [
  SlangAssignment_Blocking, SlangAssignment_Nonblocking
]> {
  let cppNamespace = "::obelisk::slangir";
}

def SlangRange_Simple      : I32EnumAttrCase<"Simple",      0, "simple">;
def SlangRange_IndexedUp   : I32EnumAttrCase<"IndexedUp",   1, "indexed_up">;
def SlangRange_IndexedDown : I32EnumAttrCase<"IndexedDown", 2, "indexed_down">;
def SlangRangeSelectionKind : I32EnumAttr<"RangeSelectionKind",
    "SystemVerilog packed range selection kind", [
  SlangRange_Simple, SlangRange_IndexedUp, SlangRange_IndexedDown
]> {
  let cppNamespace = "::obelisk::slangir";
}

def SlangDirection_In    : I32EnumAttrCase<"In",    0, "in">;
def SlangDirection_Out   : I32EnumAttrCase<"Out",   1, "out">;
def SlangDirection_InOut : I32EnumAttrCase<"InOut", 2, "inout">;
def SlangDirection_Ref   : I32EnumAttrCase<"Ref",   3, "ref">;
def SlangArgumentDirection : I32EnumAttr<"ArgumentDirection",
    "SystemVerilog port or argument direction", [
  SlangDirection_In, SlangDirection_Out, SlangDirection_InOut,
  SlangDirection_Ref
]> {
  let cppNamespace = "::obelisk::slangir";
}

def SlangDefinition_Module
    : I32EnumAttrCase<"Module", 0, "module">;
def SlangDefinition_Interface
    : I32EnumAttrCase<"Interface", 1, "interface">;
def SlangDefinition_Program
    : I32EnumAttrCase<"Program", 2, "program">;
def SlangDefinitionKind : I32EnumAttr<"DefinitionKind",
    "SystemVerilog design-unit definition kind", [
  SlangDefinition_Module, SlangDefinition_Interface, SlangDefinition_Program
]> {
  let cppNamespace = "::obelisk::slangir";
}

def SlangProcedure_Initial
    : I32EnumAttrCase<"Initial", 0, "initial">;
def SlangProcedure_Final
    : I32EnumAttrCase<"Final", 1, "final">;
def SlangProcedure_Always
    : I32EnumAttrCase<"Always", 2, "always">;
def SlangProcedure_AlwaysComb
    : I32EnumAttrCase<"AlwaysComb", 3, "always_comb">;
def SlangProcedure_AlwaysLatch
    : I32EnumAttrCase<"AlwaysLatch", 4, "always_latch">;
def SlangProcedure_AlwaysFF
    : I32EnumAttrCase<"AlwaysFF", 5, "always_ff">;
def SlangProceduralBlockKind : I32EnumAttr<"ProceduralBlockKind",
    "SystemVerilog procedural block kind", [
  SlangProcedure_Initial, SlangProcedure_Final, SlangProcedure_Always,
  SlangProcedure_AlwaysComb, SlangProcedure_AlwaysLatch,
  SlangProcedure_AlwaysFF
]> {
  let cppNamespace = "::obelisk::slangir";
}

def SlangBlock_Sequential
    : I32EnumAttrCase<"Sequential", 0, "sequential">;
def SlangBlock_JoinAll
    : I32EnumAttrCase<"JoinAll", 1, "join_all">;
def SlangBlock_JoinAny
    : I32EnumAttrCase<"JoinAny", 2, "join_any">;
def SlangBlock_JoinNone
    : I32EnumAttrCase<"JoinNone", 3, "join_none">;
def SlangStatementBlockKind : I32EnumAttr<"StatementBlockKind",
    "SystemVerilog sequential or parallel statement block kind", [
  SlangBlock_Sequential, SlangBlock_JoinAll, SlangBlock_JoinAny,
  SlangBlock_JoinNone
]> {
  let cppNamespace = "::obelisk::slangir";
}

def SlangSubroutine_Function
    : I32EnumAttrCase<"Function", 0, "function">;
def SlangSubroutine_Task
    : I32EnumAttrCase<"Task", 1, "task">;
def SlangSubroutineKind : I32EnumAttr<"SubroutineKind",
    "SystemVerilog function or task kind", [
  SlangSubroutine_Function, SlangSubroutine_Task
]> {
  let cppNamespace = "::obelisk::slangir";
}

def SlangUnary_Plus          : I32EnumAttrCase<"Plus",          0, "plus">;
def SlangUnary_Minus         : I32EnumAttrCase<"Minus",         1, "minus">;
def SlangUnary_BitwiseNot    : I32EnumAttrCase<"BitwiseNot",    2, "bitwise_not">;
def SlangUnary_BitwiseAnd    : I32EnumAttrCase<"BitwiseAnd",    3, "bitwise_and">;
def SlangUnary_BitwiseOr     : I32EnumAttrCase<"BitwiseOr",     4, "bitwise_or">;
def SlangUnary_BitwiseXor    : I32EnumAttrCase<"BitwiseXor",    5, "bitwise_xor">;
def SlangUnary_BitwiseNand   : I32EnumAttrCase<"BitwiseNand",   6, "bitwise_nand">;
def SlangUnary_BitwiseNor    : I32EnumAttrCase<"BitwiseNor",    7, "bitwise_nor">;
def SlangUnary_BitwiseXnor   : I32EnumAttrCase<"BitwiseXnor",   8, "bitwise_xnor">;
def SlangUnary_LogicalNot    : I32EnumAttrCase<"LogicalNot",    9, "logical_not">;
def SlangUnary_Preincrement  : I32EnumAttrCase<"Preincrement", 10, "preincrement">;
def SlangUnary_Predecrement  : I32EnumAttrCase<"Predecrement", 11, "predecrement">;
def SlangUnary_Postincrement : I32EnumAttrCase<"Postincrement", 12, "postincrement">;
def SlangUnary_Postdecrement : I32EnumAttrCase<"Postdecrement", 13, "postdecrement">;
def SlangUnaryOperator : I32EnumAttr<"UnaryOperator",
    "elaborated SystemVerilog unary operator", [
  SlangUnary_Plus, SlangUnary_Minus, SlangUnary_BitwiseNot,
  SlangUnary_BitwiseAnd, SlangUnary_BitwiseOr, SlangUnary_BitwiseXor,
  SlangUnary_BitwiseNand, SlangUnary_BitwiseNor, SlangUnary_BitwiseXnor,
  SlangUnary_LogicalNot, SlangUnary_Preincrement, SlangUnary_Predecrement,
  SlangUnary_Postincrement, SlangUnary_Postdecrement
]> {
  let cppNamespace = "::obelisk::slangir";
}

def SlangBinary_Add                : I32EnumAttrCase<"Add",                 0, "add">;
def SlangBinary_Subtract           : I32EnumAttrCase<"Subtract",            1, "subtract">;
def SlangBinary_Multiply           : I32EnumAttrCase<"Multiply",            2, "multiply">;
def SlangBinary_Divide             : I32EnumAttrCase<"Divide",              3, "divide">;
def SlangBinary_Mod                : I32EnumAttrCase<"Mod",                 4, "mod">;
def SlangBinary_BinaryAnd          : I32EnumAttrCase<"BinaryAnd",           5, "binary_and">;
def SlangBinary_BinaryOr           : I32EnumAttrCase<"BinaryOr",            6, "binary_or">;
def SlangBinary_BinaryXor          : I32EnumAttrCase<"BinaryXor",           7, "binary_xor">;
def SlangBinary_BinaryXnor         : I32EnumAttrCase<"BinaryXnor",          8, "binary_xnor">;
def SlangBinary_Equality           : I32EnumAttrCase<"Equality",            9, "equality">;
def SlangBinary_Inequality         : I32EnumAttrCase<"Inequality",         10, "inequality">;
def SlangBinary_CaseEquality       : I32EnumAttrCase<"CaseEquality",       11, "case_equality">;
def SlangBinary_CaseInequality     : I32EnumAttrCase<"CaseInequality",     12, "case_inequality">;
def SlangBinary_GreaterThanEqual   : I32EnumAttrCase<"GreaterThanEqual",   13, "greater_than_equal">;
def SlangBinary_GreaterThan        : I32EnumAttrCase<"GreaterThan",        14, "greater_than">;
def SlangBinary_LessThanEqual      : I32EnumAttrCase<"LessThanEqual",      15, "less_than_equal">;
def SlangBinary_LessThan           : I32EnumAttrCase<"LessThan",           16, "less_than">;
def SlangBinary_WildcardEquality   : I32EnumAttrCase<"WildcardEquality",   17, "wildcard_equality">;
def SlangBinary_WildcardInequality : I32EnumAttrCase<"WildcardInequality", 18, "wildcard_inequality">;
def SlangBinary_LogicalAnd         : I32EnumAttrCase<"LogicalAnd",         19, "logical_and">;
def SlangBinary_LogicalOr          : I32EnumAttrCase<"LogicalOr",          20, "logical_or">;
def SlangBinary_LogicalImplication : I32EnumAttrCase<"LogicalImplication", 21, "logical_implication">;
def SlangBinary_LogicalEquivalence : I32EnumAttrCase<"LogicalEquivalence", 22, "logical_equivalence">;
def SlangBinary_LogicalShiftLeft   : I32EnumAttrCase<"LogicalShiftLeft",   23, "logical_shift_left">;
def SlangBinary_LogicalShiftRight  : I32EnumAttrCase<"LogicalShiftRight",  24, "logical_shift_right">;
def SlangBinary_ArithmeticShiftLeft
    : I32EnumAttrCase<"ArithmeticShiftLeft", 25, "arithmetic_shift_left">;
def SlangBinary_ArithmeticShiftRight
    : I32EnumAttrCase<"ArithmeticShiftRight", 26, "arithmetic_shift_right">;
def SlangBinary_Power : I32EnumAttrCase<"Power", 27, "power">;
def SlangBinaryOperator : I32EnumAttr<"BinaryOperator",
    "elaborated SystemVerilog binary operator", [
  SlangBinary_Add, SlangBinary_Subtract, SlangBinary_Multiply,
  SlangBinary_Divide, SlangBinary_Mod, SlangBinary_BinaryAnd,
  SlangBinary_BinaryOr, SlangBinary_BinaryXor, SlangBinary_BinaryXnor,
  SlangBinary_Equality, SlangBinary_Inequality, SlangBinary_CaseEquality,
  SlangBinary_CaseInequality, SlangBinary_GreaterThanEqual,
  SlangBinary_GreaterThan, SlangBinary_LessThanEqual, SlangBinary_LessThan,
  SlangBinary_WildcardEquality, SlangBinary_WildcardInequality,
  SlangBinary_LogicalAnd, SlangBinary_LogicalOr,
  SlangBinary_LogicalImplication, SlangBinary_LogicalEquivalence,
  SlangBinary_LogicalShiftLeft, SlangBinary_LogicalShiftRight,
  SlangBinary_ArithmeticShiftLeft, SlangBinary_ArithmeticShiftRight,
  SlangBinary_Power
]> {
  let cppNamespace = "::obelisk::slangir";
}

def SlangCheck_None     : I32EnumAttrCase<"None",     0, "none">;
def SlangCheck_Unique   : I32EnumAttrCase<"Unique",   1, "unique">;
def SlangCheck_Unique0  : I32EnumAttrCase<"Unique0",  2, "unique0">;
def SlangCheck_Priority : I32EnumAttrCase<"Priority", 3, "priority">;
def SlangUniquePriorityCheck : I32EnumAttr<"UniquePriorityCheck",
    "unique or priority runtime check", [
  SlangCheck_None, SlangCheck_Unique, SlangCheck_Unique0,
  SlangCheck_Priority
]> {
  let cppNamespace = "::obelisk::slangir";
}

def SlangCase_Normal      : I32EnumAttrCase<"Normal",      0, "normal">;
def SlangCase_WildcardXOrZ
    : I32EnumAttrCase<"WildcardXOrZ", 1, "wildcard_x_or_z">;
def SlangCase_WildcardJustZ
    : I32EnumAttrCase<"WildcardJustZ", 2, "wildcard_just_z">;
def SlangCase_Inside : I32EnumAttrCase<"Inside", 3, "inside">;
def SlangCaseCondition : I32EnumAttr<"CaseCondition",
    "SystemVerilog case matching mode", [
  SlangCase_Normal, SlangCase_WildcardXOrZ, SlangCase_WildcardJustZ,
  SlangCase_Inside
]> {
  let cppNamespace = "::obelisk::slangir";
}

def SlangAssertion_Assert        : I32EnumAttrCase<"Assert",        0, "assert">;
def SlangAssertion_Assume        : I32EnumAttrCase<"Assume",        1, "assume">;
def SlangAssertion_CoverProperty : I32EnumAttrCase<"CoverProperty", 2, "cover_property">;
def SlangAssertion_CoverSequence : I32EnumAttrCase<"CoverSequence", 3, "cover_sequence">;
def SlangAssertion_Restrict      : I32EnumAttrCase<"Restrict",      4, "restrict">;
def SlangAssertion_Expect        : I32EnumAttrCase<"Expect",        5, "expect">;
def SlangAssertionKind : I32EnumAttr<"AssertionKind",
    "immediate or concurrent assertion directive kind", [
  SlangAssertion_Assert, SlangAssertion_Assume, SlangAssertion_CoverProperty,
  SlangAssertion_CoverSequence, SlangAssertion_Restrict,
  SlangAssertion_Expect
]> {
  let cppNamespace = "::obelisk::slangir";
}

def SlangCoverageBin_Bins
    : I32EnumAttrCase<"Bins", 0, "bins">;
def SlangCoverageBin_IllegalBins
    : I32EnumAttrCase<"IllegalBins", 1, "illegal_bins">;
def SlangCoverageBin_IgnoreBins
    : I32EnumAttrCase<"IgnoreBins", 2, "ignore_bins">;
def SlangCoverageBinKind : I32EnumAttr<"CoverageBinKind",
    "coverage bin classification", [
  SlangCoverageBin_Bins, SlangCoverageBin_IllegalBins,
  SlangCoverageBin_IgnoreBins
]> {
  let cppNamespace = "::obelisk::slangir";
}

def SlangEdge_None      : I32EnumAttrCase<"None",      0, "none">;
def SlangEdge_PosEdge   : I32EnumAttrCase<"PosEdge",   1, "posedge">;
def SlangEdge_NegEdge   : I32EnumAttrCase<"NegEdge",   2, "negedge">;
def SlangEdge_BothEdges : I32EnumAttrCase<"BothEdges", 3, "both_edges">;
def SlangEdgeKind : I32EnumAttr<"EdgeKind",
    "elaborated event edge", [
  SlangEdge_None, SlangEdge_PosEdge, SlangEdge_NegEdge,
  SlangEdge_BothEdges
]> {
  let cppNamespace = "::obelisk::slangir";
}

def SlangLifetime_Automatic
    : I32EnumAttrCase<"Automatic", 0, "automatic">;
def SlangLifetime_Static
    : I32EnumAttrCase<"Static", 1, "static">;
def SlangVariableLifetime : I32EnumAttr<"VariableLifetime",
    "SystemVerilog variable storage lifetime", [
  SlangLifetime_Automatic, SlangLifetime_Static
]> {
  let cppNamespace = "::obelisk::slangir";
}

def SlangVisibility_Public
    : I32EnumAttrCase<"Public", 0, "public">;
def SlangVisibility_Protected
    : I32EnumAttrCase<"Protected", 1, "protected">;
def SlangVisibility_Local
    : I32EnumAttrCase<"Local", 2, "local">;
def SlangVisibility : I32EnumAttr<"Visibility",
    "SystemVerilog class member visibility", [
  SlangVisibility_Public, SlangVisibility_Protected, SlangVisibility_Local
]> {
  let cppNamespace = "::obelisk::slangir";
}

def SlangRand_None  : I32EnumAttrCase<"None",  0, "none">;
def SlangRand_Rand  : I32EnumAttrCase<"Rand",  1, "rand">;
def SlangRand_RandC : I32EnumAttrCase<"RandC", 2, "randc">;
def SlangRandMode : I32EnumAttr<"RandMode",
    "SystemVerilog randomization mode", [
  SlangRand_None, SlangRand_Rand, SlangRand_RandC
]> {
  let cppNamespace = "::obelisk::slangir";
}

def SlangIntegral_Generic  : I32EnumAttrCase<"Generic",  0, "generic">;
def SlangIntegral_Bit      : I32EnumAttrCase<"Bit",      1, "bit">;
def SlangIntegral_Logic    : I32EnumAttrCase<"Logic",    2, "logic">;
def SlangIntegral_Reg      : I32EnumAttrCase<"Reg",      3, "reg">;
def SlangIntegral_ShortInt : I32EnumAttrCase<"ShortInt", 4, "shortint">;
def SlangIntegral_Int      : I32EnumAttrCase<"Int",      5, "int">;
def SlangIntegral_LongInt  : I32EnumAttrCase<"LongInt",  6, "longint">;
def SlangIntegral_Byte     : I32EnumAttrCase<"Byte",     7, "byte">;
def SlangIntegral_Integer  : I32EnumAttrCase<"Integer",  8, "integer">;
def SlangIntegralFlavor : I32EnumAttr<"IntegralFlavor",
    "declared SystemVerilog integral spelling", [
  SlangIntegral_Generic, SlangIntegral_Bit, SlangIntegral_Logic,
  SlangIntegral_Reg, SlangIntegral_ShortInt, SlangIntegral_Int,
  SlangIntegral_LongInt, SlangIntegral_Byte, SlangIntegral_Integer
]> {
  let cppNamespace = "::obelisk::slangir";
}

def SlangNet_Unknown      : I32EnumAttrCase<"Unknown",      0, "unknown">;
def SlangNet_Wire         : I32EnumAttrCase<"Wire",         1, "wire">;
def SlangNet_WAnd         : I32EnumAttrCase<"WAnd",         2, "wand">;
def SlangNet_WOr          : I32EnumAttrCase<"WOr",          3, "wor">;
def SlangNet_Tri          : I32EnumAttrCase<"Tri",          4, "tri">;
def SlangNet_TriAnd       : I32EnumAttrCase<"TriAnd",       5, "triand">;
def SlangNet_TriOr        : I32EnumAttrCase<"TriOr",        6, "trior">;
def SlangNet_Tri0         : I32EnumAttrCase<"Tri0",         7, "tri0">;
def SlangNet_Tri1         : I32EnumAttrCase<"Tri1",         8, "tri1">;
def SlangNet_TriReg       : I32EnumAttrCase<"TriReg",       9, "trireg">;
def SlangNet_Supply0      : I32EnumAttrCase<"Supply0",     10, "supply0">;
def SlangNet_Supply1      : I32EnumAttrCase<"Supply1",     11, "supply1">;
def SlangNet_UWire        : I32EnumAttrCase<"UWire",       12, "uwire">;
def SlangNet_Interconnect : I32EnumAttrCase<"Interconnect", 13, "interconnect">;
def SlangNet_UserDefined  : I32EnumAttrCase<"UserDefined", 14, "user_defined">;
def SlangNetKind : I32EnumAttr<"NetKind", "SystemVerilog net resolution kind", [
  SlangNet_Unknown, SlangNet_Wire, SlangNet_WAnd, SlangNet_WOr, SlangNet_Tri,
  SlangNet_TriAnd, SlangNet_TriOr, SlangNet_Tri0, SlangNet_Tri1,
  SlangNet_TriReg, SlangNet_Supply0, SlangNet_Supply1, SlangNet_UWire,
  SlangNet_Interconnect, SlangNet_UserDefined
]> {
  let cppNamespace = "::obelisk::slangir";
}

def SlangPortConnOrdered : I32EnumAttrCase<"Ordered", 0, "ordered">;
def SlangPortConnNamed : I32EnumAttrCase<"Named", 1, "named">;
def SlangPortConnImplicit : I32EnumAttrCase<"Implicit", 2, "implicit">;
def SlangPortConnWildcard : I32EnumAttrCase<"Wildcard", 3, "wildcard">;
def SlangPortConnOmitted : I32EnumAttrCase<"Omitted", 4, "omitted">;
def SlangPortConnExplicitOpen
    : I32EnumAttrCase<"ExplicitOpen", 5, "explicit_open">;
def SlangPortConnDefault : I32EnumAttrCase<"Default", 6, "default">;
def SlangPortConnectionKind : I32EnumAttr<"PortConnectionKind",
    "elaborated port association provenance", [
  SlangPortConnOrdered, SlangPortConnNamed, SlangPortConnImplicit,
  SlangPortConnWildcard, SlangPortConnOmitted, SlangPortConnExplicitOpen,
  SlangPortConnDefault
]> {
  let cppNamespace = "::obelisk::slangir";
}

def SlangAssertUnary_Not        : I32EnumAttrCase<"Not",        0, "not">;
def SlangAssertUnary_NextTime   : I32EnumAttrCase<"NextTime",   1, "nexttime">;
def SlangAssertUnary_SNextTime  : I32EnumAttrCase<"SNextTime",  2, "s_nexttime">;
def SlangAssertUnary_Always     : I32EnumAttrCase<"Always",     3, "always">;
def SlangAssertUnary_SAlways    : I32EnumAttrCase<"SAlways",    4, "s_always">;
def SlangAssertUnary_Eventually : I32EnumAttrCase<"Eventually", 5, "eventually">;
def SlangAssertUnary_SEventually
    : I32EnumAttrCase<"SEventually", 6, "s_eventually">;
def SlangAssertionUnaryOperator : I32EnumAttr<"AssertionUnaryOperator",
    "SystemVerilog unary sequence or property operator", [
  SlangAssertUnary_Not, SlangAssertUnary_NextTime,
  SlangAssertUnary_SNextTime, SlangAssertUnary_Always,
  SlangAssertUnary_SAlways, SlangAssertUnary_Eventually,
  SlangAssertUnary_SEventually
]> {
  let cppNamespace = "::obelisk::slangir";
}

def SlangAssertBinary_And : I32EnumAttrCase<"And", 0, "and">;
def SlangAssertBinary_Or : I32EnumAttrCase<"Or", 1, "or">;
def SlangAssertBinary_Intersect : I32EnumAttrCase<"Intersect", 2, "intersect">;
def SlangAssertBinary_Throughout : I32EnumAttrCase<"Throughout", 3, "throughout">;
def SlangAssertBinary_Within : I32EnumAttrCase<"Within", 4, "within">;
def SlangAssertBinary_Iff : I32EnumAttrCase<"Iff", 5, "iff">;
def SlangAssertBinary_Until : I32EnumAttrCase<"Until", 6, "until">;
def SlangAssertBinary_SUntil : I32EnumAttrCase<"SUntil", 7, "s_until">;
def SlangAssertBinary_UntilWith : I32EnumAttrCase<"UntilWith", 8, "until_with">;
def SlangAssertBinary_SUntilWith : I32EnumAttrCase<"SUntilWith", 9, "s_until_with">;
def SlangAssertBinary_Implies : I32EnumAttrCase<"Implies", 10, "implies">;
def SlangAssertBinary_OverlappedImplication
    : I32EnumAttrCase<"OverlappedImplication", 11, "overlapped_implication">;
def SlangAssertBinary_NonOverlappedImplication
    : I32EnumAttrCase<"NonOverlappedImplication", 12,
                      "nonoverlapped_implication">;
def SlangAssertBinary_OverlappedFollowedBy
    : I32EnumAttrCase<"OverlappedFollowedBy", 13, "overlapped_followed_by">;
def SlangAssertBinary_NonOverlappedFollowedBy
    : I32EnumAttrCase<"NonOverlappedFollowedBy", 14,
                      "nonoverlapped_followed_by">;
def SlangAssertionBinaryOperator : I32EnumAttr<"AssertionBinaryOperator",
    "SystemVerilog binary sequence or property operator", [
  SlangAssertBinary_And, SlangAssertBinary_Or, SlangAssertBinary_Intersect,
  SlangAssertBinary_Throughout, SlangAssertBinary_Within, SlangAssertBinary_Iff,
  SlangAssertBinary_Until, SlangAssertBinary_SUntil,
  SlangAssertBinary_UntilWith, SlangAssertBinary_SUntilWith,
  SlangAssertBinary_Implies, SlangAssertBinary_OverlappedImplication,
  SlangAssertBinary_NonOverlappedImplication,
  SlangAssertBinary_OverlappedFollowedBy,
  SlangAssertBinary_NonOverlappedFollowedBy
]> {
  let cppNamespace = "::obelisk::slangir";
}

def SlangRepetition_Consecutive
    : I32EnumAttrCase<"Consecutive", 0, "consecutive">;
def SlangRepetition_Nonconsecutive
    : I32EnumAttrCase<"Nonconsecutive", 1, "nonconsecutive">;
def SlangRepetition_GoTo
    : I32EnumAttrCase<"GoTo", 2, "goto">;
def SlangSequenceRepetitionKind : I32EnumAttr<"SequenceRepetitionKind",
    "SystemVerilog sequence repetition kind", [
  SlangRepetition_Consecutive, SlangRepetition_Nonconsecutive,
  SlangRepetition_GoTo
]> {
  let cppNamespace = "::obelisk::slangir";
}

def SlangStrength_Strong : I32EnumAttrCase<"Strong", 0, "strong">;
def SlangStrength_Weak : I32EnumAttrCase<"Weak", 1, "weak">;
def SlangAssertionStrength : I32EnumAttr<"AssertionStrength",
    "strong or weak property qualification", [
  SlangStrength_Strong, SlangStrength_Weak
]> {
  let cppNamespace = "::obelisk::slangir";
}

def SlangAbort_Accept : I32EnumAttrCase<"Accept", 0, "accept">;
def SlangAbort_Reject : I32EnumAttrCase<"Reject", 1, "reject">;
def SlangAssertionAbortAction : I32EnumAttr<"AssertionAbortAction",
    "accept or reject property abort action", [
  SlangAbort_Accept, SlangAbort_Reject
]> {
  let cppNamespace = "::obelisk::slangir";
}

#endif // OBELISK_DIALECT_SLANG_SLANGENUMS_TD
