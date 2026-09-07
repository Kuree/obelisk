//===- VPITraversalLate.td - IEEE VPI late traversal edges --------*- tablegen -*-===//
//
// Standalone continuation of VPITraversalModel.td for IEEE 1800-2017
// clauses 37.49 through 37.83.  This file is intended to be included after
// the common object sets in VPITraversalModel.td.
//
//===----------------------------------------------------------------------===//

// Late-clause exact source and target unions.  Keep these names distinct from
// the reusable sets in VPITraversalModel.td so this remains an include
// fragment rather than another top-level model.
def LatePropertyDeclSource : VPIObjectSet<"LatePropertyDeclSource",
  [PropertyDecl]>;
def LatePropertyInstSource : VPIObjectSet<"LatePropertyInstSource",
  [PropertyInst]>;
def LatePropFormalSource : VPIObjectSet<"LatePropFormalSource",
  [PropFormalDecl]>;
def LatePropertyArgumentTargets : VPIObjectSet<"LatePropertyArgumentTargets",
  [PropertyDecl, RefObj, VarSelect, BitSelect, IndexedPartSelect, PartSelect,
   Operation, Constant, FuncCall, MethodFuncCall, SysFuncCall, LetExpr,
   SequenceInst, Distribution, PropertyExpr, MulticlockSequenceExpr,
   PropertyInst, ClockedProp, CaseProperty], [NetFamily, VariableFamily],
  [NamedEventArray, VirtualInterfaceVar]>;
def LatePropertyFormalExprTargets : VPIObjectSet<
  "LatePropertyFormalExprTargets",
  [RefObj, VarSelect, BitSelect, IndexedPartSelect, PartSelect,
   Operation, Constant, FuncCall, MethodFuncCall, SysFuncCall, LetExpr,
   SequenceInst, Distribution, PropertyExpr, MulticlockSequenceExpr,
   PropertyInst, ClockedProp, CaseProperty], [NetFamily, VariableFamily],
  [NamedEventArray, VirtualInterfaceVar]>;

def LatePropertySpecSource : VPIObjectSet<"LatePropertySpecSource",
  [PropertySpec]>;
def LatePropertyExprSource : VPIObjectSet<"LatePropertyExprSource",
  [PropertyExpr]>;
// These sets mirror the abstract nodes in the LRM diagrams.  ExpressionFamily
// alone is too broad: it also contains property, constraint, and pattern
// objects, while the simple-expression node additionally contains nets and
// variables.
def LateSimpleExpressionTargets : VPIObjectSet<"LateSimpleExpressionTargets",
  [RefObj, VarSelect, BitSelect], [NetFamily, VariableFamily],
  [NamedEvent, NamedEventArray, VirtualInterfaceVar]>;
def LateOrdinaryExpressionTargets : VPIObjectSet<
  "LateOrdinaryExpressionTargets",
  [RefObj, VarSelect, BitSelect, IndexedPartSelect, PartSelect, Operation,
   Constant, FuncCall, MethodFuncCall, SysFuncCall, LetExpr],
  [NetFamily, VariableFamily],
  [NamedEvent, NamedEventArray, VirtualInterfaceVar]>;
def LateSequenceExprTargets : VPIObjectSet<"LateSequenceExprTargets",
  [RefObj, VarSelect, BitSelect, IndexedPartSelect, PartSelect, Operation,
   Constant, FuncCall, MethodFuncCall, SysFuncCall, LetExpr, SequenceInst,
   Distribution], [NetFamily, VariableFamily],
  [NamedEvent, NamedEventArray, VirtualInterfaceVar]>;
def LatePropertyExprTargets : VPIObjectSet<"LatePropertyExprTargets",
  [RefObj, VarSelect, BitSelect, IndexedPartSelect, PartSelect, Operation,
   Constant, FuncCall, MethodFuncCall, SysFuncCall, LetExpr, SequenceInst,
   Distribution, PropertyExpr, MulticlockSequenceExpr, PropertyInst,
   ClockedProp, CaseProperty], [NetFamily, VariableFamily],
  [NamedEvent, NamedEventArray, VirtualInterfaceVar]>;
def LateClockedPropSource : VPIObjectSet<"LateClockedPropSource",
  [ClockedProp]>;
def LateCasePropertySource : VPIObjectSet<"LateCasePropertySource",
  [CaseProperty]>;
def LateCasePropertyItemSource : VPIObjectSet<
  "LateCasePropertyItemSource", [CasePropertyItem]>;

def LateSequenceDeclSource : VPIObjectSet<"LateSequenceDeclSource",
  [SequenceDecl]>;
def LateSequenceInstSource : VPIObjectSet<"LateSequenceInstSource",
  [SequenceInst]>;
def LateSeqFormalSource : VPIObjectSet<"LateSeqFormalSource",
  [SeqFormalDecl]>;
def LateSequenceArgumentTargets : VPIObjectSet<"LateSequenceArgumentTargets",
  [RefObj, VarSelect, BitSelect, IndexedPartSelect, PartSelect, Operation,
   Constant, FuncCall, MethodFuncCall, SysFuncCall, LetExpr, SequenceInst,
   Distribution], [NetFamily, VariableFamily],
  [NamedEventArray, VirtualInterfaceVar]>;
