//===- LowerUnitCalls.cpp - Lower function and class calls ------------===//

#include "LowerUnit.h"

#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Solver/ConstraintSolver.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringSet.h"

#include <functional>
#include <limits>
#include <numeric>
#include <optional>

using namespace mlir;

namespace obelisk::simlowering {
namespace {

bool isWeakReferenceCall(semantic::SVCallExpressionOp op) {
  auto path = op->getAttrOfType<StringAttr>("referenced_path");
  return path && path.getValue().starts_with("std::weak_reference#(");
}

bool isMailboxCall(semantic::SVCallExpressionOp op) {
  auto path = op->getAttrOfType<StringAttr>("referenced_path");
  return path && path.getValue().starts_with("std::mailbox#(");
}

} // namespace

FailureOr<Value> UnitLowering::lowerCall(semantic::SVCallExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  std::optional<StringRef> referencedPath = op.getReferencedPath();
  if (referencedPath && referencedPath->starts_with("std::mailbox#(")) {
    StringRef method = op.getCalleeName();
    if (children.empty()) {
      emitError(location) << "mailbox method has no receiver";
      return failure();
    }
    FailureOr<Value> receiver = lowerExpression(children.front());
    auto mailbox = succeeded(receiver)
                       ? dyn_cast<sim::MailboxType>((*receiver).getType())
                       : sim::MailboxType{};
    if (failed(receiver) || !mailbox)
      return failure();
    if (method == "num") {
      if (children.size() != 1) {
        emitError(location) << "mailbox::num accepts no arguments";
        return failure();
      }
      Value count = sim::SimMailboxNumOp::create(builder, location, *receiver);
      FailureOr<Type> resultType = getNormalizedSemanticType(op);
      return failed(resultType)
                 ? FailureOr<Value>(failure())
                 : convert(count, *resultType, true, location, true);
    }
    if (method == "try_put") {
      if (children.size() != 2) {
        emitError(location) << "mailbox::try_put requires one message";
        return failure();
      }
      FailureOr<Value> value = lowerExpression(children[1]);
      value = succeeded(value) ? convert(*value, mailbox.getElementType(),
                                         isSignedNode(children[1]), location)
                               : FailureOr<Value>(failure());
      if (failed(value))
        return failure();
      Value success = sim::SimMailboxTryPutOp::create(
          builder, location, *receiver, cloneSequentialValue(*value, location));
      FailureOr<Type> resultType = getNormalizedSemanticType(op);
      return failed(resultType)
                 ? FailureOr<Value>(failure())
                 : convert(success, *resultType, false, location, true);
    }
    if (method == "put") {
      if (children.size() != 2) {
        emitError(location) << "mailbox::put requires one message";
        return failure();
      }
      FailureOr<Value> value = lowerExpression(children[1]);
      value = succeeded(value) ? convert(*value, mailbox.getElementType(),
                                         isSignedNode(children[1]), location)
                               : FailureOr<Value>(failure());
      if (failed(value))
        return failure();
      Value held = cloneSequentialValue(*value, location);
      Block *retry = addBlock();
      retry->addArgument(mailbox, location);
      retry->addArgument(held.getType(), location);
      Block *wait = addBlock();
      wait->addArgument(mailbox, location);
      wait->addArgument(held.getType(), location);
      Block *done = addBlock();
      cf::BranchOp::create(builder, location, retry,
                           ValueRange{*receiver, held});
      setCurrent(retry);
      Value success = sim::SimMailboxTryPutOp::create(
          builder, location, retry->getArgument(0), retry->getArgument(1));
      cf::CondBranchOp::create(builder, location, success, done, ValueRange{},
                               wait, retry->getArguments());
      setCurrent(wait);
      sim::SimSuspendMailboxOp::create(
          builder, location, wait->getArgument(0),
          sim::MailboxWaitKind::NotFull, wait->getArguments(),
          sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, retry);
      setCurrent(done);
      return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                       builder.getBoolAttr(false))
          .getResult();
    }
    if (method == "try_peek" || method == "try_get") {
      if (children.size() != 2) {
        emitError(location)
            << "mailbox::" << method << " requires one output argument";
        return failure();
      }
      FailureOr<Value> destination = lowerExpression(children[1], true);
      if (failed(destination))
        return failure();
      Type destinationType = getReferenceElementType(*destination);
      if (!destinationType) {
        emitError(location)
            << "mailbox::" << method << " output is not assignable";
        return failure();
      }
      Value success;
      Value value;
      if (method == "try_peek") {
        auto read = sim::SimMailboxTryPeekOp::create(
            builder, location,
            TypeRange{builder.getI1Type(), mailbox.getElementType()},
            *receiver);
        success = read.getSuccess();
        value = read.getValue();
      } else {
        auto read = sim::SimMailboxTryGetOp::create(
            builder, location,
            TypeRange{builder.getI1Type(), mailbox.getElementType()},
            *receiver);
        success = read.getSuccess();
        value = read.getValue();
      }
      Block *store = addBlock();
      Block *resume = addBlock();
      cf::CondBranchOp::create(builder, location, success, store, ValueRange{},
                               resume, ValueRange{});
      setCurrent(store);
      FailureOr<Value> converted =
          convert(cloneSequentialValue(value, location), destinationType, false,
                  location, isSignedNode(children[1]));
      if (failed(converted) ||
          failed(storeReference(*destination, *converted, location)))
        return failure();
      cf::BranchOp::create(builder, location, resume);
      setCurrent(resume);
      FailureOr<Type> resultType = getNormalizedSemanticType(op);
      return failed(resultType)
                 ? FailureOr<Value>(failure())
                 : convert(success, *resultType, false, location, true);
    }
    if (method == "peek" || method == "get") {
      if (children.size() != 2) {
        emitError(location)
            << "mailbox::" << method << " requires one output argument";
        return failure();
      }
      FailureOr<Value> destination = lowerExpression(children[1], true);
      if (failed(destination))
        return failure();
      Type destinationType = getReferenceElementType(*destination);
      if (!destinationType) {
        emitError(location)
            << "mailbox::" << method << " output is not assignable";
        return failure();
      }
      Block *retry = addBlock();
      retry->addArgument(mailbox, location);
      retry->addArgument((*destination).getType(), location);
      Block *wait = addBlock();
      wait->addArgument(mailbox, location);
      wait->addArgument((*destination).getType(), location);
      Block *store = addBlock();
      store->addArgument(mailbox.getElementType(), location);
      store->addArgument((*destination).getType(), location);
      Block *done = addBlock();
      cf::BranchOp::create(builder, location, retry,
                           ValueRange{*receiver, *destination});
      setCurrent(retry);
      Value success;
      Value value;
      if (method == "peek") {
        auto read = sim::SimMailboxTryPeekOp::create(
            builder, location,
            TypeRange{builder.getI1Type(), mailbox.getElementType()},
            retry->getArgument(0));
        success = read.getSuccess();
        value = read.getValue();
      } else {
        auto read = sim::SimMailboxTryGetOp::create(
            builder, location,
            TypeRange{builder.getI1Type(), mailbox.getElementType()},
            retry->getArgument(0));
        success = read.getSuccess();
        value = read.getValue();
      }
      cf::CondBranchOp::create(builder, location, success, store,
                               ValueRange{value, retry->getArgument(1)}, wait,
                               retry->getArguments());
      setCurrent(wait);
      sim::SimSuspendMailboxOp::create(
          builder, location, wait->getArgument(0),
          sim::MailboxWaitKind::NotEmpty, wait->getArguments(),
          sim::ContinuationSiteAttr{}, sim::EventRegionAttr{}, retry);
      setCurrent(store);
      FailureOr<Value> converted =
          convert(cloneSequentialValue(store->getArgument(0), location),
                  destinationType, false, location, isSignedNode(children[1]));
      if (failed(converted) ||
          failed(storeReference(store->getArgument(1), *converted, location)))
        return failure();
      cf::BranchOp::create(builder, location, done);
      setCurrent(done);
      return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                       builder.getBoolAttr(false))
          .getResult();
    }
    unsupported(op);
    return failure();
  }
  if (referencedPath && referencedPath->starts_with("std::process::")) {
    StringRef method =
        referencedPath->drop_front(StringRef("std::process::").size());
    auto dummyResult = [&]() -> Value {
      return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                       builder.getBoolAttr(false));
    };
    if (method == "self") {
      if (!children.empty()) {
        emitError(location) << "process::self requires no arguments";
        return failure();
      }
      return sim::SimProcessCurrentOp::create(builder, location).getResult();
    }
    if (method == "status") {
      if (children.size() != 1) {
        emitError(location) << "process::status requires one receiver";
        return failure();
      }
      FailureOr<Value> receiver = lowerExpression(children.front());
      FailureOr<Type> resultType = getNormalizedSemanticType(op);
      if (failed(receiver) || failed(resultType) ||
          !isa<sim::ProcessType>((*receiver).getType()))
        return failure();
      Value status = sim::SimProcessStatusOp::create(
          builder, location, builder.getI32Type(), *receiver);
      return convert(status, *resultType, true, location, true);
    }
    if (method == "get_randstate" || method == "set_randstate" ||
        method == "srandom") {
      size_t expectedChildren = method == "get_randstate" ? 1 : 2;
      if (children.size() != expectedChildren) {
        emitError(location)
            << "process::" << method << " has malformed method arguments";
        return failure();
      }
      FailureOr<Value> receiver = lowerExpression(children.front());
      if (failed(receiver) || !isa<sim::ProcessType>((*receiver).getType())) {
        emitError(location)
            << "process::" << method << " receiver is not a process object";
        return failure();
      }
      Type i64 = builder.getI64Type();
      Type stringType = sim::StringType::get(function.getContext());
      if (method == "get_randstate") {
        auto state =
            sim::SimProcessRandomStateOp::create(builder, location, *receiver);
        Value stateText = sim::SimStringFormatIntegerOp::create(
            builder, location, stringType, state.getState(),
            builder.getI32IntegerAttr(16), builder.getBoolAttr(false));
        Value separator = sim::SimStringLiteralOp::create(
            builder, location, stringType, builder.getStringAttr(":"));
        Value incrementText = sim::SimStringFormatIntegerOp::create(
            builder, location, stringType, state.getIncrement(),
            builder.getI32IntegerAttr(16), builder.getBoolAttr(false));
        return sim::SimStringConcatOp::create(
                   builder, location, stringType,
                   ValueRange{stateText, separator, incrementText})
            .getResult();
      }
      FailureOr<Value> argument = lowerExpression(children[1]);
      if (failed(argument))
        return failure();
      if (method == "srandom") {
        argument = convert(*argument, builder.getI32Type(),
                           isSignedNode(children[1]), location);
        if (failed(argument))
          return failure();
        Value seed = arith::ExtUIOp::create(builder, location, i64, *argument);
        Value increment = arith::ConstantOp::create(
            builder, location, i64,
            builder.getIntegerAttr(
                i64, APInt(64, OBELISK_RT_RANDOM_DEFAULT_INCREMENT)));
        Value multiplier = arith::ConstantOp::create(
            builder, location, i64,
            builder.getIntegerAttr(i64,
                                   APInt(64, OBELISK_RT_RANDOM_MULTIPLIER)));
        Value state = arith::AddIOp::create(builder, location, increment, seed);
        state = arith::MulIOp::create(builder, location, state, multiplier);
        state = arith::AddIOp::create(builder, location, state, increment);
        sim::SimProcessSetRandomStateOp::create(builder, location, *receiver,
                                                state, increment);
        return dummyResult();
      }
      if (!isa<sim::StringType>((*argument).getType())) {
        emitError(location)
            << "process::set_randstate argument is not a string";
        return failure();
      }
      auto old =
          sim::SimProcessRandomStateOp::create(builder, location, *receiver);
      Type i32 = builder.getI32Type();
      Value cursor = arith::ConstantOp::create(builder, location, i32,
                                               builder.getI32IntegerAttr(0));
      auto stateField = sim::SimStringScanFieldOp::create(
          builder, location, TypeRange{stringType, i32, i32}, *argument, cursor,
          builder.getStringAttr(""), static_cast<uint32_t>('x'));
      auto incrementField = sim::SimStringScanFieldOp::create(
          builder, location, TypeRange{stringType, i32, i32}, *argument,
          stateField.getNextCursor(), builder.getStringAttr(":"),
          static_cast<uint32_t>('x'));
      Value parsedState = sim::SimStringParseIntegerOp::create(
          builder, location, i64, stateField.getField(),
          builder.getI32IntegerAttr(16));
      Value parsedIncrement = sim::SimStringParseIntegerOp::create(
          builder, location, i64, incrementField.getField(),
          builder.getI32IntegerAttr(16));
      Value zero = arith::ConstantOp::create(builder, location, i32,
                                             builder.getI32IntegerAttr(0));
      Value stateMatched =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                stateField.getOk(), zero);
      Value incrementMatched =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                incrementField.getOk(), zero);
      Value matched = arith::AndIOp::create(builder, location, stateMatched,
                                            incrementMatched);
      Value state = arith::SelectOp::create(builder, location, matched,
                                            parsedState, old.getState());
      Value increment = arith::SelectOp::create(
          builder, location, matched, parsedIncrement, old.getIncrement());
      sim::SimProcessSetRandomStateOp::create(builder, location, *receiver,
                                              state, increment);
      return dummyResult();
    }
    if (method == "await") {
      if (children.size() != 1) {
        emitError(location) << "process::await requires one receiver";
        return failure();
      }
      FailureOr<Value> receiver = lowerExpression(children.front());
      if (failed(receiver) || !isa<sim::ProcessType>((*receiver).getType()))
        return failure();
      Block *continuation = addBlock();
      sim::SimSuspendAwaitOp::create(builder, location, *receiver, ValueRange{},
                                     sim::ContinuationSiteAttr{},
                                     sim::EventRegionAttr{}, continuation);
      setCurrent(continuation);
      return dummyResult();
    }
    std::optional<sim::ProcessControlKind> kind =
        llvm::StringSwitch<std::optional<sim::ProcessControlKind>>(method)
            .Case("kill", sim::ProcessControlKind::Kill)
            .Case("suspend", sim::ProcessControlKind::Suspend)
            .Case("resume", sim::ProcessControlKind::Resume)
            .Default(std::nullopt);
    if (kind) {
      if (children.size() != 1) {
        emitError(location)
            << "process::" << method << " requires one receiver";
        return failure();
      }
      FailureOr<Value> receiver = lowerExpression(children.front());
      if (failed(receiver) || !isa<sim::ProcessType>((*receiver).getType()))
        return failure();
      Block *continuation = addBlock();
      sim::SimProcessControlOp::create(
          builder, location, *kind, *receiver, ValueRange{},
          sim::ContinuationSiteAttr{}, continuation);
      setCurrent(continuation);
      return dummyResult();
    }
  }
  if (op->hasAttr(randomizeAttrName) || op->hasAttr(randomizeDispatchAttrName))
    return lowerRandomize(op);
  StringRef covergroupMethod = op.getCalleeName();
  if ((covergroupMethod == "sample" || covergroupMethod == "start" ||
       covergroupMethod == "stop" || covergroupMethod == "get_inst_coverage" ||
       covergroupMethod == "get_coverage"))
    if (auto covergroup = findSemanticCovergroup(op))
      return lowerCovergroupCall(op, covergroup);
  if (op.getIsSystemCall() && op.getCalleeName() == "index" &&
      children.size() == 1) {
    auto path = children.front()->getAttrOfType<StringAttr>("referenced_path");
    if (path) {
      auto found = iteratorIndices.find(path.getValue());
      if (found != iteratorIndices.end()) {
        FailureOr<Type> resultType = getNormalizedSemanticType(op);
        if (failed(resultType))
          return failure();
        return convert(found->second, *resultType, true, location, true);
      }
    }
  }
  bool stringBuiltin = false;
  bool containerBuiltin = false;
  bool associativeBuiltin = false;
  if (op.getIsSystemCall() && !op.getCalleeName().starts_with("$") &&
      !children.empty() && !op->hasAttr(randomModeAttrName) &&
      !op->hasAttr(constraintModeAttrName)) {
    Operation *receiverNode =
        op.getHasIteratorExpression() ? children.back() : children.front();
    FailureOr<Type> receiverType = getNormalizedSemanticType(receiverNode);
    stringBuiltin =
        succeeded(receiverType) && isa<sim::StringType>(*receiverType);
    containerBuiltin =
        succeeded(receiverType) &&
        isa<sim::DynamicArrayType, sim::QueueType>(*receiverType);
    associativeBuiltin =
        succeeded(receiverType) && isa<sim::AssocArrayType>(*receiverType);
  }
  if (op->hasAttr(randomModeAttrName) ||
      (op.getIsSystemCall() && op.getCalleeName() == "rand_mode")) {
    if (children.empty() || children.size() > 2) {
      emitError(location) << "rand_mode expects a receiver and optional on/off "
                             "argument";
      return failure();
    }
    auto propertyIndexAttr =
        op->getAttrOfType<IntegerAttr>(randomModePropertyAttrName);
    auto staticStorageAttr =
        op->getAttrOfType<IntegerAttr>(randomModeStaticStorageAttrName);
    auto staticDispatchAttr =
        op->getAttrOfType<ArrayAttr>(randomModeStaticDispatchAttrName);
    if ((propertyIndexAttr && staticStorageAttr) ||
        ((propertyIndexAttr || staticStorageAttr) && staticDispatchAttr)) {
      emitError(location) << "rand_mode metadata is malformed";
      return failure();
    }
    if (!propertyIndexAttr && !staticStorageAttr && children.size() != 2) {
      emitError(location) << "class-wide rand_mode requires an on/off argument";
      return failure();
    }
    Operation *receiverNode = children.front();
    if (propertyIndexAttr || staticStorageAttr) {
      auto member =
          dyn_cast<semantic::SVMemberAccessExpressionOp>(children.front());
      auto named =
          dyn_cast<semantic::SVNamedValueExpressionOp>(children.front());
      SmallVector<Operation *> memberChildren =
          member ? getChildren(member) : SmallVector<Operation *>{};
      bool classQualifiedStatic = staticStorageAttr && named;
      if ((!member && !classQualifiedStatic) || memberChildren.size() > 1 ||
          (propertyIndexAttr && memberChildren.empty())) {
        emitError(location) << "property rand_mode has no object receiver";
        return failure();
      }
      receiverNode = classQualifiedStatic || memberChildren.empty()
                         ? nullptr
                         : memberChildren.front();
    }
    FailureOr<Value> loweredReceiver = failure();
    if (receiverNode)
      loweredReceiver = lowerExpression(receiverNode);
    auto objectType =
        succeeded(loweredReceiver)
            ? dyn_cast<sim::ClassHandleType>((*loweredReceiver).getType())
            : sim::ClassHandleType{};
    if (receiverNode && (failed(loweredReceiver) || !objectType)) {
      emitError(location) << "rand_mode receiver is not a class object";
      return failure();
    }
    Type i64 = builder.getI64Type();
    auto staticModeReference = [&](uint64_t storage) -> Value {
      Type referenceType = sim::RefType::get(function.getContext(), i64);
      Value context = function.getBody().front().getArgument(0);
      return sim::SimContextStorageOp::create(
          builder, location, referenceType, context,
          builder.getI64IntegerAttr(storage));
    };

    FailureOr<Value> setterEnabled;
    if (children.size() == 2) {
      FailureOr<Value> argument = lowerExpression(children.back());
      setterEnabled = succeeded(argument) ? truthValue(*argument, location)
                                          : FailureOr<Value>(failure());
      if (failed(setterEnabled))
        return failure();
    }

    if (staticStorageAttr) {
      APInt storage = staticStorageAttr.getValue();
      if (storage.isNegative() || storage.getActiveBits() > 63) {
        emitError(location) << "static property rand_mode storage is malformed";
        return failure();
      }
      Value reference = staticModeReference(storage.getZExtValue());
      Value zero = arith::ConstantOp::create(builder, location, i64,
                                             builder.getI64IntegerAttr(0));
      if (children.size() == 2) {
        Value mode = arith::SelectOp::create(
            builder, location, *setterEnabled, zero,
            arith::ConstantOp::create(builder, location, i64,
                                      builder.getI64IntegerAttr(1)));
        sim::SimRefStoreOp::create(builder, location, mode, reference);
        return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                         builder.getBoolAttr(false))
            .getResult();
      }
      Value mode = sim::SimRefLoadOp::create(builder, location, i64, reference);
      Value enabled = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, mode, zero);
      FailureOr<Type> resultType = getNormalizedSemanticType(op);
      if (failed(resultType))
        return failure();
      return convert(enabled, *resultType, false, location);
    }

    if (!receiverNode) {
      emitError(location) << "rand_mode receiver is not a class object";
      return failure();
    }
    sim::SimClassDeclOp declaration =
        SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
            function, objectType.getClassName());
    while (declaration &&
           !declaration->hasAttr("obelisk_sim.random_mode_field")) {
      if (!declaration.getBaseAttr())
        break;
      declaration = SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
          function, declaration.getBaseAttr());
    }
    auto modeField = declaration
                         ? declaration->getAttrOfType<FlatSymbolRefAttr>(
                               "obelisk_sim.random_mode_field")
                         : FlatSymbolRefAttr{};
    if (!declaration || !modeField) {
      emitError(location) << "rand_mode receiver has no mode state";
      return failure();
    }
    Type referenceType = sim::ManagedRefType::get(function.getContext(), i64,
                                                  objectType.getClassName());
    Value reference = sim::SimClassFieldRefOp::create(
        builder, location, referenceType, *loweredReceiver, modeField);
    uint64_t propertyBit = 0;
    if (propertyIndexAttr) {
      APInt propertyIndex = propertyIndexAttr.getValue();
      if (propertyIndex.isNegative() || propertyIndex.getActiveBits() > 64 ||
          propertyIndex.getZExtValue() >= 64) {
        emitError(location) << "property rand_mode index is malformed";
        return failure();
      }
      propertyBit = uint64_t{1} << propertyIndex.getZExtValue();
    }
    if (children.size() == 2) {
      Value zero = arith::ConstantOp::create(builder, location, i64,
                                             builder.getI64IntegerAttr(0));
      Value disabled = arith::ConstantOp::create(
          builder, location, i64,
          builder.getIntegerAttr(
              i64, APInt(64, propertyIndexAttr ? propertyBit : UINT64_MAX)));
      Value mode;
      if (propertyIndexAttr) {
        Value oldMode =
            sim::SimManagedLoadOp::create(builder, location, i64, reference);
        Value bit = arith::ConstantOp::create(
            builder, location, i64,
            builder.getIntegerAttr(i64, APInt(64, propertyBit)));
        Value enabledMode = arith::AndIOp::create(
            builder, location, oldMode,
            arith::ConstantOp::create(
                builder, location, i64,
                builder.getIntegerAttr(i64, APInt(64, ~propertyBit))));
        Value disabledMode =
            arith::OrIOp::create(builder, location, oldMode, bit);
        mode = arith::SelectOp::create(builder, location, *setterEnabled,
                                       enabledMode, disabledMode);
      } else {
        mode = arith::SelectOp::create(builder, location, *setterEnabled, zero,
                                       disabled);
      }
      auto storeMode = [&](ArrayRef<int64_t> staticStorages) {
        sim::SimManagedStoreOp::create(builder, location, mode, reference);
        Value staticMode = arith::SelectOp::create(
            builder, location, *setterEnabled, zero,
            arith::ConstantOp::create(builder, location, i64,
                                      builder.getI64IntegerAttr(1)));
        for (int64_t storage : staticStorages)
          sim::SimRefStoreOp::create(
              builder, location, staticMode,
              staticModeReference(static_cast<uint64_t>(storage)));
      };
      if (staticDispatchAttr) {
        Block *done = addBlock();
        for (Attribute attribute : staticDispatchAttr) {
          auto entry = dyn_cast<DictionaryAttr>(attribute);
          auto target = entry ? entry.getAs<FlatSymbolRefAttr>("class")
                              : FlatSymbolRefAttr{};
          auto storages = entry ? entry.getAs<DenseI64ArrayAttr>("storages")
                                : DenseI64ArrayAttr{};
          if (!target || !storages ||
              llvm::any_of(storages.asArrayRef(),
                           [](int64_t storage) { return storage < 0; })) {
            emitError(location) << "rand_mode static dispatch is malformed";
            return failure();
          }
          Value matches = sim::SimClassIsInstanceOp::create(
              builder, location, *loweredReceiver, target);
          Block *selected = addBlock();
          Block *next = addBlock();
          cf::CondBranchOp::create(builder, location, matches, selected,
                                   ValueRange{}, next, ValueRange{});
          setCurrent(selected);
          storeMode(storages.asArrayRef());
          cf::BranchOp::create(builder, location, done);
          setCurrent(next);
        }
        if (failed(emitRuntimeFatal(location,
                                    "rand_mode called on null class object")))
          return failure();
        setCurrent(addBlock());
        cf::BranchOp::create(builder, location, done);
        setCurrent(done);
      } else {
        storeMode({});
      }
      return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                       builder.getBoolAttr(false))
          .getResult();
    }
    Value mode =
        sim::SimManagedLoadOp::create(builder, location, i64, reference);
    Value zero = arith::ConstantOp::create(builder, location, i64,
                                           builder.getI64IntegerAttr(0));
    if (propertyIndexAttr)
      mode = arith::AndIOp::create(
          builder, location, mode,
          arith::ConstantOp::create(
              builder, location, i64,
              builder.getIntegerAttr(i64, APInt(64, propertyBit))));
    Value enabled = arith::CmpIOp::create(builder, location,
                                          arith::CmpIPredicate::eq, mode, zero);
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    if (failed(resultType))
      return failure();
    return convert(enabled, *resultType, false, location);
  }
  if (op->hasAttr(constraintModeAttrName) ||
      (op.getIsSystemCall() && op.getCalleeName() == "constraint_mode")) {
    if (children.empty() || children.size() > 2) {
      emitError(location)
          << "constraint_mode expects a receiver and an optional block "
             "query or on/off argument";
      return failure();
    }
    auto blockIndexAttr =
        op->getAttrOfType<IntegerAttr>(constraintModeBlockAttrName);
    if (!blockIndexAttr && children.size() != 2) {
      emitError(location)
          << "class-wide constraint_mode requires an on/off argument";
      return failure();
    }
    Operation *receiverNode = children.front();
    if (blockIndexAttr) {
      auto member =
          dyn_cast<semantic::SVMemberAccessExpressionOp>(children.front());
      SmallVector<Operation *> memberChildren =
          member ? getChildren(member) : SmallVector<Operation *>{};
      if (!member || memberChildren.size() != 1) {
        emitError(location)
            << "constraint-block constraint_mode has no object receiver";
        return failure();
      }
      receiverNode = memberChildren.front();
    }
    FailureOr<Value> loweredReceiver = lowerExpression(receiverNode);
    auto objectType =
        succeeded(loweredReceiver)
            ? dyn_cast<sim::ClassHandleType>((*loweredReceiver).getType())
            : sim::ClassHandleType{};
    if (failed(loweredReceiver) || !objectType) {
      emitError(location) << "constraint_mode receiver is not a class object";
      return failure();
    }
    Type i64 = builder.getI64Type();
    auto staticModeReference = [&](uint64_t storage) -> Value {
      Type referenceType = sim::RefType::get(function.getContext(), i64);
      Value context = function.getBody().front().getArgument(0);
      return sim::SimContextStorageOp::create(
          builder, location, referenceType, context,
          builder.getI64IntegerAttr(storage));
    };
    auto staticStorageAttr =
        op->getAttrOfType<IntegerAttr>(constraintModeStaticStorageAttrName);
    if (staticStorageAttr) {
      APInt storage = staticStorageAttr.getValue();
      APInt blockIndex =
          blockIndexAttr ? blockIndexAttr.getValue() : APInt(1, 0);
      if (!blockIndexAttr || blockIndex.isNegative() ||
          blockIndex.getActiveBits() > 64 || blockIndex.getZExtValue() >= 64 ||
          storage.isNegative() || storage.getActiveBits() > 63) {
        emitError(location)
            << "static constraint_mode storage metadata is malformed";
        return failure();
      }
      Value reference = staticModeReference(storage.getZExtValue());
      if (children.size() == 2) {
        FailureOr<Value> argument = lowerExpression(children.back());
        FailureOr<Value> enabled = succeeded(argument)
                                       ? truthValue(*argument, location)
                                       : FailureOr<Value>(failure());
        if (failed(enabled))
          return failure();
        Value mode = arith::SelectOp::create(
            builder, location, *enabled,
            arith::ConstantOp::create(builder, location, i64,
                                      builder.getI64IntegerAttr(0)),
            arith::ConstantOp::create(builder, location, i64,
                                      builder.getI64IntegerAttr(1)));
        sim::SimRefStoreOp::create(builder, location, mode, reference);
        return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                         builder.getBoolAttr(false))
            .getResult();
      }
      Value mode = sim::SimRefLoadOp::create(builder, location, i64, reference);
      Value enabled = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, mode,
          arith::ConstantOp::create(builder, location, i64,
                                    builder.getI64IntegerAttr(0)));
      FailureOr<Type> resultType = getNormalizedSemanticType(op);
      if (failed(resultType))
        return failure();
      return convert(enabled, *resultType, false, location);
    }
    sim::SimClassDeclOp declaration =
        SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
            function, objectType.getClassName());
    while (declaration &&
           !declaration->hasAttr("obelisk_sim.constraint_mode_field")) {
      if (!declaration.getBaseAttr())
        break;
      declaration = SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
          function, declaration.getBaseAttr());
    }
    auto modeField = declaration
                         ? declaration->getAttrOfType<FlatSymbolRefAttr>(
                               "obelisk_sim.constraint_mode_field")
                         : FlatSymbolRefAttr{};
    if (!declaration || !modeField) {
      emitError(location) << "constraint_mode receiver has no mode state";
      return failure();
    }
    Type referenceType = sim::ManagedRefType::get(function.getContext(), i64,
                                                  objectType.getClassName());
    Value reference = sim::SimClassFieldRefOp::create(
        builder, location, referenceType, *loweredReceiver, modeField);
    uint64_t blockBit = 0;
    if (blockIndexAttr) {
      APInt blockIndex = blockIndexAttr.getValue();
      if (blockIndex.isNegative() || blockIndex.getActiveBits() > 64 ||
          blockIndex.getZExtValue() >= 64) {
        emitError(location) << "constraint_mode block index is malformed";
        return failure();
      }
      blockBit = uint64_t{1} << blockIndex.getZExtValue();
    }
    if (children.size() == 2) {
      FailureOr<Value> argument = lowerExpression(children.back());
      FailureOr<Value> enabled = succeeded(argument)
                                     ? truthValue(*argument, location)
                                     : FailureOr<Value>(failure());
      if (failed(enabled))
        return failure();
      Value zero = arith::ConstantOp::create(builder, location, i64,
                                             builder.getI64IntegerAttr(0));
      Value disabled = arith::ConstantOp::create(
          builder, location, i64,
          builder.getIntegerAttr(
              i64, APInt(64, blockIndexAttr ? blockBit : UINT64_MAX)));
      Value mode;
      if (blockIndexAttr) {
        Value oldMode =
            sim::SimManagedLoadOp::create(builder, location, i64, reference);
        Value bit = arith::ConstantOp::create(
            builder, location, i64,
            builder.getIntegerAttr(i64, APInt(64, blockBit)));
        Value enabledMode = arith::AndIOp::create(
            builder, location, oldMode,
            arith::ConstantOp::create(
                builder, location, i64,
                builder.getIntegerAttr(i64, APInt(64, ~blockBit))));
        Value disabledMode =
            arith::OrIOp::create(builder, location, oldMode, bit);
        mode = arith::SelectOp::create(builder, location, *enabled, enabledMode,
                                       disabledMode);
      } else {
        mode = arith::SelectOp::create(builder, location, *enabled, zero,
                                       disabled);
      }
      sim::SimManagedStoreOp::create(builder, location, mode, reference);
      if (auto staticStorages = op->getAttrOfType<DenseI64ArrayAttr>(
              constraintModeStaticStoragesAttrName)) {
        Value staticMode = arith::SelectOp::create(
            builder, location, *enabled, zero,
            arith::ConstantOp::create(builder, location, i64,
                                      builder.getI64IntegerAttr(1)));
        for (int64_t storage : staticStorages.asArrayRef()) {
          if (storage == -1)
            continue;
          if (storage < -1) {
            emitError(location)
                << "static constraint_mode storage metadata is malformed";
            return failure();
          }
          sim::SimRefStoreOp::create(
              builder, location, staticMode,
              staticModeReference(static_cast<uint64_t>(storage)));
        }
      }
      return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                       builder.getBoolAttr(false))
          .getResult();
    }
    Value mode =
        sim::SimManagedLoadOp::create(builder, location, i64, reference);
    Value bit = arith::ConstantOp::create(
        builder, location, i64,
        builder.getIntegerAttr(i64, APInt(64, blockBit)));
    Value selected = arith::AndIOp::create(builder, location, mode, bit);
    Value zero = arith::ConstantOp::create(builder, location, i64,
                                           builder.getI64IntegerAttr(0));
    Value enabled = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, selected, zero);
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    if (failed(resultType))
      return failure();
    return convert(enabled, *resultType, false, location);
  }
  StringRef objectRandomMethod = op.getCalleeName();
  if (objectRandomMethod == "get_randstate" ||
      objectRandomMethod == "set_randstate" ||
      objectRandomMethod == "srandom") {
    size_t expectedChildren = objectRandomMethod == "get_randstate" ? 1 : 2;
    if (children.size() != expectedChildren) {
      emitError(location) << objectRandomMethod
                          << " has malformed object-method arguments";
      return failure();
    }
    FailureOr<Value> loweredReceiver = lowerExpression(children.front());
    if (failed(loweredReceiver) ||
        !isa<sim::ClassHandleType>((*loweredReceiver).getType())) {
      emitError(location) << objectRandomMethod
                          << " receiver is not a class object";
      return failure();
    }
    Type i64 = builder.getI64Type();
    Value methodArgument;
    if (objectRandomMethod == "set_randstate") {
      FailureOr<Value> snapshot = lowerExpression(children[1]);
      if (failed(snapshot) || !isa<sim::StringType>((*snapshot).getType())) {
        emitError(location) << "set_randstate argument is not a string";
        return failure();
      }
      methodArgument = *snapshot;
    } else if (objectRandomMethod == "srandom") {
      FailureOr<Value> seed = lowerExpression(children[1]);
      if (failed(seed))
        return failure();
      seed = convert(*seed, builder.getI32Type(), isSignedNode(children[1]),
                     location);
      if (failed(seed))
        return failure();
      methodArgument = arith::ExtUIOp::create(builder, location, i64, *seed);
    }

    auto emitMethod = [&](Value receiver) -> FailureOr<Value> {
      auto objectType = cast<sim::ClassHandleType>(receiver.getType());
      sim::SimClassDeclOp declaration =
          SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
              function, objectType.getClassName());
      while (declaration &&
             !declaration->hasAttr("obelisk_sim.random_state_field")) {
        if (!declaration.getBaseAttr())
          break;
        declaration = SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
            function, declaration.getBaseAttr());
      }
      auto stateField = declaration
                            ? declaration->getAttrOfType<FlatSymbolRefAttr>(
                                  "obelisk_sim.random_state_field")
                            : FlatSymbolRefAttr{};
      auto incrementField = declaration
                                ? declaration->getAttrOfType<FlatSymbolRefAttr>(
                                      "obelisk_sim.random_increment_field")
                                : FlatSymbolRefAttr{};
      if (!declaration || !stateField || !incrementField) {
        emitError(location)
            << objectRandomMethod << " receiver has no object-local stream";
        return failure();
      }
      Type referenceType = sim::ManagedRefType::get(function.getContext(), i64,
                                                    objectType.getClassName());
      Value stateReference = sim::SimClassFieldRefOp::create(
          builder, location, referenceType, receiver, stateField);
      Value incrementReference = sim::SimClassFieldRefOp::create(
          builder, location, referenceType, receiver, incrementField);
      auto dummyResult = [&]() -> Value {
        return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                         builder.getBoolAttr(false));
      };
      if (objectRandomMethod == "get_randstate") {
        Value state = sim::SimManagedLoadOp::create(builder, location, i64,
                                                    stateReference);
        Value increment = sim::SimManagedLoadOp::create(builder, location, i64,
                                                        incrementReference);
        Type stringType = sim::StringType::get(function.getContext());
        Value stateText = sim::SimStringFormatIntegerOp::create(
            builder, location, stringType, state, builder.getI32IntegerAttr(16),
            builder.getBoolAttr(false));
        Value separator = sim::SimStringLiteralOp::create(
            builder, location, stringType, builder.getStringAttr(":"));
        Value prefix = sim::SimStringConcatOp::create(
            builder, location, stringType, ValueRange{stateText, separator});
        Value incrementText = sim::SimStringFormatIntegerOp::create(
            builder, location, stringType, increment,
            builder.getI32IntegerAttr(16), builder.getBoolAttr(false));
        return sim::SimStringConcatOp::create(builder, location, stringType,
                                              ValueRange{prefix, incrementText})
            .getResult();
      }
      if (objectRandomMethod == "set_randstate") {
        Type stringType = sim::StringType::get(function.getContext());
        Type i32 = builder.getI32Type();
        Value cursor = arith::ConstantOp::create(builder, location, i32,
                                                 builder.getI32IntegerAttr(0));
        auto stateField = sim::SimStringScanFieldOp::create(
            builder, location, TypeRange{stringType, i32, i32}, methodArgument,
            cursor, builder.getStringAttr(""), static_cast<uint32_t>('x'));
        auto incrementField = sim::SimStringScanFieldOp::create(
            builder, location, TypeRange{stringType, i32, i32}, methodArgument,
            stateField.getNextCursor(), builder.getStringAttr(":"),
            static_cast<uint32_t>('x'));
        Value parsedState = sim::SimStringParseIntegerOp::create(
            builder, location, i64, stateField.getField(),
            builder.getI32IntegerAttr(16));
        Value parsedIncrement = sim::SimStringParseIntegerOp::create(
            builder, location, i64, incrementField.getField(),
            builder.getI32IntegerAttr(16));
        Value zero = arith::ConstantOp::create(builder, location, i32,
                                               builder.getI32IntegerAttr(0));
        Value stateMatched =
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                  stateField.getOk(), zero);
        Value incrementMatched =
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                  incrementField.getOk(), zero);
        Value matched = arith::AndIOp::create(builder, location, stateMatched,
                                              incrementMatched);
        Value oldState = sim::SimManagedLoadOp::create(builder, location, i64,
                                                       stateReference);
        Value oldIncrement = sim::SimManagedLoadOp::create(
            builder, location, i64, incrementReference);
        Value state = arith::SelectOp::create(builder, location, matched,
                                              parsedState, oldState);
        Value increment = arith::SelectOp::create(
            builder, location, matched, parsedIncrement, oldIncrement);
        sim::SimManagedStoreOp::create(builder, location, state,
                                       stateReference);
        sim::SimManagedStoreOp::create(builder, location, increment,
                                       incrementReference);
        return dummyResult();
      }
      Value increment = arith::ConstantOp::create(
          builder, location, i64,
          builder.getIntegerAttr(
              i64, APInt(64, OBELISK_RT_RANDOM_DEFAULT_INCREMENT)));
      Value multiplier = arith::ConstantOp::create(
          builder, location, i64,
          builder.getIntegerAttr(i64, APInt(64, OBELISK_RT_RANDOM_MULTIPLIER)));
      Value state =
          arith::AddIOp::create(builder, location, increment, methodArgument);
      state = arith::MulIOp::create(builder, location, state, multiplier);
      state = arith::AddIOp::create(builder, location, state, increment);
      sim::SimManagedStoreOp::create(builder, location, state, stateReference);
      sim::SimManagedStoreOp::create(builder, location, increment,
                                     incrementReference);
      return dummyResult();
    };

    auto dispatchClasses =
        op->getAttrOfType<ArrayAttr>(objectRandomDispatchClassesAttrName);
    if (!dispatchClasses)
      return emitMethod(*loweredReceiver);

    Type resultType = objectRandomMethod == "get_randstate"
                          ? Type(sim::StringType::get(function.getContext()))
                          : Type(builder.getI1Type());
    Block *done = addBlock();
    Value result = done->addArgument(resultType, location);
    for (Attribute attribute : dispatchClasses) {
      auto target = dyn_cast<FlatSymbolRefAttr>(attribute);
      if (!target) {
        emitError(location) << "object random-stream dispatch is malformed";
        return failure();
      }
      Value matches = sim::SimClassIsInstanceOp::create(
          builder, location, *loweredReceiver, target);
      Block *selected = addBlock();
      Block *next = addBlock();
      cf::CondBranchOp::create(builder, location, matches, selected,
                               ValueRange{}, next, ValueRange{});
      setCurrent(selected);
      Type targetType =
          sim::ClassHandleType::get(function.getContext(), target);
      Value receiver = sim::SimClassCastOp::create(
          builder, location, targetType, *loweredReceiver);
      FailureOr<Value> selectedResult = emitMethod(receiver);
      if (failed(selectedResult))
        return failure();
      cf::BranchOp::create(builder, location, done,
                           ValueRange{*selectedResult});
      setCurrent(next);
    }
    if (failed(emitRuntimeFatal(
            location, "object random-stream method called on null object")))
      return failure();
    setCurrent(addBlock());
    Value unreachableResult = objectRandomMethod == "get_randstate"
                                  ? Value(sim::SimManagedNullOp::create(
                                        builder, location, resultType))
                                  : Value(arith::ConstantOp::create(
                                        builder, location, resultType,
                                        builder.getIntegerAttr(resultType, 0)));
    cf::BranchOp::create(builder, location, done,
                         ValueRange{unreachableResult});
    setCurrent(done);
    return result;
  }
  if (op.getIsSystemCall() && !stringBuiltin && !containerBuiltin &&
      !associativeBuiltin)
    return lowerSystemCall(op);
  if (associativeBuiltin)
    return lowerAssociativeArrayMethod(op);
  if (containerBuiltin)
    return lowerArrayMethod(op);
  if (isWeakReferenceCall(op)) {
    StringRef name = op.getCalleeName();
    if (name == "get") {
      if (children.size() != 1)
        return emitError(location)
                   << "weak_reference::get requires one receiver",
               failure();
      FailureOr<Value> weak = lowerExpression(children.front());
      FailureOr<Type> resultType = getNormalizedSemanticType(op);
      if (failed(weak) || failed(resultType) ||
          !isa<sim::ClassHandleType>((*weak).getType()) ||
          !isa<sim::ClassHandleType>(*resultType))
        return failure();
      return sim::SimWeakGetOp::create(builder, location, *resultType, *weak)
          .getResult();
    }
    if (name == "clear") {
      if (children.size() != 1)
        return emitError(location)
                   << "weak_reference::clear requires one receiver",
               failure();
      FailureOr<Value> weak = lowerExpression(children.front());
      if (failed(weak) || !isa<sim::ClassHandleType>((*weak).getType()))
        return failure();
      sim::SimWeakClearOp::create(builder, location, *weak);
      return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                       builder.getBoolAttr(false))
          .getResult();
    }
    if (name == "get_id") {
      if (children.size() != 1)
        return emitError(location)
                   << "weak_reference::get_id requires one object argument",
               failure();
      FailureOr<Value> object = lowerExpression(children.front());
      if (failed(object) || !isa<sim::ClassHandleType>((*object).getType()))
        return failure();
      return sim::SimClassIdOp::create(builder, location, *object).getResult();
    }
    return emitError(location)
               << "unsupported weak_reference built-in method " << name,
           failure();
  }
  auto callee = op->getAttrOfType<FlatSymbolRefAttr>(calleeAttrName);
  if (!callee && !children.empty()) {
    FailureOr<Type> receiverType = getNormalizedSemanticType(children.front());
    if (succeeded(receiverType) && isa<sim::StringType>(*receiverType)) {
      StringRef name = op.getCalleeName();
      auto result = [&](Value value) -> FailureOr<Value> {
        FailureOr<Type> resultType = getNormalizedSemanticType(op);
        if (failed(resultType))
          return failure();
        return convert(value, *resultType, false, location);
      };
      auto receiver = [&]() -> FailureOr<Value> {
        return lowerExpression(children.front());
      };
      auto integerArgument = [&](unsigned index,
                                 Type type) -> FailureOr<Value> {
        if (index >= children.size())
          return failure();
        FailureOr<Value> value = lowerExpression(children[index]);
        if (failed(value))
          return failure();
        return convert(*value, type, isSignedNode(children[index]), location);
      };

      if (name == "len" && children.size() == 1) {
        FailureOr<Value> input = receiver();
        if (failed(input))
          return failure();
        return result(sim::SimStringLengthOp::create(
            builder, location, builder.getI64Type(), *input));
      }
      if (name == "getc" && children.size() == 2) {
        FailureOr<Value> input = receiver();
        FailureOr<Value> index = integerArgument(1, builder.getI64Type());
        if (failed(input) || failed(index))
          return failure();
        return result(sim::SimStringGetcOp::create(
            builder, location, builder.getI8Type(), *input, *index));
      }
      if ((name == "toupper" || name == "tolower") && children.size() == 1) {
        FailureOr<Value> input = receiver();
        if (failed(input))
          return failure();
        return result(sim::SimStringCaseConvertOp::create(
            builder, location, sim::StringType::get(function.getContext()),
            *input, builder.getBoolAttr(name == "toupper")));
      }
      if ((name == "compare" || name == "icompare") && children.size() == 2) {
        FailureOr<Value> left = receiver();
        FailureOr<Value> right = lowerExpression(children[1]);
        if (failed(left) || failed(right))
          return failure();
        FailureOr<Value> converted =
            convert(*right, sim::StringType::get(function.getContext()),
                    isSignedNode(children[1]), location);
        if (failed(converted))
          return failure();
        return result(sim::SimStringCompareOp::create(
            builder, location, builder.getI32Type(), *left, *converted,
            builder.getBoolAttr(name == "icompare")));
      }
      if (name == "substr" && children.size() == 3) {
        FailureOr<Value> input = receiver();
        FailureOr<Value> left = integerArgument(1, builder.getI64Type());
        FailureOr<Value> right = integerArgument(2, builder.getI64Type());
        if (failed(input) || failed(left) || failed(right))
          return failure();
        return result(sim::SimStringSubstrOp::create(
            builder, location, sim::StringType::get(function.getContext()),
            *input, *left, *right));
      }
      if ((name == "atoi" || name == "atohex" || name == "atooct" ||
           name == "atobin") &&
          children.size() == 1) {
        FailureOr<Value> input = receiver();
        if (failed(input))
          return failure();
        unsigned radix = name == "atobin"   ? 2
                         : name == "atooct" ? 8
                         : name == "atohex" ? 16
                                            : 10;
        return result(sim::SimStringParseIntegerOp::create(
            builder, location, builder.getI64Type(), *input,
            builder.getI32IntegerAttr(radix)));
      }
      if (name == "atoreal" && children.size() == 1) {
        FailureOr<Value> input = receiver();
        if (failed(input))
          return failure();
        return result(sim::SimStringParseRealOp::create(
            builder, location, builder.getF64Type(), *input));
      }
      if ((name == "itoa" || name == "hextoa" || name == "octtoa" ||
           name == "bintoa") &&
          children.size() == 2) {
        FailureOr<Value> destination = lowerExpression(children.front(), true);
        FailureOr<Value> input = integerArgument(1, builder.getI64Type());
        if (failed(destination) || failed(input))
          return failure();
        unsigned radix = name == "bintoa"   ? 2
                         : name == "octtoa" ? 8
                         : name == "hextoa" ? 16
                                            : 10;
        Value updated = sim::SimStringFormatIntegerOp::create(
            builder, location, sim::StringType::get(function.getContext()),
            *input, builder.getI32IntegerAttr(radix),
            builder.getBoolAttr(name == "itoa" && isSignedNode(children[1])));
        if (failed(storeReference(*destination, updated, location)))
          return failure();
        return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                         builder.getBoolAttr(false))
            .getResult();
      }
      if (name == "realtoa" && children.size() == 2) {
        FailureOr<Value> destination = lowerExpression(children.front(), true);
        FailureOr<Value> input = lowerExpression(children[1]);
        if (failed(destination) || failed(input))
          return failure();
        FailureOr<Value> real =
            convert(*input, builder.getF64Type(), false, location);
        if (failed(real))
          return failure();
        Value updated = sim::SimStringFormatRealOp::create(
            builder, location, sim::StringType::get(function.getContext()),
            *real);
        if (failed(storeReference(*destination, updated, location)))
          return failure();
        return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                         builder.getBoolAttr(false))
            .getResult();
      }
      if (name == "putc" && children.size() == 3) {
        FailureOr<Value> destination = lowerExpression(children.front(), true);
        FailureOr<Value> index = integerArgument(1, builder.getI64Type());
        FailureOr<Value> character = integerArgument(2, builder.getI8Type());
        if (failed(destination) || failed(index) || failed(character))
          return failure();
        FailureOr<Value> input = loadReference(*destination, location);
        if (failed(input))
          return failure();
        Value updated = sim::SimStringPutcOp::create(
            builder, location, sim::StringType::get(function.getContext()),
            *input, *index, *character);
        if (failed(storeReference(*destination, updated, location)))
          return failure();
        return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                         builder.getBoolAttr(false))
            .getResult();
      }
      emitError(location) << "unsupported string built-in method " << name;
      return failure();
    }
  }
  if (!callee) {
    if (op.getHasThisClass() && !children.empty()) {
      FailureOr<Type> receiverType =
          getNormalizedSemanticType(children.front());
      if (succeeded(receiverType) &&
          isa<sim::VirtualInterfaceType>(*receiverType)) {
        emitError(location)
            << "virtual-interface method is not imported by the selected "
               "modport (exported methods are not yet supported)";
        return failure();
      }
    }
    unsupported(op) << " (indirect or system call)";
    return failure();
  }
  if (op->hasAttr("obelisk_sim.class_instance")) {
    auto formals = op->getAttrOfType<ArrayAttr>(calleeFormalsAttrName);
    bool superCall = op->hasAttr("obelisk_sim.class_super");
    bool implicitThis = !superCall && formals && thisObject &&
                        children.size() == formals.size();
    if (!formals ||
        formals.size() + (superCall || implicitThis ? 0 : 1) !=
            children.size() ||
        ((superCall || implicitThis) && !thisObject)) {
      emitError(location)
          << "instance call has no receiver or complete formal inventory";
      return failure();
    }
    FailureOr<Value> receiver = superCall || implicitThis
                                    ? FailureOr<Value>(thisObject)
                                    : lowerExpression(children.front());
    if (failed(receiver) || !isa<sim::ClassHandleType>((*receiver).getType()))
      return failure();
    auto method =
        op->getAttrOfType<FlatSymbolRefAttr>("obelisk_sim.class_method");
    auto declaration =
        method
            ? SymbolTable::lookupNearestSymbolFrom<sim::SimClassMethodDeclOp>(
                  op, method)
            : sim::SimClassMethodDeclOp{};
    if (declaration) {
      Type targetType = sim::ClassHandleType::get(function.getContext(),
                                                  declaration.getOwnerAttr());
      if ((*receiver).getType() != targetType)
        receiver = sim::SimClassCastOp::create(builder, location, targetType,
                                               *receiver)
                       .getResult();
    } else if (superCall || implicitThis) {
      emitError(location) << "implicit receiver has no class method descriptor";
      return failure();
    }
    struct ClassCopyOut {
      Value destination;
      Type formalType;
      bool formalSigned;
      bool destinationSigned;
    };
    struct TaskIndirectCopyOut {
      Value temporary;
      Value destination;
      bool formalSigned;
      bool destinationSigned;
    };
    SmallVector<Value> arguments;
    SmallVector<ClassCopyOut> copyOuts;
    SmallVector<TaskIndirectCopyOut> taskIndirectCopyOuts;
    bool classTask = op->hasAttr("obelisk_sim.is_task");
    for (auto [actual, formalAttr] :
         llvm::zip_equal(ArrayRef<Operation *>(children).drop_front(
                             superCall || implicitThis ? 0 : 1),
                         formals)) {
      auto formal = cast<DictionaryAttr>(formalAttr);
      auto direction = static_cast<semantic::SVArgumentDirection>(
          formal.getAs<IntegerAttr>("direction").getInt());
      Type formalType = formal.getAs<TypeAttr>("type").getValue();
      bool formalSigned = formal.getAs<BoolAttr>("is_signed").getValue();
      if (direction == semantic::SVArgumentDirection::In) {
        FailureOr<Value> argument = lowerExpression(actual);
        if (failed(argument))
          return failure();
        FailureOr<Value> converted =
            convert(*argument, formalType, isSignedNode(actual), location,
                    formalSigned);
        if (failed(converted))
          return failure();
        arguments.push_back(cloneSequentialValue(*converted, location));
        continue;
      }

      Operation *destination = actual;
      if (auto assignment =
              dyn_cast<semantic::SVAssignmentExpressionOp>(actual)) {
        SmallVector<Operation *> outputChildren = getChildren(assignment);
        if (outputChildren.size() == 2) {
          Operation *placeholder = outputChildren[1];
          while (isa<semantic::SVConversionExpressionOp>(placeholder)) {
            SmallVector<Operation *> converted = getChildren(placeholder);
            if (converted.size() != 1)
              break;
            placeholder = converted.front();
          }
          if (isa<semantic::SVEmptyArgumentExpressionOp>(placeholder))
            destination = outputChildren.front();
        }
      }
      FailureOr<Value> destinationRef = lowerExpression(destination, true);
      if (failed(destinationRef))
        return failure();
      Type destinationType;
      if (auto ref = dyn_cast<sim::RefType>((*destinationRef).getType()))
        destinationType = ref.getElementType();
      else if (auto ref =
                   dyn_cast<sim::ManagedRefType>((*destinationRef).getType()))
        destinationType = ref.getElementType();
      else if (auto ref =
                   dyn_cast<sim::ArgumentRefType>((*destinationRef).getType()))
        destinationType = ref.getElementType();
      else if (auto ref = dyn_cast<sim::ReferencePathType>(
                   (*destinationRef).getType()))
        destinationType = ref.getElementType();
      else {
        emitError(location)
            << "class method output, inout, and ref actuals must be variable "
               "references";
        return failure();
      }
      if (direction == semantic::SVArgumentDirection::Ref) {
        FailureOr<Value> argument =
            toArgumentReference(*destinationRef, formalType, location);
        if (failed(argument)) {
          emitError(location)
              << "class method ref actual type must exactly match the formal "
                 "type";
          return failure();
        }
        arguments.push_back(*argument);
        continue;
      }

      Value initial;
      if (direction == semantic::SVArgumentDirection::Out) {
        initial = createDefaultValue(builder, location, formalType);
        if (!initial) {
          emitError(location)
              << "cannot materialize a class output-formal default for type "
              << formalType;
          return failure();
        }
      } else {
        FailureOr<Value> loaded = loadReference(*destinationRef, location);
        if (failed(loaded))
          return failure();
        if (isa<sim::RefType>((*destinationRef).getType()))
          recordSensitivity(*destinationRef);
        FailureOr<Value> converted =
            convert(*loaded, formalType, isSignedNode(destination), location,
                    formalSigned);
        if (failed(converted))
          return failure();
        initial = *converted;
      }
      arguments.push_back(cloneSequentialValue(initial, location));
      if (classTask) {
        if (!isa<sim::RefType>((*destinationRef).getType())) {
          Value temporary = sim::SimRefAllocOp::create(
              builder, location,
              sim::RefType::get(function.getContext(), formalType), initial);
          arguments.push_back(temporary);
          taskIndirectCopyOuts.push_back({temporary, *destinationRef,
                                          formalSigned,
                                          isSignedNode(destination)});
        } else {
          arguments.push_back(*destinationRef);
        }
      } else {
        copyOuts.push_back({*destinationRef, formalType, formalSigned,
                            isSignedNode(destination)});
      }
    }
    if (auto captures = op->getAttrOfType<ArrayAttr>(calleeCapturesAttrName))
      for (Attribute captureAttr : captures) {
        StringRef path = cast<StringAttr>(captureAttr).getValue();
        Value capture = values.lookup(path);
        if (!capture) {
          emitError(location)
              << "method capture has no frozen local binding: " << path;
          return failure();
        }
        arguments.push_back(capture);
      }
    SmallVector<Type> resultTypes;
    if (auto semanticType = op->getAttrOfType<TypeAttr>("semantic_type");
        semanticType && !isa<semantic::VoidType>(semanticType.getValue())) {
      FailureOr<Type> resultType = getNormalizedSemanticType(op);
      if (failed(resultType))
        return failure();
      resultTypes.push_back(*resultType);
    }
    bool hasFunctionResult = !resultTypes.empty();
    if (!classTask)
      for (const ClassCopyOut &copyOut : copyOuts)
        resultTypes.push_back(copyOut.formalType);
    if (classTask) {
      Block *continuation = addBlock();
      auto finishTask = [&]() -> FailureOr<Value> {
        setCurrent(continuation);
        for (const TaskIndirectCopyOut &copyOut : taskIndirectCopyOuts) {
          auto temporaryType =
              cast<sim::RefType>(copyOut.temporary.getType()).getElementType();
          Value copied = sim::SimRefLoadOp::create(
              builder, location, temporaryType, copyOut.temporary);
          Type destinationType = getReferenceElementType(copyOut.destination);
          if (!destinationType)
            return failure();
          FailureOr<Value> converted =
              convert(copied, destinationType, copyOut.formalSigned, location,
                      copyOut.destinationSigned);
          if (failed(converted))
            return failure();
          if (failed(storeReference(copyOut.destination, *converted, location)))
            return failure();
        }
        return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                         builder.getBoolAttr(false))
            .getResult();
      };
      auto emitTaskCall = [&](FlatSymbolRefAttr target, Value taskReceiver) {
        SmallVector<Value> operands{function.getBody().front().getArgument(0),
                                    taskReceiver};
        llvm::append_range(operands, arguments);
        sim::SimTaskCallOp::create(builder, location, target, operands,
                                   builder.getI64IntegerAttr(operands.size()),
                                   sim::ContinuationSiteAttr{}, continuation);
      };
      if (!op->hasAttr("obelisk_sim.class_virtual")) {
        emitTaskCall(callee, *receiver);
        return finishTask();
      }

      auto slot = op->getAttrOfType<IntegerAttr>("obelisk_sim.class_slot");
      auto signature =
          op->getAttrOfType<IntegerAttr>("obelisk_sim.class_signature");
      if (!method || !slot || !signature || signature.getValue().isZero())
        return emitError(location)
                   << "virtual class task has no frozen slot and signature",
               failure();
      sim::SimClassVirtualTaskCallOp::create(
          builder, location, *receiver, method, slot, signature, arguments,
          builder.getI64IntegerAttr(arguments.size()),
          sim::ContinuationSiteAttr{}, continuation);
      return finishTask();
    }
    ValueRange results;
    if (op->hasAttr("obelisk_sim.class_virtual")) {
      auto slot = op->getAttrOfType<IntegerAttr>("obelisk_sim.class_slot");
      auto signature =
          op->getAttrOfType<IntegerAttr>("obelisk_sim.class_signature");
      if (!method || !slot || !signature || signature.getValue().isZero()) {
        emitError(location)
            << "virtual call has no frozen method slot and signature";
        return failure();
      }
      results = sim::SimClassVirtualCallOp::create(
                    builder, location, resultTypes, *receiver, method, slot,
                    signature, arguments)
                    .getResults();
    } else {
      results =
          sim::SimClassDirectCallOp::create(builder, location, resultTypes,
                                            callee, *receiver, arguments)
              .getResults();
    }
    if (!classTask)
      for (auto [index, copyOut] : llvm::enumerate(copyOuts)) {
        Value result = results[index + (hasFunctionResult ? 1 : 0)];
        Type destinationType = getReferenceElementType(copyOut.destination);
        if (!destinationType)
          return failure();
        FailureOr<Value> converted =
            convert(result, destinationType, copyOut.formalSigned, location,
                    copyOut.destinationSigned);
        if (failed(converted))
          return failure();
        if (failed(storeReference(copyOut.destination, *converted, location)))
          return failure();
      }
    if (hasFunctionResult)
      return results.front();
    return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                     builder.getBoolAttr(false))
        .getResult();
  }
  bool directTask = op->hasAttr("obelisk_sim.is_task");
  SmallVector<Value> operands{function.getBody().front().getArgument(0)};
  auto formals = op->getAttrOfType<ArrayAttr>(calleeFormalsAttrName);
  auto virtualCallees =
      op->getAttrOfType<ArrayAttr>("obelisk_sim.virtual_interface_callees");
  bool staticClassReceiver = op->hasAttr(staticClassReceiverAttrName);
  unsigned receiverCount = staticClassReceiver || virtualCallees ? 1 : 0;
  if (!formals || formals.size() + receiverCount != children.size()) {
    emitError(location)
        << "direct call has no complete frozen formal inventory";
    return failure();
  }
  ArrayRef<Operation *> actuals = children;
  Value virtualScope;
  if (virtualCallees) {
    FailureOr<Value> receiver = lowerExpression(children.front());
    if (failed(receiver) ||
        !isa<sim::VirtualInterfaceType>((*receiver).getType()))
      return failure();
    virtualScope = sim::SimVirtualInterfaceScopeOp::create(
        builder, location, builder.getI64Type(), *receiver);
    actuals = actuals.drop_front();
  } else if (staticClassReceiver) {
    // Static methods do not receive an object, but an object-qualified call
    // still evaluates its prefix expression before the explicit arguments.
    if (failed(lowerExpression(children.front())))
      return failure();
    actuals = actuals.drop_front();
  }
  struct CopyOut {
    Value destination;
    Value taskDestination;
    Type formalType;
    bool formalSigned;
    bool destinationSigned;
    uint32_t dpiCategory;
  };
  auto getDPITransportWidth = [](Type type) -> std::optional<unsigned> {
    if (isa<sim::StringType, sim::ChandleType>(type))
      return 64;
    return sim::getPackedWidth(type);
  };
  auto isDPIFourState = [](Type type) {
    Type scalar = sim::getPackedScalarType(type);
    return scalar && isa<sim::LogicType>(scalar);
  };
  SmallVector<CopyOut> copyOuts;
  SmallVector<Attribute> dpiOperandABI;
  for (auto [child, formalAttr] : llvm::zip_equal(actuals, formals)) {
    auto formal = cast<DictionaryAttr>(formalAttr);
    auto direction = static_cast<semantic::SVArgumentDirection>(
        formal.getAs<IntegerAttr>("direction").getInt());
    Type formalType = formal.getAs<TypeAttr>("type").getValue();
    bool formalSigned = formal.getAs<BoolAttr>("is_signed").getValue();
    auto dpiCategoryAttr = formal.getAs<IntegerAttr>("dpi_category");
    uint32_t dpiCategory =
        dpiCategoryAttr ? static_cast<uint32_t>(dpiCategoryAttr.getInt()) : 0;
    bool isInput = direction == semantic::SVArgumentDirection::In;
    if (op->hasAttr("obelisk.dpi.import_id")) {
      if (isa<semantic::DynArrayType, semantic::QueueType,
              semantic::AssocArrayType, sim::DynamicArrayType, sim::QueueType,
              sim::AssocArrayType>(formalType)) {
        emitError(location)
            << "DPI-C dynamic-array, queue, and associative-array "
               "marshalling is unsupported";
        return failure();
      }
      std::optional<unsigned> width = getDPITransportWidth(formalType);
      if (!width) {
        emitError(location) << "DPI formal has no fixed transport width";
        return failure();
      }
      dpiOperandABI.push_back(sim::DPIABIAttr::get(
          builder.getContext(), static_cast<sim::DPIABIKind>(dpiCategory),
          static_cast<sim::DPIArgumentDirection>(direction), *width,
          isDPIFourState(formalType), formalSigned));
    }

    Operation *actual = child;
    if (!isInput)
      if (auto assignment =
              dyn_cast<semantic::SVAssignmentExpressionOp>(child)) {
        SmallVector<Operation *> outputChildren = getChildren(assignment);
        if (outputChildren.size() == 2) {
          Operation *placeholder = outputChildren[1];
          while (isa<semantic::SVConversionExpressionOp>(placeholder)) {
            SmallVector<Operation *> converted = getChildren(placeholder);
            if (converted.size() != 1)
              break;
            placeholder = converted.front();
          }
          if (isa<semantic::SVEmptyArgumentExpressionOp>(placeholder))
            actual = outputChildren.front();
        }
      }

    if (isInput) {
      FailureOr<Value> argument = lowerExpression(actual);
      if (failed(argument))
        return failure();
      FailureOr<Value> converted = convert(
          *argument, formalType, isSignedNode(actual), location, formalSigned);
      if (failed(converted))
        return failure();
      operands.push_back(cloneSequentialValue(*converted, location));
      continue;
    }

    FailureOr<Value> destination = lowerExpression(actual, true);
    if (failed(destination))
      return failure();
    Type destinationType = getReferenceElementType(*destination);
    if (!destinationType) {
      emitError(location)
          << "output, inout, and ref actuals must be variable references";
      return failure();
    }
    if (direction == semantic::SVArgumentDirection::Ref) {
      auto argumentRef = formal.getAs<BoolAttr>("argument_ref");
      if (directTask && (!argumentRef || !argumentRef.getValue())) {
        if (!isa<sim::RefType>((*destination).getType())) {
          emitError(location) << "task ref actual must be directly addressable";
          return failure();
        }
        operands.push_back(*destination);
        continue;
      }
      FailureOr<Value> argument =
          toArgumentReference(*destination, formalType, location);
      if (failed(argument)) {
        emitError(location)
            << "ref actual type must exactly match the formal type";
        return failure();
      }
      operands.push_back(*argument);
      continue;
    }

    Value initial;
    if (direction == semantic::SVArgumentDirection::Out) {
      initial = createDefaultValue(builder, location, formalType);
      if (!initial) {
        emitError(location)
            << "cannot materialize an output-formal default for type "
            << formalType;
        return failure();
      }
    } else {
      FailureOr<Value> loaded = loadReference(*destination, location);
      if (failed(loaded))
        return failure();
      FailureOr<Value> converted = convert(
          *loaded, formalType, isSignedNode(actual), location, formalSigned);
      if (failed(converted))
        return failure();
      initial = *converted;
      if (isa<sim::RefType>((*destination).getType()))
        recordSensitivity(*destination);
    }
    operands.push_back(cloneSequentialValue(initial, location));
    Value taskDestination;
    if (directTask) {
      taskDestination = *destination;
      if (!isa<sim::RefType>((*destination).getType()))
        taskDestination = sim::SimRefAllocOp::create(
            builder, location,
            sim::RefType::get(function.getContext(), formalType), initial);
      operands.push_back(taskDestination);
    }
    copyOuts.push_back({*destination, taskDestination, formalType, formalSigned,
                        isSignedNode(actual), dpiCategory});
  }

  llvm::StringSet<> readCaptures;
  if (auto reads = op->getAttrOfType<ArrayAttr>(calleeReadCapturesAttrName))
    for (Attribute read : reads)
      readCaptures.insert(cast<StringAttr>(read).getValue());
  if (!virtualCallees)
    if (auto captures = op->getAttrOfType<ArrayAttr>(calleeCapturesAttrName))
      for (Attribute captureAttr : captures) {
        StringRef path = cast<StringAttr>(captureAttr).getValue();
        Value capture = values.lookup(path);
        if (!capture) {
          emitError(location)
              << "direct callee capture has no frozen local binding: " << path;
          return failure();
        }
        if (readCaptures.contains(path))
          recordSensitivity(capture);
        operands.push_back(capture);
      }
  BoolAttr dpiTaskAttr = op->getAttrOfType<BoolAttr>("obelisk.dpi.is_task");
  bool dpiTask = dpiTaskAttr && dpiTaskAttr.getValue();
  sim::SimFuncOp directCallee =
      SymbolTable::lookupNearestSymbolFrom<sim::SimFuncOp>(op, callee);
  bool voidFunction =
      directCallee && directCallee->hasAttr("obelisk_sim.void_function");
  bool hasFunctionResult = !dpiTask && !directTask && !voidFunction;
  SmallVector<Type> callResultTypes;
  if (hasFunctionResult) {
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    if (failed(resultType))
      return failure();
    callResultTypes.push_back(*resultType);
  }
  if (!directTask)
    for (const CopyOut &copyOut : copyOuts)
      callResultTypes.push_back(copyOut.formalType);
  SmallVector<Value> callResults;
  if (auto importID = op->getAttrOfType<IntegerAttr>("obelisk.dpi.import_id")) {
    if (operands.empty())
      return emitError(location) << "DPI call is missing its runtime context",
             failure();
    Value runtimeContext = operands.front();
    operands.erase(operands.begin());
    SmallVector<Attribute> signature(dpiOperandABI);
    if (hasFunctionResult) {
      Type resultType = callResultTypes.front();
      std::optional<unsigned> width = getDPITransportWidth(resultType);
      if (!width)
        return emitError(location)
                   << "DPI function result has no fixed transport width",
               failure();
      auto semanticResult = op->getAttrOfType<TypeAttr>("semantic_type");
      if (!semanticResult)
        return emitError(location)
                   << "DPI function result has no semantic ABI type",
               failure();
      FailureOr<DPIABIKind> resultCategory =
          getDPIABIKind(semanticResult.getValue(), location);
      if (failed(resultCategory))
        return failure();
      signature.push_back(sim::DPIABIAttr::get(
          builder.getContext(), static_cast<sim::DPIABIKind>(*resultCategory),
          sim::DPIArgumentDirection::Result, *width, isDPIFourState(resultType),
          isSignedSemanticType(semanticResult.getValue())));
    }
    for (const CopyOut &copyOut : copyOuts) {
      std::optional<unsigned> width = getDPITransportWidth(copyOut.formalType);
      if (!width)
        return emitError(location)
                   << "DPI copy-out has no fixed transport width",
               failure();
      signature.push_back(sim::DPIABIAttr::get(
          builder.getContext(),
          static_cast<sim::DPIABIKind>(copyOut.dpiCategory),
          sim::DPIArgumentDirection::Output, *width,
          isDPIFourState(copyOut.formalType), copyOut.formalSigned));
    }
    FileLineColLoc fileLocation = dyn_cast<FileLineColLoc>(location);
    StringRef sourceFile =
        fileLocation ? fileLocation.getFilename() : StringRef{};
    uint32_t sourceLine = fileLocation ? fileLocation.getLine() : 0;
    uint32_t sourceColumn = fileLocation ? fileLocation.getColumn() : 0;
    SmallVector<Type> dpiResultTypes(callResultTypes);
    dpiResultTypes.push_back(runtime::StatusType::get(builder.getContext()));
    auto call = sim::SimDPICallOp::create(
        builder, location, dpiResultTypes,
        builder.getI32IntegerAttr(
            static_cast<uint32_t>(importID.getValue().getZExtValue())),
        op->getAttrOfType<StringAttr>("obelisk.dpi.c_identifier"),
        op->getAttrOfType<IntegerAttr>("obelisk.dpi.scope_id"),
        builder.getArrayAttr(signature),
        op->getAttrOfType<BoolAttr>("obelisk.dpi.is_pure"),
        op->getAttrOfType<BoolAttr>("obelisk.dpi.is_context"),
        op->getAttrOfType<BoolAttr>("obelisk.dpi.is_task"),
        builder.getStringAttr(sourceFile),
        builder.getI32IntegerAttr(sourceLine),
        builder.getI32IntegerAttr(sourceColumn), runtimeContext, operands);
    sim::SimStatusCheckOp::create(builder, location, call.getResults().back());
    llvm::append_range(callResults, call.getResults().drop_back());
  } else if (!directTask) {
    if (!virtualCallees) {
      auto call =
          sim::SimCallOp::create(builder, location, callResultTypes, callee,
                                 operands, ArrayAttr{}, ArrayAttr{});
      llvm::append_range(callResults, call.getResults());
    } else {
      Block *merge = addBlock();
      for (Type type : callResultTypes)
        merge->addArgument(type, location);
      for (Attribute candidateAttr : virtualCallees) {
        auto candidate = cast<DictionaryAttr>(candidateAttr);
        Block *matched = addBlock();
        Block *next = addBlock();
        Value expected =
            arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                      candidate.getAs<IntegerAttr>("scope"));
        Value equal =
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                  virtualScope, expected);
        cf::CondBranchOp::create(builder, location, equal, matched,
                                 ValueRange{}, next, ValueRange{});
        setCurrent(matched);
        SmallVector<Value> candidateOperands(operands);
        for (Attribute captureAttr : candidate.getAs<ArrayAttr>("captures")) {
          StringRef path = cast<StringAttr>(captureAttr).getValue();
          Value capture = values.lookup(path);
          if (!capture)
            return emitError(location)
                       << "virtual-interface callee capture has no binding: "
                       << path,
                   failure();
          candidateOperands.push_back(capture);
        }
        for (Attribute readAttr : candidate.getAs<ArrayAttr>("read_captures")) {
          Value capture = values.lookup(cast<StringAttr>(readAttr).getValue());
          if (capture)
            recordSensitivity(capture);
        }
        auto selected =
            sim::SimCallOp::create(builder, location, callResultTypes,
                                   candidate.getAs<FlatSymbolRefAttr>("callee"),
                                   candidateOperands, ArrayAttr{}, ArrayAttr{});
        cf::BranchOp::create(builder, location, merge, selected.getResults());
        setCurrent(next);
      }
      if (failed(emitRuntimeFatal(
              location,
              "virtual interface call used a null or invalid handle.")))
        return failure();
      setCurrent(merge);
      llvm::append_range(callResults, merge->getArguments());
    }
  } else {
    Block *continuation = addBlock();
    if (!virtualCallees) {
      sim::SimTaskCallOp::create(builder, location, callee, operands,
                                 builder.getI64IntegerAttr(operands.size()),
                                 sim::ContinuationSiteAttr{}, continuation);
    } else {
      for (Attribute candidateAttr : virtualCallees) {
        auto candidate = cast<DictionaryAttr>(candidateAttr);
        Block *matched = addBlock();
        Block *next = addBlock();
        Value expected =
            arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                      candidate.getAs<IntegerAttr>("scope"));
        Value equal =
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                  virtualScope, expected);
        cf::CondBranchOp::create(builder, location, equal, matched,
                                 ValueRange{}, next, ValueRange{});
        setCurrent(matched);
        SmallVector<Value> candidateOperands(operands);
        for (Attribute captureAttr : candidate.getAs<ArrayAttr>("captures")) {
          StringRef path = cast<StringAttr>(captureAttr).getValue();
          Value capture = values.lookup(path);
          if (!capture)
            return emitError(location)
                       << "virtual-interface task capture has no binding: "
                       << path,
                   failure();
          candidateOperands.push_back(capture);
        }
        for (Attribute readAttr : candidate.getAs<ArrayAttr>("read_captures")) {
          Value capture = values.lookup(cast<StringAttr>(readAttr).getValue());
          if (capture)
            recordSensitivity(capture);
        }
        sim::SimTaskCallOp::create(
            builder, location, candidate.getAs<FlatSymbolRefAttr>("callee"),
            candidateOperands,
            builder.getI64IntegerAttr(candidateOperands.size()),
            sim::ContinuationSiteAttr{}, continuation);
        setCurrent(next);
      }
      if (failed(emitRuntimeFatal(
              location,
              "virtual interface task call used a null or invalid handle.")))
        return failure();
    }
    setCurrent(continuation);
  }
  if (!directTask) {
    Value requested = sim::SimTerminationRequestedOp::create(
        builder, location, builder.getI1Type(),
        function.getBody().front().getArgument(0));
    Block *terminate = addBlock();
    Block *resume = addBlock();
    cf::CondBranchOp::create(builder, location, requested, terminate,
                             ValueRange{}, resume, ValueRange{});
    setCurrent(terminate);
    if (function.getEntryKind() == sim::EntryKind::Observer) {
      SmallVector<Value> results;
      for (Type type : function.getFunctionType().getResults()) {
        Value result = createDefaultValue(builder, location, type);
        if (!result) {
          function.emitError(
              "cannot materialize a termination result for observer");
          return failure();
        }
        results.push_back(result);
      }
      sim::SimReturnOp::create(builder, location, results);
    } else if (failed(emitFunctionReturn(location, std::nullopt, false))) {
      return failure();
    }
    setCurrent(resume);
  }
  if (!directTask) {
    for (auto [index, copyOut] : llvm::enumerate(copyOuts)) {
      Type destinationType = getReferenceElementType(copyOut.destination);
      if (!destinationType)
        return failure();
      FailureOr<Value> converted = convert(
          callResults[index + (hasFunctionResult ? 1 : 0)], destinationType,
          copyOut.formalSigned, location, copyOut.destinationSigned);
      if (failed(converted))
        return failure();
      if (failed(storeReference(copyOut.destination, *converted, location)))
        return failure();
    }
  } else {
    for (const CopyOut &copyOut : copyOuts) {
      if (copyOut.taskDestination == copyOut.destination)
        continue;
      FailureOr<Value> copied =
          loadReference(copyOut.taskDestination, location);
      Type destinationType = getReferenceElementType(copyOut.destination);
      if (failed(copied) || !destinationType)
        return failure();
      FailureOr<Value> converted =
          convert(*copied, destinationType, copyOut.formalSigned, location,
                  copyOut.destinationSigned);
      if (failed(converted) ||
          failed(storeReference(copyOut.destination, *converted, location)))
        return failure();
    }
  }
  if (hasFunctionResult)
    return callResults.front();
  return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                   builder.getBoolAttr(false))
      .getResult();
}

