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
  if (op->hasAttr(randomizeAttrName))
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
      !children.empty()) {
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

FailureOr<Value> UnitLowering::lowerRandomize(semantic::SVCallExpressionOp op) {
  Location location = getSemanticLocation(op);
  SmallVector<Operation *> children = getChildren(op);
  auto properties = op->getAttrOfType<ArrayAttr>(randomPropertiesAttrName);
  auto totalWidthAttr =
      op->getAttrOfType<IntegerAttr>(randomTotalWidthAttrName);
  auto receiverIndexAttr =
      op->getAttrOfType<IntegerAttr>(randomReceiverIndexAttrName);
  if (children.empty() || !properties || !totalWidthAttr ||
      !receiverIndexAttr) {
    emitError(location) << "randomize call has no frozen constraint plan";
    return failure();
  }
  APInt receiverIndexValue = receiverIndexAttr.getValue();
  APInt totalWidthValue = totalWidthAttr.getValue();
  if (receiverIndexValue.isNegative() ||
      receiverIndexValue.getActiveBits() > 64 || totalWidthValue.isNegative() ||
      totalWidthValue.getActiveBits() > 64 ||
      receiverIndexValue.getZExtValue() >= children.size()) {
    emitError(location) << "randomize call has malformed constraint metadata";
    return failure();
  }
  unsigned receiverIndex =
      static_cast<unsigned>(receiverIndexValue.getZExtValue());
  uint64_t totalWidth = totalWidthValue.getZExtValue();
  if (totalWidth > 64) {
    emitError(location) << "randomize plan exceeds 64 aggregate rand bits";
    return failure();
  }

  FailureOr<Value> loweredReceiver = lowerExpression(children[receiverIndex]);
  auto objectType =
      succeeded(loweredReceiver)
          ? dyn_cast<sim::ClassHandleType>((*loweredReceiver).getType())
          : sim::ClassHandleType{};
  if (failed(loweredReceiver) || !objectType)
    return failure();
  Value receiver = *loweredReceiver;

  struct Property {
    Type type;
    unsigned width;
    bool isSigned;
    Value reference;
  };
  SmallVector<Property> planned;
  uint64_t plannedWidth = 0;
  for (Attribute propertyAttr : properties) {
    auto property = dyn_cast<DictionaryAttr>(propertyAttr);
    auto field = property ? property.getAs<FlatSymbolRefAttr>("field")
                          : FlatSymbolRefAttr{};
    auto typeAttr = property ? property.getAs<TypeAttr>("type") : TypeAttr{};
    auto widthAttr =
        property ? property.getAs<IntegerAttr>("width") : IntegerAttr{};
    auto signedAttr =
        property ? property.getAs<BoolAttr>("is_signed") : BoolAttr{};
    if (!field || !typeAttr || !widthAttr || !signedAttr ||
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
    planned.push_back(
        {type, static_cast<unsigned>(width), signedAttr.getValue(), reference});
    plannedWidth += width;
  }
  if (plannedWidth != totalWidth) {
    emitError(location) << "randomize property plan width is inconsistent";
    return failure();
  }

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
    emitError(location) << "randomize receiver has no object-local stream";
    return failure();
  }
  Type i64 = builder.getI64Type();
  Type randomReferenceType = sim::ManagedRefType::get(
      function.getContext(), i64, objectType.getClassName());
  Value stateReference = sim::SimClassFieldRefOp::create(
      builder, location, randomReferenceType, receiver, stateField);
  Value incrementReference = sim::SimClassFieldRefOp::create(
      builder, location, randomReferenceType, receiver, incrementField);
  Value state =
      sim::SimManagedLoadOp::create(builder, location, i64, stateReference);
  Value increment =
      sim::SimManagedLoadOp::create(builder, location, i64, incrementReference);

  auto constant64 = [&](uint64_t value) -> Value {
    return arith::ConstantOp::create(
        builder, location, i64, builder.getIntegerAttr(i64, APInt(64, value)));
  };
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
  Value randomDraw = next64(state);

  APInt domainMask = totalWidth == 64 ? APInt::getAllOnes(64)
                                      : APInt::getLowBitsSet(64, totalWidth);
  Value mask = arith::ConstantOp::create(
      builder, location, i64, builder.getIntegerAttr(i64, domainMask));
  Value start = arith::AndIOp::create(builder, location, randomDraw, mask);

  bool hasSoftConstraint = false;
  for (auto [index, child] : llvm::enumerate(children)) {
    if (index == receiverIndex)
      continue;
    child->walk([&](semantic::SVExpressionConstraintOp expression) {
      hasSoftConstraint |= expression.getIsSoft();
    });
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
  auto emitLiteral = [&](bool value) {
    instruction(OBELISK_RT_RANDOM_PUSH_LITERAL_V1, 1, false, 0, value);
  };
  std::function<LogicalResult(Operation *)> emitProgramExpression;
  emitProgramExpression = [&](Operation *expression) -> LogicalResult {
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

    SmallVector<Operation *> nested = getChildren(expression);
    if (isa<semantic::SVConversionExpressionOp>(expression)) {
      if (nested.size() != 1 || failed(emitProgramExpression(nested.front())))
        return failure();
      instruction(OBELISK_RT_RANDOM_CAST_V1, *width,
                  isSignedNode(nested.front()));
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

  std::function<LogicalResult(Operation *)> emitProgramConstraint;
  emitProgramConstraint = [&](Operation *constraint) -> LogicalResult {
    SmallVector<Operation *> nested = getChildren(constraint);
    if (isa<semantic::SVConstraintListOp>(constraint)) {
      if (nested.empty()) {
        emitLiteral(true);
        return success();
      }
      if (failed(emitProgramConstraint(nested.front())))
        return failure();
      for (Operation *item : ArrayRef(nested).drop_front()) {
        if (failed(emitProgramConstraint(item)))
          return failure();
        instruction(OBELISK_RT_RANDOM_LOGICAL_AND_V1, 1);
      }
      return success();
    }
    if (isa<semantic::SVExpressionConstraintOp>(constraint))
      return nested.size() == 1 ? emitProgramExpression(nested.front())
                                : failure();
    if (isa<semantic::SVImplicationConstraintOp>(constraint)) {
      if (nested.size() != 2 || failed(emitProgramExpression(nested[0])) ||
          failed(emitProgramConstraint(nested[1])))
        return failure();
      instruction(OBELISK_RT_RANDOM_LOGICAL_IMPLIES_V1, 1);
      return success();
    }
    if (auto conditional =
            dyn_cast<semantic::SVConditionalConstraintOp>(constraint)) {
      if (nested.size() != (conditional.getHasElse() ? 3u : 2u) ||
          failed(emitProgramExpression(nested[0])) ||
          failed(emitProgramConstraint(nested[1])))
        return failure();
      if (conditional.getHasElse()) {
        if (failed(emitProgramConstraint(nested[2])))
          return failure();
      } else {
        emitLiteral(true);
      }
      instruction(OBELISK_RT_RANDOM_SELECT_V1, 1);
      return success();
    }
    if (isa<semantic::SVUniquenessConstraintOp>(constraint)) {
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
  for (auto [index, root] : llvm::enumerate(children)) {
    if (index == receiverIndex)
      continue;
    SmallVector<Operation *> items = isa<semantic::SVConstraintListOp>(root)
                                         ? getChildren(root)
                                         : SmallVector<Operation *>{root};
    for (Operation *item : items) {
      bool soft = false;
      if (auto expression = dyn_cast<semantic::SVExpressionConstraintOp>(item))
        soft = expression.getIsSoft();
      if (failed(emitProgramConstraint(item)))
        return failure();
      instruction(soft ? OBELISK_RT_RANDOM_END_SOFT_V1
                       : OBELISK_RT_RANDOM_END_HARD_V1,
                  1);
      emittedSoft |= soft;
      emittedHard |= !soft;
    }
  }
  if (!emittedHard) {
    emitLiteral(true);
    instruction(OBELISK_RT_RANDOM_END_HARD_V1, 1);
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
  append32(emittedSoft ? OBELISK_RT_RANDOM_PROGRAM_HAS_SOFT : 0);
  for (const EncodedInstruction &encoded : programInstructions) {
    program.push_back(encoded.opcode);
    program.push_back(encoded.width);
    program.push_back(encoded.flags);
    program.push_back(0);
    append32(encoded.operand);
    append64(encoded.immediate);
  }
  uint64_t fallbackAttempts =
      totalWidth <= 20 ? (uint64_t{1} << totalWidth) : (uint64_t{1} << 20);
  solver::RandomProgramAnalysis analysis =
      solver::analyzeRandomProgram(program.data(), program.size());

  SmallVector<uint64_t> proposalAssignments;
  constexpr size_t maxMaterializedAssignmentTableSize = 16;
  uint64_t aggregateMask =
      totalWidth == 64 ? UINT64_MAX : (uint64_t{1} << totalWidth) - 1;
  bool validAssignmentTable =
      analysis.assignmentTables.empty() && !analysis.assignmentTable.empty() &&
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
    bool conflicts = bound.width == 0 || bound.width > 64 ||
                     bound.captureIndex >= programCaptures.size() ||
                     !isPropertyField(bound.offset, bound.width);
    if (conflicts)
      continue;
    auto found = llvm::find_if(
        proposalCaptureDomains, [&](const ProposalCaptureDomain &selected) {
          return selected.offset == bound.offset &&
                 selected.width == bound.width;
        });
    if (found != proposalCaptureDomains.end() &&
        found->isSigned != bound.isSigned)
      continue;
    if (found == proposalCaptureDomains.end()) {
      auto staticDomain = llvm::find_if(
          proposalDomains, [&](const ProposalDomain &domain) {
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
      proposalCaptureDomains.push_back(
          {bound.offset, bound.width, std::nullopt, std::nullopt, false, false,
           bound.isSigned, staticLower, staticUpper, {}, {}, {}});
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
  Value sampledComponentAssignment;
  auto materializeProposal = [&](Value rawAssignment,
                                 Value attempt) -> FailureOr<Value> {
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
      Value fullCardinality = arith::CmpIOp::create(
          builder, location, arith::CmpIPredicate::eq, bound.cardinality,
          constant64(0));
      Value safeCardinality = arith::SelectOp::create(
          builder, location, fullCardinality, constant64(1),
          bound.cardinality);
      Value reducedStep = arith::RemUIOp::create(
          builder, location, attempt, safeCardinality);
      Value step = arith::SelectOp::create(
          builder, location, fullCardinality, attempt, reducedStep);
      Value threshold = arith::SubIOp::create(builder, location,
                                              bound.cardinality, step);
      Value wraps = arith::CmpIOp::create(builder, location,
                                          arith::CmpIPredicate::uge,
                                          bound.sampledIndex, threshold);
      Value linear = arith::AddIOp::create(builder, location,
                                           bound.sampledIndex, step);
      Value wrapped = arith::SubIOp::create(builder, location,
                                            bound.sampledIndex, threshold);
      Value fieldBits = arith::SelectOp::create(builder, location, wraps,
                                                wrapped, linear);
      fieldBits = arith::AddIOp::create(builder, location, fieldBits,
                                        bound.lower);
      if (bound.isSigned)
        fieldBits = arith::XOrIOp::create(
            builder, location, fieldBits,
            constant64(uint64_t{1} << (bound.width - 1)));
      if (bound.offset != 0)
        fieldBits = arith::ShLIOp::create(builder, location, fieldBits,
                                          constant64(bound.offset));
      uint64_t fieldMask =
          bound.width == 64
              ? UINT64_MAX
              : ((uint64_t{1} << bound.width) - 1) << bound.offset;
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
    return assignment;
  };
  bool overwritesProposalDomain =
      llvm::any_of(proposalDomains, [&](const ProposalDomain &domain) {
        bool definitionTarget = llvm::any_of(
            proposalDefinitions, [&](const ProposalDefinition &definition) {
              return definition.targetOffset == domain.offset &&
                     definition.width == domain.width;
            });
        bool aliasTarget =
            llvm::any_of(proposalAliases, [&](const ProposalAlias &alias) {
              return alias.targetOffset == domain.offset &&
                     alias.width == domain.width;
            });
        return definitionTarget || aliasTarget;
      }) ||
      llvm::any_of(proposalCaptureDomains,
                   [&](const ProposalCaptureDomain &bound) {
                     bool definitionTarget = llvm::any_of(
                         proposalDefinitions,
                         [&](const ProposalDefinition &definition) {
                           return definition.targetOffset == bound.offset &&
                                  definition.width == bound.width;
                         });
                     bool aliasTarget = llvm::any_of(
                         proposalAliases, [&](const ProposalAlias &alias) {
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
                       !overwritesProposalDomain &&
                       materializesCompleteProposal;

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
    Value fullCardinality = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, cardinality,
        constant64(0));
    Value safeCardinality = arith::SelectOp::create(
        builder, location, fullCardinality, constant64(1), cardinality);
    Value negativeCardinality = arith::SubIOp::create(
        builder, location, constant64(0), safeCardinality);
    Value rejectionSize = arith::RemUIOp::create(
        builder, location, negativeCardinality, safeCardinality);
    Value limit = arith::SubIOp::create(builder, location, constant64(0),
                                        rejectionSize);
    Value acceptsAll = arith::CmpIOp::create(
        builder, location, arith::CmpIPredicate::eq, rejectionSize,
        constant64(0));
    Block *boundedLoop = addBlock();
    Block *boundedDone = addBlock();
    Value boundedState = boundedLoop->addArgument(i64, location);
    Value boundedDraw = boundedLoop->addArgument(i64, location);
    Value finalState = boundedDone->addArgument(i64, location);
    Value boundedIndex = boundedDone->addArgument(i64, location);

    cf::BranchOp::create(builder, location, boundedLoop,
                         ValueRange{state, draw});
    setCurrent(boundedLoop);
    Value reducedIndex = arith::RemUIOp::create(
        builder, location, boundedDraw, safeCardinality);
    Value index = arith::SelectOp::create(
        builder, location, fullCardinality, boundedDraw, reducedIndex);
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

  Value captureBoundsValid;
  auto requireValidCaptureBounds = [&](Value valid) {
    captureBoundsValid =
        captureBoundsValid
            ? arith::AndIOp::create(builder, location, captureBoundsValid, valid)
                  .getResult()
            : valid;
  };
  for (ProposalCaptureDomain &domain : proposalCaptureDomains) {
    uint64_t valueMask = domain.width == 64
                             ? UINT64_MAX
                             : (uint64_t{1} << domain.width) - 1;
    auto captureValue = [&](uint32_t index) {
      Value value = arith::AndIOp::create(builder, location,
                                          programCaptures[index],
                                          constant64(valueMask));
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
        requireValidCaptureBounds(arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne, capturedLower,
            constant64(valueMask)));
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
        requireValidCaptureBounds(arith::CmpIOp::create(
            builder, location, arith::CmpIPredicate::ne, capturedUpper,
            constant64(0)));
        capturedUpper = arith::SubIOp::create(builder, location, capturedUpper,
                                              constant64(1));
      }
      upper = domain.staticUpper
                  ? arith::MinUIOp::create(builder, location, upper,
                                           capturedUpper)
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

  if (validAssignmentTable && !powerOfTwoAssignmentTable)
    start = sampleBoundedIndex(proposalAssignments.size(), randomDraw);

  if (validAssignmentTables) {
    sampledComponentAssignment = constant64(0);
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
  for (ProposalDomain &domain : proposalDomains) {
    if (domain.powerOfTwo)
      continue;
    Value draw = next64(state);
    domain.sampledIndex = sampleBoundedIndex(domain.cardinality, draw);
  }
  for (ProposalCaptureDomain &bound : proposalCaptureDomains) {
    Value draw = next64(state);
    bound.sampledIndex =
        sampleDynamicBoundedIndex(bound.cardinality, draw);
  }
  sim::SimManagedStoreOp::create(builder, location, state, stateReference);

  Block *dispatchBlock = current;
  Block *loop = addBlock();
  Block *advance = addBlock();
  Block *fallbackBlock = addBlock();
  Block *commit = addBlock();
  Block *failedBlock = addBlock();
  Block *done = addBlock();
  Value counter = loop->addArgument(i64, location);
  Value attempt = loop->addArgument(i64, location);
  Value commitCounter = commit->addArgument(i64, location);
  Value doneResult = done->addArgument(builder.getI1Type(), location);

  if (invalidCaptureBoundsBlock) {
    setCurrent(invalidCaptureBoundsBlock);
    sim::SimManagedStoreOp::create(builder, location,
                                   invalidCaptureBoundsState, stateReference);
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

  setCurrent(loop);
  FailureOr<Value> proposal = materializeProposal(counter, attempt);
  if (failed(proposal))
    return failure();
  Value assignment = *proposal;
  FailureOr<SmallVector<Value>> candidates = materializeCandidates(assignment);
  if (failed(candidates))
    return failure();

  if (exactProposal) {
    cf::BranchOp::create(builder, location, commit, ValueRange{assignment});
  } else {
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

    std::function<FailureOr<Value>(Operation *)> lowerConstraint =
        [&](Operation *constraint) -> FailureOr<Value> {
      SmallVector<Operation *> nested = getChildren(constraint);
      if (isa<semantic::SVConstraintListOp>(constraint)) {
        Value result = arith::ConstantOp::create(
            builder, getSemanticLocation(constraint), builder.getI1Type(),
            builder.getBoolAttr(true));
        for (Operation *item : nested) {
          FailureOr<Value> itemResult = lowerConstraint(item);
          if (failed(itemResult))
            return failure();
          result = arith::AndIOp::create(builder, getSemanticLocation(item),
                                         result, *itemResult);
        }
        return result;
      }
      if (auto expression =
              dyn_cast<semantic::SVExpressionConstraintOp>(constraint)) {
        if (nested.size() != 1) {
          emitError(getSemanticLocation(constraint))
              << "expression constraint does not contain one predicate";
          return failure();
        }
        FailureOr<Value> value = lowerExpression(nested.front());
        if (failed(value))
          return failure();
        FailureOr<Value> predicate =
            truthValue(*value, getSemanticLocation(constraint));
        if (failed(predicate))
          return failure();
        if (!expression.getIsSoft())
          return *predicate;
        softSatisfied =
            arith::AndIOp::create(builder, getSemanticLocation(constraint),
                                  softSatisfied, *predicate);
        return arith::ConstantOp::create(
                   builder, getSemanticLocation(constraint),
                   builder.getI1Type(), builder.getBoolAttr(true))
            .getResult();
      }
      if (isa<semantic::SVImplicationConstraintOp>(constraint)) {
        if (nested.size() != 2) {
          emitError(getSemanticLocation(constraint))
              << "implication constraint does not contain a predicate and body";
          return failure();
        }
        FailureOr<Value> predicateValue = lowerExpression(nested.front());
        FailureOr<Value> body = lowerConstraint(nested.back());
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
        FailureOr<Value> thenValue = lowerConstraint(nested[1]);
        if (failed(predicateValue) || failed(thenValue))
          return failure();
        FailureOr<Value> predicate =
            truthValue(*predicateValue, getSemanticLocation(nested[0]));
        if (failed(predicate))
          return failure();
        Value elseValue;
        if (conditional.getHasElse()) {
          FailureOr<Value> loweredElse = lowerConstraint(nested[2]);
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
      FailureOr<Value> value = lowerConstraint(constraint);
      if (failed(value))
        return failure();
      satisfied = arith::AndIOp::create(builder, location, satisfied, *value);
    }
    thisObject = savedThis;
    randomizeCandidateValues = std::move(savedCandidates);
    restoreBindings.release();
    Value preferred =
        hasSoftConstraint
            ? arith::AndIOp::create(builder, location, satisfied, softSatisfied)
                  .getResult()
            : satisfied;
    cf::CondBranchOp::create(builder, location, preferred, commit,
                             ValueRange{assignment}, advance, ValueRange{});
  }

  setCurrent(advance);
  Value next = arith::AndIOp::create(
      builder, location,
      arith::AddIOp::create(builder, location, counter, constant64(1)), mask);
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
                           ValueRange{}, loop, ValueRange{next, nextAttempt});

  setCurrent(fallbackBlock);
  if (analysis.satisfiability == solver::Satisfiability::Unsatisfiable) {
    emitWarning(location)
        << "randomize hard constraints are statically unsatisfiable ("
        << analysis.backend << ")";
    cf::BranchOp::create(builder, location, failedBlock, ValueRange{});
  } else if (exactProposal) {
    cf::BranchOp::create(builder, location, failedBlock, ValueRange{});
  } else {
    auto fallback = sim::SimRandomSolveOp::create(
        builder, location, function.getBody().front().getArgument(0), next,
        constant64(fallbackAttempts), programCaptures,
        builder.getStringAttr(StringRef(
            reinterpret_cast<const char *>(program.data()), program.size())));
    cf::CondBranchOp::create(builder, location, fallback.getSuccess(), commit,
                             ValueRange{fallback.getAssignment()}, failedBlock,
                             ValueRange{});
  }

  // Static UNSAT is known for every possible runtime capture value. Bypass
  // both generated sampling tiers instead of spending their deterministic
  // rejection budgets on a constraint set the compiler has already proved
  // impossible. The object stream draw above remains observable, matching the
  // state transition of an attempted randomize call.
  setCurrent(dispatchBlock);
  if (analysis.satisfiability == solver::Satisfiability::Unsatisfiable)
    cf::BranchOp::create(builder, location, failedBlock, ValueRange{});
  else
    cf::BranchOp::create(builder, location, loop,
                         ValueRange{start, constant64(0)});

  setCurrent(commit);
  FailureOr<SmallVector<Value>> committed =
      materializeCandidates(commitCounter);
  if (failed(committed))
    return failure();
  for (auto [property, candidate] : llvm::zip_equal(planned, *committed))
    sim::SimManagedStoreOp::create(builder, location, candidate,
                                   property.reference);
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
