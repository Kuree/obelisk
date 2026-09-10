//===- VPITraversalEarly.td - IEEE 1800-2023 37.5-37.50 edges -----------===//
//
// Standalone continuation of VPITraversalModel.td.  This file is intended to
// be included after that file's common VPIObjectSet definitions.  It contains
// only tuples not already present in VPITraversalModel.td.
//
//===----------------------------------------------------------------------===//

// Concrete diagram classes used by more than one early-clause edge.  Aliased
// object spellings (vpiLogicVar, vpiArrayVar, vpiArrayNet, and vpiVarBit) are
// represented by their canonical object kinds, while the spelling named by a
// diagram remains the selector on an edge.
def VPIEarlyOrdinaryExpressions : VPIObjectSet<"EarlyOrdinaryExpressions",
  [RefObj, VarSelect, BitSelect, IndexedPartSelect, PartSelect, Operation,
   Constant, FuncCall, MethodFuncCall, SysFuncCall, LetExpr],
  [NetFamily, VariableFamily],
  [NamedEvent, NamedEventArray, VirtualInterfaceVar]>;
def VPIEarlyVariables : VPIObjectSet<"EarlyVariables",
  [ShortRealVar, RealVar, ByteVar, ShortIntVar, IntVar, LongIntVar,
   IntegerVar, TimeVar, RegArray, PackedArrayVar, BitVar, Reg, StructVar,
   UnionVar, EnumVar, StringVar, ChandleVar, ClassVar, VirtualInterfaceVar,
   RegBit]>;
def VPIEarlyNonVirtualVariables : VPIObjectSet<"EarlyNonVirtualVariables",
  [ShortRealVar, RealVar, ByteVar, ShortIntVar, IntVar, LongIntVar,
   IntegerVar, TimeVar, RegArray, PackedArrayVar, BitVar, Reg, StructVar,
   UnionVar, EnumVar, StringVar, ChandleVar, ClassVar, RegBit]>;
def VPIEarlyNets : VPIObjectSet<"EarlyNets",
  [Net, NetBit, NetArray, EnumNet, IntegerNet, TimeNet, UnionNet,
   ShortRealNet, RealNet, ByteNet, ShortIntNet, IntNet, LongIntNet, BitNet,
   InterconnectNet, InterconnectArray, StructNet, PackedArrayNet]>;
def VPIEarlyBitNetSources : VPIObjectSet<"EarlyBitNetSources", [],
  [NetFamily], [NetBit, NetArray, InterconnectArray]>;
def VPIEarlyScopes : VPIObjectSet<"EarlyScopes",
  [Begin, For, Fork, Function, Module, NamedBegin, NamedFork, Task, Package,
   Interface, Program, ClassTypespec, ClockingBlock, ClassDefn, ForeachStmt,
   GenScope, ClassObj]>;
def VPIEarlyTypespecs : VPIObjectSet<"EarlyTypespecs",
  [LongIntTypespec, ShortRealTypespec, ByteTypespec, ShortIntTypespec,
   IntTypespec, ClassTypespec, StringTypespec, ChandleTypespec, EnumTypespec,
   IntegerTypespec, TimeTypespec, RealTypespec, StructTypespec,
   UnionTypespec, BitTypespec, LogicTypespec, ArrayTypespec, VoidTypespec,
   PackedArrayTypespec, SequenceTypespec, PropertyTypespec, EventTypespec,
   InterfaceTypespec, TypeParameter]>;
// A net declared with a user-defined nettype returns that declaration from
// vpiTypespec. Nettype declarations are not themselves members of the
// generic 37.25 typespec family.
def VPIEarlyNetTypespecTargets : VPIObjectSet<"EarlyNetTypespecTargets",
  [NettypeDecl], [TypespecFamily], [TypespecMember]>;
def VPIEarlyNettypeDecls : VPIObjectSet<"EarlyNettypeDecls", [NettypeDecl]>;
def VPIEarlyInterconnectArrays : VPIObjectSet<"EarlyInterconnectArrays",
  [InterconnectArray]>;
def VPIEarlyInterconnectNets : VPIObjectSet<"EarlyInterconnectNets",
  [InterconnectNet]>;
def VPIEarlyInterconnectArrayElementTargets : VPIObjectSet<
  "EarlyInterconnectArrayElements", [InterconnectArray, InterconnectNet]>;
def VPIEarlyAssertions : VPIObjectSet<"EarlyAssertions",
  [SequenceInst, Assert, Assume, Cover, Restrict, PropertyInst,
   ImmediateAssert, ImmediateAssume, ImmediateCover]>;
def VPIEarlyConcurrentAssertions : VPIObjectSet<"EarlyConcurrentAssertions",
  [Assert, Assume, Cover, Restrict]>;
def VPIEarlyNonPrimitiveInstanceArrays : VPIObjectSet<
  "EarlyNonPrimitiveInstanceArrays", [ModuleArray, InterfaceArray,
   ProgramArray]>;
def VPIEarlyInstances : VPIObjectSet<"EarlyInstances",
  [Package, Module, Interface, Program]>;
def VPIEarlyPorts : VPIObjectSet<"EarlyPorts", [Port, PortBit]>;
def VPIEarlyTaskFuncs : VPIObjectSet<"EarlyTaskFuncs", [Task, Function]>;
def VPIEarlyTfCalls : VPIObjectSet<"EarlyTfCalls",
  [TaskCall, FuncCall, MethodTaskCall, MethodFuncCall, SysTaskCall,
   SysFuncCall]>;
def VPIEarlyPrimitiveArrays : VPIObjectSet<"EarlyPrimitiveArrays",
  [GateArray, SwitchArray, UdpArray]>;
def VPIEarlyPrimitives : VPIObjectSet<"EarlyPrimitives",
  [Gate, Switch, Udp]>;
def VPIEarlyGateObjects : VPIObjectSet<"EarlyGateObjects", [Gate]>;
def VPIEarlySwitchObjects : VPIObjectSet<"EarlySwitchObjects", [Switch]>;
def VPIEarlyUdpObjects : VPIObjectSet<"EarlyUdpObjects", [Udp]>;
def VPIEarlyGateArrayObjects : VPIObjectSet<"EarlyGateArrayObjects",
  [GateArray]>;
def VPIEarlySwitchArrayObjects : VPIObjectSet<"EarlySwitchArrayObjects",
  [SwitchArray]>;
def VPIEarlyUdpArrayObjects : VPIObjectSet<"EarlyUdpArrayObjects",
  [UdpArray]>;
def VPIEarlyOperations : VPIObjectSet<"EarlyOperations", [Operation]>;
def VPIEarlyParameters : VPIObjectSet<"EarlyParameters",
  [Parameter, TypeParameter]>;
def VPIEarlyDataParameters : VPIObjectSet<"EarlyDataParameters",
  [Parameter, SpecParam]>;
def VPIEarlyInterfaceExprs : VPIObjectSet<"EarlyInterfaceExprs",
  [Interface, Modport, VirtualInterfaceVar, RefObj, Constant]>;
def VPIEarlyRefActuals : VPIObjectSet<"EarlyRefActuals",
  [Interface, InterfaceArray, Modport, PartSelect],
  [NetFamily, VariableFamily], [Parameter, SpecParam]>;
def VPIEarlyVarParents : VPIObjectSet<"EarlyVarParents",
  [RegArray, PackedArrayVar, StructVar, UnionVar, ClassVar]>;
def VPIEarlyPackedVarElements : VPIObjectSet<"EarlyPackedVarElements",
  [EnumVar, StructVar, UnionVar, PackedArrayVar]>;
def VPIEarlyPackedNetElements : VPIObjectSet<"EarlyPackedNetElements",
  [EnumNet, StructNet, PackedArrayNet]>;
def VPIEarlyTypespecMembers : VPIObjectSet<"EarlyTypespecMembers",
  [TypespecMember]>;
def VPIEarlyEnumConsts : VPIObjectSet<"EarlyEnumConsts", [EnumConst]>;
def VPIEarlyConstraintItems : VPIObjectSet<"EarlyConstraintItems",
  [ConstraintOrdering, Implication, ConstrIf, ConstrIfElse, ConstrForEach,
   Distribution, SoftDisable, RefObj, VarSelect, BitSelect, IndexedPartSelect,
   PartSelect, Operation, Constant, FuncCall, MethodFuncCall, SysFuncCall,
   LetExpr], [NetFamily, VariableFamily],
  [Parameter, SpecParam, NamedEvent, NamedEventArray, VirtualInterfaceVar]>;
def VPIEarlyConstraintExpressions : VPIObjectSet<"EarlyConstraintExpressions",
  [Implication, ConstrIf, ConstrIfElse, ConstrForEach, Distribution,
   SoftDisable, RefObj, VarSelect, BitSelect, IndexedPartSelect, PartSelect,
   Operation, Constant, FuncCall, MethodFuncCall, SysFuncCall, LetExpr],
  [NetFamily, VariableFamily],
  [Parameter, SpecParam, NamedEvent, NamedEventArray, VirtualInterfaceVar]>;
def VPIEarlyLoopVars : VPIObjectSet<"EarlyLoopVars",
  [IntegerVar, Reg, RegBit, TimeVar, LongIntVar, ShortIntVar, IntVar,
   ByteVar, ClassVar, StringVar, EnumVar, StructVar, UnionVar, BitVar,
   ChandleVar, RegArray, PackedArrayVar, Operation]>;
def VPIEarlyNetDriverTargets : VPIObjectSet<"EarlyNetDriverTargets",
  [Port, Force, DelayTerm, ContAssign, ContAssignBit, PrimTerm]>;
def VPIEarlyLoads : VPIObjectSet<"EarlyLoads",
  [DelayTerm, AssignStmt, Force, ContAssign, ContAssignBit, PrimTerm, Port]>;
def VPIEarlyVariableDrivers : VPIObjectSet<"EarlyVariableDrivers",
  [Port, Force, ContAssign, ContAssignBit, AssignStmt]>;
