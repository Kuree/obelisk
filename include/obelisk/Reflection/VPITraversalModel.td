//===- VPITraversalModel.td - IEEE VPI legal traversal graph ------------===//
//
// This file describes the legal source/selector/target graph from IEEE
// 1800-2017 Chapter 37.  It is deliberately separate from the public spelling
// inventory: a numeric constant can be an object kind, a traversal selector,
// or both, while only legal edges determine how a call is dispatched.
//
//===----------------------------------------------------------------------===//

include "VPIObjectModel.td"

class VPIObjectSet<string cppName_,
                   list<VPIObjectKind> objects_ = [],
                   list<VPIObjectFamily> families_ = [],
                   list<VPIObjectKind> exclude_ = [],
                   bit nullRoot_ = 0,
                   bit iterateSources_ = 0> {
  string cppName = cppName_;
  list<VPIObjectKind> objects = objects_;
  list<VPIObjectFamily> families = families_;
  list<VPIObjectKind> exclude = exclude_;
  bit nullRoot = nullRoot_;
  bit iterateSources = iterateSources_;
}

class VPITraversalMode<string cppName_, int value_> {
  string cppName = cppName_;
  int value = value_;
}

def HandleMode : VPITraversalMode<"Handle", 0>;
def IterateMode : VPITraversalMode<"Iterate", 1>;

class VPITraversalOrder<string cppName_, int value_> {
  string cppName = cppName_;
  int value = value_;
}

def NoOrder : VPITraversalOrder<"None", 0>;
def SourceOrder : VPITraversalOrder<"Source", 1>;
def DeclarationOrder : VPITraversalOrder<"Declaration", 2>;
def IndexOrder : VPITraversalOrder<"Index", 3>;
def TimeOrder : VPITraversalOrder<"Time", 4>;

// Static relations are recoverable directly from the elaborated design
// inventory.  DirectChild means that the edge enumerates records whose
// scope/owner is the source scope.  ParentScope means that the edge returns
// the source's effective enclosing scope: the record named by a scope/parent
// field for ordinary records, or the nearest lexical scope for statements.
// DirectPortConnection identifies the canonical net or variable backing a
// direct whole-source port alias.
class VPIAutomaticRelation<string cppName_, int value_> {
  string cppName = cppName_;
  int value = value_;
}

def NoAutomaticRelation : VPIAutomaticRelation<"None", 0>;
def AutomaticDirectChild : VPIAutomaticRelation<"DirectChild", 1>;
def AutomaticParentScope : VPIAutomaticRelation<"ParentScope", 2>;
def AutomaticDirectPortConnection :
    VPIAutomaticRelation<"DirectPortConnection", 3>;

// statementContainment marks forward structural ownership of Statement-family
// targets. Sources may be scopes, code units/processes, assertions, or other
// statements. Cross-references and reverse parent/scope edges do not set it.
class VPITraversalEdge<VPIObjectSet sources_, VPISelector selector_,
                       VPITraversalMode mode_, VPIObjectSet targets_,
                       VPITraversalOrder order_, string clause_,
                       bit statementContainment_ = 0,
                       VPIAutomaticRelation automaticRelation_ =
                           NoAutomaticRelation> {
  VPIObjectSet sources = sources_;
  VPISelector selector = selector_;
  VPITraversalMode mode = mode_;
  VPIObjectSet targets = targets_;
  VPITraversalOrder order = order_;
  string clause = clause_;
  bit statementContainment = statementContainment_;
  VPIAutomaticRelation automaticRelation = automaticRelation_;
}

// Abstract target classes.  Expansion always discards aliases and
// non-concrete selector spellings.
def ScopeObjects : VPIObjectSet<"ScopeObjects", [ClassObj], [ScopeFamily]>;
def DeclarationObjects : VPIObjectSet<"DeclarationObjects", [],
  [DeclarationFamily]>;
// The LRM's "stmt" node excludes helper/container objects such as case item,
// continuous assignment, continuous-assignment bit, and alias statement.
def StatementObjects : VPIObjectSet<"StatementObjects",
  [AssignStmt, Assignment, Begin, Case, Deassign, DelayControl, Disable,
   EventControl, EventStmt, For, Force, Forever, Fork, If, IfElse, NamedBegin,
   NamedFork, NullStmt, Release, Repeat, RepeatControl, SysTaskCall, TaskCall,
   Wait, While, MethodTaskCall, ImmediateAssert, ImmediateAssume,
   ImmediateCover, DoWhile, OrderedWait, WaitFork, DisableFork, ExpectStmt,
   ForeachStmt, ReturnStmt, Break, Continue]>;
