//===- ObeliskSemanticKinds.td - Moore semantic opcodes ---*- tablegen -*-===//

#ifndef OBELISK_DIALECT_SIM_OBELISKSEMANTICKINDS_TD
#define OBELISK_DIALECT_SIM_OBELISKSEMANTICKINDS_TD

include "mlir/IR/EnumAttr.td"

// Each SemanticKind value encodes its operation family and fixed
// operand/result/region arities in addition to a family-local ordinal. This
// keeps structural checking independent of the source Moore dialect while
// retaining one exhaustive enum.
def ObeliskSemanticFamily_Value
    : I32EnumAttrCase<"Value", 1, "value">;
def ObeliskSemanticFamily_Effect
    : I32EnumAttrCase<"Effect", 2, "effect">;
def ObeliskSemanticFamily_Region
    : I32EnumAttrCase<"Region", 3, "region">;
def ObeliskSemanticFamily_Symbol
    : I32EnumAttrCase<"Symbol", 4, "symbol">;
def ObeliskSemanticFamily_IsolatedSymbol
    : I32EnumAttrCase<"IsolatedSymbol", 5, "isolated_symbol">;
def ObeliskSemanticFamily_SymbolTable
    : I32EnumAttrCase<"SymbolTable", 6, "symbol_table">;
def ObeliskSemanticFamily_Graph
    : I32EnumAttrCase<"Graph", 7, "graph">;
def ObeliskSemanticFamily_GraphSymbol
    : I32EnumAttrCase<"GraphSymbol", 8, "graph_symbol">;
def ObeliskSemanticFamily_Terminator
    : I32EnumAttrCase<"Terminator", 9, "terminator">;
def ObeliskSemanticFamily_IsolatedRegion
    : I32EnumAttrCase<"IsolatedRegion", 10, "isolated_region">;

def SemanticFamily : I32EnumAttr<"SemanticFamily",
    "structural family of a high-level semantic opcode", [
  ObeliskSemanticFamily_Value,
  ObeliskSemanticFamily_Effect,
  ObeliskSemanticFamily_Region,
  ObeliskSemanticFamily_Symbol,
  ObeliskSemanticFamily_IsolatedSymbol,
  ObeliskSemanticFamily_SymbolTable,
  ObeliskSemanticFamily_Graph,
  ObeliskSemanticFamily_GraphSymbol,
  ObeliskSemanticFamily_Terminator,
  ObeliskSemanticFamily_IsolatedRegion
]> {
  let cppNamespace = "::obelisk::ir";
}

