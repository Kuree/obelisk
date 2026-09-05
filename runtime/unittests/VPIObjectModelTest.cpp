//===- VPIObjectModelTest.cpp - IEEE VPI traversal model tests -----------===//

#include "obelisk/Reflection/VPIObjectModel.h"
#include "../lib/VPIInternal.h"

#include "gtest/gtest.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace {

using namespace obelisk::reflection;

using Mode = VPITraversalMode;
using Order = VPITraversalOrder;
using PropertyKind = VPIPropertyValueKind;
using PropertyStability = VPIPropertyStability;
using ProtectedAccess = VPIPropertyProtectedAccess;
using ValueDefault = VPIValueDefaultFormat;
using ValueRead = VPIValueReadSemantics;
using ArrayValueFormat = VPIArrayValueFormat;
using IndexedKind = VPIIndexedAccessKind;
using KindSet = std::set<uint32_t>;

TEST(VPIObjectModel, ClassDefinitionValueOriginStopsAtGraphBoundaries) {
  using obelisk::runtime::hasClassDefinitionValueOrigin;
  EXPECT_TRUE(hasClassDefinitionValueOrigin(vpiClassDefn, false, vpiReg));
  EXPECT_TRUE(
      hasClassDefinitionValueOrigin(vpiClassDefn, false, vpiNamedEvent));
  EXPECT_TRUE(hasClassDefinitionValueOrigin(vpiReg, true, vpiRegBit));

  EXPECT_FALSE(
      hasClassDefinitionValueOrigin(vpiClassDefn, false, vpiClassTypespec));
  EXPECT_FALSE(hasClassDefinitionValueOrigin(vpiClassDefn, false, vpiModule));
  EXPECT_FALSE(hasClassDefinitionValueOrigin(vpiClassDefn, false, vpiFunction));
  EXPECT_FALSE(hasClassDefinitionValueOrigin(vpiReg, true, vpiTypespec));

  // A class specialization has a distinct LRM rule: only its non-static
  // members are restricted. Class-definition provenance must not escape into
  // the typespec and turn that conditional rule into an unconditional one.
  EXPECT_FALSE(hasClassDefinitionValueOrigin(vpiClassTypespec, false, vpiReg));
  EXPECT_FALSE(hasClassDefinitionValueOrigin(vpiModule, false, vpiReg));
}

constexpr size_t kExpectedTraversalCount = 1884;
static_assert(sizeof(vpiTraversals) / sizeof(vpiTraversals[0]) ==
              kExpectedTraversalCount);
constexpr size_t kExpectedPropertyCount = 2337;
static_assert(sizeof(vpiProperties) / sizeof(vpiProperties[0]) ==
              kExpectedPropertyCount);
constexpr size_t kExpectedValuePolicyCount = 56;
static_assert(sizeof(vpiValuePolicies) / sizeof(vpiValuePolicies[0]) ==
              kExpectedValuePolicyCount);
constexpr size_t kExpectedArrayValuePolicyCount = 9;
static_assert(sizeof(vpiArrayValuePolicies) /
                  sizeof(vpiArrayValuePolicies[0]) ==
              kExpectedArrayValuePolicyCount);
constexpr size_t kExpectedArrayValueElementTypeCount = 89;
static_assert(sizeof(vpiArrayValueElementTypes) /
                  sizeof(vpiArrayValueElementTypes[0]) ==
              kExpectedArrayValueElementTypeCount);
constexpr size_t kExpectedIndexedAccessCount = 45;
static_assert(sizeof(vpiIndexedAccesses) / sizeof(vpiIndexedAccesses[0]) ==
              kExpectedIndexedAccessCount);
constexpr size_t kExpectedIndexedTypeResultCount = 34;
static_assert(sizeof(vpiIndexedTypeResults) /
                  sizeof(vpiIndexedTypeResults[0]) ==
              kExpectedIndexedTypeResultCount);

struct OracleKey {
  uint32_t source;
  uint32_t selector;
  Mode mode;

  bool operator<(const OracleKey &other) const {
    return std::tie(source, selector, mode) <
           std::tie(other.source, other.selector, other.mode);
  }
};

struct OracleEdge {
  KindSet targets;
  Order order;
  bool statementContainment;
  const char *clause;
};

using OracleGraph = std::map<OracleKey, OracleEdge>;

std::string objectName(uint32_t value) {
  if (value == 0)
    return "NULL";
  if (const auto *object = findVPIObjectSelector(value))
    return object->apiName;
  if (const auto *relation = findVPIRelation(value))
    return relation->apiName;
  return std::to_string(value);
}

std::string keyName(uint32_t source, uint32_t selector, Mode mode) {
  std::ostringstream stream;
  stream << objectName(source) << " --" << objectName(selector) << " ("
         << (mode == Mode::Handle ? "handle" : "iterate") << ")";
  return stream.str();
}

const VPITraversalDescriptor &requireTraversal(uint32_t source,
                                               uint32_t selector, Mode mode) {
  const auto *edge = findVPITraversal(source, selector, mode);
  EXPECT_NE(edge, nullptr) << keyName(source, selector, mode);
  // Keep subsequent diagnostics useful after a missing-edge failure.
  static constexpr VPITraversalDescriptor missing{
      0,
      0,
      Mode::Handle,
      Order::None,
      static_cast<VPIObjectSetID>(0),
      false,
      VPIAutomaticRelation::None,
      "missing",
      "missing"};
  return edge ? *edge : missing;
}

std::set<uint32_t> expandedTargets(VPIObjectSetID targets) {
  std::set<uint32_t> result;
  for (const auto &object : vpiObjectKinds)
    if (object.aliasOf == nullptr && object.role == VPIObjectRole::Concrete &&
        vpiObjectSetContains(targets, object.value))
      result.insert(object.value);
  return result;
}

std::string setNames(const std::set<uint32_t> &values) {
  std::ostringstream stream;
  bool first = true;
  for (uint32_t value : values) {
    if (!first)
      stream << ", ";
    first = false;
    stream << objectName(value);
  }
  return stream.str();
}

void expectTargetsExactly(uint32_t source, uint32_t selector, Mode mode,
                          std::initializer_list<uint32_t> expected) {
  const auto &edge = requireTraversal(source, selector, mode);
  std::set<uint32_t> expectedSet(expected);
  std::set<uint32_t> actualSet = expandedTargets(edge.targets);
  EXPECT_EQ(actualSet, expectedSet) << keyName(source, selector, mode)
                                    << "\nexpected: " << setNames(expectedSet)
                                    << "\nactual:   " << setNames(actualSet);
}

void expectContains(uint32_t source, uint32_t selector, Mode mode,
                    std::initializer_list<uint32_t> expected,
                    std::initializer_list<uint32_t> forbidden = {}) {
  const auto &edge = requireTraversal(source, selector, mode);
  for (uint32_t target : expected)
    EXPECT_TRUE(vpiObjectSetContains(edge.targets, target))
        << keyName(source, selector, mode) << " must contain "
        << objectName(target);
  for (uint32_t target : forbidden)
    EXPECT_FALSE(vpiObjectSetContains(edge.targets, target))
        << keyName(source, selector, mode) << " must not contain "
        << objectName(target);
}

void expectAbsent(uint32_t source, uint32_t selector, Mode mode) {
  EXPECT_EQ(findVPITraversal(source, selector, mode), nullptr)
      << keyName(source, selector, mode);
}

KindSet
makePropertyObjectSet(std::initializer_list<uint32_t> objects = {},
                      std::initializer_list<VPIObjectFamily> families = {},
                      std::initializer_list<uint32_t> exclusions = {},
                      bool includeRoot = false) {
  KindSet result(objects.begin(), objects.end());
  uint64_t familyMask = 0;
  for (VPIObjectFamily family : families)
    familyMask |= vpiFamilyMask(family);
  for (const auto &object : vpiObjectKinds)
    if (object.aliasOf == nullptr && object.role == VPIObjectRole::Concrete &&
        (object.families & familyMask) != 0)
      result.insert(object.value);
  for (uint32_t exclusion : exclusions)
    result.erase(exclusion);
  if (includeRoot)
    result.insert(0);
  return result;
}

std::map<uint32_t, KindSet> buildReadPropertyApplicabilityOracle() {
  std::map<uint32_t, KindSet> oracle;
  auto add = [&](const KindSet &objects,
                 std::initializer_list<uint32_t> properties) {
    for (uint32_t property : properties)
      EXPECT_TRUE(oracle.emplace(property, objects).second)
          << "duplicate property applicability oracle for " << property;
  };
  auto merge = [](KindSet left, const KindSet &right) {
    left.insert(right.begin(), right.end());
    return left;
  };

  const auto allConcrete = makePropertyObjectSet(
      {},
      {VPIObjectFamily::Scope, VPIObjectFamily::Declaration,
       VPIObjectFamily::Process, VPIObjectFamily::Statement,
       VPIObjectFamily::Expression, VPIObjectFamily::Variable,
       VPIObjectFamily::Net, VPIObjectFamily::Array, VPIObjectFamily::Typespec,
       VPIObjectFamily::Primitive, VPIObjectFamily::Timing,
       VPIObjectFamily::Assertion, VPIObjectFamily::Transient,
       VPIObjectFamily::Runtime, VPIObjectFamily::Other});
  add(allConcrete, {vpiType, vpiIsProtected, vpiAllocScheme});
  auto sourceLocated = makePropertyObjectSet(
      {},
      {VPIObjectFamily::Scope, VPIObjectFamily::Declaration,
       VPIObjectFamily::Process, VPIObjectFamily::Statement,
       VPIObjectFamily::Expression, VPIObjectFamily::Variable,
       VPIObjectFamily::Net, VPIObjectFamily::Array, VPIObjectFamily::Typespec,
       VPIObjectFamily::Primitive, VPIObjectFamily::Timing,
       VPIObjectFamily::Assertion},
      {vpiDelayTerm, vpiDelayDevice, vpiInterModPath, vpiGenScopeArray,
       vpiGenScope, vpiThread});
  add(sourceLocated, {vpiFile, vpiLineNo});

  const KindSet instances{vpiPackage, vpiModule, vpiInterface, vpiProgram};
  auto instancesAndRoot = instances;
  instancesAndRoot.insert(0);
  add(instancesAndRoot, {vpiTimeUnit, vpiTimePrecision});
  add({vpiClassVar, vpiClassObj}, {vpiObjId});
  add({vpiIODecl, vpiPort, vpiPortBit, vpiPrimTerm, vpiPathTerm,
       vpiClockingIODecl, vpiPropFormalDecl, vpiSeqFormalDecl},
      {vpiDirection});

  const KindSet scalarVector{vpiIODecl,
                             vpiPort,
                             vpiPortBit,
                             vpiNet,
                             vpiNetBit,
                             vpiNetArray,
                             vpiEnumNet,
                             vpiIntegerNet,
                             vpiTimeNet,
                             vpiUnionNet,
                             vpiShortRealNet,
                             vpiRealNet,
                             vpiByteNet,
                             vpiShortIntNet,
                             vpiIntNet,
                             vpiLongIntNet,
                             vpiBitNet,
                             vpiInterconnectNet,
                             vpiInterconnectArray,
                             vpiStructNet,
                             vpiPackedArrayNet,
                             vpiShortRealVar,
                             vpiRealVar,
                             vpiByteVar,
                             vpiShortIntVar,
                             vpiIntVar,
                             vpiLongIntVar,
                             vpiIntegerVar,
                             vpiTimeVar,
                             vpiRegArray,
                             vpiPackedArrayVar,
                             vpiBitVar,
                             vpiReg,
                             vpiStructVar,
                             vpiUnionVar,
                             vpiEnumVar,
                             vpiStringVar,
                             vpiChandleVar,
                             vpiClassVar,
                             vpiVirtualInterfaceVar,
                             vpiRegBit};
  add(scalarVector, {vpiScalar});
  auto vectorObjects = scalarVector;
  vectorObjects.insert(
      {vpiBitTypespec, vpiLogicTypespec, vpiPackedArrayTypespec});
  add(vectorObjects, {vpiVector});
  const KindSet ports{vpiPort, vpiPortBit};
  add(ports, {vpiExplicitName, vpiPortIndex, vpiPortType});

  const KindSet structuralSize{vpiIODecl,
                               vpiPort,
                               vpiPortBit,
                               vpiModuleArray,
                               vpiInterfaceArray,
                               vpiProgramArray,
                               vpiGateArray,
                               vpiSwitchArray,
                               vpiUdpArray,
                               vpiNet,
                               vpiNetBit,
                               vpiNetArray,
                               vpiEnumNet,
                               vpiIntegerNet,
                               vpiTimeNet,
                               vpiUnionNet,
                               vpiShortRealNet,
                               vpiRealNet,
                               vpiByteNet,
                               vpiShortIntNet,
                               vpiIntNet,
                               vpiLongIntNet,
                               vpiBitNet,
                               vpiInterconnectNet,
                               vpiInterconnectArray,
                               vpiStructNet,
                               vpiPackedArrayNet,
                               vpiIntegerVar,
                               vpiRealVar,
                               vpiReg,
                               vpiRegBit,
                               vpiTimeVar,
                               vpiLongIntVar,
                               vpiShortIntVar,
                               vpiIntVar,
                               vpiShortRealVar,
                               vpiByteVar,
                               vpiClassVar,
                               vpiStringVar,
                               vpiEnumVar,
                               vpiStructVar,
                               vpiUnionVar,
                               vpiBitVar,
                               vpiChandleVar,
                               vpiPackedArrayVar,
                               vpiVirtualInterfaceVar,
                               vpiRegArray,
                               vpiParameter,
                               vpiSpecParam,
                               vpiRefObj,
                               vpiVarSelect,
                               vpiBitSelect,
                               vpiIndexedPartSelect,
                               vpiPartSelect,
                               vpiOperation,
                               vpiConstant,
                               vpiFuncCall,
                               vpiMethodFuncCall,
                               vpiSysFuncCall,
                               vpiLetExpr,
                               vpiRange,
                               vpiGate,
                               vpiSwitch,
                               vpiUdp,
                               vpiUdpDefn,
                               vpiTableEntry,
                               vpiFunction,
                               vpiGenScopeArray};
  add(structuralSize, {vpiSize});
  const KindSet ordinaryVariables{
      vpiShortRealVar,        vpiRealVar,    vpiByteVar,
      vpiShortIntVar,         vpiIntVar,     vpiLongIntVar,
      vpiIntegerVar,          vpiTimeVar,    vpiRegArray,
      vpiPackedArrayVar,      vpiBitVar,     vpiReg,
      vpiStructVar,           vpiUnionVar,   vpiEnumVar,
      vpiStringVar,           vpiChandleVar, vpiClassVar,
      vpiVirtualInterfaceVar, vpiRegBit};
  KindSet ordinaryExpressions = ordinaryVariables;
  ordinaryExpressions.insert(
      {vpiParameter, vpiSpecParam, vpiRefObj, vpiVarSelect, vpiBitSelect,
       vpiPartSelect, vpiIndexedPartSelect, vpiOperation, vpiConstant,
       vpiFuncCall, vpiMethodFuncCall, vpiSysFuncCall, vpiLetExpr});
  ordinaryExpressions =
      merge(std::move(ordinaryExpressions),
            makePropertyObjectSet({}, {VPIObjectFamily::Net}));
  add({vpiPackedArrayVar, vpiStructVar, vpiUnionVar, vpiEnumVar,
       vpiStructTypespec, vpiUnionTypespec},
      {vpiPacked});
  add({vpiUnionTypespec}, {vpiTagged});
  add({vpiRegArray, vpiArrayTypespec}, {vpiArrayType});
  add(merge(ordinaryVariables, KindSet{vpiTypespecMember}), {vpiRandType});
  add({vpiConstant, vpiParameter}, {vpiConstType});

  KindSet indexedValues = ordinaryVariables;
  indexedValues.insert({vpiNet, vpiNetBit, vpiNetArray, vpiEnumNet,
                        vpiIntegerNet, vpiTimeNet, vpiUnionNet, vpiShortRealNet,
                        vpiRealNet, vpiByteNet, vpiShortIntNet, vpiIntNet,
                        vpiLongIntNet, vpiBitNet, vpiInterconnectNet,
                        vpiInterconnectArray, vpiStructNet, vpiPackedArrayNet});
  auto arrayMembers = indexedValues;
  arrayMembers.insert({vpiPackage, vpiModule, vpiInterface, vpiProgram, vpiGate,
                       vpiSwitch, vpiUdp, vpiNamedEvent, vpiGenScope});
  add(arrayMembers, {vpiArrayMember, vpiArray});
  add({vpiEnumNet, vpiStructNet, vpiPackedArrayNet, vpiStructVar, vpiUnionVar,
       vpiEnumVar, vpiPackedArrayVar},
      {vpiPackedArrayMember});
  KindSet constantSelect = ordinaryVariables;
  constantSelect.insert({vpiRefObj, vpiParameter, vpiSpecParam, vpiVarSelect,
                         vpiBitSelect, vpiPartSelect, vpiIndexedPartSelect});
  constantSelect = merge(std::move(constantSelect),
                         makePropertyObjectSet({}, {VPIObjectFamily::Net}));
  add(constantSelect, {vpiConstantSelect});
  KindSet signedObjects = ordinaryExpressions;
  signedObjects.insert({vpiIODecl, vpiFunction});
  add(signedObjects, {vpiSigned});

  add(makePropertyObjectSet({vpiPackage,
                             vpiModule,
                             vpiInterface,
                             vpiProgram,
                             vpiModuleArray,
                             vpiInterfaceArray,
                             vpiProgramArray,
                             vpiGateArray,
                             vpiSwitchArray,
                             vpiUdpArray,
                             vpiFunction,
                             vpiTask,
                             vpiGenScope,
                             vpiClockingBlock,
                             vpiClassDefn,
                             vpiModport,
                             vpiIODecl,
                             vpiPort,
                             vpiRefObj,
                             vpiVarSelect,
                             vpiBitSelect,
                             vpiGate,
                             vpiSwitch,
                             vpiUdp,
                             vpiConstraint,
                             vpiClockingIODecl,
                             vpiPropFormalDecl,
                             vpiSeqFormalDecl,
                             vpiLetDecl,
                             vpiAnyPattern,
                             vpiTaggedPattern,
                             vpiStructPattern,
                             vpiAttribute,
                             vpiGenScopeArray,
                             vpiGenVar,
                             vpiSysFuncCall,
                             vpiAssert,
                             vpiAssume,
                             vpiCover,
                             vpiRestrict,
                             vpiPropertyDecl,
                             vpiPropertyInst,
                             vpiSequenceDecl,
                             vpiSequenceInst,
                             vpiImmediateAssert,
                             vpiImmediateAssume,
                             vpiImmediateCover,
                             vpiAssignStmt,
                             vpiAssignment,
                             vpiBegin,
                             vpiCase,
                             vpiDeassign,
                             vpiDelayControl,
                             vpiDisable,
                             vpiEventControl,
                             vpiEventStmt,
                             vpiFor,
                             vpiForce,
                             vpiForever,
                             vpiFork,
                             vpiIf,
                             vpiIfElse,
                             vpiNamedBegin,
                             vpiNamedFork,
                             vpiNullStmt,
                             vpiRelease,
                             vpiRepeat,
                             vpiRepeatControl,
                             vpiSysTaskCall,
                             vpiTaskCall,
                             vpiWait,
                             vpiWhile,
                             vpiMethodTaskCall,
                             vpiDoWhile,
                             vpiOrderedWait,
                             vpiWaitFork,
                             vpiDisableFork,
                             vpiExpectStmt,
                             vpiForeachStmt,
                             vpiReturnStmt,
                             vpiBreak,
                             vpiContinue,
                             vpiEnumConst},
                            {VPIObjectFamily::Variable, VPIObjectFamily::Net,
                             VPIObjectFamily::Typespec}),
      {vpiName});
  add(makePropertyObjectSet({vpiModuleArray,   vpiInterfaceArray,
                             vpiProgramArray,  vpiGateArray,
                             vpiSwitchArray,   vpiUdpArray,
                             vpiRefObj,        vpiVarSelect,
                             vpiBitSelect,     vpiConstraint,
                             vpiGenScopeArray, vpiGate,
                             vpiSwitch,        vpiUdp,
                             vpiGenVar,        vpiTypeParameter,
                             vpiAssert,        vpiAssume,
                             vpiCover,         vpiRestrict,
                             vpiPropertyDecl,  vpiSequenceDecl},
                            {VPIObjectFamily::Scope, VPIObjectFamily::Variable,
                             VPIObjectFamily::Net},
                            {vpiClassTypespec}),
      {vpiFullName});

  add({vpiModule}, {vpiTopModule, vpiCellInstance, vpiDefDecayTime});
  add({vpiPackage, vpiModule, vpiInterface, vpiProgram, vpiRefObj,
       vpiInterfaceTypespec, vpiGate, vpiSwitch, vpiUdp, vpiUdpDefn},
      {vpiDefName});
  add({vpiPackage, vpiModule, vpiInterface, vpiProgram, vpiUdpDefn,
       vpiGenScope},
      {vpiProtected});
  add(instances, {vpiDefNetType, vpiUnconnDrive, vpiDefDelayMode, vpiCell,
                  vpiConfig, vpiLibrary, vpiTop, vpiUnit});
  add({vpiPackage, vpiModule, vpiInterface, vpiProgram, vpiAttribute},
      {vpiDefFile, vpiDefLineNo});
  add({vpiPort, vpiPortBit, vpiParamAssign}, {vpiConnByName});
  const auto nets = makePropertyObjectSet({}, {VPIObjectFamily::Net});
  add(nets, {vpiNetType, vpiExplicitScalared, vpiExplicitVectored, vpiExpanded,
             vpiChargeStrength, vpiResolvedNetType});
  add(merge(nets, KindSet{vpiGenScope}), {vpiImplicitDecl});
  add({vpiPrimTerm}, {vpiTermIndex});
  add(merge(nets, KindSet{vpiGate, vpiSwitch, vpiUdp, vpiContAssign,
                          vpiContAssignBit}),
      {vpiStrength0, vpiStrength1});
  add({vpiGate, vpiSwitch, vpiUdp, vpiUdpDefn}, {vpiPrimType});
  add({vpiModPath},
      {vpiPolarity, vpiDataPolarity, vpiPathType, vpiModPathHasIfNone});
  add({vpiPathTerm, vpiTchkTerm}, {vpiEdge});
  add({vpiTchk}, {vpiTchkType});
  add({vpiOperation, vpiAssignment}, {vpiOpType});
  add({vpiEventStmt, vpiAssignment}, {vpiBlocking});
  add({vpiCase}, {vpiCaseType});
  add(merge(nets, KindSet{vpiContAssign, vpiContAssignBit}),
      {vpiNetDeclAssign});
  add({vpiFunction, vpiFuncCall, vpiSysFuncCall}, {vpiFuncType});
  add({vpiSysFuncCall, vpiSysTaskCall, vpiMethodFuncCall, vpiMethodTaskCall},
      {vpiUserDefn});
  add({vpiSchedEvent}, {vpiScheduled});
  add({vpiFrame, vpiThread}, {vpiActive});
  add(makePropertyObjectSet(
          {vpiPackage, vpiModule, vpiInterface, vpiProgram, vpiClassDefn,
           vpiClassTypespec, vpiConstraint, vpiTask, vpiFunction},
          {VPIObjectFamily::Variable}, {vpiParameter, vpiSpecParam}),
      {vpiAutomatic});
  add(merge(ordinaryExpressions,
            KindSet{vpiTaskCall, vpiSysTaskCall, vpiMethodTaskCall}),
      {vpiDecompile});
  add({vpiAttribute}, {vpiDefAttribute});
  add({vpiDelayTerm, vpiDelayDevice}, {vpiDelayType});
  add({vpiIterator}, {vpiIteratorType});
  add({vpiContAssign, vpiContAssignBit}, {vpiOffset});
  add({0}, {vpiSaveRestartID, vpiSaveRestartLocation, vpiCompatibilityMode});
  add(makePropertyObjectSet({vpiFrame}, {VPIObjectFamily::Variable}),
      {vpiValid});
  add({vpiParameter, vpiTypeParameter}, {vpiLocalParam});
  add({vpiIndexedPartSelect}, {vpiIndexedPartSelectType});
  add({vpiRegArray}, {vpiIsMemory});
  add({vpiFork, vpiNamedFork}, {vpiJoinType});
  add({vpiInterfaceTfDecl, vpiTask, vpiFunction, vpiConstraint},
      {vpiAccessType});
  add(ordinaryVariables, {vpiIsRandomized, vpiConstantVariable});
  add({vpiFor}, {vpiLocalVarDecls});
  add(merge(ordinaryVariables,
            makePropertyObjectSet({}, {VPIObjectFamily::Net})),
      {vpiStructUnionMember});
  add(merge(ordinaryVariables, KindSet{vpiTask, vpiFunction}), {vpiVisibility});
  add({vpiAlways}, {vpiAlwaysType});
  add({vpiDistItem}, {vpiDistType});
  add({vpiClassDefn, vpiConstraint, vpiTask, vpiFunction}, {vpiVirtual});
  KindSet dynamicPrefixes = ordinaryVariables;
  dynamicPrefixes.insert({vpiRefObj, vpiParameter, vpiSpecParam, vpiVarSelect,
                          vpiBitSelect, vpiPartSelect, vpiIndexedPartSelect,
                          vpiFuncCall, vpiTaskCall, vpiSysFuncCall,
                          vpiSysTaskCall, vpiMethodFuncCall, vpiMethodTaskCall,
                          vpiNamedEvent, vpiNamedEventArray});
  dynamicPrefixes = merge(std::move(dynamicPrefixes),
                          makePropertyObjectSet({}, {VPIObjectFamily::Net}));
  add(dynamicPrefixes, {vpiHasActual});
  add({vpiConstraint}, {vpiIsConstraintEnabled});
  add(makePropertyObjectSet({vpiImplication, vpiConstrIf, vpiConstrIfElse,
                             vpiConstrForEach, vpiDistribution, vpiSoftDisable,
                             vpiRefObj, vpiVarSelect, vpiBitSelect,
                             vpiIndexedPartSelect, vpiPartSelect, vpiOperation,
                             vpiConstant, vpiFuncCall, vpiMethodFuncCall,
                             vpiSysFuncCall, vpiLetExpr},
                            {VPIObjectFamily::Net, VPIObjectFamily::Variable},
                            {vpiParameter, vpiSpecParam, vpiNamedEvent,
                             vpiNamedEventArray, vpiVirtualInterfaceVar}),
      {vpiSoft});
  add({vpiClassTypespec}, {vpiClassType});
  add({vpiTask, vpiFunction},
      {vpiMethod, vpiDPIPure, vpiDPIContext, vpiDPICStr, vpiDPICIdentifier});
  add({vpiAssert, vpiAssume, vpiCover, vpiRestrict}, {vpiIsClockInferred});
  add({vpiIf, vpiIfElse, vpiCase}, {vpiQualifier});
  add({vpiClockingBlock, vpiClockingIODecl}, {vpiInputEdge, vpiOutputEdge});
  add({vpiRefObj}, {vpiGeneric});
  add({vpiOperation}, {vpiOpStrong});
  add({vpiImmediateAssert, vpiImmediateAssume, vpiImmediateCover},
      {vpiIsDeferred, vpiIsFinal});
  add({vpiCover}, {vpiIsCoverSequence});
  add({vpiSequenceInst, vpiAssert, vpiAssume, vpiCover, vpiRestrict,
       vpiPropertyInst, vpiImmediateAssert, vpiImmediateAssume,
       vpiImmediateCover},
      {vpiStartLine, vpiColumn, vpiEndLine, vpiEndColumn});
  add({vpiVirtualInterfaceVar, vpiInterfaceTypespec}, {vpiIsModPort});
  return oracle;
}

