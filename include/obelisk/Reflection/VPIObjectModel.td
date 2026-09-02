//===- VPIObjectModel.td - IEEE VPI object inventory ---------------------===//
//
// This is the canonical, generator-readable inventory of IEEE 1800 VPI
// object and relationship selectors.  Numeric values match vpi_user.h and
// sv_vpi_user.h.  Legal source/target edges are layered on these definitions
// rather than being rediscovered in each runtime traversal implementation.
//
//===----------------------------------------------------------------------===//

class VPISelector;

class VPIObjectFamily<string cppName_, int value_> {
  string cppName = cppName_;
  int value = value_;
}

def ScopeFamily : VPIObjectFamily<"Scope", 0>;
def DeclarationFamily : VPIObjectFamily<"Declaration", 1>;
def ProcessFamily : VPIObjectFamily<"Process", 2>;
def StatementFamily : VPIObjectFamily<"Statement", 3>;
def ExpressionFamily : VPIObjectFamily<"Expression", 4>;
def VariableFamily : VPIObjectFamily<"Variable", 5>;
def NetFamily : VPIObjectFamily<"Net", 6>;
def ArrayFamily : VPIObjectFamily<"Array", 7>;
def TypespecFamily : VPIObjectFamily<"Typespec", 8>;
def PrimitiveFamily : VPIObjectFamily<"Primitive", 9>;
def TimingFamily : VPIObjectFamily<"Timing", 10>;
def AssertionFamily : VPIObjectFamily<"Assertion", 11>;
def TransientFamily : VPIObjectFamily<"Transient", 12>;
def RuntimeFamily : VPIObjectFamily<"Runtime", 13>;
def OtherFamily : VPIObjectFamily<"Other", 14>;

// A public VPI spelling is not necessarily a concrete value returned by
// vpiType.  Keep abstract grouping selectors, traversal-only spellings, and
// deprecated compatibility selectors in the ABI inventory without exposing
// them as concrete handle kinds.
class VPIObjectRole<string cppName_, int value_, bit concrete_> {
  string cppName = cppName_;
  int value = value_;
  bit concrete = concrete_;
}

def ConcreteRole : VPIObjectRole<"Concrete", 0, 1>;
def AbstractSelectorRole : VPIObjectRole<"AbstractSelector", 1, 0>;
def RelationOnlyRole : VPIObjectRole<"RelationOnly", 2, 0>;
def CompatibilitySelectorRole : VPIObjectRole<"CompatibilitySelector", 3, 0>;

class VPIObjectKind<string apiName_, int value_,
                    list<VPIObjectFamily> families_,
                    string aliasOf_ = "",
                    VPIObjectRole role_ = ConcreteRole> : VPISelector {
  string apiName = apiName_;
  int value = value_;
  list<VPIObjectFamily> families = families_;
  string aliasOf = aliasOf_;
  VPIObjectRole role = role_;
}

// IEEE 1800-2017 Annex K object types inherited from Verilog VPI.
def Always : VPIObjectKind<"vpiAlways", 1, [ProcessFamily]>;
def AssignStmt : VPIObjectKind<"vpiAssignStmt", 2, [StatementFamily]>;
def Assignment : VPIObjectKind<"vpiAssignment", 3, [StatementFamily]>;
def Begin : VPIObjectKind<"vpiBegin", 4, [StatementFamily, ScopeFamily]>;
def Case : VPIObjectKind<"vpiCase", 5, [StatementFamily]>;
def CaseItem : VPIObjectKind<"vpiCaseItem", 6, [StatementFamily]>;
def Constant : VPIObjectKind<"vpiConstant", 7, [ExpressionFamily]>;
def ContAssign : VPIObjectKind<"vpiContAssign", 8, [StatementFamily]>;
def Deassign : VPIObjectKind<"vpiDeassign", 9, [StatementFamily]>;
def DefParam : VPIObjectKind<"vpiDefParam", 10, [DeclarationFamily]>;
def DelayControl : VPIObjectKind<"vpiDelayControl", 11, [StatementFamily]>;
def Disable : VPIObjectKind<"vpiDisable", 12, [StatementFamily]>;
def EventControl : VPIObjectKind<"vpiEventControl", 13, [StatementFamily]>;
def EventStmt : VPIObjectKind<"vpiEventStmt", 14, [StatementFamily]>;
def For : VPIObjectKind<"vpiFor", 15, [StatementFamily, ScopeFamily]>;
def Force : VPIObjectKind<"vpiForce", 16, [StatementFamily]>;
def Forever : VPIObjectKind<"vpiForever", 17, [StatementFamily]>;
def Fork : VPIObjectKind<"vpiFork", 18, [StatementFamily, ScopeFamily]>;
def FuncCall : VPIObjectKind<"vpiFuncCall", 19, [ExpressionFamily]>;
def Function : VPIObjectKind<"vpiFunction", 20,
  [DeclarationFamily, ScopeFamily]>;