def VPIEarlyVariableLoads : VPIObjectSet<"EarlyVariableLoads",
  [AssignStmt, Force, ContAssign, ContAssignBit]>;
// IEEE 1800-2023 37.43 deliberately replaces the pre-2023 frame graph.
// "nets" here denotes whole net and net-array objects; a selected net bit is
// not an elaboration-hierarchy origin for a resolution-function activation.
def VPIEarlyFramesOrigins : VPIObjectSet<"EarlyFrameOrigins",
  [TaskCall, FuncCall, MethodTaskCall, MethodFuncCall],
  [ScopeFamily, NetFamily], [NetBit]>;
def VPIEarlyFrameParents : VPIObjectSet<"EarlyFrameParents", [Frame]>;
def VPIEarlyAutomatics : VPIObjectSet<"EarlyAutomatics",
  [], [VariableFamily], [Parameter, SpecParam]>;
def VPIEarlyPropertyTargets : VPIObjectSet<"EarlyPropertyTargets",
  [PropertyInst, PropertySpec]>;
def VPIEarlyVirtualInterfaceExprs : VPIObjectSet<
  "EarlyVirtualInterfaceExprs",
  [Interface, Modport, VirtualInterfaceVar, RefObj, Constant]>;
def VPIEarlyIOExprs : VPIObjectSet<"EarlyIOExprs",
  [RefObj, InterfaceTfDecl], [NetFamily, VariableFamily],
  [Parameter, SpecParam, NamedEvent, NamedEventArray]>;
def VPIEarlyCallArguments : VPIObjectSet<"EarlyCallArguments",
  [Modport, ClassObj, Gate, Switch, Udp, RefObj, VarSelect, BitSelect,
   IndexedPartSelect, PartSelect, Operation, Constant, FuncCall,
   MethodFuncCall, SysFuncCall, LetExpr],
  [NetFamily, VariableFamily, ScopeFamily]>;
def VPIEarlyParamAssignRhsTargets : VPIObjectSet<
  "EarlyParamAssignRhsTargets",
  [RefObj, VarSelect, BitSelect, IndexedPartSelect, PartSelect, Operation,
   Constant, FuncCall, MethodFuncCall, SysFuncCall, LetExpr],
  [NetFamily, VariableFamily, TypespecFamily],
  [NamedEvent, NamedEventArray, VirtualInterfaceVar]>;
def VPIEarlyDistributionValueTargets : VPIObjectSet<
  "EarlyDistributionValueTargets",
  [Range, RefObj, VarSelect, BitSelect, IndexedPartSelect, PartSelect,
   Operation, Constant, FuncCall, MethodFuncCall, SysFuncCall, LetExpr],
  [NetFamily, VariableFamily],
  [NamedEvent, NamedEventArray, VirtualInterfaceVar]>;
def VPIEarlyTchkExprTargets : VPIObjectSet<"EarlyTchkExprTargets",
  [TchkTerm, RefObj, VarSelect, BitSelect, IndexedPartSelect, PartSelect,
   Operation, Constant, FuncCall, MethodFuncCall, SysFuncCall, LetExpr],
  [NetFamily, VariableFamily],
  [NamedEvent, NamedEventArray, VirtualInterfaceVar]>;
def VPIEarlyMethodWithTargets : VPIObjectSet<"EarlyMethodWithTargets",
  [Constraint, RefObj, VarSelect, BitSelect, IndexedPartSelect, PartSelect,
   Operation, Constant, FuncCall, MethodFuncCall, SysFuncCall, LetExpr],
  [NetFamily, VariableFamily],
  [NamedEvent, NamedEventArray, VirtualInterfaceVar]>;
def VPIEarlyClassContents : VPIObjectSet<"EarlyClassContents",
  [Constraint],
  [VariableFamily, ScopeFamily],
  [Parameter, SpecParam, NamedEvent, NamedEventArray, VirtualInterfaceVar,
   Package, Module, Interface, Program, ClassDefn, ClassTypespec]>;
def VPIEarlyModuleChildren : VPIObjectSet<"EarlyModuleChildren",
  [Port, InterfaceArray, ContAssign, ModuleArray, Gate, Switch, Udp, GateArray,
   SwitchArray, UdpArray, ModPath, Tchk, DefParam, IODecl, AliasStmt],
  [ScopeFamily],
  [Package, Program]>;
def VPIEarlyInterfaceChildren : VPIObjectSet<"EarlyInterfaceChildren",
  [InterfaceTfDecl, Modport, ContAssign, ClockingBlock, Interface,
   InterfaceArray, Program, Initial, Final, Always]>;
def VPIEarlyModPathInstances : VPIObjectSet<"EarlyModPathInstances",
  [Module, Interface]>;
def VPIEarlyProgramChildren : VPIObjectSet<"EarlyProgramChildren",
  [ContAssign, ClockingBlock, Interface, InterfaceArray, Initial, Final,
   Always]>;
def VPIEarlyScopeChildren : VPIObjectSet<"EarlyScopeChildren",
  [PropertyDecl, SequenceDecl, Assert, Assume, Cover, Restrict, LetDecl,
   ParamAssign, TypeParameter],
  [VariableFamily, ScopeFamily],
  [SpecParam, NamedEventArray, Package, Module, Interface, Program, Begin,
   For, Fork, NamedBegin, NamedFork, ForeachStmt]>;
def VPIEarlyBlockScopes : VPIObjectSet<"EarlyBlockScopes",
  [Begin, NamedBegin, Fork, NamedFork]>;
def VPIEarlyPortLowConnTargets : VPIObjectSet<"EarlyPortLowConnTargets",
  [RefObj, VarSelect, BitSelect, IndexedPartSelect, PartSelect, Operation,
   Constant, FuncCall, MethodFuncCall, SysFuncCall, LetExpr],
  [NetFamily, VariableFamily],
  [Parameter, SpecParam, NamedEvent, NamedEventArray, VirtualInterfaceVar]>;
def VPIEarlySysCalls : VPIObjectSet<"EarlySysCalls",
  [SysFuncCall, SysTaskCall]>;

// 37.5 Module.  The pre-existing model supplies the right-hand iterations,
// vpiInstanceArray handle, and NULL-root vpiModule iteration.
def VPIEarlyModuleIndex : VPITraversalEdge<ModuleSource, IndexRel, HandleMode,
  VPIEarlyOrdinaryExpressions, NoOrder, "37.5">;
def VPIEarlyModuleGlobalClocking : VPITraversalEdge<ModuleSource,
  GlobalClockingRel, HandleMode, ClockingBlockObjects, NoOrder, "37.5">;
def VPIEarlyModuleDefaultClocking : VPITraversalEdge<ModuleSource,
  DefaultClockingRel, HandleMode, ClockingBlockObjects, NoOrder, "37.5">;
def VPIEarlyModuleDefaultDisable : VPITraversalEdge<ModuleSource,
  DefaultDisableIffRel, HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.5">;
def VPIEarlyModuleContainingArray : VPITraversalEdge<ModuleSource,
  ModuleArray, HandleMode, ModuleArrayObjects, NoOrder, "37.5; 37.11", 0,
  AutomaticIndexedContainer>;
def VPIEarlyModuleArrayElements : VPITraversalEdge<ModuleArrayObjects, Module,
  IterateMode, ModuleTarget, IndexOrder, "37.5; 37.11">;
def VPIEarlyModuleOfChild : VPITraversalEdge<VPIEarlyModuleChildren, Module,
  HandleMode, ModuleTarget, NoOrder, "37.5", 0, AutomaticParentScope>;

// 37.6 Interface.
def VPIEarlyInterfaceIndex : VPITraversalEdge<InterfaceObjects, IndexRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.6">;
def VPIEarlyInterfaceGlobalClocking : VPITraversalEdge<InterfaceObjects,
  GlobalClockingRel, HandleMode, ClockingBlockObjects, NoOrder, "37.6">;
def VPIEarlyInterfaceDefaultClocking : VPITraversalEdge<InterfaceObjects,
  DefaultClockingRel, HandleMode, ClockingBlockObjects, NoOrder, "37.6">;
def VPIEarlyInterfaceDefaultDisable : VPITraversalEdge<InterfaceObjects,
  DefaultDisableIffRel, HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.6">;
def VPIEarlyArrayInterfaces : VPITraversalEdge<InterfaceArrayObjects,
  Interface, IterateMode, InterfaceObjects, IndexOrder, "37.6">;
def VPIEarlyInstanceOfInterfaceChild : VPITraversalEdge<
  VPIEarlyInterfaceChildren, InstanceRel, HandleMode, VPIEarlyInstances,
  NoOrder, "37.6; 37.9; 37.39; 37.48", 0, AutomaticParentScope>;
def VPIEarlyModPathInstance : VPITraversalEdge<ModPathObjects, InstanceRel,
  HandleMode, VPIEarlyModPathInstances, NoOrder, "37.39">;

// 37.7 Modport and 37.8 interface task/function declaration.
def VPIEarlyModportInterface : VPITraversalEdge<ModportObjects, Interface,
  HandleMode, InterfaceObjects, NoOrder, "37.7">;
def VPIEarlyModportIODecls : VPITraversalEdge<ModportObjects, IODecl,
  IterateMode, IODeclObjects, NoOrder, "37.7">;
def VPIEarlyIODeclModport : VPITraversalEdge<IODeclObjects, Modport,
  HandleMode, ModportObjects, NoOrder, "37.7">;
def VPIEarlyInterfaceTfTasks : VPITraversalEdge<InterfaceTfDeclObjects, Task,
  IterateMode, VPIObjectSet<"EarlyInterfaceTfTasksTarget", [Task]>,
  NoOrder, "37.8">;
def VPIEarlyInterfaceTfFunctions : VPITraversalEdge<InterfaceTfDeclObjects,
  Function, IterateMode,
  VPIObjectSet<"EarlyInterfaceTfFunctionsTarget", [Function]>,
  NoOrder, "37.8">;