def ExpressionObjects : VPIObjectSet<"ExpressionObjects", [],
  [ExpressionFamily]>;
def VariableObjects : VPIObjectSet<"VariableObjects", [], [VariableFamily]>;
def NetObjects : VPIObjectSet<"NetObjects", [], [NetFamily]>;
def ArrayObjects : VPIObjectSet<"ArrayObjects", [], [ArrayFamily]>;
def TypespecObjects : VPIObjectSet<"TypespecObjects", [], [TypespecFamily]>;
def PrimitiveObjects : VPIObjectSet<"PrimitiveObjects", [Gate, Switch, Udp]>;
def AssertionObjects : VPIObjectSet<"AssertionObjects", [],
  [AssertionFamily]>;

def RootSource : VPIObjectSet<"Root", [], [], [], 1>;
def ModuleSource : VPIObjectSet<"Module", [Module]>;
def ScopeSource : VPIObjectSet<"Scope", [], [ScopeFamily]>;
def ModuleTarget : VPIObjectSet<"ModuleTarget", [Module]>;

// Root discovery is the only generally legal NULL-root object iteration.
def RootModules : VPITraversalEdge<RootSource, Module, IterateMode,
  ModuleTarget, NoOrder, "37.5">;

// Reusable exact unions from the Chapter 37 diagrams.
def InstanceObjects : VPIObjectSet<"InstanceObjects",
  [Package, Module, Interface, Program]>;
def StaticProcessObjects : VPIObjectSet<"StaticProcessObjects",
  [Initial, Final, Always]>;
def TaskFunctionDeclObjects : VPIObjectSet<"TaskFunctionDeclObjects",
  [Task, Function]>;
def TaskFunctionCallObjects : VPIObjectSet<"TaskFunctionCallObjects",
  [FuncCall, TaskCall, SysFuncCall, SysTaskCall, MethodFuncCall,
   MethodTaskCall]>;
def DataVariableObjects : VPIObjectSet<"DataVariableObjects", [],
  [VariableFamily], [Parameter, SpecParam]>;
def ValueParameterObjects : VPIObjectSet<"ValueParameterObjects",
  [Parameter, SpecParam]>;
def PortObjects : VPIObjectSet<"PortObjects", [Port, PortBit]>;
def PortTargetObjects : VPIObjectSet<"PortTargetObjects", [Port]>;

// Integer and Boolean properties use the same exact object-set machinery as
// traversal edges. This keeps LRM applicability generated instead of letting
// individual VPI entry points infer it from physical database record kinds.
class VPIPropertyValueKind<string cppName_, int value_> {
  string cppName = cppName_;
  int value = value_;
}

def BooleanProperty : VPIPropertyValueKind<"Boolean", 0>;
def IntegerProperty : VPIPropertyValueKind<"Integer", 1>;

class VPIProperty<string apiName_, int value_,
                  VPIPropertyValueKind valueKind_, VPIObjectSet sources_,
                  string clause_> {
  string apiName = apiName_;
  int value = value_;
  VPIPropertyValueKind valueKind = valueKind_;
  VPIObjectSet sources = sources_;
  string clause = clause_;
}

def PortScalarProperty : VPIProperty<"vpiScalar", 17, BooleanProperty,
  PortObjects, "37.14">;
def PortVectorProperty : VPIProperty<"vpiVector", 18, BooleanProperty,
  PortObjects, "37.14">;
def PortDirectionProperty : VPIProperty<"vpiDirection", 20, IntegerProperty,
  PortObjects, "37.14">;
def PortIndexProperty : VPIProperty<"vpiPortIndex", 29, IntegerProperty,
  PortObjects, "37.14">;
def PortTypeProperty : VPIProperty<"vpiPortType", 611, IntegerProperty,
  PortObjects, "37.14">;
def SelectObjects : VPIObjectSet<"SelectObjects",
  [VarSelect, BitSelect, PartSelect, IndexedPartSelect]>;
def NetVariableObjects : VPIObjectSet<"NetVariableObjects", [],
  [NetFamily, VariableFamily]>;
