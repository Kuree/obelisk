//===- CoverageDatabase.td - native coverage database schema ------------===//

class CoverageEnumValue<string cppName_, int value_> {
  string cppName = cppName_;
  int value = value_;
}

class CoverageMetric<string cppName_, int value_>
    : CoverageEnumValue<cppName_, value_>;

def MetricLine       : CoverageMetric<"Line", 1>;
def MetricToggle     : CoverageMetric<"Toggle", 2>;
def MetricFunctional : CoverageMetric<"Functional", 3>;
def MetricFSM        : CoverageMetric<"FSM", 4>;
def MetricAssertion  : CoverageMetric<"Assertion", 5>;
def MetricBranch     : CoverageMetric<"Branch", 6>;
def MetricMCDC       : CoverageMetric<"MCDC", 7>;

class CoverageToggleDimensionKind<string cppName_, int value_>
    : CoverageEnumValue<cppName_, value_>;

def ToggleRoot           : CoverageToggleDimensionKind<"Root", 1>;
def ToggleScalar         : CoverageToggleDimensionKind<"Scalar", 2>;
def ToggleEnum           : CoverageToggleDimensionKind<"Enum", 3>;
def TogglePackedArray    : CoverageToggleDimensionKind<"PackedArray", 4>;
def ToggleUnpackedArray  : CoverageToggleDimensionKind<"UnpackedArray", 5>;
def TogglePackedStruct   : CoverageToggleDimensionKind<"PackedStruct", 6>;
def ToggleUnpackedStruct : CoverageToggleDimensionKind<"UnpackedStruct", 7>;
def TogglePackedUnion    : CoverageToggleDimensionKind<"PackedUnion", 8>;
def ToggleUnpackedUnion  : CoverageToggleDimensionKind<"UnpackedUnion", 9>;
def ToggleField          : CoverageToggleDimensionKind<"Field", 10>;
def ToggleTag            : CoverageToggleDimensionKind<"Tag", 11>;

class CoverageFunctionalItemKind<string cppName_, int value_>
    : CoverageEnumValue<cppName_, value_>;

def FunctionalCoverpoint : CoverageFunctionalItemKind<"Coverpoint", 1>;
def FunctionalCross      : CoverageFunctionalItemKind<"Cross", 2>;

class CoverageFunctionalBinKind<string cppName_, int value_>
    : CoverageEnumValue<cppName_, value_>;

def FunctionalStateBin      : CoverageFunctionalBinKind<"State", 1>;
def FunctionalTransitionBin : CoverageFunctionalBinKind<"Transition", 2>;
def FunctionalCrossBin      : CoverageFunctionalBinKind<"Cross", 3>;

class CoverageFunctionalSourceRole<string cppName_, int value_>
    : CoverageEnumValue<cppName_, value_>;

def FunctionalExpandedSource   : CoverageFunctionalSourceRole<"Expanded", 1>;
def FunctionalOriginalSource   : CoverageFunctionalSourceRole<"Original", 2>;
def FunctionalMacroDefinition  : CoverageFunctionalSourceRole<"MacroDefinition", 3>;
def FunctionalMacroInvocation  : CoverageFunctionalSourceRole<"MacroInvocation", 4>;

class CoverageTransitionRepetitionKind<string cppName_, int value_>
    : CoverageEnumValue<cppName_, value_>;

def TransitionOnce           : CoverageTransitionRepetitionKind<"Once", 1>;
def TransitionConsecutive    : CoverageTransitionRepetitionKind<"Consecutive", 2>;
def TransitionGoto           : CoverageTransitionRepetitionKind<"Goto", 3>;
def TransitionNonconsecutive : CoverageTransitionRepetitionKind<"Nonconsecutive", 4>;

class CoverageValueSetKind<string cppName_, int value_>
    : CoverageEnumValue<cppName_, value_>;

def ValueSetIntegral : CoverageValueSetKind<"Integral", 1>;
def ValueSetReal     : CoverageValueSetKind<"Real", 2>;

class CoverageValueAtomKind<string cppName_, int value_>
    : CoverageEnumValue<cppName_, value_>;

def ValueAtomIntegralValue : CoverageValueAtomKind<"IntegralValue", 1>;
def ValueAtomIntegralRange : CoverageValueAtomKind<"IntegralRange", 2>;
def ValueAtomRealInterval  : CoverageValueAtomKind<"RealInterval", 3>;

class CoverageCrossRetainAutoPolicy<string cppName_, int value_>
    : CoverageEnumValue<cppName_, value_>;

// This is the compiler-resolved policy, not a language-version default.
def CrossDiscardAutoBins : CoverageCrossRetainAutoPolicy<"Discard", 1>;
def CrossRetainAutoBins  : CoverageCrossRetainAutoPolicy<"Retain", 2>;
def CrossDeferredAutoBins: CoverageCrossRetainAutoPolicy<"Deferred", 3>;

class CoverageCrossSelectorKind<string cppName_, int value_>
    : CoverageEnumValue<cppName_, value_>;

def CrossSelectorBinsof    : CoverageCrossSelectorKind<"Binsof", 1>;
def CrossSelectorNot       : CoverageCrossSelectorKind<"Not", 2>;
def CrossSelectorAnd       : CoverageCrossSelectorKind<"And", 3>;
def CrossSelectorOr        : CoverageCrossSelectorKind<"Or", 4>;
def CrossSelectorSet       : CoverageCrossSelectorKind<"Set", 5>;
def CrossSelectorAllTuples : CoverageCrossSelectorKind<"AllTuples", 6>;
def CrossSelectorWith      : CoverageCrossSelectorKind<"With", 7>;

class CoverageExpressionOwnerKind<string cppName_, int value_>
    : CoverageEnumValue<cppName_, value_>;
def ExpressionOwnerType     : CoverageExpressionOwnerKind<"Type", 1>;
def ExpressionOwnerItem     : CoverageExpressionOwnerKind<"Item", 2>;
def ExpressionOwnerBin      : CoverageExpressionOwnerKind<"Bin", 3>;
def ExpressionOwnerSelector : CoverageExpressionOwnerKind<"Selector", 4>;
def ExpressionOwnerValueSet : CoverageExpressionOwnerKind<"ValueSet", 5>;
def ExpressionOwnerFormal   : CoverageExpressionOwnerKind<"Formal", 6>;

class CoverageExpressionRole<string cppName_, int value_>
    : CoverageEnumValue<cppName_, value_>;
def ExpressionSamplingEvent   : CoverageExpressionRole<"SamplingEvent", 1>;
def ExpressionCoverpointSample: CoverageExpressionRole<"CoverpointSample", 2>;
def ExpressionCoverpointIff   : CoverageExpressionRole<"CoverpointIff", 3>;
def ExpressionCrossIff        : CoverageExpressionRole<"CrossIff", 4>;
def ExpressionBinIff          : CoverageExpressionRole<"BinIff", 5>;
def ExpressionBinSet          : CoverageExpressionRole<"BinSet", 6>;
def ExpressionBinWith         : CoverageExpressionRole<"BinWith", 7>;
def ExpressionArrayCardinality: CoverageExpressionRole<"ArrayCardinality", 8>;
def ExpressionRepeatLower     : CoverageExpressionRole<"RepeatLower", 9>;
def ExpressionRepeatUpper     : CoverageExpressionRole<"RepeatUpper", 10>;
def ExpressionSelectorWith    : CoverageExpressionRole<"SelectorWith", 11>;
def ExpressionSelectorMatches : CoverageExpressionRole<"SelectorMatches", 12>;
def ExpressionOptionRHS       : CoverageExpressionRole<"OptionRHS", 13>;
def ExpressionValueAtomSingleton : CoverageExpressionRole<"ValueAtomSingleton", 14>;
def ExpressionValueAtomLower     : CoverageExpressionRole<"ValueAtomLower", 15>;
def ExpressionValueAtomUpper     : CoverageExpressionRole<"ValueAtomUpper", 16>;
def ExpressionFormalDefault      : CoverageExpressionRole<"FormalDefault", 17>;

class CoverageFunctionalFormalKind<string cppName_, int value_>
    : CoverageEnumValue<cppName_, value_>;
def FormalConstructor : CoverageFunctionalFormalKind<"Constructor", 1>;
def FormalSample      : CoverageFunctionalFormalKind<"Sample", 2>;

class CoverageFunctionalFormalDirection<string cppName_, int value_>
    : CoverageEnumValue<cppName_, value_>;
def FormalInput : CoverageFunctionalFormalDirection<"Input", 1>;
def FormalRef   : CoverageFunctionalFormalDirection<"Ref", 2>;

class CoverageOptionScopeKind<string cppName_, int value_>
    : CoverageEnumValue<cppName_, value_>;
def OptionInstanceScope : CoverageOptionScopeKind<"Instance", 1>;
def OptionTypeScope     : CoverageOptionScopeKind<"Type", 2>;

class CoverageExpressionResultKind<string cppName_, int value_>
    : CoverageEnumValue<cppName_, value_>;
def ExpressionBoolean  : CoverageExpressionResultKind<"Boolean", 1>;
def ExpressionIntegral : CoverageExpressionResultKind<"Integral", 2>;
def ExpressionReal     : CoverageExpressionResultKind<"Real", 3>;
def ExpressionSet      : CoverageExpressionResultKind<"Set", 4>;
def ExpressionTupleSet : CoverageExpressionResultKind<"TupleSet", 5>;
def ExpressionString   : CoverageExpressionResultKind<"String", 6>;
def ExpressionTupleQueue : CoverageExpressionResultKind<"TupleQueue", 7>;

class CoverageSignedness<string cppName_, int value_>
    : CoverageEnumValue<cppName_, value_>;
def CoverageUnsigned     : CoverageSignedness<"Unsigned", 1>;
def CoverageSigned       : CoverageSignedness<"Signed", 2>;
def CoverageNotApplicable: CoverageSignedness<"NotApplicable", 3>;

class CoverageBinArrayMode<string cppName_, int value_>
    : CoverageEnumValue<cppName_, value_>;
def BinArrayScalar  : CoverageBinArrayMode<"Scalar", 1>;
def BinArrayUnsized : CoverageBinArrayMode<"Unsized", 2>;
def BinArrayFixed   : CoverageBinArrayMode<"Fixed", 3>;

class CoverageBinDistributionKind<string cppName_, int value_>
    : CoverageEnumValue<cppName_, value_>;
def BinDistributionNone            : CoverageBinDistributionKind<"None", 1>;
def BinDistributionPerValue        : CoverageBinDistributionKind<"PerValue", 2>;
def BinDistributionUniform         : CoverageBinDistributionKind<"Uniform", 3>;

class CoverageCrossMatchesPolicy<string cppName_, int value_>
    : CoverageEnumValue<cppName_, value_>;
def CrossMatchesNone  : CoverageCrossMatchesPolicy<"None", 1>;
def CrossMatchesCount : CoverageCrossMatchesPolicy<"Count", 2>;
def CrossMatchesAll   : CoverageCrossMatchesPolicy<"All", 3>;

class CoverageConfigurationOptionKind<string cppName_, int value_>
    : CoverageEnumValue<cppName_, value_>;
def ConfigGoal                 : CoverageConfigurationOptionKind<"Goal", 1>;
def ConfigWeight               : CoverageConfigurationOptionKind<"Weight", 2>;
def ConfigAtLeast              : CoverageConfigurationOptionKind<"AtLeast", 3>;
def ConfigAutoBinMax           : CoverageConfigurationOptionKind<"AutoBinMax", 4>;
def ConfigCrossNumPrintMissing : CoverageConfigurationOptionKind<"CrossNumPrintMissing", 5>;
def ConfigDetectOverlap        : CoverageConfigurationOptionKind<"DetectOverlap", 6>;
def ConfigPerInstance          : CoverageConfigurationOptionKind<"PerInstance", 7>;
def ConfigMergeInstances       : CoverageConfigurationOptionKind<"MergeInstances", 8>;
def ConfigGetInstCoverage      : CoverageConfigurationOptionKind<"GetInstCoverage", 9>;
def ConfigDistributeFirst      : CoverageConfigurationOptionKind<"DistributeFirst", 10>;
def ConfigRealInterval         : CoverageConfigurationOptionKind<"RealInterval", 11>;
def ConfigStrobe               : CoverageConfigurationOptionKind<"Strobe", 12>;
def ConfigName                 : CoverageConfigurationOptionKind<"Name", 13>;
def ConfigComment              : CoverageConfigurationOptionKind<"Comment", 14>;
def ConfigCrossRetainAutoBins  : CoverageConfigurationOptionKind<"CrossRetainAutoBins", 15>;

class CoverageConfigurationOptionOwnerKind<string cppName_, int value_>
    : CoverageEnumValue<cppName_, value_>;
def ConfigOwnerGroup : CoverageConfigurationOptionOwnerKind<"Group", 1>;
def ConfigOwnerItem  : CoverageConfigurationOptionOwnerKind<"Item", 2>;

class CoverageConfigurationValueKind<string cppName_, int value_>
    : CoverageEnumValue<cppName_, value_>;
def ConfigValueUnsigned : CoverageConfigurationValueKind<"Unsigned", 1>;
def ConfigValueSigned   : CoverageConfigurationValueKind<"Signed", 2>;
def ConfigValueBoolean  : CoverageConfigurationValueKind<"Boolean", 3>;
def ConfigValueString   : CoverageConfigurationValueKind<"String", 4>;
def ConfigValueRealBits : CoverageConfigurationValueKind<"RealBits", 5>;

class CoverageExpressionEvaluationPhase<string cppName_, int value_>
    : CoverageEnumValue<cppName_, value_>;
