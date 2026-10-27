//===- SimulationUnknownSummary.cpp - Report X/Z writes per root ---------===//
//
// schedule.known.roots treats a storage root as known while its bit of the
// generated unknown summary is clear, without loading its unknown plane: a
// clear bit means the root's unknown plane equals its pattern
// (`simulation.steady_unknown`, X/Z exactly in the pattern's bits). So every
// write that makes a summarized root differ from its pattern must set the
// root's bit before a later test runs. This pass instruments the generated
// writes: after each store, memcpy, memmove and memset into the generated
// unknown plane, it tests what was stored against the patterns of the roots
// it overlaps and, when they differ, sets those roots' bits.
//
// It runs before native group materialization, which may keep state in SSA
// and store it later: the report stays at the logical store. A gate's cold
// path calls the runtime, a group boundary, so a rescan reads the stored
// plane. Runtime services that receive the plane report their own writes. A
// plane pointer that escapes anywhere else is reported for every root after
// the escaping operation.
//
//===----------------------------------------------------------------------===//

#include "SimulationToLLVMCoroutinePrivate.h"
#include "talos/Dialect/Schedule/ScheduleFields.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Matchers.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/STLExtras.h"

#include <map>

using namespace mlir;

namespace talos::detail {
namespace {

constexpr StringLiteral unknownPlaneName = "__talos_state_unknown";
// A copy of at most this many words compares each source word with the
// patterns it lands on; a longer one, such as a memory's, reports its roots
// unconditionally.
constexpr uint64_t maximumUnrolledCopyWords = 128;

/// The summarized roots, from the tables materializeUnknownSummary emits.
struct UnknownSummaryWriteInputs {
  struct Root {
    uint64_t begin, end;
    uint32_t bit;
    /// Absolute [low, high) ranges of the pattern's X/Z bits.
    SmallVector<std::pair<uint64_t, uint64_t>, 0> pattern;
  };

  explicit UnknownSummaryWriteInputs(Operation *operation) {
    auto module = cast<ModuleOp>(operation);
    auto roots = module.lookupSymbol<LLVM::GlobalOp>(unknownSummaryRootsName);
    auto patterns =
        module.lookupSymbol<LLVM::GlobalOp>(unknownSummaryPatternsName);
    auto words = [](LLVM::GlobalOp global) {
      SmallVector<uint64_t> result;
      if (auto values =
              dyn_cast_or_null<DenseIntElementsAttr>(global.getValueOrNull()))
        for (const APInt &value : values.getValues<APInt>())
          result.push_back(value.getZExtValue());
      return result;
    };
    if (!roots || !patterns)
      return;
    SmallVector<uint64_t> records = words(roots), ranges = words(patterns);
    if (records.size() % 3 || ranges.size() % 2) {
      roots.emitError("malformed unknown summary tables");
      valid = false;
      return;
    }
    for (size_t index = 0; index != records.size() / 3; ++index) {
      Root root{records[3 * index],
                records[3 * index] + records[3 * index + 1],
                static_cast<uint32_t>(index),
                {}};
      uint64_t first = records[3 * index + 2] & UINT32_MAX;
      uint64_t count = records[3 * index + 2] >> 32;
      if (2 * (first + count) > ranges.size()) {
        roots.emitError("unknown summary root has invalid pattern ranges");
        valid = false;
        return;
      }
      for (uint64_t range = first; range != first + count; ++range)
        root.pattern.push_back(
            {ranges[2 * range], ranges[2 * range] + ranges[2 * range + 1]});
      this->roots.push_back(std::move(root));
    }
    llvm::sort(this->roots, [](const Root &lhs, const Root &rhs) {
      return lhs.begin < rhs.begin;
    });
    patterned = llvm::any_of(
        this->roots, [](const Root &root) { return !root.pattern.empty(); });
    active = true;
  }

  /// The roots overlapping bits [begin, end) of the plane.
  ArrayRef<Root> overlapping(uint64_t begin, uint64_t end) const {
    auto first = llvm::partition_point(
        roots, [&](const Root &root) { return root.end <= begin; });
    auto last = std::find_if(first, roots.end(), [&](const Root &root) {
      return root.begin >= end;
    });
    return ArrayRef<Root>(first, last);
  }

