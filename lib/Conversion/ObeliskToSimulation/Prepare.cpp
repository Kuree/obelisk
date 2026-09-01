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
#include "LowerUnit.h"
#include "PrepareCaptures.h"
#include "PrepareDeclarations.h"
#include "PrepareNetTopology.h"
#include "PrepareRandomTemplates.h"
#include "PrepareTopology.h"
#include "PrepareUnits.h"
#include "PrepareValidation.h"

#include "obelisk/Analysis/ClassDispatchAnalysis.h"
#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHash.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
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
#include <numeric>

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

static uint32_t getStableDPIID(StringRef cIdentifier) {
  uint64_t hash = obelisk_stable_hash(cIdentifier.data(), cIdentifier.size());
  uint32_t result = static_cast<uint32_t>(hash ^ (hash >> 32));
  return result == 0 ? 1 : result;
}

struct PreparedDPISignature {
  ArrayAttr entries;
  ArrayAttr aggregateLayouts;
  uint32_t logicalInputs;
};

static FailureOr<PreparedDPISignature>
prepareDPISignature(semantic::SVSubroutineSymbolOp subroutine,
                    Builder &builder) {
  MLIRContext *context = builder.getContext();
  SmallVector<Attribute> inputs;
  SmallVector<Attribute> copyOuts;
  SmallVector<Attribute> aggregateInputs;
  SmallVector<Attribute> aggregateCopyOuts;
  bool invalid = false;
  auto makeABI = [&](Type type, sim::DPIArgumentDirection direction,
                     Location location) -> FailureOr<sim::DPIABIAttr> {
    FailureOr<DPIABIType> classified = classifyDPIABIType(type, location);
    if (failed(classified))
      return failure();
    return sim::DPIABIAttr::get(
        context, static_cast<sim::DPIABIKind>(classified->kind), direction,
        classified->width, classified->fourState, classified->isSigned);
  };
  for (Operation *child : getChildren(subroutine)) {
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
    Attribute aggregateLayout = builder.getUnitAttr();
    if (input->getKind() == sim::DPIABIKind::UnpackedAggregate) {
      FailureOr<Type> normalized = getNormalizedSemanticType(formal);
      if (failed(normalized)) {
        invalid = true;
        continue;
      }
      FailureOr<sim::DPIAggregateABIAttr> layout = makeDPIAggregateABI(
          *semanticType, *normalized, getSemanticLocation(formal), builder);
      if (failed(layout)) {
        invalid = true;
        continue;
      }
      std::optional<uint64_t> span = sim::getProvenanceSpan(*normalized);
      if (!span || *span == 0 || *span > std::numeric_limits<uint32_t>::max()) {
        formal.emitError("DPI aggregate has no bounded transport layout");
        invalid = true;
        continue;
      }
      input = sim::DPIABIAttr::get(context, input->getKind(), direction,
                                   static_cast<uint32_t>(*span),
                                   input->getFourState(), input->getIsSigned());
      aggregateLayout = *layout;
    }
    inputs.push_back(*input);
    aggregateInputs.push_back(aggregateLayout);
    if (direction != sim::DPIArgumentDirection::Input)
      copyOuts.push_back(sim::DPIABIAttr::get(
          context, input->getKind(), sim::DPIArgumentDirection::Output,
          input->getWidth(), input->getFourState(), input->getIsSigned()));
    if (direction != sim::DPIArgumentDirection::Input)
      aggregateCopyOuts.push_back(aggregateLayout);
  }
  SmallVector<Attribute> signature(inputs);
  SmallVector<Attribute> aggregateLayouts(aggregateInputs);
  if (subroutine.getSubroutineKind() == semantic::SVSubroutineKind::Function) {
    auto semanticType = subroutine->getAttrOfType<TypeAttr>("semantic_type");
    auto subroutineType =
        semanticType
            ? dyn_cast<semantic::SubroutineType>(semanticType.getValue())
            : semantic::SubroutineType{};
    auto sourceSignature =
        subroutineType ? dyn_cast<FunctionType>(subroutineType.getSignature())
                       : FunctionType{};
    if (!sourceSignature || sourceSignature.getNumResults() != 1) {
      emitError(getSemanticLocation(subroutine))
          << "DPI function has no resolved result signature";
      invalid = true;
    } else if (!isa<semantic::VoidType>(sourceSignature.getResult(0))) {
      FailureOr<sim::DPIABIAttr> result = makeABI(
          sourceSignature.getResult(0), sim::DPIArgumentDirection::Result,
          getSemanticLocation(subroutine));
      if (failed(result))
        invalid = true;
      else
        signature.push_back(*result);
      if (succeeded(result))
        aggregateLayouts.push_back(builder.getUnitAttr());
    }
  }
  if (invalid)
    return failure();
  llvm::append_range(signature, copyOuts);
  llvm::append_range(aggregateLayouts, aggregateCopyOuts);
  return PreparedDPISignature{builder.getArrayAttr(signature),
                              builder.getArrayAttr(aggregateLayouts),
                              static_cast<uint32_t>(inputs.size())};
}

static semantic::SVClassTypeOp getOwningClass(Operation *member) {
  for (Operation *parent = member ? member->getParentOp() : nullptr; parent;
       parent = parent->getParentOp())
    if (auto classType = dyn_cast<semantic::SVClassTypeOp>(parent))
      return classType;
  return {};
}

static bool containsResolutionOperation(Operation *root, Operation *nested) {
  for (Operation *current = nested; current; current = current->getParentOp())
    if (current == root)
      return true;
  return false;
}

static bool isResolutionStorageBase(Operation *lvalue, Operation *reference) {
  if (lvalue == reference)
    return isa<semantic::SVNamedValueExpressionOp,
               semantic::SVHierarchicalValueExpressionOp>(lvalue);
  SmallVector<Operation *> children = getChildren(lvalue);
  if (isa<semantic::SVMemberAccessExpressionOp,
          semantic::SVElementSelectExpressionOp,
          semantic::SVRangeSelectExpressionOp>(lvalue))
    return !children.empty() &&
           containsResolutionOperation(children.front(), reference) &&
           isResolutionStorageBase(children.front(), reference);
  if (isa<semantic::SVConcatenationExpressionOp>(lvalue))
    return llvm::any_of(children, [&](Operation *child) {
      return containsResolutionOperation(child, reference) &&
             isResolutionStorageBase(child, reference);
    });
  return false;
}

static bool isWrittenResolutionReference(Operation *reference) {
  for (Operation *ancestor = reference->getParentOp(); ancestor;
       ancestor = ancestor->getParentOp()) {
    if (auto assignment =
            dyn_cast<semantic::SVAssignmentExpressionOp>(ancestor)) {
      SmallVector<Operation *> children = getChildren(assignment);
      size_t destinationIndex = assignment.getHasTimingControl() ? 1u : 0u;
      return destinationIndex < children.size() &&
             containsResolutionOperation(children[destinationIndex],
                                         reference) &&
             isResolutionStorageBase(children[destinationIndex], reference);
    }
    if (auto unary = dyn_cast<semantic::SVUnaryExpressionOp>(ancestor)) {
      using Unary = semantic::SVUnaryOperator;
      Unary kind = unary.getOperatorKind();
      if (kind != Unary::Preincrement && kind != Unary::Predecrement &&
          kind != Unary::Postincrement && kind != Unary::Postdecrement)
        continue;
      SmallVector<Operation *> children = getChildren(unary);
      return children.size() == 1 &&
             containsResolutionOperation(children.front(), reference) &&
             isResolutionStorageBase(children.front(), reference);
    }
  }
  return false;
}

static bool isStatefulResolutionSystemCall(StringRef name) {
  return llvm::StringSwitch<bool>(name)
      // Random-number functions update the process RNG state.
      .Cases({"$random", "$urandom", "$urandom_range", "$srandom"}, true)
      .Cases({"$dist_uniform", "$dist_normal", "$dist_exponential"}, true)
      .Cases({"$dist_poisson", "$dist_chi_square", "$dist_t"}, true)
      .Case("$dist_erlang", true)
      // File operations either mutate a stream or produce external effects.
      .Cases({"$fopen", "$fclose", "$fflush", "$fgetc", "$fgets"}, true)
      .Cases({"$fread", "$fseek", "$rewind", "$ungetc", "$fscanf"}, true)
      .Cases({"$fwrite", "$fdisplay", "$fmonitor", "$fstrobe"}, true)
      .Case("$ferror", true)
      // These calls mutate simulator or design state independently of their
      // apparent expression result.
      .Cases({"$readmemb", "$readmemh", "$writememb", "$writememh"}, true)
      .Cases({"$q_initialize", "$q_add", "$q_remove", "$q_full", "$q_exam"},
             true)
      .Cases({"$timeformat", "$system", "$stop", "$finish"}, true)
      .Cases({"$dumpfile", "$dumpvars", "$dumpon", "$dumpoff"}, true)
      .Cases({"$dumpall", "$dumplimit", "$dumpflush"}, true)
      .Cases({"$display", "$write", "$monitor", "$strobe"}, true)
      .Cases({"$async$and$array", "$sync$and$array", "$async$and$plane",
              "$sync$and$plane"},
             true)
      .Cases({"$async$nand$array", "$sync$nand$array", "$async$nand$plane",
              "$sync$nand$plane"},
             true)
      .Cases({"$async$or$array", "$sync$or$array", "$async$or$plane",
              "$sync$or$plane"},
             true)
      .Cases({"$async$nor$array", "$sync$nor$array", "$async$nor$plane",
              "$sync$nor$plane"},
             true)
      .Cases({"$info", "$warning", "$error", "$fatal"}, true)
      .Default(false);
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
      semantic::SVConditionalExpressionOp, semantic::SVMinTypMaxExpressionOp,
      semantic::SVConcatenationExpressionOp,
      semantic::SVReplicationExpressionOp,
      semantic::SVElementSelectExpressionOp,
      semantic::SVRangeSelectExpressionOp, semantic::SVMemberAccessExpressionOp,
      semantic::SVInsideExpressionOp, semantic::SVValueRangeExpressionOp,
      semantic::SVDistExpressionOp>(op);
}

/// Whether a fixed aggregate holds an event anywhere in its element
/// inventory. IEEE 1800-2017 6.17 gives every such element its own
/// synchronization object, which the root initializer materializes.
static bool typeContainsEvent(Type type) {
  if (isa<sim::EventType>(type))
    return true;
  if (!sim::isAggregateType(type))
    return false;
  unsigned count = sim::getAggregateNumElements(type);
  for (unsigned index = 0; index != count; ++index)
    if (typeContainsEvent(sim::getAggregateElementType(type, index)))
      return true;
  return false;
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

  // Propagate stale frontend folds through expression parents in postorder.
  // Each expression enters this set at most once, avoiding a repeated upward
  // walk (and quadratic behavior) when one expression contains many affected
  // literals or compile-time queries.
  llvm::SmallPtrSet<Operation *, 16> staleFoldExpressions;
  auto isSemanticExpression = [](Operation *operation) {
    return operation && operation->getName().getStringRef().starts_with(
                            "obelisk.sv.expression.");
  };

  // Freeze call-specific frontend facts while the whole elaborated inventory
  // is still available. This walk already visits every operation for
  // `$dumpports`, so repairing an affected expression chain below adds no
  // additional whole-design traversal.
  semanticRoot->walk<WalkOrder::PostOrder>([&](Operation *operation) {
    bool staleFold = staleFoldExpressions.erase(operation);

    // IEEE 1800-2017 5.7.1: "Unsized unsigned literal constants where the
    // high-order bit is unknown (X or x) or three-state (Z or z) shall be
    // extended to the size of the expression containing the literal
    // constant." Slang widens such a literal with the zero padding of an
    // ordinary conversion, so its cached value -- and every fold computed
    // from it -- describes the wrong operand. Lowering fills the unknown bit
    // instead, so hand these expressions to it.
    if (std::optional<unsigned> filled =
            getUnsizedUnknownFillWidth(operation)) {
      Operation *widened = operation->getParentOp();
      auto widenedType =
          widened ? widened->getAttrOfType<TypeAttr>("semantic_type")
                  : TypeAttr{};
      std::optional<uint64_t> widenedWidth =
          widenedType ? getSemanticPackedWidth(widenedType.getValue())
                      : std::nullopt;
      if (widenedWidth && *widenedWidth > *filled &&
          isSemanticExpression(widened))
        staleFoldExpressions.insert(widened);
    }

    auto call = dyn_cast<semantic::SVCallExpressionOp>(operation);

    // IEEE 1800-2017 20.7 expands intermediate typedefs before numbering
    // dimensions. Slang's cached value currently uses the flattened storage
    // order, and enclosing expressions can therefore carry stale folds too.
    // Keep all unaffected frontend folds, but recompute this short expression
    // chain from the corrected compile-time query constants.
    if (call && call->hasAttr(arrayQueryDimensionsAttrName))
      staleFold = true;

    if (staleFold && isSemanticExpression(operation)) {
      operation->removeAttr(foldedConstantAttrName);
      Operation *parent = operation->getParentOp();
      if (isSemanticExpression(parent))
        staleFoldExpressions.insert(parent);
    }

    if (!call)
      return;

    // `$dumpports` selections are arbitrary-symbol expressions whose semantic
    // type is void, so their module-instance kind is otherwise lost when code
    // units are isolated from the semantic symbol table.
    if (!call.getIsSystemCall() || call.getCalleeName() != "$dumpports")
      return;
    for (Operation *child : getChildren(call)) {
      auto selection = dyn_cast<semantic::SVArbitrarySymbolExpressionOp>(child);
      if (!selection)
        continue;
      auto found = semanticSymbols.find(
          selection.getReferencedSymbol().getLeafReference());
      Operation *target =
          found == semanticSymbols.end() ? nullptr : found->second;
      semantic::SVDefinitionSymbolOp definition;
      if (auto instance =
              dyn_cast_or_null<semantic::SVInstanceSymbolOp>(target)) {
        if (auto reference = instance.getReferencedSymbolAttr()) {
          auto source = semanticSymbols.find(reference.getLeafReference());
          if (source != semanticSymbols.end())
            definition =
                dyn_cast<semantic::SVDefinitionSymbolOp>(source->second);
        }
      }
      if (definition &&
          definition.getDefinitionKind() == semantic::SVDefinitionKind::Module)
        selection->setAttr("obelisk_sim.dumpports_scope",
                           UnitAttr::get(context));
    }
  });

  // A sequence used as a procedural event is a static assertion instance
  // (IEEE 1800-2017 9.4.2.4). Mark the referenced declaration so preparation
  // can materialize one time-zero endpoint monitor and one shared event
  // descriptor. The bounded monitor currently has no formal/local-variable
  // execution ABI, so reject those forms instead of starting an inexact
  // process-local monitor when the waiter is reached.
  semanticRoot->walk([&](semantic::SVSignalEventControlOp event) {
    SmallVector<Operation *> children = getChildren(event);
    if (children.empty())
      return;
    auto instance =
        dyn_cast<semantic::SVAssertionInstanceExpressionOp>(children.front());
    auto type = instance ? instance->getAttrOfType<TypeAttr>("semantic_type")
                         : TypeAttr{};
    if (!type || !isa<semantic::SequenceType>(type.getValue()))
      return;
    if (event.getHasIff() || instance.getArgumentCount() != 0 ||
        instance.getLocalVariableCount() != 0) {
      emitError(getSemanticLocation(instance))
          << "sequence event controls with iff, formal arguments, or local "
             "variables are not executable by the bounded endpoint monitor";
      invalid = true;
      return;
    }
    auto symbol =
        semanticSymbols.find(instance.getReferencedSymbol().getLeafReference());
    auto sequence =
        symbol == semanticSymbols.end()
            ? semantic::SVSequenceSymbolOp{}
            : dyn_cast<semantic::SVSequenceSymbolOp>(symbol->second);
    if (!sequence) {
      emitError(getSemanticLocation(instance))
          << "sequence event control does not resolve to a named sequence";
      invalid = true;
      return;
    }
    bool firstEndpointUse = !sequence->hasAttr(sequenceEndpointEventAttrName);
    sequence->setAttr(sequenceEndpointEventAttrName, UnitAttr::get(context));
    if (!firstEndpointUse)
      return;

    // IEEE 1800-2017 16.13.6 applies sampled-function clock inference to a
    // sequence instantiated in an event expression. Materialize the available
    // default clock only for a sequence that actually needs an endpoint
    // monitor; ordinary sequence declarations retain their compact AST.
    SmallVector<Operation *> declarationChildren = getChildren(sequence);
    auto defaultInstance =
        declarationChildren.size() == 1
            ? dyn_cast<semantic::SVAssertionInstanceExpressionOp>(
                  declarationChildren.front())
            : semantic::SVAssertionInstanceExpressionOp{};
    SmallVector<Operation *> defaultInstanceChildren =
        defaultInstance ? getChildren(defaultInstance)
                        : SmallVector<Operation *>{};
    Operation *declarationBody =
        defaultInstance && defaultInstance.getHasExpandedBody() &&
                defaultInstance.getArgumentCount() == 0 &&
                defaultInstanceChildren.size() == 1
            ? defaultInstanceChildren.front()
            : nullptr;
    if (!declarationBody ||
        isa<semantic::SVClockingAssertionExprOp>(declarationBody))
      return;
    auto defaultClock =
        sequence->getAttrOfType<SymbolRefAttr>("default_clocking_symbol");
    auto clockingSymbol =
        defaultClock ? semanticSymbols.find(defaultClock.getLeafReference())
                     : semanticSymbols.end();
    auto clocking = clockingSymbol == semanticSymbols.end()
                        ? semantic::SVClockingBlockSymbolOp{}
                        : dyn_cast<semantic::SVClockingBlockSymbolOp>(
                              clockingSymbol->second);
    if (!clocking)
      return;
    SmallVector<Operation *> clockingChildren = getChildren(clocking);
    auto clockEvent = llvm::find_if(clockingChildren, [](Operation *child) {
      return isa<semantic::SVSignalEventControlOp,
                 semantic::SVEventListControlOp>(child);
    });
    if (clockEvent == clockingChildren.end())
      return;
    OpBuilder endpointBuilder(context);
    endpointBuilder.setInsertionPointToEnd(&sequence->getRegion(0).front());
    Operation *resolvedClock = endpointBuilder.clone(**clockEvent);
    resolvedClock->setAttr(sequenceEndpointDefaultClockAttrName,
                           UnitAttr::get(context));
  });

  // Freeze the normalized storage type of every assertion local on its
  // expanded instance. Local declarations remain in the semantic symbol
  // inventory rather than the executable code unit, while the monitor needs
  // their exact types to materialize per-attempt state after isolation.
  semanticRoot->walk([&](semantic::SVAssertionInstanceExpressionOp instance) {
    SmallVector<Attribute> types;
    for (Attribute attribute : instance.getLocalVariableSymbols()) {
      auto reference = dyn_cast<SymbolRefAttr>(attribute);
      if (!reference) {
        emitError(getSemanticLocation(instance))
            << "assertion local variable has an invalid symbol reference";
        invalid = true;
        return;
      }
      auto symbol = semanticSymbols.find(reference.getLeafReference());
      auto local =
          symbol == semanticSymbols.end()
              ? semantic::SVLocalAssertionVarSymbolOp{}
              : dyn_cast<semantic::SVLocalAssertionVarSymbolOp>(symbol->second);
      FailureOr<Type> type =
          local ? getNormalizedSemanticType(local) : FailureOr<Type>(failure());
      if (!local || failed(type)) {
        emitError(getSemanticLocation(instance))
            << "assertion local variable does not resolve to an executable "
               "type";
        invalid = true;
        return;
      }
      types.push_back(TypeAttr::get(*type));
    }
    instance->setAttr(assertionLocalTypesAttrName,
                      ArrayAttr::get(context, types));
  });

  // IEEE 1800-2017 23.2.2.4 allows a default value only on an input port, so an
  // initializer on an output port is not a port default: 23.2.2.3 makes an
  // explicitly typed output port a variable, and the initializer is that
  // variable's declaration assignment. IEEE 1800-2017 10.5 requires it to be
  // applied before any initial or always procedure starts.
  //
  // The frontend mirrors slang, which parents the expression under the port
  // symbol where nothing consumes it. Give it to the port's variable so the
  // ordinary static-initializer path below picks it up.
  semanticRoot->walk([&](semantic::SVPortSymbolOp port) {
    if (port.getDirection() != semantic::SVArgumentDirection::Out)
      return;
    SmallVector<Operation *> initializer = getChildren(port);
    if (initializer.size() != 1)
      return;
    Operation *body = port->getParentOp();
    if (!body)
      return;
    StringRef portPath = getHierarchyName(port);
    for (Operation *sibling : getChildren(body)) {
      auto variable = dyn_cast<semantic::SVVariableSymbolOp>(sibling);
      if (!variable || getHierarchyName(variable) != portPath ||
          !getChildren(variable).empty())
        continue;
      Region &target = variable.getBody();
      if (target.empty())
        target.emplaceBlock();
      initializer.front()->moveBefore(&target.front(), target.front().end());
      break;
    }
  });

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
    bool initializedStaticLocal =
        variable &&
        variable.getLifetime() == semantic::SVVariableLifetime::Static &&
        isNestedInCodeUnit(variable) && !getChildren(variable).empty();
    bool designInitializer = variable && !isNestedInCodeUnit(variable) &&
                             !isAutomaticLocalSymbol(variable) &&
                             !getChildren(variable).empty();
    auto net = dyn_cast<semantic::SVNetSymbolOp>(op);
    bool netInitializer = net && !getNetInitializerExpressions(net).empty();
    if (isCodeUnit(op) ||
        (isa<semantic::SVSystemTimingCheckSymbolOp>(op) &&
         op->hasAttr("obelisk.basic_timing_check")) ||
        staticInitializer || initializedStaticLocal || designInitializer ||
        netInitializer || op->hasAttr(sequenceEndpointEventAttrName) ||
        (isa<semantic::SVClockingBlockSymbolOp>(op) &&
         (op->hasAttr(clockingEventMonitorRequiredAttrName) ||
          op->hasAttr(clockingEventListAttrName))))
      sourceUnits.push_back(op);
  });

  // Enum identity is intentionally erased when semantic values are normalized
  // to executable packed types. Freeze the frontend's exact enumerator
  // inventory on enum operations before code units are cloned so lowering can
  // preserve the LRM membership and name lookups without consulting semantic
  // symbols. The frontend inventory is required because anonymous and local
  // typedef enums do not necessarily have a standalone semantic declaration.
  llvm::DenseMap<Type, semantic::SVEnumTypeOp> enumDeclarations;
  struct EnumMethodInventory {
    ArrayAttr values;
    ArrayAttr names;
  };
  llvm::DenseMap<Type, EnumMethodInventory> enumMethodInventories;
  llvm::DenseSet<Type> ambiguousEnumMethodInventories;
  semanticRoot.walk([&](Operation *operation) {
    if (auto enumeration = dyn_cast<semantic::SVEnumTypeOp>(operation)) {
      enumDeclarations.try_emplace(enumeration.getSemanticType(), enumeration);
      return;
    }
    auto call = dyn_cast<semantic::SVCallExpressionOp>(operation);
    if (!call)
      return;
    ArrayAttr values = call.getEnumMethodValuesAttr();
    ArrayAttr names = call.getEnumMethodNamesAttr();
    SmallVector<Operation *> arguments = getChildren(call);
    if (!values || values.empty() || !names || names.size() != values.size() ||
        arguments.empty())
      return;
    auto receiverType =
        arguments.front()->getAttrOfType<TypeAttr>("semantic_type");
    if (!receiverType || !isa<semantic::EnumType>(receiverType.getValue()))
      return;
    Type type = receiverType.getValue();
    auto [iterator, inserted] = enumMethodInventories.try_emplace(
        type, EnumMethodInventory{values, names});
    if (!inserted && (iterator->second.values != values ||
                      iterator->second.names != names)) {
      enumMethodInventories.erase(iterator);
      ambiguousEnumMethodInventories.insert(type);
    }
  });
  auto freezeEnumValues = [&](Operation *owner, Operation *typedValue,
                              ArrayAttr spellings,
                              StringRef description) -> FailureOr<ArrayAttr> {
    FailureOr<Type> normalized = getNormalizedSemanticType(typedValue);
    if (failed(normalized))
      return failure();
    Type scalar = sim::getPackedScalarType(*normalized);
    std::optional<unsigned> width =
        scalar ? sim::getPackedWidth(scalar) : std::nullopt;
    auto semanticType = typedValue->getAttrOfType<TypeAttr>("semantic_type");
    if (!semanticType || !isa<semantic::EnumType>(semanticType.getValue()) ||
        !spellings || spellings.empty()) {
      emitError(getSemanticLocation(owner))
          << description << " has no valid enumerator inventory";
      return failure();
    }
    if (!width) {
      emitError(getSemanticLocation(owner))
          << description << " has a non-integral enum type";
      return failure();
    }
    unsigned enumWidth = width.value();
    SmallVector<Attribute> values;
    values.reserve(spellings.size());
    Type planeType = IntegerType::get(context, enumWidth);
    for (Attribute attribute : spellings) {
      auto spelling = dyn_cast<StringAttr>(attribute);
      FailureOr<ParsedConstant> parsed =
          spelling ? parseSVInteger(spelling.getValue(), enumWidth,
                                    getSemanticLocation(owner))
                   : FailureOr<ParsedConstant>(failure());
      if (failed(parsed)) {
        emitError(getSemanticLocation(owner))
            << description << " has a malformed enumerator value";
        return failure();
      }
      ArrayAttr planes = ArrayAttr::get(
          context, {IntegerAttr::get(planeType, parsed->value),
                    IntegerAttr::get(planeType, parsed->unknown)});
      values.push_back(sim::FrozenConstantAttr::get(
          context, *normalized, planes,
          isSignedSemanticType(semanticType.getValue())));
    }
    return ArrayAttr::get(context, values);
  };

  semanticRoot->walk([&](semantic::SVCallExpressionOp call) {
    bool outputCall =
        call.getIsSystemCall() &&
        llvm::StringSwitch<bool>(call.getCalleeName())
            .Cases({"$strobe",   "$strobeb",   "$strobeo",   "$strobeh",
                    "$fstrobe",  "$fstrobeb",  "$fstrobeo",  "$fstrobeh",
                    "$monitor",  "$monitorb",  "$monitoro",  "$monitorh",
                    "$fmonitor", "$fmonitorb", "$fmonitoro", "$fmonitorh",
                    "$display",  "$displayb",  "$displayo",  "$displayh",
                    "$write",    "$writeb",    "$writeo",    "$writeh",
                    "$fdisplay", "$fdisplayb", "$fdisplayo", "$fdisplayh",
                    "$fwrite",   "$fwriteb",   "$fwriteo",   "$fwriteh",
                    "$info",     "$warning",   "$error",     "$fatal",
                    "$swrite",   "$swriteb",   "$swriteo",   "$swriteh",
                    "$sformat",  "$sformatf",  "$psprintf"},
                   true)
            .Default(false);
    if (outputCall) {
      // IEEE 1800-2017 21.2.1.7: %p renders a valid enumeration value by
      // its declared mnemonic. Preserve both the packed value and a compact
      // name selection; ordinary numeric conversions continue to use the
      // packed half of the resulting runtime argument.
      for (Operation *argument : getChildren(call)) {
        auto type = argument->getAttrOfType<TypeAttr>("semantic_type");
        if (!type || !isa<semantic::EnumType>(type.getValue()))
          continue;
        SmallVector<Attribute> valueSpellings;
        SmallVector<Attribute> names;
        auto declaration = enumDeclarations.find(type.getValue());
        if (declaration != enumDeclarations.end()) {
          for (Operation *member : getChildren(declaration->second)) {
            auto enumerator = dyn_cast<semantic::SVEnumValueSymbolOp>(member);
            if (!enumerator)
              continue;
            auto value =
                enumerator->getAttrOfType<StringAttr>("constant_value");
            auto name = enumerator->getAttrOfType<StringAttr>("name");
            if (!value || !name) {
              emitError(getSemanticLocation(enumerator))
                  << "formatted enum has malformed declaration inventory";
              invalid = true;
              continue;
            }
            valueSpellings.push_back(value);
            names.push_back(name);
          }
        } else if (auto inventory = enumMethodInventories.find(type.getValue());
                   inventory != enumMethodInventories.end() &&
                   !ambiguousEnumMethodInventories.contains(type.getValue())) {
          valueSpellings.append(inventory->second.values.begin(),
                                inventory->second.values.end());
          names.append(inventory->second.names.begin(),
                       inventory->second.names.end());
        } else {
          // Numeric and default output formats need only the packed enum value
          // (IEEE 1800-2017 21.2.1.2). Leave an enum without a declaration
          // inventory unannotated; lowering diagnoses it only if the format
          // actually selects §21.2.1.7 assignment-pattern rendering.
          continue;
        }
        ArrayAttr spellings = ArrayAttr::get(context, valueSpellings);
        FailureOr<ArrayAttr> values =
            freezeEnumValues(call, argument, spellings, "formatted enum");
        if (failed(values) || names.size() != valueSpellings.size()) {
          invalid = true;
          continue;
        }
        argument->setAttr(enumFormatValuesAttrName, *values);
        argument->setAttr(enumFormatNamesAttrName,
                          ArrayAttr::get(context, names));
      }
    }
    ArrayAttr spellings = call.getEnumMethodValuesAttr();
    bool enumMethod =
        call.getIsSystemCall() && spellings &&
        llvm::StringSwitch<bool>(call.getCalleeName())
            .Cases({"first", "last", "next", "prev", "num", "name"}, true)
            .Default(false);
    if (enumMethod) {
      SmallVector<Operation *> arguments = getChildren(call);
      bool arityValid =
          (call.getCalleeName() == "next" || call.getCalleeName() == "prev")
              ? arguments.size() == 1 || arguments.size() == 2
              : arguments.size() == 1;
      if (!arityValid || !spellings || spellings.empty()) {
        emitError(getSemanticLocation(call))
            << "enum " << call.getCalleeName()
            << "() has no valid enumerator inventory";
        invalid = true;
        return;
      }
      FailureOr<ArrayAttr> values =
          freezeEnumValues(call, arguments.front(), spellings,
                           ("enum " + call.getCalleeName() + "()").str());
      if (failed(values)) {
        invalid = true;
        return;
      }
      call->setAttr(enumMethodValuesAttrName, *values);
      if (call.getCalleeName() == "name") {
        ArrayAttr names = call.getEnumMethodNamesAttr();
        if (!names || names.empty() || names.size() != spellings.size()) {
          emitError(getSemanticLocation(call))
              << "enum name() has no valid enumerator names";
          invalid = true;
          return;
        }
        call->setAttr(enumMethodNamesAttrName, names);
      }
      return;
    }
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
    // IEEE 1800-2017 6.24.2: the task form of $cast reports a failed cast as
    // a run-time error, while the function form reports it through its 0
    // result. A `void'($cast(...))` statement is still the function form --
    // 13.4.1 casts a call to void to use it as a statement -- so it must not
    // take the task form's error path.
    if (isa<semantic::SVExpressionStatementOp>(call->getParentOp()) &&
        !call.getIsVoidCasted())
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
    FailureOr<ArrayAttr> values = freezeEnumValues(
        call, destination, call.getDynamicCastEnumValuesAttr(), "enum $cast");
    if (failed(values)) {
      invalid = true;
      return;
    }
    call->setAttr(dynamicCastEnumValuesAttrName, *values);
  });

  // Assign compact, collision-free IDs from sorted elaborated paths. These
  // IDs cross both native and bytecode ABIs, so unchecked truncated hashes
  // are not acceptable.
  llvm::StringSet<> controlPaths;
  llvm::StringSet<> resumableControlPaths;
  llvm::StringSet<> staticPaths;
  semanticRoot->walk([&](Operation *op) {
    if (auto block = dyn_cast<semantic::SVBlockStatementOp>(op)) {
      if (auto path = block.getBlockPathAttr())
        controlPaths.insert(path.getValue());
    } else if (auto disable = dyn_cast<semantic::SVDisableStatementOp>(op)) {
      if (auto path = disable.getTargetPathAttr()) {
        controlPaths.insert(path.getValue());
        resumableControlPaths.insert(path.getValue());
      }
    } else if (auto declaration =
                   dyn_cast<semantic::SVVariableDeclStatementOp>(op)) {
      staticPaths.insert(declaration.getReferencedPath());
    }
  });
  semanticRoot->walk([&](semantic::SVBlockStatementOp block) {
    if (auto path = block.getBlockPathAttr();
        path && resumableControlPaths.contains(path.getValue()))
      block->setAttr("obelisk_sim.resumable_control_target",
                     UnitAttr::get(context));
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

  auto addQualifierReport = [&](Operation *statement,
                                semantic::SVUniquePriorityCheck qualifier) {
    if (qualifier == semantic::SVUniquePriorityCheck::None)
      return;
    semantic::SVInstanceBodySymbolOp body = enclosingInstance(statement);
    auto scopeAttr = body ? body->getAttrOfType<StringAttr>("hierarchical_name")
                          : StringAttr{};
    std::string scope = scopeAttr ? scopeAttr.getValue().str() : std::string{};
    uint32_t type = 0;
    switch (qualifier) {
    case semantic::SVUniquePriorityCheck::Unique:
      type = 32;
      break;
    case semantic::SVUniquePriorityCheck::Unique0:
      type = 64;
      break;
    case semantic::SVUniquePriorityCheck::Priority:
      type = 128;
      break;
    case semantic::SVUniquePriorityCheck::None:
      llvm_unreachable("handled above");
    }
    // IEEE 1800-2017 20.12 says directive_type is checked only for
    // assertions. Zero marks a violation-report target for which that mask is
    // intentionally ignored.
    assertionInventory.push_back({statement, assertionPath(statement, scope),
                                  scope, 0, instanceScopeDepths.lookup(scope),
                                  type, 0, true});
  };
  // Keep assertion inventory construction to one linear AST walk. This is on
  // the UVM path even when no assertion control call ultimately selects a
  // qualifier report.
  semanticRoot->walk([&](Operation *operation) {
    if (auto assertion =
            dyn_cast<semantic::SVImmediateAssertionStatementOp>(operation)) {
      semantic::SVInstanceBodySymbolOp body = enclosingInstance(assertion);
      auto scopeAttr =
          body ? body->getAttrOfType<StringAttr>("hierarchical_name")
               : StringAttr{};
      std::string scope =
          scopeAttr ? scopeAttr.getValue().str() : std::string{};
      uint32_t type =
          assertion.getIsDeferred() ? (assertion.getIsFinal() ? 8u : 4u) : 2u;
      assertionInventory.push_back(
          {assertion, assertionPath(assertion, scope), scope, 0,
           instanceScopeDepths.lookup(scope), type,
           directiveMask(assertion.getAssertionKind()), true});
      return;
    }
    if (auto assertion =
            dyn_cast<semantic::SVConcurrentAssertionStatementOp>(operation)) {
      semantic::SVInstanceBodySymbolOp body = enclosingInstance(assertion);
      auto scopeAttr =
          body ? body->getAttrOfType<StringAttr>("hierarchical_name")
               : StringAttr{};
      std::string scope =
          scopeAttr ? scopeAttr.getValue().str() : std::string{};
      uint32_t type =
          assertion.getAssertionKind() == semantic::SVAssertionKind::Expect
              ? 16u
              : 1u;
      assertionInventory.push_back(
          {assertion, assertionPath(assertion, scope), scope, 0,
           instanceScopeDepths.lookup(scope), type,
           directiveMask(assertion.getAssertionKind()), type == 1});
      return;
    }
    if (auto statement =
            dyn_cast<semantic::SVConditionalStatementOp>(operation))
      addQualifierReport(statement, statement.getCheckKind());
    else if (auto statement = dyn_cast<semantic::SVCaseStatementOp>(operation))
      addQualifierReport(statement, statement.getCheckKind());
    else if (auto statement =
                 dyn_cast<semantic::SVPatternCaseStatementOp>(operation))
      addQualifierReport(statement, statement.getCheckKind());
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
    bool dynamicAction = false;
    size_t actionArgument = 0;
    uint64_t assertionTypes = attemptShorthand ? 15 : 255;
    uint64_t directiveTypes = 7;
    bool dynamicAssertionTypes = false;
    bool dynamicDirectiveTypes = false;
    size_t assertionTypesArgument = 0;
    size_t directiveTypesArgument = 0;
    uint64_t levels = 0;
    bool dynamicLevels = false;
    size_t levelsArgument = 0;
    size_t firstSelector = 0;
    bool selectCurrentScope = false;
    if (shorthand) {
      if (!arguments.empty()) {
        if (arguments.front()->hasAttr("constant_value")) {
          std::optional<uint64_t> value =
              literalControlValue(arguments.front(), "levels");
          if (!value)
            return;
          levels = *value;
        } else {
          dynamicLevels = true;
          levelsArgument = 0;
        }
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
      std::optional<uint64_t> value;
      if (arguments[0]->hasAttr("constant_value")) {
        value = literalControlValue(arguments[0], "control type");
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
      } else {
        dynamicAction = true;
        actionArgument = 0;
      }
      if (arguments.size() >= 2 &&
          !isa<semantic::SVEmptyArgumentExpressionOp>(arguments[1])) {
        if (arguments[1]->hasAttr("constant_value")) {
          value = literalControlValue(arguments[1], "assertion-type mask");
          if (!value)
            return;
          assertionTypes = *value;
        } else {
          dynamicAssertionTypes = true;
          assertionTypesArgument = 1;
          assertionTypes = 255;
        }
      } else {
        assertionTypes = 255;
      }
      if (arguments.size() >= 3 &&
          !isa<semantic::SVEmptyArgumentExpressionOp>(arguments[2])) {
        if (arguments[2]->hasAttr("constant_value")) {
          value = literalControlValue(arguments[2], "directive-type mask");
          if (!value)
            return;
          directiveTypes = *value;
        } else {
          dynamicDirectiveTypes = true;
          directiveTypesArgument = 2;
          directiveTypes = 7;
        }
      }
      if (arguments.size() >= 4) {
        bool explicitLevels =
            !isa<semantic::SVEmptyArgumentExpressionOp>(arguments[3]);
        if (explicitLevels) {
          if (arguments[3]->hasAttr("constant_value")) {
            value = literalControlValue(arguments[3], "levels");
            if (!value)
              return;
            levels = *value;
          } else {
            dynamicLevels = true;
            levelsArgument = 3;
          }
        }
        firstSelector = 4;
        selectCurrentScope = explicitLevels && arguments.size() == 4;
      } else {
        firstSelector = arguments.size();
      }
    }
    if ((!dynamicAssertionTypes && (assertionTypes & ~UINT64_C(255)) != 0) ||
        (!dynamicDirectiveTypes && (directiveTypes & ~UINT64_C(7)) != 0)) {
      emitError(getSemanticLocation(call))
          << "assertion-control mask contains a value outside the assertion "
             "or directive types defined by IEEE 1800-2017 20.12";
      invalid = true;
      return;
    }
    // On, Off, and Kill do not affect expect statements. The remaining
    // controls do, so selecting an expect statement is rejected below until
    // executable expect support lands. Conversely, PassOn through VacuousOff
    // do not affect violation report types. Prune both statically known no-op
    // selections before building control targets or runtime queries.
    if (!dynamicAction && action >= 3 && action <= 5)
      assertionTypes &= ~UINT64_C(16);
    if (!dynamicAction && action >= 6 && action <= 11)
      assertionTypes &= ~UINT64_C(224);

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

    SmallVector<std::pair<int64_t, int64_t>> selectedTargets;
    SmallVector<std::pair<int64_t, int64_t>> selectedTypeMasks;
    SmallVector<std::pair<Operation *, uint64_t>> selectedAssertions;
    for (const AssertionInventoryEntry &entry : assertionInventory) {
      if ((entry.assertionType & assertionTypes) == 0 ||
          (entry.directiveType != 0 &&
           (entry.directiveType & directiveTypes) == 0))
        continue;
      bool selected = selectors.empty();
      // -1 denotes a target selected independently of the levels value. For
      // a hierarchy selector, retain the shallowest relative instance depth;
      // a run-time zero selects every descendant and a positive value selects
      // depths strictly below it, matching the fixed-level path below.
      int64_t selectedDepth = selectors.empty() ? -1 : INT64_MAX;
      for (StringRef selector : selectors) {
        if (entry.path == selector) {
          selected = true;
          selectedDepth = -1;
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
        if (dynamicLevels) {
          selected = true;
          selectedDepth = std::min<int64_t>(
              selectedDepth, static_cast<int64_t>(relativeDepth));
        } else if (levels == 0 || relativeDepth < levels) {
          selected = true;
          selectedDepth = -1;
          break;
        }
      }
      if (!selected)
        continue;
      if (!entry.supported) {
        emitError(getSemanticLocation(call))
            << "assertion control selected expect statement '" << entry.path
            << "', which is not executable by assertion control yet";
        invalid = true;
        return;
      }
      selectedTargets.push_back(
          {static_cast<int64_t>(entry.id), selectedDepth});
      if (dynamicAssertionTypes || dynamicDirectiveTypes)
        selectedTypeMasks.push_back(
            {static_cast<int64_t>(entry.assertionType),
             static_cast<int64_t>(entry.directiveType)});
      selectedAssertions.push_back({entry.operation, entry.id});
    }
    SmallVector<int64_t> selectedIDs;
    SmallVector<int64_t> selectedDepths;
    SmallVector<int64_t> selectedAssertionTypes;
    SmallVector<int64_t> selectedDirectiveTypes;
    auto appendTarget = [&](int64_t id, int64_t depth, int64_t assertionType,
                            int64_t directiveType) {
      if (!selectedIDs.empty() && selectedIDs.back() == id) {
        selectedDepths.back() = std::min(selectedDepths.back(), depth);
        if (dynamicAssertionTypes || dynamicDirectiveTypes) {
          selectedAssertionTypes.back() |= assertionType;
          selectedDirectiveTypes.back() |= directiveType;
        }
        return;
      }
      selectedIDs.push_back(id);
      selectedDepths.push_back(depth);
      if (dynamicAssertionTypes || dynamicDirectiveTypes) {
        selectedAssertionTypes.push_back(assertionType);
        selectedDirectiveTypes.push_back(directiveType);
      }
    };
    if (dynamicAssertionTypes || dynamicDirectiveTypes) {
      SmallVector<size_t> selectedOrder(selectedTargets.size());
      std::iota(selectedOrder.begin(), selectedOrder.end(), 0);
      llvm::sort(selectedOrder, [&](size_t left, size_t right) {
        return selectedTargets[left] < selectedTargets[right];
      });
      for (size_t index : selectedOrder) {
        auto [id, depth] = selectedTargets[index];
        auto [assertionType, directiveType] = selectedTypeMasks[index];
        appendTarget(id, depth, assertionType, directiveType);
      }
    } else {
      llvm::sort(selectedTargets);
      for (auto [id, depth] : selectedTargets)
        appendTarget(id, depth, 0, 0);
    }
    if (dynamicAction)
      call->setAttr(
          "obelisk_sim.assertion_control_action_argument",
          IntegerAttr::get(IntegerType::get(context, 64), actionArgument));
    else
      call->setAttr("obelisk_sim.assertion_control_action",
                    IntegerAttr::get(IntegerType::get(context, 32), action));
    call->setAttr("obelisk_sim.assertion_control_ids",
                  DenseI64ArrayAttr::get(context, selectedIDs));
    if (dynamicLevels) {
      call->setAttr(
          "obelisk_sim.assertion_control_levels_argument",
          IntegerAttr::get(IntegerType::get(context, 64), levelsArgument));
      call->setAttr("obelisk_sim.assertion_control_depths",
                    DenseI64ArrayAttr::get(context, selectedDepths));
    }
    if (dynamicAssertionTypes) {
      call->setAttr("obelisk_sim.assertion_control_assertion_types_argument",
                    IntegerAttr::get(IntegerType::get(context, 64),
                                     assertionTypesArgument));
      call->setAttr("obelisk_sim.assertion_control_assertion_types",
                    DenseI64ArrayAttr::get(context, selectedAssertionTypes));
    }
    if (dynamicDirectiveTypes) {
      call->setAttr("obelisk_sim.assertion_control_directive_types_argument",
                    IntegerAttr::get(IntegerType::get(context, 64),
                                     directiveTypesArgument));
      call->setAttr("obelisk_sim.assertion_control_directive_types",
                    DenseI64ArrayAttr::get(context, selectedDirectiveTypes));
    }
    for (auto [target, id] : selectedAssertions) {
      target->setAttr("obelisk_sim.assertion_control_target_id",
                      IntegerAttr::get(IntegerType::get(context, 64), id));
      if (dynamicAction || (action >= 3 && action <= 5))
        target->setAttr("obelisk_sim.assertion_controlled",
                        UnitAttr::get(context));
      if (dynamicAction || action == 5)
        target->setAttr("obelisk_sim.assertion_kill_controlled",
                        UnitAttr::get(context));
      if (dynamicAction || (action >= 6 && action <= 11))
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
      if (assignment.getUnsupportedDelay()) {
        emitError(getSemanticLocation(unit))
            << "continuous-assignment delays are not supported: "
            << *assignment.getUnsupportedDelay();
        invalid = true;
      }
    }
    if (auto primitive =
            dyn_cast<semantic::SVPrimitiveInstanceSymbolOp>(unit)) {
      if (primitive.getUnsupportedDelay()) {
        emitError(getSemanticLocation(unit))
            << "primitive delays are not supported: "
            << *primitive.getUnsupportedDelay();
        invalid = true;
      }
      if (auto udp = primitive->getAttrOfType<DictionaryAttr>(
              udpSemanticMetadataAttrName)) {
        auto sequential = udp.getAs<BoolAttr>("is_sequential");
        auto edgeSensitive = udp.getAs<BoolAttr>("is_edge_sensitive");
        if (!sequential || !edgeSensitive) {
          emitError(getSemanticLocation(unit))
              << "user-defined primitive is missing validated declaration "
                 "metadata";
          invalid = true;
        }
        if (auto delays = primitive.getDelayFs();
            delays && delays->size() > 2) {
          emitError(getSemanticLocation(unit))
              << "user-defined primitive delay must contain one or two values";
          invalid = true;
        }
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

  // IEEE 1800-2017 6.6.8: an untyped interconnect acquires its executable
  // data/net type exclusively from the net ports it connects, and distinct
  // elements of an unpacked interconnect array may acquire distinct types.
  // Solve those structural type constraints before descriptor allocation.
  // Scalar interconnects become ordinary typed nets; fixed heterogeneous
  // aggregates become one typed descriptor per leaf and never exist as a
  // typeless runtime object.
  {
    struct InterconnectLeaf {
      semantic::SVNetSymbolOp owner;
      SmallVector<int64_t> indices;
      std::string path;
      Type candidate;
      std::optional<Location> candidateLocation;
    };
    SmallVector<InterconnectLeaf> leaves;
    llvm::StringMap<SmallVector<unsigned>> leavesByRoot;
    llvm::StringMap<semantic::SVNetSymbolOp> interconnects;

    auto containsUntyped = [&](Type type) {
      while (auto array = dyn_cast<semantic::RangedUnpackedArrayType>(type))
        type = array.getElementType();
      return isa<semantic::UntypedType>(type);
    };
    auto leafPath = [](StringRef root, ArrayRef<int64_t> indices) {
      std::string result = root.str();
      for (int64_t index : indices)
        result += (Twine("[") + Twine(index) + "]").str();
      return result;
    };
    std::function<LogicalResult(semantic::SVNetSymbolOp, Type,
                                SmallVector<int64_t> &)>
        enumerateLeaves;
    enumerateLeaves = [&](semantic::SVNetSymbolOp net, Type type,
                          SmallVector<int64_t> &indices) -> LogicalResult {
      if (auto array = dyn_cast<semantic::RangedUnpackedArrayType>(type)) {
        int64_t index = array.getLeft();
        while (true) {
          indices.push_back(index);
          if (failed(enumerateLeaves(net, array.getElementType(), indices)))
            return failure();
          indices.pop_back();
          if (index == array.getRight())
            break;
          if (array.getLeft() < array.getRight()) {
            if (index == std::numeric_limits<int64_t>::max())
              return failure();
            ++index;
          } else {
            if (index == std::numeric_limits<int64_t>::min())
              return failure();
            --index;
          }
        }
        return success();
      }
      if (!isa<semantic::UntypedType>(type))
        return failure();
      StringRef root = getHierarchyName(net);
      unsigned id = leaves.size();
      InterconnectLeaf leaf;
      leaf.owner = net;
      leaf.indices = indices;
      leaf.path = leafPath(root, indices);
      leaves.push_back(std::move(leaf));
      leavesByRoot[root].push_back(id);
      return success();
    };

    semanticRoot->walk([&](semantic::SVNetSymbolOp net) {
      std::optional<Type> semanticType = net.getSemanticType();
      if (net.getNetKind() == semantic::SVNetKind::Interconnect &&
          semanticType && containsUntyped(*semanticType)) {
        StringRef path = getHierarchyName(net);
        interconnects[path] = net;
        SmallVector<int64_t> indices;
        if (failed(enumerateLeaves(net, *semanticType, indices))) {
          emitError(getSemanticLocation(net))
              << "interconnect has a non-fixed or malformed typeless shape";
          invalid = true;
        }
      }
    });

    SmallVector<unsigned> parents;
    parents.reserve(leaves.size());
    for (unsigned id = 0; id != leaves.size(); ++id)
      parents.push_back(id);
    std::function<unsigned(unsigned)> find = [&](unsigned id) -> unsigned {
      if (parents[id] == id)
        return id;
      return parents[id] = find(parents[id]);
    };
    auto join = [&](unsigned lhs, unsigned rhs) {
      lhs = find(lhs);
      rhs = find(rhs);
      if (lhs != rhs)
        parents[std::max(lhs, rhs)] = std::min(lhs, rhs);
    };
    auto parseKnownIndex =
        [&](Operation *expression) -> std::optional<int64_t> {
      std::optional<StringRef> spelling = getConstantSpelling(expression);
      if (!spelling)
        return std::nullopt;
      FailureOr<ParsedConstant> parsed =
          parseSVInteger(*spelling, 64, getSemanticLocation(expression));
      if (failed(parsed) || !parsed->unknown.isZero() ||
          !parsed->value.isSignedIntN(64))
        return std::nullopt;
      return parsed->value.getSExtValue();
    };
    struct InterconnectReference {
      std::string root;
      SmallVector<int64_t> indices;
    };
    std::function<std::optional<InterconnectReference>(Operation *)>
        getInterconnectReference;
    getInterconnectReference =
        [&](Operation *expression) -> std::optional<InterconnectReference> {
      if (!expression)
        return std::nullopt;
      StringRef path;
      if (auto named = dyn_cast<semantic::SVNamedValueExpressionOp>(expression))
        path = named.getReferencedPath();
      else if (auto hierarchical =
                   dyn_cast<semantic::SVHierarchicalValueExpressionOp>(
                       expression))
        path = hierarchical.getReferencedPath();
      if (!path.empty() && leavesByRoot.contains(path))
        return InterconnectReference{path.str(), {}};
      SmallVector<Operation *> children = getChildren(expression);
      if (isa<semantic::SVConversionExpressionOp>(expression) &&
          children.size() == 1)
        return getInterconnectReference(children.front());
      if (!isa<semantic::SVElementSelectExpressionOp>(expression) ||
          children.size() != 2)
        return std::nullopt;
      std::optional<InterconnectReference> base =
          getInterconnectReference(children.front());
      std::optional<int64_t> index = parseKnownIndex(children[1]);
      if (!base || !index)
        return std::nullopt;
      base->indices.push_back(*index);
      return base;
    };
    auto referencedLeaves = [&](Operation *expression) {
      SmallVector<unsigned> result;
      std::optional<InterconnectReference> reference =
          getInterconnectReference(expression);
      if (!reference)
        return result;
      auto found = leavesByRoot.find(reference->root);
      if (found == leavesByRoot.end())
        return result;
      for (unsigned id : found->second)
        if (leaves[id].indices.size() >= reference->indices.size() &&
            llvm::equal(reference->indices,
                        ArrayRef(leaves[id].indices)
                            .take_front(reference->indices.size())))
          result.push_back(id);
      return result;
    };
    std::function<void(Type, SmallVectorImpl<Type> &)> flattenFormalType;
    flattenFormalType = [&](Type type, SmallVectorImpl<Type> &result) {
      if (auto array = dyn_cast<semantic::RangedUnpackedArrayType>(type)) {
        uint64_t count =
            array.getLeft() >= array.getRight()
                ? uint64_t(array.getLeft()) - uint64_t(array.getRight()) + 1
                : uint64_t(array.getRight()) - uint64_t(array.getLeft()) + 1;
        for (uint64_t index = 0; index != count; ++index)
          flattenFormalType(array.getElementType(), result);
        return;
      }
      result.push_back(type);
    };
    auto assignCandidate = [&](unsigned id, Type candidate, Location location) {
      if (containsUntyped(candidate))
        return;
      if (!leaves[id].candidate) {
        leaves[id].candidate = candidate;
        leaves[id].candidateLocation = location;
        return;
      }
      FailureOr<Type> previous = normalizeSemanticType(
          leaves[id].candidate, *leaves[id].candidateLocation);
      FailureOr<Type> next = normalizeSemanticType(candidate, location);
      if (failed(previous) || failed(next) || *previous != *next) {
        emitError(location) << "interconnect leaf '" << leaves[id].path
                            << "' has incompatible connected net-port types";
        invalid = true;
      }
    };

    for (semantic::SVPortConnectionOp connection : portConnections) {
      Operation *actual = getPortActualLValue(connection);
      SmallVector<unsigned> actualLeaves = referencedLeaves(actual);
      StringRef internalPath =
          connection.getInternalPath().value_or(StringRef{});
      SmallVector<unsigned> internalLeaves;
      if (Operation *internal = getSingleRegionRoot(connection.getInternal()))
        internalLeaves = referencedLeaves(internal);
      else if (auto found = leavesByRoot.find(internalPath);
               found != leavesByRoot.end())
        internalLeaves = found->second;

      if (!actualLeaves.empty() && !internalLeaves.empty()) {
        if (actualLeaves.size() != internalLeaves.size()) {
          emitError(getSemanticLocation(connection))
              << "interconnect port association has incompatible fixed "
                 "leaf counts";
          invalid = true;
        } else {
          for (auto [actualLeaf, internalLeaf] :
               llvm::zip(actualLeaves, internalLeaves))
            join(actualLeaf, internalLeaf);
        }
      }

      if (!actualLeaves.empty()) {
        SmallVector<Type> formalLeaves;
        flattenFormalType(connection.getFormalType(), formalLeaves);
        if (!llvm::any_of(formalLeaves, containsUntyped)) {
          if (formalLeaves.size() != actualLeaves.size()) {
            emitError(getSemanticLocation(connection))
                << "interconnect port association has incompatible typed "
                   "leaf counts";
            invalid = true;
          } else {
            for (auto [actualLeaf, type] :
                 llvm::zip(actualLeaves, formalLeaves))
              assignCandidate(actualLeaf, type,
                              getSemanticLocation(connection));
          }
        }
      }
    }

    // Move every candidate to its connected-component root and reject
    // conflicting types before publishing the result back to individual
    // leaves.
    for (unsigned id = 0; id != leaves.size(); ++id) {
      unsigned root = find(id);
      if (id == root || !leaves[id].candidate)
        continue;
      assignCandidate(root, leaves[id].candidate,
                      *leaves[id].candidateLocation);
    }
    for (unsigned id = 0; id != leaves.size(); ++id) {
      unsigned root = find(id);
      if (!leaves[root].candidate) {
        emitError(getSemanticLocation(leaves[id].owner))
            << "interconnect leaf '" << leaves[id].path
            << "' has no typed net-port connection";
        invalid = true;
        continue;
      }
      leaves[id].candidate = leaves[root].candidate;
      leaves[id].candidateLocation = leaves[root].candidateLocation;
    }

    // Scalar interconnects use the ordinary descriptor path. Aggregate
    // interconnects retain their typeless declaration shape and carry a
    // compact, declaration-ordered leaf inventory for topology lowering.
    for (auto &entry : interconnects) {
      semantic::SVNetSymbolOp net = entry.second;
      auto foundLeaves = leavesByRoot.find(entry.getKey());
      if (foundLeaves == leavesByRoot.end())
        continue;
      ArrayRef<unsigned> ids = foundLeaves->second;
      if (ids.size() == 1 && leaves[ids.front()].indices.empty()) {
        net->setAttr("semantic_type",
                     TypeAttr::get(leaves[ids.front()].candidate));
        continue;
      }
      SmallVector<Attribute> definitions;
      definitions.reserve(ids.size());
      for (unsigned id : ids)
        definitions.push_back(builder.getDictionaryAttr(
            {builder.getNamedAttr("path",
                                  builder.getStringAttr(leaves[id].path)),
             builder.getNamedAttr(
                 "indices", builder.getDenseI64ArrayAttr(leaves[id].indices)),
             builder.getNamedAttr("type",
                                  TypeAttr::get(leaves[id].candidate))}));
      net->setAttr(interconnectLeavesAttrName,
                   builder.getArrayAttr(definitions));
    }

    // Freeze a direct descriptor identity and executable type on every fully
    // selected leaf expression. Unit capture and lowering can then bypass the
    // deliberately typeless aggregate root.
    semanticRoot->walk([&](semantic::SVElementSelectExpressionOp select) {
      SmallVector<unsigned> ids = referencedLeaves(select);
      if (ids.size() != 1 ||
          leaves[ids.front()].indices.size() !=
              getInterconnectReference(select)->indices.size())
        return;
      InterconnectLeaf &leaf = leaves[ids.front()];
      select->setAttr(interconnectLeafPathAttrName,
                      builder.getStringAttr(leaf.path));
      select->setAttr("semantic_type", TypeAttr::get(leaf.candidate));
    });
    for (semantic::SVPortConnectionOp connection : portConnections) {
      Operation *actual = getPortActualLValue(connection);
      SmallVector<unsigned> ids = referencedLeaves(actual);
      if (ids.size() != 1)
        continue;
      Type type = leaves[ids.front()].candidate;
      connection.getActual().walk([&](semantic::SVAssignmentExpressionOp op) {
        auto semanticType = op->getAttrOfType<TypeAttr>("semantic_type");
        if (semanticType && isa<semantic::UntypedType>(semanticType.getValue()))
          op->setAttr("semantic_type", TypeAttr::get(type));
      });
    }
  }
  if (invalid)
    return abort();

  FailureOr<llvm::StringMap<DescriptorInfo>> preparedDescriptors =
      materializeDesignDescriptors(module, semanticRoot, *portAliases, *scopes,
                                   designPrecisionFs, builder);
  if (failed(preparedDescriptors))
    return abort();
  llvm::StringMap<DescriptorInfo> &descriptors = *preparedDescriptors;

  if (failed(materializeRandomConstraintTemplates(
          design, *classes, semanticSymbols, descriptors)))
    return abort();

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
  struct NegativeTimingDelayedTerminalPlan {
    std::string sourcePath;
    DescriptorInfo source;
    uint64_t delayedStorage = UINT64_MAX;
    int64_t delayTicks = 0;
    std::string monitorSymbol;
  };
  SmallVector<NegativeTimingDelayedTerminalPlan> negativeTimingTerminals;
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

  // IEEE 1800-2017 Clause 30 module paths delay changes produced by a path's
  // source before publishing them at its destination. Unconditional paths
  // with one effective tuple are exactly an inertial delay on the already
  // isolated continuous driver. Conditional and overlapping paths instead
  // freeze a small rule set that lowering expands into straight-line selects;
  // the scheduler never interprets or scans a path table.
  struct TimingTerminal {
    std::string path;
    uint64_t rootWidth = 0;
    uint64_t low = 0;
    uint64_t width = 0;
    uint64_t lsb = 0;

    bool isWhole() const { return low == 0 && width == rootWidth; }
  };
  struct SimpleTimingPath {
    semantic::SVTimingPathSymbolOp declaration;
    SmallVector<TimingTerminal, 2> inputs;
    TimingTerminal output;
    bool full = false;
    int32_t polarity;
    bool edgeSensitive = false;
    int32_t edgeIdentifier = 0;
    int32_t edgePolarity = 0;
    SmallVector<int64_t, 12> delays;
    SmallVector<int64_t, 12> pulseRejectLimits;
    SmallVector<int64_t, 12> pulseErrorLimits;
    bool pulseOnDetect = false;
    bool pulseShowCancelled = false;
    Operation *condition = nullptr;
    bool ifnone = false;
  };

  struct PulseStyleRange {
    uint64_t low;
    uint64_t width;
    int32_t kind;
  };
  llvm::DenseMap<Operation *, llvm::StringMap<SmallVector<PulseStyleRange, 2>>>
      pulseStyles;
  semanticRoot->walk([&](semantic::SVPulseStyleSymbolOp style) {
    auto kind = style->getAttrOfType<IntegerAttr>("pulse_style_kind");
    auto terminals = style->getAttrOfType<ArrayAttr>("pulse_style_terminals");
    if (!kind || kind.getInt() < 0 || kind.getInt() > 3 || !terminals) {
      emitError(getSemanticLocation(style))
          << "specify pulse control has malformed frozen terminal data";
      invalid = true;
      return;
    }
    for (Attribute attr : terminals) {
      auto terminal = dyn_cast<DictionaryAttr>(attr);
      auto path = terminal ? terminal.getAs<StringAttr>("path") : StringAttr{};
      auto low = terminal ? terminal.getAs<IntegerAttr>("low") : IntegerAttr{};
      auto width =
          terminal ? terminal.getAs<IntegerAttr>("width") : IntegerAttr{};
      if (!path || !low || low.getInt() < 0 || !width || width.getInt() <= 0) {
        emitError(getSemanticLocation(style))
            << "specify pulse control has malformed frozen terminal data";
        invalid = true;
        return;
      }
      pulseStyles[style->getParentOp()][path.getValue()].push_back(
          {static_cast<uint64_t>(low.getInt()),
           static_cast<uint64_t>(width.getInt()),
           static_cast<int32_t>(kind.getInt())});
    }
  });

  // IEEE 1800-2017 30.7.1: a path-specific PATHPULSE$ overrides the
  // module-wide value; either overrides the default limits inherited from the
  // path delay. SDF gets the final precedence later under 30.7.3 and 32.5.
  struct PathPulseLimit {
    std::string source;
    std::string destination;
    int64_t reject;
    int64_t error;
  };
  llvm::DenseMap<Operation *, SmallVector<PathPulseLimit, 2>> pathPulseLimits;
  semanticRoot->walk([&](semantic::SVSpecparamSymbolOp specparam) {
    auto name = specparam->getAttrOfType<StringAttr>("path_pulse_name");
    auto reject = specparam->getAttrOfType<IntegerAttr>("path_pulse_reject_fs");
    auto error = specparam->getAttrOfType<IntegerAttr>("path_pulse_error_fs");
    if (!name)
      return;
    if (!reject || reject.getInt() < 0 || !error || error.getInt() < 0 ||
        error.getInt() < reject.getInt()) {
      emitError(getSemanticLocation(specparam))
          << "PATHPULSE$ reject and error limits must be static, nonnegative, "
             "and ordered";
      invalid = true;
      return;
    }
    StringRef suffix =
        name.getValue().drop_front(StringRef("PATHPULSE$").size());
    StringRef source;
    StringRef destination;
    if (!suffix.empty()) {
      auto split = suffix.split('$');
      if (split.first.empty() || split.second.empty() ||
          split.second.contains('$')) {
        emitError(getSemanticLocation(specparam))
            << "PATHPULSE$ path name is malformed";
        invalid = true;
        return;
      }
      source = split.first;
      destination = split.second;
    }
    pathPulseLimits[specparam->getParentOp()].push_back(
        {source.str(), destination.str(), reject.getInt(), error.getInt()});
  });

  llvm::StringMap<SmallVector<SimpleTimingPath, 2>> simpleTimingPaths;
  semanticRoot->walk([&](semantic::SVTimingPathSymbolOp path) {
    if (!path->hasAttr("obelisk.simple_timing_path"))
      return;
    auto inputs = path->getAttrOfType<ArrayAttr>("timing_input_terminals");
    auto output = path->getAttrOfType<DictionaryAttr>("timing_output_terminal");
    auto polarity = path->getAttrOfType<IntegerAttr>("timing_polarity");
    auto edgeIdentifier =
        path->getAttrOfType<IntegerAttr>("timing_edge_identifier");
    auto edgePolarity =
        path->getAttrOfType<IntegerAttr>("timing_edge_polarity");
    bool edgeSensitive = path->hasAttr("timing_edge_sensitive");
    int64_t edgeIdentifierValue = edgeIdentifier ? edgeIdentifier.getInt() : 0;
    int64_t edgePolarityValue = edgePolarity ? edgePolarity.getInt() : 0;
    auto delays = path->getAttrOfType<DenseI64ArrayAttr>("timing_delay_fs");
    if (!inputs || inputs.empty() || !output || !polarity ||
        polarity.getInt() < 0 || polarity.getInt() > 2 ||
        edgeIdentifierValue < 0 || edgeIdentifierValue > 3 ||
        edgePolarityValue < 0 || edgePolarityValue > 2 ||
        (!edgeSensitive &&
         (edgeIdentifierValue != 0 || edgePolarityValue != 0)) ||
        !delays ||
        (delays.size() != 1 && delays.size() != 2 && delays.size() != 3 &&
         delays.size() != 6 && delays.size() != 12)) {
      emitError(getSemanticLocation(path))
          << "simple specify path is missing frozen terminal or delay data";
      invalid = true;
      return;
    }
    auto parseTerminal = [&](Attribute attr) -> std::optional<TimingTerminal> {
      auto terminal = dyn_cast<DictionaryAttr>(attr);
      auto terminalPath =
          terminal ? terminal.getAs<StringAttr>("path") : StringAttr{};
      auto rootWidth =
          terminal ? terminal.getAs<IntegerAttr>("root_width") : IntegerAttr{};
      auto low = terminal ? terminal.getAs<IntegerAttr>("low") : IntegerAttr{};
      auto width =
          terminal ? terminal.getAs<IntegerAttr>("width") : IntegerAttr{};
      auto lsb = terminal ? terminal.getAs<IntegerAttr>("lsb") : IntegerAttr{};
      if (!terminalPath || !rootWidth || rootWidth.getInt() <= 0 || !low ||
          low.getInt() < 0 || !width || width.getInt() <= 0 ||
          (edgeSensitive && (!lsb || lsb.getInt() < 0)) ||
          static_cast<uint64_t>(low.getInt()) >=
              static_cast<uint64_t>(rootWidth.getInt()) ||
          static_cast<uint64_t>(width.getInt()) >
              static_cast<uint64_t>(rootWidth.getInt()) -
                  static_cast<uint64_t>(low.getInt()))
        return std::nullopt;
      uint64_t lsbValue = lsb ? static_cast<uint64_t>(lsb.getInt())
                              : static_cast<uint64_t>(low.getInt());
      if (lsbValue < static_cast<uint64_t>(low.getInt()) ||
          lsbValue >= static_cast<uint64_t>(low.getInt()) +
                          static_cast<uint64_t>(width.getInt()))
        return std::nullopt;
      return TimingTerminal{terminalPath.getValue().str(),
                            static_cast<uint64_t>(rootWidth.getInt()),
                            static_cast<uint64_t>(low.getInt()),
                            static_cast<uint64_t>(width.getInt()), lsbValue};
    };
    SmallVector<TimingTerminal, 2> inputPaths;
    for (Attribute attr : inputs) {
      std::optional<TimingTerminal> input = parseTerminal(attr);
      if (!input) {
        emitError(getSemanticLocation(path))
            << "simple specify path has malformed terminal data";
        invalid = true;
        return;
      }
      inputPaths.push_back(std::move(*input));
    }
    std::optional<TimingTerminal> outputTerminal = parseTerminal(output);
    if (!outputTerminal) {
      emitError(getSemanticLocation(path))
          << "simple specify path has malformed output terminal data";
      invalid = true;
      return;
    }
    SmallVector<Operation *> children = getChildren(path);
    bool conditional = path->hasAttr("timing_condition");
    bool ifnone = path->hasAttr("timing_ifnone");
    size_t expectedChildren =
        static_cast<size_t>(conditional) + static_cast<size_t>(edgeSensitive);
    if ((conditional && ifnone) || (edgeSensitive && ifnone) ||
        children.size() != expectedChildren) {
      emitError(getSemanticLocation(path))
          << "simple conditional specify path has malformed condition data";
      invalid = true;
      return;
    }
    auto connectionFull =
        path->getAttrOfType<BoolAttr>("timing_connection_full");
    if (!connectionFull) {
      emitError(getSemanticLocation(path))
          << "simple specify path has no frozen connection kind";
      invalid = true;
      return;
    }
    bool full = connectionFull.getValue();
    SmallVector<int64_t, 12> normalizedDelays(delays.asArrayRef());
    auto globalReject =
        module->getAttrOfType<IntegerAttr>("obelisk.pulse_reject_percent");
    auto globalError =
        module->getAttrOfType<IntegerAttr>("obelisk.pulse_error_percent");
    auto percentLimits = [&](IntegerAttr percent) {
      SmallVector<int64_t, 12> result;
      result.reserve(normalizedDelays.size());
      uint64_t value = percent ? percent.getUInt() : 100;
      for (int64_t delay : normalizedDelays) {
        uint64_t unsignedDelay = static_cast<uint64_t>(delay);
        result.push_back(
            static_cast<int64_t>((unsignedDelay / 100) * value +
                                 ((unsignedDelay % 100) * value) / 100));
      }
      return result;
    };
    // IEEE 1800-2017 30.7 and 30.7.2 default both limits to 100% of
    // each transition delay and apply the two global percentages per
    // transition. Compute in quotient/remainder form to avoid overflowing
    // large but otherwise valid static femtosecond delays.
    SmallVector<int64_t, 12> rejectLimits = percentLimits(globalReject);
    SmallVector<int64_t, 12> errorLimits = percentLimits(globalError);
    auto leafName = [](StringRef value) { return value.rsplit('.').second; };
    const PathPulseLimit *selectedPulse = nullptr;
    for (const PathPulseLimit &pulse : pathPulseLimits[path->getParentOp()]) {
      bool matches = pulse.source.empty() ||
                     (leafName(inputPaths.front().path) == pulse.source &&
                      leafName(outputTerminal->path) == pulse.destination);
      if (matches && (!selectedPulse || !pulse.source.empty()))
        selectedPulse = &pulse;
    }
    if (selectedPulse) {
      rejectLimits.assign(normalizedDelays.size(), selectedPulse->reject);
      errorLimits.assign(normalizedDelays.size(), selectedPulse->error);
    }
    SmallVector<uint64_t, 6> pulseBoundaries{
        outputTerminal->low, outputTerminal->low + outputTerminal->width};
    ArrayRef<PulseStyleRange> outputStyles;
    if (auto styles = pulseStyles.find(path->getParentOp());
        styles != pulseStyles.end()) {
      auto found = styles->second.find(outputTerminal->path);
      if (found != styles->second.end()) {
        outputStyles = found->second;
        for (const PulseStyleRange &style : outputStyles) {
          uint64_t pathEnd = outputTerminal->low + outputTerminal->width;
          uint64_t styleEnd = style.low + style.width;
          if (style.low >= pathEnd || outputTerminal->low >= styleEnd)
            continue;
          pulseBoundaries.push_back(std::max(style.low, outputTerminal->low));
          pulseBoundaries.push_back(std::min(styleEnd, pathEnd));
        }
      }
    }
    llvm::sort(pulseBoundaries);
    pulseBoundaries.erase(
        std::unique(pulseBoundaries.begin(), pulseBoundaries.end()),
        pulseBoundaries.end());
    for (auto [segmentLow, segmentEnd] :
         llvm::zip(ArrayRef<uint64_t>(pulseBoundaries).drop_back(),
                   ArrayRef<uint64_t>(pulseBoundaries).drop_front())) {
      if (segmentLow == segmentEnd)
        continue;
      bool pulseOnDetect = false;
      bool pulseShowCancelled = false;
      for (const PulseStyleRange &style : outputStyles) {
        if (style.low > segmentLow || style.low + style.width < segmentEnd)
          continue;
        if (style.kind == 0)
          pulseOnDetect = false;
        else if (style.kind == 1)
          pulseOnDetect = true;
        else if (style.kind == 2)
          pulseShowCancelled = true;
        else
          pulseShowCancelled = false;
      }
      // IEEE 1800-2017 30.7.4.1-.2 gives invocation controls precedence over
      // specify-block declarations. Preserve that precedence after range
      // splitting so it cannot vary accidentally across packed output bits.
      if (auto global =
              module->getAttrOfType<BoolAttr>("obelisk.pulse_on_detect"))
        pulseOnDetect = global.getValue();
      if (auto global =
              module->getAttrOfType<BoolAttr>("obelisk.pulse_show_cancelled"))
        pulseShowCancelled = global.getValue();
      TimingTerminal segment = *outputTerminal;
      segment.low = segmentLow;
      segment.width = segmentEnd - segmentLow;
      SmallVector<TimingTerminal, 2> segmentInputs = inputPaths;
      // IEEE 1800-2017 30.7.4 attaches style to each declared path output.
      // Split only at static style boundaries so packed execution remains a
      // few ordinary rules.  For a parallel path, Clause 30.3 maps source and
      // destination bits positionally; a full or edge-sensitive path retains
      // its complete source terminal for every destination segment.
      if (!full && !edgeSensitive) {
        uint64_t delta = segmentLow - outputTerminal->low;
        for (TimingTerminal &input : segmentInputs) {
          input.low += delta;
          input.width = segment.width;
          input.lsb = input.low;
        }
      }
      simpleTimingPaths[outputTerminal->path].push_back(
          {path, segmentInputs, segment, full,
           static_cast<int32_t>(polarity.getInt()), edgeSensitive,
           static_cast<int32_t>(edgeIdentifierValue),
           static_cast<int32_t>(edgePolarityValue), normalizedDelays,
           rejectLimits, errorLimits, pulseOnDetect, pulseShowCancelled,
           conditional ? children.front() : nullptr, ifnone});
    }
  });

  // Negative combined timing checks need one delayed copy per original
  // terminal across the complete design instance.  Solve that inventory here,
  // before isolated timing actors are formed; no runtime timing-check table or
  // name lookup is needed after these descriptor IDs are frozen.
  struct NegativeTimingCheckPlan {
    semantic::SVSystemTimingCheckSymbolOp check;
    int32_t kind = 0;
    unsigned reference = 0;
    unsigned data = 0;
    int64_t limit0 = 0;
    int64_t limit1 = 0;
  };
  SmallVector<NegativeTimingCheckPlan> negativeChecks;
  using NegativeTerminalKey = std::pair<unsigned, uint64_t>;
  llvm::DenseMap<NegativeTerminalKey, unsigned> negativeTerminalIndices;
  constexpr size_t maxNegativeTimingChecks = 65536;
  constexpr size_t maxNegativeTimingTerminals = 65536;
  constexpr uint64_t maxNegativeTimingRelaxations = 16 * 1024 * 1024;

  auto timingEventPath = [&](semantic::SVSystemTimingCheckSymbolOp check,
                             unsigned argument) -> std::optional<StringRef> {
    auto children = check->getAttrOfType<DenseI64ArrayAttr>(
        "timing_check_arg_expression_children");
    SmallVector<Operation *> roots = getChildren(check);
    if (!children || argument >= children.size())
      return std::nullopt;
    int64_t child = children[argument];
    if (child < 0 || static_cast<size_t>(child) >= roots.size())
      return std::nullopt;
    Operation *expression = roots[child];
    while (isa<semantic::SVConversionExpressionOp>(expression)) {
      SmallVector<Operation *> nested = getChildren(expression);
      if (nested.size() != 1)
        return std::nullopt;
      expression = nested.front();
    }
    if (auto named = dyn_cast<semantic::SVNamedValueExpressionOp>(expression))
      return named.getReferencedPath();
    if (auto hierarchical =
            dyn_cast<semantic::SVHierarchicalValueExpressionOp>(expression))
      return hierarchical.getReferencedPath();
    return std::nullopt;
  };
  auto terminalIndex = [&](StringRef path,
                           Location location) -> std::optional<unsigned> {
    auto descriptor = descriptors.find(path);
    if (descriptor == descriptors.end() ||
        (descriptor->second.kind != DescriptorInfo::Kind::Storage &&
         descriptor->second.kind != DescriptorInfo::Kind::Net) ||
        !descriptor->second.viewIndices.empty() ||
        descriptor->second.viewOffset != 0 ||
        descriptor->second.packedViewOffset != 0 ||
        (descriptor->second.rootType &&
         descriptor->second.rootType != descriptor->second.type)) {
      emitError(location)
          << "negative timing-check event must name one whole direct packed "
             "storage or net terminal";
      invalid = true;
      return std::nullopt;
    }
    NegativeTerminalKey key{static_cast<unsigned>(descriptor->second.kind),
                            descriptor->second.id};
    auto existing = negativeTerminalIndices.find(key);
    if (existing != negativeTerminalIndices.end())
      return existing->second;
    if (negativeTimingTerminals.size() == maxNegativeTimingTerminals) {
      emitError(location)
          << "negative timing-check delayed-terminal inventory exceeds "
             "the static planning limit";
      invalid = true;
      return std::nullopt;
    }
    std::optional<unsigned> width =
        sim::getPackedWidth(descriptor->second.type);
    if (!width || *width == 0) {
      emitError(location)
          << "negative timing-check event terminal is not packed";
      invalid = true;
      return std::nullopt;
    }
    unsigned index = negativeTimingTerminals.size();
    negativeTerminalIndices[key] = index;
    NegativeTimingDelayedTerminalPlan plan;
    plan.sourcePath = path.str();
    plan.source = descriptor->second;
    negativeTimingTerminals.push_back(std::move(plan));
    return index;
  };
  auto affectedTerminal = [&](StringRef path) {
    auto descriptor = descriptors.find(path);
    if (descriptor == descriptors.end())
      return false;
    NegativeTerminalKey key{static_cast<unsigned>(descriptor->second.kind),
                            descriptor->second.id};
    return negativeTerminalIndices.contains(key);
  };

  semanticRoot->walk([&](semantic::SVSystemTimingCheckSymbolOp check) {
    if (!check->hasAttr("obelisk.negative_timing_check") ||
        check->hasAttr("obelisk.invalid_negative_timing_window"))
      return;
    if (!check->hasAttr("obelisk.basic_timing_check"))
      return; // PrepareValidation owns the optional-argument diagnostic.
    if (negativeChecks.size() == maxNegativeTimingChecks) {
      emitError(getSemanticLocation(check))
          << "negative timing-check inventory exceeds the static planning "
             "limit";
      invalid = true;
      return;
    }
    auto kind = check->getAttrOfType<IntegerAttr>("timing_check_kind");
    auto times =
        check->getAttrOfType<DenseI64ArrayAttr>("timing_check_arg_time_fs");
    std::optional<StringRef> referencePath = timingEventPath(check, 0);
    std::optional<StringRef> dataPath = timingEventPath(check, 1);
    if (!kind || (kind.getInt() != 3 && kind.getInt() != 6) || !times ||
        times.size() <= 3 || !referencePath || !dataPath ||
        designPrecisionFs > static_cast<uint64_t>(INT64_MAX) ||
        times[2] % static_cast<int64_t>(designPrecisionFs) != 0 ||
        times[3] % static_cast<int64_t>(designPrecisionFs) != 0) {
      emitError(getSemanticLocation(check))
          << "negative timing check has no exact static direct design-"
             "precision ABI";
      invalid = true;
      return;
    }
    std::optional<unsigned> reference =
        terminalIndex(*referencePath, getSemanticLocation(check));
    std::optional<unsigned> data =
        terminalIndex(*dataPath, getSemanticLocation(check));
    if (!reference || !data)
      return;
    negativeChecks.push_back(
        {check, static_cast<int32_t>(kind.getInt()), *reference, *data,
         times[2] / static_cast<int64_t>(designPrecisionFs),
         times[3] / static_cast<int64_t>(designPrecisionFs)});
  });

  if (!negativeChecks.empty() && !invalid) {
    llvm::DenseSet<Operation *> negativeDeclarations;
    for (const NegativeTimingCheckPlan &check : negativeChecks)
      negativeDeclarations.insert(check.check);
    // IEEE 1800-2017 31.9.1 requires every non-skew timing check sharing a
    // delayed terminal to be rebound and adjusted.  This first tranche rejects
    // such a component rather than running the existing actor on an undelayed
    // approximation.
    semanticRoot->walk([&](semantic::SVSystemTimingCheckSymbolOp check) {
      if (negativeDeclarations.contains(check))
        return;
      for (unsigned argument : {0u, 1u}) {
        std::optional<StringRef> path = timingEventPath(check, argument);
        if (path && affectedTerminal(*path)) {
          emitError(getSemanticLocation(check))
              << "IEEE 1800-2017 31.9.1 delayed terminal '" << *path
              << "' also participates in another timing-check kind; that "
                 "cross-kind component is not executable yet";
          invalid = true;
          return;
        }
      }
    });
    for (auto &entry : simpleTimingPaths)
      for (const SimpleTimingPath &path : entry.second)
        for (const TimingTerminal &input : path.inputs)
          if (affectedTerminal(input.path)) {
            emitError(getSemanticLocation(path.declaration))
                << "IEEE 1800-2017 31.9.1 delayed terminal '" << input.path
                << "' also sources a Clause 30 propagation path; path-delay "
                   "flooring for that component is not executable yet";
            invalid = true;
          }
  }

  if (!negativeChecks.empty() && !invalid) {
    struct ConstraintEdge {
      unsigned from;
      unsigned to;
      int64_t weight;
    };
    SmallVector<int64_t> delays(negativeTimingTerminals.size());
    SmallVector<SmallVector<unsigned>> incidentChecks(delays.size());
    for (auto [checkIndex, check] : llvm::enumerate(negativeChecks)) {
      incidentChecks[check.reference].push_back(checkIndex);
      if (check.data != check.reference)
        incidentChecks[check.data].push_back(checkIndex);
    }
    uint64_t relaxationWork = 0;
    while (true) {
      SmallVector<ConstraintEdge> constraints;
      constraints.reserve(negativeChecks.size() * 2);
      bool arithmeticInvalid = false;
      auto checkedI64 = [&](const __int128 value, Location location,
                            StringRef purpose) -> std::optional<int64_t> {
        if (value < INT64_MIN || value > INT64_MAX) {
          emitError(location) << "negative timing-check " << purpose
                              << " exceeds the signed tick range";
          arithmeticInvalid = true;
          return std::nullopt;
        }
        return static_cast<int64_t>(value);
      };
      for (const NegativeTimingCheckPlan &check : negativeChecks) {
        if (check.limit0 >= 0 && check.limit1 >= 0)
          continue;
        // IEEE 1800-2017 31.9.1 makes both original window endpoints open.
        // On the integer precision lattice the delayed copies therefore need
        // a strict one-tick interior:
        // setuphold: -setup+1 <= dR-dD <= hold-1;
        // recrem:     1-removal <= dR-dD <= recovery-1.
        // This is the 0.01 margin that produces the exact 31.9.2 example
        // delays 10.01, 20.02, and 2.02.
        __int128 lower = check.kind == 3
                             ? -static_cast<__int128>(check.limit0) + 1
                             : 1 - static_cast<__int128>(check.limit1);
        __int128 upper = check.kind == 3
                             ? static_cast<__int128>(check.limit1) - 1
                             : static_cast<__int128>(check.limit0) - 1;
        std::optional<int64_t> lowerTick = checkedI64(
            lower, getSemanticLocation(check.check), "lower delay bound");
        std::optional<int64_t> reverseTick = checkedI64(
            -upper, getSemanticLocation(check.check), "upper delay bound");
        if (!lowerTick || !reverseTick)
          continue;
        constraints.push_back({check.data, check.reference, *lowerTick});
        constraints.push_back({check.reference, check.data, *reverseTick});
      }
      if (arithmeticInvalid) {
        invalid = true;
        break;
      }
      llvm::fill(delays, int64_t{0});
      std::optional<unsigned> infeasibleTerminal;
      for (size_t iteration = 0; iteration < delays.size(); ++iteration) {
        bool changed = false;
        for (const ConstraintEdge &edge : constraints) {
          if (++relaxationWork > maxNegativeTimingRelaxations) {
            emitError(getSemanticLocation(negativeChecks.front().check))
                << "negative timing-check constraint solving exceeds the "
                   "static planning work limit";
            invalid = true;
            break;
          }
          __int128 candidate =
              static_cast<__int128>(delays[edge.from]) + edge.weight;
          if (candidate > INT64_MAX) {
            emitError(getSemanticLocation(negativeChecks.front().check))
                << "negative timing-check delayed signal exceeds the "
                   "supported simulation-time range";
            invalid = true;
            break;
          }
          if (candidate > delays[edge.to]) {
            delays[edge.to] = static_cast<int64_t>(candidate);
            changed = true;
            if (iteration + 1 == delays.size() && !infeasibleTerminal)
              infeasibleTerminal = edge.to;
          }
        }
        if (invalid || !changed)
          break;
      }
      if (invalid || !infeasibleTerminal)
        break;

      llvm::SmallDenseSet<unsigned> inconsistentTerminals;
      inconsistentTerminals.insert(*infeasibleTerminal);
      SmallVector<unsigned> pending{*infeasibleTerminal};
      while (!pending.empty() && !invalid) {
        unsigned terminal = pending.pop_back_val();
        for (unsigned checkIndex : incidentChecks[terminal]) {
          // Component discovery is part of the same statically bounded solve,
          // not an uncharged all-edge rescan for every newly reached terminal.
          if (++relaxationWork > maxNegativeTimingRelaxations) {
            emitError(getSemanticLocation(negativeChecks.front().check))
                << "negative timing-check constraint solving exceeds the "
                   "static planning work limit";
            invalid = true;
            break;
          }
          const NegativeTimingCheckPlan &check = negativeChecks[checkIndex];
          if (check.limit0 >= 0 && check.limit1 >= 0)
            continue;
          if (inconsistentTerminals.insert(check.reference).second)
            pending.push_back(check.reference);
          if (inconsistentTerminals.insert(check.data).second)
            pending.push_back(check.data);
        }
      }
      if (invalid)
        break;
      NegativeTimingCheckPlan *repair = nullptr;
      bool repairFirst = false;
      int64_t smallest = 0;
      for (NegativeTimingCheckPlan &check : negativeChecks) {
        if (!inconsistentTerminals.contains(check.reference) &&
            !inconsistentTerminals.contains(check.data))
          continue;
        for (bool first : {true, false}) {
          int64_t value = first ? check.limit0 : check.limit1;
          if (value < 0 && (!repair || value < smallest)) {
            repair = &check;
            repairFirst = first;
            smallest = value;
          }
        }
      }
      if (!repair) {
        emitError(getSemanticLocation(negativeChecks.front().check))
            << "negative timing-check constraints are inconsistent after "
               "all negative limits were repaired";
        invalid = true;
        break;
      }
      emitWarning(getSemanticLocation(repair->check))
          << "IEEE 1800-2017 31.9.1 mutually inconsistent delayed-signal "
             "constraints; changing smallest negative limit "
          << smallest << " ticks to 0 and recalculating";
      (repairFirst ? repair->limit0 : repair->limit1) = 0;
    }

    if (!invalid) {
      for (auto [index, delay] : llvm::enumerate(delays))
        negativeTimingTerminals[index].delayTicks = delay;
      for (NegativeTimingCheckPlan &check : negativeChecks) {
        int64_t referenceDelay = delays[check.reference];
        int64_t dataDelay = delays[check.data];
        __int128 difference = static_cast<__int128>(referenceDelay) - dataDelay;
        __int128 adjusted0 =
            check.kind == 3 ? static_cast<__int128>(check.limit0) + difference
                            : static_cast<__int128>(check.limit0) - difference;
        __int128 adjusted1 =
            check.kind == 3 ? static_cast<__int128>(check.limit1) - difference
                            : static_cast<__int128>(check.limit1) + difference;
        if (adjusted0 > INT64_MAX || adjusted1 > INT64_MAX) {
          emitError(getSemanticLocation(check.check))
              << "negative timing-check adjusted limit exceeds the signed "
                 "tick range";
          invalid = true;
          continue;
        }
        if (adjusted0 <= 0 || adjusted1 <= 0) {
          emitWarning(getSemanticLocation(check.check))
              << "IEEE 1800-2017 31.9.1 adjusted timing-check limit is "
                 "nonpositive; clamping it to 0";
          adjusted0 = std::max<__int128>(adjusted0, 0);
          adjusted1 = std::max<__int128>(adjusted1, 0);
        }
        check.check->setAttr(
            "obelisk_sim.timing_adjusted_ticks",
            builder.getDenseI64ArrayAttr({static_cast<int64_t>(adjusted0),
                                          static_cast<int64_t>(adjusted1)}));
        check.check->setAttr("obelisk_sim.negative_timing_adjusted",
                             builder.getUnitAttr());
        check.check->setAttr(
            "obelisk_sim.timing_delayed_terminal_indices",
            builder.getDenseI64ArrayAttr({static_cast<int64_t>(check.reference),
                                          static_cast<int64_t>(check.data)}));
      }
    }
  }

  if (!negativeChecks.empty() && !invalid) {
    for (auto &terminal : negativeTimingTerminals) {
      // IEEE 1800-2017 31.9.1 introduces a delayed terminal only when the
      // solved delay is positive. A zero solution is the original terminal;
      // mirroring it through another Active publication would create a false
      // scheduler dependency and an extra delta-cycle occurrence.
      if (terminal.delayTicks == 0)
        continue;
      if (nextStorageId == UINT64_MAX) {
        emitError(module.getLoc())
            << "negative timing-check delayed signals exceed the storage "
               "descriptor space";
        invalid = true;
        break;
      }
      terminal.delayedStorage = nextStorageId++;
      std::string path = (Twine("__obelisk_negative_timing_delay_") +
                          Twine(terminal.delayedStorage))
                             .str();
      DescriptorInfo delayed{DescriptorInfo::Kind::Storage,
                             terminal.delayedStorage, terminal.source.scopeId,
                             terminal.source.type,
                             sim::NetResolutionKind::Wire};
      delayed.rootType = terminal.source.type;
      descriptors[path] = delayed;
      sim::SimStorageDeclOp::create(
          builder, module.getLoc(), terminal.delayedStorage,
          terminal.source.scopeId, terminal.source.type, sim::Lifetime::Design,
          builder.getStringAttr(path),
          builder.getStringAttr(
              "implicit negative timing-check delayed signal"),
          sim::ComputeObservabilityKindAttr{});
    }
    for (NegativeTimingCheckPlan &check : negativeChecks) {
      auto indices = check.check->getAttrOfType<DenseI64ArrayAttr>(
          "obelisk_sim.timing_delayed_terminal_indices");
      if (!indices || indices.size() != 2)
        continue;
      check.check->setAttr(
          "obelisk_sim.timing_delayed_storage_ids",
          builder.getDenseI64ArrayAttr(
              {static_cast<int64_t>(
                   negativeTimingTerminals[indices[0]].delayedStorage),
               static_cast<int64_t>(
                   negativeTimingTerminals[indices[1]].delayedStorage)}));
      check.check->setAttr(
          "obelisk_sim.timing_delayed_source_delays",
          builder.getDenseI64ArrayAttr(
              {negativeTimingTerminals[indices[0]].delayTicks,
               negativeTimingTerminals[indices[1]].delayTicks}));
      check.check->removeAttr("obelisk_sim.timing_delayed_terminal_indices");
    }
  }
  if (invalid)
    return abort();

  auto getDriverDependencyRoots = [&](Operation *unit) {
    SmallVector<Operation *> dependencyRoots;
    if (isa<semantic::SVContinuousAssignSymbolOp>(unit)) {
      SmallVector<Operation *> roots = getChildren(unit);
      auto assignment =
          roots.empty()
              ? semantic::SVAssignmentExpressionOp{}
              : dyn_cast<semantic::SVAssignmentExpressionOp>(roots.front());
      SmallVector<Operation *> children =
          assignment ? getChildren(assignment) : SmallVector<Operation *>{};
      if (children.size() == 2)
        dependencyRoots.push_back(children.back());
    } else if (isa<semantic::SVPrimitiveInstanceSymbolOp>(unit)) {
      bool sawInput = false;
      for (Operation *root : getChildren(unit)) {
        sawInput |= !isa<semantic::SVAssignmentExpressionOp>(root);
        if (sawInput)
          dependencyRoots.push_back(root);
      }
    } else if (isa<semantic::SVNetSymbolOp>(unit)) {
      dependencyRoots = getNetInitializerExpressions(unit);
    }
    return dependencyRoots;
  };

  // Map only statically whole, unique driver actors. This is used below to
  // prove the source closure of equal-delay paths through internal zero-delay
  // combinational nets; ambiguous or partial topology remains unsupported.
  llvm::StringMap<Operation *> wholeDriverActors;
  llvm::StringSet<> ambiguousWholeDrivers;
  if (!simpleTimingPaths.empty())
    for (const auto &entry : continuousDrivers) {
      ArrayRef<DriverInfo> drivers = entry.second;
      // A module-path delay is measured from the declared source transition,
      // so folding it onto a destination actor is valid across only
      // zero-delay combinational intermediates. Otherwise the intermediate
      // delay would be added before the path delay instead of being replaced
      // by the module-path timing relationship.
      if (drivers.empty() || entry.first->hasAttr("delay_fs"))
        continue;
      StringRef output = drivers.front().path;
      auto descriptor = descriptors.find(output);
      std::optional<unsigned> width =
          descriptor == descriptors.end()
              ? std::nullopt
              : sim::getPackedWidth(descriptor->second.type);
      bool wholeCoverage =
          width && llvm::all_of(drivers, [&](const auto &driver) {
            return driver.path == output && driver.drivenLow == 0 &&
                   driver.drivenWidth == *width;
          });
      bool strengthPair = drivers.size() == 2 && drivers[0].strengthBank &&
                          drivers[1].strengthBank &&
                          drivers[0].strengthBank != drivers[1].strengthBank;
      if (!wholeCoverage || (drivers.size() != 1 && !strengthPair))
        continue;
      auto inserted = wholeDriverActors.try_emplace(output, entry.first);
      if (!inserted.second && inserted.first->second != entry.first)
        ambiguousWholeDrivers.insert(output);
    }
  for (StringRef output : ambiguousWholeDrivers.keys())
    wholeDriverActors.erase(output);

  struct TimingDriverSpan {
    Operation *unit;
    std::optional<uint64_t> nodeID;
    uint64_t low;
    uint64_t width;
    bool procedural = false;
  };
  llvm::DenseMap<Operation *, SmallVector<Attribute>> frozenTimingRules;
  llvm::DenseMap<Operation *, llvm::StringMap<int32_t>> frozenTimingGroups;
  for (auto &entry : simpleTimingPaths) {
    StringRef output = entry.getKey();
    SmallVectorImpl<SimpleTimingPath> &paths = entry.getValue();
    SimpleTimingPath &path = paths.front();
    if (llvm::any_of(paths, [&](const SimpleTimingPath &candidate) {
          return candidate.output.rootWidth != path.output.rootWidth;
        })) {
      emitError(getSemanticLocation(path.declaration))
          << "specify paths to one output disagree on its root width";
      invalid = true;
      continue;
    }
    bool hasEdgeSensitive = llvm::any_of(
        paths, [](const SimpleTimingPath &p) { return p.edgeSensitive; });

    SmallVector<TimingDriverSpan> spans;
    for (auto &drivers : continuousDrivers) {
      SmallVector<const DriverInfo *> matching;
      for (const DriverInfo &driver : drivers.second)
        if (driver.path == output)
          matching.push_back(&driver);
      while (!matching.empty()) {
        const DriverInfo &first = *matching.pop_back_val();
        SmallVector<const DriverInfo *> equivalent{&first};
        llvm::erase_if(matching, [&](const DriverInfo *candidate) {
          bool same = candidate->drivenLow == first.drivenLow &&
                      candidate->drivenWidth == first.drivenWidth &&
                      candidate->nodeId == first.nodeId;
          if (same)
            equivalent.push_back(candidate);
          return same;
        });
        bool strengthPair =
            equivalent.size() == 2 && equivalent[0]->strengthBank &&
            equivalent[1]->strengthBank &&
            equivalent[0]->strengthBank != equivalent[1]->strengthBank;
        if (equivalent.size() != 1 && !strengthPair) {
          emitError(getSemanticLocation(path.declaration))
              << "specify path destination has an ambiguous continuous "
                 "driver span";
          invalid = true;
          break;
        }
        spans.push_back(
            {drivers.first, first.nodeId, first.drivenLow, first.drivenWidth});
      }
      if (invalid)
        break;
    }
    bool sawProceduralWriter = false;
    bool sawContinuousStorageWriter = false;
    if (hasEdgeSensitive) {
      auto descriptor = descriptors.find(output);
      bool directStorage =
          descriptor != descriptors.end() &&
          descriptor->second.kind == DescriptorInfo::Kind::Storage;
      for (Operation *source : sourceUnits) {
        bool procedural = isa<semantic::SVProceduralBlockSymbolOp>(source);
        bool continuous = isa<semantic::SVContinuousAssignSymbolOp>(source);
        if (!procedural && !continuous)
          continue;
        source->walk([&](Operation *nested) {
          auto referenced =
              nested->getAttrOfType<StringAttr>("referenced_path");
          if (!isa<semantic::SVNamedValueExpressionOp,
                   semantic::SVHierarchicalValueExpressionOp>(nested) ||
              !referenced || referenced.getValue() != output ||
              !isWrittenResolutionReference(nested))
            return;
          sawProceduralWriter |= procedural;
          sawContinuousStorageWriter |= continuous && directStorage;
          if (!procedural || !directStorage)
            return;
          auto nodeID = nested->getAttrOfType<IntegerAttr>("node_id");
          if (!nodeID || nodeID.getInt() < 0)
            return;
          spans.push_back({source, nodeID.getValue().getZExtValue(), 0,
                           path.output.rootWidth, true});
        });
      }
      if (sawProceduralWriter &&
          (sawContinuousStorageWriter || (!directStorage && !spans.empty()))) {
        emitError(getSemanticLocation(path.declaration))
            << "edge-sensitive specify path destination mixes continuous and "
               "procedural writers";
        invalid = true;
      }
    }
    if (invalid)
      continue;
    if (spans.empty()) {
      if (hasEdgeSensitive)
        emitError(getSemanticLocation(path.declaration))
            << "edge-sensitive specify path output has no executable "
               "continuous driver or direct procedural writer";
      else
        emitError(getSemanticLocation(path.declaration))
            << "simple specify path output has no continuous driver";
      invalid = true;
      continue;
    }

    bool proceduralSpans = spans.front().procedural;
    if (llvm::any_of(spans, [&](const TimingDriverSpan &span) {
          return span.procedural != proceduralSpans;
        })) {
      emitError(getSemanticLocation(path.declaration))
          << "edge-sensitive specify path destination mixes continuous and "
             "procedural writers";
      invalid = true;
      continue;
    }
    if (proceduralSpans && llvm::any_of(paths, [](const SimpleTimingPath &p) {
          return !p.edgeSensitive;
        })) {
      emitError(getSemanticLocation(path.declaration))
          << "simple specify paths on direct procedural destinations are not "
             "executable yet";
      invalid = true;
      continue;
    }

    // Every bit named by every path must have one logical owner.  Advance by
    // span endpoints instead of visiting individual bits, so wide packed
    // destinations remain compile-time constant work per driver span.
    for (const SimpleTimingPath &candidate : paths) {
      if (proceduralSpans)
        continue;
      uint64_t next = candidate.output.low;
      uint64_t end = next + candidate.output.width;
      while (next != end) {
        const TimingDriverSpan *owner = nullptr;
        uint64_t ownerEnd = end;
        for (const TimingDriverSpan &span : spans) {
          uint64_t spanEnd = span.low + span.width;
          if (span.low <= next && next < spanEnd) {
            if (owner) {
              emitError(getSemanticLocation(candidate.declaration))
                  << "specify path destination has overlapping continuous "
                     "drivers";
              invalid = true;
              break;
            }
            owner = &span;
            ownerEnd = std::min(end, spanEnd);
          }
          if (next < span.low)
            ownerEnd = std::min(ownerEnd, span.low);
        }
        if (invalid)
          break;
        if (!owner) {
          emitError(getSemanticLocation(candidate.declaration))
              << "specify path destination is not completely driven";
          invalid = true;
          break;
        }
        next = ownerEnd;
      }
      if (invalid)
        break;
    }
    if (invalid)
      continue;

    llvm::StringSet<> driverInputs;
    bool hasSimplePath = false;
    for (const SimpleTimingPath &candidate : paths)
      if (!candidate.edgeSensitive) {
        hasSimplePath = true;
        for (const TimingTerminal &input : candidate.inputs)
          driverInputs.insert(input.path);
      }
    bool identicalDelays =
        llvm::all_of(paths, [&](const SimpleTimingPath &candidate) {
          return candidate.delays == path.delays;
        });
    bool hasStateDependent = llvm::any_of(paths, [](const SimpleTimingPath &p) {
      return p.condition || p.ifnone;
    });
    auto hasExactInputs = [&](Operation *unit, bool allowTransitive) {
      // Clause 30.4.3 explicitly makes an edge path's arbitrary data-source
      // expression irrelevant to propagation and events. Its functional
      // destination actor therefore needs no dependency relationship to the
      // path source or the preserved metadata expression.
      if (!hasSimplePath)
        return true;
      llvm::StringSet<> referencedPaths;
      for (Operation *root : getDriverDependencyRoots(unit))
        root->walk([&](Operation *nested) {
          if (auto referenced =
                  nested->getAttrOfType<StringAttr>("referenced_path"))
            referencedPaths.insert(referenced.getValue());
        });
      bool exact = referencedPaths.size() == driverInputs.size();
      if (exact)
        for (StringRef input : driverInputs.keys())
          exact &= referencedPaths.contains(input);
      if (exact || !allowTransitive)
        return exact;
      llvm::StringSet<> transitiveInputs;
      llvm::DenseSet<Operation *> activeDrivers;
      std::function<bool(Operation *)> collect = [&](Operation *driverUnit) {
        if (!activeDrivers.insert(driverUnit).second)
          return false;
        bool valid = true;
        for (Operation *root : getDriverDependencyRoots(driverUnit))
          root->walk([&](Operation *nested) {
            if (!valid)
              return;
            auto referenced =
                nested->getAttrOfType<StringAttr>("referenced_path");
            if (!referenced)
              return;
            StringRef referencedPath = referenced.getValue();
            if (driverInputs.contains(referencedPath)) {
              transitiveInputs.insert(referencedPath);
              return;
            }
            auto driver = wholeDriverActors.find(referencedPath);
            if (driver == wholeDriverActors.end() || !collect(driver->second)) {
              transitiveInputs.insert(referencedPath);
              valid = false;
            }
          });
        activeDrivers.erase(driverUnit);
        return valid;
      };
      exact = collect(unit) && transitiveInputs.size() == driverInputs.size();
      if (exact)
        for (StringRef input : driverInputs.keys())
          exact &= transitiveInputs.contains(input);
      return exact;
    };
    auto hasDelayedEdgeDependency = [&](Operation *unit) {
      auto isLiteralZeroDelay = [&](Operation *operation) {
        auto delay = dyn_cast<semantic::SVDelayControlOp>(operation);
        SmallVector<Operation *> children =
            delay ? getChildren(delay) : SmallVector<Operation *>{};
        auto literal =
            children.size() == 1
                ? dyn_cast<semantic::SVIntegerLiteralOp>(children.front())
                : semantic::SVIntegerLiteralOp{};
        if (!literal)
          return false;
        FailureOr<ParsedConstant> value = parseSVInteger(
            literal.getConstantValue(), 64, getSemanticLocation(literal));
        return succeeded(value) && value->unknown.isZero() &&
               value->value.isZero();
      };
      llvm::StringSet<> referencedPaths;
      for (Operation *root : getDriverDependencyRoots(unit))
        root->walk([&](Operation *nested) {
          if (auto referenced =
                  nested->getAttrOfType<StringAttr>("referenced_path"))
            referencedPaths.insert(referenced.getValue());
        });
      for (StringRef referenced : referencedPaths.keys()) {
        auto descriptor = descriptors.find(referenced);
        if (descriptor != descriptors.end() && descriptor->second.delayedNet)
          return true;
      }
      for (Operation *source : sourceUnits) {
        if (!isa<semantic::SVProceduralBlockSymbolOp>(source))
          continue;
        bool touchesDependency = false;
        bool containsDelay = false;
        source->walk([&](Operation *nested) {
          if (auto referenced =
                  nested->getAttrOfType<StringAttr>("referenced_path"))
            touchesDependency |=
                referencedPaths.contains(referenced.getValue());
          containsDelay |= (isa<semantic::SVDelayControlOp>(nested) &&
                            !isLiteralZeroDelay(nested)) ||
                           isa<semantic::SVDelay3ControlOp,
                               semantic::SVOneStepDelayControlOp,
                               semantic::SVCycleDelayControlOp>(nested);
        });
        if (touchesDependency && containsDelay)
          return true;
      }
      return false;
    };

    bool allWholeTerminals = llvm::all_of(paths, [](const SimpleTimingPath &p) {
      return p.output.isWhole() &&
             llvm::all_of(p.inputs,
                          [](const TimingTerminal &t) { return t.isWhole(); });
    });
    bool hasExplicitDriverDelay =
        llvm::any_of(spans, [](const TimingDriverSpan &span) {
          return !span.procedural && span.unit->hasAttr("delay_fs");
        });
    bool defaultPulsePolicy =
        llvm::all_of(paths, [](const SimpleTimingPath &p) {
          return p.pulseRejectLimits == p.delays &&
                 p.pulseErrorLimits == p.delays && !p.pulseOnDetect &&
                 !p.pulseShowCancelled;
        });
    if (hasExplicitDriverDelay) {
      emitError(getSemanticLocation(path.declaration))
          << "combining a specify path with an explicitly delayed driver is "
             "not executable yet";
      invalid = true;
      continue;
    }
    if (spans.size() == 1 && spans.front().low == 0 &&
        spans.front().width == path.output.rootWidth && allWholeTerminals &&
        !hasStateDependent && !hasEdgeSensitive && path.delays.size() <= 3 &&
        defaultPulsePolicy && (paths.size() == 1 || identicalDelays) &&
        hasExactInputs(spans.front().unit,
                       identicalDelays && !hasStateDependent)) {
      spans.front().unit->setAttr("delay_fs",
                                  builder.getDenseI64ArrayAttr(path.delays));
      continue;
    }

    struct ProceduralWakeClassification {
      int32_t kind = 0;
      bool ambiguousSourceControl = false;
      Operation *monitorPrimary = nullptr;
    };
    auto classifyProceduralWake = [&](const TimingDriverSpan &span,
                                      const SimpleTimingPath &candidate) {
      ProceduralWakeClassification result;
      auto procedure = cast<semantic::SVProceduralBlockSymbolOp>(span.unit);
      if (procedure.getProcedureKind() ==
              semantic::SVProceduralBlockKind::AlwaysComb ||
          procedure.getProcedureKind() ==
              semantic::SVProceduralBlockKind::AlwaysLatch) {
        // An implicit-sensitive process is sampled only when every path source
        // is an actual dependency of the process. Otherwise a later,
        // unrelated wake could compare against a stale source snapshot.
        llvm::StringSet<> dependencies;
        span.unit->walk([&](Operation *nested) {
          if (auto referenced =
                  nested->getAttrOfType<StringAttr>("referenced_path");
              referenced && !isWrittenResolutionReference(nested))
            dependencies.insert(referenced.getValue());
        });
        bool observesEverySource =
            llvm::all_of(candidate.inputs, [&](const TimingTerminal &input) {
              return dependencies.contains(input.path);
            });
        result.kind = observesEverySource ? 2 : 0;
        return result;
      }

      std::function<Operation *(Operation *)> leadingStatement =
          [&](Operation *statement) -> Operation * {
        if (!isa<semantic::SVBlockStatementOp, semantic::SVStatementListOp>(
                statement))
          return statement;
        SmallVector<Operation *> children = getChildren(statement);
        return children.empty() ? nullptr : leadingStatement(children.front());
      };
      SmallVector<Operation *> body = getChildren(span.unit);
      Operation *leading =
          body.empty() ? nullptr : leadingStatement(body.front());
      SmallVector<Operation *> timedChildren =
          leading ? getChildren(leading) : SmallVector<Operation *>{};
      if (timedChildren.empty())
        return result;
      // IEEE 1800-2017 9.4.2 and 30.4.3: a procedural control may wake
      // more often than the declared module-path edge, but it must cover
      // every transition that can activate that path. Keep this lattice
      // explicit so broader controls use snapshot qualification without an
      // observer, while narrower controls remain diagnosed.
      auto eventCoversPathEdge = [&](int32_t eventEdge) {
        if (candidate.edgeIdentifier == 0)
          return eventEdge == 0;
        if (candidate.edgeIdentifier == 1)
          return eventEdge == 0 || eventEdge == 1 || eventEdge == 3;
        if (candidate.edgeIdentifier == 2)
          return eventEdge == 0 || eventEdge == 2 || eventEdge == 3;
        return eventEdge == 0 || eventEdge == 3;
      };
      auto eventExactlyMatchesPathEdge = [&](int32_t eventEdge) {
        return candidate.edgeIdentifier == eventEdge;
      };
      if (auto list =
              dyn_cast<semantic::SVEventListControlOp>(timedChildren.front())) {
        for (Operation *member : getChildren(list)) {
          auto event = dyn_cast<semantic::SVSignalEventControlOp>(member);
          SmallVector<Operation *> eventChildren =
              event ? getChildren(event) : SmallVector<Operation *>{};
          Operation *primary =
              eventChildren.empty() ? nullptr : eventChildren.front();
          auto directPath =
              primary ? primary->getAttrOfType<StringAttr>("referenced_path")
                      : StringAttr{};
          bool directNamed =
              isa_and_nonnull<semantic::SVNamedValueExpressionOp,
                              semantic::SVHierarchicalValueExpressionOp>(
                  primary);
          int32_t eventEdge =
              event ? static_cast<int32_t>(event.getEdgeKind()) : -1;
          bool edgeMatches = eventCoversPathEdge(eventEdge);
          if (event && candidate.inputs.size() == 1 &&
              candidate.inputs.front().isWhole() && directNamed && directPath &&
              directPath.getValue() == candidate.inputs.front().path &&
              edgeMatches) {
            // IEEE 1800-2017 30.4 qualifies a module path from the declared
            // source transition. Snapshot-on-wake is exact for an event list
            // because the source is itself an independently observed member;
            // wakes from other members produce an empty difference mask.
            result.kind = 2;
            return result;
          }
          bool derivedSource = false;
          if (primary && candidate.inputs.size() == 1)
            primary->walk([&](Operation *nested) {
              auto referenced =
                  nested->getAttrOfType<StringAttr>("referenced_path");
              derivedSource |= referenced && referenced.getValue() ==
                                                 candidate.inputs.front().path;
            });
          if (derivedSource && !isAddressableExpression(primary)) {
            // IEEE 1800-2017 9.4.2 reevaluates a computed primary for each
            // constituent dependency. Monitor the Clause 30.4 source there
            // even when the derived value itself does not wake the writer.
            result.kind = 2;
            result.monitorPrimary = primary;
            return result;
          }
        }
        return result;
      }
      auto event =
          dyn_cast<semantic::SVSignalEventControlOp>(timedChildren.front());
      SmallVector<Operation *> eventChildren =
          event ? getChildren(event) : SmallVector<Operation *>{};
      Operation *primary =
          eventChildren.empty() ? nullptr : eventChildren.front();
      auto directPath =
          primary ? primary->getAttrOfType<StringAttr>("referenced_path")
                  : StringAttr{};
      bool directNamed =
          isa_and_nonnull<semantic::SVNamedValueExpressionOp,
                          semantic::SVHierarchicalValueExpressionOp>(primary);
      int32_t eventEdge =
          event ? static_cast<int32_t>(event.getEdgeKind()) : -1;
      bool sameDirectSource =
          event && candidate.inputs.size() == 1 &&
          candidate.inputs.front().isWhole() && directNamed && directPath &&
          directPath.getValue() == candidate.inputs.front().path;
      bool exactWake =
          sameDirectSource && eventExactlyMatchesPathEdge(eventEdge);
      result.kind = exactWake                                            ? 1
                    : sameDirectSource && eventCoversPathEdge(eventEdge) ? 2
                                                                         : 0;
      timedChildren.front()->walk([&](Operation *nested) {
        auto referenced = nested->getAttrOfType<StringAttr>("referenced_path");
        if (referenced && candidate.inputs.size() == 1 &&
            referenced.getValue() == candidate.inputs.front().path)
          result.ambiguousSourceControl = true;
      });
      result.ambiguousSourceControl &= !sameDirectSource;
      if (result.ambiguousSourceControl) {
        // IEEE 1800-2017 30.4 qualifies from the declared source transition,
        // not from a later change of the derived control. Clause 9.4.2 gives
        // an outlined computed primary an independent reevaluation on every
        // constituent dependency, so it can retain exact source qualification.
        if (primary && !isAddressableExpression(primary)) {
          result.kind = 2;
          result.monitorPrimary = primary;
        } else {
          result.kind = 0;
        }
      }
      return result;
    };

    // At least one direct writer must observe each edge path exactly. Other
    // writers of the same storage are retained as explicit cancellation-only
    // sites so an unqualified procedural write can override a pending path.
    for (const SimpleTimingPath &candidate : paths) {
      if (!candidate.edgeSensitive)
        continue;
      bool hasProceduralSpan = false;
      bool hasQualifyingWake = false;
      for (const TimingDriverSpan &span : spans) {
        uint64_t begin = std::max(span.low, candidate.output.low);
        uint64_t end = std::min(span.low + span.width,
                                candidate.output.low + candidate.output.width);
        if (begin >= end || !span.procedural)
          continue;
        auto procedure = cast<semantic::SVProceduralBlockSymbolOp>(span.unit);
        if (procedure.getProcedureKind() ==
                semantic::SVProceduralBlockKind::Initial ||
            procedure.getProcedureKind() ==
                semantic::SVProceduralBlockKind::Final)
          continue;
        hasProceduralSpan = true;
        ProceduralWakeClassification wake =
            classifyProceduralWake(span, candidate);
        hasQualifyingWake |= wake.kind == 1 || wake.kind == 2;
      }
      if (invalid)
        break;
      if (hasProceduralSpan && !hasQualifyingWake) {
        emitError(getSemanticLocation(candidate.declaration))
            << "edge-sensitive procedural specify path source is not "
               "observed by a direct event control or implicit sensitivity";
        invalid = true;
        break;
      }
    }
    if (invalid)
      continue;

    llvm::DenseSet<Operation *> validatedUnits;
    for (const TimingDriverSpan &span : spans) {
      bool used = llvm::any_of(paths, [&](const SimpleTimingPath &candidate) {
        uint64_t begin = std::max(span.low, candidate.output.low);
        uint64_t end = std::min(span.low + span.width,
                                candidate.output.low + candidate.output.width);
        return begin < end;
      });
      if (!used)
        continue;
      if (!span.nodeID) {
        emitError(getSemanticLocation(path.declaration))
            << "split specify path driver has no stable lvalue identity";
        invalid = true;
        break;
      }
      if (span.procedural) {
        bool invalidWakeShape = false;
        std::function<Operation *(Operation *)> leadingStatement =
            [&](Operation *statement) -> Operation * {
          if (!isa<semantic::SVBlockStatementOp, semantic::SVStatementListOp>(
                  statement))
            return statement;
          SmallVector<Operation *> children = getChildren(statement);
          return children.empty() ? nullptr
                                  : leadingStatement(children.front());
        };
        SmallVector<Operation *> procedureChildren = getChildren(span.unit);
        Operation *leading = procedureChildren.empty()
                                 ? nullptr
                                 : leadingStatement(procedureChildren.front());
        auto procedure = cast<semantic::SVProceduralBlockSymbolOp>(span.unit);
        if (procedure.getProcedureKind() ==
                semantic::SVProceduralBlockKind::Initial ||
            procedure.getProcedureKind() ==
                semantic::SVProceduralBlockKind::Final) {
          emitError(getSemanticLocation(path.declaration))
              << "edge-sensitive specify path requires a recurring direct "
                 "procedural destination writer";
          invalid = true;
          break;
        }
        bool implicit = procedure.getProcedureKind() ==
                            semantic::SVProceduralBlockKind::AlwaysComb ||
                        procedure.getProcedureKind() ==
                            semantic::SVProceduralBlockKind::AlwaysLatch;
        if (!implicit &&
            !isa_and_nonnull<semantic::SVTimedStatementOp>(leading))
          invalidWakeShape = true;
        span.unit->walk([&](semantic::SVTimedStatementOp timed) {
          if (timed == leading)
            return;
          SmallVector<Operation *> children = getChildren(timed);
          if (children.empty() ||
              !isa<semantic::SVDelayControlOp>(children.front()))
            invalidWakeShape = true;
        });
        if (invalidWakeShape) {
          emitError(getSemanticLocation(path.declaration))
              << "edge-sensitive procedural specify path requires one outer "
                 "wake point; nested event controls are not executable yet";
          invalid = true;
          break;
        }
        // A delayed direct writer retains its qualification time in the
        // coroutine frame. Lowering subtracts the elapsed procedural delay
        // from the module-path delay (IEEE 1800-2017 30.4-30.5).
      }
      if (!span.procedural && hasEdgeSensitive &&
          hasDelayedEdgeDependency(span.unit)) {
        emitError(getSemanticLocation(path.declaration))
            << "edge-sensitive specify path has an internally delayed "
               "destination dependency; same-time path qualification cannot "
               "be paired safely";
        invalid = true;
        break;
      }
      if (!span.procedural && validatedUnits.insert(span.unit).second &&
          !hasExactInputs(span.unit, false)) {
        emitError(getSemanticLocation(path.declaration))
            << "simple specify path driver must depend only on its declared "
               "whole inputs";
        invalid = true;
        break;
      }

      llvm::StringMap<std::string> snapshots;
      for (const SimpleTimingPath &candidate : paths)
        for (const TimingTerminal &input : candidate.inputs) {
          if (snapshots.count(input.path))
            continue;
          auto inputDescriptor = descriptors.find(input.path);
          Type snapshotType =
              inputDescriptor == descriptors.end()
                  ? Type{}
                  : sim::getPackedScalarType(inputDescriptor->second.type);
          std::optional<unsigned> descriptorWidth =
              inputDescriptor == descriptors.end()
                  ? std::nullopt
                  : sim::getPackedWidth(inputDescriptor->second.type);
          std::string snapshotPath =
              (output + ".$timing_path_snapshot_" + Twine(*span.nodeID) + "_" +
               Twine(snapshots.size()))
                  .str();
          if (!snapshotType || !descriptorWidth ||
              *descriptorWidth != input.rootWidth ||
              descriptors.count(snapshotPath) || nextStorageId == UINT64_MAX) {
            emitError(getSemanticLocation(candidate.declaration))
                << "specify path source has no unique packed snapshot";
            invalid = true;
            break;
          }
          uint64_t snapshotId = nextStorageId++;
          uint64_t scopeId = getScopeId(span.unit);
          DescriptorInfo snapshot{DescriptorInfo::Kind::Storage, snapshotId,
                                  scopeId, snapshotType,
                                  sim::NetResolutionKind::Wire};
          snapshot.rootType = snapshotType;
          descriptors[snapshotPath] = snapshot;
          sim::SimStorageDeclOp::create(
              builder, getSemanticLocation(candidate.declaration), snapshotId,
              scopeId, snapshotType, sim::Lifetime::Design,
              builder.getStringAttr(snapshotPath),
              builder.getStringAttr("__obelisk_timing_path_snapshot"),
              sim::ComputeObservabilityKindAttr{});
          snapshots.try_emplace(input.path, std::move(snapshotPath));
        }
      if (invalid)
        break;

      llvm::StringMap<int32_t> &groups = frozenTimingGroups[span.unit];
      for (const SimpleTimingPath &candidate : paths) {
        uint64_t intersectionLow = std::max(span.low, candidate.output.low);
        uint64_t intersectionEnd =
            std::min(span.low + span.width,
                     candidate.output.low + candidate.output.width);
        if (intersectionLow >= intersectionEnd)
          continue;
        uint64_t intersectionWidth = intersectionEnd - intersectionLow;
        std::string groupKey;
        groupKey += Twine(output.size()).str();
        groupKey.push_back(':');
        groupKey += output;
        groupKey.push_back(';');
        groupKey += candidate.full ? "F;" : "P;";
        // IEEE 1800-2017 30.4.4.4 permits a simple ifnone path to be the
        // fallback for edge-sensitive conditional paths over the same
        // connection. Edge qualification therefore cannot participate in
        // the condition-group identity; terminal selections and connection
        // kind below still keep unrelated fallback sets disjoint.
        for (const TimingTerminal &input : candidate.inputs) {
          groupKey += Twine(input.path.size()).str();
          groupKey.push_back(':');
          groupKey += input.path;
          groupKey +=
              (":" + Twine(input.low) + ":" + Twine(input.width) + ";").str();
        }
        groupKey += ("->" + Twine(candidate.output.low) + ":" +
                     Twine(candidate.output.width))
                        .str();
        auto [group, inserted] =
            groups.try_emplace(groupKey, static_cast<int32_t>(groups.size()));
        (void)inserted;
        SmallVector<Attribute> inputAttrs;
        SmallVector<Attribute> snapshotAttrs;
        SmallVector<int64_t> inputLows;
        SmallVector<int64_t> inputWidths;
        SmallVector<int64_t> inputLsbs;
        for (const TimingTerminal &input : candidate.inputs) {
          uint64_t inputLow = input.low;
          uint64_t inputWidth = input.width;
          if (!candidate.full && !candidate.edgeSensitive) {
            inputLow += intersectionLow - candidate.output.low;
            inputWidth = intersectionWidth;
          }
          inputAttrs.push_back(builder.getStringAttr(input.path));
          snapshotAttrs.push_back(
              builder.getStringAttr(snapshots.lookup(input.path)));
          inputLows.push_back(static_cast<int64_t>(inputLow));
          inputWidths.push_back(static_cast<int64_t>(inputWidth));
          inputLsbs.push_back(static_cast<int64_t>(input.lsb));
        }
        StringAttr edgePendingPath;
        StringAttr edgeEpochPath;
        if (candidate.edgeSensitive) {
          if (span.width > UINT32_MAX) {
            emitError(getSemanticLocation(candidate.declaration))
                << "edge-sensitive specify path destination is too wide for "
                   "packed qualification state";
            invalid = true;
            break;
          }
          uint64_t stateIndex = frozenTimingRules[span.unit].size();
          std::string prefix = (output + ".$timing_path_edge_" +
                                Twine(*span.nodeID) + "_" + Twine(stateIndex))
                                   .str();
          auto addEdgeState = [&](StringRef suffix, Type type,
                                  StringRef debugName) -> StringAttr {
            std::string statePath = (Twine(prefix) + suffix).str();
            if (descriptors.count(statePath) || nextStorageId == UINT64_MAX)
              return {};
            uint64_t id = nextStorageId++;
            uint64_t scopeId = getScopeId(span.unit);
            DescriptorInfo descriptor{DescriptorInfo::Kind::Storage, id,
                                      scopeId, type,
                                      sim::NetResolutionKind::Wire};
            descriptor.rootType = type;
            descriptors[statePath] = descriptor;
            sim::SimStorageDeclOp::create(
                builder, getSemanticLocation(candidate.declaration), id,
                scopeId, type, sim::Lifetime::Design,
                builder.getStringAttr(statePath),
                builder.getStringAttr(debugName),
                sim::ComputeObservabilityKindAttr{});
            return builder.getStringAttr(statePath);
          };
          edgePendingPath =
              addEdgeState(".$pending", builder.getIntegerType(span.width),
                           "__obelisk_timing_path_edge_pending");
          edgeEpochPath = addEdgeState(".$epoch", builder.getI64Type(),
                                       "__obelisk_timing_path_edge_epoch");
          if (!edgePendingPath || !edgeEpochPath) {
            emitError(getSemanticLocation(candidate.declaration))
                << "edge-sensitive specify path has no unique qualification "
                   "state";
            invalid = true;
            break;
          }
        }
        SmallVector<NamedAttribute> attrs{
            builder.getNamedAttr("inputs", builder.getArrayAttr(inputAttrs)),
            builder.getNamedAttr("snapshots",
                                 builder.getArrayAttr(snapshotAttrs)),
            builder.getNamedAttr("input_lows",
                                 builder.getDenseI64ArrayAttr(inputLows)),
            builder.getNamedAttr("input_widths",
                                 builder.getDenseI64ArrayAttr(inputWidths)),
            builder.getNamedAttr("input_lsbs",
                                 builder.getDenseI64ArrayAttr(inputLsbs)),
            builder.getNamedAttr("output_low", builder.getI64IntegerAttr(
                                                   intersectionLow - span.low)),
            builder.getNamedAttr("output_width",
                                 builder.getI64IntegerAttr(intersectionWidth)),
            builder.getNamedAttr("output_root_width",
                                 builder.getI64IntegerAttr(span.width)),
            builder.getNamedAttr("driver_node_id",
                                 builder.getI64IntegerAttr(*span.nodeID)),
            builder.getNamedAttr("connection_full",
                                 builder.getBoolAttr(candidate.full)),
            builder.getNamedAttr("polarity",
                                 builder.getI32IntegerAttr(candidate.polarity)),
            builder.getNamedAttr("edge_sensitive",
                                 builder.getBoolAttr(candidate.edgeSensitive)),
            builder.getNamedAttr(
                "edge_identifier",
                builder.getI32IntegerAttr(candidate.edgeIdentifier)),
            builder.getNamedAttr("edge_polarity", builder.getI32IntegerAttr(
                                                      candidate.edgePolarity)),
            builder.getNamedAttr(
                "delay_fs", builder.getDenseI64ArrayAttr(candidate.delays)),
            builder.getNamedAttr(
                "pulse_reject_fs",
                builder.getDenseI64ArrayAttr(candidate.pulseRejectLimits)),
            builder.getNamedAttr(
                "pulse_error_fs",
                builder.getDenseI64ArrayAttr(candidate.pulseErrorLimits)),
            builder.getNamedAttr("pulse_on_detect",
                                 builder.getBoolAttr(candidate.pulseOnDetect)),
            builder.getNamedAttr(
                "pulse_show_cancelled",
                builder.getBoolAttr(candidate.pulseShowCancelled)),
            builder.getNamedAttr("condition_kind", builder.getI32IntegerAttr(
                                                       candidate.condition ? 1
                                                       : candidate.ifnone  ? 2
                                                                          : 0)),
            builder.getNamedAttr("condition_group",
                                 builder.getI32IntegerAttr(group->second)),
        };
        Operation *monitorPrimary = nullptr;
        if (candidate.edgeSensitive) {
          attrs.push_back(
              builder.getNamedAttr("edge_pending", edgePendingPath));
          attrs.push_back(builder.getNamedAttr("edge_epoch", edgeEpochPath));
        }
        if (span.procedural) {
          auto descriptor = descriptors.find(output);
          attrs.push_back(builder.getNamedAttr("procedural_storage",
                                               builder.getBoolAttr(true)));
          attrs.push_back(builder.getNamedAttr(
              "path_site_id",
              builder.getI64IntegerAttr(descriptor->second.id)));
          ProceduralWakeClassification wake =
              classifyProceduralWake(span, candidate);
          monitorPrimary = wake.monitorPrimary;
          // Kind 3 is an explicit cancellation-only writer. It never performs
          // snapshot-based edge qualification; the path-level validation above
          // proves that another writer observes this source exactly.
          int32_t wakeKind = wake.kind == 0 ? 3 : wake.kind;
          attrs.push_back(builder.getNamedAttr(
              "procedural_wake_kind", builder.getI32IntegerAttr(wakeKind)));
          attrs.push_back(builder.getNamedAttr(
              "procedural_monitor",
              builder.getBoolAttr(wake.monitorPrimary != nullptr)));
        }
        if (candidate.condition) {
          auto nodeID =
              candidate.condition->getAttrOfType<IntegerAttr>("node_id");
          if (!nodeID) {
            emitError(getSemanticLocation(candidate.declaration))
                << "conditional specify path condition has no stable node ID";
            invalid = true;
            break;
          }
          attrs.push_back(builder.getNamedAttr("condition_node_id", nodeID));
        }
        DictionaryAttr frozenRule = builder.getDictionaryAttr(attrs);
        frozenTimingRules[span.unit].push_back(frozenRule);
        if (monitorPrimary) {
          NamedAttrList monitor;
          for (StringRef name :
               {"inputs", "snapshots", "input_lows", "input_widths",
                "input_lsbs", "output_low", "output_width", "output_root_width",
                "edge_identifier", "edge_pending", "edge_epoch",
                "condition_kind", "condition_group", "condition_node_id"})
            if (Attribute value = frozenRule.get(name))
              monitor.set(name, value);
          SmallVector<Attribute> monitorRules;
          if (auto existing = monitorPrimary->getAttrOfType<ArrayAttr>(
                  "obelisk.timing_path_monitor_rules"))
            llvm::append_range(monitorRules, existing.getValue());
          monitorRules.push_back(builder.getDictionaryAttr(monitor));
          monitorPrimary->setAttr("obelisk.timing_path_monitor_rules",
                                  builder.getArrayAttr(monitorRules));
        }
      }
      if (invalid)
        break;
    }
  }
  for (auto &entry : frozenTimingRules)
    entry.first->setAttr("obelisk.timing_path_rules",
                         builder.getArrayAttr(entry.second));
  if (invalid)
    return abort();

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
    // A delayed declaration assignment has not driven the net when static
    // variable initialization runs, so its literal cannot seed that fold.
    if (!net || net->hasAttr("delay_fs"))
      continue;
    SmallVector<Operation *> initializer = getNetInitializerExpressions(net);
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

  llvm::DenseMap<Operation *, SmallVector<semantic::SVClassTypeOp>>
      classHierarchyCache;
  auto collectClassHierarchy =
      [&](semantic::SVClassTypeOp leaf,
          SmallVectorImpl<semantic::SVClassTypeOp> &hierarchy,
          StringRef purpose) -> LogicalResult {
    llvm::SmallPtrSet<Operation *, 8> visiting;
    std::function<LogicalResult(semantic::SVClassTypeOp,
                                SmallVectorImpl<semantic::SVClassTypeOp> &)>
        collect = [&](semantic::SVClassTypeOp classType,
                      SmallVectorImpl<semantic::SVClassTypeOp> &result)
        -> LogicalResult {
      auto cached = classHierarchyCache.find(classType);
      if (cached != classHierarchyCache.end()) {
        llvm::append_range(result, cached->second);
        return success();
      }
      if (!visiting.insert(classType).second)
        return classType.emitError("randomization class hierarchy is cyclic");
      SmallVector<semantic::SVClassTypeOp> resolved;
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
        if (failed(collect(base->second, resolved)))
          return failure();
      }
      resolved.push_back(classType);
      visiting.erase(classType);
      auto [entry, inserted] =
          classHierarchyCache.try_emplace(classType, std::move(resolved));
      (void)inserted;
      llvm::append_range(result, entry->second);
      return success();
    };
    return collect(leaf, hierarchy);
  };

  struct CompatibleConcreteClass {
    semantic::SVClassTypeOp classType;
    unsigned depth;
  };
  llvm::DenseMap<Operation *, SmallVector<CompatibleConcreteClass>>
      compatibleConcreteClassCache;
  auto getCompatibleConcreteClasses = [&](semantic::SVClassTypeOp declaredClass,
                                          StringRef purpose)
      -> FailureOr<SmallVector<CompatibleConcreteClass>> {
    auto cached = compatibleConcreteClassCache.find(declaredClass);
    if (cached != compatibleConcreteClassCache.end())
      return cached->second;
    SmallVector<CompatibleConcreteClass> compatibleClasses;
    StringRef targetInterface;
    if (declaredClass.getIsInterface())
      targetInterface =
          cast<semantic::ClassHandleType>(declaredClass.getSemanticType())
              .getClassName()
              .getLeafReference();
    for (semantic::SVClassTypeOp candidate : classSources) {
      if (candidate.getIsAbstract() || candidate.getIsInterface())
        continue;
      SmallVector<semantic::SVClassTypeOp> hierarchy;
      if (failed(collectClassHierarchy(candidate, hierarchy, purpose)))
        return failure();
      bool compatible = llvm::is_contained(hierarchy, declaredClass);
      if (!compatible && !targetInterface.empty())
        for (semantic::SVClassTypeOp hierarchyClass : hierarchy) {
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
      if (compatible)
        compatibleClasses.push_back(
            {candidate, static_cast<unsigned>(hierarchy.size())});
    }
    compatibleConcreteClassCache.try_emplace(declaredClass, compatibleClasses);
    return compatibleClasses;
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
  llvm::DenseMap<Operation *, bool> randomizationBehaviorCache;
  auto hasObservableRandomizationBehavior =
      [&](semantic::SVClassTypeOp classType) -> FailureOr<bool> {
    auto cached = randomizationBehaviorCache.find(classType);
    if (cached != randomizationBehaviorCache.end())
      return cached->second;
    SmallVector<semantic::SVClassTypeOp> hierarchy;
    if (failed(collectClassHierarchy(classType, hierarchy,
                                     "randomization behavior analysis")))
      return failure();
    for (semantic::SVClassTypeOp hierarchyClass : hierarchy)
      for (Operation *member : getChildren(hierarchyClass)) {
        if (auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(member);
            property && property.getRandMode() != semantic::SVRandMode::None) {
          randomizationBehaviorCache[classType] = true;
          return true;
        }
        if (auto method = getClassMethod(member);
            method && method.getIsPrePostRandomize().value_or(false) &&
            !method.getIsBuiltin().value_or(false)) {
          randomizationBehaviorCache[classType] = true;
          return true;
        }
      }
    SmallVector<EffectiveConstraintGroup> effectiveConstraints;
    collectEffectiveConstraints(hierarchy, effectiveConstraints);
    bool hasBehavior = !effectiveConstraints.empty();
    randomizationBehaviorCache[classType] = hasBehavior;
    return hasBehavior;
  };

  auto freezeScopeRandomizeContract =
      [&](semantic::SVCallExpressionOp call,
          SmallVector<Operation *> callChildren) -> bool {
    Location location = getSemanticLocation(call);
    uint64_t argumentCount = call.getArgumentCount();
    if (argumentCount > callChildren.size()) {
      emitError(location) << "std::randomize has malformed argument metadata";
      invalid = true;
      return true;
    }
    size_t argumentStart = callChildren.size() - argumentCount;
    ArrayRef<Operation *> argumentNodes =
        ArrayRef(callChildren).drop_front(argumentStart);
    ArrayRef<Operation *> constraintRoots =
        ArrayRef(callChildren).take_front(argumentStart);

    struct ScopeProperty {
      StringAttr path;
      SymbolRefAttr symbol;
      Type type;
      uint64_t width;
      bool isSigned;
      SmallVector<RandomSubdomain> domains;
    };
    SmallVector<ScopeProperty> properties;
    llvm::DenseMap<Operation *, unsigned> randomIndices;
    uint64_t totalWidth = 0;
    for (Operation *argument : argumentNodes) {
      auto assignment = dyn_cast<semantic::SVAssignmentExpressionOp>(argument);
      SmallVector<Operation *> operands =
          assignment ? getChildren(assignment) : SmallVector<Operation *>{};
      Operation *variable = operands.size() == 2 ? operands.front() : nullptr;
      if (!assignment || operands.size() != 2 ||
          !isa<semantic::SVEmptyArgumentExpressionOp>(operands.back()) ||
          !variable ||
          !isa<semantic::SVNamedValueExpressionOp,
               semantic::SVHierarchicalValueExpressionOp>(variable)) {
        emitError(getSemanticLocation(argument))
            << "std::randomize arguments must be variable identifiers";
        invalid = true;
        return true;
      }
      auto reference =
          variable->getAttrOfType<SymbolRefAttr>("referenced_symbol");
      auto path = variable->getAttrOfType<StringAttr>("referenced_path");
      auto semanticTypeAttr =
          variable->getAttrOfType<TypeAttr>("semantic_type");
      auto symbol = reference
                        ? semanticSymbols.find(reference.getLeafReference())
                        : semanticSymbols.end();
      if (!reference || !path || !semanticTypeAttr ||
          symbol == semanticSymbols.end()) {
        emitError(getSemanticLocation(variable))
            << "std::randomize variable does not resolve in its call scope";
        invalid = true;
        return true;
      }
      if (randomIndices.contains(symbol->second)) {
        emitError(getSemanticLocation(variable))
            << "std::randomize lists the same variable more than once";
        invalid = true;
        return true;
      }
      FailureOr<Type> type = getNormalizedSemanticType(variable);
      std::optional<unsigned> width =
          succeeded(type) ? sim::getPackedWidth(*type) : std::nullopt;
      if (failed(type) || !width || *width == 0) {
        emitError(getSemanticLocation(variable))
            << "std::randomize variables must have fixed packed types";
        invalid = true;
        return true;
      }
      if (*width > UINT32_MAX - totalWidth) {
        emitError(location)
            << "the executable std::randomize plan exceeds its 32-bit bit "
               "offset space";
        invalid = true;
        return true;
      }
      SmallVector<RandomSubdomain> domains;
      if (failed(collectRandomSubdomains(semanticTypeAttr.getValue(), 0,
                                         domains,
                                         getSemanticLocation(variable)))) {
        emitError(getSemanticLocation(variable))
            << "std::randomize variable has an unsupported semantic domain";
        invalid = true;
        return true;
      }
      unsigned index = properties.size();
      randomIndices[symbol->second] = index;
      properties.push_back(
          ScopeProperty{path, reference, *type, *width,
                        isSignedSemanticType(semanticTypeAttr.getValue()),
                        std::move(domains)});
      totalWidth += *width;
    }
    if (properties.size() > 64) {
      emitError(location)
          << "the executable std::randomize property boundary is 64";
      invalid = true;
      return true;
    }

    unsigned softConstraintCount = 0;
    for (Operation *root : constraintRoots) {
      if (!isa<semantic::SVConstraintListOp>(root)) {
        emitError(getSemanticLocation(root))
            << "std::randomize has malformed inline constraint metadata";
        invalid = true;
        return true;
      }
      root->walk([&](Operation *nested) {
        if (isa<semantic::SVCallExpressionOp>(nested)) {
          emitError(getSemanticLocation(nested))
              << "constraint functions in std::randomize require a frozen "
                 "scope-function plan";
          invalid = true;
          return;
        }
        if (auto expression =
                dyn_cast<semantic::SVExpressionConstraintOp>(nested)) {
          if (expression.getIsSoft())
            ++softConstraintCount;
          return;
        }
        if (auto solve =
                dyn_cast<semantic::SVSolveBeforeConstraintOp>(nested)) {
          auto solveCount = solve->getAttrOfType<IntegerAttr>("solve_count");
          auto afterCount = solve->getAttrOfType<IntegerAttr>("after_count");
          SmallVector<Operation *> operands = getChildren(solve);
          if (!solveCount || !afterCount ||
              solveCount.getValue().isNegative() ||
              afterCount.getValue().isNegative() ||
              solveCount.getValue().getActiveBits() > 64 ||
              afterCount.getValue().getActiveBits() > 64 ||
              solveCount.getValue().getZExtValue() > operands.size() ||
              afterCount.getValue().getZExtValue() !=
                  operands.size() - solveCount.getValue().getZExtValue()) {
            emitError(getSemanticLocation(solve))
                << "std::randomize solve-before metadata is malformed";
            invalid = true;
            return;
          }
          return;
        }
        if (isa<semantic::SVConstraintListOp,
                semantic::SVImplicationConstraintOp,
                semantic::SVConditionalConstraintOp,
                semantic::SVUniquenessConstraintOp,
                semantic::SVForeachConstraintOp>(nested))
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
      emitError(location)
          << "the executable soft-constraint priority boundary is 64";
      invalid = true;
    }
    if (invalid)
      return true;

    for (Operation *root : constraintRoots) {
      root->walk([&](Operation *nested) {
        auto reference =
            nested->getAttrOfType<SymbolRefAttr>("referenced_symbol");
        if (!reference)
          return;
        auto symbol = semanticSymbols.find(reference.getLeafReference());
        if (symbol == semanticSymbols.end())
          return;
        auto random = randomIndices.find(symbol->second);
        if (random != randomIndices.end()) {
          nested->setAttr(randomVariableAttrName,
                          builder.getI32IntegerAttr(random->second));
          return;
        }
        if (isa<semantic::SVParameterSymbolOp, semantic::SVEnumValueSymbolOp,
                semantic::SVSpecparamSymbolOp>(symbol->second))
          if (auto constant =
                  symbol->second->getAttrOfType<StringAttr>("constant_value"))
            nested->setAttr("obelisk_sim.constant_value", constant);
      });
    }

    SmallVector<Attribute> propertyAttrs;
    propertyAttrs.reserve(properties.size());
    for (auto [index, property] : llvm::enumerate(properties)) {
      SmallVector<NamedAttribute> attributes{
          builder.getNamedAttr("type", TypeAttr::get(property.type)),
          builder.getNamedAttr("width",
                               builder.getI64IntegerAttr(property.width)),
          builder.getNamedAttr(randomPropertyModeIndexAttrName,
                               builder.getI32IntegerAttr(index)),
          builder.getNamedAttr("is_signed",
                               builder.getBoolAttr(property.isSigned)),
          builder.getNamedAttr("is_randc", builder.getBoolAttr(false)),
          builder.getNamedAttr(randomPropertyPathAttrName, property.path),
          builder.getNamedAttr(randomPropertySymbolAttrName, property.symbol),
      };
      if (!property.domains.empty()) {
        SmallVector<Attribute> domains;
        for (const RandomSubdomain &domain : property.domains) {
          SmallVector<Attribute> patterns;
          for (const RandomDomainPattern &pattern : domain.patterns)
            patterns.push_back(builder.getDictionaryAttr({
                builder.getNamedAttr(
                    "mask", builder.getIntegerAttr(builder.getI64Type(),
                                                   APInt(64, pattern.mask))),
                builder.getNamedAttr(
                    "value", builder.getIntegerAttr(builder.getI64Type(),
                                                    APInt(64, pattern.value))),
            }));
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
    call->setAttr(randomizeScopeAttrName, builder.getUnitAttr());
    call->setAttr(randomizeExplicitPropertiesAttrName, builder.getUnitAttr());
    if (properties.empty())
      call->setAttr(randomizeCheckerOnlyAttrName, builder.getUnitAttr());
    call->setAttr(randomPropertiesAttrName,
                  builder.getArrayAttr(propertyAttrs));
    call->setAttr(randomContainerPropertiesAttrName, builder.getArrayAttr({}));
    call->setAttr(randomNestedConstraintModesAttrName,
                  builder.getArrayAttr({}));
    call->setAttr(randomNestedHooksAttrName, builder.getArrayAttr({}));
    call->setAttr(randomRecursiveAliasGuardsAttrName, builder.getArrayAttr({}));
    call->setAttr(randomTotalWidthAttrName,
                  builder.getI64IntegerAttr(totalWidth));
    call->setAttr(randomConstraintCountAttrName, builder.getI32IntegerAttr(0));
    call->setAttr(constraintModeStaticStoragesAttrName,
                  builder.getDenseI64ArrayAttr({}));

    for (Operation *argument : argumentNodes)
      argument->erase();
    call->setAttr("argument_count", builder.getI64IntegerAttr(0));
    call->setAttr("defaulted_arguments", builder.getDenseI64ArrayAttr({}));
    return true;
  };

  std::function<bool(semantic::SVCallExpressionOp)> freezeRandomizeContract;
  freezeRandomizeContract = [&](semantic::SVCallExpressionOp call) -> bool {
    if (!call.getIsSystemCall() || call.getCalleeName() != "randomize")
      return false;
    if (call->hasAttr(randomizeAttrName) ||
        call->hasAttr(randomizeDispatchAttrName) ||
        call->hasAttr(randomizeNestedDispatchAttrName))
      return true;
    SmallVector<Operation *> callChildren = getChildren(call);
    uint64_t argumentCount = call.getArgumentCount();
    bool hasInlineConstraints = call.getHasInlineConstraints();
    if (argumentCount == 0)
      return freezeScopeRandomizeContract(call, std::move(callChildren));
    if (argumentCount > callChildren.size()) {
      emitError(getSemanticLocation(call))
          << "randomize call has malformed argument metadata";
      invalid = true;
      return true;
    }
    unsigned receiverIndex =
        static_cast<unsigned>(callChildren.size() - argumentCount);
    // Slang represents every std::randomize variable argument as an
    // assignment-expression wrapper, whereas an object method call starts
    // with the synthesized class-handle receiver. Check the syntax shape
    // before the semantic type so std::randomize(class_handle_variable) is
    // never mistaken for class_handle_variable.randomize().
    if (isa<semantic::SVAssignmentExpressionOp>(callChildren[receiverIndex]))
      return freezeScopeRandomizeContract(call, std::move(callChildren));
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
      return freezeScopeRandomizeContract(call, std::move(callChildren));
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
    // constraint set even though randomize itself is a builtin.
    if (!call->hasAttr(randomizePlanClassAttrName)) {
      FailureOr<SmallVector<CompatibleConcreteClass>> dynamicPlans =
          getCompatibleConcreteClasses(foundClass->second,
                                       "randomization dispatch");
      if (failed(dynamicPlans)) {
        invalid = true;
        return true;
      }
      llvm::sort(*dynamicPlans, [&](const CompatibleConcreteClass &lhs,
                                    const CompatibleConcreteClass &rhs) {
        if (lhs.depth != rhs.depth)
          return lhs.depth > rhs.depth;
        return classSymbols.lookup(lhs.classType).getValue() <
               classSymbols.lookup(rhs.classType).getValue();
      });
      {
        SmallVector<semantic::SVCallExpressionOp> alternatives;
        alternatives.reserve(dynamicPlans->size());
        // Clone every raw call before inserting any clone into the source call;
        // otherwise later clones would recursively contain earlier plans.
        for (const CompatibleConcreteClass &plan : *dynamicPlans) {
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

    struct RandomObjectPathElement {
      FlatSymbolRefAttr field;
      Type concreteType;
      Type storageType;
      unsigned modeIndex;
    };
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
      SmallVector<RandomObjectPathElement> nestedObjectPath;
    };
    struct RandomContainerProperty {
      Operation *source;
      FlatSymbolRefAttr field;
      Type type;
      Type elementType;
      unsigned elementWidth;
      unsigned modeIndex;
      FlatSymbolRefAttr nestedObjectField;
      Type nestedObjectType;
      Type nestedObjectStorageType;
      unsigned nestedModeIndex;
      SmallVector<RandomObjectPathElement> nestedObjectPath;
      bool inertClassHandles;
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
      SmallVector<RandomObjectPathElement> nestedObjectPath;
    };
    struct RecursiveAliasGuard {
      FlatSymbolRefAttr field;
      Type concreteType;
      Type storageType;
      unsigned outerModeIndex;
      SmallVector<RandomObjectPathElement> path;
      unsigned aliasDepth;
    };
    SmallVector<RandomProperty, 0> properties;
    SmallVector<RandomContainerProperty> containerProperties;
    SmallVector<NestedObjectPlan, 0> nestedObjectPlans;
    SmallVector<RecursiveAliasGuard, 0> recursiveAliasGuards;
    SmallVector<Operation *> constraintRoots;
    SmallVector<EffectiveConstraintGroup> constraintGroups;
    semantic::SVSubroutineSymbolOp preRandomizeHook;
    semantic::SVSubroutineSymbolOp postRandomizeHook;
    auto hasConcreteClassCandidate =
        [&](semantic::ClassHandleType handleType) -> FailureOr<bool> {
      auto declaredClass =
          semanticClasses.find(handleType.getClassName().getLeafReference());
      if (declaredClass == semanticClasses.end())
        return failure();
      FailureOr<SmallVector<CompatibleConcreteClass>> candidates =
          getCompatibleConcreteClasses(declaredClass->second,
                                       "class-handle container analysis");
      if (failed(candidates))
        return failure();
      return !candidates->empty();
    };
    auto isInertClassHandleContainer =
        [&](Type semanticContainerType,
            Type loweredElementType) -> FailureOr<bool> {
      if (!isa<sim::ClassHandleType>(loweredElementType))
        return false;
      Type semanticElementType;
      if (auto array = dyn_cast<semantic::DynArrayType>(semanticContainerType))
        semanticElementType = array.getElementType();
      else if (auto queue =
                   dyn_cast<semantic::QueueType>(semanticContainerType))
        semanticElementType = queue.getElementType();
      auto elementHandle =
          dyn_cast<semantic::ClassHandleType>(semanticElementType);
      if (!elementHandle)
        return failure();
      FailureOr<bool> hasCandidate = hasConcreteClassCandidate(elementHandle);
      if (failed(hasCandidate))
        return failure();
      return !*hasCandidate;
    };
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
            Type elementType =
                isa<sim::DynamicArrayType>(*type)
                    ? cast<sim::DynamicArrayType>(*type).getElementType()
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
            if (isa<sim::ClassHandleType>(elementType)) {
              FailureOr<bool> inert = isInertClassHandleContainer(
                  property.getSemanticType().value_or(Type{}), elementType);
              if (failed(inert)) {
                emitError(getSemanticLocation(property))
                    << "random class-handle container has no analyzable "
                       "element class";
                invalid = true;
                continue;
              }
              if (*inert) {
                containerProperties.push_back({property,
                                               field,
                                               *type,
                                               elementType,
                                               0,
                                               modeIndex,
                                               {},
                                               {},
                                               {},
                                               0,
                                               {},
                                               true});
                continue;
              }
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
            containerProperties.push_back({property, field, *type, elementType,
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
                    ? semanticClasses.find(
                          semanticObjectType.getClassName().getLeafReference())
                    : semanticClasses.end();
            SmallVector<semantic::SVClassTypeOp> concreteClasses;
            if (declaredClass != semanticClasses.end()) {
              FailureOr<SmallVector<CompatibleConcreteClass>> candidates =
                  getCompatibleConcreteClasses(declaredClass->second,
                                               "nested object randomization");
              if (failed(candidates)) {
                invalid = true;
                return true;
              }
              for (const CompatibleConcreteClass &candidate : *candidates)
                concreteClasses.push_back(candidate.classType);
            }
            size_t unfilteredConcreteClassCount = concreteClasses.size();
            llvm::erase_if(concreteClasses,
                           [&](semantic::SVClassTypeOp candidate) {
                             FailureOr<bool> hasBehavior =
                                 hasObservableRandomizationBehavior(candidate);
                             if (failed(hasBehavior)) {
                               invalid = true;
                               return false;
                             }
                             return !*hasBehavior;
                           });
            if (invalid)
              return true;
            bool hasBehaviorlessCandidate =
                concreteClasses.size() != unfilteredConcreteClassCount;
            // A non-null object with no rand state, active constraints, or
            // lifecycle hooks contributes exactly the same singleton solution
            // as a null rand handle. It therefore belongs to the default
            // branch and must not multiply the active-object plan space.
            if (concreteClasses.empty())
              continue;
            DictionaryAttr selectedPlan;
            if (auto selectedPlans = call->getAttrOfType<ArrayAttr>(
                    randomizeNestedPlansAttrName))
              for (Attribute selectedAttr : selectedPlans)
                if (auto selected = dyn_cast<DictionaryAttr>(selectedAttr);
                    selected && !selected.get("path") &&
                    selected.getAs<FlatSymbolRefAttr>("field") == field) {
                  selectedPlan = selected;
                  break;
                }
            bool selectedProperty = static_cast<bool>(selectedPlan);
            if ((concreteClasses.size() != 1 || hasBehaviorlessCandidate) &&
                !selectedProperty) {
              struct NestedDynamicPlan {
                semantic::SVClassTypeOp classType;
                unsigned depth;
              };
              SmallVector<NestedDynamicPlan> dynamicPlans;
              for (semantic::SVClassTypeOp candidate : concreteClasses) {
                SmallVector<semantic::SVClassTypeOp> candidateHierarchy;
                if (failed(collectClassHierarchy(
                        candidate, candidateHierarchy,
                        "nested object randomization dispatch"))) {
                  invalid = true;
                  return true;
                }
                dynamicPlans.push_back(
                    {candidate,
                     static_cast<unsigned>(candidateHierarchy.size())});
              }
              llvm::sort(dynamicPlans, [&](const NestedDynamicPlan &lhs,
                                           const NestedDynamicPlan &rhs) {
                if (lhs.depth != rhs.depth)
                  return lhs.depth > rhs.depth;
                return classSymbols.lookup(lhs.classType).getValue() <
                       classSymbols.lookup(rhs.classType).getValue();
              });
              SmallVector<semantic::SVCallExpressionOp> alternatives;
              auto addSelection = [&](semantic::SVCallExpressionOp alternative,
                                      Attribute dynamicClass) {
                SmallVector<Attribute> selections;
                if (auto existing = alternative->getAttrOfType<ArrayAttr>(
                        randomizeNestedPlansAttrName))
                  llvm::append_range(selections, existing);
                SmallVector<NamedAttribute> attributes{
                    builder.getNamedAttr("field", field)};
                if (dynamicClass)
                  attributes.push_back(
                      builder.getNamedAttr("class", dynamicClass));
                else
                  attributes.push_back(
                      builder.getNamedAttr("null", builder.getUnitAttr()));
                selections.push_back(builder.getDictionaryAttr(attributes));
                alternative->setAttr(randomizeNestedPlansAttrName,
                                     builder.getArrayAttr(selections));
              };
              for (const NestedDynamicPlan &plan : dynamicPlans) {
                auto alternative =
                    cast<semantic::SVCallExpressionOp>(call->clone());
                addSelection(
                    alternative,
                    FlatSymbolRefAttr::get(
                        context,
                        classSymbols.lookup(plan.classType).getValue()));
                alternatives.push_back(alternative);
              }
              auto nullAlternative =
                  cast<semantic::SVCallExpressionOp>(call->clone());
              addSelection(nullAlternative, {});
              alternatives.push_back(nullAlternative);
              OpBuilder alternativeBuilder =
                  OpBuilder::atBlockEnd(&call->getRegion(0).front());
              for (semantic::SVCallExpressionOp alternative : alternatives) {
                alternativeBuilder.insert(alternative);
                freezeRandomizeContract(alternative);
              }
              call->setAttr(randomizeNestedDispatchAttrName,
                            builder.getUnitAttr());
              call->setAttr(randomizeNestedDispatchFieldAttrName, field);
              call->setAttr(randomizeNestedDispatchStorageAttrName,
                            TypeAttr::get(objectType));
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
            if (selectedProperty && selectedPlan.get("null"))
              continue;
            if (selectedProperty) {
              auto selectedClass =
                  selectedPlan.getAs<FlatSymbolRefAttr>("class");
              auto selected = llvm::find_if(
                  concreteClasses, [&](semantic::SVClassTypeOp candidate) {
                    return selectedClass &&
                           classSymbols.lookup(candidate).getValue() ==
                               selectedClass.getValue();
                  });
              if (selected == concreteClasses.end()) {
                emitError(getSemanticLocation(property))
                    << "nested randomization alternative has an invalid "
                       "dynamic class";
                invalid = true;
                continue;
              }
              concreteClasses.assign(1, *selected);
            }
            if (concreteClasses.empty()) {
              emitError(getSemanticLocation(property))
                  << "rand object handle has no concrete dynamic class in "
                     "the closed-world hierarchy";
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
            SmallVector<EffectiveConstraintGroup> nestedEffectiveConstraints;
            collectEffectiveConstraints(nestedHierarchy,
                                        nestedEffectiveConstraints);
            unsigned nestedModeIndex = 0;
            semantic::SVSubroutineSymbolOp nestedPreHook;
            semantic::SVSubroutineSymbolOp nestedPostHook;
            Type rootNestedConcreteType = sim::ClassHandleType::get(
                context,
                FlatSymbolRefAttr::get(
                    context,
                    classSymbols.lookup(concreteClasses.front()).getValue()));
            bool recursiveDispatchCreated = false;
            // IEEE 1800 active random objects are solved as one object graph;
            // recursively flatten eligible descendant leaves into this plan
            // instead of emitting sequential child randomize calls. Dynamic
            // dispatch and runtime object identity keep distinct paths to an
            // aliased object coherent. A recursive type cycle still needs a
            // runtime graph walk because static path expansion cannot know
            // whether the next edge is an alias or a fresh object.
            std::function<LogicalResult(semantic::SVClassPropertySymbolOp, Type,
                                        unsigned,
                                        SmallVector<RandomObjectPathElement>,
                                        llvm::SmallPtrSet<Operation *, 8>)>
                collectRecursiveObject;
            collectRecursiveObject =
                [&](semantic::SVClassPropertySymbolOp edgeProperty,
                    Type edgeStorageType, unsigned edgeModeIndex,
                    SmallVector<RandomObjectPathElement> path,
                    llvm::SmallPtrSet<Operation *, 8> ancestors)
                -> LogicalResult {
              auto semanticObjectType = dyn_cast<semantic::ClassHandleType>(
                  edgeProperty.getSemanticType().value_or(Type{}));
              auto declaredClass =
                  semanticObjectType
                      ? semanticClasses.find(semanticObjectType.getClassName()
                                                 .getLeafReference())
                      : semanticClasses.end();
              SmallVector<semantic::SVClassTypeOp> candidates;
              if (declaredClass != semanticClasses.end()) {
                FailureOr<SmallVector<CompatibleConcreteClass>> compatible =
                    getCompatibleConcreteClasses(
                        declaredClass->second,
                        "recursive nested object randomization");
                if (failed(compatible))
                  return failure();
                for (const CompatibleConcreteClass &candidate : *compatible)
                  candidates.push_back(candidate.classType);
              }
              SmallVector<Attribute> selectionPath{Attribute(field)};
              for (const RandomObjectPathElement &element : path)
                selectionPath.push_back(element.field);
              FlatSymbolRefAttr edgeField =
                  classFieldSymbols.lookup(edgeProperty);
              selectionPath.push_back(edgeField);
              ArrayAttr selectionPathAttr = builder.getArrayAttr(selectionPath);
              DictionaryAttr selectedPlan;
              if (auto selectedPlans = call->getAttrOfType<ArrayAttr>(
                      randomizeNestedPlansAttrName))
                for (Attribute selectedAttr : selectedPlans)
                  if (auto selected = dyn_cast<DictionaryAttr>(selectedAttr);
                      selected &&
                      selected.getAs<ArrayAttr>("path") == selectionPathAttr) {
                    selectedPlan = selected;
                    break;
                  }
              if (candidates.empty() && !selectedPlan) {
                emitError(getSemanticLocation(edgeProperty))
                    << "recursive rand object handle has no concrete "
                       "closed-world dynamic class";
                return failure();
              }
              if (candidates.size() != 1 && !selectedPlan) {
                struct RecursiveDynamicPlan {
                  semantic::SVClassTypeOp classType;
                  unsigned depth;
                };
                SmallVector<RecursiveDynamicPlan> dynamicPlans;
                for (semantic::SVClassTypeOp candidate : candidates) {
                  SmallVector<semantic::SVClassTypeOp> candidateHierarchy;
                  if (failed(collectClassHierarchy(
                          candidate, candidateHierarchy,
                          "recursive nested object dispatch")))
                    return failure();
                  // Null and a concrete object with no rand state,
                  // constraints, or lifecycle hooks have identical recursive
                  // randomization behavior: they contribute no variables or
                  // predicates and invoke no callbacks. Share one default
                  // dispatch alternative for every such concrete class.
                  FailureOr<bool> hasBehavior =
                      hasObservableRandomizationBehavior(candidate);
                  if (failed(hasBehavior))
                    return failure();
                  if (!*hasBehavior)
                    continue;
                  dynamicPlans.push_back(
                      {candidate,
                       static_cast<unsigned>(candidateHierarchy.size())});
                }
                llvm::sort(dynamicPlans, [&](const RecursiveDynamicPlan &lhs,
                                             const RecursiveDynamicPlan &rhs) {
                  if (lhs.depth != rhs.depth)
                    return lhs.depth > rhs.depth;
                  return classSymbols.lookup(lhs.classType).getValue() <
                         classSymbols.lookup(rhs.classType).getValue();
                });
                SmallVector<semantic::SVCallExpressionOp> alternatives;
                auto addSelection =
                    [&](semantic::SVCallExpressionOp alternative,
                        Attribute dynamicClass) {
                      SmallVector<Attribute> selections;
                      if (auto existing = alternative->getAttrOfType<ArrayAttr>(
                              randomizeNestedPlansAttrName))
                        llvm::append_range(selections, existing);
                      SmallVector<NamedAttribute> attributes{
                          builder.getNamedAttr("field", field),
                          builder.getNamedAttr("path", selectionPathAttr),
                      };
                      if (dynamicClass)
                        attributes.push_back(
                            builder.getNamedAttr("class", dynamicClass));
                      else
                        attributes.push_back(builder.getNamedAttr(
                            "null", builder.getUnitAttr()));
                      selections.push_back(
                          builder.getDictionaryAttr(attributes));
                      alternative->setAttr(randomizeNestedPlansAttrName,
                                           builder.getArrayAttr(selections));
                    };
                for (const RecursiveDynamicPlan &plan : dynamicPlans) {
                  auto alternative =
                      cast<semantic::SVCallExpressionOp>(call->clone());
                  addSelection(
                      alternative,
                      FlatSymbolRefAttr::get(
                          context,
                          classSymbols.lookup(plan.classType).getValue()));
                  alternatives.push_back(alternative);
                }
                auto nullAlternative =
                    cast<semantic::SVCallExpressionOp>(call->clone());
                addSelection(nullAlternative, {});
                alternatives.push_back(nullAlternative);
                OpBuilder alternativeBuilder =
                    OpBuilder::atBlockEnd(&call->getRegion(0).front());
                for (semantic::SVCallExpressionOp alternative : alternatives) {
                  alternativeBuilder.insert(alternative);
                  freezeRandomizeContract(alternative);
                }
                SmallVector<Attribute> dispatchPath;
                dispatchPath.push_back(builder.getDictionaryAttr({
                    builder.getNamedAttr("field", field),
                    builder.getNamedAttr("concrete_type",
                                         TypeAttr::get(rootNestedConcreteType)),
                    builder.getNamedAttr("storage_type",
                                         TypeAttr::get(objectType)),
                }));
                for (const RandomObjectPathElement &element : path)
                  dispatchPath.push_back(builder.getDictionaryAttr({
                      builder.getNamedAttr("field", element.field),
                      builder.getNamedAttr("concrete_type",
                                           TypeAttr::get(element.concreteType)),
                      builder.getNamedAttr("storage_type",
                                           TypeAttr::get(element.storageType)),
                  }));
                dispatchPath.push_back(builder.getDictionaryAttr({
                    builder.getNamedAttr("field", edgeField),
                    builder.getNamedAttr("storage_type",
                                         TypeAttr::get(edgeStorageType)),
                }));
                call->setAttr(randomizeNestedDispatchAttrName,
                              builder.getUnitAttr());
                call->setAttr(randomizeNestedDispatchFieldAttrName, field);
                call->setAttr(randomizeNestedDispatchStorageAttrName,
                              TypeAttr::get(objectType));
                call->setAttr(randomizeNestedDispatchPathAttrName,
                              builder.getArrayAttr(dispatchPath));
                call->setAttr(randomizeNestedDispatchSelectionPathAttrName,
                              selectionPathAttr);
                call->setAttr(randomReceiverIndexAttrName,
                              builder.getI32IntegerAttr(receiverIndex));
                if (explicitPropertyList) {
                  for (Operation *argument : explicitPropertyArguments)
                    argument->erase();
                  call->setAttr("argument_count", builder.getI64IntegerAttr(1));
                  call->setAttr("defaulted_arguments",
                                builder.getDenseI64ArrayAttr({0}));
                }
                recursiveDispatchCreated = true;
                return failure();
              }
              if (selectedPlan && selectedPlan.get("null"))
                return success();
              if (selectedPlan) {
                auto selectedClass =
                    selectedPlan.getAs<FlatSymbolRefAttr>("class");
                auto selected = llvm::find_if(
                    candidates, [&](semantic::SVClassTypeOp candidate) {
                      return selectedClass &&
                             classSymbols.lookup(candidate).getValue() ==
                                 selectedClass.getValue();
                    });
                if (selected == candidates.end()) {
                  emitError(getSemanticLocation(edgeProperty))
                      << "recursive nested randomization alternative has an "
                         "invalid dynamic class";
                  return failure();
                }
                candidates.assign(1, *selected);
              }
              if (candidates.empty()) {
                emitError(getSemanticLocation(edgeProperty))
                    << "recursive rand object handle has no concrete "
                       "closed-world dynamic class";
                return failure();
              }
              semantic::SVClassTypeOp concreteClass = candidates.front();
              Type concreteType = sim::ClassHandleType::get(
                  context,
                  FlatSymbolRefAttr::get(
                      context, classSymbols.lookup(concreteClass).getValue()));
              if (!ancestors.insert(concreteClass).second) {
                // IEEE 1800-2017 18.5.9 defines the active random objects as
                // a set. A recursive edge that is null contributes nothing;
                // one that closes onto an already planned ancestor must not
                // duplicate that object's variables, constraints, or hooks.
                // Freeze a runtime identity guard instead of recursively
                // expanding the class type forever. A distinct object at the
                // repeated type is diagnosed at runtime until the fully
                // dynamic graph planner can instantiate another plan node.
                unsigned aliasDepth = 0;
                if (concreteType != rootNestedConcreteType) {
                  auto alias = llvm::find_if(
                      path, [&](const RandomObjectPathElement &element) {
                        return element.concreteType == concreteType;
                      });
                  if (alias == path.end()) {
                    emitError(getSemanticLocation(edgeProperty))
                        << "recursive rand object cycle has no matching "
                           "ancestor plan";
                    return failure();
                  }
                  aliasDepth = static_cast<unsigned>(
                                   std::distance(path.begin(), alias)) +
                               1;
                }
                path.push_back({classFieldSymbols.lookup(edgeProperty),
                                concreteType, edgeStorageType, edgeModeIndex});
                recursiveAliasGuards.push_back({field, rootNestedConcreteType,
                                                objectType, modeIndex,
                                                std::move(path), aliasDepth});
                return success();
              }
              SmallVector<semantic::SVClassTypeOp> recursiveHierarchy;
              if (failed(collectClassHierarchy(
                      concreteClass, recursiveHierarchy,
                      "recursive nested object randomization")))
                return failure();
              SmallVector<EffectiveConstraintGroup> recursiveConstraints;
              collectEffectiveConstraints(recursiveHierarchy,
                                          recursiveConstraints);
              semantic::SVSubroutineSymbolOp recursivePreHook;
              semantic::SVSubroutineSymbolOp recursivePostHook;
              for (semantic::SVClassTypeOp recursiveClass : recursiveHierarchy)
                for (Operation *member : getChildren(recursiveClass))
                  if (auto method = getClassMethod(member);
                      method &&
                      method.getIsPrePostRandomize().value_or(false) &&
                      !method.getIsBuiltin().value_or(false)) {
                    StringRef name = method.getName().value_or("");
                    std::optional<Type> methodType = method.getSemanticType();
                    auto subroutineType =
                        methodType
                            ? dyn_cast<semantic::SubroutineType>(*methodType)
                            : semantic::SubroutineType{};
                    auto signature = subroutineType
                                         ? dyn_cast<FunctionType>(
                                               subroutineType.getSignature())
                                         : FunctionType{};
                    if (method.getIsStatic().value_or(false) ||
                        method.getSubroutineKind() !=
                            semantic::SVSubroutineKind::Function ||
                        !signature || signature.getNumInputs() != 0 ||
                        signature.getNumResults() != 1 ||
                        !isa<semantic::VoidType>(signature.getResult(0))) {
                      emitError(getSemanticLocation(method))
                          << "recursive randomization hooks must be void "
                             "instance functions without arguments";
                      return failure();
                    }
                    if (name == "pre_randomize")
                      recursivePreHook = method;
                    else if (name == "post_randomize")
                      recursivePostHook = method;
                    else {
                      emitError(getSemanticLocation(method))
                          << "unknown recursive randomization lifecycle hook "
                          << name;
                      return failure();
                    }
                  }
              path.push_back({classFieldSymbols.lookup(edgeProperty),
                              concreteType, edgeStorageType, edgeModeIndex});
              if (!recursiveConstraints.empty() || recursivePreHook ||
                  recursivePostHook) {
                NestedObjectPlan recursivePlan{edgeProperty,
                                               field,
                                               rootNestedConcreteType,
                                               objectType,
                                               recursiveHierarchy,
                                               std::move(recursiveConstraints),
                                               {},
                                               modeIndex,
                                               recursivePreHook,
                                               recursivePostHook,
                                               path};
                nestedObjectPlans.push_back(std::move(recursivePlan));
              }
              unsigned recursiveModeIndex = 0;
              for (semantic::SVClassTypeOp recursiveClass :
                   recursiveHierarchy) {
                for (Operation *member : getChildren(recursiveClass)) {
                  auto recursiveProperty =
                      dyn_cast<semantic::SVClassPropertySymbolOp>(member);
                  if (!recursiveProperty || recursiveProperty.getRandMode() ==
                                                semantic::SVRandMode::None)
                    continue;
                  unsigned leafModeIndex = recursiveModeIndex++;
                  if (leafModeIndex >= 64) {
                    emitError(getSemanticLocation(recursiveProperty))
                        << "recursive rand object exceeds the 64-property "
                           "rand_mode boundary";
                    return failure();
                  }
                  FailureOr<Type> recursiveType =
                      getNormalizedSemanticType(recursiveProperty);
                  std::optional<Type> semanticRecursiveType =
                      recursiveProperty.getSemanticType();
                  if (failed(recursiveType) || !semanticRecursiveType ||
                      recursiveProperty.getRandMode() ==
                          semantic::SVRandMode::RandC ||
                      recursiveProperty.getLifetime() ==
                          semantic::SVVariableLifetime::Static) {
                    emitError(getSemanticLocation(recursiveProperty))
                        << "recursive rand object properties must be "
                           "non-static rand values";
                    return failure();
                  }
                  if (isa<sim::DynamicArrayType, sim::QueueType>(
                          *recursiveType)) {
                    Type elementType =
                        isa<sim::DynamicArrayType>(*recursiveType)
                            ? cast<sim::DynamicArrayType>(*recursiveType)
                                  .getElementType()
                            : cast<sim::QueueType>(*recursiveType)
                                  .getElementType();
                    FailureOr<bool> inert = isInertClassHandleContainer(
                        *semanticRecursiveType, elementType);
                    if (failed(inert)) {
                      emitError(getSemanticLocation(recursiveProperty))
                          << "recursive random class-handle container has no "
                             "analyzable element class";
                      return failure();
                    }
                    if (*inert) {
                      containerProperties.push_back(
                          {recursiveProperty,
                           classFieldSymbols.lookup(recursiveProperty),
                           *recursiveType, elementType, 0, modeIndex, field,
                           rootNestedConcreteType, objectType, leafModeIndex,
                           path, true});
                      continue;
                    }
                    std::optional<unsigned> elementWidth =
                        sim::getPackedWidth(elementType);
                    if (!elementWidth || *elementWidth == 0 ||
                        *elementWidth > 64) {
                      emitError(getSemanticLocation(recursiveProperty))
                          << "recursive random dynamic containers require "
                             "packed integral elements no wider than 64 bits";
                      return failure();
                    }
                    containerProperties.push_back(
                        {recursiveProperty,
                         classFieldSymbols.lookup(recursiveProperty),
                         *recursiveType, elementType, *elementWidth, modeIndex,
                         field, rootNestedConcreteType, objectType,
                         leafModeIndex, path});
                    continue;
                  }
                  if (isa<sim::ClassHandleType>(*recursiveType)) {
                    if (failed(collectRecursiveObject(
                            recursiveProperty, *recursiveType, leafModeIndex,
                            path, ancestors)))
                      return failure();
                    continue;
                  }
                  std::optional<unsigned> width =
                      sim::getPackedWidth(*recursiveType);
                  if (!width || *width == 0) {
                    emitError(getSemanticLocation(recursiveProperty))
                        << "recursive rand object leaves must be packed "
                           "integral values";
                    return failure();
                  }
                  SmallVector<RandomSubdomain> domains;
                  if (failed(collectRandomSubdomains(
                          *semanticRecursiveType, 0, domains,
                          getSemanticLocation(recursiveProperty))))
                    return failure();
                  properties.push_back(
                      {recursiveProperty,
                       classFieldSymbols.lookup(recursiveProperty),
                       {},
                       *recursiveType,
                       *width,
                       modeIndex,
                       false,
                       {},
                       0,
                       false,
                       field,
                       rootNestedConcreteType,
                       objectType,
                       leafModeIndex,
                       isSignedSemanticType(*semanticRecursiveType),
                       false,
                       {},
                       {},
                       {},
                       {},
                       {},
                       std::move(domains),
                       path});
                }
              }
              return success();
            };
            llvm::SmallPtrSet<Operation *, 8> nestedAncestors;
            nestedAncestors.insert(concreteClasses.front());
            for (semantic::SVClassTypeOp nestedClass : nestedHierarchy) {
              for (Operation *nestedMember : getChildren(nestedClass)) {
                if (isa<semantic::SVConstraintBlockSymbolOp>(nestedMember)) {
                  continue;
                }
                if (auto method = getClassMethod(nestedMember);
                    method && method.getIsPrePostRandomize().value_or(false) &&
                    !method.getIsBuiltin().value_or(false)) {
                  StringRef name = method.getName().value_or("");
                  std::optional<Type> methodType = method.getSemanticType();
                  auto subroutineType =
                      methodType
                          ? dyn_cast<semantic::SubroutineType>(*methodType)
                          : semantic::SubroutineType{};
                  auto signature = subroutineType
                                       ? dyn_cast<FunctionType>(
                                             subroutineType.getSignature())
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
                    nestedProperty.getRandMode() == semantic::SVRandMode::None)
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
                if (succeeded(nestedType) && semanticNestedType &&
                    isa<sim::DynamicArrayType, sim::QueueType>(*nestedType)) {
                  Type elementType =
                      isa<sim::DynamicArrayType>(*nestedType)
                          ? cast<sim::DynamicArrayType>(*nestedType)
                                .getElementType()
                          : cast<sim::QueueType>(*nestedType).getElementType();
                  FailureOr<bool> inert = isInertClassHandleContainer(
                      *semanticNestedType, elementType);
                  if (failed(inert)) {
                    emitError(getSemanticLocation(nestedProperty))
                        << "nested random class-handle container has no "
                           "analyzable element class";
                    unsupportedNestedSemantics = true;
                    continue;
                  }
                  if (*inert &&
                      nestedProperty.getRandMode() !=
                          semantic::SVRandMode::RandC &&
                      nestedProperty.getLifetime() !=
                          semantic::SVVariableLifetime::Static) {
                    containerProperties.push_back(
                        {nestedProperty,
                         classFieldSymbols.lookup(nestedProperty),
                         *nestedType,
                         elementType,
                         0,
                         modeIndex,
                         field,
                         sim::ClassHandleType::get(
                             context,
                             FlatSymbolRefAttr::get(
                                 context,
                                 classSymbols.lookup(concreteClasses.front())
                                     .getValue())),
                         objectType,
                         childModeIndex,
                         {},
                         true});
                    continue;
                  }
                  std::optional<unsigned> elementWidth =
                      sim::getPackedWidth(elementType);
                  if (nestedProperty.getRandMode() ==
                          semantic::SVRandMode::RandC ||
                      nestedProperty.getLifetime() ==
                          semantic::SVVariableLifetime::Static ||
                      !elementWidth || *elementWidth == 0 ||
                      *elementWidth > 64) {
                    emitError(getSemanticLocation(nestedProperty))
                        << "nested random dynamic containers must be "
                           "non-static rand containers of packed integral "
                           "values no wider than 64 bits";
                    unsupportedNestedSemantics = true;
                    continue;
                  }
                  containerProperties.push_back(
                      {nestedProperty, classFieldSymbols.lookup(nestedProperty),
                       *nestedType, elementType, *elementWidth, modeIndex,
                       field,
                       sim::ClassHandleType::get(
                           context,
                           FlatSymbolRefAttr::get(
                               context,
                               classSymbols.lookup(concreteClasses.front())
                                   .getValue())),
                       objectType, childModeIndex});
                  continue;
                }
                if (succeeded(nestedType) && semanticNestedType &&
                    isa<sim::ClassHandleType>(*nestedType)) {
                  if (nestedProperty.getRandMode() ==
                          semantic::SVRandMode::RandC ||
                      nestedProperty.getLifetime() ==
                          semantic::SVVariableLifetime::Static) {
                    emitError(getSemanticLocation(nestedProperty))
                        << "recursive rand object handles must be non-static "
                           "rand values";
                    unsupportedNestedSemantics = true;
                  } else if (failed(collectRecursiveObject(
                                 nestedProperty, *nestedType, childModeIndex,
                                 {}, nestedAncestors))) {
                    if (recursiveDispatchCreated)
                      return true;
                    unsupportedNestedSemantics = true;
                  }
                  continue;
                }
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
                nestedPostHook,
                {}};
            nestedPlan.constraintGroups = std::move(nestedEffectiveConstraints);
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
          properties.push_back({property,
                                field,
                                referencePath,
                                *type,
                                *width,
                                modeIndex,
                                false,
                                {},
                                0,
                                false,
                                {},
                                {},
                                {},
                                0,
                                isSignedSemanticType(*semanticPropertyType),
                                isRandC,
                                randcKeyField,
                                randcPositionField,
                                randcKeyPath,
                                randcPositionPath,
                                randomModeStorage,
                                std::move(domains)});
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
    llvm::SmallPtrSet<Operation *, 16> nestedConstraintSources;
    for (NestedObjectPlan &plan : nestedObjectPlans) {
      for (const EffectiveConstraintGroup &group : plan.constraintGroups) {
        plan.globalConstraintIndices.push_back(constraintGroups.size());
        constraintGroups.push_back(group);
        for (semantic::SVConstraintBlockSymbolOp constraint : group)
          nestedConstraintSources.insert(constraint);
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
    llvm::DenseMap<Operation *, SmallVector<unsigned>> constraintRootIndices;
    for (auto [index, group] : llvm::enumerate(constraintGroups)) {
      for (semantic::SVConstraintBlockSymbolOp constraint : group)
        constraintIndices[constraint] = index;
    }
    // Stable mode-bit order follows the first base declaration of each named
    // block. Executable body and soft-priority order instead follow active
    // source declaration order, with every derived declaration after all base
    // declarations. Keep those two orderings deliberately separate.
    for (auto [groupIndex, group] : llvm::enumerate(constraintGroups)) {
      for (semantic::SVConstraintBlockSymbolOp constraint : group) {
        if (constraint.getIsExtern().value_or(false) ||
            constraint.getIsPure().value_or(false)) {
          emitError(getSemanticLocation(constraint))
              << "extern and pure constraint blocks are not executable yet";
          invalid = true;
          continue;
        }
        for (Operation *child : getChildren(constraint))
          if (isa<semantic::SVConstraintListOp>(child)) {
            constraintRoots.push_back(child);
            constraintRootIndices[child].push_back(groupIndex);
          }
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
          auto block =
              root->getParentOfType<semantic::SVConstraintBlockSymbolOp>();
          auto index =
              block ? constraintIndices.find(block) : constraintIndices.end();
          if (index == constraintIndices.end())
            unconditionalConstraint = true;
          else
            constraintMask |= uint64_t{1} << index->second;
        }
        bool softReference =
            llvm::any_of(constraintRoots, [&](Operation *root) {
              bool found = false;
              root->walk([&](semantic::SVExpressionConstraintOp expression) {
                if (!expression.getIsSoft())
                  return;
                expression->walk([&](Operation *nested) {
                  auto reference =
                      nested->getAttrOfType<SymbolRefAttr>("referenced_symbol");
                  if (reference &&
                      reference.getLeafReference() ==
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
        properties.push_back({property.source,
                              property.field,
                              {},
                              builder.getI32Type(),
                              32,
                              property.modeIndex,
                              true,
                              property.type,
                              constraintMask,
                              unconditionalConstraint,
                              property.nestedObjectField,
                              property.nestedObjectType,
                              property.nestedObjectStorageType,
                              property.nestedModeIndex,
                              true,
                              false,
                              {},
                              {},
                              {},
                              {},
                              {},
                              {std::move(nonnegative)},
                              property.nestedObjectPath});
      }
    }
    for (const RandomProperty &property : properties) {
      if (!property.nestedObjectField)
        continue;
      bool referenced = llvm::any_of(constraintRoots, [&](Operation *root) {
        auto block =
            root->getParentOfType<semantic::SVConstraintBlockSymbolOp>();
        // A child declaration is shared by every member of that type. Its
        // own constraint roots therefore reference the same declaration
        // symbols for each instance and are resolved after cloning.
        if (block && nestedConstraintSources.contains(block))
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
    llvm::DenseMap<Operation *, unsigned> randomIndices;
    for (auto [index, property] : llvm::enumerate(properties))
      randomIndices[property.source] = index;

    // A declaration template is an exact replacement for the class-owned
    // constraint roots only when this invocation's active object graph is the
    // exact receiver. Inline constraints and recursively randomized members
    // still require call-local composition, while active static rand properties
    // need a storage-to-variable binding that the current scalar plan does not
    // carry. Keep every such case on the complete legacy path.
    sim::SimClassDeclOp exactDeclaration =
        classes->declarations.lookup(foundClass->second);
    FlatSymbolRefAttr constraintTemplate =
        exactDeclaration ? exactDeclaration.getRandomConstraintTemplateAttr()
                         : FlatSymbolRefAttr{};
    bool templateEligible =
        constraintTemplate && !hasInlineConstraints &&
        nestedObjectPlans.empty() && containerProperties.empty() &&
        recursiveAliasGuards.empty() &&
        llvm::all_of(properties, [](const RandomProperty &property) {
          return property.field && !property.referencePath &&
                 !property.isContainerSize && !property.nestedObjectField;
        });
    if (templateEligible) {
      call->setAttr(randomizeConstraintTemplateAttrName, constraintTemplate);
      // The template contains the complete effective class constraint set.
      // Clearing the borrowed declaration roots prevents the clone/expand and
      // annotation stages below from manufacturing a call-local copy.
      constraintRoots.clear();
    }

    // Clone declaration-owned constraints into the call before expanding
    // function calls. A dynamic randomize dispatch has one frozen call per
    // concrete class, so mutating the shared class declaration would leak one
    // plan's virtual resolution and state annotations into every other plan.
    OpBuilder constraintBuilder =
        OpBuilder::atBlockEnd(&call->getRegion(0).front());
    llvm::DenseMap<Operation *, unsigned> nextConstraintRootIndex;
    for (Operation *&root : constraintRoots) {
      if (root->getParentOfType<semantic::SVCallExpressionOp>() == call)
        continue;
      Operation *source = root;
      Operation *cloned = constraintBuilder.clone(*source);
      auto indices = constraintRootIndices.find(source);
      if (indices != constraintRootIndices.end()) {
        unsigned &next = nextConstraintRootIndex[source];
        if (next >= indices->second.size()) {
          emitError(getSemanticLocation(source))
              << "constraint root has inconsistent per-instance identity";
          invalid = true;
          return true;
        }
        cloned->setAttr(randomConstraintBlockAttrName,
                        builder.getI32IntegerAttr(indices->second[next++]));
      }
      root = cloned;
    }

    auto getNestedConstraintOwner = [&](Operation *root) -> NestedObjectPlan * {
      auto index =
          root->getAttrOfType<IntegerAttr>(randomConstraintBlockAttrName);
      if (!index || index.getValue().isNegative() ||
          index.getValue().getActiveBits() > 32)
        return nullptr;
      unsigned value = index.getValue().getZExtValue();
      for (NestedObjectPlan &plan : nestedObjectPlans)
        if (llvm::is_contained(plan.globalConstraintIndices, value))
          return &plan;
      return nullptr;
    };
    auto objectPathsEqual = [](ArrayRef<RandomObjectPathElement> lhs,
                               ArrayRef<RandomObjectPathElement> rhs) {
      return lhs.size() == rhs.size() &&
             llvm::equal(lhs, rhs,
                         [](const RandomObjectPathElement &left,
                            const RandomObjectPathElement &right) {
                           return left.field == right.field &&
                                  left.concreteType == right.concreteType &&
                                  left.storageType == right.storageType;
                         });
    };
    NestedObjectPlan *randomValueOwner = nullptr;
    auto getRandomPropertyIndex =
        [&](Operation *source) -> std::optional<unsigned> {
      for (auto [index, property] : llvm::enumerate(properties)) {
        if (property.source != source)
          continue;
        if (randomValueOwner) {
          if (property.nestedObjectField == randomValueOwner->field &&
              objectPathsEqual(property.nestedObjectPath,
                               randomValueOwner->nestedObjectPath))
            return index;
        } else if (!property.nestedObjectField) {
          return index;
        }
      }
      return std::nullopt;
    };

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
        std::optional<unsigned> index =
            symbol == semanticSymbols.end()
                ? std::nullopt
                : getRandomPropertyIndex(symbol->second);
        if (index)
          return std::optional<RandomValuePath>(RandomValuePath{
              *index, 0, static_cast<unsigned>(properties[*index].width), true,
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
      randomValueOwner = getNestedConstraintOwner(root);
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
    randomValueOwner = nullptr;

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
                semantic::SVUniquenessConstraintOp,
                semantic::SVForeachConstraintOp>(nested))
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
        attributes.push_back(
            builder.getNamedAttr(randomContainerTypeAttrName,
                                 TypeAttr::get(property.containerType)));
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
        attributes.push_back(
            builder.getNamedAttr(randomNestedObjectTypeAttrName,
                                 TypeAttr::get(property.nestedObjectType)));
        attributes.push_back(builder.getNamedAttr(
            randomNestedObjectStorageTypeAttrName,
            TypeAttr::get(property.nestedObjectStorageType)));
        attributes.push_back(builder.getNamedAttr(
            randomNestedModeIndexAttrName,
            builder.getI32IntegerAttr(property.nestedModeIndex)));
        if (!property.nestedObjectPath.empty()) {
          SmallVector<Attribute> path;
          for (const RandomObjectPathElement &element :
               property.nestedObjectPath)
            path.push_back(builder.getDictionaryAttr({
                builder.getNamedAttr("field", element.field),
                builder.getNamedAttr("concrete_type",
                                     TypeAttr::get(element.concreteType)),
                builder.getNamedAttr("storage_type",
                                     TypeAttr::get(element.storageType)),
                builder.getNamedAttr(
                    "rand_mode_index",
                    builder.getI32IntegerAttr(element.modeIndex)),
            }));
          attributes.push_back(builder.getNamedAttr(
              randomNestedObjectPathAttrName, builder.getArrayAttr(path)));
        }
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
    for (const RandomContainerProperty &property : containerProperties) {
      SmallVector<NamedAttribute> attributes{
          builder.getNamedAttr("field", property.field),
          builder.getNamedAttr("type", TypeAttr::get(property.type)),
          builder.getNamedAttr("element_type",
                               TypeAttr::get(property.elementType)),
          builder.getNamedAttr("element_width", builder.getI64IntegerAttr(
                                                    property.elementWidth)),
          builder.getNamedAttr(randomPropertyModeIndexAttrName,
                               builder.getI32IntegerAttr(property.modeIndex)),
      };
      if (property.inertClassHandles)
        attributes.push_back(
            builder.getNamedAttr("inert_class_handles", builder.getUnitAttr()));
      if (property.nestedObjectField) {
        attributes.push_back(builder.getNamedAttr(
            randomNestedObjectFieldAttrName, property.nestedObjectField));
        attributes.push_back(
            builder.getNamedAttr(randomNestedObjectTypeAttrName,
                                 TypeAttr::get(property.nestedObjectType)));
        attributes.push_back(builder.getNamedAttr(
            randomNestedObjectStorageTypeAttrName,
            TypeAttr::get(property.nestedObjectStorageType)));
        attributes.push_back(builder.getNamedAttr(
            randomNestedModeIndexAttrName,
            builder.getI32IntegerAttr(property.nestedModeIndex)));
        if (!property.nestedObjectPath.empty()) {
          SmallVector<Attribute> path;
          for (const RandomObjectPathElement &element :
               property.nestedObjectPath)
            path.push_back(builder.getDictionaryAttr({
                builder.getNamedAttr("field", element.field),
                builder.getNamedAttr("concrete_type",
                                     TypeAttr::get(element.concreteType)),
                builder.getNamedAttr("storage_type",
                                     TypeAttr::get(element.storageType)),
                builder.getNamedAttr(
                    "rand_mode_index",
                    builder.getI32IntegerAttr(element.modeIndex)),
            }));
          attributes.push_back(builder.getNamedAttr(
              randomNestedObjectPathAttrName, builder.getArrayAttr(path)));
        }
      }
      containerPropertyAttrs.push_back(builder.getDictionaryAttr(attributes));
    }
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
      SmallVector<NamedAttribute> attributes{
          builder.getNamedAttr("field", plan.field),
          builder.getNamedAttr("concrete_type",
                               TypeAttr::get(plan.concreteType)),
          builder.getNamedAttr("storage_type", TypeAttr::get(plan.storageType)),
          builder.getNamedAttr("outer_mode_index",
                               builder.getI32IntegerAttr(plan.outerModeIndex)),
          builder.getNamedAttr("global_indices",
                               builder.getDenseI64ArrayAttr(indices)),
      };
      if (!plan.nestedObjectPath.empty()) {
        SmallVector<Attribute> path;
        for (const RandomObjectPathElement &element : plan.nestedObjectPath)
          path.push_back(builder.getDictionaryAttr({
              builder.getNamedAttr("field", element.field),
              builder.getNamedAttr("concrete_type",
                                   TypeAttr::get(element.concreteType)),
              builder.getNamedAttr("storage_type",
                                   TypeAttr::get(element.storageType)),
              builder.getNamedAttr("rand_mode_index", builder.getI32IntegerAttr(
                                                          element.modeIndex)),
          }));
        attributes.push_back(
            builder.getNamedAttr("path", builder.getArrayAttr(path)));
      }
      nestedConstraintModeAttrs.push_back(
          builder.getDictionaryAttr(attributes));
    }
    call->setAttr(randomNestedConstraintModesAttrName,
                  builder.getArrayAttr(nestedConstraintModeAttrs));
    SmallVector<Attribute> nestedHookAttrs;
    SmallVector<const NestedObjectPlan *> hookPlans;
    for (const NestedObjectPlan &plan : nestedObjectPlans)
      if (plan.preHook || plan.postHook)
        hookPlans.push_back(&plan);
    llvm::stable_sort(hookPlans, [](const NestedObjectPlan *left,
                                    const NestedObjectPlan *right) {
      return left->nestedObjectPath.size() < right->nestedObjectPath.size();
    });
    for (const NestedObjectPlan *planPointer : hookPlans) {
      const NestedObjectPlan &plan = *planPointer;
      SmallVector<NamedAttribute> attributes{
          builder.getNamedAttr("field", plan.field),
          builder.getNamedAttr("concrete_type",
                               TypeAttr::get(plan.concreteType)),
          builder.getNamedAttr("storage_type", TypeAttr::get(plan.storageType)),
          builder.getNamedAttr("outer_mode_index",
                               builder.getI32IntegerAttr(plan.outerModeIndex)),
      };
      if (!plan.nestedObjectPath.empty()) {
        SmallVector<Attribute> path;
        for (const RandomObjectPathElement &element : plan.nestedObjectPath)
          path.push_back(builder.getDictionaryAttr({
              builder.getNamedAttr("field", element.field),
              builder.getNamedAttr("concrete_type",
                                   TypeAttr::get(element.concreteType)),
              builder.getNamedAttr("storage_type",
                                   TypeAttr::get(element.storageType)),
              builder.getNamedAttr("rand_mode_index", builder.getI32IntegerAttr(
                                                          element.modeIndex)),
          }));
        attributes.push_back(
            builder.getNamedAttr("path", builder.getArrayAttr(path)));
      }
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
    SmallVector<Attribute> recursiveAliasGuardAttrs;
    for (const RecursiveAliasGuard &guard : recursiveAliasGuards) {
      SmallVector<Attribute> path;
      for (const RandomObjectPathElement &element : guard.path)
        path.push_back(builder.getDictionaryAttr({
            builder.getNamedAttr("field", element.field),
            builder.getNamedAttr("concrete_type",
                                 TypeAttr::get(element.concreteType)),
            builder.getNamedAttr("storage_type",
                                 TypeAttr::get(element.storageType)),
            builder.getNamedAttr("rand_mode_index",
                                 builder.getI32IntegerAttr(element.modeIndex)),
        }));
      recursiveAliasGuardAttrs.push_back(builder.getDictionaryAttr({
          builder.getNamedAttr("field", guard.field),
          builder.getNamedAttr("concrete_type",
                               TypeAttr::get(guard.concreteType)),
          builder.getNamedAttr("storage_type",
                               TypeAttr::get(guard.storageType)),
          builder.getNamedAttr("outer_mode_index",
                               builder.getI32IntegerAttr(guard.outerModeIndex)),
          builder.getNamedAttr("path", builder.getArrayAttr(path)),
          builder.getNamedAttr("alias_depth",
                               builder.getI32IntegerAttr(guard.aliasDepth)),
      }));
    }
    call->setAttr(randomRecursiveAliasGuardsAttrName,
                  builder.getArrayAttr(recursiveAliasGuardAttrs));
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
      NestedObjectPlan *frozenNestedOwner =
          getNestedConstraintOwner(constraint);
      randomValueOwner = frozenNestedOwner;
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
        std::optional<unsigned> index = getRandomPropertyIndex(symbol->second);
        if (index && !nested->hasAttr(randomFunctionStateAttrName)) {
          if (properties[*index].isContainerSize) {
            auto sizeCall = dyn_cast_or_null<semantic::SVCallExpressionOp>(
                nested->getParentOp());
            if (!sizeCall || sizeCall.getCalleeName() != "size") {
              emitError(getSemanticLocation(nested))
                  << "a constrained dynamic container may only participate "
                     "through its size() value";
              invalid = true;
            } else {
              sizeCall->setAttr(randomVariableAttrName,
                                builder.getI32IntegerAttr(*index));
            }
          } else {
            nested->setAttr(randomVariableAttrName,
                            builder.getI32IntegerAttr(*index));
          }
        } else if (property && frozenNestedOwner &&
                   property.getLifetime() !=
                       semantic::SVVariableLifetime::Static &&
                   llvm::is_contained(frozenNestedOwner->hierarchy,
                                      getOwningClass(property))) {
          nested->setAttr(randomNestedStateFieldAttrName,
                          frozenNestedOwner->field);
          nested->setAttr(randomNestedStateConcreteTypeAttrName,
                          TypeAttr::get(frozenNestedOwner->concreteType));
          nested->setAttr(randomNestedStateStorageTypeAttrName,
                          TypeAttr::get(frozenNestedOwner->storageType));
          if (!frozenNestedOwner->nestedObjectPath.empty()) {
            SmallVector<Attribute> path;
            for (const RandomObjectPathElement &element :
                 frozenNestedOwner->nestedObjectPath)
              path.push_back(builder.getDictionaryAttr({
                  builder.getNamedAttr("field", element.field),
                  builder.getNamedAttr("concrete_type",
                                       TypeAttr::get(element.concreteType)),
                  builder.getNamedAttr("storage_type",
                                       TypeAttr::get(element.storageType)),
              }));
            nested->setAttr(randomNestedStatePathAttrName,
                            builder.getArrayAttr(path));
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
      randomValueOwner = nullptr;
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

    // IEEE 1800-2017 18.9: the receiver names a constraint block, which a
    // method of the owning class may name on its own. The frontend spells the
    // qualified form as a member access and the implicit-`this` form as a
    // direct reference to the block symbol; both identify the same block.
    auto member =
        dyn_cast<semantic::SVMemberAccessExpressionOp>(callChildren.front());
    auto implicitReceiver =
        dyn_cast<semantic::SVArbitrarySymbolExpressionOp>(callChildren.front());
    auto reference =
        member ? member->getAttrOfType<SymbolRefAttr>("referenced_symbol")
        : implicitReceiver ? implicitReceiver->getAttrOfType<SymbolRefAttr>(
                                 "referenced_symbol")
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

    FailureOr<SmallVector<CompatibleConcreteClass>> compatible =
        getCompatibleConcreteClasses(foundClass->second,
                                     "object random-stream dispatch");
    if (failed(compatible)) {
      invalid = true;
      return true;
    }
    llvm::sort(*compatible, [&](const CompatibleConcreteClass &lhs,
                                const CompatibleConcreteClass &rhs) {
      if (lhs.depth != rhs.depth)
        return lhs.depth > rhs.depth;
      return classSymbols.lookup(lhs.classType).getValue() <
             classSymbols.lookup(rhs.classType).getValue();
    });
    SmallVector<Attribute> classes;
    for (const CompatibleConcreteClass &entry : *compatible)
      classes.push_back(FlatSymbolRefAttr::get(
          context, classSymbols.lookup(entry.classType).getValue()));
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

  // A randsequence's production declarations are symbols owned by the
  // statement's automatic grammar scope. Symbols must remain in the semantic
  // symbol table, but their executable production graph must participate in
  // capture analysis and must later be cloned into an isolated simulation
  // function. Freeze that graph into non-symbol semantic nodes beneath the
  // statement. References retain the source symbols' globally unique leaf
  // names, which is the same stable identity used by the preparation symbol
  // index and by production-item references.
  SmallVector<semantic::SVRandSequenceStatementOp> randSequences;
  semanticRoot->walk([&](semantic::SVRandSequenceStatementOp statement) {
    randSequences.push_back(statement);
  });
  auto getSemanticSymbolReference = [&](Operation *symbol) {
    SmallVector<StringAttr> path;
    for (Operation *current = symbol; current; current = current->getParentOp())
      if (isa<SymbolOpInterface>(current))
        path.push_back(SymbolTable::getSymbolName(current));
    std::reverse(path.begin(), path.end());
    SmallVector<FlatSymbolRefAttr> nested;
    for (StringAttr name : ArrayRef(path).drop_front())
      nested.push_back(FlatSymbolRefAttr::get(name));
    return SymbolRefAttr::get(path.front(), nested);
  };
  for (semantic::SVRandSequenceStatementOp statement : randSequences) {
    auto firstReference =
        statement->getAttrOfType<SymbolRefAttr>("first_production");
    auto first = firstReference
                     ? semanticSymbols.find(firstReference.getLeafReference())
                     : semanticSymbols.end();
    auto firstProduction =
        first != semanticSymbols.end()
            ? dyn_cast<semantic::SVRandSeqProductionSymbolOp>(first->second)
            : semantic::SVRandSeqProductionSymbolOp{};
    if (!firstProduction) {
      emitError(getSemanticLocation(statement))
          << "randsequence cannot resolve its first production";
      invalid = true;
      continue;
    }

    SmallVector<semantic::SVRandSeqProductionSymbolOp> productions;
    for (Operation *child : getChildren(firstProduction->getParentOp()))
      if (auto production =
              dyn_cast<semantic::SVRandSeqProductionSymbolOp>(child))
        productions.push_back(production);
    auto expectedCount = getUnsigned64(
        statement->getAttrOfType<IntegerAttr>("production_count"));
    if (!expectedCount || *expectedCount != productions.size()) {
      emitError(getSemanticLocation(statement))
          << "randsequence production inventory does not match its frozen "
             "production_count";
      invalid = true;
      continue;
    }

    Region &statementRegion = statement.getBody();
    if (statementRegion.empty())
      statementRegion.emplaceBlock();
    OpBuilder productionBuilder(&statementRegion.front(),
                                statementRegion.front().end());
    for (semantic::SVRandSeqProductionSymbolOp production : productions) {
      SmallVector<Attribute> formalArguments;
      SmallVector<Operation *> formalDefaultOperands;
      for (Operation *child : getChildren(production)) {
        auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child);
        if (!formal)
          continue;
        NamedAttrList attributes(formal->getAttrs());
        attributes.set("referenced_symbol", getSemanticSymbolReference(formal));
        attributes.set("referenced_path",
                       builder.getStringAttr(getHierarchyName(formal)));
        SmallVector<Operation *> defaults = getChildren(formal);
        attributes.set("default_operand_count",
                       builder.getI64IntegerAttr(defaults.size()));
        formalArguments.push_back(
            DictionaryAttr::get(context, attributes.getAttrs()));
        llvm::append_range(formalDefaultOperands, defaults);
      }

      SmallVector<Attribute> ruleVariables;
      auto ruleBlocks = production->getAttrOfType<ArrayAttr>("rule_blocks");
      if (!ruleBlocks) {
        emitError(getSemanticLocation(production))
            << "randsequence production has no frozen rule-block inventory";
        invalid = true;
        break;
      }
      for (Attribute attribute : ruleBlocks) {
        auto reference = dyn_cast<SymbolRefAttr>(attribute);
        auto found = reference
                         ? semanticSymbols.find(reference.getLeafReference())
                         : semanticSymbols.end();
        Operation *ruleBlock =
            found != semanticSymbols.end() ? found->second : nullptr;
        if (!ruleBlock) {
          emitError(getSemanticLocation(production))
              << "randsequence production cannot resolve a rule block";
          invalid = true;
          break;
        }
        SmallVector<Attribute> variables;
        for (Operation *child : getChildren(ruleBlock)) {
          auto variable = dyn_cast<semantic::SVVariableSymbolOp>(child);
          if (!variable)
            continue;
          NamedAttrList attributes(variable->getAttrs());
          attributes.set("referenced_symbol",
                         getSemanticSymbolReference(variable));
          attributes.set("referenced_path",
                         builder.getStringAttr(getHierarchyName(variable)));
          variables.push_back(
              DictionaryAttr::get(context, attributes.getAttrs()));
        }
        ruleVariables.push_back(builder.getArrayAttr(variables));
      }
      if (invalid)
        break;

      NamedAttrList attributes(production->getAttrs());
      attributes.erase(SymbolTable::getSymbolAttrName());
      attributes.erase("name");
      attributes.erase("hierarchical_name");
      attributes.set("referenced_symbol",
                     getSemanticSymbolReference(production));
      attributes.set("referenced_path",
                     builder.getStringAttr(getHierarchyName(production)));
      attributes.set("formal_arguments", builder.getArrayAttr(formalArguments));
      attributes.set("rule_variables", builder.getArrayAttr(ruleVariables));

      OperationState state(
          getSemanticLocation(production),
          semantic::SVFrozenRandSeqProductionOp::getOperationName());
      state.addAttributes(attributes);
      state.addRegion();
      Operation *frozen = productionBuilder.create(state);
      Block *body = new Block;
      frozen->getRegion(0).push_back(body);
      OpBuilder bodyBuilder(body, body->end());
      for (Operation *operand : formalDefaultOperands)
        bodyBuilder.clone(*operand);
      for (Operation *child : getChildren(production)) {
        if (!isa<SymbolOpInterface>(child))
          bodyBuilder.clone(*child);
      }
    }
  }
  if (invalid)
    return abort();

  FailureOr<PreparedCaptures> preparedCaptures = analyzeCodeUnitCaptures(
      *preparedUnits, descriptors, semanticSymbols, classSources);
  if (failed(preparedCaptures))
    return abort();
  auto &unitCaptures = preparedCaptures->descriptors;
  auto &unitReadCaptures = preparedCaptures->readDescriptors;
  auto &unitWrittenCaptures = preparedCaptures->writtenDescriptors;
  auto &unitLocals = preparedCaptures->locals;
  auto &unitConstants = preparedCaptures->constants;
  auto &observerLocalCaptures = preparedCaptures->observerLocals;
  auto &observerValueCaptures = preparedCaptures->observerValues;
  auto &observerReadLocals = preparedCaptures->observerReadLocals;
  auto &indirectRefTasks = preparedCaptures->indirectRefTasks;

  llvm::DenseMap<uint64_t, Operation *> timingConditions;
  semanticRoot->walk([&](Operation *nested) {
    auto nodeID = nested->getAttrOfType<IntegerAttr>("node_id");
    if (nodeID && nested->hasAttr("obelisk_sim.observer"))
      timingConditions.try_emplace(nodeID.getValue().getZExtValue(), nested);
  });

  for (PreparedUnit &unit : units) {
    auto rules =
        unit.source->getAttrOfType<ArrayAttr>("obelisk.timing_path_rules");
    if (!rules)
      continue;
    llvm::StringSet<> existingCaptures;
    for (const auto &capture : unitCaptures[unit.source])
      existingCaptures.insert(capture.first);
    for (Attribute attr : rules) {
      auto rule = dyn_cast<DictionaryAttr>(attr);
      SmallVector<StringAttr> inputs;
      if (auto array = rule ? rule.getAs<ArrayAttr>("inputs") : ArrayAttr{})
        for (Attribute input : array)
          if (auto path = dyn_cast<StringAttr>(input))
            inputs.push_back(path);
      if (auto legacy = rule ? rule.getAs<StringAttr>("input") : StringAttr{})
        inputs.push_back(legacy);
      SmallVector<StringAttr> snapshots;
      if (auto array = rule ? rule.getAs<ArrayAttr>("snapshots") : ArrayAttr{})
        for (Attribute snapshot : array)
          if (auto path = dyn_cast<StringAttr>(snapshot))
            snapshots.push_back(path);
      if (auto legacy =
              rule ? rule.getAs<StringAttr>("snapshot") : StringAttr{})
        snapshots.push_back(legacy);
      if (!rule || inputs.empty() || snapshots.empty() ||
          inputs.size() != snapshots.size()) {
        emitError(getSemanticLocation(unit.source))
            << "timing path has no frozen source snapshots";
        invalid = true;
        continue;
      }
      bool edgeSensitive = false;
      if (auto edge = rule.getAs<BoolAttr>("edge_sensitive"))
        edgeSensitive = edge.getValue();
      if (edgeSensitive)
        for (StringAttr input : inputs) {
          auto found = descriptors.find(input.getValue());
          if (found == descriptors.end()) {
            emitError(getSemanticLocation(unit.source))
                << "edge-sensitive timing path source no longer resolves";
            invalid = true;
            continue;
          }
          if (existingCaptures.insert(input.getValue()).second)
            unitCaptures[unit.source].push_back(
                {input.getValue().str(), found->second});
          unitReadCaptures[unit.source].insert(input.getValue());
        }
      if (edgeSensitive)
        for (StringRef name : {"edge_pending", "edge_epoch"}) {
          auto state = rule.getAs<StringAttr>(name);
          auto found =
              state ? descriptors.find(state.getValue()) : descriptors.end();
          if (!state || found == descriptors.end()) {
            emitError(getSemanticLocation(unit.source))
                << "edge-sensitive timing path qualification state no longer "
                   "resolves";
            invalid = true;
            continue;
          }
          if (existingCaptures.insert(state.getValue()).second)
            unitCaptures[unit.source].push_back(
                {state.getValue().str(), found->second});
          unitWrittenCaptures[unit.source].insert(state.getValue());
        }
      for (StringAttr snapshot : snapshots) {
        auto found = descriptors.find(snapshot.getValue());
        if (found == descriptors.end()) {
          emitError(getSemanticLocation(unit.source))
              << "timing path snapshot no longer resolves";
          invalid = true;
          continue;
        }
        if (existingCaptures.insert(snapshot.getValue()).second)
          unitCaptures[unit.source].push_back(
              {snapshot.getValue().str(), found->second});
      }
      auto conditionNode = rule.getAs<IntegerAttr>("condition_node_id");
      if (!conditionNode)
        continue;
      auto condition =
          timingConditions.find(conditionNode.getValue().getZExtValue());
      if (condition == timingConditions.end()) {
        emitError(getSemanticLocation(unit.source))
            << "timing path condition evaluator no longer resolves";
        invalid = true;
        continue;
      }
      condition->second->setAttr("obelisk.timing_path_condition_truth",
                                 builder.getUnitAttr());
      if (!observerLocalCaptures[condition->second].empty() ||
          !observerValueCaptures[condition->second].empty()) {
        emitError(getSemanticLocation(condition->second))
            << "specify path condition cannot capture an automatic local";
        invalid = true;
        continue;
      }
      for (const auto &capture : unitCaptures[condition->second])
        if (existingCaptures.insert(capture.first).second)
          unitCaptures[unit.source].push_back(capture);
    }
  }

  // Computed procedural controls monitor the declared Clause 30 source in
  // their already-outlined event-primary observer. Add monitor state as
  // captures only; snapshot/pending/condition captures are deliberately not
  // observer dependencies, so their writes cannot recursively activate the
  // observer or make condition-only changes look like source events.
  for (PreparedUnit &unit : units) {
    if (unit.entryKind != sim::EntryKind::Observer)
      continue;
    auto rules = unit.source->getAttrOfType<ArrayAttr>(
        "obelisk.timing_path_monitor_rules");
    if (!rules)
      continue;
    llvm::StringSet<> existingCaptures;
    for (const auto &capture : unitCaptures[unit.source])
      existingCaptures.insert(capture.first);
    auto addCapture = [&](StringAttr path, bool dependency) {
      auto found = path ? descriptors.find(path.getValue()) : descriptors.end();
      if (!path || found == descriptors.end()) {
        invalid = true;
        return;
      }
      if (existingCaptures.insert(path.getValue()).second)
        unitCaptures[unit.source].push_back(
            {path.getValue().str(), found->second});
      if (dependency)
        unitReadCaptures[unit.source].insert(path.getValue());
      else
        unitWrittenCaptures[unit.source].insert(path.getValue());
    };
    for (Attribute attr : rules) {
      auto rule = dyn_cast<DictionaryAttr>(attr);
      auto inputs = rule ? rule.getAs<ArrayAttr>("inputs") : ArrayAttr{};
      auto snapshots = rule ? rule.getAs<ArrayAttr>("snapshots") : ArrayAttr{};
      if (!inputs || !snapshots || inputs.size() != snapshots.size()) {
        emitError(getSemanticLocation(unit.source))
            << "derived procedural path monitor has invalid terminals";
        invalid = true;
        continue;
      }
      for (auto [input, snapshot] : llvm::zip(inputs, snapshots)) {
        addCapture(dyn_cast<StringAttr>(input), true);
        addCapture(dyn_cast<StringAttr>(snapshot), false);
      }
      addCapture(rule.getAs<StringAttr>("edge_pending"), false);
      addCapture(rule.getAs<StringAttr>("edge_epoch"), false);
      auto conditionNode = rule.getAs<IntegerAttr>("condition_node_id");
      if (!conditionNode)
        continue;
      auto condition =
          timingConditions.find(conditionNode.getValue().getZExtValue());
      if (condition == timingConditions.end()) {
        emitError(getSemanticLocation(unit.source))
            << "derived procedural path condition no longer resolves";
        invalid = true;
        continue;
      }
      condition->second->setAttr("obelisk.timing_path_condition_truth",
                                 builder.getUnitAttr());
      if (!observerLocalCaptures[condition->second].empty() ||
          !observerValueCaptures[condition->second].empty()) {
        emitError(getSemanticLocation(condition->second))
            << "specify path condition cannot capture an automatic local";
        invalid = true;
        continue;
      }
      for (const auto &capture : unitCaptures[condition->second])
        if (existingCaptures.insert(capture.first).second)
          unitCaptures[unit.source].push_back(capture);
    }
  }
  if (invalid)
    return abort();

  auto usesContextStorage = [&](Operation *source, const auto &capture) {
    return preparedCaptures->contextStorageSources.contains(source) &&
           isContextResolvableStorage(capture.second);
  };
  auto readCaptureAttributes = [&](Operation *source) {
    SmallVector<StringRef> paths;
    for (const auto &read : unitReadCaptures[source])
      paths.push_back(read.getKey());
    llvm::sort(paths);
    SmallVector<Attribute> attributes;
    attributes.reserve(paths.size());
    for (StringRef path : paths)
      attributes.push_back(builder.getStringAttr(path));
    return attributes;
  };
  auto writtenCaptureAttributes = [&](Operation *source) {
    SmallVector<StringRef> paths;
    for (const auto &written : unitWrittenCaptures[source])
      paths.push_back(written.getKey());
    llvm::sort(paths);
    SmallVector<Attribute> attributes;
    attributes.reserve(paths.size());
    for (StringRef path : paths)
      attributes.push_back(builder.getStringAttr(path));
    return attributes;
  };

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
    for (const PreparedLocal &value : observerValueCaptures[unit.source])
      captures.push_back(builder.getStringAttr(value.path));
    unit.source->setAttr(observerCapturesAttrName,
                         builder.getArrayAttr(captures));
    unit.source->setAttr(observerDependenciesAttrName,
                         builder.getArrayAttr(dependencies));
  }

  // Resolve the condition-node identities only after capture analysis has
  // frozen each outlined evaluator ABI. Copy that immutable ABI into the
  // driver rule so per-unit lowering needs no semantic-tree lookup.
  for (PreparedUnit &unit : units) {
    auto rules =
        unit.source->getAttrOfType<ArrayAttr>("obelisk.timing_path_rules");
    if (!rules)
      continue;
    SmallVector<Attribute> frozen;
    for (Attribute attr : rules) {
      auto rule = dyn_cast<DictionaryAttr>(attr);
      if (!rule) {
        invalid = true;
        break;
      }
      NamedAttrList fields(rule.getValue());
      if (auto conditionNode = rule.getAs<IntegerAttr>("condition_node_id")) {
        auto condition =
            timingConditions.find(conditionNode.getValue().getZExtValue());
        auto evaluator =
            condition == timingConditions.end()
                ? FlatSymbolRefAttr{}
                : condition->second->getAttrOfType<FlatSymbolRefAttr>(
                      "obelisk_sim.observer");
        auto captures = condition == timingConditions.end()
                            ? ArrayAttr{}
                            : condition->second->getAttrOfType<ArrayAttr>(
                                  observerCapturesAttrName);
        if (!evaluator || !captures) {
          emitError(getSemanticLocation(unit.source))
              << "timing path condition has no frozen evaluator ABI";
          invalid = true;
          break;
        }
        fields.set("condition_evaluator", evaluator);
        fields.set("condition_captures", captures);
      }
      frozen.push_back(builder.getDictionaryAttr(fields));
    }
    if (!invalid)
      unit.source->setAttr("obelisk.timing_path_rules",
                           builder.getArrayAttr(frozen));
  }
  for (PreparedUnit &unit : units) {
    auto rules = unit.source->getAttrOfType<ArrayAttr>(
        "obelisk.timing_path_monitor_rules");
    if (!rules)
      continue;
    SmallVector<Attribute> frozen;
    for (Attribute attr : rules) {
      auto rule = dyn_cast<DictionaryAttr>(attr);
      if (!rule) {
        invalid = true;
        break;
      }
      NamedAttrList fields(rule.getValue());
      if (auto conditionNode = rule.getAs<IntegerAttr>("condition_node_id")) {
        auto condition =
            timingConditions.find(conditionNode.getValue().getZExtValue());
        auto evaluator =
            condition == timingConditions.end()
                ? FlatSymbolRefAttr{}
                : condition->second->getAttrOfType<FlatSymbolRefAttr>(
                      "obelisk_sim.observer");
        auto captures = condition == timingConditions.end()
                            ? ArrayAttr{}
                            : condition->second->getAttrOfType<ArrayAttr>(
                                  observerCapturesAttrName);
        if (!evaluator || !captures) {
          emitError(getSemanticLocation(unit.source))
              << "derived procedural path condition has no frozen evaluator "
                 "ABI";
          invalid = true;
          break;
        }
        fields.set("condition_evaluator", evaluator);
        fields.set("condition_captures", captures);
      }
      frozen.push_back(builder.getDictionaryAttr(fields));
    }
    if (!invalid)
      unit.source->setAttr("obelisk.timing_path_monitor_rules",
                           builder.getArrayAttr(frozen));
  }
  if (invalid)
    return abort();

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
      SmallVector<Attribute> reads = readCaptureAttributes(found->second);
      for (const auto &capture : unitCaptures[found->second]) {
        if (usesContextStorage(found->second, capture))
          continue;
        captures.push_back(builder.getStringAttr(capture.first));
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
    auto nestedHooks =
        call->getAttrOfType<ArrayAttr>(randomNestedHooksAttrName);
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
        SmallVector<Attribute> reads = readCaptureAttributes(found->second);
        for (const auto &capture : unitCaptures[found->second]) {
          if (usesContextStorage(found->second, capture))
            continue;
          captures.push_back(builder.getStringAttr(capture.first));
        }
        attributes.push_back(builder.getNamedAttr(
            (prefix + "_captures").str(), builder.getArrayAttr(captures)));
        attributes.push_back(builder.getNamedAttr((prefix + "_reads").str(),
                                                  builder.getArrayAttr(reads)));
      }
      frozenHooks.push_back(builder.getDictionaryAttr(attributes));
    }
    call->setAttr(randomNestedHooksAttrName, builder.getArrayAttr(frozenHooks));
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
    SmallVector<const PreparedVirtualInterfaceCallee *> virtualTargets =
        preparedUnits->resolveVirtualInterfaceCallees(call);
    llvm::sort(virtualTargets, [&](const auto *lhs, const auto *rhs) {
      return preparedUnits->declarations.lookup(lhs->dispatchSource)
                 .getScopeId() <
             preparedUnits->declarations.lookup(rhs->dispatchSource)
                 .getScopeId();
    });
    Operation *targetSource = resolveDirectCallee(call);
    if (!targetSource && !virtualTargets.empty())
      targetSource = virtualTargets.front()->source;
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
      // In `super.member.method()`, Slang carries the super qualifier onto
      // the outer call even though the selected member is the receiver. Only
      // a call without an explicit receiver is a direct super-method call.
      bool directSuperDispatch =
          call.getIsSuperClass() && !call.getHasThisClass();
      if (directSuperDispatch)
        call->setAttr("obelisk_sim.class_super", builder.getUnitAttr());
      if (targetSubroutine.getIsVirtual().value_or(false) &&
          !directSuperDispatch)
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
          builder.getI32IntegerAttr(getStableDPIID(cIdentifier.getValue())));
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
    SmallVector<Attribute> readCapturePaths =
        readCaptureAttributes(targetSource);
    for (auto &capture : unitCaptures[targetSource]) {
      if (usesContextStorage(targetSource, capture))
        continue;
      capturePaths.push_back(builder.getStringAttr(capture.first));
    }
    call->setAttr(calleeCapturesAttrName, builder.getArrayAttr(capturePaths));
    call->setAttr(calleeReadCapturesAttrName,
                  builder.getArrayAttr(readCapturePaths));
    call->setAttr(calleeWrittenCapturesAttrName,
                  builder.getArrayAttr(writtenCaptureAttributes(targetSource)));
    if (!virtualTargets.empty()) {
      SmallVector<Attribute> candidates;
      for (const PreparedVirtualInterfaceCallee *record : virtualTargets) {
        Operation *candidate = record->source;
        SmallVector<Attribute> captures;
        SmallVector<Attribute> readCaptures = readCaptureAttributes(candidate);
        SmallVector<Attribute> writtenCaptures =
            writtenCaptureAttributes(candidate);
        for (const auto &capture : unitCaptures[candidate])
          if (!usesContextStorage(candidate, capture))
            captures.push_back(builder.getStringAttr(capture.first));
        candidates.push_back(builder.getDictionaryAttr({
            builder.getNamedAttr(
                "scope",
                builder.getI64IntegerAttr(
                    preparedUnits->declarations.lookup(record->dispatchSource)
                        .getScopeId())),
            builder.getNamedAttr(
                "callee", FlatSymbolRefAttr::get(
                              context, directCalleeNames.lookup(candidate))),
            builder.getNamedAttr("captures", builder.getArrayAttr(captures)),
            builder.getNamedAttr("read_captures",
                                 builder.getArrayAttr(readCaptures)),
            builder.getNamedAttr("written_captures",
                                 builder.getArrayAttr(writtenCaptures)),
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
          builder.getNamedAttr("static",
                               builder.getBoolAttr(isStaticFormal(formal))),
      };
      if (dpiTarget && semanticType) {
        formalAttrs.push_back(builder.getNamedAttr(
            "semantic_type", TypeAttr::get(*semanticType)));
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
  auto constructorCaptureSourceFor =
      [&](semantic::SVClassTypeOp classType) -> Operation * {
    if (semantic::SVSubroutineSymbolOp method = constructorSourceFor(classType))
      return method;
    return implicitConstructorSymbols.count(classType)
               ? classType.getOperation()
               : nullptr;
  };
  auto constructedClassFor = [&](semantic::SVNewClassExpressionOp construct)
      -> semantic::SVClassTypeOp {
    if (construct.getIsSuperClass()) {
      auto owner = construct->getParentOfType<semantic::SVClassTypeOp>();
      if (!owner || !owner.getBaseClass())
        return {};
      auto handle = dyn_cast<semantic::ClassHandleType>(*owner.getBaseClass());
      auto found =
          handle
              ? semanticClasses.find(handle.getClassName().getLeafReference())
              : semanticClasses.end();
      return found == semanticClasses.end() ? semantic::SVClassTypeOp{}
                                            : found->second;
    }
    auto type = construct->getAttrOfType<TypeAttr>("semantic_type");
    auto handle = type ? dyn_cast<semantic::ClassHandleType>(type.getValue())
                       : semantic::ClassHandleType{};
    auto found =
        handle ? semanticClasses.find(handle.getClassName().getLeafReference())
               : semanticClasses.end();
    return found == semanticClasses.end() ? semantic::SVClassTypeOp{}
                                          : found->second;
  };

  // A synthesized constructor has no semantic call operation on which to
  // freeze its capture ABI. Attach the immutable path inventory directly to
  // each new-class expression before its enclosing code unit is cloned.
  semanticRoot->walk([&](semantic::SVNewClassExpressionOp construct) {
    semantic::SVClassTypeOp classType = constructedClassFor(construct);
    Operation *source =
        classType ? constructorCaptureSourceFor(classType) : nullptr;
    if (!source)
      return;
    SmallVector<Attribute> captures;
    for (const auto &capture : unitCaptures[source])
      if (!usesContextStorage(source, capture))
        captures.push_back(builder.getStringAttr(capture.first));
    construct->setAttr(calleeCapturesAttrName, builder.getArrayAttr(captures));
    construct->setAttr(calleeReadCapturesAttrName,
                       builder.getArrayAttr(readCaptureAttributes(source)));
  });

  // Most executable subroutines have exactly one frozen implementation. Move
  // their statement bodies into that implementation instead of cloning the
  // fully annotated semantic tree and retaining a second copy until final
  // cleanup. Keep cloning sources that also contain observer or fork units,
  // sources shared by multiple prepared units, and constructors whose bodies
  // can still supply default arguments to derived constructors later in this
  // loop.
  llvm::DenseMap<Operation *, unsigned> sourceUseCounts;
  llvm::SmallPtrSet<Operation *, 32> unitSources;
  llvm::SmallPtrSet<Operation *, 32> sourcesWithNestedUnits;
  for (PreparedUnit &unit : units) {
    ++sourceUseCounts[unit.source];
    unitSources.insert(unit.source);
  }
  for (PreparedUnit &unit : units)
    for (Operation *parent = unit.source->getParentOp(); parent;
         parent = parent->getParentOp())
      if (unitSources.contains(parent))
        sourcesWithNestedUnits.insert(parent);

  for (PreparedUnit &unit : units) {
    auto captures = unitCaptures.lookup(unit.source);
    auto locals = unitLocals.lookup(unit.source);
    auto constants = unitConstants.lookup(unit.source);
    auto observerLocals = observerLocalCaptures.lookup(unit.source);
    auto observerValues = observerValueCaptures.lookup(unit.source);
    SmallVector<Type> copyOutResultTypes;
    bool instanceClassMethod = false;
    Type classThisType;
    StringRef classThisPath;
    std::optional<unsigned> observerThisArgument;
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
    for (const auto &capture : captures) {
      if (usesContextStorage(unit.source, capture)) {
        Type storageType = sim::RefType::get(context, capture.second.type);
        bindings.push_back(sim::DescriptorBindingAttr::get(
            context, builder.getStringAttr(capture.first), capture.second.id,
            storageType));
        continue;
      }
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
      unsigned argument = inputs.size();
      inputs.push_back(handleType);
      DictionaryAttr metadata =
          captureMetadata(builder, captureKind, capture.second.id);
      SmallVector<NamedAttribute> metadataAttrs(metadata.begin(),
                                                metadata.end());
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
      if (plannedDriver && plannedDriver->strengthBank)
        metadataAttrs.push_back(builder.getNamedAttr(
            "obelisk_sim.strength_driver_bank",
            builder.getI32IntegerAttr(*plannedDriver->strengthBank)));
      if (capture.second.kind == DescriptorInfo::Kind::Driver &&
          capture.second.delayedNet)
        metadataAttrs.push_back(builder.getNamedAttr("obelisk_sim.delayed_net",
                                                     builder.getUnitAttr()));
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
      IntegerAttr lvalueNode =
          plannedDriver && plannedDriver->nodeId
              ? builder.getI64IntegerAttr(*plannedDriver->nodeId)
              : IntegerAttr{};
      bindings.push_back(sim::ArgumentBindingAttr::get(
          context, builder.getStringAttr(capture.first), argument,
          plannedDriver ? sim::UnitArgumentKind::LValueOnly
                        : sim::UnitArgumentKind::Direct,
          /*copyOut=*/false, lvalueNode, /*copyIn=*/true));
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
      inputs.push_back(local.net
                           ? Type(sim::NetType::get(context, local.type))
                           : Type(sim::RefType::get(context, local.type)));
      argAttrs.push_back(captureMetadata(builder, sim::CaptureKind::Value));
      bindings.push_back(sim::ArgumentBindingAttr::get(
          context, builder.getStringAttr(local.path), argument,
          sim::UnitArgumentKind::Direct, /*copyOut=*/false, IntegerAttr{},
          /*copyIn=*/true));
    }
    for (const PreparedLocal &value : observerValues) {
      unsigned argument = inputs.size();
      inputs.push_back(value.type);
      argAttrs.push_back(captureMetadata(builder, sim::CaptureKind::Value));
      bindings.push_back(sim::ArgumentBindingAttr::get(
          context, builder.getStringAttr(value.path), argument,
          sim::UnitArgumentKind::Direct, /*copyOut=*/false, IntegerAttr{},
          /*copyIn=*/true));
      observerThisArgument = argument;
    }

    // Subroutine formals precede non-local captures in the public contract.
    // Function output and inout formals use copy-out results. Task copy-out
    // destinations are hidden reference arguments retained by the activation.
    // Only explicit ref formals otherwise preserve caller aliasing.
    if (auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source);
        subroutine && (unit.entryKind == sim::EntryKind::Function ||
                       unit.entryKind == sim::EntryKind::Task)) {
      bool dpiImport = subroutine.getIsDpiImport().value_or(false);
      bool dpiExport = subroutine.getDpiExportCIdentifierAttr() != nullptr;
      bool dpiSubroutine = dpiImport || dpiExport;
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
              sim::UnitArgumentKind::Direct, /*copyOut=*/false, IntegerAttr{},
              /*copyIn=*/true));
        }
        for (semantic::SVFormalArgumentSymbolOp formal : formals) {
          if (dpiSubroutine) {
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
          semantic::SVArgumentDirection direction = formal.getDirection();
          if (dpiSubroutine &&
              direction == semantic::SVArgumentDirection::Ref) {
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
          // IEEE 1800-2017 13.5.1 copies an input or inout formal in at the
          // call; 13.5.2 only copies an output formal out at the return. The
          // difference shows in a static subroutine, whose formal is a static
          // variable (13.3.1) holding what the last call left in it.
          bool copyIn = direction != semantic::SVArgumentDirection::Out;
          if (copyOut && !directTask)
            copyOutResultTypes.push_back(*type);
          formalBindings.push_back(sim::ArgumentBindingAttr::get(
              context, builder.getStringAttr(getHierarchyName(formal)),
              argument,
              isRef ? sim::UnitArgumentKind::Direct
                    : sim::UnitArgumentKind::FormalLocal,
              copyOut, IntegerAttr{}, copyIn));
          if (directTask && copyOut) {
            unsigned destinationArgument = reordered.size();
            reordered.push_back(sim::RefType::get(context, *type));
            reorderedAttrs.push_back(
                captureMetadata(builder, sim::CaptureKind::Formal));
            formalBindings.push_back(sim::ArgumentBindingAttr::get(
                context, builder.getStringAttr(getHierarchyName(formal)),
                destinationArgument, sim::UnitArgumentKind::CopyOutDestination,
                /*copyOut=*/false, IntegerAttr{}, /*copyIn=*/true));
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
              argument.getLvalueNode(), argument.getCopyIn()));
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
        bool dpiExport = subroutine.getDpiExportCIdentifierAttr() != nullptr;
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
          if ((dpiImport || dpiExport) && !voidResult &&
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
      } else if (auto coerced = unit.source->getAttrOfType<TypeAttr>(
                     observerCoercedTypeAttrName)) {
        resultType = coerced.getValue();
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
    if (isa<semantic::SVSystemTimingCheckSymbolOp>(unit.source)) {
      auto times = unit.source->getAttrOfType<DenseI64ArrayAttr>(
          "timing_check_arg_time_fs");
      auto isTime = unit.source->getAttrOfType<DenseI64ArrayAttr>(
          "timing_check_arg_is_time");
      if (!times || !isTime || times.size() != isTime.size()) {
        emitError(getSemanticLocation(unit.source))
            << "basic timing check has no frozen time ABI";
        invalid = true;
        continue;
      }
      SmallVector<int64_t> ticks;
      if (auto adjusted = unit.source->getAttrOfType<DenseI64ArrayAttr>(
              "obelisk_sim.timing_adjusted_ticks")) {
        ticks.assign(times.size(), 0);
        if (adjusted.size() != 2 || ticks.size() <= 3) {
          emitError(getSemanticLocation(unit.source))
              << "negative timing check has a malformed adjusted tick ABI";
          invalid = true;
        } else {
          ticks[2] = adjusted[0];
          ticks[3] = adjusted[1];
        }
      } else {
        ticks.reserve(times.size());
        for (auto [value, time] :
             llvm::zip_equal(times.asArrayRef(), isTime.asArrayRef())) {
          if (time && value % static_cast<int64_t>(designPrecisionFs) != 0) {
            emitError(getSemanticLocation(unit.source))
                << "basic timing-check limit is incompatible with design "
                   "precision";
            invalid = true;
            break;
          }
          ticks.push_back(time ? value / static_cast<int64_t>(designPrecisionFs)
                               : 0);
        }
      }
      if (invalid)
        continue;
      functionAttrs.push_back(
          builder.getNamedAttr("obelisk_sim.timing_check_arg_ticks",
                               builder.getDenseI64ArrayAttr(ticks)));
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.timing_check_coordinator", builder.getUnitAttr()));
      for (StringRef name :
           {"timing_check_kind", "timing_check_arg_expression_children",
            "timing_check_arg_condition_children",
            "timing_check_arg_effective_edges"}) {
        Attribute value = unit.source->getAttr(name);
        if (!value) {
          emitError(getSemanticLocation(unit.source))
              << "basic timing check is missing '" << name << "'";
          invalid = true;
          break;
        }
        functionAttrs.push_back(builder.getNamedAttr(name, value));
      }
      if (invalid)
        continue;
      for (StringRef name :
           {"timing_check_arg_condition_predicates", "timing_check_event_based",
            "timing_check_remain_active",
            "obelisk_sim.timing_delayed_storage_ids",
            "obelisk_sim.timing_delayed_source_delays",
            "obelisk_sim.negative_timing_adjusted"})
        if (Attribute value = unit.source->getAttr(name))
          functionAttrs.push_back(builder.getNamedAttr(name, value));
      auto timingKind =
          unit.source->getAttrOfType<IntegerAttr>("timing_check_kind");
      auto eventBased =
          unit.source->getAttrOfType<BoolAttr>("timing_check_event_based");
      if (timingKind &&
          (timingKind.getInt() == 8 || timingKind.getInt() == 9) &&
          (!eventBased || !eventBased.getValue())) {
        // IEEE 1800-2017 31.4.2/.3 require the timer state machine whenever
        // event_based_flag can select zero.  An absent frozen attribute may
        // be backed by the semantic argument child and is therefore timer-
        // capable; inventory its one cold helper before actor lowering.
        if (nextStorageId == UINT64_MAX) {
          emitError(getSemanticLocation(unit.source))
              << "timer timing check exceeds the storage descriptor space";
          invalid = true;
          continue;
        }
        uint64_t timerStorageID = nextStorageId++;
        std::string hierarchy =
            (Twine("__obelisk_timing_timer_") + Twine(unit.id)).str();
        sim::SimStorageDeclOp::create(
            builder, getSemanticLocation(unit.source), timerStorageID, scopeID,
            builder.getI1Type(), sim::Lifetime::Design,
            builder.getStringAttr(hierarchy),
            builder.getStringAttr("timing-check timer deadline"),
            sim::ComputeObservabilityKindAttr{});
        functionAttrs.push_back(
            builder.getNamedAttr("obelisk_sim.timing_timer_storage",
                                 builder.getI64IntegerAttr(timerStorageID)));
      }
    }
    if (auto delays =
            unit.source->getAttrOfType<DenseI64ArrayAttr>("delay_fs")) {
      if (delays.empty() || delays.size() > 3) {
        emitError(getSemanticLocation(unit.source))
            << "propagation delay must contain one to three values";
        invalid = true;
        continue;
      }
      SmallVector<int64_t, 3> ticks;
      bool delayInvalid = false;
      for (int64_t femtoseconds : delays.asArrayRef()) {
        if (femtoseconds < 0 ||
            static_cast<uint64_t>(femtoseconds) % designPrecisionFs != 0) {
          emitError(getSemanticLocation(unit.source))
              << "propagation delay is incompatible with design precision";
          invalid = true;
          delayInvalid = true;
          break;
        }
        ticks.push_back(static_cast<int64_t>(
            static_cast<uint64_t>(femtoseconds) / designPrecisionFs));
      }
      if (delayInvalid)
        continue;
      functionAttrs.push_back(
          builder.getNamedAttr("obelisk_sim.propagation_delays",
                               builder.getDenseI64ArrayAttr(ticks)));
    }
    if (auto udp = unit.source->getAttrOfType<DictionaryAttr>(
            udpSemanticMetadataAttrName))
      functionAttrs.push_back(builder.getNamedAttr(udpMetadataAttrName, udp));
    if (auto rules = unit.source->getAttrOfType<ArrayAttr>(
            "obelisk.timing_path_rules")) {
      SmallVector<Attribute> tickRules;
      bool rulesInvalid = false;
      for (Attribute attr : rules) {
        auto rule = dyn_cast<DictionaryAttr>(attr);
        auto input = rule ? rule.getAs<StringAttr>("input") : StringAttr{};
        auto inputs = rule ? rule.getAs<ArrayAttr>("inputs") : ArrayAttr{};
        auto snapshot =
            rule ? rule.getAs<StringAttr>("snapshot") : StringAttr{};
        auto snapshots =
            rule ? rule.getAs<ArrayAttr>("snapshots") : ArrayAttr{};
        auto inputLows = rule ? rule.getAs<DenseI64ArrayAttr>("input_lows")
                              : DenseI64ArrayAttr{};
        auto inputWidths = rule ? rule.getAs<DenseI64ArrayAttr>("input_widths")
                                : DenseI64ArrayAttr{};
        auto inputLsbs = rule ? rule.getAs<DenseI64ArrayAttr>("input_lsbs")
                              : DenseI64ArrayAttr{};
        auto outputLow =
            rule ? rule.getAs<IntegerAttr>("output_low") : IntegerAttr{};
        auto outputWidth =
            rule ? rule.getAs<IntegerAttr>("output_width") : IntegerAttr{};
        auto outputRootWidth =
            rule ? rule.getAs<IntegerAttr>("output_root_width") : IntegerAttr{};
        auto driverNodeID =
            rule ? rule.getAs<IntegerAttr>("driver_node_id") : IntegerAttr{};
        auto proceduralStorage =
            rule ? rule.getAs<BoolAttr>("procedural_storage") : BoolAttr{};
        auto pathSiteID =
            rule ? rule.getAs<IntegerAttr>("path_site_id") : IntegerAttr{};
        auto proceduralWakeKind =
            rule ? rule.getAs<IntegerAttr>("procedural_wake_kind")
                 : IntegerAttr{};
        auto proceduralMonitor =
            rule ? rule.getAs<BoolAttr>("procedural_monitor") : BoolAttr{};
        auto connectionFull =
            rule ? rule.getAs<BoolAttr>("connection_full") : BoolAttr{};
        auto polarity =
            rule ? rule.getAs<IntegerAttr>("polarity") : IntegerAttr{};
        auto edgeSensitive =
            rule ? rule.getAs<BoolAttr>("edge_sensitive") : BoolAttr{};
        auto edgeIdentifier =
            rule ? rule.getAs<IntegerAttr>("edge_identifier") : IntegerAttr{};
        auto edgePolarity =
            rule ? rule.getAs<IntegerAttr>("edge_polarity") : IntegerAttr{};
        auto edgePending =
            rule ? rule.getAs<StringAttr>("edge_pending") : StringAttr{};
        auto edgeEpoch =
            rule ? rule.getAs<StringAttr>("edge_epoch") : StringAttr{};
        auto delays = rule ? rule.getAs<DenseI64ArrayAttr>("delay_fs")
                           : DenseI64ArrayAttr{};
        auto pulseReject =
            rule ? rule.getAs<DenseI64ArrayAttr>("pulse_reject_fs")
                 : DenseI64ArrayAttr{};
        auto pulseError = rule ? rule.getAs<DenseI64ArrayAttr>("pulse_error_fs")
                               : DenseI64ArrayAttr{};
        auto pulseOnDetect =
            rule ? rule.getAs<BoolAttr>("pulse_on_detect") : BoolAttr{};
        auto pulseShowCancelled =
            rule ? rule.getAs<BoolAttr>("pulse_show_cancelled") : BoolAttr{};
        bool edgeSensitiveValue = edgeSensitive && edgeSensitive.getValue();
        int64_t edgeIdentifierValue =
            edgeIdentifier ? edgeIdentifier.getInt() : 0;
        int64_t edgePolarityValue = edgePolarity ? edgePolarity.getInt() : 0;
        bool legacyTerminals = input && snapshot && !inputs && !snapshots;
        bool arrayTerminals = inputs && snapshots && !inputs.empty() &&
                              inputs.size() == snapshots.size();
        if ((!legacyTerminals && !arrayTerminals) || !polarity ||
            polarity.getInt() < 0 || polarity.getInt() > 2 ||
            edgeIdentifierValue < 0 || edgeIdentifierValue > 3 ||
            edgePolarityValue < 0 || edgePolarityValue > 2 ||
            (!edgeSensitiveValue &&
             (edgeIdentifierValue != 0 || edgePolarityValue != 0)) ||
            (edgeSensitiveValue && (!edgePending || !edgeEpoch)) || !delays ||
            (delays.size() != 1 && delays.size() != 2 && delays.size() != 3 &&
             delays.size() != 6 && delays.size() != 12) ||
            !pulseReject || pulseReject.size() != delays.size() ||
            !pulseError || pulseError.size() != delays.size() ||
            !pulseOnDetect || !pulseShowCancelled ||
            (arrayTerminals &&
             (!inputLows || !inputWidths ||
              static_cast<size_t>(inputLows.size()) != inputs.size() ||
              static_cast<size_t>(inputWidths.size()) != inputs.size() ||
              (edgeSensitiveValue &&
               (!inputLsbs ||
                static_cast<size_t>(inputLsbs.size()) != inputs.size())) ||
              !outputLow || !outputWidth || !outputRootWidth ||
              !connectionFull))) {
          rulesInvalid = true;
          break;
        }
        SmallVector<int64_t, 12> ticks;
        for (int64_t femtoseconds : delays.asArrayRef()) {
          if (femtoseconds < 0 ||
              static_cast<uint64_t>(femtoseconds) % designPrecisionFs != 0) {
            rulesInvalid = true;
            break;
          }
          ticks.push_back(static_cast<int64_t>(
              static_cast<uint64_t>(femtoseconds) / designPrecisionFs));
        }
        if (rulesInvalid)
          break;
        auto scalePulseLimits = [&](DenseI64ArrayAttr values,
                                    SmallVectorImpl<int64_t> &result) {
          for (int64_t femtoseconds : values.asArrayRef()) {
            if (femtoseconds < 0 ||
                static_cast<uint64_t>(femtoseconds) % designPrecisionFs != 0) {
              rulesInvalid = true;
              return;
            }
            result.push_back(static_cast<int64_t>(
                static_cast<uint64_t>(femtoseconds) / designPrecisionFs));
          }
        };
        SmallVector<int64_t, 12> rejectTicks;
        SmallVector<int64_t, 12> errorTicks;
        scalePulseLimits(pulseReject, rejectTicks);
        scalePulseLimits(pulseError, errorTicks);
        if (rulesInvalid ||
            llvm::any_of(llvm::zip(rejectTicks, errorTicks), [](auto pair) {
              return std::get<1>(pair) < std::get<0>(pair);
            })) {
          rulesInvalid = true;
          break;
        }
        SmallVector<NamedAttribute> fields;
        if (legacyTerminals) {
          fields.push_back(builder.getNamedAttr("input", input));
          fields.push_back(builder.getNamedAttr("snapshot", snapshot));
        } else {
          fields.push_back(builder.getNamedAttr("inputs", inputs));
          fields.push_back(builder.getNamedAttr("snapshots", snapshots));
          fields.push_back(builder.getNamedAttr("input_lows", inputLows));
          fields.push_back(builder.getNamedAttr("input_widths", inputWidths));
          if (inputLsbs)
            fields.push_back(builder.getNamedAttr("input_lsbs", inputLsbs));
          fields.push_back(builder.getNamedAttr("output_low", outputLow));
          fields.push_back(builder.getNamedAttr("output_width", outputWidth));
          fields.push_back(
              builder.getNamedAttr("output_root_width", outputRootWidth));
          fields.push_back(
              builder.getNamedAttr("connection_full", connectionFull));
          if (driverNodeID)
            fields.push_back(
                builder.getNamedAttr("driver_node_id", driverNodeID));
          if (proceduralStorage)
            fields.push_back(
                builder.getNamedAttr("procedural_storage", proceduralStorage));
          if (pathSiteID)
            fields.push_back(builder.getNamedAttr("path_site_id", pathSiteID));
          if (proceduralWakeKind)
            fields.push_back(builder.getNamedAttr("procedural_wake_kind",
                                                  proceduralWakeKind));
          if (proceduralMonitor)
            fields.push_back(
                builder.getNamedAttr("procedural_monitor", proceduralMonitor));
        }
        fields.push_back(builder.getNamedAttr("polarity", polarity));
        fields.push_back(builder.getNamedAttr(
            "edge_sensitive", builder.getBoolAttr(edgeSensitiveValue)));
        fields.push_back(builder.getNamedAttr(
            "edge_identifier", builder.getI32IntegerAttr(edgeIdentifierValue)));
        fields.push_back(builder.getNamedAttr(
            "edge_polarity", builder.getI32IntegerAttr(edgePolarityValue)));
        if (edgeSensitiveValue) {
          fields.push_back(builder.getNamedAttr("edge_pending", edgePending));
          fields.push_back(builder.getNamedAttr("edge_epoch", edgeEpoch));
        }
        fields.push_back(builder.getNamedAttr(
            "delays", builder.getDenseI64ArrayAttr(ticks)));
        fields.push_back(builder.getNamedAttr(
            "pulse_reject", builder.getDenseI64ArrayAttr(rejectTicks)));
        fields.push_back(builder.getNamedAttr(
            "pulse_error", builder.getDenseI64ArrayAttr(errorTicks)));
        fields.push_back(
            builder.getNamedAttr("pulse_on_detect", pulseOnDetect));
        fields.push_back(
            builder.getNamedAttr("pulse_show_cancelled", pulseShowCancelled));
        for (StringRef name :
             {"condition_kind", "condition_group", "condition_node_id"})
          if (auto value = rule.getAs<IntegerAttr>(name))
            fields.push_back(builder.getNamedAttr(name, value));
        if (auto evaluator =
                rule.getAs<FlatSymbolRefAttr>("condition_evaluator"))
          fields.push_back(
              builder.getNamedAttr("condition_evaluator", evaluator));
        if (auto captures = rule.getAs<ArrayAttr>("condition_captures"))
          fields.push_back(
              builder.getNamedAttr("condition_captures", captures));
        tickRules.push_back(builder.getDictionaryAttr(fields));
      }
      if (rulesInvalid) {
        emitError(getSemanticLocation(unit.source))
            << "timing path delay is incompatible with design precision";
        invalid = true;
        continue;
      }
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.timing_path_rules", builder.getArrayAttr(tickRules)));
    }
    if (auto monitorRules = unit.source->getAttrOfType<ArrayAttr>(
            "obelisk.timing_path_monitor_rules"))
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.timing_path_monitor_rules", monitorRules));
    if (instanceClassMethod)
      functionAttrs.push_back(builder.getNamedAttr(
          sim::metadata::thisArgument, builder.getI32IntegerAttr(1)));
    else if (observerThisArgument)
      functionAttrs.push_back(builder.getNamedAttr(
          sim::metadata::thisArgument,
          builder.getI32IntegerAttr(*observerThisArgument)));
    if (isVoidFunction)
      functionAttrs.push_back(builder.getNamedAttr("obelisk_sim.void_function",
                                                   builder.getUnitAttr()));
    // A static function's return variable is captured as design storage rather
    // than bound as an activation-local, so name it for the return lowering.
    if (auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source))
      if (std::optional<StringRef> path = subroutine.getReturnVariablePath())
        functionAttrs.push_back(builder.getNamedAttr(
            sim::metadata::returnVariablePath, builder.getStringAttr(*path)));
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
    if (unit.source->hasAttr("obelisk_sim.override_evaluator"))
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.override_evaluator", builder.getUnitAttr()));
    if (unit.entryKind == sim::EntryKind::Observer) {
      std::optional<unsigned> width =
          results.empty()
              ? std::nullopt
              : analysis::getSimulationStorageBitWidth(results.front());
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
    if (auto subroutine =
            dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source)) {
      bool dpiImport = subroutine.getIsDpiImport().value_or(false);
      StringAttr dpiExport = subroutine.getDpiExportCIdentifierAttr();
      if (dpiImport || dpiExport) {
        FailureOr<PreparedDPISignature> dpi =
            prepareDPISignature(subroutine, builder);
        if (failed(dpi)) {
          invalid = true;
          continue;
        }
        StringAttr cIdentifier =
            dpiImport ? subroutine.getDpiCIdentifierAttr() : dpiExport;
        StringRef role =
            dpiImport ? "obelisk_sim.dpi_import" : "obelisk_sim.dpi_export";
        functionAttrs.push_back(
            builder.getNamedAttr(role, builder.getUnitAttr()));
        functionAttrs.push_back(
            builder.getNamedAttr("obelisk_sim.dpi_c_identifier", cIdentifier));
        functionAttrs.push_back(builder.getNamedAttr(
            "obelisk_sim.dpi_scope_id",
            builder.getI64IntegerAttr(getScopeId(unit.source))));
        functionAttrs.push_back(builder.getNamedAttr(
            "obelisk_sim.dpi_abi_signature", dpi->entries));
        functionAttrs.push_back(builder.getNamedAttr(
            "obelisk_sim.dpi_aggregate_layouts", dpi->aggregateLayouts));
        functionAttrs.push_back(builder.getNamedAttr(
            "obelisk_sim.dpi_logical_inputs",
            builder.getI32IntegerAttr(dpi->logicalInputs)));
        sim::SimCodeUnitDeclOp declaration =
            codeUnitDeclarations.lookup(unit.source);
        declaration->setAttr(role, builder.getUnitAttr());
        declaration->setAttr("obelisk_sim.dpi_c_identifier", cIdentifier);
        declaration->setAttr("obelisk_sim.dpi_abi_signature", dpi->entries);
        declaration->setAttr("obelisk_sim.dpi_aggregate_layouts",
                             dpi->aggregateLayouts);
        declaration->setAttr("obelisk_sim.dpi_logical_inputs",
                             builder.getI32IntegerAttr(dpi->logicalInputs));
        if (!dpiImport)
          declaration->setAttr(
              "obelisk_sim.dpi_scope_id",
              builder.getI64IntegerAttr(getScopeId(unit.source)));
        if (dpiImport) {
          functionAttrs.push_back(builder.getNamedAttr(
              "obelisk_sim.dpi_import_id",
              builder.getI32IntegerAttr(getStableDPIID(
                  subroutine.getDpiCIdentifierAttr().getValue()))));
          declaration->setAttr(
              "obelisk_sim.dpi_import_id",
              builder.getI32IntegerAttr(getStableDPIID(
                  subroutine.getDpiCIdentifierAttr().getValue())));
          if (subroutine.getSubroutineKind() ==
              semantic::SVSubroutineKind::Task) {
            declaration->setAttr("obelisk_sim.dpi_task", builder.getUnitAttr());
            functionAttrs.push_back(builder.getNamedAttr(
                "obelisk_sim.dpi_task", builder.getUnitAttr()));
          }
          if (subroutine.getIsPure().value_or(false))
            functionAttrs.push_back(builder.getNamedAttr(
                "obelisk_sim.dpi_pure", builder.getUnitAttr()));
          if (subroutine.getIsDpiContext().value_or(false))
            functionAttrs.push_back(builder.getNamedAttr(
                "obelisk_sim.dpi_context", builder.getUnitAttr()));
        } else {
          module->setAttr("obelisk_sim.has_dpi_exports", builder.getUnitAttr());
          IntegerAttr exportID =
              builder.getI32IntegerAttr(getStableDPIID(dpiExport.getValue()));
          functionAttrs.push_back(
              builder.getNamedAttr("obelisk_sim.dpi_export_id", exportID));
          declaration->setAttr("obelisk_sim.dpi_export_id", exportID);
        }
      }
    }
    if (isa<semantic::SVPortConnectionOp>(unit.source))
      functionAttrs.push_back(
          builder.getNamedAttr("internal", builder.getUnitAttr()));
    if (auto primitive =
            unit.source->getAttrOfType<StringAttr>("primitive_name"))
      functionAttrs.push_back(
          builder.getNamedAttr("obelisk_sim.primitive_name", primitive));
    if (auto passSwitchIds = unit.source->getAttrOfType<DenseI64ArrayAttr>(
            "obelisk_sim.pass_switch_ids"))
      functionAttrs.push_back(
          builder.getNamedAttr("obelisk_sim.pass_switch_ids", passSwitchIds));
    if (auto mosTopologyIds = unit.source->getAttrOfType<DenseI64ArrayAttr>(
            "obelisk_sim.mos_topology_ids"))
      functionAttrs.push_back(
          builder.getNamedAttr("obelisk_sim.mos_topology_ids", mosTopologyIds));
    if (unit.source->hasAttr(sequenceEndpointEventAttrName)) {
      functionAttrs.push_back(builder.getNamedAttr(
          sequenceEndpointMonitorAttrName, builder.getUnitAttr()));
      functionAttrs.push_back(builder.getNamedAttr(
          sequenceEndpointPathAttrName,
          builder.getStringAttr(getHierarchyName(unit.source))));
    }
    bool clockingEventMonitor =
        isa<semantic::SVClockingBlockSymbolOp>(unit.source) &&
        (unit.source->hasAttr(clockingEventMonitorRequiredAttrName) ||
         unit.source->hasAttr(clockingEventListAttrName));
    bool timingCheckCoordinator =
        isa<semantic::SVSystemTimingCheckSymbolOp>(unit.source);
    if (clockingEventMonitor) {
      functionAttrs.push_back(builder.getNamedAttr(clockingEventMonitorAttrName,
                                                   builder.getUnitAttr()));
      functionAttrs.push_back(builder.getNamedAttr(
          clockingEventMonitorPathAttrName,
          builder.getStringAttr(getHierarchyName(unit.source))));
    }
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
    // Clocking inputs must be sampled before program-domain Reactive work.
    // Keep the shared event-list monitor in the design domain even when the
    // clocking block is declared lexically inside a program.
    bool programCodeUnit =
        !clockingEventMonitor && isProgramCodeUnit(unit.source);
    bool invariantAssertionMonitor = false;
    if (programCodeUnit && unit.entryKind == sim::EntryKind::Always)
      unit.source->walk([&](semantic::SVConcurrentAssertionStatementOp) {
        invariantAssertionMonitor = true;
        return WalkResult::interrupt();
      });
    // IEEE 1800-2017 24.3.1 gives concurrent assertions invariant scheduling
    // in program and design code: their monitor samples in Preponed and
    // evaluates in Observed. The frontend represents a static assertion item
    // as a synthetic always code unit; it is not a program process and must
    // not participate in program completion accounting. Its action actor is
    // separately scheduled in Reactive by concurrent-assertion lowering.
    bool programDomain = programCodeUnit && !invariantAssertionMonitor;
    bool programProceduralRoot = unit.entryKind == sim::EntryKind::Initial ||
                                 unit.entryKind == sim::EntryKind::Always ||
                                 unit.entryKind == sim::EntryKind::AlwaysComb ||
                                 unit.entryKind == sim::EntryKind::AlwaysFF ||
                                 unit.entryKind == sim::EntryKind::AlwaysLatch;
    if (programDomain && programProceduralRoot && !hierarchy.empty())
      functionAttrs.push_back(builder.getNamedAttr(
          "obelisk_sim.program_owner_id",
          builder.getI64IntegerAttr(stableCodeUnitID(hierarchy))));
    // Final procedures are held in the runtime's end-of-simulation phase; the
    // compute graph independently places their executable fragment in its
    // postponed plan.  Their process ABI home must remain Active even when the
    // declaration belongs to a program block.  Encoding a program final as a
    // Reactive-home final is rejected by both native and bytecode scheduling
    // because no ordinary reactive work may be introduced during finalization.
    bool finalProcedure = unit.entryKind == sim::EntryKind::Final;
    // IEEE 1800-2017 31.4.1 defines simultaneous timing-check transitions by
    // simulation time, not by an individual scheduler producer wave. Observed
    // remains the coordinator's ABI home and legal notifier-publication region;
    // numeric-lookahead checks carry a slot_final wait marker that the
    // scheduler admits only after Reactive/Re-Inactive/Re-NBA quiesce.
    sim::EventRegion homeRegion =
        timingCheckCoordinator             ? sim::EventRegion::Observed
        : invariantAssertionMonitor        ? sim::EventRegion::Observed
        : programDomain && !finalProcedure ? sim::EventRegion::Reactive
                                           : sim::EventRegion::Active;
    functionAttrs.push_back(builder.getNamedAttr(
        "home_region", sim::EventRegionAttr::get(context, homeRegion)));
    functionAttrs.push_back(builder.getNamedAttr(
        "domain", sim::ExecutionDomainAttr::get(
                      context, programDomain ? sim::ExecutionDomain::Program
                                             : sim::ExecutionDomain::Design)));
    unit.function = sim::SimFuncOp::create(
        builder, getSemanticLocation(unit.source), unit.symbol, type,
        unit.entryKind, functionAttrs, argAttrs);
    SymbolTable::setSymbolVisibility(
        unit.function, unit.source->hasAttr("dpi_export_c_identifier")
                           ? SymbolTable::Visibility::Nested
                           : SymbolTable::Visibility::Private);

    if (auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source);
        subroutine && subroutine.getIsDpiImport().value_or(false)) {
      unit.function.getBody().getBlocks().clear();
      continue;
    }

    OpBuilder bodyBuilder =
        OpBuilder::atBlockEnd(&unit.function.getBody().front());
    auto lookupCaptureArgument = [&](StringRef path) -> Value {
      auto frozenBindings =
          unit.function->getAttrOfType<ArrayAttr>(bindingsAttrName);
      if (!frozenBindings)
        return {};
      for (Attribute attribute : frozenBindings) {
        auto binding = dyn_cast<sim::ArgumentBindingAttr>(attribute);
        if (binding && binding.getKind() == sim::UnitArgumentKind::Direct &&
            binding.getPath().getValue() == path)
          return unit.function.getBody().front().getArgument(
              binding.getArgument());
      }
      return {};
    };
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
      SmallVector<Operation *> initializer =
          getNetInitializerExpressions(unit.source);
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
      auto subroutine = dyn_cast<semantic::SVSubroutineSymbolOp>(unit.source);
      auto owner =
          subroutine ? getOwningClass(subroutine) : semantic::SVClassTypeOp{};
      auto clonePropertyInitializers = [&](OpBuilder &initializerBuilder,
                                           bool nestedAfterSuper = false) {
        if (!owner)
          return;
        Value receiver = unit.function.getBody().front().getArgument(1);
        for (Operation *member : getChildren(owner)) {
          auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(member);
          if (!property ||
              property.getLifetime() == semantic::SVVariableLifetime::Static)
            continue;
          SmallVector<Operation *> initializer = getChildren(property);
          if (initializer.empty()) {
            FailureOr<Type> type = getNormalizedSemanticType(property);
            FlatSymbolRefAttr field = classFieldSymbols.lookup(property);
            if (succeeded(type) && isa<sim::EventType>(*type) && field) {
              auto event = sim::SimEventCreateOp::create(
                  initializerBuilder, getSemanticLocation(property), *type);
              auto receiverType =
                  cast<sim::ClassHandleType>(receiver.getType());
              Type referenceType = sim::ManagedRefType::get(
                  context, *type, receiverType.getClassName());
              auto reference = sim::SimClassFieldRefOp::create(
                  initializerBuilder, getSemanticLocation(property),
                  referenceType, receiver, field);
              auto store = sim::SimManagedStoreOp::create(
                  initializerBuilder, getSemanticLocation(property), event,
                  reference);
              if (nestedAfterSuper) {
                UnitAttr marker = builder.getUnitAttr();
                event->setAttr(preparedInitializerAttrName, marker);
                reference->setAttr(preparedInitializerAttrName, marker);
                store->setAttr(preparedInitializerAttrName, marker);
              }
            }
            continue;
          }
          Operation *cloned = initializerBuilder.clone(*initializer.front());
          if (FlatSymbolRefAttr field = classFieldSymbols.lookup(property))
            cloned->setAttr("obelisk_sim.initialize_field", field);
        }
      };
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
            SmallVector<Value> baseCaptures;
            Operation *baseCaptureSource =
                constructorCaptureSourceFor(base->second);
            for (const auto &capture : unitCaptures[baseCaptureSource]) {
              if (usesContextStorage(baseCaptureSource, capture))
                continue;
              Value value = lookupCaptureArgument(capture.first);
              if (!value) {
                emitError(getSemanticLocation(subroutine))
                    << "constructor has no binding for base capture: "
                    << capture.first;
                invalid = true;
                break;
              }
              baseCaptures.push_back(value);
            }
            sim::SimClassDirectCallOp::create(
                bodyBuilder, getSemanticLocation(subroutine), TypeRange{},
                baseConstructor, baseReceiver, baseCaptures);
          } else {
            emitError(getSemanticLocation(subroutine))
                << "constructor cannot resolve its base constructor";
            invalid = true;
          }
          clonePropertyInitializers(bodyBuilder);
          initialized = true;
        }
      }
      bool moveBody = subroutine && !constructor &&
                      sourceUseCounts.lookup(unit.source) == 1 &&
                      !sourcesWithNestedUnits.contains(unit.source);
      for (Operation *child : getChildren(unit.source)) {
        // Declarative children remain in the frozen semantic symbol table.
        // Cloning any Symbol into the isolated simulation function would put
        // it below an operation without the SymbolTable trait.
        if (isa<SymbolOpInterface>(child))
          continue;
        if (unit.function->hasAttr("obelisk_sim.propagation_delays") &&
            isa<semantic::SVDelayControlOp, semantic::SVDelay3ControlOp>(child))
          continue;
        Operation *clonedChild = child;
        if (moveBody)
          child->moveBefore(bodyBuilder.getInsertionBlock(),
                            bodyBuilder.getInsertionBlock()->end());
        else
          clonedChild = bodyBuilder.clone(*child);
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
            clonePropertyInitializers(initializerBuilder,
                                      /*nestedAfterSuper=*/true);
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
      // A static variable owns a separate zero-time initializer code unit,
      // invoked by the root before any process is spawned.  Re-cloning that
      // initializer into its procedural declaration would evaluate it a
      // second time on first execution (and would require a second, different
      // capture ABI).  The declaration is only a lexical occurrence of the
      // already initialized descriptor-backed object.
      if (auto variable =
              dyn_cast<semantic::SVVariableSymbolOp>(symbol->second);
          variable &&
          variable.getLifetime() == semantic::SVVariableLifetime::Static)
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

  // IEEE 1800-2017 25.7.4 defines an extern fork/join interface task as one
  // concurrent call to every elaborated provider. Materialize that contract
  // only for designs that contain such a task: the ordinary direct-call and
  // virtual-interface machinery continues to call the interface stub, while
  // its prepared body becomes a static spawn/join aggregate with no runtime
  // dispatch inventory.
  if (!preparedUnits->externForkJoinTargets.empty()) {
    [&]() LLVM_ATTRIBUTE_NOINLINE {
      llvm::DenseMap<Operation *, PreparedUnit *> unitsBySource;
      unitsBySource.reserve(units.size());
      for (PreparedUnit &unit : units)
        unitsBySource.try_emplace(unit.source, &unit);

      llvm::DenseSet<uint64_t> codeUnitIDs;
      codeUnitIDs.insert(rootCodeUnitID);
      for (const PreparedUnit &unit : units)
        codeUnitIDs.insert(unit.id);

      SmallVector<Operation *> orderedStubs;
      orderedStubs.reserve(preparedUnits->externForkJoinTargets.size());
      for (const auto &aggregation : preparedUnits->externForkJoinTargets)
        orderedStubs.push_back(aggregation.first);
      llvm::sort(orderedStubs, [](Operation *lhs, Operation *rhs) {
        return getHierarchyName(lhs) < getHierarchyName(rhs);
      });

      OpBuilder aggregateBuilder =
          OpBuilder::atBlockEnd(&design.getBody().front());
      for (Operation *stubSource : orderedStubs) {
        PreparedUnit *stubUnit = unitsBySource.lookup(stubSource);
        auto stub =
            dyn_cast_or_null<semantic::SVSubroutineSymbolOp>(stubSource);
        if (!stubUnit || !stubUnit->function ||
            stubUnit->entryKind != sim::EntryKind::Task || !stub) {
          emitError(getSemanticLocation(stubSource))
              << "interface extern fork/join task has no prepared task unit";
          invalid = true;
          continue;
        }
        sim::SimFuncOp aggregate = stubUnit->function;
        Location location = getSemanticLocation(stubSource);

        // Context and formal arguments form the shared public prefix. Direct
        // task output/inout formals additionally carry one copy-out reference.
        unsigned publicArguments = 1;
        for (Operation *child : getChildren(stubSource)) {
          auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child);
          if (!formal)
            continue;
          ++publicArguments;
          if (formal.getDirection() == semantic::SVArgumentDirection::Out ||
              formal.getDirection() == semantic::SVArgumentDirection::InOut)
            ++publicArguments;
        }
        if (aggregate.getNumArguments() < publicArguments) {
          emitError(location)
              << "interface extern fork/join task has an incomplete formal ABI";
          invalid = true;
          continue;
        }

        llvm::StringMap<unsigned> aggregateCaptures;
        if (ArrayAttr bindings =
                aggregate->getAttrOfType<ArrayAttr>(bindingsAttrName)) {
          for (Attribute attribute : bindings) {
            auto binding = dyn_cast<sim::ArgumentBindingAttr>(attribute);
            if (!binding || binding.getArgument() < publicArguments)
              continue;
            auto inserted = aggregateCaptures.try_emplace(
                binding.getPath().getValue(), binding.getArgument());
            if (!inserted.second &&
                inserted.first->second != binding.getArgument()) {
              emitError(location)
                  << "interface extern fork/join task has an ambiguous capture "
                     "binding for '"
                  << binding.getPath().getValue() << "'";
              invalid = true;
            }
          }
        }
        if (invalid)
          continue;

        SmallVector<sim::SimFuncOp> branches;
        SmallVector<SmallVector<unsigned>> branchArguments;
        auto aggregation =
            preparedUnits->externForkJoinTargets.find(stubSource);
        assert(aggregation != preparedUnits->externForkJoinTargets.end());
        ArrayRef<Operation *> providers = aggregation->second;
        branches.reserve(providers.size());
        for (auto [providerIndex, providerSource] :
             llvm::enumerate(providers)) {
          PreparedUnit *providerUnit = unitsBySource.lookup(providerSource);
          if (!providerUnit || !providerUnit->function ||
              providerUnit->entryKind != sim::EntryKind::Task) {
            emitError(getSemanticLocation(providerSource))
                << "interface extern fork/join provider has no prepared task "
                   "unit";
            invalid = true;
            break;
          }
          sim::SimFuncOp provider = providerUnit->function;
          if (provider.getNumArguments() < publicArguments) {
            emitError(getSemanticLocation(providerSource))
                << "interface extern fork/join provider has an incomplete "
                   "formal ABI";
            invalid = true;
            break;
          }
          bool incompatible = false;
          for (unsigned argument = 0; argument != publicArguments; ++argument)
            incompatible |= aggregate.getArgumentTypes()[argument] !=
                            provider.getArgumentTypes()[argument];
          if (incompatible) {
            emitError(getSemanticLocation(providerSource))
                << "interface extern fork/join provider prepared formal ABI "
                   "does not match its interface task";
            invalid = true;
            break;
          }

          llvm::DenseMap<unsigned, StringRef> providerCapturePaths;
          if (ArrayAttr bindings =
                  provider->getAttrOfType<ArrayAttr>(bindingsAttrName))
            for (Attribute attribute : bindings)
              if (auto binding = dyn_cast<sim::ArgumentBindingAttr>(attribute);
                  binding && binding.getArgument() >= publicArguments)
                providerCapturePaths.try_emplace(binding.getArgument(),
                                                 binding.getPath().getValue());

          SmallVector<unsigned> providerOperandArguments;
          providerOperandArguments.reserve(provider.getNumArguments());
          for (unsigned argument = 0; argument != publicArguments; ++argument)
            providerOperandArguments.push_back(argument);
          for (unsigned argument = publicArguments;
               argument != provider.getNumArguments(); ++argument) {
            auto path = providerCapturePaths.find(argument);
            auto aggregateArgument = path == providerCapturePaths.end()
                                         ? aggregateCaptures.end()
                                         : aggregateCaptures.find(path->second);
            if (aggregateArgument == aggregateCaptures.end() ||
                aggregate.getArgumentTypes()[aggregateArgument->second] !=
                    provider.getArgumentTypes()[argument]) {
              emitError(getSemanticLocation(providerSource))
                  << "interface extern fork/join provider capture has no "
                     "matching aggregate argument";
              invalid = true;
              break;
            }
            providerOperandArguments.push_back(aggregateArgument->second);
          }
          if (invalid)
            break;

          std::string hierarchy = (Twine(getHierarchyName(stubSource)) +
                                   ".$extern_forkjoin." + Twine(providerIndex))
                                      .str();
          uint64_t codeUnitID = stableCodeUnitID(hierarchy);
          if (!codeUnitIDs.insert(codeUnitID).second) {
            emitError(location)
                << "stable code-unit ID collision for '" << hierarchy << "'";
            invalid = true;
            break;
          }
          std::string symbol = (Twine(aggregate.getSymName()) +
                                ".extern_forkjoin." + Twine(providerIndex))
                                   .str();
          uint64_t scopeID =
              codeUnitDeclarations.lookup(stubSource).getScopeId();
          sim::SimCodeUnitDeclOp::create(
              aggregateBuilder, location, codeUnitID, scopeID,
              sim::EntryKind::Fork, aggregateBuilder.getStringAttr(hierarchy),
              aggregateBuilder.getStringAttr(
                  "interface extern fork/join branch"),
              aggregateBuilder.getUnitAttr());

          SmallVector<Type> branchInputs;
          SmallVector<DictionaryAttr> argumentAttrs;
          branchInputs.reserve(providerOperandArguments.size());
          argumentAttrs.reserve(providerOperandArguments.size());
          for (unsigned argument : providerOperandArguments) {
            branchInputs.push_back(aggregate.getArgumentTypes()[argument]);
            argumentAttrs.push_back(aggregate.getArgAttrDict(argument));
          }
          SmallVector<NamedAttribute> attributes{
              aggregateBuilder.getNamedAttr(
                  "code_unit_id",
                  aggregateBuilder.getI64IntegerAttr(codeUnitID)),
              aggregateBuilder.getNamedAttr("internal",
                                            aggregateBuilder.getUnitAttr()),
              aggregateBuilder.getNamedAttr(
                  sim::metadata::hierarchicalName,
                  aggregateBuilder.getStringAttr(hierarchy))};
          const StringRef inheritedAttributes[] = {delayScaleAttrName,
                                                   delayQuantumAttrName,
                                                   "home_region", "domain"};
          for (StringRef name : inheritedAttributes)
            if (Attribute attribute = aggregate->getAttr(name))
              attributes.push_back(
                  aggregateBuilder.getNamedAttr(name, attribute));
          sim::SimFuncOp branch = sim::SimFuncOp::create(
              aggregateBuilder, location, symbol,
              FunctionType::get(context, branchInputs, {}),
              sim::EntryKind::Fork, attributes, argumentAttrs);
          SymbolTable::setSymbolVisibility(branch,
                                           SymbolTable::Visibility::Private);
          Block &entry = branch.getBody().front();
          auto aggregateControlID = aggregate->getAttrOfType<IntegerAttr>(
              "obelisk_sim.control_target_id");
          Block *controlExit = nullptr;
          Block *callBlock = &entry;
          Value controlActivation;
          if (aggregateControlID) {
            controlExit = new Block();
            branch.getBody().push_back(controlExit);
            callBlock = new Block();
            branch.getBody().push_back(callBlock);
            OpBuilder entryBuilder = OpBuilder::atBlockEnd(&entry);
            controlActivation = sim::SimControlEnterOp::create(
                entryBuilder, location, aggregateControlID);
            sim::SimControlBoundaryOp::create(
                entryBuilder, location, controlActivation, ValueRange{},
                sim::ContinuationSiteAttr{}, controlExit, callBlock);
            OpBuilder exitBuilder = OpBuilder::atBlockEnd(controlExit);
            sim::SimReturnOp::create(exitBuilder, location, ValueRange{});
          }
          Block *continuation = new Block();
          branch.getBody().push_back(continuation);
          SmallVector<Value> providerOperands;
          providerOperands.append(entry.getArguments().begin(),
                                  entry.getArguments().end());
          OpBuilder branchBuilder = OpBuilder::atBlockEnd(callBlock);
          sim::SimTaskCallOp::create(
              branchBuilder, location,
              FlatSymbolRefAttr::get(context, provider.getSymName()),
              providerOperands,
              branchBuilder.getI64IntegerAttr(providerOperands.size()),
              sim::ContinuationSiteAttr{}, continuation);
          OpBuilder continuationBuilder = OpBuilder::atBlockEnd(continuation);
          if (controlActivation) {
            sim::SimControlLeaveOp::create(continuationBuilder, location,
                                           controlActivation);
            cf::BranchOp::create(continuationBuilder, location, controlExit);
          } else {
            sim::SimReturnOp::create(continuationBuilder, location,
                                     ValueRange{});
          }
          branch->setAttr(sim::metadata::lowered,
                          aggregateBuilder.getUnitAttr());
          branches.push_back(branch);
          branchArguments.push_back(std::move(providerOperandArguments));
        }
        if (invalid)
          continue;

        Region &body = aggregate.getBody();
        body.getBlocks().clear();
        Block *entry = new Block();
        body.push_back(entry);
        for (Type input : aggregate.getArgumentTypes())
          entry->addArgument(input, location);
        OpBuilder bodyBuilder = OpBuilder::atBlockEnd(entry);
        if (branches.empty()) {
          Value contextValue = entry->getArgument(0);
          Value descriptor = arith::ConstantOp::create(
              bodyBuilder, location, bodyBuilder.getI32Type(),
              bodyBuilder.getI32IntegerAttr(static_cast<int32_t>(0x80000002u)));
          Value message = sim::SimBytesConstantOp::create(
                              bodyBuilder, location,
                              "ERROR: interface extern fork/join task has no "
                              "implementation")
                              .getResult();
          sim::SimDisplayOp::create(
              bodyBuilder, location, contextValue, descriptor,
              ValueRange{message}, true, 10,
              bodyBuilder.getDenseI32ArrayAttr({0}),
              aggregate->getAttrOfType<StringAttr>(
                  sim::metadata::hierarchicalName),
              StringAttr{},
              aggregate->getAttrOfType<IntegerAttr>(delayScaleAttrName),
              IntegerAttr{});
          sim::SimErrorOp::create(bodyBuilder, location, contextValue);
          sim::SimReturnOp::create(bodyBuilder, location, ValueRange{});
        } else {
          SmallVector<Value> processes;
          for (auto [branch, arguments] :
               llvm::zip_equal(branches, branchArguments)) {
            SmallVector<Value> operands;
            operands.reserve(arguments.size());
            for (unsigned argument : arguments)
              operands.push_back(entry->getArgument(argument));
            processes.push_back(sim::SimSpawnOp::create(bodyBuilder, location,
                                                        branch.getSymNameAttr(),
                                                        operands, ArrayAttr{},
                                                        ArrayAttr{})
                                    .getProcess());
          }
          Block *continuation = new Block();
          body.push_back(continuation);
          sim::SimSuspendJoinOp::create(
              bodyBuilder, location, sim::JoinKind::All, processes,
              processes.size(), sim::ContinuationSiteAttr{},
              sim::EventRegionAttr{}, continuation);
          OpBuilder continuationBuilder = OpBuilder::atBlockEnd(continuation);
          sim::SimReturnOp::create(continuationBuilder, location, ValueRange{});
        }
        aggregate->setAttr(sim::metadata::lowered,
                           aggregateBuilder.getUnitAttr());
      }
    }();
  }
  if (invalid)
    return abort();

  // Plain object.randomize() has one immutable plan per target class. Generic
  // UVM code can invoke the same set of plans from many class specializations;
  // expanding every plan into every caller creates hundreds of thousands of
  // duplicate SSA operations. Materialize each plain plan once as a private
  // class helper and leave only the dynamic type tests and helper calls in the
  // original units. Calls with inline constraints, explicit property lists,
  // checker mode, or constraint restrictions remain local to their caller.
  struct SharedRandomizeAlternative {
    semantic::SVCallExpressionOp call;
    sim::SimFuncOp parent;
    unsigned depth;
  };
  llvm::StringMap<SmallVector<SharedRandomizeAlternative>> sharedPlans;
  for (PreparedUnit &unit : units) {
    if (!unit.function)
      continue;
    unit.function.walk([&](semantic::SVCallExpressionOp call) {
      bool classDispatch = call->hasAttr(randomizeDispatchAttrName);
      bool nestedDispatch = call->hasAttr(randomizeNestedDispatchAttrName);
      if ((!classDispatch && !nestedDispatch) ||
          call.getHasInlineConstraints() || call.getArgumentCount() != 1 ||
          call->hasAttr(randomizeCheckerOnlyAttrName) ||
          call->hasAttr(randomizeExplicitPropertiesAttrName))
        return;
      auto restrictions = call.getConstraintRestrictions();
      if (restrictions && !restrictions.empty())
        return;
      unsigned depth = 0;
      for (Operation *ancestor = call->getParentOp();
           ancestor && ancestor != unit.function;
           ancestor = ancestor->getParentOp())
        depth += isa<semantic::SVCallExpressionOp>(ancestor);
      for (Operation *child : getChildren(call)) {
        auto alternative = dyn_cast<semantic::SVCallExpressionOp>(child);
        auto planClass = alternative
                             ? alternative->getAttrOfType<FlatSymbolRefAttr>(
                                   randomizePlanClassAttrName)
                             : FlatSymbolRefAttr{};
        if (!planClass ||
            (!alternative->hasAttr(randomizeAttrName) &&
             !alternative->hasAttr(randomizeNestedDispatchAttrName)))
          continue;
        std::string key =
            (Twine(classDispatch ? "class|" : "nested|") + planClass.getValue())
                .str();
        if (nestedDispatch) {
          llvm::raw_string_ostream stream(key);
          stream << '|';
          if (Attribute field =
                  call->getAttr(randomizeNestedDispatchFieldAttrName))
            field.print(stream);
          if (Attribute path =
                  call->getAttr(randomizeNestedDispatchSelectionPathAttrName))
            path.print(stream);
          stream << '|';
          if (Attribute selections =
                  alternative->getAttr(randomizeNestedPlansAttrName))
            selections.print(stream);
        }
        sharedPlans[key].push_back({alternative, unit.function, depth});
      }
    });
  }

  auto bindingPath = [](Attribute binding) -> StringRef {
    return TypeSwitch<Attribute, StringRef>(binding)
        .Case<sim::ArgumentBindingAttr, sim::DescriptorBindingAttr,
              sim::LocalBindingAttr, sim::ConstantBindingAttr>(
            [](auto value) { return value.getPath().getValue(); })
        .Default([](Attribute) { return StringRef{}; });
  };
  auto bindingMap = [&](sim::SimFuncOp function) {
    llvm::StringMap<Attribute> result;
    if (auto bindings = function->getAttrOfType<ArrayAttr>(bindingsAttrName))
      for (Attribute binding : bindings)
        if (StringRef path = bindingPath(binding); !path.empty())
          result[path] = binding;
    return result;
  };
  std::function<void(Attribute, bool, llvm::StringSet<> &, llvm::StringSet<> &,
                     const llvm::StringMap<Attribute> &)>
      collectPlanStrings;
  collectPlanStrings = [&](Attribute attribute, bool readContext,
                           llvm::StringSet<> &required,
                           llvm::StringSet<> &reads,
                           const llvm::StringMap<Attribute> &bindings) {
    if (auto string = dyn_cast<StringAttr>(attribute)) {
      StringRef value = string.getValue();
      if (bindings.contains(value))
        required.insert(value);
      if (readContext)
        reads.insert(value);
      return;
    }
    if (auto array = dyn_cast<ArrayAttr>(attribute)) {
      for (Attribute element : array)
        collectPlanStrings(element, readContext, required, reads, bindings);
      return;
    }
    if (auto dictionary = dyn_cast<DictionaryAttr>(attribute))
      for (NamedAttribute named : dictionary) {
        StringRef name = named.getName().strref();
        collectPlanStrings(named.getValue(),
                           readContext || name.contains("read"), required,
                           reads, bindings);
      }
  };

  OpBuilder helperBuilder = OpBuilder::atBlockEnd(&design.getBody().front());
  SmallVector<StringRef> planOrder;
  for (auto &entry : sharedPlans)
    planOrder.push_back(entry.getKey());
  llvm::sort(planOrder, [&](StringRef lhs, StringRef rhs) {
    auto maxDepth = [&](StringRef key) {
      return llvm::max_element(sharedPlans.find(key)->second,
                               [](const SharedRandomizeAlternative &lhs,
                                  const SharedRandomizeAlternative &rhs) {
                                 return lhs.depth < rhs.depth;
                               })
          ->depth;
    };
    unsigned lhsDepth = maxDepth(lhs);
    unsigned rhsDepth = maxDepth(rhs);
    return lhsDepth != rhsDepth ? lhsDepth > rhsDepth : lhs < rhs;
  });
  for (StringRef key : planOrder) {
    auto &entry = *sharedPlans.find(key);
    if (entry.second.size() < 2)
      continue;
    SharedRandomizeAlternative representative = entry.second.front();
    auto parentBindings = bindingMap(representative.parent);
    llvm::StringSet<> requiredPaths;
    llvm::StringSet<> readPaths;
    SmallVector<Operation *> children = getChildren(representative.call);
    auto receiverIndex = representative.call->getAttrOfType<IntegerAttr>(
        randomReceiverIndexAttrName);
    if (!receiverIndex || receiverIndex.getValue().isNegative() ||
        receiverIndex.getValue().getActiveBits() > 64 ||
        receiverIndex.getValue().getZExtValue() >= children.size())
      continue;
    Operation *receiver = children[receiverIndex.getValue().getZExtValue()];
    representative.call->walk([&](Operation *nested) {
      if (nested == receiver)
        return WalkResult::skip();
      for (NamedAttribute named : nested->getAttrs()) {
        StringRef name = named.getName().strref();
        collectPlanStrings(named.getValue(), name.contains("read"),
                           requiredPaths, readPaths, parentBindings);
      }
      return WalkResult::advance();
    });
    // Frozen dispatch metadata may repeat the receiver's lexical path on the
    // plan operation itself. The helper's explicit class argument replaces
    // every such occurrence, so it is not a caller-local capture.
    receiver->walk([&](Operation *nested) {
      if (auto named = dyn_cast<semantic::SVNamedValueExpressionOp>(nested))
        requiredPaths.erase(named.getReferencedPath());
      else if (auto hierarchical =
                   dyn_cast<semantic::SVHierarchicalValueExpressionOp>(nested))
        requiredPaths.erase(hierarchical.getReferencedPath());
    });

    SmallVector<StringRef> orderedPaths;
    for (const auto &path : requiredPaths)
      orderedPaths.push_back(path.getKey());
    llvm::sort(orderedPaths);
    bool supported = true;
    for (StringRef path : orderedPaths) {
      Attribute binding = parentBindings.lookup(path);
      auto argument = dyn_cast_or_null<sim::ArgumentBindingAttr>(binding);
      if (isa_and_nonnull<sim::LocalBindingAttr>(binding) ||
          (argument && (argument.getKind() != sim::UnitArgumentKind::Direct ||
                        argument.getCopyOut()))) {
        supported = false;
        break;
      }
    }
    if (!supported)
      continue;

    // Every caller must expose the same capture ABI. Context descriptors and
    // constants need no explicit operand; direct captures must agree in type.
    for (SharedRandomizeAlternative &candidate : entry.second) {
      auto candidateBindings = bindingMap(candidate.parent);
      for (StringRef path : orderedPaths) {
        Attribute expected = parentBindings.lookup(path);
        Attribute actual = candidateBindings.lookup(path);
        if (!actual || actual.getTypeID() != expected.getTypeID()) {
          supported = false;
          break;
        }
        auto expectedArgument = dyn_cast<sim::ArgumentBindingAttr>(expected);
        auto actualArgument = dyn_cast<sim::ArgumentBindingAttr>(actual);
        if (!expectedArgument && actual != expected) {
          supported = false;
          break;
        }
        if (expectedArgument &&
            (candidate.parent.getFunctionType().getInput(
                 actualArgument.getArgument()) !=
                 representative.parent.getFunctionType().getInput(
                     expectedArgument.getArgument()) ||
             actualArgument.getKind() != expectedArgument.getKind() ||
             actualArgument.getCopyOut() != expectedArgument.getCopyOut() ||
             actualArgument.getLvalueNode() !=
                 expectedArgument.getLvalueNode() ||
             candidate.parent.getArgAttrDict(actualArgument.getArgument()) !=
                 representative.parent.getArgAttrDict(
                     expectedArgument.getArgument()))) {
          supported = false;
          break;
        }
      }
      if (!supported)
        break;
    }
    if (!supported)
      continue;

    auto planClass = representative.call->getAttrOfType<FlatSymbolRefAttr>(
        randomizePlanClassAttrName);
    FailureOr<Type> resultType = getNormalizedSemanticType(representative.call);
    if (!planClass || failed(resultType)) {
      invalid = true;
      break;
    }
    Type receiverType = sim::ClassHandleType::get(context, planClass);
    SmallVector<Type> inputs{sim::ContextType::get(context), receiverType};
    SmallVector<DictionaryAttr> argumentAttrs{
        captureMetadata(builder, sim::CaptureKind::Context),
        captureMetadata(builder, sim::CaptureKind::Formal)};
    SmallVector<Attribute> helperBindings;
    SmallVector<Attribute> capturePaths;
    for (StringRef path : orderedPaths) {
      Attribute binding = parentBindings.lookup(path);
      if (isa<sim::DescriptorBindingAttr, sim::ConstantBindingAttr>(binding)) {
        helperBindings.push_back(binding);
        continue;
      }
      auto argument = cast<sim::ArgumentBindingAttr>(binding);
      unsigned oldIndex = argument.getArgument();
      unsigned newIndex = inputs.size();
      inputs.push_back(
          representative.parent.getFunctionType().getInput(oldIndex));
      argumentAttrs.push_back(representative.parent.getArgAttrDict(oldIndex));
      helperBindings.push_back(sim::ArgumentBindingAttr::get(
          context, argument.getPath(), newIndex, argument.getKind(),
          /*copyOut=*/false, argument.getLvalueNode(), argument.getCopyIn()));
      capturePaths.push_back(builder.getStringAttr(path));
    }

    SmallVector<StringRef> orderedReads;
    for (const auto &path : readPaths)
      orderedReads.push_back(path.getKey());
    llvm::sort(orderedReads);
    SmallVector<Attribute> readCapturePaths;
    for (StringRef path : orderedReads)
      readCapturePaths.push_back(builder.getStringAttr(path));

    uint64_t keyID = stableCodeUnitID(key);
    std::string symbol = (Twine("__obelisk_randomize_plan_") +
                          planClass.getValue() + "_" + Twine(keyID))
                             .str();
    std::string hierarchy =
        (Twine("$randomize::") + planClass.getValue() + "::" + Twine(keyID))
            .str();
    uint64_t codeUnitID = stableCodeUnitID(hierarchy);
    sim::SimCodeUnitDeclOp::create(
        helperBuilder, getSemanticLocation(representative.call), codeUnitID,
        uint64_t{0}, sim::EntryKind::Function,
        helperBuilder.getStringAttr(hierarchy),
        helperBuilder.getStringAttr("shared object randomization plan"),
        UnitAttr{});
    SmallVector<NamedAttribute> attrs{
        helperBuilder.getNamedAttr(bindingsAttrName,
                                   helperBuilder.getArrayAttr(helperBindings)),
        helperBuilder.getNamedAttr("code_unit_id",
                                   helperBuilder.getI64IntegerAttr(codeUnitID)),
        helperBuilder.getNamedAttr(sim::metadata::thisArgument,
                                   helperBuilder.getI32IntegerAttr(1)),
        helperBuilder.getNamedAttr(
            "home_region",
            sim::EventRegionAttr::get(context, sim::EventRegion::Active)),
        helperBuilder.getNamedAttr("domain",
                                   sim::ExecutionDomainAttr::get(
                                       context, sim::ExecutionDomain::Design)),
        helperBuilder.getNamedAttr(sim::metadata::hierarchicalName,
                                   helperBuilder.getStringAttr(hierarchy))};
    auto helper = sim::SimFuncOp::create(
        helperBuilder, getSemanticLocation(representative.call), symbol,
        FunctionType::get(context, inputs, {*resultType}),
        sim::EntryKind::Function, attrs, argumentAttrs);
    SymbolTable::setSymbolVisibility(helper, SymbolTable::Visibility::Private);
    OpBuilder bodyBuilder = OpBuilder::atBlockEnd(&helper.getBody().front());
    OperationState returnState(
        getSemanticLocation(representative.call),
        semantic::SVReturnStatementOp::getOperationName());
    returnState.addAttribute("node_id", helperBuilder.getI64IntegerAttr(
                                            representative.call.getNodeId()));
    returnState.addRegion();
    Operation *returnOp = bodyBuilder.create(returnState);
    returnOp->getRegion(0).emplaceBlock();
    OpBuilder returnBuilder =
        OpBuilder::atBlockEnd(&returnOp->getRegion(0).front());
    auto cloned = cast<semantic::SVCallExpressionOp>(
        returnBuilder.clone(*representative.call));
    cloned->setAttr(randomizeHelperReceiverAttrName,
                    helperBuilder.getUnitAttr());
    auto placeholder = UnrealizedConversionCastOp::create(
        bodyBuilder, getSemanticLocation(representative.call),
        TypeRange{*resultType}, ValueRange{});
    placeholder->setAttr(placeholderAttrName, helperBuilder.getUnitAttr());
    sim::SimReturnOp::create(bodyBuilder,
                             getSemanticLocation(representative.call),
                             placeholder.getResults());

    FlatSymbolRefAttr helperRef =
        FlatSymbolRefAttr::get(context, helper.getSymName());
    ArrayAttr captures = helperBuilder.getArrayAttr(capturePaths);
    ArrayAttr reads = helperBuilder.getArrayAttr(readCapturePaths);
    for (SharedRandomizeAlternative &candidate : entry.second) {
      candidate.call->setAttr(randomizeHelperAttrName, helperRef);
      candidate.call->setAttr(randomizeHelperCapturesAttrName, captures);
      candidate.call->setAttr(randomizeHelperReadCapturesAttrName, reads);
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
    SmallVector<Type> inputs{sim::ContextType::get(context), receiverType};
    SmallVector<DictionaryAttr> argAttrs{
        captureMetadata(builder, sim::CaptureKind::Context),
        captureMetadata(builder, sim::CaptureKind::Formal)};
    SmallVector<Attribute> bindings;
    for (const auto &capture : unitCaptures[classType]) {
      if (usesContextStorage(classType, capture)) {
        bindings.push_back(sim::DescriptorBindingAttr::get(
            context, builder.getStringAttr(capture.first), capture.second.id,
            sim::RefType::get(context, capture.second.type)));
        continue;
      }
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
      unsigned argument = inputs.size();
      inputs.push_back(handleType);
      DictionaryAttr metadata =
          captureMetadata(builder, captureKind, capture.second.id);
      SmallVector<NamedAttribute> metadataAttrs(metadata.begin(),
                                                metadata.end());
      if (capture.second.kind == DescriptorInfo::Kind::Driver &&
          capture.second.delayedNet)
        metadataAttrs.push_back(builder.getNamedAttr("obelisk_sim.delayed_net",
                                                     builder.getUnitAttr()));
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
      bindings.push_back(sim::ArgumentBindingAttr::get(
          context, builder.getStringAttr(capture.first), argument,
          sim::UnitArgumentKind::Direct, /*copyOut=*/false, IntegerAttr{},
          /*copyIn=*/true));
    }
    for (const PreparedConstant &constant : unitConstants[classType])
      bindings.push_back(sim::ConstantBindingAttr::get(
          context, builder.getStringAttr(constant.path), constant.value));
    FunctionType type = FunctionType::get(context, inputs, {});
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
        builder.getNamedAttr(sim::metadata::thisArgument,
                             builder.getI32IntegerAttr(1)),
        builder.getNamedAttr("obelisk_sim.constructor", builder.getUnitAttr()),
        builder.getNamedAttr(bindingsAttrName, builder.getArrayAttr(bindings)),
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
    llvm::StringMap<Value> captureValues;
    unsigned captureArgument = 2;
    for (const auto &capture : unitCaptures[classType]) {
      if (usesContextStorage(classType, capture))
        continue;
      captureValues[capture.first] =
          function.getBody().front().getArgument(captureArgument++);
    }

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
        } else if (semantic::SVSubroutineSymbolOp baseConstructorSource =
                       constructorSourceFor(base->second)) {
          SmallVector<Operation *> defaults;
          bool defaultsValid = true;
          for (Operation *member : getChildren(baseConstructorSource)) {
            auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(member);
            if (!formal)
              continue;
            SmallVector<Operation *> initializer = getChildren(formal);
            if (initializer.size() != 1) {
              emitError(getSemanticLocation(classType))
                  << "implicit constructor requires a default for every "
                     "base-constructor formal";
              invalid = true;
              defaultsValid = false;
              break;
            }
            defaults.push_back(initializer.front());
          }
          if (defaultsValid) {
            OperationState callState(
                getSemanticLocation(classType),
                semantic::SVCallExpressionOp::getOperationName());
            callState.addAttribute(
                "node_id", builder.getI64IntegerAttr(classType.getNodeId()));
            callState.addAttribute(
                "semantic_type",
                TypeAttr::get(semantic::VoidType::get(context)));
            callState.addAttribute("callee_name", builder.getStringAttr("new"));
            callState.addAttribute("is_system_call",
                                   builder.getBoolAttr(false));
            callState.addAttribute(
                "subroutine_kind",
                semantic::SVSubroutineKindAttr::get(
                    context, baseConstructorSource.getSubroutineKind()));
            callState.addAttribute("argument_count",
                                   builder.getI64IntegerAttr(defaults.size()));
            callState.addAttribute("has_this_class",
                                   builder.getBoolAttr(false));
            callState.addAttribute("is_super_class", builder.getBoolAttr(true));
            callState.addAttribute("has_output_arguments",
                                   builder.getBoolAttr(false));
            callState.addAttribute(
                "referenced_path",
                builder.getStringAttr(getHierarchyName(baseConstructorSource)));
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
            freezeCallContract(call);
            call->setAttr("obelisk_sim.class_super", builder.getUnitAttr());
          }
        } else {
          Value baseReceiver = sim::SimClassCastOp::create(
              bodyBuilder, getSemanticLocation(classType), baseReceiverType,
              receiver);
          SmallVector<Value> baseCaptures;
          Operation *baseCaptureSource =
              constructorCaptureSourceFor(base->second);
          for (const auto &capture : unitCaptures[baseCaptureSource]) {
            if (usesContextStorage(baseCaptureSource, capture))
              continue;
            Value value = captureValues.lookup(capture.first);
            if (!value) {
              emitError(getSemanticLocation(classType))
                  << "implicit constructor has no binding for base capture: "
                  << capture.first;
              invalid = true;
              break;
            }
            baseCaptures.push_back(value);
          }
          sim::SimClassDirectCallOp::create(
              bodyBuilder, getSemanticLocation(classType), TypeRange{},
              baseConstructor, baseReceiver, baseCaptures);
        }
      }
    }

    for (Operation *member : getChildren(classType)) {
      auto property = dyn_cast<semantic::SVClassPropertySymbolOp>(member);
      if (!property ||
          property.getLifetime() == semantic::SVVariableLifetime::Static)
        continue;
      SmallVector<Operation *> initializer = getChildren(property);
      if (initializer.empty()) {
        FailureOr<Type> type = getNormalizedSemanticType(property);
        FlatSymbolRefAttr field = classFieldSymbols.lookup(property);
        if (succeeded(type) && isa<sim::EventType>(*type) && field) {
          Value event = sim::SimEventCreateOp::create(
              bodyBuilder, getSemanticLocation(property), *type);
          auto receiverType = cast<sim::ClassHandleType>(receiver.getType());
          Type referenceType = sim::ManagedRefType::get(
              context, *type, receiverType.getClassName());
          Value reference = sim::SimClassFieldRefOp::create(
              bodyBuilder, getSemanticLocation(property), referenceType,
              receiver, field);
          sim::SimManagedStoreOp::create(
              bodyBuilder, getSemanticLocation(property), event, reference);
        }
        continue;
      }
      Operation *cloned = bodyBuilder.clone(*initializer.front());
      if (FlatSymbolRefAttr field = classFieldSymbols.lookup(property))
        cloned->setAttr("obelisk_sim.initialize_field", field);
    }
    function.walk(
        [&](semantic::SVCallExpressionOp call) { freezeCallContract(call); });
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
      sim::SimClassMethodDeclOp declaration = sim::SimClassMethodDeclOp::create(
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
      // Class methods are internal to the closed elaborated design. Direct
      // calls name their descriptor explicitly; virtual calls receive the
      // complete compatible target set below. This lets ordinary symbol DCE
      // discard unused method families before per-function lowering.
      SymbolTable::setSymbolVisibility(declaration,
                                       SymbolTable::Visibility::Private);
    }
  }
  if (invalid)
    return abort();

  // IEEE 1800-2017 6.6.7: a user-defined net is atomic, and a change to any
  // raw driver schedules evaluation of its optional resolution function in
  // Active (or Reactive for program-originated updates). Materialize one
  // compact continuous process per net. Ordinary packed nets never enter this
  // path, preserving their vectorized and bitwise resolver fast paths.
  {
    DenseMap<uint64_t, sim::SimNetDeclOp> netDeclarations;
    DenseMap<uint64_t, SmallVector<sim::SimDriverDeclOp>> netDrivers;
    for (sim::SimNetDeclOp net :
         design.getBody().front().getOps<sim::SimNetDeclOp>())
      netDeclarations[net.getId()] = net;
    for (sim::SimDriverDeclOp driver :
         design.getBody().front().getOps<sim::SimDriverDeclOp>())
      netDrivers[driver.getNetId()].push_back(driver);
    for (auto &[id, drivers] : netDrivers)
      llvm::sort(drivers,
                 [](sim::SimDriverDeclOp lhs, sim::SimDriverDeclOp rhs) {
                   return lhs.getId() < rhs.getId();
                 });

    // Port elaboration represents one physical net as several declarations
    // joined by bitwise connectivity runs. Resolution, however, belongs to
    // the complete user-net component. Collapse declarations here only for
    // generated user resolution; the ordinary packed-net topology remains
    // unchanged and keeps its optimized bitwise representation.
    DenseMap<uint64_t, uint64_t> componentParent;
    for (auto [id, declaration] : netDeclarations)
      componentParent[id] = id;
    std::function<uint64_t(uint64_t)> findComponent =
        [&](uint64_t id) -> uint64_t {
      uint64_t parent = componentParent.lookup(id);
      if (parent == id)
        return id;
      return componentParent[id] = findComponent(parent);
    };
    auto joinComponent = [&](uint64_t lhs, uint64_t rhs) {
      if (!netDeclarations.contains(lhs) || !netDeclarations.contains(rhs))
        return;
      uint64_t lhsRoot = findComponent(lhs);
      uint64_t rhsRoot = findComponent(rhs);
      if (lhsRoot != rhsRoot)
        componentParent[std::max(lhsRoot, rhsRoot)] =
            std::min(lhsRoot, rhsRoot);
    };
    for (sim::SimNetConnectDeclOp connection :
         design.getBody().front().getOps<sim::SimNetConnectDeclOp>())
      joinComponent(connection.getLhsNetId(), connection.getRhsNetId());
    DenseMap<uint64_t, SmallVector<uint64_t>> componentMembers;
    for (auto [id, declaration] : netDeclarations)
      componentMembers[findComponent(id)].push_back(id);
    for (auto &[root, members] : componentMembers)
      llvm::sort(members);

    llvm::DenseSet<uint64_t> reactiveDriverIDs;
    for (PreparedUnit &unit : units) {
      if (!unit.function ||
          unit.function.getHomeRegion() != sim::EventRegion::Reactive)
        continue;
      for (unsigned index = 1; index < unit.function.getNumArguments();
           ++index) {
        if (!isa<sim::DriverType>(unit.function.getArgumentTypes()[index]))
          continue;
        auto descriptor = unit.function.getArgAttrOfType<IntegerAttr>(
            index, sim::metadata::descriptorId);
        if (descriptor)
          reactiveDriverIDs.insert(descriptor.getValue().getZExtValue());
      }
    }

    uint64_t generatedOrdinal = 0;
    SmallVector<PreparedUnit> generatedUnits;
    for (auto &[componentRoot, members] : componentMembers) {
      sim::SimNetDeclOp net;
      for (uint64_t member : members) {
        sim::SimNetDeclOp candidate = netDeclarations.lookup(member);
        if (candidate->hasAttr("obelisk_sim.user_defined_net")) {
          net = candidate;
          break;
        }
      }
      if (!net)
        continue;
      SmallVector<sim::SimDriverDeclOp> componentDrivers;
      for (uint64_t member : members)
        llvm::append_range(componentDrivers, netDrivers[member]);
      llvm::sort(componentDrivers,
                 [](sim::SimDriverDeclOp lhs, sim::SimDriverDeclOp rhs) {
                   return lhs.getId() < rhs.getId();
                 });
      ArrayRef<sim::SimDriverDeclOp> drivers = componentDrivers;
      bool hasReactiveDriver = llvm::any_of(drivers, [&](auto driver) {
        return reactiveDriverIDs.contains(driver.getId());
      });
      bool hasActiveDriver = llvm::any_of(drivers, [&](auto driver) {
        return !reactiveDriverIDs.contains(driver.getId());
      });
      bool mixedDriverRegions = hasActiveDriver && hasReactiveDriver;
      sim::EventRegion primaryRegion = hasActiveDriver
                                           ? sim::EventRegion::Active
                                           : sim::EventRegion::Reactive;
      auto resolutionPath = net->getAttrOfType<StringAttr>(
          "obelisk_sim.resolution_function_path");
      auto resolutionSymbol = net->getAttrOfType<SymbolRefAttr>(
          "obelisk_sim.resolution_function_symbol");
      sim::SimFuncOp resolutionFunction;
      Operation *resolutionSource = nullptr;
      if (resolutionPath) {
        auto semantic =
            resolutionSymbol
                ? semanticSymbols.find(resolutionSymbol.getLeafReference())
                : semanticSymbols.end();
        resolutionSource =
            semantic == semanticSymbols.end() ? nullptr : semantic->second;
        auto resolutionSubroutine =
            dyn_cast_or_null<semantic::SVSubroutineSymbolOp>(resolutionSource);
        if (resolutionSubroutine &&
            resolutionSubroutine.getDefaultLifetime() !=
                semantic::SVVariableLifetime::Automatic) {
          emitError(getSemanticLocation(resolutionSource))
              << "user-defined net resolution function '"
              << resolutionPath.getValue() << "' must be automatic";
          invalid = true;
          continue;
        }
        resolutionFunction = resolutionSource
                                 ? unitFunctions.lookup(resolutionSource)
                                 : sim::SimFuncOp{};
        if (!resolutionFunction) {
          net.emitError() << "user-defined net resolution function '"
                          << resolutionPath.getValue()
                          << "' has no executable code unit";
          invalid = true;
          continue;
        }
        semantic::SVFormalArgumentSymbolOp driverArgument;
        for (Operation *child : getChildren(resolutionSource))
          if (auto formal =
                  dyn_cast<semantic::SVFormalArgumentSymbolOp>(child)) {
            driverArgument = formal;
            break;
          }
        bool mutatesDriverArray = false;
        if (driverArgument) {
          StringAttr argumentSymbol = driverArgument.getSymNameAttr();
          resolutionFunction.walk([&](Operation *nested) {
            SymbolRefAttr reference;
            if (auto named =
                    dyn_cast<semantic::SVNamedValueExpressionOp>(nested))
              reference = named.getReferencedSymbol();
            else if (auto hierarchical =
                         dyn_cast<semantic::SVHierarchicalValueExpressionOp>(
                             nested))
              reference = hierarchical.getReferencedSymbol();
            if (!reference ||
                reference.getLeafReference() != argumentSymbol.getValue())
              return;
            if (isWrittenResolutionReference(nested)) {
              mutatesDriverArray = true;
              return;
            }
            auto call = nested->getParentOfType<semantic::SVCallExpressionOp>();
            if (call && call.getIsSystemCall() &&
                call.getCalleeName() == "delete")
              mutatesDriverArray = true;
          });
        }
        if (mutatesDriverArray) {
          emitError(getSemanticLocation(resolutionSource))
              << "user-defined net resolution function '"
              << resolutionPath.getValue()
              << "' writes or resizes its driver-value input array";
          invalid = true;
          continue;
        }
        auto written = unitWrittenCaptures.find(resolutionSource);
        if (written != unitWrittenCaptures.end() && !written->second.empty()) {
          emitError(getSemanticLocation(resolutionSource))
              << "user-defined net resolution function '"
              << resolutionPath.getValue()
              << "' preserves state or has side effects through design "
                 "storage";
          invalid = true;
          continue;
        }
        llvm::DenseSet<Operation *> visitedFunctions;
        std::function<bool(Operation *)> hasSemanticSideEffect =
            [&](Operation *source) {
              if (!source || !visitedFunctions.insert(source).second)
                return false;
              Operation *body = source;
              if (sim::SimFuncOp lowered = unitFunctions.lookup(source))
                body = lowered;
              bool sideEffect = false;
              body->walk([&](semantic::SVCallExpressionOp call) {
                if (sideEffect)
                  return;
                if (call.getIsSystemCall()) {
                  sideEffect =
                      isStatefulResolutionSystemCall(call.getCalleeName());
                  return;
                }
                Operation *target = resolveDirectCallee(call);
                auto subroutine =
                    dyn_cast_or_null<semantic::SVSubroutineSymbolOp>(target);
                if (!subroutine) {
                  // An unresolved user call cannot be proven side-effect-free.
                  sideEffect = true;
                  return;
                }
                if (subroutine.getIsDpiImport().value_or(false)) {
                  sideEffect = !subroutine.getIsPure().value_or(false);
                  return;
                }
                if (subroutine.getSubroutineKind() ==
                    semantic::SVSubroutineKind::Task) {
                  sideEffect = true;
                  return;
                }
                sideEffect = hasSemanticSideEffect(target);
              });
              return sideEffect;
            };
        if (hasSemanticSideEffect(resolutionSource)) {
          emitError(getSemanticLocation(resolutionSource))
              << "user-defined net resolution function '"
              << resolutionPath.getValue()
              << "' has a stateful or externally visible side effect";
          invalid = true;
          continue;
        }
      } else if (drivers.size() > 1) {
        net.emitError() << "unresolved user-defined net has multiple drivers";
        invalid = true;
        continue;
      }
      if (!resolutionFunction && drivers.empty())
        continue;
      for (uint64_t member : members)
        netDeclarations.lookup(member)->removeAttr(
            "obelisk_sim.resolution_function_symbol");

      llvm::DenseSet<uint64_t> rawDriverIDs;
      for (sim::SimDriverDeclOp driver : drivers)
        rawDriverIDs.insert(driver.getId());
      for (PreparedUnit &unit : units)
        for (unsigned index = 1; index < unit.function.getNumArguments();
             ++index) {
          auto descriptor = unit.function.getArgAttrOfType<IntegerAttr>(
              index, sim::metadata::descriptorId);
          if (isa<sim::DriverType>(unit.function.getArgumentTypes()[index]) &&
              descriptor &&
              rawDriverIDs.contains(descriptor.getValue().getZExtValue()))
            unit.function.setArgAttr(index, "obelisk_sim.user_net_driver",
                                     builder.getUnitAttr());
        }

      std::string hierarchy =
          (net.getHierarchicalName().value_or(StringRef{"$user_net"}) +
           (mixedDriverRegions ? ".$resolution.active" : ".$resolution"))
              .str();
      uint64_t codeUnitID = stableCodeUnitID(hierarchy);
      if (llvm::any_of(units,
                       [&](const PreparedUnit &unit) {
                         return unit.id == codeUnitID;
                       }) ||
          codeUnitID == rootCodeUnitID) {
        net.emitError() << "stable code-unit ID collision for '" << hierarchy
                        << "'";
        invalid = true;
        continue;
      }
      std::string symbol =
          (Twine("__obelisk_user_net_resolver_") + Twine(generatedOrdinal++))
              .str();
      sim::SimCodeUnitDeclOp::create(
          builder, net.getLoc(), codeUnitID, net.getScopeId(),
          sim::EntryKind::Continuous, builder.getStringAttr(hierarchy),
          builder.getStringAttr("user-defined net resolution"), UnitAttr{});

      SmallVector<Type> inputTypes{sim::ContextType::get(context)};
      SmallVector<DictionaryAttr> argumentAttrs{
          captureMetadata(builder, sim::CaptureKind::Context)};
      for (uint64_t member : members) {
        sim::SimNetDeclOp memberNet = netDeclarations.lookup(member);
        if (memberNet.getType() != net.getType()) {
          memberNet.emitError()
              << "connected user-defined net component has incompatible "
                 "member type "
              << memberNet.getType() << "; expected " << net.getType();
          invalid = true;
          break;
        }
        inputTypes.push_back(sim::NetType::get(context, net.getType()));
        argumentAttrs.push_back(
            captureMetadata(builder, sim::CaptureKind::Net, member));
      }
      if (invalid)
        continue;
      for (sim::SimDriverDeclOp driver : drivers) {
        inputTypes.push_back(sim::DriverType::get(context, net.getType()));
        NamedAttrList attrs(
            captureMetadata(builder, sim::CaptureKind::Driver, driver.getId()));
        attrs.set("obelisk_sim.user_net_driver", builder.getUnitAttr());
        argumentAttrs.push_back(attrs.getDictionary(context));
      }
      SmallVector<NamedAttribute> functionAttrs{
          builder.getNamedAttr("code_unit_id",
                               builder.getI64IntegerAttr(codeUnitID)),
          builder.getNamedAttr(
              "home_region", sim::EventRegionAttr::get(context, primaryRegion)),
          builder.getNamedAttr("domain",
                               sim::ExecutionDomainAttr::get(
                                   context, sim::ExecutionDomain::Design)),
          builder.getNamedAttr(sim::metadata::hierarchicalName,
                               builder.getStringAttr(hierarchy)),
          builder.getNamedAttr(sim::metadata::lowered, builder.getUnitAttr())};
      auto resolver = sim::SimFuncOp::create(
          builder, net.getLoc(), symbol,
          FunctionType::get(context, inputTypes, TypeRange{}),
          sim::EntryKind::Continuous, functionAttrs, argumentAttrs);
      Block *entry = &resolver.getBody().front();
      Block *loop = new Block;
      resolver.getBody().push_back(loop);
      OpBuilder entryBuilder = OpBuilder::atBlockEnd(entry);
      Value contributionArray;
      if (resolutionFunction) {
        Type arrayType = sim::DynamicArrayType::get(context, net.getType());
        FailureOr<ContainerElementDescriptor> descriptor =
            describeContainerElement(net.getType(), net.getLoc());
        if (failed(descriptor)) {
          invalid = true;
          resolver.erase();
          continue;
        }
        Value size = arith::ConstantOp::create(
            entryBuilder, net.getLoc(), entryBuilder.getI64Type(),
            entryBuilder.getI64IntegerAttr(drivers.size()));
        contributionArray = sim::SimContainerCreateOp::create(
            entryBuilder, net.getLoc(), arrayType, size, descriptor->typeID,
            descriptor->kind, descriptor->flags, descriptor->valueSize,
            descriptor->alignment, descriptor->bitWidth,
            entryBuilder.getDenseI64ArrayAttr(descriptor->traceOffsets),
            entryBuilder.getDenseI32ArrayAttr(descriptor->traceKinds),
            OBELISK_RT_CONTAINER_DYNAMIC_ARRAY, uint64_t{0});
      }
      cf::BranchOp::create(entryBuilder, net.getLoc(), loop);
      OpBuilder resolverBuilder = OpBuilder::atBlockBegin(loop);
      Value runtimeContext = entry->getArgument(0);
      SmallVector<Value> netHandles;
      for (unsigned index = 0; index != members.size(); ++index)
        netHandles.push_back(entry->getArgument(index + 1));
      SmallVector<Value> driverHandles;
      SmallVector<Value> contributions;
      for (unsigned index = 0; index != drivers.size(); ++index) {
        Value handle = entry->getArgument(index + 1 + members.size());
        bool reactive =
            reactiveDriverIDs.contains(componentDrivers[index].getId());
        if ((primaryRegion == sim::EventRegion::Reactive) == reactive)
          driverHandles.push_back(handle);
        contributions.push_back(sim::SimDriverReadOp::create(
            resolverBuilder, net.getLoc(), net.getType(), handle));
      }

      Value resolved;
      if (!resolutionFunction) {
        resolved = contributions.front();
      } else {
        for (auto [index, contribution] : llvm::enumerate(contributions)) {
          Value ordinal = arith::ConstantOp::create(
              resolverBuilder, net.getLoc(), resolverBuilder.getI64Type(),
              resolverBuilder.getI64IntegerAttr(index));
          sim::SimContainerWriteOp::create(resolverBuilder, net.getLoc(),
                                           contributionArray, ordinal,
                                           contribution);
        }
        SmallVector<Value> operands{runtimeContext, contributionArray};
        // Resolution functions are required to be automatic and side-effect
        // free. Constant-free functions therefore need no additional state;
        // accept direct descriptor captures as well so legal package reads do
        // not force a runtime callback ABI.
        for (unsigned index = 2; index < resolutionFunction.getNumArguments();
             ++index) {
          DictionaryAttr attrs = resolutionFunction.getArgAttrDict(index);
          auto kind = dyn_cast_or_null<sim::CaptureKindAttr>(
              attrs ? attrs.get(sim::metadata::captureKind) : Attribute{});
          auto descriptorID =
              attrs ? attrs.getAs<IntegerAttr>(sim::metadata::descriptorId)
                    : IntegerAttr{};
          if (!kind || !descriptorID) {
            resolutionFunction.emitError()
                << "resolution-function capture #" << index
                << " is not a materializable descriptor";
            invalid = true;
            break;
          }
          uint64_t capturedID = descriptorID.getValue().getZExtValue();
          Type capturedType = resolutionFunction.getArgumentTypes()[index];
          Value capture;
          switch (kind.getValue()) {
          case sim::CaptureKind::Storage:
            capture = sim::SimContextStorageOp::create(
                resolverBuilder, net.getLoc(), capturedType, runtimeContext,
                resolverBuilder.getI64IntegerAttr(capturedID));
            break;
          case sim::CaptureKind::Net:
            capture = sim::SimContextNetOp::create(
                resolverBuilder, net.getLoc(), capturedType, runtimeContext,
                resolverBuilder.getI64IntegerAttr(capturedID));
            break;
          case sim::CaptureKind::Driver:
            capture = sim::SimContextDriverOp::create(
                resolverBuilder, net.getLoc(), capturedType, runtimeContext,
                resolverBuilder.getI64IntegerAttr(capturedID));
            break;
          case sim::CaptureKind::Event:
            capture = sim::SimContextEventOp::create(
                resolverBuilder, net.getLoc(), capturedType, runtimeContext,
                resolverBuilder.getI64IntegerAttr(capturedID));
            break;
          case sim::CaptureKind::Context:
          case sim::CaptureKind::Formal:
          case sim::CaptureKind::Value:
            resolutionFunction.emitError()
                << "resolution-function capture #" << index
                << " has an unsupported binding kind";
            invalid = true;
            break;
          }
          if (capture)
            operands.push_back(capture);
        }
        if (invalid) {
          resolver.erase();
          continue;
        }
        auto call = sim::SimCallOp::create(
            resolverBuilder, net.getLoc(), TypeRange{net.getType()},
            FlatSymbolRefAttr::get(context, resolutionFunction.getSymName()),
            operands, ArrayAttr{}, ArrayAttr{});
        resolved = call.getResult(0);
      }
      for (Value netHandle : netHandles)
        sim::SimNetWriteOp::create(resolverBuilder, net.getLoc(), netHandle,
                                   resolved);
      if (driverHandles.empty()) {
        sim::SimReturnOp::create(resolverBuilder, net.getLoc(), ValueRange{});
      } else if (driverHandles.size() == 1) {
        sim::SimSuspendChangeOp::create(
            resolverBuilder, net.getLoc(), driverHandles.front(), ValueRange{},
            sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, loop);
      } else {
        SmallVector<int32_t> edges(driverHandles.size(),
                                   static_cast<int32_t>(sim::EdgeKind::Change));
        sim::SimSuspendAnyOp::create(
            resolverBuilder, net.getLoc(), driverHandles,
            resolverBuilder.getDenseI32ArrayAttr(edges),
            sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, loop);
      }
      generatedUnits.push_back({semanticRoot, codeUnitID,
                                sim::EntryKind::Continuous, symbol, hierarchy,
                                resolver, ObserverResult::None});

      if (mixedDriverRegions) {
        std::string reactiveHierarchy =
            (net.getHierarchicalName().value_or(StringRef{"$user_net"}) +
             ".$resolution.reactive")
                .str();
        uint64_t reactiveCodeUnitID = stableCodeUnitID(reactiveHierarchy);
        if (reactiveCodeUnitID == rootCodeUnitID ||
            llvm::any_of(units,
                         [&](const PreparedUnit &unit) {
                           return unit.id == reactiveCodeUnitID;
                         }) ||
            llvm::any_of(generatedUnits, [&](const PreparedUnit &unit) {
              return unit.id == reactiveCodeUnitID;
            })) {
          net.emitError() << "stable code-unit ID collision for '"
                          << reactiveHierarchy << "'";
          invalid = true;
          continue;
        }

        auto reactiveResolver = cast<sim::SimFuncOp>(builder.clone(*resolver));
        std::string reactiveSymbol =
            (Twine("__obelisk_user_net_resolver_") + Twine(generatedOrdinal++))
                .str();
        reactiveResolver.setSymName(reactiveSymbol);
        reactiveResolver->setAttr(
            "code_unit_id", builder.getI64IntegerAttr(reactiveCodeUnitID));
        reactiveResolver->setAttr(
            "home_region",
            sim::EventRegionAttr::get(context, sim::EventRegion::Reactive));
        reactiveResolver->setAttr(sim::metadata::hierarchicalName,
                                  builder.getStringAttr(reactiveHierarchy));
        sim::SimCodeUnitDeclOp::create(
            builder, net.getLoc(), reactiveCodeUnitID, net.getScopeId(),
            sim::EntryKind::Continuous,
            builder.getStringAttr(reactiveHierarchy),
            builder.getStringAttr("user-defined net resolution"), UnitAttr{});

        Block *reactiveLoop = &reactiveResolver.getBody().back();
        if (reactiveLoop->empty()) {
          reactiveResolver.emitError()
              << "cloned reactive resolver has no suspension";
          invalid = true;
          continue;
        }
        reactiveLoop->back().erase();
        SmallVector<Value> reactiveHandles;
        Block &reactiveEntry = reactiveResolver.getBody().front();
        for (unsigned index = 0; index != drivers.size(); ++index)
          if (reactiveDriverIDs.contains(componentDrivers[index].getId()))
            reactiveHandles.push_back(
                reactiveEntry.getArgument(index + 1 + members.size()));
        OpBuilder reactiveBuilder = OpBuilder::atBlockEnd(reactiveLoop);
        if (reactiveHandles.size() == 1) {
          sim::SimSuspendChangeOp::create(reactiveBuilder, net.getLoc(),
                                          reactiveHandles.front(), ValueRange{},
                                          sim::ContinuationSiteAttr{},
                                          sim::EventRegionAttr{}, reactiveLoop);
        } else {
          SmallVector<int32_t> edges(
              reactiveHandles.size(),
              static_cast<int32_t>(sim::EdgeKind::Change));
          sim::SimSuspendAnyOp::create(
              reactiveBuilder, net.getLoc(), reactiveHandles,
              reactiveBuilder.getDenseI32ArrayAttr(edges),
              sim::ContinuationSiteAttr{}, sim::EventRegionAttr{},
              reactiveLoop);
        }
        generatedUnits.push_back({semanticRoot, reactiveCodeUnitID,
                                  sim::EntryKind::Continuous, reactiveSymbol,
                                  reactiveHierarchy, reactiveResolver,
                                  ObserverResult::None});
      }
    }
    llvm::append_range(units, generatedUnits);
  }
  if (invalid)
    return abort();

  // Timer-skew helpers are sibling code units, so inventory and materialize
  // them here in the serial module pass. The nested per-function lowering is
  // deliberately limited to its own body and may only consume these frozen
  // symbols (MLIR pass isolation and symbol-table mutation rules).
  {
    llvm::StringSet<> functionSymbols;
    llvm::DenseSet<uint64_t> codeUnitIDs;
    for (sim::SimFuncOp function :
         design.getBody().front().getOps<sim::SimFuncOp>()) {
      if (!functionSymbols.insert(function.getSymName()).second) {
        function.emitError("duplicate prepared code-unit symbol");
        invalid = true;
      }
      if (auto id = function.getCodeUnitId();
          id && !codeUnitIDs.insert(*id).second) {
        function.emitError("duplicate prepared stable code-unit ID");
        invalid = true;
      }
    }
    llvm::DenseSet<uint64_t> declarationIDs;
    for (sim::SimCodeUnitDeclOp declaration :
         design.getBody().front().getOps<sim::SimCodeUnitDeclOp>()) {
      if (!declarationIDs.insert(declaration.getId()).second) {
        declaration.emitError("duplicate stable code-unit declaration ID");
        invalid = true;
      }
      codeUnitIDs.insert(declaration.getId());
    }
    if (invalid)
      return abort();

    OpBuilder helperBuilder = OpBuilder::atBlockEnd(&design.getBody().front());
    for (PreparedUnit &unit : units) {
      sim::SimFuncOp coordinator = unit.function;
      if (!coordinator ||
          !coordinator->hasAttr("obelisk_sim.timing_timer_storage"))
        continue;
      Location location = getSemanticLocation(unit.source);
      std::string helperSymbol =
          (coordinator.getSymName() + ".$timing_timer").str();
      std::string helperHierarchy = helperSymbol;
      uint64_t helperCodeUnit = stableCodeUnitID(helperHierarchy);
      if (!functionSymbols.insert(helperSymbol).second ||
          !codeUnitIDs.insert(helperCodeUnit).second) {
        emitError(location)
            << "timer timing-check helper symbol or stable code-unit ID "
               "collides for '"
            << helperHierarchy << "'";
        invalid = true;
        continue;
      }
      sim::SimCodeUnitDeclOp parentDeclaration =
          codeUnitDeclarations.lookup(unit.source);
      if (!parentDeclaration) {
        emitError(location)
            << "timer timing-check coordinator has no code-unit declaration";
        invalid = true;
        continue;
      }
      sim::SimCodeUnitDeclOp::create(
          helperBuilder, location, helperCodeUnit,
          parentDeclaration.getScopeId(), sim::EntryKind::Always,
          helperBuilder.getStringAttr(helperHierarchy),
          helperBuilder.getStringAttr("timing-check timer maturity helper"),
          helperBuilder.getUnitAttr());

      Type contextType = sim::ContextType::get(context);
      Type eventType = sim::EventType::get(context);
      Type signalType = sim::RefType::get(context, helperBuilder.getI1Type());
      SmallVector<Type> inputs{contextType, eventType, signalType};
      SmallVector<DictionaryAttr> argumentAttrs{
          captureMetadata(helperBuilder, sim::CaptureKind::Context),
          captureMetadata(helperBuilder, sim::CaptureKind::Formal),
          captureMetadata(helperBuilder, sim::CaptureKind::Formal)};
      SmallVector<NamedAttribute> attrs{
          helperBuilder.getNamedAttr(
              "code_unit_id", helperBuilder.getI64IntegerAttr(helperCodeUnit)),
          helperBuilder.getNamedAttr("internal", helperBuilder.getUnitAttr()),
          helperBuilder.getNamedAttr("obelisk_sim.skew_deadline_helper",
                                     helperBuilder.getUnitAttr()),
          helperBuilder.getNamedAttr(
              "home_region",
              sim::EventRegionAttr::get(context, sim::EventRegion::Reactive)),
          helperBuilder.getNamedAttr("domain", coordinator.getDomainAttr()),
          helperBuilder.getNamedAttr(
              sim::metadata::hierarchicalName,
              helperBuilder.getStringAttr(helperHierarchy))};
      sim::SimFuncOp helper = sim::SimFuncOp::create(
          helperBuilder, location, helperSymbol,
          FunctionType::get(context, inputs, TypeRange{}),
          sim::EntryKind::Always, attrs, argumentAttrs);
      SymbolTable::setSymbolVisibility(helper,
                                       SymbolTable::Visibility::Private);

      Block &entry = helper.getBody().front();
      Block *wait = new Block();
      Block *publish = new Block();
      helper.getBody().push_back(wait);
      helper.getBody().push_back(publish);
      OpBuilder entryBuilder = OpBuilder::atBlockEnd(&entry);
      cf::BranchOp::create(entryBuilder, location, wait);
      OpBuilder waitBuilder = OpBuilder::atBlockEnd(wait);
      sim::SimSuspendEventOp::create(
          waitBuilder, location, entry.getArgument(1), ValueRange{},
          sim::ContinuationSiteAttr{},
          sim::EventRegionAttr::get(context, sim::EventRegion::Reactive),
          publish);
      OpBuilder publishBuilder = OpBuilder::atBlockEnd(publish);
      Value oldSignal = sim::SimRefLoadOp::create(publishBuilder, location,
                                                  publishBuilder.getI1Type(),
                                                  entry.getArgument(2));
      Value one = arith::ConstantOp::create(publishBuilder, location,
                                            publishBuilder.getI1Type(),
                                            publishBuilder.getBoolAttr(true));
      Value nextSignal =
          arith::XOrIOp::create(publishBuilder, location, oldSignal, one);
      // IEEE 1800-2017 31.4.2/.3: the delayed event matures in Re-NBA; this
      // once-spawned Reactive helper publishes only a private scalar, leaving
      // the exact coordinator to decide expiry after all producer regions.
      sim::SimRefStoreOp::create(publishBuilder, location, nextSignal,
                                 entry.getArgument(2));
      cf::BranchOp::create(publishBuilder, location, wait);
      helper->setAttr(sim::metadata::lowered, helperBuilder.getUnitAttr());
      coordinator->setAttr(
          "obelisk_sim.timing_timer_helper",
          FlatSymbolRefAttr::get(context, helper.getSymName()));
    }

    for (auto &terminal : negativeTimingTerminals) {
      if (terminal.delayTicks == 0)
        continue;
      Location location = module.getLoc();
      std::string identity =
          (Twine("negative-timing-delay|") +
           Twine(static_cast<unsigned>(terminal.source.kind)) + "|" +
           Twine(terminal.source.id) + "|" + Twine(terminal.delayTicks))
              .str();
      std::string monitorSymbol = (Twine("__obelisk_negative_timing_monitor_") +
                                   Twine(terminal.delayedStorage))
                                      .str();
      std::string commitSymbol = monitorSymbol + ".$commit";
      uint64_t monitorID = stableCodeUnitID(identity + "|monitor");
      uint64_t commitID = stableCodeUnitID(identity + "|commit");
      if (!functionSymbols.insert(monitorSymbol).second ||
          !codeUnitIDs.insert(monitorID).second ||
          (terminal.delayTicks > 0 &&
           (!functionSymbols.insert(commitSymbol).second ||
            !codeUnitIDs.insert(commitID).second))) {
        emitError(location)
            << "negative timing-check delayed monitor symbol or stable "
               "code-unit ID collides for '"
            << terminal.sourcePath << "'";
        invalid = true;
        continue;
      }
      terminal.monitorSymbol = monitorSymbol;
      sim::SimCodeUnitDeclOp::create(
          helperBuilder, location, monitorID, terminal.source.scopeId,
          sim::EntryKind::Always, helperBuilder.getStringAttr(monitorSymbol),
          helperBuilder.getStringAttr("negative timing-check delayed monitor"),
          helperBuilder.getUnitAttr());
      if (terminal.delayTicks > 0)
        sim::SimCodeUnitDeclOp::create(
            helperBuilder, location, commitID, terminal.source.scopeId,
            sim::EntryKind::Fork, helperBuilder.getStringAttr(commitSymbol),
            helperBuilder.getStringAttr("negative timing-check delayed commit"),
            helperBuilder.getUnitAttr());

      Type contextType = sim::ContextType::get(context);
      Type valueType = terminal.source.type;
      SmallVector<NamedAttribute> commonAttrs{
          helperBuilder.getNamedAttr("internal", helperBuilder.getUnitAttr()),
          helperBuilder.getNamedAttr(
              "home_region",
              sim::EventRegionAttr::get(context, sim::EventRegion::Active)),
          helperBuilder.getNamedAttr(
              "domain", sim::ExecutionDomainAttr::get(
                            context, sim::ExecutionDomain::Design))};

      sim::SimFuncOp commit;
      if (terminal.delayTicks > 0) {
        SmallVector<NamedAttribute> commitAttrs(commonAttrs);
        commitAttrs.push_back(helperBuilder.getNamedAttr(
            "obelisk_sim.negative_timing_delay_commit",
            helperBuilder.getUnitAttr()));
        commitAttrs.push_back(helperBuilder.getNamedAttr(
            "code_unit_id", helperBuilder.getI64IntegerAttr(commitID)));
        commitAttrs.push_back(helperBuilder.getNamedAttr(
            sim::metadata::hierarchicalName,
            helperBuilder.getStringAttr(commitSymbol)));
        SmallVector<DictionaryAttr> commitArgAttrs{
            captureMetadata(helperBuilder, sim::CaptureKind::Context),
            captureMetadata(helperBuilder, sim::CaptureKind::Value)};
        commit = sim::SimFuncOp::create(
            helperBuilder, location, commitSymbol,
            FunctionType::get(context, TypeRange{contextType, valueType},
                              TypeRange{}),
            sim::EntryKind::Fork, commitAttrs, commitArgAttrs);
        SymbolTable::setSymbolVisibility(commit,
                                         SymbolTable::Visibility::Private);
        Block &commitEntry = commit.getBody().front();
        Block *publish = new Block();
        publish->addArgument(valueType, location);
        commit.getBody().push_back(publish);
        OpBuilder commitBuilder = OpBuilder::atBlockEnd(&commitEntry);
        Value delay = sim::SimTimeConstantOp::create(
            commitBuilder, location, sim::TimeType::get(context),
            commitBuilder.getI64IntegerAttr(terminal.delayTicks));
        sim::SimSuspendDelayOp::create(
            commitBuilder, location, delay, sim::TimingSiteAttr{},
            ValueRange{commitEntry.getArgument(1)}, sim::ContinuationSiteAttr{},
            sim::EventRegionAttr::get(context, sim::EventRegion::Active),
            publish);
        OpBuilder publishBuilder = OpBuilder::atBlockEnd(publish);
        Value delayed = sim::SimContextStorageOp::create(
            publishBuilder, location, sim::RefType::get(context, valueType),
            commitEntry.getArgument(0),
            publishBuilder.getI64IntegerAttr(terminal.delayedStorage));
        sim::SimRefStoreOp::create(publishBuilder, location,
                                   publish->getArgument(0), delayed);
        sim::SimReturnOp::create(publishBuilder, location, ValueRange{});
        commit->setAttr(sim::metadata::lowered, helperBuilder.getUnitAttr());
      }

      SmallVector<NamedAttribute> monitorAttrs(commonAttrs);
      monitorAttrs.push_back(helperBuilder.getNamedAttr(
          "obelisk_sim.negative_timing_delay_monitor",
          helperBuilder.getUnitAttr()));
      monitorAttrs.push_back(helperBuilder.getNamedAttr(
          "code_unit_id", helperBuilder.getI64IntegerAttr(monitorID)));
      monitorAttrs.push_back(helperBuilder.getNamedAttr(
          sim::metadata::hierarchicalName,
          helperBuilder.getStringAttr(monitorSymbol)));
      Type sourceHandleType = terminal.source.kind == DescriptorInfo::Kind::Net
                                  ? Type(sim::NetType::get(context, valueType))
                                  : Type(sim::RefType::get(context, valueType));
      SmallVector<DictionaryAttr> monitorArgAttrs{
          captureMetadata(helperBuilder, sim::CaptureKind::Context),
          captureMetadata(helperBuilder,
                          terminal.source.kind == DescriptorInfo::Kind::Net
                              ? sim::CaptureKind::Net
                              : sim::CaptureKind::Storage,
                          terminal.source.id)};
      sim::SimFuncOp monitor = sim::SimFuncOp::create(
          helperBuilder, location, monitorSymbol,
          FunctionType::get(context, TypeRange{contextType, sourceHandleType},
                            TypeRange{}),
          sim::EntryKind::Always, monitorAttrs, monitorArgAttrs);
      SymbolTable::setSymbolVisibility(monitor,
                                       SymbolTable::Visibility::Private);
      Block &monitorEntry = monitor.getBody().front();
      Block *wait = new Block();
      Block *changed = new Block();
      monitor.getBody().push_back(wait);
      monitor.getBody().push_back(changed);
      OpBuilder monitorBuilder = OpBuilder::atBlockEnd(&monitorEntry);
      Value source = monitorEntry.getArgument(1);
      cf::BranchOp::create(monitorBuilder, location, wait);
      OpBuilder waitBuilder = OpBuilder::atBlockEnd(wait);
      sim::SimSuspendChangeOp::create(waitBuilder, location, source,
                                      ValueRange{}, sim::ContinuationSiteAttr{},
                                      sim::EventRegionAttr{}, changed);
      OpBuilder changedBuilder = OpBuilder::atBlockEnd(changed);
      Value current = terminal.source.kind == DescriptorInfo::Kind::Net
                          ? Value(sim::SimNetReadOp::create(
                                changedBuilder, location, valueType, source))
                          : Value(sim::SimRefLoadOp::create(
                                changedBuilder, location, valueType, source));
      if (commit) {
        sim::SimSpawnOp transportSpawn = sim::SimSpawnOp::create(
            changedBuilder, location, commit.getSymNameAttr(),
            ValueRange{monitorEntry.getArgument(0), current}, ArrayAttr{},
            ArrayAttr{});
        transportSpawn->setAttr(
            "obelisk_sim.negative_timing_transport_activation",
            changedBuilder.getUnitAttr());
      } else {
        // IEEE 1800-2017 31.9.1: a zero transport delay is part of the same
        // Active-region fixpoint.  Publishing directly avoids an artificial
        // zero-delay child and preserves same-slot event cohort ordering.
        Value delayed = sim::SimContextStorageOp::create(
            changedBuilder, location, sim::RefType::get(context, valueType),
            monitorEntry.getArgument(0),
            changedBuilder.getI64IntegerAttr(terminal.delayedStorage));
        sim::SimRefStoreOp::create(changedBuilder, location, current, delayed);
      }
      cf::BranchOp::create(changedBuilder, location, wait);
      monitor->setAttr(sim::metadata::lowered, helperBuilder.getUnitAttr());
    }
  }
  if (invalid)
    return abort();

  OpBuilder rootBuilder =
      OpBuilder::atBlockEnd(&rootInitializer.getBody().front());
  Value simContext = rootInitializer.getBody().front().getArgument(0);

  // IEEE 1800-2017 6.17: an event variable declared without an initial value
  // "is initialized to a new synchronization object". A scalar event variable
  // owns an event descriptor, but the events inside an unpacked array or
  // struct live in ordinary storage, whose slots start as the same null
  // handle -- triggering one element would then wake the waiters of every
  // other. Give each slot its own object before any process starts.
  {
    std::function<void(Value, Type, SmallVectorImpl<int64_t> &)>
        initializeEvents = [&](Value storage, Type type,
                               SmallVectorImpl<int64_t> &indices) {
          Location loc = rootInitializer.getLoc();
          if (isa<sim::EventType>(type)) {
            Value slot = storage;
            if (!indices.empty())
              slot = sim::SimRefSubelementOp::create(
                  rootBuilder, loc, sim::RefType::get(context, type), storage,
                  rootBuilder.getDenseI64ArrayAttr(indices));
            Value event = sim::SimEventCreateOp::create(
                rootBuilder, loc, sim::EventType::get(context));
            sim::SimRefStoreOp::create(rootBuilder, loc, event, slot);
            return;
          }
          if (!sim::isAggregateType(type))
            return;
          unsigned count = sim::getAggregateNumElements(type);
          for (unsigned index = 0; index != count; ++index) {
            indices.push_back(index);
            initializeEvents(storage, sim::getAggregateElementType(type, index),
                             indices);
            indices.pop_back();
          }
        };
    // Walk the emitted declarations rather than the descriptor map: their IR
    // order is stable, and only design-lifetime storage is elaborated here.
    for (sim::SimStorageDeclOp declaration :
         design.getBody().front().getOps<sim::SimStorageDeclOp>()) {
      Type type = declaration.getType();
      if ((declaration.getLifetime() != sim::Lifetime::Design &&
           declaration.getLifetime() != sim::Lifetime::Static) ||
          !typeContainsEvent(type))
        continue;
      // An explicit scalar initializer supplies either an alias or null. It
      // does not create a fresh synchronization object first (6.17), and the
      // marked initializer function runs below before any process is spawned.
      if (isa<sim::EventType>(type) &&
          declaration->hasAttr(eventExplicitInitializerAttrName))
        continue;
      Value storage = sim::SimContextStorageOp::create(
          rootBuilder, rootInitializer.getLoc(),
          sim::RefType::get(context, type), simContext,
          rootBuilder.getI64IntegerAttr(declaration.getId()));
      SmallVector<int64_t> indices;
      initializeEvents(storage, type, indices);
    }
  }
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

  // Initialize every implicit delayed copy after static variable
  // initialization but before any timing coordinator can subscribe.  The
  // once-spawned monitor then transports only real source publications; its
  // initialization cannot manufacture a Clause 31 timing occurrence.
  for (const auto &terminal : negativeTimingTerminals) {
    if (terminal.delayTicks == 0)
      continue;
    Type valueType = terminal.source.type;
    Type sourceHandleType = terminal.source.kind == DescriptorInfo::Kind::Net
                                ? Type(sim::NetType::get(context, valueType))
                                : Type(sim::RefType::get(context, valueType));
    Value source =
        terminal.source.kind == DescriptorInfo::Kind::Net
            ? Value(sim::SimContextNetOp::create(
                  rootBuilder, module.getLoc(), sourceHandleType, simContext,
                  rootBuilder.getI64IntegerAttr(terminal.source.id)))
            : Value(sim::SimContextStorageOp::create(
                  rootBuilder, module.getLoc(), sourceHandleType, simContext,
                  rootBuilder.getI64IntegerAttr(terminal.source.id)));
    Value initial = terminal.source.kind == DescriptorInfo::Kind::Net
                        ? Value(sim::SimNetReadOp::create(
                              rootBuilder, module.getLoc(), valueType, source))
                        : Value(sim::SimRefLoadOp::create(
                              rootBuilder, module.getLoc(), valueType, source));
    Value delayed = sim::SimContextStorageOp::create(
        rootBuilder, module.getLoc(), sim::RefType::get(context, valueType),
        simContext, rootBuilder.getI64IntegerAttr(terminal.delayedStorage));
    sim::SimRefStoreOp::create(rootBuilder, module.getLoc(), initial, delayed);
    sim::SimSpawnOp::create(
        rootBuilder, module.getLoc(),
        FlatSymbolRefAttr::get(context, terminal.monitorSymbol),
        ValueRange{simContext, source}, ArrayAttr{}, ArrayAttr{});
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
  // The statement an always procedure reaches first, looking through the
  // begin-end blocks that only group what follows.
  std::function<Operation *(Operation *)> leadingStatement =
      [&](Operation *statement) -> Operation * {
    if (!isa<semantic::SVBlockStatementOp, semantic::SVStatementListOp>(
            statement))
      return statement;
    SmallVector<Operation *> children = getChildren(statement);
    if (children.empty())
      return nullptr;
    return leadingStatement(children.front());
  };
  // IEEE 1800-2017 9.2.2.1: "If an always procedure has no control for
  // simulation time to advance, it will create a simulation deadlock
  // condition." Such a procedure does not start by waiting -- it runs its body
  // the moment it is spawned -- so putting it ahead of the continuous drivers
  // would let it read a net before any driver had propagated, where 6.5 makes
  // "the resultant value of multiple drivers ... determined by the resolution
  // function of the net type". A clocking-event or sequence-endpoint monitor
  // carries no procedural block of its own and always begins at its event
  // control.
  auto startsByWaiting = [&](const PreparedUnit &unit) {
    if (unit.entryKind != sim::EntryKind::Always &&
        unit.entryKind != sim::EntryKind::AlwaysFF)
      return false;
    auto procedure = dyn_cast<semantic::SVProceduralBlockSymbolOp>(unit.source);
    if (!procedure)
      return true;
    SmallVector<Operation *> body = getChildren(procedure);
    if (body.empty())
      return false;
    Operation *leading = leadingStatement(body.front());
    return leading && isa<semantic::SVTimedStatementOp>(leading);
  };
  auto hasDeferredTimeZeroActivation = [](const PreparedUnit &unit) {
    return unit.entryKind == sim::EntryKind::AlwaysComb ||
           unit.entryKind == sim::EntryKind::AlwaysLatch;
  };
  // IEEE 1800-2017 4.9.1: a continuous assignment process "is also evaluated at
  // time zero in order to propagate constant values. This includes implicit
  // continuous assignments inferred from port connections." A driver whose
  // source is constant never transitions afterwards, so this time-zero
  // evaluation is the only one it gets; ordering it after an initial process
  // that reads the driven net leaves that read observing the net's default
  // value forever.
  auto propagatesConstantsAtTimeZero = [](const PreparedUnit &unit) {
    return unit.entryKind == sim::EntryKind::Continuous ||
           unit.entryKind == sim::EntryKind::PortInitialize ||
           unit.entryKind == sim::EntryKind::PortInput ||
           unit.entryKind == sim::EntryKind::PortOutput;
  };
  auto initializesEventInput = [](const PreparedUnit &unit) {
    if (unit.entryKind != sim::EntryKind::PortInput &&
        unit.entryKind != sim::EntryKind::PortInitialize)
      return false;
    auto connection = dyn_cast<semantic::SVPortConnectionOp>(unit.source);
    return connection &&
           connection.getDirection() == semantic::SVArgumentDirection::In &&
           isa<semantic::EventType>(connection.getFormalType());
  };
  auto isComputedEventInput = [&](const PreparedUnit &unit) {
    if (!initializesEventInput(unit))
      return false;
    auto connection = cast<semantic::SVPortConnectionOp>(unit.source);
    Operation *actual = getSingleRegionRoot(connection.getActual());
    return !isa_and_nonnull<semantic::SVNamedValueExpressionOp,
                            semantic::SVHierarchicalValueExpressionOp>(actual);
  };

  // Establish explicit always-process sensitivities before initial processes
  // can trigger events or mutate their watched values. This deterministic
  // Active-region order prevents a source-order race from losing an event
  // before an `always @(event)` has suspended. An always process that is left
  // out of that first group carries the reason on its function, so the compute
  // graph does not order it ahead of the initial procedures it now follows.
  SmallVector<PreparedUnit *> earlyEventInputs;
  SmallVector<PreparedUnit *> computedEventInputs;
  for (PreparedUnit &unit : units) {
    if (isRootSpawned(unit) && initializesEventInput(unit)) {
      earlyEventInputs.push_back(&unit);
      if (isComputedEventInput(unit))
        computedEventInputs.push_back(&unit);
    }
    if (isRootSpawned(unit) && sim::isStartupEntryKind(unit.entryKind) &&
        !startsByWaiting(unit) && !propagatesConstantsAtTimeZero(unit))
      unit.function->setAttr(sim::startupWithoutSuspensionAttrName,
                             UnitAttr::get(context));
  }
  if (!earlyEventInputs.empty()) {
    llvm::StringMap<unsigned> producerByPath;
    for (auto [index, unit] : llvm::enumerate(earlyEventInputs)) {
      auto connection = cast<semantic::SVPortConnectionOp>(unit->source);
      StringRef internal = connection.getInternalPath().value_or(StringRef{});
      if (internal.empty() ||
          !producerByPath.try_emplace(internal, index).second) {
        emitError(getSemanticLocation(connection))
            << "cell-backed event input port has no unique destination";
        return abort();
      }
    }
    SmallVector<SmallVector<unsigned>> dependents(earlyEventInputs.size());
    SmallVector<unsigned> dependencyCounts(earlyEventInputs.size());
    for (auto [consumer, unit] : llvm::enumerate(earlyEventInputs)) {
      auto connection = cast<semantic::SVPortConnectionOp>(unit->source);
      Operation *actual = getSingleRegionRoot(connection.getActual());
      StringRef path;
      if (auto named =
              dyn_cast_or_null<semantic::SVNamedValueExpressionOp>(actual))
        path = named.getReferencedPath();
      else if (auto hierarchical =
                   dyn_cast_or_null<semantic::SVHierarchicalValueExpressionOp>(
                       actual))
        path = hierarchical.getReferencedPath();
      else if (auto member =
                   dyn_cast_or_null<semantic::SVMemberAccessExpressionOp>(
                       actual))
        path = member.getReferencedPath();
      llvm::StringSet<> visited;
      while (!path.empty() && visited.insert(path).second) {
        auto producer = producerByPath.find(path);
        if (producer != producerByPath.end()) {
          dependents[producer->second].push_back(consumer);
          ++dependencyCounts[consumer];
          break;
        }
        auto alias = portAliases->aliases.find(path);
        if (alias == portAliases->aliases.end())
          break;
        path = alias->second;
      }
    }
    SmallVector<unsigned> ready;
    for (auto [index, count] : llvm::enumerate(dependencyCounts))
      if (count == 0)
        ready.push_back(index);
    SmallVector<PreparedUnit *> ordered;
    for (size_t cursor = 0; cursor != ready.size(); ++cursor) {
      unsigned producer = ready[cursor];
      ordered.push_back(earlyEventInputs[producer]);
      for (unsigned consumer : dependents[producer])
        if (--dependencyCounts[consumer] == 0)
          ready.push_back(consumer);
    }
    if (ordered.size() != earlyEventInputs.size()) {
      emitError(getSemanticLocation(earlyEventInputs.front()->source))
          << "cell-backed event input port dependency is cyclic";
      return abort();
    }
    earlyEventInputs = std::move(ordered);
  }
  // Event controls capture the handle held by their event expression when
  // they suspend. Publish every cell-backed input's initial handle before an
  // `always @(formal)` can capture the formal's fresh local event instead.
  llvm::SmallPtrSet<PreparedUnit *, 16> featureSpawned;
  if (computedEventInputs.empty()) {
    for (PreparedUnit *unit : earlyEventInputs)
      if (failed(spawnRootUnit(*unit)))
        return abort();
    for (PreparedUnit &unit : units)
      if (isRootSpawned(unit) && startsByWaiting(unit))
        if (failed(spawnRootUnit(unit)))
          return abort();
  } else {
    design->setAttr(sim::computedEventStartupAttrName, UnitAttr::get(context));
    llvm::DenseMap<PreparedUnit *, unsigned> unitOrder;
    for (auto [index, unit] : llvm::enumerate(units))
      unitOrder[&unit] = index;

    // Whole ref/input aliases are the same event cell regardless of spelling.
    // Canonicalize them bidirectionally so producer and consumer paths meet.
    llvm::StringMap<SmallVector<std::string, 2>> aliasNeighbors;
    for (const auto &[path, target] : portAliases->aliases) {
      auto view = portAliases->refViews.find(path);
      if (view != portAliases->refViews.end() &&
          (!view->second.identity || view->second.offset != 0 ||
           view->second.packedOffset != 0 || !view->second.indices.empty()))
        continue;
      aliasNeighbors[path].push_back(target);
      aliasNeighbors[target].push_back(path.str());
    }
    llvm::StringMap<std::string> canonicalPaths;
    auto canonicalPath = [&](StringRef input) -> std::string {
      if (auto found = canonicalPaths.find(input);
          found != canonicalPaths.end())
        return found->second;
      llvm::StringSet<> visited;
      SmallVector<std::string> pending{input.str()};
      std::string canonical = input.str();
      for (size_t cursor = 0; cursor != pending.size(); ++cursor) {
        StringRef path = pending[cursor];
        if (!visited.insert(path).second)
          continue;
        if (path.compare(canonical) < 0)
          canonical = path.str();
        if (auto found = aliasNeighbors.find(path);
            found != aliasNeighbors.end())
          llvm::append_range(pending, found->second);
      }
      for (StringRef path : visited.keys())
        canonicalPaths[path] = canonical;
      return canonical;
    };

    llvm::StringMap<SmallVector<PreparedUnit *, 1>> producersByPath;
    auto addProducer = [&](StringRef path, PreparedUnit *producer) {
      if (path.empty())
        return;
      auto &producers = producersByPath[canonicalPath(path)];
      if (!llvm::is_contained(producers, producer))
        producers.push_back(producer);
    };
    for (PreparedUnit &unit : units) {
      if (!isRootSpawned(unit) || !propagatesConstantsAtTimeZero(unit))
        continue;
      if (auto connection = dyn_cast<semantic::SVPortConnectionOp>(unit.source);
          connection &&
          connection.getDirection() == semantic::SVArgumentDirection::In)
        addProducer(connection.getInternalPath().value_or(StringRef{}), &unit);
      for (const auto &path : unitWrittenCaptures[unit.source])
        addProducer(path.getKey(), &unit);
    }

    llvm::SmallPtrSet<PreparedUnit *, 16> allEventInputs(
        earlyEventInputs.begin(), earlyEventInputs.end());
    llvm::SmallPtrSet<PreparedUnit *, 16> computedEventSet(
        computedEventInputs.begin(), computedEventInputs.end());
    llvm::SmallPtrSet<PreparedUnit *, 16> featureEventInputs(
        computedEventInputs.begin(), computedEventInputs.end());
    llvm::StringMap<PreparedUnit *> eventProducerByPath;
    for (PreparedUnit *unit : earlyEventInputs) {
      auto connection = cast<semantic::SVPortConnectionOp>(unit->source);
      StringRef internal = connection.getInternalPath().value_or(StringRef{});
      if (!internal.empty())
        eventProducerByPath[canonicalPath(internal)] = unit;
    }
    bool changed;
    do {
      changed = false;
      for (PreparedUnit *consumer : earlyEventInputs) {
        if (featureEventInputs.contains(consumer) ||
            isComputedEventInput(*consumer))
          continue;
        auto connection = cast<semantic::SVPortConnectionOp>(consumer->source);
        Operation *actual = getSingleRegionRoot(connection.getActual());
        auto path = actual
                        ? actual->getAttrOfType<StringAttr>("referenced_path")
                        : StringAttr{};
        if (!path)
          continue;
        auto producer =
            eventProducerByPath.find(canonicalPath(path.getValue()));
        if (producer != eventProducerByPath.end() &&
            featureEventInputs.contains(producer->second))
          changed |= featureEventInputs.insert(consumer).second;
      }
    } while (changed);

    llvm::SmallPtrSet<PreparedUnit *, 32> startupSet;
    startupSet.insert(featureEventInputs.begin(), featureEventInputs.end());
    SmallVector<PreparedUnit *> pending(featureEventInputs.begin(),
                                        featureEventInputs.end());
    llvm::DenseMap<PreparedUnit *, SmallVector<PreparedUnit *, 2>> dependents;
    llvm::DenseMap<PreparedUnit *, unsigned> dependencyCounts;
    llvm::DenseSet<std::pair<PreparedUnit *, PreparedUnit *>> edges;
    auto addDependency = [&](PreparedUnit *producer, PreparedUnit *consumer) {
      if (!edges.insert({producer, consumer}).second)
        return;
      dependents[producer].push_back(consumer);
      ++dependencyCounts[consumer];
    };
    for (size_t cursor = 0; cursor != pending.size(); ++cursor) {
      PreparedUnit *consumer = pending[cursor];
      if (computedEventSet.contains(consumer)) {
        std::string destination =
            canonicalPath(cast<semantic::SVPortConnectionOp>(consumer->source)
                              .getInternalPath()
                              .value_or(StringRef{}));
        for (const auto &written : unitWrittenCaptures[consumer->source])
          if (canonicalPath(written.getKey()) != destination) {
            emitError(getSemanticLocation(consumer->source))
                << "computed event input startup expression has effects "
                   "outside its formal";
            return abort();
          }
      }
      auto addReadDependencies = [&](StringRef path) -> LogicalResult {
        auto found = producersByPath.find(canonicalPath(path));
        if (found == producersByPath.end())
          return success();
        for (PreparedUnit *producer : found->second) {
          if (producer == consumer) {
            emitError(getSemanticLocation(consumer->source))
                << "computed event input startup dependency is cyclic";
            return failure();
          }
          if (allEventInputs.contains(producer) &&
              !featureEventInputs.contains(producer))
            continue;
          addDependency(producer, consumer);
          if (startupSet.insert(producer).second)
            pending.push_back(producer);
        }
        return success();
      };
      for (const auto &read : unitReadCaptures[consumer->source])
        if (failed(addReadDependencies(read.getKey())))
          return abort();
      // An output port's internal source is implicit in its semantic
      // connection rather than represented by an expression region, so the
      // capture inventory only contains the external destination. Follow the
      // internal source explicitly when it lies in a computed-event startup
      // closure.
      if (auto connection =
              dyn_cast<semantic::SVPortConnectionOp>(consumer->source);
          connection &&
          connection.getDirection() == semantic::SVArgumentDirection::Out)
        if (failed(addReadDependencies(
                connection.getInternalPath().value_or(StringRef{}))))
          return abort();
    }

    for (auto &entry : dependents)
      llvm::sort(entry.second, [&](PreparedUnit *lhs, PreparedUnit *rhs) {
        return unitOrder.lookup(lhs) < unitOrder.lookup(rhs);
      });
    std::map<unsigned, PreparedUnit *> ready;
    for (PreparedUnit &unit : units)
      if (startupSet.contains(&unit) && dependencyCounts.lookup(&unit) == 0)
        ready.emplace(unitOrder.lookup(&unit), &unit);
    SmallVector<PreparedUnit *> startupOrder;
    while (!ready.empty()) {
      auto next = ready.begin();
      PreparedUnit *producer = next->second;
      ready.erase(next);
      startupOrder.push_back(producer);
      for (PreparedUnit *consumer : dependents[producer])
        if (--dependencyCounts[consumer] == 0)
          ready.emplace(unitOrder.lookup(consumer), consumer);
    }
    if (startupOrder.size() != startupSet.size()) {
      emitError(getSemanticLocation(computedEventInputs.front()->source))
          << "computed event input startup dependency is cyclic";
      return abort();
    }

    llvm::StringSet<> featureOutputs;
    for (PreparedUnit *unit : featureEventInputs) {
      auto connection = cast<semantic::SVPortConnectionOp>(unit->source);
      featureOutputs.insert(
          canonicalPath(connection.getInternalPath().value_or(StringRef{})));
    }
    struct WatchedPaths {
      SmallVector<std::string> all;
      SmallVector<std::string> signalTriggers;
      bool unresolvedEventCall = false;
    };
    auto leadingWatchedPaths = [&](const PreparedUnit &unit) {
      WatchedPaths result;
      auto procedure =
          dyn_cast<semantic::SVProceduralBlockSymbolOp>(unit.source);
      auto collectExpression = [&](Operation *expression, bool signalTrigger) {
        expression->walk([&](Operation *nested) {
          if (auto referenced =
                  nested->getAttrOfType<StringAttr>("referenced_path")) {
            result.all.push_back(referenced.getValue().str());
            if (signalTrigger)
              result.signalTriggers.push_back(referenced.getValue().str());
          }
          auto call = dyn_cast<semantic::SVCallExpressionOp>(nested);
          if (!call)
            return;
          Operation *target = resolveDirectCallee(call);
          SmallVector<const PreparedVirtualInterfaceCallee *> virtualTargets =
              preparedUnits->resolveVirtualInterfaceCallees(call);
          FailureOr<Type> callType = getNormalizedSemanticType(call);
          if (!target && virtualTargets.empty()) {
            result.unresolvedEventCall |=
                succeeded(callType) && isa<sim::EventType>(*callType);
            return;
          }
          auto collectReads = [&](Operation *callee) {
            for (const auto &read : unitReadCaptures[callee]) {
              result.all.push_back(read.getKey().str());
              if (signalTrigger)
                result.signalTriggers.push_back(read.getKey().str());
            }
          };
          collectReads(target);
          for (const PreparedVirtualInterfaceCallee *candidate : virtualTargets)
            collectReads(candidate->source);
        });
      };
      auto collectEvent = [&](semantic::SVSignalEventControlOp event) {
        SmallVector<Operation *> children = getChildren(event);
        if (children.empty())
          return;
        FailureOr<Type> type = getNormalizedSemanticType(children.front());
        if (failed(type)) {
          result.unresolvedEventCall = true;
          return;
        }
        collectExpression(children.front(), !isa<sim::EventType>(*type));
      };
      if (!procedure) {
        // Clocking-event and sequence-endpoint monitors have no procedural
        // wrapper. Classify each watched primary exactly as an ordinary event
        // list does; an iff guard is a condition, not a trigger that must arm
        // ahead of its time-zero producer.
        unit.source->walk([&](semantic::SVSignalEventControlOp event) {
          collectEvent(event);
        });
        return result;
      }
      SmallVector<Operation *> body = getChildren(procedure);
      Operation *leading = body.empty() ? nullptr : leadingStatement(body[0]);
      auto timed = dyn_cast_or_null<semantic::SVTimedStatementOp>(leading);
      SmallVector<Operation *> timedChildren =
          timed ? getChildren(timed) : SmallVector<Operation *>{};
      if (timedChildren.empty())
        return result;
      Operation *control = timedChildren.front();
      if (auto event = dyn_cast<semantic::SVSignalEventControlOp>(control))
        collectEvent(event);
      else if (auto list = dyn_cast<semantic::SVEventListControlOp>(control))
        for (Operation *member : getChildren(list))
          if (auto event = dyn_cast<semantic::SVSignalEventControlOp>(member))
            collectEvent(event);
      return result;
    };

    llvm::SmallPtrSet<PreparedUnit *, 16> affectedWaits;
    for (PreparedUnit &unit : units) {
      if (!isRootSpawned(unit) || !startsByWaiting(unit))
        continue;
      WatchedPaths watched = leadingWatchedPaths(unit);
      bool affected = llvm::any_of(watched.all, [&](const std::string &path) {
        return featureOutputs.contains(canonicalPath(path));
      });
      if (!affected)
        continue;
      if (watched.unresolvedEventCall) {
        emitError(getSemanticLocation(unit.source))
            << "computed event input startup ordering cannot resolve this "
               "event-control call";
        return abort();
      }
      bool observesPrerequisite =
          llvm::any_of(watched.signalTriggers, [&](const std::string &path) {
            auto found = producersByPath.find(canonicalPath(path));
            return found != producersByPath.end() &&
                   llvm::any_of(found->second, [&](PreparedUnit *producer) {
                     return startupSet.contains(producer) &&
                            !featureEventInputs.contains(producer);
                   });
          });
      if (observesPrerequisite) {
        emitError(getSemanticLocation(unit.source))
            << "computed event input startup dependency is cyclic through "
               "this event control";
        return abort();
      }
      affectedWaits.insert(&unit);
    }

    for (PreparedUnit *unit : earlyEventInputs)
      if (!featureEventInputs.contains(unit))
        if (failed(spawnRootUnit(*unit)))
          return abort();
    for (PreparedUnit &unit : units)
      if (isRootSpawned(unit) && startsByWaiting(unit) &&
          !affectedWaits.contains(&unit))
        if (failed(spawnRootUnit(unit)))
          return abort();
    for (PreparedUnit *unit : startupOrder) {
      unit->function->setAttr(sim::computedEventStartupAttrName,
                              UnitAttr::get(context));
      if (failed(spawnRootUnit(*unit)))
        return abort();
      featureSpawned.insert(unit);
    }
    for (PreparedUnit &unit : units)
      if (affectedWaits.contains(&unit))
        if (failed(spawnRootUnit(unit)))
          return abort();
  }

  // Then propagate the continuous drivers, so a constant reaches its readers
  // instead of racing them in source order. Re-evaluation stays event-driven,
  // so this only fixes which side of the time-zero race a constant lands on.
  for (PreparedUnit &unit : units)
    if (isRootSpawned(unit) && !initializesEventInput(unit) &&
        !featureSpawned.contains(&unit) && !startsByWaiting(unit) &&
        propagatesConstantsAtTimeZero(unit))
      if (failed(spawnRootUnit(unit)))
        return abort();

  // IEEE 1800-2017 9.2.2.2 requires the automatic time-zero activation of an
  // always_comb procedure to occur after all initial and always procedures
  // have started. Section 9.2.2.3 applies the same rule to always_latch.
  for (PreparedUnit &unit : units) {
    if (!isRootSpawned(unit) || startsByWaiting(unit) ||
        propagatesConstantsAtTimeZero(unit) ||
        hasDeferredTimeZeroActivation(unit))
      continue;
    if (failed(spawnRootUnit(unit)))
      return abort();
  }
  for (PreparedUnit &unit : units)
    if (isRootSpawned(unit) && hasDeferredTimeZeroActivation(unit))
      if (failed(spawnRootUnit(unit)))
        return abort();
  // Make virtual dispatch edges visible to generic symbol reachability while
  // the prepared semantic calls still retain their static method identity.
  // A live virtual call keeps every effective implementation compatible with
  // its receiver type; a dead caller contributes no roots. The early
  // devirtualization pass compacts the surviving slots after symbol DCE.
  {
    analysis::ClassDispatchAnalysis dispatch(design);
    llvm::StringMap<sim::SimClassMethodDeclOp> methods;
    design.walk([&](sim::SimClassMethodDeclOp method) {
      methods[method.getSymName()] = method;
    });
    llvm::StringMap<SmallVector<Attribute>> targetsByMethod;
    llvm::DenseMap<Operation *, llvm::DenseSet<Attribute>> targetsByFunction;
    design.walk([&](semantic::SVCallExpressionOp call) {
      if (!call->hasAttr("obelisk_sim.class_virtual"))
        return;
      auto reference =
          call->getAttrOfType<FlatSymbolRefAttr>("obelisk_sim.class_method");
      auto slot = call->getAttrOfType<IntegerAttr>("obelisk_sim.class_slot");
      auto signature =
          call->getAttrOfType<IntegerAttr>("obelisk_sim.class_signature");
      auto found =
          reference ? methods.find(reference.getValue()) : methods.end();
      if (found == methods.end() || !slot || !signature)
        return;
      sim::SimClassMethodDeclOp method = found->second;
      auto [cached, inserted] =
          targetsByMethod.try_emplace(method.getSymName());
      if (inserted) {
        sim::SimClassDeclOp owner = dispatch.lookup(method.getOwner());
        for (sim::SimClassMethodDeclOp target :
             dispatch.compatibleImplementations(
                 owner, slot.getValue().getZExtValue(),
                 signature.getValue().getZExtValue(), method.getIsTask()))
          cached->second.push_back(
              FlatSymbolRefAttr::get(context, target.getSymName()));
      }
      if (sim::SimFuncOp function = call->getParentOfType<sim::SimFuncOp>())
        targetsByFunction[function].insert(cached->second.begin(),
                                           cached->second.end());
    });
    for (auto &[operation, targetSet] : targetsByFunction) {
      SmallVector<Attribute> targets(targetSet.begin(), targetSet.end());
      llvm::sort(targets, [](Attribute lhs, Attribute rhs) {
        return cast<FlatSymbolRefAttr>(lhs).getValue() <
               cast<FlatSymbolRefAttr>(rhs).getValue();
      });
      sim::SimFuncOp function = cast<sim::SimFuncOp>(operation);
      OpBuilder markerBuilder =
          OpBuilder::atBlockBegin(&function.getBody().front());
      sim::SimClassDispatchTargetsOp::create(markerBuilder, function.getLoc(),
                                             builder.getArrayAttr(targets));
    }
  }

  sim::SimReturnOp::create(rootBuilder, module.getLoc(), ValueRange{});
}

} // namespace
} // namespace obelisk
