//===- LowerUnitCovergroups.cpp - Lower covergroup semantics -------------===//

#include "LowerUnit.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"

#include "obelisk/Coverage/CoverageDatabase.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"

using namespace mlir;

namespace obelisk::simlowering {

semantic::SVCovergroupTypeOp
UnitLowering::findSemanticCovergroup(Operation *operation) {
  ensureCoverageInventory();
  SmallVector<StringRef> candidateNames;
  auto semanticType = operation->getAttrOfType<TypeAttr>("semantic_type");
  if (semanticType)
    if (auto handle =
            dyn_cast<semantic::CovergroupHandleType>(semanticType.getValue())) {
      SymbolRefAttr reference = handle.getCovergroupName();
      candidateNames.push_back(reference.getLeafReference());
    }

  operation->walk<WalkOrder::PreOrder>([&](Operation *nested) {
    if (nested == operation)
      return WalkResult::advance();
    auto type = nested->getAttrOfType<TypeAttr>("semantic_type");
    auto handle =
        type ? dyn_cast<semantic::CovergroupHandleType>(type.getValue())
             : semantic::CovergroupHandleType{};
    if (!handle)
      return WalkResult::advance();
    candidateNames.push_back(handle.getCovergroupName().getLeafReference());
    return WalkResult::interrupt();
  });

  auto call = dyn_cast<semantic::SVCallExpressionOp>(operation);
  if (call && call.getReferencedSymbol()) {
    SymbolRefAttr referenced = *call.getReferencedSymbol();
    for (FlatSymbolRefAttr nested : referenced.getNestedReferences())
      candidateNames.push_back(nested.getValue());
  }
  for (StringRef name : candidateNames) {
    auto found = semanticCovergroups.find(name);
    if (found != semanticCovergroups.end())
      return found->second;
  }
  return {};
}

FailureOr<std::optional<Value>> UnitLowering::lowerCovergroupOptionAssignment(
    semantic::SVAssignmentExpressionOp op, Operation *destination,
    Operation *source, Value rhs) {
  auto field = dyn_cast<semantic::SVMemberAccessExpressionOp>(destination);
  SmallVector<Operation *> fieldChildren = getChildren(destination);
  if (!field || fieldChildren.size() != 1)
    return std::optional<Value>{};
  Operation *optionExpression = fieldChildren.front();
  auto option =
      dyn_cast<semantic::SVMemberAccessExpressionOp>(optionExpression);
  auto optionName =
      optionExpression->getAttrOfType<StringAttr>("member_name");
  SmallVector<Operation *> optionChildren;
  bool typeOption = false;
  if (option && optionName && optionName.getValue() == "option") {
    optionChildren = getChildren(optionExpression);
    if (optionChildren.size() != 1)
      return std::optional<Value>{};
  } else {
    auto reference =
        optionExpression->getAttrOfType<SymbolRefAttr>("referenced_symbol");
    auto property =
        reference ? lookupNearestSymbolFrom<semantic::SVClassPropertySymbolOp>(
                        optionExpression, reference)
                  : semantic::SVClassPropertySymbolOp{};
    auto referencedPath =
        optionExpression->getAttrOfType<StringAttr>("referenced_path");
    if ((!property || property.getName() != "type_option") &&
        (!referencedPath ||
         !referencedPath.getValue().ends_with(".type_option")))
      return std::optional<Value>{};
    typeOption = true;
    optionChildren.push_back(optionExpression);
  }

  Location location = getSemanticLocation(op);
  if (op.getHasTimingControl() || op.getOperatorKind() ||
      op.getAssignmentKind() != semantic::SVAssignmentKind::Blocking) {
    emitError(location)
        << "covergroup options require an untimed blocking simple assignment";
    return failure();
  }

  Operation *owner = optionChildren.front();
  auto fieldName = destination->getAttrOfType<StringAttr>("member_name");
  if (!fieldName)
    return std::optional<Value>{};

  if (typeOption) {
    auto reference = owner->getAttrOfType<SymbolRefAttr>("referenced_symbol");
    auto property =
        reference ? lookupNearestSymbolFrom<semantic::SVClassPropertySymbolOp>(
                        owner, reference)
                  : semantic::SVClassPropertySymbolOp{};
    auto covergroup =
        property
            ? property->getParentOfType<semantic::SVCovergroupTypeOp>()
            : semantic::SVCovergroupTypeOp{};
    Operation *semanticItem = nullptr;
    if (property) {
      semanticItem =
          property->getParentOfType<semantic::SVCoverpointSymbolOp>()
              .getOperation();
      if (!semanticItem)
        semanticItem =
            property->getParentOfType<semantic::SVCoverCrossSymbolOp>()
                .getOperation();
    }
    auto referencedPath = owner->getAttrOfType<StringAttr>("referenced_path");
    if ((!covergroup || !property || property.getName() != "type_option") &&
        referencedPath) {
      ensureCoverageInventory();
      for (const auto &entry : semanticCovergroups) {
        semantic::SVCovergroupTypeOp candidate = entry.second;
        if ((Twine(getHierarchyName(candidate)) + ".type_option").str() ==
            referencedPath.getValue()) {
          covergroup = candidate;
          semanticItem = nullptr;
          break;
        }
        candidate->walk([&](Operation *item) {
          if (semanticItem ||
              !isa<semantic::SVCoverpointSymbolOp,
                   semantic::SVCoverCrossSymbolOp>(item))
            return;
          if ((Twine(getHierarchyName(item)) + ".type_option").str() ==
              referencedPath.getValue()) {
            covergroup = candidate;
            semanticItem = item;
          }
        });
        if (semanticItem)
          break;
      }
    }
    if (!covergroup) {
      emitError(location) << "functional coverage type option has no typed owner";
      return failure();
    }
    uint64_t itemID = 0;
    if (semanticItem) {
      auto stableID = semanticItem->getAttrOfType<IntegerAttr>(
          sim::metadata::coverageFunctionalItemId);
      if (!stableID || stableID.getValue().isZero()) {
        emitError(location)
            << "functional coverage item type option has no typed v1 identity";
        return failure();
      }
      itemID = stableID.getValue().getZExtValue();
    }

    FailureOr<Type> loweredType = getNormalizedSemanticType(covergroup);
    auto handleType = succeeded(loweredType)
                          ? dyn_cast<sim::CovergroupHandleType>(*loweredType)
                          : sim::CovergroupHandleType{};
    auto declaration = handleType
                           ? lookupNearestSymbolFrom<sim::SimCovergroupDeclOp>(
                                 function, handleType.getCovergroupName())
                           : sim::SimCovergroupDeclOp{};
    if (!declaration || !declaration.getSchemaType()) {
      emitError(location)
          << "functional coverage type option has no typed v1 declaration";
      return failure();
    }

    Value context = function.getBody().front().getArgument(0);
    StringRef name = fieldName.getValue();
    if (name == "comment") {
      if (!isa<sim::StringType>(rhs.getType())) {
        emitError(location) << "type_option.comment requires a string value";
        return failure();
      }
      sim::SimCovergroupSetTypeStringOptionOp::create(
          builder, location, context, declaration.getSchemaType(), itemID,
          sim::CovergroupInstanceOptionKind::Comment, rhs);
      // type_option is also an implicitly declared static struct (19.10).
      // Leave the result empty so lowerAssignment emits the ordinary lvalue
      // store after publishing the coverage-service update.
      return std::optional<Value>{};
    }

    std::optional<sim::CovergroupInstanceOptionKind> kind;
    if (name == "weight")
      kind = sim::CovergroupInstanceOptionKind::Weight;
    else if (name == "goal")
      kind = sim::CovergroupInstanceOptionKind::Goal;
    else if (name == "merge_instances" && !itemID)
      kind = sim::CovergroupInstanceOptionKind::MergeInstances;
    if (!kind) {
      emitError(location) << "covergroup type option '" << name
                          << "' is not supported procedurally";
      return failure();
    }
    FailureOr<Value> value = convert(rhs, builder.getI64Type(),
                                     isSignedNode(source), location, true);
    if (failed(value))
      return failure();
    sim::SimCovergroupSetTypeIntegerOptionOp::create(
        builder, location, context, declaration.getSchemaType(), itemID, *kind,
        *value);
    return std::optional<Value>{};
  }

  Operation *receiver = owner;
  Operation *itemOwner = nullptr;
  if (auto member = dyn_cast<semantic::SVMemberAccessExpressionOp>(owner)) {
    SmallVector<Operation *> memberChildren = getChildren(owner);
    if (memberChildren.size() != 1) {
      emitError(location) << "functional coverage option has no instance receiver";
      return failure();
    }
    itemOwner = member;
    receiver = memberChildren.front();
  }
  semantic::SVCovergroupTypeOp covergroup = findSemanticCovergroup(receiver);
  if (!covergroup) {
    emitError(location) << "functional coverage option has no covergroup type";
    return failure();
  }

  uint64_t itemID = 0;
  Operation *semanticItem = nullptr;
  if (itemOwner) {
    auto reference = itemOwner->getAttrOfType<SymbolRefAttr>("referenced_symbol");
    if (reference) {
      StringRef leaf = reference.getLeafReference();
      covergroup->walk([&](Operation *candidate) {
        if (semanticItem ||
            !isa<semantic::SVCoverpointSymbolOp,
                 semantic::SVCoverCrossSymbolOp>(candidate))
          return;
        auto symbol = candidate->getAttrOfType<StringAttr>(
            SymbolTable::getSymbolAttrName());
        if (symbol && symbol.getValue() == leaf)
          semanticItem = candidate;
      });
    }
    auto stableID = semanticItem
                        ? semanticItem->getAttrOfType<IntegerAttr>(
                              sim::metadata::coverageFunctionalItemId)
                        : IntegerAttr{};
    if (!stableID || stableID.getValue().isZero()) {
      emitError(location)
          << "functional coverage item option has no typed v1 identity";
      return failure();
    }
    itemID = stableID.getValue().getZExtValue();
  }

  FailureOr<Value> loweredReceiver = lowerExpression(receiver);
  if (failed(loweredReceiver) ||
      !isa<sim::CovergroupHandleType>((*loweredReceiver).getType())) {
    emitError(location) << "functional coverage option receiver is not a covergroup instance";
    return failure();
  }
  Value context = function.getBody().front().getArgument(0);
  StringRef name = fieldName.getValue();
  if (name == "name") {
    if (itemID || !isa<sim::StringType>(rhs.getType())) {
      emitError(location) << "option.name is a covergroup string option";
      return failure();
    }
    sim::SimCovergroupSetNameOp::create(builder, location, context,
                                        *loweredReceiver, rhs);
    return std::optional<Value>{rhs};
  }
  if (name == "comment") {
    if (!isa<sim::StringType>(rhs.getType())) {
      emitError(location) << "option.comment requires a string value";
      return failure();
    }
    sim::SimCovergroupSetStringOptionOp::create(
        builder, location, context, *loweredReceiver,
        itemID,
        sim::CovergroupInstanceOptionKind::Comment, rhs);
    return std::optional<Value>{rhs};
  }

  std::optional<sim::CovergroupInstanceOptionKind> kind;
  if (name == "weight")
    kind = sim::CovergroupInstanceOptionKind::Weight;
  else if (name == "goal")
    kind = sim::CovergroupInstanceOptionKind::Goal;
  else if (name == "at_least")
    kind = sim::CovergroupInstanceOptionKind::AtLeast;
  else if (name == "cross_num_print_missing" &&
           (!itemID || isa<semantic::SVCoverCrossSymbolOp>(semanticItem)))
    kind = sim::CovergroupInstanceOptionKind::CrossNumPrintMissing;
  if (!kind) {
    emitError(location) << "covergroup option '" << name
                        << "' cannot be assigned procedurally";
    return failure();
  }
  FailureOr<Value> value = convert(rhs, builder.getI64Type(),
                                   isSignedNode(source), location, true);
  if (failed(value))
    return failure();
  sim::SimCovergroupSetIntegerOptionOp::create(
      builder, location, context, *loweredReceiver, itemID, *kind, *value);
  return std::optional<Value>{rhs};
}

FailureOr<std::optional<Value>> UnitLowering::lowerCovergroupIntegerOptionRead(
    semantic::SVMemberAccessExpressionOp op) {
  auto fieldName = op->getAttrOfType<StringAttr>("member_name");
  SmallVector<Operation *> fieldChildren = getChildren(op);
  if (!fieldName || fieldChildren.size() != 1)
    return std::optional<Value>{};
  auto option =
      dyn_cast<semantic::SVMemberAccessExpressionOp>(fieldChildren.front());
  auto optionName =
      fieldChildren.front()->getAttrOfType<StringAttr>("member_name");
  if (!option || !optionName || optionName.getValue() != "option")
    return std::optional<Value>{};
  SmallVector<Operation *> optionChildren = getChildren(option);
  if (optionChildren.size() != 1)
    return std::optional<Value>{};

  std::optional<sim::CovergroupInstanceOptionKind> kind;
  StringRef name = fieldName.getValue();
  if (name == "weight")
    kind = sim::CovergroupInstanceOptionKind::Weight;
  else if (name == "goal")
    kind = sim::CovergroupInstanceOptionKind::Goal;
  else if (name == "at_least")
    kind = sim::CovergroupInstanceOptionKind::AtLeast;
  else if (name == "cross_num_print_missing")
    kind = sim::CovergroupInstanceOptionKind::CrossNumPrintMissing;
  else
    return std::optional<Value>{};

  Location location = getSemanticLocation(op);
  Operation *owner = optionChildren.front();
  Operation *receiver = owner;
  Operation *itemOwner = nullptr;
  if (auto member = dyn_cast<semantic::SVMemberAccessExpressionOp>(owner)) {
    SmallVector<Operation *> memberChildren = getChildren(member);
    if (memberChildren.size() != 1) {
      emitError(location)
          << "functional coverage option has no instance receiver";
      return failure();
    }
    itemOwner = owner;
    receiver = memberChildren.front();
  }

  semantic::SVCovergroupTypeOp covergroup = findSemanticCovergroup(receiver);
  if (!covergroup)
    return std::optional<Value>{};

  uint64_t itemID = 0;
  Operation *semanticItem = nullptr;
  if (itemOwner) {
    auto reference =
        itemOwner->getAttrOfType<SymbolRefAttr>("referenced_symbol");
    if (reference) {
      StringRef leaf = reference.getLeafReference();
      covergroup->walk([&](Operation *candidate) {
        if (semanticItem || !isa<semantic::SVCoverpointSymbolOp,
                                 semantic::SVCoverCrossSymbolOp>(candidate))
          return;
        auto symbol = candidate->getAttrOfType<StringAttr>(
            SymbolTable::getSymbolAttrName());
        if (symbol && symbol.getValue() == leaf)
          semanticItem = candidate;
      });
    }
    auto stableID = semanticItem ? semanticItem->getAttrOfType<IntegerAttr>(
                                       sim::metadata::coverageFunctionalItemId)
                                 : IntegerAttr{};
    if (!stableID || stableID.getValue().isZero()) {
      emitError(location)
          << "functional coverage item option has no typed v1 identity";
      return failure();
    }
    itemID = stableID.getValue().getZExtValue();
  }
  if (*kind == sim::CovergroupInstanceOptionKind::CrossNumPrintMissing &&
      itemID && !isa<semantic::SVCoverCrossSymbolOp>(semanticItem)) {
    emitError(location)
        << "cross_num_print_missing is only available on a covergroup or cross";
    return failure();
  }

  FailureOr<Value> loweredReceiver = lowerExpression(receiver);
  if (failed(loweredReceiver) ||
      !isa<sim::CovergroupHandleType>((*loweredReceiver).getType())) {
    emitError(location)
        << "functional coverage option receiver is not a covergroup instance";
    return failure();
  }
  Value context = function.getBody().front().getArgument(0);
  Value value = sim::SimCovergroupGetIntegerOptionOp::create(
                    builder, location, context, *loweredReceiver, itemID, *kind)
                    .getValue();
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  if (failed(resultType))
    return failure();
  FailureOr<Value> converted =
      convert(value, *resultType, true, location, isSignedNode(op));
  if (failed(converted))
    return failure();
  return std::optional<Value>{*converted};
}

FailureOr<Value>
UnitLowering::lowerNewCovergroup(semantic::SVNewCovergroupExpressionOp op,
                                 semantic::SVCovergroupTypeOp covergroup,
                                 bool dispatchInheritance) {
  Location location = getSemanticLocation(op);
  if (!covergroup)
    covergroup = findSemanticCovergroup(op);
  if (!covergroup) {
    emitError(location) << "covergroup construction has no semantic type";
    return failure();
  }
  FailureOr<Type> type = dispatchInheritance && !thisObject
                             ? getNormalizedSemanticType(op)
                             : getNormalizedSemanticType(covergroup);
  auto handleType = succeeded(type) ? dyn_cast<sim::CovergroupHandleType>(*type)
                                    : sim::CovergroupHandleType{};
  if (!handleType) {
    emitError(location) << "covergroup construction has no handle type";
    return failure();
  }
  auto declaration =
      dyn_cast<FlatSymbolRefAttr>(handleType.getCovergroupName());
  if (!declaration) {
    emitError(location) << "covergroup handle has no flat declaration";
    return failure();
  }
  if (thisObject && dispatchInheritance) {
    SmallVector<semantic::SVCovergroupTypeOp> derivedGroups;
    for (const auto &entry : semanticCovergroups) {
      semantic::SVCovergroupTypeOp candidate = entry.second;
      TypeAttr base =
          candidate->getAttrOfType<TypeAttr>("obelisk.coverage.base_group");
      if (base && base.getValue() == covergroup.getSemanticType())
        derivedGroups.push_back(candidate);
    }
    llvm::sort(derivedGroups, [](auto lhs, auto rhs) {
      return std::tuple(getHierarchyName(lhs), lhs.getSymName()) <
             std::tuple(getHierarchyName(rhs), rhs.getSymName());
    });
    if (!derivedGroups.empty()) {
      Block *resume = addBlock();
      resume->addArgument(handleType, location);
      for (semantic::SVCovergroupTypeOp derived : derivedGroups) {
        auto owner = derived->getParentOfType<semantic::SVClassTypeOp>();
        auto ownerHandle =
            owner ? dyn_cast<semantic::ClassHandleType>(owner.getSemanticType())
                  : semantic::ClassHandleType{};
        if (!ownerHandle) {
          derived.emitError("inherited covergroup has no owning class");
          return failure();
        }
        FlatSymbolRefAttr target = FlatSymbolRefAttr::get(
            getSimulationClassSymbol(ownerHandle.getClassName()));
        Value matches = sim::SimClassIsInstanceOp::create(builder, location,
                                                          thisObject, target);
        Block *selected = addBlock();
        Block *next = addBlock();
        cf::CondBranchOp::create(builder, location, matches, selected,
                                 ValueRange{}, next, ValueRange{});
        setCurrent(selected);
        FailureOr<Value> selectedHandle = lowerNewCovergroup(op, derived, true);
        if (failed(selectedHandle))
          return failure();
        FailureOr<Value> adjusted =
            convert(*selectedHandle, handleType, false, location);
        if (failed(adjusted))
          return failure();
        cf::BranchOp::create(builder, location, resume, ValueRange{*adjusted});
        setCurrent(next);
      }
      FailureOr<Value> baseHandle = lowerNewCovergroup(op, covergroup, false);
      if (failed(baseHandle))
        return failure();
      cf::BranchOp::create(builder, location, resume, ValueRange{*baseHandle});
      setCurrent(resume);
      return resume->getArgument(0);
    }
  }
  Value context = function.getBody().front().getArgument(0);
  SmallVector<semantic::SVFormalArgumentSymbolOp> formals;
  for (Operation *child : getChildren(covergroup))
    if (auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child);
        formal && !formal.getIsCoverageSampleFormal().value_or(false))
      formals.push_back(formal);
  SmallVector<Operation *> actuals = getChildren(op);
  if (actuals.size() != formals.size() ||
      op.getArgumentCount() != formals.size()) {
    emitError(location)
        << "covergroup constructor argument inventory is malformed";
    return failure();
  }
  std::optional<ArrayRef<int64_t>> defaulted = op.getDefaultedArguments();
  if (defaulted && (defaulted->size() != formals.size() ||
                    !llvm::all_of(*defaulted, [](int64_t value) {
                      return value == 0 || value == 1;
                    }))) {
    emitError(location)
        << "covergroup constructor default-argument inventory is malformed";
    return failure();
  }