def LateSequenceMatchTargets : VPIObjectSet<"LateSequenceMatchTargets",
  [Assignment, FuncCall, TaskCall, SysFuncCall, SysTaskCall, MethodFuncCall,
   MethodTaskCall]>;
def LateSequenceMatchSource : VPIObjectSet<"LateSequenceMatchSource",
  [RefObj, VarSelect, BitSelect, IndexedPartSelect, PartSelect, Operation,
   Constant, FuncCall, MethodFuncCall, SysFuncCall, LetExpr, SequenceInst,
   Distribution], [NetFamily, VariableFamily],
  [NamedEvent, NamedEventArray, VirtualInterfaceVar]>;

def LateImmediateAssertAssumeSource : VPIObjectSet<
  "LateImmediateAssertAssumeSource", [ImmediateAssert, ImmediateAssume]>;
def LateImmediateAssertionSource : VPIObjectSet<
  "LateImmediateAssertionSource",
  [ImmediateAssert, ImmediateAssume, ImmediateCover]>;

def LateMulticlockSource : VPIObjectSet<"LateMulticlockSource",
  [MulticlockSequenceExpr]>;
def LateClockedSeqSource : VPIObjectSet<"LateClockedSeqSource", [ClockedSeq]>;

def LateLetDeclSource : VPIObjectSet<"LateLetDeclSource", [LetDecl]>;
def LateLetExprSource : VPIObjectSet<"LateLetExprSource", [LetExpr]>;

def LateSimpleExprSource : VPIObjectSet<"LateSimpleExprSource",
  [RefObj, VarSelect, BitSelect], [NetFamily, VariableFamily],
  [NamedEvent, NamedEventArray, VirtualInterfaceVar]>;
def LateSimpleUseTargets : VPIObjectSet<"LateSimpleUseTargets",
  [PrimTerm, PathTerm, TchkTerm, DelayTerm, Port, ContAssign, ContAssignBit,
   AssignStmt, Assignment, Begin, Case, Deassign, DelayControl, Disable,
   EventControl, EventStmt, For, Force, Forever, Fork, If, IfElse, NamedBegin,
   NamedFork, NullStmt, Release, Repeat, RepeatControl, SysTaskCall, TaskCall,
   Wait, While, MethodTaskCall, ImmediateAssert, ImmediateAssume,
   ImmediateCover, DoWhile, OrderedWait, WaitFork, DisableFork, ExpectStmt,
   ForeachStmt, ReturnStmt, Break, Continue]>;
def LateBitSelectSource : VPIObjectSet<"LateBitSelectSource", [BitSelect]>;

def LateIndexedPartSelectSource : VPIObjectSet<
  "LateIndexedPartSelectSource", [IndexedPartSelect]>;
def LatePartSelectSource : VPIObjectSet<"LatePartSelectSource", [PartSelect]>;
def LateSelectSource : VPIObjectSet<"LateSelectSource",
  [PartSelect, IndexedPartSelect]>;
def LateOperationSource : VPIObjectSet<"LateOperationSource", [Operation]>;
def LateExprOperandTargets : VPIObjectSet<"LateExprOperandTargets",
  [Range, Interface, Modport, RefObj, VarSelect, BitSelect, IndexedPartSelect,
   PartSelect, Operation, Constant, FuncCall, MethodFuncCall, SysFuncCall,
   LetExpr, AnyPattern, TaggedPattern, StructPattern, SequenceInst,
   Distribution, PropertyExpr, MulticlockSequenceExpr, PropertyInst,
   ClockedProp, CaseProperty], [NetFamily, VariableFamily],
  [NamedEvent, NamedEventArray]>;
def LateTypespecExpressionSource : VPIObjectSet<
  "LateTypespecExpressionSource", [], [ExpressionFamily],
  [RefObj, VarSelect, BitSelect, PartSelect, IndexedPartSelect, EnumConst]>;

def LateDynamicPrefixSource : VPIObjectSet<"LateDynamicPrefixSource",
  [RefObj, VarSelect, BitSelect, IndexedPartSelect, PartSelect, FuncCall,
   TaskCall, SysFuncCall, SysTaskCall, MethodFuncCall, MethodTaskCall],
  [NetFamily, VariableFamily]>;
def LateDynamicPrefixTargets : VPIObjectSet<"LateDynamicPrefixTargets",
  [ClockingBlock, RefObj, VarSelect, BitSelect, IndexedPartSelect, PartSelect,
   Operation, Constant, FuncCall, MethodFuncCall, SysFuncCall, LetExpr],
  [NetFamily, VariableFamily], [NamedEvent, NamedEventArray]>;

def LateEventStmtSource : VPIObjectSet<"LateEventStmtSource", [EventStmt]>;
def LateProcessSource : VPIObjectSet<"LateProcessSource",
  [Initial, Final, Always]>;