// 37.9 Program and 37.10 Instance.
def VPIEarlyProgramIndex : VPITraversalEdge<ProgramObjects, IndexRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.9">;
def VPIEarlyProgramDefaultClocking : VPITraversalEdge<ProgramObjects,
  DefaultClockingRel, HandleMode, ClockingBlockObjects, NoOrder, "37.9">;
def VPIEarlyProgramDefaultDisable : VPITraversalEdge<ProgramObjects,
  DefaultDisableIffRel, HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.9">;
def VPIEarlyArrayPrograms : VPITraversalEdge<ProgramArrayObjects, Program,
  IterateMode, ProgramObjects, IndexOrder, "37.9">;
def VPIEarlyRootInstances : VPITraversalEdge<RootSource, InstanceRel,
  IterateMode, VPIEarlyInstances, NoOrder, "37.10">;
def VPIEarlyInstancePrograms : VPITraversalEdge<InstanceObjects, Program,
  IterateMode, ProgramObjects, NoOrder, "37.10">;
def VPIEarlyInstanceProgramArrays : VPITraversalEdge<InstanceObjects,
  ProgramArray, IterateMode, ProgramArrayObjects, NoOrder, "37.10">;
def VPIEarlyInstanceNettypeDecls : VPITraversalEdge<InstanceObjects,
  NetTypedefRel, IterateMode, VPIEarlyNettypeDecls, NoOrder, "37.10">;

// 37.11 Instance arrays.
def VPIEarlyInstanceArrayExpr : VPITraversalEdge<InstanceArrayObjects,
  ExprRel, HandleMode, VPIEarlyOperations, NoOrder, "37.11">;
def VPIEarlyInstanceArrayRanges : VPITraversalEdge<InstanceArrayObjects,
  Range, IterateMode, RangeObjects, IndexOrder, "37.11">;
def VPIEarlyInstanceArrayLeftRange : VPITraversalEdge<InstanceArrayObjects,
  LeftRangeRel, HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.11">;
def VPIEarlyInstanceArrayRightRange : VPITraversalEdge<InstanceArrayObjects,
  RightRangeRel, HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.11">;
def VPIEarlyModuleArrayInstances : VPITraversalEdge<ModuleArrayObjects,
  InstanceRel, IterateMode, ModuleTarget, IndexOrder, "37.11">;
def VPIEarlyInterfaceArrayInstances : VPITraversalEdge<InterfaceArrayObjects,
  InstanceRel, IterateMode, InterfaceObjects, IndexOrder, "37.11">;
def VPIEarlyProgramArrayInstances : VPITraversalEdge<ProgramArrayObjects,
  InstanceRel, IterateMode, ProgramObjects, IndexOrder, "37.11">;
def VPIEarlyInstanceArrayParamAssigns : VPITraversalEdge<
  VPIEarlyNonPrimitiveInstanceArrays, ParamAssign, IterateMode,
  VPIObjectSet<"EarlyInstanceArrayParamAssignTargets", [ParamAssign]>,
  NoOrder, "37.11">;
def VPIEarlyGateContainingArray : VPITraversalEdge<VPIEarlyGateObjects,
  PrimitiveArray, HandleMode, VPIEarlyGateArrayObjects, NoOrder,
  "37.11; 37.35", 0, AutomaticIndexedContainer>;
def VPIEarlySwitchContainingArray : VPITraversalEdge<VPIEarlySwitchObjects,
  PrimitiveArray, HandleMode, VPIEarlySwitchArrayObjects, NoOrder,
  "37.11; 37.35", 0, AutomaticIndexedContainer>;
def VPIEarlyUdpContainingArray : VPITraversalEdge<VPIEarlyUdpObjects,
  PrimitiveArray, HandleMode, VPIEarlyUdpArrayObjects, NoOrder,
  "37.11; 37.35", 0, AutomaticIndexedContainer>;
def VPIEarlyGateArrayElements : VPITraversalEdge<VPIEarlyGateArrayObjects,
  PrimitiveRel, IterateMode, VPIEarlyGateObjects, IndexOrder,
  "37.11; 37.35">;
def VPIEarlySwitchArrayElements : VPITraversalEdge<
  VPIEarlySwitchArrayObjects, PrimitiveRel, IterateMode, VPIEarlySwitchObjects,
  IndexOrder, "37.11; 37.35">;
def VPIEarlyUdpArrayElements : VPITraversalEdge<VPIEarlyUdpArrayObjects,
  PrimitiveRel, IterateMode, VPIEarlyUdpObjects, IndexOrder,
  "37.11; 37.35">;
def VPIEarlyPrimitiveArrayDelay : VPITraversalEdge<PrimitiveArrayObjects,
  DelayRel, HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.11">;

// 37.12 Scope.  Existing right-hand declaration iterations are not repeated.
def VPIEarlyScopeStatement : VPITraversalEdge<TaskFunctionDeclObjects,
  StmtRel, HandleMode, StatementObjects, NoOrder, "37.12", 1>;
def VPIEarlyBlockStatements : VPITraversalEdge<VPIEarlyBlockScopes, StmtRel,
  IterateMode, StatementObjects, NoOrder, "37.12", 1>;
def VPIEarlyScopeRegs : VPITraversalEdge<NonInstanceScopeObjects, Reg,
  IterateMode, RegObjects, NoOrder, "37.12; 37.85", 0,
  AutomaticDirectChild>;
def VPIEarlyScopeRegArrays : VPITraversalEdge<NonInstanceScopeObjects,
  RegArray, IterateMode, RegArrayObjects, NoOrder, "37.12">;
def VPIEarlyScopeMemories : VPITraversalEdge<NonInstanceScopeObjects, Memory,
  IterateMode, RegArrayObjects, NoOrder, "37.12; 37.20; 37.85">;
def VPIEarlyScopeOfChild : VPITraversalEdge<StatementObjects, ScopeRel,
  HandleMode, ScopeObjects, NoOrder, "37.12", 0, AutomaticParentScope>;
def VPIEarlyScopeOfDeclaredChild : VPITraversalEdge<VPIEarlyScopeChildren,
  ScopeRel, HandleMode, ScopeObjects, NoOrder, "37.12", 0,
  AutomaticParentScope>;

// 37.13 IO declaration.
def VPIEarlyContainersIODecls : VPITraversalEdge<
  VPIObjectSet<"EarlyIODeclContainers", [Package, Interface, Program, UdpDefn,
    Task, Function]>, IODecl, IterateMode, IODeclObjects, NoOrder,
  "37.13">;
def VPIEarlyIODeclInstance : VPITraversalEdge<IODeclObjects, InstanceRel,
  HandleMode, VPIEarlyInstances, NoOrder, "37.13", 0,
  AutomaticDefinitionMemberParent>;
def VPIEarlyIODeclUdpDefn : VPITraversalEdge<IODeclObjects, UdpDefn,
  HandleMode, VPIObjectSet<"EarlyUdpDefnTarget", [UdpDefn]>, NoOrder,
  "37.13; 37.36">;
def VPIEarlyIODeclExpr : VPITraversalEdge<IODeclObjects, ExprRel, HandleMode,
  VPIEarlyIOExprs, NoOrder, "37.13", 0, AutomaticDefinitionMemberExpr>;
def VPIEarlyIODeclLeftRange : VPITraversalEdge<IODeclObjects, LeftRangeRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.13">;
def VPIEarlyIODeclRightRange : VPITraversalEdge<IODeclObjects, RightRangeRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.13">;
def VPIEarlyIODeclRanges : VPITraversalEdge<IODeclObjects, Range, IterateMode,
  RangeObjects, IndexOrder, "37.13">;
def VPIEarlyIODeclTypespec : VPITraversalEdge<IODeclObjects, Typespec,
  HandleMode, VPIEarlyTypespecs, NoOrder, "37.13">;
def VPIEarlyIODeclTaskFunc : VPITraversalEdge<IODeclObjects, TaskFuncRel,
  HandleMode, TaskFunctionDeclObjects, NoOrder, "37.13">;

// 37.14 Ports and 37.15 reference objects.
def VPIEarlyInstancePorts : VPITraversalEdge<
  VPIObjectSet<"EarlyNonModulePortInstances", [Interface, Program]>, Port,
  IterateMode, PortTargetObjects, NoOrder, "37.14", 0,
  AutomaticDirectChild>;
def VPIEarlyPortInstance : VPITraversalEdge<PortTargetObjects, InstanceRel,
  HandleMode, VPIEarlyInstances, NoOrder, "37.14", 0,
  AutomaticParentScope>;
def VPIEarlyPortBits : VPITraversalEdge<
  VPIObjectSet<"EarlyPortBitIterationSource", [Port]>, BitRel, IterateMode,
  VPIObjectSet<"EarlyPortBitTargets", [PortBit]>, IndexOrder, "37.14">;
def VPIEarlyPortBitParent : VPITraversalEdge<
  VPIObjectSet<"EarlyPortBitSources", [PortBit]>, ParentRel, HandleMode,
  VPIObjectSet<"EarlyPortTargets", [Port]>, NoOrder, "37.14">;
def VPIEarlyPortHighConn : VPITraversalEdge<PortTargetObjects, HighConnRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.14">;
def VPIEarlyPortLowConn : VPITraversalEdge<PortTargetObjects, LowConnRel,
  HandleMode, VPIEarlyPortLowConnTargets, NoOrder,
  "37.14; 37.15; 37.16; 37.17", 0, AutomaticDirectPortConnection>;
def VPIEarlyPortTypespec : VPITraversalEdge<PortObjects, Typespec, HandleMode,
  VPIEarlyTypespecs, NoOrder, "37.14">;
def VPIEarlyRefObjPortInsts : VPITraversalEdge<RefObjectObjects, PortInstRel,
  IterateMode, VPIEarlyPorts, NoOrder, "37.15">;