  struct SavedFormal {
    std::string path;
    Value value;
    Value lvalue;
  };
  SmallVector<SavedFormal> saved;
  SmallVector<Value> arguments;
  SmallVector<int64_t> formalIDs;
  SmallVector<Value> defaultValues;
  SmallVector<int64_t> defaultExpressionIDs;
  auto bindInheritedFormalAliases =
      [&](semantic::SVFormalArgumentSymbolOp formal, StringRef canonicalPath) {
        for (Operation *child : getChildren(covergroup)) {
          auto alias = dyn_cast<semantic::SVTransparentMemberSymbolOp>(child);
          if (!alias || getDebugName(alias) != getDebugName(formal))
            continue;
          std::string aliasPath = getHierarchyName(alias).str();
          if (aliasPath == canonicalPath)
            continue;
          saved.push_back(
              {aliasPath, values.lookup(aliasPath), lvalues.lookup(aliasPath)});
          values[aliasPath] = values.lookup(canonicalPath);
          if (Value reference = lvalues.lookup(canonicalPath))
            lvalues[aliasPath] = reference;
        }
      };
  for (auto [index, formal, actual] : llvm::enumerate(formals, actuals)) {
    auto id = formal->getAttrOfType<IntegerAttr>(
        sim::metadata::coverageFunctionalFormalId);
    FailureOr<Type> type = getNormalizedSemanticType(formal);
    if (!id || id.getValue().isNegative() || id.getValue().isZero() ||
        failed(type)) {
      formal.emitError("constructor formal is absent from the embedded typed "
                       "v1 FunctionalFormal plan");
      return failure();
    }
    std::string path = getHierarchyName(formal).str();
    saved.push_back({path, values.lookup(path), lvalues.lookup(path)});
    if (formal.getDirection() == semantic::SVArgumentDirection::Ref) {
      FailureOr<Value> destination = lowerExpression(actual, true);
      if (failed(destination))
        return failure();
      FailureOr<Value> reference =
          toArgumentReference(*destination, *type, getSemanticLocation(actual));
      if (failed(reference)) {
        emitError(getSemanticLocation(actual))
            << "covergroup ref actual type must exactly match its formal";
        return failure();
      }
      arguments.push_back(*reference);
      lvalues[path] = *reference;
      values[path] = sim::SimArgumentRefLoadOp::create(
          builder, getSemanticLocation(actual), *type, *reference);
    } else {
      FailureOr<Value> value = lowerExpression(actual);
      FailureOr<Value> converted =
          succeeded(value)
              ? convert(*value, *type, isSignedNode(actual),
                        getSemanticLocation(actual), isSignedNode(formal))
              : FailureOr<Value>(failure());
      if (failed(converted))
        return failure();
      arguments.push_back(*converted);
      values[path] = *converted;
    }
    bindInheritedFormalAliases(formal, path);
    formalIDs.push_back(static_cast<int64_t>(id.getValue().getZExtValue()));
    if (defaulted && (*defaulted)[index]) {
      SmallVector<Operation *> defaultChildren = getChildren(formal);
      auto defaultID =
          defaultChildren.size() == 1
              ? defaultChildren.front()->getAttrOfType<IntegerAttr>(
                    sim::metadata::coverageFunctionalExpressionId)
              : IntegerAttr{};
      if (!defaultID || defaultID.getValue().isNegative() ||
          defaultID.getValue().isZero()) {
        formal.emitError("defaulted constructor formal has no typed v1 "
                         "FunctionalExpression identity");
        return failure();
      }
      // The frontend has materialized the default as this actual. Reuse the
      // once-evaluated formal value so the default is represented in the
      // expression batch without evaluating it a second time.  In particular,
      // a ref formal's constructor payload is an ArgumentRef while its
      // FormalDefault result is the scalar value loaded through that alias.
      defaultValues.push_back(values[path]);
      defaultExpressionIDs.push_back(
          static_cast<int64_t>(defaultID.getValue().getZExtValue()));
    }
  }
  llvm::scope_exit restoreFormals([&] {
    for (const SavedFormal &entry : saved) {
      if (entry.value)
        values[entry.path] = entry.value;
      else
        values.erase(entry.path);
      if (entry.lvalue)
        lvalues[entry.path] = entry.lvalue;
      else
        lvalues.erase(entry.path);
    }
  });