// This is an exhaustive, strongly typed inventory of the Moore operations in
// the supported CIRCT API.  Specialized conversions consume some of these
// operations directly; the remaining values identify lossless high-level
// Obelisk operations without relying on operation-name strings at runtime.
def ObeliskSemantic_AShr : I32EnumAttrCase<"AShr", 18939904, "ashr">;
def ObeliskSemantic_AcosBI : I32EnumAttrCase<"AcosBI", 34668544, "builtin.acos">;
def ObeliskSemantic_AcoshBI : I32EnumAttrCase<"AcoshBI", 34668545, "builtin.acosh">;
def ObeliskSemantic_Add : I32EnumAttrCase<"Add", 18939905, "add">;
def ObeliskSemantic_AddReal : I32EnumAttrCase<"AddReal", 18939906, "fadd">;
def ObeliskSemantic_And : I32EnumAttrCase<"And", 18939907, "and">;
def ObeliskSemantic_ArrayCreate : I32EnumAttrCase<"ArrayCreate", 32571396, "array_create">;
def ObeliskSemantic_AsinBI : I32EnumAttrCase<"AsinBI", 34668546, "builtin.asin">;
def ObeliskSemantic_AsinhBI : I32EnumAttrCase<"AsinhBI", 34668547, "builtin.asinh">;
def ObeliskSemantic_Assert : I32EnumAttrCase<"Assert", 34603012, "assert">;
def ObeliskSemantic_AssignedVariable : I32EnumAttrCase<"AssignedVariable", 34668549, "assigned_variable">;
def ObeliskSemantic_AssocArrayClear : I32EnumAttrCase<"AssocArrayClear", 34603014, "assoc_array.clear">;
def ObeliskSemantic_AssocArrayDelete : I32EnumAttrCase<"AssocArrayDelete", 35651591, "assoc_array.delete">;
def ObeliskSemantic_AssocArrayExists : I32EnumAttrCase<"AssocArrayExists", 35717128, "assoc_array.exists">;
def ObeliskSemantic_AssocArrayExtract : I32EnumAttrCase<"AssocArrayExtract", 18939909, "assoc_array_extract">;
def ObeliskSemantic_AssocArrayExtractRef : I32EnumAttrCase<"AssocArrayExtractRef", 35717129, "assoc_array_extract_ref">;
def ObeliskSemantic_AssocArrayFirst : I32EnumAttrCase<"AssocArrayFirst", 35717130, "assoc_array.first">;
def ObeliskSemantic_AssocArrayLast : I32EnumAttrCase<"AssocArrayLast", 35717131, "assoc_array.last">;
def ObeliskSemantic_AssocArrayNext : I32EnumAttrCase<"AssocArrayNext", 35717132, "assoc_array.next">;
def ObeliskSemantic_AssocArrayPrev : I32EnumAttrCase<"AssocArrayPrev", 35717133, "assoc_array.prev">;
def ObeliskSemantic_AssocArraySize : I32EnumAttrCase<"AssocArraySize", 34668558, "assoc_array.size">;
def ObeliskSemantic_Assume : I32EnumAttrCase<"Assume", 34603023, "assume">;
def ObeliskSemantic_AtanBI : I32EnumAttrCase<"AtanBI", 34668560, "builtin.atan">;
def ObeliskSemantic_AtanhBI : I32EnumAttrCase<"AtanhBI", 34668561, "builtin.atanh">;
def ObeliskSemantic_BitstorealBI : I32EnumAttrCase<"BitstorealBI", 34668562, "builtin.bitstoreal">;
def ObeliskSemantic_BitstoshortrealBI : I32EnumAttrCase<"BitstoshortrealBI", 34668563, "builtin.bitstoshortreal">;
def ObeliskSemantic_BlockingAssign : I32EnumAttrCase<"BlockingAssign", 35651604, "blocking_assign">;
def ObeliskSemantic_BoolCast : I32EnumAttrCase<"BoolCast", 17891334, "bool_cast">;
def ObeliskSemantic_CallCoroutine : I32EnumAttrCase<"CallCoroutine", 50266133, "call_coroutine">;
def ObeliskSemantic_CaseEq : I32EnumAttrCase<"CaseEq", 18939911, "case_eq">;
def ObeliskSemantic_CaseNe : I32EnumAttrCase<"CaseNe", 18939912, "case_ne">;
def ObeliskSemantic_CaseXZEq : I32EnumAttrCase<"CaseXZEq", 18939913, "casexz_eq">;
def ObeliskSemantic_CaseZEq : I32EnumAttrCase<"CaseZEq", 18939914, "casez_eq">;
def ObeliskSemantic_CeilBI : I32EnumAttrCase<"CeilBI", 34668566, "builtin.ceil">;
def ObeliskSemantic_ClassDecl : I32EnumAttrCase<"ClassDecl", 100667392, "class.classdecl">;
def ObeliskSemantic_ClassMethodDecl : I32EnumAttrCase<"ClassMethodDecl", 67108864, "class.methoddecl">;
def ObeliskSemantic_ClassNew : I32EnumAttrCase<"ClassNew", 33619991, "class.new">;
def ObeliskSemantic_ClassPropertyDecl : I32EnumAttrCase<"ClassPropertyDecl", 67108865, "class.propertydecl">;
def ObeliskSemantic_ClassPropertyRef : I32EnumAttrCase<"ClassPropertyRef", 17891339, "class.property_ref">;
def ObeliskSemantic_ClassUpcast : I32EnumAttrCase<"ClassUpcast", 17891340, "class.upcast">;
def ObeliskSemantic_Clog2BI : I32EnumAttrCase<"Clog2BI", 34668568, "builtin.clog2">;
def ObeliskSemantic_Complete : I32EnumAttrCase<"Complete", 150994944, "complete">;
def ObeliskSemantic_Concat : I32EnumAttrCase<"Concat", 32571405, "concat">;
def ObeliskSemantic_ConcatRef : I32EnumAttrCase<"ConcatRef", 32571406, "concat_ref">;
def ObeliskSemantic_Conditional : I32EnumAttrCase<"Conditional", 51453952, "conditional">;
def ObeliskSemantic_Constant : I32EnumAttrCase<"Constant", 16842767, "constant">;
def ObeliskSemantic_ConstantReal : I32EnumAttrCase<"ConstantReal", 16842768, "constant_real">;
def ObeliskSemantic_ConstantString : I32EnumAttrCase<"ConstantString", 16842769, "constant_string">;
def ObeliskSemantic_ConstantTime : I32EnumAttrCase<"ConstantTime", 16842770, "constant_time">;
def ObeliskSemantic_ContinuousAssign : I32EnumAttrCase<"ContinuousAssign", 35651609, "assign">;
def ObeliskSemantic_Conversion : I32EnumAttrCase<"Conversion", 17891347, "conversion">;
def ObeliskSemantic_ConvertReal : I32EnumAttrCase<"ConvertReal", 17891348, "convert_real">;
def ObeliskSemantic_Coroutine : I32EnumAttrCase<"Coroutine", 83890176, "coroutine">;
def ObeliskSemantic_CosBI : I32EnumAttrCase<"CosBI", 34668570, "builtin.cos">;
def ObeliskSemantic_CoshBI : I32EnumAttrCase<"CoshBI", 34668571, "builtin.cosh">;
def ObeliskSemantic_Cover : I32EnumAttrCase<"Cover", 34603036, "cover">;
def ObeliskSemantic_DPIFunc : I32EnumAttrCase<"DPIFunc", 83890177, "func.dpi">;
def ObeliskSemantic_DelayedContinuousAssign : I32EnumAttrCase<"DelayedContinuousAssign", 36700189, "delayed_assign">;
def ObeliskSemantic_DelayedNonBlockingAssign : I32EnumAttrCase<"DelayedNonBlockingAssign", 36700190, "delayed_nonblocking_assign">;
def ObeliskSemantic_DetectEvent : I32EnumAttrCase<"DetectEvent", 49283103, "detect_event">;
def ObeliskSemantic_DetectLevel : I32EnumAttrCase<"DetectLevel", 34603040, "detect_level">;
def ObeliskSemantic_DisplayBI : I32EnumAttrCase<"DisplayBI", 34603041, "builtin.display">;
def ObeliskSemantic_DivReal : I32EnumAttrCase<"DivReal", 18939925, "fdiv">;
def ObeliskSemantic_DivS : I32EnumAttrCase<"DivS", 18939926, "divs">;
def ObeliskSemantic_DivU : I32EnumAttrCase<"DivU", 18939927, "divu">;
def ObeliskSemantic_DynExtract : I32EnumAttrCase<"DynExtract", 18939928, "dyn_extract">;
def ObeliskSemantic_DynExtractRef : I32EnumAttrCase<"DynExtractRef", 18939929, "dyn_extract_ref">;
def ObeliskSemantic_DynQueueExtract : I32EnumAttrCase<"DynQueueExtract", 19988506, "dyn_queue_extract">;
def ObeliskSemantic_DynQueueRefElement : I32EnumAttrCase<"DynQueueRefElement", 18939931, "dyn_queue_ref_element">;
def ObeliskSemantic_Eq : I32EnumAttrCase<"Eq", 18939932, "eq">;
def ObeliskSemantic_EqReal : I32EnumAttrCase<"EqReal", 18939933, "feq">;
def ObeliskSemantic_ExpBI : I32EnumAttrCase<"ExpBI", 34668578, "builtin.exp">;
def ObeliskSemantic_Extract : I32EnumAttrCase<"Extract", 17891358, "extract">;
def ObeliskSemantic_ExtractRef : I32EnumAttrCase<"ExtractRef", 17891359, "extract_ref">;
def ObeliskSemantic_FCloseBI : I32EnumAttrCase<"FCloseBI", 34603043, "builtin.fclose">;
def ObeliskSemantic_FDisplayBI : I32EnumAttrCase<"FDisplayBI", 35651620, "builtin.fdisplay">;
def ObeliskSemantic_FFlushBI : I32EnumAttrCase<"FFlushBI", 49283109, "builtin.fflush">;
def ObeliskSemantic_FOpenBI : I32EnumAttrCase<"FOpenBI", 34668582, "builtin.fopen">;
def ObeliskSemantic_Fge : I32EnumAttrCase<"Fge", 18939936, "fge">;
def ObeliskSemantic_Fgt : I32EnumAttrCase<"Fgt", 18939937, "fgt">;
def ObeliskSemantic_FinishBI : I32EnumAttrCase<"FinishBI", 33554471, "builtin.finish">;
def ObeliskSemantic_FinishMessageBI : I32EnumAttrCase<"FinishMessageBI", 33554472, "builtin.finish_message">;
def ObeliskSemantic_Fle : I32EnumAttrCase<"Fle", 18939938, "fle">;
def ObeliskSemantic_FloorBI : I32EnumAttrCase<"FloorBI", 34668585, "builtin.floor">;
def ObeliskSemantic_Flt : I32EnumAttrCase<"Flt", 18939939, "flt">;
def ObeliskSemantic_ForkJoin : I32EnumAttrCase<"ForkJoin", 117501952, "fork">;
def ObeliskSemantic_FormatChar : I32EnumAttrCase<"FormatChar", 17891364, "fmt.char">;
def ObeliskSemantic_FormatConcat : I32EnumAttrCase<"FormatConcat", 32571429, "fmt.concat">;
def ObeliskSemantic_FormatHierPath : I32EnumAttrCase<"FormatHierPath", 16842790, "fmt.hier_path">;
def ObeliskSemantic_FormatInt : I32EnumAttrCase<"FormatInt", 17891367, "fmt.int">;
def ObeliskSemantic_FormatLiteral : I32EnumAttrCase<"FormatLiteral", 16842792, "fmt.literal">;
def ObeliskSemantic_FormatReal : I32EnumAttrCase<"FormatReal", 17891369, "fmt.real">;
def ObeliskSemantic_FormatString : I32EnumAttrCase<"FormatString", 17891370, "fmt.string">;
def ObeliskSemantic_FormatStringToString : I32EnumAttrCase<"FormatStringToString", 17891371, "fstring_to_string">;
def ObeliskSemantic_FromBuiltinInt : I32EnumAttrCase<"FromBuiltinInt", 17891372, "from_builtin_int">;
def ObeliskSemantic_FuncDPICall : I32EnumAttrCase<"FuncDPICall", 50266154, "func.dpi.call">;
def ObeliskSemantic_GetGlobalVariable : I32EnumAttrCase<"GetGlobalVariable", 16842797, "get_global_variable">;
def ObeliskSemantic_GlobalVariable : I32EnumAttrCase<"GlobalVariable", 83890178, "global_variable">;
def ObeliskSemantic_HandleCaseEq : I32EnumAttrCase<"HandleCaseEq", 18939950, "handle_case_eq">;
def ObeliskSemantic_HandleCaseNe : I32EnumAttrCase<"HandleCaseNe", 18939951, "handle_case_ne">;
def ObeliskSemantic_HandleEq : I32EnumAttrCase<"HandleEq", 18939952, "handle_eq">;
def ObeliskSemantic_HandleNe : I32EnumAttrCase<"HandleNe", 18939953, "handle_ne">;
def ObeliskSemantic_Instance : I32EnumAttrCase<"Instance", 50266155, "instance">;
def ObeliskSemantic_IntToLogic : I32EnumAttrCase<"IntToLogic", 17891378, "int_to_logic">;
def ObeliskSemantic_IntToString : I32EnumAttrCase<"IntToString", 17891379, "int_to_string">;
def ObeliskSemantic_LnBI : I32EnumAttrCase<"LnBI", 34668588, "builtin.ln">;
def ObeliskSemantic_Log10BI : I32EnumAttrCase<"Log10BI", 34668589, "builtin.log10">;
def ObeliskSemantic_LogicToInt : I32EnumAttrCase<"LogicToInt", 17891380, "logic_to_int">;
def ObeliskSemantic_LogicToTime : I32EnumAttrCase<"LogicToTime", 17891381, "logic_to_time">;
def ObeliskSemantic_ModS : I32EnumAttrCase<"ModS", 18939958, "mods">;
def ObeliskSemantic_ModU : I32EnumAttrCase<"ModU", 18939959, "modu">;
def ObeliskSemantic_Mul : I32EnumAttrCase<"Mul", 18939960, "mul">;
def ObeliskSemantic_MulReal : I32EnumAttrCase<"MulReal", 18939961, "fmul">;
def ObeliskSemantic_Ne : I32EnumAttrCase<"Ne", 18939962, "ne">;
def ObeliskSemantic_NeReal : I32EnumAttrCase<"NeReal", 18939963, "fne">;
def ObeliskSemantic_Neg : I32EnumAttrCase<"Neg", 17891388, "neg">;
def ObeliskSemantic_NegReal : I32EnumAttrCase<"NegReal", 17891389, "fneg">;
def ObeliskSemantic_Net : I32EnumAttrCase<"Net", 49348654, "net">;
def ObeliskSemantic_NonBlockingAssign : I32EnumAttrCase<"NonBlockingAssign", 35651631, "nonblocking_assign">;
def ObeliskSemantic_Not : I32EnumAttrCase<"Not", 17891390, "not">;
def ObeliskSemantic_Null : I32EnumAttrCase<"Null", 16842815, "null">;
def ObeliskSemantic_OpenUArrayCreate : I32EnumAttrCase<"OpenUArrayCreate", 34668592, "open_uarray_create">;
def ObeliskSemantic_OpenUArrayDelete : I32EnumAttrCase<"OpenUArrayDelete", 34668593, "open_uarray_delete">;
def ObeliskSemantic_OpenUArraySize : I32EnumAttrCase<"OpenUArraySize", 34668594, "open_uarray_size">;
def ObeliskSemantic_Or : I32EnumAttrCase<"Or", 18939968, "or">;
def ObeliskSemantic_Output : I32EnumAttrCase<"Output", 166723585, "output">;
def ObeliskSemantic_PackedToSBV : I32EnumAttrCase<"PackedToSBV", 17891393, "packed_to_sbv">;
def ObeliskSemantic_PlusArgsTestBI : I32EnumAttrCase<"PlusArgsTestBI", 33620019, "builtin.plusargs_test">;
def ObeliskSemantic_PlusArgsValueBI : I32EnumAttrCase<"PlusArgsValueBI", 33685556, "builtin.plusargs_value">;
def ObeliskSemantic_PowReal : I32EnumAttrCase<"PowReal", 18939970, "fpow">;
def ObeliskSemantic_PowS : I32EnumAttrCase<"PowS", 18939971, "pows">;
def ObeliskSemantic_PowU : I32EnumAttrCase<"PowU", 18939972, "powu">;
def ObeliskSemantic_Procedure : I32EnumAttrCase<"Procedure", 50335745, "procedure">;
def ObeliskSemantic_QueueClear : I32EnumAttrCase<"QueueClear", 34603061, "queue.clear">;
def ObeliskSemantic_QueueCmp : I32EnumAttrCase<"QueueCmp", 18939973, "queue.cmp">;
def ObeliskSemantic_QueueConcat : I32EnumAttrCase<"QueueConcat", 32571462, "queue.concat">;
def ObeliskSemantic_QueueDelete : I32EnumAttrCase<"QueueDelete", 35651638, "queue.delete">;
def ObeliskSemantic_QueueFromUnpackedArray : I32EnumAttrCase<"QueueFromUnpackedArray", 17891399, "queue.from_unpacked_array">;
def ObeliskSemantic_QueueInsert : I32EnumAttrCase<"QueueInsert", 36700215, "queue.insert">;
def ObeliskSemantic_QueuePopBack : I32EnumAttrCase<"QueuePopBack", 34668600, "pop_back">;
def ObeliskSemantic_QueuePopFront : I32EnumAttrCase<"QueuePopFront", 34668601, "pop_front">;
def ObeliskSemantic_QueuePushBack : I32EnumAttrCase<"QueuePushBack", 35651642, "push_back">;
def ObeliskSemantic_QueuePushFront : I32EnumAttrCase<"QueuePushFront", 35651643, "push_front">;
def ObeliskSemantic_QueueResize : I32EnumAttrCase<"QueueResize", 17891400, "queue.resize">;
def ObeliskSemantic_QueueSet : I32EnumAttrCase<"QueueSet", 36700220, "queue.set">;
def ObeliskSemantic_QueueSizeBI : I32EnumAttrCase<"QueueSizeBI", 34668605, "builtin.size">;
def ObeliskSemantic_Read : I32EnumAttrCase<"Read", 34668606, "read">;
def ObeliskSemantic_RealToInt : I32EnumAttrCase<"RealToInt", 17891401, "real_to_int">;
def ObeliskSemantic_RealtobitsBI : I32EnumAttrCase<"RealtobitsBI", 34668607, "builtin.realtobits">;
def ObeliskSemantic_ReduceAnd : I32EnumAttrCase<"ReduceAnd", 17891402, "reduce_and">;
def ObeliskSemantic_ReduceOr : I32EnumAttrCase<"ReduceOr", 17891403, "reduce_or">;
def ObeliskSemantic_ReduceXor : I32EnumAttrCase<"ReduceXor", 17891404, "reduce_xor">;
def ObeliskSemantic_Replicate : I32EnumAttrCase<"Replicate", 17891405, "replicate">;
def ObeliskSemantic_Return : I32EnumAttrCase<"Return", 150994946, "return">;
def ObeliskSemantic_SBVToPacked : I32EnumAttrCase<"SBVToPacked", 17891406, "sbv_to_packed">;
def ObeliskSemantic_SExt : I32EnumAttrCase<"SExt", 17891407, "sext">;
def ObeliskSemantic_SIntToReal : I32EnumAttrCase<"SIntToReal", 17891408, "sint_to_real">;
def ObeliskSemantic_SVModule : I32EnumAttrCase<"SVModule", 134221824, "module">;
def ObeliskSemantic_ScanBeginFScanF : I32EnumAttrCase<"ScanBeginFScanF", 34668608, "scan.begin_fscanf">;
def ObeliskSemantic_ScanBeginSScanF : I32EnumAttrCase<"ScanBeginSScanF", 34668609, "scan.begin_sscanf">;
def ObeliskSemantic_ScanChar : I32EnumAttrCase<"ScanChar", 18808913, "scan.char">;
def ObeliskSemantic_ScanEnd : I32EnumAttrCase<"ScanEnd", 34668610, "scan.end">;
def ObeliskSemantic_ScanHierPathMatch : I32EnumAttrCase<"ScanHierPathMatch", 17891410, "scan.hier_path_match">;
def ObeliskSemantic_ScanInt : I32EnumAttrCase<"ScanInt", 18808915, "scan.int">;
def ObeliskSemantic_ScanLiteral : I32EnumAttrCase<"ScanLiteral", 17891412, "scan.literal">;
def ObeliskSemantic_ScanReal : I32EnumAttrCase<"ScanReal", 18808917, "scan.real">;
def ObeliskSemantic_ScanStr : I32EnumAttrCase<"ScanStr", 18808918, "scan.str">;
def ObeliskSemantic_ScanTime : I32EnumAttrCase<"ScanTime", 18808919, "scan.time">;
def ObeliskSemantic_ScanUnformatted : I32EnumAttrCase<"ScanUnformatted", 18808920, "scan.unformatted">;
def ObeliskSemantic_SeverityBI : I32EnumAttrCase<"SeverityBI", 34603075, "builtin.severity">;
def ObeliskSemantic_Sge : I32EnumAttrCase<"Sge", 18939993, "sge">;
def ObeliskSemantic_Sgt : I32EnumAttrCase<"Sgt", 18939994, "sgt">;
def ObeliskSemantic_Shl : I32EnumAttrCase<"Shl", 18939995, "shl">;
def ObeliskSemantic_ShortrealtobitsBI : I32EnumAttrCase<"ShortrealtobitsBI", 34668612, "builtin.shortrealtobits">;
def ObeliskSemantic_Shr : I32EnumAttrCase<"Shr", 18939996, "shr">;
def ObeliskSemantic_SinBI : I32EnumAttrCase<"SinBI", 34668613, "builtin.sin">;
def ObeliskSemantic_SinhBI : I32EnumAttrCase<"SinhBI", 34668614, "builtin.sinh">;
def ObeliskSemantic_Sle : I32EnumAttrCase<"Sle", 18939997, "sle">;
def ObeliskSemantic_Slt : I32EnumAttrCase<"Slt", 18939998, "slt">;
def ObeliskSemantic_SqrtBI : I32EnumAttrCase<"SqrtBI", 34668615, "builtin.sqrt">;
def ObeliskSemantic_StopBI : I32EnumAttrCase<"StopBI", 33554504, "builtin.stop">;
def ObeliskSemantic_StringAtobin : I32EnumAttrCase<"StringAtobin", 34668617, "string.atobin">;
def ObeliskSemantic_StringAtohex : I32EnumAttrCase<"StringAtohex", 34668618, "string.atohex">;
def ObeliskSemantic_StringAtoi : I32EnumAttrCase<"StringAtoi", 34668619, "string.atoi">;
def ObeliskSemantic_StringAtooct : I32EnumAttrCase<"StringAtooct", 34668620, "string.atooct">;
def ObeliskSemantic_StringAtoreal : I32EnumAttrCase<"StringAtoreal", 34668621, "string.atoreal">;
def ObeliskSemantic_StringBintoa : I32EnumAttrCase<"StringBintoa", 35651662, "string.bintoa">;
def ObeliskSemantic_StringCmp : I32EnumAttrCase<"StringCmp", 18939999, "string_cmp">;
def ObeliskSemantic_StringCompare : I32EnumAttrCase<"StringCompare", 35717199, "string.compare">;
def ObeliskSemantic_StringConcat : I32EnumAttrCase<"StringConcat", 49348688, "string.concat">;
def ObeliskSemantic_StringGet : I32EnumAttrCase<"StringGet", 35717201, "string.get">;
def ObeliskSemantic_StringHextoa : I32EnumAttrCase<"StringHextoa", 35651666, "string.hextoa">;
def ObeliskSemantic_StringICompare : I32EnumAttrCase<"StringICompare", 35717203, "string.icompare">;
def ObeliskSemantic_StringItoa : I32EnumAttrCase<"StringItoa", 35651668, "string.itoa">;
def ObeliskSemantic_StringLen : I32EnumAttrCase<"StringLen", 34668629, "string.len">;
def ObeliskSemantic_StringOcttoa : I32EnumAttrCase<"StringOcttoa", 35651670, "string.octtoa">;
def ObeliskSemantic_StringPut : I32EnumAttrCase<"StringPut", 36700247, "string.put">;
def ObeliskSemantic_StringRealtoa : I32EnumAttrCase<"StringRealtoa", 35651672, "string.realtoa">;
def ObeliskSemantic_StringSubstr : I32EnumAttrCase<"StringSubstr", 36765785, "string.substr">;
def ObeliskSemantic_StringToInt : I32EnumAttrCase<"StringToInt", 17891424, "string_to_int">;
def ObeliskSemantic_StringToLower : I32EnumAttrCase<"StringToLower", 34668634, "string.tolower">;
def ObeliskSemantic_StringToUpper : I32EnumAttrCase<"StringToUpper", 34668635, "string.toupper">;
def ObeliskSemantic_StructCreate : I32EnumAttrCase<"StructCreate", 32571489, "struct_create">;
def ObeliskSemantic_StructExtract : I32EnumAttrCase<"StructExtract", 17891426, "struct_extract">;
def ObeliskSemantic_StructExtractRef : I32EnumAttrCase<"StructExtractRef", 17891427, "struct_extract_ref">;
def ObeliskSemantic_StructInject : I32EnumAttrCase<"StructInject", 18940004, "struct_inject">;
def ObeliskSemantic_Sub : I32EnumAttrCase<"Sub", 18940005, "sub">;
def ObeliskSemantic_SubReal : I32EnumAttrCase<"SubReal", 18940006, "fsub">;
def ObeliskSemantic_TanBI : I32EnumAttrCase<"TanBI", 34668636, "builtin.tan">;
def ObeliskSemantic_TanhBI : I32EnumAttrCase<"TanhBI", 34668637, "builtin.tanh">;
def ObeliskSemantic_TimeBI : I32EnumAttrCase<"TimeBI", 33620062, "builtin.time">;
def ObeliskSemantic_TimeToLogic : I32EnumAttrCase<"TimeToLogic", 17891431, "time_to_logic">;
def ObeliskSemantic_ToBuiltinInt : I32EnumAttrCase<"ToBuiltinInt", 17891432, "to_builtin_int">;
def ObeliskSemantic_Trunc : I32EnumAttrCase<"Trunc", 17891433, "trunc">;
def ObeliskSemantic_UArrayCmp : I32EnumAttrCase<"UArrayCmp", 18940010, "uarray_cmp">;
def ObeliskSemantic_UIntToReal : I32EnumAttrCase<"UIntToReal", 17891435, "uint_to_real">;
def ObeliskSemantic_Uge : I32EnumAttrCase<"Uge", 18940012, "uge">;
def ObeliskSemantic_Ugt : I32EnumAttrCase<"Ugt", 18940013, "ugt">;
def ObeliskSemantic_Ule : I32EnumAttrCase<"Ule", 18940014, "ule">;
def ObeliskSemantic_Ult : I32EnumAttrCase<"Ult", 18940015, "ult">;
def ObeliskSemantic_UnionCreate : I32EnumAttrCase<"UnionCreate", 17891440, "union_create">;
def ObeliskSemantic_UnionExtract : I32EnumAttrCase<"UnionExtract", 34668639, "union_extract">;
def ObeliskSemantic_UnionExtractRef : I32EnumAttrCase<"UnionExtractRef", 34668640, "union_extract_ref">;
def ObeliskSemantic_Unreachable : I32EnumAttrCase<"Unreachable", 150994947, "unreachable">;
def ObeliskSemantic_UrandomRangeBI : I32EnumAttrCase<"UrandomRangeBI", 49348705, "builtin.urandom_range">;
def ObeliskSemantic_VTableEntry : I32EnumAttrCase<"VTableEntry", 33554530, "vtable_entry">;
def ObeliskSemantic_VTableLoadMethod : I32EnumAttrCase<"VTableLoadMethod", 17891441, "vtable.load_method">;
def ObeliskSemantic_VTable : I32EnumAttrCase<"VTable", 167776256, "vtable">;
def ObeliskSemantic_Variable : I32EnumAttrCase<"Variable", 49348707, "variable">;
def ObeliskSemantic_WaitDelay : I32EnumAttrCase<"WaitDelay", 34603108, "wait_delay">;
def ObeliskSemantic_WaitEvent : I32EnumAttrCase<"WaitEvent", 50335746, "wait_event">;
def ObeliskSemantic_WaitFork : I32EnumAttrCase<"WaitFork", 33554533, "wait_fork">;
def ObeliskSemantic_WaitLevel : I32EnumAttrCase<"WaitLevel", 50335747, "wait_level">;
def ObeliskSemantic_WildcardEq : I32EnumAttrCase<"WildcardEq", 18940018, "wildcard_eq">;
def ObeliskSemantic_WildcardNe : I32EnumAttrCase<"WildcardNe", 18940019, "wildcard_ne">;
def ObeliskSemantic_Xor : I32EnumAttrCase<"Xor", 18940020, "xor">;
def ObeliskSemantic_Yield : I32EnumAttrCase<"Yield", 152043524, "yield">;
def ObeliskSemantic_ZExt : I32EnumAttrCase<"ZExt", 17891445, "zext">;