void expectOrdinaryExpressionTargets(uint32_t source, uint32_t selector,
                                     Mode mode) {
  expectContains(source, selector, mode,
                 {vpiNet, vpiReg, vpiConstant, vpiOperation, vpiRefObj},
                 {vpiAnyPattern, vpiTaggedPattern, vpiStructPattern,
                  vpiSequenceInst, vpiDistribution, vpiPropertyExpr,
                  vpiMulticlockSequenceExpr, vpiPropertyInst, vpiClockedProp,
                  vpiCaseProperty, vpiConstraint, vpiConstraintOrdering,
                  vpiImplication, vpiConstrIf, vpiConstrIfElse,
                  vpiConstrForEach});
}

void expectNoOrder(uint32_t source, uint32_t selector, Mode mode) {
  EXPECT_EQ(requireTraversal(source, selector, mode).order, Order::None)
      << keyName(source, selector, mode);
}

void refreshImageChecksum(std::vector<uint8_t> &image) {
  uint64_t checksum = checksumVPIObjectModelImage(image.data(), image.size());
  for (unsigned byte = 0; byte != 8; ++byte)
    image[16 + byte] = static_cast<uint8_t>(checksum >> (byte * 8));
}

OracleGraph buildLrmOracle() {
  OracleGraph graph;
  auto S = [](std::initializer_list<uint32_t> kinds) {
    return KindSet(kinds.begin(), kinds.end());
  };
  auto add = [&](const char *clause, const KindSet &sources, uint32_t selector,
                 Mode mode, const KindSet &targets, Order order,
                 bool statementContainment = false) {
    for (uint32_t source : sources) {
      auto [iterator, inserted] = graph.emplace(
          OracleKey{source, selector, mode},
          OracleEdge{targets, order, statementContainment, clause});
      if (!inserted)
        ADD_FAILURE() << "duplicate LRM oracle key "
                      << keyName(source, selector, mode) << " in " << clause
                      << "; previously declared in " << iterator->second.clause;
    }
  };

#include "VPITraversalLrmOracleEarly.inc"
#include "VPITraversalLrmOracleLate.inc"

  return graph;
}

TEST(VPIObjectModel, TraversalGraphExactlyMatchesIndependentLrmOracle) {
  OracleGraph expected = buildLrmOracle();
  ASSERT_EQ(expected.size(), kExpectedTraversalCount);
  std::set<OracleKey> actualKeys;

  for (const auto &edge : vpiTraversals) {
    OracleKey key{edge.sourceType, edge.selector, edge.mode};
    actualKeys.insert(key);
    auto iterator = expected.find(key);
    ASSERT_NE(iterator, expected.end())
        << "unexpected traversal "
        << keyName(edge.sourceType, edge.selector, edge.mode) << " from "
        << edge.clause;
    EXPECT_EQ(expandedTargets(edge.targets), iterator->second.targets)
        << keyName(edge.sourceType, edge.selector, edge.mode)
        << " differs from LRM " << iterator->second.clause;
    EXPECT_EQ(edge.order, iterator->second.order)
        << keyName(edge.sourceType, edge.selector, edge.mode)
        << " differs from LRM " << iterator->second.clause;
    EXPECT_EQ(edge.statementContainment, iterator->second.statementContainment)
        << keyName(edge.sourceType, edge.selector, edge.mode)
        << " has incorrect statement-containment semantics";
  }

  EXPECT_EQ(actualKeys.size(), expected.size());
  for (const auto &[key, edge] : expected)
    EXPECT_TRUE(actualKeys.count(key))
        << "missing LRM " << edge.clause << " traversal "
        << keyName(key.source, key.selector, key.mode);
}

TEST(VPIObjectModel, StatementCallbacksExactlyMatchLrmTable38_6) {
  using Policy = VPIStatementCallbackPolicy;
  const std::map<uint32_t, Policy> expected{
      {vpiBegin, Policy::OnceBefore},
      {vpiNamedBegin, Policy::OnceBefore},
      {vpiFork, Policy::OnceBefore},
      {vpiNamedFork, Policy::OnceBefore},
      {vpiIf, Policy::OnceBefore},
      {vpiIfElse, Policy::OnceBefore},
      {vpiWhile, Policy::ConditionEachIteration},
      {vpiRepeat, Policy::RepeatEncounterAndIteration},
      {vpiFor, Policy::ForInitialAndIncrement},
      {vpiForever, Policy::ForeverEncounterAndIteration},
      {vpiWait, Policy::OnceBefore},
      {vpiCase, Policy::OnceBefore},
      {vpiAssignment, Policy::OnceBefore},
      {vpiAssignStmt, Policy::OnceBefore},
      {vpiDeassign, Policy::OnceBefore},
      {vpiDisable, Policy::OnceBefore},
      {vpiForce, Policy::OnceBefore},
      {vpiRelease, Policy::OnceBefore},
      {vpiEventStmt, Policy::OnceBefore},
      {vpiDelayControl, Policy::DelayEncounter},
      {vpiEventControl, Policy::EventEncounter},
      {vpiTaskCall, Policy::CallBefore},
      {vpiSysTaskCall, Policy::CallBefore},
  };
  std::map<uint32_t, Policy> actual;
  for (const auto &callback : vpiStatementCallbacks) {
    EXPECT_TRUE(actual.emplace(callback.objectType, callback.policy).second);
    EXPECT_STREQ(callback.clause, "IEEE 1800-2017 Table 38-6");
    EXPECT_EQ(callback.phaseMask,
              callback.objectType == vpiFor ? uint8_t{0x6} : uint8_t{0x1})
        << objectName(callback.objectType);
  }
  EXPECT_EQ(actual, expected);

  using Phase = VPIStatementCallbackPhase;
  EXPECT_TRUE(isVPIStatementCallbackPhase(vpiFor, Phase::BeforeForControls));
  EXPECT_TRUE(isVPIStatementCallbackPhase(vpiFor, Phase::BeforeForIncrement));
  EXPECT_FALSE(isVPIStatementCallbackPhase(vpiFor, Phase::BeforeExecute));
  for (uint32_t kind :
       {vpiNullStmt, vpiCaseItem, vpiContAssign, vpiImmediateAssert, vpiDoWhile,
        vpiForeachStmt, vpiReturnStmt, vpiBreak, vpiContinue})
    EXPECT_EQ(findVPIStatementCallback(kind), nullptr) << objectName(kind);
}

TEST(VPIObjectModel, ScopeOwnedStatementsAreExactlyTheModuleLevelKinds) {
  const uint64_t scopeOwnedMask =
      vpiFamilyMask(VPIObjectFamily::ScopeOwnedStatement);
  for (const auto &object : vpiObjectKinds) {
    if (object.aliasOf != nullptr || object.role != VPIObjectRole::Concrete)
      continue;
    bool expected = object.value == vpiContAssign ||
                    object.value == vpiContAssignBit ||
                    object.value == vpiAliasStmt;
    EXPECT_EQ((object.families & scopeOwnedMask) != 0, expected)
        << object.apiName;
  }
}

TEST(VPIObjectModel, BlocksIterateStatementsWithoutAnLrmOrderGuarantee) {
  for (uint32_t source : {vpiBegin, vpiNamedBegin, vpiFork, vpiNamedFork}) {
    const auto &edge = requireTraversal(source, vpiStmt, Mode::Iterate);
    EXPECT_EQ(edge.order, Order::None) << objectName(source);
    EXPECT_TRUE(vpiObjectSetContains(edge.targets, vpiAssignment));
    EXPECT_TRUE(vpiObjectSetContains(edge.targets, vpiIfElse));
    EXPECT_TRUE(vpiObjectSetContains(edge.targets, vpiFor));
    EXPECT_FALSE(vpiObjectSetContains(edge.targets, vpiCaseItem));
    expectAbsent(source, vpiStmt, Mode::Handle);
  }
}

TEST(VPIObjectModel, StatementContainmentIsDistinctFromCrossReferences) {
  struct Key {
    uint32_t source;
    uint32_t selector;
    Mode mode;
  };
  constexpr Key containment[] = {
      {vpiModule, vpiContAssign, Mode::Iterate},
      {vpiModule, vpiAliasStmt, Mode::Iterate},
      {vpiContAssign, vpiBit, Mode::Iterate},
      {vpiTask, vpiStmt, Mode::Handle},
      {vpiBegin, vpiStmt, Mode::Iterate},
      {vpiInitial, vpiStmt, Mode::Handle},
      {vpiAssignment, vpiDelayControl, Mode::Handle},
      {vpiRepeatControl, vpiEventControl, Mode::Handle},
      {vpiIfElse, vpiElseStmt, Mode::Handle},
      {vpiCase, vpiCaseItem, Mode::Iterate},
      {vpiFor, vpiForInitStmt, Mode::Handle},
      {vpiFor, vpiForInitStmt, Mode::Iterate},
      {vpiImmediateAssert, vpiStmt, Mode::Handle},
      {vpiAssert, vpiElseStmt, Mode::Handle},
  };
  for (const auto &key : containment)
    EXPECT_TRUE(isVPIStatementContainment(key.source, key.selector, key.mode))
        << keyName(key.source, key.selector, key.mode);

  constexpr Key crossReferences[] = {
      {vpiNet, vpiContAssign, Mode::Iterate},
      {vpiFrame, vpiStmt, Mode::Handle},
      {vpiThread, vpiOrigin, Mode::Handle},
      {vpiContAssignBit, vpiParent, Mode::Handle},
      {vpiClockingBlock, vpiClockingEvent, Mode::Handle},
      {vpiIfElse, vpiScope, Mode::Handle},
  };
  for (const auto &key : crossReferences) {
    ASSERT_NE(findVPITraversal(key.source, key.selector, key.mode), nullptr)
        << keyName(key.source, key.selector, key.mode);
    EXPECT_FALSE(isVPIStatementContainment(key.source, key.selector, key.mode))
        << keyName(key.source, key.selector, key.mode);
  }
}

