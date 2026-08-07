//===- LowerUnitCalls.cpp - Lower function and class calls ------------===//

#include "LowerUnit.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringSet.h"

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
  Value high = next32(state);
  Value low = next32(state);
  Value start = arith::OrIOp::create(
      builder, location,
      arith::ShLIOp::create(
          builder, location,
          arith::ExtUIOp::create(builder, location, i64, high), constant64(32)),
      arith::ExtUIOp::create(builder, location, i64, low));
  sim::SimManagedStoreOp::create(builder, location, state, stateReference);

  APInt domainMask = totalWidth == 64 ? APInt::getAllOnes(64)
                                      : APInt::getLowBitsSet(64, totalWidth);
  Value mask = arith::ConstantOp::create(
      builder, location, i64, builder.getIntegerAttr(i64, domainMask));
  start = arith::AndIOp::create(builder, location, start, mask);

  Block *loop = addBlock();
  Block *advance = addBlock();
  Block *commit = addBlock();
  Block *failedBlock = addBlock();
  Block *done = addBlock();
  Value counter = loop->addArgument(i64, location);
  Value doneResult = done->addArgument(builder.getI1Type(), location);
  cf::BranchOp::create(builder, location, loop, ValueRange{start});

  setCurrent(loop);
  SmallVector<Value> candidates;
  uint64_t offset = 0;
  for (const Property &property : planned) {
    Value bits = counter;
    if (offset != 0)
      bits =
          arith::ShRUIOp::create(builder, location, bits, constant64(offset));
    Type integerType = IntegerType::get(function.getContext(), property.width);
    if (property.width != 64)
      bits = arith::TruncIOp::create(builder, location, integerType, bits);
    FailureOr<Value> converted =
        convert(bits, property.type, false, location, property.isSigned);
    if (failed(converted))
      return failure();
    candidates.push_back(*converted);
    offset += property.width;
  }

  Value savedThis = thisObject;
  SmallVector<Value> savedCandidates = std::move(randomizeCandidateValues);
  llvm::scope_exit restoreBindings([&] {
    thisObject = savedThis;
    randomizeCandidateValues = std::move(savedCandidates);
  });
  thisObject = receiver;
  randomizeCandidateValues = candidates;

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
    if (isa<semantic::SVExpressionConstraintOp>(constraint)) {
      if (nested.size() != 1) {
        emitError(getSemanticLocation(constraint))
            << "expression constraint does not contain one predicate";
        return failure();
      }
      FailureOr<Value> value = lowerExpression(nested.front());
      return failed(value)
                 ? FailureOr<Value>(failure())
                 : truthValue(*value, getSemanticLocation(constraint));
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
  cf::CondBranchOp::create(builder, location, satisfied, commit, ValueRange{},
                           advance, ValueRange{});

  setCurrent(advance);
  Value next = arith::AndIOp::create(
      builder, location,
      arith::AddIOp::create(builder, location, counter, constant64(1)), mask);
  Value exhausted = arith::CmpIOp::create(
      builder, location, arith::CmpIPredicate::eq, next, start);
  cf::CondBranchOp::create(builder, location, exhausted, failedBlock,
                           ValueRange{}, loop, ValueRange{next});

  setCurrent(commit);
  for (auto [property, candidate] : llvm::zip_equal(planned, candidates))
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
