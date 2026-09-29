//===- SimulationStatePlaneMaterialization.cpp - Native state globals -===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "llvm/ADT/STLExtras.h"

#include <map>

using namespace mlir;

namespace obelisk::detail {

FailureOr<NativeStateLayout> buildNativeStateLayout(ModuleOp module) {
  FailureOr<analysis::NativeStateLayoutAnalysis> analyzed =
      analysis::NativeStateLayoutAnalysis::compute(module);
  if (failed(analyzed))
    return failure();
  NativeStateLayout layout;
  static_cast<analysis::NativeStateLayoutAnalysis &>(layout) =
      std::move(*analyzed);
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

} // namespace obelisk::detail
