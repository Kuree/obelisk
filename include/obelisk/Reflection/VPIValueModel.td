//===- VPIValueModel.td - IEEE VPI value-query contract -----------------===//
//
// This file is included after the Chapter 37 object and traversal inventory.
// It defines the exact object kinds for which vpi_get_value is legal, the
// ordinary scalar formats accepted by each kind, and whether the query is a
// passive snapshot or requires expression evaluation.  Shape- and
// origin-dependent exclusions are retained as generated runtime requirements.
//
//===----------------------------------------------------------------------===//

class VPIValueFormat<string cppName_, int value_> {
  string cppName = cppName_;
  int value = value_;
}

// IEEE 1800-2017 Table 38-3.  The array-query formats from 38.16 and the
// callback-only suppression format from Table 38-5 are not part of this model
// and must never be accepted by scalar vpi_get_value.
def BinStrValueFormat : VPIValueFormat<"BinStr", 1>;
def OctStrValueFormat : VPIValueFormat<"OctStr", 2>;
def DecStrValueFormat : VPIValueFormat<"DecStr", 3>;
def HexStrValueFormat : VPIValueFormat<"HexStr", 4>;
def ScalarValueFormat : VPIValueFormat<"Scalar", 5>;
def IntValueFormat : VPIValueFormat<"Int", 6>;
def RealValueFormat : VPIValueFormat<"Real", 7>;
def StringValueFormat : VPIValueFormat<"String", 8>;
def VectorValueFormat : VPIValueFormat<"Vector", 9>;
def StrengthValueFormat : VPIValueFormat<"Strength", 10>;
def TimeValueFormat : VPIValueFormat<"Time", 11>;
def ObjTypeValueFormat : VPIValueFormat<"ObjType", 12>;

class VPIValueDefaultFormat<string cppName_, int value_> {
  string cppName = cppName_;
  int value = value_;
}

// Semantic delegates to the reflection type descriptor.  ScalarOrVector uses
// the object's scalar/vector shape.  The remaining values are fixed by the
// exact Chapter 37 object kind.
def SemanticValueDefault : VPIValueDefaultFormat<"Semantic", 0>;
def ScalarOrVectorValueDefault : VPIValueDefaultFormat<"ScalarOrVector", 1>;
def IntegerValueDefault : VPIValueDefaultFormat<"Integer", 2>;
def RealValueDefault : VPIValueDefaultFormat<"Real", 3>;
def StringValueDefault : VPIValueDefaultFormat<"String", 4>;
def TimeValueDefault : VPIValueDefaultFormat<"Time", 5>;

class VPIValueReadSemantics<string cppName_, int value_> {
  string cppName = cppName_;
  int value = value_;
}

def SnapshotValueRead : VPIValueReadSemantics<"Snapshot", 0>;
def EvaluateValueRead : VPIValueReadSemantics<"Evaluate", 1>;

// Values are bit flags because more than one dynamic restriction can apply to
// an exact object kind.  These are checked only by an explicit query and add
// no scheduler or execution-path work.
class VPIValueRequirement<string cppName_, int value_> {
  string cppName = cppName_;
  int value = value_;
}

def RejectWholeUnpackedValue
    : VPIValueRequirement<"RejectWholeUnpacked", 1>;
def RejectClassDefinitionOriginValue
    : VPIValueRequirement<"RejectClassDefinitionOrigin", 2>;
def RejectNonStaticClassTypespecOriginValue
    : VPIValueRequirement<"RejectNonStaticClassTypespecOrigin", 4>;
def RestrictStringConstantValue
    : VPIValueRequirement<"RestrictStringConstant", 8>;

class VPIValuePolicy<VPIObjectSet sources_, list<VPIValueFormat> formats_,
                     VPIValueDefaultFormat defaultFormat_,
                     VPIValueReadSemantics readSemantics_,
                     list<VPIValueRequirement> requirements_, string clause_> {
  VPIObjectSet sources = sources_;
  list<VPIValueFormat> formats = formats_;
  VPIValueDefaultFormat defaultFormat = defaultFormat_;
  VPIValueReadSemantics readSemantics = readSemantics_;
  list<VPIValueRequirement> requirements = requirements_;
  string clause = clause_;
}