  SmallVector<Root, 0> roots;
  bool active = false;
  bool valid = true;
  /// Some root has a pattern.
  bool patterned = false;
};

/// The byte offset of `pointer` from the plane, when every step is a
/// constant byte GEP.
std::optional<uint64_t> constantByteOffset(Value pointer) {
  uint64_t bytes = 0;
  while (auto gep = pointer.getDefiningOp<LLVM::GEPOp>()) {
    if (!gep.getElemType().isInteger(8) || gep.getIndices().size() != 1)
      return std::nullopt;
    APInt constant;
    auto index = gep.getIndices()[0];
    if (auto integer = dyn_cast<IntegerAttr>(index))
      constant = integer.getValue();
    else if (!matchPattern(cast<Value>(index), m_ConstantInt(&constant)))
      return std::nullopt;
    if (constant.isNegative() || constant.getActiveBits() > 64 ||
        constant.getZExtValue() > UINT64_MAX - bytes)
      return std::nullopt;
    bytes += constant.getZExtValue();
    pointer = gep.getBase();
  }
  if (auto address = pointer.getDefiningOp<LLVM::AddressOfOp>();
      !address || address.getGlobalName() != unknownPlaneName)
    return std::nullopt;
  return bytes;
}

class PublishNativeUnknownSummaryPass final
    : public PassWrapper<PublishNativeUnknownSummaryPass,
                         OperationPass<LLVM::LLVMFuncOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(PublishNativeUnknownSummaryPass)
  StringRef getArgument() const final {
    return "publish-native-unknown-summary";
  }

  void runOnOperation() override {
    LLVM::LLVMFuncOp function = getOperation();
    auto cached = getCachedParentAnalysis<UnknownSummaryWriteInputs>(
        function->getParentOfType<ModuleOp>());
    if (!cached) {
      function.emitError("unknown summary publication requires its tables");
      return signalPassFailure();
    }
    inputs = &cached->get();
    if (!inputs->valid)
      return signalPassFailure();
    if (!inputs->active || function.isExternal())
      return markAllAnalysesPreserved();

    // Every operation a plane pointer reaches, following GEPs, selects and
    // block arguments.
    SmallVector<Operation *> writes, escapes;
    SmallVector<Value> pending;
    DenseSet<Value> seen;
    function.walk([&](LLVM::AddressOfOp address) {
      if (address.getGlobalName() == unknownPlaneName)
        pending.push_back(address.getResult());
    });
    while (!pending.empty()) {
      Value pointer = pending.pop_back_val();
      if (!seen.insert(pointer).second)
        continue;
      for (OpOperand &use : pointer.getUses()) {
        Operation *user = use.getOwner();
        if (isa<LLVM::GEPOp>(user) && use.getOperandNumber() == 0) {
          pending.push_back(user->getResult(0));
        } else if (isa<LLVM::SelectOp>(user) && use.getOperandNumber() != 0) {
          pending.push_back(user->getResult(0));
        } else if (isa<LLVM::LoadOp, LLVM::PtrToIntOp, LLVM::ICmpOp>(user)) {
        } else if (auto store = dyn_cast<LLVM::StoreOp>(user)) {
          (store.getAddr() == pointer ? writes : escapes).push_back(user);
        } else if (isa<LLVM::MemcpyOp, LLVM::MemmoveOp, LLVM::MemsetOp>(user)) {
          // Operand 0 is the destination.
          if (use.getOperandNumber() == 0)
            writes.push_back(user);
        } else if (auto call = dyn_cast<LLVM::CallOp>(user)) {
          // Runtime services report the writes they make.
          if (!call.getCallee() ||
              !call.getCallee()->starts_with("talos_rt_"))
            escapes.push_back(user);
        } else if (auto branch = dyn_cast<BranchOpInterface>(user)) {
          for (unsigned index = 0; index != user->getNumSuccessors(); ++index) {
            SuccessorOperands operands = branch.getSuccessorOperands(index);
            for (unsigned position = 0; position != operands.size(); ++position)
              if (operands[position] == pointer)
                pending.push_back(
                    user->getSuccessor(index)->getArgument(position));
          }
        } else {
          escapes.push_back(user);
        }
      }
    }
    if (writes.empty() && escapes.empty())
      return markAllAnalysesPreserved();
    OpBuilder builder(function.getContext());
    for (Operation *write : writes)
      instrument(builder, write);
    for (Operation *escape : llvm::make_early_inc_range(escapes)) {
      builder.setInsertionPointAfter(escape);
      markAll(builder, escape->getLoc());
    }
  }

private:
  using Root = UnknownSummaryWriteInputs::Root;

