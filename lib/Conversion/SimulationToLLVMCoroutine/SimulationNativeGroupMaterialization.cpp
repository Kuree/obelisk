//===- SimulationNativeGroupMaterialization.cpp - Native group bodies ----===//

#include "NativeSymbolUses.h"
#include "SimulationAOTPlanning.h"
#include "SimulationEvalReadySet.h"
#include "SimulationToLLVMCoroutinePrivate.h"
#include "obelisk/Dialect/Schedule/ScheduleEnums.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"

#include "obelisk/Conversion/Passes.h"
#include "obelisk/Conversion/SimulationToLLVMCoroutine.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"

#include "mlir/Analysis/CallGraph.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/LLVMIR/Transforms/InlinerInterfaceImpl.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Threading.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/InliningUtils.h"
#include "mlir/Transforms/Mem2Reg.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/GenericIteratedDominanceFrontier.h"
#include "llvm/Support/raw_ostream.h"

#include <functional>
#include <map>

using namespace mlir;

namespace obelisk::detail {
namespace {

uint64_t nativeOperationCost(Operation *operation) {
  uint64_t capacity = 0;
  if (auto update = dyn_cast<schedule::NativeReadyUpdateOp>(operation))
    capacity = update.getCapacity();
  else if (auto commit = dyn_cast<schedule::NativeReadyCommitOp>(operation))
    capacity = commit.getCapacity();
  if (capacity) {
    runtime::ReadySetLayout layout(capacity);
    // Charge deferred leaf/index maintenance before inlining. Semantic ops
    // must not accidentally multiply the existing machine-IR growth budget.
    return 12 * layout.levels + (layout.hasCache() ? 8 : 0);
  }
  return isa<schedule::NativeReadyReadOp>(operation) ? 2 : 1;
}

std::optional<uint64_t> staticByteOffset(Value pointer) {
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
  return bytes;
}

using NativeByteFootprint = SmallVector<std::pair<uint64_t, uint64_t>, 2>;
struct NativeStateFootprint {
  bool exact = false;
  NativeByteFootprint value, unknown;
};

void normalizeNativeByteFootprint(NativeByteFootprint &ranges) {
  llvm::sort(ranges);
  NativeByteFootprint merged;
  for (auto range : ranges)
    if (!merged.empty() && range.first <= merged.back().second)
      merged.back().second = std::max(merged.back().second, range.second);
    else
      merged.push_back(range);
  ranges = std::move(merged);
}

// An outlined computation is not itself an observation boundary (LRM 4.3).
// Inventory its complete call closure without inlining it or changing the
// machine-IR growth budget. Unknown aliases and external calls remain cuts.
llvm::StringMap<NativeStateFootprint>
buildNativeStateFootprints(ModuleOp module,
                           const llvm::StringMap<Type> &globalTypes,
                           const llvm::StringSet<> &metadataOnly) {
  llvm::StringMap<LLVM::LLVMFuncOp> functions;
  for (auto function : module.getOps<LLVM::LLVMFuncOp>())
    functions.try_emplace(function.getSymName(), function);
  llvm::StringMap<NativeStateFootprint> results;
  llvm::StringSet<> visiting;
  std::function<NativeStateFootprint(StringRef)> visit = [&](StringRef name) {
    if (metadataOnly.contains(name))
      return NativeStateFootprint{true, {}, {}};
    if (auto found = results.find(name); found != results.end())
      return found->second;
    auto function = functions.lookup(name);
    if (!function || function.isExternal() || !visiting.insert(name).second)
      return NativeStateFootprint{};
    NativeStateFootprint result{true, {}, {}};
    auto memory = [&](Operation *operation, Value pointer,
                      std::optional<uint64_t> bytes) {
      Value root = pointer;
      while (auto gep = root.getDefiningOp<LLVM::GEPOp>())
        root = gep.getBase();
      if (root.getDefiningOp<LLVM::AllocaOp>())
        return;
      auto address = root.getDefiningOp<LLVM::AddressOfOp>();
      if (!address || !globalTypes.contains(address.getGlobalName())) {
        result.exact = false;
        return;
      }
      StringRef global = address.getGlobalName();
      if (global != "__obelisk_state_value" &&
          global != "__obelisk_state_unknown")
        return;
      if (!bytes) {
        result.exact = false;
        return;
      }
      auto offset = staticByteOffset(pointer);
      auto array = dyn_cast<LLVM::LLVMArrayType>(globalTypes.lookup(global));
      auto interval =
          schedule::get<schedule::Field::EvalStateAccessBytes>(operation);
      if (array && interval && interval.size() == 2 && interval[0] >= 0 &&
          interval[1] > interval[0]) {
        uint64_t begin = interval[0];
        uint64_t end = std::min(uint64_t(interval[1]), array.getNumElements());
        if (begin < end) {
          (global == "__obelisk_state_value" ? result.value : result.unknown)
              .emplace_back(begin, end);
          return;
        }
      }
      if (!offset || !array || *offset > array.getNumElements() ||
          *bytes > array.getNumElements() - *offset) {
        result.exact = false;
        return;
      }
      if (*bytes)
        (global == "__obelisk_state_value" ? result.value : result.unknown)
            .emplace_back(*offset, *offset + *bytes);
    };
    function.walk([&](Operation *operation) {
      if (!result.exact || operation == function.getOperation())
        return;
      if (auto call = dyn_cast<LLVM::CallOp>(operation)) {
        auto nested = call.getCallee() ? visit(*call.getCallee())
                                       : NativeStateFootprint{};
        result.exact &= nested.exact;
        llvm::append_range(result.value, nested.value);
        llvm::append_range(result.unknown, nested.unknown);
      } else if (auto load = dyn_cast<LLVM::LoadOp>(operation)) {
        auto type = dyn_cast<IntegerType>(load.getType());
        memory(operation, load.getAddr(),
               type && type.getWidth() % 8 == 0
                   ? std::optional<uint64_t>(type.getWidth() / 8)
                   : std::nullopt);
      } else if (auto store = dyn_cast<LLVM::StoreOp>(operation)) {
        auto type = dyn_cast<IntegerType>(store.getValue().getType());
        memory(operation, store.getAddr(),
               type && type.getWidth() % 8 == 0
                   ? std::optional<uint64_t>(type.getWidth() / 8)
                   : std::nullopt);
      } else if (isa<LLVM::MemcpyOp, LLVM::MemsetOp>(operation)) {
        Value length = isa<LLVM::MemcpyOp>(operation)
                           ? cast<LLVM::MemcpyOp>(operation).getLen()
                           : cast<LLVM::MemsetOp>(operation).getLen();
        APInt bytes;
        if (!matchPattern(length, m_ConstantInt(&bytes)) ||
            bytes.getActiveBits() > 64)
          result.exact = false;
        else if (auto copy = dyn_cast<LLVM::MemcpyOp>(operation)) {
          memory(operation, copy.getDst(), bytes.getZExtValue());
          memory(operation, copy.getSrc(), bytes.getZExtValue());
        } else
          memory(operation, cast<LLVM::MemsetOp>(operation).getDst(),
                 bytes.getZExtValue());
      } else if (!isa<LLVM::ReturnOp, LLVM::AllocaOp>(operation) &&
                 !isMemoryEffectFree(operation))
        result.exact = false;
    });
    normalizeNativeByteFootprint(result.value);
    normalizeNativeByteFootprint(result.unknown);
    visiting.erase(name);
    results.try_emplace(name, result);
    return result;
  };
  for (auto function : module.getOps<LLVM::LLVMFuncOp>())
    visit(function.getSymName());
  return results;
}

/// Keep the ready words in SSA between executor boundaries. This is a local
/// representation of the original pending activations, not a second queue.
/// Retained calls may publish, observe, or reenter: materialize before them
/// and acquire their result afterward. Returns publish even on rejection or
/// error, so a handoff never restarts completed work or loses pending work.
LogicalResult promoteGroupReadyWords(LLVM::LLVMFuncOp function,
                                     const llvm::StringMap<Type> &globalTypes,
                                     uint64_t &budget) {
  auto reference =
      ::obelisk::schedule::get<::obelisk::schedule::Field::EvalGroupIngress>(
          function);
  auto array = reference ? dyn_cast_or_null<LLVM::LLVMArrayType>(
                               globalTypes.lookup(reference.getValue()))
                         : LLVM::LLVMArrayType{};
  if (!array || !array.getElementType().isInteger(64))
    return success();

  std::map<uint64_t, SmallVector<Operation *>> accesses;
  SmallVector<Operation *> boundaries;
  SmallVector<Value> pointers;
  llvm::DenseSet<Value> visited;
  bool supported = true;
  function.walk([&](Operation *operation) {
    if (auto address = dyn_cast<LLVM::AddressOfOp>(operation);
        address && address.getGlobalName() == reference.getValue())
      pointers.push_back(address.getResult());
    if (isa<LLVM::CallOp, LLVM::ReturnOp>(operation))
      boundaries.push_back(operation);
    if (isa<LLVM::InvokeOp, LLVM::InlineAsmOp>(operation))
      supported = false;
  });
  while (!pointers.empty() && supported) {
    Value pointer = pointers.pop_back_val();
    if (!visited.insert(pointer).second)
      continue;
    for (OpOperand &use : pointer.getUses()) {
      Operation *operation = use.getOwner();
      if (auto gep = dyn_cast<LLVM::GEPOp>(operation)) {
        pointers.push_back(gep.getResult());
        continue;
      }
      auto load = dyn_cast<LLVM::LoadOp>(operation);
      auto store = dyn_cast<LLVM::StoreOp>(operation);
      bool plainLoad = load && load.getType().isInteger(64) &&
                       !load.getVolatile_() &&
                       load.getOrdering() == LLVM::AtomicOrdering::not_atomic;
      bool plainStore = store && store.getAddr() == pointer &&
                        store.getValue().getType().isInteger(64) &&
                        !store.getVolatile_() &&
                        store.getOrdering() == LLVM::AtomicOrdering::not_atomic;
      if (!plainLoad && !plainStore) {
        supported = false;
        break;
      }
      auto bytes = staticByteOffset(pointer);
      if (!bytes || *bytes % 8) {
        // Hierarchical ready lookup can select a word dynamically. Keep that
        // exact access on authoritative storage and treat it as a boundary.
        boundaries.push_back(operation);
      } else if (*bytes / 8 >= array.getNumElements()) {
        supported = false;
        break;
      } else {
        accesses[*bytes / 8].push_back(operation);
      }
    }
  }
  if (!supported || accesses.empty())
    return success();
  // Flush/acquire code is also charged to the existing group growth budget.
  // Avoid multiplying a large ready set by an unresolved helper call graph.
  uint64_t perWordCost = 4 + 6 * uint64_t(boundaries.size()) +
                         function.getBody().getBlocks().size();
  if (accesses.size() > budget / perWordCost)
    return success();
  budget -= accesses.size() * perWordCost;

  OpBuilder builder(function.getContext());
  builder.setInsertionPointToStart(&function.getBody().front());
  Location location = function.getLoc();
  Type i64 = builder.getI64Type();
  Value base = LLVM::AddressOfOp::create(
      builder, location, LLVM::LLVMPointerType::get(function.getContext()),
      reference.getValue());
  struct Word {
    Value address;
    Value slot;
  };
  SmallVector<Word> words;
  SmallVector<PromotableAllocationOpInterface> allocations;
  for (auto &[index, operations] : accesses) {
    Value address = byteGEP(builder, location, base, index * 8);
    Value slot = entryAlloca(builder, location, i64, 1, 8);
    allocations.push_back(
        cast<PromotableAllocationOpInterface>(slot.getDefiningOp()));
    Value initial = LLVM::LoadOp::create(builder, location, i64, address, 8);
    LLVM::StoreOp::create(builder, location, initial, slot, 8);
    words.push_back({address, slot});
    for (Operation *operation : operations) {
      if (auto load = dyn_cast<LLVM::LoadOp>(operation))
        load.getAddrMutable().assign(slot);
      else
        cast<LLVM::StoreOp>(operation).getAddrMutable().assign(slot);
    }
  }
  for (Operation *boundary : boundaries) {
    builder.setInsertionPoint(boundary);
    for (const Word &word : words)
      LLVM::StoreOp::create(
          builder, boundary->getLoc(),
          LLVM::LoadOp::create(builder, boundary->getLoc(), i64, word.slot, 8),
          word.address, 8);
    if (isa<LLVM::ReturnOp>(boundary))
      continue;
    builder.setInsertionPointAfter(boundary);
    for (const Word &word : words)
      LLVM::StoreOp::create(builder, boundary->getLoc(),
                            LLVM::LoadOp::create(builder, boundary->getLoc(),
                                                 i64, word.address, 8),
                            word.slot, 8);
  }
  DominanceInfo dominance(function);
  if (failed(tryToPromoteMemorySlots(allocations, builder,
                                     DataLayout::closest(function), dominance)))
    return function.emitError("could not materialize group ready words in SSA");
  ::obelisk::schedule::set<::obelisk::schedule::Field::EvalSsaReadyWords>(
      function, builder.getI64IntegerAttr(words.size()));
  return success();
}

/// Forward exact physical state ranges between certified actors. Canonical
/// state is acquired at entry, including its X/Z plane; no initialization or
/// value-domain proof is inferred here. Original transition computations and
/// invalidation hooks still execute at each logical store. Only its physical
/// publication is deferred until the next boundary (LRM 4.3, 4.6, 4.10).
/// IEEE 1800-2023 4.3 permits this representation only with the same visible
/// updates; 4.6(a) retains source statement order and 4.9.3 makes subsequent
/// blocking reads see the updated current value. Opaque calls materialize
/// both planes so VPI reads/writes (38.15, 38.34) observe coherent state.
LogicalResult
promoteGroupState(LLVM::LLVMFuncOp function,
                  const llvm::StringMap<Type> &globalTypes, StringRef name,
                  uint64_t &budget, const llvm::StringSet<> &metadataOnly,
                  const llvm::StringMap<NativeStateFootprint> &callFootprints) {
  auto array = dyn_cast_or_null<LLVM::LLVMArrayType>(globalTypes.lookup(name));
  if (!array || !array.getElementType().isInteger(8))
    return success();
  struct Range {
    Type type;
    SmallVector<Operation *> operations;
    llvm::SmallDenseSet<uint32_t, 2> owners;
    bool read = false;
    bool written = false;
    unsigned stores = 0;
  };
  bool activation = schedule::has<schedule::Field::EvalRawCaptures>(function);
  std::map<std::pair<uint64_t, uint64_t>, Range> ranges;
  llvm::SetVector<Operation *> boundaries;
  using Footprint = SmallVector<std::pair<uint64_t, uint64_t>, 2>;
  DenseMap<Operation *, Footprint> boundaryFootprints;
  DenseMap<Operation *, uint64_t> operationOrder;
  uint64_t nextOperation = 0;
  auto pointerFootprint = [&](Value pointer, uint64_t bytes,
                              Footprint &footprint) {
    Value root = pointer;
    while (auto gep = root.getDefiningOp<LLVM::GEPOp>())
      root = gep.getBase();
    if (root.getDefiningOp<LLVM::AllocaOp>())
      return true;
    auto address = root.getDefiningOp<LLVM::AddressOfOp>();
    if (!address || !globalTypes.contains(address.getGlobalName()))
      return false;
    if (address.getGlobalName() != name)
      return true;
    auto offset = staticByteOffset(pointer);
    if (!offset || *offset > array.getNumElements() ||
        bytes > array.getNumElements() - *offset)
      return false;
    if (bytes)
      footprint.emplace_back(*offset, *offset + bytes);
    return true;
  };
  bool supported = true;
  function.walk([&](Operation *operation) {
    operationOrder.try_emplace(operation, nextOperation++);
    if (operation == function.getOperation())
      return;
    if (auto call = dyn_cast<LLVM::CallOp>(operation)) {
      if (call.getCallee() && metadataOnly.contains(*call.getCallee()))
        return;
      if (call.getCallee())
        if (auto found = callFootprints.find(*call.getCallee());
            found != callFootprints.end() && found->second.exact) {
          auto &footprint = name == "__obelisk_state_value"
                                ? found->second.value
                                : found->second.unknown;
          if (!footprint.empty()) {
            boundaries.insert(operation);
            boundaryFootprints.try_emplace(operation, footprint);
          }
          return;
        }
      boundaries.insert(operation);
      return;
    }
    if (isa<LLVM::ReturnOp>(operation)) {
      boundaries.insert(operation);
      return;
    }
    if (isa<LLVM::InvokeOp, LLVM::InlineAsmOp>(operation))
      supported = false;
    auto load = dyn_cast<LLVM::LoadOp>(operation);
    auto store = dyn_cast<LLVM::StoreOp>(operation);
    if (!load && !store) {
      // Intrinsics and future memory operations must not silently bypass
      // publication. Unknown control effects cannot be resumed locally.
      // Local memory intrinsics cannot observe canonical state. Preserve
      // their original order, but do not flush unrelated state around them.
      if (isa<LLVM::MemsetOp, LLVM::MemcpyOp>(operation)) {
        Value length = isa<LLVM::MemsetOp>(operation)
                           ? cast<LLVM::MemsetOp>(operation).getLen()
                           : cast<LLVM::MemcpyOp>(operation).getLen();
        APInt bytes;
        Footprint footprint;
        bool exact = matchPattern(length, m_ConstantInt(&bytes)) &&
                     bytes.getActiveBits() <= 64;
        if (exact) {
          if (auto set = dyn_cast<LLVM::MemsetOp>(operation))
            exact =
                pointerFootprint(set.getDst(), bytes.getZExtValue(), footprint);
          else {
            auto copy = cast<LLVM::MemcpyOp>(operation);
            exact = pointerFootprint(copy.getDst(), bytes.getZExtValue(),
                                     footprint) &&
                    pointerFootprint(copy.getSrc(), bytes.getZExtValue(),
                                     footprint);
          }
        }
        if (!exact || !footprint.empty())
          boundaries.insert(operation);
        if (exact)
          boundaryFootprints.try_emplace(operation, std::move(footprint));
        return;
      }
      if (!isa<LLVM::AllocaOp>(operation) && !isMemoryEffectFree(operation)) {
        if (operation->getNumRegions() ||
            operation->hasTrait<OpTrait::IsTerminator>())
          supported = false;
        else
          boundaries.insert(operation);
      }
      return;
    }
    Value pointer = load ? load.getAddr() : store.getAddr();
    Value root = pointer;
    while (auto gep = root.getDefiningOp<LLVM::GEPOp>())
      root = gep.getBase();
    auto address = root.getDefiningOp<LLVM::AddressOfOp>();
    if (!address || !globalTypes.contains(address.getGlobalName())) {
      // A returned pointer or an indirect context access may alias canonical
      // state. Local allocation addresses are the only non-global exception.
      if (!root.getDefiningOp<LLVM::AllocaOp>())
        boundaries.insert(operation);
      return;
    }
    if (address.getGlobalName() != name)
      return;
    auto owner =
        ::obelisk::schedule::get<::obelisk::schedule::Field::EvalGroupOwner>(
            operation);
    auto type = dyn_cast<IntegerType>(load ? load.getType()
                                           : store.getValue().getType());
    auto offset = staticByteOffset(pointer);
    bool plain =
        load ? !load.getVolatile_() &&
                   load.getOrdering() == LLVM::AtomicOrdering::not_atomic
             : !store.getVolatile_() &&
                   store.getOrdering() == LLVM::AtomicOrdering::not_atomic;
    if ((!owner && !activation) || !plain || !type || type.getWidth() % 8 ||
        !offset) {
      boundaries.insert(operation);
      Footprint footprint;
      auto bytes =
          schedule::get<schedule::Field::EvalStateAccessBytes>(operation);
      if (bytes && bytes.size() == 2 && bytes[0] >= 0 && bytes[1] > bytes[0]) {
        uint64_t begin = bytes[0];
        uint64_t end = std::min(uint64_t(bytes[1]), array.getNumElements());
        if (begin < end)
          boundaryFootprints.try_emplace(operation, Footprint{{begin, end}});
      } else if (type && type.getWidth() % 8 == 0 && offset &&
                 pointerFootprint(pointer, type.getWidth() / 8, footprint))
        boundaryFootprints.try_emplace(operation, std::move(footprint));
      return;
    }
    uint64_t width = type.getWidth() / 8;
    if (*offset >= array.getNumElements() ||
        width > array.getNumElements() - *offset) {
      supported = false;
      return;
    }
    Range &range = ranges[{*offset, width}];
    range.type = type;
    range.operations.push_back(operation);
    if (owner)
      range.owners.insert(owner.getUInt());
    range.read |= bool(load);
    range.written |= bool(store);
    range.stores += bool(store);
  });
  // The global address must never escape into an opaque memory operation.
  // Calls are handled at boundaries, but storing the pointer or passing it
  // through control flow would hide later aliases from the access inventory.
  // Ptr-to-int arithmetic only observes the address (including the explicit
  // proof-publication offset), not the deferred payload. Any later int-to-ptr
  // memory access is an unknown alias and already forces a boundary above.
  SmallVector<Value> pending;
  llvm::DenseSet<Value> seen;
  function.walk([&](LLVM::AddressOfOp address) {
    if (address.getGlobalName() == name)
      pending.push_back(address.getResult());
  });
  while (!pending.empty() && supported) {
    Value pointer = pending.pop_back_val();
    if (!seen.insert(pointer).second)
      continue;
    for (OpOperand &use : pointer.getUses()) {
      Operation *operation = use.getOwner();
      if (auto gep = dyn_cast<LLVM::GEPOp>(operation))
        pending.push_back(gep.getResult());
      else if (auto store = dyn_cast<LLVM::StoreOp>(operation))
        supported &= store.getAddr() == pointer;
      else
        supported &= isa<LLVM::LoadOp, LLVM::CallOp, LLVM::MemcpyOp,
                         LLVM::MemsetOp, LLVM::PtrToIntOp>(operation);
    }
  }
  if (!supported || ranges.empty())
    return success();
  // A connected physical window has one value, even when logical accesses
  // have different widths. Keep overlapping byte slices together and express
  // their loads/stores as exact integer extracts/inserts. This never unions
  // unrelated copy-connected roots or includes bytes outside the window.
  std::optional<bool> littleEndian;
  if (auto layout =
          function->getParentOfType<ModuleOp>()->getAttrOfType<StringAttr>(
              "llvm.data_layout")) {
    auto parsed = llvm::DataLayout::parse(layout.getValue());
    if (!parsed) {
      llvm::consumeError(parsed.takeError());
      return function.emitError("invalid native state data layout");
    }
    littleEndian = parsed->isLittleEndian();
  }
  struct Window {
    uint64_t offset, end;
    SmallVector<Range *> ranges;
    llvm::SmallDenseSet<uint32_t, 2> owners;
    bool read = false, written = false;
    unsigned stores = 0;
    Block *scope = nullptr;
    Operation *first = nullptr, *last = nullptr;
    uint64_t beginOrder = UINT64_MAX, endOrder = 0;
  };
  SmallVector<Window> windows;
  for (auto &[key, range] : ranges) {
    if (windows.empty() || key.first >= windows.back().end)
      windows.push_back({key.first, key.first + key.second, {}});
    Window &window = windows.back();
    window.end = std::max(window.end, key.first + key.second);
    window.ranges.push_back(&range);
    window.owners.insert(range.owners.begin(), range.owners.end());
    window.read |= range.read;
    window.written |= range.written;
    window.stores += range.stores;
  }
  DominanceInfo dominance(function);
  for (Window &window : windows) {
    bool oneBlock = true;
    for (Range *range : window.ranges)
      for (Operation *operation : range->operations) {
        if (!window.scope)
          window.scope = operation->getBlock();
        oneBlock &= window.scope == operation->getBlock();
        uint64_t order = operationOrder.lookup(operation);
        if (order < window.beginOrder) {
          window.beginOrder = order;
          window.first = operation;
        }
        if (!window.last || order > window.endOrder) {
          window.endOrder = order;
          window.last = operation;
        }
      }
    if (!oneBlock) {
      window.scope = nullptr;
      window.first = window.last = nullptr;
    }
  }
  auto intersects = [&](Operation *boundary, const Window &window) {
    if (window.scope && (boundary->getBlock() != window.scope ||
                         operationOrder.lookup(boundary) < window.beginOrder ||
                         operationOrder.lookup(boundary) > window.endOrder))
      return false;
    auto found = boundaryFootprints.find(boundary);
    return found == boundaryFootprints.end() ||
           llvm::any_of(found->second, [&](const auto &range) {
             return window.offset < range.second && range.first < window.end;
           });
  };
  SmallVector<Window *> selected;
  for (Window &window : windows) {
    uint64_t width = window.end - window.offset;
    if (!window.read || !window.written || width > 32 ||
        (window.ranges.size() > 1 && !littleEndian) ||
        !(window.owners.size() > 1 || activation))
      continue;
    // Charge actual potential joins, not every block in an unrelated region.
    // A large evaluator may have thousands of blocks but only a few reaching
    // definitions for this window. The IDF bounds its SSA merge growth.
    llvm::SmallPtrSet<Block *, 32> definitions;
    definitions.insert(&function.getBody().front());
    for (Range *range : window.ranges)
      for (Operation *operation : range->operations)
        if (isa<LLVM::StoreOp>(operation))
          definitions.insert(operation->getBlock());
    uint64_t relevantBoundaries = 0;
    for (Operation *boundary : boundaries) {
      if (!intersects(boundary, window))
        continue;
      ++relevantBoundaries;
      if (!isa<LLVM::ReturnOp>(boundary))
        definitions.insert(boundary->getBlock());
    }
    // An activation-local lane with a single conditional write often gains
    // a longer live range and unconditional epilogue stores, without saving
    // any source store. Retain that canonical form unless accesses share one
    // block or repeated writes can amortize the real publication cuts.
    if (activation && window.owners.size() < 2 && !window.scope &&
        window.stores <= relevantBoundaries)
      continue;
    SmallVector<Block *> joins;
    if (!function.getBody().hasOneBlock()) {
      llvm::IDFCalculatorBase<Block, false> idf(
          dominance.getDomTree(&function.getBody()));
      idf.setDefiningBlocks(definitions);
      idf.calculate(joins);
    }
    uint64_t cost = 4 + 6 * relevantBoundaries + joins.size();
    for (Range *range : window.ranges)
      if (range->type != IntegerType::get(function.getContext(), width * 8))
        cost += 5 * range->operations.size();
    if (budget < cost)
      continue;
    budget -= cost;
    selected.push_back(&window);
  }
  if (selected.empty())
    return success();
  OpBuilder builder(function.getContext());
  builder.setInsertionPointToStart(&function.getBody().front());
  Location location = function.getLoc();
  Value base = LLVM::AddressOfOp::create(
      builder, location, LLVM::LLVMPointerType::get(function.getContext()),
      name);
  struct Slot {
    Value address;
    Value local;
    Type type;
    Window *window;
  };
  SmallVector<Slot> slots;
  SmallVector<PromotableAllocationOpInterface> allocations;
  for (Window *window : selected) {
    auto type = builder.getIntegerType((window->end - window->offset) * 8);
    Value address = byteGEP(builder, location, base, window->offset);
    Value local = entryAlloca(builder, location, type, 1, 1);
    allocations.push_back(
        cast<PromotableAllocationOpInterface>(local.getDefiningOp()));
    // IEEE 1800-2023 4.3, 4.6(a), 4.9.3: when every access is in one
    // block, acquire at its first access and publish after its last access.
    // This preserves source order and avoids touching state on a skipped
    // branch. Intervening observers still flush/reacquire below (38.15/38.34).
    if (window->scope)
      builder.setInsertionPoint(window->first);
    LLVM::StoreOp::create(
        builder, location,
        LLVM::LoadOp::create(builder, location, type, address, 1), local, 1);
    slots.push_back({address, local, type, window});
    for (Range *range : window->ranges)
      for (Operation *operation : range->operations) {
        builder.setInsertionPoint(operation);
        auto load = dyn_cast<LLVM::LoadOp>(operation);
        auto store = dyn_cast<LLVM::StoreOp>(operation);
        Value pointer = load ? load.getAddr() : store.getAddr();
        uint64_t byte = *staticByteOffset(pointer) - window->offset;
        unsigned bits = cast<IntegerType>(range->type).getWidth();
        unsigned shift = littleEndian.value_or(true)
                             ? byte * 8
                             : type.getWidth() - byte * 8 - bits;
        Value value;
        if (load) {
          value = LLVM::LoadOp::create(builder, operation->getLoc(), type,
                                       local, 1);
          if (shift)
            value = LLVM::LShrOp::create(
                builder, operation->getLoc(), value,
                llvmConstant(builder, operation->getLoc(), type, shift));
          if (bits != type.getWidth())
            value = LLVM::TruncOp::create(builder, operation->getLoc(),
                                          range->type, value);
          load.replaceAllUsesWith(value);
        } else {
          value = store.getValue();
          if (bits != type.getWidth()) {
            value =
                LLVM::ZExtOp::create(builder, operation->getLoc(), type, value);
            if (shift)
              value = LLVM::ShlOp::create(
                  builder, operation->getLoc(), value,
                  llvmConstant(builder, operation->getLoc(), type, shift));
            APInt keep =
                ~APInt::getBitsSet(type.getWidth(), shift, shift + bits);
            Value old = LLVM::LoadOp::create(builder, operation->getLoc(), type,
                                             local, 1);
            Value mask = LLVM::ConstantOp::create(builder, operation->getLoc(),
                                                  type, keep);
            value = LLVM::OrOp::create(
                builder, operation->getLoc(),
                LLVM::AndOp::create(builder, operation->getLoc(), old, mask),
                value);
          }
          LLVM::StoreOp::create(builder, operation->getLoc(), value, local, 1);
        }
        if (window->scope && operation == window->last)
          LLVM::StoreOp::create(builder, operation->getLoc(),
                                LLVM::LoadOp::create(builder,
                                                     operation->getLoc(), type,
                                                     local, 1),
                                address, 1);
        operation->erase();
      }
    builder.setInsertionPointAfter(base.getDefiningOp());
  }
  for (Operation *boundary : boundaries) {
    builder.setInsertionPoint(boundary);
    for (const Slot &slot : slots) {
      if (!intersects(boundary, *slot.window))
        continue;
      LLVM::StoreOp::create(builder, boundary->getLoc(),
                            LLVM::LoadOp::create(builder, boundary->getLoc(),
                                                 slot.type, slot.local, 1),
                            slot.address, 1);
    }
    if (isa<LLVM::ReturnOp>(boundary))
      continue;
    builder.setInsertionPointAfter(boundary);
    for (const Slot &slot : slots) {
      if (!intersects(boundary, *slot.window))
        continue;
      LLVM::StoreOp::create(builder, boundary->getLoc(),
                            LLVM::LoadOp::create(builder, boundary->getLoc(),
                                                 slot.type, slot.address, 1),
                            slot.local, 1);
    }
  }
  if (failed(tryToPromoteMemorySlots(allocations, builder,
                                     DataLayout::closest(function), dominance)))
    return function.emitError("could not materialize group state in SSA");
  auto attribute = name == "__obelisk_state_value"
                       ? ::obelisk::schedule::Field::EvalSsaValueRanges
                       : ::obelisk::schedule::Field::EvalSsaUnknownRanges;
  schedule::set(function, attribute, builder.getI64IntegerAttr(slots.size()));
  return success();
}

struct NativeEvalValueLane {
  StringRef plane;
  uint64_t begin, end;
  bool written = false;
  SmallVector<Operation *> accesses;
};

struct NativeEvalValueInterface {
  LLVM::LLVMFuncOp body;
  SmallVector<NativeEvalValueLane> lanes;
  Type result;
  bool returnsStatus = false;
};

// Only the already selected, closed computation is eligible. In particular,
// the existence of a two-state implementation does not select that route.
bool isNativeEvalValueCall(LLVM::CallOp call, LLVM::LLVMFuncOp callee) {
  return callee && !callee.isExternal() &&
         (schedule::has<schedule::Field::EvalGroupDomainSelected>(call) ||
          schedule::has<schedule::metadata::evalInfallible>(callee) ||
          schedule::has<schedule::Field::EvalRawCaptures>(callee) ||
          schedule::has<schedule::Field::EvalFourStateSource>(callee) ||
          schedule::has<schedule::Field::EvalConditionallyTwoState>(callee));
}

ArrayAttr extendNativeEvalArgumentAttrs(OpBuilder &builder, ArrayAttr original,
                                        size_t count) {
  if (!original)
    return {};
  SmallVector<Attribute> attrs(original.getValue());
  attrs.resize(count, builder.getDictionaryAttr({}));
  return builder.getArrayAttr(attrs);
}

// Preserve the cold canonical ABI, but move (rather than duplicate) its body
// into a value interface. The source activation/route certificate still owns
// execution; the exact complete memory inventory proves only representation.
// IEEE 1800-2023 4.3 permits this representation change, while 4.6(a) and
// 4.9.3 require every blocking update and its transition actions to retain
// their original position. No final-value comparison replaces those actions.
FailureOr<NativeEvalValueInterface>
materializeNativeEvalValueInterface(LLVM::LLVMFuncOp function,
                                    SmallVector<NativeEvalValueLane> lanes,
                                    SymbolTable &symbols, bool littleEndian) {
  OpBuilder builder(function.getContext());
  Location location = function.getLoc();
  auto originalType = function.getFunctionType();
  bool status = !isa<LLVM::LLVMVoidType>(originalType.getReturnType());
  SmallVector<Type> arguments(originalType.getParams()), results;
  if (status)
    results.push_back(originalType.getReturnType());
  SmallVector<int64_t> description;
  for (const auto &lane : lanes) {
    Type type = builder.getIntegerType((lane.end - lane.begin) * 8);
    arguments.push_back(type);
    if (lane.written)
      results.push_back(type);
    description.append({int64_t(lane.plane == "__obelisk_state_unknown"),
                        int64_t(lane.begin), int64_t(lane.end - lane.begin),
                        int64_t(lane.written)});
  }
  Type result = results.empty()
                    ? Type(LLVM::LLVMVoidType::get(function.getContext()))
                    : Type(LLVM::LLVMStructType::getLiteral(
                          function.getContext(), results));
  builder.setInsertionPoint(function);
  auto body = LLVM::LLVMFuncOp::create(
      builder, location, function.getSymName().str() + ".__obelisk_eval_values",
      LLVM::LLVMFunctionType::get(result, arguments, false),
      LLVM::Linkage::Internal);
  symbols.insert(body);
  if (auto passthrough = function.getPassthroughAttr())
    body.setPassthroughAttr(passthrough);
  if (auto attrs = extendNativeEvalArgumentAttrs(
          builder, function.getArgAttrsAttr(), arguments.size()))
    body.setArgAttrsAttr(attrs);
  schedule::set<schedule::Field::EvalValueLanes>(
      body, builder.getDenseI64ArrayAttr(description));
  body.getBody().takeBody(function.getBody());
  Block &entry = body.getBody().front();
  SmallVector<Value> locals;
  SmallVector<PromotableAllocationOpInterface> allocations;
  for (const auto &lane : lanes) {
    Type type = builder.getIntegerType((lane.end - lane.begin) * 8);
    Value argument = entry.addArgument(type, location);
    builder.setInsertionPointToStart(&entry);
    Value local = entryAlloca(builder, location, type, 1, 1);
    locals.push_back(local);
    allocations.push_back(
        cast<PromotableAllocationOpInterface>(local.getDefiningOp()));
    LLVM::StoreOp::create(builder, location, argument, local, 1);
    for (Operation *operation : lane.accesses) {
      builder.setInsertionPoint(operation);
      auto load = dyn_cast<LLVM::LoadOp>(operation);
      auto store = dyn_cast<LLVM::StoreOp>(operation);
      auto narrow =
          cast<IntegerType>(load ? load.getType() : store.getValue().getType());
      uint64_t byte =
          *staticByteOffset(load ? load.getAddr() : store.getAddr()) -
          lane.begin;
      unsigned shift = littleEndian ? byte * 8
                                    : cast<IntegerType>(type).getWidth() -
                                          byte * 8 - narrow.getWidth();
      Value value;
      if (load) {
        value =
            LLVM::LoadOp::create(builder, operation->getLoc(), type, local, 1);
        if (shift)
          value = LLVM::LShrOp::create(
              builder, operation->getLoc(), value,
              llvmConstant(builder, operation->getLoc(), type, shift));
        if (narrow != type)
          value = LLVM::TruncOp::create(builder, operation->getLoc(), narrow,
                                        value);
        load.replaceAllUsesWith(value);
      } else {
        value = store.getValue();
        if (narrow != type) {
          value =
              LLVM::ZExtOp::create(builder, operation->getLoc(), type, value);
          if (shift)
            value = LLVM::ShlOp::create(
                builder, operation->getLoc(), value,
                llvmConstant(builder, operation->getLoc(), type, shift));
          APInt keep = ~APInt::getBitsSet(cast<IntegerType>(type).getWidth(),
                                          shift, shift + narrow.getWidth());
          Value previous = LLVM::LoadOp::create(builder, operation->getLoc(),
                                                type, local, 1);
          value = LLVM::OrOp::create(
              builder, operation->getLoc(), value,
              LLVM::AndOp::create(builder, operation->getLoc(), previous,
                                  LLVM::ConstantOp::create(builder,
                                                           operation->getLoc(),
                                                           type, keep)));
        }
        LLVM::StoreOp::create(builder, operation->getLoc(), value, local, 1);
      }
      operation->erase();
    }
  }
  SmallVector<LLVM::ReturnOp> returns;
  body.walk([&](LLVM::ReturnOp ret) { returns.push_back(ret); });
  for (auto ret : returns) {
    builder.setInsertionPoint(ret);
    if (results.empty())
      continue;
    Value value = LLVM::UndefOp::create(builder, ret.getLoc(), result);
    unsigned field = 0;
    if (status)
      value = insertValue(builder, ret.getLoc(), value, ret.getArg(), field++);
    for (auto [i, lane] : llvm::enumerate(lanes))
      if (lane.written)
        value = insertValue(
            builder, ret.getLoc(), value,
            LLVM::LoadOp::create(builder, ret.getLoc(),
                                 arguments[originalType.getNumParams() + i],
                                 locals[i], 1),
            field++);
    LLVM::ReturnOp::create(builder, ret.getLoc(), value);
    ret.erase();
  }
  DominanceInfo dominance(body);
  if (failed(tryToPromoteMemorySlots(allocations, builder,
                                     DataLayout::closest(body), dominance)))
    return body.emitError("could not promote eval value interface");

  // Old callers (fine Tier 2/3 execution, guards and function pointers) keep
  // the same symbol and ABI. Only a selected generated caller bypasses this
  // acquisition/publication wrapper. Observer/runtime calls still cut its
  // caller's SSA lifetime (IEEE 1800-2023 4.3, 38.15 and 38.34).
  Block *wrapper = function.addEntryBlock(builder);
  builder.setInsertionPointToStart(wrapper);
  SmallVector<Value> inputs(wrapper->getArguments()), addresses;
  for (const auto &lane : lanes) {
    Value base = LLVM::AddressOfOp::create(
        builder, location, LLVM::LLVMPointerType::get(function.getContext()),
        lane.plane);
    Value address = byteGEP(builder, location, base, lane.begin);
    addresses.push_back(address);
    inputs.push_back(LLVM::LoadOp::create(
        builder, location, builder.getIntegerType((lane.end - lane.begin) * 8),
        address, 1));
  }
  auto call = LLVM::CallOp::create(
      builder, location, results.empty() ? TypeRange{} : TypeRange{result},
      body.getSymName(), inputs);
  if (auto attrs = body.getArgAttrsAttr())
    call.setArgAttrsAttr(attrs);
  unsigned field = status;
  for (auto [i, lane] : llvm::enumerate(lanes))
    if (lane.written)
      LLVM::StoreOp::create(
          builder, location,
          LLVM::ExtractValueOp::create(builder, location, call.getResult(),
                                       ArrayRef<int64_t>{field++}),
          addresses[i], 1);
  if (status)
    LLVM::ReturnOp::create(builder, location,
                           LLVM::ExtractValueOp::create(builder, location,
                                                        call.getResult(),
                                                        ArrayRef<int64_t>{0}));
  else
    LLVM::ReturnOp::create(builder, location, ValueRange{});
  for (auto &lane : lanes)
    lane.accesses.clear();
  return NativeEvalValueInterface{body, std::move(lanes), result, status};
}

// Outlining a closed computation must not force a canonical round trip.
// Carry shared current lanes as arguments/results, allowing the existing
// caller-side SSA promoter to combine them with inlined computation. Pending
// NBA payloads remain separate; this never turns an enqueue into a blocking
// write (IEEE 1800-2023 4.9.4 and 10.4.2).
LogicalResult materializeNativeEvalValueCalls(
    ModuleOp module,
    SmallVector<std::pair<LLVM::LLVMFuncOp, uint64_t>> &callers,
    const llvm::StringMap<Type> &globalTypes,
    const llvm::StringMap<NativeStateFootprint> &footprints,
    DenseSet<Operation *> &valueCallers) {
  auto layout = module->getAttrOfType<StringAttr>("llvm.data_layout");
  if (!layout)
    return success();
  auto parsed = llvm::DataLayout::parse(layout.getValue());
  if (!parsed) {
    llvm::consumeError(parsed.takeError());
    return module.emitError("invalid native eval value data layout");
  }
  SymbolTable symbols(module);
  DenseMap<Operation *, NativeEvalValueInterface> interfaces;
  DenseSet<Operation *> rejected;
  uint64_t valueCalls = 0;
  for (auto &[caller, budget] : callers) {
    SmallVector<LLVM::CallOp> calls;
    caller.walk([&](LLVM::CallOp call) { calls.push_back(call); });
    NativeStateFootprint callerAccesses{true, {}, {}};
    caller.walk([&](Operation *operation) {
      auto load = dyn_cast<LLVM::LoadOp>(operation);
      auto store = dyn_cast<LLVM::StoreOp>(operation);
      if (!load && !store)
        return;
      Value pointer = load ? load.getAddr() : store.getAddr(), root = pointer;
      while (auto gep = root.getDefiningOp<LLVM::GEPOp>())
        root = gep.getBase();
      auto address = root.getDefiningOp<LLVM::AddressOfOp>();
      auto type = dyn_cast<IntegerType>(load ? load.getType()
                                             : store.getValue().getType());
      auto offset = staticByteOffset(pointer);
      if (address && type && offset && type.getWidth() % 8 == 0 &&
          (address.getGlobalName() == "__obelisk_state_value" ||
           address.getGlobalName() == "__obelisk_state_unknown"))
        (address.getGlobalName() == "__obelisk_state_value"
             ? callerAccesses.value
             : callerAccesses.unknown)
            .emplace_back(*offset, *offset + type.getWidth() / 8);
    });
    normalizeNativeByteFootprint(callerAccesses.value);
    normalizeNativeByteFootprint(callerAccesses.unknown);
    SmallVector<std::pair<Operation *, const NativeStateFootprint *>>
        nativeCalls;
    for (auto call : calls)
      if (call.getCallee()) {
        auto definition = symbols.lookup<LLVM::LLVMFuncOp>(*call.getCallee());
        auto footprint = footprints.find(*call.getCallee());
        if (isNativeEvalValueCall(call, definition) &&
            footprint != footprints.end() && footprint->second.exact)
          nativeCalls.emplace_back(call, &footprint->second);
      }
    for (auto call : calls) {
      if (!call.getCallee())
        continue;
      auto callee = symbols.lookup<LLVM::LLVMFuncOp>(*call.getCallee());
      if (!isNativeEvalValueCall(call, callee) || callee == caller ||
          rejected.contains(callee) || callee.getFunctionType().isVarArg() ||
          callee.getCConv() != LLVM::cconv::CConv::C ||
          call.getCConv() != LLVM::cconv::CConv::C ||
          call.getTailCallKind() != LLVM::tailcallkind::TailCallKind::None ||
          !call.getOpBundleSizes().empty())
        continue;
      auto found = interfaces.find(callee);
      if (found == interfaces.end()) {
        auto footprint = footprints.find(callee.getSymName());
        Type originalResult = callee.getFunctionType().getReturnType();
        bool status = !isa<LLVM::LLVMVoidType>(originalResult);
        if (footprint == footprints.end() || !footprint->second.exact ||
            (status && (!isa<IntegerType>(originalResult) ||
                        cast<IntegerType>(originalResult).getWidth() > 64))) {
          rejected.insert(callee);
          continue;
        }
        SmallVector<NativeEvalValueLane> ranges;
        NativeStateFootprint cuts{true, {}, {}};
        auto cutPlane = [&](StringRef plane) -> NativeByteFootprint & {
          return plane == "__obelisk_state_value" ? cuts.value : cuts.unknown;
        };
        auto cutPointer = [&](Value pointer) {
          while (auto gep = pointer.getDefiningOp<LLVM::GEPOp>())
            pointer = gep.getBase();
          auto address = pointer.getDefiningOp<LLVM::AddressOfOp>();
          if (!address ||
              (address.getGlobalName() != "__obelisk_state_value" &&
               address.getGlobalName() != "__obelisk_state_unknown"))
            return;
          auto array = cast<LLVM::LLVMArrayType>(
              globalTypes.lookup(address.getGlobalName()));
          cutPlane(address.getGlobalName())
              .emplace_back(0, array.getNumElements());
        };
        // Freeze cuts once, rather than rescanning the complete body for every
        // lane. Nested readers, intrinsics and dynamic/atomic accesses cannot
        // be replaced by a fixed value argument/result interface.
        callee.walk([&](Operation *operation) {
          if (auto nested = dyn_cast<LLVM::CallOp>(operation)) {
            if (nested.getCallee())
              if (auto target = footprints.find(*nested.getCallee());
                  target != footprints.end()) {
                llvm::append_range(cuts.value, target->second.value);
                llvm::append_range(cuts.unknown, target->second.unknown);
              }
            return;
          }
          if (auto copy = dyn_cast<LLVM::MemcpyOp>(operation)) {
            cutPointer(copy.getSrc());
            cutPointer(copy.getDst());
            return;
          }
          if (auto set = dyn_cast<LLVM::MemsetOp>(operation)) {
            cutPointer(set.getDst());
            return;
          }
          auto load = dyn_cast<LLVM::LoadOp>(operation);
          auto store = dyn_cast<LLVM::StoreOp>(operation);
          if (!load && !store)
            return;
          Value pointer = load ? load.getAddr() : store.getAddr(),
                root = pointer;
          while (auto gep = root.getDefiningOp<LLVM::GEPOp>())
            root = gep.getBase();
          auto address = root.getDefiningOp<LLVM::AddressOfOp>();
          if (!address ||
              (address.getGlobalName() != "__obelisk_state_value" &&
               address.getGlobalName() != "__obelisk_state_unknown"))
            return;
          auto offset = staticByteOffset(pointer);
          auto type = dyn_cast<IntegerType>(load ? load.getType()
                                                 : store.getValue().getType());
          auto array = dyn_cast_or_null<LLVM::LLVMArrayType>(
              globalTypes.lookup(address.getGlobalName()));
          bool plain =
              load
                  ? !load.getVolatile_() &&
                        load.getOrdering() == LLVM::AtomicOrdering::not_atomic
                  : !store.getVolatile_() &&
                        store.getOrdering() == LLVM::AtomicOrdering::not_atomic;
          if (!offset || !plain) {
            auto bytes =
                schedule::get<schedule::Field::EvalStateAccessBytes>(operation);
            if (bytes && bytes.size() == 2 && bytes[0] >= 0 &&
                bytes[1] > bytes[0])
              cutPlane(address.getGlobalName())
                  .emplace_back(bytes[0], bytes[1]);
            else if (offset && type && type.getWidth() % 8 == 0)
              cutPlane(address.getGlobalName())
                  .emplace_back(*offset, *offset + type.getWidth() / 8);
            else
              cutPointer(pointer);
            return;
          }
          if (type && type.getWidth() % 8 == 0 && array &&
              *offset <= array.getNumElements() &&
              type.getWidth() / 8 <= array.getNumElements() - *offset)
            ranges.push_back({address.getGlobalName(),
                              *offset,
                              *offset + type.getWidth() / 8,
                              bool(store),
                              {operation}});
        });
        llvm::sort(ranges, [](const auto &a, const auto &b) {
          return std::tie(a.plane, a.begin, a.end) <
                 std::tie(b.plane, b.begin, b.end);
        });
        SmallVector<NativeEvalValueLane> windows;
        for (auto &range : ranges) {
          if (windows.empty() || windows.back().plane != range.plane ||
              windows.back().end <= range.begin)
            windows.push_back(std::move(range));
          else {
            auto &window = windows.back();
            window.end = std::max(window.end, range.end);
            window.written |= range.written;
            llvm::append_range(window.accesses, range.accesses);
          }
        }
        auto overlaps = [](const NativeEvalValueLane &lane,
                           const NativeStateFootprint &footprint) {
          const auto &ranges = lane.plane == "__obelisk_state_value"
                                   ? footprint.value
                                   : footprint.unknown;
          auto first = llvm::lower_bound(
              ranges, lane.begin,
              [](auto range, uint64_t begin) { return range.second <= begin; });
          return first != ranges.end() && first->first < lane.end;
        };
        normalizeNativeByteFootprint(cuts.value);
        normalizeNativeByteFootprint(cuts.unknown);
        llvm::erase_if(windows, [&](const auto &lane) {
          if (lane.end - lane.begin > 16)
            return true;
          bool closed = !overlaps(lane, cuts);
          bool shared = overlaps(lane, callerAccesses);
          for (auto [other, footprint] : nativeCalls)
            if (other != call.getOperation())
              shared |= overlaps(lane, *footprint);
          return !closed || !shared;
        });
        llvm::stable_sort(windows, [](const auto &a, const auto &b) {
          return a.accesses.size() > b.accesses.size();
        });
        SmallVector<NativeEvalValueLane> selected;
        unsigned outputs = status, arguments = 0;
        for (Type argument : callee.getFunctionType().getParams())
          arguments += isa<IntegerType>(argument)
                           ? (cast<IntegerType>(argument).getWidth() + 63) / 64
                           : 1;
        uint64_t cost = 0;
        for (auto &lane : windows) {
          // Keep the return in at most two machine words and the complete
          // input interface within the integer-register argument budget.
          unsigned words = (lane.end - lane.begin + 7) / 8;
          if (outputs + words * lane.written > 2 || arguments + words > 6)
            continue;
          uint64_t growth = 12 + 8 * callee.getBody().getBlocks().size();
          for (auto *operation : lane.accesses) {
            Type type =
                isa<LLVM::LoadOp>(operation)
                    ? cast<LLVM::LoadOp>(operation).getType()
                    : cast<LLVM::StoreOp>(operation).getValue().getType();
            if (cast<IntegerType>(type).getWidth() !=
                (lane.end - lane.begin) * 8)
              growth += 5;
          }
          if (cost + growth + 8 * (selected.size() + 1) > budget)
            continue;
          cost += growth;
          outputs += words * lane.written;
          arguments += words;
          selected.push_back(std::move(lane));
        }
        if (selected.empty())
          continue;
        auto interface = materializeNativeEvalValueInterface(
            callee, std::move(selected), symbols, parsed->isLittleEndian());
        if (failed(interface))
          return failure();
        budget -= cost;
        found = interfaces.try_emplace(callee, std::move(*interface)).first;
      }
      auto &interface = found->second;
      uint64_t cost = 8 * interface.lanes.size();
      if (budget < cost)
        continue;
      budget -= cost;
      OpBuilder builder(call);
      SmallVector<Value> arguments(call.getOperands()), addresses;
      auto owner = schedule::get<schedule::Field::EvalGroupMember>(call);
      for (const auto &lane : interface.lanes) {
        Value base = LLVM::AddressOfOp::create(
            builder, call.getLoc(),
            LLVM::LLVMPointerType::get(module.getContext()), lane.plane);
        Value address = byteGEP(builder, call.getLoc(), base, lane.begin);
        addresses.push_back(address);
        auto load = LLVM::LoadOp::create(
            builder, call.getLoc(),
            builder.getIntegerType((lane.end - lane.begin) * 8), address, 1);
        if (owner)
          schedule::set<schedule::Field::EvalGroupOwner>(load, owner);
        arguments.push_back(load);
      }
      bool result = !isa<LLVM::LLVMVoidType>(interface.result);
      auto replacement = LLVM::CallOp::create(
          builder, call.getLoc(),
          result ? TypeRange{interface.result} : TypeRange{},
          interface.body.getSymName(), arguments);
      if (auto attrs = extendNativeEvalArgumentAttrs(
              builder, call.getArgAttrsAttr(), arguments.size()))
        replacement.setArgAttrsAttr(attrs);
      for (auto attr : call->getAttrs())
        if (attr.getName().strref().starts_with("schedule.eval."))
          replacement->setAttr(attr.getName(), attr.getValue());
      unsigned field = interface.returnsStatus;
      for (auto [i, lane] : llvm::enumerate(interface.lanes))
        if (lane.written) {
          auto store = LLVM::StoreOp::create(
              builder, call.getLoc(),
              LLVM::ExtractValueOp::create(builder, call.getLoc(),
                                           replacement.getResult(),
                                           ArrayRef<int64_t>{field++}),
              addresses[i], 1);
          if (owner)
            schedule::set<schedule::Field::EvalGroupOwner>(store, owner);
        }
      if (interface.returnsStatus)
        call.getResult().replaceAllUsesWith(LLVM::ExtractValueOp::create(
            builder, call.getLoc(), replacement.getResult(),
            ArrayRef<int64_t>{0}));
      call.erase();
      valueCallers.insert(caller);
      ++valueCalls;
    }
  }
  if (module->hasAttr("obelisk.debug.native_timing"))
    llvm::errs() << "obelisk eval value interfaces: cores=" << interfaces.size()
                 << " calls=" << valueCalls << '\n';
  return success();
}

struct NativeGroupPromotionInputs {
  explicit NativeGroupPromotionInputs(Operation *operation) {
    for (auto global : cast<ModuleOp>(operation).getOps<LLVM::GlobalOp>())
      globalTypes.try_emplace(global.getSymName(), global.getGlobalType());
    for (auto function : cast<ModuleOp>(operation).getOps<LLVM::LLVMFuncOp>())
      if (schedule::has<schedule::Field::EvalStateMetadataOnly>(function))
        metadataOnly.insert(function.getSymName());
    callFootprints = buildNativeStateFootprints(cast<ModuleOp>(operation),
                                                globalTypes, metadataOnly);
    debugTiming = operation->hasAttr("obelisk.debug.native_timing");
  }
  llvm::StringMap<Type> globalTypes;
  llvm::StringSet<> metadataOnly;
  llvm::StringMap<NativeStateFootprint> callFootprints;
  DenseMap<Operation *, std::pair<uint64_t, size_t>> residuals;
  std::shared_ptr<NativeGroupPromotionReport> report;
  bool debugTiming = false;
};

class PromoteNativeGroupFunctionPass final
    : public PassWrapper<PromoteNativeGroupFunctionPass,
                         OperationPass<LLVM::LLVMFuncOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(PromoteNativeGroupFunctionPass)
  StringRef getArgument() const final {
    return "promote-native-group-function";
  }
  void runOnOperation() override {
    auto function = getOperation();
    auto cached = getCachedParentAnalysis<NativeGroupPromotionInputs>(
        function->getParentOfType<ModuleOp>());
    if (!cached) {
      function.emitError("native group promotion requires cached global types");
      return signalPassFailure();
    }
    const auto &inputs = cached->get();
    auto found = inputs.residuals.find(function);
    if (found == inputs.residuals.end()) {
      markAllAnalysesPreserved();
      return;
    }
    auto [budget, index] = found->second;
    uint64_t remaining = budget;
    if (failed(
            promoteGroupReadyWords(function, inputs.globalTypes, remaining)) ||
        failed(promoteGroupState(function, inputs.globalTypes,
                                 "__obelisk_state_value", remaining,
                                 inputs.metadataOnly, inputs.callFootprints)) ||
        failed(promoteGroupState(function, inputs.globalTypes,
                                 "__obelisk_state_unknown", remaining,
                                 inputs.metadataOnly, inputs.callFootprints)))
      return signalPassFailure();
    if (remaining == budget) {
      markAllAnalysesPreserved();
      return;
    }
    if (!inputs.debugTiming)
      return;
    llvm::raw_string_ostream diagnostic(inputs.report->diagnostics[index]);
    auto count = [&](schedule::Field name) -> uint64_t {
      auto value = schedule::get<IntegerAttr>(function, name);
      return value ? value.getUInt() : 0;
    };
    diagnostic << "obelisk native group: " << function.getSymName()
               << " expanded_calls="
               << count(schedule::Field::EvalMaterializedGroupCalls)
               << " ready_words=" << count(schedule::Field::EvalSsaReadyWords)
               << " value_ranges=" << count(schedule::Field::EvalSsaValueRanges)
               << " unknown_ranges="
               << count(schedule::Field::EvalSsaUnknownRanges)
               << " remaining_budget=" << remaining << '\n';
  }
};

struct InlinedGroupBody {
  uint64_t remaining;
  uint64_t materialized;
};

InlinedGroupBody
inlineGroupBody(LLVM::LLVMFuncOp function, const SymbolTable &symbols,
                const DenseMap<Operation *, uint64_t> &stableCosts,
                DenseMap<Operation *, uint64_t> &costs,
                uint64_t operationLimit) {
  InlinerInterface interface(function.getContext());
  SmallVector<LLVM::CallOp> pending;
  function.walk([&](LLVM::CallOp call) {
    if (::obelisk::schedule::has<::obelisk::schedule::Field::EvalGroupMember>(
            call))
      pending.push_back(call);
  });
  std::reverse(pending.begin(), pending.end());
  // The existing native helper-size budget also caps total additional IR
  // per coordinator. Oversized computation stays in direct helpers; helper
  // calls do not acquire time/region ownership or become scheduler exits.
  uint64_t remaining = operationLimit;
  uint64_t materialized = 0;
  llvm::DenseSet<Operation *> expanded;
  while (!pending.empty()) {
    LLVM::CallOp call = pending.pop_back_val();
    if (!call.getCallee())
      continue;
    auto callee = symbols.lookup<LLVM::LLVMFuncOp>(*call.getCallee());
    if (!callee || callee == function || callee.isExternal() ||
        expanded.contains(callee) ||
        !(::obelisk::schedule::has<
              ::obelisk::schedule::Field::EvalGroupDomainSelected>(call) ||
          ::obelisk::schedule::has<schedule::metadata::evalInfallible>(
              callee) ||
          ::obelisk::schedule::has<
              ::obelisk::schedule::Field::EvalFourStateSource>(callee) ||
          ::obelisk::schedule::has<
              ::obelisk::schedule::Field::EvalConditionallyTwoState>(callee)))
      continue;
    uint64_t cost;
    if (auto frozen = stableCosts.find(callee); frozen != stableCosts.end()) {
      cost = frozen->second;
    } else {
      auto [found, inserted] = costs.try_emplace(callee, 0);
      if (inserted)
        callee.walk([&](Operation *operation) {
          if (operation != callee.getOperation())
            found->second += nativeOperationCost(operation);
        });
      cost = found->second;
    }
    if (!cost || cost > remaining)
      continue;
    SmallVector<LLVM::CallOp> nested;
    auto owner =
        ::obelisk::schedule::get<::obelisk::schedule::Field::EvalGroupMember>(
            call);
    auto clone = [&](OpBuilder &, Region *source, Block *inlineBlock,
                     Block *postInsertBlock, IRMapping &mapping,
                     bool shouldClone) {
      assert(shouldClone && "canonical executor bodies must remain intact");
      source->cloneInto(inlineBlock->getParent(),
                        postInsertBlock->getIterator(), mapping);
      for (Block &block : *source)
        for (Operation &operation : *mapping.lookup(&block)) {
          if (owner && isa<LLVM::LoadOp, LLVM::StoreOp>(operation))
            schedule::set<schedule::Field::EvalGroupOwner>(&operation, owner);
          if (auto child = dyn_cast<LLVM::CallOp>(operation)) {
            if (owner)
              ::obelisk::schedule::set<
                  ::obelisk::schedule::Field::EvalGroupMember>(child, owner);
            nested.push_back(child);
          }
        }
    };
    if (failed(inlineCall(interface, clone, cast<CallOpInterface>(*call),
                          cast<CallableOpInterface>(*callee),
                          &callee.getBody())))
      continue;
    call.erase();
    // Shared computation remains a direct helper after one expansion.
    // Besides avoiding Cartesian cloning, this cuts recursive call edges
    // even when the user disables the operation-size budget.
    expanded.insert(callee);
    remaining -= cost;
    ++materialized;
    std::reverse(nested.begin(), nested.end());
    llvm::append_range(pending, nested);
  }
  if (materialized)
    ::obelisk::schedule::set<
        ::obelisk::schedule::Field::EvalMaterializedGroupCalls>(
        function, IntegerAttr::get(IntegerType::get(function.getContext(), 64),
                                   materialized));
  return {remaining, materialized};
}

} // namespace

