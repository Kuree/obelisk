//===- CoverageSchemaTestPass.cpp - Print embedded coverage schemas ------===//
//
// Parse each module's embedded schema-only coverage database with the native
// codec, print its records one per line, and check that every
// covergroup.sample operand matches the FunctionalExpression it names.
//
//===----------------------------------------------------------------------===//

#include "AnalysisTestPasses.h"

#include "obelisk/Coverage/CoverageDatabase.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"

#include <optional>

using namespace mlir;
namespace coverage = obelisk::coverage;

namespace {

StringRef dimensionKindName(coverage::ToggleDimensionKind kind) {
  switch (kind) {
  case coverage::ToggleDimensionKind::Root:
    return "Root";
  case coverage::ToggleDimensionKind::Scalar:
    return "Scalar";
  case coverage::ToggleDimensionKind::Enum:
    return "Enum";
  case coverage::ToggleDimensionKind::PackedArray:
    return "PackedArray";
  case coverage::ToggleDimensionKind::UnpackedArray:
    return "UnpackedArray";
  case coverage::ToggleDimensionKind::PackedStruct:
    return "PackedStruct";
  case coverage::ToggleDimensionKind::UnpackedStruct:
    return "UnpackedStruct";
  case coverage::ToggleDimensionKind::PackedUnion:
    return "PackedUnion";
  case coverage::ToggleDimensionKind::UnpackedUnion:
    return "UnpackedUnion";
  case coverage::ToggleDimensionKind::Field:
    return "Field";
  case coverage::ToggleDimensionKind::Tag:
    return "Tag";
  }
  return "Unknown";
}

StringRef sourceRoleName(coverage::FunctionalSourceRole role) {
  switch (role) {
  case coverage::FunctionalSourceRole::Expanded:
    return "expanded";
  case coverage::FunctionalSourceRole::Original:
    return "original";
  case coverage::FunctionalSourceRole::MacroDefinition:
    return "definition";
  case coverage::FunctionalSourceRole::MacroInvocation:
    return "invocation";
  }
  return "unknown";
}

template <typename Enum> uint32_t raw(Enum value) {
  return static_cast<uint32_t>(value);
}

class CoverageSchemaTestPass
    : public PassWrapper<CoverageSchemaTestPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(CoverageSchemaTestPass)

  StringRef getArgument() const final {
    return "test-obelisk-print-coverage-schema";
  }
  StringRef getDescription() const final {
    return "print embedded coverage schema records and check sample operands";
  }

