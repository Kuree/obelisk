//===- Prepare.cpp - Freeze semantic IR into isolated simulation units --===//
//
// Flattens the elaborated design into numeric descriptors and creates one
// isolated `obelisk_sim.func` shell per code unit, with every non-local
// resource it needs bound to an explicit entry argument. Everything that
// requires whole-design knowledge happens here, so the per-unit passes that
// follow can run concurrently.
//
//===----------------------------------------------------------------------===//

#include "Detail.h"
#include "PrepareCaptures.h"
#include "PrepareDeclarations.h"
#include "PrepareNetTopology.h"
#include "PrepareTopology.h"
#include "PrepareUnits.h"
#include "PrepareValidation.h"

#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Runtime/StableHash.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/FormatVariadic.h"

#include <functional>
#include <limits>
#include <map>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMPREPAREPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

using namespace obelisk::simlowering;

static std::optional<uint64_t> getUnsigned64(IntegerAttr attribute) {
  if (!attribute || attribute.getValue().isNegative() ||
      attribute.getValue().getActiveBits() > 64)
    return std::nullopt;
  return attribute.getValue().getZExtValue();
}

static uint32_t getStableImportID(StringRef cIdentifier) {
  uint64_t hash = obelisk_stable_hash(cIdentifier.data(), cIdentifier.size());
  uint32_t result = static_cast<uint32_t>(hash ^ (hash >> 32));
  return result == 0 ? 1 : result;
}

static semantic::SVClassTypeOp getOwningClass(Operation *member) {
  for (Operation *parent = member ? member->getParentOp() : nullptr; parent;
       parent = parent->getParentOp())
    if (auto classType = dyn_cast<semantic::SVClassTypeOp>(parent))
      return classType;
  return {};
}

/// Return whether assigning arbitrary packed bits would violate the source
/// domain. Enum values require an enumerator inventory, and tagged unions
/// require a coordinated tag/payload choice; neither is represented by the
/// initial finite-domain bit-vector plan.
static bool hasUnsupportedRandomDomain(Type type) {
  if (isa<semantic::EnumType>(type))
    return true;
  if (auto array = dyn_cast<semantic::RangedPackedArrayType>(type))
    return hasUnsupportedRandomDomain(array.getElementType());
  if (auto array = dyn_cast<semantic::PackedArrayType>(type))
    return hasUnsupportedRandomDomain(array.getElementType());
  if (auto aggregate = dyn_cast<semantic::SourceAggregateType>(type)) {
    if (aggregate.getIsTagged())
      return true;
    for (Attribute fieldAttr : aggregate.getFields()) {
      auto field = dyn_cast<DictionaryAttr>(fieldAttr);
      auto fieldType = field ? field.getAs<TypeAttr>("type") : TypeAttr{};
      if (!fieldType || hasUnsupportedRandomDomain(fieldType.getValue()))
        return true;
    }
    return false;
  }
  auto dictionaryHasUnsupportedDomain = [&](DictionaryAttr fields) {
    for (NamedAttribute field : fields) {
      auto fieldType = dyn_cast<TypeAttr>(field.getValue());
      if (!fieldType || hasUnsupportedRandomDomain(fieldType.getValue()))
        return true;
    }
    return false;
  };
  if (auto aggregate = dyn_cast<semantic::PackedStructType>(type))
    return dictionaryHasUnsupportedDomain(aggregate.getFields());
  if (auto aggregate = dyn_cast<semantic::PackedUnionType>(type))
    return dictionaryHasUnsupportedDomain(aggregate.getFields());
  return false;
}

/// Constraint branches are evaluated eagerly while building the candidate
/// predicate. Keep that legal by admitting only expression nodes that are
/// intrinsically total and side-effect-free. Function calls and assignments
/// need a separately modeled solver-function contract before they can enter a
/// plan. Partial arithmetic is admitted here only so the encoder can apply its
/// operand-sensitive legality checks; shifts have total lowering that cannot
/// introduce poison during exhaustive search.
static bool isSupportedRandomConstraintExpression(Operation *op) {
  if (auto unary = dyn_cast<semantic::SVUnaryExpressionOp>(op)) {
    using Unary = semantic::SVUnaryOperator;
    switch (unary.getOperatorKind()) {
    case Unary::Plus:
    case Unary::Minus:
    case Unary::BitwiseNot:
    case Unary::BitwiseAnd:
    case Unary::BitwiseOr:
    case Unary::BitwiseXor:
    case Unary::BitwiseNand:
    case Unary::BitwiseNor:
    case Unary::BitwiseXnor:
    case Unary::LogicalNot:
      return true;
    case Unary::Preincrement:
    case Unary::Predecrement:
    case Unary::Postincrement:
    case Unary::Postdecrement:
      return false;
    }
    llvm_unreachable("unhandled SystemVerilog unary operator");
  }
  if (auto binary = dyn_cast<semantic::SVBinaryExpressionOp>(op)) {
    using Binary = semantic::SVBinaryOperator;
    switch (binary.getOperatorKind()) {
    case Binary::Add:
    case Binary::Subtract:
    case Binary::Multiply:
    case Binary::BinaryAnd:
    case Binary::BinaryOr:
    case Binary::BinaryXor:
    case Binary::BinaryXnor:
    case Binary::Equality:
    case Binary::Inequality:
    case Binary::CaseEquality:
    case Binary::CaseInequality:
    case Binary::GreaterThanEqual:
    case Binary::GreaterThan:
    case Binary::LessThanEqual:
    case Binary::LessThan:
    case Binary::WildcardEquality:
    case Binary::WildcardInequality:
    case Binary::LogicalAnd:
    case Binary::LogicalOr:
    case Binary::LogicalImplication:
    case Binary::LogicalEquivalence:
    case Binary::Divide:
    case Binary::Mod:
    case Binary::LogicalShiftLeft:
    case Binary::LogicalShiftRight:
    case Binary::ArithmeticShiftLeft:
    case Binary::ArithmeticShiftRight:
    case Binary::Power:
      return true;
    }
    llvm_unreachable("unhandled SystemVerilog binary operator");
  }
  return isa<
      semantic::SVNamedValueExpressionOp,
      semantic::SVHierarchicalValueExpressionOp, semantic::SVIntegerLiteralOp,
      semantic::SVUnbasedUnsizedIntegerLiteralOp,
      semantic::SVUnboundedLiteralOp, semantic::SVConversionExpressionOp,
      semantic::SVConditionalExpressionOp,
      semantic::SVConcatenationExpressionOp,
      semantic::SVReplicationExpressionOp,
      semantic::SVElementSelectExpressionOp,
      semantic::SVRangeSelectExpressionOp, semantic::SVMemberAccessExpressionOp,
      semantic::SVInsideExpressionOp, semantic::SVValueRangeExpressionOp,
      semantic::SVDistExpressionOp>(op);
}

static bool isProgramCodeUnit(Operation *op) {
  if (op->getParentOfType<semantic::SVAnonymousProgramSymbolOp>())
    return true;
  auto instance = op->getParentOfType<semantic::SVInstanceSymbolOp>();
  if (!instance)
    return false;
  auto reference = instance->getAttrOfType<SymbolRefAttr>("referenced_symbol");
  if (!reference)
    return false;
  auto definition =
      SymbolTable::lookupNearestSymbolFrom<semantic::SVDefinitionSymbolOp>(
          instance, reference);
  if (definition)
    return definition.getDefinitionKind() ==
           semantic::SVDefinitionKind::Program;

  // Elaborated instance references use the frontend's stable symbol spelling,
  // which may be flat even when parsed as a nested SymbolRefAttr. Resolve the
  // source definition name as a deterministic fallback.
  auto referencedPath = instance->getAttrOfType<StringAttr>("referenced_path");
  ModuleOp module = op->getParentOfType<ModuleOp>();
  bool program = false;
  if (referencedPath && module)
    module.walk([&](semantic::SVDefinitionSymbolOp candidate) {
      auto name = candidate->getAttrOfType<StringAttr>("name");
      if (name && name == referencedPath)
        program = candidate.getDefinitionKind() ==
                  semantic::SVDefinitionKind::Program;
    });
  return program;
}

class ObeliskSimPreparePass
    : public impl::ObeliskSimPreparePassBase<ObeliskSimPreparePass> {
public:
  void runOnOperation() override;
};