FailureOr<Value>
UnitLowering::lowerNewClass(semantic::SVNewClassExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (children.empty()) {
    sim::ClassHandleType receiverType;
    Value receiver;
    sim::SimClassDeclOp declaration;
    if (op.getIsSuperClass()) {
      if (!thisObject || !(receiverType = dyn_cast<sim::ClassHandleType>(
                               thisObject.getType()))) {
        emitError(location) << "implicit super.new has no current this object";
        return failure();
      }
      sim::SimClassDeclOp derived =
          SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
              function, receiverType.getClassName());
      if (!derived || !derived.getBaseAttr()) {
        emitError(location) << "implicit super.new has no resolved base class";
        return failure();
      }
      declaration = SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
          function, derived.getBaseAttr());
      receiverType = sim::ClassHandleType::get(function.getContext(),
                                               derived.getBaseAttr());
      receiver = sim::SimClassCastOp::create(builder, location, receiverType,
                                             thisObject);
    } else {
      FailureOr<Type> resultType = getNormalizedSemanticType(op);
      if (failed(resultType) ||
          !(receiverType = dyn_cast<sim::ClassHandleType>(*resultType)))
        return failure();
      declaration = SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
          function, receiverType.getClassName());
      receiver = sim::SimClassAllocOp::create(
          builder, location, receiverType,
          function.getBody().front().getArgument(0));
      if (failed(initializeObjectRandomStream(receiver, location)))
        return failure();
    }
    auto constructorName = declaration ? declaration->getAttrOfType<StringAttr>(
                                             "obelisk_sim.implicit_constructor")
                                       : StringAttr{};
    if (!constructorName) {
      emitError(location)
          << "implicit class constructor has no executable implementation";
      return failure();
    }
    FlatSymbolRefAttr constructor = FlatSymbolRefAttr::get(
        function.getContext(), constructorName.getValue());
    sim::SimClassDirectCallOp::create(builder, location, TypeRange{},
                                      constructor, receiver, ValueRange{});
    if (!op.getIsSuperClass())
      return receiver;
    return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                     builder.getBoolAttr(false))
        .getResult();
  }
  if (children.size() != 1) {
    unsupported(op) << " (constructor inventory)";
    return failure();
  }
  auto call = dyn_cast<semantic::SVCallExpressionOp>(children.front());
  if (call && isMailboxCall(call) && call.getCalleeName() == "new") {
    SmallVector<Operation *> actuals = getChildren(call);
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    auto mailbox = succeeded(resultType)
                       ? dyn_cast<sim::MailboxType>(*resultType)
                       : sim::MailboxType{};
    if (actuals.size() != 1 || failed(resultType) || !mailbox)
      return emitError(location)
                 << "mailbox constructor requires one resolved bound",
             failure();
    FailureOr<Value> bound = lowerExpression(actuals.front());
    bound = succeeded(bound)
                ? convert(*bound, builder.getI64Type(),
                          isSignedNode(actuals.front()), location, true)
                : FailureOr<Value>(failure());
    FailureOr<ContainerElementDescriptor> descriptor =
        describeContainerElement(mailbox.getElementType(), location);
    if (failed(bound) || failed(descriptor))
      return failure();
    return sim::SimMailboxCreateOp::create(
               builder, location, mailbox, *bound, descriptor->typeID,
               descriptor->kind, descriptor->flags, descriptor->valueSize,
               descriptor->alignment, descriptor->bitWidth,
               builder.getDenseI64ArrayAttr(descriptor->traceOffsets),
               builder.getDenseI32ArrayAttr(descriptor->traceKinds))
        .getResult();
  }
  if (call && isWeakReferenceCall(call) && call.getCalleeName() == "new") {
    SmallVector<Operation *> actuals = getChildren(call);
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    if (actuals.size() != 1 || failed(resultType) ||
        !isa<sim::ClassHandleType>(*resultType))
      return emitError(location)
                 << "weak_reference constructor requires one class handle",
             failure();
    FailureOr<Value> referent = lowerExpression(actuals.front());
    if (failed(referent) || !isa<sim::ClassHandleType>((*referent).getType()))
      return failure();
    return sim::SimWeakCreateOp::create(
               builder, location, *resultType,
               function.getBody().front().getArgument(0), *referent)
        .getResult();
  }
  auto callee = call ? call->getAttrOfType<FlatSymbolRefAttr>(calleeAttrName)
                     : FlatSymbolRefAttr{};
  auto formals = call ? call->getAttrOfType<ArrayAttr>(calleeFormalsAttrName)
                      : ArrayAttr{};
  if (!call || !callee || !formals) {
    unsupported(op) << " (unresolved constructor"
                    << (!call ? ": missing call" : "")
                    << (call && !callee ? ": missing callee" : "")
                    << (call && !formals ? ": missing formals" : "") << ")";
    return failure();
  }

  Value receiver;
  if (op.getIsSuperClass()) {
    if (!thisObject) {
      emitError(location) << "super.new has no current this object";
      return failure();
    }
    receiver = thisObject;
  } else {
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    if (failed(resultType) || !isa<sim::ClassHandleType>(*resultType))
      return failure();
    receiver =
        sim::SimClassAllocOp::create(builder, location, *resultType,
                                     function.getBody().front().getArgument(0));
    if (failed(initializeObjectRandomStream(receiver, location)))
      return failure();
  }

  SmallVector<Operation *> actuals = getChildren(call);
  if (actuals.size() != formals.size()) {
    emitError(location)
        << "constructor has no complete frozen formal inventory";
    return failure();
  }
  struct ConstructorCopyOut {
    Value destination;
    Type formalType;
    bool formalSigned;
    bool destinationSigned;
  };
  SmallVector<Value> arguments;
  SmallVector<ConstructorCopyOut> copyOuts;
  for (auto [actual, formalAttr] : llvm::zip_equal(actuals, formals)) {
    auto formal = cast<DictionaryAttr>(formalAttr);
    auto direction = static_cast<semantic::SVArgumentDirection>(
        formal.getAs<IntegerAttr>("direction").getInt());
    Type formalType = formal.getAs<TypeAttr>("type").getValue();
    bool formalSigned = formal.getAs<BoolAttr>("is_signed").getValue();
    if (direction == semantic::SVArgumentDirection::In) {
      FailureOr<Value> argument = lowerExpression(actual);
      if (failed(argument))
        return failure();
      FailureOr<Value> converted = convert(
          *argument, formalType, isSignedNode(actual), location, formalSigned);
      if (failed(converted))
        return failure();
      arguments.push_back(cloneSequentialValue(*converted, location));
      continue;
    }

    Operation *destination = actual;
    if (auto assignment =
            dyn_cast<semantic::SVAssignmentExpressionOp>(actual)) {
      SmallVector<Operation *> outputChildren = getChildren(assignment);
      if (outputChildren.size() == 2) {
        Operation *placeholder = outputChildren[1];
        while (isa<semantic::SVConversionExpressionOp>(placeholder)) {
          SmallVector<Operation *> converted = getChildren(placeholder);
          if (converted.size() != 1)
            break;
          placeholder = converted.front();
        }
        if (isa<semantic::SVEmptyArgumentExpressionOp>(placeholder))
          destination = outputChildren.front();
      }
    }
    FailureOr<Value> destinationRef = lowerExpression(destination, true);
    if (failed(destinationRef))
      return failure();
    Type destinationType;
    if (auto ref = dyn_cast<sim::RefType>((*destinationRef).getType()))
      destinationType = ref.getElementType();
    else if (auto ref =
                 dyn_cast<sim::ManagedRefType>((*destinationRef).getType()))
      destinationType = ref.getElementType();
    else if (auto ref =
                 dyn_cast<sim::ArgumentRefType>((*destinationRef).getType()))
      destinationType = ref.getElementType();
    else if (auto ref =
                 dyn_cast<sim::ReferencePathType>((*destinationRef).getType()))
      destinationType = ref.getElementType();
    else
      return emitError(location)
                 << "constructor output, inout, and ref actuals must be "
                    "variable references",
             failure();

    if (direction == semantic::SVArgumentDirection::Ref) {
      FailureOr<Value> argument =
          toArgumentReference(*destinationRef, formalType, location);
      if (failed(argument))
        return emitError(location)
                   << "constructor ref actual type must exactly match the "
                      "formal type",
               failure();
      arguments.push_back(*argument);
      continue;
    }

    Value initial;
    if (direction == semantic::SVArgumentDirection::Out) {
      initial = createDefaultValue(builder, location, formalType);
      if (!initial)
        return emitError(location)
                   << "cannot materialize a constructor output-formal "
                      "default for type "
                   << formalType,
               failure();
    } else {
      FailureOr<Value> loaded = loadReference(*destinationRef, location);
      if (failed(loaded))
        return failure();
      if (isa<sim::RefType>((*destinationRef).getType()))
        recordSensitivity(*destinationRef);
      FailureOr<Value> converted =
          convert(*loaded, formalType, isSignedNode(destination), location,
                  formalSigned);
      if (failed(converted))
        return failure();
      initial = *converted;
    }
    arguments.push_back(cloneSequentialValue(initial, location));
    copyOuts.push_back(
        {*destinationRef, formalType, formalSigned, isSignedNode(destination)});
  }
  if (auto captures = call->getAttrOfType<ArrayAttr>(calleeCapturesAttrName))
    for (Attribute captureAttr : captures) {
      StringRef path = cast<StringAttr>(captureAttr).getValue();
      Value capture = values.lookup(path);
      if (!capture) {
        emitError(location)
            << "constructor capture has no frozen local binding: " << path;
        return failure();
      }
      arguments.push_back(capture);
    }
  auto constructor =
      SymbolTable::lookupNearestSymbolFrom<sim::SimFuncOp>(function, callee);
  if (!constructor || constructor.getFunctionType().getNumInputs() < 2 ||
      !isa<sim::ClassHandleType>(constructor.getFunctionType().getInput(1))) {
    emitError(location) << "constructor implementation has no this parameter";
    return failure();
  }
  FailureOr<Value> adjustedReceiver = convert(
      receiver, constructor.getFunctionType().getInput(1), false, location);
  if (failed(adjustedReceiver))
    return failure();
  SmallVector<Type> resultTypes;
  for (const ConstructorCopyOut &copyOut : copyOuts)
    resultTypes.push_back(copyOut.formalType);
  if (constructor.getFunctionType().getNumResults() != resultTypes.size()) {
    emitError(location)
        << "constructor implementation has inconsistent copy-out results";
    return failure();
  }
  auto invocation = sim::SimClassDirectCallOp::create(
      builder, location, resultTypes, callee, *adjustedReceiver, arguments);
  for (auto [result, copyOut] :
       llvm::zip_equal(invocation.getResults(), copyOuts)) {
    Type destinationType = getReferenceElementType(copyOut.destination);
    if (!destinationType)
      return failure();
    FailureOr<Value> converted =
        convert(result, destinationType, copyOut.formalSigned, location,
                copyOut.destinationSigned);
    if (failed(converted))
      return failure();
    if (failed(storeReference(copyOut.destination, *converted, location)))
      return failure();
  }
  if (!op.getIsSuperClass())
    return receiver;
  return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                   builder.getBoolAttr(false))
      .getResult();
}