// TableGen does not have a first-class list alias, so the common list is kept
// as a base class inherited by each policy below.
class OrdinaryVPIValuePolicy<VPIObjectSet sources_,
                             VPIValueDefaultFormat defaultFormat_,
                             VPIValueReadSemantics readSemantics_,
                             list<VPIValueRequirement> requirements_,
                             string clause_>
    : VPIValuePolicy<sources_,
        [BinStrValueFormat, OctStrValueFormat, DecStrValueFormat,
         HexStrValueFormat, ScalarValueFormat, IntValueFormat,
         RealValueFormat, StringValueFormat, VectorValueFormat,
         StrengthValueFormat, TimeValueFormat, ObjTypeValueFormat],
        defaultFormat_, readSemantics_, requirements_, clause_>;

def LogicNetValueObjects : VPIObjectSet<"LogicNetValueObjects",
  [Net, NetBit, BitNet, InterconnectNet, PackedArrayNet]>;
def LogicVariableValueObjects : VPIObjectSet<"LogicVariableValueObjects",
  [Reg, RegBit, BitVar, PackedArrayVar]>;
def SemanticNetValueObjects
    : VPIObjectSet<"SemanticNetValueObjects", [EnumNet]>;
def SemanticVariableValueObjects
    : VPIObjectSet<"SemanticVariableValueObjects", [EnumVar, ChandleVar]>;
def IntegerNetValueObjects : VPIObjectSet<"IntegerNetValueObjects",
  [IntegerNet, ByteNet, ShortIntNet, IntNet, LongIntNet]>;
def IntegerVariableValueObjects
    : VPIObjectSet<"IntegerVariableValueObjects",
        [IntegerVar, ByteVar, ShortIntVar, IntVar, LongIntVar]>;
def RealNetValueObjects : VPIObjectSet<"RealNetValueObjects",
  [ShortRealNet, RealNet]>;
def RealVariableValueObjects : VPIObjectSet<"RealVariableValueObjects",
  [ShortRealVar, RealVar]>;
def TimeNetValueObjects : VPIObjectSet<"TimeNetValueObjects", [TimeNet]>;
def TimeVariableValueObjects
    : VPIObjectSet<"TimeVariableValueObjects", [TimeVar]>;
def StringStateValueObjects : VPIObjectSet<"StringStateValueObjects",
  [StringVar]>;
def AggregateNetValueObjects : VPIObjectSet<"AggregateNetValueObjects",
  [StructNet, UnionNet]>;
def AggregateVariableValueObjects
    : VPIObjectSet<"AggregateVariableValueObjects", [StructVar, UnionVar]>;

def LogicNetValues : OrdinaryVPIValuePolicy<LogicNetValueObjects,
  ScalarOrVectorValueDefault, SnapshotValueRead, [],
  "37.16; 38.15">;
def LogicVariableValues : OrdinaryVPIValuePolicy<LogicVariableValueObjects,
  ScalarOrVectorValueDefault, SnapshotValueRead,
  [RejectClassDefinitionOriginValue,
   RejectNonStaticClassTypespecOriginValue], "37.17; 38.15">;
def SemanticNetValues : OrdinaryVPIValuePolicy<SemanticNetValueObjects,
  SemanticValueDefault, SnapshotValueRead, [], "37.16; 38.15">;
def SemanticVariableValues
    : OrdinaryVPIValuePolicy<SemanticVariableValueObjects,
        SemanticValueDefault, SnapshotValueRead,
        [RejectClassDefinitionOriginValue,
         RejectNonStaticClassTypespecOriginValue], "37.17; 38.15">;
def IntegerNetValues : OrdinaryVPIValuePolicy<IntegerNetValueObjects,
  IntegerValueDefault, SnapshotValueRead, [], "37.16; 38.15">;
def IntegerVariableValues
    : OrdinaryVPIValuePolicy<IntegerVariableValueObjects,
        IntegerValueDefault, SnapshotValueRead,
        [RejectClassDefinitionOriginValue,
         RejectNonStaticClassTypespecOriginValue], "37.17; 38.15">;