def LateAssignmentSource : VPIObjectSet<"LateAssignmentSource", [Assignment]>;
def LateAssignmentLhsTargets : VPIObjectSet<"LateAssignmentLhsTargets",
  [Interface, Modport, RefObj, VarSelect, BitSelect, IndexedPartSelect,
   PartSelect, Operation, Constant, FuncCall, MethodFuncCall, SysFuncCall,
   LetExpr], [NetFamily, VariableFamily], [NamedEvent, NamedEventArray]>;
def LateEventControlSource : VPIObjectSet<"LateEventControlSource",
  [EventControl]>;
def LateEventConditionTargets : VPIObjectSet<"LateEventConditionTargets",
  [RefObj, VarSelect, BitSelect, IndexedPartSelect, PartSelect,
   Operation, Constant, FuncCall, MethodFuncCall, SysFuncCall, LetExpr,
   SequenceInst], [NetFamily, VariableFamily],
  [NamedEventArray, VirtualInterfaceVar]>;
def LateDisableConditionTargets : VPIObjectSet<"LateDisableConditionTargets",
  [Distribution, RefObj, VarSelect, BitSelect, IndexedPartSelect, PartSelect,
   Operation, Constant, FuncCall, MethodFuncCall, SysFuncCall, LetExpr],
  [NetFamily, VariableFamily],
  [NamedEvent, NamedEventArray, VirtualInterfaceVar]>;
def LateWhileRepeatSource : VPIObjectSet<"LateWhileRepeatSource",
  [While, Repeat]>;
def LateWaitSource : VPIObjectSet<"LateWaitSource", [Wait]>;
def LateOrderedWaitSource : VPIObjectSet<"LateOrderedWaitSource",
  [OrderedWait]>;
def LateOrderedWaitConditionTargets : VPIObjectSet<
  "LateOrderedWaitConditionTargets",
  [RefObj, VarSelect, BitSelect, IndexedPartSelect, PartSelect, Operation,
   Constant, FuncCall, MethodFuncCall, SysFuncCall, LetExpr, SequenceInst],
  [NetFamily, VariableFamily],
  [NamedEvent, NamedEventArray, VirtualInterfaceVar]>;
def LateDelayControlSource : VPIObjectSet<"LateDelayControlSource",
  [DelayControl]>;
def LateRepeatControlSource : VPIObjectSet<"LateRepeatControlSource",
  [RepeatControl]>;
def LateForeverSource : VPIObjectSet<"LateForeverSource", [Forever]>;
def LateIfSource : VPIObjectSet<"LateIfSource", [If, IfElse]>;
def LateIfElseSource : VPIObjectSet<"LateIfElseSource", [IfElse]>;

def LateCaseSource : VPIObjectSet<"LateCaseSource", [Case]>;
def LateCaseItemSource : VPIObjectSet<"LateCaseItemSource", [CaseItem]>;
def LateCaseExprTargets : VPIObjectSet<"LateCaseExprTargets",
  [RefObj, VarSelect, BitSelect, IndexedPartSelect, PartSelect, Operation,
   Constant, FuncCall, MethodFuncCall, SysFuncCall, LetExpr],
  [NetFamily, VariableFamily],
  [NamedEvent, NamedEventArray, VirtualInterfaceVar]>;
def LatePatternSource : VPIObjectSet<"LatePatternSource",
  [AnyPattern, TaggedPattern, StructPattern]>;
def LateTaggedPatternSource : VPIObjectSet<"LateTaggedPatternSource",
  [TaggedPattern]>;

def LateExpectSource : VPIObjectSet<"LateExpectSource", [ExpectStmt]>;
def LateForSource : VPIObjectSet<"LateForSource", [For]>;
def LateDoWhileSource : VPIObjectSet<"LateDoWhileSource", [DoWhile]>;
def LateForeachSource : VPIObjectSet<"LateForeachSource", [ForeachStmt]>;
def LateForeachLoopTargets : VPIObjectSet<"LateForeachLoopTargets",
  [IntegerVar, Reg, RegBit, TimeVar, LongIntVar, ShortIntVar, IntVar,
   ByteVar, ClassVar, StringVar, EnumVar, StructVar, UnionVar, BitVar,
   ChandleVar, RegArray, PackedArrayVar, Operation]>;
def LateForeachIndexedTargets : VPIObjectSet<"LateForeachIndexedTargets",
  [RegArray, PackedArrayVar, StringVar]>;
def LateAliasSource : VPIObjectSet<"LateAliasSource", [AliasStmt]>;
def LateDisableSource : VPIObjectSet<"LateDisableSource", [Disable]>;
def LateDisableTargets : VPIObjectSet<"LateDisableTargets",
  [Task, Function, NamedBegin, NamedFork]>;
def LateReturnSource : VPIObjectSet<"LateReturnSource", [ReturnStmt]>;
def LateProceduralAssignLhsSource : VPIObjectSet<
  "LateProceduralAssignLhsSource", [AssignStmt, Force, Deassign, Release]>;
def LateProceduralAssignRhsSource : VPIObjectSet<
  "LateProceduralAssignRhsSource", [AssignStmt, Force]>;