/// Materialize certified activation bodies before native partitioning. The
/// original pending bits, source predicates, and boundary exits remain the
/// schedule: neither a value snapshot nor an unconditional call sequence can
/// replace that activation evidence (IEEE 1800-2023 4.3--4.7).
///
/// Route specialization must run first. A durable two-state certificate then
/// exposes a direct body; an unresolved route remains an executor boundary.
/// In particular, do not select a two-state callee just because it appears in
/// an indirect call's allowed-callee inventory.
FailureOr<std::shared_ptr<NativeGroupPromotionReport>>
materializeNativeEvalGroupBodies(ModuleOp module, AnalysisManager manager) {
  auto report = std::make_shared<NativeGroupPromotionReport>();
  SmallVector<LLVM::LLVMFuncOp> groups;
  for (auto function : module.getOps<LLVM::LLVMFuncOp>())
    if (::obelisk::schedule::has<::obelisk::schedule::Field::EvalRankedMembers>(
            function))
      groups.push_back(function);
  auto &inputs = manager.getAnalysis<NativeGroupPromotionInputs>();
  inputs.report = report;
  DialectRegistry registry;
  LLVM::registerInlinerInterface(registry);
  module.getContext()->appendDialectRegistry(registry);
  SymbolTable symbols(module);
  // Candidates are transient compiler-owned bodies. Preserve user-authored
  // candidates with existing callers rather than deleting or retargeting
  // their callable interface. Inventory uses once, not once per group.
  DenseSet<Operation *> externallyUsedCandidates;
  if (llvm::any_of(groups, [](auto function) {
        return ::obelisk::schedule::has<
            ::obelisk::schedule::Field::EvalDataflowCandidate>(function);
      })) {
    SmallVector<Region *> scopes;
    module.walk([&](Operation *operation) {
      if (operation->hasTrait<OpTrait::SymbolTable>())
        for (Region &region : operation->getRegions())
          scopes.push_back(&region);
    });
    auto uses = collectNativeSymbolUses(module.getContext(), scopes);
    if (!uses) {
      // An unknown symbol scope cannot prove a transient candidate unused.
      for (auto function : groups)
        if (::obelisk::schedule::has<
                ::obelisk::schedule::Field::EvalDataflowCandidate>(function))
          externallyUsedCandidates.insert(function);
    } else {
      SymbolTableCollection tables;
      // Resolve in the scope containing the user, as SymbolUserMap does.
      // A symbol-table operation's own attributes still use its outer scope.
      for (const auto &use : *uses)
        if (auto function = tables.lookupNearestSymbolFrom<LLVM::LLVMFuncOp>(
                use.getUser()->getParentOp(), use.getSymbolRef());
            function &&
            ::obelisk::schedule::has<
                ::obelisk::schedule::Field::EvalDataflowCandidate>(function))
          externallyUsedCandidates.insert(function);
    }
  }

  auto limit =
      ::obelisk::schedule::get<::obelisk::schedule::Field::MaxInlineOps>(
          module);
  uint64_t operationLimit = limit ? limit.getValue().getZExtValue() : 5000;
  if (!operationLimit)
    operationLimit = UINT64_MAX;

  // Measure each needed definition once. Charging its own operations bounds
  // both cloning and subsequent optimization; following shared callees here
  // would repeatedly charge the same graph, just as following global
  // initializers did in native partition cost estimation.
  llvm::DenseMap<Operation *, uint64_t> costs;
  SmallVector<std::pair<LLVM::LLVMFuncOp, uint64_t>> residuals;
  DenseSet<Operation *> whollyOutlinedGroups;
  DenseMap<Operation *, uint64_t> residualBudgets;
  DenseSet<Operation *> finalized, discarded, refinedChildren;
  struct Refinement {
    LLVM::LLVMFuncOp parent;
    SmallVector<LLVM::LLVMFuncOp> children;
  };
  SmallVector<Refinement> refinements;
  DenseMap<Operation *, unsigned> refinementRoots;
  SmallVector<uint64_t> refinementBudgets;
  // Establish body read/write dependencies with MLIR's call graph before
  // mutation. A worker must neither read a group another worker can change,
  // nor change a definition read by the ordered refinement phase. Shared leaf
  // executors remain immutable; groups with dependencies retain source order.
  CallGraph callGraph(module);
  DenseSet<CallGraphNode *> groupNodes, reachesGroup, referencedGroups;
  DenseMap<CallGraphNode *, SmallVector<CallGraphNode *>> callers;
  for (CallGraphNode *node : callGraph)
    for (const auto &edge : *node)
      callers[edge.getTarget()].push_back(node);
  SmallVector<CallGraphNode *> pending;
  for (auto function : groups)
    if (auto *node = callGraph.lookupNode(&function.getBody())) {
      groupNodes.insert(node);
      reachesGroup.insert(node);
      pending.push_back(node);
    }
  while (!pending.empty()) {
    auto *node = pending.pop_back_val();
    for (auto *caller : callers.lookup(node))
      if (reachesGroup.insert(caller).second)
        pending.push_back(caller);
  }
  DenseSet<CallGraphNode *> reachable;
  llvm::append_range(pending, groupNodes);
  while (!pending.empty()) {
    auto *node = pending.pop_back_val();
    if (!reachable.insert(node).second)
      continue;
    for (const auto &edge : *node) {
      if (groupNodes.contains(edge.getTarget()))
        referencedGroups.insert(edge.getTarget());
      if (!edge.getTarget()->isExternal())
        pending.push_back(edge.getTarget());
    }
  }
  SmallVector<LLVM::LLVMFuncOp> stableDefinitions;
  for (auto *node : reachable) {
    if (node->isExternal() || groupNodes.contains(node))
      continue;
    if (auto function = dyn_cast<LLVM::LLVMFuncOp>(
            node->getCallableRegion()->getParentOp()))
      stableDefinitions.push_back(function);
  }
  SmallVector<uint64_t> stableCounts(stableDefinitions.size());
  parallelFor(module.getContext(), 0, stableDefinitions.size(), [&](size_t i) {
    stableDefinitions[i].walk([&](Operation *operation) {
      if (operation != stableDefinitions[i].getOperation())
        stableCounts[i] += nativeOperationCost(operation);
    });
  });
  DenseMap<Operation *, uint64_t> stableCosts;
  for (auto [i, function] : llvm::enumerate(stableDefinitions))
    stableCosts.try_emplace(function, stableCounts[i]);

  // Refinement can replace the original named by candidate metadata without
  // a call edge. Preserve that write-before-inline dependency as well. An
  // original preceding its candidate may be prepared early: its ordered
  // inlining already runs before the candidate's body transaction.
  DenseMap<Operation *, size_t> groupOrder;
  for (auto [i, function] : llvm::enumerate(groups))
    groupOrder.try_emplace(function, i);
  DenseSet<Operation *> earlyRefinementTargets;
  for (auto [i, function] : llvm::enumerate(groups))
    if (auto candidate = ::obelisk::schedule::get<
            ::obelisk::schedule::Field::EvalDataflowCandidate>(function)) {
      auto original = symbols.lookup<LLVM::LLVMFuncOp>(candidate.getValue());
      auto position = groupOrder.find(original);
      if (position != groupOrder.end() && position->second > i)
        earlyRefinementTargets.insert(original);
    }
  SmallVector<LLVM::LLVMFuncOp> independent;
  for (auto function : groups) {
    auto *node = callGraph.lookupNode(&function.getBody());
    if (!node || referencedGroups.contains(node) ||
        earlyRefinementTargets.contains(function) ||
        externallyUsedCandidates.contains(function) ||
        llvm::any_of(*node, [&](const auto &edge) {
          return reachesGroup.contains(edge.getTarget());
        }))
      continue;
    independent.push_back(function);
  }
  SmallVector<InlinedGroupBody> inlinedBodies(independent.size());
  parallelFor(module.getContext(), 0, independent.size(), [&](size_t i) {
    DenseMap<Operation *, uint64_t> localCosts;
    inlinedBodies[i] = inlineGroupBody(independent[i], symbols, stableCosts,
                                       localCosts, operationLimit);
  });
  DenseMap<Operation *, InlinedGroupBody> preInlined;
  for (auto [i, function] : llvm::enumerate(independent))
    preInlined.try_emplace(function, inlinedBodies[i]);
  if (module->hasAttr("obelisk.debug.native_timing"))
    llvm::errs() << "obelisk native group inlining: independent="
                 << independent.size()
                 << " ordered=" << groups.size() - independent.size() << '\n';

  // Refinement appends children. Keep original activation CFGs intact until
  // all candidates have been considered; memory SSA would erase the entry
  // evidence needed to prove a partial split.
  for (size_t index = 0; index < groups.size(); ++index) {
    auto function = groups[index];
    if (externallyUsedCandidates.contains(function)) {
      ::obelisk::schedule::remove<
          ::obelisk::schedule::Field::EvalDataflowCandidate>(function);
      continue;
    }
    auto inlined = preInlined.find(function);
    InlinedGroupBody body =
        inlined != preInlined.end()
            ? inlined->second
            : inlineGroupBody(function, symbols, stableCosts, costs,
                              operationLimit);
    // Refinement may erase a candidate and allocate new child functions.
    // Do not retain a consumed operation pointer across that allocation.
    if (inlined != preInlined.end())
      preInlined.erase(inlined);
    // Member effects are now visible in one CFG. Combine semantic readiness
    // reads/updates before expanding indexes or predicating the computation.
    if (failed(lowerNativeScheduleActionsToLLVM(function)))
      return failure();
    uint64_t remaining = body.remaining;
    if (auto candidate = ::obelisk::schedule::get<
            ::obelisk::schedule::Field::EvalDataflowCandidate>(function)) {
      auto original = symbols.lookup<LLVM::LLVMFuncOp>(candidate.getValue());
      auto members = ::obelisk::schedule::get<
          ::obelisk::schedule::Field::EvalRankedMembers>(function);
      auto pendingGlobal = symbols.lookup<LLVM::GlobalOp>(
          "__obelisk_eval_promotion_pending_mask_v1");
      auto pendingType =
          pendingGlobal
              ? dyn_cast<LLVM::LLVMArrayType>(pendingGlobal.getGlobalType())
              : LLVM::LLVMArrayType{};
      bool identitiesValid =
          original && original != function && !original.empty() &&
          !::obelisk::schedule::has<
              ::obelisk::schedule::Field::EvalDataflowCandidate>(original) &&
          !::obelisk::schedule::has<
              ::obelisk::schedule::Field::EvalDataflowExecutor>(original) &&
          members &&
          !::obelisk::schedule::has<
              ::obelisk::schedule::Field::EvalDataflowFallback>(original) &&
          !members.empty() &&
          original.getFunctionType() == function.getFunctionType() &&
          ::obelisk::schedule::get<
              ::obelisk::schedule::Field::EvalRankedMembers>(original) ==
              members &&
          ::obelisk::schedule::get<
              ::obelisk::schedule::Field::EvalGroupIngress>(original) ==
              ::obelisk::schedule::get<
                  ::obelisk::schedule::Field::EvalGroupIngress>(function) &&
          pendingType && pendingType.getElementType().isInteger(64);
      llvm::SmallDenseSet<uint32_t> identities;
      if (identitiesValid)
        for (int32_t owner : members.asArrayRef())
          identitiesValid &=
              owner >= 0 &&
              uint32_t(owner) / 64 < pendingType.getNumElements() &&
              identities.insert(owner).second;
      bool accepted = identitiesValid && materializeNativeGroupDataflow(
                                             function, symbols, operationLimit);
      if (module->hasAttr("obelisk.debug.native_timing"))
        llvm::errs() << "obelisk group dataflow: " << function.getSymName()
                     << " accepted=" << accepted << '\n';
      if (!accepted) {
        if (identitiesValid) {
          auto [root, inserted] =
              refinementRoots.try_emplace(function, refinementBudgets.size());
          if (inserted)
            refinementBudgets.push_back(operationLimit);
          unsigned rootID = root->second;
          auto children = splitNativeEvalGroup(original, function, symbols,
                                               refinementBudgets[rootID]);
          llvm::append_range(groups, children);
          if (!children.empty()) {
            refinements.push_back({original, {children[0], children[2]}});
            refinedChildren.insert(children[0]);
            refinedChildren.insert(children[2]);
            for (unsigned i : {1u, 3u})
              refinementRoots[children[i]] = rootID;
          }
        }
        symbols.erase(function);
        continue;
      }
      // One value-domain check for the whole predicated computation. A
      // pending proof retains the ordinary per-member path, allowing the
      // unaffected members to remain two-state. Activation predicates are
      // still evaluated inside the dataflow body; no cached coherence fact
      // can be invalidated by a known VPI deposit.
      // Promote the slow body before adding its fast guard, so slow-path
      // materialization never introduces loads on a collapsed child's entry.
      if (auto remaining = residualBudgets.find(original);
          remaining != residualBudgets.end()) {
        if (failed(lowerNativeScheduleActionsToLLVM(original)))
          return failure();
        if (failed(promoteGroupReadyWords(original, inputs.globalTypes,
                                          remaining->second)) ||
            failed(promoteGroupState(original, inputs.globalTypes,
                                     "__obelisk_state_value", remaining->second,
                                     inputs.metadataOnly,
                                     inputs.callFootprints)) ||
            failed(promoteGroupState(
                original, inputs.globalTypes, "__obelisk_state_unknown",
                remaining->second, inputs.metadataOnly, inputs.callFootprints)))
          return failure();
        finalized.insert(original);
      }
      if (refinedChildren.contains(original)) {
        // The parent owns this child's selection point. Keeping another
        // guarded wrapper on the fast path would add a call boundary which
        // physical native partitioning cannot reliably inline away.
        ::obelisk::schedule::set<
            ::obelisk::schedule::Field::EvalDataflowExecutor>(
            original, FlatSymbolRefAttr::get(function));
        ::obelisk::schedule::remove<
            ::obelisk::schedule::Field::EvalDataflowCandidate>(function);
        continue;
      }
      // Make computation the actual group entry before physical partitioning.
      // Keep exactly one copy of each body: the original guarded executor
      // becomes a cold helper, while the finite SSA body moves into the entry.
      // Native partitioning can then split helpers without reintroducing a
      // wrapper call on every known activation.
      Region fallbackBody;
      fallbackBody.takeBody(original.getBody());
      original.getBody().takeBody(function.getBody());
      function.getBody().takeBody(fallbackBody);
      for (auto name : {::obelisk::schedule::Field::EvalMaterializedGroupCalls,
                        ::obelisk::schedule::Field::EvalSsaReadyWords,
                        ::obelisk::schedule::Field::EvalSsaValueRanges,
                        ::obelisk::schedule::Field::EvalSsaUnknownRanges,
                        ::obelisk::schedule::Field::EvalPredicatedDataflow,
                        ::obelisk::schedule::Field::EvalDataflowSlots,
                        ::obelisk::schedule::Field::EvalCoalescedSlots,
                        ::obelisk::schedule::Field::EvalCacheHintWords}) {
        Attribute slow = schedule::get<Attribute>(original, name),
                  fast = schedule::get<Attribute>(function, name);
        schedule::remove(original, name);
        schedule::remove(function, name);
        if (fast)
          schedule::set(original, name, fast);
        if (slow)
          schedule::set(function, name, slow);
      }
      symbols.remove(function);
      function.setSymName(original.getSymName().str() + ".fallback");
      symbols.insert(function);
      ::obelisk::schedule::remove<
          ::obelisk::schedule::Field::EvalDataflowCandidate>(function);
      OpBuilder builder(module.getContext());
      Location loc = function.getLoc();
      Block *oldEntry = &original.getBody().front();
      auto *guard = new Block, *fallback = new Block;
      original.getBody().push_front(guard);
      original.getBody().push_back(fallback);
      for (BlockArgument argument : oldEntry->getArguments())
        guard->addArgument(argument.getType(), argument.getLoc());
      builder.setInsertionPointToStart(guard);
      unsigned width = 64;
      for (int32_t owner : members.asArrayRef())
        width = std::max(width, unsigned(owner) + 1);
      APInt mask(width, 0);
      for (int32_t owner : members.asArrayRef())
        mask.setBit(owner);
      Value pending = LLVM::AddressOfOp::create(
          builder, loc, LLVM::LLVMPointerType::get(module.getContext()),
          "__obelisk_eval_promotion_pending_mask_v1");
      Value needed = llvmConstant(builder, loc, builder.getI64Type(), 0);
      for (unsigned word = 0; word < mask.getNumWords(); ++word) {
        uint64_t bits = ownerMaskWord(mask, word);
        if (!bits)
          continue;
        Value selected = LLVM::AndOp::create(
            builder, loc, loadOwnerWord(builder, loc, pending, word),
            llvmConstant(builder, loc, builder.getI64Type(), bits));
        needed = LLVM::OrOp::create(builder, loc, needed, selected);
      }
      Value known = LLVM::ICmpOp::create(
          builder, loc, LLVM::ICmpPredicate::eq, needed,
          llvmConstant(builder, loc, builder.getI64Type(), 0));
      LLVM::CondBrOp::create(builder, loc, known, oldEntry,
                             guard->getArguments(), fallback, ValueRange{});
      builder.setInsertionPointToStart(fallback);
      LLVM::CallOp::create(builder, loc, TypeRange{}, function.getSymName(),
                           guard->getArguments());
      LLVM::ReturnOp::create(builder, loc, ValueRange{});
      ::obelisk::schedule::set<
          ::obelisk::schedule::Field::EvalDataflowFallback>(
          original, FlatSymbolRefAttr::get(function));
      continue;
    }
    // A completely outlined group can still carry current lanes through
    // explicit helper interfaces; zero inlined calls is not an eval exit.
    if (!body.materialized && !refinedChildren.contains(function))
      whollyOutlinedGroups.insert(function);
    residuals.emplace_back(function, remaining);
    residualBudgets[function] = remaining;
  }
  for (auto &refinement : llvm::reverse(refinements)) {
    bool useful = llvm::any_of(refinement.children, [](auto child) {
      return ::obelisk::schedule::has<
                 ::obelisk::schedule::Field::EvalDataflowExecutor>(child) ||
             ::obelisk::schedule::has<
                 ::obelisk::schedule::Field::EvalGroupChildren>(child);
    });
    if (!useful) {
      for (auto child : refinement.children) {
        discarded.insert(child);
        symbols.erase(child);
      }
      continue;
    }
    // Children partition the original ordered leaf identities. A parent adds
    // no readiness bits, domain guard, or event queue. Backward publications
    // survive child return for the shared loop (IEEE 1800-2023 4.4--4.7).
    auto parent = refinement.parent;
    parent.getBody().dropAllReferences();
    parent.getBody().getBlocks().clear();
    OpBuilder builder(module.getContext());
    Block *entry = parent.addEntryBlock(builder);
    builder.setInsertionPointToStart(entry);
    auto ingress =
        ::obelisk::schedule::get<::obelisk::schedule::Field::EvalGroupIngress>(
            parent);
    Value ready = LLVM::AddressOfOp::create(
        builder, parent.getLoc(),
        LLVM::LLVMPointerType::get(module.getContext()), ingress.getValue());
    SmallVector<Attribute> references;
    for (auto child : refinement.children) {
      references.push_back(FlatSymbolRefAttr::get(child));
      // Predication removes internal branches but may still compute inactive
      // expressions. Pay for a collapsed child only when it owns pending work.
      // Reacquire after each child: an earlier child can publish a later one.
      std::map<unsigned, uint64_t> masks;
      for (int32_t owner :
           ::obelisk::schedule::get<
               ::obelisk::schedule::Field::EvalRankedMembers>(child)
               .asArrayRef())
        masks[unsigned(owner) / 64] |= uint64_t{1} << (unsigned(owner) % 64);
      Value pending =
          llvmConstant(builder, parent.getLoc(), builder.getI64Type(), 0);
      for (auto [word, mask] : masks) {
        Value bits = LLVM::AndOp::create(
            builder, parent.getLoc(),
            loadOwnerWord(builder, parent.getLoc(), ready, word),
            llvmConstant(builder, parent.getLoc(), builder.getI64Type(), mask));
        pending = LLVM::OrOp::create(builder, parent.getLoc(), pending, bits);
      }
      Value active = LLVM::ICmpOp::create(
          builder, parent.getLoc(), LLVM::ICmpPredicate::ne, pending,
          llvmConstant(builder, parent.getLoc(), builder.getI64Type(), 0));
      auto *execute = new Block, *next = new Block;
      parent.getBody().push_back(execute);
      parent.getBody().push_back(next);
      LLVM::CondBrOp::create(builder, parent.getLoc(), active, execute,
                             ValueRange{}, next, ValueRange{});
      builder.setInsertionPointToStart(execute);
      if (auto fast = ::obelisk::schedule::get<
              ::obelisk::schedule::Field::EvalDataflowExecutor>(child)) {
        Value proof = LLVM::AddressOfOp::create(
            builder, parent.getLoc(),
            LLVM::LLVMPointerType::get(module.getContext()),
            "__obelisk_eval_promotion_pending_mask_v1");
        Value needed =
            llvmConstant(builder, parent.getLoc(), builder.getI64Type(), 0);
        for (auto [word, mask] : masks) {
          Value bits = LLVM::AndOp::create(
              builder, parent.getLoc(),
              loadOwnerWord(builder, parent.getLoc(), proof, word),
              llvmConstant(builder, parent.getLoc(), builder.getI64Type(),
                           mask));
          needed = LLVM::OrOp::create(builder, parent.getLoc(), needed, bits);
        }
        Value known = LLVM::ICmpOp::create(
            builder, parent.getLoc(), LLVM::ICmpPredicate::eq, needed,
            llvmConstant(builder, parent.getLoc(), builder.getI64Type(), 0));
        auto *collapsed = new Block, *fallback = new Block;
        parent.getBody().push_back(collapsed);
        parent.getBody().push_back(fallback);
        LLVM::CondBrOp::create(builder, parent.getLoc(), known, collapsed,
                               ValueRange{}, fallback, ValueRange{});
        builder.setInsertionPointToStart(collapsed);
        LLVM::CallOp::create(builder, parent.getLoc(), TypeRange{},
                             fast.getValue(), entry->getArguments());
        LLVM::BrOp::create(builder, parent.getLoc(), ValueRange{}, next);
        builder.setInsertionPointToStart(fallback);
      }
      LLVM::CallOp::create(builder, parent.getLoc(), TypeRange{},
                           child.getSymName(), entry->getArguments());
      LLVM::BrOp::create(builder, parent.getLoc(), ValueRange{}, next);
      builder.setInsertionPointToStart(next);
    }
    LLVM::ReturnOp::create(builder, parent.getLoc(), ValueRange{});
    ::obelisk::schedule::set<::obelisk::schedule::Field::EvalGroupChildren>(
        parent, builder.getArrayAttr(references));
    if (module->hasAttr("obelisk.debug.native_timing"))
      llvm::errs() << "obelisk group hierarchy: " << parent.getSymName()
                   << " children=" << refinement.children.size() << '\n';
  }
  // Group refinement and symbol creation are complete. Residual promotion
  // only rewrites its own function and reads globals through a frozen table.
  llvm::erase_if(residuals, [&](const auto &residual) {
    auto function = residual.first;
    return discarded.contains(function) || finalized.contains(function) ||
           ::obelisk::schedule::has<
               ::obelisk::schedule::Field::EvalGroupChildren>(function);
  });
  // A canonical output need not be globally private to keep its intermediate
  // contents in SSA during a native activation. Transition/knownness actions
  // have already been materialized; their ordered notifications are retained.
  // The access inventory flushes at every opaque alias, observer call and exit
  // (LRM 4.3, 4.6, 9.4.2). Unlike group predication, this retains
  // loops/branches.
  for (auto function : module.getOps<LLVM::LLVMFuncOp>())
    if (!function.isExternal() &&
        schedule::has<schedule::Field::EvalRawCaptures>(function) &&
        !schedule::has<schedule::Field::EvalRankedMembers>(function))
      residuals.emplace_back(function, operationLimit);
  DenseSet<Operation *> valueCallers;
  if (failed(
          materializeNativeEvalValueCalls(module, residuals, inputs.globalTypes,
                                          inputs.callFootprints, valueCallers)))
    return failure();
  // Retain the original ready-word representation when no computation was
  // exposed. Promoting readiness alone can add spills without avoiding any
  // state round trip in a completely outlined group.
  llvm::erase_if(residuals, [&](const auto &residual) {
    return whollyOutlinedGroups.contains(residual.first) &&
           !valueCallers.contains(residual.first);
  });
  // Value cores and their canonical wrappers have different footprints. Freeze
  // the new complete closure before any function-local promotion worker runs.
  inputs.callFootprints = buildNativeStateFootprints(module, inputs.globalTypes,
                                                     inputs.metadataOnly);
  report->diagnostics.resize(residuals.size());
  for (auto [index, residual] : llvm::enumerate(residuals))
    inputs.residuals.try_emplace(residual.first,
                                 std::make_pair(residual.second, index));
  return report;
}

