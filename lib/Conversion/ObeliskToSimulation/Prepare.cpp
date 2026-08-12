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
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/FormatVariadic.h"

#include <functional>
#include <limits>
#include <map>
#include <memory>

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

/// Constraint branches are evaluated eagerly while building the candidate
/// predicate. Keep that legal by admitting only expression nodes that are
/// intrinsically total and side-effect-free. Admitted constraint functions are
/// expanded before this predicate runs, while assignments remain outside the
/// expression boundary. Partial arithmetic is admitted here only so the
/// encoder can apply its operand-sensitive legality checks; shifts have total
/// lowering that cannot introduce poison during exhaustive search.
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
    if (isCompileTimeOnlyInstanceMember(op))
      return;
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

  // Enum identity is intentionally erased when semantic values are normalized
  // to executable packed types. Freeze the frontend's exact enumerator
  // inventory on each dynamic cast before code units are cloned so lowering
  // can preserve the LRM membership check without consulting semantic symbols.
  semanticRoot->walk([&](semantic::SVCallExpressionOp call) {
    if (call.getCalleeName() != "$cast")
      return;
    SmallVector<Operation *> arguments = getChildren(call);
    if (arguments.size() != 2)
      return;
    std::optional<semantic::SVDynamicCastKind> kind = call.getDynamicCastKind();
    if (!kind) {
      emitError(getSemanticLocation(call))
          << "$cast has no valid elaborated cast classification";
      invalid = true;
      return;
    }
    if (isa<semantic::SVExpressionStatementOp>(call->getParentOp()))
      call->setAttr(dynamicCastTaskAttrName, UnitAttr::get(context));
    if (*kind != semantic::SVDynamicCastKind::EnumMembership)
      return;
    Operation *destination = arguments.front();
    if (auto assignment =
            dyn_cast<semantic::SVAssignmentExpressionOp>(destination)) {
      SmallVector<Operation *> children = getChildren(assignment);
      if (children.size() == 2 &&
          isa<semantic::SVEmptyArgumentExpressionOp>(children[1]))
        destination = children.front();
    }
    auto semanticType = destination->getAttrOfType<TypeAttr>("semantic_type");
    if (!semanticType || !isa<semantic::EnumType>(semanticType.getValue())) {
      emitError(getSemanticLocation(call))
          << "enum $cast destination has no elaborated enum type";
      invalid = true;
      return;
    }
    ArrayAttr spellings = call.getDynamicCastEnumValuesAttr();
    FailureOr<Type> normalized = getNormalizedSemanticType(destination);
    if (failed(normalized)) {
      invalid = true;
      return;
    }
    Type scalar = sim::getPackedScalarType(*normalized);
    std::optional<unsigned> width =
        scalar ? sim::getPackedWidth(scalar) : std::nullopt;
    if (!spellings || spellings.empty() || !width) {
      emitError(getSemanticLocation(call))
          << "enum $cast has no frozen enumerator inventory";
      invalid = true;
      return;
    }
    SmallVector<Attribute> values;
    values.reserve(spellings.size());
    unsigned enumWidth = width.value();
    Type planeType = IntegerType::get(context, enumWidth);
    for (Attribute attribute : spellings) {
      auto spelling = dyn_cast<StringAttr>(attribute);
      FailureOr<ParsedConstant> parsed =
          spelling ? parseSVInteger(spelling.getValue(), enumWidth,
                                    getSemanticLocation(call))
                   : FailureOr<ParsedConstant>(failure());
      if (failed(parsed)) {
        emitError(getSemanticLocation(call))
            << "enum $cast has a malformed enumerator value";
        invalid = true;
        return;
      }
      ArrayAttr planes = ArrayAttr::get(
          context, {IntegerAttr::get(planeType, parsed->value),
                    IntegerAttr::get(planeType, parsed->unknown)});
      values.push_back(sim::FrozenConstantAttr::get(
          context, *normalized, planes,
          isSignedSemanticType(semanticType.getValue())));
    }
    call->setAttr(dynamicCastEnumValuesAttrName,
                  ArrayAttr::get(context, values));
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
        else if (auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(op);
                 subroutine && subroutine.getSubroutineKind() ==
                                   semantic::SVSubroutineKind::Task)
          path = subroutine.getHierarchicalNameAttr();
      } else if (auto declaration =
                     dyn_cast<semantic::SVVariableDeclStatementOp>(op)) {
        path = StringAttr::get(context, declaration.getReferencedPath());
      }
      if (path && ids.contains(path.getValue()))
        op->setAttr(attrName, IntegerAttr::get(IntegerType::get(context, 64),
                                               ids.lookup(path.getValue())));
    });
  };
  assignPathIDs(controlPaths, "obelisk_sim.control_target_id");
  assignPathIDs(staticPaths, "obelisk_sim.static_site_id");

  struct AssertionInventoryEntry {
    Operation *operation = nullptr;
    std::string path;
    std::string scope;
    uint64_t id = 0;
    uint32_t scopeDepth = 0;
    uint32_t assertionType = 0;
    uint32_t directiveType = 0;
    bool supported = false;
  };
  SmallVector<AssertionInventoryEntry> assertionInventory;
  llvm::StringMap<uint32_t> instanceScopeDepths;
  semanticRoot->walk([&](semantic::SVInstanceBodySymbolOp body) {
    if (isCompileTimeOnlyInstanceMember(body))
      return;
    auto path = body->getAttrOfType<StringAttr>("hierarchical_name");
    if (!path)
      return;
    uint32_t depth = 0;
    for (Operation *parent = body; parent; parent = parent->getParentOp())
      depth += isa<semantic::SVInstanceBodySymbolOp>(parent);
    instanceScopeDepths[path.getValue()] = depth;
  });

  auto enclosingInstance =
      [&](Operation *operation) -> semantic::SVInstanceBodySymbolOp {
    return operation
               ? operation->getParentOfType<semantic::SVInstanceBodySymbolOp>()
               : semantic::SVInstanceBodySymbolOp{};
  };
  auto assertionPath = [&](Operation *operation, StringRef scope) {
    if (auto block = dyn_cast_or_null<semantic::SVBlockStatementOp>(
            operation ? operation->getParentOp() : nullptr)) {
      SmallVector<Operation *> contents = getChildren(block);
      if (contents.size() == 1 && contents.front() == operation)
        if (auto path = block.getBlockPathAttr())
          return path.getValue().str();
    }
    auto node = operation ? operation->getAttrOfType<IntegerAttr>("node_id")
                          : IntegerAttr{};
    return (Twine(scope) + ".$assert$" +
            Twine(node ? node.getValue().getZExtValue() : 0))
        .str();
  };
  auto directiveMask = [](semantic::SVAssertionKind kind) -> uint32_t {
    switch (kind) {
    case semantic::SVAssertionKind::Assert:
      return 1;
    case semantic::SVAssertionKind::CoverProperty:
    case semantic::SVAssertionKind::CoverSequence:
      return 2;
    case semantic::SVAssertionKind::Assume:
    case semantic::SVAssertionKind::Restrict:
      return 4;
    case semantic::SVAssertionKind::Expect:
      return 1;
    }
    llvm_unreachable("unhandled assertion directive kind");
  };

  semanticRoot->walk([&](semantic::SVImmediateAssertionStatementOp assertion) {
    semantic::SVInstanceBodySymbolOp body = enclosingInstance(assertion);
    auto scopeAttr = body ? body->getAttrOfType<StringAttr>("hierarchical_name")
                          : StringAttr{};
    std::string scope = scopeAttr ? scopeAttr.getValue().str() : std::string{};
    uint32_t type =
        assertion.getIsDeferred() ? (assertion.getIsFinal() ? 8u : 4u) : 2u;
    assertionInventory.push_back(
        {assertion, assertionPath(assertion, scope), scope, 0,
         instanceScopeDepths.lookup(scope), type,
         directiveMask(assertion.getAssertionKind()), true});
  });
  semanticRoot->walk([&](semantic::SVConcurrentAssertionStatementOp assertion) {
    semantic::SVInstanceBodySymbolOp body = enclosingInstance(assertion);
    auto scopeAttr = body ? body->getAttrOfType<StringAttr>("hierarchical_name")
                          : StringAttr{};
    std::string scope = scopeAttr ? scopeAttr.getValue().str() : std::string{};
    uint32_t type =
        assertion.getAssertionKind() == semantic::SVAssertionKind::Expect ? 16u
                                                                          : 1u;
    assertionInventory.push_back(
        {assertion, assertionPath(assertion, scope), scope, 0,
         instanceScopeDepths.lookup(scope), type,
         directiveMask(assertion.getAssertionKind()), false});
  });

  llvm::sort(assertionInventory, [](const AssertionInventoryEntry &left,
                                    const AssertionInventoryEntry &right) {
    return left.path < right.path;
  });
  uint64_t nextAssertionID = controlPaths.size() + 1;
  for (AssertionInventoryEntry &entry : assertionInventory) {
    if (!entry.supported)
      continue;
    if (auto block = dyn_cast_or_null<semantic::SVBlockStatementOp>(
            entry.operation->getParentOp()))
      if (auto target = block->getAttrOfType<IntegerAttr>(
              "obelisk_sim.control_target_id"))
        entry.id = target.getValue().getZExtValue();
    if (entry.id == 0)
      entry.id = nextAssertionID++;
  }

  auto literalControlValue = [&](Operation *argument,
                                 StringRef role) -> std::optional<uint64_t> {
    auto spelling = argument
                        ? argument->getAttrOfType<StringAttr>("constant_value")
                        : StringAttr{};
    FailureOr<ParsedConstant> parsed =
        spelling ? parseSVInteger(spelling.getValue(), 64,
                                  getSemanticLocation(argument))
                 : FailureOr<ParsedConstant>(failure());
    if (failed(parsed) || !parsed->unknown.isZero()) {
      emitError(getSemanticLocation(argument))
          << "assertion-control " << role << " must be a fixed integer literal";
      invalid = true;
      return std::nullopt;
    }
    return parsed->value.getZExtValue();
  };

  semanticRoot->walk([&](semantic::SVCallExpressionOp call) {
    StringRef name = call.getCalleeName();
    bool attemptShorthand =
        name == "$asserton" || name == "$assertoff" || name == "$assertkill";
    uint32_t shorthandAction = llvm::StringSwitch<uint32_t>(name)
                                   .Case("$asserton", 3)
                                   .Case("$assertoff", 4)
                                   .Case("$assertkill", 5)
                                   .Case("$assertpasson", 6)
                                   .Case("$assertpassoff", 7)
                                   .Case("$assertfailon", 8)
                                   .Case("$assertfailoff", 9)
                                   .Case("$assertnonvacuouson", 10)
                                   .Case("$assertvacuousoff", 11)
                                   .Default(0);
    bool shorthand = shorthandAction != 0;
    if (!shorthand && name != "$assertcontrol")
      return;
    SmallVector<Operation *> arguments = getChildren(call);
    uint32_t action = shorthandAction;
    uint64_t assertionTypes = attemptShorthand ? 15 : 31;
    uint64_t directiveTypes = 7;
    uint64_t levels = 0;
    size_t firstSelector = 0;
    bool selectCurrentScope = false;
    if (shorthand) {
      if (!arguments.empty()) {
        std::optional<uint64_t> value =
            literalControlValue(arguments.front(), "levels");
        if (!value)
          return;
        levels = *value;
        firstSelector = 1;
        selectCurrentScope = arguments.size() == 1;
      }
    } else {
      if (arguments.empty()) {
        emitError(getSemanticLocation(call))
            << "$assertcontrol requires a control type";
        invalid = true;
        return;
      }
      std::optional<uint64_t> value =
          literalControlValue(arguments[0], "control type");
      if (!value)
        return;
      if (*value < 1 || *value > 11) {
        emitError(getSemanticLocation(arguments[0]))
            << "$assertcontrol control type must be in the range 1 through "
               "11";
        invalid = true;
        return;
      }
      action = static_cast<uint32_t>(*value);
      if (arguments.size() >= 2 &&
          !isa<semantic::SVEmptyArgumentExpressionOp>(arguments[1])) {
        value = literalControlValue(arguments[1], "assertion-type mask");
        if (!value)
          return;
        assertionTypes = *value;
      } else {
        assertionTypes = 31;
      }
      if (arguments.size() >= 3 &&
          !isa<semantic::SVEmptyArgumentExpressionOp>(arguments[2])) {
        value = literalControlValue(arguments[2], "directive-type mask");
        if (!value)
          return;
        directiveTypes = *value;
      }
      if (arguments.size() >= 4) {
        bool explicitLevels =
            !isa<semantic::SVEmptyArgumentExpressionOp>(arguments[3]);
        if (explicitLevels) {
          value = literalControlValue(arguments[3], "levels");
          if (!value)
            return;
          levels = *value;
        }
        firstSelector = 4;
        selectCurrentScope = explicitLevels && arguments.size() == 4;
      } else {
        firstSelector = arguments.size();
      }
    }
    if ((assertionTypes & ~UINT64_C(31)) != 0 ||
        (directiveTypes & ~UINT64_C(7)) != 0) {
      emitError(getSemanticLocation(call))
          << "assertion-control masks select unsupported unique, unique0, "
             "priority, or directive kinds";
      invalid = true;
      return;
    }
    // On, Off, and Kill do not affect expect statements. The remaining
    // controls do, so selecting an expect statement is rejected below until
    // executable expect support lands.
    if (action >= 3 && action <= 5)
      assertionTypes &= ~UINT64_C(16);

    SmallVector<StringRef> selectors;
    for (Operation *argument : ArrayRef(arguments).drop_front(firstSelector)) {
      auto path = argument->getAttrOfType<StringAttr>("referenced_path");
      if (!path) {
        emitError(getSemanticLocation(argument))
            << "assertion-control selectors must be resolved hierarchy or "
               "assertion identifiers";
        invalid = true;
        return;
      }
      selectors.push_back(path.getValue());
    }
    if (selectCurrentScope) {
      auto scope = call->getAttrOfType<StringAttr>("system_scope_path");
      if (!scope || !instanceScopeDepths.contains(scope.getValue())) {
        emitError(getSemanticLocation(call))
            << "assertion-control levels-only form has no supported current "
               "module-instance scope";
        invalid = true;
        return;
      }
      selectors.push_back(scope.getValue());
    }
    for (StringRef selector : selectors) {
      bool assertion = llvm::any_of(assertionInventory,
                                    [&](const AssertionInventoryEntry &entry) {
                                      return entry.path == selector;
                                    });
      if (!assertion && !instanceScopeDepths.contains(selector)) {
        emitError(getSemanticLocation(call))
            << "assertion-control selector '" << selector
            << "' is not an assertion or supported module-instance scope";
        invalid = true;
        return;
      }
    }

    SmallVector<int64_t> selectedIDs;
    SmallVector<std::pair<Operation *, uint64_t>> selectedAssertions;
    for (const AssertionInventoryEntry &entry : assertionInventory) {
      if ((entry.assertionType & assertionTypes) == 0 ||
          (entry.directiveType & directiveTypes) == 0)
        continue;
      bool selected = selectors.empty();
      for (StringRef selector : selectors) {
        if (entry.path == selector) {
          selected = true;
          break;
        }
        auto scope = instanceScopeDepths.find(selector);
        if (scope == instanceScopeDepths.end() ||
            entry.scopeDepth < scope->second ||
            !(entry.scope == selector ||
              (StringRef(entry.scope).starts_with(selector) &&
               StringRef(entry.scope)
                   .drop_front(selector.size())
                   .starts_with("."))))
          continue;
        uint64_t relativeDepth = entry.scopeDepth - scope->second;
        if (levels == 0 || relativeDepth < levels) {
          selected = true;
          break;
        }
      }
      if (!selected)
        continue;
      if (!entry.supported) {
        emitError(getSemanticLocation(call))
            << "assertion control selected concurrent assertion '" << entry.path
            << "', but this slice supports immediate assertions only";
        invalid = true;
        return;
      }
      selectedIDs.push_back(static_cast<int64_t>(entry.id));
      selectedAssertions.push_back({entry.operation, entry.id});
    }
    llvm::sort(selectedIDs);
    selectedIDs.erase(std::unique(selectedIDs.begin(), selectedIDs.end()),
                      selectedIDs.end());
    call->setAttr("obelisk_sim.assertion_control_action",
                  IntegerAttr::get(IntegerType::get(context, 32), action));
    call->setAttr("obelisk_sim.assertion_control_ids",
                  DenseI64ArrayAttr::get(context, selectedIDs));
    for (auto [target, id] : selectedAssertions) {
      target->setAttr("obelisk_sim.assertion_control_target_id",
                      IntegerAttr::get(IntegerType::get(context, 64), id));
      if (action >= 3 && action <= 5)
        target->setAttr("obelisk_sim.assertion_controlled",
                        UnitAttr::get(context));
      if (action >= 6 && action <= 11)
        target->setAttr("obelisk_sim.assertion_action_controlled",
                        UnitAttr::get(context));
    }
  });

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
        if (isCompileTimeOnlyInstanceMember(body))
          return;
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

  // Coverpoint expressions are evaluated from their semantic declarations at
  // each manual sample site rather than cloned into a prepared code unit.
  // Freeze instance-property descriptors on those declarations now, while
  // the semantic symbol table and flattened class layout are both available.
  semanticRoot->walk([&](semantic::SVCovergroupTypeOp covergroup) {
    covergroup->walk([&](Operation *nested) {
      SymbolRefAttr reference;
      if (auto named = dyn_cast<semantic::SVNamedValueExpressionOp>(nested))
        reference = named.getReferencedSymbol();
      else if (auto member =
                   dyn_cast<semantic::SVMemberAccessExpressionOp>(nested))
        reference = member.getReferencedSymbol();
      if (!reference)
        return;
      auto symbol = semanticSymbols.find(reference.getLeafReference());
      if (symbol == semanticSymbols.end())
        return;
      auto property =
          dyn_cast<semantic::SVClassPropertySymbolOp>(symbol->second);
      if (!property ||
          property.getLifetime() == semantic::SVVariableLifetime::Static)
        return;
      if (FlatSymbolRefAttr field = classFieldSymbols.lookup(property))
        nested->setAttr("obelisk_sim.class_field", field);
    });
  });

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

  // A static randc property shares one cycle across every object. Its source
  // value already has class-wide storage; materialize the two compiler-owned
  // cycle words beside that storage instead of adding per-instance fields.
  // These descriptors participate in the ordinary capture ABI, so native and
  // bytecode execution retain exactly the same persistent cycle state.
  llvm::DenseMap<Operation *, std::pair<std::string, std::string>>
      staticRandCStatePaths;
  uint64_t nextStorageId = 0;
  for (const auto &entry : descriptors)
    if (entry.second.kind == DescriptorInfo::Kind::Storage) {
      if (entry.second.id == UINT64_MAX) {
        emitError(module.getLoc())
            << "static randc state exceeds the storage descriptor space";
        return abort();
      }
      nextStorageId = std::max(nextStorageId, entry.second.id + 1);
    }
  Type i64 = builder.getI64Type();
  for (semantic::SVClassTypeOp classType : classSources)
    for (Operation *member : getChildren(classType)) {
      auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(member);
      if (!property ||
          property.getLifetime() != semantic::SVVariableLifetime::Static ||
          property.getRandMode() != semantic::SVRandMode::RandC)
        continue;
      StringRef propertyPath = getHierarchyName(property);
      auto source = descriptors.find(propertyPath);
      if (source == descriptors.end() ||
          source->second.kind != DescriptorInfo::Kind::Storage) {
        emitError(getSemanticLocation(property))
            << "static randc property has no class-wide storage descriptor";
        return abort();
      }
      if (nextStorageId > UINT64_MAX - 2) {
        emitError(getSemanticLocation(property))
            << "static randc state exceeds the storage descriptor space";
        return abort();
      }
      std::string keyPath = (propertyPath + ".$randc_key").str();
      std::string positionPath = (propertyPath + ".$randc_position").str();
      if (descriptors.count(keyPath) || descriptors.count(positionPath)) {
        emitError(getSemanticLocation(property))
            << "static randc state conflicts with an existing design object";
        return abort();
      }
      auto addState = [&](StringRef path, StringRef debugName) {
        uint64_t id = nextStorageId++;
        DescriptorInfo descriptor{DescriptorInfo::Kind::Storage, id,
                                  source->second.scopeId, i64,
                                  sim::NetResolutionKind::Wire};
        descriptor.rootType = i64;
        descriptors[path] = descriptor;
        sim::SimStorageDeclOp::create(
            builder, getSemanticLocation(property), id, source->second.scopeId,
            i64, sim::Lifetime::Design, builder.getStringAttr(path),
            builder.getStringAttr(debugName),
            sim::ComputeObservabilityKindAttr{});
      };
      addState(keyPath, "__obelisk_static_randc_key");
      addState(positionPath, "__obelisk_static_randc_position");
      staticRandCStatePaths[property] = {std::move(keyPath),
                                         std::move(positionPath)};
    }

  FailureOr<ContinuousDriverMap> preparedNetTopology =
      materializeNetTopology(sourceUnits, portConnections, semanticSymbols,
                             descriptors, *scopes, builder);
  if (failed(preparedNetTopology))
    return abort();
  ContinuousDriverMap &continuousDrivers = *preparedNetTopology;

  // Static variable initialization precedes process execution, but established
  // simulators expose a declaration net's sole constant driver to those
  // initializers. Record only literal, full-net, single-driver cases. Folding
  // their reads preserves the continuous process itself, including its
  // time-zero transition for procedural listeners.
  llvm::DenseMap<uint64_t, unsigned> netDriverCounts;
  for (const auto &entry : continuousDrivers)
    for (const DriverInfo &driver : entry.second) {
      auto target = descriptors.find(driver.path);
      if (target != descriptors.end() &&
          target->second.kind == DescriptorInfo::Kind::Net)
        ++netDriverCounts[target->second.id];
    }
  llvm::DenseMap<Operation *, StringAttr> staticLiteralNets;
  for (Operation *source : sourceUnits) {
    auto net = dyn_cast<semantic::SVNetSymbolOp>(source);
    if (!net)
      continue;
    SmallVector<Operation *> initializer = getChildren(net);
    std::optional<StringRef> spelling =
        initializer.size() == 1 ? getConstantSpelling(initializer.front())
                                : std::nullopt;
    auto target = descriptors.find(getHierarchyName(net));
    auto drivers = continuousDrivers.find(net);
    if (!spelling || target == descriptors.end() ||
        target->second.kind != DescriptorInfo::Kind::Net ||
        netDriverCounts.lookup(target->second.id) != 1 ||
        drivers == continuousDrivers.end() || drivers->second.size() != 1)
      continue;
    std::optional<unsigned> width = sim::getPackedWidth(target->second.type);
    const DriverInfo &driver = drivers->second.front();
    if (!width || driver.drivenLow != 0 || driver.drivenWidth != *width)
      continue;
    staticLiteralNets.try_emplace(net, builder.getStringAttr(*spelling));
  }

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
  auto collectStaticConstraintStorages =
      [&](ArrayRef<EffectiveConstraintGroup> groups,
          Location location) -> FailureOr<SmallVector<int64_t>> {
    SmallVector<int64_t> storages;
    storages.reserve(groups.size());
    for (const EffectiveConstraintGroup &group : groups) {
      semantic::SVConstraintBlockSymbolOp constraint =
          group.empty() ? semantic::SVConstraintBlockSymbolOp{} : group.back();
      if (!constraint || !constraint.getIsStatic().value_or(false)) {
        storages.push_back(-1);
        continue;
      }
      auto storage = constraint->getAttrOfType<IntegerAttr>(
          staticConstraintStorageAttrName);
      if (!storage || storage.getValue().isNegative() ||
          storage.getValue().getActiveBits() > 63) {
        emitError(location)
            << "static constraint block has no valid shared mode storage";
        return failure();
      }
      storages.push_back(
          static_cast<int64_t>(storage.getValue().getZExtValue()));
    }
    return storages;
  };

  struct RandomDomainPattern {
    uint64_t mask;
    uint64_t value;
  };
  struct RandomSubdomain {
    uint64_t offset;
    uint64_t width;
    SmallVector<RandomDomainPattern> patterns;
  };
  llvm::DenseMap<Type, semantic::SVEnumTypeOp> enumDeclarations;
  semanticRoot.walk([&](semantic::SVEnumTypeOp enumeration) {
    enumDeclarations.try_emplace(enumeration.getSemanticType(), enumeration);
  });
  llvm::DenseMap<Type, SmallVector<uint64_t>> enumValues;

  auto widthMask = [](uint64_t width) {
    return width == 64 ? UINT64_MAX : (uint64_t{1} << width) - 1;
  };
  constexpr size_t maxRandomDomainPatterns = 4096;
  std::function<FailureOr<SmallVector<RandomDomainPattern>>(Type, Location)>
      buildWholeDomain;
  buildWholeDomain =
      [&](Type type,
          Location location) -> FailureOr<SmallVector<RandomDomainPattern>> {
    std::optional<uint64_t> width = getSemanticBitstreamWidth(type);
    if (!width || *width > 64)
      return failure();
    if (auto enumeration = dyn_cast<semantic::EnumType>(type)) {
      auto found = enumValues.find(enumeration);
      if (found == enumValues.end()) {
        auto declaration = enumDeclarations.find(enumeration);
        if (declaration == enumDeclarations.end() || !width || *width == 0) {
          emitError(location)
              << "random enum domain has no declaration inventory";
          return failure();
        }
        SmallVector<uint64_t> values;
        for (Operation *child : getChildren(declaration->second)) {
          auto value = dyn_cast<semantic::SVEnumValueSymbolOp>(child);
          if (!value)
            continue;
          auto spelling = value->getAttrOfType<StringAttr>("constant_value");
          FailureOr<ParsedConstant> parsed =
              spelling ? parseSVInteger(spelling.getValue(), *width,
                                        getSemanticLocation(value))
                       : FailureOr<ParsedConstant>(failure());
          if (failed(parsed) || !parsed->unknown.isZero()) {
            emitError(getSemanticLocation(value))
                << "random enum values must be fixed two-state constants";
            return failure();
          }
          uint64_t bits = parsed->value.getZExtValue();
          if (!llvm::is_contained(values, bits))
            values.push_back(bits);
        }
        if (values.empty()) {
          emitError(location) << "random enum domain has no values";
          return failure();
        }
        llvm::sort(values);
        found = enumValues.try_emplace(enumeration, std::move(values)).first;
      }
      SmallVector<RandomDomainPattern> patterns;
      uint64_t mask = widthMask(*width);
      for (uint64_t value : found->second)
        patterns.push_back({mask, value});
      return patterns;
    }
    auto combine = [&](SmallVector<RandomDomainPattern> &result,
                       ArrayRef<RandomDomainPattern> nested,
                       uint64_t offset) -> LogicalResult {
      if (nested.size() != 0 &&
          result.size() > maxRandomDomainPatterns / nested.size()) {
        emitError(location) << "random finite-domain expansion exceeds "
                            << maxRandomDomainPatterns << " patterns";
        return failure();
      }
      SmallVector<RandomDomainPattern> combined;
      combined.reserve(result.size() * nested.size());
      for (const RandomDomainPattern &outer : result)
        for (const RandomDomainPattern &inner : nested)
          combined.push_back({outer.mask | (inner.mask << offset),
                              outer.value | (inner.value << offset)});
      result = std::move(combined);
      return success();
    };
    auto arrayDomain =
        [&](Type elementType,
            uint64_t count) -> FailureOr<SmallVector<RandomDomainPattern>> {
      std::optional<uint64_t> elementWidth =
          getSemanticBitstreamWidth(elementType);
      FailureOr<SmallVector<RandomDomainPattern>> element =
          buildWholeDomain(elementType, location);
      if (!elementWidth || *elementWidth == 0 || failed(element))
        return failure();
      SmallVector<RandomDomainPattern> result{{0, 0}};
      for (uint64_t index = 0; index != count; ++index)
        if (failed(combine(result, *element, index * *elementWidth)))
          return failure();
      return result;
    };
    if (auto array = dyn_cast<semantic::RangedPackedArrayType>(type)) {
      std::optional<uint64_t> elementWidth =
          getSemanticBitstreamWidth(array.getElementType());
      if (!elementWidth || *elementWidth == 0 || *width % *elementWidth != 0)
        return failure();
      return arrayDomain(array.getElementType(), *width / *elementWidth);
    }
    if (auto array = dyn_cast<semantic::PackedArrayType>(type))
      return arrayDomain(array.getElementType(), array.getSize());
    if (auto aggregate = dyn_cast<semantic::SourceAggregateType>(type)) {
      if (!aggregate.getIsPacked())
        return failure();
      if (aggregate.getIsUnion() && !aggregate.getIsTagged())
        return SmallVector<RandomDomainPattern>{{0, 0}};
      if (aggregate.getIsUnion()) {
        uint64_t tagBits = aggregate.getTagBits();
        if (tagBits == 0 || tagBits > *width)
          return failure();
        uint64_t payloadWidth = *width - tagBits;
        SmallVector<RandomDomainPattern> result;
        for (Attribute fieldAttr : aggregate.getFields()) {
          auto field = dyn_cast<DictionaryAttr>(fieldAttr);
          auto typeAttr = field ? field.getAs<TypeAttr>("type") : TypeAttr{};
          auto ordinal =
              field ? field.getAs<IntegerAttr>("ordinal") : IntegerAttr{};
          if (!typeAttr || !ordinal || ordinal.getValue().isNegative() ||
              ordinal.getValue().getActiveBits() > 64)
            return failure();
          uint64_t fieldOrdinal = ordinal.getValue().getZExtValue();
          if (fieldOrdinal > widthMask(tagBits))
            return failure();
          Type fieldType = typeAttr.getValue();
          uint64_t fieldWidth = 0;
          SmallVector<RandomDomainPattern> fieldPatterns{{0, 0}};
          if (!isa<semantic::VoidType>(fieldType)) {
            std::optional<uint64_t> packedWidth =
                getSemanticBitstreamWidth(fieldType);
            FailureOr<SmallVector<RandomDomainPattern>> nested =
                buildWholeDomain(fieldType, location);
            if (!packedWidth || *packedWidth > payloadWidth || failed(nested))
              return failure();
            fieldWidth = *packedWidth;
            fieldPatterns = std::move(*nested);
          }
          uint64_t paddingMask =
              widthMask(payloadWidth) & ~widthMask(fieldWidth);
          uint64_t tagMask = widthMask(tagBits) << payloadWidth;
          uint64_t tagValue = fieldOrdinal << payloadWidth;
          for (const RandomDomainPattern &pattern : fieldPatterns) {
            result.push_back({pattern.mask | paddingMask | tagMask,
                              pattern.value | tagValue});
            if (result.size() > maxRandomDomainPatterns) {
              emitError(location) << "random finite-domain expansion exceeds "
                                  << maxRandomDomainPatterns << " patterns";
              return failure();
            }
          }
        }
        return result;
      }
      SmallVector<RandomDomainPattern> result{{0, 0}};
      for (Attribute fieldAttr : aggregate.getFields()) {
        auto field = dyn_cast<DictionaryAttr>(fieldAttr);
        auto typeAttr = field ? field.getAs<TypeAttr>("type") : TypeAttr{};
        auto offset =
            field ? field.getAs<IntegerAttr>("packed_offset") : IntegerAttr{};
        if (!typeAttr || !offset || offset.getValue().isNegative() ||
            offset.getValue().getActiveBits() > 64)
          return failure();
        std::optional<uint64_t> fieldWidth =
            getSemanticBitstreamWidth(typeAttr.getValue());
        uint64_t fieldOffset = offset.getValue().getZExtValue();
        if (!fieldWidth || fieldOffset > *width ||
            *fieldWidth > *width - fieldOffset)
          return failure();
        FailureOr<SmallVector<RandomDomainPattern>> nested =
            buildWholeDomain(typeAttr.getValue(), location);
        if (failed(nested) || failed(combine(result, *nested, fieldOffset)))
          return failure();
      }
      return result;
    }
    return SmallVector<RandomDomainPattern>{{0, 0}};
  };

  std::function<LogicalResult(Type, uint64_t,
                              SmallVectorImpl<RandomSubdomain> &, Location)>
      collectRandomSubdomains;
  collectRandomSubdomains = [&](Type type, uint64_t baseOffset,
                                SmallVectorImpl<RandomSubdomain> &result,
                                Location location) -> LogicalResult {
    std::optional<uint64_t> width = getSemanticBitstreamWidth(type);
    if (!width || *width == 0 || *width > UINT32_MAX ||
        baseOffset > UINT32_MAX - *width)
      return failure();
    if (isa<semantic::EnumType>(type)) {
      FailureOr<SmallVector<RandomDomainPattern>> patterns =
          buildWholeDomain(type, location);
      if (failed(patterns))
        return failure();
      result.push_back({baseOffset, *width, std::move(*patterns)});
      return success();
    }
    if (auto array = dyn_cast<semantic::RangedPackedArrayType>(type)) {
      std::optional<uint64_t> elementWidth =
          getSemanticBitstreamWidth(array.getElementType());
      if (!elementWidth || *elementWidth == 0 || *width % *elementWidth != 0)
        return failure();
      for (uint64_t index = 0; index != *width / *elementWidth; ++index)
        if (failed(collectRandomSubdomains(array.getElementType(),
                                           baseOffset + index * *elementWidth,
                                           result, location)))
          return failure();
      return success();
    }
    if (auto array = dyn_cast<semantic::PackedArrayType>(type)) {
      std::optional<uint64_t> elementWidth =
          getSemanticBitstreamWidth(array.getElementType());
      if (!elementWidth || *elementWidth == 0)
        return failure();
      for (uint64_t index = 0; index != array.getSize(); ++index)
        if (failed(collectRandomSubdomains(array.getElementType(),
                                           baseOffset + index * *elementWidth,
                                           result, location)))
          return failure();
      return success();
    }
    if (auto aggregate = dyn_cast<semantic::SourceAggregateType>(type)) {
      if (!aggregate.getIsPacked())
        return failure();
      if (aggregate.getIsUnion() && aggregate.getIsTagged()) {
        FailureOr<SmallVector<RandomDomainPattern>> patterns =
            buildWholeDomain(type, location);
        if (failed(patterns))
          return failure();
        result.push_back({baseOffset, *width, std::move(*patterns)});
        return success();
      }
      if (aggregate.getIsUnion())
        return success();
      for (Attribute fieldAttr : aggregate.getFields()) {
        auto field = dyn_cast<DictionaryAttr>(fieldAttr);
        auto typeAttr = field ? field.getAs<TypeAttr>("type") : TypeAttr{};
        auto offset =
            field ? field.getAs<IntegerAttr>("packed_offset") : IntegerAttr{};
        if (!typeAttr || !offset || offset.getValue().isNegative() ||
            offset.getValue().getActiveBits() > 64 ||
            failed(collectRandomSubdomains(typeAttr.getValue(),
                                           baseOffset +
                                               offset.getValue().getZExtValue(),
                                           result, location)))
          return failure();
      }
      return success();
    }
    return success();
  };

  // Freeze object randomization into each call before its semantic class and
  // constraint declarations are erased. The unit-lowering pass is isolated,
  // so the cloned constraint expressions and this compact field inventory are
  // its complete compiler-owned randomization plan.
  std::function<bool(semantic::SVCallExpressionOp)> freezeRandomizeContract;
  freezeRandomizeContract = [&](semantic::SVCallExpressionOp call) -> bool {
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
        frozenChecker || (argumentCount == 2 &&
                          isa<semantic::SVNullLiteralOp>(callChildren.back()));
    bool explicitPropertyList = argumentCount > 1 && !checkerOnly;
    llvm::SmallPtrSet<Operation *, 8> explicitProperties;
    SmallVector<Operation *> explicitPropertyArguments;
    SmallVector<semantic::SVClassPropertySymbolOp> explicitPropertySymbols;
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
    if (explicitPropertyList) {
      for (uint64_t index = 1; index != argumentCount; ++index) {
        Operation *argument = callChildren[receiverIndex + index];
        if (!isa<semantic::SVNamedValueExpressionOp>(argument)) {
          emitError(getSemanticLocation(argument))
              << "randomize property argument must be a class property name";
          invalid = true;
          return true;
        }
        auto reference =
            argument->getAttrOfType<SymbolRefAttr>("referenced_symbol");
        auto symbol = reference
                          ? semanticSymbols.find(reference.getLeafReference())
                          : semanticSymbols.end();
        auto property =
            symbol != semanticSymbols.end()
                ? dyn_cast<semantic::SVClassPropertySymbolOp>(symbol->second)
                : semantic::SVClassPropertySymbolOp{};
        if (!property) {
          emitError(getSemanticLocation(argument))
              << "randomize property argument does not resolve to a class "
                 "property";
          invalid = true;
          return true;
        }
        explicitProperties.insert(property);
        explicitPropertyArguments.push_back(argument);
        explicitPropertySymbols.push_back(property);
      }
      call->setAttr(randomizeExplicitPropertiesAttrName, builder.getUnitAttr());
    }

    auto foundClass =
        semanticClasses.find(receiverType.getClassName().getLeafReference());
    if (auto plannedClass = call->getAttrOfType<FlatSymbolRefAttr>(
            randomizePlanClassAttrName)) {
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
    if (explicitPropertyList) {
      SmallVector<semantic::SVClassTypeOp> receiverHierarchy;
      if (failed(collectClassHierarchy(foundClass->second, receiverHierarchy,
                                       "randomize property selection"))) {
        invalid = true;
        return true;
      }
      llvm::SmallPtrSet<Operation *, 8> receiverClasses;
      for (semantic::SVClassTypeOp classType : receiverHierarchy)
        receiverClasses.insert(classType);
      for (auto [argument, property] : llvm::zip_equal(
               explicitPropertyArguments, explicitPropertySymbols)) {
        auto owner = property->getParentOfType<semantic::SVClassTypeOp>();
        if (!owner || !receiverClasses.contains(owner)) {
          emitError(getSemanticLocation(argument))
              << "randomize property argument does not belong to the "
                 "receiver class hierarchy";
          invalid = true;
          return true;
        }
      }
    }

    // A randomize call uses the dynamic object's complete property and
    // constraint set even though randomize itself is a builtin. Specialize a
    // frozen plan for every concrete class that can inhabit the static handle,
    // then select the exact plan once at the call site. Keeping the
    // alternatives as nested semantic calls lets the ordinary capture analysis
    // see their union and keeps all sampler generation in the isolated unit
    // lowering.
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
        bool compatible =
            llvm::is_contained(candidateHierarchy, foundClass->second);
        if (!compatible && foundClass->second.getIsInterface()) {
          StringRef targetInterface = cast<semantic::ClassHandleType>(
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
              if (interface && interface.getClassName().getLeafReference() ==
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
      llvm::sort(dynamicPlans,
                 [&](const DynamicPlan &lhs, const DynamicPlan &rhs) {
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
          auto alternative = cast<semantic::SVCallExpressionOp>(call->clone());
          if (checkerOnly) {
            SmallVector<Operation *> alternativeChildren =
                getChildren(alternative);
            if (!frozenChecker &&
                (alternativeChildren.empty() ||
                 !isa<semantic::SVNullLiteralOp>(alternativeChildren.back()))) {
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
        if (explicitPropertyList) {
          for (Operation *argument : explicitPropertyArguments)
            argument->erase();
          call->setAttr("argument_count", builder.getI64IntegerAttr(1));
          call->setAttr("defaulted_arguments",
                        builder.getDenseI64ArrayAttr({0}));
        }
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
      StringAttr referencePath;
      Type type;
      uint64_t width;
      unsigned modeIndex;
      bool isContainerSize;
      Type containerType;
      uint64_t sizeConstraintMask;
      bool hasUnconditionalSizeConstraint;
      FlatSymbolRefAttr nestedObjectField;
      Type nestedObjectType;
      Type nestedObjectStorageType;
      unsigned nestedModeIndex;
      bool isSigned;
      bool isRandC;
      FlatSymbolRefAttr randcKeyField;
      FlatSymbolRefAttr randcPositionField;
      StringAttr randcKeyPath;
      StringAttr randcPositionPath;
      IntegerAttr randomModeStorage;
      SmallVector<RandomSubdomain> domains;
    };
    struct RandomContainerProperty {
      Operation *source;
      FlatSymbolRefAttr field;
      Type type;
      Type elementType;
      unsigned elementWidth;
      unsigned modeIndex;
    };
    struct NestedObjectPlan {
      Operation *source;
      FlatSymbolRefAttr field;
      Type concreteType;
      Type storageType;
      SmallVector<semantic::SVClassTypeOp> hierarchy;
      SmallVector<EffectiveConstraintGroup> constraintGroups;
      SmallVector<unsigned> globalConstraintIndices;
      unsigned outerModeIndex;
      semantic::SVSubroutineSymbolOp preHook;
      semantic::SVSubroutineSymbolOp postHook;
    };
    SmallVector<RandomProperty> properties;
    SmallVector<RandomContainerProperty> containerProperties;
    SmallVector<NestedObjectPlan> nestedObjectPlans;
    SmallVector<Operation *> constraintRoots;
    SmallVector<EffectiveConstraintGroup> constraintGroups;
    semantic::SVSubroutineSymbolOp preRandomizeHook;
    semantic::SVSubroutineSymbolOp postRandomizeHook;
    if (hasInlineConstraints)
      for (auto [index, child] : llvm::enumerate(callChildren))
        if (index != receiverIndex && isa<semantic::SVConstraintListOp>(child))
          constraintRoots.push_back(child);
    unsigned randomPropertyIndex = 0;
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
          unsigned modeIndex = randomPropertyIndex;
          if (property.getRandMode() != semantic::SVRandMode::None)
            ++randomPropertyIndex;
          bool isStatic =
              property.getLifetime() == semantic::SVVariableLifetime::Static;
          if (explicitPropertyList
                  ? !explicitProperties.contains(property)
                  : property.getRandMode() == semantic::SVRandMode::None)
            continue;
          FailureOr<Type> type = getNormalizedSemanticType(property);
          if (failed(type)) {
            invalid = true;
            continue;
          }
          FlatSymbolRefAttr field;
          StringAttr referencePath;
          if (isStatic) {
            StringRef path = getHierarchyName(property);
            auto descriptor = descriptors.find(path);
            if (path.empty() || descriptor == descriptors.end() ||
                descriptor->second.kind != DescriptorInfo::Kind::Storage) {
              emitError(getSemanticLocation(property))
                  << "static random property has no class-wide storage "
                     "descriptor";
              invalid = true;
              continue;
            }
            referencePath = builder.getStringAttr(path);
          } else {
            field = classFieldSymbols.lookup(property);
          }
          if (isa<sim::DynamicArrayType, sim::QueueType>(*type)) {
            Type elementType = isa<sim::DynamicArrayType>(*type)
                                   ? cast<sim::DynamicArrayType>(*type)
                                         .getElementType()
                                   : cast<sim::QueueType>(*type).getElementType();
            if (isStatic) {
              emitError(getSemanticLocation(property))
                  << "static random dynamic containers are not executable "
                     "yet";
              invalid = true;
              continue;
            }
            if (property.getRandMode() == semantic::SVRandMode::RandC) {
              emitError(getSemanticLocation(property))
                  << "randc dynamic containers require per-element cyclic "
                     "state";
              invalid = true;
              continue;
            }
            std::optional<unsigned> elementWidth =
                sim::getPackedWidth(elementType);
            if (!elementWidth || *elementWidth == 0 || *elementWidth > 64) {
              emitError(getSemanticLocation(property))
                  << "random dynamic container elements must be packed "
                     "integral "
                     "values no wider than 64 bits";
              invalid = true;
              continue;
            }
            containerProperties.push_back({property, field, *type,
                                           elementType,
                                           *elementWidth, modeIndex});
            continue;
          }
          if (auto objectType = dyn_cast<sim::ClassHandleType>(*type)) {
            if (isStatic) {
              emitError(getSemanticLocation(property))
                  << "static rand object handles are not executable yet";
              invalid = true;
              continue;
            }
            if (property.getRandMode() == semantic::SVRandMode::RandC) {
              emitError(getSemanticLocation(property))
                  << "object handles cannot be declared randc";
              invalid = true;
              continue;
            }
            auto semanticObjectType = dyn_cast<semantic::ClassHandleType>(
                property.getSemanticType().value_or(Type{}));
            auto declaredClass =
                semanticObjectType
                    ? semanticClasses.find(semanticObjectType.getClassName()
                                               .getLeafReference())
                    : semanticClasses.end();
            SmallVector<semantic::SVClassTypeOp> concreteClasses;
            if (declaredClass != semanticClasses.end())
              for (semantic::SVClassTypeOp candidate : classSources) {
                if (candidate.getIsAbstract() || candidate.getIsInterface())
                  continue;
                SmallVector<semantic::SVClassTypeOp> candidateHierarchy;
                if (failed(collectClassHierarchy(
                        candidate, candidateHierarchy,
                        "nested object randomization"))) {
                  invalid = true;
                  continue;
                }
                if (llvm::is_contained(candidateHierarchy,
                                       declaredClass->second))
                  concreteClasses.push_back(candidate);
              }
            if (concreteClasses.size() != 1) {
              emitError(getSemanticLocation(property))
                  << "rand object handle requires a unique concrete dynamic "
                     "class in the closed-world hierarchy";
              invalid = true;
              continue;
            }
            SmallVector<semantic::SVClassTypeOp> nestedHierarchy;
            if (failed(collectClassHierarchy(concreteClasses.front(),
                                             nestedHierarchy,
                                             "nested object randomization"))) {
              invalid = true;
              continue;
            }
            bool unsupportedNestedSemantics = false;
            unsigned nestedModeIndex = 0;
            semantic::SVSubroutineSymbolOp nestedPreHook;
            semantic::SVSubroutineSymbolOp nestedPostHook;
            for (semantic::SVClassTypeOp nestedClass : nestedHierarchy) {
              for (Operation *nestedMember : getChildren(nestedClass)) {
                if (isa<semantic::SVConstraintBlockSymbolOp>(nestedMember)) {
                  continue;
                }
                if (auto method = getClassMethod(nestedMember);
                    method &&
                    method.getIsPrePostRandomize().value_or(false) &&
                    !method.getIsBuiltin().value_or(false)) {
                  StringRef name = method.getName().value_or("");
                  std::optional<Type> methodType = method.getSemanticType();
                  auto subroutineType =
                      methodType
                          ? dyn_cast<semantic::SubroutineType>(*methodType)
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
                        << "nested randomization hooks must be void instance "
                           "functions without arguments";
                    unsupportedNestedSemantics = true;
                  } else if (name == "pre_randomize") {
                    nestedPreHook = method;
                  } else if (name == "post_randomize") {
                    nestedPostHook = method;
                  } else {
                    emitError(getSemanticLocation(method))
                        << "unknown nested randomization lifecycle hook "
                        << name;
                    unsupportedNestedSemantics = true;
                  }
                  continue;
                }
                auto nestedProperty =
                    dyn_cast<semantic::SVClassPropertySymbolOp>(nestedMember);
                if (!nestedProperty ||
                    nestedProperty.getRandMode() ==
                        semantic::SVRandMode::None)
                  continue;
                unsigned childModeIndex = nestedModeIndex++;
                if (childModeIndex >= 64) {
                  emitError(getSemanticLocation(nestedProperty))
                      << "nested rand object exceeds the 64-property "
                         "rand_mode boundary";
                  unsupportedNestedSemantics = true;
                  continue;
                }
                FailureOr<Type> nestedType =
                    getNormalizedSemanticType(nestedProperty);
                std::optional<Type> semanticNestedType =
                    nestedProperty.getSemanticType();
                std::optional<unsigned> nestedWidth =
                    succeeded(nestedType) ? sim::getPackedWidth(*nestedType)
                                          : std::nullopt;
                if (failed(nestedType) || !semanticNestedType || !nestedWidth ||
                    *nestedWidth == 0 ||
                    nestedProperty.getRandMode() ==
                        semantic::SVRandMode::RandC ||
                    nestedProperty.getLifetime() ==
                        semantic::SVVariableLifetime::Static) {
                  emitError(getSemanticLocation(nestedProperty))
                      << "nested rand object properties must be non-static "
                         "packed rand values";
                  unsupportedNestedSemantics = true;
                  continue;
                }
                SmallVector<RandomSubdomain> nestedDomains;
                if (failed(collectRandomSubdomains(
                        *semanticNestedType, 0, nestedDomains,
                        getSemanticLocation(nestedProperty)))) {
                  emitError(getSemanticLocation(nestedProperty))
                      << "nested rand object property has an unsupported "
                         "finite domain";
                  unsupportedNestedSemantics = true;
                  continue;
                }
                properties.push_back(
                    {nestedProperty,
                     classFieldSymbols.lookup(nestedProperty),
                     {},
                     *nestedType,
                     *nestedWidth,
                     modeIndex,
                     false,
                     {},
                     0,
                     false,
                     field,
                     sim::ClassHandleType::get(
                         context,
                         FlatSymbolRefAttr::get(
                             context,
                             classSymbols.lookup(concreteClasses.front())
                                 .getValue())),
                     objectType,
                     childModeIndex,
                     isSignedSemanticType(*semanticNestedType),
                     false,
                     {},
                     {},
                     {},
                     {},
                     {},
                     std::move(nestedDomains)});
              }
            }
            NestedObjectPlan nestedPlan{
                property,
                field,
                sim::ClassHandleType::get(
                    context,
                    FlatSymbolRefAttr::get(
                        context, classSymbols.lookup(concreteClasses.front())
                                     .getValue())),
                objectType,
                nestedHierarchy,
                {},
                {},
                modeIndex,
                nestedPreHook,
                nestedPostHook};
            if (llvm::any_of(nestedObjectPlans,
                             [&](const NestedObjectPlan &existing) {
                               return existing.concreteType ==
                                      nestedPlan.concreteType;
                             })) {
              emitError(getSemanticLocation(property))
                  << "multiple rand handles of the same concrete child class "
                     "require per-instance constraint identities";
              invalid = true;
              continue;
            }
            collectEffectiveConstraints(nestedHierarchy,
                                        nestedPlan.constraintGroups);
            for (const EffectiveConstraintGroup &group :
                 nestedPlan.constraintGroups) {
              semantic::SVConstraintBlockSymbolOp activeConstraint =
                  group.empty() ? semantic::SVConstraintBlockSymbolOp{}
                                : group.back();
              if (activeConstraint &&
                  activeConstraint.getIsStatic().value_or(false)) {
                emitError(getSemanticLocation(property))
                    << "static nested constraint blocks require null-aware "
                       "shared constraint_mode composition";
                invalid = true;
              }
            }
            nestedObjectPlans.push_back(std::move(nestedPlan));
            if (unsupportedNestedSemantics)
              invalid = true;
            continue;
          }
          std::optional<unsigned> width = sim::getPackedWidth(*type);
          if (!width || *width == 0 || (!field && !referencePath)) {
            emitError(getSemanticLocation(property))
                << "random properties must be packed integral values";
            invalid = true;
            continue;
          }
          bool isRandC = property.getRandMode() == semantic::SVRandMode::RandC;
          FlatSymbolRefAttr randcKeyField =
              randcKeyFieldSymbols.lookup(property);
          FlatSymbolRefAttr randcPositionField =
              randcPositionFieldSymbols.lookup(property);
          StringAttr randcKeyPath;
          StringAttr randcPositionPath;
          IntegerAttr randomModeStorage;
          if (isStatic &&
              property.getRandMode() != semantic::SVRandMode::None) {
            randomModeStorage = property->getAttrOfType<IntegerAttr>(
                staticRandomModeStorageAttrName);
            if (!randomModeStorage ||
                randomModeStorage.getValue().isNegative() ||
                randomModeStorage.getValue().getActiveBits() > 63) {
              emitError(getSemanticLocation(property))
                  << "static random property has no valid shared rand_mode "
                     "storage";
              invalid = true;
              continue;
            }
          }
          if (isRandC && isStatic) {
            auto state = staticRandCStatePaths.find(property);
            if (state != staticRandCStatePaths.end()) {
              randcKeyPath = builder.getStringAttr(state->second.first);
              randcPositionPath = builder.getStringAttr(state->second.second);
            }
          }
          if (isRandC &&
              (*width > 32 || ((!randcKeyField || !randcPositionField) &&
                               (!randcKeyPath || !randcPositionPath)))) {
            emitError(getSemanticLocation(property))
                << "randc properties must be packed integral values no wider "
                   "than 32 bits";
            invalid = true;
            continue;
          }
          std::optional<Type> semanticPropertyType = property.getSemanticType();
          SmallVector<RandomSubdomain> domains;
          if (!semanticPropertyType ||
              failed(collectRandomSubdomains(*semanticPropertyType, 0, domains,
                                             getSemanticLocation(property)))) {
            emitError(getSemanticLocation(property))
                << "random property has a finite domain that cannot be "
                   "represented by the executable randomization plan";
            invalid = true;
            continue;
          }
          properties.push_back({property, field, referencePath, *type, *width,
                                modeIndex, false, {}, 0, false, {}, {}, {}, 0,
                                isSignedSemanticType(*semanticPropertyType),
                                isRandC, randcKeyField, randcPositionField,
                                randcKeyPath, randcPositionPath,
                                randomModeStorage, std::move(domains)});
          continue;
        }
      }
    }
    if (randomPropertyIndex > 64) {
      emitError(getSemanticLocation(call))
          << "the executable rand_mode boundary is 64 effective random "
             "properties";
      invalid = true;
      return true;
    }
    auto freezeHook = [&](semantic::SVSubroutineSymbolOp hook,
                          StringRef calleeAttr, StringRef ownerAttr,
                          StringRef sourceAttr) -> LogicalResult {
      if (!hook)
        return success();
      auto callee = directCalleeNames.find(hook);
      semantic::SVClassTypeOp owner = getOwningClass(hook);
      StringAttr ownerSymbol =
          owner ? classSymbols.lookup(owner) : StringAttr{};
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
    llvm::DenseMap<Operation *, NestedObjectPlan *> nestedConstraintOwners;
    for (NestedObjectPlan &plan : nestedObjectPlans) {
      for (const EffectiveConstraintGroup &group : plan.constraintGroups) {
        plan.globalConstraintIndices.push_back(constraintGroups.size());
        constraintGroups.push_back(group);
        for (semantic::SVConstraintBlockSymbolOp constraint : group)
          nestedConstraintOwners[constraint] = &plan;
      }
    }
    if (constraintGroups.size() > 64) {
      emitError(getSemanticLocation(call))
          << "the executable constraint_mode boundary is 64 effective "
             "constraint blocks";
      invalid = true;
      return true;
    }
    FailureOr<SmallVector<int64_t>> staticConstraintStorages =
        collectStaticConstraintStorages(constraintGroups,
                                        getSemanticLocation(call));
    if (failed(staticConstraintStorages)) {
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
    for (const EffectiveConstraintGroup &group : constraintGroups) {
      for (semantic::SVConstraintBlockSymbolOp constraint : group) {
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
    for (RandomContainerProperty &property : containerProperties) {
      bool referenced = llvm::any_of(constraintRoots, [&](Operation *root) {
        bool found = false;
        root->walk([&](Operation *nested) {
          auto reference =
              nested->getAttrOfType<SymbolRefAttr>("referenced_symbol");
          if (reference && reference.getLeafReference() ==
                               property.source->getAttrOfType<StringAttr>(
                                   SymbolTable::getSymbolAttrName()))
            found = true;
        });
        return found;
      });
      if (referenced) {
        uint64_t constraintMask = 0;
        bool unconditionalConstraint = false;
        for (Operation *root : constraintRoots) {
          bool rootReferences = false;
          root->walk([&](Operation *nested) {
            auto reference =
                nested->getAttrOfType<SymbolRefAttr>("referenced_symbol");
            if (reference && reference.getLeafReference() ==
                                 property.source->getAttrOfType<StringAttr>(
                                     SymbolTable::getSymbolAttrName()))
              rootReferences = true;
          });
          if (!rootReferences)
            continue;
          auto block = root->getParentOfType<
              semantic::SVConstraintBlockSymbolOp>();
          auto index = block ? constraintIndices.find(block)
                             : constraintIndices.end();
          if (index == constraintIndices.end())
            unconditionalConstraint = true;
          else
            constraintMask |= uint64_t{1} << index->second;
        }
        bool softReference = llvm::any_of(constraintRoots, [&](Operation *root) {
          bool found = false;
          root->walk([&](semantic::SVExpressionConstraintOp expression) {
            if (!expression.getIsSoft())
              return;
            expression->walk([&](Operation *nested) {
              auto reference =
                  nested->getAttrOfType<SymbolRefAttr>("referenced_symbol");
              if (reference && reference.getLeafReference() ==
                                   property.source->getAttrOfType<StringAttr>(
                                       SymbolTable::getSymbolAttrName()))
                found = true;
            });
          });
          return found;
        });
        if (softReference) {
          emitError(getSemanticLocation(property.source))
              << "soft constraints on dynamic container size require "
                 "discard-aware resize planning";
          invalid = true;
          continue;
        }
        RandomSubdomain nonnegative;
        nonnegative.offset = 31;
        nonnegative.width = 1;
        nonnegative.patterns.push_back({1, 0});
        properties.push_back(
            {property.source,
             property.field,
             {},
             builder.getI32Type(),
             32,
             property.modeIndex,
             true,
             property.type,
             constraintMask,
             unconditionalConstraint,
             {},
             {},
             {},
             0,
             true,
             false,
             {},
             {},
             {},
             {},
             {},
             {std::move(nonnegative)}});
      }
    }
    for (const RandomProperty &property : properties) {
      if (!property.nestedObjectField)
        continue;
      bool referenced = llvm::any_of(constraintRoots, [&](Operation *root) {
        auto block =
            root->getParentOfType<semantic::SVConstraintBlockSymbolOp>();
        auto owner = block ? nestedConstraintOwners.find(block)
                           : nestedConstraintOwners.end();
        if (owner != nestedConstraintOwners.end() &&
            owner->second->field == property.nestedObjectField)
          return false;
        bool found = false;
        root->walk([&](Operation *nested) {
          auto reference =
              nested->getAttrOfType<SymbolRefAttr>("referenced_symbol");
          if (reference && reference.getLeafReference() ==
                               property.source->getAttrOfType<StringAttr>(
                                   SymbolTable::getSymbolAttrName()))
            found = true;
        });
        return found;
      });
      if (referenced) {
        emitError(getSemanticLocation(property.source))
            << "constraints that dereference rand object handles require "
               "error-aware recursive constraint composition";
        invalid = true;
      }
    }
    llvm::SmallPtrSet<Operation *, 16> randomPropertySources;
    for (const RandomProperty &property : properties)
      randomPropertySources.insert(property.source);
    for (Operation *root : constraintRoots) {
      auto block = root->getParentOfType<semantic::SVConstraintBlockSymbolOp>();
      auto owner = block ? nestedConstraintOwners.find(block)
                         : nestedConstraintOwners.end();
      if (owner == nestedConstraintOwners.end())
        continue;
      root->walk([&](Operation *nested) {
        auto reference =
            nested->getAttrOfType<SymbolRefAttr>("referenced_symbol");
        auto symbol = reference
                          ? semanticSymbols.find(reference.getLeafReference())
                          : semanticSymbols.end();
        auto childProperty =
            symbol != semanticSymbols.end()
                ? dyn_cast<semantic::SVClassPropertySymbolOp>(symbol->second)
                : semantic::SVClassPropertySymbolOp{};
        if (childProperty && !randomPropertySources.contains(childProperty)) {
          emitError(getSemanticLocation(childProperty))
              << "nested object constraints that read non-random child state "
                 "require guarded child-state captures";
          invalid = true;
        }
      });
    }
    llvm::DenseMap<Operation *, unsigned> randomIndices;
    for (auto [index, property] : llvm::enumerate(properties))
      randomIndices[property.source] = index;
    // Clone declaration-owned constraints into the call before expanding
    // function calls. A dynamic randomize dispatch has one frozen call per
    // concrete class, so mutating the shared class declaration would leak one
    // plan's virtual resolution and state annotations into every other plan.
    OpBuilder constraintBuilder =
        OpBuilder::atBlockEnd(&call->getRegion(0).front());
    for (Operation *&root : constraintRoots) {
      if (root->getParentOfType<semantic::SVCallExpressionOp>() == call)
        continue;
      Operation *source = root;
      Operation *cloned = constraintBuilder.clone(*source);
      auto constraint =
          dyn_cast<semantic::SVConstraintBlockSymbolOp>(source->getParentOp());
      if (constraint)
        if (auto index = constraintIndices.find(constraint);
            index != constraintIndices.end())
          cloned->setAttr(randomConstraintBlockAttrName,
                          builder.getI32IntegerAttr(index->second));
      root = cloned;
    }

    struct RandomValuePath {
      unsigned property;
      uint64_t offset;
      unsigned width;
      bool precise;
      bool isState;
    };
    std::function<FailureOr<std::optional<RandomValuePath>>(Operation *)>
        getRandomValuePath;
    getRandomValuePath = [&](Operation *expression)
        -> FailureOr<std::optional<RandomValuePath>> {
      if (auto reference =
              expression->getAttrOfType<SymbolRefAttr>("referenced_symbol")) {
        auto symbol = semanticSymbols.find(reference.getLeafReference());
        auto index = symbol == semanticSymbols.end()
                         ? randomIndices.end()
                         : randomIndices.find(symbol->second);
        if (index != randomIndices.end())
          return std::optional<RandomValuePath>(RandomValuePath{
              index->second, 0,
              static_cast<unsigned>(properties[index->second].width), true,
              expression->hasAttr(randomFunctionStateAttrName)});
      }

      SmallVector<Operation *> children = getChildren(expression);
      if (children.empty())
        return std::optional<RandomValuePath>{};
      bool member = isa<semantic::SVMemberAccessExpressionOp>(expression);
      bool element = isa<semantic::SVElementSelectExpressionOp>(expression);
      auto range = dyn_cast<semantic::SVRangeSelectExpressionOp>(expression);
      if (!member && !element && !range)
        return std::optional<RandomValuePath>{};
      FailureOr<std::optional<RandomValuePath>> base =
          getRandomValuePath(children.front());
      if (failed(base) || !*base)
        return base;
      if (!(**base).precise)
        return base;
      FailureOr<Type> resultType = getNormalizedSemanticType(expression);
      std::optional<unsigned> resultWidth =
          succeeded(resultType) ? sim::getPackedWidth(*resultType)
                                : std::nullopt;
      if (!resultWidth || *resultWidth == 0) {
        emitError(getSemanticLocation(expression))
            << "constraint function ordering path has no packed width";
        return failure();
      }

      uint64_t relativeOffset = 0;
      if (member) {
        auto packedOffset =
            expression->getAttrOfType<IntegerAttr>("packed_offset");
        if (!packedOffset || packedOffset.getValue().isNegative() ||
            packedOffset.getValue().getActiveBits() > 64) {
          emitError(getSemanticLocation(expression))
              << "constraint function ordering path has malformed packed "
                 "member metadata";
          return failure();
        }
        relativeOffset = packedOffset.getValue().getZExtValue();
      } else {
        if (children.size() != (element ? 2u : 3u)) {
          emitError(getSemanticLocation(expression))
              << "constraint function ordering path has malformed selection "
                 "metadata";
          return failure();
        }
        auto parseKnownIndex =
            [&](Operation *index) -> FailureOr<std::optional<int64_t>> {
          std::optional<StringRef> spelling = getConstantSpelling(index);
          if (!spelling)
            return std::optional<int64_t>{};
          FailureOr<ParsedConstant> parsed =
              parseSVInteger(*spelling, 64, getSemanticLocation(index));
          if (failed(parsed))
            return failure();
          if (!parsed->unknown.isZero())
            return std::optional<int64_t>{};
          return std::optional<int64_t>(parsed->value.getSExtValue());
        };
        FailureOr<std::optional<int64_t>> first = parseKnownIndex(children[1]);
        if (failed(first))
          return failure();
        if (!*first) {
          (**base).precise = false;
          return base;
        }

        auto sourceTypeAttr =
            children.front()->getAttrOfType<TypeAttr>("semantic_type");
        if (!sourceTypeAttr) {
          emitError(getSemanticLocation(expression))
              << "constraint function ordering selection has no source type";
          return failure();
        }
        Type sourceType = sourceTypeAttr.getValue();
        if (auto enumeration = dyn_cast<semantic::EnumType>(sourceType))
          sourceType = enumeration.getBaseType();
        int64_t left = static_cast<int64_t>((*base)->width) - 1;
        int64_t right = 0;
        unsigned elementWidth = 1;
        if (auto integral = dyn_cast<semantic::IntegralType>(sourceType)) {
          left = integral.getLeft();
          right = integral.getRight();
        } else if (auto packed =
                       dyn_cast<semantic::RangedPackedArrayType>(sourceType)) {
          left = packed.getLeft();
          right = packed.getRight();
          APInt leftBound(65, static_cast<uint64_t>(left), true);
          APInt rightBound(65, static_cast<uint64_t>(right), true);
          APInt elementCount = leftBound - rightBound;
          if (elementCount.isNegative())
            elementCount = -elementCount;
          ++elementCount;
          if (elementCount.getActiveBits() > 64 ||
              elementCount.getZExtValue() == 0 ||
              elementCount.getZExtValue() > (**base).width ||
              (**base).width % elementCount.getZExtValue() != 0) {
            emitError(getSemanticLocation(expression))
                << "constraint function ordering selection has malformed "
                   "element width";
            return failure();
          }
          elementWidth = (**base).width / elementCount.getZExtValue();
        }
        bool descending = left >= right;
        auto physicalOffset = [&](int64_t index) -> std::optional<uint64_t> {
          APInt selected(65, static_cast<uint64_t>(index), true);
          APInt boundary(65, static_cast<uint64_t>(right), true);
          APInt ordinal =
              descending ? selected - boundary : boundary - selected;
          if (ordinal.isNegative() || ordinal.getActiveBits() > 64)
            return std::nullopt;
          APInt scaled = ordinal * APInt(65, elementWidth);
          if (scaled.getActiveBits() > 64)
            return std::nullopt;
          return scaled.getZExtValue();
        };
        std::optional<uint64_t> low = physicalOffset(**first);
        if (!low) {
          emitError(getSemanticLocation(expression))
              << "constraint function ordering selection is out of range";
          return failure();
        }
        if (element) {
          if (*resultWidth != elementWidth) {
            emitError(getSemanticLocation(expression))
                << "constraint function ordering element selection width is "
                   "inconsistent";
            return failure();
          }
        } else if (range) {
          if (range.getSelectionKind() ==
              semantic::SVRangeSelectionKind::Simple) {
            FailureOr<std::optional<int64_t>> second =
                parseKnownIndex(children[2]);
            if (failed(second))
              return failure();
            if (!*second) {
              (**base).precise = false;
              return base;
            }
            std::optional<uint64_t> other = physicalOffset(**second);
            if (!other) {
              emitError(getSemanticLocation(expression))
                  << "constraint function ordering selection is out of range";
              return failure();
            }
            uint64_t high = std::max(*low, *other);
            *low = std::min(*low, *other);
            uint64_t selectedWidth = high - *low + elementWidth;
            if (*resultWidth != selectedWidth) {
              emitError(getSemanticLocation(expression))
                  << "constraint function ordering range selection width is "
                     "inconsistent";
              return failure();
            }
          } else {
            FailureOr<std::optional<int64_t>> selectedElements =
                parseKnownIndex(children[2]);
            if (failed(selectedElements))
              return failure();
            if (!*selectedElements || **selectedElements <= 0 ||
                static_cast<uint64_t>(**selectedElements) >
                    UINT64_MAX / elementWidth ||
                static_cast<uint64_t>(**selectedElements) * elementWidth !=
                    *resultWidth) {
              emitError(getSemanticLocation(expression))
                  << "constraint function ordering indexed selection width "
                     "is inconsistent";
              return failure();
            }
            bool baseNamesHighBit =
                (descending &&
                 range.getSelectionKind() ==
                     semantic::SVRangeSelectionKind::IndexedDown) ||
                (!descending && range.getSelectionKind() ==
                                    semantic::SVRangeSelectionKind::IndexedUp);
            if (baseNamesHighBit && *resultWidth > elementWidth) {
              uint64_t adjustment = *resultWidth - elementWidth;
              if (*low < adjustment) {
                emitError(getSemanticLocation(expression))
                    << "constraint function ordering selection is out of "
                       "range";
                return failure();
              }
              *low -= adjustment;
            }
          }
        }
        relativeOffset = *low;
      }
      if (relativeOffset > (*base)->width ||
          *resultWidth > (*base)->width - relativeOffset) {
        emitError(getSemanticLocation(expression))
            << "constraint function ordering path is out of range";
        return failure();
      }
      (*base)->offset += relativeOffset;
      (*base)->width = *resultWidth;
      return base;
    };

    SmallVector<uint64_t> randomPropertyOffsets;
    uint64_t randomPropertyOffset = 0;
    for (const RandomProperty &property : properties) {
      randomPropertyOffsets.push_back(randomPropertyOffset);
      randomPropertyOffset += property.width;
    }

    // IEEE 1800 function arguments establish implicit solve ordering. Match
    // Slang's analysis exactly: rand value paths occurring in arguments to a
    // user function precede every non-overlapping rand value path outside such
    // an argument in the same expression constraint. The callee body is not
    // traversed for this analysis because its non-argument reads are state.
    for (Operation *root : constraintRoots) {
      SmallVector<std::pair<uint64_t, uint64_t>> functionOrder;
      root->walk([&](semantic::SVExpressionConstraintOp expression) {
        SmallVector<uint64_t> arguments;
        SmallVector<uint64_t> nonArguments;
        bool impreciseArguments = false;
        bool impreciseNonArguments = false;
        Operation *imprecisePath = nullptr;
        std::function<void(Operation *, bool)> collectReferences =
            [&](Operation *nested, bool inFunctionArgument) {
              if (auto function =
                      dyn_cast<semantic::SVCallExpressionOp>(nested);
                  function && !function.getIsSystemCall()) {
                SmallVector<Operation *> children = getChildren(function);
                uint64_t argumentCount = function.getArgumentCount();
                if (argumentCount > children.size()) {
                  emitError(getSemanticLocation(function))
                      << "constraint function has malformed argument metadata";
                  invalid = true;
                  return;
                }
                for (Operation *argument :
                     ArrayRef(children).take_back(argumentCount))
                  collectReferences(argument, true);
                return;
              }
              FailureOr<std::optional<RandomValuePath>> path =
                  getRandomValuePath(nested);
              if (failed(path)) {
                invalid = true;
                return;
              }
              if (*path) {
                const RandomValuePath &valuePath = **path;
                if (!valuePath.precise) {
                  (inFunctionArgument ? impreciseArguments
                                      : impreciseNonArguments) = true;
                  if (!imprecisePath)
                    imprecisePath = nested;
                } else {
                  uint64_t valueMask =
                      valuePath.width == 64
                          ? UINT64_MAX
                          : (uint64_t{1} << valuePath.width) - 1;
                  uint64_t globalOffset =
                      randomPropertyOffsets[valuePath.property] +
                      valuePath.offset;
                  if (globalOffset >= 64 ||
                      valuePath.width > 64 - globalOffset) {
                    (inFunctionArgument ? impreciseArguments
                                        : impreciseNonArguments) = true;
                    if (!imprecisePath)
                      imprecisePath = nested;
                    return;
                  }
                  uint64_t mask = valueMask << globalOffset;
                  SmallVector<uint64_t> &target =
                      inFunctionArgument ? arguments : nonArguments;
                  if (!llvm::is_contained(target, mask))
                    target.push_back(mask);
                }
                // A selected path consumes its base. Selection indices remain
                // independent expressions and can themselves name rand state.
                SmallVector<Operation *> children = getChildren(nested);
                if (isa<semantic::SVElementSelectExpressionOp,
                        semantic::SVRangeSelectExpressionOp>(nested))
                  for (Operation *index : ArrayRef(children).drop_front())
                    collectReferences(index, inFunctionArgument);
                return;
              }
              for (Operation *child : getChildren(nested))
                collectReferences(child, inFunctionArgument);
            };
        for (Operation *child : getChildren(expression))
          collectReferences(child, false);
        bool hasArgumentPath = impreciseArguments || !arguments.empty();
        bool hasNonArgumentPath =
            impreciseNonArguments || !nonArguments.empty();
        if (hasArgumentPath && hasNonArgumentPath &&
            (impreciseArguments || impreciseNonArguments)) {
          emitError(getSemanticLocation(imprecisePath))
              << "constraint function implicit ordering requires statically "
                 "selected rand paths";
          invalid = true;
          return;
        }
        for (uint64_t before : arguments)
          for (uint64_t after : nonArguments)
            if ((before & after) == 0 &&
                !llvm::is_contained(functionOrder,
                                    std::make_pair(before, after)))
              functionOrder.emplace_back(before, after);
      });
      if (!functionOrder.empty()) {
        SmallVector<int64_t> encoded;
        encoded.reserve(functionOrder.size() * 2);
        for (auto [before, after] : functionOrder) {
          encoded.push_back(static_cast<int64_t>(before));
          encoded.push_back(static_cast<int64_t>(after));
        }
        root->setAttr(randomFunctionOrderAttrName,
                      builder.getDenseI64ArrayAttr(encoded));
      }
    }

    auto resolveConstraintFunction = [&](semantic::SVCallExpressionOp function)
        -> FailureOr<semantic::SVSubroutineSymbolOp> {
      if (function.getIsSystemCall()) {
        emitError(getSemanticLocation(function))
            << "system function calls in constraints are not executable yet";
        return failure();
      }
      auto reference =
          function->getAttrOfType<SymbolRefAttr>("referenced_symbol");
      auto symbol = reference
                        ? semanticSymbols.find(reference.getLeafReference())
                        : semanticSymbols.end();
      auto target =
          symbol == semanticSymbols.end()
              ? semantic::SVSubroutineSymbolOp{}
              : dyn_cast<semantic::SVSubroutineSymbolOp>(symbol->second);
      if (!target) {
        emitError(getSemanticLocation(function))
            << "constraint function does not resolve to a subroutine";
        return failure();
      }
      if (target.getSubroutineKind() != semantic::SVSubroutineKind::Function ||
          target.getIsConstructor().value_or(false) ||
          target.getIsBuiltin().value_or(false) ||
          target.getIsDpiImport().value_or(false)) {
        emitError(getSemanticLocation(function))
            << "constraint calls require a user-defined SystemVerilog "
               "function";
        return failure();
      }

      semantic::SVClassTypeOp owner = getOwningClass(target);
      bool instanceMethod = owner && !target.getIsStatic().value_or(false);
      SmallVector<Operation *> callChildren = getChildren(function);
      uint64_t argumentCount = function.getArgumentCount();
      if (argumentCount > callChildren.size()) {
        emitError(getSemanticLocation(function))
            << "constraint function has malformed argument metadata";
        return failure();
      }
      ArrayRef<Operation *> receiverChildren =
          ArrayRef(callChildren).drop_back(argumentCount);
      if (instanceMethod) {
        if (!llvm::is_contained(hierarchy, owner)) {
          emitError(getSemanticLocation(function))
              << "constraint instance functions currently require the "
                 "randomized object as their receiver";
          return failure();
        }
        if (function.getHasThisClass()) {
          if (receiverChildren.size() != 1) {
            emitError(getSemanticLocation(function))
                << "constraint instance function has malformed receiver "
                   "metadata";
            return failure();
          }
          auto receiverRef =
              receiverChildren.front()->getAttrOfType<SymbolRefAttr>(
                  "referenced_symbol");
          auto receiver =
              receiverRef ? semanticSymbols.find(receiverRef.getLeafReference())
                          : semanticSymbols.end();
          auto variable =
              receiver == semanticSymbols.end()
                  ? semantic::SVVariableSymbolOp{}
                  : dyn_cast<semantic::SVVariableSymbolOp>(receiver->second);
          if (!variable || variable.getName().value_or("") != "this" ||
              !variable.getIsCompilerGenerated().value_or(false)) {
            emitError(getSemanticLocation(function))
                << "constraint instance functions currently require the "
                   "randomized object as their receiver";
            return failure();
          }
        } else if (!receiverChildren.empty()) {
          emitError(getSemanticLocation(function))
              << "constraint instance function has unexpected receiver "
                 "metadata";
          return failure();
        }
      } else if (!receiverChildren.empty()) {
        emitError(getSemanticLocation(function))
            << "constraint non-instance function has unexpected receiver "
               "metadata";
        return failure();
      }

      if (instanceMethod && target.getIsVirtual().value_or(false) &&
          !function.getIsSuperClass()) {
        auto slot = virtualMethodSlots.find(target);
        if (slot == virtualMethodSlots.end() || slot->second == UINT32_MAX) {
          emitError(getSemanticLocation(function))
              << "constraint virtual function has no executable dispatch "
                 "slot";
          return failure();
        }
        for (semantic::SVClassTypeOp classType : llvm::reverse(hierarchy)) {
          bool found = false;
          for (Operation *member : getChildren(classType)) {
            semantic::SVSubroutineSymbolOp candidate = getClassMethod(member);
            auto candidateSlot = virtualMethodSlots.find(candidate);
            if (candidate && candidateSlot != virtualMethodSlots.end() &&
                candidateSlot->second == slot->second) {
              target = candidate;
              found = true;
              break;
            }
          }
          if (found)
            break;
        }
      }
      return target;
    };

    auto getConstraintFunctionResult =
        [&](semantic::SVSubroutineSymbolOp function,
            semantic::SVCallExpressionOp call) -> FailureOr<Operation *> {
      SmallVector<semantic::SVReturnStatementOp> returns;
      SmallVector<semantic::SVExpressionStatementOp> expressionStatements;
      bool unsupportedStatement = false;
      bool unsupportedLocal = false;
      function->walk([&](Operation *nested) {
        if (nested == function.getOperation())
          return;
        if (auto local = dyn_cast<semantic::SVVariableSymbolOp>(nested)) {
          auto reference = FlatSymbolRefAttr::get(context, local.getSymName());
          bool compilerState =
              local.getIsCompilerGenerated().value_or(false) &&
              ((function.getReturnVariableSymbol() &&
                function.getReturnVariableSymbol()->getLeafReference() ==
                    reference.getValue()) ||
               (function.getThisVariableSymbol() &&
                function.getThisVariableSymbol()->getLeafReference() ==
                    reference.getValue()));
          unsupportedLocal |= !compilerState;
          return;
        }
        if (auto ret = dyn_cast<semantic::SVReturnStatementOp>(nested)) {
          returns.push_back(ret);
          return;
        }
        if (auto statement =
                dyn_cast<semantic::SVExpressionStatementOp>(nested)) {
          expressionStatements.push_back(statement);
          return;
        }
        StringRef name = nested->getName().getStringRef();
        if (name.starts_with("obelisk.sv.statement.") &&
            !isa<semantic::SVStatementListOp, semantic::SVBlockStatementOp>(
                nested))
          unsupportedStatement = true;
      });
      if (unsupportedLocal || unsupportedStatement) {
        emitError(getSemanticLocation(call))
            << "constraint function " << function.getName().value_or("")
            << " must be a side-effect-free expression function";
        return failure();
      }
      if (returns.size() == 1 && expressionStatements.empty()) {
        SmallVector<Operation *> values = getChildren(returns.front());
        if (values.size() == 1)
          return values.front();
      }
      if (returns.empty() && expressionStatements.size() == 1) {
        SmallVector<Operation *> statement =
            getChildren(expressionStatements.front());
        auto assignment = statement.size() == 1
                              ? dyn_cast<semantic::SVAssignmentExpressionOp>(
                                    statement.front())
                              : semantic::SVAssignmentExpressionOp{};
        SmallVector<Operation *> operands =
            assignment ? getChildren(assignment) : SmallVector<Operation *>{};
        auto lhs = operands.size() == 2
                       ? operands.front()->getAttrOfType<SymbolRefAttr>(
                             "referenced_symbol")
                       : SymbolRefAttr{};
        if (assignment &&
            assignment.getAssignmentKind() ==
                semantic::SVAssignmentKind::Blocking &&
            function.getReturnVariableSymbol() && lhs &&
            lhs.getLeafReference() ==
                function.getReturnVariableSymbol()->getLeafReference())
          return operands.back();
      }
      emitError(getSemanticLocation(call))
          << "constraint function " << function.getName().value_or("")
          << " must define its result with one return expression or one "
             "blocking assignment";
      return failure();
    };

    std::function<FailureOr<Operation *>(
        Operation *, const llvm::DenseMap<Operation *, Operation *> &, bool,
        SmallVectorImpl<Operation *> &)>
        cloneConstraintExpression;
    auto isRandomContainerSizeCall = [&](semantic::SVCallExpressionOp call) {
      if (!call.getIsSystemCall() || call.getCalleeName() != "size")
        return false;
      SmallVector<Operation *> operands = getChildren(call);
      if (operands.size() != 1)
        return false;
      auto reference =
          operands.front()->getAttrOfType<SymbolRefAttr>("referenced_symbol");
      auto symbol = reference
                        ? semanticSymbols.find(reference.getLeafReference())
                        : semanticSymbols.end();
      auto index = symbol == semanticSymbols.end()
                       ? randomIndices.end()
                       : randomIndices.find(symbol->second);
      return index != randomIndices.end() &&
             properties[index->second].isContainerSize;
    };
    cloneConstraintExpression =
        [&](Operation *source,
            const llvm::DenseMap<Operation *, Operation *> &substitutions,
            bool functionBody,
            SmallVectorImpl<Operation *> &callStack) -> FailureOr<Operation *> {
      if (auto reference =
              source->getAttrOfType<SymbolRefAttr>("referenced_symbol")) {
        auto symbol = semanticSymbols.find(reference.getLeafReference());
        if (symbol != semanticSymbols.end())
          if (auto substitution = substitutions.find(symbol->second);
              substitution != substitutions.end())
            return substitution->second->clone();
      }

      if (auto callExpression = dyn_cast<semantic::SVCallExpressionOp>(source);
          callExpression && !isRandomContainerSizeCall(callExpression)) {
        FailureOr<semantic::SVSubroutineSymbolOp> function =
            resolveConstraintFunction(callExpression);
        if (failed(function))
          return failure();
        if (llvm::is_contained(callStack, function->getOperation())) {
          emitError(getSemanticLocation(callExpression))
              << "recursive constraint function calls are not executable";
          return failure();
        }

        SmallVector<semantic::SVFormalArgumentSymbolOp> formals;
        for (Operation *child : getChildren(*function))
          if (auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child))
            formals.push_back(formal);
        SmallVector<Operation *> children = getChildren(callExpression);
        uint64_t argumentCount = callExpression.getArgumentCount();
        if (argumentCount > children.size() ||
            formals.size() != argumentCount) {
          emitError(getSemanticLocation(callExpression))
              << "constraint function argument count does not match its "
                 "declaration";
          return failure();
        }
        for (semantic::SVFormalArgumentSymbolOp formal : formals) {
          bool constReference =
              formal.getDirection() == semantic::SVArgumentDirection::Ref &&
              formal.getIsConst().value_or(false);
          if (formal.getDirection() != semantic::SVArgumentDirection::In &&
              !constReference) {
            emitError(getSemanticLocation(callExpression))
                << "constraint functions cannot have output, inout, or "
                   "non-const ref arguments";
            return failure();
          }
        }

        auto templates = std::make_unique<Block>();
        for (Operation *argument :
             ArrayRef(children).take_back(argumentCount)) {
          FailureOr<Operation *> cloned = cloneConstraintExpression(
              argument, substitutions, functionBody, callStack);
          if (failed(cloned))
            return failure();
          templates->push_back(*cloned);
        }
        llvm::DenseMap<Operation *, Operation *> functionSubstitutions;
        for (auto [formal, actual] :
             llvm::zip_equal(formals, templates->getOperations()))
          functionSubstitutions[formal.getOperation()] = &actual;

        FailureOr<Operation *> result =
            getConstraintFunctionResult(*function, callExpression);
        if (failed(result))
          return failure();
        callStack.push_back(function->getOperation());
        FailureOr<Operation *> cloned = cloneConstraintExpression(
            *result, functionSubstitutions, true, callStack);
        callStack.pop_back();
        return cloned;
      }

      Operation *cloned = source->cloneWithoutRegions();
      if (functionBody) {
        if (auto reference =
                source->getAttrOfType<SymbolRefAttr>("referenced_symbol")) {
          auto symbol = semanticSymbols.find(reference.getLeafReference());
          if (symbol != semanticSymbols.end() &&
              randomIndices.contains(symbol->second))
            cloned->setAttr(randomFunctionStateAttrName, builder.getUnitAttr());
        }
      }
      for (auto [sourceRegion, clonedRegion] :
           llvm::zip_equal(source->getRegions(), cloned->getRegions())) {
        for (Block &sourceBlock : sourceRegion) {
          auto *clonedBlock = new Block();
          clonedRegion.push_back(clonedBlock);
          for (Operation &child : sourceBlock) {
            FailureOr<Operation *> clonedChild = cloneConstraintExpression(
                &child, substitutions, functionBody, callStack);
            if (failed(clonedChild)) {
              cloned->destroy();
              return failure();
            }
            clonedBlock->push_back(*clonedChild);
          }
        }
      }
      return cloned;
    };

    for (Operation *&root : constraintRoots) {
      llvm::DenseMap<Operation *, Operation *> substitutions;
      SmallVector<Operation *> callStack;
      FailureOr<Operation *> expanded =
          cloneConstraintExpression(root, substitutions, false, callStack);
      if (failed(expanded)) {
        invalid = true;
        return true;
      }
      root->getBlock()->getOperations().insert(root->getIterator(), *expanded);
      root->erase();
      root = *expanded;
    }

    uint64_t totalWidth = 0;
    for (const RandomProperty &property : properties) {
      if (property.width > UINT32_MAX - totalWidth) {
        emitError(getSemanticLocation(call))
            << "the executable randomization plan exceeds its 32-bit bit "
               "offset space";
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
        auto call = dyn_cast<semantic::SVCallExpressionOp>(nested);
        if (nested->hasTrait<OpTrait::SemanticASTNode>() &&
            !isSupportedRandomConstraintExpression(nested) &&
            !(call && isRandomContainerSizeCall(call))) {
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
    for (const RandomProperty &property : properties) {
      SmallVector<NamedAttribute> attributes{
          builder.getNamedAttr("type", TypeAttr::get(property.type)),
          builder.getNamedAttr("width",
                               builder.getI64IntegerAttr(property.width)),
          builder.getNamedAttr(randomPropertyModeIndexAttrName,
                               builder.getI32IntegerAttr(property.modeIndex)),
          builder.getNamedAttr("is_signed",
                               builder.getBoolAttr(property.isSigned)),
          builder.getNamedAttr("is_randc",
                               builder.getBoolAttr(property.isRandC)),
      };
      if (property.isContainerSize) {
        attributes.push_back(builder.getNamedAttr(randomContainerSizeAttrName,
                                                  builder.getUnitAttr()));
        attributes.push_back(builder.getNamedAttr(
            randomContainerTypeAttrName, TypeAttr::get(property.containerType)));
        attributes.push_back(builder.getNamedAttr(
            "size_constraint_mask",
            builder.getIntegerAttr(builder.getI64Type(),
                                   APInt(64, property.sizeConstraintMask))));
        attributes.push_back(builder.getNamedAttr(
            "unconditional_size_constraint",
            builder.getBoolAttr(property.hasUnconditionalSizeConstraint)));
      }
      if (property.nestedObjectField) {
        attributes.push_back(builder.getNamedAttr(
            randomNestedObjectFieldAttrName, property.nestedObjectField));
        attributes.push_back(builder.getNamedAttr(
            randomNestedObjectTypeAttrName,
            TypeAttr::get(property.nestedObjectType)));
        attributes.push_back(builder.getNamedAttr(
            randomNestedObjectStorageTypeAttrName,
            TypeAttr::get(property.nestedObjectStorageType)));
        attributes.push_back(builder.getNamedAttr(
            randomNestedModeIndexAttrName,
            builder.getI32IntegerAttr(property.nestedModeIndex)));
      }
      if (property.field)
        attributes.push_back(builder.getNamedAttr("field", property.field));
      else
        attributes.push_back(builder.getNamedAttr(randomPropertyPathAttrName,
                                                  property.referencePath));
      if (property.randomModeStorage)
        attributes.push_back(builder.getNamedAttr(
            randomPropertyModeStorageAttrName, property.randomModeStorage));
      if (property.isRandC) {
        if (property.randcKeyField) {
          attributes.push_back(
              builder.getNamedAttr("randc_key_field", property.randcKeyField));
          attributes.push_back(builder.getNamedAttr(
              "randc_position_field", property.randcPositionField));
        } else {
          attributes.push_back(builder.getNamedAttr(randomRandCKeyPathAttrName,
                                                    property.randcKeyPath));
          attributes.push_back(builder.getNamedAttr(
              randomRandCPositionPathAttrName, property.randcPositionPath));
        }
      }
      if (!property.domains.empty()) {
        SmallVector<Attribute> domains;
        for (const RandomSubdomain &domain : property.domains) {
          SmallVector<Attribute> patterns;
          for (const RandomDomainPattern &pattern : domain.patterns) {
            patterns.push_back(builder.getDictionaryAttr({
                builder.getNamedAttr(
                    "mask", builder.getIntegerAttr(builder.getI64Type(),
                                                   APInt(64, pattern.mask))),
                builder.getNamedAttr(
                    "value", builder.getIntegerAttr(builder.getI64Type(),
                                                    APInt(64, pattern.value))),
            }));
          }
          domains.push_back(builder.getDictionaryAttr({
              builder.getNamedAttr("offset",
                                   builder.getI64IntegerAttr(domain.offset)),
              builder.getNamedAttr("width",
                                   builder.getI64IntegerAttr(domain.width)),
              builder.getNamedAttr("patterns", builder.getArrayAttr(patterns)),
          }));
        }
        attributes.push_back(
            builder.getNamedAttr("domains", builder.getArrayAttr(domains)));
      }
      propertyAttrs.push_back(builder.getDictionaryAttr(attributes));
    }
    call->setAttr(randomizeAttrName, builder.getUnitAttr());
    call->setAttr(randomReceiverIndexAttrName,
                  builder.getI32IntegerAttr(receiverIndex));
    call->setAttr(randomPropertiesAttrName,
                  builder.getArrayAttr(propertyAttrs));
    SmallVector<Attribute> containerPropertyAttrs;
    for (const RandomContainerProperty &property : containerProperties)
      containerPropertyAttrs.push_back(builder.getDictionaryAttr({
          builder.getNamedAttr("field", property.field),
          builder.getNamedAttr("type", TypeAttr::get(property.type)),
          builder.getNamedAttr("element_type",
                               TypeAttr::get(property.elementType)),
          builder.getNamedAttr("element_width",
                               builder.getI64IntegerAttr(property.elementWidth)),
          builder.getNamedAttr(randomPropertyModeIndexAttrName,
                               builder.getI32IntegerAttr(property.modeIndex)),
      }));
    call->setAttr(randomContainerPropertiesAttrName,
                  builder.getArrayAttr(containerPropertyAttrs));
    SmallVector<Attribute> nestedConstraintModeAttrs;
    for (const NestedObjectPlan &plan : nestedObjectPlans) {
      if (plan.globalConstraintIndices.empty())
        continue;
      SmallVector<int64_t> indices;
      indices.reserve(plan.globalConstraintIndices.size());
      for (unsigned index : plan.globalConstraintIndices)
        indices.push_back(index);
      nestedConstraintModeAttrs.push_back(builder.getDictionaryAttr({
          builder.getNamedAttr("field", plan.field),
          builder.getNamedAttr("concrete_type",
                               TypeAttr::get(plan.concreteType)),
          builder.getNamedAttr("storage_type", TypeAttr::get(plan.storageType)),
          builder.getNamedAttr("global_indices",
                               builder.getDenseI64ArrayAttr(indices)),
      }));
    }
    call->setAttr(randomNestedConstraintModesAttrName,
                  builder.getArrayAttr(nestedConstraintModeAttrs));
    SmallVector<Attribute> nestedHookAttrs;
    for (const NestedObjectPlan &plan : nestedObjectPlans) {
      if (!plan.preHook && !plan.postHook)
        continue;
      SmallVector<NamedAttribute> attributes{
          builder.getNamedAttr("field", plan.field),
          builder.getNamedAttr("concrete_type",
                               TypeAttr::get(plan.concreteType)),
          builder.getNamedAttr("storage_type", TypeAttr::get(plan.storageType)),
          builder.getNamedAttr("outer_mode_index",
                               builder.getI32IntegerAttr(plan.outerModeIndex)),
      };
      auto addHook = [&](semantic::SVSubroutineSymbolOp hook,
                         StringRef prefix) {
        if (!hook)
          return;
        auto callee = directCalleeNames.find(hook);
        semantic::SVClassTypeOp owner = getOwningClass(hook);
        StringAttr ownerSymbol =
            owner ? classSymbols.lookup(owner) : StringAttr{};
        if (callee == directCalleeNames.end() || !ownerSymbol) {
          emitError(getSemanticLocation(hook))
              << "nested randomization hook has no executable class method";
          invalid = true;
          return;
        }
        attributes.push_back(builder.getNamedAttr(
            (prefix + "_source").str(),
            FlatSymbolRefAttr::get(context, hook.getSymName())));
        attributes.push_back(builder.getNamedAttr(
            (prefix + "_callee").str(),
            FlatSymbolRefAttr::get(context, callee->second)));
        attributes.push_back(builder.getNamedAttr(
            (prefix + "_owner").str(),
            FlatSymbolRefAttr::get(context, ownerSymbol.getValue())));
      };
      addHook(plan.preHook, "pre");
      addHook(plan.postHook, "post");
      nestedHookAttrs.push_back(builder.getDictionaryAttr(attributes));
    }
    call->setAttr(randomNestedHooksAttrName,
                  builder.getArrayAttr(nestedHookAttrs));
    call->setAttr(randomTotalWidthAttrName,
                  builder.getI64IntegerAttr(totalWidth));
    call->setAttr(randomConstraintCountAttrName,
                  builder.getI32IntegerAttr(constraintGroups.size()));
    call->setAttr(constraintModeStaticStoragesAttrName,
                  builder.getDenseI64ArrayAttr(*staticConstraintStorages));

    // Property arguments name object fields; they are compile-time controls,
    // not expressions evaluated by the randomize call. Once their exact set
    // has been frozen, remove them so capture analysis does not mistake the
    // names for ordinary unit-local reads.
    if (explicitPropertyList) {
      for (Operation *argument : explicitPropertyArguments)
        argument->erase();
      call->setAttr("argument_count", builder.getI64IntegerAttr(1));
      call->setAttr("defaulted_arguments", builder.getDenseI64ArrayAttr({0}));
    }

    auto annotateConstraint = [&](Operation *constraint) {
      constraint->walk([&](Operation *nested) {
        auto reference =
            nested->getAttrOfType<SymbolRefAttr>("referenced_symbol");
        if (!reference)
          return;
        auto symbol = semanticSymbols.find(reference.getLeafReference());
        if (symbol == semanticSymbols.end())
          return;
        auto property =
            dyn_cast<semantic::SVClassPropertySymbolOp>(symbol->second);
        if ((!property ||
             property.getLifetime() != semantic::SVVariableLifetime::Static))
          if (auto field = classFieldSymbols.find(symbol->second);
              field != classFieldSymbols.end())
            nested->setAttr("obelisk_sim.class_field", field->second);
        if (auto index = randomIndices.find(symbol->second);
            index != randomIndices.end() &&
            !nested->hasAttr(randomFunctionStateAttrName)) {
          if (properties[index->second].isContainerSize) {
            auto sizeCall = dyn_cast_or_null<semantic::SVCallExpressionOp>(
                nested->getParentOp());
            if (!sizeCall || sizeCall.getCalleeName() != "size") {
              emitError(getSemanticLocation(nested))
                  << "a constrained dynamic container may only participate "
                     "through its size() value";
              invalid = true;
            } else {
              sizeCall->setAttr(randomVariableAttrName,
                                builder.getI32IntegerAttr(index->second));
            }
          } else {
            nested->setAttr(randomVariableAttrName,
                            builder.getI32IntegerAttr(index->second));
          }
        }
        if (isa<semantic::SVParameterSymbolOp, semantic::SVEnumValueSymbolOp,
                semantic::SVSpecparamSymbolOp>(symbol->second))
          if (auto constant =
                  symbol->second->getAttrOfType<StringAttr>("constant_value"))
            nested->setAttr("obelisk_sim.constant_value", constant);
      });
      constraint->walk([&](Operation *nested) {
        if (!isa<semantic::SVMemberAccessExpressionOp,
                 semantic::SVElementSelectExpressionOp,
                 semantic::SVRangeSelectExpressionOp>(nested))
          return;
        FailureOr<std::optional<RandomValuePath>> path =
            getRandomValuePath(nested);
        if (failed(path)) {
          invalid = true;
          return;
        }
        if (!*path || !(**path).precise || (**path).isState)
          return;
        uint64_t globalOffset =
            randomPropertyOffsets[(**path).property] + (**path).offset;
        if (globalOffset > UINT32_MAX ||
            (**path).width > UINT32_MAX - globalOffset) {
          invalid = true;
          return;
        }
        nested->setAttr(randomVariableBitOffsetAttrName,
                        builder.getI64IntegerAttr(globalOffset));
      });
    };
    for (Operation *root : constraintRoots)
      annotateConstraint(root);
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

      semantic::SVClassTypeOp owner = getOwningClass(target);
      struct DynamicClass {
        semantic::SVClassTypeOp type;
        unsigned depth;
        SmallVector<int64_t> staticModeStorages;
      };
      SmallVector<DynamicClass> compatible;
      bool hasStaticRandomProperty = false;
      for (semantic::SVClassTypeOp candidate : classSources) {
        if (candidate.getIsAbstract() || candidate.getIsInterface())
          continue;
        SmallVector<semantic::SVClassTypeOp> hierarchy;
        if (failed(collectClassHierarchy(candidate, hierarchy, "rand_mode"))) {
          invalid = true;
          return true;
        }
        if (!llvm::is_contained(hierarchy, owner))
          continue;
        SmallVector<int64_t> storages;
        for (semantic::SVClassTypeOp current : hierarchy) {
          for (Operation *member : getChildren(current)) {
            auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(member);
            if (!property ||
                property.getLifetime() !=
                    semantic::SVVariableLifetime::Static ||
                property.getRandMode() == semantic::SVRandMode::None)
              continue;
            auto storage = property->getAttrOfType<IntegerAttr>(
                staticRandomModeStorageAttrName);
            if (!storage || storage.getValue().isNegative() ||
                storage.getValue().getActiveBits() > 63) {
              emitError(getSemanticLocation(property))
                  << "static random property has no valid shared rand_mode "
                     "storage";
              invalid = true;
              return true;
            }
            storages.push_back(
                static_cast<int64_t>(storage.getValue().getZExtValue()));
          }
        }
        hasStaticRandomProperty |= !storages.empty();
        compatible.push_back({candidate,
                              static_cast<unsigned>(hierarchy.size()),
                              std::move(storages)});
      }
      if (hasStaticRandomProperty) {
        llvm::sort(compatible,
                   [&](const DynamicClass &lhs, const DynamicClass &rhs) {
                     if (lhs.depth != rhs.depth)
                       return lhs.depth > rhs.depth;
                     return classSymbols.lookup(lhs.type).getValue() <
                            classSymbols.lookup(rhs.type).getValue();
                   });
        SmallVector<Attribute> dispatch;
        for (const DynamicClass &entry : compatible) {
          StringAttr className = classSymbols.lookup(entry.type);
          if (!className) {
            emitError(getSemanticLocation(entry.type))
                << "rand_mode dispatch class has no prepared symbol";
            invalid = true;
            return true;
          }
          FlatSymbolRefAttr classSymbol =
              FlatSymbolRefAttr::get(context, className.getValue());
          dispatch.push_back(builder.getDictionaryAttr({
              builder.getNamedAttr("class", classSymbol),
              builder.getNamedAttr("storages", builder.getDenseI64ArrayAttr(
                                                   entry.staticModeStorages)),
          }));
        }
        call->setAttr(randomModeStaticDispatchAttrName,
                      builder.getArrayAttr(dispatch));
      }
      return true;
    }

    Operation *propertyExpression = callChildren.front();
    auto member =
        dyn_cast<semantic::SVMemberAccessExpressionOp>(propertyExpression);
    auto named =
        dyn_cast<semantic::SVNamedValueExpressionOp>(propertyExpression);
    auto reference = member || named
                         ? propertyExpression->getAttrOfType<SymbolRefAttr>(
                               "referenced_symbol")
                         : SymbolRefAttr{};
    auto symbol = reference ? semanticSymbols.find(reference.getLeafReference())
                            : semanticSymbols.end();
    auto property =
        symbol != semanticSymbols.end()
            ? dyn_cast<semantic::SVClassPropertySymbolOp>(symbol->second)
            : semantic::SVClassPropertySymbolOp{};
    if (!property || property.getRandMode() == semantic::SVRandMode::None)
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
        if (!candidate || candidate.getRandMode() == semantic::SVRandMode::None)
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
    if (property.getLifetime() == semantic::SVVariableLifetime::Static) {
      auto storage =
          property->getAttrOfType<IntegerAttr>(staticRandomModeStorageAttrName);
      if (!storage || storage.getValue().isNegative() ||
          storage.getValue().getActiveBits() > 63) {
        emitError(getSemanticLocation(call))
            << "static property rand_mode has no valid shared storage";
        invalid = true;
        return true;
      }
      call->setAttr(randomModeStaticStorageAttrName, storage);
    } else {
      call->setAttr(randomModePropertyAttrName,
                    builder.getI32IntegerAttr(propertyIndex));
    }
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
      semantic::SVClassTypeOp owner = getOwningClass(target);
      SmallVector<semantic::SVClassTypeOp> hierarchy;
      if (failed(collectClassHierarchy(owner, hierarchy, "constraint_mode"))) {
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
      FailureOr<SmallVector<int64_t>> staticStorages =
          collectStaticConstraintStorages(groups, getSemanticLocation(call));
      if (failed(staticStorages)) {
        invalid = true;
        return true;
      }
      call->setAttr(constraintModeAttrName, builder.getUnitAttr());
      call->setAttr(constraintModeStaticStoragesAttrName,
                    builder.getDenseI64ArrayAttr(*staticStorages));
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
    if (failed(collectClassHierarchy(owner, hierarchy, "constraint_mode"))) {
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
    FailureOr<SmallVector<int64_t>> staticStorages =
        collectStaticConstraintStorages(groups, getSemanticLocation(call));
    if (failed(staticStorages)) {
      invalid = true;
      return true;
    }
    if ((*staticStorages)[*constraintIndex] >= 0)
      call->setAttr(
          constraintModeStaticStorageAttrName,
          builder.getI64IntegerAttr((*staticStorages)[*constraintIndex]));
    return true;
  };

  auto freezeObjectRandomDispatch =
      [&](semantic::SVCallExpressionOp call) -> bool {
    StringRef name = call.getCalleeName();
    if (name != "get_randstate" && name != "set_randstate" && name != "srandom")
      return false;
    if (call->hasAttr(objectRandomDispatchClassesAttrName))
      return true;
    SmallVector<Operation *> children = getChildren(call);
    if (children.empty())
      return false;
    auto typeAttr = children.front()->getAttrOfType<TypeAttr>("semantic_type");
    auto receiverType =
        typeAttr ? dyn_cast<semantic::ClassHandleType>(typeAttr.getValue())
                 : semantic::ClassHandleType{};
    if (!receiverType)
      return false;
    auto foundClass =
        semanticClasses.find(receiverType.getClassName().getLeafReference());
    if (foundClass == semanticClasses.end() ||
        !foundClass->second.getIsInterface())
      return false;

    struct DynamicClass {
      semantic::SVClassTypeOp type;
      unsigned depth;
    };
    SmallVector<DynamicClass> compatible;
    StringRef target = receiverType.getClassName().getLeafReference();
    for (semantic::SVClassTypeOp candidate : classSources) {
      if (candidate.getIsAbstract() || candidate.getIsInterface())
        continue;
      SmallVector<semantic::SVClassTypeOp> hierarchy;
      if (failed(collectClassHierarchy(candidate, hierarchy,
                                       "object random-stream dispatch"))) {
        invalid = true;
        return true;
      }
      bool matches = false;
      for (semantic::SVClassTypeOp current : hierarchy) {
        for (Attribute attribute : current.getImplementedInterfaces()) {
          auto interfaceType = dyn_cast<TypeAttr>(attribute);
          auto interface = interfaceType ? dyn_cast<semantic::ClassHandleType>(
                                               interfaceType.getValue())
                                         : semantic::ClassHandleType{};
          if (interface &&
              interface.getClassName().getLeafReference() == target) {
            matches = true;
            break;
          }
        }
        if (matches)
          break;
      }
      if (matches)
        compatible.push_back(
            {candidate, static_cast<unsigned>(hierarchy.size())});
    }
    llvm::sort(compatible,
               [&](const DynamicClass &lhs, const DynamicClass &rhs) {
                 if (lhs.depth != rhs.depth)
                   return lhs.depth > rhs.depth;
                 return classSymbols.lookup(lhs.type).getValue() <
                        classSymbols.lookup(rhs.type).getValue();
               });
    SmallVector<Attribute> classes;
    for (const DynamicClass &entry : compatible)
      classes.push_back(FlatSymbolRefAttr::get(
          context, classSymbols.lookup(entry.type).getValue()));
    call->setAttr(objectRandomDispatchClassesAttrName,
                  builder.getArrayAttr(classes));
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
    if (!freezeRandModeContract(call) && !freezeConstraintModeContract(call) &&
        !freezeObjectRandomDispatch(call))
      freezeRandomizeContract(call);
  if (invalid)
    return abort();

  // Freeze static property selections before capture analysis. Such a member
  // is an addressable class-wide storage root, while its object prefix is only
  // an evaluated qualifier and must not be classified as the assigned base.
  semanticRoot->walk([&](semantic::SVMemberAccessExpressionOp member) {
    auto symbol =
        semanticSymbols.find(member.getReferencedSymbol().getLeafReference());
    auto property =
        symbol != semanticSymbols.end()
            ? dyn_cast<semantic::SVClassPropertySymbolOp>(symbol->second)
            : semantic::SVClassPropertySymbolOp{};
    if (property &&
        property.getLifetime() == semantic::SVVariableLifetime::Static)
      member->setAttr(staticClassPropertyAttrName, builder.getUnitAttr());
  });

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
    if (failed(freeze(randomPreHookSourceAttrName,
                      randomPreHookCapturesAttrName,
                      randomPreHookReadCapturesAttrName)) ||
        failed(freeze(randomPostHookSourceAttrName,
                      randomPostHookCapturesAttrName,
                      randomPostHookReadCapturesAttrName)))
      return failure();
    auto nestedHooks = call->getAttrOfType<ArrayAttr>(randomNestedHooksAttrName);
    if (!nestedHooks)
      return success();
    SmallVector<Attribute> frozenHooks;
    for (Attribute hookAttr : nestedHooks) {
      auto hook = dyn_cast<DictionaryAttr>(hookAttr);
      if (!hook)
        return failure();
      SmallVector<NamedAttribute> attributes(hook.begin(), hook.end());
      for (StringRef prefix : {StringRef("pre"), StringRef("post")}) {
        auto source = hook.getAs<FlatSymbolRefAttr>((prefix + "_source").str());
        if (!source)
          continue;
        auto found = semanticSymbols.find(source.getLeafReference());
        if (found == semanticSymbols.end()) {
          emitError(getSemanticLocation(call))
              << "nested randomization hook source no longer resolves";
          return failure();
        }
        SmallVector<Attribute> captures;
        SmallVector<Attribute> reads;
        for (const auto &capture : unitCaptures[found->second]) {
          captures.push_back(builder.getStringAttr(capture.first));
          if (unitReadCaptures[found->second].contains(capture.first))
            reads.push_back(builder.getStringAttr(capture.first));
        }
        attributes.push_back(builder.getNamedAttr(
            (prefix + "_captures").str(), builder.getArrayAttr(captures)));
        attributes.push_back(builder.getNamedAttr(
            (prefix + "_reads").str(), builder.getArrayAttr(reads)));
      }
      frozenHooks.push_back(builder.getDictionaryAttr(attributes));
    }
    call->setAttr(randomNestedHooksAttrName,
                  builder.getArrayAttr(frozenHooks));
    return success();
  };

  auto freezeCallContract = [&](semantic::SVCallExpressionOp call) {
    // Virtual-interface calls are frozen while the original semantic tree is
    // still intact.  Clones inherit this contract and must not try to resolve
    // the (by then potentially erased) semantic callees again.
    if (call->hasAttr("obelisk_sim.virtual_interface_callees"))
      return;
    if (freezeRandModeContract(call))
      return;
    if (freezeConstraintModeContract(call))
      return;
    if (freezeObjectRandomDispatch(call))
      return;
    if (freezeRandomizeContract(call)) {
      if (failed(freezeRandomizeHookCaptures(call)))
        invalid = true;
      return;
    }
    SmallVector<Operation *> virtualTargets =
        preparedUnits->resolveVirtualInterfaceCallees(call);
    llvm::sort(virtualTargets, [&](Operation *lhs, Operation *rhs) {
      return preparedUnits->declarations.lookup(lhs).getScopeId() <
             preparedUnits->declarations.lookup(rhs).getScopeId();
    });
    Operation *targetSource = resolveDirectCallee(call);
    if (!targetSource && !virtualTargets.empty())
      targetSource = virtualTargets.front();
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
    } else if (auto targetSubroutine =
                   dyn_cast<semantic::SVSubroutineSymbolOp>(targetSource);
               targetSubroutine && getOwningClass(targetSubroutine) &&
               targetSubroutine.getIsStatic().value_or(false) &&
               call.getHasThisClass()) {
      call->setAttr(staticClassReceiverAttrName, builder.getUnitAttr());
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
    if (!virtualTargets.empty()) {
      SmallVector<Attribute> candidates;
      for (Operation *candidate : virtualTargets) {
        SmallVector<Attribute> captures;
        SmallVector<Attribute> readCaptures;
        for (const auto &capture : unitCaptures[candidate])
          captures.push_back(builder.getStringAttr(capture.first));
        for (const auto &capture : unitCaptures[candidate])
          if (unitReadCaptures[candidate].contains(capture.first))
            readCaptures.push_back(builder.getStringAttr(capture.first));
        candidates.push_back(builder.getDictionaryAttr({
            builder.getNamedAttr(
                "scope", builder.getI64IntegerAttr(
                             preparedUnits->declarations.lookup(candidate)
                                 .getScopeId())),
            builder.getNamedAttr(
                "callee", FlatSymbolRefAttr::get(
                              context, directCalleeNames.lookup(candidate))),
            builder.getNamedAttr("captures", builder.getArrayAttr(captures)),
            builder.getNamedAttr("read_captures",
                                 builder.getArrayAttr(readCaptures)),
        }));
      }
      call->setAttr("obelisk_sim.virtual_interface_callees",
                    builder.getArrayAttr(candidates));
    }
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

  // Freeze virtual dispatch before materializing code units can erase their
  // semantic source operations.  The complete candidate set, ABI, and capture
  // paths are immutable attributes copied along with every later call clone.
  semanticRoot->walk([&](semantic::SVCallExpressionOp call) {
    if (!preparedUnits->resolveVirtualInterfaceCallees(call).empty())
      freezeCallContract(call);
  });

  auto constructorSourceFor = [](semantic::SVClassTypeOp classType) {
    for (Operation *child : getChildren(classType)) {
      semantic::SVSubroutineSymbolOp method = getClassMethod(child);
      if (method && method.getIsConstructor().value_or(false))
        return method;
    }
    return semantic::SVSubroutineSymbolOp{};
  };
  auto constructorSymbolFor =
      [&](semantic::SVClassTypeOp classType) -> FlatSymbolRefAttr {
    if (FlatSymbolRefAttr implicit =
            implicitConstructorSymbols.lookup(classType))
      return implicit;
    semantic::SVSubroutineSymbolOp method = constructorSourceFor(classType);
    auto found =
        method ? directCalleeNames.find(method) : directCalleeNames.end();
    return found == directCalleeNames.end()
               ? FlatSymbolRefAttr{}
               : FlatSymbolRefAttr::get(context, found->second);
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
    if (unit.entryKind == sim::EntryKind::Task)
      if (auto targetID = unit.source->getAttrOfType<IntegerAttr>(
              "obelisk_sim.control_target_id"))
        functionAttrs.push_back(
            builder.getNamedAttr("obelisk_sim.control_target_id", targetID));
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
      auto clonePropertyInitializers = [&](OpBuilder &initializerBuilder) {
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
          Operation *cloned = initializerBuilder.clone(*initializer.front());
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
        clonePropertyInitializers(bodyBuilder);
        initialized = true;
      }
      if (constructor && owner && owner.getBaseClass()) {
        bool hasExplicitSuperCall = false;
        for (Operation *child : getChildren(unit.source))
          child->walk([&](semantic::SVNewClassExpressionOp construct) {
            hasExplicitSuperCall |= construct.getIsSuperClass();
          });
        auto baseHandle =
            dyn_cast<semantic::ClassHandleType>(*owner.getBaseClass());
        auto base = baseHandle
                        ? semanticClasses.find(
                              baseHandle.getClassName().getLeafReference())
                        : semanticClasses.end();
        if (base == semanticClasses.end()) {
          emitError(getSemanticLocation(owner))
              << "constructor cannot resolve its base class";
          invalid = true;
        } else if (!hasExplicitSuperCall) {
          semantic::SVCallExpressionOp extendsCall;
          for (Operation *member : getChildren(owner)) {
            auto call = dyn_cast<semantic::SVCallExpressionOp>(member);
            auto target =
                call ? dyn_cast_or_null<semantic::SVSubroutineSymbolOp>(
                           resolveDirectCallee(call))
                     : semantic::SVSubroutineSymbolOp{};
            if (!target || !target.getIsConstructor().value_or(false) ||
                getOwningClass(target) != base->second)
              continue;
            extendsCall = call;
            break;
          }
          if (extendsCall) {
            auto cloned = cast<semantic::SVCallExpressionOp>(
                bodyBuilder.clone(*extendsCall));
            // Slang represents a constructor call in an extends clause as a
            // direct child of the class rather than of the explicit
            // constructor. Execute it with the current object as the base
            // receiver before initializing the derived fields.
            cloned->setAttr("obelisk_sim.class_super", builder.getUnitAttr());
          } else if (semantic::SVSubroutineSymbolOp baseConstructor =
                         constructorSourceFor(base->second)) {
            SmallVector<Operation *> defaults;
            bool defaultsValid = true;
            for (Operation *member : getChildren(baseConstructor)) {
              auto formal =
                  dyn_cast<semantic::SVFormalArgumentSymbolOp>(member);
              if (!formal)
                continue;
              SmallVector<Operation *> initializer = getChildren(formal);
              if (initializer.size() != 1) {
                emitError(getSemanticLocation(subroutine))
                    << "implicit base constructor call requires a default "
                       "for every formal";
                invalid = true;
                defaultsValid = false;
                break;
              }
              defaults.push_back(initializer.front());
            }
            if (defaultsValid) {
              OperationState callState(
                  getSemanticLocation(subroutine),
                  semantic::SVCallExpressionOp::getOperationName());
              callState.addAttribute(
                  "node_id", builder.getI64IntegerAttr(subroutine.getNodeId()));
              callState.addAttribute(
                  "semantic_type",
                  TypeAttr::get(semantic::VoidType::get(context)));
              callState.addAttribute("callee_name",
                                     builder.getStringAttr("new"));
              callState.addAttribute("is_system_call",
                                     builder.getBoolAttr(false));
              callState.addAttribute(
                  "subroutine_kind",
                  semantic::SVSubroutineKindAttr::get(
                      context, baseConstructor.getSubroutineKind()));
              callState.addAttribute(
                  "argument_count", builder.getI64IntegerAttr(defaults.size()));
              callState.addAttribute("has_this_class",
                                     builder.getBoolAttr(false));
              callState.addAttribute("is_super_class",
                                     builder.getBoolAttr(true));
              callState.addAttribute("has_output_arguments",
                                     builder.getBoolAttr(false));
              callState.addAttribute(
                  "referenced_path",
                  builder.getStringAttr(getHierarchyName(baseConstructor)));
              callState.addAttribute("has_iterator_expression",
                                     builder.getBoolAttr(false));
              callState.addAttribute("has_inline_constraints",
                                     builder.getBoolAttr(false));
              callState.addAttribute("constraint_restrictions",
                                     builder.getArrayAttr({}));
              SmallVector<int64_t> defaulted(defaults.size(), 1);
              callState.addAttribute("defaulted_arguments",
                                     builder.getDenseI64ArrayAttr(defaulted));
              callState.addRegion();
              auto call = cast<semantic::SVCallExpressionOp>(
                  bodyBuilder.create(callState));
              call.getBody().emplaceBlock();
              OpBuilder argumentBuilder =
                  OpBuilder::atBlockEnd(&call.getBody().front());
              for (Operation *argument : defaults)
                argumentBuilder.clone(*argument);
            }
          } else if (FlatSymbolRefAttr baseConstructor =
                         constructorSymbolFor(base->second)) {
            Type baseReceiverType = sim::ClassHandleType::get(
                context,
                FlatSymbolRefAttr::get(
                    context, classSymbols.lookup(base->second).getValue()));
            Value receiver = unit.function.getBody().front().getArgument(1);
            Value baseReceiver = sim::SimClassCastOp::create(
                bodyBuilder, getSemanticLocation(subroutine), baseReceiverType,
                receiver);
            sim::SimClassDirectCallOp::create(
                bodyBuilder, getSemanticLocation(subroutine), TypeRange{},
                baseConstructor, baseReceiver, ValueRange{});
          } else {
            emitError(getSemanticLocation(subroutine))
                << "constructor cannot resolve its base constructor";
            invalid = true;
          }
          clonePropertyInitializers(bodyBuilder);
          initialized = true;
        }
      }
      for (Operation *child : getChildren(unit.source)) {
        if (isa<semantic::SVFormalArgumentSymbolOp,
                semantic::SVVariableSymbolOp,
                semantic::SVStatementBlockSymbolOp>(child))
          continue;
        Operation *clonedChild = bodyBuilder.clone(*child);
        if (constructor && owner && owner.getBaseClass() && !initialized) {
          Operation *superStatement = nullptr;
          clonedChild->walk([&](semantic::SVNewClassExpressionOp construct) {
            if (!construct.getIsSuperClass() || superStatement)
              return;
            Operation *anchor = construct;
            while (anchor != clonedChild &&
                   !isa<semantic::SVStatementListOp>(anchor->getParentOp()))
              anchor = anchor->getParentOp();
            superStatement =
                isa<semantic::SVStatementListOp>(anchor->getParentOp())
                    ? anchor
                    : clonedChild;
          });
          if (superStatement) {
            OpBuilder initializerBuilder(superStatement);
            initializerBuilder.setInsertionPointAfter(superStatement);
            clonePropertyInitializers(initializerBuilder);
            initialized = true;
          }
        }
      }
      if (constructor && !initialized)
        clonePropertyInitializers(bodyBuilder);
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
    auto propertyInitializer =
        dyn_cast<semantic::SVClassPropertySymbolOp>(unit.source);
    bool staticInitializer =
        isa<semantic::SVVariableSymbolOp>(unit.source) ||
        (propertyInitializer && propertyInitializer.getLifetime() ==
                                    semantic::SVVariableLifetime::Static);
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
        if (staticInitializer)
          if (auto constant = staticLiteralNets.find(symbol->second);
              constant != staticLiteralNets.end())
            named->setAttr(staticNetConstantAttrName, constant->second);
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
        if (symbol != semanticSymbols.end()) {
          if (staticInitializer)
            if (auto constant = staticLiteralNets.find(symbol->second);
                constant != staticLiteralNets.end())
              hierarchical->setAttr(staticNetConstantAttrName,
                                    constant->second);
          if (isa<semantic::SVParameterSymbolOp, semantic::SVEnumValueSymbolOp,
                  semantic::SVSpecparamSymbolOp>(symbol->second))
            if (auto constant =
                    symbol->second->getAttrOfType<StringAttr>("constant_value"))
              hierarchical->setAttr("obelisk_sim.constant_value", constant);
        }
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
      IntegerAttr interfaceOrdinal =
          classType.getIsInterface() && isVirtual
              ? builder.getI64IntegerAttr(
                    classes->interfaceMethodOrdinals.lookup(method))
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
          TypeAttr::get(functionType), slot, signatureID, interfaceOrdinal,
          implementationRef, builder.getBoolAttr(isVirtual),
          builder.getBoolAttr(isPure),
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
  auto startsByWaiting = [](const PreparedUnit &unit) {
    return unit.entryKind == sim::EntryKind::Always ||
           unit.entryKind == sim::EntryKind::AlwaysFF;
  };
  auto hasDeferredTimeZeroActivation = [](const PreparedUnit &unit) {
    return unit.entryKind == sim::EntryKind::AlwaysComb ||
           unit.entryKind == sim::EntryKind::AlwaysLatch;
  };

  // Establish explicit always-process sensitivities before initial processes
  // can trigger events or mutate their watched values. This deterministic
  // Active-region order prevents a source-order race from losing an event
  // before an `always @(event)` has suspended.
  for (PreparedUnit &unit : units)
    if (isRootSpawned(unit) && startsByWaiting(unit))
      if (failed(spawnRootUnit(unit)))
        return abort();

  // IEEE 1800-2017 9.2.2.2 requires the automatic time-zero activation of an
  // always_comb procedure to occur after all initial and always procedures
  // have started. Section 9.2.2.3 applies the same rule to always_latch.
  for (PreparedUnit &unit : units) {
    if (!isRootSpawned(unit) || startsByWaiting(unit) ||
        hasDeferredTimeZeroActivation(unit))
      continue;
    if (failed(spawnRootUnit(unit)))
      return abort();
  }
  for (PreparedUnit &unit : units)
    if (isRootSpawned(unit) && hasDeferredTimeZeroActivation(unit))
      if (failed(spawnRootUnit(unit)))
        return abort();
  sim::SimReturnOp::create(rootBuilder, module.getLoc(), ValueRange{});
}

} // namespace
} // namespace obelisk
