//===- SimulationReferenceLifetimeLowering.cpp - Native ref ownership ---===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Analysis/Liveness.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Dominance.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/ADT/SetVector.h"

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMINSTRUMENTREFERENCELIFETIMESPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace detail {
namespace {

constexpr llvm::StringLiteral automaticOwnerReleaseInstrumentedAttr =
    "obelisk.owner_release_instrumented";

bool isReferenceView(Operation *operation) {
  return isa<sim::SimRefExtractOp, sim::SimRefDynExtractOp,
             sim::SimRefSubelementOp, sim::SimRefArrayElementOp>(operation);
}

llvm::SetVector<Value> collectReferenceFamily(Value root) {
  llvm::SetVector<Value> family;
  family.insert(root);
  for (size_t index = 0; index != family.size(); ++index) {
    Value reference = family[index];
    for (OpOperand &use : reference.getUses()) {
      Operation *user = use.getOwner();
      if (isReferenceView(user)) {
        for (Value result : user->getResults())
          if (isa<sim::RefType>(result.getType()))
            family.insert(result);
      }
      auto branch = dyn_cast<BranchOpInterface>(user);
      if (!branch)
        continue;
      for (unsigned successorIndex = 0, end = user->getNumSuccessors();
           successorIndex != end; ++successorIndex) {
        Block *successor = user->getSuccessor(successorIndex);
        SuccessorOperands successorOperands =
            branch.getSuccessorOperands(successorIndex);
        for (unsigned
                 argumentIndex = successorOperands.getProducedOperandCount(),
                 argumentEnd = successorOperands.size();
             argumentIndex != argumentEnd; ++argumentIndex)
          if (successorOperands[argumentIndex] == reference)
            family.insert(successor->getArgument(argumentIndex));
      }
    }
  }
  return family;
}

void insertAutomaticOwnerRelease(OpBuilder &builder, Location location,
                                 Value handle) {
  sim::SimRefReleaseOwnerOp::create(builder, location, handle);
}

void emitNativeStateRelease(OpBuilder &builder, Location location, Value handle,
                            bool ownerReference) {
  Type pointer = LLVM::LLVMPointerType::get(builder.getContext());
  Type i32 = builder.getI32Type();
  Value contextAddress = LLVM::AddressOfOp::create(builder, location, pointer,
                                                   "__obelisk_current_context");
  Value context =
      LLVM::LoadOp::create(builder, location, pointer, contextAddress, 8);
  Value owner = arith::ConstantOp::create(
      builder, location, i32,
      builder.getI32IntegerAttr(ownerReference ? 1 : 0));
  Value status = LLVM::CallOp::create(
                     builder, location, TypeRange{i32},
                     SymbolRefAttr::get(builder.getContext(),
                                        "obelisk_rt_v1_native_state_release"),
                     ValueRange{context, handle, owner})
                     .getResult();
  LLVM::CallOp::create(
      builder, location, TypeRange{},
      SymbolRefAttr::get(builder.getContext(), "obelisk_rt_v1_scheduler_fail"),
      ValueRange{context, status});
}

class RefAllocConversion final
    : public OpConversionPattern<sim::SimRefAllocOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimRefAllocOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getInitialValue().empty())
      return failure();
    std::optional<unsigned> width =
        nativeStateWidth(op.getInitialValue().getType());
    if (!width || *width == 0)
      return failure();
    Location location = op.getLoc();
    if (!op->hasAttr(automaticOwnerReleaseInstrumentedAttr))
      return op.emitError("automatic reference lifetime was not instrumented");
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i32 = rewriter.getI32Type();
    Type i64 = rewriter.getI64Type();
    auto savePlane = [&](Value value) {
      Value address = entryAlloca(rewriter, location, value.getType(), 1, 1);
      LLVM::StoreOp::create(rewriter, location, value, address, 1);
      return address;
    };
    Value initial = adaptor.getInitialValue().front();
    if (isa<FloatType>(op.getInitialValue().getType()))
      initial =
          arith::BitcastOp::create(rewriter, location,
                                   rewriter.getIntegerType(*nativeStateWidth(
                                       op.getInitialValue().getType())),
                                   initial);
    Value value = savePlane(initial);
    Value unknown = LLVM::ZeroOp::create(rewriter, location, pointer);
    if (adaptor.getInitialValue().size() == 2)
      unknown = savePlane(adaptor.getInitialValue()[1]);
    Value outHandle = entryAlloca(rewriter, location, i64, 1, 8);
    Value invalid = llvmConstant(rewriter, location, i64, UINT64_MAX);
    LLVM::StoreOp::create(rewriter, location, invalid, outHandle, 8);
    Value contextAddress = LLVM::AddressOfOp::create(
        rewriter, location, pointer, "__obelisk_current_context");
    Value context =
        LLVM::LoadOp::create(rewriter, location, pointer, contextAddress, 8);
    SmallVector<uint64_t, 2> rootOffsets;
    if (!sim::getManagedHandleOffsets(op.getInitialValue().getType(),
                                      rootOffsets))
      return failure();
    SmallVector<Value> arguments{
        context, llvmConstant(rewriter, location, i64, *width), value, unknown};
    StringRef allocation = "obelisk_rt_v1_native_state_alloc";
    if (!rootOffsets.empty()) {
      Value count = llvmConstant(rewriter, location, i64, rootOffsets.size());
      Value offsets =
          entryAlloca(rewriter, location, i64, rootOffsets.size(), 8);
      for (auto [index, offset] : llvm::enumerate(rootOffsets))
        LLVM::StoreOp::create(
            rewriter, location, llvmConstant(rewriter, location, i64, offset),
            byteGEP(rewriter, location, offsets, index * sizeof(uint64_t)), 8);
      allocation = "obelisk_rt_v1_native_state_alloc_with_roots";
      arguments.push_back(offsets);
      arguments.push_back(count);
    }
    arguments.push_back(outHandle);
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(), allocation), arguments)
            .getResult();
    reportManagedStatus(rewriter, location, context, status);
    Value handle = LLVM::LoadOp::create(rewriter, location, i64, outHandle, 8);
    rewriter.replaceOp(op, handle);
    return success();
  }
};

