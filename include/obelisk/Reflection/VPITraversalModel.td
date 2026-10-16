//===- VPITraversalModel.td - IEEE VPI legal traversal graph ------------===//
//
// This file describes the legal source/selector/target graph from IEEE
// 1800-2023 Chapter 37. It is deliberately separate from the public spelling
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
                   bit iterateSources_ = 0,
                   bit allowEmpty_ = 0> {
  string cppName = cppName_;
  list<VPIObjectKind> objects = objects_;
  list<VPIObjectFamily> families = families_;
  list<VPIObjectKind> exclude = exclude_;
  bit nullRoot = nullRoot_;
  bit iterateSources = iterateSources_;
  bit allowEmpty = allowEmpty_;
}

// How a concrete VPI object identity may be materialized.  These are carrier
// capabilities, not implementation-status flags: a kind may have more than
// one representation when the LRM identity can arise in more than one way
// (for example a source task may be an executable code unit or an immutable
// declaration anchor).  Every canonical concrete kind is required to occur
// in at least one representation specification below.
class VPIObjectRepresentation<string cppName_, int value_> {
  string cppName = cppName_;
  int value = value_;
}

def PhysicalScopeRepresentation
    : VPIObjectRepresentation<"PhysicalScope", 0>;
def PhysicalObjectRepresentation
    : VPIObjectRepresentation<"PhysicalObject", 1>;
def CodeUnitRepresentation : VPIObjectRepresentation<"CodeUnit", 2>;
def StatementRepresentation : VPIObjectRepresentation<"Statement", 3>;
def StaticImageRepresentation : VPIObjectRepresentation<"StaticImage", 4>;
def SemanticSyntheticRepresentation
    : VPIObjectRepresentation<"SemanticSynthetic", 5>;
def RuntimeRepresentation : VPIObjectRepresentation<"Runtime", 6>;