def Gate : VPIObjectKind<"vpiGate", 21, [PrimitiveFamily]>;
def If : VPIObjectKind<"vpiIf", 22, [StatementFamily]>;
def IfElse : VPIObjectKind<"vpiIfElse", 23, [StatementFamily]>;
def Initial : VPIObjectKind<"vpiInitial", 24, [ProcessFamily]>;
def IntegerVar : VPIObjectKind<"vpiIntegerVar", 25, [VariableFamily]>;
def InterModPath : VPIObjectKind<"vpiInterModPath", 26, [TimingFamily]>;
def Iterator : VPIObjectKind<"vpiIterator", 27, [RuntimeFamily]>;
def IODecl : VPIObjectKind<"vpiIODecl", 28, [DeclarationFamily]>;
def Memory : VPIObjectKind<"vpiMemory", 29, [VariableFamily], "",
  CompatibilitySelectorRole>;
def MemoryWord : VPIObjectKind<"vpiMemoryWord", 30, [VariableFamily], "",
  CompatibilitySelectorRole>;
def ModPath : VPIObjectKind<"vpiModPath", 31, [TimingFamily]>;
def Module : VPIObjectKind<"vpiModule", 32, [ScopeFamily]>;
def NamedBegin : VPIObjectKind<"vpiNamedBegin", 33,
  [StatementFamily, ScopeFamily]>;
def NamedEvent : VPIObjectKind<"vpiNamedEvent", 34, [VariableFamily]>;
def NamedFork : VPIObjectKind<"vpiNamedFork", 35,
  [StatementFamily, ScopeFamily]>;
def Net : VPIObjectKind<"vpiNet", 36, [NetFamily]>;
def NetBit : VPIObjectKind<"vpiNetBit", 37, [NetFamily]>;
def NullStmt : VPIObjectKind<"vpiNullStmt", 38, [StatementFamily]>;
def Operation : VPIObjectKind<"vpiOperation", 39, [ExpressionFamily]>;
def ParamAssign : VPIObjectKind<"vpiParamAssign", 40, [DeclarationFamily]>;
def Parameter : VPIObjectKind<"vpiParameter", 41,
  [DeclarationFamily, VariableFamily]>;
def PartSelect : VPIObjectKind<"vpiPartSelect", 42, [ExpressionFamily]>;
def PathTerm : VPIObjectKind<"vpiPathTerm", 43, [TimingFamily]>;
def Port : VPIObjectKind<"vpiPort", 44, [DeclarationFamily]>;
def PortBit : VPIObjectKind<"vpiPortBit", 45, [DeclarationFamily]>;
def PrimTerm : VPIObjectKind<"vpiPrimTerm", 46, [PrimitiveFamily]>;
def RealVar : VPIObjectKind<"vpiRealVar", 47, [VariableFamily]>;
def Reg : VPIObjectKind<"vpiReg", 48, [VariableFamily]>;
def RegBit : VPIObjectKind<"vpiRegBit", 49, [VariableFamily]>;
def Release : VPIObjectKind<"vpiRelease", 50, [StatementFamily]>;
def Repeat : VPIObjectKind<"vpiRepeat", 51, [StatementFamily]>;
def RepeatControl : VPIObjectKind<"vpiRepeatControl", 52, [StatementFamily]>;
def SchedEvent : VPIObjectKind<"vpiSchedEvent", 53, [RuntimeFamily]>;
def SpecParam : VPIObjectKind<"vpiSpecParam", 54,
  [DeclarationFamily, VariableFamily]>;
def Switch : VPIObjectKind<"vpiSwitch", 55, [PrimitiveFamily]>;
def SysFuncCall : VPIObjectKind<"vpiSysFuncCall", 56, [ExpressionFamily]>;
def SysTaskCall : VPIObjectKind<"vpiSysTaskCall", 57, [StatementFamily]>;
def TableEntry : VPIObjectKind<"vpiTableEntry", 58, [DeclarationFamily]>;
def Task : VPIObjectKind<"vpiTask", 59, [DeclarationFamily, ScopeFamily]>;
def TaskCall : VPIObjectKind<"vpiTaskCall", 60, [StatementFamily]>;
def Tchk : VPIObjectKind<"vpiTchk", 61, [TimingFamily]>;
def TchkTerm : VPIObjectKind<"vpiTchkTerm", 62, [TimingFamily]>;
def TimeVar : VPIObjectKind<"vpiTimeVar", 63, [VariableFamily]>;
def TimeQueue : VPIObjectKind<"vpiTimeQueue", 64, [RuntimeFamily]>;
def Udp : VPIObjectKind<"vpiUdp", 65, [PrimitiveFamily]>;
def UdpDefn : VPIObjectKind<"vpiUdpDefn", 66, [DeclarationFamily]>;
def UserSystf : VPIObjectKind<"vpiUserSystf", 67, [RuntimeFamily]>;
def VarSelect : VPIObjectKind<"vpiVarSelect", 68, [ExpressionFamily]>;
def Wait : VPIObjectKind<"vpiWait", 69, [StatementFamily]>;
def While : VPIObjectKind<"vpiWhile", 70, [StatementFamily]>;
def Attribute : VPIObjectKind<"vpiAttribute", 105, [DeclarationFamily]>;
def BitSelect : VPIObjectKind<"vpiBitSelect", 106, [ExpressionFamily]>;
def Callback : VPIObjectKind<"vpiCallback", 107, [RuntimeFamily]>;
def DelayTerm : VPIObjectKind<"vpiDelayTerm", 108, [TimingFamily]>;
def DelayDevice : VPIObjectKind<"vpiDelayDevice", 109, [TimingFamily]>;
def Frame : VPIObjectKind<"vpiFrame", 110, [TransientFamily]>;
def GateArray : VPIObjectKind<"vpiGateArray", 111,
  [ArrayFamily, PrimitiveFamily]>;