TEST(VPIObjectModel, AutomaticRelationsAreExplicitStructuralEdges) {
  using Automatic = VPIAutomaticRelation;
  for (const auto &edge : vpiTraversals) {
    if (edge.automaticRelation == Automatic::None)
      continue;
    EXPECT_FALSE(edge.statementContainment)
        << keyName(edge.sourceType, edge.selector, edge.mode);
    EXPECT_EQ(edge.mode, edge.automaticRelation == Automatic::DirectChild
                             ? Mode::Iterate
                             : Mode::Handle)
        << keyName(edge.sourceType, edge.selector, edge.mode);
  }

  EXPECT_EQ(
      requireTraversal(vpiModule, vpiProcess, Mode::Iterate).automaticRelation,
      Automatic::DirectChild);
  EXPECT_EQ(
      requireTraversal(vpiModule, vpiTaskFunc, Mode::Iterate).automaticRelation,
      Automatic::DirectChild);
  EXPECT_EQ(requireTraversal(vpiClassDefn, vpiInternalScope, Mode::Iterate)
                .automaticRelation,
            Automatic::DirectChild);
  EXPECT_EQ(requireTraversal(vpiClassDefn, vpiMethods, Mode::Iterate)
                .automaticRelation,
            Automatic::DirectChild);
  EXPECT_EQ(
      requireTraversal(vpiModule, vpiNet, Mode::Iterate).automaticRelation,
      Automatic::DirectChild);
  EXPECT_EQ(requireTraversal(vpiModule, vpiVariables, Mode::Iterate)
                .automaticRelation,
            Automatic::DirectChild);
  EXPECT_EQ(
      requireTraversal(vpiInitial, vpiModule, Mode::Handle).automaticRelation,
      Automatic::ParentScope);
  EXPECT_EQ(
      requireTraversal(vpiInitial, vpiScope, Mode::Handle).automaticRelation,
      Automatic::ParentScope);
  EXPECT_EQ(
      requireTraversal(vpiIfElse, vpiScope, Mode::Handle).automaticRelation,
      Automatic::ParentScope);
  EXPECT_EQ(
      requireTraversal(vpiPort, vpiInstance, Mode::Handle).automaticRelation,
      Automatic::ParentScope);
  EXPECT_EQ(
      requireTraversal(vpiPort, vpiLowConn, Mode::Handle).automaticRelation,
      Automatic::DirectPortConnection);
  EXPECT_EQ(requireTraversal(vpiReg, vpiModule, Mode::Handle).automaticRelation,
            Automatic::ParentScope);

  // General connectivity and expression relations cannot be inferred from
  // structural ownership and must remain explicit producer data. Direct
  // whole-source port aliases are the narrow connectivity exception above.
  EXPECT_EQ(
      requireTraversal(vpiNet, vpiDriver, Mode::Iterate).automaticRelation,
      Automatic::None);
  EXPECT_EQ(
      requireTraversal(vpiAssignment, vpiLhs, Mode::Handle).automaticRelation,
      Automatic::None);
}

TEST(VPIObjectModel, IteratorUseIsExactlyTheDerivedIterationSourceClosure) {
  KindSet expectedSources;
  for (const auto &edge : vpiTraversals)
    if (edge.mode == Mode::Iterate && edge.sourceType != 0)
      expectedSources.insert(edge.sourceType);

  const auto &use = requireTraversal(vpiIterator, vpiUse, Mode::Handle);
  EXPECT_EQ(expandedTargets(use.targets), expectedSources);
  expectAbsent(vpiIterator, vpiUse, Mode::Iterate);
}

TEST(VPIObjectModel, ScalarVectorPropertiesHaveExactLrmApplicability) {
  const KindSet expected{
      vpiIODecl,
      vpiPort,
      vpiPortBit,
      vpiNet,
      vpiNetBit,
      vpiNetArray,
      vpiEnumNet,
      vpiIntegerNet,
      vpiTimeNet,
      vpiUnionNet,
      vpiShortRealNet,
      vpiRealNet,
      vpiByteNet,
      vpiShortIntNet,
      vpiIntNet,
      vpiLongIntNet,
      vpiBitNet,
      vpiInterconnectNet,
      vpiInterconnectArray,
      vpiStructNet,
      vpiPackedArrayNet,
      vpiShortRealVar,
      vpiRealVar,
      vpiByteVar,
      vpiShortIntVar,
      vpiIntVar,
      vpiLongIntVar,
      vpiIntegerVar,
      vpiTimeVar,
      vpiRegArray,
      vpiPackedArrayVar,
      vpiBitVar,
      vpiReg,
      vpiStructVar,
      vpiUnionVar,
      vpiEnumVar,
      vpiStringVar,
      vpiChandleVar,
      vpiClassVar,
      vpiVirtualInterfaceVar,
      vpiRegBit,
  };
  KindSet expectedVector = expected;
  expectedVector.insert(vpiBitTypespec);
  expectedVector.insert(vpiLogicTypespec);
  expectedVector.insert(vpiPackedArrayTypespec);
  for (const auto &object : vpiObjectKinds) {
    if (object.aliasOf != nullptr || object.role != VPIObjectRole::Concrete)
      continue;
    for (uint32_t property : {uint32_t(vpiScalar), uint32_t(vpiVector)}) {
      const bool shouldExist =
          (property == vpiVector ? expectedVector : expected)
              .count(object.value) != 0;
      const auto *descriptor = findVPIProperty(object.value, property);
      EXPECT_EQ(descriptor != nullptr, shouldExist) << object.apiName;
      if (descriptor) {
        EXPECT_EQ(descriptor->valueKind, PropertyKind::Boolean);
        EXPECT_STREQ(descriptor->clause, property == vpiVector
                                             ? "37.14; 37.16; 37.17; 37.23"
                                             : "37.14; 37.16; 37.17");
      }
      VPIObjectModelImageProperty imageProperty{};
      EXPECT_EQ(findVPIObjectModelImageProperty(
                    vpiObjectModelImage, object.value, property, imageProperty),
                shouldExist)
          << object.apiName;
      if (shouldExist) {
        EXPECT_EQ(imageProperty.valueKind, PropertyKind::Boolean);
      }
    }
  }
}

TEST(VPIObjectModel, StructuralTypePropertiesHaveExactLrmApplicability) {
  struct Expected {
    uint32_t property;
    KindSet objects;
    PropertyKind kind;
    const char *clause;
  };
  const std::array<Expected, 6> expected{{
      {vpiSize,
       {vpiIODecl,
        vpiPort,
        vpiPortBit,
        vpiModuleArray,
        vpiInterfaceArray,
        vpiProgramArray,
        vpiGateArray,
        vpiSwitchArray,
        vpiUdpArray,
        vpiNet,
        vpiNetBit,
        vpiNetArray,
        vpiEnumNet,
        vpiIntegerNet,
        vpiTimeNet,
        vpiUnionNet,
        vpiShortRealNet,
        vpiRealNet,
        vpiByteNet,
        vpiShortIntNet,
        vpiIntNet,
        vpiLongIntNet,
        vpiBitNet,
        vpiInterconnectNet,
        vpiInterconnectArray,
        vpiStructNet,
        vpiPackedArrayNet,
        vpiIntegerVar,
        vpiRealVar,
        vpiReg,
        vpiRegBit,
        vpiTimeVar,
        vpiLongIntVar,
        vpiShortIntVar,
        vpiIntVar,
        vpiShortRealVar,
        vpiByteVar,
        vpiClassVar,
        vpiStringVar,
        vpiEnumVar,
        vpiStructVar,
        vpiUnionVar,
        vpiBitVar,
        vpiChandleVar,
        vpiPackedArrayVar,
        vpiVirtualInterfaceVar,
        vpiRegArray,
        vpiParameter,
        vpiSpecParam,
        vpiRefObj,
        vpiVarSelect,
        vpiBitSelect,
        vpiIndexedPartSelect,
        vpiPartSelect,
        vpiOperation,
        vpiConstant,
        vpiFuncCall,
        vpiMethodFuncCall,
        vpiSysFuncCall,
        vpiLetExpr,
        vpiRange,
        vpiGate,
        vpiSwitch,
        vpiUdp,
        vpiUdpDefn,
        vpiTableEntry,
        vpiFunction,
        vpiGenScopeArray},
       PropertyKind::Integer,
       "37.11; 37.13; 37.14; 37.16; 37.17; 37.18; 37.19; 37.22; "
       "37.26; 37.33; 37.34; 37.39; 37.57; 37.83"},
      {vpiPacked,
       {vpiPackedArrayVar, vpiStructVar, vpiUnionVar, vpiEnumVar,
        vpiStructTypespec, vpiUnionTypespec},
       PropertyKind::Boolean,
       "37.18; 37.23; 37.24"},
      {vpiTagged, {vpiUnionTypespec}, PropertyKind::Boolean, "37.23"},
      {vpiArrayType,
       {vpiRegArray, vpiArrayTypespec},
       PropertyKind::Integer,
       "37.17; 37.23"},
      {vpiRandType,
       {vpiIntegerVar,
        vpiRealVar,
        vpiReg,
        vpiRegBit,
        vpiTimeVar,
        vpiLongIntVar,
        vpiShortIntVar,
        vpiIntVar,
        vpiShortRealVar,
        vpiByteVar,
        vpiClassVar,
        vpiStringVar,
        vpiEnumVar,
        vpiStructVar,
        vpiUnionVar,
        vpiBitVar,
        vpiChandleVar,
        vpiPackedArrayVar,
        vpiVirtualInterfaceVar,
        vpiRegArray,
        vpiTypespecMember},
       PropertyKind::Integer,
       "37.17; 37.23"},
      {vpiConstType,
       {vpiConstant, vpiParameter},
       PropertyKind::Integer,
       "37.26; 37.57"},
  }};
  for (const Expected &item : expected) {
    for (const auto &object : vpiObjectKinds) {
      if (object.aliasOf != nullptr || object.role != VPIObjectRole::Concrete)
        continue;
      bool shouldExist = item.objects.count(object.value) != 0;
      const auto *descriptor = findVPIProperty(object.value, item.property);
      EXPECT_EQ(descriptor != nullptr, shouldExist) << object.apiName;
      if (descriptor) {
        EXPECT_EQ(descriptor->valueKind, item.kind) << object.apiName;
        EXPECT_STREQ(descriptor->clause, item.clause) << object.apiName;
      }
      VPIObjectModelImageProperty imageProperty{};
      EXPECT_EQ(findVPIObjectModelImageProperty(vpiObjectModelImage,
                                                object.value, item.property,
                                                imageProperty),
                shouldExist)
          << object.apiName;
      if (shouldExist) {
        EXPECT_EQ(imageProperty.valueKind, item.kind) << object.apiName;
      }
    }
  }
}