def VPIEarlyRefObjInstance : VPITraversalEdge<RefObjectObjects, InstanceRel,
  HandleMode, VPIEarlyInstances, NoOrder, "37.15", 0,
  AutomaticDefinitionMemberParent>;
def VPIEarlyRefObjParent : VPITraversalEdge<RefObjectObjects, ParentRel,
  HandleMode, RefObjectObjects, NoOrder, "37.15">;
def VPIEarlyRefObjTypespec : VPITraversalEdge<RefObjectObjects, Typespec,
  HandleMode, VPIEarlyTypespecs, NoOrder, "37.15">;
def VPIEarlyRefObjActual : VPITraversalEdge<RefObjectObjects, ActualRel,
  HandleMode, VPIEarlyRefActuals, NoOrder, "37.15", 0,
  AutomaticDefinitionMemberExpr>;
def VPIEarlyRefObjTaskFunc : VPITraversalEdge<RefObjectObjects, TaskFuncRel,
  HandleMode, TaskFunctionDeclObjects, NoOrder, "37.15">;

// 37.16 Nets.
def VPIEarlyNetPortInsts : VPITraversalEdge<NetObjects, PortInstRel,
  IterateMode, VPIEarlyPorts, NoOrder, "37.16">;
def VPIEarlyNetBits : VPITraversalEdge<VPIEarlyBitNetSources, BitRel,
  IterateMode,
  VPIObjectSet<"EarlyNetBitTargets", [NetBit]>, IndexOrder, "37.16">;
def VPIEarlyNetBitIndex : VPITraversalEdge<
  VPIObjectSet<"EarlyNetBitSources", [NetBit]>, IndexRel, HandleMode,
  VPIEarlyOrdinaryExpressions, NoOrder, "37.16">;
def VPIEarlyNetIndices : VPITraversalEdge<VPIEarlyNets, IndexRel, IterateMode,
  VPIEarlyOrdinaryExpressions, IndexOrder, "37.16">;
def VPIEarlyArrayNetModule : VPITraversalEdge<NetArrayObjects, Module,
  HandleMode, ModuleTarget, NoOrder, "37.16">;
def VPIEarlyArrayNetRanges : VPITraversalEdge<NetArrayObjects, Range,
  IterateMode, RangeObjects, IndexOrder, "37.16; 37.24">;
def VPIEarlyOrdinaryNetParentSources : VPIObjectSet<
  "EarlyOrdinaryNetParentSources", [], [NetFamily],
  [InterconnectArray, InterconnectNet]>;
def VPIEarlyNetParent : VPITraversalEdge<VPIEarlyOrdinaryNetParentSources,
  ParentRel, HandleMode, VPIEarlyNets, NoOrder, "37.16">;
def VPIEarlyInterconnectParent : VPITraversalEdge<
  VPIEarlyInterconnectArrayElementTargets, ParentRel, HandleMode,
  VPIEarlyNets, NoOrder, "37.16", 0,
  AutomaticIndexedContainer>;
def VPIEarlyNetDrivers : VPITraversalEdge<NetObjects, DriverRel, IterateMode,
  VPIEarlyNetDriverTargets, NoOrder, "37.16; 37.46">;
def VPIEarlyNetLoads : VPITraversalEdge<NetObjects, LoadRel, IterateMode,
  VPIEarlyLoads, NoOrder, "37.16; 37.46">;
def VPIEarlyNetLocalDrivers : VPITraversalEdge<NetObjects, LocalDriverRel,
  IterateMode, VPIEarlyNetDriverTargets, NoOrder, "37.16">;
def VPIEarlyNetLocalLoads : VPITraversalEdge<NetObjects, LocalLoadRel,
  IterateMode, VPIEarlyLoads, NoOrder, "37.16">;
def VPIEarlyNetPrimTerms : VPITraversalEdge<NetObjects, PrimTerm, IterateMode,
  PrimTermObjects, NoOrder, "37.16">;
def VPIEarlyNetContAssigns : VPITraversalEdge<NetObjects, ContAssign,
  IterateMode, ContAssignObjects, NoOrder, "37.16">;
def VPIEarlyNetPathTerms : VPITraversalEdge<NetObjects, PathTerm, IterateMode,
  PathTermObjects, NoOrder, "37.16">;
def VPIEarlyNetTchkTerms : VPITraversalEdge<NetObjects, TchkTerm, IterateMode,
  TchkTermObjects, NoOrder, "37.16">;
def VPIEarlyNetSimNet : VPITraversalEdge<NetObjects, SimNetRel, HandleMode,
  VPIEarlyNets, NoOrder, "37.16">;
def VPIEarlyNetTypespec : VPITraversalEdge<
  VPIObjectSet<"EarlyNetTypespecSources", [], [NetFamily],
    [InterconnectArray]>,
  Typespec, HandleMode, VPIEarlyNetTypespecTargets, NoOrder, "37.16; 37.24">;
def VPIEarlyVectorNetRanges : VPITraversalEdge<
  VPIObjectSet<"EarlyVectorNetSources", [Net, StructNet, EnumNet, IntegerNet,
    TimeNet, PackedArrayNet]>, Range, IterateMode, RangeObjects, IndexOrder,
  "37.16">;
def VPIEarlyVectorNetLeftRange : VPITraversalEdge<
  VPIObjectSet<"EarlyVectorNetLeftSources", [Net, StructNet, EnumNet,
    IntegerNet, TimeNet, PackedArrayNet]>, LeftRangeRel, HandleMode,
  VPIEarlyOrdinaryExpressions, NoOrder, "37.16">;
def VPIEarlyVectorNetRightRange : VPITraversalEdge<
  VPIObjectSet<"EarlyVectorNetRightSources", [Net, StructNet, EnumNet,
    IntegerNet, TimeNet, PackedArrayNet]>, RightRangeRel, HandleMode,
  VPIEarlyOrdinaryExpressions, NoOrder, "37.16">;
def VPIEarlyStructNetMembers : VPITraversalEdge<
  VPIObjectSet<"EarlyStructNetSources", [StructNet]>, MemberRel, IterateMode,
  VPIEarlyNets, NoOrder, "37.16">;
def VPIEarlyPackedNetElementsEdge : VPITraversalEdge<
  VPIObjectSet<"EarlyPackedNetSources", [PackedArrayNet]>, ElementRel,
  IterateMode, VPIEarlyPackedNetElements, IndexOrder, "37.16">;
def VPIEarlyPackedNetElementIndex : VPITraversalEdge<
  VPIEarlyPackedNetElements, IndexRel, HandleMode, VPIEarlyOrdinaryExpressions,
  NoOrder, "37.16">;
// 37.17-37.21 Variables, packed arrays, selects, compatibility memory, and
// variable connectivity.
def VPIEarlyVariableExpr : VPITraversalEdge<VPIEarlyNonVirtualVariables,
  ExprRel, HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.17">;
def VPIEarlyVariableModule : VPITraversalEdge<VPIEarlyVariables, Module,
  HandleMode, ModuleTarget, NoOrder, "37.17; 37.20", 0,
  AutomaticParentScope>;
def VPIEarlyVariableInstance : VPITraversalEdge<VPIEarlyVariables,
  InstanceRel, HandleMode, VPIEarlyInstances, NoOrder, "37.17", 0,
  AutomaticParentScope>;
def VPIEarlyVariablePortInsts : VPITraversalEdge<VPIEarlyVariables,
  PortInstRel, IterateMode, VPIEarlyPorts, NoOrder, "37.17">;
def VPIEarlyVariableDriversEdge : VPITraversalEdge<VPIEarlyVariables, DriverRel,
  IterateMode, VPIEarlyVariableDrivers, NoOrder, "37.17; 37.21">;
def VPIEarlyVariableLoadsEdge : VPITraversalEdge<VPIEarlyVariables, LoadRel,
  IterateMode, VPIEarlyVariableLoads, NoOrder, "37.17; 37.21">;
def VPIEarlyVariablePrimTerms : VPITraversalEdge<VPIEarlyVariables, PrimTerm,
  IterateMode, PrimTermObjects, NoOrder, "37.17">;
def VPIEarlyVariableContAssigns : VPITraversalEdge<VPIEarlyVariables,
  ContAssign, IterateMode, ContAssignObjects, NoOrder, "37.17">;
def VPIEarlyVariablePathTerms : VPITraversalEdge<VPIEarlyVariables, PathTerm,
  IterateMode, PathTermObjects, NoOrder, "37.17">;
def VPIEarlyVariableTchkTerms : VPITraversalEdge<VPIEarlyVariables, TchkTerm,
  IterateMode, TchkTermObjects, NoOrder, "37.17">;
def VPIEarlyVariableTypespec : VPITraversalEdge<VPIEarlyNonVirtualVariables,
  Typespec, HandleMode, VPIEarlyTypespecs, NoOrder, "37.17">;
def VPIEarlyVariableParent : VPITraversalEdge<VPIEarlyVariables, ParentRel,
  HandleMode, VPIEarlyVarParents, NoOrder, "37.17">;
def VPIEarlyArrayVarSelects : VPITraversalEdge<RegArrayObjects, VarSelect,
  IterateMode, VPIObjectSet<"EarlyVarSelectTargets", [VarSelect]>, IndexOrder,
  "37.17; 37.19">;
def VPIEarlyVariableBits : VPITraversalEdge<
  VPIObjectSet<"EarlyBitVariableSources", [Reg, BitVar, StructVar, UnionVar,
    PackedArrayVar]>, BitRel, IterateMode,
  VPIObjectSet<"EarlyVarBitTargets", [RegBit]>, IndexOrder, "37.17">;
def VPIEarlyVarBitIndex : VPITraversalEdge<
  VPIObjectSet<"EarlyVarBitIndexSources", [RegBit]>, IndexRel, HandleMode,
  VPIEarlyOrdinaryExpressions, NoOrder, "37.17">;
def VPIEarlyArrayVarIndices : VPITraversalEdge<VPIEarlyVariables, IndexRel,
  IterateMode, VPIEarlyOrdinaryExpressions, IndexOrder, "37.17; 37.18">;