std::unique_ptr<Pass> createPromoteNativeGroupFunctionPass() {
  return std::make_unique<PromoteNativeGroupFunctionPass>();
}

} // namespace obelisk::detail

namespace obelisk {
#define GEN_PASS_DEF_MATERIALIZEOBELISKNATIVEEVALGROUPSPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {
struct MaterializeObeliskNativeEvalGroupsPass
    : impl::MaterializeObeliskNativeEvalGroupsPassBase<
          MaterializeObeliskNativeEvalGroupsPass> {
  using MaterializeObeliskNativeEvalGroupsPassBase::
      MaterializeObeliskNativeEvalGroupsPassBase;

  void runOnOperation() override {
    auto report = detail::materializeNativeEvalGroupBodies(
        getOperation(), getAnalysisManager());
    if (failed(report))
      return signalPassFailure();
    if (!(*report)->diagnostics.empty()) {
      OpPassManager functions(ModuleOp::getOperationName());
      functions.nest<LLVM::LLVMFuncOp>().addPass(
          detail::createPromoteNativeGroupFunctionPass());
      if (failed(runPipeline(functions, getOperation())))
        return signalPassFailure();
    }
    for (const auto &diagnostic : (*report)->diagnostics)
      llvm::errs() << diagnostic;
  }
};
} // namespace
} // namespace obelisk