TEST(VPIObjectModel, ReadPropertySelectorInventoryIsExhaustive) {
  struct Expected {
    uint32_t value;
    const char *name;
    PropertyKind kind;
    PropertyStability stability;
    ProtectedAccess protectedAccess;
    bool symbolicString;
    const char *clause;
  };
#define STATIC_PROPERTY(NAME, KIND, CLAUSE)                                    \
  {                                                                            \
    NAME, #NAME, PropertyKind::KIND, PropertyStability::Static,                \
        ProtectedAccess::Denied, false, CLAUSE                                 \
  }
#define DYNAMIC_PROPERTY(NAME, KIND, CLAUSE)                                   \
  {                                                                            \
    NAME, #NAME, PropertyKind::KIND, PropertyStability::Dynamic,               \
        ProtectedAccess::Denied, false, CLAUSE                                 \
  }
#define PROTECTED_PROPERTY(NAME, KIND, CLAUSE)                                 \
  {                                                                            \
    NAME, #NAME, PropertyKind::KIND, PropertyStability::Static,                \
        ProtectedAccess::Allowed, false, CLAUSE                                \
  }
#define SYMBOLIC_PROPERTY(NAME, CLAUSE)                                        \
  {                                                                            \
    NAME, #NAME, PropertyKind::Integer, PropertyStability::Static,             \
        ProtectedAccess::Denied, true, CLAUSE                                  \
  }
#define PROTECTED_SYMBOLIC_PROPERTY(NAME, CLAUSE)                              \
  {                                                                            \
    NAME, #NAME, PropertyKind::Integer, PropertyStability::Static,             \
        ProtectedAccess::Allowed, true, CLAUSE                                 \
  }
  const std::array<Expected, 114> expected{{
      PROTECTED_SYMBOLIC_PROPERTY(vpiType, "37.3.2"),
      STATIC_PROPERTY(vpiName, String, "37.4-37.83"),
      STATIC_PROPERTY(vpiFullName, String, "37.10-37.83"),
      STATIC_PROPERTY(vpiSize, Integer,
                      "37.11; 37.13; 37.14; 37.16; 37.17; 37.18; 37.19; "
                      "37.22; 37.26; 37.33; 37.34; 37.39; 37.57; 37.83"),
      STATIC_PROPERTY(vpiFile, String, "37.3.3"),
      STATIC_PROPERTY(vpiLineNo, Integer, "37.3.3"),
      STATIC_PROPERTY(vpiTopModule, Boolean, "37.5"),
      STATIC_PROPERTY(vpiCellInstance, Boolean, "37.10"),
      STATIC_PROPERTY(vpiDefName, String, "37.10; 37.15; 37.28; 37.33; 37.34"),
      STATIC_PROPERTY(vpiProtected, Boolean, "37.10; 37.34; 37.83"),
      STATIC_PROPERTY(vpiTimeUnit, Integer, "37.10; 38.6"),
      STATIC_PROPERTY(vpiTimePrecision, Integer, "37.10; 38.6"),
      STATIC_PROPERTY(vpiDefNetType, Integer, "37.10"),
      STATIC_PROPERTY(vpiUnconnDrive, Integer, "37.10"),
      STATIC_PROPERTY(vpiDefFile, String, "37.10; 37.81"),
      STATIC_PROPERTY(vpiDefLineNo, Integer, "37.10; 37.81"),
      STATIC_PROPERTY(vpiScalar, Boolean, "37.14; 37.16; 37.17"),
      STATIC_PROPERTY(vpiVector, Boolean, "37.14; 37.16; 37.17; 37.23"),
      STATIC_PROPERTY(vpiExplicitName, Boolean, "37.14"),
      STATIC_PROPERTY(vpiDirection, Integer,
                      "37.13; 37.14; 37.33; 37.37; 37.46; 37.49; 37.51"),
      STATIC_PROPERTY(vpiConnByName, Boolean, "37.14; 37.26"),
      SYMBOLIC_PROPERTY(vpiNetType, "37.16"),
      STATIC_PROPERTY(vpiExplicitScalared, Boolean, "37.16"),
      STATIC_PROPERTY(vpiExplicitVectored, Boolean, "37.16"),
      STATIC_PROPERTY(vpiExpanded, Boolean, "37.16"),
      STATIC_PROPERTY(vpiImplicitDecl, Boolean, "37.16; 37.83"),
      STATIC_PROPERTY(vpiChargeStrength, Integer, "37.16"),
      STATIC_PROPERTY(vpiArray, Boolean,
                      "37.10; 37.16; 37.17; 37.25; 37.33; 37.83"),
      STATIC_PROPERTY(vpiPortIndex, Integer, "37.14"),
      STATIC_PROPERTY(vpiTermIndex, Integer, "37.33"),
      STATIC_PROPERTY(vpiStrength0, Integer, "37.16; 37.33; 37.45"),
      STATIC_PROPERTY(vpiStrength1, Integer, "37.16; 37.33; 37.45"),
      SYMBOLIC_PROPERTY(vpiPrimType, "37.33; 37.34"),
      STATIC_PROPERTY(vpiPolarity, Integer, "37.37"),
      STATIC_PROPERTY(vpiDataPolarity, Integer, "37.37"),
      STATIC_PROPERTY(vpiEdge, Integer, "37.37; 37.38"),
      STATIC_PROPERTY(vpiPathType, Integer, "37.37"),
      SYMBOLIC_PROPERTY(vpiTchkType, "37.38"),
      SYMBOLIC_PROPERTY(vpiOpType, "37.50; 37.52; 37.57; 37.62"),
      STATIC_PROPERTY(vpiConstType, Integer, "37.26; 37.57"),
      STATIC_PROPERTY(vpiBlocking, Boolean, "37.60; 37.62"),
      STATIC_PROPERTY(vpiCaseType, Integer, "37.70"),
      STATIC_PROPERTY(vpiNetDeclAssign, Boolean, "37.16; 37.45"),
      STATIC_PROPERTY(vpiFuncType, Integer, "37.39; 37.40"),
      STATIC_PROPERTY(vpiUserDefn, Boolean, "37.40"),
      DYNAMIC_PROPERTY(vpiScheduled, Boolean, "38.34"),
      STATIC_PROPERTY(vpiDefDelayMode, Integer, "37.10"),
      STATIC_PROPERTY(vpiDefDecayTime, Integer, "37.5"),
      DYNAMIC_PROPERTY(vpiActive, Boolean, "37.41; 37.42"),
      STATIC_PROPERTY(vpiAutomatic, Boolean,
                      "37.3.7; 37.10; 37.17; 37.25; 37.29; 37.30; 37.32; "
                      "37.39"),
      STATIC_PROPERTY(vpiCell, String, "37.10"),
      STATIC_PROPERTY(vpiConfig, String, "37.10"),
      STATIC_PROPERTY(vpiConstantSelect, Boolean,
                      "37.16; 37.17; 37.18; 37.19; 37.57"),
      STATIC_PROPERTY(vpiDecompile, String, "37.40; 37.57"),
      STATIC_PROPERTY(vpiDefAttribute, Boolean, "37.81"),
      SYMBOLIC_PROPERTY(vpiDelayType, "37.43"),
      STATIC_PROPERTY(vpiIteratorType, Integer, "37.82"),
      STATIC_PROPERTY(vpiLibrary, String, "37.10"),
      STATIC_PROPERTY(vpiOffset, Integer, "37.45"),
      SYMBOLIC_PROPERTY(vpiResolvedNetType, "37.16"),
      DYNAMIC_PROPERTY(vpiSaveRestartID, Integer, "38.9; 38.36.1"),
      DYNAMIC_PROPERTY(vpiSaveRestartLocation, String, "38.9; 38.11; 38.36.1"),
      DYNAMIC_PROPERTY(vpiValid, Integer, "37.3.7; Annex I"),
      STATIC_PROPERTY(vpiSigned, Boolean,
                      "37.13; 37.16; 37.17; 37.26; 37.39; 37.57"),
      STATIC_PROPERTY(vpiLocalParam, Boolean, "37.26; 37.29; 37.83"),
      STATIC_PROPERTY(vpiModPathHasIfNone, Boolean, "37.37"),
      STATIC_PROPERTY(vpiIndexedPartSelectType, Integer, "37.57"),
      STATIC_PROPERTY(vpiIsMemory, Boolean, "37.20"),
      PROTECTED_PROPERTY(vpiIsProtected, Boolean, "37.3.6"),
      STATIC_PROPERTY(vpiTop, Boolean, "37.10"),
      STATIC_PROPERTY(vpiUnit, Boolean, "37.10"),
      STATIC_PROPERTY(vpiJoinType, Integer, "37.12"),
      STATIC_PROPERTY(vpiAccessType, Integer, "37.8; 37.32; 37.39"),
      STATIC_PROPERTY(vpiArrayType, Integer, "37.17; 37.23"),
      STATIC_PROPERTY(vpiArrayMember, Boolean,
                      "37.5; 37.6; 37.9; 37.16; 37.17; 37.25; 37.33; "
                      "37.83"),
      DYNAMIC_PROPERTY(vpiIsRandomized, Boolean, "37.17"),
      STATIC_PROPERTY(vpiLocalVarDecls, Integer, "37.72"),
      STATIC_PROPERTY(vpiRandType, Integer, "37.17; 37.23"),
      STATIC_PROPERTY(vpiPortType, Integer, "37.14"),
      STATIC_PROPERTY(vpiConstantVariable, Boolean, "37.17"),
      STATIC_PROPERTY(vpiStructUnionMember, Boolean, "37.16; 37.17; 37.18"),
      STATIC_PROPERTY(vpiVisibility, Integer, "37.17; 37.39"),
      STATIC_PROPERTY(vpiAlwaysType, Integer, "37.61"),
      STATIC_PROPERTY(vpiDistType, Integer, "37.32"),
      STATIC_PROPERTY(vpiPacked, Boolean, "37.18; 37.23; 37.24"),
      STATIC_PROPERTY(vpiTagged, Boolean, "37.23"),
      STATIC_PROPERTY(vpiVirtual, Boolean, "37.29; 37.32; 37.39"),
      DYNAMIC_PROPERTY(vpiHasActual, Boolean, "37.59"),
      DYNAMIC_PROPERTY(vpiIsConstraintEnabled, Boolean, "37.32"),
      STATIC_PROPERTY(vpiSoft, Boolean, "37.36"),
      STATIC_PROPERTY(vpiClassType, Integer, "37.30"),
      STATIC_PROPERTY(vpiMethod, Boolean, "37.39"),
      STATIC_PROPERTY(vpiIsClockInferred, Boolean, "37.48"),
      STATIC_PROPERTY(vpiQualifier, Integer, "37.69; 37.70"),
      STATIC_PROPERTY(vpiInputEdge, Integer, "37.46"),
      STATIC_PROPERTY(vpiOutputEdge, Integer, "37.46"),
      STATIC_PROPERTY(vpiGeneric, Boolean, "37.15"),
      STATIC_PROPERTY(vpiCompatibilityMode, Integer, "Annex M"),
      STATIC_PROPERTY(vpiPackedArrayMember, Boolean, "37.16; 37.17; 37.18"),
      STATIC_PROPERTY(vpiOpStrong, Boolean, "37.50"),
      STATIC_PROPERTY(vpiIsDeferred, Integer, "37.53"),
      STATIC_PROPERTY(vpiAllocScheme, Integer, "37.3.7"),
      STATIC_PROPERTY(vpiIsCoverSequence, Boolean, "37.48"),
      DYNAMIC_PROPERTY(vpiObjId, Int64, "37.31"),
      STATIC_PROPERTY(vpiStartLine, Integer, "37.47"),
      STATIC_PROPERTY(vpiColumn, Integer, "37.47"),
      STATIC_PROPERTY(vpiEndLine, Integer, "37.47"),
      STATIC_PROPERTY(vpiEndColumn, Integer, "37.47"),
      STATIC_PROPERTY(vpiDPIPure, Boolean, "37.39"),
      STATIC_PROPERTY(vpiDPIContext, Boolean, "37.39"),
      STATIC_PROPERTY(vpiDPICStr, Integer, "37.39"),
      STATIC_PROPERTY(vpiDPICIdentifier, String, "37.39"),
      STATIC_PROPERTY(vpiIsModPort, Boolean, "37.27; 37.28"),
      STATIC_PROPERTY(vpiIsFinal, Integer, "37.53"),
  }};
#undef STATIC_PROPERTY
#undef DYNAMIC_PROPERTY
#undef PROTECTED_PROPERTY
#undef SYMBOLIC_PROPERTY
#undef PROTECTED_SYMBOLIC_PROPERTY

  std::map<uint32_t, Expected> oracle;
  for (const Expected &item : expected)
    ASSERT_TRUE(oracle.try_emplace(item.value, item).second) << item.name;
  EXPECT_EQ(oracle.size(), expected.size());

  std::map<uint32_t, size_t> generatedCounts;
  const KindSet dynamicSizeObjects{
      vpiRegArray,       vpiStringVar,   vpiRefObj,
      vpiVarSelect,      vpiOperation,   vpiFuncCall,
      vpiMethodFuncCall, vpiSysFuncCall, vpiLetExpr};
  KindSet protectedSizeObjects{vpiParameter,
                               vpiSpecParam,
                               vpiShortRealVar,
                               vpiRealVar,
                               vpiByteVar,
                               vpiShortIntVar,
                               vpiIntVar,
                               vpiLongIntVar,
                               vpiIntegerVar,
                               vpiTimeVar,
                               vpiRegArray,
                               vpiPackedArrayVar,
                               vpiBitVar,
                               vpiReg,
                               vpiStructVar,
                               vpiUnionVar,
                               vpiEnumVar,
                               vpiStringVar,
                               vpiChandleVar,
                               vpiClassVar,
                               vpiVirtualInterfaceVar,
                               vpiRegBit,
                               vpiRefObj,
                               vpiVarSelect,
                               vpiBitSelect,
                               vpiIndexedPartSelect,
                               vpiPartSelect,
                               vpiOperation,
                               vpiConstant,
                               vpiFuncCall,
                               vpiMethodFuncCall,
                               vpiSysFuncCall,
                               vpiLetExpr};
  for (const VPIObjectKindDescriptor &object : vpiObjectKinds)
    if (object.aliasOf == nullptr && object.role == VPIObjectRole::Concrete &&
        (object.families & vpiFamilyMask(VPIObjectFamily::Net)) != 0)
      protectedSizeObjects.insert(object.value);
  for (const VPIPropertyDescriptor &descriptor : vpiProperties) {
    auto found = oracle.find(descriptor.property);
    ASSERT_NE(found, oracle.end()) << descriptor.apiName;
    const Expected &item = found->second;
    EXPECT_STREQ(descriptor.apiName, item.name);
    EXPECT_EQ(descriptor.valueKind, item.kind) << item.name;
    PropertyStability expectedStability = item.stability;
    if (descriptor.property == vpiSize &&
        dynamicSizeObjects.count(descriptor.sourceType) != 0)
      expectedStability = PropertyStability::Dynamic;
    if (descriptor.property == vpiAllocScheme) {
      const auto *object = findVPIObjectKind(descriptor.sourceType);
      if (object &&
          (object->families & (vpiFamilyMask(VPIObjectFamily::Variable) |
                               vpiFamilyMask(VPIObjectFamily::Net) |
                               vpiFamilyMask(VPIObjectFamily::Transient))) != 0)
        expectedStability = PropertyStability::Dynamic;
      switch (descriptor.sourceType) {
      case vpiRefObj:
      case vpiVarSelect:
      case vpiBitSelect:
      case vpiPartSelect:
      case vpiIndexedPartSelect:
      case vpiFuncCall:
      case vpiTaskCall:
      case vpiSysFuncCall:
      case vpiSysTaskCall:
      case vpiMethodFuncCall:
      case vpiMethodTaskCall:
        expectedStability = PropertyStability::Dynamic;
        break;
      default:
        break;
      }
    }
    ProtectedAccess expectedProtectedAccess = item.protectedAccess;
    if (descriptor.property == vpiSize &&
        protectedSizeObjects.count(descriptor.sourceType) != 0)
      expectedProtectedAccess = ProtectedAccess::Allowed;
    EXPECT_EQ(descriptor.stability, expectedStability) << item.name;
    EXPECT_EQ(descriptor.protectedAccess, expectedProtectedAccess) << item.name;
    EXPECT_EQ(descriptor.symbolicString, item.symbolicString) << item.name;
    EXPECT_STREQ(descriptor.clause, item.clause) << item.name;
    ++generatedCounts[descriptor.property];

    VPIObjectModelImageProperty imageProperty{};
    ASSERT_TRUE(findVPIObjectModelImageProperty(
        vpiObjectModelImage, descriptor.sourceType, descriptor.property,
        imageProperty));
    EXPECT_EQ(imageProperty.valueKind, item.kind);
    EXPECT_EQ(imageProperty.stability, expectedStability);
    EXPECT_EQ(imageProperty.protectedAccess, expectedProtectedAccess);
    EXPECT_EQ(imageProperty.symbolicString, item.symbolicString);
  }
  for (const Expected &item : expected)
    EXPECT_NE(generatedCounts[item.value], 0u) << item.name;
}

TEST(VPIObjectModel, ReadPropertyApplicabilityExactlyMatchesLrmOracle) {
  const std::map<uint32_t, KindSet> expected =
      buildReadPropertyApplicabilityOracle();
  std::map<uint32_t, KindSet> actual;
  for (const VPIPropertyDescriptor &descriptor : vpiProperties)
    actual[descriptor.property].insert(descriptor.sourceType);

  ASSERT_EQ(actual.size(), expected.size());
  for (const auto &[property, expectedObjects] : expected) {
    auto found = actual.find(property);
    ASSERT_NE(found, actual.end()) << property;
    EXPECT_EQ(found->second, expectedObjects)
        << "property " << property << " expected: " << setNames(expectedObjects)
        << " actual: " << setNames(found->second);
  }
}

TEST(VPIObjectModel, SymbolicStringPropertiesExactlyMatchLrm) {
  const std::set<uint32_t> expected{
      vpiType,     vpiDelayType, vpiNetType,        vpiOpType,
      vpiPrimType, vpiTchkType,  vpiResolvedNetType};
  std::set<uint32_t> actual;
  for (const VPIPropertyDescriptor &descriptor : vpiProperties) {
    if (!descriptor.symbolicString)
      continue;
    actual.insert(descriptor.property);
    VPIObjectModelImageProperty imageProperty{};
    ASSERT_TRUE(findVPIObjectModelImageProperty(
        vpiObjectModelImage, descriptor.sourceType, descriptor.property,
        imageProperty));
    EXPECT_TRUE(imageProperty.symbolicString) << descriptor.apiName;
    EXPECT_EQ(descriptor.valueKind, PropertyKind::Integer)
        << descriptor.apiName;
  }
  EXPECT_EQ(actual, expected);
}

TEST(VPIObjectModel, ValuePoliciesExactlyMatchTheIndependentLrmOracle) {
  struct ExpectedPolicy {
    uint16_t formats;
    ValueDefault defaultFormat;
    ValueRead readSemantics;
    uint8_t requirements;
    const char *clause;
  };
  constexpr uint16_t fullFormats = 0x1ffe;
  constexpr uint16_t tableEntryFormats =
      (uint16_t{1} << vpiStringVal) | (uint16_t{1} << vpiVectorVal);
  constexpr uint8_t rejectWholeUnpacked =
      static_cast<uint8_t>(VPIValueRequirement::RejectWholeUnpacked);
  constexpr uint8_t rejectDefinitionOrigin =
      static_cast<uint8_t>(VPIValueRequirement::RejectClassDefinitionOrigin);
  constexpr uint8_t rejectTypespecOrigin = static_cast<uint8_t>(
      VPIValueRequirement::RejectNonStaticClassTypespecOrigin);
  constexpr uint8_t restrictStringConstant =
      static_cast<uint8_t>(VPIValueRequirement::RestrictStringConstant);
  constexpr uint8_t rejectNonRuntimeOrigin =
      rejectDefinitionOrigin | rejectTypespecOrigin;

  std::map<uint32_t, ExpectedPolicy> oracle;
  auto add = [&](std::initializer_list<uint32_t> objects, uint16_t formats,
                 ValueDefault defaultFormat, ValueRead readSemantics,
                 uint8_t requirements, const char *clause) {
    for (uint32_t object : objects)
      ASSERT_TRUE(oracle
                      .emplace(object, ExpectedPolicy{formats, defaultFormat,
                                                      readSemantics,
                                                      requirements, clause})
                      .second)
          << objectName(object);
  };

  add({vpiNet, vpiNetBit, vpiBitNet, vpiInterconnectNet, vpiPackedArrayNet},
      fullFormats, ValueDefault::ScalarOrVector, ValueRead::Snapshot, 0,
      "37.16; 38.15");
  add({vpiEnumNet}, fullFormats, ValueDefault::Semantic, ValueRead::Snapshot, 0,
      "37.16; 38.15");
  add({vpiIntegerNet, vpiByteNet, vpiShortIntNet, vpiIntNet, vpiLongIntNet},
      fullFormats, ValueDefault::Integer, ValueRead::Snapshot, 0,
      "37.16; 38.15");
  add({vpiShortRealNet, vpiRealNet}, fullFormats, ValueDefault::Real,
      ValueRead::Snapshot, 0, "37.16; 38.15");
  add({vpiTimeNet}, fullFormats, ValueDefault::Time, ValueRead::Snapshot, 0,
      "37.16; 38.15");
  add({vpiStructNet, vpiUnionNet}, fullFormats, ValueDefault::ScalarOrVector,
      ValueRead::Snapshot, rejectWholeUnpacked, "37.16; 38.15");

  add({vpiReg, vpiRegBit, vpiBitVar, vpiPackedArrayVar}, fullFormats,
      ValueDefault::ScalarOrVector, ValueRead::Snapshot, rejectNonRuntimeOrigin,
      "37.17; 38.15");
  add({vpiEnumVar, vpiChandleVar}, fullFormats, ValueDefault::Semantic,
      ValueRead::Snapshot, rejectNonRuntimeOrigin, "37.17; 38.15");
  add({vpiIntegerVar, vpiByteVar, vpiShortIntVar, vpiIntVar, vpiLongIntVar},
      fullFormats, ValueDefault::Integer, ValueRead::Snapshot,
      rejectNonRuntimeOrigin, "37.17; 38.15");
  add({vpiShortRealVar, vpiRealVar}, fullFormats, ValueDefault::Real,
      ValueRead::Snapshot, rejectNonRuntimeOrigin, "37.17; 38.15");
  add({vpiTimeVar}, fullFormats, ValueDefault::Time, ValueRead::Snapshot,
      rejectNonRuntimeOrigin, "37.17; 38.15");
  add({vpiStringVar}, fullFormats, ValueDefault::String, ValueRead::Snapshot,
      rejectNonRuntimeOrigin, "37.17; 38.15");
  add({vpiStructVar, vpiUnionVar}, fullFormats, ValueDefault::ScalarOrVector,
      ValueRead::Snapshot, rejectWholeUnpacked | rejectNonRuntimeOrigin,
      "37.17; 37.24; 38.15");

  add({vpiVarSelect, vpiBitSelect, vpiPartSelect, vpiIndexedPartSelect,
       vpiOperation, vpiFuncCall, vpiMethodFuncCall, vpiSysFuncCall,
       vpiLetExpr},
      fullFormats, ValueDefault::Semantic, ValueRead::Evaluate,
      rejectNonRuntimeOrigin, "37.19; 37.57; 38.15");
  add({vpiConstant}, fullFormats, ValueDefault::Semantic, ValueRead::Snapshot,
      restrictStringConstant, "37.57; 38.15");
  add({vpiParameter, vpiSpecParam, vpiEnumConst, vpiAttribute}, fullFormats,
      ValueDefault::Semantic, ValueRead::Snapshot, 0,
      "37.23; 37.26; 37.81; 38.15");
  add({vpiGate, vpiSwitch, vpiUdp, vpiPrimTerm, vpiDelayTerm, vpiContAssign,
       vpiContAssignBit},
      fullFormats, ValueDefault::Semantic, ValueRead::Snapshot, 0,
      "37.33; 37.43; 37.45; 38.15");
  add({vpiFsmHandle}, fullFormats, ValueDefault::Semantic, ValueRead::Snapshot,
      0, "40.5.3");
  add({vpiTableEntry}, tableEntryFormats, ValueDefault::String,
      ValueRead::Snapshot, 0, "37.34; 38.15");

  ASSERT_EQ(oracle.size(), kExpectedValuePolicyCount);
  const VPIValuePolicyDescriptor *previous = nullptr;
  for (const auto &policy : vpiValuePolicies) {
    SCOPED_TRACE(objectName(policy.sourceType));
    auto expected = oracle.find(policy.sourceType);
    ASSERT_NE(expected, oracle.end());
    EXPECT_EQ(policy.formatMask, expected->second.formats);
    EXPECT_EQ(policy.defaultFormat, expected->second.defaultFormat);
    EXPECT_EQ(policy.readSemantics, expected->second.readSemantics);
    EXPECT_EQ(policy.requirements, expected->second.requirements);
    EXPECT_STREQ(policy.clause, expected->second.clause);
    EXPECT_EQ(findVPIValuePolicy(policy.sourceType), &policy);
    if (previous) {
      EXPECT_LT(previous->sourceType, policy.sourceType);
    }
    previous = &policy;

    for (uint32_t format = 0; format != 19; ++format)
      EXPECT_EQ(acceptsVPIValueFormat(policy, format),
                (policy.formatMask & (uint16_t{1} << format)) != 0)
          << "format " << format;

    VPIObjectModelImageValuePolicy imagePolicy{};
    ASSERT_TRUE(findVPIObjectModelImageValuePolicy(
        vpiObjectModelImage, policy.sourceType, imagePolicy));
    EXPECT_EQ(imagePolicy.formatMask, policy.formatMask);
    EXPECT_EQ(imagePolicy.defaultFormat, policy.defaultFormat);
    EXPECT_EQ(imagePolicy.readSemantics, policy.readSemantics);
    EXPECT_EQ(imagePolicy.requirements, policy.requirements);
  }

  for (const auto &object : vpiObjectKinds) {
    if (object.aliasOf != nullptr || object.role != VPIObjectRole::Concrete)
      continue;
    const bool expected = oracle.count(object.value) != 0;
    EXPECT_EQ(findVPIValuePolicy(object.value) != nullptr, expected)
        << object.apiName;
    VPIObjectModelImageValuePolicy imagePolicy{};
    EXPECT_EQ(findVPIObjectModelImageValuePolicy(vpiObjectModelImage,
                                                 object.value, imagePolicy),
              expected)
        << object.apiName;
  }
}