class AutomaticOwnerReleaseConversion final
    : public OpConversionPattern<sim::SimRefReleaseOwnerOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimRefReleaseOwnerOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getReference().size() != 1)
      return failure();
    emitNativeStateRelease(rewriter, op.getLoc(),
                           adaptor.getReference().front(), true);
    rewriter.eraseOp(op);
    return success();
  }
};

} // namespace

LogicalResult insertAutomaticOwnerReleases(sim::SimFuncOp function) {
  SmallVector<sim::SimRefAllocOp> allocations;
  function.walk([&](sim::SimRefAllocOp allocation) {
    allocations.push_back(allocation);
  });
  if (allocations.empty())
    return success();

  for (sim::SimRefAllocOp allocation : allocations) {
    if (allocation->hasAttr(automaticOwnerReleaseInstrumentedAttr))
      continue;
    llvm::SetVector<Value> family =
        collectReferenceFamily(allocation.getResult());

    // Reference ownership is represented by the allocation rather than by an
    // SSA value. A block argument fed by more than one ownership family would
    // make the release path-dependent and may release the selected handle
    // twice. Reject such merges until ownership tokens are explicit in SSA.
    for (Value reference : family) {
      auto argument = dyn_cast<BlockArgument>(reference);
      if (!argument)
        continue;
      Block *owner = argument.getOwner();
      unsigned argumentIndex = argument.getArgNumber();
      for (Block &predecessor : function.getBody()) {
        Operation *terminator = predecessor.getTerminator();
        for (auto [successorIndex, successor] :
             llvm::enumerate(predecessor.getSuccessors())) {
          if (successor != owner)
            continue;
          auto branch = dyn_cast<BranchOpInterface>(terminator);
          if (!branch)
            return allocation.emitError(
                "automatic reference block argument has an unsupported "
                "incoming edge");
          SuccessorOperands operands =
              branch.getSuccessorOperands(successorIndex);
          if (argumentIndex >= operands.size() ||
              operands.isOperandProduced(argumentIndex) ||
              !family.contains(operands[argumentIndex]))
            return allocation.emitError(
                "automatic reference block argument merges distinct "
                "ownership origins");
        }
      }
    }

    // Earlier allocations may have split lifetime-exit edges, so recompute
    // dominance for the current CFG rather than retaining a stale analysis.
    DominanceInfo dominance(function);
    Liveness liveness(function);
    auto isLiveInto = [&](Block *block) {
      for (Value reference : family) {
        if (liveness.getLiveIn(block).contains(reference))
          return true;
        if (auto argument = dyn_cast<BlockArgument>(reference);
            argument && argument.getOwner() == block)
          return true;
      }
      return false;
    };
    auto representativeAt = [&](Operation *operation) -> Value {
      for (Value reference : family)
        if (dominance.dominates(reference, operation))
          return reference;
      return {};
    };

    llvm::DenseSet<Block *> activeBlocks;
    SmallVector<Block *> worklist{allocation->getBlock()};
    while (!worklist.empty()) {
      Block *block = worklist.pop_back_val();
      if (!activeBlocks.insert(block).second)
        continue;
      Operation *terminator = block->getTerminator();
      Value representative = representativeAt(terminator);
      if (!representative)
        return allocation.emitError(
            "cannot identify an automatic reference on a CFG lifetime exit");

      SmallVector<bool> liveEdges;
      liveEdges.reserve(terminator->getNumSuccessors());
      bool anyLive = false;
      for (Block *successor : terminator->getSuccessors()) {
        bool live = isLiveInto(successor);
        liveEdges.push_back(live);
        anyLive |= live;
        if (live)
          worklist.push_back(successor);
      }

      OpBuilder builder(terminator);
      if (!anyLive) {
        insertAutomaticOwnerRelease(builder, allocation.getLoc(),
                                    representative);
        continue;
      }
      if (llvm::all_of(liveEdges, [](bool live) { return live; }))
        continue;

      auto branch = dyn_cast<BranchOpInterface>(terminator);
      if (!branch)
        return allocation.emitError(
            "cannot split an automatic-reference lifetime exit edge");
      for (unsigned successorIndex = 0, end = terminator->getNumSuccessors();
           successorIndex != end; ++successorIndex) {
        if (liveEdges[successorIndex])
          continue;
        Block *destination = terminator->getSuccessor(successorIndex);
        SuccessorOperands successorOperands =
            branch.getSuccessorOperands(successorIndex);
        if (successorOperands.getProducedOperandCount() != 0)
          return allocation.emitError(
              "cannot split a produced automatic-reference CFG edge");
        SmallVector<Value> forwarded(
            successorOperands.getForwardedOperands().begin(),
            successorOperands.getForwardedOperands().end());
        successorOperands.getMutableForwardedOperands().append(representative);

        auto *cleanup = new Block;
        function.getBody().push_back(cleanup);
        for (Value value : forwarded)
          cleanup->addArgument(value.getType(), terminator->getLoc());
        BlockArgument cleanupHandle = cleanup->addArgument(
            representative.getType(), terminator->getLoc());
        terminator->setSuccessor(cleanup, successorIndex);

        OpBuilder cleanupBuilder(cleanup, cleanup->end());
        insertAutomaticOwnerRelease(cleanupBuilder, allocation.getLoc(),
                                    cleanupHandle);
        cf::BranchOp::create(cleanupBuilder, terminator->getLoc(), destination,
                             cleanup->getArguments().drop_back());
      }
    }
    allocation->setAttr(automaticOwnerReleaseInstrumentedAttr,
                        UnitAttr::get(function.getContext()));
  }
  return success();
}