def ExpressionEvaluateConstructor : CoverageExpressionEvaluationPhase<"Constructor", 1>;
def ExpressionEvaluateSample      : CoverageExpressionEvaluationPhase<"Sample", 2>;
def ExpressionEvaluateEvent       : CoverageExpressionEvaluationPhase<"Event", 3>;
def ExpressionEvaluateOption      : CoverageExpressionEvaluationPhase<"Option", 4>;

class CoverageTupleElementMode<string cppName_, int value_>
    : CoverageEnumValue<cppName_, value_>;
def TupleElementsBins   : CoverageTupleElementMode<"BinTuple", 1>;
def TupleElementsValues : CoverageTupleElementMode<"ValueTuple", 2>;

class CoverageResolvedValueSetRole<string cppName_, int value_>
    : CoverageEnumValue<cppName_, value_>;
def ResolvedValueSetStateBin : CoverageResolvedValueSetRole<"StateBin", 1>;
def ResolvedValueSetTransitionStep
    : CoverageResolvedValueSetRole<"TransitionStep", 2>;
def ResolvedValueSetSelectorIntersection
    : CoverageResolvedValueSetRole<"SelectorIntersection", 3>;
def ResolvedValueSetTupleComponent
    : CoverageResolvedValueSetRole<"TupleComponent", 4>;

// Section values are the exact v1 wire ABI. The reader rejects every unknown
// section and every codec version other than v1.
class CoverageSection<string cppName_, int value_, int recordSize_,
                      bit required_ = 0, bit staticSchema_ = 0> {
  string cppName = cppName_;
  int value = value_;
  int recordSize = recordSize_;
  bit required = required_;
  bit staticSchema = staticSchema_;
}

def Strings                    : CoverageSection<"Strings", 1, 0, 1, 1>;
def SourceFiles                : CoverageSection<"SourceFiles", 2, 48, 0, 1>;
def Scopes                     : CoverageSection<"Scopes", 3, 32, 0, 1>;
def LinePoints                 : CoverageSection<"LinePoints", 4, 64, 0, 1>;
def ToggleObjects              : CoverageSection<"ToggleObjects", 5, 64, 0, 1>;
def ToggleTypeDimensions       : CoverageSection<"ToggleTypeDimensions", 6, 64, 0, 1>;
def FunctionalTypes            : CoverageSection<"FunctionalTypes", 7, 32, 0, 1>;
def FunctionalItems            : CoverageSection<"FunctionalItems", 8, 48, 0, 1>;
def FunctionalBins             : CoverageSection<"FunctionalBins", 9, 48, 0, 1>;
def TransitionPrograms         : CoverageSection<"TransitionPrograms", 10, 32, 0, 1>;
def CrossPlans                 : CoverageSection<"CrossPlans", 11, 64, 0, 1>;
def Exclusions                 : CoverageSection<"Exclusions", 12, 24, 0, 1>;
def Runs                       : CoverageSection<"Runs", 13, 64>;
def ResolvedCovergroupInstances: CoverageSection<"ResolvedCovergroupInstances", 14, 72>;
def Counters                   : CoverageSection<"Counters", 15, 56>;
def SparseCrossTuples          : CoverageSection<"SparseCrossTuples", 16, 56>;
def IllegalBinDiagnostics      : CoverageSection<"IllegalBinDiagnostics", 17, 56>;
def UserMetadata               : CoverageSection<"UserMetadata", 18, 24>;
def FunctionalSourceRanges     : CoverageSection<"FunctionalSourceRanges", 19, 64, 0, 1>;
def TransitionAlternatives     : CoverageSection<"TransitionAlternatives", 20, 32, 0, 1>;
def TransitionSteps            : CoverageSection<"TransitionSteps", 21, 64, 0, 1>;
def CrossTargets               : CoverageSection<"CrossTargets", 22, 48, 0, 1>;
def CrossBins                  : CoverageSection<"CrossBins", 23, 32, 0, 1>;
def CrossSelectorNodes         : CoverageSection<"CrossSelectorNodes", 24, 112, 0, 1>;
def CrossSelectorOperands      : CoverageSection<"CrossSelectorOperands", 25, 24, 0, 1>;
def SparseCrossTupleComponents : CoverageSection<"SparseCrossTupleComponents", 26, 8>;
def FunctionalValueSets        : CoverageSection<"FunctionalValueSets", 27, 56, 0, 1>;
def FunctionalValueAtoms       : CoverageSection<"FunctionalValueAtoms", 28, 64, 0, 1>;
def FunctionalValueLimbs       : CoverageSection<"FunctionalValueLimbs", 29, 56, 0, 1>;
def FunctionalExpressions      : CoverageSection<"FunctionalExpressions", 30, 88, 0, 1>;
def FunctionalConfigurations   : CoverageSection<"FunctionalConfigurations", 31, 48>;
def ResolvedFunctionalItems    : CoverageSection<"ResolvedFunctionalItems", 32, 96>;
def ResolvedFunctionalBins     : CoverageSection<"ResolvedFunctionalBins", 33, 104>;
def ResolvedTransitionSteps    : CoverageSection<"ResolvedTransitionSteps", 34, 88>;
def ResolvedCrossPlans         : CoverageSection<"ResolvedCrossPlans", 35, 72>;
def ResolvedCrossSelectorBindings : CoverageSection<"ResolvedCrossSelectorBindings", 36, 96>;
def ResolvedFunctionalValueSets : CoverageSection<"ResolvedFunctionalValueSets", 37, 120>;
def ResolvedFunctionalValueAtoms : CoverageSection<"ResolvedFunctionalValueAtoms", 38, 48>;
def ResolvedFunctionalValueLimbs : CoverageSection<"ResolvedFunctionalValueLimbs", 39, 56>;
def FunctionalBinPlans         : CoverageSection<"FunctionalBinPlans", 40, 56, 0, 1>;
def FunctionalTupleSets        : CoverageSection<"FunctionalTupleSets", 41, 40, 0, 1>;
def FunctionalTupleSetTuples   : CoverageSection<"FunctionalTupleSetTuples", 42, 32, 0, 1>;
def FunctionalTupleSetComponents : CoverageSection<"FunctionalTupleSetComponents", 43, 40, 0, 1>;
def FunctionalOptionPlans      : CoverageSection<"FunctionalOptionPlans", 44, 40, 0, 1>;
def FunctionalConfigurationOptions : CoverageSection<"FunctionalConfigurationOptions", 45, 88>;
def ResolvedFunctionalBinPlans : CoverageSection<"ResolvedFunctionalBinPlans", 46, 96>;
def ResolvedFunctionalTupleSets : CoverageSection<"ResolvedFunctionalTupleSets", 47, 88>;
def ResolvedFunctionalTupleSetTuples : CoverageSection<"ResolvedFunctionalTupleSetTuples", 48, 32>;
def ResolvedFunctionalTupleSetComponents : CoverageSection<"ResolvedFunctionalTupleSetComponents", 49, 40>;
def ResolvedTransitionAlternatives : CoverageSection<"ResolvedTransitionAlternatives", 50, 80>;
def ResolvedFunctionalBinGroups : CoverageSection<"ResolvedFunctionalBinGroups", 51, 88>;
def ResolvedTransitionExpansionGroups : CoverageSection<"ResolvedTransitionExpansionGroups", 52, 80>;
def ResolvedCrossAutomaticBinCountLimbs : CoverageSection<"ResolvedCrossAutomaticBinCountLimbs", 53, 8>;
def ResolvedCrossAutomaticNodes : CoverageSection<"ResolvedCrossAutomaticNodes", 54, 72>;
def ResolvedCrossAutomaticEdges : CoverageSection<"ResolvedCrossAutomaticEdges", 55, 32>;
def FunctionalFormals          : CoverageSection<"FunctionalFormals", 56, 64, 0, 1>;
def ResolvedInstanceOptions    : CoverageSection<"ResolvedInstanceOptions", 57, 64>;

// Normalized functional records use generated wire declarations, accessors,
// field descriptors, and low-level validation.
class CoverageFieldEncoding<string cppName_, int width_> {
  string cppName = cppName_;
  int width = width_;
}

def CovU32    : CoverageFieldEncoding<"U32", 4>;
def CovU64    : CoverageFieldEncoding<"U64", 8>;
def CovS64    : CoverageFieldEncoding<"S64", 8>;
def CovUUID   : CoverageFieldEncoding<"UUID", 16>;
def CovDigest : CoverageFieldEncoding<"Digest", 32>;

class CoverageFieldDomain<string cppName_, string category_,
                          list<CoverageEnumValue> enumValues_ = [],
                          int targetSection_ = 0> {
  string cppName = cppName_;
  string category = category_;
  list<CoverageEnumValue> enumValues = enumValues_;
  int targetSection = targetSection_;
}

def DomainNone : CoverageFieldDomain<"None", "None">;
def DomainTransitionRepetitionKind : CoverageFieldDomain<
    "TransitionRepetitionKind", "Enum",
    [TransitionOnce, TransitionConsecutive, TransitionGoto,
     TransitionNonconsecutive]>;
def DomainFunctionalValueSetKind : CoverageFieldDomain<
    "FunctionalValueSetKind", "Enum", [ValueSetIntegral, ValueSetReal]>;
def DomainFunctionalValueAtomKind : CoverageFieldDomain<
    "FunctionalValueAtomKind", "Enum",
    [ValueAtomIntegralValue, ValueAtomIntegralRange, ValueAtomRealInterval]>;
def DomainCrossRetainAutoPolicy : CoverageFieldDomain<
    "CrossRetainAutoPolicy", "Enum",
    [CrossDiscardAutoBins, CrossRetainAutoBins, CrossDeferredAutoBins]>;
def DomainCrossSelectorKind : CoverageFieldDomain<
    "CrossSelectorKind", "Enum",
    [CrossSelectorBinsof, CrossSelectorNot, CrossSelectorAnd,
     CrossSelectorOr, CrossSelectorSet, CrossSelectorAllTuples,
     CrossSelectorWith]>;
def DomainFunctionalItemKind : CoverageFieldDomain<
    "FunctionalItemKind", "Enum", [FunctionalCoverpoint, FunctionalCross]>;
def DomainFunctionalBinKind : CoverageFieldDomain<
    "FunctionalBinKind", "Enum",
    [FunctionalStateBin, FunctionalTransitionBin, FunctionalCrossBin]>;
def DomainFunctionalExpressionOwnerKind : CoverageFieldDomain<
    "FunctionalExpressionOwnerKind", "Enum",
    [ExpressionOwnerType, ExpressionOwnerItem, ExpressionOwnerBin,
     ExpressionOwnerSelector, ExpressionOwnerValueSet, ExpressionOwnerFormal]>;
def DomainFunctionalExpressionRole : CoverageFieldDomain<
    "FunctionalExpressionRole", "Enum",
    [ExpressionSamplingEvent, ExpressionCoverpointSample,
     ExpressionCoverpointIff, ExpressionCrossIff, ExpressionBinIff,
     ExpressionBinSet, ExpressionBinWith, ExpressionArrayCardinality,
     ExpressionRepeatLower, ExpressionRepeatUpper, ExpressionSelectorWith,
     ExpressionSelectorMatches, ExpressionOptionRHS,
     ExpressionValueAtomSingleton, ExpressionValueAtomLower,
     ExpressionValueAtomUpper, ExpressionFormalDefault]>;
def DomainFunctionalFormalKind : CoverageFieldDomain<
    "FunctionalFormalKind", "Enum", [FormalConstructor, FormalSample]>;
def DomainFunctionalFormalDirection : CoverageFieldDomain<
    "FunctionalFormalDirection", "Enum", [FormalInput, FormalRef]>;
def DomainFunctionalExpressionResultKind : CoverageFieldDomain<
    "FunctionalExpressionResultKind", "Enum",
    [ExpressionBoolean, ExpressionIntegral, ExpressionReal, ExpressionSet,
     ExpressionTupleSet, ExpressionString, ExpressionTupleQueue]>;
def DomainCoverageSignedness : CoverageFieldDomain<
    "CoverageSignedness", "Enum",
    [CoverageUnsigned, CoverageSigned, CoverageNotApplicable]>;
def DomainFunctionalBinArrayMode : CoverageFieldDomain<
    "FunctionalBinArrayMode", "Enum",
    [BinArrayScalar, BinArrayUnsized, BinArrayFixed]>;
def DomainFunctionalBinDistributionKind : CoverageFieldDomain<
    "FunctionalBinDistributionKind", "Enum",
    [BinDistributionNone, BinDistributionPerValue, BinDistributionUniform]>;
def DomainCrossMatchesPolicy : CoverageFieldDomain<
    "CrossMatchesPolicy", "Enum",
    [CrossMatchesNone, CrossMatchesCount, CrossMatchesAll]>;
def DomainFunctionalOptionScopeKind : CoverageFieldDomain<
    "FunctionalOptionScopeKind", "Enum",
    [OptionInstanceScope, OptionTypeScope]>;
def DomainFunctionalConfigurationOptionKind : CoverageFieldDomain<
    "FunctionalConfigurationOptionKind", "Enum",
    [ConfigGoal, ConfigWeight, ConfigAtLeast, ConfigAutoBinMax,
     ConfigCrossNumPrintMissing, ConfigDetectOverlap, ConfigPerInstance,
     ConfigMergeInstances, ConfigGetInstCoverage, ConfigDistributeFirst,
     ConfigRealInterval, ConfigStrobe, ConfigName, ConfigComment,
     ConfigCrossRetainAutoBins]>;
def DomainFunctionalConfigurationOptionOwnerKind : CoverageFieldDomain<
    "FunctionalConfigurationOptionOwnerKind", "Enum",
    [ConfigOwnerGroup, ConfigOwnerItem]>;
