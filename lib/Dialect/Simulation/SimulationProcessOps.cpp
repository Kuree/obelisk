//===- SimulationProcessOps.cpp - Process, call, and reference op verifiers ===//
//
// SimFuncOp parsing/printing/verification, calls and observers, DPI, spawn and
// control ops, aggregate and union access, and the memory-slot promotion and
// destructuring interfaces for reference allocations.
//
//===----------------------------------------------------------------------===//

#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "SimulationVerifiers.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/StableHash.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Interfaces/FunctionImplementation.h"
#include "mlir/Transforms/InliningUtils.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/ADT/bit.h"

#include <algorithm>
#include <limits>
#include <optional>

using namespace mlir;

namespace obelisk::sim {

void SimFuncOp::build(OpBuilder &builder, OperationState &state, StringRef name,
                      FunctionType type, EntryKind entryKind,
                      ArrayRef<NamedAttribute> attrs,
                      ArrayRef<DictionaryAttr> argAttrs) {
  state.addAttribute(SymbolTable::getSymbolAttrName(),
                     builder.getStringAttr(name));
  state.addAttribute(getFunctionTypeAttrName(state.name), TypeAttr::get(type));
  state.addAttribute(getEntryKindAttrName(state.name),
                     EntryKindAttr::get(builder.getContext(), entryKind));
  state.attributes.append(attrs.begin(), attrs.end());
  if (!argAttrs.empty())
    state.addAttribute(getArgAttrsAttrName(state.name),
                       builder.getArrayAttr(SmallVector<Attribute>(
                           argAttrs.begin(), argAttrs.end())));
  Region *body = state.addRegion();
  // Construct the entry block without changing the caller's insertion point.
  // Function builders are routinely invoked while populating their enclosing
  // symbol table; using OpBuilder::createBlock here would redirect subsequent
  // sibling creation into this function body.
  body->push_back(new Block());
  Block *entry = &body->front();
  SmallVector<Location> locations(type.getNumInputs(), state.location);
  entry->addArguments(type.getInputs(), locations);
}

ParseResult SimFuncOp::parse(OpAsmParser &parser, OperationState &result) {
  auto buildType =
      [](Builder &builder, ArrayRef<Type> inputs, ArrayRef<Type> results,
         function_interface_impl::VariadicFlag, std::string &) -> Type {
    return builder.getFunctionType(inputs, results);
  };
  return function_interface_impl::parseFunctionOp(
      parser, result, /*allowVariadic=*/false,
      getFunctionTypeAttrName(result.name), buildType,
      getArgAttrsAttrName(result.name), getResAttrsAttrName(result.name));
}

void SimFuncOp::print(OpAsmPrinter &printer) {
  function_interface_impl::printFunctionOp(
      printer, *this, /*isVariadic=*/false, getFunctionTypeAttrName(),
      getArgAttrsAttrName(), getResAttrsAttrName());
}

LogicalResult verifyUnitBindings(SimFuncOp function) {
  Attribute raw = function->getAttr(metadata::bindings);
  if (!raw)
    return success();
  auto bindings = dyn_cast<ArrayAttr>(raw);
  if (!bindings)
    return function.emitOpError()
           << "has malformed " << metadata::bindings << ": expected an array";

  FunctionType type = function.getFunctionType();
  enum class ProviderKind { Direct, FormalLocal, Local, Constant };
  struct PathState {
    std::optional<unsigned> provider;
    std::optional<ProviderKind> providerKind;
    bool formalCopiesOut = false;
    std::optional<unsigned> taskStaticDirect;
    std::optional<unsigned> lvalueOnly;
    std::optional<unsigned> copyOutDestination;
  };
  llvm::StringMap<PathState> paths;
  SmallVector<std::optional<unsigned>> argumentBindings(type.getNumInputs());
  std::optional<unsigned> returnBinding;

  auto claimProvider = [&](StringRef path, unsigned index,
                           ProviderKind kind) -> LogicalResult {
    PathState &state = paths[path];
    if (state.provider) {
      // A static task formal also has descriptor-backed storage. Preparation
      // deliberately emits the activation-local formal first and the direct
      // static-storage binding second; UnitLowering copies in between them.
      if (function.getEntryKind() == EntryKind::Task &&
          state.providerKind == ProviderKind::FormalLocal &&
          kind == ProviderKind::Direct && !state.taskStaticDirect) {
        state.taskStaticDirect = index;
        return success();
      }
      return function.emitOpError()
             << "has malformed " << metadata::bindings << " entries #"
             << *state.provider << " and #" << index
             << ": both provide the source value for path '" << path << "'";
    }
    state.provider = index;
    state.providerKind = kind;
    return success();
  };

  for (auto [index, binding] : llvm::enumerate(bindings)) {
    if (auto local = dyn_cast<LocalBindingAttr>(binding)) {
      if (failed(claimProvider(local.getPath().getValue(), index,
                               ProviderKind::Local)))
        return failure();
      if (local.getIsReturn()) {
        if (function.getEntryKind() != EntryKind::Function)
          return function.emitOpError()
                 << "has malformed " << metadata::bindings << " entry #"
                 << index
                 << ": only a function may have a return-local binding";
        if (returnBinding)
          return function.emitOpError()
                 << "has malformed " << metadata::bindings << " entries #"
                 << *returnBinding << " and #" << index
                 << ": multiple local bindings are marked as the function "
                    "return";
        returnBinding = index;
      }
      continue;
    }
    if (auto constant = dyn_cast<ConstantBindingAttr>(binding)) {
      if (failed(claimProvider(constant.getPath().getValue(), index,
                               ProviderKind::Constant)))
        return failure();
      continue;
    }
    auto argument = dyn_cast<ArgumentBindingAttr>(binding);
    if (!argument)
      return function.emitOpError()
             << "has malformed " << metadata::bindings << " entry #" << index
             << ": expected an argument, local, or constant binding";
    if (argument.getArgument() >= type.getNumInputs())
      return function.emitOpError()
             << "has malformed " << metadata::bindings << " entry #" << index
             << ": argument index " << argument.getArgument()
             << " is outside the function signature";
    if (argument.getArgument() == 0)
      return function.emitOpError()
             << "has malformed " << metadata::bindings << " entry #" << index
             << ": context argument cannot carry a source binding";

    unsigned argumentIndex = argument.getArgument();
    if (argumentBindings[argumentIndex])
      return function.emitOpError()
             << "has malformed " << metadata::bindings << " entries #"
             << *argumentBindings[argumentIndex] << " and #" << index
             << ": both bind function argument #" << argumentIndex;
    argumentBindings[argumentIndex] = index;

    Type argumentType = type.getInput(argument.getArgument());
    StringRef path = argument.getPath().getValue();
    PathState &state = paths[path];
    switch (argument.getKind()) {
    case UnitArgumentKind::Direct:
      if (isa<ContextType>(argumentType))
        return function.emitOpError()
               << "has malformed " << metadata::bindings << " entry #" << index
               << ": direct binding cannot target a context argument";
      if (failed(claimProvider(path, index, ProviderKind::Direct)))
        return failure();
      break;
    case UnitArgumentKind::LValueOnly:
      if (!isa<RefType, ArgumentRefType, NetType, DriverType>(argumentType))
        return function.emitOpError()
               << "has malformed " << metadata::bindings << " entry #" << index
               << ": lvalue-only binding requires a storage, net, or driver "
                  "argument";
      state.lvalueOnly = index;
      break;
    case UnitArgumentKind::FormalLocal:
      if (!isNormalizedValueType(argumentType))
        return function.emitOpError()
               << "has malformed " << metadata::bindings << " entry #" << index
               << ": formal-local binding requires a normalized value "
                  "argument";
      if (failed(claimProvider(path, index, ProviderKind::FormalLocal)))
        return failure();
      state.formalCopiesOut = argument.getCopyOut();
      break;
    case UnitArgumentKind::CopyOutDestination:
      if (!isa<RefType>(argumentType))
        return function.emitOpError()
               << "has malformed " << metadata::bindings << " entry #" << index
               << ": copy-out destination requires a storage argument";
      if (state.copyOutDestination)
        return function.emitOpError()
               << "has malformed " << metadata::bindings << " entries #"
               << *state.copyOutDestination << " and #" << index
               << ": multiple copy-out destinations bind path '" << path << "'";
      state.copyOutDestination = index;
      break;
    }
  }

  for (const auto &entry : paths) {
    StringRef path = entry.getKey();
    const PathState &state = entry.getValue();
    if (state.lvalueOnly && state.providerKind &&
        *state.providerKind != ProviderKind::Direct)
      return function.emitOpError()
             << "has malformed " << metadata::bindings << " entry #"
             << *state.lvalueOnly << ": lvalue-only path '" << path
             << "' conflicts with a non-direct value binding";
    if (state.copyOutDestination &&
        (!state.providerKind ||
         *state.providerKind != ProviderKind::FormalLocal ||
         !state.formalCopiesOut))
      return function.emitOpError()
             << "has malformed " << metadata::bindings << " entry #"
             << *state.copyOutDestination << ": copy-out destination path '"
             << path << "' requires a copy-out formal-local binding";
    if (state.copyOutDestination && function.getEntryKind() != EntryKind::Task)
      return function.emitOpError()
             << "has malformed " << metadata::bindings << " entry #"
             << *state.copyOutDestination
             << ": copy-out destination bindings are valid only on tasks";
    if (function.getEntryKind() == EntryKind::Task && state.formalCopiesOut &&
        !state.copyOutDestination)
      return function.emitOpError()
             << "has malformed " << metadata::bindings << " entry #"
             << *state.provider << ": task copy-out formal path '" << path
             << "' requires a destination binding";
  }
  return success();
}

LogicalResult SimFuncOp::verify() {
  FunctionType type = getFunctionType();
  if (getDomain() == ExecutionDomain::Program &&
      getHomeRegion() != EventRegion::Reactive)
    return emitOpError(
        "program-domain code units must have reactive home region");
  if (getDomain() == ExecutionDomain::Design &&
      getHomeRegion() != EventRegion::Active &&
      getHomeRegion() != EventRegion::Observed &&
      getHomeRegion() != EventRegion::Reactive &&
      getHomeRegion() != EventRegion::Postponed)
    return emitOpError(
        "design-domain code units must have active, observed, reactive, or "
        "postponed home region");
  if (getHomeRegion() == EventRegion::Postponed &&
      failed(verifyPostponedReadOnly(*this)))
    return failure();
  if (getHomeRegion() == EventRegion::Observed) {
    WalkResult noDelay = getBody().walk([&](SimSuspendDelayOp operation) {
      operation.emitOpError("is not permitted in an observed-region code unit");
      return WalkResult::interrupt();
    });
    if (noDelay.wasInterrupted())
      return failure();
  }
  if (getCodeUnitIdAttr() &&
      failed(verifyNonnegative(*this, getCodeUnitIdAttr(), "code-unit ID")))
    return failure();
  if (type.getNumInputs() == 0 || !isa<ContextType>(type.getInput(0)))
    return emitOpError("first argument must be !obelisk_sim.context");
  for (Type input : type.getInputs()) {
    if (!isa<ContextType, RefType, ArgumentRefType, NetType, DriverType,
             EventType, ProcessType, ManagedRefType, IntegerType, LogicType,
             TimeType, CovergroupHandleType>(input) &&
        !isManagedHandleType(input) && !isa<FloatType>(input) &&
        !isAggregateType(input))
      return emitOpError() << "contains non-normalized argument type " << input;
    if (auto integer = dyn_cast<IntegerType>(input);
        integer && !integer.isSignless())
      return emitOpError("builtin integer arguments must be signless");
  }
  for (Type result : type.getResults()) {
    if (!isa<IntegerType, LogicType, TimeType, EventType, ProcessType,
             ManagedRefType, ArgumentRefType, CovergroupHandleType>(result) &&
        !isManagedHandleType(result) && !isa<FloatType>(result) &&
        !isAggregateType(result))
      return emitOpError() << "contains non-normalized result type " << result;
    if (auto integer = dyn_cast<IntegerType>(result);
        integer && !integer.isSignless())
      return emitOpError("builtin integer results must be signless");
  }
  if (failed(verifyUnitBindings(*this)))
    return failure();

  bool zeroTimeResultEntry = getEntryKind() == EntryKind::Function ||
                             getEntryKind() == EntryKind::Observer;
  if (!zeroTimeResultEntry && !type.getResults().empty())
    return emitOpError("process and root entries must not return values");
  if (getEntryKind() == EntryKind::RootInitializer && type.getNumInputs() != 1)
    return emitOpError("root initializer accepts only the context argument");
  if (getEntryKind() == EntryKind::Observer) {
    if (type.getNumResults() != 1 ||
        !isa<IntegerType, LogicType, FloatType>(type.getResult(0)))
      return emitOpError("observer entry must return one scalar result");
  }
  if (getEntryKind() == EntryKind::Function ||
      getEntryKind() == EntryKind::Observer) {
    // Only the time-controlled statements are illegal in a SystemVerilog
    // function. Nonblocking assignment, nonblocking event trigger, and
    // `fork ... join_none` are all legal there and consume no simulation
    // time, so they stay representable and are handled by the schedule.
    WalkResult blocking = getBody().walk([&](Operation *op) {
      if (isa<SimSuspendDelayOp, SimSuspendChangeOp, SimSuspendEdgeOp,
              SimSuspendEdgeIffOp, SimSuspendLevelOp, SimSuspendAnyOp,
              SimSuspendEventOp, SimSuspendObserveOp, SimSuspendForeverOp,
              SimSuspendAwaitOp, SimSuspendJoinOp, SimSuspendChildrenOp>(op)) {
        op->emitOpError(getEntryKind() == EntryKind::Function
                            ? "is not permitted in a zero-time function entry"
                            : "is not permitted in a zero-time observer entry");
        return WalkResult::interrupt();
      }
      if (getEntryKind() == EntryKind::Observer && isa<SimTaskCallOp>(op)) {
        op->emitOpError("task calls are not permitted in an observer entry");
        return WalkResult::interrupt();
      }
      return WalkResult::advance();
    });
    if (blocking.wasInterrupted())
      return failure();
  }

  ArrayAttr argAttrs = getArgAttrsAttr();
  if (!argAttrs || argAttrs.size() != type.getNumInputs())
    return emitOpError(
        "requires one argument metadata dictionary per argument");
  for (auto [index, attr] : llvm::enumerate(argAttrs)) {
    auto dictionary = dyn_cast<DictionaryAttr>(attr);
    std::optional<CaptureKind> kind = getCaptureKind(dictionary);
    if (!kind)
      return emitOpError() << "argument #" << index
                           << " requires obelisk_sim.capture_kind metadata";
    if (index == 0 && *kind != CaptureKind::Context)
      return emitOpError("argument #0 must have context capture metadata");
    if (index != 0 && *kind == CaptureKind::Context)
      return emitOpError() << "argument #" << index
                           << " cannot have context capture metadata";
    bool needsDescriptor =
        *kind == CaptureKind::Storage || *kind == CaptureKind::Net ||
        *kind == CaptureKind::Driver || *kind == CaptureKind::Event;
    auto descriptor = dictionary.getAs<IntegerAttr>(metadata::descriptorId);
    if (needsDescriptor && !descriptor)
      return emitOpError() << "argument #" << index
                           << " requires obelisk_sim.descriptor_id metadata";
    if (!needsDescriptor && descriptor)
      return emitOpError() << "argument #" << index
                           << " must not have descriptor metadata";
    if (descriptor && descriptor.getValue().isNegative())
      return emitOpError() << "argument #" << index
                           << " has a negative descriptor ID";
    if (descriptor && descriptor.getValue().getBitWidth() > 64)
      return emitOpError() << "argument #" << index
                           << " has a descriptor ID wider than 64 bits";
  }
  return success();
}

LogicalResult SimReturnOp::verify() {
  auto function = (*this)->getParentOfType<SimFuncOp>();
  if (!function)
    return emitOpError("must be nested in obelisk_sim.func");
  if (getOperandTypes() != function.getFunctionType().getResults())
    return emitOpError(
        "operand types must match the enclosing function results");
  return success();
}

LogicalResult SimCallOp::verify() {
  Operation *parent = getOperation()->getParentOp();
  if (!getOperation()->getParentOfType<SimFuncOp>() &&
      (!parent || (parent->getName().getStringRef() != "func.func" &&
                   parent->getName().getStringRef() != "llvm.func")))
    return emitOpError("must be nested in obelisk_sim.func");
  return success();
}

LogicalResult SimCallOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  auto callee = symbolTable.lookupNearestSymbolFrom<SimFuncOp>(getOperation(),
                                                               getCalleeAttr());
  if (!callee || callee.getEntryKind() != EntryKind::Function)
    return emitOpError("callee must name a sibling function entry");
  if (getOperandTypes() != callee.getFunctionType().getInputs() ||
      getResultTypes() != callee.getFunctionType().getResults())
    return emitOpError("operand and result types must match callee signature");
  return success();
}