def NamedEventObjects : VPIObjectSet<"NamedEventObjects",
  [NamedEvent, NamedEventArray]>;
def NamedEventArrayObjects : VPIObjectSet<"NamedEventArrayObjects",
  [NamedEventArray]>;
def ArrayContainerObjects : VPIObjectSet<"ArrayContainerObjects", [],
  [ArrayFamily]>;
def ConcreteTypespecObjects : VPIObjectSet<"ConcreteTypespecObjects",
  [LongIntTypespec, ShortRealTypespec, ByteTypespec, ShortIntTypespec,
   IntTypespec, ClassTypespec, StringTypespec, ChandleTypespec, EnumTypespec,
   IntegerTypespec, TimeTypespec, RealTypespec, StructTypespec, UnionTypespec,
   BitTypespec, LogicTypespec, ArrayTypespec, VoidTypespec,
   PackedArrayTypespec, SequenceTypespec, PropertyTypespec, EventTypespec,
   InterfaceTypespec]>;
def RangeObjects : VPIObjectSet<"RangeObjects", [Range]>;
def IndexExpressionObjects : VPIObjectSet<"IndexExpressionObjects", [],
  [ExpressionFamily]>;
def AssignmentObjects : VPIObjectSet<"AssignmentObjects",
  [Assignment, AssignStmt, Deassign, Force, Release, AliasStmt]>;
def ControlObjects : VPIObjectSet<"ControlObjects",
  [DelayControl, EventControl, RepeatControl]>;
def PatternObjects : VPIObjectSet<"PatternObjects",
  [AnyPattern, TaggedPattern, StructPattern]>;
def ConcurrentAssertionObjects : VPIObjectSet<"ConcurrentAssertionObjects",
  [Assert, Assume, Cover, Restrict]>;
def ImmediateAssertionObjects : VPIObjectSet<"ImmediateAssertionObjects",
  [ImmediateAssert, ImmediateAssume, ImmediateCover]>;
def PropertyExpressionObjects : VPIObjectSet<"PropertyExpressionObjects",
  [Operation, SequenceInst, MulticlockSequenceExpr, PropertyInst, ClockedProp,
   CaseProperty]>;
def SequenceExpressionObjects : VPIObjectSet<"SequenceExpressionObjects", [],
  [ExpressionFamily]>;
def DriverObjects : VPIObjectSet<"DriverObjects",
  [Port, Force, DelayTerm, ContAssign, ContAssignBit, PrimTerm, AssignStmt]>;
def DelayTermObjects : VPIObjectSet<"DelayTermObjects", [DelayTerm]>;
def CallbackObjects : VPIObjectSet<"CallbackObjects", [Callback]>;
def TimeQueueObjects : VPIObjectSet<"TimeQueueObjects", [TimeQueue]>;
def FrameObjects : VPIObjectSet<"FrameObjects", [Frame]>;
def ThreadObjects : VPIObjectSet<"ThreadObjects", [Thread]>;
def AttributeObjects : VPIObjectSet<"AttributeObjects", [Attribute]>;
def PropertyDeclObjects : VPIObjectSet<"PropertyDeclObjects", [PropertyDecl]>;
def PropertySpecObjects : VPIObjectSet<"PropertySpecObjects", [PropertySpec]>;
def PropertyExprTargets : VPIObjectSet<"PropertyExprTargets",
  [PropertyExpr, NamedEvent, PropertyDecl]>;
def SequenceDeclObjects : VPIObjectSet<"SequenceDeclObjects", [SequenceDecl]>;
def SeqFormalObjects : VPIObjectSet<"SeqFormalObjects", [SeqFormalDecl]>;
def PropFormalObjects : VPIObjectSet<"PropFormalObjects", [PropFormalDecl]>;
def CaseItemObjects : VPIObjectSet<"CaseItemObjects", [CaseItem]>;
def CasePropertyItemObjects : VPIObjectSet<"CasePropertyItemObjects",
  [CasePropertyItem]>;
def ClockedSeqObjects : VPIObjectSet<"ClockedSeqObjects", [ClockedSeq]>;
def GenScopeObjects : VPIObjectSet<"GenScopeObjects", [GenScope]>;
def GenScopeArrayObjects : VPIObjectSet<"GenScopeArrayObjects",
  [GenScopeArray]>;