  struct ConstructorExpression {
    Operation *expression = nullptr;
    uint64_t id = 0;
    uint32_t phase = 0;
    uint32_t ordinal = 0;
    SmallVector<IntegerAttr> candidates;
    SmallVector<std::string> iteratorPaths;
  };
  SmallVector<ConstructorExpression> constructorExpressions;
  WalkResult inventoryResult = covergroup->walk<
      WalkOrder::PreOrder>([&](Operation *expression) -> WalkResult {
    if (auto ids = expression->getAttrOfType<DenseI64ArrayAttr>(
            sim::metadata::coverageFunctionalWithExpressionIds)) {
      auto ordinals = expression->getAttrOfType<DenseI64ArrayAttr>(
          sim::metadata::coverageFunctionalWithExpressionOrdinals);
      auto candidates = expression->getAttrOfType<ArrayAttr>(
          sim::metadata::coverageFunctionalWithCandidateValues);
      auto iteratorPath = expression->getAttrOfType<StringAttr>(
          sim::metadata::coverageFunctionalWithIteratorPath);
      auto crossCandidates = expression->getAttrOfType<ArrayAttr>(
          sim::metadata::coverageFunctionalCrossWithCandidateValues);
      auto crossTargetPaths = expression->getAttrOfType<ArrayAttr>(
          sim::metadata::coverageFunctionalCrossWithTargetPaths);
      ArrayRef<int64_t> idValues = ids.asArrayRef();
      ArrayRef<int64_t> ordinalValues =
          ordinals ? ordinals.asArrayRef() : ArrayRef<int64_t>{};
      const bool stateBatch =
          candidates && iteratorPath && !crossCandidates && !crossTargetPaths;
      const bool crossBatch =
          !candidates && !iteratorPath && crossCandidates && crossTargetPaths &&
          !crossTargetPaths.empty() &&
          crossCandidates.size() % crossTargetPaths.size() == 0 &&
          crossCandidates.size() / crossTargetPaths.size() == idValues.size();
      if (!ordinals || (!stateBatch && !crossBatch) || idValues.empty() ||
          idValues.size() != ordinalValues.size() ||
          (stateBatch && idValues.size() != candidates.size()) ||
          (stateBatch &&
           idValues.size() > coverage::MaxFunctionalWithCandidates) ||
          (crossBatch &&
           idValues.size() > coverage::MaxFunctionalCrossWithCandidates)) {
        expression->emitError(
            "constructor with-expression batch metadata is malformed");
        return WalkResult::interrupt();
      }
      SmallVector<std::string> targetPaths;
      if (crossBatch) {
        targetPaths.reserve(crossTargetPaths.size());
        for (Attribute path : crossTargetPaths) {
          auto string = dyn_cast<StringAttr>(path);
          if (!string || string.getValue().empty()) {
            expression->emitError(
                "constructor cross with-expression target path is malformed");
            return WalkResult::interrupt();
          }
          targetPaths.push_back(string.getValue().str());
        }
      }
      for (auto [index, id, ordinal] :
           llvm::enumerate(idValues, ordinalValues)) {
        SmallVector<IntegerAttr> tuple;
        if (stateBatch) {
          auto integer = dyn_cast<IntegerAttr>(candidates[index]);
          if (integer)
            tuple.push_back(integer);
        } else {
          tuple.reserve(targetPaths.size());
          for (Attribute candidate : crossCandidates.getValue().slice(
                   index * targetPaths.size(), targetPaths.size())) {
            auto integer = dyn_cast<IntegerAttr>(candidate);
            if (integer)
              tuple.push_back(integer);
          }
        }
        if (!id || ordinal < 0 || ordinal > UINT32_MAX ||
            tuple.size() != (stateBatch ? 1 : targetPaths.size()) ||
            (index && ordinal != ordinalValues[index - 1] + 1)) {
          expression->emitError(
              "constructor with-expression entries are malformed or not "
              "in contiguous typed v1 result-ordinal order");
          return WalkResult::interrupt();
        }
        SmallVector<std::string> paths =
            stateBatch ? SmallVector<std::string>{iteratorPath.getValue().str()}
                       : targetPaths;
        constructorExpressions.push_back(
            {expression, static_cast<uint64_t>(id),
             static_cast<uint32_t>(
                 coverage::FunctionalExpressionEvaluationPhase::Constructor),
             static_cast<uint32_t>(ordinal), std::move(tuple),
             std::move(paths)});
      }
      return WalkResult::skip();
    }

    auto phase = expression->getAttrOfType<IntegerAttr>(
        sim::metadata::coverageFunctionalExpressionPhase);
    auto role = expression->getAttrOfType<IntegerAttr>(
        sim::metadata::coverageFunctionalExpressionRole);
    const uint64_t phaseValue =
        phase ? phase.getValue().getZExtValue() : uint64_t{0};
    if ((phaseValue !=
             static_cast<uint32_t>(
                 coverage::FunctionalExpressionEvaluationPhase::Constructor) &&
         phaseValue !=
             static_cast<uint32_t>(
                 coverage::FunctionalExpressionEvaluationPhase::Option)) ||
        (role && role.getValue().getZExtValue() ==
                     static_cast<uint32_t>(
                         coverage::FunctionalExpressionRole::FormalDefault)))
      return WalkResult::advance();
    auto id = expression->getAttrOfType<IntegerAttr>(
        sim::metadata::coverageFunctionalExpressionId);
    auto ordinal = expression->getAttrOfType<IntegerAttr>(
        sim::metadata::coverageFunctionalExpressionOrdinal);
    if (!phase || !id || !ordinal || id.getValue().isZero() ||
        phase.getValue().ugt(UINT32_MAX) ||
        ordinal.getValue().ugt(UINT32_MAX)) {
      expression->emitError(
          "constructor expression has an invalid typed v1 identity, "
          "phase, or result ordinal");
      return WalkResult::interrupt();
    }
    constructorExpressions.push_back(
        {expression,
         id.getValue().getZExtValue(),
         static_cast<uint32_t>(phaseValue),
         static_cast<uint32_t>(ordinal.getValue().getZExtValue()),
         {},
         {}});
    return WalkResult::advance();
  });
  if (inventoryResult.wasInterrupted())
    return failure();
  llvm::sort(constructorExpressions, [](const ConstructorExpression &lhs,
                                        const ConstructorExpression &rhs) {
    return std::tie(lhs.phase, lhs.ordinal) < std::tie(rhs.phase, rhs.ordinal);
  });
  SmallVector<Value> constructorValues(defaultValues);
  SmallVector<int64_t> expressionIDs(defaultExpressionIDs);
  std::optional<std::pair<uint32_t, uint32_t>> previousPosition;
  llvm::DenseSet<uint64_t> seenExpressionIDs;
  for (const ConstructorExpression &entry : constructorExpressions) {
    Operation *expression = entry.expression;
    const std::pair position{entry.phase, entry.ordinal};
    if (previousPosition && *previousPosition >= position) {
      expression->emitError(
          "constructor expressions are not in unique typed v1 "
          "result-ordinal order");
      return failure();
    }
    previousPosition = position;
    if (!seenExpressionIDs.insert(entry.id).second) {
      expression->emitError("functional expression ID hash collision");
      return failure();
    }

    SmallVector<Value> previousIteratorValues;
    if (!entry.candidates.empty()) {
      if (entry.candidates.size() != entry.iteratorPaths.size()) {
        expression->emitError(
            "constructor with-expression tuple metadata is malformed");
        return failure();
      }
      auto point =
          expression->getParentOfType<semantic::SVCoverpointSymbolOp>();
      Type stateScalar;
      if (point && entry.candidates.size() == 1) {
        FailureOr<Type> pointType = getNormalizedSemanticType(point);
        if (succeeded(pointType))
          stateScalar = sim::getPackedScalarType(*pointType);
      }
      previousIteratorValues.reserve(entry.iteratorPaths.size());
      for (auto [candidate, path] :
           llvm::zip_equal(entry.candidates, entry.iteratorPaths)) {
        auto candidateType = dyn_cast<IntegerType>(candidate.getType());
        Type scalar = stateScalar;
        bool inconsistentIteratorType = false;
        if (!scalar && !path.empty()) {
          expression->walk([&](semantic::SVNamedValueExpressionOp reference) {
            if (reference.getReferencedPath() != path)
              return;
            FailureOr<Type> normalized = getNormalizedSemanticType(reference);
            Type referencedScalar = succeeded(normalized)
                                        ? sim::getPackedScalarType(*normalized)
                                        : Type{};
            if (!referencedScalar || (scalar && scalar != referencedScalar)) {
              inconsistentIteratorType = true;
              return;
            }
            scalar = referencedScalar;
          });
        }
        if (!scalar)
          scalar = candidateType;
        std::optional<unsigned> width = sim::getPackedWidth(scalar);
        const bool validCandidateWidth =
            width && candidateType &&
            (candidateType.getWidth() == *width ||
             (isa<sim::LogicType>(scalar) &&
              candidateType.getWidth() == *width * 2));
        if (inconsistentIteratorType || !scalar || !validCandidateWidth) {
          expression->emitError(
              "constructor with-expression candidate type is malformed");
          return failure();
        }
        if (path.empty()) {
          previousIteratorValues.push_back({});
          continue;
        }
        previousIteratorValues.push_back(values.lookup(path));
        if (auto integer = dyn_cast<IntegerType>(scalar)) {
          values[path] = arith::ConstantOp::create(
              builder, getSemanticLocation(expression), integer,
              builder.getIntegerAttr(integer, candidate.getValue()));
        } else if (auto logic = dyn_cast<sim::LogicType>(scalar)) {
          auto planeType = builder.getIntegerType(logic.getWidth());
          llvm::APInt value = candidate.getValue().trunc(logic.getWidth());
          llvm::APInt unknown = candidateType.getWidth() == logic.getWidth()
                                    ? llvm::APInt::getZero(logic.getWidth())
                                    : candidate.getValue()
                                          .lshr(logic.getWidth())
                                          .trunc(logic.getWidth());
          values[path] = sim::SimLogicConstantOp::create(
              builder, getSemanticLocation(expression), logic,
              builder.getIntegerAttr(planeType, value),
              builder.getIntegerAttr(planeType, unknown));
        } else {
          expression->emitError(
              "constructor with-expression iterator is not integral");
          return failure();
        }
      }
    }
    llvm::scope_exit restoreIterator([&] {
      for (auto [path, previous] :
           llvm::zip_equal(entry.iteratorPaths, previousIteratorValues)) {
        if (path.empty())
          continue;
        if (previous)
          values[path] = previous;
        else
          values.erase(path);
      }
    });

    FailureOr<Value> lowered = lowerExpression(expression);
    if (failed(lowered))
      return failure();
    Value value = *lowered;
    if (!entry.candidates.empty()) {
      FailureOr<Value> predicate =
          truthValue(value, getSemanticLocation(expression));
      if (failed(predicate))
        return failure();
      constructorValues.push_back(*predicate);
      expressionIDs.push_back(static_cast<int64_t>(entry.id));
      continue;
    }

    auto kind = expression->getAttrOfType<IntegerAttr>(
        sim::metadata::coverageFunctionalExpressionKind);
    auto bitWidth = expression->getAttrOfType<IntegerAttr>(
        sim::metadata::coverageFunctionalExpressionBitWidth);
    auto signedness = expression->getAttrOfType<IntegerAttr>(
        sim::metadata::coverageFunctionalExpressionSignedness);
    if (!kind || !bitWidth || !signedness) {
      expression->emitError(
          "constructor expression has no typed result descriptor");
      return failure();
    }
    auto resultKind = static_cast<coverage::FunctionalExpressionResultKind>(
        kind.getValue().getZExtValue());
    if (resultKind == coverage::FunctionalExpressionResultKind::Boolean) {
      FailureOr<Value> predicate =
          truthValue(value, getSemanticLocation(expression));
      if (failed(predicate))
        return failure();
      value = *predicate;
    } else if (resultKind ==
               coverage::FunctionalExpressionResultKind::Integral) {
      FailureOr<Value> scalar =
          toPackedScalar(value, getSemanticLocation(expression));
      if (failed(scalar))
        return failure();
      uint64_t width = bitWidth.getValue().getZExtValue();
      if (!width || width > UINT32_MAX) {
        expression->emitError(
            "constructor expression has an invalid integral result width");
        return failure();
      }
      Type targetType =
          isa<sim::LogicType>((*scalar).getType())
              ? Type(sim::LogicType::get(function.getContext(), width))
              : Type(builder.getIntegerType(width));
      FailureOr<Value> converted = convert(
          *scalar, targetType, isSignedNode(expression),
          getSemanticLocation(expression),
          signedness.getValue().getZExtValue() ==
              static_cast<uint32_t>(coverage::CoverageSignedness::Signed));
      if (failed(converted))
        return failure();
      value = *converted;
    } else if (resultKind ==
               coverage::FunctionalExpressionResultKind::TupleQueue) {
      auto queue = dyn_cast<sim::QueueType>(value.getType());
      if (!queue || queue.getBound() != 0 ||
          !isa<sim::UnpackedStructType>(queue.getElementType())) {
        expression->emitError("cross_set_expression did not lower to an "
                              "unbounded CrossQueueType");
        return failure();
      }
    } else if (resultKind == coverage::FunctionalExpressionResultKind::Set) {
      Type elementType;
      if (auto packed = dyn_cast<sim::PackedArrayType>(value.getType())) {
        elementType = packed.getElementType();
        Type temporaryType =
            sim::DynamicArrayType::get(function.getContext(), elementType);
        FailureOr<Value> converted =
            convert(value, temporaryType, /*sourceSigned=*/false,
                    getSemanticLocation(expression), /*targetSigned=*/false);
        if (failed(converted))
          return failure();
        value = *converted;
      } else if (auto fixed =
                     dyn_cast<sim::UnpackedArrayType>(value.getType())) {
        elementType = fixed.getElementType();
        FailureOr<ContainerElementDescriptor> descriptor =
            describeContainerElement(elementType,
                                     getSemanticLocation(expression));
        std::optional<uint64_t> elementSpan =
            sim::getProvenanceSpan(elementType);
        if (failed(descriptor) || !elementSpan || !*elementSpan) {
          expression->emitError("fixed coverage set does not have a "
                                "representable element layout");
          return failure();
        }
        uint64_t count = sim::getAggregateNumElements(fixed);
        Value size = arith::ConstantOp::create(
            builder, getSemanticLocation(expression), builder.getI64Type(),
            builder.getI64IntegerAttr(count));
        Type temporaryType =
            sim::DynamicArrayType::get(function.getContext(), elementType);
        Value temporary = sim::SimContainerCreateOp::create(
            builder, getSemanticLocation(expression), temporaryType, size,
            descriptor->typeID, descriptor->kind, descriptor->flags,
            descriptor->valueSize, descriptor->alignment, descriptor->bitWidth,
            builder.getDenseI64ArrayAttr(descriptor->traceOffsets),
            builder.getDenseI32ArrayAttr(descriptor->traceKinds),
            sim::ContainerKind::DynamicArray, 0);
        sim::SimContainerImportFixedOp::create(builder,
                                               getSemanticLocation(expression),
                                               temporary, value, *elementSpan);
        value = temporary;
      } else if (auto array =
                     dyn_cast<sim::DynamicArrayType>(value.getType())) {
        elementType = array.getElementType();
      } else if (auto queue = dyn_cast<sim::QueueType>(value.getType())) {
        elementType = queue.getElementType();
      } else {
        expression->emitError(
            "coverpoint bin set expression did not lower to a packed array, "
            "fixed unpacked array, dynamic array, or queue");
        return failure();
      }
      const uint64_t width = bitWidth.getValue().getZExtValue();
      const auto expressionSignedness =
          static_cast<coverage::CoverageSignedness>(
              signedness.getValue().getZExtValue());
      std::optional<unsigned> elementWidth = sim::getPackedWidth(elementType);
      const bool integral =
          elementWidth && *elementWidth == width && width &&
          expressionSignedness != coverage::CoverageSignedness::NotApplicable;
      auto realElement = dyn_cast<FloatType>(elementType);
      const bool real =
          realElement && realElement.getWidth() == width &&
          (width == 32 || width == 64) &&
          expressionSignedness == coverage::CoverageSignedness::NotApplicable;
      if (!integral && !real) {
        expression->emitError(
            "coverpoint bin set expression has inconsistent element metadata");
        return failure();
      }
    }
    constructorValues.push_back(value);
    expressionIDs.push_back(static_cast<int64_t>(entry.id));
  }
  Value handle = sim::SimCovergroupCreateOp::create(
      builder, location, handleType, context, declaration,
      llvm::to_vector(llvm::concat<Value>(arguments, constructorValues)),
      builder.getI32IntegerAttr(arguments.size()),
      builder.getDenseI64ArrayAttr(formalIDs),
      builder.getDenseI64ArrayAttr(expressionIDs));
  if (covergroup.getCoverageEventKind() ==
          semantic::SVCoverageEventKind::Clocking &&
      failed(deferCovergroupClockingSampler(op, covergroup, handle)))
    return failure();
  if (covergroup.getCoverageEventKind() ==
          semantic::SVCoverageEventKind::Block &&
      failed(deferCovergroupBlockEventSampler(op, covergroup, handle)))
    return failure();
  return handle;
}

