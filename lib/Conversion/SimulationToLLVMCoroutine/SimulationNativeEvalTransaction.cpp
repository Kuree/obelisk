//===- SimulationNativeEvalTransaction.cpp - Local NBA captures ---------===//

#include "SimulationAOTPlanning.h"
#include "SimulationToLLVMCoroutinePrivate.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/SymbolTable.h"
#include "obelisk/Dialect/Native/NativeDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"

using namespace mlir;

namespace obelisk::detail {
namespace {

constexpr StringLiteral dirtyName = "__obelisk_aot_nba_dirty_roots_v1";
constexpr StringLiteral summaryName = "__obelisk_aot_nba_dirty_summary_v1";

struct PendingRoot {
  uint32_t root, ordinal;
  uint64_t mask;
  uint32_t region;
  bool transient;
  std::string accumulator;
  uint64_t bitmapOffset = 0;
  uint64_t payloadOffset = 0;
  bool twoState = false;
};

struct PendingWord {
  uint32_t canonicalWord;
  uint64_t offset, mask = 0;
  SmallVector<unsigned> roots{};
};

struct Address {
  StringRef global;
  uint64_t offset;
};

StringRef baseGlobal(Value pointer) {
  while (auto gep = pointer.getDefiningOp<LLVM::GEPOp>())
    pointer = gep.getBase();
  auto global = pointer.getDefiningOp<LLVM::AddressOfOp>();
  return global ? global.getGlobalName() : StringRef{};
}

struct CaptureInventory {
  SmallVector<int64_t, 5> target;
  llvm::SmallDenseSet<uint64_t, 4> sites;
  unsigned stages = 0;
  bool exact = true;
};

struct MemoryInventory {
  bool seen = false;
  bool exact = true;
};

// This decodes the explicit lowering interface only. Logical ownership and
// captured-target identity came from NativeEvalTransactionPlan, before packing.
std::optional<Address> address(Value pointer) {
  uint64_t offset = 0;
  while (auto gep = pointer.getDefiningOp<LLVM::GEPOp>()) {
    if (!gep.getElemType().isInteger(8) || gep.getIndices().size() != 1)
      return std::nullopt;
    APInt index;
    if (auto constant = dyn_cast<IntegerAttr>(gep.getIndices()[0]))
      index = constant.getValue();
    else if (!matchPattern(cast<Value>(gep.getIndices()[0]),
                           m_ConstantInt(&index)))
      return std::nullopt;
    if (index.isNegative() || index.getActiveBits() > 64 ||
        index.getZExtValue() > UINT64_MAX - offset)
      return std::nullopt;
    offset += index.getZExtValue();
    pointer = gep.getBase();
  }
  auto global = pointer.getDefiningOp<LLVM::AddressOfOp>();
  if (!global)
    return std::nullopt;
  return Address{global.getGlobalName(), offset};
}

bool zero(Value value) {
  APInt constant;
  return matchPattern(value, m_ConstantInt(&constant)) && constant.isZero();
}

Value constant(OpBuilder &builder, Location loc, uint64_t value) {
  return llvmConstant(builder, loc, builder.getI64Type(), value);
}
Value load(OpBuilder &builder, Location loc, Value frame, uint64_t offset) {
  return LLVM::LoadOp::create(builder, loc, builder.getI64Type(),
                              byteGEP(builder, loc, frame, offset), 8);
}
void store(OpBuilder &builder, Location loc, Value frame, uint64_t offset,
           Value value) {
  LLVM::StoreOp::create(builder, loc, value,
                        byteGEP(builder, loc, frame, offset), 8);
}

uint64_t field(const PendingRoot &root, unsigned lane) {
  return root.payloadOffset + (lane - (root.twoState && lane == 2)) * 8;
}

Value pendingBits(OpBuilder &builder, Location loc, Value frame,
                  ArrayRef<PendingWord> words) {
  Value bits = constant(builder, loc, 0);
  for (const auto &word : words)
    bits = LLVM::OrOp::create(builder, loc, bits,
                              load(builder, loc, frame, word.offset));
  return bits;
}

Value selected(OpBuilder &builder, Location loc, Value bits,
               const PendingRoot &root, unsigned destination) {
  if (destination == root.root % 64)
    return LLVM::AndOp::create(
        builder, loc, bits, constant(builder, loc, uint64_t{1} << destination));
  Value bit = LLVM::AndOp::create(
      builder, loc,
      LLVM::LShrOp::create(builder, loc, bits,
                           constant(builder, loc, root.root % 64)),
      constant(builder, loc, 1));
  return destination ? LLVM::ShlOp::create(builder, loc, bit,
                                           constant(builder, loc, destination))
                           .getResult()
                     : bit;
}

// Export/adopt at a real execution cut, preserving captured operands rather
// than replaying producers (LRM 4.6(b), 4.9.4, 10.4.2). Packed CURRENT state
// remains handled by its existing publication proof; this is PENDING state.
void transfer(OpBuilder &builder, Location loc, Value frame,
              ArrayRef<PendingRoot> roots, ArrayRef<PendingWord> words,
              const llvm::SmallBitVector &mask, bool exporting, bool writable) {
  // Each sparse word has its own empty fast path. Preserve source bit
  // positions and transfer only this function's closure, without imposing a
  // model-wide root count or permuting the logical readiness index.
  for (const auto &word : words) {
    uint64_t scope = 0;
    for (unsigned ordinal : word.roots) {
      const auto &root = roots[ordinal];
      if (mask.test(root.ordinal))
        scope |= uint64_t{1} << (root.root % 64);
    }
    if (!scope)
      continue;
    Value available;
    if (exporting) {
      available = load(builder, loc, frame, word.offset);
    } else {
      Value dirty = LLVM::AddressOfOp::create(
          builder, loc, LLVM::LLVMPointerType::get(builder.getContext()),
          dirtyName);
      available = LLVM::LoadOp::create(
          builder, loc, builder.getI64Type(),
          byteGEP(builder, loc, dirty, uint64_t{word.canonicalWord} * 8), 8);
    }
    Value any =
        LLVM::ICmpOp::create(builder, loc, LLVM::ICmpPredicate::ne,
                             LLVM::AndOp::create(builder, loc, available,
                                                 constant(builder, loc, scope)),
                             constant(builder, loc, 0));
    Block *entry = builder.getInsertionBlock();
    Block *done = entry->splitBlock(builder.getInsertionPoint());
    Block *body = new Block;
    entry->getParent()->getBlocks().insert(Region::iterator(done), body);
    builder.setInsertionPointToEnd(entry);
    LLVM::CondBrOp::create(builder, loc, any, body, ValueRange{}, done,
                           ValueRange{});
    builder.setInsertionPointToStart(body);
    for (unsigned ordinal : word.roots) {
      const auto &root = roots[ordinal];
      if (!mask.test(root.ordinal))
        continue;
      Value dirty = LLVM::AddressOfOp::create(
          builder, loc, LLVM::LLVMPointerType::get(builder.getContext()),
          dirtyName);
      Value leaf = byteGEP(builder, loc, dirty, uint64_t{root.root / 64} * 8);
      Value bits = exporting ? load(builder, loc, frame, word.offset)
                             : LLVM::LoadOp::create(
                                   builder, loc, builder.getI64Type(), leaf, 8)
                                   .getResult();
      unsigned bit = root.root % 64;
      Value present = LLVM::ICmpOp::create(
          builder, loc, LLVM::ICmpPredicate::ne,
          LLVM::AndOp::create(builder, loc, bits,
                              constant(builder, loc, uint64_t{1} << bit)),
          constant(builder, loc, 0));
      Block *head = builder.getInsertionBlock();
      Block *tail = head->splitBlock(builder.getInsertionPoint());
      Block *copy = new Block;
      head->getParent()->getBlocks().insert(Region::iterator(tail), copy);
      builder.setInsertionPointToEnd(head);
      LLVM::CondBrOp::create(builder, loc, present, copy, ValueRange{}, tail,
                             ValueRange{});
      builder.setInsertionPointToStart(copy);
      Value accumulator = LLVM::AddressOfOp::create(
          builder, loc, LLVM::LLVMPointerType::get(builder.getContext()),
          root.accumulator);
      if (!exporting) {
        Value contextAddress = LLVM::AddressOfOp::create(
            builder, loc, LLVM::LLVMPointerType::get(builder.getContext()),
            "__obelisk_current_context");
        Value runtimeContext = LLVM::LoadOp::create(
            builder, loc, LLVM::LLVMPointerType::get(builder.getContext()),
            contextAddress, 8);
        Value allowed =
            LLVM::CallOp::create(
                builder, loc, TypeRange{builder.getI32Type()},
                writable ? "obelisk_rt_v1_static_nba_specialization_guard_write"
                         : "obelisk_rt_v1_static_nba_specialization_guard",
                ValueRange{runtimeContext,
                           llvmConstant(builder, loc, builder.getI32Type(),
                                        root.root)})
                .getResult();
        Value valid = LLVM::LoadOp::create(
            builder, loc, builder.getI32Type(),
            nbaFieldGEP(
                builder, loc, accumulator,
                offsetof(obelisk_rt_generated_nba_accumulator_256, valid)),
            4);
        Value generated = LLVM::ICmpOp::create(
            builder, loc, LLVM::ICmpPredicate::ne,
            LLVM::AndOp::create(builder, loc, allowed, valid),
            llvmConstant(builder, loc, builder.getI32Type(), 0));
        Value region = LLVM::LoadOp::create(
            builder, loc, builder.getI32Type(),
            nbaFieldGEP(builder, loc, accumulator,
                        offsetof(obelisk_rt_generated_nba_accumulator_256,
                                 exec_region)),
            4);
        Value capturedMask = LLVM::LoadOp::create(
            builder, loc, builder.getI64Type(),
            nbaFieldGEP(
                builder, loc, accumulator,
                offsetof(obelisk_rt_generated_nba_accumulator_256, write_mask)),
            8);
        generated = LLVM::AndOp::create(
            builder, loc, generated,
            LLVM::AndOp::create(
                builder, loc,
                LLVM::ICmpOp::create(
                    builder, loc, LLVM::ICmpPredicate::eq, region,
                    llvmConstant(builder, loc, builder.getI32Type(),
                                 root.region)),
                LLVM::ICmpOp::create(builder, loc, LLVM::ICmpPredicate::eq,
                                     capturedMask,
                                     constant(builder, loc, root.mask))));
        // The leaf bit also covers runtime/ordered captures. Leave those forms
        // addressable for the common NBA prefix; importing their bit alone
        // would consume work without its captured payload (LRM 4.9.4/10.4.2).
        Block *adopt = new Block;
        copy->getParent()->getBlocks().insert(Region::iterator(tail), adopt);
        LLVM::CondBrOp::create(builder, loc, generated, adopt, ValueRange{},
                               tail, ValueRange{});
        builder.setInsertionPointToStart(adopt);
      }
      constexpr uint64_t offsets[] = {
          offsetof(obelisk_rt_generated_nba_accumulator_256, value),
          offsetof(obelisk_rt_generated_nba_accumulator_256, unknown),
          offsetof(obelisk_rt_generated_nba_accumulator_256, transient)};
      for (unsigned lane = 0; lane != 3; ++lane) {
        if ((lane == 1 && root.twoState) || (lane == 2 && !root.transient))
          continue;
        Value canonical = nbaFieldGEP(builder, loc, accumulator, offsets[lane]);
        if (exporting)
          LLVM::StoreOp::create(builder, loc,
                                load(builder, loc, frame, field(root, lane)),
                                canonical, 8);
        else
          store(builder, loc, frame, field(root, lane),
                LLVM::LoadOp::create(builder, loc, builder.getI64Type(),
                                     canonical, 8));
      }
      if (exporting) {
        LLVM::StoreOp::create(
            builder, loc, constant(builder, loc, root.mask),
            nbaFieldGEP(
                builder, loc, accumulator,
                offsetof(obelisk_rt_generated_nba_accumulator_256, write_mask)),
            8);
        LLVM::StoreOp::create(
            builder, loc, llvmConstant(builder, loc, builder.getI32Type(), 1),
            nbaFieldGEP(
                builder, loc, accumulator,
                offsetof(obelisk_rt_generated_nba_accumulator_256, valid)),
            4);
        LLVM::StoreOp::create(
            builder, loc,
            llvmConstant(builder, loc, builder.getI32Type(), root.region),
            nbaFieldGEP(builder, loc, accumulator,
                        offsetof(obelisk_rt_generated_nba_accumulator_256,
                                 exec_region)),
            4);
        Value old =
            LLVM::LoadOp::create(builder, loc, builder.getI64Type(), leaf, 8);
        LLVM::StoreOp::create(
            builder, loc,
            LLVM::OrOp::create(
                builder, loc, old,
                constant(builder, loc, uint64_t{1} << (root.root % 64))),
            leaf, 8);
        Value summary = LLVM::AddressOfOp::create(
            builder, loc, LLVM::LLVMPointerType::get(builder.getContext()),
            summaryName);
        Value page =
            byteGEP(builder, loc, summary, uint64_t{root.root / 4096} * 8);
        Value oldPage =
            LLVM::LoadOp::create(builder, loc, builder.getI64Type(), page, 8);
        LLVM::StoreOp::create(
            builder, loc,
            LLVM::OrOp::create(
                builder, loc, oldPage,
                constant(builder, loc, uint64_t{1} << ((root.root / 64) % 64))),
            page, 8);
        store(
            builder, loc, frame, word.offset,
            LLVM::AndOp::create(builder, loc, bits,
                                constant(builder, loc, ~(uint64_t{1} << bit))));
        if (root.transient)
          store(builder, loc, frame, field(root, 2), constant(builder, loc, 0));
      } else {
        // Adoption transfers ownership, not just the index bit. Runtime
        // snapshots and generated knownness predicates also inspect validity
        // directly. A stale canonical valid/mask could republish this capture
        // after the private consumer commits it (LRM 4.9.4/10.4.2).
        LLVM::StoreOp::create(
            builder, loc, llvmConstant(builder, loc, builder.getI32Type(), 0),
            nbaFieldGEP(
                builder, loc, accumulator,
                offsetof(obelisk_rt_generated_nba_accumulator_256, valid)),
            4);
        LLVM::StoreOp::create(
            builder, loc, constant(builder, loc, 0),
            nbaFieldGEP(
                builder, loc, accumulator,
                offsetof(obelisk_rt_generated_nba_accumulator_256, write_mask)),
            8);
        if (root.transient)
          LLVM::StoreOp::create(
              builder, loc, constant(builder, loc, 0),
              nbaFieldGEP(builder, loc, accumulator,
                          offsetof(obelisk_rt_generated_nba_accumulator_256,
                                   transient)),
              8);
        LLVM::StoreOp::create(
            builder, loc,
            LLVM::AndOp::create(
                builder, loc, bits,
                constant(builder, loc, ~(uint64_t{1} << (root.root % 64)))),
            leaf, 8);
        store(builder, loc, frame, word.offset,
              LLVM::OrOp::create(builder, loc,
                                 load(builder, loc, frame, word.offset),
                                 constant(builder, loc, uint64_t{1} << bit)));
      }
      LLVM::BrOp::create(builder, loc, ValueRange{}, tail);
      builder.setInsertionPointToStart(tail);
    }
    LLVM::BrOp::create(builder, loc, ValueRange{}, done);
    builder.setInsertionPointToStart(done);
  }
}

ArrayAttr extendArgAttrs(MLIRContext *context, ArrayAttr original, unsigned n) {
  SmallVector<Attribute> attrs;
  if (original)
    llvm::append_range(attrs, original.getValue());
  attrs.resize(n, DictionaryAttr::get(context));
  // Generated cores use this activation-local pointer only for frame accesses
  // and forwarding to other cores. Opaque cuts receive canonical captured
  // operands. Publish the nonescaping contract across LLVM partitions so those
  // calls do not force private readiness/payload words back into memory.
  attrs.push_back(DictionaryAttr::get(
      context,
      {NamedAttribute(
           StringAttr::get(context, LLVM::LLVMDialect::getNoAliasAttrName()),
           UnitAttr::get(context)),
       NamedAttribute(
           StringAttr::get(context, LLVM::LLVMDialect::getNoCaptureAttrName()),
           UnitAttr::get(context))}));
  return ArrayAttr::get(context, attrs);
}

// Calls that cannot inspect/stage pending NBA captures. The source writer
// inventory separately excludes runtime Active writers of localized roots.
bool pendingRuntimeTransparent(LLVM::CallOp call,
                               const DenseMap<uint32_t, unsigned> &byRoot) {
  auto callee = call.getCallee();
  if (!callee)
    return false;
  if (*callee == "obelisk_rt_v1_scheduler_prepare_eval_nba_local" ||
      *callee == "obelisk_rt_v1_scheduler_prepare_eval_nba_local_write" ||
      *callee == "obelisk_rt_v1_scheduler_complete_eval_nba" ||
      *callee == "obelisk_rt_v1_static_nba_direct_commit_guard" ||
      *callee == "obelisk_rt_v1_static_nba_account_generated_commits" ||
      *callee == "obelisk_rt_v1_static_nba_batch_guard" ||
      *callee == "obelisk_rt_v1_static_nba_batch_knownness" ||
      *callee == "obelisk_rt_v1_static_nba_batch_finish" ||
      *callee == "obelisk_rt_v1_scheduler_priority_signal_pending" ||
      *callee == "obelisk_rt_v1_scheduler_handoff_pending" ||
      *callee == "obelisk_rt_v1_scheduler_termination_requested" ||
      *callee == "obelisk_rt_v1_scheduler_time" ||
      *callee == "obelisk_rt_v1_scheduler_fail" ||
      // These leaves allocate only the separate ordered queue or update
      // proof/coverage metadata. None consumes generated NBA accumulators,
      // executes an actor or invokes foreign code (LRM 4.9.4, 10.4.2).
      *callee == "obelisk_rt_v1_eval_nba_reserve" ||
      *callee == "obelisk_rt_v1_native_promotion_invalidate_ranges" ||
      *callee == "obelisk_rt_v1_native_promotion_recheck_ranges" ||
      *callee == "obelisk_rt_v1_native_promotion_invalidate_buckets" ||
      *callee == "obelisk_rt_v1_native_promotion_recheck_buckets" ||
      // The unknown summary's rescan reads CURRENT unknown planes, which
      // pending NBAs do not touch, and only changes summary bits.
      *callee == "obelisk_rt_v1_native_state_refresh_unknown_summary" ||
      *callee == "obelisk_rt_v1_native_state_mark_unknown" ||
      *callee == "obelisk_rt_v1_coverage_point_hit" ||
      *callee == "obelisk_rt_v1_strength_resolve_kind" ||
      // eval_display formats its already captured arguments and writes to
      // runtime-owned FILE streams. It neither reads pending accumulators nor
      // calls the scheduler/VPI. Source CURRENT reads stay at their original
      // evaluation position; no NBA is committed for display (LRM 4.9.4).
      *callee == "obelisk_rt_v1_eval_display" ||
      // An ordered prefix applies its explicit captured operands to CURRENT.
      // It does not consume the separate pending generated accumulators.
      // Keep its source-order publications, without exporting unrelated next
      // values before each prefix update (LRM 4.6(b), 10.4.2).
      *callee == "obelisk_rt_v1_static_nba_commit_ordered" ||
      *callee == "obelisk_rt_v1_static_specialization_guard" ||
      *callee == "obelisk_rt_v1_static_nba_specialization_guard" ||
      *callee == "obelisk_rt_v1_static_nba_specialization_guard_write" ||
      callee->starts_with("llvm."))
    return true;
  if (*callee == "obelisk_rt_v1_scheduler_static_transition_owned" &&
      call.getArgOperands().size() == 9 &&
      call.getArgOperands()[1].getDefiningOp<LLVM::ConstantOp>() &&
      call.getArgOperands()[2].getDefiningOp<LLVM::ConstantOp>())
    // The verified publication bridge delivers waits but cannot execute a
    // waiting actor or consume pending captures. CURRENT publication still
    // occurs at this call (LRM 4.6(a), 9.4.2).
    return true;
  if ((*callee == "obelisk_rt_v1_static_nba_claim" ||
       *callee == "obelisk_rt_v1_static_nba_stage_wide") &&
      call.getArgOperands().size() >= 2) {
    APInt index;
    // These ABIs validate and stage only the explicit root. Staging an
    // unrelated root does not expose this transaction's private captures.
    return matchPattern(call.getArgOperands()[1], m_ConstantInt(&index)) &&
           index.getActiveBits() <= 32 &&
           !byRoot.contains(index.getZExtValue());
  }
  if (*callee == "obelisk_rt_v1_scheduler_static_nba")
    if (auto root =
            schedule::get<schedule::Field::EvalPendingRuntimeRoot>(call))
      return !byRoot.contains(root.getValue().getZExtValue());
  return false;
}

} // namespace

LogicalResult materializeNativeEvalTransactions(ModuleOp module) {
  auto certificate =
      schedule::get<schedule::Field::EvalTransactionRoots>(module);
  if (!certificate ||
      !schedule::has<schedule::Field::EvalRuntimeCalendar>(module))
    return success();
  MLIRContext *context = module.getContext();
  llvm::StringMap<LLVM::LLVMFuncOp> functions;
  bool writable = false;
  llvm::SetVector<Operation *> reachable;
  SmallVector<LLVM::LLVMFuncOp> work;
  for (auto function : module.getOps<LLVM::LLVMFuncOp>()) {
    functions.try_emplace(function.getSymName(), function);
    writable |=
        function.getSymName().starts_with("__obelisk_eval_write_guard_");
    if (schedule::has<schedule::metadata::evalCallClosureRoot>(function))
      work.push_back(function);
  }
  while (!work.empty()) {
    auto function = work.pop_back_val();
    if (function.isExternal() || !reachable.insert(function.getOperation()))
      continue;
    function.walk([&](LLVM::CallOp call) {
      if (call.getCallee())
        if (auto callee = functions.lookup(*call.getCallee()))
          work.push_back(callee);
    });
  }
  DenseMap<uint32_t, CaptureInventory> captures;
  DenseMap<uint32_t, std::string> commitConsumers;
  llvm::StringMap<MemoryInventory> memoryInventory;
  for (Operation *operation : reachable) {
    // Inventory every use of an accumulator address, not just recognizable
    // loads/stores. A cast, CFG argument or memory intrinsic can hide an alias
    // from address(). Localizing only the visible accesses would split capture
    // ownership and change the consumed payload (LRM 4.9.4/10.4.2).
    cast<LLVM::LLVMFuncOp>(operation).walk([&](LLVM::AddressOfOp address) {
      auto &memory = memoryInventory[address.getGlobalName()];
      SmallVector<Value> pointers{address.getResult()};
      DenseSet<Value> seen;
      while (!pointers.empty()) {
        Value pointer = pointers.pop_back_val();
        if (!seen.insert(pointer).second)
          continue;
        for (OpOperand &use : pointer.getUses()) {
          Operation *user = use.getOwner();
          if (auto gep = dyn_cast<LLVM::GEPOp>(user)) {
            if (gep.getBase() == pointer) {
              pointers.push_back(gep.getResult());
              continue;
            }
          } else if (auto load = dyn_cast<LLVM::LoadOp>(user)) {
            if (load.getAddr() == pointer)
              continue;
          } else if (auto store = dyn_cast<LLVM::StoreOp>(user)) {
            if (store.getAddr() == pointer && store.getValue() != pointer)
              continue;
          }
          memory.exact = false;
        }
      }
    });
    cast<LLVM::LLVMFuncOp>(operation).walk([&](Operation *nested) {
      if (auto runtime =
              schedule::get<schedule::Field::EvalPendingRuntimeRoot>(nested))
        captures[runtime.getValue().getZExtValue()].exact = false;
      if (auto spec =
              schedule::get<schedule::Field::EvalPendingStageSpec>(nested)) {
        auto data = spec.asArrayRef();
        if (data.size() != 6 || data[0] < 0 || data[0] > UINT32_MAX)
          return;
        auto &capture = captures[data[0]];
        if (capture.target.empty())
          capture.target.assign(data.begin(), data.begin() + 5);
        else
          capture.exact &= llvm::equal(capture.target, data.take_front(5));
        capture.sites.insert(static_cast<uint64_t>(data[5]));
        ++capture.stages;
      }
      auto read = dyn_cast<LLVM::LoadOp>(nested);
      auto write = dyn_cast<LLVM::StoreOp>(nested);
      if (read && read.getType().isInteger(32))
        if (auto root =
                schedule::get<schedule::Field::EvalPendingDirtyRoot>(read))
          if (auto at = address(read.getAddr());
              at &&
              at->offset ==
                  offsetof(obelisk_rt_generated_nba_accumulator_256, valid))
            commitConsumers.try_emplace(root.getValue().getZExtValue(),
                                        at->global.str());
      if (read || write) {
        Value pointer = read ? read.getAddr() : write.getAddr();
        StringRef name = baseGlobal(pointer);
        if (name.empty())
          return;
        auto &memory = memoryInventory[name];
        memory.seen = true;
        auto at = address(pointer);
        if (!at) {
          memory.exact = false;
          return;
        }
        Type type = read ? read.getType() : write.getValue().getType();
        memory.exact &=
            read ? !read.getVolatile_() &&
                       read.getOrdering() == LLVM::AtomicOrdering::not_atomic
                 : !write.getVolatile_() &&
                       write.getOrdering() == LLVM::AtomicOrdering::not_atomic;
        bool index = name == dirtyName || name == summaryName;
        bool word =
            index ? at->offset % 8 == 0
                  : at->offset ==
                            offsetof(obelisk_rt_generated_nba_accumulator_256,
                                     value) ||
                        at->offset ==
                            offsetof(obelisk_rt_generated_nba_accumulator_256,
                                     unknown) ||
                        at->offset ==
                            offsetof(obelisk_rt_generated_nba_accumulator_256,
                                     write_mask) ||
                        at->offset ==
                            offsetof(obelisk_rt_generated_nba_accumulator_256,
                                     transient);
        bool flag =
            !index &&
            (at->offset ==
                 offsetof(obelisk_rt_generated_nba_accumulator_256, valid) ||
             at->offset == offsetof(obelisk_rt_generated_nba_accumulator_256,
                                    exec_region));
        memory.exact &=
            (word && type.isInteger(64)) || (flag && type.isInteger(32));
      } else if (auto call = dyn_cast<LLVM::CallOp>(nested)) {
        // An accumulator passed as an address cannot be privatized without
        // an explicit capture interface for the receiving function.
        for (Value argument : call.getArgOperands())
          if (isa<LLVM::LLVMPointerType>(argument.getType()))
            if (StringRef name = baseGlobal(argument); !name.empty())
              memoryInventory[name].exact = false;
      }
    });
  }
  // Dynamic or escaping readiness addresses cannot be projected onto the
  // private sparse index. Retain one canonical owner rather than localizing
  // only the visible constant-offset queries (LRM 4.9.4/10.4.2).
  for (StringRef name : {StringRef(dirtyName), StringRef(summaryName)})
    if (auto memory = memoryInventory.find(name);
        memory != memoryInventory.end() && !memory->second.exact)
      return success();
  SmallVector<PendingRoot> candidates;
  for (Attribute raw : certificate) {
    auto record = dyn_cast<DictionaryAttr>(raw);
    auto info =
        record ? record.getAs<DenseI64ArrayAttr>("root") : DenseI64ArrayAttr{};
    auto name = record ? record.getAs<FlatSymbolRefAttr>("accumulator")
                       : FlatSymbolRefAttr{};
    auto sites =
        record ? record.getAs<DenseI64ArrayAttr>("sites") : DenseI64ArrayAttr{};
    if (!info || info.size() != 8 || !name || !sites)
      return module.emitError("invalid source eval transaction certificate");
    auto data = info.asArrayRef();
    if (data[1] < 0 || data[1] > UINT32_MAX || data[4] <= 0 || data[4] > 64 ||
        data[6] != OBELISK_RT_REGION_NBA)
      return module.emitError("unsupported source eval transaction interval");
    auto capture = captures.find(data[1]);
    auto memory = memoryInventory.find(name.getValue());
    auto consumer = commitConsumers.find(data[1]);
    if (capture == captures.end() || !capture->second.exact ||
        !capture->second.stages || memory == memoryInventory.end() ||
        !memory->second.seen || !memory->second.exact || sites.empty() ||
        consumer == commitConsumers.end() ||
        consumer->second != name.getValue())
      continue;
    // LRM 4.9.4/10.4.2: a private capture needs the typed generated consumer
    // that commits it at the common barrier. Source writer ownership alone
    // does not close an interval whose publication is runtime-owned. Such a
    // root would export at every NBA cut and retain the accumulator round trip.
    const auto &lowered = capture->second;
    if (lowered.target[1] != data[2] || lowered.target[2] != data[4] ||
        !lowered.target[3] || lowered.target[4] != data[6] ||
        (data[5] && lowered.target[3] != data[5]) ||
        lowered.sites.size() != sites.size() ||
        !llvm::all_of(sites.asArrayRef(), [&](int64_t site) {
          return lowered.sites.contains(static_cast<uint64_t>(site));
        }))
      continue;
    candidates.push_back({static_cast<uint32_t>(data[1]), 0,
                          static_cast<uint64_t>(lowered.target[3]),
                          static_cast<uint32_t>(data[6]), bool(data[7]),
                          name.getValue().str()});
  }
  // Admit the complete proved set. Scratch is proportional to admitted scalar
  // payloads, and wrappers/word transfers use each function's transitive scope.
  // The readiness layout is sparse in canonical words; gaps cost no storage.
  SmallVector<PendingRoot> roots = candidates;
  llvm::sort(roots,
             [](const auto &a, const auto &b) { return a.root < b.root; });
  if (roots.empty())
    return success();
  SmallVector<PendingWord> words;
  DenseMap<uint32_t, unsigned> byWord;
  llvm::StringMap<unsigned> byAccumulator;
  DenseMap<uint32_t, unsigned> byRoot;
  for (auto [ordinal, root] : llvm::enumerate(roots)) {
    root.ordinal = ordinal;
    uint32_t canonicalWord = root.root / 64;
    if (words.empty() || words.back().canonicalWord != canonicalWord) {
      byWord.try_emplace(canonicalWord, words.size());
      words.push_back({canonicalWord, words.size() * 8});
    }
    auto &word = words.back();
    root.bitmapOffset = word.offset;
    word.mask |= uint64_t{1} << (root.root % 64);
    word.roots.push_back(ordinal);
    byAccumulator.try_emplace(root.accumulator, root.ordinal);
    byRoot.try_emplace(root.root, root.ordinal);
  }
  uint64_t frameBytes = words.size() * 8;
  for (auto &root : roots) {
    root.twoState = bool(native::get<native::TwoState>(module));
    root.payloadOffset = frameBytes;
    frameBytes += (root.transient ? 24 : 16) - (root.twoState ? 8 : 0);
  }

  // Freeze the transitive pure-call inventory before moving bodies. Outlined
  // computation is not itself a publication cut; an opaque descendant is.
  DenseSet<Operation *> opaque;
  DenseMap<Operation *, SmallVector<Operation *>> pureCallers;
  llvm::StringMap<unsigned> externalCuts;
  for (Operation *operation : reachable) {
    auto function = cast<LLVM::LLVMFuncOp>(operation);
    if (schedule::has<schedule::Field::EvalStateMetadataOnly>(function))
      continue;
    function.walk([&](LLVM::CallOp call) {
      if (pendingRuntimeTransparent(call, byRoot))
        return;
      auto callee = call.getCallee() ? functions.lookup(*call.getCallee())
                                     : LLVM::LLVMFuncOp{};
      if (callee && reachable.contains(callee.getOperation()))
        pureCallers[callee.getOperation()].push_back(operation);
      else {
        opaque.insert(operation);
        ++externalCuts[call.getCallee().value_or("<indirect>")];
      }
    });
  }
  SmallVector<Operation *> opaqueWork(opaque.begin(), opaque.end());
  while (!opaqueWork.empty())
    for (Operation *caller : pureCallers[opaqueWork.pop_back_val()])
      if (opaque.insert(caller).second)
        opaqueWork.push_back(caller);
  if (native::has<native::DebugTiming>(module)) {
    SmallVector<StringRef> names;
    for (const auto &entry : externalCuts)
      names.push_back(entry.getKey());
    llvm::sort(names);
    for (StringRef name : names)
      llvm::errs() << "obelisk eval pending external cut: " << name
                   << " sites=" << externalCuts.lookup(name) << '\n';
  }

  DenseMap<Operation *, llvm::SmallBitVector> masks;
  DenseMap<Operation *, SmallVector<Operation *>> callers;
  for (Operation *operation : reachable) {
    auto function = cast<LLVM::LLVMFuncOp>(operation);
    auto &scope = masks.try_emplace(operation, roots.size()).first->second;
    // A metadata-only helper touches no pending root, so it is never cloned
    // and its convention need not be the default one.
    if (!schedule::has<schedule::Field::EvalStateMetadataOnly>(function) &&
        (function.isVarArg() || function.getCConv() != LLVM::CConv::C))
      return success();
    function.walk([&](Operation *nested) {
      for (auto role :
           {schedule::get<schedule::Field::EvalPendingDirtyRoot>(nested),
            schedule::get<schedule::Field::EvalPendingSummaryRoot>(nested)})
        if (role)
          if (auto root = byRoot.find(role.getValue().getZExtValue());
              root != byRoot.end())
            scope.set(root->second);
      if (auto call = dyn_cast<LLVM::CallOp>(nested)) {
        if (call.getCallee())
          if (auto callee = functions.lookup(*call.getCallee()))
            if (reachable.contains(callee.getOperation()))
              callers[callee.getOperation()].push_back(operation);
      }
      Value pointer;
      if (auto read = dyn_cast<LLVM::LoadOp>(nested))
        pointer = read.getAddr();
      else if (auto write = dyn_cast<LLVM::StoreOp>(nested))
        pointer = write.getAddr();
      if (pointer)
        if (auto at = address(pointer)) {
          if (auto root = byAccumulator.find(at->global);
              root != byAccumulator.end())
            scope.set(root->second);
          // Untagged logical index queries can select private captures even
          // when this function has no direct accumulator access. Tagged
          // producers for canonical roots keep their canonical RMW interface.
          if (schedule::has<schedule::Field::EvalPendingRuntimeRoot>(nested))
            return;
          if (at->global == dirtyName &&
              !schedule::has<schedule::Field::EvalPendingDirtyRoot>(nested)) {
            if (auto word = byWord.find(at->offset / 8); word != byWord.end())
              for (unsigned ordinal : words[word->second].roots)
                scope.set(ordinal);
          } else if (at->global == summaryName &&
                     !schedule::has<schedule::Field::EvalPendingSummaryRoot>(
                         nested)) {
            for (const auto &word : words)
              if (word.canonicalWord / 64 == at->offset / 8)
                for (unsigned ordinal : word.roots)
                  scope.set(ordinal);
          }
        }
    });
  }
  if (native::has<native::DebugTiming>(module)) {
    SmallVector<std::pair<unsigned, StringRef>> scopes;
    for (const auto &[operation, mask] : masks)
      if (mask.any())
        scopes.emplace_back(mask.count(),
                            cast<LLVM::LLVMFuncOp>(operation).getSymName());
    llvm::sort(scopes, [](auto a, auto b) {
      return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    for (auto [count, name] : ArrayRef(scopes).take_front(8))
      llvm::errs() << "obelisk eval pending direct scope: roots=" << count
                   << " function=" << name << '\n';
  }
  SmallVector<Operation *> pending;
  for (const auto &[operation, mask] : masks)
    if (mask.any())
      pending.push_back(operation);
  while (!pending.empty()) {
    Operation *callee = pending.pop_back_val();
    for (Operation *caller : callers[callee]) {
      llvm::SmallBitVector merged = masks[caller];
      merged |= masks[callee];
      if (merged != masks[caller]) {
        masks[caller] = merged;
        pending.push_back(caller);
      }
    }
  }

  // Changing an ABI must not silently discard a musttail/operand-bundle or
  // calling-convention contract. The appended pointer refers to caller-owned
  // storage, so even an ordinary tail-call promise is not valid for this ABI.
  for (Operation *operation : reachable) {
    bool supported = true;
    cast<LLVM::LLVMFuncOp>(operation).walk([&](LLVM::CallOp call) {
      auto callee = call.getCallee() ? functions.lookup(*call.getCallee())
                                     : LLVM::LLVMFuncOp{};
      if (callee && masks.lookup(callee.getOperation()).any())
        supported &=
            call.getCConv() == LLVM::CConv::C &&
            call.getTailCallKind() == LLVM::tailcallkind::TailCallKind::None &&
            call.getOpBundleSizes().empty();
    });
    if (!supported)
      return success();
  }

  SymbolTable symbols(module);
  DenseMap<Operation *, LLVM::LLVMFuncOp> cores;
  SmallVector<LLVM::LLVMFuncOp> originals;
  for (Operation *operation : reachable)
    if (masks.lookup(operation).any())
      originals.push_back(cast<LLVM::LLVMFuncOp>(operation));
  for (auto original : originals) {
    OpBuilder builder(original);
    SmallVector<Type> args(original.getArgumentTypes());
    args.push_back(LLVM::LLVMPointerType::get(context));
    auto core = LLVM::LLVMFuncOp::create(
        builder, original.getLoc(),
        (original.getSymName() + ".__obelisk_pending").str(),
        LLVM::LLVMFunctionType::get(original.getFunctionType().getReturnType(),
                                    args));
    symbols.insert(core);
    for (NamedAttribute attr : original->getAttrs())
      if (attr.getName() != "sym_name" && attr.getName() != "function_type" &&
          attr.getName() != "arg_attrs")
        core->setAttr(attr.getName(), attr.getValue());
    core.setLinkage(LLVM::Linkage::Internal);
    core.setArgAttrsAttr(extendArgAttrs(context, original.getArgAttrsAttr(),
                                        original.getNumArguments()));
    core.getBody().takeBody(original.getBody());
    core.getBody().front().addArgument(LLVM::LLVMPointerType::get(context),
                                       original.getLoc());
    cores.try_emplace(original.getOperation(), core);
  }

  llvm::SmallBitVector allRoots(roots.size(), true);
  for (auto original : originals) {
    auto core = cores.lookup(original.getOperation());
    Value frame = core.getBody().front().getArguments().back();
    SmallVector<Operation *> operations;
    core.walk([&](Operation *operation) { operations.push_back(operation); });
    for (Operation *operation : operations) {
      auto read = dyn_cast<LLVM::LoadOp>(operation);
      auto write = dyn_cast<LLVM::StoreOp>(operation);
      auto rootRole =
          schedule::get<schedule::Field::EvalPendingDirtyRoot>(operation);
      auto summaryRole =
          schedule::get<schedule::Field::EvalPendingSummaryRoot>(operation);
      bool canonicalIndex =
          schedule::has<schedule::Field::EvalPendingRuntimeRoot>(operation);
      OpBuilder builder(operation);
      Location loc = operation->getLoc();
      if (rootRole && byRoot.contains(rootRole.getValue().getZExtValue())) {
        const auto &root =
            roots[byRoot.lookup(rootRole.getValue().getZExtValue())];
        Value bits = load(builder, loc, frame, root.bitmapOffset);
        if (read) {
          Value pending = selected(builder, loc, bits, root, root.root % 64);
          if (read.getType().isInteger(32))
            pending = LLVM::ZExtOp::create(
                builder, loc, read.getType(),
                LLVM::ICmpOp::create(builder, loc, LLVM::ICmpPredicate::ne,
                                     pending, constant(builder, loc, 0)));
          read.replaceAllUsesWith(pending);
          read.erase();
        } else if (write) {
          store(builder, loc, frame, root.bitmapOffset,
                LLVM::OrOp::create(
                    builder, loc, bits,
                    constant(builder, loc, uint64_t{1} << (root.root % 64))));
          write.erase();
        }
        continue;
      }
      if (summaryRole && write &&
          byRoot.contains(summaryRole.getValue().getZExtValue())) {
        write.erase();
        continue;
      }
      if (read || write) {
        auto at = address(read ? read.getAddr() : write.getAddr());
        if (!at)
          continue;
        auto mapped = byAccumulator.find(at->global);
        if (mapped != byAccumulator.end()) {
          const auto &root = roots[mapped->second];
          std::optional<unsigned> lane;
          if (at->offset ==
              offsetof(obelisk_rt_generated_nba_accumulator_256, value))
            lane = 0;
          else if (at->offset ==
                   offsetof(obelisk_rt_generated_nba_accumulator_256, unknown))
            lane = 1;
          else if (at->offset ==
                   offsetof(obelisk_rt_generated_nba_accumulator_256,
                            transient))
            lane = 2;
          if (lane) {
            if ((*lane == 1 && root.twoState) ||
                (*lane == 2 && !root.transient)) {
              if (read) {
                read.replaceAllUsesWith(constant(builder, loc, 0));
                read.erase();
              } else {
                write.erase();
              }
              continue;
            }
            Value local = byteGEP(builder, loc, frame, field(root, *lane));
            if (read) {
              read.getAddrMutable().assign(local);
              if (*lane < 2 && root.mask != UINT64_MAX) {
                // Every certified capture writes this same mask. Bits
                // outside it are not a logical pending value: commit blends
                // them from CURRENT. Make that interface explicit so a
                // partial stage need not preserve unused accumulator bits
                // across outlined calls (LRM 4.9.4/10.4.2).
                builder.setInsertionPointAfter(read);
                auto masked =
                    LLVM::AndOp::create(builder, loc, read.getResult(),
                                        constant(builder, loc, root.mask));
                read.getResult().replaceAllUsesExcept(masked, masked);
              }
            } else {
              write.getAddrMutable().assign(local);
              if (*lane < 2 && root.mask != UINT64_MAX)
                write.getValueMutable().assign(
                    LLVM::AndOp::create(builder, loc, write.getValue(),
                                        constant(builder, loc, root.mask)));
            }
          } else if (write) {
            if (at->offset ==
                    offsetof(obelisk_rt_generated_nba_accumulator_256, valid) &&
                zero(write.getValue())) {
              // Clearing captured validity consumes this local NBA, even
              // when a private batch clears its leaf word only afterward.
              // Do not republish an already committed capture at the next
              // opaque cut (LRM 4.9.4/10.4.2).
              store(builder, loc, frame, root.bitmapOffset,
                    LLVM::AndOp::create(
                        builder, loc,
                        load(builder, loc, frame, root.bitmapOffset),
                        constant(builder, loc,
                                 ~(uint64_t{1} << (root.root % 64)))));
            }
            write.erase();
          } else {
            Value replacement;
            if (at->offset == offsetof(obelisk_rt_generated_nba_accumulator_256,
                                       write_mask)) {
              if (schedule::has<schedule::Field::EvalPendingMaskAtCommit>(
                      read)) {
                replacement = constant(builder, loc, root.mask);
              } else {
                Value staged = selected(
                    builder, loc, load(builder, loc, frame, root.bitmapOffset),
                    root, 0);
                replacement = LLVM::SelectOp::create(
                    builder, loc,
                    LLVM::ICmpOp::create(builder, loc, LLVM::ICmpPredicate::ne,
                                         staged, constant(builder, loc, 0)),
                    constant(builder, loc, root.mask),
                    constant(builder, loc, 0));
              }
            } else if (at->offset ==
                       offsetof(obelisk_rt_generated_nba_accumulator_256,
                                valid)) {
              // A caller can inspect validity outside the commit's selected
              // root branch (for example a knownness predicate). Preserve
              // the actual capture bit there too; the existence of a typed
              // consumer does not prove that this barrier staged the root
              // (LRM 4.9.4/10.4.2).
              replacement = LLVM::ZExtOp::create(
                  builder, loc, read.getType(),
                  LLVM::ICmpOp::create(
                      builder, loc, LLVM::ICmpPredicate::ne,
                      selected(builder, loc,
                               load(builder, loc, frame, root.bitmapOffset),
                               root, 0),
                      constant(builder, loc, 0)));
            } else {
              replacement = llvmConstant(
                  builder, loc, read.getType(),
                  at->offset ==
                          offsetof(obelisk_rt_generated_nba_accumulator_256,
                                   exec_region)
                      ? root.region
                      : 1);
            }
            read.replaceAllUsesWith(replacement);
            read.erase();
          }
          continue;
        }
        if (at->global == dirtyName && at->offset % 8 == 0 && !rootRole &&
            !canonicalIndex) {
          auto mappedWord = byWord.find(at->offset / 8);
          if (mappedWord == byWord.end())
            continue;
          const auto &word = words[mappedWord->second];
          uint64_t localMask = word.mask;
          if (write && zero(write.getValue())) {
            store(builder, loc, frame, word.offset,
                  LLVM::AndOp::create(builder, loc,
                                      load(builder, loc, frame, word.offset),
                                      constant(builder, loc, ~localMask)));
          } else if (write) {
            // Reads use the complete logical index, including private bits.
            // That value may reach an update through CFG block arguments or
            // be shared by a selection and a read/modify/write. Project at
            // the write, rather than suppressing private bits at the read.
            // An untagged update cannot create a canonical capture for a
            // localized root: its producers use the explicit root role above
            // or an opaque captured-value ABI. Preserve canonical bits only
            // when the old canonical index and this update both retain them
            // (LRM 4.9.4/10.4.2: one owner and one consumption per capture).
            Value prior = LLVM::LoadOp::create(
                builder, loc, builder.getI64Type(), write.getAddr(), 8);
            Value canonical = LLVM::OrOp::create(
                builder, loc, prior, constant(builder, loc, ~localMask));
            write.getValueMutable().assign(
                LLVM::AndOp::create(builder, loc, write.getValue(), canonical));
          } else if (read) {
            OpBuilder after(read);
            after.setInsertionPointAfter(read);
            Value bits = load(after, loc, frame, word.offset);
            Value combined = LLVM::AndOp::create(
                after, loc, bits, constant(after, loc, localMask));
            Value old = read.getResult();
            auto merged = LLVM::OrOp::create(after, loc, old, combined);
            old.replaceAllUsesExcept(merged, merged);
          }
          continue;
        }
        if (at->global == summaryName && at->offset % 8 == 0 && !summaryRole &&
            !canonicalIndex) {
          Value privatePage = constant(builder, loc, 0);
          uint64_t localMask = 0;
          for (const auto &word : words) {
            if (word.canonicalWord / 64 != at->offset / 8)
              continue;
            uint64_t bit = uint64_t{1} << (word.canonicalWord % 64);
            localMask |= bit;
            Value bits;
            if (read) {
              bits = load(builder, loc, frame, word.offset);
            } else {
              // A logical page may contain private-only leaf words. The
              // canonical summary indexes canonical payloads only; testing
              // the canonical leaf also preserves mixed private/canonical
              // words (LRM 4.9.4/10.4.2: coherent ownership at both levels).
              Value dirty = LLVM::AddressOfOp::create(
                  builder, loc, LLVM::LLVMPointerType::get(context), dirtyName);
              bits =
                  load(builder, loc, dirty, uint64_t{word.canonicalWord} * 8);
            }
            Value present =
                LLVM::ICmpOp::create(builder, loc, LLVM::ICmpPredicate::ne,
                                     bits, constant(builder, loc, 0));
            privatePage = LLVM::OrOp::create(
                builder, loc, privatePage,
                LLVM::SelectOp::create(builder, loc, present,
                                       constant(builder, loc, bit),
                                       constant(builder, loc, 0)));
          }
          if (!localMask)
            continue;
          if (read) {
            builder.setInsertionPointAfter(read);
            auto merged =
                LLVM::OrOp::create(builder, loc, read.getResult(), privatePage);
            read.getResult().replaceAllUsesExcept(merged, merged);
          } else {
            Value canonical = LLVM::OrOp::create(
                builder, loc, privatePage, constant(builder, loc, ~localMask));
            write.getValueMutable().assign(
                LLVM::AndOp::create(builder, loc, write.getValue(), canonical));
          }
          continue;
        }
      }
      auto call = dyn_cast<LLVM::CallOp>(operation);
      if (!call)
        continue;
      auto callee = call.getCallee() ? functions.lookup(*call.getCallee())
                                     : LLVM::LLVMFuncOp{};
      if (callee && cores.contains(callee.getOperation())) {
        SmallVector<Value> args(call.getArgOperands());
        args.push_back(frame);
        auto replacement = LLVM::CallOp::create(
            builder, loc, call.getResultTypes(),
            cores.lookup(callee.getOperation()).getSymName(), args);
        if (call.getArgAttrsAttr())
          replacement.setArgAttrsAttr(extendArgAttrs(
              context, call.getArgAttrsAttr(), call.getArgOperands().size()));
        for (NamedAttribute attr : call->getAttrs())
          if (attr.getName() != "callee" && attr.getName() != "arg_attrs" &&
              attr.getName() != "var_callee_type" &&
              attr.getName() != "operandSegmentSizes" &&
              attr.getName() != "op_bundle_sizes")
            replacement->setAttr(attr.getName(), attr.getValue());
        call.replaceAllUsesWith(replacement.getResults());
        call.erase();
      } else if (call.getCallee() &&
                 *call.getCallee() ==
                     "obelisk_rt_v1_scheduler_prepare_eval_nba") {
        Value bits = pendingBits(builder, loc, frame, words);
        Value localRegion = LLVM::SelectOp::create(
            builder, loc,
            LLVM::ICmpOp::create(builder, loc, LLVM::ICmpPredicate::ne, bits,
                                 constant(builder, loc, 0)),
            llvmConstant(builder, loc, builder.getI32Type(),
                         OBELISK_RT_REGION_NBA),
            llvmConstant(builder, loc, builder.getI32Type(), UINT32_MAX));
        StringRef service =
            writable ? "obelisk_rt_v1_scheduler_prepare_eval_nba_local_write"
                     : "obelisk_rt_v1_scheduler_prepare_eval_nba_local";
        auto replacement = LLVM::CallOp::create(
            builder, loc, call.getResultTypes(), service,
            ValueRange{call.getArgOperands()[0], localRegion,
                       call.getArgOperands()[1]});
        call.replaceAllUsesWith(replacement.getResults());
        call.erase();
        // A successful MAX result cancels the transaction's pending NBA on
        // finish; never export it again after final procedures have run.
        // Do not read the out parameter on a handoff/error (LRM 4.5, 20.2).
        builder.setInsertionPointAfter(replacement);
        Value ok = LLVM::ICmpOp::create(
            builder, loc, LLVM::ICmpPredicate::eq, replacement.getResult(),
            llvmConstant(builder, loc, builder.getI32Type(), OBELISK_RT_OK));
        Block *head = builder.getInsertionBlock();
        Block *tail = head->splitBlock(builder.getInsertionPoint());
        Block *prepared = new Block;
        head->getParent()->getBlocks().insert(Region::iterator(tail), prepared);
        builder.setInsertionPointToEnd(head);
        LLVM::CondBrOp::create(builder, loc, ok, prepared, ValueRange{}, tail,
                               ValueRange{});
        builder.setInsertionPointToStart(prepared);
        Value barrier =
            LLVM::LoadOp::create(builder, loc, builder.getI32Type(),
                                 replacement.getArgOperands()[2], 4);
        Value ended = LLVM::ICmpOp::create(
            builder, loc, LLVM::ICmpPredicate::eq, barrier,
            llvmConstant(builder, loc, builder.getI32Type(), UINT32_MAX));
        for (const auto &word : words)
          store(builder, loc, frame, word.offset,
                LLVM::SelectOp::create(builder, loc, ended,
                                       constant(builder, loc, 0),
                                       load(builder, loc, frame, word.offset)));
        LLVM::BrOp::create(builder, loc, ValueRange{}, tail);
      } else if (!pendingRuntimeTransparent(call, byRoot) &&
                 (!callee || !reachable.contains(callee.getOperation()) ||
                  opaque.contains(callee.getOperation()))) {
        // Cold/fine/opaque execution retains the existing captured NBA ABI.
        // LRM 38.15/38.34: no private continuation is exposed to foreign code.
        transfer(builder, loc, frame, roots, words, allRoots, true, writable);
        builder.setInsertionPointAfter(call);
        transfer(builder, loc, frame, roots, words, allRoots, false, writable);
      }
    }

    // Keep every original actor/helper symbol and signature for fine/cold
    // ingress. Only generated calls use the extra private, nonescaping slot.
    OpBuilder builder(original);
    Location loc = original.getLoc();
    Block *entry = original.addEntryBlock(builder);
    builder.setInsertionPointToStart(entry);
    Value local =
        entryAlloca(builder, loc, builder.getI64Type(), frameBytes / 8, 8);
    for (const auto &word : words)
      store(builder, loc, local, word.offset, constant(builder, loc, 0));
    const auto &scope = masks[original.getOperation()];
    // Unrelated payloads never need initialization. A later opaque adoption
    // writes a payload before setting its bit. Initialize this closure's lanes
    // for predicates and partial-stage RMWs that execute without a capture.
    for (const auto &root : roots)
      if (scope.test(root.ordinal))
        for (unsigned lane = 0; lane != (root.transient ? 3u : 2u); ++lane)
          if (lane != 1 || !root.twoState)
            store(builder, loc, local, field(root, lane),
                  constant(builder, loc, 0));
    transfer(builder, loc, local, roots, words,
             masks.lookup(original.getOperation()), false, writable);
    SmallVector<Value> args(entry->getArguments());
    args.push_back(local);
    auto call = LLVM::CallOp::create(
        builder, loc,
        isa<LLVM::LLVMVoidType>(original.getFunctionType().getReturnType())
            ? TypeRange{}
            : TypeRange{original.getFunctionType().getReturnType()},
        core.getSymName(), args);
    transfer(builder, loc, local, roots, words,
             masks.lookup(original.getOperation()), true, writable);
    LLVM::ReturnOp::create(builder, loc, call.getResults());
  }
  getOrDeclareLLVMFunction(
      module,
      writable ? "obelisk_rt_v1_scheduler_prepare_eval_nba_local_write"
               : "obelisk_rt_v1_scheduler_prepare_eval_nba_local",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 32),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module,
      writable ? "obelisk_rt_v1_static_nba_specialization_guard_write"
               : "obelisk_rt_v1_static_nba_specialization_guard",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 32)});
  if (native::has<native::DebugTiming>(module))
    llvm::errs() << "obelisk eval local pending roots: " << roots.size()
                 << " cores=" << cores.size()
                 << " fixed-candidates=" << candidates.size()
                 << " bitmap-words=" << words.size()
                 << " frame-bytes=" << frameBytes << '\n';
  return success();
}

} // namespace obelisk::detail