def ModuleArray : VPIObjectKind<"vpiModuleArray", 112, [ArrayFamily]>;
def PrimitiveArray : VPIObjectKind<"vpiPrimitiveArray", 113,
  [ArrayFamily, PrimitiveFamily], "", AbstractSelectorRole>;
def NetArray : VPIObjectKind<"vpiNetArray", 114,
  [ArrayFamily, NetFamily]>;
def Range : VPIObjectKind<"vpiRange", 115, [DeclarationFamily]>;
def RegArray : VPIObjectKind<"vpiRegArray", 116,
  [ArrayFamily, VariableFamily]>;
def SwitchArray : VPIObjectKind<"vpiSwitchArray", 117,
  [ArrayFamily, PrimitiveFamily]>;
def UdpArray : VPIObjectKind<"vpiUdpArray", 118,
  [ArrayFamily, PrimitiveFamily]>;
def ContAssignBit : VPIObjectKind<"vpiContAssignBit", 128,
  [StatementFamily]>;
def NamedEventArray : VPIObjectKind<"vpiNamedEventArray", 129,
  [ArrayFamily, VariableFamily]>;
def IndexedPartSelect : VPIObjectKind<"vpiIndexedPartSelect", 130,
  [ExpressionFamily]>;
def GenScopeArray : VPIObjectKind<"vpiGenScopeArray", 133, [ArrayFamily]>;
def GenScope : VPIObjectKind<"vpiGenScope", 134, [ScopeFamily]>;
def GenVar : VPIObjectKind<"vpiGenVar", 135, [DeclarationFamily]>;

// SystemVerilog VPI extensions.  Aliases remain explicit so generated ABI
// checks cover every public spelling without inventing duplicate semantics.
def Package : VPIObjectKind<"vpiPackage", 600, [ScopeFamily]>;
def Interface : VPIObjectKind<"vpiInterface", 601, [ScopeFamily]>;
def Program : VPIObjectKind<"vpiProgram", 602, [ScopeFamily]>;
def InterfaceArray : VPIObjectKind<"vpiInterfaceArray", 603, [ArrayFamily]>;
def ProgramArray : VPIObjectKind<"vpiProgramArray", 604, [ArrayFamily]>;
def Typespec : VPIObjectKind<"vpiTypespec", 605, [TypespecFamily], "",
  AbstractSelectorRole>;
def Modport : VPIObjectKind<"vpiModport", 606, [DeclarationFamily]>;
def InterfaceTfDecl : VPIObjectKind<"vpiInterfaceTfDecl", 607,
  [DeclarationFamily]>;
def RefObj : VPIObjectKind<"vpiRefObj", 608, [ExpressionFamily]>;
def TypeParameter : VPIObjectKind<"vpiTypeParameter", 609,
  [DeclarationFamily, TypespecFamily]>;
def VarBit : VPIObjectKind<"vpiVarBit", 49, [VariableFamily], "vpiRegBit">;
def LongIntVar : VPIObjectKind<"vpiLongIntVar", 610, [VariableFamily]>;
def ShortIntVar : VPIObjectKind<"vpiShortIntVar", 611, [VariableFamily]>;
def IntVar : VPIObjectKind<"vpiIntVar", 612, [VariableFamily]>;
def ShortRealVar : VPIObjectKind<"vpiShortRealVar", 613, [VariableFamily]>;
def ByteVar : VPIObjectKind<"vpiByteVar", 614, [VariableFamily]>;
def ClassVar : VPIObjectKind<"vpiClassVar", 615, [VariableFamily]>;
def StringVar : VPIObjectKind<"vpiStringVar", 616, [VariableFamily]>;
def EnumVar : VPIObjectKind<"vpiEnumVar", 617, [VariableFamily]>;
def StructVar : VPIObjectKind<"vpiStructVar", 618, [VariableFamily]>;
def UnionVar : VPIObjectKind<"vpiUnionVar", 619, [VariableFamily]>;
def BitVar : VPIObjectKind<"vpiBitVar", 620, [VariableFamily]>;
def LogicVar : VPIObjectKind<"vpiLogicVar", 48, [VariableFamily], "vpiReg">;
def ArrayVar : VPIObjectKind<"vpiArrayVar", 116,
  [ArrayFamily, VariableFamily],
  "vpiRegArray">;
def ClassObj : VPIObjectKind<"vpiClassObj", 621, [TransientFamily]>;
def ChandleVar : VPIObjectKind<"vpiChandleVar", 622, [VariableFamily]>;
def PackedArrayVar : VPIObjectKind<"vpiPackedArrayVar", 623,
  [ArrayFamily, VariableFamily]>;
def VirtualInterfaceVar : VPIObjectKind<"vpiVirtualInterfaceVar", 728,
  [VariableFamily]>;