LogicalResult UnitLowering::deferCovergroupBlockEventSampler(
    semantic::SVNewCovergroupExpressionOp construct,
    semantic::SVCovergroupTypeOp covergroup, Value handle) {
  auto node = construct->getAttrOfType<IntegerAttr>("node_id");
  if (!node)
    return construct.emitError(
        "block-event covergroup construction has no stable node identity");
  IntegerAttr samplerNode = node;
  if (covergroup->hasAttr("obelisk.coverage.base_group")) {
    std::string key =
        (Twine(node.getValue().getZExtValue()) + ":" + covergroup.getSymName())
            .str();
    samplerNode = builder.getI64IntegerAttr(stableCodeUnitID(key));
  }

  semantic::SVBlockEventListControlOp control;
  for (Operation *child : getChildren(covergroup))
    if ((control = dyn_cast<semantic::SVBlockEventListControlOp>(child)))
      break;
  if (!control)
    return construct.emitError(
        "block-event covergroup has no prepared event control");
  SmallVector<Operation *> targets = getChildren(control);
  if (targets.empty() || targets.size() != control.getEventKinds().size())
    return control.emitError("block-event target and kind counts differ");

  struct BlockEventGroup {
    Value receiver;
    SmallVector<int64_t> targetIDs;
    SmallVector<int32_t> eventKinds;
  };
  SmallVector<BlockEventGroup> eventGroups;
  for (auto [target, kindAttr] :
       llvm::zip_equal(targets, control.getEventKinds())) {
    auto targetID =
        target->getAttrOfType<IntegerAttr>(coverageBlockEventTargetIdAttrName);
    if (!targetID || !targetID.getValue().isStrictlyPositive())
      return target->emitError(
          "block-event target has no stable coverage identity");
    Value receiver;
    if (target->hasAttr(coverageBlockEventInstanceMethodAttrName)) {
      SmallVector<Operation *> receiverExpressions = getChildren(target);
      if (receiverExpressions.empty()) {
        if (!thisObject)
          return target->emitError(
              "instance-method block event has no receiver expression");
        receiver = thisObject;
      } else {
        if (receiverExpressions.size() != 1)
          return target->emitError(
              "instance-method block event must have one receiver expression");
        FailureOr<Value> loweredReceiver =
            lowerExpression(receiverExpressions.front());
        if (failed(loweredReceiver) ||
            !isa<sim::ClassHandleType>((*loweredReceiver).getType()))
          return target->emitError(
              "instance-method block-event receiver is not a class handle");
        receiver = *loweredReceiver;
      }
    }
    auto group = llvm::find_if(eventGroups, [&](const BlockEventGroup &entry) {
      return entry.receiver == receiver;
    });
    if (group == eventGroups.end()) {
      eventGroups.push_back({receiver, {}, {}});
      group = std::prev(eventGroups.end());
    }
    group->targetIDs.push_back(
        static_cast<int64_t>(targetID.getValue().getZExtValue()));
    auto kind = cast<semantic::SVCoverageBlockEventKindAttr>(kindAttr);
    group->eventKinds.push_back(
        kind.getValue() == semantic::SVCoverageBlockEventKind::Begin ? 0 : 1);
  }

  MLIRContext *context = function.getContext();
  SmallVector<DictionaryAttr> argumentAttrs{
      captureMetadata(builder, sim::CaptureKind::Context),
      captureMetadata(builder, sim::CaptureKind::Formal)};
  if (thisObject)
    argumentAttrs.push_back(captureMetadata(builder, sim::CaptureKind::Formal));
  const unsigned fixedArguments = thisObject ? 3 : 2;
  SmallVector<Attribute> bindings;
  SmallVector<Value> captures;
  llvm::StringSet<> referencedPaths;
  covergroup->walk([&](Operation *nested) {
    if (nested == control || control->isAncestor(nested))
      return;
    if (auto path = nested->getAttrOfType<StringAttr>("referenced_path");
        path && !path.getValue().empty())
      referencedPaths.insert(path.getValue());
    if (auto callCaptures =
            nested->getAttrOfType<ArrayAttr>(calleeCapturesAttrName))
      for (Attribute capture : callCaptures)
        referencedPaths.insert(cast<StringAttr>(capture).getValue());
    if (auto observerCaptures =
            nested->getAttrOfType<ArrayAttr>(observerCapturesAttrName))
      for (Attribute capture : observerCaptures)
        referencedPaths.insert(cast<StringAttr>(capture).getValue());
  });

  DenseMap<unsigned, unsigned> capturedArguments;
  llvm::StringSet<> boundPaths;
  for (Operation *child : getChildren(covergroup)) {
    auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child);
    if (!formal || formal.getIsCoverageSampleFormal().value_or(false))
      continue;
    SmallVector<std::string> paths{getHierarchyName(formal).str()};
    for (Operation *candidate : getChildren(covergroup)) {
      auto alias = dyn_cast<semantic::SVTransparentMemberSymbolOp>(candidate);
      if (alias && getDebugName(alias) == getDebugName(formal))
        paths.push_back(getHierarchyName(alias).str());
    }
    for (StringRef path : paths) {
      if (!referencedPaths.contains(path) || !boundPaths.insert(path).second)
        continue;
      Value value = lvalues.lookup(path);
      if (!value)
        value = values.lookup(path);
      if (!value)
        return construct.emitError()
               << "referenced covergroup constructor formal '" << path
               << "' has no construction-time binding";
      unsigned argument = captures.size() + fixedArguments;
      captures.push_back(value);
      argumentAttrs.push_back(
          captureMetadata(builder, sim::CaptureKind::Formal));
      bindings.push_back(sim::ArgumentBindingAttr::get(
          context, builder.getStringAttr(path), argument,
          sim::UnitArgumentKind::Direct, /*copyOut=*/false, IntegerAttr{},
          /*copyIn=*/true));
    }
  }
  if (ArrayAttr parentBindings =
          function->getAttrOfType<ArrayAttr>(bindingsAttrName))
    for (Attribute binding : parentBindings) {
      StringRef path = sim::getUnitBindingPath(binding);
      if (path.empty() || !referencedPaths.contains(path) ||
          boundPaths.contains(path))
        continue;
      if (isa<sim::DescriptorBindingAttr, sim::ConstantBindingAttr>(binding)) {
        bindings.push_back(binding);
        boundPaths.insert(path);
        continue;
      }
      auto argument = dyn_cast<sim::ArgumentBindingAttr>(binding);
      if (!argument ||
          (argument.getKind() != sim::UnitArgumentKind::Direct &&
           argument.getKind() != sim::UnitArgumentKind::LValueOnly))
        continue;
      const unsigned parentArgument = argument.getArgument();
      if (parentArgument >= function.getNumArguments())
        return function.emitError(
            "covergroup block-event capture references an invalid parent "
            "argument");
      auto [position, inserted] = capturedArguments.try_emplace(
          parentArgument, captures.size() + fixedArguments);
      if (inserted) {
        Value value = function.getArgument(parentArgument);
        captures.push_back(value);
        DictionaryAttr attrs = function.getArgAttrDict(parentArgument);
        if (!attrs)
          attrs = captureMetadata(builder, sim::CaptureKind::Formal);
        if (isa<sim::RefType, sim::ArgumentRefType, sim::NetType,
                sim::DriverType>(value.getType()) &&
            !isStaticallyAllocatedOverrideTarget(value) &&
            !attrs.contains("simulation.automatic_reference_capture")) {
          SmallVector<NamedAttribute> entries(attrs.begin(), attrs.end());
          entries.push_back(
              builder.getNamedAttr("simulation.automatic_reference_capture",
                                   builder.getUnitAttr()));
          attrs = builder.getDictionaryAttr(entries);
        }
        argumentAttrs.push_back(attrs);
      }
      bindings.push_back(sim::ArgumentBindingAttr::get(
          context, argument.getPath(), position->second, argument.getKind(),
          argument.getCopyOut(), argument.getLvalueNode(),
          argument.getCopyIn()));
      boundPaths.insert(path);
    }

  SmallVector<Value> baseCaptures;
  if (thisObject)
    baseCaptures.push_back(thisObject);
  llvm::append_range(baseCaptures, captures);
  for (auto [groupIndex, group] : llvm::enumerate(eventGroups)) {
    SmallVector<Value> planCaptures(baseCaptures);
    SmallVector<Attribute> planCaptureAttrs(argumentAttrs.begin(),
                                            argumentAttrs.end());
    if (group.receiver)
      planCaptures.insert(planCaptures.begin(), group.receiver);
    std::string groupIdentity = (Twine(samplerNode.getValue().getZExtValue()) +
                                 ":block-event:" + Twine(groupIndex))
                                    .str();
    IntegerAttr groupNode =
        builder.getI64IntegerAttr(stableCodeUnitID(groupIdentity));
    sim::SimCovergroupBlockEventPlanOp::create(
        builder, getSemanticLocation(construct), function.getArgument(0),
        handle, planCaptures,
        builder.getBoolAttr(static_cast<bool>(group.receiver)),
        builder.getI32IntegerAttr(thisObject ? 0 : -1), groupNode,
        covergroup.getSymNameAttr(), builder.getArrayAttr(bindings),
        builder.getArrayAttr(planCaptureAttrs),
        builder.getDenseI64ArrayAttr(group.targetIDs),
        builder.getDenseI32ArrayAttr(group.eventKinds));
  }
  return success();
}

LogicalResult UnitLowering::deferCovergroupClockingSampler(
    semantic::SVNewCovergroupExpressionOp construct,
    semantic::SVCovergroupTypeOp covergroup, Value handle) {
  auto node = construct->getAttrOfType<IntegerAttr>("node_id");
  if (!node)
    return construct.emitError(
        "clocking covergroup construction has no stable node identity");
  IntegerAttr samplerNode = node;
  if (covergroup->hasAttr("obelisk.coverage.base_group")) {
    std::string key =
        (Twine(node.getValue().getZExtValue()) + ":" + covergroup.getSymName())
            .str();
    samplerNode = builder.getI64IntegerAttr(stableCodeUnitID(key));
  }

  semantic::SVClassTypeOp owner =
      covergroup->getParentOfType<semantic::SVClassTypeOp>();
  Value classOwner;
  if (owner) {
    if (!thisObject)
      return construct.emitError(
          "embedded clocking covergroup construction has no owning object");
    classOwner = thisObject;
  }

  MLIRContext *context = function.getContext();
  SmallVector<DictionaryAttr> argumentAttrs{
      captureMetadata(builder, sim::CaptureKind::Context),
      captureMetadata(builder, sim::CaptureKind::Formal)};
  if (classOwner)
    argumentAttrs.push_back(captureMetadata(builder, sim::CaptureKind::Formal));
  const unsigned fixedArguments = classOwner ? 3 : 2;
  SmallVector<Attribute> bindings;
  SmallVector<Value> captures;
  llvm::StringSet<> referencedPaths;
  llvm::StringSet<> primaryDependencyPaths;
  covergroup->walk([&](Operation *nested) {
    if (auto path = nested->getAttrOfType<StringAttr>("referenced_path");
        path && !path.getValue().empty())
      referencedPaths.insert(path.getValue());
    if (auto callCaptures =
            nested->getAttrOfType<ArrayAttr>(calleeCapturesAttrName))
      for (Attribute capture : callCaptures)
        referencedPaths.insert(cast<StringAttr>(capture).getValue());
    if (auto observerCaptures =
            nested->getAttrOfType<ArrayAttr>(observerCapturesAttrName))
      for (Attribute capture : observerCaptures)
        referencedPaths.insert(cast<StringAttr>(capture).getValue());
  });
  for (Operation *child : getChildren(covergroup)) {
    SmallVector<Operation *> controls;
    if (isa<semantic::SVSignalEventControlOp>(child))
      controls.push_back(child);
    else if (isa<semantic::SVEventListControlOp>(child))
      llvm::append_range(controls, getChildren(child));
    else
      continue;
    for (Operation *control : controls) {
      SmallVector<Operation *> expressions = getChildren(control);
      if (expressions.empty())
        continue;
      Operation *primary = expressions.front();
      if (auto dependencies =
              primary->getAttrOfType<ArrayAttr>(observerDependenciesAttrName))
        for (Attribute dependency : dependencies)
          primaryDependencyPaths.insert(
              cast<StringAttr>(dependency).getValue());
      primary->walk([&](Operation *nested) {
        if (auto path = nested->getAttrOfType<StringAttr>("referenced_path");
            path && !path.getValue().empty())
          primaryDependencyPaths.insert(path.getValue());
      });
    }
    break;
  }

  DenseMap<unsigned, unsigned> capturedArguments;
  llvm::StringSet<> boundPaths;
  if (classOwner) {
    std::optional<StringRef> path = owner.getThisVariablePath();
    if (!path)
      return construct.emitError(
          "embedded clocking covergroup owner has no this binding");
    bindings.push_back(sim::ArgumentBindingAttr::get(
        context, builder.getStringAttr(*path), 2, sim::UnitArgumentKind::Direct,
        /*copyOut=*/false, IntegerAttr{},
        /*copyIn=*/true));
    boundPaths.insert(*path);
  }
  auto isDynamicReferencePath = [](Value value) {
    while (auto retype = value.getDefiningOp<sim::SimArgumentRefRetypeOp>())
      value = retype.getInput();
    return value.getDefiningOp<sim::SimArgumentRefFromPathOp>() != nullptr;
  };
  for (Operation *child : getChildren(covergroup)) {
    auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child);
    if (!formal || formal.getIsCoverageSampleFormal().value_or(false))
      continue;
    SmallVector<std::string> paths{getHierarchyName(formal).str()};
    for (Operation *candidate : getChildren(covergroup)) {
      auto alias = dyn_cast<semantic::SVTransparentMemberSymbolOp>(candidate);
      if (alias && getDebugName(alias) == getDebugName(formal))
        paths.push_back(getHierarchyName(alias).str());
    }
    for (StringRef path : paths) {
      if (!referencedPaths.contains(path) || !boundPaths.insert(path).second)
        continue;
      Value value = lvalues.lookup(path);
      if (!value)
        value = values.lookup(path);
      if (!value)
        return construct.emitError()
               << "referenced covergroup constructor formal '" << path
               << "' has no construction-time binding";
      if (isDynamicReferencePath(value) &&
          primaryDependencyPaths.contains(path))
        return construct.emitError()
               << "clocking-event covergroup constructor ref formal '" << path
               << "' is bound through a dynamic reference path; dynamic "
                  "reference-path event watches are not executable yet";
      unsigned argument = captures.size() + fixedArguments;
      captures.push_back(value);
      argumentAttrs.push_back(
          captureMetadata(builder, sim::CaptureKind::Formal));
      bindings.push_back(sim::ArgumentBindingAttr::get(
          context, builder.getStringAttr(path), argument,
          sim::UnitArgumentKind::Direct, /*copyOut=*/false, IntegerAttr{},
          /*copyIn=*/true));
    }
  }
  if (ArrayAttr parentBindings =
          function->getAttrOfType<ArrayAttr>(bindingsAttrName))
    for (Attribute binding : parentBindings) {
      StringRef path = sim::getUnitBindingPath(binding);
      if (path.empty() || !referencedPaths.contains(path) ||
          boundPaths.contains(path))
        continue;
      if (isa<sim::DescriptorBindingAttr, sim::ConstantBindingAttr>(binding)) {
        bindings.push_back(binding);
        boundPaths.insert(path);
        continue;
      }
      if (auto argument = dyn_cast<sim::ArgumentBindingAttr>(binding)) {
        if (argument.getKind() != sim::UnitArgumentKind::Direct &&
            argument.getKind() != sim::UnitArgumentKind::LValueOnly)
          continue;
        const unsigned parentArgument = argument.getArgument();
        if (parentArgument >= function.getNumArguments())
          return function.emitError(
              "covergroup event capture references an invalid parent "
              "argument");
        auto [position, inserted] = capturedArguments.try_emplace(
            parentArgument, captures.size() + fixedArguments);
        if (inserted) {
          Value value = function.getArgument(parentArgument);
          captures.push_back(value);
          DictionaryAttr attrs = function.getArgAttrDict(parentArgument);
          if (!attrs)
            attrs = captureMetadata(builder, sim::CaptureKind::Formal);
          if (isa<sim::RefType, sim::ArgumentRefType, sim::NetType,
                  sim::DriverType>(value.getType()) &&
              !isStaticallyAllocatedOverrideTarget(value) &&
              !attrs.contains("simulation.automatic_reference_capture")) {
            SmallVector<NamedAttribute> entries(attrs.begin(), attrs.end());
            entries.push_back(
                builder.getNamedAttr("simulation.automatic_reference_capture",
                                     builder.getUnitAttr()));
            attrs = builder.getDictionaryAttr(entries);
          }
          argumentAttrs.push_back(attrs);
        }
        bindings.push_back(sim::ArgumentBindingAttr::get(
            context, argument.getPath(), position->second, argument.getKind(),
            argument.getCopyOut(), argument.getLvalueNode(),
            argument.getCopyIn()));
        boundPaths.insert(path);
      }
    }

  SmallVector<Attribute> captureAttrs(argumentAttrs.begin(),
                                      argumentAttrs.end());
  if (classOwner)
    captures.insert(captures.begin(), classOwner);
  SmallVector<Attribute> eventObservers;
  for (Operation *child : getChildren(covergroup)) {
    SmallVector<Operation *> controls;
    if (isa<semantic::SVSignalEventControlOp>(child))
      controls.push_back(child);
    else if (isa<semantic::SVEventListControlOp>(child))
      llvm::append_range(controls, getChildren(child));
    else
      continue;
    for (Operation *control : controls) {
      SmallVector<Operation *> expressions = getChildren(control);
      for (Operation *expression : expressions)
        if (auto evaluator = expression->getAttr("simulation.observer"))
          eventObservers.push_back(evaluator);
    }
    break;
  }
  sim::SimCovergroupClockingSpawnOp::create(
      builder, getSemanticLocation(construct), function.getArgument(0), handle,
      captures, builder.getI32IntegerAttr(classOwner ? 0 : -1), samplerNode,
      covergroup.getSymNameAttr(), builder.getArrayAttr(bindings),
      builder.getArrayAttr(captureAttrs), builder.getArrayAttr(eventObservers));
  return success();
}