def RealNetValues : OrdinaryVPIValuePolicy<RealNetValueObjects,
  RealValueDefault, SnapshotValueRead, [], "37.16; 38.15">;
def RealVariableValues : OrdinaryVPIValuePolicy<RealVariableValueObjects,
  RealValueDefault, SnapshotValueRead,
  [RejectClassDefinitionOriginValue,
   RejectNonStaticClassTypespecOriginValue], "37.17; 38.15">;
def TimeNetValues : OrdinaryVPIValuePolicy<TimeNetValueObjects,
  TimeValueDefault, SnapshotValueRead, [], "37.16; 38.15">;
def TimeVariableValues : OrdinaryVPIValuePolicy<TimeVariableValueObjects,
  TimeValueDefault, SnapshotValueRead,
  [RejectClassDefinitionOriginValue,
   RejectNonStaticClassTypespecOriginValue], "37.17; 38.15">;
def StringStateValues : OrdinaryVPIValuePolicy<StringStateValueObjects,
  StringValueDefault, SnapshotValueRead,
  [RejectClassDefinitionOriginValue,
   RejectNonStaticClassTypespecOriginValue], "37.17; 38.15">;
def AggregateNetValues : OrdinaryVPIValuePolicy<AggregateNetValueObjects,
  ScalarOrVectorValueDefault, SnapshotValueRead,
  [RejectWholeUnpackedValue], "37.16; 38.15">;
def AggregateVariableValues
    : OrdinaryVPIValuePolicy<AggregateVariableValueObjects,
        ScalarOrVectorValueDefault, SnapshotValueRead,
        [RejectWholeUnpackedValue, RejectClassDefinitionOriginValue,
         RejectNonStaticClassTypespecOriginValue], "37.17; 37.24; 38.15">;

def EvaluatedValueObjects : VPIObjectSet<"EvaluatedValueObjects",
  [VarSelect, BitSelect, PartSelect, IndexedPartSelect, Operation,
   FuncCall, MethodFuncCall, SysFuncCall, LetExpr]>;
def ConstantValueObjects : VPIObjectSet<"ConstantValueObjects", [Constant]>;
def ImmutableValueObjects : VPIObjectSet<"ImmutableValueObjects",
  [Parameter, SpecParam, EnumConst, Attribute]>;
def PrimitiveValueObjects : VPIObjectSet<"PrimitiveValueObjects",
  [Gate, Switch, Udp, PrimTerm, DelayTerm, ContAssign, ContAssignBit]>;
def FsmStateValueObjects : VPIObjectSet<"FsmStateValueObjects", [FsmHandle]>;
def TableEntryValueObjects : VPIObjectSet<"TableEntryValueObjects",
  [TableEntry]>;

def EvaluatedValues : OrdinaryVPIValuePolicy<EvaluatedValueObjects,
  SemanticValueDefault, EvaluateValueRead,
  [RejectClassDefinitionOriginValue,
   RejectNonStaticClassTypespecOriginValue], "37.19; 37.57; 38.15">;
def ConstantValues : OrdinaryVPIValuePolicy<ConstantValueObjects,
  SemanticValueDefault, SnapshotValueRead, [RestrictStringConstantValue],
  "37.57; 38.15">;
def ImmutableValues : OrdinaryVPIValuePolicy<ImmutableValueObjects,
  SemanticValueDefault, SnapshotValueRead, [],
  "37.23; 37.26; 37.81; 38.15">;
def PrimitiveValues : OrdinaryVPIValuePolicy<PrimitiveValueObjects,
  SemanticValueDefault, SnapshotValueRead, [],
  "37.33; 37.43; 37.45; 38.15">;
def FsmStateValues : OrdinaryVPIValuePolicy<FsmStateValueObjects,
  SemanticValueDefault, SnapshotValueRead, [], "40.5.3">;
def TableEntryValues : VPIValuePolicy<TableEntryValueObjects,
  [StringValueFormat, VectorValueFormat], StringValueDefault,
  SnapshotValueRead, [], "37.34; 38.15">;
