//===- SimulationStatePlaneMaterialization.cpp - Native state globals -===//

#include "SimulationToLLVMCoroutinePrivate.h"
#include "obelisk/Analysis/NativeStorageLayout.h"
#include "obelisk/Dialect/Native/NativeDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleOps.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/bit.h"
#include "llvm/IR/DataLayout.h"

#include <map>

using namespace mlir;

namespace obelisk::detail {

Value nbaFieldGEP(OpBuilder &builder, Location location, Value base,
                  uint64_t offset) {
  Value address = byteGEP(builder, location, base, offset);
  auto module =
      builder.getInsertionBlock()->getParentOp()->getParentOfType<ModuleOp>();
  if (module && native::get<native::TwoState>(module))
    native::set<native::NBAField>(address.getDefiningOp(),
                                  builder.getI64IntegerAttr(offset));
  return address;
}

FailureOr<NativeStateLayout>
buildNativeStateLayout(ModuleOp module, const llvm::DataLayout &dataLayout) {
  FailureOr<analysis::NativeStateLayoutAnalysis> analyzed =
      analysis::NativeStateLayoutAnalysis::compute(module);
  if (failed(analyzed))
    return failure();
  NativeStateLayout layout;
  static_cast<analysis::NativeStateLayoutAnalysis &>(layout) =
      std::move(*analyzed);
  layout.packedStorage =
      std::make_shared<PackedStorageLayout>(module, dataLayout);
  return layout;
}

void materializeNativeStatePlanes(ModuleOp module,
                                  const NativeStateLayout &layout) {
  OpBuilder builder(module.getContext());
  Location location = module.getLoc();
  // The private guard word supports the existing unaligned scalar windows.
  uint64_t bytes = (layout.bitCount + 7) / 8 + sizeof(uint64_t);
  Type array = LLVM::LLVMArrayType::get(builder.getI8Type(), bytes);
  for (StringRef name : {"__obelisk_state_value", "__obelisk_state_unknown"}) {
    builder.setInsertionPointToStart(module.getBody());
    auto global =
        LLVM::GlobalOp::create(builder, location, array, false,
                               LLVM::Linkage::Internal, name, Attribute{}, 8);
    auto *block = new Block;
    global.getInitializerRegion().push_back(block);
    builder.setInsertionPointToStart(block);
    LLVM::ReturnOp::create(builder, location,
                           LLVM::ZeroOp::create(builder, location, array));
  }

  struct Initial {
    uint64_t width;
    uint64_t value;
    uint64_t unknown;
  };
  std::map<uint64_t, Initial> roots;
  // IEEE 1800-2023 6.8, Table 6-7: two-state variables start at zero and
  // four-state variables start at X, independently of their packed width.
  for (const auto &bound : layout.bounds)
    roots[bound.offset] = {bound.width, 0, bound.fourState};
  // Variable defaults are part of allocating the state image, before either
  // execution tier starts. Do not express them as process stores: that would
  // introduce fictitious writers and change scheduling/observation proofs.
  llvm::DenseSet<uint64_t> storageOffsets;
  for (const auto &[id, offset] : layout.storageOffsets)
    storageOffsets.insert(offset);
  for (const auto &bound : layout.bounds) {
    if (!storageOffsets.contains(bound.offset))
      continue;
    // IEEE 1800-2023 7.2.2: struct member defaults are part of the type's
    // default value, so such storage starts from its exact default planes.
    if (analysis::hasMemberDefaults(bound.type)) {
      analysis::DefaultStoragePlanes planes =
          analysis::getDefaultStoragePlanes(bound.type);
      roots.erase(bound.offset);
      unsigned width = planes.value.getBitWidth();
      for (unsigned position = 0; position != width;) {
        bool value = planes.value[position];
        bool unknown = planes.unknown[position];
        unsigned end = position + 1;
        while (end != width && planes.value[end] == value &&
               planes.unknown[end] == unknown)
          ++end;
        roots.emplace(bound.offset + position,
                      Initial{end - position, value, unknown});
        position = end;
      }
      continue;
    }
    if (!bound.fourState)
      continue;
    APInt mask = analysis::getDefaultUnknownMask(bound.type);
    if (mask.isAllOnes())
      continue;
    roots.erase(bound.offset);
    // Enumerate set runs word by word. Shifting the entire APInt for every
    // narrow array element would make initialization planning quadratic.
    for (unsigned wordIndex = 0; wordIndex < mask.getNumWords(); ++wordIndex) {
      uint64_t word = mask.getRawData()[wordIndex];
      while (word) {
        unsigned low = llvm::countr_zero(word);
        unsigned width = llvm::countr_one(word >> low);
        roots.emplace(bound.offset + uint64_t(wordIndex) * 64 + low,
                      Initial{width, 0, 1});
        word &= width == 64 ? 0 : ~(((uint64_t{1} << width) - 1) << low);
      }
    }
  }
  for (const auto &driver : layout.driverLayouts)
    roots.at(driver.offset).value = !driver.initialX;
  auto resolve = [](Initial state, sim::NetResolutionKind resolution) {
    // IEEE 1800-2023 6.6.5, 6.6.6, 6.7.1: undriven nets start at Z,
    // pull/supply nets at their implicit value, and trireg at X.
    switch (resolution) {
    case sim::NetResolutionKind::Tri0:
    case sim::NetResolutionKind::Supply0:
      state.value = state.unknown = 0;
      break;
    case sim::NetResolutionKind::Tri1:
    case sim::NetResolutionKind::Supply1:
      state.value = 1;
      state.unknown = 0;
      break;
    case sim::NetResolutionKind::TriReg:
      state.value = 0;
      break;
    default:
      break;
    }
    return state;
  };
  llvm::DenseMap<uint64_t,
                 SmallVector<std::pair<uint64_t, sim::NetResolutionKind>>>
      overrides;
  // Connectivity only records connected bits. Visit those sparse facts, not
  // every bit of an otherwise uniform net or a large unpacked variable.
  for (const auto &[logical, canonical] : layout.connectivityCanonical) {
    auto found = layout.connectivityResolutions.find(canonical);
    if (found != layout.connectivityResolutions.end())
      overrides[logical.first].push_back({logical.second, found->second});
  }
  for (const auto &net : layout.netLayouts) {
    Initial base = roots.at(net.offset);
    base.value = net.fourState;
    Initial ordinary = resolve(base, net.resolution);
    auto &bits = overrides[net.id];
    llvm::sort(bits, [](auto a, auto b) { return a.first < b.first; });
    SmallVector<std::pair<uint64_t, Initial>> pieces;
    auto append = [&](uint64_t offset, uint64_t width, Initial state) {
      if (!pieces.empty()) {
        auto &[previous, value] = pieces.back();
        if (previous + value.width == offset && value.value == state.value &&
            value.unknown == state.unknown) {
          value.width += width;
          return;
        }
      }
      pieces.push_back({offset, {width, state.value, state.unknown}});
    };
    roots.erase(net.offset);
    uint64_t position = 0;
    for (auto [bit, resolution] : bits) {
      Initial effective = resolve(base, resolution);
      if (effective.value == ordinary.value &&
          effective.unknown == ordinary.unknown)
        continue;
      if (position < bit)
        append(net.offset + position, bit - position, ordinary);
      append(net.offset + bit, 1, effective);
      position = bit + 1;
    }
    if (position < net.width)
      append(net.offset + position, net.width - position, ordinary);
    for (auto [offset, state] : pieces)
      roots.emplace(offset, state);
  }

  SmallVector<uint64_t> fills;
  for (auto [offset, state] : roots) {
    if (native::get<native::TwoState>(module)) {
      state.value &= !state.unknown;
      state.unknown = 0;
    }
    if (!state.width || (!state.value && !state.unknown))
      continue;
    size_t size = fills.size();
    if (size && fills[size - 4] + fills[size - 3] == offset &&
        fills[size - 2] == state.value && fills[size - 1] == state.unknown) {
      fills[size - 3] += state.width;
      continue;
    }
    llvm::append_range(fills, ArrayRef<uint64_t>{offset, state.width,
                                                 state.value, state.unknown});
  }
  builder.setInsertionPointToStart(module.getBody());
  auto tensor =
      RankedTensorType::get({int64_t(fills.size())}, builder.getI64Type());
  LLVM::GlobalOp::create(
      builder, location,
      LLVM::LLVMArrayType::get(builder.getI64Type(), fills.size()), true,
      LLVM::Linkage::Internal, "__obelisk_state_initializers_v1",
      DenseIntElementsAttr::get(tensor, ArrayRef<uint64_t>(fills)), 8);
}

void assignUnknownSummary(ModuleOp module, NativeStateLayout &layout) {
  layout.unknownSummaryBits.clear();
  layout.unknownSummaryRoots.clear();
  layout.unknownSummaryPatterns.clear();
  layout.unknownSummaryGates = 0;
  layout.steadyUnknown.clear();
  if (native::get<native::TwoState>(module))
    return;
  module.walk([&](sim::SimStorageDeclOp storage) {
    auto ranges = sim::get<sim::metadata::SteadyUnknown>(storage);
    auto handle = layout.storage.find(storage.getId());
    obelisk_rt_stable_handle_v1 decoded{};
    if (!ranges || handle == layout.storage.end() ||
        !obelisk_rt_stable_handle_decode(handle->second, &decoded))
      return;
    auto &pattern = layout.steadyUnknown[decoded.id];
    for (size_t index = 0; index + 1 < size_t(ranges.size()); index += 2)
      pattern.push_back({uint64_t(ranges[index]), uint64_t(ranges[index + 1])});
  });
  // One summary bit per four-state root a known.roots gate tests, numbered by
  // first test. A reference without a fixed root gets none; its gate fails.
  // Each gate gets its own backoff state.
  llvm::DenseMap<uint32_t, bool> fourState;
  for (const auto &bound : layout.bounds)
    fourState[bound.handleID] = bound.fourState;
  Builder builder(module.getContext());
  module.walk([&](schedule::NativeKnownRootsOp gate) {
    schedule::set<schedule::Field::NativeKnownGate>(
        gate, builder.getI64IntegerAttr(layout.unknownSummaryGates++));
    for (Value reference : gate.getReferences()) {
      auto storage = reference.getDefiningOp<sim::SimContextStorageOp>();
      auto handle =
          storage ? layout.storage.find(storage.getId()) : layout.storage.end();
      obelisk_rt_stable_handle_v1 decoded{};
      if (handle == layout.storage.end() ||
          !obelisk_rt_stable_handle_decode(handle->second, &decoded) ||
          decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC)
        continue;
      if (!fourState.lookup(decoded.id)) {
        layout.unknownSummaryBits.try_emplace(decoded.id,
                                              NativeStateLayout::twoStateRoot);
        continue;
      }
      if (layout.unknownSummaryBits
              .try_emplace(decoded.id, layout.unknownSummaryRoots.size())
              .second)
        layout.unknownSummaryRoots.push_back(decoded.id);
    }
  });
  // Patterns hold absolute plane ranges, clipped to their root.
  llvm::DenseMap<uint32_t, const NativeStateLayout::Bound *> bounds;
  for (const auto &bound : layout.bounds)
    bounds[bound.handleID] = &bound;
  for (uint32_t handle : layout.unknownSummaryRoots) {
    auto &pattern = layout.unknownSummaryPatterns.emplace_back();
    const NativeStateLayout::Bound *bound = bounds.lookup(handle);
    for (auto [low, width] : layout.steadyUnknown.lookup(handle))
      if (low < bound->width)
        llvm::append_range(
            pattern,
            ArrayRef<uint64_t>{bound->offset + low,
                               std::min<uint64_t>(width, bound->width - low)});
  }
  materializeUnknownSummary(module, layout);
}

// Lowering shares its address calculations with mixed-state designs. Once
// every producer is lowered, remove the implicit zero plane in one place.
// In particular, a derived unknown pointer must become null too: keeping a
// GEP of null would give the runtime a non-null, invalid plane address.
void eliminateTwoStateUnknownPlane(ModuleOp module) {
  if (!native::get<native::TwoState>(module))
    return;
  constexpr uint64_t unknownOffset =
      offsetof(obelisk_rt_generated_nba_accumulator_256, unknown);
  constexpr uint64_t afterUnknown =
      offsetof(obelisk_rt_generated_nba_accumulator_256, write_mask);
  constexpr uint64_t unknownBytes = afterUnknown - unknownOffset;
  static_assert(unknownBytes ==
                sizeof(obelisk_rt_generated_nba_accumulator_256::unknown));
  SmallVector<LLVM::GlobalOp> globals;
  llvm::DenseSet<StringRef> names;
  for (auto global : module.getOps<LLVM::GlobalOp>())
    if (global.getSymName() == "__obelisk_state_unknown" ||
        global.getSymName().starts_with("__obelisk_eval_nba_unknown_")) {
      globals.push_back(global);
      names.insert(global.getSymName());
    }
  llvm::DenseSet<Value> pointers;
  SmallVector<Value> worklist;
  module.walk([&](LLVM::AddressOfOp address) {
    if (names.contains(address.getGlobalName())) {
      pointers.insert(address.getResult());
      worklist.push_back(address.getResult());
    } else if (address.getGlobalName() == "__obelisk_continuous_unknown") {
      for (Operation *user : address.getResult().getUsers())
        if (auto load = dyn_cast<LLVM::LoadOp>(user))
          if (pointers.insert(load.getResult()).second)
            worklist.push_back(load.getResult());
    }
  });
  // The generated accumulator's value, write mask and control fields keep
  // their logical addresses during planning. Physically omit its 32-byte
  // unknown array and close that hole only after pending-frame rewriting.
  module.walk([&](LLVM::GEPOp gep) {
    auto field = native::get<native::NBAField>(gep);
    if (!field)
      return;
    native::remove<native::NBAField>(gep);
    uint64_t offset = field.getValue().getZExtValue();
    if (offset >= unknownOffset && offset < afterUnknown) {
      if (pointers.insert(gep.getResult()).second)
        worklist.push_back(gep.getResult());
    } else if (offset >= afterUnknown) {
      // nbaFieldGEP addresses the record as bytes with one constant index.
      assert(gep.getDynamicIndices().empty() &&
             gep.getRawConstantIndices().size() == 1 &&
             "NBA field address is not a constant byte offset");
      gep.setRawConstantIndices(
          ArrayRef<int32_t>{int32_t(offset - unknownBytes)});
    }
  });
  for (auto accumulator : module.getOps<LLVM::GlobalOp>()) {
    if (!accumulator.getSymName().starts_with("__obelisk_aot_nba_accumulator_"))
      continue;
    auto type = LLVM::LLVMArrayType::get(
        IntegerType::get(module.getContext(), 8),
        sizeof(obelisk_rt_generated_nba_accumulator_256) - unknownBytes);
    accumulator.setGlobalType(type);
    accumulator.getInitializerRegion().walk(
        [&](LLVM::ZeroOp zero) { zero.getResult().setType(type); });
  }
  for (size_t i = 0; i != worklist.size(); ++i)
    for (Operation *user : worklist[i].getUsers())
      if (isa<LLVM::GEPOp, LLVM::BitcastOp>(user) &&
          pointers.insert(user->getResult(0)).second)
        worklist.push_back(user->getResult(0));
  SmallVector<Operation *> erase;
  module.walk([&](Operation *op) {
    OpBuilder builder(op);
    if (auto load = dyn_cast<LLVM::LoadOp>(op)) {
      if (pointers.contains(load.getAddr())) {
        load.getResult().replaceAllUsesWith(
            LLVM::ZeroOp::create(builder, op->getLoc(), load.getType()));
        erase.push_back(op);
      }
    } else if (auto store = dyn_cast<LLVM::StoreOp>(op)) {
      if (pointers.contains(store.getAddr()))
        erase.push_back(op);
    } else if (auto copy = dyn_cast<LLVM::MemcpyOp>(op)) {
      if (pointers.contains(copy.getDst())) {
        erase.push_back(op);
      } else if (pointers.contains(copy.getSrc())) {
        LLVM::MemsetOp::create(
            builder, op->getLoc(), copy.getDst(),
            LLVM::ZeroOp::create(builder, op->getLoc(), builder.getI8Type()),
            copy.getLen(), copy.getIsVolatile());
        erase.push_back(op);
      }
    } else if (auto set = dyn_cast<LLVM::MemsetOp>(op)) {
      if (pointers.contains(set.getDst()))
        erase.push_back(op);
    }
  });
  for (Operation *op : erase)
    op->erase();
  for (Value pointer : llvm::reverse(worklist)) {
    Operation *op = pointer.getDefiningOp();
    OpBuilder builder(op);
    pointer.replaceAllUsesWith(
        LLVM::ZeroOp::create(builder, op->getLoc(), pointer.getType()));
    op->erase();
  }
  for (auto global : globals)
    global.erase();
}

void materializeUnknownSummary(ModuleOp module,
                               const NativeStateLayout &layout) {
  if (layout.unknownSummaryRoots.empty())
    return;
  OpBuilder builder = OpBuilder::atBlockBegin(module.getBody());
  Location location = module.getLoc();
  Type i64 = builder.getI64Type();
  auto global = [&](StringRef name, ArrayRef<uint64_t> words, bool constant) {
    auto tensor = RankedTensorType::get({int64_t(words.size())}, i64);
    LLVM::GlobalOp::create(builder, location,
                           LLVM::LLVMArrayType::get(i64, words.size()),
                           constant, LLVM::Linkage::Internal, name,
                           DenseIntElementsAttr::get(tensor, words), 8);
  };
  llvm::DenseMap<uint32_t, const NativeStateLayout::Bound *> bounds;
  for (const auto &bound : layout.bounds)
    bounds[bound.handleID] = &bound;
  // obelisk_rt_native_unknown_root_v1 records: offset, width and the
  // (first, count) slice of pattern ranges, packed little-endian.
  SmallVector<uint64_t> roots, patterns;
  for (auto [bit, handle] : llvm::enumerate(layout.unknownSummaryRoots)) {
    const NativeStateLayout::Bound *bound = bounds.lookup(handle);
    ArrayRef<uint64_t> ranges;
    if (bit < layout.unknownSummaryPatterns.size())
      ranges = layout.unknownSummaryPatterns[bit];
    uint64_t slice = patterns.size() / 2 | uint64_t(ranges.size() / 2) << 32;
    llvm::append_range(roots,
                       ArrayRef<uint64_t>{bound->offset, bound->width, slice});
    llvm::append_range(patterns, ranges);
  }
  // Until the generated code first tests a root, it may hold X/Z (IEEE
  // 1800-2023 6.8); the runtime confirms each bit when it binds the summary.
  uint64_t count = layout.unknownSummaryRoots.size();
  SmallVector<uint64_t> bits((count + 63) / 64, ~uint64_t{0});
  if (count % 64)
    bits.back() = (uint64_t{1} << (count % 64)) - 1;
  global(unknownSummaryName, bits, false);
  global(unknownSummaryRootsName, roots, true);
  // A zero-length array cannot be addressed; the runtime reads none of it.
  if (patterns.empty())
    patterns.assign(2, 0);
  global(unknownSummaryPatternsName, patterns, true);
  global(unknownSummaryBackoffName,
         SmallVector<uint64_t>(
             std::max<uint32_t>(layout.unknownSummaryGates, 1), 0),
         false);
  Type pointer = LLVM::LLVMPointerType::get(module.getContext());
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_state_refresh_unknown_summary",
      builder.getI32Type(), {pointer, pointer, i64, pointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_native_state_mark_unknown",
                           LLVM::LLVMVoidType::get(module.getContext()),
                           {pointer, i64, i64});
}

} // namespace obelisk::detail
