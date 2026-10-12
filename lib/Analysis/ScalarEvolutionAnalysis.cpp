#include "obelisk/Analysis/ScalarEvolutionAnalysis.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/Matchers.h"
#include "obelisk/Analysis/IntegerRangeIPO.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Solver/BitVectorProof.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include <algorithm>
#include <functional>

using namespace mlir;
namespace obelisk::analysis {
namespace {
std::optional<unsigned> integerWidth(Value value) {
  auto width = sim::getPackedWidth(value.getType());
  return isa<IntegerType, sim::LogicType>(value.getType()) && width &&
                 *width > 0 && *width <= 128
             ? width
             : std::nullopt;
}
bool transparent(Operation *op) {
  if (auto reduction = dyn_cast<sim::SimLogicReductionOp>(op))
    return reduction.getKind() == sim::ReductionKind::Or ||
           reduction.getKind() == sim::ReductionKind::Nor ||
           reduction.getKind() == sim::ReductionKind::And;
  return isa<
      arith::AddIOp, arith::SubIOp, arith::MulIOp, arith::AndIOp, arith::OrIOp,
      arith::XOrIOp, arith::ExtSIOp, arith::ExtUIOp, arith::TruncIOp,
      arith::CmpIOp, arith::SelectOp, arith::ShLIOp, arith::ShRUIOp,
      arith::ShRSIOp, arith::DivUIOp, arith::RemUIOp, arith::DivSIOp,
      arith::RemSIOp, sim::SimLogicFromBitsOp, sim::SimLogicToBitsOp,
      sim::SimLogicResizeOp, sim::SimLogicExtractOp, sim::SimLogicUnaryOp,
      sim::SimLogicBinaryOp, sim::SimLogicCompareOp, sim::SimLogicIsTrueOp>(op);
}
} // namespace

struct ScalarEvolutionAnalysis::Impl {
  // Lifetime order matters: SCEV references all the objects above it.
  llvm::LLVMContext context;
  llvm::Module module{"integer_proofs", context};
  llvm::DenseMap<Value, llvm::Value *> values;
  llvm::DenseMap<llvm::Value *, llvm::Value *> replacements;
  llvm::DenseSet<Value> known;
  llvm::SmallVector<std::unique_ptr<llvm::DominatorTree>> dominators;
  llvm::SmallVector<std::unique_ptr<llvm::LoopInfo>> loops;
  llvm::TargetLibraryInfoImpl libraryImpl{
      llvm::Triple("unknown-unknown-unknown")};
  llvm::TargetLibraryInfo library{libraryImpl};
  llvm::SmallVector<std::unique_ptr<llvm::AssumptionCache>> assumptions;
  llvm::SmallVector<std::unique_ptr<llvm::ScalarEvolution>> evolutions;
  llvm::DenseMap<Region *, llvm::ScalarEvolution *> regionEvolution;
  mutable llvm::DenseMap<Value, ConstantIntRanges> cached;
  mutable unsigned queries = 0;
  struct Proof {
    Value value;
    APInt lower, upper;
    SmallVector<solver::BitVectorRange> facts;
    bool valid;
  };
  mutable SmallVector<Proof> proofs;

  llvm::Value *resolve(llvm::Value *value) {
    SmallVector<llvm::Value *> path;
    while (auto *next = replacements.lookup(value)) {
      path.push_back(value);
      value = next;
    }
    for (auto *previous : path)
      replacements[previous] = value;
    return value;
  }