def LateCallbackOwnerSource : VPIObjectSet<"LateCallbackOwnerSource",
  [PrimTerm, TimeQueue, RefObj, VarSelect, BitSelect, IndexedPartSelect,
   PartSelect, Operation, Constant, FuncCall, MethodFuncCall, SysFuncCall,
   LetExpr, AssignStmt, Assignment, Begin, Case, Deassign, DelayControl,
   Disable, EventControl, EventStmt, For, Force, Forever, Fork, If, IfElse,
   NamedBegin, NamedFork, NullStmt, Release, Repeat, RepeatControl,
   SysTaskCall, TaskCall, Wait, While, MethodTaskCall, ImmediateAssert,
   ImmediateAssume, ImmediateCover, DoWhile, OrderedWait, WaitFork,
   DisableFork, ExpectStmt, ForeachStmt, ReturnStmt, Break, Continue],
  [NetFamily, VariableFamily]>;
def LateAttributeOwnerSource : VPIObjectSet<"LateAttributeOwnerSource",
  [Package, Module, Interface, Program, Port, PathTerm, ModPath, Tchk,
   ParamAssign, Task, Function, TableEntry, Operation, Assert,
   Assume, Cover, Restrict, SequenceDecl, PropertyDecl, ClockingBlock,
   ClassDefn, Constraint, PrimTerm, Gate, Switch, Udp, Initial, Final, Always],
  [NetFamily, VariableFamily, StatementFamily],
  [CaseItem, ContAssign, ContAssignBit, AliasStmt]>;
def LateIteratorSource : VPIObjectSet<"LateIteratorSource", [Iterator]>;
// vpiUse returns the reference passed to vpi_iterate().  Derive the target
// closure from the model itself so adding a legal iterator cannot silently
// make this relation incomplete.
def LateIteratorUseTargets : VPIObjectSet<"LateIteratorUseTargets",
  [], [], [], 0, 1>;

def LateGeneratedSource : VPIObjectSet<"LateGeneratedSource",
  [Module, GenVar, GenScope]>;
def LateGenScopeSource : VPIObjectSet<"LateGenScopeSource", [GenScope]>;
def LateGenScopeArraySource : VPIObjectSet<"LateGenScopeArraySource",
  [GenScopeArray]>;

// 37.49 Property declaration.
def LatePropertyInstDecl : VPITraversalEdge<LatePropertyInstSource,
  PropertyDecl, HandleMode, PropertyDeclObjects, NoOrder, "37.49">;
def LatePropertyDeclFormals : VPITraversalEdge<LatePropertyDeclSource,
  PropFormalDecl, IterateMode, PropFormalObjects, DeclarationOrder, "37.49">;
def LatePropertyDeclVariables : VPITraversalEdge<LatePropertyDeclSource,
  VariablesRel, IterateMode, DataVariableObjects, NoOrder, "37.49">;
def LatePropertyDeclSpec : VPITraversalEdge<LatePropertyDeclSource,
  PropertySpec, HandleMode, PropertySpecObjects, NoOrder, "37.49">;
def LatePropertyInstDisable : VPITraversalEdge<LatePropertyInstSource,
  DisableConditionRel, HandleMode, LateOrdinaryExpressionTargets, NoOrder,
  "37.49">;
def LatePropertyInstArguments : VPITraversalEdge<LatePropertyInstSource,
  ArgumentRel, IterateMode, LatePropertyArgumentTargets, DeclarationOrder,
  "37.49">;
def LatePropertyFormalExpr : VPITraversalEdge<LatePropFormalSource, ExprRel,
  HandleMode, LatePropertyFormalExprTargets, NoOrder, "37.49">;
def LatePropertyFormalTypespec : VPITraversalEdge<LatePropFormalSource,
  Typespec, HandleMode, ConcreteTypespecObjects, NoOrder, "37.49">;

// 37.50 Property specification.
def LatePropertySpecClock : VPITraversalEdge<LatePropertySpecSource,
  ClockingEventRel, HandleMode, LateOrdinaryExpressionTargets, NoOrder,
  "37.50">;
def LatePropertySpecDisable : VPITraversalEdge<LatePropertySpecSource,
  DisableConditionRel, HandleMode, LateDisableConditionTargets, NoOrder,
  "37.50">;
def LatePropertySpecExpr : VPITraversalEdge<LatePropertySpecSource,
  PropertyExpr, HandleMode, LatePropertyExprTargets, NoOrder, "37.50">;
def LatePropertyExprOperands : VPITraversalEdge<LatePropertyExprSource,
  OperandRel, IterateMode, LatePropertyExprTargets, SourceOrder, "37.50">;
def LateClockedPropertyClock : VPITraversalEdge<LateClockedPropSource,
  ClockingEventRel, HandleMode, LateOrdinaryExpressionTargets, NoOrder,
  "37.50">;
def LateClockedPropertyExpr : VPITraversalEdge<LateClockedPropSource,
  PropertyExpr, HandleMode, LatePropertyExprTargets, NoOrder, "37.50">;
def LateCasePropertyCondition : VPITraversalEdge<LateCasePropertySource,
  ConditionRel, HandleMode, LateOrdinaryExpressionTargets, NoOrder, "37.50">;
def LateCasePropertyItems : VPITraversalEdge<LateCasePropertySource,
  CasePropertyItem, IterateMode, CasePropertyItemObjects, NoOrder,
  "37.50">;