LogicalResult materializeCovergroupClockingSamplers(
    sim::SimDesignOp design, const UnitLoweringInputs &loweringInputs) {
  SmallVector<sim::SimCovergroupClockingSpawnOp> plans;
  design.walk(
      [&](sim::SimCovergroupClockingSpawnOp plan) { plans.push_back(plan); });
  SmallVector<sim::SimCovergroupBlockEventPlanOp> blockPlans;
  design.walk([&](sim::SimCovergroupBlockEventPlanOp plan) {
    blockPlans.push_back(plan);
  });
  if (plans.empty() && blockPlans.empty())
    return success();

  ModuleOp module = design->getParentOfType<ModuleOp>();
  llvm::StringMap<semantic::SVCovergroupTypeOp> covergroups;
  for (Operation &topLevel : module.getBody()->getOperations())
    if (auto root = dyn_cast<semantic::SVRootSymbolOp>(topLevel))
      root->walk([&](semantic::SVCovergroupTypeOp covergroup) {
        covergroups[covergroup.getSymName()] = covergroup;
      });

  llvm::DenseMap<uint64_t, Operation *> codeUnitIDs;
  for (sim::SimCodeUnitDeclOp declaration :
       design.getBody().front().getOps<sim::SimCodeUnitDeclOp>())
    codeUnitIDs.try_emplace(declaration.getId(), declaration);

  for (sim::SimCovergroupClockingSpawnOp plan : plans) {
    sim::SimFuncOp parent = plan->getParentOfType<sim::SimFuncOp>();
    auto found = covergroups.find(plan.getCovergroupSymbol());
    if (!parent || found == covergroups.end()) {
      plan.emitError("deferred clocking sampler has no semantic covergroup");
      return failure();
    }
    semantic::SVCovergroupTypeOp covergroup = found->second;
    bool strobe = false;
    for (Operation *child : getChildren(covergroup)) {
      auto body = dyn_cast<semantic::SVCovergroupBodySymbolOp>(child);
      if (!body)
        continue;
      for (Operation *member : getChildren(body)) {
        auto option = dyn_cast<semantic::SVCoverageOptionOp>(member);
        if (!option ||
            option.getOwnerKind() !=
                semantic::SVCoverageOptionOwnerKind::Covergroup ||
            option.getScopeKind() !=
                semantic::SVCoverageOptionScopeKind::Type ||
            option.getOptionKind() != semantic::SVCoverageOptionKind::Strobe)
          continue;
        SmallVector<Operation *> expressions = getChildren(option);
        std::optional<StringRef> spelling =
            expressions.size() == 1 ? getConstantSpelling(expressions.front())
                                    : std::nullopt;
        if (!spelling) {
          option.emitError(
              "type_option.strobe must have a known elaboration-time value");
          return failure();
        }
        FailureOr<ParsedConstant> parsed =
            parseSVInteger(*spelling, 1, getSemanticLocation(option));
        if (failed(parsed) || !parsed->unknown.isZero()) {
          option.emitError(
              "type_option.strobe must have a known elaboration-time value");
          return failure();
        }
        strobe = !parsed->value.isZero();
      }
    }
    Operation *control = nullptr;
    for (Operation *child : getChildren(covergroup))
      if (isa<semantic::SVSignalEventControlOp, semantic::SVEventListControlOp>(
              child)) {
        control = child;
        break;
      }
    if (!control) {
      plan.emitError("clocking covergroup has no prepared event control");
      return failure();
    }

    const uint64_t parentID = parent.getCodeUnitId().value_or(0);
    uint64_t scopeID = 0;
    std::string parentHierarchy = parent.getSymName().str();
    for (sim::SimCodeUnitDeclOp declaration :
         design.getBody().front().getOps<sim::SimCodeUnitDeclOp>())
      if (declaration.getId() == parentID) {
        scopeID = declaration.getScopeId();
        parentHierarchy = declaration.getHierarchicalName().str();
        break;
      }
    const uint64_t node = plan.getNodeId();
    std::string identity =
        (parent.getSymName() + ".$covergroup_event." + Twine(node)).str();
    std::string evaluatorIdentity =
        (parent.getSymName() + ".$covergroup_event_sample." + Twine(node))
            .str();
    std::string hierarchy =
        (Twine(parentHierarchy) + ".$covergroup_event." + Twine(node)).str();
    std::string evaluatorHierarchy =
        (Twine(parentHierarchy) + ".$covergroup_event_sample." + Twine(node))
            .str();
    const uint64_t codeUnitID = stableCodeUnitID(identity);
    const uint64_t evaluatorCodeUnitID = stableCodeUnitID(evaluatorIdentity);
    if (design.lookupSymbol<sim::SimFuncOp>(identity) ||
        design.lookupSymbol<sim::SimFuncOp>(evaluatorIdentity)) {
      plan.emitError("duplicate covergroup clocking sampler symbol '")
          << identity << "'";
      return failure();
    }
    if (auto collision = codeUnitIDs.find(codeUnitID);
        collision != codeUnitIDs.end()) {
      plan.emitError("covergroup clocking sampler code-unit ID collision");
      collision->second->emitRemark("colliding code unit is here");
      return failure();
    }
    if (auto collision = codeUnitIDs.find(evaluatorCodeUnitID);
        collision != codeUnitIDs.end() || evaluatorCodeUnitID == codeUnitID) {
      plan.emitError(
          "covergroup event sample evaluator code-unit ID collision");
      if (collision != codeUnitIDs.end())
        collision->second->emitRemark("colliding code unit is here");
      return failure();
    }

    SmallVector<Value> spawnOperands{plan.getContext(), plan.getHandle()};
    llvm::append_range(spawnOperands, plan.getCaptures());
    const int32_t classOwnerCapture = plan.getClassOwnerCapture();
    if (classOwnerCapture < -1 ||
        (classOwnerCapture >= 0 &&
         static_cast<size_t>(classOwnerCapture) >= plan.getCaptures().size())) {
      plan.emitError("clocking sampler class-owner capture is invalid");
      return failure();
    }
    SmallVector<Type> inputs;
    llvm::transform(spawnOperands, std::back_inserter(inputs),
                    [](Value value) { return value.getType(); });
    SmallVector<DictionaryAttr> argumentAttrs;
    for (Attribute attribute : plan.getCaptureAttrs()) {
      auto dictionary = dyn_cast<DictionaryAttr>(attribute);
      if (!dictionary) {
        plan.emitError("clocking sampler capture metadata is not a dictionary");
        return failure();
      }
      argumentAttrs.push_back(dictionary);
    }
    if (argumentAttrs.size() != inputs.size()) {
      plan.emitError("clocking sampler capture metadata count mismatch");
      return failure();
    }
    for (DictionaryAttr attrs : ArrayRef(argumentAttrs).drop_front(2))
      if (attrs.contains("simulation.automatic_reference_capture")) {
        plan.emitError(
            "clocking-event covergroup sample captures automatic state; "
            "this exact event-instant slice requires static captures");
        return failure();
      }

    OpBuilder outlineBuilder(parent);
    SmallVector<NamedAttribute> attributes{
        outlineBuilder.getNamedAttr(bindingsAttrName, plan.getBindings()),
        outlineBuilder.getNamedAttr(
            "code_unit_id", outlineBuilder.getI64IntegerAttr(codeUnitID)),
        outlineBuilder.getNamedAttr("internal", outlineBuilder.getUnitAttr()),
        ::obelisk::schedule::named<
            ::obelisk::schedule::Field::DetachedControls>(
            outlineBuilder.getUnitAttr()),
        ::obelisk::schedule::named<::obelisk::schedule::Field::PrimeOnSpawn>(
            outlineBuilder.getUnitAttr()),
        ::obelisk::schedule::named<
            ::obelisk::schedule::Field::CovergroupClockingSampler>(
            outlineBuilder.getUnitAttr()),
        outlineBuilder.getNamedAttr(sim::metadata::hierarchicalName,
                                    outlineBuilder.getStringAttr(hierarchy))};
    if (classOwnerCapture >= 0)
      attributes.push_back(outlineBuilder.getNamedAttr(
          sim::metadata::thisArgument,
          outlineBuilder.getI32IntegerAttr(2 + classOwnerCapture)));
    for (StringRef name : {StringRef("home_region"), StringRef("domain")})
      if (Attribute attribute = parent->getAttr(name))
        attributes.push_back(outlineBuilder.getNamedAttr(name, attribute));

    SmallVector<NamedAttribute> evaluatorAttributes{
        outlineBuilder.getNamedAttr(bindingsAttrName, plan.getBindings()),
        outlineBuilder.getNamedAttr(
            "code_unit_id",
            outlineBuilder.getI64IntegerAttr(evaluatorCodeUnitID)),
        outlineBuilder.getNamedAttr("internal", outlineBuilder.getUnitAttr()),
        outlineBuilder.getNamedAttr(
            "simulation.covergroup_event_sample_evaluator",
            outlineBuilder.getUnitAttr()),
        outlineBuilder.getNamedAttr(
            sim::metadata::hierarchicalName,
            outlineBuilder.getStringAttr(evaluatorHierarchy)),
        outlineBuilder.getNamedAttr(
            observerResultAttrName,
            outlineBuilder.getI32IntegerAttr(
                static_cast<uint32_t>(ObserverResult::Truth))),
        ::obelisk::schedule::named<::obelisk::schedule::Field::ObserverWidth>(
            outlineBuilder.getI32IntegerAttr(1)),
        ::obelisk::schedule::named<
            ::obelisk::schedule::Field::ObserverFourState>(
            outlineBuilder.getBoolAttr(false))};
    if (classOwnerCapture >= 0)
      evaluatorAttributes.push_back(outlineBuilder.getNamedAttr(
          sim::metadata::thisArgument,
          outlineBuilder.getI32IntegerAttr(2 + classOwnerCapture)));
    if (strobe)
      evaluatorAttributes.push_back(outlineBuilder.getNamedAttr(
          "simulation.covergroup_strobe_sample_evaluator",
          outlineBuilder.getUnitAttr()));
    for (StringRef name : {StringRef("home_region"), StringRef("domain")})
      if (Attribute attribute = parent->getAttr(name))
        evaluatorAttributes.push_back(
            outlineBuilder.getNamedAttr(name, attribute));

    sim::SimFuncOp evaluator = sim::SimFuncOp::create(
        outlineBuilder, plan.getLoc(), evaluatorIdentity,
        FunctionType::get(design.getContext(), inputs,
                          outlineBuilder.getI1Type()),
        sim::EntryKind::Observer, evaluatorAttributes, argumentAttrs);
    SymbolTable::setSymbolVisibility(evaluator,
                                     SymbolTable::Visibility::Private);
    UnitLowering evaluatorLowering(evaluator, loweringInputs);
    Value classOwner = classOwnerCapture >= 0
                           ? evaluator.getArgument(2 + classOwnerCapture)
                           : Value{};
    FailureOr<Value> evaluatorResult = evaluatorLowering.emitCovergroupSample(
        covergroup, evaluator.getArgument(1), classOwner, plan.getLoc());
    if (failed(evaluatorResult)) {
      evaluator.erase();
      return failure();
    }
    Value evaluatorSucceeded =
        arith::ConstantOp::create(evaluatorLowering.builder, plan.getLoc(),
                                  evaluatorLowering.builder.getI1Type(),
                                  evaluatorLowering.builder.getBoolAttr(true));
    sim::SimReturnOp::create(evaluatorLowering.builder, plan.getLoc(),
                             ValueRange{evaluatorSucceeded});
    evaluator->setAttr(sim::metadata::lowered, outlineBuilder.getUnitAttr());

    sim::SimFuncOp sampler = sim::SimFuncOp::create(
        outlineBuilder, plan.getLoc(), identity,
        FunctionType::get(design.getContext(), inputs, TypeRange{}),
        sim::EntryKind::Fork, attributes, argumentAttrs);
    SymbolTable::setSymbolVisibility(sampler, SymbolTable::Visibility::Private);

    UnitLowering nested(sampler, loweringInputs);

    SmallVector<semantic::SVSignalEventControlOp> events;
    if (auto event = dyn_cast<semantic::SVSignalEventControlOp>(control))
      events.push_back(event);
    else
      for (Operation *child : getChildren(control))
        events.push_back(cast<semantic::SVSignalEventControlOp>(child));
    SmallVector<Value> primaries;
    SmallVector<Value> initials;
    SmallVector<Value> conditions;
    SmallVector<int32_t> edges;
    SmallVector<int32_t> conditionIndices;
    for (semantic::SVSignalEventControlOp event : events) {
      SmallVector<Operation *> children = getChildren(event);
      llvm::SetVector<Value> evaluatedDependencies;
      llvm::SetVector<Value> *savedDependencies = nested.observedDependencies;
      nested.observedDependencies = &evaluatedDependencies;
      FailureOr<Value> initial = nested.lowerExpression(children.front());
      nested.observedDependencies = savedDependencies;
      if (succeeded(initial))
        initial = nested.toPackedScalar(*initial,
                                        getSemanticLocation(children.front()));
      SmallVector<Value> dynamicDependencies;
      for (Value dependency : evaluatedDependencies)
        if (isa<sim::EventType, sim::ManagedWatchType>(dependency.getType()))
          dynamicDependencies.push_back(dependency);
      FailureOr<Value> primary =
          nested.bindObserver(children.front(), dynamicDependencies);
      if (failed(initial) || failed(primary)) {
        sampler.erase();
        evaluator.erase();
        return failure();
      }
      primaries.push_back(*primary);
      initials.push_back(*initial);
      edges.push_back(static_cast<int32_t>(event.getEdgeKind()));
      if (!event.getHasIff()) {
        conditionIndices.push_back(-1);
        continue;
      }
      FailureOr<Value> condition = nested.bindObserver(children[1]);
      if (failed(condition)) {
        sampler.erase();
        evaluator.erase();
        return failure();
      }
      conditionIndices.push_back(static_cast<int32_t>(conditions.size()));
      conditions.push_back(*condition);
    }
    SmallVector<Value> observerCaptures(sampler.getArguments().drop_front());
    Value observer = sim::SimObserverBindOp::create(
        nested.builder, plan.getLoc(),
        sim::ObserverType::get(design.getContext(), nested.builder.getI1Type()),
        evaluator.getSymNameAttr(), observerCaptures,
        static_cast<uint32_t>(observerCaptures.size()));
    SmallVector<Value> registrationValues(primaries);
    llvm::append_range(registrationValues, initials);
    llvm::append_range(registrationValues, conditions);
    registrationValues.push_back(observer);
    sim::SimCovergroupClockEventRegisterOp::create(
        nested.builder, plan.getLoc(), sampler.getArgument(0),
        sampler.getArgument(1), registrationValues,
        nested.builder.getI32IntegerAttr(conditions.size()),
        nested.builder.getBoolAttr(strobe),
        nested.builder.getDenseI32ArrayAttr(edges),
        nested.builder.getDenseI32ArrayAttr(conditionIndices));
    Block *parked = nested.addBlock();
    sim::SimSuspendForeverOp::create(
        nested.builder, plan.getLoc(), ValueRange{},
        schedule::ContinuationSiteAttr{}, sim::EventRegionAttr{}, parked);
    nested.setCurrent(parked);
    sim::SimReturnOp::create(nested.builder, plan.getLoc(), ValueRange{});
    sampler->setAttr(sim::metadata::lowered, outlineBuilder.getUnitAttr());

    OpBuilder declarationBuilder(sampler);
    sim::SimCodeUnitDeclOp::create(
        declarationBuilder, plan.getLoc(), evaluatorCodeUnitID, scopeID,
        sim::EntryKind::Observer,
        declarationBuilder.getStringAttr(evaluatorHierarchy),
        declarationBuilder.getStringAttr("covergroup event sample evaluator"),
        declarationBuilder.getUnitAttr());
    sim::SimCodeUnitDeclOp::create(
        declarationBuilder, plan.getLoc(), codeUnitID, scopeID,
        sim::EntryKind::Fork, declarationBuilder.getStringAttr(hierarchy),
        declarationBuilder.getStringAttr("covergroup clocking-event sampler"),
        declarationBuilder.getUnitAttr());
    codeUnitIDs.try_emplace(codeUnitID, sampler);
    codeUnitIDs.try_emplace(evaluatorCodeUnitID, evaluator);

    OpBuilder spawnBuilder(plan);
    sim::SimSpawnOp::create(spawnBuilder, plan.getLoc(),
                            sampler.getSymNameAttr(), spawnOperands,
                            ArrayAttr{}, ArrayAttr{});
    plan.erase();
  }

  for (sim::SimCovergroupBlockEventPlanOp plan : blockPlans) {
    sim::SimFuncOp parent = plan->getParentOfType<sim::SimFuncOp>();
    auto found = covergroups.find(plan.getCovergroupSymbol());
    if (!parent || found == covergroups.end()) {
      plan.emitError("deferred block-event sampler has no semantic covergroup");
      return failure();
    }
    semantic::SVCovergroupTypeOp covergroup = found->second;
    if (covergroup.getCoverageEventKind() !=
        semantic::SVCoverageEventKind::Block) {
      plan.emitError("deferred block-event sampler has the wrong event kind");
      return failure();
    }

    const uint64_t parentID = parent.getCodeUnitId().value_or(0);
    uint64_t scopeID = 0;
    std::string parentHierarchy = parent.getSymName().str();
    for (sim::SimCodeUnitDeclOp declaration :
         design.getBody().front().getOps<sim::SimCodeUnitDeclOp>())
      if (declaration.getId() == parentID) {
        scopeID = declaration.getScopeId();
        parentHierarchy = declaration.getHierarchicalName().str();
        break;
      }
    const uint64_t node = plan.getNodeId();
    std::string identity =
        (parent.getSymName() + ".$covergroup_block_event_sample." + Twine(node))
            .str();
    std::string hierarchy = (Twine(parentHierarchy) +
                             ".$covergroup_block_event_sample." + Twine(node))
                                .str();
    const uint64_t codeUnitID = stableCodeUnitID(identity);
    if (design.lookupSymbol<sim::SimFuncOp>(identity)) {
      plan.emitError("duplicate covergroup block-event evaluator symbol '")
          << identity << "'";
      return failure();
    }
    if (auto collision = codeUnitIDs.find(codeUnitID);
        collision != codeUnitIDs.end()) {
      plan.emitError("covergroup block-event evaluator code-unit ID collision");
      collision->second->emitRemark("colliding code unit is here");
      return failure();
    }

    if (plan.getHasReceiver() && plan.getCaptures().empty()) {
      plan.emitError("block-event sampler receiver capture is missing");
      return failure();
    }
    ValueRange evaluatorCaptures = plan.getCaptures();
    Value receiver;
    if (plan.getHasReceiver()) {
      receiver = evaluatorCaptures.front();
      evaluatorCaptures = evaluatorCaptures.drop_front();
    }
    const int32_t classOwnerCapture = plan.getClassOwnerCapture();
    if (classOwnerCapture < -1 ||
        (classOwnerCapture >= 0 &&
         static_cast<size_t>(classOwnerCapture) >= evaluatorCaptures.size())) {
      plan.emitError("block-event sampler class-owner capture is invalid");
      return failure();
    }
    SmallVector<Value> evaluatorOperands{plan.getContext(), plan.getHandle()};
    llvm::append_range(evaluatorOperands, evaluatorCaptures);
    SmallVector<Type> inputs;
    llvm::transform(evaluatorOperands, std::back_inserter(inputs),
                    [](Value value) { return value.getType(); });
    SmallVector<DictionaryAttr> argumentAttrs;
    for (Attribute attribute : plan.getCaptureAttrs()) {
      auto dictionary = dyn_cast<DictionaryAttr>(attribute);
      if (!dictionary) {
        plan.emitError(
            "block-event sampler capture metadata is not a dictionary");
        return failure();
      }
      argumentAttrs.push_back(dictionary);
    }
    if (argumentAttrs.size() != inputs.size()) {
      plan.emitError("block-event sampler capture metadata count mismatch");
      return failure();
    }

    OpBuilder outlineBuilder(parent);
    SmallVector<NamedAttribute> attributes{
        outlineBuilder.getNamedAttr(bindingsAttrName, plan.getBindings()),
        outlineBuilder.getNamedAttr(
            "code_unit_id", outlineBuilder.getI64IntegerAttr(codeUnitID)),
        outlineBuilder.getNamedAttr("internal", outlineBuilder.getUnitAttr()),
        outlineBuilder.getNamedAttr(
            "simulation.covergroup_block_event_sample_evaluator",
            outlineBuilder.getUnitAttr()),
        outlineBuilder.getNamedAttr(sim::metadata::hierarchicalName,
                                    outlineBuilder.getStringAttr(hierarchy)),
        outlineBuilder.getNamedAttr(
            observerResultAttrName,
            outlineBuilder.getI32IntegerAttr(
                static_cast<uint32_t>(ObserverResult::Truth))),
        ::obelisk::schedule::named<::obelisk::schedule::Field::ObserverWidth>(
            outlineBuilder.getI32IntegerAttr(1)),
        ::obelisk::schedule::named<
            ::obelisk::schedule::Field::ObserverFourState>(
            outlineBuilder.getBoolAttr(false))};
    if (classOwnerCapture >= 0)
      attributes.push_back(outlineBuilder.getNamedAttr(
          sim::metadata::thisArgument,
          outlineBuilder.getI32IntegerAttr(2 + classOwnerCapture)));
    for (StringRef name : {StringRef("home_region"), StringRef("domain")})
      if (Attribute attribute = parent->getAttr(name))
        attributes.push_back(outlineBuilder.getNamedAttr(name, attribute));

    sim::SimFuncOp evaluator = sim::SimFuncOp::create(
        outlineBuilder, plan.getLoc(), identity,
        FunctionType::get(design.getContext(), inputs,
                          outlineBuilder.getI1Type()),
        sim::EntryKind::Observer, attributes, argumentAttrs);
    SymbolTable::setSymbolVisibility(evaluator,
                                     SymbolTable::Visibility::Private);
    UnitLowering evaluatorLowering(evaluator, loweringInputs);
    Value classOwner = classOwnerCapture >= 0
                           ? evaluator.getArgument(2 + classOwnerCapture)
                           : Value{};
    FailureOr<Value> evaluatorResult = evaluatorLowering.emitCovergroupSample(
        covergroup, evaluator.getArgument(1), classOwner, plan.getLoc());
    if (failed(evaluatorResult)) {
      evaluator.erase();
      return failure();
    }
    Value evaluatorSucceeded =
        arith::ConstantOp::create(evaluatorLowering.builder, plan.getLoc(),
                                  evaluatorLowering.builder.getI1Type(),
                                  evaluatorLowering.builder.getBoolAttr(true));
    sim::SimReturnOp::create(evaluatorLowering.builder, plan.getLoc(),
                             ValueRange{evaluatorSucceeded});
    evaluator->setAttr(sim::metadata::lowered, outlineBuilder.getUnitAttr());

    OpBuilder declarationBuilder(evaluator);
    sim::SimCodeUnitDeclOp::create(
        declarationBuilder, plan.getLoc(), codeUnitID, scopeID,
        sim::EntryKind::Observer, declarationBuilder.getStringAttr(hierarchy),
        declarationBuilder.getStringAttr(
            "covergroup block-event sample evaluator"),
        declarationBuilder.getUnitAttr());
    codeUnitIDs.try_emplace(codeUnitID, evaluator);

    OpBuilder registrationBuilder(plan);
    SmallVector<Value> observerCaptures(evaluatorOperands.begin() + 1,
                                        evaluatorOperands.end());
    Value observer = sim::SimObserverBindOp::create(
        registrationBuilder, plan.getLoc(),
        sim::ObserverType::get(design.getContext(),
                               registrationBuilder.getI1Type()),
        evaluator.getSymNameAttr(), observerCaptures,
        static_cast<uint32_t>(observerCaptures.size()));
    sim::SimCovergroupBlockEventRegisterOp::create(
        registrationBuilder, plan.getLoc(), plan.getContext(), plan.getHandle(),
        receiver, observer, plan.getTargetIdsAttr(), plan.getEventKindsAttr());
    plan.erase();
  }
  return success();
}