def VPIEarlyVectorVarRanges : VPITraversalEdge<
  VPIObjectSet<"EarlyVectorVarSources", [PackedArrayVar, BitVar, Reg,
    StructVar, UnionVar, EnumVar]>, Range, IterateMode, RangeObjects,
  IndexOrder, "37.17">;
def VPIEarlyVectorVarLeftRange : VPITraversalEdge<
  VPIObjectSet<"EarlyVectorVarLeftSources", [PackedArrayVar, BitVar, Reg,
    StructVar, UnionVar, EnumVar]>, LeftRangeRel, HandleMode,
  VPIEarlyOrdinaryExpressions, NoOrder, "37.17">;
def VPIEarlyVectorVarRightRange : VPITraversalEdge<
  VPIObjectSet<"EarlyVectorVarRightSources", [PackedArrayVar, BitVar, Reg,
    StructVar, UnionVar, EnumVar]>, RightRangeRel, HandleMode,
  VPIEarlyOrdinaryExpressions, NoOrder, "37.17">;
def VPIEarlyStructUnionMembers : VPITraversalEdge<
  VPIObjectSet<"EarlyStructUnionSources", [StructVar, UnionVar]>, MemberRel,
  IterateMode, VPIEarlyVariables, NoOrder, "37.17; 37.26">;
def VPIEarlyPackedVarElementsEdge : VPITraversalEdge<
  VPIObjectSet<"EarlyPackedVarSources", [PackedArrayVar]>, ElementRel,
  IterateMode, VPIEarlyPackedVarElements, IndexOrder, "37.18">;
def VPIEarlyPackedVarElementIndex : VPITraversalEdge<
  VPIEarlyPackedVarElements, IndexRel, HandleMode, VPIEarlyOrdinaryExpressions,
  NoOrder, "37.18">;
def VPIEarlyVarSelectParent : VPITraversalEdge<
  VPIObjectSet<"EarlyVarSelectParentSource", [VarSelect]>, ParentRel,
  HandleMode, RegArrayObjects, NoOrder, "37.19">;
def VPIEarlyVarSelectIndex : VPITraversalEdge<
  VPIObjectSet<"EarlyIndexedSelectSources", [VarSelect, BitSelect]>, IndexRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.19; 37.58">;
def VPIEarlyVarSelectIndices : VPITraversalEdge<
  VPIObjectSet<"EarlyVarSelectSource", [VarSelect]>, IndexRel, IterateMode,
  VPIEarlyOrdinaryExpressions, IndexOrder, "37.19">;
def VPIEarlyVarSelectTypespec : VPITraversalEdge<
  VPIObjectSet<"EarlySelectTypespecSources",
    [VarSelect, BitSelect, PartSelect, IndexedPartSelect]>,
  Typespec, HandleMode, VPIEarlyTypespecs, NoOrder, "37.19; 37.58; 37.59">;
def VPIEarlyModuleMemories : VPITraversalEdge<ModuleSource, Memory,
  IterateMode, RegArrayObjects, NoOrder, "37.20">;
def VPIEarlyMemoryLeftRange : VPITraversalEdge<RegArrayObjects, LeftRangeRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.20">;
def VPIEarlyMemoryRightRange : VPITraversalEdge<RegArrayObjects,
  RightRangeRel, HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.20">;
def VPIEarlyMemoryWords : VPITraversalEdge<RegArrayObjects, MemoryWord,
  IterateMode, RegObjects, IndexOrder, "37.20">;
def VPIEarlyMemoryWordIndex : VPITraversalEdge<RegObjects, IndexRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.20">;

// 37.22-37.27 Range, nettypes, generic interconnect, typespecs,
// structures/unions, and named events.
def VPIEarlyRangeLeft : VPITraversalEdge<RangeObjects, LeftRangeRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.22">;
def VPIEarlyRangeRight : VPITraversalEdge<RangeObjects, RightRangeRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.22">;
def VPIEarlyNettypeAlias : VPITraversalEdge<
  VPIEarlyNettypeDecls, NetTypedefAliasRel, HandleMode,
  VPIEarlyNettypeDecls, NoOrder, "37.23">;
def VPIEarlyNettypeTypespec : VPITraversalEdge<VPIEarlyNettypeDecls, Typespec,
  HandleMode, VPIEarlyTypespecs, NoOrder, "37.23">;
def VPIEarlyNettypeWith : VPITraversalEdge<VPIEarlyNettypeDecls, WithRel,
  HandleMode, FunctionObjects, NoOrder,
  "37.23">;
def VPIEarlyInterconnectArrayLeft : VPITraversalEdge<
  VPIEarlyInterconnectArrays,
  LeftRangeRel, HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.24">;
def VPIEarlyInterconnectArrayRight : VPITraversalEdge<
  VPIEarlyInterconnectArrays,
  RightRangeRel, HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.24">;
def VPIEarlyInterconnectArrayElements : VPITraversalEdge<
  VPIEarlyInterconnectArrays, ElementRel, IterateMode,
  VPIEarlyInterconnectArrayElementTargets,
  IndexOrder, "37.24">;
def VPIEarlyInterconnectNetElements : VPITraversalEdge<VPIEarlyInterconnectNets,
  ElementRel, IterateMode, VPIEarlyNets, IndexOrder, "37.24">;
def VPIEarlyInterconnectNetMembers : VPITraversalEdge<VPIEarlyInterconnectNets,
  MemberRel, IterateMode, VPIEarlyNets, DeclarationOrder, "37.24">;
def VPIEarlyInstanceTypespecs : VPITraversalEdge<InstanceObjects, Typespec,
  IterateMode, VPIEarlyTypespecs, NoOrder, "37.25">;
def VPIEarlyTypespecInstance : VPITraversalEdge<VPIEarlyTypespecs,
  InstanceRel, HandleMode, VPIEarlyInstances, NoOrder, "37.25">;
def VPIEarlyTypespecAlias : VPITraversalEdge<VPIEarlyTypespecs,
  TypedefAliasRel, HandleMode, VPIEarlyTypespecs, NoOrder, "37.25">;
def VPIEarlyEnumBase : VPITraversalEdge<
  VPIObjectSet<"EarlyEnumTypespecSource", [EnumTypespec]>, BaseTypespecRel,
  HandleMode, VPIEarlyTypespecs, NoOrder, "37.25">;
def VPIEarlyEnumConstants : VPITraversalEdge<
  VPIObjectSet<"EarlyEnumTypespecConstSource", [EnumTypespec]>, EnumConst,
  IterateMode, VPIEarlyEnumConsts, NoOrder, "37.25">;
def VPIEarlyEnumConstantTypespec : VPITraversalEdge<VPIEarlyEnumConsts,
  EnumTypespec, HandleMode,
  VPIObjectSet<"EarlyEnumTypespecTarget", [EnumTypespec]>, NoOrder, "37.25">;
def VPIEarlyTypespecMemberTypespec : VPITraversalEdge<
  VPIEarlyTypespecMembers, Typespec, HandleMode, VPIEarlyTypespecs, NoOrder,
  "37.25">;
def VPIEarlyTypespecMemberExpr : VPITraversalEdge<VPIEarlyTypespecMembers,
  ExprRel, HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.25">;
def VPIEarlyAggregateTypespecMembers : VPITraversalEdge<
  VPIObjectSet<"EarlyAggregateTypespecSources", [StructTypespec,
    UnionTypespec]>, TypespecMember, IterateMode, VPIEarlyTypespecMembers,
  NoOrder, "37.25">;
def VPIEarlyPackedTypespecRanges : VPITraversalEdge<
  VPIObjectSet<"EarlyPackedTypespecSources", [BitTypespec, LogicTypespec,
    PackedArrayTypespec, ArrayTypespec]>, Range, IterateMode, RangeObjects,
  IndexOrder, "37.25">;
def VPIEarlyPackedTypespecLeft : VPITraversalEdge<
  VPIObjectSet<"EarlyPackedTypespecLeftSources", [BitTypespec,
    LogicTypespec, PackedArrayTypespec, ArrayTypespec]>, LeftRangeRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.25">;
def VPIEarlyPackedTypespecRight : VPITraversalEdge<
  VPIObjectSet<"EarlyPackedTypespecRightSources", [BitTypespec,
    LogicTypespec, PackedArrayTypespec, ArrayTypespec]>, RightRangeRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.25">;
def VPIEarlyArrayIndexTypespec : VPITraversalEdge<
  VPIObjectSet<"EarlyArrayTypespecIndexSource", [ArrayTypespec]>,
  IndexTypespecRel, HandleMode, VPIEarlyTypespecs, NoOrder, "37.25">;
def VPIEarlyBitElemTypespec : VPITraversalEdge<
  VPIObjectSet<"EarlyBitTypespecSource", [BitTypespec]>, ElemTypespecRel,
  HandleMode, VPIObjectSet<"EarlyBitTypespecTarget", [BitTypespec]>,
  NoOrder, "37.25">;
def VPIEarlyLogicElemTypespec : VPITraversalEdge<
  VPIObjectSet<"EarlyLogicTypespecSource", [LogicTypespec]>, ElemTypespecRel,
  HandleMode, VPIObjectSet<"EarlyLogicTypespecTarget", [LogicTypespec]>,
  NoOrder, "37.25">;
def VPIEarlyPackedArrayElemTypespec : VPITraversalEdge<
  VPIObjectSet<"EarlyPackedArrayTypespecSource", [PackedArrayTypespec]>,
  ElemTypespecRel, HandleMode,
  VPIObjectSet<"EarlyPackedArrayTypespecTargets", [EnumTypespec,
    StructTypespec, UnionTypespec, PackedArrayTypespec]>, NoOrder, "37.25">;
def VPIEarlyArrayElemTypespec : VPITraversalEdge<
  VPIObjectSet<"EarlyArrayTypespecSource", [ArrayTypespec]>, ElemTypespecRel,
  HandleMode, VPIEarlyTypespecs, NoOrder, "37.25">;