def LongIntTypespec : VPIObjectKind<"vpiLongIntTypespec", 625,
  [TypespecFamily]>;
def ShortRealTypespec : VPIObjectKind<"vpiShortRealTypespec", 626,
  [TypespecFamily]>;
def ByteTypespec : VPIObjectKind<"vpiByteTypespec", 627, [TypespecFamily]>;
def ShortIntTypespec : VPIObjectKind<"vpiShortIntTypespec", 628,
  [TypespecFamily]>;
def IntTypespec : VPIObjectKind<"vpiIntTypespec", 629, [TypespecFamily]>;
def ClassTypespec : VPIObjectKind<"vpiClassTypespec", 630,
  [ScopeFamily, TypespecFamily]>;
def StringTypespec : VPIObjectKind<"vpiStringTypespec", 631,
  [TypespecFamily]>;
def ChandleTypespec : VPIObjectKind<"vpiChandleTypespec", 632,
  [TypespecFamily]>;
def EnumTypespec : VPIObjectKind<"vpiEnumTypespec", 633, [TypespecFamily]>;
def EnumConst : VPIObjectKind<"vpiEnumConst", 634,
  [DeclarationFamily, ExpressionFamily]>;
def IntegerTypespec : VPIObjectKind<"vpiIntegerTypespec", 635,
  [TypespecFamily]>;
def TimeTypespec : VPIObjectKind<"vpiTimeTypespec", 636, [TypespecFamily]>;
def RealTypespec : VPIObjectKind<"vpiRealTypespec", 637, [TypespecFamily]>;
def StructTypespec : VPIObjectKind<"vpiStructTypespec", 638,
  [TypespecFamily]>;
def UnionTypespec : VPIObjectKind<"vpiUnionTypespec", 639,
  [TypespecFamily]>;
def BitTypespec : VPIObjectKind<"vpiBitTypespec", 640, [TypespecFamily]>;
def LogicTypespec : VPIObjectKind<"vpiLogicTypespec", 641,
  [TypespecFamily]>;
def ArrayTypespec : VPIObjectKind<"vpiArrayTypespec", 642,
  [TypespecFamily]>;
def VoidTypespec : VPIObjectKind<"vpiVoidTypespec", 643, [TypespecFamily]>;
def TypespecMember : VPIObjectKind<"vpiTypespecMember", 644,
  [DeclarationFamily, TypespecFamily]>;
def PackedArrayTypespec : VPIObjectKind<"vpiPackedArrayTypespec", 692,
  [TypespecFamily]>;
def SequenceTypespec : VPIObjectKind<"vpiSequenceTypespec", 696,
  [TypespecFamily]>;
def PropertyTypespec : VPIObjectKind<"vpiPropertyTypespec", 697,
  [TypespecFamily]>;
def EventTypespec : VPIObjectKind<"vpiEventTypespec", 698, [TypespecFamily]>;
def InterfaceTypespec : VPIObjectKind<"vpiInterfaceTypespec", 906,
  [TypespecFamily]>;

def ClockingBlock : VPIObjectKind<"vpiClockingBlock", 650, [ScopeFamily]>;
def ClockingIODecl : VPIObjectKind<"vpiClockingIODecl", 651,
  [DeclarationFamily]>;
def ClassDefn : VPIObjectKind<"vpiClassDefn", 652,
  [DeclarationFamily, ScopeFamily]>;
def Constraint : VPIObjectKind<"vpiConstraint", 653, [DeclarationFamily]>;
def ConstraintOrdering : VPIObjectKind<"vpiConstraintOrdering", 654,
  [DeclarationFamily]>;
def DistItem : VPIObjectKind<"vpiDistItem", 645, [ExpressionFamily]>;
def AliasStmt : VPIObjectKind<"vpiAliasStmt", 646, [StatementFamily]>;
def Thread : VPIObjectKind<"vpiThread", 647,
  [ProcessFamily, TransientFamily]>;
def MethodFuncCall : VPIObjectKind<"vpiMethodFuncCall", 648,
  [ExpressionFamily]>;
def MethodTaskCall : VPIObjectKind<"vpiMethodTaskCall", 649,
  [StatementFamily]>;
def Assert : VPIObjectKind<"vpiAssert", 686, [AssertionFamily]>;
def Assume : VPIObjectKind<"vpiAssume", 687, [AssertionFamily]>;
def Cover : VPIObjectKind<"vpiCover", 688, [AssertionFamily]>;
def Restrict : VPIObjectKind<"vpiRestrict", 901, [AssertionFamily]>;
def DisableCondition : VPIObjectKind<"vpiDisableCondition", 689,
  [OtherFamily], "", RelationOnlyRole>;
def ClockingEvent : VPIObjectKind<"vpiClockingEvent", 690,
  [OtherFamily], "", RelationOnlyRole>;
def PropertyDecl : VPIObjectKind<"vpiPropertyDecl", 655,
  [AssertionFamily, DeclarationFamily]>;
def PropertySpec : VPIObjectKind<"vpiPropertySpec", 656, [AssertionFamily]>;
def PropertyExpr : VPIObjectKind<"vpiPropertyExpr", 657,
  [AssertionFamily, ExpressionFamily]>;