def FunctionObjects : VPIObjectSet<"FunctionObjects", [Function]>;
def ClassDefnObjects : VPIObjectSet<"ClassDefnObjects", [ClassDefn]>;
def ClassTypespecObjects : VPIObjectSet<"ClassTypespecObjects",
  [ClassTypespec]>;
def ClassObjectObjects : VPIObjectSet<"ClassObjectObjects", [ClassObj]>;
def RefObjectObjects : VPIObjectSet<"RefObjectObjects", [RefObj]>;
def VirtualInterfaceObjects : VPIObjectSet<"VirtualInterfaceObjects",
  [VirtualInterfaceVar]>;
def InterfaceModportObjects : VPIObjectSet<"InterfaceModportObjects",
  [Interface, Modport]>;
def EmptyStatementTargets : VPIObjectSet<"EmptyStatementTargets",
  [NullStmt]>;

def InterfaceObjects : VPIObjectSet<"InterfaceObjects", [Interface]>;
def ProgramObjects : VPIObjectSet<"ProgramObjects", [Program]>;
def PackageObjects : VPIObjectSet<"PackageObjects", [Package]>;
def ModportObjects : VPIObjectSet<"ModportObjects", [Modport]>;
def IODeclObjects : VPIObjectSet<"IODeclObjects", [IODecl]>;
def InterfaceTfDeclObjects : VPIObjectSet<"InterfaceTfDeclObjects",
  [InterfaceTfDecl]>;
def ParameterObjects : VPIObjectSet<"ParameterObjects", [Parameter]>;
def TypeParameterObjects : VPIObjectSet<"TypeParameterObjects",
  [TypeParameter]>;
def ParameterIterationObjects : VPIObjectSet<"ParameterIterationObjects",
  [Parameter, TypeParameter]>;
def SpecParamObjects : VPIObjectSet<"SpecParamObjects", [SpecParam]>;
def ProcessObjects : VPIObjectSet<"ProcessObjects", [], [ProcessFamily]>;
def ContAssignObjects : VPIObjectSet<"ContAssignObjects", [ContAssign]>;
def DefParamObjects : VPIObjectSet<"DefParamObjects", [DefParam]>;
def AliasStmtObjects : VPIObjectSet<"AliasStmtObjects", [AliasStmt]>;
def ClockingBlockObjects : VPIObjectSet<"ClockingBlockObjects",
  [ClockingBlock]>;
def ClockingIODeclObjects : VPIObjectSet<"ClockingIODeclObjects",
  [ClockingIODecl]>;
def ClassVarObjects : VPIObjectSet<"ClassVarObjects", [ClassVar]>;
def ConstraintObjects : VPIObjectSet<"ConstraintObjects", [Constraint]>;
def ConstraintItemObjects : VPIObjectSet<"ConstraintItemObjects",
  [ConstraintOrdering, Implication, ConstrIf, ConstrIfElse, ConstrForEach,
   Distribution, SoftDisable]>;
def ConstraintOrderingObjects : VPIObjectSet<"ConstraintOrderingObjects",
  [ConstraintOrdering]>;
def DistItemObjects : VPIObjectSet<"DistItemObjects", [DistItem]>;
def PrimTermObjects : VPIObjectSet<"PrimTermObjects", [PrimTerm]>;
def PathTermObjects : VPIObjectSet<"PathTermObjects", [PathTerm]>;
def TchkTermObjects : VPIObjectSet<"TchkTermObjects", [TchkTerm]>;
def TchkObjects : VPIObjectSet<"TchkObjects", [Tchk]>;
def ModPathObjects : VPIObjectSet<"ModPathObjects", [ModPath]>;
def InterModPathObjects : VPIObjectSet<"InterModPathObjects", [InterModPath]>;
def DelayDeviceObjects : VPIObjectSet<"DelayDeviceObjects", [DelayDevice]>;
def PrimitiveArrayObjects : VPIObjectSet<"PrimitiveArrayObjects",
  [GateArray, SwitchArray, UdpArray]>;
def PrimitiveConcreteObjects : VPIObjectSet<"PrimitiveConcreteObjects",
  [Gate, Switch, Udp]>;
def InstanceArrayObjects : VPIObjectSet<"InstanceArrayObjects",
  [ModuleArray, InterfaceArray, ProgramArray, GateArray, SwitchArray,
   UdpArray]>;
def NetArrayObjects : VPIObjectSet<"NetArrayObjects",
  [NetArray, InterconnectArray]>;