LogicalResult UnitLowering::initializeObjectRandomStream(Value object,
                                                         Location location) {
  auto objectType = dyn_cast<sim::ClassHandleType>(object.getType());
  if (!objectType)
    return failure();
  sim::SimClassDeclOp declaration =
      SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
          function, objectType.getClassName());
  while (declaration &&
         !declaration->hasAttr("obelisk_sim.random_state_field")) {
    if (!declaration.getBaseAttr())
      break;
    declaration = SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
        function, declaration.getBaseAttr());
  }
  auto stateField = declaration ? declaration->getAttrOfType<FlatSymbolRefAttr>(
                                      "obelisk_sim.random_state_field")
                                : FlatSymbolRefAttr{};
  auto incrementField = declaration
                            ? declaration->getAttrOfType<FlatSymbolRefAttr>(
                                  "obelisk_sim.random_increment_field")
                            : FlatSymbolRefAttr{};
  if (!declaration || !stateField || !incrementField) {
    emitError(location) << "class hierarchy has no inline random stream";
    return failure();
  }

  Value context = function.getBody().front().getArgument(0);
  Value state = sim::SimRandomNextOp::create(builder, location,
                                             builder.getI64Type(), context);
  Value increment = sim::SimRandomNextOp::create(builder, location,
                                                 builder.getI64Type(), context);
  Value one = arith::ConstantOp::create(builder, location, builder.getI64Type(),
                                        builder.getI64IntegerAttr(1));
  increment = arith::OrIOp::create(builder, location, increment, one);
  Type referenceType = sim::ManagedRefType::get(
      function.getContext(), builder.getI64Type(), objectType.getClassName());
  Value stateReference = sim::SimClassFieldRefOp::create(
      builder, location, referenceType, object, stateField);
  Value incrementReference = sim::SimClassFieldRefOp::create(
      builder, location, referenceType, object, incrementField);
  sim::SimManagedStoreOp::create(builder, location, state, stateReference);
  sim::SimManagedStoreOp::create(builder, location, increment,
                                 incrementReference);
  return success();
}

} // namespace obelisk::simlowering