def MulticlockSequenceExpr : VPIObjectKind<"vpiMulticlockSequenceExpr", 658,
  [AssertionFamily, ExpressionFamily]>;
def ClockedSeq : VPIObjectKind<"vpiClockedSeq", 659, [AssertionFamily]>;
def ClockedProp : VPIObjectKind<"vpiClockedProp", 902, [AssertionFamily]>;
def PropertyInst : VPIObjectKind<"vpiPropertyInst", 660,
  [AssertionFamily, ExpressionFamily]>;
def SequenceDecl : VPIObjectKind<"vpiSequenceDecl", 661,
  [AssertionFamily, DeclarationFamily]>;
def CaseProperty : VPIObjectKind<"vpiCaseProperty", 662,
  [AssertionFamily, ExpressionFamily]>;
def CasePropertyItem : VPIObjectKind<"vpiCasePropertyItem", 905,
  [AssertionFamily]>;
def SequenceInst : VPIObjectKind<"vpiSequenceInst", 664,
  [AssertionFamily, ExpressionFamily]>;
def ImmediateAssert : VPIObjectKind<"vpiImmediateAssert", 665,
  [AssertionFamily, StatementFamily]>;
def ImmediateAssume : VPIObjectKind<"vpiImmediateAssume", 694,
  [AssertionFamily, StatementFamily]>;
def ImmediateCover : VPIObjectKind<"vpiImmediateCover", 695,
  [AssertionFamily, StatementFamily]>;
def Return : VPIObjectKind<"vpiReturn", 666, [OtherFamily], "",
  RelationOnlyRole>;
def AnyPattern : VPIObjectKind<"vpiAnyPattern", 667, [ExpressionFamily]>;
def TaggedPattern : VPIObjectKind<"vpiTaggedPattern", 668,
  [ExpressionFamily]>;
def StructPattern : VPIObjectKind<"vpiStructPattern", 669,
  [ExpressionFamily]>;
def DoWhile : VPIObjectKind<"vpiDoWhile", 670, [StatementFamily]>;
def OrderedWait : VPIObjectKind<"vpiOrderedWait", 671, [StatementFamily]>;
def WaitFork : VPIObjectKind<"vpiWaitFork", 672, [StatementFamily]>;
def DisableFork : VPIObjectKind<"vpiDisableFork", 673, [StatementFamily]>;
def ExpectStmt : VPIObjectKind<"vpiExpectStmt", 674,
  [AssertionFamily, StatementFamily]>;
def ForeachStmt : VPIObjectKind<"vpiForeachStmt", 675,
  [StatementFamily, ScopeFamily]>;
def ReturnStmt : VPIObjectKind<"vpiReturnStmt", 691, [StatementFamily]>;
def Final : VPIObjectKind<"vpiFinal", 676, [ProcessFamily]>;
def Extends : VPIObjectKind<"vpiExtends", 677, [DeclarationFamily]>;
def Distribution : VPIObjectKind<"vpiDistribution", 678,
  [ExpressionFamily]>;
def SeqFormalDecl : VPIObjectKind<"vpiSeqFormalDecl", 679,
  [AssertionFamily, DeclarationFamily]>;
def PropFormalDecl : VPIObjectKind<"vpiPropFormalDecl", 699,
  [AssertionFamily, DeclarationFamily]>;
def ArrayNet : VPIObjectKind<"vpiArrayNet", 114,
  [ArrayFamily, NetFamily], "vpiNetArray">;
def EnumNet : VPIObjectKind<"vpiEnumNet", 680, [NetFamily]>;
def IntegerNet : VPIObjectKind<"vpiIntegerNet", 681, [NetFamily]>;
def LogicNet : VPIObjectKind<"vpiLogicNet", 36, [NetFamily], "vpiNet">;
def TimeNet : VPIObjectKind<"vpiTimeNet", 682, [NetFamily]>;
def UnionNet : VPIObjectKind<"vpiUnionNet", 525, [NetFamily]>;
def ShortRealNet : VPIObjectKind<"vpiShortRealNet", 526, [NetFamily]>;
def RealNet : VPIObjectKind<"vpiRealNet", 527, [NetFamily]>;
def ByteNet : VPIObjectKind<"vpiByteNet", 528, [NetFamily]>;
def ShortIntNet : VPIObjectKind<"vpiShortIntNet", 529, [NetFamily]>;
def IntNet : VPIObjectKind<"vpiIntNet", 530, [NetFamily]>;
def LongIntNet : VPIObjectKind<"vpiLongIntNet", 531, [NetFamily]>;
def BitNet : VPIObjectKind<"vpiBitNet", 532, [NetFamily]>;
def InterconnectNet : VPIObjectKind<"vpiInterconnectNet", 533, [NetFamily]>;
def InterconnectArray : VPIObjectKind<"vpiInterconnectArray", 534,
  [ArrayFamily, NetFamily]>;
def StructNet : VPIObjectKind<"vpiStructNet", 683, [NetFamily]>;
def Break : VPIObjectKind<"vpiBreak", 684, [StatementFamily]>;
def Continue : VPIObjectKind<"vpiContinue", 685, [StatementFamily]>;
def PackedArrayNet : VPIObjectKind<"vpiPackedArrayNet", 693,
  [ArrayFamily, NetFamily]>;