def VariableArrayObjects : VPIObjectSet<"VariableArrayObjects",
  [RegArray, PackedArrayVar, NamedEventArray]>;
def PropertyInstObjects : VPIObjectSet<"PropertyInstObjects", [PropertyInst]>;
def SequenceInstObjects : VPIObjectSet<"SequenceInstObjects", [SequenceInst]>;
def LetDeclObjects : VPIObjectSet<"LetDeclObjects", [LetDecl]>;
def LetExprObjects : VPIObjectSet<"LetExprObjects", [LetExpr]>;
def EventStmtObjects : VPIObjectSet<"EventStmtObjects", [EventStmt]>;
def AssignmentStmtObjects : VPIObjectSet<"AssignmentStmtObjects",
  [Assignment]>;
def EventControlObjects : VPIObjectSet<"EventControlObjects", [EventControl]>;
def DelayControlObjects : VPIObjectSet<"DelayControlObjects", [DelayControl]>;
def RepeatControlObjects : VPIObjectSet<"RepeatControlObjects", [RepeatControl]>;
def WhileRepeatObjects : VPIObjectSet<"WhileRepeatObjects", [While, Repeat]>;
def WaitObjects : VPIObjectSet<"WaitObjects", [Wait]>;
def OrderedWaitObjects : VPIObjectSet<"OrderedWaitObjects", [OrderedWait]>;
def ForeverObjects : VPIObjectSet<"ForeverObjects", [Forever]>;
def IfObjects : VPIObjectSet<"IfObjects", [If, IfElse]>;
def IfElseObjects : VPIObjectSet<"IfElseObjects", [IfElse]>;
def CaseObjects : VPIObjectSet<"CaseObjects", [Case]>;
def ExpectObjects : VPIObjectSet<"ExpectObjects", [ExpectStmt]>;
def ForObjects : VPIObjectSet<"ForObjects", [For]>;
def DoWhileObjects : VPIObjectSet<"DoWhileObjects", [DoWhile]>;
def ForeachObjects : VPIObjectSet<"ForeachObjects", [ForeachStmt]>;
def DisableObjects : VPIObjectSet<"DisableObjects", [Disable]>;
def ReturnStmtObjects : VPIObjectSet<"ReturnStmtObjects", [ReturnStmt]>;
def ProceduralAssignLhsObjects : VPIObjectSet<"ProceduralAssignLhsObjects",
  [AssignStmt, Force, Deassign, Release]>;
def ProceduralAssignRhsObjects : VPIObjectSet<"ProceduralAssignRhsObjects",
  [AssignStmt, Force]>;
def AttributeOwnerObjects : VPIObjectSet<"AttributeOwnerObjects",
  [Package, Module, Interface, Program, Port, PathTerm, ModPath, Tchk,
   ParamAssign, Task, Function, TableEntry, Operation, Assert, Assume, Cover,
   Restrict, SequenceDecl, PropertyDecl, ClockingBlock, ClassDefn, Constraint],
  [NetFamily, VariableFamily, StatementFamily, ProcessFamily,
   PrimitiveFamily]>;
def UserSystfObjects : VPIObjectSet<"UserSystfObjects", [UserSystf]>;
def SysTaskCallObjects : VPIObjectSet<"SysTaskCallObjects", [SysTaskCall]>;
def SystemTfCallObjects : VPIObjectSet<"SystemTfCallObjects",
  [SysFuncCall, SysTaskCall]>;
def IteratorObjects : VPIObjectSet<"IteratorObjects", [Iterator]>;
def AnyConcreteObjects : VPIObjectSet<"AnyConcreteObjects", [],
  [ScopeFamily, DeclarationFamily, ProcessFamily, StatementFamily,
   ExpressionFamily, VariableFamily, NetFamily, ArrayFamily, TypespecFamily,
   PrimitiveFamily, TimingFamily, AssertionFamily, TransientFamily,
   RuntimeFamily, OtherFamily]>;
def InterfaceArrayObjects : VPIObjectSet<"InterfaceArrayObjects",
  [InterfaceArray]>;
def ProgramArrayObjects : VPIObjectSet<"ProgramArrayObjects", [ProgramArray]>;
def ModuleArrayObjects : VPIObjectSet<"ModuleArrayObjects", [ModuleArray]>;
def NamedEventTargetObjects : VPIObjectSet<"NamedEventTargetObjects",
  [NamedEvent]>;