def DomainFunctionalConfigurationValueKind : CoverageFieldDomain<
    "FunctionalConfigurationValueKind", "Enum",
    [ConfigValueUnsigned, ConfigValueSigned, ConfigValueBoolean,
     ConfigValueString, ConfigValueRealBits]>;
def DomainFunctionalExpressionEvaluationPhase : CoverageFieldDomain<
    "FunctionalExpressionEvaluationPhase", "Enum",
    [ExpressionEvaluateConstructor, ExpressionEvaluateSample,
     ExpressionEvaluateEvent, ExpressionEvaluateOption]>;
def DomainFunctionalTupleElementMode : CoverageFieldDomain<
    "FunctionalTupleElementMode", "Enum",
    [TupleElementsBins, TupleElementsValues]>;
def DomainResolvedFunctionalValueSetRole : CoverageFieldDomain<
    "ResolvedFunctionalValueSetRole", "Enum",
    [ResolvedValueSetStateBin, ResolvedValueSetTransitionStep,
     ResolvedValueSetSelectorIntersection, ResolvedValueSetTupleComponent]>;
def DomainMetricKind : CoverageFieldDomain<
    "MetricKind", "Enum",
    [MetricLine, MetricToggle, MetricFunctional, MetricFSM, MetricAssertion,
     MetricBranch, MetricMCDC]>;
def DomainToggleDimensionKind : CoverageFieldDomain<
    "ToggleDimensionKind", "Enum",
    [ToggleRoot, ToggleScalar, ToggleEnum, TogglePackedArray,
     ToggleUnpackedArray, TogglePackedStruct, ToggleUnpackedStruct,
     TogglePackedUnion, ToggleUnpackedUnion, ToggleField, ToggleTag]>;
def DomainFunctionalSourceRole : CoverageFieldDomain<
    "FunctionalSourceRole", "Enum",
    [FunctionalExpandedSource, FunctionalOriginalSource,
     FunctionalMacroDefinition, FunctionalMacroInvocation]>;

def DomainStrings : CoverageFieldDomain<"Strings", "String", [], 1>;
def DomainSourceFiles : CoverageFieldDomain<"SourceFiles", "StableRef", [], 2>;
def DomainScopes : CoverageFieldDomain<"Scopes", "StableRef", [], 3>;
def DomainLinePoints : CoverageFieldDomain<"LinePoints", "StableRef", [], 4>;
def DomainToggleObjects : CoverageFieldDomain<"ToggleObjects", "StableRef", [], 5>;
def DomainFunctionalTypes : CoverageFieldDomain<"FunctionalTypes", "StableRef", [], 7>;
def DomainFunctionalItems : CoverageFieldDomain<"FunctionalItems", "StableRef", [], 8>;
def DomainFunctionalBins : CoverageFieldDomain<"FunctionalBins", "StableRef", [], 9>;
def DomainFunctionalExpressions : CoverageFieldDomain<"FunctionalExpressions", "StableRef", [], 30>;
def DomainFunctionalFormals : CoverageFieldDomain<"FunctionalFormals", "StableRef", [], 56>;
def DomainFunctionalValueSets : CoverageFieldDomain<"FunctionalValueSets", "StableRef", [], 27>;
def DomainFunctionalValueAtoms : CoverageFieldDomain<"FunctionalValueAtoms", "Range", [], 28>;
def DomainFunctionalValueLimbs : CoverageFieldDomain<"FunctionalValueLimbs", "Range", [], 29>;
def DomainTransitionAlternatives : CoverageFieldDomain<"TransitionAlternatives", "Range", [], 20>;
def DomainTransitionSteps : CoverageFieldDomain<"TransitionSteps", "Range", [], 21>;
def DomainCrossTargets : CoverageFieldDomain<"CrossTargets", "Range", [], 22>;
def DomainCrossBins : CoverageFieldDomain<"CrossBins", "Range", [], 23>;
def DomainCrossSelectorNodes : CoverageFieldDomain<"CrossSelectorNodes", "StableRef", [], 24>;
def DomainCrossSelectorOperands : CoverageFieldDomain<"CrossSelectorOperands", "Range", [], 25>;
def DomainSparseCrossTupleComponents : CoverageFieldDomain<"SparseCrossTupleComponents", "Range", [], 26>;
def DomainResolvedFunctionalItems : CoverageFieldDomain<"ResolvedFunctionalItems", "StableRef", [], 32>;
def DomainResolvedFunctionalBins : CoverageFieldDomain<"ResolvedFunctionalBins", "StableRef", [], 33>;
def DomainResolvedFunctionalBinRows : CoverageFieldDomain<"ResolvedFunctionalBinRows", "Range", [], 33>;
def DomainResolvedFunctionalValueSets : CoverageFieldDomain<"ResolvedFunctionalValueSets", "StableRef", [], 37>;
def DomainResolvedTransitionAlternatives : CoverageFieldDomain<"ResolvedTransitionAlternatives", "StableRef", [], 50>;
def DomainResolvedTransitionAlternativeRows : CoverageFieldDomain<"ResolvedTransitionAlternativeRows", "Range", [], 50>;
def DomainResolvedTransitionSteps : CoverageFieldDomain<"ResolvedTransitionSteps", "Range", [], 34>;
def DomainResolvedFunctionalBinGroups : CoverageFieldDomain<"ResolvedFunctionalBinGroups", "Range", [], 51>;
def DomainResolvedTransitionExpansionGroups : CoverageFieldDomain<"ResolvedTransitionExpansionGroups", "Range", [], 52>;
def DomainResolvedFunctionalValueAtoms : CoverageFieldDomain<"ResolvedFunctionalValueAtoms", "Range", [], 38>;
def DomainResolvedFunctionalValueLimbs : CoverageFieldDomain<"ResolvedFunctionalValueLimbs", "Range", [], 39>;
def DomainResolvedFunctionalTupleSets : CoverageFieldDomain<"ResolvedFunctionalTupleSets", "StableRef", [], 47>;
def DomainResolvedFunctionalTupleSetTuples : CoverageFieldDomain<"ResolvedFunctionalTupleSetTuples", "Range", [], 48>;
def DomainResolvedFunctionalTupleSetComponents : CoverageFieldDomain<"ResolvedFunctionalTupleSetComponents", "Range", [], 49>;
def DomainResolvedFunctionalTupleSetTupleIDs : CoverageFieldDomain<"ResolvedFunctionalTupleSetTupleIDs", "StableRef", [], 48>;
def DomainResolvedCrossAutomaticNodes : CoverageFieldDomain<"ResolvedCrossAutomaticNodes", "StableRef", [], 54>;
def DomainResolvedCrossAutomaticEdges : CoverageFieldDomain<"ResolvedCrossAutomaticEdges", "Range", [], 55>;
def DomainFunctionalTupleSets : CoverageFieldDomain<"FunctionalTupleSets", "StableRef", [], 41>;
def DomainFunctionalTupleSetTuples : CoverageFieldDomain<"FunctionalTupleSetTuples", "Range", [], 42>;
def DomainFunctionalTupleSetComponents : CoverageFieldDomain<"FunctionalTupleSetComponents", "Range", [], 43>;
def DomainFunctionalTupleSetTupleIDs : CoverageFieldDomain<"FunctionalTupleSetTupleIDs", "StableRef", [], 42>;
def DomainRuns : CoverageFieldDomain<"Runs", "UUIDRef", [], 13>;

// Relational union/owner domains are deliberately typed but validated by the
// handwritten semantic layer rather than a single section lookup.
def DomainFunctionalExpressionOwners : CoverageFieldDomain<"FunctionalExpressionOwners", "Relational">;
def DomainFunctionalOptionOwners : CoverageFieldDomain<"FunctionalOptionOwners", "Relational">;
def DomainResolvedFunctionalOptionOwners : CoverageFieldDomain<"ResolvedFunctionalOptionOwners", "Relational">;
def DomainResolvedInstances : CoverageFieldDomain<"ResolvedInstances", "Relational">;
def DomainCoverageEntities : CoverageFieldDomain<"CoverageEntities", "Relational">;

class CoverageField<string cppName_, string diagnosticName_, int offset_,
                    CoverageFieldEncoding encoding_> {
  string cppName = cppName_;
  string diagnosticName = diagnosticName_;
  int offset = offset_;
  CoverageFieldEncoding encoding = encoding_;
  // Generated low-level validation metadata. Cross-record semantic rules
  // remain handwritten in the portable codec.
  string semantic = "Scalar";
  CoverageFieldDomain domain = DomainNone;
  bit nullable = 0;
  int validMask = -1;
  string pairedField = "";
}

class CoverageRecord<string cppName_, CoverageSection section_,
                     list<CoverageField> fields_> {
  string cppName = cppName_;
  CoverageSection section = section_;
  list<CoverageField> fields = fields_;
}

def SFId : CoverageField<"Id", "id", 0, CovU64> {
  let semantic = "StableID"; let domain = DomainSourceFiles;
}
def SFPath : CoverageField<"Path", "path", 8, CovU32> {
  let semantic = "StringIndex"; let domain = DomainStrings;
}
def SFReserved : CoverageField<"Reserved", "reserved", 12, CovU32> {
  let semantic = "ReservedZero";
}
def SFDigest : CoverageField<"Digest", "digest", 16, CovDigest> {
  let semantic = "Digest";
}
def SourceFileRecord : CoverageRecord<"SourceFileRecord", SourceFiles,
    [SFId, SFPath, SFReserved, SFDigest]>;

def ScopeId : CoverageField<"Id", "id", 0, CovU64> {
  let semantic = "StableID"; let domain = DomainScopes;
}
def ScopeParent : CoverageField<"Parent", "parent", 8, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainScopes; let nullable = 1;
}
def ScopeName : CoverageField<"Name", "name", 16, CovU32> {
  let semantic = "StringIndex"; let domain = DomainStrings;
}
def ScopeKind : CoverageField<"Kind", "kind", 20, CovU32>;
def ScopeDefinition : CoverageField<"Definition", "definition", 24, CovU32> {
  let semantic = "StringIndex"; let domain = DomainStrings;
}
def ScopeReserved : CoverageField<"Reserved", "reserved", 28, CovU32> {
  let semantic = "ReservedZero";
}
def ScopeRecord : CoverageRecord<"ScopeRecord", Scopes,
    [ScopeId, ScopeParent, ScopeName, ScopeKind, ScopeDefinition,
     ScopeReserved]>;

def LPId : CoverageField<"Id", "id", 0, CovU64> {
  let semantic = "StableID"; let domain = DomainLinePoints;
}
def LPFile : CoverageField<"File", "file", 8, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainSourceFiles;
}
def LPEndFile : CoverageField<"EndFile", "end_file", 16, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainSourceFiles;
}
def LPScope : CoverageField<"Scope", "scope", 24, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainScopes;
}
def LPMacro : CoverageField<"MacroName", "macro_name", 32, CovU32> {
  let semantic = "StringIndex"; let domain = DomainStrings;
}
def LPLine : CoverageField<"Line", "line", 36, CovU32>;
def LPColumn : CoverageField<"Column", "column", 40, CovU32>;
def LPEndLine : CoverageField<"EndLine", "end_line", 44, CovU32>;
def LPEndColumn : CoverageField<"EndColumn", "end_column", 48, CovU32>;
def LPSemanticPhase : CoverageField<"SemanticPhase", "semantic_phase", 52, CovU32>;
def LPFlags : CoverageField<"Flags", "flags", 56, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def LPReserved : CoverageField<"Reserved", "reserved", 60, CovU32> {
  let semantic = "ReservedZero";
}
def LinePointRecord : CoverageRecord<"LinePointRecord", LinePoints,
    [LPId, LPFile, LPEndFile, LPScope, LPMacro, LPLine, LPColumn, LPEndLine,
     LPEndColumn, LPSemanticPhase, LPFlags, LPReserved]>;

def TOId : CoverageField<"Id", "id", 0, CovU64> {
  let semantic = "StableID"; let domain = DomainToggleObjects;
}
def TOScope : CoverageField<"Scope", "scope", 8, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainScopes;
}
def TOFile : CoverageField<"File", "file", 16, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainSourceFiles;
}
def TOName : CoverageField<"Name", "name", 24, CovU32> {
  let semantic = "StringIndex"; let domain = DomainStrings;
}
def TOTypeRoot : CoverageField<"TypeRoot", "type_root", 28, CovU32>;
def TOBitWidth : CoverageField<"BitWidth", "bit_width", 32, CovU64>;
def TOLine : CoverageField<"Line", "line", 40, CovU32>;
def TOColumn : CoverageField<"Column", "column", 44, CovU32>;
def TOEndLine : CoverageField<"EndLine", "end_line", 48, CovU32>;
def TOEndColumn : CoverageField<"EndColumn", "end_column", 52, CovU32>;
def TOFlags : CoverageField<"Flags", "flags", 56, CovU32> {
  let semantic = "Flags"; let validMask = 1;
}
def TOReserved : CoverageField<"Reserved", "reserved", 60, CovU32> {
  let semantic = "ReservedZero";
}
def ToggleObjectRecord : CoverageRecord<"ToggleObjectRecord", ToggleObjects,
    [TOId, TOScope, TOFile, TOName, TOTypeRoot, TOBitWidth, TOLine, TOColumn,
     TOEndLine, TOEndColumn, TOFlags, TOReserved]>;