def LateCasePropertyItemExprs : VPITraversalEdge<LateCasePropertyItemSource,
  ExprRel, IterateMode, LateOrdinaryExpressionTargets, NoOrder, "37.50">;
def LateCasePropertyItemProperty : VPITraversalEdge<
  LateCasePropertyItemSource, PropertyExpr, HandleMode, LatePropertyExprTargets,
  NoOrder, "37.50">;

// 37.51 Sequence declaration.
def LateSequenceInstDecl : VPITraversalEdge<LateSequenceInstSource,
  SequenceDecl, HandleMode, SequenceDeclObjects, NoOrder, "37.51">;
def LateSequenceDeclFormals : VPITraversalEdge<LateSequenceDeclSource,
  SeqFormalDecl, IterateMode, SeqFormalObjects, DeclarationOrder, "37.51">;
def LateSequenceDeclVariables : VPITraversalEdge<LateSequenceDeclSource,
  VariablesRel, IterateMode, DataVariableObjects, NoOrder, "37.51">;
def LateSequenceDeclExpr : VPITraversalEdge<LateSequenceDeclSource, ExprRel,
  HandleMode, LateSequenceExprTargets, NoOrder, "37.51">;
def LateSequenceFormalExpr : VPITraversalEdge<LateSeqFormalSource, ExprRel,
  HandleMode, LateSequenceArgumentTargets, NoOrder, "37.51">;
def LateSequenceFormalTypespec : VPITraversalEdge<LateSeqFormalSource,
  Typespec, HandleMode, ConcreteTypespecObjects, NoOrder, "37.51">;

// 37.52 Sequence expression.
def LateSequenceInstArguments : VPITraversalEdge<LateSequenceInstSource,
  ArgumentRel, IterateMode, LateSequenceArgumentTargets, DeclarationOrder,
  "37.52">;
def LateSequenceMatchItems : VPITraversalEdge<LateSequenceMatchSource,
  MatchItemRel, IterateMode, LateSequenceMatchTargets, NoOrder, "37.52">;

// 37.53 Immediate assertions.
def LateImmediateAssertionExpr : VPITraversalEdge<
  LateImmediateAssertionSource, ExprRel, HandleMode,
  LateOrdinaryExpressionTargets, NoOrder, "37.53">;
def LateImmediateAssertionStmt : VPITraversalEdge<
  LateImmediateAssertionSource, StmtRel, HandleMode, StatementObjects,
  NoOrder, "37.53", 1>;
def LateImmediateAssertionElse : VPITraversalEdge<
  LateImmediateAssertAssumeSource, ElseStmtRel, HandleMode, StatementObjects,
  NoOrder, "37.53", 1>;

// 37.54 Multiclock sequence expression.
def LateMulticlockSequences : VPITraversalEdge<LateMulticlockSource,
  ClockedSeq, IterateMode, ClockedSeqObjects, NoOrder, "37.54">;
def LateClockedSequenceClock : VPITraversalEdge<LateClockedSeqSource,
  ClockingEventRel, HandleMode, LateOrdinaryExpressionTargets, NoOrder,
  "37.54">;
def LateClockedSequenceExpr : VPITraversalEdge<LateClockedSeqSource, ExprRel,
  HandleMode, LateSequenceExprTargets, NoOrder, "37.54">;

// 37.55 Let declarations and expressions.
def LateLetExprArguments : VPITraversalEdge<LateLetExprSource, ArgumentRel,
  IterateMode, LateOrdinaryExpressionTargets, DeclarationOrder, "37.55">;
def LateLetDeclExpr : VPITraversalEdge<LateLetDeclSource, ExprRel, HandleMode,
  LateOrdinaryExpressionTargets, NoOrder, "37.55">;
def LateLetDeclFormals : VPITraversalEdge<LateLetDeclSource, SeqFormalDecl,
  IterateMode, SeqFormalObjects, NoOrder, "37.55">;

// 37.56 Simple expressions.
def LateSimpleExprUses : VPITraversalEdge<LateSimpleExprSource, UseRel,
  IterateMode, LateSimpleUseTargets, NoOrder, "37.56">;
def LateBitSelectParent : VPITraversalEdge<LateBitSelectSource, ParentRel,
  HandleMode, LateSimpleExpressionTargets, NoOrder, "37.56">;

// 37.57 Expressions.
def LateIndexedPartSelectBase : VPITraversalEdge<
  LateIndexedPartSelectSource, BaseExprRel, HandleMode,
  LateOrdinaryExpressionTargets, NoOrder, "37.57">;
def LateIndexedPartSelectWidth : VPITraversalEdge<
  LateIndexedPartSelectSource, WidthExprRel, HandleMode,
  LateOrdinaryExpressionTargets, NoOrder, "37.57">;
def LateSelectParent : VPITraversalEdge<LateSelectSource, ParentRel,
  HandleMode, LateSimpleExpressionTargets, NoOrder, "37.57">;
def LatePartSelectLeft : VPITraversalEdge<LatePartSelectSource, LeftRangeRel,
  HandleMode, LateOrdinaryExpressionTargets, NoOrder, "37.57">;