def NettypeDecl : VPIObjectKind<"vpiNettypeDecl", 523,
  [DeclarationFamily, TypespecFamily]>;
def ConstraintExpr : VPIObjectKind<"vpiConstraintExpr", 747,
  [ExpressionFamily], "", AbstractSelectorRole>;
def ElseConst : VPIObjectKind<"vpiElseConst", 748, [OtherFamily], "",
  RelationOnlyRole>;
def Implication : VPIObjectKind<"vpiImplication", 749, [ExpressionFamily]>;
def ConstrIf : VPIObjectKind<"vpiConstrIf", 738, [ExpressionFamily]>;
def ConstrIfElse : VPIObjectKind<"vpiConstrIfElse", 739,
  [ExpressionFamily]>;
def ConstrForEach : VPIObjectKind<"vpiConstrForEach", 736,
  [ExpressionFamily]>;
def SoftDisable : VPIObjectKind<"vpiSoftDisable", 733, [ExpressionFamily]>;
def LetDecl : VPIObjectKind<"vpiLetDecl", 903, [DeclarationFamily]>;
def LetExpr : VPIObjectKind<"vpiLetExpr", 904, [ExpressionFamily]>;
def Fsm : VPIObjectKind<"vpiFsm", 758, [RuntimeFamily]>;
def FsmHandle : VPIObjectKind<"vpiFsmHandle", 759, [RuntimeFamily]>;

// IEEE 1800-2017 Table 38-6 is deliberately modeled separately from the
// statement family.  Not every object classified as a statement by Chapter
// 37 is eligible for cbStmt (for example vpiNullStmt and vpiCaseItem).
class VPIStatementCallbackPhase<string cppName_, int value_> {
  string cppName = cppName_;
  int value = value_;
}

def BeforeExecutePhase : VPIStatementCallbackPhase<"BeforeExecute", 0>;
def BeforeForControlsPhase
    : VPIStatementCallbackPhase<"BeforeForControls", 1>;
def BeforeForIncrementPhase
    : VPIStatementCallbackPhase<"BeforeForIncrement", 2>;

class VPIStatementCallbackPolicy<string cppName_, int value_,
                                 list<VPIStatementCallbackPhase> phases_> {
  string cppName = cppName_;
  int value = value_;
  list<VPIStatementCallbackPhase> phases = phases_;
}

def OnceBeforePolicy
    : VPIStatementCallbackPolicy<"OnceBefore", 0, [BeforeExecutePhase]>;
def ConditionEachIterationPolicy : VPIStatementCallbackPolicy<
    "ConditionEachIteration", 1, [BeforeExecutePhase]>;
def RepeatEncounterAndIterationPolicy : VPIStatementCallbackPolicy<
    "RepeatEncounterAndIteration", 2, [BeforeExecutePhase]>;
def ForInitialAndIncrementPolicy : VPIStatementCallbackPolicy<
    "ForInitialAndIncrement", 3,
    [BeforeForControlsPhase, BeforeForIncrementPhase]>;
def ForeverEncounterAndIterationPolicy : VPIStatementCallbackPolicy<
    "ForeverEncounterAndIteration", 4, [BeforeExecutePhase]>;
def DelayEncounterPolicy
    : VPIStatementCallbackPolicy<"DelayEncounter", 5, [BeforeExecutePhase]>;
def EventEncounterPolicy
    : VPIStatementCallbackPolicy<"EventEncounter", 6, [BeforeExecutePhase]>;
def CallBeforePolicy
    : VPIStatementCallbackPolicy<"CallBefore", 7, [BeforeExecutePhase]>;

class VPIStatementCallbackSpec<VPIObjectKind object_,
                               VPIStatementCallbackPolicy policy_> {
  VPIObjectKind object = object_;
  VPIStatementCallbackPolicy policy = policy_;
  string clause = "IEEE 1800-2017 Table 38-6";
}

def BeginCallback : VPIStatementCallbackSpec<Begin, OnceBeforePolicy>;
def NamedBeginCallback
    : VPIStatementCallbackSpec<NamedBegin, OnceBeforePolicy>;
def ForkCallback : VPIStatementCallbackSpec<Fork, OnceBeforePolicy>;
def NamedForkCallback
    : VPIStatementCallbackSpec<NamedFork, OnceBeforePolicy>;
def IfCallback : VPIStatementCallbackSpec<If, OnceBeforePolicy>;
def IfElseCallback : VPIStatementCallbackSpec<IfElse, OnceBeforePolicy>;
def WhileCallback
    : VPIStatementCallbackSpec<While, ConditionEachIterationPolicy>;
def RepeatCallback
    : VPIStatementCallbackSpec<Repeat, RepeatEncounterAndIterationPolicy>;
def ForCallback
    : VPIStatementCallbackSpec<For, ForInitialAndIncrementPolicy>;
def ForeverCallback
    : VPIStatementCallbackSpec<Forever, ForeverEncounterAndIterationPolicy>;