  Impl(Operation *function, const llvm::DenseSet<Value> &selectors) {
    if (selectors.empty())
      return;
    llvm::DenseSet<Value> relevant;
    SmallVector<Value> pending(selectors.begin(), selectors.end());
    function->walk([&](cf::CondBranchOp branch) {
      pending.push_back(branch.getCondition());
    });
    llvm::DenseMap<Value, SmallVector<Value>> dependencies, users;
    llvm::DenseSet<Value> unknownLogic, certifiedLogic;
    while (!pending.empty()) {
      Value value = pending.pop_back_val();
      if (!integerWidth(value) || !relevant.insert(value).second)
        continue;
      bool logic = isa<sim::LogicType>(value.getType());
      if (logic && getIntegerResultRange(value))
        certifiedLogic.insert(value);
      Operation *op = value.getDefiningOp();
      if (auto constant = dyn_cast_or_null<sim::SimLogicConstantOp>(op)) {
        if (!constant.getUnknown().isZero())
          unknownLogic.insert(value);
      } else if (op &&
                 (transparent(op) || isa_and_nonnull<DictionaryAttr>(
                                         getIntegerResultExpression(value)))) {
        if (logic && isa<sim::SimLogicBinaryOp>(op)) {
          auto kind = cast<sim::SimLogicBinaryOp>(op).getKind();
          if (kind != sim::BinaryKind::Add && kind != sim::BinaryKind::Sub &&
              kind != sim::BinaryKind::Mul && kind != sim::BinaryKind::And &&
              kind != sim::BinaryKind::Or && kind != sim::BinaryKind::Xor)
            unknownLogic.insert(value);
        }
        for (Value input : op->getOperands())
          if (integerWidth(input))
            dependencies[value].push_back(input);
          else if (logic && isa<sim::LogicType>(input.getType()))
            // A wide four-state input is outside the integer proof slice,
            // not a known value. Narrowing or comparing it must preserve X/Z.
            unknownLogic.insert(value);
      } else if (auto arg = dyn_cast<BlockArgument>(value);
                 arg && !arg.getOwner()->isEntryBlock()) {
        for (Block *pred : arg.getOwner()->getPredecessors()) {
          auto branch = dyn_cast<BranchOpInterface>(pred->getTerminator());
          if (!branch) {
            if (logic)
              unknownLogic.insert(value);
            continue;
          }
          for (unsigned i = 0; i < pred->getNumSuccessors(); ++i)
            if (pred->getSuccessor(i) == arg.getOwner()) {
              auto operands = branch.getSuccessorOperands(i);
              if (arg.getArgNumber() < operands.getProducedOperandCount()) {
                if (logic)
                  unknownLogic.insert(value);
              } else {
                dependencies[value].push_back(operands[arg.getArgNumber()]);
              }
            }
        }
      } else if (logic) {
        auto argument = dyn_cast<BlockArgument>(value);
        auto function =
            argument
                ? dyn_cast<sim::SimFuncOp>(argument.getOwner()->getParentOp())
                : sim::SimFuncOp{};
        if (!function ||
            !decodeIntegerRange(
                function.getArgAttr(argument.getArgNumber(), integerRangeAttr),
                integerWidth(value)))
          unknownLogic.insert(value);
      }
      for (Value input : dependencies[value]) {
        pending.push_back(input);
        if (logic && isa<sim::LogicType>(input.getType()))
          users[input].push_back(value);
      }
    }
    // Knownness is separate from bit-vector algebra. Propagate every opaque
    // or X/Z input through the dependency graph, including recurrence SCCs.
    // A cyclic arithmetic phi is known only if all its initial inputs are.
    // A global may-range summary independently excludes X/Z on an opaque
    // call result, even when its numeric value depends on memory or on a
    // four-state input converted to two-state inside the callee.
    for (Value value : certifiedLogic)
      unknownLogic.erase(value);
    pending.assign(unknownLogic.begin(), unknownLogic.end());
    while (!pending.empty())
      for (Value user : users[pending.pop_back_val()])
        if (!certifiedLogic.contains(user) && unknownLogic.insert(user).second)
          pending.push_back(user);
    for (Value value : relevant)
      if (!unknownLogic.contains(value))
        known.insert(value);
    for (Region &region : function->getRegions())
      if (!region.empty())
        build(region, relevant);
  }