  void runOnOperation() final {
    SmallVector<ModuleOp> modules;
    getOperation()->walk([&](ModuleOp module) {
      if (obelisk::runtime::has<obelisk::runtime::CoverageSchemaBlob>(module))
        modules.push_back(module);
    });
    if (modules.empty()) {
      getOperation().emitError("no embedded coverage schema found");
      return signalPassFailure();
    }
    llvm::DenseMap<uint64_t, coverage::FunctionalExpression> expressions;
    raw_ostream &os = llvm::errs();
    for (auto [index, module] : llvm::enumerate(modules)) {
      DenseI8ArrayAttr blob =
          obelisk::runtime::get<obelisk::runtime::CoverageSchemaBlob>(module);
      if (!blob) {
        module.emitError("coverage schema blob must be an i8 array");
        return signalPassFailure();
      }
      ArrayRef<int8_t> bytes = blob.asArrayRef();
      coverage::Database database;
      coverage::Diagnostic diagnostic;
      if (coverage::Status status = coverage::parse(
              reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size(),
              database, {}, &diagnostic);
          status != coverage::Status::Ok) {
        module.emitError() << "invalid coverage schema: "
                           << coverage::statusName(status) << " "
                           << diagnostic.detail;
        return signalPassFailure();
      }
      os << "schema " << index << '\n';
      if (failed(print(database, os, expressions, module)))
        return signalPassFailure();
    }
    WalkResult result = getOperation()->walk(
        [&](obelisk::sim::SimCovergroupSampleOp sample) {
          return failed(checkSample(sample, expressions))
                     ? WalkResult::interrupt()
                     : WalkResult::advance();
        });
    if (result.wasInterrupted())
      signalPassFailure();
  }

private:
  static LogicalResult
  print(const coverage::Database &db, raw_ostream &os,
        llvm::DenseMap<uint64_t, coverage::FunctionalExpression> &expressions,
        ModuleOp module) {
    llvm::DenseMap<uint64_t, StringRef> files;
    for (const coverage::SourceFile &file : db.sourceFiles)
      files[file.id] = file.path;

    for (const coverage::ToggleDimension &dimension : db.toggleDimensions)
      os << "dimension kind=" << dimensionKindName(dimension.kind)
         << " offset=" << dimension.bitOffset << " width=" << dimension.bitWidth
         << '\n';
    for (const coverage::Scope &scope : db.scopes)
      os << "scope id=" << scope.id << " parent=" << scope.parent
         << " name=" << scope.name << " kind=" << scope.kind
         << " definition=" << scope.definition << '\n';
    for (const coverage::LinePoint &point : db.linePoints)
      os << "line_point id=" << point.id << " file=" << files.lookup(point.file)
         << " end_file=" << files.lookup(point.endFile)
         << " scope=" << point.scope << " macro=" << point.macroName
         << " range=" << point.line << ':' << point.column << '-'
         << point.endLine << ':' << point.endColumn
         << " phase=" << point.semanticPhase << " flags=" << point.flags
         << '\n';
    for (const coverage::FunctionalType &type : db.functionalTypes) {
      os << "functional_type id=" << type.id << " name=" << type.name
         << " language=" << type.languageVersion
         << " hierarchy=" << type.hierarchy;
      if (type.base)
        os << " base=" << type.base;
      os << '\n';
    }
    for (const coverage::FunctionalItem &item : db.functionalItems) {
      os << "functional_item id=" << item.id << " type=" << item.type
         << " name=" << item.name << " kind=" << raw(item.kind);
      if (item.flags)
        os << " flags=" << item.flags;
      os << " ordinal=" << item.ordinal << " hierarchy=" << item.hierarchy
         << '\n';
    }
    for (const coverage::FunctionalBin &bin : db.functionalBins)
      os << "functional_bin id=" << bin.id << " item=" << bin.item
         << " name=" << bin.name << " kind=" << raw(bin.kind)
         << " flags=" << bin.flags << " ordinal=" << bin.ordinal
         << " at_least=" << bin.atLeast << " hierarchy=" << bin.hierarchy
         << '\n';
    for (const coverage::TransitionProgram &program : db.transitionPrograms)
      os << "transition_program bin=" << program.bin << " item=" << program.item
         << " first_alternative=" << program.firstAlternative
         << " alternative_count=" << program.alternativeCount
         << " flags=" << program.flags << '\n';
    for (const coverage::TransitionAlternative &alternative :
         db.transitionAlternatives)
      os << "transition_alternative bin=" << alternative.bin
         << " terminal_value_set=" << alternative.terminalValueSet
         << " first_step=" << alternative.firstStep
         << " step_count=" << alternative.stepCount
         << " ordinal=" << alternative.ordinal
         << " flags=" << alternative.flags << '\n';
    for (const coverage::TransitionStep &step : db.transitionSteps)
      os << "transition_step bin=" << step.bin << " value_set=" << step.valueSet
         << " lower_expression=" << step.lowerExpression
         << " upper_expression=" << step.upperExpression
         << " lower_bound=" << step.lowerBound
         << " upper_bound=" << step.upperBound
         << " alternative_ordinal=" << step.alternativeOrdinal
         << " ordinal=" << step.ordinal
         << " repetition=" << raw(step.repetition) << " flags=" << step.flags
         << '\n';
    for (const coverage::CrossPlan &plan : db.crossPlans)
      os << "cross_plan item=" << plan.item
         << " first_target=" << plan.firstTarget
         << " target_count=" << plan.targetCount
         << " first_bin=" << plan.firstBin << " bin_count=" << plan.binCount
         << " retain=" << raw(plan.retainAutoPolicy)
         << " iff_expression=" << plan.iffExpression
         << " tuple_element_type=" << plan.tupleElementType
         << " tuple_provenance_span=" << plan.tupleProvenanceSpan
         << " tuple_flags=" << plan.tupleFlags << '\n';
    for (const coverage::CrossTarget &target : db.crossTargets)
      os << "cross_target cross=" << target.cross << " target=" << target.target
         << " ordinal=" << target.ordinal
         << " tuple_bit_offset=" << target.tupleBitOffset
         << " tuple_bit_width=" << target.tupleBitWidth
         << " tuple_result_kind=" << raw(target.tupleResultKind)
         << " tuple_signedness=" << raw(target.tupleSignedness)
         << " tuple_flags=" << target.tupleFlags << '\n';
    for (const coverage::CrossBinPlan &bin : db.crossBins)
      os << "cross_bin bin=" << bin.bin << " cross=" << bin.cross
         << " root_selector=" << bin.rootSelector << " flags=" << bin.flags
         << '\n';
    for (const coverage::CrossSelectorNode &node : db.crossSelectorNodes)
      os << "cross_selector id=" << node.id << " cross=" << node.cross
         << " target=" << node.target << " bin=" << node.bin
         << " value_set=" << node.valueSet << " kind=" << raw(node.kind)
         << " ordinal=" << node.ordinal
         << " first_operand=" << node.firstOperand
         << " operand_count=" << node.operandCount
         << " with_expression=" << node.withExpression
         << " construction_expression=" << node.constructionExpression
         << " tuple_set=" << node.tupleSet
         << " matches_expression=" << node.matchesExpression
         << " matches_policy=" << raw(node.matchesPolicy)
         << " matches_count=" << node.matchesCount << " flags=" << node.flags
         << " predicate_root=" << node.predicateRoot << '\n';
    for (const coverage::CrossPredicateNode &predicate :
         db.crossPredicateNodes) {
      os << "cross_predicate id=" << predicate.id
         << " selector=" << predicate.selector
         << " first_operand=" << predicate.firstOperand
         << " operand_count=" << predicate.operandCount << " operands=";
      ArrayRef<uint64_t> operands(db.crossPredicateOperands);
      if (static_cast<uint64_t>(predicate.firstOperand) +
              predicate.operandCount <=
          operands.size())
        llvm::interleave(
            operands.slice(predicate.firstOperand, predicate.operandCount),
            os, ",");
      os << " expression=" << predicate.expression
         << " target=" << predicate.target << " kind=" << raw(predicate.kind)
         << " width=" << predicate.bitWidth
         << " signedness=" << raw(predicate.signedness)
         << " ordinal=" << predicate.ordinal << " flags=" << predicate.flags
         << " element_width=" << predicate.elementWidth
         << " index_base=" << predicate.indexBase << '\n';
    }
    for (const coverage::CrossSelectorOperand &operand :
         db.crossSelectorOperands)
      os << "cross_selector_operand node=" << operand.node
         << " operand=" << operand.operand << " ordinal=" << operand.ordinal
         << '\n';
    for (const coverage::FunctionalFormal &formal : db.functionalFormals)
      os << "functional_formal id=" << formal.id << " type=" << formal.type
         << " name=" << formal.name << " kind=" << raw(formal.kind)
         << " direction=" << raw(formal.direction)
         << " result_kind=" << raw(formal.resultKind)
         << " flags=" << formal.flags << " width=" << formal.bitWidth
         << " signedness=" << raw(formal.signedness)
         << " ordinal=" << formal.ordinal
         << " default_expression=" << formal.defaultExpression
         << " value_type=" << formal.valueType << '\n';
    for (const coverage::FunctionalContainerType &container :
         db.functionalContainerTypes)
      os << "functional_container_type id=" << container.id
         << " kind=" << raw(container.kind)
         << " key_kind=" << raw(container.keyKind)
         << " element_type=" << container.elementType
         << " key_width=" << container.keyWidth
         << " bound=" << container.bound << '\n';
    for (const coverage::FunctionalAggregateType &aggregate :
         db.functionalAggregateTypes)
      os << "functional_aggregate_type id=" << aggregate.id
         << " element_type=" << aggregate.elementType
         << " value_size=" << aggregate.valueSize
         << " alignment=" << aggregate.alignment
         << " width=" << aggregate.bitWidth << " flags=" << aggregate.flags
         << '\n';
    for (const coverage::FunctionalClassType &classType :
         db.functionalClassTypes)
      os << "functional_class_type id=" << classType.id
         << " class_type=" << classType.classType << '\n';
    for (const coverage::FunctionalVirtualInterfaceType &interfaceType :
         db.functionalVirtualInterfaceTypes)
      os << "functional_virtual_interface_type id=" << interfaceType.id
         << " interface_type=" << interfaceType.interfaceType << '\n';
    for (const coverage::FunctionalVirtualInterfaceScope &scope :
         db.functionalVirtualInterfaceScopes)
      os << "functional_virtual_interface_scope id=" << scope.id
         << " interface_type=" << scope.interfaceType << '\n';
    for (const coverage::FunctionalValueSet &set : db.functionalValueSets)
      os << "functional_value_set id=" << set.id << " item=" << set.item
         << " atoms=" << set.atomCount << " width=" << set.bitWidth
         << " kind=" << raw(set.kind) << " flags=" << set.flags
         << " signedness=" << raw(set.signedness)
         << " set_expression=" << set.setExpression << '\n';
    for (const coverage::FunctionalValueAtom &atom : db.functionalValueAtoms)
      os << "functional_value_atom set=" << atom.valueSet
         << " ordinal=" << atom.ordinal << " kind=" << raw(atom.kind)
         << " flags=" << atom.flags
         << " lower_expression=" << atom.lowerExpression
         << " upper_expression=" << atom.upperExpression << '\n';
    for (const coverage::FunctionalExpression &expression :
         db.functionalExpressions) {
      if (!expressions.try_emplace(expression.id, expression).second)
        return module.emitError(
            "duplicate FunctionalExpression ID across schemas");
      os << "functional_expression id=" << expression.id
         << " owner=" << expression.owner
         << " owner_kind=" << raw(expression.ownerKind)
         << " role=" << raw(expression.role)
         << " result_kind=" << raw(expression.resultKind)
         << " width=" << expression.bitWidth
         << " signedness=" << raw(expression.signedness)
         << " owner_ordinal=" << expression.ownerOrdinal
         << " owner_subordinal=" << expression.ownerSubordinal
         << " phase=" << raw(expression.evaluationPhase)
         << " result_ordinal=" << expression.resultOrdinal
         << " flags=" << expression.flags << '\n';
    }
    for (const coverage::FunctionalSourceRange &source :
         db.functionalSourceRanges)
      os << "functional_source bin=" << source.bin
         << " role=" << sourceRoleName(source.role)
         << " ordinal=" << source.ordinal
         << " file=" << files.lookup(source.file)
         << " end_file=" << files.lookup(source.endFile)
         << " macro=" << source.macroName << " range=" << source.line << ':'
         << source.column << '-' << source.endLine << ':' << source.endColumn
         << '\n';
    for (const coverage::Exclusion &exclusion : db.exclusions)
      os << "exclusion entity=" << exclusion.entity
         << " metric=" << raw(exclusion.metric)
         << " reason=" << exclusion.reason << '\n';
    for (const coverage::FunctionalBinPlan &plan : db.functionalBinPlans)
      os << "functional_bin_plan bin=" << plan.bin
         << " value_set=" << plan.valueSet
         << " iff_expression=" << plan.iffExpression
         << " cardinality_expression=" << plan.cardinalityExpression
         << " array_cardinality=" << plan.arrayCardinality
         << " array_mode=" << raw(plan.arrayMode)
         << " distribution=" << raw(plan.distribution)
         << " flags=" << plan.flags << '\n';
    for (const coverage::FunctionalOptionPlan &plan : db.functionalOptionPlans)
      os << "functional_option_plan owner=" << plan.owner
         << " expression=" << plan.expression
         << " owner_kind=" << raw(plan.ownerKind)
         << " scope=" << raw(plan.scope) << " option=" << raw(plan.option)
         << " ordinal=" << plan.ordinal << " flags=" << plan.flags << '\n';
    return success();
  }