def RegObjects : VPIObjectSet<"RegObjects", [Reg]>;
def RegArrayObjects : VPIObjectSet<"RegArrayObjects", [RegArray]>;
def NetTargetObjects : VPIObjectSet<"NetTargetObjects", [Net]>;
def NetArrayTargetObjects : VPIObjectSet<"NetArrayTargetObjects", [NetArray]>;
def AssertionTargetObjects : VPIObjectSet<"AssertionTargetObjects",
  [Assert, Assume, Cover, Restrict]>;
def TypedefTargetObjects : VPIObjectSet<"TypedefTargetObjects",
  [LongIntTypespec, ShortRealTypespec, ByteTypespec, ShortIntTypespec,
   IntTypespec, ClassTypespec, StringTypespec, ChandleTypespec, EnumTypespec,
   IntegerTypespec, TimeTypespec, RealTypespec, StructTypespec, UnionTypespec,
   BitTypespec, LogicTypespec, ArrayTypespec, VoidTypespec,
   PackedArrayTypespec, SequenceTypespec, PropertyTypespec, EventTypespec,
   InterfaceTypespec]>;
def NonInstanceScopeObjects : VPIObjectSet<"NonInstanceScopeObjects", [],
  [ScopeFamily], [Package, Module, Interface, Program, ForeachStmt]>;
def ScopeWithoutModuleObjects : VPIObjectSet<"ScopeWithoutModuleObjects", [],
  [ScopeFamily], [Module]>;

// Instance and scope contents (37.5-37.12).
def ModulePorts : VPITraversalEdge<ModuleSource, Port, IterateMode,
  PortTargetObjects, NoOrder, "37.5", 0, AutomaticDirectChild>;
def ModuleInterfaces : VPITraversalEdge<ModuleSource, Interface, IterateMode,
  InterfaceObjects, NoOrder, "37.5", 0, AutomaticDirectChild>;
def ModuleInterfaceArrays : VPITraversalEdge<ModuleSource, InterfaceArray,
  IterateMode, InterfaceArrayObjects, NoOrder, "37.5">;
def ModuleProcesses : VPITraversalEdge<ModuleSource, ProcessRel, IterateMode,
  StaticProcessObjects, NoOrder, "37.5", 0, AutomaticDirectChild>;
def ModuleContAssigns : VPITraversalEdge<ModuleSource, ContAssign, IterateMode,
  ContAssignObjects, NoOrder, "37.5", 1>;
def ModuleChildren : VPITraversalEdge<ModuleSource, Module, IterateMode,
  ModuleTarget, NoOrder, "37.5", 0, AutomaticDirectChild>;
def ModuleArrays : VPITraversalEdge<ModuleSource, ModuleArray, IterateMode,
  ModuleArrayObjects, NoOrder, "37.5">;
def ModulePrimitives : VPITraversalEdge<ModuleSource, PrimitiveRel,
  IterateMode, PrimitiveConcreteObjects, NoOrder, "37.5">;
def ModulePrimitiveArrays : VPITraversalEdge<ModuleSource, PrimitiveArray,
  IterateMode, PrimitiveArrayObjects, NoOrder, "37.5">;
def ModuleModPaths : VPITraversalEdge<ModuleSource, ModPath, IterateMode,
  ModPathObjects, NoOrder, "37.5">;
def ModuleTchks : VPITraversalEdge<ModuleSource, Tchk, IterateMode,
  TchkObjects, NoOrder, "37.5">;
def ModuleDefParams : VPITraversalEdge<ModuleSource, DefParam, IterateMode,
  DefParamObjects, NoOrder, "37.5">;
def ModuleIODecls : VPITraversalEdge<ModuleSource, IODecl, IterateMode,
  IODeclObjects, NoOrder, "37.5">;
def ModuleAliasStmts : VPITraversalEdge<ModuleSource, AliasStmt, IterateMode,
  AliasStmtObjects, NoOrder, "37.5", 1>;
def ModuleClockingBlocks : VPITraversalEdge<ModuleSource, ClockingBlock,
  IterateMode, ClockingBlockObjects, NoOrder, "37.5">;
def ModuleInternalScopes : VPITraversalEdge<ModuleSource, InternalScopeRel,
  IterateMode, ScopeObjects, NoOrder, "37.5", 0, AutomaticDirectChild>;