  /// Set the summary bits of `roots`.
  void markRoots(OpBuilder &builder, Location location, ArrayRef<Root> roots) {
    std::map<uint64_t, uint64_t> words;
    for (const Root &root : roots)
      words[root.bit / 64] |= uint64_t{1} << (root.bit % 64);
    Type i64 = builder.getI64Type();
    Value summary = LLVM::AddressOfOp::create(
        builder, location, LLVM::LLVMPointerType::get(builder.getContext()),
        unknownSummaryName);
    for (auto [word, mask] : words) {
      Value address = byteGEP(builder, location, summary, 8 * word);
      Value bits = LLVM::LoadOp::create(builder, location, i64, address, 8);
      bits = LLVM::OrOp::create(builder, location, bits,
                                llvmConstant(builder, location, i64, mask));
      LLVM::StoreOp::create(builder, location, bits, address, 8);
    }
  }

  /// Report `bitWidth` bits at `pointer` through the runtime, which finds
  /// their roots.
  void markThroughRuntime(OpBuilder &builder, Location location, Value pointer,
                          Value bitWidth) {
    Type i64 = builder.getI64Type();
    Type ptr = LLVM::LLVMPointerType::get(builder.getContext());
    Value base =
        LLVM::AddressOfOp::create(builder, location, ptr, unknownPlaneName);
    Value bytes = LLVM::SubOp::create(
        builder, location,
        LLVM::PtrToIntOp::create(builder, location, i64, pointer),
        LLVM::PtrToIntOp::create(builder, location, i64, base));
    Value bitOffset = LLVM::ShlOp::create(
        builder, location, bytes, llvmConstant(builder, location, i64, 3));
    Value context = LLVM::LoadOp::create(
        builder, location, ptr,
        LLVM::AddressOfOp::create(builder, location, ptr,
                                  "__talos_current_context"),
        8);
    LLVM::CallOp::create(builder, location, TypeRange{},
                         "talos_rt_v1_native_state_mark_unknown",
                         ValueRange{context, bitOffset, bitWidth});
  }

  /// A byte count as an i64 bit count.
  static Value bitLength(OpBuilder &builder, Location location, Value bytes) {
    Type i64 = builder.getI64Type();
    if (bytes.getType() != i64)
      bytes = LLVM::ZExtOp::create(builder, location, i64, bytes);
    return LLVM::ShlOp::create(builder, location, bytes,
                               llvmConstant(builder, location, i64, 3));
  }

  void markAll(OpBuilder &builder, Location location) {
    markRoots(builder, location, inputs->roots);
  }

  /// Run `mark` when `test` holds, in a block of its own after the builder's
  /// insertion point.
  void guarded(OpBuilder &builder, Location location, Value test,
               function_ref<void()> mark) {
    Block *head = builder.getInsertionBlock();
    Block *continuation = head->splitBlock(builder.getInsertionPoint());
    Block *marking = new Block;
    head->getParent()->getBlocks().insert(continuation->getIterator(), marking);
    builder.setInsertionPointToEnd(head);
    auto branch =
        LLVM::CondBrOp::create(builder, location, test, marking, continuation);
    branch.setBranchWeights(builder.getDenseI32ArrayAttr({1, (1 << 20) - 1}));
    builder.setInsertionPointToStart(marking);
    mark();
    LLVM::BrOp::create(builder, location, ValueRange{}, continuation);
    builder.setInsertionPointToStart(continuation);
  }