  llvm::Value *translate(Value result, llvm::IRBuilder<> &builder) {
    Operation *op = result.getDefiningOp();
    auto get = [&](Value value) -> llvm::Value * {
      // Nested regions and other function bodies have independent shadows.
      if (value.getParentRegion() != result.getParentRegion())
        return nullptr;
      auto found = values.find(value);
      return found == values.end() ? nullptr : resolve(found->second);
    };
    if (auto expression = dyn_cast_or_null<DictionaryAttr>(
            getIntegerResultExpression(result))) {
      std::function<llvm::Value *(DictionaryAttr)> evaluate =
          [&](DictionaryAttr node) -> llvm::Value * {
        if (!node)
          return nullptr;
        auto kind = node.getAs<StringAttr>("kind").getValue();
        auto *type = llvm::IntegerType::get(
            context, node.getAs<IntegerAttr>("width").getInt());
        if (kind == "constant")
          return llvm::ConstantInt::get(
              context, node.getAs<IntegerAttr>("value").getValue());
        if (kind == "argument") {
          unsigned index = node.getAs<IntegerAttr>("index").getInt();
          if (index >= op->getNumOperands() ||
              !known.contains(op->getOperand(index)))
            return nullptr;
          return get(op->getOperand(index));
        }
        auto operands = node.getAs<ArrayAttr>("operands");
        auto *lhs = evaluate(cast<DictionaryAttr>(operands[0]));
        if (!lhs)
          return nullptr;
        if (kind == "identity")
          return lhs;
        if (kind == "sext")
          return builder.CreateSExtOrTrunc(lhs, type);
        if (kind == "zext" || kind == "trunc")
          return builder.CreateZExtOrTrunc(lhs, type);
        auto *rhs = evaluate(cast<DictionaryAttr>(operands[1]));
        if (!rhs)
          return nullptr;
        if (kind == "add")
          return builder.CreateAdd(lhs, rhs);
        if (kind == "sub")
          return builder.CreateSub(lhs, rhs);
        if (kind == "mul")
          return builder.CreateMul(lhs, rhs);
        if (kind == "and")
          return builder.CreateAnd(lhs, rhs);
        if (kind == "or")
          return builder.CreateOr(lhs, rhs);
        if (kind == "xor")
          return builder.CreateXor(lhs, rhs);
        return nullptr;
      };
      return evaluate(expression);
    }
    if (op->getNumResults() != 1 || !transparent(op))
      return nullptr;
    // Reading the value plane of unknown logic does not preserve ordinary
    // arithmetic semantics. An opaque freeze is the conservative model.
    for (Value input : op->getOperands())
      if (!get(input) || !known.contains(input))
        return nullptr;
    auto lhs = op->getNumOperands() ? get(op->getOperand(0)) : nullptr;
    auto rhs = op->getNumOperands() > 1 ? get(op->getOperand(1)) : nullptr;
    if (isa<arith::AddIOp>(op))
      return builder.CreateAdd(lhs, rhs);
    if (isa<arith::SubIOp>(op))
      return builder.CreateSub(lhs, rhs);
    if (isa<arith::MulIOp>(op))
      return builder.CreateMul(lhs, rhs);
    if (isa<arith::AndIOp>(op))
      return builder.CreateAnd(lhs, rhs);
    if (isa<arith::OrIOp>(op))
      return builder.CreateOr(lhs, rhs);
    if (isa<arith::XOrIOp>(op))
      return builder.CreateXor(lhs, rhs);
    // Integer shifts and divisions have the same poison and undefined cases
    // in arith and LLVM IR, so the translation is exact.
    if (isa<arith::ShLIOp>(op))
      return builder.CreateShl(lhs, rhs);
    if (isa<arith::ShRUIOp>(op))
      return builder.CreateLShr(lhs, rhs);
    if (isa<arith::ShRSIOp>(op))
      return builder.CreateAShr(lhs, rhs);
    if (isa<arith::DivUIOp>(op))
      return builder.CreateUDiv(lhs, rhs);
    if (isa<arith::RemUIOp>(op))
      return builder.CreateURem(lhs, rhs);
    if (isa<arith::DivSIOp>(op))
      return builder.CreateSDiv(lhs, rhs);
    if (isa<arith::RemSIOp>(op))
      return builder.CreateSRem(lhs, rhs);
    auto *type =
        llvm::IntegerType::get(context, *integerWidth(op->getResult(0)));
    if (isa<arith::ExtSIOp>(op))
      return builder.CreateSExt(lhs, type);
    if (isa<arith::ExtUIOp>(op))
      return builder.CreateZExt(lhs, type);
    if (isa<arith::TruncIOp>(op))
      return builder.CreateTrunc(lhs, type);
    if (isa<sim::SimLogicFromBitsOp, sim::SimLogicToBitsOp>(op))
      return lhs;
    if (isa<sim::SimLogicIsTrueOp>(op))
      return lhs->getType()->isIntegerTy(1)
                 ? lhs
                 : builder.CreateICmpNE(
                       lhs, llvm::ConstantInt::get(lhs->getType(), 0));
    if (auto resize = dyn_cast<sim::SimLogicResizeOp>(op))
      return resize.getIsSigned() ? builder.CreateSExtOrTrunc(lhs, type)
                                  : builder.CreateZExtOrTrunc(lhs, type);
    // Every canonical spelling of a known computation must translate: a
    // truncation is a low slice, x == 0 is !x, and x != 0 is |x.
    if (auto extract = dyn_cast<sim::SimLogicExtractOp>(op)) {
      llvm::Value *bits = lhs;
      if (extract.getLowBit() != 0)
        bits = builder.CreateLShr(
            bits, llvm::ConstantInt::get(lhs->getType(), extract.getLowBit()));
      return builder.CreateTrunc(bits, type);
    }
    if (auto unary = dyn_cast<sim::SimLogicUnaryOp>(op)) {
      switch (unary.getKind()) {
      case sim::UnaryKind::Plus:
        return lhs;
      case sim::UnaryKind::Negate:
        return builder.CreateNeg(lhs);
      case sim::UnaryKind::BitNot:
        return builder.CreateNot(lhs);
      case sim::UnaryKind::LogicalNot:
        return builder.CreateICmpEQ(lhs,
                                    llvm::ConstantInt::get(lhs->getType(), 0));
      }
    }
    if (auto reduction = dyn_cast<sim::SimLogicReductionOp>(op)) {
      auto *zero = llvm::ConstantInt::get(lhs->getType(), 0);
      if (reduction.getKind() == sim::ReductionKind::Or)
        return builder.CreateICmpNE(lhs, zero);
      if (reduction.getKind() == sim::ReductionKind::Nor)
        return builder.CreateICmpEQ(lhs, zero);
      return builder.CreateICmpEQ(
          lhs, llvm::ConstantInt::getAllOnesValue(lhs->getType()));
    }
    if (auto binary = dyn_cast<sim::SimLogicBinaryOp>(op)) {
      switch (binary.getKind()) {
      case sim::BinaryKind::Add:
        return builder.CreateAdd(lhs, rhs);
      case sim::BinaryKind::Sub:
        return builder.CreateSub(lhs, rhs);
      case sim::BinaryKind::Mul:
        return builder.CreateMul(lhs, rhs);
      case sim::BinaryKind::And:
        return builder.CreateAnd(lhs, rhs);
      case sim::BinaryKind::Or:
        return builder.CreateOr(lhs, rhs);
      case sim::BinaryKind::Xor:
        return builder.CreateXor(lhs, rhs);
      default:
        return nullptr;
      }
    }
    if (auto cmp = dyn_cast<arith::CmpIOp>(op)) {
      llvm::CmpInst::Predicate predicate = llvm::CmpInst::BAD_ICMP_PREDICATE;
      switch (cmp.getPredicate()) {
      case arith::CmpIPredicate::eq:
        predicate = llvm::CmpInst::ICMP_EQ;
        break;
      case arith::CmpIPredicate::ne:
        predicate = llvm::CmpInst::ICMP_NE;
        break;
      case arith::CmpIPredicate::slt:
        predicate = llvm::CmpInst::ICMP_SLT;
        break;
      case arith::CmpIPredicate::sle:
        predicate = llvm::CmpInst::ICMP_SLE;
        break;
      case arith::CmpIPredicate::sgt:
        predicate = llvm::CmpInst::ICMP_SGT;
        break;
      case arith::CmpIPredicate::sge:
        predicate = llvm::CmpInst::ICMP_SGE;
        break;
      case arith::CmpIPredicate::ult:
        predicate = llvm::CmpInst::ICMP_ULT;
        break;
      case arith::CmpIPredicate::ule:
        predicate = llvm::CmpInst::ICMP_ULE;
        break;
      case arith::CmpIPredicate::ugt:
        predicate = llvm::CmpInst::ICMP_UGT;
        break;
      case arith::CmpIPredicate::uge:
        predicate = llvm::CmpInst::ICMP_UGE;
        break;
      }
      return builder.CreateICmp(predicate, lhs, rhs);
    }
    if (auto cmp = dyn_cast<sim::SimLogicCompareOp>(op)) {
      llvm::CmpInst::Predicate predicate;
      switch (cmp.getKind()) {
      case sim::CompareKind::Eq:
      case sim::CompareKind::CaseEq:
      case sim::CompareKind::CaseZEq:
      case sim::CompareKind::CaseXZEq:
      case sim::CompareKind::WildEq:
        predicate = llvm::CmpInst::ICMP_EQ;
        break;
      case sim::CompareKind::Ne:
      case sim::CompareKind::CaseNe:
      case sim::CompareKind::WildNe:
        predicate = llvm::CmpInst::ICMP_NE;
        break;
      case sim::CompareKind::SLT:
        predicate = llvm::CmpInst::ICMP_SLT;
        break;
      case sim::CompareKind::SLE:
        predicate = llvm::CmpInst::ICMP_SLE;
        break;
      case sim::CompareKind::SGT:
        predicate = llvm::CmpInst::ICMP_SGT;
        break;
      case sim::CompareKind::SGE:
        predicate = llvm::CmpInst::ICMP_SGE;
        break;
      case sim::CompareKind::ULT:
        predicate = llvm::CmpInst::ICMP_ULT;
        break;
      case sim::CompareKind::ULE:
        predicate = llvm::CmpInst::ICMP_ULE;
        break;
      case sim::CompareKind::UGT:
        predicate = llvm::CmpInst::ICMP_UGT;
        break;
      case sim::CompareKind::UGE:
        predicate = llvm::CmpInst::ICMP_UGE;
        break;
      default:
        return nullptr;
      }
      return builder.CreateICmp(predicate, lhs, rhs);
    }
    if (isa<arith::SelectOp>(op))
      return builder.CreateSelect(lhs, rhs, get(op->getOperand(2)));
    return nullptr;
  }

