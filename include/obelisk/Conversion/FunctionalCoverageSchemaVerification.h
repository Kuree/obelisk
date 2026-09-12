//===- FunctionalCoverageSchemaVerification.h -----------------*- C++ -*-===//
//
// Dedicated pre-backend validation of typed functional-coverage IR against
// the embedded exact-v1 schema. This deliberately does not run from core op
// verifiers: structural IR remains independently constructible and schema
// parsing happens once at the backend boundary.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_CONVERSION_FUNCTIONALCOVERAGESCHEMAVERIFICATION_H
#define OBELISK_CONVERSION_FUNCTIONALCOVERAGESCHEMAVERIFICATION_H

#include "obelisk/Coverage/CoverageDatabase.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

namespace obelisk {

inline mlir::LogicalResult
verifyFunctionalCoverageSchemaBeforeBackend(mlir::ModuleOp module) {
  using namespace mlir;
  bool hasTypedFunctionalOperation = false;
  module.walk([&](Operation *op) {
    hasTypedFunctionalOperation |=
        isa<sim::SimCovergroupCreateOp, sim::SimCovergroupFormalReadOp,
            sim::SimCovergroupSampleOp, sim::SimCovergroupStartOp,
            sim::SimCovergroupStopOp, sim::SimCovergroupSetIntegerOptionOp,
            sim::SimCovergroupGetIntegerOptionOp,
            sim::SimCovergroupSetStringOptionOp,
            sim::SimCovergroupInstanceQueryOp, sim::SimCovergroupTypeQueryOp>(
            op);
  });
  if (!hasTypedFunctionalOperation)
    return success();

  auto blob = module->getAttrOfType<DenseI8ArrayAttr>(
      sim::metadata::coverageSchemaBlob);
  if (!blob)
    return module.emitError(
        "typed functional coverage requires an embedded exact-v1 coverage "
        "schema");
  ArrayRef<int8_t> signedBytes = blob.asArrayRef();
  coverage::Database schema;
  coverage::Diagnostic diagnostic;
  coverage::Status status =
      coverage::parse(reinterpret_cast<const uint8_t *>(signedBytes.data()),
                      signedBytes.size(), schema, {}, &diagnostic);
  if (status != coverage::Status::Ok)
    return module.emitError()
           << "cannot parse embedded exact-v1 coverage schema: "
           << coverage::statusName(status)
           << (diagnostic.field ? " at field " : "")
           << (diagnostic.field ? diagnostic.field : "")
           << (diagnostic.detail.empty() ? "" : ": ") << diagnostic.detail;

  llvm::DenseMap<uint64_t, const coverage::FunctionalItem *> items;
  llvm::DenseMap<uint64_t, const coverage::FunctionalBin *> bins;
  llvm::DenseMap<uint64_t, const coverage::FunctionalValueSet *> valueSets;
  llvm::DenseMap<uint64_t, const coverage::CrossSelectorNode *> selectors;
  llvm::DenseMap<uint64_t, const coverage::FunctionalFormal *> formals;
  llvm::DenseSet<uint64_t> types;
  for (const auto &entry : schema.functionalTypes)
    types.insert(entry.id);
  for (const auto &entry : schema.functionalItems)
    items.try_emplace(entry.id, &entry);
  for (const auto &entry : schema.functionalBins)
    bins.try_emplace(entry.id, &entry);
  for (const auto &entry : schema.functionalValueSets)
    valueSets.try_emplace(entry.id, &entry);
  for (const auto &entry : schema.crossSelectorNodes)
    selectors.try_emplace(entry.id, &entry);
  for (const auto &entry : schema.functionalFormals)
    formals.try_emplace(entry.id, &entry);

  auto ownerType = [&](const coverage::FunctionalExpression &expression) {
    switch (expression.ownerKind) {
    case coverage::FunctionalExpressionOwnerKind::Type:
      return expression.owner;
    case coverage::FunctionalExpressionOwnerKind::Item: {
      auto found = items.find(expression.owner);
      return found == items.end() ? uint64_t{0} : found->second->type;
    }
    case coverage::FunctionalExpressionOwnerKind::Bin: {
      auto found = bins.find(expression.owner);
      if (found == bins.end())
        return uint64_t{0};
      auto item = items.find(found->second->item);
      return item == items.end() ? uint64_t{0} : item->second->type;
    }
    case coverage::FunctionalExpressionOwnerKind::ValueSet: {
      auto found = valueSets.find(expression.owner);
      if (found == valueSets.end())
        return uint64_t{0};
      auto item = items.find(found->second->item);
      return item == items.end() ? uint64_t{0} : item->second->type;
    }
    case coverage::FunctionalExpressionOwnerKind::Formal: {
      auto found = formals.find(expression.owner);
      return found == formals.end() ? uint64_t{0} : found->second->type;
    }
    case coverage::FunctionalExpressionOwnerKind::Selector: {
      auto found = selectors.find(expression.owner);
      if (found == selectors.end())
        return uint64_t{0};
      auto item = items.find(found->second->cross);
      return item == items.end() ? uint64_t{0} : item->second->type;
    }
    }
    return uint64_t{0};
  };
  auto valueMatches = [](Type type,
                         coverage::FunctionalExpressionResultKind kind,
                         uint32_t width, uint32_t flags = 0) {
    switch (kind) {
    case coverage::FunctionalExpressionResultKind::Boolean:
      return type.isSignlessInteger(1);
    case coverage::FunctionalExpressionResultKind::Integral:
      if (std::optional<unsigned> packedWidth = sim::getPackedWidth(type))
        return *packedWidth == width;
      return false;
    case coverage::FunctionalExpressionResultKind::Real:
      return type.isF64();
    case coverage::FunctionalExpressionResultKind::String:
      return isa<sim::StringType>(type);
    case coverage::FunctionalExpressionResultKind::TupleQueue: {
      auto queue = dyn_cast<sim::QueueType>(type);
      return queue && queue.getBound() == 0 &&
             isa<sim::UnpackedStructType>(queue.getElementType());
    }
    case coverage::FunctionalExpressionResultKind::Set: {
      Type element;
      if (auto array = dyn_cast<sim::DynamicArrayType>(type))
        element = array.getElementType();
      else if (auto queue = dyn_cast<sim::QueueType>(type))
        element = queue.getElementType();
      if (!element)
        return false;
      if (auto real = dyn_cast<FloatType>(element))
        return (real.getWidth() == 32 || real.getWidth() == 64) &&
               real.getWidth() == width && flags == 0;
      Type scalar = sim::getPackedScalarType(element);
      if (std::optional<unsigned> elementWidth = sim::getPackedWidth(scalar)) {
        const bool fourState = isa<sim::LogicType>(scalar);
        const bool expectedFourState =
            flags & coverage::FunctionalExpressionSetElementFourState;
        return *elementWidth == width && fourState == expectedFourState;
      }
      return false;
    }
    case coverage::FunctionalExpressionResultKind::TupleSet:
      return true;
    }
    return false;
  };
  auto referenceElement = [](Type type) -> Type {
    if (auto ref = dyn_cast<sim::RefType>(type))
      return ref.getElementType();
    if (auto ref = dyn_cast<sim::ManagedRefType>(type))
      return ref.getElementType();
    if (auto ref = dyn_cast<sim::ArgumentRefType>(type))
      return ref.getElementType();
    return {};
  };
  auto declarationType = [&](Operation *op, sim::CovergroupHandleType handle) {
    auto reference = handle.getCovergroupName();
    auto declaration =
        SymbolTable::lookupNearestSymbolFrom<sim::SimCovergroupDeclOp>(
            op, reference);
    return declaration ? declaration.getSchemaType() : uint64_t{0};
  };
  auto verifyQueryItem = [&](Operation *operation, uint64_t type,
                             int64_t rawItem) -> LogicalResult {
    if (!types.contains(type)) {
      operation->emitError(
          "declaration schema type is absent from the embedded exact-v1 "
          "functional schema");
      return failure();
    }
    if (!rawItem)
      return success();
    auto found = items.find(static_cast<uint64_t>(rawItem));
    if (found == items.end() || found->second->type != type) {
      operation->emitError(
          "has a nonexistent or wrong-owner functional item ID");
      return failure();
    }
    return success();
  };

  llvm::DenseMap<uint64_t, SmallVector<const coverage::FunctionalFormal *>>
      constructorFormalsByType;
  for (const coverage::FunctionalFormal &formal : schema.functionalFormals)
    if (formal.kind == coverage::FunctionalFormalKind::Constructor)
      constructorFormalsByType[formal.type].push_back(&formal);
  for (auto &entry : constructorFormalsByType)
    llvm::sort(entry.second, [](const auto *lhs, const auto *rhs) {
      return lhs->ordinal < rhs->ordinal;
    });

  using Phase = coverage::FunctionalExpressionEvaluationPhase;
  llvm::DenseMap<uint64_t, SmallVector<const coverage::FunctionalExpression *>>
      constructorExpressionsByType;
  llvm::DenseMap<uint64_t, SmallVector<const coverage::FunctionalExpression *>>
      sampleExpressionsByType;
  for (const coverage::FunctionalExpression &expression :
       schema.functionalExpressions) {
    uint64_t type = ownerType(expression);
    if (expression.evaluationPhase == Phase::Constructor ||
        expression.evaluationPhase == Phase::Option)
      constructorExpressionsByType[type].push_back(&expression);
    else if (expression.evaluationPhase == Phase::Sample)
      sampleExpressionsByType[type].push_back(&expression);
  }
  auto sortExpressions = [](auto &map, bool phaseOrdered) {
    for (auto &entry : map)
      llvm::sort(
          entry.second, [phaseOrdered](const auto *lhs, const auto *rhs) {
            if (phaseOrdered && lhs->evaluationPhase != rhs->evaluationPhase)
              return lhs->evaluationPhase < rhs->evaluationPhase;
            return lhs->resultOrdinal < rhs->resultOrdinal;
          });
  };
  sortExpressions(constructorExpressionsByType, true);
  sortExpressions(sampleExpressionsByType, false);

  auto verifyExpressionBatch =
      [&](Operation *operation, ArrayRef<int64_t> rawIDs, ValueRange values,
          uint64_t type, Phase phase, StringRef label) -> LogicalResult {
    const auto &byType = phase == Phase::Constructor
                             ? constructorExpressionsByType
                             : sampleExpressionsByType;
    auto foundExpected = byType.find(type);
    ArrayRef<const coverage::FunctionalExpression *> expected =
        foundExpected == byType.end()
            ? ArrayRef<const coverage::FunctionalExpression *>{}
            : ArrayRef<const coverage::FunctionalExpression *>(
                  foundExpected->second);
    size_t supplied = 0;
    for (const coverage::FunctionalExpression *expression : expected) {
      bool isDefault =
          expression->role == coverage::FunctionalExpressionRole::FormalDefault;
      bool present = supplied < rawIDs.size() && rawIDs[supplied] > 0 &&
                     static_cast<uint64_t>(rawIDs[supplied]) == expression->id;
      // A default helper executes only when its actual was omitted. All other
      // phase expressions are mandatory, and every supplied default remains
      // in its schema result-ordinal position.
      if (isDefault && !present)
        continue;
      if (!present) {
        operation->emitError()
            << "does not contain the complete schema-ordered " << label
            << " FunctionalExpression batch; expected ID " << expression->id
            << " at result ordinal " << expression->resultOrdinal;
        return failure();
      }
      if (!valueMatches(values[supplied].getType(), expression->resultKind,
                        expression->bitWidth, expression->flags)) {
        operation->emitError()
            << "has a type-mismatched " << label << " FunctionalExpression ID "
            << expression->id << " at result ordinal "
            << expression->resultOrdinal;
        return failure();
      }
      ++supplied;
    }
    if (supplied != rawIDs.size()) {
      operation->emitError()
          << "has an extra, reordered, wrong-owner, or wrong-phase " << label
          << " FunctionalExpression ID " << rawIDs[supplied];
      return failure();
    }
    return success();
  };

  bool invalid = false;
  module.walk([&](Operation *operation) {
    if (auto create = dyn_cast<sim::SimCovergroupCreateOp>(operation)) {
      uint64_t type = declarationType(create, create.getResult().getType());
      ValueRange arguments = create.getPayloads().take_front(
          static_cast<size_t>(create.getArgumentCount()));
      ValueRange values = create.getPayloads().drop_front(
          static_cast<size_t>(create.getArgumentCount()));
      if (!types.contains(type)) {
        create.emitError(
            "declaration schema type is absent from the embedded exact-v1 "
            "functional schema");
        invalid = true;
      }
      auto expectedIt = constructorFormalsByType.find(type);
      ArrayRef<const coverage::FunctionalFormal *> expectedFormals =
          expectedIt == constructorFormalsByType.end()
              ? ArrayRef<const coverage::FunctionalFormal *>{}
              : ArrayRef<const coverage::FunctionalFormal *>(
                    expectedIt->second);
      if (create.getFormalIds().size() != expectedFormals.size()) {
        create.emitError()
            << "requires the complete schema-ordered constructor "
               "FunctionalFormal batch; expected "
            << expectedFormals.size() << " entries but got "
            << create.getFormalIds().size();
        invalid = true;
      } else {
        for (auto [index, rawID] : llvm::enumerate(create.getFormalIds())) {
          const coverage::FunctionalFormal *formal = expectedFormals[index];
          Type argumentType =
              formal->direction == coverage::FunctionalFormalDirection::Ref
                  ? referenceElement(arguments[index].getType())
                  : arguments[index].getType();
          if (rawID <= 0 || static_cast<uint64_t>(rawID) != formal->id ||
              !valueMatches(argumentType, formal->resultKind,
                            formal->bitWidth)) {
            create.emitError()
                << "constructor FunctionalFormal batch is reordered, "
                   "wrong-owner, or type-mismatched at schema ordinal "
                << index << "; expected ID " << formal->id;
            invalid = true;
          }
        }
      }
      if (failed(verifyExpressionBatch(create, create.getExpressionIds(),
                                       values, type, Phase::Constructor,
                                       "constructor")))
        invalid = true;
    } else if (auto read =
                   dyn_cast<sim::SimCovergroupFormalReadOp>(operation)) {
      uint64_t type = declarationType(read, read.getHandle().getType());
      auto found = read.getFormalId() > 0
                       ? formals.find(static_cast<uint64_t>(read.getFormalId()))
                       : formals.end();
      if (found != formals.end() &&
          found->second->resultKind ==
              coverage::FunctionalExpressionResultKind::String) {
        read.emitError("String constructor FunctionalFormal values are "
                       "constructor-only and cannot be read while sampling");
        invalid = true;
      } else if (found == formals.end() || found->second->type != type ||
                 found->second->kind !=
                     coverage::FunctionalFormalKind::Constructor ||
                 !valueMatches(read.getResult().getType(),
                               found->second->resultKind,
                               found->second->bitWidth)) {
        read.emitError("has a nonexistent, wrong-owner, or type-mismatched "
                       "constructor FunctionalFormal ID");
        invalid = true;
      }
    } else if (auto control = dyn_cast<sim::SimCovergroupStartOp>(operation)) {
      uint64_t type = declarationType(control, control.getHandle().getType());
      if (failed(verifyQueryItem(control, type, control.getItem())))
        invalid = true;
    } else if (auto control = dyn_cast<sim::SimCovergroupStopOp>(operation)) {
      uint64_t type = declarationType(control, control.getHandle().getType());
      if (failed(verifyQueryItem(control, type, control.getItem())))
        invalid = true;
    } else if (auto set =
                   dyn_cast<sim::SimCovergroupSetIntegerOptionOp>(operation)) {
      uint64_t type = declarationType(set, set.getHandle().getType());
      if (failed(verifyQueryItem(set, type, set.getItem())))
        invalid = true;
    } else if (auto get =
                   dyn_cast<sim::SimCovergroupGetIntegerOptionOp>(operation)) {
      uint64_t type = declarationType(get, get.getHandle().getType());
      if (failed(verifyQueryItem(get, type, get.getItem())))
        invalid = true;
    } else if (auto set =
                   dyn_cast<sim::SimCovergroupSetStringOptionOp>(operation)) {
      uint64_t type = declarationType(set, set.getHandle().getType());
      if (failed(verifyQueryItem(set, type, set.getItem())))
        invalid = true;
    } else if (auto set = dyn_cast<sim::SimCovergroupSetTypeIntegerOptionOp>(
                   operation)) {
      if (failed(verifyQueryItem(set, set.getTypeId(), set.getItem())))
        invalid = true;
    } else if (auto set = dyn_cast<sim::SimCovergroupSetTypeStringOptionOp>(
                   operation)) {
      if (failed(verifyQueryItem(set, set.getTypeId(), set.getItem())))
        invalid = true;
    } else if (auto query =
                   dyn_cast<sim::SimCovergroupInstanceQueryOp>(operation)) {
      uint64_t type = declarationType(query, query.getHandle().getType());
      if (failed(verifyQueryItem(query, type, query.getItem())))
        invalid = true;
    } else if (auto query =
                   dyn_cast<sim::SimCovergroupTypeQueryOp>(operation)) {
      auto declaration =
          SymbolTable::lookupNearestSymbolFrom<sim::SimCovergroupDeclOp>(
              query, query.getDeclarationAttr());
      uint64_t type = declaration ? declaration.getSchemaType() : uint64_t{0};
      if (failed(verifyQueryItem(query, type, query.getItem())))
        invalid = true;
    } else if (auto sample = dyn_cast<sim::SimCovergroupSampleOp>(operation)) {
      uint64_t type = declarationType(sample, sample.getHandle().getType());
      if (failed(verifyExpressionBatch(sample, sample.getExpressionIds(),
                                       sample.getValues(), type, Phase::Sample,
                                       "sample")))
        invalid = true;
    }
  });
  return failure(invalid);
}

} // namespace obelisk

#endif