  /// Whether `value` (an integer) differs from `pattern` in a bit of
  /// `tracked`.
  static Value differs(OpBuilder &builder, Location location, Value value,
                       const APInt &tracked, const APInt &pattern) {
    auto type = cast<IntegerType>(value.getType());
    auto constant = [&](const APInt &bits) {
      return LLVM::ConstantOp::create(builder, location, type,
                                      builder.getIntegerAttr(type, bits));
    };
    if (!pattern.isZero())
      value = LLVM::XOrOp::create(builder, location, value, constant(pattern));
    if (!tracked.isAllOnes())
      value = LLVM::AndOp::create(builder, location, value, constant(tracked));
    return LLVM::ICmpOp::create(
        builder, location, LLVM::ICmpPredicate::ne, value,
        LLVM::ConstantOp::create(builder, location, type,
                                 builder.getIntegerAttr(type, 0)));
  }

  /// The roots a write of `bits` bits at `pointer` may overlap, or none when
  /// only the runtime can tell. For a write at a constant offset, `tracked`
  /// selects its bits inside summarized roots and `pattern` the patterns'
  /// X/Z bits among them. `exact` is cleared when the write may land on
  /// pattern bits it cannot be compared with: a dynamic write into a root
  /// with a pattern, or an unresolved one while some root has a pattern.
  std::optional<ArrayRef<Root>> resolve(Operation *write, Value pointer,
                                        uint64_t bits, APInt &tracked,
                                        APInt &pattern, bool &exact) {
    tracked = pattern = APInt::getZero(bits);
    exact = true;
    auto window = [&](uint64_t begin, uint64_t low, uint64_t high,
                      APInt &mask) {
      low = std::max(low, begin);
      high = std::min(high, begin + bits);
      if (low < high)
        mask.setBits(low - begin, high - begin);
    };
    if (std::optional<uint64_t> bytes = constantByteOffset(pointer)) {
      uint64_t begin = 8 * *bytes;
      ArrayRef<Root> roots = inputs->overlapping(begin, begin + bits);
      for (const Root &root : roots) {
        window(begin, root.begin, root.end, tracked);
        for (auto [low, high] : root.pattern)
          window(begin, low, high, pattern);
      }
      return roots;
    }
    tracked.setAllBits();
    // A generated dynamic store records its clipped destination range.
    if (auto range =
            schedule::get<schedule::Field::EvalUnknownWriteRange>(write);
        range && range.size() == 2 && range[0] >= 0 && range[1] > 0 &&
        static_cast<uint64_t>(range[0]) <=
            UINT64_MAX - static_cast<uint64_t>(range[1])) {
      ArrayRef<Root> roots = inputs->overlapping(range[0], range[0] + range[1]);
      exact = llvm::all_of(
          roots, [](const Root &root) { return root.pattern.empty(); });
      return roots;
    }
    exact = !inputs->patterned;
    return std::nullopt;
  }

  static Value always(OpBuilder &builder, Location location) {
    return LLVM::ConstantOp::create(builder, location, builder.getI1Type(),
                                    builder.getBoolAttr(true));
  }