  void build(Region &region, const llvm::DenseSet<Value> &relevant) {
    if (!region.front().hasNoPredecessors())
      return;
    SmallVector<llvm::Type *> parameterTypes;
    for (BlockArgument arg : region.front().getArguments())
      if (relevant.contains(arg))
        parameterTypes.push_back(
            llvm::IntegerType::get(context, *integerWidth(arg)));
    auto *function = llvm::Function::Create(
        llvm::FunctionType::get(llvm::Type::getVoidTy(context), parameterTypes,
                                false),
        llvm::GlobalValue::InternalLinkage, "ranges", module);
    llvm::DenseMap<Block *, llvm::BasicBlock *> blocks;
    llvm::DenseMap<Operation *, llvm::SmallVector<llvm::BasicBlock *>> edges;
    SmallVector<llvm::Instruction *> retired;
    unsigned parameter = 0;
    for (Block &block : region)
      blocks[&block] = llvm::BasicBlock::Create(context, "", function);
    for (Block &block : region) {
      llvm::IRBuilder<> builder(blocks[&block]);
      for (BlockArgument arg : block.getArguments()) {
        if (!relevant.contains(arg))
          continue;
        auto *type = llvm::IntegerType::get(context, *integerWidth(arg));
        if (block.isEntryBlock()) {
          auto *input = function->getArg(parameter++);
          values[arg] = input;
          auto source = dyn_cast<sim::SimFuncOp>(region.getParentOp());
          if (auto cap = source ? decodeIntegerRange(
                                      source.getArgAttr(arg.getArgNumber(),
                                                        integerRangeAttr),
                                      integerWidth(arg))
                                : std::nullopt) {
            unsigned width = *integerWidth(arg);
            auto unsignedRange =
                cap->umin().isZero() && cap->umax().isMaxValue()
                    ? llvm::ConstantRange(width, true)
                    : llvm::ConstantRange(cap->umin(), cap->umax() + 1);
            auto signedRange =
                cap->smin().isMinSignedValue() && cap->smax().isMaxSignedValue()
                    ? llvm::ConstantRange(width, true)
                    : llvm::ConstantRange(cap->smin(), cap->smax() + 1);
            auto range = unsignedRange.intersectWith(signedRange);
            if (!range.isFullSet() && !range.isEmptySet())
              input->addAttr(
                  llvm::Attribute::get(context, llvm::Attribute::Range, range));
          }
        } else {
          values[arg] = builder.CreatePHI(type, 2);
        }
      }
      for (Operation &op : block)
        for (Value value : op.getResults()) {
          if (!relevant.contains(value))
            continue;
          auto *type = llvm::IntegerType::get(context, *integerWidth(value));
          llvm::APInt bits;
          if (matchPattern(value, m_ConstantInt(&bits)))
            values[value] = llvm::ConstantInt::get(context, bits);
          else if (auto constant = dyn_cast<sim::SimLogicConstantOp>(&op);
                   constant && constant.getUnknown().isZero())
            values[value] =
                llvm::ConstantInt::get(context, constant.getValue());
          else
            values[value] = builder.CreateFreeze(llvm::PoisonValue::get(type));
        }
    }
    for (Block &block : region)
      for (Operation &op : block) {
        for (Value value : op.getResults()) {
          auto found = values.find(value);
          if (found == values.end())
            continue;
          auto *placeholder = llvm::dyn_cast<llvm::FreezeInst>(found->second);
          if (!placeholder || !known.contains(value))
            continue;
          llvm::IRBuilder<> builder(placeholder);
          if (auto *result = translate(value, builder)) {
            placeholder->replaceAllUsesWith(result);
            // Other MLIR values can alias a from/to-bits instruction's result.
            // Update aliases before erasing the disposable placeholder.
            replacements[placeholder] = result;
            // Keep retired pointer identities alive until every binding has
            // been resolved. Reusing a deleted instruction's address would
            // confuse the replacement map with a newly allocated instruction.
            placeholder->removeFromParent();
            retired.push_back(placeholder);
          }
        }
      }
    for (Block &block : region) {
      Operation *terminator = block.getTerminator();
      auto &targets = edges[terminator];
      for (Block *successor : terminator->getSuccessors()) {
        // Separate edge blocks also handle duplicate destinations with
        // different forwarded phi values without inventing an LLVM phi.
        auto *edge = llvm::BasicBlock::Create(context, "", function);
        llvm::IRBuilder<>(edge).CreateBr(blocks[successor]);
        targets.push_back(edge);
      }
      llvm::IRBuilder<> builder(blocks[&block]);
      if (targets.empty())
        builder.CreateRetVoid();
      else if (targets.size() == 1)
        builder.CreateBr(targets[0]);
      else if (targets.size() == 2) {
        llvm::Value *condition = nullptr;
        if (auto branch = dyn_cast<cf::CondBranchOp>(terminator))
          condition = resolve(values.lookup(branch.getCondition()));
        if (!condition)
          condition =
              builder.CreateFreeze(llvm::PoisonValue::get(builder.getInt1Ty()));
        builder.CreateCondBr(condition, targets[0], targets[1]);
      } else {
        auto *condition =
            builder.CreateFreeze(llvm::PoisonValue::get(builder.getInt32Ty()));
        auto *branch =
            builder.CreateSwitch(condition, targets[0], targets.size() - 1);
        for (unsigned i = 1; i < targets.size(); ++i)
          branch->addCase(builder.getInt32(i), targets[i]);
      }
      auto interface = dyn_cast<BranchOpInterface>(terminator);
      for (unsigned i = 0; i < targets.size(); ++i)
        for (BlockArgument arg : terminator->getSuccessor(i)->getArguments()) {
          auto *phi = llvm::dyn_cast_or_null<llvm::PHINode>(values.lookup(arg));
          if (!phi)
            continue;
          llvm::Value *input = nullptr;
          if (interface) {
            auto operands = interface.getSuccessorOperands(i);
            if (arg.getArgNumber() >= operands.getProducedOperandCount())
              input = resolve(values.lookup(operands[arg.getArgNumber()]));
          }
          if (!input) {
            llvm::IRBuilder<> edgeBuilder(targets[i]->getTerminator());
            input = edgeBuilder.CreateFreeze(
                llvm::PoisonValue::get(phi->getType()));
          }
          phi->addIncoming(input, targets[i]);
        }
    }
    for (auto &entry : values)
      entry.second = resolve(entry.second);
    replacements.clear();
    for (auto *placeholder : retired)
      placeholder->deleteValue();
    // Analysis must only observe valid SSA. Unsupported region/branch
    // semantics fail closed instead of reaching SCEV with a malformed CFG.
    if (llvm::verifyFunction(*function)) {
      for (auto &entry : llvm::make_early_inc_range(values))
        if (entry.first.getParentRegion() == &region)
          values.erase(entry.first);
      function->eraseFromParent();
      return;
    }
    dominators.push_back(std::make_unique<llvm::DominatorTree>(*function));
    loops.push_back(std::make_unique<llvm::LoopInfo>(*dominators.back()));
    assumptions.push_back(std::make_unique<llvm::AssumptionCache>(*function));
    evolutions.push_back(std::make_unique<llvm::ScalarEvolution>(
        *function, library, *assumptions.back(), *dominators.back(),
        *loops.back()));
    regionEvolution[&region] = evolutions.back().get();
  }