TEST(VPIObjectModel, ArrayValuePoliciesExactlyMatchTheIndependentLrmOracle) {
  static_assert(static_cast<uint8_t>(ArrayValueFormat::Int) == vpiIntVal);
  static_assert(static_cast<uint8_t>(ArrayValueFormat::Real) == vpiRealVal);
  static_assert(static_cast<uint8_t>(ArrayValueFormat::Vector) == vpiVectorVal);
  static_assert(static_cast<uint8_t>(ArrayValueFormat::Time) == vpiTimeVal);
  static_assert(static_cast<uint8_t>(ArrayValueFormat::ShortInt) ==
                vpiShortIntVal);
  static_assert(static_cast<uint8_t>(ArrayValueFormat::LongInt) ==
                vpiLongIntVal);
  static_assert(static_cast<uint8_t>(ArrayValueFormat::ShortReal) ==
                vpiShortRealVal);
  static_assert(static_cast<uint8_t>(ArrayValueFormat::RawTwoState) ==
                vpiRawTwoStateVal);
  static_assert(static_cast<uint8_t>(ArrayValueFormat::RawFourState) ==
                vpiRawFourStateVal);

  const KindSet bitLayouts{
      vpiNet,
      vpiInterconnectNet,
      vpiIntegerNet,
      vpiTimeNet,
      vpiByteNet,
      vpiShortIntNet,
      vpiIntNet,
      vpiLongIntNet,
      vpiBitNet,
      vpiEnumNet,
      vpiStructNet,
      vpiUnionNet,
      vpiPackedArrayNet,
      vpiReg,
      vpiIntegerVar,
      vpiTimeVar,
      vpiByteVar,
      vpiShortIntVar,
      vpiIntVar,
      vpiLongIntVar,
      vpiBitVar,
      vpiEnumVar,
      vpiStructVar,
      vpiUnionVar,
      vpiPackedArrayVar,
  };
  const std::map<uint32_t, KindSet> oracle{
      {vpiIntVal, {vpiIntVar, vpiIntNet, vpiIntegerVar, vpiIntegerNet}},
      {vpiRealVal, {vpiRealVar, vpiRealNet}},
      {vpiVectorVal, bitLayouts},
      {vpiTimeVal, {vpiTimeVar, vpiTimeNet}},
      {vpiShortIntVal, {vpiByteVar, vpiShortIntVar}},
      {vpiLongIntVal, {vpiByteVar, vpiShortIntVar, vpiLongIntVar}},
      {vpiShortRealVal, {vpiShortRealVar}},
      {vpiRawTwoStateVal, bitLayouts},
      {vpiRawFourStateVal, bitLayouts},
  };

  ASSERT_EQ(oracle.size(), kExpectedArrayValuePolicyCount);
  const VPIArrayValuePolicyDescriptor *previous = nullptr;
  size_t expectedFirstElement = 0;
  for (const auto &policy : vpiArrayValuePolicies) {
    SCOPED_TRACE(policy.format);
    auto expected = oracle.find(policy.format);
    ASSERT_NE(expected, oracle.end());
    EXPECT_STREQ(policy.clause, "38.16");
    EXPECT_EQ(policy.firstElementType, expectedFirstElement);
    EXPECT_EQ(policy.elementTypeCount, expected->second.size());
    EXPECT_EQ(findVPIArrayValuePolicy(policy.format), &policy);
    if (previous) {
      EXPECT_LT(previous->format, policy.format);
    }
    previous = &policy;

    size_t offset = policy.firstElementType;
    uint32_t previousElement = 0;
    for (uint32_t expectedElement : expected->second) {
      ASSERT_LT(offset, kExpectedArrayValueElementTypeCount);
      EXPECT_EQ(vpiArrayValueElementTypes[offset], expectedElement);
      if (offset != policy.firstElementType) {
        EXPECT_LT(previousElement, vpiArrayValueElementTypes[offset]);
      }
      previousElement = vpiArrayValueElementTypes[offset++];
    }
    expectedFirstElement += expected->second.size();

    for (const auto &object : vpiObjectKinds) {
      if (object.aliasOf != nullptr || object.role != VPIObjectRole::Concrete)
        continue;
      const bool allowed = expected->second.count(object.value) != 0;
      EXPECT_EQ(acceptsVPIArrayValueFormat(policy.format, object.value),
                allowed)
          << object.apiName;
      VPIObjectModelImageArrayValuePolicy imagePolicy{};
      EXPECT_EQ(
          findVPIObjectModelImageArrayValuePolicy(
              vpiObjectModelImage, policy.format, object.value, imagePolicy),
          allowed)
          << object.apiName;
      if (allowed) {
        EXPECT_EQ(imagePolicy.format, policy.format);
        EXPECT_EQ(imagePolicy.elementType, object.value);
      }
    }
  }
  EXPECT_EQ(expectedFirstElement, kExpectedArrayValueElementTypeCount);

  for (uint32_t format = 0; format != 21; ++format) {
    const bool supported = oracle.count(format) != 0;
    EXPECT_EQ(findVPIArrayValuePolicy(format) != nullptr, supported) << format;
    if (!supported) {
      for (const auto &object : vpiObjectKinds)
        EXPECT_FALSE(acceptsVPIArrayValueFormat(format, object.value))
            << format << ": " << object.apiName;
    }
  }

  // Public aliases have the same numeric object identity as their canonical
  // Chapter 37 spellings and therefore inherit the same compatibility.
  EXPECT_TRUE(acceptsVPIArrayValueFormat(vpiVectorVal, vpiLogicVar));
  EXPECT_FALSE(acceptsVPIArrayValueFormat(vpiVectorVal, vpiArrayNet));
  EXPECT_FALSE(acceptsVPIArrayValueFormat(vpiShortIntVal, vpiShortIntNet));
  EXPECT_FALSE(acceptsVPIArrayValueFormat(vpiLongIntVal, vpiLongIntNet));
  EXPECT_FALSE(acceptsVPIArrayValueFormat(vpiShortRealVal, vpiShortRealNet));
}

TEST(VPIObjectModel, UniversalPropertiesCoverEveryConcreteObject) {
  for (const auto &object : vpiObjectKinds) {
    if (object.aliasOf != nullptr || object.role != VPIObjectRole::Concrete)
      continue;
    for (const auto &[property, kind] :
         std::array<std::pair<uint32_t, PropertyKind>, 2>{{
             {vpiType, PropertyKind::Integer},
             {vpiIsProtected, PropertyKind::Boolean},
         }}) {
      const auto *descriptor = findVPIProperty(object.value, property);
      ASSERT_NE(descriptor, nullptr) << object.apiName;
      EXPECT_EQ(descriptor->valueKind, kind) << object.apiName;
      VPIObjectModelImageProperty imageProperty{};
      ASSERT_TRUE(findVPIObjectModelImageProperty(
          vpiObjectModelImage, object.value, property, imageProperty))
          << object.apiName;
      EXPECT_EQ(imageProperty.valueKind, kind) << object.apiName;
    }
  }
}

TEST(VPIObjectModel, SourceLocationPropertiesHaveExactGlobalExclusions) {
  const KindSet excluded{vpiDelayTerm,     vpiDelayDevice, vpiInterModPath,
                         vpiGenScopeArray, vpiGenScope,    vpiThread};
  constexpr uint32_t sourceFamilies =
      vpiFamilyMask(VPIObjectFamily::Scope) |
      vpiFamilyMask(VPIObjectFamily::Declaration) |
      vpiFamilyMask(VPIObjectFamily::Process) |
      vpiFamilyMask(VPIObjectFamily::Statement) |
      vpiFamilyMask(VPIObjectFamily::Expression) |
      vpiFamilyMask(VPIObjectFamily::Variable) |
      vpiFamilyMask(VPIObjectFamily::Net) |
      vpiFamilyMask(VPIObjectFamily::Array) |
      vpiFamilyMask(VPIObjectFamily::Typespec) |
      vpiFamilyMask(VPIObjectFamily::Primitive) |
      vpiFamilyMask(VPIObjectFamily::Timing) |
      vpiFamilyMask(VPIObjectFamily::Assertion);
  for (const auto &object : vpiObjectKinds) {
    if (object.aliasOf != nullptr || object.role != VPIObjectRole::Concrete)
      continue;
    const bool expected = (object.families & sourceFamilies) != 0 &&
                          !excluded.count(object.value);
    for (const auto &[property, kind] :
         std::array<std::pair<uint32_t, PropertyKind>, 2>{{
             {vpiFile, PropertyKind::String},
             {vpiLineNo, PropertyKind::Integer},
         }}) {
      const auto *descriptor = findVPIProperty(object.value, property);
      EXPECT_EQ(descriptor != nullptr, expected) << object.apiName;
      if (descriptor) {
        EXPECT_EQ(descriptor->valueKind, kind) << object.apiName;
      }
      VPIObjectModelImageProperty imageProperty{};
      EXPECT_EQ(findVPIObjectModelImageProperty(
                    vpiObjectModelImage, object.value, property, imageProperty),
                expected)
          << object.apiName;
      if (expected) {
        EXPECT_EQ(imageProperty.valueKind, kind) << object.apiName;
      }
    }
  }
}

TEST(VPIObjectModel, NullRootAndClassIdentityPropertiesAreExact) {
  for (uint32_t property :
       {uint32_t(vpiTimeUnit), uint32_t(vpiTimePrecision)}) {
    const auto *root = findVPIProperty(0, property);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->valueKind, PropertyKind::Integer);
    for (uint32_t object : {uint32_t(vpiPackage), uint32_t(vpiModule),
                            uint32_t(vpiInterface), uint32_t(vpiProgram)})
      ASSERT_NE(findVPIProperty(object, property), nullptr);
    EXPECT_EQ(findVPIProperty(vpiReg, property), nullptr);
  }

  for (uint32_t object : {uint32_t(vpiClassVar), uint32_t(vpiClassObj)}) {
    const auto *identity = findVPIProperty(object, vpiObjId);
    ASSERT_NE(identity, nullptr);
    EXPECT_EQ(identity->valueKind, PropertyKind::Int64);
  }
  EXPECT_EQ(findVPIProperty(vpiModule, vpiObjId), nullptr);
}

TEST(VPIObjectModel, PortPropertiesHaveExactLrmApplicability) {
  struct ExpectedProperty {
    uint32_t value;
    PropertyKind kind;
  };
  constexpr std::array<ExpectedProperty, 2> expected{{
      {vpiPortIndex, PropertyKind::Integer},
      {vpiPortType, PropertyKind::Integer},
  }};
  for (const auto &property : expected) {
    const auto *descriptor = findVPIProperty(vpiPort, property.value);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->valueKind, property.kind);
    EXPECT_STREQ(descriptor->clause, "37.14");
    const auto *bitDescriptor = findVPIProperty(vpiPortBit, property.value);
    ASSERT_NE(bitDescriptor, nullptr);
    EXPECT_EQ(bitDescriptor->valueKind, property.kind);
    EXPECT_STREQ(bitDescriptor->clause, "37.14");
    EXPECT_EQ(findVPIProperty(vpiModule, property.value), nullptr);
    EXPECT_EQ(findVPIProperty(vpiReg, property.value), nullptr);
    EXPECT_EQ(findVPIProperty(vpiNet, property.value), nullptr);

    VPIObjectModelImageProperty imageProperty{};
    ASSERT_TRUE(findVPIObjectModelImageProperty(vpiObjectModelImage, vpiPort,
                                                property.value, imageProperty));
    EXPECT_EQ(imageProperty.valueKind, property.kind);
    ASSERT_TRUE(findVPIObjectModelImageProperty(vpiObjectModelImage, vpiPortBit,
                                                property.value, imageProperty));
    EXPECT_EQ(imageProperty.valueKind, property.kind);
    EXPECT_FALSE(findVPIObjectModelImageProperty(
        vpiObjectModelImage, vpiReg, property.value, imageProperty));
  }

  const KindSet directionObjects{
      vpiIODecl,   vpiPort,           vpiPortBit,        vpiPrimTerm,
      vpiPathTerm, vpiClockingIODecl, vpiPropFormalDecl, vpiSeqFormalDecl};
  for (const auto &object : vpiObjectKinds) {
    if (object.aliasOf != nullptr || object.role != VPIObjectRole::Concrete)
      continue;
    const auto *descriptor = findVPIProperty(object.value, vpiDirection);
    const bool expectedDirection = directionObjects.count(object.value) != 0;
    EXPECT_EQ(descriptor != nullptr, expectedDirection) << object.apiName;
    if (descriptor) {
      EXPECT_EQ(descriptor->valueKind, PropertyKind::Integer);
      EXPECT_STREQ(descriptor->clause,
                   "37.13; 37.14; 37.33; 37.37; 37.46; 37.49; 37.51");
    }
  }
}

TEST(VPIObjectModel, CompactImageExactlyMatchesTheLrmTraversalGraph) {
  ASSERT_TRUE(validateVPIObjectModelImage(vpiObjectModelImage,
                                          sizeof(vpiObjectModelImage)));
  EXPECT_LT(sizeof(vpiObjectModelImage), 48u * 1024u);
  EXPECT_EQ(readVPIObjectModelImage32(vpiObjectModelImage, 60),
            kExpectedTraversalCount);
  EXPECT_EQ(readVPIObjectModelImage32(vpiObjectModelImage, 68),
            kExpectedPropertyCount);
  EXPECT_EQ(readVPIObjectModelImage32(vpiObjectModelImage, 76),
            kExpectedValuePolicyCount);
  EXPECT_EQ(readVPIObjectModelImage32(vpiObjectModelImage, 84),
            kExpectedIndexedAccessCount);
  EXPECT_EQ(readVPIObjectModelImage32(vpiObjectModelImage, 92),
            kExpectedIndexedTypeResultCount);

  size_t canonicalObjects = 0;
  for (const auto &object : vpiObjectKinds)
    canonicalObjects += object.aliasOf == nullptr;
  size_t canonicalRelations = 0;
  for (const auto &relation : vpiRelations)
    canonicalRelations += relation.aliasOf == nullptr;
  EXPECT_EQ(readVPIObjectModelImage32(vpiObjectModelImage, 28),
            canonicalObjects);
  EXPECT_EQ(readVPIObjectModelImage32(vpiObjectModelImage, 36),
            canonicalRelations);

  uint32_t objectOffset = readVPIObjectModelImage32(vpiObjectModelImage, 24);
  uint32_t imageObject = 0;
  for (const auto &object : vpiObjectKinds) {
    if (object.aliasOf != nullptr)
      continue;
    const uint8_t *record = vpiObjectModelImage + objectOffset +
                            imageObject * vpiObjectModelImageObjectSize;
    EXPECT_EQ(readVPIObjectModelImage16(record, 0), object.value)
        << object.apiName;
    EXPECT_EQ(record[2], static_cast<uint8_t>(object.role)) << object.apiName;
    EXPECT_EQ(record[3], 0) << object.apiName;
    EXPECT_EQ(readVPIObjectModelImage64(record, 4), object.families)
        << object.apiName;
    ++imageObject;
  }
  uint32_t relationOffset = readVPIObjectModelImage32(vpiObjectModelImage, 32);
  uint32_t imageRelation = 0;
  for (const auto &relation : vpiRelations) {
    if (relation.aliasOf != nullptr)
      continue;
    const uint8_t *record = vpiObjectModelImage + relationOffset +
                            imageRelation * vpiObjectModelImageRelationSize;
    EXPECT_EQ(readVPIObjectModelImage16(record, 0), relation.value)
        << relation.apiName;
    EXPECT_EQ(record[2], static_cast<uint8_t>(relation.cardinality))
        << relation.apiName;
    EXPECT_EQ(record[3], 0) << relation.apiName;
    ++imageRelation;
  }

  for (const auto &edge : vpiTraversals) {
    SCOPED_TRACE(keyName(edge.sourceType, edge.selector, edge.mode));
    VPIObjectModelImageTraversal imageEdge{};
    ASSERT_TRUE(findVPIObjectModelImageTraversal(vpiObjectModelImage,
                                                 edge.sourceType, edge.selector,
                                                 edge.mode, imageEdge));
    EXPECT_EQ(imageEdge.order, edge.order);
    EXPECT_EQ(imageEdge.statementContainment, edge.statementContainment);
    EXPECT_EQ(imageEdge.automaticRelation, edge.automaticRelation);
    for (const auto &object : vpiObjectKinds) {
      if (object.aliasOf != nullptr || object.role != VPIObjectRole::Concrete)
        continue;
      EXPECT_EQ(vpiObjectModelImageTargetContains(
                    vpiObjectModelImage, imageEdge.targets, object.value),
                vpiObjectSetContains(edge.targets, object.value))
          << object.apiName;
    }
  }

  for (const auto &access : vpiIndexedAccesses) {
    VPIObjectModelImageIndexedAccess imageAccess{};
    ASSERT_TRUE(findVPIObjectModelImageIndexedAccess(
        vpiObjectModelImage, access.sourceType, imageAccess));
    EXPECT_EQ(imageAccess.accessKind, access.accessKind);
    EXPECT_EQ(imageAccess.terminalResult, access.terminalResult);
    if (access.accessKind == IndexedKind::RelationElement) {
      EXPECT_EQ(imageAccess.relationSelector, access.relationSelector);
      EXPECT_EQ(imageAccess.unpackedFallback, access.unpackedFallback);
      EXPECT_EQ(imageAccess.packedFallback, access.packedFallback);
    } else {
      EXPECT_EQ(imageAccess.relationSelector, 0);
      EXPECT_EQ(imageAccess.unpackedFallback, access.unpackedFallback);
      EXPECT_EQ(imageAccess.packedFallback, access.packedFallback);
    }
    EXPECT_EQ(imageAccess.mapSemanticType, access.mapSemanticType);
    for (const auto &object : vpiObjectKinds) {
      if (object.aliasOf != nullptr || object.role != VPIObjectRole::Concrete)
        continue;
      EXPECT_EQ(vpiObjectModelImageTargetContains(
                    vpiObjectModelImage, imageAccess.targets, object.value),
                indexedVPIResultAllowed(access, object.value))
          << object.apiName;
    }
  }
  for (const auto &mapping : vpiIndexedTypeResults) {
    VPIObjectModelImageIndexedTypeResult imageMapping{};
    ASSERT_TRUE(findVPIObjectModelImageIndexedTypeResult(
        vpiObjectModelImage, mapping.accessKind, mapping.selectedTypespec,
        imageMapping));
    EXPECT_EQ(imageMapping.resultType, mapping.resultType);
  }
}

