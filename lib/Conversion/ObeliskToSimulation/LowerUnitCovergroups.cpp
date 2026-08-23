//===- LowerUnitCovergroups.cpp - Lower covergroup semantics -------------===//

#include "LowerUnit.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

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

FailureOr<Value>
UnitLowering::lowerNewCovergroup(semantic::SVNewCovergroupExpressionOp op) {
  Location location = getSemanticLocation(op);
  if (op.getArgumentCount() != 0 || !getChildren(op).empty()) {
    emitError(location) << "covergroup construction requires zero arguments";
    return failure();
  }
  FailureOr<Type> type = getNormalizedSemanticType(op);
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
  Value context = function.getBody().front().getArgument(0);
  return sim::SimCovergroupCreateOp::create(builder, location, handleType,
                                            context, declaration)
      .getResult();
}

FailureOr<Value>
UnitLowering::lowerCovergroupSample(semantic::SVCallExpressionOp op,
                                    semantic::SVCovergroupTypeOp covergroup,
                                    Value handle, Value classOwner) {
  Location location = getSemanticLocation(op);
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

  struct SavedFormal {
    std::string path;
    Value value;
  };
  SmallVector<SavedFormal> savedFormals;
  for (auto [formal, actual] :
       llvm::zip_equal(formals, ArrayRef<Operation *>(children).drop_front())) {
    FailureOr<Value> value = lowerExpression(actual);
    FailureOr<Type> type = getNormalizedSemanticType(formal);
    if (failed(value) || failed(type))
      return failure();
    FailureOr<Value> converted =
        convert(*value, *type, isSignedNode(actual),
                getSemanticLocation(actual), isSignedNode(formal));
    if (failed(converted))
      return failure();
    std::string path = getHierarchyName(formal).str();
    savedFormals.push_back({path, values.lookup(path)});
    values[path] = *converted;
  }
  llvm::scope_exit restoreFormals([&] {
    for (const SavedFormal &saved : savedFormals) {
      if (saved.value)
        values[saved.path] = saved.value;
      else
        values.erase(saved.path);
    }
  });

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

  SmallVector<semantic::SVCoverpointSymbolOp> coverpoints;
  for (Operation *child : getChildren(covergroup))
    if (auto body = dyn_cast<semantic::SVCovergroupBodySymbolOp>(child))
      for (Operation *member : getChildren(body))
        if (auto coverpoint = dyn_cast<semantic::SVCoverpointSymbolOp>(member))
          coverpoints.push_back(coverpoint);

  auto falseValue = [&](Location loc) -> Value {
    return arith::ConstantOp::create(builder, loc, builder.getI1Type(),
                                     builder.getBoolAttr(false));
  };
  auto trueValue = [&](Location loc) -> Value {
    return arith::ConstantOp::create(builder, loc, builder.getI1Type(),
                                     builder.getBoolAttr(true));
  };
  SmallVector<Value> hitDecisions;

  for (semantic::SVCoverpointSymbolOp coverpoint : coverpoints) {
    Location pointLocation = getSemanticLocation(coverpoint);
    SmallVector<Operation *> members = getChildren(coverpoint);
    if (members.empty()) {
      emitError(pointLocation) << "coverpoint has no sampled expression";
      return failure();
    }
    Block *pointDone = nullptr;
    unsigned expressionCount = coverpoint.getHasIff() ? 2 : 1;
    if (members.size() < expressionCount) {
      emitError(pointLocation)
          << "coverpoint expression inventory is malformed";
      return failure();
    }
    SmallVector<semantic::SVCoverageBinSymbolOp> bins;
    for (Operation *member :
         ArrayRef<Operation *>(members).drop_front(expressionCount))
      if (auto bin = dyn_cast<semantic::SVCoverageBinSymbolOp>(member))
        bins.push_back(bin);
    SmallVector<semantic::SVCoverageBinSymbolOp> contributingBins;
    for (semantic::SVCoverageBinSymbolOp bin : bins)
      if (bin->hasAttr(coverageContributingAttrName))
        contributingBins.push_back(bin);
    if (coverpoint.getHasIff()) {
      FailureOr<Value> iff = lowerExpression(members[1]);
      FailureOr<Value> condition =
          succeeded(iff) ? truthValue(*iff, getSemanticLocation(members[1]))
                         : FailureOr<Value>(failure());
      if (failed(condition))
        return failure();
      Block *pointBody = addBlock();
      pointDone = addBlock();
      SmallVector<Value> skipped;
      skipped.reserve(contributingBins.size());
      for (size_t index = 0; index < contributingBins.size(); ++index) {
        pointDone->addArgument(builder.getI1Type(), pointLocation);
        skipped.push_back(falseValue(pointLocation));
      }
      cf::CondBranchOp::create(builder, getSemanticLocation(members[1]),
                               *condition, pointBody, ValueRange{}, pointDone,
                               skipped);
      setCurrent(pointBody);
    }

    FailureOr<Value> sampledValue = lowerExpression(members.front());
    FailureOr<Value> sampledScalar =
        succeeded(sampledValue)
            ? toPackedScalar(*sampledValue,
                             getSemanticLocation(members.front()))
            : FailureOr<Value>(failure());
    if (failed(sampledScalar))
      return failure();
    Value selector = *sampledScalar;
    bool logic = isa<sim::LogicType>(selector.getType());
    auto integerType = dyn_cast<IntegerType>(selector.getType());
    if (!logic && !integerType) {
      emitError(pointLocation)
          << "coverpoint expression must be a two-state or four-state integral";
      return failure();
    }

    unsigned selectorWidth = logic ? cast<sim::LogicType>(selector.getType())
                                         .getWidth()
                                   : integerType.getWidth();
    Type planeType = builder.getIntegerType(selectorWidth);
    auto constant = [&](Location candidateLocation,
                        const APInt &bits) -> Value {
      if (!logic)
        return arith::ConstantOp::create(
                   builder, candidateLocation, integerType,
                   builder.getIntegerAttr(integerType, bits))
            .getResult();
      return sim::SimLogicConstantOp::create(
                 builder, candidateLocation, selector.getType(),
                 builder.getIntegerAttr(planeType, bits),
                 builder.getIntegerAttr(planeType, 0))
          .getResult();
    };
    auto compare = [&](Location candidateLocation, const APInt &bits,
                       sim::CompareKind logicKind,
                       arith::CmpIPredicate integerKind) -> Value {
      Value candidate = constant(candidateLocation, bits);
      if (logic) {
        Value result = sim::SimLogicCompareOp::create(
            builder, candidateLocation,
            sim::LogicType::get(function.getContext(), 1), logicKind, selector,
            candidate);
        return sim::SimLogicIsTrueOp::create(builder, candidateLocation,
                                             builder.getI1Type(), result)
            .getResult();
      }
      return arith::CmpIOp::create(builder, candidateLocation, integerKind,
                                   selector, candidate)
          .getResult();
    };

    auto matchBin = [&](semantic::SVCoverageBinSymbolOp bin)
        -> FailureOr<Value> {
      Location binLocation = getSemanticLocation(bin);
      auto encoded = bin->getAttrOfType<ArrayAttr>(
          coverageResolvedIntervalsAttrName);
      if (!encoded || encoded.size() % 2 != 0) {
        emitError(binLocation)
            << "coverage bin has no resolved state-value inventory";
        return failure();
      }
      bool isSigned = isSignedNode(members.front());
      Value binMatched = falseValue(binLocation);
      for (size_t index = 0; index < encoded.size(); index += 2) {
        auto lower = dyn_cast<IntegerAttr>(encoded[index]);
        auto upper = dyn_cast<IntegerAttr>(encoded[index + 1]);
        if (!lower || !upper || lower.getType() != planeType ||
            upper.getType() != planeType) {
          emitError(binLocation)
              << "coverage bin has malformed resolved state-value endpoints";
          return failure();
        }
        Value matched;
        if (lower.getValue() == upper.getValue()) {
          matched = compare(binLocation, lower.getValue(),
                            sim::CompareKind::Eq,
                            arith::CmpIPredicate::eq);
        } else {
          Value lowerMatched = compare(
              binLocation, lower.getValue(),
              isSigned ? sim::CompareKind::SGE : sim::CompareKind::UGE,
              isSigned ? arith::CmpIPredicate::sge
                       : arith::CmpIPredicate::uge);
          Value upperMatched = compare(
              binLocation, upper.getValue(),
              isSigned ? sim::CompareKind::SLE : sim::CompareKind::ULE,
              isSigned ? arith::CmpIPredicate::sle
                       : arith::CmpIPredicate::ule);
          matched = arith::AndIOp::create(builder, binLocation, lowerMatched,
                                          upperMatched);
        }
        binMatched = arith::OrIOp::create(builder, binLocation, binMatched,
                                          matched);
      }
      return binMatched;
    };

    SmallVector<Value> ordinaryMatches;
    Value explicitlyDefinedMatched = falseValue(pointLocation);
    Value ignoredMatched = falseValue(pointLocation);
    Value illegalMatched = falseValue(pointLocation);
    bool hasIllegalBins = false;
    bool hasIllegalDefault = false;
    for (semantic::SVCoverageBinSymbolOp bin : bins) {
      // Ordinary default bins collect data but IEEE 1800-2017 19.5 excludes
      // them from coverage. Coverpoint-bin introspection is not in this
      // executable slice, so no runtime state is needed for that count.
      if (bin.getIsDefault()) {
        if (bin.getBinsKind() == semantic::SVCoverageBinKind::IllegalBins) {
          hasIllegalBins = true;
          hasIllegalDefault = true;
        }
        continue;
      }
      FailureOr<Value> binMatched = matchBin(bin);
      if (failed(binMatched))
        return failure();
      explicitlyDefinedMatched = arith::OrIOp::create(
          builder, getSemanticLocation(bin), explicitlyDefinedMatched,
          *binMatched);
      switch (bin.getBinsKind()) {
      case semantic::SVCoverageBinKind::Bins:
        if (bin->hasAttr(coverageContributingAttrName))
          ordinaryMatches.push_back(*binMatched);
        break;
      case semantic::SVCoverageBinKind::IgnoreBins:
        ignoredMatched = arith::OrIOp::create(builder, getSemanticLocation(bin),
                                              ignoredMatched, *binMatched);
        break;
      case semantic::SVCoverageBinKind::IllegalBins:
        hasIllegalBins = true;
        illegalMatched = arith::OrIOp::create(builder, getSemanticLocation(bin),
                                              illegalMatched, *binMatched);
        break;
      }
    }
    if (hasIllegalDefault) {
      Value known = trueValue(pointLocation);
      if (logic) {
        Type bitsType = builder.getIntegerType(selectorWidth);
        Value bits = sim::SimLogicToBitsOp::create(builder, pointLocation,
                                                   bitsType, selector);
        Value roundTrip = sim::SimLogicFromBitsOp::create(
            builder, pointLocation, selector.getType(), bits);
        known = sim::SimLogicCompareOp::create(
            builder, pointLocation, builder.getI1Type(),
            sim::CompareKind::CaseEq, selector, roundTrip);
      }
      Value noExplicitMatch = arith::XOrIOp::create(
          builder, pointLocation, explicitlyDefinedMatched,
          trueValue(pointLocation));
      Value defaultMatched = arith::AndIOp::create(
          builder, pointLocation, known, noExplicitMatch);
      illegalMatched = arith::OrIOp::create(
          builder, pointLocation, illegalMatched, defaultMatched);
    }

    // Sections 19.5.5 and 19.5.6 remove ignored and illegal values from every
    // ordinary bin after distribution. Illegal bins take precedence and also
    // issue a nonterminating runtime error when sampled.
    Value excludedMatched = arith::OrIOp::create(
        builder, pointLocation, ignoredMatched, illegalMatched);
    Value allowed = arith::XOrIOp::create(
        builder, pointLocation, excludedMatched, trueValue(pointLocation));
    SmallVector<Value> pointDecisions;
    pointDecisions.reserve(ordinaryMatches.size());
    for (Value matched : ordinaryMatches)
      pointDecisions.push_back(
          arith::AndIOp::create(builder, pointLocation, matched, allowed));

    if (hasIllegalBins) {
      Block *illegalBlock = addBlock();
      Block *afterIllegal = addBlock();
      cf::CondBranchOp::create(builder, pointLocation, illegalMatched,
                               illegalBlock, ValueRange{}, afterIllegal,
                               ValueRange{});
      setCurrent(illegalBlock);
      sim::SimErrorOp::create(builder, pointLocation, context);
      emitBranch(afterIllegal);
      setCurrent(afterIllegal);
    }

    if (pointDone) {
      cf::BranchOp::create(builder, pointLocation, pointDone, pointDecisions);
      setCurrent(pointDone);
      llvm::append_range(hitDecisions, pointDone->getArguments());
    } else {
      llvm::append_range(hitDecisions, pointDecisions);
    }
  }

  sim::SimCovergroupSampleOp::create(builder, location, context, handle,
                                     hitDecisions);
  emitBranch(doneBlock);
  setCurrent(doneBlock);
  return falseValue(location);
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

  if (op.getReferencedSymbol()) {
    StringRef leaf = op.getReferencedSymbol()->getLeafReference();
    bool coverpointMethod = false;
    covergroup->walk([&](semantic::SVCoverpointSymbolOp point) {
      point->walk([&](Operation *nested) {
        auto symbol =
            nested->getAttrOfType<StringAttr>(SymbolTable::getSymbolAttrName());
        if (symbol && symbol.getValue() == leaf)
          coverpointMethod = true;
      });
    });
    if (coverpointMethod) {
      emitError(location) << "coverpoint methods are not supported";
      return failure();
    }
  }

  bool instanceMethod = name == "sample" || name == "start" || name == "stop" ||
                        name == "get_inst_coverage";
  Value handle;
  Value classOwner;
  if (instanceMethod) {
    if (children.empty()) {
      emitError(location) << "covergroup instance method has no receiver";
      return failure();
    }
    Operation *receiver = children.front();
    bool classMember = static_cast<bool>(
        covergroup->getParentOfType<semantic::SVClassTypeOp>());
    if (classMember && name == "sample") {
      if (auto member =
              dyn_cast<semantic::SVMemberAccessExpressionOp>(receiver)) {
        SmallVector<Operation *> memberChildren = getChildren(member);
        auto field =
            member->getAttrOfType<FlatSymbolRefAttr>("obelisk_sim.class_field");
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
        Type referenceType = sim::ManagedRefType::get(
            function.getContext(), *handleType, ownerType.getClassName());
        Value reference = sim::SimClassFieldRefOp::create(
            builder, getSemanticLocation(member), referenceType, *owner, field);
        handle = sim::SimManagedLoadOp::create(
            builder, getSemanticLocation(member), *handleType, reference);
        classOwner = *owner;
      } else if (isa<semantic::SVNamedValueExpressionOp>(receiver) &&
                 receiver->hasAttr("obelisk_sim.class_field") && thisObject) {
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
  if (name == "start" || name == "stop") {
    if (children.size() != 1 || op.getArgumentCount() != 0) {
      emitError(location) << "covergroup " << name << "() takes no arguments";
      return failure();
    }
    if (name == "start")
      sim::SimCovergroupStartOp::create(builder, location, context, handle);
    else
      sim::SimCovergroupStopOp::create(builder, location, context, handle);
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
  size_t expectedChildren = (name == "get_inst_coverage" ? 1 : 0) + 2;
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
        context, handle);
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
        context, declaration);
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