FailureOr<Value> UnitLowering::lowerCovergroupSample(
    semantic::SVCallExpressionOp op, semantic::SVCovergroupTypeOp covergroup,
    Value handle, Value classOwner, bool dispatchInheritance) {
  Location location = getSemanticLocation(op);
  if (classOwner && dispatchInheritance) {
    SmallVector<semantic::SVCovergroupTypeOp> derivedGroups;
    for (const auto &entry : semanticCovergroups) {
      semantic::SVCovergroupTypeOp candidate = entry.second;
      TypeAttr base =
          candidate->getAttrOfType<TypeAttr>("obelisk.coverage.base_group");
      if (base && base.getValue() == covergroup.getSemanticType())
        derivedGroups.push_back(candidate);
    }
    llvm::sort(derivedGroups, [](auto lhs, auto rhs) {
      return std::tuple(getHierarchyName(lhs), lhs.getSymName()) <
             std::tuple(getHierarchyName(rhs), rhs.getSymName());
    });
    if (!derivedGroups.empty()) {
      Block *resume = addBlock();
      resume->addArgument(builder.getI1Type(), location);
      for (semantic::SVCovergroupTypeOp derived : derivedGroups) {
        auto owner = derived->getParentOfType<semantic::SVClassTypeOp>();
        auto ownerHandle =
            owner ? dyn_cast<semantic::ClassHandleType>(owner.getSemanticType())
                  : semantic::ClassHandleType{};
        FailureOr<Type> derivedType = getNormalizedSemanticType(derived);
        if (!ownerHandle || failed(derivedType)) {
          derived.emitError("inherited covergroup has no executable type");
          return failure();
        }
        FlatSymbolRefAttr target = FlatSymbolRefAttr::get(
            getSimulationClassSymbol(ownerHandle.getClassName()));
        Value matches = sim::SimClassIsInstanceOp::create(builder, location,
                                                          classOwner, target);
        Block *selected = addBlock();
        Block *next = addBlock();
        cf::CondBranchOp::create(builder, location, matches, selected,
                                 ValueRange{}, next, ValueRange{});
        setCurrent(selected);
        FailureOr<Value> adjusted =
            convert(handle, *derivedType, false, location);
        FailureOr<Value> selectedResult =
            succeeded(adjusted) ? lowerCovergroupSample(op, derived, *adjusted,
                                                        classOwner, true)
                                : FailureOr<Value>(failure());
        if (failed(selectedResult))
          return failure();
        cf::BranchOp::create(builder, location, resume,
                             ValueRange{*selectedResult});
        setCurrent(next);
      }
      FailureOr<Value> baseResult =
          lowerCovergroupSample(op, covergroup, handle, classOwner, false);
      if (failed(baseResult))
        return failure();
      cf::BranchOp::create(builder, location, resume, ValueRange{*baseResult});
      setCurrent(resume);
      return resume->getArgument(0);
    }
  }
  SmallVector<Operation *> children = getChildren(op);
  SmallVector<semantic::SVFormalArgumentSymbolOp> formals;
  for (Operation *child : getChildren(covergroup))
    if (auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child);
        formal && formal.getIsCoverageSampleFormal().value_or(false))
      formals.push_back(formal);
  if (children.size() != formals.size() + 1 ||
      op.getArgumentCount() != formals.size()) {
    emitError(location) << "covergroup sample argument inventory is malformed";
    return failure();
  }
  std::optional<ArrayRef<int64_t>> defaulted = op.getDefaultedArguments();
  if (defaulted && (defaulted->size() != formals.size() ||
                    !llvm::all_of(*defaulted, [](int64_t value) {
                      return value == 0 || value == 1;
                    }))) {
    emitError(location)
        << "covergroup sample default-argument inventory is malformed";
    return failure();
  }

  struct SavedFormal {
    std::string path;
    Value value;
    Value lvalue;
  };
  SmallVector<SavedFormal> savedFormals;
  SmallVector<Value> defaultValues;
  SmallVector<int64_t> defaultExpressionIDs;
  SmallVector<uint32_t> defaultResultOrdinals;
  auto bindInheritedFormalAliases =
      [&](semantic::SVFormalArgumentSymbolOp formal, StringRef canonicalPath,
          Value boundValue) {
        for (Operation *child : getChildren(covergroup)) {
          auto alias = dyn_cast<semantic::SVTransparentMemberSymbolOp>(child);
          if (!alias || getDebugName(alias) != getDebugName(formal))
            continue;
          std::string aliasPath = getHierarchyName(alias).str();
          if (aliasPath == canonicalPath)
            continue;
          savedFormals.push_back(
              {aliasPath, values.lookup(aliasPath), lvalues.lookup(aliasPath)});
          values[aliasPath] = boundValue;
          if (Value reference = lvalues.lookup(canonicalPath))
            lvalues[aliasPath] = reference;
        }
      };
  for (auto [index, formal, actual] :
       llvm::enumerate(formals, ArrayRef<Operation *>(children).drop_front())) {
    FailureOr<Type> type = getNormalizedSemanticType(formal);
    if (failed(type))
      return failure();
    std::string path = getHierarchyName(formal).str();
    savedFormals.push_back({path, values.lookup(path), lvalues.lookup(path)});

    // IEEE 1800-2017 13.5.3 evaluates an omitted default in the scope that
    // contains the declaration.  For a covergroup embedded in a class that
    // means the object owning the selected covergroup, not the caller's
    // current `this`.  Explicit actuals continue to use the caller context.
    Value savedThisObject = thisObject;
    if (defaulted && (*defaulted)[index] && classOwner)
      thisObject = classOwner;
    llvm::scope_exit restoreActualThis([&] { thisObject = savedThisObject; });

    Value boundValue;
    if (formal.getDirection() == semantic::SVArgumentDirection::Ref) {
      FailureOr<Value> destination = lowerExpression(actual, true);
      if (failed(destination))
        return failure();
      FailureOr<Value> reference =
          toArgumentReference(*destination, *type, getSemanticLocation(actual));
      if (failed(reference)) {
        emitError(getSemanticLocation(actual))
            << "covergroup sample ref actual type must exactly match its "
               "formal";
        return failure();
      }
      lvalues[path] = *reference;
      boundValue = sim::SimArgumentRefLoadOp::create(
          builder, getSemanticLocation(actual), *type, *reference);
    } else {
      FailureOr<Value> value = lowerExpression(actual);
      FailureOr<Value> converted =
          succeeded(value)
              ? convert(*value, *type, isSignedNode(actual),
                        getSemanticLocation(actual), isSignedNode(formal))
              : FailureOr<Value>(failure());
      if (failed(converted))
        return failure();
      boundValue = *converted;
    }
    values[path] = boundValue;
    bindInheritedFormalAliases(formal, path, boundValue);
    if (defaulted && (*defaulted)[index]) {
      SmallVector<Operation *> defaultChildren = getChildren(formal);
      auto defaultID =
          defaultChildren.size() == 1
              ? defaultChildren.front()->getAttrOfType<IntegerAttr>(
                    sim::metadata::coverageFunctionalExpressionId)
              : IntegerAttr{};
      if (!defaultID || defaultID.getValue().isNegative() ||
          defaultID.getValue().isZero()) {
        formal.emitError("defaulted sample formal has no typed v1 "
                         "FunctionalExpression identity");
        return failure();
      }
      defaultValues.push_back(boundValue);
      defaultExpressionIDs.push_back(
          static_cast<int64_t>(defaultID.getValue().getZExtValue()));
      auto ordinal = defaultChildren.front()->getAttrOfType<IntegerAttr>(
          sim::metadata::coverageFunctionalExpressionOrdinal);
      if (!ordinal || ordinal.getValue().isNegative()) {
        formal.emitError("defaulted sample formal has no typed v1 result "
                         "ordinal");
        return failure();
      }
      defaultResultOrdinals.push_back(
          static_cast<uint32_t>(ordinal.getValue().getZExtValue()));
    }
  }
  llvm::scope_exit restoreFormals([&] {
    for (const SavedFormal &saved : savedFormals) {
      if (saved.value)
        values[saved.path] = saved.value;
      else
        values.erase(saved.path);
      if (saved.lvalue)
        lvalues[saved.path] = saved.lvalue;
      else
        lvalues.erase(saved.path);
    }
  });

  return emitCovergroupSample(covergroup, handle, classOwner, location,
                              defaultValues, defaultExpressionIDs,
                              defaultResultOrdinals);
}