class VPIObjectRepresentationSpec<VPIObjectRepresentation representation_,
                                  VPIObjectSet objects_> {
  VPIObjectRepresentation representation = representation_;
  VPIObjectSet objects = objects_;
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
// Reverse edge from a selected static hierarchy member to the relation-backed
// array that owns its source index. This is distinct from lexical scope: a
// module-array member's vpiModule skips the array and names the enclosing
// module, while vpiModuleArray/vpiInstanceArray name the array itself.
def AutomaticIndexedContainer :
    VPIAutomaticRelation<"IndexedContainer", 4>;
// DefinitionMember materializes definition-owned declaration topology through
// an elaborated instance's compact definition/specialization binding.  The
// reverse relation retains that instance provenance on the synthetic member
// cursor, so no per-instance declaration object is required in the image.
def AutomaticDefinitionMember :
    VPIAutomaticRelation<"DefinitionMember", 5>;
def AutomaticDefinitionMemberParent :
    VPIAutomaticRelation<"DefinitionMemberParent", 6>;
// Resolve a definition-shared declaration's instance-specific expression
// endpoint through the compact definition-binding endpoint slice.
def AutomaticDefinitionMemberExpr :
    VPIAutomaticRelation<"DefinitionMemberExpr", 7>;
// Resolve an instance-specific relation whose source is a synthetic
// definition-member cursor.  Unlike DefinitionMember, both the binding and
// member participate in the key, so two instances of one definition never
// share downstream connectivity accidentally.
def AutomaticDefinitionMemberInstanceRelation :
    VPIAutomaticRelation<"DefinitionMemberInstanceRelation", 8>;

// statementContainment marks forward structural ownership of Statement-family
// targets. Sources may be scopes, code units/processes, assertions, or other
// statements. Cross-references and reverse parent/scope edges do not set it.
class VPITraversalEdge<VPIObjectSet sources_, VPISelector selector_,
                       VPITraversalMode mode_, VPIObjectSet targets_,
                       VPITraversalOrder order_, string clause_,
                       bit statementContainment_ = 0,
                       VPIAutomaticRelation automaticRelation_ =
                           NoAutomaticRelation,
                       int inverseSelector_ = 0> {
  VPIObjectSet sources = sources_;
  VPISelector selector = selector_;
  VPITraversalMode mode = mode_;
  VPIObjectSet targets = targets_;
  VPITraversalOrder order = order_;
  string clause = clause_;
  bit statementContainment = statementContainment_;
  VPIAutomaticRelation automaticRelation = automaticRelation_;
  // Nonzero only when an automatically materialized reverse traversal also
  // owns the singular forward edge from each target back to its source.
  int inverseSelector = inverseSelector_;
}

// Access-by-index is a handle construction operation, not a traversal edge.
// In particular, vpiIndex is the reverse relation from an already-selected
// object to the expression(s) that selected it.  Keep the access mechanism
// and its exact result set explicit so the runtime only implements storage
// geometry; TableGen remains authoritative for LRM applicability.
class VPIIndexedAccessKind<string cppName_, int value_> {
  string cppName = cppName_;
  int value = value_;
}

def PortElementAccess : VPIIndexedAccessKind<"PortElement", 0>;
def NetElementAccess : VPIIndexedAccessKind<"NetElement", 1>;
def VariableElementAccess : VPIIndexedAccessKind<"VariableElement", 2>;
def RelationElementAccess : VPIIndexedAccessKind<"RelationElement", 3>;

class VPIIndexedAccess<VPIObjectSet sources_,
                       VPIIndexedAccessKind accessKind_,
                       VPIObjectSet targets_, VPIObjectKind terminalResult_,
                       VPIObjectKind unpackedFallback_,
                       VPIObjectKind packedFallback_, bit mapSemanticType_,
                       string clause_,
                       VPISelector relationSelector_ = IndexRel> {
  VPIObjectSet sources = sources_;
  VPIIndexedAccessKind accessKind = accessKind_;
  VPIObjectSet targets = targets_;
  VPIObjectKind terminalResult = terminalResult_;
  VPIObjectKind unpackedFallback = unpackedFallback_;
  VPIObjectKind packedFallback = packedFallback_;
  bit mapSemanticType = mapSemanticType_;
  string clause = clause_;
  VPISelector relationSelector = relationSelector_;
}

// Static hierarchy arrays select canonical immutable design records rather
// than slicing executable storage. Reference the existing IndexOrder edge so
// the legal source, selector, target set, and indexed policy cannot drift.
class VPIRelationIndexedAccess<VPITraversalEdge elementEdge_,
                               VPIObjectSet resultTargets_,
                               VPIObjectKind partialResult_,
                               VPIObjectKind terminalResult_,
                               string clause_>
    : VPIIndexedAccess<elementEdge_.sources, RelationElementAccess,
                       resultTargets_, terminalResult_, partialResult_,
                       terminalResult_, 0, clause_, elementEdge_.selector>;

class VPIIndexedTypeResult<VPIIndexedAccessKind accessKind_,
                           VPIObjectKind selectedTypespec_,
                           VPIObjectKind result_, string clause_> {
  VPIIndexedAccessKind accessKind = accessKind_;
  VPIObjectKind selectedTypespec = selectedTypespec_;
  VPIObjectKind result = result_;
  string clause = clause_;
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
   ForeachStmt, ReturnStmt, Break, Continue, FuncCall, MethodFuncCall,
   SysFuncCall]>;
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
def PackageSource : VPIObjectSet<"Package", [Package]>;
def ScopeSource : VPIObjectSet<"Scope", [], [ScopeFamily]>;
def ModuleTarget : VPIObjectSet<"ModuleTarget", [Module]>;

// Root discovery is the only generally legal NULL-root object iteration.
def RootModules : VPITraversalEdge<RootSource, Module, IterateMode,
  ModuleTarget, NoOrder, "37.5">;

// Reusable exact unions from the Chapter 37 diagrams.
def InstanceObjects : VPIObjectSet<"InstanceObjects",
  [Package, Module, Interface, Program]>;
// Source design units with distinct elaborated instances. Packages are not
// represented by SimVPIDefinitionDeclOp bindings and retain fixed-image
// definition metadata.
def DefinitionInstanceObjects : VPIObjectSet<"DefinitionInstanceObjects",
  [Module, Interface, Program]>;
def StaticProcessObjects : VPIObjectSet<"StaticProcessObjects",
  [Initial, Final, Always]>;
def TaskFunctionDeclObjects : VPIObjectSet<"TaskFunctionDeclObjects",
  [Task, Function]>;
def AutomaticImageObjects : VPIObjectSet<"AutomaticImageObjects",
  [Package, Module, Interface, Program, Task, Function, NamedEvent,
   NamedEventArray, Constraint]>;
def TaskFunctionCallObjects : VPIObjectSet<"TaskFunctionCallObjects",
  [FuncCall, TaskCall, SysFuncCall, SysTaskCall, MethodFuncCall,
   MethodTaskCall]>;
// 37.10 and 37.12 expose parameters and named events through separate
// relations from the data-variable inventory.
def DataVariableObjects : VPIObjectSet<"DataVariableObjects", [],
  [VariableFamily], [Parameter, SpecParam, NamedEvent, NamedEventArray]>;
def ValueParameterObjects : VPIObjectSet<"ValueParameterObjects",
  [Parameter, SpecParam]>;
def PortObjects : VPIObjectSet<"PortObjects", [Port, PortBit]>;
def PortTargetObjects : VPIObjectSet<"PortTargetObjects", [Port]>;

// Exact union of the concrete objects whose scalar/vector properties are
// defined by the port, net, and variable diagrams. Keep this explicit: the
// broad NetFamily and VariableFamily also contain objects for which those
// properties are not defined (for example named events and parameters).
// Aliased API spellings are represented by their canonical object kinds.
def ScalarVectorObjects : VPIObjectSet<"ScalarVectorObjects",
  [IODecl, Port, PortBit,
   Net, NetBit, NetArray, EnumNet, IntegerNet, TimeNet, UnionNet,
   ShortRealNet, RealNet, ByteNet, ShortIntNet, IntNet, LongIntNet, BitNet,
   InterconnectNet, InterconnectArray, StructNet, PackedArrayNet,
   ShortRealVar, RealVar, ByteVar, ShortIntVar, IntVar, LongIntVar,
   IntegerVar, TimeVar, RegArray, PackedArrayVar, BitVar, Reg, StructVar,
   UnionVar, EnumVar, StringVar, ChandleVar, ClassVar, VirtualInterfaceVar,
   RegBit]>;
def VectorPropertyObjects : VPIObjectSet<"VectorPropertyObjects",
  [IODecl, Port, PortBit,
   Net, NetBit, NetArray, EnumNet, IntegerNet, TimeNet, UnionNet,
   ShortRealNet, RealNet, ByteNet, ShortIntNet, IntNet, LongIntNet, BitNet,
   InterconnectNet, InterconnectArray, StructNet, PackedArrayNet,
   ShortRealVar, RealVar, ByteVar, ShortIntVar, IntVar, LongIntVar,
   IntegerVar, TimeVar, RegArray, PackedArrayVar, BitVar, Reg, StructVar,
   UnionVar, EnumVar, StringVar, ChandleVar, ClassVar, VirtualInterfaceVar,
   RegBit, BitTypespec, LogicTypespec, PackedArrayTypespec]>;

// Integer and Boolean properties use the same exact object-set machinery as
// traversal edges. This keeps LRM applicability generated instead of letting
// individual VPI entry points infer it from physical database record kinds.
class VPIPropertyValueKind<string cppName_, int value_> {
  string cppName = cppName_;
  int value = value_;
}

def BooleanProperty : VPIPropertyValueKind<"Boolean", 0>;
def IntegerProperty : VPIPropertyValueKind<"Integer", 1>;
def Int64Property : VPIPropertyValueKind<"Int64", 2>;
def StringProperty : VPIPropertyValueKind<"String", 3>;
def NoPropertyObjects : VPIObjectSet<"NoPropertyObjects", [], [], [], 0, 0,
  1>;

// Whether the answer can be derived exclusively from immutable elaboration
// metadata, or must be sampled from current simulation/handle state.  This is
// query metadata only and never creates scheduler work.
class VPIPropertyStability<string cppName_, int value_> {
  string cppName = cppName_;
  int value = value_;
}
def StaticProperty : VPIPropertyStability<"Static", 0>;
def DynamicProperty : VPIPropertyStability<"Dynamic", 1>;

// IEEE 1800-2023 37.3.6 generally permits only vpiType and vpiIsProtected on
// a protected object. Section 37.59 additionally permits vpiSize for protected
// expression objects; that source-specific exception is modeled below.
class VPIPropertyProtectedAccess<string cppName_, int value_> {
  string cppName = cppName_;
  int value = value_;
}
def DeniedWhenProtected : VPIPropertyProtectedAccess<"Denied", 0>;
def AllowedWhenProtected : VPIPropertyProtectedAccess<"Allowed", 1>;

class VPIPropertyRealization<string cppName_, int value_> {
  string cppName = cppName_;
  int value = value_;
}
def DerivedProperty : VPIPropertyRealization<"Derived", 0>;
def FixedImageProperty : VPIPropertyRealization<"FixedImage", 1>;
def RuntimeProperty : VPIPropertyRealization<"Runtime", 2>;
// Immutable properties whose value can vary across a packed physical object.
// The design image stores canonical bit runs and the query resolves a whole
// handle or selection only when every selected bit has one exact value.
def IndexedImageProperty : VPIPropertyRealization<"IndexedImage", 3>;
// Immutable metadata stored once on a source definition and reached through
// the elaborated instance's compact definition binding.
def DefinitionImageProperty : VPIPropertyRealization<"DefinitionImage", 4>;
// A source declaration may preserve an exact immutable fact even where older
// images or live views derive it. Absence falls through to the derived query;
// explicit false remains distinct from unavailable source information.
def OptionalImageProperty : VPIPropertyRealization<"OptionalImage", 5>;

class VPIProperty<string apiName_, int value_,
                  VPIPropertyValueKind valueKind_, VPIObjectSet sources_,
                  string clause_,
                  VPIPropertyStability stability_ = StaticProperty,
                  VPIPropertyProtectedAccess protectedAccess_ =
                      DeniedWhenProtected,
                  bit symbolicString_ = 0,
                  VPIObjectSet dynamicSources_ = NoPropertyObjects,
                  VPIObjectSet protectedSources_ = NoPropertyObjects,
                  VPIPropertyRealization realization_ = DerivedProperty,
                  VPIObjectSet fixedImageSources_ = NoPropertyObjects,
                  VPIObjectSet runtimeSources_ = NoPropertyObjects,
                  VPIObjectSet definitionImageSources_ = NoPropertyObjects> {
  string apiName = apiName_;
  int value = value_;
  VPIPropertyValueKind valueKind = valueKind_;
  VPIObjectSet sources = sources_;
  string clause = clause_;
  VPIPropertyStability stability = stability_;
  VPIPropertyProtectedAccess protectedAccess = protectedAccess_;
  bit symbolicString = symbolicString_;
  VPIObjectSet dynamicSources = dynamicSources_;
  VPIObjectSet protectedSources = protectedSources_;
  VPIPropertyRealization realization = realization_;
  VPIObjectSet fixedImageSources = fixedImageSources_;
  VPIObjectSet runtimeSources = runtimeSources_;
  VPIObjectSet definitionImageSources = definitionImageSources_;
}

// Allowed domains for immutable integer properties. Symbolic rows additionally
// carry the spelling returned by vpi_get_str; a numeric domain is not required
// to be symbolic (for example, vpiChargeStrength).
class VPIIntegerPropertyValue<VPIProperty property_, int value_,
                              string symbolicName_ = ""> {
  VPIProperty property = property_;
  int value = value_;
  string symbolicName = symbolicName_;
}
class VPISymbolicPropertyValue<VPIProperty property_, int value_,
                               string apiName_>
    : VPIIntegerPropertyValue<property_, value_, apiName_>;

// Properties that apply independently of the per-object data-model diagrams.
// AllConcreteObjects deliberately names every family instead of duplicating
// the object inventory. Expansion retains only canonical concrete kinds.
def AllConcreteObjects : VPIObjectSet<"AllConcreteObjects", [],
  [ScopeFamily, DeclarationFamily, ProcessFamily, StatementFamily,
   ExpressionFamily, VariableFamily, NetFamily, ArrayFamily, TypespecFamily,
   PrimitiveFamily, TimingFamily, AssertionFamily, TransientFamily,
   RuntimeFamily, OtherFamily]>;

// Exact carrier classification for the complete canonical object inventory.
// Keep these independent from property realization: a vpiName may be derived
// while the object identity itself lives in a static record, for example.
def PhysicalScopeRepresentationObjects
    : VPIObjectSet<"PhysicalScopeRepresentationObjects",
                   [Module, Interface, Program]>;
def PhysicalObjectRepresentationObjects
    : VPIObjectSet<"PhysicalObjectRepresentationObjects", [Port],
                   [VariableFamily, NetFamily],
                   [Parameter, SpecParam, NamedEvent, NamedEventArray,
                    NetBit, RegBit, InterconnectArray]>;
def CodeUnitRepresentationObjects
    : VPIObjectSet<"CodeUnitRepresentationObjects",
                   [Always, Initial, Final, Task, Function]>;
def StatementRepresentationObjects
    : VPIObjectSet<"StatementRepresentationObjects", [], [StatementFamily]>;
def StaticImageRepresentationObjects
    : VPIObjectSet<"StaticImageRepresentationObjects",
                   [Package, ClockingBlock, GenScope, ModuleArray,
                    InterfaceArray, ProgramArray, GenScopeArray, NamedEvent,
                    NamedEventArray, InterconnectArray],
                   [DeclarationFamily, ExpressionFamily, TypespecFamily,
                    PrimitiveFamily, TimingFamily, AssertionFamily],
                   [Port, InterModPath, ImmediateAssert, ImmediateAssume,
                    ImmediateCover, ExpectStmt]>;
def SemanticSyntheticRepresentationObjects
    : VPIObjectSet<"SemanticSyntheticRepresentationObjects",
                   [Range, Port, PortBit, Constant, EnumConst, ModuleArray,
                    InterfaceArray, ProgramArray, GateArray, SwitchArray,
                    UdpArray, GenScopeArray, InterModPath],
                   [VariableFamily, NetFamily, TypespecFamily],
                   [Parameter, SpecParam, NamedEvent]>;
def RuntimeRepresentationObjects
    : VPIObjectSet<"RuntimeRepresentationObjects",
                   [Callback, Iterator, SchedEvent, TimeQueue, UserSystf,
                    Frame, Thread, ClassObj, InterModPath, Fsm, FsmHandle,
                    Task, Function, Constraint],
                   [ExpressionFamily, VariableFamily, NetFamily]>;

def PhysicalScopeRepresentationSpec
    : VPIObjectRepresentationSpec<PhysicalScopeRepresentation,
                                  PhysicalScopeRepresentationObjects>;
def PhysicalObjectRepresentationSpec
    : VPIObjectRepresentationSpec<PhysicalObjectRepresentation,
                                  PhysicalObjectRepresentationObjects>;
def CodeUnitRepresentationSpec
    : VPIObjectRepresentationSpec<CodeUnitRepresentation,
                                  CodeUnitRepresentationObjects>;
def StatementRepresentationSpec
    : VPIObjectRepresentationSpec<StatementRepresentation,
                                  StatementRepresentationObjects>;
def StaticImageRepresentationSpec
    : VPIObjectRepresentationSpec<StaticImageRepresentation,
                                  StaticImageRepresentationObjects>;
def SemanticSyntheticRepresentationSpec
    : VPIObjectRepresentationSpec<SemanticSyntheticRepresentation,
                                  SemanticSyntheticRepresentationObjects>;
def RuntimeRepresentationSpec
    : VPIObjectRepresentationSpec<RuntimeRepresentation,
                                  RuntimeRepresentationObjects>;
def SourceLocatedObjects : VPIObjectSet<"SourceLocatedObjects", [],
  [ScopeFamily, DeclarationFamily, ProcessFamily, StatementFamily,
   ExpressionFamily, VariableFamily, NetFamily, ArrayFamily, TypespecFamily,
   PrimitiveFamily, TimingFamily, AssertionFamily],
  [DelayTerm, DelayDevice, InterModPath, GenScopeArray, GenScope, Thread]>;
def InstanceAndRootObjects : VPIObjectSet<"InstanceAndRootObjects",
  [Package, Module, Interface, Program], [], [], 1>;
def ClassIdentityObjects : VPIObjectSet<"ClassIdentityObjects",
  [ClassVar, ClassObj]>;
def DirectionPropertyObjects : VPIObjectSet<"DirectionPropertyObjects",
  [IODecl, Port, PortBit, PrimTerm, PathTerm, ClockingIODecl,
   PropFormalDecl, SeqFormalDecl]>;
def PortIndexPropertyObjects : VPIObjectSet<"PortIndexPropertyObjects",
  [Port]>;

def TypeProperty : VPIProperty<"vpiType", 1, IntegerProperty,
  AllConcreteObjects, "37.3.2", StaticProperty, AllowedWhenProtected, 1>;
def FileProperty : VPIProperty<"vpiFile", 5, StringProperty,
  SourceLocatedObjects, "37.3.3">;
def LineNoProperty : VPIProperty<"vpiLineNo", 6, IntegerProperty,
  SourceLocatedObjects, "37.3.3">;
def TimeUnitProperty : VPIProperty<"vpiTimeUnit", 11, IntegerProperty,
  InstanceAndRootObjects, "37.10; 38.6">;
def TimePrecisionProperty : VPIProperty<"vpiTimePrecision", 12,
  IntegerProperty, InstanceAndRootObjects, "37.10; 38.6">;
def IsProtectedProperty : VPIProperty<"vpiIsProtected", 74, BooleanProperty,
  AllConcreteObjects, "37.3.6", StaticProperty, AllowedWhenProtected, 0,
  NoPropertyObjects, NoPropertyObjects, FixedImageProperty>;
def ObjIdProperty : VPIProperty<"vpiObjId", 660, Int64Property,
  ClassIdentityObjects, "37.33", DynamicProperty, DeniedWhenProtected, 0,
  NoPropertyObjects, NoPropertyObjects, RuntimeProperty>;

def ScalarProperty : VPIProperty<"vpiScalar", 17, BooleanProperty,
  ScalarVectorObjects, "37.14; 37.16; 37.17">;
def VectorProperty : VPIProperty<"vpiVector", 18, BooleanProperty,
  VectorPropertyObjects, "37.14; 37.16; 37.17; 37.25">;
def PortDirectionProperty : VPIProperty<"vpiDirection", 20, IntegerProperty,
  DirectionPropertyObjects,
  "37.13; 37.14; 37.35; 37.39; 37.48; 37.51; 37.53",
  StaticProperty, DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects,
  DerivedProperty, VPIObjectSet<"PrimitiveTerminalDirectionObjects", [PrimTerm]>>;
def PortIndexProperty : VPIProperty<"vpiPortIndex", 29, IntegerProperty,
  PortIndexPropertyObjects, "37.14">;
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
// Exact union of the concrete objects for which Chapter 37 defines vpiSize.
// Keep this explicit: neither ExpressionFamily nor VariableFamily is a legal
// approximation (for example statements and named events have no vpiSize).
def StructuralSizeObjects : VPIObjectSet<"StructuralSizeObjects",
  [IODecl, Port, PortBit,
   ModuleArray, InterfaceArray, ProgramArray, GateArray, SwitchArray, UdpArray,
   Net, NetBit, NetArray, EnumNet, IntegerNet, TimeNet, UnionNet,
   ShortRealNet, RealNet, ByteNet, ShortIntNet, IntNet, LongIntNet, BitNet,
   InterconnectNet, InterconnectArray, StructNet, PackedArrayNet,
   IntegerVar, RealVar, Reg, RegBit, TimeVar, LongIntVar, ShortIntVar, IntVar,
   ShortRealVar, ByteVar, ClassVar, StringVar, EnumVar, StructVar, UnionVar,
   BitVar, ChandleVar, PackedArrayVar, VirtualInterfaceVar, RegArray,
   Parameter, SpecParam,
   RefObj, VarSelect, BitSelect, IndexedPartSelect, PartSelect, Operation,
   Constant, FuncCall, MethodFuncCall, SysFuncCall, LetExpr,
   Range, Gate, Switch, Udp, UdpDefn, TableEntry, Function, GenScopeArray]>;
def DynamicSizePropertyObjects : VPIObjectSet<"DynamicSizePropertyObjects",
  [RegArray, StringVar, RefObj, VarSelect, Operation, FuncCall,
   MethodFuncCall, SysFuncCall, LetExpr]>;
def ProtectedExpressionSizeObjects : VPIObjectSet<
  "ProtectedExpressionSizeObjects",
  [Parameter, SpecParam,
   ShortRealVar, RealVar, ByteVar, ShortIntVar, IntVar, LongIntVar,
   IntegerVar, TimeVar, RegArray, PackedArrayVar, BitVar, Reg, StructVar,
   UnionVar, EnumVar, StringVar, ChandleVar, ClassVar, VirtualInterfaceVar,
   RegBit,
   RefObj, VarSelect, BitSelect, IndexedPartSelect, PartSelect, Operation,
   Constant, FuncCall, MethodFuncCall, SysFuncCall, LetExpr], [NetFamily]>;
def DynamicAllocSchemeObjects : VPIObjectSet<"DynamicAllocSchemeObjects",
  [RefObj, VarSelect, BitSelect, PartSelect, IndexedPartSelect,
   FuncCall, TaskCall, SysFuncCall, SysTaskCall, MethodFuncCall,
   MethodTaskCall], [VariableFamily, NetFamily, TransientFamily]>;
def PackedPropertyObjects : VPIObjectSet<"PackedPropertyObjects",
  [PackedArrayVar, StructVar, UnionVar, EnumVar, StructTypespec,
   UnionTypespec, InterconnectArray]>;
def TaggedTypespecObjects : VPIObjectSet<"TaggedTypespecObjects",
  [UnionTypespec]>;
def ArrayTypespecObjects : VPIObjectSet<"ArrayTypespecObjects",
  [ArrayTypespec]>;
def ArrayTypePropertyObjects : VPIObjectSet<"ArrayTypePropertyObjects",
  [RegArray, ArrayTypespec]>;
def OrdinaryVariablePropertyObjects : VPIObjectSet<
  "OrdinaryVariablePropertyObjects",
  [ShortRealVar, RealVar, ByteVar, ShortIntVar, IntVar, LongIntVar,
   IntegerVar, TimeVar, RegArray, PackedArrayVar, BitVar, Reg, StructVar,
   UnionVar, EnumVar, StringVar, ChandleVar, ClassVar, VirtualInterfaceVar,
   RegBit]>;
def RandomizationTypeObjects : VPIObjectSet<"RandomizationTypeObjects",
  [TypespecMember, ShortRealVar, RealVar, ByteVar, ShortIntVar, IntVar,
   LongIntVar, IntegerVar, TimeVar, RegArray, PackedArrayVar, BitVar, Reg,
   StructVar, UnionVar, EnumVar, StringVar, ChandleVar, ClassVar,
   VirtualInterfaceVar, RegBit]>;
def ConstantTypePropertyObjects : VPIObjectSet<"ConstantTypePropertyObjects",
  [Constant, Parameter]>;
def IndexedValueObjects : VPIObjectSet<"IndexedValueObjects",
  [ShortRealVar, RealVar, ByteVar, ShortIntVar, IntVar, LongIntVar,
   IntegerVar, TimeVar, RegArray, PackedArrayVar, BitVar, Reg, StructVar,
   UnionVar, EnumVar, StringVar, ChandleVar, ClassVar, VirtualInterfaceVar,
   RegBit, Net, NetBit, NetArray, EnumNet, IntegerNet, TimeNet, UnionNet,
   ShortRealNet, RealNet, ByteNet, ShortIntNet, IntNet, LongIntNet, BitNet,
   InterconnectNet, InterconnectArray, StructNet, PackedArrayNet]>;
def ConstantSelectPropertyObjects : VPIObjectSet<
  "ConstantSelectPropertyObjects",
  [RefObj, Parameter, SpecParam, VarSelect, BitSelect, PartSelect,
   IndexedPartSelect, ShortRealVar, RealVar, ByteVar, ShortIntVar, IntVar,
   LongIntVar, IntegerVar, TimeVar, RegArray, PackedArrayVar, BitVar, Reg,
   StructVar, UnionVar, EnumVar, StringVar, ChandleVar, ClassVar,
   VirtualInterfaceVar, RegBit], [NetFamily]>;
def SignedPropertyObjects : VPIObjectSet<"SignedPropertyObjects",
  [IODecl, Function, Parameter, SpecParam, ShortRealVar, RealVar, ByteVar,
   ShortIntVar, IntVar, LongIntVar, IntegerVar, TimeVar, RegArray,
   PackedArrayVar, BitVar, Reg, StructVar, UnionVar, EnumVar, StringVar,
   ChandleVar, ClassVar, VirtualInterfaceVar, RegBit,
   RefObj, VarSelect, BitSelect, PartSelect, IndexedPartSelect, Operation,
   Constant, FuncCall, MethodFuncCall, SysFuncCall, LetExpr], [NetFamily]>;
def PackedArrayMemberPropertyObjects : VPIObjectSet<
  "PackedArrayMemberPropertyObjects",
  [EnumNet, StructNet, PackedArrayNet, StructVar, UnionVar, EnumVar,
   PackedArrayVar]>;
def StructUnionMemberPropertyObjects : VPIObjectSet<
  "StructUnionMemberPropertyObjects",
  [ShortRealVar, RealVar, ByteVar, ShortIntVar, IntVar, LongIntVar,
   IntegerVar, TimeVar, RegArray, PackedArrayVar, BitVar, Reg, StructVar,
   UnionVar, EnumVar, StringVar, ChandleVar, ClassVar, VirtualInterfaceVar,
   RegBit], [NetFamily]>;
def ArrayMemberObjects : VPIObjectSet<"ArrayMemberObjects",
  [Package, Module, Interface, Program, Gate, Switch, Udp, NamedEvent, GenScope,
   ShortRealVar, RealVar, ByteVar, ShortIntVar, IntVar, LongIntVar,
   IntegerVar, TimeVar, RegArray, PackedArrayVar, BitVar, Reg, StructVar,
   UnionVar, EnumVar, StringVar, ChandleVar, ClassVar, VirtualInterfaceVar,
   RegBit, Net, NetBit, NetArray, EnumNet, IntegerNet, TimeNet, UnionNet,
   ShortRealNet, RealNet, ByteNet, ShortIntNet, IntNet, LongIntNet, BitNet,
   InterconnectNet, InterconnectArray, StructNet, PackedArrayNet]>;

def StructuralSizeProperty : VPIProperty<"vpiSize", 4, IntegerProperty,
  StructuralSizeObjects,
  "37.11; 37.13; 37.14; 37.16; 37.17; 37.18; 37.19; 37.22; 37.28; "
  "37.35; 37.36; 37.41; 37.59; 37.85", StaticProperty,
  DeniedWhenProtected, 0, DynamicSizePropertyObjects,
  ProtectedExpressionSizeObjects, DerivedProperty, NoPropertyObjects,
  DynamicSizePropertyObjects>;
def StructuralPackedProperty : VPIProperty<"vpiPacked", 630,
  BooleanProperty, PackedPropertyObjects, "37.18; 37.24; 37.25; 37.26">;
def StructuralTaggedProperty : VPIProperty<"vpiTagged", 632,
  BooleanProperty, TaggedTypespecObjects, "37.25">;
def StructuralArrayTypeProperty : VPIProperty<"vpiArrayType", 606,
  IntegerProperty, ArrayTypePropertyObjects, "37.17; 37.25">;
def StructuralRandTypeProperty : VPIProperty<"vpiRandType", 610,
  IntegerProperty, RandomizationTypeObjects, "37.17; 37.25", StaticProperty,
  DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects,
  OptionalImageProperty>;
def StructuralConstTypeProperty : VPIProperty<"vpiConstType", 40,
  IntegerProperty, ConstantTypePropertyObjects, "37.28; 37.59",
  StaticProperty, DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects,
  DerivedProperty, VPIObjectSet<"SourceConstantProperties", [Constant]>>;
def IndexedArrayMemberProperty : VPIProperty<"vpiArrayMember", 607,
  BooleanProperty, ArrayMemberObjects,
  "37.5; 37.6; 37.9; 37.16; 37.17; 37.27; 37.35; 37.85">;
def IndexedPackedArrayMemberProperty : VPIProperty<"vpiPackedArrayMember", 655,
  BooleanProperty, PackedArrayMemberPropertyObjects, "37.16; 37.17; 37.18">;
def IndexedConstantSelectProperty : VPIProperty<"vpiConstantSelect", 53,
  BooleanProperty, ConstantSelectPropertyObjects,
  "37.16; 37.17; 37.18; 37.19; 37.59">;
def IndexedSignedProperty : VPIProperty<"vpiSigned", 65,
  BooleanProperty, SignedPropertyObjects,
  "37.13; 37.16; 37.17; 37.28; 37.41; 37.59">;

// Complete IEEE 1800-2023 read-property inventory. These sets follow the
// object boxes in Chapter 37.  A property may legally return a null string or
// a false/default value; applicability is independent of that value.
def InstanceArrayPropertyObjects : VPIObjectSet<
  "InstanceArrayPropertyObjects",
  [ModuleArray, InterfaceArray, ProgramArray, GateArray, SwitchArray,
   UdpArray]>;
def NamedPropertyObjects : VPIObjectSet<"NamedPropertyObjects",
  [Package, Module, Interface, Program,
   ModuleArray, InterfaceArray, ProgramArray, GateArray, SwitchArray, UdpArray,
   Function, Task, GenScope,
   ClockingBlock, ClassDefn, Modport, IODecl, Port, RefObj, VarSelect,
   BitSelect, Gate, Switch, Udp, Constraint, ClockingIODecl, PropFormalDecl,
   SeqFormalDecl, LetDecl, AnyPattern, TaggedPattern, StructPattern,
   Attribute, GenScopeArray, GenVar, SysFuncCall, Assert, Assume,
   Cover, Restrict, PropertyDecl, PropertyInst, SequenceDecl, SequenceInst,
   ImmediateAssert, ImmediateAssume, ImmediateCover, AssignStmt, Assignment,
   Begin, Case, Deassign, DelayControl, Disable, EventControl, EventStmt, For,
   Force, Forever, Fork, If, IfElse, NamedBegin, NamedFork, NullStmt, Release,
   Repeat, RepeatControl, SysTaskCall, TaskCall, Wait, While, MethodTaskCall,
   DoWhile, OrderedWait, WaitFork, DisableFork, ExpectStmt, ForeachStmt,
   ReturnStmt, Break, Continue, EnumConst, NettypeDecl],
  [VariableFamily, NetFamily, TypespecFamily]>;
def FullNamePropertyObjects : VPIObjectSet<"FullNamePropertyObjects",
  [ModuleArray, InterfaceArray, ProgramArray, GateArray, SwitchArray, UdpArray,
   RefObj, VarSelect, BitSelect, Constraint, GenScopeArray, Gate, Switch, Udp,
   GenVar, TypeParameter,
   Assert, Assume, Cover, Restrict, PropertyDecl, SequenceDecl],
  [ScopeFamily, VariableFamily, NetFamily],
  [ClassTypespec]>;
def DefinitionLocationObjects : VPIObjectSet<"DefinitionLocationObjects",
  [Package, Module, Interface, Program, Attribute]>;
def DefinitionNameObjects : VPIObjectSet<"DefinitionNameObjects",
  [Package, Module, Interface, Program, RefObj, InterfaceTypespec,
   Gate, Switch, Udp, UdpDefn]>;
def ProtectedLegacyObjects : VPIObjectSet<"ProtectedLegacyObjects",
  [Package, Module, Interface, Program, UdpDefn, GenScope]>;
def AutomaticPropertyObjects : VPIObjectSet<"AutomaticPropertyObjects",
  [Package, Module, Interface, Program, ClassDefn, ClassTypespec, Constraint,
   Task, Function], [VariableFamily], [Parameter, SpecParam]>;
def NetPropertyObjects : VPIObjectSet<"NetPropertyObjects", [], [NetFamily]>;
def PrimitivePropertyObjects : VPIObjectSet<"PrimitivePropertyObjects",
  [Gate, Switch, Udp]>;
def PrimitiveAndContinuousObjects : VPIObjectSet<
  "PrimitiveAndContinuousObjects",
  [Gate, Switch, Udp, ContAssign, ContAssignBit]>;
def OperationPropertyObjects : VPIObjectSet<"OperationPropertyObjects",
  [Operation, Assignment]>;
def DecompilePropertyObjects : VPIObjectSet<"DecompilePropertyObjects",
  [Parameter, SpecParam,
   ShortRealVar, RealVar, ByteVar, ShortIntVar, IntVar, LongIntVar,
   IntegerVar, TimeVar, RegArray, PackedArrayVar, BitVar, Reg, StructVar,
   UnionVar, EnumVar, StringVar, ChandleVar, ClassVar, VirtualInterfaceVar,
   RegBit,
   RefObj, VarSelect, BitSelect, PartSelect, IndexedPartSelect, Operation,
   Constant, FuncCall, MethodFuncCall, SysFuncCall, LetExpr,
   TaskCall, SysTaskCall, MethodTaskCall], [NetFamily]>;
def ValidPropertyObjects : VPIObjectSet<"ValidPropertyObjects", [Frame],
  [VariableFamily]>;
def DynamicPrefixPropertyObjects : VPIObjectSet<
  "DynamicPrefixPropertyObjects",
  [RefObj, Parameter, SpecParam, VarSelect, BitSelect, PartSelect,
   IndexedPartSelect, FuncCall, TaskCall, SysFuncCall, SysTaskCall,
   MethodFuncCall, MethodTaskCall, NamedEvent, NamedEventArray, ShortRealVar,
   RealVar, ByteVar, ShortIntVar, IntVar, LongIntVar, IntegerVar, TimeVar,
   RegArray, PackedArrayVar, BitVar, Reg, StructVar, UnionVar, EnumVar,
   StringVar, ChandleVar, ClassVar, VirtualInterfaceVar, RegBit],
  [NetFamily]>;
def ConcurrentAssertionPropertyObjects : VPIObjectSet<
  "ConcurrentAssertionPropertyObjects", [Assert, Assume, Cover, Restrict]>;
def ImmediateAssertionPropertyObjects : VPIObjectSet<
  "ImmediateAssertionPropertyObjects",
  [ImmediateAssert, ImmediateAssume, ImmediateCover]>;
def AssertionLocationPropertyObjects : VPIObjectSet<
  "AssertionLocationPropertyObjects",
  [SequenceInst, Assert, Assume, Cover, Restrict, PropertyInst,
   ImmediateAssert, ImmediateAssume, ImmediateCover]>;
def ConstraintExpressionPropertyObjects : VPIObjectSet<
  "ConstraintExpressionPropertyObjects",
  [Implication, ConstrIf, ConstrIfElse, ConstrForEach, Distribution,
   SoftDisable, RefObj, VarSelect, BitSelect, IndexedPartSelect, PartSelect,
   Operation, Constant, FuncCall, MethodFuncCall, SysFuncCall, LetExpr],
  [NetFamily, VariableFamily],
  [Parameter, SpecParam, NamedEvent, NamedEventArray, VirtualInterfaceVar]>;
def NullOnlyPropertyObjects : VPIObjectSet<"NullOnlyPropertyObjects", [], [],
  [], 1>;
def PortAndParamAssignObjects : VPIObjectSet<"PortAndParamAssignObjects",
  [Port, PortBit, ParamAssign]>;
def NetAndGenScopeObjects : VPIObjectSet<"NetAndGenScopeObjects", [GenScope],
  [NetFamily]>;
def ReadPrimTermObjects : VPIObjectSet<"ReadPrimTermObjects", [PrimTerm]>;
def StrengthPropertyObjects : VPIObjectSet<"StrengthPropertyObjects",
  [Gate, Switch, Udp, ContAssign, ContAssignBit], [NetFamily]>;
def StaticDriverStrengthObjects : VPIObjectSet<"StaticDriverStrengthObjects",
  [Gate, ContAssign], [NetFamily]>;
def PrimitiveDefinitionObjects : VPIObjectSet<"PrimitiveDefinitionObjects",
  [Gate, Switch, Udp, UdpDefn]>;
def ReadModPathObjects : VPIObjectSet<"ReadModPathObjects", [ModPath]>;
def EdgePropertyObjects : VPIObjectSet<"EdgePropertyObjects",
  [PathTerm, TchkTerm]>;
def TimingCheckObjects : VPIObjectSet<"TimingCheckObjects", [Tchk]>;
def BlockingPropertyObjects : VPIObjectSet<"BlockingPropertyObjects",
  [EventStmt, Assignment]>;
def ReadCaseObjects : VPIObjectSet<"ReadCaseObjects", [Case]>;
def NetAndContinuousObjects : VPIObjectSet<"NetAndContinuousObjects",
  [ContAssign, ContAssignBit], [NetFamily]>;
def FunctionPropertyObjects : VPIObjectSet<"FunctionPropertyObjects",
  [Function, FuncCall, SysFuncCall]>;
def SystemCallObjects : VPIObjectSet<"SystemCallObjects",
  [SysFuncCall, SysTaskCall, MethodFuncCall, MethodTaskCall]>;
def ScheduledEventObjects : VPIObjectSet<"ScheduledEventObjects",
  [SchedEvent]>;
def ActivePropertyObjects : VPIObjectSet<"ActivePropertyObjects",
  [Frame, Thread]>;
def ReadAttributeObjects : VPIObjectSet<"ReadAttributeObjects", [Attribute]>;
def DelayTypePropertyObjects : VPIObjectSet<"DelayTypePropertyObjects",
  [DelayTerm, DelayDevice]>;
def ReadIteratorObjects : VPIObjectSet<"ReadIteratorObjects", [Iterator]>;
def ContinuousAssignmentObjects : VPIObjectSet<
  "ContinuousAssignmentObjects", [ContAssign, ContAssignBit]>;
def ReadParameterObjects : VPIObjectSet<"ReadParameterObjects",
  [Parameter, TypeParameter]>;
def ReadValueParameterObjects : VPIObjectSet<"ReadValueParameterObjects",
  [Parameter]>;
def IndexedPartSelectObjects : VPIObjectSet<"IndexedPartSelectObjects",
  [IndexedPartSelect]>;
def ReadRegArrayObjects : VPIObjectSet<"ReadRegArrayObjects", [RegArray]>;
def ForkObjects : VPIObjectSet<"ForkObjects", [Fork, NamedFork]>;
def AccessTypePropertyObjects : VPIObjectSet<"AccessTypePropertyObjects",
  [InterfaceTfDecl, Task, Function, Constraint]>;
def ReadForObjects : VPIObjectSet<"ReadForObjects", [For]>;
def VisibilityPropertyObjects : VPIObjectSet<"VisibilityPropertyObjects",
  [Task, Function, ShortRealVar, RealVar, ByteVar, ShortIntVar, IntVar,
   LongIntVar, IntegerVar, TimeVar, RegArray, PackedArrayVar, BitVar, Reg,
   StructVar, UnionVar, EnumVar, StringVar, ChandleVar, ClassVar,
   VirtualInterfaceVar, RegBit]>;
def AlwaysObjects : VPIObjectSet<"AlwaysObjects", [Always]>;
def DistributionItemObjects : VPIObjectSet<"DistributionItemObjects",
  [DistItem]>;
def VirtualPropertyObjects : VPIObjectSet<"VirtualPropertyObjects",
  [ClassDefn, Constraint, Task, Function]>;
def ReadConstraintObjects : VPIObjectSet<"ReadConstraintObjects", [Constraint]>;
def QualifierPropertyObjects : VPIObjectSet<"QualifierPropertyObjects",
  [If, IfElse, Case]>;
def ClockingPropertyObjects : VPIObjectSet<"ClockingPropertyObjects",
  [ClockingBlock, ClockingIODecl]>;
def ReferenceObjects : VPIObjectSet<"ReferenceObjects", [RefObj]>;
def PropertySpecificationObjects : VPIObjectSet<
  "PropertySpecificationObjects", [Operation]>;
def CoverObjects : VPIObjectSet<"CoverObjects", [Cover]>;
def ReadClassTypespecObjects : VPIObjectSet<"ReadClassTypespecObjects",
  [ClassTypespec]>;
def InterfaceTypePropertyObjects : VPIObjectSet<
  "InterfaceTypePropertyObjects", [VirtualInterfaceVar, InterfaceTypespec]>;

def NameProperty : VPIProperty<"vpiName", 2, StringProperty,
  NamedPropertyObjects, "37.4-37.85", StaticProperty, DeniedWhenProtected, 0,
  NoPropertyObjects, NoPropertyObjects, OptionalImageProperty>;
def FullNameProperty : VPIProperty<"vpiFullName", 3, StringProperty,
  FullNamePropertyObjects, "37.10-37.85">;
def TopModuleProperty : VPIProperty<"vpiTopModule", 7, BooleanProperty,
  ModuleSource, "37.5", StaticProperty, DeniedWhenProtected, 0,
  NoPropertyObjects, NoPropertyObjects, FixedImageProperty>;
def CellInstanceProperty : VPIProperty<"vpiCellInstance", 8,
  BooleanProperty, ModuleSource, "37.10", StaticProperty,
  DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects,
  FixedImageProperty>;
def DefNameProperty : VPIProperty<"vpiDefName", 9, StringProperty,
  DefinitionNameObjects, "37.10; 37.15; 37.30; 37.35; 37.36",
  StaticProperty, DeniedWhenProtected, 0, NoPropertyObjects,
  NoPropertyObjects, DerivedProperty, PackageSource, NoPropertyObjects,
  VPIObjectSet<"SharedDefinitionNameObjects",
               [Module, Interface, Program, Gate, Switch]>>;
def ProtectedProperty : VPIProperty<"vpiProtected", 10, BooleanProperty,
  ProtectedLegacyObjects, "37.10; 37.36; 37.85">;
def DefNetTypeProperty : VPIProperty<"vpiDefNetType", 13, IntegerProperty,
  InstanceObjects, "37.10">;
def UnconnDriveProperty : VPIProperty<"vpiUnconnDrive", 14, IntegerProperty,
  InstanceObjects, "37.10">;
def DefFileProperty : VPIProperty<"vpiDefFile", 15, StringProperty,
  DefinitionLocationObjects, "37.10; 37.83", StaticProperty,
  DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects,
  FixedImageProperty, NoPropertyObjects, NoPropertyObjects,
  DefinitionInstanceObjects>;
def DefLineNoProperty : VPIProperty<"vpiDefLineNo", 16, IntegerProperty,
  DefinitionLocationObjects, "37.10; 37.83", StaticProperty,
  DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects,
  FixedImageProperty, NoPropertyObjects, NoPropertyObjects,
  DefinitionInstanceObjects>;
def ExplicitNameProperty : VPIProperty<"vpiExplicitName", 19,
  BooleanProperty, PortObjects, "37.14">;
def ClassParameterAssignmentObjects
    : VPIObjectSet<"ClassParameterAssignmentObjects", [ParamAssign]>;
def ConnByNameProperty
    : VPIProperty<"vpiConnByName", 21, BooleanProperty,
                  PortAndParamAssignObjects, "37.14; 37.28", StaticProperty,
                  DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects,
                  DerivedProperty, ClassParameterAssignmentObjects>;
def NetTypeProperty : VPIProperty<"vpiNetType", 22, IntegerProperty,
  NetPropertyObjects, "37.16", StaticProperty, DeniedWhenProtected, 1,
  NoPropertyObjects, NoPropertyObjects, FixedImageProperty>;
def NetTypeWire : VPISymbolicPropertyValue<NetTypeProperty, 1, "vpiWire">;
def NetTypeWand : VPISymbolicPropertyValue<NetTypeProperty, 2, "vpiWand">;
def NetTypeWor : VPISymbolicPropertyValue<NetTypeProperty, 3, "vpiWor">;
def NetTypeTri : VPISymbolicPropertyValue<NetTypeProperty, 4, "vpiTri">;
def NetTypeTri0 : VPISymbolicPropertyValue<NetTypeProperty, 5, "vpiTri0">;
def NetTypeTri1 : VPISymbolicPropertyValue<NetTypeProperty, 6, "vpiTri1">;
def NetTypeTriReg
    : VPISymbolicPropertyValue<NetTypeProperty, 7, "vpiTriReg">;
def NetTypeTriAnd
    : VPISymbolicPropertyValue<NetTypeProperty, 8, "vpiTriAnd">;
def NetTypeTriOr
    : VPISymbolicPropertyValue<NetTypeProperty, 9, "vpiTriOr">;
def NetTypeSupply1
    : VPISymbolicPropertyValue<NetTypeProperty, 10, "vpiSupply1">;
def NetTypeSupply0
    : VPISymbolicPropertyValue<NetTypeProperty, 11, "vpiSupply0">;
def NetTypeUwire
    : VPISymbolicPropertyValue<NetTypeProperty, 13, "vpiUwire">;
def NetTypeNettypeNet
    : VPISymbolicPropertyValue<NetTypeProperty, 14, "vpiNettypeNet">;
def NetTypeNettypeNetSelect
    : VPISymbolicPropertyValue<NetTypeProperty, 15, "vpiNettypeNetSelect">;
def NetTypeInterconnect
    : VPISymbolicPropertyValue<NetTypeProperty, 16, "vpiInterconnect">;
def ExplicitScalaredProperty : VPIProperty<"vpiExplicitScalared", 23,
  BooleanProperty, NetPropertyObjects, "37.16", StaticProperty,
  DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects,
  FixedImageProperty>;
def ExplicitVectoredProperty : VPIProperty<"vpiExplicitVectored", 24,
  BooleanProperty, NetPropertyObjects, "37.16", StaticProperty,
  DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects,
  FixedImageProperty>;
def ExpandedProperty : VPIProperty<"vpiExpanded", 25, BooleanProperty,
  NetPropertyObjects, "37.16", StaticProperty, DeniedWhenProtected, 0,
  NoPropertyObjects, NoPropertyObjects, FixedImageProperty>;
def ImplicitDeclProperty : VPIProperty<"vpiImplicitDecl", 26,
  BooleanProperty, NetAndGenScopeObjects, "37.16; 37.85", StaticProperty,
  DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects,
  DerivedProperty, NetPropertyObjects>;
def ChargeStrengthProperty : VPIProperty<"vpiChargeStrength", 27,
  IntegerProperty, NetPropertyObjects, "37.16", StaticProperty,
  DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects,
  FixedImageProperty>;
def ChargeStrengthNone
    : VPIIntegerPropertyValue<ChargeStrengthProperty, 0>;
def ChargeStrengthSmall
    : VPIIntegerPropertyValue<ChargeStrengthProperty, 2>;
def ChargeStrengthMedium
    : VPIIntegerPropertyValue<ChargeStrengthProperty, 4>;
def ChargeStrengthLarge
    : VPIIntegerPropertyValue<ChargeStrengthProperty, 16>;
def ArrayProperty : VPIProperty<"vpiArray", 28, BooleanProperty,
  ArrayMemberObjects, "37.10; 37.16; 37.17; 37.27; 37.35; 37.85">;
def TermIndexProperty : VPIProperty<"vpiTermIndex", 30, IntegerProperty,
  ReadPrimTermObjects, "37.35">;
def Strength0Property : VPIProperty<"vpiStrength0", 31, IntegerProperty,
  StrengthPropertyObjects, "37.16; 37.35; 37.47", StaticProperty,
  DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects,
  DerivedProperty, StaticDriverStrengthObjects>;
def Strength1Property : VPIProperty<"vpiStrength1", 32, IntegerProperty,
  StrengthPropertyObjects, "37.16; 37.35; 37.47", StaticProperty,
  DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects,
  DerivedProperty, StaticDriverStrengthObjects>;
def Strength0HiZ : VPIIntegerPropertyValue<Strength0Property, 1>;
def Strength0Weak : VPIIntegerPropertyValue<Strength0Property, 8>;
def Strength0Pull : VPIIntegerPropertyValue<Strength0Property, 32>;
def Strength0Strong : VPIIntegerPropertyValue<Strength0Property, 64>;
def Strength0Supply : VPIIntegerPropertyValue<Strength0Property, 128>;
def Strength1HiZ : VPIIntegerPropertyValue<Strength1Property, 1>;
def Strength1Weak : VPIIntegerPropertyValue<Strength1Property, 8>;
def Strength1Pull : VPIIntegerPropertyValue<Strength1Property, 32>;
def Strength1Strong : VPIIntegerPropertyValue<Strength1Property, 64>;
def Strength1Supply : VPIIntegerPropertyValue<Strength1Property, 128>;
def PrimTypeProperty : VPIProperty<"vpiPrimType", 33, IntegerProperty,
  PrimitiveDefinitionObjects, "37.35; 37.36", StaticProperty,
  DeniedWhenProtected, 1, NoPropertyObjects, NoPropertyObjects, DerivedProperty,
  NoPropertyObjects, NoPropertyObjects,
  VPIObjectSet<"SharedPrimitiveDefinitionObjects", [Gate, Switch]>>;

// Built-in definitions preserve the standard subtype and source spelling once.
class VPIBuiltinPrimitive<int value_, string apiName_, string sourceName_,
                          VPIObjectKind objectKind_>
    : VPISymbolicPropertyValue<PrimTypeProperty, value_, apiName_> {
  string sourceName = sourceName_;
  VPIObjectKind objectKind = objectKind_;
  // IEEE 28.3.2: switches pass input strengths; gates have static defaults.
  int defaultDriveStrength = !if(!eq(objectKind_, Gate), 64, 0);
}
def PrimitiveAnd : VPIBuiltinPrimitive<1, "vpiAndPrim", "and", Gate>;
def PrimitiveNand : VPIBuiltinPrimitive<2, "vpiNandPrim", "nand", Gate>;
def PrimitiveNor : VPIBuiltinPrimitive<3, "vpiNorPrim", "nor", Gate>;
def PrimitiveOr : VPIBuiltinPrimitive<4, "vpiOrPrim", "or", Gate>;
def PrimitiveXor : VPIBuiltinPrimitive<5, "vpiXorPrim", "xor", Gate>;
def PrimitiveXnor : VPIBuiltinPrimitive<6, "vpiXnorPrim", "xnor", Gate>;
def PrimitiveBuf : VPIBuiltinPrimitive<7, "vpiBufPrim", "buf", Gate>;
def PrimitiveNot : VPIBuiltinPrimitive<8, "vpiNotPrim", "not", Gate>;
def PrimitiveBufif0 : VPIBuiltinPrimitive<9, "vpiBufif0Prim", "bufif0", Gate>;
def PrimitiveBufif1 : VPIBuiltinPrimitive<10, "vpiBufif1Prim", "bufif1", Gate>;
def PrimitiveNotif0 : VPIBuiltinPrimitive<11, "vpiNotif0Prim", "notif0", Gate>;
def PrimitiveNotif1 : VPIBuiltinPrimitive<12, "vpiNotif1Prim", "notif1", Gate>;
def PrimitiveNmos : VPIBuiltinPrimitive<13, "vpiNmosPrim", "nmos", Switch>;
def PrimitivePmos : VPIBuiltinPrimitive<14, "vpiPmosPrim", "pmos", Switch>;
def PrimitiveCmos : VPIBuiltinPrimitive<15, "vpiCmosPrim", "cmos", Switch>;
def PrimitiveRnmos : VPIBuiltinPrimitive<16, "vpiRnmosPrim", "rnmos", Switch>;
def PrimitiveRpmos : VPIBuiltinPrimitive<17, "vpiRpmosPrim", "rpmos", Switch>;
def PrimitiveRcmos : VPIBuiltinPrimitive<18, "vpiRcmosPrim", "rcmos", Switch>;
def PrimitiveRtran : VPIBuiltinPrimitive<19, "vpiRtranPrim", "rtran", Switch>;
def PrimitiveRtranif0 : VPIBuiltinPrimitive<20, "vpiRtranif0Prim", "rtranif0", Switch>;
def PrimitiveRtranif1 : VPIBuiltinPrimitive<21, "vpiRtranif1Prim", "rtranif1", Switch>;
def PrimitiveTran : VPIBuiltinPrimitive<22, "vpiTranPrim", "tran", Switch>;
def PrimitiveTranif0 : VPIBuiltinPrimitive<23, "vpiTranif0Prim", "tranif0", Switch>;
def PrimitiveTranif1 : VPIBuiltinPrimitive<24, "vpiTranif1Prim", "tranif1", Switch>;
def PrimitivePullup : VPIBuiltinPrimitive<25, "vpiPullupPrim", "pullup", Gate> {
  let defaultDriveStrength = 32;
}
def PrimitivePulldown : VPIBuiltinPrimitive<26, "vpiPulldownPrim", "pulldown", Gate> {
  let defaultDriveStrength = 32;
}
def PrimitiveSeq : VPISymbolicPropertyValue<PrimTypeProperty, 27, "vpiSeqPrim">;
def PrimitiveComb : VPISymbolicPropertyValue<PrimTypeProperty, 28, "vpiCombPrim">;
def PolarityProperty : VPIProperty<"vpiPolarity", 34, IntegerProperty,
  ReadModPathObjects, "37.39">;
def DataPolarityProperty : VPIProperty<"vpiDataPolarity", 35,
  IntegerProperty, ReadModPathObjects, "37.39">;
def EdgeProperty : VPIProperty<"vpiEdge", 36, IntegerProperty,
  EdgePropertyObjects, "37.39; 37.40">;
def PathTypeProperty : VPIProperty<"vpiPathType", 37, IntegerProperty,
  ReadModPathObjects, "37.39">;
def TchkTypeProperty : VPIProperty<"vpiTchkType", 38, IntegerProperty,
  TimingCheckObjects, "37.40", StaticProperty, DeniedWhenProtected, 1>;
def OpTypeProperty : VPIProperty<"vpiOpType", 39, IntegerProperty,
  OperationPropertyObjects, "37.52; 37.54; 37.59; 37.64", StaticProperty,
  DeniedWhenProtected, 1, NoPropertyObjects, NoPropertyObjects, DerivedProperty,
  VPIObjectSet<"SourceOperationProperties", [Operation, Assignment]>>;
// IEEE 1800-2023 Annex M: canonical symbolic spellings for vpiOpType.
def OpTypeMinusOp : VPISymbolicPropertyValue<OpTypeProperty, 1, "vpiMinusOp">;
def OpTypePlusOp : VPISymbolicPropertyValue<OpTypeProperty, 2, "vpiPlusOp">;
def OpTypeNotOp : VPISymbolicPropertyValue<OpTypeProperty, 3, "vpiNotOp">;
def OpTypeBitNegOp : VPISymbolicPropertyValue<OpTypeProperty, 4, "vpiBitNegOp">;
def OpTypeUnaryAndOp : VPISymbolicPropertyValue<OpTypeProperty, 5, "vpiUnaryAndOp">;
def OpTypeUnaryNandOp : VPISymbolicPropertyValue<OpTypeProperty, 6, "vpiUnaryNandOp">;
def OpTypeUnaryOrOp : VPISymbolicPropertyValue<OpTypeProperty, 7, "vpiUnaryOrOp">;
def OpTypeUnaryNorOp : VPISymbolicPropertyValue<OpTypeProperty, 8, "vpiUnaryNorOp">;
def OpTypeUnaryXorOp : VPISymbolicPropertyValue<OpTypeProperty, 9, "vpiUnaryXorOp">;
def OpTypeUnaryXNorOp : VPISymbolicPropertyValue<OpTypeProperty, 10, "vpiUnaryXNorOp">;
def OpTypeSubOp : VPISymbolicPropertyValue<OpTypeProperty, 11, "vpiSubOp">;
def OpTypeDivOp : VPISymbolicPropertyValue<OpTypeProperty, 12, "vpiDivOp">;
def OpTypeModOp : VPISymbolicPropertyValue<OpTypeProperty, 13, "vpiModOp">;
def OpTypeEqOp : VPISymbolicPropertyValue<OpTypeProperty, 14, "vpiEqOp">;
def OpTypeNeqOp : VPISymbolicPropertyValue<OpTypeProperty, 15, "vpiNeqOp">;
def OpTypeCaseEqOp : VPISymbolicPropertyValue<OpTypeProperty, 16, "vpiCaseEqOp">;
def OpTypeCaseNeqOp : VPISymbolicPropertyValue<OpTypeProperty, 17, "vpiCaseNeqOp">;
def OpTypeGtOp : VPISymbolicPropertyValue<OpTypeProperty, 18, "vpiGtOp">;
def OpTypeGeOp : VPISymbolicPropertyValue<OpTypeProperty, 19, "vpiGeOp">;
def OpTypeLtOp : VPISymbolicPropertyValue<OpTypeProperty, 20, "vpiLtOp">;
def OpTypeLeOp : VPISymbolicPropertyValue<OpTypeProperty, 21, "vpiLeOp">;
def OpTypeLShiftOp : VPISymbolicPropertyValue<OpTypeProperty, 22, "vpiLShiftOp">;
def OpTypeRShiftOp : VPISymbolicPropertyValue<OpTypeProperty, 23, "vpiRShiftOp">;
def OpTypeAddOp : VPISymbolicPropertyValue<OpTypeProperty, 24, "vpiAddOp">;
def OpTypeMultOp : VPISymbolicPropertyValue<OpTypeProperty, 25, "vpiMultOp">;
def OpTypeLogAndOp : VPISymbolicPropertyValue<OpTypeProperty, 26, "vpiLogAndOp">;
def OpTypeLogOrOp : VPISymbolicPropertyValue<OpTypeProperty, 27, "vpiLogOrOp">;
def OpTypeBitAndOp : VPISymbolicPropertyValue<OpTypeProperty, 28, "vpiBitAndOp">;
def OpTypeBitOrOp : VPISymbolicPropertyValue<OpTypeProperty, 29, "vpiBitOrOp">;
def OpTypeBitXorOp : VPISymbolicPropertyValue<OpTypeProperty, 30, "vpiBitXorOp">;
def OpTypeBitXNorOp : VPISymbolicPropertyValue<OpTypeProperty, 31, "vpiBitXNorOp">;
def OpTypeConditionOp : VPISymbolicPropertyValue<OpTypeProperty, 32, "vpiConditionOp">;
def OpTypeConcatOp : VPISymbolicPropertyValue<OpTypeProperty, 33, "vpiConcatOp">;
def OpTypeMultiConcatOp : VPISymbolicPropertyValue<OpTypeProperty, 34, "vpiMultiConcatOp">;
def OpTypeEventOrOp : VPISymbolicPropertyValue<OpTypeProperty, 35, "vpiEventOrOp">;
def OpTypeNullOp : VPISymbolicPropertyValue<OpTypeProperty, 36, "vpiNullOp">;
def OpTypeListOp : VPISymbolicPropertyValue<OpTypeProperty, 37, "vpiListOp">;
def OpTypeMinTypMaxOp : VPISymbolicPropertyValue<OpTypeProperty, 38, "vpiMinTypMaxOp">;
def OpTypePosedgeOp : VPISymbolicPropertyValue<OpTypeProperty, 39, "vpiPosedgeOp">;
def OpTypeNegedgeOp : VPISymbolicPropertyValue<OpTypeProperty, 40, "vpiNegedgeOp">;
def OpTypeArithLShiftOp : VPISymbolicPropertyValue<OpTypeProperty, 41, "vpiArithLShiftOp">;
def OpTypeArithRShiftOp : VPISymbolicPropertyValue<OpTypeProperty, 42, "vpiArithRShiftOp">;
def OpTypePowerOp : VPISymbolicPropertyValue<OpTypeProperty, 43, "vpiPowerOp">;
def OpTypeImplyOp : VPISymbolicPropertyValue<OpTypeProperty, 50, "vpiImplyOp">;
def OpTypeNonOverlapImplyOp : VPISymbolicPropertyValue<OpTypeProperty, 51, "vpiNonOverlapImplyOp">;
def OpTypeOverlapImplyOp : VPISymbolicPropertyValue<OpTypeProperty, 52, "vpiOverlapImplyOp">;
def OpTypeUnaryCycleDelayOp : VPISymbolicPropertyValue<OpTypeProperty, 53, "vpiUnaryCycleDelayOp">;
def OpTypeCycleDelayOp : VPISymbolicPropertyValue<OpTypeProperty, 54, "vpiCycleDelayOp">;
def OpTypeIntersectOp : VPISymbolicPropertyValue<OpTypeProperty, 55, "vpiIntersectOp">;
def OpTypeFirstMatchOp : VPISymbolicPropertyValue<OpTypeProperty, 56, "vpiFirstMatchOp">;
def OpTypeThroughoutOp : VPISymbolicPropertyValue<OpTypeProperty, 57, "vpiThroughoutOp">;
def OpTypeWithinOp : VPISymbolicPropertyValue<OpTypeProperty, 58, "vpiWithinOp">;
def OpTypeRepeatOp : VPISymbolicPropertyValue<OpTypeProperty, 59, "vpiRepeatOp">;
def OpTypeConsecutiveRepeatOp : VPISymbolicPropertyValue<OpTypeProperty, 60, "vpiConsecutiveRepeatOp">;
def OpTypeGotoRepeatOp : VPISymbolicPropertyValue<OpTypeProperty, 61, "vpiGotoRepeatOp">;
def OpTypePostIncOp : VPISymbolicPropertyValue<OpTypeProperty, 62, "vpiPostIncOp">;
def OpTypePreIncOp : VPISymbolicPropertyValue<OpTypeProperty, 63, "vpiPreIncOp">;
def OpTypePostDecOp : VPISymbolicPropertyValue<OpTypeProperty, 64, "vpiPostDecOp">;
def OpTypePreDecOp : VPISymbolicPropertyValue<OpTypeProperty, 65, "vpiPreDecOp">;
def OpTypeMatchOp : VPISymbolicPropertyValue<OpTypeProperty, 66, "vpiMatchOp">;
def OpTypeCastOp : VPISymbolicPropertyValue<OpTypeProperty, 67, "vpiCastOp">;
def OpTypeIffOp : VPISymbolicPropertyValue<OpTypeProperty, 68, "vpiIffOp">;
def OpTypeWildEqOp : VPISymbolicPropertyValue<OpTypeProperty, 69, "vpiWildEqOp">;
def OpTypeWildNeqOp : VPISymbolicPropertyValue<OpTypeProperty, 70, "vpiWildNeqOp">;
def OpTypeStreamLROp : VPISymbolicPropertyValue<OpTypeProperty, 71, "vpiStreamLROp">;
def OpTypeStreamRLOp : VPISymbolicPropertyValue<OpTypeProperty, 72, "vpiStreamRLOp">;
def OpTypeMatchedOp : VPISymbolicPropertyValue<OpTypeProperty, 73, "vpiMatchedOp">;
def OpTypeTriggeredOp : VPISymbolicPropertyValue<OpTypeProperty, 74, "vpiTriggeredOp">;
def OpTypeAssignmentPatternOp : VPISymbolicPropertyValue<OpTypeProperty, 75, "vpiAssignmentPatternOp">;
def OpTypeMultiAssignmentPatternOp : VPISymbolicPropertyValue<OpTypeProperty, 76, "vpiMultiAssignmentPatternOp">;
def OpTypeIfOp : VPISymbolicPropertyValue<OpTypeProperty, 77, "vpiIfOp">;
def OpTypeIfElseOp : VPISymbolicPropertyValue<OpTypeProperty, 78, "vpiIfElseOp">;
def OpTypeCompAndOp : VPISymbolicPropertyValue<OpTypeProperty, 79, "vpiCompAndOp">;
def OpTypeCompOrOp : VPISymbolicPropertyValue<OpTypeProperty, 80, "vpiCompOrOp">;
def OpTypeTypeOp : VPISymbolicPropertyValue<OpTypeProperty, 81, "vpiTypeOp">;
def OpTypeAssignmentOp : VPISymbolicPropertyValue<OpTypeProperty, 82, "vpiAssignmentOp">;
def OpTypeAcceptOnOp : VPISymbolicPropertyValue<OpTypeProperty, 83, "vpiAcceptOnOp">;
def OpTypeRejectOnOp : VPISymbolicPropertyValue<OpTypeProperty, 84, "vpiRejectOnOp">;
def OpTypeSyncAcceptOnOp : VPISymbolicPropertyValue<OpTypeProperty, 85, "vpiSyncAcceptOnOp">;
def OpTypeSyncRejectOnOp : VPISymbolicPropertyValue<OpTypeProperty, 86, "vpiSyncRejectOnOp">;
def OpTypeOverlapFollowedByOp : VPISymbolicPropertyValue<OpTypeProperty, 87, "vpiOverlapFollowedByOp">;
def OpTypeNonOverlapFollowedByOp : VPISymbolicPropertyValue<OpTypeProperty, 88, "vpiNonOverlapFollowedByOp">;
def OpTypeNexttimeOp : VPISymbolicPropertyValue<OpTypeProperty, 89, "vpiNexttimeOp">;
def OpTypeAlwaysOp : VPISymbolicPropertyValue<OpTypeProperty, 90, "vpiAlwaysOp">;
def OpTypeEventuallyOp : VPISymbolicPropertyValue<OpTypeProperty, 91, "vpiEventuallyOp">;
def OpTypeUntilOp : VPISymbolicPropertyValue<OpTypeProperty, 92, "vpiUntilOp">;
def OpTypeUntilWithOp : VPISymbolicPropertyValue<OpTypeProperty, 93, "vpiUntilWithOp">;
def OpTypeImpliesOp : VPISymbolicPropertyValue<OpTypeProperty, 94, "vpiImpliesOp">;
def OpTypeInsideOp : VPISymbolicPropertyValue<OpTypeProperty, 95, "vpiInsideOp">;
def BlockingProperty : VPIProperty<"vpiBlocking", 41, BooleanProperty,
  BlockingPropertyObjects, "37.62; 37.64", StaticProperty,
  DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects, DerivedProperty,
  VPIObjectSet<"SourceAssignmentBlockingProperties", [Assignment]>>;
def CaseTypeProperty : VPIProperty<"vpiCaseType", 42, IntegerProperty,
  ReadCaseObjects, "37.72", StaticProperty, DeniedWhenProtected, 0,
  NoPropertyObjects, NoPropertyObjects, FixedImageProperty>;
def CaseTypeExact : VPIIntegerPropertyValue<CaseTypeProperty, 1>;
def CaseTypeX : VPIIntegerPropertyValue<CaseTypeProperty, 2>;
def CaseTypeZ : VPIIntegerPropertyValue<CaseTypeProperty, 3>;
def NetDeclAssignProperty : VPIProperty<"vpiNetDeclAssign", 43,
  BooleanProperty, NetAndContinuousObjects, "37.16; 37.47", StaticProperty,
  DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects,
  DerivedProperty,
  VPIObjectSet<"NetDeclarationAssignmentProperties", [ContAssign], [NetFamily]>>;
def FuncTypeProperty : VPIProperty<"vpiFuncType", 44, IntegerProperty,
  FunctionPropertyObjects, "37.41; 37.42">;
def UserDefnProperty : VPIProperty<"vpiUserDefn", 45, BooleanProperty,
  SystemCallObjects, "37.42">;
def ScheduledProperty : VPIProperty<"vpiScheduled", 46, BooleanProperty,
  ScheduledEventObjects, "38.34", DynamicProperty, DeniedWhenProtected, 0,
  NoPropertyObjects, NoPropertyObjects, RuntimeProperty>;
def DefDelayModeProperty : VPIProperty<"vpiDefDelayMode", 47,
  IntegerProperty, InstanceObjects, "37.10">;
def DefDecayTimeProperty : VPIProperty<"vpiDefDecayTime", 48,
  IntegerProperty, ModuleSource, "37.5">;
def ActiveProperty : VPIProperty<"vpiActive", 49, BooleanProperty,
  ActivePropertyObjects, "37.43; 37.44", DynamicProperty,
  DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects,
  RuntimeProperty>;
def AutomaticProperty : VPIProperty<"vpiAutomatic", 50, BooleanProperty,
  AutomaticPropertyObjects, "37.3.7; 37.10; 37.17; 37.27; 37.31; 37.32; "
  "37.34; 37.41", StaticProperty, DeniedWhenProtected, 0,
  NoPropertyObjects, NoPropertyObjects, OptionalImageProperty,
  AutomaticImageObjects>;
def CellProperty : VPIProperty<"vpiCell", 51, StringProperty,
  InstanceObjects, "37.10">;
def ConfigProperty : VPIProperty<"vpiConfig", 52, StringProperty,
  InstanceObjects, "37.10">;
def DecompileProperty : VPIProperty<"vpiDecompile", 54, StringProperty,
  DecompilePropertyObjects, "37.42; 37.59">;
def DefAttributeProperty : VPIProperty<"vpiDefAttribute", 55,
  BooleanProperty, ReadAttributeObjects, "37.83">;
def DelayTypeProperty : VPIProperty<"vpiDelayType", 56, IntegerProperty,
  DelayTypePropertyObjects, "37.45", StaticProperty, DeniedWhenProtected, 1>;
def IteratorTypeProperty : VPIProperty<"vpiIteratorType", 57,
  IntegerProperty, ReadIteratorObjects, "37.84">;
def LibraryProperty : VPIProperty<"vpiLibrary", 58, StringProperty,
  InstanceObjects, "37.10">;
def OffsetProperty : VPIProperty<"vpiOffset", 60, IntegerProperty,
  ContinuousAssignmentObjects, "37.47">;
def ResolvedNetTypeProperty : VPIProperty<"vpiResolvedNetType", 61,
  IntegerProperty, NetPropertyObjects, "37.16", StaticProperty,
  DeniedWhenProtected, 1, NoPropertyObjects, NoPropertyObjects,
  IndexedImageProperty>;
def ResolvedNetTypeWire
    : VPISymbolicPropertyValue<ResolvedNetTypeProperty, 1, "vpiWire">;
def ResolvedNetTypeWand
    : VPISymbolicPropertyValue<ResolvedNetTypeProperty, 2, "vpiWand">;
def ResolvedNetTypeWor
    : VPISymbolicPropertyValue<ResolvedNetTypeProperty, 3, "vpiWor">;
def ResolvedNetTypeTri
    : VPISymbolicPropertyValue<ResolvedNetTypeProperty, 4, "vpiTri">;
def ResolvedNetTypeTri0
    : VPISymbolicPropertyValue<ResolvedNetTypeProperty, 5, "vpiTri0">;
def ResolvedNetTypeTri1
    : VPISymbolicPropertyValue<ResolvedNetTypeProperty, 6, "vpiTri1">;
def ResolvedNetTypeTriReg
    : VPISymbolicPropertyValue<ResolvedNetTypeProperty, 7, "vpiTriReg">;
def ResolvedNetTypeTriAnd
    : VPISymbolicPropertyValue<ResolvedNetTypeProperty, 8, "vpiTriAnd">;
def ResolvedNetTypeTriOr
    : VPISymbolicPropertyValue<ResolvedNetTypeProperty, 9, "vpiTriOr">;
def ResolvedNetTypeSupply1
    : VPISymbolicPropertyValue<ResolvedNetTypeProperty, 10, "vpiSupply1">;
def ResolvedNetTypeSupply0
    : VPISymbolicPropertyValue<ResolvedNetTypeProperty, 11, "vpiSupply0">;
def ResolvedNetTypeUwire
    : VPISymbolicPropertyValue<ResolvedNetTypeProperty, 13, "vpiUwire">;
def ResolvedNetTypeNettypeNet
    : VPISymbolicPropertyValue<ResolvedNetTypeProperty, 14, "vpiNettypeNet">;
def ResolvedNetTypeNettypeNetSelect
    : VPISymbolicPropertyValue<ResolvedNetTypeProperty, 15,
        "vpiNettypeNetSelect">;
def ResolvedNetTypeInterconnect
    : VPISymbolicPropertyValue<ResolvedNetTypeProperty, 16,
        "vpiInterconnect">;
def SaveRestartIDProperty : VPIProperty<"vpiSaveRestartID", 62,
  IntegerProperty, NullOnlyPropertyObjects, "38.9; 38.36.1",
  DynamicProperty, DeniedWhenProtected, 0, NoPropertyObjects,
  NoPropertyObjects, RuntimeProperty>;
def SaveRestartLocationProperty : VPIProperty<"vpiSaveRestartLocation", 63,
  StringProperty, NullOnlyPropertyObjects, "38.9; 38.11; 38.36.1",
  DynamicProperty, DeniedWhenProtected, 0, NoPropertyObjects,
  NoPropertyObjects, RuntimeProperty>;
def ValidProperty : VPIProperty<"vpiValid", 64, IntegerProperty,
  ValidPropertyObjects, "37.3.7; Annex I", DynamicProperty,
  DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects,
  RuntimeProperty>;
def LocalParamProperty : VPIProperty<"vpiLocalParam", 70, BooleanProperty,
  ReadParameterObjects, "37.28; 37.31; 37.85", StaticProperty,
  DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects,
  FixedImageProperty, ReadParameterObjects>;
def ModPathHasIfNoneProperty : VPIProperty<"vpiModPathHasIfNone", 71,
  BooleanProperty, ReadModPathObjects, "37.39">;
def IndexedPartSelectTypeProperty : VPIProperty<"vpiIndexedPartSelectType",
  72, IntegerProperty, IndexedPartSelectObjects, "37.59">;
def IsMemoryProperty : VPIProperty<"vpiIsMemory", 73, BooleanProperty,
  ReadRegArrayObjects, "37.20">;

def TopProperty : VPIProperty<"vpiTop", 600, BooleanProperty,
  InstanceObjects, "37.10", StaticProperty, DeniedWhenProtected, 0,
  NoPropertyObjects, NoPropertyObjects, FixedImageProperty>;
def UnitProperty : VPIProperty<"vpiUnit", 602, BooleanProperty,
  InstanceObjects, "37.10", StaticProperty, DeniedWhenProtected, 0,
  NoPropertyObjects, NoPropertyObjects, FixedImageProperty>;
def JoinTypeProperty : VPIProperty<"vpiJoinType", 603, IntegerProperty,
  ForkObjects, "37.12">;
def AccessTypeProperty : VPIProperty<"vpiAccessType", 604, IntegerProperty,
  AccessTypePropertyObjects, "37.8; 37.34; 37.41", StaticProperty,
  DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects, OptionalImageProperty,
  ReadConstraintObjects>;
def IsRandomizedProperty : VPIProperty<"vpiIsRandomized", 608,
  BooleanProperty, OrdinaryVariablePropertyObjects, "37.17", DynamicProperty,
  DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects,
  RuntimeProperty>;
def LocalVarDeclsProperty : VPIProperty<"vpiLocalVarDecls", 609,
  IntegerProperty, ReadForObjects, "37.74">;
def ConstantVariableProperty : VPIProperty<"vpiConstantVariable", 612,
  BooleanProperty, OrdinaryVariablePropertyObjects, "37.17", StaticProperty,
  DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects,
  OptionalImageProperty>;
def StructUnionMemberProperty : VPIProperty<"vpiStructUnionMember", 615,
  BooleanProperty, StructUnionMemberPropertyObjects,
  "37.16; 37.17; 37.18">;
def VisibilityProperty : VPIProperty<"vpiVisibility", 620, IntegerProperty,
  VisibilityPropertyObjects, "37.17; 37.41", StaticProperty,
  DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects,
  OptionalImageProperty, TaskFunctionDeclObjects>;
def VisibilityPublic : VPIIntegerPropertyValue<VisibilityProperty, 1>;
def VisibilityProtected : VPIIntegerPropertyValue<VisibilityProperty, 2>;
def VisibilityLocal : VPIIntegerPropertyValue<VisibilityProperty, 3>;
def AlwaysTypeProperty : VPIProperty<"vpiAlwaysType", 624,
  IntegerProperty, AlwaysObjects, "37.63", StaticProperty,
  DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects,
  FixedImageProperty, AlwaysObjects>;
def AlwaysTypeAlways
    : VPIIntegerPropertyValue<AlwaysTypeProperty, 1>;
def AlwaysTypeAlwaysComb
    : VPIIntegerPropertyValue<AlwaysTypeProperty, 2>;
def AlwaysTypeAlwaysFF
    : VPIIntegerPropertyValue<AlwaysTypeProperty, 3>;
def AlwaysTypeAlwaysLatch
    : VPIIntegerPropertyValue<AlwaysTypeProperty, 4>;
def DistTypeProperty : VPIProperty<"vpiDistType", 625, IntegerProperty,
  DistributionItemObjects, "37.34">;
def VirtualProperty : VPIProperty<"vpiVirtual", 635, BooleanProperty,
  VirtualPropertyObjects, "37.31; 37.34; 37.41", StaticProperty,
  DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects,
  OptionalImageProperty>;
def HasActualProperty : VPIProperty<"vpiHasActual", 636, BooleanProperty,
  DynamicPrefixPropertyObjects, "37.61", DynamicProperty,
  DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects,
  RuntimeProperty>;
def IsConstraintEnabledProperty : VPIProperty<"vpiIsConstraintEnabled", 638,
  BooleanProperty, ReadConstraintObjects, "37.34", DynamicProperty,
  DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects,
  RuntimeProperty>;
def SoftProperty : VPIProperty<"vpiSoft", 639, BooleanProperty,
  ConstraintExpressionPropertyObjects, "37.38">;
def ClassTypeProperty : VPIProperty<"vpiClassType", 640, IntegerProperty,
  ReadClassTypespecObjects, "37.32">;
def MethodProperty : VPIProperty<"vpiMethod", 645, BooleanProperty,
  TaskFunctionDeclObjects, "37.41", StaticProperty, DeniedWhenProtected, 0,
  NoPropertyObjects, NoPropertyObjects, FixedImageProperty>;

def IsClockInferredProperty : VPIProperty<"vpiIsClockInferred", 649,
  BooleanProperty, ConcurrentAssertionPropertyObjects, "37.50">;
def QualifierProperty : VPIProperty<"vpiQualifier", 650, IntegerProperty,
  QualifierPropertyObjects, "37.71; 37.72", StaticProperty,
  DeniedWhenProtected, 0, NoPropertyObjects, NoPropertyObjects,
  FixedImageProperty>;
def InputEdgeProperty : VPIProperty<"vpiInputEdge", 651, IntegerProperty,
  ClockingPropertyObjects, "37.48">;
def OutputEdgeProperty : VPIProperty<"vpiOutputEdge", 652, IntegerProperty,
  ClockingPropertyObjects, "37.48">;
def GenericProperty : VPIProperty<"vpiGeneric", 653, BooleanProperty,
  ReferenceObjects, "37.15">;
def CompatibilityModeProperty : VPIProperty<"vpiCompatibilityMode", 654,
  IntegerProperty, NullOnlyPropertyObjects, "Annex M">;
def OpStrongProperty : VPIProperty<"vpiOpStrong", 656, BooleanProperty,
  PropertySpecificationObjects, "37.52">;
def IsDeferredProperty : VPIProperty<"vpiIsDeferred", 657, IntegerProperty,
  ImmediateAssertionPropertyObjects, "37.55">;
def AllocSchemeProperty : VPIProperty<"vpiAllocScheme", 658,
  IntegerProperty, AllConcreteObjects, "37.3.7", StaticProperty,
  DeniedWhenProtected, 0, DynamicAllocSchemeObjects, NoPropertyObjects,
  DerivedProperty, NoPropertyObjects, DynamicAllocSchemeObjects>;
def IsCoverSequenceProperty : VPIProperty<"vpiIsCoverSequence", 659,
  BooleanProperty, CoverObjects, "37.50">;
def StartLineProperty : VPIProperty<"vpiStartLine", 661, IntegerProperty,
  AssertionLocationPropertyObjects, "37.49">;
def ColumnProperty : VPIProperty<"vpiColumn", 662, IntegerProperty,
  AssertionLocationPropertyObjects, "37.49">;
def EndLineProperty : VPIProperty<"vpiEndLine", 663, IntegerProperty,
  AssertionLocationPropertyObjects, "37.49">;
def EndColumnProperty : VPIProperty<"vpiEndColumn", 664, IntegerProperty,
  AssertionLocationPropertyObjects, "37.49">;
def DPIPureProperty : VPIProperty<"vpiDPIPure", 665, BooleanProperty,
  TaskFunctionDeclObjects, "37.41">;
def DPIContextProperty : VPIProperty<"vpiDPIContext", 666, BooleanProperty,
  TaskFunctionDeclObjects, "37.41">;
def DPICStrProperty : VPIProperty<"vpiDPICStr", 667, IntegerProperty,
  TaskFunctionDeclObjects, "37.41">;
def DPICIdentifierProperty : VPIProperty<"vpiDPICIdentifier", 668,
  StringProperty, TaskFunctionDeclObjects, "37.41">;
def IsModPortProperty : VPIProperty<"vpiIsModPort", 669, BooleanProperty,
  InterfaceTypePropertyObjects, "37.29; 37.30">;
def IsFinalProperty : VPIProperty<"vpiIsFinal", 670, IntegerProperty,
  ImmediateAssertionPropertyObjects, "37.55">;
// Exact source-only identities that may occupy an immutable static-object
// image record. Runtime-created handles, executable processes, statements,
// nets, and ordinary design storage use their dedicated representations.
def StaticImageObjects
    : VPIObjectSet<
          "StaticImageObjects",
          [Package, Parameter, TypeParameter, ParamAssign, Constant, Operation,
           ClassDefn, Extends, Constraint, Modport, Task, FuncCall, TaskCall,
           MethodFuncCall, MethodTaskCall, SysFuncCall, SysTaskCall, Function,
           ClockingBlock, GenScope, PropertyDecl, SequenceDecl, Gate, Switch,
           Udp, PrimTerm, NamedEvent, ModuleArray, InterfaceArray, ProgramArray,
           GateArray, SwitchArray, UdpArray, NamedEventArray, GenScopeArray,
           InterconnectArray, LongIntTypespec, ShortRealTypespec, ByteTypespec,
           ShortIntTypespec, IntTypespec, ClassTypespec, StringTypespec,
           ChandleTypespec, EnumTypespec, IntegerTypespec, TimeTypespec,
           RealTypespec, StructTypespec, UnionTypespec, BitTypespec,
           LogicTypespec, ArrayTypespec, VoidTypespec, PackedArrayTypespec,
           SequenceTypespec, PropertyTypespec, EventTypespec, InterfaceTypespec,
           EnumConst, NettypeDecl]>;
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
// 37.25 details 6 and 11: aliases inherit the target kind, including an
// unresolved type parameter acting as a typespec.
def TypedefTargetObjects : VPIObjectSet<"TypedefTargetObjects",
  [TypeParameter, LongIntTypespec, ShortRealTypespec, ByteTypespec,
   ShortIntTypespec,
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
  IterateMode, PrimitiveConcreteObjects, NoOrder, "37.5", 0,
  AutomaticDirectChild>;
def ModulePrimitiveArrays : VPITraversalEdge<ModuleSource, PrimitiveArray,
  IterateMode, PrimitiveArrayObjects, NoOrder, "37.5", 0,
  AutomaticDirectChild>;
def ModuleModPaths : VPITraversalEdge<ModuleSource, ModPath, IterateMode,
  ModPathObjects, NoOrder, "37.5">;
def ModuleTchks : VPITraversalEdge<ModuleSource, Tchk, IterateMode,
  TchkObjects, NoOrder, "37.5">;
def ModuleDefParams : VPITraversalEdge<ModuleSource, DefParam, IterateMode,
  DefParamObjects, NoOrder, "37.5">;
def ModuleIODecls : VPITraversalEdge<ModuleSource, IODecl, IterateMode,
  IODeclObjects, NoOrder, "37.5", 0, AutomaticDefinitionMember>;
def ModuleAliasStmts : VPITraversalEdge<ModuleSource, AliasStmt, IterateMode,
  AliasStmtObjects, NoOrder, "37.5", 1>;
def ModuleClockingBlocks : VPITraversalEdge<ModuleSource, ClockingBlock,
  IterateMode, ClockingBlockObjects, NoOrder, "37.5">;
def ModuleInternalScopes : VPITraversalEdge<ModuleSource, InternalScopeRel,
  IterateMode, ScopeObjects, NoOrder, "37.5", 0, AutomaticDirectChild>;
def ModuleInstanceArrays : VPITraversalEdge<ModuleSource, InstanceArrayRel,
  HandleMode, ModuleArrayObjects, NoOrder, "37.5", 0,
  AutomaticIndexedContainer>;

def InterfaceTfDecls : VPITraversalEdge<InterfaceObjects, InterfaceTfDecl,
  IterateMode, InterfaceTfDeclObjects, NoOrder, "37.6">;
def InterfaceModports : VPITraversalEdge<InterfaceObjects, Modport,
  IterateMode, ModportObjects, NoOrder, "37.6", 0, AutomaticDirectChild>;
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
  InstanceArrayRel, HandleMode, InterfaceArrayObjects, NoOrder,
  "37.6", 0, AutomaticIndexedContainer>;

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
  HandleMode, ProgramArrayObjects, NoOrder, "37.9", 0,
  AutomaticIndexedContainer>;

def InstanceTaskFunctions : VPITraversalEdge<InstanceObjects, TaskFuncRel,
  IterateMode, TaskFunctionDeclObjects, NoOrder, "37.10", 0,
  AutomaticDirectChild>;
def InstanceNets : VPITraversalEdge<InstanceObjects, Net, IterateMode,
  NetObjects, NoOrder, "37.10", 0, AutomaticDirectChild>;
def InstanceNetArrays : VPITraversalEdge<InstanceObjects, NetArray,
  IterateMode, NetArrayObjects, NoOrder, "37.10", 0, AutomaticDirectChild>;
def InstanceVariables : VPITraversalEdge<InstanceObjects, VariablesRel,
  IterateMode, DataVariableObjects, NoOrder, "37.10", 0,
  AutomaticDirectChild>;
def InstanceRegs : VPITraversalEdge<InstanceObjects, Reg, IterateMode,
  RegObjects, NoOrder, "37.10", 0, AutomaticDirectChild>;
def InstanceRegArrays : VPITraversalEdge<InstanceObjects, RegArray,
  IterateMode, RegArrayObjects, NoOrder, "37.10", 0, AutomaticDirectChild>;
def InstanceNamedEvents : VPITraversalEdge<InstanceObjects, NamedEvent,
  IterateMode, NamedEventTargetObjects, NoOrder, "37.10", 0,
  AutomaticDirectChild>;
def InstanceNamedEventArrays : VPITraversalEdge<InstanceObjects,
  NamedEventArray, IterateMode, NamedEventArrayObjects, NoOrder, "37.10", 0,
  AutomaticDirectChild>;
def InstanceParameters : VPITraversalEdge<InstanceObjects, Parameter,
  IterateMode, ParameterIterationObjects, NoOrder, "37.10", 0,
  AutomaticDirectChild>;
def InstanceTypeParameters : VPITraversalEdge<InstanceObjects, TypeParameter,
  IterateMode, TypeParameterObjects, NoOrder, "37.10", 0,
  AutomaticDirectChild>;
def InstanceSpecParams : VPITraversalEdge<InstanceObjects, SpecParam,
  IterateMode, SpecParamObjects, NoOrder, "37.10">;
def InstanceAssertions : VPITraversalEdge<InstanceObjects, AssertionRel,
  IterateMode, AssertionTargetObjects, NoOrder, "37.10", 0,
  AutomaticDirectChild>;
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
  NoOrder, "37.12", 0, AutomaticDirectChild>;
def ScopeNamedEvents : VPITraversalEdge<NonInstanceScopeObjects, NamedEvent,
  IterateMode, NamedEventTargetObjects, NoOrder, "37.12; 37.85">;
def ScopeNamedEventArrays : VPITraversalEdge<NonInstanceScopeObjects,
  NamedEventArray, IterateMode, NamedEventArrayObjects, NoOrder,
  "37.12; 37.85">;
def ScopeVariables : VPITraversalEdge<NonInstanceScopeObjects, VariablesRel,
  IterateMode, DataVariableObjects, NoOrder, "37.12; 37.85">;
def InstanceVirtualInterfaces : VPITraversalEdge<InstanceObjects,
  VirtualInterfaceVar, IterateMode, VirtualInterfaceObjects, NoOrder,
  "37.12", 0, AutomaticDirectChild>;
def ScopeVirtualInterfaces : VPITraversalEdge<
  VPIObjectSet<"NonInstanceVirtualInterfaceScopes", [], [ScopeFamily],
    [Package, Module, Interface, Program]>,
  VirtualInterfaceVar, IterateMode, VirtualInterfaceObjects, NoOrder,
  "37.12">;
def ScopeParameters
    : VPITraversalEdge<VPIObjectSet<"DirectParameterScopes", [], [ScopeFamily],
                                    [Package, Module, Interface, Program,
                                     ForeachStmt, ClassTypespec]>,
                       Parameter, IterateMode, ParameterIterationObjects,
                       NoOrder, "37.12; 37.85", 0, AutomaticDirectChild>;
// A lexical class type references the defining class's exact declarations;
// a specialization owns its effective parameter declarations (37.32d1,d12).
def ClassTypespecParameters
    : VPITraversalEdge<ClassTypespecObjects, Parameter, IterateMode,
                       ParameterIterationObjects, NoOrder, "37.32">;
def ScopeInternalScopes : VPITraversalEdge<ScopeWithoutModuleObjects,
  InternalScopeRel,
  IterateMode, ScopeObjects, NoOrder, "37.12; 37.85", 0,
  AutomaticDirectChild>;
def ScopeImports : VPITraversalEdge<ScopeSource, ImportRel, IterateMode,
  DeclarationObjects, NoOrder, "37.12">;
def ScopeTypedefs : VPITraversalEdge<NonInstanceScopeObjects, TypedefRel,
  IterateMode, TypedefTargetObjects, NoOrder, "37.12; 37.85">;
def ScopeLets : VPITraversalEdge<ScopeSource, LetDecl, IterateMode,
  LetDeclObjects, NoOrder, "37.12">;

// NULL-root relations.
def RootUserSystfs : VPITraversalEdge<RootSource, UserSystf, IterateMode,
  UserSystfObjects, NoOrder, "37.42">;
def RootCurrentSysTfCall : VPITraversalEdge<RootSource, SysTfCallRel,
  HandleMode, SystemTfCallObjects, NoOrder, "37.42">;
def RootCurrentFrame : VPITraversalEdge<RootSource, Frame, HandleMode,
  FrameObjects, NoOrder, "37.43">;
def RootCallbacks : VPITraversalEdge<RootSource, Callback, IterateMode,
  CallbackObjects, NoOrder, "37.80">;
def RootTimeQueues : VPITraversalEdge<RootSource, TimeQueue, IterateMode,
  TimeQueueObjects, TimeOrder, "37.81">;
def RootActiveTimeFormat : VPITraversalEdge<RootSource, ActiveTimeFormatRel,
  HandleMode, SysTaskCallObjects, NoOrder, "37.82">;

include "VPITraversalEarly.td"
include "VPITraversalLate.td"

// 37.10 details 2-3 apply to retained expression occurrences as well as
// declarations. Their exact source instance comes from the occurrence binding.
def RetainedComputedExpressions : VPIObjectSet<"RetainedComputedExpressions",
  [Constant, Operation, PartSelect, IndexedPartSelect]>;
def RetainedExpressionModule : VPITraversalEdge<RetainedComputedExpressions,
  Module, HandleMode, ModuleTarget, NoOrder, "37.10">;
def RetainedExpressionInstance : VPITraversalEdge<RetainedComputedExpressions,
  InstanceRel, HandleMode, InstanceObjects, NoOrder, "37.10">;

// Access-by-index policies from the arrows labelled "access by index" in the
// Chapter 37 diagrams.  Physical and semantic type records decide whether a
// particular object is actually indexable and which member kind is produced;
// these exact sets constrain that result without baking the VPI object graph
// into runtime code.
def IndexedPortSource : VPIObjectSet<"IndexedPortSource", [Port]>;
def IndexedNetSources : VPIObjectSet<"IndexedNetSources",
  [Net, NetArray, EnumNet, IntegerNet, TimeNet, UnionNet, ShortRealNet,
   RealNet, ByteNet, ShortIntNet, IntNet, LongIntNet, BitNet,
   InterconnectNet, StructNet, PackedArrayNet]>;
def IndexedNetResults : VPIObjectSet<"IndexedNetResults", [], [NetFamily],
  [InterconnectArray]>;
def IndexedInterconnectArraySource : VPIObjectSet<
  "IndexedInterconnectArraySource", [InterconnectArray]>;
def IndexedVariableSources : VPIObjectSet<"IndexedVariableSources",
  [ShortRealVar, RealVar, ByteVar, ShortIntVar, IntVar, LongIntVar,
   IntegerVar, TimeVar, RegArray, PackedArrayVar, BitVar, Reg, StructVar,
   UnionVar, EnumVar, StringVar, ChandleVar, ClassVar,
   VirtualInterfaceVar]>;
def IndexedPort : VPIIndexedAccess<IndexedPortSource, PortElementAccess,
  PortObjects, PortBit, Port, Port, 0,
  "37.14; 38.19; 38.20">;
def IndexedNet : VPIIndexedAccess<IndexedNetSources, NetElementAccess,
  IndexedNetResults, NetBit, NetArray, Net, 1, "37.16; 38.19; 38.20">;
def IndexedInterconnectArray : VPIRelationIndexedAccess<
  VPIEarlyInterconnectArrayElements, VPIEarlyInterconnectArrayElementTargets,
  InterconnectArray, InterconnectNet, "37.16; 37.24; 38.19; 38.20">;
def IndexedVariable : VPIIndexedAccess<IndexedVariableSources,
  VariableElementAccess, VPIEarlyVariables, RegBit, RegArray, Reg, 1,
  "37.17; 37.20; 38.19; 38.20">;

def IndexedModuleArrayResults : VPIObjectSet<"IndexedModuleArrayResults",
  [ModuleArray, Module]>;
def IndexedInterfaceArrayResults : VPIObjectSet<
  "IndexedInterfaceArrayResults", [InterfaceArray, Interface]>;
def IndexedProgramArrayResults : VPIObjectSet<"IndexedProgramArrayResults",
  [ProgramArray, Program]>;
def IndexedGateArrayResults : VPIObjectSet<"IndexedGateArrayResults",
  [GateArray, Gate]>;
def IndexedSwitchArrayResults : VPIObjectSet<"IndexedSwitchArrayResults",
  [SwitchArray, Switch]>;
def IndexedUdpArrayResults : VPIObjectSet<"IndexedUdpArrayResults",
  [UdpArray, Udp]>;
def IndexedNamedEventArrayResults : VPIObjectSet<
  "IndexedNamedEventArrayResults", [NamedEventArray, NamedEvent]>;
def IndexedGenScopeArrayResults : VPIObjectSet<
  "IndexedGenScopeArrayResults", [GenScopeArray, GenScope]>;

def IndexedModuleArray : VPIRelationIndexedAccess<
  VPIEarlyModuleArrayElements, IndexedModuleArrayResults, ModuleArray, Module,
  "37.5; 37.11; 38.19; 38.20">;
def IndexedInterfaceArray : VPIRelationIndexedAccess<
  VPIEarlyArrayInterfaces, IndexedInterfaceArrayResults, InterfaceArray,
  Interface, "37.6; 37.11; 38.19; 38.20">;
def IndexedProgramArray : VPIRelationIndexedAccess<
  VPIEarlyArrayPrograms, IndexedProgramArrayResults, ProgramArray, Program,
  "37.9; 37.11; 38.19; 38.20">;
def IndexedGateArray : VPIRelationIndexedAccess<
  VPIEarlyGateArrayElements, IndexedGateArrayResults, GateArray, Gate,
  "37.11; 37.35; 38.19; 38.20">;
def IndexedSwitchArray : VPIRelationIndexedAccess<
  VPIEarlySwitchArrayElements, IndexedSwitchArrayResults, SwitchArray, Switch,
  "37.11; 37.35; 38.19; 38.20">;
def IndexedUdpArray : VPIRelationIndexedAccess<
  VPIEarlyUdpArrayElements, IndexedUdpArrayResults, UdpArray, Udp,
  "37.11; 37.35; 38.19; 38.20">;
def IndexedNamedEventArray : VPIRelationIndexedAccess<
  VPIEarlyNamedEventArrayElements, IndexedNamedEventArrayResults,
  NamedEventArray, NamedEvent, "37.27; 38.19; 38.20">;
def IndexedGenScopeArray : VPIRelationIndexedAccess<
  LateGenScopeArrayElements, IndexedGenScopeArrayResults, GenScopeArray,
  GenScope, "37.85; 38.19; 38.20">;

multiclass IndexedValueTypeResult<VPIObjectKind typespec,
                                  VPIObjectKind variable,
                                  VPIObjectKind net, string clause> {
  def Var : VPIIndexedTypeResult<VariableElementAccess, typespec, variable,
                                 clause>;
  def Net : VPIIndexedTypeResult<NetElementAccess, typespec, net, clause>;
}

defm IndexedLongInt : IndexedValueTypeResult<LongIntTypespec, LongIntVar,
  LongIntNet, "37.16; 37.17">;
defm IndexedShortReal : IndexedValueTypeResult<ShortRealTypespec, ShortRealVar,
  ShortRealNet, "37.16; 37.17">;
defm IndexedByte : IndexedValueTypeResult<ByteTypespec, ByteVar, ByteNet,
  "37.16; 37.17">;
defm IndexedShortInt : IndexedValueTypeResult<ShortIntTypespec, ShortIntVar,
  ShortIntNet, "37.16; 37.17">;
defm IndexedInt : IndexedValueTypeResult<IntTypespec, IntVar, IntNet,
  "37.16; 37.17">;
defm IndexedEnum : IndexedValueTypeResult<EnumTypespec, EnumVar, EnumNet,
  "37.16; 37.17">;
defm IndexedInteger : IndexedValueTypeResult<IntegerTypespec, IntegerVar,
  IntegerNet, "37.16; 37.17">;
defm IndexedTime : IndexedValueTypeResult<TimeTypespec, TimeVar, TimeNet,
  "37.16; 37.17">;
defm IndexedReal : IndexedValueTypeResult<RealTypespec, RealVar, RealNet,
  "37.16; 37.17">;
defm IndexedStruct : IndexedValueTypeResult<StructTypespec, StructVar,
  StructNet, "37.16; 37.17">;
defm IndexedUnion : IndexedValueTypeResult<UnionTypespec, UnionVar, UnionNet,
  "37.16; 37.17">;
defm IndexedBit : IndexedValueTypeResult<BitTypespec, BitVar, BitNet,
  "37.16; 37.17">;
defm IndexedLogic : IndexedValueTypeResult<LogicTypespec, Reg, Net,
  "37.16; 37.17">;
defm IndexedArray : IndexedValueTypeResult<ArrayTypespec, RegArray, NetArray,
  "37.16; 37.17">;
defm IndexedPackedArray : IndexedValueTypeResult<PackedArrayTypespec,
  PackedArrayVar, PackedArrayNet, "37.16; 37.18">;

def IndexedClassVar : VPIIndexedTypeResult<VariableElementAccess,
  ClassTypespec, ClassVar, "37.17">;
def IndexedStringVar : VPIIndexedTypeResult<VariableElementAccess,
  StringTypespec, StringVar, "37.17">;
def IndexedChandleVar : VPIIndexedTypeResult<VariableElementAccess,
  ChandleTypespec, ChandleVar, "37.17">;
def IndexedInterfaceVar : VPIIndexedTypeResult<VariableElementAccess,
  InterfaceTypespec, VirtualInterfaceVar, "37.17">;

include "VPIValueModel.td"

// Exact pure source operators represented by immutable default-expression
// anchors. The source enums stay authoritative; this table supplies the IEEE
// VPI encoding and Table 11-2 precedence for verification and decompilation.
class VPISourceOperator<string name_, int value_, int arity_, string token_,
                        int precedence_, bit rightAssociative_ = 0> {
  string sourceName = name_;
  int value = value_;
  int arity = arity_;
  string token = token_;
  int precedence = precedence_;
  bit rightAssociative = rightAssociative_;
}
def SourceOperatorMinus : VPISourceOperator<"minus", 1, 1, "-", 14>;
def SourceOperatorPlus : VPISourceOperator<"plus", 2, 1, "+", 14>;
def SourceOperatorLogicalNot : VPISourceOperator<"logical_not", 3, 1, "!", 14>;
def SourceOperatorBitwiseNot : VPISourceOperator<"bitwise_not", 4, 1, "~", 14>;
def SourceOperatorBitwiseAnd : VPISourceOperator<"bitwise_and", 5, 1, "&", 14>;
def SourceOperatorBitwiseNand : VPISourceOperator<"bitwise_nand", 6, 1, "~&", 14>;
def SourceOperatorBitwiseOr : VPISourceOperator<"bitwise_or", 7, 1, "|", 14>;
def SourceOperatorBitwiseNor : VPISourceOperator<"bitwise_nor", 8, 1, "~|", 14>;
def SourceOperatorBitwiseXor : VPISourceOperator<"bitwise_xor", 9, 1, "^", 14>;
def SourceOperatorBitwiseXnor : VPISourceOperator<"bitwise_xnor", 10, 1, "~^", 14>;
def SourceOperatorSubtract : VPISourceOperator<"subtract", 11, 2, "-", 11>;
def SourceOperatorDivide : VPISourceOperator<"divide", 12, 2, "/", 12>;
def SourceOperatorMod : VPISourceOperator<"mod", 13, 2, "%", 12>;
def SourceOperatorEquality : VPISourceOperator<"equality", 14, 2, "==", 8>;
def SourceOperatorInequality : VPISourceOperator<"inequality", 15, 2, "!=", 8>;
def SourceOperatorCaseEquality : VPISourceOperator<"case_equality", 16, 2, "===", 8>;
def SourceOperatorCaseInequality : VPISourceOperator<"case_inequality", 17, 2, "!==", 8>;
def SourceOperatorGreaterThan : VPISourceOperator<"greater_than", 18, 2, ">", 9>;
def SourceOperatorGreaterThanEqual : VPISourceOperator<"greater_than_equal", 19, 2, ">=", 9>;
def SourceOperatorLessThan : VPISourceOperator<"less_than", 20, 2, "<", 9>;
def SourceOperatorLessThanEqual : VPISourceOperator<"less_than_equal", 21, 2, "<=", 9>;
def SourceOperatorLogicalShiftLeft : VPISourceOperator<"logical_shift_left", 22, 2, "<<", 10>;
def SourceOperatorLogicalShiftRight : VPISourceOperator<"logical_shift_right", 23, 2, ">>", 10>;
def SourceOperatorAdd : VPISourceOperator<"add", 24, 2, "+", 11>;
def SourceOperatorMultiply : VPISourceOperator<"multiply", 25, 2, "*", 12>;
def SourceOperatorLogicalAnd : VPISourceOperator<"logical_and", 26, 2, "&&", 4>;
def SourceOperatorLogicalOr : VPISourceOperator<"logical_or", 27, 2, "||", 3>;
def SourceOperatorBinaryAnd : VPISourceOperator<"binary_and", 28, 2, "&", 7>;
def SourceOperatorBinaryOr : VPISourceOperator<"binary_or", 29, 2, "|", 5>;
def SourceOperatorBinaryXor : VPISourceOperator<"binary_xor", 30, 2, "^", 6>;
def SourceOperatorBinaryXnor : VPISourceOperator<"binary_xnor", 31, 2, "~^", 6>;
def SourceOperatorConditional : VPISourceOperator<"conditional", 32, 3, "?:", 2, 1>;
def SourceOperatorArithmeticShiftLeft : VPISourceOperator<"arithmetic_shift_left", 41, 2, "<<<", 10>;
def SourceOperatorArithmeticShiftRight : VPISourceOperator<"arithmetic_shift_right", 42, 2, ">>>", 10>;
def SourceOperatorPower : VPISourceOperator<"power", 43, 2, "**", 13>;
def SourceOperatorLogicalImplication : VPISourceOperator<"logical_implication", 50, 2, "->", 1, 1>;
def SourceOperatorWildcardEquality : VPISourceOperator<"wildcard_equality", 69, 2, "==?", 8>;
def SourceOperatorWildcardInequality : VPISourceOperator<"wildcard_inequality", 70, 2, "!=?", 8>;