def WaitCallback : VPIStatementCallbackSpec<Wait, OnceBeforePolicy>;
def CaseCallback : VPIStatementCallbackSpec<Case, OnceBeforePolicy>;
def AssignmentCallback
    : VPIStatementCallbackSpec<Assignment, OnceBeforePolicy>;
def AssignStmtCallback
    : VPIStatementCallbackSpec<AssignStmt, OnceBeforePolicy>;
def DeassignCallback : VPIStatementCallbackSpec<Deassign, OnceBeforePolicy>;
def DisableCallback : VPIStatementCallbackSpec<Disable, OnceBeforePolicy>;
def ForceCallback : VPIStatementCallbackSpec<Force, OnceBeforePolicy>;
def ReleaseCallback : VPIStatementCallbackSpec<Release, OnceBeforePolicy>;
def EventStmtCallback : VPIStatementCallbackSpec<EventStmt, OnceBeforePolicy>;
def DelayControlCallback
    : VPIStatementCallbackSpec<DelayControl, DelayEncounterPolicy>;
def EventControlCallback
    : VPIStatementCallbackSpec<EventControl, EventEncounterPolicy>;
def TaskCallCallback : VPIStatementCallbackSpec<TaskCall, CallBeforePolicy>;
def SysTaskCallCallback
    : VPIStatementCallbackSpec<SysTaskCall, CallBeforePolicy>;

class VPIRelationCardinality<string cppName_> {
  string cppName = cppName_;
}
def One : VPIRelationCardinality<"One">;
def Many : VPIRelationCardinality<"Many">;
def OneOrMany : VPIRelationCardinality<"OneOrMany">;

class VPIRelation<string apiName_, int value_,
                  VPIRelationCardinality cardinality_,
                  string aliasOf_ = ""> : VPISelector {
  string apiName = apiName_;
  int value = value_;
  VPIRelationCardinality cardinality = cardinality_;
  string aliasOf = aliasOf_;
}

// One-to-one selectors from vpi_user.h.
// Ordered waits permit vpi_iterate(vpiCondition, ...), while other sources use
// vpi_handle().
def ConditionRel : VPIRelation<"vpiCondition", 71, OneOrMany>;
def DelayRel : VPIRelation<"vpiDelay", 72, One>;
def ElseStmtRel : VPIRelation<"vpiElseStmt", 73, One>;
def ForIncStmtRel : VPIRelation<"vpiForIncStmt", 74, OneOrMany>;
def ForInitStmtRel : VPIRelation<"vpiForInitStmt", 75, OneOrMany>;
def HighConnRel : VPIRelation<"vpiHighConn", 76, One>;
def LhsRel : VPIRelation<"vpiLhs", 77, One>;
// Multidimensional objects permit vpi_iterate(vpiIndex, ...), even though the
// legacy header groups vpiIndex with one-to-one selectors.
def IndexRel : VPIRelation<"vpiIndex", 78, OneOrMany>;
def LeftRangeRel : VPIRelation<"vpiLeftRange", 79, One>;
def LowConnRel : VPIRelation<"vpiLowConn", 80, One>;
def ParentRel : VPIRelation<"vpiParent", 81, One>;
def RhsRel : VPIRelation<"vpiRhs", 82, One>;
def RightRangeRel : VPIRelation<"vpiRightRange", 83, One>;
def ScopeRel : VPIRelation<"vpiScope", 84, One>;
def SysTfCallRel : VPIRelation<"vpiSysTfCall", 85, One>;
def TchkDataTermRel : VPIRelation<"vpiTchkDataTerm", 86, One>;
def TchkNotifierRel : VPIRelation<"vpiTchkNotifier", 87, One>;
def TchkRefTermRel : VPIRelation<"vpiTchkRefTerm", 88, One>;

// One-to-many selectors from vpi_user.h.
def ArgumentRel : VPIRelation<"vpiArgument", 89, Many>;
def BitRel : VPIRelation<"vpiBit", 90, Many>;
def DriverRel : VPIRelation<"vpiDriver", 91, Many>;
def InternalScopeRel : VPIRelation<"vpiInternalScope", 92, Many>;
def LoadRel : VPIRelation<"vpiLoad", 93, Many>;
def ModDataPathInRel : VPIRelation<"vpiModDataPathIn", 94, Many>;
def ModPathInRel : VPIRelation<"vpiModPathIn", 95, Many>;
def ModPathOutRel : VPIRelation<"vpiModPathOut", 96, Many>;
def OperandRel : VPIRelation<"vpiOperand", 97, Many>;
def PortInstRel : VPIRelation<"vpiPortInst", 98, Many>;
def ProcessRel : VPIRelation<"vpiProcess", 99, Many>;
def VariablesRel : VPIRelation<"vpiVariables", 100, Many>;
// vpiUse is one-to-one for iterator-to-reference traversal and one-to-many for
// expression-to-use traversal.
def UseRel : VPIRelation<"vpiUse", 101, OneOrMany>;

// Selectors whose cardinality depends on the reference object.
def ExprRel : VPIRelation<"vpiExpr", 102, OneOrMany>;
def PrimitiveRel : VPIRelation<"vpiPrimitive", 103, OneOrMany>;
def StmtRel : VPIRelation<"vpiStmt", 104, OneOrMany>;