def VPIEarlyNamedEventTypespec : VPITraversalEdge<
  NamedEventTargetObjects, Typespec, HandleMode,
  VPIObjectSet<"EarlyEventTypespecTarget", [EventTypespec]>, NoOrder,
  "37.27">;
def VPIEarlyNamedEventModule : VPITraversalEdge<NamedEventTargetObjects, Module,
  HandleMode, ModuleTarget, NoOrder, "37.27", 0, AutomaticParentScope>;
def VPIEarlyNamedEventInstance : VPITraversalEdge<NamedEventTargetObjects,
  InstanceRel, HandleMode, VPIEarlyInstances, NoOrder, "37.27", 0,
  AutomaticParentScope>;
def VPIEarlyNamedEventWaiters : VPITraversalEdge<NamedEventTargetObjects,
  WaitingProcessesRel, IterateMode, ThreadObjects, NoOrder, "37.27">;
def VPIEarlyNamedEventArrayRange : VPITraversalEdge<NamedEventArrayObjects,
  Range, IterateMode, RangeObjects, IndexOrder, "37.27">;
def VPIEarlyNamedEventArrayTypespec : VPITraversalEdge<NamedEventArrayObjects,
  Typespec, HandleMode,
  VPIObjectSet<"EarlyNamedEventArrayTypespecTarget", [ArrayTypespec]>,
  NoOrder, "37.27">;
def VPIEarlyNamedEventArrayModule : VPITraversalEdge<NamedEventArrayObjects,
  Module, HandleMode, ModuleTarget, NoOrder, "37.27", 0,
  AutomaticParentScope>;
def VPIEarlyNamedEventArrayInstance : VPITraversalEdge<
  NamedEventArrayObjects, InstanceRel, HandleMode, VPIEarlyInstances, NoOrder,
  "37.27", 0, AutomaticParentScope>;
def VPIEarlyNamedEventArrayElements : VPITraversalEdge<NamedEventArrayObjects,
  NamedEvent, IterateMode, NamedEventTargetObjects, IndexOrder, "37.27">;
def VPIEarlyNamedEventParent : VPITraversalEdge<NamedEventTargetObjects,
  ParentRel, HandleMode, NamedEventArrayObjects, NoOrder, "37.27", 0,
  AutomaticIndexedContainer>;
def VPIEarlyNamedEventIndices : VPITraversalEdge<NamedEventTargetObjects,
  IndexRel, IterateMode, VPIEarlyOrdinaryExpressions, IndexOrder, "37.27">;

// 37.28 Parameters and assignments.
def VPIEarlyParameterModule : VPITraversalEdge<VPIEarlyParameters, Module,
  HandleMode, ModuleTarget, NoOrder, "37.28">;
def VPIEarlyParameterTypespec : VPITraversalEdge<ParameterObjects, Typespec,
  HandleMode, VPIEarlyTypespecs, NoOrder, "37.28">;
def VPIEarlyParameterExpr : VPITraversalEdge<ParameterObjects, ExprRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.28">;
def VPIEarlyParameterLeft : VPITraversalEdge<ParameterObjects, LeftRangeRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.28">;
def VPIEarlyParameterRight : VPITraversalEdge<ParameterObjects,
  RightRangeRel, HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.28">;
def VPIEarlyTypeParameterTypespec : VPITraversalEdge<TypeParameterObjects,
  Typespec, HandleMode, VPIEarlyTypespecs, NoOrder, "37.28">;
def VPIEarlyTypeParameterExpr : VPITraversalEdge<TypeParameterObjects,
  ExprRel, HandleMode, VPIEarlyTypespecs, NoOrder, "37.28">;
def VPIEarlyDefParamLhs : VPITraversalEdge<DefParamObjects, LhsRel,
  HandleMode, ParameterObjects, NoOrder, "37.28">;
def VPIEarlyDefParamRhs : VPITraversalEdge<DefParamObjects, RhsRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.28">;
def VPIEarlyModuleParamAssigns : VPITraversalEdge<ModuleSource, ParamAssign,
  IterateMode, VPIObjectSet<"EarlyModuleParamAssignTargets", [ParamAssign]>,
  NoOrder, "37.28">;
def VPIEarlyScopeParamAssigns : VPITraversalEdge<NonInstanceScopeObjects,
  ParamAssign, IterateMode,
  VPIObjectSet<"EarlyScopeParamAssignTargets", [ParamAssign]>,
  NoOrder, "37.28">;
def VPIEarlyParamAssignModule : VPITraversalEdge<
  VPIObjectSet<"EarlyParamAssignSources", [ParamAssign]>, Module, HandleMode,
  ModuleTarget, NoOrder, "37.28">;
def VPIEarlyParamAssignLhs : VPITraversalEdge<
  VPIObjectSet<"EarlyParamAssignLhsSources", [ParamAssign]>, LhsRel,
  HandleMode, VPIEarlyParameters, NoOrder, "37.28">;
def VPIEarlyParamAssignRhs : VPITraversalEdge<
  VPIObjectSet<"EarlyParamAssignRhsSources", [ParamAssign]>, RhsRel,
  HandleMode, VPIEarlyParamAssignRhsTargets, NoOrder, "37.28">;

// 37.29-37.33 Interface/class types and class instances.
def VPIEarlyVirtualInterfaceActual : VPITraversalEdge<
  VirtualInterfaceObjects, ActualRel, HandleMode, InterfaceModportObjects,
  NoOrder, "37.29">;
def VPIEarlyVirtualInterfaceTypespec : VPITraversalEdge<
  VirtualInterfaceObjects, Typespec, HandleMode,
  VPIObjectSet<"EarlyVirtualInterfaceTypespecTarget", [InterfaceTypespec]>,
  NoOrder, "37.29">;
def VPIEarlyVirtualInterfaceExpr : VPITraversalEdge<
  VirtualInterfaceObjects, ExprRel, HandleMode,
  VPIEarlyVirtualInterfaceExprs, NoOrder, "37.29">;
def VPIEarlyInterfaceTypespecParent : VPITraversalEdge<
  VPIObjectSet<"EarlyInterfaceTypespecSource", [InterfaceTypespec]>,
  ParentRel, HandleMode,
  VPIObjectSet<"EarlyParentInterfaceTypespecTarget", [InterfaceTypespec]>,
  NoOrder, "37.30">;
def VPIEarlyInterfaceTypespecParamAssigns : VPITraversalEdge<
  VPIObjectSet<"EarlyInterfaceTypespecParamSource", [InterfaceTypespec]>,
  ParamAssign, IterateMode,
  VPIObjectSet<"EarlyInterfaceTypespecParamTargets", [ParamAssign]>,
  NoOrder, "37.30">;
def VPIEarlyParamAssignInterfaceTypespec : VPITraversalEdge<
  VPIObjectSet<"EarlyInterfaceParamAssignSource", [ParamAssign]>,
  InterfaceTypespec, HandleMode,
  VPIObjectSet<"EarlyInterfaceParamAssignTarget", [InterfaceTypespec]>,
  NoOrder, "37.30">;
def VPIEarlyRootClassDefns : VPITraversalEdge<RootSource, ClassDefn,
  IterateMode, ClassDefnObjects, NoOrder, "37.31">;
def VPIEarlyClassDefnTypespec : VPITraversalEdge<ClassDefnObjects,
  ClassTypespec, HandleMode, ClassTypespecObjects, NoOrder, "37.31">;
def VPIEarlyClassExtends : VPITraversalEdge<ClassDefnObjects, ExtendsRel,
  HandleMode, VPIObjectSet<"EarlyExtendsTarget", [Extends]>, NoOrder,
  "37.31">;
def VPIEarlyExtendsClassDefn : VPITraversalEdge<
  VPIObjectSet<"EarlyExtendsSource", [Extends]>, ClassDefn, HandleMode,
  ClassDefnObjects, NoOrder, "37.31">;
def VPIEarlyExtendsTypespec : VPITraversalEdge<
  VPIObjectSet<"EarlyExtendsTypespecSource", [Extends]>, ClassTypespec,
  HandleMode, ClassTypespecObjects, NoOrder, "37.31">;
def VPIEarlyExtendsArguments : VPITraversalEdge<
  VPIObjectSet<"EarlyExtendsArgumentSource", [Extends]>, ArgumentRel,
  IterateMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.31">;
def VPIEarlyDerivedClasses : VPITraversalEdge<ClassDefnObjects,
  DerivedClassesRel, IterateMode, ClassDefnObjects, NoOrder,
  "37.31">;
def VPIEarlyClassDefnInstances : VPITraversalEdge<ClassDefnObjects,
  InstanceRel, IterateMode, VPIEarlyInstances, NoOrder, "37.31">;
def VPIEarlyClassDefnMethods : VPITraversalEdge<ClassDefnObjects, MethodsRel,
  IterateMode, VPIEarlyTaskFuncs, NoOrder, "37.31", 0,
  AutomaticDirectChild>;
def VPIEarlyClassDefnConstraints : VPITraversalEdge<ClassDefnObjects,
  Constraint, IterateMode, ConstraintObjects, DeclarationOrder, "37.31">;
def VPIEarlyClassTypespecClassDefn : VPITraversalEdge<ClassTypespecObjects,
  ClassDefn, HandleMode, ClassDefnObjects, NoOrder, "37.32">;
def VPIEarlyClassTypespecExtends : VPITraversalEdge<ClassTypespecObjects,
  ExtendsRel, HandleMode, ClassTypespecObjects, NoOrder, "37.32">;
def VPIEarlyClassTypespecMethods : VPITraversalEdge<ClassTypespecObjects,
  MethodsRel, IterateMode, VPIEarlyTaskFuncs, NoOrder, "37.32">;
def VPIEarlyClassTypespecConstraints : VPITraversalEdge<ClassTypespecObjects,
  Constraint, IterateMode, ConstraintObjects, NoOrder, "37.32">;