TEST(VPIObjectModel, IndexedAccessPoliciesExactlyMatchLrmObjectDiagrams) {
  const KindSet variableSources{
      vpiShortRealVar,       vpiRealVar,    vpiByteVar,
      vpiShortIntVar,        vpiIntVar,     vpiLongIntVar,
      vpiIntegerVar,         vpiTimeVar,    vpiRegArray,
      vpiPackedArrayVar,     vpiBitVar,     vpiReg,
      vpiStructVar,          vpiUnionVar,   vpiEnumVar,
      vpiStringVar,          vpiChandleVar, vpiClassVar,
      vpiVirtualInterfaceVar};
  const KindSet netSources{
      vpiNet,          vpiNetArray,        vpiEnumNet,
      vpiIntegerNet,   vpiTimeNet,         vpiUnionNet,
      vpiShortRealNet, vpiRealNet,         vpiByteNet,
      vpiShortIntNet,  vpiIntNet,          vpiLongIntNet,
      vpiBitNet,       vpiInterconnectNet, vpiInterconnectArray,
      vpiStructNet,    vpiPackedArrayNet};
  const std::map<uint32_t, IndexedKind> exactKinds{
      {vpiPort, IndexedKind::PortElement}};
  struct RelationPolicy {
    uint32_t partial;
    uint32_t terminal;
    uint32_t selector;
  };
  const std::map<uint32_t, RelationPolicy> relationPolicies{
      {vpiModuleArray, {vpiModuleArray, vpiModule, vpiModule}},
      {vpiInterfaceArray, {vpiInterfaceArray, vpiInterface, vpiInterface}},
      {vpiProgramArray, {vpiProgramArray, vpiProgram, vpiProgram}},
      {vpiGateArray, {vpiGateArray, vpiGate, vpiPrimitive}},
      {vpiSwitchArray, {vpiSwitchArray, vpiSwitch, vpiPrimitive}},
      {vpiUdpArray, {vpiUdpArray, vpiUdp, vpiPrimitive}},
      {vpiNamedEventArray, {vpiNamedEventArray, vpiNamedEvent, vpiNamedEvent}},
      {vpiGenScopeArray, {vpiGenScopeArray, vpiGenScope, vpiGenScope}},
  };
  KindSet variableTargets = variableSources;
  variableTargets.insert(vpiRegBit);
  KindSet netTargets = netSources;
  netTargets.insert(vpiNetBit);
  const KindSet portTargets{vpiPort, vpiPortBit};

  size_t seen = 0;
  for (const auto &access : vpiIndexedAccesses) {
    ++seen;
    const auto exact = exactKinds.find(access.sourceType);
    const auto relation = relationPolicies.find(access.sourceType);
    if (exact != exactKinds.end())
      EXPECT_EQ(access.accessKind, exact->second);
    else if (relation != relationPolicies.end())
      EXPECT_EQ(access.accessKind, IndexedKind::RelationElement);
    else if (variableSources.count(access.sourceType))
      EXPECT_EQ(access.accessKind, IndexedKind::VariableElement);
    else if (netSources.count(access.sourceType))
      EXPECT_EQ(access.accessKind, IndexedKind::NetElement);
    else
      ADD_FAILURE() << "unexpected indexed-access source "
                    << objectName(access.sourceType);
    EXPECT_EQ(findVPIIndexedAccess(access.sourceType), &access);
    EXPECT_NE(access.clause, nullptr);
    EXPECT_NE(*access.clause, '\0');
    if (relation != relationPolicies.end()) {
      EXPECT_EQ(access.terminalResult, relation->second.terminal);
      EXPECT_EQ(access.unpackedFallback, relation->second.partial);
      EXPECT_EQ(access.packedFallback, relation->second.terminal);
      EXPECT_EQ(access.relationSelector, relation->second.selector);
      EXPECT_FALSE(access.mapSemanticType);
    } else if (access.sourceType == vpiPort) {
      EXPECT_EQ(access.terminalResult, vpiPortBit);
      EXPECT_EQ(access.unpackedFallback, vpiPort);
      EXPECT_EQ(access.packedFallback, vpiPort);
      EXPECT_FALSE(access.mapSemanticType);
    } else if (access.sourceType == vpiInterconnectArray) {
      EXPECT_EQ(access.terminalResult, vpiNetBit);
      EXPECT_EQ(access.unpackedFallback, vpiInterconnectArray);
      EXPECT_EQ(access.packedFallback, vpiInterconnectNet);
      EXPECT_FALSE(access.mapSemanticType);
    } else if (netSources.count(access.sourceType)) {
      EXPECT_EQ(access.terminalResult, vpiNetBit);
      EXPECT_EQ(access.unpackedFallback, vpiNetArray);
      EXPECT_EQ(access.packedFallback, vpiNet);
      EXPECT_TRUE(access.mapSemanticType);
    } else {
      EXPECT_EQ(access.terminalResult, vpiRegBit);
      EXPECT_EQ(access.unpackedFallback, vpiRegArray);
      EXPECT_EQ(access.packedFallback, vpiReg);
      EXPECT_TRUE(access.mapSemanticType);
    }
    KindSet relationTargets;
    if (relation != relationPolicies.end())
      relationTargets = {relation->second.partial, relation->second.terminal};
    const KindSet &expectedTargets =
        relation != relationPolicies.end()              ? relationTargets
        : access.accessKind == IndexedKind::PortElement ? portTargets
        : access.accessKind == IndexedKind::NetElement  ? netTargets
                                                        : variableTargets;
    for (const auto &object : vpiObjectKinds) {
      if (object.aliasOf != nullptr || object.role != VPIObjectRole::Concrete)
        continue;
      EXPECT_EQ(indexedVPIResultAllowed(access, object.value),
                expectedTargets.count(object.value) != 0)
          << object.apiName;
    }
  }
  EXPECT_EQ(seen, variableSources.size() + netSources.size() +
                      exactKinds.size() + relationPolicies.size());
  EXPECT_EQ(findVPIIndexedAccess(vpiModule), nullptr);
  EXPECT_EQ(findVPIIndexedAccess(vpiNetBit), nullptr);
  EXPECT_EQ(findVPIIndexedAccess(vpiRegBit), nullptr);
  for (const auto &[source, policy] : relationPolicies) {
    const auto *access = findVPIIndexedAccess(source);
    ASSERT_NE(access, nullptr);
    EXPECT_EQ(access->accessKind, IndexedKind::RelationElement);
    EXPECT_EQ(access->relationSelector, policy.selector);
  }
}

TEST(VPIObjectModel, IndexedTypeResultsExactlyMatchLrmValueKinds) {
  using Key = std::pair<IndexedKind, uint32_t>;
  const std::map<Key, uint32_t> expected{
      {{IndexedKind::VariableElement, vpiLongIntTypespec}, vpiLongIntVar},
      {{IndexedKind::NetElement, vpiLongIntTypespec}, vpiLongIntNet},
      {{IndexedKind::VariableElement, vpiShortRealTypespec}, vpiShortRealVar},
      {{IndexedKind::NetElement, vpiShortRealTypespec}, vpiShortRealNet},
      {{IndexedKind::VariableElement, vpiByteTypespec}, vpiByteVar},
      {{IndexedKind::NetElement, vpiByteTypespec}, vpiByteNet},
      {{IndexedKind::VariableElement, vpiShortIntTypespec}, vpiShortIntVar},
      {{IndexedKind::NetElement, vpiShortIntTypespec}, vpiShortIntNet},
      {{IndexedKind::VariableElement, vpiIntTypespec}, vpiIntVar},
      {{IndexedKind::NetElement, vpiIntTypespec}, vpiIntNet},
      {{IndexedKind::VariableElement, vpiEnumTypespec}, vpiEnumVar},
      {{IndexedKind::NetElement, vpiEnumTypespec}, vpiEnumNet},
      {{IndexedKind::VariableElement, vpiIntegerTypespec}, vpiIntegerVar},
      {{IndexedKind::NetElement, vpiIntegerTypespec}, vpiIntegerNet},
      {{IndexedKind::VariableElement, vpiTimeTypespec}, vpiTimeVar},
      {{IndexedKind::NetElement, vpiTimeTypespec}, vpiTimeNet},
      {{IndexedKind::VariableElement, vpiRealTypespec}, vpiRealVar},
      {{IndexedKind::NetElement, vpiRealTypespec}, vpiRealNet},
      {{IndexedKind::VariableElement, vpiStructTypespec}, vpiStructVar},
      {{IndexedKind::NetElement, vpiStructTypespec}, vpiStructNet},
      {{IndexedKind::VariableElement, vpiUnionTypespec}, vpiUnionVar},
      {{IndexedKind::NetElement, vpiUnionTypespec}, vpiUnionNet},
      {{IndexedKind::VariableElement, vpiBitTypespec}, vpiBitVar},
      {{IndexedKind::NetElement, vpiBitTypespec}, vpiBitNet},
      {{IndexedKind::VariableElement, vpiLogicTypespec}, vpiReg},
      {{IndexedKind::NetElement, vpiLogicTypespec}, vpiNet},
      {{IndexedKind::VariableElement, vpiArrayTypespec}, vpiRegArray},
      {{IndexedKind::NetElement, vpiArrayTypespec}, vpiNetArray},
      {{IndexedKind::VariableElement, vpiPackedArrayTypespec},
       vpiPackedArrayVar},
      {{IndexedKind::NetElement, vpiPackedArrayTypespec}, vpiPackedArrayNet},
      {{IndexedKind::VariableElement, vpiClassTypespec}, vpiClassVar},
      {{IndexedKind::VariableElement, vpiStringTypespec}, vpiStringVar},
      {{IndexedKind::VariableElement, vpiChandleTypespec}, vpiChandleVar},
      {{IndexedKind::VariableElement, vpiInterfaceTypespec},
       vpiVirtualInterfaceVar},
  };
  ASSERT_EQ(expected.size(), kExpectedIndexedTypeResultCount);
  for (const auto &mapping : vpiIndexedTypeResults) {
    auto found = expected.find({mapping.accessKind, mapping.selectedTypespec});
    ASSERT_NE(found, expected.end());
    EXPECT_EQ(mapping.resultType, found->second);
    EXPECT_EQ(
        findVPIIndexedTypeResult(mapping.accessKind, mapping.selectedTypespec),
        &mapping);
    EXPECT_NE(mapping.clause, nullptr);
    EXPECT_NE(*mapping.clause, '\0');
  }
  EXPECT_EQ(
      findVPIIndexedTypeResult(IndexedKind::PortElement, vpiLogicTypespec),
      nullptr);
}

TEST(VPIObjectModel, IndexedValuePropertiesHaveExactLrmApplicability) {
  const KindSet valueExpected{vpiShortRealVar,
                              vpiRealVar,
                              vpiByteVar,
                              vpiShortIntVar,
                              vpiIntVar,
                              vpiLongIntVar,
                              vpiIntegerVar,
                              vpiTimeVar,
                              vpiRegArray,
                              vpiPackedArrayVar,
                              vpiBitVar,
                              vpiReg,
                              vpiStructVar,
                              vpiUnionVar,
                              vpiEnumVar,
                              vpiStringVar,
                              vpiChandleVar,
                              vpiClassVar,
                              vpiVirtualInterfaceVar,
                              vpiRegBit,
                              vpiNet,
                              vpiNetBit,
                              vpiNetArray,
                              vpiEnumNet,
                              vpiIntegerNet,
                              vpiTimeNet,
                              vpiUnionNet,
                              vpiShortRealNet,
                              vpiRealNet,
                              vpiByteNet,
                              vpiShortIntNet,
                              vpiIntNet,
                              vpiLongIntNet,
                              vpiBitNet,
                              vpiInterconnectNet,
                              vpiInterconnectArray,
                              vpiStructNet,
                              vpiPackedArrayNet};
  KindSet arrayMemberExpected = valueExpected;
  arrayMemberExpected.insert({vpiPackage, vpiModule, vpiInterface, vpiProgram,
                              vpiGate, vpiSwitch, vpiUdp, vpiNamedEvent,
                              vpiGenScope});
  const KindSet packedArrayMemberExpected{
      vpiEnumNet,  vpiStructNet, vpiPackedArrayNet, vpiStructVar,
      vpiUnionVar, vpiEnumVar,   vpiPackedArrayVar};
  KindSet constantSelectExpected = valueExpected;
  constantSelectExpected.insert({vpiRefObj, vpiParameter, vpiSpecParam,
                                 vpiVarSelect, vpiBitSelect, vpiPartSelect,
                                 vpiIndexedPartSelect});
  KindSet signedExpected = valueExpected;
  signedExpected.insert({vpiIODecl, vpiFunction, vpiParameter, vpiSpecParam,
                         vpiRefObj, vpiVarSelect, vpiBitSelect, vpiPartSelect,
                         vpiIndexedPartSelect, vpiOperation, vpiConstant,
                         vpiFuncCall, vpiMethodFuncCall, vpiSysFuncCall,
                         vpiLetExpr});
  const std::map<uint32_t, KindSet> expectedByProperty{
      {vpiArrayMember, arrayMemberExpected},
      {vpiPackedArrayMember, packedArrayMemberExpected},
      {vpiConstantSelect, constantSelectExpected},
      {vpiSigned, signedExpected},
  };
  constexpr uint32_t properties[]{vpiArrayMember, vpiPackedArrayMember,
                                  vpiConstantSelect, vpiSigned};
  for (uint32_t property : properties)
    for (const auto &object : vpiObjectKinds) {
      if (object.aliasOf != nullptr || object.role != VPIObjectRole::Concrete)
        continue;
      const KindSet &expected = expectedByProperty.at(property);
      EXPECT_EQ(findVPIProperty(object.value, property) != nullptr,
                expected.count(object.value) != 0)
          << object.apiName << " property=" << property;
    }
}