Operation::operand_range SimObserverBindOp::getCaptures() {
  size_t count = getCaptureCountAttr().getValue().isNegative()
                     ? 0
                     : std::min<uint64_t>(getCaptureCount(), getNumOperands());
  return getValues().take_front(count);
}

Operation::operand_range SimObserverBindOp::getDependencies() {
  size_t count = getCaptureCountAttr().getValue().isNegative()
                     ? 0
                     : std::min<uint64_t>(getCaptureCount(), getNumOperands());
  return getValues().drop_front(count);
}

LogicalResult SimObserverBindOp::verify() {
  if (getCaptureCountAttr().getValue().isNegative() ||
      static_cast<uint64_t>(getCaptureCount()) > getNumOperands())
    return emitOpError("capture count exceeds the operand inventory");
  for (Value capture : getCaptures()) {
    Type type = capture.getType();
    Type element;
    if (auto reference = dyn_cast<RefType>(type))
      element = reference.getElementType();
    else if (auto net = dyn_cast<NetType>(type))
      element = net.getElementType();
    else if (auto driver = dyn_cast<DriverType>(type))
      element = driver.getElementType();
    else if (isa<EventType>(type))
      continue;
    else
      return emitOpError(
          "captures must use storage, net, driver, or named-event handles");
    if (!isa<FloatType>(element) && !getPackedWidth(element))
      return emitOpError(
          "captured handles must refer to packed or floating values");
  }
  for (Value dependency : getDependencies())
    if (!isa<RefType, NetType, EventType>(dependency.getType()))
      return emitOpError(
          "dependencies must be storage, net, or named-event handles");
  if ((*this)->hasAttr("obelisk_sim.event_primary")) {
    auto observer = cast<ObserverType>(getResult().getType());
    auto integer = dyn_cast<IntegerType>(observer.getResultType());
    if (!integer || integer.getWidth() != 1 ||
        llvm::none_of(getDependencies(), [](Value dependency) {
          return isa<EventType>(dependency.getType());
        }))
      return emitOpError(
          "event-primary bindings must return i1 and depend on an event");
  }
  return success();
}