def VPIEarlyClassVarObject : VPITraversalEdge<ClassVarObjects, ClassObj,
  HandleMode, ClassObjectObjects, NoOrder, "37.33">;
def VPIEarlyClassVarTypespec : VPITraversalEdge<ClassVarObjects,
  ClassTypespec, HandleMode, ClassTypespecObjects, NoOrder, "37.33">;
def VPIEarlyClassObjTypespec : VPITraversalEdge<ClassObjectObjects,
  ClassTypespec, HandleMode, ClassTypespecObjects, NoOrder, "37.33">;
def VPIEarlyClassObjVariables : VPITraversalEdge<ClassObjectObjects,
  VariablesRel, IterateMode, VPIEarlyVariables, NoOrder, "37.33">;
def VPIEarlyClassObjWaiters : VPITraversalEdge<ClassObjectObjects,
  WaitingProcessesRel, IterateMode, ThreadObjects, NoOrder, "37.33">;
def VPIEarlyClassObjMessages : VPITraversalEdge<ClassObjectObjects,
  MessagesRel, IterateMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.33">;
def VPIEarlyClassObjMethods : VPITraversalEdge<ClassObjectObjects, MethodsRel,
  IterateMode, VPIEarlyTaskFuncs, NoOrder, "37.33">;
def VPIEarlyClassObjConstraints : VPITraversalEdge<ClassObjectObjects,
  Constraint, IterateMode, ConstraintObjects, NoOrder, "37.33">;
def VPIEarlyClassObjParameters : VPITraversalEdge<ClassObjectObjects,
  Parameter, IterateMode, ParameterIterationObjects, NoOrder, "37.33">;
def VPIEarlyClassObjVirtualInterfaces : VPITraversalEdge<ClassObjectObjects,
  VirtualInterfaceVar, IterateMode, VirtualInterfaceObjects, NoOrder,
  "37.33">;
def VPIEarlyClassObjNamedEvents : VPITraversalEdge<ClassObjectObjects,
  NamedEvent, IterateMode, NamedEventTargetObjects, NoOrder, "37.33">;
def VPIEarlyClassObjNamedEventArrays : VPITraversalEdge<ClassObjectObjects,
  NamedEventArray, IterateMode, NamedEventArrayObjects, NoOrder, "37.33">;
def VPIEarlyClassObjScopes : VPITraversalEdge<ClassObjectObjects,
  InternalScopeRel, IterateMode, VPIEarlyScopes, NoOrder, "37.33">;

// 37.34 Constraint/order/distribution and 37.38 constraint expressions.
def VPIEarlyRootConstraints : VPITraversalEdge<RootSource, Constraint,
  IterateMode, ConstraintObjects, NoOrder, "37.34">;
def VPIEarlyConstraintParent : VPITraversalEdge<ConstraintObjects, ParentRel,
  HandleMode, ClassObjectObjects, NoOrder, "37.34">;
def VPIEarlyConstraintItemsEdge : VPITraversalEdge<ConstraintObjects,
  ConstraintItemRel, IterateMode, VPIEarlyConstraintItems, SourceOrder,
  "37.34">;
def VPIEarlySolveBefore : VPITraversalEdge<ConstraintOrderingObjects,
  SolveBeforeRel, IterateMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.34">;
def VPIEarlySolveAfter : VPITraversalEdge<ConstraintOrderingObjects,
  SolveAfterRel, IterateMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.34">;
def VPIEarlyDistributionItems : VPITraversalEdge<
  VPIObjectSet<"EarlyDistributionSource", [Distribution]>, DistItem,
  IterateMode, DistItemObjects, NoOrder, "37.34">;
def VPIEarlyDistItemParent : VPITraversalEdge<DistItemObjects, Distribution,
  HandleMode, VPIObjectSet<"EarlyDistributionTarget", [Distribution]>,
  NoOrder, "37.34">;
def VPIEarlyDistributionExpr : VPITraversalEdge<
  VPIObjectSet<"EarlyDistributionExprSource", [Distribution]>, ExprRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.34">;
def VPIEarlyDistItemValueRange : VPITraversalEdge<DistItemObjects,
  ValueRangeRel, HandleMode, VPIEarlyDistributionValueTargets, NoOrder,
  "37.34">;
def VPIEarlyDistItemWeight : VPITraversalEdge<DistItemObjects, WeightRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.34">;
def VPIEarlyConstraintCondition : VPITraversalEdge<
  VPIObjectSet<"EarlyConditionalConstraintSources", [Implication, ConstrIf,
    ConstrIfElse]>, ConditionRel, HandleMode, VPIEarlyOrdinaryExpressions,
  NoOrder, "37.38">;
def VPIEarlyConstraintSubexpressions : VPITraversalEdge<
  VPIObjectSet<"EarlyConstraintSubexprSources", [Implication, ConstrIf,
    ConstrIfElse, ConstrForEach]>, ConstraintExpr, IterateMode,
  VPIEarlyConstraintExpressions, SourceOrder, "37.38">;
def VPIEarlyConstraintElse : VPITraversalEdge<
  VPIObjectSet<"EarlyConstraintElseSources", [ConstrIfElse]>, ElseConstRel,
  HandleMode, VPIEarlyConstraintExpressions, NoOrder, "37.38">;
def VPIEarlySoftDisableExpr : VPITraversalEdge<
  VPIObjectSet<"EarlySoftDisableSource", [SoftDisable]>, ExprRel, HandleMode,
  VPIEarlyOrdinaryExpressions, NoOrder, "37.38">;
def VPIEarlyForeachVariables : VPITraversalEdge<
  VPIObjectSet<"EarlyConstrForeachSource", [ConstrForEach]>, VariablesRel,
  IterateMode,
  VPIObjectSet<"EarlyConstraintIndexedVariables", [RegArray, PackedArrayVar]>,
  NoOrder, "37.38">;
def VPIEarlyForeachLoopVars : VPITraversalEdge<
  VPIObjectSet<"EarlyConstrForeachLoopSource", [ConstrForEach]>, LoopVarsRel,
  IterateMode, VPIEarlyLoopVars, SourceOrder, "37.38">;

// 37.35-37.37 Primitive/UDP/intermodule path.
def VPIEarlyPrimitiveDelay : VPITraversalEdge<VPIEarlyPrimitives, DelayRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.35">;
def VPIEarlyPrimitiveIndex : VPITraversalEdge<PrimitiveObjects, IndexRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.35">;
def VPIEarlyPrimitiveUdpDefn : VPITraversalEdge<
  VPIObjectSet<"EarlyUdpPrimitiveSource", [Udp]>, UdpDefn, HandleMode,
  VPIObjectSet<"EarlyPrimitiveUdpDefnTarget", [UdpDefn]>, NoOrder, "37.35">;
def VPIEarlyPrimitiveTerms : VPITraversalEdge<PrimitiveObjects, PrimTerm,
  IterateMode, PrimTermObjects, IndexOrder, "37.35">;
def VPIEarlyPrimTermPrimitive : VPITraversalEdge<PrimTermObjects,
  PrimitiveRel, HandleMode, VPIEarlyPrimitives, NoOrder, "37.35">;
def VPIEarlyPrimTermExpr : VPITraversalEdge<PrimTermObjects, ExprRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.35">;
def VPIEarlyRootUdpDefns : VPITraversalEdge<RootSource, UdpDefn, IterateMode,
  VPIObjectSet<"EarlyRootUdpDefnTargets", [UdpDefn]>, NoOrder, "37.36">;
def VPIEarlyUdpTableEntries : VPITraversalEdge<
  VPIObjectSet<"EarlyUdpDefinitionSource", [UdpDefn]>, TableEntry,
  IterateMode, VPIObjectSet<"EarlyTableEntryTargets", [TableEntry]>,
  NoOrder, "37.36">;
def VPIEarlyUdpInitial : VPITraversalEdge<
  VPIObjectSet<"EarlyUdpInitialSource", [UdpDefn]>, Initial, HandleMode,
  VPIObjectSet<"EarlyUdpInitialTarget", [Initial]>, NoOrder, "37.36">;
def VPIEarlyInterModPathPorts : VPITraversalEdge<InterModPathObjects,
  PortsRel, IterateMode, VPIEarlyPorts, NoOrder, "37.37">;

// 37.39 Module paths and 37.40 timing checks.
def VPIEarlyModPathCondition : VPITraversalEdge<ModPathObjects, ConditionRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.39">;
def VPIEarlyModPathDelay : VPITraversalEdge<ModPathObjects, DelayRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.39">;
def VPIEarlyModDataPathIn : VPITraversalEdge<ModPathObjects,
  ModDataPathInRel, IterateMode, PathTermObjects, NoOrder, "37.39">;
def VPIEarlyModPathOut : VPITraversalEdge<ModPathObjects, ModPathOutRel,
  IterateMode, PathTermObjects, NoOrder, "37.39">;
def VPIEarlyModPathIn : VPITraversalEdge<ModPathObjects, ModPathInRel,
  IterateMode, PathTermObjects, NoOrder, "37.39">;
def VPIEarlyPathTermModPath : VPITraversalEdge<PathTermObjects, ModPath,
  HandleMode, ModPathObjects, NoOrder, "37.39">;
def VPIEarlyPathTermExpr : VPITraversalEdge<PathTermObjects, ExprRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.39">;
def VPIEarlyTchkDelay : VPITraversalEdge<TchkObjects, DelayRel, HandleMode,
  VPIEarlyOrdinaryExpressions, NoOrder, "37.40">;
def VPIEarlyTchkExprs : VPITraversalEdge<TchkObjects, ExprRel, IterateMode,
  VPIEarlyTchkExprTargets, NoOrder, "37.40">;
def VPIEarlyTchkRefTerm : VPITraversalEdge<TchkObjects, TchkRefTermRel,
  HandleMode, TchkTermObjects, NoOrder, "37.40">;