def LatePartSelectRight : VPITraversalEdge<LatePartSelectSource,
  RightRangeRel, HandleMode, LateOrdinaryExpressionTargets, NoOrder, "37.57">;
// vpiOperation is the concrete representation for ordinary, property, and
// sequence operations, so their context-sensitive target unions collapse to
// one dispatch key in the generated legality table.
def LateOperationOperands : VPITraversalEdge<LateOperationSource, OperandRel,
  IterateMode, LateExprOperandTargets, SourceOrder, "37.50/37.52/37.57">;
def LateExpressionTypespec : VPITraversalEdge<LateTypespecExpressionSource,
  Typespec, HandleMode, ConcreteTypespecObjects, NoOrder, "37.57; 37.70">;

// 37.59 Dynamic prefixing.
def LateDynamicPrefix : VPITraversalEdge<LateDynamicPrefixSource, PrefixRel,
  HandleMode, LateDynamicPrefixTargets, NoOrder, "37.40; 37.59">;

// 37.60 Event statement.
def LateEventStmtEvent : VPITraversalEdge<LateEventStmtSource, NamedEvent,
  HandleMode, NamedEventTargetObjects, NoOrder, "37.60">;

// 37.61 Process.
def LateProcessModule : VPITraversalEdge<LateProcessSource, Module,
  HandleMode, ModuleTarget, NoOrder, "37.61", 0, AutomaticParentScope>;
def LateProcessScope : VPITraversalEdge<LateProcessSource, ScopeRel,
  HandleMode, ScopeObjects, NoOrder, "37.61", 0, AutomaticParentScope>;
def LateProcessStmt : VPITraversalEdge<LateProcessSource, StmtRel, HandleMode,
  StatementObjects, NoOrder, "37.61", 1>;

// 37.62 Assignment.
def LateAssignmentLhs : VPITraversalEdge<LateAssignmentSource, LhsRel,
  HandleMode, LateAssignmentLhsTargets, NoOrder, "37.62">;
def LateAssignmentRhs : VPITraversalEdge<LateAssignmentSource, RhsRel,
  HandleMode, LateOrdinaryExpressionTargets, NoOrder, "37.62">;
def LateAssignmentDelayControl : VPITraversalEdge<LateAssignmentSource,
  DelayControl, HandleMode, DelayControlObjects, NoOrder, "37.62", 1>;
def LateAssignmentEventControl : VPITraversalEdge<LateAssignmentSource,
  EventControl, HandleMode, EventControlObjects, NoOrder, "37.62", 1>;
def LateAssignmentRepeatControl : VPITraversalEdge<LateAssignmentSource,
  RepeatControl, HandleMode, RepeatControlObjects, NoOrder, "37.62", 1>;

// 37.63 Event control.
def LateEventControlCondition : VPITraversalEdge<LateEventControlSource,
  ConditionRel, HandleMode, LateEventConditionTargets, NoOrder, "37.63">;
def LateEventControlStmt : VPITraversalEdge<LateEventControlSource, StmtRel,
  HandleMode, StatementObjects, NoOrder, "37.63", 1>;

// 37.64 While and repeat.
def LateWhileRepeatCondition : VPITraversalEdge<LateWhileRepeatSource,
  ConditionRel, HandleMode, LateOrdinaryExpressionTargets, NoOrder, "37.64">;
def LateWhileRepeatStmt : VPITraversalEdge<LateWhileRepeatSource, StmtRel,
  HandleMode, StatementObjects, NoOrder, "37.64", 1>;

// 37.65 Waits.
def LateWaitCondition : VPITraversalEdge<LateWaitSource, ConditionRel,
  HandleMode, LateOrdinaryExpressionTargets, NoOrder, "37.65">;
def LateWaitStmt : VPITraversalEdge<LateWaitSource, StmtRel, HandleMode,
  StatementObjects, NoOrder, "37.65", 1>;
def LateOrderedWaitConditions : VPITraversalEdge<LateOrderedWaitSource,
  ConditionRel, IterateMode, LateOrderedWaitConditionTargets, NoOrder,
  "37.65">;
def LateOrderedWaitStmt : VPITraversalEdge<LateOrderedWaitSource, StmtRel,
  HandleMode, StatementObjects, NoOrder, "37.65", 1>;
def LateOrderedWaitElse : VPITraversalEdge<LateOrderedWaitSource,
  ElseStmtRel, HandleMode, StatementObjects, NoOrder, "37.65", 1>;

// 37.66 Delay control.
def LateDelayControlDelay : VPITraversalEdge<LateDelayControlSource, DelayRel,
  HandleMode, LateOrdinaryExpressionTargets, NoOrder, "37.66">;
def LateDelayControlStmt : VPITraversalEdge<LateDelayControlSource, StmtRel,
  HandleMode, StatementObjects, NoOrder, "37.66", 1>;

// 37.67 Repeat control.
def LateRepeatControlExpr : VPITraversalEdge<LateRepeatControlSource, ExprRel,
  HandleMode, LateOrdinaryExpressionTargets, NoOrder, "37.67">;
def LateRepeatControlEvent : VPITraversalEdge<LateRepeatControlSource,
  EventControl, HandleMode, EventControlObjects, NoOrder, "37.67", 1>;