LogicalResult
SimObserverBindOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  auto evaluator = symbolTable.lookupNearestSymbolFrom<SimFuncOp>(
      getOperation(), getEvaluatorAttr());
  if (!evaluator || evaluator.getEntryKind() != EntryKind::Observer)
    return emitOpError("evaluator must name a sibling observer entry");
  FunctionType type = evaluator.getFunctionType();
  if (type.getNumInputs() == 0 || !isa<ContextType>(type.getInput(0)))
    return emitOpError("observer evaluator is missing its context argument");
  if (getCaptures().getTypes() != type.getInputs().drop_front())
    return emitOpError(
        "capture types must match evaluator arguments after context");
  if (type.getNumResults() != 1 ||
      type.getResult(0) != getResult().getType().getResultType())
    return emitOpError("result type must match the evaluator result");
  return success();
}

Operation::operand_range SimTaskCallOp::getArguments() {
  size_t count = getArgumentCountAttr().getValue().isNegative()
                     ? 0
                     : std::min<uint64_t>(getArgumentCount(), getNumOperands());
  return getValues().take_front(count);
}

Operation::operand_range SimTaskCallOp::getContinuationOperands() {
  size_t count = getArgumentCountAttr().getValue().isNegative()
                     ? 0
                     : std::min<uint64_t>(getArgumentCount(), getNumOperands());
  return getValues().drop_front(count);
}

MutableOperandRange SimTaskCallOp::getContinuationOperandsMutable() {
  unsigned count =
      getArgumentCountAttr().getValue().isNegative()
          ? 0
          : std::min<uint64_t>(getArgumentCount(), getNumOperands());
  return MutableOperandRange(getOperation(), count, getNumOperands() - count);
}

LogicalResult SimTaskCallOp::verify() {
  auto function = getOperation()->getParentOfType<SimFuncOp>();
  if (!function)
    return emitOpError("must be nested in obelisk_sim.func");
  if (function.getEntryKind() == EntryKind::Function)
    return emitOpError("is not permitted in a zero-time function entry");
  if (getArgumentCountAttr().getValue().isNegative() ||
      static_cast<uint64_t>(getArgumentCount()) > getNumOperands())
    return emitOpError("argument count exceeds the operand inventory");
  return verifyContinuation(*this, getContinuationOperands(),
                            getContinuation());
}

LogicalResult
SimTaskCallOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  auto callee = symbolTable.lookupNearestSymbolFrom<SimFuncOp>(getOperation(),
                                                               getCalleeAttr());
  if (!callee || callee.getEntryKind() != EntryKind::Task)
    return emitOpError("callee must name a sibling task entry");
  if (getArguments().getTypes() != callee.getFunctionType().getInputs())
    return emitOpError("argument types must match the task signature");
  if (!callee.getFunctionType().getResults().empty())
    return emitOpError("task entry must not return SSA results");
  return success();
}

void SimDPICallOp::getEffects(
    SmallVectorImpl<MemoryEffects::EffectInstance> &effects) {
  if (getIsPure())
    return;
  effects.emplace_back(MemoryEffects::Read::get(), ExternalResource::get());
  effects.emplace_back(MemoryEffects::Write::get(), ExternalResource::get());
  effects.emplace_back(MemoryEffects::Read::get(), SchedulerResource::get());
}