TEST(VPIObjectModel, CompactImageValidationRejectsCorruptionAndTruncation) {
  std::vector<uint8_t> damaged(std::begin(vpiObjectModelImage),
                               std::end(vpiObjectModelImage));
  auto reset = [&] {
    damaged.assign(std::begin(vpiObjectModelImage),
                   std::end(vpiObjectModelImage));
  };
  auto write16 = [&](uint32_t offset, uint16_t value) {
    damaged[offset] = static_cast<uint8_t>(value);
    damaged[offset + 1] = static_cast<uint8_t>(value >> 8);
  };
  auto write64 = [&](uint32_t offset, uint64_t value) {
    for (unsigned byte = 0; byte != 8; ++byte)
      damaged[offset + byte] = static_cast<uint8_t>(value >> (byte * 8));
  };
  damaged[0] ^= 1;
  EXPECT_FALSE(validateVPIObjectModelImage(damaged.data(), damaged.size()));
  damaged[0] ^= 1;
  damaged.back() ^= 1;
  EXPECT_FALSE(validateVPIObjectModelImage(damaged.data(), damaged.size()));
  EXPECT_FALSE(validateVPIObjectModelImage(vpiObjectModelImage,
                                           sizeof(vpiObjectModelImage) - 1));

  reset();
  uint32_t objectOffset = readVPIObjectModelImage32(damaged.data(), 24);
  damaged[objectOffset + 3] = 1;
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));
  EXPECT_FALSE(validateVPIObjectModelImage(damaged.data(), damaged.size()));

  reset();
  uint32_t setOffset = readVPIObjectModelImage32(damaged.data(), 40);
  damaged[setOffset + 2] = 0;
  damaged[setOffset + 3] = 0;
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));
  EXPECT_FALSE(validateVPIObjectModelImage(damaged.data(), damaged.size()));

  reset();
  uint32_t traversalOffset = readVPIObjectModelImage32(damaged.data(), 56);
  uint32_t traversalCount = readVPIObjectModelImage32(damaged.data(), 60);
  for (uint32_t index = 0; index != traversalCount; ++index) {
    uint8_t *record = damaged.data() + traversalOffset +
                      index * vpiObjectModelImageTraversalSize;
    if (record[6] == static_cast<uint8_t>(Mode::Handle)) {
      record[7] = static_cast<uint8_t>(Order::Source);
      break;
    }
  }
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));
  EXPECT_FALSE(validateVPIObjectModelImage(damaged.data(), damaged.size()));

  reset();
  traversalOffset = readVPIObjectModelImage32(damaged.data(), 56);
  damaged[traversalOffset + 7] |= 0x40;
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));
  EXPECT_FALSE(validateVPIObjectModelImage(damaged.data(), damaged.size()));

  reset();
  traversalOffset = readVPIObjectModelImage32(damaged.data(), 56);
  traversalCount = readVPIObjectModelImage32(damaged.data(), 60);
  for (uint32_t index = 0; index != traversalCount; ++index) {
    uint8_t *record = damaged.data() + traversalOffset +
                      index * vpiObjectModelImageTraversalSize;
    if (record[6] == static_cast<uint8_t>(Mode::Iterate) &&
        (record[7] & vpiObjectModelImageAutomaticRelationMask) != 0) {
      record[7] = static_cast<uint8_t>(
          (record[7] & ~vpiObjectModelImageAutomaticRelationMask) |
          (static_cast<uint8_t>(VPIAutomaticRelation::DirectPortConnection)
           << vpiObjectModelImageAutomaticRelationShift));
      break;
    }
  }
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));
  EXPECT_FALSE(validateVPIObjectModelImage(damaged.data(), damaged.size()));

  reset();
  objectOffset = readVPIObjectModelImage32(damaged.data(), 24);
  uint32_t objectCount = readVPIObjectModelImage32(damaged.data(), 28);
  setOffset = readVPIObjectModelImage32(damaged.data(), 40);
  uint32_t kindOffset = readVPIObjectModelImage32(damaged.data(), 48);
  traversalOffset = readVPIObjectModelImage32(damaged.data(), 56);
  traversalCount = readVPIObjectModelImage32(damaged.data(), 60);
  bool damagedIndexedContainer = false;
  for (uint32_t index = 0; index != traversalCount; ++index) {
    const uint8_t *traversal = damaged.data() + traversalOffset +
                               index * vpiObjectModelImageTraversalSize;
    uint8_t automatic =
        (traversal[7] & vpiObjectModelImageAutomaticRelationMask) >>
        vpiObjectModelImageAutomaticRelationShift;
    if (automatic !=
        static_cast<uint8_t>(VPIAutomaticRelation::IndexedContainer))
      continue;
    uint16_t setIndex = readVPIObjectModelImage16(traversal, 4);
    const uint8_t *set = damaged.data() + setOffset +
                         uint32_t{setIndex} * vpiObjectModelImageSetSize;
    uint16_t first = readVPIObjectModelImage16(set, 0);
    uint16_t targetKind = readVPIObjectModelImage16(
        damaged.data(), kindOffset + uint32_t{first} * 2);
    for (uint32_t objectIndex = 0; objectIndex != objectCount; ++objectIndex) {
      uint32_t offset =
          objectOffset + objectIndex * vpiObjectModelImageObjectSize;
      if (readVPIObjectModelImage16(damaged.data(), offset) != targetKind)
        continue;
      write64(offset + 4, vpiFamilyMask(VPIObjectFamily::Scope));
      damagedIndexedContainer = true;
      break;
    }
    break;
  }
  ASSERT_TRUE(damagedIndexedContainer);
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));
  EXPECT_FALSE(validateVPIObjectModelImage(damaged.data(), damaged.size()));

  reset();
  uint32_t setCount = readVPIObjectModelImage32(damaged.data(), 44);
  kindOffset = readVPIObjectModelImage32(damaged.data(), 48);
  for (uint32_t index = 0; index != setCount; ++index) {
    const uint8_t *record =
        damaged.data() + setOffset + index * vpiObjectModelImageSetSize;
    uint16_t first = readVPIObjectModelImage16(record, 0);
    uint16_t count = readVPIObjectModelImage16(record, 2);
    if (count < 2)
      continue;
    uint16_t left =
        readVPIObjectModelImage16(damaged.data(), kindOffset + first * 2);
    uint16_t right = readVPIObjectModelImage16(
        damaged.data(), kindOffset + (uint32_t{first} + 1) * 2);
    write16(kindOffset + first * 2, right);
    write16(kindOffset + (uint32_t{first} + 1) * 2, left);
    break;
  }
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  reset();
  write16(traversalOffset + 4, static_cast<uint16_t>(setCount));
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  reset();
  damaged[traversalOffset + 6] = 2;
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  reset();
  write16(traversalOffset, UINT16_MAX);
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  reset();
  uint32_t relationOffset = readVPIObjectModelImage32(damaged.data(), 32);
  damaged[relationOffset + 2] = 3;
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  reset();
  uint32_t propertyOffset = readVPIObjectModelImage32(damaged.data(), 64);
  damaged[propertyOffset + 4] = 4;
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  reset();
  damaged[propertyOffset + 5] = 8;
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  reset();
  write16(propertyOffset + vpiObjectModelImagePropertySize + 2,
          readVPIObjectModelImage16(damaged.data(), propertyOffset + 2));
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  uint32_t valuePolicyOffset = readVPIObjectModelImage32(damaged.data(), 72);
  reset();
  damaged[valuePolicyOffset + 4] =
      static_cast<uint8_t>(VPIValueDefaultFormat::Time) + 1;
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  reset();
  damaged[valuePolicyOffset + 5] =
      static_cast<uint8_t>(VPIValueReadSemantics::Evaluate) + 1;
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  reset();
  write16(valuePolicyOffset + 2, 1);
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  reset();
  damaged[valuePolicyOffset + 6] = 0x80;
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  reset();
  damaged[valuePolicyOffset + 7] = 1;
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  reset();
  write16(valuePolicyOffset + vpiObjectModelImageValuePolicySize,
          readVPIObjectModelImage16(damaged.data(), valuePolicyOffset));
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  uint32_t arrayValuePolicyOffset =
      readVPIObjectModelImage32(damaged.data(), 96);
  reset();
  write16(arrayValuePolicyOffset, vpiStringVal);
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  reset();
  write16(arrayValuePolicyOffset + 2, vpiRealVar);
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  reset();
  write16(arrayValuePolicyOffset + vpiObjectModelImageArrayValuePolicySize,
          readVPIObjectModelImage16(damaged.data(), arrayValuePolicyOffset));
  write16(
      arrayValuePolicyOffset + vpiObjectModelImageArrayValuePolicySize + 2,
      readVPIObjectModelImage16(damaged.data(), arrayValuePolicyOffset + 2));
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  uint32_t indexedAccessOffset = readVPIObjectModelImage32(damaged.data(), 80);
  reset();
  damaged[indexedAccessOffset + 4] =
      static_cast<uint8_t>(IndexedKind::RelationElement) + 1;
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  reset();
  damaged[indexedAccessOffset + 5] = 2;
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  reset();
  write16(indexedAccessOffset + 6, 0);
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  // A relation-backed indexed access must terminate at the member kind, not
  // at the array fallback kind. This is independently correlated with the
  // canonical relation traversal by the compact-image validator.
  reset();
  uint32_t indexedAccessCount = readVPIObjectModelImage32(damaged.data(), 84);
  bool damagedRelationTerminal = false;
  for (uint32_t index = 0; index != indexedAccessCount; ++index) {
    uint32_t offset =
        indexedAccessOffset + index * vpiObjectModelImageIndexedAccessSize;
    if (damaged[offset + 4] !=
        static_cast<uint8_t>(IndexedKind::RelationElement))
      continue;
    write16(offset + 6, readVPIObjectModelImage16(damaged.data(), offset + 8));
    damagedRelationTerminal = true;
    break;
  }
  ASSERT_TRUE(damagedRelationTerminal);
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  uint32_t indexedTypeResultOffset =
      readVPIObjectModelImage32(damaged.data(), 88);
  reset();
  damaged[indexedTypeResultOffset] =
      static_cast<uint8_t>(IndexedKind::RelationElement) + 1;
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  reset();
  damaged[indexedTypeResultOffset + 1] = 1;
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  reset();
  write16(indexedTypeResultOffset + 4, 0);
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  reset();
  write16(indexedTypeResultOffset + 2, vpiTypespec);
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  reset();
  write16(indexedAccessOffset + 2, static_cast<uint16_t>(setCount));
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  reset();
  write16(indexedAccessOffset + vpiObjectModelImageIndexedAccessSize,
          readVPIObjectModelImage16(damaged.data(), indexedAccessOffset));
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  // A structurally valid but different model is rejected by the generated
  // canonical fingerprint, keeping compiler and runtime schemas in lockstep.
  reset();
  damaged[objectOffset + 4] ^= 0x80;
  refreshImageChecksum(damaged);
  EXPECT_TRUE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));
  EXPECT_FALSE(validateVPIObjectModelImage(damaged.data(), damaged.size()));
}

TEST(VPIObjectModel, TraversalDescriptorsAreSortedUniqueAndSearchable) {
  const VPITraversalDescriptor *previous = nullptr;
  for (const auto &edge : vpiTraversals) {
    SCOPED_TRACE(keyName(edge.sourceType, edge.selector, edge.mode));
    EXPECT_EQ(findVPITraversal(edge.sourceType, edge.selector, edge.mode),
              &edge);
    if (edge.sourceType != 0) {
      EXPECT_NE(findVPIObjectKind(edge.sourceType), nullptr);
    }
    EXPECT_TRUE(findVPIObjectSelector(edge.selector) != nullptr ||
                findVPIRelation(edge.selector) != nullptr);
    if (previous) {
      bool strictlyOrdered = previous->sourceType < edge.sourceType ||
                             (previous->sourceType == edge.sourceType &&
                              (previous->selector < edge.selector ||
                               (previous->selector == edge.selector &&
                                static_cast<uint8_t>(previous->mode) <
                                    static_cast<uint8_t>(edge.mode))));
      EXPECT_TRUE(strictlyOrdered) << "duplicate or unsorted traversal key";
    }
    previous = &edge;
  }
}

TEST(VPIObjectModel, EveryTraversalHasConcreteNonemptyTargets) {
  for (const auto &edge : vpiTraversals) {
    SCOPED_TRACE(keyName(edge.sourceType, edge.selector, edge.mode));
    EXPECT_FALSE(expandedTargets(edge.targets).empty());
    EXPECT_NE(edge.selectorName, nullptr);
    EXPECT_NE(edge.clause, nullptr);
    EXPECT_NE(edge.clause[0], '\0');
  }
}

// The unqualified "expr" enclosure recurs throughout 37.5-37.83.  Every one
// of these independently listed arcs accepts nets and variables as ordinary
// expressions, while assertion, pattern, and constraint nodes belong only to
// diagrams that name those nodes explicitly.
TEST(VPIObjectModel, OrdinaryExpressionArcsUseTheExactLrmUnion) {
  struct Key {
    uint32_t source;
    uint32_t selector;
    Mode mode;
  };
  constexpr Key keys[] = {
      {vpiModule, vpiDefaultDisableIff, Mode::Handle},
      {vpiInterface, vpiDefaultDisableIff, Mode::Handle},
      {vpiProgram, vpiDefaultDisableIff, Mode::Handle},
      {vpiGateArray, vpiLeftRange, Mode::Handle},
      {vpiGateArray, vpiRightRange, Mode::Handle},
      {vpiGateArray, vpiDelay, Mode::Handle},
      {vpiPort, vpiHighConn, Mode::Handle},
      {vpiPort, vpiLowConn, Mode::Handle},
      {vpiNet, vpiLeftRange, Mode::Handle},
      {vpiNet, vpiRightRange, Mode::Handle},
      {vpiNetBit, vpiIndex, Mode::Handle},
      {vpiNetBit, vpiIndex, Mode::Iterate},
      {vpiReg, vpiExpr, Mode::Handle},
      {vpiReg, vpiLeftRange, Mode::Handle},
      {vpiReg, vpiRightRange, Mode::Handle},
      {vpiVarSelect, vpiIndex, Mode::Handle},
      {vpiVarSelect, vpiIndex, Mode::Iterate},
      {vpiRange, vpiLeftRange, Mode::Handle},
      {vpiRange, vpiRightRange, Mode::Handle},
      {vpiParameter, vpiExpr, Mode::Handle},
      {vpiExtends, vpiArgument, Mode::Iterate},
      {vpiClassObj, vpiMessages, Mode::Iterate},
      {vpiConstraintOrdering, vpiSolveBefore, Mode::Iterate},
      {vpiConstraintOrdering, vpiSolveAfter, Mode::Iterate},
      {vpiDistribution, vpiExpr, Mode::Handle},
      {vpiDistItem, vpiWeight, Mode::Handle},
      {vpiGate, vpiIndex, Mode::Handle},
      {vpiGate, vpiDelay, Mode::Handle},
      {vpiModPath, vpiCondition, Mode::Handle},
      {vpiModPath, vpiDelay, Mode::Handle},
      {vpiTchk, vpiDelay, Mode::Handle},
      {vpiTchkTerm, vpiCondition, Mode::Handle},
      {vpiFunction, vpiLeftRange, Mode::Handle},
      {vpiFunction, vpiRightRange, Mode::Handle},
      {vpiContAssign, vpiLhs, Mode::Handle},
      {vpiContAssign, vpiRhs, Mode::Handle},
      {vpiAssert, vpiClockingEvent, Mode::Handle},
      {vpiPropertyInst, vpiDisableCondition, Mode::Handle},
      {vpiPropertySpec, vpiClockingEvent, Mode::Handle},
      {vpiCaseProperty, vpiCondition, Mode::Handle},
      {vpiCasePropertyItem, vpiExpr, Mode::Iterate},
      {vpiImmediateAssert, vpiExpr, Mode::Handle},
      {vpiLetDecl, vpiExpr, Mode::Handle},
      {vpiLetExpr, vpiArgument, Mode::Iterate},
      {vpiIndexedPartSelect, vpiBaseExpr, Mode::Handle},
      {vpiIndexedPartSelect, vpiWidthExpr, Mode::Handle},
      {vpiPartSelect, vpiLeftRange, Mode::Handle},
      {vpiPartSelect, vpiRightRange, Mode::Handle},
      {vpiAssignment, vpiRhs, Mode::Handle},
      {vpiWhile, vpiCondition, Mode::Handle},
      {vpiWait, vpiCondition, Mode::Handle},
      {vpiDelayControl, vpiDelay, Mode::Handle},
      {vpiRepeatControl, vpiExpr, Mode::Handle},
      {vpiIf, vpiCondition, Mode::Handle},
      {vpiCase, vpiCondition, Mode::Handle},
      {vpiCaseItem, vpiExpr, Mode::Iterate},
      {vpiFor, vpiCondition, Mode::Handle},
      {vpiReturnStmt, vpiCondition, Mode::Handle},
      {vpiDoWhile, vpiCondition, Mode::Handle},
      {vpiAssignStmt, vpiLhs, Mode::Handle},
      {vpiAssignStmt, vpiRhs, Mode::Handle},
      {vpiAliasStmt, vpiLhs, Mode::Handle},
      {vpiAliasStmt, vpiRhs, Mode::Handle},
      {vpiGenScope, vpiIndex, Mode::Handle},
  };
  for (const auto &key : keys) {
    SCOPED_TRACE(keyName(key.source, key.selector, key.mode));
    expectOrdinaryExpressionTargets(key.source, key.selector, key.mode);
  }
}

// IEEE 1800-2017 37.5, 37.40, 37.41, and 37.79-37.80.  Root traversals are
// unusually easy to over-broaden because their reference object is NULL.
TEST(VPIObjectModel, RootTraversalsHaveExactSpecialTargetsAndOrders) {
  expectTargetsExactly(0, vpiModule, Mode::Iterate, {vpiModule});
  expectTargetsExactly(0, vpiSysTfCall, Mode::Handle,
                       {vpiSysFuncCall, vpiSysTaskCall});
  expectTargetsExactly(0, vpiFrame, Mode::Handle, {vpiFrame});
  expectTargetsExactly(0, vpiActiveTimeFormat, Mode::Handle, {vpiSysTaskCall});
  EXPECT_EQ(requireTraversal(0, vpiTimeQueue, Mode::Iterate).order,
            Order::Time);
  expectAbsent(0, vpiActiveTimeFormat, Mode::Iterate);
  expectAbsent(0, vpiSysTfCall, Mode::Iterate);
}

// 37.6-37.12 distinguish immediate instance, lexical scope, and array-parent
// relationships.  These negative cases prevent a generic Scope/Array family
// from leaking into a relation with a narrower LRM node.
TEST(VPIObjectModel, InstanceScopeAndArrayRelationsDoNotLeak) {
  expectTargetsExactly(vpiModport, vpiInterface, Mode::Handle, {vpiInterface});
  expectTargetsExactly(vpiInterfaceTfDecl, vpiTask, Mode::Iterate, {vpiTask});
  expectTargetsExactly(vpiInterfaceTfDecl, vpiFunction, Mode::Iterate,
                       {vpiFunction});
  expectTargetsExactly(vpiModule, vpiInstance, Mode::Handle, {vpiModule});
  expectTargetsExactly(vpiNamedEvent, vpiParent, Mode::Handle,
                       {vpiNamedEventArray});
  expectAbsent(vpiModuleArray, vpiIndex, Mode::Handle);
  expectAbsent(vpiPrimitiveArray, vpiIndex, Mode::Handle);
  expectAbsent(vpiPortBit, vpiBit, Mode::Iterate);
}

// Cross-checks for dashed enclosures that span multiple concrete object
// kinds.  These are transcribed from 37.10, 37.12, 37.15-37.16, 37.26,
// 37.29, 37.32, 37.36-37.37, and 37.41.
TEST(VPIObjectModel, EarlyDiagramEnclosuresExpandWithoutFamilyLeakage) {
  expectContains(vpiModule, vpiParameter, Mode::Iterate,
                 {vpiParameter, vpiTypeParameter}, {vpiSpecParam});
  EXPECT_NE(findVPITraversal(vpiTypeParameter, vpiScope, Mode::Handle),
            nullptr);
  expectContains(vpiAssignment, vpiScope, Mode::Handle,
                 {vpiModule, vpiTask, vpiFunction, vpiClassObj});
  expectContains(vpiFrame, vpiOrigin, Mode::Handle,
                 {vpiModule, vpiTask, vpiFunction, vpiClassObj});
  expectContains(vpiRefObj, vpiActual, Mode::Handle,
                 {vpiNetArray, vpiInterconnectArray});

  EXPECT_NE(findVPITraversal(vpiNet, vpiBit, Mode::Iterate), nullptr);
  expectAbsent(vpiNetBit, vpiBit, Mode::Iterate);
  expectAbsent(vpiNetArray, vpiBit, Mode::Iterate);
  expectAbsent(vpiInterconnectArray, vpiBit, Mode::Iterate);

  expectContains(vpiConstraint, vpiConstraintItem, Mode::Iterate,
                 {vpiConstant, vpiOperation, vpiRefObj, vpiConstrIf,
                  vpiConstraintOrdering},
                 {vpiNamedEvent, vpiNamedEventArray, vpiVirtualInterfaceVar});
  expectTargetsExactly(vpiModPath, vpiInstance, Mode::Handle,
                       {vpiModule, vpiInterface});

  EXPECT_EQ(
      requireTraversal(vpiClassDefn, vpiDerivedClasses, Mode::Iterate).order,
      Order::None);
  EXPECT_EQ(requireTraversal(vpiClassDefn, vpiInstance, Mode::Iterate).order,
            Order::None);
  EXPECT_EQ(requireTraversal(vpiFrame, vpiAutomatics, Mode::Iterate).order,
            Order::None);
}