def TDObject : CoverageField<"Object", "object", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainToggleObjects;
}
def TDParent : CoverageField<"Parent", "parent", 8, CovU32>;
def TDKind : CoverageField<"Kind", "kind", 12, CovU32> {
  let semantic = "Enum"; let domain = DomainToggleDimensionKind;
}
// UINT32_MAX is the canonical no-child sentinel, so this range is checked by
// the relational toggle-tree validator rather than the generic zero-based
// range helper.
def TDFirstChild : CoverageField<"FirstChild", "first_child", 16, CovU32>;
def TDChildCount : CoverageField<"ChildCount", "child_count", 20, CovU32>;
def TDLeft : CoverageField<"Left", "left", 24, CovS64>;
def TDRight : CoverageField<"Right", "right", 32, CovS64>;
def TDBitOffset : CoverageField<"BitOffset", "bit_offset", 40, CovU64>;
def TDBitWidth : CoverageField<"BitWidth", "bit_width", 48, CovU64>;
def TDName : CoverageField<"Name", "name", 56, CovU32> {
  let semantic = "StringIndex"; let domain = DomainStrings;
}
def TDFlags : CoverageField<"Flags", "flags", 60, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def ToggleDimensionRecord : CoverageRecord<"ToggleDimensionRecord",
    ToggleTypeDimensions,
    [TDObject, TDParent, TDKind, TDFirstChild, TDChildCount, TDLeft, TDRight,
     TDBitOffset, TDBitWidth, TDName, TDFlags]>;

def FTId : CoverageField<"Id", "id", 0, CovU64> {
  let semantic = "StableID"; let domain = DomainFunctionalTypes;
}
def FTScope : CoverageField<"Scope", "scope", 8, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainScopes;
}
def FTName : CoverageField<"Name", "name", 16, CovU32> {
  let semantic = "StringIndex"; let domain = DomainStrings;
}
def FTFlags : CoverageField<"Flags", "flags", 20, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def FTLanguageVersion : CoverageField<"LanguageVersion", "language_version", 24, CovU32>;
def FTHierarchy : CoverageField<"Hierarchy", "hierarchy", 28, CovU32> {
  let semantic = "StringIndex"; let domain = DomainStrings;
}
def FunctionalTypeRecord : CoverageRecord<"FunctionalTypeRecord",
    FunctionalTypes,
    [FTId, FTScope, FTName, FTFlags, FTLanguageVersion, FTHierarchy]>;

def FIId : CoverageField<"Id", "id", 0, CovU64> {
  let semantic = "StableID"; let domain = DomainFunctionalItems;
}
def FIType : CoverageField<"Type", "type", 8, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalTypes;
}
def FIName : CoverageField<"Name", "name", 16, CovU32> {
  let semantic = "StringIndex"; let domain = DomainStrings;
}
def FIKind : CoverageField<"Kind", "kind", 20, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalItemKind;
}
def FIFlags : CoverageField<"Flags", "flags", 24, CovU32> {
  let semantic = "Flags"; let validMask = 1;
}
def FIGoal : CoverageField<"Goal", "goal", 28, CovU32>;
def FIWeight : CoverageField<"Weight", "weight", 32, CovU32>;
def FIOrdinal : CoverageField<"Ordinal", "ordinal", 36, CovU32>;
def FIHierarchy : CoverageField<"Hierarchy", "hierarchy", 40, CovU32> {
  let semantic = "StringIndex"; let domain = DomainStrings;
}
def FIReserved : CoverageField<"Reserved", "reserved", 44, CovU32> {
  let semantic = "ReservedZero";
}
def FunctionalItemRecord : CoverageRecord<"FunctionalItemRecord",
    FunctionalItems,
    [FIId, FIType, FIName, FIKind, FIFlags, FIGoal, FIWeight, FIOrdinal,
     FIHierarchy, FIReserved]>;

def FBId : CoverageField<"Id", "id", 0, CovU64> {
  let semantic = "StableID"; let domain = DomainFunctionalBins;
}
def FBItem : CoverageField<"Item", "item", 8, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalItems;
}
def FBName : CoverageField<"Name", "name", 16, CovU32> {
  let semantic = "StringIndex"; let domain = DomainStrings;
}
def FBKind : CoverageField<"Kind", "kind", 20, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalBinKind;
}
def FBFlags : CoverageField<"Flags", "flags", 24, CovU32> {
  let semantic = "Flags"; let validMask = 223;
}
def FBOrdinal : CoverageField<"Ordinal", "ordinal", 28, CovU32>;
def FBAtLeast : CoverageField<"AtLeast", "at_least", 32, CovU64>;
def FBHierarchy : CoverageField<"Hierarchy", "hierarchy", 40, CovU32> {
  let semantic = "StringIndex"; let domain = DomainStrings;
}
def FBReserved : CoverageField<"Reserved", "reserved", 44, CovU32> {
  let semantic = "ReservedZero";
}
def FunctionalBinRecord : CoverageRecord<"FunctionalBinRecord",
    FunctionalBins,
    [FBId, FBItem, FBName, FBKind, FBFlags, FBOrdinal, FBAtLeast,
     FBHierarchy, FBReserved]>;

def EXEntity : CoverageField<"Entity", "entity", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainCoverageEntities;
}
def EXMetric : CoverageField<"Metric", "metric", 8, CovU32> {
  let semantic = "Enum"; let domain = DomainMetricKind;
}
def EXReason : CoverageField<"Reason", "reason", 12, CovU32> {
  let semantic = "StringIndex"; let domain = DomainStrings;
}
def EXFlags : CoverageField<"Flags", "flags", 16, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def EXReserved : CoverageField<"Reserved", "reserved", 20, CovU32> {
  let semantic = "ReservedZero";
}
def ExclusionRecord : CoverageRecord<"ExclusionRecord", Exclusions,
    [EXEntity, EXMetric, EXReason, EXFlags, EXReserved]>;

def RunUUID : CoverageField<"UUID", "uuid", 0, CovUUID> {
  let semantic = "UUIDID"; let domain = DomainRuns;
}
def RunName : CoverageField<"Name", "name", 16, CovU32> {
  let semantic = "StringIndex"; let domain = DomainStrings;
}
def RunStatus : CoverageField<"Status", "status", 20, CovU32>;
def RunSimulationTime : CoverageField<"SimulationTime", "simulation_time", 24, CovU64>;
def RunTimestamp : CoverageField<"Timestamp", "timestamp", 32, CovU64>;
def RunSeed : CoverageField<"Seed", "seed", 40, CovU64>;
def RunTagCount : CoverageField<"TagCount", "tag_count", 48, CovU32>;
def RunReserved : CoverageField<"Reserved", "reserved", 52, CovU32> {
  let semantic = "ReservedZero";
}
def RunFlags : CoverageField<"Flags", "flags", 56, CovU64> {
  let semantic = "Flags"; let validMask = 7;
}
def RunRecord : CoverageRecord<"RunRecord", Runs,
    [RunUUID, RunName, RunStatus, RunSimulationTime, RunTimestamp, RunSeed,
     RunTagCount, RunReserved, RunFlags]>;

def RIrun : CoverageField<"Run", "run", 0, CovUUID> {
  let semantic = "UUIDRef"; let domain = DomainRuns;
}
def RIType : CoverageField<"Type", "type", 16, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalTypes;
}
def RIId : CoverageField<"Id", "id", 24, CovU64> {
  let semantic = "StableID"; let domain = DomainResolvedInstances;
}
def RIName : CoverageField<"Name", "name", 32, CovU32> {
  let semantic = "StringIndex"; let domain = DomainStrings;
}
def RIFlags : CoverageField<"Flags", "flags", 36, CovU32> {
  let semantic = "Flags"; let validMask = 1;
}
def RIConfiguration : CoverageField<"Configuration", "configuration", 40, CovDigest> {
  let semantic = "Digest";
}
def ResolvedInstanceRecord : CoverageRecord<"ResolvedInstanceRecord",
    ResolvedCovergroupInstances,
    [RIrun, RIType, RIId, RIName, RIFlags, RIConfiguration]>;

def RIORun : CoverageField<"Run", "run", 0, CovUUID> {
  let semantic = "UUIDRef"; let domain = DomainRuns;
}
def RIOInstance : CoverageField<"Instance", "instance", 16, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedInstances;
  // Zero identifies a run-local type-option override. Nonzero values identify
  // the existing per-instance option rows.
  let nullable = 1;
}
def RIOOwner : CoverageField<"Owner", "owner", 24, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalOptionOwners;
  let nullable = 1;
}
def RIOOwnerKind : CoverageField<"OwnerKind", "owner_kind", 32, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalConfigurationOptionOwnerKind;
}
def RIOOption : CoverageField<"Option", "option", 36, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalConfigurationOptionKind;
}
def RIOValueKind : CoverageField<"ValueKind", "value_kind", 40, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalConfigurationValueKind;
}
def RIOStringValue : CoverageField<"StringValue", "string_value", 44, CovU32> {
  let semantic = "StringIndex"; let domain = DomainStrings;
}
def RIOValue : CoverageField<"Value", "value", 48, CovU64>;
def RIOFlags : CoverageField<"Flags", "flags", 56, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def RIOReserved : CoverageField<"Reserved", "reserved", 60, CovU32> {
  let semantic = "ReservedZero";
}
def ResolvedInstanceOptionRecord : CoverageRecord<
    "ResolvedInstanceOptionRecord", ResolvedInstanceOptions,
    [RIORun, RIOInstance, RIOOwner, RIOOwnerKind, RIOOption, RIOValueKind,
     RIOStringValue, RIOValue, RIOFlags, RIOReserved]>;

def CounterRun : CoverageField<"Run", "run", 0, CovUUID> {
  let semantic = "UUIDRef"; let domain = DomainRuns;
}
def CounterEntity : CoverageField<"Entity", "entity", 16, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainCoverageEntities;
}
def CounterInstance : CoverageField<"Instance", "instance", 24, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedInstances;
  let nullable = 1;
}
def CounterValue : CoverageField<"Value", "value", 32, CovU64>;
def CounterMetric : CoverageField<"Metric", "metric", 40, CovU32> {
  let semantic = "Enum"; let domain = DomainMetricKind;
}
def CounterSubindex : CoverageField<"Subindex", "subindex", 44, CovU32>;
def CounterFlags : CoverageField<"Flags", "flags", 48, CovU32> {
  let semantic = "Flags"; let validMask = 1;
}
def CounterReserved : CoverageField<"Reserved", "reserved", 52, CovU32> {
  let semantic = "ReservedZero";
}
def CounterRecord : CoverageRecord<"CounterRecord", Counters,
    [CounterRun, CounterEntity, CounterInstance, CounterValue, CounterMetric,
     CounterSubindex, CounterFlags, CounterReserved]>;

def MetadataRun : CoverageField<"Run", "run", 0, CovUUID> {
  let semantic = "UUIDRef"; let domain = DomainRuns; let nullable = 1;
}
def MetadataKey : CoverageField<"Key", "key", 16, CovU32> {
  let semantic = "StringIndex"; let domain = DomainStrings;
}
def MetadataValue : CoverageField<"Value", "value", 20, CovU32> {
  let semantic = "StringIndex"; let domain = DomainStrings;
}
def UserMetadataRecord : CoverageRecord<"UserMetadataRecord", UserMetadata,
    [MetadataRun, MetadataKey, MetadataValue]>;

def FSRBin : CoverageField<"Bin", "bin", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalBins;
}
def FSRRole : CoverageField<"Role", "role", 8, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalSourceRole;
}
def FSROrdinal : CoverageField<"Ordinal", "ordinal", 12, CovU32>;
def FSRFile : CoverageField<"File", "file", 16, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainSourceFiles;
}
def FSREndFile : CoverageField<"EndFile", "end_file", 24, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainSourceFiles;
}
def FSRMacro : CoverageField<"MacroName", "macro_name", 32, CovU32> {
  let semantic = "StringIndex"; let domain = DomainStrings;
}
def FSRLine : CoverageField<"Line", "line", 36, CovU32>;
def FSRColumn : CoverageField<"Column", "column", 40, CovU32>;
def FSREndLine : CoverageField<"EndLine", "end_line", 44, CovU32>;
def FSREndColumn : CoverageField<"EndColumn", "end_column", 48, CovU32>;
def FSRFlags : CoverageField<"Flags", "flags", 52, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def FSRReserved0 : CoverageField<"Reserved0", "reserved_0", 56, CovU32> {
  let semantic = "ReservedZero";
}
def FSRReserved1 : CoverageField<"Reserved1", "reserved_1", 60, CovU32> {
  let semantic = "ReservedZero";
}
def FunctionalSourceRangeRecord : CoverageRecord<
    "FunctionalSourceRangeRecord", FunctionalSourceRanges,
    [FSRBin, FSRRole, FSROrdinal, FSRFile, FSREndFile, FSRMacro, FSRLine,
     FSRColumn, FSREndLine, FSREndColumn, FSRFlags, FSRReserved0,
     FSRReserved1]>;

def TPBin       : CoverageField<"Bin", "bin", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalBins;
}
def TPItem      : CoverageField<"Item", "item", 8, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalItems;
}
def TPFirstAlt  : CoverageField<"FirstAlternative", "first_alternative", 16, CovU32> {
  let semantic = "RangeStart"; let domain = DomainTransitionAlternatives;
  let pairedField = "AlternativeCount";
}
def TPAltCount  : CoverageField<"AlternativeCount", "alternative_count", 20, CovU32> {
  let semantic = "RangeCount"; let domain = DomainTransitionAlternatives;
  let pairedField = "FirstAlternative";
}
def TPFlags     : CoverageField<"Flags", "flags", 24, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def TPReserved  : CoverageField<"Reserved", "reserved", 28, CovU32> {
  let semantic = "ReservedZero";
}
def TransitionProgramRecord : CoverageRecord<"TransitionProgramRecord", TransitionPrograms,
    [TPBin, TPItem, TPFirstAlt, TPAltCount, TPFlags, TPReserved]>;