LogicalResult SimDPICallOp::verify() {
  if (!getOperation()->getParentOfType<SimFuncOp>())
    return emitOpError("must be nested in obelisk_sim.func");
  if (getImportId() == 0)
    return emitOpError("import ID must be nonzero");
  if (getCIdentifier().empty())
    return emitOpError("C identifier must not be empty");
  if (getIsPure() && (getIsContext() || getIsTask()))
    return emitOpError("pure DPI imports cannot be context imports or tasks");
  if (getNumResults() == 0 ||
      !isa<runtime::StatusType>(getResults().back().getType()))
    return emitOpError("must return a trailing runtime status");
  auto physicalCount = getOperation()->getAttrOfType<IntegerAttr>(
      "obelisk.dpi.logical_operand_count");
  uint64_t logicalInputs = physicalCount
                               ? physicalCount.getValue().getZExtValue()
                               : getArguments().size();
  if (logicalInputs > getAbiSignature().size())
    return emitOpError("logical operand count exceeds the ABI signature");

  SmallVector<DPIABIAttr> signature;
  signature.reserve(getAbiSignature().size());
  for (Attribute attr : getAbiSignature()) {
    auto abi = dyn_cast<DPIABIAttr>(attr);
    if (!abi)
      return emitOpError("ABI signature entries must be DPI ABI attributes");
    signature.push_back(abi);
  }

  uint64_t outputCursor = logicalInputs;
  if (!getIsTask() && outputCursor < signature.size() &&
      signature[outputCursor].getDirection() == DPIArgumentDirection::Result)
    ++outputCursor;
  for (uint64_t index = 0; index != logicalInputs; ++index) {
    DPIABIAttr input = signature[index];
    if (input.getDirection() == DPIArgumentDirection::Result)
      return emitOpError("a DPI formal cannot have result direction");
    if (input.getDirection() == DPIArgumentDirection::Input)
      continue;
    if (outputCursor >= signature.size())
      return emitOpError("DPI signature is missing a formal copy-out");
    DPIABIAttr output = signature[outputCursor++];
    if (output.getDirection() != DPIArgumentDirection::Output ||
        output.getKind() != input.getKind() ||
        output.getWidth() != input.getWidth() ||
        output.getFourState() != input.getFourState() ||
        output.getIsSigned() != input.getIsSigned())
      return emitOpError("DPI formal copy-out must match its input ABI entry");
  }
  if (!getIsTask() &&
      llvm::any_of(ArrayRef<DPIABIAttr>(signature).drop_front(outputCursor),
                   [](DPIABIAttr abi) {
                     return abi.getDirection() == DPIArgumentDirection::Result;
                   }))
    return emitOpError("a DPI function signature must place its result first");
  if (outputCursor != signature.size())
    return emitOpError("DPI signature has excess result entries");

  auto verifyLogicalType = [&](Type type, DPIABIAttr abi) -> LogicalResult {
    std::optional<unsigned> width = getPackedWidth(type);
    bool fourState = isa<LogicType>(getPackedScalarType(type));
    if (!width || *width != abi.getWidth() || fourState != abi.getFourState())
      return emitOpError(
          "logical operand or result type disagrees with its DPI ABI entry");
    return success();
  };
  if (!physicalCount) {
    if (!isa<runtime::ContextType, ContextType>(getRuntimeContext().getType()))
      return emitOpError(
          "runtime context must have simulation or runtime context type");
    uint64_t logicalOutputs = signature.size() - logicalInputs;
    if (getArguments().size() != logicalInputs ||
        getNumResults() - 1 != logicalOutputs)
      return emitOpError(
          "ABI signature must describe every data operand and result");
    for (auto [value, abi] : llvm::zip_equal(
             getArguments(),
             ArrayRef<DPIABIAttr>(signature).take_front(logicalInputs)))
      if (failed(verifyLogicalType(value.getType(), abi)))
        return failure();
    for (auto [value, abi] : llvm::zip_equal(
             getResults().drop_back(),
             ArrayRef<DPIABIAttr>(signature).drop_front(logicalInputs)))
      if (failed(verifyLogicalType(value.getType(), abi)))
        return failure();
    return success();
  }

  auto verifyPhysicalTypes = [&](TypeRange types, ArrayRef<DPIABIAttr> entries,
                                 StringRef role) -> LogicalResult {
    size_t physical = 0;
    for (DPIABIAttr abi : entries) {
      unsigned planes = abi.getFourState() ? 2 : 1;
      if (physical + planes > types.size())
        return emitOpError()
               << "is missing a physical DPI " << role << " plane";
      for (unsigned plane = 0; plane != planes; ++plane) {
        auto integer = dyn_cast<IntegerType>(types[physical++]);
        if (!integer || integer.getWidth() != abi.getWidth())
          return emitOpError()
                 << "has a malformed physical DPI " << role << " plane";
      }
    }
    if (physical != types.size())
      return emitOpError() << "has excess physical DPI " << role << " planes";
    return success();
  };
  if (failed(verifyPhysicalTypes(
          getArguments().getTypes(),
          ArrayRef<DPIABIAttr>(signature).take_front(logicalInputs), "input")))
    return failure();
  return verifyPhysicalTypes(
      getResults().drop_back().getTypes(),
      ArrayRef<DPIABIAttr>(signature).drop_front(logicalInputs), "result");
}

LogicalResult SimSpawnOp::verify() {
  if (!getOperation()->getParentOfType<SimFuncOp>())
    return emitOpError("must be nested in obelisk_sim.func");
  return success();
}

LogicalResult SimSpawnOp::verifySymbolUses(SymbolTableCollection &symbolTable) {
  auto callee = symbolTable.lookupNearestSymbolFrom<SimFuncOp>(getOperation(),
                                                               getCalleeAttr());
  if (!callee || callee.getEntryKind() == EntryKind::Function ||
      callee.getEntryKind() == EntryKind::Observer ||
      callee.getEntryKind() == EntryKind::Task ||
      callee.getEntryKind() == EntryKind::RootInitializer)
    return emitOpError("callee must name a sibling process entry");
  if (getOperandTypes() != callee.getFunctionType().getInputs() ||
      !callee.getFunctionType().getResults().empty())
    return emitOpError("operands must match the void callee signature");
  return success();
}

LogicalResult SimControlEnterOp::verify() {
  return verifyPositive(*this, getTargetIdAttr(), "control target ID");
}

LogicalResult SimControlDisableOp::verify() {
  if (failed(verifyPositive(*this, getTargetIdAttr(), "control target ID")))
    return failure();
  if (getActivation() &&
      getActivation().getType() != ControlType::get(getContext()))
    return emitOpError("activation must be a control token");
  if (getActivation() && getHierarchical())
    return emitOpError(
        "hierarchical disable must not name one activation token");
  return success();
}

LogicalResult SimStaticOnceOp::verify() {
  return verifyPositive(*this, getIdAttr(), "static initialization ID");
}

LogicalResult SimDeferredOnceOp::verify() {
  return verifyPositive(*this, getIdAttr(), "deferred assertion site ID");
}

LogicalResult SimSampledReadOp::verify() {
  Type element;
  if (auto ref = dyn_cast<RefType>(getSource().getType()))
    element = ref.getElementType();
  else if (auto net = dyn_cast<NetType>(getSource().getType()))
    element = net.getElementType();
  else
    return emitOpError("source must be a storage or net reference");
  if (element != getResult().getType() || !getPackedWidth(element))
    return emitOpError("source element and result must have one packed type");
  return success();
}

LogicalResult SimSampledHistoryOp::verify() {
  if (getCurrent().getType() != getResult().getType() ||
      !getPackedWidth(getCurrent().getType()))
    return emitOpError("current and result must have one packed type");
  if (failed(verifyPositive(*this, getIdAttr(), "sample history site ID")) ||
      failed(verifyPositive(*this, getDepthAttr(), "sample history depth")))
    return failure();
  return success();
}

