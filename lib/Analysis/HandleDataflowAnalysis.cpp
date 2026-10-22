//===- HandleDataflowAnalysis.cpp - SSA reference dataflow
//------------------===//
#include "obelisk/Analysis/HandleDataflowAnalysis.h"
#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/IntegerRangeAnalysis.h"
#include "mlir/Analysis/DataFlow/SparseAnalysis.h"
#include "obelisk/Analysis/LogicBitAnalysis.h"
#include "obelisk/Analysis/NetConnectivityAnalysis.h"
#include "obelisk/Analysis/ScopedIntegerRangeAnalysis.h"
#include "obelisk/Dialect/Schedule/ScheduleOps.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/TypeSwitch.h"

#include <climits>
#include <memory>
#include <mutex>

using namespace mlir;
namespace obelisk::analysis {
namespace {
schedule::ComputeResourceKind resourceKind(Type type) {
  if (isa<sim::RefType>(type))
    return schedule::ComputeResourceKind::Storage;
  if (isa<sim::NetType, sim::DriverType>(type))
    return schedule::ComputeResourceKind::Net;
  if (isa<sim::EventType>(type))
    return schedule::ComputeResourceKind::Event;
  return schedule::ComputeResourceKind::Unknown;
}
/// Known-conditional bounds proofs name at most this many selectors; more
/// forget the proof rather than grow every reference fact.
constexpr size_t maximumKnownSelectors = 4;
struct ReferenceValue {
  bool initialized = false;
  HandleFact target;
  HandleCertificate proof;
  bool operator==(const ReferenceValue &rhs) const {
    return initialized == rhs.initialized && target == rhs.target &&
           proof == rhs.proof;
  }
  static ReferenceValue unknown() { return {true, {}, {}}; }
  static ReferenceValue join(ReferenceValue lhs, ReferenceValue rhs) {
    if (!lhs.initialized)
      return rhs;
    if (!rhs.initialized || lhs == rhs)
      return lhs;
    auto &a = lhs.target;
    const auto &b = rhs.target;
    if (a.resource == schedule::ComputeResourceKind::Unknown ||
        a.resource != b.resource || a.descriptor != b.descriptor ||
        a.formal != b.formal || a.rootWidth != b.rootWidth)
      return unknown();
    bool exact =
        a.low == b.low && a.width == b.width && !a.dynamic && !b.dynamic;
    bool inBounds = a.low <= a.rootWidth && a.width <= a.rootWidth - a.low &&
                    b.low <= b.rootWidth && b.width <= b.rootWidth - b.low;
    uint64_t low = inBounds ? std::min(a.low, b.low) : 0;
    uint64_t end =
        inBounds ? std::max(a.low + a.width, b.low + b.width) : a.rootWidth;
    a.low = low;
    a.width = end - low;
    // Effect consumers need the bounded union of static views. A varying
    // address at a CFG join is not an unbounded dynamic selector; the separate
    // lowering certificate loses its constant-address proof below.
    a.dynamic |= b.dynamic || !inBounds;
    lhs.proof.constantAddress &= rhs.proof.constantAddress && exact;
    lhs.proof.inBounds &= rhs.proof.inBounds;
    lhs.proof.inBoundsIfKnown &= rhs.proof.inBoundsIfKnown;
    for (Value selector : rhs.proof.knownSelectors)
      if (lhs.proof.inBoundsIfKnown &&
          !llvm::is_contained(lhs.proof.knownSelectors, selector))
        lhs.proof.knownSelectors.push_back(selector);
    if (!lhs.proof.inBoundsIfKnown ||
        lhs.proof.knownSelectors.size() > maximumKnownSelectors) {
      lhs.proof.inBoundsIfKnown = false;
      lhs.proof.knownSelectors.clear();
    }
    if (!lhs.proof.laneKnown || !rhs.proof.laneKnown ||
        lhs.proof.low != rhs.proof.low || lhs.proof.width != rhs.proof.width ||
        lhs.proof.stride != rhs.proof.stride ||
        lhs.proof.clipped != rhs.proof.clipped) {
      lhs.proof.laneKnown = false;
      lhs.proof.low = lhs.proof.width = lhs.proof.stride = 0;
      lhs.proof.clipped = false;
    }
    if (lhs.proof.directDynamicSelection != rhs.proof.directDynamicSelection)
      lhs.proof.directDynamicSelection = {};
    return lhs;
  }
  void print(raw_ostream &os) const {
    if (!initialized)
      os << "bottom";
    else
      os << "resource=" << unsigned(target.resource) << " low=" << target.low
         << " width=" << target.width << " dynamic=" << target.dynamic;
  }
};
using ReferenceLattice = dataflow::Lattice<ReferenceValue>;
class ReferenceAnalysis
    : public dataflow::SparseForwardDataFlowAnalysis<ReferenceLattice> {
public:
  ReferenceAnalysis(DataFlowSolver &solver,
                    const DenseMap<uint64_t, uint64_t> &driverNets,
                    ScopedIntegerRangeAnalysis *scopedRanges, bool logicBits)
      : SparseForwardDataFlowAnalysis(solver), driverNets(driverNets),
        scopedRanges(scopedRanges), logicBits(logicBits) {}
  LogicalResult visitOperation(Operation *op,
                               ArrayRef<const ReferenceLattice *> operands,
                               ArrayRef<ReferenceLattice *> results) override {
    auto put = [&](ReferenceValue value) {
      for (auto *result : results)
        propagateIfChanged(result, result->join(value));
    };
    auto declare = [&](Value result, schedule::ComputeResourceKind resource,
                       std::optional<uint64_t> descriptor) {
      auto width = sim::getProvenanceSpan(result.getType());
      if (!width || resource == schedule::ComputeResourceKind::Unknown)
        return put(ReferenceValue::unknown());
      ReferenceValue value{
          true, {resource, descriptor, {}, 0, *width, *width}, {}};
      value.proof = {resource == schedule::ComputeResourceKind::Storage &&
                         bool(descriptor),
                     true,
                     0,
                     *width,
                     0,
                     false,
                     {}};
      value.proof.inBounds = value.proof.inBoundsIfKnown = true;
      put(value);
    };
    auto forgetSelection = [&]() {
      ReferenceValue value = operands.front()->getValue();
      if (!value.initialized)
        return;
      value.target.low = 0;
      value.target.width = value.target.rootWidth;
      value.target.dynamic = true;
      value.proof = {};
      put(value);
    };
    auto select = [&](Value result, std::optional<uint64_t> offset,
                      bool array) {
      ReferenceValue value = operands.front()->getValue();
      auto width = sim::getProvenanceSpan(result.getType());
      if (!value.initialized)
        return;
      if (value.target.resource == schedule::ComputeResourceKind::Unknown ||
          !width)
        return put(ReferenceValue::unknown());
      auto &p = value.target;
      auto &c = value.proof;
      if (offset && (p.low > p.rootWidth || *offset > p.rootWidth - p.low ||
                     *width > p.rootWidth - p.low - *offset))
        return forgetSelection();
      if (offset) {
        auto parentWidth = sim::getProvenanceSpan(op->getOperand(0).getType());
        if (!parentWidth || *offset > *parentWidth ||
            *width > *parentWidth - *offset || p.width < *parentWidth)
          return forgetSelection();
        // A CFG join can describe several addresses with one bounded hull.
        // Apply the selection to every possible view, preserving that hull.
        // Dynamic clipped views retain their full conservative effect range.
        if (!p.dynamic) {
          p.low += *offset;
          p.width -= *parentWidth - *width;
        }
        if (c.laneKnown && !c.clipped && *offset <= c.width &&
            *width <= c.width - *offset) {
          c.low += *offset;
          c.width = *width;
        } else {
          c.laneKnown = false;
          c.low = c.width = c.stride = 0;
          c.clipped = false;
        }
        // LRM 7.4.5, 11.5.1: a fixed field of an indexed element retains the
        // bounded dynamic selection certificate. Lowering combines its constant
        // field offset with the selected element before accessing the canonical
        // planes.
        if (c.directDynamicSelection)
          c.directDynamicSelection = result;
      } else {
        bool selectorValid = false;
        Value selector = op->getOperand(1);
        auto selectorWidth = sim::getPackedWidth(selector.getType());
        Type parentType =
            llvm::TypeSwitch<Type, Type>(op->getOperand(0).getType())
                .Case<sim::RefType, sim::DriverType>(
                    [](auto type) { return type.getElementType(); });
        // The inclusive signed selector values that address storage. Dynamic
        // extracts and word views use physical bit offsets, including when
        // their parent is an array. Declaration indices apply only to
        // ref.array_element.
        std::optional<std::pair<int64_t, int64_t>> validSelectors;
        if (!array) {
          auto parentWidth = sim::getProvenanceSpan(parentType);
          if (parentWidth && *width <= *parentWidth &&
              *parentWidth - *width <= uint64_t(INT64_MAX))
            validSelectors = {0, int64_t(*parentWidth - *width)};
        } else if (auto arrayType =
                       dyn_cast<sim::UnpackedArrayType>(parentType)) {
          validSelectors = {
              std::min(arrayType.getLeft(), arrayType.getRight()),
              std::max(arrayType.getLeft(), arrayType.getRight())};
        } else if (auto arrayType =
                       dyn_cast<sim::PackedArrayType>(parentType)) {
          validSelectors = {
              std::min(arrayType.getLeft(), arrayType.getRight()),
              std::max(arrayType.getLeft(), arrayType.getRight())};
        }
        unsigned comparisonWidth =
            std::max<unsigned>(selectorWidth.value_or(0), 65);
        auto covers = [&](const APInt &low, const APInt &high) {
          return validSelectors &&
                 low.sext(comparisonWidth)
                     .sge(
                         APInt(comparisonWidth, validSelectors->first, true)) &&
                 high.sext(comparisonWidth)
                     .sle(APInt(comparisonWidth, validSelectors->second, true));
        };
        if (selectorWidth && *selectorWidth <= 128 &&
            isa<IntegerType, sim::LogicType>(selector.getType())) {
          std::optional<ConstantIntRanges> bounds;
          if (isa<IntegerType>(selector.getType())) {
            auto *range = getOrCreateFor<dataflow::IntegerValueRangeLattice>(
                getProgramPointAfter(op), selector);
            if (range->getValue().isUninitialized())
              return;
            bounds = range->getValue().getValue();
          }
          if (scopedRanges) {
            const auto *scoped = getOrCreateFor<ScopedRangeLattice>(
                getProgramPointAfter(op), getProgramPointBefore(op));
            if (!scoped->reachable)
              return;
            if (auto constraint = scopedRanges->getRange(selector, *scoped))
              bounds = bounds ? bounds->intersection(*constraint) : *constraint;
          }
          if (bounds) {
            selectorValid = covers(bounds->smin(), bounds->smax());
            if (!selectorValid && validSelectors && scopedRanges) {
              const auto *scoped = getOrCreateFor<ScopedRangeLattice>(
                  getProgramPointAfter(op), getProgramPointBefore(op));
              selectorValid = scopedRanges->proveBounds(
                  selector, *scoped,
                  APInt(comparisonWidth, validSelectors->first, true),
                  APInt(comparisonWidth, validSelectors->second, true));
            }
          }
        }
        // A four-state selector without that unconditional proof can still
        // address storage for every value it takes once known. Its
        // selections are then invalid only through X/Z bits (IEEE 1800-2023
        // 7.4.6, 11.5.1), which consumers must exclude separately.
        bool validIfKnown = selectorValid;
        if (!selectorValid && logicBits && selectorWidth &&
            isa<sim::LogicType>(selector.getType())) {
          const LogicBitFacts &bits = getOrCreateFor<LogicBitLattice>(
                                          getProgramPointAfter(op), selector)
                                          ->getValue();
          if (!bits.initialized)
            return;
          if (bits.known.getBitWidth() == *selectorWidth) {
            auto [low, high] = getKnownValueSignedRange(bits);
            validIfKnown = covers(low, high);
          }
        }
        c.inBounds &= selectorValid;
        c.inBoundsIfKnown &= validIfKnown;
        if (c.inBoundsIfKnown && !selectorValid &&
            !llvm::is_contained(c.knownSelectors, selector)) {
          if (c.knownSelectors.size() == maximumKnownSelectors) {
            c.inBoundsIfKnown = false;
            c.knownSelectors.clear();
          } else {
            c.knownSelectors.push_back(selector);
          }
        }
        bool wholeRoot = !p.dynamic && p.low == 0 && p.width == p.rootWidth &&
                         c.constantAddress;
        // Array selectors validate against their declared parent range before
        // adding its fixed root offset (LRM 7.4.5). A bounded array field is
        // therefore as direct as a whole-root array. Packed part-selects still
        // require the whole-root proof because they can overlap a boundary.
        bool boundedArray = array && !p.dynamic && c.constantAddress &&
                            c.laneKnown && !c.clipped;
        auto stride = array ? sim::getArrayElementStorageStride(parentType)
                            : std::optional<uint64_t>{1};
        bool lane = !p.dynamic && c.laneKnown && *width && stride &&
                    (!array || c.width % *stride == 0);
        c.constantAddress = false;
        c.laneKnown = lane;
        if (lane) {
          c.width = *width;
          c.stride = *stride;
          c.clipped = !array;
        } else {
          c.low = c.width = c.stride = 0;
          c.clipped = false;
        }
        c.directDynamicSelection =
            (wholeRoot || boundedArray || c.directDynamicSelection) ? result
                                                                    : Value{};
        p.dynamic = true;
      }
      put(value);
    };
    llvm::TypeSwitch<Operation *>(op)
        .Case<sim::SimContextStorageOp>([&](auto x) {
          declare(x.getResult(), schedule::ComputeResourceKind::Storage,
                  x.getId());
        })
        .Case<sim::SimContextNetOp>([&](auto x) {
          declare(x.getResult(), schedule::ComputeResourceKind::Net, x.getId());
        })
        .Case<sim::SimContextEventOp>([&](auto x) {
          declare(x.getResult(), schedule::ComputeResourceKind::Event,
                  x.getId());
        })
        .Case<sim::SimContextDriverOp>([&](auto x) {
          auto net = driverNets.find(x.getId());
          declare(
              x.getResult(),
              net == driverNets.end() ? schedule::ComputeResourceKind::Unknown
                                      : schedule::ComputeResourceKind::Net,
              net == driverNets.end() ? std::nullopt
                                      : std::optional<uint64_t>(net->second));
        })
        .Case<sim::SimRefAllocOp>([&](auto x) {
          declare(x.getResult(), schedule::ComputeResourceKind::Local, {});
        })
        .Case<sim::SimRefExtractOp, sim::SimNetExtractOp,
              sim::SimDriverExtractOp>(
            [&](auto x) { select(x.getResult(), x.getLowBit(), false); })
        .Case<sim::SimRefDynExtractOp, schedule::NativeWordViewOp,
              sim::SimDriverDynExtractOp>(
            [&](auto x) { select(x.getResult(), {}, false); })
        .Case<sim::SimRefArrayElementOp, sim::SimDriverArrayElementOp>(
            [&](auto x) { select(x.getResult(), {}, true); })
        .Case<sim::SimRefSubelementOp, sim::SimDriverSubelementOp>([&](auto x) {
          Type type = x.getInput().getType().getElementType();
          uint64_t low = 0;
          for (int64_t index : x.getIndices()) {
            if (index < 0 || uint64_t(index) > UINT_MAX)
              return forgetSelection();
            auto child = sim::getAggregateProvenanceSubelement(type, index);
            if (!child || child->first > UINT64_MAX - low)
              return forgetSelection();
            low += child->first;
            type = sim::getAggregateElementType(type, index);
          }
          select(x.getResult(), low, false);
        })
        .Default([&](Operation *) { put(ReferenceValue::unknown()); });
    return success();
  }

private:
  void setToEntryState(ReferenceLattice *lattice) override {
    ReferenceValue value = ReferenceValue::unknown();
    auto argument = dyn_cast<BlockArgument>(lattice->getAnchor());
    auto function =
        argument ? dyn_cast<sim::SimFuncOp>(argument.getOwner()->getParentOp())
                 : sim::SimFuncOp{};
    if (!function || argument.getOwner() != &function.getBody().front())
      return propagateIfChanged(lattice, lattice->join(value));
    unsigned index = argument.getArgNumber();
    auto resource = resourceKind(argument.getType());
    auto width = sim::getProvenanceSpan(argument.getType());
    if (resource == schedule::ComputeResourceKind::Unknown || !width ||
        (isa<sim::DriverType>(argument.getType()) &&
         function.getArgAttr(index,
                             sim::metadata::UserNetDriver::getNameStr())))
      return propagateIfChanged(lattice, lattice->join(value));
    value.target = {resource, {}, {}, 0, *width, *width};
    auto rootType = function.getArgAttrOfType<TypeAttr>(
        index, sim::metadata::DescriptorRootType::getNameStr());
    if (rootType) {
      auto root = sim::getProvenanceSpan(rootType.getValue());
      auto low = function.getArgAttrOfType<IntegerAttr>(
          index, sim::metadata::DescriptorLow::getNameStr());
      if (!root || !low || low.getValue().isNegative() ||
          low.getValue().getActiveBits() > 64 || low.getUInt() > *root ||
          *width > *root - low.getUInt())
        return propagateIfChanged(lattice,
                                  lattice->join(ReferenceValue::unknown()));
      value.target.low = low.getUInt();
      value.target.rootWidth = *root;
    }
    auto capture = function.getArgAttrOfType<sim::CaptureKindAttr>(
        index, sim::metadata::CaptureKind::getNameStr());
    auto descriptor = function.getArgAttrOfType<IntegerAttr>(
        index, sim::metadata::DescriptorId::getNameStr());
    bool observer = function.getEntryKind() == sim::EntryKind::Observer &&
                    index != 0 && capture &&
                    capture.getValue() == sim::CaptureKind::Value;
    if (capture && (capture.getValue() == sim::CaptureKind::Formal || observer))
      value.target.formal = index;
    else if (descriptor) {
      if (descriptor.getValue().isNegative() ||
          descriptor.getValue().getActiveBits() > 64)
        return propagateIfChanged(lattice,
                                  lattice->join(ReferenceValue::unknown()));
      value.target.descriptor = descriptor.getUInt();
      if (isa<sim::DriverType>(argument.getType())) {
        auto net = driverNets.find(*value.target.descriptor);
        if (net == driverNets.end())
          return propagateIfChanged(lattice,
                                    lattice->join(ReferenceValue::unknown()));
        value.target.descriptor = net->second;
      }
    }
    value.proof = {false, true, value.target.low, value.target.width, 0,
                   false, {}};
    value.proof.inBounds = value.proof.inBoundsIfKnown = true;
    propagateIfChanged(lattice, lattice->join(value));
  }
  const DenseMap<uint64_t, uint64_t> &driverNets;
  ScopedIntegerRangeAnalysis *scopedRanges;
  /// Whether the solver also derives logic bit facts for selectors.
  bool logicBits;
};

static sim::SimDesignOp getDriverDesign(sim::SimFuncOp function) {
  // Only inspect the owned function. Storage-only convenience queries should
  // not walk all design declarations for each function.
  WalkResult result = function.walk([](Operation *op) {
    for (Value value : op->getResults())
      if (isa<sim::DriverType>(value.getType()))
        return WalkResult::interrupt();
    for (Region &region : op->getRegions())
      for (Block &block : region)
        for (BlockArgument argument : block.getArguments())
          if (isa<sim::DriverType>(argument.getType()))
            return WalkResult::interrupt();
    return WalkResult::advance();
  });
  return result.wasInterrupted() ? function->getParentOfType<sim::SimDesignOp>()
                                 : sim::SimDesignOp();
}

/// Exact input revision of one function analysis. MLIR's fingerprint covers
/// operation, block and value identities, attributes, properties, operands,
/// successors and result types. Operation names are added because a new
/// operation can reuse an erased one's address, and block argument types
/// because in-place retyping keeps the argument identity. The solver reads
/// nothing else except the design's driver-to-net normalization.
struct HandleRevision {
  OperationFingerPrint body;
  llvm::hash_code shape;
  uint64_t drivers;
  bool operator==(const HandleRevision &other) const {
    return body == other.body && shape == other.shape &&
           drivers == other.drivers;
  }
};

/// Many independent passes and analyses each construct a
/// HandleDataflowAnalysis and solve the same unchanged functions, small
/// callees once per call site. Every function reuses its solved result while
/// its revision is unchanged; any IR edit changes the key. Checking a revision
/// walks the body once, far cheaper than solving it. Only the latest revision
/// of each function is kept.
class HandleResultCache {
public:
  std::shared_ptr<const HandleDataflowResult>
  lookup(Operation *function, const HandleRevision &revision) {
    std::lock_guard<std::mutex> lock(mutex);
    auto found = entries.find(function);
    return found != entries.end() && found->second.revision == revision
               ? found->second.result
               : nullptr;
  }
  void insert(Operation *function, const HandleRevision &revision,
              std::shared_ptr<const HandleDataflowResult> result) {
    std::lock_guard<std::mutex> lock(mutex);
    Entry entry{revision, std::move(result)};
    auto [found, inserted] = entries.try_emplace(function, entry);
    if (!inserted)
      found->second = std::move(entry);
  }

private:
  struct Entry {
    HandleRevision revision;
    std::shared_ptr<const HandleDataflowResult> result;
  };
  std::mutex mutex;
  DenseMap<Operation *, Entry> entries;
};
HandleResultCache &getHandleResultCache() {
  static HandleResultCache cache;
  return cache;
}
} // namespace

HandleDataflowAnalysis::HandleDataflowAnalysis(sim::SimDesignOp design) {
  // Build the immutable normalization snapshot before parallel consumers run.
  if (design) {
    isolatedDrivenNets = getIsolatedSingleDriverNets(design);
    for (auto driver : design.getOps<sim::SimDriverDeclOp>())
      driverNets[driver.getId()] = driver.getNetId();
  }
  SmallVector<std::pair<uint64_t, uint64_t>> drivers(driverNets.begin(),
                                                     driverNets.end());
  llvm::sort(drivers);
  driverDigest = llvm::hash_combine_range(drivers.begin(), drivers.end());
}
HandleDataflowAnalysis::HandleDataflowAnalysis(sim::SimFuncOp function)
    : HandleDataflowAnalysis(getDriverDesign(function)) {}
namespace {
HandleDataflowResult
solveHandles(sim::SimFuncOp function,
             const DenseMap<uint64_t, uint64_t> &driverNets,
             bool dynamicSelectors) {
  HandleDataflowResult result;
  DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
  solver.load<dataflow::DeadCodeAnalysis>();
  solver.load<dataflow::SparseConstantPropagation>();
  solver.load<dataflow::IntegerRangeAnalysis>();
  auto *scopedRanges =
      dynamicSelectors
          ? solver.load<ScopedIntegerRangeAnalysis>(function.getOperation())
          : nullptr;
  if (dynamicSelectors)
    solver.load<SparseLogicBitAnalysis>();
  solver.load<ReferenceAnalysis>(driverNets, scopedRanges, dynamicSelectors);
  if (failed(solver.initializeAndRun(function)))
    return result;
  auto record = [&](Value value) {
    if (scopedRanges && isa<sim::LogicType>(value.getType()) &&
        scopedRanges->excludesXZ(value))
      result.knownLogicValues.insert(value);
    if (resourceKind(value.getType()) == schedule::ComputeResourceKind::Unknown)
      return;
    auto *state = solver.lookupState<ReferenceLattice>(value);
    if (!state || !state->getValue().initialized)
      return;
    result.facts.try_emplace(value, state->getValue().target);
    result.certificates.try_emplace(value, state->getValue().proof);
  };
  function.walk([&](Operation *op) {
    for (Value value : op->getResults())
      record(value);
    for (auto &region : op->getRegions())
      for (auto &block : region) {
        if (auto *state = solver.lookupState<dataflow::Executable>(
                solver.getProgramPointBefore(&block));
            state && !state->isLive())
          result.nonExecutableBlocks.insert(&block);
        for (Value value : block.getArguments())
          record(value);
      }
  });
  return result;
}
} // namespace
HandleDataflowResult
HandleDataflowAnalysis::analyze(sim::SimFuncOp function) const {
  if (function.isExternal() || function.getBody().empty())
    return HandleDataflowResult();
  bool hasHandles = false, dynamicSelectors = false, driverHandles = false;
  llvm::hash_code shape = 0;
  auto inspect = [&](Value value) {
    hasHandles |=
        resourceKind(value.getType()) != schedule::ComputeResourceKind::Unknown;
    driverHandles |= isa<sim::DriverType>(value.getType());
  };
  function.walk([&](Operation *op) {
    shape = llvm::hash_combine(shape, op->getName().getAsOpaquePointer());
    dynamicSelectors |=
        isa<sim::SimRefArrayElementOp, sim::SimRefDynExtractOp,
            schedule::NativeWordViewOp, sim::SimDriverArrayElementOp,
            sim::SimDriverDynExtractOp>(op);
    for (auto value : op->getResults())
      inspect(value);
    for (auto &region : op->getRegions())
      for (auto &block : region)
        for (auto value : block.getArguments()) {
          inspect(value);
          shape = llvm::hash_combine(shape, value.getType());
        }
  });
  if (!hasHandles)
    return HandleDataflowResult();
  // A function without driver handles never reads the driver map. Key it as
  // the function constructor does, so every design index shares its result.
  static const uint64_t noDriverDigest =
      HandleDataflowAnalysis(sim::SimDesignOp()).driverDigest;
  HandleRevision revision{OperationFingerPrint(function), shape,
                          driverHandles ? driverDigest : noDriverDigest};
  HandleResultCache &cache = getHandleResultCache();
  if (auto cached = cache.lookup(function, revision))
    return *cached;
  auto solved = std::make_shared<const HandleDataflowResult>(
      solveHandles(function, driverNets, dynamicSelectors));
  cache.insert(function, revision, solved);
  return *solved;
}
HandleFacts HandleDataflowAnalysis::derive(sim::SimFuncOp function) const {
  return analyze(function).facts;
}
HandleFacts deriveHandleFacts(sim::SimFuncOp function) {
  return HandleDataflowAnalysis(function).derive(function);
}
} // namespace obelisk::analysis