LogicalResult
releaseNativeAutomaticState(ModuleOp module,
                            const ReferenceArgumentMap &referenceArguments) {
  SmallVector<sim::SimFuncOp> functions;
  module.walk([&](sim::SimFuncOp function) { functions.push_back(function); });
  for (sim::SimFuncOp function : functions) {
    auto arguments = referenceArguments.find(function.getOperation());
    if (arguments == referenceArguments.end() || function.getBody().empty())
      continue;
    SmallVector<sim::SimReturnOp> returns;
    function.walk(
        [&](sim::SimReturnOp operation) { returns.push_back(operation); });
    for (sim::SimReturnOp operation : returns) {
      OpBuilder builder(operation);
      for (unsigned index : arguments->second) {
        if (index >= function.getBody().front().getNumArguments())
          return function.emitError(
              "converted automatic-reference argument index is invalid");
        emitNativeStateRelease(builder, operation.getLoc(),
                               function.getBody().front().getArgument(index),
                               false);
      }
    }
  }
  return success();
}

void populateReferenceLifetimeToLLVMConversionPatterns(
    RewritePatternSet &patterns, TypeConverter &converter) {
  patterns.add<AutomaticOwnerReleaseConversion, RefAllocConversion>(
      converter, patterns.getContext());
}

} // namespace detail

namespace {

class ObeliskSimInstrumentReferenceLifetimesPass
    : public impl::ObeliskSimInstrumentReferenceLifetimesPassBase<
          ObeliskSimInstrumentReferenceLifetimesPass> {
public:
  void runOnOperation() override {
    if (failed(detail::insertAutomaticOwnerReleases(getOperation())))
      signalPassFailure();
  }
};

} // namespace
} // namespace obelisk