LogicalResult SimClockedSampleUpdateOp::verify() {
  if (!getPackedWidth(getCurrent().getType()))
    return emitOpError("current value must have one packed type");
  if (failed(verifyPositive(*this, getIdAttr(), "clocked sample site ID")) ||
      failed(verifyPositive(*this, getDepthAttr(),
                            "clocked sample history depth")))
    return failure();
  return success();
}

LogicalResult SimClockedSampleReadOp::verify() {
  if (!getPackedWidth(getResult().getType()))
    return emitOpError("result must have one packed type");
  if (failed(verifyPositive(*this, getIdAttr(), "clocked sample site ID")) ||
      failed(verifyPositive(*this, getDepthAttr(),
                            "clocked sample history depth")))
    return failure();
  if (getAge() > getDepth())
    return emitOpError("sample age must not exceed history depth");
  return success();
}

LogicalResult SimDeferredEnqueueOp::verify() {
  if (failed(verifyPositive(*this, getIdAttr(), "deferred assertion site ID")))
    return failure();
  if (auto assertionID = getOperation()->getAttrOfType<IntegerAttr>(
          "obelisk_sim.assertion_control_target_id"))
    return verifyPositive(*this, assertionID,
                          "deferred assertion control target ID");
  return success();
}

LogicalResult SimDeferredMatureOp::verify() {
  if (!getTicket().getType().isInteger(64))
    return emitOpError("ticket must be an i64");
  return success();
}

LogicalResult SimAssertionControlOp::verify() {
  if (getAction() < 1 || getAction() > 11)
    return emitOpError("action must be in the range 1 through 11");
  return verifyPositive(*this, getAssertionIdAttr(),
                        "assertion control target ID");
}

LogicalResult SimAssertionEnabledOp::verify() {
  return verifyPositive(*this, getAssertionIdAttr(),
                        "assertion control target ID");
}

LogicalResult SimAssertionActionStateOp::verify() {
  return verifyPositive(*this, getAssertionIdAttr(),
                        "assertion control target ID");
}

LogicalResult SimContextStorageOp::verify() {
  return verifyNonnegative(*this, getIdAttr(), "storage ID");
}
LogicalResult SimContextNetOp::verify() {
  return verifyNonnegative(*this, getIdAttr(), "net ID");
}
LogicalResult SimContextDriverOp::verify() {
  return verifyNonnegative(*this, getIdAttr(), "driver ID");
}
LogicalResult SimContextEventOp::verify() {
  return verifyNonnegative(*this, getIdAttr(), "event ID");
}

static bool isUnionAggregate(Type type) {
  return isa<PackedUnionType, UnpackedUnionType>(type);
}

static bool isStructOrArrayAggregate(Type type) {
  return isAggregateType(type) && !isUnionAggregate(type);
}

LogicalResult SimPackedFlattenOp::verify() {
  if (!isAggregateType(getInput().getType()) ||
      !getPackedScalarType(getInput().getType()))
    return emitOpError("input must be a packed aggregate");
  if (getResult().getType() != getPackedScalarType(getInput().getType()))
    return emitOpError(
        "result must be the aggregate's width- and state-matched scalar");
  return success();
}

LogicalResult SimPackedUnflattenOp::verify() {
  if (!isAggregateType(getResult().getType()) ||
      !getPackedScalarType(getResult().getType()))
    return emitOpError("result must be a packed aggregate");
  if (getInput().getType() != getPackedScalarType(getResult().getType()))
    return emitOpError(
        "input must be the aggregate's width- and state-matched scalar");
  return success();
}

static LogicalResult verifyAggregateIndex(Operation *operation, Type aggregate,
                                          IntegerAttr index, Type result,
                                          bool requireUnion) {
  if (!isAggregateType(aggregate) ||
      requireUnion != isUnionAggregate(aggregate))
    return operation->emitOpError()
           << (requireUnion ? "input must be a union"
                            : "input must be a struct or fixed array");
  if (index.getValue().isNegative() || index.getValue().getActiveBits() > 32)
    return operation->emitOpError("aggregate index must be nonnegative");
  uint64_t ordinal = index.getValue().getZExtValue();
  Type expected = ordinal <= std::numeric_limits<unsigned>::max()
                      ? getAggregateElementType(aggregate, ordinal)
                      : Type{};
  if (!expected)
    return operation->emitOpError("aggregate index is out of range");
  if (result != expected)
    return operation->emitOpError()
           << "result type must match aggregate element type " << expected;
  return success();
}

LogicalResult SimAggregateDefaultOp::verify() {
  if (!isAggregateType(getResult().getType()))
    return emitOpError("result must be a fixed aggregate type");
  return success();
}

LogicalResult SimAggregateConstructOp::verify() {
  Type type = getResult().getType();
  if (!isStructOrArrayAggregate(type))
    return emitOpError("result must be a struct or fixed array");
  if (getElements().size() != getAggregateNumElements(type))
    return emitOpError("requires one operand per aggregate element");
  for (auto [index, element] : llvm::enumerate(getElements()))
    if (element.getType() != getAggregateElementType(type, index))
      return emitOpError() << "operand #" << index
                           << " does not match its aggregate element type";
  return success();
}

LogicalResult SimAggregateExtractOp::verify() {
  return verifyAggregateIndex(*this, getInput().getType(), getIndexAttr(),
                              getResult().getType(), false);
}

LogicalResult SimAggregateInsertOp::verify() {
  if (getInput().getType() != getResult().getType())
    return emitOpError("input and result aggregate types must match");
  return verifyAggregateIndex(*this, getInput().getType(), getIndexAttr(),
                              getReplacement().getType(), false);
}

LogicalResult SimArrayDynExtractOp::verify() {
  Type type = getInput().getType();
  if (!isa<PackedArrayType, UnpackedArrayType>(type))
    return emitOpError("input must be a fixed array");
  if (failed(verifyNormalizedIndex(*this, getIndex().getType())))
    return failure();
  if (getResult().getType() != getAggregateElementType(type, 0))
    return emitOpError("result must match the array element type");
  return success();
}

LogicalResult SimUnionConstructOp::verify() {
  return verifyAggregateIndex(*this, getResult().getType(), getIndexAttr(),
                              getValue().getType(), true);
}

LogicalResult SimUnionExtractOp::verify() {
  return verifyAggregateIndex(*this, getInput().getType(), getIndexAttr(),
                              getResult().getType(), true);
}

LogicalResult SimUnionIsActiveOp::verify() {
  Type type = getInput().getType();
  bool tagged = false;
  if (auto packed = dyn_cast<PackedUnionType>(type))
    tagged = packed.getIsTagged();
  else if (auto unpacked = dyn_cast<UnpackedUnionType>(type))
    tagged = unpacked.getIsTagged();
  else
    return emitOpError("input must be a tagged union");
  if (!tagged)
    return emitOpError("input union must be tagged");
  if (getIndexAttr().getValue().isNegative() ||
      getIndex() >= getAggregateNumElements(type))
    return emitOpError("tagged union member index is out of range");
  return success();
}

OpFoldResult SimUnionIsActiveOp::fold(FoldAdaptor) {
  if (auto packed = dyn_cast<PackedUnionType>(getInput().getType());
      packed && packed.getTagBits() == 0)
    return IntegerAttr::get(getResult().getType(), true);
  if (auto construct = getInput().getDefiningOp<SimUnionConstructOp>())
    return IntegerAttr::get(getResult().getType(),
                            construct.getIndex() == getIndex());
  return {};
}