FailureOr<Value>
UnitLowering::emitCovergroupSample(semantic::SVCovergroupTypeOp covergroup,
                                   Value handle, Value classOwner,
                                   Location location, ValueRange defaultValues,
                                   ArrayRef<int64_t> defaultExpressionIDs,
                                   ArrayRef<uint32_t> defaultResultOrdinals) {

  // Sample actuals are evaluated in the caller's context. Only declaration
  // expressions in the embedded covergroup use the object that owns the
  // covergroup property as their implicit `this`.
  Value savedThisObject = thisObject;
  if (classOwner)
    thisObject = classOwner;
  llvm::scope_exit restoreThisObject([&] { thisObject = savedThisObject; });

  Value context = function.getBody().front().getArgument(0);
  Value enabled = sim::SimCovergroupSampleEnabledOp::create(
      builder, location, builder.getI1Type(), context, handle);
  Block *sampleBlock = addBlock();
  Block *doneBlock = addBlock();
  cf::CondBranchOp::create(builder, location, enabled, sampleBlock,
                           ValueRange{}, doneBlock, ValueRange{});
  setCurrent(sampleBlock);

  SmallVector<std::pair<std::string, Value>> savedConstructorFormals;
  for (Operation *child : getChildren(covergroup)) {
    auto formal = dyn_cast<semantic::SVFormalArgumentSymbolOp>(child);
    if (!formal || formal.getIsCoverageSampleFormal().value_or(false))
      continue;
    auto id = formal->getAttrOfType<IntegerAttr>(
        sim::metadata::coverageFunctionalFormalId);
    FailureOr<Type> type = getNormalizedSemanticType(formal);
    if (!id || id.getValue().isNegative() || id.getValue().isZero() ||
        failed(type)) {
      formal.emitError("constructor formal is absent from the embedded typed "
                       "v1 FunctionalFormal plan");
      return failure();
    }
    // String constructor formals are intentionally constructor-only. The
    // preparation verifier rejects their use by sample-time expressions, so
    // no long-lived managed String word is rooted in functional state.
    if (isa<sim::StringType>(*type))
      continue;
    std::string path = getHierarchyName(formal).str();
    savedConstructorFormals.emplace_back(path, values.lookup(path));
    values[path] = sim::SimCovergroupFormalReadOp::create(
        builder, getSemanticLocation(formal), *type, context, handle, id);
  }
  llvm::scope_exit restoreConstructorFormals([&] {
    for (const auto &[path, value] : savedConstructorFormals) {
      if (value)
        values[path] = value;
      else
        values.erase(path);
    }
  });

  // IEEE 1800-2017 19.5 requires the coverpoint expression and its iff
  // condition to be evaluated when the group is sampled. Keep those results
  // typed and evaluate each semantic expression once. The runtime uses the
  // stable FunctionalExpression identities to perform all bin, transition,
  // and cross matching against the handle's resolved configuration.
  SmallVector<Value> sampleValues(defaultValues);
  SmallVector<int64_t> expressionIDs(defaultExpressionIDs);
  llvm::DenseSet<uint64_t> seenExpressionIDs;
  seenExpressionIDs.insert(defaultExpressionIDs.begin(),
                           defaultExpressionIDs.end());
  std::optional<uint32_t> lastSampleResultOrdinal;
  if (!defaultResultOrdinals.empty())
    lastSampleResultOrdinal = defaultResultOrdinals.back();

  auto appendSampleExpression = [&](Operation *expression,
                                    bool predicate) -> LogicalResult {
    auto id = expression->getAttrOfType<IntegerAttr>(
        sim::metadata::coverageFunctionalExpressionId);
    auto ordinal = expression->getAttrOfType<IntegerAttr>(
        sim::metadata::coverageFunctionalExpressionOrdinal);
    if (!id || !ordinal || id.getValue().isNegative() ||
        ordinal.getValue().isNegative()) {
      expression->emitError(
          "sample-phase expression is absent from the embedded typed v1 "
          "FunctionalExpression plan");
      return failure();
    }
    uint64_t stableID = id.getValue().getZExtValue();
    uint64_t resultOrdinal = ordinal.getValue().getZExtValue();
    if (!stableID || (lastSampleResultOrdinal &&
                      resultOrdinal <= *lastSampleResultOrdinal)) {
      expression->emitError(
          "sample-phase expressions are not in typed v1 result-ordinal order");
      return failure();
    }
    lastSampleResultOrdinal = static_cast<uint32_t>(resultOrdinal);
    if (!seenExpressionIDs.insert(stableID).second) {
      expression->emitError("functional expression ID hash collision");
      return failure();
    }

    FailureOr<Value> lowered = lowerExpression(expression);
    if (failed(lowered))
      return failure();
    Value value = *lowered;
    auto kind = expression->getAttrOfType<IntegerAttr>(
        sim::metadata::coverageFunctionalExpressionKind);
    auto bitWidth = expression->getAttrOfType<IntegerAttr>(
        sim::metadata::coverageFunctionalExpressionBitWidth);
    auto signedness = expression->getAttrOfType<IntegerAttr>(
        sim::metadata::coverageFunctionalExpressionSignedness);
    if (!kind || !bitWidth || !signedness) {
      expression->emitError(
          "sample-phase expression has no typed result descriptor");
      return failure();
    }
    auto resultKind = static_cast<coverage::FunctionalExpressionResultKind>(
        kind.getValue().getZExtValue());
    if (predicate ||
        resultKind == coverage::FunctionalExpressionResultKind::Boolean) {
      if (resultKind != coverage::FunctionalExpressionResultKind::Boolean) {
        expression->emitError(
            "sample predicate does not have a Boolean result descriptor");
        return failure();
      }
      FailureOr<Value> condition =
          truthValue(value, getSemanticLocation(expression));
      if (failed(condition))
        return failure();
      value = *condition;
    } else if (resultKind ==
               coverage::FunctionalExpressionResultKind::Integral) {
      FailureOr<Value> scalar =
          toPackedScalar(value, getSemanticLocation(expression));
      if (failed(scalar))
        return failure();
      uint64_t width = bitWidth.getValue().getZExtValue();
      if (!width || width > UINT32_MAX) {
        expression->emitError(
            "sample-phase expression has an invalid integral result width");
        return failure();
      }
      Type targetType =
          isa<sim::LogicType>((*scalar).getType())
              ? Type(sim::LogicType::get(function.getContext(), width))
              : Type(builder.getIntegerType(width));
      FailureOr<Value> converted = convert(
          *scalar, targetType, isSignedNode(expression),
          getSemanticLocation(expression),
          signedness.getValue().getZExtValue() ==
              static_cast<uint32_t>(coverage::CoverageSignedness::Signed));
      if (failed(converted))
        return failure();
      value = *converted;
    } else if (resultKind != coverage::FunctionalExpressionResultKind::Real ||
               !value.getType().isF64()) {
      expression->emitError(
          "sample-phase expression has an unsupported typed result");
      return failure();
    }
    Type type = value.getType();
    if ((!isa<IntegerType>(type) || !cast<IntegerType>(type).isSignless()) &&
        !isa<sim::LogicType>(type) && !type.isF64()) {
      expression->emitError(
          "functional sample expression must lower to a signless integer, "
          "!simulation.logic, or f64 value");
      return failure();
    }

    sampleValues.push_back(value);
    expressionIDs.push_back(static_cast<int64_t>(stableID));
    return success();
  };

  for (Operation *child : getChildren(covergroup)) {
    auto body = dyn_cast<semantic::SVCovergroupBodySymbolOp>(child);
    if (!body)
      continue;
    for (Operation *member : getChildren(body)) {
      if (auto cross = dyn_cast<semantic::SVCoverCrossSymbolOp>(member)) {
        SmallVector<Operation *> members = getChildren(cross);
        if (cross.getHasIff()) {
          if (members.empty()) {
            cross.emitError("cross iff expression inventory is malformed");
            return failure();
          }
          if (failed(appendSampleExpression(members.front(), true)))
            return failure();
        }
        WalkResult binResult =
            cross.walk([&](semantic::SVCoverageBinSymbolOp bin) -> WalkResult {
              FailureOr<CoverageBinChildren> decoded =
                  decodeCoverageBinChildren(bin);
              if (failed(decoded))
                return WalkResult::interrupt();
              if (decoded->iff &&
                  decoded->iff->hasAttr(
                      sim::metadata::coverageFunctionalExpressionId) &&
                  failed(appendSampleExpression(decoded->iff, true)))
                return WalkResult::interrupt();
              return WalkResult::advance();
            });
        if (binResult.wasInterrupted())
          return failure();
        continue;
      }
      auto coverpoint = dyn_cast<semantic::SVCoverpointSymbolOp>(member);
      if (!coverpoint)
        continue;
      SmallVector<Operation *> members = getChildren(coverpoint);
      unsigned expressionCount = coverpoint.getHasIff() ? 2 : 1;
      if (members.size() < expressionCount) {
        coverpoint.emitError("coverpoint expression inventory is malformed");
        return failure();
      }
      if (failed(appendSampleExpression(members.front(), false)))
        return failure();
      if (coverpoint.getHasIff()) {
        if (failed(appendSampleExpression(members[1], true)))
          return failure();
      }

      for (Operation *candidate :
           ArrayRef<Operation *>(members).drop_front(expressionCount)) {
        auto bin = dyn_cast<semantic::SVCoverageBinSymbolOp>(candidate);
        if (!bin)
          continue;
        FailureOr<CoverageBinChildren> decoded = decodeCoverageBinChildren(bin);
        if (failed(decoded))
          return failure();
        if (decoded->iff &&
            decoded->iff->hasAttr(
                sim::metadata::coverageFunctionalExpressionId)) {
          if (failed(appendSampleExpression(decoded->iff, true)))
            return failure();
        }
      }
    }
  }

  sim::SimCovergroupSampleOp::create(
      builder, location, context, handle, sampleValues,
      builder.getDenseI64ArrayAttr(expressionIDs));
  emitBranch(doneBlock);
  setCurrent(doneBlock);
  return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                   builder.getBoolAttr(false))
      .getResult();
}

