//===- HandleDataflowAnalysis.cpp - SSA reference dataflow
//------------------===//
#include "obelisk/Analysis/HandleDataflowAnalysis.h"
#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/SparseAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "llvm/ADT/TypeSwitch.h"

#include <climits>

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
                    const DenseMap<uint64_t, uint64_t> &driverNets)
      : SparseForwardDataFlowAnalysis(solver), driverNets(driverNets) {}
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
        bool wholeRoot = !p.dynamic && p.low == 0 && p.width == p.rootWidth &&
                         c.constantAddress;
        // Array selectors validate against their declared parent range before
        // adding its fixed root offset (LRM 7.4.5). A bounded array field is
        // therefore as direct as a whole-root array. Packed part-selects still
        // require the whole-root proof because they can overlap a boundary.
        bool boundedArray = array && !p.dynamic && c.constantAddress &&
                            c.laneKnown && !c.clipped;
        bool lane = !p.dynamic && c.laneKnown && *width &&
                    (!array || c.width % *width == 0);
        c.constantAddress = false;
        c.laneKnown = lane;
        if (lane) {
          c.width = *width;
          c.stride = array ? *width : 1;
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
        .Case<sim::SimRefDynExtractOp, sim::SimDriverDynExtractOp>(
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
         function.getArgAttr(index, "simulation.user_net_driver")))
      return propagateIfChanged(lattice, lattice->join(value));
    value.target = {resource, {}, {}, 0, *width, *width};
    auto rootType = function.getArgAttrOfType<TypeAttr>(
        index, sim::metadata::descriptorRootType);
    if (rootType) {
      auto root = sim::getProvenanceSpan(rootType.getValue());
      auto low = function.getArgAttrOfType<IntegerAttr>(
          index, sim::metadata::descriptorLow);
      if (!root || !low || low.getValue().isNegative() ||
          low.getValue().getActiveBits() > 64 || low.getUInt() > *root ||
          *width > *root - low.getUInt())
        return propagateIfChanged(lattice,
                                  lattice->join(ReferenceValue::unknown()));
      value.target.low = low.getUInt();
      value.target.rootWidth = *root;
    }
    auto capture = function.getArgAttrOfType<sim::CaptureKindAttr>(
        index, sim::metadata::captureKind);
    auto descriptor = function.getArgAttrOfType<IntegerAttr>(
        index, sim::metadata::descriptorId);
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
    propagateIfChanged(lattice, lattice->join(value));
  }
  const DenseMap<uint64_t, uint64_t> &driverNets;
};
} // namespace

HandleDataflowAnalysis::HandleDataflowAnalysis(sim::SimDesignOp design)
    : design(design ? design.getOperation() : nullptr) {}
HandleDataflowResult
HandleDataflowAnalysis::analyze(sim::SimFuncOp function) const {
  HandleDataflowResult result;
  if (function.isExternal() || function.getBody().empty())
    return result;
  bool hasHandles = false, hasDrivers = false;
  auto inspect = [&](Value value) {
    hasHandles |=
        resourceKind(value.getType()) != schedule::ComputeResourceKind::Unknown;
    hasDrivers |= isa<sim::DriverType>(value.getType());
  };
  function.walk([&](Operation *op) {
    for (auto value : op->getResults())
      inspect(value);
    for (auto &region : op->getRegions())
      for (auto &block : region)
        for (auto value : block.getArguments())
          inspect(value);
  });
  if (!hasHandles)
    return result;
  // Most functions carry no drivers. In particular, the convenience entry
  // point must not scan the entire design once per storage-only function.
  if (hasDrivers && !driverNets) {
    driverNets.emplace();
    if (design)
      for (auto driver :
           cast<sim::SimDesignOp>(design).getOps<sim::SimDriverDeclOp>())
        (*driverNets)[driver.getId()] = driver.getNetId();
  }
  const DenseMap<uint64_t, uint64_t> emptyDriverNets;
  DataFlowSolver solver(DataFlowConfig().setInterprocedural(false));
  solver.load<dataflow::DeadCodeAnalysis>();
  solver.load<dataflow::SparseConstantPropagation>();
  solver.load<ReferenceAnalysis>(driverNets ? *driverNets : emptyDriverNets);
  if (failed(solver.initializeAndRun(function)))
    return result;
  auto record = [&](Value value) {
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
      for (auto &block : region)
        for (Value value : block.getArguments())
          record(value);
  });
  return result;
}
HandleFacts HandleDataflowAnalysis::derive(sim::SimFuncOp function) const {
  return analyze(function).facts;
}
HandleFacts deriveHandleFacts(sim::SimFuncOp function) {
  return HandleDataflowAnalysis(function->getParentOfType<sim::SimDesignOp>())
      .derive(function);
}
} // namespace obelisk::analysis