static LogicalResult verifySubelementPath(Operation *operation, Type input,
                                          ArrayRef<int64_t> indices,
                                          Type result) {
  if (indices.empty())
    return operation->emitOpError("subelement path must not be empty");
  Type current = input;
  for (int64_t index : indices) {
    if (index < 0 ||
        static_cast<uint64_t>(index) > std::numeric_limits<unsigned>::max())
      return operation->emitOpError("subelement index must be nonnegative");
    current = getAggregateElementType(current, static_cast<unsigned>(index));
    if (!current)
      return operation->emitOpError("subelement path is out of range");
  }
  if (current != result)
    return operation->emitOpError()
           << "result element type must match selected subelement " << current;
  return success();
}

LogicalResult SimRefSubelementOp::verify() {
  return verifySubelementPath(*this, getInput().getType().getElementType(),
                              getIndices(),
                              getResult().getType().getElementType());
}

LogicalResult SimDriverSubelementOp::verify() {
  return verifySubelementPath(*this, getInput().getType().getElementType(),
                              getIndices(),
                              getResult().getType().getElementType());
}

static LogicalResult verifyArrayElementView(Operation *operation, Type input,
                                            Type index, Type result) {
  if (!isa<PackedArrayType, UnpackedArrayType>(input))
    return operation->emitOpError("input element must be a fixed array");
  if (failed(verifyNormalizedIndex(operation, index)))
    return failure();
  if (result != getAggregateElementType(input, 0))
    return operation->emitOpError("result must match the array element type");
  return success();
}

LogicalResult SimRefArrayElementOp::verify() {
  return verifyArrayElementView(*this, getInput().getType().getElementType(),
                                getIndex().getType(),
                                getResult().getType().getElementType());
}

LogicalResult SimDriverArrayElementOp::verify() {
  return verifyArrayElementView(*this, getInput().getType().getElementType(),
                                getIndex().getType(),
                                getResult().getType().getElementType());
}

LogicalResult SimRefAllocOp::verify() {
  if (getInitialValue().getType() != getResult().getType().getElementType())
    return emitOpError("initial value must match allocated element type");
  return success();
}

static bool isReenteredAllocation(SimRefAllocOp allocation) {
  Block *block = allocation->getBlock();
  return llvm::any_of(block->getPredecessors(), [&](Block *predecessor) {
    return block->isReachable(predecessor);
  });
}

static bool isResetBeforeFirstUse(SimRefAllocOp allocation) {
  Value reference = allocation.getResult();
  for (Operation *operation = allocation->getNextNode(); operation;
       operation = operation->getNextNode()) {
    bool usesReference =
        llvm::any_of(reference.getUsers(), [&](Operation *user) {
          return user == operation || operation->isProperAncestor(user);
        });
    if (!usesReference)
      continue;
    auto store = dyn_cast<SimRefStoreOp>(operation);
    return store && store.getReference() == reference;
  }
  return false;
}

SmallVector<MemorySlot> SimRefAllocOp::getPromotableSlots() {
  // The initial value belongs to each execution of ref.alloc, not merely to
  // the first entry into its function. Generic mem2reg models an allocator as
  // one definition. A cyclic allocation is therefore only safe to promote
  // when an explicit store resets it before its first use on every reentry.
  if (isReenteredAllocation(*this) && !isResetBeforeFirstUse(*this))
    return {};
  return {{getResult(), getResult().getType().getElementType()}};
}

static std::optional<unsigned> getUnionSelectedInitializer(Value value) {
  if (auto construct = value.getDefiningOp<SimUnionConstructOp>())
    return static_cast<unsigned>(construct.getIndex());
  if (auto defaultValue = value.getDefiningOp<SimAggregateDefaultOp>()) {
    Type type = defaultValue.getResult().getType();
    if (auto packed = dyn_cast<PackedUnionType>(type);
        packed && packed.getIsTagged() && !containsFourStateLeaf(type))
      return 0;
    if (auto unpacked = dyn_cast<UnpackedUnionType>(type);
        unpacked && !unpacked.getIsTagged())
      return 0;
  }
  return std::nullopt;
}

SmallVector<DestructurableMemorySlot> SimRefAllocOp::getDestructurableSlots() {
  if (isReenteredAllocation(*this) && !isResetBeforeFirstUse(*this))
    return {};
  Type elementType = getResult().getType().getElementType();
  auto destructurable = dyn_cast<DestructurableTypeInterface>(elementType);
  if (!destructurable)
    return {};
  std::optional<DenseMap<Attribute, Type>> elements =
      destructurable.getSubelementIndexMap();
  if (!elements || elements->empty())
    return {};

  // A union can only lose its shared backing when every view and its
  // initializer agree on one active field. Whole accesses and mixed views
  // deliberately retain the allocation.
  if (isUnionAggregate(elementType)) {
    std::optional<unsigned> selected =
        getUnionSelectedInitializer(getInitialValue());
    if (!selected)
      return {};
    for (OpOperand &use : getResult().getUses()) {
      auto view = dyn_cast<SimRefSubelementOp>(use.getOwner());
      if (!view || use.get() != view.getInput() || view.getIndices().empty() ||
          static_cast<unsigned>(view.getIndices()[0]) != *selected)
        return {};
    }
  }
  return {DestructurableMemorySlot{{getResult(), elementType}, *elements}};
}

Value materializeDefaultValue(OpBuilder &builder, Location location,
                                     Type type) {
  if (isAggregateType(type))
    return SimAggregateDefaultOp::create(builder, location, type);
  if (auto integer = dyn_cast<IntegerType>(type))
    return arith::ConstantOp::create(builder, location, integer,
                                     builder.getIntegerAttr(integer, 0));
  if (isa<FloatType>(type))
    return arith::ConstantOp::create(builder, location, type,
                                     builder.getFloatAttr(type, 0.0));
  if (auto logic = dyn_cast<LogicType>(type)) {
    auto plane = IntegerType::get(type.getContext(), logic.getWidth());
    return SimLogicConstantOp::create(
        builder, location, logic,
        builder.getIntegerAttr(plane, APInt::getZero(logic.getWidth())),
        builder.getIntegerAttr(plane, APInt::getAllOnes(logic.getWidth())));
  }
  if (isa<TimeType>(type))
    return SimTimeConstantOp::create(builder, location, type,
                                     builder.getI64IntegerAttr(0));
  return {};
}

DenseMap<Attribute, MemorySlot> SimRefAllocOp::destructure(
    const DestructurableMemorySlot &slot,
    const llvm::SmallPtrSetImpl<Attribute> &usedIndices, OpBuilder &builder,
    SmallVectorImpl<DestructurableAllocationOpInterface> &newAllocators) {
  assert(slot.ptr == getResult());
  builder.setInsertionPointAfter(*this);
  SmallVector<Attribute> sorted(usedIndices.begin(), usedIndices.end());
  llvm::sort(sorted, [](Attribute lhs, Attribute rhs) {
    return cast<IntegerAttr>(lhs).getInt() < cast<IntegerAttr>(rhs).getInt();
  });

  DenseMap<Attribute, MemorySlot> subslots;
  for (Attribute attribute : sorted) {
    unsigned index = cast<IntegerAttr>(attribute).getInt();
    Type type = slot.subelementTypes.lookup(attribute);
    Value initial;
    if (auto construct =
            getInitialValue().getDefiningOp<SimAggregateConstructOp>())
      initial = construct.getElements()[index];
    else if (auto construct =
                 getInitialValue().getDefiningOp<SimUnionConstructOp>();
             construct && construct.getIndex() == index)
      initial = construct.getValue();
    else if (isUnionAggregate(slot.elemType))
      initial = SimUnionExtractOp::create(builder, getLoc(), type,
                                          getInitialValue(), index);
    else
      initial = SimAggregateExtractOp::create(builder, getLoc(), type,
                                              getInitialValue(), index);
    auto allocation = SimRefAllocOp::create(
        builder, getLoc(), RefType::get(getContext(), type), initial);
    newAllocators.push_back(allocation);
    subslots.try_emplace(attribute, MemorySlot{allocation.getResult(), type});
  }
  return subslots;
}