def VPIEarlyTchkDataTerm : VPITraversalEdge<TchkObjects, TchkDataTermRel,
  HandleMode, TchkTermObjects, NoOrder, "37.40">;
def VPIEarlyTchkNotifier : VPITraversalEdge<TchkObjects, TchkNotifierRel,
  HandleMode, RegObjects, NoOrder, "37.40">;
def VPIEarlyTchkTermExpr : VPITraversalEdge<TchkTermObjects, ExprRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.40">;
def VPIEarlyTchkTermCondition : VPITraversalEdge<TchkTermObjects,
  ConditionRel, HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.40">;

// 37.41-37.42 Task/function declarations and calls.
def VPIEarlyFunctionLeftRange : VPITraversalEdge<FunctionObjects,
  LeftRangeRel, HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.41">;
def VPIEarlyFunctionRightRange : VPITraversalEdge<FunctionObjects,
  RightRangeRel, HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.41">;
def VPIEarlyFunctionReturn : VPITraversalEdge<FunctionObjects, ReturnRel,
  HandleMode, VPIEarlyVariables, NoOrder, "37.41">;
def VPIEarlyFunctionClassDefn : VPITraversalEdge<FunctionObjects, ClassDefn,
  HandleMode, ClassDefnObjects, NoOrder, "37.41">;
def VPIEarlyTaskFuncParent : VPITraversalEdge<TaskFunctionDeclObjects,
  ParentRel, HandleMode, RefObjectObjects, NoOrder, "37.41">;
def VPIEarlyFuncCallFunction : VPITraversalEdge<
  VPIObjectSet<"EarlyFunctionCallSources", [FuncCall, MethodFuncCall]>,
  Function, HandleMode, FunctionObjects, NoOrder, "37.41; 37.42">;
def VPIEarlyTaskCallTask : VPITraversalEdge<
  VPIObjectSet<"EarlyTaskCallSources", [TaskCall, MethodTaskCall]>, Task,
  HandleMode, VPIObjectSet<"EarlyTaskTarget", [Task]>, NoOrder,
  "37.41; 37.42">;
def VPIEarlyCallScope : VPITraversalEdge<
  VPIObjectSet<"EarlyExpressionCallSources",
    [FuncCall, MethodFuncCall, SysFuncCall]>,
  ScopeRel, HandleMode, VPIEarlyScopes, NoOrder, "37.42">;
def VPIEarlyCallArgumentsEdge : VPITraversalEdge<TaskFunctionCallObjects,
  ArgumentRel, IterateMode, VPIEarlyCallArguments, NoOrder, "37.42">;
def VPIEarlyMethodWith : VPITraversalEdge<
  VPIObjectSet<"EarlyMethodWithSources", [MethodFuncCall, MethodTaskCall]>,
  WithRel, HandleMode, VPIEarlyMethodWithTargets, NoOrder, "37.42">;
def VPIEarlySysCallUserSystf : VPITraversalEdge<VPIEarlySysCalls, UserSystf,
  HandleMode, UserSystfObjects, NoOrder, "37.42">;

// 37.43-37.45 Frames, threads, and delay terminals.  The 2023 frame object
// model is explicitly not backward compatible with IEEE 1800-2017.
def VPIEarlyFrameOrigin : VPITraversalEdge<FrameObjects, OriginRel,
  HandleMode, VPIEarlyFramesOrigins, NoOrder, "37.43">;
def VPIEarlyFrameParent : VPITraversalEdge<FrameObjects, ParentRel,
  HandleMode, VPIEarlyFrameParents, NoOrder, "37.43">;
def VPIEarlyFrameStmt : VPITraversalEdge<FrameObjects, StmtRel, HandleMode,
  StatementObjects, NoOrder, "37.43">;
def VPIEarlyFrameThread : VPITraversalEdge<FrameObjects, Thread, HandleMode,
  ThreadObjects, NoOrder, "37.43">;
def VPIEarlyThreadFrame : VPITraversalEdge<ThreadObjects, Frame, HandleMode,
  FrameObjects, NoOrder, "37.43; 37.44">;
def VPIEarlyFrameAutomatics : VPITraversalEdge<FrameObjects, AutomaticsRel,
  IterateMode, VPIEarlyAutomatics, NoOrder, "37.43">;
def VPIEarlyThreadParent : VPITraversalEdge<ThreadObjects, ParentRel,
  HandleMode, ThreadObjects, NoOrder, "37.44">;
def VPIEarlyThreadOrigin : VPITraversalEdge<ThreadObjects, OriginRel,
  HandleMode, StatementObjects, NoOrder, "37.44">;
def VPIEarlyThreadChildren : VPITraversalEdge<ThreadObjects, Thread,
  IterateMode, ThreadObjects, NoOrder, "37.44">;
def VPIEarlyDelayDeviceModule : VPITraversalEdge<DelayDeviceObjects, Module,
  HandleMode, ModuleTarget, NoOrder, "37.45">;
def VPIEarlyDelayDeviceInTerm : VPITraversalEdge<DelayDeviceObjects,
  InTermRel, HandleMode, DelayTermObjects, NoOrder, "37.45">;
def VPIEarlyDelayDeviceOutTerm : VPITraversalEdge<DelayDeviceObjects,
  OutTermRel, HandleMode, DelayTermObjects, NoOrder, "37.45">;
def VPIEarlyDelayTermDrivers : VPITraversalEdge<DelayTermObjects, DriverRel,
  IterateMode, VPIEarlyNetDriverTargets, NoOrder, "37.45">;
def VPIEarlyDelayTermLoads : VPITraversalEdge<DelayTermObjects, LoadRel,
  IterateMode, VPIEarlyLoads, NoOrder, "37.45">;

// 37.47 Continuous assignments.
def VPIEarlyContAssignDelay : VPITraversalEdge<ContAssignObjects, DelayRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.47">;
def VPIEarlyContAssignBits : VPITraversalEdge<ContAssignObjects, BitRel,
  IterateMode, VPIObjectSet<"EarlyContAssignBitTargets", [ContAssignBit]>,
  IndexOrder, "37.47", 1>;
def VPIEarlyContAssignBitParent : VPITraversalEdge<
  VPIObjectSet<"EarlyContAssignBitSources", [ContAssignBit]>, ParentRel,
  HandleMode, ContAssignObjects, NoOrder, "37.47">;
def VPIEarlyContAssignRhs : VPITraversalEdge<ContAssignObjects, RhsRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.47">;
def VPIEarlyContAssignLhs : VPITraversalEdge<ContAssignObjects, LhsRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.47">;

// 37.48 Clocking blocks and clocking IO declarations.
def VPIEarlyClockingInputSkew : VPITraversalEdge<ClockingBlockObjects,
  InputSkewRel, HandleMode, DelayControlObjects, NoOrder, "37.48">;
def VPIEarlyClockingOutputSkew : VPITraversalEdge<ClockingBlockObjects,
  OutputSkewRel, HandleMode, DelayControlObjects, NoOrder, "37.48">;
def VPIEarlyClockingEvent : VPITraversalEdge<ClockingBlockObjects,
  ClockingEventRel, HandleMode, EventControlObjects, NoOrder, "37.48">;
def VPIEarlyClockingIODecls : VPITraversalEdge<ClockingBlockObjects,
  ClockingIODecl, IterateMode, ClockingIODeclObjects, NoOrder, "37.48">;
def VPIEarlyClockingPrefix : VPITraversalEdge<ClockingBlockObjects,
  PrefixRel, HandleMode, VirtualInterfaceObjects, NoOrder, "37.48">;
def VPIEarlyClockingActual : VPITraversalEdge<ClockingBlockObjects,
  ActualRel, HandleMode, ClockingBlockObjects, NoOrder, "37.48">;
def VPIEarlyClockingIOInputSkew : VPITraversalEdge<ClockingIODeclObjects,
  InputSkewRel, HandleMode, DelayControlObjects, NoOrder, "37.48">;
def VPIEarlyClockingIOOutputSkew : VPITraversalEdge<ClockingIODeclObjects,
  OutputSkewRel, HandleMode, DelayControlObjects, NoOrder, "37.48">;
def VPIEarlyClockingIOExpr : VPITraversalEdge<ClockingIODeclObjects, ExprRel,
  HandleMode, VPIEarlyOrdinaryExpressions, NoOrder, "37.48">;

// 37.49-37.50 Assertion and concurrent assertion diagrams.
def VPIEarlyRootAssertions : VPITraversalEdge<RootSource, AssertionRel,
  IterateMode, VPIEarlyAssertions, NoOrder, "37.49">;
def VPIEarlyAssertionInstance : VPITraversalEdge<VPIEarlyAssertions,
  InstanceRel, HandleMode, VPIEarlyInstances, NoOrder, "37.49">;
def VPIEarlyAssertionClockingBlock : VPITraversalEdge<VPIEarlyAssertions,
  ClockingBlock, HandleMode, ClockingBlockObjects, NoOrder, "37.49">;
def VPIEarlyConcurrentClockingEvent : VPITraversalEdge<
  VPIEarlyConcurrentAssertions, ClockingEventRel, HandleMode,
  VPIEarlyOrdinaryExpressions, NoOrder, "37.50">;
def VPIEarlyConcurrentStmt : VPITraversalEdge<
  VPIObjectSet<"EarlyConcurrentStatementSources", [Assert, Assume, Cover]>,
  StmtRel, HandleMode, StatementObjects, NoOrder, "37.50", 1>;
def VPIEarlyConcurrentElseStmt : VPITraversalEdge<
  VPIObjectSet<"EarlyConcurrentElseSources", [Assert]>, ElseStmtRel,
  HandleMode, StatementObjects, NoOrder, "37.50", 1>;
def VPIEarlyConcurrentProperty : VPITraversalEdge<
  VPIEarlyConcurrentAssertions, PropertyRel, HandleMode,
  VPIEarlyPropertyTargets, NoOrder, "37.50">;