void ObeliskSimPreparePass::runOnOperation() {
  ModuleOp module = getOperation();
  MLIRContext *context = &getContext();

  FailureOr<ValidatedSemanticDesign> validated = validateSemanticDesign(module);
  if (failed(validated)) {
    signalPassFailure();
    return;
  }
  semantic::SVRootSymbolOp semanticRoot = validated->root;
  llvm::StringMap<Operation *> &semanticSymbols = validated->symbols;
  bool invalid = false;

  SmallVector<Operation *> sourceUnits;
  semanticRoot->walk<WalkOrder::PreOrder>([&](Operation *op) {
    if (auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(op);
        subroutine && subroutine.getIsBuiltin().value_or(false))
      return;
    auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(op);
    bool staticInitializer =
        property &&
        property.getLifetime() == semantic::SVVariableLifetime::Static &&
        !getChildren(property).empty();
    auto variable = dyn_cast<semantic::SVVariableSymbolOp>(op);
    bool designInitializer = variable && !isNestedInCodeUnit(variable) &&
                             !isAutomaticLocalSymbol(variable) &&
                             !getChildren(variable).empty();
    auto net = dyn_cast<semantic::SVNetSymbolOp>(op);
    bool netInitializer = net && !getChildren(net).empty();
    if (isCodeUnit(op) || staticInitializer || designInitializer ||
        netInitializer)
      sourceUnits.push_back(op);
  });

  // Assign compact, collision-free IDs from sorted elaborated paths. These
  // IDs cross both native and bytecode ABIs, so unchecked truncated hashes
  // are not acceptable.
  llvm::StringSet<> controlPaths;
  llvm::StringSet<> staticPaths;
  semanticRoot->walk([&](Operation *op) {
    if (auto block = dyn_cast<semantic::SVBlockStatementOp>(op)) {
      if (auto path = block.getBlockPathAttr())
        controlPaths.insert(path.getValue());
    } else if (auto disable = dyn_cast<semantic::SVDisableStatementOp>(op)) {
      if (auto path = disable.getTargetPathAttr())
        controlPaths.insert(path.getValue());
    } else if (auto declaration =
                   dyn_cast<semantic::SVVariableDeclStatementOp>(op)) {
      staticPaths.insert(declaration.getReferencedPath());
    }
  });
  auto assignPathIDs = [&](llvm::StringSet<> &paths, StringRef attrName) {
    SmallVector<StringRef> ordered;
    ordered.reserve(paths.size());
    for (const auto &path : paths)
      ordered.push_back(path.getKey());
    llvm::sort(ordered);
    llvm::StringMap<uint64_t> ids;
    for (auto [index, path] : llvm::enumerate(ordered))
      ids[path] = index + 1;
    semanticRoot->walk([&](Operation *op) {
      StringAttr path;
      if (attrName == "obelisk_sim.control_target_id") {
        if (auto block = dyn_cast<semantic::SVBlockStatementOp>(op))
          path = block.getBlockPathAttr();
        else if (auto disable = dyn_cast<semantic::SVDisableStatementOp>(op))
          path = disable.getTargetPathAttr();
      } else if (auto declaration =
                     dyn_cast<semantic::SVVariableDeclStatementOp>(op)) {
        path = StringAttr::get(context, declaration.getReferencedPath());
      }
      if (path)
        op->setAttr(attrName, IntegerAttr::get(IntegerType::get(context, 64),
                                               ids.lookup(path.getValue())));
    });
  };
  assignPathIDs(controlPaths, "obelisk_sim.control_target_id");
  assignPathIDs(staticPaths, "obelisk_sim.static_site_id");

  uint64_t designPrecisionFs = std::numeric_limits<uint64_t>::max();
  auto accumulateTimeScale = [&](Operation *source, StringRef kind) {
    auto timeUnit = source->getAttrOfType<IntegerAttr>("time_unit_fs");
    auto timePrecision =
        source->getAttrOfType<IntegerAttr>("time_precision_fs");
    if (static_cast<bool>(timeUnit) != static_cast<bool>(timePrecision)) {
      emitError(getSemanticLocation(source))
          << kind << " has an incomplete elaborated time scale";
      invalid = true;
      return;
    }
    if (!timeUnit)
      return;
    std::optional<uint64_t> unitFsValue = getUnsigned64(timeUnit);
    std::optional<uint64_t> precisionFsValue = getUnsigned64(timePrecision);
    if (!unitFsValue || !precisionFsValue) {
      emitError(getSemanticLocation(source))
          << "elaborated time scale does not fit an unsigned 64-bit value";
      invalid = true;
      return;
    }
    uint64_t unitFs = *unitFsValue;
    uint64_t precisionFs = *precisionFsValue;
    if (unitFs == 0 || precisionFs == 0 || unitFs < precisionFs ||
        unitFs % precisionFs != 0) {
      emitError(getSemanticLocation(source))
          << "invalid elaborated time scale " << unitFs << "fs/" << precisionFs
          << "fs";
      invalid = true;
      return;
    }
    designPrecisionFs = std::min(designPrecisionFs, precisionFs);
  };
  for (Operation *unit : sourceUnits) {
    if (auto assignment =
            dyn_cast<semantic::SVContinuousAssignSymbolOp>(unit)) {
      if (assignment.getUnsupportedStrength()) {
        emitError(getSemanticLocation(unit))
            << "continuous-assignment strengths are not supported: "
            << *assignment.getUnsupportedStrength();
        invalid = true;
      }
      if (assignment.getUnsupportedDelay()) {
        emitError(getSemanticLocation(unit))
            << "continuous-assignment delays are not supported: "
            << *assignment.getUnsupportedDelay();
        invalid = true;
      }
    }
    if (auto primitive =
            dyn_cast<semantic::SVPrimitiveInstanceSymbolOp>(unit)) {
      if (primitive.getUnsupportedStrength()) {
        emitError(getSemanticLocation(unit))
            << "primitive strengths are not supported: "
            << *primitive.getUnsupportedStrength();
        invalid = true;
      }
      if (primitive.getUnsupportedDelay()) {
        emitError(getSemanticLocation(unit))
            << "primitive delays are not supported: "
            << *primitive.getUnsupportedDelay();
        invalid = true;
      }
    }
    // Synthetic code units do not carry an elaborated time scale. They must
    // not introduce a 1ns precision into a design whose actual declarations
    // use a different precision.
    accumulateTimeScale(unit, "code unit");
  }
  semanticRoot->walk<WalkOrder::PreOrder>(
      [&](semantic::SVInstanceBodySymbolOp body) {
        accumulateTimeScale(body, "simulation scope");
      });
  if (designPrecisionFs == std::numeric_limits<uint64_t>::max())
    designPrecisionFs = 1'000'000;
  if (designPrecisionFs >
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    module.emitError("design time precision exceeds the simulation time ABI");
    signalPassFailure();
    return;
  }
  if (invalid) {
    signalPassFailure();
    return;
  }

  OpBuilder moduleBuilder(module.getBodyRegion());
  moduleBuilder.setInsertionPointToEnd(module.getBody());
  auto design = sim::SimDesignOp::create(
      moduleBuilder, module.getLoc(), "design",
      moduleBuilder.getI64IntegerAttr(designPrecisionFs),
      sim::ComputeGraphAttr{});
  design.getBody().push_back(new Block());
  OpBuilder builder(context);
  builder.setInsertionPointToStart(&design.getBody().front());

  // Any failure from here on leaves a partially built design behind, so every
  // exit erases it rather than emitting half-lowered IR.
  auto abort = [&] {
    design.erase();
    signalPassFailure();
  };

  if (failed(materializeCovergroupDeclarations(semanticRoot, builder))) {
    abort();
    return;
  }

  FailureOr<PreparedClassDeclarations> classes = materializeClassDeclarations(
      module, design, semanticRoot, builder, semanticSymbols);
  if (failed(classes)) {
    abort();
    return;
  }
  auto &classSources = classes->sources;
  auto &classSymbols = classes->symbols;
  auto &classFieldSymbols = classes->fieldSymbols;
  auto &randcKeyFieldSymbols = classes->randcKeyFieldSymbols;
  auto &randcPositionFieldSymbols = classes->randcPositionFieldSymbols;
  auto &classMethodSymbols = classes->methodSymbols;
  auto &implicitConstructorSymbols = classes->implicitConstructorSymbols;
  auto &virtualMethodSlots = classes->virtualMethodSlots;
  auto &virtualMethodSignatures = classes->virtualMethodSignatures;
  auto &semanticClasses = classes->semanticClasses;

  FailureOr<PreparedScopeDeclarations> scopes = materializeScopeDeclarations(
      semanticRoot, sourceUnits, designPrecisionFs, builder);
  if (failed(scopes)) {
    abort();
    return;
  }
  auto getScopeId = [&](Operation *operation) {
    return scopes->lookup(operation);
  };

  FailureOr<PreparedPortAliases> portAliases = analyzePortAliases(semanticRoot);
  if (failed(portAliases)) {
    abort();
    return;
  }
  auto &portConnections = portAliases->connections;

  FailureOr<llvm::StringMap<DescriptorInfo>> preparedDescriptors =
      materializeDesignDescriptors(module, semanticRoot, *portAliases, *scopes,
                                   builder);
  if (failed(preparedDescriptors))
    return abort();
  llvm::StringMap<DescriptorInfo> &descriptors = *preparedDescriptors;

  FailureOr<ContinuousDriverMap> preparedNetTopology =
      materializeNetTopology(sourceUnits, portConnections, semanticSymbols,
                             descriptors, *scopes, builder);
  if (failed(preparedNetTopology))
    return abort();
  ContinuousDriverMap &continuousDrivers = *preparedNetTopology;

  FailureOr<PreparedUnits> preparedUnits = materializeCodeUnitDeclarations(
      module, semanticRoot, sourceUnits, semanticSymbols, *scopes, builder);
  if (failed(preparedUnits))
    return abort();
  auto &units = preparedUnits->units;
  auto &directCalleeNames = preparedUnits->directCalleeNames;
  auto &codeUnitDeclarations = preparedUnits->declarations;
  uint64_t rootCodeUnitID = preparedUnits->rootID;
  auto resolveDirectCallee =
      [&](semantic::SVCallExpressionOp call) -> Operation * {
    return preparedUnits->resolveDirectCallee(call, semanticSymbols);
  };

  // Create the root shell first. Its body is filled after all process shells
  // exist, so every spawn uses an immutable precomputed flat name.
  SmallVector<DictionaryAttr> rootArgAttrs{
      captureMetadata(builder, sim::CaptureKind::Context)};
  auto rootType =
      FunctionType::get(context, {sim::ContextType::get(context)}, {});
  SmallVector<NamedAttribute> rootAttrs{builder.getNamedAttr(
      "code_unit_id", builder.getI64IntegerAttr(rootCodeUnitID))};
  rootAttrs.push_back(builder.getNamedAttr(
      "home_region",
      sim::EventRegionAttr::get(context, sim::EventRegion::Active)));
  rootAttrs.push_back(builder.getNamedAttr(
      "domain",
      sim::ExecutionDomainAttr::get(context, sim::ExecutionDomain::Design)));
  auto rootInitializer = sim::SimFuncOp::create(
      builder, module.getLoc(), "__obelisk_root", rootType,
      sim::EntryKind::RootInitializer, rootAttrs, rootArgAttrs);

  auto collectClassHierarchy =
      [&](semantic::SVClassTypeOp leaf,
          SmallVectorImpl<semantic::SVClassTypeOp> &hierarchy,
          StringRef purpose) -> LogicalResult {
    llvm::SmallPtrSet<Operation *, 8> visiting;
    std::function<LogicalResult(semantic::SVClassTypeOp)> collect =
        [&](semantic::SVClassTypeOp classType) -> LogicalResult {
      if (!visiting.insert(classType).second)
        return classType.emitError("randomization class hierarchy is cyclic");
      if (std::optional<Type> baseType = classType.getBaseClass()) {
        auto baseHandle = dyn_cast<semantic::ClassHandleType>(*baseType);
        auto base = baseHandle
                        ? semanticClasses.find(
                              baseHandle.getClassName().getLeafReference())
                        : semanticClasses.end();
        if (base == semanticClasses.end()) {
          emitError(getSemanticLocation(classType))
              << purpose << " cannot resolve the base class";
          return failure();
        }
        if (failed(collect(base->second)))
          return failure();
      }
      hierarchy.push_back(classType);
      return success();
    };
    return collect(leaf);
  };

  using EffectiveConstraintGroup =
      SmallVector<semantic::SVConstraintBlockSymbolOp, 2>;
  auto collectEffectiveConstraints =
      [&](ArrayRef<semantic::SVClassTypeOp> hierarchy,
          SmallVectorImpl<EffectiveConstraintGroup> &groups) {
    llvm::StringMap<unsigned> namedIndices;
    for (semantic::SVClassTypeOp classType : hierarchy) {
      for (Operation *member : getChildren(classType)) {
        auto constraint =
            dyn_cast<semantic::SVConstraintBlockSymbolOp>(member);
        if (!constraint)
          continue;
        std::optional<StringRef> name = constraint.getName();
        if (!name) {
          groups.push_back({constraint});
          continue;
        }
        auto [entry, inserted] = namedIndices.try_emplace(
            *name, static_cast<unsigned>(groups.size()));
        if (inserted) {
          groups.push_back({constraint});
          continue;
        }
        EffectiveConstraintGroup &group = groups[entry->second];
        if (!constraint.getIsExtends().value_or(false))
          group.clear();
        group.push_back(constraint);
      }
    }
  };

  // Freeze object randomization into each call before its semantic class and
  // constraint declarations are erased. The unit-lowering pass is isolated,
  // so the cloned constraint expressions and this compact field inventory are
  // its complete compiler-owned randomization plan.
  std::function<bool(semantic::SVCallExpressionOp)> freezeRandomizeContract;
  freezeRandomizeContract =
      [&](semantic::SVCallExpressionOp call) -> bool {
    if (!call.getIsSystemCall() || call.getCalleeName() != "randomize")
      return false;
    if (call->hasAttr(randomizeAttrName) ||
        call->hasAttr(randomizeDispatchAttrName))
      return true;
    SmallVector<Operation *> callChildren = getChildren(call);
    uint64_t argumentCount = call.getArgumentCount();
    bool hasInlineConstraints = call.getHasInlineConstraints();
    if (argumentCount == 0) {
      emitError(getSemanticLocation(call))
          << "std::randomize is outside the executable object-randomization "
             "boundary";
      invalid = true;
      return true;
    }
    if (argumentCount > callChildren.size()) {
      emitError(getSemanticLocation(call))
          << "randomize call has malformed argument metadata";
      invalid = true;
      return true;
    }
    unsigned receiverIndex =
        static_cast<unsigned>(callChildren.size() - argumentCount);
    bool frozenChecker = call->hasAttr(randomizeCheckerOnlyAttrName);
    bool checkerOnly =
        frozenChecker ||
        (argumentCount == 2 &&
         isa<semantic::SVNullLiteralOp>(callChildren.back()));
    auto receiverTypeAttr =
        callChildren[receiverIndex]->getAttrOfType<TypeAttr>("semantic_type");
    auto receiverType =
        receiverTypeAttr
            ? dyn_cast<semantic::ClassHandleType>(receiverTypeAttr.getValue())
            : semantic::ClassHandleType{};
    if (!receiverType) {
      emitError(getSemanticLocation(call))
          << "std::randomize is outside the executable object-randomization "
             "boundary";
      invalid = true;
      return true;
    }
    if (checkerOnly)
      call->setAttr(randomizeCheckerOnlyAttrName, builder.getUnitAttr());
    if (argumentCount != 1 && !checkerOnly) {
      emitError(getSemanticLocation(call))
          << "randomize property argument lists are outside the executable "
             "object-randomization boundary";
      invalid = true;
      return true;
    }

    auto foundClass =
        semanticClasses.find(receiverType.getClassName().getLeafReference());
    if (auto plannedClass =
            call->getAttrOfType<FlatSymbolRefAttr>(randomizePlanClassAttrName)) {
      foundClass = semanticClasses.end();
      for (semantic::SVClassTypeOp candidate : classSources)
        if (classSymbols.lookup(candidate).getValue() ==
            plannedClass.getValue()) {
          foundClass = semanticClasses.find(
              cast<semantic::ClassHandleType>(candidate.getSemanticType())
                  .getClassName()
                  .getLeafReference());
          break;
        }
    }
    if (foundClass == semanticClasses.end()) {
      emitError(getSemanticLocation(call))
          << "randomize receiver class does not resolve";
      invalid = true;
      return true;
    }

    // A randomize call uses the dynamic object's complete property and
    // constraint set even though randomize itself is a builtin. Specialize a
    // frozen plan for every concrete class that can inhabit the static handle,
    // then select the exact plan once at the call site. Keeping the alternatives
    // as nested semantic calls lets the ordinary capture analysis see their
    // union and keeps all sampler generation in the isolated unit lowering.
    if (!call->hasAttr(randomizePlanClassAttrName)) {
      struct DynamicPlan {
        semantic::SVClassTypeOp classType;
        unsigned depth;
      };
      SmallVector<DynamicPlan> dynamicPlans;
      for (semantic::SVClassTypeOp candidate : classSources) {
        if (candidate.getIsAbstract() || candidate.getIsInterface())
          continue;
        SmallVector<semantic::SVClassTypeOp> candidateHierarchy;
        if (failed(collectClassHierarchy(candidate, candidateHierarchy,
                                         "randomization dispatch"))) {
          invalid = true;
          return true;
        }
        bool compatible = llvm::is_contained(candidateHierarchy,
                                             foundClass->second);
        if (!compatible && foundClass->second.getIsInterface()) {
          StringRef targetInterface =
              cast<semantic::ClassHandleType>(
                  foundClass->second.getSemanticType())
                  .getClassName()
                  .getLeafReference();
          // Slang records the transitive interface closure on the class that
          // declares `implements`, but a derived class has an empty local
          // interface list. Search its base hierarchy as well so an
          // interface-typed handle can select plans for inherited
          // implementations.
          for (semantic::SVClassTypeOp hierarchyClass : candidateHierarchy) {
            for (Attribute attribute :
                 hierarchyClass.getImplementedInterfaces()) {
              auto type = dyn_cast<TypeAttr>(attribute);
              auto interface =
                  type ? dyn_cast<semantic::ClassHandleType>(type.getValue())
                       : semantic::ClassHandleType{};
              if (interface &&
                  interface.getClassName().getLeafReference() ==
                      targetInterface) {
                compatible = true;
                break;
              }
            }
            if (compatible)
              break;
          }
        }
        if (compatible)
          dynamicPlans.push_back(
              {candidate, static_cast<unsigned>(candidateHierarchy.size())});
      }
      llvm::sort(dynamicPlans, [&](const DynamicPlan &lhs,
                                   const DynamicPlan &rhs) {
        if (lhs.depth != rhs.depth)
          return lhs.depth > rhs.depth;
        return classSymbols.lookup(lhs.classType).getValue() <
               classSymbols.lookup(rhs.classType).getValue();
      });
      {
        SmallVector<semantic::SVCallExpressionOp> alternatives;
        alternatives.reserve(dynamicPlans.size());
        // Clone every raw call before inserting any clone into the source call;
        // otherwise later clones would recursively contain earlier plans.
        for (const DynamicPlan &plan : dynamicPlans) {
          auto alternative =
              cast<semantic::SVCallExpressionOp>(call->clone());
          if (checkerOnly) {
            SmallVector<Operation *> alternativeChildren =
                getChildren(alternative);
            if (!frozenChecker &&
                (alternativeChildren.empty() ||
                 !isa<semantic::SVNullLiteralOp>(
                     alternativeChildren.back()))) {
              emitError(getSemanticLocation(call))
                  << "randomize(null) has malformed checker metadata";
              invalid = true;
              return true;
            }
            if (!frozenChecker) {
              alternativeChildren.back()->erase();
              alternative->setAttr("argument_count",
                                   builder.getI64IntegerAttr(1));
              alternative->setAttr("defaulted_arguments",
                                   builder.getDenseI64ArrayAttr({0}));
            }
          }
          alternative->setAttr(
              randomizePlanClassAttrName,
              FlatSymbolRefAttr::get(
                  context, classSymbols.lookup(plan.classType).getValue()));
          alternatives.push_back(alternative);
        }
        OpBuilder alternativeBuilder =
            OpBuilder::atBlockEnd(&call->getRegion(0).front());
        for (semantic::SVCallExpressionOp alternative : alternatives) {
          alternativeBuilder.insert(alternative);
          freezeRandomizeContract(alternative);
        }
        call->setAttr(randomizeDispatchAttrName, builder.getUnitAttr());
        call->setAttr(randomReceiverIndexAttrName,
                      builder.getI32IntegerAttr(receiverIndex));
        return true;
      }
    }

    SmallVector<semantic::SVClassTypeOp> hierarchy;
    if (failed(collectClassHierarchy(foundClass->second, hierarchy,
                                     "randomization"))) {
      invalid = true;
      return true;
    }

    struct RandomProperty {
      Operation *source;
      FlatSymbolRefAttr field;
      Type type;
      uint64_t width;
      bool isSigned;
      bool isRandC;
      FlatSymbolRefAttr randcKeyField;
      FlatSymbolRefAttr randcPositionField;
    };
    SmallVector<RandomProperty> properties;
    SmallVector<Operation *> constraintRoots;
    SmallVector<EffectiveConstraintGroup> constraintGroups;
    semantic::SVSubroutineSymbolOp preRandomizeHook;
    semantic::SVSubroutineSymbolOp postRandomizeHook;
    if (hasInlineConstraints)
      for (auto [index, child] : llvm::enumerate(callChildren))
        if (index != receiverIndex && isa<semantic::SVConstraintListOp>(child))
          constraintRoots.push_back(child);
    for (semantic::SVClassTypeOp classType : hierarchy) {
      for (Operation *member : getChildren(classType)) {
        if (semantic::SVSubroutineSymbolOp method = getClassMethod(member);
            method && method.getIsPrePostRandomize().value_or(false) &&
            !method.getIsBuiltin().value_or(false)) {
          StringRef name = method.getName().value_or("");
          std::optional<Type> methodType = method.getSemanticType();
          auto subroutineType =
              methodType ? dyn_cast<semantic::SubroutineType>(*methodType)
                         : semantic::SubroutineType{};
          auto signature =
              subroutineType
                  ? dyn_cast<FunctionType>(subroutineType.getSignature())
                  : FunctionType{};
          if (method.getIsStatic().value_or(false) ||
              method.getSubroutineKind() !=
                  semantic::SVSubroutineKind::Function ||
              !signature || signature.getNumInputs() != 0 ||
              signature.getNumResults() != 1 ||
              !isa<semantic::VoidType>(signature.getResult(0))) {
            emitError(getSemanticLocation(method))
                << "randomization hooks must be void instance functions "
                   "without arguments";
            invalid = true;
          } else if (name == "pre_randomize") {
            preRandomizeHook = method;
          } else if (name == "post_randomize") {
            postRandomizeHook = method;
          } else {
            emitError(getSemanticLocation(method))
                << "unknown randomization lifecycle hook " << name;
            invalid = true;
          }
          continue;
        }
        if (auto property =
                dyn_cast<semantic::SVClassPropertySymbolOp>(member)) {
          if (property.getLifetime() == semantic::SVVariableLifetime::Static ||
              property.getRandMode() == semantic::SVRandMode::None)
            continue;
          FailureOr<Type> type = getNormalizedSemanticType(property);
          FlatSymbolRefAttr field = classFieldSymbols.lookup(property);
          if (failed(type)) {
            invalid = true;
            continue;
          }
          std::optional<unsigned> width = sim::getPackedWidth(*type);
          if (!width || *width == 0 || *width > 64 || !field) {
            emitError(getSemanticLocation(property))
                << "random properties must be packed integral values no "
                   "wider than 64 bits";
            invalid = true;
            continue;
          }
          bool isRandC =
              property.getRandMode() == semantic::SVRandMode::RandC;
          FlatSymbolRefAttr randcKeyField =
              randcKeyFieldSymbols.lookup(property);
          FlatSymbolRefAttr randcPositionField =
              randcPositionFieldSymbols.lookup(property);
          if (isRandC &&
              (*width > 32 || !randcKeyField || !randcPositionField)) {
            emitError(getSemanticLocation(property))
                << "randc properties must be packed integral values no wider "
                   "than 32 bits";
            invalid = true;
            continue;
          }
          std::optional<Type> semanticPropertyType = property.getSemanticType();
          if (!semanticPropertyType ||
              hasUnsupportedRandomDomain(*semanticPropertyType)) {
            emitError(getSemanticLocation(property))
                << "rand enum and tagged-union domains are not executable yet";
            invalid = true;
            continue;
          }
          properties.push_back(
              {property, field, *type, *width,
               isSignedSemanticType(*semanticPropertyType), isRandC,
               randcKeyField, randcPositionField});
          continue;
        }
      }
    }
    auto freezeHook = [&](semantic::SVSubroutineSymbolOp hook,
                          StringRef calleeAttr,
                          StringRef ownerAttr,
                          StringRef sourceAttr) -> LogicalResult {
      if (!hook)
        return success();
      auto callee = directCalleeNames.find(hook);
      semantic::SVClassTypeOp owner = getOwningClass(hook);
      StringAttr ownerSymbol = owner ? classSymbols.lookup(owner) : StringAttr{};
      if (callee == directCalleeNames.end() || !ownerSymbol) {
        emitError(getSemanticLocation(hook))
            << "randomization hook has no executable class method";
        return failure();
      }
      call->setAttr(calleeAttr,
                    FlatSymbolRefAttr::get(context, callee->second));
      call->setAttr(ownerAttr,
                    FlatSymbolRefAttr::get(context, ownerSymbol.getValue()));
      call->setAttr(sourceAttr,
                    FlatSymbolRefAttr::get(context, hook.getSymName()));
      return success();
    };
    if (failed(freezeHook(preRandomizeHook, randomPreHookAttrName,
                          randomPreHookOwnerAttrName,
                          randomPreHookSourceAttrName)) ||
        failed(freezeHook(postRandomizeHook, randomPostHookAttrName,
                          randomPostHookOwnerAttrName,
                          randomPostHookSourceAttrName)))
      invalid = true;
    collectEffectiveConstraints(hierarchy, constraintGroups);
    if (constraintGroups.size() > 64) {
      emitError(getSemanticLocation(call))
          << "the executable constraint_mode boundary is 64 effective "
             "constraint blocks";
      invalid = true;
      return true;
    }
    llvm::DenseMap<Operation *, unsigned> constraintIndices;
    for (auto [index, group] : llvm::enumerate(constraintGroups)) {
      for (semantic::SVConstraintBlockSymbolOp constraint : group)
        constraintIndices[constraint] = index;
    }
    // Stable mode-bit order follows the first base declaration of each named
    // block. Executable body and soft-priority order instead follow active
    // source declaration order, with every derived declaration after all base
    // declarations. Keep those two orderings deliberately separate.
    for (semantic::SVClassTypeOp classType : hierarchy) {
      for (Operation *member : getChildren(classType)) {
        auto constraint =
            dyn_cast<semantic::SVConstraintBlockSymbolOp>(member);
        if (!constraint || !constraintIndices.contains(constraint))
          continue;
        if (constraint.getIsExtern().value_or(false) ||
            constraint.getIsPure().value_or(false)) {
          emitError(getSemanticLocation(constraint))
              << "extern and pure constraint blocks are not executable yet";
          invalid = true;
          continue;
        }
        for (Operation *child : getChildren(constraint))
          if (isa<semantic::SVConstraintListOp>(child))
            constraintRoots.push_back(child);
      }
    }

    uint64_t totalWidth = 0;
    for (const RandomProperty &property : properties) {
      if (property.width > 64 - totalWidth) {
        emitError(getSemanticLocation(call))
            << "the executable exhaustive randomization boundary is 64 "
               "aggregate rand bits";
        invalid = true;
        return true;
      }
      totalWidth += property.width;
    }

    unsigned softConstraintCount = 0;
    for (Operation *root : constraintRoots) {
      root->walk([&](Operation *nested) {
        if (auto expression =
                dyn_cast<semantic::SVExpressionConstraintOp>(nested)) {
          if (expression.getIsSoft()) {
            ++softConstraintCount;
          }
          return;
        }
        if (auto solve =
                dyn_cast<semantic::SVSolveBeforeConstraintOp>(nested)) {
          auto solveCount = solve->getAttrOfType<IntegerAttr>("solve_count");
          auto afterCount = solve->getAttrOfType<IntegerAttr>("after_count");
          SmallVector<Operation *> operands = getChildren(solve);
          if (solveCount && afterCount && !solveCount.getValue().isNegative() &&
              !afterCount.getValue().isNegative() &&
              solveCount.getValue().getActiveBits() <= 64 &&
              afterCount.getValue().getActiveBits() <= 64) {
            uint64_t beforeSize = solveCount.getValue().getZExtValue();
            uint64_t afterSize = afterCount.getValue().getZExtValue();
            if (beforeSize <= operands.size() &&
                afterSize == operands.size() - beforeSize) {
              for (Operation *before :
                   ArrayRef(operands).take_front(beforeSize))
                for (Operation *after :
                     ArrayRef(operands).drop_front(beforeSize)) {
                  auto beforeSymbol =
                      before->getAttrOfType<SymbolRefAttr>("referenced_symbol");
                  auto afterSymbol =
                      after->getAttrOfType<SymbolRefAttr>("referenced_symbol");
                  if (beforeSymbol && beforeSymbol == afterSymbol) {
                    emitError(getSemanticLocation(solve))
                        << "solve before cannot order a property before itself";
                    invalid = true;
                    return;
                  }
                }
            }
          }
          return;
        }
        if (isa<semantic::SVConstraintListOp,
                semantic::SVImplicationConstraintOp,
                semantic::SVConditionalConstraintOp,
                semantic::SVUniquenessConstraintOp>(nested))
          return;
        if (nested->hasTrait<OpTrait::SemanticDeclarativeNode>() &&
            !isa<semantic::SVExpressionConstraintOp>(nested)) {
          emitError(getSemanticLocation(nested))
              << "constraint form is outside the executable hard-expression "
                 "boundary: "
              << nested->getName();
          invalid = true;
          return;
        }
        if (nested->hasTrait<OpTrait::SemanticASTNode>() &&
            !isSupportedRandomConstraintExpression(nested)) {
          emitError(getSemanticLocation(nested))
              << "constraint expression is outside the total side-effect-free "
                 "executable boundary: "
              << nested->getName();
          invalid = true;
        }
      });
    }
    if (softConstraintCount > 64) {
      emitError(getSemanticLocation(call))
          << "the executable soft-constraint priority boundary is 64";
      invalid = true;
    }

    SmallVector<Attribute> propertyAttrs;
    llvm::DenseMap<Operation *, unsigned> randomIndices;
    for (auto [index, property] : llvm::enumerate(properties)) {
      randomIndices[property.source] = index;
      SmallVector<NamedAttribute> attributes{
          builder.getNamedAttr("field", property.field),
          builder.getNamedAttr("type", TypeAttr::get(property.type)),
          builder.getNamedAttr("width",
                               builder.getI64IntegerAttr(property.width)),
          builder.getNamedAttr("is_signed",
                               builder.getBoolAttr(property.isSigned)),
          builder.getNamedAttr("is_randc",
                               builder.getBoolAttr(property.isRandC)),
      };
      if (property.isRandC) {
        attributes.push_back(
            builder.getNamedAttr("randc_key_field", property.randcKeyField));
        attributes.push_back(builder.getNamedAttr(
            "randc_position_field", property.randcPositionField));
      }
      propertyAttrs.push_back(builder.getDictionaryAttr(attributes));
    }
    call->setAttr(randomizeAttrName, builder.getUnitAttr());
    call->setAttr(randomReceiverIndexAttrName,
                  builder.getI32IntegerAttr(receiverIndex));
    call->setAttr(randomPropertiesAttrName,
                  builder.getArrayAttr(propertyAttrs));
    call->setAttr(randomTotalWidthAttrName,
                  builder.getI64IntegerAttr(totalWidth));
    call->setAttr(randomConstraintCountAttrName,
                  builder.getI32IntegerAttr(constraintGroups.size()));

    auto annotateConstraint = [&](Operation *constraint) {
      constraint->walk([&](Operation *nested) {
        auto reference =
            nested->getAttrOfType<SymbolRefAttr>("referenced_symbol");
        if (!reference)
          return;
        auto symbol = semanticSymbols.find(reference.getLeafReference());
        if (symbol == semanticSymbols.end())
          return;
        if (auto field = classFieldSymbols.find(symbol->second);
            field != classFieldSymbols.end())
          nested->setAttr("obelisk_sim.class_field", field->second);
        if (auto index = randomIndices.find(symbol->second);
            index != randomIndices.end())
          nested->setAttr(randomVariableAttrName,
                          builder.getI32IntegerAttr(index->second));
        if (isa<semantic::SVParameterSymbolOp, semantic::SVEnumValueSymbolOp,
                semantic::SVSpecparamSymbolOp>(symbol->second))
          if (auto constant =
                  symbol->second->getAttrOfType<StringAttr>("constant_value"))
            nested->setAttr("obelisk_sim.constant_value", constant);
      });
    };
    for (auto [index, child] : llvm::enumerate(callChildren))
      if (index != receiverIndex && isa<semantic::SVConstraintListOp>(child))
        annotateConstraint(child);

    OpBuilder constraintBuilder =
        OpBuilder::atBlockEnd(&call->getRegion(0).front());
    for (Operation *root : constraintRoots) {
      if (llvm::is_contained(callChildren, root))
        continue;
      Operation *cloned = constraintBuilder.clone(*root);
      auto constraint =
          dyn_cast<semantic::SVConstraintBlockSymbolOp>(root->getParentOp());
      if (constraint)
        if (auto index = constraintIndices.find(constraint);
            index != constraintIndices.end())
          cloned->setAttr(randomConstraintBlockAttrName,
                          builder.getI32IntegerAttr(index->second));
      annotateConstraint(cloned);
    }
    return true;
  };

  // Preserve the distinction between the class-wide rand_mode builtin and a
  // property rand_mode builtin while the semantic declarations still exist.
  // The latter also needs the property's stable base-first randomization-plan
  // index; the unit pass sees only lowered class fields after preparation.
  auto freezeRandModeContract = [&](semantic::SVCallExpressionOp call) -> bool {
    if (call.getCalleeName() != "rand_mode")
      return false;
    if (call->hasAttr(randomModeAttrName))
      return true;

    SmallVector<Operation *> callChildren = getChildren(call);
    if (callChildren.empty())
      return false;
    if (!call.getIsSystemCall()) {
      auto reference = call->getAttrOfType<SymbolRefAttr>("referenced_symbol");
      auto symbol = reference
                        ? semanticSymbols.find(reference.getLeafReference())
                        : semanticSymbols.end();
      auto target =
          symbol != semanticSymbols.end()
              ? dyn_cast<semantic::SVSubroutineSymbolOp>(symbol->second)
              : semantic::SVSubroutineSymbolOp{};
      if (!target || !target.getIsBuiltin().value_or(false) ||
          target.getName().value_or("") != "rand_mode" ||
          !getOwningClass(target))
        return false;
      call->setAttr(randomModeAttrName, builder.getUnitAttr());
      return true;
    }

    auto member =
        dyn_cast<semantic::SVMemberAccessExpressionOp>(callChildren.front());
    auto reference =
        member ? member->getAttrOfType<SymbolRefAttr>("referenced_symbol")
               : SymbolRefAttr{};
    auto symbol = reference ? semanticSymbols.find(reference.getLeafReference())
                            : semanticSymbols.end();
    auto property =
        symbol != semanticSymbols.end()
            ? dyn_cast<semantic::SVClassPropertySymbolOp>(symbol->second)
            : semantic::SVClassPropertySymbolOp{};
    if (!property ||
        property.getLifetime() == semantic::SVVariableLifetime::Static ||
        property.getRandMode() == semantic::SVRandMode::None)
      return false;

    auto owner =
        dyn_cast_or_null<semantic::SVClassTypeOp>(property->getParentOp());
    if (!owner)
      return false;
    SmallVector<semantic::SVClassTypeOp> hierarchy;
    if (failed(collectClassHierarchy(owner, hierarchy, "rand_mode"))) {
      invalid = true;
      return true;
    }

    unsigned propertyIndex = 0;
    bool found = false;
    for (semantic::SVClassTypeOp classType : hierarchy) {
      for (Operation *classMember : getChildren(classType)) {
        auto candidate =
            dyn_cast<semantic::SVClassPropertySymbolOp>(classMember);
        if (!candidate ||
            candidate.getLifetime() == semantic::SVVariableLifetime::Static ||
            candidate.getRandMode() == semantic::SVRandMode::None)
          continue;
        if (candidate == property) {
          found = true;
          break;
        }
        ++propertyIndex;
      }
      if (found)
        break;
    }
    if (!found || propertyIndex >= 64) {
      emitError(getSemanticLocation(call))
          << "property rand_mode exceeds the 64-property executable boundary";
      invalid = true;
      return true;
    }
    call->setAttr(randomModeAttrName, builder.getUnitAttr());
    call->setAttr(randomModePropertyAttrName,
                  builder.getI32IntegerAttr(propertyIndex));
    return true;
  };

  // A class-wide constraint_mode call is a builtin method, while a named
  // constraint-block call is represented as a system call whose first child
  // is a member access. Freeze both forms and assign named blocks the same
  // base-first index used by the effective inherited constraint set.
  auto freezeConstraintModeContract =
      [&](semantic::SVCallExpressionOp call) -> bool {
    if (call.getCalleeName() != "constraint_mode")
      return false;
    if (call->hasAttr(constraintModeAttrName))
      return true;

    SmallVector<Operation *> callChildren = getChildren(call);
    if (callChildren.empty())
      return false;
    if (!call.getIsSystemCall()) {
      auto reference = call->getAttrOfType<SymbolRefAttr>("referenced_symbol");
      auto symbol = reference
                        ? semanticSymbols.find(reference.getLeafReference())
                        : semanticSymbols.end();
      auto target =
          symbol != semanticSymbols.end()
              ? dyn_cast<semantic::SVSubroutineSymbolOp>(symbol->second)
              : semantic::SVSubroutineSymbolOp{};
      if (!target || !target.getIsBuiltin().value_or(false) ||
          target.getName().value_or("") != "constraint_mode" ||
          !getOwningClass(target))
        return false;
      call->setAttr(constraintModeAttrName, builder.getUnitAttr());
      return true;
    }

    auto member =
        dyn_cast<semantic::SVMemberAccessExpressionOp>(callChildren.front());
    auto reference =
        member ? member->getAttrOfType<SymbolRefAttr>("referenced_symbol")
               : SymbolRefAttr{};
    auto symbol = reference ? semanticSymbols.find(reference.getLeafReference())
                            : semanticSymbols.end();
    auto constraint =
        symbol != semanticSymbols.end()
            ? dyn_cast<semantic::SVConstraintBlockSymbolOp>(symbol->second)
            : semantic::SVConstraintBlockSymbolOp{};
    auto owner = constraint ? dyn_cast_or_null<semantic::SVClassTypeOp>(
                                  constraint->getParentOp())
                            : semantic::SVClassTypeOp{};
    if (!constraint || !owner)
      return false;

    SmallVector<semantic::SVClassTypeOp> hierarchy;
    if (failed(
            collectClassHierarchy(owner, hierarchy, "constraint_mode"))) {
      invalid = true;
      return true;
    }
    SmallVector<EffectiveConstraintGroup> groups;
    collectEffectiveConstraints(hierarchy, groups);
    if (groups.size() > 64) {
      emitError(getSemanticLocation(call))
          << "constraint_mode exceeds the 64-block executable boundary";
      invalid = true;
      return true;
    }

    std::optional<StringRef> targetName = constraint.getName();
    std::optional<unsigned> constraintIndex;
    for (auto [index, group] : llvm::enumerate(groups)) {
      bool matches = llvm::is_contained(group, constraint);
      if (!matches && targetName && !group.empty())
        matches = group.front().getName() == targetName;
      if (matches) {
        constraintIndex = index;
        break;
      }
    }
    if (!constraintIndex) {
      emitError(getSemanticLocation(call))
          << "constraint_mode cannot resolve its effective constraint block";
      invalid = true;
      return true;
    }
    call->setAttr(constraintModeAttrName, builder.getUnitAttr());
    call->setAttr(constraintModeBlockAttrName,
                  builder.getI32IntegerAttr(*constraintIndex));
    return true;
  };

  // The capture inventory must see class constraints after they have been
  // cloned into their calling code unit. In particular, package and design
  // variables referenced by a class constraint are ordinary unit captures.
  SmallVector<semantic::SVCallExpressionOp> semanticCalls;
  semanticRoot->walk([&](semantic::SVCallExpressionOp call) {
    semanticCalls.push_back(call);
  });
  for (semantic::SVCallExpressionOp call : semanticCalls)
    if (!freezeRandModeContract(call) &&
        !freezeConstraintModeContract(call))
      freezeRandomizeContract(call);
  if (invalid)
    return abort();

  FailureOr<PreparedCaptures> preparedCaptures = analyzeCodeUnitCaptures(
      *preparedUnits, descriptors, semanticSymbols, classSources);
  if (failed(preparedCaptures))
    return abort();
  auto &unitCaptures = preparedCaptures->descriptors;
  auto &unitReadCaptures = preparedCaptures->readDescriptors;
  auto &unitLocals = preparedCaptures->locals;
  auto &unitConstants = preparedCaptures->constants;
  auto &observerLocalCaptures = preparedCaptures->observerLocals;
  auto &observerReadLocals = preparedCaptures->observerReadLocals;
  auto &indirectRefTasks = preparedCaptures->indirectRefTasks;

  for (PreparedUnit &unit : units) {
    if (unit.entryKind != sim::EntryKind::Observer)
      continue;
    SmallVector<Attribute> captures;
    SmallVector<Attribute> dependencies;
    for (auto &capture : unitCaptures[unit.source]) {
      captures.push_back(builder.getStringAttr(capture.first));
      if (unitReadCaptures[unit.source].contains(capture.first))
        dependencies.push_back(builder.getStringAttr(capture.first));
    }
    for (const PreparedLocal &local : observerLocalCaptures[unit.source]) {
      captures.push_back(builder.getStringAttr(local.path));
      if (observerReadLocals[unit.source].contains(local.path))
        dependencies.push_back(builder.getStringAttr(local.path));
    }
    unit.source->setAttr("obelisk_sim.observer_captures",
                         builder.getArrayAttr(captures));
    unit.source->setAttr("obelisk_sim.observer_dependencies",
                         builder.getArrayAttr(dependencies));
  }

  auto freezeRandomizeHookCaptures =
      [&](semantic::SVCallExpressionOp call) -> LogicalResult {
    auto freeze = [&](StringRef sourceAttr, StringRef capturesAttr,
                      StringRef readsAttr) -> LogicalResult {
      auto source = call->getAttrOfType<FlatSymbolRefAttr>(sourceAttr);
      if (!source)
        return success();
      auto found = semanticSymbols.find(source.getLeafReference());
      if (found == semanticSymbols.end()) {
        emitError(getSemanticLocation(call))
            << "randomization hook source no longer resolves";
        return failure();
      }
      SmallVector<Attribute> captures;
      SmallVector<Attribute> reads;
      for (const auto &capture : unitCaptures[found->second]) {
        captures.push_back(builder.getStringAttr(capture.first));
        if (unitReadCaptures[found->second].contains(capture.first))
          reads.push_back(builder.getStringAttr(capture.first));
      }
      call->setAttr(capturesAttr, builder.getArrayAttr(captures));
      call->setAttr(readsAttr, builder.getArrayAttr(reads));
      return success();
    };
    return failure(failed(freeze(randomPreHookSourceAttrName,
                                 randomPreHookCapturesAttrName,
                                 randomPreHookReadCapturesAttrName)) ||
                   failed(freeze(randomPostHookSourceAttrName,
                                 randomPostHookCapturesAttrName,
                                 randomPostHookReadCapturesAttrName)));
  };

  auto freezeCallContract = [&](semantic::SVCallExpressionOp call) {
    if (freezeRandModeContract(call))
      return;
    if (freezeConstraintModeContract(call))
      return;
    if (freezeRandomizeContract(call)) {
      if (failed(freezeRandomizeHookCaptures(call)))
        invalid = true;
      return;
    }
    Operation *targetSource = resolveDirectCallee(call);
    if (!targetSource)
      return;
    auto target = directCalleeNames.find(targetSource);
    assert(target != directCalleeNames.end() &&
           "resolved direct callee has no frozen symbol");
    call->setAttr(calleeAttrName,
                  FlatSymbolRefAttr::get(context, target->second));
    if (auto targetSubroutine =
            dyn_cast<semantic::SVSubroutineSymbolOp>(targetSource);
        targetSubroutine && getOwningClass(targetSubroutine) &&
        !targetSubroutine.getIsStatic().value_or(false)) {
      call->setAttr("obelisk_sim.class_instance", builder.getUnitAttr());
      if (call.getIsSuperClass())
        call->setAttr("obelisk_sim.class_super", builder.getUnitAttr());
      if (targetSubroutine.getIsVirtual().value_or(false) &&
          !call.getIsSuperClass())
        call->setAttr("obelisk_sim.class_virtual", builder.getUnitAttr());
      if (FlatSymbolRefAttr method =
              classMethodSymbols.lookup(targetSubroutine)) {
        call->setAttr("obelisk_sim.class_method", method);
        if (targetSubroutine.getIsVirtual().value_or(false)) {
          call->setAttr("obelisk_sim.class_slot",
                        builder.getI64IntegerAttr(
                            virtualMethodSlots.lookup(targetSubroutine)));
          call->setAttr("obelisk_sim.class_signature",
                        builder.getI64IntegerAttr(
                            virtualMethodSignatures.lookup(targetSubroutine)));
        }
      }
    }
    SmallVector<Attribute> capturePaths;
    bool dpiTarget = false;
    if (auto subroutine =
            dyn_cast<semantic::SVSubroutineSymbolOp>(targetSource);
        subroutine && subroutine.getIsDpiImport().value_or(false)) {
      dpiTarget = true;
      StringAttr cIdentifier = subroutine.getDpiCIdentifierAttr();
      call->setAttr(
          "obelisk.dpi.import_id",
          builder.getI32IntegerAttr(getStableImportID(cIdentifier.getValue())));
      call->setAttr("obelisk.dpi.c_identifier", cIdentifier);
      call->setAttr("obelisk.dpi.scope_id",
                    builder.getI64IntegerAttr(getScopeId(targetSource)));
      call->setAttr(
          "obelisk.dpi.is_pure",
          builder.getBoolAttr(subroutine.getIsPure().value_or(false)));
      call->setAttr(
          "obelisk.dpi.is_context",
          builder.getBoolAttr(subroutine.getIsDpiContext().value_or(false)));
      call->setAttr("obelisk.dpi.is_task",
                    builder.getBoolAttr(subroutine.getSubroutineKind() ==
                                        semantic::SVSubroutineKind::Task));
    }
    if (auto subroutine =
            dyn_cast<semantic::SVSubroutineSymbolOp>(targetSource);
        subroutine && !subroutine.getIsDpiImport().value_or(false) &&
        subroutine.getSubroutineKind() == semantic::SVSubroutineKind::Task)
      call->setAttr("obelisk_sim.is_task", builder.getUnitAttr());
    SmallVector<Attribute> readCapturePaths;
    for (auto &capture : unitCaptures[targetSource]) {
      capturePaths.push_back(builder.getStringAttr(capture.first));
      if (unitReadCaptures[targetSource].contains(capture.first))
        readCapturePaths.push_back(builder.getStringAttr(capture.first));
    }
    call->setAttr(calleeCapturesAttrName, builder.getArrayAttr(capturePaths));
    call->setAttr(calleeReadCapturesAttrName,
                  builder.getArrayAttr(readCapturePaths));
    // One dictionary per callee formal keeps the direction, normalized type,
    // and signedness of the frozen signature together.
    SmallVector<Attribute> formals;
    for (Operation *targetChild : getChildren(targetSource)) {
      auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(targetChild);
      if (!formal)
        continue;
      FailureOr<Type> formalType = getNormalizedSemanticType(formal);
      if (failed(formalType)) {
        invalid = true;
        continue;
      }
      std::optional<Type> semanticType = formal.getSemanticType();
      SmallVector<NamedAttribute> formalAttrs{
          builder.getNamedAttr(
              "direction", builder.getI64IntegerAttr(
                               static_cast<int64_t>(formal.getDirection()))),
          builder.getNamedAttr("type", TypeAttr::get(*formalType)),
          builder.getNamedAttr(
              "is_signed",
              builder.getBoolAttr(semanticType &&
                                  isSignedSemanticType(*semanticType))),
          builder.getNamedAttr(
              "argument_ref",
              builder.getBoolAttr(formal.getDirection() ==
                                      semantic::SVArgumentDirection::Ref &&
                                  indirectRefTasks.contains(targetSource))),
      };
      if (dpiTarget && semanticType) {
        FailureOr<DPIABIKind> category =
            getDPIABIKind(*semanticType, getSemanticLocation(formal));
        if (failed(category)) {
          invalid = true;
          continue;
        }
        formalAttrs.push_back(builder.getNamedAttr(
            "dpi_category",
            builder.getI32IntegerAttr(static_cast<uint32_t>(*category))));
      }
      formals.push_back(builder.getDictionaryAttr(formalAttrs));
    }
    call->setAttr(calleeFormalsAttrName, builder.getArrayAttr(formals));
  };

  for (PreparedUnit &unit : units) {
    auto captures = unitCaptures.lookup(unit.source);
    auto locals = unitLocals.lookup(unit.source);
    auto constants = unitConstants.lookup(unit.source);
    auto observerLocals = observerLocalCaptures.lookup(unit.source);
    SmallVector<Type> copyOutResultTypes;
    bool instanceClassMethod = false;
    Type classThisType;
    StringRef classThisPath;
    if (auto subroutine =
            dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source)) {
      auto owner = getOwningClass(subroutine);
      instanceClassMethod = owner && !subroutine.getIsStatic().value_or(false);
      if (instanceClassMethod) {
        FailureOr<Type> normalized = getNormalizedSemanticType(owner);
        std::optional<StringRef> path = subroutine.getThisVariablePath();
        bool pure = subroutine.getIsPure().value_or(false);
        if (failed(normalized) || (!path && !pure)) {
          emitError(getSemanticLocation(subroutine))
              << "instance method has no resolved this binding";
          invalid = true;
        } else {
          classThisType = *normalized;
          // Pure prototypes have no executable body and therefore no
          // elaborated `this` variable. They still need the same canonical
          // receiver position in their frozen virtual-method signature.
          classThisPath = path.value_or("__obelisk_pure_this");
        }
      }
    }

    // A continuous assignment may read its own target. Keep the ordinary net
    // bindings for reads and add role-specific driver bindings for each
    // syntactic net sink.
    if (auto found = continuousDrivers.find(unit.source);
        found != continuousDrivers.end())
      for (const DriverInfo &driver : found->second)
        captures.push_back({driver.path, driver.descriptor});

    SmallVector<Type> inputs{sim::ContextType::get(context)};
    SmallVector<DictionaryAttr> argAttrs{
        captureMetadata(builder, sim::CaptureKind::Context)};
    SmallVector<Attribute> bindings;
    for (auto indexedCapture : llvm::enumerate(captures)) {
      size_t captureIndex = indexedCapture.index();
      const auto &capture = indexedCapture.value();
      sim::CaptureKind captureKind = sim::CaptureKind::Storage;
      Type handleType;
      switch (capture.second.kind) {
      case DescriptorInfo::Kind::Storage:
        captureKind = sim::CaptureKind::Storage;
        handleType = sim::RefType::get(context, capture.second.type);
        break;
      case DescriptorInfo::Kind::Net:
        captureKind = sim::CaptureKind::Net;
        handleType = sim::NetType::get(context, capture.second.type);
        break;
      case DescriptorInfo::Kind::Driver:
        captureKind = sim::CaptureKind::Driver;
        handleType = sim::DriverType::get(context, capture.second.type);
        break;
      case DescriptorInfo::Kind::Event:
        captureKind = sim::CaptureKind::Event;
        handleType = sim::EventType::get(context);
        break;
      }
      inputs.push_back(handleType);
      DictionaryAttr metadata =
          captureMetadata(builder, captureKind, capture.second.id);
      SmallVector<NamedAttribute> metadataAttrs(metadata.begin(),
                                                metadata.end());
      if (capture.second.rootType &&
          (capture.second.viewOffset != 0 ||
           capture.second.rootType != capture.second.type)) {
        metadataAttrs.push_back(
            builder.getNamedAttr(sim::metadata::descriptorRootType,
                                 TypeAttr::get(capture.second.rootType)));
        metadataAttrs.push_back(builder.getNamedAttr(
            sim::metadata::descriptorLow,
            builder.getI64IntegerAttr(capture.second.viewOffset)));
        if (!capture.second.viewIndices.empty())
          metadataAttrs.push_back(builder.getNamedAttr(
              sim::metadata::descriptorIndices,
              builder.getDenseI64ArrayAttr(capture.second.viewIndices)));
        if (capture.second.aggregateViewType)
          metadataAttrs.push_back(builder.getNamedAttr(
              sim::metadata::descriptorAggregateType,
              TypeAttr::get(capture.second.aggregateViewType)));
        if (capture.second.packedViewOffset != 0 ||
            capture.second.aggregateViewType != capture.second.type)
          metadataAttrs.push_back(builder.getNamedAttr(
              sim::metadata::descriptorPackedLow,
              builder.getI64IntegerAttr(capture.second.packedViewOffset)));
      }
      argAttrs.push_back(builder.getDictionaryAttr(metadataAttrs));
      const DriverInfo *plannedDriver = nullptr;
      if (capture.second.kind == DescriptorInfo::Kind::Driver)
        if (auto found = continuousDrivers.find(unit.source);
            found != continuousDrivers.end())
          if (auto planned = llvm::find_if(found->second,
                                           [&](const DriverInfo &driver) {
                                             return driver.descriptor.id ==
                                                    capture.second.id;
                                           });
              planned != found->second.end())
            plannedDriver = &*planned;
      IntegerAttr lvalueNode =
          plannedDriver && plannedDriver->nodeId
              ? builder.getI64IntegerAttr(*plannedDriver->nodeId)
              : IntegerAttr{};
      bindings.push_back(sim::ArgumentBindingAttr::get(
          context, builder.getStringAttr(capture.first), captureIndex + 1,
          plannedDriver ? sim::UnitArgumentKind::LValueOnly
                        : sim::UnitArgumentKind::Direct,
          /*copyOut=*/false, lvalueNode));
    }
    for (const PreparedLocal &local : locals) {
      auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source);
      bool isReturn =
          subroutine && subroutine.getReturnVariablePath() == local.path;
      bindings.push_back(sim::LocalBindingAttr::get(
          context, builder.getStringAttr(local.path), local.type,
          local.automatic, local.patternVariable, isReturn));
    }
    for (const PreparedConstant &constant : constants)
      bindings.push_back(sim::ConstantBindingAttr::get(
          context, builder.getStringAttr(constant.path), constant.value));

    for (const PreparedLocal &local : observerLocals) {
      unsigned argument = inputs.size();
      inputs.push_back(sim::RefType::get(context, local.type));
      argAttrs.push_back(captureMetadata(builder, sim::CaptureKind::Value));
      bindings.push_back(sim::ArgumentBindingAttr::get(
          context, builder.getStringAttr(local.path), argument,
          sim::UnitArgumentKind::Direct, /*copyOut=*/false, IntegerAttr{}));
    }

    // Subroutine formals precede non-local captures in the public contract.
    // Function output and inout formals use copy-out results. Task copy-out
    // destinations are hidden reference arguments retained by the activation.
    // Only explicit ref formals otherwise preserve caller aliasing.
    if (auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source);
        subroutine && (unit.entryKind == sim::EntryKind::Function ||
                       unit.entryKind == sim::EntryKind::Task)) {
      bool dpiImport = subroutine.getIsDpiImport().value_or(false);
      bool directTask = unit.entryKind == sim::EntryKind::Task;
      SmallVector<semantic::SVFormalArgumentSymbolOp> formals;
      for (Operation *child : getChildren(unit.source))
        if (auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child))
          formals.push_back(formal);
      if (instanceClassMethod || !formals.empty()) {
        SmallVector<Type> reordered{inputs.front()};
        SmallVector<DictionaryAttr> reorderedAttrs{argAttrs.front()};
        SmallVector<Attribute> formalBindings;
        if (instanceClassMethod) {
          unsigned argument = reordered.size();
          reordered.push_back(classThisType);
          reorderedAttrs.push_back(
              captureMetadata(builder, sim::CaptureKind::Formal));
          formalBindings.push_back(sim::ArgumentBindingAttr::get(
              context, builder.getStringAttr(classThisPath), argument,
              sim::UnitArgumentKind::Direct, /*copyOut=*/false, IntegerAttr{}));
        }
        for (semantic::SVFormalArgumentSymbolOp formal : formals) {
          if (dpiImport) {
            std::optional<Type> semanticType = formal.getSemanticType();
            if (!semanticType ||
                failed(getDPIABIKind(*semanticType,
                                     getSemanticLocation(formal)))) {
              invalid = true;
              continue;
            }
          }
          FailureOr<Type> type = getNormalizedSemanticType(formal);
          if (failed(type)) {
            invalid = true;
            continue;
          }
          if (dpiImport && !sim::getPackedWidth(*type)) {
            emitError(getSemanticLocation(formal))
                << "DPI import formal type is unsupported by the initial "
                   "integral ABI";
            invalid = true;
            continue;
          }
          semantic::SVArgumentDirection direction = formal.getDirection();
          if (dpiImport && direction == semantic::SVArgumentDirection::Ref) {
            emitError(getSemanticLocation(formal))
                << "DPI ref formals are not supported; use input, output, or "
                   "inout";
            invalid = true;
            continue;
          }
          bool isRef = direction == semantic::SVArgumentDirection::Ref;
          bool indirectRef = indirectRefTasks.contains(unit.source);
          Type argumentType =
              isRef ? (directTask && !instanceClassMethod && !indirectRef
                           ? Type(sim::RefType::get(context, *type))
                           : Type(sim::ArgumentRefType::get(context, *type)))
                    : *type;
          unsigned argument = reordered.size();
          reordered.push_back(argumentType);
          reorderedAttrs.push_back(
              captureMetadata(builder, sim::CaptureKind::Formal));
          // Value formals are callee-local variables. Inputs copy in, outputs
          // and inouts copy out, and mem2reg removes the allocation whenever
          // the local does not escape. Only `ref` preserves caller aliasing.
          bool copyOut = direction == semantic::SVArgumentDirection::Out ||
                         direction == semantic::SVArgumentDirection::InOut;
          if (copyOut && !directTask)
            copyOutResultTypes.push_back(*type);
          formalBindings.push_back(sim::ArgumentBindingAttr::get(
              context, builder.getStringAttr(getHierarchyName(formal)),
              argument,
              isRef ? sim::UnitArgumentKind::Direct
                    : sim::UnitArgumentKind::FormalLocal,
              copyOut, IntegerAttr{}));
          if (directTask && copyOut) {
            unsigned destinationArgument = reordered.size();
            reordered.push_back(sim::RefType::get(context, *type));
            reorderedAttrs.push_back(
                captureMetadata(builder, sim::CaptureKind::Formal));
            formalBindings.push_back(sim::ArgumentBindingAttr::get(
                context, builder.getStringAttr(getHierarchyName(formal)),
                destinationArgument, sim::UnitArgumentKind::CopyOutDestination,
                /*copyOut=*/false, IntegerAttr{}));
          }
        }
        unsigned offset = reordered.size() - 1;
        reordered.append(inputs.begin() + 1, inputs.end());
        reorderedAttrs.append(argAttrs.begin() + 1, argAttrs.end());
        for (Attribute binding : bindings) {
          if (instanceClassMethod) {
            StringRef path =
                TypeSwitch<Attribute, StringRef>(binding)
                    .Case<sim::ArgumentBindingAttr, sim::LocalBindingAttr,
                          sim::ConstantBindingAttr>(
                        [](auto value) { return value.getPath().getValue(); })
                    .Default([](Attribute) { return StringRef{}; });
            if (path == classThisPath)
              continue;
          }
          auto argument = dyn_cast<sim::ArgumentBindingAttr>(binding);
          if (!argument) {
            formalBindings.push_back(binding);
            continue;
          }
          formalBindings.push_back(sim::ArgumentBindingAttr::get(
              context, argument.getPath(), argument.getArgument() + offset,
              argument.getKind(), argument.getCopyOut(),
              argument.getLvalueNode()));
        }
        inputs = std::move(reordered);
        argAttrs = std::move(reorderedAttrs);
        bindings = std::move(formalBindings);
      }
    }

    if (invalid)
      continue;
    SmallVector<Type> results;
    bool isVoidFunction = false;
    if (unit.entryKind == sim::EntryKind::Function) {
      auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source);
      if (!subroutine) {
        if (!isa<semantic::SVVariableSymbolOp,
                 semantic::SVClassPropertySymbolOp>(unit.source)) {
          emitError(getSemanticLocation(unit.source))
              << "synthetic zero-time function has an unsupported source";
          invalid = true;
        } else {
          isVoidFunction = true;
        }
      } else {
        bool dpiImport = subroutine.getIsDpiImport().value_or(false);
        if (subroutine.getSubroutineKind() ==
                semantic::SVSubroutineKind::Function &&
            !subroutine.getIsConstructor().value_or(false)) {
          std::optional<SymbolRefAttr> returnSymbol =
              subroutine.getReturnVariableSymbol();
          FailureOr<Type> resultType = failure();
          Type semanticResultType;
          if (returnSymbol) {
            auto symbol =
                semanticSymbols.find(returnSymbol->getLeafReference());
            if (symbol == semanticSymbols.end()) {
              emitError(getSemanticLocation(unit.source))
                  << "function return variable does not resolve";
              invalid = true;
              continue;
            }
            resultType = getNormalizedSemanticType(symbol->second);
            if (auto attr =
                    symbol->second->getAttrOfType<TypeAttr>("semantic_type"))
              semanticResultType = attr.getValue();
          } else {
            auto semanticType =
                unit.source->getAttrOfType<TypeAttr>("semantic_type");
            auto subroutineType = semanticType
                                      ? dyn_cast<semantic::SubroutineType>(
                                            semanticType.getValue())
                                      : semantic::SubroutineType{};
            auto signature =
                subroutineType
                    ? dyn_cast<FunctionType>(subroutineType.getSignature())
                    : FunctionType{};
            if (!signature || signature.getNumResults() != 1) {
              emitError(getSemanticLocation(unit.source))
                  << (dpiImport
                          ? "DPI function has no resolved return signature"
                          : "function is missing its elaborated return "
                            "variable "
                            "and resolved return signature");
              invalid = true;
              continue;
            }
            semanticResultType = signature.getResult(0);
            if (!isa<semantic::VoidType>(semanticResultType))
              resultType = normalizeSemanticType(
                  semanticResultType, getSemanticLocation(unit.source));
          }
          bool voidResult =
              isa_and_nonnull<semantic::VoidType>(semanticResultType);
          isVoidFunction = voidResult;
          if (!voidResult && failed(resultType)) {
            invalid = true;
            continue;
          }
          if (!voidResult && dpiImport && !sim::getPackedWidth(*resultType)) {
            emitError(getSemanticLocation(unit.source))
                << "DPI import return type is unsupported by the initial "
                   "integral ABI";
            invalid = true;
            continue;
          }
          if (dpiImport && !voidResult &&
              (!semanticResultType ||
               failed(getDPIABIKind(semanticResultType,
                                    getSemanticLocation(unit.source))))) {
            invalid = true;
            continue;
          }
          if (!voidResult)
            results.push_back(*resultType);
        }
      }
    } else if (unit.entryKind == sim::EntryKind::Observer) {
      Type resultType;
      if (unit.observerResult == ObserverResult::Truth ||
          unit.observerResult == ObserverResult::Event) {
        resultType = builder.getI1Type();
      } else {
        FailureOr<Type> normalized = getNormalizedSemanticType(unit.source);
        if (failed(normalized)) {
          invalid = true;
          continue;
        }
        resultType = isa<FloatType>(*normalized)
                         ? *normalized
                         : sim::getPackedScalarType(*normalized);
        if (!resultType) {
          emitError(getSemanticLocation(unit.source))
              << "observer expression does not have a packed scalar result";
          invalid = true;
          continue;
        }
      }
      results.push_back(resultType);
    }
    llvm::append_range(results, copyOutResultTypes);
    FunctionType type = FunctionType::get(context, inputs, results);
    NamedAttribute bindingAttr =
        builder.getNamedAttr(bindingsAttrName, builder.getArrayAttr(bindings));
    uint64_t timeUnitFs = designPrecisionFs;
    uint64_t timePrecisionFs = designPrecisionFs;
    uint64_t scopeID = getScopeId(unit.source);
    if (scopeID < scopes->declarations.size()) {
      sim::SimScopeDeclOp scope = scopes->declarations[scopeID];
      timeUnitFs = scope->getAttrOfType<IntegerAttr>("dpi_unit_femtoseconds")
                       .getValue()
                       .getZExtValue();
      timePrecisionFs =
          scope->getAttrOfType<IntegerAttr>("dpi_precision_femtoseconds")
              .getValue()
              .getZExtValue();
    }
    if (auto attr = unit.source->getAttrOfType<IntegerAttr>("time_unit_fs")) {
      std::optional<uint64_t> value = getUnsigned64(attr);
      if (!value) {
        emitError(getSemanticLocation(unit.source))
            << "code unit time scale does not fit an unsigned 64-bit value";
        invalid = true;
        continue;
      }
      timeUnitFs = *value;
    }
    if (auto attr =
            unit.source->getAttrOfType<IntegerAttr>("time_precision_fs")) {
      std::optional<uint64_t> value = getUnsigned64(attr);
      if (!value) {
        emitError(getSemanticLocation(unit.source))
            << "code unit time precision does not fit an unsigned 64-bit "
               "value";
        invalid = true;
        continue;
      }
      timePrecisionFs = *value;
    }
    if (timeUnitFs < designPrecisionFs || timeUnitFs % designPrecisionFs != 0) {
      emitError(getSemanticLocation(unit.source))
          << "code unit time scale is incompatible with design precision";
      invalid = true;
      continue;
    }
    NamedAttribute delayScaleAttr = builder.getNamedAttr(
        delayScaleAttrName,
        builder.getI64IntegerAttr(timeUnitFs / designPrecisionFs));
    NamedAttribute delayQuantumAttr = builder.getNamedAttr(
        delayQuantumAttrName,
        builder.getI64IntegerAttr(timePrecisionFs / designPrecisionFs));
    SmallVector<NamedAttribute> functionAttrs{
        bindingAttr, delayScaleAttr, delayQuantumAttr,
        builder.getNamedAttr("code_unit_id",
                             builder.getI64IntegerAttr(unit.id))};
    if (instanceClassMethod)
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.this_argument", builder.getI32IntegerAttr(1)));
    if (isVoidFunction)
      functionAttrs.push_back(builder.getNamedAttr("obelisk_sim.void_function",
                                                   builder.getUnitAttr()));
    if (isa<semantic::SVClassPropertySymbolOp>(unit.source))
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.static_initializer", builder.getUnitAttr()));
    if (auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source);
        subroutine && subroutine.getIsConstructor().value_or(false))
      functionAttrs.push_back(builder.getNamedAttr("obelisk_sim.constructor",
                                                   builder.getUnitAttr()));
    if (unit.entryKind == sim::EntryKind::Observer)
      functionAttrs.push_back(
          builder.getNamedAttr(observerResultAttrName,
                               builder.getI32IntegerAttr(static_cast<uint32_t>(
                                   unit.observerResult))));
    if (unit.entryKind == sim::EntryKind::Observer) {
      std::optional<unsigned> width =
          results.empty() ? std::nullopt
          : isa<FloatType>(results.front())
              ? std::optional<unsigned>(
                    cast<FloatType>(results.front()).getWidth())
              : sim::getPackedWidth(results.front());
      if (!width) {
        emitError(getSemanticLocation(unit.source))
            << "observer result width is not fixed";
        invalid = true;
        continue;
      }
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.observer_width", builder.getI32IntegerAttr(*width)));
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.observer_four_state",
          builder.getBoolAttr(isa<sim::LogicType>(results.front()))));
    }
    if (auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source);
        subroutine && subroutine.getIsDpiImport().value_or(false)) {
      SmallVector<Attribute> dpiInputs;
      SmallVector<Attribute> dpiCopyOuts;
      auto makeABI = [&](Type type, sim::DPIArgumentDirection direction,
                         Location location) -> FailureOr<sim::DPIABIAttr> {
        FailureOr<DPIABIType> classified = classifyDPIABIType(type, location);
        if (failed(classified))
          return failure();
        return sim::DPIABIAttr::get(
            context, static_cast<sim::DPIABIKind>(classified->kind), direction,
            classified->width, classified->fourState, classified->isSigned);
      };
      for (Operation *child : getChildren(unit.source)) {
        auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child);
        if (!formal)
          continue;
        std::optional<Type> semanticType = formal.getSemanticType();
        if (!semanticType) {
          formal.emitError("DPI formal has no semantic ABI type");
          invalid = true;
          continue;
        }
        sim::DPIArgumentDirection direction =
            static_cast<sim::DPIArgumentDirection>(formal.getDirection());
        FailureOr<sim::DPIABIAttr> input =
            makeABI(*semanticType, direction, getSemanticLocation(formal));
        if (failed(input)) {
          invalid = true;
          continue;
        }
        dpiInputs.push_back(*input);
        if (direction != sim::DPIArgumentDirection::Input)
          dpiCopyOuts.push_back(sim::DPIABIAttr::get(
              context, input->getKind(), sim::DPIArgumentDirection::Output,
              input->getWidth(), input->getFourState(), input->getIsSigned()));
      }
      SmallVector<Attribute> dpiSignature(dpiInputs);
      if (subroutine.getSubroutineKind() ==
          semantic::SVSubroutineKind::Function) {
        auto semanticType =
            unit.source->getAttrOfType<TypeAttr>("semantic_type");
        auto subroutineType =
            semanticType
                ? dyn_cast<semantic::SubroutineType>(semanticType.getValue())
                : semantic::SubroutineType{};
        auto sourceSignature =
            subroutineType
                ? dyn_cast<FunctionType>(subroutineType.getSignature())
                : FunctionType{};
        if (!sourceSignature || sourceSignature.getNumResults() != 1) {
          emitError(getSemanticLocation(unit.source))
              << "DPI function has no resolved result signature";
          invalid = true;
        } else if (!isa<semantic::VoidType>(sourceSignature.getResult(0))) {
          FailureOr<sim::DPIABIAttr> result = makeABI(
              sourceSignature.getResult(0), sim::DPIArgumentDirection::Result,
              getSemanticLocation(unit.source));
          if (failed(result))
            invalid = true;
          else
            dpiSignature.push_back(*result);
        }
      }
      llvm::append_range(dpiSignature, dpiCopyOuts);
      functionAttrs.push_back(builder.getNamedAttr("obelisk_sim.dpi_import",
                                                   builder.getUnitAttr()));
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.dpi_c_identifier", subroutine.getDpiCIdentifierAttr()));
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.dpi_scope_id",
          builder.getI64IntegerAttr(getScopeId(unit.source))));
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.dpi_import_id",
          builder.getI32IntegerAttr(getStableImportID(
              subroutine.getDpiCIdentifierAttr().getValue()))));
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.dpi_abi_signature", builder.getArrayAttr(dpiSignature)));
      functionAttrs.push_back(
          builder.getNamedAttr("obelisk_sim.dpi_logical_inputs",
                               builder.getI32IntegerAttr(dpiInputs.size())));
      sim::SimCodeUnitDeclOp declaration =
          codeUnitDeclarations.lookup(unit.source);
      declaration->setAttr("obelisk_sim.dpi_import", builder.getUnitAttr());
      declaration->setAttr("obelisk_sim.dpi_c_identifier",
                           subroutine.getDpiCIdentifierAttr());
      declaration->setAttr("obelisk_sim.dpi_import_id",
                           builder.getI32IntegerAttr(getStableImportID(
                               subroutine.getDpiCIdentifierAttr().getValue())));
      declaration->setAttr("obelisk_sim.dpi_abi_signature",
                           builder.getArrayAttr(dpiSignature));
      declaration->setAttr("obelisk_sim.dpi_logical_inputs",
                           builder.getI32IntegerAttr(dpiInputs.size()));
      if (subroutine.getSubroutineKind() == semantic::SVSubroutineKind::Task)
        declaration->setAttr("obelisk_sim.dpi_task", builder.getUnitAttr());
      if (subroutine.getIsPure().value_or(false))
        functionAttrs.push_back(builder.getNamedAttr("obelisk_sim.dpi_pure",
                                                     builder.getUnitAttr()));
      if (subroutine.getIsDpiContext().value_or(false))
        functionAttrs.push_back(builder.getNamedAttr("obelisk_sim.dpi_context",
                                                     builder.getUnitAttr()));
      if (subroutine.getSubroutineKind() == semantic::SVSubroutineKind::Task)
        functionAttrs.push_back(builder.getNamedAttr("obelisk_sim.dpi_task",
                                                     builder.getUnitAttr()));
    }
    if (isa<semantic::SVPortConnectionOp>(unit.source))
      functionAttrs.push_back(
          builder.getNamedAttr("internal", builder.getUnitAttr()));
    if (auto primitive =
            unit.source->getAttrOfType<StringAttr>("primitive_name"))
      functionAttrs.push_back(
          builder.getNamedAttr("obelisk_sim.primitive_name", primitive));
    StringRef hierarchy = isa<semantic::SVPortConnectionOp>(unit.source)
                              ? getHierarchyName(unit.source->getParentOp())
                              : getHierarchyName(unit.source);
    if (!hierarchy.empty())
      functionAttrs.push_back(builder.getNamedAttr(
          sim::metadata::hierarchicalName, builder.getStringAttr(hierarchy)));
    bool programDomain = isProgramCodeUnit(unit.source);
    functionAttrs.push_back(builder.getNamedAttr(
        "home_region", sim::EventRegionAttr::get(
                           context, programDomain ? sim::EventRegion::Reactive
                                                  : sim::EventRegion::Active)));
    functionAttrs.push_back(builder.getNamedAttr(
        "domain", sim::ExecutionDomainAttr::get(
                      context, programDomain ? sim::ExecutionDomain::Program
                                             : sim::ExecutionDomain::Design)));
    unit.function = sim::SimFuncOp::create(
        builder, getSemanticLocation(unit.source), unit.symbol, type,
        unit.entryKind, functionAttrs, argAttrs);
    SymbolTable::setSymbolVisibility(unit.function,
                                     SymbolTable::Visibility::Private);

    if (auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source);
        subroutine && subroutine.getIsDpiImport().value_or(false)) {
      unit.function.getBody().getBlocks().clear();
      continue;
    }

    OpBuilder bodyBuilder =
        OpBuilder::atBlockEnd(&unit.function.getBody().front());
    if (unit.entryKind == sim::EntryKind::Observer ||
        isa<semantic::SVPortConnectionOp>(unit.source)) {
      bodyBuilder.clone(*unit.source);
    } else if (isa<semantic::SVVariableSymbolOp,
                   semantic::SVClassPropertySymbolOp>(unit.source)) {
      SmallVector<Operation *> initializer = getChildren(unit.source);
      auto memberOrdinals = unit.source->getAttrOfType<DenseI64ArrayAttr>(
          "obelisk.aggregate_member_initializer_ordinals");
      if (memberOrdinals &&
          initializer.size() != static_cast<size_t>(memberOrdinals.size())) {
        emitError(getSemanticLocation(unit.source))
            << "aggregate member initializer metadata has "
            << memberOrdinals.size() << " ordinals but " << initializer.size()
            << " expressions";
        invalid = true;
      } else if (memberOrdinals) {
        for (auto [expression, ordinal] :
             llvm::zip_equal(initializer, memberOrdinals.asArrayRef())) {
          Operation *cloned = bodyBuilder.clone(*expression);
          cloned->setAttr("obelisk_sim.initialize_static",
                          builder.getStringAttr(getHierarchyName(unit.source)));
          cloned->setAttr("obelisk_sim.initialize_subelement",
                          builder.getI64IntegerAttr(ordinal));
        }
      } else if (initializer.size() != 1) {
        emitError(getSemanticLocation(unit.source))
            << "design initializer must have one expression";
        invalid = true;
      } else {
        Operation *cloned = bodyBuilder.clone(*initializer.front());
        cloned->setAttr("obelisk_sim.initialize_static",
                        builder.getStringAttr(getHierarchyName(unit.source)));
      }
    } else if (isa<semantic::SVNetSymbolOp>(unit.source)) {
      SmallVector<Operation *> initializer = getChildren(unit.source);
      if (initializer.size() != 1) {
        emitError(getSemanticLocation(unit.source))
            << "net initializer must have one expression";
        invalid = true;
      } else {
        Operation *cloned = bodyBuilder.clone(*initializer.front());
        cloned->setAttr("obelisk_sim.initialize_net",
                        builder.getStringAttr(getHierarchyName(unit.source)));
      }
    } else {
      auto clonePropertyInitializers = [&] {
        auto owner = dyn_cast_or_null<semantic::SVClassTypeOp>(
            unit.source->getParentOp());
        if (!owner)
          return;
        for (Operation *member : getChildren(owner)) {
          auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(member);
          if (!property ||
              property.getLifetime() == semantic::SVVariableLifetime::Static)
            continue;
          SmallVector<Operation *> initializer = getChildren(property);
          if (initializer.empty())
            continue;
          Operation *cloned = bodyBuilder.clone(*initializer.front());
          if (FlatSymbolRefAttr field = classFieldSymbols.lookup(property))
            cloned->setAttr("obelisk_sim.initialize_field", field);
        }
      };
      auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source);
      auto owner = subroutine ? dyn_cast_or_null<semantic::SVClassTypeOp>(
                                    unit.source->getParentOp())
                              : semantic::SVClassTypeOp{};
      bool constructor =
          subroutine && subroutine.getIsConstructor().value_or(false);
      bool initialized = false;
      if (constructor && owner && !owner.getBaseClass()) {
        clonePropertyInitializers();
        initialized = true;
      }
      for (Operation *child : getChildren(unit.source)) {
        if (isa<semantic::SVFormalArgumentSymbolOp,
                semantic::SVVariableSymbolOp,
                semantic::SVStatementBlockSymbolOp>(child))
          continue;
        bodyBuilder.clone(*child);
        if (constructor && owner && owner.getBaseClass() && !initialized) {
          bool containsSuper = false;
          child->walk([&](semantic::SVNewClassExpressionOp construct) {
            containsSuper |= construct.getIsSuperClass();
          });
          if (containsSuper) {
            clonePropertyInitializers();
            initialized = true;
          }
        }
      }
      if (constructor && !initialized)
        clonePropertyInitializers();
    }
    // Materialize declaration initializers before annotating calls. A walk is
    // not required to revisit operations inserted beneath the current node,
    // so doing both in one walk could leave calls in local initializers
    // without their frozen callee contract.
    unit.function.walk([&](semantic::SVVariableDeclStatementOp declaration) {
      auto symbol = semanticSymbols.find(
          declaration.getReferencedSymbol().getLeafReference());
      if (symbol == semanticSymbols.end())
        return;
      SmallVector<Operation *> initializer = getChildren(symbol->second);
      if (initializer.empty())
        return;
      OpBuilder declarationBuilder =
          OpBuilder::atBlockEnd(&declaration->getRegion(0).front());
      auto memberOrdinals = symbol->second->getAttrOfType<DenseI64ArrayAttr>(
          "obelisk.aggregate_member_initializer_ordinals");
      if (!memberOrdinals) {
        declarationBuilder.clone(*initializer.front());
        return;
      }
      if (initializer.size() != static_cast<size_t>(memberOrdinals.size())) {
        emitError(getSemanticLocation(symbol->second))
            << "aggregate member initializer metadata has "
            << memberOrdinals.size() << " ordinals but " << initializer.size()
            << " expressions";
        invalid = true;
        return;
      }
      declaration->setAttr("obelisk_sim.aggregate_member_initializers",
                           builder.getUnitAttr());
      for (auto [expression, ordinal] :
           llvm::zip_equal(initializer, memberOrdinals.asArrayRef())) {
        Operation *cloned = declarationBuilder.clone(*expression);
        cloned->setAttr("obelisk_sim.initialize_subelement",
                        builder.getI64IntegerAttr(ordinal));
      }
    });
    unit.function.walk([&](Operation *nested) {
      if (auto call = dyn_cast<semantic::SVCallExpressionOp>(nested)) {
        freezeCallContract(call);
        return;
      }
      if (auto named = dyn_cast<semantic::SVNamedValueExpressionOp>(nested)) {
        auto symbol = semanticSymbols.find(
            named.getReferencedSymbol().getLeafReference());
        if (symbol == semanticSymbols.end())
          return;
        if (isa<semantic::SVParameterSymbolOp, semantic::SVEnumValueSymbolOp,
                semantic::SVSpecparamSymbolOp>(symbol->second))
          if (auto constant =
                  symbol->second->getAttrOfType<StringAttr>("constant_value"))
            named->setAttr("obelisk_sim.constant_value", constant);
        auto field = classFieldSymbols.find(symbol->second);
        auto property =
            dyn_cast<semantic::SVClassPropertySymbolOp>(symbol->second);
        if (field != classFieldSymbols.end() &&
            (!property ||
             property.getLifetime() != semantic::SVVariableLifetime::Static))
          named->setAttr("obelisk_sim.class_field", field->second);
        return;
      }
      if (auto hierarchical =
              dyn_cast<semantic::SVHierarchicalValueExpressionOp>(nested)) {
        auto symbol = semanticSymbols.find(
            hierarchical.getReferencedSymbol().getLeafReference());
        if (symbol != semanticSymbols.end() &&
            isa<semantic::SVParameterSymbolOp, semantic::SVEnumValueSymbolOp,
                semantic::SVSpecparamSymbolOp>(symbol->second))
          if (auto constant =
                  symbol->second->getAttrOfType<StringAttr>("constant_value"))
            hierarchical->setAttr("obelisk_sim.constant_value", constant);
        return;
      }
      if (auto member =
              dyn_cast<semantic::SVMemberAccessExpressionOp>(nested)) {
        auto symbol = semanticSymbols.find(
            member.getReferencedSymbol().getLeafReference());
        if (symbol == semanticSymbols.end())
          return;
        if (isa<semantic::SVParameterSymbolOp, semantic::SVEnumValueSymbolOp,
                semantic::SVSpecparamSymbolOp>(symbol->second)) {
          if (auto constant =
                  symbol->second->getAttrOfType<StringAttr>("constant_value"))
            member->setAttr("obelisk_sim.constant_value", constant);
          return;
        }
        auto field = classFieldSymbols.find(symbol->second);
        auto property =
            dyn_cast<semantic::SVClassPropertySymbolOp>(symbol->second);
        if (field != classFieldSymbols.end() &&
            (!property ||
             property.getLifetime() != semantic::SVVariableLifetime::Static))
          member->setAttr("obelisk_sim.class_field", field->second);
        return;
      }
    });
    if (unit.entryKind != sim::EntryKind::Function &&
        unit.entryKind != sim::EntryKind::Observer) {
      sim::SimReturnOp::create(bodyBuilder, getSemanticLocation(unit.source),
                               ValueRange{});
    } else if (type.getNumResults() != 0) {
      auto placeholder = UnrealizedConversionCastOp::create(
          bodyBuilder, getSemanticLocation(unit.source), type.getResults(),
          ValueRange{});
      placeholder->setAttr(placeholderAttrName, builder.getUnitAttr());
      sim::SimReturnOp::create(bodyBuilder, getSemanticLocation(unit.source),
                               placeholder.getResults());
    } else {
      sim::SimReturnOp::create(bodyBuilder, getSemanticLocation(unit.source),
                               ValueRange{});
    }
  }
  if (invalid)
    return abort();

  llvm::DenseMap<Operation *, sim::SimFuncOp> unitFunctions;
  for (PreparedUnit &unit : units)
    if (unit.function)
      unitFunctions[unit.source] = unit.function;

  auto constructorSymbolFor =
      [&](semantic::SVClassTypeOp classType) -> FlatSymbolRefAttr {
    if (FlatSymbolRefAttr implicit =
            implicitConstructorSymbols.lookup(classType))
      return implicit;
    for (Operation *child : getChildren(classType)) {
      semantic::SVSubroutineSymbolOp method = getClassMethod(child);
      if (!method || !method.getIsConstructor().value_or(false))
        continue;
      auto found = directCalleeNames.find(method);
      if (found != directCalleeNames.end())
        return FlatSymbolRefAttr::get(context, found->second);
    }
    return {};
  };

  // Slang omits an executable subroutine node for an implicit constructor.
  // Materialize that lifecycle edge explicitly so `new` without a declared
  // constructor still performs base construction and declaration-order
  // property initialization.
  for (semantic::SVClassTypeOp classType : classSources) {
    FlatSymbolRefAttr constructor =
        implicitConstructorSymbols.lookup(classType);
    if (!constructor)
      continue;
    Type receiverType = sim::ClassHandleType::get(
        context, FlatSymbolRefAttr::get(
                     context, classSymbols.lookup(classType).getValue()));
    FunctionType type = FunctionType::get(
        context, {sim::ContextType::get(context), receiverType}, {});
    SmallVector<DictionaryAttr> argAttrs{
        captureMetadata(builder, sim::CaptureKind::Context),
        captureMetadata(builder, sim::CaptureKind::Formal)};
    std::string hierarchy =
        (getHierarchyName(classType) + Twine("::new")).str();
    uint64_t codeUnitID = stableCodeUnitID(hierarchy);
    sim::SimCodeUnitDeclOp::create(
        builder, getSemanticLocation(classType), codeUnitID, uint64_t{0},
        sim::EntryKind::Function, builder.getStringAttr(hierarchy),
        builder.getStringAttr("implicit constructor"), UnitAttr{});
    SmallVector<NamedAttribute> attrs{
        builder.getNamedAttr("code_unit_id",
                             builder.getI64IntegerAttr(codeUnitID)),
        builder.getNamedAttr("obelisk_sim.this_argument",
                             builder.getI32IntegerAttr(1)),
        builder.getNamedAttr("obelisk_sim.constructor", builder.getUnitAttr()),
        builder.getNamedAttr(
            "home_region",
            sim::EventRegionAttr::get(context, sim::EventRegion::Active)),
        builder.getNamedAttr("domain",
                             sim::ExecutionDomainAttr::get(
                                 context, sim::ExecutionDomain::Design)),
        builder.getNamedAttr(sim::metadata::hierarchicalName,
                             builder.getStringAttr(hierarchy))};
    sim::SimFuncOp function = sim::SimFuncOp::create(
        builder, getSemanticLocation(classType), constructor.getValue(), type,
        sim::EntryKind::Function, attrs, argAttrs);
    SymbolTable::setSymbolVisibility(function,
                                     SymbolTable::Visibility::Private);
    OpBuilder bodyBuilder = OpBuilder::atBlockEnd(&function.getBody().front());
    Value receiver = function.getBody().front().getArgument(1);

    if (std::optional<Type> baseType = classType.getBaseClass()) {
      auto baseHandle = dyn_cast<semantic::ClassHandleType>(*baseType);
      auto base = baseHandle ? semanticClasses.find(
                                   baseHandle.getClassName().getLeafReference())
                             : semanticClasses.end();
      FlatSymbolRefAttr baseConstructor =
          base == semanticClasses.end() ? FlatSymbolRefAttr{}
                                        : constructorSymbolFor(base->second);
      if (!baseConstructor) {
        emitError(getSemanticLocation(classType))
            << "implicit constructor cannot resolve its base constructor";
        invalid = true;
      } else {
        Type baseReceiverType = sim::ClassHandleType::get(
            context,
            FlatSymbolRefAttr::get(
                context, classSymbols.lookup(base->second).getValue()));
        semantic::SVCallExpressionOp baseCall;
        for (Operation *child : getChildren(classType)) {
          auto candidate = dyn_cast<semantic::SVCallExpressionOp>(child);
          Operation *target =
              candidate ? resolveDirectCallee(candidate) : nullptr;
          if (target &&
              directCalleeNames.lookup(target) == baseConstructor.getValue()) {
            baseCall = candidate;
            break;
          }
        }
        if (baseCall) {
          auto cloned =
              cast<semantic::SVCallExpressionOp>(bodyBuilder.clone(*baseCall));
          freezeCallContract(cloned);
          // An extends-clause constructor call is represented as an ordinary
          // semantic call. It still uses the current object as the base-class
          // receiver, just like an explicit super.new call.
          cloned->setAttr("obelisk_sim.class_super", builder.getUnitAttr());
        } else {
          Value baseReceiver = sim::SimClassCastOp::create(
              bodyBuilder, getSemanticLocation(classType), baseReceiverType,
              receiver);
          sim::SimClassDirectCallOp::create(
              bodyBuilder, getSemanticLocation(classType), TypeRange{},
              baseConstructor, baseReceiver, ValueRange{});
        }
      }
    }

    for (Operation *member : getChildren(classType)) {
      auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(member);
      if (!property ||
          property.getLifetime() == semantic::SVVariableLifetime::Static)
        continue;
      SmallVector<Operation *> initializer = getChildren(property);
      if (initializer.empty())
        continue;
      Operation *cloned = bodyBuilder.clone(*initializer.front());
      if (FlatSymbolRefAttr field = classFieldSymbols.lookup(property))
        cloned->setAttr("obelisk_sim.initialize_field", field);
    }
    sim::SimReturnOp::create(bodyBuilder, getSemanticLocation(classType),
                             ValueRange{});
  }
  if (invalid)
    return abort();

  for (semantic::SVClassTypeOp classType : classSources) {
    for (Operation *child : getChildren(classType)) {
      auto method = getClassMethod(child);
      if (!method || method.getIsBuiltin().value_or(false))
        continue;
      bool isPure = method.getIsPure().value_or(false);
      sim::SimFuncOp typedImplementation = unitFunctions.lookup(method);
      if (!typedImplementation && !isPure) {
        method.emitError("concrete class method has no executable code unit");
        invalid = true;
        continue;
      }
      FlatSymbolRefAttr methodSymbol = classMethodSymbols.lookup(method);
      if (!methodSymbol) {
        method.emitError("class method has no frozen descriptor symbol");
        invalid = true;
        continue;
      }
      bool isVirtual = method.getIsVirtual().value_or(false);
      IntegerAttr slot =
          isVirtual
              ? builder.getI64IntegerAttr(virtualMethodSlots.lookup(method))
              : IntegerAttr{};
      IntegerAttr signatureID =
          isVirtual ? builder.getI64IntegerAttr(
                          virtualMethodSignatures.lookup(method))
                    : IntegerAttr{};
      sim::SimFuncOp implementation =
          isPure ? sim::SimFuncOp{} : typedImplementation;
      FlatSymbolRefAttr implementationRef =
          implementation
              ? FlatSymbolRefAttr::get(context, implementation.getSymName())
              : FlatSymbolRefAttr{};
      Type functionType = typedImplementation
                              ? Type(typedImplementation.getFunctionType())
                              : Type(FunctionType::get(context, {}, {}));
      sim::SimClassMethodDeclOp::create(
          builder, getSemanticLocation(method),
          builder.getStringAttr(methodSymbol.getValue()),
          FlatSymbolRefAttr::get(context,
                                 classSymbols.lookup(classType).getValue()),
          TypeAttr::get(functionType), slot, signatureID, implementationRef,
          builder.getBoolAttr(isVirtual), builder.getBoolAttr(isPure),
          builder.getBoolAttr(method.getIsStatic().value_or(false)),
          builder.getBoolAttr(method.getSubroutineKind() ==
                              semantic::SVSubroutineKind::Task),
          builder.getBoolAttr(method.getIsFinal().value_or(false)),
          builder.getStringAttr(getDebugName(method)));
    }
  }
  if (invalid)
    return abort();

  OpBuilder rootBuilder =
      OpBuilder::atBlockEnd(&rootInitializer.getBody().front());
  Value simContext = rootInitializer.getBody().front().getArgument(0);
  auto materializeRootOperands =
      [&](PreparedUnit &unit) -> FailureOr<SmallVector<Value>> {
    SmallVector<Value> operands{simContext};
    for (unsigned index = 1; index < unit.function.getNumArguments(); ++index) {
      DictionaryAttr attrs = unit.function.getArgAttrDict(index);
      auto kind = dyn_cast_or_null<sim::CaptureKindAttr>(
          attrs ? attrs.get(captureKindAttrName) : Attribute{});
      auto descriptor = attrs ? attrs.getAs<IntegerAttr>(descriptorIdAttrName)
                              : IntegerAttr{};
      if (!kind || !descriptor) {
        unit.function.emitError() << "root-invoked argument #" << index
                                  << " has no descriptor capture metadata";
        return failure();
      }
      uint64_t id = descriptor.getValue().getZExtValue();
      Type type = unit.function.getArgumentTypes()[index];
      Location loc = unit.function.getLoc();
      switch (kind.getValue()) {
      case sim::CaptureKind::Storage: {
        auto rootTypeAttr =
            attrs.getAs<TypeAttr>(sim::metadata::descriptorRootType);
        Type contextType =
            rootTypeAttr
                ? Type(sim::RefType::get(context, rootTypeAttr.getValue()))
                : type;
        Value storage = sim::SimContextStorageOp::create(
                            rootBuilder, loc, contextType, simContext,
                            rootBuilder.getI64IntegerAttr(id))
                            .getResult();
        if (rootTypeAttr) {
          auto low = attrs.getAs<IntegerAttr>(sim::metadata::descriptorLow);
          if (!low) {
            unit.function.emitError()
                << "view capture is missing its descriptor offset";
            return failure();
          }
          if (auto indices = attrs.getAs<DenseI64ArrayAttr>(
                  sim::metadata::descriptorIndices)) {
            auto aggregateType =
                attrs.getAs<TypeAttr>(sim::metadata::descriptorAggregateType);
            if (!aggregateType) {
              unit.function.emitError()
                  << "aggregate view capture is missing its result type";
              return failure();
            }
            Type resultType =
                sim::RefType::get(context, aggregateType.getValue());
            storage = sim::SimRefSubelementOp::create(
                          rootBuilder, loc, resultType, storage, indices)
                          .getResult();
          }
          if (storage.getType() != type) {
            auto packedLow =
                attrs.getAs<IntegerAttr>(sim::metadata::descriptorPackedLow);
            if (!packedLow) {
              unit.function.emitError()
                  << "packed view capture is missing its bit offset";
              return failure();
            }
            storage = sim::SimRefExtractOp::create(rootBuilder, loc, type,
                                                   storage, packedLow)
                          .getResult();
          }
        }
        operands.push_back(storage);
        break;
      }
      case sim::CaptureKind::Net:
        operands.push_back(
            sim::SimContextNetOp::create(rootBuilder, loc, type, simContext,
                                         rootBuilder.getI64IntegerAttr(id))
                .getResult());
        break;
      case sim::CaptureKind::Driver:
        operands.push_back(
            sim::SimContextDriverOp::create(rootBuilder, loc, type, simContext,
                                            rootBuilder.getI64IntegerAttr(id))
                .getResult());
        break;
      case sim::CaptureKind::Event:
        operands.push_back(
            sim::SimContextEventOp::create(rootBuilder, loc, type, simContext,
                                           rootBuilder.getI64IntegerAttr(id))
                .getResult());
        break;
      case sim::CaptureKind::Context:
      case sim::CaptureKind::Formal:
      case sim::CaptureKind::Value:
        unit.function.emitError()
            << "root-invoked argument #" << index
            << " cannot be materialized by the root initializer";
        return failure();
      }
    }
    return operands;
  };

  // Design variable and static class-property initializers are zero-time
  // private functions. Run all of them before creating any process so initial
  // blocks observe fully initialized static state.
  for (PreparedUnit &unit : units) {
    auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(unit.source);
    bool initializer = isa<semantic::SVVariableSymbolOp>(unit.source) ||
                       (property && property.getLifetime() ==
                                        semantic::SVVariableLifetime::Static);
    if (!initializer)
      continue;
    FailureOr<SmallVector<Value>> operands = materializeRootOperands(unit);
    if (failed(operands))
      return abort();
    sim::SimCallOp::create(rootBuilder, unit.function.getLoc(), TypeRange{},
                           FlatSymbolRefAttr::get(context, unit.symbol),
                           *operands, ArrayAttr{}, ArrayAttr{});
  }

  auto spawnRootUnit = [&](PreparedUnit &unit) -> LogicalResult {
    FailureOr<SmallVector<Value>> operands = materializeRootOperands(unit);
    if (failed(operands))
      return failure();
    sim::SimSpawnOp::create(rootBuilder, unit.function.getLoc(),
                            sim::ProcessType::get(context),
                            FlatSymbolRefAttr::get(context, unit.symbol),
                            *operands, ArrayAttr{}, ArrayAttr{});
    return success();
  };
  auto isRootSpawned = [](const PreparedUnit &unit) {
    return unit.entryKind != sim::EntryKind::Function &&
           unit.entryKind != sim::EntryKind::Task &&
           unit.entryKind != sim::EntryKind::Observer;
  };
  auto isRepeatingProcess = [](const PreparedUnit &unit) {
    return unit.entryKind == sim::EntryKind::Always ||
           unit.entryKind == sim::EntryKind::AlwaysComb ||
           unit.entryKind == sim::EntryKind::AlwaysLatch ||
           unit.entryKind == sim::EntryKind::AlwaysFF;
  };

  // Establish repeating-process sensitivities before initial processes can
  // trigger events or mutate their watched values. The standard permits
  // either Active-region order at time zero; choosing this deterministic
  // order matches established simulator behavior and prevents a source-order
  // race from losing an event before an `always @(event)` has suspended.
  for (PreparedUnit &unit : units)
    if (isRootSpawned(unit) && isRepeatingProcess(unit))
      if (failed(spawnRootUnit(unit)))
        return abort();
  for (PreparedUnit &unit : units) {
    if (!isRootSpawned(unit) || isRepeatingProcess(unit))
      continue;
    if (failed(spawnRootUnit(unit)))
      return abort();
  }
  sim::SimReturnOp::create(rootBuilder, module.getLoc(), ValueRange{});
}

} // namespace
} // namespace obelisk