  /// A sample batch passes sample-phase expression results in result-ordinal
  /// order, each typed by the expression's result kind.
  static LogicalResult checkSample(
      obelisk::sim::SimCovergroupSampleOp sample,
      const llvm::DenseMap<uint64_t, coverage::FunctionalExpression>
          &expressions) {
    ArrayRef<int64_t> ids = sample.getExpressionIds();
    if (ids.size() != sample.getValues().size())
      return sample.emitError("sample ID/type cardinality mismatch");
    // Omitted defaults leave holes in the sample result ordinals; the
    // production verifier checks which defaults may be omitted.
    std::optional<uint32_t> previousOrdinal;
    for (auto [id, value] : llvm::zip_equal(ids, sample.getValues())) {
      auto found = expressions.find(static_cast<uint64_t>(id));
      if (found == expressions.end())
        return sample.emitError()
               << "sample references absent FunctionalExpression " << id;
      const coverage::FunctionalExpression &expression = found->second;
      if (expression.evaluationPhase !=
              coverage::FunctionalExpressionEvaluationPhase::Sample ||
          (previousOrdinal && expression.resultOrdinal <= *previousOrdinal))
        return sample.emitError() << "sample FunctionalExpression " << id
                                  << " has wrong phase/ordinal";
      previousOrdinal = expression.resultOrdinal;
      Type type = value.getType();
      unsigned width = 0;
      if (auto integer = dyn_cast<IntegerType>(type))
        width = integer.getWidth();
      else if (auto logic = dyn_cast<obelisk::sim::LogicType>(type))
        width = logic.getWidth();
      bool matches = true;
      switch (expression.resultKind) {
      case coverage::FunctionalExpressionResultKind::Boolean:
        matches = type.isInteger(1);
        break;
      case coverage::FunctionalExpressionResultKind::Integral:
        matches = width && width == expression.bitWidth;
        break;
      case coverage::FunctionalExpressionResultKind::Real:
        matches = type.isF64();
        break;
      case coverage::FunctionalExpressionResultKind::Aggregate:
        matches = isa<obelisk::sim::ReferencePathType>(type);
        break;
      default:
        break;
      }
      if (!matches)
        return sample.emitError()
               << "FunctionalExpression " << id << " has sample type " << type;
    }
    return success();
  }
};

} // namespace

namespace obelisk {
void registerCoverageSchemaTestPass() {
  PassRegistration<CoverageSchemaTestPass>();
}
} // namespace obelisk