def ActiveTimeFormatRel : VPIRelation<"vpiActiveTimeFormat", 119, One>;
def InTermRel : VPIRelation<"vpiInTerm", 120, One>;
def InstanceArrayRel : VPIRelation<"vpiInstanceArray", 121, One>;
def LocalDriverRel : VPIRelation<"vpiLocalDriver", 122, Many>;
def LocalLoadRel : VPIRelation<"vpiLocalLoad", 123, Many>;
def OutTermRel : VPIRelation<"vpiOutTerm", 124, One>;
def PortsRel : VPIRelation<"vpiPorts", 125, Many>;
def SimNetRel : VPIRelation<"vpiSimNet", 126, One>;
def TaskFuncRel : VPIRelation<"vpiTaskFunc", 127, OneOrMany>;
def BaseExprRel : VPIRelation<"vpiBaseExpr", 131, One>;
def WidthExprRel : VPIRelation<"vpiWidthExpr", 132, One>;
def AutomaticsRel : VPIRelation<"vpiAutomatics", 136, Many>;

// SystemVerilog one-to-one selectors.
def ActualRel : VPIRelation<"vpiActual", 700, One>;
def TypedefAliasRel : VPIRelation<"vpiTypedefAlias", 701, One>;
def IndexTypespecRel : VPIRelation<"vpiIndexTypespec", 702, One>;
def BaseTypespecRel : VPIRelation<"vpiBaseTypespec", 703, One>;
def ElemTypespecRel : VPIRelation<"vpiElemTypespec", 704, One>;
def NetTypedefAliasRel : VPIRelation<"vpiNetTypedefAlias", 705, One>;
def InputSkewRel : VPIRelation<"vpiInputSkew", 706, One>;
def OutputSkewRel : VPIRelation<"vpiOutputSkew", 707, One>;
def GlobalClockingRel : VPIRelation<"vpiGlobalClocking", 708, One>;
def DefaultClockingRel : VPIRelation<"vpiDefaultClocking", 709, One>;
def DefaultDisableIffRel : VPIRelation<"vpiDefaultDisableIff", 710, One>;
def OriginRel : VPIRelation<"vpiOrigin", 713, One>;
def PrefixRel : VPIRelation<"vpiPrefix", 714, One>;
def WithRel : VPIRelation<"vpiWith", 715, One>;
def PropertyRel : VPIRelation<"vpiProperty", 718, One>;
def ValueRangeRel : VPIRelation<"vpiValueRange", 720, One>;
def PatternRel : VPIRelation<"vpiPattern", 721, One>;
def WeightRel : VPIRelation<"vpiWeight", 722, One>;
// Clause 37.32 normatively defines ordered iteration for this selector; the
// one-to-one grouping in sv_vpi_user.h is stale.
def ConstraintItemRel : VPIRelation<"vpiConstraintItem", 746, Many>;
def ReturnRel : VPIRelation<"vpiReturn", 666, One>;
def ExtendsRel : VPIRelation<"vpiExtends", 677, One>;
def DisableConditionRel : VPIRelation<"vpiDisableCondition", 689, One>;
def ClockingEventRel : VPIRelation<"vpiClockingEvent", 690, One>;
def ElseConstRel : VPIRelation<"vpiElseConst", 748, One>;

// SystemVerilog one-to-many selectors.
def TypedefRel : VPIRelation<"vpiTypedef", 725, Many>;
def ImportRel : VPIRelation<"vpiImport", 726, Many>;
def DerivedClassesRel : VPIRelation<"vpiDerivedClasses", 727, Many>;
// Deprecated spelling for the vpiVirtualInterfaceVar object selector.  The
// relation and object namespaces form a selector union at the API boundary.
def InterfaceDeclRel : VPIRelation<"vpiInterfaceDecl", 728, Many,
  "vpiVirtualInterfaceVar">;
def MethodsRel : VPIRelation<"vpiMethods", 730, Many>;
def SolveBeforeRel : VPIRelation<"vpiSolveBefore", 731, Many>;
def SolveAfterRel : VPIRelation<"vpiSolveAfter", 732, Many>;
def WaitingProcessesRel : VPIRelation<"vpiWaitingProcesses", 734, Many>;
def MessagesRel : VPIRelation<"vpiMessages", 735, Many>;
def LoopVarsRel : VPIRelation<"vpiLoopVars", 737, Many>;
def ConcurrentAssertionRel : VPIRelation<"vpiConcurrentAssertion", 740,
  Many>;
def ConcurrentAssertionsRel : VPIRelation<"vpiConcurrentAssertions", 740,
  Many, "vpiConcurrentAssertion">;
def MatchItemRel : VPIRelation<"vpiMatchItem", 741, Many>;
def MemberRel : VPIRelation<"vpiMember", 742, Many>;
def ElementRel : VPIRelation<"vpiElement", 743, Many>;
def AssertionRel : VPIRelation<"vpiAssertion", 744, Many>;
def InstanceRel : VPIRelation<"vpiInstance", 745, OneOrMany>;
def FsmStatesRel : VPIRelation<"vpiFsmStates", 775, Many>;
def FsmStateExpressionRel : VPIRelation<"vpiFsmStateExpression", 776, One>;