def SemanticKind : I32EnumAttr<"SemanticKind",
    "strongly typed high-level SystemVerilog semantic operation", [
  ObeliskSemantic_AShr, ObeliskSemantic_AcosBI, ObeliskSemantic_AcoshBI,
  ObeliskSemantic_Add, ObeliskSemantic_AddReal, ObeliskSemantic_And,
  ObeliskSemantic_ArrayCreate, ObeliskSemantic_AsinBI,
  ObeliskSemantic_AsinhBI, ObeliskSemantic_Assert,
  ObeliskSemantic_AssignedVariable, ObeliskSemantic_AssocArrayClear,
  ObeliskSemantic_AssocArrayDelete, ObeliskSemantic_AssocArrayExists,
  ObeliskSemantic_AssocArrayExtract, ObeliskSemantic_AssocArrayExtractRef,
  ObeliskSemantic_AssocArrayFirst, ObeliskSemantic_AssocArrayLast,
  ObeliskSemantic_AssocArrayNext, ObeliskSemantic_AssocArrayPrev,
  ObeliskSemantic_AssocArraySize, ObeliskSemantic_Assume,
  ObeliskSemantic_AtanBI, ObeliskSemantic_AtanhBI,
  ObeliskSemantic_BitstorealBI, ObeliskSemantic_BitstoshortrealBI,
  ObeliskSemantic_BlockingAssign, ObeliskSemantic_BoolCast,
  ObeliskSemantic_CallCoroutine, ObeliskSemantic_CaseEq,
  ObeliskSemantic_CaseNe, ObeliskSemantic_CaseXZEq,
  ObeliskSemantic_CaseZEq, ObeliskSemantic_CeilBI,
  ObeliskSemantic_ClassDecl, ObeliskSemantic_ClassMethodDecl,
  ObeliskSemantic_ClassNew, ObeliskSemantic_ClassPropertyDecl,
  ObeliskSemantic_ClassPropertyRef, ObeliskSemantic_ClassUpcast,
  ObeliskSemantic_Clog2BI, ObeliskSemantic_Complete,
  ObeliskSemantic_Concat, ObeliskSemantic_ConcatRef,
  ObeliskSemantic_Conditional, ObeliskSemantic_Constant,
  ObeliskSemantic_ConstantReal, ObeliskSemantic_ConstantString,
  ObeliskSemantic_ConstantTime, ObeliskSemantic_ContinuousAssign,
  ObeliskSemantic_Conversion, ObeliskSemantic_ConvertReal,
  ObeliskSemantic_Coroutine, ObeliskSemantic_CosBI,
  ObeliskSemantic_CoshBI, ObeliskSemantic_Cover,
  ObeliskSemantic_DPIFunc, ObeliskSemantic_DelayedContinuousAssign,
  ObeliskSemantic_DelayedNonBlockingAssign, ObeliskSemantic_DetectEvent,
  ObeliskSemantic_DetectLevel, ObeliskSemantic_DisplayBI,
  ObeliskSemantic_DivReal, ObeliskSemantic_DivS, ObeliskSemantic_DivU,
  ObeliskSemantic_DynExtract, ObeliskSemantic_DynExtractRef,
  ObeliskSemantic_DynQueueExtract, ObeliskSemantic_DynQueueRefElement,
  ObeliskSemantic_Eq, ObeliskSemantic_EqReal, ObeliskSemantic_ExpBI,
  ObeliskSemantic_Extract, ObeliskSemantic_ExtractRef,
  ObeliskSemantic_FCloseBI, ObeliskSemantic_FDisplayBI,
  ObeliskSemantic_FFlushBI, ObeliskSemantic_FOpenBI, ObeliskSemantic_Fge,
  ObeliskSemantic_Fgt, ObeliskSemantic_FinishBI,
  ObeliskSemantic_FinishMessageBI, ObeliskSemantic_Fle,
  ObeliskSemantic_FloorBI, ObeliskSemantic_Flt, ObeliskSemantic_ForkJoin,
  ObeliskSemantic_FormatChar, ObeliskSemantic_FormatConcat,
  ObeliskSemantic_FormatHierPath, ObeliskSemantic_FormatInt,
  ObeliskSemantic_FormatLiteral, ObeliskSemantic_FormatReal,
  ObeliskSemantic_FormatString, ObeliskSemantic_FormatStringToString,
  ObeliskSemantic_FromBuiltinInt, ObeliskSemantic_FuncDPICall,
  ObeliskSemantic_GetGlobalVariable, ObeliskSemantic_GlobalVariable,
  ObeliskSemantic_HandleCaseEq, ObeliskSemantic_HandleCaseNe,
  ObeliskSemantic_HandleEq, ObeliskSemantic_HandleNe,
  ObeliskSemantic_Instance, ObeliskSemantic_IntToLogic,
  ObeliskSemantic_IntToString, ObeliskSemantic_LnBI,
  ObeliskSemantic_Log10BI, ObeliskSemantic_LogicToInt,
  ObeliskSemantic_LogicToTime, ObeliskSemantic_ModS,
  ObeliskSemantic_ModU, ObeliskSemantic_Mul, ObeliskSemantic_MulReal,
  ObeliskSemantic_Ne, ObeliskSemantic_NeReal, ObeliskSemantic_Neg,
  ObeliskSemantic_NegReal, ObeliskSemantic_Net,
  ObeliskSemantic_NonBlockingAssign, ObeliskSemantic_Not,
  ObeliskSemantic_Null, ObeliskSemantic_OpenUArrayCreate,
  ObeliskSemantic_OpenUArrayDelete, ObeliskSemantic_OpenUArraySize,
  ObeliskSemantic_Or, ObeliskSemantic_Output, ObeliskSemantic_PackedToSBV,
  ObeliskSemantic_PlusArgsTestBI, ObeliskSemantic_PlusArgsValueBI,
  ObeliskSemantic_PowReal, ObeliskSemantic_PowS, ObeliskSemantic_PowU,
  ObeliskSemantic_Procedure, ObeliskSemantic_QueueClear,
  ObeliskSemantic_QueueCmp, ObeliskSemantic_QueueConcat,
  ObeliskSemantic_QueueDelete, ObeliskSemantic_QueueFromUnpackedArray,
  ObeliskSemantic_QueueInsert, ObeliskSemantic_QueuePopBack,
  ObeliskSemantic_QueuePopFront, ObeliskSemantic_QueuePushBack,
  ObeliskSemantic_QueuePushFront, ObeliskSemantic_QueueResize,
  ObeliskSemantic_QueueSet, ObeliskSemantic_QueueSizeBI,
  ObeliskSemantic_Read, ObeliskSemantic_RealToInt,
  ObeliskSemantic_RealtobitsBI, ObeliskSemantic_ReduceAnd,
  ObeliskSemantic_ReduceOr, ObeliskSemantic_ReduceXor,
  ObeliskSemantic_Replicate, ObeliskSemantic_Return,
  ObeliskSemantic_SBVToPacked, ObeliskSemantic_SExt,
  ObeliskSemantic_SIntToReal, ObeliskSemantic_SVModule,
  ObeliskSemantic_ScanBeginFScanF, ObeliskSemantic_ScanBeginSScanF,
  ObeliskSemantic_ScanChar, ObeliskSemantic_ScanEnd,
  ObeliskSemantic_ScanHierPathMatch, ObeliskSemantic_ScanInt,
  ObeliskSemantic_ScanLiteral, ObeliskSemantic_ScanReal,
  ObeliskSemantic_ScanStr, ObeliskSemantic_ScanTime,
  ObeliskSemantic_ScanUnformatted, ObeliskSemantic_SeverityBI,
  ObeliskSemantic_Sge, ObeliskSemantic_Sgt, ObeliskSemantic_Shl,
  ObeliskSemantic_ShortrealtobitsBI, ObeliskSemantic_Shr,
  ObeliskSemantic_SinBI, ObeliskSemantic_SinhBI, ObeliskSemantic_Sle,
  ObeliskSemantic_Slt, ObeliskSemantic_SqrtBI, ObeliskSemantic_StopBI,
  ObeliskSemantic_StringAtobin, ObeliskSemantic_StringAtohex,
  ObeliskSemantic_StringAtoi, ObeliskSemantic_StringAtooct,
  ObeliskSemantic_StringAtoreal, ObeliskSemantic_StringBintoa,
  ObeliskSemantic_StringCmp, ObeliskSemantic_StringCompare,
  ObeliskSemantic_StringConcat, ObeliskSemantic_StringGet,
  ObeliskSemantic_StringHextoa, ObeliskSemantic_StringICompare,
  ObeliskSemantic_StringItoa, ObeliskSemantic_StringLen,
  ObeliskSemantic_StringOcttoa, ObeliskSemantic_StringPut,
  ObeliskSemantic_StringRealtoa, ObeliskSemantic_StringSubstr,
  ObeliskSemantic_StringToInt, ObeliskSemantic_StringToLower,
  ObeliskSemantic_StringToUpper, ObeliskSemantic_StructCreate,
  ObeliskSemantic_StructExtract, ObeliskSemantic_StructExtractRef,
  ObeliskSemantic_StructInject, ObeliskSemantic_Sub,
  ObeliskSemantic_SubReal, ObeliskSemantic_TanBI,
  ObeliskSemantic_TanhBI, ObeliskSemantic_TimeBI,
  ObeliskSemantic_TimeToLogic, ObeliskSemantic_ToBuiltinInt,
  ObeliskSemantic_Trunc, ObeliskSemantic_UArrayCmp,
  ObeliskSemantic_UIntToReal, ObeliskSemantic_Uge, ObeliskSemantic_Ugt,
  ObeliskSemantic_Ule, ObeliskSemantic_Ult, ObeliskSemantic_UnionCreate,
  ObeliskSemantic_UnionExtract, ObeliskSemantic_UnionExtractRef,
  ObeliskSemantic_Unreachable, ObeliskSemantic_UrandomRangeBI,
  ObeliskSemantic_VTableEntry, ObeliskSemantic_VTableLoadMethod,
  ObeliskSemantic_VTable, ObeliskSemantic_Variable,
  ObeliskSemantic_WaitDelay, ObeliskSemantic_WaitEvent,
  ObeliskSemantic_WaitFork, ObeliskSemantic_WaitLevel,
  ObeliskSemantic_WildcardEq, ObeliskSemantic_WildcardNe,
  ObeliskSemantic_Xor, ObeliskSemantic_Yield, ObeliskSemantic_ZExt
]> {
  let cppNamespace = "::obelisk::ir";
}

#endif // OBELISK_DIALECT_SIM_OBELISKSEMANTICKINDS_TD