def ModuleInstanceArrays : VPITraversalEdge<ModuleSource, InstanceArrayRel,
  HandleMode, InstanceArrayObjects, NoOrder, "37.5">;

def InterfaceTfDecls : VPITraversalEdge<InterfaceObjects, InterfaceTfDecl,
  IterateMode, InterfaceTfDeclObjects, NoOrder, "37.6">;
def InterfaceModports : VPITraversalEdge<InterfaceObjects, Modport,
  IterateMode, ModportObjects, NoOrder, "37.6">;
def InterfaceModPaths : VPITraversalEdge<InterfaceObjects, ModPath,
  IterateMode, ModPathObjects, NoOrder, "37.6">;
def InterfaceContAssigns : VPITraversalEdge<InterfaceObjects, ContAssign,
  IterateMode, ContAssignObjects, NoOrder, "37.6", 1>;
def InterfaceClockingBlocks : VPITraversalEdge<InterfaceObjects,
  ClockingBlock, IterateMode, ClockingBlockObjects, NoOrder, "37.6">;
def InterfaceChildren : VPITraversalEdge<InterfaceObjects, Interface,
  IterateMode, InterfaceObjects, NoOrder, "37.6", 0,
  AutomaticDirectChild>;
def InterfaceArrays : VPITraversalEdge<InterfaceObjects, InterfaceArray,
  IterateMode, InterfaceArrayObjects, NoOrder, "37.6">;
def InterfaceProcesses : VPITraversalEdge<InterfaceObjects, ProcessRel,
  IterateMode, StaticProcessObjects, NoOrder, "37.6", 0,
  AutomaticDirectChild>;
def InterfaceInstanceArrays : VPITraversalEdge<InterfaceObjects,
  InstanceArrayRel, HandleMode, InstanceArrayObjects, NoOrder, "37.6">;

def ProgramContAssigns : VPITraversalEdge<ProgramObjects, ContAssign,
  IterateMode, ContAssignObjects, NoOrder, "37.9", 1>;
def ProgramClockingBlocks : VPITraversalEdge<ProgramObjects, ClockingBlock,
  IterateMode, ClockingBlockObjects, NoOrder, "37.9">;
def ProgramInterfaces : VPITraversalEdge<ProgramObjects, Interface,
  IterateMode, InterfaceObjects, NoOrder, "37.9", 0, AutomaticDirectChild>;
def ProgramInterfaceArrays : VPITraversalEdge<ProgramObjects, InterfaceArray,
  IterateMode, InterfaceArrayObjects, NoOrder, "37.9">;
def ProgramProcesses : VPITraversalEdge<ProgramObjects, ProcessRel,
  IterateMode, StaticProcessObjects, NoOrder, "37.9", 0,
  AutomaticDirectChild>;
def ProgramInstanceArrays : VPITraversalEdge<ProgramObjects, InstanceArrayRel,
  HandleMode, InstanceArrayObjects, NoOrder, "37.9">;

def InstanceTaskFunctions : VPITraversalEdge<InstanceObjects, TaskFuncRel,
  IterateMode, TaskFunctionDeclObjects, NoOrder, "37.10", 0,
  AutomaticDirectChild>;
def InstanceNets : VPITraversalEdge<InstanceObjects, Net, IterateMode,
  NetObjects, NoOrder, "37.10", 0, AutomaticDirectChild>;
def InstanceNetArrays : VPITraversalEdge<InstanceObjects, NetArray,
  IterateMode, NetArrayObjects, NoOrder, "37.10">;
def InstanceVariables : VPITraversalEdge<InstanceObjects, VariablesRel,
  IterateMode, DataVariableObjects, NoOrder, "37.10", 0,
  AutomaticDirectChild>;
def InstanceRegs : VPITraversalEdge<InstanceObjects, Reg, IterateMode,
  RegObjects, NoOrder, "37.10", 0, AutomaticDirectChild>;
def InstanceRegArrays : VPITraversalEdge<InstanceObjects, RegArray,
  IterateMode, RegArrayObjects, NoOrder, "37.10">;
def InstanceNamedEvents : VPITraversalEdge<InstanceObjects, NamedEvent,
  IterateMode, NamedEventTargetObjects, NoOrder, "37.10">;
def InstanceNamedEventArrays : VPITraversalEdge<InstanceObjects,
  NamedEventArray, IterateMode, NamedEventObjects, NoOrder, "37.10">;