def TABin       : CoverageField<"Bin", "bin", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalBins;
}
def TATerminal  : CoverageField<"TerminalValueSet", "terminal_value_set", 8, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalValueSets;
}
def TAFirstStep : CoverageField<"FirstStep", "first_step", 16, CovU32> {
  let semantic = "RangeStart"; let domain = DomainTransitionSteps;
  let pairedField = "StepCount";
}
def TAStepCount : CoverageField<"StepCount", "step_count", 20, CovU32> {
  let semantic = "RangeCount"; let domain = DomainTransitionSteps;
  let pairedField = "FirstStep";
}
def TAOrdinal   : CoverageField<"Ordinal", "ordinal", 24, CovU32>;
def TAFlags     : CoverageField<"Flags", "flags", 28, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def TransitionAlternativeRecord : CoverageRecord<"TransitionAlternativeRecord", TransitionAlternatives,
    [TABin, TATerminal, TAFirstStep, TAStepCount, TAOrdinal, TAFlags]>;

def TSBin       : CoverageField<"Bin", "bin", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalBins;
}
def TSValueSet  : CoverageField<"ValueSet", "value_set", 8, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalValueSets;
}
def TSLowerExpr : CoverageField<"LowerExpression", "lower_expression", 16, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalExpressions;
  let nullable = 1;
}
def TSUpperExpr : CoverageField<"UpperExpression", "upper_expression", 24, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalExpressions;
  let nullable = 1;
}
def TSLower     : CoverageField<"LowerBound", "lower_bound", 32, CovU64>;
def TSUpper     : CoverageField<"UpperBound", "upper_bound", 40, CovU64>;
def TSAlt       : CoverageField<"AlternativeOrdinal", "alternative_ordinal", 48, CovU32>;
def TSOrdinal   : CoverageField<"Ordinal", "ordinal", 52, CovU32>;
def TSRepeat    : CoverageField<"Repetition", "repetition", 56, CovU32> {
  let semantic = "Enum"; let domain = DomainTransitionRepetitionKind;
}
def TSFlags     : CoverageField<"Flags", "flags", 60, CovU32> {
  let semantic = "Flags"; let validMask = 1;
}
def TransitionStepRecord : CoverageRecord<"TransitionStepRecord", TransitionSteps,
    [TSBin, TSValueSet, TSLowerExpr, TSUpperExpr, TSLower,
     TSUpper, TSAlt, TSOrdinal, TSRepeat, TSFlags]>;

def CPItem      : CoverageField<"Item", "item", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalItems;
}
def CPFirstTgt  : CoverageField<"FirstTarget", "first_target", 8, CovU32> {
  let semantic = "RangeStart"; let domain = DomainCrossTargets;
  let pairedField = "TargetCount";
}
def CPTgtCount  : CoverageField<"TargetCount", "target_count", 12, CovU32> {
  let semantic = "RangeCount"; let domain = DomainCrossTargets;
  let pairedField = "FirstTarget";
}
def CPFirstBin  : CoverageField<"FirstBin", "first_bin", 16, CovU32> {
  let semantic = "RangeStart"; let domain = DomainCrossBins;
  let pairedField = "BinCount";
}
def CPBinCount  : CoverageField<"BinCount", "bin_count", 20, CovU32> {
  let semantic = "RangeCount"; let domain = DomainCrossBins;
  let pairedField = "FirstBin";
}
def CPRetain    : CoverageField<"RetainAutoPolicy", "retain_auto_policy", 24, CovU32> {
  let semantic = "Enum"; let domain = DomainCrossRetainAutoPolicy;
}
def CPFlags     : CoverageField<"Flags", "flags", 28, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def CPIff       : CoverageField<"IffExpression", "iff_expression", 32, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalExpressions;
  let nullable = 1;
}
def CPTupleElementType : CoverageField<"TupleElementType", "tuple_element_type", 40, CovU64>;
def CPTupleProvenanceSpan : CoverageField<"TupleProvenanceSpan", "tuple_provenance_span", 48, CovU64>;
def CPTupleFlags : CoverageField<"TupleFlags", "tuple_flags", 56, CovU32> {
  let semantic = "Flags"; let validMask = 1;
}
def CPTupleReserved : CoverageField<"TupleReserved", "tuple_reserved", 60, CovU32> {
  let semantic = "ReservedZero";
}
def CrossPlanRecord : CoverageRecord<"CrossPlanRecord", CrossPlans,
    [CPItem, CPFirstTgt, CPTgtCount, CPFirstBin, CPBinCount, CPRetain, CPFlags,
     CPIff, CPTupleElementType, CPTupleProvenanceSpan, CPTupleFlags,
     CPTupleReserved]>;

def CTACross    : CoverageField<"Cross", "cross", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalItems;
}
def CTATarget   : CoverageField<"Target", "target", 8, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalItems;
}
def CTAOrdinal  : CoverageField<"Ordinal", "ordinal", 16, CovU32>;
def CTAReserved : CoverageField<"Reserved", "reserved", 20, CovU32> {
  let semantic = "ReservedZero";
}
def CTATupleBitOffset : CoverageField<"TupleBitOffset", "tuple_bit_offset", 24, CovU64>;
def CTATupleBitWidth : CoverageField<"TupleBitWidth", "tuple_bit_width", 32, CovU32>;
def CTATupleResultKind : CoverageField<"TupleResultKind", "tuple_result_kind", 36, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalExpressionResultKind;
}
def CTATupleSignedness : CoverageField<"TupleSignedness", "tuple_signedness", 40, CovU32> {
  let semantic = "Enum"; let domain = DomainCoverageSignedness;
}
def CTATupleFlags : CoverageField<"TupleFlags", "tuple_flags", 44, CovU32> {
  let semantic = "Flags"; let validMask = 3;
}
def CrossTargetRecord : CoverageRecord<"CrossTargetRecord", CrossTargets,
    [CTACross, CTATarget, CTAOrdinal, CTAReserved, CTATupleBitOffset,
     CTATupleBitWidth, CTATupleResultKind, CTATupleSignedness,
     CTATupleFlags]>;

def CBCBin      : CoverageField<"Bin", "bin", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalBins;
}
def CBCCross    : CoverageField<"Cross", "cross", 8, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalItems;
}
def CBCRoot     : CoverageField<"RootSelector", "root_selector", 16, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainCrossSelectorNodes;
}
def CBCFlags    : CoverageField<"Flags", "flags", 24, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def CBCReserved : CoverageField<"Reserved", "reserved", 28, CovU32> {
  let semantic = "ReservedZero";
}
def CrossBinRecord : CoverageRecord<"CrossBinRecord", CrossBins,
    [CBCBin, CBCCross, CBCRoot, CBCFlags, CBCReserved]>;

def CSNId       : CoverageField<"Id", "id", 0, CovU64> {
  let semantic = "StableID"; let domain = DomainCrossSelectorNodes;
}
def CSNCross    : CoverageField<"Cross", "cross", 8, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalItems;
}
def CSNTarget   : CoverageField<"Target", "target", 16, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalItems;
  let nullable = 1;
}
def CSNBin      : CoverageField<"Bin", "bin", 24, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalBins;
  let nullable = 1;
}
def CSNValueSet : CoverageField<"ValueSet", "value_set", 32, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalValueSets;
  let nullable = 1;
}
def CSNWithExpression: CoverageField<"WithExpression", "with_expression", 40, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalExpressions;
  let nullable = 1;
}
def CSNConstruction : CoverageField<"ConstructionExpression", "construction_expression", 48, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalExpressions;
  let nullable = 1;
}
def CSNTupleSet : CoverageField<"TupleSet", "tuple_set", 56, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalTupleSets;
  let nullable = 1;
}
def CSNMatchesExpression : CoverageField<"MatchesExpression", "matches_expression", 64, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalExpressions;
  let nullable = 1;
}
def CSNFirstOp  : CoverageField<"FirstOperand", "first_operand", 72, CovU32> {
  let semantic = "RangeStart"; let domain = DomainCrossSelectorOperands;
  let pairedField = "OperandCount";
}
def CSNOpCount  : CoverageField<"OperandCount", "operand_count", 76, CovU32> {
  let semantic = "RangeCount"; let domain = DomainCrossSelectorOperands;
  let pairedField = "FirstOperand";
}
def CSNKind     : CoverageField<"Kind", "kind", 80, CovU32> {
  let semantic = "Enum"; let domain = DomainCrossSelectorKind;
}
def CSNOrdinal  : CoverageField<"Ordinal", "ordinal", 84, CovU32>;
def CSNFlags    : CoverageField<"Flags", "flags", 88, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def CSNMatchesPolicy : CoverageField<"MatchesPolicy", "matches_policy", 92, CovU32> {
  let semantic = "Enum"; let domain = DomainCrossMatchesPolicy;
}
def CSNMatchesCount : CoverageField<"MatchesCount", "matches_count", 96, CovU64>;
def CSNReserved : CoverageField<"Reserved", "reserved", 104, CovU64> {
  let semantic = "ReservedZero";
}
def CrossSelectorNodeRecord : CoverageRecord<"CrossSelectorNodeRecord", CrossSelectorNodes,
    [CSNId, CSNCross, CSNTarget, CSNBin, CSNValueSet, CSNWithExpression,
     CSNConstruction, CSNTupleSet, CSNMatchesExpression, CSNFirstOp,
     CSNOpCount, CSNKind, CSNOrdinal, CSNFlags, CSNMatchesPolicy,
     CSNMatchesCount, CSNReserved]>;

def CSOCNode    : CoverageField<"Node", "node", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainCrossSelectorNodes;
}
def CSOCOperand : CoverageField<"Operand", "operand", 8, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainCrossSelectorNodes;
}
def CSOCOrdinal : CoverageField<"Ordinal", "ordinal", 16, CovU32>;
def CSOCReserved: CoverageField<"Reserved", "reserved", 20, CovU32> {
  let semantic = "ReservedZero";
}
def CrossSelectorOperandRecord : CoverageRecord<"CrossSelectorOperandRecord", CrossSelectorOperands,
    [CSOCNode, CSOCOperand, CSOCOrdinal, CSOCReserved]>;

def SCTRun      : CoverageField<"Run", "run", 0, CovUUID> {
  let semantic = "UUIDRef"; let domain = DomainRuns;
}
def SCTInstance : CoverageField<"Instance", "instance", 16, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedInstances;
}
def SCTCross    : CoverageField<"Cross", "cross", 24, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalItems;
}
def SCTFirst    : CoverageField<"FirstComponent", "first_component", 32, CovU32> {
  let semantic = "RangeStart"; let domain = DomainSparseCrossTupleComponents;
  let pairedField = "ComponentCount";
}
def SCTCount    : CoverageField<"ComponentCount", "component_count", 36, CovU32> {
  let semantic = "RangeCount"; let domain = DomainSparseCrossTupleComponents;
  let pairedField = "FirstComponent";
}
def SCTHits     : CoverageField<"Hits", "hits", 40, CovU64>;
def SCTFlags    : CoverageField<"Flags", "flags", 48, CovU32> {
  let semantic = "Flags"; let validMask = 1;
}
def SCTReserved : CoverageField<"Reserved", "reserved", 52, CovU32> {
  let semantic = "ReservedZero";
}
def SparseCrossTupleRecord : CoverageRecord<"SparseCrossTupleRecord", SparseCrossTuples,
    [SCTRun, SCTInstance, SCTCross, SCTFirst, SCTCount, SCTHits, SCTFlags, SCTReserved]>;

def SCTCBin : CoverageField<"Bin", "bin", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalBins;
}
def SparseCrossTupleComponentRecord : CoverageRecord<"SparseCrossTupleComponentRecord",
    SparseCrossTupleComponents, [SCTCBin]>;

def IBDRun      : CoverageField<"Run", "run", 0, CovUUID> {
  let semantic = "UUIDRef"; let domain = DomainRuns;
}
def IBDBin      : CoverageField<"Bin", "bin", 16, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalBins;
}
def IBDInstance : CoverageField<"Instance", "instance", 24, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedInstances;
}
def IBDSimulationTime : CoverageField<"SimulationTime", "simulation_time", 32,
                                            CovU64>;
def IBDCount    : CoverageField<"Count", "count", 40, CovU64>;
def IBDMessage  : CoverageField<"Message", "message", 48, CovU32> {
  let semantic = "StringIndex"; let domain = DomainStrings;
}
def IBDFlags    : CoverageField<"Flags", "flags", 52, CovU32> {
  let semantic = "Flags"; let validMask = 1; // Saturated count.
}
def IllegalBinDiagnosticRecord : CoverageRecord<"IllegalBinDiagnosticRecord",
    IllegalBinDiagnostics, [IBDRun, IBDBin, IBDInstance, IBDSimulationTime,
                            IBDCount, IBDMessage, IBDFlags]>;