std::optional<DestructurableAllocationOpInterface>
SimRefAllocOp::handleDestructuringComplete(const DestructurableMemorySlot &slot,
                                           OpBuilder &) {
  assert(slot.ptr == getResult());
  getOperation()->erase();
  return std::nullopt;
}

Value SimRefAllocOp::getDefaultValue(const MemorySlot &, OpBuilder &) {
  return getInitialValue();
}

void SimRefAllocOp::handleBlockArgument(const MemorySlot &, BlockArgument,
                                        OpBuilder &) {}

std::optional<PromotableAllocationOpInterface>
SimRefAllocOp::handlePromotionComplete(const MemorySlot &, Value, OpBuilder &) {
  getOperation()->erase();
  return std::nullopt;
}

bool SimRefLoadOp::loadsFrom(const MemorySlot &slot) {
  return getReference() == slot.ptr;
}
bool SimRefLoadOp::storesTo(const MemorySlot &) { return false; }
Value SimRefLoadOp::getStored(const MemorySlot &, OpBuilder &, Value,
                              const DataLayout &) {
  return {};
}
bool SimRefLoadOp::canUsesBeRemoved(
    const MemorySlot &slot,
    const llvm::SmallPtrSetImpl<OpOperand *> &blockingUses,
    SmallVectorImpl<OpOperand *> &, const DataLayout &) {
  return getReference() == slot.ptr &&
         llvm::all_of(blockingUses, [&](OpOperand *use) {
           return use == &getReferenceMutable();
         });
}
DeletionKind SimRefLoadOp::removeBlockingUses(
    const MemorySlot &, const llvm::SmallPtrSetImpl<OpOperand *> &, OpBuilder &,
    Value reachingDefinition, const DataLayout &) {
  getResult().replaceAllUsesWith(reachingDefinition);
  return DeletionKind::Delete;
}

bool SimRefStoreOp::loadsFrom(const MemorySlot &) { return false; }
bool SimRefStoreOp::storesTo(const MemorySlot &slot) {
  return getReference() == slot.ptr;
}
Value SimRefStoreOp::getStored(const MemorySlot &slot, OpBuilder &, Value,
                               const DataLayout &) {
  return storesTo(slot) ? getValue() : Value{};
}
bool SimRefStoreOp::canUsesBeRemoved(
    const MemorySlot &slot,
    const llvm::SmallPtrSetImpl<OpOperand *> &blockingUses,
    SmallVectorImpl<OpOperand *> &, const DataLayout &) {
  return getReference() == slot.ptr &&
         llvm::all_of(blockingUses, [&](OpOperand *use) {
           return use == &getReferenceMutable();
         });
}
DeletionKind
SimRefStoreOp::removeBlockingUses(const MemorySlot &,
                                  const llvm::SmallPtrSetImpl<OpOperand *> &,
                                  OpBuilder &, Value, const DataLayout &) {
  return DeletionKind::Delete;
}

static SmallVector<Attribute>
getSortedSubslotIndices(const DestructurableMemorySlot &slot) {
  SmallVector<Attribute> indices;
  indices.reserve(slot.subelementTypes.size());
  for (auto [index, type] : slot.subelementTypes)
    indices.push_back(index);
  llvm::sort(indices, [](Attribute lhs, Attribute rhs) {
    return cast<IntegerAttr>(lhs).getInt() < cast<IntegerAttr>(rhs).getInt();
  });
  return indices;
}

bool SimRefLoadOp::canRewire(const DestructurableMemorySlot &slot,
                             llvm::SmallPtrSetImpl<Attribute> &usedIndices,
                             SmallVectorImpl<MemorySlot> &,
                             const DataLayout &) {
  if (getReference() != slot.ptr || getResult().getType() != slot.elemType ||
      isUnionAggregate(slot.elemType))
    return false;
  if (isa<PackedArrayType, UnpackedArrayType>(slot.elemType) &&
      llvm::any_of(getResult().getUsers(), [](Operation *user) {
        return isa<SimArrayDynExtractOp>(user);
      }))
    return false;
  for (Attribute index : getSortedSubslotIndices(slot))
    usedIndices.insert(index);
  return true;
}

DeletionKind SimRefLoadOp::rewire(const DestructurableMemorySlot &slot,
                                  DenseMap<Attribute, MemorySlot> &subslots,
                                  OpBuilder &builder, const DataLayout &) {
  SmallVector<Value> elements;
  for (Attribute index : getSortedSubslotIndices(slot)) {
    MemorySlot subslot = subslots.at(index);
    elements.push_back(
        SimRefLoadOp::create(builder, getLoc(), subslot.elemType, subslot.ptr));
  }
  Value reconstructed = SimAggregateConstructOp::create(
      builder, getLoc(), slot.elemType, elements);
  getResult().replaceAllUsesWith(reconstructed);
  return DeletionKind::Delete;
}

LogicalResult SimRefLoadOp::ensureOnlySafeAccesses(
    const MemorySlot &slot, SmallVectorImpl<MemorySlot> &, const DataLayout &) {
  return success(getReference() != slot.ptr ||
                 getResult().getType() == slot.elemType);
}

bool SimRefStoreOp::canRewire(const DestructurableMemorySlot &slot,
                              llvm::SmallPtrSetImpl<Attribute> &usedIndices,
                              SmallVectorImpl<MemorySlot> &,
                              const DataLayout &) {
  if (getReference() != slot.ptr || getValue() == slot.ptr ||
      getValue().getType() != slot.elemType || isUnionAggregate(slot.elemType))
    return false;
  for (Attribute index : getSortedSubslotIndices(slot))
    usedIndices.insert(index);
  return true;
}

DeletionKind SimRefStoreOp::rewire(const DestructurableMemorySlot &slot,
                                   DenseMap<Attribute, MemorySlot> &subslots,
                                   OpBuilder &builder, const DataLayout &) {
  for (Attribute attribute : getSortedSubslotIndices(slot)) {
    unsigned index = cast<IntegerAttr>(attribute).getInt();
    MemorySlot subslot = subslots.at(attribute);
    Value element = SimAggregateExtractOp::create(
        builder, getLoc(), subslot.elemType, getValue(), index);
    SimRefStoreOp::create(builder, getLoc(), element, subslot.ptr);
  }
  return DeletionKind::Delete;
}

LogicalResult SimRefStoreOp::ensureOnlySafeAccesses(
    const MemorySlot &slot, SmallVectorImpl<MemorySlot> &, const DataLayout &) {
  return success(getReference() != slot.ptr ||
                 getValue().getType() == slot.elemType);
}

static Attribute getFirstSubelementIndex(DenseI64ArrayAttr indices,
                                         MLIRContext *context) {
  if (!indices || indices.empty() || indices[0] < 0 ||
      static_cast<uint64_t>(indices[0]) > std::numeric_limits<uint32_t>::max())
    return {};
  return getSubelementIndexAttr(context, static_cast<unsigned>(indices[0]));
}