def InstanceParameters : VPITraversalEdge<InstanceObjects, Parameter,
  IterateMode, ParameterIterationObjects, NoOrder, "37.10">;
def InstanceTypeParameters : VPITraversalEdge<InstanceObjects, TypeParameter,
  IterateMode, TypeParameterObjects, NoOrder, "37.10">;
def InstanceSpecParams : VPITraversalEdge<InstanceObjects, SpecParam,
  IterateMode, SpecParamObjects, NoOrder, "37.10">;
def InstanceAssertions : VPITraversalEdge<InstanceObjects, AssertionRel,
  IterateMode, AssertionTargetObjects, NoOrder, "37.10">;
def InstanceTypedefs : VPITraversalEdge<InstanceObjects, TypedefRel,
  IterateMode, TypedefTargetObjects, NoOrder, "37.10">;
def InstanceClassDefns : VPITraversalEdge<InstanceObjects, ClassDefn,
  IterateMode, ClassDefnObjects, NoOrder, "37.10">;

def ScopePropertyDecls : VPITraversalEdge<ScopeSource, PropertyDecl,
  IterateMode, PropertyDeclObjects, NoOrder, "37.12">;
def ScopeSequenceDecls : VPITraversalEdge<ScopeSource, SequenceDecl,
  IterateMode, SequenceDeclObjects, NoOrder, "37.12">;
def ScopeConcurrentAssertions : VPITraversalEdge<ScopeSource,
  ConcurrentAssertionRel, IterateMode, ConcurrentAssertionObjects,
  NoOrder, "37.12">;
def ScopeNamedEvents : VPITraversalEdge<NonInstanceScopeObjects, NamedEvent,
  IterateMode, NamedEventTargetObjects, NoOrder, "37.12; 37.83">;
def ScopeNamedEventArrays : VPITraversalEdge<NonInstanceScopeObjects,
  NamedEventArray, IterateMode, NamedEventObjects, NoOrder,
  "37.12; 37.83">;
def ScopeVariables : VPITraversalEdge<NonInstanceScopeObjects, VariablesRel,
  IterateMode, DataVariableObjects, NoOrder, "37.12; 37.83">;
def ScopeVirtualInterfaces : VPITraversalEdge<ScopeSource,
  VirtualInterfaceVar, IterateMode, VirtualInterfaceObjects, NoOrder,
  "37.12">;
def ScopeParameters : VPITraversalEdge<NonInstanceScopeObjects, Parameter,
  IterateMode, ParameterIterationObjects, NoOrder, "37.12; 37.83">;
def ScopeInternalScopes : VPITraversalEdge<ScopeWithoutModuleObjects,
  InternalScopeRel,
  IterateMode, ScopeObjects, NoOrder, "37.12; 37.83">;
def ScopeImports : VPITraversalEdge<ScopeSource, ImportRel, IterateMode,
  DeclarationObjects, NoOrder, "37.12">;
def ScopeTypedefs : VPITraversalEdge<NonInstanceScopeObjects, TypedefRel,
  IterateMode, TypedefTargetObjects, NoOrder, "37.12; 37.83">;
def ScopeLets : VPITraversalEdge<ScopeSource, LetDecl, IterateMode,
  LetDeclObjects, NoOrder, "37.12">;

// NULL-root relations.
def RootUserSystfs : VPITraversalEdge<RootSource, UserSystf, IterateMode,
  UserSystfObjects, NoOrder, "37.40">;
def RootCurrentSysTfCall : VPITraversalEdge<RootSource, SysTfCallRel,
  HandleMode, SystemTfCallObjects, NoOrder, "37.40">;
def RootCurrentFrame : VPITraversalEdge<RootSource, Frame, HandleMode,
  FrameObjects, NoOrder, "37.41">;
def RootCallbacks : VPITraversalEdge<RootSource, Callback, IterateMode,
  CallbackObjects, NoOrder, "37.78">;
def RootTimeQueues : VPITraversalEdge<RootSource, TimeQueue, IterateMode,
  TimeQueueObjects, TimeOrder, "37.79">;
def RootActiveTimeFormat : VPITraversalEdge<RootSource, ActiveTimeFormatRel,
  HandleMode, SysTaskCallObjects, NoOrder, "37.80">;

include "VPITraversalEarly.td"
include "VPITraversalLate.td"
