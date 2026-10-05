//===- NativeWords.cpp - Preserve independent packed lane computations
//------===//
#include "../../../Conversion/SimulationToSchedule/NativePipeline.h"
#include "NativeWordPredication.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/CSE.h"
#include "obelisk/Analysis/HandleDataflowAnalysis.h"
#include "obelisk/Analysis/NativeWordAnalysis.h"
#include "obelisk/Analysis/SimulationAnalysis.h"
#include "obelisk/Dialect/Schedule/ScheduleDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleOps.h"
#include "obelisk/Dialect/Schedule/Transforms/Passes.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

using namespace mlir;
namespace obelisk {
#define GEN_PASS_DEF_OPTIMIZENATIVEWORDSPASS
#include "obelisk/Dialect/Schedule/Transforms/Passes.h.inc"
namespace {
using Kind = analysis::WordLaneKind;
bool isLaneReference(Kind kind) {
  return kind == Kind::Reference || kind == Kind::ReverseReference;
}

bool canWidenDirectly(Operation *op) {
  if (isa<sim::SimRefLoadOp, sim::SimArrayDynExtractOp,
          sim::SimLogicDynExtractOp, sim::SimBitsDynExtractOp>(op))
    return true;
  return analysis::isWordBitwiseComputation(op);
}

bool coalesceWord(sim::SimFuncOp function, analysis::BoundedLoop loop,
                  const analysis::HandleDataflowResult &handles,
                  const analysis::NBAMergeSafety &observers,
                  const DenseSet<uint64_t> &privateRoots,
                  std::string *diagnostic = nullptr) {
  auto reject = [&](StringRef reason, Operation *op = nullptr) {
    if (diagnostic) {
      diagnostic->clear();
      llvm::raw_string_ostream os(*diagnostic);
      os << reason;
      if (op) {
        os << ": ";
        os << op->getName();
      }
    }
    return false;
  };
  if (!loop.stride.isOne() && !loop.stride.isAllOnes())
    return reject("nonunit stride");
  // Additional header arguments may be unchanged captures forwarded through
  // the backedge. A varying recurrence needs a different dependence proof;
  // sparse lane uniformity alone does not establish loop invariance.
  for (BlockArgument argument : loop.header->getArguments())
    if (argument != loop.induction &&
        loop.latch.getDestOperands()[argument.getArgNumber()] != argument)
      return reject("varying loop-carried state");
  auto bounds = analysis::getInductionRange(loop, false);
  if (!bounds)
    return reject("unproved induction range");
  APInt lanes = bounds->smax() - bounds->smin() + 1;
  if (!lanes.uge(2))
    return reject("fewer than two lanes");
  unsigned elementWidth = 0;
  for (Block *block : loop.blocks)
    for (auto store : block->getOps<sim::SimRefStoreOp>()) {
      auto bits = sim::getPackedWidth(store.getValue().getType());
      if (!bits || *bits > 32 || !*bits)
        return reject("store element too wide for two native lanes", store);
      elementWidth = std::max<unsigned>(elementWidth, *bits);
    }
  if (!elementWidth)
    return reject("no packed store");

  analysis::NativeWordAnalysis lanesAnalysis(function, loop, elementWidth);
  DenseMap<Operation *, analysis::WordTruthTable> tables;
  DenseMap<uint64_t, SmallVector<Value>> reads, writes;
  unsigned stores = 0;
  for (Block *block : loop.blocks) {
    auto escapes = [&](Value value) {
      return llvm::any_of(value.getUsers(), [&](Operation *user) {
        return !loop.members.contains(user->getBlock());
      });
    };
    if (block != loop.header && llvm::any_of(block->getArguments(), escapes))
      return reject("escaping block argument");
    for (Operation &op : *block) {
      if (op.getNumRegions() || isa<CallOpInterface>(op) ||
          (block != loop.header && llvm::any_of(op.getResults(), escapes)))
        return reject("nested region, call or escaping result", &op);
      if (&op == loop.condition.getOperation())
        continue;
      if (block == loop.header) {
        if (!isMemoryEffectFree(&op))
          return reject("effectful header", &op);
        continue;
      }
      if (auto branch = dyn_cast<cf::CondBranchOp>(op)) {
        if (lanesAnalysis.get(branch.getCondition()) != Kind::Uniform)
          return reject("lane-dependent branch", &op);
      }
      if (auto branch = dyn_cast<cf::SwitchOp>(op)) {
        if (lanesAnalysis.get(branch.getFlag()) != Kind::Uniform)
          return reject("lane-dependent branch", &op);
      }
      if (auto branch = dyn_cast<BranchOpInterface>(op)) {
        for (Block *successor : op.getSuccessors())
          if (!loop.members.contains(successor))
            return reject("early loop exit", &op);
        continue;
      }
      auto load = dyn_cast<sim::SimRefLoadOp>(op);
      auto store = dyn_cast<sim::SimRefStoreOp>(op);
      if (load || store) {
        Value reference = load ? load.getReference() : store.getReference();
        auto fact = handles.facts.find(reference);
        if (fact == handles.facts.end() || !fact->second.descriptor ||
            fact->second.resource != schedule::ComputeResourceKind::Storage)
          return reject("unresolved storage root", &op);
        uint64_t root = *fact->second.descriptor;
        if (store) {
          // Observer privacy is separate from the memory-dependence proof
          // below; in-place updates need equal or disjoint access maps.
          if (!isLaneReference(lanesAnalysis.get(reference)) ||
              !privateRoots.contains(root) ||
              (!observers.storageMayMerge(root) &&
               !observers.storageNeedsTransients(root)))
            return reject("unproved destination geometry or observer", &op);
          Kind value = lanesAnalysis.get(store.getValue());
          if (value != Kind::Logic && value != Kind::Uniform)
            return reject("non-lane store value", &op);
          writes[root].push_back(reference);
          ++stores;
        } else {
          reads[root].push_back(reference);
        }
      } else if (!isMemoryEffectFree(&op)) {
        return reject("effectful body", &op);
      }
      for (Value value : op.getResults()) {
        Kind kind = lanesAnalysis.get(value);
        if (kind == Kind::Unknown)
          return reject("unknown lane dataflow", &op);
        if (kind == Kind::Logic || isLaneReference(kind)) {
          Type type = value.getType();
          if (auto reference = dyn_cast<sim::RefType>(type))
            type = reference.getElementType();
          // Every widened value must fit, including loaded fields wider than
          // any destination. Uniform scalar values do not consume lane space.
          auto bits = sim::getPackedWidth(type);
          if (!bits || !*bits || *bits > 32)
            return reject("lane element too wide for two native lanes", &op);
          elementWidth = std::max<unsigned>(elementWidth, *bits);
        }
        if (kind == Kind::Logic && !canWidenDirectly(&op)) {
          auto table = analysis::computeWordTruthTable(&op);
          if (!table)
            return reject("unfoldable scalar computation", &op);
          tables.try_emplace(&op, std::move(*table));
        }
        if (isLaneReference(kind)) {
          auto certificate = handles.certificates.find(value);
          if (certificate == handles.certificates.end() ||
              !certificate->second.inBounds)
            return reject("unproved reference bounds", &op);
          // References must end at the proven access; an escaping view or
          // additional selector would invalidate contiguous lane geometry.
          if (llvm::any_of(value.getUsers(), [](Operation *user) {
                return !isa<sim::SimRefLoadOp, sim::SimRefStoreOp>(user) &&
                       !analysis::isWordReferenceIdentity(user);
              }))
            return reject("unsupported reference use", &op);
        }
      }
    }
  }
  if (!stores)
    return reject("no lane store");
  bool nestedArguments = llvm::any_of(loop.blocks, [&](Block *block) {
    return block != loop.header && block->getNumArguments() != 0;
  });
  auto independent = [&](Value a, Value b) {
    const auto &x = handles.facts.find(a)->second;
    const auto &y = handles.facts.find(b)->second;
    if (x.low + x.width <= y.low || y.low + y.width <= x.low)
      return true;
    // Equal lane maps only alias the same element in each iteration. Preserve
    // source statement order within that lane. Nested executions also need
    // disjoint outer residues: equal maps alone could move across lanes on
    // different inner iterations.
    if ((nestedArguments && !lanesAnalysis.hasDisjointIterations(a)) ||
        !isLaneReference(lanesAnalysis.get(a)) ||
        lanesAnalysis.get(a) != lanesAnalysis.get(b))
      return false;
    auto access = [](Value value) {
      Operation *op = value.getDefiningOp();
      while (op && analysis::isWordReferenceIdentity(op))
        op = op->getOperand(0).getDefiningOp();
      return op;
    };
    Operation *lhs = access(a), *rhs = access(b);
    return lhs && rhs &&
           OperationEquivalence::isEquivalentTo(
               lhs, rhs, OperationEquivalence::exactValueMatch, nullptr,
               OperationEquivalence::IgnoreLocations);
  };
  for (auto &entry : writes) {
    for (Value a : entry.second) {
      if (nestedArguments && !lanesAnalysis.hasDisjointIterations(a))
        return reject("unproved nested store self-dependence",
                      a.getDefiningOp());
      for (Value b : entry.second)
        if (a != b && !independent(a, b))
          return reject("overlapping store maps", a.getDefiningOp());
      for (Value b : reads[entry.first])
        if (!independent(a, b))
          return reject("overlapping read/write maps", b.getDefiningOp());
    }
  }
  // An index is only an address, induction update or original loop condition.
  // In particular, index-to-logic arithmetic cannot become lane-wise logic.
  for (Block *block : loop.blocks)
    for (BlockArgument argument : block->getArguments())
      if (argument != loop.induction &&
          lanesAnalysis.get(argument) != Kind::Uniform)
        return reject("lane-dependent phi");

  unsigned groupSize = 64 / elementWidth;
  while (groupSize > 1 && (lanes.ult(groupSize) || lanes.urem(groupSize) != 0))
    --groupSize;
  if (groupSize < 2)
    groupSize = 64 / elementWidth;
  APInt remainder(lanes.getBitWidth(), lanes.urem(groupSize));
  bool hasTail = !remainder.isZero();
  unsigned width = groupSize * elementWidth;
  bool chunked = lanes.ugt(groupSize);

  // All eligibility decisions precede mutation. Each original lane has the
  // same ordered writes and four-state value; the influence proof excludes
  // observation of cross-lane intermediate states (LRM 4.3, 4.6, 9.4.2).
  OpBuilder builder(function.getContext());
  Location loc = loop.condition.getLoc();
  Region *parent = loop.header->getParent();
  auto *wrapper = new Block;
  parent->getBlocks().insert(loop.header->getIterator(), wrapper);
  for (BlockArgument argument : loop.header->getArguments())
    wrapper->addArgument(argument.getType(), argument.getLoc());
  SmallVector<std::pair<Operation *, unsigned>> entries;
  for (Block *pred : loop.header->getPredecessors())
    if (!loop.members.contains(pred))
      for (unsigned i = 0; i < pred->getNumSuccessors(); ++i)
        if (pred->getSuccessor(i) == loop.header)
          entries.emplace_back(pred->getTerminator(), i);
  for (auto [terminator, i] : entries)
    terminator->setSuccessor(wrapper, i);
  builder.setInsertionPointToEnd(wrapper);
  // An exit can forward unchanged captures, constants, or the final induction
  // value into the next loop. Rematerialize its pure header expressions at
  // that final value outside the region; no lane-local value escapes.
  IRMapping exitMapping;
  APInt delta = loop.isAdd ? loop.stride : -loop.stride;
  Value exitInduction = arith::ConstantOp::create(
      builder, loc,
      builder.getIntegerAttr(loop.induction.getType(),
                             loop.init + lanes * delta));
  for (BlockArgument argument : loop.header->getArguments())
    exitMapping.map(argument,
                    argument == loop.induction
                        ? exitInduction
                        : wrapper->getArgument(argument.getArgNumber()));
  for (Operation &op : loop.header->without_terminator())
    builder.clone(op, exitMapping);
  auto replaceLiveOut = [&](Value original) {
    original.replaceUsesWithIf(
        exitMapping.lookup(original), [&](OpOperand &use) {
          return !loop.members.contains(use.getOwner()->getBlock());
        });
  };
  for (BlockArgument argument : loop.header->getArguments())
    replaceLiveOut(argument);
  for (Operation &op : loop.header->without_terminator())
    for (Value value : op.getResults())
      replaceLiveOut(value);
  SmallVector<Value> exitOperands;
  for (Value value : loop.condition.getFalseDestOperands())
    exitOperands.push_back(exitMapping.lookupOrDefault(value));
  Block *wordBlock = wrapper;
  Value chunkIndex = wrapper->getArgument(loop.induction.getArgNumber());
  if (chunked) {
    wordBlock = new Block;
    parent->getBlocks().insert(loop.header->getIterator(), wordBlock);
    Value limit = arith::ConstantOp::create(
        builder, loc,
        builder.getIntegerAttr(loop.induction.getType(),
                               hasTail ? loop.init + (lanes - remainder) * delta
                                       : loop.limit));
    Value more = arith::CmpIOp::create(
        builder, loc, hasTail ? arith::CmpIPredicate::ne : loop.predicate,
        chunkIndex, limit);
    SmallVector<Value> tailOperands(wrapper->getArguments());
    tailOperands[loop.induction.getArgNumber()] = limit;
    cf::CondBranchOp::create(builder, loc, more, wordBlock, ValueRange{},
                             hasTail ? loop.header : loop.exit,
                             hasTail ? ValueRange(tailOperands)
                                     : ValueRange(exitOperands));
    builder.setInsertionPointToEnd(wordBlock);
  }
  auto region =
      schedule::NativeWordRegionOp::create(builder, loc, width, groupSize);
  if (chunked) {
    Value stride = arith::ConstantOp::create(
        builder, loc,
        builder.getIntegerAttr(loop.induction.getType(), delta * groupSize));
    Value next = arith::AddIOp::create(builder, loc, chunkIndex, stride);
    SmallVector<Value> operands(wrapper->getArguments());
    operands[loop.induction.getArgNumber()] = next;
    cf::BranchOp::create(builder, loc, wrapper, operands);
  } else {
    cf::BranchOp::create(builder, loc, loop.exit, exitOperands);
  }
  IRMapping mapping;
  for (Block *old : loop.blocks) {
    auto *next = new Block;
    region.getBody().push_back(next);
    mapping.map(old, next);
    if (old != loop.header)
      for (BlockArgument arg : old->getArguments())
        mapping.map(arg, next->addArgument(arg.getType(), arg.getLoc()));
  }
  builder.setInsertionPointToStart(mapping.lookup(loop.header));
  Value init;
  if (chunked) {
    init = chunkIndex;
    if (delta.isNegative()) {
      Value offset = arith::ConstantOp::create(
          builder, loc,
          builder.getIntegerAttr(loop.induction.getType(), groupSize - 1));
      init = arith::SubIOp::create(builder, loc, init, offset);
    }
  } else {
    init = arith::ConstantOp::create(
        builder, loc,
        builder.getIntegerAttr(loop.induction.getType(), bounds->smin()));
  }
  mapping.map(loop.induction, init);
  for (BlockArgument argument : loop.header->getArguments())
    if (argument != loop.induction)
      mapping.map(argument, wrapper->getArgument(argument.getArgNumber()));
  auto wordType = [&](Type scalar) -> Type {
    unsigned bits = *sim::getPackedWidth(scalar) * groupSize;
    return isa<IntegerType>(sim::getPackedScalarType(scalar))
               ? Type(builder.getIntegerType(bits))
               : Type(sim::LogicType::get(function.getContext(), bits));
  };
  auto mappedValue = [&](Value value) {
    while (Value next = mapping.lookupOrNull(value)) {
      if (next == value)
        break;
      value = next;
    }
    return value;
  };
  auto wideValue = [&](Value original, unsigned targetWidth) -> Value {
    Value value = mappedValue(original);
    if (!isa<IntegerType, sim::LogicType>(value.getType()))
      value = sim::SimPackedFlattenOp::create(
          builder, original.getLoc(), sim::getPackedScalarType(value.getType()),
          value);
    if (sim::getPackedWidth(value.getType()) == targetWidth)
      return value;
    unsigned scalarWidth = targetWidth / groupSize;
    assert(sim::getPackedWidth(value.getType()) == scalarWidth &&
           "a broadcast must preserve the complete scalar element");
    if (scalarWidth > 1) {
      bool integer = isa<IntegerType>(value.getType());
      if (integer)
        value = sim::SimLogicFromBitsOp::create(
            builder, original.getLoc(),
            sim::LogicType::get(function.getContext(), scalarWidth), value);
      value = sim::SimLogicReplicateOp::create(
          builder, original.getLoc(),
          sim::LogicType::get(function.getContext(), targetWidth), value,
          groupSize);
      if (integer)
        value = sim::SimLogicToBitsOp::create(
            builder, original.getLoc(), builder.getIntegerType(targetWidth),
            value);
      return value;
    }
    if (isa<IntegerType>(value.getType()))
      return arith::ExtSIOp::create(builder, original.getLoc(),
                                    wordType(value.getType()), value);
    return sim::SimLogicResizeOp::create(
        builder, original.getLoc(), wordType(value.getType()), value, true);
  };
  // Blocks need not be listed in dominance order: clone operands first and
  // remap after every definition exists, just as for the bounded CFG unroller.
  SmallVector<Operation *> cloned;
  DenseMap<Operation *, unsigned> reversedStores;
  DenseSet<Operation *> widenedComputations;
  auto lowIndex = [&](Operation &op, bool reverse) -> Value {
    Type result = op.getResult(0).getType();
    if (auto reference = dyn_cast<sim::RefType>(result))
      result = reference.getElementType();
    unsigned scalarWidth = *sim::getPackedWidth(result);
    Value index = op.getOperand(1);
    if (isa<sim::LogicType>(index.getType())) {
      index = sim::SimLogicToBitsOp::create(
          builder, op.getLoc(),
          builder.getIntegerType(*sim::getPackedWidth(index.getType())), index);
      cloned.push_back(index.getDefiningOp());
    }
    unsigned bits = cast<IntegerType>(index.getType()).getWidth();
    if (bits < 65) {
      index = arith::ExtSIOp::create(builder, op.getLoc(),
                                     builder.getIntegerType(65), index);
      cloned.push_back(index.getDefiningOp());
    }
    auto constant = [&](int64_t value) -> Value {
      return arith::ConstantOp::create(
          builder, op.getLoc(), index.getType(),
          builder.getIntegerAttr(
              index.getType(),
              APInt(cast<IntegerType>(index.getType()).getWidth(), value,
                    /*isSigned=*/true)));
    };
    if (isa<sim::SimRefArrayElementOp, sim::SimArrayDynExtractOp>(op)) {
      Type type = op.getOperand(0).getType();
      if (auto reference = dyn_cast<sim::RefType>(type))
        type = reference.getElementType();
      int64_t base;
      bool reversed;
      if (auto array = dyn_cast<sim::PackedArrayType>(type)) {
        base = array.getRight();
        reversed = array.getLeft() < array.getRight();
      } else {
        auto unpacked = cast<sim::UnpackedArrayType>(type);
        base = unpacked.getLeft();
        reversed = unpacked.getLeft() > unpacked.getRight();
      }
      if (reversed) {
        index =
            arith::SubIOp::create(builder, op.getLoc(), constant(base), index);
        cloned.push_back(index.getDefiningOp());
      } else if (base != 0) {
        index =
            arith::SubIOp::create(builder, op.getLoc(), index, constant(base));
        cloned.push_back(index.getDefiningOp());
      }
    }
    if (reverse) {
      index = arith::SubIOp::create(builder, op.getLoc(), index,
                                    constant(groupSize - 1));
      cloned.push_back(index.getDefiningOp());
    }
    if (scalarWidth > 1) {
      index = arith::MulIOp::create(builder, op.getLoc(), index,
                                    constant(scalarWidth));
      cloned.push_back(index.getDefiningOp());
    }
    return index;
  };
  for (Block *old : loop.blocks) {
    builder.setInsertionPointToEnd(mapping.lookup(old));
    for (Operation &op : *old) {
      if (&op == loop.condition.getOperation()) {
        auto branch = cf::BranchOp::create(
            builder, op.getLoc(), loop.condition.getTrueDest(),
            loop.condition.getTrueDestOperands());
        cloned.push_back(branch);
        continue;
      }
      if (&op == loop.latch.getOperation()) {
        schedule::NativeWordYieldOp::create(builder, op.getLoc());
        continue;
      }
      Kind kind = op.getNumResults() == 1 ? lanesAnalysis.get(op.getResult(0))
                                          : Kind::Unknown;
      // Lane aggregates are represented by their flat word throughout the
      // region. Flatten/unflatten preserve the exact value and unknown planes.
      if (kind == Kind::Logic &&
          isa<sim::SimPackedFlattenOp, sim::SimPackedUnflattenOp>(op)) {
        mapping.map(op.getResult(0), op.getOperand(0));
        continue;
      }
      if (isLaneReference(kind)) {
        if (analysis::isWordReferenceIdentity(&op)) {
          mapping.map(op.getResult(0), op.getOperand(0));
          continue;
        }
        Type word = wordType(
            cast<sim::RefType>(op.getResult(0).getType()).getElementType());
        auto resultType = sim::RefType::get(function.getContext(), word);
        Value low = lowIndex(op, kind == Kind::ReverseReference);
        Operation *wide;
        if (isa<sim::UnpackedArrayType>(
                cast<sim::RefType>(op.getOperand(0).getType())
                    .getElementType()))
          wide = schedule::NativeWordViewOp::create(
              builder, op.getLoc(), resultType, op.getOperand(0), low);
        else
          wide = sim::SimRefDynExtractOp::create(
              builder, op.getLoc(), resultType, op.getOperand(0), low);
        wide->setAttrs(op.getAttrDictionary());
        mapping.map(op.getResult(0), wide->getResult(0));
        cloned.push_back(wide);
        continue;
      }
      auto table = tables.find(&op);
      if (table != tables.end()) {
        SmallVector<Value> inputs;
        for (unsigned index : table->second.operands)
          inputs.push_back(op.getOperand(index));
        auto wide = schedule::NativeWordTableOp::create(
            builder, op.getLoc(), wordType(op.getResult(0).getType()), inputs,
            builder.getIntegerAttr(
                builder.getIntegerType(table->second.value.getBitWidth()),
                table->second.value),
            builder.getIntegerAttr(
                builder.getIntegerType(table->second.unknown.getBitWidth()),
                table->second.unknown));
        mapping.map(op.getResult(0), wide.getResult());
        cloned.push_back(wide);
        continue;
      }
      bool reverseResult = false;
      Operation *copy;
      if (kind == Kind::Logic && isa<sim::SimArrayDynExtractOp>(op)) {
        Type inputType = op.getOperand(0).getType();
        auto array = dyn_cast<sim::PackedArrayType>(inputType);
        bool descendingStorage =
            array ? array.getLeft() < array.getRight()
                  : cast<sim::UnpackedArrayType>(inputType).getLeft() >
                        cast<sim::UnpackedArrayType>(inputType).getRight();
        reverseResult = (lanesAnalysis.get(op.getOperand(1)) ==
                         Kind::ReverseIndex) != descendingStorage;
        Value low = lowIndex(op, reverseResult);
        Type wideType = wordType(op.getResult(0).getType());
        if (!array) {
          copy = schedule::NativeWordExtractOp::create(
              builder, op.getLoc(), wideType, op.getOperand(0), low);
        } else {
          Value flat = sim::SimPackedFlattenOp::create(
              builder, op.getLoc(), sim::getPackedScalarType(array),
              op.getOperand(0));
          cloned.push_back(flat.getDefiningOp());
          if (isa<IntegerType>(wideType))
            copy = sim::SimBitsDynExtractOp::create(builder, op.getLoc(),
                                                    wideType, flat, low);
          else
            copy = sim::SimLogicDynExtractOp::create(builder, op.getLoc(),
                                                     wideType, flat, low);
        }
      } else {
        copy = op.clone();
        builder.insert(copy);
      }
      if (kind == Kind::Logic)
        copy->getResult(0).setType(wordType(op.getResult(0).getType()));
      if (kind == Kind::Logic && analysis::isWordBitwiseComputation(&op))
        widenedComputations.insert(copy);
      if (auto load = dyn_cast<sim::SimRefLoadOp>(op))
        reverseResult =
            lanesAnalysis.get(load.getReference()) == Kind::ReverseReference;
      if (kind == Kind::Logic &&
          isa<sim::SimLogicDynExtractOp, sim::SimBitsDynExtractOp>(op)) {
        reverseResult =
            lanesAnalysis.get(op.getOperand(1)) == Kind::ReverseIndex;
        builder.setInsertionPoint(copy);
        copy->setOperand(1, lowIndex(op, reverseResult));
        builder.setInsertionPointAfter(copy);
      }
      if (auto store = dyn_cast<sim::SimRefStoreOp>(op))
        if (lanesAnalysis.get(store.getReference()) == Kind::ReverseReference)
          reversedStores.try_emplace(
              copy, *sim::getPackedWidth(store.getValue().getType()));
      for (auto [from, to] : llvm::zip(op.getResults(), copy->getResults()))
        mapping.map(from, to);
      cloned.push_back(copy);
      if (reverseResult) {
        auto reverse = schedule::NativeWordReverseOp::create(
            builder, op.getLoc(), copy->getResult(0).getType(),
            copy->getResult(0),
            *sim::getPackedWidth(op.getResult(0).getType()));
        mapping.map(op.getResult(0), reverse.getResult());
        cloned.push_back(reverse);
      }
    }
  }
  for (Operation *op : cloned) {
    for (OpOperand &operand : op->getOpOperands())
      operand.set(mappedValue(operand.get()));
    for (BlockOperand &successor : op->getBlockOperands())
      successor.set(mapping.lookupOrDefault(successor.get()));
    if (auto store = dyn_cast<sim::SimRefStoreOp>(op)) {
      unsigned storeWidth =
          *sim::getPackedWidth(store.getReference().getType().getElementType());
      if (sim::getPackedWidth(store.getValue().getType()) != storeWidth) {
        builder.setInsertionPoint(store);
        store.getValueMutable().assign(wideValue(store.getValue(), storeWidth));
      }
      if (reversedStores.contains(op)) {
        builder.setInsertionPoint(store);
        store.getValueMutable().assign(schedule::NativeWordReverseOp::create(
            builder, store.getLoc(), store.getValue().getType(),
            store.getValue(), reversedStores.lookup(op)));
      }
    } else if (widenedComputations.contains(op)) {
      builder.setInsertionPoint(op);
      unsigned resultWidth = *sim::getPackedWidth(op->getResult(0).getType());
      for (OpOperand &operand : op->getOpOperands())
        operand.set(wideValue(operand.get(), resultWidth));
    } else if (auto table = dyn_cast<schedule::NativeWordTableOp>(op)) {
      builder.setInsertionPoint(table);
      for (OpOperand &operand : table->getOpOperands())
        operand.set(wideValue(operand.get(), groupSize));
    }
  }
  if (!hasTail) {
    for (Block *block : loop.blocks)
      block->dropAllReferences();
    for (Block *block : loop.blocks)
      block->dropAllDefinedValueUses();
    for (Block *block : loop.blocks)
      block->erase();
  }
  // Keep the original loop as an exact tail with a constant entry induction.
  // The next analysis iteration can coalesce that smaller range too; a
  // one-element tail retains its original scalar semantics. This preserves
  // bounds, descending iteration and final header values without masked
  // out-of-range accesses.
  return true;
}

struct WordFunctionResult {
  uint64_t formed = 0;
  SmallVector<std::string> messages;
};

// Prepared before the nested pipeline. Workers read immutable design facts
// and own distinct result slots; they never inspect or mutate sibling bodies.
struct WordDesignState {
  WordDesignState(sim::SimDesignOp design, bool diagnostics)
      : observers(design), handles(design), diagnostics(diagnostics) {
    for (int64_t root :
         schedule::get<schedule::Field::NbaPrivateInfluence>(design)
             .asArrayRef())
      privateRoots.insert(root);
    for (auto function : design.getOps<sim::SimFuncOp>()) {
      if (function.getBody().empty() ||
          !schedule::has<schedule::Field::EvalRawCaptures>(function))
        continue;
      order.push_back(function);
      results.try_emplace(function, std::make_unique<WordFunctionResult>());
    }
  }
  analysis::NBAMergeSafety observers;
  analysis::HandleDataflowAnalysis handles;
  DenseSet<uint64_t> privateRoots;
  bool diagnostics;
  SmallVector<sim::SimFuncOp> order;
  DenseMap<Operation *, std::unique_ptr<WordFunctionResult>> results;
};

class OptimizeNativeWordFunctionPass final
    : public PassWrapper<OptimizeNativeWordFunctionPass,
                         OperationPass<sim::SimFuncOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(OptimizeNativeWordFunctionPass)
  explicit OptimizeNativeWordFunctionPass(
      std::shared_ptr<const WordDesignState> state)
      : state(std::move(state)) {}
  StringRef getArgument() const final {
    return "schedule-optimize-native-word-function";
  }
  StringRef getDescription() const final {
    return "Prove and coalesce independent lanes in one native eval function";
  }
  void runOnOperation() override {
    auto function = getOperation();
    auto found = state->results.find(function);
    if (found == state->results.end()) {
      markAllAnalysesPreserved();
      return;
    }
    WordFunctionResult &result = *found->second;
    const auto &handleAnalysis = state->handles;
    const auto &observers = state->observers;
    const auto &privateRoots = state->privateRoots;
    bool diagnostics = state->diagnostics;
    bool changed;
    do {
      changed = false;
      auto loops = analysis::analyzeBoundedLoops(function.getBody());
      if (loops.empty())
        break;
      auto handles = handleAnalysis.analyze(function);
      SmallVector<std::string> rejections;
      for (const auto &loop : llvm::reverse(loops)) {
        std::string reason;
        Location location = loop.condition->getLoc();
        auto reportFormed = [&] {
          if (diagnostics) {
            std::string message;
            llvm::raw_string_ostream os(message);
            os << "obelisk native word formed: " << function.getSymName() << " "
               << location << '\n';
            result.messages.push_back(std::move(message));
          }
        };
        bool success =
            coalesceWord(function, loop, handles, observers, privateRoots,
                         diagnostics ? &reason : nullptr);
        if (success) {
          reportFormed();
          ++result.formed;
          changed = true;
          break;
        }
        auto bounds = analysis::getInductionRange(loop, false);
        bool hasPackedStore = llvm::any_of(loop.blocks, [&](Block *block) {
          return llvm::any_of(block->getOps<sim::SimRefStoreOp>(),
                              [](sim::SimRefStoreOp store) {
                                auto width = sim::getPackedWidth(
                                    store.getValue().getType());
                                return width && *width <= 32;
                              });
        });
        if (hasPackedStore && bounds && bounds->smax() != bounds->smin()) {
          // Speculate only in a disposable body. No change reaches the
          // program unless the complete lane/bounds/observer proof passes.
          IRMapping mapping;
          OwningOpRef<sim::SimFuncOp> candidate(
              cast<sim::SimFuncOp>(function->clone(mapping)));
          Block *header = mapping.lookup(loop.header);
          auto numberValues = [&] {
            // Dominance-based CSE gives structurally congruent address
            // expressions one SSA identity, including packed parent views.
            IRRewriter rewriter(function.getContext());
            DominanceInfo dominance(*candidate);
            eliminateCommonSubExpressions(rewriter, dominance, *candidate);
          };
          numberValues();
          auto findCopy = [&]() -> std::optional<analysis::BoundedLoop> {
            // Rebuild the nested termination proofs after CSE. Asking
            // the single-loop recognizer with no proven inner backedges
            // would silently disable this retry for every nested loop.
            for (auto analyzed :
                 analysis::analyzeBoundedLoops(candidate->getBody()))
              if (analyzed.header == header)
                return analyzed;
            return std::nullopt;
          };
          auto copy = findCopy();
          if (copy)
            success = coalesceWord(
                *candidate, *copy, handleAnalysis.analyze(*candidate),
                observers, privateRoots, diagnostics ? &reason : nullptr);
          if (!success && copy && predicateNativeWordLoop(*copy)) {
            numberValues();
            copy = findCopy();
            success = copy && coalesceWord(*candidate, *copy,
                                           handleAnalysis.analyze(*candidate),
                                           observers, privateRoots,
                                           diagnostics ? &reason : nullptr);
          }
          if (success)
            function.getBody().takeBody(candidate->getBody());
        }
        if (success) {
          reportFormed();
          ++result.formed;
          changed = true;
          break;
        }
        if (diagnostics) {
          std::string message;
          llvm::raw_string_ostream os(message);
          os << "obelisk native word rejected: " << function.getSymName() << " "
             << location << " " << reason << '\n';
          rejections.push_back(std::move(message));
        }
      }
      if (!changed)
        llvm::append_range(result.messages, std::move(rejections));
    } while (changed);
  }

private:
  std::shared_ptr<const WordDesignState> state;
};

class OptimizeNativeWordsPass final
    : public impl::OptimizeNativeWordsPassBase<OptimizeNativeWordsPass> {
  void runOnOperation() override {
    uint64_t formed = 0;
    bool diagnostics = getOperation()->hasAttr("obelisk.debug.native_timing");
    WalkResult status = getOperation().walk([&](sim::SimDesignOp design) {
      auto graph = design.getComputeGraphAttr();
      if (!graph || graph.getVpi() != schedule::ComputeVPIMode::Off ||
          !schedule::has<schedule::Field::NbaPrivateInfluence>(design))
        return WalkResult::advance();
      auto state = std::make_shared<WordDesignState>(design, diagnostics);
      OpPassManager pipeline(sim::SimDesignOp::getOperationName());
      pipeline.addNestedPass<sim::SimFuncOp>(
          std::make_unique<OptimizeNativeWordFunctionPass>(state));
      if (failed(runPipeline(pipeline, design)))
        return WalkResult::interrupt();
      // Print in source order, without locking the function workers.
      for (auto function : state->order) {
        const auto &result = *state->results.find(function)->second;
        formed += result.formed;
        for (const auto &message : result.messages)
          llvm::errs() << message;
      }
      return WalkResult::advance();
    });
    if (status.wasInterrupted()) {
      signalPassFailure();
      return;
    }
    if (diagnostics)
      llvm::errs() << "obelisk native word regions: formed=" << formed << '\n';
    // Only the nonsuspending eval implementation changes. Actor identity,
    // capture ABI, canonical storage and the frozen bytecode stay valid.
    markAnalysesPreserved<detail::NativePipelineAnalysis>();
  }
};
} // namespace
} // namespace obelisk