  std::optional<ConstantIntRanges> range(Value value) const {
    if (!known.contains(value))
      return std::nullopt;
    if (auto found = cached.find(value); found != cached.end())
      return found->second;
    auto *input = values.lookup(value);
    auto found = regionEvolution.find(value.getParentRegion());
    if (!input || found == regionEvolution.end())
      return std::nullopt;
    auto *evolution = found->second;
    auto *expression = evolution->getSCEV(input);
    auto unsignedRange = evolution->getUnsignedRange(expression);
    auto signedRange = evolution->getSignedRange(expression);
    if (unsignedRange.isEmptySet() || signedRange.isEmptySet())
      return std::nullopt;
    auto result = ConstantIntRanges(
        unsignedRange.getUnsignedMin(), unsignedRange.getUnsignedMax(),
        signedRange.getSignedMin(), signedRange.getSignedMax());
    cached.try_emplace(value, result);
    return result;
  }
};

ScalarEvolutionAnalysis::ScalarEvolutionAnalysis(
    Operation *function, const llvm::DenseSet<Value> &selectors)
    : impl(std::make_unique<Impl>(function, selectors)) {}
ScalarEvolutionAnalysis::~ScalarEvolutionAnalysis() = default;
std::optional<ConstantIntRanges>
ScalarEvolutionAnalysis::getRange(Value value) const {
  return impl->range(value);
}
bool ScalarEvolutionAnalysis::excludesXZ(Value value) const {
  return impl->known.contains(value);
}
bool ScalarEvolutionAnalysis::proveBounds(
    Value value, const llvm::APInt &lower, const llvm::APInt &upper,
    llvm::function_ref<std::optional<ConstantIntRanges>(Value)> scopedRange)
    const {
  if (!impl->known.contains(value))
    return false;
  auto *input = impl->values.lookup(value);
  if (!input)
    return false;
  llvm::SmallPtrSet<llvm::Value *, 32> dependencies;
  SmallVector<llvm::Value *> pending{input};
  while (!pending.empty()) {
    auto *current = pending.pop_back_val();
    if (!dependencies.insert(current).second)
      continue;
    if (dependencies.size() > 128)
      return false;
    if (isa<llvm::BinaryOperator, llvm::CastInst, llvm::SelectInst,
            llvm::ICmpInst>(current))
      for (auto &operand : cast<llvm::Instruction>(current)->operands())
        pending.push_back(operand.get());
  }
  SmallVector<solver::BitVectorRange> facts;
  for (const auto &[source, target] : impl->values)
    if (dependencies.contains(target) && impl->known.contains(source))
      if (auto fact = scopedRange(source))
        facts.push_back(
            {target, fact->umin(), fact->umax(), fact->smin(), fact->smax()});
  unsigned width = *integerWidth(value);
  if (lower.getSignificantBits() > width || upper.getSignificantBits() > width)
    return false;
  auto sameFact = [](const solver::BitVectorRange &lhs,
                     const solver::BitVectorRange &rhs) {
    return lhs.value == rhs.value && lhs.umin == rhs.umin &&
           lhs.umax == rhs.umax && lhs.smin == rhs.smin && lhs.smax == rhs.smax;
  };
  for (const auto &proof : impl->proofs)
    if (proof.value == value && proof.lower == lower && proof.upper == upper &&
        proof.facts.size() == facts.size() &&
        std::equal(proof.facts.begin(), proof.facts.end(), facts.begin(),
                   sameFact))
      return proof.valid;
  if (impl->queries >= 32)
    return false;
  ++impl->queries;
  bool valid = solver::proveBitVectorBounds(input, lower.sextOrTrunc(width),
                                            upper.sextOrTrunc(width), facts);
  impl->proofs.push_back({value, lower, upper, std::move(facts), valid});
  return valid;
}
} // namespace obelisk::analysis