def FVSId       : CoverageField<"Id", "id", 0, CovU64> {
  let semantic = "StableID"; let domain = DomainFunctionalValueSets;
}
def FVSItem     : CoverageField<"Item", "item", 8, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalItems;
}
def FVSFirst    : CoverageField<"FirstAtom", "first_atom", 16, CovU32> {
  let semantic = "RangeStart"; let domain = DomainFunctionalValueAtoms;
  let pairedField = "AtomCount";
}
def FVSCount    : CoverageField<"AtomCount", "atom_count", 20, CovU32> {
  let semantic = "RangeCount"; let domain = DomainFunctionalValueAtoms;
  let pairedField = "FirstAtom";
}
def FVSBitWidth : CoverageField<"BitWidth", "bit_width", 24, CovU32>;
def FVSKind     : CoverageField<"Kind", "kind", 28, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalValueSetKind;
}
def FVSFlags    : CoverageField<"Flags", "flags", 32, CovU32> {
  let semantic = "Flags"; let validMask = 1;
}
def FVSSignedness : CoverageField<"Signedness", "signedness", 36, CovU32> {
  let semantic = "Enum"; let domain = DomainCoverageSignedness;
}
def FVSReserved : CoverageField<"Reserved", "reserved", 40, CovU64> {
  let semantic = "ReservedZero";
}
def FVSSetExpr  : CoverageField<"SetExpression", "set_expression", 48, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalExpressions;
  let nullable = 1;
}
def FunctionalValueSetRecord : CoverageRecord<"FunctionalValueSetRecord", FunctionalValueSets,
    [FVSId, FVSItem, FVSFirst, FVSCount, FVSBitWidth, FVSKind, FVSFlags,
     FVSSignedness, FVSReserved, FVSSetExpr]>;

def FVASet      : CoverageField<"ValueSet", "value_set", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalValueSets;
}
def FVALowReal  : CoverageField<"RealLowBits", "real_low_bits", 8, CovU64>;
def FVAHighReal : CoverageField<"RealHighBits", "real_high_bits", 16, CovU64>;
def FVAFirst    : CoverageField<"FirstLimb", "first_limb", 24, CovU32> {
  let semantic = "RangeStart"; let domain = DomainFunctionalValueLimbs;
  let pairedField = "LimbCount";
}
def FVACount    : CoverageField<"LimbCount", "limb_count", 28, CovU32> {
  let semantic = "RangeCount"; let domain = DomainFunctionalValueLimbs;
  let pairedField = "FirstLimb";
}
def FVAOrdinal  : CoverageField<"Ordinal", "ordinal", 32, CovU32>;
def FVAKind     : CoverageField<"Kind", "kind", 36, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalValueAtomKind;
}
def FVAFlags    : CoverageField<"Flags", "flags", 40, CovU32> {
  // Bits 2 and 3 are legal only on unresolved real tolerance intervals. Bits
  // 4 and 5 represent the IEEE 1800-2023 `$` real-range endpoints. Bit 6
  // distinguishes a source real range from an individual value. Handwritten
  // metric validation enforces their context-sensitive restrictions.
  let semantic = "Flags"; let validMask = 127;
}
def FVAReserved : CoverageField<"Reserved", "reserved", 44, CovU32> {
  let semantic = "ReservedZero";
}
def FVALowerExpr: CoverageField<"LowerExpression", "lower_expression", 48, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalExpressions;
  let nullable = 1;
}
def FVAUpperExpr: CoverageField<"UpperExpression", "upper_expression", 56, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalExpressions;
  let nullable = 1;
}
def FunctionalValueAtomRecord : CoverageRecord<"FunctionalValueAtomRecord", FunctionalValueAtoms,
    [FVASet, FVALowReal, FVAHighReal, FVAFirst, FVACount, FVAOrdinal,
     FVAKind, FVAFlags, FVAReserved, FVALowerExpr, FVAUpperExpr]>;

def FVLSet      : CoverageField<"ValueSet", "value_set", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalValueSets;
}
def FVLAtom     : CoverageField<"AtomOrdinal", "atom_ordinal", 8, CovU32>;
def FVLOrdinal  : CoverageField<"Ordinal", "ordinal", 12, CovU32>;
def FVLLowA     : CoverageField<"LowAval", "low_aval", 16, CovU64>;
def FVLLowB     : CoverageField<"LowBval", "low_bval", 24, CovU64>;
def FVLHighA    : CoverageField<"HighAval", "high_aval", 32, CovU64>;
def FVLHighB    : CoverageField<"HighBval", "high_bval", 40, CovU64>;
def FVLWildcard : CoverageField<"WildcardMask", "wildcard_mask", 48, CovU64>;
def FunctionalValueLimbRecord : CoverageRecord<"FunctionalValueLimbRecord", FunctionalValueLimbs,
    [FVLSet, FVLAtom, FVLOrdinal, FVLLowA, FVLLowB, FVLHighA, FVLHighB,
     FVLWildcard]>;

def FEId        : CoverageField<"Id", "id", 0, CovU64> {
  let semantic = "StableID"; let domain = DomainFunctionalExpressions;
}
def FEOwner     : CoverageField<"Owner", "owner", 8, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalExpressionOwners;
}
def FEOwnerKind : CoverageField<"OwnerKind", "owner_kind", 16, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalExpressionOwnerKind;
}
def FERole      : CoverageField<"Role", "role", 20, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalExpressionRole;
}
def FEResultKind: CoverageField<"ResultKind", "result_kind", 24, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalExpressionResultKind;
}
def FEFlags     : CoverageField<"Flags", "flags", 28, CovU32> {
  // Bit 0 records that a state-bin set expression has four-state elements.
  // Bits 1 and 2 identify block sampling events and their end boundary.
  // Handwritten validation enforces the result/role-specific contracts.
  let semantic = "Flags"; let validMask = 7;
}
def FESemanticDigest : CoverageField<"SemanticDigest", "semantic_digest", 32, CovDigest> {
  let semantic = "Digest";
}
def FEBitWidth  : CoverageField<"BitWidth", "bit_width", 64, CovU32>;
def FESignedness: CoverageField<"Signedness", "signedness", 68, CovU32> {
  let semantic = "Enum"; let domain = DomainCoverageSignedness;
}
def FEOwnerOrdinal : CoverageField<"OwnerOrdinal", "owner_ordinal", 72, CovU32>;
def FEOwnerSubordinal : CoverageField<"OwnerSubordinal", "owner_subordinal", 76, CovU32>;
def FEEvaluationPhase : CoverageField<"EvaluationPhase", "evaluation_phase", 80, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalExpressionEvaluationPhase;
}
def FEResultOrdinal : CoverageField<"ResultOrdinal", "result_ordinal", 84, CovU32>;
def FunctionalExpressionRecord : CoverageRecord<"FunctionalExpressionRecord", FunctionalExpressions,
    [FEId, FEOwner, FEOwnerKind, FERole, FEResultKind, FEFlags,
     FESemanticDigest,
     FEBitWidth, FESignedness, FEOwnerOrdinal, FEOwnerSubordinal,
     FEEvaluationPhase, FEResultOrdinal]>;

def FFId : CoverageField<"Id", "id", 0, CovU64> {
  let semantic = "StableID"; let domain = DomainFunctionalFormals;
}
def FFType : CoverageField<"Type", "type", 8, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalTypes;
}
def FFName : CoverageField<"Name", "name", 16, CovU32> {
  let semantic = "StringIndex"; let domain = DomainStrings;
}
def FFKind : CoverageField<"Kind", "kind", 20, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalFormalKind;
}
def FFDirection : CoverageField<"Direction", "direction", 24, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalFormalDirection;
}
def FFResultKind : CoverageField<"ResultKind", "result_kind", 28, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalExpressionResultKind;
}
def FFFlags : CoverageField<"Flags", "flags", 32, CovU32> {
  let semantic = "Flags"; let validMask = 1;
}
def FFBitWidth : CoverageField<"BitWidth", "bit_width", 36, CovU32>;
def FFSignedness : CoverageField<"Signedness", "signedness", 40, CovU32> {
  let semantic = "Enum"; let domain = DomainCoverageSignedness;
}
def FFOrdinal : CoverageField<"Ordinal", "ordinal", 44, CovU32>;
def FFDefaultExpression : CoverageField<"DefaultExpression", "default_expression", 48, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalExpressions;
  let nullable = 1;
}
def FFReserved : CoverageField<"Reserved", "reserved", 56, CovU64> {
  let semantic = "ReservedZero";
}
def FunctionalFormalRecord : CoverageRecord<"FunctionalFormalRecord",
    FunctionalFormals,
    [FFId, FFType, FFName, FFKind, FFDirection, FFResultKind, FFFlags,
     FFBitWidth, FFSignedness, FFOrdinal, FFDefaultExpression, FFReserved]>;

def FCType      : CoverageField<"Type", "type", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalTypes;
}
def FCConfig    : CoverageField<"Configuration", "configuration", 8, CovDigest> {
  let semantic = "Digest";
}
def FCFlags     : CoverageField<"Flags", "flags", 40, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def FCReserved  : CoverageField<"Reserved", "reserved", 44, CovU32> {
  let semantic = "ReservedZero";
}
def FunctionalConfigurationRecord : CoverageRecord<"FunctionalConfigurationRecord",
    FunctionalConfigurations, [FCType, FCConfig, FCFlags, FCReserved]>;

def RFIType       : CoverageField<"Type", "type", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalTypes;
}
def RFIConfig     : CoverageField<"Configuration", "configuration", 8, CovDigest> {
  let semantic = "Digest";
}
def RFIId         : CoverageField<"Id", "id", 40, CovU64> {
  let semantic = "StableID"; let domain = DomainResolvedFunctionalItems;
}
def RFITemplate   : CoverageField<"TemplateItem", "template_item", 48, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalItems;
}
def RFIName       : CoverageField<"Name", "name", 56, CovU32> {
  let semantic = "StringIndex"; let domain = DomainStrings;
}
def RFICategory   : CoverageField<"Kind", "kind", 60, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalItemKind;
}
def RFIFlags      : CoverageField<"Flags", "flags", 64, CovU32> {
  let semantic = "Flags"; let validMask = 1;
}
def RFIGoal       : CoverageField<"Goal", "goal", 68, CovU32>;
def RFIWeight     : CoverageField<"Weight", "weight", 72, CovU32>;
def RFIOrdinal    : CoverageField<"Ordinal", "ordinal", 76, CovU32>;
def RFIHierarchy  : CoverageField<"Hierarchy", "hierarchy", 80, CovU32> {
  let semantic = "StringIndex"; let domain = DomainStrings;
}
def RFIReserved0  : CoverageField<"Reserved0", "reserved", 84, CovU32> {
  let semantic = "ReservedZero";
}
def RFIReserved1  : CoverageField<"Reserved1", "reserved_tail", 88, CovU64> {
  let semantic = "ReservedZero";
}
def ResolvedFunctionalItemRecord : CoverageRecord<"ResolvedFunctionalItemRecord",
    ResolvedFunctionalItems, [RFIType, RFIConfig, RFIId, RFITemplate, RFIName,
      RFICategory, RFIFlags, RFIGoal, RFIWeight, RFIOrdinal, RFIHierarchy,
      RFIReserved0, RFIReserved1]>;

def RFBType       : CoverageField<"Type", "type", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalTypes;
}
def RFBConfig     : CoverageField<"Configuration", "configuration", 8, CovDigest> {
  let semantic = "Digest";
}
def RFBId         : CoverageField<"Id", "id", 40, CovU64> {
  let semantic = "StableID"; let domain = DomainResolvedFunctionalBins;
}
def RFBTemplate   : CoverageField<"TemplateBin", "template_bin", 48, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalBins;
  let nullable = 1;
}
def RFBItem       : CoverageField<"Item", "item", 56, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalItems;
}
def RFBName       : CoverageField<"Name", "name", 64, CovU32> {
  let semantic = "StringIndex"; let domain = DomainStrings;
}
def RFBCategory   : CoverageField<"Kind", "kind", 68, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalBinKind;
}
def RFBFlags      : CoverageField<"Flags", "flags", 72, CovU32> {
  let semantic = "Flags"; let validMask = 223;
}
def RFBOrdinal    : CoverageField<"Ordinal", "ordinal", 76, CovU32>;
def RFBExpansionOrdinal : CoverageField<"ExpansionOrdinal", "expansion_ordinal", 80, CovU32>;
def RFBPadding    : CoverageField<"Padding", "padding", 84, CovU32> {
  let semantic = "ReservedZero";
}
def RFBAtLeast    : CoverageField<"AtLeast", "at_least", 88, CovU64>;
def RFBHierarchy  : CoverageField<"Hierarchy", "hierarchy", 96, CovU32> {
  let semantic = "StringIndex"; let domain = DomainStrings;
}
def RFBReserved   : CoverageField<"Reserved", "reserved", 100, CovU32> {
  let semantic = "ReservedZero";
}
def ResolvedFunctionalBinRecord : CoverageRecord<"ResolvedFunctionalBinRecord",
    ResolvedFunctionalBins, [RFBType, RFBConfig, RFBId, RFBTemplate, RFBItem,
      RFBName, RFBCategory, RFBFlags, RFBOrdinal, RFBExpansionOrdinal,
      RFBPadding, RFBAtLeast, RFBHierarchy, RFBReserved]>;

def RTSType       : CoverageField<"Type", "type", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalTypes;
}
def RTSConfig     : CoverageField<"Configuration", "configuration", 8, CovDigest> {
  let semantic = "Digest";
}
def RTSBin        : CoverageField<"Bin", "bin", 40, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalBins;
}
def RTSAlternative: CoverageField<"Alternative", "alternative", 48, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedTransitionAlternatives;
}
def RTSValueSet   : CoverageField<"ValueSet", "value_set", 56, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalValueSets;
}
def RTSLower      : CoverageField<"LowerBound", "lower_bound", 64, CovU64>;
def RTSUpper      : CoverageField<"UpperBound", "upper_bound", 72, CovU64>;
def RTSOrdinal    : CoverageField<"Ordinal", "ordinal", 80, CovU32>;
def RTSFlags      : CoverageField<"Flags", "flags", 84, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def ResolvedTransitionStepRecord : CoverageRecord<"ResolvedTransitionStepRecord",
    ResolvedTransitionSteps, [RTSType, RTSConfig, RTSBin, RTSAlternative,
      RTSValueSet, RTSLower, RTSUpper, RTSOrdinal, RTSFlags]>;