// 37.68 Forever.
def LateForeverStmt : VPITraversalEdge<LateForeverSource, StmtRel, HandleMode,
  StatementObjects, NoOrder, "37.68", 1>;

// 37.69 If and if-else.
def LateIfCondition : VPITraversalEdge<LateIfSource, ConditionRel, HandleMode,
  LateOrdinaryExpressionTargets, NoOrder, "37.69">;
def LateIfStmt : VPITraversalEdge<LateIfSource, StmtRel, HandleMode,
  StatementObjects, NoOrder, "37.69", 1>;
def LateIfElseStmt : VPITraversalEdge<LateIfElseSource, ElseStmtRel,
  HandleMode, StatementObjects, NoOrder, "37.69", 1>;

// 37.70 Case and pattern.
def LateCaseCondition : VPITraversalEdge<LateCaseSource, ConditionRel,
  HandleMode, LateOrdinaryExpressionTargets, NoOrder, "37.70">;
def LateCaseItems : VPITraversalEdge<LateCaseSource, CaseItem, IterateMode,
  CaseItemObjects, NoOrder, "37.70", 1>;
def LateCaseItemExprs : VPITraversalEdge<LateCaseItemSource, ExprRel,
  IterateMode, LateCaseExprTargets, NoOrder, "37.70">;
def LateCaseItemStmt : VPITraversalEdge<LateCaseItemSource, StmtRel,
  HandleMode, StatementObjects, NoOrder, "37.70", 1>;
def LatePatternChild : VPITraversalEdge<LatePatternSource, PatternRel,
  HandleMode, PatternObjects, NoOrder, "37.70">;
// 37.71 Expect.
def LateExpectProperty : VPITraversalEdge<LateExpectSource, PropertySpec,
  HandleMode, PropertySpecObjects, NoOrder, "37.71">;
def LateExpectStmt : VPITraversalEdge<LateExpectSource, StmtRel, HandleMode,
  StatementObjects, NoOrder, "37.71", 1>;
def LateExpectElse : VPITraversalEdge<LateExpectSource, ElseStmtRel,
  HandleMode, StatementObjects, NoOrder, "37.71", 1>;

// 37.72 For.  SystemVerilog permits lists, while the legacy singular
// traversal remains legal; hence both modes are recorded.
def LateForInit : VPITraversalEdge<LateForSource, ForInitStmtRel, HandleMode,
  StatementObjects, NoOrder, "37.72", 1>;
def LateForInits : VPITraversalEdge<LateForSource, ForInitStmtRel, IterateMode,
  StatementObjects, NoOrder, "37.72", 1>;
def LateForCondition : VPITraversalEdge<LateForSource, ConditionRel,
  HandleMode, LateOrdinaryExpressionTargets, NoOrder, "37.72">;
def LateForInc : VPITraversalEdge<LateForSource, ForIncStmtRel, HandleMode,
  StatementObjects, NoOrder, "37.72", 1>;
def LateForIncs : VPITraversalEdge<LateForSource, ForIncStmtRel, IterateMode,
  StatementObjects, NoOrder, "37.72", 1>;
def LateForStmt : VPITraversalEdge<LateForSource, StmtRel, HandleMode,
  StatementObjects, NoOrder, "37.72", 1>;

// 37.73 Do-while and foreach.
def LateDoWhileCondition : VPITraversalEdge<LateDoWhileSource, ConditionRel,
  HandleMode, LateOrdinaryExpressionTargets, NoOrder, "37.73">;
def LateDoWhileStmt : VPITraversalEdge<LateDoWhileSource, StmtRel, HandleMode,
  StatementObjects, NoOrder, "37.73", 1>;
def LateForeachLoopVars : VPITraversalEdge<LateForeachSource, LoopVarsRel,
  IterateMode, LateForeachLoopTargets, SourceOrder, "37.73">;
def LateForeachVariables : VPITraversalEdge<LateForeachSource, VariablesRel,
  IterateMode, LateForeachIndexedTargets, NoOrder, "37.73">;
def LateForeachStmt : VPITraversalEdge<LateForeachSource, StmtRel, HandleMode,
  StatementObjects, NoOrder, "37.73", 1>;

// 37.74 Alias statement.
def LateAliasLhs : VPITraversalEdge<LateAliasSource, LhsRel, HandleMode,
  LateOrdinaryExpressionTargets, NoOrder, "37.74">;
def LateAliasRhs : VPITraversalEdge<LateAliasSource, RhsRel, HandleMode,
  LateOrdinaryExpressionTargets, NoOrder, "37.74">;
def LateAliasInstance : VPITraversalEdge<LateAliasSource, InstanceRel,
  HandleMode, InstanceObjects, NoOrder, "37.74", 0, AutomaticParentScope>;

// 37.75 Disables.
def LateDisableExpr : VPITraversalEdge<LateDisableSource, ExprRel, HandleMode,
  LateDisableTargets, NoOrder, "37.75">;

// 37.76 Return statement.
def LateReturnCondition : VPITraversalEdge<LateReturnSource, ConditionRel,
  HandleMode, LateOrdinaryExpressionTargets, NoOrder, "37.76">;

