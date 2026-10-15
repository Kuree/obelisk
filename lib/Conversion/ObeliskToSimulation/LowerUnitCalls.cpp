//===- LowerUnitCalls.cpp - Lower function and class calls ------------===//

#include "LowerUnit.h"
#include "PrepareRandomFunctions.h"
#include "obelisk/Dialect/DPI/DPIDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"

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

bool isSemaphoreCall(semantic::SVCallExpressionOp op) {
  auto path = op->getAttrOfType<StringAttr>("referenced_path");
  return path && path.getValue().starts_with("std::semaphore::");
}

} // namespace

Operation *UnitLowering::getCopyOutActual(Operation *actual) {
  auto assignment = dyn_cast<semantic::SVAssignmentExpressionOp>(actual);
  if (!assignment)
    return actual;
  SmallVector<Operation *> children = getChildren(assignment);
  if (children.size() != 2)
    return actual;
  Operation *placeholder = children[1];
  while (isa<semantic::SVConversionExpressionOp>(placeholder)) {
    SmallVector<Operation *> converted = getChildren(placeholder);
    if (converted.size() != 1)
      break;
    placeholder = converted.front();
  }
  return isa<semantic::SVEmptyArgumentExpressionOp>(placeholder)
             ? children.front()
             : actual;
}

FailureOr<Value> UnitLowering::copyInFromActual(Operation *actual,
                                                Type formalType,
                                                bool formalSigned,
                                                Location location) {
  FailureOr<Value> value = lowerExpression(actual);
  if (failed(value))
    return failure();
  return convert(*value, formalType, isSignedNode(actual), location,
                 formalSigned);
}

LogicalResult UnitLowering::copyOutToActual(Operation *actual,
                                            Value scopeObject, Value value,
                                            bool formalSigned,
                                            Location location) {
  // IEEE 1800-2023 4.9.7: copy-out on return behaves as a blocking
  // assignment, so the actual is addressed on return. A subroutine that
  // changes an index of its own actual writes the element it selects then.
  Value savedThisObject = thisObject;
  if (scopeObject)
    thisObject = scopeObject;
  llvm::scope_exit restoreThisObject([&] { thisObject = savedThisObject; });
  FailureOr<Value> destination = lowerExpression(actual, /*lvalue=*/true);
  if (failed(destination))
    return failure();
  Type destinationType = getReferenceElementType(*destination);
  if (!destinationType)
    return emitError(location)
               << "output and inout actuals must be variable references",
           failure();
  FailureOr<Value> converted = convert(value, destinationType, formalSigned,
                                       location, isSignedNode(actual));
  if (failed(converted))
    return failure();
  return storeReference(*destination, *converted, location);
}