def RCPType       : CoverageField<"Type", "type", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalTypes;
}
def RCPConfig     : CoverageField<"Configuration", "configuration", 8, CovDigest> {
  let semantic = "Digest";
}
def RCPCross      : CoverageField<"Cross", "cross", 40, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalItems;
}
def RCPRetain     : CoverageField<"RetainAutoPolicy", "retain_auto_policy", 48, CovU32> {
  let semantic = "Enum"; let domain = DomainCrossRetainAutoPolicy;
}
def RCPFlags      : CoverageField<"Flags", "flags", 52, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def RCPFirstAutomaticBinCountLimb : CoverageField<"FirstAutomaticBinCountLimb", "first_automatic_bin_count_limb", 56, CovU32>;
def RCPAutomaticBinCountLimbCount : CoverageField<"AutomaticBinCountLimbCount", "automatic_bin_count_limb_count", 60, CovU32>;
def RCPRootNode : CoverageField<"RootNode", "root_node", 64, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedCrossAutomaticNodes;
  let nullable = 1;
}
def ResolvedCrossPlanRecord : CoverageRecord<"ResolvedCrossPlanRecord",
    ResolvedCrossPlans,
    [RCPType, RCPConfig, RCPCross, RCPRetain, RCPFlags,
     RCPFirstAutomaticBinCountLimb, RCPAutomaticBinCountLimbCount,
     RCPRootNode]>;

def RCABCLValue : CoverageField<"Value", "value", 0, CovU64>;
def ResolvedCrossAutomaticBinCountLimbRecord
    : CoverageRecord<"ResolvedCrossAutomaticBinCountLimbRecord",
      ResolvedCrossAutomaticBinCountLimbs, [RCABCLValue]>;

def RCANType : CoverageField<"Type", "type", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalTypes;
}
def RCANConfig : CoverageField<"Configuration", "configuration", 8, CovDigest> {
  let semantic = "Digest";
}
def RCANCross : CoverageField<"Cross", "cross", 40, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalItems;
}
def RCANId : CoverageField<"Id", "id", 48, CovU64> {
  let semantic = "StableID"; let domain = DomainResolvedCrossAutomaticNodes;
}
def RCANTargetOrdinal : CoverageField<"TargetOrdinal", "target_ordinal", 56, CovU32>;
def RCANFirstEdge : CoverageField<"FirstEdge", "first_edge", 60, CovU32> {
  let semantic = "RangeStart"; let domain = DomainResolvedCrossAutomaticEdges;
  let pairedField = "EdgeCount";
}
def RCANEdgeCount : CoverageField<"EdgeCount", "edge_count", 64, CovU32> {
  let semantic = "RangeCount"; let domain = DomainResolvedCrossAutomaticEdges;
  let pairedField = "FirstEdge";
}
def RCANFlags : CoverageField<"Flags", "flags", 68, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def ResolvedCrossAutomaticNodeRecord : CoverageRecord<
    "ResolvedCrossAutomaticNodeRecord", ResolvedCrossAutomaticNodes,
    [RCANType, RCANConfig, RCANCross, RCANId, RCANTargetOrdinal,
     RCANFirstEdge, RCANEdgeCount, RCANFlags]>;

def RCAENode : CoverageField<"Node", "node", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedCrossAutomaticNodes;
}
def RCAEBin : CoverageField<"Bin", "bin", 8, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalBins;
}
def RCAEChild : CoverageField<"Child", "child", 16, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedCrossAutomaticNodes;
  let nullable = 1;
}
def RCAEOrdinal : CoverageField<"Ordinal", "ordinal", 24, CovU32>;
def RCAEFlags : CoverageField<"Flags", "flags", 28, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def ResolvedCrossAutomaticEdgeRecord : CoverageRecord<
    "ResolvedCrossAutomaticEdgeRecord", ResolvedCrossAutomaticEdges,
    [RCAENode, RCAEBin, RCAEChild, RCAEOrdinal, RCAEFlags]>;

def RCSBType      : CoverageField<"Type", "type", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalTypes;
}
def RCSBConfig    : CoverageField<"Configuration", "configuration", 8, CovDigest> {
  let semantic = "Digest";
}
def RCSBCross     : CoverageField<"Cross", "cross", 40, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalItems;
}
def RCSBNode      : CoverageField<"Node", "node", 48, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainCrossSelectorNodes;
}
def RCSBValueSet  : CoverageField<"ValueSet", "value_set", 56, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalValueSets;
  let nullable = 1;
}
def RCSBWithExpression : CoverageField<"WithExpression", "with_expression", 64, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalExpressions;
  let nullable = 1;
}
def RCSBTupleSet  : CoverageField<"TupleSet", "tuple_set", 72, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalTupleSets;
  let nullable = 1;
}
def RCSBMatchesPolicy : CoverageField<"MatchesPolicy", "matches_policy", 80, CovU32> {
  let semantic = "Enum"; let domain = DomainCrossMatchesPolicy;
}
def RCSBFlags     : CoverageField<"Flags", "flags", 84, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def RCSBMatchesCount : CoverageField<"MatchesCount", "matches_count", 88, CovU64>;
def ResolvedCrossSelectorBindingRecord : CoverageRecord<
    "ResolvedCrossSelectorBindingRecord", ResolvedCrossSelectorBindings,
    [RCSBType, RCSBConfig, RCSBCross, RCSBNode, RCSBValueSet,
     RCSBWithExpression, RCSBTupleSet, RCSBMatchesPolicy, RCSBFlags,
     RCSBMatchesCount]>;

def RVSSetType    : CoverageField<"Type", "type", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalTypes;
}
def RVSConfig     : CoverageField<"Configuration", "configuration", 8, CovDigest> {
  let semantic = "Digest";
}
def RVSId         : CoverageField<"Id", "id", 40, CovU64> {
  let semantic = "StableID"; let domain = DomainResolvedFunctionalValueSets;
}
def RVSTemplate   : CoverageField<"TemplateValueSet", "template_value_set", 48, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalValueSets;
  let nullable = 1;
}
def RVSItem       : CoverageField<"Item", "item", 56, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalItems;
}
def RVSFirst      : CoverageField<"FirstAtom", "first_atom", 64, CovU32> {
  let semantic = "RangeStart"; let domain = DomainResolvedFunctionalValueAtoms;
  let pairedField = "AtomCount";
}
def RVSCount      : CoverageField<"AtomCount", "atom_count", 68, CovU32> {
  let semantic = "RangeCount"; let domain = DomainResolvedFunctionalValueAtoms;
  let pairedField = "FirstAtom";
}
def RVSBitWidth   : CoverageField<"BitWidth", "bit_width", 72, CovU32>;
def RVSKind       : CoverageField<"Kind", "kind", 76, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalValueSetKind;
}
def RVSFlags      : CoverageField<"Flags", "flags", 80, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def RVSSignedness : CoverageField<"Signedness", "signedness", 84, CovU32> {
  let semantic = "Enum"; let domain = DomainCoverageSignedness;
}
def RVSOwnerBin    : CoverageField<"OwnerBin", "owner_bin", 88, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalBins;
  let nullable = 1;
}
def RVSOwnerSelector : CoverageField<"OwnerSelector", "owner_selector", 96, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainCrossSelectorNodes;
  let nullable = 1;
}
def RVSOwnerOrdinal : CoverageField<"OwnerOrdinal", "owner_ordinal", 104, CovU32>;
def RVSOwnerSubordinal : CoverageField<"OwnerSubordinal", "owner_subordinal", 108, CovU32>;
def RVSRole       : CoverageField<"Role", "role", 112, CovU32> {
  let semantic = "Enum"; let domain = DomainResolvedFunctionalValueSetRole;
}
def RVSReserved  : CoverageField<"Reserved", "reserved", 116, CovU32> {
  let semantic = "ReservedZero";
}
def ResolvedFunctionalValueSetRecord : CoverageRecord<
    "ResolvedFunctionalValueSetRecord", ResolvedFunctionalValueSets,
    [RVSSetType, RVSConfig, RVSId, RVSTemplate, RVSItem, RVSFirst, RVSCount,
     RVSBitWidth, RVSKind, RVSFlags, RVSSignedness, RVSOwnerBin,
     RVSOwnerSelector, RVSOwnerOrdinal, RVSOwnerSubordinal, RVSRole,
     RVSReserved]>;

def RFASet      : CoverageField<"ValueSet", "value_set", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalValueSets;
}
def RFAFirst    : CoverageField<"FirstLimb", "first_limb", 24, CovU32> {
  let semantic = "RangeStart"; let domain = DomainResolvedFunctionalValueLimbs;
  let pairedField = "LimbCount";
}
def RFACount    : CoverageField<"LimbCount", "limb_count", 28, CovU32> {
  let semantic = "RangeCount"; let domain = DomainResolvedFunctionalValueLimbs;
  let pairedField = "FirstLimb";
}
def ResolvedFunctionalValueAtomRecord : CoverageRecord<
    "ResolvedFunctionalValueAtomRecord", ResolvedFunctionalValueAtoms,
    [RFASet, FVALowReal, FVAHighReal, RFAFirst, RFACount, FVAOrdinal,
     FVAKind, FVAFlags, FVAReserved]>;

def RFLSet      : CoverageField<"ValueSet", "value_set", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalValueSets;
}
def ResolvedFunctionalValueLimbRecord : CoverageRecord<
    "ResolvedFunctionalValueLimbRecord", ResolvedFunctionalValueLimbs,
    [RFLSet, FVLAtom, FVLOrdinal, FVLLowA, FVLLowB, FVLHighA, FVLHighB,
     FVLWildcard]>;

def FBPBin       : CoverageField<"Bin", "bin", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalBins;
}
def FBPValueSet  : CoverageField<"ValueSet", "value_set", 8, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalValueSets;
  let nullable = 1;
}
def FBPIff       : CoverageField<"IffExpression", "iff_expression", 16, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalExpressions;
  let nullable = 1;
}
def FBPCardExpr  : CoverageField<"CardinalityExpression", "cardinality_expression", 24, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalExpressions;
  let nullable = 1;
}
def FBPCard      : CoverageField<"ArrayCardinality", "array_cardinality", 32, CovU64>;
def FBPArrayMode : CoverageField<"ArrayMode", "array_mode", 40, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalBinArrayMode;
}
def FBPDistrib   : CoverageField<"Distribution", "distribution", 44, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalBinDistributionKind;
}
def FBPFlags     : CoverageField<"Flags", "flags", 48, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def FBPReserved  : CoverageField<"Reserved", "reserved", 52, CovU32> {
  let semantic = "ReservedZero";
}
def FunctionalBinPlanRecord : CoverageRecord<"FunctionalBinPlanRecord",
    FunctionalBinPlans, [FBPBin, FBPValueSet, FBPIff, FBPCardExpr, FBPCard,
      FBPArrayMode, FBPDistrib, FBPFlags, FBPReserved]>;

def FTSId       : CoverageField<"Id", "id", 0, CovU64> {
  let semantic = "StableID"; let domain = DomainFunctionalTupleSets;
}
def FTSCross    : CoverageField<"Cross", "cross", 8, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalItems;
}
def FTSNode     : CoverageField<"Selector", "selector", 16, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainCrossSelectorNodes;
}
def FTSFirst    : CoverageField<"FirstTuple", "first_tuple", 24, CovU32> {
  let semantic = "RangeStart"; let domain = DomainFunctionalTupleSetTuples;
  let pairedField = "TupleCount";
}
def FTSCount    : CoverageField<"TupleCount", "tuple_count", 28, CovU32> {
  let semantic = "RangeCount"; let domain = DomainFunctionalTupleSetTuples;
  let pairedField = "FirstTuple";
}
def FTSMode     : CoverageField<"ElementMode", "element_mode", 32, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalTupleElementMode;
}
def FTSFlags    : CoverageField<"Flags", "flags", 36, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def FunctionalTupleSetRecord : CoverageRecord<"FunctionalTupleSetRecord",
    FunctionalTupleSets, [FTSId, FTSCross, FTSNode, FTSFirst, FTSCount,
      FTSMode, FTSFlags]>;

def FTTSet      : CoverageField<"TupleSet", "tuple_set", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalTupleSets;
}
def FTTId       : CoverageField<"Id", "id", 8, CovU64> {
  let semantic = "StableID"; let domain = DomainFunctionalTupleSetTupleIDs;
}
def FTTFirst    : CoverageField<"FirstComponent", "first_component", 16, CovU32> {
  let semantic = "RangeStart"; let domain = DomainFunctionalTupleSetComponents;
  let pairedField = "ComponentCount";
}
def FTTCount    : CoverageField<"ComponentCount", "component_count", 20, CovU32> {
  let semantic = "RangeCount"; let domain = DomainFunctionalTupleSetComponents;
  let pairedField = "FirstComponent";
}
def FTTOrdinal  : CoverageField<"Ordinal", "ordinal", 24, CovU32>;
def FTTFlags    : CoverageField<"Flags", "flags", 28, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def FunctionalTupleRecord : CoverageRecord<"FunctionalTupleRecord",
    FunctionalTupleSetTuples, [FTTSet, FTTId, FTTFirst, FTTCount, FTTOrdinal,
      FTTFlags]>;

