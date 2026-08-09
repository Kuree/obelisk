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
#include <tuple>

using namespace mlir;

namespace obelisk::simlowering {
namespace {

bool isWeakReferenceCall(semantic::SVCallExpressionOp op) {
  auto path = op->getAttrOfType<StringAttr>("referenced_path");
  return path && path.getValue().starts_with("std::weak_reference#(");
}

} // namespace

FailureOr<Value> UnitLowering::lowerCall(semantic::SVCallExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (op->hasAttr(randomizeAttrName) ||
      op->hasAttr(randomizeDispatchAttrName))
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
    Operation *receiverNode = children.front();
    if (propertyIndexAttr) {
      auto member =
          dyn_cast<semantic::SVMemberAccessExpressionOp>(children.front());
      SmallVector<Operation *> memberChildren =
          member ? getChildren(member) : SmallVector<Operation *>{};
      if (!member || memberChildren.size() != 1) {
        emitError(location) << "property rand_mode has no object receiver";
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
    Type i64 = builder.getI64Type();
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
        mode = arith::SelectOp::create(builder, location, *enabled, enabledMode,
                                       disabledMode);
      } else {
        mode = arith::SelectOp::create(builder, location, *enabled, zero,
                                       disabled);
      }
      sim::SimManagedStoreOp::create(builder, location, mode, reference);
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
    Type i64 = builder.getI64Type();
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

      auto signature =
          op->getAttrOfType<IntegerAttr>("obelisk_sim.class_signature");
      auto receiverType = dyn_cast<sim::ClassHandleType>((*receiver).getType());
      sim::SimDesignOp design = function->getParentOfType<sim::SimDesignOp>();
      sim::SimClassDeclOp staticClass =
          receiverType
              ? SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
                    function, receiverType.getClassName())
              : sim::SimClassDeclOp{};
      if (!method || !signature || !design || !staticClass)
        return emitError(location)
                   << "virtual class task has no complete dispatch inventory",
               failure();

      SmallVector<sim::SimClassMethodDeclOp> methods(
          design.getBody().front().getOps<sim::SimClassMethodDeclOp>());
      auto lookupClass = [&](FlatSymbolRefAttr symbol) {
        return SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
            function, symbol);
      };
      auto derivesFrom = [&](sim::SimClassDeclOp candidate,
                             sim::SimClassDeclOp target) {
        for (sim::SimClassDeclOp current = candidate; current;) {
          if (current == target)
            return true;
          if (target.getIsInterface() && current.getInterfacesAttr())
            for (Attribute interface : current.getInterfacesAttr())
              if (cast<FlatSymbolRefAttr>(interface).getValue() ==
                  target.getSymName())
                return true;
          current = current.getBaseAttr() ? lookupClass(current.getBaseAttr())
                                          : sim::SimClassDeclOp{};
        }
        return false;
      };
      auto inheritanceDepth = [&](sim::SimClassDeclOp candidate) {
        uint64_t depth = 0;
        for (sim::SimClassDeclOp current = candidate; current;
             current = current.getBaseAttr()
                           ? lookupClass(current.getBaseAttr())
                           : sim::SimClassDeclOp{})
          ++depth;
        return depth;
      };
      struct TaskTarget {
        FlatSymbolRefAttr dynamicClass;
        FlatSymbolRefAttr implementation;
        uint64_t depth;
        uint64_t classId;
      };
      SmallVector<TaskTarget> targets;
      for (sim::SimClassDeclOp candidate :
           design.getBody().front().getOps<sim::SimClassDeclOp>()) {
        if (candidate.getIsAbstract() || candidate.getIsInterface() ||
            !derivesFrom(candidate, staticClass))
          continue;
        sim::SimClassMethodDeclOp implementation;
        for (sim::SimClassDeclOp current = candidate;
             current && !implementation;
             current = current.getBaseAttr()
                           ? lookupClass(current.getBaseAttr())
                           : sim::SimClassDeclOp{})
          for (sim::SimClassMethodDeclOp candidateMethod : methods)
            if (candidateMethod.getOwner() == current.getSymName() &&
                candidateMethod.getSignatureIdAttr() &&
                candidateMethod.getSignatureId() ==
                    signature.getValue().getZExtValue() &&
                candidateMethod.getImplementationAttr()) {
              implementation = candidateMethod;
              break;
            }
        if (implementation)
          targets.push_back({
              FlatSymbolRefAttr::get(function.getContext(),
                                     candidate.getSymName()),
              implementation.getImplementationAttr(),
              inheritanceDepth(candidate),
              candidate.getId(),
          });
      }
      llvm::sort(targets, [](const TaskTarget &lhs, const TaskTarget &rhs) {
        return std::tuple(lhs.depth, lhs.classId) >
               std::tuple(rhs.depth, rhs.classId);
      });
      if (targets.empty())
        return emitError(location)
                   << "virtual class task has no concrete implementation",
               failure();
      for (const TaskTarget &target : targets) {
        Value matches = sim::SimClassIsInstanceOp::create(
            builder, location, *receiver, target.dynamicClass);
        Block *invoke = addBlock();
        Block *next = addBlock();
        cf::CondBranchOp::create(builder, location, matches, invoke,
                                 ValueRange{}, next, ValueRange{});
        setCurrent(invoke);
        auto implementation =
            SymbolTable::lookupNearestSymbolFrom<sim::SimFuncOp>(
                function, target.implementation);
        if (!implementation ||
            implementation.getFunctionType().getNumInputs() < 2)
          return emitError(location)
                     << "virtual class task implementation is missing",
                 failure();
        Type expectedReceiver = implementation.getFunctionType().getInput(1);
        Value adjusted = *receiver;
        if (adjusted.getType() != expectedReceiver)
          adjusted = sim::SimClassCastOp::create(builder, location,
                                                 expectedReceiver, adjusted);
        emitTaskCall(target.implementation, adjusted);
        setCurrent(next);
      }
      Value verbosity =
          arith::ConstantOp::create(builder, location, builder.getI32Type(),
                                    builder.getI32IntegerAttr(1));
      sim::SimFatalOp::create(builder, location,
                              function.getBody().front().getArgument(0),
                              verbosity);
      emitBranch(continuation);
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
  if (!formals || formals.size() != children.size()) {
    emitError(location)
        << "direct call has no complete frozen formal inventory";
    return failure();
  }
  struct CopyOut {
    Value destination;
    Value taskDestination;
    Type formalType;
    bool formalSigned;
    bool destinationSigned;
    uint32_t dpiCategory;
  };
  SmallVector<CopyOut> copyOuts;
  SmallVector<Attribute> dpiOperandABI;
  for (auto [child, formalAttr] : llvm::zip_equal(children, formals)) {
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
      std::optional<unsigned> width = sim::getPackedWidth(formalType);
      if (!width) {
        emitError(location) << "DPI formal has no fixed packed integral width";
        return failure();
      }
      dpiOperandABI.push_back(sim::DPIABIAttr::get(
          builder.getContext(), static_cast<sim::DPIABIKind>(dpiCategory),
          static_cast<sim::DPIArgumentDirection>(direction), *width,
          isa<sim::LogicType>(sim::getPackedScalarType(formalType)),
          formalSigned));
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
  ValueRange callResults;
  if (auto importID = op->getAttrOfType<IntegerAttr>("obelisk.dpi.import_id")) {
    if (operands.empty())
      return emitError(location) << "DPI call is missing its runtime context",
             failure();
    Value runtimeContext = operands.front();
    operands.erase(operands.begin());
    SmallVector<Attribute> signature(dpiOperandABI);
    if (hasFunctionResult) {
      Type resultType = callResultTypes.front();
      std::optional<unsigned> width = sim::getPackedWidth(resultType);
      if (!width)
        return emitError(location)
                   << "DPI function result has no fixed packed width",
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
          sim::DPIArgumentDirection::Result, *width,
          isa<sim::LogicType>(sim::getPackedScalarType(resultType)),
          isSignedSemanticType(semanticResult.getValue())));
    }
    for (const CopyOut &copyOut : copyOuts) {
      std::optional<unsigned> width = sim::getPackedWidth(copyOut.formalType);
      signature.push_back(sim::DPIABIAttr::get(
          builder.getContext(),
          static_cast<sim::DPIABIKind>(copyOut.dpiCategory),
          sim::DPIArgumentDirection::Output, *width,
          isa<sim::LogicType>(sim::getPackedScalarType(copyOut.formalType)),
          copyOut.formalSigned));
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
    callResults = call.getResults().drop_back();
  } else if (!directTask) {
    auto call =
        sim::SimCallOp::create(builder, location, callResultTypes, callee,
                               operands, ArrayAttr{}, ArrayAttr{});
    callResults = call.getResults();
  } else {
    Block *continuation = addBlock();
    sim::SimTaskCallOp::create(builder, location, callee, operands,
                               builder.getI64IntegerAttr(operands.size()),
                               sim::ContinuationSiteAttr{}, continuation);
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
UnitLowering::lowerRandomize(semantic::SVCallExpressionOp op,
                             Value receiverOverride) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  if (op->hasAttr(randomizeDispatchAttrName)) {
    auto receiverIndexAttr =
        op->getAttrOfType<IntegerAttr>(randomReceiverIndexAttrName);
    FailureOr<Type> resultType = getNormalizedSemanticType(op);
    if (!receiverIndexAttr || failed(resultType) ||
        receiverIndexAttr.getValue().isNegative() ||
        receiverIndexAttr.getValue().getActiveBits() > 64 ||
        receiverIndexAttr.getValue().getZExtValue() >= children.size()) {
      emitError(location) << "randomize dispatch has malformed metadata";
      return failure();
    }
    unsigned receiverIndex =
        static_cast<unsigned>(receiverIndexAttr.getValue().getZExtValue());
    FailureOr<Value> loweredReceiver = receiverOverride
                                           ? FailureOr<Value>(receiverOverride)
                                           : lowerExpression(
                                                 children[receiverIndex]);
    if (failed(loweredReceiver) ||
        !isa<sim::ClassHandleType>((*loweredReceiver).getType())) {
      emitError(location) << "randomize dispatch receiver is not a class object";
      return failure();
    }

    SmallVector<semantic::SVCallExpressionOp> alternatives;
    for (Operation *child : children)
      if (auto alternative = dyn_cast<semantic::SVCallExpressionOp>(child);
          alternative &&
          alternative->hasAttr(randomizePlanClassAttrName) &&
          alternative->hasAttr(randomizeAttrName))
        alternatives.push_back(alternative);

    Block *done = addBlock();
    Value doneResult = done->addArgument(*resultType, location);
    for (semantic::SVCallExpressionOp alternative : alternatives) {
      auto planClass = alternative->getAttrOfType<FlatSymbolRefAttr>(
          randomizePlanClassAttrName);
      if (!planClass) {
        emitError(location) << "randomize alternative has no target class";
        return failure();
      }
      Value matches = sim::SimClassIsInstanceOp::create(
          builder, location, *loweredReceiver, planClass);
      Block *selected = addBlock();
      Block *next = addBlock();
      cf::CondBranchOp::create(builder, location, matches, selected,
                               ValueRange{}, next, ValueRange{});
      setCurrent(selected);
      FailureOr<Value> result = lowerRandomize(alternative, *loweredReceiver);
      if (failed(result))
        return failure();
      cf::BranchOp::create(builder, location, done, ValueRange{*result});
      setCurrent(next);
    }
    FailureOr<Value> noObject = convert(
        arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                  builder.getBoolAttr(false)),
        *resultType, false, location);
    if (failed(noObject))
      return failure();
    cf::BranchOp::create(builder, location, done, ValueRange{*noObject});
    setCurrent(done);
    return doneResult;
  }

  auto properties = op->getAttrOfType<ArrayAttr>(randomPropertiesAttrName);
  auto totalWidthAttr =
      op->getAttrOfType<IntegerAttr>(randomTotalWidthAttrName);
  auto receiverIndexAttr =
      op->getAttrOfType<IntegerAttr>(randomReceiverIndexAttrName);
  auto constraintCountAttr =
      op->getAttrOfType<IntegerAttr>(randomConstraintCountAttrName);
  if (children.empty() || !properties || !totalWidthAttr ||
      !receiverIndexAttr || !constraintCountAttr) {
    emitError(location) << "randomize call has no frozen constraint plan";
    return failure();
  }
  APInt receiverIndexValue = receiverIndexAttr.getValue();
  APInt totalWidthValue = totalWidthAttr.getValue();
  APInt constraintCountValue = constraintCountAttr.getValue();
  if (receiverIndexValue.isNegative() ||
      receiverIndexValue.getActiveBits() > 64 || totalWidthValue.isNegative() ||
      totalWidthValue.getActiveBits() > 64 ||
      constraintCountValue.isNegative() ||
      constraintCountValue.getActiveBits() > 64 ||
      receiverIndexValue.getZExtValue() >= children.size()) {
    emitError(location) << "randomize call has malformed constraint metadata";
    return failure();
  }
  unsigned receiverIndex =
      static_cast<unsigned>(receiverIndexValue.getZExtValue());
  uint64_t totalWidth = totalWidthValue.getZExtValue();
  uint64_t constraintCount = constraintCountValue.getZExtValue();
  bool checkerOnly = op->hasAttr(randomizeCheckerOnlyAttrName);
  if (totalWidth > 64 || constraintCount > 64) {
    emitError(location)
        << "randomize plan exceeds its 64-bit property or constraint boundary";
    return failure();
  }

  FailureOr<Value> loweredReceiver =
      receiverOverride ? FailureOr<Value>(receiverOverride)
                       : lowerExpression(children[receiverIndex]);
  if (receiverOverride) {
    auto planClass = op->getAttrOfType<FlatSymbolRefAttr>(
        randomizePlanClassAttrName);
    if (!planClass) {
      emitError(location) << "randomize receiver override has no target class";
      return failure();
    }
    Type targetType =
        sim::ClassHandleType::get(function.getContext(), planClass);
    if ((*loweredReceiver).getType() != targetType)
      loweredReceiver = sim::SimClassCastOp::create(
                            builder, location, targetType, *loweredReceiver)
                            .getResult();
  }
  auto objectType =
      succeeded(loweredReceiver)
          ? dyn_cast<sim::ClassHandleType>((*loweredReceiver).getType())
          : sim::ClassHandleType{};
  if (failed(loweredReceiver) || !objectType)
    return failure();
  Value receiver = *loweredReceiver;

  auto callLifecycleHook = [&](StringRef calleeAttr,
                               StringRef ownerAttr, StringRef capturesAttr,
                               StringRef readsAttr) -> LogicalResult {
    auto callee = op->getAttrOfType<FlatSymbolRefAttr>(calleeAttr);
    auto owner = op->getAttrOfType<FlatSymbolRefAttr>(ownerAttr);
    if (static_cast<bool>(callee) != static_cast<bool>(owner)) {
      emitError(location) << "randomize lifecycle hook metadata is incomplete";
      return failure();
    }
    if (!callee)
      return success();
    Type ownerType = sim::ClassHandleType::get(function.getContext(), owner);
    Value hookReceiver = receiver;
    if (hookReceiver.getType() != ownerType)
      hookReceiver = sim::SimClassCastOp::create(
          builder, location, ownerType, hookReceiver);
    llvm::StringSet<> readCaptures;
    if (auto reads = op->getAttrOfType<ArrayAttr>(readsAttr))
      for (Attribute read : reads)
        readCaptures.insert(cast<StringAttr>(read).getValue());
    SmallVector<Value> arguments;
    if (auto captures = op->getAttrOfType<ArrayAttr>(capturesAttr))
      for (Attribute captureAttr : captures) {
        StringRef path = cast<StringAttr>(captureAttr).getValue();
        Value capture = values.lookup(path);
        if (!capture) {
          emitError(location)
              << "randomization hook capture has no local binding: " << path;
          return failure();
        }
        if (readCaptures.contains(path))
          recordSensitivity(capture);
        arguments.push_back(capture);
      }
    sim::SimClassDirectCallOp::create(builder, location, TypeRange{}, callee,
                                      hookReceiver, arguments);
    return success();
  };
  if (!checkerOnly &&
      failed(callLifecycleHook(randomPreHookAttrName,
                               randomPreHookOwnerAttrName,
                               randomPreHookCapturesAttrName,
                               randomPreHookReadCapturesAttrName)))
    return failure();

  struct Property {
    Type type;
    unsigned width;
    bool isSigned;
    Value reference;
    bool isRandC;
    Value randcKeyReference;
    Value randcPositionReference;
    Value nextRandcKey;
    Value nextRandcPosition;
  };
  SmallVector<Property> planned;
  uint64_t plannedWidth = 0;
  Type i64 = builder.getI64Type();
  for (Attribute propertyAttr : properties) {
    auto property = dyn_cast<DictionaryAttr>(propertyAttr);
    auto field = property ? property.getAs<FlatSymbolRefAttr>("field")
                          : FlatSymbolRefAttr{};
    auto typeAttr = property ? property.getAs<TypeAttr>("type") : TypeAttr{};
    auto widthAttr =
        property ? property.getAs<IntegerAttr>("width") : IntegerAttr{};
    auto signedAttr =
        property ? property.getAs<BoolAttr>("is_signed") : BoolAttr{};
    auto randcAttr =
        property ? property.getAs<BoolAttr>("is_randc") : BoolAttr{};
    if (!field || !typeAttr || !widthAttr || !signedAttr || !randcAttr ||
        widthAttr.getValue().isZero() || widthAttr.getValue().isNegative() ||
        widthAttr.getValue().getActiveBits() > 64) {
      emitError(location) << "randomize property plan is malformed";
      return failure();
    }
    uint64_t width = widthAttr.getValue().getZExtValue();
    if (width > 64 - plannedWidth) {
      emitError(location) << "randomize property plan exceeds 64 bits";
      return failure();
    }
    Type type = typeAttr.getValue();
    std::optional<unsigned> typeWidth = sim::getPackedWidth(type);
    if (!typeWidth || *typeWidth != width) {
      emitError(location) << "randomize property type width is inconsistent";
      return failure();
    }
    Type referenceType = sim::ManagedRefType::get(function.getContext(), type,
                                                  objectType.getClassName());
    Value reference = sim::SimClassFieldRefOp::create(
        builder, location, referenceType, receiver, field);
    Value randcKeyReference;
    Value randcPositionReference;
    if (randcAttr.getValue()) {
      auto keyField = property.getAs<FlatSymbolRefAttr>("randc_key_field");
      auto positionField =
          property.getAs<FlatSymbolRefAttr>("randc_position_field");
      if (width > 32 || !keyField || !positionField) {
        emitError(location) << "randc property plan is malformed";
        return failure();
      }
      Type stateReferenceType = sim::ManagedRefType::get(
          function.getContext(), i64, objectType.getClassName());
      randcKeyReference = sim::SimClassFieldRefOp::create(
          builder, location, stateReferenceType, receiver, keyField);
      randcPositionReference = sim::SimClassFieldRefOp::create(
          builder, location, stateReferenceType, receiver, positionField);
    }
    planned.push_back({type, static_cast<unsigned>(width),
                       signedAttr.getValue(), reference, randcAttr.getValue(),
                       randcKeyReference, randcPositionReference, {}, {}});
    plannedWidth += width;
  }
  if (plannedWidth != totalWidth) {
    emitError(location) << "randomize property plan width is inconsistent";
    return failure();
  }
  bool hasRandC = llvm::any_of(
      planned, [](const Property &property) { return property.isRandC; });

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
  auto modeField = declaration ? declaration->getAttrOfType<FlatSymbolRefAttr>(
                                     "obelisk_sim.random_mode_field")
                               : FlatSymbolRefAttr{};
  auto constraintModeField =
      declaration ? declaration->getAttrOfType<FlatSymbolRefAttr>(
                        "obelisk_sim.constraint_mode_field")
                  : FlatSymbolRefAttr{};
  if (!declaration || !stateField || !incrementField || !modeField ||
      !constraintModeField) {
    emitError(location)
        << "randomize receiver has no object-local stream or mode state";
    return failure();
  }
  Type randomReferenceType = sim::ManagedRefType::get(
      function.getContext(), i64, objectType.getClassName());
  Value stateReference = sim::SimClassFieldRefOp::create(
      builder, location, randomReferenceType, receiver, stateField);
  Value incrementReference = sim::SimClassFieldRefOp::create(
      builder, location, randomReferenceType, receiver, incrementField);
  Value modeReference = sim::SimClassFieldRefOp::create(
      builder, location, randomReferenceType, receiver, modeField);
  Value constraintModeReference = sim::SimClassFieldRefOp::create(
      builder, location, randomReferenceType, receiver, constraintModeField);
  Value state =
      sim::SimManagedLoadOp::create(builder, location, i64, stateReference);
  Value increment =
      sim::SimManagedLoadOp::create(builder, location, i64, incrementReference);
  Value mode =
      sim::SimManagedLoadOp::create(builder, location, i64, modeReference);
  Value constraintMode = sim::SimManagedLoadOp::create(
      builder, location, i64, constraintModeReference);