FailureOr<Value> UnitLowering::lowerCall(semantic::SVCallExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  std::optional<StringRef> referencedPath = op.getReferencedPath();
  if (referencedPath && referencedPath->starts_with("std::semaphore::")) {
    StringRef method = op.getCalleeName();
    if (children.empty())
      return emitError(location) << "semaphore method has no receiver",
             failure();
    FailureOr<Value> receiver = lowerExpression(children.front());
    if (failed(receiver) || !isa<sim::SemaphoreType>((*receiver).getType()))
      return failure();
    if (children.size() != 2)
      return emitError(location)
                 << "semaphore::" << method << " requires one key count",
             failure();
    FailureOr<Value> keys = lowerExpression(children[1]);
    keys = succeeded(keys) ? convert(*keys, builder.getI32Type(),
                                     isSignedNode(children[1]), location, true)
                           : FailureOr<Value>(failure());
    if (failed(keys))
      return failure();
    if (method == "put") {
      sim::SimSemaphorePutOp::create(builder, location, *receiver, *keys);
      return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                       builder.getBoolAttr(false))
          .getResult();
    }
    if (method == "try_get") {
      Value success = sim::SimSemaphoreTryGetOp::create(builder, location,
                                                        *receiver, *keys);
      FailureOr<Type> resultType = getNormalizedSemanticType(op);
      return failed(resultType)
                 ? FailureOr<Value>(failure())
                 : convert(success, *resultType, false, location, true);
    }
    if (method == "get") {
      Block *attempt = addBlock();
      attempt->addArgument((*receiver).getType(), location);
      attempt->addArgument(builder.getI32Type(), location);
      Block *wait = addBlock();
      wait->addArgument((*receiver).getType(), location);
      wait->addArgument(builder.getI32Type(), location);
      Block *done = addBlock();
      cf::BranchOp::create(builder, location, attempt,
                           ValueRange{*receiver, *keys});
      setCurrent(attempt);
      Value success = sim::SimSemaphoreTryGetOp::create(
          builder, location, attempt->getArgument(0), attempt->getArgument(1));
      cf::CondBranchOp::create(builder, location, success, done, ValueRange{},
                               wait, attempt->getArguments());
      setCurrent(wait);
      sim::SimSuspendSemaphoreOp::create(
          builder, location, wait->getArgument(0), wait->getArgument(1),
          ValueRange{}, schedule::ContinuationSiteAttr{},
          sim::EventRegionAttr{}, done);
      setCurrent(done);
      return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                       builder.getBoolAttr(false))
          .getResult();
    }
    return emitError(location) << "unsupported semaphore method " << method,
           failure();
  }
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
    bool untyped = isa<sim::BoxType>(mailbox.getElementType());
    auto boxValue = [&](Value value, Operation *source) -> FailureOr<Value> {
      FailureOr<ContainerElementDescriptor> descriptor =
          describeContainerElement(value.getType(), location);
      if (failed(descriptor))
        return failure();
      if (auto semanticType = source->getAttrOfType<TypeAttr>("semantic_type"))
        descriptor->typeID = getStableTypeID(semanticType.getValue());
      Type arrayType =
          sim::DynamicArrayType::get(function.getContext(), value.getType());
      Value one = arith::ConstantIntOp::create(builder, location, 1, 64);
      Value zero = arith::ConstantIntOp::create(builder, location, 0, 64);
      Value array = sim::SimContainerCreateOp::create(
          builder, location, arrayType, one, descriptor->typeID,
          descriptor->kind, descriptor->flags, descriptor->valueSize,
          descriptor->alignment, descriptor->bitWidth,
          builder.getDenseI64ArrayAttr(descriptor->traceOffsets),
          builder.getDenseI32ArrayAttr(descriptor->traceKinds),
          sim::ContainerKind::DynamicArray, 0);
      sim::SimContainerWriteOp::create(builder, location, array, zero,
                                       cloneSequentialValue(value, location));
      return sim::SimBoxPackOp::create(builder, location,
                                       sim::BoxType::get(function.getContext()),
                                       array)
          .getResult();
    };
    auto boxMatches = [&](Value box, Type type,
                          Operation *destination) -> FailureOr<Value> {
      FailureOr<ContainerElementDescriptor> descriptor =
          describeContainerElement(type, location);
      if (failed(descriptor))
        return failure();
      if (auto semanticType =
              destination->getAttrOfType<TypeAttr>("semantic_type"))
        descriptor->typeID = getStableTypeID(semanticType.getValue());
      return sim::SimBoxIsTypeOp::create(builder, location, builder.getI1Type(),
                                         box, descriptor->typeID)
          .getResult();
    };
    auto unboxValue = [&](Value box, Type type) -> Value {
      Type arrayType = sim::DynamicArrayType::get(function.getContext(), type);
      Value array =
          sim::SimBoxCastOp::create(builder, location, arrayType, box);
      Value zero = arith::ConstantIntOp::create(builder, location, 0, 64);
      return sim::SimContainerReadOp::create(builder, location, type, array,
                                             zero);
    };
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
      value = succeeded(value)
                  ? (untyped ? boxValue(*value, children[1])
                             : convert(*value, mailbox.getElementType(),
                                       isSignedNode(children[1]), location))
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
      value = succeeded(value)
                  ? (untyped ? boxValue(*value, children[1])
                             : convert(*value, mailbox.getElementType(),
                                       isSignedNode(children[1]), location))
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
          schedule::ContinuationSiteAttr{}, sim::EventRegionAttr{}, retry);
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
      if (untyped) {
        auto peek = sim::SimMailboxTryPeekOp::create(
            builder, location,
            TypeRange{builder.getI1Type(), mailbox.getElementType()},
            *receiver);
        FailureOr<Value> matches =
            boxMatches(peek.getValue(), destinationType, children[1]);
        if (failed(matches))
          return failure();
        Value accepted = arith::AndIOp::create(builder, location,
                                               peek.getSuccess(), *matches);
        Value zero = arith::ConstantIntOp::create(builder, location, 0, 32);
        Value one = arith::ConstantIntOp::create(builder, location, 1, 32);
        Value mismatch =
            arith::ConstantIntOp::create(builder, location, -1, 32);
        Value presentStatus =
            arith::SelectOp::create(builder, location, *matches, one, mismatch);
        Value status = arith::SelectOp::create(
            builder, location, peek.getSuccess(), presentStatus, zero);
        Block *store = addBlock();
        Block *resume = addBlock();
        cf::CondBranchOp::create(builder, location, accepted, store,
                                 ValueRange{}, resume, ValueRange{});
        setCurrent(store);
        Value box = peek.getValue();
        if (method == "try_get") {
          auto get = sim::SimMailboxTryGetOp::create(
              builder, location,
              TypeRange{builder.getI1Type(), mailbox.getElementType()},
              *receiver);
          box = get.getValue();
        }
        Value value = unboxValue(box, destinationType);
        if (failed(storeReference(
                *destination, cloneSequentialValue(value, location), location)))
          return failure();
        cf::BranchOp::create(builder, location, resume);
        setCurrent(resume);
        FailureOr<Type> resultType = getNormalizedSemanticType(op);
        return failed(resultType)
                   ? FailureOr<Value>(failure())
                   : convert(status, *resultType, true, location, true);
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
      if (untyped) {
        Block *retry = addBlock();
        retry->addArgument(mailbox, location);
        retry->addArgument((*destination).getType(), location);
        Block *wait = addBlock();
        wait->addArgument(mailbox, location);
        wait->addArgument((*destination).getType(), location);
        Block *check = addBlock();
        check->addArgument(mailbox.getElementType(), location);
        check->addArgument(mailbox, location);
        check->addArgument((*destination).getType(), location);
        Block *store = addBlock();
        store->addArgument(mailbox.getElementType(), location);
        store->addArgument(mailbox, location);
        store->addArgument((*destination).getType(), location);
        Block *mismatch = addBlock();
        Block *done = addBlock();
        cf::BranchOp::create(builder, location, retry,
                             ValueRange{*receiver, *destination});
        setCurrent(retry);
        auto peek = sim::SimMailboxTryPeekOp::create(
            builder, location,
            TypeRange{builder.getI1Type(), mailbox.getElementType()},
            retry->getArgument(0));
        cf::CondBranchOp::create(builder, location, peek.getSuccess(), check,
                                 ValueRange{peek.getValue(),
                                            retry->getArgument(0),
                                            retry->getArgument(1)},
                                 wait, retry->getArguments());
        setCurrent(wait);
        sim::SimSuspendMailboxOp::create(
            builder, location, wait->getArgument(0),
            sim::MailboxWaitKind::NotEmpty, wait->getArguments(),
            schedule::ContinuationSiteAttr{}, sim::EventRegionAttr{}, retry);
        setCurrent(check);
        FailureOr<Value> matches =
            boxMatches(check->getArgument(0), destinationType, children[1]);
        if (failed(matches))
          return failure();
        cf::CondBranchOp::create(builder, location, *matches, store,
                                 check->getArguments(), mismatch, ValueRange{});
        setCurrent(mismatch);
        sim::SimErrorOp::create(builder, location,
                                function.getBody().front().getArgument(0));
        cf::BranchOp::create(builder, location, done);
        setCurrent(store);
        Value box = store->getArgument(0);
        if (method == "get") {
          auto get = sim::SimMailboxTryGetOp::create(
              builder, location,
              TypeRange{builder.getI1Type(), mailbox.getElementType()},
              store->getArgument(1));
          box = get.getValue();
        }
        Value value = unboxValue(box, destinationType);
        if (failed(storeReference(store->getArgument(2),
                                  cloneSequentialValue(value, location),
                                  location)))
          return failure();
        cf::BranchOp::create(builder, location, done);
        setCurrent(done);
        return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                         builder.getBoolAttr(false))
            .getResult();
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
          schedule::ContinuationSiteAttr{}, sim::EventRegionAttr{}, retry);
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
            sim::RadixAttr::get(builder.getContext(), sim::Radix::Hex),
            builder.getBoolAttr(false));
        Value separator = sim::SimStringLiteralOp::create(
            builder, location, stringType, builder.getStringAttr(":"));
        Value incrementText = sim::SimStringFormatIntegerOp::create(
            builder, location, stringType, state.getIncrement(),
            sim::RadixAttr::get(builder.getContext(), sim::Radix::Hex),
            builder.getBoolAttr(false));
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
          builder.getStringAttr(""), static_cast<uint32_t>('x'), 0);
      auto incrementField = sim::SimStringScanFieldOp::create(
          builder, location, TypeRange{stringType, i32, i32}, *argument,
          stateField.getNextCursor(), builder.getStringAttr(":"),
          static_cast<uint32_t>('x'), 0);
      Value parsedState = sim::SimStringParseIntegerOp::create(
          builder, location, i64, stateField.getField(),
          sim::RadixAttr::get(builder.getContext(), sim::Radix::Hex));
      Value parsedIncrement = sim::SimStringParseIntegerOp::create(
          builder, location, i64, incrementField.getField(),
          sim::RadixAttr::get(builder.getContext(), sim::Radix::Hex));
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
                                     schedule::ContinuationSiteAttr{},
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
          schedule::ContinuationSiteAttr{}, continuation);
      setCurrent(continuation);
      return dummyResult();
    }
  }
  if (sim::has<sim::metadata::Randomize>(op) ||
      sim::has<sim::metadata::RandomizeDispatch>(op) ||
      sim::has<sim::metadata::RandomizeNestedDispatch>(op))
    return lowerRandomize(op);
  StringRef covergroupMethod = op.getCalleeName();
  if ((covergroupMethod == "sample" || covergroupMethod == "start" ||
       covergroupMethod == "stop" || covergroupMethod == "set_inst_name" ||
       covergroupMethod == "get_inst_coverage" ||
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
      !children.empty() && !sim::has<sim::metadata::RandMode>(op) &&
      !sim::has<sim::metadata::ConstraintMode>(op)) {
    Operation *receiverNode =
        op.getHasIteratorExpression() ? children.back() : children.front();
    FailureOr<Type> receiverType = getNormalizedSemanticType(receiverNode);
    stringBuiltin =
        succeeded(receiverType) && isa<sim::StringType>(*receiverType);
    containerBuiltin =
        succeeded(receiverType) &&
        isa<sim::DynamicArrayType, sim::QueueType, sim::UnpackedArrayType>(
            *receiverType);
    associativeBuiltin =
        succeeded(receiverType) && isa<sim::AssocArrayType>(*receiverType);
  }
  if (sim::has<sim::metadata::RandMode>(op) ||
      (op.getIsSystemCall() && op.getCalleeName() == "rand_mode")) {
    if (children.empty() || children.size() > 2) {
      emitError(location) << "rand_mode expects a receiver and optional on/off "
                             "argument";
      return failure();
    }
    auto propertyIndexAttr = sim::get<sim::metadata::RandModeProperty>(op);
    auto staticStorageAttr = sim::get<sim::metadata::RandModeStaticStorage>(op);
    auto staticDispatchAttr =
        sim::get<sim::metadata::RandModeStaticDispatch>(op);
    auto arrayTypeAttr = sim::get<sim::metadata::RandModeArrayType>(op);
    auto arrayModeField = sim::get<sim::metadata::RandModeArrayField>(op);
    auto arrayModeStorage = sim::get<sim::metadata::RandModeArrayStorage>(op);
    auto structureDepth = sim::get<sim::metadata::RandModeStructDepth>(op);
    auto selectionMask = sim::get<sim::metadata::RandModeSelectionMask>(op);
    auto ancestorMask = sim::get<sim::metadata::RandModeAncestorMask>(op);
    auto familyMask = sim::get<sim::metadata::RandModeFamilyMask>(op);
    auto structureElementFields =
        sim::get<sim::metadata::RandModeStructElementFields>(op);
    if (structureDepth || selectionMask || ancestorMask || familyMask) {
      if (!structureDepth || structureDepth.getValue().isNegative() ||
          structureDepth.getValue().getActiveBits() > 32 || !selectionMask ||
          !ancestorMask || !familyMask ||
          selectionMask.getValue().getBitWidth() != 64 ||
          ancestorMask.getValue().getBitWidth() != 64 ||
          familyMask.getValue().getBitWidth() != 64 ||
          (!propertyIndexAttr && !staticStorageAttr)) {
        emitError(location) << "structure rand_mode metadata is malformed";
        return failure();
      }
    }
    if (static_cast<bool>(arrayTypeAttr) !=
        static_cast<bool>(arrayModeField || arrayModeStorage)) {
      emitError(location) << "array rand_mode metadata is malformed";
      return failure();
    }
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
    SmallVector<Operation *> arrayIndices;
    while (auto selection =
               dyn_cast<semantic::SVElementSelectExpressionOp>(receiverNode)) {
      auto operands = getChildren(selection);
      if (!arrayTypeAttr || operands.size() != 2) {
        emitError(location) << "selected rand_mode has no array mode inventory";
        return failure();
      }
      arrayIndices.push_back(operands.back());
      receiverNode = operands.front();
    }
    std::reverse(arrayIndices.begin(), arrayIndices.end());
    if (structureDepth)
      for (uint64_t depth = 0; depth < structureDepth.getValue().getZExtValue();
           ++depth) {
        auto operands = getChildren(receiverNode);
        if (!isa<semantic::SVMemberAccessExpressionOp>(receiverNode) ||
            operands.size() != 1) {
          emitError(location)
              << "structure rand_mode receiver path is malformed";
          return failure();
        }
        receiverNode = operands.front();
      }
    bool implicitThisProperty = false;
    if (propertyIndexAttr || staticStorageAttr) {
      auto member =
          dyn_cast<semantic::SVMemberAccessExpressionOp>(receiverNode);
      auto named = dyn_cast<semantic::SVNamedValueExpressionOp>(receiverNode);
      SmallVector<Operation *> memberChildren =
          member ? getChildren(member) : SmallVector<Operation *>{};
      bool classQualifiedStatic = staticStorageAttr && named;
      implicitThisProperty = propertyIndexAttr && named && thisObject;
      if ((!member && !classQualifiedStatic && !implicitThisProperty) ||
          memberChildren.size() > 1 ||
          (propertyIndexAttr && member && memberChildren.empty())) {
        emitError(location) << "property rand_mode has no object receiver";
        return failure();
      }
      if (classQualifiedStatic || implicitThisProperty ||
          memberChildren.empty())
        receiverNode = nullptr;
      else
        receiverNode = memberChildren.front();
    }
    Value loweredReceiver;
    if (implicitThisProperty) {
      loweredReceiver = thisObject;
    } else if (receiverNode) {
      FailureOr<Value> receiver = lowerExpression(receiverNode);
      if (failed(receiver))
        return failure();
      loweredReceiver = *receiver;
    }
    auto objectType =
        loweredReceiver
            ? dyn_cast<sim::ClassHandleType>(loweredReceiver.getType())
            : sim::ClassHandleType{};
    if ((receiverNode || implicitThisProperty) && !objectType) {
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

    SmallVector<Value> loweredIndices;
    for (Operation *index : arrayIndices) {
      FailureOr<Value> value = lowerExpression(index);
      auto sourceType = index->getAttrOfType<TypeAttr>("semantic_type");
      FailureOr<Value> normalized =
          succeeded(value)
              ? toArrayIndex(*value,
                             sourceType &&
                                 isSignedSemanticType(sourceType.getValue()),
                             location)
              : FailureOr<Value>(failure());
      if (failed(normalized))
        return failure();
      loweredIndices.push_back(*normalized);
    }

    FailureOr<Value> setterEnabled;
    if (children.size() == 2) {
      FailureOr<Value> argument = lowerExpression(children.back());
      setterEnabled = succeeded(argument) ? truthValue(*argument, location)
                                          : FailureOr<Value>(failure());
      if (failed(setterEnabled))
        return failure();
    }

    if (staticStorageAttr && !arrayTypeAttr && !selectionMask) {
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

    if (!staticStorageAttr && !receiverNode && !implicitThisProperty) {
      emitError(location) << "rand_mode receiver is not a class object";
      return failure();
    }
    sim::SimClassDeclOp declaration =
        objectType ? lookupNearestSymbolFrom<sim::SimClassDeclOp>(
                         function, objectType.getClassName())
                   : sim::SimClassDeclOp{};
    while (declaration &&
           !sim::has<sim::metadata::RandomModeField>(declaration)) {
      if (!declaration.getBaseAttr())
        break;
      declaration = lookupNearestSymbolFrom<sim::SimClassDeclOp>(
          function, declaration.getBaseAttr());
    }
    auto modeField = declaration
                         ? sim::get<sim::metadata::RandomModeField>(declaration)
                         : FlatSymbolRefAttr{};
    if (!staticStorageAttr && (!declaration || !modeField)) {
      emitError(location) << "rand_mode receiver has no mode state";
      return failure();
    }
    Value reference;
    if (staticStorageAttr) {
      APInt storage = staticStorageAttr.getValue();
      if (storage.isNegative() || storage.getActiveBits() > 63) {
        emitError(location) << "static property rand_mode storage is malformed";
        return failure();
      }
      reference = staticModeReference(storage.getZExtValue());
    } else {
      Type referenceType = sim::ManagedRefType::get(function.getContext(), i64,
                                                    objectType.getClassName());
      reference = sim::SimClassFieldRefOp::create(
          builder, location, referenceType, loweredReceiver, modeField);
    }
    uint64_t propertyBit = staticStorageAttr ? 1 : 0;
    if (propertyIndexAttr) {
      APInt propertyIndex = propertyIndexAttr.getValue();
      if (propertyIndex.isNegative() || propertyIndex.getActiveBits() > 64 ||
          propertyIndex.getZExtValue() >= 64) {
        emitError(location) << "property rand_mode index is malformed";
        return failure();
      }
      propertyBit = uint64_t{1} << propertyIndex.getZExtValue();
    }
    if (selectionMask)
      propertyBit = selectionMask.getValue().getZExtValue();
    uint64_t ancestorBits =
        ancestorMask ? ancestorMask.getValue().getZExtValue() : 0;
    auto constantMode = [&](uint64_t bits) -> Value {
      return arith::ConstantOp::create(
          builder, location, i64, builder.getIntegerAttr(i64, APInt(64, bits)));
    };
    auto materializeAncestorModes = [&](Value oldMode) -> Value {
      if (!ancestorBits)
        return oldMode;
      Value disabled = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ne,
          arith::AndIOp::create(builder, location, oldMode,
                                constantMode(ancestorBits)),
          constantMode(0));
      Value inherited = arith::OrIOp::create(
          builder, location, oldMode,
          constantMode(familyMask.getValue().getZExtValue()));
      return arith::SelectOp::create(builder, location, disabled, inherited,
                                     oldMode);
    };
    if (arrayTypeAttr) {
      auto layout = getFixedRandomArrayLayout(arrayTypeAttr.getValue());
      if (!layout ||
          static_cast<bool>(arrayModeField) ==
              static_cast<bool>(arrayModeStorage) ||
          !propertyBit ||
          (arrayModeField && (!objectType || !propertyIndexAttr)) ||
          (arrayModeStorage && !staticStorageAttr) ||
          (arrayModeStorage &&
           (arrayModeStorage.getValue().isNegative() ||
            arrayModeStorage.getValue().getActiveBits() > 63))) {
        emitError(location) << "array rand_mode metadata is malformed";
        return failure();
      }
      Type bitmapType = builder.getIntegerType(layout->elementCount);
      Value bitmapReference;
      if (arrayModeField) {
        Type refType = sim::ManagedRefType::get(
            function.getContext(), bitmapType, objectType.getClassName());
        bitmapReference = sim::SimClassFieldRefOp::create(
            builder, location, refType, loweredReceiver, arrayModeField);
      } else {
        bitmapReference = sim::SimContextStorageOp::create(
            builder, location,
            sim::RefType::get(function.getContext(), bitmapType),
            function.getBody().front().getArgument(0), arrayModeStorage);
      }
      auto bitmapConstant = [&](APInt value) -> Value {
        return arith::ConstantOp::create(
            builder, location, bitmapType,
            builder.getIntegerAttr(bitmapType, value));
      };
      Value bitmapZero = bitmapConstant(APInt(layout->elementCount, 0));
      if (arrayIndices.empty()) {
        if (children.size() != 2) {
          emitError(location)
              << "rand_mode() queries require a singular array element";
          return failure();
        }
        if (failed(storeReference(bitmapReference, bitmapZero, location)))
          return failure();
      } else {
        Value valid = arith::ConstantOp::create(
            builder, location, builder.getI1Type(), builder.getBoolAttr(true));
        Value offset = arith::ConstantOp::create(builder, location, i64,
                                                 builder.getI64IntegerAttr(0));
        // Bit zero describes the rightmost integral leaf in the logical
        // bitstream. Each source index contributes its distance from the
        // declared right bound, scaled by the remaining dimensions.
        Type selectedType = arrayTypeAttr.getValue();
        unsigned remaining = layout->elementCount;
        for (Value index : loweredIndices) {
          auto array = dyn_cast<sim::UnpackedArrayType>(selectedType);
          if (!array) {
            emitError(location)
                << "rand_mode selection is not an unpacked array element";
            return failure();
          }
          Value known =
              arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                        builder.getBoolAttr(true));
          if (auto logic = dyn_cast<sim::LogicType>(index.getType())) {
            Value bits = sim::SimLogicToBitsOp::create(
                builder, location, builder.getIntegerType(logic.getWidth()),
                index);
            Value roundTrip =
                sim::SimLogicFromBitsOp::create(builder, location, logic, bits);
            known = sim::SimLogicCompareOp::create(
                builder, location, builder.getI1Type(),
                sim::CompareKind::CaseEq, index, roundTrip);
            index = bits;
          }
          auto indexType = cast<IntegerType>(index.getType());
          auto indexConstant = [&](int64_t value) -> Value {
            return arith::ConstantOp::create(
                builder, location, indexType,
                builder.getIntegerAttr(
                    indexType, APInt(indexType.getWidth(),
                                     static_cast<uint64_t>(value), true)));
          };
          // Check the widened index before narrowing. Otherwise a large or
          // unknown index could accidentally select an existing mode bit.
          Value inRange = arith::AndIOp::create(
              builder, location,
              arith::CmpIOp::create(
                  builder, location, arith::CmpIPredicate::sge, index,
                  indexConstant(std::min(array.getLeft(), array.getRight()))),
              arith::CmpIOp::create(
                  builder, location, arith::CmpIPredicate::sle, index,
                  indexConstant(std::max(array.getLeft(), array.getRight()))));
          valid = arith::AndIOp::create(
              builder, location, valid,
              arith::AndIOp::create(builder, location, known, inRange));
          Value distance =
              array.getLeft() >= array.getRight()
                  ? arith::SubIOp::create(builder, location, index,
                                          indexConstant(array.getRight()))
                        .getResult()
                  : arith::SubIOp::create(builder, location,
                                          indexConstant(array.getRight()),
                                          index)
                        .getResult();
          remaining /= sim::getAggregateNumElements(array);
          Value ordinal =
              arith::TruncIOp::create(builder, location, i64, distance);
          ordinal = arith::MulIOp::create(
              builder, location, ordinal,
              arith::ConstantOp::create(builder, location, i64,
                                        builder.getI64IntegerAttr(remaining)));
          offset = arith::AddIOp::create(builder, location, offset, ordinal);
          selectedType = array.getElementType();
        }
        if (children.size() == 1 && isa<sim::UnpackedArrayType>(selectedType)) {
          emitError(location)
              << "rand_mode() queries require a singular array element";
          return failure();
        }
        FailureOr<Type> resultType = children.size() == 1
                                         ? getNormalizedSemanticType(op)
                                         : FailureOr<Type>(builder.getI1Type());
        if (failed(resultType))
          return failure();
        Block *active = addBlock();
        Block *inactive = addBlock();
        Block *done = addBlock();
        Value result = done->addArgument(*resultType, location);
        cf::CondBranchOp::create(builder, location, valid, active, ValueRange{},
                                 inactive, ValueRange{});
        setCurrent(inactive);
        Value falseBit = arith::ConstantOp::create(
            builder, location, builder.getI1Type(), builder.getBoolAttr(false));
        FailureOr<Value> falseResult =
            convert(falseBit, *resultType, false, location);
        if (failed(falseResult))
          return failure();
        cf::BranchOp::create(builder, location, done, ValueRange{*falseResult});
        setCurrent(active);
        FailureOr<Value> oldMode = loadReference(reference, location);
        FailureOr<Value> bitmap = loadReference(bitmapReference, location);
        if (failed(oldMode) || failed(bitmap))
          return failure();
        Value parentDisabled = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne,
            arith::AndIOp::create(
                builder, location, *oldMode,
                arith::ConstantOp::create(
                    builder, location, i64,
                    builder.getIntegerAttr(
                        i64, APInt(64, propertyBit | ancestorBits)))),
            arith::ConstantOp::create(builder, location, i64,
                                      builder.getI64IntegerAttr(0)));
        FailureOr<Value> shift = convert(offset, bitmapType, false, location);
        if (failed(shift))
          return failure();
        Value selectionMask =
            arith::ShLIOp::create(builder, location,
                                  bitmapConstant(APInt::getLowBitsSet(
                                      layout->elementCount, remaining)),
                                  *shift);
        Value newResult;
        if (children.size() == 1) {
          Value elementDisabled = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::ne,
              arith::AndIOp::create(builder, location, *bitmap, selectionMask),
              bitmapZero);
          Value disabled = arith::OrIOp::create(
              builder, location, parentDisabled, elementDisabled);
          Value enabled = arith::XOrIOp::create(
              builder, location, disabled,
              arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                        builder.getBoolAttr(true)));
          FailureOr<Value> converted =
              convert(enabled, *resultType, false, location);
          if (failed(converted))
            return failure();
          newResult = *converted;
        } else {
          // The parent bit lazily disables the whole array. Materialize that
          // state before changing a subset, then clear the parent bit so the
          // bitmap alone describes the remaining inactive elements.
          Value oldBitmap = arith::SelectOp::create(
              builder, location, parentDisabled,
              bitmapConstant(APInt::getAllOnes(layout->elementCount)), *bitmap);
          Value enabledBitmap = arith::AndIOp::create(
              builder, location, oldBitmap,
              arith::XOrIOp::create(
                  builder, location, selectionMask,
                  bitmapConstant(APInt::getAllOnes(layout->elementCount))));
          Value disabledBitmap =
              arith::OrIOp::create(builder, location, oldBitmap, selectionMask);
          Value nextBitmap = arith::SelectOp::create(
              builder, location, *setterEnabled, enabledBitmap, disabledBitmap);
          Value nextMode = arith::AndIOp::create(
              builder, location, materializeAncestorModes(*oldMode),
              arith::ConstantOp::create(
                  builder, location, i64,
                  builder.getIntegerAttr(
                      i64, APInt(64, ~(propertyBit | ancestorBits)))));
          if (failed(storeReference(bitmapReference, nextBitmap, location)) ||
              failed(storeReference(reference, nextMode, location)))
            return failure();
          newResult =
              arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                        builder.getBoolAttr(false));
        }
        cf::BranchOp::create(builder, location, done, ValueRange{newResult});
        setCurrent(done);
        return result;
      }
    }
    if (children.size() == 2) {
      Value zero = arith::ConstantOp::create(builder, location, i64,
                                             builder.getI64IntegerAttr(0));
      Value disabled = arith::ConstantOp::create(
          builder, location, i64,
          builder.getIntegerAttr(
              i64, APInt(64, (propertyIndexAttr || staticStorageAttr)
                                 ? propertyBit
                                 : UINT64_MAX)));
      Value mode;
      if (propertyIndexAttr || staticStorageAttr) {
        FailureOr<Value> loaded = loadReference(reference, location);
        if (failed(loaded))
          return failure();
        Value oldMode = arith::AndIOp::create(builder, location,
                                              materializeAncestorModes(*loaded),
                                              constantMode(~ancestorBits));
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
      auto storeMode = [&](ArrayRef<int64_t> staticStorages,
                           ArrayAttr elementFields,
                           FlatSymbolRefAttr target) -> LogicalResult {
        if (failed(storeReference(reference, mode, location)))
          return failure();
        Value staticMode = arith::SelectOp::create(
            builder, location, *setterEnabled, zero,
            arith::ConstantOp::create(builder, location, i64,
                                      builder.getI64IntegerAttr(1)));
        for (int64_t storage : staticStorages)
          sim::SimRefStoreOp::create(
              builder, location, staticMode,
              staticModeReference(static_cast<uint64_t>(storage)));
        if (elementFields) {
          Value object = loweredReceiver;
          if (object && target) {
            Type targetType =
                sim::ClassHandleType::get(function.getContext(), target);
            if (object.getType() != targetType)
              object = sim::SimClassCastOp::create(builder, location,
                                                   targetType, object);
          }
          for (auto entry :
               elementFields.getAsRange<sim::RandomElementModesAttr>()) {
            FlatSymbolRefAttr field = entry.getField();
            Type bitmapType = builder.getIntegerType(entry.getWidth());
            Value bitmapReference;
            if (field && (!object || !target)) {
              emitError(location)
                  << "rand_mode element field has no object receiver";
              return failure();
            }
            if (field)
              bitmapReference = sim::SimClassFieldRefOp::create(
                  builder, location,
                  sim::ManagedRefType::get(function.getContext(), bitmapType,
                                           target),
                  object, field);
            else
              bitmapReference = sim::SimContextStorageOp::create(
                  builder, location,
                  sim::RefType::get(function.getContext(), bitmapType),
                  function.getBody().front().getArgument(0),
                  builder.getI64IntegerAttr(*entry.getStorage()));
            Value bitmapZero = arith::ConstantOp::create(
                builder, location, bitmapType,
                builder.getIntegerAttr(
                    bitmapType,
                    APInt(cast<IntegerType>(bitmapType).getWidth(), 0)));
            if (failed(storeReference(bitmapReference, bitmapZero, location)))
              return failure();
          }
        }
        return success();
      };
      if (staticDispatchAttr) {
        Block *done = addBlock();
        for (auto entry :
             staticDispatchAttr.getAsRange<sim::RandModeDispatchAttr>()) {
          FlatSymbolRefAttr target = entry.getTarget();
          Value matches = sim::SimClassIsInstanceOp::create(
              builder, location, loweredReceiver, target);
          Block *selected = addBlock();
          Block *next = addBlock();
          cf::CondBranchOp::create(builder, location, matches, selected,
                                   ValueRange{}, next, ValueRange{});
          setCurrent(selected);
          if (failed(storeMode(entry.getStorages().asArrayRef(),
                               entry.getElements(), target)))
            return failure();
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
        if (failed(storeMode(
                {}, structureElementFields,
                objectType ? cast<FlatSymbolRefAttr>(objectType.getClassName())
                           : FlatSymbolRefAttr{})))
          return failure();
      }
      return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                       builder.getBoolAttr(false))
          .getResult();
    }
    FailureOr<Value> loadedMode = loadReference(reference, location);
    if (failed(loadedMode))
      return failure();
    Value mode = *loadedMode;
    Value zero = arith::ConstantOp::create(builder, location, i64,
                                           builder.getI64IntegerAttr(0));
    if (propertyIndexAttr || staticStorageAttr)
      mode = arith::AndIOp::create(
          builder, location, mode,
          arith::ConstantOp::create(
              builder, location, i64,
              builder.getIntegerAttr(i64,
                                     APInt(64, propertyBit | ancestorBits))));
    Value enabled = arith::CmpIOp::create(builder, location,
                                          arith::CmpIPredicate::eq, mode, zero);
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    if (failed(resultType))
      return failure();
    return convert(enabled, *resultType, false, location);
  }
  if (sim::has<sim::metadata::ConstraintMode>(op) ||
      (op.getIsSystemCall() && op.getCalleeName() == "constraint_mode")) {
    if (children.empty() || children.size() > 2) {
      emitError(location)
          << "constraint_mode expects a receiver and an optional block "
             "query or on/off argument";
      return failure();
    }
    auto blockIndexAttr = sim::get<sim::metadata::ConstraintModeBlock>(op);
    if (!blockIndexAttr && children.size() != 2) {
      emitError(location)
          << "class-wide constraint_mode requires an on/off argument";
      return failure();
    }
    Operation *receiverNode = children.front();
    // IEEE 1800-2017 18.9: a method of the owning class may name a constraint
    // block on its own, which the frontend spells as a direct reference to the
    // block symbol rather than as a member access. That form's object is the
    // enclosing method's `this`.
    bool implicitThisBlock = false;
    if (blockIndexAttr) {
      auto member =
          dyn_cast<semantic::SVMemberAccessExpressionOp>(children.front());
      SmallVector<Operation *> memberChildren =
          member ? getChildren(member) : SmallVector<Operation *>{};
      implicitThisBlock =
          !member &&
          isa<semantic::SVArbitrarySymbolExpressionOp>(children.front()) &&
          thisObject;
      if (!implicitThisBlock && (!member || memberChildren.size() != 1)) {
        emitError(location)
            << "constraint-block constraint_mode has no object receiver";
        return failure();
      }
      if (!implicitThisBlock)
        receiverNode = memberChildren.front();
    }
    FailureOr<Value> loweredReceiver = implicitThisBlock
                                           ? FailureOr<Value>(thisObject)
                                           : lowerExpression(receiverNode);
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
        sim::get<sim::metadata::ConstraintModeStaticStorage>(op);
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
        lookupNearestSymbolFrom<sim::SimClassDeclOp>(function,
                                                     objectType.getClassName());
    while (declaration &&
           !sim::has<sim::metadata::ConstraintModeField>(declaration)) {
      if (!declaration.getBaseAttr())
        break;
      declaration = lookupNearestSymbolFrom<sim::SimClassDeclOp>(
          function, declaration.getBaseAttr());
    }
    auto modeField =
        declaration ? sim::get<sim::metadata::ConstraintModeField>(declaration)
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
      if (auto staticStorages =
              sim::get<sim::metadata::ConstraintModeStaticStorages>(op)) {
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
          lookupNearestSymbolFrom<sim::SimClassDeclOp>(
              function, objectType.getClassName());
      while (declaration &&
             !sim::has<sim::metadata::RandomStateField>(declaration)) {
        if (!declaration.getBaseAttr())
          break;
        declaration = lookupNearestSymbolFrom<sim::SimClassDeclOp>(
            function, declaration.getBaseAttr());
      }
      auto stateField =
          declaration ? sim::get<sim::metadata::RandomStateField>(declaration)
                      : FlatSymbolRefAttr{};
      auto incrementField =
          declaration
              ? sim::get<sim::metadata::RandomIncrementField>(declaration)
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
            builder, location, stringType, state,
            sim::RadixAttr::get(builder.getContext(), sim::Radix::Hex),
            builder.getBoolAttr(false));
        Value separator = sim::SimStringLiteralOp::create(
            builder, location, stringType, builder.getStringAttr(":"));
        Value prefix = sim::SimStringConcatOp::create(
            builder, location, stringType, ValueRange{stateText, separator});
        Value incrementText = sim::SimStringFormatIntegerOp::create(
            builder, location, stringType, increment,
            sim::RadixAttr::get(builder.getContext(), sim::Radix::Hex),
            builder.getBoolAttr(false));
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
            cursor, builder.getStringAttr(""), static_cast<uint32_t>('x'), 0);
        auto incrementField = sim::SimStringScanFieldOp::create(
            builder, location, TypeRange{stringType, i32, i32}, methodArgument,
            stateField.getNextCursor(), builder.getStringAttr(":"),
            static_cast<uint32_t>('x'), 0);
        Value parsedState = sim::SimStringParseIntegerOp::create(
            builder, location, i64, stateField.getField(),
            sim::RadixAttr::get(builder.getContext(), sim::Radix::Hex));
        Value parsedIncrement = sim::SimStringParseIntegerOp::create(
            builder, location, i64, incrementField.getField(),
            sim::RadixAttr::get(builder.getContext(), sim::Radix::Hex));
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
        sim::get<sim::metadata::ObjectRandomDispatchClasses>(op);
    if (!dispatchClasses)
      return emitMethod(*loweredReceiver);

    Type resultType = objectRandomMethod == "get_randstate"
                          ? Type(sim::StringType::get(function.getContext()))
                          : Type(builder.getI1Type());
    Block *done = addBlock();
    Value result = done->addArgument(resultType, location);
    for (auto target : dispatchClasses.getAsRange<FlatSymbolRefAttr>()) {
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
  auto callee = sim::get<sim::metadata::Callee>(op);
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
            sim::RadixAttr::get(builder.getContext(),
                                static_cast<sim::Radix>(radix))));
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
        // IEEE 1800-2017 6.16.11 through 6.16.14 word the four methods alike,
        // differing only in base: each stores "the ASCII <base> representation
        // of i" for the same signed `integer` formal. The sign therefore
        // survives the radix, so -11 spells "-b" in hex rather than the
        // unsigned reading of its two's-complement pattern.
        Value updated = sim::SimStringFormatIntegerOp::create(
            builder, location, sim::StringType::get(function.getContext()),
            *input,
            sim::RadixAttr::get(builder.getContext(),
                                static_cast<sim::Radix>(radix)),
            builder.getBoolAttr(isSignedNode(children[1])));
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
  if (sim::has<sim::metadata::ClassInstance>(op)) {
    auto formals = sim::get<sim::metadata::CalleeFormals>(op);
    std::optional<ArrayRef<int64_t>> defaulted = op.getDefaultedArguments();
    bool superCall = sim::has<sim::metadata::ClassSuper>(op);
    bool implicitThis = !superCall && formals && thisObject &&
                        children.size() == formals.size();
    if (!formals) {
      emitError(location) << "instance call has no frozen formal inventory";
      return failure();
    }
    if (!defaulted || defaulted->size() != formals.size() ||
        llvm::any_of(*defaulted,
                     [](int64_t value) { return value != 0 && value != 1; })) {
      emitError(location)
          << "instance call has malformed default argument inventory";
      return failure();
    }
    if (formals.size() + (superCall || implicitThis ? 0 : 1) !=
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
    auto method = sim::get<sim::metadata::ClassMethod>(op);
    auto declaration =
        method ? lookupNearestSymbolFrom<sim::SimClassMethodDeclOp>(function,
                                                                    method)
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
      Operation *actual;
      Value scopeObject;
      Type formalType;
      bool formalSigned;
    };
    struct TaskCopyOut {
      Value formal;
      Operation *actual;
      Value scopeObject;
      bool formalSigned;
    };
    SmallVector<Value> arguments;
    SmallVector<ClassCopyOut> copyOuts;
    SmallVector<TaskCopyOut> taskCopyOuts;
    bool classTask = sim::has<sim::metadata::IsTask>(op);
    size_t formalIndex = 0;
    for (auto [actual, formalAttr] :
         llvm::zip_equal(ArrayRef<Operation *>(children).drop_front(
                             superCall || implicitThis ? 0 : 1),
                         formals)) {
      bool usesDefault = (*defaulted)[formalIndex++] != 0;
      Value savedThisObject = thisObject;
      if (usesDefault)
        thisObject = *receiver;
      llvm::scope_exit restoreThisObject([&] { thisObject = savedThisObject; });
      auto formal = cast<sim::CalleeFormalAttr>(formalAttr);
      sim::FormalDirection direction = formal.getDirection();
      Type formalType = formal.getType();
      bool formalSigned = formal.getIsSigned();
      if (direction == sim::FormalDirection::In) {
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

      Operation *destination = getCopyOutActual(actual);
      if (direction == sim::FormalDirection::Ref) {
        FailureOr<Value> destinationRef = lowerExpression(destination, true);
        if (failed(destinationRef))
          return failure();
        if (!getReferenceElementType(*destinationRef)) {
          emitError(location)
              << "class method ref actuals must be variable references";
          return failure();
        }
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
      if (direction == sim::FormalDirection::Out) {
        bool staticFormal = formal.getIsStatic();
        initial =
            staticFormal && isa<sim::EventType>(formalType)
                ? sim::SimEventNullOp::create(builder, location, formalType)
                      .getResult()
                : createDefaultValue(builder, location, formalType);
        if (!initial) {
          emitError(location)
              << "cannot materialize a class output-formal default for type "
              << formalType;
          return failure();
        }
      } else {
        FailureOr<Value> copied =
            copyInFromActual(destination, formalType, formalSigned, location);
        if (failed(copied))
          return failure();
        initial = *copied;
      }
      arguments.push_back(cloneSequentialValue(initial, location));
      Value scopeObject = usesDefault ? *receiver : Value();
      if (classTask) {
        // IEEE 1800-2023 13.5.1: a task's changes to a formal stay invisible
        // outside it until it returns, so the formal has its own storage.
        Value temporary = sim::SimRefAllocOp::create(
            builder, location,
            sim::RefType::get(function.getContext(), formalType), initial);
        arguments.push_back(temporary);
        taskCopyOuts.push_back(
            {temporary, destination, scopeObject, formalSigned});
      } else {
        copyOuts.push_back({destination, scopeObject, formalType, formalSigned});
      }
    }
    if (auto captures = sim::get<sim::metadata::CalleeCaptures>(op))
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
        for (const TaskCopyOut &copyOut : taskCopyOuts) {
          Value returned = sim::SimRefLoadOp::create(
              builder, location,
              cast<sim::RefType>(copyOut.formal.getType()).getElementType(),
              copyOut.formal);
          if (failed(copyOutToActual(copyOut.actual, copyOut.scopeObject,
                                     returned, copyOut.formalSigned,
                                     location)))
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
                                   schedule::ContinuationSiteAttr{},
                                   continuation);
      };
      if (!sim::has<sim::metadata::ClassVirtual>(op)) {
        emitTaskCall(callee, *receiver);
        return finishTask();
      }

      auto slot = sim::get<sim::metadata::ClassSlot>(op);
      auto signature = sim::get<sim::metadata::ClassSignature>(op);
      if (!method || !slot || !signature || signature.getValue().isZero())
        return emitError(location)
                   << "virtual class task has no frozen slot and signature",
               failure();
      sim::SimClassVirtualTaskCallOp::create(
          builder, location, *receiver, method, slot, signature, arguments,
          builder.getI64IntegerAttr(arguments.size()),
          schedule::ContinuationSiteAttr{}, continuation);
      return finishTask();
    }
    ValueRange results;
    if (sim::has<sim::metadata::ClassVirtual>(op)) {
      auto slot = sim::get<sim::metadata::ClassSlot>(op);
      auto signature = sim::get<sim::metadata::ClassSignature>(op);
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
    if (failed(emitTerminationPoll(location)))
      return failure();
    if (!classTask)
      for (auto [index, copyOut] : llvm::enumerate(copyOuts))
        if (failed(copyOutToActual(
                copyOut.actual, copyOut.scopeObject,
                results[index + (hasFunctionResult ? 1 : 0)],
                copyOut.formalSigned, location)))
          return failure();
    if (hasFunctionResult)
      return results.front();
    return arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                     builder.getBoolAttr(false))
        .getResult();
  }
  bool directTask = sim::has<sim::metadata::IsTask>(op);
  SmallVector<Value> operands{function.getBody().front().getArgument(0)};
  auto formals = sim::get<sim::metadata::CalleeFormals>(op);
  auto virtualCallees = sim::get<sim::metadata::VirtualInterfaceCallees>(op);
  bool staticClassReceiver = sim::has<sim::metadata::StaticClassReceiver>(op);
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
    receiver = lowerVirtualInterfaceReceiverMembers(op, *receiver, location);
    if (failed(receiver))
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
    Operation *actual;
    /// The actual addressed when the call began, for transports it shapes.
    Value destination;
    Value taskDestination;
    Type formalType;
    Type resultType;
    bool formalSigned;
    bool destinationSigned;
    uint32_t dpiCategory;
    uint32_t dpiWidth;
    bool dpiFourState;
    dpi::OpenArrayABIAttr openArray;
    dpi::AggregateABIAttr aggregate;
  };
  auto getDPITransportWidth = [](Type type) -> std::optional<unsigned> {
    if (isa<sim::StringType, sim::ChandleType, sim::DynamicArrayType,
            sim::QueueType>(type))
      return 64;
    if (type.isF32())
      return 32;
    if (type.isF64())
      return 64;
    return sim::getPackedWidth(type);
  };
  auto isDPIFourState = [](Type type) {
    Type scalar = sim::getPackedScalarType(type);
    return scalar && isa<sim::LogicType>(scalar);
  };
  SmallVector<CopyOut> copyOuts;
  SmallVector<Attribute> dpiOperandABI;
  SmallVector<Attribute> dpiOpenArrayLayouts;
  SmallVector<Attribute> dpiAggregateLayouts;
  auto packOpenArrayActual = [&](Value value) -> FailureOr<Value> {
    std::optional<SmallVector<uint64_t>> plan =
        sim::getDPIAggregateBitStreamPlan(value.getType());
    if (!plan)
      return value;
    uint64_t width = (*plan)[3];
    if (width == 0 || width > std::numeric_limits<unsigned>::max())
      return emitError(location)
                 << "DPI open-array transport width is not representable",
             failure();
    Type packed = streamContainsFourState(value.getType())
                      ? Type(sim::LogicType::get(builder.getContext(), width))
                      : Type(IntegerType::get(builder.getContext(), width));
    SmallVector<int64_t> encoded;
    encoded.reserve(plan->size());
    llvm::transform(*plan, std::back_inserter(encoded),
                    [](uint64_t word) { return static_cast<int64_t>(word); });
    return sim::SimAggregateExportBitstreamOp::create(
               builder, location, packed, value,
               builder.getDenseI64ArrayAttr(encoded))
        .getResult();
  };
  auto makeOpenArrayLayout =
      [&](Type formalSemantic, Type actualSemantic,
          Type transportType) -> FailureOr<dpi::OpenArrayABIAttr> {
    // The actual supplies the concrete width of unsized packed dimensions.
    // Remove only its unpacked dimensions; the remaining packed shape is one
    // canonical C element and is normalized to [width-1:0] by the DPI ABI.
    Type element = actualSemantic;
    while (true) {
      if (auto array = dyn_cast<semantic::RangedUnpackedArrayType>(element)) {
        element = array.getElementType();
        continue;
      }
      if (auto array = dyn_cast<semantic::UnpackedArrayType>(element)) {
        element = array.getElementType();
        continue;
      }
      if (auto array = dyn_cast<semantic::DynArrayType>(element)) {
        element = array.getElementType();
        continue;
      }
      break;
    }
    FailureOr<DPIABIType> elementABI = classifyDPIABIType(element, location);
    if (failed(elementABI))
      return failure();
    FailureOr<Type> normalizedElement =
        normalizeSemanticType(element, location, function);
    if (failed(normalizedElement))
      return failure();
    FailureOr<dpi::AggregateABIAttr> elementLayout =
        makeDPIAggregateABI(element, *normalizedElement, location, builder,
                            /*compactTransport=*/true);
    if (failed(elementLayout))
      return failure();
    SmallVector<SemanticDimension> actualDimensions;
    SmallVector<SemanticDimension> formalDimensions;
    llvm::copy_if(
        getSemanticDimensions(actualSemantic),
        std::back_inserter(actualDimensions),
        [](const SemanticDimension &dimension) { return dimension.unpacked; });
    llvm::copy_if(
        getSemanticDimensions(formalSemantic),
        std::back_inserter(formalDimensions),
        [](const SemanticDimension &dimension) { return dimension.unpacked; });
    if (actualDimensions.size() != formalDimensions.size())
      return emitError(location)
                 << "DPI open-array actual and formal have different "
                    "unpacked dimension counts",
             failure();

    auto fixedExtent =
        [&](const SemanticDimension &dimension) -> FailureOr<uint64_t> {
      uint64_t distance = dimension.left >= dimension.right
                              ? static_cast<uint64_t>(dimension.left) -
                                    static_cast<uint64_t>(dimension.right)
                              : static_cast<uint64_t>(dimension.right) -
                                    static_cast<uint64_t>(dimension.left);
      if (distance == UINT64_MAX)
        return failure();
      return distance + 1;
    };
    auto childType = [](Type type) -> Type {
      return llvm::TypeSwitch<Type, Type>(type)
          .Case<sim::UnpackedArrayType, sim::DynamicArrayType, sim::QueueType>(
              [](auto array) { return array.getElementType(); })
          .Default([](Type) { return Type{}; });
    };

    SmallVector<int64_t> ranges;
    SmallVector<int64_t> sourceRanges;
    SmallVector<int64_t> shapePlan;
    bool hasRuntimeShape = false;
    bool simpleRootRuntime = actualDimensions.size() == 1;
    Type sourceType = transportType;
    for (auto [index, actualDimension] : llvm::enumerate(actualDimensions)) {
      const SemanticDimension &formalDimension = formalDimensions[index];
      bool formalUnsized =
          formalDimension.kind == SemanticDimensionKind::OpenArray;
      bool runtime =
          actualDimension.kind == SemanticDimensionKind::DynamicArray ||
          actualDimension.kind == SemanticDimensionKind::Queue;
      if (!runtime && !actualDimension.isFixed())
        return emitError(location)
                   << "DPI open-array actual has an unsupported runtime "
                      "dimension",
               failure();
      if (!formalUnsized && !formalDimension.isFixed())
        return emitError(location)
                   << "DPI open-array formal has an unsupported dimension",
               failure();

      FailureOr<uint64_t> actualExtent = runtime
                                             ? FailureOr<uint64_t>(uint64_t{0})
                                             : fixedExtent(actualDimension);
      FailureOr<uint64_t> formalExtent = formalUnsized
                                             ? FailureOr<uint64_t>(uint64_t{0})
                                             : fixedExtent(formalDimension);
      if (failed(actualExtent) || failed(formalExtent) ||
          (!runtime && !formalUnsized && *actualExtent != *formalExtent) ||
          (!formalUnsized && *formalExtent > INT32_MAX))
        return emitError(location)
                   << "DPI open-array dimension size is not representable or "
                      "does not match its sized formal",
               failure();

      int64_t descriptorLeft = 0;
      int64_t descriptorRight = 0;
      if (formalUnsized) {
        if (!runtime) {
          descriptorLeft = actualDimension.left;
          descriptorRight = actualDimension.right;
        }
      } else {
        descriptorRight = static_cast<int64_t>(*formalExtent - 1);
      }
      ranges.append({descriptorLeft, descriptorRight});
      if (runtime)
        sourceRanges.append({0, 0});
      else
        sourceRanges.append({actualDimension.left, actualDimension.right});

      Type child = childType(sourceType);
      std::optional<uint64_t> childSpan = sim::getProvenanceSpan(child);
      if (!child || !childSpan || *childSpan == 0 || *childSpan > INT64_MAX)
        return emitError(location)
                   << "DPI open-array dimension has no bounded element layout",
               failure();
      // Managed containers are always two-state handle words even when their
      // eventual leaf type is four-state. Only the immediate provenance view
      // determines whether container_export_fixed has an unknown plane.
      bool childFourState = sim::containsFourStateLeaf(child);
      if (runtime) {
        uint32_t kind =
            actualDimension.kind == SemanticDimensionKind::Queue ? 2u : 1u;
        shapePlan.append({static_cast<int64_t>(kind), descriptorLeft,
                          descriptorRight, 0, 0,
                          static_cast<int64_t>(*childSpan),
                          childFourState ? 1 : 0, formalUnsized ? 0 : 1});
        hasRuntimeShape = true;
        simpleRootRuntime &= formalUnsized;
      } else {
        auto storageStride = sim::getArrayElementStorageStride(sourceType);
        if (!storageStride)
          return emitError(location)
                     << "DPI open-array fixed dimension has no bounded stride",
                 failure();
        uint64_t stride = *storageStride;
        if (stride == 0 || stride > INT64_MAX ||
            (*actualExtent - 1) > UINT64_MAX / stride)
          return emitError(location)
                     << "DPI open-array fixed dimension stride overflows",
                 failure();
        uint64_t start = actualDimension.left <= actualDimension.right
                             ? 0
                             : (*actualExtent - 1) * stride;
        int64_t signedStride = actualDimension.left <= actualDimension.right
                                   ? static_cast<int64_t>(stride)
                                   : -static_cast<int64_t>(stride);
        shapePlan.append({0, descriptorLeft, descriptorRight,
                          static_cast<int64_t>(start), signedStride,
                          static_cast<int64_t>(*childSpan),
                          childFourState ? 1 : 0, formalUnsized ? 0 : 1});
      }
      sourceType = child;
    }
    uint32_t elementWidth = elementABI->width;
    uint32_t storage = 0;
    if (hasRuntimeShape) {
      elementLayout =
          makeDPIAggregateABI(element, *normalizedElement, location, builder,
                              /*compactTransport=*/false);
      std::optional<uint64_t> span = sim::getProvenanceSpan(*normalizedElement);
      if (failed(elementLayout) || !span || *span == 0 ||
          *span > std::numeric_limits<uint32_t>::max())
        return emitError(location)
                   << "DPI dynamic open-array element has no bounded layout",
               failure();
      elementWidth = static_cast<uint32_t>(*span);
      storage = simpleRootRuntime ? 1u : 2u;
    }
    std::optional<unsigned> transportWidth;
    if (storage == 0) {
      std::optional<SmallVector<uint64_t>> plan =
          sim::getDPIAggregateBitStreamPlan(transportType);
      uint64_t width = plan ? (*plan)[3] : 0;
      if (width != 0 && width <= UINT_MAX)
        transportWidth = static_cast<unsigned>(width);
      else
        transportWidth = getDPITransportWidth(transportType);
    } else if (storage == 1) {
      transportWidth = 64;
    } else if (std::optional<uint64_t> width =
                   sim::getProvenanceSpan(transportType);
               width && *width <= UINT_MAX) {
      transportWidth = static_cast<unsigned>(*width);
    }
    if (!transportWidth || *transportWidth == 0)
      return emitError(location)
                 << "DPI open-array actual has no bounded transport for "
                 << transportType,
             failure();
    int64_t packedLeft = static_cast<int64_t>(elementWidth) - 1;
    bool transportFourState = storage == 0
                                  ? elementABI->fourState
                                  : sim::containsFourStateLeaf(transportType);
    return dpi::OpenArrayABIAttr::get(
        builder.getContext(), storage,
        static_cast<dpi::ABIKind>(elementABI->kind), elementWidth,
        *transportWidth, transportFourState, elementABI->fourState,
        elementLayout->getCSize(), elementLayout->getCAlignment(),
        elementLayout->getStringCount(), elementLayout->getLeaves(), packedLeft,
        0, builder.getDenseI64ArrayAttr(ranges),
        builder.getDenseI64ArrayAttr(sourceRanges),
        builder.getDenseI64ArrayAttr(storage == 2 ? shapePlan
                                                  : ArrayRef<int64_t>{}));
  };
  auto makeAggregateLayout =
      [&](Type semanticType,
          Type normalizedType) -> FailureOr<dpi::AggregateABIAttr> {
    return makeDPIAggregateABI(semanticType, normalizedType, location, builder);
  };
  for (auto [child, formalAttr] : llvm::zip_equal(actuals, formals)) {
    auto formal = cast<sim::CalleeFormalAttr>(formalAttr);
    sim::FormalDirection direction = formal.getDirection();
    bool isInput = direction == sim::FormalDirection::In;
    Operation *actual = isInput ? child : getCopyOutActual(child);
    Type formalType = formal.getType();
    bool formalSigned = formal.getIsSigned();
    Type formalSemantic = formal.getSemanticType();
    uint32_t dpiCategory = 0;
    if (formalSemantic) {
      FailureOr<DPIABIKind> category = getDPIABIKind(formalSemantic, location);
      if (failed(category))
        return failure();
      dpiCategory = static_cast<uint32_t>(*category);
    }
    bool dpiImport = dpi::has<dpi::ImportId>(op);
    bool openFormal =
        dpiImport && formalSemantic &&
        llvm::any_of(getSemanticDimensions(formalSemantic),
                     [](const SemanticDimension &dimension) {
                       return dimension.kind ==
                              SemanticDimensionKind::OpenArray;
                     });
    dpi::AggregateABIAttr formalAggregateLayout;
    if (dpiImport && !openFormal) {
      if (isa<semantic::DynArrayType, semantic::QueueType,
              semantic::AssocArrayType, sim::DynamicArrayType, sim::QueueType,
              sim::AssocArrayType>(formalType)) {
        emitError(location)
            << "DPI-C dynamic-array, queue, and associative-array "
               "marshalling is unsupported";
        return failure();
      }
      bool aggregateFormal =
          dpiCategory == static_cast<uint32_t>(dpi::ABIKind::UnpackedAggregate);
      std::optional<unsigned> width = getDPITransportWidth(formalType);
      if (aggregateFormal)
        if (std::optional<uint64_t> span = sim::getProvenanceSpan(formalType);
            span && *span <= std::numeric_limits<unsigned>::max())
          width = static_cast<unsigned>(*span);
      if (!width) {
        emitError(location) << "DPI formal has no fixed transport width";
        return failure();
      }
      if (aggregateFormal) {
        if (!formalSemantic)
          return failure();
        FailureOr<dpi::AggregateABIAttr> layout =
            makeAggregateLayout(formalSemantic, formalType);
        if (failed(layout))
          return failure();
        formalAggregateLayout = *layout;
      }
      dpiOperandABI.push_back(dpi::ABIAttr::get(
          builder.getContext(), static_cast<dpi::ABIKind>(dpiCategory),
          static_cast<dpi::ArgumentDirection>(direction), *width,
          aggregateFormal ? streamContainsFourState(formalType)
                          : isDPIFourState(formalType),
          formalSigned));
      dpiOpenArrayLayouts.push_back(builder.getUnitAttr());
      dpiAggregateLayouts.push_back(formalAggregateLayout
                                        ? Attribute(formalAggregateLayout)
                                        : Attribute(builder.getUnitAttr()));
    }

    if (isInput) {
      FailureOr<Value> argument = lowerExpression(actual);
      if (failed(argument))
        return failure();
      if (dpiImport && openFormal) {
        auto semanticType = actual->getAttrOfType<TypeAttr>("semantic_type");
        if (!semanticType || !formalSemantic)
          return emitError(location)
                     << "DPI open-array binding has no semantic shape",
                 failure();
        FailureOr<dpi::OpenArrayABIAttr> layout = makeOpenArrayLayout(
            formalSemantic, semanticType.getValue(), (*argument).getType());
        if (failed(layout))
          return failure();
        FailureOr<Value> transport = layout->getStorage() == 0
                                         ? packOpenArrayActual(*argument)
                                         : FailureOr<Value>(*argument);
        if (failed(transport))
          return failure();
        dpiOperandABI.push_back(dpi::ABIAttr::get(
            builder.getContext(), dpi::ABIKind::OpenArray,
            dpi::ArgumentDirection::Input, layout->getElementWidth(),
            layout->getFourState(), false));
        dpiOpenArrayLayouts.push_back(*layout);
        dpiAggregateLayouts.push_back(builder.getUnitAttr());
        operands.push_back(cloneSequentialValue(*transport, location));
        continue;
      }
      FailureOr<Value> converted = convert(
          *argument, formalType, isSignedNode(actual), location, formalSigned);
      if (failed(converted))
        return failure();
      operands.push_back(cloneSequentialValue(*converted, location));
      continue;
    }

    // Only a ref formal and a DPI open array, whose transport is shaped by
    // the actual itself, address the actual when the call begins.
    FailureOr<Value> destination = Value();
    Type destinationType;
    if (direction == sim::FormalDirection::Ref || (dpiImport && openFormal)) {
      destination = lowerExpression(actual, true);
      if (failed(destination))
        return failure();
      destinationType = getReferenceElementType(*destination);
      if (!destinationType) {
        emitError(location)
            << "output, inout, and ref actuals must be variable references";
        return failure();
      }
    }
    if (direction == sim::FormalDirection::Ref) {
      bool argumentRef = formal.getArgumentRef();
      if (directTask && !argumentRef) {
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

    if (dpiImport && openFormal) {
      FailureOr<Value> loaded = loadReference(*destination, location);
      if (failed(loaded))
        return failure();
      auto semanticType = actual->getAttrOfType<TypeAttr>("semantic_type");
      if (!semanticType || !formalSemantic)
        return emitError(location)
                   << "DPI open-array binding has no semantic shape",
               failure();
      FailureOr<dpi::OpenArrayABIAttr> layout = makeOpenArrayLayout(
          formalSemantic, semanticType.getValue(), (*loaded).getType());
      if (failed(layout))
        return failure();
      if (layout->getStorage() != 0 && direction == sim::FormalDirection::Out)
        return emitError(location)
                   << "dynamic-array and queue actuals cannot be passed to "
                      "an output DPI open-array formal with unsized "
                      "dimensions",
               failure();
      FailureOr<Value> transport = layout->getStorage() == 0
                                       ? packOpenArrayActual(*loaded)
                                       : FailureOr<Value>(*loaded);
      if (failed(transport))
        return failure();
      dpiOperandABI.push_back(dpi::ABIAttr::get(
          builder.getContext(), dpi::ABIKind::OpenArray,
          static_cast<dpi::ArgumentDirection>(direction),
          layout->getElementWidth(), layout->getFourState(), false));
      dpiOpenArrayLayouts.push_back(*layout);
      dpiAggregateLayouts.push_back(builder.getUnitAttr());
      operands.push_back(cloneSequentialValue(*transport, location));
      copyOuts.push_back({actual,
                          *destination,
                          {},
                          destinationType,
                          (*transport).getType(),
                          false,
                          isSignedNode(actual),
                          static_cast<uint32_t>(dpi::ABIKind::OpenArray),
                          layout->getElementWidth(),
                          layout->getFourState(),
                          *layout,
                          {}});
      continue;
    }

    Value initial;
    if (direction == sim::FormalDirection::Out) {
      bool staticFormal = formal.getIsStatic();
      initial = staticFormal && isa<sim::EventType>(formalType)
                    ? sim::SimEventNullOp::create(builder, location, formalType)
                          .getResult()
                    : createDefaultValue(builder, location, formalType);
      if (!initial) {
        emitError(location)
            << "cannot materialize an output-formal default for type "
            << formalType;
        return failure();
      }
    } else {
      FailureOr<Value> copied =
          copyInFromActual(actual, formalType, formalSigned, location);
      if (failed(copied))
        return failure();
      initial = *copied;
    }
    operands.push_back(cloneSequentialValue(initial, location));
    Value taskDestination;
    if (directTask) {
      // IEEE 1800-2023 13.5.1: a task's changes to a formal stay invisible
      // outside it until it returns, so the formal has its own storage.
      taskDestination = sim::SimRefAllocOp::create(
          builder, location,
          sim::RefType::get(function.getContext(), formalType), initial);
      operands.push_back(taskDestination);
    }
    copyOuts.push_back(
        {actual,
         Value(),
         taskDestination,
         formalType,
         formalType,
         formalSigned,
         isSignedNode(actual),
         dpiCategory,
         formalAggregateLayout
             ? static_cast<uint32_t>(
                   sim::getProvenanceSpan(formalType).value_or(0))
             : getDPITransportWidth(formalType).value_or(0),
         formalAggregateLayout ? streamContainsFourState(formalType)
                               : isDPIFourState(formalType),
         {},
         formalAggregateLayout});
  }

  llvm::StringSet<> readCaptures;
  if (auto reads = sim::get<sim::metadata::CalleeReadCaptures>(op))
    for (Attribute read : reads)
      readCaptures.insert(cast<StringAttr>(read).getValue());
  llvm::StringSet<> writtenCaptures;
  if (auto writes = sim::get<sim::metadata::CalleeWrittenCaptures>(op))
    for (Attribute written : writes)
      writtenCaptures.insert(cast<StringAttr>(written).getValue());
  bool excludeWrittenSensitivity =
      function.getEntryKind() == sim::EntryKind::AlwaysComb ||
      function.getEntryKind() == sim::EntryKind::AlwaysLatch;
  bool includeCalleeReadSensitivity =
      function.getEntryKind() != sim::EntryKind::Continuous &&
      function.getEntryKind() != sim::EntryKind::PortInput &&
      function.getEntryKind() != sim::EntryKind::PortOutput;
  if (!virtualCallees)
    for (const auto &read : readCaptures) {
      if (!includeCalleeReadSensitivity ||
          (excludeWrittenSensitivity &&
           writtenCaptures.contains(read.getKey())))
        continue;
      Value capture = values.lookup(read.getKey());
      if (!capture)
        capture = lvalues.lookup(read.getKey());
      if (capture)
        recordSensitivity(capture);
    }
  if (!virtualCallees)
    if (auto captures = sim::get<sim::metadata::CalleeCaptures>(op))
      for (Attribute captureAttr : captures) {
        StringRef path = cast<StringAttr>(captureAttr).getValue();
        Value capture = values.lookup(path);
        if (!capture) {
          emitError(location)
              << "direct callee capture has no frozen local binding: " << path;
          return failure();
        }
        if (includeCalleeReadSensitivity && readCaptures.contains(path) &&
            (!excludeWrittenSensitivity || !writtenCaptures.contains(path)))
          recordSensitivity(capture);
        operands.push_back(capture);
      }
  bool dpiTask = dpi::has<dpi::Task>(op);
  sim::SimFuncOp directCallee =
      lookupNearestSymbolFrom<sim::SimFuncOp>(op, callee);
  const UnitLoweringInputs::Callable *callable =
      loweringInputs.getCallable(directCallee);
  bool voidFunction = callable && callable->voidFunction;
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
      callResultTypes.push_back(copyOut.resultType);
  SmallVector<Value> callResults;
  if (IntegerAttr importID = dpi::get<dpi::ImportId>(op)) {
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
      signature.push_back(dpi::ABIAttr::get(
          builder.getContext(), static_cast<dpi::ABIKind>(*resultCategory),
          dpi::ArgumentDirection::Result, *width, isDPIFourState(resultType),
          isSignedSemanticType(semanticResult.getValue())));
      dpiOpenArrayLayouts.push_back(builder.getUnitAttr());
      dpiAggregateLayouts.push_back(builder.getUnitAttr());
    }
    for (const CopyOut &copyOut : copyOuts) {
      if (copyOut.dpiWidth == 0)
        return emitError(location)
                   << "DPI copy-out has no fixed transport width",
               failure();
      signature.push_back(dpi::ABIAttr::get(
          builder.getContext(), static_cast<dpi::ABIKind>(copyOut.dpiCategory),
          dpi::ArgumentDirection::Output, copyOut.dpiWidth,
          copyOut.dpiFourState, copyOut.formalSigned));
      dpiOpenArrayLayouts.push_back(copyOut.openArray
                                        ? Attribute(copyOut.openArray)
                                        : Attribute(builder.getUnitAttr()));
      dpiAggregateLayouts.push_back(copyOut.aggregate
                                        ? Attribute(copyOut.aggregate)
                                        : Attribute(builder.getUnitAttr()));
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
        dpi::get<dpi::CIdentifier>(op), dpi::get<dpi::ScopeId>(op),
        builder.getArrayAttr(signature),
        builder.getBoolAttr(dpi::has<dpi::Pure>(op)),
        builder.getBoolAttr(dpi::has<dpi::Context>(op)),
        builder.getBoolAttr(dpiTask), builder.getStringAttr(sourceFile),
        builder.getI32IntegerAttr(sourceLine),
        builder.getI32IntegerAttr(sourceColumn), runtimeContext, operands,
        builder.getArrayAttr(dpiOpenArrayLayouts),
        builder.getArrayAttr(dpiAggregateLayouts),
        /*logical_inputs=*/IntegerAttr{});
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
      for (auto candidate :
           virtualCallees.getAsRange<sim::VirtualInterfaceCalleeAttr>()) {
        Block *matched = addBlock();
        Block *next = addBlock();
        Value expected = arith::ConstantOp::create(
            builder, location, builder.getI64Type(),
            builder.getI64IntegerAttr(candidate.getScope()));
        Value equal =
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                  virtualScope, expected);
        cf::CondBranchOp::create(builder, location, equal, matched,
                                 ValueRange{}, next, ValueRange{});
        setCurrent(matched);
        SmallVector<Value> candidateOperands(operands);
        for (Attribute captureAttr : candidate.getCaptures()) {
          StringRef path = cast<StringAttr>(captureAttr).getValue();
          Value capture = values.lookup(path);
          if (!capture)
            return emitError(location)
                       << "virtual-interface callee capture has no binding: "
                       << path,
                   failure();
          candidateOperands.push_back(capture);
        }
        llvm::StringSet<> candidateWrites;
        if (ArrayAttr writes = candidate.getWrites())
          for (Attribute writeAttr : writes)
            candidateWrites.insert(cast<StringAttr>(writeAttr).getValue());
        for (Attribute readAttr : candidate.getReads()) {
          StringRef path = cast<StringAttr>(readAttr).getValue();
          if (!includeCalleeReadSensitivity ||
              (excludeWrittenSensitivity && candidateWrites.contains(path)))
            continue;
          Value capture = values.lookup(path);
          if (capture)
            recordSensitivity(capture);
        }
        auto selected = sim::SimCallOp::create(
            builder, location, callResultTypes, candidate.getCallee(),
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
                                 schedule::ContinuationSiteAttr{},
                                 continuation);
    } else {
      for (auto candidate :
           virtualCallees.getAsRange<sim::VirtualInterfaceCalleeAttr>()) {
        Block *matched = addBlock();
        Block *next = addBlock();
        Value expected = arith::ConstantOp::create(
            builder, location, builder.getI64Type(),
            builder.getI64IntegerAttr(candidate.getScope()));
        Value equal =
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                  virtualScope, expected);
        cf::CondBranchOp::create(builder, location, equal, matched,
                                 ValueRange{}, next, ValueRange{});
        setCurrent(matched);
        SmallVector<Value> candidateOperands(operands);
        for (Attribute captureAttr : candidate.getCaptures()) {
          StringRef path = cast<StringAttr>(captureAttr).getValue();
          Value capture = values.lookup(path);
          if (!capture)
            return emitError(location)
                       << "virtual-interface task capture has no binding: "
                       << path,
                   failure();
          candidateOperands.push_back(capture);
        }
        llvm::StringSet<> candidateWrites;
        if (ArrayAttr writes = candidate.getWrites())
          for (Attribute writeAttr : writes)
            candidateWrites.insert(cast<StringAttr>(writeAttr).getValue());
        for (Attribute readAttr : candidate.getReads()) {
          StringRef path = cast<StringAttr>(readAttr).getValue();
          if (!includeCalleeReadSensitivity ||
              (excludeWrittenSensitivity && candidateWrites.contains(path)))
            continue;
          Value capture = values.lookup(path);
          if (capture)
            recordSensitivity(capture);
        }
        sim::SimTaskCallOp::create(
            builder, location, candidate.getCallee(), candidateOperands,
            builder.getI64IntegerAttr(candidateOperands.size()),
            schedule::ContinuationSiteAttr{}, continuation);
        setCurrent(next);
      }
      if (failed(emitRuntimeFatal(
              location,
              "virtual interface task call used a null or invalid handle.")))
        return failure();
    }
    setCurrent(continuation);
  }
  if (!directTask && failed(emitTerminationPoll(location)))
    return failure();
  if (!directTask) {
    for (auto [index, copyOut] : llvm::enumerate(copyOuts)) {
      Value result = callResults[index + (hasFunctionResult ? 1 : 0)];
      if (!copyOut.destination) {
        if (failed(copyOutToActual(copyOut.actual, {}, result,
                                   copyOut.formalSigned, location)))
          return failure();
        continue;
      }
      Type destinationType = getReferenceElementType(copyOut.destination);
      if (!destinationType)
        return failure();
      FailureOr<Value> converted =
          copyOut.openArray &&
                  sim::getDPIAggregateBitStreamImportPlan(destinationType)
              ? convertFixedBitstreamTarget(result, destinationType, location,
                                            false, true)
              : convert(result, destinationType, copyOut.formalSigned, location,
                        copyOut.destinationSigned);
      if (failed(converted))
        return failure();
      if (failed(storeReference(copyOut.destination, *converted, location)))
        return failure();
    }
  } else {
    for (const CopyOut &copyOut : copyOuts) {
      FailureOr<Value> returned =
          loadReference(copyOut.taskDestination, location);
      if (failed(returned) ||
          failed(copyOutToActual(copyOut.actual, {}, *returned,
                                 copyOut.formalSigned, location)))
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
  auto recordReadCaptures = [&](Operation *source) {
    if (auto reads = sim::get<sim::metadata::CalleeReadCaptures>(source))
      for (Attribute readAttr : reads) {
        StringRef path = cast<StringAttr>(readAttr).getValue();
        Value capture = values.lookup(path);
        if (!capture)
          capture = lvalues.lookup(path);
        if (capture)
          recordSensitivity(capture);
      }
  };
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
          lookupNearestSymbolFrom<sim::SimClassDeclOp>(
              function, receiverType.getClassName());
      if (!derived || !derived.getBaseAttr()) {
        emitError(location) << "implicit super.new has no resolved base class";
        return failure();
      }
      declaration = lookupNearestSymbolFrom<sim::SimClassDeclOp>(
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
      declaration = lookupNearestSymbolFrom<sim::SimClassDeclOp>(
          function, receiverType.getClassName());
      receiver = sim::SimClassAllocOp::create(
          builder, location, receiverType,
          function.getBody().front().getArgument(0));
      if (failed(initializeObjectRandomStream(receiver, location)))
        return failure();
    }
    auto constructor = declaration ? declaration.getImplicitConstructorAttr()
                                   : FlatSymbolRefAttr{};
    if (!constructor) {
      emitError(location)
          << "implicit class constructor has no executable implementation";
      return failure();
    }
    SmallVector<Value> arguments;
    recordReadCaptures(op);
    if (auto captures = sim::get<sim::metadata::CalleeCaptures>(op))
      for (Attribute captureAttr : captures) {
        StringRef path = cast<StringAttr>(captureAttr).getValue();
        Value capture = values.lookup(path);
        if (!capture) {
          emitError(location)
              << "implicit constructor capture has no frozen local binding: "
              << path;
          return failure();
        }
        arguments.push_back(capture);
      }
    sim::SimClassDirectCallOp::create(builder, location, TypeRange{},
                                      constructor, receiver, arguments);
    if (failed(emitTerminationPoll(location)))
      return failure();
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
    descriptor->typeID = mailbox.getMessageTypeID();
    return sim::SimMailboxCreateOp::create(
               builder, location, mailbox, *bound, descriptor->typeID,
               descriptor->kind, descriptor->flags, descriptor->valueSize,
               descriptor->alignment, descriptor->bitWidth,
               builder.getDenseI64ArrayAttr(descriptor->traceOffsets),
               builder.getDenseI32ArrayAttr(descriptor->traceKinds))
        .getResult();
  }
  if (call && isSemaphoreCall(call) && call.getCalleeName() == "new") {
    SmallVector<Operation *> actuals = getChildren(call);
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    if (actuals.size() != 1 || failed(resultType) ||
        !isa<sim::SemaphoreType>(*resultType))
      return emitError(location)
                 << "semaphore constructor requires one resolved key count",
             failure();
    FailureOr<Value> keys = lowerExpression(actuals.front());
    keys = succeeded(keys)
               ? convert(*keys, builder.getI32Type(),
                         isSignedNode(actuals.front()), location, true)
               : FailureOr<Value>(failure());
    if (failed(keys))
      return failure();
    return sim::SimSemaphoreCreateOp::create(builder, location, *keys)
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
  auto callee =
      call ? sim::get<sim::metadata::Callee>(call) : FlatSymbolRefAttr{};
  auto formals =
      call ? sim::get<sim::metadata::CalleeFormals>(call) : ArrayAttr{};
  if (!call || !callee || !formals) {
    unsupported(op) << " (unresolved constructor"
                    << (!call ? ": missing call" : "")
                    << (call && !callee ? ": missing callee" : "")
                    << (call && !formals ? ": missing formals" : "") << ")";
    return failure();
  }

  SmallVector<Operation *> actuals = getChildren(call);
  std::optional<ArrayRef<int64_t>> defaulted = call.getDefaultedArguments();
  if (!defaulted || defaulted->size() != formals.size() ||
      llvm::any_of(*defaulted,
                   [](int64_t value) { return value != 0 && value != 1; })) {
    emitError(location)
        << "constructor has malformed default argument inventory";
    return failure();
  }
  if (actuals.size() != formals.size()) {
    emitError(location)
        << "constructor has no complete frozen formal inventory";
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

  struct ConstructorCopyOut {
    Operation *actual;
    Value scopeObject;
    Type formalType;
    bool formalSigned;
  };
  SmallVector<Value> arguments;
  SmallVector<ConstructorCopyOut> copyOuts;
  size_t formalIndex = 0;
  for (auto [actual, formalAttr] : llvm::zip_equal(actuals, formals)) {
    bool usesDefault = (*defaulted)[formalIndex++] != 0;
    Value savedThisObject = thisObject;
    if (usesDefault)
      thisObject = receiver;
    llvm::scope_exit restoreThisObject([&] { thisObject = savedThisObject; });
    auto formal = cast<sim::CalleeFormalAttr>(formalAttr);
    sim::FormalDirection direction = formal.getDirection();
    Type formalType = formal.getType();
    bool formalSigned = formal.getIsSigned();
    if (direction == sim::FormalDirection::In) {
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

    Operation *destination = getCopyOutActual(actual);
    if (direction == sim::FormalDirection::Ref) {
      FailureOr<Value> destinationRef = lowerExpression(destination, true);
      if (failed(destinationRef))
        return failure();
      if (!getReferenceElementType(*destinationRef))
        return emitError(location)
                   << "constructor ref actuals must be variable references",
               failure();
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
    if (direction == sim::FormalDirection::Out) {
      bool staticFormal = formal.getIsStatic();
      initial = staticFormal && isa<sim::EventType>(formalType)
                    ? sim::SimEventNullOp::create(builder, location, formalType)
                          .getResult()
                    : createDefaultValue(builder, location, formalType);
      if (!initial)
        return emitError(location)
                   << "cannot materialize a constructor output-formal "
                      "default for type "
                   << formalType,
               failure();
    } else {
      FailureOr<Value> copied =
          copyInFromActual(destination, formalType, formalSigned, location);
      if (failed(copied))
        return failure();
      initial = *copied;
    }
    arguments.push_back(cloneSequentialValue(initial, location));
    copyOuts.push_back({destination, usesDefault ? receiver : Value(),
                        formalType, formalSigned});
  }
  recordReadCaptures(call);
  if (auto captures = sim::get<sim::metadata::CalleeCaptures>(call))
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
  auto constructor = lookupNearestSymbolFrom<sim::SimFuncOp>(function, callee);
  const UnitLoweringInputs::Callable *signature =
      loweringInputs.getCallable(constructor);
  if (!signature || signature->type.getNumInputs() < 2 ||
      !isa<sim::ClassHandleType>(signature->type.getInput(1))) {
    emitError(location) << "constructor implementation has no this parameter";
    return failure();
  }
  FailureOr<Value> adjustedReceiver =
      convert(receiver, signature->type.getInput(1), false, location);
  if (failed(adjustedReceiver))
    return failure();
  SmallVector<Type> resultTypes;
  for (const ConstructorCopyOut &copyOut : copyOuts)
    resultTypes.push_back(copyOut.formalType);
  if (signature->type.getNumResults() != resultTypes.size()) {
    emitError(location)
        << "constructor implementation has inconsistent copy-out results";
    return failure();
  }
  auto invocation = sim::SimClassDirectCallOp::create(
      builder, location, resultTypes, callee, *adjustedReceiver, arguments);
  if (failed(emitTerminationPoll(location)))
    return failure();
  for (auto [result, copyOut] :
       llvm::zip_equal(invocation.getResults(), copyOuts))
    if (failed(copyOutToActual(copyOut.actual, copyOut.scopeObject, result,
                               copyOut.formalSigned, location)))
      return failure();
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
      lookupNearestSymbolFrom<sim::SimClassDeclOp>(function,
                                                   objectType.getClassName());
  while (declaration &&
         !sim::has<sim::metadata::RandomStateField>(declaration)) {
    if (!declaration.getBaseAttr())
      break;
    declaration = lookupNearestSymbolFrom<sim::SimClassDeclOp>(
        function, declaration.getBaseAttr());
  }
  auto stateField = declaration
                        ? sim::get<sim::metadata::RandomStateField>(declaration)
                        : FlatSymbolRefAttr{};
  auto incrementField =
      declaration ? sim::get<sim::metadata::RandomIncrementField>(declaration)
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