FailureOr<Value>
UnitLowering::lowerCovergroupCall(semantic::SVCallExpressionOp op,
                                  semantic::SVCovergroupTypeOp covergroup) {
  Location location = getSemanticLocation(op);
  StringRef name = op.getCalleeName();
  SmallVector<Operation *> children = getChildren(op);
  Value context = function.getBody().front().getArgument(0);
  auto voidResult = [&]() -> Value {
    return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                     builder.getBoolAttr(false));
  };

  Operation *functionalItemMethodOwner = nullptr;
  if (op.getReferencedSymbol()) {
    StringRef leaf = op.getReferencedSymbol()->getLeafReference();
    auto findMethodOwner = [&](Operation *item) {
      item->walk([&](Operation *nested) {
        auto symbol =
            nested->getAttrOfType<StringAttr>(SymbolTable::getSymbolAttrName());
        if (symbol && symbol.getValue() == leaf)
          functionalItemMethodOwner = item;
      });
    };
    covergroup->walk(
        [&](semantic::SVCoverpointSymbolOp point) { findMethodOwner(point); });
    covergroup->walk(
        [&](semantic::SVCoverCrossSymbolOp cross) { findMethodOwner(cross); });
    if (functionalItemMethodOwner && name != "get_inst_coverage" &&
        name != "get_coverage" && name != "start" && name != "stop") {
      emitError(location) << "functional coverage item method " << name
                          << " is not supported";
      return failure();
    }
  }

  uint64_t itemID = 0;
  if (functionalItemMethodOwner) {
    auto item = functionalItemMethodOwner->getAttrOfType<IntegerAttr>(
        sim::metadata::coverageFunctionalItemId);
    if (!item || item.getValue().isZero()) {
      functionalItemMethodOwner->emitError(
          "functional coverage item method has no typed v1 FunctionalItem "
          "identity");
      return failure();
    }
    itemID = item.getValue().getZExtValue();
  }

  bool instanceMethod = name == "sample" || name == "start" || name == "stop" ||
                        name == "set_inst_name" || name == "get_inst_coverage";
  Value handle;
  Value classOwner;
  if (instanceMethod) {
    if (children.empty()) {
      emitError(location) << "covergroup instance method has no receiver";
      return failure();
    }
    Operation *receiver = children.front();
    if (functionalItemMethodOwner) {
      auto member = dyn_cast<semantic::SVMemberAccessExpressionOp>(receiver);
      SmallVector<Operation *> memberChildren = getChildren(receiver);
      if (!member || memberChildren.size() != 1) {
        emitError(location)
            << "coverpoint instance method has no covergroup receiver";
        return failure();
      }
      receiver = memberChildren.front();
    }
    bool classMember = static_cast<bool>(
        covergroup->getParentOfType<semantic::SVClassTypeOp>());
    if (classMember && name == "sample") {
      if (auto member =
              dyn_cast<semantic::SVMemberAccessExpressionOp>(receiver)) {
        SmallVector<Operation *> memberChildren = getChildren(member);
        auto field =
            member->getAttrOfType<FlatSymbolRefAttr>("simulation.class_field");
        FailureOr<Type> handleType = getNormalizedSemanticType(member);
        FailureOr<Value> owner = memberChildren.size() == 1
                                     ? lowerExpression(memberChildren.front())
                                     : FailureOr<Value>(failure());
        auto ownerType =
            succeeded(owner)
                ? dyn_cast<sim::ClassHandleType>((*owner).getType())
                : sim::ClassHandleType{};
        if (!field || failed(handleType) || failed(owner) || !ownerType ||
            !isa<sim::CovergroupHandleType>(*handleType)) {
          emitError(location)
              << "class covergroup receiver has no owning object field";
          return failure();
        }
        Type storageType = *handleType;
        if (auto storage = member->getAttrOfType<TypeAttr>(
                "simulation.covergroup_field_storage_type"))
          storageType = storage.getValue();
        Type referenceType = sim::ManagedRefType::get(
            function.getContext(), storageType, ownerType.getClassName());
        Value reference = sim::SimClassFieldRefOp::create(
            builder, getSemanticLocation(member), referenceType, *owner, field);
        Value stored = sim::SimManagedLoadOp::create(
            builder, getSemanticLocation(member), storageType, reference);
        FailureOr<Value> converted =
            convert(stored, *handleType, false, getSemanticLocation(member));
        if (failed(converted))
          return failure();
        handle = *converted;
        classOwner = *owner;
      } else if (isa<semantic::SVNamedValueExpressionOp>(receiver) &&
                 receiver->hasAttr("simulation.class_field") && thisObject) {
        FailureOr<Value> lowered = lowerExpression(receiver);
        if (failed(lowered))
          return failure();
        handle = *lowered;
        classOwner = thisObject;
      } else {
        emitError(location)
            << "class covergroup sample has no owning object expression";
        return failure();
      }
    } else {
      FailureOr<Value> lowered = lowerExpression(receiver);
      if (failed(lowered))
        return failure();
      handle = *lowered;
    }
    if (!handle || !isa<sim::CovergroupHandleType>(handle.getType()))
      return failure();
  }

  if (name == "sample")
    return lowerCovergroupSample(op, covergroup, handle, classOwner);
  if (name == "set_inst_name") {
    if (children.size() != 2 || op.getArgumentCount() != 1) {
      emitError(location)
          << "covergroup set_inst_name() requires exactly one argument";
      return failure();
    }
    FailureOr<Value> instanceName = lowerExpression(children[1]);
    if (failed(instanceName) ||
        !isa<sim::StringType>((*instanceName).getType())) {
      emitError(location)
          << "covergroup set_inst_name() argument must be a string";
      return failure();
    }
    sim::SimCovergroupSetNameOp::create(builder, location, context, handle,
                                        *instanceName);
    return voidResult();
  }
  if (name == "start" || name == "stop") {
    if (children.size() != 1 || op.getArgumentCount() != 0) {
      emitError(location) << "covergroup " << name << "() takes no arguments";
      return failure();
    }
    if (name == "start")
      sim::SimCovergroupStartOp::create(builder, location, context, handle,
                                        builder.getI64IntegerAttr(itemID));
    else
      sim::SimCovergroupStopOp::create(builder, location, context, handle,
                                       builder.getI64IntegerAttr(itemID));
    return voidResult();
  }
  if (name != "get_inst_coverage" && name != "get_coverage") {
    emitError(location) << "unsupported covergroup method " << name;
    return failure();
  }

  std::optional<ArrayRef<int64_t>> defaulted = op.getDefaultedArguments();
  if (!defaulted || defaulted->size() != 2 ||
      !llvm::all_of(*defaulted,
                    [](int64_t value) { return value == 0 || value == 1; })) {
    emitError(location) << "malformed covergroup query argument metadata";
    return failure();
  }
  bool noOutputs =
      llvm::all_of(*defaulted, [](int64_t value) { return value == 1; });
  bool twoOutputs =
      llvm::all_of(*defaulted, [](int64_t value) { return value == 0; });
  if (!noOutputs && !twoOutputs) {
    emitError(location)
        << "coverage queries require either zero or two output arguments";
    return failure();
  }
  bool hasTypeQueryReceiver = false;
  if (name == "get_coverage" && !children.empty()) {
    Operation *candidate = children.front();
    if (functionalItemMethodOwner) {
      auto member = dyn_cast<semantic::SVMemberAccessExpressionOp>(candidate);
      hasTypeQueryReceiver = member && getChildren(member).size() == 1;
      if (hasTypeQueryReceiver)
        candidate = getChildren(member).front();
    } else {
      FailureOr<Type> candidateType = getNormalizedSemanticType(candidate);
      hasTypeQueryReceiver = succeeded(candidateType) &&
                             isa<sim::CovergroupHandleType>(*candidateType);
    }
    if (hasTypeQueryReceiver && !handle) {
      FailureOr<Value> lowered = lowerExpression(candidate);
      if (failed(lowered) ||
          !isa<sim::CovergroupHandleType>((*lowered).getType()))
        return failure();
      handle = *lowered;
    }
  }
  size_t expectedChildren =
      (name == "get_inst_coverage" || hasTypeQueryReceiver ? 1 : 0) + 2;
  if (children.size() != expectedChildren) {
    emitError(location) << "malformed covergroup query argument inventory";
    return failure();
  }

  Value percentage;
  Value covered;
  Value total;
  if (name == "get_inst_coverage") {
    auto query = sim::SimCovergroupInstanceQueryOp::create(
        builder, location,
        TypeRange{builder.getF64Type(), builder.getI32Type(),
                  builder.getI32Type()},
        context, handle, builder.getI64IntegerAttr(itemID));
    percentage = query.getPercentage();
    covered = query.getCovered();
    total = query.getTotal();
  } else {
    auto semanticHandle =
        dyn_cast<semantic::CovergroupHandleType>(covergroup.getSemanticType());
    if (!semanticHandle) {
      emitError(location) << "covergroup query has no declaration handle";
      return failure();
    }
    FlatSymbolRefAttr declaration = FlatSymbolRefAttr::get(
        getSimulationCovergroupSymbol(semanticHandle.getCovergroupName()));
    auto query = sim::SimCovergroupTypeQueryOp::create(
        builder, location,
        TypeRange{builder.getF64Type(), builder.getI32Type(),
                  builder.getI32Type()},
        context, declaration, builder.getI64IntegerAttr(itemID));
    percentage = query.getPercentage();
    covered = query.getCovered();
    total = query.getTotal();
  }

  if (twoOutputs) {
    ArrayRef<Operation *> outputs =
        ArrayRef<Operation *>(children).take_back(2);
    SmallVector<Value, 2> queryOutputs{covered, total};
    for (auto [actual, value] : llvm::zip_equal(outputs, queryOutputs)) {
      Operation *destination = actual;
      if (auto assignment =
              dyn_cast<semantic::SVAssignmentExpressionOp>(actual)) {
        SmallVector<Operation *> assignmentChildren = getChildren(assignment);
        if (assignmentChildren.size() == 2)
          destination = assignmentChildren.front();
      }
      FailureOr<Value> reference = lowerExpression(destination, true);
      if (failed(reference))
        return failure();
      Type destinationType = getReferenceElementType(*reference);
      if (!destinationType) {
        emitError(getSemanticLocation(destination))
            << "coverage query outputs must be variables";
        return failure();
      }
      FailureOr<Value> converted =
          convert(value, destinationType, true,
                  getSemanticLocation(destination), isSignedNode(destination));
      if (failed(converted) ||
          failed(storeReference(*reference, *converted,
                                getSemanticLocation(destination))))
        return failure();
    }
  }
  return percentage;
}

} // namespace obelisk::simlowering