  void instrument(OpBuilder &builder, Operation *write) {
    Location location = write->getLoc();
    Type i64 = builder.getI64Type();
    if (auto store = dyn_cast<LLVM::StoreOp>(write)) {
      Value value = store.getValue();
      auto type = dyn_cast<IntegerType>(value.getType());
      builder.setInsertionPointAfter(store);
      if (!type) {
        // Not a canonical packed word: report it unconditionally.
        markAll(builder, location);
        return;
      }
      APInt tracked, pattern, constant;
      bool exact;
      std::optional<ArrayRef<Root>> roots = resolve(
          store, store.getAddr(), type.getWidth(), tracked, pattern, exact);
      if (roots && (roots->empty() || tracked.isZero()))
        return;
      if (exact && matchPattern(value, m_ConstantInt(&constant)) &&
          ((constant ^ pattern) & tracked).isZero())
        return;
      Value test = exact ? differs(builder, location, value, tracked, pattern)
                         : always(builder, location);
      guarded(builder, location, test, [&] {
        if (roots)
          markRoots(builder, location, *roots);
        else
          markThroughRuntime(
              builder, location, store.getAddr(),
              llvmConstant(builder, location, i64, type.getWidth()));
      });
      return;
    }
    if (auto set = dyn_cast<LLVM::MemsetOp>(write)) {
      builder.setInsertionPointAfter(set);
      APInt length;
      bool constantLength =
          matchPattern(set.getLen(), m_ConstantInt(&length)) &&
          length.getActiveBits() <= 61;
      if (constantLength && length.isZero())
        return;
      APInt tracked, pattern;
      bool exact = !inputs->patterned;
      std::optional<ArrayRef<Root>> roots =
          constantLength ? resolve(set, set.getDst(), 8 * length.getZExtValue(),
                                   tracked, pattern, exact)
                         : std::nullopt;
      if (roots && (roots->empty() || tracked.isZero()))
        return;
      // A byte fill matches a pattern-free destination only when it is zero.
      exact &= !roots || pattern.isZero();
      if (exact && matchPattern(set.getVal(), m_Zero()))
        return;
      Value test = exact ? differs(builder, location, set.getVal(),
                                   APInt::getAllOnes(8), APInt::getZero(8))
                         : always(builder, location);
      guarded(builder, location, test, [&] {
        if (roots)
          markRoots(builder, location, *roots);
        else
          markThroughRuntime(builder, location, set.getDst(),
                             bitLength(builder, location, set.getLen()));
      });
      return;
    }
    // memcpy and memmove: test the source before the copy can change it.
    Value destination = write->getOperand(0), source = write->getOperand(1),
          lengthValue = write->getOperand(2);
    APInt length;
    bool constantLength = matchPattern(lengthValue, m_ConstantInt(&length)) &&
                          length.getActiveBits() <= 61;
    if (constantLength && length.isZero())
      return;
    APInt tracked, pattern;
    bool exact = !inputs->patterned;
    std::optional<ArrayRef<Root>> roots =
        constantLength ? resolve(write, destination, 8 * length.getZExtValue(),
                                 tracked, pattern, exact)
                       : std::nullopt;
    if (roots && (roots->empty() || tracked.isZero()))
      return;
    builder.setInsertionPoint(write);
    Value any;
    if (constantLength && exact &&
        length.getZExtValue() <= 8 * maximumUnrolledCopyWords) {
      // Compare each copied word with the patterns it lands on.
      uint64_t bytes = length.getZExtValue();
      for (uint64_t offset = 0; offset < bytes;) {
        uint64_t chunk = std::min<uint64_t>(8, bytes - offset);
        APInt keep = roots ? tracked.extractBits(8 * chunk, 8 * offset).zext(64)
                           : APInt::getLowBitsSet(64, 8 * chunk);
        APInt expected =
            roots ? pattern.extractBits(8 * chunk, 8 * offset).zext(64)
                  : APInt::getZero(64);
        if (!keep.isZero()) {
          Type type = builder.getIntegerType(8 * chunk);
          Value word = LLVM::LoadOp::create(
              builder, location, type,
              byteGEP(builder, location, source, offset), 1);
          if (chunk != 8)
            word = LLVM::ZExtOp::create(builder, location, i64, word);
          if (!expected.isZero())
            word = LLVM::XOrOp::create(
                builder, location, word,
                llvmConstant(builder, location, i64, expected.getZExtValue()));
          if (!keep.isAllOnes())
            word = LLVM::AndOp::create(
                builder, location, word,
                llvmConstant(builder, location, i64, keep.getZExtValue()));
          any =
              any ? LLVM::OrOp::create(builder, location, any, word).getResult()
                  : word;
        }
        offset += chunk;
      }
      any = LLVM::ICmpOp::create(builder, location, LLVM::ICmpPredicate::ne,
                                 any, llvmConstant(builder, location, i64, 0));
    } else {
      // A long or variable copy, or one that may land on pattern bits at an
      // unknown place, is reported unconditionally.
      any = always(builder, location);
    }
    builder.setInsertionPointAfter(write);
    guarded(builder, location, any, [&] {
      if (roots)
        markRoots(builder, location, *roots);
      else
        markThroughRuntime(builder, location, destination,
                           bitLength(builder, location, lengthValue));
    });
  }

  const UnknownSummaryWriteInputs *inputs = nullptr;
};

} // namespace

LogicalResult prepareNativeUnknownSummaryPublication(ModuleOp module,
                                                     AnalysisManager manager) {
  return success(manager.getAnalysis<UnknownSummaryWriteInputs>().valid);
}

std::unique_ptr<Pass> createPublishNativeUnknownSummaryPass() {
  return std::make_unique<PublishNativeUnknownSummaryPass>();
}

} // namespace talos::detail