// 37.19-37.28 defines typespec, parameter, enum, array, and virtual-interface
// relations.  In particular, a virtual interface cannot accept an arbitrary
// concrete typespec merely because both are declarations.
TEST(VPIObjectModel, TypeRelationsPreserveLrmNarrowing) {
  expectTargetsExactly(vpiVirtualInterfaceVar, vpiTypespec, Mode::Handle,
                       {vpiInterfaceTypespec});
  expectContains(vpiVirtualInterfaceVar, vpiExpr, Mode::Handle,
                 {vpiInterface, vpiModport, vpiVirtualInterfaceVar, vpiRefObj},
                 {vpiIntTypespec, vpiNamedEvent, vpiNamedEventArray});
  expectTargetsExactly(vpiNamedEvent, vpiParent, Mode::Handle,
                       {vpiNamedEventArray});
  expectAbsent(vpiPackedArrayVar, vpiVarSelect, Mode::Iterate);
  expectTargetsExactly(vpiRegArray, vpiVarSelect, Mode::Iterate,
                       {vpiVarSelect});
}

// 37.14-37.17.  Connection relations have deliberately different target
// nodes: low connection is a reference object, high connection is an
// expression, and ref-object actuals include declared objects but not
// declaration metadata.
TEST(VPIObjectModel, PortsReferencesNetsAndVariablesKeepConnectionKinds) {
  expectContains(vpiPort, vpiLowConn, Mode::Handle,
                 {vpiConstant, vpiOperation, vpiRefObj},
                 {vpiPropertyExpr, vpiNamedEvent, vpiTypespec});
  expectContains(vpiPort, vpiHighConn, Mode::Handle,
                 {vpiConstant, vpiOperation, vpiRefObj},
                 {vpiPropertyExpr, vpiNamedEvent, vpiTypespec});
  expectContains(vpiRefObj, vpiActual, Mode::Handle,
                 {vpiInterface, vpiInterfaceArray, vpiModport, vpiNet, vpiReg,
                  vpiNamedEvent, vpiNamedEventArray, vpiPartSelect},
                 {vpiTypespec, vpiPropertyExpr, vpiConstraint});
  expectContains(vpiNet, vpiDriver, Mode::Iterate,
                 {vpiPort, vpiForce, vpiDelayTerm, vpiContAssign, vpiPrimTerm},
                 {vpiAssignStmt, vpiAssignment});
  expectContains(vpiReg, vpiDriver, Mode::Iterate,
                 {vpiPort, vpiForce, vpiContAssign, vpiAssignStmt},
                 {vpiPrimTerm, vpiDelayTerm, vpiAssignment});
}

// 37.29-37.36.  Constraints use expression nodes, never constraint-item
// declarations, and only UDP instances have a UDP definition transition.
TEST(VPIObjectModel, ClassesConstraintsAndPrimitivesUseExactNodeKinds) {
  expectContains(vpiConstrIfElse, vpiConstraintExpr, Mode::Iterate,
                 {vpiConstant, vpiOperation, vpiRefObj},
                 {vpiConstraintOrdering, vpiConstraint, vpiNamedEvent});
  expectAbsent(vpiGate, vpiUdpDefn, Mode::Handle);
  expectAbsent(vpiSwitch, vpiUdpDefn, Mode::Handle);
  expectTargetsExactly(vpiUdp, vpiUdpDefn, Mode::Handle, {vpiUdpDefn});
  expectTargetsExactly(vpiGateArray, vpiPrimitive, Mode::Iterate, {vpiGate});
  expectTargetsExactly(vpiSwitchArray, vpiPrimitive, Mode::Iterate,
                       {vpiSwitch});
  expectTargetsExactly(vpiUdpArray, vpiPrimitive, Mode::Iterate, {vpiUdp});
}

// 37.48: restrict has no action statement.  Assert alone has a fail action;
// the disable condition belongs to the referenced property object rather than
// directly to each concurrent assertion statement.
TEST(VPIObjectModel, ConcurrentAssertionEdgesMatchEachStatementKind) {
  expectAbsent(vpiRestrict, vpiStmt, Mode::Handle);
  expectAbsent(vpiCover, vpiElseStmt, Mode::Handle);
  expectAbsent(vpiAssume, vpiElseStmt, Mode::Handle);
  expectAbsent(vpiRestrict, vpiElseStmt, Mode::Handle);
  for (uint32_t source : {vpiAssert, vpiAssume, vpiCover})
    EXPECT_NE(findVPITraversal(source, vpiStmt, Mode::Handle), nullptr);
  EXPECT_NE(findVPITraversal(vpiAssert, vpiElseStmt, Mode::Handle), nullptr);
  for (uint32_t source : {vpiAssert, vpiAssume, vpiCover, vpiRestrict})
    expectAbsent(source, vpiDisableCondition, Mode::Handle);
}

// 37.40-37.46.  Calls, runtime frames, continuous assignments, and clocking
// declarations are separate diagrams even though they share many expression
// targets.  These checks keep their object-specific reverse edges intact.
TEST(VPIObjectModel, CallsFramesAssignmentsAndClockingKeepSpecificEdges) {
  expectTargetsExactly(vpiTaskCall, vpiTask, Mode::Handle, {vpiTask});
  expectTargetsExactly(vpiFuncCall, vpiFunction, Mode::Handle, {vpiFunction});
  expectContains(vpiFrame, vpiParent, Mode::Handle,
                 {vpiModule, vpiTask, vpiFunction, vpiTaskCall, vpiFuncCall,
                  vpiFrame, vpiMethodTaskCall, vpiMethodFuncCall},
                 {vpiThread});
  expectTargetsExactly(vpiThread, vpiParent, Mode::Handle, {vpiThread});
  expectTargetsExactly(vpiThread, vpiFrame, Mode::Handle, {vpiFrame});
  expectContains(vpiContAssign, vpiLhs, Mode::Handle,
                 {vpiConstant, vpiOperation, vpiRefObj},
                 {vpiAssignStmt, vpiPropertyExpr, vpiNamedEvent});
  expectTargetsExactly(vpiClockingBlock, vpiPrefix, Mode::Handle,
                       {vpiVirtualInterfaceVar});
  expectTargetsExactly(vpiClockingBlock, vpiActual, Mode::Handle,
                       {vpiClockingBlock});
}

// 37.40-37.41.  Task/function arguments use a broader explicit union than an
// ordinary expression, but "primitive" means concrete gate/switch/UDP
// objects, never terminals or primitive arrays.  Automatic frame state also
// includes virtual-interface variables.
TEST(VPIObjectModel, CallArgumentsAndFrameAutomaticsUseExactUnions) {
  for (uint32_t source : {vpiTaskCall, vpiFuncCall, vpiMethodTaskCall,
                          vpiMethodFuncCall, vpiSysTaskCall, vpiSysFuncCall}) {
    SCOPED_TRACE(objectName(source));
    expectContains(source, vpiArgument, Mode::Iterate,
                   {vpiNet, vpiReg, vpiConstant, vpiOperation, vpiInterface,
                    vpiModport, vpiModule, vpiTask, vpiClassObj, vpiGate,
                    vpiSwitch, vpiUdp, vpiNamedEvent, vpiNamedEventArray},
                   {vpiPrimTerm, vpiGateArray, vpiSwitchArray, vpiUdpArray,
                    vpiPrimitiveArray, vpiPropertyExpr, vpiPropertyInst,
                    vpiSequenceInst, vpiConstraint});
  }
  expectContains(vpiFrame, vpiAutomatics, Mode::Iterate,
                 {vpiVirtualInterfaceVar, vpiClassVar, vpiStringVar,
                  vpiNamedEvent, vpiNamedEventArray},
                 {vpiParameter, vpiSpecParam});
}

// 37.26, 37.32, 37.36, 37.38, 37.40, and 37.46 each augment an ordinary
// expression with one diagram-specific node.  None admits arbitrary members
// of ExpressionFamily.
TEST(VPIObjectModel, SpecialExpressionRelationsAddOnlyTheirNamedNodes) {
  expectContains(vpiParamAssign, vpiRhs, Mode::Handle,
                 {vpiNet, vpiReg, vpiConstant, vpiOperation, vpiIntTypespec},
                 {vpiAnyPattern, vpiPropertyExpr, vpiSequenceInst,
                  vpiConstraint, vpiNamedEvent});
  expectContains(vpiDistItem, vpiValueRange, Mode::Handle,
                 {vpiRange, vpiNet, vpiReg, vpiConstant, vpiOperation},
                 {vpiAnyPattern, vpiPropertyExpr, vpiSequenceInst,
                  vpiConstraint, vpiDistribution});
  expectContains(
      vpiTchk, vpiExpr, Mode::Iterate,
      {vpiTchkTerm, vpiNet, vpiReg, vpiConstant, vpiOperation},
      {vpiAnyPattern, vpiPropertyExpr, vpiSequenceInst, vpiConstraint});
  expectContains(
      vpiMethodFuncCall, vpiWith, Mode::Handle,
      {vpiConstraint, vpiNet, vpiReg, vpiConstant, vpiOperation},
      {vpiAnyPattern, vpiPropertyExpr, vpiSequenceInst, vpiConstraintOrdering});
  expectOrdinaryExpressionTargets(vpiClockingIODecl, vpiExpr, Mode::Handle);

  expectContains(vpiConstrForEach, vpiLoopVars, Mode::Iterate,
                 {vpiIntegerVar, vpiReg, vpiLongIntVar, vpiShortIntVar,
                  vpiIntVar, vpiByteVar, vpiEnumVar, vpiBitVar, vpiStringVar,
                  vpiClassVar, vpiOperation},
                 {vpiRealVar, vpiShortRealVar, vpiNamedEvent});
}

TEST(VPIObjectModel, ClassClassObjectAndClockingIterationsDoNotInventOrder) {
  for (uint32_t selector : {vpiDerivedClasses, vpiInstance, vpiMethods})
    expectNoOrder(vpiClassDefn, selector, Mode::Iterate);
  EXPECT_EQ(requireTraversal(vpiClassDefn, vpiConstraint, Mode::Iterate).order,
            Order::Declaration);
  for (uint32_t selector : {vpiMethods, vpiConstraint})
    expectNoOrder(vpiClassTypespec, selector, Mode::Iterate);
  for (uint32_t selector : {vpiVariables, vpiWaitingProcesses, vpiMessages,
                            vpiMethods, vpiConstraint, vpiParameter})
    expectNoOrder(vpiClassObj, selector, Mode::Iterate);
  expectNoOrder(vpiClockingBlock, vpiClockingIODecl, Mode::Iterate);
}

// 37.49-37.50: property arguments and formals admit named events, but not
// named-event arrays; disable conditions are ordinary expressions (plus a
// distribution on property spec), not sequence-instance nodes.
TEST(VPIObjectModel, PropertyRelationsUsePropertySpecificExpressionUnions) {
  expectContains(
      vpiPropertyInst, vpiArgument, Mode::Iterate,
      {vpiNamedEvent, vpiPropertyExpr, vpiSequenceInst, vpiDistribution},
      {vpiNamedEventArray, vpiVirtualInterfaceVar});
  expectContains(
      vpiPropFormalDecl, vpiExpr, Mode::Handle,
      {vpiNamedEvent, vpiPropertyExpr, vpiSequenceInst, vpiDistribution},
      {vpiNamedEventArray, vpiVirtualInterfaceVar});
  expectContains(vpiPropertyInst, vpiDisableCondition, Mode::Handle,
                 {vpiConstant, vpiOperation},
                 {vpiSequenceInst, vpiDistribution, vpiNamedEvent});
  expectContains(vpiPropertySpec, vpiDisableCondition, Mode::Handle,
                 {vpiConstant, vpiOperation, vpiDistribution},
                 {vpiSequenceInst, vpiNamedEvent});
}

// 37.51-37.53: a sequence actual/formal may be a named event; a match item
// may hang off any complete sequence expression, including an ordinary
// expression, but never off a property expression.
TEST(VPIObjectModel, SequenceRelationsUseCompleteSequenceExpressionUnion) {
  expectContains(vpiSequenceInst, vpiArgument, Mode::Iterate,
                 {vpiNamedEvent, vpiConstant, vpiOperation, vpiSequenceInst,
                  vpiDistribution},
                 {vpiNamedEventArray, vpiVirtualInterfaceVar, vpiPropertyExpr});
  expectContains(vpiSeqFormalDecl, vpiExpr, Mode::Handle,
                 {vpiNamedEvent, vpiConstant, vpiOperation, vpiSequenceInst,
                  vpiDistribution},
                 {vpiNamedEventArray, vpiVirtualInterfaceVar, vpiPropertyExpr});
  for (uint32_t source :
       {vpiConstant, vpiOperation, vpiRefObj, vpiSequenceInst, vpiDistribution})
    EXPECT_NE(findVPITraversal(source, vpiMatchItem, Mode::Iterate), nullptr)
        << objectName(source);
  expectAbsent(vpiPropertyExpr, vpiMatchItem, Mode::Iterate);
}

// 37.54-37.57 and 37.60.  Immediate assertions are statements, while event
// statements point to a named-event object.  Expression operands may include
// interface expressions and assertion expressions, but a use traversal is
// only available on simple expressions.
TEST(VPIObjectModel, ImmediateAssertionsAndExpressionNodesStayDistinct) {
  for (uint32_t source :
       {vpiImmediateAssert, vpiImmediateAssume, vpiImmediateCover}) {
    EXPECT_NE(findVPITraversal(source, vpiExpr, Mode::Handle), nullptr);
    EXPECT_NE(findVPITraversal(source, vpiStmt, Mode::Handle), nullptr);
  }
  expectAbsent(vpiImmediateCover, vpiElseStmt, Mode::Handle);
  expectTargetsExactly(vpiEventStmt, vpiNamedEvent, Mode::Handle,
                       {vpiNamedEvent});
  expectContains(vpiOperation, vpiOperand, Mode::Iterate,
                 {vpiConstant, vpiOperation, vpiInterface, vpiModport,
                  vpiSequenceInst, vpiPropertyInst},
                 {vpiNamedEvent, vpiNamedEventArray});
  EXPECT_NE(findVPITraversal(vpiRefObj, vpiUse, Mode::Iterate), nullptr);
  expectAbsent(vpiOperation, vpiUse, Mode::Iterate);
}

// 37.40 and 37.59.  Method receivers admit ordinary expression objects, while
// dynamic prefixing adds class variables, virtual interfaces, and clocking
// blocks.  Named events are source objects in 37.59, not receiver targets.
TEST(VPIObjectModel, ExpressionPrefixAndTypespecExceptionsAreExact) {
  expectContains(vpiMethodFuncCall, vpiPrefix, Mode::Handle,
                 {vpiClockingBlock, vpiClassVar, vpiVirtualInterfaceVar,
                  vpiRefObj, vpiConstant, vpiOperation},
                 {vpiInterface, vpiModport, vpiNamedEvent, vpiNamedEventArray});
  for (uint32_t source : {vpiRefObj, vpiVarSelect, vpiBitSelect, vpiPartSelect,
                          vpiIndexedPartSelect})
    EXPECT_NE(findVPITraversal(source, vpiTypespec, Mode::Handle), nullptr)
        << objectName(source);
}

// 37.61-37.77 statement diagrams.  These assertions cover distinctions that
// broad Statement/Expression family sets tend to erase.
TEST(VPIObjectModel, StatementRelationsKeepHandleAndIterationSemantics) {
  expectAbsent(vpiOrderedWait, vpiCondition, Mode::Handle);
  EXPECT_NE(findVPITraversal(vpiOrderedWait, vpiCondition, Mode::Iterate),
            nullptr);
  EXPECT_NE(findVPITraversal(vpiFor, vpiForInitStmt, Mode::Handle), nullptr);
  EXPECT_NE(findVPITraversal(vpiFor, vpiForInitStmt, Mode::Iterate), nullptr);
  EXPECT_NE(findVPITraversal(vpiFor, vpiForIncStmt, Mode::Handle), nullptr);
  EXPECT_NE(findVPITraversal(vpiFor, vpiForIncStmt, Mode::Iterate), nullptr);
  expectTargetsExactly(vpiForeachStmt, vpiVariables, Mode::Iterate,
                       {vpiRegArray, vpiPackedArrayVar, vpiStringVar});
  expectContains(vpiForeachStmt, vpiLoopVars, Mode::Iterate,
                 {vpiIntegerVar, vpiReg, vpiLongIntVar, vpiShortIntVar,
                  vpiIntVar, vpiByteVar, vpiEnumVar, vpiBitVar, vpiStringVar,
                  vpiClassVar, vpiOperation},
                 {vpiRealVar, vpiShortRealVar, vpiNamedEvent});
  expectTargetsExactly(vpiAliasStmt, vpiInstance, Mode::Handle,
                       {vpiModule, vpiInterface, vpiProgram, vpiPackage});
}

// 37.78-37.83: callbacks and attributes are associated only through the
// diagram's source objects.  They must not become generic transitions from a
// relation-only selector or the NULL root beyond the explicitly listed root
// callback edge.
TEST(VPIObjectModel, RuntimeAndMetadataRelationsDoNotBecomeUniversal) {
  EXPECT_NE(findVPITraversal(0, vpiCallback, Mode::Iterate), nullptr);
  expectAbsent(vpiReturn, vpiCallback, Mode::Iterate);
  expectAbsent(vpiTypespec, vpiCallback, Mode::Iterate);
  expectAbsent(vpiReturn, vpiAttribute, Mode::Iterate);
  expectAbsent(0, vpiAttribute, Mode::Iterate);
  expectAbsent(vpiIterator, vpiUse, Mode::Iterate);
  EXPECT_NE(findVPITraversal(vpiIterator, vpiUse, Mode::Handle), nullptr);
}

} // namespace