def FTCCTuple   : CoverageField<"Tuple", "tuple", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalTupleSetTupleIDs;
}
def FTCCTarget  : CoverageField<"Target", "target", 8, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalItems;
}
def FTCCBin     : CoverageField<"Bin", "bin", 16, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalBins;
  let nullable = 1;
}
def FTCCSet     : CoverageField<"ValueSet", "value_set", 24, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalValueSets;
  let nullable = 1;
}
def FTCCOrdinal : CoverageField<"Ordinal", "ordinal", 32, CovU32>;
def FTCCFlags   : CoverageField<"Flags", "flags", 36, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def FunctionalTupleComponentRecord : CoverageRecord<
    "FunctionalTupleComponentRecord", FunctionalTupleSetComponents,
    [FTCCTuple, FTCCTarget, FTCCBin, FTCCSet, FTCCOrdinal, FTCCFlags]>;

def FOPOwner    : CoverageField<"Owner", "owner", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalOptionOwners;
}
def FOPExpr     : CoverageField<"Expression", "expression", 8, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalExpressions;
}
def FOPOwnerKind: CoverageField<"OwnerKind", "owner_kind", 16, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalConfigurationOptionOwnerKind;
}
def FOPScope    : CoverageField<"Scope", "scope", 20, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalOptionScopeKind;
}
def FOPOption   : CoverageField<"Option", "option", 24, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalConfigurationOptionKind;
}
def FOPOrdinal  : CoverageField<"Ordinal", "ordinal", 28, CovU32>;
def FOPFlags    : CoverageField<"Flags", "flags", 32, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def FOPReserved : CoverageField<"Reserved", "reserved", 36, CovU32> {
  let semantic = "ReservedZero";
}
def FunctionalOptionPlanRecord : CoverageRecord<"FunctionalOptionPlanRecord",
    FunctionalOptionPlans, [FOPOwner, FOPExpr, FOPOwnerKind, FOPScope,
      FOPOption, FOPOrdinal, FOPFlags, FOPReserved]>;

def FCOType     : CoverageField<"Type", "type", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalTypes;
}
def FCOConfig   : CoverageField<"Configuration", "configuration", 8, CovDigest> {
  let semantic = "Digest";
}
def FCOOwner    : CoverageField<"Owner", "owner", 40, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalOptionOwners;
}
def FCOOwnerKind: CoverageField<"OwnerKind", "owner_kind", 48, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalConfigurationOptionOwnerKind;
}
def FCOScope    : CoverageField<"Scope", "scope", 52, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalOptionScopeKind;
}
def FCOOption   : CoverageField<"Option", "option", 56, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalConfigurationOptionKind;
}
def FCOValueKind: CoverageField<"ValueKind", "value_kind", 60, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalConfigurationValueKind;
}
def FCOFlags    : CoverageField<"Flags", "flags", 64, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def FCOPadding  : CoverageField<"Padding", "padding", 68, CovU32> {
  let semantic = "ReservedZero";
}
def FCOValue    : CoverageField<"Value", "value", 72, CovU64>;
def FCOString   : CoverageField<"String", "string", 80, CovU32> {
  let semantic = "StringIndex"; let domain = DomainStrings;
  let nullable = 1;
}
def FCOReserved : CoverageField<"Reserved", "reserved", 84, CovU32> {
  let semantic = "ReservedZero";
}
def FunctionalConfigurationOptionRecord : CoverageRecord<
    "FunctionalConfigurationOptionRecord", FunctionalConfigurationOptions,
    [FCOType, FCOConfig, FCOOwner, FCOOwnerKind, FCOScope, FCOOption,
     FCOValueKind, FCOFlags, FCOPadding, FCOValue, FCOString, FCOReserved]>;

def RFBPType    : CoverageField<"Type", "type", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalTypes;
}
def RFBPConfig  : CoverageField<"Configuration", "configuration", 8, CovDigest> {
  let semantic = "Digest";
}
def RFBPBin     : CoverageField<"Bin", "bin", 40, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalBins;
}
def RFBPSet     : CoverageField<"ValueSet", "value_set", 48, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalValueSets;
  let nullable = 1;
}
def RFBPIff     : CoverageField<"IffExpression", "iff_expression", 56, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalExpressions;
  let nullable = 1;
}
def RFBPCardExpr: CoverageField<"CardinalityExpression", "cardinality_expression", 64, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalExpressions;
  let nullable = 1;
}
def RFBPCard    : CoverageField<"ArrayCardinality", "array_cardinality", 72, CovU64>;
def RFBPArray   : CoverageField<"ArrayMode", "array_mode", 80, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalBinArrayMode;
}
def RFBPDistrib : CoverageField<"Distribution", "distribution", 84, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalBinDistributionKind;
}
def RFBPFlags   : CoverageField<"Flags", "flags", 88, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def RFBPReserved: CoverageField<"Reserved", "reserved", 92, CovU32> {
  let semantic = "ReservedZero";
}
def ResolvedFunctionalBinPlanRecord : CoverageRecord<
    "ResolvedFunctionalBinPlanRecord", ResolvedFunctionalBinPlans,
    [RFBPType, RFBPConfig, RFBPBin, RFBPSet, RFBPIff, RFBPCardExpr,
     RFBPCard, RFBPArray, RFBPDistrib, RFBPFlags, RFBPReserved]>;

def RFTSType    : CoverageField<"Type", "type", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalTypes;
}
def RFTSConfig  : CoverageField<"Configuration", "configuration", 8, CovDigest> {
  let semantic = "Digest";
}
def RFTSId      : CoverageField<"Id", "id", 40, CovU64> {
  let semantic = "StableID"; let domain = DomainResolvedFunctionalTupleSets;
}
def RFTSTemplate: CoverageField<"TemplateTupleSet", "template_tuple_set", 48, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalTupleSets;
  let nullable = 1;
}
def RFTSCross   : CoverageField<"Cross", "cross", 56, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalItems;
}
def RFTSSelector: CoverageField<"Selector", "selector", 64, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainCrossSelectorNodes;
}
def RFTSFirst   : CoverageField<"FirstTuple", "first_tuple", 72, CovU32> {
  let semantic = "RangeStart"; let domain = DomainResolvedFunctionalTupleSetTuples;
  let pairedField = "TupleCount";
}
def RFTSCount   : CoverageField<"TupleCount", "tuple_count", 76, CovU32> {
  let semantic = "RangeCount"; let domain = DomainResolvedFunctionalTupleSetTuples;
  let pairedField = "FirstTuple";
}
def RFTSMode    : CoverageField<"ElementMode", "element_mode", 80, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalTupleElementMode;
}
def RFTSFlags   : CoverageField<"Flags", "flags", 84, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def ResolvedFunctionalTupleSetRecord : CoverageRecord<
    "ResolvedFunctionalTupleSetRecord", ResolvedFunctionalTupleSets,
    [RFTSType, RFTSConfig, RFTSId, RFTSTemplate, RFTSCross, RFTSSelector,
     RFTSFirst, RFTSCount, RFTSMode, RFTSFlags]>;

def RFTTSet     : CoverageField<"TupleSet", "tuple_set", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalTupleSets;
}
def RFTTId      : CoverageField<"Id", "id", 8, CovU64> {
  let semantic = "StableID"; let domain = DomainResolvedFunctionalTupleSetTupleIDs;
}
def RFTTFirst   : CoverageField<"FirstComponent", "first_component", 16, CovU32> {
  let semantic = "RangeStart"; let domain = DomainResolvedFunctionalTupleSetComponents;
  let pairedField = "ComponentCount";
}
def RFTTCount   : CoverageField<"ComponentCount", "component_count", 20, CovU32> {
  let semantic = "RangeCount"; let domain = DomainResolvedFunctionalTupleSetComponents;
  let pairedField = "FirstComponent";
}
def RFTTOrdinal : CoverageField<"Ordinal", "ordinal", 24, CovU32>;
def RFTTFlags   : CoverageField<"Flags", "flags", 28, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def ResolvedFunctionalTupleRecord : CoverageRecord<
    "ResolvedFunctionalTupleRecord", ResolvedFunctionalTupleSetTuples,
    [RFTTSet, RFTTId, RFTTFirst, RFTTCount, RFTTOrdinal, RFTTFlags]>;

def RFTCCTuple  : CoverageField<"Tuple", "tuple", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalTupleSetTupleIDs;
}
def RFTCCTarget : CoverageField<"Target", "target", 8, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalItems;
}
def RFTCCBin    : CoverageField<"Bin", "bin", 16, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalBins;
  let nullable = 1;
}
def RFTCCSet    : CoverageField<"ValueSet", "value_set", 24, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalValueSets;
  let nullable = 1;
}
def RFTCCOrdinal: CoverageField<"Ordinal", "ordinal", 32, CovU32>;
def RFTCCFlags  : CoverageField<"Flags", "flags", 36, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def ResolvedFunctionalTupleComponentRecord : CoverageRecord<
    "ResolvedFunctionalTupleComponentRecord",
    ResolvedFunctionalTupleSetComponents,
    [RFTCCTuple, RFTCCTarget, RFTCCBin, RFTCCSet, RFTCCOrdinal, RFTCCFlags]>;

def RTAType : CoverageField<"Type", "type", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalTypes;
}
def RTAConfig : CoverageField<"Configuration", "configuration", 8, CovDigest> {
  let semantic = "Digest";
}
def RTAId : CoverageField<"Id", "id", 40, CovU64> {
  let semantic = "StableID"; let domain = DomainResolvedTransitionAlternatives;
}
def RTABin : CoverageField<"Bin", "bin", 48, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalBins;
}
def RTATemplate : CoverageField<"TemplateAlternativeOrdinal", "template_alternative_ordinal", 56, CovU32>;
def RTAExpansion : CoverageField<"ExpansionOrdinal", "expansion_ordinal", 60, CovU32>;
def RTAFirst : CoverageField<"FirstStep", "first_step", 64, CovU32> {
  let semantic = "RangeStart"; let domain = DomainResolvedTransitionSteps;
  let pairedField = "StepCount";
}
def RTACount : CoverageField<"StepCount", "step_count", 68, CovU32> {
  let semantic = "RangeCount"; let domain = DomainResolvedTransitionSteps;
  let pairedField = "FirstStep";
}
def RTAOrdinal : CoverageField<"Ordinal", "ordinal", 72, CovU32>;
def RTAFlags : CoverageField<"Flags", "flags", 76, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def ResolvedTransitionAlternativeRecord : CoverageRecord<
    "ResolvedTransitionAlternativeRecord", ResolvedTransitionAlternatives,
    [RTAType, RTAConfig, RTAId, RTABin, RTATemplate, RTAExpansion, RTAFirst,
     RTACount, RTAOrdinal, RTAFlags]>;

def RBGType : CoverageField<"Type", "type", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalTypes;
}
def RBGConfig : CoverageField<"Configuration", "configuration", 8, CovDigest> {
  let semantic = "Digest";
}
def RBGItem : CoverageField<"Item", "item", 40, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalItems;
}
def RBGTemplate : CoverageField<"TemplateBin", "template_bin", 48, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalBins;
  let nullable = 1;
}
def RBGFirst : CoverageField<"FirstBin", "first_bin", 56, CovU32> {
  let semantic = "RangeStart"; let domain = DomainResolvedFunctionalBinRows;
  let pairedField = "BinCount";
}
def RBGCount : CoverageField<"BinCount", "bin_count", 60, CovU32> {
  let semantic = "RangeCount"; let domain = DomainResolvedFunctionalBinRows;
  let pairedField = "FirstBin";
}
def RBGCardinality : CoverageField<"ArrayCardinality", "array_cardinality", 64, CovU64>;
def RBGMode : CoverageField<"ArrayMode", "array_mode", 72, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalBinArrayMode;
}
def RBGDistribution : CoverageField<"Distribution", "distribution", 76, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalBinDistributionKind;
}
def RBGFlags : CoverageField<"Flags", "flags", 80, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def RBGKind : CoverageField<"Kind", "kind", 84, CovU32> {
  let semantic = "Enum"; let domain = DomainFunctionalBinKind;
}
def ResolvedFunctionalBinGroupRecord : CoverageRecord<
    "ResolvedFunctionalBinGroupRecord", ResolvedFunctionalBinGroups,
    [RBGType, RBGConfig, RBGItem, RBGTemplate, RBGFirst, RBGCount,
     RBGCardinality, RBGMode, RBGDistribution, RBGFlags, RBGKind]>;

def REGType : CoverageField<"Type", "type", 0, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalTypes;
}
def REGConfig : CoverageField<"Configuration", "configuration", 8, CovDigest> {
  let semantic = "Digest";
}
def REGItem : CoverageField<"Item", "item", 40, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainResolvedFunctionalItems;
}
def REGTemplate : CoverageField<"TemplateBin", "template_bin", 48, CovU64> {
  let semantic = "StableIDRef"; let domain = DomainFunctionalBins;
}
def REGTemplateAlternative : CoverageField<"TemplateAlternativeOrdinal", "template_alternative_ordinal", 56, CovU32>;
def REGFirst : CoverageField<"FirstAlternative", "first_alternative", 60, CovU32> {
  let semantic = "RangeStart"; let domain = DomainResolvedTransitionAlternativeRows;
  let pairedField = "AlternativeCount";
}
def REGCount : CoverageField<"AlternativeCount", "alternative_count", 64, CovU32> {
  let semantic = "RangeCount"; let domain = DomainResolvedTransitionAlternativeRows;
  let pairedField = "FirstAlternative";
}
def REGFlags : CoverageField<"Flags", "flags", 68, CovU32> {
  let semantic = "Flags"; let validMask = 0;
}
def REGExpansionCount : CoverageField<"ExpansionCount", "expansion_count", 72, CovU64>;
def ResolvedTransitionExpansionGroupRecord : CoverageRecord<
    "ResolvedTransitionExpansionGroupRecord",
    ResolvedTransitionExpansionGroups,
    [REGType, REGConfig, REGItem, REGTemplate, REGTemplateAlternative,
     REGFirst, REGCount, REGFlags, REGExpansionCount]>;