bool SimRefSubelementOp::canRewire(
    const DestructurableMemorySlot &slot,
    llvm::SmallPtrSetImpl<Attribute> &usedIndices,
    SmallVectorImpl<MemorySlot> &mustBeSafelyUsed, const DataLayout &) {
  if (getInput() != slot.ptr)
    return false;
  Attribute index = getFirstSubelementIndex(getIndicesAttr(), getContext());
  if (!index || !slot.subelementTypes.contains(index))
    return false;
  usedIndices.insert(index);
  mustBeSafelyUsed.push_back(
      {getResult(), getResult().getType().getElementType()});
  return true;
}

DeletionKind
SimRefSubelementOp::rewire(const DestructurableMemorySlot &,
                           DenseMap<Attribute, MemorySlot> &subslots,
                           OpBuilder &builder, const DataLayout &) {
  Attribute index = getFirstSubelementIndex(getIndicesAttr(), getContext());
  MemorySlot subslot = subslots.at(index);
  Value replacement = subslot.ptr;
  ArrayRef<int64_t> path = getIndices();
  if (path.size() > 1) {
    auto remaining = builder.getDenseI64ArrayAttr(path.drop_front());
    replacement = SimRefSubelementOp::create(
        builder, getLoc(), getResult().getType(), subslot.ptr, remaining);
  }
  getResult().replaceAllUsesWith(replacement);
  return DeletionKind::Delete;
}

LogicalResult SimRefSubelementOp::ensureOnlySafeAccesses(
    const MemorySlot &slot, SmallVectorImpl<MemorySlot> &mustBeSafelyUsed,
    const DataLayout &) {
  if (getInput() != slot.ptr)
    return success();
  Type result = getResult().getType().getElementType();
  if (failed(verifySubelementPath(getOperation(), slot.elemType, getIndices(),
                                  result)))
    return failure();
  mustBeSafelyUsed.push_back({getResult(), result});
  return success();
}

LogicalResult SimRefExtractOp::ensureOnlySafeAccesses(
    const MemorySlot &slot, SmallVectorImpl<MemorySlot> &mustBeSafelyUsed,
    const DataLayout &) {
  if (getInput() != slot.ptr)
    return success();
  std::optional<unsigned> input = getPackedWidth(slot.elemType);
  Type resultType = getResult().getType().getElementType();
  std::optional<unsigned> result = getPackedWidth(resultType);
  if (!input || !result || getLowBit() > *input ||
      *result > *input - getLowBit())
    return failure();
  mustBeSafelyUsed.push_back({getResult(), resultType});
  return success();
}

LogicalResult SimRefDynExtractOp::ensureOnlySafeAccesses(
    const MemorySlot &slot, SmallVectorImpl<MemorySlot> &mustBeSafelyUsed,
    const DataLayout &) {
  if (getInput() != slot.ptr)
    return success();
  Type resultType = getResult().getType().getElementType();
  if (!getPackedWidth(slot.elemType) || !getPackedWidth(resultType))
    return failure();
  mustBeSafelyUsed.push_back({getResult(), resultType});
  return success();
}

LogicalResult SimRefArrayElementOp::ensureOnlySafeAccesses(
    const MemorySlot &slot, SmallVectorImpl<MemorySlot> &mustBeSafelyUsed,
    const DataLayout &) {
  if (getInput() != slot.ptr)
    return success();
  Type resultType = getResult().getType().getElementType();
  if (!isa<PackedArrayType, UnpackedArrayType>(slot.elemType) ||
      getAggregateElementType(slot.elemType, 0) != resultType)
    return failure();
  mustBeSafelyUsed.push_back({getResult(), resultType});
  return success();
}

LogicalResult SimRefExtractOp::verify() {
  Type inputType = getInput().getType().getElementType();
  Type resultType = getResult().getType().getElementType();
  if (failed(verifyMatchingStateDomain(*this, inputType, resultType)))
    return failure();
  auto input = getPackedWidth(inputType);
  auto result = getPackedWidth(resultType);
  if (!input || !result || getLowBitAttr().getValue().isNegative() ||
      getLowBitAttr().getValue().getZExtValue() + *result > *input)
    return emitOpError("constant selection is outside the input element width");
  return success();
}

LogicalResult SimOverrideOp::verify() {
  Type elementType;
  if (auto reference = dyn_cast<RefType>(getTarget().getType()))
    elementType = reference.getElementType();
  else if (auto net = dyn_cast<NetType>(getTarget().getType()))
    elementType = net.getElementType();
  else
    return emitOpError("target must be a static reference or built-in net");
  if (getIsAssign() && !isa<RefType>(getTarget().getType()))
    return emitOpError("procedural assign requires a variable reference");
  if (elementType != getValue().getType())
    return emitOpError("target element type must match the override value");
  if (!getPackedWidth(elementType))
    return emitOpError("requires a fixed-width packed value");
  return success();
}

LogicalResult SimReleaseOverrideOp::verify() {
  Type elementType;
  if (auto reference = dyn_cast<RefType>(getTarget().getType()))
    elementType = reference.getElementType();
  else if (auto net = dyn_cast<NetType>(getTarget().getType()))
    elementType = net.getElementType();
  else
    return emitOpError("target must be a static reference or built-in net");
  if (getIsAssign() && !isa<RefType>(getTarget().getType()))
    return emitOpError("procedural deassign requires a variable reference");
  if (!getPackedWidth(elementType))
    return emitOpError("requires a fixed-width packed value");
  return success();
}

LogicalResult SimNetExtractOp::verify() {
  Type inputType = getInput().getType().getElementType();
  Type resultType = getResult().getType().getElementType();
  if (failed(verifyMatchingStateDomain(*this, inputType, resultType)))
    return failure();
  auto input = getPackedWidth(inputType);
  auto result = getPackedWidth(resultType);
  if (!input || !result || getLowBitAttr().getValue().isNegative() ||
      getLowBitAttr().getValue().getZExtValue() + *result > *input)
    return emitOpError("constant selection is outside the input element width");
  return success();
}

LogicalResult SimRefDynExtractOp::verify() {
  Type inputType = getInput().getType().getElementType();
  Type resultType = getResult().getType().getElementType();
  if (failed(verifyNormalizedIndex(*this, getLowBit().getType())) ||
      failed(verifyMatchingStateDomain(*this, inputType, resultType)))
    return failure();
  auto input = getPackedWidth(inputType);
  auto result = getPackedWidth(resultType);
  if (!input || !result || *result > *input)
    return emitOpError("result element width exceeds input element width");
  return success();
}

LogicalResult SimDriverExtractOp::verify() {
  Type inputType = getInput().getType().getElementType();
  Type resultType = getResult().getType().getElementType();
  if (failed(verifyMatchingStateDomain(*this, inputType, resultType)))
    return failure();
  auto input = getPackedWidth(inputType);
  auto result = getPackedWidth(resultType);
  if (!input || !result || getLowBitAttr().getValue().isNegative() ||
      getLowBitAttr().getValue().getZExtValue() + *result > *input)
    return emitOpError("constant selection is outside the input element width");
  return success();
}

LogicalResult SimDriverDynExtractOp::verify() {
  Type inputType = getInput().getType().getElementType();
  Type resultType = getResult().getType().getElementType();
  if (failed(verifyNormalizedIndex(*this, getLowBit().getType())) ||
      failed(verifyMatchingStateDomain(*this, inputType, resultType)))
    return failure();
  auto input = getPackedWidth(inputType);
  auto result = getPackedWidth(resultType);
  if (!input || !result || *result > *input)
    return emitOpError("result element width exceeds input element width");
  return success();
}

} // namespace obelisk::sim