// 37.77 Procedural assign, force, deassign, and release.
def LateProceduralAssignLhs : VPITraversalEdge<
  LateProceduralAssignLhsSource, LhsRel, HandleMode,
  LateOrdinaryExpressionTargets, NoOrder, "37.77">;
def LateProceduralAssignRhs : VPITraversalEdge<
  LateProceduralAssignRhsSource, RhsRel, HandleMode,
  LateOrdinaryExpressionTargets, NoOrder, "37.77">;

// 37.78 Callback.  The NULL-root edge is in VPITraversalModel.td.
def LateOwnedCallbacks : VPITraversalEdge<LateCallbackOwnerSource, Callback,
  IterateMode, CallbackObjects, NoOrder, "37.78">;

// 37.79 and 37.80 contain only NULL-root edges, defined by the common model.

// 37.81 Attribute.
def LateAttributes : VPITraversalEdge<LateAttributeOwnerSource, Attribute,
  IterateMode, AttributeObjects, NoOrder, "37.81">;
def LateAttributeParent : VPITraversalEdge<AttributeObjects, ParentRel,
  HandleMode, LateAttributeOwnerSource, NoOrder, "37.81">;

// 37.82 Iterator.
def LateIteratorUse : VPITraversalEdge<LateIteratorSource, UseRel, HandleMode,
  LateIteratorUseTargets, NoOrder, "37.82">;

// 37.83 Generate scopes.
def LateGeneratedInstance : VPITraversalEdge<LateGeneratedSource, InstanceRel,
  HandleMode, ModuleTarget, NoOrder, "37.83", 0, AutomaticParentScope>;
def LateGenScopeArrayElements : VPITraversalEdge<LateGenScopeArraySource,
  GenScope, IterateMode, GenScopeObjects, IndexOrder, "37.83">;
def LateGenScopeIndex : VPITraversalEdge<LateGenScopeSource, IndexRel,
  HandleMode, LateOrdinaryExpressionTargets, NoOrder, "37.83">;
def LateGenScopeNets : VPITraversalEdge<LateGenScopeSource, Net, IterateMode,
  NetObjects, NoOrder, "37.83">;
def LateGenScopeNetArrays : VPITraversalEdge<LateGenScopeSource, NetArray,
  IterateMode, NetArrayObjects, NoOrder, "37.83">;
def LateGenScopeProcesses : VPITraversalEdge<LateGenScopeSource, ProcessRel,
  IterateMode, StaticProcessObjects, NoOrder, "37.83">;
def LateGenScopeContAssigns : VPITraversalEdge<LateGenScopeSource, ContAssign,
  IterateMode, ContAssignObjects, NoOrder, "37.83", 1>;
def LateGenScopeModules : VPITraversalEdge<LateGenScopeSource, Module,
  IterateMode, ModuleTarget, NoOrder, "37.83">;
def LateGenScopeModuleArrays : VPITraversalEdge<LateGenScopeSource,
  ModuleArray, IterateMode, ModuleArrayObjects, NoOrder, "37.83">;
def LateGenScopePrimitives : VPITraversalEdge<LateGenScopeSource,
  PrimitiveRel, IterateMode, PrimitiveConcreteObjects, NoOrder, "37.83", 0,
  AutomaticDirectChild>;
def LateGenScopePrimitiveArrays : VPITraversalEdge<LateGenScopeSource,
  PrimitiveArray, IterateMode, PrimitiveArrayObjects, NoOrder, "37.83", 0,
  AutomaticDirectChild>;
def LateGenScopeDefParams : VPITraversalEdge<LateGenScopeSource, DefParam,
  IterateMode, DefParamObjects, NoOrder, "37.83">;
def LateGenScopeArrays : VPITraversalEdge<LateGenScopeSource, GenScopeArray,
  IterateMode, GenScopeArrayObjects, NoOrder, "37.83">;
def LateGenScopePrograms : VPITraversalEdge<LateGenScopeSource, Program,
  IterateMode, ProgramObjects, NoOrder, "37.83">;
def LateGenScopeProgramArrays : VPITraversalEdge<LateGenScopeSource,
  ProgramArray, IterateMode, ProgramArrayObjects, NoOrder, "37.83">;
def LateGenScopeAssertions : VPITraversalEdge<LateGenScopeSource,
  AssertionRel, IterateMode, AssertionTargetObjects, NoOrder, "37.83">;
def LateGenScopeInterfaces : VPITraversalEdge<LateGenScopeSource, Interface,
  IterateMode, InterfaceObjects, NoOrder, "37.83">;
def LateGenScopeInterfaceArrays : VPITraversalEdge<LateGenScopeSource,
  InterfaceArray, IterateMode, InterfaceArrayObjects, NoOrder, "37.83">;
def LateGenScopeAliases : VPITraversalEdge<LateGenScopeSource, AliasStmt,
  IterateMode, AliasStmtObjects, NoOrder, "37.83", 1>;
def LateGenScopeClockingBlocks : VPITraversalEdge<LateGenScopeSource,
  ClockingBlock, IterateMode, ClockingBlockObjects, NoOrder, "37.83">;