  auto constant64 = [&](uint64_t value) -> Value {
    return arith::ConstantOp::create(
        builder, location, i64, builder.getIntegerAttr(i64, APInt(64, value)));
  };
  APInt domainMask = totalWidth == 64 ? APInt::getAllOnes(64)
                                      : APInt::getLowBitsSet(64, totalWidth);
  Value mask = arith::ConstantOp::create(
      builder, location, i64, builder.getIntegerAttr(i64, domainMask));
  uint64_t propertyModeMask =
      planned.size() == 64 ? UINT64_MAX : (uint64_t{1} << planned.size()) - 1;
  Value relevantMode =
      checkerOnly
          ? constant64(propertyModeMask)
          : arith::AndIOp::create(builder, location, mode,
                                  constant64(propertyModeMask))
                .getResult();
  Value randomizationEnabled = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, relevantMode, constant64(0));
  Value allPropertiesDisabled =
      arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                            relevantMode, constant64(propertyModeMask));
  uint64_t constraintModeMask =
      constraintCount == 64 ? UINT64_MAX
                            : (uint64_t{1} << constraintCount) - 1;
  Value relevantConstraintMode = arith::AndIOp::create(
      builder, location, constraintMode, constant64(constraintModeMask));
  Value allConstraintsEnabled = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, relevantConstraintMode,
      constant64(0));

  auto next32 = [&](Value &streamState) -> Value {
    Value old = streamState;
    streamState = arith::AddIOp::create(
        builder, location,
        arith::MulIOp::create(builder, location, old,
                              constant64(UINT64_C(6364136223846793005))),
        increment);
    Value xored = arith::XOrIOp::create(
        builder, location,
        arith::ShRUIOp::create(builder, location, old, constant64(18)), old);
    Value shifted =
        arith::ShRUIOp::create(builder, location, xored, constant64(27));
    Type i32 = builder.getI32Type();
    Value bits = arith::TruncIOp::create(builder, location, i32, shifted);
    Value rotation = arith::TruncIOp::create(
        builder, location, i32,
        arith::ShRUIOp::create(builder, location, old, constant64(59)));
    Value zero32 = arith::ConstantOp::create(builder, location, i32,
                                             builder.getI32IntegerAttr(0));
    Value thirtyOne = arith::ConstantOp::create(builder, location, i32,
                                                builder.getI32IntegerAttr(31));
    Value leftAmount = arith::AndIOp::create(
        builder, location,
        arith::SubIOp::create(builder, location, zero32, rotation), thirtyOne);
    return arith::OrIOp::create(
        builder, location,
        arith::ShRUIOp::create(builder, location, bits, rotation),
        arith::ShLIOp::create(builder, location, bits, leftAmount));
  };
  auto next64 = [&](Value &streamState) -> Value {
    Value high = next32(streamState);
    Value low = next32(streamState);
    return arith::OrIOp::create(
        builder, location,
        arith::ShLIOp::create(
            builder, location,
            arith::ExtUIOp::create(builder, location, i64, high),
            constant64(32)),
        arith::ExtUIOp::create(builder, location, i64, low));
  };

  Value currentAssignment = constant64(0);
  Value mutableMask = constant64(0);
  SmallVector<Value> propertyEnabled;
  uint64_t currentOffset = 0;
  for (auto [index, property] : llvm::enumerate(planned)) {
    Value current = sim::SimManagedLoadOp::create(
        builder, location, property.type, property.reference);
    FailureOr<Value> scalar = toPackedScalar(current, location);
    FailureOr<Value> extended =
        succeeded(scalar)
            ? convert(*scalar, i64, property.isSigned, location, false)
            : FailureOr<Value>(failure());
    if (failed(extended))
      return failure();
    uint64_t valueMask =
        property.width == 64 ? UINT64_MAX : (uint64_t{1} << property.width) - 1;
    Value bits = arith::AndIOp::create(builder, location, *extended,
                                       constant64(valueMask));

    Value propertyMode = arith::AndIOp::create(
        builder, location, relevantMode, constant64(uint64_t{1} << index));
    Value enabled =
        checkerOnly
            ? arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                        builder.getBoolAttr(false))
                  .getResult()
            : arith::CmpIOp::create(builder, location,
                                    arith::CmpIPredicate::eq, propertyMode,
                                    constant64(0))
                  .getResult();
    propertyEnabled.push_back(enabled);
    if (property.isRandC) {
      Block *enabledBlock = addBlock();
      Block *disabledBlock = addBlock();
      Block *mergeBlock = addBlock();
      Value mergedState = mergeBlock->addArgument(i64, location);
      Value mergedBits = mergeBlock->addArgument(i64, location);
      Value mergedKey = mergeBlock->addArgument(i64, location);
      Value mergedPosition = mergeBlock->addArgument(i64, location);
      cf::CondBranchOp::create(builder, location, enabled, enabledBlock,
                               ValueRange{}, disabledBlock, ValueRange{});

      setCurrent(disabledBlock);
      cf::BranchOp::create(builder, location, mergeBlock,
                           ValueRange{state, bits, constant64(0), constant64(0)});

      setCurrent(enabledBlock);
      Value key = sim::SimManagedLoadOp::create(
          builder, location, i64, property.randcKeyReference);
      Value position = sim::SimManagedLoadOp::create(
          builder, location, i64, property.randcPositionReference);
      Value needsRekey = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, position, constant64(0));
      Block *rekeyBlock = addBlock();
      Block *cycleBlock = addBlock();
      Value cycleKey = cycleBlock->addArgument(i64, location);
      Value cycleState = cycleBlock->addArgument(i64, location);
      cf::CondBranchOp::create(builder, location, needsRekey, rekeyBlock,
                               ValueRange{}, cycleBlock,
                               ValueRange{key, state});

      setCurrent(rekeyBlock);
      Value rekeyState = state;
      Value newKey = next64(rekeyState);
      cf::BranchOp::create(builder, location, cycleBlock,
                           ValueRange{newKey, rekeyState});

      setCurrent(cycleBlock);
      auto cycle = sim::SimRandomCycleNextOp::create(
          builder, location, cycleKey, position,
          builder.getI32IntegerAttr(property.width));
      cf::BranchOp::create(builder, location, mergeBlock,
                           ValueRange{cycleState, cycle.getValue(), cycleKey,
                                      cycle.getNextPosition()});

      setCurrent(mergeBlock);
      state = mergedState;
      bits = mergedBits;
      property.nextRandcKey = mergedKey;
      property.nextRandcPosition = mergedPosition;
    }
    uint64_t aggregateMask = valueMask;
    if (currentOffset != 0) {
      bits = arith::ShLIOp::create(builder, location, bits,
                                   constant64(currentOffset));
      aggregateMask <<= currentOffset;
    }
    currentAssignment =
        arith::OrIOp::create(builder, location, currentAssignment, bits);

    Value enabledMask =
        property.isRandC
            ? constant64(0)
            : arith::SelectOp::create(builder, location, enabled,
                                      constant64(aggregateMask), constant64(0))
                  .getResult();
    mutableMask =
        arith::OrIOp::create(builder, location, mutableMask, enabledMask);
    currentOffset += property.width;
  }
  Value fixedAssignment = arith::AndIOp::create(
      builder, location, currentAssignment,
      arith::XOrIOp::create(builder, location, mutableMask, mask));

  bool hasSoftConstraint = false;
  for (auto [index, child] : llvm::enumerate(children)) {
    if (index == receiverIndex)
      continue;
    child->walk([&](semantic::SVExpressionConstraintOp expression) {
      hasSoftConstraint |= expression.getIsSoft();
    });
  }

  SmallVector<SmallVector<unsigned>> solveBeforeLayers;
  struct SolveBeforeEdge {
    unsigned before;
    unsigned after;
    uint32_t constraintBlock;

    bool operator==(const SolveBeforeEdge &other) const {
      return before == other.before && after == other.after &&
             constraintBlock == other.constraintBlock;
    }
  };
  SmallVector<SolveBeforeEdge> solveBeforeEdges;
  bool hasSolveBefore = false;
  auto randomPropertyIndex = [&](Operation *expression) -> FailureOr<unsigned> {
    auto indexAttr =
        expression->getAttrOfType<IntegerAttr>(randomVariableAttrName);
    if (!indexAttr || indexAttr.getValue().isNegative() ||
        indexAttr.getValue().getActiveBits() > 64 ||
        indexAttr.getValue().getZExtValue() >= planned.size()) {
      emitError(getSemanticLocation(expression))
          << "solve before currently requires direct rand properties";
      return failure();
    }
    return static_cast<unsigned>(indexAttr.getValue().getZExtValue());
  };
  for (auto [index, root] : llvm::enumerate(children)) {
    if (index == receiverIndex)
      continue;
    uint32_t solveConstraintBlock = OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1;
    if (auto block =
            root->getAttrOfType<IntegerAttr>(randomConstraintBlockAttrName)) {
      APInt value = block.getValue();
      if (value.isNegative() || value.getActiveBits() > 64 ||
          value.getZExtValue() >= constraintCount) {
        emitError(getSemanticLocation(root))
            << "random constraint block index is malformed";
        return failure();
      }
      solveConstraintBlock = static_cast<uint32_t>(value.getZExtValue());
    }
    WalkResult result =
        root->walk([&](semantic::SVSolveBeforeConstraintOp solve) {
          hasSolveBefore = true;
          auto solveCountAttr =
              solve->getAttrOfType<IntegerAttr>("solve_count");
          auto afterCountAttr =
              solve->getAttrOfType<IntegerAttr>("after_count");
          SmallVector<Operation *> operands = getChildren(solve);
          if (!solveCountAttr || !afterCountAttr ||
              solveCountAttr.getValue().isNegative() ||
              afterCountAttr.getValue().isNegative() ||
              solveCountAttr.getValue().getActiveBits() > 64 ||
              afterCountAttr.getValue().getActiveBits() > 64) {
            emitError(getSemanticLocation(solve))
                << "solve before has malformed operand counts";
            return WalkResult::interrupt();
          }
          uint64_t solveCount = solveCountAttr.getValue().getZExtValue();
          uint64_t afterCount = afterCountAttr.getValue().getZExtValue();
          if (solveCount == 0 || afterCount == 0 ||
              solveCount > operands.size() ||
              afterCount != operands.size() - solveCount) {
            emitError(getSemanticLocation(solve))
                << "solve before has inconsistent operand counts";
            return WalkResult::interrupt();
          }
          SmallVector<unsigned> before;
          SmallVector<unsigned> after;
          for (auto [operandIndex, operand] : llvm::enumerate(operands)) {
            FailureOr<unsigned> property = randomPropertyIndex(operand);
            if (failed(property))
              return WalkResult::interrupt();
            (operandIndex < solveCount ? before : after).push_back(*property);
          }
          for (unsigned lhs : before)
            for (unsigned rhs : after) {
              if (lhs == rhs) {
                emitError(getSemanticLocation(solve))
                    << "solve before cannot order a property before itself";
                return WalkResult::interrupt();
              }
              SolveBeforeEdge edge{lhs, rhs, solveConstraintBlock};
              if (!llvm::is_contained(solveBeforeEdges, edge))
                solveBeforeEdges.push_back(edge);
            }
          return WalkResult::advance();
        });
    if (result.wasInterrupted())
      return failure();
  }
  if (hasSolveBefore) {
    SmallVector<bool> involved(planned.size(), false);
    SmallVector<bool> emitted(planned.size(), false);
    for (const SolveBeforeEdge &edge : solveBeforeEdges) {
      involved[edge.before] = true;
      involved[edge.after] = true;
    }
    size_t remaining = llvm::count(involved, true);
    while (remaining != 0) {
      SmallVector<unsigned> layer;
      for (unsigned property = 0; property != planned.size(); ++property) {
        if (!involved[property] || emitted[property])
          continue;
        bool hasPredecessor =
            llvm::any_of(solveBeforeEdges, [&](const auto &edge) {
              return edge.after == property && !emitted[edge.before];
            });
        if (!hasPredecessor)
          layer.push_back(property);
      }
      if (layer.empty()) {
        emitError(location) << "solve before ordering contains a cycle";
        return failure();
      }
      for (unsigned property : layer)
        emitted[property] = true;
      remaining -= layer.size();
      solveBeforeLayers.push_back(std::move(layer));
    }
  }

  struct EncodedInstruction {
    uint8_t opcode;
    uint8_t width;
    uint8_t flags = 0;
    uint32_t operand = 0;
    uint64_t immediate = 0;
  };
  SmallVector<EncodedInstruction> programInstructions;
  SmallVector<Value> programCaptures;
  auto instruction = [&](uint8_t opcode, unsigned width, bool isSigned = false,
                         uint32_t operand = 0, uint64_t immediate = 0) {
    programInstructions.push_back(
        {opcode, static_cast<uint8_t>(width),
         static_cast<uint8_t>(isSigned ? OBELISK_RT_RANDOM_INSTRUCTION_SIGNED
                                       : 0),
         operand, immediate});
  };
  auto expressionWidth = [&](Operation *expression) -> FailureOr<unsigned> {
    FailureOr<Type> type = getNormalizedSemanticType(expression);
    std::optional<unsigned> width =
        succeeded(type) ? sim::getPackedWidth(*type) : std::nullopt;
    if (!width || *width == 0 || *width > 64) {
      emitError(getSemanticLocation(expression))
          << "runtime random constraint values must be packed and no wider "
             "than 64 bits";
      return failure();
    }
    return *width;
  };
  auto dependsOnCandidate = [&](Operation *expression) {
    bool dependent = false;
    expression->walk([&](Operation *nested) {
      dependent |= nested->hasAttr(randomVariableAttrName);
    });
    return dependent;
  };
  struct DistRangePlan {
    uint64_t lower;
    uint64_t cardinality;
    uint64_t coefficient;
    uint64_t selectionCoefficient;
    uint32_t weightCapture;
    bool weightSigned;
    Value weight;
  };
  struct DistPlan {
    Operation *source;
    unsigned propertyIndex;
    uint32_t propertyOffset;
    uint32_t constraintBlock;
    SmallVector<DistRangePlan> ranges;
  };
  SmallVector<DistPlan> distPlans;
  uint32_t activeProgramConstraintBlock =
      OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1;
  auto emitLiteral = [&](bool value) {
    instruction(OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, false, 0, value);
  };
  std::function<LogicalResult(Operation *)> emitProgramExpression;
  emitProgramExpression = [&](Operation *expression) -> LogicalResult {
    if (auto dist = dyn_cast<semantic::SVDistExpressionOp>(expression)) {
      if (llvm::is_contained(llvm::map_range(
                                 distPlans,
                                 [](const DistPlan &plan) { return plan.source; }),
                             expression)) {
        emitError(getSemanticLocation(expression))
            << "distribution expression was encoded more than once";
        return failure();
      }
      SmallVector<Operation *> nested = getChildren(expression);
      auto itemCountAttr =
          expression->getAttrOfType<IntegerAttr>("item_count");
      auto itemHasWeight =
          expression->getAttrOfType<DenseI64ArrayAttr>("item_has_weight");
      auto itemWeightKinds =
          expression->getAttrOfType<DenseI64ArrayAttr>("item_weight_kinds");
      auto hasDefaultWeight =
          expression->getAttrOfType<BoolAttr>("has_default_weight");
      auto defaultWeightKind =
          expression->getAttrOfType<IntegerAttr>("default_weight_kind");
      if (!itemCountAttr || itemCountAttr.getValue().isNegative() ||
          itemCountAttr.getValue().getActiveBits() > 64 || !itemHasWeight ||
          !itemWeightKinds || !hasDefaultWeight || !defaultWeightKind) {
        emitError(getSemanticLocation(expression))
            << "distribution expression has malformed metadata";
        return failure();
      }
      uint64_t itemCount = itemCountAttr.getValue().getZExtValue();
      if (itemCount == 0 || itemCount > static_cast<uint64_t>(INT64_MAX) ||
          itemHasWeight.size() != static_cast<int64_t>(itemCount) ||
          itemWeightKinds.size() != static_cast<int64_t>(itemCount) ||
          nested.empty() ||
          (defaultWeightKind.getInt() != 0 &&
           defaultWeightKind.getInt() != 1)) {
        emitError(getSemanticLocation(expression))
            << "distribution expression has inconsistent metadata";
        return failure();
      }

      Operation *target = nested.front();
      while (isa<semantic::SVConversionExpressionOp>(target)) {
        SmallVector<Operation *> converted = getChildren(target);
        if (converted.size() != 1)
          break;
        target = converted.front();
      }
      auto variable = target->getAttrOfType<IntegerAttr>(randomVariableAttrName);
      if (!variable || variable.getValue().isNegative() ||
          variable.getValue().getActiveBits() > 64 ||
          variable.getValue().getZExtValue() >= planned.size()) {
        emitError(getSemanticLocation(expression))
            << "dist currently requires a direct rand property on its left "
               "hand side";
        return failure();
      }
      unsigned propertyIndex =
          static_cast<unsigned>(variable.getValue().getZExtValue());
      const Property &property = planned[propertyIndex];
      FailureOr<unsigned> comparisonWidth = expressionWidth(nested.front());
      if (property.isRandC) {
        emitError(getSemanticLocation(expression))
            << "dist cannot weight a randc property";
        return failure();
      }
      if (failed(comparisonWidth) || *comparisonWidth < property.width ||
          isSignedNode(nested.front()) != property.isSigned) {
        emitError(getSemanticLocation(expression))
            << "dist requires a widening conversion that preserves the rand "
               "property signedness";
        return failure();
      }

      struct RawDistItem {
        Operation *value;
        Operation *weight;
        bool perRange;
      };
      SmallVector<RawDistItem> rawItems;
      size_t childIndex = 1;
      for (uint64_t index = 0; index != itemCount; ++index) {
        if (childIndex >= nested.size() ||
            (itemHasWeight[index] != 0 && itemHasWeight[index] != 1) ||
            (itemWeightKinds[index] != 0 && itemWeightKinds[index] != 1)) {
          emitError(getSemanticLocation(expression))
              << "distribution item metadata is malformed";
          return failure();
        }
        Operation *value = nested[childIndex++];
        Operation *weight = nullptr;
        if (itemHasWeight[index] != 0) {
          if (childIndex >= nested.size())
            return failure();
          weight = nested[childIndex++];
        }
        rawItems.push_back(
            {value, weight, itemWeightKinds[index] != 0});
      }
      Operation *defaultWeight = nullptr;
      if (hasDefaultWeight.getValue()) {
        if (childIndex >= nested.size()) {
          emitError(getSemanticLocation(expression))
              << "distribution default weight is missing";
          return failure();
        }
        defaultWeight = nested[childIndex++];
      }
      if (childIndex != nested.size()) {
        emitError(getSemanticLocation(expression))
            << "distribution expression has an unexpected child inventory";
        return failure();
      }

      std::function<FailureOr<ParsedConstant>(Operation *)> constantValue;
      constantValue = [&](Operation *constant) -> FailureOr<ParsedConstant> {
        if (isa<semantic::SVConversionExpressionOp>(constant)) {
          SmallVector<Operation *> converted = getChildren(constant);
          if (converted.size() != 1)
            return failure();
          return constantValue(converted.front());
        }
        if (auto unary = dyn_cast<semantic::SVUnaryExpressionOp>(constant)) {
          SmallVector<Operation *> operand = getChildren(unary);
          if (operand.size() != 1)
            return failure();
          FailureOr<ParsedConstant> value = constantValue(operand.front());
          if (failed(value))
            return failure();
          switch (unary.getOperatorKind()) {
          case semantic::SVUnaryOperator::Plus:
            return *value;
          case semantic::SVUnaryOperator::Minus:
            value->value = -value->value;
            return *value;
          case semantic::SVUnaryOperator::BitwiseNot:
            value->value = ~value->value;
            return *value;
          default:
            return failure();
          }
        }
        std::optional<StringRef> spelling = getConstantSpelling(constant);
        if (!spelling)
          return failure();
        return parseSVInteger(*spelling, *comparisonWidth,
                              getSemanticLocation(constant));
      };
      auto constantBits = [&](Operation *value) -> FailureOr<uint64_t> {
        FailureOr<ParsedConstant> parsed = constantValue(value);
        if (failed(parsed)) {
          emitError(getSemanticLocation(value))
              << "dist range endpoints must be compile-time integral "
                 "constants";
          return failure();
        }
        if (!parsed->unknown.isZero()) {
          emitError(getSemanticLocation(value))
              << "dist range endpoints must be two-state constants";
          return failure();
        }
        APInt bits = parsed->value;
        if (property.isSigned) {
          APInt minimum =
              APInt::getSignedMinValue(property.width).sext(*comparisonWidth);
          APInt maximum =
              APInt::getSignedMaxValue(property.width).sext(*comparisonWidth);
          if (bits.slt(minimum) || bits.sgt(maximum)) {
            emitError(getSemanticLocation(value))
                << "dist endpoint is outside the rand property domain";
            return failure();
          }
        } else {
          APInt maximum = APInt::getLowBitsSet(*comparisonWidth, property.width);
          if (bits.ugt(maximum)) {
            emitError(getSemanticLocation(value))
                << "dist endpoint is outside the rand property domain";
            return failure();
          }
        }
        uint64_t propertyBits = bits.trunc(property.width).getZExtValue();
        if (property.isSigned)
          propertyBits ^= uint64_t{1} << (property.width - 1);
        return propertyBits;
      };

      struct PendingRange {
        uint64_t lower;
        uint64_t cardinality;
        Operation *weight;
        bool perRange;
        uint64_t denominator;
      };
      SmallVector<PendingRange> pending;
      SmallVector<std::pair<uint64_t, uint64_t>> explicitIntervals;
      auto appendItemRange = [&](const RawDistItem &item) -> LogicalResult {
        uint64_t lower = 0;
        uint64_t upper = 0;
        if (auto range = dyn_cast<semantic::SVValueRangeExpressionOp>(item.value)) {
          SmallVector<Operation *> endpoints = getChildren(range);
          if (endpoints.size() != 2) {
            emitError(getSemanticLocation(item.value))
                << "dist range has malformed endpoints";
            return failure();
          }
          FailureOr<uint64_t> first = constantBits(endpoints[0]);
          FailureOr<uint64_t> second = constantBits(endpoints[1]);
          if (failed(first) || failed(second))
            return failure();
          lower = std::min(*first, *second);
          upper = std::max(*first, *second);
        } else {
          FailureOr<uint64_t> singleton = constantBits(item.value);
          if (failed(singleton))
            return failure();
          lower = upper = *singleton;
        }
        uint64_t cardinality = upper - lower + 1;
        if (cardinality == 0 && item.perRange) {
          emitError(getSemanticLocation(item.value))
              << "a per-range dist weight cannot span the complete 64-bit "
                 "domain";
          return failure();
        }
        pending.push_back(
            {lower, cardinality, item.weight, item.perRange, cardinality});
        explicitIntervals.emplace_back(lower, upper);
        return success();
      };
      for (const RawDistItem &item : rawItems)
        if (failed(appendItemRange(item)))
          return failure();

      if (defaultWeight) {
        llvm::sort(explicitIntervals);
        SmallVector<std::pair<uint64_t, uint64_t>> merged;
        for (auto interval : explicitIntervals) {
          if (merged.empty() ||
              (merged.back().second != UINT64_MAX &&
               interval.first > merged.back().second + 1)) {
            merged.push_back(interval);
          } else {
            merged.back().second =
                std::max(merged.back().second, interval.second);
          }
        }
        uint64_t domainMaximum = property.width == 64
                                     ? UINT64_MAX
                                     : (uint64_t{1} << property.width) - 1;
        SmallVector<std::pair<uint64_t, uint64_t>> complement;
        uint64_t next = 0;
        bool exhausted = false;
        for (auto interval : merged) {
          if (next < interval.first)
            complement.emplace_back(next, interval.first - 1);
          if (interval.second == UINT64_MAX) {
            exhausted = true;
            break;
          }
          next = interval.second + 1;
        }
        if (!exhausted && next <= domainMaximum)
          complement.emplace_back(next, domainMaximum);
        uint64_t defaultCardinality = 0;
        for (auto interval : complement) {
          uint64_t cardinality = interval.second - interval.first + 1;
          uint64_t updated = defaultCardinality + cardinality;
          if (updated < defaultCardinality)
            defaultCardinality = 0;
          else
            defaultCardinality = updated;
        }
        bool defaultPerRange = defaultWeightKind.getInt() != 0;
        if (defaultPerRange && !complement.empty() &&
            defaultCardinality == 0) {
          emitError(getSemanticLocation(defaultWeight))
              << "a per-range default dist weight cannot cover the complete "
                 "64-bit domain";
          return failure();
        }
        for (auto interval : complement)
          pending.push_back({interval.first,
                             interval.second - interval.first + 1,
                             defaultWeight, defaultPerRange,
                             defaultCardinality});
      }

      uint64_t normalization = 1;
      for (const PendingRange &range : pending) {
        if (!range.perRange)
          continue;
        uint64_t divisor = std::gcd(normalization, range.denominator);
        uint64_t factor = range.denominator / divisor;
        if (normalization > UINT64_MAX / factor) {
          emitError(getSemanticLocation(expression))
              << "dist per-range normalization exceeds 64 bits";
          return failure();
        }
        normalization *= factor;
      }

      llvm::DenseMap<Operation *, std::pair<uint32_t, Value>> weights;
      std::optional<std::pair<uint32_t, Value>> implicitWeight;
      auto materializeWeight = [&](Operation *weight,
                                   bool &isSigned) -> FailureOr<std::pair<uint32_t, Value>> {
        if (weight) {
          if (dependsOnCandidate(weight)) {
            emitError(getSemanticLocation(weight))
                << "dist weights cannot depend on randomized properties";
            return failure();
          }
          if (auto found = weights.find(weight); found != weights.end()) {
            isSigned = isSignedNode(weight);
            return found->second;
          }
          FailureOr<Value> lowered = lowerExpression(weight);
          FailureOr<Value> scalar =
              succeeded(lowered)
                  ? toPackedScalar(*lowered, getSemanticLocation(weight))
                  : FailureOr<Value>(failure());
          if (failed(scalar) || !isa<IntegerType>((*scalar).getType())) {
            emitError(getSemanticLocation(weight))
                << "dist weights must be packed integral values";
            return failure();
          }
          isSigned = isSignedNode(weight);
          FailureOr<Value> extended = convert(
              *scalar, i64, isSigned, getSemanticLocation(weight), false);
          if (failed(extended))
            return failure();
          uint32_t capture = static_cast<uint32_t>(programCaptures.size());
          programCaptures.push_back(*extended);
          auto result = std::make_pair(capture, *extended);
          weights[weight] = result;
          return result;
        }
        isSigned = false;
        if (!implicitWeight) {
          uint32_t capture = static_cast<uint32_t>(programCaptures.size());
          Value one = constant64(1);
          programCaptures.push_back(one);
          implicitWeight = std::make_pair(capture, one);
        }
        return *implicitWeight;
      };

      DistPlan plan{expression, propertyIndex, 0,
                    activeProgramConstraintBlock, {}};
      for (unsigned index = 0; index != propertyIndex; ++index)
        plan.propertyOffset += planned[index].width;
      for (const PendingRange &range : pending) {
        bool weightSigned = false;
        FailureOr<std::pair<uint32_t, Value>> weight =
            materializeWeight(range.weight, weightSigned);
        if (failed(weight))
          return failure();
        uint64_t coefficient =
            range.perRange ? normalization / range.denominator : normalization;
        if (range.cardinality == 0 ||
            coefficient > UINT64_MAX / range.cardinality) {
          emitError(getSemanticLocation(expression))
              << "dist range selection mass exceeds 64 bits";
          return failure();
        }
        uint64_t selectionCoefficient = coefficient * range.cardinality;
        plan.ranges.push_back(
            {range.lower, range.cardinality, coefficient,
             selectionCoefficient, weight->first, weightSigned,
             weight->second});
      }
      if (plan.ranges.empty()) {
        emitError(getSemanticLocation(expression))
            << "distribution has no supported values";
        return failure();
      }
      distPlans.push_back(std::move(plan));

      auto emitWeightPositive = [&](Operation *weight) -> LogicalResult {
        bool weightSigned = false;
        FailureOr<std::pair<uint32_t, Value>> materialized =
            materializeWeight(weight, weightSigned);
        if (failed(materialized))
          return failure();
        instruction(OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, 64, weightSigned,
                    materialized->first);
        instruction(OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 64, false, 0, 0);
        instruction(OBELISK_RT_RANDOM_GT_V1, 1, weightSigned);
        return success();
      };
      auto emitMatch = [&](Operation *value) -> LogicalResult {
        if (auto range = dyn_cast<semantic::SVValueRangeExpressionOp>(value)) {
          SmallVector<Operation *> endpoints = getChildren(range);
          if (endpoints.size() != 2)
            return failure();
          FailureOr<uint64_t> first = constantBits(endpoints[0]);
          FailureOr<uint64_t> second = constantBits(endpoints[1]);
          if (failed(first) || failed(second))
            return failure();
          if (*first > *second)
            std::swap(endpoints[0], endpoints[1]);
          if (
              failed(emitProgramExpression(nested.front())) ||
              failed(emitProgramExpression(endpoints[0])))
            return failure();
          instruction(OBELISK_RT_RANDOM_GE_V1, 1,
                      isSignedNode(nested.front()));
          if (failed(emitProgramExpression(nested.front())) ||
              failed(emitProgramExpression(endpoints[1])))
            return failure();
          instruction(OBELISK_RT_RANDOM_LE_V1, 1,
                      isSignedNode(nested.front()));
          instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
          return success();
        }
        if (failed(emitProgramExpression(nested.front())) ||
            failed(emitProgramExpression(value)))
          return failure();
        instruction(OBELISK_RT_RANDOM_EQ_V1, 1,
                    isSignedNode(nested.front()));
        return success();
      };

      bool firstSupport = true;
      for (const RawDistItem &item : rawItems) {
        if (failed(emitMatch(item.value)) ||
            failed(emitWeightPositive(item.weight)))
          return failure();
        instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
        if (!firstSupport)
          instruction(OBELISK_RT_RANDOM_LOGICAL_OR_V1, 1);
        firstSupport = false;
      }
      if (defaultWeight) {
        bool firstExplicit = true;
        for (const RawDistItem &item : rawItems) {
          if (failed(emitMatch(item.value)))
            return failure();
          if (!firstExplicit)
            instruction(OBELISK_RT_RANDOM_LOGICAL_OR_V1, 1);
          firstExplicit = false;
        }
        instruction(OBELISK_RT_RANDOM_LOGICAL_NOT_V1, 1);
        if (failed(emitWeightPositive(defaultWeight)))
          return failure();
        instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
        if (!firstSupport)
          instruction(OBELISK_RT_RANDOM_LOGICAL_OR_V1, 1);
        firstSupport = false;
      }
      if (firstSupport)
        emitLiteral(false);
      return success();
    }
    FailureOr<unsigned> width = expressionWidth(expression);
    if (failed(width))
      return failure();
    if (auto variable =
            expression->getAttrOfType<IntegerAttr>(randomVariableAttrName)) {
      APInt indexValue = variable.getValue();
      if (indexValue.isNegative() || indexValue.getActiveBits() > 64 ||
          indexValue.getZExtValue() >= planned.size()) {
        emitError(getSemanticLocation(expression))
            << "random constraint variable index is malformed";
        return failure();
      }
      unsigned index = static_cast<unsigned>(indexValue.getZExtValue());
      uint32_t offset = 0;
      for (unsigned current = 0; current != index; ++current)
        offset += planned[current].width;
      instruction(OBELISK_RT_RANDOM_PUSH_VARIABLE_V1, planned[index].width,
                  planned[index].isSigned, offset);
      return success();
    }
    if (std::optional<StringRef> spelling = getConstantSpelling(expression)) {
      FailureOr<ParsedConstant> parsed =
          parseSVInteger(*spelling, *width, getSemanticLocation(expression));
      if (failed(parsed) || !parsed->unknown.isZero()) {
        emitError(getSemanticLocation(expression))
            << "four-state constants are not executable in the runtime "
               "random solver";
        return failure();
      }
      instruction(OBELISK_RT_RANDOM_PUSH_LITERAL_V1, *width,
                  isSignedNode(expression), 0, parsed->value.getZExtValue());
      return success();
    }
    SmallVector<Operation *> nested = getChildren(expression);
    if (isa<semantic::SVConversionExpressionOp>(expression)) {
      if (nested.size() != 1 || failed(emitProgramExpression(nested.front())))
        return failure();
      instruction(OBELISK_RT_RANDOM_CAST_V1, *width,
                  isSignedNode(nested.front()));
      return success();
    }
    if (!dependsOnCandidate(expression)) {
      FailureOr<Value> value = lowerExpression(expression);
      FailureOr<Value> scalar =
          succeeded(value)
              ? toPackedScalar(*value, getSemanticLocation(expression))
              : FailureOr<Value>(failure());
      if (failed(scalar) || !isa<IntegerType>((*scalar).getType())) {
        emitError(getSemanticLocation(expression))
            << "four-state and non-integral runtime constraint captures are "
               "not executable yet";
        return failure();
      }
      FailureOr<Value> extended =
          convert(*scalar, builder.getI64Type(), false,
                  getSemanticLocation(expression), false);
      if (failed(extended))
        return failure();
      uint32_t capture = static_cast<uint32_t>(programCaptures.size());
      programCaptures.push_back(*extended);
      instruction(OBELISK_RT_RANDOM_PUSH_CAPTURE_V1, *width,
                  isSignedNode(expression), capture);
      return success();
    }

    if (auto unary = dyn_cast<semantic::SVUnaryExpressionOp>(expression)) {
      if (nested.size() != 1 || failed(emitProgramExpression(nested.front())))
        return failure();
      uint8_t opcode = 0;
      using Unary = semantic::SVUnaryOperator;
      switch (unary.getOperatorKind()) {
      case Unary::Plus:
        opcode = OBELISK_RT_RANDOM_POS_V1;
        break;
      case Unary::Minus:
        opcode = OBELISK_RT_RANDOM_NEG_V1;
        break;
      case Unary::BitwiseNot:
        opcode = OBELISK_RT_RANDOM_BIT_NOT_V1;
        break;
      case Unary::BitwiseAnd:
        opcode = OBELISK_RT_RANDOM_REDUCE_AND_V1;
        break;
      case Unary::BitwiseOr:
        opcode = OBELISK_RT_RANDOM_REDUCE_OR_V1;
        break;
      case Unary::BitwiseXor:
        opcode = OBELISK_RT_RANDOM_REDUCE_XOR_V1;
        break;
      case Unary::BitwiseNand:
        opcode = OBELISK_RT_RANDOM_REDUCE_NAND_V1;
        break;
      case Unary::BitwiseNor:
        opcode = OBELISK_RT_RANDOM_REDUCE_NOR_V1;
        break;
      case Unary::BitwiseXnor:
        opcode = OBELISK_RT_RANDOM_REDUCE_XNOR_V1;
        break;
      case Unary::LogicalNot:
        opcode = OBELISK_RT_RANDOM_LOGICAL_NOT_V1;
        break;
      default:
        return failure();
      }
      instruction(opcode, *width, isSignedNode(expression));
      return success();
    }
    if (auto binary = dyn_cast<semantic::SVBinaryExpressionOp>(expression)) {
      if (nested.size() != 2 || failed(emitProgramExpression(nested[0])) ||
          failed(emitProgramExpression(nested[1])))
        return failure();
      uint8_t opcode = 0;
      bool signedOperation = isSignedNode(nested.front());
      using Binary = semantic::SVBinaryOperator;
      switch (binary.getOperatorKind()) {
      case Binary::Add:
        opcode = OBELISK_RT_RANDOM_ADD_V1;
        break;
      case Binary::Subtract:
        opcode = OBELISK_RT_RANDOM_SUB_V1;
        break;
      case Binary::Multiply:
        opcode = OBELISK_RT_RANDOM_MUL_V1;
        break;
      case Binary::Divide:
      case Binary::Mod: {
        FailureOr<unsigned> divisorWidth = expressionWidth(nested[1]);
        std::optional<StringRef> spelling = getConstantSpelling(nested[1]);
        FailureOr<ParsedConstant> divisor =
            succeeded(divisorWidth) && spelling
                ? parseSVInteger(*spelling, *divisorWidth,
                                 getSemanticLocation(nested[1]))
                : FailureOr<ParsedConstant>(failure());
        if (failed(divisor) || !divisor->unknown.isZero() ||
            divisor->value.isZero()) {
          emitError(getSemanticLocation(expression))
              << "runtime random division and modulo require a statically "
                 "nonzero divisor";
          return failure();
        }
        opcode = binary.getOperatorKind() == Binary::Divide
                     ? OBELISK_RT_RANDOM_DIV_V1
                     : OBELISK_RT_RANDOM_MOD_V1;
        break;
      }
      case Binary::BinaryAnd:
        opcode = OBELISK_RT_RANDOM_BIT_AND_V1;
        break;
      case Binary::BinaryOr:
        opcode = OBELISK_RT_RANDOM_BIT_OR_V1;
        break;
      case Binary::BinaryXor:
        opcode = OBELISK_RT_RANDOM_BIT_XOR_V1;
        break;
      case Binary::BinaryXnor:
        opcode = OBELISK_RT_RANDOM_BIT_XNOR_V1;
        break;
      case Binary::Equality:
      case Binary::CaseEquality:
      case Binary::WildcardEquality:
        opcode = OBELISK_RT_RANDOM_EQ_V1;
        break;
      case Binary::Inequality:
      case Binary::CaseInequality:
      case Binary::WildcardInequality:
        opcode = OBELISK_RT_RANDOM_NE_V1;
        break;
      case Binary::GreaterThanEqual:
        opcode = OBELISK_RT_RANDOM_GE_V1;
        break;
      case Binary::GreaterThan:
        opcode = OBELISK_RT_RANDOM_GT_V1;
        break;
      case Binary::LessThanEqual:
        opcode = OBELISK_RT_RANDOM_LE_V1;
        break;
      case Binary::LessThan:
        opcode = OBELISK_RT_RANDOM_LT_V1;
        break;
      case Binary::LogicalAnd:
        opcode = OBELISK_RT_RANDOM_LOGICAL_AND_V1;
        break;
      case Binary::LogicalOr:
        opcode = OBELISK_RT_RANDOM_LOGICAL_OR_V1;
        break;
      case Binary::LogicalImplication:
        opcode = OBELISK_RT_RANDOM_LOGICAL_IMPLIES_V1;
        break;
      case Binary::LogicalEquivalence:
        opcode = OBELISK_RT_RANDOM_LOGICAL_EQUIV_V1;
        break;
      case Binary::LogicalShiftLeft:
      case Binary::ArithmeticShiftLeft:
        opcode = OBELISK_RT_RANDOM_SHIFT_LEFT_V1;
        break;
      case Binary::LogicalShiftRight:
        opcode = OBELISK_RT_RANDOM_SHIFT_RIGHT_V1;
        break;
      case Binary::ArithmeticShiftRight:
        opcode = isSignedNode(nested[0])
                     ? OBELISK_RT_RANDOM_SHIFT_RIGHT_ARITH_V1
                     : OBELISK_RT_RANDOM_SHIFT_RIGHT_V1;
        break;
      case Binary::Power:
        if (isSignedNode(nested[1])) {
          FailureOr<unsigned> exponentWidth = expressionWidth(nested[1]);
          std::optional<StringRef> spelling = getConstantSpelling(nested[1]);
          FailureOr<ParsedConstant> exponent =
              succeeded(exponentWidth) && spelling
                  ? parseSVInteger(*spelling, *exponentWidth,
                                   getSemanticLocation(nested[1]))
                  : FailureOr<ParsedConstant>(failure());
          if (failed(exponent) || !exponent->unknown.isZero() ||
              exponent->value.isNegative()) {
            emitError(getSemanticLocation(expression))
                << "runtime random integral power requires an unsigned or "
                   "statically nonnegative exponent";
            return failure();
          }
        }
        opcode = OBELISK_RT_RANDOM_POWER_V1;
        break;
      default:
        emitError(getSemanticLocation(expression))
            << "operator is not encoded by the runtime random solver";
        return failure();
      }
      instruction(opcode, *width, signedOperation);
      return success();
    }
    if (isa<semantic::SVConditionalExpressionOp>(expression)) {
      if (nested.size() != 3 || failed(emitProgramExpression(nested[0])) ||
          failed(emitProgramExpression(nested[1])) ||
          failed(emitProgramExpression(nested[2])))
        return failure();
      instruction(OBELISK_RT_RANDOM_SELECT_V1, *width,
                  isSignedNode(expression));
      return success();
    }
    if (isa<semantic::SVInsideExpressionOp>(expression)) {
      if (nested.size() < 2)
        return failure();
      bool first = true;
      for (Operation *item : ArrayRef(nested).drop_front()) {
        if (auto range = dyn_cast<semantic::SVValueRangeExpressionOp>(item)) {
          SmallVector<Operation *> endpoints = getChildren(range);
          if (endpoints.size() != 2 ||
              failed(emitProgramExpression(nested.front())) ||
              failed(emitProgramExpression(endpoints[0])))
            return failure();
          instruction(OBELISK_RT_RANDOM_GE_V1, 1, isSignedNode(nested.front()));
          if (failed(emitProgramExpression(nested.front())) ||
              failed(emitProgramExpression(endpoints[1])))
            return failure();
          instruction(OBELISK_RT_RANDOM_LE_V1, 1, isSignedNode(nested.front()));
          instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
        } else {
          if (failed(emitProgramExpression(nested.front())) ||
              failed(emitProgramExpression(item)))
            return failure();
          instruction(OBELISK_RT_RANDOM_EQ_V1, 1, isSignedNode(nested.front()));
        }
        if (!first)
          instruction(OBELISK_RT_RANDOM_LOGICAL_OR_V1, 1);
        first = false;
      }
      return success();
    }
    emitError(getSemanticLocation(expression))
        << "candidate-dependent expression is not encoded by the runtime "
           "random solver: "
        << expression->getName();
    return failure();
  };

  std::function<LogicalResult(Operation *, Operation *)> emitProgramConstraint;
  emitProgramConstraint = [&](Operation *constraint,
                              Operation *softTarget) -> LogicalResult {
    SmallVector<Operation *> nested = getChildren(constraint);
    if (isa<semantic::SVConstraintListOp>(constraint)) {
      if (nested.empty()) {
        emitLiteral(true);
        return success();
      }
      if (failed(emitProgramConstraint(nested.front(), softTarget)))
        return failure();
      for (Operation *item : ArrayRef(nested).drop_front()) {
        if (failed(emitProgramConstraint(item, softTarget)))
          return failure();
        instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
      }
      return success();
    }
    if (isa<semantic::SVSolveBeforeConstraintOp>(constraint)) {
      emitLiteral(true);
      return success();
    }
    if (auto expression =
            dyn_cast<semantic::SVExpressionConstraintOp>(constraint)) {
      bool selected = softTarget ? softTarget == constraint
                                 : !expression.getIsSoft();
      if (!selected) {
        emitLiteral(true);
        return success();
      }
      return nested.size() == 1 ? emitProgramExpression(nested.front())
                                : failure();
    }
    if (isa<semantic::SVImplicationConstraintOp>(constraint)) {
      if (nested.size() != 2 || failed(emitProgramExpression(nested[0])) ||
          failed(emitProgramConstraint(nested[1], softTarget)))
        return failure();
      instruction(OBELISK_RT_RANDOM_LOGICAL_IMPLIES_V1, 1);
      return success();
    }
    if (auto conditional =
            dyn_cast<semantic::SVConditionalConstraintOp>(constraint)) {
      if (nested.size() != (conditional.getHasElse() ? 3u : 2u) ||
          failed(emitProgramExpression(nested[0])) ||
          failed(emitProgramConstraint(nested[1], softTarget)))
        return failure();
      if (conditional.getHasElse()) {
        if (failed(emitProgramConstraint(nested[2], softTarget)))
          return failure();
      } else {
        emitLiteral(true);
      }
      instruction(OBELISK_RT_RANDOM_SELECT_V1, 1);
      return success();
    }
    if (isa<semantic::SVUniquenessConstraintOp>(constraint)) {
      if (softTarget) {
        emitLiteral(true);
        return success();
      }
      bool first = true;
      for (size_t left = 0; left != nested.size(); ++left)
        for (size_t right = left + 1; right != nested.size(); ++right) {
          if (failed(emitProgramExpression(nested[left])) ||
              failed(emitProgramExpression(nested[right])))
            return failure();
          instruction(OBELISK_RT_RANDOM_NE_V1, 1, isSignedNode(nested[left]));
          if (!first)
            instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
          first = false;
        }
      if (first)
        emitLiteral(true);
      return success();
    }
    emitError(getSemanticLocation(constraint))
        << "constraint is not encoded by the runtime random solver: "
        << constraint->getName();
    return failure();
  };

  // Encode and analyze the fallback program before synthesizing the tier-0
  // CFG. Candidate-independent captures are materialized once in the dispatch
  // block and shared with the runtime fallback. Keeping the analysis here also
  // lets the generated proposal consume conservative compiler-side domains.
  Value programSavedThis = thisObject;
  SmallVector<Value> programSavedCandidates =
      std::move(randomizeCandidateValues);
  llvm::scope_exit restoreProgramBindings([&] {
    thisObject = programSavedThis;
    randomizeCandidateValues = std::move(programSavedCandidates);
  });
  thisObject = receiver;
  bool emittedHard = false;
  bool emittedSoft = false;
  llvm::DenseMap<Operation *, uint64_t> softPriorities;
  uint64_t nextSoftPriority = 0;
  auto assignSoftPriorities = [&](bool inlineConstraints) {
    for (auto [index, root] : llvm::enumerate(children)) {
      if (index == receiverIndex ||
          root->hasAttr(randomConstraintBlockAttrName) == inlineConstraints)
        continue;
      SmallVector<Operation *> items = isa<semantic::SVConstraintListOp>(root)
                                           ? getChildren(root)
                                           : SmallVector<Operation *>{root};
      for (Operation *item : items)
        item->walk([&](semantic::SVExpressionConstraintOp expression) {
          if (expression.getIsSoft())
            softPriorities[expression] = nextSoftPriority++;
        });
    }
  };
  // Later declarations have higher priority. Class constraints are frozen in
  // base-to-derived declaration order, and every inline constraint has higher
  // priority than the class constraints it augments.
  assignSoftPriorities(/*inlineConstraints=*/false);
  assignSoftPriorities(/*inlineConstraints=*/true);
  for (auto [index, root] : llvm::enumerate(children)) {
    if (index == receiverIndex)
      continue;
    uint32_t constraintBlock = OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1;
    if (auto block =
            root->getAttrOfType<IntegerAttr>(randomConstraintBlockAttrName)) {
      APInt value = block.getValue();
      if (value.isNegative() || value.getActiveBits() > 64 ||
          value.getZExtValue() >= constraintCount) {
        emitError(getSemanticLocation(root))
            << "random constraint block index is malformed";
        return failure();
      }
      constraintBlock = static_cast<uint32_t>(value.getZExtValue());
    }
    SmallVector<Operation *> items = isa<semantic::SVConstraintListOp>(root)
                                         ? getChildren(root)
                                         : SmallVector<Operation *>{root};
    for (Operation *item : items) {
      bool hasHard = false;
      SmallVector<semantic::SVExpressionConstraintOp> softConstraints;
      item->walk([&](Operation *nested) {
        if (auto expression =
                dyn_cast<semantic::SVExpressionConstraintOp>(nested)) {
          if (expression.getIsSoft())
            softConstraints.push_back(expression);
          else
            hasHard = true;
        } else if (isa<semantic::SVUniquenessConstraintOp>(nested)) {
          hasHard = true;
        }
      });
      if (hasHard) {
        activeProgramConstraintBlock = constraintBlock;
        if (failed(emitProgramConstraint(item, /*softTarget=*/nullptr)))
          return failure();
        instruction(OBELISK_RT_RANDOM_END_HARD_V1, 1, false,
                    constraintBlock);
        emittedHard = true;
      }
      for (semantic::SVExpressionConstraintOp soft : softConstraints) {
        bool containsDist = false;
        soft->walk([&](semantic::SVDistExpressionOp) { containsDist = true; });
        if (containsDist) {
          emitError(getSemanticLocation(soft))
              << "soft dist constraints are not executable yet";
          return failure();
        }
        activeProgramConstraintBlock = constraintBlock;
        if (failed(emitProgramConstraint(item, soft)))
          return failure();
        instruction(OBELISK_RT_RANDOM_END_SOFT_V1, 1, false, constraintBlock,
                    softPriorities.lookup(soft));
        emittedSoft = true;
      }
    }
  }
  if (!emittedHard) {
    emitLiteral(true);
    instruction(OBELISK_RT_RANDOM_END_HARD_V1, 1, false,
                OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1);
  }
  if (!distPlans.empty() && hasSolveBefore) {
    emitError(location)
        << "dist combined with solve before is not executable yet";
    return failure();
  }
  for (auto [index, plan] : llvm::enumerate(distPlans))
    for (const DistPlan &other : ArrayRef(distPlans).drop_front(index + 1))
      if (plan.propertyIndex == other.propertyIndex) {
        emitError(getSemanticLocation(other.source))
            << "multiple active dist constraints for one property are not "
               "executable yet";
        return failure();
      }
  thisObject = programSavedThis;
  randomizeCandidateValues = std::move(programSavedCandidates);
  restoreProgramBindings.release();

  SmallVector<uint8_t> program;
  auto append16 = [&](uint16_t value) {
    program.push_back(static_cast<uint8_t>(value));
    program.push_back(static_cast<uint8_t>(value >> 8));
  };
  auto append32 = [&](uint32_t value) {
    for (unsigned index = 0; index != 4; ++index)
      program.push_back(static_cast<uint8_t>(value >> (index * 8)));
  };
  auto append64 = [&](uint64_t value) {
    for (unsigned index = 0; index != 8; ++index)
      program.push_back(static_cast<uint8_t>(value >> (index * 8)));
  };
  append32(OBELISK_RT_RANDOM_PROGRAM_MAGIC);
  append16(OBELISK_RT_RANDOM_PROGRAM_VERSION);
  append16(OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE);
  append32(static_cast<uint32_t>(totalWidth));
  append32(static_cast<uint32_t>(programInstructions.size()));
  append32(static_cast<uint32_t>(programCaptures.size()));
  uint32_t programFlags = emittedSoft ? OBELISK_RT_RANDOM_PROGRAM_HAS_SOFT : 0;
  if (hasSolveBefore)
    programFlags |= OBELISK_RT_RANDOM_PROGRAM_HAS_SOLVE_BEFORE;
  if (!distPlans.empty())
    programFlags |= OBELISK_RT_RANDOM_PROGRAM_HAS_DIST;
  append32(programFlags);
  for (const EncodedInstruction &encoded : programInstructions) {
    program.push_back(encoded.opcode);
    program.push_back(encoded.width);
    program.push_back(encoded.flags);
    program.push_back(0);
    append32(encoded.operand);
    append64(encoded.immediate);
  }
  if (hasSolveBefore) {
    append32(static_cast<uint32_t>(solveBeforeEdges.size()));
    SmallVector<uint64_t> propertyMasks;
    uint64_t offset = 0;
    for (const Property &property : planned) {
      uint64_t valueMask = property.width == 64
                               ? UINT64_MAX
                               : (uint64_t{1} << property.width) - 1;
      propertyMasks.push_back(valueMask << offset);
      offset += property.width;
    }
    for (const SolveBeforeEdge &edge : solveBeforeEdges) {
      append64(propertyMasks[edge.before]);
      append64(propertyMasks[edge.after]);
      append32(edge.constraintBlock);
      append32(0);
    }
  }
  if (!distPlans.empty()) {
    append32(static_cast<uint32_t>(distPlans.size()));
    uint32_t recordCount = 0;
    for (const DistPlan &plan : distPlans)
      recordCount += static_cast<uint32_t>(plan.ranges.size());
    append32(recordCount);
    for (auto [group, plan] : llvm::enumerate(distPlans)) {
      for (const DistRangePlan &range : plan.ranges) {
        append32(static_cast<uint32_t>(group));
        append32(plan.constraintBlock);
        append32(plan.propertyOffset);
        append16(static_cast<uint16_t>(planned[plan.propertyIndex].width));
        append16(0);
        append64(range.lower);
        append64(range.cardinality);
        append64(range.coefficient);
        append32(range.weightCapture);
        uint32_t flags = range.weightSigned
                             ? OBELISK_RT_RANDOM_DIST_WEIGHT_SIGNED
                             : 0;
        if (planned[plan.propertyIndex].isSigned)
          flags |= OBELISK_RT_RANDOM_DIST_TARGET_SIGNED;
        append32(flags);
      }
    }
  }
  uint64_t fallbackAttempts =
      totalWidth <= 20 ? (uint64_t{1} << totalWidth) : (uint64_t{1} << 20);
  solver::RandomProgramAnalysis analysis = solver::analyzeRandomProgram(
      program.data(), program.size(), /*resourceLimit=*/100000,
      /*preferGlobalAssignmentTable=*/hasSolveBefore);

  SmallVector<uint64_t> proposalAssignments;
  constexpr size_t maxMaterializedAssignmentTableSize = 16;
  uint64_t aggregateMask =
      totalWidth == 64 ? UINT64_MAX : (uint64_t{1} << totalWidth) - 1;
  bool validAssignmentTable =
      distPlans.empty() && analysis.assignmentTables.empty() &&
      !analysis.assignmentTable.empty() &&
      analysis.assignmentTable.size() <= maxMaterializedAssignmentTableSize &&
      llvm::all_of(analysis.assignmentTable, [&](uint64_t assignment) {
        return (assignment & ~aggregateMask) == 0;
      });
  bool powerOfTwoAssignmentTable =
      validAssignmentTable && (analysis.assignmentTable.size() &
                               (analysis.assignmentTable.size() - 1)) == 0;
  if (validAssignmentTable)
    proposalAssignments.append(analysis.assignmentTable.begin(),
                               analysis.assignmentTable.end());

  struct SolveBeforeTableNode {
    SmallVector<unsigned> children;
    SmallVector<uint64_t> assignments;
  };
  SmallVector<SolveBeforeTableNode> solveBeforeTableNodes;
  std::optional<unsigned> solveBeforeTableRoot;
  SmallVector<uint64_t> solveBeforeLayerMasks;
  std::function<unsigned(ArrayRef<uint64_t>, unsigned)> buildSolveBeforeTable;
  if (hasSolveBefore) {
    for (ArrayRef<unsigned> layer : solveBeforeLayers) {
      uint64_t mask = 0;
      uint64_t offset = 0;
      for (auto [propertyIndex, property] : llvm::enumerate(planned)) {
        uint64_t valueMask = property.width == 64
                                 ? UINT64_MAX
                                 : (uint64_t{1} << property.width) - 1;
        if (llvm::is_contained(layer, propertyIndex))
          mask |= valueMask << offset;
        offset += property.width;
      }
      solveBeforeLayerMasks.push_back(mask);
    }
    buildSolveBeforeTable = [&](ArrayRef<uint64_t> rows,
                                unsigned layer) -> unsigned {
      unsigned node = solveBeforeTableNodes.size();
      solveBeforeTableNodes.emplace_back();
      if (layer == solveBeforeLayerMasks.size()) {
        solveBeforeTableNodes[node].assignments.append(rows.begin(),
                                                       rows.end());
        return node;
      }
      SmallVector<std::pair<uint64_t, SmallVector<uint64_t>>> groups;
      for (uint64_t row : rows) {
        uint64_t key = row & solveBeforeLayerMasks[layer];
        auto group = llvm::find_if(
            groups, [&](const auto &entry) { return entry.first == key; });
        if (group == groups.end()) {
          groups.emplace_back(key, SmallVector<uint64_t>{});
          group = std::prev(groups.end());
        }
        group->second.push_back(row);
      }
      llvm::sort(groups, [](const auto &lhs, const auto &rhs) {
        return lhs.first < rhs.first;
      });
      SmallVector<unsigned> children;
      for (const auto &group : groups)
        children.push_back(buildSolveBeforeTable(group.second, layer + 1));
      solveBeforeTableNodes[node].children = std::move(children);
      return node;
    };
    if (validAssignmentTable && !hasSoftConstraint)
      solveBeforeTableRoot = buildSolveBeforeTable(proposalAssignments, 0);
  }

  struct ProposalAssignmentTable {
    uint64_t mask;
    SmallVector<uint64_t> assignments;
  };
  SmallVector<ProposalAssignmentTable> proposalAssignmentTables;
  uint64_t proposalAssignmentTableMask = 0;
  auto coversWholeProperties = [&](uint64_t tableMask) {
    uint64_t offset = 0;
    for (const Property &property : planned) {
      uint64_t valueMask = property.width == 64
                               ? UINT64_MAX
                               : (uint64_t{1} << property.width) - 1;
      uint64_t propertyMask = valueMask << offset;
      uint64_t overlap = tableMask & propertyMask;
      if (overlap != 0 && overlap != propertyMask)
        return false;
      offset += property.width;
    }
    return true;
  };
  // Component tables are sampled once and then held while the residual
  // proposal advances. A soft preference must instead be able to visit every
  // hard-legal table row, so retain the existing checker/fallback path there.
  bool validAssignmentTables = analysis.assignmentTable.empty() &&
                               distPlans.empty() &&
                               !hasSoftConstraint &&
                               !analysis.assignmentTables.empty();
  for (const solver::RandomAssignmentTable &table : analysis.assignmentTables) {
    bool valid =
        table.mask != 0 && (table.mask & ~aggregateMask) == 0 &&
        coversWholeProperties(table.mask) &&
        (table.mask & proposalAssignmentTableMask) == 0 &&
        !table.assignments.empty() &&
        table.assignments.size() <= maxMaterializedAssignmentTableSize &&
        llvm::all_of(table.assignments, [&](uint64_t assignment) {
          return (assignment & ~table.mask) == 0;
        });
    if (!valid) {
      validAssignmentTables = false;
      break;
    }
    proposalAssignmentTableMask |= table.mask;
    proposalAssignmentTables.push_back(
        {table.mask, SmallVector<uint64_t>(table.assignments.begin(),
                                           table.assignments.end())});
  }
  if (!validAssignmentTables) {
    proposalAssignmentTables.clear();
    proposalAssignmentTableMask = 0;
  }
  SmallVector<unsigned> solveBeforeComponentTableRoots;

  struct ProposalDomain {
    uint32_t offset;
    unsigned width;
    uint64_t lower;
    uint64_t cardinality;
    bool powerOfTwo;
    Value sampledIndex;
  };
  SmallVector<ProposalDomain> proposalDomains;
  struct ProposalCaptureDomain {
    uint32_t offset;
    unsigned width;
    std::optional<uint32_t> lowerCapture;
    std::optional<uint32_t> upperCapture;
    bool lowerExclusive;
    bool upperExclusive;
    bool isSigned;
    std::optional<uint64_t> staticLower;
    std::optional<uint64_t> staticUpper;
    Value lower;
    Value cardinality;
    Value sampledIndex;
  };
  SmallVector<ProposalCaptureDomain> proposalCaptureDomains;
  size_t materializedCaptureBounds = 0;
  struct ProposalAlias {
    uint32_t targetOffset;
    uint32_t sourceOffset;
    unsigned width;
  };
  SmallVector<ProposalAlias> proposalAliases;
  struct ProposalDefinition {
    uint32_t targetOffset;
    unsigned width;
    uint32_t expressionBegin;
    uint32_t expressionEnd;
  };
  SmallVector<ProposalDefinition> proposalDefinitions;
  uint32_t propertyOffset = 0;
  for (const Property &property : planned) {
    auto found = llvm::find_if(analysis.domains,
                               [&](const solver::RandomVariableDomain &domain) {
                                 return domain.offset == propertyOffset &&
                                        domain.width == property.width;
                               });
    if (found != analysis.domains.end() && found->lower <= found->upper) {
      uint64_t fullMaximum = property.width == 64
                                 ? UINT64_MAX
                                 : (uint64_t{1} << property.width) - 1;
      uint64_t distance = found->upper - found->lower;
      uint64_t cardinality = distance + 1;
      if (found->upper <= fullMaximum && cardinality != 0)
        proposalDomains.push_back({propertyOffset,
                                   property.width,
                                   found->lower,
                                   cardinality,
                                   (cardinality & (cardinality - 1)) == 0,
                                   {}});
    }
    propertyOffset += property.width;
  }
  llvm::erase_if(proposalDomains, [&](const ProposalDomain &domain) {
    return llvm::any_of(distPlans, [&](const DistPlan &plan) {
      return plan.propertyOffset == domain.offset &&
             planned[plan.propertyIndex].width == domain.width;
    });
  });
  size_t materializedDomains = proposalDomains.size();
  auto isPropertyField = [&](uint32_t offset, uint32_t width) {
    uint32_t currentOffset = 0;
    for (const Property &property : planned) {
      if (currentOffset == offset && property.width == width)
        return true;
      currentOffset += property.width;
    }
    return false;
  };
  for (const solver::RandomVariableCaptureBound &bound :
       analysis.captureBounds) {
    bool targetsDist = llvm::any_of(distPlans, [&](const DistPlan &plan) {
      return plan.propertyOffset == bound.offset &&
             planned[plan.propertyIndex].width == bound.width;
    });
    if (targetsDist)
      continue;
    bool conflicts = bound.width == 0 || bound.width > 64 ||
                     bound.captureIndex >= programCaptures.size() ||
                     !isPropertyField(bound.offset, bound.width);
    if (conflicts)
      continue;
    auto found = llvm::find_if(proposalCaptureDomains,
                               [&](const ProposalCaptureDomain &selected) {
                                 return selected.offset == bound.offset &&
                                        selected.width == bound.width;
                               });
    if (found != proposalCaptureDomains.end() &&
        found->isSigned != bound.isSigned)
      continue;
    if (found == proposalCaptureDomains.end()) {
      auto staticDomain =
          llvm::find_if(proposalDomains, [&](const ProposalDomain &domain) {
            return domain.offset == bound.offset && domain.width == bound.width;
          });
      if (bound.isSigned && staticDomain != proposalDomains.end())
        continue;
      std::optional<uint64_t> staticLower;
      std::optional<uint64_t> staticUpper;
      if (staticDomain != proposalDomains.end()) {
        staticLower = staticDomain->lower;
        staticUpper = staticDomain->lower + staticDomain->cardinality - 1;
        proposalDomains.erase(staticDomain);
      }
      proposalCaptureDomains.push_back({bound.offset,
                                        bound.width,
                                        std::nullopt,
                                        std::nullopt,
                                        false,
                                        false,
                                        bound.isSigned,
                                        staticLower,
                                        staticUpper,
                                        {},
                                        {},
                                        {}});
      found = std::prev(proposalCaptureDomains.end());
    }
    bool lower = bound.kind == solver::RandomCaptureBoundKind::LowerInclusive ||
                 bound.kind == solver::RandomCaptureBoundKind::LowerExclusive;
    std::optional<uint32_t> &capture =
        lower ? found->lowerCapture : found->upperCapture;
    if (capture)
      continue;
    capture = bound.captureIndex;
    if (lower)
      found->lowerExclusive =
          bound.kind == solver::RandomCaptureBoundKind::LowerExclusive;
    else
      found->upperExclusive =
          bound.kind == solver::RandomCaptureBoundKind::UpperExclusive;
    ++materializedCaptureBounds;
  }
  for (const solver::RandomVariableAlias &alias : analysis.aliases) {
    if (!distPlans.empty())
      break;
    bool sourceIsTarget = llvm::any_of(
        analysis.aliases, [&](const solver::RandomVariableAlias &other) {
          return other.targetOffset == alias.sourceOffset;
        });
    unsigned targetCount = llvm::count_if(
        analysis.aliases, [&](const solver::RandomVariableAlias &other) {
          return other.targetOffset == alias.targetOffset;
        });
    if (alias.targetOffset != alias.sourceOffset &&
        isPropertyField(alias.targetOffset, alias.width) &&
        isPropertyField(alias.sourceOffset, alias.width) && !sourceIsTarget &&
        targetCount == 1)
      proposalAliases.push_back(
          {alias.targetOffset, alias.sourceOffset, alias.width});
  }
  if (hasSolveBefore && !proposalAliases.empty()) {
    auto solveLayerForField = [&](uint32_t fieldOffset, unsigned fieldWidth) {
      uint32_t offset = 0;
      for (auto [propertyIndex, property] : llvm::enumerate(planned)) {
        if (offset == fieldOffset && property.width == fieldWidth) {
          for (auto [layerIndex, layer] : llvm::enumerate(solveBeforeLayers))
            if (llvm::is_contained(layer, propertyIndex))
              return static_cast<unsigned>(layerIndex);
          break;
        }
        offset += property.width;
      }
      return static_cast<unsigned>(solveBeforeLayers.size());
    };
    SmallVector<ProposalAlias> orientedAliases;
    SmallVector<bool> consumed(proposalAliases.size(), false);
    for (unsigned index = 0; index != proposalAliases.size(); ++index) {
      if (consumed[index])
        continue;
      const ProposalAlias &first = proposalAliases[index];
      SmallVector<uint32_t> members{first.sourceOffset};
      for (unsigned other = index; other != proposalAliases.size(); ++other) {
        const ProposalAlias &alias = proposalAliases[other];
        if (alias.sourceOffset != first.sourceOffset ||
            alias.width != first.width)
          continue;
        consumed[other] = true;
        members.push_back(alias.targetOffset);
      }
      bool hasDefinition = llvm::any_of(
          analysis.definitions,
          [&](const solver::RandomVariableDefinition &definition) {
            return definition.width == first.width &&
                   llvm::is_contained(members, definition.targetOffset);
          });
      uint32_t source = first.sourceOffset;
      if (!hasDefinition)
        source = *llvm::min_element(members, [&](uint32_t lhs, uint32_t rhs) {
          return std::pair(solveLayerForField(lhs, first.width), lhs) <
                 std::pair(solveLayerForField(rhs, first.width), rhs);
        });
      for (uint32_t member : members)
        if (member != source)
          orientedAliases.push_back({member, source, first.width});
    }
    proposalAliases = std::move(orientedAliases);
  }
  llvm::erase_if(proposalDomains, [&](const ProposalDomain &domain) {
    auto alias =
        llvm::find_if(proposalAliases, [&](const ProposalAlias &candidate) {
          return candidate.targetOffset == domain.offset &&
                 candidate.width == domain.width;
        });
    if (alias == proposalAliases.end())
      return false;
    auto sourceDomain =
        llvm::find_if(proposalDomains, [&](const ProposalDomain &candidate) {
          return candidate.offset == alias->sourceOffset &&
                 candidate.width == alias->width;
        });
    return sourceDomain != proposalDomains.end() &&
           sourceDomain->lower == domain.lower &&
           sourceDomain->cardinality == domain.cardinality;
  });
  auto canonicalProposalField = [&](uint32_t offset, unsigned width) {
    auto alias = llvm::find_if(proposalAliases, [&](const ProposalAlias &item) {
      return item.targetOffset == offset && item.width == width;
    });
    return alias == proposalAliases.end() ? offset : alias->sourceOffset;
  };

  auto isDefinitionUnary = [](uint8_t opcode) {
    return opcode >= OBELISK_RT_RANDOM_CAST_V1 &&
           opcode <= OBELISK_RT_RANDOM_LOGICAL_NOT_V1;
  };
  auto isDefinitionBinary = [](uint8_t opcode) {
    return (opcode >= OBELISK_RT_RANDOM_ADD_V1 &&
            opcode <= OBELISK_RT_RANDOM_LOGICAL_EQUIV_V1) ||
           (opcode >= OBELISK_RT_RANDOM_DIV_V1 &&
            opcode <= OBELISK_RT_RANDOM_POWER_V1);
  };
  for (const solver::RandomVariableDefinition &definition :
       analysis.definitions) {
    if (!distPlans.empty())
      break;
    if (!isPropertyField(definition.targetOffset, definition.width) ||
        definition.expressionBegin >= definition.expressionEnd ||
        definition.expressionEnd > programInstructions.size())
      continue;
    SmallVector<unsigned> widths;
    bool supported = true;
    for (const EncodedInstruction &encoded :
         llvm::ArrayRef(programInstructions)
             .slice(definition.expressionBegin,
                    definition.expressionEnd - definition.expressionBegin)) {
      if (encoded.opcode == OBELISK_RT_RANDOM_PUSH_VARIABLE_V1) {
        uint32_t sourceOffset =
            canonicalProposalField(encoded.operand, encoded.width);
        uint64_t variableEnd =
            static_cast<uint64_t>(sourceOffset) + encoded.width;
        uint64_t targetEnd =
            static_cast<uint64_t>(definition.targetOffset) + definition.width;
        bool overlapsTarget =
            sourceOffset < targetEnd && definition.targetOffset < variableEnd;
        if (overlapsTarget || sourceOffset >= totalWidth ||
            encoded.width > totalWidth - sourceOffset) {
          supported = false;
          break;
        }
        widths.push_back(encoded.width);
      } else if (encoded.opcode == OBELISK_RT_RANDOM_PUSH_CAPTURE_V1) {
        if (encoded.operand >= programCaptures.size()) {
          supported = false;
          break;
        }
        widths.push_back(encoded.width);
      } else if (encoded.opcode == OBELISK_RT_RANDOM_PUSH_LITERAL_V1) {
        widths.push_back(encoded.width);
      } else if (isDefinitionUnary(encoded.opcode)) {
        if (widths.empty()) {
          supported = false;
          break;
        }
        widths.back() = encoded.width;
      } else if (isDefinitionBinary(encoded.opcode)) {
        if (widths.size() < 2) {
          supported = false;
          break;
        }
        widths.pop_back();
        widths.back() = encoded.width;
      } else if (encoded.opcode == OBELISK_RT_RANDOM_SELECT_V1) {
        if (widths.size() < 3) {
          supported = false;
          break;
        }
        widths.pop_back();
        widths.pop_back();
        widths.back() = encoded.width;
      } else {
        supported = false;
        break;
      }
    }
    if (supported && widths.size() == 1 && widths.front() == definition.width)
      proposalDefinitions.push_back({definition.targetOffset, definition.width,
                                     definition.expressionBegin,
                                     definition.expressionEnd});
  }

  auto selectAssignmentTable = [&](ArrayRef<uint64_t> assignments,
                                   Value index) -> Value {
    Value assignment = constant64(assignments.front());
    for (auto [tableIndex, tableAssignment] :
         llvm::enumerate(llvm::drop_begin(assignments))) {
      Value selected =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                index, constant64(tableIndex + 1));
      assignment = arith::SelectOp::create(
          builder, location, selected, constant64(tableAssignment), assignment);
    }
    return assignment;
  };
  Value sampledSolveBeforeAssignment;
  Value sampledComponentAssignment;
  Value sampledDistAssignment;
  uint64_t sampledDistMask = 0;
  auto materializeProposal = [&](Value rawAssignment,
                                 Value attempt) -> FailureOr<Value> {
    if (sampledSolveBeforeAssignment)
      return sampledSolveBeforeAssignment;
    if (!proposalAssignments.empty()) {
      if (proposalAssignments.size() == 1)
        return constant64(proposalAssignments.front());
      Value index =
          powerOfTwoAssignmentTable
              ? arith::AndIOp::create(
                    builder, location, rawAssignment,
                    constant64(proposalAssignments.size() - 1))
                    .getResult()
              : arith::RemUIOp::create(builder, location, rawAssignment,
                                       constant64(proposalAssignments.size()))
                    .getResult();
      return selectAssignmentTable(proposalAssignments, index);
    }
    Value assignment = rawAssignment;
    if (sampledComponentAssignment)
      assignment = arith::OrIOp::create(
          builder, location,
          arith::AndIOp::create(builder, location, assignment,
                                constant64(~proposalAssignmentTableMask)),
          sampledComponentAssignment);
    for (const ProposalDomain &domain : proposalDomains) {
      Value fieldBits;
      if (domain.powerOfTwo) {
        fieldBits = rawAssignment;
        if (domain.offset != 0)
          fieldBits = arith::ShRUIOp::create(builder, location, fieldBits,
                                             constant64(domain.offset));
        fieldBits = arith::AndIOp::create(builder, location, fieldBits,
                                          constant64(domain.cardinality - 1));
      } else {
        // Advance the independently unbiased starting index without unsigned
        // overflow. This visits every interval value cyclically when the
        // generated checker requests retries.
        Value step = arith::RemUIOp::create(builder, location, attempt,
                                            constant64(domain.cardinality));
        Value threshold = arith::SubIOp::create(
            builder, location, constant64(domain.cardinality), step);
        Value wraps =
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::uge,
                                  domain.sampledIndex, threshold);
        Value linear =
            arith::AddIOp::create(builder, location, domain.sampledIndex, step);
        Value wrapped = arith::SubIOp::create(builder, location,
                                              domain.sampledIndex, threshold);
        fieldBits =
            arith::SelectOp::create(builder, location, wraps, wrapped, linear);
      }
      if (domain.lower != 0)
        fieldBits = arith::AddIOp::create(builder, location, fieldBits,
                                          constant64(domain.lower));
      if (domain.offset != 0)
        fieldBits = arith::ShLIOp::create(builder, location, fieldBits,
                                          constant64(domain.offset));
      uint64_t fieldMask = domain.width == 64
                               ? UINT64_MAX
                               : ((uint64_t{1} << domain.width) - 1)
                                     << domain.offset;
      assignment = arith::OrIOp::create(
          builder, location,
          arith::AndIOp::create(builder, location, assignment,
                                constant64(~fieldMask)),
          fieldBits);
    }
    for (const ProposalCaptureDomain &bound : proposalCaptureDomains) {
      // The unbiased starting index is advanced cyclically for residual
      // checker retries, exactly as for constant non-power-of-two domains.
      Value fullCardinality =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                bound.cardinality, constant64(0));
      Value safeCardinality = arith::SelectOp::create(
          builder, location, fullCardinality, constant64(1), bound.cardinality);
      Value reducedStep =
          arith::RemUIOp::create(builder, location, attempt, safeCardinality);
      Value step = arith::SelectOp::create(builder, location, fullCardinality,
                                           attempt, reducedStep);
      Value threshold =
          arith::SubIOp::create(builder, location, bound.cardinality, step);
      Value wraps =
          arith::CmpIOp::create(builder, location, arith::CmpIPredicate::uge,
                                bound.sampledIndex, threshold);
      Value linear =
          arith::AddIOp::create(builder, location, bound.sampledIndex, step);
      Value wrapped = arith::SubIOp::create(builder, location,
                                            bound.sampledIndex, threshold);
      Value fieldBits =
          arith::SelectOp::create(builder, location, wraps, wrapped, linear);
      fieldBits =
          arith::AddIOp::create(builder, location, fieldBits, bound.lower);
      if (bound.isSigned)
        fieldBits =
            arith::XOrIOp::create(builder, location, fieldBits,
                                  constant64(uint64_t{1} << (bound.width - 1)));
      if (bound.offset != 0)
        fieldBits = arith::ShLIOp::create(builder, location, fieldBits,
                                          constant64(bound.offset));
      uint64_t fieldMask = bound.width == 64
                               ? UINT64_MAX
                               : ((uint64_t{1} << bound.width) - 1)
                                     << bound.offset;
      assignment = arith::OrIOp::create(
          builder, location,
          arith::AndIOp::create(builder, location, assignment,
                                constant64(~fieldMask)),
          fieldBits);
    }
    struct DefinitionValue {
      Value bits;
      unsigned width;
    };
    auto maskDefinitionValue = [&](Value bits, unsigned width) {
      if (width == 64)
        return bits;
      return arith::AndIOp::create(builder, location, bits,
                                   constant64((uint64_t{1} << width) - 1))
          .getResult();
    };
    auto resizeDefinitionValue = [&](DefinitionValue input, unsigned width,
                                     bool signExtend) {
      Value bits = input.bits;
      if (input.width < width && signExtend) {
        unsigned shift = 64 - input.width;
        bits =
            arith::ShLIOp::create(builder, location, bits, constant64(shift));
        bits =
            arith::ShRSIOp::create(builder, location, bits, constant64(shift));
      }
      return maskDefinitionValue(bits, width);
    };
    auto definitionTruth = [&](Value bits) {
      return arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                   bits, constant64(0))
          .getResult();
    };
    auto definitionBooleanBits = [&](Value condition) {
      return arith::ExtUIOp::create(builder, location, builder.getI64Type(),
                                    condition)
          .getResult();
    };
    for (const ProposalDefinition &definition : proposalDefinitions) {
      SmallVector<DefinitionValue> stack;
      for (const EncodedInstruction &encoded :
           llvm::ArrayRef(programInstructions)
               .slice(definition.expressionBegin,
                      definition.expressionEnd - definition.expressionBegin)) {
        bool signedOperation =
            (encoded.flags & OBELISK_RT_RANDOM_INSTRUCTION_SIGNED) != 0;
        if (encoded.opcode == OBELISK_RT_RANDOM_PUSH_VARIABLE_V1) {
          uint32_t sourceOffset =
              canonicalProposalField(encoded.operand, encoded.width);
          Value bits = assignment;
          if (sourceOffset != 0)
            bits = arith::ShRUIOp::create(builder, location, bits,
                                          constant64(sourceOffset));
          stack.push_back(
              {maskDefinitionValue(bits, encoded.width), encoded.width});
          continue;
        }
        if (encoded.opcode == OBELISK_RT_RANDOM_PUSH_CAPTURE_V1) {
          stack.push_back({maskDefinitionValue(programCaptures[encoded.operand],
                                               encoded.width),
                           encoded.width});
          continue;
        }
        if (encoded.opcode == OBELISK_RT_RANDOM_PUSH_LITERAL_V1) {
          stack.push_back({maskDefinitionValue(constant64(encoded.immediate),
                                               encoded.width),
                           encoded.width});
          continue;
        }
        if (encoded.opcode == OBELISK_RT_RANDOM_SELECT_V1) {
          if (stack.size() < 3)
            return failure();
          DefinitionValue falseValue = stack.pop_back_val();
          DefinitionValue trueValue = stack.pop_back_val();
          DefinitionValue condition = stack.pop_back_val();
          Value bits = arith::SelectOp::create(
              builder, location, definitionTruth(condition.bits),
              resizeDefinitionValue(trueValue, encoded.width, signedOperation),
              resizeDefinitionValue(falseValue, encoded.width,
                                    signedOperation));
          stack.push_back(
              {maskDefinitionValue(bits, encoded.width), encoded.width});
          continue;
        }
        if (isDefinitionUnary(encoded.opcode)) {
          if (stack.empty())
            return failure();
          DefinitionValue input = stack.pop_back_val();
          Value bits = input.bits;
          if (encoded.opcode <= OBELISK_RT_RANDOM_BIT_NOT_V1)
            bits = resizeDefinitionValue(input, encoded.width, signedOperation);
          if (encoded.opcode == OBELISK_RT_RANDOM_NEG_V1)
            bits =
                arith::SubIOp::create(builder, location, constant64(0), bits);
          else if (encoded.opcode == OBELISK_RT_RANDOM_BIT_NOT_V1)
            bits = arith::XOrIOp::create(
                builder, location, bits,
                constant64(encoded.width == 64
                               ? UINT64_MAX
                               : (uint64_t{1} << encoded.width) - 1));
          else if (encoded.opcode == OBELISK_RT_RANDOM_REDUCE_AND_V1 ||
                   encoded.opcode == OBELISK_RT_RANDOM_REDUCE_NAND_V1) {
            Value allOnes = constant64(input.width == 64
                                           ? UINT64_MAX
                                           : (uint64_t{1} << input.width) - 1);
            arith::CmpIPredicate predicate =
                encoded.opcode == OBELISK_RT_RANDOM_REDUCE_AND_V1
                    ? arith::CmpIPredicate::eq
                    : arith::CmpIPredicate::ne;
            bits = definitionBooleanBits(arith::CmpIOp::create(
                builder, location, predicate, input.bits, allOnes));
          } else if (encoded.opcode == OBELISK_RT_RANDOM_REDUCE_OR_V1) {
            bits = definitionBooleanBits(definitionTruth(input.bits));
          } else if (encoded.opcode == OBELISK_RT_RANDOM_REDUCE_XOR_V1 ||
                     encoded.opcode == OBELISK_RT_RANDOM_REDUCE_XNOR_V1) {
            bits = constant64(0);
            for (unsigned bit = 0; bit != input.width; ++bit) {
              Value current = input.bits;
              if (bit != 0)
                current = arith::ShRUIOp::create(builder, location, current,
                                                 constant64(bit));
              current = arith::AndIOp::create(builder, location, current,
                                              constant64(1));
              bits = arith::XOrIOp::create(builder, location, bits, current);
            }
            if (encoded.opcode == OBELISK_RT_RANDOM_REDUCE_XNOR_V1)
              bits =
                  arith::XOrIOp::create(builder, location, bits, constant64(1));
          } else if (encoded.opcode == OBELISK_RT_RANDOM_REDUCE_NOR_V1 ||
                     encoded.opcode == OBELISK_RT_RANDOM_LOGICAL_NOT_V1) {
            bits = definitionBooleanBits(arith::CmpIOp::create(
                builder, location, arith::CmpIPredicate::eq, input.bits,
                constant64(0)));
          }
          stack.push_back(
              {maskDefinitionValue(bits, encoded.width), encoded.width});
          continue;
        }
        if (!isDefinitionBinary(encoded.opcode) || stack.size() < 2)
          return failure();
        DefinitionValue rhs = stack.pop_back_val();
        DefinitionValue lhs = stack.pop_back_val();
        if (encoded.opcode == OBELISK_RT_RANDOM_SHIFT_LEFT_V1 ||
            encoded.opcode == OBELISK_RT_RANDOM_SHIFT_RIGHT_V1 ||
            encoded.opcode == OBELISK_RT_RANDOM_SHIFT_RIGHT_ARITH_V1) {
          bool arithmeticRight =
              encoded.opcode == OBELISK_RT_RANDOM_SHIFT_RIGHT_ARITH_V1;
          Value left = resizeDefinitionValue(
              lhs, arithmeticRight ? 64 : encoded.width, arithmeticRight);
          Value oversized = arith::CmpIOp::create(
              builder, location, arith::CmpIPredicate::uge, rhs.bits,
              constant64(encoded.width));
          Value safeAmount = arith::SelectOp::create(
              builder, location, oversized, constant64(0), rhs.bits);
          Value shifted;
          if (encoded.opcode == OBELISK_RT_RANDOM_SHIFT_LEFT_V1)
            shifted =
                arith::ShLIOp::create(builder, location, left, safeAmount);
          else if (arithmeticRight)
            shifted =
                arith::ShRSIOp::create(builder, location, left, safeAmount);
          else
            shifted =
                arith::ShRUIOp::create(builder, location, left, safeAmount);
          Value oversizedResult = constant64(0);
          if (arithmeticRight) {
            Value negative = arith::CmpIOp::create(builder, location,
                                                   arith::CmpIPredicate::slt,
                                                   left, constant64(0));
            oversizedResult = arith::SelectOp::create(
                builder, location, negative,
                constant64(encoded.width == 64
                               ? UINT64_MAX
                               : (uint64_t{1} << encoded.width) - 1),
                constant64(0));
          }
          Value bits = arith::SelectOp::create(builder, location, oversized,
                                               oversizedResult, shifted);
          stack.push_back(
              {maskDefinitionValue(bits, encoded.width), encoded.width});
          continue;
        }
        if (encoded.opcode == OBELISK_RT_RANDOM_POWER_V1) {
          Value base =
              resizeDefinitionValue(lhs, encoded.width, signedOperation);
          Value bits = constant64(1);
          for (unsigned bit = 0; bit != rhs.width; ++bit) {
            Value exponentBit = rhs.bits;
            if (bit != 0)
              exponentBit = arith::ShRUIOp::create(
                  builder, location, exponentBit, constant64(bit));
            exponentBit = arith::AndIOp::create(builder, location, exponentBit,
                                                constant64(1));
            Value multiplied =
                arith::MulIOp::create(builder, location, bits, base);
            multiplied = maskDefinitionValue(multiplied, encoded.width);
            bits = arith::SelectOp::create(builder, location,
                                           definitionTruth(exponentBit),
                                           multiplied, bits);
            if (bit + 1 != rhs.width)
              base = maskDefinitionValue(
                  arith::MulIOp::create(builder, location, base, base),
                  encoded.width);
          }
          stack.push_back(
              {maskDefinitionValue(bits, encoded.width), encoded.width});
          continue;
        }
        bool logical = encoded.opcode >= OBELISK_RT_RANDOM_LOGICAL_AND_V1 &&
                       encoded.opcode <= OBELISK_RT_RANDOM_LOGICAL_EQUIV_V1;
        unsigned operandWidth =
            encoded.opcode >= OBELISK_RT_RANDOM_EQ_V1 &&
                    encoded.opcode <= OBELISK_RT_RANDOM_LT_V1
                ? std::max(lhs.width, rhs.width)
                : encoded.width;
        Value left =
            logical ? lhs.bits
                    : resizeDefinitionValue(lhs, operandWidth, signedOperation);
        Value right =
            logical ? rhs.bits
                    : resizeDefinitionValue(rhs, operandWidth, signedOperation);
        Value bits;
        switch (encoded.opcode) {
        case OBELISK_RT_RANDOM_ADD_V1:
          bits = arith::AddIOp::create(builder, location, left, right);
          break;
        case OBELISK_RT_RANDOM_SUB_V1:
          bits = arith::SubIOp::create(builder, location, left, right);
          break;
        case OBELISK_RT_RANDOM_MUL_V1:
          bits = arith::MulIOp::create(builder, location, left, right);
          break;
        case OBELISK_RT_RANDOM_DIV_V1:
        case OBELISK_RT_RANDOM_MOD_V1:
          if (signedOperation) {
            Value signedLeft =
                resizeDefinitionValue({left, operandWidth}, 64, true);
            Value signedRight =
                resizeDefinitionValue({right, operandWidth}, 64, true);
            Value overflow = arith::AndIOp::create(
                builder, location,
                arith::CmpIOp::create(builder, location,
                                      arith::CmpIPredicate::eq, signedLeft,
                                      constant64(uint64_t{1} << 63)),
                arith::CmpIOp::create(builder, location,
                                      arith::CmpIPredicate::eq, signedRight,
                                      constant64(UINT64_MAX)));
            Value safeRight = arith::SelectOp::create(
                builder, location, overflow, constant64(1), signedRight);
            if (encoded.opcode == OBELISK_RT_RANDOM_DIV_V1) {
              Value quotient = arith::DivSIOp::create(builder, location,
                                                      signedLeft, safeRight);
              bits = arith::SelectOp::create(builder, location, overflow,
                                             signedLeft, quotient);
            } else {
              Value remainder = arith::RemSIOp::create(builder, location,
                                                       signedLeft, safeRight);
              bits = arith::SelectOp::create(builder, location, overflow,
                                             constant64(0), remainder);
            }
          } else if (encoded.opcode == OBELISK_RT_RANDOM_DIV_V1) {
            bits = arith::DivUIOp::create(builder, location, left, right);
          } else {
            bits = arith::RemUIOp::create(builder, location, left, right);
          }
          break;
        case OBELISK_RT_RANDOM_BIT_AND_V1:
          bits = arith::AndIOp::create(builder, location, left, right);
          break;
        case OBELISK_RT_RANDOM_BIT_OR_V1:
          bits = arith::OrIOp::create(builder, location, left, right);
          break;
        case OBELISK_RT_RANDOM_BIT_XOR_V1:
          bits = arith::XOrIOp::create(builder, location, left, right);
          break;
        case OBELISK_RT_RANDOM_BIT_XNOR_V1:
          bits = arith::XOrIOp::create(builder, location, left, right);
          bits = arith::XOrIOp::create(
              builder, location, bits,
              constant64(encoded.width == 64
                             ? UINT64_MAX
                             : (uint64_t{1} << encoded.width) - 1));
          break;
        case OBELISK_RT_RANDOM_EQ_V1:
        case OBELISK_RT_RANDOM_NE_V1:
        case OBELISK_RT_RANDOM_GE_V1:
        case OBELISK_RT_RANDOM_GT_V1:
        case OBELISK_RT_RANDOM_LE_V1:
        case OBELISK_RT_RANDOM_LT_V1: {
          Value comparedLeft = left;
          Value comparedRight = right;
          if (signedOperation && encoded.opcode >= OBELISK_RT_RANDOM_GE_V1) {
            comparedLeft =
                resizeDefinitionValue({left, operandWidth}, 64, true);
            comparedRight =
                resizeDefinitionValue({right, operandWidth}, 64, true);
          }
          arith::CmpIPredicate predicate;
          switch (encoded.opcode) {
          case OBELISK_RT_RANDOM_EQ_V1:
            predicate = arith::CmpIPredicate::eq;
            break;
          case OBELISK_RT_RANDOM_NE_V1:
            predicate = arith::CmpIPredicate::ne;
            break;
          case OBELISK_RT_RANDOM_GE_V1:
            predicate = signedOperation ? arith::CmpIPredicate::sge
                                        : arith::CmpIPredicate::uge;
            break;
          case OBELISK_RT_RANDOM_GT_V1:
            predicate = signedOperation ? arith::CmpIPredicate::sgt
                                        : arith::CmpIPredicate::ugt;
            break;
          case OBELISK_RT_RANDOM_LE_V1:
            predicate = signedOperation ? arith::CmpIPredicate::sle
                                        : arith::CmpIPredicate::ule;
            break;
          case OBELISK_RT_RANDOM_LT_V1:
            predicate = signedOperation ? arith::CmpIPredicate::slt
                                        : arith::CmpIPredicate::ult;
            break;
          default:
            return failure();
          }
          bits = definitionBooleanBits(arith::CmpIOp::create(
              builder, location, predicate, comparedLeft, comparedRight));
          break;
        }
        case OBELISK_RT_RANDOM_LOGICAL_AND_V1:
        case OBELISK_RT_RANDOM_LOGICAL_OR_V1:
        case OBELISK_RT_RANDOM_LOGICAL_IMPLIES_V1:
        case OBELISK_RT_RANDOM_LOGICAL_EQUIV_V1: {
          Value leftTruth = definitionTruth(lhs.bits);
          Value rightTruth = definitionTruth(rhs.bits);
          Value predicate;
          if (encoded.opcode == OBELISK_RT_RANDOM_LOGICAL_AND_V1)
            predicate =
                arith::AndIOp::create(builder, location, leftTruth, rightTruth);
          else if (encoded.opcode == OBELISK_RT_RANDOM_LOGICAL_OR_V1)
            predicate =
                arith::OrIOp::create(builder, location, leftTruth, rightTruth);
          else if (encoded.opcode == OBELISK_RT_RANDOM_LOGICAL_IMPLIES_V1) {
            Value leftFalse = arith::CmpIOp::create(builder, location,
                                                    arith::CmpIPredicate::eq,
                                                    lhs.bits, constant64(0));
            predicate =
                arith::OrIOp::create(builder, location, leftFalse, rightTruth);
          } else {
            predicate = arith::CmpIOp::create(builder, location,
                                              arith::CmpIPredicate::eq,
                                              leftTruth, rightTruth);
          }
          bits = definitionBooleanBits(predicate);
          break;
        }
        default:
          return failure();
        }
        stack.push_back(
            {maskDefinitionValue(bits, encoded.width), encoded.width});
      }
      if (stack.size() != 1 || stack.back().width != definition.width)
        return failure();
      uint64_t valueMask = definition.width == 64
                               ? UINT64_MAX
                               : (uint64_t{1} << definition.width) - 1;
      Value fieldBits = stack.back().bits;
      if (definition.targetOffset != 0)
        fieldBits = arith::ShLIOp::create(builder, location, fieldBits,
                                          constant64(definition.targetOffset));
      uint64_t targetMask = valueMask << definition.targetOffset;
      assignment = arith::OrIOp::create(
          builder, location,
          arith::AndIOp::create(builder, location, assignment,
                                constant64(~targetMask)),
          fieldBits);
    }
    SmallVector<std::tuple<uint32_t, unsigned, Value>> aliasSources;
    for (const ProposalAlias &alias : proposalAliases) {
      uint64_t valueMask =
          alias.width == 64 ? UINT64_MAX : (uint64_t{1} << alias.width) - 1;
      auto cachedSource = llvm::find_if(aliasSources, [&](const auto &source) {
        return std::get<0>(source) == alias.sourceOffset &&
               std::get<1>(source) == alias.width;
      });
      Value sourceBits;
      if (cachedSource != aliasSources.end()) {
        sourceBits = std::get<2>(*cachedSource);
      } else {
        sourceBits = assignment;
        if (alias.sourceOffset != 0)
          sourceBits = arith::ShRUIOp::create(builder, location, sourceBits,
                                              constant64(alias.sourceOffset));
        sourceBits = arith::AndIOp::create(builder, location, sourceBits,
                                           constant64(valueMask));
        aliasSources.emplace_back(alias.sourceOffset, alias.width, sourceBits);
      }
      if (alias.targetOffset != 0)
        sourceBits = arith::ShLIOp::create(builder, location, sourceBits,
                                           constant64(alias.targetOffset));
      uint64_t targetMask = valueMask << alias.targetOffset;
      assignment = arith::OrIOp::create(
          builder, location,
          arith::AndIOp::create(builder, location, assignment,
                                constant64(~targetMask)),
          sourceBits);
    }
    if (sampledDistAssignment)
      assignment = arith::OrIOp::create(
          builder, location,
          arith::AndIOp::create(builder, location, assignment,
                                constant64(~sampledDistMask)),
          sampledDistAssignment);
    return assignment;
  };
  bool overwritesProposalDomain =
      llvm::any_of(
          proposalDomains,
          [&](const ProposalDomain &domain) {
            bool definitionTarget = llvm::any_of(
                proposalDefinitions, [&](const ProposalDefinition &definition) {
                  return definition.targetOffset == domain.offset &&
                         definition.width == domain.width;
                });
            auto alias = llvm::find_if(
                proposalAliases, [&](const ProposalAlias &candidate) {
                  return candidate.targetOffset == domain.offset &&
                         candidate.width == domain.width;
                });
            if (alias == proposalAliases.end())
              return definitionTarget;
            auto sourceDomain = llvm::find_if(
                proposalDomains, [&](const ProposalDomain &candidate) {
                  return candidate.offset == alias->sourceOffset &&
                         candidate.width == alias->width;
                });
            bool sameAliasDomain =
                sourceDomain != proposalDomains.end() &&
                sourceDomain->lower == domain.lower &&
                sourceDomain->cardinality == domain.cardinality;
            return definitionTarget || !sameAliasDomain;
          }) ||
      llvm::any_of(
          proposalCaptureDomains, [&](const ProposalCaptureDomain &bound) {
            bool definitionTarget = llvm::any_of(
                proposalDefinitions, [&](const ProposalDefinition &definition) {
                  return definition.targetOffset == bound.offset &&
                         definition.width == bound.width;
                });
            bool aliasTarget =
                llvm::any_of(proposalAliases, [&](const ProposalAlias &alias) {
                  return alias.targetOffset == bound.offset &&
                         alias.width == bound.width;
                });
            return definitionTarget || aliasTarget;
          });
  bool tableProposal = !analysis.assignmentTable.empty();
  bool componentTableProposal = !analysis.assignmentTables.empty();
  bool materializesCompleteProposal =
      tableProposal
          ? proposalAssignments.size() == analysis.assignmentTable.size()
      : componentTableProposal
          ? proposalAssignmentTables.size() ==
                    analysis.assignmentTables.size() &&
                materializedDomains == analysis.domains.size() &&
                materializedCaptureBounds == analysis.captureBounds.size() &&
                proposalAliases.size() == analysis.aliases.size() &&
                proposalDefinitions.size() == analysis.definitions.size()
          : materializedDomains == analysis.domains.size() &&
                materializedCaptureBounds == analysis.captureBounds.size() &&
                proposalAliases.size() == analysis.aliases.size() &&
                proposalDefinitions.size() == analysis.definitions.size();
  bool exactProposal = analysis.proposalExact && !hasSoftConstraint &&
                       !hasRandC && distPlans.empty() &&
                       !overwritesProposalDomain &&
                       materializesCompleteProposal;

  auto structuralProposalFollowsSolveOrder = [&]() {
    auto propertyIndexForField = [&](uint32_t fieldOffset,
                                     unsigned fieldWidth)
        -> std::optional<unsigned> {
      uint32_t offset = 0;
      for (auto [index, property] : llvm::enumerate(planned)) {
        if (offset == fieldOffset && property.width == fieldWidth)
          return index;
        offset += property.width;
      }
      return std::nullopt;
    };

    SmallVector<uint64_t> dependencies(planned.size(), 0);
    for (const ProposalAlias &alias : proposalAliases) {
      std::optional<unsigned> target =
          propertyIndexForField(alias.targetOffset, alias.width);
      std::optional<unsigned> source =
          propertyIndexForField(alias.sourceOffset, alias.width);
      if (!target || !source)
        return false;
      dependencies[*target] |= uint64_t{1} << *source;
    }
    for (const ProposalDefinition &definition : proposalDefinitions) {
      std::optional<unsigned> target =
          propertyIndexForField(definition.targetOffset, definition.width);
      if (!target)
        return false;
      for (const EncodedInstruction &encoded :
           llvm::ArrayRef(programInstructions)
               .slice(definition.expressionBegin,
                      definition.expressionEnd - definition.expressionBegin)) {
        if (encoded.opcode != OBELISK_RT_RANDOM_PUSH_VARIABLE_V1)
          continue;
        uint32_t sourceOffset =
            canonicalProposalField(encoded.operand, encoded.width);
        std::optional<unsigned> source =
            propertyIndexForField(sourceOffset, encoded.width);
        if (!source)
          return false;
        dependencies[*target] |= uint64_t{1} << *source;
      }
    }

    // Compute transitive property dependencies so an unordered intermediate
    // cannot hide a later-to-earlier solve dependency.
    for (unsigned intermediate = 0; intermediate != planned.size();
         ++intermediate) {
      uint64_t intermediateBit = uint64_t{1} << intermediate;
      for (uint64_t &propertyDependencies : dependencies)
        if ((propertyDependencies & intermediateBit) != 0)
          propertyDependencies |= dependencies[intermediate];
    }

    SmallVector<std::optional<unsigned>> propertyLayers(planned.size());
    for (auto [layerIndex, layer] : llvm::enumerate(solveBeforeLayers))
      for (unsigned property : layer)
        propertyLayers[property] = layerIndex;
    for (unsigned target = 0; target != planned.size(); ++target) {
      if (!propertyLayers[target])
        continue;
      for (unsigned source = 0; source != planned.size(); ++source) {
        if ((dependencies[target] & (uint64_t{1} << source)) == 0 ||
            !propertyLayers[source])
          continue;
        if (*propertyLayers[source] > *propertyLayers[target])
          return false;
      }
    }
    return true;
  };

  bool solveBeforeRequiresRuntime = hasSolveBefore && hasSoftConstraint;
  if (hasSolveBefore && !solveBeforeTableRoot &&
      analysis.satisfiability != solver::Satisfiability::Unsatisfiable) {
    uint64_t orderedPropertyMask = 0;
    for (uint64_t layerMask : solveBeforeLayerMasks)
      orderedPropertyMask |= layerMask;

    uint64_t uncoveredOrderedMask =
        orderedPropertyMask & ~proposalAssignmentTableMask;
    if (uncoveredOrderedMask != 0) {
      bool unresolvedComponentCrossesLayers = llvm::any_of(
          analysis.constraintComponentMasks, [&](uint64_t componentMask) {
            if ((componentMask & ~proposalAssignmentTableMask) == 0)
              return false;
            return llvm::count_if(solveBeforeLayerMasks,
                                  [&](uint64_t layerMask) {
                                    return (componentMask & layerMask) != 0;
                                  }) > 1;
          });
      bool validStructuralOrder = !unresolvedComponentCrossesLayers ||
                                  structuralProposalFollowsSolveOrder();
      if (!exactProposal || !analysis.hasConstraintComponentPartition ||
          !validStructuralOrder)
        solveBeforeRequiresRuntime = true;
    }
    if (!solveBeforeRequiresRuntime && validAssignmentTables)
      for (const ProposalAssignmentTable &table : proposalAssignmentTables)
        solveBeforeComponentTableRoots.push_back(
            buildSolveBeforeTable(table.assignments, 0));
  }

  if (solveBeforeRequiresRuntime && totalWidth > 20) {
    emitError(location)
        << "solve before residual fallback requires exhaustive traversal of "
           "at most 20 aggregate random bits; compile-time planning could not "
           "preserve the ordered distribution for "
        << totalWidth << " bits";
    return failure();
  }

  // An ordered residual solve must start from the single aggregate draw and
  // evaluate the original formula. Do not speculatively consume draws for a
  // compile-time proposal that this path cannot use, or reject a dynamic
  // capture bound before the residual solver sees the complete relation.
  if (solveBeforeRequiresRuntime) {
    solveBeforeTableRoot.reset();
    solveBeforeComponentTableRoots.clear();
    validAssignmentTable = false;
    validAssignmentTables = false;
    proposalDomains.clear();
    proposalCaptureDomains.clear();
  }

  // The compiler plan is proven for the all-enabled constraint set. If a
  // block is disabled at runtime, bypass plan-specific bounds, tables, and
  // extra draws; start a plain masked-domain search from the one object-stream
  // draw already consumed above. This avoids accidentally retaining the
  // distribution or failure conditions of a disabled constraint.
  Block *disabledCheckBlock = addBlock();
  Block *enabledSamplingBlock = addBlock();
  cf::CondBranchOp::create(builder, location, allPropertiesDisabled,
                           disabledCheckBlock, ValueRange{},
                           enabledSamplingBlock, ValueRange{});
  setCurrent(enabledSamplingBlock);
  Value randomDraw = next64(state);
  Value start = arith::OrIOp::create(
      builder, location,
      arith::AndIOp::create(builder, location, randomDraw, mutableMask),
      fixedAssignment);
  Value modeStart = start;
  Value modeState = state;
  Block *modeSamplingDispatchBlock = addBlock();
  Block *plannedSamplingBlock = addBlock();
  Value usePlannedSampling = allConstraintsEnabled;
  if (hasSolveBefore || !distPlans.empty())
    usePlannedSampling = arith::AndIOp::create(
        builder, location, usePlannedSampling, randomizationEnabled);
  cf::CondBranchOp::create(builder, location, usePlannedSampling,
                           plannedSamplingBlock, ValueRange{},
                           modeSamplingDispatchBlock, ValueRange{});
  setCurrent(plannedSamplingBlock);

  auto sampleBoundedIndex = [&](uint64_t cardinality, Value draw) -> Value {
    if ((cardinality & (cardinality - 1)) == 0)
      return arith::AndIOp::create(builder, location, draw,
                                   constant64(cardinality - 1));
    uint64_t remainder = ((UINT64_MAX % cardinality) + 1) % cardinality;
    uint64_t limit = UINT64_MAX - (remainder - 1);
    Block *boundedLoop = addBlock();
    Block *boundedDone = addBlock();
    Value boundedState = boundedLoop->addArgument(i64, location);
    Value boundedDraw = boundedLoop->addArgument(i64, location);
    Value finalState = boundedDone->addArgument(i64, location);
    Value boundedIndex = boundedDone->addArgument(i64, location);

    cf::BranchOp::create(builder, location, boundedLoop,
                         ValueRange{state, draw});
    setCurrent(boundedLoop);
    Value index = arith::RemUIOp::create(builder, location, boundedDraw,
                                         constant64(cardinality));
    Value accepted =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ult,
                              boundedDraw, constant64(limit));
    Value retryState = boundedState;
    Value retryDraw = next64(retryState);
    cf::CondBranchOp::create(builder, location, accepted, boundedDone,
                             ValueRange{boundedState, index}, boundedLoop,
                             ValueRange{retryState, retryDraw});

    setCurrent(boundedDone);
    state = finalState;
    return boundedIndex;
  };

  auto sampleDynamicBoundedIndex = [&](Value cardinality, Value draw) -> Value {
    // A zero cardinality represents the full 2^64-element domain. Use one as
    // a safe modulo divisor in that case, then select the original draw as the
    // index. Otherwise, rejecting values at or above the unsigned limit
    // removes the short tail of modulo buckets.
    Value fullCardinality =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                              cardinality, constant64(0));
    Value safeCardinality = arith::SelectOp::create(
        builder, location, fullCardinality, constant64(1), cardinality);
    Value negativeCardinality = arith::SubIOp::create(
        builder, location, constant64(0), safeCardinality);
    Value rejectionSize = arith::RemUIOp::create(
        builder, location, negativeCardinality, safeCardinality);
    Value limit =
        arith::SubIOp::create(builder, location, constant64(0), rejectionSize);
    Value acceptsAll =
        arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                              rejectionSize, constant64(0));
    Block *boundedLoop = addBlock();
    Block *boundedDone = addBlock();
    Value boundedState = boundedLoop->addArgument(i64, location);
    Value boundedDraw = boundedLoop->addArgument(i64, location);
    Value finalState = boundedDone->addArgument(i64, location);
    Value boundedIndex = boundedDone->addArgument(i64, location);

    cf::BranchOp::create(builder, location, boundedLoop,
                         ValueRange{state, draw});
    setCurrent(boundedLoop);
    Value reducedIndex =
        arith::RemUIOp::create(builder, location, boundedDraw, safeCardinality);
    Value index = arith::SelectOp::create(builder, location, fullCardinality,
                                          boundedDraw, reducedIndex);
    Value belowLimit = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ult, boundedDraw, limit);
    Value accepted =
        arith::OrIOp::create(builder, location, acceptsAll, belowLimit);
    Value retryState = boundedState;
    Value retryDraw = next64(retryState);
    cf::CondBranchOp::create(builder, location, accepted, boundedDone,
                             ValueRange{boundedState, index}, boundedLoop,
                             ValueRange{retryState, retryDraw});

    setCurrent(boundedDone);
    state = finalState;
    return boundedIndex;
  };

  struct MaterializedDistPlan {
    const DistPlan *plan;
    SmallVector<Value> weights;
    Value totalWeight;
  };
  SmallVector<MaterializedDistPlan> materializedDistPlans;
  Value distWeightsValid;
  auto requireValidDistWeights = [&](Value valid) {
    distWeightsValid = distWeightsValid
                           ? arith::AndIOp::create(builder, location,
                                                   distWeightsValid, valid)
                                 .getResult()
                           : valid;
  };
  for (const DistPlan &plan : distPlans) {
    MaterializedDistPlan materialized{&plan, {}, constant64(0)};
    for (const DistRangePlan &range : plan.ranges) {
      if (range.weightSigned)
        requireValidDistWeights(arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::sge, range.weight,
            constant64(0)));
      Value zero = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, range.weight,
          constant64(0));
      Value safeWeight = arith::SelectOp::create(
          builder, location, zero, constant64(1), range.weight);
      Value scaled = arith::MulIOp::create(
          builder, location, range.weight,
          constant64(range.selectionCoefficient));
      Value recovered = arith::DivUIOp::create(builder, location, scaled,
                                                safeWeight);
      Value productValid = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, recovered,
          constant64(range.selectionCoefficient));
      productValid = arith::OrIOp::create(builder, location, zero, productValid);
      requireValidDistWeights(productValid);
      Value updated = arith::AddIOp::create(builder, location,
                                             materialized.totalWeight, scaled);
      Value sumValid = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::uge, updated,
          materialized.totalWeight);
      requireValidDistWeights(sumValid);
      materialized.totalWeight = updated;
      materialized.weights.push_back(scaled);
    }
    requireValidDistWeights(arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::ne,
        materialized.totalWeight, constant64(0)));
    materializedDistPlans.push_back(std::move(materialized));
  }

  Block *invalidDistWeightsBlock = nullptr;
  Value invalidDistWeightsState;
  if (distWeightsValid) {
    invalidDistWeightsState = state;
    Block *validDistWeightsBlock = addBlock();
    invalidDistWeightsBlock = addBlock();
    cf::CondBranchOp::create(builder, location, distWeightsValid,
                             validDistWeightsBlock, ValueRange{},
                             invalidDistWeightsBlock, ValueRange{});
    setCurrent(validDistWeightsBlock);
  }

  if (!materializedDistPlans.empty()) {
    sampledDistAssignment = constant64(0);
    for (const MaterializedDistPlan &materialized : materializedDistPlans) {
      Value choice = sampleDynamicBoundedIndex(materialized.totalWeight,
                                                next64(state));
      const DistRangePlan *first = &materialized.plan->ranges.front();
      Value selectedLower = constant64(first->lower);
      Value selectedCardinality = constant64(first->cardinality);
      Value cumulative = materialized.weights.front();
      for (size_t index = 1; index != materialized.plan->ranges.size();
           ++index) {
        const DistRangePlan &range = materialized.plan->ranges[index];
        Value selected = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::uge, choice, cumulative);
        selectedLower = arith::SelectOp::create(
            builder, location, selected, constant64(range.lower),
            selectedLower);
        selectedCardinality = arith::SelectOp::create(
            builder, location, selected, constant64(range.cardinality),
            selectedCardinality);
        cumulative = arith::AddIOp::create(
            builder, location, cumulative, materialized.weights[index]);
      }
      Value field = arith::AddIOp::create(
          builder, location, selectedLower,
          sampleDynamicBoundedIndex(selectedCardinality, next64(state)));
      const Property &property =
          planned[materialized.plan->propertyIndex];
      if (property.isSigned)
        field = arith::XOrIOp::create(
            builder, location, field,
            constant64(uint64_t{1} << (property.width - 1)));
      uint64_t fieldMask = property.width == 64
                               ? UINT64_MAX
                               : (uint64_t{1} << property.width) - 1;
      field = arith::AndIOp::create(builder, location, field,
                                    constant64(fieldMask));
      if (materialized.plan->propertyOffset != 0) {
        field = arith::ShLIOp::create(
            builder, location, field,
            constant64(materialized.plan->propertyOffset));
        fieldMask <<= materialized.plan->propertyOffset;
      }
      sampledDistAssignment = arith::OrIOp::create(
          builder, location, sampledDistAssignment, field);
      sampledDistMask |= fieldMask;
    }
  }

  Value captureBoundsValid;
  auto requireValidCaptureBounds = [&](Value valid) {
    captureBoundsValid = captureBoundsValid
                             ? arith::AndIOp::create(builder, location,
                                                     captureBoundsValid, valid)
                                   .getResult()
                             : valid;
  };
  for (ProposalCaptureDomain &domain : proposalCaptureDomains) {
    uint64_t valueMask =
        domain.width == 64 ? UINT64_MAX : (uint64_t{1} << domain.width) - 1;
    auto captureValue = [&](uint32_t index) {
      Value value = arith::AndIOp::create(
          builder, location, programCaptures[index], constant64(valueMask));
      if (domain.isSigned)
        value = arith::XOrIOp::create(
            builder, location, value,
            constant64(uint64_t{1} << (domain.width - 1)));
      return value;
    };
    domain.lower = constant64(domain.staticLower.value_or(0));
    Value upper = constant64(domain.staticUpper.value_or(valueMask));
    if (domain.lowerCapture) {
      Value capturedLower = captureValue(*domain.lowerCapture);
      if (domain.lowerExclusive) {
        requireValidCaptureBounds(
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                  capturedLower, constant64(valueMask)));
        capturedLower = arith::AddIOp::create(builder, location, capturedLower,
                                              constant64(1));
      }
      domain.lower = domain.staticLower
                         ? arith::MaxUIOp::create(builder, location,
                                                  domain.lower, capturedLower)
                               .getResult()
                         : capturedLower;
    }
    if (domain.upperCapture) {
      Value capturedUpper = captureValue(*domain.upperCapture);
      if (domain.upperExclusive) {
        requireValidCaptureBounds(
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::ne,
                                  capturedUpper, constant64(0)));
        capturedUpper = arith::SubIOp::create(builder, location, capturedUpper,
                                              constant64(1));
      }
      upper = domain.staticUpper ? arith::MinUIOp::create(builder, location,
                                                          upper, capturedUpper)
                                       .getResult()
                                 : capturedUpper;
    }
    if ((domain.lowerCapture && domain.upperCapture) || domain.staticLower ||
        domain.staticUpper) {
      requireValidCaptureBounds(arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::ule, domain.lower, upper));
    }
    domain.cardinality = arith::AddIOp::create(
        builder, location,
        arith::SubIOp::create(builder, location, upper, domain.lower),
        constant64(1));
  }

  Block *invalidCaptureBoundsBlock = nullptr;
  Value invalidCaptureBoundsState;
  if (captureBoundsValid) {
    invalidCaptureBoundsState = state;
    Block *validCaptureBoundsBlock = addBlock();
    invalidCaptureBoundsBlock = addBlock();
    cf::CondBranchOp::create(builder, location, captureBoundsValid,
                             validCaptureBoundsBlock, ValueRange{},
                             invalidCaptureBoundsBlock, ValueRange{});
    setCurrent(validCaptureBoundsBlock);
  }

  std::function<Value(unsigned, std::optional<Value>)> sampleSolveTable;
  if (solveBeforeTableRoot || !solveBeforeComponentTableRoots.empty()) {
    sampleSolveTable = [&](unsigned nodeIndex,
                           std::optional<Value> availableDraw) -> Value {
      const SolveBeforeTableNode &node = solveBeforeTableNodes[nodeIndex];
      if (node.children.empty()) {
        if (node.assignments.size() == 1)
          return constant64(node.assignments.front());
        Value draw = availableDraw ? *availableDraw : next64(state);
        Value index = sampleBoundedIndex(node.assignments.size(), draw);
        return selectAssignmentTable(node.assignments, index);
      }
      if (node.children.size() == 1)
        return sampleSolveTable(node.children.front(), availableDraw);

      Value draw = availableDraw ? *availableDraw : next64(state);
      Value index = sampleBoundedIndex(node.children.size(), draw);
      Value branchState = state;
      Block *merge = addBlock();
      Value mergedState = merge->addArgument(i64, location);
      Value mergedAssignment = merge->addArgument(i64, location);
      SmallVector<Block *> branches;
      for (size_t child = 0; child != node.children.size(); ++child)
        branches.push_back(addBlock());

      for (size_t child = 0; child + 1 != node.children.size(); ++child) {
        Block *nextDispatch = addBlock();
        Value selected =
            arith::CmpIOp::create(builder, location, arith::CmpIPredicate::eq,
                                  index, constant64(child));
        cf::CondBranchOp::create(builder, location, selected, branches[child],
                                 ValueRange{}, nextDispatch, ValueRange{});
        setCurrent(nextDispatch);
      }
      cf::BranchOp::create(builder, location, branches.back(), ValueRange{});

      for (auto [child, branch] : llvm::enumerate(branches)) {
        setCurrent(branch);
        state = branchState;
        Value assignment = sampleSolveTable(node.children[child], std::nullopt);
        cf::BranchOp::create(builder, location, merge,
                             ValueRange{state, assignment});
      }
      setCurrent(merge);
      state = mergedState;
      return mergedAssignment;
    };
  }
  if (solveBeforeTableRoot) {
    sampledSolveBeforeAssignment =
        sampleSolveTable(*solveBeforeTableRoot, randomDraw);
  }

  if (validAssignmentTable && !solveBeforeTableRoot &&
      !powerOfTwoAssignmentTable)
    start = sampleBoundedIndex(proposalAssignments.size(), randomDraw);

  if (validAssignmentTables) {
    sampledComponentAssignment = constant64(0);
    if (!solveBeforeComponentTableRoots.empty()) {
      for (unsigned root : solveBeforeComponentTableRoots) {
        Value selected = sampleSolveTable(root, std::nullopt);
        sampledComponentAssignment = arith::OrIOp::create(
            builder, location, sampledComponentAssignment, selected);
      }
    } else {
      for (const ProposalAssignmentTable &table : proposalAssignmentTables) {
        Value index = constant64(0);
        if (table.assignments.size() != 1) {
          Value draw = next64(state);
          index = sampleBoundedIndex(table.assignments.size(), draw);
        }
        Value selected = selectAssignmentTable(table.assignments, index);
        sampledComponentAssignment = arith::OrIOp::create(
            builder, location, sampledComponentAssignment, selected);
      }
    }
  }
  for (ProposalDomain &domain : proposalDomains) {
    if (domain.powerOfTwo)
      continue;
    Value draw = next64(state);
    domain.sampledIndex = sampleBoundedIndex(domain.cardinality, draw);
  }
  for (ProposalCaptureDomain &bound : proposalCaptureDomains) {
    Value draw = next64(state);
    bound.sampledIndex = sampleDynamicBoundedIndex(bound.cardinality, draw);
  }
  sim::SimManagedStoreOp::create(builder, location, state, stateReference);

  Block *dispatchBlock = current;
  Block *loop = addBlock();
  Block *advance = addBlock();
  Block *fallbackBlock = addBlock();
  Block *modeLoop = addBlock();
  Block *modeAdvance = addBlock();
  Block *modeFallbackBlock = addBlock();
  Block *commit = addBlock();
  Block *postBlock = addBlock();
  Block *failedBlock = addBlock();
  Block *done = addBlock();
  Value counter = loop->addArgument(i64, location);
  Value attempt = loop->addArgument(i64, location);
  Value fallbackStart = fallbackBlock->addArgument(i64, location);
  Value modeCounter = modeLoop->addArgument(i64, location);
  Value modeAttempt = modeLoop->addArgument(i64, location);
  Value modeFallbackStart = modeFallbackBlock->addArgument(i64, location);
  Value commitCounter = commit->addArgument(i64, location);
  Value doneResult = done->addArgument(builder.getI1Type(), location);

  if (invalidCaptureBoundsBlock) {
    setCurrent(invalidCaptureBoundsBlock);
    sim::SimManagedStoreOp::create(builder, location, invalidCaptureBoundsState,
                                   stateReference);
    cf::BranchOp::create(builder, location, failedBlock, ValueRange{});
  }
  if (invalidDistWeightsBlock) {
    setCurrent(invalidDistWeightsBlock);
    sim::SimManagedStoreOp::create(builder, location, invalidDistWeightsState,
                                   stateReference);
    cf::BranchOp::create(builder, location, failedBlock, ValueRange{});
  }

  auto materializeCandidates =
      [&](Value assignment) -> FailureOr<SmallVector<Value>> {
    SmallVector<Value> candidates;
    uint64_t offset = 0;
    for (const Property &property : planned) {
      Value bits = assignment;
      if (offset != 0)
        bits =
            arith::ShRUIOp::create(builder, location, bits, constant64(offset));
      Type integerType =
          IntegerType::get(function.getContext(), property.width);
      if (property.width != 64)
        bits = arith::TruncIOp::create(builder, location, integerType, bits);
      FailureOr<Value> converted =
          convert(bits, property.type, false, location, property.isSigned);
      if (failed(converted))
        return failure();
      candidates.push_back(*converted);
      offset += property.width;
    }
    return candidates;
  };

  struct ConstraintCheck {
    Value hard;
    Value preferred;
  };
  auto materializeConstraintCheck =
      [&](Value assignment) -> FailureOr<ConstraintCheck> {
    FailureOr<SmallVector<Value>> candidates =
        materializeCandidates(assignment);
    if (failed(candidates))
      return failure();
    Value savedThis = thisObject;
    SmallVector<Value> savedCandidates = std::move(randomizeCandidateValues);
    llvm::scope_exit restoreBindings([&] {
      thisObject = savedThis;
      randomizeCandidateValues = std::move(savedCandidates);
    });
    thisObject = receiver;
    randomizeCandidateValues = *candidates;

    Value softSatisfied;
    if (hasSoftConstraint)
      softSatisfied = arith::ConstantOp::create(
          builder, location, builder.getI1Type(), builder.getBoolAttr(true));

    std::function<FailureOr<Value>(Operation *, Operation *)> lowerConstraint =
        [&](Operation *constraint,
            Operation *softTarget) -> FailureOr<Value> {
      SmallVector<Operation *> nested = getChildren(constraint);
      if (isa<semantic::SVConstraintListOp>(constraint)) {
        Value result = arith::ConstantOp::create(
            builder, getSemanticLocation(constraint), builder.getI1Type(),
            builder.getBoolAttr(true));
        for (Operation *item : nested) {
          FailureOr<Value> itemResult = lowerConstraint(item, softTarget);
          if (failed(itemResult))
            return failure();
          result = arith::AndIOp::create(builder, getSemanticLocation(item),
                                         result, *itemResult);
        }
        return result;
      }
      if (isa<semantic::SVSolveBeforeConstraintOp>(constraint))
        return arith::ConstantOp::create(
                   builder, getSemanticLocation(constraint),
                   builder.getI1Type(), builder.getBoolAttr(true))
            .getResult();
      if (auto expression =
              dyn_cast<semantic::SVExpressionConstraintOp>(constraint)) {
        bool selected = softTarget ? softTarget == constraint
                                   : !expression.getIsSoft();
        if (!selected)
          return arith::ConstantOp::create(
                     builder, getSemanticLocation(constraint),
                     builder.getI1Type(), builder.getBoolAttr(true))
              .getResult();
        if (nested.size() != 1) {
          emitError(getSemanticLocation(constraint))
              << "expression constraint does not contain one predicate";
          return failure();
        }
        if (isa<semantic::SVDistExpressionOp>(nested.front())) {
          auto plan = llvm::find_if(distPlans, [&](const DistPlan &candidate) {
            return candidate.source == nested.front();
          });
          if (plan == distPlans.end() ||
              plan->propertyIndex >= randomizeCandidateValues.size()) {
            emitError(getSemanticLocation(nested.front()))
                << "dist constraint has no frozen weighted plan";
            return failure();
          }
          const Property &property = planned[plan->propertyIndex];
          FailureOr<Value> scalar = toPackedScalar(
              randomizeCandidateValues[plan->propertyIndex],
              getSemanticLocation(nested.front()));
          FailureOr<Value> extended =
              succeeded(scalar)
                  ? convert(*scalar, i64, false,
                            getSemanticLocation(nested.front()), false)
                  : FailureOr<Value>(failure());
          if (failed(extended))
            return failure();
          Value field = *extended;
          uint64_t valueMask = property.width == 64
                                   ? UINT64_MAX
                                   : (uint64_t{1} << property.width) - 1;
          field = arith::AndIOp::create(builder, location, field,
                                        constant64(valueMask));
          if (property.isSigned)
            field = arith::XOrIOp::create(
                builder, location, field,
                constant64(uint64_t{1} << (property.width - 1)));
          Value supported = arith::ConstantOp::create(
              builder, location, builder.getI1Type(), builder.getBoolAttr(false));
          for (const DistRangePlan &range : plan->ranges) {
            Value matches;
            if (range.cardinality == 0) {
              matches = arith::ConstantOp::create(
                  builder, location, builder.getI1Type(),
                  builder.getBoolAttr(true));
            } else {
              Value atOrAbove = arith::CmpIOp::create(
                  builder, location, arith::CmpIPredicate::uge, field,
                  constant64(range.lower));
              Value distance = arith::SubIOp::create(
                  builder, location, field, constant64(range.lower));
              Value belowEnd = arith::CmpIOp::create(
                  builder, location, arith::CmpIPredicate::ult, distance,
                  constant64(range.cardinality));
              matches = arith::AndIOp::create(builder, location, atOrAbove,
                                               belowEnd);
            }
            arith::CmpIPredicate positivePredicate =
                range.weightSigned ? arith::CmpIPredicate::sgt
                                   : arith::CmpIPredicate::ugt;
            Value positive = arith::CmpIOp::create(
                builder, location, positivePredicate, range.weight,
                constant64(0));
            Value active =
                arith::AndIOp::create(builder, location, matches, positive);
            supported = arith::OrIOp::create(builder, location, supported,
                                              active);
          }
          return supported;
        }
        FailureOr<Value> value = lowerExpression(nested.front());
        if (failed(value))
          return failure();
        FailureOr<Value> predicate =
            truthValue(*value, getSemanticLocation(constraint));
        if (failed(predicate))
          return failure();
        return *predicate;
      }
      if (isa<semantic::SVImplicationConstraintOp>(constraint)) {
        if (nested.size() != 2) {
          emitError(getSemanticLocation(constraint))
              << "implication constraint does not contain a predicate and body";
          return failure();
        }
        FailureOr<Value> predicateValue = lowerExpression(nested.front());
        FailureOr<Value> body =
            lowerConstraint(nested.back(), softTarget);
        if (failed(predicateValue) || failed(body))
          return failure();
        FailureOr<Value> predicate =
            truthValue(*predicateValue, getSemanticLocation(nested.front()));
        if (failed(predicate))
          return failure();
        Value falseValue = arith::ConstantOp::create(
            builder, getSemanticLocation(constraint), builder.getI1Type(),
            builder.getBoolAttr(false));
        Value notPredicate = arith::CmpIOp::create(
            builder, getSemanticLocation(constraint), arith::CmpIPredicate::eq,
            *predicate, falseValue);
        return arith::OrIOp::create(builder, getSemanticLocation(constraint),
                                    notPredicate, *body)
            .getResult();
      }
      if (auto conditional =
              dyn_cast<semantic::SVConditionalConstraintOp>(constraint)) {
        size_t expected = conditional.getHasElse() ? 3 : 2;
        if (nested.size() != expected) {
          emitError(getSemanticLocation(constraint))
              << "conditional constraint has inconsistent branch inventory";
          return failure();
        }
        FailureOr<Value> predicateValue = lowerExpression(nested[0]);
        FailureOr<Value> thenValue = lowerConstraint(nested[1], softTarget);
        if (failed(predicateValue) || failed(thenValue))
          return failure();
        FailureOr<Value> predicate =
            truthValue(*predicateValue, getSemanticLocation(nested[0]));
        if (failed(predicate))
          return failure();
        Value elseValue;
        if (conditional.getHasElse()) {
          FailureOr<Value> loweredElse =
              lowerConstraint(nested[2], softTarget);
          if (failed(loweredElse))
            return failure();
          elseValue = *loweredElse;
        } else {
          elseValue = arith::ConstantOp::create(
              builder, getSemanticLocation(constraint), builder.getI1Type(),
              builder.getBoolAttr(true));
        }
        return arith::SelectOp::create(builder, getSemanticLocation(constraint),
                                       *predicate, *thenValue, elseValue)
            .getResult();
      }
      if (isa<semantic::SVUniquenessConstraintOp>(constraint)) {
        if (softTarget)
          return arith::ConstantOp::create(
                     builder, getSemanticLocation(constraint),
                     builder.getI1Type(), builder.getBoolAttr(true))
              .getResult();
        SmallVector<Value> values;
        for (Operation *item : nested) {
          FailureOr<Value> value = lowerExpression(item);
          if (failed(value))
            return failure();
          values.push_back(*value);
        }
        Value result = arith::ConstantOp::create(
            builder, getSemanticLocation(constraint), builder.getI1Type(),
            builder.getBoolAttr(true));
        for (size_t left = 0; left < values.size(); ++left)
          for (size_t right = left + 1; right < values.size(); ++right) {
            if (values[left].getType() != values[right].getType()) {
              emitError(getSemanticLocation(constraint))
                  << "uniqueness operands do not have one normalized type";
              return failure();
            }
            FailureOr<Value> equal = conditionalEqual(
                values[left], values[right], values[left].getType(),
                getSemanticLocation(constraint), true);
            if (failed(equal))
              return failure();
            Value falseValue = arith::ConstantOp::create(
                builder, getSemanticLocation(constraint), builder.getI1Type(),
                builder.getBoolAttr(false));
            Value distinct = arith::CmpIOp::create(
                builder, getSemanticLocation(constraint),
                arith::CmpIPredicate::eq, *equal, falseValue);
            result = arith::AndIOp::create(
                builder, getSemanticLocation(constraint), result, distinct);
          }
        return result;
      }
      emitError(getSemanticLocation(constraint))
          << "unsupported executable constraint node " << constraint->getName();
      return failure();
    };

    Value satisfied = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    for (auto [index, constraint] : llvm::enumerate(children)) {
      if (index == receiverIndex)
        continue;
      bool hasHard = false;
      SmallVector<semantic::SVExpressionConstraintOp> softConstraints;
      constraint->walk([&](Operation *nested) {
        if (auto expression =
                dyn_cast<semantic::SVExpressionConstraintOp>(nested)) {
          if (expression.getIsSoft())
            softConstraints.push_back(expression);
          else
            hasHard = true;
        } else if (isa<semantic::SVUniquenessConstraintOp>(nested)) {
          hasHard = true;
        }
      });
      FailureOr<Value> hard =
          hasHard
              ? lowerConstraint(constraint, /*softTarget=*/nullptr)
              : FailureOr<Value>(arith::ConstantOp::create(
                                     builder, getSemanticLocation(constraint),
                                     builder.getI1Type(),
                                     builder.getBoolAttr(true))
                                     .getResult());
      if (failed(hard))
        return failure();
      SmallVector<Value> softValues;
      for (semantic::SVExpressionConstraintOp soft : softConstraints) {
        FailureOr<Value> value = lowerConstraint(constraint, soft);
        if (failed(value))
          return failure();
        softValues.push_back(*value);
      }
      if (auto block = constraint->getAttrOfType<IntegerAttr>(
              randomConstraintBlockAttrName)) {
        APInt blockIndex = block.getValue();
        if (blockIndex.isNegative() || blockIndex.getActiveBits() > 64 ||
            blockIndex.getZExtValue() >= constraintCount) {
          emitError(getSemanticLocation(constraint))
              << "random constraint block index is malformed";
          return failure();
        }
        uint64_t blockBit = uint64_t{1} << blockIndex.getZExtValue();
        Value selected = arith::AndIOp::create(
            builder, location, relevantConstraintMode,
            constant64(blockBit));
        Value disabled = arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne, selected,
            constant64(0));
        Value trueValue = arith::ConstantOp::create(
            builder, location, builder.getI1Type(), builder.getBoolAttr(true));
        hard = arith::SelectOp::create(builder, location, disabled, trueValue,
                                       *hard)
                   .getResult();
        for (Value &soft : softValues)
          soft = arith::SelectOp::create(builder, location, disabled,
                                         trueValue, soft);
      }
      satisfied = arith::AndIOp::create(builder, location, satisfied, *hard);
      for (Value soft : softValues)
        softSatisfied = arith::AndIOp::create(builder, location, softSatisfied,
                                              soft);
    }
    thisObject = savedThis;
    randomizeCandidateValues = std::move(savedCandidates);
    restoreBindings.release();
    Value preferred =
        hasSoftConstraint
            ? arith::AndIOp::create(builder, location, satisfied, softSatisfied)
                  .getResult()
            : satisfied;
    return ConstraintCheck{satisfied, preferred};
  };

  auto advanceEnabledProperties = [&](Value current) {
    Value next = current;
    Value carry = arith::ConstantOp::create(
        builder, location, builder.getI1Type(), builder.getBoolAttr(true));
    uint64_t offset = 0;
    for (auto [property, enabled] :
         llvm::zip_equal(planned, propertyEnabled)) {
      uint64_t valueMask = property.width == 64
                               ? UINT64_MAX
                               : (uint64_t{1} << property.width) - 1;
      uint64_t aggregateMask = valueMask << offset;
      if (property.isRandC) {
        offset += property.width;
        continue;
      }
      Value aggregateMaskValue = constant64(aggregateMask);
      Value field = arith::AndIOp::create(builder, location, current,
                                          aggregateMaskValue);
      Value wrapped = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, field,
          aggregateMaskValue);
      Value incremented = arith::AndIOp::create(
          builder, location,
          arith::AddIOp::create(builder, location, field,
                                constant64(uint64_t{1} << offset)),
          aggregateMaskValue);
      Value updated = arith::OrIOp::create(
          builder, location,
          arith::AndIOp::create(builder, location, next,
                                constant64(~aggregateMask)),
          incremented);
      Value update =
          arith::AndIOp::create(builder, location, carry, enabled);
      next = arith::SelectOp::create(builder, location, update, updated, next);
      Value disabled = arith::XOrIOp::create(
          builder, location, enabled,
          arith::ConstantOp::create(builder, location, builder.getI1Type(),
                                    builder.getBoolAttr(true)));
      Value propagate =
          arith::OrIOp::create(builder, location, disabled, wrapped);
      carry = arith::AndIOp::create(builder, location, carry, propagate);
      offset += property.width;
    }
    return next;
  };

  setCurrent(disabledCheckBlock);
  FailureOr<ConstraintCheck> disabledCheck =
      materializeConstraintCheck(currentAssignment);
  if (failed(disabledCheck))
    return failure();
  cf::CondBranchOp::create(builder, location, disabledCheck->hard, postBlock,
                           ValueRange{}, failedBlock, ValueRange{});

  setCurrent(loop);
  FailureOr<Value> proposal = materializeProposal(counter, attempt);
  if (failed(proposal))
    return failure();
  if (exactProposal) {
    Block *partialCheck = addBlock();
    cf::CondBranchOp::create(builder, location, randomizationEnabled, commit,
                             ValueRange{*proposal}, partialCheck, ValueRange{});
    setCurrent(partialCheck);
    Value assignment = arith::OrIOp::create(
        builder, location,
        arith::AndIOp::create(builder, location, *proposal, mutableMask),
        fixedAssignment);
    FailureOr<ConstraintCheck> check = materializeConstraintCheck(assignment);
    if (failed(check))
      return failure();
    cf::CondBranchOp::create(builder, location, check->preferred, commit,
                             ValueRange{assignment}, advance, ValueRange{});
  } else {
    Value assignment = arith::OrIOp::create(
        builder, location,
        arith::AndIOp::create(builder, location, *proposal, mutableMask),
        fixedAssignment);
    FailureOr<ConstraintCheck> check = materializeConstraintCheck(assignment);
    if (failed(check))
      return failure();
    cf::CondBranchOp::create(builder, location, check->preferred, commit,
                             ValueRange{assignment}, advance, ValueRange{});
  }
  setCurrent(advance);
  Value next = advanceEnabledProperties(counter);
  Value nextAttempt =
      arith::AddIOp::create(builder, location, attempt, constant64(1));
  Value domainExhausted = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, next, start);
  Value samplerExhausted =
      arith::CmpIOp::create(builder, location, arith::CmpIPredicate::uge,
                            nextAttempt, constant64(64));
  Value exhausted = arith::OrIOp::create(builder, location, domainExhausted,
                                         samplerExhausted);
  cf::CondBranchOp::create(builder, location, exhausted, fallbackBlock,
                           ValueRange{next}, loop,
                           ValueRange{next, nextAttempt});

  setCurrent(fallbackBlock);
  if (analysis.satisfiability == solver::Satisfiability::Unsatisfiable) {
    emitWarning(location)
        << "randomize hard constraints are statically unsatisfiable ("
        << analysis.backend << ")";
    cf::BranchOp::create(builder, location, failedBlock, ValueRange{});
  } else {
    auto fallback = sim::SimRandomSolveOp::create(
        builder, location, function.getBody().front().getArgument(0),
        fallbackStart, mutableMask, relevantConstraintMode,
        constant64(fallbackAttempts), state, increment, programCaptures,
        builder.getStringAttr(StringRef(
            reinterpret_cast<const char *>(program.data()), program.size())));
    sim::SimManagedStoreOp::create(builder, location,
                                   fallback.getNextRngState(), stateReference);
    cf::CondBranchOp::create(builder, location, fallback.getSuccess(), commit,
                             ValueRange{fallback.getAssignment()}, failedBlock,
                             ValueRange{});
  }

  setCurrent(modeSamplingDispatchBlock);
  sim::SimManagedStoreOp::create(builder, location, modeState, stateReference);
  if (hasSolveBefore)
    cf::BranchOp::create(builder, location, modeFallbackBlock,
                         ValueRange{modeStart});
  else
    cf::BranchOp::create(builder, location, modeLoop,
                         ValueRange{modeStart, constant64(0)});

  setCurrent(modeLoop);
  FailureOr<ConstraintCheck> modeCheck =
      materializeConstraintCheck(modeCounter);
  if (failed(modeCheck))
    return failure();
  cf::CondBranchOp::create(builder, location, modeCheck->preferred, commit,
                           ValueRange{modeCounter}, modeAdvance, ValueRange{});

  setCurrent(modeAdvance);
  Value modeNext = advanceEnabledProperties(modeCounter);
  Value modeNextAttempt =
      arith::AddIOp::create(builder, location, modeAttempt, constant64(1));
  Value modeDomainExhausted = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, modeNext, modeStart);
  Value modeSamplerExhausted =
      arith::CmpIOp::create(builder, location, arith::CmpIPredicate::uge,
                            modeNextAttempt, constant64(64));
  Value modeExhausted = arith::OrIOp::create(
      builder, location, modeDomainExhausted, modeSamplerExhausted);
  cf::CondBranchOp::create(builder, location, modeExhausted, modeFallbackBlock,
                           ValueRange{modeNext}, modeLoop,
                           ValueRange{modeNext, modeNextAttempt});

  setCurrent(modeFallbackBlock);
  auto modeFallback = sim::SimRandomSolveOp::create(
      builder, location, function.getBody().front().getArgument(0),
      modeFallbackStart, mutableMask, relevantConstraintMode,
      constant64(fallbackAttempts), modeState, increment, programCaptures,
      builder.getStringAttr(StringRef(
          reinterpret_cast<const char *>(program.data()), program.size())));
  sim::SimManagedStoreOp::create(
      builder, location, modeFallback.getNextRngState(), stateReference);
  cf::CondBranchOp::create(builder, location, modeFallback.getSuccess(), commit,
                           ValueRange{modeFallback.getAssignment()},
                           failedBlock, ValueRange{});

  // Static UNSAT is known for every possible runtime capture value when all
  // constraint blocks are enabled. Bypass planned sampling instead of spending
  // its deterministic rejection budget on a set the compiler has already
  // proved impossible. A disabled block takes the separate live-mask path,
  // where the reduced constraint set may be satisfiable. The object stream
  // draw above remains observable in either case.
  setCurrent(dispatchBlock);
  if (analysis.satisfiability == solver::Satisfiability::Unsatisfiable)
    cf::BranchOp::create(builder, location, failedBlock, ValueRange{});
  else if (solveBeforeRequiresRuntime)
    cf::BranchOp::create(builder, location, fallbackBlock, ValueRange{start});
  else
    cf::BranchOp::create(builder, location, loop,
                         ValueRange{start, constant64(0)});

  setCurrent(commit);
  FailureOr<SmallVector<Value>> committed =
      materializeCandidates(commitCounter);
  if (failed(committed))
    return failure();
  Block *commitDone = addBlock();
  for (auto [property, candidate, enabled] :
       llvm::zip_equal(planned, *committed, propertyEnabled)) {
    Block *store = addBlock();
    Block *nextProperty = addBlock();
    cf::CondBranchOp::create(builder, location, enabled, store, ValueRange{},
                             nextProperty, ValueRange{});
    setCurrent(store);
    sim::SimManagedStoreOp::create(builder, location, candidate,
                                   property.reference);
    if (property.isRandC) {
      sim::SimManagedStoreOp::create(builder, location, property.nextRandcKey,
                                     property.randcKeyReference);
      sim::SimManagedStoreOp::create(builder, location,
                                     property.nextRandcPosition,
                                     property.randcPositionReference);
    }
    cf::BranchOp::create(builder, location, nextProperty);
    setCurrent(nextProperty);
  }
  cf::BranchOp::create(builder, location, commitDone);
  setCurrent(commitDone);
  cf::BranchOp::create(builder, location, postBlock);

  setCurrent(postBlock);
  if (!checkerOnly &&
      failed(callLifecycleHook(randomPostHookAttrName,
                               randomPostHookOwnerAttrName,
                               randomPostHookCapturesAttrName,
                               randomPostHookReadCapturesAttrName)))
    return failure();
  Value success = arith::ConstantOp::create(
      builder, location, builder.getI1Type(), builder.getBoolAttr(true));
  cf::BranchOp::create(builder, location, done, ValueRange{success});

  setCurrent(failedBlock);
  Value noSolution = arith::ConstantOp::create(
      builder, location, builder.getI1Type(), builder.getBoolAttr(false));
  cf::BranchOp::create(builder, location, done, ValueRange{noSolution});

  setCurrent(done);
  FailureOr<Type> resultType = getNormalizedSemanticType(op);
  if (failed(resultType))
    return failure();
  return convert(doneResult, *resultType, false, location);
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
