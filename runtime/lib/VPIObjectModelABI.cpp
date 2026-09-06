//===- VPIObjectModelABI.cpp - generated VPI ABI checks ------------------===//

#include "VPIInternal.h"
#include "obelisk/Reflection/VPIObjectModel.h"

#define OBELISK_CHECK_VPI_VALUE(apiName, schemaValue)                          \
  static_assert(static_cast<uint32_t>(apiName) == uint32_t(schemaValue),       \
                #apiName " differs from the generated VPI schema");

OBELISK_FOR_EACH_VPI_OBJECT_KIND(OBELISK_CHECK_VPI_VALUE)
OBELISK_FOR_EACH_VPI_RELATION(OBELISK_CHECK_VPI_VALUE)
OBELISK_FOR_EACH_VPI_PROPERTY(OBELISK_CHECK_VPI_VALUE)

#undef OBELISK_CHECK_VPI_VALUE

using namespace obelisk::reflection;

static_assert(vpiObjectModelImageHeaderSize == 120);
static_assert(vpiObjectModelImageTraversalSize == 8);
static_assert(vpiObjectModelImagePropertySize == 6);
static_assert(vpiObjectModelImageValuePolicySize == 8);
static_assert(vpiObjectModelImageArrayValuePolicySize == 4);
static_assert(vpiObjectModelImageIndexedAccessSize == 12);
static_assert(vpiObjectModelImageIndexedTypeResultSize == 8);
static_assert(vpiObjectModelImageIntegerPropertyValueSize == 12);
static_assert(findVPIIntegerPropertyValue(vpiNetType, vpiTriReg)->value ==
              vpiTriReg);
static_assert(findVPIIntegerPropertyValue(vpiChargeStrength, vpiMediumCharge)
                  ->symbolicName[0] == '\0');
static_assert(findVPIIndexedAccess(vpiPort)->terminalResult == vpiPortBit);
static_assert(!findVPIIndexedAccess(vpiPort)->mapSemanticType);
static_assert(findVPIIndexedAccess(vpiRegArray)->unpackedFallback ==
              vpiRegArray);
static_assert(findVPIIndexedAccess(vpiInterconnectArray)->packedFallback ==
              vpiInterconnectNet);
static_assert(findVPIIndexedTypeResult(VPIIndexedAccessKind::VariableElement,
                                       vpiIntTypespec)
                  ->resultType == vpiIntVar);
static_assert(findVPIIndexedTypeResult(VPIIndexedAccessKind::NetElement,
                                       vpiLogicTypespec)
                  ->resultType == vpiNet);
static_assert(findVPIProperty(vpiPort, vpiDirection)->valueKind ==
              VPIPropertyValueKind::Integer);
static_assert(findVPIProperty(vpiPort, vpiScalar)->valueKind ==
              VPIPropertyValueKind::Boolean);
static_assert(findVPIProperty(vpiReg, vpiScalar)->valueKind ==
              VPIPropertyValueKind::Boolean);
static_assert(findVPIProperty(vpiNet, vpiVector)->valueKind ==
              VPIPropertyValueKind::Boolean);
static_assert(findVPIProperty(vpiPortBit, vpiPortIndex)->valueKind ==
              VPIPropertyValueKind::Integer);
static_assert(findVPIProperty(vpiReg, vpiDirection) == nullptr);

static_assert(static_cast<uint8_t>(VPIValueFormat::BinStr) == vpiBinStrVal);
static_assert(static_cast<uint8_t>(VPIValueFormat::ObjType) == vpiObjTypeVal);
static_assert(findVPIValuePolicy(vpiReg)->defaultFormat ==
              VPIValueDefaultFormat::ScalarOrVector);
static_assert(findVPIValuePolicy(vpiIntVar)->defaultFormat ==
              VPIValueDefaultFormat::Integer);
static_assert(findVPIValuePolicy(vpiRealVar)->defaultFormat ==
              VPIValueDefaultFormat::Real);
static_assert(findVPIValuePolicy(vpiStringVar)->defaultFormat ==
              VPIValueDefaultFormat::String);
static_assert(findVPIValuePolicy(vpiTimeVar)->defaultFormat ==
              VPIValueDefaultFormat::Time);
static_assert(findVPIValuePolicy(vpiOperation)->readSemantics ==
              VPIValueReadSemantics::Evaluate);
static_assert(findVPIValuePolicy(vpiNet)->readSemantics ==
              VPIValueReadSemantics::Snapshot);
static_assert(acceptsVPIValueFormat(*findVPIValuePolicy(vpiTableEntry),
                                    vpiStringVal));
static_assert(!acceptsVPIValueFormat(*findVPIValuePolicy(vpiTableEntry),
                                     vpiIntVal));
static_assert(findVPIValuePolicy(vpiPort) == nullptr);
static_assert(findVPIValuePolicy(vpiPortBit) == nullptr);
static_assert(findVPIValuePolicy(vpiRegArray) == nullptr);
static_assert(findVPIValuePolicy(vpiClassVar) == nullptr);
static_assert(findVPIValuePolicy(vpiVirtualInterfaceVar) == nullptr);
static_assert(acceptsVPIArrayValueFormat(vpiVectorVal, vpiLogicVar));
static_assert(acceptsVPIArrayValueFormat(vpiRawFourStateVal, vpiEnumVar));
static_assert(!acceptsVPIArrayValueFormat(vpiShortIntVal, vpiShortIntNet));
static_assert(!acceptsVPIArrayValueFormat(vpiVectorVal, vpiRealVar));

static_assert(findVPIObjectKind(vpiModule)->role == VPIObjectRole::Concrete);
static_assert(findVPIObjectKind(vpiReturn) == nullptr);
static_assert(findVPIObjectSelector(vpiReturn)->role ==
              VPIObjectRole::RelationOnly);
static_assert(findVPIObjectSelector(vpiTypespec)->role ==
              VPIObjectRole::AbstractSelector);
static_assert(findVPIObjectSelector(vpiMemory)->role ==
              VPIObjectRole::CompatibilitySelector);
static_assert(findVPIObjectSelector(vpiFor)->families &
              vpiFamilyMask(VPIObjectFamily::Scope));
static_assert(findVPIObjectSelector(vpiModuleArray)->families &
              vpiFamilyMask(VPIObjectFamily::Array));
static_assert(!(findVPIObjectSelector(vpiModuleArray)->families &
                vpiFamilyMask(VPIObjectFamily::Scope)));

static_assert(findVPIRelation(vpiForInitStmt)->cardinality ==
              VPIRelationCardinality::OneOrMany);
static_assert(findVPIRelation(vpiForIncStmt)->cardinality ==
              VPIRelationCardinality::OneOrMany);
static_assert(findVPIRelation(vpiIndex)->cardinality ==
              VPIRelationCardinality::OneOrMany);
static_assert(findVPIRelation(vpiCondition)->cardinality ==
              VPIRelationCardinality::OneOrMany);
static_assert(findVPIRelation(vpiUse)->cardinality ==
              VPIRelationCardinality::OneOrMany);
static_assert(findVPIRelation(vpiInTerm)->cardinality ==
              VPIRelationCardinality::One);
static_assert(findVPIRelation(vpiOutTerm)->cardinality ==
              VPIRelationCardinality::One);
static_assert(findVPIRelation(vpiTaskFunc)->cardinality ==
              VPIRelationCardinality::OneOrMany);
static_assert(findVPIRelation(vpiInterfaceDecl) == nullptr);

static_assert(static_cast<uint8_t>(VPIObjectFamily::Scope) == 0);
static_assert(static_cast<uint8_t>(VPIObjectFamily::Other) == 14);
static_assert(static_cast<uint8_t>(VPIObjectRole::Concrete) == 0);
static_assert(static_cast<uint8_t>(VPIObjectRole::Alias) == 4);

static_assert(hasVPITraversal(0, vpiModule, VPITraversalMode::Iterate));
static_assert(vpiObjectSetContains(
    findVPITraversal(0, vpiModule, VPITraversalMode::Iterate)->targets,
    vpiModule));
static_assert(!vpiObjectSetContains(
    findVPITraversal(0, vpiModule, VPITraversalMode::Iterate)->targets,
    vpiReg));

static_assert(hasVPITraversal(vpiModule, vpiPort, VPITraversalMode::Iterate));
static_assert(findVPITraversal(vpiModule, vpiPort, VPITraversalMode::Iterate)
                  ->order == VPITraversalOrder::None);
static_assert(vpiObjectSetContains(
    findVPITraversal(vpiModule, vpiPort, VPITraversalMode::Iterate)->targets,
    vpiPort));
static_assert(!vpiObjectSetContains(
    findVPITraversal(vpiModule, vpiPort, VPITraversalMode::Iterate)->targets,
    vpiPortBit));
static_assert(findVPITraversal(vpiModule, vpiPort, VPITraversalMode::Handle) ==
              nullptr);

static_assert(findVPITraversal(0, vpiTimeQueue, VPITraversalMode::Iterate)
                  ->order == VPITraversalOrder::Time);
static_assert(findVPITraversal(vpiThread, vpiModule,
                               VPITraversalMode::Handle) == nullptr);
static_assert(vpiObjectSetContains(
    findVPITraversal(vpiPropertySpec, vpiPropertyExpr, VPITraversalMode::Handle)
        ->targets,
    vpiDistribution));
static_assert(hasVPITraversal(vpiFor, vpiForInitStmt,
                              VPITraversalMode::Handle));
static_assert(hasVPITraversal(vpiFor, vpiForInitStmt,
                              VPITraversalMode::Iterate));
static_assert(isVPIStatementContainment(vpiFor, vpiForInitStmt,
                                        VPITraversalMode::Handle));
static_assert(isVPIStatementContainment(vpiFor, vpiForInitStmt,
                                        VPITraversalMode::Iterate));
static_assert(isVPIStatementContainment(vpiBegin, vpiStmt,
                                        VPITraversalMode::Iterate));
static_assert(!isVPIStatementContainment(vpiFrame, vpiStmt,
                                         VPITraversalMode::Handle));
static_assert(!isVPIStatementContainment(vpiNet, vpiContAssign,
                                         VPITraversalMode::Iterate));
static_assert(findVPITraversal(vpiOrderedWait, vpiCondition,
                               VPITraversalMode::Handle) == nullptr);
static_assert(hasVPITraversal(vpiOrderedWait, vpiCondition,
                              VPITraversalMode::Iterate));
static_assert(vpiObjectSetContains(
    findVPITraversal(vpiForeachStmt, vpiVariables, VPITraversalMode::Iterate)
        ->targets,
    vpiStringVar));
static_assert(!vpiObjectSetContains(
    findVPITraversal(vpiForeachStmt, vpiVariables, VPITraversalMode::Iterate)
        ->targets,
    vpiIntVar));
static_assert(vpiObjectSetContains(findVPITraversal(vpiAliasStmt, vpiInstance,
                                                    VPITraversalMode::Handle)
                                       ->targets,
                                   vpiModule));
static_assert(vpiObjectSetContains(
    findVPITraversal(vpiIterator, vpiUse, VPITraversalMode::Handle)->targets,
    vpiPropertyDecl));
static_assert(vpiObjectSetContains(
    findVPITraversal(vpiIterator, vpiUse, VPITraversalMode::Handle)->targets,
    vpiConstraint));
static_assert(vpiObjectSetContains(findVPITraversal(vpiGenScope, vpiAssertion,
                                                    VPITraversalMode::Iterate)
                                       ->targets,
                                   vpiAssert));
static_assert(!vpiObjectSetContains(findVPITraversal(vpiGenScope, vpiAssertion,
                                                     VPITraversalMode::Iterate)
                                        ->targets,
                                    vpiPropertyDecl));

static_assert(hasVPITraversal(vpiIODecl, vpiTaskFunc,
                              VPITraversalMode::Handle));
static_assert(hasVPITraversal(vpiModule, vpiTaskFunc,
                              VPITraversalMode::Iterate));
static_assert(vpiObjectSetContains(findVPITraversal(vpiVirtualInterfaceVar,
                                                    vpiTypespec,
                                                    VPITraversalMode::Handle)
                                       ->targets,
                                   vpiInterfaceTypespec));
static_assert(!vpiObjectSetContains(findVPITraversal(vpiVirtualInterfaceVar,
                                                     vpiTypespec,
                                                     VPITraversalMode::Handle)
                                        ->targets,
                                    vpiIntTypespec));
static_assert(vpiObjectSetContains(
    findVPITraversal(vpiVirtualInterfaceVar, vpiExpr, VPITraversalMode::Handle)
        ->targets,
    vpiModport));
static_assert(hasVPITraversal(vpiMethodFuncCall, vpiPrefix,
                              VPITraversalMode::Handle));
static_assert(hasVPITraversal(vpiTaskCall, vpiScope, VPITraversalMode::Handle));
static_assert(findVPITraversal(vpiPrimitiveArray, vpiIndex,
                               VPITraversalMode::Handle) == nullptr);
static_assert(findVPITraversal(vpiGate, vpiUdpDefn, VPITraversalMode::Handle) ==
              nullptr);
static_assert(hasVPITraversal(vpiUdp, vpiUdpDefn, VPITraversalMode::Handle));
static_assert(findVPITraversal(vpiPortBit, vpiBit, VPITraversalMode::Iterate) ==
              nullptr);
static_assert(hasVPITraversal(vpiRegArray, vpiVarSelect,
                              VPITraversalMode::Iterate));
static_assert(findVPITraversal(vpiPackedArrayVar, vpiVarSelect,
                               VPITraversalMode::Iterate) == nullptr);
static_assert(vpiObjectSetContains(findVPITraversal(vpiNamedEvent, vpiParent,
                                                    VPITraversalMode::Handle)
                                       ->targets,
                                   vpiNamedEventArray));
static_assert(!vpiObjectSetContains(findVPITraversal(vpiNamedEvent, vpiParent,
                                                     VPITraversalMode::Handle)
                                        ->targets,
                                    vpiNamedEvent));
static_assert(vpiObjectSetContains(
    findVPITraversal(0, vpiSysTfCall, VPITraversalMode::Handle)->targets,
    vpiSysFuncCall));
static_assert(!vpiObjectSetContains(
    findVPITraversal(0, vpiSysTfCall, VPITraversalMode::Handle)->targets,
    vpiFuncCall));
static_assert(findVPITraversal(vpiRestrict, vpiStmt,
                               VPITraversalMode::Handle) == nullptr);
static_assert(findVPITraversal(vpiCover, vpiElseStmt,
                               VPITraversalMode::Handle) == nullptr);
static_assert(hasVPITraversal(vpiParameter, vpiScope,
                              VPITraversalMode::Handle));
static_assert(vpiObjectSetContains(findVPITraversal(vpiConstrIfElse,
                                                    vpiConstraintExpr,
                                                    VPITraversalMode::Iterate)
                                       ->targets,
                                   vpiConstant));
static_assert(!vpiObjectSetContains(findVPITraversal(vpiConstrIfElse,
                                                     vpiConstraintExpr,
                                                     VPITraversalMode::Iterate)
                                        ->targets,
                                    vpiConstraintOrdering));

// High-risk exactness checks for the generated LRM traversal graph.  The
// exhaustive behavior checks live in VPIObjectModelTest; these assertions keep
// accidental ABI/header drift visible in builds that disable tests.
static_assert(hasVPITraversal(vpiInterfaceArray, vpiInterface,
                              VPITraversalMode::Iterate));
static_assert(findVPITraversal(vpiModuleArray, vpiInterface,
                               VPITraversalMode::Iterate) == nullptr);
static_assert(vpiObjectSetContains(
    findVPITraversal(vpiGateArray, vpiExpr, VPITraversalMode::Handle)->targets,
    vpiOperation));
static_assert(!vpiObjectSetContains(
    findVPITraversal(vpiGateArray, vpiExpr, VPITraversalMode::Handle)->targets,
    vpiConstant));
static_assert(findVPITraversal(vpiGateArray, vpiInstance,
                               VPITraversalMode::Iterate) == nullptr);
static_assert(vpiObjectSetContains(findVPITraversal(vpiModuleArray, vpiInstance,
                                                    VPITraversalMode::Iterate)
                                       ->targets,
                                   vpiModule));
static_assert(!vpiObjectSetContains(
    findVPITraversal(vpiModuleArray, vpiInstance, VPITraversalMode::Iterate)
        ->targets,
    vpiInterface));
static_assert(findVPITraversal(vpiGateArray, vpiParamAssign,
                               VPITraversalMode::Iterate) == nullptr);
static_assert(findVPITraversal(vpiPortBit, vpiInstance,
                               VPITraversalMode::Handle) == nullptr);
static_assert(findVPITraversal(vpiPortBit, vpiHighConn,
                               VPITraversalMode::Handle) == nullptr);
static_assert(findVPITraversal(vpiPortBit, vpiLowConn,
                               VPITraversalMode::Handle) == nullptr);
static_assert(vpiObjectSetContains(
    findVPITraversal(vpiPort, vpiLowConn, VPITraversalMode::Handle)->targets,
    vpiOperation));
static_assert(hasVPITraversal(vpiIntVar, vpiModule, VPITraversalMode::Handle));
static_assert(hasVPITraversal(vpiIntVar, vpiInstance,
                              VPITraversalMode::Handle));
static_assert(hasVPITraversal(vpiNamedEvent, vpiModule,
                              VPITraversalMode::Handle));
static_assert(hasVPITraversal(vpiNamedEventArray, vpiInstance,
                              VPITraversalMode::Handle));
static_assert(findVPITraversal(vpiNamedEventArray, vpiScope,
                               VPITraversalMode::Handle) == nullptr);
static_assert(!vpiObjectSetContains(findVPITraversal(vpiDelayTerm, vpiDriver,
                                                     VPITraversalMode::Iterate)
                                        ->targets,
                                    vpiAssignStmt));
static_assert(hasVPITraversal(vpiAssert, vpiElseStmt,
                              VPITraversalMode::Handle));
static_assert(findVPITraversal(vpiAssume, vpiElseStmt,
                               VPITraversalMode::Handle) == nullptr);
static_assert(findVPITraversal(vpiAssert, vpiDisableCondition,
                               VPITraversalMode::Handle) == nullptr);
static_assert(!vpiObjectSetContains(
    findVPITraversal(vpiFrame, vpiStmt, VPITraversalMode::Handle)->targets,
    vpiCaseItem));
static_assert(!vpiObjectSetContains(
    findVPITraversal(vpiThread, vpiOrigin, VPITraversalMode::Handle)->targets,
    vpiContAssign));
static_assert(vpiObjectSetContains(findVPITraversal(vpiForeachStmt, vpiLoopVars,
                                                    VPITraversalMode::Iterate)
                                       ->targets,
                                   vpiStringVar));
static_assert(vpiObjectSetContains(findVPITraversal(vpiForeachStmt, vpiLoopVars,
                                                    VPITraversalMode::Iterate)
                                       ->targets,
                                   vpiClassVar));
static_assert(!vpiObjectSetContains(
    findVPITraversal(vpiForeachStmt, vpiLoopVars, VPITraversalMode::Iterate)
        ->targets,
    vpiRealVar));
static_assert(vpiObjectSetContains(
    findVPITraversal(vpiMethodFuncCall, vpiPrefix, VPITraversalMode::Handle)
        ->targets,
    vpiClockingBlock));
static_assert(vpiObjectSetContains(
    findVPITraversal(vpiMethodFuncCall, vpiPrefix, VPITraversalMode::Handle)
        ->targets,
    vpiVirtualInterfaceVar));
static_assert(!vpiObjectSetContains(
    findVPITraversal(vpiMethodFuncCall, vpiPrefix, VPITraversalMode::Handle)
        ->targets,
    vpiNamedEvent));
static_assert(vpiObjectSetContains(
    findVPITraversal(0, vpiActiveTimeFormat, VPITraversalMode::Handle)->targets,
    vpiSysTaskCall));
static_assert(!vpiObjectSetContains(
    findVPITraversal(0, vpiActiveTimeFormat, VPITraversalMode::Handle)->targets,
    vpiSysFuncCall));
static_assert(vpiObjectSetContains(findVPITraversal(vpiPropFormalDecl, vpiExpr,
                                                    VPITraversalMode::Handle)
                                       ->targets,
                                   vpiNamedEvent));
static_assert(!vpiObjectSetContains(findVPITraversal(vpiPropFormalDecl, vpiExpr,
                                                     VPITraversalMode::Handle)
                                        ->targets,
                                    vpiNamedEventArray));
static_assert(!vpiObjectSetContains(findVPITraversal(vpiPropertyInst,
                                                     vpiDisableCondition,
                                                     VPITraversalMode::Handle)
                                        ->targets,
                                    vpiDistribution));
static_assert(vpiObjectSetContains(findVPITraversal(vpiPropertySpec,
                                                    vpiDisableCondition,
                                                    VPITraversalMode::Handle)
                                       ->targets,
                                   vpiDistribution));
static_assert(findVPITraversal(vpiNamedEvent, vpiUse,
                               VPITraversalMode::Iterate) == nullptr);
static_assert(vpiObjectSetContains(
    findVPITraversal(vpiAssignment, vpiLhs, VPITraversalMode::Handle)->targets,
    vpiVirtualInterfaceVar));
static_assert(!vpiObjectSetContains(
    findVPITraversal(vpiAssignment, vpiLhs, VPITraversalMode::Handle)->targets,
    vpiNamedEvent));
static_assert(vpiObjectSetContains(
    findVPITraversal(vpiEventControl, vpiCondition, VPITraversalMode::Handle)
        ->targets,
    vpiNamedEvent));
static_assert(!vpiObjectSetContains(
    findVPITraversal(vpiEventControl, vpiCondition, VPITraversalMode::Handle)
        ->targets,
    vpiNamedEventArray));
static_assert(vpiObjectSetContains(findVPITraversal(vpiModule, vpiParameter,
                                                    VPITraversalMode::Iterate)
                                       ->targets,
                                   vpiTypeParameter));
static_assert(hasVPITraversal(vpiTypeParameter, vpiScope,
                              VPITraversalMode::Handle));
static_assert(vpiObjectSetContains(
    findVPITraversal(vpiTaskCall, vpiScope, VPITraversalMode::Handle)->targets,
    vpiClassObj));
static_assert(vpiObjectSetContains(
    findVPITraversal(vpiFrame, vpiOrigin, VPITraversalMode::Handle)->targets,
    vpiClassObj));
static_assert(vpiObjectSetContains(
    findVPITraversal(vpiRefObj, vpiActual, VPITraversalMode::Handle)->targets,
    vpiNetArray));
static_assert(findVPITraversal(vpiNetBit, vpiBit, VPITraversalMode::Iterate) ==
              nullptr);
static_assert(findVPITraversal(vpiNetArray, vpiBit,
                               VPITraversalMode::Iterate) == nullptr);
static_assert(vpiObjectSetContains(findVPITraversal(vpiConstraint,
                                                    vpiConstraintItem,
                                                    VPITraversalMode::Iterate)
                                       ->targets,
                                   vpiConstant));
static_assert(!vpiObjectSetContains(findVPITraversal(vpiConstraint,
                                                     vpiConstraintItem,
                                                     VPITraversalMode::Iterate)
                                        ->targets,
                                    vpiNamedEvent));
static_assert(vpiObjectSetContains(findVPITraversal(vpiModPath, vpiInstance,
                                                    VPITraversalMode::Handle)
                                       ->targets,
                                   vpiInterface));
static_assert(!vpiObjectSetContains(findVPITraversal(vpiModPath, vpiInstance,
                                                     VPITraversalMode::Handle)
                                        ->targets,
                                    vpiProgram));
static_assert(findVPITraversal(vpiClassDefn, vpiDerivedClasses,
                               VPITraversalMode::Iterate)
                  ->order == VPITraversalOrder::None);
static_assert(vpiObjectSetContains(
    findVPITraversal(vpiModule, vpiIndex, VPITraversalMode::Handle)->targets,
    vpiNet));
static_assert(vpiObjectSetContains(
    findVPITraversal(vpiModule, vpiIndex, VPITraversalMode::Handle)->targets,
    vpiIntVar));
static_assert(!vpiObjectSetContains(
    findVPITraversal(vpiModule, vpiIndex, VPITraversalMode::Handle)->targets,
    vpiPropertyExpr));
static_assert(vpiObjectSetContains(findVPITraversal(vpiTaskCall, vpiArgument,
                                                    VPITraversalMode::Iterate)
                                       ->targets,
                                   vpiClassObj));
static_assert(vpiObjectSetContains(findVPITraversal(vpiTaskCall, vpiArgument,
                                                    VPITraversalMode::Iterate)
                                       ->targets,
                                   vpiGate));
static_assert(vpiObjectSetContains(findVPITraversal(vpiTaskCall, vpiArgument,
                                                    VPITraversalMode::Iterate)
                                       ->targets,
                                   vpiNamedEvent));
static_assert(!vpiObjectSetContains(findVPITraversal(vpiTaskCall, vpiArgument,
                                                     VPITraversalMode::Iterate)
                                        ->targets,
                                    vpiPrimTerm));
static_assert(!vpiObjectSetContains(findVPITraversal(vpiTaskCall, vpiArgument,
                                                     VPITraversalMode::Iterate)
                                        ->targets,
                                    vpiGateArray));
static_assert(vpiObjectSetContains(findVPITraversal(vpiFrame, vpiAutomatics,
                                                    VPITraversalMode::Iterate)
                                       ->targets,
                                   vpiVirtualInterfaceVar));
static_assert(vpiObjectSetContains(
    findVPITraversal(vpiConstrForEach, vpiLoopVars, VPITraversalMode::Iterate)
        ->targets,
    vpiStringVar));
static_assert(!vpiObjectSetContains(
    findVPITraversal(vpiConstrForEach, vpiLoopVars, VPITraversalMode::Iterate)
        ->targets,
    vpiRealVar));
static_assert(vpiObjectSetContains(
    findVPITraversal(vpiParamAssign, vpiRhs, VPITraversalMode::Handle)->targets,
    vpiNet));
static_assert(vpiObjectSetContains(
    findVPITraversal(vpiParamAssign, vpiRhs, VPITraversalMode::Handle)->targets,
    vpiIntTypespec));
static_assert(!vpiObjectSetContains(
    findVPITraversal(vpiParamAssign, vpiRhs, VPITraversalMode::Handle)->targets,
    vpiPropertyExpr));
static_assert(vpiObjectSetContains(findVPITraversal(vpiDistItem, vpiValueRange,
                                                    VPITraversalMode::Handle)
                                       ->targets,
                                   vpiRange));
static_assert(!vpiObjectSetContains(findVPITraversal(vpiDistItem, vpiValueRange,
                                                     VPITraversalMode::Handle)
                                        ->targets,
                                    vpiPropertyExpr));
static_assert(vpiObjectSetContains(
    findVPITraversal(vpiTchk, vpiExpr, VPITraversalMode::Iterate)->targets,
    vpiTchkTerm));
static_assert(!vpiObjectSetContains(
    findVPITraversal(vpiTchk, vpiExpr, VPITraversalMode::Iterate)->targets,
    vpiPropertyExpr));
static_assert(vpiObjectSetContains(findVPITraversal(vpiMethodFuncCall, vpiWith,
                                                    VPITraversalMode::Handle)
                                       ->targets,
                                   vpiConstraint));
static_assert(!vpiObjectSetContains(findVPITraversal(vpiMethodFuncCall, vpiWith,
                                                     VPITraversalMode::Handle)
                                        ->targets,
                                    vpiPropertyExpr));
static_assert(vpiObjectSetContains(findVPITraversal(vpiClockingIODecl, vpiExpr,
                                                    VPITraversalMode::Handle)
                                       ->targets,
                                   vpiNet));
static_assert(!vpiObjectSetContains(findVPITraversal(vpiClockingIODecl, vpiExpr,
                                                     VPITraversalMode::Handle)
                                        ->targets,
                                    vpiPropertyExpr));
static_assert(findVPITraversal(vpiClassDefn, vpiMethods,
                               VPITraversalMode::Iterate)
                  ->order == VPITraversalOrder::None);
static_assert(findVPITraversal(vpiClockingBlock, vpiClockingIODecl,
                               VPITraversalMode::Iterate)
                  ->order == VPITraversalOrder::None);
